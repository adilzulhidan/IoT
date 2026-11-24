#define BLYNK_PRINT Serial
#include "secrets.h"
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Pin mengikuti sambungan yang diberikan.
constexpr uint8_t PIN_TRIG = 18;
constexpr uint8_t PIN_ECHO = 19;
constexpr uint8_t PIN_RELAY = 26;
constexpr uint8_t PIN_LCD_SDA = 21;
constexpr uint8_t PIN_LCD_SCL = 22;

// Ubah sesuai jarak dari muka sensor ke dasar/acuan pengukuran (cm).
// Contoh: jika sensor 35 cm di atas dasar, isi 35.0.
constexpr float SENSOR_TO_BOTTOM_CM = 35.0f;

// Banyak modul relay 5 V aktif-LOW. Jika relay bekerja terbalik,
// ubah menjadi false setelah memeriksa perilaku modul Anda.
constexpr bool RELAY_ACTIVE_LOW = true;

constexpr float WATER_LOW_CM = 5.0f;
constexpr float WATER_HIGH_CM = 15.0f;
constexpr uint32_t SENSOR_INTERVAL_MS = 2000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
constexpr uint32_t ECHO_TIMEOUT_US = 30000;

// Alamat LCD umum adalah 0x27; jika layar kosong, cek alamat modul I2C.
LiquidCrystal_I2C lcd(0x27, 16, 2);
BlynkTimer timer;

bool automaticMode = true;
bool relayOn = false;
bool haveValidReading = false;
float distanceCm = NAN;
float waterHeightCm = NAN;
uint32_t lastWiFiAttempt = 0;

void setRelay(bool on) {
  relayOn = on;
  const uint8_t onLevel = RELAY_ACTIVE_LOW ? LOW : HIGH;
  digitalWrite(PIN_RELAY, on ? onLevel : !onLevel);
}

void printLcdLine(uint8_t row, const String &message) {
  lcd.setCursor(0, row);
  String padded = message;
  if (padded.length() > 16) padded.remove(16);
  while (padded.length() < 16) padded += ' ';
  lcd.print(padded);
}

void showError(const char *message) {
  haveValidReading = false;
  // Pembacaan gagal: matikan keluaran otomatis agar status lama tidak tertahan.
  if (automaticMode) setRelay(false);
  printLcdLine(0, "Sensor: ERROR");
  printLcdLine(1, message);
  Serial.printf("Pembacaan tidak valid: %s\n", message);
}

void readAndPublish() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(3);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  const unsigned long duration = pulseIn(PIN_ECHO, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) {
    showError("Tiada pantulan");
    return;
  }

  distanceCm = duration * 0.0343f / 2.0f;
  if (distanceCm < 20.0f || distanceCm > 400.0f) {
    showError("Jarak luar 20-400");
    return;
  }

  waterHeightCm = SENSOR_TO_BOTTOM_CM - distanceCm;
  if (waterHeightCm < 0.0f) waterHeightCm = 0.0f;
  haveValidReading = true;

  if (automaticMode) {
    // Histeresis: di antara 5 dan 15 cm, pertahankan keadaan relay.
    if (waterHeightCm < WATER_LOW_CM) setRelay(false);
    else if (waterHeightCm >= WATER_HIGH_CM) setRelay(true);
  }

  printLcdLine(0, "Air: " + String(waterHeightCm, 1) + " cm");
  printLcdLine(1, String(automaticMode ? "AUTO " : "MANUAL ") +
                  "Relay:" + (relayOn ? "ON" : "OFF"));
  Serial.printf("Jarak=%.1f cm, tinggi=%.1f cm, mode=%s, relay=%s\n",
                distanceCm, waterHeightCm,
                automaticMode ? "AUTO" : "MANUAL", relayOn ? "ON" : "OFF");

  if (Blynk.connected()) {
    Blynk.virtualWrite(V0, distanceCm);
    Blynk.virtualWrite(V1, waterHeightCm);
  }
}

BLYNK_WRITE(V2) {
  automaticMode = param.asInt() != 0;
  Serial.printf("Mode: %s\n", automaticMode ? "AUTO" : "MANUAL");
  if (!automaticMode) Blynk.syncVirtual(V3);
}

BLYNK_WRITE(V3) {
  if (!automaticMode) setRelay(param.asInt() != 0);
}

BLYNK_CONNECTED() {
  // Ambil kembali pilihan mode dan perintah manual terakhir dari Blynk.
  Blynk.syncVirtual(V2, V3);
}

void maintainConnections() {
  if (WiFi.status() != WL_CONNECTED) {
    const uint32_t now = millis();
    if (now - lastWiFiAttempt >= WIFI_RETRY_INTERVAL_MS) {
      lastWiFiAttempt = now;
      Serial.println("Mencoba menyambungkan Wi-Fi...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    return;
  }
  if (!Blynk.connected()) Blynk.connect(1000);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_RELAY, OUTPUT);
  setRelay(false);

  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  lcd.init();
  lcd.backlight();
  printLcdLine(0, "Monitoring Air");
  printLcdLine(1, "Memulai...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWiFiAttempt = millis();
  Blynk.config(BLYNK_AUTH_TOKEN);

  timer.setInterval(SENSOR_INTERVAL_MS, readAndPublish);
  timer.setInterval(1000, maintainConnections);
  readAndPublish();
}

void loop() {
  if (Blynk.connected()) Blynk.run();
  timer.run();
}
