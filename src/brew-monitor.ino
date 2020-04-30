#include <stdio.h>

#include <ESP8266WiFi.h>

#include <TFT_eSPI.h>
#include <DS18B20.h>

#define BREW_TEMP_MIN_C 28.0f
#define BREW_TEMP_MAX_C 28.5f

#define OVERHEAT_LIMIT_C 40.0f
#define OVERHEAT_RESET_TEMP_C 30.0f

#define SENSOR_PIN 0

#define RELAIS_HEATER_PIN 16
#define RELAIS_COOLER_PIN 5

#define LED_1_PIN 2

#define ORANGE 0xFBE0

#define SENSOR_ADDRESS_LENGTH 8

typedef enum {
    SENSOR_BREW = 0,
    SENSOR_AMBIENT,
    SENSOR_COUNT
} sensors_e;

static const uint8_t sensorAddresses[][SENSOR_ADDRESS_LENGTH] = {
    { 40, 32, 161, 220, 6, 0, 0, 148 },
    { 40, 255, 93, 27, 0, 22, 2, 103 },
};

typedef struct globalState_s {
    float temperatures[SENSOR_COUNT];
    bool heaterOn;
    bool overheated;
    uint32_t lastHeaterCycleS;
    uint32_t lastOffCycleS;
    uint64_t cycleStartMs;
} globalState_t;

globalState_t state;

DS18B20 ds(0);

TFT_eSPI tft = TFT_eSPI(240, 320);

/*
//! Long time delay, it is recommended to use shallow sleep, which can effectively reduce the current consumption
void espDelay(uint32_t us)
{   
    esp_sleep_enable_timer_wakeup(us);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_light_sleep_start();
}
*/

void sensorsDetect(void)
{
    while (ds.selectNext()) {
        switch (ds.getFamilyCode()) {
        case MODEL_DS18S20:
            Serial.println("Model: DS18S20/DS1820");
            break;
        case MODEL_DS1822:
            Serial.println("Model: DS1822");
            break;
        case MODEL_DS18B20:
            Serial.println("Model: DS18B20");
            break;
        default:
            Serial.println("Unrecognized Device");
            break;
        }

        uint8_t address[8];
        ds.getAddress(address);

        Serial.print("Address:");
        for (uint8_t i = 0; i < 8; i++) {
            Serial.print(" ");
            Serial.print(address[i]);
        }

        Serial.println();

        Serial.print("Resolution: ");
        Serial.println(ds.getResolution());

        Serial.print("Power Mode: ");
        if (ds.getPowerMode()) {
            Serial.println("External");
        } else {
            Serial.println("Parasite");
        }

        Serial.printf("Temperature: %f C\n", ds.getTempC());
    }
}

void setup(void)
{
    Serial.begin(115200);
    Serial.println("Start");

    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(1);

    sensorsDetect();

    tft.init();
    tft.setRotation(3);
    tft.setTextDatum(MC_DATUM);
    tft.fillScreen(TFT_BLACK);
    tft.setTextFont(4);

    pinMode(RELAIS_HEATER_PIN, OUTPUT);
    pinMode(RELAIS_COOLER_PIN, OUTPUT);

    digitalWrite(RELAIS_HEATER_PIN, 1);
    digitalWrite(RELAIS_COOLER_PIN, 1);

    pinMode(LED_1_PIN, OUTPUT);
}

void readSensors(void)
{
    static unsigned sensorIndex = 0;

    bool selected = ds.select((uint8_t *)sensorAddresses[sensorIndex]);
    if (selected) {
        state.temperatures[sensorIndex] = ds.getTempC();
    }

    sensorIndex = (sensorIndex + 1) % SENSOR_COUNT;
}

void setHeater(bool heaterOn)
{
    if (state.heaterOn != heaterOn) {
        uint64_t nowMs = millis();
        if (state.heaterOn) {
            state.lastHeaterCycleS = (nowMs - state.cycleStartMs) / 1000;
        } else {
            state.lastOffCycleS = (nowMs - state.cycleStartMs) / 1000;
        }
        state.cycleStartMs = nowMs;

        state.heaterOn = heaterOn;
    }
}

void updateState(void)
{
    if (state.temperatures[SENSOR_BREW] >= OVERHEAT_LIMIT_C
        || state.temperatures[SENSOR_AMBIENT] >= OVERHEAT_LIMIT_C) {
        state.overheated = true;
        setHeater(false);
    } else if (state.temperatures[SENSOR_BREW] < OVERHEAT_RESET_TEMP_C
        && state.temperatures[SENSOR_AMBIENT] < OVERHEAT_RESET_TEMP_C) {
        state.overheated = false;
    }

    if (!state.overheated) {
        if (state.temperatures[SENSOR_BREW] <= BREW_TEMP_MIN_C) {
            setHeater(true);
        } else if (state.temperatures[SENSOR_BREW] >= BREW_TEMP_MAX_C) {
            setHeater(false);
        }
    }
}

void updateOutput(void)
{
    digitalWrite(RELAIS_HEATER_PIN, !(state.heaterOn && !state.overheated));
}

void updateDisplay(void)
{

/*
Geometry for 320 x 240 display:

Top row,
0, 0, 319, 239
*/

    static uint64_t lastRunTimeMs = 0;
    
    uint64_t nowMs = millis();
    if (nowMs - lastRunTimeMs >= 200) {
        lastRunTimeMs = nowMs;

        tft.fillRect(10, 0, 310, 210, TFT_BLACK);

        tft.setTextColor(state.overheated ? TFT_RED : state.heaterOn ? ORANGE : TFT_GREEN);

        tft.setTextSize(3);
        tft.setCursor(10, 0);
        tft.printf("B %.2f", state.temperatures[SENSOR_BREW]);

        tft.setCursor(10, 70);
        tft.printf("A %.2f", state.temperatures[SENSOR_AMBIENT]);

        tft.setTextSize(1);
        tft.setCursor(10, 140);
        tft.print(state.overheated ? "OVER" : state.heaterOn ? "HEATER" : "");

        tft.setCursor(10, 164);
        uint32_t timeS = (millis() - state.cycleStartMs) / 1000;
        tft.printf("Current: %d:%02d", timeS / 60, timeS % 60);

        tft.setCursor(10, 188);
        tft.printf("Heater: %d:%02d, Off: %d:%02d", state.lastHeaterCycleS / 60, state.lastHeaterCycleS % 60, state.lastOffCycleS / 60, state.lastOffCycleS % 60);
    }
}

/*void wifi_scan(void)
{
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);

    tft.drawString("Scan Network", tft.width() / 2, tft.height() / 2);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int16_t n = WiFi.scanNetworks();
    tft.fillScreen(TFT_BLACK);
    if (n == 0) {
        tft.drawString("no networks found", tft.width() / 2, tft.height() / 2);
    } else {
        tft.setCursor(0, 0);
        Serial.printf("Found %d net\n", n);
        for (int i = 0; i < n; ++i) {
            char buff[512];
            sprintf(buff,
                    "[%d]:%s(%d)",
                    i + 1,
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i));
            tft.println(buff);
        }
    }
    WiFi.mode(WIFI_OFF);
}*/

void loop(void)
{
    readSensors();

    updateState();

    updateOutput();

    updateDisplay();
}
