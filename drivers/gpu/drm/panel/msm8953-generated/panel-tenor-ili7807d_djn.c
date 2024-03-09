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

struct ili7807d_djn_53 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data ili7807d_djn_53_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline
struct ili7807d_djn_53 *to_ili7807d_djn_53(struct drm_panel *panel)
{
	return container_of(panel, struct ili7807d_djn_53, panel);
}

static void ili7807d_djn_53_reset(struct ili7807d_djn_53 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int ili7807d_djn_53_on(struct ili7807d_djn_53 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x42, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x43, 0xa3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x44, 0x9e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x45, 0x18);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x46, 0x16);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4a, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4b, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x50, 0x37);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x51, 0x37);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x57, 0x89);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5a, 0x33);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb4, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x22, 0x06);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x36, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6c, 0x45);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x06);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x41);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x01, 0x14);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x02, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x03, 0x86);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x04, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x05, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x06, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x07, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x08, 0x81);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x09, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0a, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0b, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0c, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0d, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0e, 0x78);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0f, 0x78);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x10, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x11, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x12, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x13, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x14, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x15, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x31, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x32, 0x13);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x33, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x34, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x36, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x37, 0x26);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x38, 0x09);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x39, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3a, 0x23);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3b, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3c, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3d, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3e, 0x2a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3f, 0x29);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x40, 0x28);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x41, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x42, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x43, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x44, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x45, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x46, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x47, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x48, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x49, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4a, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4b, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4c, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4d, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4e, 0x2a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4f, 0x29);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x50, 0x28);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x61, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x62, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x63, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x64, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x65, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x66, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x67, 0x26);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x68, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x69, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6a, 0x23);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6b, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6c, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6d, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6e, 0x2a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x29);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x70, 0x28);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x71, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x72, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x73, 0x13);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x74, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x75, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x76, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x77, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x78, 0x09);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x79, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7a, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7b, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7c, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7d, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7e, 0x2a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7f, 0x29);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x80, 0x28);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x96, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x97, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa0, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa1, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa2, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa3, 0x1c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd0, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xda, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd4, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe5, 0x18);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdb, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdd, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb2, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x01, 0x9b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x02, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x03, 0xa7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x04, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x05, 0xbb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x06, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x07, 0xce);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x08, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x09, 0xde);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0a, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0b, 0xec);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0c, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0d, 0xf9);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0e, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0f, 0x06);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x10, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x11, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x12, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x13, 0x38);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x14, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x15, 0x57);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x16, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x17, 0x8a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x18, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x19, 0xb4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1a, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1b, 0xf6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1c, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1d, 0x2b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1e, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1f, 0x2c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x20, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x21, 0x5c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x22, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x23, 0x90);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x24, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x25, 0xb2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x26, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x27, 0xe2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x28, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x29, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2a, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2b, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2c, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2d, 0x3e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2e, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2f, 0x4d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x30, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x31, 0x5c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x32, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x33, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x34, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x86);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x36, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x37, 0xa7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x38, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x39, 0xd1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3a, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3b, 0xe6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3c, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3d, 0x9b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3e, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3f, 0xa7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x40, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x41, 0xbb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x42, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x43, 0xce);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x44, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x45, 0xde);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x46, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x47, 0xec);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x48, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x49, 0xf9);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4a, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4b, 0x06);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4c, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4d, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4e, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4f, 0x38);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x50, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x51, 0x57);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x52, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x53, 0x8a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x54, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x55, 0xb4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x56, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x57, 0xf6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x58, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x59, 0x2b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5a, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5b, 0x2c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5c, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5d, 0x5c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5e, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5f, 0x90);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x60, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x61, 0xb2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x62, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x63, 0xe2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x64, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x65, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x66, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x67, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x68, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x69, 0x3e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6a, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6b, 0x4d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6c, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6d, 0x5c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6e, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x70, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x71, 0x86);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x72, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x73, 0xa7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x74, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x75, 0xd1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x76, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x77, 0xe6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x78, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x79, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x01, 0xa8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x02, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x03, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x04, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x05, 0xbe);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x06, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x07, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x08, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x09, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0a, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0b, 0xee);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0c, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0d, 0xfb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0e, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0f, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x10, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x11, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x12, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x13, 0x39);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x14, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x15, 0x58);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x16, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x17, 0x8b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x18, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x19, 0xb5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1a, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1b, 0xf6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1c, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1d, 0x2b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1e, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1f, 0x2d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x20, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x21, 0x5c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x22, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x23, 0x8f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x24, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x25, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x26, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x27, 0xdc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x28, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x29, 0xfa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2a, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2b, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2c, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2d, 0x2f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2e, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2f, 0x3d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x30, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x31, 0x4d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x32, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x33, 0x60);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x34, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x75);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x36, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x37, 0x9b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x38, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x39, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3a, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3b, 0xe6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3c, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3d, 0xa8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3e, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3f, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x40, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x41, 0xbe);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x42, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x43, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x44, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x45, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x46, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x47, 0xee);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x48, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x49, 0xfb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4a, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4b, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4c, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4d, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4e, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4f, 0x39);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x50, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x51, 0x58);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x52, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x53, 0x8b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x54, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x55, 0xb5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x56, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x57, 0xf6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x58, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x59, 0x2b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5a, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5b, 0x2d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5c, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5d, 0x5c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5e, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5f, 0x8f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x60, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x61, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x62, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x63, 0xdc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x64, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x65, 0xfa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x66, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x67, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x68, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x69, 0x2f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6a, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6b, 0x3d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6c, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6d, 0x4d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6e, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x60);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x70, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x71, 0x75);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x72, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x73, 0x9b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x74, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x75, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x76, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x77, 0xe6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x78, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x79, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x01, 0x83);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x02, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x03, 0x8f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x04, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x05, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x06, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x07, 0xb7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x08, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x09, 0xc7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0a, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0b, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0c, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0d, 0xe4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0e, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0f, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x10, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x11, 0xfc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x12, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x13, 0x25);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x14, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x15, 0x46);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x16, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x17, 0x7c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x18, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x19, 0xa9);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1a, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1b, 0xee);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1c, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1d, 0x25);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1e, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1f, 0x27);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x20, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x21, 0x59);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x22, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x23, 0x8e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x24, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x25, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x26, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x27, 0xe3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x28, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x29, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2a, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2b, 0x43);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2c, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2d, 0x4f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2e, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2f, 0x5b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x30, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x31, 0x69);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x32, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x33, 0x79);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x34, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x8c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x36, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x37, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x38, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x39, 0xd2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3a, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3b, 0xe6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3c, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3d, 0x83);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3e, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3f, 0x8f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x40, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x41, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x42, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x43, 0xb7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x44, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x45, 0xc7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x46, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x47, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x48, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x49, 0xe4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4a, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4b, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4c, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4d, 0xfc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4e, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4f, 0x25);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x50, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x51, 0x46);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x52, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x53, 0x7c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x54, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x55, 0xa9);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x56, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x57, 0xee);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x58, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x59, 0x25);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5a, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5b, 0x27);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5c, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5d, 0x59);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5e, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5f, 0x8e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x60, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x61, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x62, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x63, 0xe3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x64, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x65, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x66, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x67, 0x43);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x68, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x69, 0x4f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6a, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6b, 0x5b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6c, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6d, 0x69);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6e, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x79);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x70, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x71, 0x8c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x72, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x73, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x74, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x75, 0xd2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x76, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x77, 0xe6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x78, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x79, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce, 0x07);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce, 0x27);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x05);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6, 0xdc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcf, 0x57);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int ili7807d_djn_53_off(struct ili7807d_djn_53 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 32);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int ili7807d_djn_53_prepare(struct drm_panel *panel)
{
	struct ili7807d_djn_53 *ctx = to_ili7807d_djn_53(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ili7807d_djn_53_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ili7807d_djn_53_reset(ctx);

	ret = ili7807d_djn_53_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ili7807d_djn_53_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int ili7807d_djn_53_unprepare(struct drm_panel *panel)
{
	struct ili7807d_djn_53 *ctx = to_ili7807d_djn_53(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = ili7807d_djn_53_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ili7807d_djn_53_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode ili7807d_djn_53_mode = {
	.clock = (1080 + 344 + 16 + 24) * (1920 + 8 + 6 + 6) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 344,
	.hsync_end = 1080 + 344 + 16,
	.htotal = 1080 + 344 + 16 + 24,
	.vdisplay = 1920,
	.vsync_start = 1920 + 8,
	.vsync_end = 1920 + 8 + 6,
	.vtotal = 1920 + 8 + 6 + 6,
	.width_mm = 0,
	.height_mm = 0,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int ili7807d_djn_53_get_modes(struct drm_panel *panel,
				     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &ili7807d_djn_53_mode);
}

static const struct drm_panel_funcs ili7807d_djn_53_panel_funcs = {
	.prepare = ili7807d_djn_53_prepare,
	.unprepare = ili7807d_djn_53_unprepare,
	.get_modes = ili7807d_djn_53_get_modes,
};

static int ili7807d_djn_53_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ili7807d_djn_53 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(ili7807d_djn_53_supplies),
					    ili7807d_djn_53_supplies,
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
			  MIPI_DSI_CLOCK_NON_CONTINUOUS |
			  MIPI_DSI_MODE_VIDEO_NO_HFP | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &ili7807d_djn_53_panel_funcs,
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

static void ili7807d_djn_53_remove(struct mipi_dsi_device *dsi)
{
	struct ili7807d_djn_53 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ili7807d_djn_53_of_match[] = {
	{ .compatible = "tenor,ili7807d_djn" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ili7807d_djn_53_of_match);

static struct mipi_dsi_driver ili7807d_djn_53_driver = {
	.probe = ili7807d_djn_53_probe,
	.remove = ili7807d_djn_53_remove,
	.driver = {
		.name = "panel-ili7807d-djn-53",
		.of_match_table = ili7807d_djn_53_of_match,
	},
};
module_mipi_dsi_driver(ili7807d_djn_53_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for ili7807d djn 53 1080p video mode dsi panel");
MODULE_LICENSE("GPL");
