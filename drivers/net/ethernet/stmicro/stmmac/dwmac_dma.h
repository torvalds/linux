/* SPDX-License-Identifier: GPL-2.0-only */
/*******************************************************************************
  DWMAC DMA Header file.

  Copyright (C) 2007-2009  STMicroelectronics Ltd


  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#ifndef __DWMAC_DMA_H__
#define __DWMAC_DMA_H__

/* DMA CRS Control and Status Register Mapping */
#define DMA_BUS_MODE		0x00001000	/* Bus Mode */

#define DMA_BUS_MODE_SFT_RESET	0x00000001	/* Software Reset */

#define DMA_XMT_POLL_DEMAND	0x00001004	/* Transmit Poll Demand */
#define DMA_RCV_POLL_DEMAND	0x00001008	/* Received Poll Demand */
#define DMA_RCV_BASE_ADDR	0x0000100c	/* Receive List Base */
#define DMA_TX_BASE_ADDR	0x00001010	/* Transmit List Base */

#define DMA_STATUS		0x00001014	/* Status Register */
#define DMA_STATUS_GPI		0x10000000	/* PMT interrupt */
#define DMA_STATUS_GMI		0x08000000	/* MMC interrupt */
#define DMA_STATUS_GLI		0x04000000	/* GMAC Line interface int */
#define DMA_STATUS_TS_MASK	GENMASK(22, 20)	/* Transmit Process State */
#define DMA_STATUS_RS_MASK	GENMASK(19, 17)	/* Receive Process State */
#define DMA_STATUS_NIS	0x00010000	/* Normal Interrupt Summary */
#define DMA_STATUS_AIS	0x00008000	/* Abnormal Interrupt Summary */
#define DMA_STATUS_ERI	0x00004000	/* Early Receive Interrupt */
#define DMA_STATUS_FBI	0x00002000	/* Fatal Bus Error Interrupt */
#define DMA_STATUS_ETI	0x00000400	/* Early Transmit Interrupt */
#define DMA_STATUS_RWT	0x00000200	/* Receive Watchdog Timeout */
#define DMA_STATUS_RPS	0x00000100	/* Receive Process Stopped */
#define DMA_STATUS_RU	0x00000080	/* Receive Buffer Unavailable */
#define DMA_STATUS_RI	0x00000040	/* Receive Interrupt */
#define DMA_STATUS_UNF	0x00000020	/* Transmit Underflow */
#define DMA_STATUS_OVF	0x00000010	/* Receive Overflow */
#define DMA_STATUS_TJT	0x00000008	/* Transmit Jabber Timeout */
#define DMA_STATUS_TU	0x00000004	/* Transmit Buffer Unavailable */
#define DMA_STATUS_TPS	0x00000002	/* Transmit Process Stopped */
#define DMA_STATUS_TI	0x00000001	/* Transmit Interrupt */

#define DMA_STATUS_MSK_COMMON		(DMA_STATUS_NIS | \
					 DMA_STATUS_AIS | \
					 DMA_STATUS_FBI)

#define DMA_STATUS_MSK_RX		(DMA_STATUS_ERI | \
					 DMA_STATUS_RWT | \
					 DMA_STATUS_RPS | \
					 DMA_STATUS_RU | \
					 DMA_STATUS_RI | \
					 DMA_STATUS_OVF | \
					 DMA_STATUS_MSK_COMMON)

#define DMA_STATUS_MSK_TX		(DMA_STATUS_ETI | \
					 DMA_STATUS_UNF | \
					 DMA_STATUS_TJT | \
					 DMA_STATUS_TU | \
					 DMA_STATUS_TPS | \
					 DMA_STATUS_TI | \
					 DMA_STATUS_MSK_COMMON)

#define DMA_CONTROL		0x00001018	/* Ctrl (Operational Mode) */

/* DMA Control register defines */
#define DMA_CONTROL_FTF		0x00100000	/* Flush transmit FIFO */
#define DMA_CONTROL_ST		0x00002000	/* Start/Stop Transmission */
#define DMA_CONTROL_SR		0x00000002	/* Start/Stop Receive */

#define DMA_INTR_ENA		0x0000101c	/* Interrupt Enable */

/* DMA Normal interrupt */
#define DMA_INTR_ENA_NIE 0x00010000	/* Normal Summary */
#define DMA_INTR_ENA_TIE 0x00000001	/* Transmit Interrupt */
#define DMA_INTR_ENA_RIE 0x00000040	/* Receive Interrupt */

#define DMA_INTR_NORMAL	(DMA_INTR_ENA_NIE | DMA_INTR_ENA_RIE | \
			DMA_INTR_ENA_TIE)

