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

struct boe5p2_ili7807b {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data boe5p2_ili7807b_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline
struct boe5p2_ili7807b *to_boe5p2_ili7807b(struct drm_panel *panel)
{
	return container_of(panel, struct boe5p2_ili7807b, panel);
}

static void boe5p2_ili7807b_reset(struct boe5p2_ili7807b *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
}

static int boe5p2_ili7807b_on(struct boe5p2_ili7807b *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0xee);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xfb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0x54);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x3e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x3d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x3c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x39);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0xfe);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1d, 0x3d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0xfc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0xb4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0xee);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0xfe);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0x3e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_GAMMA_CURVE, 0xfd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x04, 0x01);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS,
				     0x00, 0xa4);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int boe5p2_ili7807b_off(struct boe5p2_ili7807b *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int boe5p2_ili7807b_prepare(struct drm_panel *panel)
{
	struct boe5p2_ili7807b *ctx = to_boe5p2_ili7807b(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(boe5p2_ili7807b_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	boe5p2_ili7807b_reset(ctx);

	ret = boe5p2_ili7807b_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(boe5p2_ili7807b_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int boe5p2_ili7807b_unprepare(struct drm_panel *panel)
{
	struct boe5p2_ili7807b *ctx = to_boe5p2_ili7807b(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = boe5p2_ili7807b_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(boe5p2_ili7807b_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode boe5p2_ili7807b_mode = {
	.clock = (1080 + 120 + 24 + 46) * (1920 + 16 + 12 + 16) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 120,
	.hsync_end = 1080 + 120 + 24,
	.htotal = 1080 + 120 + 24 + 46,
	.vdisplay = 1920,
	.vsync_start = 1920 + 16,
	.vsync_end = 1920 + 16 + 12,
	.vtotal = 1920 + 16 + 12 + 16,
	.width_mm = 65,
	.height_mm = 116,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int boe5p2_ili7807b_get_modes(struct drm_panel *panel,
				     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &boe5p2_ili7807b_mode);
}

static const struct drm_panel_funcs boe5p2_ili7807b_panel_funcs = {
	.prepare = boe5p2_ili7807b_prepare,
	.unprepare = boe5p2_ili7807b_unprepare,
	.get_modes = boe5p2_ili7807b_get_modes,
};

static int boe5p2_ili7807b_bl_update_status(struct backlight_device *bl)
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

// TODO: Check if /sys/class/backlight/.../actual_brightness actually returns
// correct values. If not, remove this function.
static int boe5p2_ili7807b_bl_get_brightness(struct backlight_device *bl)
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

static const struct backlight_ops boe5p2_ili7807b_bl_ops = {
	.update_status = boe5p2_ili7807b_bl_update_status,
	.get_brightness = boe5p2_ili7807b_bl_get_brightness,
};

static struct backlight_device *
boe5p2_ili7807b_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1023,
		.max_brightness = 1023,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &boe5p2_ili7807b_bl_ops, &props);
}

static int boe5p2_ili7807b_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct boe5p2_ili7807b *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(boe5p2_ili7807b_supplies),
					    boe5p2_ili7807b_supplies,
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

	drm_panel_init(&ctx->panel, dev, &boe5p2_ili7807b_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = boe5p2_ili7807b_create_backlight(dsi);
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

static void boe5p2_ili7807b_remove(struct mipi_dsi_device *dsi)
{
	struct boe5p2_ili7807b *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boe5p2_ili7807b_of_match[] = {
	{ .compatible = "asus,ze520kl-ili7807b-boe" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boe5p2_ili7807b_of_match);

static struct mipi_dsi_driver boe5p2_ili7807b_driver = {
	.probe = boe5p2_ili7807b_probe,
	.remove = boe5p2_ili7807b_remove,
	.driver = {
		.name = "panel-boe5p2-ili7807b",
		.of_match_table = boe5p2_ili7807b_of_match,
	},
};
module_mipi_dsi_driver(boe5p2_ili7807b_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for boe5p2 ili7807b 1080p video mode dsi panel");
MODULE_LICENSE("GPL");
