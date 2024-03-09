// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dwmac-sun8i.c - Allwinner sun8i DWMAC specific glue layer
 *
 * Copyright (C) 2017 Corentin Labbe <clabbe.montjoie@gmail.com>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mdio-mux.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <linux/stmmac.h>

#include "stmmac.h"
#include "stmmac_platform.h"

/* General notes on dwmac-sun8i:
 * Locking: no locking is necessary in this file because all necessary locking
 *		is done in the "stmmac files"
 */

/* struct emac_variant - Describe dwmac-sun8i hardware variant
 * @default_syscon_value:	The default value of the EMAC register in syscon
 *				This value is used for disabling properly EMAC
 *				and used as a good starting value in case of the
 *				boot process(uboot) leave some stuff.
 * @syscon_field		reg_field for the syscon's gmac register
 * @soc_has_internal_phy:	Does the MAC embed an internal PHY
 * @support_mii:		Does the MAC handle MII
 * @support_rmii:		Does the MAC handle RMII
 * @support_rgmii:		Does the MAC handle RGMII
 *
 * @rx_delay_max:		Maximum raw value for RX delay chain
 * @tx_delay_max:		Maximum raw value for TX delay chain
 *				These two also indicate the bitmask for
 *				the RX and TX delay chain registers. A
 *				value of zero indicates this is not supported.
 */
struct emac_variant {
	u32 default_syscon_value;
	const struct reg_field *syscon_field;
	bool soc_has_internal_phy;
	bool support_mii;
	bool support_rmii;
	bool support_rgmii;
	u8 rx_delay_max;
	u8 tx_delay_max;
};

/* struct sunxi_priv_data - hold all sunxi private data
 * @ephy_clk:	reference to the optional EPHY clock for the internal PHY
 * @regulator:	reference to the optional regulator
 * @rst_ephy:	reference to the optional EPHY reset for the internal PHY
 * @variant:	reference to the current board variant
 * @regmap:	regmap for using the syscon
 * @internal_phy_powered: Does the internal PHY is enabled
 * @use_internal_phy: Is the internal PHY selected for use
 * @mux_handle:	Internal pointer used by mdio-mux lib
 */
struct sunxi_priv_data {
	struct clk *ephy_clk;
	struct regulator *regulator;
	struct reset_control *rst_ephy;
	const struct emac_variant *variant;
	struct regmap_field *regmap_field;
	bool internal_phy_powered;
	bool use_internal_phy;
	void *mux_handle;
};

/* EMAC clock register @ 0x30 in the "system control" address range */
static const struct reg_field sun8i_syscon_reg_field = {
	.reg = 0x30,
	.lsb = 0,
	.msb = 31,
};

/* EMAC clock register @ 0x164 in the CCU address range */
static const struct reg_field sun8i_ccu_reg_field = {
	.reg = 0x164,
	.lsb = 0,
	.msb = 31,
};

static const struct emac_variant emac_variant_h3 = {
	.default_syscon_value = 0x58000,
	.syscon_field = &sun8i_syscon_reg_field,
	.soc_has_internal_phy = true,
	.support_mii = true,
	.support_rmii = true,
	.support_rgmii = true,
	.rx_delay_max = 31,
	.tx_delay_max = 7,
};

static const struct emac_variant emac_variant_v3s = {
	.default_syscon_value = 0x38000,
	.syscon_field = &sun8i_syscon_reg_field,
	.soc_has_internal_phy = true,
	.support_mii = true
};

static const struct emac_variant emac_variant_a83t = {
	.default_syscon_value = 0,
	.syscon_field = &sun8i_syscon_reg_field,
	.soc_has_internal_phy = false,
	.support_mii = true,
	.support_rgmii = true,
	.rx_delay_max = 31,
	.tx_delay_max = 7,
};

static const struct emac_variant emac_variant_r40 = {
	.default_syscon_value = 0,
	.syscon_field = &sun8i_ccu_reg_field,
	.support_mii = true,
	.support_rgmii = true,
	.rx_delay_max = 7,
};

static const struct emac_variant emac_variant_a64 = {
	.default_syscon_value = 0,
	.syscon_field = &sun8i_syscon_reg_field,
	.soc_has_internal_phy = false,
	.support_mii = true,
	.support_rmii = true,
	.support_rgmii = true,
	.rx_delay_max = 31,
	.tx_delay_max = 7,
};

static const struct emac_variant emac_variant_h6 = {
	.default_syscon_value = 0x50000,
	.syscon_field = &sun8i_syscon_reg_field,
	/* The "Internal PHY" of H6 is not on the die. It's on the
	 * co-packaged AC200 chip instead.
	 */
	.soc_has_internal_phy = false,
	.support_mii = true,
	.support_rmii = true,
	.support_rgmii = true,
	.rx_delay_max = 31,
	.tx_delay_max = 7,
};

#define EMAC_BASIC_CTL0 0x00
#define EMAC_BASIC_CTL1 0x04
#define EMAC_INT_STA    0x08
#define EMAC_INT_EN     0x0C
#define EMAC_TX_CTL0    0x10
#define EMAC_TX_CTL1    0x14
#define EMAC_TX_FLOW_CTL        0x1C
#define EMAC_TX_DESC_LIST 0x20
#define EMAC_RX_CTL0    0x24
#define EMAC_RX_CTL1    0x28
#define EMAC_RX_DESC_LIST 0x34
#define EMAC_RX_FRM_FLT 0x38
#define EMAC_MDIO_CMD   0x48
#define EMAC_MDIO_DATA  0x4C
#define EMAC_MACADDR_HI(reg) (0x50 + (reg) * 8)
#define EMAC_MACADDR_LO(reg) (0x54 + (reg) * 8)
#define EMAC_TX_DMA_STA 0xB0
#define EMAC_TX_CUR_DESC        0xB4
#define EMAC_TX_CUR_BUF 0xB8
#define EMAC_RX_DMA_STA 0xC0
#define EMAC_RX_CUR_DESC        0xC4
#define EMAC_RX_CUR_BUF 0xC8

