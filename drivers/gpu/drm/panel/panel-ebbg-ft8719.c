// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Joel Selvaraj <jo@jsfamily.in>
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

static const char * const regulator_names[] = {
	"vddio",
	"vddpos",
	"vddneg",
};

static const unsigned long regulator_enable_loads[] = {
	62000,
	100000,
	100000
};

struct ebbg_ft8719 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;

	struct regulator_bulk_data supplies[ARRAY_SIZE(regulator_names)];

	struct gpio_desc *reset_gpio;
};

static inline
struct ebbg_ft8719 *to_ebbg_ft8719(struct drm_panel *panel)
{
	return container_of(panel, struct ebbg_ft8719, panel);
}

static void ebbg_ft8719_reset(struct ebbg_ft8719 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(4000, 5000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(15000, 16000);
}

static int ebbg_ft8719_on(struct ebbg_ft8719 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x00ff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 90);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int ebbg_ft8719_off(struct ebbg_ft8719 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 90);

	return dsi_ctx.accum_err;
}

static int ebbg_ft8719_prepare(struct drm_panel *panel)
{
	struct ebbg_ft8719 *ctx = to_ebbg_ft8719(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	ebbg_ft8719_reset(ctx);

	ret = ebbg_ft8719_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int ebbg_ft8719_unprepare(struct drm_panel *panel)
{
	struct ebbg_ft8719 *ctx = to_ebbg_ft8719(panel);

	ebbg_ft8719_off(ctx);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode ebbg_ft8719_mode = {
	.clock = (1080 + 28 + 4 + 16) * (2246 + 120 + 4 + 12) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 28,
	.hsync_end = 1080 + 28 + 4,
	.htotal = 1080 + 28 + 4 + 16,
	.vdisplay = 2246,
	.vsync_start = 2246 + 120,
	.vsync_end = 2246 + 120 + 4,
	.vtotal = 2246 + 120 + 4 + 12,
	.width_mm = 68,
	.height_mm = 141,
};

static int ebbg_ft8719_get_modes(struct drm_panel *panel,
				     struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &ebbg_ft8719_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs ebbg_ft8719_panel_funcs = {
	.prepare = ebbg_ft8719_prepare,
	.unprepare = ebbg_ft8719_unprepare,
	.get_modes = ebbg_ft8719_get_modes,
};

static int ebbg_ft8719_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ebbg_ft8719 *ctx;
	int i, ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++)
		ctx->supplies[i].supply = regulator_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++) {
		ret = regulator_set_load(ctx->supplies[i].consumer,
						regulator_enable_loads[i]);
		if (ret)
			return dev_err_probe(dev, ret,
						 "Failed to set regulator load\n");
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &ebbg_ft8719_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void ebbg_ft8719_remove(struct mipi_dsi_device *dsi)
{
	struct ebbg_ft8719 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ebbg_ft8719_of_match[] = {
	{ .compatible = "ebbg,ft8719" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ebbg_ft8719_of_match);

static struct mipi_dsi_driver ebbg_ft8719_driver = {
	.probe = ebbg_ft8719_probe,
	.remove = ebbg_ft8719_remove,
	.driver = {
		.name = "panel-ebbg-ft8719",
		.of_match_table = ebbg_ft8719_of_match,
	},
};
module_mipi_dsi_driver(ebbg_ft8719_driver);

MODULE_AUTHOR("Joel Selvaraj <jo@jsfamily.in>");
MODULE_DESCRIPTION("DRM driver for EBBG FT8719 video dsi panel");
MODULE_LICENSE("GPL v2");
