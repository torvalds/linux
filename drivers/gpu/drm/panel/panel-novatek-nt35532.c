// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree.
 * Copyright (c) 2026 Cristian Cozzolino <cristian_ci@protonmail.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct novatek_nt35532 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
	const struct nt35532_panel_desc *desc;
};

struct nt35532_panel_desc {
	const struct drm_display_mode *mode;
	int (*on)(struct novatek_nt35532 *ctx);
};

static const struct regulator_bulk_data nt35532_supplies[] = {
	{ .supply = "vci" },
	{ .supply = "vddi" },
	{ .supply = "avee" },
	{ .supply = "avdd" },
};

static inline struct novatek_nt35532 *to_novatek_nt35532(struct drm_panel *panel)
{
	return container_of_const(panel, struct novatek_nt35532, panel);
}

static void nt35532_reset(struct novatek_nt35532 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int rimob_panel_on(struct novatek_nt35532 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6e, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x68, 0x13);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0x01);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0x01);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x05);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd7, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8, 0x7e);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x01, 0x55);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x04, 0x0c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x05, 0x3a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x06, 0x50);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x07, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0a, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0c, 0x06);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0d, 0x6b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0e, 0x6b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0f, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x10, 0x63);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x11, 0x3c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x12, 0x5c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x15, 0x60);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x16, 0x15);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x17, 0x15);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5b, 0xca);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5c, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5d, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5f, 0x1b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x60, 0xd5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x61, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6c, 0xab);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6d, 0x44);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6e, 0x80);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x05);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x01, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x02, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x03, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x04, 0x38);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x05, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x06, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x07, 0x19);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x08, 0x1b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x09, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0a, 0x1d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0b, 0x17);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0c, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0d, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0e, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0f, 0x0c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x10, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x11, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x12, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x13, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x14, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x15, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x16, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x17, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x18, 0x38);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x19, 0x18);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1a, 0x1a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1b, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1c, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1d, 0x1c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1e, 0x16);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1f, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x20, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x21, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x22, 0x06);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x23, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x24, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x25, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x26, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x27, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x54, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x55, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x56, 0x1a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x58, 0x19);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x59, 0x36);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5a, 0x1b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5b, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5c, 0x32);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5e, 0x27);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5f, 0x28);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x60, 0x2b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x61, 0x2c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x62, 0x18);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x63, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x64, 0x32);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x65, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x66, 0x44);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x67, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x68, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x69, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6a, 0x06);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6b, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6c, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6d, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x78, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x79, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7e, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7f, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x80, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x81, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8d, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8e, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0xc0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x90, 0x73);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x91, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x92, 0x09);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x96, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x97, 0x14);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x99, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9a, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9b, 0x61);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9c, 0x15);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9d, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa2, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xaa, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xab, 0xda);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xac, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xae, 0xf4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xaf, 0x40);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x7f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb1, 0x16);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb2, 0x53);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb4, 0x2a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb5, 0x3a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb6, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbc, 0x85);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbe, 0x3b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbf, 0x13);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0x77);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1, 0x77);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2, 0x77);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3, 0x77);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4, 0x77);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x77);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6, 0x77);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7, 0x77);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc9, 0x2a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x92);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce, 0x18);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcf, 0x88);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd0, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd2, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xed, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xee, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xef, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfa, 0x03);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x75, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x76, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x77, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x78, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x79, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7a, 0x49);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7b, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7c, 0x66);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7d, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7e, 0x7d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7f, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x80, 0x91);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x81, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x82, 0xa3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0xb3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x85, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x86, 0xc1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x87, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x88, 0xf1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x89, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8a, 0x18);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8b, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8c, 0x54);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8d, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8e, 0x85);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x90, 0xd1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x91, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x92, 0x0c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x93, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x94, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x95, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x96, 0x43);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x97, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98, 0x7d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x99, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9a, 0xa2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9b, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9c, 0xd7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9d, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9e, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa0, 0x2b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa2, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa3, 0x3a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa4, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa5, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa6, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xaa, 0x73);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xab, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xac, 0x8e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xad, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xae, 0xae);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xaf, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0xc9);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb1, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb2, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb4, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb5, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb6, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb7, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb8, 0x49);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba, 0x66);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbb, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbc, 0x7d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbe, 0x91);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbf, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0xa3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2, 0xb3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4, 0xc1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6, 0xf1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0x18);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc9, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca, 0x54);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x85);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce, 0xd1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcf, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd0, 0x0c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd2, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd4, 0x43);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0x7d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd7, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8, 0xa2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xda, 0xd7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdb, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdc, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdd, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xde, 0x2b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdf, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe0, 0x3a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe1, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe2, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe3, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe4, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe5, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe6, 0x73);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe7, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe8, 0x8e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe9, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xea, 0xae);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xeb, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xec, 0xc9);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xed, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xee, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xef, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf1, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf2, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf3, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf4, 0x49);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf6, 0x66);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf8, 0x7d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfa, 0x91);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x01, 0xa3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x02, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x03, 0xb3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x04, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x05, 0xc1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x06, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x07, 0xf1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x08, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x09, 0x18);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0a, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0b, 0x54);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0c, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0d, 0x85);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0e, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x0f, 0xd1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x10, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x11, 0x0c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x12, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x13, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x14, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x15, 0x43);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x16, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x17, 0x7d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x18, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x19, 0xa2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1a, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1b, 0xd7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1c, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1d, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1e, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x1f, 0x2b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x20, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x21, 0x3a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x22, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x23, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x24, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x25, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x26, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x27, 0x73);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x28, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x29, 0x8e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2a, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2b, 0xae);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2d, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2f, 0xc9);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x30, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x31, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x32, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x33, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x34, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x36, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x37, 0x49);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x38, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x39, 0x66);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3a, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3b, 0x7d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3d, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x3f, 0x91);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x40, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x41, 0xa3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x42, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x43, 0xb3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x44, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x45, 0xc1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x46, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x47, 0xf1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x48, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x49, 0x18);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4a, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4b, 0x54);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4c, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4d, 0x85);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4e, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x4f, 0xd1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x50, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x51, 0x0c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x52, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x53, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x54, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x55, 0x43);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x56, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x58, 0x7d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x59, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5a, 0xa2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5b, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5c, 0xd7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5d, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5e, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5f, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x60, 0x2b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x61, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x62, 0x3a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x63, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x64, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x65, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x66, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x67, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x68, 0x73);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x69, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6a, 0x8e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6b, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6c, 0xae);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6d, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6e, 0xc9);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x70, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x71, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x72, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x73, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x74, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x75, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x76, 0x49);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x77, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x78, 0x66);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x79, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7a, 0x7d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7b, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7c, 0x91);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7d, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7e, 0xa3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x7f, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x80, 0xb3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x81, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x82, 0xc1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0xf1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x85, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x86, 0x18);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x87, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x88, 0x54);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x89, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8a, 0x85);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8b, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8c, 0xd1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8d, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8e, 0x0c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x90, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x91, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x92, 0x43);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x93, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x94, 0x7d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x95, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x96, 0xa2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x97, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98, 0xd7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x99, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9a, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9b, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9c, 0x2b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9d, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9e, 0x3a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa0, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa2, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa3, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa4, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa5, 0x73);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa6, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7, 0x8e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xaa, 0xae);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xab, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xac, 0xc9);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xad, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xae, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xaf, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb1, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb2, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb4, 0x49);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb5, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb6, 0x66);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb7, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb8, 0x7d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba, 0x91);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbb, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbc, 0xa3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbe, 0xb3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbf, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0xc1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2, 0xf1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4, 0x18);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6, 0x54);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0x85);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc9, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca, 0xd1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x0c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcf, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd0, 0x43);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd2, 0x7d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd4, 0xa2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0xd7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd7, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xda, 0x2b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdb, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdc, 0x3a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdd, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xde, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdf, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe0, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe1, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe2, 0x73);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe3, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe4, 0x8e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe5, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe6, 0xae);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe7, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe8, 0xc9);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe9, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xea, 0xcd);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0xee);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x02, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x40, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x02, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x41, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x02, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x42, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0x01);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x36, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd4, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0x48);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd7, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, MIPI_DCS_EXIT_SLEEP_MODE, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_DISPLAY_ON, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 50);

	return dsi_ctx.accum_err;
}

