// Tamagotchi da vida - ESP32-C3 Super Mini + TFT ST7789 240x320
// Bichinho desenhado 100% em codigo (sem imagens).
#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ================= PINOS =================
#define TFT_CS    7
#define TFT_DC    2
#define TFT_RST   3
#define TFT_SCLK  4
#define TFT_MOSI  6

#define BTN_A     0    // Comer
#define BTN_B     1    // Brincar
#define BTN_C     9    // Dormir/Acordar (botao BOOT da placa)
#define BUZZER    5
#define LED_R     10
#define LED_G     20
#define LED_Y     21

#define BUZZER_PASSIVO true   // false se o buzzer for ativo

// ================= JOGO (ajuste aqui) =================
// Segundos para perder 1 ponto. Valores baixos = teste rapido.
// Para uso real, tente 60 a 300.
#define T_COMIDA   6
#define T_ALEGRIA  5
#define T_ENERGIA  8

#define DUR_COMER    1500   // ms da animacao de comer
#define DUR_BRINCAR  1500   // ms da animacao de brincar

// ================= LAYOUT =================
#define SCREEN_W 240
#define SCREEN_H 320
#define TOP_H    40
#define AREA_Y   40
#define AREA_H   200
#define BOT_Y    240

#define PET_W 160
#define PET_H 160
#define PET_X ((SCREEN_W - PET_W) / 2)
#define PET_Y (AREA_Y + (AREA_H - PET_H) / 2)

#define C565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
const uint16_t COR_OUTLINE = C565(60, 140, 100);
const uint16_t COR_BODY    = C565(130, 225, 170);
const uint16_t COR_BELLY   = C565(205, 245, 220);
const uint16_t COR_DARK    = C565(30, 30, 50);
const uint16_t COR_CHEEK   = C565(255, 150, 170);
const uint16_t COR_SHADOW  = C565(35, 35, 45);
const uint16_t COR_RED     = C565(230, 50, 50);
const uint16_t COR_LEAF    = C565(60, 180, 60);
const uint16_t COR_WHITE   = 0xFFFF;

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(PET_W, PET_H);   // desenha aqui e envia de uma vez (sem piscar)
Preferences prefs;

// ================= ESTADO =================
int comida = 80, alegria = 80, energia = 80;   // 0..100 (alto = bom)
bool dormindo = false;
bool mudou = true;
uint32_t segundos = 0;
uint32_t comendoAte = 0, brincandoAte = 0;

enum Humor { H_FELIZ, H_FOME, H_ENTEDIADO, H_CANSADO, H_DORMINDO };

int menorStatus() { return min(comida, min(alegria, energia)); }

Humor humorAtual() {
  if (dormindo) return H_DORMINDO;
  int m = menorStatus();
  if (m >= 50) return H_FELIZ;
  if (m == comida) return H_FOME;
  if (m == alegria) return H_ENTEDIADO;
  return H_CANSADO;
}

// ================= BOTOES =================
struct Botao { uint8_t pino; bool estado; uint32_t ultimaMudanca; };
Botao bA = {BTN_A, true, 0}, bB = {BTN_B, true, 0}, bC = {BTN_C, true, 0};

bool apertou(Botao &b) {
  bool lido = digitalRead(b.pino);
  if (lido != b.estado && millis() - b.ultimaMudanca > 30) {
    b.estado = lido;
    b.ultimaMudanca = millis();
    return !lido;            // LOW = apertado (INPUT_PULLUP)
  }
  return false;
}

// ================= BUZZER (sem travar o loop) =================
uint32_t beepFim = 0;
void beep(int freq, int ms) {
  if (BUZZER_PASSIVO) tone(BUZZER, freq); else digitalWrite(BUZZER, HIGH);
  beepFim = millis() + ms;
}
void beepTick() {
  if (beepFim && (int32_t)(millis() - beepFim) >= 0) {
    if (BUZZER_PASSIVO) noTone(BUZZER); else digitalWrite(BUZZER, LOW);
    beepFim = 0;
  }
}

