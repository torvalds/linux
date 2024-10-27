// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung AMS427AP24 panel with S6E88A0 controller
 * Copyright (c) 2024 Jakob Hauser <jahau@rocketmail.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct s6e88a0_ams427ap24 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data s6e88a0_ams427ap24_supplies[] = {
	{ .supply = "vdd3" },
	{ .supply = "vci" },
};

static inline
struct s6e88a0_ams427ap24 *to_s6e88a0_ams427ap24(struct drm_panel *panel)
{
	return container_of(panel, struct s6e88a0_ams427ap24, panel);
}

static void s6e88a0_ams427ap24_reset(struct s6e88a0_ams427ap24 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(18000, 19000);
}

static int s6e88a0_ams427ap24_on(struct s6e88a0_ams427ap24 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a); // level 1 key on
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0x5a, 0x5a); // level 2 key on
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x11); // src latch set global 1
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfd, 0x11); // src latch set 1
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x13); // src latch set global 2
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfd, 0x18); // src latch set 2
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x02); // avdd set 1
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x30); // avdd set 2

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf1, 0x5a, 0x5a); // level 3 key on
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x4c); // pixel clock divider pol.
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf2, 0x03, 0x0d); // unknown
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf1, 0xa5, 0xa5); // level 3 key off
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca,
				     0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x00, 0x00, 0x00); // set gamma
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2,
				     0x40, 0x08, 0x20, 0x00, 0x08); // set aid
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0x28, 0x0b); // set elvss
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x03); // gamma update
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x00); // acl off
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5); // level 1 key off
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0xa5, 0xa5); // level 2 key off

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int s6e88a0_ams427ap24_off(struct s6e88a0_ams427ap24 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int s6e88a0_ams427ap24_prepare(struct drm_panel *panel)
{
	struct s6e88a0_ams427ap24 *ctx = to_s6e88a0_ams427ap24(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(s6e88a0_ams427ap24_supplies),
				    ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	s6e88a0_ams427ap24_reset(ctx);

	ret = s6e88a0_ams427ap24_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(s6e88a0_ams427ap24_supplies),
				       ctx->supplies);
		return ret;
	}

	return 0;
}

static int s6e88a0_ams427ap24_unprepare(struct drm_panel *panel)
{
	struct s6e88a0_ams427ap24 *ctx = to_s6e88a0_ams427ap24(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = s6e88a0_ams427ap24_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(s6e88a0_ams427ap24_supplies),
			       ctx->supplies);

	return 0;
}

static const struct drm_display_mode s6e88a0_ams427ap24_mode = {
	.clock = (540 + 94 + 4 + 18) * (960 + 12 + 1 + 3) * 60 / 1000,
	.hdisplay = 540,
	.hsync_start = 540 + 94,
	.hsync_end = 540 + 94 + 4,
	.htotal = 540 + 94 + 4 + 18,
	.vdisplay = 960,
	.vsync_start = 960 + 12,
	.vsync_end = 960 + 12 + 1,
	.vtotal = 960 + 12 + 1 + 3,
	.width_mm = 55,
	.height_mm = 95,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int s6e88a0_ams427ap24_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector,
						    &s6e88a0_ams427ap24_mode);
}

static const struct drm_panel_funcs s6e88a0_ams427ap24_panel_funcs = {
	.prepare = s6e88a0_ams427ap24_prepare,
	.unprepare = s6e88a0_ams427ap24_unprepare,
	.get_modes = s6e88a0_ams427ap24_get_modes,
};

static int s6e88a0_ams427ap24_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e88a0_ams427ap24 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
				      ARRAY_SIZE(s6e88a0_ams427ap24_supplies),
				      s6e88a0_ams427ap24_supplies,
				      &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_NO_EOT_PACKET;

	drm_panel_init(&ctx->panel, dev, &s6e88a0_ams427ap24_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void s6e88a0_ams427ap24_remove(struct mipi_dsi_device *dsi)
{
	struct s6e88a0_ams427ap24 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6e88a0_ams427ap24_of_match[] = {
	{ .compatible = "samsung,s6e88a0-ams427ap24" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, s6e88a0_ams427ap24_of_match);

static struct mipi_dsi_driver s6e88a0_ams427ap24_driver = {
	.probe = s6e88a0_ams427ap24_probe,
	.remove = s6e88a0_ams427ap24_remove,
	.driver = {
		.name = "panel-s6e88a0-ams427ap24",
		.of_match_table = s6e88a0_ams427ap24_of_match,
	},
};
module_mipi_dsi_driver(s6e88a0_ams427ap24_driver);

MODULE_AUTHOR("Jakob Hauser <jahau@rocketmail.com>");
MODULE_DESCRIPTION("Samsung AMS427AP24 panel with S6E88A0 controller");
MODULE_LICENSE("GPL v2");
