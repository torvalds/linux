/*
 * GPIOs and interrupts for Palm Tungsten|T5 Handheld Computer
 *
 * Authors:	Ales Snuparek <snuparek@atlas.cz>
 *		Marek Vasut <marek.vasut@gmail.com>
 *		Justin Kendrick <twilightsentry@gmail.com>
 *		RichardT5 <richard_t5@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _INCLUDE_PALMT5_H_
#define _INCLUDE_PALMT5_H_

#include <mach/irqs.h> /* PXA_GPIO_TO_IRQ */

/** HERE ARE GPIOs **/

/* GPIOs */
#define GPIO_NR_PALMT5_GPIO_RESET		1

#define GPIO_NR_PALMT5_POWER_DETECT		90
#define GPIO_NR_PALMT5_HOTSYNC_BUTTON_N		10
#define GPIO_NR_PALMT5_EARPHONE_DETECT		107

/* SD/MMC */
#define GPIO_NR_PALMT5_SD_DETECT_N		14
#define GPIO_NR_PALMT5_SD_POWER			114
#define GPIO_NR_PALMT5_SD_READONLY		115

/* TOUCHSCREEN */
#define GPIO_NR_PALMT5_WM9712_IRQ		27

/* IRDA - disable GPIO connected to SD pin of tranceiver (TFBS4710?) ? */
#define GPIO_NR_PALMT5_IR_DISABLE		40

/* USB */
#define GPIO_NR_PALMT5_USB_DETECT_N		15
#define GPIO_NR_PALMT5_USB_PULLUP		93

/* LCD/BACKLIGHT */
#define GPIO_NR_PALMT5_BL_POWER			84
#define GPIO_NR_PALMT5_LCD_POWER		96

/* BLUETOOTH */
#define GPIO_NR_PALMT5_BT_POWER			17
#define GPIO_NR_PALMT5_BT_RESET			83

/* INTERRUPTS */
#define IRQ_GPIO_PALMT5_SD_DETECT_N	PXA_GPIO_TO_IRQ(GPIO_NR_PALMT5_SD_DETECT_N)
#define IRQ_GPIO_PALMT5_WM9712_IRQ	PXA_GPIO_TO_IRQ(GPIO_NR_PALMT5_WM9712_IRQ)
#define IRQ_GPIO_PALMT5_USB_DETECT	PXA_GPIO_TO_IRQ(GPIO_NR_PALMT5_USB_DETECT)
#define IRQ_GPIO_PALMT5_GPIO_RESET	PXA_GPIO_TO_IRQ(GPIO_NR_PALMT5_GPIO_RESET)

/** HERE ARE INIT VALUES **/

/* Various addresses  */
#define PALMT5_PHYS_RAM_START	0xa0000000
#define PALMT5_PHYS_IO_START	0x40000000
#define PALMT5_STR_BASE		0xa0200000

/* TOUCHSCREEN */
#define AC97_LINK_FRAME		21

/* BATTERY */
#define PALMT5_BAT_MAX_VOLTAGE		4000	/* 4.00v current voltage */
#define PALMT5_BAT_MIN_VOLTAGE		3550	/* 3.55v critical voltage */
#define PALMT5_BAT_MAX_CURRENT		0	/* unknown */
#define PALMT5_BAT_MIN_CURRENT		0	/* unknown */
#define PALMT5_BAT_MAX_CHARGE		1	/* unknown */
#define PALMT5_BAT_MIN_CHARGE		1	/* unknown */
#define PALMT5_MAX_LIFE_MINS		360    /* on-life in minutes */

#define PALMT5_BAT_MEASURE_DELAY	(HZ * 1)

/* BACKLIGHT */
#define PALMT5_MAX_INTENSITY		0xFE
#define PALMT5_DEFAULT_INTENSITY	0x7E
#define PALMT5_LIMIT_MASK		0x7F
#define PALMT5_PRESCALER		0x3F
#define PALMT5_PERIOD_NS		3500

#endif
