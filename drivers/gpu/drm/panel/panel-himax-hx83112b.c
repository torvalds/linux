// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree.
 * Copyright (c) 2025 Luca Weiss <luca@lucaweiss.eu>
 */

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

/* Manufacturer specific DSI commands */
#define HX83112B_SETPOWER1	0xb1
#define HX83112B_SETDISP	0xb2
#define HX83112B_SETDRV		0xb4
#define HX83112B_SETEXTC	0xb9
#define HX83112B_SETBANK	0xbd
#define HX83112B_SETDGCLUT	0xc1
#define HX83112B_SETDISMO	0xc2
#define HX83112B_UNKNOWN1	0xc6
#define HX83112B_SETPANEL	0xcc
#define HX83112B_UNKNOWN2	0xd1
#define HX83112B_SETPOWER2	0xd2
#define HX83112B_SETGIP0	0xd3
#define HX83112B_SETGIP1	0xd5
#define HX83112B_SETGIP2	0xd6
#define HX83112B_SETGIP3	0xd8
#define HX83112B_SETIDLE	0xdd
#define HX83112B_UNKNOWN3	0xe7
#define HX83112B_UNKNOWN4	0xe9

struct hx83112b_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data hx83112b_supplies[] = {
	{ .supply = "iovcc" },
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct hx83112b_panel *to_hx83112b_panel(struct drm_panel *panel)
{
	return container_of(panel, struct hx83112b_panel, panel);
}

static void hx83112b_reset(struct hx83112b_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int hx83112b_on(struct hx83112b_panel *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETEXTC, 0x83, 0x11, 0x2b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETDISMO, 0x08, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETDISP, 0x04, 0x38, 0x08, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETPOWER1,
				     0xf8, 0x27, 0x27, 0x00, 0x00, 0x0b, 0x0e,
				     0x0b, 0x0e, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETPOWER2, 0x2d, 0x2d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETDISP,
				     0x80, 0x02, 0x18, 0x80, 0x70, 0x00, 0x08,
				     0x1c, 0x08, 0x11, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0xd1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETDISP, 0x00, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETDISP, 0xb5, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETIDLE,
				     0x00, 0x00, 0x08, 0x1c, 0x08, 0x34, 0x34,
				     0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETDRV,
				     0x65, 0x6b, 0x00, 0x00, 0xd0, 0xd4, 0x36,
				     0xcf, 0x06, 0xce, 0x00, 0xce, 0x00, 0x00,
				     0x00, 0x07, 0x00, 0x2a, 0x07, 0x01, 0x07,
				     0x00, 0x00, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0xc3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETDRV, 0x01, 0x67, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETDGCLUT, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETDGCLUT,
				     0xff, 0xfb, 0xf9, 0xf6, 0xf4, 0xf1, 0xef,
				     0xea, 0xe7, 0xe5, 0xe2, 0xdf, 0xdd, 0xda,
				     0xd8, 0xd5, 0xd2, 0xcf, 0xcc, 0xc5, 0xbe,
				     0xb7, 0xb0, 0xa8, 0xa0, 0x98, 0x8e, 0x85,
				     0x7b, 0x72, 0x69, 0x5e, 0x53, 0x48, 0x3e,
				     0x35, 0x2b, 0x22, 0x17, 0x0d, 0x09, 0x07,
				     0x05, 0x01, 0x00, 0x26, 0xf0, 0x86, 0x25,
				     0x6e, 0xb6, 0xdd, 0xf3, 0xd8, 0xcc, 0x9b,
				     0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETDGCLUT,
				     0xff, 0xfb, 0xf9, 0xf6, 0xf4, 0xf1, 0xef,
				     0xea, 0xe7, 0xe5, 0xe2, 0xdf, 0xdd, 0xda,
				     0xd8, 0xd5, 0xd2, 0xcf, 0xcc, 0xc5, 0xbe,
				     0xb7, 0xb0, 0xa8, 0xa0, 0x98, 0x8e, 0x85,
				     0x7b, 0x72, 0x69, 0x5e, 0x53, 0x48, 0x3e,
				     0x35, 0x2b, 0x22, 0x17, 0x0d, 0x09, 0x07,
				     0x05, 0x01, 0x00, 0x26, 0xf0, 0x86, 0x25,
				     0x6e, 0xb6, 0xdd, 0xf3, 0xd8, 0xcc, 0x9b,
				     0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETDGCLUT,
				     0xff, 0xfb, 0xf9, 0xf6, 0xf4, 0xf1, 0xef,
				     0xea, 0xe7, 0xe5, 0xe2, 0xdf, 0xdd, 0xda,
				     0xd8, 0xd5, 0xd2, 0xcf, 0xcc, 0xc5, 0xbe,
				     0xb7, 0xb0, 0xa8, 0xa0, 0x98, 0x8e, 0x85,
				     0x7b, 0x72, 0x69, 0x5e, 0x53, 0x48, 0x3e,
				     0x35, 0x2b, 0x22, 0x17, 0x0d, 0x09, 0x07,
				     0x05, 0x01, 0x00, 0x26, 0xf0, 0x86, 0x25,
				     0x6e, 0xb6, 0xdd, 0xf3, 0xd8, 0xcc, 0x9b,
				     0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETDISMO, 0xc8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETPANEL, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETGIP0,
				     0x81, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
				     0x04, 0x00, 0x01, 0x13, 0x40, 0x04, 0x09,
				     0x09, 0x0b, 0x0b, 0x32, 0x10, 0x08, 0x00,
				     0x08, 0x32, 0x10, 0x08, 0x00, 0x08, 0x32,
				     0x10, 0x08, 0x00, 0x08, 0x00, 0x00, 0x0a,
				     0x08, 0x7b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0xc5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN1, 0xf7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0xd4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN1, 0x6e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0xef);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETGIP0, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0xc8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETGIP0, 0xa1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETGIP1,
				     0x18, 0x18, 0x19, 0x18, 0x18, 0x20, 0x18,
				     0x18, 0x18, 0x10, 0x10, 0x18, 0x18, 0x00,
				     0x00, 0x18, 0x18, 0x01, 0x01, 0x18, 0x18,
				     0x28, 0x28, 0x18, 0x18, 0x18, 0x18, 0x18,
				     0x2f, 0x2f, 0x30, 0x30, 0x31, 0x31, 0x35,
				     0x35, 0x36, 0x36, 0x37, 0x37, 0x18, 0x18,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xfc,
				     0xfc, 0x00, 0x00, 0xfc, 0xfc, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETGIP2,
				     0x18, 0x18, 0x19, 0x18, 0x18, 0x20, 0x19,
				     0x18, 0x18, 0x10, 0x10, 0x18, 0x18, 0x00,
				     0x00, 0x18, 0x18, 0x01, 0x01, 0x18, 0x18,
				     0x28, 0x28, 0x18, 0x18, 0x18, 0x18, 0x18,
				     0x2f, 0x2f, 0x30, 0x30, 0x31, 0x31, 0x35,
				     0x35, 0x36, 0x36, 0x37, 0x37, 0x18, 0x18,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETGIP3,
				     0xaa, 0xaa, 0xaa, 0xaf, 0xea, 0xaa, 0xaa,
				     0xaa, 0xaa, 0xaf, 0xea, 0xaa, 0xaa, 0xaa,
				     0xab, 0xaf, 0xef, 0xaa, 0xaa, 0xaa, 0xaa,
				     0xaf, 0xea, 0xaa);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETGIP3,
				     0xaa, 0xaa, 0xab, 0xaf, 0xea, 0xaa, 0xaa,
				     0xaa, 0xae, 0xaf, 0xea, 0xaa);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETGIP3,
				     0xaa, 0xaa, 0xaa, 0xaf, 0xea, 0xaa, 0xaa,
				     0xaa, 0xaa, 0xaf, 0xea, 0xaa);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETGIP3,
				     0xba, 0xaa, 0xaa, 0xaf, 0xea, 0xaa, 0xaa,
				     0xaa, 0xaa, 0xaf, 0xea, 0xaa, 0xba, 0xaa,
				     0xaa, 0xaf, 0xea, 0xaa, 0xaa, 0xaa, 0xaa,
				     0xaf, 0xea, 0xaa);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0xe4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN3, 0x17, 0x69);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN3,
				     0x09, 0x09, 0x00, 0x07, 0xe8, 0x00, 0x26,
				     0x00, 0x07, 0x00, 0x00, 0xe8, 0x32, 0x00,
				     0xe9, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x01,
				     0x01, 0x00, 0x12, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN3,
				     0x02, 0x00, 0x01, 0x20, 0x01, 0x18, 0x08,
				     0xa8, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN3, 0x20, 0x20, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN3,
				     0x00, 0xdc, 0x11, 0x70, 0x00, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0xc9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN3,
				     0x2a, 0xce, 0x02, 0x70, 0x01, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN4, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, HX83112B_UNKNOWN2, 0x27);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x24);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	return dsi_ctx.accum_err;
}

