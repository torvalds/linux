/*
 *  arch/arm/mach-pxa/include/mach/z2.h
 *
 *  Author: Ken McGuire
 *  Created: Feb 6, 2009
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ASM_ARCH_ZIPIT2_H
#define ASM_ARCH_ZIPIT2_H

/* LEDs */
#define	GPIO10_ZIPITZ2_LED_WIFI		10
#define	GPIO85_ZIPITZ2_LED_CHARGED	85
#define	GPIO83_ZIPITZ2_LED_CHARGING	83

/* SD/MMC */
#define	GPIO96_ZIPITZ2_SD_DETECT	96

/* GPIO Buttons */
#define	GPIO1_ZIPITZ2_POWER_BUTTON	1
#define	GPIO98_ZIPITZ2_LID_BUTTON	98

/* Libertas GSPI8686 WiFi */
#define	GPIO14_ZIPITZ2_WIFI_POWER	14
#define	GPIO24_ZIPITZ2_WIFI_CS		24
#define	GPIO36_ZIPITZ2_WIFI_IRQ		36

/* LCD */
#define	GPIO19_ZIPITZ2_LCD_RESET	19
#define	GPIO88_ZIPITZ2_LCD_CS		88

/* MISC GPIOs */
#define	GPIO0_ZIPITZ2_AC_DETECT		0
#define GPIO37_ZIPITZ2_HEADSET_DETECT	37

#endif
