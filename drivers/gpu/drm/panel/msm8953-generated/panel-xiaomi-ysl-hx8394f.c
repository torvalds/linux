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

struct truly720 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data truly720_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct truly720 *to_truly720(struct drm_panel *panel)
{
	return container_of(panel, struct truly720, panel);
}

static void truly720_reset(struct truly720 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int truly720_on(struct truly720 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0xff, 0x83, 0x94);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba,
				     0x63, 0x03, 0x68, 0x6b, 0xb2, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1,
				     0x48, 0x07, 0x67, 0x09, 0x31, 0x54, 0x71,
				     0x31, 0x50, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2,
				     0x00, 0x80, 0x78, 0x06, 0x08, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4,
				     0x71, 0x72, 0x29, 0x72, 0x29, 0x72, 0x01,
				     0x0c, 0x7c, 0x33, 0x00, 0x3f, 0x71, 0x72,
				     0x29, 0x72, 0x29, 0x72, 0x01, 0x0c, 0x7e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0,
				     0x1a, 0x1a, 0x20, 0x25, 0x25, 0x28, 0x2b,
				     0x28, 0x52, 0x63, 0x72, 0x70, 0x77, 0x8a,
				     0x90, 0x93, 0xa1, 0xa5, 0xa0, 0xb0, 0xc1,
				     0x61, 0x5f, 0x64, 0x69, 0x6c, 0x72, 0x7a,
				     0x7c, 0x1a, 0x1a, 0x20, 0x24, 0x25, 0x28,
				     0x2a, 0x28, 0x53, 0x63, 0x71, 0x6f, 0x77,
				     0x8a, 0x8f, 0x94, 0xa0, 0xa4, 0xa0, 0xaf,
				     0xc1, 0x60, 0x5f, 0x63, 0x68, 0x6c, 0x73,
				     0x7a, 0x7c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd3,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f,
				     0x0f, 0x32, 0x10, 0x01, 0x00, 0x01, 0x52,
				     0x15, 0xa8, 0x05, 0xa8, 0x32, 0x10, 0x00,
				     0x00, 0x00, 0x67, 0x43, 0x05, 0x05, 0x37,
				     0x0e, 0x0e, 0x27, 0x06, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd5,
				     0x19, 0x19, 0x18, 0x18, 0x24, 0x25, 0x06,
				     0x07, 0x04, 0x05, 0x02, 0x03, 0x00, 0x01,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
				     0x18, 0x18, 0x20, 0x21, 0x18, 0x18, 0x18,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
				     0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6,
				     0x18, 0x18, 0x19, 0x19, 0x21, 0x20, 0x01,
				     0x00, 0x03, 0x02, 0x05, 0x04, 0x07, 0x06,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
				     0x18, 0x18, 0x25, 0x24, 0x18, 0x18, 0x18,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
				     0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0x25, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0x1f, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd4, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd3, 0x01, 0x04, 0x00, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8,
				     0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff,
				     0xff, 0xff, 0xff, 0xff, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0xed);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x11);
	mipi_dsi_usleep_range(&dsi_ctx, 5000, 6000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca,
				     0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
				     0x20, 0x20);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x00ff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc9,
				     0x13, 0x00, 0x0a, 0x10, 0xb2, 0x20, 0x00,
				     0x91, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x2c);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int truly720_off(struct truly720 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 36);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int truly720_prepare(struct drm_panel *panel)
{
	struct truly720 *ctx = to_truly720(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(truly720_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	truly720_reset(ctx);

	ret = truly720_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(truly720_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int truly720_unprepare(struct drm_panel *panel)
{
	struct truly720 *ctx = to_truly720(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = truly720_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(truly720_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode truly720_mode = {
	.clock = (720 + 96 + 20 + 80) * (1440 + 10 + 4 + 4) * 60 / 1000,
	.hdisplay = 720,
	.hsync_start = 720 + 96,
	.hsync_end = 720 + 96 + 20,
	.htotal = 720 + 96 + 20 + 80,
	.vdisplay = 1440,
	.vsync_start = 1440 + 10,
	.vsync_end = 1440 + 10 + 4,
	.vtotal = 1440 + 10 + 4 + 4,
	.width_mm = 68,
	.height_mm = 136,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int truly720_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &truly720_mode);
}

static const struct drm_panel_funcs truly720_panel_funcs = {
	.prepare = truly720_prepare,
	.unprepare = truly720_unprepare,
	.get_modes = truly720_get_modes,
};

static int truly720_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct truly720 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(truly720_supplies),
					    truly720_supplies,
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
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &truly720_panel_funcs,
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

static void truly720_remove(struct mipi_dsi_device *dsi)
{
	struct truly720 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id truly720_of_match[] = {
	{ .compatible = "xiaomi,ysl-hx8394f" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, truly720_of_match);

static struct mipi_dsi_driver truly720_driver = {
	.probe = truly720_probe,
	.remove = truly720_remove,
	.driver = {
		.name = "panel-truly720",
		.of_match_table = truly720_of_match,
	},
};
module_mipi_dsi_driver(truly720_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for truly hx8394f hd720 video mode dsi panel");
MODULE_LICENSE("GPL");
