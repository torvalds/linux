// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
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

struct shenchao_ili9881 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data shenchao_ili9881_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline
struct shenchao_ili9881 *to_shenchao_ili9881(struct drm_panel *panel)
{
	return container_of(panel, struct shenchao_ili9881, panel);
}

static void shenchao_ili9881_reset(struct shenchao_ili9881 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
}

static int shenchao_ili9881_on(struct shenchao_ili9881 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x98, 0x81, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x2c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 150);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	return dsi_ctx.accum_err;
}

static int shenchao_ili9881_off(struct shenchao_ili9881 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 36);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int shenchao_ili9881_prepare(struct drm_panel *panel)
{
	struct shenchao_ili9881 *ctx = to_shenchao_ili9881(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(shenchao_ili9881_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	shenchao_ili9881_reset(ctx);

	ret = shenchao_ili9881_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(shenchao_ili9881_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int shenchao_ili9881_unprepare(struct drm_panel *panel)
{
	struct shenchao_ili9881 *ctx = to_shenchao_ili9881(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = shenchao_ili9881_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(shenchao_ili9881_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode shenchao_ili9881_mode = {
	.clock = (720 + 52 + 8 + 154) * (1520 + 9 + 2 + 11) * 60 / 1000,
	.hdisplay = 720,
	.hsync_start = 720 + 52,
	.hsync_end = 720 + 52 + 8,
	.htotal = 720 + 52 + 8 + 154,
	.vdisplay = 1520,
	.vsync_start = 1520 + 9,
	.vsync_end = 1520 + 9 + 2,
	.vtotal = 1520 + 9 + 2 + 11,
	.width_mm = 65,
	.height_mm = 138,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int shenchao_ili9881_get_modes(struct drm_panel *panel,
				      struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &shenchao_ili9881_mode);
}

static const struct drm_panel_funcs shenchao_ili9881_panel_funcs = {
	.prepare = shenchao_ili9881_prepare,
	.unprepare = shenchao_ili9881_unprepare,
	.get_modes = shenchao_ili9881_get_modes,
};

static int shenchao_ili9881_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct backlight_ops shenchao_ili9881_bl_ops = {
	.update_status = shenchao_ili9881_bl_update_status,
};

static struct backlight_device *
shenchao_ili9881_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 4095,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &shenchao_ili9881_bl_ops, &props);
}

static int shenchao_ili9881_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct shenchao_ili9881 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(shenchao_ili9881_supplies),
					    shenchao_ili9881_supplies,
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

	drm_panel_init(&ctx->panel, dev, &shenchao_ili9881_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = shenchao_ili9881_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void shenchao_ili9881_remove(struct mipi_dsi_device *dsi)
{
	struct shenchao_ili9881 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id shenchao_ili9881_of_match[] = {
	{ .compatible = "xiaomi,onclite-ili9881" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, shenchao_ili9881_of_match);

static struct mipi_dsi_driver shenchao_ili9881_driver = {
	.probe = shenchao_ili9881_probe,
	.remove = shenchao_ili9881_remove,
	.driver = {
		.name = "panel-shenchao-ili9881",
		.of_match_table = shenchao_ili9881_of_match,
	},
};
module_mipi_dsi_driver(shenchao_ili9881_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for shenchao ili9881 video mode dsi panel");
MODULE_LICENSE("GPL");
