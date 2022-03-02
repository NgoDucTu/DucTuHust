#include "DHT.h"
#include "FS.h"
#include "SD_MMC.h"
#include "soc/soc.h"           // Disable brownour problems
#include "soc/rtc_cntl_reg.h"  // Disable brownour problems
#include <SPI.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino-FreeRtos.h>


long startTask1, endTask1, startTask2, endTask2, startTask3, endTask3;

#define DHTTYPE DHT11 // DHT 11
uint8_t DHTPin = 3; 
DHT dht(DHTPin, DHTTYPE);

typedef struct queue_unit {
  float Temperature;
  float Humidity;
  String dayStamp;
  String timeStamp;
} queue_unit;

String formattedDate;
String dataMessage;

// Replace with your network credentials
const char* ssid     = "MINH TIEN";
const char* password = "16052008";


// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Define task, queue, semaphore  
TaskHandle_t Task1, Task2, Task3, Task4;

QueueHandle_t queue;

void setup() {
  Serial.begin(115200);
  pinMode(DHTPin, INPUT);
  dht.begin();
  
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");
  
  // Initialize a NTPClient to get time
  timeClient.begin();
  timeClient.setTimeOffset(19800);

  // set LCD
  
  lcd.init();
  lcd.backlight();
  
  // Initialize SD card
  if(!SD_MMC.begin()){
    Serial.println("SD Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }
  Serial.println("Initializing SD card...");
  if (!SD_MMC.begin()) {
    Serial.println("ERROR - SD card initialization failed!");
    return;    // init failed
  }
  File file = SD_MMC.open("/data.txt");
  if(!file) {
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    writeFile(SD_MMC, "/data.txt", "Date, Time, Temperature, Humidity \r\n");
  }
  else {
    Serial.println("File already exists");  
  }
  file.close();

   queue = xQueueCreate(5,sizeof(queue_unit));
   xTaskCreatePinnedToCore(
                    Read_TempHum,   /* Task function. */
                    "Task1",     /* name of task. */
                    2048,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    3,           /* priority of the task */
                    &Task1,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */                  

  //create a task that will be executed in the Task2code() function, with priority 1 and executed on core 1
  xTaskCreatePinnedToCore(
                    getTimeStamp,   /* Task function. */
                    "Task2",     /* name of task. */
                    2048,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    2,           /* priority of the task */
                    &Task2,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */
  xTaskCreatePinnedToCore(
                    logSDCard,   /* Task function. */
                    "Task3",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &Task3,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */
   xTaskCreatePinnedToCore(
                    RTC_display,   /* Task function. */
                    "Task4",     /* name of task. */
                    2048,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &Task4,      /* Task handle to keep track of created task */
                    1);          /* pin task to core 1 */
   
 
}
void loop() {
}
// Function to get temperature
void Read_TempHum(void *)
{
  while(1){
      queue_unit temp;
      startTask1 = xTaskGetTickCount();
      temp.Temperature = dht.readTemperature(); 
      temp.Humidity = dht.readHumidity();
      xQueueSend(queue,&temp,portMAX_DELAY); 
      Serial.print("Temperature = ");
      Serial.println(temp.Temperature);
      Serial.print("Humidity = ");
      Serial.println(temp.Humidity);
      endTask1 = xTaskGetTickCount();
      vTaskDelay(5000);
  }
}
// Function to get date and time from NTPClient
void getTimeStamp(void*) {
  while(1){
      queue_unit temp;
      xQueueReceive(queue, &temp, portMAX_DELAY);
      startTask2 = xTaskGetTickCount();
      while(!timeClient.update()) {
      timeClient.forceUpdate();
      }
      formattedDate = timeClient.getFormattedDate();
      Serial.println(formattedDate);
      int splitT = formattedDate.indexOf("T");
      temp.dayStamp = formattedDate.substring(0, splitT);
      Serial.println(temp.dayStamp);
      // Extract time
      temp.timeStamp = formattedDate.substring(splitT+1, formattedDate.length()-1);
      Serial.println(temp.timeStamp);
      
      xQueueSend(queue,&temp,10); 
      
      xTaskNotifyGive( Task3 );
            
      endTask2 = xTaskGetTickCount();
      vTaskDelay(5000);
  }
}
// Write the sensor readings on the SD card
void logSDCard(void*) {
  while(1){

      ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
      queue_unit temp;
      xQueuePeek(queue, &temp, portMAX_DELAY);
      
      startTask3 = xTaskGetTickCount();
      dataMessage =  String(temp.dayStamp) + "," + String(temp.timeStamp) + "," + 
                String(temp.Temperature) + "," + String(temp.Humidity)+ "," + String(startTask1)+ "," + String(endTask1)+ "," + String(startTask2)+ "," + String(endTask1)+ "," + String(startTask3)+ "," + String(endTask3) + "\r\n";
      Serial.print("Save data: ");
      Serial.println(dataMessage);
      appendFile(SD_MMC, "/data.txt", dataMessage.c_str());
      //xSemaphoreGive( sema );
      endTask3 = xTaskGetTickCount();

      xTaskNotifyGive( Task4 );
      
      vTaskDelay(5000);

  }
}

void RTC_display(void*){
  while(1)
  {
  
  ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
  queue_unit temp;
  xQueueReceive(queue, &temp, portMAX_DELAY);
  char _buffer1[12];
  sprintf(_buffer1,"TEMP = %02u ",temp.Temperature);
  Serial.print(_buffer1);
  lcd.setCursor(0,0);
  lcd.print(_buffer1);
  
  char _buffer2[12];
  sprintf(_buffer2,"HUM = %02u ",temp.Humidity);
  Serial.println(_buffer2);
  lcd.setCursor(0,1);
  lcd.print(_buffer2);
  }
}










// Write to the SD card (DON'T MODIFY THIS FUNCTION)
void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);
  File file = fs.open(path, FILE_WRITE);
  if(!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if(file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}
// Append data to the SD card (DON'T MODIFY THIS FUNCTION)
void appendFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Appending to file: %s\n", path);
  File file = fs.open(path, FILE_APPEND);
  if(!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if(file.print(message)) {
    Serial.println("Message appended\n");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}
