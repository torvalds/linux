/* linux/arch/arm/mach-s3c64xx/include/mach/gpio-bank-o.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 * 	Ben Dooks <ben@simtec.co.uk>
 * 	http://armlinux.simtec.co.uk/
 *
 * GPIO Bank O register and configuration definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define S3C64XX_GPOCON			(S3C64XX_GPO_BASE + 0x00)
#define S3C64XX_GPODAT			(S3C64XX_GPO_BASE + 0x04)
#define S3C64XX_GPOPUD			(S3C64XX_GPO_BASE + 0x08)
#define S3C64XX_GPOCONSLP		(S3C64XX_GPO_BASE + 0x0c)
#define S3C64XX_GPOPUDSLP		(S3C64XX_GPO_BASE + 0x10)

#define S3C64XX_GPO_CONMASK(__gpio)	(0x3 << ((__gpio) * 2))
#define S3C64XX_GPO_INPUT(__gpio)	(0x0 << ((__gpio) * 2))
#define S3C64XX_GPO_OUTPUT(__gpio)	(0x1 << ((__gpio) * 2))

#define S3C64XX_GPO0_MEM0_nCS2		(0x02 << 0)
#define S3C64XX_GPO0_EINT_G7_0		(0x03 << 0)

#define S3C64XX_GPO1_MEM0_nCS3		(0x02 << 2)
#define S3C64XX_GPO1_EINT_G7_1		(0x03 << 2)

#define S3C64XX_GPO2_MEM0_nCS4		(0x02 << 4)
#define S3C64XX_GPO2_EINT_G7_2		(0x03 << 4)

#define S3C64XX_GPO3_MEM0_nCS5		(0x02 << 6)
#define S3C64XX_GPO3_EINT_G7_3		(0x03 << 6)

#define S3C64XX_GPO4_EINT_G7_4		(0x03 << 8)

#define S3C64XX_GPO5_EINT_G7_5		(0x03 << 10)

#define S3C64XX_GPO6_MEM0_ADDR6		(0x02 << 12)
#define S3C64XX_GPO6_EINT_G7_6		(0x03 << 12)

#define S3C64XX_GPO7_MEM0_ADDR7		(0x02 << 14)
#define S3C64XX_GPO7_EINT_G7_7		(0x03 << 14)

#define S3C64XX_GPO8_MEM0_ADDR8		(0x02 << 16)
#define S3C64XX_GPO8_EINT_G7_8		(0x03 << 16)

#define S3C64XX_GPO9_MEM0_ADDR9		(0x02 << 18)
#define S3C64XX_GPO9_EINT_G7_9		(0x03 << 18)

#define S3C64XX_GPO10_MEM0_ADDR10	(0x02 << 20)
#define S3C64XX_GPO10_EINT_G7_10	(0x03 << 20)

#define S3C64XX_GPO11_MEM0_ADDR11	(0x02 << 22)
#define S3C64XX_GPO11_EINT_G7_11	(0x03 << 22)

#define S3C64XX_GPO12_MEM0_ADDR12	(0x02 << 24)
#define S3C64XX_GPO12_EINT_G7_12	(0x03 << 24)

#define S3C64XX_GPO13_MEM0_ADDR13	(0x02 << 26)
#define S3C64XX_GPO13_EINT_G7_13	(0x03 << 26)

#define S3C64XX_GPO14_MEM0_ADDR14	(0x02 << 28)
#define S3C64XX_GPO14_EINT_G7_14	(0x03 << 28)

#define S3C64XX_GPO15_MEM0_ADDR15	(0x02 << 30)
#define S3C64XX_GPO15_EINT_G7_15	(0x03 << 30)

