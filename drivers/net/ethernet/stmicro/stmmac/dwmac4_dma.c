// SPDX-License-Identifier: GPL-2.0-only
/*
 * This is the driver for the GMAC on-chip Ethernet controller for ST SoCs.
 * DWC Ether MAC version 4.xx  has been used for  developing this code.
 *
 * This contains the functions to handle the dma.
 *
 * Copyright (C) 2015  STMicroelectronics Ltd
 *
 * Author: Alexandre Torgue <alexandre.torgue@st.com>
 */

#include <linux/io.h>
#include "dwmac4.h"
#include "dwmac4_dma.h"

static void dwmac4_dma_axi(void __iomem *ioaddr, struct stmmac_axi *axi)
{
	u32 value = readl(ioaddr + DMA_SYS_BUS_MODE);
	int i;

	pr_info("dwmac4: Master AXI performs %s burst length\n",
		(value & DMA_SYS_BUS_FB) ? "fixed" : "any");

	if (axi->axi_lpi_en)
		value |= DMA_AXI_EN_LPI;
	if (axi->axi_xit_frm)
		value |= DMA_AXI_LPI_XIT_FRM;

	value &= ~DMA_AXI_WR_OSR_LMT;
	value |= (axi->axi_wr_osr_lmt & DMA_AXI_OSR_MAX) <<
		 DMA_AXI_WR_OSR_LMT_SHIFT;

	value &= ~DMA_AXI_RD_OSR_LMT;
	value |= (axi->axi_rd_osr_lmt & DMA_AXI_OSR_MAX) <<
		 DMA_AXI_RD_OSR_LMT_SHIFT;

	/* Depending on the UNDEF bit the Master AXI will perform any burst
	 * length according to the BLEN programmed (by default all BLEN are
	 * set).
	 */
	for (i = 0; i < AXI_BLEN; i++) {
		switch (axi->axi_blen[i]) {
		case 256:
			value |= DMA_AXI_BLEN256;
			break;
		case 128:
			value |= DMA_AXI_BLEN128;
			break;
		case 64:
			value |= DMA_AXI_BLEN64;
			break;
		case 32:
			value |= DMA_AXI_BLEN32;
			break;
		case 16:
			value |= DMA_AXI_BLEN16;
			break;
		case 8:
			value |= DMA_AXI_BLEN8;
			break;
		case 4:
			value |= DMA_AXI_BLEN4;
			break;
		}
	}

	writel(value, ioaddr + DMA_SYS_BUS_MODE);
}

static void dwmac4_dma_init_rx_chan(void __iomem *ioaddr,
				    struct stmmac_dma_cfg *dma_cfg,
				    dma_addr_t dma_rx_phy, u32 chan)
{
	u32 value;
	u32 rxpbl = dma_cfg->rxpbl ?: dma_cfg->pbl;

	value = readl(ioaddr + DMA_CHAN_RX_CONTROL(chan));
	value = value | (rxpbl << DMA_BUS_MODE_RPBL_SHIFT);
	writel(value, ioaddr + DMA_CHAN_RX_CONTROL(chan));

	if (IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT) && likely(dma_cfg->eame))
		writel(upper_32_bits(dma_rx_phy),
		       ioaddr + DMA_CHAN_RX_BASE_ADDR_HI(chan));

	writel(lower_32_bits(dma_rx_phy), ioaddr + DMA_CHAN_RX_BASE_ADDR(chan));
}

static void dwmac4_dma_init_tx_chan(void __iomem *ioaddr,
				    struct stmmac_dma_cfg *dma_cfg,
				    dma_addr_t dma_tx_phy, u32 chan)
{
	u32 value;
	u32 txpbl = dma_cfg->txpbl ?: dma_cfg->pbl;

	value = readl(ioaddr + DMA_CHAN_TX_CONTROL(chan));
	value = value | (txpbl << DMA_BUS_MODE_PBL_SHIFT);

	/* Enable OSP to get best performance */
	value |= DMA_CONTROL_OSP;

	writel(value, ioaddr + DMA_CHAN_TX_CONTROL(chan));

	if (IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT) && likely(dma_cfg->eame))
		writel(upper_32_bits(dma_tx_phy),
		       ioaddr + DMA_CHAN_TX_BASE_ADDR_HI(chan));

	writel(lower_32_bits(dma_tx_phy), ioaddr + DMA_CHAN_TX_BASE_ADDR(chan));
}

