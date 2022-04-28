#include <Arduino.h>
#include <Keypad.h>
#include <Wifi.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// storage id
#define STORAGE_ID "SfbClQ6PRR9UKREP5BwO"

// Wifi config
#define WIFI_SSID "Vento"
#define WIFI_PASSWORD "haidil272"

// Firebase config
#define API_KEY "AIzaSyALew81rfYoNZiqozEzGHxI2c3yrc_X97Y"
#define DATABASE_URL "https://smart-foodbank-default-rtdb.asia-southeast1.firebasedatabase.app/"

// Firebase auth config
#define USER_EMAIL "foodbank@admin.com"
#define USER_PASSWORD "haidil272"
String uid;
String storageName = "STORAGE";

// function declaration
void ultrasonic();
void detectMovememnt();
void checkKEY();
void connectFirebase();
void readFirebase();

// initiate lcd
int lcdColumn = 16;
int lcdRow = 2;
LiquidCrystal_I2C lcd(0x27, lcdColumn, lcdRow);

// define sound speed in cm/uS
#define SOUND_SPEED 0.034

// Pin declaration
const int relayPin = 32;
const int greenLED = 23;
const int redLED = 15;
const int motionSensor = 19;
const int trigPin = 25;
const int echoPin = 33;

// check state
boolean isUnlock = false;
boolean motionDetected = false;
boolean humanMovement = false;

// initiate keypad
const byte ROWS = 4; // four rows
const byte COLS = 4; // four columns

char keys[ROWS][COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};

// keypad pins
byte rowPins[ROWS] = {12, 14, 27, 26};
byte colPins[COLS] = {2, 4, 5, 18};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
const int len_key = 5;
char master_key[len_key] = {'1', '2', '3', '4', '1'};
char attempt_key[len_key];
int z = 0;

// for ultrasonic sensor
long duration;
float distanceCm;

// time handler
long now = millis();
long lastTrigger = 0;
long motionStart = 0;
int timeInterval = 30000;
int timeForDistance = 120000;

// for motion sensor
int pinStateCurrent = LOW;  // current state of pin
int pinStatePrevious = LOW; // previous state of pin

// for firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataprevMillis = 0;
int intValue;
float floatValue;
bool signinOK = false;

void setup()
{
  Serial.begin(9600);
  connectFirebase();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(storageName);
  pinMode(greenLED, OUTPUT);
  pinMode(redLED, OUTPUT);
  pinMode(relayPin, OUTPUT);

  pinMode(trigPin, OUTPUT); // Sets the trigPin as an Output
  pinMode(echoPin, INPUT);  // Sets the echoPin as an Input

  pinMode(motionSensor, INPUT);
  // attachInterrupt(digitalPinToInterrupt(motionSensor), detectMovement, HIGH);
}

void loop()
{
  now = millis();

  if ((!isUnlock && now - sendDataprevMillis > 15000) || (!isUnlock && sendDataprevMillis == 0))
  {
    sendDataprevMillis = millis();
    if (Firebase.RTDB.getBool(&fbdo, "/StorageData/isUnlock"))
    {
      isUnlock = fbdo.boolData();
      lastTrigger = millis();
    }

    if (Firebase.RTDB.getString(&fbdo, "/StorageData/storageName"))
    {
      storageName = fbdo.stringData();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(storageName);
    }
  }

  char key = keypad.getKey();
  lcd.setCursor(z, 1);
  if (key != NULL)
  {
    lcd.print(key);
  }
  lcd.blink();
  if (key)
  {
    switch (key)
    {
    case '*':
      z = 0;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(storageName);
      break;
    case '#':
      delay(100); // added debounce
      checkKEY();
      break;
    default:
      attempt_key[z] = key;
      z++;
    }
  }

  if (isUnlock)
  {
    digitalWrite(greenLED, HIGH);
    digitalWrite(relayPin, HIGH);

    // activate motion sensor
    pinStatePrevious = pinStateCurrent;          // store old state
    pinStateCurrent = digitalRead(motionSensor); // read new state

    if (pinStatePrevious == LOW && pinStateCurrent == HIGH)
    { // pin state change: LOW -> HIGH
      Serial.println("Motion detected!");
      motionDetected = true;
      motionStart = millis();
    }
  }

  if (isUnlock && (now - lastTrigger > timeInterval))
  {
    isUnlock = false;
    digitalWrite(greenLED, LOW);
    digitalWrite(relayPin, LOW);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(storageName);
    Firebase.RTDB.setBool(&fbdo, "StorageData/isUnlock", false);
  }

  if (motionDetected && (now - motionStart > timeInterval))
  {
    Serial.print("Motion ended");
    motionDetected = false;
  }

  if (motionDetected && isUnlock)
  {
    humanMovement = true;
  }

  if (humanMovement && (now - motionStart > timeForDistance))
  {
    humanMovement = false;
    ultrasonic();
    Serial.print("calculating distance... ");
    Serial.println(distanceCm);
    Firebase.RTDB.setFloat(&fbdo, "StorageData/depth", distanceCm);
  }
}

void connectFirebase()
{
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Failed to connect wifi");
  }
  else
  {
    Serial.print("Connected");
    Serial.println(WiFi.localIP());
  }

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);

  // Assign the callback function for the long running token generation task
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  // Assign the maximum retry of token generation
  config.max_token_generation_retry = 5;

  // Initialize the library with the Firebase authen and config
  Firebase.begin(&config, &auth);

  // Getting the user UID might take a few seconds
  Serial.println("Getting User UID");
  while ((auth.token.uid) == "")
  {
    Serial.print('.');
    delay(1000);
    signinOK = true;
  }
  // Print user UID
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.print(uid);
}

void checkKEY()
{
  int correct = 0;
  int i;
  for (i = 0; i < len_key; i++)
  {
    if (attempt_key[i] == master_key[i])
    {
      correct++;
    }
  }
  if (correct == len_key && z == len_key)
  {
    lcd.setCursor(0, 1);
    lcd.print("Correct key!");
    Serial.println("unlocking storage");
    isUnlock = true;
    lastTrigger = millis();

    z = 0;
  }
  else
  {
    lcd.setCursor(0, 1);
    lcd.print("Incorrect Key");
    digitalWrite(redLED, HIGH);
    delay(3000);
    digitalWrite(redLED, LOW);
    z = 0;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(storageName);
  }
  for (int zz = 0; zz < len_key; zz++)
  {
    attempt_key[zz] = 0;
  }
}

void detectMovement()
{
  Serial.println("MOTION DETECTED!");
  motionDetected = true;
  motionStart = millis();
}

void ultrasonic()
{
  // Clears the trigPin

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(echoPin, HIGH);

  // Calculate the distance
  distanceCm = duration * SOUND_SPEED / 2;
}