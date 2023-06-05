// SPDX-License-Identifier: GPL-2.0-only
/*
 * Novatek NT36523 DriverIC panels driver
 *
 * Copyright (c) 2022, 2023 Jianhua Lu <lujianhua000@gmail.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

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

static const struct drm_panel_funcs nt36523_panel_funcs = {
	.disable = nt36523_disable,
	.prepare = nt36523_prepare,
	.unprepare = nt36523_unprepare,
	.get_modes = nt36523_get_modes,
};

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

	ret = drm_panel_of_backlight(&pinfo->panel);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get backlight\n");

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
