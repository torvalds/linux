/*
 * linux/arch/arm/mach-kirkwood/mpp.h -- Multi Purpose Pins
 *
 * Copyright 2009: Marvell Technology Group Ltd.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __KIRKWOOD_MPP_H
#define __KIRKWOOD_MPP_H

#define MPP(_num, _sel, _in, _out, _F6180, _F6190, _F6192, _F6281) ( \
	/* MPP number */		((_num) & 0xff) | \
	/* MPP select value */		(((_sel) & 0xf) << 8) | \
	/* may be input signal */	((!!(_in)) << 12) | \
	/* may be output signal */	((!!(_out)) << 13) | \
	/* available on F6180 */	((!!(_F6180)) << 14) | \
	/* available on F6190 */	((!!(_F6190)) << 15) | \
	/* available on F6192 */	((!!(_F6192)) << 16) | \
	/* available on F6281 */	((!!(_F6281)) << 17))

#define MPP_NUM(x)	((x) & 0xff)
#define MPP_SEL(x)	(((x) >> 8) & 0xf)

				/*   num sel  i  o  6180 6190 6192 6281 */

#define MPP_INPUT_MASK		MPP(  0, 0x0, 1, 0, 0,   0,   0,   0    )
#define MPP_OUTPUT_MASK		MPP(  0, 0x0, 0, 1, 0,   0,   0,   0    )

#define MPP_F6180_MASK		MPP(  0, 0x0, 0, 0, 1,   0,   0,   0    )
#define MPP_F6190_MASK		MPP(  0, 0x0, 0, 0, 0,   1,   0,   0    )
#define MPP_F6192_MASK		MPP(  0, 0x0, 0, 0, 0,   0,   1,   0    )
#define MPP_F6281_MASK		MPP(  0, 0x0, 0, 0, 0,   0,   0,   1    )

#define MPP0_GPIO		MPP(  0, 0x0, 1, 1, 1,   1,   1,   1    )
#define MPP0_NF_IO2		MPP(  0, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP0_SPI_SCn		MPP(  0, 0x2, 0, 1, 1,   1,   1,   1    )

#define MPP1_GPO		MPP(  1, 0x0, 0, 1, 1,   1,   1,   1    )
#define MPP1_NF_IO3		MPP(  1, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP1_SPI_MOSI		MPP(  1, 0x2, 0, 1, 1,   1,   1,   1    )

#define MPP2_GPO		MPP(  2, 0x0, 0, 1, 1,   1,   1,   1    )
#define MPP2_NF_IO4		MPP(  2, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP2_SPI_SCK		MPP(  2, 0x2, 0, 1, 1,   1,   1,   1    )

#define MPP3_GPO		MPP(  3, 0x0, 0, 1, 1,   1,   1,   1    )
#define MPP3_NF_IO5		MPP(  3, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP3_SPI_MISO		MPP(  3, 0x2, 1, 0, 1,   1,   1,   1    )

#define MPP4_GPIO		MPP(  4, 0x0, 1, 1, 1,   1,   1,   1    )
#define MPP4_NF_IO6		MPP(  4, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP4_UART0_RXD		MPP(  4, 0x2, 1, 0, 1,   1,   1,   1    )
#define MPP4_SATA1_ACTn		MPP(  4, 0x5, 0, 1, 0,   0,   1,   1    )
#define MPP4_PTP_CLK		MPP(  4, 0xd, 1, 0, 1,   1,   1,   1    )

#define MPP5_GPO		MPP(  5, 0x0, 0, 1, 1,   1,   1,   1    )
#define MPP5_NF_IO7		MPP(  5, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP5_UART0_TXD		MPP(  5, 0x2, 0, 1, 1,   1,   1,   1    )
#define MPP5_PTP_TRIG_GEN	MPP(  5, 0x4, 0, 1, 1,   1,   1,   1    )
#define MPP5_SATA0_ACTn		MPP(  5, 0x5, 0, 1, 0,   1,   1,   1    )

