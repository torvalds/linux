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

struct ft8716 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data ft8716_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct ft8716 *to_ft8716(struct drm_panel *panel)
{
	return container_of(panel, struct ft8716, panel);
}

static void ft8716_reset(struct ft8716 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int ft8716_on(struct ft8716 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x87, 0x16, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x87, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8,
				     0x80, 0x81, 0x80, 0x00, 0x00, 0x00, 0x01,
				     0x01, 0x01, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8,
				     0x00, 0x01, 0x02, 0x01, 0x01, 0x02, 0x01,
				     0x02, 0x02, 0x02, 0x02, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8,
				     0x00, 0x01, 0x03, 0x06, 0x08, 0x0b, 0x0e,
				     0x12, 0x15, 0x17, 0x1a, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8,
				     0x80, 0x80, 0x80, 0x82, 0x83, 0x85, 0x88,
				     0x89, 0x8b, 0x8e, 0x8f, 0x8e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8,
				     0x82, 0x85, 0x85, 0x86, 0x89, 0x8c, 0x90,
				     0x93, 0x97, 0x9b, 0x9e, 0x9c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8,
				     0x02, 0x04, 0x05, 0x06, 0x09, 0x0c, 0x0e,
				     0x12, 0x14, 0x17, 0x19, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0xff, 0xf4, 0xf7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd7, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6,
				     0x0a, 0x08, 0x0a, 0x14, 0x1d, 0x16, 0x16,
				     0x17, 0x0e, 0x12, 0x13, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6,
				     0xae, 0xae, 0xc8, 0xa9, 0xc9, 0xbf, 0x9a,
				     0x9a, 0xaf, 0xab, 0x92, 0x99);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6,
				     0x8d, 0x8d, 0xb0, 0x9c, 0xb0, 0xab, 0x77,
				     0x77, 0x96, 0x9c, 0x8c, 0x91);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6,
				     0xaa, 0xaa, 0x98, 0x8e, 0x99, 0x73, 0x6f,
				     0x74, 0x8a, 0x92, 0x86, 0x8a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca,
				     0x7f, 0xff, 0xb9, 0xff, 0xb9, 0xff, 0x00,
				     0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0xb0, 0xbb, 0xbb, 0x9b, 0xdc, 0xbb, 0xab,
				     0xa9, 0x88, 0x88, 0x88, 0x56, 0x55, 0x55,
				     0x55, 0x55, 0x55, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0xb0, 0xba, 0xab, 0x8c, 0xeb, 0xba, 0xaa,
				     0x9a, 0x88, 0x88, 0x88, 0x67, 0x66, 0x56,
				     0x55, 0x55, 0x55, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0xb0, 0xb9, 0xab, 0x8b, 0xdb, 0xba, 0x9a,
				     0x9a, 0x88, 0x88, 0x88, 0x67, 0x66, 0x66,
				     0x66, 0x56, 0x55, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0xa0, 0xaa, 0x9b, 0x8c, 0xd9, 0xba, 0x9a,
				     0x99, 0x88, 0x88, 0x88, 0x67, 0x66, 0x66,
				     0x66, 0x66, 0x66, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0xa0, 0xa9, 0x9b, 0x8b, 0xd9, 0xaa, 0xaa,
				     0x98, 0x88, 0x88, 0x88, 0x68, 0x66, 0x66,
				     0x66, 0x66, 0x66, 0x66);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0xa0, 0xa9, 0x8a, 0x8c, 0xc9, 0x9a, 0x9a,
				     0x99, 0x88, 0x88, 0x88, 0x78, 0x77, 0x66,
				     0x66, 0x66, 0x66, 0x66);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0xa0, 0x99, 0x8a, 0x9b, 0xc8, 0x9a, 0xa9,
				     0x89, 0x88, 0x88, 0x88, 0x78, 0x77, 0x77,
				     0x67, 0x66, 0x66, 0x66);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0x90, 0x9a, 0x89, 0x9b, 0xb8, 0xa9, 0x99,
				     0x8a, 0x88, 0x88, 0x88, 0x78, 0x77, 0x77,
				     0x77, 0x67, 0x66, 0x66);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0x90, 0x99, 0x8a, 0x8a, 0xb8, 0xa9, 0x99,
				     0x89, 0x88, 0x88, 0x88, 0x78, 0x77, 0x77,
				     0x77, 0x77, 0x77, 0x66);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0x90, 0x99, 0x89, 0x8a, 0xb8, 0x99, 0x89,
				     0x8a, 0x88, 0x88, 0x88, 0x78, 0x77, 0x77,
				     0x77, 0x77, 0x77, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0x80, 0x99, 0x89, 0x8a, 0xa8, 0x99, 0x89,
				     0x99, 0x88, 0x88, 0x88, 0x88, 0x78, 0x77,
				     0x77, 0x77, 0x77, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x1b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0x80, 0xa8, 0x98, 0x89, 0xa8, 0x98, 0x89,
				     0x99, 0x88, 0x88, 0x88, 0x88, 0x88, 0x78,
				     0x77, 0x77, 0x77, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0x80, 0x98, 0x89, 0x89, 0x98, 0x89, 0x89,
				     0x89, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
				     0x88, 0x77, 0x77, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0x80, 0x98, 0x98, 0x98, 0x97, 0x98, 0x88,
				     0x89, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
				     0x88, 0x88, 0x77, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0x80, 0x88, 0x89, 0x88, 0x88, 0x89, 0x98,
				     0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
				     0x88, 0x88, 0x88, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7,
				     0x80, 0x88, 0x88, 0x98, 0x97, 0x88, 0x88,
				     0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
				     0x88, 0x88, 0x88, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xb2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca, 0x04);
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

