// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Jitao Shi <jitao.shi@mediatek.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct boe_panel;

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int bpc;

	/**
	 * @width_mm: width of the panel's active display area
	 * @height_mm: height of the panel's active display area
	 */
	struct {
		unsigned int width_mm;
		unsigned int height_mm;
	} size;

	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	int (*init)(struct boe_panel *boe);
	unsigned int lanes;
	bool discharge_on_disable;
	bool lp11_before_reset;
};

struct boe_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	const struct panel_desc *desc;

	enum drm_panel_orientation orientation;
	struct regulator *pp3300;
	struct regulator *pp1800;
	struct regulator *avee;
	struct regulator *avdd;
	struct gpio_desc *enable_gpio;
};

static int boe_tv110c9m_init(struct boe_panel *boe)
{
	struct mipi_dsi_multi_context ctx = { .dsi = boe->dsi };

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x05, 0xd9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x07, 0x78);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x08, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0d, 0x63);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0e, 0x91);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0f, 0x73);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x95, 0xe6);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x96, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6d, 0x66);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x75, 0xa2);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x77, 0x3b);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x08, 0x00, 0x23, 0x00, 0x4d, 0x00, 0x6d,
				     0x00, 0x89, 0x00, 0xa1, 0x00, 0xb6, 0x00, 0xc9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x00, 0xda, 0x01, 0x13, 0x01, 0x3c, 0x01, 0x7e,
				     0x01, 0xab, 0x01, 0xf7, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb2, 0x02, 0x67, 0x02, 0xa6, 0x02, 0xd1, 0x03, 0x08,
				     0x03, 0x2e, 0x03, 0x5b, 0x03, 0x6b, 0x03, 0x7b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x03, 0x8e, 0x03, 0xa2, 0x03, 0xb7, 0x03, 0xe7,
				     0x03, 0xfd, 0x03, 0xff);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x00, 0x08, 0x00, 0x23, 0x00, 0x4d, 0x00, 0x6d,
				     0x00, 0x89, 0x00, 0xa1, 0x00, 0xb6, 0x00, 0xc9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5, 0x00, 0xda, 0x01, 0x13, 0x01, 0x3c, 0x01, 0x7e,
				     0x01, 0xab, 0x01, 0xf7, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x02, 0x67, 0x02, 0xa6, 0x02, 0xd1, 0x03, 0x08,
				     0x03, 0x2e, 0x03, 0x5b, 0x03, 0x6b, 0x03, 0x7b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb7, 0x03, 0x8e, 0x03, 0xa2, 0x03, 0xb7, 0x03, 0xe7,
				     0x03, 0xfd, 0x03, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x00, 0x08, 0x00, 0x23, 0x00, 0x4d, 0x00, 0x6d,
				     0x00, 0x89, 0x00, 0xa1, 0x00, 0xb6, 0x00, 0xc9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0x00, 0xda, 0x01, 0x13, 0x01, 0x3c, 0x01, 0x7e,
				     0x01, 0xab, 0x01, 0xf7, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x02, 0x67, 0x02, 0xa6, 0x02, 0xd1, 0x03, 0x08,
				     0x03, 0x2e, 0x03, 0x5b, 0x03, 0x6b, 0x03, 0x7b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbb, 0x03, 0x8e, 0x03, 0xa2, 0x03, 0xb7, 0x03, 0xe7,
				     0x03, 0xfd, 0x03, 0xff);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x21);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x45, 0x00, 0x65,
				     0x00, 0x81, 0x00, 0x99, 0x00, 0xae, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x00, 0xd2, 0x01, 0x0b, 0x01, 0x34, 0x01, 0x76,
				     0x01, 0xa3, 0x01, 0xef, 0x02, 0x27, 0x02, 0x29);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb2, 0x02, 0x5f, 0x02, 0x9e, 0x02, 0xc9, 0x03, 0x00,
				     0x03, 0x26, 0x03, 0x53, 0x03, 0x63, 0x03, 0x73);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x03, 0x86, 0x03, 0x9a, 0x03, 0xaf, 0x03, 0xdf,
				     0x03, 0xf5, 0x03, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x45, 0x00, 0x65,
				     0x00, 0x81, 0x00, 0x99, 0x00, 0xae, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5, 0x00, 0xd2, 0x01, 0x0b, 0x01, 0x34, 0x01, 0x76,
				     0x01, 0xa3, 0x01, 0xef, 0x02, 0x27, 0x02, 0x29);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x02, 0x5f, 0x02, 0x9e, 0x02, 0xc9, 0x03, 0x00,
				     0x03, 0x26, 0x03, 0x53, 0x03, 0x63, 0x03, 0x73);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb7, 0x03, 0x86, 0x03, 0x9a, 0x03, 0xaf, 0x03, 0xdf,
				     0x03, 0xf5, 0x03, 0xe0);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x45, 0x00, 0x65,
				     0x00, 0x81, 0x00, 0x99, 0x00, 0xae, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0x00, 0xd2, 0x01, 0x0b, 0x01, 0x34, 0x01, 0x76,
				     0x01, 0xa3, 0x01, 0xef, 0x02, 0x27, 0x02, 0x29);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x02, 0x5f, 0x02, 0x9e, 0x02, 0xc9, 0x03, 0x00,
				     0x03, 0x26, 0x03, 0x53, 0x03, 0x63, 0x03, 0x73);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbb, 0x03, 0x86, 0x03, 0x9a, 0x03, 0xaf, 0x03, 0xdf,
				     0x03, 0xf5, 0x03, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x24);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x01, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x02, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x03, 0x1c);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x04, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x05, 0x1d);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x07, 0x04);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x08, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x09, 0x0f);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0a, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0b, 0x0e);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0c, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0d, 0x0d);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0e, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0f, 0x0c);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x10, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x11, 0x08);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x12, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x13, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x14, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x15, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x16, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x17, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x18, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x19, 0x1c);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1a, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1b, 0x1d);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1c, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1d, 0x04);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1e, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1f, 0x0f);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x20, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x21, 0x0e);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x22, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x23, 0x0d);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x24, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x25, 0x0c);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x26, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x27, 0x08);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x28, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x29, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2b, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2d, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2f, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x44);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x33, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x34, 0x32);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x37, 0x44);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x38, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x39, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3a, 0x5d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3b, 0x60);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3d, 0x42);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3f, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x43, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x47, 0x66);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4a, 0x5d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4b, 0x60);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4c, 0x91);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4d, 0x21);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4e, 0x43);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x51, 0x12);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x52, 0x34);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x55, 0x82, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x56, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x58, 0x21);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x59, 0x30);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5a, 0x60);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5b, 0x50);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5e, 0x00, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x65, 0x82);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7e, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7f, 0x3c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x82, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x97, 0xc0);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x05, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x05, 0x05, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x91, 0x44);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x92, 0xa9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x93, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x94, 0x96);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd7, 0x55);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xda, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xde, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdb, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdc, 0xa9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdd, 0x22);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdf, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe0, 0xa9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe1, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe2, 0xa9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe3, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe4, 0xa9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe5, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe6, 0xa9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x8d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x8e, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5, 0x90);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x25);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x05, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x19, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1f, 0x60);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x20, 0x50);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x26, 0x60);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x27, 0x50);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x33, 0x60);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x34, 0x50);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3f, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x40, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x44, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x45, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x48, 0x60);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x49, 0x50);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5e, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x61, 0x60);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x62, 0x50);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf1, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x64, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x67, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6a, 0x16);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x70, 0x30);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa2, 0xf3);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa3, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa4, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa5, 0xff);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd6, 0x08);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x26);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x00, 0xa1);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x02, 0x31);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x04, 0x28);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x06, 0x30);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0c, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0d, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x11, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x12, 0x50);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x13, 0x56);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x14, 0x57);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x15, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x16, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x17, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x18, 0x86);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x19, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1a, 0x7f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1b, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1c, 0xbf);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x22, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x23, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2a, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2b, 0x7f);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1e, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1f, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x24, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x25, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2f, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x31, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x32, 0x7d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x39, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3a, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x20, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x33, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x34, 0x78);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x35, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x9e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x4e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa9, 0x49);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xaa, 0x4b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xab, 0x48);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xac, 0x43);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xad, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xae, 0x50);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xaf, 0x44);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x54);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x4e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb2, 0x4d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x4c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x41);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5, 0x47);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x53);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb7, 0x3e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x51);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0x3c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x3b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbb, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbc, 0x45);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x55);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbe, 0x3d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbf, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x52);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x4a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0x39);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0x42);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x56, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x58, 0x80);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x59, 0x75);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5b, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5e, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5f, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x61, 0x2e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x62, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x63, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x64, 0x43);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x65, 0x2d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x66, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x67, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x68, 0x44);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x78, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x22, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x23, 0x08);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x24, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x25, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x26, 0xf8);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x27, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x28, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x29, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2a, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2d, 0x1a);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x23);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x07, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x14, 0x60);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x16, 0xc0);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3a, 0x08);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x18, 0x40);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x35, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x51, 0x00, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x53, 0x24);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x55, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbb, 0x13);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3b, 0x03, 0x96, 0x1a, 0x04, 0x04);

	mipi_dsi_msleep(&ctx, 100);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x11);

	mipi_dsi_msleep(&ctx, 200);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x29);

	mipi_dsi_msleep(&ctx, 100);

	return 0;
};

