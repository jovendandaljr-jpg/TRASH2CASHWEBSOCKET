
/*
  SMART TRASH BIN WITH COIN DISPENSER + STOPPER SERVO
  FINAL – ARDUINO UNO / NANO
  RULES:
  2 CANS = 1 PESO
  3 BOTTLES = 1 PESO

  FEATURES:
  - Coin hopper feedback
  - Retry if no coin detected
  - Safe reward deduction
  - Trash stopper servo for reliable classification

  DEVELOPER: JV STUDIO
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

LiquidCrystal_I2C lcd(0x27, 20, 4);
Servo servoGate, servoLid, servoStop;

// ---------------- PINS ----------------
#define INDUCTIVE_PIN    2
#define OK_BUTTON_PIN    3
#define RELAY_PIN        4
#define COIN_SENSOR_PIN  5
#define BUZZER_PIN       6
#define SERVO_GATE_PIN   7
#define SERVO_STOP_PIN   8   // ⭐ NEW STOPPER SERVO
#define TRIG_ENTRY       9
#define ECHO_ENTRY       10
#define TRIG_TRASH       11
#define ECHO_TRASH       12
#define SERVO_LID_PIN    13

// ---------------- PARAMETERS ----------------
#define ULTRA_DIST_ENTRY 10
#define ULTRA_DIST_TRASH 6
#define GATE_OPEN_TIME   2000UL
#define CLASSIFY_TIME    2500UL 
#define CLAIM_TIMEOUT    10000UL
#define STOP_TIME        5000UL

#define CANS_FOR_REWARD     2
#define BOTTLES_FOR_REWARD  3

#define LID_CENTER 118
#define LID_BOTTLE 50
#define LID_CAN 160
#define GATE_OPEN_POS 120
#define GATE_CLOSED_POS 0

#define STOP_CLOSED_POS 0     // stopper closed (hold trash)
#define STOP_OPEN_POS   100    // stopper open (release trash)

#define RELAY_ON_TIME 500
#define COIN_TIMEOUT  5000UL   // wait for coin

// ---------------- VARIABLES ----------------
int canCount = 0;
int bottleCount = 0;
int coinCount = 0;

bool trashDetected = false;
bool classifying = false;
bool claimMode = false;

unsigned long gateTimer;
unsigned long classifyTimer;
unsigned long claimTimer;

bool metalConfirmed = false;
int trashStableCount = 0;

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();

  pinMode(INDUCTIVE_PIN, INPUT);
  pinMode(OK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(COIN_SENSOR_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(TRIG_ENTRY, OUTPUT);
  pinMode(ECHO_ENTRY, INPUT);
  pinMode(TRIG_TRASH, OUTPUT);
  pinMode(ECHO_TRASH, INPUT);

  digitalWrite(RELAY_PIN, HIGH);

  servoGate.attach(SERVO_GATE_PIN);
  servoLid.attach(SERVO_LID_PIN);
  servoStop.attach(SERVO_STOP_PIN);

  servoGate.write(GATE_CLOSED_POS);
  servoLid.write(LID_CENTER);
  servoStop.write(STOP_CLOSED_POS);  // stopper closed initially
}

// ---------------- LOOP ----------------
void loop() {

  // ---------- CLAIM MODE ----------
  if (claimMode) {
    long remain = (CLAIM_TIMEOUT - (millis() - claimTimer)) / 1000;
    if (remain < 0) remain = 0;

    lcd.setCursor(0,3);
    lcd.print("PRESS OK | ");
    lcd.print(remain);
    lcd.print("s   ");

    if (millis() - claimTimer >= CLAIM_TIMEOUT) {
      claimMode = false;
      showIdle();
    }

    if (digitalRead(OK_BUTTON_PIN) == LOW) {
      delay(200);
      dispenseReward();
    }
    sendSerial();
    return;
  }

  // ---------- ENTRY DETECT ----------
  if (!trashDetected && readUltrasonic(TRIG_ENTRY, ECHO_ENTRY, ULTRA_DIST_ENTRY)) {
    trashDetected = true;
    gateTimer = millis();
    servoGate.write(GATE_OPEN_POS);

    lcd.clear();
    lcd.print("TRASH DETECTED");
    lcd.setCursor(0,1);
    lcd.print("INSERT ITEM");
    sendSerial();
  }

  // ---------- CLOSE GATE ----------
  if (trashDetected && millis() - gateTimer >= GATE_OPEN_TIME) {
    servoGate.write(GATE_CLOSED_POS);
    trashDetected = false;

    delay(200); // sensor stabilization buffer

    classifying = true;
    classifyTimer = millis();
    metalConfirmed = false;
    trashStableCount = 0;

    lcd.clear();
    lcd.print("CLASSIFYING...");

    servoStop.write(STOP_CLOSED_POS); // hold trash during classification

    Serial.println("CLASSIFICATION START");
    sendSerial();
  }

  // ---------- CLASSIFICATION ----------
  if (classifying) {

    if (readUltrasonic(TRIG_TRASH, ECHO_TRASH, ULTRA_DIST_TRASH))
      trashStableCount++;
    else
      trashStableCount = 0;

    if (digitalRead(INDUCTIVE_PIN) == LOW) {
      metalConfirmed = true;
      Serial.println("METAL DETECTED");
    }

    if (millis() - classifyTimer >= CLASSIFY_TIME) {

      if (trashStableCount < 3) {
        lcd.clear();
        lcd.print("NO TRASH");
      }
      else if (metalConfirmed) {
        Serial.println("CAN ACCEPTED");
        acceptCan();
      }
      else {
        Serial.println("BOTTLE ACCEPTED");
        acceptBottle();
      }

      classifying = false;
      delay(500);
      showIdle();
      sendSerial();
    }
  }

  // ---------- CHECK CLAIM ----------
  if (canCount >= CANS_FOR_REWARD || bottleCount >= BOTTLES_FOR_REWARD) {
    claimMode = true;
    claimTimer = millis();

    lcd.clear();
    lcd.print("REWARD READY!");
    lcd.setCursor(0,1);
    lcd.print("PRESS OK");
    sendSerial();
  }
}

// ---------------- FUNCTIONS ----------------

bool readUltrasonic(int trig, int echo, int maxDist) {

  int detections = 0;

  for (int i = 0; i < 10; i++) {

    digitalWrite(trig, LOW);
    delayMicroseconds(2);

    digitalWrite(trig, HIGH);
    delayMicroseconds(10);

    digitalWrite(trig, LOW);

    long duration = pulseIn(echo, HIGH, 35000);

    if (duration > 0) {

      long dist = duration * 0.034 / 2;

      if (dist > 1 && dist <= maxDist) {
        detections++;
      }
    }

    delay(2);
  }

  return (detections >= 1);
}

// ---------------- CAN ACCEPT ----------------
void acceptCan() {
  canCount++;
  tone(BUZZER_PIN, 1200, 150);

  lcd.clear();
  lcd.print("CAN ACCEPTED");
  lcd.setCursor(0,1);
  lcd.print("TOTAL: ");
  lcd.print(canCount);

  servoLid.write(LID_CAN);       // prepare lid
  delay(400);                    // wait before releasing
  servoStop.write(STOP_OPEN_POS); // release trash
  delay(700);
  servoLid.write(LID_CENTER);
  servoStop.write(STOP_CLOSED_POS);

  metalConfirmed = false;
  sendSerial();
}

// ---------------- BOTTLE ACCEPT ----------------
void acceptBottle() {
  bottleCount++;
  tone(BUZZER_PIN, 800, 150);

  lcd.clear();
  lcd.print("BOTTLE ACCEPTED");
  lcd.setCursor(0,1);
  lcd.print("TOTAL: ");
  lcd.print(bottleCount);

  servoLid.write(LID_BOTTLE);    // prepare lid
  delay(400);                    // wait before releasing
  servoStop.write(STOP_OPEN_POS); // release trash
  delay(700);
  servoLid.write(LID_CENTER);
  servoStop.write(STOP_CLOSED_POS);

  metalConfirmed = false;
  sendSerial();
}

// ---------------- COIN DISPENSE ----------------
void dispenseReward() {
  lcd.clear();
  lcd.print("DISPENSING...");

  unsigned long startTime = millis();
  bool coinDetected = false;

  digitalWrite(RELAY_PIN, LOW); // hopper ON

  while (millis() - startTime < COIN_TIMEOUT) {
    if (digitalRead(COIN_SENSOR_PIN) == LOW) {
      coinDetected = true;
      break;
    }
  }

  digitalWrite(RELAY_PIN, HIGH); // hopper OFF

  if (coinDetected) {
    tone(BUZZER_PIN, 1500, 200);

    lcd.clear();
    lcd.print("1 PESO GIVEN");

    if (canCount >= CANS_FOR_REWARD) {
      canCount -= CANS_FOR_REWARD;
    }
    else if (bottleCount >= BOTTLES_FOR_REWARD) {
      bottleCount -= BOTTLES_FOR_REWARD;
    }

    coinCount++;
    delay(1200);
    claimMode = false;
    showIdle();
  }
  else {
    tone(BUZZER_PIN, 400, 300);

    lcd.clear();
    lcd.print("NO COIN!");
    lcd.setCursor(0,1);
    lcd.print("TRY AGAIN");

    delay(1500);
    claimTimer = millis();
  }

  sendSerial();
}

// ---------------- IDLE SCREEN ----------------
void showIdle() {
  lcd.clear();
  lcd.print("SMART TRASH BIN");
  lcd.setCursor(0,1);
  lcd.print("2 CANS = 1 PESO");
  lcd.setCursor(0,2);
  lcd.print("3 BOTTLES = 1 PESO");

  sendSerial();
}

// ---------------- SERIAL ----------------
void sendSerial() {
  Serial.print("CANS:"); Serial.print(canCount);
  Serial.print(",BOTTLES:"); Serial.print(bottleCount);
  Serial.print(",COINS:"); Serial.print(coinCount);
  Serial.print(",MODE:");
  if(claimMode) Serial.println("CLAIM");
  else if(classifying) Serial.println("CLASSIFY");
  else if(trashDetected) Serial.println("ENTRY");
  else Serial.println("IDLE");
}
