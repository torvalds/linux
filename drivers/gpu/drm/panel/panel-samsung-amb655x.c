// SPDX-License-Identifier: GPL-2.0-only
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
//   Copyright (c) 2024 Caleb Connolly <caleb@postmarketos.org>


#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct samsung_amb655x {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[3];
};

static inline
struct samsung_amb655x *to_samsung_amb655x(struct drm_panel *panel)
{
	return container_of(panel, struct samsung_amb655x, panel);
}

static void samsung_amb655x_reset(struct samsung_amb655x *amb655x)
{
	gpiod_set_value_cansleep(amb655x->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(amb655x->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(amb655x->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int samsung_amb655x_on(struct samsung_amb655x *amb655x)
{
	struct drm_dsc_picture_parameter_set pps;
	struct mipi_dsi_device *dsi = amb655x->dsi;
	struct mipi_dsi_multi_context ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	drm_dsc_pps_payload_pack(&pps, &amb655x->dsc);

	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_buffer_multi(&ctx, &pps, sizeof(pps));
	mipi_dsi_dcs_write_long_multi(&ctx, 0x9d, 0x01);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0xa5, 0xa5);

	mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 11);

	/* VLIN CURRENT LIMIT */
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xb0, 0x04);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xd5, 0x24, 0x9e, 0x9e, 0x00, 0x20);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0xa5, 0xa5);

	/* OSC Select */
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xfc, 0x5a, 0x5a);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xb0, 0x16);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xd1, 0x22);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xd6, 0x11);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xfc, 0xa5, 0xa5);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0xa5, 0xa5);

	/* TE ON */
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_long_multi(&ctx, MIPI_DCS_SET_TEAR_ON, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0xa5, 0xa5);

	/* TSP Setting */
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xdf, 0x83, 0x00, 0x10);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xe6, 0x01);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0xa5, 0xa5);

	mipi_dsi_dcs_set_column_address_multi(&ctx, 0x0000, 1080 - 1);
	mipi_dsi_dcs_set_page_address_multi(&ctx, 0x0000, 2400 - 1);

	/* FD Setting */
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xd5, 0x8d);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xb0, 0x0a);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xd5, 0x05);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0xa5, 0xa5);

	/* FFC Function */
	mipi_dsi_dcs_write_long_multi(&ctx, 0xfc, 0x5a, 0x5a);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xe4, 0xa6, 0x75, 0xa3);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xe9,
				      0x11, 0x75, 0xa6, 0x75,
				      0xa3, 0x4b, 0x17, 0xac,
				      0x4b, 0x17, 0xac, 0x00,
				      0x19, 0x19);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xfc, 0xa5, 0xa5);
	mipi_dsi_msleep(&ctx, 61);

	/* Dimming Setting */
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xb0, 0x06);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xb7, 0x01);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xb0, 0x05);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xb7, 0x13);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xb7, 0x4c);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0xa5, 0xa5);

	mipi_dsi_dcs_write_long_multi(&ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);

	/* refresh rate Transition */
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0x5a, 0x5a);
	/* 60 Hz */
	//mipi_dsi_dcs_write_long_multi(&ctx, 0x60, 0x00);
	/* 120 Hz */
	mipi_dsi_dcs_write_long_multi(&ctx, 0x60, 0x10);

	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0xa5, 0xa5);

	/* ACL Mode */
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_long_multi(&ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_long_multi(&ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	mipi_dsi_msleep(&ctx, 110);

	mipi_dsi_dcs_write_long_multi(&ctx, 0x9F, 0x5A, 0x5A);
	mipi_dsi_dcs_set_display_on_multi(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, MIPI_DCS_ENTER_NORMAL_MODE);
	mipi_dsi_dcs_write_long_multi(&ctx, 0x9F, 0xA5, 0xA5);

	return ctx.accum_err;
}

static int samsung_amb655x_off(struct samsung_amb655x *amb655x)
{
	struct mipi_dsi_device *dsi = amb655x->dsi;
	struct mipi_dsi_multi_context ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_long_multi(&ctx, 0x9f, 0x5a, 0x5a);

	mipi_dsi_dcs_set_display_on_multi(&ctx);
	mipi_dsi_msleep(&ctx, 20);

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_msleep(&ctx, 20);

	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);
	mipi_dsi_dcs_write_long_multi(&ctx, 0x9f, 0xa5, 0xa5);

	mipi_dsi_msleep(&ctx, 150);

	return ctx.accum_err;
}

