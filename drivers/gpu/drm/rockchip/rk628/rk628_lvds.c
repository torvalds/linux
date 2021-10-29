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

#include <drm/drm_of.h>
#include <drm/drm_atomic.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>

#include <video/of_display_timing.h>
#include <video/videomode.h>

enum lvds_format {
	LVDS_FORMAT_VESA_24BIT,
	LVDS_FORMAT_JEIDA_24BIT,
	LVDS_FORMAT_JEIDA_18BIT,
	LVDS_FORMAT_VESA_18BIT,
};

enum lvds_link_type {
	LVDS_SINGLE_LINK,
	LVDS_DUAL_LINK_ODD_EVEN_PIXELS,
	LVDS_DUAL_LINK_EVEN_ODD_PIXELS,
	LVDS_DUAL_LINK_LEFT_RIGHT_PIXELS,
	LVDS_DUAL_LINK_RIGHT_LEFT_PIXELS,
};

struct rk628_lvds {
	struct drm_bridge base;
	struct drm_connector connector;
	struct drm_panel *panel;
	struct drm_display_mode mode;
	struct device *dev;
	struct regmap *grf;
	struct phy *phy;
	struct rk628 *parent;
	enum lvds_format format;
	enum lvds_link_type link_type;
};

static inline struct rk628_lvds *bridge_to_lvds(struct drm_bridge *b)
{
	return container_of(b, struct rk628_lvds, base);
}

static inline struct rk628_lvds *connector_to_lvds(struct drm_connector *c)
{
	return container_of(c, struct rk628_lvds, connector);
}

static enum lvds_format rk628_lvds_get_format(u32 bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
		return LVDS_FORMAT_JEIDA_24BIT;
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		return LVDS_FORMAT_VESA_18BIT;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
	default:
		return LVDS_FORMAT_VESA_24BIT;
	}
}

static enum lvds_link_type rk628_lvds_get_link_type(struct rk628_lvds *lvds)
{
	struct device *dev = lvds->dev;
	const char *str;
	int ret;

	ret = of_property_read_string(dev->of_node, "rockchip,link-type", &str);
	if (ret < 0)
		return LVDS_SINGLE_LINK;

	if (!strcmp(str, "dual-link-odd-even-pixels"))
		return LVDS_DUAL_LINK_ODD_EVEN_PIXELS;
	else if (!strcmp(str, "dual-link-even-odd-pixels"))
		return LVDS_DUAL_LINK_EVEN_ODD_PIXELS;
	else if (!strcmp(str, "dual-link-left-right-pixels"))
		return LVDS_DUAL_LINK_LEFT_RIGHT_PIXELS;
	else if (!strcmp(str, "dual-link-right-left-pixels"))
		return LVDS_DUAL_LINK_RIGHT_LEFT_PIXELS;
	else
		return LVDS_SINGLE_LINK;
}

static struct drm_encoder *
rk628_lvds_connector_best_encoder(struct drm_connector *connector)
{
	struct rk628_lvds *lvds = connector_to_lvds(connector);

	return lvds->base.encoder;
}

static int rk628_lvds_connector_get_modes(struct drm_connector *connector)
{
	struct rk628_lvds *lvds = connector_to_lvds(connector);
	struct drm_display_info *info = &connector->display_info;
	int num_modes = 0;

	num_modes = drm_panel_get_modes(lvds->panel, connector);

	if (info->num_bus_formats)
		lvds->format = rk628_lvds_get_format(info->bus_formats[0]);
	else
		lvds->format = LVDS_FORMAT_VESA_24BIT;

	return num_modes;
}

static const struct drm_connector_helper_funcs
rk628_lvds_connector_helper_funcs = {
	.get_modes = rk628_lvds_connector_get_modes,
	.best_encoder = rk628_lvds_connector_best_encoder,
};

