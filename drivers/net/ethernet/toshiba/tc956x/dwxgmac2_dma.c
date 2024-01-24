/*
 * TC956X ethernet driver.
 *
 * dwxgmac2_dma.c
 *
 * Copyright (C) 2018 Synopsys, Inc. and/or its affiliates.
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Synopsys Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *  15 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported without module parameter
 *  VERSION     : 01-00-02
 *  20 Jul 2021 : 1. Debug prints removed
 *  VERSION     : 01-00-03
 *  23 Sep 2021 : 1. Updating RX Queue Threshold Limits for Flow control
 *  		  Threshold Limit for Activating Flow control 
 *  		  Threshold Limit for Deactivating Flow control 
 *  VERSION     : 01-00-14
 *  08 Dec 2021 : 1. Added module parameter for Flow control thresholds per Queue
 *  VERSION     : 01-00-30
 *  02 Feb 2022 : 1. Tx Queue flushed and checked for status after Tx DMA stop
 *  VERSION     : 01-00-40
 *  29 Apr 2022 : 1. Checking for DMA status update as stop after TX DMA stop
 *  		  2. Checking for Tx MTL Queue Read/Write contollers in idle state after TX DMA stop
 *  VERSION     : 01-00-51
 *  26 Dec 2023 : 1. Kernel 6.6 Porting changes
 *  VERSION     : 01-03-59
 *
 */

#include <linux/iopoll.h>
#include "tc956xmac.h"
#include "dwxgmac2.h"

static int dwxgmac2_dma_reset(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + XGMAC_DMA_MODE);

	/* DMA SW reset */
	writel(value | XGMAC_SWR, ioaddr + XGMAC_DMA_MODE);

	return readl_poll_timeout(ioaddr + XGMAC_DMA_MODE, value,
				  !(value & XGMAC_SWR), 0, 100000);
}

static void dwxgmac2_dma_init(struct tc956xmac_priv *priv, void __iomem *ioaddr,
			      struct tc956xmac_dma_cfg *dma_cfg, int atds)
{
	u32 value = readl(ioaddr + XGMAC_DMA_SYSBUS_MODE);

	if (dma_cfg->aal)
		value |= XGMAC_AAL;

	if (dma_cfg->eame)
		value |= XGMAC_EAME;

	writel(value, ioaddr + XGMAC_DMA_SYSBUS_MODE);

	value = readl(ioaddr + XGMAC_DMA_MODE);
	/* Due to the erratum in XGMAC 3.01a,  DSPW=0, OWRQ=3 needs to be set */
	value &= ~XGMAC_DSPW;
	value |= XGMAC_DMA_MODE_INTM;
	writel(value, ioaddr + XGMAC_DMA_MODE);

}

static void dwxgmac2_dma_init_chan(struct tc956xmac_priv *priv, void __iomem *ioaddr,
				   struct tc956xmac_dma_cfg *dma_cfg, u32 chan)
{
	u32 value = readl(ioaddr + XGMAC_DMA_CH_CONTROL(chan));

	if (dma_cfg->pblx8)
		value |= XGMAC_PBLx8;

	writel(value, ioaddr + XGMAC_DMA_CH_CONTROL(chan));
	writel(XGMAC_DMA_INT_DEFAULT_EN, ioaddr + XGMAC_DMA_CH_INT_EN(chan));
}

static void dwxgmac2_dma_init_rx_chan(struct tc956xmac_priv *priv,
					void __iomem *ioaddr,
					struct tc956xmac_dma_cfg *dma_cfg,
					dma_addr_t phy, u32 chan)
{
	u32 rxpbl = dma_cfg->rxpbl ?: dma_cfg->pbl;
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));
	value &= ~XGMAC_RxPBL;
	value |= (rxpbl << XGMAC_RxPBL_SHIFT) & XGMAC_RxPBL;
	writel(value, ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));

	/* Due to the erratum in XGMAC 3.01a,  DSPW=0, OWRQ=3 needs to be set */
	value = readl(ioaddr + XGMAC_DMA_CH_RX_CONTROL2(chan));
	value &= ~XGMAC_OWRQ;
	value |= (3 << XGMAC_OWRQ_SHIFT);
	writel(value, ioaddr + XGMAC_DMA_CH_RX_CONTROL2(chan));

	if (likely(dma_cfg->eame))
