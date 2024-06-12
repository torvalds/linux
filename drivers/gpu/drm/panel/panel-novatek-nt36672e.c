// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

static const char * const regulator_names[] = {
	"vddi",
	"avdd",
	"avee",
};

static const unsigned long regulator_enable_loads[] = {
	62000,
	100000,
	100000,
};

struct panel_desc {
	const struct drm_display_mode *display_mode;
	u32 width_mm;
	u32 height_mm;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
	const char *panel_name;
	void (*init_sequence)(struct mipi_dsi_multi_context *ctx);
};

struct nt36672e_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[3];
	const struct panel_desc *desc;
};

static inline struct nt36672e_panel *to_nt36672e_panel(struct drm_panel *panel)
{
	return container_of(panel, struct nt36672e_panel, panel);
}

static void nt36672e_1080x2408_60hz_init(struct mipi_dsi_multi_context *ctx)
{
	mipi_dsi_dcs_write_seq_multi(ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xc0, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xc1, 0x89, 0x28, 0x00, 0x08, 0x00, 0xaa, 0x02,
				     0x0e, 0x00, 0x2b, 0x00, 0x07, 0x0d, 0xb7, 0x0c, 0xb7);

	mipi_dsi_dcs_write_seq_multi(ctx, 0xc2, 0x1b, 0xa0);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x01, 0x66);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x06, 0x40);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x07, 0x38);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x2f, 0x83);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x69, 0x91);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x95, 0xd1);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x96, 0xd1);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf2, 0x64);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf3, 0x54);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf4, 0x64);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf5, 0x54);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf6, 0x64);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf7, 0x54);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf8, 0x64);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf9, 0x54);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xff, 0x24);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x01, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x03, 0x0c);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x05, 0x1d);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x08, 0x2f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x09, 0x2e);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x0a, 0x2d);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x0b, 0x2c);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x11, 0x17);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x12, 0x13);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x13, 0x15);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x15, 0x14);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x16, 0x16);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x17, 0x18);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x1b, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x1d, 0x1d);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x20, 0x2f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x21, 0x2e);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x22, 0x2d);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x23, 0x2c);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x29, 0x17);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x2a, 0x13);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x2b, 0x15);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x2f, 0x14);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x30, 0x16);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x31, 0x18);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x32, 0x04);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x34, 0x10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x35, 0x1f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x36, 0x1f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x4d, 0x14);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x4e, 0x36);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x4f, 0x36);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x53, 0x36);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x71, 0x30);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x79, 0x11);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x7a, 0x82);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x7b, 0x8f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x7d, 0x04);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x80, 0x04);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x81, 0x04);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x82, 0x13);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x84, 0x31);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x85, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x86, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x87, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x90, 0x13);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x92, 0x31);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x93, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x94, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x95, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9c, 0xf4);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9d, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xa0, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xa2, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xa3, 0x02);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xa4, 0x04);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xa5, 0x04);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xc6, 0xc0);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xc9, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xd9, 0x80);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xe9, 0x02);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xff, 0x25);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x18, 0x22);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x19, 0xe4);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x21, 0x40);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x66, 0xd8);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x68, 0x50);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x69, 0x10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x6b, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x6d, 0x0d);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x6e, 0x48);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x72, 0x41);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x73, 0x4a);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x74, 0xd0);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x77, 0x62);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x79, 0x7e);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x7d, 0x03);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x7e, 0x15);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x7f, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x84, 0x4d);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xcf, 0x80);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xd6, 0x80);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xd7, 0x80);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xef, 0x20);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0x84);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xff, 0x26);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x81, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x83, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x84, 0x03);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x85, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x86, 0x03);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x87, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x88, 0x05);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x8a, 0x1a);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x8b, 0x11);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x8c, 0x24);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x8e, 0x42);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x8f, 0x11);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x90, 0x11);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x91, 0x11);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9a, 0x80);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9b, 0x04);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9c, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9d, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9e, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x01, 0x68);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x20, 0x81);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x21, 0x6a);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x25, 0x81);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x26, 0x94);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x6e, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x6f, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x70, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x71, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x72, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x75, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x76, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x77, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x7d, 0x09);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x7e, 0x67);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x80, 0x23);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x82, 0x09);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x83, 0x67);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x88, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x89, 0x10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xa5, 0x10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xa6, 0x23);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xa7, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb6, 0x40);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xe5, 0x02);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xe6, 0xd3);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xeb, 0x03);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xec, 0x28);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x00, 0x91);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x03, 0x20);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x07, 0x50);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x0a, 0x70);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x0c, 0x04);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x0d, 0x40);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x0f, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x11, 0xe0);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x15, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x16, 0xa4);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x19, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x1a, 0x78);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x1b, 0x23);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x1d, 0x36);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x1e, 0x3e);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x1f, 0x3e);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x20, 0x3e);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x28, 0xfd);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x29, 0x12);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x2a, 0xe1);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x2d, 0x0a);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x30, 0x49);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x33, 0x96);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x34, 0xff);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x35, 0x40);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x36, 0xde);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x37, 0xf9);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x38, 0x45);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x39, 0xd9);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x3a, 0x49);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x4a, 0xf0);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x7a, 0x09);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x7b, 0x40);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x7f, 0xf0);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x83, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x84, 0xa4);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x87, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x88, 0x78);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x89, 0x23);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x8b, 0x36);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x8c, 0x7d);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x8d, 0x7d);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x8e, 0x7d);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x00, 0x00, 0x00, 0x17, 0x00, 0x49, 0x00,
				     0x6a, 0x00, 0x89, 0x00, 0x9f, 0x00, 0xb6, 0x00, 0xc8);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb1, 0x00, 0xd9, 0x01, 0x10, 0x01, 0x3a, 0x01,
				     0x7a, 0x01, 0xa9, 0x01, 0xf2, 0x02, 0x2d, 0x02, 0x2e);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb2, 0x02, 0x64, 0x02, 0xa3, 0x02, 0xca, 0x03,
				     0x00, 0x03, 0x1e, 0x03, 0x4a, 0x03, 0x59, 0x03, 0x6a);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb3, 0x03, 0x7d, 0x03, 0x93, 0x03, 0xab, 0x03,
				     0xc8, 0x03, 0xec, 0x03, 0xfe, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb4, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x51, 0x00,
				     0x71, 0x00, 0x90, 0x00, 0xa7, 0x00, 0xbf, 0x00, 0xd1);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb5, 0x00, 0xe2, 0x01, 0x1a, 0x01, 0x43, 0x01,
				     0x83, 0x01, 0xb2, 0x01, 0xfa, 0x02, 0x34, 0x02, 0x36);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb6, 0x02, 0x6b, 0x02, 0xa8, 0x02, 0xd0, 0x03,
				     0x03, 0x03, 0x21, 0x03, 0x4d, 0x03, 0x5b, 0x03, 0x6b);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb7, 0x03, 0x7e, 0x03, 0x94, 0x03, 0xac, 0x03,
				     0xc8, 0x03, 0xec, 0x03, 0xfe, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb8, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x51, 0x00,
				     0x72, 0x00, 0x92, 0x00, 0xa8, 0x00, 0xbf, 0x00, 0xd1);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb9, 0x00, 0xe2, 0x01, 0x18, 0x01, 0x42, 0x01,
				     0x81, 0x01, 0xaf, 0x01, 0xf5, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xba, 0x02, 0x68, 0x02, 0xa6, 0x02, 0xcd, 0x03,
				     0x01, 0x03, 0x1f, 0x03, 0x4a, 0x03, 0x59, 0x03, 0x6a);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xbb, 0x03, 0x7d, 0x03, 0x93, 0x03, 0xab, 0x03,
				     0xc8, 0x03, 0xec, 0x03, 0xfe, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xff, 0x21);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x00, 0x00, 0x00, 0x17, 0x00, 0x49, 0x00,
				     0x6a, 0x00, 0x89, 0x00, 0x9f, 0x00, 0xb6, 0x00, 0xc8);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb1, 0x00, 0xd9, 0x01, 0x10, 0x01, 0x3a, 0x01,
				     0x7a, 0x01, 0xa9, 0x01, 0xf2, 0x02, 0x2d, 0x02, 0x2e);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb2, 0x02, 0x64, 0x02, 0xa3, 0x02, 0xca, 0x03,
				     0x00, 0x03, 0x1e, 0x03, 0x4a, 0x03, 0x59, 0x03, 0x6a);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb3, 0x03, 0x7d, 0x03, 0x93, 0x03, 0xab, 0x03,
				     0xc8, 0x03, 0xec, 0x03, 0xfe, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb4, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x51, 0x00,
				     0x71, 0x00, 0x90, 0x00, 0xa7, 0x00, 0xbf, 0x00, 0xd1);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb5, 0x00, 0xe2, 0x01, 0x1a, 0x01, 0x43, 0x01,
				     0x83, 0x01, 0xb2, 0x01, 0xfa, 0x02, 0x34, 0x02, 0x36);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb6, 0x02, 0x6b, 0x02, 0xa8, 0x02, 0xd0, 0x03,
				     0x03, 0x03, 0x21, 0x03, 0x4d, 0x03, 0x5b, 0x03, 0x6b);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb7, 0x03, 0x7e, 0x03, 0x94, 0x03, 0xac, 0x03,
				     0xc8, 0x03, 0xec, 0x03, 0xfe, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb8, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x51, 0x00,
				     0x72, 0x00, 0x92, 0x00, 0xa8, 0x00, 0xbf, 0x00, 0xd1);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb9, 0x00, 0xe2, 0x01, 0x18, 0x01, 0x42, 0x01,
				     0x81, 0x01, 0xaf, 0x01, 0xf5, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xba, 0x02, 0x68, 0x02, 0xa6, 0x02, 0xcd, 0x03,
				     0x01, 0x03, 0x1f, 0x03, 0x4a, 0x03, 0x59, 0x03, 0x6a);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xbb, 0x03, 0x7d, 0x03, 0x93, 0x03, 0xab, 0x03,
				     0xc8, 0x03, 0xec, 0x03, 0xfe, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xff, 0x2c);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x61, 0x1f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x62, 0x1f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x7e, 0x03);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x6a, 0x14);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x6b, 0x36);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x6c, 0x36);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x6d, 0x36);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x53, 0x04);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x54, 0x04);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x55, 0x04);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x56, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x58, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x59, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xff, 0xf0);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x5a, 0x00);

	mipi_dsi_dcs_write_seq_multi(ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x51, 0xff);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x53, 0x24);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x55, 0x01);
}

