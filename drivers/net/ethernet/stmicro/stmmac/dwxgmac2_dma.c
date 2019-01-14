// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
 * stmmac XGMAC support.
 */

#include <linux/iopoll.h>
#include "stmmac.h"
#include "dwxgmac2.h"

static int dwxgmac2_dma_reset(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + XGMAC_DMA_MODE);

	/* DMA SW reset */
	writel(value | XGMAC_SWR, ioaddr + XGMAC_DMA_MODE);

	return readl_poll_timeout(ioaddr + XGMAC_DMA_MODE, value,
				  !(value & XGMAC_SWR), 0, 100000);
}

static void dwxgmac2_dma_init(void __iomem *ioaddr,
			      struct stmmac_dma_cfg *dma_cfg, int atds)
{
	u32 value = readl(ioaddr + XGMAC_DMA_SYSBUS_MODE);

	if (dma_cfg->aal)
		value |= XGMAC_AAL;

	writel(value, ioaddr + XGMAC_DMA_SYSBUS_MODE);
}

static void dwxgmac2_dma_init_chan(void __iomem *ioaddr,
				   struct stmmac_dma_cfg *dma_cfg, u32 chan)
{
	u32 value = readl(ioaddr + XGMAC_DMA_CH_CONTROL(chan));

	if (dma_cfg->pblx8)
		value |= XGMAC_PBLx8;

	writel(value, ioaddr + XGMAC_DMA_CH_CONTROL(chan));
	writel(XGMAC_DMA_INT_DEFAULT_EN, ioaddr + XGMAC_DMA_CH_INT_EN(chan));
}

static void dwxgmac2_dma_init_rx_chan(void __iomem *ioaddr,
				      struct stmmac_dma_cfg *dma_cfg,
				      u32 dma_rx_phy, u32 chan)
{
	u32 rxpbl = dma_cfg->rxpbl ?: dma_cfg->pbl;
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));
	value &= ~XGMAC_RxPBL;
	value |= (rxpbl << XGMAC_RxPBL_SHIFT) & XGMAC_RxPBL;
	writel(value, ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));

	writel(dma_rx_phy, ioaddr + XGMAC_DMA_CH_RxDESC_LADDR(chan));
}

static void dwxgmac2_dma_init_tx_chan(void __iomem *ioaddr,
				      struct stmmac_dma_cfg *dma_cfg,
				      u32 dma_tx_phy, u32 chan)
{
	u32 txpbl = dma_cfg->txpbl ?: dma_cfg->pbl;
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));
	value &= ~XGMAC_TxPBL;
	value |= (txpbl << XGMAC_TxPBL_SHIFT) & XGMAC_TxPBL;
	value |= XGMAC_OSP;
	writel(value, ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));

	writel(dma_tx_phy, ioaddr + XGMAC_DMA_CH_TxDESC_LADDR(chan));
}

static void dwxgmac2_dma_axi(void __iomem *ioaddr, struct stmmac_axi *axi)
{
	u32 value = readl(ioaddr + XGMAC_DMA_SYSBUS_MODE);
	int i;

	if (axi->axi_lpi_en)
		value |= XGMAC_EN_LPI;
	if (axi->axi_xit_frm)
		value |= XGMAC_LPI_XIT_PKT;

	value &= ~XGMAC_WR_OSR_LMT;
	value |= (axi->axi_wr_osr_lmt << XGMAC_WR_OSR_LMT_SHIFT) &
		XGMAC_WR_OSR_LMT;

	value &= ~XGMAC_RD_OSR_LMT;
	value |= (axi->axi_rd_osr_lmt << XGMAC_RD_OSR_LMT_SHIFT) &
		XGMAC_RD_OSR_LMT;

	value &= ~XGMAC_BLEN;
	for (i = 0; i < AXI_BLEN; i++) {
		if (axi->axi_blen[i])
			value &= ~XGMAC_UNDEF;

		switch (axi->axi_blen[i]) {
		case 256:
			value |= XGMAC_BLEN256;
			break;
		case 128:
			value |= XGMAC_BLEN128;
			break;
		case 64:
			value |= XGMAC_BLEN64;
			break;
		case 32:
			value |= XGMAC_BLEN32;
			break;
		case 16:
			value |= XGMAC_BLEN16;
			break;
		case 8:
			value |= XGMAC_BLEN8;
			break;
		case 4:
			value |= XGMAC_BLEN4;
			break;
		}
	}

	writel(value, ioaddr + XGMAC_DMA_SYSBUS_MODE);
}

