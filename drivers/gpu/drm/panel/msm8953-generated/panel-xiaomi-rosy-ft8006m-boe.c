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

struct ft8006m_boe_5p7 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data ft8006m_boe_5p7_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline
struct ft8006m_boe_5p7 *to_ft8006m_boe_5p7(struct drm_panel *panel)
{
	return container_of(panel, struct ft8006m_boe_5p7, panel);
}

static void ft8006m_boe_5p7_reset(struct ft8006m_boe_5p7 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(35);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int ft8006m_boe_5p7_on(struct ft8006m_boe_5p7 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x5a, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x83, 0xac, 0xb4, 0x6d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x5a, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90,
				     0xfc, 0x6f, 0xf6, 0xef, 0xcf, 0xaf, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_READ_DDB_START,
				     0x00, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80,
				     0xea, 0xd3, 0xc6, 0xb8, 0xaf, 0xa6, 0x9e,
				     0x98, 0x92, 0x8e, 0x8a, 0x85, 0x00, 0x60,
				     0xf6, 0xcf);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x5a, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90,
				     0xff, 0x0f, 0x00, 0x00, 0x2c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 2000, 3000);

	return dsi_ctx.accum_err;
}

static int ft8006m_boe_5p7_off(struct ft8006m_boe_5p7 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x5a);

	return dsi_ctx.accum_err;
}

static int ft8006m_boe_5p7_prepare(struct drm_panel *panel)
{
	struct ft8006m_boe_5p7 *ctx = to_ft8006m_boe_5p7(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ft8006m_boe_5p7_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ft8006m_boe_5p7_reset(ctx);

	ret = ft8006m_boe_5p7_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ft8006m_boe_5p7_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int ft8006m_boe_5p7_unprepare(struct drm_panel *panel)
{
	struct ft8006m_boe_5p7 *ctx = to_ft8006m_boe_5p7(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = ft8006m_boe_5p7_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ft8006m_boe_5p7_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode ft8006m_boe_5p7_mode = {
	.clock = (720 + 45 + 14 + 25) * (1440 + 65 + 8 + 37) * 60 / 1000,
	.hdisplay = 720,
	.hsync_start = 720 + 45,
	.hsync_end = 720 + 45 + 14,
	.htotal = 720 + 45 + 14 + 25,
	.vdisplay = 1440,
	.vsync_start = 1440 + 65,
	.vsync_end = 1440 + 65 + 8,
	.vtotal = 1440 + 65 + 8 + 37,
	.width_mm = 65,
	.height_mm = 129,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int ft8006m_boe_5p7_get_modes(struct drm_panel *panel,
				     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &ft8006m_boe_5p7_mode);
}

static const struct drm_panel_funcs ft8006m_boe_5p7_panel_funcs = {
	.prepare = ft8006m_boe_5p7_prepare,
	.unprepare = ft8006m_boe_5p7_unprepare,
	.get_modes = ft8006m_boe_5p7_get_modes,
};

static int ft8006m_boe_5p7_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ft8006m_boe_5p7 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(ft8006m_boe_5p7_supplies),
					    ft8006m_boe_5p7_supplies,
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
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &ft8006m_boe_5p7_panel_funcs,
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

static void ft8006m_boe_5p7_remove(struct mipi_dsi_device *dsi)
{
	struct ft8006m_boe_5p7 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ft8006m_boe_5p7_of_match[] = {
	{ .compatible = "xiaomi,rosy-ft8006m-boe" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ft8006m_boe_5p7_of_match);

static struct mipi_dsi_driver ft8006m_boe_5p7_driver = {
	.probe = ft8006m_boe_5p7_probe,
	.remove = ft8006m_boe_5p7_remove,
	.driver = {
		.name = "panel-ft8006m-boe-5p7",
		.of_match_table = ft8006m_boe_5p7_of_match,
	},
};
module_mipi_dsi_driver(ft8006m_boe_5p7_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for ft8006m_boe_5p7_720p_video");
MODULE_LICENSE("GPL");
