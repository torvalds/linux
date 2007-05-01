/*
 * LED, LCD and Button panel driver for Cobalt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997 by Andrew Bose
 *
 * Linux kernel version history:
 *       March 2001: Ported from 2.0.34  by Liam Davies
 *
 */

// function headers

#define LCD_CHARS_PER_LINE 40
#define MAX_IDLE_TIME 120

struct lcd_display {
        unsigned buttons;
        int size1;
        int size2;
        unsigned char line1[LCD_CHARS_PER_LINE];
        unsigned char line2[LCD_CHARS_PER_LINE];
        unsigned char cursor_address;
        unsigned char character;
        unsigned char leds;
        unsigned char *RomImage;
};



#define LCD_DRIVER	"Cobalt LCD Driver v2.10"

#define LCD		"lcd: "

#define kLCD_IR		0x0F000000
#define kLCD_DR		0x0F000010
#define kGPI		0x0D000000
#define kLED		0x0C000000

#define kDD_R00         0x00
#define kDD_R01         0x27
#define kDD_R10         0x40
#define kDD_R11         0x67

#define kLCD_Addr       0x00000080

#define LCDTimeoutValue	0xfff


// Macros

#define LCDWriteData(x)	outl((x << 24), kLCD_DR)
#define LCDWriteInst(x)	outl((x << 24), kLCD_IR)

#define LCDReadData	(inl(kLCD_DR) >> 24)
#define LCDReadInst	(inl(kLCD_IR) >> 24)

#define GPIRead		(inl(kGPI) >> 24)

#define LEDSet(x)	outb((char)x, kLED)

#define WRITE_GAL(x,y)	outl(y, 0x04000000 | (x))
#define BusyCheck()	while ((LCDReadInst & 0x80) == 0x80)



/*
 * Function command codes for io_ctl.
 */
#define LCD_On			1
#define LCD_Off			2
#define LCD_Clear		3
#define LCD_Reset		4
#define LCD_Cursor_Left		5
#define LCD_Cursor_Right	6
#define LCD_Disp_Left		7
#define LCD_Disp_Right		8
#define LCD_Get_Cursor		9
#define LCD_Set_Cursor		10
#define LCD_Home		11
#define LCD_Read		12
#define LCD_Write		13
#define LCD_Cursor_Off		14
#define LCD_Cursor_On		15
#define LCD_Get_Cursor_Pos	16
#define LCD_Set_Cursor_Pos	17
#define LCD_Blink_Off           18

#define LED_Set			40
#define LED_Bit_Set		41
#define LED_Bit_Clear		42


//  Button defs
#define BUTTON_Read             50


// Ethernet LINK check hackaroo
#define LINK_Check              90
#define LINK_Check_2		91

//  Button patterns  _B - single layer lcd boards

#define BUTTON_NONE               0x3F
#define BUTTON_NONE_B             0xFE

#define BUTTON_Left               0x3B
#define BUTTON_Left_B             0xFA

#define BUTTON_Right              0x37
#define BUTTON_Right_B            0xDE

#define BUTTON_Up                 0x2F
#define BUTTON_Up_B               0xF6

#define BUTTON_Down               0x1F
#define BUTTON_Down_B             0xEE

#define BUTTON_Next               0x3D
#define BUTTON_Next_B             0x7E

#define BUTTON_Enter              0x3E
#define BUTTON_Enter_B            0xBE

#define BUTTON_Reset_B            0xFC


// debounce constants

#define BUTTON_SENSE            160000
#define BUTTON_DEBOUNCE		5000


//  Galileo register stuff

#define kGal_DevBank2Cfg        0x1466DB33
#define kGal_DevBank2PReg       0x464
#define kGal_DevBank3Cfg        0x146FDFFB
#define kGal_DevBank3PReg       0x468

// Network

#define kIPADDR			1
#define kNETMASK		2
#define kGATEWAY		3
#define kDNS			4

#define kClassA			5
#define kClassB			6
#define kClassC			7