static void dwxgmac2_dma_rx_mode(void __iomem *ioaddr, int mode,
				 u32 channel, int fifosz, u8 qmode)
{
	u32 value = readl(ioaddr + XGMAC_MTL_RXQ_OPMODE(channel));
	unsigned int rqs = fifosz / 256 - 1;

	if (mode == SF_DMA_MODE) {
		value |= XGMAC_RSF;
	} else {
		value &= ~XGMAC_RSF;
		value &= ~XGMAC_RTC;

		if (mode <= 64)
			value |= 0x0 << XGMAC_RTC_SHIFT;
		else if (mode <= 96)
			value |= 0x2 << XGMAC_RTC_SHIFT;
		else
			value |= 0x3 << XGMAC_RTC_SHIFT;
	}

	value &= ~XGMAC_RQS;
	value |= (rqs << XGMAC_RQS_SHIFT) & XGMAC_RQS;

	writel(value, ioaddr + XGMAC_MTL_RXQ_OPMODE(channel));

	/* Enable MTL RX overflow */
	value = readl(ioaddr + XGMAC_MTL_QINTEN(channel));
	writel(value | XGMAC_RXOIE, ioaddr + XGMAC_MTL_QINTEN(channel));
}

static void dwxgmac2_dma_tx_mode(void __iomem *ioaddr, int mode,
				 u32 channel, int fifosz, u8 qmode)
{
	u32 value = readl(ioaddr + XGMAC_MTL_TXQ_OPMODE(channel));
	unsigned int tqs = fifosz / 256 - 1;

	if (mode == SF_DMA_MODE) {
		value |= XGMAC_TSF;
	} else {
		value &= ~XGMAC_TSF;
		value &= ~XGMAC_TTC;

		if (mode <= 64)
			value |= 0x0 << XGMAC_TTC_SHIFT;
		else if (mode <= 96)
			value |= 0x2 << XGMAC_TTC_SHIFT;
		else if (mode <= 128)
			value |= 0x3 << XGMAC_TTC_SHIFT;
		else if (mode <= 192)
			value |= 0x4 << XGMAC_TTC_SHIFT;
		else if (mode <= 256)
			value |= 0x5 << XGMAC_TTC_SHIFT;
		else if (mode <= 384)
			value |= 0x6 << XGMAC_TTC_SHIFT;
		else
			value |= 0x7 << XGMAC_TTC_SHIFT;
	}

	/* Use static TC to Queue mapping */
	value |= (channel << XGMAC_Q2TCMAP_SHIFT) & XGMAC_Q2TCMAP;

	value &= ~XGMAC_TXQEN;
	if (qmode != MTL_QUEUE_AVB)
		value |= 0x2 << XGMAC_TXQEN_SHIFT;
	else
		value |= 0x1 << XGMAC_TXQEN_SHIFT;

	value &= ~XGMAC_TQS;
	value |= (tqs << XGMAC_TQS_SHIFT) & XGMAC_TQS;

	writel(value, ioaddr +  XGMAC_MTL_TXQ_OPMODE(channel));
}

static void dwxgmac2_enable_dma_irq(void __iomem *ioaddr, u32 chan)
{
	writel(XGMAC_DMA_INT_DEFAULT_EN, ioaddr + XGMAC_DMA_CH_INT_EN(chan));
}

static void dwxgmac2_disable_dma_irq(void __iomem *ioaddr, u32 chan)
{
	writel(0, ioaddr + XGMAC_DMA_CH_INT_EN(chan));
}

static void dwxgmac2_dma_start_tx(void __iomem *ioaddr, u32 chan)
{
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));
	value |= XGMAC_TXST;
	writel(value, ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));

	value = readl(ioaddr + XGMAC_TX_CONFIG);
	value |= XGMAC_CONFIG_TE;
	writel(value, ioaddr + XGMAC_TX_CONFIG);
}

static void dwxgmac2_dma_stop_tx(void __iomem *ioaddr, u32 chan)
{
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));
	value &= ~XGMAC_TXST;
	writel(value, ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));

	value = readl(ioaddr + XGMAC_TX_CONFIG);
	value &= ~XGMAC_CONFIG_TE;
	writel(value, ioaddr + XGMAC_TX_CONFIG);
}

