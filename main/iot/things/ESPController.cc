//ESPController文件
#include "iot/thing.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mqtt.h"
#include "driver/uart.h"
#include <cstring>
#include <vector>
 
#define TAG "ESPController"
 
// MQTT 配置
#define MQTT_URI       "mqtt://192.168.229.27:1883"
#define CLIENT_ID      "ESP32-Controller"
 
// MQTT 主题
#define LED_CMD_TOPIC  "home/livingroom/led/command"
#define FAN_CMD_TOPIC  "home/livingroom/fan/command"
#define FAN_PRESET_TOPIC "home/livingroom/fan/preset_mode"
 
// UART 配置
#define UART_PORT UART_NUM_1 // 更换为其他可用UART端口
#define BUF_SIZE       (1024)
 
static esp_mqtt_client_handle_t mqtt_client;
 
namespace iot {
 
class ESPController : public Thing {
private:
    static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            esp_wifi_connect();
        }
    }
 
    static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            esp_mqtt_client_start(mqtt_client);
        }
    }
 
    static void mqtt_event_handler(void* handler_args, esp_event_base_t base, 
                                  int32_t event_id, void* event_data) {
        esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
        if (event->event_id == MQTT_EVENT_CONNECTED) {
            ESP_LOGI(TAG, "MQTT Connected");
        }
    }
 
    static void uart_read_task(void* arg) {
        uint8_t data[BUF_SIZE];
        while (1) {
            int len = uart_read_bytes(UART_PORT, data, BUF_SIZE - 1, pdMS_TO_TICKS(100)); // 增加超时时间
            if (len > 0) {
                data[len] = '\0';
                std::string command((char*)data);
                command.erase(command.find_last_not_of("\r\n") + 1);
 
                if (command.find("打开客厅灯") != std::string::npos) {
                    send_mqtt_command(LED_CMD_TOPIC, "ON");
                } else if (command.find("关闭客厅灯") != std::string::npos) {
                    send_mqtt_command(LED_CMD_TOPIC, "OFF");
                } else if (command.find("打开风扇") != std::string::npos) {
                    send_mqtt_command(FAN_CMD_TOPIC, "ON");
                } else if (command.find("关闭风扇") != std::string::npos) {
                    send_mqtt_command(FAN_CMD_TOPIC, "OFF");
                } else if (command.find("一挡") != std::string::npos) {
                    send_mqtt_command(FAN_PRESET_TOPIC, "Low");
                } else if (command.find("二挡") != std::string::npos) {
                    send_mqtt_command(FAN_PRESET_TOPIC, "Medium");
                } else if (command.find("三挡") != std::string::npos) {
                    send_mqtt_command(FAN_PRESET_TOPIC, "High");
                }
            }else {
                vTaskDelay(pdMS_TO_TICKS(10));}
        }
    }
 
    static void send_mqtt_command(const char* topic, const char* payload) {
        esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 1, 0);
        ESP_LOGI(TAG, "发送指令: %s -> %s", topic, payload);
    }
 
    public:
ESPController() : Thing("LivingRoomController", "客厅设备控制器") {
    static bool netif_initialized = false;
    if (!netif_initialized) {
        esp_netif_init();
        esp_event_loop_create_default();
        netif_initialized = true;
    }
 
        // 初始化 MQTT（正确设置 client_id）
        esp_mqtt_client_config_t mqtt_cfg = {};
        mqtt_cfg.broker.address.uri = MQTT_URI;
        mqtt_cfg.credentials.client_id = CLIENT_ID;  // 嵌套在 credentials 中
        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
 
        // 注册 MQTT 事件
        esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
        // 直接启动 MQTT 客户端（假设网络已就绪）
         esp_mqtt_client_start(mqtt_client);
 
        // 初始化 UART
            uart_config_t uart_config = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        };
        uart_param_config(UART_PORT, &uart_config);
        uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0);
 
        // 创建 UART 读取任务
        xTaskCreate(uart_read_task, "uart_task", 4096, NULL, 2, NULL);
 
        // 注册属性与方法
        properties_.AddBooleanProperty("light_status", "客厅灯状态", [this]() -> bool {
            return false;
        });
 
        // 使用完全限定的参数类型
        methods_.AddMethod("ControlLight", "控制客厅灯开关",
            ParameterList(std::vector<Parameter>{Parameter("state", "开关状态", kValueTypeString)}),
            [this](const ParameterList& params) {
                send_mqtt_command(LED_CMD_TOPIC, params["state"].string().c_str());
            }
        );
 
        methods_.AddMethod("ControlFan", "控制风扇开关",
            ParameterList(std::vector<Parameter>{Parameter("state", "开关状态", kValueTypeString)}),
            [this](const ParameterList& params) {
                std::string state = params["state"].string();
                send_mqtt_command(FAN_CMD_TOPIC, state.c_str());
            }
        );
 
        methods_.AddMethod("SetFanSpeed", "设置风扇档位",
            ParameterList(std::vector<Parameter>{Parameter("speed", "档位（Low/Medium/High）", kValueTypeString)}),
            [this](const ParameterList& params) {
                std::string speed = params["speed"].string();
                send_mqtt_command(FAN_PRESET_TOPIC, speed.c_str());
            }
        );
    }
};
 
} // namespace iot
 
DECLARE_THING(ESPController);