// ================= DESENHO DO BICHINHO =================
// estilo: 0 aberto, 1 piscando, 2 feliz (^), 3 fechado (u), 4 sonolento, 5 triste
void olho(int x, int y, int estilo, int lado) {
  switch (estilo) {
    case 0:
    case 5:
    case 4:
      canvas.fillCircle(x, y, 9, COR_DARK);
      canvas.fillCircle(x - 3, y - 3, 3, COR_WHITE);
      if (estilo == 4) {                              // palpebra caida
        canvas.fillRect(x - 10, y - 10, 21, 10, COR_BODY);
        canvas.fillRect(x - 10, y, 21, 2, COR_DARK);
      }
      if (estilo == 5)                                // sobrancelha triste
        canvas.drawLine(x + lado * 10, y - 9, x - lado * 8, y - 16, COR_DARK);
      break;
    case 1:
      canvas.fillRect(x - 9, y, 18, 3, COR_DARK);
      break;
    case 2:
      for (int r = 7; r <= 9; r++) canvas.drawCircleHelper(x, y + 4, r, 0x3, COR_DARK);
      break;
    case 3:
      for (int r = 7; r <= 9; r++) canvas.drawCircleHelper(x, y - 4, r, 0xC, COR_DARK);
      break;
  }
}

void bocaSorriso(int cx, int cy) {
  canvas.fillCircle(cx, cy + 10, 10, COR_DARK);
  canvas.fillRect(cx - 11, cy - 1, 23, 11, COR_BODY);     // esconde a metade de cima
  canvas.fillCircle(cx, cy + 17, 3, COR_CHEEK);           // lingua
}
void bocaO(int cx, int cy, int r) { canvas.fillCircle(cx, cy + 12, r, COR_DARK); }
void bocaReta(int cx, int cy)     { canvas.fillRect(cx - 8, cy + 12, 16, 3, COR_DARK); }

void maca(int x, int y, int r) {
  canvas.fillCircle(x, y, r, COR_RED);
  canvas.fillRect(x - 1, y - r - 3, 2, 4, COR_LEAF);
}

void estrela(int x, int y) {
  canvas.drawLine(x - 4, y, x + 4, y, 0xFFE0);
  canvas.drawLine(x, y - 4, x, y + 4, 0xFFE0);
}

