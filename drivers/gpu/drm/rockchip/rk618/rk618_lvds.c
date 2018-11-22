// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/mfd/rk618.h>

#include <drm/drmP.h>
#include <drm/drm_of.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>

#include <video/of_display_timing.h>
#include <video/videomode.h>

#include "rk618_dither.h"

enum {
	LVDS_8BIT_MODE_FORMAT_1,
	LVDS_8BIT_MODE_FORMAT_2,
	LVDS_8BIT_MODE_FORMAT_3,
	LVDS_6BIT_MODE,
};

struct rk618_lvds {
	struct drm_bridge base;
	struct drm_connector connector;
	struct drm_panel *panel;
	struct device *dev;
	struct regmap *regmap;
	struct clk *clock;
	struct rk618 *parent;
	bool dual_channel;
	u32 format;
};

static inline struct rk618_lvds *bridge_to_lvds(struct drm_bridge *b)
{
	return container_of(b, struct rk618_lvds, base);
}

static inline struct rk618_lvds *connector_to_lvds(struct drm_connector *c)
{
	return container_of(c, struct rk618_lvds, connector);
}

static struct drm_encoder *
rk618_lvds_connector_best_encoder(struct drm_connector *connector)
{
	struct rk618_lvds *lvds = connector_to_lvds(connector);

	return lvds->base.encoder;
}

static int rk618_lvds_connector_get_modes(struct drm_connector *connector)
{
	struct rk618_lvds *lvds = connector_to_lvds(connector);

	return drm_panel_get_modes(lvds->panel);
}

static const struct drm_connector_helper_funcs
rk618_lvds_connector_helper_funcs = {
	.get_modes = rk618_lvds_connector_get_modes,
	.best_encoder = rk618_lvds_connector_best_encoder,
};

static enum drm_connector_status
rk618_lvds_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void rk618_lvds_connector_destroy(struct drm_connector *connector)
{
	struct rk618_lvds *lvds = connector_to_lvds(connector);

	drm_panel_detach(lvds->panel);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rk618_lvds_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = rk618_lvds_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rk618_lvds_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void rk618_lvds_bridge_enable(struct drm_bridge *bridge)
{
	struct rk618_lvds *lvds = bridge_to_lvds(bridge);
	u32 value;

	clk_prepare_enable(lvds->clock);

	rk618_frc_dclk_invert(lvds->parent);

	value = LVDS_CON_CHA0TTL_DISABLE | LVDS_CON_CHA1TTL_DISABLE |
		LVDS_CON_CHA0_POWER_UP | LVDS_CON_CBG_POWER_UP |
		LVDS_CON_PLL_POWER_UP | LVDS_CON_SELECT(lvds->format);

	if (lvds->dual_channel)
		value |= LVDS_CON_CHA1_POWER_UP | LVDS_DCLK_INV |
			 LVDS_CON_CHASEL_DOUBLE_CHANNEL;
	else
		value |= LVDS_CON_CHA1_POWER_DOWN |
			 LVDS_CON_CHASEL_SINGLE_CHANNEL;

	regmap_write(lvds->regmap, RK618_LVDS_CON, value);

	drm_panel_prepare(lvds->panel);
	drm_panel_enable(lvds->panel);
}

static void rk618_lvds_bridge_disable(struct drm_bridge *bridge)
{
	struct rk618_lvds *lvds = bridge_to_lvds(bridge);

	drm_panel_disable(lvds->panel);
	drm_panel_unprepare(lvds->panel);

	regmap_write(lvds->regmap, RK618_LVDS_CON,
		     LVDS_CON_CHA0_POWER_DOWN | LVDS_CON_CHA1_POWER_DOWN |
		     LVDS_CON_CBG_POWER_DOWN | LVDS_CON_PLL_POWER_DOWN);

	clk_disable_unprepare(lvds->clock);
}

static void rk618_lvds_bridge_mode_set(struct drm_bridge *bridge,
				       struct drm_display_mode *mode,
				       struct drm_display_mode *adj)
{
	struct rk618_lvds *lvds = bridge_to_lvds(bridge);
	struct drm_connector *connector = &lvds->connector;
	struct drm_display_info *info = &connector->display_info;
	u32 bus_format;

	if (info->num_bus_formats)
		bus_format = info->bus_formats[0];
	else
		bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG;

	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:	/* jeida-18 */
		lvds->format = LVDS_6BIT_MODE;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:	/* jeida-24 */
		lvds->format = LVDS_8BIT_MODE_FORMAT_2;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:	/* vesa-24 */
		lvds->format = LVDS_8BIT_MODE_FORMAT_1;
		break;
	default:
		lvds->format = LVDS_8BIT_MODE_FORMAT_3;
		break;
	}
}

