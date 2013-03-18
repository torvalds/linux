/*
 * KB3310B Embedded Controller
 *
 *  Copyright (C) 2008 Lemote Inc.
 *  Author: liujl <liujl@lemote.com>, 2008-03-14
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _EC_KB3310B_H
#define _EC_KB3310B_H

extern unsigned char ec_read(unsigned short addr);
extern void ec_write(unsigned short addr, unsigned char val);
extern int ec_query_seq(unsigned char cmd);
extern int ec_query_event_num(void);
extern int ec_get_event_num(void);

typedef int (*sci_handler) (int status);
extern sci_handler yeeloong_report_lid_status;

#define SCI_IRQ_NUM 0x0A

/*
 * The following registers are determined by the EC index configuration.
 * 1, fill the PORT_HIGH as EC register high part.
 * 2, fill the PORT_LOW as EC register low part.
 * 3, fill the PORT_DATA as EC register write data or get the data from it.
 */
#define EC_IO_PORT_HIGH 0x0381
#define EC_IO_PORT_LOW	0x0382
#define EC_IO_PORT_DATA 0x0383

/*
 * EC delay time is 500us for register and status access
 */
#define EC_REG_DELAY	500	/* unit : us */
#define EC_CMD_TIMEOUT	0x1000

/*
 * EC access port for SCI communication
 */
#define EC_CMD_PORT		0x66
#define EC_STS_PORT		0x66
#define EC_DAT_PORT		0x62
#define CMD_INIT_IDLE_MODE	0xdd
#define CMD_EXIT_IDLE_MODE	0xdf
#define CMD_INIT_RESET_MODE	0xd8
#define CMD_REBOOT_SYSTEM	0x8c
#define CMD_GET_EVENT_NUM	0x84
#define CMD_PROGRAM_PIECE	0xda

/* temperature & fan registers */
#define REG_TEMPERATURE_VALUE	0xF458
#define REG_FAN_AUTO_MAN_SWITCH 0xF459
#define BIT_FAN_AUTO		0
#define BIT_FAN_MANUAL		1
#define REG_FAN_CONTROL		0xF4D2
#define BIT_FAN_CONTROL_ON	(1 << 0)
#define BIT_FAN_CONTROL_OFF	(0 << 0)
#define REG_FAN_STATUS		0xF4DA
#define BIT_FAN_STATUS_ON	(1 << 0)
#define BIT_FAN_STATUS_OFF	(0 << 0)
#define REG_FAN_SPEED_HIGH	0xFE22
#define REG_FAN_SPEED_LOW	0xFE23
#define REG_FAN_SPEED_LEVEL	0xF4CC
/* fan speed divider */
#define FAN_SPEED_DIVIDER	480000	/* (60*1000*1000/62.5/2)*/

/* battery registers */
#define REG_BAT_DESIGN_CAP_HIGH		0xF77D
#define REG_BAT_DESIGN_CAP_LOW		0xF77E
#define REG_BAT_FULLCHG_CAP_HIGH	0xF780
#define REG_BAT_FULLCHG_CAP_LOW		0xF781
#define REG_BAT_DESIGN_VOL_HIGH		0xF782
#define REG_BAT_DESIGN_VOL_LOW		0xF783
#define REG_BAT_CURRENT_HIGH		0xF784
#define REG_BAT_CURRENT_LOW		0xF785
#define REG_BAT_VOLTAGE_HIGH		0xF786
#define REG_BAT_VOLTAGE_LOW		0xF787
#define REG_BAT_TEMPERATURE_HIGH	0xF788
#define REG_BAT_TEMPERATURE_LOW		0xF789
#define REG_BAT_RELATIVE_CAP_HIGH	0xF492
#define REG_BAT_RELATIVE_CAP_LOW	0xF493
#define REG_BAT_VENDOR			0xF4C4
#define FLAG_BAT_VENDOR_SANYO		0x01
#define FLAG_BAT_VENDOR_SIMPLO		0x02
#define REG_BAT_CELL_COUNT		0xF4C6
#define FLAG_BAT_CELL_3S1P		0x03
#define FLAG_BAT_CELL_3S2P		0x06
#define REG_BAT_CHARGE			0xF4A2
#define FLAG_BAT_CHARGE_DISCHARGE	0x01
#define FLAG_BAT_CHARGE_CHARGE		0x02
#define FLAG_BAT_CHARGE_ACPOWER		0x00
#define REG_BAT_STATUS			0xF4B0
#define BIT_BAT_STATUS_LOW		(1 << 5)
#define BIT_BAT_STATUS_DESTROY		(1 << 2)
#define BIT_BAT_STATUS_FULL		(1 << 1)
#define BIT_BAT_STATUS_IN		(1 << 0)
#define REG_BAT_CHARGE_STATUS		0xF4B1
#define BIT_BAT_CHARGE_STATUS_OVERTEMP	(1 << 2)
#define BIT_BAT_CHARGE_STATUS_PRECHG	(1 << 1)
#define REG_BAT_STATE			0xF482
#define BIT_BAT_STATE_CHARGING		(1 << 1)
#define BIT_BAT_STATE_DISCHARGING	(1 << 0)
#define REG_BAT_POWER			0xF440
#define BIT_BAT_POWER_S3		(1 << 2)
#define BIT_BAT_POWER_ON		(1 << 1)
#define BIT_BAT_POWER_ACIN		(1 << 0)