static int nt35532_off(struct novatek_nt35532 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int nt35532_prepare(struct drm_panel *panel)
{
	struct novatek_nt35532 *ctx = to_novatek_nt35532(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(nt35532_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	nt35532_reset(ctx);

	ret = ctx->desc->on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(nt35532_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int nt35532_unprepare(struct drm_panel *panel)
{
	struct novatek_nt35532 *ctx = to_novatek_nt35532(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = nt35532_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(nt35532_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode rimob_panel_mode = {
	.clock = (1080 + 100 + 6 + 94) * (1920 + 15 + 6 + 10) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 100,
	.hsync_end = 1080 + 100 + 6,
	.htotal = 1080 + 100 + 6 + 94,
	.vdisplay = 1920,
	.vsync_start = 1920 + 15,
	.vsync_end = 1920 + 15 + 6,
	.vtotal = 1920 + 15 + 6 + 10,
	.width_mm = 68,
	.height_mm = 121,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int nt35532_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct novatek_nt35532 *ctx = to_novatek_nt35532(panel);
	const struct drm_display_mode *mode = ctx->desc->mode;

	return drm_connector_helper_get_modes_fixed(connector, mode);
}

static const struct nt35532_panel_desc rimob_panel_desc = {
	.mode = &rimob_panel_mode,
	.on = rimob_panel_on,
};

static const struct drm_panel_funcs novatek_nt35532_panel_funcs = {
	.prepare = nt35532_prepare,
	.unprepare = nt35532_unprepare,
	.get_modes = nt35532_get_modes,
};

static int nt35532_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct novatek_nt35532 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct novatek_nt35532, panel,
				   &novatek_nt35532_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->desc = device_get_match_data(dev);

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(nt35532_supplies),
					    nt35532_supplies,
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
			  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_LPM;

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

static void nt35532_remove(struct mipi_dsi_device *dsi)
{
	struct novatek_nt35532 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id nt35532_of_match[] = {
	{ .compatible = "flipkart,rimob-panel-nt35532-cs", .data = &rimob_panel_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt35532_of_match);

static struct mipi_dsi_driver nt35532_driver = {
	.probe = nt35532_probe,
	.remove = nt35532_remove,
	.driver = {
		.name = "panel-novatek-nt35532",
		.of_match_table = nt35532_of_match,
	},
};
module_mipi_dsi_driver(nt35532_driver);

MODULE_DESCRIPTION("DRM driver for Novatek NT35532-based 1080p video mode DSI panel");
MODULE_LICENSE("GPL");
