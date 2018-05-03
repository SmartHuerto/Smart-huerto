#include <Firebase.h>
#include <FirebaseArduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <NTPClient.h>
#include <Timezone.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include "BlueDot_BME280_TSL2591.h"


ESP8266WebServer server(80); //inicializamos servidor en puerto 80
String humedad_aire = "35.5";
String temperatura = "56";
String humedad_suelo = "27";
String luminosidad = "200";
String lluvia = "No";

//declaramos las constantes de inicio
const int pinLed = D3;
const int pinLluvia = D6;
String presion;
BlueDot_BME280_TSL2591 bme280;
BlueDot_BME280_TSL2591 tsl2591;
#define APssid "Smart guerto config"
#define APpasswd "pruebaF4sil"
const String ARD_ID = "Id arduino";
#define FIREBASE_HOST "url-firebase"
#define FIREBASE_AUTH "Secreto firebase"
#define NTP_OFFSET 60 * 60 // En segundos
#define NTP_INTERVAL 60 * 1000 // En milisegundos
#define NTP_ADDRESS "pool.ntp.org" // URL NTP
IPAddress LOCAL_IP(192, 168, 7, 69);
IPAddress SUBNET(255, 255, 255, 0);
String emergencia = "contrasena";
unsigned long int timeout = 0;

const char * meses[12] = {
  "Enero",
  "Febrero",
  "Marzo",
  "Abril",
  "Mayo",
  "Junio",
  "Julio",
  "Agosto",
  "Septiembre",
  "Octubre",
  "Noviembre",
  "Diciembre"
};
String st;
String content;

bool configurado = false;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL); //configuraos servidor NTP para fecha y hora
TimeChangeRule CEST = {
  "CEST",
  Last,
  Sun,
  Mar,
  2,
  60
}; // Hora de Verano de Europa Central
TimeChangeRule CET = {
  "CET ",
  Last,
  Sun,
  Oct,
  3,
  0
}; // Hora Estandar de Europa Central
Timezone CE(CEST, CET); //configuramos zona horaria
time_t local, utc;

void setup() {

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.begin(115200); //abrimos serial
  //if(SPIFFS.format()){Serial.println("formateo completo");}else{Serial.println("error formato");}
  SPIFFS.begin(); //abrimos SPIFFS
  delay(500);
  //a partir de aqui no config
  if (SPIFFS.exists("/config.txt")) {
    Serial.println("Leyendo archivo config...");
    File f = SPIFFS.open("/config.txt", "r");
    if (!f) {
      Serial.println("error al abrir archivo");
      parpadeo(2);
    } else {
      String ssid = f.readStringUntil(';');
      Serial.println(ssid);
      String passwd = f.readStringUntil(';');
      Serial.println(passwd);
      emergencia = f.readStringUntil(';');
      Serial.println(emergencia);
      f.close();
      if (SPIFFS.exists("/timeout.txt")) {
        Serial.println("Leyendo archivo timeout...");
        File f = SPIFFS.open("/timeout.txt", "r");
        if (!f) {
          Serial.println("error al abrir archivo");
          parpadeo(2);
        } else {
          timeout = f.readStringUntil(';').toInt();
          Serial.println(timeout);
          Serial.println("timeout recuperado");
          f.close();
        }
      }
      WiFi.disconnect();
      delay(500);
      WiFi.begin(ssid.c_str(), passwd.c_str());
      if (probarWifi()) {
        iniciarSensores();
        Serial.println("cargando firebase");
        cargarFirebase();
        configurado = true;
      } else {
        if (SPIFFS.remove("/config.txt")) {
          Serial.println("Config eliminado");
          ESP.restart();
        } else {
          Serial.println("Error al abrir");
          parpadeo(3);
          ESP.restart();
        }

      }
    }
  } else {

    cargarWeb('0');

  }

}

void loop() {
  if (configurado) {

    recogerValores();
    delay(500);
    if (reseteo()) {
      if (SPIFFS.remove("/config.txt")) {
        Serial.println("Reseteado correctamente");
        delay(500);
        ESP.restart();
      }
    }
    timeClient.update();
    unsigned long utc = timeClient.getEpochTime();
    if (utc >= timeout) {
      enviarDatosH(humedad_aire, humedad_suelo, temperatura, luminosidad, lluvia, presion);
    } else {
      enviarDatosActuales(humedad_aire, humedad_suelo, temperatura, luminosidad, lluvia, presion);
    }
    delay(1000);
  } else {
    server.handleClient();
  }
}

