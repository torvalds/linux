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

struct nt36672_tianmaplus_e7 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data nt36672_tianmaplus_e7_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline
struct nt36672_tianmaplus_e7 *to_nt36672_tianmaplus_e7(struct drm_panel *panel)
{
	return container_of(panel, struct nt36672_tianmaplus_e7, panel);
}

static void nt36672_tianmaplus_e7_reset(struct nt36672_tianmaplus_e7 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int nt36672_tianmaplus_e7_on(struct nt36672_tianmaplus_e7 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x51, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x53, 0x2c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x55, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x11, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x29, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int nt36672_tianmaplus_e7_off(struct nt36672_tianmaplus_e7 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x00);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int nt36672_tianmaplus_e7_prepare(struct drm_panel *panel)
{
	struct nt36672_tianmaplus_e7 *ctx = to_nt36672_tianmaplus_e7(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(nt36672_tianmaplus_e7_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	nt36672_tianmaplus_e7_reset(ctx);

	ret = nt36672_tianmaplus_e7_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(nt36672_tianmaplus_e7_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int nt36672_tianmaplus_e7_unprepare(struct drm_panel *panel)
{
	struct nt36672_tianmaplus_e7 *ctx = to_nt36672_tianmaplus_e7(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = nt36672_tianmaplus_e7_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(nt36672_tianmaplus_e7_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode nt36672_tianmaplus_e7_mode = {
	.clock = (1080 + 16 + 20 + 64) * (2160 + 4 + 2 + 4) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 16,
	.hsync_end = 1080 + 16 + 20,
	.htotal = 1080 + 16 + 20 + 64,
	.vdisplay = 2160,
	.vsync_start = 2160 + 4,
	.vsync_end = 2160 + 4 + 2,
	.vtotal = 2160 + 4 + 2 + 4,
	.width_mm = 69,
	.height_mm = 122,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int nt36672_tianmaplus_e7_get_modes(struct drm_panel *panel,
					   struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &nt36672_tianmaplus_e7_mode);
}

static const struct drm_panel_funcs nt36672_tianmaplus_e7_panel_funcs = {
	.prepare = nt36672_tianmaplus_e7_prepare,
	.unprepare = nt36672_tianmaplus_e7_unprepare,
	.get_modes = nt36672_tianmaplus_e7_get_modes,
};

static int nt36672_tianmaplus_e7_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt36672_tianmaplus_e7 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(nt36672_tianmaplus_e7_supplies),
					    nt36672_tianmaplus_e7_supplies,
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
			  MIPI_DSI_MODE_VIDEO_HSE |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &nt36672_tianmaplus_e7_panel_funcs,
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

static void nt36672_tianmaplus_e7_remove(struct mipi_dsi_device *dsi)
{
	struct nt36672_tianmaplus_e7 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id nt36672_tianmaplus_e7_of_match[] = {
	{ .compatible = "xiaomi,nt36672-tianma-fhdplus-e7" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt36672_tianmaplus_e7_of_match);

static struct mipi_dsi_driver nt36672_tianmaplus_e7_driver = {
	.probe = nt36672_tianmaplus_e7_probe,
	.remove = nt36672_tianmaplus_e7_remove,
	.driver = {
		.name = "panel-nt36672-tianmaplus-e7",
		.of_match_table = nt36672_tianmaplus_e7_of_match,
	},
};
module_mipi_dsi_driver(nt36672_tianmaplus_e7_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for nt36672 tianma e7 fhdplus video mode dsi panel");
MODULE_LICENSE("GPL");