/* Use in EMAC_BASIC_CTL0 */
#define EMAC_DUPLEX_FULL	BIT(0)
#define EMAC_LOOPBACK		BIT(1)
#define EMAC_SPEED_1000 0
#define EMAC_SPEED_100 (0x03 << 2)
#define EMAC_SPEED_10 (0x02 << 2)

/* Use in EMAC_BASIC_CTL1 */
#define EMAC_BURSTLEN_SHIFT		24

/* Used in EMAC_RX_FRM_FLT */
#define EMAC_FRM_FLT_RXALL              BIT(0)
#define EMAC_FRM_FLT_CTL                BIT(13)
#define EMAC_FRM_FLT_MULTICAST          BIT(16)

/* Used in RX_CTL1*/
#define EMAC_RX_MD              BIT(1)
#define EMAC_RX_TH_MASK		GENMASK(5, 4)
#define EMAC_RX_TH_32		0
#define EMAC_RX_TH_64		(0x1 << 4)
#define EMAC_RX_TH_96		(0x2 << 4)
#define EMAC_RX_TH_128		(0x3 << 4)
#define EMAC_RX_DMA_EN  BIT(30)
#define EMAC_RX_DMA_START       BIT(31)

/* Used in TX_CTL1*/
#define EMAC_TX_MD              BIT(1)
#define EMAC_TX_NEXT_FRM        BIT(2)
#define EMAC_TX_TH_MASK		GENMASK(10, 8)
#define EMAC_TX_TH_64		0
#define EMAC_TX_TH_128		(0x1 << 8)
#define EMAC_TX_TH_192		(0x2 << 8)
#define EMAC_TX_TH_256		(0x3 << 8)
#define EMAC_TX_DMA_EN  BIT(30)
#define EMAC_TX_DMA_START       BIT(31)

/* Used in RX_CTL0 */
#define EMAC_RX_RECEIVER_EN             BIT(31)
#define EMAC_RX_DO_CRC BIT(27)
#define EMAC_RX_FLOW_CTL_EN             BIT(16)

/* Used in TX_CTL0 */
#define EMAC_TX_TRANSMITTER_EN  BIT(31)

/* Used in EMAC_TX_FLOW_CTL */
#define EMAC_TX_FLOW_CTL_EN             BIT(0)

/* Used in EMAC_INT_STA */
#define EMAC_TX_INT             BIT(0)
#define EMAC_TX_DMA_STOP_INT    BIT(1)
#define EMAC_TX_BUF_UA_INT      BIT(2)
#define EMAC_TX_TIMEOUT_INT     BIT(3)
#define EMAC_TX_UNDERFLOW_INT   BIT(4)
#define EMAC_TX_EARLY_INT       BIT(5)
#define EMAC_RX_INT             BIT(8)
#define EMAC_RX_BUF_UA_INT      BIT(9)
#define EMAC_RX_DMA_STOP_INT    BIT(10)
#define EMAC_RX_TIMEOUT_INT     BIT(11)
#define EMAC_RX_OVERFLOW_INT    BIT(12)
#define EMAC_RX_EARLY_INT       BIT(13)
#define EMAC_RGMII_STA_INT      BIT(16)

#define EMAC_INT_MSK_COMMON	EMAC_RGMII_STA_INT
#define EMAC_INT_MSK_TX		(EMAC_TX_INT | \
				 EMAC_TX_DMA_STOP_INT | \
				 EMAC_TX_BUF_UA_INT | \
				 EMAC_TX_TIMEOUT_INT | \
				 EMAC_TX_UNDERFLOW_INT | \
				 EMAC_TX_EARLY_INT |\
				 EMAC_INT_MSK_COMMON)
#define EMAC_INT_MSK_RX		(EMAC_RX_INT | \
				 EMAC_RX_BUF_UA_INT | \
				 EMAC_RX_DMA_STOP_INT | \
				 EMAC_RX_TIMEOUT_INT | \
				 EMAC_RX_OVERFLOW_INT | \
				 EMAC_RX_EARLY_INT | \
				 EMAC_INT_MSK_COMMON)

#define MAC_ADDR_TYPE_DST BIT(31)

/* H3 specific bits for EPHY */
#define H3_EPHY_ADDR_SHIFT	20
#define H3_EPHY_CLK_SEL		BIT(18) /* 1: 24MHz, 0: 25MHz */
#define H3_EPHY_LED_POL		BIT(17) /* 1: active low, 0: active high */
#define H3_EPHY_SHUTDOWN	BIT(16) /* 1: shutdown, 0: power up */
#define H3_EPHY_SELECT		BIT(15) /* 1: internal PHY, 0: external PHY */
#define H3_EPHY_MUX_MASK	(H3_EPHY_SHUTDOWN | H3_EPHY_SELECT)
#define DWMAC_SUN8I_MDIO_MUX_INTERNAL_ID	1
#define DWMAC_SUN8I_MDIO_MUX_EXTERNAL_ID	2

/* H3/A64 specific bits */
#define SYSCON_RMII_EN		BIT(13) /* 1: enable RMII (overrides EPIT) */

/* Generic system control EMAC_CLK bits */
#define SYSCON_ETXDC_SHIFT		10
#define SYSCON_ERXDC_SHIFT		5
/* EMAC PHY Interface Type */
#define SYSCON_EPIT			BIT(2) /* 1: RGMII, 0: MII */
#define SYSCON_ETCS_MASK		GENMASK(1, 0)
#define SYSCON_ETCS_MII		0x0
#define SYSCON_ETCS_EXT_GMII	0x1
#define SYSCON_ETCS_INT_GMII	0x2

/* sun8i_dwmac_dma_reset() - reset the EMAC
 * Called from stmmac via stmmac_dma_ops->reset
 */
static int sun8i_dwmac_dma_reset(void __iomem *ioaddr)
{
	writel(0, ioaddr + EMAC_RX_CTL1);
	writel(0, ioaddr + EMAC_TX_CTL1);
	writel(0, ioaddr + EMAC_RX_FRM_FLT);
	writel(0, ioaddr + EMAC_RX_DESC_LIST);
	writel(0, ioaddr + EMAC_TX_DESC_LIST);
	writel(0, ioaddr + EMAC_INT_EN);
	writel(0x1FFFFFF, ioaddr + EMAC_INT_STA);
	return 0;
}

