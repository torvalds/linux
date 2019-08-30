/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * DWMAC4 DMA Header file.
 *
 * Copyright (C) 2007-2015  STMicroelectronics Ltd
 *
 * Author: Alexandre Torgue <alexandre.torgue@st.com>
 */

#ifndef __DWMAC4_DMA_H__
#define __DWMAC4_DMA_H__

/* Define the max channel number used for tx (also rx).
 * dwmac4 accepts up to 8 channels for TX (and also 8 channels for RX
 */
#define DMA_CHANNEL_NB_MAX		1

#define DMA_BUS_MODE			0x00001000
#define DMA_SYS_BUS_MODE		0x00001004
#define DMA_STATUS			0x00001008
#define DMA_DEBUG_STATUS_0		0x0000100c
#define DMA_DEBUG_STATUS_1		0x00001010
#define DMA_DEBUG_STATUS_2		0x00001014
#define DMA_AXI_BUS_MODE		0x00001028

/* DMA Bus Mode bitmap */
#define DMA_BUS_MODE_SFT_RESET		BIT(0)

/* DMA SYS Bus Mode bitmap */
#define DMA_BUS_MODE_SPH		BIT(24)
#define DMA_BUS_MODE_PBL		BIT(16)
#define DMA_BUS_MODE_PBL_SHIFT		16
#define DMA_BUS_MODE_RPBL_SHIFT		16
#define DMA_BUS_MODE_MB			BIT(14)
#define DMA_BUS_MODE_FB			BIT(0)

/* DMA Interrupt top status */
#define DMA_STATUS_MAC			BIT(17)
#define DMA_STATUS_MTL			BIT(16)
#define DMA_STATUS_CHAN7		BIT(7)
#define DMA_STATUS_CHAN6		BIT(6)
#define DMA_STATUS_CHAN5		BIT(5)
#define DMA_STATUS_CHAN4		BIT(4)
#define DMA_STATUS_CHAN3		BIT(3)
#define DMA_STATUS_CHAN2		BIT(2)
#define DMA_STATUS_CHAN1		BIT(1)
#define DMA_STATUS_CHAN0		BIT(0)

/* DMA debug status bitmap */
#define DMA_DEBUG_STATUS_TS_MASK	0xf
#define DMA_DEBUG_STATUS_RS_MASK	0xf

/* DMA AXI bitmap */
#define DMA_AXI_EN_LPI			BIT(31)
#define DMA_AXI_LPI_XIT_FRM		BIT(30)
#define DMA_AXI_WR_OSR_LMT		GENMASK(27, 24)
#define DMA_AXI_WR_OSR_LMT_SHIFT	24
#define DMA_AXI_RD_OSR_LMT		GENMASK(19, 16)
#define DMA_AXI_RD_OSR_LMT_SHIFT	16

#define DMA_AXI_OSR_MAX			0xf
#define DMA_AXI_MAX_OSR_LIMIT ((DMA_AXI_OSR_MAX << DMA_AXI_WR_OSR_LMT_SHIFT) | \
				(DMA_AXI_OSR_MAX << DMA_AXI_RD_OSR_LMT_SHIFT))

#define DMA_SYS_BUS_MB			BIT(14)
#define DMA_AXI_1KBBE			BIT(13)
#define DMA_SYS_BUS_AAL			BIT(12)
#define DMA_AXI_BLEN256			BIT(7)
#define DMA_AXI_BLEN128			BIT(6)
#define DMA_AXI_BLEN64			BIT(5)
#define DMA_AXI_BLEN32			BIT(4)
#define DMA_AXI_BLEN16			BIT(3)
#define DMA_AXI_BLEN8			BIT(2)
#define DMA_AXI_BLEN4			BIT(1)
#define DMA_SYS_BUS_FB			BIT(0)

#define DMA_BURST_LEN_DEFAULT		(DMA_AXI_BLEN256 | DMA_AXI_BLEN128 | \
					DMA_AXI_BLEN64 | DMA_AXI_BLEN32 | \
					DMA_AXI_BLEN16 | DMA_AXI_BLEN8 | \
					DMA_AXI_BLEN4)

#define DMA_AXI_BURST_LEN_MASK		0x000000FE