#ifdef TC956X
		writel(TC956X_HOST_PHYSICAL_ADRS_MASK | (upper_32_bits(phy) & 0xF),
	       ioaddr + XGMAC_DMA_CH_RxDESC_HADDR(chan));
#else
		writel(upper_32_bits(phy), ioaddr + XGMAC_DMA_CH_RxDESC_HADDR(chan));
#endif
	writel(lower_32_bits(phy), ioaddr + XGMAC_DMA_CH_RxDESC_LADDR(chan));
}

static void dwxgmac2_dma_init_tx_chan(struct tc956xmac_priv *priv,
					void __iomem *ioaddr,
					struct tc956xmac_dma_cfg *dma_cfg,
					dma_addr_t phy, u32 chan)
{
	u32 txpbl = dma_cfg->txpbl ?: dma_cfg->pbl;
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));
	value &= ~XGMAC_TxPBL;
	value |= (txpbl << XGMAC_TxPBL_SHIFT) & XGMAC_TxPBL;
	value |= XGMAC_OSP;
	writel(value, ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));

	if (likely(dma_cfg->eame))
#ifdef TC956X
		writel(TC956X_HOST_PHYSICAL_ADRS_MASK |
			(upper_32_bits(phy) & 0xF),
			ioaddr + XGMAC_DMA_CH_TxDESC_HADDR(chan));
#else
		writel(upper_32_bits(phy), ioaddr +
			XGMAC_DMA_CH_TxDESC_HADDR(chan));
#endif
	writel(lower_32_bits(phy), ioaddr + XGMAC_DMA_CH_TxDESC_LADDR(chan));
}

static void dwxgmac2_dma_axi(struct tc956xmac_priv *priv, void __iomem *ioaddr,
					struct tc956xmac_axi *axi)
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

	if (!axi->axi_fb)
		value |= XGMAC_UNDEF;

	value &= ~XGMAC_BLEN;
	for (i = 0; i < AXI_BLEN; i++) {
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
		netdev_dbg(priv->dev, "%s: Burst length supported = %d",
				__func__, axi->axi_blen[i]);
	}

	writel(value, ioaddr + XGMAC_DMA_SYSBUS_MODE);
	writel(0, ioaddr + XGMAC_TX_EDMA_CTRL);
	writel(XGMAC_RDPS, ioaddr + XGMAC_RX_EDMA_CTRL);
}

static void dwxgmac2_dma_dump_regs(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, u32 *reg_space)
{
	int i;

	for (i = ETH_DMA_DUMP_OFFSET1; i <= ETH_DMA_DUMP_OFFSET1_END; i++) {
		reg_space[i] = readl(ioaddr + MAC_OFFSET + (4 * i));
		KPRINT_DEBUG1("%04x : %08x\n", i*4, reg_space[i]);
	}

	for (i = ETH_DMA_DUMP_OFFSET2; i < XGMAC_REGSIZE; i++) {
		reg_space[i] = readl(ioaddr + MAC_OFFSET + (4 * i));
		KPRINT_DEBUG1("%04x : %08x\n", i*4, reg_space[i]);
	}

	KPRINT_DEBUG1("**********************************************************************************");
}

/**
 * dwxgmac2_dma_rx_mode - Configure Rx MTL registers.
 *
 * @ioaddr: Device SFR base address
 * @mode: MTL operating mode
 * @channel: Rx channel number
 * @fifosz: MTL FIFO size
 * @qmode: Queue mode
 */
static void dwxgmac2_dma_rx_mode(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, int mode,
					u32 channel, int fifosz, u8 qmode)
{
	u32 value = readl(ioaddr + XGMAC_MTL_RXQ_OPMODE(channel));
	unsigned int rqs = 0;

	if (fifosz != 0)
		rqs = fifosz / 256 - 1;


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

	if ((fifosz >= 4096) && (qmode != MTL_QUEUE_AVB)) {
		u32 flow = readl(ioaddr + XGMAC_MTL_RXQ_FLOW_CONTROL(channel));
		unsigned int rfd, rfa;

		value |= XGMAC_EHFC;

		rfd = priv->plat->rx_queues_cfg[channel].rfd;
		rfa = priv->plat->rx_queues_cfg[channel].rfa;

		flow &= ~XGMAC_RFD;
		flow |= rfd << XGMAC_RFD_SHIFT;

		flow &= ~XGMAC_RFA;
		flow |= rfa << XGMAC_RFA_SHIFT;

		writel(flow, ioaddr + XGMAC_MTL_RXQ_FLOW_CONTROL(channel));
	}

	writel(value, ioaddr + XGMAC_MTL_RXQ_OPMODE(channel));

	/* Enable MTL RX overflow */
	value = readl(ioaddr + XGMAC_MTL_QINTEN(channel));
	writel(value | XGMAC_RXOIE, ioaddr + XGMAC_MTL_QINTEN(channel));
}

