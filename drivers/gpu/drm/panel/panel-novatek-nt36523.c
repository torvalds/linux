// SPDX-License-Identifier: GPL-2.0-only
/*
 * Novatek NT36523 DriverIC panels driver
 *
 * Copyright (c) 2022, 2023 Jianhua Lu <lujianhua000@gmail.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define DSI_NUM_MIN 1

#define mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, cmd, seq...)        \
		do {                                                 \
			mipi_dsi_dcs_write_seq(dsi0, cmd, seq);      \
			mipi_dsi_dcs_write_seq(dsi1, cmd, seq);      \
		} while (0)

struct panel_info {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
	const struct panel_desc *desc;
	enum drm_panel_orientation orientation;

	struct gpio_desc *reset_gpio;
	struct backlight_device *backlight;
	struct regulator *vddio;

	bool prepared;
};

struct panel_desc {
	unsigned int width_mm;
	unsigned int height_mm;

	unsigned int bpc;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;

	const struct drm_display_mode *modes;
	unsigned int num_modes;
	const struct mipi_dsi_device_info dsi_info;
	int (*init_sequence)(struct panel_info *pinfo);

	bool is_dual_dsi;
	bool has_dcs_backlight;
};

static inline struct panel_info *to_panel_info(struct drm_panel *panel)
{
	return container_of(panel, struct panel_info, panel);
}

static int elish_boe_init_sequence(struct panel_info *pinfo)
{
	struct mipi_dsi_device *dsi0 = pinfo->dsi[0];
	struct mipi_dsi_device *dsi1 = pinfo->dsi[1];
	/* No datasheet, so write magic init sequence directly */
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xb9, 0x05);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x20);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x18, 0x40);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xb9, 0x02);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x23);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x00, 0x80);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x01, 0x84);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x05, 0x2d);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x06, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x07, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x08, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x09, 0x45);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x11, 0x02);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x12, 0x80);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x15, 0x83);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x16, 0x0c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x29, 0x0a);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x30, 0xff);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x31, 0xfe);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x32, 0xfd);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x33, 0xfb);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x34, 0xf8);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x35, 0xf5);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x36, 0xf3);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x37, 0xf2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x38, 0xf2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x39, 0xf2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x3a, 0xef);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x3b, 0xec);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x3d, 0xe9);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x3f, 0xe5);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x40, 0xe5);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x41, 0xe5);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x2a, 0x13);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x45, 0xff);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x46, 0xf4);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x47, 0xe7);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x48, 0xda);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x49, 0xcd);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4a, 0xc0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4b, 0xb3);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4c, 0xb2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4d, 0xb2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4e, 0xb2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4f, 0x99);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x50, 0x80);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x51, 0x68);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x52, 0x66);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x53, 0x66);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x54, 0x66);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x2b, 0x0e);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x58, 0xff);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x59, 0xfb);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x5a, 0xf7);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x5b, 0xf3);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x5c, 0xef);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x5d, 0xe3);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x5e, 0xda);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x5f, 0xd8);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x60, 0xd8);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x61, 0xd8);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x62, 0xcb);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x63, 0xbf);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x64, 0xb3);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x65, 0xb2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x66, 0xb2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x67, 0xb2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x2a);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x25, 0x47);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x30, 0x47);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x39, 0x47);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x26);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x19, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1a, 0xe0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1b, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1c, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x2a, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x2b, 0xe0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0xf0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x84, 0x08);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x85, 0x0c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x20);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x51, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x25);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x91, 0x1f);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x92, 0x0f);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x93, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x94, 0x18);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x95, 0x03);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x96, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xb0, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x25);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x19, 0x1f);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1b, 0x1b);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x24);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xb8, 0x28);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x27);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd0, 0x31);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd1, 0x20);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd2, 0x30);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd4, 0x08);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xde, 0x80);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xdf, 0x02);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x26);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x00, 0x81);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x01, 0xb0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x22);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x9f, 0x50);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x6f, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x70, 0x11);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x73, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x74, 0x49);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x76, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x77, 0x49);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xa0, 0x3f);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xa9, 0x50);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xaa, 0x28);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xab, 0x28);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xad, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xb8, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xb9, 0x49);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xba, 0x49);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xbb, 0x49);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xbe, 0x04);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xbf, 0x49);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc0, 0x04);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc1, 0x59);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc2, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc5, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc6, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc7, 0x48);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xca, 0x43);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xcb, 0x3c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xce, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xcf, 0x43);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd0, 0x3c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd3, 0x43);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd4, 0x3c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd7, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xdc, 0x43);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xdd, 0x3c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xe1, 0x43);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xe2, 0x3c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xf2, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xf3, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xf4, 0x48);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x25);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x13, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x14, 0x23);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xbc, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xbd, 0x23);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x2a);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x97, 0x3c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x98, 0x02);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x99, 0x95);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x9a, 0x03);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x9b, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x9c, 0x0b);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x9d, 0x0a);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x9e, 0x90);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x22);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x9f, 0x50);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x23);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xa3, 0x50);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0xe0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x14, 0x60);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x16, 0xc0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4f, 0x02);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0xf0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x3a, 0x08);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0xd0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x02, 0xaf);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x09, 0xee);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1c, 0x99);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1d, 0x09);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x51, 0x0f, 0xff);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x53, 0x2c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x35, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xbb, 0x13);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x3b, 0x03, 0xac, 0x1a, 0x04, 0x04);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x11);
	msleep(70);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x29);

	return 0;
}

