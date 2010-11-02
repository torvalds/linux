/*
 * arch/arm/mach-tegra/include/mach/spdif.h
 *
 *
 * Copyright (c) 2008-2009, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */


#ifndef __ARCH_ARM_MACH_TEGRA_SPDIF_H
#define __ARCH_ARM_MACH_TEGRA_SPDIF_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/platform_device.h>

/* Offsets from TEGRA_SPDIF_BASE */

#define SPDIF_CTRL_0			0x0
#define SPDIF_STATUS_0			0x4
#define SPDIF_STROBE_CTRL_0		0x8
#define SPDIF_DATA_FIFO_CSR_0		0x0C
#define SPDIF_DATA_OUT_0		0x40
#define SPDIF_DATA_IN_0			0x80
#define SPDIF_CH_STA_RX_A_0		0x100
#define SPDIF_CH_STA_RX_B_0		0x104
#define SPDIF_CH_STA_RX_C_0		0x108
#define SPDIF_CH_STA_RX_D_0		0x10C
#define SPDIF_CH_STA_RX_E_0		0x110
#define SPDIF_CH_STA_RX_F_0		0x114
#define SPDIF_CH_STA_TX_A_0		0x140
#define SPDIF_CH_STA_TX_B_0		0x144
#define SPDIF_CH_STA_TX_C_0		0x148
#define SPDIF_CH_STA_TX_D_0		0x14C
#define SPDIF_CH_STA_TX_E_0		0x150
#define SPDIF_CH_STA_TX_F_0		0x154
#define SPDIF_USR_STA_RX_A_0		0x180
#define SPDIF_USR_DAT_TX_A_0		0x1C0

/*
 * Register SPDIF_CTRL_0
 */

/*
 * 1=start capturing from left channel,0=start
 * capturing from right channel.
 */
#define SPDIF_CTRL_0_CAP_LC			(1<<30)

/* SPDIF receiver(RX):	1=enable, 0=disable. */
#define SPDIF_CTRL_0_RX_EN			(1<<29)

/* SPDIF Transmitter(TX):	1=enable, 0=disable. */
#define SPDIF_CTRL_0_TX_EN			(1<<28)

/* Transmit Channel status:	1=enable, 0=disable. */
#define SPDIF_CTRL_0_TC_EN			(1<<27)

/* Transmit user Data:		1=enable, 0=disable. */
#define SPDIF_CTRL_0_TU_EN			(1<<26)

/* Interrupt on transmit error:	 1=enable, 0=disable. */
#define SPDIF_CTRL_0_IE_TXE			(1<<25)

/* Interrupt on receive error:	 1=enable, 0=disable. */
#define SPDIF_CTRL_0_IE_RXE			(1<<24)

/* Interrupt on invalid preamble:       1=enable, 0=disable. */
#define SPDIF_CTRL_0_IE_P			(1<<23)

/* Interrupt on "B" preamble:	   1=enable, 0=disable. */
#define SPDIF_CTRL_0_IE_B			(1<<22)

/*
 * Interrupt when block of channel status received:
 * 1=enable, 0=disable.
 */
#define SPDIF_CTRL_0_IE_C			(1<<21)

/*
 * Interrupt when a valid information unit (IU) recieve:
 * 1=enable, 0=disable.
 */
#define SPDIF_CTRL_0_IE_U			(1<<20)

/*
 * Interrupt when RX user FIFO attn. level is reached:
 * 1=enable, 0=disable.
 */
#define SPDIF_CTRL_0_QE_RU			(1<<19)

/*
 * Interrupt when TX user FIFO attn. level is reached:
 * 1=enable, 0=disable.
 */
#define SPDIF_CTRL_0_QE_TU			(1<<18)

/*
 * Interrupt when RX data FIFO attn. level is reached:
 * 1=enable, 0=disable.
 */
#define SPDIF_CTRL_0_QE_RX			(1<<17)

/*
 * Interrupt when TX data FIFO attn. level is reached:
 * 1=enable, 0=disable.
 */
#define SPDIF_CTRL_0_QE_TX			(1<<16)

