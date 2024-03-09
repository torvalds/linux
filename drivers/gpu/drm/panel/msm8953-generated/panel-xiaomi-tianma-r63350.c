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

struct tianma_r63350 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data tianma_r63350_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct tianma_r63350 *to_tianma_r63350(struct drm_panel *panel)
{
	return container_of(panel, struct tianma_r63350, panel);
}

static void tianma_r63350_reset(struct tianma_r63350 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int tianma_r63350_on(struct tianma_r63350 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2,
					 0x31, 0xf7, 0x80, 0x17, 0x18, 0x00,
					 0x00, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3,
					 0x1b, 0x33, 0x99, 0xbb, 0xb3, 0x33,
					 0x33, 0x33, 0x11, 0x00, 0x01, 0x00,
					 0x00, 0xd8, 0xa0, 0x05, 0x3f, 0x3f,
					 0x33, 0x33, 0x72, 0x12, 0x8a, 0x57,
					 0x3d, 0xbc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7,
					 0x00, 0x12, 0x1a, 0x25, 0x33, 0x42,
					 0x4c, 0x5c, 0x42, 0x4a, 0x55, 0x5f,
					 0x69, 0x6f, 0x75, 0x00, 0x12, 0x1a,
					 0x25, 0x33, 0x42, 0x4c, 0x5c, 0x42,
					 0x4a, 0x55, 0x5f, 0x69, 0x6f, 0x75);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8,
					 0x01, 0x00, 0xfe, 0x00, 0xfe, 0xc8,
					 0x00, 0x00, 0x02, 0x00, 0x00, 0xfc,
					 0x00, 0x04, 0xfe, 0x04, 0x0d, 0xed,
					 0x00);
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

static int tianma_r63350_off(struct tianma_r63350 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3,
					 0x13, 0x33, 0x99, 0xb3, 0xb3, 0x33,
					 0x33, 0x33, 0x11, 0x00, 0x01, 0x00,
					 0x00, 0xd8, 0xa0, 0x05, 0x3f, 0x3f,
					 0x33, 0x33, 0x72, 0x12, 0x8a, 0x57,
					 0x3d, 0xbc);
	mipi_dsi_msleep(&dsi_ctx, 50);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x03);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);

	return dsi_ctx.accum_err;
}

static int tianma_r63350_prepare(struct drm_panel *panel)
{
	struct tianma_r63350 *ctx = to_tianma_r63350(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(tianma_r63350_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	tianma_r63350_reset(ctx);

	ret = tianma_r63350_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(tianma_r63350_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int tianma_r63350_unprepare(struct drm_panel *panel)
{
	struct tianma_r63350 *ctx = to_tianma_r63350(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = tianma_r63350_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(tianma_r63350_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode tianma_r63350_mode = {
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

static int tianma_r63350_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &tianma_r63350_mode);
}

static const struct drm_panel_funcs tianma_r63350_panel_funcs = {
	.prepare = tianma_r63350_prepare,
	.unprepare = tianma_r63350_unprepare,
	.get_modes = tianma_r63350_get_modes,
};

static int tianma_r63350_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct tianma_r63350 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(tianma_r63350_supplies),
					    tianma_r63350_supplies,
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

	drm_panel_init(&ctx->panel, dev, &tianma_r63350_panel_funcs,
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

static void tianma_r63350_remove(struct mipi_dsi_device *dsi)
{
	struct tianma_r63350 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id tianma_r63350_of_match[] = {
	{ .compatible = "xiaomi,tianma-r63350" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tianma_r63350_of_match);

static struct mipi_dsi_driver tianma_r63350_driver = {
	.probe = tianma_r63350_probe,
	.remove = tianma_r63350_remove,
	.driver = {
		.name = "panel-tianma-r63350",
		.of_match_table = tianma_r63350_of_match,
	},
};
module_mipi_dsi_driver(tianma_r63350_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for tianma r63350 1080p video mode dsi panel");
MODULE_LICENSE("GPL");
