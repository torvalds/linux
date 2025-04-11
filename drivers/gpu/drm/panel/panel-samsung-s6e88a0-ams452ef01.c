// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2019, Michael Srba

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct s6e88a0_ams452ef01 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
};

static inline struct
s6e88a0_ams452ef01 *to_s6e88a0_ams452ef01(struct drm_panel *panel)
{
	return container_of(panel, struct s6e88a0_ams452ef01, panel);
}

static void s6e88a0_ams452ef01_reset(struct s6e88a0_ams452ef01 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
}

static int s6e88a0_ams452ef01_on(struct s6e88a0_ams452ef01 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a); // enable LEVEL2 commands
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x4c); // set Pixel Clock Divider polarity

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	// set default brightness/gama
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca,
				     0x01, 0x00, 0x01, 0x00, 0x01, 0x00,// V255 RR,GG,BB
				     0x80, 0x80, 0x80,			// V203 R,G,B
				     0x80, 0x80, 0x80,			// V151 R,G,B
				     0x80, 0x80, 0x80,			// V87  R,G,B
				     0x80, 0x80, 0x80,			// V51  R,G,B
				     0x80, 0x80, 0x80,			// V35  R,G,B
				     0x80, 0x80, 0x80,			// V23  R,G,B
				     0x80, 0x80, 0x80,			// V11  R,G,B
				     0x6b, 0x68, 0x71,			// V3   R,G,B
				     0x00, 0x00, 0x00);			// V1   R,G,B
	// set default Amoled Off Ratio
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x40, 0x0a, 0x17, 0x00, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0x2c, 0x0b); // set default elvss voltage
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x03); // gamma/aor update
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5); // disable LEVEL2 commands

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static void s6e88a0_ams452ef01_off(struct s6e88a0_ams452ef01 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi};

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 35);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
}

static int s6e88a0_ams452ef01_prepare(struct drm_panel *panel)
{
	struct s6e88a0_ams452ef01 *ctx = to_s6e88a0_ams452ef01(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	s6e88a0_ams452ef01_reset(ctx);

	ret = s6e88a0_ams452ef01_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies),
				       ctx->supplies);
		return ret;
	}

	return 0;
}

static int s6e88a0_ams452ef01_unprepare(struct drm_panel *panel)
{
	struct s6e88a0_ams452ef01 *ctx = to_s6e88a0_ams452ef01(panel);

	s6e88a0_ams452ef01_off(ctx);

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode s6e88a0_ams452ef01_mode = {
	.clock = (540 + 88 + 4 + 20) * (960 + 14 + 2 + 8) * 60 / 1000,
	.hdisplay = 540,
	.hsync_start = 540 + 88,
	.hsync_end = 540 + 88 + 4,
	.htotal = 540 + 88 + 4 + 20,
	.vdisplay = 960,
	.vsync_start = 960 + 14,
	.vsync_end = 960 + 14 + 2,
	.vtotal = 960 + 14 + 2 + 8,
	.width_mm = 56,
	.height_mm = 100,
};

static int s6e88a0_ams452ef01_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &s6e88a0_ams452ef01_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs s6e88a0_ams452ef01_panel_funcs = {
	.unprepare = s6e88a0_ams452ef01_unprepare,
	.prepare = s6e88a0_ams452ef01_prepare,
	.get_modes = s6e88a0_ams452ef01_get_modes,
};

static int s6e88a0_ams452ef01_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e88a0_ams452ef01 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vdd3";
	ctx->supplies[1].supply = "vci";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		dev_err(dev, "Failed to get reset-gpios: %d\n", ret);
		return ret;
	}

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST;

	drm_panel_init(&ctx->panel, dev, &s6e88a0_ams452ef01_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void s6e88a0_ams452ef01_remove(struct mipi_dsi_device *dsi)
{
	struct s6e88a0_ams452ef01 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6e88a0_ams452ef01_of_match[] = {
	{ .compatible = "samsung,s6e88a0-ams452ef01" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, s6e88a0_ams452ef01_of_match);

static struct mipi_dsi_driver s6e88a0_ams452ef01_driver = {
	.probe = s6e88a0_ams452ef01_probe,
	.remove = s6e88a0_ams452ef01_remove,
	.driver = {
		.name = "panel-s6e88a0-ams452ef01",
		.of_match_table = s6e88a0_ams452ef01_of_match,
	},
};
module_mipi_dsi_driver(s6e88a0_ams452ef01_driver);

MODULE_AUTHOR("Michael Srba <Michael.Srba@seznam.cz>");
MODULE_DESCRIPTION("MIPI-DSI based Panel Driver for AMS452EF01 AMOLED LCD with a S6E88A0 controller");
MODULE_LICENSE("GPL v2");