void enviarDatosH(String hum_aire, String hum_suel, String temp, String lum, String lluvia, String presion) {
  Serial.println("Enviando datos hora");
  // Actualizar el cliente NTP y obtener la marca de tiempo UNIX UTC
  timeClient.update();
  unsigned long utc = timeClient.getEpochTime();
  //Convertir marca de tiempo UTC UNIX a hora local
  local = CE.toLocal(utc);

  String mes = meses[month(local) - 1];
  String ano = String(year(local));
  if (year(local) < 2018) {
    timeout = 0;
  } else {
    String dia = String(day(local));
    String hora = getHora(local);
    Serial.println("Hora correcta");
    Firebase.setString(ARD_ID + "/indexado/" + ano + "/" + mes + "/" + dia + "/" + hora + "/luminosidad", lum);
    Firebase.setString(ARD_ID + "/indexado/" + ano + "/" + mes + "/" + dia + "/" + hora + "/temperatura", temp + "ºC");
    Firebase.setString(ARD_ID + "/indexado/" + ano + "/" + mes + "/" + dia + "/" + hora + "/humedad aire", hum_aire + "%");
    Firebase.setString(ARD_ID + "/indexado/" + ano + "/" + mes + "/" + dia + "/" + hora + "/humedad suelo", hum_suel + "%");
    Firebase.setString(ARD_ID + "/indexado/" + ano + "/" + mes + "/" + dia + "/" + hora + "/lluvia", lluvia);
    Firebase.setString(ARD_ID + "/indexado/" + ano + "/" + mes + "/" + dia + "/" + hora + "/presion", presion);
    //if (Firebase.failed()) {
    //Serial.print("Error en la conexion con la base de datos: ");
    //Serial.println(Firebase.error());
    //} else {
    Serial.println("Datos indexados enviados correctamente");
    //}
    saveTimeout(utc + 3600);
  }
}

void enviarDatosActuales(String hum_aire, String hum_suel, String temp, String lum, String lluvia, String presion) {
  Serial.println("Enviando datos actuales");
  Firebase.setString(ARD_ID + "/actual/luminosidad", lum);
  Firebase.setString(ARD_ID + "/actual/temperatura", temp + "ºC");
  Firebase.setString(ARD_ID + "/actual/humedad aire", hum_aire + "%");
  Firebase.setString(ARD_ID + "/actual/humedad suelo", hum_suel + "%");
  Firebase.setString(ARD_ID + "/actual/lluvia", lluvia);
  Firebase.setString(ARD_ID + "/actual/presion", presion);
}

String getHora(time_t t) {
  // Retorna la hora sin segundos
  String hora = "";
  if (hour(t) < 10)
    hora += "0";
  hora += hour(t);
  hora += ":";
  if (minute(t) < 10) // Agrega un cero si hora o minuto tiene una cifra
    hora += "0";
  hora += minute(t);
  return hora;
}

void cargarWeb(char opc) {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(APssid, APpasswd, 6); // iniciamos wifi con ssid passwd y canal 6
  WiFi.softAPConfig(LOCAL_IP, LOCAL_IP, SUBNET);
  Serial.println(WiFi.softAPIP());
  if (opc == '0') {
    int n = WiFi.scanNetworks();
    Serial.println("escaneo realizado");
    if (n == 0) {
      Serial.println("no se han encontrado redes");
      parpadeo(5);
    } else {
      Serial.print(n);
      Serial.println(" redes encontradas");
      for (int i = 0; i < n; ++i) {
        // Imprime SSID y RSSI
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(WiFi.SSID(i));
        Serial.print(" (");
        Serial.print(WiFi.RSSI(i));
        Serial.print(")");
        Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
        delay(10);
      }
    }
    Serial.println("");
    st = "<select name=\"ssid\">";
    for (int i = 0; i < n; ++i) {
      // hacemos un select con los ssid y RSSI
      st += "<option value\"";
      st += WiFi.SSID(i);
      st += "\">";
      st += WiFi.SSID(i);
      st += "</option><p>";
      st += " (";
      st += WiFi.RSSI(i);
      st += ")";
      st += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
      st += "</p>";

    }
    st += "</select>";

    delay(100);
    //cargamos paginas webs
    server.on("/config", []() {
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML>\r\n<html><head><meta charset=\"UTF-8\"><title>Configuración de smart guerto</title></head><body>Smart huerto te saluda desde ";
      content += ipStr;
      content += "<p></br></br></p>";
      content += "<form method='get' action='setting' id=\"config\"><label> SSID: </label>";
      content += st;
      content += "<label>  Contraseña: </label><input type=\"password\" name=\"pass\" length=64></br></br><label>Contraseña para posterior reseteo:  </label><input type='password' name='emergencia' length=32><label>     </label><input type='submit' value='Enviar'></form>";
      content += "</body></html>";
      server.send(200, "text/html", content);
    });
    server.on("/setting", []() {
      String slc_ssid = server.arg("ssid");
      String slc_passwd = server.arg("pass");
      String new_emergencia = server.arg("emergencia");
      server.send(200, "text/plain", "guardando cambios..., puedes cerrar esta ventana e ir a la base de datos");
      delay(500);

      File f = SPIFFS.open("/config.txt", "w");
      if (!f) {
        Serial.println("error al abrir");
      } else {
        f.print(slc_ssid + ";");
        f.print(slc_passwd + ";");
        f.print(new_emergencia + ";");
        f.close();
      }
      ESP.restart();

    });
    server.on("/", [] {
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML>\r\n<html><head><meta charset=\"UTF-8\"><title>Configuración de smart guerto</title></head><body><p>Smart huerto te saluda desde ";
      content += ipStr;
      content += "</p><p><a href=\"/config\" >Ir a configuración</a></body></html>";
      server.send(200, "text/html", content);
    });
  }
  server.begin();
}

