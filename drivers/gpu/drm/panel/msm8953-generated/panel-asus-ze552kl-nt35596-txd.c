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

struct txd5p5_nt35596 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data txd5p5_nt35596_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct txd5p5_nt35596 *to_txd5p5_nt35596(struct drm_panel *panel)
{
	return container_of(panel, struct txd5p5_nt35596, panel);
}

static void txd5p5_nt35596_reset(struct txd5p5_nt35596 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(11000, 12000);
}

static int txd5p5_nt35596_on(struct txd5p5_nt35596 *ctx)
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
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x04, 0x01);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS,
				     0x00, 0xa4);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	return dsi_ctx.accum_err;
}

static int txd5p5_nt35596_off(struct txd5p5_nt35596 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int txd5p5_nt35596_prepare(struct drm_panel *panel)
{
	struct txd5p5_nt35596 *ctx = to_txd5p5_nt35596(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(txd5p5_nt35596_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	txd5p5_nt35596_reset(ctx);

	ret = txd5p5_nt35596_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(txd5p5_nt35596_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int txd5p5_nt35596_unprepare(struct drm_panel *panel)
{
	struct txd5p5_nt35596 *ctx = to_txd5p5_nt35596(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = txd5p5_nt35596_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(txd5p5_nt35596_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode txd5p5_nt35596_mode = {
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

static int txd5p5_nt35596_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &txd5p5_nt35596_mode);
}

static const struct drm_panel_funcs txd5p5_nt35596_panel_funcs = {
	.prepare = txd5p5_nt35596_prepare,
	.unprepare = txd5p5_nt35596_unprepare,
	.get_modes = txd5p5_nt35596_get_modes,
};

static int txd5p5_nt35596_bl_update_status(struct backlight_device *bl)
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
static int txd5p5_nt35596_bl_get_brightness(struct backlight_device *bl)
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

static const struct backlight_ops txd5p5_nt35596_bl_ops = {
	.update_status = txd5p5_nt35596_bl_update_status,
	.get_brightness = txd5p5_nt35596_bl_get_brightness,
};

static struct backlight_device *
txd5p5_nt35596_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1023,
		.max_brightness = 1023,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &txd5p5_nt35596_bl_ops, &props);
}

static int txd5p5_nt35596_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct txd5p5_nt35596 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(txd5p5_nt35596_supplies),
					    txd5p5_nt35596_supplies,
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

	drm_panel_init(&ctx->panel, dev, &txd5p5_nt35596_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = txd5p5_nt35596_create_backlight(dsi);
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

static void txd5p5_nt35596_remove(struct mipi_dsi_device *dsi)
{
	struct txd5p5_nt35596 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id txd5p5_nt35596_of_match[] = {
	{ .compatible = "asus,ze552kl-nt35596-txd" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, txd5p5_nt35596_of_match);

static struct mipi_dsi_driver txd5p5_nt35596_driver = {
	.probe = txd5p5_nt35596_probe,
	.remove = txd5p5_nt35596_remove,
	.driver = {
		.name = "panel-txd5p5-nt35596",
		.of_match_table = txd5p5_nt35596_of_match,
	},
};
module_mipi_dsi_driver(txd5p5_nt35596_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for txd5p5 NT35596 1080p video mode dsi panel");
MODULE_LICENSE("GPL");