static void dwmac4_dma_init_channel(void __iomem *ioaddr,
				    struct stmmac_dma_cfg *dma_cfg, u32 chan)
{
	u32 value;

	/* common channel control register config */
	value = readl(ioaddr + DMA_CHAN_CONTROL(chan));
	if (dma_cfg->pblx8)
		value = value | DMA_BUS_MODE_PBL;
	writel(value, ioaddr + DMA_CHAN_CONTROL(chan));

	/* Mask interrupts by writing to CSR7 */
	writel(DMA_CHAN_INTR_DEFAULT_MASK,
	       ioaddr + DMA_CHAN_INTR_ENA(chan));
}

static void dwmac410_dma_init_channel(void __iomem *ioaddr,
				      struct stmmac_dma_cfg *dma_cfg, u32 chan)
{
	u32 value;

	/* common channel control register config */
	value = readl(ioaddr + DMA_CHAN_CONTROL(chan));
	if (dma_cfg->pblx8)
		value = value | DMA_BUS_MODE_PBL;

	writel(value, ioaddr + DMA_CHAN_CONTROL(chan));

	/* Mask interrupts by writing to CSR7 */
	writel(DMA_CHAN_INTR_DEFAULT_MASK_4_10,
	       ioaddr + DMA_CHAN_INTR_ENA(chan));
}

static void dwmac4_dma_init(void __iomem *ioaddr,
			    struct stmmac_dma_cfg *dma_cfg, int atds)
{
	u32 value = readl(ioaddr + DMA_SYS_BUS_MODE);

	/* Set the Fixed burst mode */
	if (dma_cfg->fixed_burst)
		value |= DMA_SYS_BUS_FB;

	/* Mixed Burst has no effect when fb is set */
	if (dma_cfg->mixed_burst)
		value |= DMA_SYS_BUS_MB;

	if (dma_cfg->aal)
		value |= DMA_SYS_BUS_AAL;

	if (dma_cfg->eame)
		value |= DMA_SYS_BUS_EAME;

	writel(value, ioaddr + DMA_SYS_BUS_MODE);

	value = readl(ioaddr + DMA_BUS_MODE);

	if (dma_cfg->multi_msi_en) {
		value &= ~DMA_BUS_MODE_INTM_MASK;
		value |= (DMA_BUS_MODE_INTM_MODE1 << DMA_BUS_MODE_INTM_SHIFT);
	}

	if (dma_cfg->dche)
		value |= DMA_BUS_MODE_DCHE;

	writel(value, ioaddr + DMA_BUS_MODE);

}

