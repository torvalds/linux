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

struct otm1911 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data otm1911_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct otm1911 *to_otm1911(struct drm_panel *panel)
{
	return container_of(panel, struct otm1911, panel);
}

static void otm1911_reset(struct otm1911 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int otm1911_on(struct otm1911 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x19, 0x11, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x19, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcb,
				     0xfe, 0xf4, 0xf4, 0xf4, 0x00, 0x00, 0x00,
				     0x00, 0xf4, 0x07, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1,
				     0xd6, 0xde, 0xfa, 0x1e, 0x40, 0x37, 0x55,
				     0x87, 0xb1, 0x55, 0x95, 0xc7, 0xf0, 0x15,
				     0x95, 0x41, 0x68, 0xdd, 0x06, 0x9a, 0x2b,
				     0x52, 0x72, 0x99, 0xaa, 0xca, 0xd3, 0x07,
				     0x42, 0xfa, 0x66, 0x8a, 0xc5, 0xef, 0xff,
				     0xff, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2,
				     0xd6, 0xde, 0xfc, 0x1e, 0x40, 0x34, 0x4d,
				     0x7d, 0xa9, 0x55, 0x8c, 0xb4, 0xe1, 0x05,
				     0x95, 0x31, 0x59, 0xcc, 0xf4, 0x5a, 0x17,
				     0x40, 0x5f, 0x80, 0xaa, 0xad, 0xb8, 0xe7,
				     0x27, 0xea, 0x4b, 0x6d, 0xaa, 0xe5, 0xff,
				     0xff, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe3,
				     0x00, 0x26, 0x72, 0xb3, 0x00, 0xda, 0x02,
				     0x46, 0x7e, 0x54, 0x6f, 0xa2, 0xd4, 0xfe,
				     0x55, 0x2f, 0x5a, 0xd0, 0xfb, 0x5a, 0x21,
				     0x4a, 0x69, 0x8f, 0xaa, 0xc0, 0xc8, 0xf8,
				     0x36, 0xea, 0x5d, 0x80, 0xc0, 0xee, 0xff,
				     0xff, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4,
				     0xa5, 0xb2, 0xd4, 0xf6, 0x00, 0x0e, 0x2f,
				     0x68, 0x91, 0x55, 0x78, 0xaa, 0xd4, 0xfb,
				     0x55, 0x2a, 0x52, 0xc7, 0xf0, 0x5a, 0x14,
				     0x3b, 0x5a, 0x7e, 0xaa, 0xa9, 0xaf, 0xdb,
				     0x1a, 0xea, 0x3f, 0x64, 0xa3, 0xe3, 0xff,
				     0xff, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe5,
				     0xba, 0xc4, 0xe3, 0x02, 0x40, 0x1b, 0x3d,
				     0x73, 0x9b, 0x55, 0x83, 0xb6, 0xe2, 0x08,
				     0x95, 0x38, 0x60, 0xd7, 0x00, 0x9a, 0x26,
				     0x4d, 0x6d, 0x91, 0xaa, 0xc9, 0xd7, 0x10,
				     0x4a, 0xfa, 0x6c, 0x8f, 0xc7, 0xf0, 0xff,
				     0xff, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe6,
				     0x52, 0x43, 0x57, 0x72, 0x55, 0x81, 0x8f,
				     0xae, 0xd0, 0x55, 0xb0, 0xd3, 0xf8, 0x1a,
				     0x95, 0x40, 0x66, 0xd8, 0xff, 0x5a, 0x21,
				     0x4b, 0x69, 0x90, 0xaa, 0xb7, 0xc3, 0xfa,
				     0x39, 0xea, 0x58, 0x7a, 0xb2, 0xe7, 0xff,
				     0xff, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8,
				     0x03, 0xfc, 0x04, 0x00, 0x03, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8,
				     0x03, 0xc6, 0x03, 0xe3, 0x04, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8,
				     0x04, 0x00, 0x03, 0xe4, 0x03, 0xbf);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6,
				     0x0f, 0x10, 0x0f, 0x14, 0x18, 0x12, 0x10,
				     0x0d, 0x0e, 0x12, 0x12, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6,
				     0x97, 0x8d, 0xce, 0x91, 0xdf, 0xe5, 0xb3,
				     0x99, 0x8f, 0x8a, 0x92, 0x9a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6,
				     0x90, 0x89, 0x9a, 0x8d, 0x80, 0x85, 0xa3,
				     0x91, 0x8a, 0x87, 0x8c, 0x91);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6,
				     0x89, 0x98, 0x9a, 0x85, 0x85, 0x8e, 0x91,
				     0x88, 0x85, 0x83, 0x86, 0x89);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca,
				     0xab, 0xa5, 0xa1, 0x9c, 0x98, 0x94, 0x90,
				     0x8d, 0x8a, 0x87, 0x84, 0x82);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca,
				     0xf8, 0xff, 0x00, 0xfc, 0xff, 0xcc, 0xfa,
				     0xff, 0x66);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca, 0x03);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x00ff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x2c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int otm1911_off(struct otm1911 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x19, 0x11, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x19, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int otm1911_prepare(struct drm_panel *panel)
{
	struct otm1911 *ctx = to_otm1911(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(otm1911_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	otm1911_reset(ctx);

	ret = otm1911_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(otm1911_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int otm1911_unprepare(struct drm_panel *panel)
{
	struct otm1911 *ctx = to_otm1911(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = otm1911_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(otm1911_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode otm1911_mode = {
	.clock = (1080 + 24 + 20 + 24) * (1920 + 14 + 2 + 6) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 24,
	.hsync_end = 1080 + 24 + 20,
	.htotal = 1080 + 24 + 20 + 24,
	.vdisplay = 1920,
	.vsync_start = 1920 + 14,
	.vsync_end = 1920 + 14 + 2,
	.vtotal = 1920 + 14 + 2 + 6,
	.width_mm = 69,
	.height_mm = 122,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int otm1911_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &otm1911_mode);
}

static const struct drm_panel_funcs otm1911_panel_funcs = {
	.prepare = otm1911_prepare,
	.unprepare = otm1911_unprepare,
	.get_modes = otm1911_get_modes,
};

static int otm1911_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct otm1911 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(otm1911_supplies),
					    otm1911_supplies,
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
			  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &otm1911_panel_funcs,
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

static void otm1911_remove(struct mipi_dsi_device *dsi)
{
	struct otm1911 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id otm1911_of_match[] = {
	{ .compatible = "xiaomi,otm1911" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, otm1911_of_match);

static struct mipi_dsi_driver otm1911_driver = {
	.probe = otm1911_probe,
	.remove = otm1911_remove,
	.driver = {
		.name = "panel-otm1911",
		.of_match_table = otm1911_of_match,
	},
};
module_mipi_dsi_driver(otm1911_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for otm1911 fhd video mode dsi panel");
MODULE_LICENSE("GPL");