#define MPP6_SYSRST_OUTn	MPP(  6, 0x1, 0, 1, 1,   1,   1,   1    )
#define MPP6_SPI_MOSI		MPP(  6, 0x2, 0, 1, 1,   1,   1,   1    )
#define MPP6_PTP_TRIG_GEN	MPP(  6, 0x3, 0, 1, 1,   1,   1,   1    )

#define MPP7_GPO		MPP(  7, 0x0, 0, 1, 1,   1,   1,   1    )
#define MPP7_PEX_RST_OUTn	MPP(  7, 0x1, 0, 1, 1,   1,   1,   1    )
#define MPP7_SPI_SCn		MPP(  7, 0x2, 0, 1, 1,   1,   1,   1    )
#define MPP7_PTP_TRIG_GEN	MPP(  7, 0x3, 0, 1, 1,   1,   1,   1    )

#define MPP8_GPIO		MPP(  8, 0x0, 1, 1, 1,    1,  1,   1    )
#define MPP8_TW_SDA		MPP(  8, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP8_UART0_RTS		MPP(  8, 0x2, 0, 1, 1,   1,   1,   1    )
#define MPP8_UART1_RTS		MPP(  8, 0x3, 0, 1, 1,   1,   1,   1    )
#define MPP8_MII0_RXERR		MPP(  8, 0x4, 1, 0, 0,   1,   1,   1    )
#define MPP8_SATA1_PRESENTn	MPP(  8, 0x5, 0, 1, 0,   0,   1,   1    )
#define MPP8_PTP_CLK		MPP(  8, 0xc, 1, 0, 1,   1,   1,   1    )
#define MPP8_MII0_COL		MPP(  8, 0xd, 1, 0, 1,   1,   1,   1    )

#define MPP9_GPIO		MPP(  9, 0x0, 1, 1, 1,   1,   1,   1    )
#define MPP9_TW_SCK		MPP(  9, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP9_UART0_CTS		MPP(  9, 0x2, 1, 0, 1,   1,   1,   1    )
#define MPP9_UART1_CTS		MPP(  9, 0x3, 1, 0, 1,   1,   1,   1    )
#define MPP9_SATA0_PRESENTn	MPP(  9, 0x5, 0, 1, 0,   1,   1,   1    )
#define MPP9_PTP_EVENT_REQ	MPP(  9, 0xc, 1, 0, 1,   1,   1,   1    )
#define MPP9_MII0_CRS		MPP(  9, 0xd, 1, 0, 1,   1,   1,   1    )

#define MPP10_GPO		MPP( 10, 0x0, 0, 1, 1,   1,   1,   1    )
#define MPP10_SPI_SCK		MPP( 10, 0x2, 0, 1, 1,   1,   1,   1    )
#define MPP10_UART0_TXD		MPP( 10, 0X3, 0, 1, 1,   1,   1,   1    )
#define MPP10_SATA1_ACTn	MPP( 10, 0x5, 0, 1, 0,   0,   1,   1    )
#define MPP10_PTP_TRIG_GEN	MPP( 10, 0xc, 0, 1, 1,   1,   1,   1    )

#define MPP11_GPIO		MPP( 11, 0x0, 1, 1, 1,   1,   1,   1    )
#define MPP11_SPI_MISO		MPP( 11, 0x2, 1, 0, 1,   1,   1,   1    )
#define MPP11_UART0_RXD		MPP( 11, 0x3, 1, 0, 1,   1,   1,   1    )
#define MPP11_PTP_EVENT_REQ	MPP( 11, 0x4, 1, 0, 1,   1,   1,   1    )
#define MPP11_PTP_TRIG_GEN	MPP( 11, 0xc, 0, 1, 1,   1,   1,   1    )
#define MPP11_PTP_CLK		MPP( 11, 0xd, 1, 0, 1,   1,   1,   1    )
#define MPP11_SATA0_ACTn	MPP( 11, 0x5, 0, 1, 0,   1,   1,   1    )