/* sun8i_dwmac_dma_init() - initialize the EMAC
 * Called from stmmac via stmmac_dma_ops->init
 */
static void sun8i_dwmac_dma_init(void __iomem *ioaddr,
				 struct stmmac_dma_cfg *dma_cfg, int atds)
{
	writel(EMAC_RX_INT | EMAC_TX_INT, ioaddr + EMAC_INT_EN);
	writel(0x1FFFFFF, ioaddr + EMAC_INT_STA);
}

static void sun8i_dwmac_dma_init_rx(struct stmmac_priv *priv,
				    void __iomem *ioaddr,
				    struct stmmac_dma_cfg *dma_cfg,
				    dma_addr_t dma_rx_phy, u32 chan)
{
	/* Write RX descriptors address */
	writel(lower_32_bits(dma_rx_phy), ioaddr + EMAC_RX_DESC_LIST);
}

static void sun8i_dwmac_dma_init_tx(struct stmmac_priv *priv,
				    void __iomem *ioaddr,
				    struct stmmac_dma_cfg *dma_cfg,
				    dma_addr_t dma_tx_phy, u32 chan)
{
	/* Write TX descriptors address */
	writel(lower_32_bits(dma_tx_phy), ioaddr + EMAC_TX_DESC_LIST);
}

/* sun8i_dwmac_dump_regs() - Dump EMAC address space
 * Called from stmmac_dma_ops->dump_regs
 * Used for ethtool
 */
static void sun8i_dwmac_dump_regs(struct stmmac_priv *priv,
				  void __iomem *ioaddr, u32 *reg_space)
{
	int i;

	for (i = 0; i < 0xC8; i += 4) {
		if (i == 0x32 || i == 0x3C)
			continue;
		reg_space[i / 4] = readl(ioaddr + i);
	}
}

/* sun8i_dwmac_dump_mac_regs() - Dump EMAC address space
 * Called from stmmac_ops->dump_regs
 * Used for ethtool
 */
static void sun8i_dwmac_dump_mac_regs(struct mac_device_info *hw,
				      u32 *reg_space)
{
	int i;
	void __iomem *ioaddr = hw->pcsr;

	for (i = 0; i < 0xC8; i += 4) {
		if (i == 0x32 || i == 0x3C)
			continue;
		reg_space[i / 4] = readl(ioaddr + i);
	}
}

static void sun8i_dwmac_enable_dma_irq(struct stmmac_priv *priv,
				       void __iomem *ioaddr, u32 chan,
				       bool rx, bool tx)
{
	u32 value = readl(ioaddr + EMAC_INT_EN);

	if (rx)
		value |= EMAC_RX_INT;
	if (tx)
		value |= EMAC_TX_INT;

	writel(value, ioaddr + EMAC_INT_EN);
}

static void sun8i_dwmac_disable_dma_irq(struct stmmac_priv *priv,
					void __iomem *ioaddr, u32 chan,
					bool rx, bool tx)
{
	u32 value = readl(ioaddr + EMAC_INT_EN);

	if (rx)
		value &= ~EMAC_RX_INT;
	if (tx)
		value &= ~EMAC_TX_INT;

	writel(value, ioaddr + EMAC_INT_EN);
}

static void sun8i_dwmac_dma_start_tx(struct stmmac_priv *priv,
				     void __iomem *ioaddr, u32 chan)
{
	u32 v;

	v = readl(ioaddr + EMAC_TX_CTL1);
	v |= EMAC_TX_DMA_START;
	v |= EMAC_TX_DMA_EN;
	writel(v, ioaddr + EMAC_TX_CTL1);
}

static void sun8i_dwmac_enable_dma_transmission(void __iomem *ioaddr)
{
	u32 v;

	v = readl(ioaddr + EMAC_TX_CTL1);
	v |= EMAC_TX_DMA_START;
	v |= EMAC_TX_DMA_EN;
	writel(v, ioaddr + EMAC_TX_CTL1);
}

static void sun8i_dwmac_dma_stop_tx(struct stmmac_priv *priv,
				    void __iomem *ioaddr, u32 chan)
{
	u32 v;

	v = readl(ioaddr + EMAC_TX_CTL1);
	v &= ~EMAC_TX_DMA_EN;
	writel(v, ioaddr + EMAC_TX_CTL1);
}

static void sun8i_dwmac_dma_start_rx(struct stmmac_priv *priv,
				     void __iomem *ioaddr, u32 chan)
{
	u32 v;

	v = readl(ioaddr + EMAC_RX_CTL1);
	v |= EMAC_RX_DMA_START;
	v |= EMAC_RX_DMA_EN;
	writel(v, ioaddr + EMAC_RX_CTL1);
}

static void sun8i_dwmac_dma_stop_rx(struct stmmac_priv *priv,
				    void __iomem *ioaddr, u32 chan)
{
	u32 v;

	v = readl(ioaddr + EMAC_RX_CTL1);
	v &= ~EMAC_RX_DMA_EN;
	writel(v, ioaddr + EMAC_RX_CTL1);
}