static void dwxgmac2_dma_tx_mode(struct tc956xmac_priv *priv,
				 void __iomem *ioaddr, int mode,
				 u32 channel, int fifosz, u8 qmode)
{
	u32 value = readl(ioaddr + XGMAC_MTL_TXQ_OPMODE(channel));
	unsigned int tqs = 0;
	u8 traffic_class = 0;

	if (fifosz != 0)
		tqs = fifosz / 256 - 1;

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

	traffic_class = priv->plat->tx_queues_cfg[channel].traffic_class;
	value |= (traffic_class << XGMAC_Q2TCMAP_SHIFT) & XGMAC_Q2TCMAP;

	value &= ~XGMAC_TXQEN;
	if (qmode != MTL_QUEUE_AVB)
		value |= 0x2 << XGMAC_TXQEN_SHIFT;
	else
		value |= 0x1 << XGMAC_TXQEN_SHIFT;

	value &= ~XGMAC_TQS;
	value |= (tqs << XGMAC_TQS_SHIFT) & XGMAC_TQS;

	writel(value, ioaddr +  XGMAC_MTL_TXQ_OPMODE(channel));
}

static void dwxgmac2_enable_dma_irq(struct tc956xmac_priv *priv,
				    void __iomem *ioaddr, u32 chan,
				    bool rx, bool tx)
{
	u32 value = readl(ioaddr + XGMAC_DMA_CH_INT_EN(chan));

	if (rx)
		value |= XGMAC_DMA_INT_DEFAULT_RX;
	if (tx)
		value |= XGMAC_DMA_INT_DEFAULT_TX;

	writel(value, ioaddr + XGMAC_DMA_CH_INT_EN(chan));
}

static void dwxgmac2_disable_dma_irq(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, u32 chan,
					bool rx, bool tx)
{
	u32 value = readl(ioaddr + XGMAC_DMA_CH_INT_EN(chan));

	if (rx)
		value &= ~XGMAC_DMA_INT_DEFAULT_RX;
	if (tx)
		value &= ~XGMAC_DMA_INT_DEFAULT_TX;

	writel(value, ioaddr + XGMAC_DMA_CH_INT_EN(chan));
}

static void dwxgmac2_dma_start_tx(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, u32 chan)
{
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));
	value |= XGMAC_TXST;
	writel(value, ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));

	/* TE set will enable all the MAC transmisster, PF to configure when
	 *  starting its channel for tranmission
	 */
#ifndef TC956X_SRIOV_VF
#ifndef TC956X_DMA_OFFLOAD_ENABLE
	value = readl(ioaddr + XGMAC_TX_CONFIG);
	value |= XGMAC_CONFIG_TE;
	writel(value, ioaddr + XGMAC_TX_CONFIG);
#endif
#endif
}