#define MPP12_GPO		MPP( 12, 0x0, 0, 1, 1,   1,   1,   1    )
#define MPP12_SD_CLK		MPP( 12, 0x1, 0, 1, 1,   1,   1,   1    )

#define MPP13_GPIO		MPP( 13, 0x0, 1, 1, 1,   1,   1,   1    )
#define MPP13_SD_CMD		MPP( 13, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP13_UART1_TXD		MPP( 13, 0x3, 0, 1, 1,   1,   1,   1    )

#define MPP14_GPIO		MPP( 14, 0x0, 1, 1, 1,   1,   1,   1    )
#define MPP14_SD_D0		MPP( 14, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP14_UART1_RXD		MPP( 14, 0x3, 1, 0, 1,   1,   1,   1    )
#define MPP14_SATA1_PRESENTn	MPP( 14, 0x4, 0, 1, 0,   0,   1,   1    )
#define MPP14_MII0_COL		MPP( 14, 0xd, 1, 0, 1,   1,   1,   1    )

#define MPP15_GPIO		MPP( 15, 0x0, 1, 1, 1,   1,   1,   1    )
#define MPP15_SD_D1		MPP( 15, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP15_UART0_RTS		MPP( 15, 0x2, 0, 1, 1,   1,   1,   1    )
#define MPP15_UART1_TXD		MPP( 15, 0x3, 0, 1, 1,   1,   1,   1    )
#define MPP15_SATA0_ACTn	MPP( 15, 0x4, 0, 1, 0,   1,   1,   1    )

#define MPP16_GPIO		MPP( 16, 0x0, 1, 1, 1,   1,   1,   1    )
#define MPP16_SD_D2		MPP( 16, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP16_UART0_CTS		MPP( 16, 0x2, 1, 0, 1,   1,   1,   1    )
#define MPP16_UART1_RXD		MPP( 16, 0x3, 1, 0, 1,   1,   1,   1    )
#define MPP16_SATA1_ACTn	MPP( 16, 0x4, 0, 1, 0,   0,   1,   1    )
#define MPP16_MII0_CRS		MPP( 16, 0xd, 1, 0, 1,   1,   1,   1    )

#define MPP17_GPIO		MPP( 17, 0x0, 1, 1, 1,   1,   1,   1    )
#define MPP17_SD_D3		MPP( 17, 0x1, 1, 1, 1,   1,   1,   1    )
#define MPP17_SATA0_PRESENTn	MPP( 17, 0x4, 0, 1, 0,   1,   1,   1    )

#define MPP18_GPO		MPP( 18, 0x0, 0, 1, 1,   1,   1,   1    )
#define MPP18_NF_IO0		MPP( 18, 0x1, 1, 1, 1,   1,   1,   1    )

#define MPP19_GPO		MPP( 19, 0x0, 0, 1, 1,   1,   1,   1    )
#define MPP19_NF_IO1		MPP( 19, 0x1, 1, 1, 1,   1,   1,   1    )

#define MPP20_GPIO		MPP( 20, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP20_TSMP0		MPP( 20, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP20_TDM_CH0_TX_QL	MPP( 20, 0x2, 0, 1, 0,   0,   1,   1    )
#define MPP20_GE1_0		MPP( 20, 0x3, 0, 0, 0,   1,   1,   1    )
#define MPP20_AUDIO_SPDIFI	MPP( 20, 0x4, 1, 0, 0,   0,   1,   1    )
#define MPP20_SATA1_ACTn	MPP( 20, 0x5, 0, 1, 0,   0,   1,   1    )

#define MPP21_GPIO		MPP( 21, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP21_TSMP1		MPP( 21, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP21_TDM_CH0_RX_QL	MPP( 21, 0x2, 0, 1, 0,   0,   1,   1    )
#define MPP21_GE1_1		MPP( 21, 0x3, 0, 0, 0,   1,   1,   1    )
#define MPP21_AUDIO_SPDIFO	MPP( 21, 0x4, 0, 1, 0,   0,   1,   1    )
#define MPP21_SATA0_ACTn	MPP( 21, 0x5, 0, 1, 0,   1,   1,   1    )

