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
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xec,
					 0x64, 0xdc, 0xec, 0x3b, 0x52, 0x00, 0x0b, 0x0b,
					 0x13, 0x15, 0x68, 0x0b, 0xb5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x03);

	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_ADDRESS_MODE, 0x00);

	mipi_dsi_dcs_set_pixel_format_multi(&dsi_ctx, 0x77);
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0x0000, 0x0437);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x077f);
	mipi_dsi_dcs_set_tear_scanline_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x00ff);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x00);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 80);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x03);

	return dsi_ctx.accum_err;
}

static void jdi_fhd_r63452_off(struct jdi_fhd_r63452 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xec,
					 0x64, 0xdc, 0xec, 0x3b, 0x52, 0x00, 0x0b, 0x0b,
					 0x13, 0x15, 0x68, 0x0b, 0x95);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x03);

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 2000, 3000);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
}

static int jdi_fhd_r63452_prepare(struct drm_panel *panel)
{
	struct jdi_fhd_r63452 *ctx = to_jdi_fhd_r63452(panel);
	int ret;

	jdi_fhd_r63452_reset(ctx);

	ret = jdi_fhd_r63452_on(ctx);
	if (ret < 0)
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return ret;
}

static int jdi_fhd_r63452_unprepare(struct drm_panel *panel)
{
	struct jdi_fhd_r63452 *ctx = to_jdi_fhd_r63452(panel);

	/*
	 * NOTE: We don't return an error here as while the panel won't have
	 * been cleanly turned off at least we've asserted the reset signal
	 * so it should be safe to power it back on again later
	 */
	jdi_fhd_r63452_off(ctx);

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
