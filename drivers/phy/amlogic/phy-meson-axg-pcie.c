// SPDX-License-Identifier: GPL-2.0
/*
 * Amlogic AXG PCIE PHY driver
 *
 * Copyright (C) 2020 Remi Pommarel <repk@triplefau.lt>
 */
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/platform_device.h>
#include <linux/bitfield.h>
#include <dt-bindings/phy/phy.h>

#define MESON_PCIE_REG0 0x00
#define		MESON_PCIE_COMMON_CLK	BIT(4)
#define		MESON_PCIE_PORT_SEL	GENMASK(3, 2)
#define		MESON_PCIE_CLK		BIT(1)
#define		MESON_PCIE_POWERDOWN	BIT(0)

#define MESON_PCIE_TWO_X1		FIELD_PREP(MESON_PCIE_PORT_SEL, 0x3)
#define MESON_PCIE_COMMON_REF_CLK	FIELD_PREP(MESON_PCIE_COMMON_CLK, 0x1)
#define MESON_PCIE_PHY_INIT		(MESON_PCIE_TWO_X1 |		\
					 MESON_PCIE_COMMON_REF_CLK)
#define MESON_PCIE_RESET_DELAY		500

struct phy_axg_pcie_priv {
	struct phy *phy;
	struct phy *analog;
	struct regmap *regmap;
	struct reset_control *reset;
};

static const struct regmap_config phy_axg_pcie_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = MESON_PCIE_REG0,
};

static int phy_axg_pcie_power_on(struct phy *phy)
{
	struct phy_axg_pcie_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = phy_power_on(priv->analog);
	if (ret != 0)
		return ret;

	regmap_update_bits(priv->regmap, MESON_PCIE_REG0,
			   MESON_PCIE_POWERDOWN, 0);
	return 0;
}

static int phy_axg_pcie_power_off(struct phy *phy)
{
	struct phy_axg_pcie_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = phy_power_off(priv->analog);
	if (ret != 0)
		return ret;

	regmap_update_bits(priv->regmap, MESON_PCIE_REG0,
			   MESON_PCIE_POWERDOWN, 1);
	return 0;
}

static int phy_axg_pcie_init(struct phy *phy)
{
	struct phy_axg_pcie_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = phy_init(priv->analog);
	if (ret != 0)
		return ret;

	regmap_write(priv->regmap, MESON_PCIE_REG0, MESON_PCIE_PHY_INIT);
	return reset_control_reset(priv->reset);
}

static int phy_axg_pcie_exit(struct phy *phy)
{
	struct phy_axg_pcie_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = phy_exit(priv->analog);
	if (ret != 0)
		return ret;

	return reset_control_reset(priv->reset);
}

static int phy_axg_pcie_reset(struct phy *phy)
{
	struct phy_axg_pcie_priv *priv = phy_get_drvdata(phy);
	int ret = 0;

	ret = phy_reset(priv->analog);
	if (ret != 0)
		goto out;

	ret = reset_control_assert(priv->reset);
	if (ret != 0)
		goto out;
	udelay(MESON_PCIE_RESET_DELAY);

	ret = reset_control_deassert(priv->reset);
	if (ret != 0)
		goto out;
	udelay(MESON_PCIE_RESET_DELAY);

out:
	return ret;
}

static const struct phy_ops phy_axg_pcie_ops = {
	.init = phy_axg_pcie_init,
	.exit = phy_axg_pcie_exit,
	.power_on = phy_axg_pcie_power_on,
	.power_off = phy_axg_pcie_power_off,
	.reset = phy_axg_pcie_reset,
	.owner = THIS_MODULE,
};

static int phy_axg_pcie_probe(struct platform_device *pdev)
{
	struct phy_provider *pphy;
	struct device *dev = &pdev->dev;
	struct phy_axg_pcie_priv *priv;
	struct device_node *np = dev->of_node;
	struct resource *res;
	void __iomem *base;
	int ret;

	priv = devm_kmalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->phy = devm_phy_create(dev, np, &phy_axg_pcie_ops);
	if (IS_ERR(priv->phy)) {
		ret = PTR_ERR(priv->phy);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to create PHY\n");
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->regmap = devm_regmap_init_mmio(dev, base,
					     &phy_axg_pcie_regmap_conf);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->reset = devm_reset_control_array_get(dev, false, false);
	if (IS_ERR(priv->reset))
		return PTR_ERR(priv->reset);

	priv->analog = devm_phy_get(dev, "analog");
	if (IS_ERR(priv->analog))
		return PTR_ERR(priv->analog);

	phy_set_drvdata(priv->phy, priv);
	dev_set_drvdata(dev, priv);
	pphy = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(pphy);
}

static const struct of_device_id phy_axg_pcie_of_match[] = {
	{
		.compatible = "amlogic,axg-pcie-phy",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, phy_axg_pcie_of_match);

static struct platform_driver phy_axg_pcie_driver = {
	.probe = phy_axg_pcie_probe,
	.driver = {
		.name = "phy-axg-pcie",
		.of_match_table = phy_axg_pcie_of_match,
	},
};
module_platform_driver(phy_axg_pcie_driver);

MODULE_AUTHOR("Remi Pommarel <repk@triplefau.lt>");
MODULE_DESCRIPTION("Amlogic AXG PCIE PHY driver");
MODULE_LICENSE("GPL v2");