static int nt36672e_power_on(struct nt36672e_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(&dsi->dev, "regulator bulk enable failed: %d\n", ret);
		return ret;
	}

	/*
	 * Reset sequence of nt36672e panel requires the panel to be out of reset
	 * for 10ms, followed by being held in reset for 10ms and then out again.
	 */
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10000, 20000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10000, 20000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10000, 20000);

	return 0;
}

static int nt36672e_power_off(struct nt36672e_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	int ret = 0;

	gpiod_set_value(ctx->reset_gpio, 0);

	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret)
		dev_err(&dsi->dev, "regulator bulk disable failed: %d\n", ret);

	return ret;
}

static int nt36672e_on(struct nt36672e_panel *nt36672e)
{
	struct mipi_dsi_multi_context ctx = { .dsi = nt36672e->dsi };
	const struct panel_desc *desc = nt36672e->desc;

	nt36672e->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	if (desc->init_sequence)
		desc->init_sequence(&ctx);

	mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&ctx);

	mipi_dsi_msleep(&ctx, 100);

	return ctx.accum_err;
}

static int nt36672e_off(struct nt36672e_panel *panel)
{
	struct mipi_dsi_multi_context ctx = { .dsi = panel->dsi };

	panel->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_msleep(&ctx, 20);

	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 60);

	return ctx.accum_err;
}

