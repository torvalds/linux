// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Alexander Warnecke <awarnecke002@hotmail.com>
 * Copyright (c) 2023 Manuel Traut <manut@mecka.net>
 * Copyright (c) 2023 Dang Huynh <danct12@riseup.net>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct boe_th101mb31ig002 {
	struct drm_panel panel;

	struct mipi_dsi_device *dsi;

	struct regulator *power;
	struct gpio_desc *enable;
	struct gpio_desc *reset;

	enum drm_panel_orientation orientation;
};

static void boe_th101mb31ig002_reset(struct boe_th101mb31ig002 *ctx)
{
	gpiod_direction_output(ctx->reset, 0);
	usleep_range(10, 100);
	gpiod_direction_output(ctx->reset, 1);
	usleep_range(10, 100);
	gpiod_direction_output(ctx->reset, 0);
	usleep_range(5000, 6000);
}

static int boe_th101mb31ig002_enable(struct drm_panel *panel)
{
	struct boe_th101mb31ig002 *ctx = container_of(panel,
						      struct boe_th101mb31ig002,
						      panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	mipi_dsi_dcs_write_seq(dsi, 0xE0, 0xAB, 0xBA);
	mipi_dsi_dcs_write_seq(dsi, 0xE1, 0xBA, 0xAB);
	mipi_dsi_dcs_write_seq(dsi, 0xB1, 0x10, 0x01, 0x47, 0xFF);
	mipi_dsi_dcs_write_seq(dsi, 0xB2, 0x0C, 0x14, 0x04, 0x50, 0x50, 0x14);
	mipi_dsi_dcs_write_seq(dsi, 0xB3, 0x56, 0x53, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xB4, 0x33, 0x30, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xB6, 0xB0, 0x00, 0x00, 0x10, 0x00, 0x10,
				    0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xB8, 0x05, 0x12, 0x29, 0x49, 0x48, 0x00,
				    0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xB9, 0x7C, 0x65, 0x55, 0x49, 0x46, 0x36,
				    0x3B, 0x24, 0x3D, 0x3C, 0x3D, 0x5C, 0x4C,
				    0x55, 0x47, 0x46, 0x39, 0x26, 0x06, 0x7C,
				    0x65, 0x55, 0x49, 0x46, 0x36, 0x3B, 0x24,
				    0x3D, 0x3C, 0x3D, 0x5C, 0x4C, 0x55, 0x47,
				    0x46, 0x39, 0x26, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0xFF, 0x87, 0x12, 0x34, 0x44, 0x44,
				    0x44, 0x44, 0x98, 0x04, 0x98, 0x04, 0x0F,
				    0x00, 0x00, 0xC1);
	mipi_dsi_dcs_write_seq(dsi, 0xC1, 0x54, 0x94, 0x02, 0x85, 0x9F, 0x00,
				    0x7F, 0x00, 0x54, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xC2, 0x17, 0x09, 0x08, 0x89, 0x08, 0x11,
				    0x22, 0x20, 0x44, 0xFF, 0x18, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xC3, 0x86, 0x46, 0x05, 0x05, 0x1C, 0x1C,
				    0x1D, 0x1D, 0x02, 0x1F, 0x1F, 0x1E, 0x1E,
				    0x0F, 0x0F, 0x0D, 0x0D, 0x13, 0x13, 0x11,
				    0x11, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xC4, 0x07, 0x07, 0x04, 0x04, 0x1C, 0x1C,
				    0x1D, 0x1D, 0x02, 0x1F, 0x1F, 0x1E, 0x1E,
				    0x0E, 0x0E, 0x0C, 0x0C, 0x12, 0x12, 0x10,
				    0x10, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xC6, 0x2A, 0x2A);
	mipi_dsi_dcs_write_seq(dsi, 0xC8, 0x21, 0x00, 0x31, 0x42, 0x34, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0xCA, 0xCB, 0x43);
	mipi_dsi_dcs_write_seq(dsi, 0xCD, 0x0E, 0x4B, 0x4B, 0x20, 0x19, 0x6B,
				    0x06, 0xB3);
	mipi_dsi_dcs_write_seq(dsi, 0xD2, 0xE3, 0x2B, 0x38, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xD4, 0x00, 0x01, 0x00, 0x0E, 0x04, 0x44,
				    0x08, 0x10, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xE6, 0x80, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
				    0xFF, 0xFF);
	mipi_dsi_dcs_write_seq(dsi, 0xF0, 0x12, 0x03, 0x20, 0x00, 0xFF);
	mipi_dsi_dcs_write_seq(dsi, 0xF3, 0x00);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	msleep(120);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set panel on: %d\n", ret);
		return ret;
	}

	return 0;
}

