#include "temp_humi_monitor.h"
#include <Wire.h>
#include <ArduinoJson.h>
#include "task_webserver.h"

DHT20 dht20;
// I2C LCD: address 33 (0x21), 16x2
LiquidCrystal_I2C lcd(33, 16, 2);

static DisplayState computeDisplayState(uint8_t tempLevel, uint8_t humiLevel);
static void updateLcd(float temperature, float humidity, DisplayState state);
static void sendSensorToWeb(float temperature, float humidity);

void temp_humi_monitor(void *pvParameters)
{
  Wire.begin(11, 12);
  dht20.begin();

  lcd.begin();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DHT20 starting...");
  lcd.setCursor(0, 1);
  lcd.print("Please wait");
  vTaskDelay(pdMS_TO_TICKS(1500));

  uint8_t lastTempLevel = glob_temp_level;
  uint8_t lastHumiLevel = glob_humi_level;

  for (;;)
  {
    dht20.read();
    float temperature = dht20.getTemperature();
    float humidity    = dht20.getHumidity();

    if (isnan(temperature) || isnan(humidity))
    {
      Serial.println("Failed to read from DHT20!");
      temperature = -1.0f;
      humidity    = -1.0f;
    }

    // Cập nhật giá trị thô toàn cục
    glob_temperature = temperature;
    glob_humidity    = humidity;

    // Phân loại mức nhiệt theo ngưỡng runtime
    uint8_t tempLevel = TEMP_LEVEL_NORMAL;
    if (temperature < tempColdThreshold)
      tempLevel = TEMP_LEVEL_COLD;
    else if (temperature > tempHotThreshold)
      tempLevel = TEMP_LEVEL_HOT;

    // Phân loại mức ẩm theo ngưỡng runtime
    uint8_t humiLevel = HUMI_LEVEL_OK;
    if (humidity < humiDryThreshold)
      humiLevel = HUMI_LEVEL_DRY;
    else if (humidity > humiHumidThreshold)
      humiLevel = HUMI_LEVEL_HUMID;

    glob_temp_level = tempLevel;
    glob_humi_level = humiLevel;

    // ====== Semaphore cho Task 1 & 2 ======
    if (tempLevel != lastTempLevel && xTempLedSemaphore != nullptr)
    {
      xSemaphoreGive(xTempLedSemaphore);
      lastTempLevel = tempLevel;
    }

    if (humiLevel != lastHumiLevel && xHumiNeoSemaphore != nullptr)
    {
      xSemaphoreGive(xHumiNeoSemaphore);
      lastHumiLevel = humiLevel;
    }

    // ====== LCD 3 trạng thái ======
    DisplayState state = computeDisplayState(tempLevel, humiLevel);
    glob_display_state = state;
    updateLcd(temperature, humidity, state);

    // Gửi dữ liệu lên webserver qua WebSocket
    sendSensorToWeb(temperature, humidity);

    Serial.print("[DHT20] H: ");
    Serial.print(humidity);
    Serial.print("%  T: ");
    Serial.print(temperature);
    Serial.println(" C");

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

static DisplayState computeDisplayState(uint8_t tempLevel, uint8_t humiLevel)
{
  // CRITICAL nếu quá nóng hoặc quá ẩm
  if (tempLevel == TEMP_LEVEL_HOT || humiLevel == HUMI_LEVEL_HUMID)
    return DISPLAY_STATE_CRITICAL;

  // WARNING nếu hơi lạnh hoặc hơi khô
  if (tempLevel == TEMP_LEVEL_COLD || humiLevel == HUMI_LEVEL_DRY)
    return DISPLAY_STATE_WARNING;

  // Ngược lại là NORMAL
  return DISPLAY_STATE_NORMAL;
}

static void updateLcd(float temperature, float humidity, DisplayState state)
{
  lcd.clear();

  lcd.setCursor(0, 0);
  switch (state)
  {
  case DISPLAY_STATE_NORMAL:
    lcd.print("State: NORMAL ");
    break;
  case DISPLAY_STATE_WARNING:
    lcd.print("State: WARN   ");
    break;
  case DISPLAY_STATE_CRITICAL:
    lcd.print("State: CRITIC!");
    break;
  }

  lcd.setCursor(0, 1);
  lcd.print("T:");
  lcd.print(temperature, 1);
  lcd.print("C ");
  lcd.print("H:");
  lcd.print(humidity, 0);
  lcd.print("%");
}

static void sendSensorToWeb(float temperature, float humidity)
{
  StaticJsonDocument<128> doc;
  doc["page"] = "sensor";
  doc["temp"] = temperature;
  doc["humi"] = humidity;

  String json;
  serializeJson(doc, json);
  Webserver_sendata(json);
}