#define MPP22_GPIO		MPP( 22, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP22_TSMP2		MPP( 22, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP22_TDM_CH2_TX_QL	MPP( 22, 0x2, 0, 1, 0,   0,   1,   1    )
#define MPP22_GE1_2		MPP( 22, 0x3, 0, 0, 0,   1,   1,   1    )
#define MPP22_AUDIO_SPDIFRMKCLK	MPP( 22, 0x4, 0, 1, 0,   0,   1,   1    )
#define MPP22_SATA1_PRESENTn	MPP( 22, 0x5, 0, 1, 0,   0,   1,   1    )

#define MPP23_GPIO		MPP( 23, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP23_TSMP3		MPP( 23, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP23_TDM_CH2_RX_QL	MPP( 23, 0x2, 1, 0, 0,   0,   1,   1    )
#define MPP23_GE1_3		MPP( 23, 0x3, 0, 0, 0,   1,   1,   1    )
#define MPP23_AUDIO_I2SBCLK	MPP( 23, 0x4, 0, 1, 0,   0,   1,   1    )
#define MPP23_SATA0_PRESENTn	MPP( 23, 0x5, 0, 1, 0,   1,   1,   1    )

#define MPP24_GPIO		MPP( 24, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP24_TSMP4		MPP( 24, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP24_TDM_SPI_CS0	DEV( 24, 0x2, 0, 1, 0,   0,   1,   1    )
#define MPP24_GE1_4		MPP( 24, 0x3, 0, 0, 0,   1,   1,   1    )
#define MPP24_AUDIO_I2SDO	MPP( 24, 0x4, 0, 1, 0,   0,   1,   1    )

#define MPP25_GPIO		MPP( 25, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP25_TSMP5		MPP( 25, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP25_TDM_SPI_SCK	MPP( 25, 0x2, 0, 1, 0,   0,   1,   1    )
#define MPP25_GE1_5		MPP( 25, 0x3, 0, 0, 0,   1,   1,   1    )
#define MPP25_AUDIO_I2SLRCLK	MPP( 25, 0x4, 0, 1, 0,   0,   1,   1    )

#define MPP26_GPIO		MPP( 26, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP26_TSMP6		MPP( 26, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP26_TDM_SPI_MISO	MPP( 26, 0x2, 1, 0, 0,   0,   1,   1    )
#define MPP26_GE1_6		MPP( 26, 0x3, 0, 0, 0,   1,   1,   1    )
#define MPP26_AUDIO_I2SMCLK	MPP( 26, 0x4, 0, 1, 0,   0,   1,   1    )

#define MPP27_GPIO		MPP( 27, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP27_TSMP7		MPP( 27, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP27_TDM_SPI_MOSI	MPP( 27, 0x2, 0, 1, 0,   0,   1,   1    )
#define MPP27_GE1_7		MPP( 27, 0x3, 0, 0, 0,   1,   1,   1    )
#define MPP27_AUDIO_I2SDI	MPP( 27, 0x4, 1, 0, 0,   0,   1,   1    )

#define MPP28_GPIO		MPP( 28, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP28_TSMP8		MPP( 28, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP28_TDM_CODEC_INTn	MPP( 28, 0x2, 0, 0, 0,   0,   1,   1    )
#define MPP28_GE1_8		MPP( 28, 0x3, 0, 0, 0,   1,   1,   1    )
#define MPP28_AUDIO_EXTCLK	MPP( 28, 0x4, 1, 0, 0,   0,   1,   1    )