static int samsung_amb655x_prepare(struct drm_panel *panel)
{
	struct samsung_amb655x *ctx = to_samsung_amb655x(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	/*
	 * During the first call to prepare, the regulators are already enabled
	 * since they're boot-on. Avoid enabling them twice so we keep the refcounts
	 * balanced.
	 */
	if (!regulator_is_enabled(ctx->supplies[0].consumer)) {
		ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		if (ret) {
			dev_err(dev, "Failed to enable regulators: %d\n", ret);
			return ret;
		}
	}

	samsung_amb655x_reset(ctx);

	ret = samsung_amb655x_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	msleep(28);

	return 0;
}

static int samsung_amb655x_unprepare(struct drm_panel *panel)
{
	struct samsung_amb655x *amb655x = to_samsung_amb655x(panel);
	struct device *dev = &amb655x->dsi->dev;
	int ret;

	ret = samsung_amb655x_off(amb655x);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(amb655x->reset_gpio, 1);

	ret = regulator_bulk_disable(ARRAY_SIZE(amb655x->supplies), amb655x->supplies);
	if (ret) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct drm_display_mode samsung_amb655x_120_mode = {
	.clock = (1080 + 52 + 24 + 24) * (2400 + 4 + 4 + 8) * 120 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 52,
	.hsync_end = 1080 + 52 + 24,
	.htotal = 1080 + 52 + 24 + 24,
	.vdisplay = 2400,
	.vsync_start = 2400 + 4,
	.vsync_end = 2400 + 4 + 4,
	.vtotal = 2400 + 4 + 4 + 8,
	.width_mm = 70,
	.height_mm = 151,
};

static int samsung_amb655x_get_modes(struct drm_panel *panel,
				     struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &samsung_amb655x_120_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs samsung_amb655x_panel_funcs = {
	.prepare = samsung_amb655x_prepare,
	.unprepare = samsung_amb655x_unprepare,
	.get_modes = samsung_amb655x_get_modes,
};

static int samsung_amb655x_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct backlight_ops samsung_amb655x_bl_ops = {
	.update_status = samsung_amb655x_bl_update_status,
};

static struct backlight_device *
samsung_amb655x_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 2047,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &samsung_amb655x_bl_ops, &props);
}

static int samsung_amb655x_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct samsung_amb655x *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "vdd";
	ctx->supplies[2].supply = "avdd";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to get vddio regulator\n");

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret)
		dev_err(dev, "Failed to enable regulators: %d\n", ret);

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;

	drm_panel_init(&ctx->panel, dev, &samsung_amb655x_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = samsung_amb655x_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 1;

	/* TODO: Pass slice_per_pkt = 2 */
	ctx->dsc.slice_height = 30;
	ctx->dsc.slice_width = 540;
	/*
	 * TODO: hdisplay should be read from the selected mode once
	 * it is passed back to drm_panel (in prepare?)
	 */
	WARN_ON(1080 % ctx->dsc.slice_width);
	ctx->dsc.slice_count = 1080 / ctx->dsc.slice_width;
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

static void samsung_amb655x_remove(struct mipi_dsi_device *dsi)
{
	struct samsung_amb655x *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id samsung_amb655x_of_match[] = {
	{ .compatible = "samsung,amb655x" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, samsung_amb655x_of_match);

static struct mipi_dsi_driver samsung_amb655x_driver = {
	.probe = samsung_amb655x_probe,
	.remove = samsung_amb655x_remove,
	.driver = {
		.name = "panel-samsung-amb655x",
		.of_match_table = samsung_amb655x_of_match,
	},
};
module_mipi_dsi_driver(samsung_amb655x_driver);

MODULE_AUTHOR("Caleb Connolly <caleb@postmarketos.org>");
MODULE_DESCRIPTION("DRM driver for Samsung AMB655X DSC DSI panel");
MODULE_LICENSE("GPL");