static void _dwmac4_dump_dma_regs(void __iomem *ioaddr, u32 channel,
				  u32 *reg_space)
{
	reg_space[DMA_CHAN_CONTROL(channel) / 4] =
		readl(ioaddr + DMA_CHAN_CONTROL(channel));
	reg_space[DMA_CHAN_TX_CONTROL(channel) / 4] =
		readl(ioaddr + DMA_CHAN_TX_CONTROL(channel));
	reg_space[DMA_CHAN_RX_CONTROL(channel) / 4] =
		readl(ioaddr + DMA_CHAN_RX_CONTROL(channel));
	reg_space[DMA_CHAN_TX_BASE_ADDR(channel) / 4] =
		readl(ioaddr + DMA_CHAN_TX_BASE_ADDR(channel));
	reg_space[DMA_CHAN_RX_BASE_ADDR(channel) / 4] =
		readl(ioaddr + DMA_CHAN_RX_BASE_ADDR(channel));
	reg_space[DMA_CHAN_TX_END_ADDR(channel) / 4] =
		readl(ioaddr + DMA_CHAN_TX_END_ADDR(channel));
	reg_space[DMA_CHAN_RX_END_ADDR(channel) / 4] =
		readl(ioaddr + DMA_CHAN_RX_END_ADDR(channel));
	reg_space[DMA_CHAN_TX_RING_LEN(channel) / 4] =
		readl(ioaddr + DMA_CHAN_TX_RING_LEN(channel));
	reg_space[DMA_CHAN_RX_RING_LEN(channel) / 4] =
		readl(ioaddr + DMA_CHAN_RX_RING_LEN(channel));
	reg_space[DMA_CHAN_INTR_ENA(channel) / 4] =
		readl(ioaddr + DMA_CHAN_INTR_ENA(channel));
	reg_space[DMA_CHAN_RX_WATCHDOG(channel) / 4] =
		readl(ioaddr + DMA_CHAN_RX_WATCHDOG(channel));
	reg_space[DMA_CHAN_SLOT_CTRL_STATUS(channel) / 4] =
		readl(ioaddr + DMA_CHAN_SLOT_CTRL_STATUS(channel));
	reg_space[DMA_CHAN_CUR_TX_DESC(channel) / 4] =
		readl(ioaddr + DMA_CHAN_CUR_TX_DESC(channel));
	reg_space[DMA_CHAN_CUR_RX_DESC(channel) / 4] =
		readl(ioaddr + DMA_CHAN_CUR_RX_DESC(channel));
	reg_space[DMA_CHAN_CUR_TX_BUF_ADDR(channel) / 4] =
		readl(ioaddr + DMA_CHAN_CUR_TX_BUF_ADDR(channel));
	reg_space[DMA_CHAN_CUR_RX_BUF_ADDR(channel) / 4] =
		readl(ioaddr + DMA_CHAN_CUR_RX_BUF_ADDR(channel));
	reg_space[DMA_CHAN_STATUS(channel) / 4] =
		readl(ioaddr + DMA_CHAN_STATUS(channel));
}

static void dwmac4_dump_dma_regs(void __iomem *ioaddr, u32 *reg_space)
{
	int i;

	for (i = 0; i < DMA_CHANNEL_NB_MAX; i++)
		_dwmac4_dump_dma_regs(ioaddr, i, reg_space);
}

static void dwmac4_rx_watchdog(void __iomem *ioaddr, u32 riwt, u32 queue)
{
	writel(riwt, ioaddr + DMA_CHAN_RX_WATCHDOG(queue));
}

static void dwmac4_dma_rx_chan_op_mode(void __iomem *ioaddr, int mode,
				       u32 channel, int fifosz, u8 qmode)
{
	unsigned int rqs = fifosz / 256 - 1;
	u32 mtl_rx_op;

	mtl_rx_op = readl(ioaddr + MTL_CHAN_RX_OP_MODE(channel));

	if (mode == SF_DMA_MODE) {
		pr_debug("GMAC: enable RX store and forward mode\n");
		mtl_rx_op |= MTL_OP_MODE_RSF;
	} else {
		pr_debug("GMAC: disable RX SF mode (threshold %d)\n", mode);
		mtl_rx_op &= ~MTL_OP_MODE_RSF;
		mtl_rx_op &= MTL_OP_MODE_RTC_MASK;
		if (mode <= 32)
			mtl_rx_op |= MTL_OP_MODE_RTC_32;
		else if (mode <= 64)
			mtl_rx_op |= MTL_OP_MODE_RTC_64;
		else if (mode <= 96)
			mtl_rx_op |= MTL_OP_MODE_RTC_96;
		else
			mtl_rx_op |= MTL_OP_MODE_RTC_128;
	}

	mtl_rx_op &= ~MTL_OP_MODE_RQS_MASK;
	mtl_rx_op |= rqs << MTL_OP_MODE_RQS_SHIFT;

	/* Enable flow control only if each channel gets 4 KiB or more FIFO and
	 * only if channel is not an AVB channel.
	 */
	if ((fifosz >= 4096) && (qmode != MTL_QUEUE_AVB)) {
		unsigned int rfd, rfa;

		mtl_rx_op |= MTL_OP_MODE_EHFC;

		/* Set Threshold for Activating Flow Control to min 2 frames,
		 * i.e. 1500 * 2 = 3000 bytes.
		 *
		 * Set Threshold for Deactivating Flow Control to min 1 frame,
		 * i.e. 1500 bytes.
		 */
		switch (fifosz) {
		case 4096:
			/* This violates the above formula because of FIFO size
			 * limit therefore overflow may occur in spite of this.
			 */
			rfd = 0x03; /* Full-2.5K */
			rfa = 0x01; /* Full-1.5K */
			break;

		default:
			rfd = 0x07; /* Full-4.5K */
			rfa = 0x04; /* Full-3K */
			break;
		}

		mtl_rx_op &= ~MTL_OP_MODE_RFD_MASK;
		mtl_rx_op |= rfd << MTL_OP_MODE_RFD_SHIFT;

		mtl_rx_op &= ~MTL_OP_MODE_RFA_MASK;
		mtl_rx_op |= rfa << MTL_OP_MODE_RFA_SHIFT;
	}

	writel(mtl_rx_op, ioaddr + MTL_CHAN_RX_OP_MODE(channel));
}

