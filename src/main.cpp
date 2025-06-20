#include <Arduino.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Konfigurasi pin untuk GPS
#define GPS_RX 17 // Pin RX ESP32 terhubung ke TX GPSY
#define GPS_TX 16 // Pin TX ESP32 terhubung ke RX GPS

// Konfigurasi WiFi
const char *ssid = "Samsung";
const char *password = "tidakada";

// Konfigurasi Telegram Bot
const String botToken = "7821626558:AAHf0ZRPg2cbhHToxfd0gG3bguA9sGf5Yv0";
const String chatId = "1300764591";
const String telegramApiUrl = "https://api.telegram.org/bot" + botToken;

// Konfigurasi Zona Aman (Default)
double HOME_LAT = -6.2088;       // Default latitude rumah
double HOME_LNG = 106.8456;      // Default longitude rumah
double SAFE_ZONE_RADIUS = 200.0; // Default radius zona aman dalam meter

// Inisialisasi objek GPS
TinyGPSPlus gps;
HardwareSerial GPSSerial(1); // Menggunakan Serial1

// Variabel untuk tracking
bool startupMessageSent = false;
bool inSafeZone = true;
bool zoneAlertSent = false;
unsigned long lastTelegramUpdate = 0;
unsigned long lastCommandCheck = 0;
unsigned long lastZoneAlert = 0;                       // Tambahkan variabel untuk tracking alert
const unsigned long TELEGRAM_UPDATE_INTERVAL = 300000; // 5 menit
const unsigned long COMMAND_CHECK_INTERVAL = 5000;     // Cek perintah setiap 5 detik
const unsigned long ZONE_ALERT_INTERVAL = 30000;       // Alert setiap 30 detik

// Tambahkan variabel untuk tracking update ID
unsigned long lastUpdateId = 0;

// Function Prototypes
void sendTelegramMessage(String message);
void sendLocationToTelegram();
void displayGPSInfo();
void checkSafeZone();
double calculateDistance(double lat1, double lng1, double lat2, double lng2);
void handleTelegramCommands();
String getUpdates();
void processCommand(String command);

void setup()
{
  // Inisialisasi Serial Monitor
  Serial.begin(115200);
  Serial.println("\n\nESP32 GPS + Zona Aman + Telegram Bot");
  Serial.println("-------------------------------------");

  // Koneksi WiFi
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi terhubung!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Inisialisasi GPS
  Serial.println("Koneksi GPS:");
  Serial.println("TX GPS -> GPIO17");
  Serial.println("RX GPS -> GPIO16");
  GPSSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS Module Test");
  Serial.println("Status: LED GPS menyala (merah) - OK");
  Serial.println("Menunggu data GPS...");
  Serial.println("-------------------------------------");

  delay(2000);
}

void loop()
{
  // Baca data dari GPS
  while (GPSSerial.available() > 0)
  {
    if (gps.encode(GPSSerial.read()))
    {
      // Tampilkan info GPS setiap 1 detik
      static unsigned long lastPrint = 0;
      if (millis() - lastPrint >= 1000)
      {
        displayGPSInfo();
        checkSafeZone();
        lastPrint = millis();
      }

      // Kirim pesan startup hanya sekali
      if (!startupMessageSent && gps.location.isValid())
      {
        String startupMsg = "🚀 ESP32 GPS Tracker telah aktif!\n";
        startupMsg += "📍 GPS fix diperoleh!\n";
        startupMsg += "🏠 Zona Aman: " + String(SAFE_ZONE_RADIUS) + "m dari rumah\n";
        startupMsg += "📱 Perintah yang tersedia:\n";
        startupMsg += "• /lokasi - Cek posisi saat ini\n";
        startupMsg += "• /setrumah - Set koordinat rumah dari posisi saat ini\n";
        startupMsg += "• /setradius [meter] - Set radius zona aman\n";
        startupMsg += "• /status - Cek status zona aman\n";
        startupMsg += "• /help - Tampilkan bantuan";
        sendTelegramMessage(startupMsg);
        startupMessageSent = true;
      }
    }
  }

  // Cek perintah Telegram setiap 5 detik
  if (millis() - lastCommandCheck >= COMMAND_CHECK_INTERVAL)
  {
    handleTelegramCommands();
    lastCommandCheck = millis();
  }

  // Cek jika GPS tidak merespon
  if (millis() > 5000 && gps.charsProcessed() < 10)
  {
    Serial.println("\nGPS tidak mengirim data. Periksa:");
    Serial.println("1. Koneksi TX/RX sudah benar");
    Serial.println("2. GPS berada di area terbuka");
    Serial.println("3. Tunggu beberapa menit untuk cold start");
    if (!startupMessageSent)
    {
      sendTelegramMessage("⚠️ GPS tidak merespon! Periksa koneksi.");
      startupMessageSent = true;
    }
    while (true)
      ;
  }
}

