// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Nia Espera <a5b6@riseup.net>
 * Copyright (c) 2025 David Heidelberg <david@ixit.cz>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/swab.h>
#include <linux/backlight.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#define MCS_ELVSS_ON            0xb1

struct samsung_s6e3fc2x01 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data s6e3fc2x01_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vci" },
	{ .supply = "poc" },
};

static inline
struct samsung_s6e3fc2x01 *to_samsung_s6e3fc2x01(struct drm_panel *panel)
{
	return container_of(panel, struct samsung_s6e3fc2x01, panel);
}

#define s6e3fc2x01_test_key_on_lvl1(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9f, 0xa5, 0xa5)
#define s6e3fc2x01_test_key_off_lvl1(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9f, 0x5a, 0x5a)
#define s6e3fc2x01_test_key_on_lvl2(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0x5a, 0x5a)
#define s6e3fc2x01_test_key_off_lvl2(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0xa5, 0xa5)
#define s6e3fc2x01_test_key_on_lvl3(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfc, 0x5a, 0x5a)
#define s6e3fc2x01_test_key_off_lvl3(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfc, 0xa5, 0xa5)

static void s6e3fc2x01_reset(struct samsung_s6e3fc2x01 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
}

static int s6e3fc2x01_on(struct samsung_s6e3fc2x01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	s6e3fc2x01_test_key_on_lvl1(&dsi_ctx);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x0a);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	s6e3fc2x01_test_key_off_lvl1(&dsi_ctx);

	s6e3fc2x01_test_key_on_lvl2(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcd, 0x01);
	s6e3fc2x01_test_key_off_lvl2(&dsi_ctx);

	mipi_dsi_usleep_range(&dsi_ctx, 15000, 16000);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x0f);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	s6e3fc2x01_test_key_on_lvl1(&dsi_ctx);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	s6e3fc2x01_test_key_off_lvl1(&dsi_ctx);

	s6e3fc2x01_test_key_on_lvl2(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xeb, 0x17,
					       0x41, 0x92,
					       0x0e, 0x10,
					       0x82, 0x5a);
	s6e3fc2x01_test_key_off_lvl2(&dsi_ctx);

	/* Column & Page Address Setting */
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0x0000, 0x0437);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x0923);

	/* Horizontal & Vertical sync Setting */
	s6e3fc2x01_test_key_on_lvl2(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe8, 0x10, 0x30);
	s6e3fc2x01_test_key_off_lvl2(&dsi_ctx);

	s6e3fc2x01_test_key_on_lvl3(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe3, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xed, 0x67);
	s6e3fc2x01_test_key_off_lvl3(&dsi_ctx);

	s6e3fc2x01_test_key_on_lvl2(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x12);
	s6e3fc2x01_test_key_off_lvl2(&dsi_ctx);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);

	s6e3fc2x01_test_key_on_lvl2(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_ELVSS_ON, 0x00, 0x01);
	s6e3fc2x01_test_key_off_lvl2(&dsi_ctx);

	s6e3fc2x01_test_key_on_lvl2(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x00, 0xc1);
	s6e3fc2x01_test_key_off_lvl2(&dsi_ctx);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x78);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x90);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	s6e3fc2x01_test_key_on_lvl2(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_ELVSS_ON, 0xc6, 0x00, 0x00,
				     0x21, 0xed, 0x02, 0x08, 0x06, 0xc1, 0x27,
				     0xfc, 0xdc, 0xe4, 0x00, 0xd9, 0xe6, 0xe7,
				     0x00, 0xfc, 0xff, 0xea);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_ELVSS_ON, 0x00, 0x00);
	s6e3fc2x01_test_key_off_lvl2(&dsi_ctx);

	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	return dsi_ctx.accum_err;
}

