// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct jdi_r63452_5p5 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data jdi_r63452_5p5_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct jdi_r63452_5p5 *to_jdi_r63452_5p5(struct drm_panel *panel)
{
	return container_of(panel, struct jdi_r63452_5p5, panel);
}

static void jdi_r63452_5p5_reset(struct jdi_r63452_5p5 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int jdi_r63452_5p5_on(struct jdi_r63452_5p5 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca,
					 0x1d, 0xfc, 0xfc, 0xfc, 0x00, 0xfa,
					 0x01, 0x9c, 0x00, 0xf5, 0xe0, 0x05,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x9f, 0x6e,
					 0x61, 0x9d, 0x69, 0x5b, 0x2d, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	return dsi_ctx.accum_err;
}

static int jdi_r63452_5p5_off(struct jdi_r63452_5p5 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int jdi_r63452_5p5_prepare(struct drm_panel *panel)
{
	struct jdi_r63452_5p5 *ctx = to_jdi_r63452_5p5(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(jdi_r63452_5p5_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	jdi_r63452_5p5_reset(ctx);

	ret = jdi_r63452_5p5_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(jdi_r63452_5p5_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int jdi_r63452_5p5_unprepare(struct drm_panel *panel)
{
	struct jdi_r63452_5p5 *ctx = to_jdi_r63452_5p5(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = jdi_r63452_5p5_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(jdi_r63452_5p5_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode jdi_r63452_5p5_mode = {
	.clock = (1080 + 45 + 8 + 45) * (1920 + 16 + 4 + 16) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 45,
	.hsync_end = 1080 + 45 + 8,
	.htotal = 1080 + 45 + 8 + 45,
	.vdisplay = 1920,
	.vsync_start = 1920 + 16,
	.vsync_end = 1920 + 16 + 4,
	.vtotal = 1920 + 16 + 4 + 16,
	.width_mm = 68,
	.height_mm = 121,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int jdi_r63452_5p5_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &jdi_r63452_5p5_mode);
}

static const struct drm_panel_funcs jdi_r63452_5p5_panel_funcs = {
	.prepare = jdi_r63452_5p5_prepare,
	.unprepare = jdi_r63452_5p5_unprepare,
	.get_modes = jdi_r63452_5p5_get_modes,
};

static int jdi_r63452_5p5_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct jdi_r63452_5p5 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(jdi_r63452_5p5_supplies),
					    jdi_r63452_5p5_supplies,
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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_HSE |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &jdi_r63452_5p5_panel_funcs,
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

static void jdi_r63452_5p5_remove(struct mipi_dsi_device *dsi)
{
	struct jdi_r63452_5p5 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id jdi_r63452_5p5_of_match[] = {
	{ .compatible = "huawei,milan-jdi-r63452" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jdi_r63452_5p5_of_match);

static struct mipi_dsi_driver jdi_r63452_5p5_driver = {
	.probe = jdi_r63452_5p5_probe,
	.remove = jdi_r63452_5p5_remove,
	.driver = {
		.name = "panel-jdi-r63452-5p5",
		.of_match_table = jdi_r63452_5p5_of_match,
	},
};
module_mipi_dsi_driver(jdi_r63452_5p5_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for JDI_R63452_5P5_1080P_CMD");
MODULE_LICENSE("GPL");
