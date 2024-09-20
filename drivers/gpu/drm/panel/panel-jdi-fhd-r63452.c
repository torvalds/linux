// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Raffaele Tranquillini <raffaele.tranquillini@gmail.com>
 *
 * Generated using linux-mdss-dsi-panel-driver-generator from Lineage OS device tree:
 * https://github.com/LineageOS/android_kernel_xiaomi_msm8996/blob/lineage-18.1/arch/arm/boot/dts/qcom/a1-msm8996-mtp.dtsi
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct jdi_fhd_r63452 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
};

static inline struct jdi_fhd_r63452 *to_jdi_fhd_r63452(struct drm_panel *panel)
{
	return container_of(panel, struct jdi_fhd_r63452, panel);
}

static void jdi_fhd_r63452_reset(struct jdi_fhd_r63452 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int jdi_fhd_r63452_on(struct jdi_fhd_r63452 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xd6, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xec,
				   0x64, 0xdc, 0xec, 0x3b, 0x52, 0x00, 0x0b, 0x0b,
				   0x13, 0x15, 0x68, 0x0b, 0xb5);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x03);

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear on: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_ADDRESS_MODE, 0x00);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0x77);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_column_address(dsi, 0x0000, 0x0437);
	if (ret < 0) {
		dev_err(dev, "Failed to set column address: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_page_address(dsi, 0x0000, 0x077f);
	if (ret < 0) {
		dev_err(dev, "Failed to set page address: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_tear_scanline(dsi, 0x0000);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear scanline: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_display_brightness(dsi, 0x00ff);
	if (ret < 0) {
		dev_err(dev, "Failed to set display brightness: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x84, 0x00);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(20);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(80);

	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x84, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xc8, 0x11);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x03);

	return 0;
}

static int jdi_fhd_r63452_off(struct jdi_fhd_r63452 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xd6, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xec,
				   0x64, 0xdc, 0xec, 0x3b, 0x52, 0x00, 0x0b, 0x0b,
				   0x13, 0x15, 0x68, 0x0b, 0x95);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x03);

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	usleep_range(2000, 3000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	return 0;
}

static int jdi_fhd_r63452_prepare(struct drm_panel *panel)
{
	struct jdi_fhd_r63452 *ctx = to_jdi_fhd_r63452(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	jdi_fhd_r63452_reset(ctx);

	ret = jdi_fhd_r63452_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int jdi_fhd_r63452_unprepare(struct drm_panel *panel)
{
	struct jdi_fhd_r63452 *ctx = to_jdi_fhd_r63452(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = jdi_fhd_r63452_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode jdi_fhd_r63452_mode = {
	.clock = (1080 + 120 + 16 + 40) * (1920 + 4 + 2 + 4) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 120,
	.hsync_end = 1080 + 120 + 16,
	.htotal = 1080 + 120 + 16 + 40,
	.vdisplay = 1920,
	.vsync_start = 1920 + 4,
	.vsync_end = 1920 + 4 + 2,
	.vtotal = 1920 + 4 + 2 + 4,
	.width_mm = 64,
	.height_mm = 114,
};

static int jdi_fhd_r63452_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &jdi_fhd_r63452_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs jdi_fhd_r63452_panel_funcs = {
	.prepare = jdi_fhd_r63452_prepare,
	.unprepare = jdi_fhd_r63452_unprepare,
	.get_modes = jdi_fhd_r63452_get_modes,
};

static int jdi_fhd_r63452_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct jdi_fhd_r63452 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &jdi_fhd_r63452_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		return ret;
	}

	return 0;
}

static void jdi_fhd_r63452_remove(struct mipi_dsi_device *dsi)
{
	struct jdi_fhd_r63452 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id jdi_fhd_r63452_of_match[] = {
	{ .compatible = "jdi,fhd-r63452" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jdi_fhd_r63452_of_match);

static struct mipi_dsi_driver jdi_fhd_r63452_driver = {
	.probe = jdi_fhd_r63452_probe,
	.remove = jdi_fhd_r63452_remove,
	.driver = {
		.name = "panel-jdi-fhd-r63452",
		.of_match_table = jdi_fhd_r63452_of_match,
	},
};
module_mipi_dsi_driver(jdi_fhd_r63452_driver);

MODULE_AUTHOR("Raffaele Tranquillini <raffaele.tranquillini@gmail.com>");
MODULE_DESCRIPTION("DRM driver for JDI FHD R63452 DSI panel, command mode");
MODULE_LICENSE("GPL v2");
