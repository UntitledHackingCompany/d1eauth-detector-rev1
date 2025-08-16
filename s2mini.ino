#include <WiFi.h>
#include <WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Preferences.h> // For persistent storage

#define DEAUTH_IN_PIN  16
#define DEBOUNCE_MS    200

const char* WIFI_SSID = "WIFI NAME";
const char* WIFI_PASS = "WIFI PASS";

WebServer server(80);
Preferences prefs; // NVS storage object

WiFiUDP ntpUDP;
long gmtOffset = 0; // seconds offset from GMT
NTPClient timeClient(ntpUDP, "pool.ntp.org", gmtOffset, 60000);

struct Event {
  String timestamp;
  String type;
};

#define MAX_EVENTS 200
Event logs[MAX_EVENTS];
int logIndex = 0;

bool lastState = false;
unsigned long lastDebounceTime = 0;
bool firstEvent = true;

// HTML Helpers
String htmlHeader(String title) {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>)rawliteral" + title + R"rawliteral(</title>
<style>
body { margin:0; font-family: Arial, sans-serif; background-color:#1a1a1a; color:#eee; }
.sidebar { position:fixed; top:0; left:0; width:200px; height:100%; background-color:#111; padding-top:20px; }
.sidebar a { padding:12px; text-decoration:none; font-size:18px; color:#ff4444; display:block; }
.sidebar a:hover { background-color:#333; }
.content { margin-left:200px; padding:20px; }
table { border-collapse:collapse; width:100%; }
th, td { border:1px solid #333; padding:8px; }
th { background-color:#ff4444; color:white; }
tr:nth-child(even) { background-color:#2a2a2a; }
button, input[type=submit] { background-color:#ff4444; border:none; padding:10px; color:white; font-size:16px; cursor:pointer; }
button:hover, input[type=submit]:hover { background-color:#cc0000; }
</style>
</head>
<body>
<div class="sidebar">
<a href="/">Logs</a>
<a href="/status">Status</a>
<a href="/settings">Settings</a>
</div>
<div class="content">
<h1>)rawliteral" + title + R"rawliteral(</h1>
)rawliteral";
}

String htmlFooter() {
  return "</div></body></html>";
}

// Logs page
String getLogsHTML() {
  String html = htmlHeader("Logs");
  html += "<form action='/clear' method='get'><button type='submit'>Clear Logs</button></form>";
  html += "<table><tr><th>#</th><th>Event</th><th>Timestamp</th></tr><tbody id='logBody'>";
  for (int i = 0; i < logIndex; i++) {
    html += "<tr><td>" + String(i+1) + "</td><td>" + logs[i].type + "</td><td>" + logs[i].timestamp + "</td></tr>";
  }
  html += "</tbody></table>";
  html += R"rawliteral(
  <script>
  function updateLogs(){
    fetch('/logsData').then(res => res.text()).then(html => {
      document.getElementById('logBody').innerHTML = html;
    });
  }
  setInterval(updateLogs, 2000);
  </script>
  )rawliteral";
  html += htmlFooter();
  return html;
}


// Status page
String getStatusHTML() {
  return htmlHeader("Status") + R"rawliteral(
<p>D4 Pin Status: <span id="statusVal">Loading...</span></p>
<script>
function updateStatus(){
  fetch('/statusData').then(res => res.text()).then(txt => {
    document.getElementById('statusVal').innerText = txt;
  });
}
setInterval(updateStatus, 1000);
updateStatus();
</script>
)rawliteral" + htmlFooter();
}

// Settings page
String getSettingsHTML() {
  return htmlHeader("Settings") + R"rawliteral(
<form action="/setTZ" method="get">
<label for="tz">Timezone Offset (e.g., -3 for GMT-0300):</label><br>
<input type="number" name="tz" step="1" min="-12" max="14">
<input type="submit" value="Save">
</form>
<p>Current offset: )rawliteral" + String(gmtOffset / 3600) + " hours</p>" + htmlFooter();
}

// Handlers
void handleRoot() { server.send(200, "text/html", getLogsHTML()); }
void handleStatus() { server.send(200, "text/html", getStatusHTML()); }
void handleSettings() { server.send(200, "text/html", getSettingsHTML()); }

void handleClear() {
  logIndex = 0;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStatusData() {
  bool current = digitalRead(DEAUTH_IN_PIN);
  server.send(200, "text/plain", current ? "HIGH" : "LOW");
}

void handleLogsData() {
  String tableRows;
  for (int i = 0; i < logIndex; i++) {
    tableRows += "<tr><td>" + String(i+1) + "</td><td>" + logs[i].type + "</td><td>" + logs[i].timestamp + "</td></tr>";
  }
  server.send(200, "text/html", tableRows);
}

void handleSetTZ() {
  if (server.hasArg("tz")) {
    int offsetHours = server.arg("tz").toInt();
    gmtOffset = offsetHours * 3600;
    timeClient.setTimeOffset(gmtOffset);
    prefs.putInt("tz_offset", offsetHours); // store in NVS
  }
  server.sendHeader("Location", "/settings");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  pinMode(DEAUTH_IN_PIN, INPUT);

  // Load saved timezone from NVS
  prefs.begin("deauthdet", false);
  int savedOffsetHours = prefs.getInt("tz_offset", 0);
  gmtOffset = savedOffsetHours * 3600;

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  timeClient.begin();
  timeClient.setTimeOffset(gmtOffset);
  timeClient.update();

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/settings", handleSettings);
  server.on("/clear", handleClear);
  server.on("/statusData", handleStatusData);
  server.on("/setTZ", handleSetTZ);
  server.on("/logsData", handleLogsData);
  
  server.begin();

  Serial.println("Web server started");
}

void loop() {
  server.handleClient();
  timeClient.update();

  bool current = digitalRead(DEAUTH_IN_PIN);
  unsigned long now = millis();

  if (current && !lastState && (now - lastDebounceTime > DEBOUNCE_MS)) {
    lastDebounceTime = now;
    if (logIndex < MAX_EVENTS) {
      logs[logIndex].timestamp = timeClient.getFormattedTime();
      logs[logIndex].type = firstEvent ? "STARTUP" : "DEAUTH";
      firstEvent = false;
      logIndex++;
    }
    Serial.println(logs[logIndex-1].type + " detected at " + logs[logIndex-1].timestamp);
  }
  lastState = current;
}
