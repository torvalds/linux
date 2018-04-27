// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <video/of_display_timing.h>

#include <drm/drmP.h>
#include <drm/drm_of.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>

struct lt8912 {
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct drm_display_mode mode;
	struct device *dev;
	struct mipi_dsi_device *dsi;
	struct regmap *regmap[3];
	struct gpio_desc *reset_n;
};

static inline struct lt8912 *bridge_to_lt8912(struct drm_bridge *b)
{
	return container_of(b, struct lt8912, bridge);
}

static inline struct lt8912 *connector_to_lt8912(struct drm_connector *c)
{
	return container_of(c, struct lt8912, connector);
}

/* LT8912 MIPI to HDMI & LVDS REG setting - 20180115.txt */
static void lt8912_init(struct lt8912 *lt8912)
{
	u8 lanes = lt8912->dsi->lanes;
	const struct drm_display_mode *mode = &lt8912->mode;
	u32 hactive, hfp, hsync, hbp, vfp, vsync, vbp, htotal, vtotal;
	unsigned int version[2];

	/* TODO: lvds output init */

	hactive = mode->hdisplay;
	hfp = mode->hsync_start - mode->hdisplay;
	hsync = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vsync = mode->vsync_end - mode->vsync_start;
	vbp = mode->vtotal - mode->vsync_end;
	htotal = mode->htotal;
	vtotal = mode->vtotal;

	regmap_read(lt8912->regmap[0], 0x00, &version[0]);
	regmap_read(lt8912->regmap[0], 0x01, &version[1]);

	dev_info(lt8912->dev, "LT8912 ID: %02x, %02x\n",
		 version[0], version[1]);

	/* DigitalClockEn */
	regmap_write(lt8912->regmap[0], 0x08, 0xff);
	regmap_write(lt8912->regmap[0], 0x09, 0x81);
	regmap_write(lt8912->regmap[0], 0x0a, 0xff);
	regmap_write(lt8912->regmap[0], 0x0b, 0x64);
	regmap_write(lt8912->regmap[0], 0x0c, 0xff);

	regmap_write(lt8912->regmap[0], 0x44, 0x31);
	regmap_write(lt8912->regmap[0], 0x51, 0x1f);

	/* TxAnalog */
	regmap_write(lt8912->regmap[0], 0x31, 0xa1);
	regmap_write(lt8912->regmap[0], 0x32, 0xa1);
	regmap_write(lt8912->regmap[0], 0x33, 0x03);
	regmap_write(lt8912->regmap[0], 0x37, 0x00);
	regmap_write(lt8912->regmap[0], 0x38, 0x22);
	regmap_write(lt8912->regmap[0], 0x60, 0x82);

	/* CbusAnalog */
	regmap_write(lt8912->regmap[0], 0x39, 0x45);
	regmap_write(lt8912->regmap[0], 0x3b, 0x00);

	/* HDMIPllAnalog */
	regmap_write(lt8912->regmap[0], 0x44, 0x31);
	regmap_write(lt8912->regmap[0], 0x55, 0x44);
	regmap_write(lt8912->regmap[0], 0x57, 0x01);
	regmap_write(lt8912->regmap[0], 0x5a, 0x02);

	/* MipiBasicSet */
	regmap_write(lt8912->regmap[1], 0x10, 0x01);
	regmap_write(lt8912->regmap[1], 0x11, 0x08);
	regmap_write(lt8912->regmap[1], 0x12, 0x04);
	regmap_write(lt8912->regmap[1], 0x13, lanes % 4);
	regmap_write(lt8912->regmap[1], 0x14, 0x00);

	regmap_write(lt8912->regmap[1], 0x15, 0x00);
	regmap_write(lt8912->regmap[1], 0x1a, 0x03);
	regmap_write(lt8912->regmap[1], 0x1b, 0x03);

	/* MIPIDig */
	regmap_write(lt8912->regmap[1], 0x18, hsync);
	regmap_write(lt8912->regmap[1], 0x19, vsync);
	regmap_write(lt8912->regmap[1], 0x1c, hactive);
	regmap_write(lt8912->regmap[1], 0x1d, hactive >> 8);

	regmap_write(lt8912->regmap[1], 0x1e, 0x67);
	regmap_write(lt8912->regmap[1], 0x2f, 0x0c);

	regmap_write(lt8912->regmap[1], 0x34, htotal);
	regmap_write(lt8912->regmap[1], 0x35, htotal >> 8);
	regmap_write(lt8912->regmap[1], 0x36, vtotal);
	regmap_write(lt8912->regmap[1], 0x37, vtotal >> 8);
	regmap_write(lt8912->regmap[1], 0x38, vbp);
	regmap_write(lt8912->regmap[1], 0x39, vbp >> 8);
	regmap_write(lt8912->regmap[1], 0x3a, vfp);
	regmap_write(lt8912->regmap[1], 0x3b, vfp >> 8);
	regmap_write(lt8912->regmap[1], 0x3c, hbp);
	regmap_write(lt8912->regmap[1], 0x3d, hbp >> 8);
	regmap_write(lt8912->regmap[1], 0x3e, hfp);
	regmap_write(lt8912->regmap[1], 0x3f, hfp >> 8);

	/* DDSConfig */
	regmap_write(lt8912->regmap[1], 0x4e, 0x52);
	regmap_write(lt8912->regmap[1], 0x4f, 0xde);
	regmap_write(lt8912->regmap[1], 0x50, 0xc0);
	regmap_write(lt8912->regmap[1], 0x51, 0x80);
	regmap_write(lt8912->regmap[1], 0x51, 0x00);

	regmap_write(lt8912->regmap[1], 0x1f, 0x5e);
	regmap_write(lt8912->regmap[1], 0x20, 0x01);
	regmap_write(lt8912->regmap[1], 0x21, 0x2c);
	regmap_write(lt8912->regmap[1], 0x22, 0x01);
	regmap_write(lt8912->regmap[1], 0x23, 0xfa);
	regmap_write(lt8912->regmap[1], 0x24, 0x00);
	regmap_write(lt8912->regmap[1], 0x25, 0xc8);
	regmap_write(lt8912->regmap[1], 0x26, 0x00);
	regmap_write(lt8912->regmap[1], 0x27, 0x5e);
	regmap_write(lt8912->regmap[1], 0x28, 0x01);
	regmap_write(lt8912->regmap[1], 0x29, 0x2c);
	regmap_write(lt8912->regmap[1], 0x2a, 0x01);
	regmap_write(lt8912->regmap[1], 0x2b, 0xfa);
	regmap_write(lt8912->regmap[1], 0x2c, 0x00);
	regmap_write(lt8912->regmap[1], 0x2d, 0xc8);
	regmap_write(lt8912->regmap[1], 0x2e, 0x00);

	regmap_write(lt8912->regmap[0], 0x03, 0x7f);
	usleep_range(10000, 20000);
	regmap_write(lt8912->regmap[0], 0x03, 0xff);

	regmap_write(lt8912->regmap[1], 0x42, 0x64);
	regmap_write(lt8912->regmap[1], 0x43, 0x00);
	regmap_write(lt8912->regmap[1], 0x44, 0x04);
	regmap_write(lt8912->regmap[1], 0x45, 0x00);
	regmap_write(lt8912->regmap[1], 0x46, 0x59);
	regmap_write(lt8912->regmap[1], 0x47, 0x00);
	regmap_write(lt8912->regmap[1], 0x48, 0xf2);
	regmap_write(lt8912->regmap[1], 0x49, 0x06);
	regmap_write(lt8912->regmap[1], 0x4a, 0x00);
	regmap_write(lt8912->regmap[1], 0x4b, 0x72);
	regmap_write(lt8912->regmap[1], 0x4c, 0x45);
	regmap_write(lt8912->regmap[1], 0x4d, 0x00);
	regmap_write(lt8912->regmap[1], 0x52, 0x08);
	regmap_write(lt8912->regmap[1], 0x53, 0x00);
	regmap_write(lt8912->regmap[1], 0x54, 0xb2);
	regmap_write(lt8912->regmap[1], 0x55, 0x00);
	regmap_write(lt8912->regmap[1], 0x56, 0xe4);
	regmap_write(lt8912->regmap[1], 0x57, 0x0d);
	regmap_write(lt8912->regmap[1], 0x58, 0x00);
	regmap_write(lt8912->regmap[1], 0x59, 0xe4);
	regmap_write(lt8912->regmap[1], 0x5a, 0x8a);
	regmap_write(lt8912->regmap[1], 0x5b, 0x00);
	regmap_write(lt8912->regmap[1], 0x5c, 0x34);
	regmap_write(lt8912->regmap[1], 0x1e, 0x4f);
	regmap_write(lt8912->regmap[1], 0x51, 0x00);

	regmap_write(lt8912->regmap[0], 0xb2, 0x01);

	/* AudioIIsEn */
	regmap_write(lt8912->regmap[2], 0x06, 0x08);
	regmap_write(lt8912->regmap[2], 0x07, 0xf0);

	regmap_write(lt8912->regmap[2], 0x34, 0xd2);

	regmap_write(lt8912->regmap[2], 0x3c, 0x41);

	/* MIPIRxLogicRes */
	regmap_write(lt8912->regmap[0], 0x03, 0x7f);
	usleep_range(10000, 20000);
	regmap_write(lt8912->regmap[0], 0x03, 0xff);

	regmap_write(lt8912->regmap[1], 0x51, 0x80);
	usleep_range(10000, 20000);
	regmap_write(lt8912->regmap[1], 0x51, 0x00);
}