static void dwxgmac2_dma_start_rx(void __iomem *ioaddr, u32 chan)
{
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));
	value |= XGMAC_RXST;
	writel(value, ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));

	value = readl(ioaddr + XGMAC_RX_CONFIG);
	value |= XGMAC_CONFIG_RE;
	writel(value, ioaddr + XGMAC_RX_CONFIG);
}

static void dwxgmac2_dma_stop_rx(void __iomem *ioaddr, u32 chan)
{
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));
	value &= ~XGMAC_RXST;
	writel(value, ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));

	value = readl(ioaddr + XGMAC_RX_CONFIG);
	value &= ~XGMAC_CONFIG_RE;
	writel(value, ioaddr + XGMAC_RX_CONFIG);
}

static int dwxgmac2_dma_interrupt(void __iomem *ioaddr,
				  struct stmmac_extra_stats *x, u32 chan)
{
	u32 intr_status = readl(ioaddr + XGMAC_DMA_CH_STATUS(chan));
	int ret = 0;

	/* ABNORMAL interrupts */
	if (unlikely(intr_status & XGMAC_AIS)) {
		if (unlikely(intr_status & XGMAC_TPS)) {
			x->tx_process_stopped_irq++;
			ret |= tx_hard_error;
		}
		if (unlikely(intr_status & XGMAC_FBE)) {
			x->fatal_bus_error_irq++;
			ret |= tx_hard_error;
		}
	}

	/* TX/RX NORMAL interrupts */
	if (likely(intr_status & XGMAC_NIS)) {
		x->normal_irq_n++;

		if (likely(intr_status & XGMAC_RI)) {
			u32 value = readl(ioaddr + XGMAC_DMA_CH_INT_EN(chan));
			if (likely(value & XGMAC_RIE)) {
				x->rx_normal_irq_n++;
				ret |= handle_rx;
			}
		}
		if (likely(intr_status & XGMAC_TI)) {
			x->tx_normal_irq_n++;
			ret |= handle_tx;
		}
	}

	/* Clear interrupts */
	writel(~0x0, ioaddr + XGMAC_DMA_CH_STATUS(chan));

	return ret;
}

static void dwxgmac2_get_hw_feature(void __iomem *ioaddr,
				    struct dma_features *dma_cap)
{
	u32 hw_cap;

	/*  MAC HW feature 0 */
	hw_cap = readl(ioaddr + XGMAC_HW_FEATURE0);
	dma_cap->rx_coe = (hw_cap & XGMAC_HWFEAT_RXCOESEL) >> 16;
	dma_cap->tx_coe = (hw_cap & XGMAC_HWFEAT_TXCOESEL) >> 14;
	dma_cap->atime_stamp = (hw_cap & XGMAC_HWFEAT_TSSEL) >> 12;
	dma_cap->av = (hw_cap & XGMAC_HWFEAT_AVSEL) >> 11;
	dma_cap->av &= (hw_cap & XGMAC_HWFEAT_RAVSEL) >> 10;
	dma_cap->pmt_magic_frame = (hw_cap & XGMAC_HWFEAT_MGKSEL) >> 7;
	dma_cap->pmt_remote_wake_up = (hw_cap & XGMAC_HWFEAT_RWKSEL) >> 6;
	dma_cap->mbps_1000 = (hw_cap & XGMAC_HWFEAT_GMIISEL) >> 1;

	/* MAC HW feature 1 */
	hw_cap = readl(ioaddr + XGMAC_HW_FEATURE1);
	dma_cap->tsoen = (hw_cap & XGMAC_HWFEAT_TSOEN) >> 18;
	dma_cap->tx_fifo_size =
		128 << ((hw_cap & XGMAC_HWFEAT_TXFIFOSIZE) >> 6);
	dma_cap->rx_fifo_size =
		128 << ((hw_cap & XGMAC_HWFEAT_RXFIFOSIZE) >> 0);

	/* MAC HW feature 2 */
	hw_cap = readl(ioaddr + XGMAC_HW_FEATURE2);
	dma_cap->pps_out_num = (hw_cap & XGMAC_HWFEAT_PPSOUTNUM) >> 24;
	dma_cap->number_tx_channel =
		((hw_cap & XGMAC_HWFEAT_TXCHCNT) >> 18) + 1;
	dma_cap->number_rx_channel =
		((hw_cap & XGMAC_HWFEAT_RXCHCNT) >> 12) + 1;
	dma_cap->number_tx_queues =
		((hw_cap & XGMAC_HWFEAT_TXQCNT) >> 6) + 1;
	dma_cap->number_rx_queues =
		((hw_cap & XGMAC_HWFEAT_RXQCNT) >> 0) + 1;
}