/* Loopback test mode:   1=enable internal loopback, 0=Normal mode. */
#define SPDIF_CTRL_0_LBK_EN			(1<<15)

/*
 * Pack data mode:
 * 1=Packeted left/right channel data into a single word,
 * 0=Single data (16 bit needs to be  padded to match the
 * interface data bit size)
 */
#define SPDIF_CTRL_0_PACK		 (1<<14)

/*
 * 00=16bit data
 * 01=20bit data
 * 10=24bit data
 * 11=raw data
 */
#define SPDIF_BIT_MODE_MODE16BIT	(0)
#define SPDIF_BIT_MODE_MODE20BIT	(1)
#define SPDIF_BIT_MODE_MODE24BIT	(2)
#define SPDIF_BIT_MODE_MODERAW		(3)
#define SPDIF_CTRL_0_BIT_MODE_SHIFT	(12)

#define SPDIF_CTRL_0_BIT_MODE_MASK		\
		((0x3) << SPDIF_CTRL_0_BIT_MODE_SHIFT)
#define SPDIF_CTRL_0_BIT_MODE_MODE16BIT		\
	(SPDIF_BIT_MODE_MODE16BIT << SPDIF_CTRL_0_BIT_MODE_SHIFT)
#define SPDIF_CTRL_0_BIT_MODE_MODE20BIT		\
	(SPDIF_BIT_MODE_MODE20BIT << SPDIF_CTRL_0_BIT_MODE_SHIFT)
#define SPDIF_CTRL_0_BIT_MODE_MODE24BIT		\
	(SPDIF_BIT_MODE_MODE24BIT << SPDIF_CTRL_0_BIT_MODE_SHIFT)
#define SPDIF_CTRL_0_BIT_MODE_MODERAW		\
		(SPDIF_BIT_MODE_MODERAW << SPDIF_CTRL_0_BIT_MODE_SHIFT)


/*
 *  SPDIF Status Register
 * -------------------------
 * Note:  IS_P, IS_B, IS_C, and IS_U are sticky bits.
 * Software must write a 1 to the corresponding bit location
 * to clear the status.
 */

/* Register SPDIF_STATUS_0 */

/*
 * Receiver(RX) shifter is busy receiving data.  1=busy, 0=not busy.
 * This bit is asserted when the receiver first locked onto the
 * preamble of the data stream after RX_EN is asserted.  This bit is
 * deasserted when either,
 * (a)  the end of a frame is reached after RX_EN is deeasserted, or
 * (b)  the SPDIF data stream becomes inactive.
 */
#define SPDIF_STATUS_0_RX_BSY			(1<<29)


/*
 * Transmitter(TX) shifter is busy transmitting data.
 * 1=busy, 0=not busy.
 * This bit is asserted when TX_EN is asserted.
 * This bit is deasserted when the end of a frame is reached after
 * TX_EN is deasserted.
 */
#define SPDIF_STATUS_0_TX_BSY			(1<<28)

/*
 * TX is busy shifting out channel status.  1=busy, 0=not busy.
 * This bit is asserted when both TX_EN and TC_EN are asserted and
 * data from CH_STA_TX_A register is loaded into the internal shifter.
 * This bit is deasserted when either,
 * (a) the end of a frame is reached after TX_EN is deasserted, or
 * (b) CH_STA_TX_F register is loaded into the internal shifter.
 */
#define SPDIF_STATUS_0_TC_BSY			(1<<27)

/*
 * TX User data FIFO busy.  1=busy, 0=not busy.
 * This bit is asserted when TX_EN and TXU_EN are asserted and
 * there's data in the TX user FIFO.  This bit is deassert when either,
 * (a) the end of a frame is reached after TX_EN is deasserted, or
 * (b) there's no data left in the TX user FIFO.
 */
#define SPDIF_STATUS_0_TU_BSY			(1<<26)

/* Tx FIFO Underrun error status:	1=error, 0=no error */
#define SPDIF_STATUS_0_TX_ERR			(1<<25)

/* Rx FIFO Overrun error status:	 1=error, 0=no error */
#define SPDIF_STATUS_0_RX_ERR			(1<<24)