static int s6e3fc2x01_enable(struct drm_panel *panel)
{
	struct samsung_s6e3fc2x01 *ctx = to_samsung_s6e3fc2x01(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	s6e3fc2x01_test_key_on_lvl1(&dsi_ctx);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	s6e3fc2x01_test_key_off_lvl1(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int s6e3fc2x01_off(struct samsung_s6e3fc2x01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	s6e3fc2x01_test_key_on_lvl1(&dsi_ctx);

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);

	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	s6e3fc2x01_test_key_on_lvl2(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 16000, 17000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x82);
	s6e3fc2x01_test_key_off_lvl2(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 16000, 17000);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	s6e3fc2x01_test_key_off_lvl1(&dsi_ctx);

	s6e3fc2x01_test_key_on_lvl2(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf4, 0x01);
	s6e3fc2x01_test_key_off_lvl2(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 160);

	return dsi_ctx.accum_err;
}

static int s6e3fc2x01_disable(struct drm_panel *panel)
{
	struct samsung_s6e3fc2x01 *ctx = to_samsung_s6e3fc2x01(panel);

	s6e3fc2x01_off(ctx);

	return 0;
}

static int s6e3fc2x01_prepare(struct drm_panel *panel)
{
	struct samsung_s6e3fc2x01 *ctx = to_samsung_s6e3fc2x01(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(s6e3fc2x01_supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	s6e3fc2x01_reset(ctx);

	ret = s6e3fc2x01_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(s6e3fc2x01_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int s6e3fc2x01_unprepare(struct drm_panel *panel)
{
	struct samsung_s6e3fc2x01 *ctx = to_samsung_s6e3fc2x01(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(s6e3fc2x01_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode ams641rw_mode = {
	.clock = (1080 + 72 + 16 + 36) * (2340 + 32 + 4 + 18) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 72,
	.hsync_end = 1080 + 72 + 16,
	.htotal = 1080 + 72 + 16 + 36,
	.vdisplay = 2340,
	.vsync_start = 2340 + 32,
	.vsync_end = 2340 + 32 + 4,
	.vtotal = 2340 + 32 + 4 + 18,
	.width_mm = 68,
	.height_mm = 145,
};

static int s6e3fc2x01_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &ams641rw_mode);
}

static const struct drm_panel_funcs samsung_s6e3fc2x01_panel_funcs = {
	.prepare = s6e3fc2x01_prepare,
	.enable = s6e3fc2x01_enable,
	.disable = s6e3fc2x01_disable,
	.unprepare = s6e3fc2x01_unprepare,
	.get_modes = s6e3fc2x01_get_modes,
};

static int s6e3fc2x01_panel_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int err;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	err = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (err < 0)
		return err;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct backlight_ops s6e3fc2x01_panel_bl_ops = {
	.update_status = s6e3fc2x01_panel_bl_update_status,
};

static struct backlight_device *
s6e3fc2x01_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_PLATFORM,
		.brightness = 512,
		.max_brightness = 1023,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &s6e3fc2x01_panel_bl_ops, &props);
}

static int s6e3fc2x01_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct samsung_s6e3fc2x01 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct samsung_s6e3fc2x01, panel,
				   &samsung_s6e3fc2x01_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(s6e3fc2x01_supplies),
					    s6e3fc2x01_supplies,
					    &ctx->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");


	/* keep the display on for flicker-free experience */
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
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

	ctx->panel.backlight = s6e3fc2x01_create_backlight(dsi);
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

static void s6e3fc2x01_remove(struct mipi_dsi_device *dsi)
{
	struct samsung_s6e3fc2x01 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6e3fc2x01_of_match[] = {
	{ .compatible = "samsung,s6e3fc2x01-ams641rw", .data = &ams641rw_mode },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e3fc2x01_of_match);

static struct mipi_dsi_driver s6e3fc2x01_driver = {
	.probe = s6e3fc2x01_probe,
	.remove = s6e3fc2x01_remove,
	.driver = {
		.name = "panel-samsung-s6e3fc2x01",
		.of_match_table = s6e3fc2x01_of_match,
	},
};
module_mipi_dsi_driver(s6e3fc2x01_driver);

MODULE_AUTHOR("David Heidelberg <david@ixit.cz>");
MODULE_DESCRIPTION("DRM driver for Samsung S6E3FC2X01 DDIC");
MODULE_LICENSE("GPL");
