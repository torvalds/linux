// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree.
 * Copyright (c) 2023 Linaro Limited
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct rm692e5_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
};

static inline struct rm692e5_panel *to_rm692e5_panel(struct drm_panel *panel)
{
	return container_of(panel, struct rm692e5_panel, panel);
}

static void rm692e5_reset(struct rm692e5_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static void rm692e5_on(struct mipi_dsi_multi_context *dsi_ctx)
{
	dsi_ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfe, 0x41);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xd6, 0x00);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfe, 0x16);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x8a, 0x87);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfe, 0x71);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x82, 0x01);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xc6, 0x00);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xc7, 0x2c);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xc8, 0x64);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xc9, 0x3c);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xca, 0x80);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xcb, 0x02);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xcc, 0x02);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfe, 0x38);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x18, 0x13);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfe, 0xf4);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x00, 0xff);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x01, 0xff);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x02, 0xcf);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x03, 0xbc);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x04, 0xb9);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x05, 0x99);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x06, 0x02);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x07, 0x0a);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x08, 0xe0);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x09, 0x4c);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x0a, 0xeb);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x0b, 0xe8);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x0c, 0x32);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x0d, 0x07);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfe, 0xf4);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x0d, 0xc0);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x0e, 0xff);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x0f, 0xff);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x10, 0x33);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x11, 0x6f);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x12, 0x6e);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x13, 0xa6);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x14, 0x80);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x15, 0x02);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x16, 0x38);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x17, 0xd3);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x18, 0x3a);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x19, 0xba);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x1a, 0xcc);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x1b, 0x01);

	mipi_dsi_dcs_nop_multi(dsi_ctx);

	mipi_dsi_msleep(dsi_ctx, 32);

	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfe, 0x38);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x18, 0x13);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfe, 0xd1);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xd3, 0x00);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xd0, 0x00);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xd2, 0x00);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xd4, 0x00);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xb4, 0x01);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfe, 0xf9);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x00, 0xaf);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x1d, 0x37);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x44, 0x0a, 0x7b);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfe, 0x00);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfa, 0x01);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xc2, 0x08);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x35, 0x00);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x51, 0x05, 0x42);

	mipi_dsi_dcs_exit_sleep_mode_multi(dsi_ctx);
	mipi_dsi_msleep(dsi_ctx, 100);
	mipi_dsi_dcs_set_display_on_multi(dsi_ctx);
}

static int rm692e5_disable(struct drm_panel *panel)
{
	struct rm692e5_panel *ctx = to_rm692e5_panel(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfe, 0x00);

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 100);

	return dsi_ctx.accum_err;
}

static int rm692e5_prepare(struct drm_panel *panel)
{
	struct rm692e5_panel *ctx = to_rm692e5_panel(panel);
	struct drm_dsc_picture_parameter_set pps;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	dsi_ctx.accum_err = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (dsi_ctx.accum_err)
		return dsi_ctx.accum_err;

	rm692e5_reset(ctx);

	rm692e5_on(&dsi_ctx);

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);

	mipi_dsi_picture_parameter_set_multi(&dsi_ctx, &pps);
	mipi_dsi_compression_mode_ext_multi(&dsi_ctx, true, MIPI_DSI_COMPRESSION_DSC, 0);
	mipi_dsi_msleep(&dsi_ctx, 28);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfe, 0x40);

	/* 0x05 -> 90Hz, 0x00 -> 60Hz */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x05);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfe, 0x00);

	if (dsi_ctx.accum_err) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	}

	return dsi_ctx.accum_err;
}

static int rm692e5_unprepare(struct drm_panel *panel)
{
	struct rm692e5_panel *ctx = to_rm692e5_panel(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode rm692e5_mode = {
	.clock = (1224 + 32 + 8 + 8) * (2700 + 8 + 2 + 8) * 90 / 1000,
	.hdisplay = 1224,
	.hsync_start = 1224 + 32,
	.hsync_end = 1224 + 32 + 8,
	.htotal = 1224 + 32 + 8 + 8,
	.vdisplay = 2700,
	.vsync_start = 2700 + 8,
	.vsync_end = 2700 + 8 + 2,
	.vtotal = 2700 + 8 + 2 + 8,
	.width_mm = 68,
	.height_mm = 150,
};

static int rm692e5_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &rm692e5_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs rm692e5_panel_funcs = {
	.prepare = rm692e5_prepare,
	.unprepare = rm692e5_unprepare,
	.disable = rm692e5_disable,
	.get_modes = rm692e5_get_modes,
};

static int rm692e5_bl_update_status(struct backlight_device *bl)
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

static int rm692e5_bl_get_brightness(struct backlight_device *bl)
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

static const struct backlight_ops rm692e5_bl_ops = {
	.update_status = rm692e5_bl_update_status,
	.get_brightness = rm692e5_bl_get_brightness,
};

static struct backlight_device *
rm692e5_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 4095,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &rm692e5_bl_ops, &props);
}

static int rm692e5_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct rm692e5_panel *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "dvdd";
	ctx->supplies[2].supply = "vci";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &rm692e5_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = rm692e5_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	/* TODO: Pass slice_per_pkt = 2 */
	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 1;
	ctx->dsc.slice_height = 60;
	ctx->dsc.slice_width = 1224;

	ctx->dsc.slice_count = 1224 / ctx->dsc.slice_width;
	ctx->dsc.bits_per_component = 8;
	ctx->dsc.bits_per_pixel = 8 << 4; /* 4 fractional bits */
	ctx->dsc.block_pred_enable = true;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void rm692e5_remove(struct mipi_dsi_device *dsi)
{
	struct rm692e5_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id rm692e5_of_match[] = {
	{ .compatible = "fairphone,fp5-rm692e5-boe" },
	{ }
};
MODULE_DEVICE_TABLE(of, rm692e5_of_match);

static struct mipi_dsi_driver rm692e5_driver = {
	.probe = rm692e5_probe,
	.remove = rm692e5_remove,
	.driver = {
		.name = "panel-rm692e5-boe-amoled",
		.of_match_table = rm692e5_of_match,
	},
};
module_mipi_dsi_driver(rm692e5_driver);

MODULE_DESCRIPTION("DRM driver for rm692e5-equipped DSI panels");
MODULE_LICENSE("GPL");