static int boe_th101mb31ig002_disable(struct drm_panel *panel)
{
	struct boe_th101mb31ig002 *ctx = container_of(panel,
						      struct boe_th101mb31ig002,
						      panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		dev_err(dev, "Failed to set panel off: %d\n", ret);

	msleep(120);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);

	return 0;
}

static int boe_th101mb31ig002_unprepare(struct drm_panel *panel)
{
	struct boe_th101mb31ig002 *ctx = container_of(panel,
						      struct boe_th101mb31ig002,
						      panel);

	gpiod_set_value_cansleep(ctx->reset, 1);
	gpiod_set_value_cansleep(ctx->enable, 0);
	regulator_disable(ctx->power);

	return 0;
}

static int boe_th101mb31ig002_prepare(struct drm_panel *panel)
{
	struct boe_th101mb31ig002 *ctx = container_of(panel,
						      struct boe_th101mb31ig002,
						      panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_enable(ctx->power);
	if (ret) {
		dev_err(dev, "Failed to enable power supply: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(ctx->enable, 1);
	msleep(50);
	boe_th101mb31ig002_reset(ctx);
	boe_th101mb31ig002_enable(panel);

	return 0;
}

static const struct drm_display_mode boe_th101mb31ig002_default_mode = {
	.clock		= 73500,
	.hdisplay	= 800,
	.hsync_start	= 800 + 64,
	.hsync_end	= 800 + 64 + 16,
	.htotal		= 800 + 64 + 16 + 64,
	.vdisplay	= 1280,
	.vsync_start	= 1280 + 2,
	.vsync_end	= 1280 + 2 + 4,
	.vtotal		= 1280 + 2 + 4 + 12,
	.width_mm	= 135,
	.height_mm	= 216,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int boe_th101mb31ig002_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct boe_th101mb31ig002 *ctx = container_of(panel,
						      struct boe_th101mb31ig002,
						      panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev,
				  &boe_th101mb31ig002_default_mode);
	if (!mode) {
		dev_err(panel->dev, "Failed to add mode %ux%u@%u\n",
			boe_th101mb31ig002_default_mode.hdisplay,
			boe_th101mb31ig002_default_mode.vdisplay,
			drm_mode_vrefresh(&boe_th101mb31ig002_default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	connector->display_info.bpc = 8;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	/*
	 * TODO: Remove once all drm drivers call
	 * drm_connector_set_orientation_from_panel()
	 */
	drm_connector_set_panel_orientation(connector, ctx->orientation);

	drm_mode_probed_add(connector, mode);

	return 1;
}

static enum drm_panel_orientation
boe_th101mb31ig002_get_orientation(struct drm_panel *panel)
{
	struct boe_th101mb31ig002 *ctx = container_of(panel,
						      struct boe_th101mb31ig002,
						      panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs boe_th101mb31ig002_funcs = {
	.prepare = boe_th101mb31ig002_prepare,
	.unprepare = boe_th101mb31ig002_unprepare,
	.disable = boe_th101mb31ig002_disable,
	.get_modes = boe_th101mb31ig002_get_modes,
	.get_orientation = boe_th101mb31ig002_get_orientation,
};

static int boe_th101mb31ig002_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct boe_th101mb31ig002 *ctx;
	int ret;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_MODE_LPM;

	ctx->power = devm_regulator_get(&dsi->dev, "power");
	if (IS_ERR(ctx->power))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->power),
				     "Failed to get power regulator\n");

	ctx->enable = devm_gpiod_get(&dsi->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->enable))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->enable),
				     "Failed to get enable GPIO\n");

	ctx->reset = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->reset),
				     "Failed to get reset GPIO\n");

	ret = of_drm_get_panel_orientation(dsi->dev.of_node,
					   &ctx->orientation);
	if (ret)
		return dev_err_probe(&dsi->dev, ret,
				     "Failed to get orientation\n");

	drm_panel_init(&ctx->panel, &dsi->dev, &boe_th101mb31ig002_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err_probe(&dsi->dev, ret,
			      "Failed to attach panel to DSI host\n");
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void boe_th101mb31ig002_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct boe_th101mb31ig002 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boe_th101mb31ig002_of_match[] = {
	{ .compatible = "boe,th101mb31ig002-28a", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boe_th101mb31ig002_of_match);

static struct mipi_dsi_driver boe_th101mb31ig002_driver = {
	.driver = {
		.name = "boe-th101mb31ig002-28a",
		.of_match_table = boe_th101mb31ig002_of_match,
	},
	.probe = boe_th101mb31ig002_dsi_probe,
	.remove = boe_th101mb31ig002_dsi_remove,
};
module_mipi_dsi_driver(boe_th101mb31ig002_driver);

MODULE_AUTHOR("Alexander Warnecke <awarnecke002@hotmail.com>");
MODULE_DESCRIPTION("BOE TH101MB31IG002-28A MIPI-DSI LCD panel");
MODULE_LICENSE("GPL");
