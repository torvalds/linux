// SPDX-License-Identifier: GPL-2.0-only
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved.
//   Copyright (c) 2024, Neil Armstrong <neil.armstrong@linaro.org>

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct boe_tv101wum_ll2 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data boe_tv101wum_ll2_supplies[] = {
	{ .supply = "vsp" },
	{ .supply = "vsn" },
};

static inline struct boe_tv101wum_ll2 *to_boe_tv101wum_ll2(struct drm_panel *panel)
{
	return container_of(panel, struct boe_tv101wum_ll2, panel);
}

static void boe_tv101wum_ll2_reset(struct boe_tv101wum_ll2 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	msleep(120);
}

static int boe_tv101wum_ll2_on(struct boe_tv101wum_ll2 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x5a, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0xff, 0x81, 0x68, 0x6c, 0x22,
				     0x6d, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x5a, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x2c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x5a, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa2, 0x38);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x50, 0x5a, 0x0c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x80, 0xfd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x50, 0x00);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static void boe_tv101wum_ll2_off(struct boe_tv101wum_ll2 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 70);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 20);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x5a);

	mipi_dsi_msleep(&dsi_ctx, 150);
}

static int boe_tv101wum_ll2_prepare(struct drm_panel *panel)
{
	struct boe_tv101wum_ll2 *ctx = to_boe_tv101wum_ll2(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(boe_tv101wum_ll2_supplies),
				    ctx->supplies);
	if (ret < 0)
		return ret;

	boe_tv101wum_ll2_reset(ctx);

	ret = boe_tv101wum_ll2_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(boe_tv101wum_ll2_supplies),
				       ctx->supplies);
		return ret;
	}

	return 0;
}

static int boe_tv101wum_ll2_unprepare(struct drm_panel *panel)
{
	struct boe_tv101wum_ll2 *ctx = to_boe_tv101wum_ll2(panel);

	/* Ignore errors on failure, in any case set gpio and disable regulators */
	boe_tv101wum_ll2_off(ctx);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(boe_tv101wum_ll2_supplies),
			       ctx->supplies);

	return 0;
}

static const struct drm_display_mode boe_tv101wum_ll2_mode = {
	.clock = (1200 + 27 + 8 + 12) * (1920 + 155 + 8 + 32) * 60 / 1000,
	.hdisplay = 1200,
	.hsync_start = 1200 + 27,
	.hsync_end = 1200 + 27 + 8,
	.htotal = 1200 + 27 + 8 + 12,
	.vdisplay = 1920,
	.vsync_start = 1920 + 155,
	.vsync_end = 1920 + 155 + 8,
	.vtotal = 1920 + 155 + 8 + 32,
	.width_mm = 136,
	.height_mm = 217,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int boe_tv101wum_ll2_get_modes(struct drm_panel *panel,
				      struct drm_connector *connector)
{
	/* We do not set display_info.bpc since unset value is bpc=8 by default */
	return drm_connector_helper_get_modes_fixed(connector, &boe_tv101wum_ll2_mode);
}

static const struct drm_panel_funcs boe_tv101wum_ll2_panel_funcs = {
	.prepare = boe_tv101wum_ll2_prepare,
	.unprepare = boe_tv101wum_ll2_unprepare,
	.get_modes = boe_tv101wum_ll2_get_modes,
};

static int boe_tv101wum_ll2_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct boe_tv101wum_ll2 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct boe_tv101wum_ll2, panel,
				   &boe_tv101wum_ll2_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(&dsi->dev,
					ARRAY_SIZE(boe_tv101wum_ll2_supplies),
					boe_tv101wum_ll2_supplies,
					&ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_HSE;

	ctx->panel.prepare_prev_first = true;

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

static void boe_tv101wum_ll2_remove(struct mipi_dsi_device *dsi)
{
	struct boe_tv101wum_ll2 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boe_tv101wum_ll2_of_match[] = {
	{ .compatible = "boe,tv101wum-ll2" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boe_tv101wum_ll2_of_match);

static struct mipi_dsi_driver boe_tv101wum_ll2_driver = {
	.probe = boe_tv101wum_ll2_probe,
	.remove = boe_tv101wum_ll2_remove,
	.driver = {
		.name = "panel-boe-tv101wum_ll2",
		.of_match_table = boe_tv101wum_ll2_of_match,
	},
};
module_mipi_dsi_driver(boe_tv101wum_ll2_driver);

MODULE_DESCRIPTION("DRM driver for BOE TV101WUM-LL2 Panel");
MODULE_LICENSE("GPL");