void cargarFirebase() {
  //inicializamos wifi y Firebase

  // Actualizar el cliente NTP y obtener la marca de tiempo UNIX UTC
  timeClient.update();
  unsigned long utc = timeClient.getEpochTime();
  //Convertir marca de tiempo UTC UNIX a hora local
  local = CE.toLocal(utc);

  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  if (Firebase.failed()) {
    Serial.println("Connection error:  ");
    Serial.println(Firebase.error());
    parpadeo(4);
  } else {
    Serial.println("Conexion con la base de datos correcta");
    configurado = true;
    Firebase.setString(ARD_ID + "/emerg", "\"null\"");
    Serial.println("emrg configurado correctamente");
  }

}

bool probarWifi() {
  int a = 0;
  Serial.print("connectando");
  while (a < 25) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(500);
      a++;
    } else {
      Serial.println();
      Serial.print("conectado como: ");
      Serial.println(WiFi.localIP()); //conectamos wifi
      return true;
    }
  }
  Serial.println();
  Serial.print("Error de conexion numero:  ");
  Serial.println(WiFi.status());
  return false;
}

void iniciarSensores() {
  ESP.wdtDisable();
  Serial.println("Inicializando sensores");
  Wire.begin();
  bme280.parameter.I2CAddress = 0x77;
  tsl2591.parameter.I2CAddress = 0x29;
  tsl2591.parameter.gain = 0b01;
  tsl2591.parameter.integration = 0b000;
  tsl2591.config_TSL2591();
  bme280.parameter.sensorMode = 0b11;
  bme280.parameter.IIRfilter = 0b100;
  bme280.parameter.humidOversampling = 0b101;
  bme280.parameter.tempOversampling = 0b101;
  bme280.parameter.pressOversampling = 0b101;
  bme280.parameter.pressureSeaLevel = 1013.25;
  bme280.parameter.tempOutsideCelsius = 15;
  if (bme280.init_BME280() != 0x60)
  {
    Serial.println(F("Error al inciar BME280"));
  }
  else
  {
    Serial.println(F("BME280 inciado correctamente"));
  }

  if (tsl2591.init_TSL2591() != 0x50)
  {
    Serial.println(F("Error al iniciar TSL2591"));
  }
  else
  {
    Serial.println(F("TSL2591 iniciado correctamente"));
  }
  Serial.println("Sensor bluedot correcto");
  pinMode(pinLluvia, INPUT);
  Serial.println("Sensor lluvia correcto");
  pinMode(pinLed, OUTPUT);
  digitalWrite(pinLed, LOW);
  Serial.println("Led correcto");
  Serial.println("Sensores iniciados");
  delay(500);

}

void recogerValores() {
  //Recogemos los valores de los sensores
  humedad_aire = String(bme280.readHumidity());
  temperatura = String(bme280.readTempC());
  presion = String(bme280.readPressure());
  luminosidad = String(tsl2591.readIlluminance_TSL2591() / 100);
  humedad_suelo = getHumedadSuelo();
  //sensor lluvia
  if (!digitalRead(pinLluvia)) {
    lluvia = "Si";
  } else {
    lluvia = "No";
  }
  Serial.println(humedad_aire);
  Serial.println(temperatura);
  Serial.println(luminosidad);
  Serial.println(lluvia);
  Serial.println(humedad_suelo);

}

String getHumedadSuelo() {
  float valor = 1024 - analogRead(A0);
  return String(100 * valor / 1024);
}

bool reseteo() {
  String emerg = quitarComillas(Firebase.getString(ARD_ID + "/emerg"));
  Serial.println(emerg);
  Serial.println(emergencia);
  if (emerg == emergencia) {
    return true;
  } else {
    return false;
  }
}

void parpadeo(int veces) {
  for (int i = 0; i < veces; i++) {
    digitalWrite(pinLed, HIGH);
    delay(500);
    digitalWrite(pinLed, LOW);
    delay(500);
  }
  delay(2000);
  for (int i = 0; i < veces; i++) {
    digitalWrite(pinLed, HIGH);
    delay(500);
    digitalWrite(pinLed, LOW);
    delay(500);
  }
  delay(1000);
  digitalWrite(pinLed, LOW);
  ESP.restart();
}

String quitarComillas(String cadena) {
  String temp = "";
  for (int i = 1; i < cadena.length() - 1; i++) {
    temp += cadena.c_str()[i];
  }
  return temp;
}
void saveTimeout(int hora) {
  File f = SPIFFS.open("/timeout.txt", "w");
  if (!f) {
    Serial.println("error al guardar timeout");
  } else {
    f.print(String(hora) + ";");
    Serial.println("Timeout salvado");
    timeout = hora;
  }
}