void handleTelegramCommands()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    String updates = getUpdates();
    if (updates != "")
    {
      processCommand(updates);
    }
  }
}

String getUpdates()
{
  HTTPClient http;
  String url = telegramApiUrl + "/getUpdates?limit=1&offset=" + String(lastUpdateId + 1);

  http.begin(url);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0)
  {
    String response = http.getString();
    http.end();

    // Parse JSON untuk mendapatkan pesan terakhir
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);

    if (doc["ok"] == true && doc["result"][0]["message"]["text"])
    {
      String text = doc["result"][0]["message"]["text"].as<String>();
      String fromChatId = doc["result"][0]["message"]["chat"]["id"].as<String>();
      unsigned long updateId = doc["result"][0]["update_id"].as<unsigned long>();

      // Hanya proses pesan dari chat ID yang diizinkan dan update ID baru
      if (fromChatId == chatId && updateId > lastUpdateId)
      {
        lastUpdateId = updateId;
        return text;
      }
    }
  }
  else
  {
    http.end();
  }

  return "";
}

void processCommand(String command)
{
  command.toLowerCase();

  if (command == "/lokasi" || command == "/location")
  {
    sendLocationToTelegram();
  }
  else if (command == "/setrumah" || command == "/sethome")
  {
    if (gps.location.isValid())
    {
      HOME_LAT = gps.location.lat();
      HOME_LNG = gps.location.lng();

      String msg = "🏠 Koordinat rumah berhasil diupdate!\n";
      msg += "Lat: " + String(HOME_LAT, 6) + "\n";
      msg += "Lng: " + String(HOME_LNG, 6) + "\n";
      msg += "Radius: " + String(SAFE_ZONE_RADIUS) + "m";

      sendTelegramMessage(msg);
    }
    else
    {
      sendTelegramMessage("⚠️ GPS belum fix! Tunggu beberapa saat.");
    }
  }
  else if (command.startsWith("/setradius"))
  {
    // Parse radius dari perintah /setradius 300
    int spaceIndex = command.indexOf(' ');
    if (spaceIndex > 0)
    {
      String radiusStr = command.substring(spaceIndex + 1);
      double newRadius = radiusStr.toDouble();

      if (newRadius > 0 && newRadius <= 10000) // Batas 10km
      {
        SAFE_ZONE_RADIUS = newRadius;
        String msg = "📏 Radius zona aman diupdate!\n";
        msg += "Radius baru: " + String(SAFE_ZONE_RADIUS) + "m\n";
        msg += "Lat rumah: " + String(HOME_LAT, 6) + "\n";
        msg += "Lng rumah: " + String(HOME_LNG, 6);

        sendTelegramMessage(msg);
      }
      else
      {
        sendTelegramMessage("⚠️ Radius harus antara 1-10000 meter!");
      }
    }
    else
    {
      sendTelegramMessage("❌ Format: /setradius [meter]\nContoh: /setradius 300");
    }
  }
  else if (command == "/status")
  {
    if (gps.location.isValid())
    {
      double distance = calculateDistance(HOME_LAT, HOME_LNG, gps.location.lat(), gps.location.lng());

      String msg = "📊 **Status Zona Aman**\n";
      msg += "📍 Koordinat rumah:\n";
      msg += "Lat: " + String(HOME_LAT, 6) + "\n";
      msg += "Lng: " + String(HOME_LNG, 6) + "\n";
      msg += "📏 Radius: " + String(SAFE_ZONE_RADIUS) + "m\n";
      msg += "📍 Jarak saat ini: " + String(distance, 0) + "m\n";

      if (distance <= SAFE_ZONE_RADIUS)
        msg += "✅ Status: Dalam zona aman";
      else
        msg += "⚠️ Status: KELUAR ZONA AMAN!";

      sendTelegramMessage(msg);
    }
    else
    {
      sendTelegramMessage("⚠️ GPS belum fix! Tidak dapat mengecek status.");
    }
  }
  else if (command == "/help")
  {
    String helpMsg = "📱 **Perintah yang Tersedia**\n\n";
    helpMsg += "📍 **/lokasi** - Cek posisi saat ini\n";
    helpMsg += "🏠 **/setrumah** - Set koordinat rumah dari posisi saat ini\n";
    helpMsg += "📏 **/setradius [meter]** - Set radius zona aman\n";
    helpMsg += "📊 **/status** - Cek status zona aman\n";
    helpMsg += "❓ **/help** - Tampilkan bantuan ini\n\n";
    helpMsg += "💡 **Tips:**\n";
    helpMsg += "• Gunakan /setrumah saat berada di rumah\n";
    helpMsg += "• Radius default: 200m\n";
    helpMsg += "• Maksimal radius: 10km";

    sendTelegramMessage(helpMsg);
  }
}

