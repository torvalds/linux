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

struct ofilm_622_v0 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data ofilm_622_v0_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct ofilm_622_v0 *to_ofilm_622_v0(struct drm_panel *panel)
{
	return container_of(panel, struct ofilm_622_v0, panel);
}

static void ofilm_622_v0_reset(struct ofilm_622_v0 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(35);
}

static int ofilm_622_v0_on(struct ofilm_622_v0 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x5a, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0xcc, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x2c, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x5a, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x00);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int ofilm_622_v0_off(struct ofilm_622_v0 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 72);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x5a);
	mipi_dsi_msleep(&dsi_ctx, 150);

	return dsi_ctx.accum_err;
}

static int ofilm_622_v0_prepare(struct drm_panel *panel)
{
	struct ofilm_622_v0 *ctx = to_ofilm_622_v0(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ofilm_622_v0_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ofilm_622_v0_reset(ctx);

	ret = ofilm_622_v0_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ofilm_622_v0_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int ofilm_622_v0_unprepare(struct drm_panel *panel)
{
	struct ofilm_622_v0 *ctx = to_ofilm_622_v0(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = ofilm_622_v0_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ofilm_622_v0_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode ofilm_622_v0_mode = {
	.clock = (720 + 50 + 16 + 48) * (1520 + 50 + 8 + 37) * 60 / 1000,
	.hdisplay = 720,
	.hsync_start = 720 + 50,
	.hsync_end = 720 + 50 + 16,
	.htotal = 720 + 50 + 16 + 48,
	.vdisplay = 1520,
	.vsync_start = 1520 + 50,
	.vsync_end = 1520 + 50 + 8,
	.vtotal = 1520 + 50 + 8 + 37,
	.width_mm = 0,
	.height_mm = 0,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int ofilm_622_v0_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &ofilm_622_v0_mode);
}

static const struct drm_panel_funcs ofilm_622_v0_panel_funcs = {
	.prepare = ofilm_622_v0_prepare,
	.unprepare = ofilm_622_v0_unprepare,
	.get_modes = ofilm_622_v0_get_modes,
};

static int ofilm_622_v0_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ofilm_622_v0 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(ofilm_622_v0_supplies),
					    ofilm_622_v0_supplies,
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
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &ofilm_622_v0_panel_funcs,
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

static void ofilm_622_v0_remove(struct mipi_dsi_device *dsi)
{
	struct ofilm_622_v0 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ofilm_622_v0_of_match[] = {
	{ .compatible = "ofilm,622-v0" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ofilm_622_v0_of_match);

static struct mipi_dsi_driver ofilm_622_v0_driver = {
	.probe = ofilm_622_v0_probe,
	.remove = ofilm_622_v0_remove,
	.driver = {
		.name = "panel-ofilm-622-v0",
		.of_match_table = ofilm_622_v0_of_match,
	},
};
module_mipi_dsi_driver(ofilm_622_v0_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for mipi_mot_vid_ofilm_720p_622");
MODULE_LICENSE("GPL");
