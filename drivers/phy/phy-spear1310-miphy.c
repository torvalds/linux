/*
 * ST SPEAr1310-miphy driver
 *
 * Copyright (C) 2014 ST Microelectronics
 * Pratyush Anand <pratyush.anand@st.com>
 * Mohit Kumar <mohit.kumar@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>

/* SPEAr1310 Registers */
#define SPEAR1310_PCIE_SATA_CFG			0x3A4
	#define SPEAR1310_PCIE_SATA2_SEL_PCIE		(0 << 31)
	#define SPEAR1310_PCIE_SATA1_SEL_PCIE		(0 << 30)
	#define SPEAR1310_PCIE_SATA0_SEL_PCIE		(0 << 29)
	#define SPEAR1310_PCIE_SATA2_SEL_SATA		BIT(31)
	#define SPEAR1310_PCIE_SATA1_SEL_SATA		BIT(30)
	#define SPEAR1310_PCIE_SATA0_SEL_SATA		BIT(29)
	#define SPEAR1310_SATA2_CFG_TX_CLK_EN		BIT(27)
	#define SPEAR1310_SATA2_CFG_RX_CLK_EN		BIT(26)
	#define SPEAR1310_SATA2_CFG_POWERUP_RESET	BIT(25)
	#define SPEAR1310_SATA2_CFG_PM_CLK_EN		BIT(24)
	#define SPEAR1310_SATA1_CFG_TX_CLK_EN		BIT(23)
	#define SPEAR1310_SATA1_CFG_RX_CLK_EN		BIT(22)
	#define SPEAR1310_SATA1_CFG_POWERUP_RESET	BIT(21)
	#define SPEAR1310_SATA1_CFG_PM_CLK_EN		BIT(20)
	#define SPEAR1310_SATA0_CFG_TX_CLK_EN		BIT(19)
	#define SPEAR1310_SATA0_CFG_RX_CLK_EN		BIT(18)
	#define SPEAR1310_SATA0_CFG_POWERUP_RESET	BIT(17)
	#define SPEAR1310_SATA0_CFG_PM_CLK_EN		BIT(16)
	#define SPEAR1310_PCIE2_CFG_DEVICE_PRESENT	BIT(11)
	#define SPEAR1310_PCIE2_CFG_POWERUP_RESET	BIT(10)
	#define SPEAR1310_PCIE2_CFG_CORE_CLK_EN		BIT(9)
	#define SPEAR1310_PCIE2_CFG_AUX_CLK_EN		BIT(8)
	#define SPEAR1310_PCIE1_CFG_DEVICE_PRESENT	BIT(7)
	#define SPEAR1310_PCIE1_CFG_POWERUP_RESET	BIT(6)
	#define SPEAR1310_PCIE1_CFG_CORE_CLK_EN		BIT(5)
	#define SPEAR1310_PCIE1_CFG_AUX_CLK_EN		BIT(4)
	#define SPEAR1310_PCIE0_CFG_DEVICE_PRESENT	BIT(3)
	#define SPEAR1310_PCIE0_CFG_POWERUP_RESET	BIT(2)
	#define SPEAR1310_PCIE0_CFG_CORE_CLK_EN		BIT(1)
	#define SPEAR1310_PCIE0_CFG_AUX_CLK_EN		BIT(0)

	#define SPEAR1310_PCIE_CFG_MASK(x) ((0xF << (x * 4)) | BIT((x + 29)))
	#define SPEAR1310_SATA_CFG_MASK(x) ((0xF << (x * 4 + 16)) | \
			BIT((x + 29)))
	#define SPEAR1310_PCIE_CFG_VAL(x) \
			(SPEAR1310_PCIE_SATA##x##_SEL_PCIE | \
			SPEAR1310_PCIE##x##_CFG_AUX_CLK_EN | \
			SPEAR1310_PCIE##x##_CFG_CORE_CLK_EN | \
			SPEAR1310_PCIE##x##_CFG_POWERUP_RESET | \
			SPEAR1310_PCIE##x##_CFG_DEVICE_PRESENT)
	#define SPEAR1310_SATA_CFG_VAL(x) \
			(SPEAR1310_PCIE_SATA##x##_SEL_SATA | \
			SPEAR1310_SATA##x##_CFG_PM_CLK_EN | \
			SPEAR1310_SATA##x##_CFG_POWERUP_RESET | \
			SPEAR1310_SATA##x##_CFG_RX_CLK_EN | \
			SPEAR1310_SATA##x##_CFG_TX_CLK_EN)

#define SPEAR1310_PCIE_MIPHY_CFG_1		0x3A8
	#define SPEAR1310_MIPHY_DUAL_OSC_BYPASS_EXT	BIT(31)
	#define SPEAR1310_MIPHY_DUAL_CLK_REF_DIV2	BIT(28)
	#define SPEAR1310_MIPHY_DUAL_PLL_RATIO_TOP(x)	(x << 16)
	#define SPEAR1310_MIPHY_SINGLE_OSC_BYPASS_EXT	BIT(15)
	#define SPEAR1310_MIPHY_SINGLE_CLK_REF_DIV2	BIT(12)
	#define SPEAR1310_MIPHY_SINGLE_PLL_RATIO_TOP(x)	(x << 0)
	#define SPEAR1310_PCIE_SATA_MIPHY_CFG_SATA_MASK (0xFFFF)
	#define SPEAR1310_PCIE_SATA_MIPHY_CFG_PCIE_MASK (0xFFFF << 16)
	#define SPEAR1310_PCIE_SATA_MIPHY_CFG_SATA \
			(SPEAR1310_MIPHY_DUAL_OSC_BYPASS_EXT | \
			SPEAR1310_MIPHY_DUAL_CLK_REF_DIV2 | \
			SPEAR1310_MIPHY_DUAL_PLL_RATIO_TOP(60) | \
			SPEAR1310_MIPHY_SINGLE_OSC_BYPASS_EXT | \
			SPEAR1310_MIPHY_SINGLE_CLK_REF_DIV2 | \
			SPEAR1310_MIPHY_SINGLE_PLL_RATIO_TOP(60))
	#define SPEAR1310_PCIE_SATA_MIPHY_CFG_SATA_25M_CRYSTAL_CLK \
			(SPEAR1310_MIPHY_SINGLE_PLL_RATIO_TOP(120))
	#define SPEAR1310_PCIE_SATA_MIPHY_CFG_PCIE \
			(SPEAR1310_MIPHY_DUAL_OSC_BYPASS_EXT | \
			SPEAR1310_MIPHY_DUAL_PLL_RATIO_TOP(25) | \
			SPEAR1310_MIPHY_SINGLE_OSC_BYPASS_EXT | \
			SPEAR1310_MIPHY_SINGLE_PLL_RATIO_TOP(25))

#define SPEAR1310_PCIE_MIPHY_CFG_2		0x3AC

enum spear1310_miphy_mode {
	SATA,
	PCIE,
};

struct spear1310_miphy_priv {
	/* instance id of this phy */
	u32				id;
	/* phy mode: 0 for SATA 1 for PCIe */
	enum spear1310_miphy_mode	mode;
	/* regmap for any soc specific misc registers */
	struct regmap			*misc;
	/* phy struct pointer */
	struct phy			*phy;
};

static int spear1310_miphy_pcie_init(struct spear1310_miphy_priv *priv)
{
	u32 val;

	regmap_update_bits(priv->misc, SPEAR1310_PCIE_MIPHY_CFG_1,
			   SPEAR1310_PCIE_SATA_MIPHY_CFG_PCIE_MASK,
			   SPEAR1310_PCIE_SATA_MIPHY_CFG_PCIE);

	switch (priv->id) {
	case 0:
		val = SPEAR1310_PCIE_CFG_VAL(0);
		break;
	case 1:
		val = SPEAR1310_PCIE_CFG_VAL(1);
		break;
	case 2:
		val = SPEAR1310_PCIE_CFG_VAL(2);
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(priv->misc, SPEAR1310_PCIE_SATA_CFG,
			   SPEAR1310_PCIE_CFG_MASK(priv->id), val);

	return 0;
}

static int spear1310_miphy_pcie_exit(struct spear1310_miphy_priv *priv)
{
	regmap_update_bits(priv->misc, SPEAR1310_PCIE_SATA_CFG,
			   SPEAR1310_PCIE_CFG_MASK(priv->id), 0);

	regmap_update_bits(priv->misc, SPEAR1310_PCIE_MIPHY_CFG_1,
			   SPEAR1310_PCIE_SATA_MIPHY_CFG_PCIE_MASK, 0);

	return 0;
}

static int spear1310_miphy_init(struct phy *phy)
{
	struct spear1310_miphy_priv *priv = phy_get_drvdata(phy);
	int ret = 0;

	if (priv->mode == PCIE)
		ret = spear1310_miphy_pcie_init(priv);

	return ret;
}

static int spear1310_miphy_exit(struct phy *phy)
{
	struct spear1310_miphy_priv *priv = phy_get_drvdata(phy);
	int ret = 0;

	if (priv->mode == PCIE)
		ret = spear1310_miphy_pcie_exit(priv);

	return ret;
}

static const struct of_device_id spear1310_miphy_of_match[] = {
	{ .compatible = "st,spear1310-miphy" },
	{ },
};
MODULE_DEVICE_TABLE(of, spear1310_miphy_of_match);

static struct phy_ops spear1310_miphy_ops = {
	.init = spear1310_miphy_init,
	.exit = spear1310_miphy_exit,
	.owner = THIS_MODULE,
};

static struct phy *spear1310_miphy_xlate(struct device *dev,
					 struct of_phandle_args *args)
{
	struct spear1310_miphy_priv *priv = dev_get_drvdata(dev);

	if (args->args_count < 1) {
		dev_err(dev, "DT did not pass correct no of args\n");
		return ERR_PTR(-ENODEV);
	}

	priv->mode = args->args[0];

	if (priv->mode != SATA && priv->mode != PCIE) {
		dev_err(dev, "DT did not pass correct phy mode\n");
		return ERR_PTR(-ENODEV);
	}

	return priv->phy;
}

static int spear1310_miphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spear1310_miphy_priv *priv;
	struct phy_provider *phy_provider;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->misc =
		syscon_regmap_lookup_by_phandle(dev->of_node, "misc");
	if (IS_ERR(priv->misc)) {
		dev_err(dev, "failed to find misc regmap\n");
		return PTR_ERR(priv->misc);
	}

	if (of_property_read_u32(dev->of_node, "phy-id", &priv->id)) {
		dev_err(dev, "failed to find phy id\n");
		return -EINVAL;
	}

	priv->phy = devm_phy_create(dev, NULL, &spear1310_miphy_ops);
	if (IS_ERR(priv->phy)) {
		dev_err(dev, "failed to create SATA PCIe PHY\n");
		return PTR_ERR(priv->phy);
	}

	dev_set_drvdata(dev, priv);
	phy_set_drvdata(priv->phy, priv);

	phy_provider =
		devm_of_phy_provider_register(dev, spear1310_miphy_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	return 0;
}

static struct platform_driver spear1310_miphy_driver = {
	.probe		= spear1310_miphy_probe,
	.driver = {
		.name = "spear1310-miphy",
		.of_match_table = of_match_ptr(spear1310_miphy_of_match),
	},
};

module_platform_driver(spear1310_miphy_driver);

MODULE_DESCRIPTION("ST SPEAR1310-MIPHY driver");
MODULE_AUTHOR("Pratyush Anand <pratyush.anand@st.com>");
MODULE_LICENSE("GPL v2");
