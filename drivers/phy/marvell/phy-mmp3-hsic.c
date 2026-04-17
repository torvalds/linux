// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Lubomir Rintel <lkundrak@v3.sk>
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#define HSIC_CTRL	0x08
#define HSIC_ENABLE	BIT(7)
#define PLL_BYPASS	BIT(4)

struct mmp3_hsic_data {
	void __iomem *base;
};

static int mmp3_hsic_phy_init(struct phy *phy)
{
	struct mmp3_hsic_data *mmp3 = phy_get_drvdata(phy);
	u32 hsic_ctrl;

	hsic_ctrl = readl_relaxed(mmp3->base + HSIC_CTRL);
	hsic_ctrl |= HSIC_ENABLE;
	hsic_ctrl |= PLL_BYPASS;
	writel_relaxed(hsic_ctrl, mmp3->base + HSIC_CTRL);

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
	struct mmp3_hsic_data *mmp3;
	struct phy_provider *provider;
	struct phy *phy;

	mmp3 = devm_kzalloc(dev, sizeof(*mmp3), GFP_KERNEL);
	if (!mmp3)
		return -ENOMEM;

	mmp3->base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(mmp3->base))
		return PTR_ERR(mmp3->base);

	phy = devm_phy_create(dev, NULL, &mmp3_hsic_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(phy);
	}

	phy_set_drvdata(phy, mmp3);
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
