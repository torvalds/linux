/*
 * Samsung EXYNOS SoC series Display Port PHY driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

/* DPTX_PHY_CONTROL register */
#define EXYNOS_DPTX_PHY_ENABLE		(1 << 0)

struct exynos_dp_video_phy {
	void __iomem *regs;
};

static int __set_phy_state(struct exynos_dp_video_phy *state, unsigned int on)
{
	u32 reg;

	reg = readl(state->regs);
	if (on)
		reg |= EXYNOS_DPTX_PHY_ENABLE;
	else
		reg &= ~EXYNOS_DPTX_PHY_ENABLE;
	writel(reg, state->regs);

	return 0;
}

static int exynos_dp_video_phy_power_on(struct phy *phy)
{
	struct exynos_dp_video_phy *state = phy_get_drvdata(phy);

	return __set_phy_state(state, 1);
}

static int exynos_dp_video_phy_power_off(struct phy *phy)
{
	struct exynos_dp_video_phy *state = phy_get_drvdata(phy);

	return __set_phy_state(state, 0);
}

static struct phy_ops exynos_dp_video_phy_ops = {
	.power_on	= exynos_dp_video_phy_power_on,
	.power_off	= exynos_dp_video_phy_power_off,
	.owner		= THIS_MODULE,
};

static int exynos_dp_video_phy_probe(struct platform_device *pdev)
{
	struct exynos_dp_video_phy *state;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct phy_provider *phy_provider;
	struct phy *phy;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	state->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(state->regs))
		return PTR_ERR(state->regs);

	phy = devm_phy_create(dev, NULL, &exynos_dp_video_phy_ops, NULL);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create Display Port PHY\n");
		return PTR_ERR(phy);
	}
	phy_set_drvdata(phy, state);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id exynos_dp_video_phy_of_match[] = {
	{ .compatible = "samsung,exynos5250-dp-video-phy" },
	{ },
};
MODULE_DEVICE_TABLE(of, exynos_dp_video_phy_of_match);

static struct platform_driver exynos_dp_video_phy_driver = {
	.probe	= exynos_dp_video_phy_probe,
	.driver = {
		.name	= "exynos-dp-video-phy",
		.owner	= THIS_MODULE,
		.of_match_table	= exynos_dp_video_phy_of_match,
	}
};
module_platform_driver(exynos_dp_video_phy_driver);

MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_DESCRIPTION("Samsung EXYNOS SoC DP PHY driver");
MODULE_LICENSE("GPL v2");