/* Preamble status:	1=bad/missing preamble, 0=Preamble ok */
#define SPDIF_STATUS_0_IS_P			(1<<23)

/* B-preamble detection status: 0=not detected, 1=B-preamble detected */
#define SPDIF_STATUS_0_IS_B			(1<<22)

/*
 * RX channel block data receive status:
 * 1=received entire block of channel status,
 * 0=entire block not recieved yet.
 */
#define SPDIF_STATUS_0_IS_C			(1<<21)

/* RX User Data Valid flag:  1=valid IU detected, 0 = no IU detected. */
#define SPDIF_STATUS_0_IS_U			(1<<20)

/*
 * RX User FIFO Status:
 * 1=attention level reached, 0=attention level not reached.
 */
#define SPDIF_STATUS_0_QS_RU			(1<<19)

/*
 * TX User FIFO Status:
 * 1=attention level reached, 0=attention level not reached.
 */
#define SPDIF_STATUS_0_QS_TU			(1<<18)

/*
 * RX Data FIFO Status:
 * 1=attention level reached, 0=attention level not reached.
 */
#define SPDIF_STATUS_0_QS_RX			(1<<17)

/*
 * TX Data FIFO Status:
 * 1=attention level reached, 0=attention level not reached.
 */
#define SPDIF_STATUS_0_QS_TX			(1<<16)


/* SPDIF FIFO Configuration and Status Register */

/* Register SPDIF_DATA_FIFO_CSR_0 */

#define SPDIF_FIFO_ATN_LVL_ONE_SLOT		0
#define SPDIF_FIFO_ATN_LVL_FOUR_SLOTS		1
#define SPDIF_FIFO_ATN_LVL_EIGHT_SLOTS		2
#define SPDIF_FIFO_ATN_LVL_TWELVE_SLOTS		3


/* Clear Receiver User FIFO (RX USR.FIFO) */
#define SPDIF_DATA_FIFO_CSR_0_RU_CLR		(1<<31)

/*
 * RX USR.FIFO Attention Level:
 * 00=1-slot-full, 01=2-slots-full, 10=3-slots-full, 11=4-slots-full.
 */

#define SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_RU1		(0)
#define SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_RU2		(1)
#define SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_RU3		(2)
#define SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_RU4		(3)

