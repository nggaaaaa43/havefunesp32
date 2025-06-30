#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include <BluetoothSerial.h>

#define SS_PIN 5
#define RST_PIN 27
#define LAMPU_PIN 2

#define MAX_UIDS 10
#define UID_SIZE 4
#define PASS_ADDR 0
#define PASS_MAX_LEN 20
#define UID_START_ADDR 20

#define MAX_USERS 5
#define USERNAME_LEN 16
#define PASSWORD_LEN 16
#define ROLE_LEN 1
#define USER_RECORD_SIZE (USERNAME_LEN + PASSWORD_LEN + ROLE_LEN)
#define USER_BASE_ADDR 100

MFRC522 rfid(SS_PIN, RST_PIN);
BluetoothSerial SerialBT;

bool LightUp = false;
bool AccessApprove = false;
String mode = "";

enum Role { NONE, USER, ADMIN };
Role currentRole = NONE;
String currentUsername = "";

unsigned long timeLog = 0;
const unsigned long timeoutLog = 60000;

void hideBluetoothDevice() {
  esp_bluedroid_status_t btStatus = esp_bluedroid_get_status();
  if (btStatus == ESP_BLUEDROID_STATUS_ENABLED) {
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    Serial.println("🔒 Bluetooth disembunyikan dari publik.");
  } else {
    Serial.println("⚠️ Bluetooth belum aktif, tidak bisa disembunyikan.");
  }
}

