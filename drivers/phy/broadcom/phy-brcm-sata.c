/*
 * Broadcom SATA3 AHCI Controller PHY Driver
 *
 * Copyright (C) 2016 Broadcom
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
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#define SATA_PCB_BANK_OFFSET				0x23c
#define SATA_PCB_REG_OFFSET(ofs)			((ofs) * 4)

#define MAX_PORTS					2

/* Register offset between PHYs in PCB space */
#define SATA_PCB_REG_28NM_SPACE_SIZE			0x1000

/* The older SATA PHY registers duplicated per port registers within the map,
 * rather than having a separate map per port.
 */
#define SATA_PCB_REG_40NM_SPACE_SIZE			0x10

/* Register offset between PHYs in PHY control space */
#define SATA_PHY_CTRL_REG_28NM_SPACE_SIZE		0x8

enum brcm_sata_phy_version {
	BRCM_SATA_PHY_STB_28NM,
	BRCM_SATA_PHY_STB_40NM,
	BRCM_SATA_PHY_IPROC_NS2,
	BRCM_SATA_PHY_IPROC_NSP,
	BRCM_SATA_PHY_IPROC_SR,
};

enum brcm_sata_phy_rxaeq_mode {
	RXAEQ_MODE_OFF = 0,
	RXAEQ_MODE_AUTO,
	RXAEQ_MODE_MANUAL,
};

static enum brcm_sata_phy_rxaeq_mode rxaeq_to_val(const char *m)
{
	if (!strcmp(m, "auto"))
		return RXAEQ_MODE_AUTO;
	else if (!strcmp(m, "manual"))
		return RXAEQ_MODE_MANUAL;
	else
		return RXAEQ_MODE_OFF;
}

struct brcm_sata_port {
	int portnum;
	struct phy *phy;
	struct brcm_sata_phy *phy_priv;
	bool ssc_en;
	enum brcm_sata_phy_rxaeq_mode rxaeq_mode;
	u32 rxaeq_val;
};

struct brcm_sata_phy {
	struct device *dev;
	void __iomem *phy_base;
	void __iomem *ctrl_base;
	enum brcm_sata_phy_version version;

	struct brcm_sata_port phys[MAX_PORTS];
};

enum sata_phy_regs {
	BLOCK0_REG_BANK				= 0x000,
	BLOCK0_XGXSSTATUS			= 0x81,
	BLOCK0_XGXSSTATUS_PLL_LOCK		= BIT(12),
	BLOCK0_SPARE				= 0x8d,
	BLOCK0_SPARE_OOB_CLK_SEL_MASK		= 0x3,
	BLOCK0_SPARE_OOB_CLK_SEL_REFBY2		= 0x1,

	PLL_REG_BANK_0				= 0x050,
	PLL_REG_BANK_0_PLLCONTROL_0		= 0x81,
	PLLCONTROL_0_FREQ_DET_RESTART		= BIT(13),
	PLLCONTROL_0_FREQ_MONITOR		= BIT(12),
	PLLCONTROL_0_SEQ_START			= BIT(15),
	PLL_CAP_CONTROL				= 0x85,
	PLL_ACTRL2				= 0x8b,
	PLL_ACTRL2_SELDIV_MASK			= 0x1f,
	PLL_ACTRL2_SELDIV_SHIFT			= 9,
	PLL_ACTRL6				= 0x86,

	PLL1_REG_BANK				= 0x060,
	PLL1_ACTRL2				= 0x82,
	PLL1_ACTRL3				= 0x83,
	PLL1_ACTRL4				= 0x84,

	TX_REG_BANK				= 0x070,
	TX_ACTRL0				= 0x80,
	TX_ACTRL0_TXPOL_FLIP			= BIT(6),

	AEQRX_REG_BANK_0			= 0xd0,
	AEQ_CONTROL1				= 0x81,
	AEQ_CONTROL1_ENABLE			= BIT(2),
	AEQ_CONTROL1_FREEZE			= BIT(3),
	AEQ_FRC_EQ				= 0x83,
	AEQ_FRC_EQ_FORCE			= BIT(0),
	AEQ_FRC_EQ_FORCE_VAL			= BIT(1),
	AEQRX_REG_BANK_1			= 0xe0,