static void lt8912_exit(struct lt8912 *lt8912)
{
	regmap_write(lt8912->regmap[0], 0x08, 0x00);
	regmap_write(lt8912->regmap[0], 0x09, 0x81);
	regmap_write(lt8912->regmap[0], 0x0a, 0x00);
	regmap_write(lt8912->regmap[0], 0x0b, 0x20);
	regmap_write(lt8912->regmap[0], 0x0c, 0x00);

	regmap_write(lt8912->regmap[0], 0x54, 0x1d);
	regmap_write(lt8912->regmap[0], 0x51, 0x15);

	regmap_write(lt8912->regmap[0], 0x44, 0x31);
	regmap_write(lt8912->regmap[0], 0x41, 0xbd);
	regmap_write(lt8912->regmap[0], 0x5c, 0x11);

	regmap_write(lt8912->regmap[0], 0x30, 0x08);
	regmap_write(lt8912->regmap[0], 0x31, 0x00);
	regmap_write(lt8912->regmap[0], 0x32, 0x00);
	regmap_write(lt8912->regmap[0], 0x33, 0x00);
	regmap_write(lt8912->regmap[0], 0x34, 0x00);
	regmap_write(lt8912->regmap[0], 0x35, 0x00);
	regmap_write(lt8912->regmap[0], 0x36, 0x00);
	regmap_write(lt8912->regmap[0], 0x37, 0x00);
	regmap_write(lt8912->regmap[0], 0x38, 0x00);
}