static void dwxgmac2_dma_stop_tx(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, u32 chan)
{
	u32 value;
	int limit;

	value = readl(ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));
	value &= ~XGMAC_TXST;
	writel(value, ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));

	/* TE reset will disable the MAC transmisster, it is possible that
	 * other MAC channels are used by other VF/PF. So donot configure this
	 */

	/*Check whether Tx DMA is in stop state */
	limit = 10000;
	while (limit--) {
		if ((readl(ioaddr + XGMAC_DMA_CH_STATUS(chan)) & XGMAC_TPS))
			break;
		udelay(1);
	}
	if (limit == -1)
		KPRINT_ERR("Tx DMA (%d) is not in stop state\n",chan);

	DBGPR_FUNC(priv->device, "%s DMA chnl status : 0x%x, chnl : %d, limit [%d]\n", __func__, readl(ioaddr + XGMAC_DMA_CH_STATUS(chan)),chan, limit);

	/*Check whether MTL Tx Read/Write controller is in Idle state */
	limit = 10000;
	while (limit--) {
		if (!(readl(ioaddr + XGMAC_MTL_TXQ_Debug(chan)) & (XGMAC_MTL_DEBUG_TWCSTS | 
			XGMAC_MTL_DEBUG_TRCSTS_MASK)))
			break;
		udelay(1);
	}
	if (limit == -1)
		KPRINT_ERR("MTL Tx Read/Write controller (%d) is not in idle state\n",chan);

	DBGPR_FUNC(priv->device, "%s MTL TXQ status : 0x%x, chnl : %d, limit [%d]\n", __func__, readl(ioaddr + XGMAC_MTL_TXQ_Debug(chan)),chan, limit);

	/* Flush the Tx Queue */
	value = readl(ioaddr + XGMAC_MTL_TXQ_OPMODE(chan));
	value |= XGMAC_FTQ;
	writel(value, ioaddr +  XGMAC_MTL_TXQ_OPMODE(chan));

	/*Check the TxQ empty status with timeout of 10ms*/
	limit = 10000;
	while (limit--) {
		if (!(readl(ioaddr + XGMAC_MTL_TXQ_Debug(chan)) & XGMAC_MTL_DEBUG_TXQSTS))
			break;
		udelay(1);
	}
	if (limit == -1)
		KPRINT_ERR("Tx Queue did not get time to empty after flush operation\n");

	DBGPR_FUNC(priv->device, "%s MTL TXQ status after flush: 0x%x, limit [%d]\n", __func__, readl(ioaddr + XGMAC_MTL_TXQ_Debug(chan)), limit);

#ifndef TC956X_SRIOV_VF
#ifndef TC956X_DMA_OFFLOAD_ENABLE
	value = readl(ioaddr + XGMAC_TX_CONFIG);
	value &= ~XGMAC_CONFIG_TE;
	writel(value, ioaddr + XGMAC_TX_CONFIG);
#endif
#endif
}

static void dwxgmac2_dma_start_rx(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, u32 chan)
{
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));
	value |= XGMAC_RXST;
	writel(value, ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));

	/* RE set will enable all the MAC receiver, PF to configure when
	 *  starting its channel for reception
	 */
#ifndef TC956X_SRIOV_VF
#ifndef TC956X_DMA_OFFLOAD_ENABLE
	value = readl(ioaddr + XGMAC_RX_CONFIG);
	value |= XGMAC_CONFIG_RE;
	writel(value, ioaddr + XGMAC_RX_CONFIG);
#endif
#endif
}

static void dwxgmac2_dma_stop_rx(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, u32 chan)
{
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));
	value &= ~XGMAC_RXST;
	writel(value, ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));

#ifndef TC956X_SRIOV_VF
#ifndef TC956X_DMA_OFFLOAD_ENABLE
	value = readl(ioaddr + XGMAC_RX_CONFIG);
	value &= ~XGMAC_CONFIG_RE;
	writel(value, ioaddr + XGMAC_RX_CONFIG);
#endif
#endif
}

static int dwxgmac2_dma_interrupt(struct tc956xmac_priv *priv, void __iomem *ioaddr,
				  struct tc956xmac_extra_stats *x, u32 chan)
{
	u32 intr_status = readl(ioaddr + XGMAC_DMA_CH_STATUS(chan));
	u32 intr_en = readl(ioaddr + XGMAC_DMA_CH_INT_EN(chan));
	int ret = 0;

	/* ABNORMAL interrupts */
	if (unlikely(intr_status & XGMAC_AIS)) {
		if (unlikely(intr_status & XGMAC_RBU)) {
			x->rx_buf_unav_irq[chan]++;
			ret |= handle_rx;
		}
		if (unlikely(intr_status & XGMAC_TPS)) {
			x->tx_process_stopped_irq[chan]++;
			ret |= tx_hard_error;
		}
		if (unlikely(intr_status & XGMAC_FBE)) {
			x->fatal_bus_error_irq[chan]++;
			ret |= tx_hard_error;
		}
	}