void displayGPSInfo()
{
  Serial.println("\n=== Informasi GPS ===");

  if (gps.location.isValid())
  {
    Serial.print("Lokasi: ");
    Serial.print(gps.location.lat(), 6);
    Serial.print(", ");
    Serial.println(gps.location.lng(), 6);

    // Hitung jarak dari rumah
    double distance = calculateDistance(HOME_LAT, HOME_LNG, gps.location.lat(), gps.location.lng());
    Serial.print("Jarak dari rumah: ");
    Serial.print(distance);
    Serial.println(" meter");

    if (distance <= SAFE_ZONE_RADIUS)
    {
      Serial.println("Status: ✅ Dalam zona aman");
    }
    else
    {
      Serial.println("Status: ⚠️ KELUAR ZONA AMAN!");
    }
  }
  else
  {
    Serial.println("Lokasi: Menunggu fix...");
  }

  Serial.print("Satelit: ");
  Serial.println(gps.satellites.value());

  if (gps.altitude.isValid())
  {
    Serial.print("Ketinggian: ");
    Serial.print(gps.altitude.meters());
    Serial.println(" meter");
  }

  if (gps.speed.isValid())
  {
    Serial.print("Kecepatan: ");
    Serial.print(gps.speed.kmph());
    Serial.println(" km/h");
  }

  Serial.println("==================\n");
}

void checkSafeZone()
{
  if (gps.location.isValid())
  {
    double distance = calculateDistance(HOME_LAT, HOME_LNG, gps.location.lat(), gps.location.lng());

    if (distance > SAFE_ZONE_RADIUS)
    {
      // Keluar dari zona aman
      if (!zoneAlertSent)
      {
        // Alert pertama kali keluar zona
        String alertMsg = "🚨 PERINGATAN! KELUAR ZONA AMAN!\n";
        alertMsg += "📍 Jarak dari rumah: " + String(distance, 0) + "m\n";
        alertMsg += "⚠️ Melebihi batas " + String(SAFE_ZONE_RADIUS) + "m!\n";
        alertMsg += "🔍 Segera cek lokasi!";

        sendTelegramMessage(alertMsg);
        sendLocationToTelegram();
        zoneAlertSent = true;
        inSafeZone = false;
        lastZoneAlert = millis(); // Set waktu alert pertama
      }
      else if (millis() - lastZoneAlert >= ZONE_ALERT_INTERVAL)
      {
        // Alert berulang setiap 30 detik
        String repeatAlertMsg = "🚨 PERINGATAN BERULANG! MASIH KELUAR ZONA AMAN!\n";
        repeatAlertMsg += "📍 Jarak dari rumah: " + String(distance, 0) + "m\n";
        repeatAlertMsg += "⚠️ Melebihi batas " + String(SAFE_ZONE_RADIUS) + "m!\n";
        repeatAlertMsg += "⏰ Alert ke-" + String((millis() - lastZoneAlert) / ZONE_ALERT_INTERVAL) + "\n";
        repeatAlertMsg += "🔍 Segera cek lokasi!";

        sendTelegramMessage(repeatAlertMsg);
        sendLocationToTelegram();
        lastZoneAlert = millis(); // Reset timer
      }
    }
    else if (distance <= SAFE_ZONE_RADIUS && !inSafeZone)
    {
      // Kembali ke zona aman
      String safeMsg = "✅ KEMBALI KE ZONA AMAN!\n";
      safeMsg += "📍 Jarak dari rumah: " + String(distance, 0) + "m\n";
      safeMsg += "🏠 Dalam radius " + String(SAFE_ZONE_RADIUS) + "m\n";
      safeMsg += "🔕 Alert peringatan dihentikan";

      sendTelegramMessage(safeMsg);
      zoneAlertSent = false;
      inSafeZone = true;
      lastZoneAlert = 0; // Reset alert timer
    }
  }
}

