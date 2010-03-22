/* linux/arch/arm/mach-s3c64xx/include/mach/gpio-bank-d.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 * 	Ben Dooks <ben@simtec.co.uk>
 * 	http://armlinux.simtec.co.uk/
 *
 * GPIO Bank D register and configuration definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define S3C64XX_GPDCON			(S3C64XX_GPD_BASE + 0x00)
#define S3C64XX_GPDDAT			(S3C64XX_GPD_BASE + 0x04)
#define S3C64XX_GPDPUD			(S3C64XX_GPD_BASE + 0x08)
#define S3C64XX_GPDCONSLP		(S3C64XX_GPD_BASE + 0x0c)
#define S3C64XX_GPDPUDSLP		(S3C64XX_GPD_BASE + 0x10)

#define S3C64XX_GPD_CONMASK(__gpio)	(0xf << ((__gpio) * 4))
#define S3C64XX_GPD_INPUT(__gpio)	(0x0 << ((__gpio) * 4))
#define S3C64XX_GPD_OUTPUT(__gpio)	(0x1 << ((__gpio) * 4))

#define S3C64XX_GPD0_PCM0_SCLK		(0x02 << 0)
#define S3C64XX_GPD0_I2S0_CLK		(0x03 << 0)
#define S3C64XX_GPD0_AC97_BITCLK	(0x04 << 0)
#define S3C64XX_GPD0_EINT_G3_0		(0x07 << 0)

#define S3C64XX_GPD1_PCM0_EXTCLK	(0x02 << 4)
#define S3C64XX_GPD1_I2S0_CDCLK		(0x03 << 4)
#define S3C64XX_GPD1_AC97_nRESET	(0x04 << 4)
#define S3C64XX_GPD1_EINT_G3_1		(0x07 << 4)

#define S3C64XX_GPD2_PCM0_FSYNC		(0x02 << 8)
#define S3C64XX_GPD2_I2S0_LRCLK		(0x03 << 8)
#define S3C64XX_GPD2_AC97_SYNC		(0x04 << 8)
#define S3C64XX_GPD2_EINT_G3_2		(0x07 << 8)

#define S3C64XX_GPD3_PCM0_SIN		(0x02 << 12)
#define S3C64XX_GPD3_I2S0_DI		(0x03 << 12)
#define S3C64XX_GPD3_AC97_SDI		(0x04 << 12)
#define S3C64XX_GPD3_EINT_G3_3		(0x07 << 12)

#define S3C64XX_GPD4_PCM0_SOUT		(0x02 << 16)
#define S3C64XX_GPD4_I2S0_D0		(0x03 << 16)
#define S3C64XX_GPD4_AC97_SDO		(0x04 << 16)
#define S3C64XX_GPD4_EINT_G3_4		(0x07 << 16)

