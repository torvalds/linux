// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

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

struct auo720 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data auo720_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct auo720 *to_auo720(struct drm_panel *panel)
{
	return container_of(panel, struct auo720, panel);
}

static void auo720_reset(struct auo720 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int auo720_on(struct auo720 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0xf7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x00);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0xff0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x2c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35);

	return dsi_ctx.accum_err;
}

static int auo720_off(struct auo720 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x00);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int auo720_prepare(struct drm_panel *panel)
{
	struct auo720 *ctx = to_auo720(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(auo720_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	auo720_reset(ctx);

	ret = auo720_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(auo720_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int auo720_unprepare(struct drm_panel *panel)
{
	struct auo720 *ctx = to_auo720(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = auo720_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(auo720_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode auo720_mode = {
	.clock = (720 + 60 + 8 + 344) * (1440 + 12 + 2 + 14) * 60 / 1000,
	.hdisplay = 720,
	.hsync_start = 720 + 60,
	.hsync_end = 720 + 60 + 8,
	.htotal = 720 + 60 + 8 + 344,
	.vdisplay = 1440,
	.vsync_start = 1440 + 12,
	.vsync_end = 1440 + 12 + 2,
	.vtotal = 1440 + 12 + 2 + 14,
	.width_mm = 68,
	.height_mm = 136,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int auo720_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &auo720_mode);
}

static const struct drm_panel_funcs auo720_panel_funcs = {
	.prepare = auo720_prepare,
	.unprepare = auo720_unprepare,
	.get_modes = auo720_get_modes,
};

static int auo720_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct auo720 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(auo720_supplies),
					    auo720_supplies,
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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS |
			  MIPI_DSI_MODE_VIDEO_NO_HBP | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &auo720_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void auo720_remove(struct mipi_dsi_device *dsi)
{
	struct auo720 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id auo720_of_match[] = {
	{ .compatible = "xiaomi,ysl-ili7807d" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, auo720_of_match);

static struct mipi_dsi_driver auo720_driver = {
	.probe = auo720_probe,
	.remove = auo720_remove,
	.driver = {
		.name = "panel-auo720",
		.of_match_table = auo720_of_match,
	},
};
module_mipi_dsi_driver(auo720_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for auo ili7807d hd720 video mode dsi panel");
MODULE_LICENSE("GPL");