/* DMA Abnormal interrupt */
#define DMA_INTR_ENA_AIE 0x00008000	/* Abnormal Summary */
#define DMA_INTR_ENA_FBE 0x00002000	/* Fatal Bus Error */
#define DMA_INTR_ENA_UNE 0x00000020	/* Tx Underflow */

#define DMA_INTR_ABNORMAL	(DMA_INTR_ENA_AIE | DMA_INTR_ENA_FBE | \
				DMA_INTR_ENA_UNE)

/* DMA default interrupt mask */
#define DMA_INTR_DEFAULT_MASK	(DMA_INTR_NORMAL | DMA_INTR_ABNORMAL)
#define DMA_INTR_DEFAULT_RX	(DMA_INTR_ENA_RIE)
#define DMA_INTR_DEFAULT_TX	(DMA_INTR_ENA_TIE)

#define DMA_MISSED_FRAME_CTR	0x00001020	/* Missed Frame Counter */

/* Following DMA defines are channels oriented */
#define DMA_CHAN_BASE_OFFSET			0x100

static inline u32 dma_chan_base_addr(u32 base, u32 chan)
{
	return base + chan * DMA_CHAN_BASE_OFFSET;
}

#define DMA_CHAN_BUS_MODE(chan)	dma_chan_base_addr(DMA_BUS_MODE, chan)
#define DMA_CHAN_XMT_POLL_DEMAND(chan)	\
				dma_chan_base_addr(DMA_XMT_POLL_DEMAND, chan)
#define DMA_CHAN_RCV_POLL_DEMAND(chan)	\
				dma_chan_base_addr(DMA_RCV_POLL_DEMAND, chan)
#define DMA_CHAN_RCV_BASE_ADDR(chan)	\
				dma_chan_base_addr(DMA_RCV_BASE_ADDR, chan)
#define DMA_CHAN_TX_BASE_ADDR(chan)	\
				dma_chan_base_addr(DMA_TX_BASE_ADDR, chan)
#define DMA_CHAN_STATUS(chan)	dma_chan_base_addr(DMA_STATUS, chan)
#define DMA_CHAN_CONTROL(chan)	dma_chan_base_addr(DMA_CONTROL, chan)
#define DMA_CHAN_INTR_ENA(chan)	dma_chan_base_addr(DMA_INTR_ENA, chan)
#define DMA_CHAN_RX_WATCHDOG(chan)	\
				dma_chan_base_addr(DMA_RX_WATCHDOG, chan)


/* Rx watchdog register */
#define DMA_RX_WATCHDOG		0x00001024

/* AXI Master Bus Mode */
#define DMA_AXI_BUS_MODE	0x00001028

#define DMA_AXI_EN_LPI		BIT(31)
#define DMA_AXI_LPI_XIT_FRM	BIT(30)
#define DMA_AXI_WR_OSR_LMT	GENMASK(23, 20)
#define DMA_AXI_RD_OSR_LMT	GENMASK(19, 16)

#define	DMA_AXI_1KBBE		BIT(13)

#define DMA_AXI_UNDEF		BIT(0)

#define DMA_CUR_TX_BUF_ADDR	0x00001050	/* Current Host Tx Buffer */
#define DMA_CUR_RX_BUF_ADDR	0x00001054	/* Current Host Rx Buffer */
#define DMA_HW_FEATURE		0x00001058	/* HW Feature Register */

#define NUM_DWMAC100_DMA_REGS	9
#define NUM_DWMAC1000_DMA_REGS	23
#define NUM_DWMAC4_DMA_REGS	27

void dwmac_enable_dma_transmission(void __iomem *ioaddr, u32 chan);
void dwmac_enable_dma_reception(void __iomem *ioaddr, u32 chan);
void dwmac_enable_dma_irq(struct stmmac_priv *priv, void __iomem *ioaddr,
			  u32 chan, bool rx, bool tx);
void dwmac_disable_dma_irq(struct stmmac_priv *priv, void __iomem *ioaddr,
			   u32 chan, bool rx, bool tx);
void dwmac_dma_start_tx(struct stmmac_priv *priv, void __iomem *ioaddr,
			u32 chan);
void dwmac_dma_stop_tx(struct stmmac_priv *priv, void __iomem *ioaddr,
		       u32 chan);
void dwmac_dma_start_rx(struct stmmac_priv *priv, void __iomem *ioaddr,
			u32 chan);
void dwmac_dma_stop_rx(struct stmmac_priv *priv, void __iomem *ioaddr,
		       u32 chan);
int dwmac_dma_interrupt(struct stmmac_priv *priv, void __iomem *ioaddr,
			struct stmmac_extra_stats *x, u32 chan, u32 dir);
int dwmac_dma_reset(void __iomem *ioaddr);

#endif /* __DWMAC_DMA_H__ */