static int elish_csot_init_sequence(struct panel_info *pinfo)
{
	struct mipi_dsi_device *dsi0 = pinfo->dsi[0];
	struct mipi_dsi_device *dsi1 = pinfo->dsi[1];
	/* No datasheet, so write magic init sequence directly */
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xb9, 0x05);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x20);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x18, 0x40);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xb9, 0x02);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0xd0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x02, 0xaf);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x00, 0x30);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x09, 0xee);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1c, 0x99);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1d, 0x09);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0xf0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x3a, 0x08);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0xe0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4f, 0x02);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x20);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x58, 0x40);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x35, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x23);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x00, 0x80);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x01, 0x84);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x05, 0x2d);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x06, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x07, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x08, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x09, 0x45);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x11, 0x02);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x12, 0x80);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x15, 0x83);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x16, 0x0c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x29, 0x0a);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x30, 0xff);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x31, 0xfe);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x32, 0xfd);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x33, 0xfb);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x34, 0xf8);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x35, 0xf5);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x36, 0xf3);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x37, 0xf2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x38, 0xf2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x39, 0xf2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x3a, 0xef);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x3b, 0xec);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x3d, 0xe9);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x3f, 0xe5);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x40, 0xe5);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x41, 0xe5);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x2a, 0x13);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x45, 0xff);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x46, 0xf4);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x47, 0xe7);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x48, 0xda);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x49, 0xcd);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4a, 0xc0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4b, 0xb3);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4c, 0xb2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4d, 0xb2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4e, 0xb2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x4f, 0x99);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x50, 0x80);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x51, 0x68);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x52, 0x66);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x53, 0x66);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x54, 0x66);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x2b, 0x0e);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x58, 0xff);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x59, 0xfb);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x5a, 0xf7);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x5b, 0xf3);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x5c, 0xef);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x5d, 0xe3);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x5e, 0xda);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x5f, 0xd8);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x60, 0xd8);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x61, 0xd8);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x62, 0xcb);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x63, 0xbf);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x64, 0xb3);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x65, 0xb2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x66, 0xb2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x67, 0xb2);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x51, 0x0f, 0xff);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x53, 0x2c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x55, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xbb, 0x13);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x3b, 0x03, 0xac, 0x1a, 0x04, 0x04);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x2a);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x25, 0x46);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x30, 0x46);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x39, 0x46);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x26);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x01, 0xb0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x19, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1a, 0xe0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1b, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1c, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x2a, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x2b, 0xe0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0xf0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x84, 0x08);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x85, 0x0c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x20);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x51, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x25);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x91, 0x1f);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x92, 0x0f);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x93, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x94, 0x18);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x95, 0x03);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x96, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xb0, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x25);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x19, 0x1f);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1b, 0x1b);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x24);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xb8, 0x28);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x27);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd0, 0x31);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd1, 0x20);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd4, 0x08);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xde, 0x80);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xdf, 0x02);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x26);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x00, 0x81);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x01, 0xb0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x22);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x6f, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x70, 0x11);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x73, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x74, 0x4d);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xa0, 0x3f);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xa9, 0x50);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xaa, 0x28);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xab, 0x28);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xad, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xb8, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xb9, 0x4b);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xba, 0x96);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xbb, 0x4b);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xbe, 0x07);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xbf, 0x4b);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc0, 0x07);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc1, 0x5c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc2, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc5, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc6, 0x3f);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc7, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xca, 0x08);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xcb, 0x40);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xce, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xcf, 0x08);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd0, 0x40);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd3, 0x08);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xd4, 0x40);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x25);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xbc, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xbd, 0x1c);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x2a);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x9a, 0x03);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x11);
	msleep(70);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x29);

	return 0;
}