static void lt8912_power_on(struct lt8912 *lt8912)
{
	gpiod_direction_output(lt8912->reset_n, 1);
	msleep(120);
	gpiod_direction_output(lt8912->reset_n, 0);
}

static void lt8912_power_off(struct lt8912 *lt8912)
{
	gpiod_direction_output(lt8912->reset_n, 1);
}

static enum drm_connector_status
lt8912_connector_detect(struct drm_connector *connector, bool force)
{
	/* TODO: HPD handing (reg[0xc1] - bit[7]) */
	return connector_status_connected;
}

static const struct drm_connector_funcs lt8912_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = lt8912_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_encoder *
lt8912_connector_best_encoder(struct drm_connector *connector)
{
	struct lt8912 *lt8912 = connector_to_lt8912(connector);

	return lt8912->bridge.encoder;
}

static int lt8912_connector_get_modes(struct drm_connector *connector)
{
	struct lt8912 *lt8912 = connector_to_lt8912(connector);
	struct drm_display_mode *mode;
	int ret;

	/* TODO: EDID handing */

	mode = drm_mode_create(connector->dev);
	if (!mode)
		return -EINVAL;

	ret = of_get_drm_display_mode(lt8912->dev->of_node, mode,
				      OF_USE_NATIVE_MODE);
	if (ret) {
		dev_err(lt8912->dev, "failed to get display timings\n");
		drm_mode_destroy(connector->dev, mode);
		return 0;
	}

	mode->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_connector_helper_funcs lt8912_connector_helper_funcs = {
	.get_modes = lt8912_connector_get_modes,
	.best_encoder = lt8912_connector_best_encoder,
};

static void lt8912_bridge_post_disable(struct drm_bridge *bridge)
{
	struct lt8912 *lt8912 = bridge_to_lt8912(bridge);

	lt8912_power_off(lt8912);
}

static void lt8912_bridge_disable(struct drm_bridge *bridge)
{
	struct lt8912 *lt8912 = bridge_to_lt8912(bridge);

	lt8912_exit(lt8912);
}

static void lt8912_bridge_enable(struct drm_bridge *bridge)
{
	struct lt8912 *lt8912 = bridge_to_lt8912(bridge);

	lt8912_init(lt8912);
}

static void lt8912_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct lt8912 *lt8912 = bridge_to_lt8912(bridge);

	lt8912_power_on(lt8912);
}

