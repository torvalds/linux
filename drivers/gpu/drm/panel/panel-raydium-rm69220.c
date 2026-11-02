// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree.
 * Copyright (c) 2026 Alexander Koskovich <akoskovich@pm.me>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/device-id/of.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct raydium_rm69220 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data raydium_rm69220_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "dvdd" },
	{ .supply = "vci" },
};

static inline
struct raydium_rm69220 *to_raydium_rm69220(struct drm_panel *panel)
{
	return container_of(panel, struct raydium_rm69220, panel);
}

static void raydium_rm69220_reset(struct raydium_rm69220 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(32);
}

static int raydium_rm69220_on(struct raydium_rm69220 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x76);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9a, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9a, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x78);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9a, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x79);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9a, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x74);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa9, 0x68);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xab, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0xa6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbf, 0x87);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x82);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xd4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa2, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfa, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xd2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x97, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x36, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0xab);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3a, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3d, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3f, 0x58);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x40, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x42, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x43, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x44, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x45, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x46, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x47, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x49, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4a, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4b, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4d, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4e, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4f, 0x39);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x54, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x56, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0xd3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5e, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x46);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6a, 0x54);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6b, 0x62);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6c, 0x69);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6e, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x79);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x70, 0x7b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x71, 0x7d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x72, 0x7e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x73, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x74, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7a, 0xbe);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7b, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7c, 0xfc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7d, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7e, 0xfa);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7f, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0xf8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x3b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x83, 0x3b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x78);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x3b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x86, 0xb6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x87, 0x4b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x88, 0xf6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x89, 0x4c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8a, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8b, 0x4c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8c, 0x74);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8d, 0x5c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8e, 0x74);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8f, 0x8c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x93, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x95, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x96, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x87, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4b, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4d, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xa1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0x67);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0xa7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfa, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x0d, 0xbb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x00);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 80);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	return dsi_ctx.accum_err;
}

static int raydium_rm69220_off(struct raydium_rm69220 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);

	return dsi_ctx.accum_err;
}

static int raydium_rm69220_prepare(struct drm_panel *panel)
{
	struct raydium_rm69220 *ctx = to_raydium_rm69220(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(raydium_rm69220_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	raydium_rm69220_reset(ctx);

	ret = raydium_rm69220_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		goto err;
	}

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);

	ret = mipi_dsi_picture_parameter_set(ctx->dsi, &pps);
	if (ret < 0) {
		dev_err(dev, "failed to transmit PPS: %d\n", ret);
		goto err;
	}

	ret = mipi_dsi_compression_mode(ctx->dsi, true);
	if (ret < 0) {
		dev_err(dev, "failed to enable compression mode: %d\n", ret);
		goto err;
	}

	msleep(28);

	return 0;

err:
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(raydium_rm69220_supplies), ctx->supplies);
	return ret;
}

static int raydium_rm69220_unprepare(struct drm_panel *panel)
{
	struct raydium_rm69220 *ctx = to_raydium_rm69220(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = raydium_rm69220_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(raydium_rm69220_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode raydium_rm69220_mode = {
	.clock = (1080 + 36 + 4 + 36) * (2392 + 72 + 4 + 52) * 120 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 36,
	.hsync_end = 1080 + 36 + 4,
	.htotal = 1080 + 36 + 4 + 36,
	.vdisplay = 2392,
	.vsync_start = 2392 + 72,
	.vsync_end = 2392 + 72 + 4,
	.vtotal = 2392 + 72 + 4 + 52,
	.width_mm = 71,
	.height_mm = 157,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int raydium_rm69220_get_modes(struct drm_panel *panel,
					    struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &raydium_rm69220_mode);
}

static const struct drm_panel_funcs raydium_rm69220_panel_funcs = {
	.prepare = raydium_rm69220_prepare,
	.unprepare = raydium_rm69220_unprepare,
	.get_modes = raydium_rm69220_get_modes,
};

static int raydium_rm69220_bl_update_status(struct backlight_device *bl)
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

static const struct backlight_ops raydium_rm69220_bl_ops = {
	.update_status = raydium_rm69220_bl_update_status,
};

static struct backlight_device *
raydium_rm69220_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 2946,
		.max_brightness = 3442, /* 4095 is HBM max */
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &raydium_rm69220_bl_ops, &props);
}

static int raydium_rm69220_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct raydium_rm69220 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct raydium_rm69220, panel,
				   &raydium_rm69220_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(raydium_rm69220_supplies),
					    raydium_rm69220_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB101010;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM |
			  MIPI_DSI_MODE_DSC_ALL_SLICES_IN_PKT;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = raydium_rm69220_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	ret = devm_drm_panel_add(dev, &ctx->panel);
	if (ret < 0)
		return ret;

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 1;
	ctx->dsc.slice_height = 13;
	ctx->dsc.slice_width = 540;

	ctx->dsc.slice_count = 1080 / ctx->dsc.slice_width;
	ctx->dsc.bits_per_component = 10;
	ctx->dsc.bits_per_pixel = 8 << 4; /* 4 fractional bits */
	ctx->dsc.block_pred_enable = true;

	return devm_mipi_dsi_attach(dev, dsi);
}

static const struct of_device_id raydium_rm69220_of_match[] = {
	{ .compatible = "boe,bf068mwm-td0" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, raydium_rm69220_of_match);

static struct mipi_dsi_driver raydium_rm69220_driver = {
	.probe = raydium_rm69220_probe,
	.driver = {
		.name = "panel-raydium-rm69220",
		.of_match_table = raydium_rm69220_of_match,
	},
};
module_mipi_dsi_driver(raydium_rm69220_driver);

MODULE_AUTHOR("Alexander Koskovich <akoskovich@pm.me>");
MODULE_DESCRIPTION("DRM driver for RM69220-equipped DSI panels");
MODULE_LICENSE("GPL");
