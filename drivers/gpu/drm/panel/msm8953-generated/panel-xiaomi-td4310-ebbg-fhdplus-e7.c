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

struct td4310_ebbgplus_e7 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data td4310_ebbgplus_e7_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline
struct td4310_ebbgplus_e7 *to_td4310_ebbgplus_e7(struct drm_panel *panel)
{
	return container_of(panel, struct td4310_ebbgplus_e7, panel);
}

static void td4310_ebbgplus_e7_reset(struct td4310_ebbgplus_e7 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(30);
}

static int td4310_ebbgplus_e7_on(struct td4310_ebbgplus_e7 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7,
					 0x00, 0x10, 0x1c, 0x2c, 0x3a, 0x45,
					 0x5d, 0x6f, 0x7d, 0x8a, 0x3e, 0x4b,
					 0x5b, 0x71, 0x7b, 0x88, 0x98, 0xa5,
					 0xb1, 0x00, 0x10, 0x1c, 0x2c, 0x3a,
					 0x45, 0x5d, 0x6f, 0x7d, 0x8a, 0x3e,
					 0x4b, 0x5b, 0x71, 0x7b, 0x88, 0x98,
					 0xa5, 0xb1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8,
					 0x01, 0x00, 0x03, 0x01, 0x03, 0xfc,
					 0x00, 0x00, 0x03, 0xfe, 0xfe, 0xfc,
					 0x00, 0x00, 0x01, 0xfe, 0xfb, 0xfc,
					 0x00, 0x00);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x00ff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int td4310_ebbgplus_e7_off(struct td4310_ebbgplus_e7 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int td4310_ebbgplus_e7_prepare(struct drm_panel *panel)
{
	struct td4310_ebbgplus_e7 *ctx = to_td4310_ebbgplus_e7(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(td4310_ebbgplus_e7_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	td4310_ebbgplus_e7_reset(ctx);

	ret = td4310_ebbgplus_e7_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(td4310_ebbgplus_e7_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int td4310_ebbgplus_e7_unprepare(struct drm_panel *panel)
{
	struct td4310_ebbgplus_e7 *ctx = to_td4310_ebbgplus_e7(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = td4310_ebbgplus_e7_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(td4310_ebbgplus_e7_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode td4310_ebbgplus_e7_mode = {
	.clock = (1080 + 130 + 2 + 26) * (2160 + 4 + 10 + 54) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 130,
	.hsync_end = 1080 + 130 + 2,
	.htotal = 1080 + 130 + 2 + 26,
	.vdisplay = 2160,
	.vsync_start = 2160 + 4,
	.vsync_end = 2160 + 4 + 10,
	.vtotal = 2160 + 4 + 10 + 54,
	.width_mm = 69,
	.height_mm = 122,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int td4310_ebbgplus_e7_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &td4310_ebbgplus_e7_mode);
}

static const struct drm_panel_funcs td4310_ebbgplus_e7_panel_funcs = {
	.prepare = td4310_ebbgplus_e7_prepare,
	.unprepare = td4310_ebbgplus_e7_unprepare,
	.get_modes = td4310_ebbgplus_e7_get_modes,
};

static int td4310_ebbgplus_e7_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct td4310_ebbgplus_e7 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(td4310_ebbgplus_e7_supplies),
					    td4310_ebbgplus_e7_supplies,
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
			  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &td4310_ebbgplus_e7_panel_funcs,
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

static void td4310_ebbgplus_e7_remove(struct mipi_dsi_device *dsi)
{
	struct td4310_ebbgplus_e7 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id td4310_ebbgplus_e7_of_match[] = {
	{ .compatible = "xiaomi,td4310-ebbg-fhdplus-e7" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, td4310_ebbgplus_e7_of_match);

static struct mipi_dsi_driver td4310_ebbgplus_e7_driver = {
	.probe = td4310_ebbgplus_e7_probe,
	.remove = td4310_ebbgplus_e7_remove,
	.driver = {
		.name = "panel-td4310-ebbgplus-e7",
		.of_match_table = td4310_ebbgplus_e7_of_match,
	},
};
module_mipi_dsi_driver(td4310_ebbgplus_e7_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for td4310 ebbg fhdplus e7 video mode dsi panel");
MODULE_LICENSE("GPL");