static int sun8i_dwmac_dma_interrupt(struct stmmac_priv *priv,
				     void __iomem *ioaddr,
				     struct stmmac_extra_stats *x, u32 chan,
				     u32 dir)
{
	struct stmmac_pcpu_stats *stats = this_cpu_ptr(priv->xstats.pcpu_stats);
	int ret = 0;
	u32 v;

	v = readl(ioaddr + EMAC_INT_STA);

	if (dir == DMA_DIR_RX)
		v &= EMAC_INT_MSK_RX;
	else if (dir == DMA_DIR_TX)
		v &= EMAC_INT_MSK_TX;

	if (v & EMAC_TX_INT) {
		ret |= handle_tx;
		u64_stats_update_begin(&stats->syncp);
		u64_stats_inc(&stats->tx_normal_irq_n[chan]);
		u64_stats_update_end(&stats->syncp);
	}

	if (v & EMAC_TX_DMA_STOP_INT)
		x->tx_process_stopped_irq++;

	if (v & EMAC_TX_BUF_UA_INT)
		x->tx_process_stopped_irq++;

	if (v & EMAC_TX_TIMEOUT_INT)
		ret |= tx_hard_error;

	if (v & EMAC_TX_UNDERFLOW_INT) {
		ret |= tx_hard_error;
		x->tx_undeflow_irq++;
	}

	if (v & EMAC_TX_EARLY_INT)
		x->tx_early_irq++;

	if (v & EMAC_RX_INT) {
		ret |= handle_rx;
		u64_stats_update_begin(&stats->syncp);
		u64_stats_inc(&stats->rx_normal_irq_n[chan]);
		u64_stats_update_end(&stats->syncp);
	}

	if (v & EMAC_RX_BUF_UA_INT)
		x->rx_buf_unav_irq++;

	if (v & EMAC_RX_DMA_STOP_INT)
		x->rx_process_stopped_irq++;

	if (v & EMAC_RX_TIMEOUT_INT)
		ret |= tx_hard_error;

	if (v & EMAC_RX_OVERFLOW_INT) {
		ret |= tx_hard_error;
		x->rx_overflow_irq++;
	}

	if (v & EMAC_RX_EARLY_INT)
		x->rx_early_irq++;

	if (v & EMAC_RGMII_STA_INT)
		x->irq_rgmii_n++;

	writel(v, ioaddr + EMAC_INT_STA);

	return ret;
}

static void sun8i_dwmac_dma_operation_mode_rx(struct stmmac_priv *priv,
					      void __iomem *ioaddr, int mode,
					      u32 channel, int fifosz, u8 qmode)
{
	u32 v;

	v = readl(ioaddr + EMAC_RX_CTL1);
	if (mode == SF_DMA_MODE) {
		v |= EMAC_RX_MD;
	} else {
		v &= ~EMAC_RX_MD;
		v &= ~EMAC_RX_TH_MASK;
		if (mode < 32)
			v |= EMAC_RX_TH_32;
		else if (mode < 64)
			v |= EMAC_RX_TH_64;
		else if (mode < 96)
			v |= EMAC_RX_TH_96;
		else if (mode < 128)
			v |= EMAC_RX_TH_128;
	}
	writel(v, ioaddr + EMAC_RX_CTL1);
}

static void sun8i_dwmac_dma_operation_mode_tx(struct stmmac_priv *priv,
					      void __iomem *ioaddr, int mode,
					      u32 channel, int fifosz, u8 qmode)
{
	u32 v;

	v = readl(ioaddr + EMAC_TX_CTL1);
	if (mode == SF_DMA_MODE) {
		v |= EMAC_TX_MD;
		/* Undocumented bit (called TX_NEXT_FRM in BSP), the original
		 * comment is
		 * "Operating on second frame increase the performance
		 * especially when transmit store-and-forward is used."
		 */
		v |= EMAC_TX_NEXT_FRM;
	} else {
		v &= ~EMAC_TX_MD;
		v &= ~EMAC_TX_TH_MASK;
		if (mode < 64)
			v |= EMAC_TX_TH_64;
		else if (mode < 128)
			v |= EMAC_TX_TH_128;
		else if (mode < 192)
			v |= EMAC_TX_TH_192;
		else if (mode < 256)
			v |= EMAC_TX_TH_256;
	}
	writel(v, ioaddr + EMAC_TX_CTL1);
}

static const struct stmmac_dma_ops sun8i_dwmac_dma_ops = {
	.reset = sun8i_dwmac_dma_reset,
	.init = sun8i_dwmac_dma_init,
	.init_rx_chan = sun8i_dwmac_dma_init_rx,
	.init_tx_chan = sun8i_dwmac_dma_init_tx,
	.dump_regs = sun8i_dwmac_dump_regs,
	.dma_rx_mode = sun8i_dwmac_dma_operation_mode_rx,
	.dma_tx_mode = sun8i_dwmac_dma_operation_mode_tx,
	.enable_dma_transmission = sun8i_dwmac_enable_dma_transmission,
	.enable_dma_irq = sun8i_dwmac_enable_dma_irq,
	.disable_dma_irq = sun8i_dwmac_disable_dma_irq,
	.start_tx = sun8i_dwmac_dma_start_tx,
	.stop_tx = sun8i_dwmac_dma_stop_tx,
	.start_rx = sun8i_dwmac_dma_start_rx,
	.stop_rx = sun8i_dwmac_dma_stop_rx,
	.dma_interrupt = sun8i_dwmac_dma_interrupt,
};

static int sun8i_dwmac_power_internal_phy(struct stmmac_priv *priv);

static int sun8i_dwmac_init(struct platform_device *pdev, void *priv)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct sunxi_priv_data *gmac = priv;
	int ret;

	if (gmac->regulator) {
		ret = regulator_enable(gmac->regulator);
		if (ret) {
			dev_err(&pdev->dev, "Fail to enable regulator\n");
			return ret;
		}
	}

	if (gmac->use_internal_phy) {
		ret = sun8i_dwmac_power_internal_phy(netdev_priv(ndev));
		if (ret)
			goto err_disable_regulator;
	}

	return 0;

err_disable_regulator:
	if (gmac->regulator)
		regulator_disable(gmac->regulator);

	return ret;
}

static void sun8i_dwmac_core_init(struct mac_device_info *hw,
				  struct net_device *dev)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 v;

	v = (8 << EMAC_BURSTLEN_SHIFT); /* burst len */
	writel(v, ioaddr + EMAC_BASIC_CTL1);
}

static void sun8i_dwmac_set_mac(void __iomem *ioaddr, bool enable)
{
	u32 t, r;

	t = readl(ioaddr + EMAC_TX_CTL0);
	r = readl(ioaddr + EMAC_RX_CTL0);
	if (enable) {
		t |= EMAC_TX_TRANSMITTER_EN;
		r |= EMAC_RX_RECEIVER_EN;
	} else {
		t &= ~EMAC_TX_TRANSMITTER_EN;
		r &= ~EMAC_RX_RECEIVER_EN;
	}
	writel(t, ioaddr + EMAC_TX_CTL0);
	writel(r, ioaddr + EMAC_RX_CTL0);
}

