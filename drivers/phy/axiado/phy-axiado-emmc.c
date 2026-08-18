// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Axiado eMMC PHY driver
 *
 * Copyright (C) 2017 Arasan Chip Systems Inc.
 * Copyright (C) 2022-2026 Axiado Corporation (or its affiliates).
 *
 * Based on Arasan Driver (sdhci-pci-arasan.c)
 * sdhci-pci-arasan.c - Driver for Arasan PCI Controller with integrated phy.
 */
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

/* Arasan eMMC 5.1 - PHY configuration registers */
#define CAP_REG_IN_S1_MSB		0x04
#define PHY_CTRL_1			0x38
#define PHY_CTRL_2			0x3c
#define PHY_CTRL_3			0x40
#define STATUS				0x50

#define DLL_ENBL	BIT(26)
#define RTRIM_EN	BIT(21)
#define PDB_ENBL	BIT(23)
#define RETB_ENBL	BIT(1)

#define REN_STRB	BIT(27)
#define REN_CMD_EN	GENMASK(20, 12)

/* Pull-UP Enable on CMD Line */
#define PU_CMD_EN	GENMASK(11, 3)

/* Selection value for the optimum delay from 1-32 output tap lines */
#define OTAP_DLY	0x02
/* DLL charge pump current trim default [1000] */
#define DLL_TRM_ICP	0x08
/* Select the frequency range of DLL Operation */
#define FRQ_SEL	0x01

#define OTAP_SEL_MASK		GENMASK(10, 7)
#define DLL_TRM_MASK		GENMASK(25, 22)
#define DLL_FRQSEL_MASK		GENMASK(27, 25)

#define OTAP_SEL(x)		(FIELD_PREP(OTAP_SEL_MASK, x) | OTAPDLY_EN)
#define DLL_TRM(x)		(FIELD_PREP(DLL_TRM_MASK, x) | DLL_ENBL)
#define DLL_FRQSEL(x)	FIELD_PREP(DLL_FRQSEL_MASK, x)

#define OTAPDLY_EN	BIT(11)

#define SEL_DLY_RXCLK	BIT(18)
#define SEL_DLY_TXCLK	BIT(19)

#define CALDONE_MASK	0x40
#define DLL_RDY_MASK	0x1
#define MAX_CLK_BUF0	BIT(20)
#define MAX_CLK_BUF1	BIT(21)
#define MAX_CLK_BUF2	BIT(22)

#define CLK_MULTIPLIER	0xc008e
#define POLL_TIMEOUT_MS	3000
#define POLL_DELAY_US	100

struct axiado_emmc_phy {
	void __iomem *reg_base;
	struct device *dev;
};

static int axiado_emmc_phy_init(struct phy *phy)
{
	struct axiado_emmc_phy *ax_phy = phy_get_drvdata(phy);
	struct device *dev = ax_phy->dev;
	u32 val;
	int ret;

	val = readl(ax_phy->reg_base + PHY_CTRL_1);
	writel(val | RETB_ENBL | RTRIM_EN, ax_phy->reg_base + PHY_CTRL_1);

	val = readl(ax_phy->reg_base + PHY_CTRL_3);
	writel(val | PDB_ENBL, ax_phy->reg_base + PHY_CTRL_3);

	ret = readl_poll_timeout(ax_phy->reg_base + STATUS, val,
				 val & CALDONE_MASK, POLL_DELAY_US,
				 POLL_TIMEOUT_MS * 1000);
	if (ret) {
		dev_err(dev, "PHY calibration timeout\n");
		return ret;
	}

	val = readl(ax_phy->reg_base + PHY_CTRL_1);
	writel(val | REN_CMD_EN | PU_CMD_EN, ax_phy->reg_base + PHY_CTRL_1);

	val = readl(ax_phy->reg_base + PHY_CTRL_2);
	writel(val | REN_STRB, ax_phy->reg_base + PHY_CTRL_2);

	val = readl(ax_phy->reg_base + PHY_CTRL_3);
	writel(val | MAX_CLK_BUF0 | MAX_CLK_BUF1 | MAX_CLK_BUF2,
	       ax_phy->reg_base + PHY_CTRL_3);

	writel(CLK_MULTIPLIER, ax_phy->reg_base + CAP_REG_IN_S1_MSB);

	val = readl(ax_phy->reg_base + PHY_CTRL_3);
	writel(val | SEL_DLY_RXCLK | SEL_DLY_TXCLK,
	       ax_phy->reg_base + PHY_CTRL_3);

	return 0;
}

