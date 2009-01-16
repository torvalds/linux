/* linux/arch/arm/plat-s3c64xx/include/plat/gpio-bank-p.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 * 	Ben Dooks <ben@simtec.co.uk>
 * 	http://armlinux.simtec.co.uk/
 *
 * GPIO Bank P register and configuration definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define S3C64XX_GPPCON			(S3C64XX_GPP_BASE + 0x00)
#define S3C64XX_GPPDAT			(S3C64XX_GPP_BASE + 0x04)
#define S3C64XX_GPPPUD			(S3C64XX_GPP_BASE + 0x08)
#define S3C64XX_GPPCONSLP		(S3C64XX_GPP_BASE + 0x0c)
#define S3C64XX_GPPPUDSLP		(S3C64XX_GPP_BASE + 0x10)

#define S3C64XX_GPP_CONMASK(__gpio)	(0x3 << ((__gpio) * 2))
#define S3C64XX_GPP_INPUT(__gpio)	(0x0 << ((__gpio) * 2))
#define S3C64XX_GPP_OUTPUT(__gpio)	(0x1 << ((__gpio) * 2))

#define S3C64XX_GPP0_MEM0_ADDRV		(0x02 << 0)
#define S3C64XX_GPP0_EINT_G8_0		(0x03 << 0)

#define S3C64XX_GPP1_MEM0_SMCLK		(0x02 << 2)
#define S3C64XX_GPP1_EINT_G8_1		(0x03 << 2)

#define S3C64XX_GPP2_MEM0_nWAIT		(0x02 << 4)
#define S3C64XX_GPP2_EINT_G8_2		(0x03 << 4)

#define S3C64XX_GPP3_MEM0_RDY0_ALE	(0x02 << 6)
#define S3C64XX_GPP3_EINT_G8_3		(0x03 << 6)

#define S3C64XX_GPP4_MEM0_RDY1_CLE	(0x02 << 8)
#define S3C64XX_GPP4_EINT_G8_4		(0x03 << 8)

#define S3C64XX_GPP5_MEM0_INTsm0_FWE	(0x02 << 10)
#define S3C64XX_GPP5_EINT_G8_5		(0x03 << 10)

#define S3C64XX_GPP6_MEM0_(null)	(0x02 << 12)
#define S3C64XX_GPP6_EINT_G8_6		(0x03 << 12)

#define S3C64XX_GPP7_MEM0_INTsm1_FRE	(0x02 << 14)
#define S3C64XX_GPP7_EINT_G8_7		(0x03 << 14)

#define S3C64XX_GPP8_MEM0_RPn_RnB	(0x02 << 16)
#define S3C64XX_GPP8_EINT_G8_8		(0x03 << 16)

#define S3C64XX_GPP9_MEM0_ATA_RESET	(0x02 << 18)
#define S3C64XX_GPP9_EINT_G8_9		(0x03 << 18)

#define S3C64XX_GPP10_MEM0_ATA_INPACK	(0x02 << 20)
#define S3C64XX_GPP10_EINT_G8_10	(0x03 << 20)

#define S3C64XX_GPP11_MEM0_ATA_REG	(0x02 << 22)
#define S3C64XX_GPP11_EINT_G8_11	(0x03 << 22)

#define S3C64XX_GPP12_MEM0_ATA_WE	(0x02 << 24)
#define S3C64XX_GPP12_EINT_G8_12	(0x03 << 24)

#define S3C64XX_GPP13_MEM0_ATA_OE	(0x02 << 26)
#define S3C64XX_GPP13_EINT_G8_13	(0x03 << 26)

#define S3C64XX_GPP14_MEM0_ATA_CD	(0x02 << 28)
#define S3C64XX_GPP14_EINT_G8_14	(0x03 << 28)

