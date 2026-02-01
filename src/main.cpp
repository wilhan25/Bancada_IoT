/*
  Projeto : Automação Residencial com ESP32
  Autor   : Eng. Wilhan Almeida
  Data    : 30/01/2026

  Descrição:
  Sistema de automação residencial utilizando ESP32,
  FreeRTOS e MQTT para controle de iluminação, sensores
  e atuadores simulados.
*/

#include <Arduino.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <WiFi.h>


//PINAGEM
#define led_sala    2
#define led_cozinha 4
#define led_quarto  5
#define motor_varal 16
#define buzzer      18

#define pir_pin     19
#define mq2_pin     34
#define dht_pin     15

#define DHT_TIPO    DHT22

// WIFI E BROKER MQTT
const char* ssid = "Wokwi-GUEST";
const char* senha = "";
const char* brokerMQTT = "broker.hivemq.com";

// CLIENTE WIFI E MQTT
WiFiClient clienteWiFi;
PubSubClient clienteMQTT(clienteWiFi);

DHT dht(dht_pin,DHT_TIPO); // config do sensor de umidade e temperatura

//DEFINÇÃO DOS ESTADOS  DO SISTEMA
typedef enum{
  ESTADO_INICIANDO,
  ESTADO_WIFI,
  ESTADO_MQTT,
  ESTADO_EXECUTANDO,
  ESTADO_ERRO
} EstadoSistema;

EstadoSistema estadoAtual = ESTADO_INICIANDO;

//EVENTOS
#define EVT_WIFI_OK     (1 << 0)
#define EVT_MQTT_OK     (1 << 1)
#define EVT_MOVIMENTO   (1 << 2)
#define EVT_GAS         (1 << 3)

//REFERENCIAS PARA ACESSO A INTERNO
TaskHandle_t tarefaEstadoHandle;
TaskHandle_t tarefaWifiHandle;
TaskHandle_t tarefaMQTTHandle;
TaskHandle_t tarefaIluminacaoHandle;
TaskHandle_t tarefaPIRHandle;
TaskHandle_t tarefaGasHandle;
TaskHandle_t tarefaClimaHandle;
TaskHandle_t tarefaVralHandle;


//TASK PARA CONTROLE DE ESTADO
void tarefaEstado(void *param){
  uint32_t eventos;

  while (1)
  {
    xTaskNotifyWait(0,0xFFFFFFFF, &eventos, portMAX_DELAY);
    
    switch (estadoAtual)
    {
    case ESTADO_INICIANDO:
      estadoAtual = ESTADO_WIFI;
      xTaskNotify(tarefaWifiHandle, 0, eNoAction);
      break;
    
    case ESTADO_WIFI:
      if(eventos & EVT_WIFI_OK){
        estadoAtual = ESTADO_MQTT;
        xTaskNotify(tarefaMQTTHandle, 0, eNoAction);
      }
      break;

    case ESTADO_MQTT:
      if(eventos & EVT_MQTT_OK){
        estadoAtual = ESTADO_EXECUTANDO;
      }
      break;

    case ESTADO_ERRO:
      estadoAtual = ESTADO_WIFI;
      xTaskNotify(tarefaWifiHandle, 0, eNoAction);
      break;
    
    default:
      break;
    }
  }  
}

//TASK WIFI
void tarefaWifi(void *param){
  while (1)
  {
    WiFi.begin(ssid,senha);

    if (WiFi.waitForConnectResult() == WL_CONNECTED)
    {
      xTaskNotify(tarefaEstadoHandle, EVT_WIFI_OK, eSetBits);
    }
    else{
      estadoAtual = ESTADO_ERRO;
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }  
}

//TASK MQTT
void callbackMQTT(char* topico, byte* payload, unsigned int tamanho){
  String mensagem;
  for (int i = 0; i < tamanho; i++)
  {
    mensagem += (char)payload[i];
  }
  if(String(topico) == "casa/luz/sala") digitalWrite(led_sala, mensagem =="ON");
  if(String(topico) == "casa/luz,cozinha") digitalWrite(led_cozinha, mensagem =="ON");
  if(String(topico) == "casa/luz/quarto") digitalWrite(led_quarto, mensagem=="ON");  
}

void tarefaMQTT(void *param){
  clienteMQTT.setServer(brokerMQTT, 1883);
  clienteMQTT.setCallback(callbackMQTT);

  while (1)
  {
    if(!clienteMQTT.connected()){
      if (clienteMQTT.connect("ESP32-CASA"))
      {
        clienteMQTT.subscribe("casa/luz/#");
        xTaskNotify(tarefaEstadoHandle, EVT_MQTT_OK, eSetBits);
      }      
    }
    clienteMQTT.loop();
    vTaskDelay(pdMS_TO_TICKS(100));
  }  
}

//TASK VARAL
void tarefaClima(void *param){
  while (1)
  {
    float umidade = dht.readHumidity();
    float temperatura = dht.readTemperature();

    if(umidade < 60){
      digitalWrite(motor_varal,HIGH);
    } else{
      digitalWrite(motor_varal,LOW);
    }

    char payload[50];
    sprintf(payload,"{\"temp\": %.1f, \"umi\":%.1f}", temperatura, umidade);
    clienteMQTT.publish("casa/clima", payload);

    vTaskDelay(pdMS_TO_TICKS(5000));
  }  
}

//TASK DO SENSOR DE MOVIMENTO
void tarefaPIR(void *param){
  while(1){
    if(digitalRead(pir_pin)){
      digitalWrite(led_sala,HIGH);
      digitalWrite(led_cozinha,HIGH);
      digitalWrite(led_quarto,HIGH);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

//TASK PARA VAZAMENTO DE GÁS
void tarefaGas(void *param){
  while(1){
    int gas = analogRead(mq2_pin);

    if(gas>2000){
      digitalWrite(buzzer,HIGH);
      clienteMQTT.publish("casa/gas/alerta", "GAS DETECTADO");
    } else{
      digitalWrite(buzzer, LOW);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

//SETUP
void setup(){
  pinMode(led_sala, OUTPUT);
  pinMode(led_cozinha, OUTPUT);
  pinMode(led_quarto, OUTPUT);
  pinMode(motor_varal, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(pir_pin, INPUT);
  pinMode(mq2_pin, INPUT);

  dht.begin();

  xTaskCreate(tarefaEstado, "estado", 4096, NULL, 3, &tarefaEstadoHandle);
  xTaskCreate(tarefaWifi, "wifi", 4096, NULL, 2, &tarefaWifiHandle);
  xTaskCreate(tarefaMQTT,"mqtt", 4096, NULL, 2, &tarefaMQTTHandle);
  xTaskCreate(tarefaGas, "Gas", 2048, NULL, 1, &tarefaGasHandle);
  xTaskCreate(tarefaClima, "Clima", 4096, NULL, 1, &tarefaClimaHandle);
  xTaskCreate(tarefaPIR, "movimento", 2048, NULL, 1, &tarefaPIRHandle);

  xTaskNotify(tarefaEstadoHandle, 0, eNoAction);
}

void loop(){
  //vazio
}