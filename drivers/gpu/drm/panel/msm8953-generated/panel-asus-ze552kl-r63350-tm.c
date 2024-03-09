// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

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

struct tm5p5_r63350 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data tm5p5_r63350_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct tm5p5_r63350 *to_tm5p5_r63350(struct drm_panel *panel)
{
	return container_of(panel, struct tm5p5_r63350, panel);
}

static void tm5p5_r63350_reset(struct tm5p5_r63350 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
}

static int tm5p5_r63350_on(struct tm5p5_r63350 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2,
					 0x31, 0xf7, 0x80, 0x0e, 0x08, 0x00,
					 0x00, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb8,
					 0x57, 0x51, 0x12, 0x00, 0x0d, 0x50,
					 0x50);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb9,
					 0x8d, 0x51, 0x20, 0x00, 0x2a, 0xa0,
					 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0xb5, 0x51, 0x2e, 0x00, 0x1f, 0xa0,
					 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x50, 0x40, 0x43, 0x49, 0x53, 0x60,
					 0x6e, 0x7d, 0x8f, 0xa0, 0xb2, 0xc5,
					 0xd7, 0xe9, 0xf5, 0xfc, 0xff, 0x02,
					 0x2c, 0x04, 0x04, 0x00, 0x04, 0x69,
					 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x24);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS,
				     0x11);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int tm5p5_r63350_off(struct tm5p5_r63350 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3,
					 0x13, 0x33, 0xbb, 0xb3, 0xb3, 0x33,
					 0x33, 0x33, 0x33, 0x00, 0x01, 0x00,
					 0x00, 0xd8, 0xa0, 0x05, 0x2f, 0x2f,
					 0x33, 0x33, 0x72, 0x12, 0x8a, 0x57,
					 0x3d, 0xbc);
	mipi_dsi_msleep(&dsi_ctx, 50);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 60);

	return dsi_ctx.accum_err;
}

static int tm5p5_r63350_prepare(struct drm_panel *panel)
{
	struct tm5p5_r63350 *ctx = to_tm5p5_r63350(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(tm5p5_r63350_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	tm5p5_r63350_reset(ctx);

	ret = tm5p5_r63350_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(tm5p5_r63350_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int tm5p5_r63350_unprepare(struct drm_panel *panel)
{
	struct tm5p5_r63350 *ctx = to_tm5p5_r63350(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = tm5p5_r63350_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(tm5p5_r63350_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode tm5p5_r63350_mode = {
	.clock = (1080 + 120 + 6 + 80) * (1920 + 8 + 6 + 8) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 120,
	.hsync_end = 1080 + 120 + 6,
	.htotal = 1080 + 120 + 6 + 80,
	.vdisplay = 1920,
	.vsync_start = 1920 + 8,
	.vsync_end = 1920 + 8 + 6,
	.vtotal = 1920 + 8 + 6 + 8,
	.width_mm = 68,
	.height_mm = 121,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int tm5p5_r63350_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &tm5p5_r63350_mode);
}

static const struct drm_panel_funcs tm5p5_r63350_panel_funcs = {
	.prepare = tm5p5_r63350_prepare,
	.unprepare = tm5p5_r63350_unprepare,
	.get_modes = tm5p5_r63350_get_modes,
};

static int tm5p5_r63350_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

// TODO: Check if /sys/class/backlight/.../actual_brightness actually returns
// correct values. If not, remove this function.
static int tm5p5_r63350_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness & 0xff;
}

static const struct backlight_ops tm5p5_r63350_bl_ops = {
	.update_status = tm5p5_r63350_bl_update_status,
	.get_brightness = tm5p5_r63350_bl_get_brightness,
};

static struct backlight_device *
tm5p5_r63350_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &tm5p5_r63350_bl_ops, &props);
}

static int tm5p5_r63350_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct tm5p5_r63350 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(tm5p5_r63350_supplies),
					    tm5p5_r63350_supplies,
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

	drm_panel_init(&ctx->panel, dev, &tm5p5_r63350_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = tm5p5_r63350_create_backlight(dsi);
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

static void tm5p5_r63350_remove(struct mipi_dsi_device *dsi)
{
	struct tm5p5_r63350 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id tm5p5_r63350_of_match[] = {
	{ .compatible = "asus,ze552kl-r63350-tm" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tm5p5_r63350_of_match);

static struct mipi_dsi_driver tm5p5_r63350_driver = {
	.probe = tm5p5_r63350_probe,
	.remove = tm5p5_r63350_remove,
	.driver = {
		.name = "panel-tm5p5-r63350",
		.of_match_table = tm5p5_r63350_of_match,
	},
};
module_mipi_dsi_driver(tm5p5_r63350_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for tm5p5 R63350 1080p video mode dsi panel");
MODULE_LICENSE("GPL");
