// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 *
 * Author: Chen Shunqing <csq@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <video/of_display_timing.h>
#include <linux/regmap.h>
#include <linux/mfd/rk618.h>

#include <drm/drm_drv.h>
#include <drm/drm_of.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include <video/videomode.h>

#include "../rockchip_drm_drv.h"
#include "rk618_dither.h"

struct rk618_rgb {
	struct drm_bridge base;
	struct drm_connector connector;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct device *dev;
	struct regmap *regmap;
	struct clk *clock;
	struct rk618 *parent;
	u32 bus_format;
	u32 id;
	struct rockchip_drm_sub_dev sub_dev;
};

static inline struct rk618_rgb *bridge_to_rgb(struct drm_bridge *b)
{
	return container_of(b, struct rk618_rgb, base);
}

static inline struct rk618_rgb *connector_to_rgb(struct drm_connector *c)
{
	return container_of(c, struct rk618_rgb, connector);
}

static struct drm_encoder *
rk618_rgb_connector_best_encoder(struct drm_connector *connector)
{
	struct rk618_rgb *rgb = connector_to_rgb(connector);

	return rgb->base.encoder;
}

static int rk618_rgb_connector_get_modes(struct drm_connector *connector)
{
	struct rk618_rgb *rgb = connector_to_rgb(connector);
	struct drm_display_info *info = &connector->display_info;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	int num_modes = 0;

	num_modes = drm_panel_get_modes(rgb->panel, connector);

	if (info->num_bus_formats)
		rgb->bus_format = info->bus_formats[0];
	else
		rgb->bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	drm_display_info_set_bus_formats(&connector->display_info,
					 &bus_format, 1);

	return num_modes;
}

static const struct drm_connector_helper_funcs
rk618_rgb_connector_helper_funcs = {
	.get_modes = rk618_rgb_connector_get_modes,
	.best_encoder = rk618_rgb_connector_best_encoder,
};

