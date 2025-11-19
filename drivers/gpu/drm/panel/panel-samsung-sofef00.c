// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Casey Connolly <casey.connolly@linaro.org>
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/backlight.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct sofef00_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data sofef00_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vci" },
	{ .supply = "poc" },
};

static inline
struct sofef00_panel *to_sofef00_panel(struct drm_panel *panel)
{
	return container_of(panel, struct sofef00_panel, panel);
}

#define sofef00_test_key_on_lvl2(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0x5a, 0x5a)
#define sofef00_test_key_off_lvl2(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0xa5, 0xa5)

static void sofef00_panel_reset(struct sofef00_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(12000, 13000);
}

static int sofef00_panel_on(struct sofef00_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	sofef00_test_key_on_lvl2(&dsi_ctx);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	sofef00_test_key_off_lvl2(&dsi_ctx);

	sofef00_test_key_on_lvl2(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0x12);
	sofef00_test_key_off_lvl2(&dsi_ctx);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);

	return dsi_ctx.accum_err;
}

static int sofef00_enable(struct drm_panel *panel)
{
	struct sofef00_panel *ctx = to_sofef00_panel(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int sofef00_panel_off(struct sofef00_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 40);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 160);

	return dsi_ctx.accum_err;
}

static int sofef00_panel_prepare(struct drm_panel *panel)
{
	struct sofef00_panel *ctx = to_sofef00_panel(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(sofef00_supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	sofef00_panel_reset(ctx);

	ret = sofef00_panel_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(sofef00_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int sofef00_disable(struct drm_panel *panel)
{
	struct sofef00_panel *ctx = to_sofef00_panel(panel);

	sofef00_panel_off(ctx);

	return 0;
}

static int sofef00_panel_unprepare(struct drm_panel *panel)
{
	struct sofef00_panel *ctx = to_sofef00_panel(panel);

	regulator_bulk_disable(ARRAY_SIZE(sofef00_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode ams628nw01_panel_mode = {
	.clock = (1080 + 112 + 16 + 36) * (2280 + 36 + 8 + 12) * 60 / 1000,

	.hdisplay = 1080,
	.hsync_start = 1080 + 112,
	.hsync_end = 1080 + 112 + 16,
	.htotal = 1080 + 112 + 16 + 36,

	.vdisplay = 2280,
	.vsync_start = 2280 + 36,
	.vsync_end = 2280 + 36 + 8,
	.vtotal = 2280 + 36 + 8 + 12,

	.width_mm = 68,
	.height_mm = 145,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int sofef00_panel_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &ams628nw01_panel_mode);
}

static const struct drm_panel_funcs sofef00_panel_panel_funcs = {
	.prepare = sofef00_panel_prepare,
	.enable = sofef00_enable,
	.disable = sofef00_disable,
	.unprepare = sofef00_panel_unprepare,
	.get_modes = sofef00_panel_get_modes,
};

static int sofef00_panel_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	int err;
	u16 brightness = (u16)backlight_get_brightness(bl);

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	err = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (err < 0)
		return err;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct backlight_ops sofef00_panel_bl_ops = {
	.update_status = sofef00_panel_bl_update_status,
};

static struct backlight_device *
sofef00_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_PLATFORM,
		.brightness = 512,
		.max_brightness = 1023,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &sofef00_panel_bl_ops, &props);
}

static int sofef00_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct sofef00_panel *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct sofef00_panel, panel,
				   &sofef00_panel_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(sofef00_supplies),
					    sofef00_supplies,
					    &ctx->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = sofef00_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void sofef00_panel_remove(struct mipi_dsi_device *dsi)
{
	struct sofef00_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id sofef00_panel_of_match[] = {
	{ .compatible = "samsung,sofef00" }, /* legacy */
	{ .compatible = "samsung,sofef00-ams628nw01" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sofef00_panel_of_match);

static struct mipi_dsi_driver sofef00_panel_driver = {
	.probe = sofef00_panel_probe,
	.remove = sofef00_panel_remove,
	.driver = {
		.name = "panel-samsung-sofef00",
		.of_match_table = sofef00_panel_of_match,
	},
};

module_mipi_dsi_driver(sofef00_panel_driver);

MODULE_AUTHOR("Casey Connolly <casey.connolly@linaro.org>");
MODULE_DESCRIPTION("DRM driver for Samsung SOFEF00 DDIC");
MODULE_LICENSE("GPL v2");
