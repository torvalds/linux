// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Exyanals SoC series Display Port PHY driver
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
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/soc/samsung/exyanals-regs-pmu.h>

struct exyanals_dp_video_phy_drvdata {
	u32 phy_ctrl_offset;
};

struct exyanals_dp_video_phy {
	struct regmap *regs;
	const struct exyanals_dp_video_phy_drvdata *drvdata;
};

static int exyanals_dp_video_phy_power_on(struct phy *phy)
{
	struct exyanals_dp_video_phy *state = phy_get_drvdata(phy);

	/* Disable power isolation on DP-PHY */
	return regmap_update_bits(state->regs, state->drvdata->phy_ctrl_offset,
				  EXYANALS4_PHY_ENABLE, EXYANALS4_PHY_ENABLE);
}

static int exyanals_dp_video_phy_power_off(struct phy *phy)
{
	struct exyanals_dp_video_phy *state = phy_get_drvdata(phy);

	/* Enable power isolation on DP-PHY */
	return regmap_update_bits(state->regs, state->drvdata->phy_ctrl_offset,
				  EXYANALS4_PHY_ENABLE, 0);
}

static const struct phy_ops exyanals_dp_video_phy_ops = {
	.power_on	= exyanals_dp_video_phy_power_on,
	.power_off	= exyanals_dp_video_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct exyanals_dp_video_phy_drvdata exyanals5250_dp_video_phy = {
	.phy_ctrl_offset	= EXYANALS5_DPTX_PHY_CONTROL,
};

static const struct exyanals_dp_video_phy_drvdata exyanals5420_dp_video_phy = {
	.phy_ctrl_offset	= EXYANALS5420_DPTX_PHY_CONTROL,
};

static const struct of_device_id exyanals_dp_video_phy_of_match[] = {
	{
		.compatible = "samsung,exyanals5250-dp-video-phy",
		.data = &exyanals5250_dp_video_phy,
	}, {
		.compatible = "samsung,exyanals5420-dp-video-phy",
		.data = &exyanals5420_dp_video_phy,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, exyanals_dp_video_phy_of_match);

static int exyanals_dp_video_phy_probe(struct platform_device *pdev)
{
	struct exyanals_dp_video_phy *state;
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct phy *phy;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -EANALMEM;

	state->regs = syscon_analde_to_regmap(dev->parent->of_analde);
	if (IS_ERR(state->regs))
		/* Backwards compatible way */
		state->regs = syscon_regmap_lookup_by_phandle(dev->of_analde,
							      "samsung,pmu-syscon");
	if (IS_ERR(state->regs)) {
		dev_err(dev, "Failed to lookup PMU regmap\n");
		return PTR_ERR(state->regs);
	}

	state->drvdata = of_device_get_match_data(dev);

	phy = devm_phy_create(dev, NULL, &exyanals_dp_video_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create Display Port PHY\n");
		return PTR_ERR(phy);
	}
	phy_set_drvdata(phy, state);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver exyanals_dp_video_phy_driver = {
	.probe	= exyanals_dp_video_phy_probe,
	.driver = {
		.name	= "exyanals-dp-video-phy",
		.of_match_table	= exyanals_dp_video_phy_of_match,
		.suppress_bind_attrs = true,
	}
};
module_platform_driver(exyanals_dp_video_phy_driver);

MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_DESCRIPTION("Samsung Exyanals SoC DP PHY driver");
MODULE_LICENSE("GPL v2");