	/* TX/RX NORMAL interrupts */
//	if (likely(intr_status & XGMAC_NIS)) {
	if (1) {
		x->normal_irq_n[chan]++;

		if (likely(intr_status & XGMAC_RI)) {
			x->rx_normal_irq_n[chan]++;
			ret |= handle_rx;
		}
		if (likely(intr_status & (XGMAC_TI | XGMAC_TBU))) {
			x->tx_normal_irq_n[chan]++;
			ret |= handle_tx;
		}
	}

	/* Clear interrupts */
	writel(intr_en & intr_status, ioaddr + XGMAC_DMA_CH_STATUS(chan));

	return ret;
}

static void dwxgmac2_get_hw_feature(struct tc956xmac_priv *priv,
					void __iomem *ioaddr,
					struct dma_features *dma_cap)
{
	u32 hw_cap;

	/*  MAC HW feature 0 */
	hw_cap = readl(ioaddr + XGMAC_HW_FEATURE0);
	dma_cap->vlins = (hw_cap & XGMAC_HWFEAT_SAVLANINS) >> 27;
	dma_cap->rx_coe = (hw_cap & XGMAC_HWFEAT_RXCOESEL) >> 16;
	dma_cap->tx_coe = (hw_cap & XGMAC_HWFEAT_TXCOESEL) >> 14;
	dma_cap->eee = (hw_cap & XGMAC_HWFEAT_EEESEL) >> 13;
	dma_cap->atime_stamp = (hw_cap & XGMAC_HWFEAT_TSSEL) >> 12;
	dma_cap->av = (hw_cap & XGMAC_HWFEAT_AVSEL) >> 11;
	dma_cap->av &= !((hw_cap & XGMAC_HWFEAT_RAVSEL) >> 10);
	dma_cap->arpoffsel = (hw_cap & XGMAC_HWFEAT_ARPOFFSEL) >> 9;
	dma_cap->rmon = (hw_cap & XGMAC_HWFEAT_MMCSEL) >> 8;
	dma_cap->pmt_magic_frame = (hw_cap & XGMAC_HWFEAT_MGKSEL) >> 7;
	dma_cap->pmt_remote_wake_up = (hw_cap & XGMAC_HWFEAT_RWKSEL) >> 6;
#ifdef TC956X_WITHOUT_MDIO
	dma_cap->sma_mdio = (hw_cap & XGMAC_HWFEAT_SMASEL) >> 5;
#endif
	dma_cap->vlhash = (hw_cap & XGMAC_HWFEAT_VLHASH) >> 4;
	dma_cap->mbps_1000 = (hw_cap & XGMAC_HWFEAT_GMIISEL) >> 1;

	/* MAC HW feature 1 */
	hw_cap = readl(ioaddr + XGMAC_HW_FEATURE1);
	dma_cap->l3l4fnum = (hw_cap & XGMAC_HWFEAT_L3L4FNUM) >> 27;
	dma_cap->hash_tb_sz = (hw_cap & XGMAC_HWFEAT_HASHTBLSZ) >> 24;
	dma_cap->rssen = (hw_cap & XGMAC_HWFEAT_RSSEN) >> 20;
	dma_cap->tsoen = (hw_cap & XGMAC_HWFEAT_TSOEN) >> 18;
	dma_cap->sphen = (hw_cap & XGMAC_HWFEAT_SPHEN) >> 17;
	dma_cap->ptoen = (hw_cap & XGMAC_HWFEAT_PTOEN) >> 12;
	dma_cap->osten = (hw_cap & XGMAC_HWFEAT_OSTEN) >> 11;

	dma_cap->addr64 = (hw_cap & XGMAC_HWFEAT_ADDR64) >> 14;
	switch (dma_cap->addr64) {
	case 0:
		dma_cap->addr64 = 32;
		break;
	case 1:
		dma_cap->addr64 = 40;
		break;
	case 2:
		dma_cap->addr64 = 48;
		break;
	default:
		dma_cap->addr64 = 32;
		break;
	}

