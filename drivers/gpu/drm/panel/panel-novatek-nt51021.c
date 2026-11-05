// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026, Ryan Brue <ryanbrue.dev@gmail.com>
// Copyright (c) 2025, Nickolay Goppen <setotau@mainlining.org>
// Copyright (c) 2024, Alexey Minnekhanov <alexeymin@minlexx.ru>

/*
 * This panel IC doesn't use the set_display_on command although
 * it uses set_display_off command. This is not an error.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct novatek_nt51021_desc;

struct novatek_nt51021_variant {
	int (*init)(struct novatek_nt51021_desc *ctx);
	int (*off)(struct novatek_nt51021_desc *ctx);

	const struct drm_display_mode *display_mode;
	unsigned long mode_flags;
};

struct novatek_nt51021_desc {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;

	const struct novatek_nt51021_variant *variant;
};

static const struct regulator_bulk_data nt51021_novatek_supplies[] = {
	{ .supply = "vdd" },
	{ .supply = "avdd" },
	{ .supply = "havdd" },
};

static inline struct novatek_nt51021_desc *to_novatek_panel(struct drm_panel *panel)
{
	return container_of_const(panel, struct novatek_nt51021_desc, panel);
}

static void nt51021_novatek_reset(struct novatek_nt51021_desc *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static void nt51021_cabc_on(struct mipi_dsi_multi_context *dsi_ctx)
{
	if (dsi_ctx->accum_err)
		return;

	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x8f, 0x00);
	mipi_dsi_usleep_range(dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x8f, 0xa5);
	mipi_dsi_usleep_range(dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x83, 0xbb);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x84, 0x22);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x90, 0x40);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x8f, 0x00);
	mipi_dsi_usleep_range(dsi_ctx, 1000, 2000);
}

static void nt51021_cabc_off(struct mipi_dsi_multi_context *dsi_ctx)
{
	if (dsi_ctx->accum_err)
		return;

	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x8f, 0xa5);
	mipi_dsi_usleep_range(dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x83, 0xbb);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x84, 0x22);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x90, 0xc0);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x8f, 0x00);
	mipi_dsi_usleep_range(dsi_ctx, 1000, 2000);
}

static int boe_tv080wum_nx2_init(struct novatek_nt51021_desc *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0xa5);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x01, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0xa5);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8c, 0x8e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x6c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0x8b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8b, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x97, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfa, 0x15);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfd, 0x1b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x85, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x86, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98, 0xc1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0x05);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1, 0x0c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3, 0x34);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4, 0x44);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7, 0x66);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb, 0x3b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x53);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x69);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce, 0x6a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcf, 0x6c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd0, 0x6b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1, 0x75);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd2, 0x83);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3, 0x99);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd4, 0x9a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd7, 0xdb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8, 0xe1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0xe7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xda, 0xed);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdb, 0xf4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdc, 0xfb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdd, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xde, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdf, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe0, 0x15);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe1, 0x1c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe2, 0x32);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe3, 0x44);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe4, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe5, 0x60);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe6, 0x6b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe7, 0x74);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe8, 0x7e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe9, 0x14);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xea, 0x25);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xeb, 0x4d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xec, 0x63);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xed, 0x71);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xee, 0x6a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xef, 0x6c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x6b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf1, 0x75);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf2, 0x83);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf3, 0x99);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf4, 0x9a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf6, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0xdb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf8, 0xe1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf9, 0xe7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfa, 0xed);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0xf4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0xfb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfd, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfe, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0xbb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa1, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa2, 0xfe);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa3, 0xfa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa4, 0xf7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa5, 0xf3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa6, 0xf1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7, 0xed);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa8, 0xeb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0xe9);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xaa, 0xe6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xaf, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x35);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb1, 0x89);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb2, 0x99);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3, 0x99);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb4, 0x0d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb5, 0x1a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb6, 0x16);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9a, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9b, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x96, 0xe6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x99, 0x06);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0x05);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1, 0x0c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3, 0x34);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4, 0x44);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7, 0x66);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb, 0x3b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x53);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x69);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce, 0x6a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcf, 0x6c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd0, 0x6b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1, 0x75);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd2, 0x83);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3, 0x99);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd4, 0x9a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd7, 0xdb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8, 0xe1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0xe7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xda, 0xed);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdb, 0xf4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdc, 0xfb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdd, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xde, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdf, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe0, 0x15);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe1, 0x1c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe2, 0x32);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe3, 0x44);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe4, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe5, 0x60);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe6, 0x6b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe7, 0x74);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe8, 0x7e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe9, 0x14);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xea, 0x25);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xeb, 0x4d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xec, 0x63);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xed, 0x71);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xee, 0x6a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xef, 0x6c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x6b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf1, 0x75);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf2, 0x83);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf3, 0x99);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf4, 0x9a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf6, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0xdb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf8, 0xe1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf9, 0xe7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfa, 0xed);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0xf4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0xfb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfd, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfe, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0xcc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x33);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0x05);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1, 0x0c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3, 0x34);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4, 0x44);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7, 0x66);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc9, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb, 0x3b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x53);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x69);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce, 0x6a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcf, 0x6c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd0, 0x6b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1, 0x75);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd2, 0x83);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3, 0x99);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd4, 0x9a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd7, 0xdb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8, 0xe1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0xe7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xda, 0xed);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdb, 0xf4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdc, 0xfb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdd, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xde, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdf, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe0, 0x15);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe1, 0x1c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe2, 0x32);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe3, 0x44);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe4, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe5, 0x60);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe6, 0x6b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe7, 0x74);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe8, 0x7e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe9, 0x14);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xea, 0x25);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xeb, 0x4d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xec, 0x63);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xed, 0x71);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xee, 0x6a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xef, 0x6c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x6b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf1, 0x75);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf2, 0x83);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf3, 0x99);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf4, 0x9a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf6, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0xdb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf8, 0xe1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf9, 0xe7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfa, 0xed);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0xf4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0xfb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfd, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfe, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0xbb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x94, 0xba);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 5000, 6000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);

	nt51021_cabc_on(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int boe_tv101wum_nx0_init(struct novatek_nt51021_desc *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0xa5);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x01, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0xa5);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8c, 0x8e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfa, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfd, 0x1b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x6c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0xfc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x97, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8b, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x85, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x86, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98, 0xc1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9c, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2, 0x1f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6, 0x55);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0x68);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc9, 0xe3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca, 0xea);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcf, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd0, 0x09);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1, 0x1d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd2, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd4, 0x5b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5, 0xb6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0xbd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd7, 0xc5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xda, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdb, 0xeb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdc, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdd, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xde, 0xfc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdf, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe0, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe1, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe2, 0x1f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe3, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe4, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe5, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe6, 0x55);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe7, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe8, 0x68);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe9, 0xe3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xea, 0xea);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xeb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xec, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xed, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xee, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xef, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x09);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf1, 0x1d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf2, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf3, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf4, 0x5b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0xb6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf6, 0xbd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0xc5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf8, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf9, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfa, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0xeb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfd, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfe, 0xfc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0xbb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa1, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa2, 0xfe);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa3, 0xfa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa4, 0xf7);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa5, 0xf3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa6, 0xf1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa7, 0xed);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa8, 0xeb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0xe9);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xaa, 0xe6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xaf, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb1, 0x23);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb2, 0x34);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3, 0x77);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb4, 0x0d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb5, 0x1a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb6, 0x16);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9a, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9b, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x96, 0xe6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x99, 0x06);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2, 0x1f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6, 0x55);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0x68);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc9, 0xe3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca, 0xea);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcf, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd0, 0x09);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1, 0x1d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd2, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd4, 0x5b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5, 0xb6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0xbd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd7, 0xc5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xda, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdb, 0xeb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdc, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdd, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xde, 0xfc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdf, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe0, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe1, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe2, 0x1f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe3, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe4, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe5, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe6, 0x55);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe7, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe8, 0x68);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe9, 0xe3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xea, 0xea);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xeb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xec, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xed, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xee, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xef, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x09);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf1, 0x1d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf2, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf3, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf4, 0x5b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0xb6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf6, 0xbd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0xc5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf8, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf9, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfa, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0xeb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfd, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfe, 0xfc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0xcc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x33);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc2, 0x1f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6, 0x55);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0x68);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc9, 0xe3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca, 0xea);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcf, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd0, 0x09);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1, 0x1d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd2, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd4, 0x5b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5, 0xb6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0xbd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd7, 0xc5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd9, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xda, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdb, 0xeb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdc, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdd, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xde, 0xfc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xdf, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe0, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe1, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe2, 0x1f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe3, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe4, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe5, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe6, 0x55);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe7, 0x5d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe8, 0x68);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe9, 0xe3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xea, 0xea);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xeb, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xec, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xed, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xee, 0x07);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xef, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x09);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf1, 0x1d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf2, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf3, 0x52);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf4, 0x5b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0xb6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf6, 0xbd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0xc5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf8, 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf9, 0xd6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfa, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0xeb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfd, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfe, 0xfc);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0xff);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 5000, 6000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);

	nt51021_cabc_on(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);

	return dsi_ctx.accum_err;
}

static int boe_tv101wum_nm0_init(struct novatek_nt51021_desc *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0xa5);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x01, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0xa5);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8c, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x6c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0x8b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x97, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8b, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x83, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x84, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa9, 0x4b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x85, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x86, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9c, 0x10);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 5);

	return dsi_ctx.accum_err;
}

static int nt51021_novatek_with_cabc_off(struct novatek_nt51021_desc *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	nt51021_cabc_off(&dsi_ctx);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0xa5);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 130);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 4000, 5000);

	return dsi_ctx.accum_err;
}

static int nt51021_novatek_off(struct novatek_nt51021_desc *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0xa5);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 130);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x8f, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 4000, 5000);

	return dsi_ctx.accum_err;
}

static int nt51021_novatek_prepare(struct drm_panel *panel)
{
	struct novatek_nt51021_desc *ctx = to_novatek_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(nt51021_novatek_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}
	nt51021_novatek_reset(ctx);

	ret = ctx->variant->init(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(nt51021_novatek_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int nt51021_novatek_unprepare(struct drm_panel *panel)
{
	struct novatek_nt51021_desc *ctx = to_novatek_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = ctx->variant->off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(nt51021_novatek_supplies), ctx->supplies);

	return 0;
}

static int nt51021_novatek_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	struct novatek_nt51021_desc *ctx = to_novatek_panel(panel);

	return drm_connector_helper_get_modes_fixed(connector,
						    ctx->variant->display_mode);
}

static const struct drm_panel_funcs nt51021_novatek_panel_funcs = {
	.prepare = nt51021_novatek_prepare,
	.unprepare = nt51021_novatek_unprepare,
	.get_modes = nt51021_novatek_get_modes,
};

static int nt51021_novatek_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct novatek_nt51021_desc *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct novatek_nt51021_desc, panel,
				   &nt51021_novatek_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->variant = of_device_get_match_data(dev);
	if (!ctx->variant)
		return dev_err_probe(dev, -ENODEV, "No match data\n");

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(nt51021_novatek_supplies),
					    nt51021_novatek_supplies,
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
	dsi->mode_flags = ctx->variant->mode_flags;

	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	ret = devm_drm_panel_add(dev, &ctx->panel);
	if (ret)
		return ret;

	return devm_mipi_dsi_attach(dev, dsi);
}

static const struct drm_display_mode boe_tv080wum_nx2_mode = {
	.clock = (1200 + 84 + 4 + 60) * (1920 + 36 + 1 + 25) * 60 / 1000,
	.hdisplay = 1200,
	.hsync_start = 1200 + 84,
	.hsync_end = 1200 + 84 + 4,
	.htotal = 1200 + 84 + 4 + 60,
	.vdisplay = 1920,
	.vsync_start = 1920 + 36,
	.vsync_end = 1920 + 36 + 1,
	.vtotal = 1920 + 36 + 1 + 25,
	.width_mm = 107,
	.height_mm = 172,
	.type = DRM_MODE_TYPE_DRIVER,
};

static const struct drm_display_mode boe_tv101wum_nm0_mode = {
	.clock = 163333,
	.hdisplay = 1200,
	.hsync_start = 1200 + 110,
	.hsync_end = 1200 + 110 + 1,
	.htotal = 1200 + 110 + 1 + 32,
	.vdisplay = 1920,
	.vsync_start = 1920 + 11,
	.vsync_end = 1920 + 11 + 1,
	.vtotal = 1920 + 11 + 1 + 14,
	.width_mm = 135,
	.height_mm = 216,
	.type = DRM_MODE_TYPE_DRIVER,
};

static const struct drm_display_mode boe_tv101wum_nx0_mode = {
	.clock = (1200 + 172 + 4 + 32) * (1920 + 25 + 1 + 14) * 60 / 1000,
	.hdisplay = 1200,
	.hsync_start = 1200 + 172,
	.hsync_end = 1200 + 172 + 4,
	.htotal = 1200 + 172 + 4 + 32,
	.vdisplay = 1920,
	.vsync_start = 1920 + 25,
	.vsync_end = 1920 + 25 + 1,
	.vtotal = 1920 + 25 + 1 + 14,
	.width_mm = 135,
	.height_mm = 216,
	.type = DRM_MODE_TYPE_DRIVER,
};

#define NT51021_BURST_MODE_FLAGS	(MIPI_DSI_MODE_VIDEO | \
					 MIPI_DSI_MODE_VIDEO_BURST | \
					 MIPI_DSI_MODE_NO_EOT_PACKET | \
					 MIPI_DSI_MODE_LPM)

static const struct novatek_nt51021_variant boe_tv080wum_nx2_data = {
	.init = boe_tv080wum_nx2_init,
	.off = nt51021_novatek_with_cabc_off,
	.display_mode = &boe_tv080wum_nx2_mode,
	.mode_flags = NT51021_BURST_MODE_FLAGS,
};

static const struct novatek_nt51021_variant boe_tv101wum_nm0_data = {
	.init = boe_tv101wum_nm0_init,
	.off = nt51021_novatek_off,
	.display_mode = &boe_tv101wum_nm0_mode,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM,
};

static const struct novatek_nt51021_variant boe_tv101wum_nx0_data = {
	.init = boe_tv101wum_nx0_init,
	.off = nt51021_novatek_with_cabc_off,
	.display_mode = &boe_tv101wum_nx0_mode,
	.mode_flags = NT51021_BURST_MODE_FLAGS,
};

static const struct of_device_id nt51021_novatek_of_match[] = {
	{ .compatible = "boe,tv080wum-nx2", .data = &boe_tv080wum_nx2_data },
	{ .compatible = "boe,tv101wum-nm0", .data = &boe_tv101wum_nm0_data },
	{ .compatible = "boe,tv101wum-nx0", .data = &boe_tv101wum_nx0_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt51021_novatek_of_match);

static struct mipi_dsi_driver nt51021_boe_driver = {
	.probe = nt51021_novatek_probe,
	.driver = {
		.name = "panel-novatek-nt51021",
		.of_match_table = nt51021_novatek_of_match,
	},
};
module_mipi_dsi_driver(nt51021_boe_driver);

MODULE_AUTHOR("Nickolay Goppen <setotau@mainlining.org>");
MODULE_AUTHOR("Alexey Minnekhanov <alexeymin@minlexx.ru>");
MODULE_AUTHOR("Ryan Brue <ryanbrue.dev@gmail.com>");
MODULE_DESCRIPTION("DRM driver for Novatek NT51021-based DSI panels");
MODULE_LICENSE("GPL");