#define MPP29_GPIO		MPP( 29, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP29_TSMP9		MPP( 29, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP29_TDM_CODEC_RSTn	MPP( 29, 0x2, 0, 0, 0,   0,   1,   1    )
#define MPP29_GE1_9		MPP( 29, 0x3, 0, 0, 0,   1,   1,   1    )

#define MPP30_GPIO		MPP( 30, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP30_TSMP10		MPP( 30, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP30_TDM_PCLK		MPP( 30, 0x2, 1, 1, 0,   0,   1,   1    )
#define MPP30_GE1_10		MPP( 30, 0x3, 0, 0, 0,   1,   1,   1    )

#define MPP31_GPIO		MPP( 31, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP31_TSMP11		MPP( 31, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP31_TDM_FS		MPP( 31, 0x2, 1, 1, 0,   0,   1,   1    )
#define MPP31_GE1_11		MPP( 31, 0x3, 0, 0, 0,   1,   1,   1    )

#define MPP32_GPIO		MPP( 32, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP32_TSMP12		MPP( 32, 0x1, 1, 1, 0,   0,   1,   1    )
#define MPP32_TDM_DRX		MPP( 32, 0x2, 1, 0, 0,   0,   1,   1    )
#define MPP32_GE1_12		MPP( 32, 0x3, 0, 0, 0,   1,   1,   1    )

#define MPP33_GPIO		MPP( 33, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP33_TDM_DTX		MPP( 33, 0x2, 0, 1, 0,   0,   1,   1    )
#define MPP33_GE1_13		MPP( 33, 0x3, 0, 0, 0,   1,   1,   1    )

#define MPP34_GPIO		MPP( 34, 0x0, 1, 1, 0,   1,   1,   1    )
#define MPP34_TDM_SPI_CS1	MPP( 34, 0x2, 0, 1, 0,   0,   1,   1    )
#define MPP34_GE1_14		MPP( 34, 0x3, 0, 0, 0,   1,   1,   1    )

#define MPP35_GPIO		MPP( 35, 0x0, 1, 1, 1,   1,   1,   1    )
#define MPP35_TDM_CH0_TX_QL	MPP( 35, 0x2, 0, 1, 0,   0,   1,   1    )
#define MPP35_GE1_15		MPP( 35, 0x3, 0, 0, 0,   1,   1,   1    )
#define MPP35_SATA0_ACTn	MPP( 35, 0x5, 0, 1, 0,   1,   1,   1    )
#define MPP35_MII0_RXERR	MPP( 35, 0xc, 1, 0, 1,   1,   1,   1    )

#define MPP36_GPIO		MPP( 36, 0x0, 1, 1, 1,   0,   0,   1    )
#define MPP36_TSMP0		MPP( 36, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP36_TDM_SPI_CS1	MPP( 36, 0x2, 0, 1, 0,   0,   0,   1    )
#define MPP36_AUDIO_SPDIFI	MPP( 36, 0x4, 1, 0, 1,   0,   0,   1    )

#define MPP37_GPIO		MPP( 37, 0x0, 1, 1, 1,   0,   0,   1    )
#define MPP37_TSMP1		MPP( 37, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP37_TDM_CH2_TX_QL	MPP( 37, 0x2, 0, 1, 0,   0,   0,   1    )
#define MPP37_AUDIO_SPDIFO	MPP( 37, 0x4, 0, 1, 1,   0,   0,   1    )

#define MPP38_GPIO		MPP( 38, 0x0, 1, 1, 1,   0,   0,   1    )
#define MPP38_TSMP2		MPP( 38, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP38_TDM_CH2_RX_QL	MPP( 38, 0x2, 0, 1, 0,   0,   0,   1    )
#define MPP38_AUDIO_SPDIFRMLCLK	MPP( 38, 0x4, 0, 1, 1,   0,   0,   1    )

#define MPP39_GPIO		MPP( 39, 0x0, 1, 1, 1,   0,   0,   1    )
#define MPP39_TSMP3		MPP( 39, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP39_TDM_SPI_CS0	MPP( 39, 0x2, 0, 1, 0,   0,   0,   1    )
#define MPP39_AUDIO_I2SBCLK	MPP( 39, 0x4, 0, 1, 1,   0,   0,   1    )

