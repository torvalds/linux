// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Kamil Gołda <kamil.golda@protonmail.com>
// Copyright (c) Yedaya Katsman <yedaya.ka@gmail.com>
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) The Linux Foundation. All rights reserved.

#include <linux/backlight.h>
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

struct s6e8fc0_ctx {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data s6e8fc0_supplies[] = {
	{ .supply = "vdd" },
	{ .supply = "vci" },
};

static inline
struct s6e8fc0_ctx *to_s6e8fc0_ctx(struct drm_panel *panel)
{
	return container_of_const(panel, struct s6e8fc0_ctx, panel);
}

static void s6e8fc0_m1906f9_reset(struct s6e8fc0_ctx *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(12000, 13000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

#define s6e8fc0_test_key_on_lvl2(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0x5a, 0x5a)
#define s6e8fc0_test_key_off_lvl2(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0xa5, 0xa5)
#define s6e8fc0_test_key_on_lvl3(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfc, 0x5a, 0x5a)
#define s6e8fc0_test_key_off_lvl3(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfc, 0xa5, 0xa5)

static int s6e8fc0_m1906f9_on(struct s6e8fc0_ctx *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	s6e8fc0_test_key_on_lvl3(&dsi_ctx);

	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x20);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x04, 0xed);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xed,
				     0xe4, 0x08, 0x96, 0xa4, 0x2a, 0x72, 0xe2,
				     0xca, 0x00);
	s6e8fc0_test_key_off_lvl3(&dsi_ctx);
	s6e8fc0_test_key_on_lvl2(&dsi_ctx);
	s6e8fc0_test_key_on_lvl3(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1, 0x93);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x05, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf4, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xed, 0x01, 0x81, 0x04);
	s6e8fc0_test_key_off_lvl2(&dsi_ctx);
	s6e8fc0_test_key_off_lvl3(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int s6e8fc0_m1906f9_off(struct s6e8fc0_ctx *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int s6e8fc0_m1906f9_prepare(struct drm_panel *panel)
{
	struct s6e8fc0_ctx *ctx = to_s6e8fc0_ctx(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(s6e8fc0_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	s6e8fc0_m1906f9_reset(ctx);

	ret = s6e8fc0_m1906f9_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(s6e8fc0_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int s6e8fc0_m1906f9_unprepare(struct drm_panel *panel)
{
	struct s6e8fc0_ctx *ctx = to_s6e8fc0_ctx(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = s6e8fc0_m1906f9_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(s6e8fc0_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode s6e8fc0_m1906f9_samsungp_mode = {
	.clock = (720 + 350 + 40 + 294) * (1560 + 17 + 2 + 5) * 60 / 1000,
	.hdisplay = 720,
	.hsync_start = 720 + 350,
	.hsync_end = 720 + 350 + 40,
	.htotal = 720 + 350 + 40 + 294,
	.vdisplay = 1560,
	.vsync_start = 1560 + 17,
	.vsync_end = 1560 + 17 + 2,
	.vtotal = 1560 + 17 + 2 + 5,
	.width_mm = 65,
	.height_mm = 140,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int s6e8fc0_m1906f9_get_modes(struct drm_panel *panel,
				      struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &s6e8fc0_m1906f9_samsungp_mode);
}

static const struct drm_panel_funcs s6e8fc0_m1906f9_panel_funcs = {
	.prepare = s6e8fc0_m1906f9_prepare,
	.unprepare = s6e8fc0_m1906f9_unprepare,
	.get_modes = s6e8fc0_m1906f9_get_modes,
};

static int s6e8fc0_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int s6e8fc0_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops s6e8fc0_bl_ops = {
	.update_status = s6e8fc0_bl_update_status,
	.get_brightness = s6e8fc0_bl_get_brightness,
};

static struct backlight_device *
s6e8fc0_m1906f9_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 512,
		.max_brightness = 1023,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &s6e8fc0_bl_ops, &props);
}

static int s6e8fc0_m1906f9_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e8fc0_ctx *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct s6e8fc0_ctx, panel,
				   &s6e8fc0_m1906f9_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(s6e8fc0_supplies),
					    s6e8fc0_supplies,
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

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = s6e8fc0_m1906f9_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void s6e8fc0_remove(struct mipi_dsi_device *dsi)
{
	struct s6e8fc0_ctx *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id samsung_s6e8fc0_of_match[] = {
	{ .compatible = "samsung,s6e8fc0-m1906f9" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, samsung_s6e8fc0_of_match);

static struct mipi_dsi_driver s6e8fc0_driver = {
	.probe = s6e8fc0_m1906f9_probe,
	.remove = s6e8fc0_remove,
	.driver = {
		.name = "panel-samsung-s6e8fc0-m1906f9",
		.of_match_table = samsung_s6e8fc0_of_match,
	},
};
module_mipi_dsi_driver(s6e8fc0_driver);

MODULE_AUTHOR("Kamil Gołda <kamil.golda@protonmail.com>");
MODULE_DESCRIPTION("DRM driver for Samsung s6e8fc0 DSI controller");
MODULE_LICENSE("GPL");