	OOB_REG_BANK				= 0x150,
	OOB1_REG_BANK				= 0x160,
	OOB_CTRL1				= 0x80,
	OOB_CTRL1_BURST_MAX_MASK		= 0xf,
	OOB_CTRL1_BURST_MAX_SHIFT		= 12,
	OOB_CTRL1_BURST_MIN_MASK		= 0xf,
	OOB_CTRL1_BURST_MIN_SHIFT		= 8,
	OOB_CTRL1_WAKE_IDLE_MAX_MASK		= 0xf,
	OOB_CTRL1_WAKE_IDLE_MAX_SHIFT		= 4,
	OOB_CTRL1_WAKE_IDLE_MIN_MASK		= 0xf,
	OOB_CTRL1_WAKE_IDLE_MIN_SHIFT		= 0,
	OOB_CTRL2				= 0x81,
	OOB_CTRL2_SEL_ENA_SHIFT			= 15,
	OOB_CTRL2_SEL_ENA_RC_SHIFT		= 14,
	OOB_CTRL2_RESET_IDLE_MAX_MASK		= 0x3f,
	OOB_CTRL2_RESET_IDLE_MAX_SHIFT		= 8,
	OOB_CTRL2_BURST_CNT_MASK		= 0x3,
	OOB_CTRL2_BURST_CNT_SHIFT		= 6,
	OOB_CTRL2_RESET_IDLE_MIN_MASK		= 0x3f,
	OOB_CTRL2_RESET_IDLE_MIN_SHIFT		= 0,

	TXPMD_REG_BANK				= 0x1a0,
	TXPMD_CONTROL1				= 0x81,
	TXPMD_CONTROL1_TX_SSC_EN_FRC		= BIT(0),
	TXPMD_CONTROL1_TX_SSC_EN_FRC_VAL	= BIT(1),
	TXPMD_TX_FREQ_CTRL_CONTROL1		= 0x82,
	TXPMD_TX_FREQ_CTRL_CONTROL2		= 0x83,
	TXPMD_TX_FREQ_CTRL_CONTROL2_FMIN_MASK	= 0x3ff,
	TXPMD_TX_FREQ_CTRL_CONTROL3		= 0x84,
	TXPMD_TX_FREQ_CTRL_CONTROL3_FMAX_MASK	= 0x3ff,
};

enum sata_phy_ctrl_regs {
	PHY_CTRL_1				= 0x0,
	PHY_CTRL_1_RESET			= BIT(0),
};

static inline void __iomem *brcm_sata_pcb_base(struct brcm_sata_port *port)
{
	struct brcm_sata_phy *priv = port->phy_priv;
	u32 size = 0;

	switch (priv->version) {
	case BRCM_SATA_PHY_STB_28NM:
	case BRCM_SATA_PHY_IPROC_NS2:
		size = SATA_PCB_REG_28NM_SPACE_SIZE;
		break;
	case BRCM_SATA_PHY_STB_40NM:
		size = SATA_PCB_REG_40NM_SPACE_SIZE;
		break;
	default:
		dev_err(priv->dev, "invalid phy version\n");
		break;
	}

	return priv->phy_base + (port->portnum * size);
}

static inline void __iomem *brcm_sata_ctrl_base(struct brcm_sata_port *port)
{
	struct brcm_sata_phy *priv = port->phy_priv;
	u32 size = 0;

	switch (priv->version) {
	case BRCM_SATA_PHY_IPROC_NS2:
		size = SATA_PHY_CTRL_REG_28NM_SPACE_SIZE;
		break;
	default:
		dev_err(priv->dev, "invalid phy version\n");
		break;
	}

	return priv->ctrl_base + (port->portnum * size);
}

static void brcm_sata_phy_wr(void __iomem *pcb_base, u32 bank,
			     u32 ofs, u32 msk, u32 value)
{
	u32 tmp;

	writel(bank, pcb_base + SATA_PCB_BANK_OFFSET);
	tmp = readl(pcb_base + SATA_PCB_REG_OFFSET(ofs));
	tmp = (tmp & msk) | value;
	writel(tmp, pcb_base + SATA_PCB_REG_OFFSET(ofs));
}

static u32 brcm_sata_phy_rd(void __iomem *pcb_base, u32 bank, u32 ofs)
{
	writel(bank, pcb_base + SATA_PCB_BANK_OFFSET);
	return readl(pcb_base + SATA_PCB_REG_OFFSET(ofs));
}

/* These defaults were characterized by H/W group */
#define STB_FMIN_VAL_DEFAULT	0x3df
#define STB_FMAX_VAL_DEFAULT	0x3df
#define STB_FMAX_VAL_SSC	0x83

