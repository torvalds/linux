/*
 * Copyright 2006 Ben Dooks <ben-linux@fluff.org>
 *
 * Copyright (c) 2005 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * iPAQ H1940 series definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __MACH_S3C24XX_H1940_H
#define __MACH_S3C24XX_H1940_H __FILE__

#define H1940_SUSPEND_CHECKSUM		(0x30003ff8)
#define H1940_SUSPEND_RESUMEAT		(0x30081000)
#define H1940_SUSPEND_CHECK		(0x30080000)

extern void h1940_pm_return(void);
extern int h1940_led_blink_set(unsigned gpio, int state,
			       unsigned long *delay_on,
			       unsigned long *delay_off);

#include <linux/gpio.h>

#define H1940_LATCH_GPIO(x)		(S3C_GPIO_END + (x))

/* SD layer latch */

#define H1940_LATCH_LCD_P0		H1940_LATCH_GPIO(0)
#define H1940_LATCH_LCD_P1		H1940_LATCH_GPIO(1)
#define H1940_LATCH_LCD_P2		H1940_LATCH_GPIO(2)
#define H1940_LATCH_LCD_P3		H1940_LATCH_GPIO(3)
#define H1940_LATCH_MAX1698_nSHUTDOWN	H1940_LATCH_GPIO(4)
#define H1940_LATCH_LED_RED		H1940_LATCH_GPIO(5)
#define H1940_LATCH_SDQ7		H1940_LATCH_GPIO(6)
#define H1940_LATCH_USB_DP		H1940_LATCH_GPIO(7)

/* CPU layer latch */

#define H1940_LATCH_UDA_POWER		H1940_LATCH_GPIO(8)
#define H1940_LATCH_AUDIO_POWER		H1940_LATCH_GPIO(9)
#define H1940_LATCH_SM803_ENABLE	H1940_LATCH_GPIO(10)
#define H1940_LATCH_LCD_P4		H1940_LATCH_GPIO(11)
#define H1940_LATCH_SD_POWER		H1940_LATCH_GPIO(12)
#define H1940_LATCH_BLUETOOTH_POWER	H1940_LATCH_GPIO(13)
#define H1940_LATCH_LED_GREEN		H1940_LATCH_GPIO(14)
#define H1940_LATCH_LED_FLASH		H1940_LATCH_GPIO(15)

#endif /* __MACH_S3C24XX_H1940_H */
