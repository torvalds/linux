/*
 * Samsung SoC DP (Display Port) interface driver.
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/of_graph.h>
#include <linux/component.h>
#include <video/of_display_timing.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

#include <drm/bridge/analogix_dp.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_crtc.h"

#define to_dp(nm)	container_of(nm, struct exynos_dp_device, nm)

struct exynos_dp_device {
	struct drm_encoder         encoder;
	struct drm_connector       *connector;
	struct drm_bridge          *ptn_bridge;
	struct drm_device          *drm_dev;
	struct device              *dev;

	struct videomode           vm;
	struct analogix_dp_plat_data plat_data;
};

static int exynos_dp_crtc_clock_enable(struct analogix_dp_plat_data *plat_data,
				bool enable)
{
	struct exynos_dp_device *dp = to_dp(plat_data);
	struct drm_encoder *encoder = &dp->encoder;

	if (!encoder->crtc)
		return -EPERM;

	exynos_drm_pipe_clk_enable(to_exynos_crtc(encoder->crtc), enable);

	return 0;
}

static int exynos_dp_poweron(struct analogix_dp_plat_data *plat_data)
{
	return exynos_dp_crtc_clock_enable(plat_data, true);
}

static int exynos_dp_poweroff(struct analogix_dp_plat_data *plat_data)
{
	return exynos_dp_crtc_clock_enable(plat_data, false);
}

static int exynos_dp_get_modes(struct analogix_dp_plat_data *plat_data,
			       struct drm_connector *connector)
{
	struct exynos_dp_device *dp = to_dp(plat_data);
	struct drm_display_mode *mode;
	int num_modes = 0;

	if (dp->plat_data.panel)
		return num_modes;

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		DRM_ERROR("failed to create a new display mode.\n");
		return num_modes;
	}

	drm_display_mode_from_videomode(&dp->vm, mode);
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return num_modes + 1;
}

static int exynos_dp_bridge_attach(struct analogix_dp_plat_data *plat_data,
				   struct drm_bridge *bridge,
				   struct drm_connector *connector)
{
	struct exynos_dp_device *dp = to_dp(plat_data);
	int ret;

	dp->connector = connector;

	/* Pre-empt DP connector creation if there's a bridge */
	if (dp->ptn_bridge) {
		ret = drm_bridge_attach(&dp->encoder, dp->ptn_bridge, bridge);
		if (ret) {
			DRM_ERROR("Failed to attach bridge to drm\n");
			bridge->next = NULL;
			return ret;
		}
	}

	return 0;
}

static void exynos_dp_mode_set(struct drm_encoder *encoder,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
}

static void exynos_dp_nop(struct drm_encoder *encoder)
{
	/* do nothing */
}

static const struct drm_encoder_helper_funcs exynos_dp_encoder_helper_funcs = {
	.mode_set = exynos_dp_mode_set,
	.enable = exynos_dp_nop,
	.disable = exynos_dp_nop,
};

static const struct drm_encoder_funcs exynos_dp_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int exynos_dp_dt_parse_panel(struct exynos_dp_device *dp)
{
	int ret;

	ret = of_get_videomode(dp->dev->of_node, &dp->vm, OF_USE_NATIVE_MODE);
	if (ret) {
		DRM_ERROR("failed: of_get_videomode() : %d\n", ret);
		return ret;
	}
	return 0;
}

static int exynos_dp_bind(struct device *dev, struct device *master, void *data)
{
	struct exynos_dp_device *dp = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &dp->encoder;
	struct drm_device *drm_dev = data;
	int ret;

	/*
	 * Just like the probe function said, we don't need the
	 * device drvrate anymore, we should leave the charge to
	 * analogix dp driver, set the device drvdata to NULL.
	 */
	dev_set_drvdata(dev, NULL);

	dp->dev = dev;
	dp->drm_dev = drm_dev;

	dp->plat_data.dev_type = EXYNOS_DP;
	dp->plat_data.power_on = exynos_dp_poweron;
	dp->plat_data.power_off = exynos_dp_poweroff;
	dp->plat_data.attach = exynos_dp_bridge_attach;
	dp->plat_data.get_modes = exynos_dp_get_modes;

	if (!dp->plat_data.panel && !dp->ptn_bridge) {
		ret = exynos_dp_dt_parse_panel(dp);
		if (ret)
			return ret;
	}

	drm_encoder_init(drm_dev, encoder, &exynos_dp_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);

	drm_encoder_helper_add(encoder, &exynos_dp_encoder_helper_funcs);

	ret = exynos_drm_set_possible_crtcs(encoder, EXYNOS_DISPLAY_TYPE_LCD);
	if (ret < 0)
		return ret;

	dp->plat_data.encoder = encoder;

	return analogix_dp_bind(dev, dp->drm_dev, &dp->plat_data);
}

static void exynos_dp_unbind(struct device *dev, struct device *master,
			     void *data)
{
	return analogix_dp_unbind(dev, master, data);
}

static const struct component_ops exynos_dp_ops = {
	.bind	= exynos_dp_bind,
	.unbind	= exynos_dp_unbind,
};

static int exynos_dp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np;
	struct exynos_dp_device *dp;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	int ret;

	dp = devm_kzalloc(&pdev->dev, sizeof(struct exynos_dp_device),
			  GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	/*
	 * We just use the drvdata until driver run into component
	 * add function, and then we would set drvdata to null, so
	 * that analogix dp driver would take charge of the drvdata.
	 */
	platform_set_drvdata(pdev, dp);

	/* This is for the backward compatibility. */
	np = of_parse_phandle(dev->of_node, "panel", 0);
	if (np) {
		dp->plat_data.panel = of_drm_find_panel(np);
		of_node_put(np);
		if (!dp->plat_data.panel)
			return -EPROBE_DEFER;
		goto out;
	}

	ret = drm_of_find_panel_or_bridge(dev->of_node, 0, 0, &panel, &bridge);
	if (ret)
		return ret;

	/* The remote port can be either a panel or a bridge */
	dp->plat_data.panel = panel;
	dp->ptn_bridge = bridge;

out:
	return component_add(&pdev->dev, &exynos_dp_ops);
}

static int exynos_dp_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &exynos_dp_ops);

	return 0;
}

#ifdef CONFIG_PM
static int exynos_dp_suspend(struct device *dev)
{
	return analogix_dp_suspend(dev);
}

static int exynos_dp_resume(struct device *dev)
{
	return analogix_dp_resume(dev);
}
#endif

static const struct dev_pm_ops exynos_dp_pm_ops = {
	SET_RUNTIME_PM_OPS(exynos_dp_suspend, exynos_dp_resume, NULL)
};

static const struct of_device_id exynos_dp_match[] = {
	{ .compatible = "samsung,exynos5-dp" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_dp_match);

struct platform_driver dp_driver = {
	.probe		= exynos_dp_probe,
	.remove		= exynos_dp_remove,
	.driver		= {
		.name	= "exynos-dp",
		.owner	= THIS_MODULE,
		.pm	= &exynos_dp_pm_ops,
		.of_match_table = exynos_dp_match,
	},
};

MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_DESCRIPTION("Samsung Specific Analogix-DP Driver Extension");
MODULE_LICENSE("GPL v2");
