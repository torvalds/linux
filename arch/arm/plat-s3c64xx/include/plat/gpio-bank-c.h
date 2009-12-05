/* linux/arch/arm/plat-s3c64xx/include/plat/gpio-bank-c.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 * 	Ben Dooks <ben@simtec.co.uk>
 * 	http://armlinux.simtec.co.uk/
 *
 * GPIO Bank C register and configuration definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define S3C64XX_GPCCON			(S3C64XX_GPC_BASE + 0x00)
#define S3C64XX_GPCDAT			(S3C64XX_GPC_BASE + 0x04)
#define S3C64XX_GPCPUD			(S3C64XX_GPC_BASE + 0x08)
#define S3C64XX_GPCCONSLP		(S3C64XX_GPC_BASE + 0x0c)
#define S3C64XX_GPCPUDSLP		(S3C64XX_GPC_BASE + 0x10)

#define S3C64XX_GPC_CONMASK(__gpio)	(0xf << ((__gpio) * 4))
#define S3C64XX_GPC_INPUT(__gpio)	(0x0 << ((__gpio) * 4))
#define S3C64XX_GPC_OUTPUT(__gpio)	(0x1 << ((__gpio) * 4))

#define S3C64XX_GPC0_SPI_MISO0		(0x02 << 0)
#define S3C64XX_GPC0_EINT_G2_0		(0x07 << 0)

#define S3C64XX_GPC1_SPI_CLKO		(0x02 << 4)
#define S3C64XX_GPC1_EINT_G2_1		(0x07 << 4)

#define S3C64XX_GPC2_SPI_MOSIO		(0x02 << 8)
#define S3C64XX_GPC2_EINT_G2_2		(0x07 << 8)

#define S3C64XX_GPC3_SPI_nCSO		(0x02 << 12)
#define S3C64XX_GPC3_EINT_G2_3		(0x07 << 12)

#define S3C64XX_GPC4_SPI_MISO1		(0x02 << 16)
#define S3C64XX_GPC4_MMC2_CMD		(0x03 << 16)
#define S3C64XX_GPC4_I2S_V40_DO0	(0x05 << 16)
#define S3C64XX_GPC4_EINT_G2_4		(0x07 << 16)

#define S3C64XX_GPC5_SPI_CLK1		(0x02 << 20)
#define S3C64XX_GPC5_MMC2_CLK		(0x03 << 20)
#define S3C64XX_GPC5_I2S_V40_DO1	(0x05 << 20)
#define S3C64XX_GPC5_EINT_G2_5		(0x07 << 20)

#define S3C64XX_GPC6_SPI_MOSI1		(0x02 << 24)
#define S3C64XX_GPC6_EINT_G2_6		(0x07 << 24)

#define S3C64XX_GPC7_SPI_nCS1		(0x02 << 28)
#define S3C64XX_GPC7_I2S_V40_DO2	(0x05 << 28)
#define S3C64XX_GPC7_EINT_G2_7		(0x07 << 28)