static void brcm_stb_sata_ssc_init(struct brcm_sata_port *port)
{
	void __iomem *base = brcm_sata_pcb_base(port);
	struct brcm_sata_phy *priv = port->phy_priv;
	u32 tmp;

	/* override the TX spread spectrum setting */
	tmp = TXPMD_CONTROL1_TX_SSC_EN_FRC_VAL | TXPMD_CONTROL1_TX_SSC_EN_FRC;
	brcm_sata_phy_wr(base, TXPMD_REG_BANK, TXPMD_CONTROL1, ~tmp, tmp);

	/* set fixed min freq */
	brcm_sata_phy_wr(base, TXPMD_REG_BANK, TXPMD_TX_FREQ_CTRL_CONTROL2,
			 ~TXPMD_TX_FREQ_CTRL_CONTROL2_FMIN_MASK,
			 STB_FMIN_VAL_DEFAULT);

	/* set fixed max freq depending on SSC config */
	if (port->ssc_en) {
		dev_info(priv->dev, "enabling SSC on port%d\n", port->portnum);
		tmp = STB_FMAX_VAL_SSC;
	} else {
		tmp = STB_FMAX_VAL_DEFAULT;
	}

	brcm_sata_phy_wr(base, TXPMD_REG_BANK, TXPMD_TX_FREQ_CTRL_CONTROL3,
			  ~TXPMD_TX_FREQ_CTRL_CONTROL3_FMAX_MASK, tmp);
}

#define AEQ_FRC_EQ_VAL_SHIFT	2
#define AEQ_FRC_EQ_VAL_MASK	0x3f

static int brcm_stb_sata_rxaeq_init(struct brcm_sata_port *port)
{
	void __iomem *base = brcm_sata_pcb_base(port);
	u32 tmp = 0, reg = 0;

	switch (port->rxaeq_mode) {
	case RXAEQ_MODE_OFF:
		return 0;

	case RXAEQ_MODE_AUTO:
		reg = AEQ_CONTROL1;
		tmp = AEQ_CONTROL1_ENABLE | AEQ_CONTROL1_FREEZE;
		break;

	case RXAEQ_MODE_MANUAL:
		reg = AEQ_FRC_EQ;
		tmp = AEQ_FRC_EQ_FORCE | AEQ_FRC_EQ_FORCE_VAL;
		if (port->rxaeq_val > AEQ_FRC_EQ_VAL_MASK)
			return -EINVAL;
		tmp |= port->rxaeq_val << AEQ_FRC_EQ_VAL_SHIFT;
		break;
	}

	brcm_sata_phy_wr(base, AEQRX_REG_BANK_0, reg, ~tmp, tmp);
	brcm_sata_phy_wr(base, AEQRX_REG_BANK_1, reg, ~tmp, tmp);

	return 0;
}

static int brcm_stb_sata_init(struct brcm_sata_port *port)
{
	brcm_stb_sata_ssc_init(port);

	return brcm_stb_sata_rxaeq_init(port);
}

/* NS2 SATA PLL1 defaults were characterized by H/W group */
#define NS2_PLL1_ACTRL2_MAGIC	0x1df8
#define NS2_PLL1_ACTRL3_MAGIC	0x2b00
#define NS2_PLL1_ACTRL4_MAGIC	0x8824