void desenhaPet() {
  uint32_t agora = millis();
  Humor h = humorAtual();
  bool comendo = agora < comendoAte;
  bool brincando = agora < brincandoAte;

  canvas.fillScreen(0);

  // movimento
  float bob;
  if (dormindo)              bob = sin(agora / 900.0) * 2;
  else if (h == H_CANSADO)   bob = sin(agora / 700.0) * 2;
  else                       bob = sin(agora / 300.0) * 3;
  int cy = 86 + (int)bob;
  int cx = 80;
  if (brincando) {
    float p = 1.0f - (brincandoAte - agora) / (float)DUR_BRINCAR;   // 0..1
    cy -= (int)(fabs(sin(p * PI * 3)) * 18);
  }
  if (!dormindo && menorStatus() < 20) cx += ((agora / 80) % 2) ? 2 : -2;   // tremendo

  // sombra
  canvas.fillRoundRect(cx - 40, 146, 80, 8, 4, COR_SHADOW);

  // orelhas
  canvas.fillTriangle(cx - 42, cy - 18, cx - 40, cy - 62, cx - 8, cy - 40, COR_OUTLINE);
  canvas.fillTriangle(cx - 37, cy - 30, cx - 36, cy - 54, cx - 20, cy - 40, COR_CHEEK);
  canvas.fillTriangle(cx + 42, cy - 18, cx + 40, cy - 62, cx + 8, cy - 40, COR_OUTLINE);
  canvas.fillTriangle(cx + 37, cy - 30, cx + 36, cy - 54, cx + 20, cy - 40, COR_CHEEK);

  // pes
  canvas.fillRoundRect(cx - 38, cy + 38, 26, 14, 6, COR_OUTLINE);
  canvas.fillRoundRect(cx + 12, cy + 38, 26, 14, 6, COR_OUTLINE);

  // corpo
  canvas.fillRoundRect(cx - 50, cy - 46, 100, 92, 44, COR_OUTLINE);
  canvas.fillRoundRect(cx - 48, cy - 44, 96, 88, 42, COR_BODY);
  canvas.fillRoundRect(cx - 26, cy + 22, 52, 20, 10, COR_BELLY);

  // bracos
  int armY = brincando ? cy - 14 + (int)(sin(agora / 80.0) * 5) : cy + 12;
  canvas.fillCircle(cx - 50, armY, 10, COR_OUTLINE);
  canvas.fillCircle(cx - 50, armY, 8, COR_BODY);
  canvas.fillCircle(cx + 50, armY, 10, COR_OUTLINE);
  canvas.fillCircle(cx + 50, armY, 8, COR_BODY);

  // bochechas
  canvas.fillCircle(cx - 38, cy + 6, 6, COR_CHEEK);
  canvas.fillCircle(cx + 38, cy + 6, 6, COR_CHEEK);

  // rosto
  bool piscando = (agora % 3500) < 150;
  int ex = 24, ey = cy - 8;
  int estOlho;
  switch (h) {
    case H_FELIZ:      estOlho = piscando ? 1 : 0; break;
    case H_FOME:       estOlho = 5; break;
    case H_ENTEDIADO:  estOlho = 4; break;
    case H_CANSADO:    estOlho = 4; break;
    default:           estOlho = 3; break;   // dormindo
  }
  if (brincando) estOlho = 2;
  if (comendo)   estOlho = piscando ? 1 : 0;
  olho(cx - ex, ey, estOlho, -1);
  olho(cx + ex, ey, estOlho, +1);

  if (comendo) {
    if ((agora / 150) % 2) bocaO(cx, cy, 8); else bocaO(cx, cy, 3);
  } else if (brincando) {
    bocaSorriso(cx, cy);
  } else {
    switch (h) {
      case H_FELIZ:      bocaSorriso(cx, cy); break;
      case H_FOME:       bocaO(cx, cy, 4); break;
      case H_ENTEDIADO:  bocaReta(cx, cy); break;
      case H_CANSADO:    bocaO(cx, cy, 6 + (int)(sin(agora / 500.0) * 2)); break;
      case H_DORMINDO:   bocaO(cx, cy, 2); break;
    }
  }

  // extras por situacao
  if (comendo) {
    float p = 1.0f - (comendoAte - agora) / (float)DUR_COMER;     // 0..1
    if (p < 0.85f) {
      int ax = 132 - (int)((132 - cx) * p);
      int ay = 28 + (int)((cy + 14 - 28) * p);
      maca(ax, ay, 9 - (int)(6 * p));
    }
  } else if (h == H_FOME) {
    canvas.fillCircle(136, 22, 17, COR_WHITE);                    // balao de pensamento
    canvas.fillCircle(120, 46, 4, COR_WHITE);
    canvas.fillCircle(114, 56, 2, COR_WHITE);
    maca(136, 24, 9);
  } else if (h == H_ENTEDIADO) {
    canvas.setTextSize(2);
    canvas.setTextColor(COR_WHITE);
    canvas.setCursor(112, 8);
    canvas.print("...");
  } else if (h == H_DORMINDO) {
    canvas.setTextSize(2);
    canvas.setTextColor(C565(120, 200, 255));
    uint32_t t = (agora / 20) % 100;
    for (int i = 0; i < 3; i++) {
      int p = (t + i * 33) % 100;
      canvas.setCursor(cx + 38 + p / 5, cy - 20 - p / 2);
      canvas.print("Z");
    }
  }
  if (brincando) {
    for (int i = 0; i < 4; i++) {
      float a = agora / 300.0 + i * PI / 2;
      estrela(80 + (int)(cos(a) * 68), 70 + (int)(sin(a) * 45));
    }
  }

  tft.drawRGBBitmap(PET_X, PET_Y, canvas.getBuffer(), PET_W, PET_H);
}

