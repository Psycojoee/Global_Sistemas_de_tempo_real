/*
 * Global Solution 2 - Monitor de Redes Wi-Fi
 * Simulado no Wokwi
 * 
 * Nome: Thiago Marques da Silva - RM88049
 * Nome: Gabriel de Freitas - RM86666
 * Nome: Alessandro Chiarele Filho - RM84282
 */

// --- Bibliotecas ---
#include <WiFi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include <string.h>

// --- Configurações de Tempo ---
#define TEMPO_SCAN_MS 3000
#define WDT_TIMEOUT_S 5

// --- Credenciais Wi-Fi (para Wokwi) ---
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// --- Lista Segura de Redes ---
/* * Cenário 1 (Seguro): Deixe "Wokwi-GUEST" na lista.
 * Cenário 2 (Alerta): Remova "Wokwi-GUEST" da lista.
 */
const char* LISTA_SEGURA[5] = {
    //"Wokwi-GUEST",
    "REDE_CORPORATIVA_1",
    "REDE_CORPORATIVA_5G",
    "Visitantes_Empresa",
    "Hotspot_TI"
};

// --- Handles Globais do FreeRTOS ---
QueueHandle_t g_queue_ssids;
QueueHandle_t g_queue_alerts;
SemaphoreHandle_t g_list_mutex;


// --- Função de Conexão Wi-Fi (Arduino/Wokwi) ---
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando em ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}


// --- TAREFA 1: Scanner (Prioridade Baixa) ---
void task_wifi_scanner(void *pvParameters) {
    esp_task_wdt_add(NULL); 
    char current_ssid_str[64];
    char* ssid_ptr_to_send = NULL;

    for(;;) {
        if (WiFi.status() == WL_CONNECTED) {
            strncpy(current_ssid_str, WiFi.SSID().c_str(), 64);
        } else {
            strncpy(current_ssid_str, "DESCONECTADO", 64);
        }

        ssid_ptr_to_send = (char*) malloc(strlen(current_ssid_str) + 1);

        if (ssid_ptr_to_send == NULL) {
            Serial.printf("[SCANNER] ERRO CRITICO: Falha no Malloc!\n");
        } else {
            strcpy(ssid_ptr_to_send, current_ssid_str);
            if (xQueueSend(g_queue_ssids, &ssid_ptr_to_send, pdMS_TO_TICKS(1000)) != pdTRUE) {
                Serial.printf("[SCANNER] ERRO: Fila A (SSIDs) cheia!\n");
                free(ssid_ptr_to_send);
            }
        }
        
        esp_task_wdt_reset(); 
        vTaskDelay(pdMS_TO_TICKS(TEMPO_SCAN_MS));
    }
}


// --- TAREFA 2: Validador (Prioridade Média) ---
void task_wifi_validator(void *pvParameters) {
    esp_task_wdt_add(NULL); 
    char* received_ssid_ptr = NULL;
    
    for(;;) {
        if (xQueueReceive(g_queue_ssids, &received_ssid_ptr, portMAX_DELAY) == pdTRUE) {
            
            if (received_ssid_ptr == NULL) {
                Serial.printf("[VALIDADOR] ERRO: Ponteiro nulo recebido!\n");
                continue; 
            }

            bool encontrado = false;
            
            if (xSemaphoreTake(g_list_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                for (int i = 0; i < 5; i++) {
                    if (strcmp(received_ssid_ptr, LISTA_SEGURA[i]) == 0) {
                        encontrado = true;
                        break;
                    }
                }
                xSemaphoreGive(g_list_mutex); 
            } else {
                Serial.printf("[VALIDADOR] ERRO: Nao conseguiu pegar o semaforo!\n");
            }

            if (encontrado) {
                Serial.printf("[VALIDADOR] Rede Segura: %s\n", received_ssid_ptr);
                free(received_ssid_ptr);
            } else {
                if (xQueueSend(g_queue_alerts, &received_ssid_ptr, pdMS_TO_TICKS(1000)) != pdTRUE) {
                    Serial.printf("[VALIDADOR] ERRO: Fila B (Alertas) cheia!\n");
                    free(received_ssid_ptr);
                }
            }
        }
        esp_task_wdt_reset(); 
    }
}


// --- TAREFA 3: Logger de Alerta (Prioridade Alta) ---
void task_alert_logger(void *pvParameters) {
    esp_task_wdt_add(NULL); 
    char* alert_ssid_ptr = NULL;

    for(;;) {
        if (xQueueReceive(g_queue_alerts, &alert_ssid_ptr, portMAX_DELAY) == pdTRUE) {
            if (alert_ssid_ptr != NULL) {
                // --- REQUISITO: Registro simples de log quando um alerta for gerado ---
                Serial.printf("[ALERTA] REDE INSEGURA DETECTADA: %s\n", alert_ssid_ptr);
                free(alert_ssid_ptr);
            }
        }
        esp_task_wdt_reset(); 
    }
}


// --- SETUP ---
void setup()
{
    Serial.begin(115200);
    setup_wifi(); 
    
    esp_task_wdt_config_t configWDT = {
        .timeout_ms = WDT_TIMEOUT_S * 1000, 
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&configWDT);
    esp_task_wdt_add(NULL);
    
    g_queue_ssids = xQueueCreate(10, sizeof(char*)); 
    g_queue_alerts = xQueueCreate(5, sizeof(char*)); 
    g_list_mutex = xSemaphoreCreateMutex();
    
    xTaskCreate(
        task_wifi_scanner,   
        "Scanner",      
        4096,           
        NULL,           
        1,
        NULL            
    );
    xTaskCreate(
        task_wifi_validator, 
        "Validador", 
        4096, 
        NULL, 
        2,
        NULL 
    );
    xTaskCreate(
        task_alert_logger,    
        "Logger",    
        2048, 
        NULL, 
        3,
        NULL 
    );
    
    Serial.printf("\n>>> Sistema de Monitoramento de Wi-Fi Iniciado <<<\n");
    esp_task_wdt_reset();
}


// --- LOOP ---
void loop()
{
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
}