/* linux/arch/arm/plat-s3c64xx/include/plat/gpio-bank-n.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 * 	Ben Dooks <ben@simtec.co.uk>
 * 	http://armlinux.simtec.co.uk/
 *
 * GPIO Bank N register and configuration definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define S3C64XX_GPNCON			(S3C64XX_GPN_BASE + 0x00)
#define S3C64XX_GPNDAT			(S3C64XX_GPN_BASE + 0x04)
#define S3C64XX_GPNPUD			(S3C64XX_GPN_BASE + 0x08)

#define S3C64XX_GPN_CONMASK(__gpio)	(0x3 << ((__gpio) * 2))
#define S3C64XX_GPN_INPUT(__gpio)	(0x0 << ((__gpio) * 2))
#define S3C64XX_GPN_OUTPUT(__gpio)	(0x1 << ((__gpio) * 2))

#define S3C64XX_GPN0_EINT0		(0x02 << 0)
#define S3C64XX_GPN0_KP_ROW0		(0x03 << 0)

#define S3C64XX_GPN1_EINT1		(0x02 << 2)
#define S3C64XX_GPN1_KP_ROW1		(0x03 << 2)

#define S3C64XX_GPN2_EINT2		(0x02 << 4)
#define S3C64XX_GPN2_KP_ROW2		(0x03 << 4)

#define S3C64XX_GPN3_EINT3		(0x02 << 6)
#define S3C64XX_GPN3_KP_ROW3		(0x03 << 6)

#define S3C64XX_GPN4_EINT4		(0x02 << 8)
#define S3C64XX_GPN4_KP_ROW4		(0x03 << 8)

#define S3C64XX_GPN5_EINT5		(0x02 << 10)
#define S3C64XX_GPN5_KP_ROW5		(0x03 << 10)

#define S3C64XX_GPN6_EINT6		(0x02 << 12)
#define S3C64XX_GPN6_KP_ROW6		(0x03 << 12)

#define S3C64XX_GPN7_EINT7		(0x02 << 14)
#define S3C64XX_GPN7_KP_ROW7		(0x03 << 14)

#define S3C64XX_GPN8_EINT8		(0x02 << 16)
#define S3C64XX_GPN9_EINT9		(0x02 << 18)
#define S3C64XX_GPN10_EINT10		(0x02 << 20)
#define S3C64XX_GPN11_EINT11		(0x02 << 22)
#define S3C64XX_GPN12_EINT12		(0x02 << 24)
#define S3C64XX_GPN13_EINT13		(0x02 << 26)
#define S3C64XX_GPN14_EINT14		(0x02 << 28)
#define S3C64XX_GPN15_EINT15		(0x02 << 30)