static int nt36672e_panel_prepare(struct drm_panel *panel)
{
	struct nt36672e_panel *ctx = to_nt36672e_panel(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	int ret;

	ret = nt36672e_power_on(ctx);
	if (ret < 0)
		return ret;

	ret = nt36672e_on(ctx);
	if (ret < 0) {
		if (nt36672e_power_off(ctx))
			dev_err(&dsi->dev, "power off failed\n");
		return ret;
	}

	return 0;
}

static int nt36672e_panel_unprepare(struct drm_panel *panel)
{
	struct nt36672e_panel *ctx = to_nt36672e_panel(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	int ret;

	nt36672e_off(ctx);

	ret = nt36672e_power_off(ctx);
	if (ret < 0)
		dev_err(&dsi->dev, "power off failed: %d\n", ret);

	return 0;
}

static const struct drm_display_mode nt36672e_1080x2408_60hz = {
	.name = "1080x2408",
	.clock = 181690,
	.hdisplay = 1080,
	.hsync_start = 1080 + 76,
	.hsync_end = 1080 + 76 + 12,
	.htotal = 1080 + 76 + 12 + 56,
	.vdisplay = 2408,
	.vsync_start = 2408 + 46,
	.vsync_end = 2408 + 46 + 10,
	.vtotal = 2408 + 46 + 10 + 10,
	.flags = 0,
};

static const struct panel_desc nt36672e_panel_desc = {
	.display_mode = &nt36672e_1080x2408_60hz,
	.width_mm = 74,
	.height_mm = 131,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
	.panel_name = "nt36672e fhd plus panel",
	.init_sequence = nt36672e_1080x2408_60hz_init,
};

static int nt36672e_panel_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct nt36672e_panel *ctx = to_nt36672e_panel(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->desc->display_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = ctx->desc->width_mm;
	connector->display_info.height_mm = ctx->desc->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs nt36672e_drm_funcs = {
	.prepare = nt36672e_panel_prepare,
	.unprepare = nt36672e_panel_unprepare,
	.get_modes = nt36672e_panel_get_modes,
};

static int nt36672e_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt36672e_panel *ctx;
	int i, ret = 0;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->desc = of_device_get_match_data(dev);
	if (!ctx->desc) {
		dev_err(dev, "missing device configuration\n");
		return -ENODEV;
	}

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++) {
		ctx->supplies[i].supply = regulator_names[i];
		ctx->supplies[i].init_load_uA = regulator_enable_loads[i];
	}

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
			ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio), "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = ctx->desc->lanes;
	dsi->format = ctx->desc->format;
	dsi->mode_flags = ctx->desc->mode_flags;

	drm_panel_init(&ctx->panel, dev, &nt36672e_drm_funcs, DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	ctx->panel.prepare_prev_first = true;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		goto err_dsi_attach;
	}

	return 0;

err_dsi_attach:
	drm_panel_remove(&ctx->panel);
	return ret;
}

static void nt36672e_panel_remove(struct mipi_dsi_device *dsi)
{
	struct nt36672e_panel *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(ctx->dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id nt36672e_of_match[] = {
	{
		.compatible = "novatek,nt36672e",
		.data = &nt36672e_panel_desc,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, nt36672e_of_match);

static struct mipi_dsi_driver nt36672e_panel_driver = {
	.driver = {
		.name = "panel-novatek-nt36672e",
		.of_match_table = nt36672e_of_match,
	},
	.probe = nt36672e_panel_probe,
	.remove = nt36672e_panel_remove,
};
module_mipi_dsi_driver(nt36672e_panel_driver);

MODULE_AUTHOR("Ritesh Kumar <quic_riteshk@quicinc.com>");
MODULE_DESCRIPTION("Novatek NT36672E DSI Panel Driver");
MODULE_LICENSE("GPL");
