/*
 * GPIOs and interrupts for Palm Treo smartphones
 *
 * currently supported:
 *     Palm Treo 680 (GSM)
 *     Palm Centro 685 (GSM)
 *
 * Author:     Tomas Cech <sleep_walker@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * find more info at www.hackndev.com
 *
 */

#ifndef _INCLUDE_TREO_H_
#define _INCLUDE_TREO_H_

/* GPIOs */
#define GPIO_NR_TREO_POWER_DETECT	0
#define GPIO_NR_TREO_AMP_EN		27
#define GPIO_NR_TREO_GREEN_LED	20
#define GPIO_NR_TREO_RED_LED		79
#define GPIO_NR_TREO_SD_DETECT_N	113
#define GPIO_NR_TREO_EP_DETECT_N	116
#define GPIO_NR_TREO_USB_DETECT	1
#define GPIO_NR_TREO_USB_PULLUP	114
#define GPIO_NR_TREO_GSM_POWER	40
#define GPIO_NR_TREO_GSM_RESET	87
#define GPIO_NR_TREO_GSM_WAKE	57
#define GPIO_NR_TREO_GSM_HOST_WAKE	14
#define GPIO_NR_TREO_GSM_TRIGGER	10
#define GPIO_NR_TREO_IR_EN		115
#define GPIO_NR_TREO_IR_TXD		47
#define GPIO_NR_TREO_BL_POWER	38
#define GPIO_NR_TREO_LCD_POWER	25

/* Treo680 specific GPIOs */
#define GPIO_NR_TREO680_SD_READONLY	33
#define GPIO_NR_TREO680_SD_POWER	42
#define GPIO_NR_TREO680_VIBRATE_EN	44
#define GPIO_NR_TREO680_KEYB_BL		24
#define GPIO_NR_TREO680_BT_EN		43
#define GPIO_NR_TREO680_LCD_POWER	77
#define GPIO_NR_TREO680_LCD_EN		86
#define GPIO_NR_TREO680_LCD_EN_N	25

/* Centro685 specific GPIOs */
#define GPIO_NR_CENTRO_SD_POWER		21
#define GPIO_NR_CENTRO_VIBRATE_EN	22
#define GPIO_NR_CENTRO_KEYB_BL		33
#define GPIO_NR_CENTRO_BT_EN		80

/* Various addresses  */
#define TREO_PHYS_RAM_START	0xa0000000
#define TREO_PHYS_IO_START	0x40000000
#define TREO_STR_BASE	0xa2000000

/* BACKLIGHT */
#define TREO_MAX_INTENSITY		254
#define TREO_DEFAULT_INTENSITY	160
#define TREO_LIMIT_MASK		0x7F
#define TREO_PRESCALER		63
#define TREO_PERIOD_NS		3500

#endif
