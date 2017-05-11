/*
 * Rockchip DP PHY driver
 *
 * Copyright (C) 2016 FuZhou Rockchip Co., Ltd.
 * Author: Yakir Yang <ykk@@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

struct rockchip_dp_phy_drv_data {
	u32 grf_reg_offset;
	u32 ref_clk_sel_inter;
	u32 siddq_on;
	u32 siddq_off;
};

struct rockchip_dp_phy {
	struct device  *dev;
	struct regmap  *grf;
	struct clk     *phy_24m;
	struct reset_control *rst_24m;

	const struct rockchip_dp_phy_drv_data *drv_data;
};

static int rockchip_set_phy_state(struct phy *phy, bool enable)
{
	struct rockchip_dp_phy *dp = phy_get_drvdata(phy);
	const struct rockchip_dp_phy_drv_data *drv_data = dp->drv_data;
	int ret;

	if (enable) {
		if (dp->rst_24m) {
			/* EDP 24m clock domain software reset request. */
			reset_control_assert(dp->rst_24m);
			usleep_range(20, 40);
			reset_control_deassert(dp->rst_24m);
			usleep_range(20, 40);
		}

		ret = regmap_write(dp->grf, drv_data->grf_reg_offset,
				   drv_data->siddq_on);
		if (ret < 0) {
			dev_err(dp->dev, "Can't enable PHY power %d\n", ret);
			return ret;
		}

		ret = clk_prepare_enable(dp->phy_24m);
	} else {
		clk_disable_unprepare(dp->phy_24m);

		ret = regmap_write(dp->grf, drv_data->grf_reg_offset,
				   drv_data->siddq_off);
	}

	return ret;
}

static int rockchip_dp_phy_power_on(struct phy *phy)
{
	return rockchip_set_phy_state(phy, true);
}

static int rockchip_dp_phy_power_off(struct phy *phy)
{
	return rockchip_set_phy_state(phy, false);
}

static const struct phy_ops rockchip_dp_phy_ops = {
	.power_on	= rockchip_dp_phy_power_on,
	.power_off	= rockchip_dp_phy_power_off,
	.owner		= THIS_MODULE,
};

static int rockchip_dp_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct phy_provider *phy_provider;
	struct rockchip_dp_phy *dp;
	const struct rockchip_dp_phy_drv_data *drv_data;
	struct phy *phy;
	int ret;

	if (!np)
		return -ENODEV;

	if (!dev->parent || !dev->parent->of_node)
		return -ENODEV;

	drv_data = of_device_get_match_data(dev);
	if (!drv_data) {
		dev_err(dev, "No OF match data provided\n");
		return -EINVAL;
	}

	dp = devm_kzalloc(dev, sizeof(*dp), GFP_KERNEL);
	if (IS_ERR(dp))
		return -ENOMEM;

	dp->dev = dev;
	dp->drv_data = drv_data;

	dp->phy_24m = devm_clk_get(dev, "24m");
	if (IS_ERR(dp->phy_24m)) {
		dev_err(dev, "cannot get clock 24m\n");
		return PTR_ERR(dp->phy_24m);
	}

	ret = clk_set_rate(dp->phy_24m, 24000000);
	if (ret < 0) {
		dev_err(dp->dev, "cannot set clock phy_24m %d\n", ret);
		return ret;
	}

	/* optional */
	dp->rst_24m = devm_reset_control_get_optional(&pdev->dev, "edp_24m");
	if (IS_ERR(dp->rst_24m)) {
		dev_info(dev, "No edp_24m reset control specified\n");
		dp->rst_24m = NULL;
	}

	dp->grf = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(dp->grf)) {
		dev_err(dev, "rk3288-dp needs the General Register Files syscon\n");
		return PTR_ERR(dp->grf);
	}

	ret = regmap_write(dp->grf, drv_data->grf_reg_offset,
			   drv_data->ref_clk_sel_inter);
	if (ret) {
		dev_err(dp->dev, "Could not config GRF edp ref clk: %d\n", ret);
		return ret;
	}

	phy = devm_phy_create(dev, np, &rockchip_dp_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create phy\n");
		return PTR_ERR(phy);
	}
	phy_set_drvdata(phy, dp);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct rockchip_dp_phy_drv_data rk3288_dp_phy_drv_data = {
	.grf_reg_offset = 0x274,
	.ref_clk_sel_inter = BIT(4) | BIT(20),
	.siddq_on = 0 | BIT(21),
	.siddq_off = BIT(5) | BIT(21),
};

static const struct rockchip_dp_phy_drv_data rk3368_dp_phy_drv_data = {
	.grf_reg_offset = 0x410,
	.ref_clk_sel_inter = BIT(0) | BIT(16),
	.siddq_on = 0 | BIT(17),
	.siddq_off = BIT(1) | BIT(17),
};

static const struct of_device_id rockchip_dp_phy_dt_ids[] = {
	{ .compatible = "rockchip,rk3288-dp-phy",
	  .data = &rk3288_dp_phy_drv_data },
	{ .compatible = "rockchip,rk3368-dp-phy",
	  .data = &rk3368_dp_phy_drv_data },
	{}
};

MODULE_DEVICE_TABLE(of, rockchip_dp_phy_dt_ids);

static struct platform_driver rockchip_dp_phy_driver = {
	.probe		= rockchip_dp_phy_probe,
	.driver		= {
		.name	= "rockchip-dp-phy",
		.of_match_table = rockchip_dp_phy_dt_ids,
	},
};

module_platform_driver(rockchip_dp_phy_driver);

MODULE_AUTHOR("Yakir Yang <ykk@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip DP PHY driver");
MODULE_LICENSE("GPL v2");
