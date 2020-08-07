// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Exynos SoC series Display Port PHY driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>

struct exynos_dp_video_phy_drvdata {
	u32 phy_ctrl_offset;
};

struct exynos_dp_video_phy {
	struct regmap *regs;
	const struct exynos_dp_video_phy_drvdata *drvdata;
};

static int exynos_dp_video_phy_power_on(struct phy *phy)
{
	struct exynos_dp_video_phy *state = phy_get_drvdata(phy);

	/* Disable power isolation on DP-PHY */
	return regmap_update_bits(state->regs, state->drvdata->phy_ctrl_offset,
				  EXYNOS4_PHY_ENABLE, EXYNOS4_PHY_ENABLE);
}

static int exynos_dp_video_phy_power_off(struct phy *phy)
{
	struct exynos_dp_video_phy *state = phy_get_drvdata(phy);

	/* Enable power isolation on DP-PHY */
	return regmap_update_bits(state->regs, state->drvdata->phy_ctrl_offset,
				  EXYNOS4_PHY_ENABLE, 0);
}

static const struct phy_ops exynos_dp_video_phy_ops = {
	.power_on	= exynos_dp_video_phy_power_on,
	.power_off	= exynos_dp_video_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct exynos_dp_video_phy_drvdata exynos5250_dp_video_phy = {
	.phy_ctrl_offset	= EXYNOS5_DPTX_PHY_CONTROL,
};

static const struct exynos_dp_video_phy_drvdata exynos5420_dp_video_phy = {
	.phy_ctrl_offset	= EXYNOS5420_DPTX_PHY_CONTROL,
};

static const struct of_device_id exynos_dp_video_phy_of_match[] = {
	{
		.compatible = "samsung,exynos5250-dp-video-phy",
		.data = &exynos5250_dp_video_phy,
	}, {
		.compatible = "samsung,exynos5420-dp-video-phy",
		.data = &exynos5420_dp_video_phy,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, exynos_dp_video_phy_of_match);

static int exynos_dp_video_phy_probe(struct platform_device *pdev)
{
	struct exynos_dp_video_phy *state;
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct phy *phy;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->regs = syscon_regmap_lookup_by_phandle(dev->of_node,
						      "samsung,pmu-syscon");
	if (IS_ERR(state->regs)) {
		dev_err(dev, "Failed to lookup PMU regmap\n");
		return PTR_ERR(state->regs);
	}

	state->drvdata = of_device_get_match_data(dev);

	phy = devm_phy_create(dev, NULL, &exynos_dp_video_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create Display Port PHY\n");
		return PTR_ERR(phy);
	}
	phy_set_drvdata(phy, state);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver exynos_dp_video_phy_driver = {
	.probe	= exynos_dp_video_phy_probe,
	.driver = {
		.name	= "exynos-dp-video-phy",
		.of_match_table	= exynos_dp_video_phy_of_match,
		.suppress_bind_attrs = true,
	}
};
module_platform_driver(exynos_dp_video_phy_driver);

MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_DESCRIPTION("Samsung Exynos SoC DP PHY driver");
MODULE_LICENSE("GPL v2");