static int inx_hj110iz_init(struct boe_panel *boe)
{
	struct mipi_dsi_multi_context ctx = { .dsi = boe->dsi };

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x05, 0xd1);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x06, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x07, 0x87);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x08, 0x4b);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0d, 0x63);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0e, 0x91);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0f, 0x69);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x94, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x95, 0xf5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x96, 0xf5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x9d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x9e, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x69, 0x98);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x75, 0xa2);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x77, 0xb3);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x58, 0x43);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x24);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x91, 0x44);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x92, 0x4c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x94, 0x86);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x60, 0x96);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x61, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x63, 0x70);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0xca);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x00, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x01, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x02, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x03, 0x29);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x04, 0x22);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x05, 0x22);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x06, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x07, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x08, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x09, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0a, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0b, 0x09);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0c, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0d, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0e, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0f, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x10, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x11, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x12, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x13, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x14, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x15, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x16, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x17, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x18, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x19, 0x29);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1a, 0x22);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1b, 0x22);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1c, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1d, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1e, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1f, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x20, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x21, 0x09);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x22, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x23, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x24, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x25, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x26, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x27, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x28, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x29, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2b, 0x03);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2f, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x35);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x37, 0xa7);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x39, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3a, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3b, 0x32);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3d, 0x12);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3f, 0x33);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x40, 0x31);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x41, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x42, 0x42);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x47, 0x77);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x48, 0x77);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4a, 0x45);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4b, 0x45);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4c, 0x14);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4d, 0x21);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4e, 0x43);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4f, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x55, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x56, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x58, 0x21);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x59, 0x70);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5a, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5b, 0x32);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5c, 0x88);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5e, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5f, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7a, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7b, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7e, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7f, 0x3c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x80, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x81, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x82, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x97, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0x10);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd7, 0x55);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd8, 0x55);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd9, 0x23);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xda, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdc, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdd, 0x55);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xde, 0x27);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdf, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe0, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe1, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe2, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe3, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe4, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe5, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe6, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe7, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe9, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xea, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xeb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xee, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xef, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf0, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x05, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x05, 0x05, 0x00, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x25);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x05, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf1, 0x10);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1e, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1f, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x20, 0x32);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x25, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x26, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x27, 0x32);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3f, 0x80);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x40, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x43, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x44, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x45, 0x46);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x48, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x49, 0x32);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5b, 0x80);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5d, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5e, 0x32);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5f, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x60, 0x32);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x61, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x62, 0x32);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x68, 0x0c);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6c, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6e, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x78, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x79, 0xc5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7a, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7b, 0xb0);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x26);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x00, 0xa1);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x02, 0x31);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0a, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x04, 0x50);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x06, 0x30);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0c, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0d, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x11, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x12, 0x50);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x13, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x14, 0x58);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x15, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x16, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x17, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x18, 0x86);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x22, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x23, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x19, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1a, 0x31);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1b, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1c, 0x29);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2a, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2b, 0x31);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1e, 0x62);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1f, 0x62);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2f, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x62);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x31, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x32, 0x7f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x33, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x34, 0x89);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x35, 0x67);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x39, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3a, 0x62);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3b, 0x06);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x89);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x4e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa9, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xaa, 0x3e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xab, 0x3d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xac, 0x3c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xad, 0x3b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xae, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xaf, 0x39);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x38);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd0, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd1, 0x54);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xde, 0x43);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdf, 0x02);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x18);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x56, 0x06);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x58, 0x80);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x59, 0x78);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5b, 0x18);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5d, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5e, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5f, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x61, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x62, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x63, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x64, 0x44);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x65, 0x1b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x66, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x67, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x68, 0x44);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x98, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x9b, 0xbe);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xab, 0x14);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbc, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x28);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x22, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x23, 0x08);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x24, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x25, 0x62);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x26, 0xf8);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x27, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x28, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x29, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2a, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2d, 0x1a);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x64, 0x96);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x65, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x66, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x67, 0x96);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x68, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x69, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6a, 0x96);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6b, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x70, 0x92);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x71, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x72, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x79, 0x96);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7a, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x88, 0x96);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x89, 0x10);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa2, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa3, 0x30);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa4, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa5, 0x03);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe8, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x97, 0x3c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x98, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x99, 0x95);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x9a, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x9b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x9c, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x9d, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x9e, 0x90);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x25);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x13, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x14, 0xd7);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdb, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdc, 0xd7);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x17, 0xcf);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x19, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1b, 0x5b);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x20);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x24, 0x00, 0x38,
				     0x00, 0x4c, 0x00, 0x5e, 0x00, 0x6f, 0x00, 0x7e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x00, 0x8c, 0x00, 0xbe, 0x00, 0xe5, 0x01, 0x27,
				     0x01, 0x58, 0x01, 0xa8, 0x01, 0xe8, 0x01, 0xea);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb2, 0x02, 0x28, 0x02, 0x71, 0x02, 0x9e, 0x02, 0xda,
				     0x03, 0x00, 0x03, 0x31, 0x03, 0x40, 0x03, 0x51);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x03, 0x62, 0x03, 0x75, 0x03, 0x89, 0x03, 0x9c,
				     0x03, 0xaa, 0x03, 0xb2);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x27, 0x00, 0x3d,
				     0x00, 0x52, 0x00, 0x64, 0x00, 0x75, 0x00, 0x84);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5, 0x00, 0x93, 0x00, 0xc5, 0x00, 0xec, 0x01, 0x2c,
				     0x01, 0x5d, 0x01, 0xac, 0x01, 0xec, 0x01, 0xee);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x02, 0x2b, 0x02, 0x73, 0x02, 0xa0, 0x02, 0xdb,
				     0x03, 0x01, 0x03, 0x31, 0x03, 0x41, 0x03, 0x51);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb7, 0x03, 0x63, 0x03, 0x75, 0x03, 0x89, 0x03, 0x9c,
				     0x03, 0xaa, 0x03, 0xb2);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x2a, 0x00, 0x40,
				     0x00, 0x56, 0x00, 0x68, 0x00, 0x7a, 0x00, 0x89);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0x00, 0x98, 0x00, 0xc9, 0x00, 0xf1, 0x01, 0x30,
				     0x01, 0x61, 0x01, 0xb0, 0x01, 0xef, 0x01, 0xf1);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x02, 0x2e, 0x02, 0x76, 0x02, 0xa3, 0x02, 0xdd,
				     0x03, 0x02, 0x03, 0x32, 0x03, 0x42, 0x03, 0x53);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbb, 0x03, 0x66, 0x03, 0x75, 0x03, 0x89, 0x03, 0x9c,
				     0x03, 0xaa, 0x03, 0xb2);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x21);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x24, 0x00, 0x38,
				     0x00, 0x4c, 0x00, 0x5e, 0x00, 0x6f, 0x00, 0x7e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x00, 0x8c, 0x00, 0xbe, 0x00, 0xe5, 0x01, 0x27,
				     0x01, 0x58, 0x01, 0xa8, 0x01, 0xe8, 0x01, 0xea);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb2, 0x02, 0x28, 0x02, 0x71, 0x02, 0x9e, 0x02, 0xda,
				     0x03, 0x00, 0x03, 0x31, 0x03, 0x40, 0x03, 0x51);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x03, 0x62, 0x03, 0x77, 0x03, 0x90, 0x03, 0xac,
				     0x03, 0xca, 0x03, 0xda);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x27, 0x00, 0x3d,
				     0x00, 0x52, 0x00, 0x64, 0x00, 0x75, 0x00, 0x84);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5, 0x00, 0x93, 0x00, 0xc5, 0x00, 0xec, 0x01, 0x2c,
				     0x01, 0x5d, 0x01, 0xac, 0x01, 0xec, 0x01, 0xee);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x02, 0x2b, 0x02, 0x73, 0x02, 0xa0, 0x02, 0xdb,
				     0x03, 0x01, 0x03, 0x31, 0x03, 0x41, 0x03, 0x51);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb7, 0x03, 0x63, 0x03, 0x77, 0x03, 0x90, 0x03, 0xac,
				     0x03, 0xca, 0x03, 0xda);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x2a, 0x00, 0x40,
				     0x00, 0x56, 0x00, 0x68, 0x00, 0x7a, 0x00, 0x89);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0x00, 0x98, 0x00, 0xc9, 0x00, 0xf1, 0x01, 0x30,
				     0x01, 0x61, 0x01, 0xb0, 0x01, 0xef, 0x01, 0xf1);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x02, 0x2e, 0x02, 0x76, 0x02, 0xa3, 0x02, 0xdd,
				     0x03, 0x02, 0x03, 0x32, 0x03, 0x42, 0x03, 0x53);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbb, 0x03, 0x66, 0x03, 0x77, 0x03, 0x90, 0x03, 0xac,
				     0x03, 0xca, 0x03, 0xda);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3a, 0x08);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0x01);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x20);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x18, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x10);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xff, 0x10);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x35, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3b, 0x03, 0xae, 0x1a, 0x04, 0x04);

	mipi_dsi_msleep(&ctx, 100);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x11);

	mipi_dsi_msleep(&ctx, 200);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x29);

	mipi_dsi_msleep(&ctx, 100);

	return 0;
};

