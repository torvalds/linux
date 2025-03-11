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
#define MCS_PASSWD		0xf0

struct ams581vf01 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data ams581vf01_supplies[] = {
	{ .supply = "vdd3p3" },
	{ .supply = "vddio" },
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct ams581vf01 *to_ams581vf01(struct drm_panel *panel)
{
	return container_of(panel, struct ams581vf01, panel);
}

static void ams581vf01_reset(struct ams581vf01 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int ams581vf01_on(struct ams581vf01 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	/* Sleep Out, Wait 10ms */
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	usleep_range(10000, 11000);

	/* TE On */
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	/* MIC Setting */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD, 0x5a, 0x5a); /* Unlock */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xeb, 0x17,
					       0x41, 0x92,
					       0x0e, 0x10,
					       0x82, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD, 0xa5, 0xa5); /* Lock */

	/* Column & Page Address Setting */
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0x0000, 0x0437);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x0923);

	/* Brightness Setting */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);

	/* Horizontal & Vertical sync Setting */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD, 0x5a, 0x5a); /* Unlock */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_ACCESS_PROT_OFF, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe8, 0x11, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD, 0xa5, 0xa5); /* Lock */
	mipi_dsi_msleep(&dsi_ctx, 110);

	/* Display On */
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static void ams581vf01_off(struct ams581vf01 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	/* Display Off & Sleep In */
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	/* VCI operating mode change */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD, 0x5a, 0x5a); /* Unlock */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_ACCESS_PROT_OFF, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf4, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD, 0xa5, 0xa5);  /* Lock */

	mipi_dsi_msleep(&dsi_ctx, 120);
}

static int ams581vf01_prepare(struct drm_panel *panel)
{
	struct ams581vf01 *ctx = to_ams581vf01(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ams581vf01_supplies),
				    ctx->supplies);
	if (ret < 0)
		return ret;

	ams581vf01_reset(ctx);

	ret = ams581vf01_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ams581vf01_supplies),
				       ctx->supplies);
		return ret;
	}

	return 0;
}

static int ams581vf01_unprepare(struct drm_panel *panel)
{
	struct ams581vf01 *ctx = to_ams581vf01(panel);

	ams581vf01_off(ctx);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ams581vf01_supplies),
			       ctx->supplies);

	return 0;
}

static const struct drm_display_mode ams581vf01_mode = {
	.clock = (1080 + 32 + 73 + 98) * (2340 + 8 + 1 + 8) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 32,
	.hsync_end = 1080 + 32 + 73,
	.htotal = 1080 + 32 + 73 + 98,
	.vdisplay = 2340,
	.vsync_start = 2340 + 8,
	.vsync_end = 2340 + 8 + 1,
	.vtotal = 2340 + 8 + 1 + 8,
	.width_mm = 62,
	.height_mm = 134,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int ams581vf01_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &ams581vf01_mode);
}

static const struct drm_panel_funcs ams581vf01_panel_funcs = {
	.prepare = ams581vf01_prepare,
	.unprepare = ams581vf01_unprepare,
	.get_modes = ams581vf01_get_modes,
};

static int ams581vf01_bl_update_status(struct backlight_device *bl)
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

static const struct backlight_ops ams581vf01_bl_ops = {
	.update_status = ams581vf01_bl_update_status,
};

static struct backlight_device *
ams581vf01_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 511,
		.max_brightness = 1023,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
						&ams581vf01_bl_ops, &props);
}

static int ams581vf01_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ams581vf01 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(&dsi->dev,
					    ARRAY_SIZE(ams581vf01_supplies),
					    ams581vf01_supplies,
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

	drm_panel_init(&ctx->panel, dev, &ams581vf01_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = ams581vf01_create_backlight(dsi);
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

static void ams581vf01_remove(struct mipi_dsi_device *dsi)
{
	struct ams581vf01 *ctx = mipi_dsi_get_drvdata(dsi);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ams581vf01_of_match[] = {
	{ .compatible = "samsung,ams581vf01" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ams581vf01_of_match);

static struct mipi_dsi_driver ams581vf01_driver = {
	.probe = ams581vf01_probe,
	.remove = ams581vf01_remove,
	.driver = {
		.name = "panel-samsung-ams581vf01",
		.of_match_table = ams581vf01_of_match,
	},
};
module_mipi_dsi_driver(ams581vf01_driver);

MODULE_AUTHOR("Danila Tikhonov <danila@jiaxyga.com>");
MODULE_DESCRIPTION("DRM driver for SAMSUNG AMS581VF01 cmd mode dsi panel");
MODULE_LICENSE("GPL");
