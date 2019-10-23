// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/mfd/rk628.h>
#include <linux/phy/phy.h>

#include <drm/drmP.h>
#include <drm/drm_of.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>

#include <video/of_display_timing.h>
#include <video/videomode.h>

struct rk628_rgb {
	struct drm_bridge base;
	struct drm_connector connector;
	struct drm_display_mode mode;
	struct drm_panel *panel;
	struct device *dev;
	struct regmap *grf;
	struct rk628 *parent;
};

static inline struct rk628_rgb *bridge_to_rgb(struct drm_bridge *b)
{
	return container_of(b, struct rk628_rgb, base);
}

static inline struct rk628_rgb *connector_to_rgb(struct drm_connector *c)
{
	return container_of(c, struct rk628_rgb, connector);
}

static struct drm_encoder *
rk628_rgb_connector_best_encoder(struct drm_connector *connector)
{
	struct rk628_rgb *rgb = connector_to_rgb(connector);

	return rgb->base.encoder;
}

static int rk628_rgb_connector_get_modes(struct drm_connector *connector)
{
	struct rk628_rgb *rgb = connector_to_rgb(connector);

	return drm_panel_get_modes(rgb->panel);
}

static const struct drm_connector_helper_funcs
rk628_rgb_connector_helper_funcs = {
	.get_modes = rk628_rgb_connector_get_modes,
	.best_encoder = rk628_rgb_connector_best_encoder,
};

static void rk628_rgb_connector_destroy(struct drm_connector *connector)
{
	struct rk628_rgb *rgb = connector_to_rgb(connector);

	drm_panel_detach(rgb->panel);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rk628_rgb_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rk628_rgb_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void rk628_rgb_bridge_enable(struct drm_bridge *bridge)
{
	struct rk628_rgb *rgb = bridge_to_rgb(bridge);

	regmap_update_bits(rgb->grf, GRF_SYSTEM_CON0,
			   SW_BT_DATA_OEN_MASK | SW_OUTPUT_MODE_MASK,
			   SW_OUTPUT_MODE(OUTPUT_MODE_RGB));

	drm_panel_prepare(rgb->panel);
	drm_panel_enable(rgb->panel);
}

static void rk628_rgb_bridge_disable(struct drm_bridge *bridge)
{
	struct rk628_rgb *rgb = bridge_to_rgb(bridge);

	drm_panel_disable(rgb->panel);
	drm_panel_unprepare(rgb->panel);
}

static int rk628_rgb_bridge_attach(struct drm_bridge *bridge)
{
	struct rk628_rgb *rgb = bridge_to_rgb(bridge);
	struct drm_connector *connector = &rgb->connector;
	struct drm_device *drm = bridge->dev;
	int ret;

	ret = drm_connector_init(drm, connector, &rk628_rgb_connector_funcs,
				 DRM_MODE_CONNECTOR_DPI);
	if (ret) {
		dev_err(rgb->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &rk628_rgb_connector_helper_funcs);
	drm_connector_attach_encoder(connector, bridge->encoder);

	ret = drm_panel_attach(rgb->panel, connector);
	if (ret) {
		dev_err(rgb->dev, "Failed to attach panel\n");
		return ret;
	}

	return 0;
}

static void rk628_rgb_bridge_mode_set(struct drm_bridge *bridge,
				      struct drm_display_mode *mode,
				      struct drm_display_mode *adj)
{
	struct rk628_rgb *rgb = bridge_to_rgb(bridge);

	drm_mode_copy(&rgb->mode, adj);
}

static const struct drm_bridge_funcs rk628_rgb_bridge_funcs = {
	.attach = rk628_rgb_bridge_attach,
	.enable = rk628_rgb_bridge_enable,
	.disable = rk628_rgb_bridge_disable,
	.mode_set = rk628_rgb_bridge_mode_set,
};

static int rk628_rgb_probe(struct platform_device *pdev)
{
	struct rk628 *rk628 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk628_rgb *rgb;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	rgb = devm_kzalloc(dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	rgb->dev = dev;
	rgb->parent = rk628;
	rgb->grf = rk628->grf;
	platform_set_drvdata(pdev, rgb);

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1,
					  &rgb->panel, NULL);
	if (ret)
		return ret;

	rgb->base.funcs = &rk628_rgb_bridge_funcs;
	rgb->base.of_node = dev->of_node;
	drm_bridge_add(&rgb->base);

	return 0;
}

static int rk628_rgb_remove(struct platform_device *pdev)
{
	struct rk628_rgb *rgb = platform_get_drvdata(pdev);

	drm_bridge_remove(&rgb->base);

	return 0;
}

static const struct of_device_id rk628_rgb_of_match[] = {
	{ .compatible = "rockchip,rk628-rgb", },
	{},
};
MODULE_DEVICE_TABLE(of, rk628_rgb_of_match);

static struct platform_driver rk628_rgb_driver = {
	.driver = {
		.name = "rk628-rgb",
		.of_match_table = of_match_ptr(rk628_rgb_of_match),
	},
	.probe = rk628_rgb_probe,
	.remove = rk628_rgb_remove,
};
module_platform_driver(rk628_rgb_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 RGB driver");
MODULE_LICENSE("GPL v2");
