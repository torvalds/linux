// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019-2024 Linaro Ltd
 * Author: Sumit Semwal <sumit.semwal@linaro.org>
 *	 Dmitry Baryshkov <dmitry.baryshkov@linaro.org>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>

#define NUM_SUPPLIES 2

struct sw43408_panel {
	struct drm_panel base;
	struct mipi_dsi_device *link;

	struct regulator_bulk_data supplies[NUM_SUPPLIES];

	struct gpio_desc *reset_gpio;

	struct drm_dsc_config dsc;
};

static inline struct sw43408_panel *to_panel_info(struct drm_panel *panel)
{
	return container_of(panel, struct sw43408_panel, base);
}

static int sw43408_unprepare(struct drm_panel *panel)
{
	struct sw43408_panel *ctx = to_panel_info(panel);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(ctx->link);
	if (ret < 0)
		dev_err(panel->dev, "set_display_off cmd failed ret = %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(ctx->link);
	if (ret < 0)
		dev_err(panel->dev, "enter_sleep cmd failed ret = %d\n", ret);

	msleep(100);

	gpiod_set_value(ctx->reset_gpio, 1);

	return regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
}

static int sw43408_program(struct drm_panel *panel)
{
	struct sw43408_panel *ctx = to_panel_info(panel);
	struct drm_dsc_picture_parameter_set pps;

	mipi_dsi_dcs_write_seq(ctx->link, MIPI_DCS_SET_GAMMA_CURVE, 0x02);

	mipi_dsi_dcs_set_tear_on(ctx->link, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	mipi_dsi_dcs_write_seq(ctx->link, 0x53, 0x0c, 0x30);
	mipi_dsi_dcs_write_seq(ctx->link, 0x55, 0x00, 0x70, 0xdf, 0x00, 0x70, 0xdf);
	mipi_dsi_dcs_write_seq(ctx->link, 0xf7, 0x01, 0x49, 0x0c);

	mipi_dsi_dcs_exit_sleep_mode(ctx->link);

	msleep(135);

	/* COMPRESSION_MODE moved after setting the PPS */

	mipi_dsi_dcs_write_seq(ctx->link, 0xb0, 0xac);
	mipi_dsi_dcs_write_seq(ctx->link, 0xe5,
			       0x00, 0x3a, 0x00, 0x3a, 0x00, 0x0e, 0x10);
	mipi_dsi_dcs_write_seq(ctx->link, 0xb5,
			       0x75, 0x60, 0x2d, 0x5d, 0x80, 0x00, 0x0a, 0x0b,
			       0x00, 0x05, 0x0b, 0x00, 0x80, 0x0d, 0x0e, 0x40,
			       0x00, 0x0c, 0x00, 0x16, 0x00, 0xb8, 0x00, 0x80,
			       0x0d, 0x0e, 0x40, 0x00, 0x0c, 0x00, 0x16, 0x00,
			       0xb8, 0x00, 0x81, 0x00, 0x03, 0x03, 0x03, 0x01,
			       0x01);
	msleep(85);
	mipi_dsi_dcs_write_seq(ctx->link, 0xcd,
			       0x00, 0x00, 0x00, 0x19, 0x19, 0x19, 0x19, 0x19,
			       0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19,
			       0x16, 0x16);
	mipi_dsi_dcs_write_seq(ctx->link, 0xcb, 0x80, 0x5c, 0x07, 0x03, 0x28);
	mipi_dsi_dcs_write_seq(ctx->link, 0xc0, 0x02, 0x02, 0x0f);
	mipi_dsi_dcs_write_seq(ctx->link, 0x55, 0x04, 0x61, 0xdb, 0x04, 0x70, 0xdb);
	mipi_dsi_dcs_write_seq(ctx->link, 0xb0, 0xca);

	mipi_dsi_dcs_set_display_on(ctx->link);

	msleep(50);

	ctx->link->mode_flags &= ~MIPI_DSI_MODE_LPM;

	drm_dsc_pps_payload_pack(&pps, ctx->link->dsc);
	mipi_dsi_picture_parameter_set(ctx->link, &pps);

	ctx->link->mode_flags |= MIPI_DSI_MODE_LPM;

	/*
	 * This panel uses PPS selectors with offset:
	 * PPS 1 if pps_identifier is 0
	 * PPS 2 if pps_identifier is 1
	 */
	mipi_dsi_compression_mode_ext(ctx->link, true,
				      MIPI_DSI_COMPRESSION_DSC, 1);

	return 0;
}

static int sw43408_prepare(struct drm_panel *panel)
{
	struct sw43408_panel *ctx = to_panel_info(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	usleep_range(5000, 6000);

	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(9000, 10000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(9000, 10000);

	ret = sw43408_program(panel);
	if (ret)
		goto poweroff;

	return 0;

poweroff:
	gpiod_set_value(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	return ret;
}

static const struct drm_display_mode sw43408_mode = {
	.clock = (1080 + 20 + 32 + 20) * (2160 + 20 + 4 + 20) * 60 / 1000,

	.hdisplay = 1080,
	.hsync_start = 1080 + 20,
	.hsync_end = 1080 + 20 + 32,
	.htotal = 1080 + 20 + 32 + 20,

	.vdisplay = 2160,
	.vsync_start = 2160 + 20,
	.vsync_end = 2160 + 20 + 4,
	.vtotal = 2160 + 20 + 4 + 20,

	.width_mm = 62,
	.height_mm = 124,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int sw43408_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &sw43408_mode);
}

static int sw43408_backlight_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);

	return mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
}

const struct backlight_ops sw43408_backlight_ops = {
	.update_status = sw43408_backlight_update_status,
};

static int sw43408_backlight_init(struct sw43408_panel *ctx)
{
	struct device *dev = &ctx->link->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_PLATFORM,
		.brightness = 255,
		.max_brightness = 255,
	};

	ctx->base.backlight = devm_backlight_device_register(dev, dev_name(dev), dev,
							     ctx->link,
							     &sw43408_backlight_ops,
							     &props);

	if (IS_ERR(ctx->base.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->base.backlight),
				     "Failed to create backlight\n");

	return 0;
}

static const struct drm_panel_funcs sw43408_funcs = {
	.unprepare = sw43408_unprepare,
	.prepare = sw43408_prepare,
	.get_modes = sw43408_get_modes,
};

static const struct of_device_id sw43408_of_match[] = {
	{ .compatible = "lg,sw43408", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sw43408_of_match);

static int sw43408_add(struct sw43408_panel *ctx)
{
	struct device *dev = &ctx->link->dev;
	int ret;

	ctx->supplies[0].supply = "vddi"; /* 1.88 V */
	ctx->supplies[0].init_load_uA = 62000;
	ctx->supplies[1].supply = "vpnl"; /* 3.0 V */
	ctx->supplies[1].init_load_uA = 857000;

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		return dev_err_probe(dev, ret, "cannot get reset gpio\n");
	}

	ret = sw43408_backlight_init(ctx);
	if (ret < 0)
		return ret;

	ctx->base.prepare_prev_first = true;

	drm_panel_init(&ctx->base, dev, &sw43408_funcs, DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->base);
	return ret;
}

static int sw43408_probe(struct mipi_dsi_device *dsi)
{
	struct sw43408_panel *ctx;
	int ret;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	dsi->mode_flags = MIPI_DSI_MODE_LPM;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 4;

	ctx->link = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	ret = sw43408_add(ctx);
	if (ret < 0)
		return ret;

	/* The panel works only in the DSC mode. Set DSC params. */
	ctx->dsc.dsc_version_major = 0x1;
	ctx->dsc.dsc_version_minor = 0x1;

	/* slice_count * slice_width == width */
	ctx->dsc.slice_height = 16;
	ctx->dsc.slice_width = 540;
	ctx->dsc.slice_count = 2;
	ctx->dsc.bits_per_component = 8;
	ctx->dsc.bits_per_pixel = 8 << 4;
	ctx->dsc.block_pred_enable = true;

	dsi->dsc = &ctx->dsc;

	return mipi_dsi_attach(dsi);
}

static void sw43408_remove(struct mipi_dsi_device *dsi)
{
	struct sw43408_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = sw43408_unprepare(&ctx->base);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to unprepare panel: %d\n", ret);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->base);
}

static struct mipi_dsi_driver sw43408_driver = {
	.driver = {
		.name = "panel-lg-sw43408",
		.of_match_table = sw43408_of_match,
	},
	.probe = sw43408_probe,
	.remove = sw43408_remove,
};
module_mipi_dsi_driver(sw43408_driver);

MODULE_AUTHOR("Sumit Semwal <sumit.semwal@linaro.org>");
MODULE_DESCRIPTION("LG SW436408 MIPI-DSI LED panel");
MODULE_LICENSE("GPL");
