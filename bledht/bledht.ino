//#include <DHT.h>
#include "dht11.h"

// based off of: http://dmitry.gr/index.php?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery


#define PIN_CE	9               //Output
#define PIN_nCS	10              //Output


#define MY_MAC_0	0xEF
#define MY_MAC_1	0xFF
#define MY_MAC_2	0xC0
#define MY_MAC_3	0xAA
#define MY_MAC_4	0x18
#define MY_MAC_5	0x00

#define DHTPIN 2
#define DHTTYPE DHT22

void btLeCrc(const uint8_t * data, uint8_t len, uint8_t * dst)
{

    uint8_t v, t, d;

    while (len--) {

        d = *data++;
        for (v = 0; v < 8; v++, d >>= 1) {

            t = dst[0] >> 7;

            dst[0] <<= 1;
            if (dst[1] & 0x80)
                dst[0] |= 1;
            dst[1] <<= 1;
            if (dst[2] & 0x80)
                dst[1] |= 1;
            dst[2] <<= 1;


            if (t != (d & 1)) {

                dst[2] ^= 0x5B;
                dst[1] ^= 0x06;
            }
        }
    }
}

uint8_t swapbits(uint8_t a)
{

    uint8_t v = 0;

    if (a & 0x80)
        v |= 0x01;
    if (a & 0x40)
        v |= 0x02;
    if (a & 0x20)
        v |= 0x04;
    if (a & 0x10)
        v |= 0x08;
    if (a & 0x08)
        v |= 0x10;
    if (a & 0x04)
        v |= 0x20;
    if (a & 0x02)
        v |= 0x40;
    if (a & 0x01)
        v |= 0x80;

    return v;
}

void btLeWhiten(uint8_t * data, uint8_t len, uint8_t whitenCoeff)
{

    uint8_t m;

    while (len--) {

        for (m = 1; m; m <<= 1) {

            if (whitenCoeff & 0x80) {

                whitenCoeff ^= 0x11;
                (*data) ^= m;
            }
            whitenCoeff <<= 1;
        }
        data++;
    }
}

static inline uint8_t btLeWhitenStart(uint8_t chan)
{
    //the value we actually use is what BT'd use left shifted one...makes our life easier

    return swapbits(chan) | 2;
}

void btLePacketEncode(uint8_t * packet, uint8_t len, uint8_t chan)
{
    //length is of packet, including crc. pre-populate crc in packet with initial crc value!

    uint8_t i, dataLen = len - 3;

    btLeCrc(packet, dataLen, packet + dataLen);
    for (i = 0; i < 3; i++, dataLen++)
        packet[dataLen] = swapbits(packet[dataLen]);
    btLeWhiten(packet, len, btLeWhitenStart(chan));
    for (i = 0; i < len; i++)
        packet[i] = swapbits(packet[i]);

}

uint8_t spi_byte(uint8_t byte)
{
    shiftOut(11, 13, MSBFIRST, byte);

    return byte;
}

void nrf_cmd(uint8_t cmd, uint8_t data)
{
    digitalWrite(PIN_nCS, LOW); //cbi(PORTB, PIN_nCS);
    spi_byte(cmd);
    spi_byte(data);
    digitalWrite(PIN_nCS, HIGH);        // sbi(PORTB, PIN_nCS); //Deselect chip
}

void nrf_simplebyte(uint8_t cmd)
{
    digitalWrite(PIN_nCS, LOW); //cbi(PORTB, PIN_nCS);
    spi_byte(cmd);
    digitalWrite(PIN_nCS, HIGH);        //sbi(PORTB, PIN_nCS);
}

void nrf_manybytes(uint8_t * data, uint8_t len)
{

    digitalWrite(PIN_nCS, LOW); // cbi(PORTB, PIN_nCS);
    do {

        spi_byte(*data++);

    } while (--len);
    digitalWrite(PIN_nCS, HIGH);        // sbi(PORTB, PIN_nCS);
}