static void dwmac4_dma_tx_chan_op_mode(void __iomem *ioaddr, int mode,
				       u32 channel, int fifosz, u8 qmode)
{
	u32 mtl_tx_op = readl(ioaddr + MTL_CHAN_TX_OP_MODE(channel));
	unsigned int tqs = fifosz / 256 - 1;

	if (mode == SF_DMA_MODE) {
		pr_debug("GMAC: enable TX store and forward mode\n");
		/* Transmit COE type 2 cannot be done in cut-through mode. */
		mtl_tx_op |= MTL_OP_MODE_TSF;
	} else {
		pr_debug("GMAC: disabling TX SF (threshold %d)\n", mode);
		mtl_tx_op &= ~MTL_OP_MODE_TSF;
		mtl_tx_op &= MTL_OP_MODE_TTC_MASK;
		/* Set the transmit threshold */
		if (mode <= 32)
			mtl_tx_op |= MTL_OP_MODE_TTC_32;
		else if (mode <= 64)
			mtl_tx_op |= MTL_OP_MODE_TTC_64;
		else if (mode <= 96)
			mtl_tx_op |= MTL_OP_MODE_TTC_96;
		else if (mode <= 128)
			mtl_tx_op |= MTL_OP_MODE_TTC_128;
		else if (mode <= 192)
			mtl_tx_op |= MTL_OP_MODE_TTC_192;
		else if (mode <= 256)
			mtl_tx_op |= MTL_OP_MODE_TTC_256;
		else if (mode <= 384)
			mtl_tx_op |= MTL_OP_MODE_TTC_384;
		else
			mtl_tx_op |= MTL_OP_MODE_TTC_512;
	}
	/* For an IP with DWC_EQOS_NUM_TXQ == 1, the fields TXQEN and TQS are RO
	 * with reset values: TXQEN on, TQS == DWC_EQOS_TXFIFO_SIZE.
	 * For an IP with DWC_EQOS_NUM_TXQ > 1, the fields TXQEN and TQS are R/W
	 * with reset values: TXQEN off, TQS 256 bytes.
	 *
	 * TXQEN must be written for multi-channel operation and TQS must
	 * reflect the available fifo size per queue (total fifo size / number
	 * of enabled queues).
	 */
	mtl_tx_op &= ~MTL_OP_MODE_TXQEN_MASK;
	if (qmode != MTL_QUEUE_AVB)
		mtl_tx_op |= MTL_OP_MODE_TXQEN;
	else
		mtl_tx_op |= MTL_OP_MODE_TXQEN_AV;
	mtl_tx_op &= ~MTL_OP_MODE_TQS_MASK;
	mtl_tx_op |= tqs << MTL_OP_MODE_TQS_SHIFT;

	writel(mtl_tx_op, ioaddr +  MTL_CHAN_TX_OP_MODE(channel));
}

static void dwmac4_get_hw_feature(void __iomem *ioaddr,
				  struct dma_features *dma_cap)
{
	u32 hw_cap = readl(ioaddr + GMAC_HW_FEATURE0);

