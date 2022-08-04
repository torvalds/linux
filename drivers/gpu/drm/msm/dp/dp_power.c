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
	struct platform_device *pdev;
	struct device *dev;
	struct drm_device *drm_dev;
	struct clk *link_clk_src;
	struct clk *pixel_provider;
	struct clk *link_provider;
	struct regulator_bulk_data supplies[DP_DEV_REGULATOR_MAX];

	struct dp_power dp_power;
};

static void dp_power_regulator_disable(struct dp_power_private *power)
{
	struct regulator_bulk_data *s = power->supplies;
	const struct dp_reg_entry *regs = power->parser->regulator_cfg->regs;
	int num = power->parser->regulator_cfg->num;
	int i;

	DBG("");
	for (i = num - 1; i >= 0; i--)
		if (regs[i].disable_load >= 0)
			regulator_set_load(s[i].consumer,
					   regs[i].disable_load);

	regulator_bulk_disable(num, s);
}

static int dp_power_regulator_enable(struct dp_power_private *power)
{
	struct regulator_bulk_data *s = power->supplies;
	const struct dp_reg_entry *regs = power->parser->regulator_cfg->regs;
	int num = power->parser->regulator_cfg->num;
	int ret, i;

	DBG("");
	for (i = 0; i < num; i++) {
		if (regs[i].enable_load >= 0) {
			ret = regulator_set_load(s[i].consumer,
						 regs[i].enable_load);
			if (ret < 0) {
				pr_err("regulator %d set op mode failed, %d\n",
					i, ret);
				goto fail;
			}
		}
	}

	ret = regulator_bulk_enable(num, s);
	if (ret < 0) {
		pr_err("regulator enable failed, %d\n", ret);
		goto fail;
	}

	return 0;

fail:
	for (i--; i >= 0; i--)
		regulator_set_load(s[i].consumer, regs[i].disable_load);
	return ret;
}

static int dp_power_regulator_init(struct dp_power_private *power)
{
	struct regulator_bulk_data *s = power->supplies;
	const struct dp_reg_entry *regs = power->parser->regulator_cfg->regs;
	struct platform_device *pdev = power->pdev;
	int num = power->parser->regulator_cfg->num;
	int i, ret;

	for (i = 0; i < num; i++)
		s[i].supply = regs[i].name;

	ret = devm_regulator_bulk_get(&pdev->dev, num, s);
	if (ret < 0) {
		pr_err("%s: failed to init regulator, ret=%d\n",
						__func__, ret);
		return ret;
	}

	return 0;
}

static int dp_power_clk_init(struct dp_power_private *power)
{
	int rc = 0;
	struct dss_module_power *core, *ctrl, *stream;
	struct device *dev = &power->pdev->dev;

	core = &power->parser->mp[DP_CORE_PM];
	ctrl = &power->parser->mp[DP_CTRL_PM];
	stream = &power->parser->mp[DP_STREAM_PM];

	rc = devm_clk_bulk_get(dev, core->num_clk, core->clocks);
	if (rc) {
		DRM_ERROR("failed to get %s clk. err=%d\n",
			dp_parser_pm_name(DP_CORE_PM), rc);
		return rc;
	}

	rc = devm_clk_bulk_get(dev, ctrl->num_clk, ctrl->clocks);
	if (rc) {
		DRM_ERROR("failed to get %s clk. err=%d\n",
			dp_parser_pm_name(DP_CTRL_PM), rc);
		return -ENODEV;
	}

	rc = devm_clk_bulk_get(dev, stream->num_clk, stream->clocks);
	if (rc) {
		DRM_ERROR("failed to get %s clk. err=%d\n",
			dp_parser_pm_name(DP_CTRL_PM), rc);
		return -ENODEV;
	}

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
			if (rc) {
				DRM_ERROR("fail to enable clks: %s. err=%d\n",
					dp_parser_pm_name(DP_CORE_PM), rc);
				return rc;
			}
			dp_power->core_clks_on = true;
		}
	}

	mp = &power->parser->mp[pm_type];
	if (enable) {
		rc = clk_bulk_prepare_enable(mp->num_clk, mp->clocks);
		if (rc) {
			DRM_ERROR("failed to enable clks, err: %d\n", rc);
			return rc;
		}
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
	int rc = 0;
	struct dp_power_private *power;

	if (!dp_power) {
		DRM_ERROR("invalid power data\n");
		return -EINVAL;
	}

	power = container_of(dp_power, struct dp_power_private, dp_power);

	pm_runtime_enable(&power->pdev->dev);

	rc = dp_power_regulator_init(power);
	if (rc) {
		DRM_ERROR("failed to init regulators %d\n", rc);
		goto error;
	}

	rc = dp_power_clk_init(power);
	if (rc) {
		DRM_ERROR("failed to init clocks %d\n", rc);
		goto error;
	}
	return 0;

error:
	pm_runtime_disable(&power->pdev->dev);
	return rc;
}

void dp_power_client_deinit(struct dp_power *dp_power)
{
	struct dp_power_private *power;

	if (!dp_power) {
		DRM_ERROR("invalid power data\n");
		return;
	}

	power = container_of(dp_power, struct dp_power_private, dp_power);

	pm_runtime_disable(&power->pdev->dev);
}

int dp_power_init(struct dp_power *dp_power, bool flip)
{
	int rc = 0;
	struct dp_power_private *power = NULL;

	if (!dp_power) {
		DRM_ERROR("invalid power data\n");
		return -EINVAL;
	}

	power = container_of(dp_power, struct dp_power_private, dp_power);

	pm_runtime_get_sync(&power->pdev->dev);
	rc = dp_power_regulator_enable(power);
	if (rc) {
		DRM_ERROR("failed to enable regulators, %d\n", rc);
		goto exit;
	}

	rc = dp_power_clk_enable(dp_power, DP_CORE_PM, true);
	if (rc) {
		DRM_ERROR("failed to enable DP core clocks, %d\n", rc);
		goto err_clk;
	}

	return 0;

err_clk:
	dp_power_regulator_disable(power);
exit:
	pm_runtime_put_sync(&power->pdev->dev);
	return rc;
}

int dp_power_deinit(struct dp_power *dp_power)
{
	struct dp_power_private *power;

	power = container_of(dp_power, struct dp_power_private, dp_power);

	dp_power_clk_enable(dp_power, DP_CORE_PM, false);
	dp_power_regulator_disable(power);
	pm_runtime_put_sync(&power->pdev->dev);
	return 0;
}

struct dp_power *dp_power_get(struct device *dev, struct dp_parser *parser)
{
	struct dp_power_private *power;
	struct dp_power *dp_power;

	if (!parser) {
		DRM_ERROR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	power = devm_kzalloc(&parser->pdev->dev, sizeof(*power), GFP_KERNEL);
	if (!power)
		return ERR_PTR(-ENOMEM);

	power->parser = parser;
	power->pdev = parser->pdev;
	power->dev = dev;

	dp_power = &power->dp_power;

	return dp_power;
}
