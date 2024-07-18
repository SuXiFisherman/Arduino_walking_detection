#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <SD.h>
#include <time.h>

// WiFi 相关设置
const char* ssid = "bear123";
const char* password = "61911030b";

// FSR 引脚和阈值
const int fsrPin1 = 34;
const int fsrPin2 = 35;
const int fsrThreshold = 600;

// 变量声明
int fsr1PressCount = 0;
int fsr2PressCount = 0;
bool fsr2Pressed = false;
const int numDays = 7;
float dailyData[numDays] = {0};

// Web 服务器
AsyncWebServer server(80);

// HTML 页面
const char* index_html = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta http-equiv="refresh" content="2">
  <script src="https://code.highcharts.com/highcharts.js"></script>
  <style>
    body {
      min-width: 310px;
      max-width: 800px;
      height: 400px;
      margin: 0 auto;
    }
    h2 {
      font-family: Arial;
      font-size: 2.5rem;
      text-align: center;
    }
  </style>
</head>
<body>
  <h2>Steps Gaits Monitor</h2>
  <div id="chart-data" class="container"></div>
</body>
<script>
Highcharts.chart('chart-data', {
    chart: {
        type: 'column'
    },
    title: {
        text: 'Weekly Data Overview'
    },
    xAxis: {
        categories: [
            'Monday',
            'Tuesday',
            'Wednesday',
            'Thursday',
            'Friday',
            'Saturday',
            'Sunday'
        ],
        crosshair: true
    },
    yAxis: {
        min: 0,
        title: {
            text: 'Data Value'
        }
    },
    tooltip: {
        headerFormat: '<span style="font-size:10px">{point.key}</span><table>',
        pointFormat: '<tr><td style="color:{series.color};padding:0">{series.name}: </td>' +
            '<td style="padding:0"><b>{point.y:.1f} </b></td></tr>',
        footerFormat: '</table>',
        shared: true,
        useHTML: true
    },
    plotOptions: {
        column: {
            pointPadding: 0.2,
            borderWidth: 0
        }
    },
    series: [{
        name: 'Data',
        data: []

    }]
});
</script>
</html>
)rawliteral";

// 函数声明
void logDataToSDCard(int fsrNum, int pressCount);
void logRawData(int fsr1Value, int fsr2Value);
int getDayOfWeek();

void setup() {
  // 初始化串行通信
  Serial.begin(115200);

  // 初始化SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    return;
  }
  
  // 初始化SD卡
  if (!SD.begin()) {
    Serial.println("Initialization of SD card failed");
    return;
  }
  Serial.println("SD card initialized");

 // 测试创建和写入文件
  File testFile = SD.open("/test.txt", FILE_WRITE);
  if (!testFile) {
    Serial.println("Failed to open test file for writing");
  } else {
    Serial.println("Test file opened successfully");
    testFile.println("This is a test file.");
    testFile.close();
    Serial.println("Test file written and closed");
  }
  // 连接到Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }

  // 配置NTP服务器和时区
  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  while (!time(nullptr)) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("Time updated via NTP.");


  // 打印ESP32本地IP地址
  Serial.println(WiFi.localIP());

  // 读取保存的数据
  File file = SPIFFS.open("/data.txt", "r");
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  if (file.read((uint8_t*)dailyData, sizeof(dailyData)) != sizeof(dailyData)) {
    Serial.println("Failed to read data from file");
  } else {
    Serial.println("读取成功！");
  }
  file.close();

  // 打印数据
  for (int i = 0; i < 7; i++) {
    Serial.print("   ");
    Serial.print(dailyData[i]);
  }

  int dayOfWeek = getDayOfWeek(); // 获取今天是星期几，1-星期一  2-星期二
  Serial.println(dayOfWeek);

  // 为根/网页设置路由
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    int dayOfWeek = getDayOfWeek();
    String modifiedHtml(index_html);
    String dataString = "data: [";

    // 添加每天的数据到dataString中
    for (int i = 0; i < numDays; i++) {
      if (i > 0) {
        dataString += ", ";
      }
      
      // 如果有数据，则使用数据值；否则，使用0
      float dataValue = dailyData[i];
      dataString += String(dataValue);
    }

    dataString += "]";
    
    // 替换HTML页面中的data: []为动态生成的数据
    modifiedHtml.replace("data: []", dataString);

    request->send(200, "text/html", modifiedHtml); // 发送修改后的index.html
  });

  // 启动服务器
  server.begin();
}

