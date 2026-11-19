#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HX711.h>
#include <vector>
#include <LittleFS.h>

HX711 scale;                    
#define DT 32                  
#define SCK 26                 

#define I2C_SCK 22
#define I2C_SDA 21

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const float calibration_factor = 910.21;       

float last_full_weight = 0.0; // полный вес до ингредиента
float last_weight = 0.0;      // вес текущего добавленного ингредиента
float temp_weight = 0.0;      // временная переменная для отслеживания изменений

std::vector<float> weights_list;   // массив для хранения весов ингредиентов

enum State {IDLE, DISPENSING, LOCKED};
State state = IDLE;

unsigned long lock_timer = 0; // таймер для блокировки веса
const unsigned long lock_time = 3000; // время для блокировки

// для вывода режима на дисплей таймер и флаг
unsigned long show_locked_timer = 0;
bool is_showing_locked = false;

String getTimeString() { // время для лога
  unsigned long totalSeconds = millis() / 1000;
  unsigned long seconds = totalSeconds % 60;
  unsigned long minutes = (totalSeconds / 60) % 60;
  unsigned long hours = (totalSeconds / 3600);
  char timeBuf[12];
  sprintf(timeBuf, "[%02lu:%02lu:%02lu]", hours, minutes, seconds);
  return String(timeBuf);
}

void setup(){
  Serial.begin(115200);  
  delay(500);  

  if(!LittleFS.begin(true)){
    Serial.println("LittleFS Mount Failed");
  } else {
    Serial.println("LittleFS Ready.");
  }
  
  Wire.begin(I2C_SDA, I2C_SCK);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED ERROR");
    for(;;);
  }
  
  display.clearDisplay();
  Serial.println("oled init");     

  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Tare reset.");
  display.display();

  scale.begin(DT, SCK);           
  scale.set_scale(calibration_factor); // калибруем весы                                                  
  Serial.println("load cell init " + String(scale.is_ready() ? "OK" : "ERROR"));                                                                  
  delay(2000);
  scale.tare(); // обнуляем весы
  display.clearDisplay();


  // вывод предыдущего лога
  Serial.println("\n");
  Serial.println("        PREVIOUS LOG:");
  Serial.println("\n");
  
  File fileRead = LittleFS.open("/log.txt", FILE_READ);
  if (!fileRead || fileRead.size() == 0) {
    Serial.println("No log found.");
  } else {
    while (fileRead.available()) {
      Serial.write(fileRead.read()); 
    }
    fileRead.close();
  }
  Serial.println("\n");
}


void loop() {
  float weight = scale.get_units(3);  // вес в данный момент


  if (weight < -10.0) {
    scale.tare();
    last_full_weight = 0.0;
    last_weight = 0.0;
    weights_list.clear();
  }

  if (weight > -1.5 && weight < 1.5) {
    weight = 0.0;
  }

  if (weight < 0.0) {
    weight = 0.0;
  }


  switch(state) {
    
    case IDLE:
      // переходим из ожидания в насыпание от 2 грамм
      if (weight - last_full_weight > 10.0) {
        Serial.println("Dispensing start...");
        temp_weight = weight;  
        lock_timer = millis();
        is_showing_locked = false;
        state = DISPENSING;
      }
      break;

    case DISPENSING:
      if (abs(weight - temp_weight) < 1.0) { // если изменение веса меньше 1 грамма то включаем таймер
        if (millis() - lock_timer >= lock_time) {
          state = LOCKED; // если 3 сек изменение меньше грамма то блокируем
        }
      } else {
        temp_weight = weight;
        lock_timer = millis();
      }
      break;

    case LOCKED:
      last_weight = weight - last_full_weight;
      last_full_weight = weight;

      weights_list.push_back(last_weight);

      // логирование
      String logEntry = getTimeString() + " #" + String(weights_list.size()) + 
                        " | Added: " + String(last_weight, 1) + " g" + 
                        " | Total: " + String(last_full_weight, 1) + " g";
      Serial.println(logEntry);

      File file = LittleFS.open("/log.txt", FILE_APPEND);
      if(file){
        file.println(logEntry);
        file.close();
      }

      is_showing_locked = true;
      show_locked_timer = millis();
      state = IDLE;
      break;
  }

  // вывод на дисплей
  display.clearDisplay();
  
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  if (is_showing_locked) {
    display.print("LOCKED!");
    if (millis() - show_locked_timer > 2000) {
      is_showing_locked = false;
    }
  } 
  else if (state == DISPENSING) {
    int dots = (millis() / 400) % 4; 
    String dispText = "DISP";
    for(int i = 0; i < dots; i++) {
      dispText += "."; // бегающие точки для красоты
    }
    display.print(dispText);
  } 
  else {
    display.print("WAITING");
  }

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print("INGR:");
  display.setTextSize(2);
  display.setCursor(40, 20);
  display.print("+"); 
  display.print(last_weight, 0); 
  display.print(" g");

  display.setTextSize(1);
  display.setCursor(0, 50);
  display.print("TOTAL:");
  display.setTextSize(2);
  display.setCursor(40, 46);
  display.print(last_full_weight, 0); 
  display.print(" g");

  display.display();

  delay(50);
}