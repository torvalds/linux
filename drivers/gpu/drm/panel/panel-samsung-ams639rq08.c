// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Danila Tikhonov <danila@jiaxyga.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

/* Manufacturer Command Set */
#define MCS_ACCESS_PROT_OFF	0xb0
#define MCS_UNKNOWN_B7		0xb7
#define MCS_BIAS_CURRENT_CTRL	0xd1
#define MCS_PASSWD1		0xf0
#define MCS_PASSWD2		0xfc
#define MCS_UNKNOWN_FF		0xff

struct ams639rq08 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data ams639rq08_supplies[] = {
	{ .supply = "vdd3p3" },
	{ .supply = "vddio" },
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct ams639rq08 *to_ams639rq08(struct drm_panel *panel)
{
	return container_of(panel, struct ams639rq08, panel);
}

static void ams639rq08_reset(struct ams639rq08 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int ams639rq08_on(struct ams639rq08 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	/* Delay 2ms for VCI1 power */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD1, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD2, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_ACCESS_PROT_OFF, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_UNKNOWN_FF, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_ACCESS_PROT_OFF, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_BIAS_CURRENT_CTRL, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD1, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD2, 0xa5, 0xa5);

	/* Sleep Out */
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	usleep_range(10000, 11000);

	/* TE OUT (Vsync On) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD1, 0x5a, 0x5a);

	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	/* DBV Smooth Transition */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_UNKNOWN_B7, 0x01, 0x4b);

	/* Edge Dimming Speed Setting */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_ACCESS_PROT_OFF, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_UNKNOWN_B7, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD1, 0xa5, 0xa5);

	/* Page Address Set */
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x0923);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD1, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD2, 0x5a, 0x5a);

	/* Set DDIC internal HFP */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_ACCESS_PROT_OFF, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_BIAS_CURRENT_CTRL, 0x11);

	/* OFC Setting 84.1 Mhz */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe9, 0x11, 0x55,
					       0xa6, 0x75, 0xa3,
					       0xb9, 0xa1, 0x4a,
					       0x00, 0x1a, 0xb8);

	/* Err_FG Setting */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1,
					       0x00, 0x00, 0x02,
					       0x02, 0x42, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2,
					       0x00, 0x00, 0x00,
					       0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_ACCESS_PROT_OFF, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD1, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD2, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);

	/* Brightness Control */
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);

	/* Display On */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 67);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static void ams639rq08_off(struct ams639rq08 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
}

static int ams639rq08_prepare(struct drm_panel *panel)
{
	struct ams639rq08 *ctx = to_ams639rq08(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ams639rq08_supplies),
				    ctx->supplies);
	if (ret < 0)
		return ret;

	ams639rq08_reset(ctx);

	ret = ams639rq08_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ams639rq08_supplies),
				       ctx->supplies);
		return ret;
	}

	return 0;
}

static int ams639rq08_unprepare(struct drm_panel *panel)
{
	struct ams639rq08 *ctx = to_ams639rq08(panel);

	ams639rq08_off(ctx);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ams639rq08_supplies),
			       ctx->supplies);

	return 0;
}

static const struct drm_display_mode ams639rq08_mode = {
	.clock = (1080 + 64 + 20 + 64) * (2340 + 64 + 20 + 64) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 64,
	.hsync_end = 1080 + 64 + 20,
	.htotal = 1080 + 64 + 20 + 64,
	.vdisplay = 2340,
	.vsync_start = 2340 + 64,
	.vsync_end = 2340 + 64 + 20,
	.vtotal = 2340 + 64 + 20 + 64,
	.width_mm = 68,
	.height_mm = 147,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int ams639rq08_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &ams639rq08_mode);
}

static const struct drm_panel_funcs ams639rq08_panel_funcs = {
	.prepare = ams639rq08_prepare,
	.unprepare = ams639rq08_unprepare,
	.get_modes = ams639rq08_get_modes,
};

static int ams639rq08_bl_update_status(struct backlight_device *bl)
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

static int ams639rq08_bl_get_brightness(struct backlight_device *bl)
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

static const struct backlight_ops ams639rq08_bl_ops = {
	.update_status = ams639rq08_bl_update_status,
	.get_brightness = ams639rq08_bl_get_brightness,
};

static struct backlight_device *
ams639rq08_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1023,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
						&ams639rq08_bl_ops, &props);
}

static int ams639rq08_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ams639rq08 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(&dsi->dev,
					    ARRAY_SIZE(ams639rq08_supplies),
					    ams639rq08_supplies,
					    &ctx->supplies);
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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &ams639rq08_panel_funcs,
			DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = ams639rq08_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
					"Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void ams639rq08_remove(struct mipi_dsi_device *dsi)
{
	struct ams639rq08 *ctx = mipi_dsi_get_drvdata(dsi);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ams639rq08_of_match[] = {
	{ .compatible = "samsung,ams639rq08" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ams639rq08_of_match);

static struct mipi_dsi_driver ams639rq08_driver = {
	.probe = ams639rq08_probe,
	.remove = ams639rq08_remove,
	.driver = {
		.name = "panel-samsung-ams639rq08",
		.of_match_table = ams639rq08_of_match,
	},
};
module_mipi_dsi_driver(ams639rq08_driver);

MODULE_AUTHOR("Danila Tikhonov <danila@jiaxyga.com>");
MODULE_DESCRIPTION("DRM driver for SAMSUNG AMS639RQ08 cmd mode dsi panel");
MODULE_LICENSE("GPL");