/* Set MAC address at slot reg_n
 * All slot > 0 need to be enabled with MAC_ADDR_TYPE_DST
 * If addr is NULL, clear the slot
 */
static void sun8i_dwmac_set_umac_addr(struct mac_device_info *hw,
				      const unsigned char *addr,
				      unsigned int reg_n)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 v;

	if (!addr) {
		writel(0, ioaddr + EMAC_MACADDR_HI(reg_n));
		return;
	}

	stmmac_set_mac_addr(ioaddr, addr, EMAC_MACADDR_HI(reg_n),
			    EMAC_MACADDR_LO(reg_n));
	if (reg_n > 0) {
		v = readl(ioaddr + EMAC_MACADDR_HI(reg_n));
		v |= MAC_ADDR_TYPE_DST;
		writel(v, ioaddr + EMAC_MACADDR_HI(reg_n));
	}
}

static void sun8i_dwmac_get_umac_addr(struct mac_device_info *hw,
				      unsigned char *addr,
				      unsigned int reg_n)
{
	void __iomem *ioaddr = hw->pcsr;

	stmmac_get_mac_addr(ioaddr, addr, EMAC_MACADDR_HI(reg_n),
			    EMAC_MACADDR_LO(reg_n));
}

/* caution this function must return non 0 to work */
static int sun8i_dwmac_rx_ipc_enable(struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 v;

	v = readl(ioaddr + EMAC_RX_CTL0);
	v |= EMAC_RX_DO_CRC;
	writel(v, ioaddr + EMAC_RX_CTL0);

	return 1;
}

static void sun8i_dwmac_set_filter(struct mac_device_info *hw,
				   struct net_device *dev)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 v;
	int i = 1;
	struct netdev_hw_addr *ha;
	int macaddrs = netdev_uc_count(dev) + netdev_mc_count(dev) + 1;

	v = EMAC_FRM_FLT_CTL;

	if (dev->flags & IFF_PROMISC) {
		v = EMAC_FRM_FLT_RXALL;
	} else if (dev->flags & IFF_ALLMULTI) {
		v |= EMAC_FRM_FLT_MULTICAST;
	} else if (macaddrs <= hw->unicast_filter_entries) {
		if (!netdev_mc_empty(dev)) {
			netdev_for_each_mc_addr(ha, dev) {
				sun8i_dwmac_set_umac_addr(hw, ha->addr, i);
				i++;
			}
		}
		if (!netdev_uc_empty(dev)) {
			netdev_for_each_uc_addr(ha, dev) {
				sun8i_dwmac_set_umac_addr(hw, ha->addr, i);
				i++;
			}
		}
	} else {
		if (!(readl(ioaddr + EMAC_RX_FRM_FLT) & EMAC_FRM_FLT_RXALL))
			netdev_info(dev, "Too many address, switching to promiscuous\n");
		v = EMAC_FRM_FLT_RXALL;
	}

	/* Disable unused address filter slots */
	while (i < hw->unicast_filter_entries)
		sun8i_dwmac_set_umac_addr(hw, NULL, i++);

	writel(v, ioaddr + EMAC_RX_FRM_FLT);
}

static void sun8i_dwmac_flow_ctrl(struct mac_device_info *hw,
				  unsigned int duplex, unsigned int fc,
				  unsigned int pause_time, u32 tx_cnt)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 v;

	v = readl(ioaddr + EMAC_RX_CTL0);
	if (fc == FLOW_AUTO)
		v |= EMAC_RX_FLOW_CTL_EN;
	else
		v &= ~EMAC_RX_FLOW_CTL_EN;
	writel(v, ioaddr + EMAC_RX_CTL0);

	v = readl(ioaddr + EMAC_TX_FLOW_CTL);
	if (fc == FLOW_AUTO)
		v |= EMAC_TX_FLOW_CTL_EN;
	else
		v &= ~EMAC_TX_FLOW_CTL_EN;
	writel(v, ioaddr + EMAC_TX_FLOW_CTL);
}

static int sun8i_dwmac_reset(struct stmmac_priv *priv)
{
	u32 v;
	int err;

	v = readl(priv->ioaddr + EMAC_BASIC_CTL1);
	writel(v | 0x01, priv->ioaddr + EMAC_BASIC_CTL1);

	/* The timeout was previoulsy set to 10ms, but some board (OrangePI0)
	 * need more if no cable plugged. 100ms seems OK
	 */
	err = readl_poll_timeout(priv->ioaddr + EMAC_BASIC_CTL1, v,
				 !(v & 0x01), 100, 100000);

	if (err) {
		dev_err(priv->device, "EMAC reset timeout\n");
		return err;
	}
	return 0;
}

/* Search in mdio-mux node for internal PHY node and get its clk/reset */
static int get_ephy_nodes(struct stmmac_priv *priv)
{
	struct sunxi_priv_data *gmac = priv->plat->bsp_priv;
	struct device_node *mdio_mux, *iphynode;
	struct device_node *mdio_internal;
	int ret;

	mdio_mux = of_get_child_by_name(priv->device->of_node, "mdio-mux");
	if (!mdio_mux) {
		dev_err(priv->device, "Cannot get mdio-mux node\n");
		return -ENODEV;
	}

	mdio_internal = of_get_compatible_child(mdio_mux,
						"allwinner,sun8i-h3-mdio-internal");
	of_node_put(mdio_mux);
	if (!mdio_internal) {
		dev_err(priv->device, "Cannot get internal_mdio node\n");
		return -ENODEV;
	}

	/* Seek for internal PHY */
	for_each_child_of_node(mdio_internal, iphynode) {
		gmac->ephy_clk = of_clk_get(iphynode, 0);
		if (IS_ERR(gmac->ephy_clk))
			continue;
		gmac->rst_ephy = of_reset_control_get_exclusive(iphynode, NULL);
		if (IS_ERR(gmac->rst_ephy)) {
			ret = PTR_ERR(gmac->rst_ephy);
			if (ret == -EPROBE_DEFER) {
				of_node_put(iphynode);
				of_node_put(mdio_internal);
				return ret;
			}
			continue;
		}
		dev_info(priv->device, "Found internal PHY node\n");
		of_node_put(iphynode);
		of_node_put(mdio_internal);
		return 0;
	}

	of_node_put(mdio_internal);
	return -ENODEV;
}

