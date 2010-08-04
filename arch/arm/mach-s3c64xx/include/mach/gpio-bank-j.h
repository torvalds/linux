/* linux/arch/arm/mach-s3c64xx/include/mach/gpio-bank-j.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 * 	Ben Dooks <ben@simtec.co.uk>
 * 	http://armlinux.simtec.co.uk/
 *
 * GPIO Bank J register and configuration definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define S3C64XX_GPJCON			(S3C64XX_GPJ_BASE + 0x00)
#define S3C64XX_GPJDAT			(S3C64XX_GPJ_BASE + 0x04)
#define S3C64XX_GPJPUD			(S3C64XX_GPJ_BASE + 0x08)
#define S3C64XX_GPJCONSLP		(S3C64XX_GPJ_BASE + 0x0c)
#define S3C64XX_GPJPUDSLP		(S3C64XX_GPJ_BASE + 0x10)

#define S3C64XX_GPJ_CONMASK(__gpio)	(0x3 << ((__gpio) * 2))
#define S3C64XX_GPJ_INPUT(__gpio)	(0x0 << ((__gpio) * 2))
#define S3C64XX_GPJ_OUTPUT(__gpio)	(0x1 << ((__gpio) * 2))

#define S3C64XX_GPJ0_VD16		(0x02 << 0)
#define S3C64XX_GPJ1_VD17		(0x02 << 2)
#define S3C64XX_GPJ2_VD18		(0x02 << 4)
#define S3C64XX_GPJ3_VD19		(0x02 << 6)
#define S3C64XX_GPJ4_VD20		(0x02 << 8)
#define S3C64XX_GPJ5_VD21		(0x02 << 10)
#define S3C64XX_GPJ6_VD22		(0x02 << 12)
#define S3C64XX_GPJ7_VD23		(0x02 << 14)
#define S3C64XX_GPJ8_LCD_HSYNC		(0x02 << 16)
#define S3C64XX_GPJ9_LCD_VSYNC		(0x02 << 18)
#define S3C64XX_GPJ10_LCD_VDEN		(0x02 << 20)
#define S3C64XX_GPJ11_LCD_VCLK		(0x02 << 22)