static int j606f_boe_init_sequence(struct panel_info *pinfo)
{
	struct mipi_dsi_device *dsi = pinfo->dsi[0];
	struct device *dev = &dsi->dev;
	int ret;

	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x05, 0xd9);
	mipi_dsi_dcs_write_seq(dsi, 0x07, 0x78);
	mipi_dsi_dcs_write_seq(dsi, 0x08, 0x5a);
	mipi_dsi_dcs_write_seq(dsi, 0x0d, 0x63);
	mipi_dsi_dcs_write_seq(dsi, 0x0e, 0x91);
	mipi_dsi_dcs_write_seq(dsi, 0x0f, 0x73);
	mipi_dsi_dcs_write_seq(dsi, 0x95, 0xeb);
	mipi_dsi_dcs_write_seq(dsi, 0x96, 0xeb);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PARTIAL_ROWS, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0x6d, 0x66);
	mipi_dsi_dcs_write_seq(dsi, 0x75, 0xa2);
	mipi_dsi_dcs_write_seq(dsi, 0x77, 0xb3);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x08, 0x00, 0x23, 0x00, 0x4d, 0x00, 0x6d, 0x00,
			       0x89, 0x00, 0xa1, 0x00, 0xb6, 0x00, 0xc9);
	mipi_dsi_dcs_write_seq(dsi, 0xb1, 0x00, 0xda, 0x01, 0x13, 0x01, 0x3c, 0x01, 0x7e, 0x01,
			       0xab, 0x01, 0xf7, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xb2, 0x02, 0x67, 0x02, 0xa6, 0x02, 0xd1, 0x03, 0x08, 0x03,
			       0x2e, 0x03, 0x5b, 0x03, 0x6b, 0x03, 0x7b);
	mipi_dsi_dcs_write_seq(dsi, 0xb3, 0x03, 0x8e, 0x03, 0xa2, 0x03, 0xb7, 0x03, 0xe7, 0x03,
			       0xfd, 0x03, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xb4, 0x00, 0x08, 0x00, 0x23, 0x00, 0x4d, 0x00, 0x6d, 0x00,
			       0x89, 0x00, 0xa1, 0x00, 0xb6, 0x00, 0xc9);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x00, 0xda, 0x01, 0x13, 0x01, 0x3c, 0x01, 0x7e, 0x01,
			       0xab, 0x01, 0xf7, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xb6, 0x02, 0x67, 0x02, 0xa6, 0x02, 0xd1, 0x03, 0x08, 0x03,
			       0x2e, 0x03, 0x5b, 0x03, 0x6b, 0x03, 0x7b);
	mipi_dsi_dcs_write_seq(dsi, 0xb7, 0x03, 0x8e, 0x03, 0xa2, 0x03, 0xb7, 0x03, 0xe7, 0x03,
			       0xfd, 0x03, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xb8, 0x00, 0x08, 0x00, 0x23, 0x00, 0x4d, 0x00, 0x6d, 0x00,
			       0x89, 0x00, 0xa1, 0x00, 0xb6, 0x00, 0xc9);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x00, 0xda, 0x01, 0x13, 0x01, 0x3c, 0x01, 0x7e, 0x01,
			       0xab, 0x01, 0xf7, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xba, 0x02, 0x67, 0x02, 0xa6, 0x02, 0xd1, 0x03, 0x08, 0x03,
			       0x2e, 0x03, 0x5b, 0x03, 0x6b, 0x03, 0x7b);
	mipi_dsi_dcs_write_seq(dsi, 0xbb, 0x03, 0x8e, 0x03, 0xa2, 0x03, 0xb7, 0x03, 0xe7, 0x03,
			       0xfd, 0x03, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x21);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x45, 0x00, 0x65, 0x00,
			       0x81, 0x00, 0x99, 0x00, 0xae, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq(dsi, 0xb1, 0x00, 0xd2, 0x01, 0x0b, 0x01, 0x34, 0x01, 0x76, 0x01,
			       0xa3, 0x01, 0xef, 0x02, 0x27, 0x02, 0x29);
	mipi_dsi_dcs_write_seq(dsi, 0xb2, 0x02, 0x5f, 0x02, 0x9e, 0x02, 0xc9, 0x03, 0x00, 0x03,
			       0x26, 0x03, 0x53, 0x03, 0x63, 0x03, 0x73);
	mipi_dsi_dcs_write_seq(dsi, 0xb3, 0x03, 0x86, 0x03, 0x9a, 0x03, 0xaf, 0x03, 0xdf, 0x03,
			       0xf5, 0x03, 0xf7);
	mipi_dsi_dcs_write_seq(dsi, 0xb4, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x45, 0x00, 0x65, 0x00,
			       0x81, 0x00, 0x99, 0x00, 0xae, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x00, 0xd2, 0x01, 0x0b, 0x01, 0x34, 0x01, 0x76, 0x01,
			       0xa3, 0x01, 0xef, 0x02, 0x27, 0x02, 0x29);
	mipi_dsi_dcs_write_seq(dsi, 0xb6, 0x02, 0x5f, 0x02, 0x9e, 0x02, 0xc9, 0x03, 0x00, 0x03,
			       0x26, 0x03, 0x53, 0x03, 0x63, 0x03, 0x73);
	mipi_dsi_dcs_write_seq(dsi, 0xb7, 0x03, 0x86, 0x03, 0x9a, 0x03, 0xaf, 0x03, 0xdf, 0x03,
			       0xf5, 0x03, 0xf7);
	mipi_dsi_dcs_write_seq(dsi, 0xb8, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x45, 0x00, 0x65, 0x00,
			       0x81, 0x00, 0x99, 0x00, 0xae, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x00, 0xd2, 0x01, 0x0b, 0x01, 0x34, 0x01, 0x76, 0x01,
			       0xa3, 0x01, 0xef, 0x02, 0x27, 0x02, 0x29);
	mipi_dsi_dcs_write_seq(dsi, 0xba, 0x02, 0x5f, 0x02, 0x9e, 0x02, 0xc9, 0x03, 0x00, 0x03,
			       0x26, 0x03, 0x53, 0x03, 0x63, 0x03, 0x73);
	mipi_dsi_dcs_write_seq(dsi, 0xbb, 0x03, 0x86, 0x03, 0x9a, 0x03, 0xaf, 0x03, 0xdf, 0x03,
			       0xf5, 0x03, 0xf7);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x23);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0x07, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x11, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x12, 0x77);
	mipi_dsi_dcs_write_seq(dsi, 0x15, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0x16, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x24);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x01, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x02, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0x03, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0x04, 0x1d);
	mipi_dsi_dcs_write_seq(dsi, 0x05, 0x1d);
	mipi_dsi_dcs_write_seq(dsi, 0x06, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x07, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x08, 0x0f);
	mipi_dsi_dcs_write_seq(dsi, 0x09, 0x0f);
	mipi_dsi_dcs_write_seq(dsi, 0x0a, 0x0e);
	mipi_dsi_dcs_write_seq(dsi, 0x0b, 0x0e);
	mipi_dsi_dcs_write_seq(dsi, 0x0c, 0x0d);
	mipi_dsi_dcs_write_seq(dsi, 0x0d, 0x0d);
	mipi_dsi_dcs_write_seq(dsi, 0x0e, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x0f, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x10, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x11, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x12, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x13, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x14, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x15, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x16, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x17, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x18, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0x19, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0x1a, 0x1d);
	mipi_dsi_dcs_write_seq(dsi, 0x1b, 0x1d);
	mipi_dsi_dcs_write_seq(dsi, 0x1c, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x1d, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x1e, 0x0f);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0x0f);
	mipi_dsi_dcs_write_seq(dsi, 0x20, 0x0e);
	mipi_dsi_dcs_write_seq(dsi, 0x21, 0x0e);
	mipi_dsi_dcs_write_seq(dsi, 0x22, 0x0d);
	mipi_dsi_dcs_write_seq(dsi, 0x23, 0x0d);
	mipi_dsi_dcs_write_seq(dsi, 0x24, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x25, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_GAMMA_CURVE, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x27, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x28, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x29, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x2a, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x2b, 0x00);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_LUT, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x2f, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PARTIAL_ROWS, 0x44);
	mipi_dsi_dcs_write_seq(dsi, 0x33, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x34, 0x32);
	mipi_dsi_dcs_write_seq(dsi, 0x37, 0x44);
	mipi_dsi_dcs_write_seq(dsi, 0x38, 0x40);
	mipi_dsi_dcs_write_seq(dsi, 0x39, 0x00);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0x9a);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x3b, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_3D_CONTROL, 0x42);
	mipi_dsi_dcs_write_seq(dsi, 0x3f, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x43, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x47, 0x66);
	mipi_dsi_dcs_write_seq(dsi, 0x4a, 0x9a);
	mipi_dsi_dcs_write_seq(dsi, 0x4b, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, 0x4c, 0x91);
	mipi_dsi_dcs_write_seq(dsi, 0x4d, 0x21);
	mipi_dsi_dcs_write_seq(dsi, 0x4e, 0x43);

	ret = mipi_dsi_dcs_set_display_brightness(dsi, 18);
	if (ret < 0) {
		dev_err(dev, "Failed to set display brightness: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x52, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0x55, 0x82, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0x56, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x58, 0x21);
	mipi_dsi_dcs_write_seq(dsi, 0x59, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0x5a, 0xba);
	mipi_dsi_dcs_write_seq(dsi, 0x5b, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0x00, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x5f, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x65, 0x82);
	mipi_dsi_dcs_write_seq(dsi, 0x7e, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x7f, 0x3c);
	mipi_dsi_dcs_write_seq(dsi, 0x82, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x97, 0xc0);
	mipi_dsi_dcs_write_seq(dsi, 0xb6,
			       0x05, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
			       0x05, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x92, 0xc4);
	mipi_dsi_dcs_write_seq(dsi, 0x93, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, 0x94, 0x5f);
	mipi_dsi_dcs_write_seq(dsi, 0xd7, 0x55);
	mipi_dsi_dcs_write_seq(dsi, 0xda, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0xde, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xdb, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xdc, 0xc4);
	mipi_dsi_dcs_write_seq(dsi, 0xdd, 0x22);
	mipi_dsi_dcs_write_seq(dsi, 0xdf, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe0, 0xc4);
	mipi_dsi_dcs_write_seq(dsi, 0xe1, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe2, 0xc4);
	mipi_dsi_dcs_write_seq(dsi, 0xe3, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe4, 0xc4);
	mipi_dsi_dcs_write_seq(dsi, 0xe5, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe6, 0xc4);
	mipi_dsi_dcs_write_seq(dsi, 0x5c, 0x88);
	mipi_dsi_dcs_write_seq(dsi, 0x5d, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x8d, 0x88);
	mipi_dsi_dcs_write_seq(dsi, 0x8e, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x90);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x05, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x19, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0xba);
	mipi_dsi_dcs_write_seq(dsi, 0x20, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_GAMMA_CURVE, 0xba);
	mipi_dsi_dcs_write_seq(dsi, 0x27, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, 0x33, 0xba);
	mipi_dsi_dcs_write_seq(dsi, 0x34, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, 0x3f, 0xe0);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_VSYNC_TIMING, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x44, 0x00);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_GET_SCANLINE, 0x40);
	mipi_dsi_dcs_write_seq(dsi, 0x48, 0xba);
	mipi_dsi_dcs_write_seq(dsi, 0x49, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, 0x5b, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x5d, 0x00);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0xd0);
	mipi_dsi_dcs_write_seq(dsi, 0x61, 0xba);
	mipi_dsi_dcs_write_seq(dsi, 0x62, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, 0xf1, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x64, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0x67, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0x6a, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0x70, 0x30);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_READ_PPS_START, 0xf3);
	mipi_dsi_dcs_write_seq(dsi, 0xa3, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xa4, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xa5, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xd6, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x26);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0xa1);
	mipi_dsi_dcs_write_seq(dsi, 0x0a, 0xf2);
	mipi_dsi_dcs_write_seq(dsi, 0x04, 0x28);
	mipi_dsi_dcs_write_seq(dsi, 0x06, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0x0c, 0x13);
	mipi_dsi_dcs_write_seq(dsi, 0x0d, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x0f, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x11, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x12, 0x50);
	mipi_dsi_dcs_write_seq(dsi, 0x13, 0x51);
	mipi_dsi_dcs_write_seq(dsi, 0x14, 0x65);
	mipi_dsi_dcs_write_seq(dsi, 0x15, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x16, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0x17, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, 0x18, 0x86);
	mipi_dsi_dcs_write_seq(dsi, 0x19, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0x1a, 0x7b);
	mipi_dsi_dcs_write_seq(dsi, 0x1b, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0x1c, 0xbb);
	mipi_dsi_dcs_write_seq(dsi, 0x22, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x23, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x2a, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0x2b, 0x7b);
	mipi_dsi_dcs_write_seq(dsi, 0x1d, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x1e, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, 0x24, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x25, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, 0x2f, 0x05);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PARTIAL_ROWS, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PARTIAL_COLUMNS, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x32, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, 0x39, 0x00);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0xc3);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x20, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x33, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0x34, 0x78);
	mipi_dsi_dcs_write_seq(dsi, 0x35, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0xc8, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xc9, 0x82);
	mipi_dsi_dcs_write_seq(dsi, 0xca, 0x4e);
	mipi_dsi_dcs_write_seq(dsi, 0xcb, 0x00);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_READ_PPS_CONTINUE, 0x4c);
	mipi_dsi_dcs_write_seq(dsi, 0xaa, 0x47);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x27);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x56, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x58, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0x59, 0x53);
	mipi_dsi_dcs_write_seq(dsi, 0x5a, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x5b, 0x14);
	mipi_dsi_dcs_write_seq(dsi, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x5d, 0x01);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x5f, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0x60, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x61, 0x1d);
	mipi_dsi_dcs_write_seq(dsi, 0x62, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x63, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x64, 0x24);
	mipi_dsi_dcs_write_seq(dsi, 0x65, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0x66, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x67, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x68, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x78, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xd1, 0x24);
	mipi_dsi_dcs_write_seq(dsi, 0xd2, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x22, 0x2f);
	mipi_dsi_dcs_write_seq(dsi, 0x23, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x24, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x25, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_GAMMA_CURVE, 0xf8);
	mipi_dsi_dcs_write_seq(dsi, 0x27, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x28, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, 0x29, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x2a, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, 0x2b, 0x00);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_LUT, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0xe0);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x14, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x16, 0xc0);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0xf0);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0x08);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x24);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0x5d);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x3b, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x4a, 0x5d);
	mipi_dsi_dcs_write_seq(dsi, 0x4b, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x5a, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x5b, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x91, 0x44);
	mipi_dsi_dcs_write_seq(dsi, 0x92, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xdb, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xdc, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xdd, 0x22);
	mipi_dsi_dcs_write_seq(dsi, 0xdf, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe0, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xe1, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe2, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xe3, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe4, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xe5, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe6, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x5d, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x8d, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x8e, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x20, 0x60);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_GAMMA_CURVE, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x27, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x33, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x34, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x48, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x49, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x5b, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x61, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x62, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x26);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x02, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0x19, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x1a, 0x7f);
	mipi_dsi_dcs_write_seq(dsi, 0x1b, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x1c, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x2a, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x2b, 0x7f);
	mipi_dsi_dcs_write_seq(dsi, 0x1e, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0x25, 0x75);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PARTIAL_ROWS, 0x75);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PARTIAL_COLUMNS, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0x32, 0x8d);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0x75);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x25, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x18, 0x40);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x02);

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear on: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0xbb, 0x13);
	mipi_dsi_dcs_write_seq(dsi, 0x3b, 0x03, 0x5f, 0x1a, 0x04, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x10);
	usleep_range(10000, 11000);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);

	ret = mipi_dsi_dcs_set_display_brightness(dsi, 0);
	if (ret < 0) {
		dev_err(dev, "Failed to set display brightness: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x2c);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x68, 0x05, 0x01);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(100);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(30);

	return 0;
}

static const struct drm_display_mode elish_boe_modes[] = {
	{
		/* There is only one 120 Hz timing, but it doesn't work perfectly, 104 Hz preferred */
		.clock = (1600 + 60 + 8 + 60) * (2560 + 26 + 4 + 168) * 104 / 1000,
		.hdisplay = 1600,
		.hsync_start = 1600 + 60,
		.hsync_end = 1600 + 60 + 8,
		.htotal = 1600 + 60 + 8 + 60,
		.vdisplay = 2560,
		.vsync_start = 2560 + 26,
		.vsync_end = 2560 + 26 + 4,
		.vtotal = 2560 + 26 + 4 + 168,
	},
};

static const struct drm_display_mode elish_csot_modes[] = {
	{
		/* There is only one 120 Hz timing, but it doesn't work perfectly, 104 Hz preferred */
		.clock = (1600 + 200 + 40 + 52) * (2560 + 26 + 4 + 168) * 104 / 1000,
		.hdisplay = 1600,
		.hsync_start = 1600 + 200,
		.hsync_end = 1600 + 200 + 40,
		.htotal = 1600 + 200 + 40 + 52,
		.vdisplay = 2560,
		.vsync_start = 2560 + 26,
		.vsync_end = 2560 + 26 + 4,
		.vtotal = 2560 + 26 + 4 + 168,
	},
};

static const struct drm_display_mode j606f_boe_modes[] = {
	{
		.clock = (1200 + 58 + 2 + 60) * (2000 + 26 + 2 + 93) * 60 / 1000,
		.hdisplay = 1200,
		.hsync_start = 1200 + 58,
		.hsync_end = 1200 + 58 + 2,
		.htotal = 1200 + 58 + 2 + 60,
		.vdisplay = 2000,
		.vsync_start = 2000 + 26,
		.vsync_end = 2000 + 26 + 2,
		.vtotal = 2000 + 26 + 2 + 93,
		.width_mm = 143,
		.height_mm = 235,
	},
};

static const struct panel_desc elish_boe_desc = {
	.modes = elish_boe_modes,
	.num_modes = ARRAY_SIZE(elish_boe_modes),
	.dsi_info = {
		.type = "BOE-elish",
		.channel = 0,
		.node = NULL,
	},
	.width_mm = 127,
	.height_mm = 203,
	.bpc = 8,
	.lanes = 3,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.init_sequence = elish_boe_init_sequence,
	.is_dual_dsi = true,
};

static const struct panel_desc elish_csot_desc = {
	.modes = elish_csot_modes,
	.num_modes = ARRAY_SIZE(elish_csot_modes),
	.dsi_info = {
		.type = "CSOT-elish",
		.channel = 0,
		.node = NULL,
	},
	.width_mm = 127,
	.height_mm = 203,
	.bpc = 8,
	.lanes = 3,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.init_sequence = elish_csot_init_sequence,
	.is_dual_dsi = true,
};

static const struct panel_desc j606f_boe_desc = {
	.modes = j606f_boe_modes,
	.num_modes = ARRAY_SIZE(j606f_boe_modes),
	.width_mm = 143,
	.height_mm = 235,
	.bpc = 8,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.init_sequence = j606f_boe_init_sequence,
	.has_dcs_backlight = true,
};

static void nt36523_reset(struct panel_info *pinfo)
{
	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	usleep_range(12000, 13000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 0);
	usleep_range(12000, 13000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	usleep_range(12000, 13000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 0);
	usleep_range(12000, 13000);
}

static int nt36523_prepare(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int ret;

	if (pinfo->prepared)
		return 0;

	ret = regulator_enable(pinfo->vddio);
	if (ret) {
		dev_err(panel->dev, "failed to enable vddio regulator: %d\n", ret);
		return ret;
	}

	nt36523_reset(pinfo);

	ret = pinfo->desc->init_sequence(pinfo);
	if (ret < 0) {
		regulator_disable(pinfo->vddio);
		dev_err(panel->dev, "failed to initialize panel: %d\n", ret);
		return ret;
	}

	pinfo->prepared = true;

	return 0;
}

static int nt36523_disable(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int i, ret;

	for (i = 0; i < DSI_NUM_MIN + pinfo->desc->is_dual_dsi; i++) {
		ret = mipi_dsi_dcs_set_display_off(pinfo->dsi[i]);
		if (ret < 0)
			dev_err(&pinfo->dsi[i]->dev, "failed to set display off: %d\n", ret);
	}

	for (i = 0; i < DSI_NUM_MIN + pinfo->desc->is_dual_dsi; i++) {
		ret = mipi_dsi_dcs_enter_sleep_mode(pinfo->dsi[i]);
		if (ret < 0)
			dev_err(&pinfo->dsi[i]->dev, "failed to enter sleep mode: %d\n", ret);
	}

	msleep(70);

	return 0;
}

static int nt36523_unprepare(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);

	if (!pinfo->prepared)
		return 0;

	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	regulator_disable(pinfo->vddio);

	pinfo->prepared = false;

	return 0;
}

static void nt36523_remove(struct mipi_dsi_device *dsi)
{
	struct panel_info *pinfo = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(pinfo->dsi[0]);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI0 host: %d\n", ret);

	if (pinfo->desc->is_dual_dsi) {
		ret = mipi_dsi_detach(pinfo->dsi[1]);
		if (ret < 0)
			dev_err(&pinfo->dsi[1]->dev, "failed to detach from DSI1 host: %d\n", ret);
		mipi_dsi_device_unregister(pinfo->dsi[1]);
	}

	drm_panel_remove(&pinfo->panel);
}

static int nt36523_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int i;

	for (i = 0; i < pinfo->desc->num_modes; i++) {
		const struct drm_display_mode *m = &pinfo->desc->modes[i];
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->dev, m);
		if (!mode) {
			dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
			return -ENOMEM;
		}

		mode->type = DRM_MODE_TYPE_DRIVER;
		if (i == 0)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.width_mm = pinfo->desc->width_mm;
	connector->display_info.height_mm = pinfo->desc->height_mm;
	connector->display_info.bpc = pinfo->desc->bpc;

	return pinfo->desc->num_modes;
}