// ================= INTERFACE =================
void desenhaBarra(int idx, const char *nome, int val) {
  int x = idx * 80 + 4;
  uint16_t cor = val >= 50 ? ST77XX_GREEN : (val >= 20 ? ST77XX_YELLOW : ST77XX_RED);
  tft.fillRect(x, 0, 76, TOP_H, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(x, 6);
  tft.print(nome);
  tft.drawRect(x, 18, 72, 12, ST77XX_WHITE);
  tft.fillRect(x + 2, 20, (68 * val) / 100, 8, cor);
}

void desenhaRodape() {
  tft.fillRect(0, BOT_Y, SCREEN_W, SCREEN_H - BOT_Y, ST77XX_BLACK);

  const char *humor;
  uint16_t cor = menorStatus() < 20 ? ST77XX_RED : ST77XX_YELLOW;
  switch (humorAtual()) {
    case H_DORMINDO:  humor = "Zzz..."; cor = ST77XX_CYAN; break;
    case H_FELIZ:     humor = "Feliz"; cor = ST77XX_GREEN; break;
    case H_FOME:      humor = "Com fome"; break;
    case H_ENTEDIADO: humor = "Entediado"; break;
    default:          humor = "Cansado"; break;
  }
  tft.setTextSize(3);
  tft.setTextColor(cor);
  tft.setCursor((SCREEN_W - strlen(humor) * 18) / 2, BOT_Y + 6);
  tft.print(humor);

  const char *letras[3] = {"A", "B", "C"};
  const char *nomes[3]  = {"Comer", "Brinca", dormindo ? "Acorda" : "Dorme"};
  for (int i = 0; i < 3; i++) {
    int w = strlen(nomes[i]) * 12;
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(i * 80 + 37, BOT_Y + 48);
    tft.print(letras[i]);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_ORANGE);
    tft.setCursor(i * 80 + (80 - w) / 2, BOT_Y + 62);
    tft.print(nomes[i]);
  }
}

void atualizaLeds() {
  int m = menorStatus();
  digitalWrite(LED_G, m >= 50);
  digitalWrite(LED_Y, m >= 20 && m < 50);
  digitalWrite(LED_R, m < 20);
}

void redesenha() {
  desenhaBarra(0, "COMIDA", comida);
  desenhaBarra(1, "ALEGRIA", alegria);
  desenhaBarra(2, "ENERGIA", energia);
  desenhaRodape();
  atualizaLeds();
}

// ================= ACOES =================
void acaoComer() {
  if (dormindo) return;
  if (comida >= 100) { beep(200, 150); return; }
  comida = min(100, comida + 20);
  comendoAte = millis() + DUR_COMER;
  beep(880, 80);
  mudou = true;
}

void acaoBrincar() {
  if (dormindo) return;
  if (energia < 10) { beep(200, 200); return; }   // cansado demais
  alegria = min(100, alegria + 20);
  energia = max(0, energia - 8);
  comida  = max(0, comida - 3);
  brincandoAte = millis() + DUR_BRINCAR;
  beep(1200, 80);
  mudou = true;
}

void alternaSono() {
  dormindo = !dormindo;
  comendoAte = brincandoAte = 0;
  beep(500, 100);
  mudou = true;
}

void passaUmSegundo() {
  segundos++;
  if (dormindo) {
    energia = min(100, energia + 2);
    if (segundos % (T_COMIDA * 3) == 0) comida = max(0, comida - 1);
    if (energia >= 100) dormindo = false;
  } else {
    if (segundos % T_COMIDA  == 0) comida  = max(0, comida - 1);
    if (segundos % T_ALEGRIA == 0) alegria = max(0, alegria - 1);
    if (segundos % T_ENERGIA == 0) energia = max(0, energia - 1);
  }
  mudou = true;
}

void salvar() {
  prefs.putInt("comida", comida);
  prefs.putInt("alegria", alegria);
  prefs.putInt("energia", energia);
}

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(115200);
  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_C, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_Y, OUTPUT);

  prefs.begin("pet", false);
  comida  = prefs.getInt("comida", 80);
  alegria = prefs.getInt("alegria", 80);
  energia = prefs.getInt("energia", 80);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(240, 320);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  if (!canvas.getBuffer()) Serial.println("Sem memoria para o canvas!");
  beep(1000, 100);
}

void loop() {
  static uint32_t ultimoSeg = 0, ultimoSave = 0, ultimoAlerta = 0, ultimoFrame = 0;

  if (apertou(bA)) acaoComer();
  if (apertou(bB)) acaoBrincar();
  if (apertou(bC)) alternaSono();

  if (millis() - ultimoSeg >= 1000) { ultimoSeg += 1000; passaUmSegundo(); }

  if (mudou) { mudou = false; redesenha(); }

  if (millis() - ultimoFrame >= 66) {             // ~15 quadros por segundo
    ultimoFrame = millis();
    if (canvas.getBuffer()) desenhaPet();
  }

  // alerta sonoro quando algum status esta critico
  if (!dormindo && menorStatus() < 20 && millis() - ultimoAlerta > 10000) {
    ultimoAlerta = millis();
    beep(1000, 150);
  }

  if (millis() - ultimoSave > 30000) { ultimoSave = millis(); salvar(); }

  beepTick();
}