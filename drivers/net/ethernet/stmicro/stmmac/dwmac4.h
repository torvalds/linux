/*
 * DWMAC4 Header file.
 *
 * Copyright (C) 2015  STMicroelectronics Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * Author: Alexandre Torgue <alexandre.torgue@st.com>
 */

#ifndef __DWMAC4_H__
#define __DWMAC4_H__

#include "common.h"

/*  MAC registers */
#define GMAC_CONFIG			0x00000000
#define GMAC_PACKET_FILTER		0x00000008
#define GMAC_HASH_TAB_0_31		0x00000010
#define GMAC_HASH_TAB_32_63		0x00000014
#define GMAC_RX_FLOW_CTRL		0x00000090
#define GMAC_QX_TX_FLOW_CTRL(x)		(0x70 + x * 4)
#define GMAC_INT_STATUS			0x000000b0
#define GMAC_INT_EN			0x000000b4
#define GMAC_PCS_BASE			0x000000e0
#define GMAC_PHYIF_CONTROL_STATUS	0x000000f8
#define GMAC_PMT			0x000000c0
#define GMAC_VERSION			0x00000110
#define GMAC_DEBUG			0x00000114
#define GMAC_HW_FEATURE0		0x0000011c
#define GMAC_HW_FEATURE1		0x00000120
#define GMAC_HW_FEATURE2		0x00000124
#define GMAC_MDIO_ADDR			0x00000200
#define GMAC_MDIO_DATA			0x00000204
#define GMAC_ADDR_HIGH(reg)		(0x300 + reg * 8)
#define GMAC_ADDR_LOW(reg)		(0x304 + reg * 8)

/* MAC Packet Filtering */
#define GMAC_PACKET_FILTER_PR		BIT(0)
#define GMAC_PACKET_FILTER_HMC		BIT(2)
#define GMAC_PACKET_FILTER_PM		BIT(4)

#define GMAC_MAX_PERFECT_ADDRESSES	128

/* MAC Flow Control RX */
#define GMAC_RX_FLOW_CTRL_RFE		BIT(0)

/* MAC Flow Control TX */
#define GMAC_TX_FLOW_CTRL_TFE		BIT(1)
#define GMAC_TX_FLOW_CTRL_PT_SHIFT	16

/*  MAC Interrupt bitmap*/
#define GMAC_INT_RGSMIIS		BIT(0)
#define GMAC_INT_PCS_LINK		BIT(1)
#define GMAC_INT_PCS_ANE		BIT(2)
#define GMAC_INT_PCS_PHYIS		BIT(3)
#define GMAC_INT_PMT_EN			BIT(4)
#define GMAC_INT_LPI_EN			BIT(5)

#define	GMAC_PCS_IRQ_DEFAULT	(GMAC_INT_RGSMIIS | GMAC_INT_PCS_LINK |	\
				 GMAC_INT_PCS_ANE)

#define	GMAC_INT_DEFAULT_MASK	GMAC_INT_PMT_EN

enum dwmac4_irq_status {
	time_stamp_irq = 0x00001000,
	mmc_rx_csum_offload_irq = 0x00000800,
	mmc_tx_irq = 0x00000400,
	mmc_rx_irq = 0x00000200,
	mmc_irq = 0x00000100,
	pmt_irq = 0x00000010,
};

/* MAC PMT bitmap */
enum power_event {
	pointer_reset =	0x80000000,
	global_unicast = 0x00000200,
	wake_up_rx_frame = 0x00000040,
	magic_frame = 0x00000020,
	wake_up_frame_en = 0x00000004,
	magic_pkt_en = 0x00000002,
	power_down = 0x00000001,
};

/* MAC Debug bitmap */
#define GMAC_DEBUG_TFCSTS_MASK		GENMASK(18, 17)
#define GMAC_DEBUG_TFCSTS_SHIFT		17
#define GMAC_DEBUG_TFCSTS_IDLE		0
#define GMAC_DEBUG_TFCSTS_WAIT		1
#define GMAC_DEBUG_TFCSTS_GEN_PAUSE	2
#define GMAC_DEBUG_TFCSTS_XFER		3
#define GMAC_DEBUG_TPESTS		BIT(16)
#define GMAC_DEBUG_RFCFCSTS_MASK	GENMASK(2, 1)
#define GMAC_DEBUG_RFCFCSTS_SHIFT	1
#define GMAC_DEBUG_RPESTS		BIT(0)

/* MAC config */
#define GMAC_CONFIG_IPC			BIT(27)
#define GMAC_CONFIG_2K			BIT(22)
#define GMAC_CONFIG_ACS			BIT(20)
#define GMAC_CONFIG_BE			BIT(18)
#define GMAC_CONFIG_JD			BIT(17)
#define GMAC_CONFIG_JE			BIT(16)
#define GMAC_CONFIG_PS			BIT(15)
#define GMAC_CONFIG_FES			BIT(14)
#define GMAC_CONFIG_DM			BIT(13)
#define GMAC_CONFIG_DCRS		BIT(9)
#define GMAC_CONFIG_TE			BIT(1)
#define GMAC_CONFIG_RE			BIT(0)