static int sun8i_dwmac_power_internal_phy(struct stmmac_priv *priv)
{
	struct sunxi_priv_data *gmac = priv->plat->bsp_priv;
	int ret;

	if (gmac->internal_phy_powered) {
		dev_warn(priv->device, "Internal PHY already powered\n");
		return 0;
	}

	dev_info(priv->device, "Powering internal PHY\n");
	ret = clk_prepare_enable(gmac->ephy_clk);
	if (ret) {
		dev_err(priv->device, "Cannot enable internal PHY\n");
		return ret;
	}

	/* Make sure the EPHY is properly reseted, as U-Boot may leave
	 * it at deasserted state, and thus it may fail to reset EMAC.
	 *
	 * This assumes the driver has exclusive access to the EPHY reset.
	 */
	ret = reset_control_reset(gmac->rst_ephy);
	if (ret) {
		dev_err(priv->device, "Cannot reset internal PHY\n");
		clk_disable_unprepare(gmac->ephy_clk);
		return ret;
	}

	gmac->internal_phy_powered = true;

	return 0;
}

static void sun8i_dwmac_unpower_internal_phy(struct sunxi_priv_data *gmac)
{
	if (!gmac->internal_phy_powered)
		return;

	clk_disable_unprepare(gmac->ephy_clk);
	reset_control_assert(gmac->rst_ephy);
	gmac->internal_phy_powered = false;
}

/* MDIO multiplexing switch function
 * This function is called by the mdio-mux layer when it thinks the mdio bus
 * multiplexer needs to switch.
 * 'current_child' is the current value of the mux register
 * 'desired_child' is the value of the 'reg' property of the target child MDIO
 * node.
 * The first time this function is called, current_child == -1.
 * If current_child == desired_child, then the mux is already set to the
 * correct bus.
 */
static int mdio_mux_syscon_switch_fn(int current_child, int desired_child,
				     void *data)
{
	struct stmmac_priv *priv = data;
	struct sunxi_priv_data *gmac = priv->plat->bsp_priv;
	u32 reg, val;
	int ret = 0;

	if (current_child ^ desired_child) {
		regmap_field_read(gmac->regmap_field, &reg);
		switch (desired_child) {
		case DWMAC_SUN8I_MDIO_MUX_INTERNAL_ID:
			dev_info(priv->device, "Switch mux to internal PHY");
			val = (reg & ~H3_EPHY_MUX_MASK) | H3_EPHY_SELECT;
			gmac->use_internal_phy = true;
			break;
		case DWMAC_SUN8I_MDIO_MUX_EXTERNAL_ID:
			dev_info(priv->device, "Switch mux to external PHY");
			val = (reg & ~H3_EPHY_MUX_MASK) | H3_EPHY_SHUTDOWN;
			gmac->use_internal_phy = false;
			break;
		default:
			dev_err(priv->device, "Invalid child ID %x\n",
				desired_child);
			return -EINVAL;
		}
		regmap_field_write(gmac->regmap_field, val);
		if (gmac->use_internal_phy) {
			ret = sun8i_dwmac_power_internal_phy(priv);
			if (ret)
				return ret;
		} else {
			sun8i_dwmac_unpower_internal_phy(gmac);
		}
		/* After changing syscon value, the MAC need reset or it will
		 * use the last value (and so the last PHY set).
		 */
		ret = sun8i_dwmac_reset(priv);
	}
	return ret;
}

static int sun8i_dwmac_register_mdio_mux(struct stmmac_priv *priv)
{
	int ret;
	struct device_node *mdio_mux;
	struct sunxi_priv_data *gmac = priv->plat->bsp_priv;

	mdio_mux = of_get_child_by_name(priv->device->of_node, "mdio-mux");
	if (!mdio_mux)
		return -ENODEV;

	ret = mdio_mux_init(priv->device, mdio_mux, mdio_mux_syscon_switch_fn,
			    &gmac->mux_handle, priv, priv->mii);
	of_node_put(mdio_mux);
	return ret;
}

static int sun8i_dwmac_set_syscon(struct device *dev,
				  struct plat_stmmacenet_data *plat)
{
	struct sunxi_priv_data *gmac = plat->bsp_priv;
	struct device_node *node = dev->of_node;
	int ret;
	u32 reg, val;

	ret = regmap_field_read(gmac->regmap_field, &val);
	if (ret) {
		dev_err(dev, "Fail to read from regmap field.\n");
		return ret;
	}

	reg = gmac->variant->default_syscon_value;
	if (reg != val)
		dev_warn(dev,
			 "Current syscon value is not the default %x (expect %x)\n",
			 val, reg);

	if (gmac->variant->soc_has_internal_phy) {
		if (of_property_read_bool(node, "allwinner,leds-active-low"))
			reg |= H3_EPHY_LED_POL;
		else
			reg &= ~H3_EPHY_LED_POL;

		/* Force EPHY xtal frequency to 24MHz. */
		reg |= H3_EPHY_CLK_SEL;

		ret = of_mdio_parse_addr(dev, plat->phy_node);
		if (ret < 0) {
			dev_err(dev, "Could not parse MDIO addr\n");
			return ret;
		}
		/* of_mdio_parse_addr returns a valid (0 ~ 31) PHY
		 * address. No need to mask it again.
		 */
		reg |= 1 << H3_EPHY_ADDR_SHIFT;
	} else {
		/* For SoCs without internal PHY the PHY selection bit should be
		 * set to 0 (external PHY).
		 */
		reg &= ~H3_EPHY_SELECT;
	}

	if (!of_property_read_u32(node, "allwinner,tx-delay-ps", &val)) {
		if (val % 100) {
			dev_err(dev, "tx-delay must be a multiple of 100\n");
			return -EINVAL;
		}
		val /= 100;
		dev_dbg(dev, "set tx-delay to %x\n", val);
		if (val <= gmac->variant->tx_delay_max) {
			reg &= ~(gmac->variant->tx_delay_max <<
				 SYSCON_ETXDC_SHIFT);
			reg |= (val << SYSCON_ETXDC_SHIFT);
		} else {
			dev_err(dev, "Invalid TX clock delay: %d\n",
				val);
			return -EINVAL;
		}
	}

