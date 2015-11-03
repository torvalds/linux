/*
 * Copyright (C) 2015 Heiko Schocher <hs@denx.de>
 *
 * from:
 * drivers/gpu/drm/panel/panel-ld9040.c
 * ld9040 AMOLED LCD drm_panel driver.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd
 * Derived from drivers/video/backlight/ld9040.c
 *
 * Andrzej Hajda <a.hajda@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <drm/drmP.h>
#include <drm/drm_panel.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

struct lg4573 {
	struct drm_panel panel;
	struct spi_device *spi;
	struct videomode vm;
};

static inline struct lg4573 *panel_to_lg4573(struct drm_panel *panel)
{
	return container_of(panel, struct lg4573, panel);
}

static int lg4573_spi_write_u16(struct lg4573 *ctx, u16 data)
{
	struct spi_transfer xfer = {
		.len = 2,
	};
	u16 temp = cpu_to_be16(data);
	struct spi_message msg;

	dev_dbg(ctx->panel.dev, "writing data: %x\n", data);
	xfer.tx_buf = &temp;
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(ctx->spi, &msg);
}

static int lg4573_spi_write_u16_array(struct lg4573 *ctx, const u16 *buffer,
				      unsigned int count)
{
	unsigned int i;
	int ret;

	for (i = 0; i < count; i++) {
		ret = lg4573_spi_write_u16(ctx, buffer[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int lg4573_spi_write_dcs(struct lg4573 *ctx, u8 dcs)
{
	return lg4573_spi_write_u16(ctx, (0x70 << 8 | dcs));
}

static int lg4573_display_on(struct lg4573 *ctx)
{
	int ret;

	ret = lg4573_spi_write_dcs(ctx, MIPI_DCS_EXIT_SLEEP_MODE);
	if (ret)
		return ret;

	msleep(5);

	return lg4573_spi_write_dcs(ctx, MIPI_DCS_SET_DISPLAY_ON);
}

static int lg4573_display_off(struct lg4573 *ctx)
{
	int ret;

	ret = lg4573_spi_write_dcs(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	if (ret)
		return ret;

	msleep(120);

	return lg4573_spi_write_dcs(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
}

static int lg4573_display_mode_settings(struct lg4573 *ctx)
{
	static const u16 display_mode_settings[] = {
		0x703A, 0x7270, 0x70B1, 0x7208,
		0x723B, 0x720F, 0x70B2, 0x7200,
		0x72C8, 0x70B3, 0x7200, 0x70B4,
		0x7200, 0x70B5, 0x7242, 0x7210,
		0x7210, 0x7200, 0x7220, 0x70B6,
		0x720B, 0x720F, 0x723C, 0x7213,
		0x7213, 0x72E8, 0x70B7, 0x7246,
		0x7206, 0x720C, 0x7200, 0x7200,
	};

	dev_dbg(ctx->panel.dev, "transfer display mode settings\n");
	return lg4573_spi_write_u16_array(ctx, display_mode_settings,
					  ARRAY_SIZE(display_mode_settings));
}

static int lg4573_power_settings(struct lg4573 *ctx)
{
	static const u16 power_settings[] = {
		0x70C0, 0x7201, 0x7211, 0x70C3,
		0x7207, 0x7203, 0x7204, 0x7204,
		0x7204, 0x70C4, 0x7212, 0x7224,
		0x7218, 0x7218, 0x7202, 0x7249,
		0x70C5, 0x726F, 0x70C6, 0x7241,
		0x7263,
	};

	dev_dbg(ctx->panel.dev, "transfer power settings\n");
	return lg4573_spi_write_u16_array(ctx, power_settings,
					  ARRAY_SIZE(power_settings));
}

static int lg4573_gamma_settings(struct lg4573 *ctx)
{
	static const u16 gamma_settings[] = {
		0x70D0, 0x7203, 0x7207, 0x7273,
		0x7235, 0x7200, 0x7201, 0x7220,
		0x7200, 0x7203, 0x70D1, 0x7203,
		0x7207, 0x7273, 0x7235, 0x7200,
		0x7201, 0x7220, 0x7200, 0x7203,
		0x70D2, 0x7203, 0x7207, 0x7273,
		0x7235, 0x7200, 0x7201, 0x7220,
		0x7200, 0x7203, 0x70D3, 0x7203,
		0x7207, 0x7273, 0x7235, 0x7200,
		0x7201, 0x7220, 0x7200, 0x7203,
		0x70D4, 0x7203, 0x7207, 0x7273,
		0x7235, 0x7200, 0x7201, 0x7220,
		0x7200, 0x7203, 0x70D5, 0x7203,
		0x7207, 0x7273, 0x7235, 0x7200,
		0x7201, 0x7220, 0x7200, 0x7203,
	};

	dev_dbg(ctx->panel.dev, "transfer gamma settings\n");
	return lg4573_spi_write_u16_array(ctx, gamma_settings,
					  ARRAY_SIZE(gamma_settings));
}

static int lg4573_init(struct lg4573 *ctx)
{
	int ret;

	dev_dbg(ctx->panel.dev, "initializing LCD\n");

	ret = lg4573_display_mode_settings(ctx);
	if (ret)
		return ret;

	ret = lg4573_power_settings(ctx);
	if (ret)
		return ret;

	return lg4573_gamma_settings(ctx);
}

static int lg4573_power_on(struct lg4573 *ctx)
{
	return lg4573_display_on(ctx);
}

static int lg4573_disable(struct drm_panel *panel)
{
	struct lg4573 *ctx = panel_to_lg4573(panel);

	return lg4573_display_off(ctx);
}

static int lg4573_enable(struct drm_panel *panel)
{
	struct lg4573 *ctx = panel_to_lg4573(panel);

	lg4573_init(ctx);

	return lg4573_power_on(ctx);
}

static const struct drm_display_mode default_mode = {
	.clock = 27000,
	.hdisplay = 480,
	.hsync_start = 480 + 10,
	.hsync_end = 480 + 10 + 59,
	.htotal = 480 + 10 + 59 + 10,
	.vdisplay = 800,
	.vsync_start = 800 + 15,
	.vsync_end = 800 + 15 + 15,
	.vtotal = 800 + 15 + 15 + 15,
	.vrefresh = 60,
};

static int lg4573_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	panel->connector->display_info.width_mm = 61;
	panel->connector->display_info.height_mm = 103;

	return 1;
}

static const struct drm_panel_funcs lg4573_drm_funcs = {
	.disable = lg4573_disable,
	.enable = lg4573_enable,
	.get_modes = lg4573_get_modes,
};

static int lg4573_probe(struct spi_device *spi)
{
	struct lg4573 *ctx;
	int ret;

	ctx = devm_kzalloc(&spi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->spi = spi;

	spi_set_drvdata(spi, ctx);
	spi->bits_per_word = 8;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "SPI setup failed: %d\n", ret);
		return ret;
	}

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = &spi->dev;
	ctx->panel.funcs = &lg4573_drm_funcs;

	return drm_panel_add(&ctx->panel);
}

static int lg4573_remove(struct spi_device *spi)
{
	struct lg4573 *ctx = spi_get_drvdata(spi);

	lg4573_display_off(ctx);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id lg4573_of_match[] = {
	{ .compatible = "lg,lg4573" },
	{ }
};
MODULE_DEVICE_TABLE(of, lg4573_of_match);

static struct spi_driver lg4573_driver = {
	.probe = lg4573_probe,
	.remove = lg4573_remove,
	.driver = {
		.name = "lg4573",
		.owner = THIS_MODULE,
		.of_match_table = lg4573_of_match,
	},
};
module_spi_driver(lg4573_driver);

MODULE_AUTHOR("Heiko Schocher <hs@denx.de>");
MODULE_DESCRIPTION("lg4573 LCD Driver");
MODULE_LICENSE("GPL v2");
