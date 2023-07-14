// SPDX-License-Identifier: GPL-2.0
/*
 * NV3051D MIPI-DSI panel driver for Anbernic RG353x
 * Copyright (C) 2022 Chris Morgan
 *
 * based on
 *
 * Elida kd35t133 3.5" MIPI-DSI panel driver
 * Copyright (C) Theobroma Systems 2020
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <video/display_timing.h>
#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct nv3051d_panel_info {
	const struct drm_display_mode *display_modes;
	unsigned int num_modes;
	u16 width_mm, height_mm;
	u32 bus_flags;
};

struct panel_nv3051d {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	const struct nv3051d_panel_info *panel_info;
	struct regulator *vdd;
};

static inline struct panel_nv3051d *panel_to_panelnv3051d(struct drm_panel *panel)
{
	return container_of(panel, struct panel_nv3051d, panel);
}

static int panel_nv3051d_init_sequence(struct panel_nv3051d *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	/*
	 * Init sequence was supplied by device vendor with no
	 * documentation.
	 */

	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x52);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xE3, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x03, 0x40);
	mipi_dsi_dcs_write_seq(dsi, 0x04, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x05, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0x24, 0x12);
	mipi_dsi_dcs_write_seq(dsi, 0x25, 0x1E);
	mipi_dsi_dcs_write_seq(dsi, 0x26, 0x28);
	mipi_dsi_dcs_write_seq(dsi, 0x27, 0x52);
	mipi_dsi_dcs_write_seq(dsi, 0x28, 0x57);
	mipi_dsi_dcs_write_seq(dsi, 0x29, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x2A, 0xDF);
	mipi_dsi_dcs_write_seq(dsi, 0x38, 0x9C);
	mipi_dsi_dcs_write_seq(dsi, 0x39, 0xA7);
	mipi_dsi_dcs_write_seq(dsi, 0x3A, 0x53);
	mipi_dsi_dcs_write_seq(dsi, 0x44, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x49, 0x3C);
	mipi_dsi_dcs_write_seq(dsi, 0x59, 0xFE);
	mipi_dsi_dcs_write_seq(dsi, 0x5C, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x91, 0x77);
	mipi_dsi_dcs_write_seq(dsi, 0x92, 0x77);
	mipi_dsi_dcs_write_seq(dsi, 0xA0, 0x55);
	mipi_dsi_dcs_write_seq(dsi, 0xA1, 0x50);
	mipi_dsi_dcs_write_seq(dsi, 0xA4, 0x9C);
	mipi_dsi_dcs_write_seq(dsi, 0xA7, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xA8, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xA9, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xAA, 0xFC);
	mipi_dsi_dcs_write_seq(dsi, 0xAB, 0x28);
	mipi_dsi_dcs_write_seq(dsi, 0xAC, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xAD, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xAE, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xAF, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0xB0, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xB1, 0x26);
	mipi_dsi_dcs_write_seq(dsi, 0xB2, 0x28);
	mipi_dsi_dcs_write_seq(dsi, 0xB3, 0x28);
	mipi_dsi_dcs_write_seq(dsi, 0xB4, 0x33);
	mipi_dsi_dcs_write_seq(dsi, 0xB5, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xB6, 0x26);
	mipi_dsi_dcs_write_seq(dsi, 0xB7, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xB8, 0x26);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x52);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xB1, 0x0E);
	mipi_dsi_dcs_write_seq(dsi, 0xD1, 0x0E);
	mipi_dsi_dcs_write_seq(dsi, 0xB4, 0x29);
	mipi_dsi_dcs_write_seq(dsi, 0xD4, 0x2B);
	mipi_dsi_dcs_write_seq(dsi, 0xB2, 0x0C);
	mipi_dsi_dcs_write_seq(dsi, 0xD2, 0x0A);
	mipi_dsi_dcs_write_seq(dsi, 0xB3, 0x28);
	mipi_dsi_dcs_write_seq(dsi, 0xD3, 0x28);
	mipi_dsi_dcs_write_seq(dsi, 0xB6, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0xD6, 0x0D);
	mipi_dsi_dcs_write_seq(dsi, 0xB7, 0x32);
	mipi_dsi_dcs_write_seq(dsi, 0xD7, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0xC1, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xE1, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xB8, 0x0A);
	mipi_dsi_dcs_write_seq(dsi, 0xD8, 0x0A);
	mipi_dsi_dcs_write_seq(dsi, 0xB9, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xD9, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xBD, 0x13);
	mipi_dsi_dcs_write_seq(dsi, 0xDD, 0x13);
	mipi_dsi_dcs_write_seq(dsi, 0xBC, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0xDC, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0xBB, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0xDB, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0xBA, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0xDA, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0xBE, 0x18);
	mipi_dsi_dcs_write_seq(dsi, 0xDE, 0x18);
	mipi_dsi_dcs_write_seq(dsi, 0xBF, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0xDF, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0xC0, 0x17);
	mipi_dsi_dcs_write_seq(dsi, 0xE0, 0x17);
	mipi_dsi_dcs_write_seq(dsi, 0xB5, 0x3B);
	mipi_dsi_dcs_write_seq(dsi, 0xD5, 0x3C);
	mipi_dsi_dcs_write_seq(dsi, 0xB0, 0x0B);
	mipi_dsi_dcs_write_seq(dsi, 0xD0, 0x0C);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x52);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x2A);
	mipi_dsi_dcs_write_seq(dsi, 0x01, 0x2A);
	mipi_dsi_dcs_write_seq(dsi, 0x02, 0x2A);
	mipi_dsi_dcs_write_seq(dsi, 0x03, 0x2A);
	mipi_dsi_dcs_write_seq(dsi, 0x04, 0x61);
	mipi_dsi_dcs_write_seq(dsi, 0x05, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0x06, 0xC7);
	mipi_dsi_dcs_write_seq(dsi, 0x07, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x08, 0x82);
	mipi_dsi_dcs_write_seq(dsi, 0x09, 0x83);
	mipi_dsi_dcs_write_seq(dsi, 0x30, 0x2A);
	mipi_dsi_dcs_write_seq(dsi, 0x31, 0x2A);
	mipi_dsi_dcs_write_seq(dsi, 0x32, 0x2A);
	mipi_dsi_dcs_write_seq(dsi, 0x33, 0x2A);
	mipi_dsi_dcs_write_seq(dsi, 0x34, 0x61);
	mipi_dsi_dcs_write_seq(dsi, 0x35, 0xC5);
	mipi_dsi_dcs_write_seq(dsi, 0x36, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0x37, 0x23);
	mipi_dsi_dcs_write_seq(dsi, 0x40, 0x82);
	mipi_dsi_dcs_write_seq(dsi, 0x41, 0x83);
	mipi_dsi_dcs_write_seq(dsi, 0x42, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0x43, 0x81);
	mipi_dsi_dcs_write_seq(dsi, 0x44, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0x45, 0xF2);
	mipi_dsi_dcs_write_seq(dsi, 0x46, 0xF1);
	mipi_dsi_dcs_write_seq(dsi, 0x47, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0x48, 0xF4);
	mipi_dsi_dcs_write_seq(dsi, 0x49, 0xF3);
	mipi_dsi_dcs_write_seq(dsi, 0x50, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0x51, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x52, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x53, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0x54, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0x55, 0xF6);
	mipi_dsi_dcs_write_seq(dsi, 0x56, 0xF5);
	mipi_dsi_dcs_write_seq(dsi, 0x57, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0x58, 0xF8);
	mipi_dsi_dcs_write_seq(dsi, 0x59, 0xF7);
	mipi_dsi_dcs_write_seq(dsi, 0x7E, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0x7F, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0xE0, 0x5A);
	mipi_dsi_dcs_write_seq(dsi, 0xB1, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xB4, 0x0E);
	mipi_dsi_dcs_write_seq(dsi, 0xB5, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0xB6, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xB7, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0xB8, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xB9, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xBA, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0xC7, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xCA, 0x0E);
	mipi_dsi_dcs_write_seq(dsi, 0xCB, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0xCC, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xCD, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0xCE, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xCF, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xD0, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0x81, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0x84, 0x0E);
	mipi_dsi_dcs_write_seq(dsi, 0x85, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0x86, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0x87, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x88, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0x89, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x8A, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x97, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0x9A, 0x0E);
	mipi_dsi_dcs_write_seq(dsi, 0x9B, 0x0F);
	mipi_dsi_dcs_write_seq(dsi, 0x9C, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0x9D, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x9E, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0x9F, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xA0, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x52);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0x01, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x02, 0xDA);
	mipi_dsi_dcs_write_seq(dsi, 0x03, 0xBA);
	mipi_dsi_dcs_write_seq(dsi, 0x04, 0xA8);
	mipi_dsi_dcs_write_seq(dsi, 0x05, 0x9A);
	mipi_dsi_dcs_write_seq(dsi, 0x06, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x07, 0xFF);
	mipi_dsi_dcs_write_seq(dsi, 0x08, 0x91);
	mipi_dsi_dcs_write_seq(dsi, 0x09, 0x90);
	mipi_dsi_dcs_write_seq(dsi, 0x0A, 0xFF);
	mipi_dsi_dcs_write_seq(dsi, 0x0B, 0x8F);
	mipi_dsi_dcs_write_seq(dsi, 0x0C, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x0D, 0x58);
	mipi_dsi_dcs_write_seq(dsi, 0x0E, 0x48);
	mipi_dsi_dcs_write_seq(dsi, 0x0F, 0x38);
	mipi_dsi_dcs_write_seq(dsi, 0x10, 0x2B);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x52);
	mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x36, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0x3A, 0x70);

	dev_dbg(ctx->dev, "Panel init sequence done\n");

	return 0;
}

