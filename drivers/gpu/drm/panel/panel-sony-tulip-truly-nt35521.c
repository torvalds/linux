// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Linaro Limited
 *
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
 *   Copyright (c) 2013, The Linux Foundation. All rights reserved.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct truly_nt35521 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *blen_gpio;
};

static inline
struct truly_nt35521 *to_truly_nt35521(struct drm_panel *panel)
{
	return container_of(panel, struct truly_nt35521, panel);
}

static void truly_nt35521_reset(struct truly_nt35521 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(150);
}

static int truly_nt35521_on(struct truly_nt35521 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_generic_write_seq(dsi, 0x6f, 0x11, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xf7, 0x20, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x6f, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xb1, 0x21);
	mipi_dsi_generic_write_seq(dsi, 0xbd, 0x01, 0xa0, 0x10, 0x08, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xb8, 0x01, 0x02, 0x0c, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0xbb, 0x11, 0x11);
	mipi_dsi_generic_write_seq(dsi, 0xbc, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xb6, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x09, 0x09);
	mipi_dsi_generic_write_seq(dsi, 0xb1, 0x09, 0x09);
	mipi_dsi_generic_write_seq(dsi, 0xbc, 0x8c, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xbd, 0x8c, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xca, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xc0, 0x04);
	mipi_dsi_generic_write_seq(dsi, 0xbe, 0xb5);
	mipi_dsi_generic_write_seq(dsi, 0xb3, 0x35, 0x35);
	mipi_dsi_generic_write_seq(dsi, 0xb4, 0x25, 0x25);
	mipi_dsi_generic_write_seq(dsi, 0xb9, 0x43, 0x43);
	mipi_dsi_generic_write_seq(dsi, 0xba, 0x24, 0x24);
	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0xee, 0x03);
	mipi_dsi_generic_write_seq(dsi, 0xb0,
				   0x00, 0xb2, 0x00, 0xb3, 0x00, 0xb6, 0x00, 0xc3,
				   0x00, 0xce, 0x00, 0xe1, 0x00, 0xf3, 0x01, 0x11);
	mipi_dsi_generic_write_seq(dsi, 0xb1,
				   0x01, 0x2e, 0x01, 0x5c, 0x01, 0x82, 0x01, 0xc3,
				   0x01, 0xfe, 0x02, 0x00, 0x02, 0x37, 0x02, 0x77);
	mipi_dsi_generic_write_seq(dsi, 0xb2,
				   0x02, 0xa1, 0x02, 0xd7, 0x02, 0xfe, 0x03, 0x2c,
				   0x03, 0x4b, 0x03, 0x63, 0x03, 0x8f, 0x03, 0x90);
	mipi_dsi_generic_write_seq(dsi, 0xb3, 0x03, 0x96, 0x03, 0x98);
	mipi_dsi_generic_write_seq(dsi, 0xb4,
				   0x00, 0x81, 0x00, 0x8b, 0x00, 0x9c, 0x00, 0xa9,
				   0x00, 0xb5, 0x00, 0xcb, 0x00, 0xdf, 0x01, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0xb5,
				   0x01, 0x1f, 0x01, 0x51, 0x01, 0x7a, 0x01, 0xbf,
				   0x01, 0xfa, 0x01, 0xfc, 0x02, 0x34, 0x02, 0x76);
	mipi_dsi_generic_write_seq(dsi, 0xb6,
				   0x02, 0x9f, 0x02, 0xd7, 0x02, 0xfc, 0x03, 0x2c,
				   0x03, 0x4a, 0x03, 0x63, 0x03, 0x8f, 0x03, 0xa2);
	mipi_dsi_generic_write_seq(dsi, 0xb7, 0x03, 0xb8, 0x03, 0xba);
	mipi_dsi_generic_write_seq(dsi, 0xb8,
				   0x00, 0x01, 0x00, 0x02, 0x00, 0x0e, 0x00, 0x2a,
				   0x00, 0x41, 0x00, 0x67, 0x00, 0x87, 0x00, 0xb9);
	mipi_dsi_generic_write_seq(dsi, 0xb9,
				   0x00, 0xe2, 0x01, 0x22, 0x01, 0x54, 0x01, 0xa3,
				   0x01, 0xe6, 0x01, 0xe7, 0x02, 0x24, 0x02, 0x67);
	mipi_dsi_generic_write_seq(dsi, 0xba,
				   0x02, 0x93, 0x02, 0xcd, 0x02, 0xf6, 0x03, 0x31,
				   0x03, 0x6c, 0x03, 0xe9, 0x03, 0xef, 0x03, 0xf4);
	mipi_dsi_generic_write_seq(dsi, 0xbb, 0x03, 0xf6, 0x03, 0xf7);
	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x03);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x22, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xb1, 0x22, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xb2, 0x05, 0x00, 0x60, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xb3, 0x05, 0x00, 0x60, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xb4, 0x05, 0x00, 0x60, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xb5, 0x05, 0x00, 0x60, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xba, 0x53, 0x00, 0x60, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xbb, 0x53, 0x00, 0x60, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xbc, 0x53, 0x00, 0x60, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xbd, 0x53, 0x00, 0x60, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xc0, 0x00, 0x34, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xc1, 0x00, 0x00, 0x34, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xc2, 0x00, 0x00, 0x34, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xc3, 0x00, 0x00, 0x34, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xc4, 0x60);
	mipi_dsi_generic_write_seq(dsi, 0xc5, 0xc0);
	mipi_dsi_generic_write_seq(dsi, 0xc6, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xc7, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x05);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x17, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xb1, 0x17, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xb2, 0x17, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xb3, 0x17, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xb4, 0x17, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xb5, 0x17, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xb6, 0x17, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xb7, 0x17, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xb8, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xb9, 0x00, 0x03);
	mipi_dsi_generic_write_seq(dsi, 0xba, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xbb, 0x02, 0x03);
	mipi_dsi_generic_write_seq(dsi, 0xbc, 0x02, 0x03);
	mipi_dsi_generic_write_seq(dsi, 0xbd, 0x03, 0x03, 0x00, 0x03, 0x03);
	mipi_dsi_generic_write_seq(dsi, 0xc0, 0x0b);
	mipi_dsi_generic_write_seq(dsi, 0xc1, 0x09);
	mipi_dsi_generic_write_seq(dsi, 0xc2, 0xa6);
	mipi_dsi_generic_write_seq(dsi, 0xc3, 0x05);
	mipi_dsi_generic_write_seq(dsi, 0xc4, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xc5, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0xc6, 0x22);
	mipi_dsi_generic_write_seq(dsi, 0xc7, 0x03);
	mipi_dsi_generic_write_seq(dsi, 0xc8, 0x07, 0x20);
	mipi_dsi_generic_write_seq(dsi, 0xc9, 0x03, 0x20);
	mipi_dsi_generic_write_seq(dsi, 0xca, 0x01, 0x60);
	mipi_dsi_generic_write_seq(dsi, 0xcb, 0x01, 0x60);
	mipi_dsi_generic_write_seq(dsi, 0xcc, 0x00, 0x00, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0xcd, 0x00, 0x00, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0xce, 0x00, 0x00, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0xcf, 0x00, 0x00, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0xd1, 0x00, 0x05, 0x01, 0x07, 0x10);
	mipi_dsi_generic_write_seq(dsi, 0xd2, 0x10, 0x05, 0x05, 0x03, 0x10);
	mipi_dsi_generic_write_seq(dsi, 0xd3, 0x20, 0x00, 0x43, 0x07, 0x10);
	mipi_dsi_generic_write_seq(dsi, 0xd4, 0x30, 0x00, 0x43, 0x07, 0x10);
	mipi_dsi_generic_write_seq(dsi, 0xd0,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xd5,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xd6,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xd7,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xd8, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xe5, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xe6, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xe7, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xe8, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xe9, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xea, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xeb, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xec, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xed, 0x30);
	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xb1, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xb2, 0x2d, 0x2e);
	mipi_dsi_generic_write_seq(dsi, 0xb3, 0x31, 0x34);
	mipi_dsi_generic_write_seq(dsi, 0xb4, 0x29, 0x2a);
	mipi_dsi_generic_write_seq(dsi, 0xb5, 0x12, 0x10);
	mipi_dsi_generic_write_seq(dsi, 0xb6, 0x18, 0x16);
	mipi_dsi_generic_write_seq(dsi, 0xb7, 0x00, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0xb8, 0x08, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xb9, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xba, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xbb, 0x31, 0x08);
	mipi_dsi_generic_write_seq(dsi, 0xbc, 0x03, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xbd, 0x17, 0x19);
	mipi_dsi_generic_write_seq(dsi, 0xbe, 0x11, 0x13);
	mipi_dsi_generic_write_seq(dsi, 0xbf, 0x2a, 0x29);
	mipi_dsi_generic_write_seq(dsi, 0xc0, 0x34, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xc1, 0x2e, 0x2d);
	mipi_dsi_generic_write_seq(dsi, 0xc2, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xc3, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xc4, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xc5, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xc6, 0x2e, 0x2d);
	mipi_dsi_generic_write_seq(dsi, 0xc7, 0x31, 0x34);
	mipi_dsi_generic_write_seq(dsi, 0xc8, 0x29, 0x2a);
	mipi_dsi_generic_write_seq(dsi, 0xc9, 0x17, 0x19);
	mipi_dsi_generic_write_seq(dsi, 0xca, 0x11, 0x13);
	mipi_dsi_generic_write_seq(dsi, 0xcb, 0x03, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xcc, 0x08, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xcd, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xce, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xcf, 0x31, 0x08);
	mipi_dsi_generic_write_seq(dsi, 0xd0, 0x00, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0xd1, 0x12, 0x10);
	mipi_dsi_generic_write_seq(dsi, 0xd2, 0x18, 0x16);
	mipi_dsi_generic_write_seq(dsi, 0xd3, 0x2a, 0x29);
	mipi_dsi_generic_write_seq(dsi, 0xd4, 0x34, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xd5, 0x2d, 0x2e);
	mipi_dsi_generic_write_seq(dsi, 0xd6, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xd7, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xe5, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xe6, 0x31, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xd8, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xd9, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xe7, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x6f, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0xf7, 0x47);
	mipi_dsi_generic_write_seq(dsi, 0x6f, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xf7, 0x02);
	mipi_dsi_generic_write_seq(dsi, 0x6f, 0x17);
	mipi_dsi_generic_write_seq(dsi, 0xf4, 0x60);
	mipi_dsi_generic_write_seq(dsi, 0x6f, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xf9, 0x46);
	mipi_dsi_generic_write_seq(dsi, 0x6f, 0x11);
	mipi_dsi_generic_write_seq(dsi, 0xf3, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0x35, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xd9, 0x02, 0x03, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xb1, 0x6c, 0x21);
	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x35, 0x00);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	usleep_range(1000, 2000);

	mipi_dsi_generic_write_seq(dsi, 0x53, 0x24);

	return 0;
}