static int boe_init(struct boe_panel *boe)
{
	struct mipi_dsi_multi_context ctx = { .dsi = boe->dsi };

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0xe5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x52);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x88);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x8b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbf, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe0, 0x26);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe1, 0x26);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdc, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdd, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0x26);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcd, 0x26);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd2, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd3, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe6, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe7, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0x09);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0x09);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd8, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd9, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd6, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd7, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd4, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd5, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xde, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdf, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0x17);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0x26);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0x31);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc6, 0x2c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc7, 0x33);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0x31);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x37);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x37);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x37);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0x39);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcd, 0x2e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xce, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcf, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd0, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd2, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd3, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd4, 0x17);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd5, 0x26);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd6, 0x31);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd7, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd8, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd9, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xda, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdb, 0x37);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdc, 0x37);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdd, 0x37);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xde, 0x39);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdf, 0x2e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe0, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe1, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe2, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe7, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xde, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x43);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe4, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe5, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc7, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd5, 0x32);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbc, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb2, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x25);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5, 0x39);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x4e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb7, 0x72);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x97);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0xdc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x22);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbb, 0xa4);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbc, 0x2b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbe, 0xa9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbf, 0x25);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x61);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x97);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0xb2);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0xcd);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0xd9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0xe7);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc6, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc7, 0xfa);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0xfc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0xaf);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcd, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xce, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb2, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x24);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5, 0x39);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb7, 0x72);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x98);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0xdc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x23);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbb, 0xa6);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbc, 0x2c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x30);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbe, 0xaa);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbf, 0x26);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x62);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x9b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0xb5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0xcf);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0xdb);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0xe8);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc6, 0xf5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc7, 0xfa);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0xfc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0xaf);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcd, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xce, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x09);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb2, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x24);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5, 0x3b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb7, 0x73);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x99);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x26);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbb, 0xad);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbc, 0x36);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbe, 0xae);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbf, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x66);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x9e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0xb8);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0xd1);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0xdd);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0xe9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc6, 0xf6);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc7, 0xfa);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0xfc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0xaf);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcd, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xce, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb2, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x25);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5, 0x39);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x4e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb7, 0x72);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x97);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0xdc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x22);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbb, 0xa4);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbc, 0x2b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbe, 0xa9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbf, 0x25);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x61);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x97);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0xb2);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0xcd);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0xd9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0xe7);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc6, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc7, 0xfa);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0xfc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0xaf);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcd, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xce, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb2, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x24);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5, 0x39);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb7, 0x72);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x98);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0xdc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x23);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbb, 0xa6);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbc, 0x2c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x30);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbe, 0xaa);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbf, 0x26);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x62);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x9b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0xb5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0xcf);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0xdb);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0xe8);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc6, 0xf5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc7, 0xfa);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0xfc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0xaf);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcd, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xce, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb2, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x24);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5, 0x3b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb7, 0x73);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x99);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x26);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbb, 0xad);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbc, 0x36);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbe, 0xae);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbf, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x66);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x9e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0xb8);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0xd1);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0xdd);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0xe9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc6, 0xf6);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc7, 0xfa);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0xfc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0xaf);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcd, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xce, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb3, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb8, 0x68);

	mipi_dsi_msleep(&ctx, 150);

	return 0;
};