	if (!of_property_read_u32(node, "allwinner,rx-delay-ps", &val)) {
		if (val % 100) {
			dev_err(dev, "rx-delay must be a multiple of 100\n");
			return -EINVAL;
		}
		val /= 100;
		dev_dbg(dev, "set rx-delay to %x\n", val);
		if (val <= gmac->variant->rx_delay_max) {
			reg &= ~(gmac->variant->rx_delay_max <<
				 SYSCON_ERXDC_SHIFT);
			reg |= (val << SYSCON_ERXDC_SHIFT);
		} else {
			dev_err(dev, "Invalid RX clock delay: %d\n",
				val);
			return -EINVAL;
		}
	}

	/* Clear interface mode bits */
	reg &= ~(SYSCON_ETCS_MASK | SYSCON_EPIT);
	if (gmac->variant->support_rmii)
		reg &= ~SYSCON_RMII_EN;

	switch (plat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		/* default */
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		reg |= SYSCON_EPIT | SYSCON_ETCS_INT_GMII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		reg |= SYSCON_RMII_EN | SYSCON_ETCS_EXT_GMII;
		break;
	default:
		dev_err(dev, "Unsupported interface mode: %s",
			phy_modes(plat->mac_interface));
		return -EINVAL;
	}

	regmap_field_write(gmac->regmap_field, reg);

	return 0;
}

static void sun8i_dwmac_unset_syscon(struct sunxi_priv_data *gmac)
{
	u32 reg = gmac->variant->default_syscon_value;

	regmap_field_write(gmac->regmap_field, reg);
}

static void sun8i_dwmac_exit(struct platform_device *pdev, void *priv)
{
	struct sunxi_priv_data *gmac = priv;

	if (gmac->variant->soc_has_internal_phy)
		sun8i_dwmac_unpower_internal_phy(gmac);

	if (gmac->regulator)
		regulator_disable(gmac->regulator);
}

static void sun8i_dwmac_set_mac_loopback(void __iomem *ioaddr, bool enable)
{
	u32 value = readl(ioaddr + EMAC_BASIC_CTL0);

	if (enable)
		value |= EMAC_LOOPBACK;
	else
		value &= ~EMAC_LOOPBACK;

	writel(value, ioaddr + EMAC_BASIC_CTL0);
}

static const struct stmmac_ops sun8i_dwmac_ops = {
	.core_init = sun8i_dwmac_core_init,
	.set_mac = sun8i_dwmac_set_mac,
	.dump_regs = sun8i_dwmac_dump_mac_regs,
	.rx_ipc = sun8i_dwmac_rx_ipc_enable,
	.set_filter = sun8i_dwmac_set_filter,
	.flow_ctrl = sun8i_dwmac_flow_ctrl,
	.set_umac_addr = sun8i_dwmac_set_umac_addr,
	.get_umac_addr = sun8i_dwmac_get_umac_addr,
	.set_mac_loopback = sun8i_dwmac_set_mac_loopback,
};

static struct mac_device_info *sun8i_dwmac_setup(void *ppriv)
{
	struct mac_device_info *mac;
	struct stmmac_priv *priv = ppriv;

	mac = devm_kzalloc(priv->device, sizeof(*mac), GFP_KERNEL);
	if (!mac)
		return NULL;

	mac->pcsr = priv->ioaddr;
	mac->mac = &sun8i_dwmac_ops;
	mac->dma = &sun8i_dwmac_dma_ops;

	priv->dev->priv_flags |= IFF_UNICAST_FLT;

	/* The loopback bit seems to be re-set when link change
	 * Simply mask it each time
	 * Speed 10/100/1000 are set in BIT(2)/BIT(3)
	 */
	mac->link.speed_mask = GENMASK(3, 2) | EMAC_LOOPBACK;
	mac->link.speed10 = EMAC_SPEED_10;
	mac->link.speed100 = EMAC_SPEED_100;
	mac->link.speed1000 = EMAC_SPEED_1000;
	mac->link.duplex = EMAC_DUPLEX_FULL;
	mac->mii.addr = EMAC_MDIO_CMD;
	mac->mii.data = EMAC_MDIO_DATA;
	mac->mii.reg_shift = 4;
	mac->mii.reg_mask = GENMASK(8, 4);
	mac->mii.addr_shift = 12;
	mac->mii.addr_mask = GENMASK(16, 12);
	mac->mii.clk_csr_shift = 20;
	mac->mii.clk_csr_mask = GENMASK(22, 20);
	mac->unicast_filter_entries = 8;

	/* Synopsys Id is not available */
	priv->synopsys_id = 0;

	return mac;
}

static struct regmap *sun8i_dwmac_get_syscon_from_dev(struct device_node *node)
{
	struct device_node *syscon_node;
	struct platform_device *syscon_pdev;
	struct regmap *regmap = NULL;

	syscon_node = of_parse_phandle(node, "syscon", 0);
	if (!syscon_node)
		return ERR_PTR(-ENODEV);

	syscon_pdev = of_find_device_by_node(syscon_node);
	if (!syscon_pdev) {
		/* platform device might not be probed yet */
		regmap = ERR_PTR(-EPROBE_DEFER);
		goto out_put_node;
	}

	/* If no regmap is found then the other device driver is at fault */
	regmap = dev_get_regmap(&syscon_pdev->dev, NULL);
	if (!regmap)
		regmap = ERR_PTR(-EINVAL);

	platform_device_put(syscon_pdev);
out_put_node:
	of_node_put(syscon_node);
	return regmap;
}