static int truly_nt35521_off(struct truly_nt35521 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(50);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(150);

	return 0;
}

static int truly_nt35521_prepare(struct drm_panel *panel)
{
	struct truly_nt35521 *ctx = to_truly_nt35521(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	truly_nt35521_reset(ctx);

	ret = truly_nt35521_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int truly_nt35521_unprepare(struct drm_panel *panel)
{
	struct truly_nt35521 *ctx = to_truly_nt35521(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = truly_nt35521_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies),
			       ctx->supplies);

	return 0;
}

static int truly_nt35521_enable(struct drm_panel *panel)
{
	struct truly_nt35521 *ctx = to_truly_nt35521(panel);

	gpiod_set_value_cansleep(ctx->blen_gpio, 1);

	return 0;
}

static int truly_nt35521_disable(struct drm_panel *panel)
{
	struct truly_nt35521 *ctx = to_truly_nt35521(panel);

	gpiod_set_value_cansleep(ctx->blen_gpio, 0);

	return 0;
}

static const struct drm_display_mode truly_nt35521_mode = {
	.clock = (720 + 232 + 20 + 112) * (1280 + 18 + 1 + 18) * 60 / 1000,
	.hdisplay = 720,
	.hsync_start = 720 + 232,
	.hsync_end = 720 + 232 + 20,
	.htotal = 720 + 232 + 20 + 112,
	.vdisplay = 1280,
	.vsync_start = 1280 + 18,
	.vsync_end = 1280 + 18 + 1,
	.vtotal = 1280 + 18 + 1 + 18,
	.width_mm = 65,
	.height_mm = 116,
};

static int truly_nt35521_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &truly_nt35521_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs truly_nt35521_panel_funcs = {
	.prepare = truly_nt35521_prepare,
	.unprepare = truly_nt35521_unprepare,
	.enable = truly_nt35521_enable,
	.disable = truly_nt35521_disable,
	.get_modes = truly_nt35521_get_modes,
};

static int truly_nt35521_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

	return 0;
}