static int hx83112b_off(struct hx83112b_panel *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int hx83112b_prepare(struct drm_panel *panel)
{
	struct hx83112b_panel *ctx = to_hx83112b_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(hx83112b_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	hx83112b_reset(ctx);

	ret = hx83112b_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(hx83112b_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int hx83112b_unprepare(struct drm_panel *panel)
{
	struct hx83112b_panel *ctx = to_hx83112b_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = hx83112b_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(hx83112b_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode hx83112b_mode = {
	.clock = (1080 + 40 + 4 + 12) * (2160 + 32 + 2 + 2) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 40,
	.hsync_end = 1080 + 40 + 4,
	.htotal = 1080 + 40 + 4 + 12,
	.vdisplay = 2160,
	.vsync_start = 2160 + 32,
	.vsync_end = 2160 + 32 + 2,
	.vtotal = 2160 + 32 + 2 + 2,
	.width_mm = 65,
	.height_mm = 128,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int hx83112b_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &hx83112b_mode);
}

static const struct drm_panel_funcs hx83112b_panel_funcs = {
	.prepare = hx83112b_prepare,
	.unprepare = hx83112b_unprepare,
	.get_modes = hx83112b_get_modes,
};

static int hx83112b_bl_update_status(struct backlight_device *bl)
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

static const struct backlight_ops hx83112b_bl_ops = {
	.update_status = hx83112b_bl_update_status,
};

static struct backlight_device *
hx83112b_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 4095,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &hx83112b_bl_ops, &props);
}

static int hx83112b_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct hx83112b_panel *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct hx83112b_panel, panel,
				   &hx83112b_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(hx83112b_supplies),
					    hx83112b_supplies,
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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS |
			  MIPI_DSI_MODE_VIDEO_NO_HSA | MIPI_DSI_MODE_LPM;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = hx83112b_create_backlight(dsi);
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

static void hx83112b_remove(struct mipi_dsi_device *dsi)
{
	struct hx83112b_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id hx83112b_of_match[] = {
	{ .compatible = "djn,98-03057-6598b-i" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hx83112b_of_match);

static struct mipi_dsi_driver hx83112b_driver = {
	.probe = hx83112b_probe,
	.remove = hx83112b_remove,
	.driver = {
		.name = "panel-himax-hx83112b",
		.of_match_table = hx83112b_of_match,
	},
};
module_mipi_dsi_driver(hx83112b_driver);

MODULE_DESCRIPTION("DRM driver for hx83112b-equipped DSI panels");
MODULE_LICENSE("GPL");