static int brcm_ns2_sata_init(struct brcm_sata_port *port)
{
	int try;
	unsigned int val;
	void __iomem *base = brcm_sata_pcb_base(port);
	void __iomem *ctrl_base = brcm_sata_ctrl_base(port);
	struct device *dev = port->phy_priv->dev;

	/* Configure OOB control */
	val = 0x0;
	val |= (0xc << OOB_CTRL1_BURST_MAX_SHIFT);
	val |= (0x4 << OOB_CTRL1_BURST_MIN_SHIFT);
	val |= (0x9 << OOB_CTRL1_WAKE_IDLE_MAX_SHIFT);
	val |= (0x3 << OOB_CTRL1_WAKE_IDLE_MIN_SHIFT);
	brcm_sata_phy_wr(base, OOB_REG_BANK, OOB_CTRL1, 0x0, val);
	val = 0x0;
	val |= (0x1b << OOB_CTRL2_RESET_IDLE_MAX_SHIFT);
	val |= (0x2 << OOB_CTRL2_BURST_CNT_SHIFT);
	val |= (0x9 << OOB_CTRL2_RESET_IDLE_MIN_SHIFT);
	brcm_sata_phy_wr(base, OOB_REG_BANK, OOB_CTRL2, 0x0, val);

	/* Configure PHY PLL register bank 1 */
	val = NS2_PLL1_ACTRL2_MAGIC;
	brcm_sata_phy_wr(base, PLL1_REG_BANK, PLL1_ACTRL2, 0x0, val);
	val = NS2_PLL1_ACTRL3_MAGIC;
	brcm_sata_phy_wr(base, PLL1_REG_BANK, PLL1_ACTRL3, 0x0, val);
	val = NS2_PLL1_ACTRL4_MAGIC;
	brcm_sata_phy_wr(base, PLL1_REG_BANK, PLL1_ACTRL4, 0x0, val);

	/* Configure PHY BLOCK0 register bank */
	/* Set oob_clk_sel to refclk/2 */
	brcm_sata_phy_wr(base, BLOCK0_REG_BANK, BLOCK0_SPARE,
			 ~BLOCK0_SPARE_OOB_CLK_SEL_MASK,
			 BLOCK0_SPARE_OOB_CLK_SEL_REFBY2);

	/* Strobe PHY reset using PHY control register */
	writel(PHY_CTRL_1_RESET, ctrl_base + PHY_CTRL_1);
	mdelay(1);
	writel(0x0, ctrl_base + PHY_CTRL_1);
	mdelay(1);

	/* Wait for PHY PLL lock by polling pll_lock bit */
	try = 50;
	while (try) {
		val = brcm_sata_phy_rd(base, BLOCK0_REG_BANK,
					BLOCK0_XGXSSTATUS);
		if (val & BLOCK0_XGXSSTATUS_PLL_LOCK)
			break;
		msleep(20);
		try--;
	}
	if (!try) {
		/* PLL did not lock; give up */
		dev_err(dev, "port%d PLL did not lock\n", port->portnum);
		return -ETIMEDOUT;
	}

	dev_dbg(dev, "port%d initialized\n", port->portnum);

	return 0;
}

static int brcm_nsp_sata_init(struct brcm_sata_port *port)
{
	struct brcm_sata_phy *priv = port->phy_priv;
	struct device *dev = port->phy_priv->dev;
	void __iomem *base = priv->phy_base;
	unsigned int oob_bank;
	unsigned int val, try;

	/* Configure OOB control */
	if (port->portnum == 0)
		oob_bank = OOB_REG_BANK;
	else if (port->portnum == 1)
		oob_bank = OOB1_REG_BANK;
	else
		return -EINVAL;

	val = 0x0;
	val |= (0x0f << OOB_CTRL1_BURST_MAX_SHIFT);
	val |= (0x06 << OOB_CTRL1_BURST_MIN_SHIFT);
	val |= (0x0f << OOB_CTRL1_WAKE_IDLE_MAX_SHIFT);
	val |= (0x06 << OOB_CTRL1_WAKE_IDLE_MIN_SHIFT);
	brcm_sata_phy_wr(base, oob_bank, OOB_CTRL1, 0x0, val);

	val = 0x0;
	val |= (0x2e << OOB_CTRL2_RESET_IDLE_MAX_SHIFT);
	val |= (0x02 << OOB_CTRL2_BURST_CNT_SHIFT);
	val |= (0x16 << OOB_CTRL2_RESET_IDLE_MIN_SHIFT);
	brcm_sata_phy_wr(base, oob_bank, OOB_CTRL2, 0x0, val);


	brcm_sata_phy_wr(base, PLL_REG_BANK_0, PLL_ACTRL2,
		~(PLL_ACTRL2_SELDIV_MASK << PLL_ACTRL2_SELDIV_SHIFT),
		0x0c << PLL_ACTRL2_SELDIV_SHIFT);

	brcm_sata_phy_wr(base, PLL_REG_BANK_0, PLL_CAP_CONTROL,
						0xff0, 0x4f0);

	val = PLLCONTROL_0_FREQ_DET_RESTART | PLLCONTROL_0_FREQ_MONITOR;
	brcm_sata_phy_wr(base, PLL_REG_BANK_0, PLL_REG_BANK_0_PLLCONTROL_0,
								~val, val);
	val = PLLCONTROL_0_SEQ_START;
	brcm_sata_phy_wr(base, PLL_REG_BANK_0, PLL_REG_BANK_0_PLLCONTROL_0,
								~val, 0);
	mdelay(10);
	brcm_sata_phy_wr(base, PLL_REG_BANK_0, PLL_REG_BANK_0_PLLCONTROL_0,
								~val, val);

	/* Wait for pll_seq_done bit */
	try = 50;
	while (--try) {
		val = brcm_sata_phy_rd(base, BLOCK0_REG_BANK,
					BLOCK0_XGXSSTATUS);
		if (val & BLOCK0_XGXSSTATUS_PLL_LOCK)
			break;
		msleep(20);
	}
	if (!try) {
		/* PLL did not lock; give up */
		dev_err(dev, "port%d PLL did not lock\n", port->portnum);
		return -ETIMEDOUT;
	}

	dev_dbg(dev, "port%d initialized\n", port->portnum);

	return 0;
}

