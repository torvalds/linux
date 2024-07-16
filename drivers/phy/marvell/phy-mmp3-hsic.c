// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Lubomir Rintel <lkundrak@v3.sk>
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#define HSIC_CTRL	0x08
#define HSIC_ENABLE	BIT(7)
#define PLL_BYPASS	BIT(4)

static int mmp3_hsic_phy_init(struct phy *phy)
{
	void __iomem *base = (void __iomem *)phy_get_drvdata(phy);
	u32 hsic_ctrl;

	hsic_ctrl = readl_relaxed(base + HSIC_CTRL);
	hsic_ctrl |= HSIC_ENABLE;
	hsic_ctrl |= PLL_BYPASS;
	writel_relaxed(hsic_ctrl, base + HSIC_CTRL);

	return 0;
}

static const struct phy_ops mmp3_hsic_phy_ops = {
	.init		= mmp3_hsic_phy_init,
	.owner		= THIS_MODULE,
};

static const struct of_device_id mmp3_hsic_phy_of_match[] = {
	{ .compatible = "marvell,mmp3-hsic-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, mmp3_hsic_phy_of_match);

static int mmp3_hsic_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct resource *resource;
	void __iomem *base;
	struct phy *phy;

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, resource);
	if (IS_ERR(base))
		return PTR_ERR(base);

	phy = devm_phy_create(dev, NULL, &mmp3_hsic_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(phy);
	}

	phy_set_drvdata(phy, (void *)base);
	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "failed to register PHY provider\n");
		return PTR_ERR(provider);
	}

	return 0;
}

static struct platform_driver mmp3_hsic_phy_driver = {
	.probe		= mmp3_hsic_phy_probe,
	.driver		= {
		.name	= "mmp3-hsic-phy",
		.of_match_table = mmp3_hsic_phy_of_match,
	},
};
module_platform_driver(mmp3_hsic_phy_driver);

MODULE_AUTHOR("Lubomir Rintel <lkundrak@v3.sk>");
MODULE_DESCRIPTION("Marvell MMP3 USB HSIC PHY Driver");
MODULE_LICENSE("GPL");