static int auo_kd101n80_45na_init(struct boe_panel *boe)
{
	struct mipi_dsi_multi_context ctx = { .dsi = boe->dsi };

	msleep(24);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x11);

	mipi_dsi_msleep(&ctx, 120);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x29);

	mipi_dsi_msleep(&ctx, 120);

	return 0;
};

static int auo_b101uan08_3_init(struct boe_panel *boe)
{
	struct mipi_dsi_multi_context ctx = { .dsi = boe->dsi };

	msleep(24);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x48);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x48);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0x47);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0x47);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc6, 0x45);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc7, 0x45);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0x64);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x64);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcd, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xce, 0x66);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcf, 0x66);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd0, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd1, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd2, 0x41);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd3, 0x41);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd4, 0x48);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd5, 0x48);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd6, 0x47);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd7, 0x47);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd8, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd9, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xda, 0x45);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdb, 0x45);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdc, 0x64);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdd, 0x64);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xde, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdf, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe0, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe1, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe2, 0x66);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe3, 0x66);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe4, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe5, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe6, 0x41);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe7, 0x41);

	mipi_dsi_msleep(&ctx, 150);

	return 0;
};

static int starry_qfh032011_53g_init(struct boe_panel *boe)
{
	struct mipi_dsi_multi_context ctx = { .dsi = boe->dsi };

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc6, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc7, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0x4d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x52);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x51);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcd, 0x5d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xce, 0x5b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcf, 0x4b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd0, 0x49);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd1, 0x47);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd2, 0x45);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd3, 0x41);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd7, 0x50);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd8, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd9, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xda, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdb, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdc, 0x4e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdd, 0x52);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xde, 0x51);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe1, 0x5e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe2, 0x5c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe3, 0x4c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe4, 0x4a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe5, 0x48);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe6, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe7, 0x42);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbe, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0x44);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x42);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcd, 0x3e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcf, 0x60);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd2, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd3, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd4, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd5, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd6, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd7, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd9, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe4, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe5, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0x24);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0x23);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0x29);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc6, 0x23);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc7, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0x19);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0x17);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x17);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x18);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcd, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xce, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcf, 0x23);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd0, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd1, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd2, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd3, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd4, 0x13);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd5, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd6, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd7, 0x13);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd8, 0x17);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd9, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xda, 0x19);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdb, 0x17);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdc, 0x17);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdd, 0x18);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xde, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdf, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe0, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe1, 0x23);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe2, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0X11);

	mipi_dsi_msleep(&ctx, 120);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0X29);

	mipi_dsi_msleep(&ctx, 80);

	return 0;
};

