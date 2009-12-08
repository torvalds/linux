/*
 * linux/include/asm-arm/arch-pxa/palmtc-gpio.h
 *
 * GPIOs and interrupts for Palm Tungsten|C Handheld Computer
 *
 * Authors:	Alex Osborne <bobofdoom@gmail.com>
 *		Marek Vasut <marek.vasut@gmail.com>
 *		Holger Bocklet <bitz.email@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _INCLUDE_PALMTC_H_
#define _INCLUDE_PALMTC_H_

/** HERE ARE GPIOs **/

/* GPIOs */
#define GPIO_NR_PALMTC_EARPHONE_DETECT	2
#define GPIO_NR_PALMTC_CRADLE_DETECT	5
#define GPIO_NR_PALMTC_HOTSYNC_BUTTON	7

/* SD/MMC */
#define GPIO_NR_PALMTC_SD_DETECT_N	12
#define GPIO_NR_PALMTC_SD_POWER		32
#define GPIO_NR_PALMTC_SD_READONLY	54

/* WLAN */
#define GPIO_NR_PALMTC_PCMCIA_READY	13
#define GPIO_NR_PALMTC_PCMCIA_PWRREADY	14
#define GPIO_NR_PALMTC_PCMCIA_POWER1	15
#define GPIO_NR_PALMTC_PCMCIA_POWER2	33
#define GPIO_NR_PALMTC_PCMCIA_POWER3	55
#define GPIO_NR_PALMTC_PCMCIA_RESET	78

/* UDC */
#define GPIO_NR_PALMTC_USB_DETECT_N	4
#define GPIO_NR_PALMTC_USB_POWER	36

/* LCD/BACKLIGHT */
#define GPIO_NR_PALMTC_BL_POWER		16
#define GPIO_NR_PALMTC_LCD_POWER	44
#define GPIO_NR_PALMTC_LCD_BLANK	38

/* UART */
#define GPIO_NR_PALMTC_RS232_POWER	37

/* IRDA */
#define GPIO_NR_PALMTC_IR_DISABLE	45

/* IRQs */
#define IRQ_GPIO_PALMTC_SD_DETECT_N	IRQ_GPIO(GPIO_NR_PALMTC_SD_DETECT_N)
#define IRQ_GPIO_PALMTC_WLAN_READY	IRQ_GPIO(GPIO_NR_PALMTC_WLAN_READY)

/* UCB1400 GPIOs */
#define GPIO_NR_PALMTC_POWER_DETECT	(0x80 | 0x00)
#define GPIO_NR_PALMTC_HEADPHONE_DETECT	(0x80 | 0x01)
#define GPIO_NR_PALMTC_SPEAKER_ENABLE	(0x80 | 0x03)
#define GPIO_NR_PALMTC_VIBRA_POWER	(0x80 | 0x05)
#define GPIO_NR_PALMTC_LED_POWER	(0x80 | 0x07)

/** HERE ARE INIT VALUES **/
#define PALMTC_UCB1400_GPIO_OFFSET	0x80

/* BATTERY */
#define PALMTC_BAT_MAX_VOLTAGE		4000	/* 4.00V maximum voltage */
#define PALMTC_BAT_MIN_VOLTAGE		3550	/* 3.55V critical voltage */
#define PALMTC_BAT_MAX_CURRENT		0	/* unknokn */
#define PALMTC_BAT_MIN_CURRENT		0	/* unknown */
#define PALMTC_BAT_MAX_CHARGE		1	/* unknown */
#define PALMTC_BAT_MIN_CHARGE		1	/* unknown */
#define PALMTC_MAX_LIFE_MINS		240	/* on-life in minutes */

#define PALMTC_BAT_MEASURE_DELAY	(HZ * 1)

/* BACKLIGHT */
#define PALMTC_MAX_INTENSITY		0xFE
#define PALMTC_DEFAULT_INTENSITY	0x7E
#define PALMTC_LIMIT_MASK		0x7F
#define PALMTC_PRESCALER		0x3F
#define PALMTC_PERIOD_NS		3500

#endif