#define SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_SHIFT		(29)
#define SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_MASK	\
		(0x3 << SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_RU1_WORD_FULL		\
		(SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_RU1 <<	\
			SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_SHIF)
#define SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_RU2_WORD_FULL		\
		(SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_RU2 <<	\
			SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_SHIF)
#define SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_RU3_WORD_FULL		\
		(SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_RU3 <<	\
			SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_SHIF)
#define SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_RU4_WORD_FULL		\
		(SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_RU4 <<	\
			SPDIF_DATA_FIFO_CSR_0_RU_ATN_LVL_SHIF)

/* Number of RX USR.FIFO levels with valid data. */
#define SPDIF_DATA_FIFO_CSR_0_FULL_COUNT_SHIFT		(24)
#define SPDIF_DATA_FIFO_CSR_0_FULL_COUNT_MASK			\
		(0x1f << SPDIF_DATA_FIFO_CSR_0_FULL_COUNT_SHIFT)

/* Clear Transmitter User FIFO (TX USR.FIFO) */
#define SPDIF_DATA_FIFO_CSR_0_TU_CLR			 (1<<23)

/*
 * TxUSR.FIFO Attention Level:
 * 11=4-slots-empty, 10=3-slots-empty, 01=2-slots-empty, 00=1-slot-empty.
 */

#define SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_TU1		(0)
#define SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_TU2		(1)
#define SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_TU3		(2)
#define SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_TU4		(3)

#define SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_SHIFT		(21)
#define SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_MASK			\
		(0x3 << SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_TU1_WORD_EMPTY		\
		(SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_TU1 <<	\
			SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_TU2_WORD_EMPTY		\
		(SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_TU2 <<	\
			SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_TU3_WORD_EMPTY		\
		(SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_TU3 <<	\
			SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_TU4_WORD_EMPTY		\
		(SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_TU4 <<	\
			SPDIF_DATA_FIFO_CSR_0_TU_ATN_LVL_SHIFT)

/* Number of Tx USR.FIFO levels that could be filled. */
#define SPDIF_DATA_FIFO_CSR_0_TU_EMPTY_COUNT_SHIFT		(16)
#define SPDIF_DATA_FIFO_CSR_0_TU_EMPTY_COUNT_FIELD		\
		((0x1f) << SPDIF_DATA_FIFO_CSR_0_TU_EMPTY_COUNT_SHIFT)

/* Clear Receiver Data FIFO (RX DATA.FIFO). */
#define SPDIF_DATA_FIFO_CSR_0_RX_CLR			(1<<15)

/*
 * Rx FIFO Attention Level:
 * 11=12-slots-full, 10=8-slots-full, 01=4-slots-full, 00=1-slot-full.
 */
#define SPDIF_DATA_FIFO_CSR_0_RX_ATN_LVL_SHIFT		(13)
#define SPDIF_DATA_FIFO_CSR_0_RX_ATN_LVL_MASK			\
		(0x3 << SPDIF_DATA_FIFO_CSR_0_RX_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_RX_ATN_LVL_RX1_WORD_FULL		\
	(SPDIF_FIFO_ATN_LVL_ONE_SLOT <<				\
		SPDIF_DATA_FIFO_CSR_0_RX_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_RX_ATN_LVL_RX4_WORD_FULL		\
		(SPDIF_FIFO_ATN_LVL_FOUR_SLOTS <<		\
			SPDIF_DATA_FIFO_CSR_0_RX_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_RX_ATN_LVL_RX8_WORD_FULL		\
		(SPDIF_FIFO_ATN_LVL_EIGHT_SLOTS <<		\
			SPDIF_DATA_FIFO_CSR_0_RX_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_RX_ATN_LVL_RX12_WORD_FULL		\
		(SPDIF_FIFO_ATN_LVL_TWELVE_SLOTS <<		\
			SPDIF_DATA_FIFO_CSR_0_RX_ATN_LVL_SHIFT)


/* Number of RX DATA.FIFO levels with valid data */
#define SPDIF_DATA_FIFO_CSR_0_RX_DATA_FIFO_FULL_COUNT_SHIFT	(8)
#define SPDIF_DATA_FIFO_CSR_0_RX_DATA_FIFO_FULL_COUNT_FIELD	\
	((0x1f) << SPDIF_DATA_FIFO_CSR_0_RX_DATA_FIFO_FULL_COUNT_SHIFT)

/* Clear Transmitter Data FIFO (TX DATA.FIFO) */
#define SPDIF_DATA_FIFO_CSR_0_TX_CLR			(1<<7)

/*
 * Tx FIFO Attention Level:
 * 11=12-slots-empty, 10=8-slots-empty, 01=4-slots-empty, 00=1-slot-empty
 */
#define SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_SHIFT		(5)
#define SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_MASK			\
		(0x3 << SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_TX1_WORD_FULL		\
		(SPDIF_FIFO_ATN_LVL_ONE_SLOT <<			\
			SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_TX4_WORD_FULL		\
		(SPDIF_FIFO_ATN_LVL_FOUR_SLOTS <<		\
			SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_TX8_WORD_FULL		\
		(SPDIF_FIFO_ATN_LVL_EIGHT_SLOTS <<		\
			SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_SHIFT)
#define SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_TX12_WORD_FULL		\
		(SPDIF_FIFO_ATN_LVL_TWELVE_SLOTS <<		\
			SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_SHIFT)


/* Number of Tx DATA.FIFO levels that could be filled. */
#define SPDIF_DATA_FIFO_CSR_0_TD_EMPTY_COUNT_SHIFT		(0)
#define SPDIF_DATA_FIFO_CSR_0_TD_EMPTY_COUNT_MASK		\
		((0x1f) << SPDIF_DATA_FIFO_CSR_0_TD_EMPTY_COUNT_SHIFT)


#endif /* __ARCH_ARM_MACH_TEGRA_SPDIF_H */