static int panel_nv3051d_unprepare(struct drm_panel *panel)
{
	struct panel_nv3051d *ctx = panel_to_panelnv3051d(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		dev_err(ctx->dev, "failed to set display off: %d\n", ret);

	msleep(20);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to enter sleep mode: %d\n", ret);
		return ret;
	}

	usleep_range(10000, 15000);

	regulator_disable(ctx->vdd);

	return 0;
}

static int panel_nv3051d_prepare(struct drm_panel *panel)
{
	struct panel_nv3051d *ctx = panel_to_panelnv3051d(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	dev_dbg(ctx->dev, "Resetting the panel\n");
	ret = regulator_enable(ctx->vdd);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to enable vdd supply: %d\n", ret);
		return ret;
	}

	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(150);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);

	ret = panel_nv3051d_init_sequence(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "Panel init sequence failed: %d\n", ret);
		goto disable_vdd;
	}

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to exit sleep mode: %d\n", ret);
		goto disable_vdd;
	}

	msleep(200);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to set display on: %d\n", ret);
		goto disable_vdd;
	}

	usleep_range(10000, 15000);

	return 0;

disable_vdd:
	regulator_disable(ctx->vdd);
	return ret;
}

static int panel_nv3051d_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	struct panel_nv3051d *ctx = panel_to_panelnv3051d(panel);
	const struct nv3051d_panel_info *panel_info = ctx->panel_info;
	struct drm_display_mode *mode;
	unsigned int i;

	for (i = 0; i < panel_info->num_modes; i++) {
		mode = drm_mode_duplicate(connector->dev,
					  &panel_info->display_modes[i]);
		if (!mode)
			return -ENOMEM;

		drm_mode_set_name(mode);

		mode->type = DRM_MODE_TYPE_DRIVER;
		if (panel_info->num_modes == 1)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.bpc = 8;
	connector->display_info.width_mm = panel_info->width_mm;
	connector->display_info.height_mm = panel_info->height_mm;
	connector->display_info.bus_flags = panel_info->bus_flags;

	return panel_info->num_modes;
}