void loop() {
  int fsrStatus1 = analogRead(fsrPin1);
  int fsrStatus2 = analogRead(fsrPin2);
  
  // 记录原始数据
  logRawData(fsrStatus1, fsrStatus2);

  if (fsrStatus1 >= fsrThreshold && fsrStatus2 >= fsrThreshold) {
    Serial.println("Both FSRs pressed simultaneously, no action taken.");
    return; // 如果兩個 FSR 同时被按下，则不进行计算
  }
  if (fsrStatus2 >= fsrThreshold) {
    fsr2Pressed = true;
    fsr2PressCount++; // 增加第二个FSR的按压次数
    Serial.print("FSR 2 has been pressed ");
    Serial.print(fsr2PressCount);
    Serial.println(" times.");
    logDataToSDCard(2, fsr2PressCount); // 记录FSR2数据到SD卡
    delay(200); // 防止抖动
  } else if (fsr2Pressed && fsrStatus1 >= fsrThreshold) {
    fsr2Pressed = false; // 重置FSR2的状态
    fsr1PressCount++; // 增加第一个FSR的按压次数
    dailyData[getDayOfWeek() - 1]++; // 更新当天的数据
    Serial.print("FSR 1 has been pressed AFTER FSR 2, total times today: ");
    Serial.println(fsr1PressCount);
    logDataToSDCard(1, fsr1PressCount); // 记录FSR1数据到SD卡

    // 每次按压后保存数据到SPIFFS
    // 每次按压后保存数据到SPIFFS
    File file = SPIFFS.open("/data.txt", "w");
    if (!file) {
      Serial.println("Failed to open file for writing");
      return;
    }
    if (file.write((uint8_t*)dailyData, sizeof(dailyData)) != sizeof(dailyData)) {
      Serial.println("Failed to write data to file");
    }
    file.close();
  }
}

// 获取今天是星期几，返回值范围是 1（星期一）到 7（星期日）
int getDayOfWeek() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  int wday = timeinfo->tm_wday;
  return (wday == 0) ? 7 : wday; // tm_wday returns 0 for Sunday
}

// 日志记录功能
void logDataToSDCard(int fsrNum, int pressCount) {
  // 获取当前时间
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char timeBuffer[20];
  strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", timeinfo);

  // 创建或打开日志文件
  File logFile = SD.open("/fsr_log.txt", FILE_APPEND);
  if (!logFile) {
    Serial.println("Failed to open log file on SD card");
    return;
  }

  // 写入数据
  if (logFile.print("FSR ") &&
      logFile.print(fsrNum) &&
      logFile.print(" pressed at ") &&
      logFile.print(timeBuffer) &&
      logFile.print(", count: ") &&
      logFile.println(pressCount)) {
    Serial.println("Data logged to SD card");
  } else {
    Serial.println("Failed to log data to SD card");
  }
  logFile.close();
}

// 记录原始数据功能
void logRawData(int fsr1Value, int fsr2Value) {
  // 获取当前时间
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char timeBuffer[20];
  strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", timeinfo);

  // 创建或打开原始数据文件
  File rawDataFile = SD.open("/fsr_raw_data.txt", FILE_APPEND);
  if (!rawDataFile) {
    Serial.println("Failed to open raw data file on SD card");
    return;
  }

  // 写入原始数据
  if (rawDataFile.print(timeBuffer) &&
      rawDataFile.print(", FSR1: ") &&
      rawDataFile.print(fsr1Value) &&
      rawDataFile.print(", FSR2: ") &&
      rawDataFile.println(fsr2Value)) {
    Serial.println("Raw data logged to SD card");
  } else {
    Serial.println("Failed to log raw data to SD card");
  }

  rawDataFile.close();
}
