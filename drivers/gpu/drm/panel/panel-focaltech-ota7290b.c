// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Waveshare International Limited
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct ota7290b {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;

	struct regulator *power;
	struct gpio_desc *reset;
	struct regulator *avdd;
	struct regulator *vdd;
	struct regulator *vcc;

	enum drm_panel_orientation orientation;
};

static inline struct ota7290b *panel_to_ota(struct drm_panel *panel)
{
	return container_of(panel, struct ota7290b, panel);
}

static int ota7290b_prepare(struct drm_panel *panel)
{
	struct ota7290b *ctx = panel_to_ota(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	int ret;

	if (ctx->vcc) {
		ret = regulator_enable(ctx->vcc);
		if (ret)
			dev_err(panel->dev, "failed to enable VCC regulator: %d\n", ret);
	}

	if (ctx->reset) {
		gpiod_set_value_cansleep(ctx->reset, 0);
		msleep(60);
		gpiod_set_value_cansleep(ctx->reset, 1);
		msleep(60);
	}

	if (ctx->vdd) {
		ret = regulator_enable(ctx->vdd);
		if (ret)
			dev_err(panel->dev, "failed to enable VDD regulator: %d\n", ret);
	}

	if (ctx->reset) {
		gpiod_set_value_cansleep(ctx->reset, 0);
		msleep(60);
	}

	if (ctx->avdd) {
		ret = regulator_enable(ctx->avdd);
		if (ret)
			dev_err(panel->dev, "failed to enable AVDD regulator: %d\n", ret);
	}

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);

	if (dsi_ctx.accum_err < 0)
		dev_err(panel->dev, "failed to init panel: %d\n", dsi_ctx.accum_err);

	return dsi_ctx.accum_err;
}

static int ota7290b_unprepare(struct drm_panel *panel)
{
	struct ota7290b *ctx = panel_to_ota(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	if (ctx->avdd)
		regulator_disable(ctx->avdd);

	if (ctx->reset) {
		gpiod_set_value_cansleep(ctx->reset, 1);
		msleep(5);
	}

	if (ctx->vdd)
		regulator_disable(ctx->vdd);

	if (ctx->vcc)
		regulator_disable(ctx->vcc);

	return 0;
}

static const struct drm_display_mode waveshare_dsi_touch_8_8_a_mode = {
	.clock = 75000,

	.hdisplay = 480,
	.hsync_start = 480 + 50,
	.hsync_end = 480 + 50 + 50,
	.htotal = 480 + 50 + 50 + 50,

	.vdisplay = 1920,
	.vsync_start = 1920 + 20,
	.vsync_end = 1920 + 20 + 20,
	.vtotal = 1920 + 20 + 20 + 20,

	.width_mm = 68,
	.height_mm = 219,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int ota7290b_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &waveshare_dsi_touch_8_8_a_mode);
}

static enum drm_panel_orientation ota7290b_get_orientation(struct drm_panel *panel)
{
	struct ota7290b *ctx = panel_to_ota(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs ota7290b_funcs = {
	.prepare = ota7290b_prepare,
	.unprepare = ota7290b_unprepare,
	.get_modes = ota7290b_get_modes,
	.get_orientation = ota7290b_get_orientation,
};

static int ota7290b_probe(struct mipi_dsi_device *dsi)
{
	struct ota7290b *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(&dsi->dev, struct ota7290b, panel,
				   &ota7290b_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	ctx->reset = devm_gpiod_get_optional(&dsi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->reset),
				     "Couldn't get our reset GPIO\n");

	ctx->vcc = devm_regulator_get_optional(&dsi->dev, "vcc");
	if (IS_ERR(ctx->vcc))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->vcc),
					"Couldn't get our VCC supply\n");

	ctx->avdd = devm_regulator_get_optional(&dsi->dev, "avdd");
	if (IS_ERR(ctx->avdd))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->avdd),
					"Couldn't get our AVDD supply\n");

	ctx->vdd = devm_regulator_get_optional(&dsi->dev, "vdd");
	if (IS_ERR(ctx->vdd))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->vdd),
					"Couldn't get our VDD supply\n");

	ret = of_drm_get_panel_orientation(dsi->dev.of_node, &ctx->orientation);
	if (ret) {
		dev_err(&dsi->dev, "%pOF: failed to get orientation: %d\n",
			dsi->dev.of_node, ret);
		return ret;
	}

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	ctx->panel.prepare_prev_first = true;

	ret = devm_drm_panel_add(&dsi->dev, &ctx->panel);
	if (ret)
		return ret;

	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_HSE |
		MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	dsi->format = MIPI_DSI_FMT_RGB888,
	dsi->lanes = 2;

	return devm_mipi_dsi_attach(&dsi->dev, dsi);
}

static const struct of_device_id ota7290b_of_match[] = {
	{ .compatible = "waveshare,8.8-dsi-touch-a", },
	{}
};
MODULE_DEVICE_TABLE(of, ota7290b_of_match);

static struct mipi_dsi_driver ota7290b_driver = {
	.probe		= ota7290b_probe,
	.driver = {
		.name		= "focaltech-ota7290b",
		.of_match_table	= ota7290b_of_match,
	},
};
module_mipi_dsi_driver(ota7290b_driver);

MODULE_DESCRIPTION("Panel driver for Focaltech OTA7290B panels");
MODULE_LICENSE("GPL");