static const struct drm_panel_funcs panel_nv3051d_funcs = {
	.unprepare	= panel_nv3051d_unprepare,
	.prepare	= panel_nv3051d_prepare,
	.get_modes	= panel_nv3051d_get_modes,
};

static int panel_nv3051d_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct panel_nv3051d *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;

	ctx->panel_info = of_device_get_match_data(dev);
	if (!ctx->panel_info)
		return -EINVAL;

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset gpio\n");
		return PTR_ERR(ctx->reset_gpio);
	}

	ctx->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ctx->vdd)) {
		ret = PTR_ERR(ctx->vdd);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to request vdd regulator: %d\n", ret);
		return ret;
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET;

	drm_panel_init(&ctx->panel, &dsi->dev, &panel_nv3051d_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach failed: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void panel_nv3051d_shutdown(struct mipi_dsi_device *dsi)
{
	struct panel_nv3051d *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = drm_panel_unprepare(&ctx->panel);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to unprepare panel: %d\n", ret);

	ret = drm_panel_disable(&ctx->panel);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to disable panel: %d\n", ret);
}

static void panel_nv3051d_remove(struct mipi_dsi_device *dsi)
{
	struct panel_nv3051d *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	panel_nv3051d_shutdown(dsi);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct drm_display_mode nv3051d_rgxx3_modes[] = {
	{ /* 120hz */
		.hdisplay	= 640,
		.hsync_start	= 640 + 40,
		.hsync_end	= 640 + 40 + 2,
		.htotal		= 640 + 40 + 2 + 80,
		.vdisplay	= 480,
		.vsync_start	= 480 + 18,
		.vsync_end	= 480 + 18 + 2,
		.vtotal		= 480 + 18 + 2 + 28,
		.clock		= 48300,
		.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
	{ /* 100hz */
		.hdisplay       = 640,
		.hsync_start    = 640 + 40,
		.hsync_end      = 640 + 40 + 2,
		.htotal         = 640 + 40 + 2 + 80,
		.vdisplay       = 480,
		.vsync_start    = 480 + 18,
		.vsync_end      = 480 + 18 + 2,
		.vtotal         = 480 + 18 + 2 + 28,
		.clock          = 40250,
		.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
	{ /* 60hz */
		.hdisplay	= 640,
		.hsync_start	= 640 + 40,
		.hsync_end	= 640 + 40 + 2,
		.htotal		= 640 + 40 + 2 + 80,
		.vdisplay	= 480,
		.vsync_start	= 480 + 18,
		.vsync_end	= 480 + 18 + 2,
		.vtotal		= 480 + 18 + 2 + 28,
		.clock		= 24150,
		.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
};

static const struct nv3051d_panel_info nv3051d_rgxx3_info = {
	.display_modes = nv3051d_rgxx3_modes,
	.num_modes = ARRAY_SIZE(nv3051d_rgxx3_modes),
	.width_mm = 70,
	.height_mm = 57,
	.bus_flags = DRM_BUS_FLAG_DE_LOW | DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE,
};

static const struct of_device_id newvision_nv3051d_of_match[] = {
	{ .compatible = "newvision,nv3051d", .data = &nv3051d_rgxx3_info },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, newvision_nv3051d_of_match);

static struct mipi_dsi_driver newvision_nv3051d_driver = {
	.driver = {
		.name = "panel-newvision-nv3051d",
		.of_match_table = newvision_nv3051d_of_match,
	},
	.probe	= panel_nv3051d_probe,
	.remove = panel_nv3051d_remove,
	.shutdown = panel_nv3051d_shutdown,
};
module_mipi_dsi_driver(newvision_nv3051d_driver);

MODULE_AUTHOR("Chris Morgan <macromorgan@hotmail.com>");
MODULE_DESCRIPTION("DRM driver for Newvision NV3051D based MIPI DSI panels");
MODULE_LICENSE("GPL");