static void lt8912_bridge_mode_set(struct drm_bridge *bridge,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adj)
{
	struct lt8912 *lt8912 = bridge_to_lt8912(bridge);

	drm_mode_copy(&lt8912->mode, adj);
}

static int lt8912_bridge_attach(struct drm_bridge *bridge)
{
	struct lt8912 *lt8912 = bridge_to_lt8912(bridge);
	struct drm_connector *connector = &lt8912->connector;
	int ret;

	ret = drm_connector_init(bridge->dev, connector,
				 &lt8912_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		dev_err(lt8912->dev, "failed to initialize connector\n");
		return ret;
	}

	drm_connector_helper_add(connector, &lt8912_connector_helper_funcs);
	drm_mode_connector_attach_encoder(connector, bridge->encoder);

	return 0;
}

static const struct drm_bridge_funcs lt8912_bridge_funcs = {
	.attach = lt8912_bridge_attach,
	.mode_set = lt8912_bridge_mode_set,
	.pre_enable = lt8912_bridge_pre_enable,
	.enable = lt8912_bridge_enable,
	.disable = lt8912_bridge_disable,
	.post_disable = lt8912_bridge_post_disable,
};

static const struct regmap_config lt8912_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
};

static int lt8912_i2c_init(struct lt8912 *lt8912,
			   struct i2c_adapter *adapter)
{
	struct i2c_board_info info[] = {
		{ I2C_BOARD_INFO("lt8912p0", 0x48), },
		{ I2C_BOARD_INFO("lt8912p1", 0x49), },
		{ I2C_BOARD_INFO("lt8912p2", 0x4a), }
	};
	struct regmap *regmap;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(info); i++) {
		struct i2c_client *client;

		client = i2c_new_device(adapter, &info[i]);
		if (!client)
			return -ENODEV;

		regmap = devm_regmap_init_i2c(client, &lt8912_regmap_config);
		if (IS_ERR(regmap)) {
			ret = PTR_ERR(regmap);
			dev_err(lt8912->dev,
				"Failed to initialize regmap: %d\n", ret);
			return ret;
		}

		lt8912->regmap[i] = regmap;
	}

	return 0;
}

static int lt8912_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lt8912 *lt8912;
	struct device_node *node;
	struct i2c_adapter *adapter;
	int ret;

	lt8912 = devm_kzalloc(dev, sizeof(*lt8912), GFP_KERNEL);
	if (!lt8912)
		return -ENOMEM;

	lt8912->dev = dev;
	lt8912->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, lt8912);

	lt8912->reset_n = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(lt8912->reset_n)) {
		ret = PTR_ERR(lt8912->reset_n);
		dev_err(dev, "failed to request reset GPIO: %d\n", ret);
		return ret;
	}

	node = of_parse_phandle(dev->of_node, "i2c-bus", 0);
	if (!node) {
		dev_err(dev, "No i2c-bus found\n");
		return -ENODEV;
	}

	adapter = of_find_i2c_adapter_by_node(node);
	of_node_put(node);
	if (!adapter) {
		dev_err(dev, "No i2c adapter found\n");
		return -EPROBE_DEFER;
	}

	ret = lt8912_i2c_init(lt8912, adapter);
	if (ret)
		return ret;

	/* TODO: interrupt handing */

	lt8912->bridge.funcs = &lt8912_bridge_funcs;
	lt8912->bridge.of_node = dev->of_node;
	ret = drm_bridge_add(&lt8912->bridge);
	if (ret) {
		dev_err(dev, "failed to add bridge: %d\n", ret);
		return ret;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET;

	ret = mipi_dsi_attach(dsi);
	if (ret) {
		drm_bridge_remove(&lt8912->bridge);
		dev_err(dev, "failed to attach dsi to host: %d\n", ret);
		return ret;
	}

	return 0;
}

static int lt8912_remove(struct mipi_dsi_device *dsi)
{
	struct lt8912 *lt8912 = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_bridge_remove(&lt8912->bridge);

	return 0;
}

static const struct of_device_id lt8912_of_match[] = {
	{ .compatible = "lontium,lt8912" },
	{}
};
MODULE_DEVICE_TABLE(of, lt8912_of_match);

static struct mipi_dsi_driver lt8912_driver = {
	.driver = {
		.name = "lt8912",
		.of_match_table = lt8912_of_match,
	},
	.probe = lt8912_probe,
	.remove = lt8912_remove,
};
module_mipi_dsi_driver(lt8912_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Lontium LT8912 MIPI-DSI to LVDS and HDMI/MHL bridge");
MODULE_LICENSE("GPL v2");
