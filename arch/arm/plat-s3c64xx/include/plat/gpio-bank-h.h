/* linux/arch/arm/plat-s3c64xx/include/plat/gpio-bank-h.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 * 	Ben Dooks <ben@simtec.co.uk>
 * 	http://armlinux.simtec.co.uk/
 *
 * GPIO Bank H register and configuration definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define S3C64XX_GPHCON0			(S3C64XX_GPH_BASE + 0x00)
#define S3C64XX_GPHCON1			(S3C64XX_GPH_BASE + 0x04)
#define S3C64XX_GPHDAT			(S3C64XX_GPH_BASE + 0x08)
#define S3C64XX_GPHPUD			(S3C64XX_GPH_BASE + 0x0c)
#define S3C64XX_GPHCONSLP		(S3C64XX_GPH_BASE + 0x10)
#define S3C64XX_GPHPUDSLP		(S3C64XX_GPH_BASE + 0x14)

#define S3C64XX_GPH_CONMASK(__gpio)	(0xf << ((__gpio) * 4))
#define S3C64XX_GPH_INPUT(__gpio)	(0x0 << ((__gpio) * 4))
#define S3C64XX_GPH_OUTPUT(__gpio)	(0x1 << ((__gpio) * 4))

#define S3C64XX_GPH0_MMC1_CLK		(0x02 << 0)
#define S3C64XX_GPH0_KP_COL0		(0x04 << 0)
#define S3C64XX_GPH0_EINT_G6_0		(0x07 << 0)

#define S3C64XX_GPH1_MMC1_CMD		(0x02 << 4)
#define S3C64XX_GPH1_KP_COL1		(0x04 << 4)
#define S3C64XX_GPH1_EINT_G6_1		(0x07 << 4)

#define S3C64XX_GPH2_MMC1_DATA0		(0x02 << 8)
#define S3C64XX_GPH2_KP_COL2		(0x04 << 8)
#define S3C64XX_GPH2_EINT_G6_2		(0x07 << 8)

#define S3C64XX_GPH3_MMC1_DATA1		(0x02 << 12)
#define S3C64XX_GPH3_KP_COL3		(0x04 << 12)
#define S3C64XX_GPH3_EINT_G6_3		(0x07 << 12)

#define S3C64XX_GPH4_MMC1_DATA2		(0x02 << 16)
#define S3C64XX_GPH4_KP_COL4		(0x04 << 16)
#define S3C64XX_GPH4_EINT_G6_4		(0x07 << 16)

#define S3C64XX_GPH5_MMC1_DATA3		(0x02 << 20)
#define S3C64XX_GPH5_KP_COL5		(0x04 << 20)
#define S3C64XX_GPH5_EINT_G6_5		(0x07 << 20)

#define S3C64XX_GPH6_MMC1_DATA4		(0x02 << 24)
#define S3C64XX_GPH6_MMC2_DATA0		(0x03 << 24)
#define S3C64XX_GPH6_KP_COL6		(0x04 << 24)
#define S3C64XX_GPH6_I2S_V40_BCLK	(0x05 << 24)
#define S3C64XX_GPH6_ADDR_CF0		(0x06 << 24)
#define S3C64XX_GPH6_EINT_G6_6		(0x07 << 24)

#define S3C64XX_GPH7_MMC1_DATA5		(0x02 << 28)
#define S3C64XX_GPH7_MMC2_DATA1		(0x03 << 28)
#define S3C64XX_GPH7_KP_COL7		(0x04 << 28)
#define S3C64XX_GPH7_I2S_V40_CDCLK	(0x05 << 28)
#define S3C64XX_GPH7_ADDR_CF1		(0x06 << 28)
#define S3C64XX_GPH7_EINT_G6_7		(0x07 << 28)

#define S3C64XX_GPH8_MMC1_DATA6		(0x02 << 32)
#define S3C64XX_GPH8_MMC2_DATA2		(0x03 << 32)
#define S3C64XX_GPH8_I2S_V40_LRCLK	(0x05 << 32)
#define S3C64XX_GPH8_ADDR_CF2		(0x06 << 32)
#define S3C64XX_GPH8_EINT_G6_8		(0x07 << 32)

#define S3C64XX_GPH9_MMC1_DATA7		(0x02 << 36)
#define S3C64XX_GPH9_MMC2_DATA3		(0x03 << 36)
#define S3C64XX_GPH9_I2S_V40_DI		(0x05 << 36)
#define S3C64XX_GPH9_EINT_G6_9		(0x07 << 36)