static int truly_nt35521_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	return brightness & 0xff;
}

static const struct backlight_ops truly_nt35521_bl_ops = {
	.update_status = truly_nt35521_bl_update_status,
	.get_brightness = truly_nt35521_bl_get_brightness,
};

static struct backlight_device *
truly_nt35521_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &truly_nt35521_bl_ops, &props);
}

static int truly_nt35521_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct truly_nt35521 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "positive5";
	ctx->supplies[1].supply = "negative5";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->blen_gpio = devm_gpiod_get(dev, "backlight", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->blen_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->blen_gpio),
				     "Failed to get backlight-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &truly_nt35521_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = truly_nt35521_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void truly_nt35521_remove(struct mipi_dsi_device *dsi)
{
	struct truly_nt35521 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id truly_nt35521_of_match[] = {
	{ .compatible = "sony,tulip-truly-nt35521" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, truly_nt35521_of_match);

static struct mipi_dsi_driver truly_nt35521_driver = {
	.probe = truly_nt35521_probe,
	.remove = truly_nt35521_remove,
	.driver = {
		.name = "panel-truly-nt35521",
		.of_match_table = truly_nt35521_of_match,
	},
};
module_mipi_dsi_driver(truly_nt35521_driver);

MODULE_AUTHOR("Shawn Guo <shawn.guo@linaro.org>");
MODULE_DESCRIPTION("DRM driver for Sony Tulip Truly NT35521 panel");
MODULE_LICENSE("GPL v2");
