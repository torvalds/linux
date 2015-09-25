/*
 * Copyright (c) 2014 Linaro Ltd.
 * Copyright (c) 2014 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define SATA_PHY0_CTLL		0xa0
#define MPLL_MULTIPLIER_SHIFT	1
#define MPLL_MULTIPLIER_MASK	0xfe
#define MPLL_MULTIPLIER_50M	0x3c
#define MPLL_MULTIPLIER_100M	0x1e
#define PHY_RESET		BIT(0)
#define REF_SSP_EN		BIT(9)
#define SSC_EN			BIT(10)
#define REF_USE_PAD		BIT(23)

#define SATA_PORT_PHYCTL	0x174
#define SPEED_MODE_MASK		0x6f0000
#define HALF_RATE_SHIFT		16
#define PHY_CONFIG_SHIFT	18
#define GEN2_EN_SHIFT		21
#define SPEED_CTRL		BIT(20)

#define SATA_PORT_PHYCTL1	0x148
#define AMPLITUDE_MASK		0x3ffffe
#define AMPLITUDE_GEN3		0x68
#define AMPLITUDE_GEN3_SHIFT	15
#define AMPLITUDE_GEN2		0x56
#define AMPLITUDE_GEN2_SHIFT	8
#define AMPLITUDE_GEN1		0x56
#define AMPLITUDE_GEN1_SHIFT	1

#define SATA_PORT_PHYCTL2	0x14c
#define PREEMPH_MASK		0x3ffff
#define PREEMPH_GEN3		0x20
#define PREEMPH_GEN3_SHIFT	12
#define PREEMPH_GEN2		0x15
#define PREEMPH_GEN2_SHIFT	6
#define PREEMPH_GEN1		0x5
#define PREEMPH_GEN1_SHIFT	0

struct hix5hd2_priv {
	void __iomem	*base;
	struct regmap	*peri_ctrl;
};

enum phy_speed_mode {
	SPEED_MODE_GEN1 = 0,
	SPEED_MODE_GEN2 = 1,
	SPEED_MODE_GEN3 = 2,
};

static int hix5hd2_sata_phy_init(struct phy *phy)
{
	struct hix5hd2_priv *priv = phy_get_drvdata(phy);
	u32 val, data[2];
	int ret;

	if (priv->peri_ctrl) {
		ret = of_property_read_u32_array(phy->dev.of_node,
						 "hisilicon,power-reg",
						 &data[0], 2);
		if (ret) {
			dev_err(&phy->dev, "Fail read hisilicon,power-reg\n");
			return ret;
		}

		regmap_update_bits(priv->peri_ctrl, data[0],
				   BIT(data[1]), BIT(data[1]));
	}

	/* reset phy */
	val = readl_relaxed(priv->base + SATA_PHY0_CTLL);
	val &= ~(MPLL_MULTIPLIER_MASK | REF_USE_PAD);
	val |= MPLL_MULTIPLIER_50M << MPLL_MULTIPLIER_SHIFT |
	       REF_SSP_EN | PHY_RESET;
	writel_relaxed(val, priv->base + SATA_PHY0_CTLL);
	msleep(20);
	val &= ~PHY_RESET;
	writel_relaxed(val, priv->base + SATA_PHY0_CTLL);

	val = readl_relaxed(priv->base + SATA_PORT_PHYCTL1);
	val &= ~AMPLITUDE_MASK;
	val |= AMPLITUDE_GEN3 << AMPLITUDE_GEN3_SHIFT |
	       AMPLITUDE_GEN2 << AMPLITUDE_GEN2_SHIFT |
	       AMPLITUDE_GEN1 << AMPLITUDE_GEN1_SHIFT;
	writel_relaxed(val, priv->base + SATA_PORT_PHYCTL1);

	val = readl_relaxed(priv->base + SATA_PORT_PHYCTL2);
	val &= ~PREEMPH_MASK;
	val |= PREEMPH_GEN3 << PREEMPH_GEN3_SHIFT |
	       PREEMPH_GEN2 << PREEMPH_GEN2_SHIFT |
	       PREEMPH_GEN1 << PREEMPH_GEN1_SHIFT;
	writel_relaxed(val, priv->base + SATA_PORT_PHYCTL2);

	/* ensure PHYCTRL setting takes effect */
	val = readl_relaxed(priv->base + SATA_PORT_PHYCTL);
	val &= ~SPEED_MODE_MASK;
	val |= SPEED_MODE_GEN1 << HALF_RATE_SHIFT |
	       SPEED_MODE_GEN1 << PHY_CONFIG_SHIFT |
	       SPEED_MODE_GEN1 << GEN2_EN_SHIFT | SPEED_CTRL;
	writel_relaxed(val, priv->base + SATA_PORT_PHYCTL);

	msleep(20);
	val &= ~SPEED_MODE_MASK;
	val |= SPEED_MODE_GEN3 << HALF_RATE_SHIFT |
	       SPEED_MODE_GEN3 << PHY_CONFIG_SHIFT |
	       SPEED_MODE_GEN3 << GEN2_EN_SHIFT | SPEED_CTRL;
	writel_relaxed(val, priv->base + SATA_PORT_PHYCTL);

	val &= ~(SPEED_MODE_MASK | SPEED_CTRL);
	val |= SPEED_MODE_GEN2 << HALF_RATE_SHIFT |
	       SPEED_MODE_GEN2 << PHY_CONFIG_SHIFT |
	       SPEED_MODE_GEN2 << GEN2_EN_SHIFT;
	writel_relaxed(val, priv->base + SATA_PORT_PHYCTL);

	return 0;
}

static const struct phy_ops hix5hd2_sata_phy_ops = {
	.init		= hix5hd2_sata_phy_init,
	.owner		= THIS_MODULE,
};

static int hix5hd2_sata_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct phy *phy;
	struct hix5hd2_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	priv->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!priv->base)
		return -ENOMEM;

	priv->peri_ctrl = syscon_regmap_lookup_by_phandle(dev->of_node,
					"hisilicon,peripheral-syscon");
	if (IS_ERR(priv->peri_ctrl))
		priv->peri_ctrl = NULL;

	phy = devm_phy_create(dev, NULL, &hix5hd2_sata_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(phy);
	}

	phy_set_drvdata(phy, priv);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id hix5hd2_sata_phy_of_match[] = {
	{.compatible = "hisilicon,hix5hd2-sata-phy",},
	{ },
};
MODULE_DEVICE_TABLE(of, hix5hd2_sata_phy_of_match);

static struct platform_driver hix5hd2_sata_phy_driver = {
	.probe	= hix5hd2_sata_phy_probe,
	.driver = {
		.name	= "hix5hd2-sata-phy",
		.of_match_table	= hix5hd2_sata_phy_of_match,
	}
};
module_platform_driver(hix5hd2_sata_phy_driver);

MODULE_AUTHOR("Jiancheng Xue <xuejiancheng@huawei.com>");
MODULE_DESCRIPTION("HISILICON HIX5HD2 SATA PHY driver");
MODULE_ALIAS("platform:hix5hd2-sata-phy");
MODULE_LICENSE("GPL v2");