	/*  MAC HW feature0 */
	dma_cap->mbps_10_100 = (hw_cap & GMAC_HW_FEAT_MIISEL);
	dma_cap->mbps_1000 = (hw_cap & GMAC_HW_FEAT_GMIISEL) >> 1;
	dma_cap->half_duplex = (hw_cap & GMAC_HW_FEAT_HDSEL) >> 2;
	dma_cap->vlhash = (hw_cap & GMAC_HW_FEAT_VLHASH) >> 4;
	dma_cap->multi_addr = (hw_cap & GMAC_HW_FEAT_ADDMAC) >> 18;
	dma_cap->pcs = (hw_cap & GMAC_HW_FEAT_PCSSEL) >> 3;
	dma_cap->sma_mdio = (hw_cap & GMAC_HW_FEAT_SMASEL) >> 5;
	dma_cap->pmt_remote_wake_up = (hw_cap & GMAC_HW_FEAT_RWKSEL) >> 6;
	dma_cap->pmt_magic_frame = (hw_cap & GMAC_HW_FEAT_MGKSEL) >> 7;
	/* MMC */
	dma_cap->rmon = (hw_cap & GMAC_HW_FEAT_MMCSEL) >> 8;
	/* IEEE 1588-2008 */
	dma_cap->atime_stamp = (hw_cap & GMAC_HW_FEAT_TSSEL) >> 12;
	/* 802.3az - Energy-Efficient Ethernet (EEE) */
	dma_cap->eee = (hw_cap & GMAC_HW_FEAT_EEESEL) >> 13;
	/* TX and RX csum */
	dma_cap->tx_coe = (hw_cap & GMAC_HW_FEAT_TXCOSEL) >> 14;
	dma_cap->rx_coe =  (hw_cap & GMAC_HW_FEAT_RXCOESEL) >> 16;
	dma_cap->vlins = (hw_cap & GMAC_HW_FEAT_SAVLANINS) >> 27;
	dma_cap->arpoffsel = (hw_cap & GMAC_HW_FEAT_ARPOFFSEL) >> 9;

	/* MAC HW feature1 */
	hw_cap = readl(ioaddr + GMAC_HW_FEATURE1);
	dma_cap->l3l4fnum = (hw_cap & GMAC_HW_FEAT_L3L4FNUM) >> 27;
	dma_cap->hash_tb_sz = (hw_cap & GMAC_HW_HASH_TB_SZ) >> 24;
	dma_cap->av = (hw_cap & GMAC_HW_FEAT_AVSEL) >> 20;
	dma_cap->tsoen = (hw_cap & GMAC_HW_TSOEN) >> 18;
	dma_cap->sphen = (hw_cap & GMAC_HW_FEAT_SPHEN) >> 17;

	dma_cap->addr64 = (hw_cap & GMAC_HW_ADDR64) >> 14;
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

	/* RX and TX FIFO sizes are encoded as log2(n / 128). Undo that by
	 * shifting and store the sizes in bytes.
	 */
	dma_cap->tx_fifo_size = 128 << ((hw_cap & GMAC_HW_TXFIFOSIZE) >> 6);
	dma_cap->rx_fifo_size = 128 << ((hw_cap & GMAC_HW_RXFIFOSIZE) >> 0);
	/* MAC HW feature2 */
	hw_cap = readl(ioaddr + GMAC_HW_FEATURE2);
	/* TX and RX number of channels */
	dma_cap->number_rx_channel =
		((hw_cap & GMAC_HW_FEAT_RXCHCNT) >> 12) + 1;
	dma_cap->number_tx_channel =
		((hw_cap & GMAC_HW_FEAT_TXCHCNT) >> 18) + 1;
	/* TX and RX number of queues */
	dma_cap->number_rx_queues =
		((hw_cap & GMAC_HW_FEAT_RXQCNT) >> 0) + 1;
	dma_cap->number_tx_queues =
		((hw_cap & GMAC_HW_FEAT_TXQCNT) >> 6) + 1;
	/* PPS output */
	dma_cap->pps_out_num = (hw_cap & GMAC_HW_FEAT_PPSOUTNUM) >> 24;