#define MPP40_GPIO		MPP( 40, 0x0, 1, 1, 1,   0,   0,   1    )
#define MPP40_TSMP4		MPP( 40, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP40_TDM_SPI_SCK	MPP( 40, 0x2, 0, 1, 0,   0,   0,   1    )
#define MPP40_AUDIO_I2SDO	MPP( 40, 0x4, 0, 1, 1,   0,   0,   1    )

#define MPP41_GPIO		MPP( 41, 0x0, 1, 1, 1,   0,   0,   1    )
#define MPP41_TSMP5		MPP( 41, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP41_TDM_SPI_MISO	MPP( 41, 0x2, 1, 0, 0,   0,   0,   1    )
#define MPP41_AUDIO_I2SLRC	MPP( 41, 0x4, 0, 1, 1,   0,   0,   1    )

#define MPP42_GPIO		MPP( 42, 0x0, 1, 1, 1,   0,   0,   1    )
#define MPP42_TSMP6		MPP( 42, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP42_TDM_SPI_MOSI	MPP( 42, 0x2, 0, 1, 0,   0,   0,   1    )
#define MPP42_AUDIO_I2SMCLK	MPP( 42, 0x4, 0, 1, 1,   0,   0,   1    )

#define MPP43_GPIO		MPP( 43, 0x0, 1, 1, 1,   0,   0,   1    )
#define MPP43_TSMP7		MPP( 43, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP43_TDM_CODEC_INTn	MPP( 43, 0x2, 0, 0, 0,   0,   0,   1    )
#define MPP43_AUDIO_I2SDI	MPP( 43, 0x4, 1, 0, 1,   0,   0,   1    )

#define MPP44_GPIO		MPP( 44, 0x0, 1, 1, 1,   0,   0,   1    )
#define MPP44_TSMP8		MPP( 44, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP44_TDM_CODEC_RSTn	MPP( 44, 0x2, 0, 0, 0,   0,   0,   1    )
#define MPP44_AUDIO_EXTCLK	MPP( 44, 0x4, 1, 0, 1,   0,   0,   1    )

#define MPP45_GPIO		MPP( 45, 0x0, 1, 1, 0,   0,   0,   1    )
#define MPP45_TSMP9		MPP( 45, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP45_TDM_PCLK		MPP( 45, 0x2, 1, 1, 0,   0,   0,   1    )

#define MPP46_GPIO		MPP( 46, 0x0, 1, 1, 0,   0,   0,   1    )
#define MPP46_TSMP10		MPP( 46, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP46_TDM_FS		MPP( 46, 0x2, 1, 1, 0,   0,   0,   1    )

#define MPP47_GPIO		MPP( 47, 0x0, 1, 1, 0,   0,   0,   1    )
#define MPP47_TSMP11		MPP( 47, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP47_TDM_DRX		MPP( 47, 0x2, 1, 0, 0,   0,   0,   1    )

#define MPP48_GPIO		MPP( 48, 0x0, 1, 1, 0,   0,   0,   1    )
#define MPP48_TSMP12		MPP( 48, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP48_TDM_DTX		MPP( 48. 0x2, 0, 1, 0,   0,   0,   1    )

#define MPP49_GPIO		MPP( 49, 0x0, 1, 1, 0,   0,   0,   1    )
#define MPP49_TSMP9		MPP( 49, 0x1, 1, 1, 0,   0,   0,   1    )
#define MPP49_TDM_CH0_RX_QL	MPP( 49, 0x2, 0, 1, 0,   0,   0,   1    )
#define MPP49_PTP_CLK		MPP( 49, 0x5, 1, 0, 0,   0,   0,   1    )

#define MPP_MAX			49

void kirkwood_mpp_conf(unsigned int *mpp_list);

#endif
