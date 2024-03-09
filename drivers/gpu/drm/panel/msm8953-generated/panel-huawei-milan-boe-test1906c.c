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

struct boe_test1906c_5p5 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data boe_test1906c_5p5_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline
struct boe_test1906c_5p5 *to_boe_test1906c_5p5(struct drm_panel *panel)
{
	return container_of(panel, struct boe_test1906c_5p5, panel);
}

static void boe_test1906c_5p5_reset(struct boe_test1906c_5p5 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(15000, 16000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(15000, 16000);
}

static int boe_test1906c_5p5_on(struct boe_test1906c_5p5 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x19, 0x06, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x19, 0x06);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1, 0xcc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1, 0xdd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xda, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdb, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdc, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1, 0x00, 0xc0, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x91);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x14, 0x1e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x85);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x14);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x86);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x14);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x95);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x05);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x06);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x81);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa5, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5,
					 0x09, 0x16, 0x09, 0x16);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0x1a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x9d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0x1a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0x16);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x8d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0x17);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xe3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xed);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0x16);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xe5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0x16);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x81);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0x16);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x83);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0x16);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xe1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0x16);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc,
					 0x0c, 0x0c, 0x00, 0x00, 0x0c, 0x0c,
					 0x0c, 0x0c, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x33,
					 0x33, 0x33, 0x33);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc,
					 0x33, 0x33, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1, 0x55, 0x55);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x94);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xc1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0xf5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0x88);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8, 0x23, 0x23);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3, 0x33);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb4,
					 0x1c, 0x19, 0x3f, 0x01, 0x64, 0x5c,
					 0x01, 0xa0, 0x5f, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb4, 0x64);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0,
					 0x00, 0x74, 0x00, 0x0a, 0x0a, 0x00,
					 0x74, 0x0a, 0x0a, 0x00, 0x74, 0x00,
					 0x0a, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0,
					 0x00, 0x00, 0x00, 0x02, 0x00, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0,
					 0x00, 0x00, 0x02, 0x00, 0x00, 0x1f,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0,
					 0x00, 0x00, 0x02, 0x00, 0x00, 0x1f,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2,
					 0x83, 0x01, 0x00, 0x00, 0x82, 0x01,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2,
					 0x82, 0x02, 0x00, 0x00, 0x88, 0x81,
					 0x02, 0x00, 0x00, 0x88, 0x00, 0x02,
					 0x00, 0x00, 0x88);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2,
					 0x01, 0x02, 0x00, 0x00, 0x88, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x33, 0x33,
					 0x33, 0x33, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3,
					 0x83, 0x01, 0x00, 0x00, 0x82, 0x01,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3,
					 0x00, 0x00, 0x00, 0x00, 0x82, 0x02,
					 0x00, 0x00, 0x88, 0x81, 0x02, 0x00,
					 0x00, 0x88);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3,
					 0x00, 0x02, 0x00, 0x00, 0x88, 0x01,
					 0x02, 0x00, 0x00, 0x88, 0x00, 0x00,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3,
					 0x33, 0x33, 0x33, 0x33, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb,
					 0x00, 0x00, 0x00, 0x00, 0x30, 0x00,
					 0x03, 0x00, 0x00, 0x00, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb,
					 0x00, 0x00, 0x00, 0xbf, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0xff, 0x00, 0x00,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x77, 0x77);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb,
					 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
					 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
					 0x01, 0x01, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb,
					 0x01, 0x01, 0x01, 0xf3, 0x01, 0x01,
					 0x01, 0x01, 0x00, 0xf3, 0x00, 0x00,
					 0x01, 0x00, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb,
					 0x00, 0x01, 0x00, 0x01, 0x00, 0x01,
					 0x00, 0x01, 0x00, 0x00, 0x77, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb,
					 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
					 0x03, 0x33, 0x03, 0x00, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc,
					 0x08, 0x09, 0x18, 0x19, 0x0c, 0x0d,
					 0x0e, 0x0f, 0x07, 0x07, 0x07, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc,
					 0x09, 0x08, 0x19, 0x18, 0x0f, 0x0e,
					 0x0d, 0x0c, 0x07, 0x07, 0x07, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc,
					 0x14, 0x15, 0x16, 0x17, 0x1c, 0x1d,
					 0x1e, 0x1f, 0x01, 0x04, 0x20, 0x07,
					 0x07, 0x07, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc,
					 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
					 0x07, 0x07, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd,
					 0x1a, 0x01, 0x11, 0x12, 0x1a, 0x05,
					 0x18, 0x07, 0x1a, 0x1a, 0x23, 0x23,
					 0x23, 0x1f, 0x1e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x1d, 0x23, 0x23);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd,
					 0x1a, 0x02, 0x11, 0x12, 0x1a, 0x06,
					 0x18, 0x08, 0x1a, 0x1a, 0x23, 0x23,
					 0x23, 0x1f, 0x1e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x1d, 0x23, 0x23);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa4,
					 0xaf, 0x00, 0x20, 0x04, 0x00, 0x17,
					 0x15, 0x03, 0x60, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa4, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7,
					 0xff, 0x0f, 0x1e, 0x00, 0x20, 0x00,
					 0x01, 0x98, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7,
					 0x1d, 0x20, 0x3d, 0x00, 0x00, 0xbe,
					 0x90, 0xce, 0x05, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7,
					 0x00, 0x1e, 0x1e, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7,
					 0xff, 0x0f, 0x1e, 0x00, 0x1f, 0x00,
					 0x01, 0x18, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7,
					 0xff, 0xff, 0x1c, 0x00, 0x01, 0x7c,
					 0x81, 0x7c, 0x0b, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7,
					 0x00, 0x3c, 0x3c, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7,
					 0xff, 0xff, 0x1d, 0x00, 0x01, 0x7c,
					 0x81, 0x7c, 0x0b, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9,
					 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9,
					 0xff, 0x3a, 0x49, 0x00, 0x1e, 0x00,
					 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x77);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9,
					 0xff, 0xff, 0x1d, 0x00, 0x01, 0x7c,
					 0x81, 0x7c, 0x0b, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9,
					 0x00, 0x3c, 0x3c, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa4,
					 0x05, 0xf0, 0x0b, 0xe0, 0x00, 0x00,
					 0x0b, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xab,
					 0x05, 0x14, 0x00, 0x00, 0xff, 0x6a,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xab,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x07, 0x00, 0x00, 0x00, 0x00,
					 0x01, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xab,
					 0x00, 0x00, 0x21, 0x0a, 0x00, 0x00,
					 0x44, 0x04, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x93);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x87);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x00, 0x00, 0x33, 0x00, 0x33, 0x00,
					 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x90);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00,
					 0x00, 0x00, 0xfc, 0x00, 0xfc, 0x00,
					 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x00, 0x00, 0x40, 0x40, 0x40, 0x00,
					 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
					 0x01, 0x01, 0xf1, 0x01, 0xf1, 0x01,
					 0x01, 0x01, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
					 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
					 0x04, 0x04, 0xf7, 0x04, 0xf7, 0x04,
					 0x04, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x00, 0x00, 0x15, 0x15, 0x15, 0x04,
					 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xf4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce, 0x25);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0xff, 0xff, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2a,
					 0x00, 0x00, 0x04, 0x37);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2b,
					 0x00, 0x00, 0x07, 0x7f);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int boe_test1906c_5p5_off(struct boe_test1906c_5p5 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int boe_test1906c_5p5_prepare(struct drm_panel *panel)
{
	struct boe_test1906c_5p5 *ctx = to_boe_test1906c_5p5(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(boe_test1906c_5p5_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	boe_test1906c_5p5_reset(ctx);

	ret = boe_test1906c_5p5_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(boe_test1906c_5p5_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int boe_test1906c_5p5_unprepare(struct drm_panel *panel)
{
	struct boe_test1906c_5p5 *ctx = to_boe_test1906c_5p5(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = boe_test1906c_5p5_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(boe_test1906c_5p5_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode boe_test1906c_5p5_mode = {
	.clock = (1080 + 45 + 8 + 45) * (1920 + 16 + 4 + 16) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 45,
	.hsync_end = 1080 + 45 + 8,
	.htotal = 1080 + 45 + 8 + 45,
	.vdisplay = 1920,
	.vsync_start = 1920 + 16,
	.vsync_end = 1920 + 16 + 4,
	.vtotal = 1920 + 16 + 4 + 16,
	.width_mm = 68,
	.height_mm = 121,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int boe_test1906c_5p5_get_modes(struct drm_panel *panel,
				       struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &boe_test1906c_5p5_mode);
}

static const struct drm_panel_funcs boe_test1906c_5p5_panel_funcs = {
	.prepare = boe_test1906c_5p5_prepare,
	.unprepare = boe_test1906c_5p5_unprepare,
	.get_modes = boe_test1906c_5p5_get_modes,
};

static int boe_test1906c_5p5_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct boe_test1906c_5p5 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(boe_test1906c_5p5_supplies),
					    boe_test1906c_5p5_supplies,
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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_HSE |
			  MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &boe_test1906c_5p5_panel_funcs,
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

static void boe_test1906c_5p5_remove(struct mipi_dsi_device *dsi)
{
	struct boe_test1906c_5p5 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boe_test1906c_5p5_of_match[] = {
	{ .compatible = "huawei,milan-boe-test1906c" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boe_test1906c_5p5_of_match);

static struct mipi_dsi_driver boe_test1906c_5p5_driver = {
	.probe = boe_test1906c_5p5_probe,
	.remove = boe_test1906c_5p5_remove,
	.driver = {
		.name = "panel-boe-test1906c-5p5",
		.of_match_table = boe_test1906c_5p5_of_match,
	},
};
module_mipi_dsi_driver(boe_test1906c_5p5_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for BOE_TEST1906C_5P5_1080P_CMD");
MODULE_LICENSE("GPL");