/* Following DMA defines are chanels oriented */
#define DMA_CHAN_BASE_ADDR		0x00001100
#define DMA_CHAN_BASE_OFFSET		0x80
#define DMA_CHANX_BASE_ADDR(x)		(DMA_CHAN_BASE_ADDR + \
					(x * DMA_CHAN_BASE_OFFSET))
#define DMA_CHAN_REG_NUMBER		17

#define DMA_CHAN_CONTROL(x)		DMA_CHANX_BASE_ADDR(x)
#define DMA_CHAN_TX_CONTROL(x)		(DMA_CHANX_BASE_ADDR(x) + 0x4)
#define DMA_CHAN_RX_CONTROL(x)		(DMA_CHANX_BASE_ADDR(x) + 0x8)
#define DMA_CHAN_TX_BASE_ADDR(x)	(DMA_CHANX_BASE_ADDR(x) + 0x14)
#define DMA_CHAN_RX_BASE_ADDR(x)	(DMA_CHANX_BASE_ADDR(x) + 0x1c)
#define DMA_CHAN_TX_END_ADDR(x)		(DMA_CHANX_BASE_ADDR(x) + 0x20)
#define DMA_CHAN_RX_END_ADDR(x)		(DMA_CHANX_BASE_ADDR(x) + 0x28)
#define DMA_CHAN_TX_RING_LEN(x)		(DMA_CHANX_BASE_ADDR(x) + 0x2c)
#define DMA_CHAN_RX_RING_LEN(x)		(DMA_CHANX_BASE_ADDR(x) + 0x30)
#define DMA_CHAN_INTR_ENA(x)		(DMA_CHANX_BASE_ADDR(x) + 0x34)
#define DMA_CHAN_RX_WATCHDOG(x)		(DMA_CHANX_BASE_ADDR(x) + 0x38)
#define DMA_CHAN_SLOT_CTRL_STATUS(x)	(DMA_CHANX_BASE_ADDR(x) + 0x3c)
#define DMA_CHAN_CUR_TX_DESC(x)		(DMA_CHANX_BASE_ADDR(x) + 0x44)
#define DMA_CHAN_CUR_RX_DESC(x)		(DMA_CHANX_BASE_ADDR(x) + 0x4c)
#define DMA_CHAN_CUR_TX_BUF_ADDR(x)	(DMA_CHANX_BASE_ADDR(x) + 0x54)
#define DMA_CHAN_CUR_RX_BUF_ADDR(x)	(DMA_CHANX_BASE_ADDR(x) + 0x5c)
#define DMA_CHAN_STATUS(x)		(DMA_CHANX_BASE_ADDR(x) + 0x60)

/* DMA Control X */
#define DMA_CONTROL_MSS_MASK		GENMASK(13, 0)

/* DMA Tx Channel X Control register defines */
#define DMA_CONTROL_TSE			BIT(12)
#define DMA_CONTROL_OSP			BIT(4)
#define DMA_CONTROL_ST			BIT(0)

/* DMA Rx Channel X Control register defines */
#define DMA_CONTROL_SR			BIT(0)
#define DMA_RBSZ_MASK			GENMASK(14, 1)
#define DMA_RBSZ_SHIFT			1

/* Interrupt status per channel */
#define DMA_CHAN_STATUS_REB		GENMASK(21, 19)
#define DMA_CHAN_STATUS_REB_SHIFT	19
#define DMA_CHAN_STATUS_TEB		GENMASK(18, 16)
#define DMA_CHAN_STATUS_TEB_SHIFT	16
#define DMA_CHAN_STATUS_NIS		BIT(15)
#define DMA_CHAN_STATUS_AIS		BIT(14)
#define DMA_CHAN_STATUS_CDE		BIT(13)
#define DMA_CHAN_STATUS_FBE		BIT(12)
#define DMA_CHAN_STATUS_ERI		BIT(11)
#define DMA_CHAN_STATUS_ETI		BIT(10)
#define DMA_CHAN_STATUS_RWT		BIT(9)
#define DMA_CHAN_STATUS_RPS		BIT(8)
#define DMA_CHAN_STATUS_RBU		BIT(7)
#define DMA_CHAN_STATUS_RI		BIT(6)
#define DMA_CHAN_STATUS_TBU		BIT(2)
#define DMA_CHAN_STATUS_TPS		BIT(1)
#define DMA_CHAN_STATUS_TI		BIT(0)