static int sun8i_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct sunxi_priv_data *gmac;
	struct device *dev = &pdev->dev;
	phy_interface_t interface;
	int ret;
	struct stmmac_priv *priv;
	struct net_device *ndev;
	struct regmap *regmap;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	gmac = devm_kzalloc(dev, sizeof(*gmac), GFP_KERNEL);
	if (!gmac)
		return -ENOMEM;

	gmac->variant = of_device_get_match_data(&pdev->dev);
	if (!gmac->variant) {
		dev_err(&pdev->dev, "Missing dwmac-sun8i variant\n");
		return -EINVAL;
	}

	/* Optional regulator for PHY */
	gmac->regulator = devm_regulator_get_optional(dev, "phy");
	if (IS_ERR(gmac->regulator)) {
		if (PTR_ERR(gmac->regulator) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(dev, "No regulator found\n");
		gmac->regulator = NULL;
	}

	/* The "GMAC clock control" register might be located in the
	 * CCU address range (on the R40), or the system control address
	 * range (on most other sun8i and later SoCs).
	 *
	 * The former controls most if not all clocks in the SoC. The
	 * latter has an SoC identification register, and on some SoCs,
	 * controls to map device specific SRAM to either the intended
	 * peripheral, or the CPU address space.
	 *
	 * In either case, there should be a coordinated and restricted
	 * method of accessing the register needed here. This is done by
	 * having the device export a custom regmap, instead of a generic
	 * syscon, which grants all access to all registers.
	 *
	 * To support old device trees, we fall back to using the syscon
	 * interface if possible.
	 */
	regmap = sun8i_dwmac_get_syscon_from_dev(pdev->dev.of_node);
	if (IS_ERR(regmap))
		regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							 "syscon");
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&pdev->dev, "Unable to map syscon: %d\n", ret);
		return ret;
	}

	gmac->regmap_field = devm_regmap_field_alloc(dev, regmap,
						     *gmac->variant->syscon_field);
	if (IS_ERR(gmac->regmap_field)) {
		ret = PTR_ERR(gmac->regmap_field);
		dev_err(dev, "Unable to map syscon register: %d\n", ret);
		return ret;
	}

	ret = of_get_phy_mode(dev->of_node, &interface);
	if (ret)
		return -EINVAL;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	/* platform data specifying hardware features and callbacks.
	 * hardware features were copied from Allwinner drivers.
	 */
	plat_dat->mac_interface = interface;
	plat_dat->rx_coe = STMMAC_RX_COE_TYPE2;
	plat_dat->tx_coe = 1;
	plat_dat->flags |= STMMAC_FLAG_HAS_SUN8I;
	plat_dat->bsp_priv = gmac;
	plat_dat->init = sun8i_dwmac_init;
	plat_dat->exit = sun8i_dwmac_exit;
	plat_dat->setup = sun8i_dwmac_setup;
	plat_dat->tx_fifo_size = 4096;
	plat_dat->rx_fifo_size = 16384;

	ret = sun8i_dwmac_set_syscon(&pdev->dev, plat_dat);
	if (ret)
		return ret;

	ret = sun8i_dwmac_init(pdev, plat_dat->bsp_priv);
	if (ret)
		goto dwmac_syscon;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto dwmac_exit;

	ndev = dev_get_drvdata(&pdev->dev);
	priv = netdev_priv(ndev);

	/* the MAC is runtime suspended after stmmac_dvr_probe(), so we
	 * need to ensure the MAC resume back before other operations such
	 * as reset.
	 */
	pm_runtime_get_sync(&pdev->dev);

	/* The mux must be registered after parent MDIO
	 * so after stmmac_dvr_probe()
	 */
	if (gmac->variant->soc_has_internal_phy) {
		ret = get_ephy_nodes(priv);
		if (ret)
			goto dwmac_remove;
		ret = sun8i_dwmac_register_mdio_mux(priv);
		if (ret) {
			dev_err(&pdev->dev, "Failed to register mux\n");
			goto dwmac_mux;
		}
	} else {
		ret = sun8i_dwmac_reset(priv);
		if (ret)
			goto dwmac_remove;
	}

	pm_runtime_put(&pdev->dev);

	return 0;

dwmac_mux:
	reset_control_put(gmac->rst_ephy);
	clk_put(gmac->ephy_clk);
dwmac_remove:
	pm_runtime_put_noidle(&pdev->dev);
	stmmac_dvr_remove(&pdev->dev);
dwmac_exit:
	sun8i_dwmac_exit(pdev, gmac);
dwmac_syscon:
	sun8i_dwmac_unset_syscon(gmac);

	return ret;
}

static void sun8i_dwmac_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct sunxi_priv_data *gmac = priv->plat->bsp_priv;

	if (gmac->variant->soc_has_internal_phy) {
		mdio_mux_uninit(gmac->mux_handle);
		sun8i_dwmac_unpower_internal_phy(gmac);
		reset_control_put(gmac->rst_ephy);
		clk_put(gmac->ephy_clk);
	}

	stmmac_pltfr_remove(pdev);
	sun8i_dwmac_unset_syscon(gmac);
}

static void sun8i_dwmac_shutdown(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct sunxi_priv_data *gmac = priv->plat->bsp_priv;

	sun8i_dwmac_exit(pdev, gmac);
}

static const struct of_device_id sun8i_dwmac_match[] = {
	{ .compatible = "allwinner,sun8i-h3-emac",
		.data = &emac_variant_h3 },
	{ .compatible = "allwinner,sun8i-v3s-emac",
		.data = &emac_variant_v3s },
	{ .compatible = "allwinner,sun8i-a83t-emac",
		.data = &emac_variant_a83t },
	{ .compatible = "allwinner,sun8i-r40-gmac",
		.data = &emac_variant_r40 },
	{ .compatible = "allwinner,sun50i-a64-emac",
		.data = &emac_variant_a64 },
	{ .compatible = "allwinner,sun50i-h6-emac",
		.data = &emac_variant_h6 },
	{ }
};
MODULE_DEVICE_TABLE(of, sun8i_dwmac_match);

static struct platform_driver sun8i_dwmac_driver = {
	.probe  = sun8i_dwmac_probe,
	.remove_new = sun8i_dwmac_remove,
	.shutdown = sun8i_dwmac_shutdown,
	.driver = {
		.name           = "dwmac-sun8i",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = sun8i_dwmac_match,
	},
};
module_platform_driver(sun8i_dwmac_driver);

MODULE_AUTHOR("Corentin Labbe <clabbe.montjoie@gmail.com>");
MODULE_DESCRIPTION("Allwinner sun8i DWMAC specific glue layer");
MODULE_LICENSE("GPL");