/* SR PHY PLL0 registers */
#define SR_PLL0_ACTRL6_MAGIC			0xa

/* SR PHY PLL1 registers */
#define SR_PLL1_ACTRL2_MAGIC			0x32
#define SR_PLL1_ACTRL3_MAGIC			0x2
#define SR_PLL1_ACTRL4_MAGIC			0x3e8

static int brcm_sr_sata_init(struct brcm_sata_port *port)
{
	struct brcm_sata_phy *priv = port->phy_priv;
	struct device *dev = port->phy_priv->dev;
	void __iomem *base = priv->phy_base;
	unsigned int val, try;

	/* Configure PHY PLL register bank 1 */
	val = SR_PLL1_ACTRL2_MAGIC;
	brcm_sata_phy_wr(base, PLL1_REG_BANK, PLL1_ACTRL2, 0x0, val);
	val = SR_PLL1_ACTRL3_MAGIC;
	brcm_sata_phy_wr(base, PLL1_REG_BANK, PLL1_ACTRL3, 0x0, val);
	val = SR_PLL1_ACTRL4_MAGIC;
	brcm_sata_phy_wr(base, PLL1_REG_BANK, PLL1_ACTRL4, 0x0, val);

	/* Configure PHY PLL register bank 0 */
	val = SR_PLL0_ACTRL6_MAGIC;
	brcm_sata_phy_wr(base, PLL_REG_BANK_0, PLL_ACTRL6, 0x0, val);

	/* Wait for PHY PLL lock by polling pll_lock bit */
	try = 50;
	do {
		val = brcm_sata_phy_rd(base, BLOCK0_REG_BANK,
					BLOCK0_XGXSSTATUS);
		if (val & BLOCK0_XGXSSTATUS_PLL_LOCK)
			break;
		msleep(20);
		try--;
	} while (try);

	if ((val & BLOCK0_XGXSSTATUS_PLL_LOCK) == 0) {
		/* PLL did not lock; give up */
		dev_err(dev, "port%d PLL did not lock\n", port->portnum);
		return -ETIMEDOUT;
	}

	/* Invert Tx polarity */
	brcm_sata_phy_wr(base, TX_REG_BANK, TX_ACTRL0,
			 ~TX_ACTRL0_TXPOL_FLIP, TX_ACTRL0_TXPOL_FLIP);

	/* Configure OOB control to handle 100MHz reference clock */
	val = ((0xc << OOB_CTRL1_BURST_MAX_SHIFT) |
		(0x4 << OOB_CTRL1_BURST_MIN_SHIFT) |
		(0x8 << OOB_CTRL1_WAKE_IDLE_MAX_SHIFT) |
		(0x3 << OOB_CTRL1_WAKE_IDLE_MIN_SHIFT));
	brcm_sata_phy_wr(base, OOB_REG_BANK, OOB_CTRL1, 0x0, val);
	val = ((0x1b << OOB_CTRL2_RESET_IDLE_MAX_SHIFT) |
		(0x2 << OOB_CTRL2_BURST_CNT_SHIFT) |
		(0x9 << OOB_CTRL2_RESET_IDLE_MIN_SHIFT));
	brcm_sata_phy_wr(base, OOB_REG_BANK, OOB_CTRL2, 0x0, val);

	return 0;
}

static int brcm_sata_phy_init(struct phy *phy)
{
	int rc;
	struct brcm_sata_port *port = phy_get_drvdata(phy);

	switch (port->phy_priv->version) {
	case BRCM_SATA_PHY_STB_28NM:
	case BRCM_SATA_PHY_STB_40NM:
		rc = brcm_stb_sata_init(port);
		break;
	case BRCM_SATA_PHY_IPROC_NS2:
		rc = brcm_ns2_sata_init(port);
		break;
	case BRCM_SATA_PHY_IPROC_NSP:
		rc = brcm_nsp_sata_init(port);
		break;
	case BRCM_SATA_PHY_IPROC_SR:
		rc = brcm_sr_sata_init(port);
		break;
	default:
		rc = -ENODEV;
	}

	return rc;
}

