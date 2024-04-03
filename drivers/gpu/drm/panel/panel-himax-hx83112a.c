// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree.
 * Copyright (c) 2024 Luca Weiss <luca.weiss@fairphone.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

/* Manufacturer specific DSI commands */
#define HX83112A_SETPOWER1	0xb1
#define HX83112A_SETDISP	0xb2
#define HX83112A_SETDRV		0xb4
#define HX83112A_SETEXTC	0xb9
#define HX83112A_SETBANK	0xbd
#define HX83112A_SETPTBA	0xbf
#define HX83112A_SETDGCLUT	0xc1
#define HX83112A_SETTCON	0xc7
#define HX83112A_SETCLOCK	0xcb
#define HX83112A_SETPANEL	0xcc
#define HX83112A_SETPOWER2	0xd2
#define HX83112A_SETGIP0	0xd3
#define HX83112A_SETGIP1	0xd5
#define HX83112A_SETGIP2	0xd6
#define HX83112A_SETGIP3	0xd8
#define HX83112A_SETTP1		0xe7
#define HX83112A_UNKNOWN1	0xe9

struct hx83112a_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
};

static inline struct hx83112a_panel *to_hx83112a_panel(struct drm_panel *panel)
{
	return container_of(panel, struct hx83112a_panel, panel);
}

static void hx83112a_reset(struct hx83112a_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(50);
}

static int hx83112a_on(struct hx83112a_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETEXTC, 0x83, 0x11, 0x2a);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETPOWER1,
			       0x08, 0x28, 0x28, 0x83, 0x83, 0x4c, 0x4f, 0x33);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETDISP,
			       0x00, 0x02, 0x00, 0x90, 0x24, 0x00, 0x08, 0x19,
			       0xea, 0x11, 0x11, 0x00, 0x11, 0xa3);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETDRV,
			       0x58, 0x68, 0x58, 0x68, 0x0f, 0xef, 0x0b, 0xc0,
			       0x0b, 0xc0, 0x0b, 0xc0, 0x00, 0xff, 0x00, 0xff,
			       0x00, 0x00, 0x14, 0x15, 0x00, 0x29, 0x11, 0x07,
			       0x12, 0x00, 0x29);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x02);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETDRV,
			       0x00, 0x12, 0x12, 0x11, 0x88, 0x12, 0x12, 0x00,
			       0x53);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x03);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETDGCLUT,
			       0xff, 0xfe, 0xfb, 0xf8, 0xf4, 0xf1, 0xed, 0xe6,
			       0xe2, 0xde, 0xdb, 0xd6, 0xd3, 0xcf, 0xca, 0xc6,
			       0xc2, 0xbe, 0xb9, 0xb0, 0xa7, 0x9e, 0x96, 0x8d,
			       0x84, 0x7c, 0x74, 0x6b, 0x62, 0x5a, 0x51, 0x49,
			       0x41, 0x39, 0x31, 0x29, 0x21, 0x19, 0x12, 0x0a,
			       0x06, 0x05, 0x02, 0x01, 0x00, 0x00, 0xc9, 0xb3,
			       0x08, 0x0e, 0xf2, 0xe1, 0x59, 0xf4, 0x22, 0xad,
			       0x40);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x02);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETDGCLUT,
			       0xff, 0xfe, 0xfb, 0xf8, 0xf4, 0xf1, 0xed, 0xe6,
			       0xe2, 0xde, 0xdb, 0xd6, 0xd3, 0xcf, 0xca, 0xc6,
			       0xc2, 0xbe, 0xb9, 0xb0, 0xa7, 0x9e, 0x96, 0x8d,
			       0x84, 0x7c, 0x74, 0x6b, 0x62, 0x5a, 0x51, 0x49,
			       0x41, 0x39, 0x31, 0x29, 0x21, 0x19, 0x12, 0x0a,
			       0x06, 0x05, 0x02, 0x01, 0x00, 0x00, 0xc9, 0xb3,
			       0x08, 0x0e, 0xf2, 0xe1, 0x59, 0xf4, 0x22, 0xad,
			       0x40);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x01);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETDGCLUT,
			       0xff, 0xfe, 0xfb, 0xf8, 0xf4, 0xf1, 0xed, 0xe6,
			       0xe2, 0xde, 0xdb, 0xd6, 0xd3, 0xcf, 0xca, 0xc6,
			       0xc2, 0xbe, 0xb9, 0xb0, 0xa7, 0x9e, 0x96, 0x8d,
			       0x84, 0x7c, 0x74, 0x6b, 0x62, 0x5a, 0x51, 0x49,
			       0x41, 0x39, 0x31, 0x29, 0x21, 0x19, 0x12, 0x0a,
			       0x06, 0x05, 0x02, 0x01, 0x00, 0x00, 0xc9, 0xb3,
			       0x08, 0x0e, 0xf2, 0xe1, 0x59, 0xf4, 0x22, 0xad,
			       0x40);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETDGCLUT, 0x01);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETTCON,
			       0x70, 0x00, 0x04, 0xe0, 0x33, 0x00);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETPANEL, 0x08);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETPOWER2, 0x2b, 0x2b);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETGIP0,
			       0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08,
			       0x08, 0x03, 0x03, 0x22, 0x18, 0x07, 0x07, 0x07,
			       0x07, 0x32, 0x10, 0x06, 0x00, 0x06, 0x32, 0x10,
			       0x07, 0x00, 0x07, 0x32, 0x19, 0x31, 0x09, 0x31,
			       0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x08,
			       0x09, 0x30, 0x00, 0x00, 0x00, 0x06, 0x0d, 0x00,
			       0x0f);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x01);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETGIP0,
			       0x00, 0x00, 0x19, 0x10, 0x00, 0x0a, 0x00, 0x81);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETGIP1,
			       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
			       0xc0, 0xc0, 0x18, 0x18, 0x19, 0x19, 0x18, 0x18,
			       0x40, 0x40, 0x18, 0x18, 0x18, 0x18, 0x3f, 0x3f,
			       0x28, 0x28, 0x24, 0x24, 0x02, 0x03, 0x02, 0x03,
			       0x00, 0x01, 0x00, 0x01, 0x31, 0x31, 0x31, 0x31,
			       0x30, 0x30, 0x30, 0x30, 0x2f, 0x2f, 0x2f, 0x2f);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETGIP2,
			       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
			       0x40, 0x40, 0x18, 0x18, 0x18, 0x18, 0x19, 0x19,
			       0x40, 0x40, 0x18, 0x18, 0x18, 0x18, 0x3f, 0x3f,
			       0x24, 0x24, 0x28, 0x28, 0x01, 0x00, 0x01, 0x00,
			       0x03, 0x02, 0x03, 0x02, 0x31, 0x31, 0x31, 0x31,
			       0x30, 0x30, 0x30, 0x30, 0x2f, 0x2f, 0x2f, 0x2f);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETGIP3,
			       0xaa, 0xea, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xea,
			       0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xea, 0xab, 0xaa,
			       0xaa, 0xaa, 0xaa, 0xea, 0xab, 0xaa, 0xaa, 0xaa);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x01);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETGIP3,
			       0xaa, 0x2e, 0x28, 0x00, 0x00, 0x00, 0xaa, 0x2e,
			       0x28, 0x00, 0x00, 0x00, 0xaa, 0xee, 0xaa, 0xaa,
			       0xaa, 0xaa, 0xaa, 0xee, 0xaa, 0xaa, 0xaa, 0xaa);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x02);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETGIP3,
			       0xaa, 0xff, 0xff, 0xff, 0xff, 0xff, 0xaa, 0xff,
			       0xff, 0xff, 0xff, 0xff);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x03);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETGIP3,
			       0xaa, 0xaa, 0xea, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
			       0xea, 0xaa, 0xaa, 0xaa, 0xaa, 0xff, 0xff, 0xff,
			       0xff, 0xff, 0xaa, 0xff, 0xff, 0xff, 0xff, 0xff);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETTP1,
			       0x0e, 0x0e, 0x1e, 0x65, 0x1c, 0x65, 0x00, 0x50,
			       0x20, 0x20, 0x00, 0x00, 0x02, 0x02, 0x02, 0x05,
			       0x14, 0x14, 0x32, 0xb9, 0x23, 0xb9, 0x08);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x01);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETTP1,
			       0x02, 0x00, 0xa8, 0x01, 0xa8, 0x0d, 0xa4, 0x0e);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x02);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETTP1,
			       0x00, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
			       0x00, 0x00, 0x00, 0x02, 0x00);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_UNKNOWN1, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETCLOCK, 0xd1, 0xd6);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_UNKNOWN1, 0x3f);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_UNKNOWN1, 0xc6);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_SETPTBA, 0x37);
	mipi_dsi_dcs_write_seq(dsi, HX83112A_UNKNOWN1, 0x3f);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(150);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(50);

	return 0;
}