static DHT11 dht(2);

void setup()
{
    // set the digital pin as output:
    pinMode(PIN_nCS, OUTPUT);
    pinMode(PIN_CE, OUTPUT);
    pinMode(11, OUTPUT);
    pinMode(13, OUTPUT);

    Serial.begin(9600);
    Serial.println("HELLO");

    dht.init();
}

void loop()
{
    static const uint8_t chRf[] = { 2, 26, 80 };
    static const uint8_t chLe[] = { 37, 38, 39 };
    uint8_t i, L, ch = 0;
    uint8_t buf[32];

    nrf_cmd(0x20, 0x12);        //on, no crc, int on RX/TX done
    nrf_cmd(0x21, 0x00);        //no auto-acknowledge
    nrf_cmd(0x22, 0x00);        //no RX
    nrf_cmd(0x23, 0x02);        //5-byte address
    nrf_cmd(0x24, 0x00);        //no auto-retransmit
    nrf_cmd(0x26, 0x06);        //1MBps at 0dBm
    nrf_cmd(0x27, 0x3E);        //clear various flags
    nrf_cmd(0x3C, 0x00);        //no dynamic payloads
    nrf_cmd(0x3D, 0x00);        //no features
    nrf_cmd(0x31, 32);          //always RX 32 bytes
    nrf_cmd(0x22, 0x01);        //RX on pipe 0

    buf[0] = 0x30;              //set addresses
    buf[1] = swapbits(0x8E);
    buf[2] = swapbits(0x89);
    buf[3] = swapbits(0xBE);
    buf[4] = swapbits(0xD6);
    nrf_manybytes(buf, 5);
    buf[0] = 0x2A;
    nrf_manybytes(buf, 5);



    while (1) {

        int rh;
        int t;

        dht.read(&rh, &t);

        char txt[20];
        sprintf(txt, "RH=%2d%%,T=%3d°C", rh, t); // RH= 37%,T= 21gC

        L = 0;

        buf[L++] = 0x42;        //PDU type, given address is random
        buf[L++] = 11 + 15;     //0x11 /*+ 8*/ + 10; //17 bytes of payload

        buf[L++] = MY_MAC_0;
        buf[L++] = MY_MAC_1;
        buf[L++] = MY_MAC_2;
        buf[L++] = MY_MAC_3;
        buf[L++] = MY_MAC_4;
        buf[L++] = MY_MAC_5;

        buf[L++] = 2;           //flags (LE-only, limited discovery mode)
        buf[L++] = 0x01;
        buf[L++] = 0x05;

        buf[L++] = 16;
        buf[L++] = 0x08;

        memcpy(&buf[L], txt, strlen(txt));
        L += strlen(txt);

        buf[1] = L - 2;

        buf[L++] = 0x55;        //CRC start value: 0x555555
        buf[L++] = 0x55;
        buf[L++] = 0x55;

        if (++ch == sizeof(chRf))
            ch = 0;

        nrf_cmd(0x25, chRf[ch]);
        nrf_cmd(0x27, 0x6E);    //clear flags

        btLePacketEncode(buf, L, chLe[ch]);

        nrf_simplebyte(0xE2);   //Clear RX Fifo
        nrf_simplebyte(0xE1);   //Clear TX Fifo

        digitalWrite(PIN_nCS, LOW);     // cbi(PORTB, PIN_nCS);
        spi_byte(0xA0);
        for (i = 0; i < L; i++)
            spi_byte(buf[i]);
        digitalWrite(PIN_nCS, HIGH);    // sbi(PORTB, PIN_nCS);

        nrf_cmd(0x20, 0x12);    //tx on
        digitalWrite(PIN_CE, HIGH);     // sbi(PORTB, PIN_CE);       //do tx
        delay(10);              //delay_ms(10);
        digitalWrite(PIN_CE, LOW);      // cbi(PORTB, PIN_CE);       (in preparation of switching to RX quickly)
        
        delay(1000);
    }
}
