// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_opp.h>
#include "dp_power.h"
#include "msm_drv.h"

struct dp_power_private {
	struct dp_parser *parser;
	struct device *dev;
	struct drm_device *drm_dev;
	struct clk *link_clk_src;
	struct clk *pixel_provider;
	struct clk *link_provider;

	struct dp_power dp_power;
};

static int dp_power_clk_init(struct dp_power_private *power)
{
	int rc = 0;
	struct dss_module_power *core, *ctrl, *stream;
	struct device *dev = power->dev;

	core = &power->parser->mp[DP_CORE_PM];
	ctrl = &power->parser->mp[DP_CTRL_PM];
	stream = &power->parser->mp[DP_STREAM_PM];

	rc = devm_clk_bulk_get(dev, core->num_clk, core->clocks);
	if (rc)
		return rc;

	rc = devm_clk_bulk_get(dev, ctrl->num_clk, ctrl->clocks);
	if (rc)
		return -ENODEV;

	rc = devm_clk_bulk_get(dev, stream->num_clk, stream->clocks);
	if (rc)
		return -ENODEV;

	return 0;
}

int dp_power_clk_status(struct dp_power *dp_power, enum dp_pm_type pm_type)
{
	struct dp_power_private *power;

	power = container_of(dp_power, struct dp_power_private, dp_power);

	drm_dbg_dp(power->drm_dev,
		"core_clk_on=%d link_clk_on=%d stream_clk_on=%d\n",
		dp_power->core_clks_on, dp_power->link_clks_on, dp_power->stream_clks_on);

	if (pm_type == DP_CORE_PM)
		return dp_power->core_clks_on;

	if (pm_type == DP_CTRL_PM)
		return dp_power->link_clks_on;

	if (pm_type == DP_STREAM_PM)
		return dp_power->stream_clks_on;

	return 0;
}

int dp_power_clk_enable(struct dp_power *dp_power,
		enum dp_pm_type pm_type, bool enable)
{
	int rc = 0;
	struct dp_power_private *power;
	struct dss_module_power *mp;

	power = container_of(dp_power, struct dp_power_private, dp_power);

	if (pm_type != DP_CORE_PM && pm_type != DP_CTRL_PM &&
			pm_type != DP_STREAM_PM) {
		DRM_ERROR("unsupported power module: %s\n",
				dp_parser_pm_name(pm_type));
		return -EINVAL;
	}

	if (enable) {
		if (pm_type == DP_CORE_PM && dp_power->core_clks_on) {
			drm_dbg_dp(power->drm_dev,
					"core clks already enabled\n");
			return 0;
		}

		if (pm_type == DP_CTRL_PM && dp_power->link_clks_on) {
			drm_dbg_dp(power->drm_dev,
					"links clks already enabled\n");
			return 0;
		}

		if (pm_type == DP_STREAM_PM && dp_power->stream_clks_on) {
			drm_dbg_dp(power->drm_dev,
					"pixel clks already enabled\n");
			return 0;
		}

		if ((pm_type == DP_CTRL_PM) && (!dp_power->core_clks_on)) {
			drm_dbg_dp(power->drm_dev,
					"Enable core clks before link clks\n");
			mp = &power->parser->mp[DP_CORE_PM];

			rc = clk_bulk_prepare_enable(mp->num_clk, mp->clocks);
			if (rc)
				return rc;

			dp_power->core_clks_on = true;
		}
	}

	mp = &power->parser->mp[pm_type];
	if (enable) {
		rc = clk_bulk_prepare_enable(mp->num_clk, mp->clocks);
		if (rc)
			return rc;
	} else {
		clk_bulk_disable_unprepare(mp->num_clk, mp->clocks);
	}

	if (pm_type == DP_CORE_PM)
		dp_power->core_clks_on = enable;
	else if (pm_type == DP_STREAM_PM)
		dp_power->stream_clks_on = enable;
	else
		dp_power->link_clks_on = enable;

	drm_dbg_dp(power->drm_dev, "%s clocks for %s\n",
			enable ? "enable" : "disable",
			dp_parser_pm_name(pm_type));
	drm_dbg_dp(power->drm_dev,
		"strem_clks:%s link_clks:%s core_clks:%s\n",
		dp_power->stream_clks_on ? "on" : "off",
		dp_power->link_clks_on ? "on" : "off",
		dp_power->core_clks_on ? "on" : "off");

	return 0;
}

int dp_power_client_init(struct dp_power *dp_power)
{
	struct dp_power_private *power;

	power = container_of(dp_power, struct dp_power_private, dp_power);

	return dp_power_clk_init(power);
}

int dp_power_init(struct dp_power *dp_power)
{
	return dp_power_clk_enable(dp_power, DP_CORE_PM, true);
}

int dp_power_deinit(struct dp_power *dp_power)
{
	return dp_power_clk_enable(dp_power, DP_CORE_PM, false);
}

struct dp_power *dp_power_get(struct device *dev, struct dp_parser *parser)
{
	struct dp_power_private *power;
	struct dp_power *dp_power;

	power = devm_kzalloc(dev, sizeof(*power), GFP_KERNEL);
	if (!power)
		return ERR_PTR(-ENOMEM);

	power->parser = parser;
	power->dev = dev;

	dp_power = &power->dp_power;

	return dp_power;
}