static int hx83112a_disable(struct drm_panel *panel)
{
	struct hx83112a_panel *ctx = to_hx83112a_panel(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(20);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	return 0;
}

static int hx83112a_prepare(struct drm_panel *panel)
{
	struct hx83112a_panel *ctx = to_hx83112a_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	hx83112a_reset(ctx);

	ret = hx83112a_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int hx83112a_unprepare(struct drm_panel *panel)
{
	struct hx83112a_panel *ctx = to_hx83112a_panel(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode hx83112a_mode = {
	.clock = (1080 + 28 + 8 + 8) * (2340 + 27 + 5 + 5) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 28,
	.hsync_end = 1080 + 28 + 8,
	.htotal = 1080 + 28 + 8 + 8,
	.vdisplay = 2340,
	.vsync_start = 2340 + 27,
	.vsync_end = 2340 + 27 + 5,
	.vtotal = 2340 + 27 + 5 + 5,
	.width_mm = 67,
	.height_mm = 145,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int hx83112a_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &hx83112a_mode);
}

static const struct drm_panel_funcs hx83112a_panel_funcs = {
	.prepare = hx83112a_prepare,
	.unprepare = hx83112a_unprepare,
	.disable = hx83112a_disable,
	.get_modes = hx83112a_get_modes,
};

static int hx83112a_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct hx83112a_panel *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vdd1";
	ctx->supplies[1].supply = "vsn";
	ctx->supplies[2].supply = "vsp";
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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_HSE |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &hx83112a_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void hx83112a_remove(struct mipi_dsi_device *dsi)
{
	struct hx83112a_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id hx83112a_of_match[] = {
	{ .compatible = "djn,9a-3r063-1102b" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hx83112a_of_match);

static struct mipi_dsi_driver hx83112a_driver = {
	.probe = hx83112a_probe,
	.remove = hx83112a_remove,
	.driver = {
		.name = "panel-himax-hx83112a",
		.of_match_table = hx83112a_of_match,
	},
};
module_mipi_dsi_driver(hx83112a_driver);

MODULE_DESCRIPTION("DRM driver for hx83112a-equipped DSI panels");
MODULE_LICENSE("GPL");