static int axiado_emmc_phy_power_on(struct phy *phy)
{
	struct axiado_emmc_phy *ax_phy = phy_get_drvdata(phy);
	struct device *dev = ax_phy->dev;
	u32 val;
	int ret;

	val = readl(ax_phy->reg_base + PHY_CTRL_1);
	writel(val | RETB_ENBL, ax_phy->reg_base + PHY_CTRL_1);

	val = readl(ax_phy->reg_base + PHY_CTRL_3);
	writel(val | PDB_ENBL, ax_phy->reg_base + PHY_CTRL_3);

	val = readl(ax_phy->reg_base + PHY_CTRL_2);
	writel(val | OTAP_SEL(OTAP_DLY), ax_phy->reg_base + PHY_CTRL_2);

	val = readl(ax_phy->reg_base + PHY_CTRL_1);
	writel(val | DLL_TRM(DLL_TRM_ICP), ax_phy->reg_base + PHY_CTRL_1);

	val = readl(ax_phy->reg_base + PHY_CTRL_3);
	writel(val | DLL_FRQSEL(FRQ_SEL), ax_phy->reg_base + PHY_CTRL_3);

	ret = read_poll_timeout(readl, val, val & DLL_RDY_MASK, POLL_DELAY_US,
				POLL_TIMEOUT_MS * 1000, false,
				ax_phy->reg_base + STATUS);
	if (ret) {
		dev_err(dev, "DLL ready timeout\n");
		return ret;
	}

	return 0;
}

static int axiado_emmc_phy_power_off(struct phy *phy)
{
	struct axiado_emmc_phy *ax_phy = phy_get_drvdata(phy);
	u32 val;

	val = readl(ax_phy->reg_base + PHY_CTRL_1);
	val &= ~(DLL_TRM_MASK | DLL_ENBL);
	writel(val, ax_phy->reg_base + PHY_CTRL_1);

	val = readl(ax_phy->reg_base + PHY_CTRL_3);
	val &= ~(DLL_FRQSEL_MASK | PDB_ENBL);
	writel(val, ax_phy->reg_base + PHY_CTRL_3);

	return 0;
}

static const struct phy_ops axiado_emmc_phy_ops = {
	.init = axiado_emmc_phy_init,
	.power_on = axiado_emmc_phy_power_on,
	.power_off = axiado_emmc_phy_power_off,
	.owner = THIS_MODULE,
};

static const struct of_device_id axiado_emmc_phy_of_match[] = {
	{ .compatible = "axiado,ax3000-emmc-phy" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, axiado_emmc_phy_of_match);

static int axiado_emmc_phy_probe(struct platform_device *pdev)
{
	struct axiado_emmc_phy *ax_phy;
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct phy *generic_phy;

	if (!dev->of_node)
		return -ENODEV;

	ax_phy = devm_kzalloc(dev, sizeof(*ax_phy), GFP_KERNEL);
	if (!ax_phy)
		return -ENOMEM;

	ax_phy->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ax_phy->reg_base))
		return PTR_ERR(ax_phy->reg_base);

	ax_phy->dev = dev;

	generic_phy = devm_phy_create(dev, dev->of_node, &axiado_emmc_phy_ops);
	if (IS_ERR(generic_phy))
		return dev_err_probe(dev, PTR_ERR(generic_phy),
				     "failed to create PHY\n");

	phy_set_drvdata(generic_phy, ax_phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver axiado_emmc_phy_driver = {
	.probe = axiado_emmc_phy_probe,
	.driver = {
		.name = "axiado-emmc-phy",
		.of_match_table = axiado_emmc_phy_of_match,
	},
};
module_platform_driver(axiado_emmc_phy_driver);

MODULE_DESCRIPTION("AX3000 eMMC PHY Driver");
MODULE_AUTHOR("Axiado Corporation");
MODULE_LICENSE("GPL");