static void dwxgmac2_rx_watchdog(void __iomem *ioaddr, u32 riwt, u32 nchan)
{
	u32 i;

	for (i = 0; i < nchan; i++)
		writel(riwt & XGMAC_RWT, ioaddr + XGMAC_DMA_CH_Rx_WATCHDOG(i));
}

static void dwxgmac2_set_rx_ring_len(void __iomem *ioaddr, u32 len, u32 chan)
{
	writel(len, ioaddr + XGMAC_DMA_CH_RxDESC_RING_LEN(chan));
}

static void dwxgmac2_set_tx_ring_len(void __iomem *ioaddr, u32 len, u32 chan)
{
	writel(len, ioaddr + XGMAC_DMA_CH_TxDESC_RING_LEN(chan));
}

static void dwxgmac2_set_rx_tail_ptr(void __iomem *ioaddr, u32 ptr, u32 chan)
{
	writel(ptr, ioaddr + XGMAC_DMA_CH_RxDESC_TAIL_LPTR(chan));
}

static void dwxgmac2_set_tx_tail_ptr(void __iomem *ioaddr, u32 ptr, u32 chan)
{
	writel(ptr, ioaddr + XGMAC_DMA_CH_TxDESC_TAIL_LPTR(chan));
}

static void dwxgmac2_enable_tso(void __iomem *ioaddr, bool en, u32 chan)
{
	u32 value = readl(ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));

	if (en)
		value |= XGMAC_TSE;
	else
		value &= ~XGMAC_TSE;

	writel(value, ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));
}

static void dwxgmac2_qmode(void __iomem *ioaddr, u32 channel, u8 qmode)
{
	u32 value = readl(ioaddr + XGMAC_MTL_TXQ_OPMODE(channel));

	value &= ~XGMAC_TXQEN;
	if (qmode != MTL_QUEUE_AVB) {
		value |= 0x2 << XGMAC_TXQEN_SHIFT;
		writel(0, ioaddr + XGMAC_MTL_TCx_ETS_CONTROL(channel));
	} else {
		value |= 0x1 << XGMAC_TXQEN_SHIFT;
	}

	writel(value, ioaddr +  XGMAC_MTL_TXQ_OPMODE(channel));
}

static void dwxgmac2_set_bfsize(void __iomem *ioaddr, int bfsize, u32 chan)
{
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));
	value |= bfsize << 1;
	writel(value, ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));
}

const struct stmmac_dma_ops dwxgmac210_dma_ops = {
	.reset = dwxgmac2_dma_reset,
	.init = dwxgmac2_dma_init,
	.init_chan = dwxgmac2_dma_init_chan,
	.init_rx_chan = dwxgmac2_dma_init_rx_chan,
	.init_tx_chan = dwxgmac2_dma_init_tx_chan,
	.axi = dwxgmac2_dma_axi,
	.dump_regs = NULL,
	.dma_rx_mode = dwxgmac2_dma_rx_mode,
	.dma_tx_mode = dwxgmac2_dma_tx_mode,
	.enable_dma_irq = dwxgmac2_enable_dma_irq,
	.disable_dma_irq = dwxgmac2_disable_dma_irq,
	.start_tx = dwxgmac2_dma_start_tx,
	.stop_tx = dwxgmac2_dma_stop_tx,
	.start_rx = dwxgmac2_dma_start_rx,
	.stop_rx = dwxgmac2_dma_stop_rx,
	.dma_interrupt = dwxgmac2_dma_interrupt,
	.get_hw_feature = dwxgmac2_get_hw_feature,
	.rx_watchdog = dwxgmac2_rx_watchdog,
	.set_rx_ring_len = dwxgmac2_set_rx_ring_len,
	.set_tx_ring_len = dwxgmac2_set_tx_ring_len,
	.set_rx_tail_ptr = dwxgmac2_set_rx_tail_ptr,
	.set_tx_tail_ptr = dwxgmac2_set_tx_tail_ptr,
	.enable_tso = dwxgmac2_enable_tso,
	.qmode = dwxgmac2_qmode,
	.set_bfsize = dwxgmac2_set_bfsize,
};
