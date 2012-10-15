/*
 *  OMAP1 DMA channel definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __OMAP1_DMA_CHANNEL_H
#define __OMAP1_DMA_CHANNEL_H

/* DMA channels for omap1 */
#define OMAP_DMA_NO_DEVICE		0
#define OMAP_DMA_MCSI1_TX		1
#define OMAP_DMA_MCSI1_RX		2
#define OMAP_DMA_I2C_RX			3
#define OMAP_DMA_I2C_TX			4
#define OMAP_DMA_EXT_NDMA_REQ		5
#define OMAP_DMA_EXT_NDMA_REQ2		6
#define OMAP_DMA_UWIRE_TX		7
#define OMAP_DMA_MCBSP1_TX		8
#define OMAP_DMA_MCBSP1_RX		9
#define OMAP_DMA_MCBSP3_TX		10
#define OMAP_DMA_MCBSP3_RX		11
#define OMAP_DMA_UART1_TX		12
#define OMAP_DMA_UART1_RX		13
#define OMAP_DMA_UART2_TX		14
#define OMAP_DMA_UART2_RX		15
#define OMAP_DMA_MCBSP2_TX		16
#define OMAP_DMA_MCBSP2_RX		17
#define OMAP_DMA_UART3_TX		18
#define OMAP_DMA_UART3_RX		19
#define OMAP_DMA_CAMERA_IF_RX		20
#define OMAP_DMA_MMC_TX			21
#define OMAP_DMA_MMC_RX			22
#define OMAP_DMA_NAND			23
#define OMAP_DMA_IRQ_LCD_LINE		24
#define OMAP_DMA_MEMORY_STICK		25
#define OMAP_DMA_USB_W2FC_RX0		26
#define OMAP_DMA_USB_W2FC_RX1		27
#define OMAP_DMA_USB_W2FC_RX2		28
#define OMAP_DMA_USB_W2FC_TX0		29
#define OMAP_DMA_USB_W2FC_TX1		30
#define OMAP_DMA_USB_W2FC_TX2		31

/* These are only for 1610 */
#define OMAP_DMA_CRYPTO_DES_IN		32
#define OMAP_DMA_SPI_TX			33
#define OMAP_DMA_SPI_RX			34
#define OMAP_DMA_CRYPTO_HASH		35
#define OMAP_DMA_CCP_ATTN		36
#define OMAP_DMA_CCP_FIFO_NOT_EMPTY	37
#define OMAP_DMA_CMT_APE_TX_CHAN_0	38
#define OMAP_DMA_CMT_APE_RV_CHAN_0	39
#define OMAP_DMA_CMT_APE_TX_CHAN_1	40
#define OMAP_DMA_CMT_APE_RV_CHAN_1	41
#define OMAP_DMA_CMT_APE_TX_CHAN_2	42
#define OMAP_DMA_CMT_APE_RV_CHAN_2	43
#define OMAP_DMA_CMT_APE_TX_CHAN_3	44
#define OMAP_DMA_CMT_APE_RV_CHAN_3	45
#define OMAP_DMA_CMT_APE_TX_CHAN_4	46
#define OMAP_DMA_CMT_APE_RV_CHAN_4	47
#define OMAP_DMA_CMT_APE_TX_CHAN_5	48
#define OMAP_DMA_CMT_APE_RV_CHAN_5	49
#define OMAP_DMA_CMT_APE_TX_CHAN_6	50
#define OMAP_DMA_CMT_APE_RV_CHAN_6	51
#define OMAP_DMA_CMT_APE_TX_CHAN_7	52
#define OMAP_DMA_CMT_APE_RV_CHAN_7	53
#define OMAP_DMA_MMC2_TX		54
#define OMAP_DMA_MMC2_RX		55
#define OMAP_DMA_CRYPTO_DES_OUT		56

#endif /* __OMAP1_DMA_CHANNEL_H */