static enum drm_connector_status
rk618_rgb_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void rk618_rgb_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rk618_rgb_connector_funcs = {
	.detect = rk618_rgb_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rk618_rgb_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void rk618_rgb_bridge_enable(struct drm_bridge *bridge)
{
	struct rk618_rgb *rgb = bridge_to_rgb(bridge);
	u32 value;

	clk_prepare_enable(rgb->clock);

	rk618_frc_dclk_invert(rgb->parent);
	rk618_frc_dither_init(rgb->parent, rgb->bus_format);

	if (rgb->id) {
		value = LVDS_CON_CBG_POWER_DOWN | LVDS_CON_CHA1_POWER_DOWN |
			LVDS_CON_CHA0_POWER_DOWN | LVDS_CON_CHA0TTL_ENABLE |
			LVDS_CON_CHA1TTL_ENABLE | LVDS_CON_PLL_POWER_DOWN;
		regmap_write(rgb->regmap, RK618_LVDS_CON, value);

		regmap_write(rgb->regmap, RK618_IO_CON0, PORT2_OUTPUT_TTL);
	} else {
		value = LVDS_CON_CHA1TTL_DISABLE | LVDS_CON_CHA0TTL_DISABLE |
			LVDS_CON_CHA1_POWER_DOWN | LVDS_CON_CHA0_POWER_DOWN |
			LVDS_CON_CBG_POWER_DOWN | LVDS_CON_PLL_POWER_DOWN;
		regmap_write(rgb->regmap, RK618_LVDS_CON, value);

		regmap_write(rgb->regmap, RK618_IO_CON0,
			     PORT1_OUTPUT_TTL_ENABLE);
	}

	if (rgb->panel) {
		drm_panel_prepare(rgb->panel);
		drm_panel_enable(rgb->panel);
	}
}

static void rk618_rgb_bridge_disable(struct drm_bridge *bridge)
{
	struct rk618_rgb *rgb = bridge_to_rgb(bridge);

	if (rgb->panel) {
		drm_panel_disable(rgb->panel);
		drm_panel_unprepare(rgb->panel);
	}

	if (rgb->id)
		regmap_write(rgb->regmap, RK618_LVDS_CON,
			     LVDS_CON_CHA0_POWER_DOWN |
			     LVDS_CON_CHA1_POWER_DOWN |
			     LVDS_CON_CBG_POWER_DOWN |
			     LVDS_CON_PLL_POWER_DOWN);
	else
		regmap_write(rgb->regmap, RK618_IO_CON0,
			     PORT1_OUTPUT_TTL_DISABLE);

	clk_disable_unprepare(rgb->clock);
}

static int rk618_rgb_bridge_attach(struct drm_bridge *bridge,
				   enum drm_bridge_attach_flags flags)
{
	struct rk618_rgb *rgb = bridge_to_rgb(bridge);
	struct device *dev = rgb->dev;
	struct drm_connector *connector = &rgb->connector;
	struct drm_device *drm = bridge->dev;
	int ret;

	if (rgb->panel) {
		ret = drm_connector_init(drm, connector,
					 &rk618_rgb_connector_funcs,
					 DRM_MODE_CONNECTOR_DPI);
		if (ret) {
			dev_err(dev, "Failed to initialize connector\n");
			return ret;
		}

		drm_connector_helper_add(connector,
					 &rk618_rgb_connector_helper_funcs);
		drm_connector_attach_encoder(connector, bridge->encoder);

		rgb->sub_dev.connector = &rgb->connector;
		rgb->sub_dev.of_node = rgb->dev->of_node;
		rockchip_drm_register_sub_dev(&rgb->sub_dev);
	} else {
		ret = drm_bridge_attach(bridge->encoder, rgb->bridge, bridge, 0);
		if (ret) {
			dev_err(dev, "failed to attach bridge\n");
			return ret;
		}
	}

	return 0;
}

static void rk618_rgb_bridge_detach(struct drm_bridge *bridge)
{
	struct rk618_rgb *rgb = bridge_to_rgb(bridge);

	rockchip_drm_unregister_sub_dev(&rgb->sub_dev);
}

static const struct drm_bridge_funcs rk618_rgb_bridge_funcs = {
	.attach = rk618_rgb_bridge_attach,
	.detach = rk618_rgb_bridge_detach,
	.enable = rk618_rgb_bridge_enable,
	.disable = rk618_rgb_bridge_disable,
};

static int rk618_rgb_probe(struct platform_device *pdev)
{
	struct rk618 *rk618 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk618_rgb *rgb;
	int id, ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	rgb = devm_kzalloc(dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	rgb->dev = dev;
	rgb->parent = rk618;
	rgb->regmap = rk618->regmap;
	platform_set_drvdata(pdev, rgb);

	rgb->clock = devm_clk_get(dev, "rgb");
	if (IS_ERR(rgb->clock)) {
		ret = PTR_ERR(rgb->clock);
		dev_err(dev, "failed to get rgb clock: %d\n", ret);
		return ret;
	}

	for (id = 0; id < 2; id++) {
		struct device_node *remote, *endpoint;

		endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 1, id);
		if (!endpoint)
			continue;

		remote = of_graph_get_remote_port_parent(endpoint);
		of_node_put(endpoint);
		if (!remote) {
			dev_err(dev, "no panel/bridge connected\n");
			return -ENODEV;
		}

		rgb->panel = of_drm_find_panel(remote);
		if (IS_ERR(rgb->panel)) {
			rgb->panel = NULL;
			rgb->bridge = of_drm_find_bridge(remote);
		}
		of_node_put(remote);
		if (!rgb->panel && !rgb->bridge) {
			dev_err(dev, "Waiting for panel/bridge driver\n");
			return -EPROBE_DEFER;
		}

		rgb->id = id;
	}

	rgb->base.funcs = &rk618_rgb_bridge_funcs;
	rgb->base.of_node = dev->of_node;
	drm_bridge_add(&rgb->base);

	return 0;
}

static int rk618_rgb_remove(struct platform_device *pdev)
{
	struct rk618_rgb *rgb = platform_get_drvdata(pdev);

	drm_bridge_remove(&rgb->base);

	return 0;
}

static const struct of_device_id rk618_rgb_of_match[] = {
	{ .compatible = "rockchip,rk618-rgb", },
	{},
};
MODULE_DEVICE_TABLE(of, rk618_rgb_of_match);

static struct platform_driver rk618_rgb_driver = {
	.driver = {
		.name = "rk618-rgb",
		.of_match_table = of_match_ptr(rk618_rgb_of_match),
	},
	.probe = rk618_rgb_probe,
	.remove = rk618_rgb_remove,
};
module_platform_driver(rk618_rgb_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_AUTHOR("Chen Shunqing <csq@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK618 RGB driver");
MODULE_LICENSE("GPL v2");