static int ft8716_off(struct ft8716 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x87, 0x16, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x87, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x5a, 0xa5, 0x87, 0x16);

	return dsi_ctx.accum_err;
}

static int ft8716_prepare(struct drm_panel *panel)
{
	struct ft8716 *ctx = to_ft8716(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ft8716_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ft8716_reset(ctx);

	ret = ft8716_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ft8716_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int ft8716_unprepare(struct drm_panel *panel)
{
	struct ft8716 *ctx = to_ft8716(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = ft8716_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ft8716_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode ft8716_mode = {
	.clock = (1080 + 32 + 4 + 32) * (1920 + 16 + 2 + 16) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 32,
	.hsync_end = 1080 + 32 + 4,
	.htotal = 1080 + 32 + 4 + 32,
	.vdisplay = 1920,
	.vsync_start = 1920 + 16,
	.vsync_end = 1920 + 16 + 2,
	.vtotal = 1920 + 16 + 2 + 16,
	.width_mm = 69,
	.height_mm = 122,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int ft8716_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &ft8716_mode);
}

static const struct drm_panel_funcs ft8716_panel_funcs = {
	.prepare = ft8716_prepare,
	.unprepare = ft8716_unprepare,
	.get_modes = ft8716_get_modes,
};

static int ft8716_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ft8716 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(ft8716_supplies),
					    ft8716_supplies,
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

	drm_panel_init(&ctx->panel, dev, &ft8716_panel_funcs,
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

static void ft8716_remove(struct mipi_dsi_device *dsi)
{
	struct ft8716 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ft8716_of_match[] = {
	{ .compatible = "mdss,ft8716-fhd" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ft8716_of_match);

static struct mipi_dsi_driver ft8716_driver = {
	.probe = ft8716_probe,
	.remove = ft8716_remove,
	.driver = {
		.name = "panel-ft8716",
		.of_match_table = ft8716_of_match,
	},
};
module_mipi_dsi_driver(ft8716_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for ft8716 fhd video mode dsi panel");
MODULE_LICENSE("GPL");