static inline struct boe_panel *to_boe_panel(struct drm_panel *panel)
{
	return container_of(panel, struct boe_panel, base);
}

static int boe_panel_disable(struct drm_panel *panel)
{
	struct boe_panel *boe = to_boe_panel(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = boe->dsi };

	boe->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);

	mipi_dsi_msleep(&ctx, 150);

	return ctx.accum_err;
}

static int boe_panel_unprepare(struct drm_panel *panel)
{
	struct boe_panel *boe = to_boe_panel(panel);

	if (boe->desc->discharge_on_disable) {
		regulator_disable(boe->avee);
		regulator_disable(boe->avdd);
		usleep_range(5000, 7000);
		gpiod_set_value(boe->enable_gpio, 0);
		usleep_range(5000, 7000);
		regulator_disable(boe->pp1800);
		regulator_disable(boe->pp3300);
	} else {
		gpiod_set_value(boe->enable_gpio, 0);
		usleep_range(1000, 2000);
		regulator_disable(boe->avee);
		regulator_disable(boe->avdd);
		usleep_range(5000, 7000);
		regulator_disable(boe->pp1800);
		regulator_disable(boe->pp3300);
	}

	return 0;
}

static int boe_panel_prepare(struct drm_panel *panel)
{
	struct boe_panel *boe = to_boe_panel(panel);
	int ret;

	gpiod_set_value(boe->enable_gpio, 0);
	usleep_range(1000, 1500);

	ret = regulator_enable(boe->pp3300);
	if (ret < 0)
		return ret;

	ret = regulator_enable(boe->pp1800);
	if (ret < 0)
		return ret;

	usleep_range(3000, 5000);

	ret = regulator_enable(boe->avdd);
	if (ret < 0)
		goto poweroff1v8;
	ret = regulator_enable(boe->avee);
	if (ret < 0)
		goto poweroffavdd;

	usleep_range(10000, 11000);

	if (boe->desc->lp11_before_reset) {
		ret = mipi_dsi_dcs_nop(boe->dsi);
		if (ret < 0) {
			dev_err(&boe->dsi->dev, "Failed to send NOP: %d\n", ret);
			goto poweroff;
		}
		usleep_range(1000, 2000);
	}
	gpiod_set_value(boe->enable_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value(boe->enable_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value(boe->enable_gpio, 1);
	usleep_range(6000, 10000);

	ret = boe->desc->init(boe);
	if (ret < 0)
		goto poweroff;

	return 0;

poweroff:
	gpiod_set_value(boe->enable_gpio, 0);
	regulator_disable(boe->avee);
poweroffavdd:
	regulator_disable(boe->avdd);
poweroff1v8:
	usleep_range(5000, 7000);
	regulator_disable(boe->pp1800);

	return ret;
}

static int boe_panel_enable(struct drm_panel *panel)
{
	msleep(130);
	return 0;
}

static const struct drm_display_mode boe_tv110c9m_default_mode = {
	.clock = 166594,
	.hdisplay = 1200,
	.hsync_start = 1200 + 40,
	.hsync_end = 1200 + 40 + 8,
	.htotal = 1200 + 40 + 8 + 28,
	.vdisplay = 2000,
	.vsync_start = 2000 + 26,
	.vsync_end = 2000 + 26 + 2,
	.vtotal = 2000 + 26 + 2 + 148,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct panel_desc boe_tv110c9m_desc = {
	.modes = &boe_tv110c9m_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 143,
		.height_mm = 238,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO
			| MIPI_DSI_MODE_VIDEO_HSE
			| MIPI_DSI_CLOCK_NON_CONTINUOUS
			| MIPI_DSI_MODE_VIDEO_BURST,
	.init = boe_tv110c9m_init,
};

static const struct drm_display_mode inx_hj110iz_default_mode = {
	.clock = 168432,
	.hdisplay = 1200,
	.hsync_start = 1200 + 40,
	.hsync_end = 1200 + 40 + 8,
	.htotal = 1200 + 40 + 8 + 28,
	.vdisplay = 2000,
	.vsync_start = 2000 + 26,
	.vsync_end = 2000 + 26 + 2,
	.vtotal = 2000 + 26 + 2 + 172,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct panel_desc inx_hj110iz_desc = {
	.modes = &inx_hj110iz_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 143,
		.height_mm = 238,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO
			| MIPI_DSI_MODE_VIDEO_HSE
			| MIPI_DSI_CLOCK_NON_CONTINUOUS
			| MIPI_DSI_MODE_VIDEO_BURST,
	.init = inx_hj110iz_init,
};

static const struct drm_display_mode boe_tv101wum_nl6_default_mode = {
	.clock = 159425,
	.hdisplay = 1200,
	.hsync_start = 1200 + 100,
	.hsync_end = 1200 + 100 + 40,
	.htotal = 1200 + 100 + 40 + 24,
	.vdisplay = 1920,
	.vsync_start = 1920 + 10,
	.vsync_end = 1920 + 10 + 14,
	.vtotal = 1920 + 10 + 14 + 4,
};

static const struct panel_desc boe_tv101wum_nl6_desc = {
	.modes = &boe_tv101wum_nl6_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 135,
		.height_mm = 216,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM,
	.init = boe_init,
	.discharge_on_disable = false,
};

static const struct drm_display_mode auo_kd101n80_45na_default_mode = {
	.clock = 157000,
	.hdisplay = 1200,
	.hsync_start = 1200 + 60,
	.hsync_end = 1200 + 60 + 24,
	.htotal = 1200 + 60 + 24 + 56,
	.vdisplay = 1920,
	.vsync_start = 1920 + 16,
	.vsync_end = 1920 + 16 + 4,
	.vtotal = 1920 + 16 + 4 + 16,
};

static const struct panel_desc auo_kd101n80_45na_desc = {
	.modes = &auo_kd101n80_45na_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 135,
		.height_mm = 216,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM,
	.init = auo_kd101n80_45na_init,
	.discharge_on_disable = true,
};

static const struct drm_display_mode boe_tv101wum_n53_default_mode = {
	.clock = 159916,
	.hdisplay = 1200,
	.hsync_start = 1200 + 80,
	.hsync_end = 1200 + 80 + 24,
	.htotal = 1200 + 80 + 24 + 60,
	.vdisplay = 1920,
	.vsync_start = 1920 + 20,
	.vsync_end = 1920 + 20 + 4,
	.vtotal = 1920 + 20 + 4 + 10,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct panel_desc boe_tv101wum_n53_desc = {
	.modes = &boe_tv101wum_n53_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 135,
		.height_mm = 216,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM,
	.init = boe_init,
};

static const struct drm_display_mode auo_b101uan08_3_default_mode = {
	.clock = 159667,
	.hdisplay = 1200,
	.hsync_start = 1200 + 60,
	.hsync_end = 1200 + 60 + 4,
	.htotal = 1200 + 60 + 4 + 80,
	.vdisplay = 1920,
	.vsync_start = 1920 + 34,
	.vsync_end = 1920 + 34 + 2,
	.vtotal = 1920 + 34 + 2 + 24,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct panel_desc auo_b101uan08_3_desc = {
	.modes = &auo_b101uan08_3_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 135,
		.height_mm = 216,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM,
	.init = auo_b101uan08_3_init,
	.lp11_before_reset = true,
};

static const struct drm_display_mode boe_tv105wum_nw0_default_mode = {
	.clock = 159916,
	.hdisplay = 1200,
	.hsync_start = 1200 + 80,
	.hsync_end = 1200 + 80 + 24,
	.htotal = 1200 + 80 + 24 + 60,
	.vdisplay = 1920,
	.vsync_start = 1920 + 20,
	.vsync_end = 1920 + 20 + 4,
	.vtotal = 1920 + 20 + 4 + 10,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct panel_desc boe_tv105wum_nw0_desc = {
	.modes = &boe_tv105wum_nw0_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 141,
		.height_mm = 226,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM,
	.init = boe_init,
	.lp11_before_reset = true,
};

static const struct drm_display_mode starry_qfh032011_53g_default_mode = {
	.clock = 165731,
	.hdisplay = 1200,
	.hsync_start = 1200 + 100,
	.hsync_end = 1200 + 100 + 10,
	.htotal = 1200 + 100 + 10 + 100,
	.vdisplay = 1920,
	.vsync_start = 1920 + 14,
	.vsync_end = 1920 + 14 + 10,
	.vtotal = 1920 + 14 + 10 + 15,
};

static const struct panel_desc starry_qfh032011_53g_desc = {
	.modes = &starry_qfh032011_53g_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 135,
		.height_mm = 216,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM,
	.init = starry_qfh032011_53g_init,
	.lp11_before_reset = true,
};

static int boe_panel_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct boe_panel *boe = to_boe_panel(panel);
	const struct drm_display_mode *m = boe->desc->modes;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, m);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
		return -ENOMEM;
	}

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = boe->desc->size.width_mm;
	connector->display_info.height_mm = boe->desc->size.height_mm;
	connector->display_info.bpc = boe->desc->bpc;
	/*
	 * TODO: Remove once all drm drivers call
	 * drm_connector_set_orientation_from_panel()
	 */
	drm_connector_set_panel_orientation(connector, boe->orientation);

	return 1;
}

static enum drm_panel_orientation boe_panel_get_orientation(struct drm_panel *panel)
{
	struct boe_panel *boe = to_boe_panel(panel);

	return boe->orientation;
}

static const struct drm_panel_funcs boe_panel_funcs = {
	.disable = boe_panel_disable,
	.unprepare = boe_panel_unprepare,
	.prepare = boe_panel_prepare,
	.enable = boe_panel_enable,
	.get_modes = boe_panel_get_modes,
	.get_orientation = boe_panel_get_orientation,
};

static int boe_panel_add(struct boe_panel *boe)
{
	struct device *dev = &boe->dsi->dev;
	int err;

	boe->avdd = devm_regulator_get(dev, "avdd");
	if (IS_ERR(boe->avdd))
		return PTR_ERR(boe->avdd);

	boe->avee = devm_regulator_get(dev, "avee");
	if (IS_ERR(boe->avee))
		return PTR_ERR(boe->avee);

	boe->pp3300 = devm_regulator_get(dev, "pp3300");
	if (IS_ERR(boe->pp3300))
		return PTR_ERR(boe->pp3300);

	boe->pp1800 = devm_regulator_get(dev, "pp1800");
	if (IS_ERR(boe->pp1800))
		return PTR_ERR(boe->pp1800);

	boe->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(boe->enable_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(boe->enable_gpio));
		return PTR_ERR(boe->enable_gpio);
	}

	gpiod_set_value(boe->enable_gpio, 0);

	boe->base.prepare_prev_first = true;

	drm_panel_init(&boe->base, dev, &boe_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	err = of_drm_get_panel_orientation(dev->of_node, &boe->orientation);
	if (err < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, err);
		return err;
	}

	err = drm_panel_of_backlight(&boe->base);
	if (err)
		return err;

	boe->base.funcs = &boe_panel_funcs;
	boe->base.dev = &boe->dsi->dev;

	drm_panel_add(&boe->base);

	return 0;
}

static int boe_panel_probe(struct mipi_dsi_device *dsi)
{
	struct boe_panel *boe;
	int ret;
	const struct panel_desc *desc;

	boe = devm_kzalloc(&dsi->dev, sizeof(*boe), GFP_KERNEL);
	if (!boe)
		return -ENOMEM;

	desc = of_device_get_match_data(&dsi->dev);
	dsi->lanes = desc->lanes;
	dsi->format = desc->format;
	dsi->mode_flags = desc->mode_flags;
	boe->desc = desc;
	boe->dsi = dsi;
	ret = boe_panel_add(boe);
	if (ret < 0)
		return ret;

	mipi_dsi_set_drvdata(dsi, boe);

	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&boe->base);

	return ret;
}

static void boe_panel_remove(struct mipi_dsi_device *dsi)
{
	struct boe_panel *boe = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	if (boe->base.dev)
		drm_panel_remove(&boe->base);
}

static const struct of_device_id boe_of_match[] = {
	{ .compatible = "boe,tv101wum-nl6",
	  .data = &boe_tv101wum_nl6_desc
	},
	{ .compatible = "auo,kd101n80-45na",
	  .data = &auo_kd101n80_45na_desc
	},
	{ .compatible = "boe,tv101wum-n53",
	  .data = &boe_tv101wum_n53_desc
	},
	{ .compatible = "auo,b101uan08.3",
	  .data = &auo_b101uan08_3_desc
	},
	{ .compatible = "boe,tv105wum-nw0",
	  .data = &boe_tv105wum_nw0_desc
	},
	{ .compatible = "boe,tv110c9m-ll3",
	  .data = &boe_tv110c9m_desc
	},
	{ .compatible = "innolux,hj110iz-01a",
	  .data = &inx_hj110iz_desc
	},
	{ .compatible = "starry,2081101qfh032011-53g",
	  .data = &starry_qfh032011_53g_desc
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boe_of_match);

static struct mipi_dsi_driver boe_panel_driver = {
	.driver = {
		.name = "panel-boe-tv101wum-nl6",
		.of_match_table = boe_of_match,
	},
	.probe = boe_panel_probe,
	.remove = boe_panel_remove,
};
module_mipi_dsi_driver(boe_panel_driver);

MODULE_AUTHOR("Jitao Shi <jitao.shi@mediatek.com>");
MODULE_DESCRIPTION("BOE tv101wum-nl6 1200x1920 video mode panel driver");
MODULE_LICENSE("GPL v2");