static void rk628_lvds_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rk628_lvds_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rk628_lvds_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void rk628_lvds_bridge_enable(struct drm_bridge *bridge)
{
	struct rk628_lvds *lvds = bridge_to_lvds(bridge);
	const struct drm_display_mode *mode = &lvds->mode;
	u32 val, bus_width;
	int ret;

	regmap_update_bits(lvds->grf, GRF_SYSTEM_CON0, SW_OUTPUT_MODE_MASK,
			   SW_OUTPUT_MODE(OUTPUT_MODE_LVDS));

	switch (lvds->link_type) {
	case LVDS_DUAL_LINK_ODD_EVEN_PIXELS:
		val = SW_LVDS_CON_CHASEL(1) | SW_LVDS_CON_STARTSEL(0) |
		      SW_LVDS_CON_DUAL_SEL(0);
		bus_width = COMBTXPHY_MODULEA_EN | COMBTXPHY_MODULEB_EN;
		break;
	case LVDS_DUAL_LINK_EVEN_ODD_PIXELS:
		val = SW_LVDS_CON_CHASEL(1) | SW_LVDS_CON_STARTSEL(1) |
		      SW_LVDS_CON_DUAL_SEL(0);
		bus_width = COMBTXPHY_MODULEA_EN | COMBTXPHY_MODULEB_EN;
		break;
	case LVDS_DUAL_LINK_LEFT_RIGHT_PIXELS:
		val = SW_LVDS_CON_CHASEL(1) | SW_LVDS_CON_STARTSEL(0) |
		      SW_LVDS_CON_DUAL_SEL(1);
		regmap_update_bits(lvds->grf, GRF_POST_PROC_CON,
				   SW_SPLIT_EN, SW_SPLIT_EN);
		bus_width = COMBTXPHY_MODULEA_EN | COMBTXPHY_MODULEB_EN;
		break;
	case LVDS_DUAL_LINK_RIGHT_LEFT_PIXELS:
		val = SW_LVDS_CON_CHASEL(1) | SW_LVDS_CON_STARTSEL(1) |
		      SW_LVDS_CON_DUAL_SEL(1);
		regmap_update_bits(lvds->grf, GRF_POST_PROC_CON,
				   SW_SPLIT_EN, SW_SPLIT_EN);
		bus_width = COMBTXPHY_MODULEA_EN | COMBTXPHY_MODULEB_EN;
		break;
	case LVDS_SINGLE_LINK:
	default:
		val = SW_LVDS_CON_CHASEL(0) | SW_LVDS_CON_STARTSEL(0) |
		      SW_LVDS_CON_DUAL_SEL(0);
		bus_width = COMBTXPHY_MODULEA_EN;
		break;
	}

	val |= SW_LVDS_CON_SELECT(lvds->format) |
	       SW_LVDS_CON_MSBSEL(0) |
	       SW_LVDS_CON_CLKINV(0);
	regmap_write(lvds->grf, GRF_LVDS_TX_CON, val);

	bus_width |= (mode->clock / 1000) << 8;
	phy_set_bus_width(lvds->phy, bus_width);

	ret = phy_set_mode(lvds->phy, PHY_MODE_LVDS);
	if (ret) {
		dev_err(lvds->dev, "failed to set phy mode: %d\n", ret);
		return;
	}

	phy_power_on(lvds->phy);

	drm_panel_prepare(lvds->panel);
	drm_panel_enable(lvds->panel);
}

static void rk628_lvds_bridge_disable(struct drm_bridge *bridge)
{
	struct rk628_lvds *lvds = bridge_to_lvds(bridge);

	drm_panel_disable(lvds->panel);
	drm_panel_unprepare(lvds->panel);
	phy_power_off(lvds->phy);
}

static int rk628_lvds_bridge_attach(struct drm_bridge *bridge,
				    enum drm_bridge_attach_flags flags)
{
	struct rk628_lvds *lvds = bridge_to_lvds(bridge);
	struct drm_connector *connector = &lvds->connector;
	struct drm_device *drm = bridge->dev;
	int ret;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	ret = drm_connector_init(drm, connector, &rk628_lvds_connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret) {
		dev_err(lvds->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &rk628_lvds_connector_helper_funcs);
	drm_connector_attach_encoder(connector, bridge->encoder);

	return 0;
}

static void rk628_lvds_bridge_mode_set(struct drm_bridge *bridge,
				       const struct drm_display_mode *mode,
				       const struct drm_display_mode *adj)
{
	struct rk628_lvds *lvds = bridge_to_lvds(bridge);

	drm_mode_copy(&lvds->mode, mode);
}

static const struct drm_bridge_funcs rk628_lvds_bridge_funcs = {
	.attach = rk628_lvds_bridge_attach,
	.enable = rk628_lvds_bridge_enable,
	.disable = rk628_lvds_bridge_disable,
	.mode_set = rk628_lvds_bridge_mode_set,
};

static int rk628_lvds_probe(struct platform_device *pdev)
{
	struct rk628 *rk628 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk628_lvds *lvds;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	lvds = devm_kzalloc(dev, sizeof(*lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1,
					  &lvds->panel, NULL);
	if (ret)
		return ret;

	lvds->dev = dev;
	lvds->parent = rk628;
	lvds->grf = rk628->grf;
	lvds->link_type = rk628_lvds_get_link_type(lvds);
	platform_set_drvdata(pdev, lvds);

	lvds->phy = devm_of_phy_get(dev, dev->of_node, NULL);
	if (IS_ERR(lvds->phy)) {
		ret = PTR_ERR(lvds->phy);
		dev_err(dev, "failed to get phy: %d\n", ret);
		return ret;
	}

	lvds->base.funcs = &rk628_lvds_bridge_funcs;
	lvds->base.of_node = dev->of_node;
	drm_bridge_add(&lvds->base);

	return 0;
}

static int rk628_lvds_remove(struct platform_device *pdev)
{
	struct rk628_lvds *lvds = platform_get_drvdata(pdev);

	drm_bridge_remove(&lvds->base);

	return 0;
}

static const struct of_device_id rk628_lvds_of_match[] = {
	{ .compatible = "rockchip,rk628-lvds", },
	{},
};
MODULE_DEVICE_TABLE(of, rk628_lvds_of_match);

static struct platform_driver rk628_lvds_driver = {
	.driver = {
		.name = "rk628-lvds",
		.of_match_table = of_match_ptr(rk628_lvds_of_match),
	},
	.probe = rk628_lvds_probe,
	.remove = rk628_lvds_remove,
};
module_platform_driver(rk628_lvds_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 LVDS driver");
MODULE_LICENSE("GPL v2");