	/* IEEE 1588-2002 */
	dma_cap->time_stamp = 0;
	/* Number of Auxiliary Snapshot Inputs */
	dma_cap->aux_snapshot_n = (hw_cap & GMAC_HW_FEAT_AUXSNAPNUM) >> 28;

	/* MAC HW feature3 */
	hw_cap = readl(ioaddr + GMAC_HW_FEATURE3);

	/* 5.10 Features */
	dma_cap->asp = (hw_cap & GMAC_HW_FEAT_ASP) >> 28;
	dma_cap->tbssel = (hw_cap & GMAC_HW_FEAT_TBSSEL) >> 27;
	dma_cap->fpesel = (hw_cap & GMAC_HW_FEAT_FPESEL) >> 26;
	dma_cap->estwid = (hw_cap & GMAC_HW_FEAT_ESTWID) >> 20;
	dma_cap->estdep = (hw_cap & GMAC_HW_FEAT_ESTDEP) >> 17;
	dma_cap->estsel = (hw_cap & GMAC_HW_FEAT_ESTSEL) >> 16;
	dma_cap->frpes = (hw_cap & GMAC_HW_FEAT_FRPES) >> 13;
	dma_cap->frpbs = (hw_cap & GMAC_HW_FEAT_FRPBS) >> 11;
	dma_cap->frpsel = (hw_cap & GMAC_HW_FEAT_FRPSEL) >> 10;
	dma_cap->dvlan = (hw_cap & GMAC_HW_FEAT_DVLAN) >> 5;
}

/* Enable/disable TSO feature and set MSS */
static void dwmac4_enable_tso(void __iomem *ioaddr, bool en, u32 chan)
{
	u32 value;

	if (en) {
		/* enable TSO */
		value = readl(ioaddr + DMA_CHAN_TX_CONTROL(chan));
		writel(value | DMA_CONTROL_TSE,
		       ioaddr + DMA_CHAN_TX_CONTROL(chan));
	} else {
		/* enable TSO */
		value = readl(ioaddr + DMA_CHAN_TX_CONTROL(chan));
		writel(value & ~DMA_CONTROL_TSE,
		       ioaddr + DMA_CHAN_TX_CONTROL(chan));
	}
}

static void dwmac4_qmode(void __iomem *ioaddr, u32 channel, u8 qmode)
{
	u32 mtl_tx_op = readl(ioaddr + MTL_CHAN_TX_OP_MODE(channel));

	mtl_tx_op &= ~MTL_OP_MODE_TXQEN_MASK;
	if (qmode != MTL_QUEUE_AVB)
		mtl_tx_op |= MTL_OP_MODE_TXQEN;
	else
		mtl_tx_op |= MTL_OP_MODE_TXQEN_AV;

	writel(mtl_tx_op, ioaddr +  MTL_CHAN_TX_OP_MODE(channel));
}

static void dwmac4_set_bfsize(void __iomem *ioaddr, int bfsize, u32 chan)
{
	u32 value = readl(ioaddr + DMA_CHAN_RX_CONTROL(chan));

	value &= ~DMA_RBSZ_MASK;
	value |= (bfsize << DMA_RBSZ_SHIFT) & DMA_RBSZ_MASK;

	writel(value, ioaddr + DMA_CHAN_RX_CONTROL(chan));
}

static void dwmac4_enable_sph(void __iomem *ioaddr, bool en, u32 chan)
{
	u32 value = readl(ioaddr + GMAC_EXT_CONFIG);

	value &= ~GMAC_CONFIG_HDSMS;
	value |= GMAC_CONFIG_HDSMS_256; /* Segment max 256 bytes */
	writel(value, ioaddr + GMAC_EXT_CONFIG);

	value = readl(ioaddr + DMA_CHAN_CONTROL(chan));
	if (en)
		value |= DMA_CONTROL_SPH;
	else
		value &= ~DMA_CONTROL_SPH;
	writel(value, ioaddr + DMA_CHAN_CONTROL(chan));
}