void resetAllUsers() {
  for (int i = USER_BASE_ADDR; i < USER_BASE_ADDR + (MAX_USERS * USER_RECORD_SIZE); i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  Serial.println("🧹 Semua akun dihapus dari EEPROM.");
}

void injectUser(const char* username, const char* password, char role) {
  for (int i = 0; i < MAX_USERS; i++) {
    int base = USER_BASE_ADDR + i * USER_RECORD_SIZE;
    if (EEPROM.read(base) == 0xFF) {
      for (int j = 0; j < USERNAME_LEN; j++) {
        EEPROM.write(base + j, j < strlen(username) ? username[j] : 0);
      }
      for (int j = 0; j < PASSWORD_LEN; j++) {
        EEPROM.write(base + USERNAME_LEN + j, j < strlen(password) ? password[j] : 0);
      }
      EEPROM.write(base + USERNAME_LEN + PASSWORD_LEN, role);
      EEPROM.commit();
      Serial.println("🛠️ Akun telah di-inject ke EEPROM.");
      return;
    }
  }
  Serial.println("❌ Slot akun penuh.");
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();
  pinMode(LAMPU_PIN, OUTPUT);
  digitalWrite(LAMPU_PIN, LOW);
  EEPROM.begin(512);

  SerialBT.begin("RANGGA_MICROCONTROLLER", true);
  delay(1000);
  hideBluetoothDevice();

  /*
  resetAllUsers();
  Serial.println("📦 Cek EEPROM slot akun:");
  for (int i = 0; i < MAX_USERS; i++) {
    int base = USER_BASE_ADDR + i * USER_RECORD_SIZE;
    byte val = EEPROM.read(base);
    Serial.print("Slot ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.println(val == 0xFF ? "Kosong" : "Terisi");
  }
  injectUser("admin", "admin123", 'A');
  Serial.println("🟢 Sistem aktif. Silakan login dengan format: login username=password");
  */
}

// (Selanjutnya tidak diubah)

void loop() {
  if (SerialBT.available()) {
    String input = SerialBT.readStringUntil('\n');
    input.replace("\r", "");
    input.replace("\n", "");
    input.trim();

    if (!AccessApprove) {
      if (input.equals("")) {
        SerialBT.println("🟢 Sistem aktif.\n🔐 Silahkan login dengan format:\n\nlogin username=password\n");
        }
      if (input.startsWith("login ")) {
        if (loginUser(input.substring(6))) {
          AccessApprove = true;
          timeLog = millis();
          SerialBT.println("✅ Login berhasil.");
          SerialBT.println("👋 Selamat Datang, " + currentUsername + "." + "\n💭 Ketik 'help' untuk melihat daftar perintah.");
        } else {
          SerialBT.println("❌ Login gagal.\n🔎 Periksa username/password.");
        }
      } else {
        SerialBT.println("❗️Masukkan perintah login dulu❗️\n\nlogin username=password");
      }
      return;
    }

    timeLog = millis();
    //input.toLowerCase();
    mode = input;

    if (mode == "help") {
      SerialBT.println("📖 Daftar Perintah:");
      SerialBT.println("- Login. \nlogin username=password");
      SerialBT.println("\n- Hidup/Matikan Lampu. \nlight on / light off");
      if (currentRole == ADMIN) {
        SerialBT.println("\n- Menghapus seluruh kartu terdaftar. \nresetcardlist");
        SerialBT.println("\n- Menambah kartu baru. \naddcard");
        SerialBT.println("\n- Menghapus kartu tertentu. \ndelcard");
        SerialBT.println("\n- Lihat kartu terdaftar. \nlistcard");
        SerialBT.println("\n- Menambah akun baru. \nadduser nama=[username] password=[password] role=[a/u]");
        SerialBT.println("\n-Menghapus akun. \ndeluser [username]");
        SerialBT.println("\n- Lihat daftar akun aktif. \nlistuser");
      }
      return;
    }

    if (mode == "light on") {
      digitalWrite(LAMPU_PIN, HIGH);
      LightUp = true;
      SerialBT.println("💡 Lampu dinyalakan via Bluetooth.");
      return;
    } else if (mode == "light off") {
      digitalWrite(LAMPU_PIN, LOW);
      LightUp = false;
      SerialBT.println("💤 Lampu dimatikan via Bluetooth.");
      return;
    }

    if (currentRole != ADMIN) {
      SerialBT.println("⛔ Perintah hanya untuk admin.");
      return;
    }

    if (mode == "resetcardlist") {
      resetEEPROM_UID();
      return;
    } else if (mode == "addcard") {
      addUID(rfid.uid.uidByte);
      return;
    } else if (mode == "delcard") {
      deleteUID(rfid.uid.uidByte);
      return;
    } else if (mode == "listcard") {
      displayUID();
      return;
    } else if (input.startsWith("adduser ")) {
      String u = input.substring(8);
      int pIdx = u.indexOf(" password=");
      int rIdx = u.indexOf(" role=");
      if (pIdx > 0 && rIdx > pIdx) {
        String uname = u.substring(5, pIdx);
        String pwd = u.substring(pIdx + 10, rIdx);
        String role = u.substring(rIdx + 6);
        if (addUser(uname, pwd, role[0])) {
          SerialBT.println("✅ User ditambahkan.");
        } else {
          SerialBT.println("❌ Gagal tambah user.");
        }
      } else {
        SerialBT.println("⚠️ Format salah. Contoh: adduser nama=admin password=123 role=A");
      }
      return;
    } else if (input.startsWith("deluser ")) {
      String uname = input.substring(8);
      if (deleteUser(uname)) {
        SerialBT.println("🗑️ User dihapus.");
      } else {
        SerialBT.println("❌ Gagal hapus user.");
      }
      return;
    } else if (mode == "listuser") {
      listUsers();
      return;
    }
  }

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    if (RegisteredUID(rfid.uid.uidByte)) {
      LightUp = !LightUp;
      delay(100);
      digitalWrite(LAMPU_PIN, LightUp ? HIGH : LOW);
      SerialBT.println(LightUp ? "💡 Lampu ON (UID valid)." : "💤 Lampu OFF (UID valid)." );
    } else {
      SerialBT.println("🚫 UID tidak dikenal.");
    }

    SerialBT.print("🔍 UID dibaca: ");
    for (byte i = 0; i < rfid.uid.size; i++) {
      SerialBT.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
      SerialBT.print(rfid.uid.uidByte[i], HEX);
    }
    SerialBT.println();

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  if (AccessApprove && millis() - timeLog > timeoutLog) {
    AccessApprove = false;
    currentRole = NONE;
    SerialBT.println("⏳ Sesi habis karena tidak ada aktivitas. Silahkan login ulang.");
  }
}

// ... fungsi loginUser, findUser, addUser, deleteUser, listUsers tetap seperti sebelumnya ...
bool loginUser(String input) {
  int sep = input.indexOf('=');
  if (sep == -1) return false;
  String uname = input.substring(0, sep);
  String pass = input.substring(sep + 1);

  char role;
  int index;
  if (findUser(uname, pass, &role, &index)) {
    currentUsername = uname;
    currentRole = (role == 'A') ? ADMIN : USER;
    return true;
  }
  return false;
}

bool findUser(String uname, String pass, char* roleOut, int* indexOut) {
  for (int i = 0; i < MAX_USERS; i++) {
    int base = USER_BASE_ADDR + i * USER_RECORD_SIZE;
    char storedUser[USERNAME_LEN + 1];
    char storedPass[PASSWORD_LEN + 1];
    for (int j = 0; j < USERNAME_LEN; j++) storedUser[j] = EEPROM.read(base + j);
    for (int j = 0; j < PASSWORD_LEN; j++) storedPass[j] = EEPROM.read(base + USERNAME_LEN + j);
    storedUser[USERNAME_LEN] = '\0';
    storedPass[PASSWORD_LEN] = '\0';
    if (String(storedUser).equals(uname) && String(storedPass).equals(pass)) {
      *roleOut = EEPROM.read(base + USERNAME_LEN + PASSWORD_LEN);
      *indexOut = i;
      return true;
    }
  }
  return false;
}

bool addUser(String uname, String pass, char role) {
  if (uname.length() == 0 || pass.length() == 0) return false;
  if (uname.length() > USERNAME_LEN || pass.length() > PASSWORD_LEN) return false;
  for (int i = 0; i < MAX_USERS; i++) {
    int base = USER_BASE_ADDR + i * USER_RECORD_SIZE;
    if (EEPROM.read(base) == 0xFF) {
      for (int j = 0; j < USERNAME_LEN; j++) EEPROM.write(base + j, j < uname.length() ? uname[j] : 0);
      for (int j = 0; j < PASSWORD_LEN; j++) EEPROM.write(base + USERNAME_LEN + j, j < pass.length() ? pass[j] : 0);
      EEPROM.write(base + USERNAME_LEN + PASSWORD_LEN, role);
      EEPROM.commit();
      return true;
    }
  }
  return false;
}

bool deleteUser(String uname) {
  for (int i = 0; i < MAX_USERS; i++) {
    int base = USER_BASE_ADDR + i * USER_RECORD_SIZE;
    char storedUser[USERNAME_LEN + 1];
    for (int j = 0; j < USERNAME_LEN; j++) storedUser[j] = EEPROM.read(base + j);
    storedUser[USERNAME_LEN] = '\0';
    if (String(storedUser).equals(uname)) {
      for (int j = 0; j < USER_RECORD_SIZE; j++) EEPROM.write(base + j, 0xFF);
      EEPROM.commit();
      return true;
    }
  }
  return false;
}

void listUsers() {
  SerialBT.println("📋 Daftar Akun:");
  for (int i = 0; i < MAX_USERS; i++) {
    int base = USER_BASE_ADDR + i * USER_RECORD_SIZE;
    if (EEPROM.read(base) != 0xFF) {
      char storedUser[USERNAME_LEN + 1];
      char storedRole;
      for (int j = 0; j < USERNAME_LEN; j++) storedUser[j] = EEPROM.read(base + j);
      storedUser[USERNAME_LEN] = '\0';
      storedRole = EEPROM.read(base + USERNAME_LEN + PASSWORD_LEN);
      SerialBT.print("- ");
      SerialBT.print(String(storedUser));
      SerialBT.print(" (Role: ");
      SerialBT.print(storedRole);
      SerialBT.println(")");
    }
  }
}

// Fungsi UID, RFID dan lainnya tetap seperti sebelumnya ...
void loadPasswordFromEEPROM() {
  char pass[PASS_MAX_LEN + 1];
  bool empty = true;

  for (int i = 0; i < PASS_MAX_LEN; i++) {
    pass[i] = EEPROM.read(PASS_ADDR + i);
    if (pass[i] != 0xFF && pass[i] != '\0') empty = false;
    if (pass[i] == '\0') break;
  }
  pass[PASS_MAX_LEN] = '\0';
  }

void savePasswordToEEPROM(String pass) {
  for (int i = 0; i < PASS_MAX_LEN; i++) {
    if (i < pass.length()) {
      EEPROM.write(PASS_ADDR + i, pass[i]);
    } else {
      EEPROM.write(PASS_ADDR + i, 0);
    }
  }
  EEPROM.commit();
  }

void addUID(byte *newUID) {
  bool promptShown = false;
  unsigned long startTime = millis();
  const unsigned long timeout = 10000; // 10 detik

  while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    if (!promptShown) {
      SerialBT.println("💳 Silahkan tap kartu baru");
      promptShown = true;
    }
    // Cek apakah sudah melebihi batas waktu
    if (millis() - startTime > timeout) {
      SerialBT.println("⏱️ Sesi waktu tap kartu telah habis.");
      return;
    }
    delay(100); // hindari spam loop
  }

  if (RegisteredUID(newUID)) {
    SerialBT.println("⚠️ UID sudah ada.");
    return;
  }

  for (int i = 0; i < MAX_UIDS; i++) {
    int base = UID_START_ADDR + (i * UID_SIZE);
    bool empty = true;
    for (int j = 0; j < UID_SIZE; j++) {
      if (EEPROM.read(base + j) != 0xFF) {
        empty = false;
        break;
      }
    }

    if (empty) {
      for (int j = 0; j < UID_SIZE; j++) {
        EEPROM.write(base + j, newUID[j]);
      }
      EEPROM.commit();
      SerialBT.print("🆕 UID ditambahkan: ");
      for (int j = 0; j < UID_SIZE; j++) {
        if (newUID[j] < 0x10) SerialBT.print("0");
        SerialBT.print(newUID[j], HEX);
        SerialBT.print(" ");
      }
      SerialBT.println();
      SerialBT.println("✅ Kartu berhasil ditambahkan.");

       rfid.PICC_HaltA();
       rfid.PCD_StopCrypto1();
       delay(1000);
      return;
    }
  }
  SerialBT.println("❌ EEPROM penuh.");
  }