	if (IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT))
		NMSGPR_INFO(priv->device, "64 bit platform\n");
	else {
		NMSGPR_INFO(priv->device, "32 bit platform\n");
		dma_cap->addr64 = 32;
	}

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

	/* MAC HW feature 3 */
	hw_cap = readl(ioaddr + XGMAC_HW_FEATURE3);
	dma_cap->tbssel = (hw_cap & XGMAC_HWFEAT_TBSSEL) >> 27;
	dma_cap->fpesel = (hw_cap & XGMAC_HWFEAT_FPESEL) >> 26;
	dma_cap->estwid = (hw_cap & XGMAC_HWFEAT_ESTWID) >> 23;
	dma_cap->estdep = (hw_cap & XGMAC_HWFEAT_ESTDEP) >> 20;
	dma_cap->estsel = (hw_cap & XGMAC_HWFEAT_ESTSEL) >> 19;
	dma_cap->asp = (hw_cap & XGMAC_HWFEAT_ASP) >> 14;
	dma_cap->dvlan = (hw_cap & XGMAC_HWFEAT_DVLAN) >> 13;
	dma_cap->frpes = (hw_cap & XGMAC_HWFEAT_FRPES) >> 11;
	dma_cap->frpbs = (hw_cap & XGMAC_HWFEAT_FRPPB) >> 9;
#ifndef TC956X_SRIOV_VF
	dma_cap->frpsel = (hw_cap & XGMAC_HWFEAT_FRPSEL) >> 3;
#elif (defined TC956X_SRIOV_VF)
	dma_cap->frpsel = 0; /* VF to not support FRP */
#endif
	switch (dma_cap->frpes) {
	default:
		dma_cap->frpes = 0;
		break;
	case 0x0:
		dma_cap->frpes = 64;
		break;
	case 0x1:
		dma_cap->frpes = 128;
		break;
	case 0x2:
		dma_cap->frpes = 256;
		break;
	}
#ifdef TC956X_WITHOUT_MDIO
	if (priv->plat->interface == PHY_INTERFACE_MODE_RGMII)
		dma_cap->sma_mdio = 0;
#endif
}

static void dwxgmac2_rx_watchdog(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, u32 riwt, u32 nchan)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
#ifdef TC956X_SRIOV_PF
	if (priv->plat->rx_ch_in_use[nchan] == TC956X_DISABLE_CHNL)
		return;
#elif (defined TC956X_SRIOV_VF)
	if (priv->plat->ch_in_use[nchan] == NOT_USED)
		return;
#else
	if (priv->plat->rx_dma_ch_owner[nchan] != USE_IN_TC956X_SW)
		return;
#endif

	writel(riwt & XGMAC_RWT, ioaddr + XGMAC_DMA_CH_Rx_WATCHDOG(nchan));
#else
	u32 i;

	for (i = 0; i < nchan; i++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[i] == TC956X_DISABLE_CHNL)
			continue;
#elif (defined TC956X_SRIOV_VF)
		if (priv->plat->ch_in_use[i] == NOT_USED)
			continue;
#else
		if (priv->plat->rx_dma_ch_owner[i] != USE_IN_TC956X_SW)
			continue;
#endif
		writel(riwt & XGMAC_RWT, ioaddr + XGMAC_DMA_CH_Rx_WATCHDOG(i));
	}
#endif
}

static void dwxgmac2_set_rx_ring_len(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, u32 len, u32 chan)
{
	u32 val;

	val = readl(ioaddr + XGMAC_DMA_CH_RX_CONTROL2(chan));
	val &= ~XGMAC_RDRL;
	val |= (len << XGMAC_RDRL_SHIFT);
	writel(val, ioaddr + XGMAC_DMA_CH_RX_CONTROL2(chan));
}

static void dwxgmac2_set_tx_ring_len(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, u32 len, u32 chan)
{
	writel(len, ioaddr + XGMAC_DMA_CH_TX_CONTROL2(chan));
}

static void dwxgmac2_set_rx_tail_ptr(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, u32 ptr, u32 chan)
{
	writel(ptr, ioaddr + XGMAC_DMA_CH_RxDESC_TAIL_LPTR(chan));

}

static void dwxgmac2_set_tx_tail_ptr(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, u32 ptr, u32 chan)
{
	writel(ptr, ioaddr + XGMAC_DMA_CH_TxDESC_TAIL_LPTR(chan));
}

