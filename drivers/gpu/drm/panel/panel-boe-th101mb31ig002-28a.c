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
#include <drm/drm_probe_helper.h>

struct boe_th101mb31ig002;

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	int (*init)(struct boe_th101mb31ig002 *ctx);
	unsigned int lanes;
	bool lp11_before_reset;
	unsigned int vcioo_to_lp11_delay_ms;
	unsigned int lp11_to_reset_delay_ms;
	unsigned int backlight_off_to_display_off_delay_ms;
	unsigned int enter_sleep_to_reset_down_delay_ms;
	unsigned int power_off_delay_ms;
};

struct boe_th101mb31ig002 {
	struct drm_panel panel;

	struct mipi_dsi_device *dsi;

	const struct panel_desc *desc;

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

static int boe_th101mb31ig002_enable(struct boe_th101mb31ig002 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0xab, 0xba);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1, 0xba, 0xab);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0x10, 0x01, 0x47, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x0c, 0x14, 0x04, 0x50, 0x50, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x56, 0x53, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x33, 0x30, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0xb0, 0x00, 0x00, 0x10, 0x00, 0x10,
					       0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x05, 0x12, 0x29, 0x49, 0x48, 0x00,
					       0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x7c, 0x65, 0x55, 0x49, 0x46, 0x36,
					       0x3b, 0x24, 0x3d, 0x3c, 0x3d, 0x5c, 0x4c,
					       0x55, 0x47, 0x46, 0x39, 0x26, 0x06, 0x7c,
					       0x65, 0x55, 0x49, 0x46, 0x36, 0x3b, 0x24,
					       0x3d, 0x3c, 0x3d, 0x5c, 0x4c, 0x55, 0x47,
					       0x46, 0x39, 0x26, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xff, 0x87, 0x12, 0x34, 0x44, 0x44,
					       0x44, 0x44, 0x98, 0x04, 0x98, 0x04, 0x0f,
					       0x00, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1, 0x54, 0x94, 0x02, 0x85, 0x9f, 0x00,
					       0x7f, 0x00, 0x54, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2, 0x17, 0x09, 0x08, 0x89, 0x08, 0x11,
					       0x22, 0x20, 0x44, 0xff, 0x18, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3, 0x86, 0x46, 0x05, 0x05, 0x1c, 0x1c,
					       0x1d, 0x1d, 0x02, 0x1f, 0x1f, 0x1e, 0x1e,
					       0x0f, 0x0f, 0x0d, 0x0d, 0x13, 0x13, 0x11,
					       0x11, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc4, 0x07, 0x07, 0x04, 0x04, 0x1c, 0x1c,
					       0x1d, 0x1d, 0x02, 0x1f, 0x1f, 0x1e, 0x1e,
					       0x0e, 0x0e, 0x0c, 0x0c, 0x12, 0x12, 0x10,
					       0x10, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x2a, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc8, 0x21, 0x00, 0x31, 0x42, 0x34, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca, 0xcb, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcd, 0x0e, 0x4b, 0x4b, 0x20, 0x19, 0x6b,
					       0x06, 0xb3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0xe3, 0x2b, 0x38, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd4, 0x00, 0x01, 0x00, 0x0e, 0x04, 0x44,
					       0x08, 0x10, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe6, 0x80, 0x01, 0xff, 0xff, 0xff, 0xff,
					       0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x12, 0x03, 0x20, 0x00, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x00);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int starry_er88577_init_cmd(struct boe_th101mb31ig002 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	msleep(70);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0xab, 0xba);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1, 0xba, 0xab);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0x10, 0x01, 0x47, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x0c, 0x14, 0x04, 0x50, 0x50, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x56, 0x53, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x33, 0x30, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0xb0, 0x00, 0x00, 0x10, 0x00, 0x10,
					       0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x05, 0x12, 0x29, 0x49, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x7c, 0x61, 0x4f, 0x42, 0x3e, 0x2d,
					       0x31, 0x1a, 0x33, 0x33, 0x33, 0x52, 0x40,
					       0x47, 0x38, 0x34, 0x26, 0x0e, 0x06, 0x7c,
					       0x61, 0x4f, 0x42, 0x3e, 0x2d, 0x31, 0x1a,
					       0x33, 0x33, 0x33, 0x52, 0x40, 0x47, 0x38,
					       0x34, 0x26, 0x0e, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0xcc, 0x76, 0x12, 0x34, 0x44, 0x44,
					       0x44, 0x44, 0x98, 0x04, 0x98, 0x04, 0x0f,
					       0x00, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1, 0x54, 0x94, 0x02, 0x85, 0x9f, 0x00,
					       0x6f, 0x00, 0x54, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2, 0x17, 0x09, 0x08, 0x89, 0x08, 0x11,
					       0x22, 0x20, 0x44, 0xff, 0x18, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3, 0x87, 0x47, 0x05, 0x05, 0x1c, 0x1c,
					       0x1d, 0x1d, 0x02, 0x1e, 0x1e, 0x1f, 0x1f,
					       0x0f, 0x0f, 0x0d, 0x0d, 0x13, 0x13, 0x11,
					       0x11, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc4, 0x06, 0x06, 0x04, 0x04, 0x1c, 0x1c,
					       0x1d, 0x1d, 0x02, 0x1e, 0x1e, 0x1f, 0x1f,
					       0x0e, 0x0e, 0x0c, 0x0c, 0x12, 0x12, 0x10,
					       0x10, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc8, 0x21, 0x00, 0x31, 0x42, 0x34, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca, 0xcb, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcd, 0x0e, 0x4b, 0x4b, 0x20, 0x19, 0x6b,
					       0x06, 0xb3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd1, 0x40, 0x0d, 0xff, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0xe3, 0x2b, 0x38, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd3, 0x00, 0x00, 0x00, 0x00,
					       0x00, 0x33, 0x20, 0x3a, 0xd5, 0x86, 0xf3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd4, 0x00, 0x01, 0x00, 0x0e, 0x04, 0x44,
					       0x08, 0x10, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe6, 0x80, 0x09, 0xff, 0xff, 0xff, 0xff,
					       0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x12, 0x03, 0x20, 0x00, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x00);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int boe_th101mb31ig002_disable(struct drm_panel *panel)
{
	struct boe_th101mb31ig002 *ctx = container_of(panel,
						      struct boe_th101mb31ig002,
						      panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	if (ctx->desc->backlight_off_to_display_off_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, ctx->desc->backlight_off_to_display_off_delay_ms);

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	if (ctx->desc->enter_sleep_to_reset_down_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, ctx->desc->enter_sleep_to_reset_down_delay_ms);

	return dsi_ctx.accum_err;
}

static int boe_th101mb31ig002_unprepare(struct drm_panel *panel)
{
	struct boe_th101mb31ig002 *ctx = container_of(panel,
						      struct boe_th101mb31ig002,
						      panel);

	gpiod_set_value_cansleep(ctx->reset, 1);
	gpiod_set_value_cansleep(ctx->enable, 0);
	regulator_disable(ctx->power);

	if (ctx->desc->power_off_delay_ms)
		msleep(ctx->desc->power_off_delay_ms);

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

	if (ctx->desc->vcioo_to_lp11_delay_ms)
		msleep(ctx->desc->vcioo_to_lp11_delay_ms);

	if (ctx->desc->lp11_before_reset) {
		ret = mipi_dsi_dcs_nop(ctx->dsi);
		if (ret)
			return ret;
	}

	if (ctx->desc->lp11_to_reset_delay_ms)
		msleep(ctx->desc->lp11_to_reset_delay_ms);

	gpiod_set_value_cansleep(ctx->enable, 1);
	msleep(50);
	boe_th101mb31ig002_reset(ctx);

	ret = ctx->desc->init(ctx);
	if (ret)
		return ret;

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

static const struct panel_desc boe_th101mb31ig002_desc = {
	.modes = &boe_th101mb31ig002_default_mode,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_MODE_LPM,
	.init = boe_th101mb31ig002_enable,
};

static const struct drm_display_mode starry_er88577_default_mode = {
	.clock	= (800 + 25 + 25 + 25) * (1280 + 20 + 4 + 12) * 60 / 1000,
	.hdisplay = 800,
	.hsync_start = 800 + 25,
	.hsync_end = 800 + 25 + 25,
	.htotal = 800 + 25 + 25 + 25,
	.vdisplay = 1280,
	.vsync_start = 1280 + 20,
	.vsync_end = 1280 + 20 + 4,
	.vtotal = 1280 + 20 + 4 + 12,
	.width_mm = 135,
	.height_mm = 216,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct panel_desc starry_er88577_desc = {
	.modes = &starry_er88577_default_mode,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM,
	.init = starry_er88577_init_cmd,
	.lp11_before_reset = true,
	.vcioo_to_lp11_delay_ms = 5,
	.lp11_to_reset_delay_ms = 50,
	.backlight_off_to_display_off_delay_ms = 100,
	.enter_sleep_to_reset_down_delay_ms = 100,
	.power_off_delay_ms = 1000,
};

static int boe_th101mb31ig002_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct boe_th101mb31ig002 *ctx = container_of(panel,
						      struct boe_th101mb31ig002,
						      panel);
	const struct drm_display_mode *desc_mode = ctx->desc->modes;

	connector->display_info.bpc = 8;
	/*
	 * TODO: Remove once all drm drivers call
	 * drm_connector_set_orientation_from_panel()
	 */
	drm_connector_set_panel_orientation(connector, ctx->orientation);

	return drm_connector_helper_get_modes_fixed(connector, desc_mode);
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
	const struct panel_desc *desc;
	int ret;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	desc = of_device_get_match_data(&dsi->dev);
	dsi->lanes = desc->lanes;
	dsi->format = desc->format;
	dsi->mode_flags = desc->mode_flags;
	ctx->desc = desc;

	ctx->power = devm_regulator_get(&dsi->dev, "power");
	if (IS_ERR(ctx->power))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->power),
				     "Failed to get power regulator\n");

	ctx->enable = devm_gpiod_get(&dsi->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->enable))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->enable),
				     "Failed to get enable GPIO\n");

	ctx->reset = devm_gpiod_get_optional(&dsi->dev, "reset", GPIOD_OUT_HIGH);
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
	{
		.compatible = "boe,th101mb31ig002-28a",
		.data = &boe_th101mb31ig002_desc
	},
	{
		.compatible = "starry,er88577",
		.data = &starry_er88577_desc
	},
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
