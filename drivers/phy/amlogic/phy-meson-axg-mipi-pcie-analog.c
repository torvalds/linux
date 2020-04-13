// SPDX-License-Identifier: GPL-2.0
/*
 * Amlogic AXG MIPI + PCIE analog PHY driver
 *
 * Copyright (C) 2019 Remi Pommarel <repk@triplefau.lt>
 */
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <dt-bindings/phy/phy.h>

#define HHI_MIPI_CNTL0 0x00
#define		HHI_MIPI_CNTL0_COMMON_BLOCK	GENMASK(31, 28)
#define		HHI_MIPI_CNTL0_ENABLE		BIT(29)
#define		HHI_MIPI_CNTL0_BANDGAP		BIT(26)
#define		HHI_MIPI_CNTL0_DECODE_TO_RTERM	GENMASK(15, 12)
#define		HHI_MIPI_CNTL0_OUTPUT_EN	BIT(3)

#define HHI_MIPI_CNTL1 0x01
#define		HHI_MIPI_CNTL1_CH0_CML_PDR_EN	BIT(12)
#define		HHI_MIPI_CNTL1_LP_ABILITY	GENMASK(5, 4)
#define		HHI_MIPI_CNTL1_LP_RESISTER	BIT(3)
#define		HHI_MIPI_CNTL1_INPUT_SETTING	BIT(2)
#define		HHI_MIPI_CNTL1_INPUT_SEL	BIT(1)
#define		HHI_MIPI_CNTL1_PRBS7_EN		BIT(0)

#define HHI_MIPI_CNTL2 0x02
#define		HHI_MIPI_CNTL2_CH_PU		GENMASK(31, 25)
#define		HHI_MIPI_CNTL2_CH_CTL		GENMASK(24, 19)
#define		HHI_MIPI_CNTL2_CH0_DIGDR_EN	BIT(18)
#define		HHI_MIPI_CNTL2_CH_DIGDR_EN	BIT(17)
#define		HHI_MIPI_CNTL2_LPULPS_EN	BIT(16)
#define		HHI_MIPI_CNTL2_CH_EN(n)		BIT(15 - (n))
#define		HHI_MIPI_CNTL2_CH0_LP_CTL	GENMASK(10, 1)

struct phy_axg_mipi_pcie_analog_priv {
	struct phy *phy;
	unsigned int mode;
	struct regmap *regmap;
};

static const struct regmap_config phy_axg_mipi_pcie_analog_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = HHI_MIPI_CNTL2,
};

static int phy_axg_mipi_pcie_analog_power_on(struct phy *phy)
{
	struct phy_axg_mipi_pcie_analog_priv *priv = phy_get_drvdata(phy);

	/* MIPI not supported yet */
	if (priv->mode != PHY_TYPE_PCIE)
		return -EINVAL;

	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			   HHI_MIPI_CNTL0_BANDGAP, HHI_MIPI_CNTL0_BANDGAP);

	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			   HHI_MIPI_CNTL0_ENABLE, HHI_MIPI_CNTL0_ENABLE);
	return 0;
}

static int phy_axg_mipi_pcie_analog_power_off(struct phy *phy)
{
	struct phy_axg_mipi_pcie_analog_priv *priv = phy_get_drvdata(phy);

	/* MIPI not supported yet */
	if (priv->mode != PHY_TYPE_PCIE)
		return -EINVAL;

	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			   HHI_MIPI_CNTL0_BANDGAP, 0);
	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			   HHI_MIPI_CNTL0_ENABLE, 0);
	return 0;
}

static int phy_axg_mipi_pcie_analog_init(struct phy *phy)
{
	return 0;
}

static int phy_axg_mipi_pcie_analog_exit(struct phy *phy)
{
	return 0;
}

static const struct phy_ops phy_axg_mipi_pcie_analog_ops = {
	.init = phy_axg_mipi_pcie_analog_init,
	.exit = phy_axg_mipi_pcie_analog_exit,
	.power_on = phy_axg_mipi_pcie_analog_power_on,
	.power_off = phy_axg_mipi_pcie_analog_power_off,
	.owner = THIS_MODULE,
};

static struct phy *phy_axg_mipi_pcie_analog_xlate(struct device *dev,
						  struct of_phandle_args *args)
{
	struct phy_axg_mipi_pcie_analog_priv *priv = dev_get_drvdata(dev);
	unsigned int mode;

	if (args->args_count != 1) {
		dev_err(dev, "invalid number of arguments\n");
		return ERR_PTR(-EINVAL);
	}

	mode = args->args[0];

	/* MIPI mode is not supported yet */
	if (mode != PHY_TYPE_PCIE) {
		dev_err(dev, "invalid phy mode select argument\n");
		return ERR_PTR(-EINVAL);
	}

	priv->mode = mode;
	return priv->phy;
}

static int phy_axg_mipi_pcie_analog_probe(struct platform_device *pdev)
{
	struct phy_provider *phy;
	struct device *dev = &pdev->dev;
	struct phy_axg_mipi_pcie_analog_priv *priv;
	struct device_node *np = dev->of_node;
	struct regmap *map;
	struct resource *res;
	void __iomem *base;
	int ret;

	priv = devm_kmalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base)) {
		dev_err(dev, "failed to get regmap base\n");
		return PTR_ERR(base);
	}

	map = devm_regmap_init_mmio(dev, base,
				    &phy_axg_mipi_pcie_analog_regmap_conf);
	if (IS_ERR(map)) {
		dev_err(dev, "failed to get HHI regmap\n");
		return PTR_ERR(map);
	}
	priv->regmap = map;

	priv->phy = devm_phy_create(dev, np, &phy_axg_mipi_pcie_analog_ops);
	if (IS_ERR(priv->phy)) {
		ret = PTR_ERR(priv->phy);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to create PHY\n");
		return ret;
	}

	phy_set_drvdata(priv->phy, priv);
	dev_set_drvdata(dev, priv);

	phy = devm_of_phy_provider_register(dev,
					    phy_axg_mipi_pcie_analog_xlate);

	return PTR_ERR_OR_ZERO(phy);
}

static const struct of_device_id phy_axg_mipi_pcie_analog_of_match[] = {
	{
		.compatible = "amlogic,axg-mipi-pcie-analog-phy",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, phy_axg_mipi_pcie_analog_of_match);

static struct platform_driver phy_axg_mipi_pcie_analog_driver = {
	.probe = phy_axg_mipi_pcie_analog_probe,
	.driver = {
		.name = "phy-axg-mipi-pcie-analog",
		.of_match_table = phy_axg_mipi_pcie_analog_of_match,
	},
};
module_platform_driver(phy_axg_mipi_pcie_analog_driver);

MODULE_AUTHOR("Remi Pommarel <repk@triplefau.lt>");
MODULE_DESCRIPTION("Amlogic AXG MIPI + PCIE analog PHY driver");
MODULE_LICENSE("GPL v2");