static void dwxgmac2_enable_tso(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, bool en, u32 chan)
{
	u32 value = readl(ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));

	if (en)
		value |= XGMAC_TSE;
	else
		value &= ~XGMAC_TSE;

	writel(value, ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static void dwxgmac2_qmode(struct tc956xmac_priv *priv, void __iomem *ioaddr,
				u32 channel, u8 qmode)
{
	u32 value = readl(ioaddr + XGMAC_MTL_TXQ_OPMODE(channel));
#ifndef TC956X_SRIOV_VF
	u32 flow = readl(ioaddr + XGMAC_RX_FLOW_CTRL);
#endif
	value &= ~XGMAC_TXQEN;
	if (qmode != MTL_QUEUE_AVB) {
		value |= 0x2 << XGMAC_TXQEN_SHIFT;
		//writel(0, ioaddr + XGMAC_MTL_TCx_ETS_CONTROL(channel));
	} else {
		value |= 0x1 << XGMAC_TXQEN_SHIFT;
		/* RFE configuration is handled in PF driver */
#ifndef TC956X_SRIOV_VF
		writel(flow & (~XGMAC_RFE), ioaddr + XGMAC_RX_FLOW_CTRL);
#endif
	}

	writel(value, ioaddr +  XGMAC_MTL_TXQ_OPMODE(channel));
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static void dwxgmac2_set_bfsize(struct tc956xmac_priv *priv,
				void __iomem *ioaddr, int bfsize, u32 chan)
{
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));
	value &= ~XGMAC_RBSZ;
	value |= bfsize << XGMAC_RBSZ_SHIFT;
	writel(value, ioaddr + XGMAC_DMA_CH_RX_CONTROL(chan));
}

static void dwxgmac2_enable_sph(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, bool en, u32 chan)
{
	u32 value = readl(ioaddr + XGMAC_RX_CONFIG);

	/* Common register configuration only done in PF driver */
#ifndef TC956X_SRIOV_VF
	value &= ~XGMAC_CONFIG_HDSMS;
	value |= XGMAC_CONFIG_HDSMS_256; /* Segment max 256 bytes */
	writel(value, ioaddr + XGMAC_RX_CONFIG);
#endif
	value = readl(ioaddr + XGMAC_DMA_CH_CONTROL(chan));
	if (en)
		value |= XGMAC_SPH;
	else
		value &= ~XGMAC_SPH;
	writel(value, ioaddr + XGMAC_DMA_CH_CONTROL(chan));
}

static int dwxgmac2_enable_tbs(struct tc956xmac_priv *priv, void __iomem *ioaddr,
					bool en, u32 chan)
{
	u32 value = readl(ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));

	if (en) {
		value &= ~XGMAC_TFSEL;
		value |= (XGMAC_EDSE) | (0 << XGMAC_TFSEL_SHIFT);
	} else {
		value &= ~XGMAC_EDSE;
	}
	writel(value, ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan));

	value = readl(ioaddr + XGMAC_DMA_CH_TX_CONTROL(chan)) & XGMAC_EDSE;

	if (en && !value)
		return -EIO;

	/* Below configuration done on common DMA registers
	 * so PF driver can configure this.
	 */
	writel((100 << XGMAC_FTOS_SHIFT) | (1 << XGMAC_FGOS_SHIFT) | XGMAC_FTOV,
		ioaddr + XGMAC_DMA_TBS_CTRL0);
	writel((100 << XGMAC_FTOS_SHIFT) | (1 << XGMAC_FGOS_SHIFT) | XGMAC_FTOV,
		ioaddr + XGMAC_DMA_TBS_CTRL1);
	writel((100 << XGMAC_FTOS_SHIFT) | (1 << XGMAC_FGOS_SHIFT) | XGMAC_FTOV,
		ioaddr + XGMAC_DMA_TBS_CTRL2);
	writel((100 << XGMAC_FTOS_SHIFT) | (1 << XGMAC_FGOS_SHIFT) | XGMAC_FTOV,
		ioaddr + XGMAC_DMA_TBS_CTRL3);
	return 0;
}

