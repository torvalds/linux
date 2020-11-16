// SPDX-License-Identifier: GPL-2.0
/*
 * Amlogic AXG MIPI + PCIE analog PHY driver
 *
 * Copyright (C) 2019 Remi Pommarel <repk@triplefau.lt>
 */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <dt-bindings/phy/phy.h>

#define HHI_MIPI_CNTL0 0x00
#define		HHI_MIPI_CNTL0_COMMON_BLOCK	GENMASK(31, 28)
#define		HHI_MIPI_CNTL0_ENABLE		BIT(29)
#define		HHI_MIPI_CNTL0_BANDGAP		BIT(26)
#define		HHI_MIPI_CNTL0_DIF_REF_CTL1	GENMASK(25, 16)
#define		HHI_MIPI_CNTL0_DIF_REF_CTL0	GENMASK(15, 0)

#define HHI_MIPI_CNTL1 0x04
#define		HHI_MIPI_CNTL1_CH0_CML_PDR_EN	BIT(12)
#define		HHI_MIPI_CNTL1_LP_ABILITY	GENMASK(5, 4)
#define		HHI_MIPI_CNTL1_LP_RESISTER	BIT(3)
#define		HHI_MIPI_CNTL1_INPUT_SETTING	BIT(2)
#define		HHI_MIPI_CNTL1_INPUT_SEL	BIT(1)
#define		HHI_MIPI_CNTL1_PRBS7_EN		BIT(0)

#define HHI_MIPI_CNTL2 0x08
#define		HHI_MIPI_CNTL2_CH_PU		GENMASK(31, 25)
#define		HHI_MIPI_CNTL2_CH_CTL		GENMASK(24, 19)
#define		HHI_MIPI_CNTL2_CH0_DIGDR_EN	BIT(18)
#define		HHI_MIPI_CNTL2_CH_DIGDR_EN	BIT(17)
#define		HHI_MIPI_CNTL2_LPULPS_EN	BIT(16)
#define		HHI_MIPI_CNTL2_CH_EN		GENMASK(15, 11)
#define		HHI_MIPI_CNTL2_CH0_LP_CTL	GENMASK(10, 1)

#define DSI_LANE_0              (1 << 4)
#define DSI_LANE_1              (1 << 3)
#define DSI_LANE_CLK            (1 << 2)
#define DSI_LANE_2              (1 << 1)
#define DSI_LANE_3              (1 << 0)
#define DSI_LANE_MASK		(0x1F)

struct phy_axg_mipi_pcie_analog_priv {
	struct phy *phy;
	struct regmap *regmap;
	bool dsi_configured;
	bool dsi_enabled;
	bool powered;
	struct phy_configure_opts_mipi_dphy config;
};

static void phy_bandgap_enable(struct phy_axg_mipi_pcie_analog_priv *priv)
{
	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			HHI_MIPI_CNTL0_BANDGAP, HHI_MIPI_CNTL0_BANDGAP);

	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			HHI_MIPI_CNTL0_ENABLE, HHI_MIPI_CNTL0_ENABLE);
}

static void phy_bandgap_disable(struct phy_axg_mipi_pcie_analog_priv *priv)
{
	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			HHI_MIPI_CNTL0_BANDGAP, 0);
	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			HHI_MIPI_CNTL0_ENABLE, 0);
}

static void phy_dsi_analog_enable(struct phy_axg_mipi_pcie_analog_priv *priv)
{
	u32 reg;

	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			   HHI_MIPI_CNTL0_DIF_REF_CTL1,
			   FIELD_PREP(HHI_MIPI_CNTL0_DIF_REF_CTL1, 0x1b8));
	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			   BIT(31), BIT(31));
	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			   HHI_MIPI_CNTL0_DIF_REF_CTL0,
			   FIELD_PREP(HHI_MIPI_CNTL0_DIF_REF_CTL0, 0x8));

	regmap_write(priv->regmap, HHI_MIPI_CNTL1, 0x001e);

	regmap_write(priv->regmap, HHI_MIPI_CNTL2,
		     (0x26e0 << 16) | (0x459 << 0));

	reg = DSI_LANE_CLK;
	switch (priv->config.lanes) {
	case 4:
		reg |= DSI_LANE_3;
		fallthrough;
	case 3:
		reg |= DSI_LANE_2;
		fallthrough;
	case 2:
		reg |= DSI_LANE_1;
		fallthrough;
	case 1:
		reg |= DSI_LANE_0;
		break;
	default:
		reg = 0;
	}

	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL2,
			   HHI_MIPI_CNTL2_CH_EN,
			   FIELD_PREP(HHI_MIPI_CNTL2_CH_EN, reg));

	priv->dsi_enabled = true;
}

static void phy_dsi_analog_disable(struct phy_axg_mipi_pcie_analog_priv *priv)
{
	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			HHI_MIPI_CNTL0_DIF_REF_CTL1,
			FIELD_PREP(HHI_MIPI_CNTL0_DIF_REF_CTL1, 0));
	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0, BIT(31), 0);
	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL0,
			HHI_MIPI_CNTL0_DIF_REF_CTL1, 0);

	regmap_write(priv->regmap, HHI_MIPI_CNTL1, 0x6);

	regmap_write(priv->regmap, HHI_MIPI_CNTL2, 0x00200000);

	priv->dsi_enabled = false;
}

static int phy_axg_mipi_pcie_analog_configure(struct phy *phy,
					      union phy_configure_opts *opts)
{
	struct phy_axg_mipi_pcie_analog_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = phy_mipi_dphy_config_validate(&opts->mipi_dphy);
	if (ret)
		return ret;

	memcpy(&priv->config, opts, sizeof(priv->config));

	priv->dsi_configured = true;

	/* If PHY was already powered on, setup the DSI analog part */
	if (priv->powered) {
		/* If reconfiguring, disable & reconfigure */
		if (priv->dsi_enabled)
			phy_dsi_analog_disable(priv);

		usleep_range(100, 200);

		phy_dsi_analog_enable(priv);
	}

	return 0;
}

static int phy_axg_mipi_pcie_analog_power_on(struct phy *phy)
{
	struct phy_axg_mipi_pcie_analog_priv *priv = phy_get_drvdata(phy);

	phy_bandgap_enable(priv);

	if (priv->dsi_configured)
		phy_dsi_analog_enable(priv);

	priv->powered = true;

	return 0;
}

static int phy_axg_mipi_pcie_analog_power_off(struct phy *phy)
{
	struct phy_axg_mipi_pcie_analog_priv *priv = phy_get_drvdata(phy);

	phy_bandgap_disable(priv);

	if (priv->dsi_enabled)
		phy_dsi_analog_disable(priv);

	priv->powered = false;

	return 0;
}

static const struct phy_ops phy_axg_mipi_pcie_analog_ops = {
	.configure = phy_axg_mipi_pcie_analog_configure,
	.power_on = phy_axg_mipi_pcie_analog_power_on,
	.power_off = phy_axg_mipi_pcie_analog_power_off,
	.owner = THIS_MODULE,
};

static int phy_axg_mipi_pcie_analog_probe(struct platform_device *pdev)
{
	struct phy_provider *phy;
	struct device *dev = &pdev->dev;
	struct phy_axg_mipi_pcie_analog_priv *priv;
	struct device_node *np = dev->of_node;
	struct regmap *map;
	int ret;

	priv = devm_kmalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Get the hhi system controller node */
	map = syscon_node_to_regmap(of_get_parent(dev->of_node));
	if (IS_ERR(map)) {
		dev_err(dev,
			"failed to get HHI regmap\n");
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

	phy = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

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