static int dwmac4_enable_tbs(void __iomem *ioaddr, bool en, u32 chan)
{
	u32 value = readl(ioaddr + DMA_CHAN_TX_CONTROL(chan));

	if (en)
		value |= DMA_CONTROL_EDSE;
	else
		value &= ~DMA_CONTROL_EDSE;

	writel(value, ioaddr + DMA_CHAN_TX_CONTROL(chan));

	value = readl(ioaddr + DMA_CHAN_TX_CONTROL(chan)) & DMA_CONTROL_EDSE;
	if (en && !value)
		return -EIO;

	writel(DMA_TBS_DEF_FTOS, ioaddr + DMA_TBS_CTRL);
	return 0;
}

const struct stmmac_dma_ops dwmac4_dma_ops = {
	.reset = dwmac4_dma_reset,
	.init = dwmac4_dma_init,
	.init_chan = dwmac4_dma_init_channel,
	.init_rx_chan = dwmac4_dma_init_rx_chan,
	.init_tx_chan = dwmac4_dma_init_tx_chan,
	.axi = dwmac4_dma_axi,
	.dump_regs = dwmac4_dump_dma_regs,
	.dma_rx_mode = dwmac4_dma_rx_chan_op_mode,
	.dma_tx_mode = dwmac4_dma_tx_chan_op_mode,
	.enable_dma_irq = dwmac4_enable_dma_irq,
	.disable_dma_irq = dwmac4_disable_dma_irq,
	.start_tx = dwmac4_dma_start_tx,
	.stop_tx = dwmac4_dma_stop_tx,
	.start_rx = dwmac4_dma_start_rx,
	.stop_rx = dwmac4_dma_stop_rx,
	.dma_interrupt = dwmac4_dma_interrupt,
	.get_hw_feature = dwmac4_get_hw_feature,
	.rx_watchdog = dwmac4_rx_watchdog,
	.set_rx_ring_len = dwmac4_set_rx_ring_len,
	.set_tx_ring_len = dwmac4_set_tx_ring_len,
	.set_rx_tail_ptr = dwmac4_set_rx_tail_ptr,
	.set_tx_tail_ptr = dwmac4_set_tx_tail_ptr,
	.enable_tso = dwmac4_enable_tso,
	.qmode = dwmac4_qmode,
	.set_bfsize = dwmac4_set_bfsize,
	.enable_sph = dwmac4_enable_sph,
};

const struct stmmac_dma_ops dwmac410_dma_ops = {
	.reset = dwmac4_dma_reset,
	.init = dwmac4_dma_init,
	.init_chan = dwmac410_dma_init_channel,
	.init_rx_chan = dwmac4_dma_init_rx_chan,
	.init_tx_chan = dwmac4_dma_init_tx_chan,
	.axi = dwmac4_dma_axi,
	.dump_regs = dwmac4_dump_dma_regs,
	.dma_rx_mode = dwmac4_dma_rx_chan_op_mode,
	.dma_tx_mode = dwmac4_dma_tx_chan_op_mode,
	.enable_dma_irq = dwmac410_enable_dma_irq,
	.disable_dma_irq = dwmac4_disable_dma_irq,
	.start_tx = dwmac4_dma_start_tx,
	.stop_tx = dwmac4_dma_stop_tx,
	.start_rx = dwmac4_dma_start_rx,
	.stop_rx = dwmac4_dma_stop_rx,
	.dma_interrupt = dwmac4_dma_interrupt,
	.get_hw_feature = dwmac4_get_hw_feature,
	.rx_watchdog = dwmac4_rx_watchdog,
	.set_rx_ring_len = dwmac4_set_rx_ring_len,
	.set_tx_ring_len = dwmac4_set_tx_ring_len,
	.set_rx_tail_ptr = dwmac4_set_rx_tail_ptr,
	.set_tx_tail_ptr = dwmac4_set_tx_tail_ptr,
	.enable_tso = dwmac4_enable_tso,
	.qmode = dwmac4_qmode,
	.set_bfsize = dwmac4_set_bfsize,
	.enable_sph = dwmac4_enable_sph,
	.enable_tbs = dwmac4_enable_tbs,
};