double calculateDistance(double lat1, double lng1, double lat2, double lng2)
{
  // Rumus Haversine untuk menghitung jarak
  const double R = 6371000; // Radius bumi dalam meter

  double lat1Rad = lat1 * PI / 180;
  double lat2Rad = lat2 * PI / 180;
  double deltaLat = (lat2 - lat1) * PI / 180;
  double deltaLng = (lng2 - lng1) * PI / 180;

  double a = sin(deltaLat / 2) * sin(deltaLat / 2) +
             cos(lat1Rad) * cos(lat2Rad) *
                 sin(deltaLng / 2) * sin(deltaLng / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));

  return R * c;
}

void sendTelegramMessage(String message)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    String url = telegramApiUrl + "/sendMessage";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonPayload = "{\"chat_id\":\"" + chatId + "\",\"text\":\"" + message + "\"}";

    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0)
    {
      Serial.println("Pesan Telegram terkirim! Response: " + String(httpResponseCode));
    }
    else
    {
      Serial.println("Error mengirim pesan Telegram: " + String(httpResponseCode));
    }

    http.end();
  }
  else
  {
    Serial.println("WiFi tidak terhubung!");
  }
}

void sendLocationToTelegram()
{
  if (WiFi.status() == WL_CONNECTED && gps.location.isValid())
  {
    HTTPClient http;
    String url = telegramApiUrl + "/sendLocation";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonPayload = "{\"chat_id\":\"" + chatId + "\",\"latitude\":" + String(gps.location.lat(), 6) +
                         ",\"longitude\":" + String(gps.location.lng(), 6) + "}";

    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0)
    {
      Serial.println("Lokasi terkirim ke Telegram! Response: " + String(httpResponseCode));

      // Kirim info tambahan
      double distance = calculateDistance(HOME_LAT, HOME_LNG, gps.location.lat(), gps.location.lng());

      String infoMessage = "📍 **Lokasi Saat Ini**\n";
      infoMessage += "Lat: " + String(gps.location.lat(), 6) + "\n";
      infoMessage += "Lng: " + String(gps.location.lng(), 6) + "\n";
      infoMessage += "🏠 Jarak dari rumah: " + String(distance, 0) + "m\n";
      infoMessage += "Satelit: " + String(gps.satellites.value()) + "\n";

      if (gps.altitude.isValid())
        infoMessage += "Ketinggian: " + String(gps.altitude.meters()) + "m\n";

      if (gps.speed.isValid())
        infoMessage += "Kecepatan: " + String(gps.speed.kmph()) + " km/h\n";

      if (distance <= SAFE_ZONE_RADIUS)
        infoMessage += "✅ Status: Dalam zona aman";
      else
        infoMessage += "⚠️ Status: KELUAR ZONA AMAN!";

      sendTelegramMessage(infoMessage);
    }
    else
    {
      Serial.println("Error mengirim lokasi: " + String(httpResponseCode));
    }

    http.end();
  }
  else
  {
    sendTelegramMessage("⚠️ Tidak dapat mengirim lokasi. GPS belum fix atau WiFi terputus.");
  }
}