// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct auo_r61322 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data auo_r61322_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct auo_r61322 *to_auo_r61322(struct drm_panel *panel)
{
	return container_of(panel, struct auo_r61322, panel);
}

static void auo_r61322_reset(struct auo_r61322 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int auo_r61322_on(struct auo_r61322 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3,
					 0x14, 0x40, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb4, 0x0c, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb6, 0x0b, 0xd3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1,
					 0x0c, 0x60, 0x80, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x01, 0x00, 0x00, 0x00, 0x10,
					 0x02, 0x0f, 0x10, 0x00, 0x88, 0x00,
					 0x00, 0x01, 0x00, 0x00, 0x00, 0x62,
					 0x30, 0x40, 0xa5, 0x0f, 0x04, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2,
					 0x20, 0xf0, 0x07, 0x80, 0x17, 0x18,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4,
					 0x70, 0x00, 0x03, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x05, 0x01, 0x00,
					 0x00, 0x01, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6,
					 0x78, 0x28, 0x4b, 0x1e, 0x41, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb,
					 0xff, 0xff, 0x0f, 0xff, 0xff, 0x0f,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x23);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd,
					 0x01, 0x04, 0x04, 0x04, 0x04, 0x01,
					 0x03, 0x02, 0x10, 0x0f, 0x0e, 0x0d,
					 0x0c, 0x0b, 0x0a, 0x09, 0x00, 0x12,
					 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
					 0x12, 0x00, 0x09, 0x0a, 0x0b, 0x0c,
					 0x0d, 0x0e, 0x0f, 0x10, 0x02, 0x03,
					 0x01, 0x04, 0x04, 0x04, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd0,
					 0x11, 0x59, 0xbb, 0x68, 0xd9, 0x4c,
					 0x99, 0x11, 0x19, 0x8c, 0xcd, 0x14,
					 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3,
					 0xa3, 0x33, 0xbb, 0xdd, 0xd5, 0x33,
					 0x33, 0x33, 0x00, 0x00, 0x0a, 0x91,
					 0x91, 0x21, 0x21, 0x3b, 0x3b, 0x33,
					 0x33, 0x37, 0x60, 0xfd, 0xfe, 0x07,
					 0x10, 0x00, 0x00, 0x00, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5,
					 0x06, 0x00, 0x00, 0x00, 0x75, 0x00,
					 0x75, 0x01, 0x00, 0x00, 0x00, 0x00,
					 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd7,
					 0x10, 0xf0, 0x04, 0x2a, 0x04, 0x10,
					 0x00, 0x80, 0x03, 0x1d, 0xc0, 0x00,
					 0xdc, 0x10, 0x10, 0x60, 0x10, 0x07,
					 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xeb,
					 0x10, 0x55, 0x55, 0x44, 0x50, 0x55,
					 0x55, 0x55, 0x55, 0x51, 0x11, 0x44,
					 0x44, 0x44, 0x10, 0x21, 0x0e, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x44,
					 0x44, 0x44, 0x44, 0x33, 0x37, 0x44,
					 0x05);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7,
					 0x16, 0x27, 0x2e, 0x36, 0x43, 0x4e,
					 0x56, 0x64, 0x47, 0x4e, 0x59, 0x65,
					 0x6c, 0x72, 0x77, 0x0e, 0x27, 0x2e,
					 0x36, 0x43, 0x4e, 0x56, 0x64, 0x47,
					 0x4e, 0x59, 0x65, 0x6c, 0x72, 0x77);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8,
					 0x00, 0x00, 0xff, 0x05, 0xff, 0xec,
					 0x00, 0x00, 0x01, 0x04, 0xfe, 0xfc,
					 0x00, 0x00, 0x01, 0x05, 0x09, 0xfa,
					 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb8,
					 0x07, 0x87, 0x26, 0x18, 0x00, 0x32);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb9,
					 0x07, 0x75, 0x61, 0x20, 0x16, 0x87);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x07, 0x70, 0x81, 0x20, 0x45, 0xb4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbb, 0x01, 0x1e, 0x14);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbc, 0x01, 0x50, 0x32);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x00, 0xb4, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca,
					 0x00, 0x80, 0x80, 0x80, 0x80, 0x80,
					 0x80, 0x80, 0x08, 0x20, 0x80, 0x80,
					 0x0a, 0x4a, 0x37, 0xa0, 0x55, 0xf8,
					 0x0c, 0x0c, 0x20, 0x10, 0x3f, 0x3f,
					 0x00, 0x00, 0x10, 0x10, 0x3f, 0x3f,
					 0x3f, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x03);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x00ff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int auo_r61322_off(struct auo_r61322 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);

	return dsi_ctx.accum_err;
}

static int auo_r61322_prepare(struct drm_panel *panel)
{
	struct auo_r61322 *ctx = to_auo_r61322(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(auo_r61322_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	auo_r61322_reset(ctx);

	ret = auo_r61322_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(auo_r61322_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int auo_r61322_unprepare(struct drm_panel *panel)
{
	struct auo_r61322 *ctx = to_auo_r61322(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = auo_r61322_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(auo_r61322_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode auo_r61322_mode = {
	.clock = (1080 + 150 + 10 + 40) * (1920 + 24 + 2 + 21) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 150,
	.hsync_end = 1080 + 150 + 10,
	.htotal = 1080 + 150 + 10 + 40,
	.vdisplay = 1920,
	.vsync_start = 1920 + 24,
	.vsync_end = 1920 + 24 + 2,
	.vtotal = 1920 + 24 + 2 + 21,
	.width_mm = 80,
	.height_mm = 142,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int auo_r61322_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &auo_r61322_mode);
}

static const struct drm_panel_funcs auo_r61322_panel_funcs = {
	.prepare = auo_r61322_prepare,
	.unprepare = auo_r61322_unprepare,
	.get_modes = auo_r61322_get_modes,
};

static int auo_r61322_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct auo_r61322 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(auo_r61322_supplies),
					    auo_r61322_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &auo_r61322_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void auo_r61322_remove(struct mipi_dsi_device *dsi)
{
	struct auo_r61322 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id auo_r61322_of_match[] = {
	{ .compatible = "xiaomi,auo-r61322" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, auo_r61322_of_match);

static struct mipi_dsi_driver auo_r61322_driver = {
	.probe = auo_r61322_probe,
	.remove = auo_r61322_remove,
	.driver = {
		.name = "panel-auo-r61322",
		.of_match_table = auo_r61322_of_match,
	},
};
module_mipi_dsi_driver(auo_r61322_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for auo r61322 1080p video mode dsi panel");
MODULE_LICENSE("GPL");