static enum drm_panel_orientation nt36523_get_orientation(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);

	return pinfo->orientation;
}

static const struct drm_panel_funcs nt36523_panel_funcs = {
	.disable = nt36523_disable,
	.prepare = nt36523_prepare,
	.unprepare = nt36523_unprepare,
	.get_modes = nt36523_get_modes,
	.get_orientation = nt36523_get_orientation,
};

static int nt36523_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int nt36523_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops nt36523_bl_ops = {
	.update_status = nt36523_bl_update_status,
	.get_brightness = nt36523_bl_get_brightness,
};

static struct backlight_device *nt36523_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 512,
		.max_brightness = 4095,
		.scale = BACKLIGHT_SCALE_NON_LINEAR,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &nt36523_bl_ops, &props);
}

static int nt36523_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi1;
	struct mipi_dsi_host *dsi1_host;
	struct panel_info *pinfo;
	const struct mipi_dsi_device_info *info;
	int i, ret;

	pinfo = devm_kzalloc(dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	pinfo->vddio = devm_regulator_get(dev, "vddio");
	if (IS_ERR(pinfo->vddio))
		return dev_err_probe(dev, PTR_ERR(pinfo->vddio), "failed to get vddio regulator\n");

	pinfo->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(pinfo->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(pinfo->reset_gpio), "failed to get reset gpio\n");

	pinfo->desc = of_device_get_match_data(dev);
	if (!pinfo->desc)
		return -ENODEV;

	/* If the panel is dual dsi, register DSI1 */
	if (pinfo->desc->is_dual_dsi) {
		info = &pinfo->desc->dsi_info;

		dsi1 = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);
		if (!dsi1) {
			dev_err(dev, "cannot get secondary DSI node.\n");
			return -ENODEV;
		}

		dsi1_host = of_find_mipi_dsi_host_by_node(dsi1);
		of_node_put(dsi1);
		if (!dsi1_host)
			return dev_err_probe(dev, -EPROBE_DEFER, "cannot get secondary DSI host\n");

		pinfo->dsi[1] = mipi_dsi_device_register_full(dsi1_host, info);
		if (!pinfo->dsi[1]) {
			dev_err(dev, "cannot get secondary DSI device\n");
			return -ENODEV;
		}
	}

	pinfo->dsi[0] = dsi;
	mipi_dsi_set_drvdata(dsi, pinfo);
	drm_panel_init(&pinfo->panel, dev, &nt36523_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	ret = of_drm_get_panel_orientation(dev->of_node, &pinfo->orientation);
	if (ret < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}

	if (pinfo->desc->has_dcs_backlight) {
		pinfo->panel.backlight = nt36523_create_backlight(dsi);
		if (IS_ERR(pinfo->panel.backlight))
			return dev_err_probe(dev, PTR_ERR(pinfo->panel.backlight),
					     "Failed to create backlight\n");
	} else {
		ret = drm_panel_of_backlight(&pinfo->panel);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to get backlight\n");
	}

	drm_panel_add(&pinfo->panel);

	for (i = 0; i < DSI_NUM_MIN + pinfo->desc->is_dual_dsi; i++) {
		pinfo->dsi[i]->lanes = pinfo->desc->lanes;
		pinfo->dsi[i]->format = pinfo->desc->format;
		pinfo->dsi[i]->mode_flags = pinfo->desc->mode_flags;

		ret = mipi_dsi_attach(pinfo->dsi[i]);
		if (ret < 0)
			return dev_err_probe(dev, ret, "cannot attach to DSI%d host.\n", i);
	}

	return 0;
}

static const struct of_device_id nt36523_of_match[] = {
	{
		.compatible = "lenovo,j606f-boe-nt36523w",
		.data = &j606f_boe_desc,
	},
	{
		.compatible = "xiaomi,elish-boe-nt36523",
		.data = &elish_boe_desc,
	},
	{
		.compatible = "xiaomi,elish-csot-nt36523",
		.data = &elish_csot_desc,
	},
	{},
};
MODULE_DEVICE_TABLE(of, nt36523_of_match);

static struct mipi_dsi_driver nt36523_driver = {
	.probe = nt36523_probe,
	.remove = nt36523_remove,
	.driver = {
		.name = "panel-novatek-nt36523",
		.of_match_table = nt36523_of_match,
	},
};
module_mipi_dsi_driver(nt36523_driver);

MODULE_AUTHOR("Jianhua Lu <lujianhua000@gmail.com>");
MODULE_DESCRIPTION("DRM driver for Novatek NT36523 based MIPI DSI panels");
MODULE_LICENSE("GPL");
