// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 NXP
 * Based on panel-raspberrypi-touchscreen by Broadcom
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>

#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

struct ws_bridge {
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct backlight_device *backlight;
	struct device *dev;
	struct regmap *reg_map;
};

static const struct regmap_config ws_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
};

static struct ws_bridge *bridge_to_ws_bridge(struct drm_bridge *bridge)
{
	return container_of(bridge, struct ws_bridge, bridge);
}

static int ws_bridge_attach_dsi(struct ws_bridge *ws)
{
	const struct mipi_dsi_device_info info = {
		.type = "ws-bridge",
		.channel = 0,
		.node = NULL,
	};
	struct device_node *dsi_host_node;
	struct device *dev = ws->dev;
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	int ret;

	dsi_host_node = of_graph_get_remote_node(dev->of_node, 0, 0);
	if (!dsi_host_node) {
		dev_err(dev, "Failed to get remote port\n");
		return -ENODEV;
	}
	host = of_find_mipi_dsi_host_by_node(dsi_host_node);
	of_node_put(dsi_host_node);
	if (!host)
		return dev_err_probe(dev, -EPROBE_DEFER, "Failed to find dsi_host\n");

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi))
		return dev_err_probe(dev, PTR_ERR(dsi), "Failed to create dsi device\n");

	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 2;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to attach dsi to host\n");

	return 0;
}

static int ws_bridge_bridge_attach(struct drm_bridge *bridge,
				   struct drm_encoder *encoder,
				   enum drm_bridge_attach_flags flags)
{
	struct ws_bridge *ws = bridge_to_ws_bridge(bridge);
	int ret;

	ret = ws_bridge_attach_dsi(ws);
	if (ret)
		return ret;

	return drm_bridge_attach(encoder, ws->next_bridge,
				 &ws->bridge, flags);
}

static void ws_bridge_bridge_enable(struct drm_bridge *bridge)
{
	struct ws_bridge *ws = bridge_to_ws_bridge(bridge);

	regmap_write(ws->reg_map, 0xad, 0x01);
	backlight_enable(ws->backlight);
}

static void ws_bridge_bridge_disable(struct drm_bridge *bridge)
{
	struct ws_bridge *ws = bridge_to_ws_bridge(bridge);

	backlight_disable(ws->backlight);
	regmap_write(ws->reg_map, 0xad, 0x00);
}

static const struct drm_bridge_funcs ws_bridge_bridge_funcs = {
	.enable = ws_bridge_bridge_enable,
	.disable = ws_bridge_bridge_disable,
	.attach = ws_bridge_bridge_attach,
};

static int ws_bridge_bl_update_status(struct backlight_device *bl)
{
	struct ws_bridge *ws = bl_get_data(bl);

	regmap_write(ws->reg_map, 0xab, 0xff - backlight_get_brightness(bl));
	regmap_write(ws->reg_map, 0xaa, 0x01);

	return 0;
}

static const struct backlight_ops ws_bridge_bl_ops = {
	.update_status = ws_bridge_bl_update_status,
};

static struct backlight_device *ws_bridge_create_backlight(struct ws_bridge *ws)
{
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};
	struct device *dev = ws->dev;

	return devm_backlight_device_register(dev, dev_name(dev), dev, ws,
					      &ws_bridge_bl_ops, &props);
}

static int ws_bridge_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct drm_panel *panel;
	struct ws_bridge *ws;
	int ret;

	ws = devm_drm_bridge_alloc(dev, struct ws_bridge, bridge, &ws_bridge_bridge_funcs);
	if (IS_ERR(ws))
		return PTR_ERR(ws);

	ws->dev = dev;

	ws->reg_map = devm_regmap_init_i2c(i2c, &ws_regmap_config);
	if (IS_ERR(ws->reg_map))
		return dev_err_probe(dev, PTR_ERR(ws->reg_map), "Failed to allocate regmap\n");

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1, &panel, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to find remote panel\n");

	ws->next_bridge = devm_drm_panel_bridge_add(dev, panel);
	if (IS_ERR(ws->next_bridge))
		return PTR_ERR(ws->next_bridge);

	ws->backlight = ws_bridge_create_backlight(ws);
	if (IS_ERR(ws->backlight)) {
		ret = PTR_ERR(ws->backlight);
		dev_err(dev, "Failed to create backlight: %d\n", ret);
		return ret;
	}

	regmap_write(ws->reg_map, 0xc0, 0x01);
	regmap_write(ws->reg_map, 0xc2, 0x01);
	regmap_write(ws->reg_map, 0xac, 0x01);

	ws->bridge.type = DRM_MODE_CONNECTOR_DPI;
	ws->bridge.of_node = dev->of_node;
	devm_drm_bridge_add(dev, &ws->bridge);

	return 0;
}

static const struct of_device_id ws_bridge_of_ids[] = {
	{.compatible = "waveshare,dsi2dpi",},
	{ }
};

MODULE_DEVICE_TABLE(of, ws_bridge_of_ids);

static struct i2c_driver ws_bridge_driver = {
	.driver = {
		.name = "ws_dsi2dpi",
		.of_match_table = ws_bridge_of_ids,
	},
	.probe = ws_bridge_probe,
};
module_i2c_driver(ws_bridge_driver);

MODULE_AUTHOR("Joseph Guo <qijian.guo@nxp.com>");
MODULE_DESCRIPTION("Waveshare DSI2DPI bridge driver");
MODULE_LICENSE("GPL");
