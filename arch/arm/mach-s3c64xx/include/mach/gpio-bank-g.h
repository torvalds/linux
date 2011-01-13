/* linux/arch/arm/mach-s3c64xx/include/mach/gpio-bank-g.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 * 	Ben Dooks <ben@simtec.co.uk>
 * 	http://armlinux.simtec.co.uk/
 *
 * GPIO Bank G register and configuration definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define S3C64XX_GPGCON			(S3C64XX_GPG_BASE + 0x00)
#define S3C64XX_GPGDAT			(S3C64XX_GPG_BASE + 0x04)
#define S3C64XX_GPGPUD			(S3C64XX_GPG_BASE + 0x08)
#define S3C64XX_GPGCONSLP		(S3C64XX_GPG_BASE + 0x0c)
#define S3C64XX_GPGPUDSLP		(S3C64XX_GPG_BASE + 0x10)

#define S3C64XX_GPG_CONMASK(__gpio)	(0xf << ((__gpio) * 4))
#define S3C64XX_GPG_INPUT(__gpio)	(0x0 << ((__gpio) * 4))
#define S3C64XX_GPG_OUTPUT(__gpio)	(0x1 << ((__gpio) * 4))

#define S3C64XX_GPG0_MMC0_CLK		(0x02 << 0)
#define S3C64XX_GPG0_EINT_G5_0		(0x07 << 0)

#define S3C64XX_GPG1_MMC0_CMD		(0x02 << 4)
#define S3C64XX_GPG1_EINT_G5_1		(0x07 << 4)

#define S3C64XX_GPG2_MMC0_DATA0		(0x02 << 8)
#define S3C64XX_GPG2_EINT_G5_2		(0x07 << 8)

#define S3C64XX_GPG3_MMC0_DATA1		(0x02 << 12)
#define S3C64XX_GPG3_EINT_G5_3		(0x07 << 12)

#define S3C64XX_GPG4_MMC0_DATA2		(0x02 << 16)
#define S3C64XX_GPG4_EINT_G5_4		(0x07 << 16)

#define S3C64XX_GPG5_MMC0_DATA3		(0x02 << 20)
#define S3C64XX_GPG5_EINT_G5_5		(0x07 << 20)