/* MAC HW features0 bitmap */
#define GMAC_HW_FEAT_ADDMAC		BIT(18)
#define GMAC_HW_FEAT_RXCOESEL		BIT(16)
#define GMAC_HW_FEAT_TXCOSEL		BIT(14)
#define GMAC_HW_FEAT_EEESEL		BIT(13)
#define GMAC_HW_FEAT_TSSEL		BIT(12)
#define GMAC_HW_FEAT_MMCSEL		BIT(8)
#define GMAC_HW_FEAT_MGKSEL		BIT(7)
#define GMAC_HW_FEAT_RWKSEL		BIT(6)
#define GMAC_HW_FEAT_SMASEL		BIT(5)
#define GMAC_HW_FEAT_VLHASH		BIT(4)
#define GMAC_HW_FEAT_PCSSEL		BIT(3)
#define GMAC_HW_FEAT_HDSEL		BIT(2)
#define GMAC_HW_FEAT_GMIISEL		BIT(1)
#define GMAC_HW_FEAT_MIISEL		BIT(0)

/* MAC HW features1 bitmap */
#define GMAC_HW_FEAT_AVSEL		BIT(20)
#define GMAC_HW_TSOEN			BIT(18)

/* MAC HW features2 bitmap */
#define GMAC_HW_FEAT_TXCHCNT		GENMASK(21, 18)
#define GMAC_HW_FEAT_RXCHCNT		GENMASK(15, 12)

/* MAC HW ADDR regs */
#define GMAC_HI_DCS			GENMASK(18, 16)
#define GMAC_HI_DCS_SHIFT		16
#define GMAC_HI_REG_AE			BIT(31)

/*  MTL registers */
#define MTL_INT_STATUS			0x00000c20
#define MTL_INT_Q0			BIT(0)

#define MTL_CHAN_BASE_ADDR		0x00000d00
#define MTL_CHAN_BASE_OFFSET		0x40
#define MTL_CHANX_BASE_ADDR(x)		(MTL_CHAN_BASE_ADDR + \
					(x * MTL_CHAN_BASE_OFFSET))

#define MTL_CHAN_TX_OP_MODE(x)		MTL_CHANX_BASE_ADDR(x)
#define MTL_CHAN_TX_DEBUG(x)		(MTL_CHANX_BASE_ADDR(x) + 0x8)
#define MTL_CHAN_INT_CTRL(x)		(MTL_CHANX_BASE_ADDR(x) + 0x2c)
#define MTL_CHAN_RX_OP_MODE(x)		(MTL_CHANX_BASE_ADDR(x) + 0x30)
#define MTL_CHAN_RX_DEBUG(x)		(MTL_CHANX_BASE_ADDR(x) + 0x38)

#define MTL_OP_MODE_RSF			BIT(5)
#define MTL_OP_MODE_TXQEN		BIT(3)
#define MTL_OP_MODE_TSF			BIT(1)

#define MTL_OP_MODE_TQS_MASK		GENMASK(24, 16)

#define MTL_OP_MODE_TTC_MASK		0x70
#define MTL_OP_MODE_TTC_SHIFT		4

#define MTL_OP_MODE_TTC_32		0
#define MTL_OP_MODE_TTC_64		(1 << MTL_OP_MODE_TTC_SHIFT)
#define MTL_OP_MODE_TTC_96		(2 << MTL_OP_MODE_TTC_SHIFT)
#define MTL_OP_MODE_TTC_128		(3 << MTL_OP_MODE_TTC_SHIFT)
#define MTL_OP_MODE_TTC_192		(4 << MTL_OP_MODE_TTC_SHIFT)
#define MTL_OP_MODE_TTC_256		(5 << MTL_OP_MODE_TTC_SHIFT)
#define MTL_OP_MODE_TTC_384		(6 << MTL_OP_MODE_TTC_SHIFT)
#define MTL_OP_MODE_TTC_512		(7 << MTL_OP_MODE_TTC_SHIFT)

#define MTL_OP_MODE_RTC_MASK		0x18
#define MTL_OP_MODE_RTC_SHIFT		3

#define MTL_OP_MODE_RTC_32		(1 << MTL_OP_MODE_RTC_SHIFT)
#define MTL_OP_MODE_RTC_64		0
#define MTL_OP_MODE_RTC_96		(2 << MTL_OP_MODE_RTC_SHIFT)
#define MTL_OP_MODE_RTC_128		(3 << MTL_OP_MODE_RTC_SHIFT)

/*  MTL debug */
#define MTL_DEBUG_TXSTSFSTS		BIT(5)
#define MTL_DEBUG_TXFSTS		BIT(4)
#define MTL_DEBUG_TWCSTS		BIT(3)

