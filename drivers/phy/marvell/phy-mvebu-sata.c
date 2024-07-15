// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	phy-mvebu-sata.c: SATA Phy driver for the Marvell mvebu SoCs.
 *
 *	Copyright (C) 2013 Andrew Lunn <andrew@lunn.ch>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/phy/phy.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

struct priv {
	struct clk	*clk;
	void __iomem	*base;
};

#define SATA_PHY_MODE_2	0x0330
#define  MODE_2_FORCE_PU_TX	BIT(0)
#define  MODE_2_FORCE_PU_RX	BIT(1)
#define  MODE_2_PU_PLL		BIT(2)
#define  MODE_2_PU_IVREF	BIT(3)
#define SATA_IF_CTRL	0x0050
#define  CTRL_PHY_SHUTDOWN	BIT(9)

static int phy_mvebu_sata_power_on(struct phy *phy)
{
	struct priv *priv = phy_get_drvdata(phy);
	u32 reg;

	clk_prepare_enable(priv->clk);

	/* Enable PLL and IVREF */
	reg = readl(priv->base + SATA_PHY_MODE_2);
	reg |= (MODE_2_FORCE_PU_TX | MODE_2_FORCE_PU_RX |
		MODE_2_PU_PLL | MODE_2_PU_IVREF);
	writel(reg , priv->base + SATA_PHY_MODE_2);

	/* Enable PHY */
	reg = readl(priv->base + SATA_IF_CTRL);
	reg &= ~CTRL_PHY_SHUTDOWN;
	writel(reg, priv->base + SATA_IF_CTRL);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int phy_mvebu_sata_power_off(struct phy *phy)
{
	struct priv *priv = phy_get_drvdata(phy);
	u32 reg;

	clk_prepare_enable(priv->clk);

	/* Disable PLL and IVREF */
	reg = readl(priv->base + SATA_PHY_MODE_2);
	reg &= ~(MODE_2_FORCE_PU_TX | MODE_2_FORCE_PU_RX |
		 MODE_2_PU_PLL | MODE_2_PU_IVREF);
	writel(reg, priv->base + SATA_PHY_MODE_2);

	/* Disable PHY */
	reg = readl(priv->base + SATA_IF_CTRL);
	reg |= CTRL_PHY_SHUTDOWN;
	writel(reg, priv->base + SATA_IF_CTRL);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct phy_ops phy_mvebu_sata_ops = {
	.power_on	= phy_mvebu_sata_power_on,
	.power_off	= phy_mvebu_sata_power_off,
	.owner		= THIS_MODULE,
};

static int phy_mvebu_sata_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct priv *priv;
	struct phy *phy;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get(&pdev->dev, "sata");
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	phy = devm_phy_create(&pdev->dev, NULL, &phy_mvebu_sata_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	phy_set_drvdata(phy, priv);

	phy_provider = devm_of_phy_provider_register(&pdev->dev,
						     of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	/* The boot loader may of left it on. Turn it off. */
	phy_mvebu_sata_power_off(phy);

	return 0;
}

static const struct of_device_id phy_mvebu_sata_of_match[] = {
	{ .compatible = "marvell,mvebu-sata-phy" },
	{ },
};

static struct platform_driver phy_mvebu_sata_driver = {
	.probe	= phy_mvebu_sata_probe,
	.driver = {
		.name	= "phy-mvebu-sata",
		.of_match_table	= phy_mvebu_sata_of_match,
	}
};
builtin_platform_driver(phy_mvebu_sata_driver);