static void dwxgmac2_desc_stats(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 chno;

	for (chno = 0; chno < priv->plat->tx_queues_to_use; chno++) {
#ifdef TC956X_SRIOV_VF
		if (priv->plat->ch_in_use[chno] == 0)
			continue;
#endif
		priv->xstats.txch_status[chno] =
			readl(ioaddr + XGMAC_DMA_CH_STATUS(chno));
		priv->xstats.txch_control[chno] =
			readl(ioaddr + XGMAC_DMA_CH_TX_CONTROL(chno));
		priv->xstats.txch_desc_list_haddr[chno] =
			readl(ioaddr + XGMAC_DMA_CH_TxDESC_HADDR(chno));
		priv->xstats.txch_desc_list_laddr[chno] =
			readl(ioaddr + XGMAC_DMA_CH_TxDESC_LADDR(chno));
		priv->xstats.txch_desc_ring_len[chno] =
			readl(ioaddr + XGMAC_DMA_CH_TX_CONTROL2(chno)) & (XGMAC_TDRL);
		priv->xstats.txch_desc_curr_haddr[chno] =
			readl(ioaddr + XGMAC_DMA_CH_Cur_TxDESC_HADDR(chno));
		priv->xstats.txch_desc_curr_laddr[chno] =
			readl(ioaddr + XGMAC_DMA_CH_Cur_TxDESC_LADDR(chno));
		priv->xstats.txch_desc_tail[chno] =
			readl(ioaddr + XGMAC_DMA_CH_TxDESC_TAIL_LPTR(chno));
		priv->xstats.txch_desc_buf_haddr[chno] =
			readl(ioaddr + XGMAC_DMA_CH_Cur_TxBuff_HADDR(chno));
		priv->xstats.txch_desc_buf_laddr[chno] =
			readl(ioaddr + XGMAC_DMA_CH_Cur_TxBuff_LADDR(chno));
		priv->xstats.txch_sw_cur_tx[chno] = priv->tx_queue[chno].cur_tx;
		priv->xstats.txch_sw_dirty_tx[chno] = priv->tx_queue[chno].dirty_tx;
	}

	for (chno = 0; chno < priv->plat->rx_queues_to_use; chno++) {
#ifdef TC956X_SRIOV_VF
		if (priv->plat->ch_in_use[chno] == 0)
			continue;
#endif
		priv->xstats.rxch_status[chno] =
			readl(ioaddr + XGMAC_DMA_CH_STATUS(chno));
		priv->xstats.rxch_control[chno] =
			readl(ioaddr + XGMAC_DMA_CH_RX_CONTROL(chno));
		priv->xstats.rxch_desc_list_haddr[chno] =
			readl(ioaddr + XGMAC_DMA_CH_RxDESC_HADDR(chno));
		priv->xstats.rxch_desc_list_laddr[chno] =
			readl(ioaddr + XGMAC_DMA_CH_RxDESC_LADDR(chno));
		priv->xstats.rxch_desc_ring_len[chno] =
			readl(ioaddr + XGMAC_DMA_CH_RX_CONTROL2(chno)) & (XGMAC_RDRL);
		priv->xstats.rxch_desc_curr_haddr[chno] =
			readl(ioaddr + XGMAC_DMA_CH_Cur_RxDESC_HADDR(chno));
		priv->xstats.rxch_desc_curr_laddr[chno] =
			readl(ioaddr + XGMAC_DMA_CH_Cur_RxDESC_LADDR(chno));
		priv->xstats.rxch_desc_tail[chno] =
			readl(ioaddr + XGMAC_DMA_CH_RxDESC_TAIL_LPTR(chno));
		priv->xstats.rxch_desc_buf_haddr[chno] =
			readl(ioaddr + XGMAC_DMA_CH_Cur_RxBuff_HADDR(chno));
		priv->xstats.rxch_desc_buf_laddr[chno] =
			readl(ioaddr + XGMAC_DMA_CH_Cur_RxBuff_LADDR(chno));
		priv->xstats.rxch_sw_cur_rx[chno] = priv->rx_queue[chno].cur_rx;
		priv->xstats.rxch_sw_dirty_rx[chno] = priv->rx_queue[chno].dirty_rx;
	}
}

const struct tc956xmac_dma_ops dwxgmac210_dma_ops = {
	.reset = dwxgmac2_dma_reset,
	.init = dwxgmac2_dma_init,
	.init_chan = dwxgmac2_dma_init_chan,
	.init_rx_chan = dwxgmac2_dma_init_rx_chan,
	.init_tx_chan = dwxgmac2_dma_init_tx_chan,
	.axi = dwxgmac2_dma_axi,
	.dump_regs = dwxgmac2_dma_dump_regs,
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
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.qmode = dwxgmac2_qmode,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.set_bfsize = dwxgmac2_set_bfsize,
	.enable_sph = dwxgmac2_enable_sph,
	.enable_tbs = dwxgmac2_enable_tbs,
	.desc_stats = dwxgmac2_desc_stats,
};