/* MTL debug: Tx FIFO Read Controller Status */
#define MTL_DEBUG_TRCSTS_MASK		GENMASK(2, 1)
#define MTL_DEBUG_TRCSTS_SHIFT		1
#define MTL_DEBUG_TRCSTS_IDLE		0
#define MTL_DEBUG_TRCSTS_READ		1
#define MTL_DEBUG_TRCSTS_TXW		2
#define MTL_DEBUG_TRCSTS_WRITE		3
#define MTL_DEBUG_TXPAUSED		BIT(0)

/* MAC debug: GMII or MII Transmit Protocol Engine Status */
#define MTL_DEBUG_RXFSTS_MASK		GENMASK(5, 4)
#define MTL_DEBUG_RXFSTS_SHIFT		4
#define MTL_DEBUG_RXFSTS_EMPTY		0
#define MTL_DEBUG_RXFSTS_BT		1
#define MTL_DEBUG_RXFSTS_AT		2
#define MTL_DEBUG_RXFSTS_FULL		3
#define MTL_DEBUG_RRCSTS_MASK		GENMASK(2, 1)
#define MTL_DEBUG_RRCSTS_SHIFT		1
#define MTL_DEBUG_RRCSTS_IDLE		0
#define MTL_DEBUG_RRCSTS_RDATA		1
#define MTL_DEBUG_RRCSTS_RSTAT		2
#define MTL_DEBUG_RRCSTS_FLUSH		3
#define MTL_DEBUG_RWCSTS		BIT(0)

/*  MTL interrupt */
#define MTL_RX_OVERFLOW_INT_EN		BIT(24)
#define MTL_RX_OVERFLOW_INT		BIT(16)

/* Default operating mode of the MAC */
#define GMAC_CORE_INIT (GMAC_CONFIG_JD | GMAC_CONFIG_PS | GMAC_CONFIG_ACS | \
			GMAC_CONFIG_BE | GMAC_CONFIG_DCRS)

/* To dump the core regs excluding  the Address Registers */
#define	GMAC_REG_NUM	132

/*  MTL debug */
#define MTL_DEBUG_TXSTSFSTS		BIT(5)
#define MTL_DEBUG_TXFSTS		BIT(4)
#define MTL_DEBUG_TWCSTS		BIT(3)

/* MTL debug: Tx FIFO Read Controller Status */
#define MTL_DEBUG_TRCSTS_MASK		GENMASK(2, 1)
#define MTL_DEBUG_TRCSTS_SHIFT		1
#define MTL_DEBUG_TRCSTS_IDLE		0
#define MTL_DEBUG_TRCSTS_READ		1
#define MTL_DEBUG_TRCSTS_TXW		2
#define MTL_DEBUG_TRCSTS_WRITE		3
#define MTL_DEBUG_TXPAUSED		BIT(0)

/* MAC debug: GMII or MII Transmit Protocol Engine Status */
#define MTL_DEBUG_RXFSTS_MASK		GENMASK(5, 4)
#define MTL_DEBUG_RXFSTS_SHIFT		4
#define MTL_DEBUG_RXFSTS_EMPTY		0
#define MTL_DEBUG_RXFSTS_BT		1
#define MTL_DEBUG_RXFSTS_AT		2
#define MTL_DEBUG_RXFSTS_FULL		3
#define MTL_DEBUG_RRCSTS_MASK		GENMASK(2, 1)
#define MTL_DEBUG_RRCSTS_SHIFT		1
#define MTL_DEBUG_RRCSTS_IDLE		0
#define MTL_DEBUG_RRCSTS_RDATA		1
#define MTL_DEBUG_RRCSTS_RSTAT		2
#define MTL_DEBUG_RRCSTS_FLUSH		3
#define MTL_DEBUG_RWCSTS		BIT(0)

/* SGMII/RGMII status register */
#define GMAC_PHYIF_CTRLSTATUS_TC		BIT(0)
#define GMAC_PHYIF_CTRLSTATUS_LUD		BIT(1)
#define GMAC_PHYIF_CTRLSTATUS_SMIDRXS		BIT(4)
#define GMAC_PHYIF_CTRLSTATUS_LNKMOD		BIT(16)
#define GMAC_PHYIF_CTRLSTATUS_SPEED		GENMASK(18, 17)
#define GMAC_PHYIF_CTRLSTATUS_SPEED_SHIFT	17
#define GMAC_PHYIF_CTRLSTATUS_LNKSTS		BIT(19)
#define GMAC_PHYIF_CTRLSTATUS_JABTO		BIT(20)
#define GMAC_PHYIF_CTRLSTATUS_FALSECARDET	BIT(21)
/* LNKMOD */
#define GMAC_PHYIF_CTRLSTATUS_LNKMOD_MASK	0x1
/* LNKSPEED */
#define GMAC_PHYIF_CTRLSTATUS_SPEED_125		0x2
#define GMAC_PHYIF_CTRLSTATUS_SPEED_25		0x1
#define GMAC_PHYIF_CTRLSTATUS_SPEED_2_5		0x0

extern const struct stmmac_dma_ops dwmac4_dma_ops;
extern const struct stmmac_dma_ops dwmac410_dma_ops;
#endif /* __DWMAC4_H__ */
