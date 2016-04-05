/*
 * This is our library for the Adafruit  ILI9341 Breakout and Shield
 * ----> http://www.adafruit.com/products/1651
 *
 * Check out the links above for our tutorials and wiring diagrams
 * These displays use SPI to communicate, 4 or 5 pins are required to
 * interface (RST is optional)
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing
 * products from Adafruit!
 *
 * Written by Limor Fried/Ladyada for Adafruit Industries.
 * MIT license, all text above must be included in any redistribution
 */

#ifndef __HX8357_H__
#define __HX8357_H__

#define HX8357D 0xD
#define HX8357B 0xB

#define HX8357_TFTWIDTH  320
#define HX8357_TFTHEIGHT 480

#define HX8357_SETOSC 0xB0
#define HX8357_SETPWR1 0xB1
#define HX8357B_SETDISPLAY 0xB2
#define HX8357_SETRGB 0xB3
#define HX8357D_SETCOM  0xB6

#define HX8357B_SETDISPMODE  0xB4
#define HX8357D_SETCYC  0xB4
#define HX8357B_SETOTP 0xB7
#define HX8357D_SETC 0xB9

#define HX8357B_SET_PANEL_DRIVING 0xC0
#define HX8357D_SETSTBA 0xC0
#define HX8357B_SETDGC  0xC1
#define HX8357B_SETID  0xC3
#define HX8357B_SETDDB  0xC4
#define HX8357B_SETDISPLAYFRAME 0xC5
#define HX8357B_GAMMASET 0xC8
#define HX8357B_SETCABC  0xC9
#define HX8357_SETPANEL  0xCC

#define HX8357B_SETPOWER 0xD0
#define HX8357B_SETVCOM 0xD1
#define HX8357B_SETPWRNORMAL 0xD2

#define HX8357B_RDID1   0xDA
#define HX8357B_RDID2   0xDB
#define HX8357B_RDID3   0xDC
#define HX8357B_RDID4   0xDD

#define HX8357D_SETGAMMA 0xE0

#define HX8357B_SETGAMMA 0xC8
#define HX8357B_SETPANELRELATED  0xE9

/* Color definitions */
#define	HX8357_BLACK   0x0000
#define	HX8357_BLUE    0x001F
#define	HX8357_RED     0xF800
#define	HX8357_GREEN   0x07E0
#define HX8357_CYAN    0x07FF
#define HX8357_MAGENTA 0xF81F
#define HX8357_YELLOW  0xFFE0
#define HX8357_WHITE   0xFFFF

#endif /* __HX8357_H__ */
