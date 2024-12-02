/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GPIOs and interrupts for Palm Tungsten|E2 Handheld Computer
 *
 * Author:
 *		Carlos Eduardo Medaglia Dyonisio <cadu@nerdfeliz.com>
 */

#ifndef _INCLUDE_PALMTE2_H_
#define _INCLUDE_PALMTE2_H_

/** HERE ARE GPIOs **/

/* GPIOs */
#define GPIO_NR_PALMTE2_POWER_DETECT		9
#define GPIO_NR_PALMTE2_HOTSYNC_BUTTON_N	4
#define GPIO_NR_PALMTE2_EARPHONE_DETECT		15

/* SD/MMC */
#define GPIO_NR_PALMTE2_SD_DETECT_N		10
#define GPIO_NR_PALMTE2_SD_POWER		55
#define GPIO_NR_PALMTE2_SD_READONLY		51

/* IRDA -  disable GPIO connected to SD pin of tranceiver (TFBS4710?) ? */
#define GPIO_NR_PALMTE2_IR_DISABLE		48

/* USB */
#define GPIO_NR_PALMTE2_USB_DETECT_N		35
#define GPIO_NR_PALMTE2_USB_PULLUP		53

/* LCD/BACKLIGHT */
#define GPIO_NR_PALMTE2_BL_POWER		56
#define GPIO_NR_PALMTE2_LCD_POWER		37

/* KEYS */
#define GPIO_NR_PALMTE2_KEY_NOTES	5
#define GPIO_NR_PALMTE2_KEY_TASKS	7
#define GPIO_NR_PALMTE2_KEY_CALENDAR	11
#define GPIO_NR_PALMTE2_KEY_CONTACTS	13
#define GPIO_NR_PALMTE2_KEY_CENTER	14
#define GPIO_NR_PALMTE2_KEY_LEFT	19
#define GPIO_NR_PALMTE2_KEY_RIGHT	20
#define GPIO_NR_PALMTE2_KEY_DOWN	21
#define GPIO_NR_PALMTE2_KEY_UP		22

/** HERE ARE INIT VALUES **/

/* BACKLIGHT */
#define PALMTE2_MAX_INTENSITY		0xFE
#define PALMTE2_DEFAULT_INTENSITY	0x7E
#define PALMTE2_LIMIT_MASK		0x7F
#define PALMTE2_PRESCALER		0x3F
#define PALMTE2_PERIOD_NS		3500

/* BATTERY */
#define PALMTE2_BAT_MAX_VOLTAGE		4000	/* 4.00v current voltage */
#define PALMTE2_BAT_MIN_VOLTAGE		3550	/* 3.55v critical voltage */
#define PALMTE2_BAT_MAX_CURRENT		0	/* unknown */
#define PALMTE2_BAT_MIN_CURRENT		0	/* unknown */
#define PALMTE2_BAT_MAX_CHARGE		1	/* unknown */
#define PALMTE2_BAT_MIN_CHARGE		1	/* unknown */
#define PALMTE2_MAX_LIFE_MINS		360	/* on-life in minutes */

#endif
