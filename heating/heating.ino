#include <LiquidCrystal.h>
#include <ESP8266wifi.h>
#include <SoftwareSerial.h>

#define sw_serial_rx_pin 21 //  Connect this pin to TX on the esp8266
#define sw_serial_tx_pin 20 //  Connect this pin to RX on the esp8266
#define esp8266_reset_pin 10 // Connect this pin to CH_PD on the esp8266, not reset. (let reset be unconnected)

#define SERVER_PORT "23"
#define SSID "technical"
#define PASSWORD "#technical"

SoftwareSerial swSerial(sw_serial_rx_pin, sw_serial_tx_pin);
ESP8266wifi wifi(Serial1, Serial1, esp8266_reset_pin, swSerial);
//ESP8266wifi wifi(Serial1, Serial1, esp8266_reset_pin);

const int beta = 4090; // from thermistor datasheet
const int resistance = 33;

LiquidCrystal lcd(9, 8, 5, 4, 3, 2);

int relayPin = 7;
int aPin = 14;
int bPin = 15;
int buttonPin = 16;
int analogPin = 0;
int lightPin = 6;
int count2 = 0;
int count = 0;
int i = 0;

float setTemp = 20.0;
float measuredTemp;
char mode = 'C';        // can be changed to F 
boolean is_off = true;  // whether the thermostat is set to on or off
boolean heatingOn = false; // boiler demand 
boolean ended = false;
float hysteresis = 0.25;

void processCommand(WifiMessage msg);
uint8_t wifi_started = false;

// TCP Commands
const char RST[] PROGMEM = "RST";
const char TEMP[] PROGMEM = "TEMP";
const char ON[] PROGMEM = "ON";
const char OFF[] PROGMEM = "OFF";

void setup() {
  lcd.begin(2, 20);
  pinMode(relayPin, OUTPUT);
  pinMode(aPin, INPUT);  
  pinMode(bPin, INPUT);
  pinMode(buttonPin, INPUT);
  pinMode(lightPin, OUTPUT);
  // back light on at first boot
  digitalWrite(lightPin, HIGH);
  digitalWrite(buttonPin, HIGH);
  digitalWrite(aPin, HIGH);
  digitalWrite(bPin, HIGH);
  lcd.clear();

  // start debug serial
  swSerial.begin(9600);
  // start HW serial for ESP8266 (change baud depending on firmware)
  Serial1.begin(9600);
  while (!Serial1)
    ;

  //swSerial.println("Starting wifi");
  wifi.setTransportToTCP();// this is also default
  wifi.endSendWithNewline(true); // Will end all transmissions with a newline and carrage return ie println.. default is true

  wifi_started = wifi.begin();
  if (wifi_started) {
    Serial1.println("AT+CWLAP");
    delay(2000);
    wifi.connectToAP(SSID, PASSWORD);
    wifi.startLocalServer(SERVER_PORT);
  } else {
    swSerial.println("wifi not connected");
  }
}

void loop() {
  measuredTemp = readTemp();
  if (digitalRead(buttonPin) == LOW) {
    // something happened so turn the backlight on
    backlight(HIGH);
    is_off = ! is_off;
    updateDisplay();
    delay(500); // debounce
  }
  int change = getEncoderTurn();
  setTemp = setTemp + change * 0.5;
  if (count == 1000) {
    // turn the backlight off after a few seconds
    if (count2 == 100) {
      backlight(LOW);
    }
    count2++;
    updateDisplay();
    updateOutputs();
    count = 0;
  }
  count ++;

  static WifiConnection *connections;

  // check connections if the ESP8266 is there
  if (wifi_started)
    wifi.checkConnections(&connections);

  // check for messages if there is a connection
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].connected) {
      // See if there is a message
      WifiMessage msg = wifi.getIncomingMessage();
      // Check message is there
      if (msg.hasData) {
        // process the command
        processCommand(msg);
      }
    }
  }

}

void backlight(boolean state) {
  // set the backlight and reset count2
  digitalWrite(lightPin, state);
  count2 = 0;
}

int getEncoderTurn() {
  // if off then don't change
  if (is_off) {
    return 0; 
  }
  // return -1, 0, or +1
  static int oldA = LOW;
  static int oldB = LOW;
  int result = 0;
  int newA = digitalRead(aPin);
  int newB = digitalRead(bPin);
  if (newA != oldA || newB != oldB) {
    // something has changed
    if (oldA == LOW && newA == HIGH) {
      result = -(oldB * 2 - 1);
      // turn the back light on
      backlight(HIGH);
    }
  }
  oldA = newA;
  oldB = newB;
  return result;
} 

float readTemp() {
  long a = analogRead(analogPin);
  float temp = beta / (log(((1025.0 * resistance / a) - 33.0) / 33.0) + (beta / 298.0)) - 273.0;
  return temp;
}

void updateOutputs() {
  if (!is_off &&  measuredTemp < setTemp - hysteresis) {
    //digitalWrite(ledPin, HIGH);
    digitalWrite(relayPin, HIGH);
    heatingOn = true;
  } 
  else if (is_off || measuredTemp > setTemp + hysteresis) {
    //digitalWrite(ledPin, LOW);
    digitalWrite(relayPin, LOW);
    heatingOn = false;
  }
}

void updateDisplay() {
  lcd.setCursor(0,0);
  lcd.print("Actual: ");
  lcd.print(adjustUnits(measuredTemp));
  lcd.print(" o");
  lcd.print(mode);
  lcd.print(" ");
  
  lcd.setCursor(0,1);
  if (is_off) {
    lcd.print("****HEAT OFF****");
  }
  else {
    lcd.print("Set:    ");
    lcd.print(adjustUnits(setTemp));
    lcd.print(" o");
    lcd.print(mode);
    lcd.print(" ");
  }
}

float adjustUnits(float temp) {
  if (mode == 'C') {
    return temp;
  }
  else {
    return (temp * 9) / 5 + 32;
  }
}

void processCommand(WifiMessage msg)
{
  // scanf holders
  int set;
  char str[16];
  char buffer[10];

  // Get command and setting
  sscanf(msg.message,"%15s %d",str,&set);
  
  //Get temperature
  if (!strcmp_P(str,TEMP))
  {
    ftoa(buffer, readTemp());
    wifi.send(msg.channel, buffer);
  }
  else if (!strcmp_P(str, ON))
  {
    is_off = false;
    backlight(HIGH);
    wifi.send(msg.channel, "Thermostat ON");
  }
  else if (!strcmp_P(str, OFF))
  {
    wifi.send(msg.channel, "Thermostat OFF");
    backlight(HIGH);
    is_off = true;
  }
  // Reset system by temp enable watchdog
  else if (!strcmp_P(str,RST)) 
  {
    wifi.send(msg.channel,"SYSTEM RESET...");
    // soft reset by reseting PC
    asm volatile ("  jmp 0");
  }
  // Unknown command
  else
  {
    wifi.send(msg.channel,"ERR");
  }
  updateDisplay();
}

void ftoa(char fstr[80], float num)
{
  int m = log10(num);
  int digit;
  float tolerance = .0001f;

  while (num > 0 + tolerance) {
    float weight = pow(10.0f, m);
    digit = floor(num / weight);
    num -= (digit*weight);
    *(fstr++)= '0' + digit;
    if (m == 0)
      *(fstr++) = '.';
    m--;
  }
  *(fstr) = '\0';
}