/* Interrupt enable bits per channel */
#define DMA_CHAN_INTR_ENA_NIE		BIT(16)
#define DMA_CHAN_INTR_ENA_AIE		BIT(15)
#define DMA_CHAN_INTR_ENA_NIE_4_10	BIT(15)
#define DMA_CHAN_INTR_ENA_AIE_4_10	BIT(14)
#define DMA_CHAN_INTR_ENA_CDE		BIT(13)
#define DMA_CHAN_INTR_ENA_FBE		BIT(12)
#define DMA_CHAN_INTR_ENA_ERE		BIT(11)
#define DMA_CHAN_INTR_ENA_ETE		BIT(10)
#define DMA_CHAN_INTR_ENA_RWE		BIT(9)
#define DMA_CHAN_INTR_ENA_RSE		BIT(8)
#define DMA_CHAN_INTR_ENA_RBUE		BIT(7)
#define DMA_CHAN_INTR_ENA_RIE		BIT(6)
#define DMA_CHAN_INTR_ENA_TBUE		BIT(2)
#define DMA_CHAN_INTR_ENA_TSE		BIT(1)
#define DMA_CHAN_INTR_ENA_TIE		BIT(0)

#define DMA_CHAN_INTR_NORMAL		(DMA_CHAN_INTR_ENA_NIE | \
					 DMA_CHAN_INTR_ENA_RIE | \
					 DMA_CHAN_INTR_ENA_TIE)

#define DMA_CHAN_INTR_ABNORMAL		(DMA_CHAN_INTR_ENA_AIE | \
					 DMA_CHAN_INTR_ENA_FBE)
/* DMA default interrupt mask for 4.00 */
#define DMA_CHAN_INTR_DEFAULT_MASK	(DMA_CHAN_INTR_NORMAL | \
					 DMA_CHAN_INTR_ABNORMAL)

#define DMA_CHAN_INTR_NORMAL_4_10	(DMA_CHAN_INTR_ENA_NIE_4_10 | \
					 DMA_CHAN_INTR_ENA_RIE | \
					 DMA_CHAN_INTR_ENA_TIE)

#define DMA_CHAN_INTR_ABNORMAL_4_10	(DMA_CHAN_INTR_ENA_AIE_4_10 | \
					 DMA_CHAN_INTR_ENA_FBE)
/* DMA default interrupt mask for 4.10a */
#define DMA_CHAN_INTR_DEFAULT_MASK_4_10	(DMA_CHAN_INTR_NORMAL_4_10 | \
					 DMA_CHAN_INTR_ABNORMAL_4_10)

/* channel 0 specific fields */
#define DMA_CHAN0_DBG_STAT_TPS		GENMASK(15, 12)
#define DMA_CHAN0_DBG_STAT_TPS_SHIFT	12
#define DMA_CHAN0_DBG_STAT_RPS		GENMASK(11, 8)
#define DMA_CHAN0_DBG_STAT_RPS_SHIFT	8

int dwmac4_dma_reset(void __iomem *ioaddr);
void dwmac4_enable_dma_irq(void __iomem *ioaddr, u32 chan);
void dwmac410_enable_dma_irq(void __iomem *ioaddr, u32 chan);
void dwmac4_disable_dma_irq(void __iomem *ioaddr, u32 chan);
void dwmac4_dma_start_tx(void __iomem *ioaddr, u32 chan);
void dwmac4_dma_stop_tx(void __iomem *ioaddr, u32 chan);
void dwmac4_dma_start_rx(void __iomem *ioaddr, u32 chan);
void dwmac4_dma_stop_rx(void __iomem *ioaddr, u32 chan);
int dwmac4_dma_interrupt(void __iomem *ioaddr,
			 struct stmmac_extra_stats *x, u32 chan);
void dwmac4_set_rx_ring_len(void __iomem *ioaddr, u32 len, u32 chan);
void dwmac4_set_tx_ring_len(void __iomem *ioaddr, u32 len, u32 chan);
void dwmac4_set_rx_tail_ptr(void __iomem *ioaddr, u32 tail_ptr, u32 chan);
void dwmac4_set_tx_tail_ptr(void __iomem *ioaddr, u32 tail_ptr, u32 chan);

#endif /* __DWMAC4_DMA_H__ */