static int rk618_lvds_bridge_attach(struct drm_bridge *bridge)
{
	struct rk618_lvds *lvds = bridge_to_lvds(bridge);
	struct device *dev = lvds->dev;
	struct drm_connector *connector = &lvds->connector;
	struct drm_device *drm = bridge->dev;
	int ret;

	connector->port = dev->of_node;

	ret = drm_connector_init(drm, connector, &rk618_lvds_connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret) {
		dev_err(lvds->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &rk618_lvds_connector_helper_funcs);
	drm_mode_connector_attach_encoder(connector, bridge->encoder);

	ret = drm_panel_attach(lvds->panel, connector);
	if (ret) {
		dev_err(lvds->dev, "Failed to attach panel\n");
		return ret;
	}

	return 0;
}

static const struct drm_bridge_funcs rk618_lvds_bridge_funcs = {
	.attach = rk618_lvds_bridge_attach,
	.mode_set = rk618_lvds_bridge_mode_set,
	.enable = rk618_lvds_bridge_enable,
	.disable = rk618_lvds_bridge_disable,
};

static int rk618_lvds_parse_dt(struct rk618_lvds *lvds)
{
	struct device *dev = lvds->dev;

	lvds->dual_channel = of_property_read_bool(dev->of_node,
						   "dual-channel");

	return 0;
}

static int rk618_lvds_probe(struct platform_device *pdev)
{
	struct rk618 *rk618 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct device_node *endpoint;
	struct rk618_lvds *lvds;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	lvds = devm_kzalloc(dev, sizeof(*lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;

	lvds->dev = dev;
	lvds->parent = rk618;
	platform_set_drvdata(pdev, lvds);

	ret = rk618_lvds_parse_dt(lvds);
	if (ret) {
		dev_err(dev, "failed to parse DT\n");
		return ret;
	}

	lvds->regmap = dev_get_regmap(dev->parent, NULL);
	if (!lvds->regmap)
		return -ENODEV;

	endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 1, -1);
	if (endpoint) {
		struct device_node *remote;

		remote = of_graph_get_remote_port_parent(endpoint);
		of_node_put(endpoint);
		if (!remote) {
			dev_err(dev, "no panel connected\n");
			return -ENODEV;
		}

		lvds->panel = of_drm_find_panel(remote);
		of_node_put(remote);
		if (!lvds->panel) {
			dev_err(dev, "Waiting for panel driver\n");
			return -EPROBE_DEFER;
		}
	}

	lvds->clock = devm_clk_get(dev, "lvds");
	if (IS_ERR(lvds->clock)) {
		ret = PTR_ERR(lvds->clock);
		dev_err(dev, "failed to get lvds clock: %d\n", ret);
		return ret;
	}

	lvds->base.funcs = &rk618_lvds_bridge_funcs;
	lvds->base.of_node = dev->of_node;
	ret = drm_bridge_add(&lvds->base);
	if (ret) {
		dev_err(dev, "failed to add drm_bridge: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rk618_lvds_remove(struct platform_device *pdev)
{
	struct rk618_lvds *lvds = platform_get_drvdata(pdev);

	drm_bridge_remove(&lvds->base);

	return 0;
}

static const struct of_device_id rk618_lvds_of_match[] = {
	{ .compatible = "rockchip,rk618-lvds", },
	{},
};
MODULE_DEVICE_TABLE(of, rk618_lvds_of_match);

static struct platform_driver rk618_lvds_driver = {
	.driver = {
		.name = "rk618-lvds",
		.of_match_table = of_match_ptr(rk618_lvds_of_match),
	},
	.probe = rk618_lvds_probe,
	.remove = rk618_lvds_remove,
};
module_platform_driver(rk618_lvds_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK618 LVDS driver");
MODULE_LICENSE("GPL v2");
