/*
 * ST spear1340-miphy driver
 *
 * Copyright (C) 2014 ST Microelectronics
 * Pratyush Anand <pratyush.anand@gmail.com>
 * Mohit Kumar <mohit.kumar.dhaka@gmail.com>
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

/* SPEAr1340 Registers */
/* Power Management Registers */
#define SPEAR1340_PCM_CFG			0x100
	#define SPEAR1340_PCM_CFG_SATA_POWER_EN		BIT(11)
#define SPEAR1340_PCM_WKUP_CFG			0x104
#define SPEAR1340_SWITCH_CTR			0x108

#define SPEAR1340_PERIP1_SW_RST			0x318
	#define SPEAR1340_PERIP1_SW_RSATA		BIT(12)
#define SPEAR1340_PERIP2_SW_RST			0x31C
#define SPEAR1340_PERIP3_SW_RST			0x320

/* PCIE - SATA configuration registers */
#define SPEAR1340_PCIE_SATA_CFG			0x424
	/* PCIE CFG MASks */
	#define SPEAR1340_PCIE_CFG_DEVICE_PRESENT	BIT(11)
	#define SPEAR1340_PCIE_CFG_POWERUP_RESET	BIT(10)
	#define SPEAR1340_PCIE_CFG_CORE_CLK_EN		BIT(9)
	#define SPEAR1340_PCIE_CFG_AUX_CLK_EN		BIT(8)
	#define SPEAR1340_SATA_CFG_TX_CLK_EN		BIT(4)
	#define SPEAR1340_SATA_CFG_RX_CLK_EN		BIT(3)
	#define SPEAR1340_SATA_CFG_POWERUP_RESET	BIT(2)
	#define SPEAR1340_SATA_CFG_PM_CLK_EN		BIT(1)
	#define SPEAR1340_PCIE_SATA_SEL_PCIE		(0)
	#define SPEAR1340_PCIE_SATA_SEL_SATA		(1)
	#define SPEAR1340_PCIE_SATA_CFG_MASK		0xF1F
	#define SPEAR1340_PCIE_CFG_VAL	(SPEAR1340_PCIE_SATA_SEL_PCIE | \
			SPEAR1340_PCIE_CFG_AUX_CLK_EN | \
			SPEAR1340_PCIE_CFG_CORE_CLK_EN | \
			SPEAR1340_PCIE_CFG_POWERUP_RESET | \
			SPEAR1340_PCIE_CFG_DEVICE_PRESENT)
	#define SPEAR1340_SATA_CFG_VAL	(SPEAR1340_PCIE_SATA_SEL_SATA | \
			SPEAR1340_SATA_CFG_PM_CLK_EN | \
			SPEAR1340_SATA_CFG_POWERUP_RESET | \
			SPEAR1340_SATA_CFG_RX_CLK_EN | \
			SPEAR1340_SATA_CFG_TX_CLK_EN)

#define SPEAR1340_PCIE_MIPHY_CFG		0x428
	#define SPEAR1340_MIPHY_OSC_BYPASS_EXT		BIT(31)
	#define SPEAR1340_MIPHY_CLK_REF_DIV2		BIT(27)
	#define SPEAR1340_MIPHY_CLK_REF_DIV4		(2 << 27)
	#define SPEAR1340_MIPHY_CLK_REF_DIV8		(3 << 27)
	#define SPEAR1340_MIPHY_PLL_RATIO_TOP(x)	(x << 0)
	#define SPEAR1340_PCIE_MIPHY_CFG_MASK		0xF80000FF
	#define SPEAR1340_PCIE_SATA_MIPHY_CFG_SATA \
			(SPEAR1340_MIPHY_OSC_BYPASS_EXT | \
			SPEAR1340_MIPHY_CLK_REF_DIV2 | \
			SPEAR1340_MIPHY_PLL_RATIO_TOP(60))
	#define SPEAR1340_PCIE_SATA_MIPHY_CFG_SATA_25M_CRYSTAL_CLK \
			(SPEAR1340_MIPHY_PLL_RATIO_TOP(120))
	#define SPEAR1340_PCIE_SATA_MIPHY_CFG_PCIE \
			(SPEAR1340_MIPHY_OSC_BYPASS_EXT | \
			SPEAR1340_MIPHY_PLL_RATIO_TOP(25))

enum spear1340_miphy_mode {
	SATA,
	PCIE,
};

struct spear1340_miphy_priv {
	/* phy mode: 0 for SATA 1 for PCIe */
	enum spear1340_miphy_mode	mode;
	/* regmap for any soc specific misc registers */
	struct regmap			*misc;
	/* phy struct pointer */
	struct phy			*phy;
};

static int spear1340_miphy_sata_init(struct spear1340_miphy_priv *priv)
{
	regmap_update_bits(priv->misc, SPEAR1340_PCIE_SATA_CFG,
			   SPEAR1340_PCIE_SATA_CFG_MASK,
			   SPEAR1340_SATA_CFG_VAL);
	regmap_update_bits(priv->misc, SPEAR1340_PCIE_MIPHY_CFG,
			   SPEAR1340_PCIE_MIPHY_CFG_MASK,
			   SPEAR1340_PCIE_SATA_MIPHY_CFG_SATA_25M_CRYSTAL_CLK);
	/* Switch on sata power domain */
	regmap_update_bits(priv->misc, SPEAR1340_PCM_CFG,
			   SPEAR1340_PCM_CFG_SATA_POWER_EN,
			   SPEAR1340_PCM_CFG_SATA_POWER_EN);
	/* Wait for SATA power domain on */
	msleep(20);

	/* Disable PCIE SATA Controller reset */
	regmap_update_bits(priv->misc, SPEAR1340_PERIP1_SW_RST,
			   SPEAR1340_PERIP1_SW_RSATA, 0);
	/* Wait for SATA reset de-assert completion */
	msleep(20);

	return 0;
}