void deleteUID(byte *uidDelete) {
  bool promptShown = false;
  unsigned long startTime = millis();
  const unsigned long timeout = 10000; // 10 detik

  while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    if (!promptShown) {
      SerialBT.println("💳 Silahkan tap kartu untuk dihapus");
      promptShown = true;
    }
    // Cek apakah sudah melebihi batas waktu
    if (millis() - startTime > timeout) {
      SerialBT.println("⏱️ Sesi waktu tap kartu telah habis.");
      return;
    }
    delay(100); // hindari spam loop
  }

  for (int i = 0; i < MAX_UIDS; i++) {
    int base = UID_START_ADDR + (i * UID_SIZE);
    bool match = true;
    for (int j = 0; j < UID_SIZE; j++) {
      if (EEPROM.read(base + j) != uidDelete[j]) {
        match = false;
        break;
      }
    }

    if (match) {
      for (int j = 0; j < UID_SIZE; j++) {
        EEPROM.write(base + j, 0xFF);
      }
      EEPROM.commit();
      SerialBT.println("🗑️ UID dihapus.");
      SerialBT.println("✅ Kartu berhasil dihapus.");
      
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      delay(1000);
      return;
    }
  }
  SerialBT.println("❌ UID tidak ditemukan.");
  }

void resetEEPROM_UID() {
  for (int i = UID_START_ADDR; i < UID_START_ADDR + (MAX_UIDS * UID_SIZE); i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  SerialBT.println("⚠️ Semua UID dihapus.");
  }

bool RegisteredUID(byte *uid) {
  for (int i = 0; i < MAX_UIDS; i++) {
    int base = UID_START_ADDR + (i * UID_SIZE);
    bool match = true;
    for (int j = 0; j < UID_SIZE; j++) {
      if (EEPROM.read(base + j) != uid[j]) {
        match = false;
        break;
      }
    }
    if (match) return true;
    }
    return false;
    }

void displayUID() {
  SerialBT.println("📋 UID Tersimpan:");
  for (int i = 0; i < MAX_UIDS; i++) {
    int base = UID_START_ADDR + (i * UID_SIZE);
    bool empty = true;
    for (int j = 0; j < UID_SIZE; j++) {
      if (EEPROM.read(base + j) != 0xFF) {
        empty = false;
        break;
      }
    }

    if (!empty) {
      SerialBT.print("Slot ");
      SerialBT.print(i + 1);
      SerialBT.print(": ");
      for (int j = 0; j < UID_SIZE; j++) {
        byte b = EEPROM.read(base + j);
        if (b < 0x10) SerialBT.print("0");
        SerialBT.print(b, HEX);
        SerialBT.print(" ");
      }
      SerialBT.println();
    }
    }
  }