/* other registers */
/* Audio: rd/wr */
#define REG_AUDIO_VOLUME	0xF46C
#define REG_AUDIO_MUTE		0xF4E7
#define REG_AUDIO_BEEP		0xF4D0
/* USB port power or not: rd/wr */
#define REG_USB0_FLAG		0xF461
#define REG_USB1_FLAG		0xF462
#define REG_USB2_FLAG		0xF463
#define BIT_USB_FLAG_ON		1
#define BIT_USB_FLAG_OFF	0
/* LID */
#define REG_LID_DETECT		0xF4BD
#define BIT_LID_DETECT_ON	1
#define BIT_LID_DETECT_OFF	0
/* CRT */
#define REG_CRT_DETECT		0xF4AD
#define BIT_CRT_DETECT_PLUG	1
#define BIT_CRT_DETECT_UNPLUG	0
/* LCD backlight brightness adjust: 9 levels */
#define REG_DISPLAY_BRIGHTNESS	0xF4F5
/* Black screen Status */
#define BIT_DISPLAY_LCD_ON	1
#define BIT_DISPLAY_LCD_OFF	0
/* LCD backlight control: off/restore */
#define REG_BACKLIGHT_CTRL	0xF7BD
#define BIT_BACKLIGHT_ON	1
#define BIT_BACKLIGHT_OFF	0
/* Reset the machine auto-clear: rd/wr */
#define REG_RESET		0xF4EC
#define BIT_RESET_ON		1
/* Light the led: rd/wr */
#define REG_LED			0xF4C8
#define BIT_LED_RED_POWER	(1 << 0)
#define BIT_LED_ORANGE_POWER	(1 << 1)
#define BIT_LED_GREEN_CHARGE	(1 << 2)
#define BIT_LED_RED_CHARGE	(1 << 3)
#define BIT_LED_NUMLOCK		(1 << 4)
/* Test led mode, all led on/off */
#define REG_LED_TEST		0xF4C2
#define BIT_LED_TEST_IN		1
#define BIT_LED_TEST_OUT	0
/* Camera on/off */
#define REG_CAMERA_STATUS	0xF46A
#define BIT_CAMERA_STATUS_ON	1
#define BIT_CAMERA_STATUS_OFF	0
#define REG_CAMERA_CONTROL	0xF7B7
#define BIT_CAMERA_CONTROL_OFF	0
#define BIT_CAMERA_CONTROL_ON	1
/* Wlan Status */
#define REG_WLAN		0xF4FA
#define BIT_WLAN_ON		1
#define BIT_WLAN_OFF		0
#define REG_DISPLAY_LCD		0xF79F

/* SCI Event Number from EC */
enum {
	EVENT_LID = 0x23,	/*  LID open/close */
	EVENT_DISPLAY_TOGGLE,	/*  Fn+F3 for display switch */
	EVENT_SLEEP,		/*  Fn+F1 for entering sleep mode */
	EVENT_OVERTEMP,		/*  Over-temperature happened */
	EVENT_CRT_DETECT,	/*  CRT is connected */
	EVENT_CAMERA,		/*  Camera on/off */
	EVENT_USB_OC2,		/*  USB2 Over Current occurred */
	EVENT_USB_OC0,		/*  USB0 Over Current occurred */
	EVENT_BLACK_SCREEN,	/*  Turn on/off backlight */
	EVENT_AUDIO_MUTE,	/*  Mute on/off */
	EVENT_DISPLAY_BRIGHTNESS,/* LCD backlight brightness adjust */
	EVENT_AC_BAT,		/*  AC & Battery relative issue */
	EVENT_AUDIO_VOLUME,	/*  Volume adjust */
	EVENT_WLAN,		/*  Wlan on/off */
	EVENT_END
};

#endif /* !_EC_KB3310B_H */