static int spear1340_miphy_sata_exit(struct spear1340_miphy_priv *priv)
{
	regmap_update_bits(priv->misc, SPEAR1340_PCIE_SATA_CFG,
			   SPEAR1340_PCIE_SATA_CFG_MASK, 0);
	regmap_update_bits(priv->misc, SPEAR1340_PCIE_MIPHY_CFG,
			   SPEAR1340_PCIE_MIPHY_CFG_MASK, 0);

	/* Enable PCIE SATA Controller reset */
	regmap_update_bits(priv->misc, SPEAR1340_PERIP1_SW_RST,
			   SPEAR1340_PERIP1_SW_RSATA,
			   SPEAR1340_PERIP1_SW_RSATA);
	/* Wait for SATA power domain off */
	msleep(20);
	/* Switch off sata power domain */
	regmap_update_bits(priv->misc, SPEAR1340_PCM_CFG,
			   SPEAR1340_PCM_CFG_SATA_POWER_EN, 0);
	/* Wait for SATA reset assert completion */
	msleep(20);

	return 0;
}

static int spear1340_miphy_pcie_init(struct spear1340_miphy_priv *priv)
{
	regmap_update_bits(priv->misc, SPEAR1340_PCIE_MIPHY_CFG,
			   SPEAR1340_PCIE_MIPHY_CFG_MASK,
			   SPEAR1340_PCIE_SATA_MIPHY_CFG_PCIE);
	regmap_update_bits(priv->misc, SPEAR1340_PCIE_SATA_CFG,
			   SPEAR1340_PCIE_SATA_CFG_MASK,
			   SPEAR1340_PCIE_CFG_VAL);

	return 0;
}

static int spear1340_miphy_pcie_exit(struct spear1340_miphy_priv *priv)
{
	regmap_update_bits(priv->misc, SPEAR1340_PCIE_MIPHY_CFG,
			   SPEAR1340_PCIE_MIPHY_CFG_MASK, 0);
	regmap_update_bits(priv->misc, SPEAR1340_PCIE_SATA_CFG,
			   SPEAR1340_PCIE_SATA_CFG_MASK, 0);

	return 0;
}

static int spear1340_miphy_init(struct phy *phy)
{
	struct spear1340_miphy_priv *priv = phy_get_drvdata(phy);
	int ret = 0;

	if (priv->mode == SATA)
		ret = spear1340_miphy_sata_init(priv);
	else if (priv->mode == PCIE)
		ret = spear1340_miphy_pcie_init(priv);

	return ret;
}

static int spear1340_miphy_exit(struct phy *phy)
{
	struct spear1340_miphy_priv *priv = phy_get_drvdata(phy);
	int ret = 0;

	if (priv->mode == SATA)
		ret = spear1340_miphy_sata_exit(priv);
	else if (priv->mode == PCIE)
		ret = spear1340_miphy_pcie_exit(priv);

	return ret;
}

static const struct of_device_id spear1340_miphy_of_match[] = {
	{ .compatible = "st,spear1340-miphy" },
	{ },
};
MODULE_DEVICE_TABLE(of, spear1340_miphy_of_match);

static struct phy_ops spear1340_miphy_ops = {
	.init = spear1340_miphy_init,
	.exit = spear1340_miphy_exit,
	.owner = THIS_MODULE,
};

#ifdef CONFIG_PM_SLEEP
static int spear1340_miphy_suspend(struct device *dev)
{
	struct spear1340_miphy_priv *priv = dev_get_drvdata(dev);
	int ret = 0;

	if (priv->mode == SATA)
		ret = spear1340_miphy_sata_exit(priv);

	return ret;
}

static int spear1340_miphy_resume(struct device *dev)
{
	struct spear1340_miphy_priv *priv = dev_get_drvdata(dev);
	int ret = 0;

	if (priv->mode == SATA)
		ret = spear1340_miphy_sata_init(priv);

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(spear1340_miphy_pm_ops, spear1340_miphy_suspend,
			 spear1340_miphy_resume);

static struct phy *spear1340_miphy_xlate(struct device *dev,
					 struct of_phandle_args *args)
{
	struct spear1340_miphy_priv *priv = dev_get_drvdata(dev);

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

static int spear1340_miphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spear1340_miphy_priv *priv;
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

	priv->phy = devm_phy_create(dev, NULL, &spear1340_miphy_ops);
	if (IS_ERR(priv->phy)) {
		dev_err(dev, "failed to create SATA PCIe PHY\n");
		return PTR_ERR(priv->phy);
	}

	dev_set_drvdata(dev, priv);
	phy_set_drvdata(priv->phy, priv);

	phy_provider =
		devm_of_phy_provider_register(dev, spear1340_miphy_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	return 0;
}

static struct platform_driver spear1340_miphy_driver = {
	.probe		= spear1340_miphy_probe,
	.driver = {
		.name = "spear1340-miphy",
		.pm = &spear1340_miphy_pm_ops,
		.of_match_table = of_match_ptr(spear1340_miphy_of_match),
	},
};

module_platform_driver(spear1340_miphy_driver);

MODULE_DESCRIPTION("ST SPEAR1340-MIPHY driver");
MODULE_AUTHOR("Pratyush Anand <pratyush.anand@gmail.com>");
MODULE_LICENSE("GPL v2");