static const struct phy_ops phy_ops = {
	.init		= brcm_sata_phy_init,
	.owner		= THIS_MODULE,
};

static const struct of_device_id brcm_sata_phy_of_match[] = {
	{ .compatible	= "brcm,bcm7445-sata-phy",
	  .data = (void *)BRCM_SATA_PHY_STB_28NM },
	{ .compatible	= "brcm,bcm7425-sata-phy",
	  .data = (void *)BRCM_SATA_PHY_STB_40NM },
	{ .compatible	= "brcm,iproc-ns2-sata-phy",
	  .data = (void *)BRCM_SATA_PHY_IPROC_NS2 },
	{ .compatible = "brcm,iproc-nsp-sata-phy",
	  .data = (void *)BRCM_SATA_PHY_IPROC_NSP },
	{ .compatible	= "brcm,iproc-sr-sata-phy",
	  .data = (void *)BRCM_SATA_PHY_IPROC_SR },
	{},
};
MODULE_DEVICE_TABLE(of, brcm_sata_phy_of_match);

static int brcm_sata_phy_probe(struct platform_device *pdev)
{
	const char *rxaeq_mode;
	struct device *dev = &pdev->dev;
	struct device_node *dn = dev->of_node, *child;
	const struct of_device_id *of_id;
	struct brcm_sata_phy *priv;
	struct resource *res;
	struct phy_provider *provider;
	int ret, count = 0;

	if (of_get_child_count(dn) == 0)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_set_drvdata(dev, priv);
	priv->dev = dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy");
	priv->phy_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->phy_base))
		return PTR_ERR(priv->phy_base);

	of_id = of_match_node(brcm_sata_phy_of_match, dn);
	if (of_id)
		priv->version = (enum brcm_sata_phy_version)of_id->data;
	else
		priv->version = BRCM_SATA_PHY_STB_28NM;

	if (priv->version == BRCM_SATA_PHY_IPROC_NS2) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "phy-ctrl");
		priv->ctrl_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->ctrl_base))
			return PTR_ERR(priv->ctrl_base);
	}

	for_each_available_child_of_node(dn, child) {
		unsigned int id;
		struct brcm_sata_port *port;

		if (of_property_read_u32(child, "reg", &id)) {
			dev_err(dev, "missing reg property in node %s\n",
					child->name);
			ret = -EINVAL;
			goto put_child;
		}

		if (id >= MAX_PORTS) {
			dev_err(dev, "invalid reg: %u\n", id);
			ret = -EINVAL;
			goto put_child;
		}
		if (priv->phys[id].phy) {
			dev_err(dev, "already registered port %u\n", id);
			ret = -EINVAL;
			goto put_child;
		}

		port = &priv->phys[id];
		port->portnum = id;
		port->phy_priv = priv;
		port->phy = devm_phy_create(dev, child, &phy_ops);
		port->rxaeq_mode = RXAEQ_MODE_OFF;
		if (!of_property_read_string(child, "brcm,rxaeq-mode",
					     &rxaeq_mode))
			port->rxaeq_mode = rxaeq_to_val(rxaeq_mode);
		if (port->rxaeq_mode == RXAEQ_MODE_MANUAL)
			of_property_read_u32(child, "brcm,rxaeq-value",
					     &port->rxaeq_val);
		port->ssc_en = of_property_read_bool(child, "brcm,enable-ssc");
		if (IS_ERR(port->phy)) {
			dev_err(dev, "failed to create PHY\n");
			ret = PTR_ERR(port->phy);
			goto put_child;
		}

		phy_set_drvdata(port->phy, port);
		count++;
	}

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "could not register PHY provider\n");
		return PTR_ERR(provider);
	}

	dev_info(dev, "registered %d port(s)\n", count);

	return 0;
put_child:
	of_node_put(child);
	return ret;
}

static struct platform_driver brcm_sata_phy_driver = {
	.probe	= brcm_sata_phy_probe,
	.driver	= {
		.of_match_table	= brcm_sata_phy_of_match,
		.name		= "brcm-sata-phy",
	}
};
module_platform_driver(brcm_sata_phy_driver);

MODULE_DESCRIPTION("Broadcom SATA PHY driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Carino");
MODULE_AUTHOR("Brian Norris");
MODULE_ALIAS("platform:phy-brcm-sata");
