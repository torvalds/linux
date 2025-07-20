//SPDX-License-Identifier: GPL-2.0-only
//Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>

#include <video/mipi_display.h>

struct visionox_r66451 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[2];
};

static inline struct visionox_r66451 *to_visionox_r66451(struct drm_panel *panel)
{
	return container_of(panel, struct visionox_r66451, panel);
}

static void visionox_r66451_reset(struct visionox_r66451 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 10100);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 10100);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 10100);
}

static int visionox_r66451_on(struct visionox_r66451 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2,
				     0x09, 0x24, 0x0c, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
				     0x09, 0x3c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd7,
				     0x00, 0xb9, 0x3c, 0x00, 0x40, 0x04, 0x00, 0xa0, 0x0a,
				     0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19,
				     0x3c, 0x00, 0x40, 0x04, 0x00, 0xa0, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xde,
				     0x40, 0x00, 0x18, 0x00, 0x18, 0x00, 0x18, 0x00, 0x18,
				     0x10, 0x00, 0x18, 0x00, 0x18, 0x00, 0x18, 0x02, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe8, 0x00, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0x00, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc4,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcf,
				     0x64, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
				     0x00, 0x0b, 0x77, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
				     0x02, 0x02, 0x02, 0x02, 0x02, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd3,
				     0x45, 0x00, 0x00, 0x01, 0x13, 0x15, 0x00, 0x15, 0x07,
				     0x0f, 0x77, 0x77, 0x77, 0x37, 0xb2, 0x11, 0x00, 0xa0,
				     0x3c, 0x9c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd7,
				     0x00, 0xb9, 0x34, 0x00, 0x40, 0x04, 0x00, 0xa0, 0x0a,
				     0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19,
				     0x34, 0x00, 0x40, 0x04, 0x00, 0xa0, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x3a, 0x00, 0x3a, 0x00, 0x3a, 0x00, 0x3a, 0x00, 0x3a,
				     0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x0a, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a,
				     0x00, 0x32, 0x00, 0x0a, 0x00, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf,
				     0x50, 0x42, 0x58, 0x81, 0x2d, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x01, 0x0f, 0xff, 0xd4, 0x0e, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x0f, 0x53, 0xf1, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0x34, 0xb4, 0x00, 0x00, 0x00, 0x39,
				     0x04, 0x09, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe6, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x50, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x50, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf2, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x01, 0x00, 0x00, 0x00, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf4, 0x00, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf2, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x50, 0x42);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0, 1080 - 1);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0, 2340 - 1);

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	return dsi_ctx.accum_err;
}

static void visionox_r66451_off(struct visionox_r66451 *ctx)
{
	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
}

static int visionox_r66451_prepare(struct drm_panel *panel)
{
	struct visionox_r66451 *ctx = to_visionox_r66451(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies),
				    ctx->supplies);
	if (ret < 0)
		return ret;

	visionox_r66451_reset(ctx);

	ret = visionox_r66451_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	mipi_dsi_compression_mode(ctx->dsi, true);

	return 0;
}

static int visionox_r66451_unprepare(struct drm_panel *panel)
{
	struct visionox_r66451 *ctx = to_visionox_r66451(panel);

	visionox_r66451_off(ctx);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode visionox_r66451_mode = {
	.clock = 345830,
	.hdisplay = 1080,
	.hsync_start = 1175,
	.hsync_end = 1176,
	.htotal = 1216,
	.vdisplay = 2340,
	.vsync_start = 2365,
	.vsync_end = 2366,
	.vtotal = 2370,
	.width_mm = 0,
	.height_mm = 0,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int visionox_r66451_enable(struct drm_panel *panel)
{
	struct visionox_r66451 *ctx = to_visionox_r66451(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct drm_dsc_picture_parameter_set pps;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	if (!dsi->dsc) {
		dev_err(&dsi->dev, "DSC not attached to DSI\n");
		return -ENODEV;
	}

	drm_dsc_pps_payload_pack(&pps, dsi->dsc);
	mipi_dsi_picture_parameter_set_multi(&dsi_ctx, &pps);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int visionox_r66451_disable(struct drm_panel *panel)
{
	struct visionox_r66451 *ctx = to_visionox_r66451(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int visionox_r66451_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	drm_connector_helper_get_modes_fixed(connector, &visionox_r66451_mode);
	return 1;
}

static const struct drm_panel_funcs visionox_r66451_funcs = {
	.prepare = visionox_r66451_prepare,
	.unprepare = visionox_r66451_unprepare,
	.get_modes = visionox_r66451_get_modes,
	.enable = visionox_r66451_enable,
	.disable = visionox_r66451_disable,
};

static int visionox_r66451_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);

	return mipi_dsi_dcs_set_display_brightness(dsi, brightness);
}

static const struct backlight_ops visionox_r66451_bl_ops = {
	.update_status = visionox_r66451_bl_update_status,
};

static struct backlight_device *
visionox_r66451_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &visionox_r66451_bl_ops, &props);
}

static int visionox_r66451_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct visionox_r66451 *ctx;
	struct drm_dsc_config *dsc;
	int ret = 0;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	dsc = devm_kzalloc(dev, sizeof(*dsc), GFP_KERNEL);
	if (!dsc)
		return -ENOMEM;

	/* Set DSC params */
	dsc->dsc_version_major = 0x1;
	dsc->dsc_version_minor = 0x2;

	dsc->slice_height = 20;
	dsc->slice_width = 540;
	dsc->slice_count = 2;
	dsc->bits_per_component = 8;
	dsc->bits_per_pixel = 8 << 4;
	dsc->block_pred_enable = true;

	dsi->dsc = dsc;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "vdd";

	ret = devm_regulator_bulk_get(&dsi->dev, ARRAY_SIZE(ctx->supplies),
			ctx->supplies);

	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio), "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS;
	ctx->panel.prepare_prev_first = true;

	drm_panel_init(&ctx->panel, dev, &visionox_r66451_funcs, DRM_MODE_CONNECTOR_DSI);
	ctx->panel.backlight = visionox_r66451_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				"Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
	}

	return ret;
}

static void visionox_r66451_remove(struct mipi_dsi_device *dsi)
{
	struct visionox_r66451 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id visionox_r66451_of_match[] = {
	{.compatible = "visionox,r66451"},
	{ /*sentinel*/ }
};
MODULE_DEVICE_TABLE(of, visionox_r66451_of_match);

static struct mipi_dsi_driver visionox_r66451_driver = {
	.probe = visionox_r66451_probe,
	.remove = visionox_r66451_remove,
	.driver = {
		.name = "panel-visionox-r66451",
		.of_match_table = visionox_r66451_of_match,
	},
};

module_mipi_dsi_driver(visionox_r66451_driver);

MODULE_AUTHOR("Jessica Zhang <quic_jesszhan@quicinc.com>");
MODULE_DESCRIPTION("Panel driver for the Visionox R66451 AMOLED DSI panel");
MODULE_LICENSE("GPL");
