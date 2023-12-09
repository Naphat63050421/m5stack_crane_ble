/*
    Video: https://www.youtube.com/watch?v=oCMOYS71NIU
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
    Ported to Arduino ESP32 by Evandro Copercini
    updated by chegewara

   Create a BLE server that, once we receive a connection, will send periodic notifications.
   The service advertises itself as: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
   And has a characteristic of: beb5483e-36e1-4688-b7f5-ea07361b26a8

   The design of creating the BLE server is:
   1. Create a BLE Server
   2. Create a BLE Service
   3. Create a BLE Characteristic on the Service
   4. Create a BLE Descriptor on the characteristic
   5. Start the service.
   6. Start advertising.

   A connect hander associated with the server starts a background task that performs notification
   every couple of seconds.
*/
#include "Arduino.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <string.h>
#include "M5Stack.h"
#include "M5GFX.h"
#include "HX711.h"

#define LOADCELL_DOUT_PIN       33
#define LOADCELL_SCK_PIN        32
#define readingsToAverage       70

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define SERIAL_DEBUG

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;;
//bool firsttime_flag = false;
uint8_t txValue = 0;
HX711 scale;
M5GFX display;
M5Canvas canvas(&display);

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

void setup()
{
  Serial.begin(115200);

  // Create the BLE Device
  BLEDevice::init("CRANE_SCALE_DEVICE");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pTxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );

  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic * pRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      BLECharacteristic::PROPERTY_WRITE
                                          );

  //  pRxCharacteristic->setCallbacks(new MyCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();

  Serial.println("Waiting a client connection to notify...");

  M5.begin();
  M5.Power.begin();
  display.begin();
  canvas.setColorDepth(1);
  canvas.createSprite(display.width(), display.height());
  canvas.setTextDatum(MC_DATUM);
  canvas.setPaletteColor(1, YELLOW);

  canvas.drawString("Calibration sensor....", 160, 80);
  canvas.pushSprite(0, 0);

  //config Freq
  rtc_cpu_freq_config_t config;
  Serial.println("Initializing the scale");
  rtc_clk_cpu_freq_get_config(&config);
  rtc_clk_cpu_freq_to_config(RTC_CPU_FREQ_80M, &config);
  rtc_clk_cpu_freq_set_config_fast(&config);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(11.2); 
  scale.tare(); 

}

void loop()
{
  M5.update();
  float sum_weight = 0.0;
  float weight = 0.0;
  float reading_averg[readingsToAverage];
  canvas.fillSprite(BLACK);                                                // back ground สีดำ
  canvas.setTextSize(1);                                                   // text size 1
  canvas.drawString("Connect the Weight Unit to PortB(G33,G32)", 160, 40); // text
  canvas.drawString("Click Btn A for Calibration", 160, 80);               // text

  weight = scale.get_units();
  weight = weight / 1000;
  // เมื่อมีค่าน้ำหนักจะเริ่มการคำนวน
  if (weight > 0.50) // +- load cell เวลาขยับอาจจะเกิดค่าน้ำหนักทำให้เกิดการคำนวนได้
  {
    canvas.setTextSize(3);
    canvas.drawString("Calculated..", 160, 150);
    // ตอนคำนวนอาจจะมีstatusบอกว่าอีกกี่วินาทีจะได้น้ำหนักตอนคำนวน (แต่ยังไม่ได้ทำ)
    canvas.pushSprite(0, 0);
    for (int i = 0; i < readingsToAverage; i++) // for loop คำนวนค่าน้ำหนัก AVERG  70 ตัว loop time เวลาประมาณ 5 วินาที ของ 70 ตัว
                                                //(แต่จะเปลี่ยนเป็น Timer function "millis()" จับเวลาแทน)
    {
      reading_averg[i] = scale.get_units();
      sum_weight += reading_averg[i];
    }
    sum_weight = sum_weight / readingsToAverage; // คำนวน Averange
    sum_weight = sum_weight / 1000;              // แปลงหน่วย
    if (sum_weight < 0.00)                       // เผื่อถ้าค่าข้อมูลติดลบตอนยังไม่เจอน้ำหนักจะให้ Fillค่า 0.0 kg เอาไว้ (อาจจะไม่มีก็ได้)
    {
      sum_weight = 0.00;
    }
    canvas.drawString("Weight " + String(sum_weight) + "Kg", 160, 150);
    canvas.pushSprite(0, 0);

    double datatest = 12.0f; //test data
    if (deviceConnected)
    {
      pTxCharacteristic->setValue(datatest); //test send data to APPICATION
      Serial.println(datatest); // debug data
      pTxCharacteristic->notify();
      txValue++;
      delay(10); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
    }
    // disconnecting
    if (!deviceConnected && oldDeviceConnected)
    {
      delay(500);                  // give the bluetooth stack the chance to get things ready
      pServer->startAdvertising(); // restart advertising
      Serial.println("start advertising");
      oldDeviceConnected = deviceConnected;
    }
    // connecting
    if (deviceConnected && !oldDeviceConnected)
    {
      // do stuff here on connecting
      oldDeviceConnected = deviceConnected;
    }

    delay(5000); // ให้ดูผลการชั่งน้ำหนัก 5 วินาที
  }
  canvas.setTextSize(3);                            // config text size
  canvas.drawString(">>Intput Weight<<", 160, 150); // text
  // Serial.println(weight); // text
  canvas.pushSprite(0, 0); // pushSprite ออกหน้าจอ
  // Serial.print("raw_weight: ");
  // Serial.println(weight);
  // Serial.print("Pull_weight: ");
  // Serial.println(sum_weight);
}