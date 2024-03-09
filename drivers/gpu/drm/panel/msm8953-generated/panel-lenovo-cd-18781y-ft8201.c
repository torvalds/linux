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

struct boyift8201_800p {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data boyift8201_800p_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline
struct boyift8201_800p *to_boyift8201_800p(struct drm_panel *panel)
{
	return container_of(panel, struct boyift8201_800p, panel);
}

static void boyift8201_800p_reset(struct boyift8201_800p *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(50);
}

static int boyift8201_800p_on(struct boyift8201_800p *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x11, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 150);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x29, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int boyift8201_800p_off(struct boyift8201_800p *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 150);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int boyift8201_800p_prepare(struct drm_panel *panel)
{
	struct boyift8201_800p *ctx = to_boyift8201_800p(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(boyift8201_800p_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	boyift8201_800p_reset(ctx);

	ret = boyift8201_800p_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(boyift8201_800p_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int boyift8201_800p_unprepare(struct drm_panel *panel)
{
	struct boyift8201_800p *ctx = to_boyift8201_800p(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = boyift8201_800p_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(boyift8201_800p_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode boyift8201_800p_mode = {
	.clock = (800 + 25 + 25 + 25) * (1280 + 184 + 2 + 32) * 60 / 1000,
	.hdisplay = 800,
	.hsync_start = 800 + 25,
	.hsync_end = 800 + 25 + 25,
	.htotal = 800 + 25 + 25 + 25,
	.vdisplay = 1280,
	.vsync_start = 1280 + 184,
	.vsync_end = 1280 + 184 + 2,
	.vtotal = 1280 + 184 + 2 + 32,
	.width_mm = 0,
	.height_mm = 0,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int boyift8201_800p_get_modes(struct drm_panel *panel,
				     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &boyift8201_800p_mode);
}

static const struct drm_panel_funcs boyift8201_800p_panel_funcs = {
	.prepare = boyift8201_800p_prepare,
	.unprepare = boyift8201_800p_unprepare,
	.get_modes = boyift8201_800p_get_modes,
};

static int boyift8201_800p_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct boyift8201_800p *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(boyift8201_800p_supplies),
					    boyift8201_800p_supplies,
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

	drm_panel_init(&ctx->panel, dev, &boyift8201_800p_panel_funcs,
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

static void boyift8201_800p_remove(struct mipi_dsi_device *dsi)
{
	struct boyift8201_800p *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boyift8201_800p_of_match[] = {
	{ .compatible = "lenovo,cd-18781y-ft8201" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boyift8201_800p_of_match);

static struct mipi_dsi_driver boyift8201_800p_driver = {
	.probe = boyift8201_800p_probe,
	.remove = boyift8201_800p_remove,
	.driver = {
		.name = "panel-boyift8201-800p",
		.of_match_table = boyift8201_800p_of_match,
	},
};
module_mipi_dsi_driver(boyift8201_800p_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for boyift8201 800p video mode dsi panel");
MODULE_LICENSE("GPL");
