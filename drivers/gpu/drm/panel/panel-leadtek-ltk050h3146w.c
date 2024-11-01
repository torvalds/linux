// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Theobroma Systems Design und Consulting GmbH
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/display_timing.h>
#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct ltk050h3146w_cmd {
	char cmd;
	char data;
};

struct ltk050h3146w;
struct ltk050h3146w_desc {
	const unsigned long mode_flags;
	const struct drm_display_mode *mode;
	void (*init)(struct mipi_dsi_multi_context *dsi_ctx);
};

struct ltk050h3146w {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct regulator *vci;
	struct regulator *iovcc;
	const struct ltk050h3146w_desc *panel_desc;
};

static const struct ltk050h3146w_cmd page1_cmds[] = {
	{ 0x22, 0x0A }, /* BGR SS GS */
	{ 0x31, 0x00 }, /* column inversion */
	{ 0x53, 0xA2 }, /* VCOM1 */
	{ 0x55, 0xA2 }, /* VCOM2 */
	{ 0x50, 0x81 }, /* VREG1OUT=5V */
	{ 0x51, 0x85 }, /* VREG2OUT=-5V */
	{ 0x62, 0x0D }, /* EQT Time setting */
/*
 * The vendor init selected page 1 here _again_
 * Is this supposed to be page 2?
 */
	{ 0xA0, 0x00 },
	{ 0xA1, 0x1A },
	{ 0xA2, 0x28 },
	{ 0xA3, 0x13 },
	{ 0xA4, 0x16 },
	{ 0xA5, 0x29 },
	{ 0xA6, 0x1D },
	{ 0xA7, 0x1E },
	{ 0xA8, 0x84 },
	{ 0xA9, 0x1C },
	{ 0xAA, 0x28 },
	{ 0xAB, 0x75 },
	{ 0xAC, 0x1A },
	{ 0xAD, 0x19 },
	{ 0xAE, 0x4D },
	{ 0xAF, 0x22 },
	{ 0xB0, 0x28 },
	{ 0xB1, 0x54 },
	{ 0xB2, 0x66 },
	{ 0xB3, 0x39 },
	{ 0xC0, 0x00 },
	{ 0xC1, 0x1A },
	{ 0xC2, 0x28 },
	{ 0xC3, 0x13 },
	{ 0xC4, 0x16 },
	{ 0xC5, 0x29 },
	{ 0xC6, 0x1D },
	{ 0xC7, 0x1E },
	{ 0xC8, 0x84 },
	{ 0xC9, 0x1C },
	{ 0xCA, 0x28 },
	{ 0xCB, 0x75 },
	{ 0xCC, 0x1A },
	{ 0xCD, 0x19 },
	{ 0xCE, 0x4D },
	{ 0xCF, 0x22 },
	{ 0xD0, 0x28 },
	{ 0xD1, 0x54 },
	{ 0xD2, 0x66 },
	{ 0xD3, 0x39 },
};

static const struct ltk050h3146w_cmd page3_cmds[] = {
	{ 0x01, 0x00 },
	{ 0x02, 0x00 },
	{ 0x03, 0x73 },
	{ 0x04, 0x00 },
	{ 0x05, 0x00 },
	{ 0x06, 0x0a },
	{ 0x07, 0x00 },
	{ 0x08, 0x00 },
	{ 0x09, 0x01 },
	{ 0x0a, 0x00 },
	{ 0x0b, 0x00 },
	{ 0x0c, 0x01 },
	{ 0x0d, 0x00 },
	{ 0x0e, 0x00 },
	{ 0x0f, 0x1d },
	{ 0x10, 0x1d },
	{ 0x11, 0x00 },
	{ 0x12, 0x00 },
	{ 0x13, 0x00 },
	{ 0x14, 0x00 },
	{ 0x15, 0x00 },
	{ 0x16, 0x00 },
	{ 0x17, 0x00 },
	{ 0x18, 0x00 },
	{ 0x19, 0x00 },
	{ 0x1a, 0x00 },
	{ 0x1b, 0x00 },
	{ 0x1c, 0x00 },
	{ 0x1d, 0x00 },
	{ 0x1e, 0x40 },
	{ 0x1f, 0x80 },
	{ 0x20, 0x06 },
	{ 0x21, 0x02 },
	{ 0x22, 0x00 },
	{ 0x23, 0x00 },
	{ 0x24, 0x00 },
	{ 0x25, 0x00 },
	{ 0x26, 0x00 },
	{ 0x27, 0x00 },
	{ 0x28, 0x33 },
	{ 0x29, 0x03 },
	{ 0x2a, 0x00 },
	{ 0x2b, 0x00 },
	{ 0x2c, 0x00 },
	{ 0x2d, 0x00 },
	{ 0x2e, 0x00 },
	{ 0x2f, 0x00 },
	{ 0x30, 0x00 },
	{ 0x31, 0x00 },
	{ 0x32, 0x00 },
	{ 0x33, 0x00 },
	{ 0x34, 0x04 },
	{ 0x35, 0x00 },
	{ 0x36, 0x00 },
	{ 0x37, 0x00 },
	{ 0x38, 0x3C },
	{ 0x39, 0x35 },
	{ 0x3A, 0x01 },
	{ 0x3B, 0x40 },
	{ 0x3C, 0x00 },
	{ 0x3D, 0x01 },
	{ 0x3E, 0x00 },
	{ 0x3F, 0x00 },
	{ 0x40, 0x00 },
	{ 0x41, 0x88 },
	{ 0x42, 0x00 },
	{ 0x43, 0x00 },
	{ 0x44, 0x1F },
	{ 0x50, 0x01 },
	{ 0x51, 0x23 },
	{ 0x52, 0x45 },
	{ 0x53, 0x67 },
	{ 0x54, 0x89 },
	{ 0x55, 0xab },
	{ 0x56, 0x01 },
	{ 0x57, 0x23 },
	{ 0x58, 0x45 },
	{ 0x59, 0x67 },
	{ 0x5a, 0x89 },
	{ 0x5b, 0xab },
	{ 0x5c, 0xcd },
	{ 0x5d, 0xef },
	{ 0x5e, 0x11 },
	{ 0x5f, 0x01 },
	{ 0x60, 0x00 },
	{ 0x61, 0x15 },
	{ 0x62, 0x14 },
	{ 0x63, 0x0E },
	{ 0x64, 0x0F },
	{ 0x65, 0x0C },
	{ 0x66, 0x0D },
	{ 0x67, 0x06 },
	{ 0x68, 0x02 },
	{ 0x69, 0x07 },
	{ 0x6a, 0x02 },
	{ 0x6b, 0x02 },
	{ 0x6c, 0x02 },
	{ 0x6d, 0x02 },
	{ 0x6e, 0x02 },
	{ 0x6f, 0x02 },
	{ 0x70, 0x02 },
	{ 0x71, 0x02 },
	{ 0x72, 0x02 },
	{ 0x73, 0x02 },
	{ 0x74, 0x02 },
	{ 0x75, 0x01 },
	{ 0x76, 0x00 },
	{ 0x77, 0x14 },
	{ 0x78, 0x15 },
	{ 0x79, 0x0E },
	{ 0x7a, 0x0F },
	{ 0x7b, 0x0C },
	{ 0x7c, 0x0D },
	{ 0x7d, 0x06 },
	{ 0x7e, 0x02 },
	{ 0x7f, 0x07 },
	{ 0x80, 0x02 },
	{ 0x81, 0x02 },
	{ 0x82, 0x02 },
	{ 0x83, 0x02 },
	{ 0x84, 0x02 },
	{ 0x85, 0x02 },
	{ 0x86, 0x02 },
	{ 0x87, 0x02 },
	{ 0x88, 0x02 },
	{ 0x89, 0x02 },
	{ 0x8A, 0x02 },
};

static const struct ltk050h3146w_cmd page4_cmds[] = {
	{ 0x70, 0x00 },
	{ 0x71, 0x00 },
	{ 0x82, 0x0F }, /* VGH_MOD clamp level=15v */
	{ 0x84, 0x0F }, /* VGH clamp level 15V */
	{ 0x85, 0x0D }, /* VGL clamp level (-10V) */
	{ 0x32, 0xAC },
	{ 0x8C, 0x80 },
	{ 0x3C, 0xF5 },
	{ 0xB5, 0x07 }, /* GAMMA OP */
	{ 0x31, 0x45 }, /* SOURCE OP */
	{ 0x3A, 0x24 }, /* PS_EN OFF */
	{ 0x88, 0x33 }, /* LVD */
};

static inline
struct ltk050h3146w *panel_to_ltk050h3146w(struct drm_panel *panel)
{
	return container_of(panel, struct ltk050h3146w, panel);
}

static void ltk050h3148w_init_sequence(struct mipi_dsi_multi_context *dsi_ctx)
{
	/*
	 * Init sequence was supplied by the panel vendor without much
	 * documentation.
	 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9, 0xff, 0x83, 0x94);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb1, 0x50, 0x15, 0x75, 0x09, 0x32, 0x44,
				     0x71, 0x31, 0x55, 0x2f);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xba, 0x63, 0x03, 0x68, 0x6b, 0xb2, 0xc0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2, 0x88);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb2, 0x00, 0x80, 0x64, 0x10, 0x07);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb4, 0x05, 0x70, 0x05, 0x70, 0x01, 0x70,
				     0x01, 0x0c, 0x86, 0x75, 0x00, 0x3f, 0x01, 0x74,
				     0x01, 0x74, 0x01, 0x74, 0x01, 0x0c, 0x86);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd3, 0x00, 0x00, 0x07, 0x07, 0x40, 0x1e,
				     0x08, 0x00, 0x32, 0x10, 0x08, 0x00, 0x08, 0x54,
				     0x15, 0x10, 0x05, 0x04, 0x02, 0x12, 0x10, 0x05,
				     0x07, 0x33, 0x34, 0x0c, 0x0c, 0x37, 0x10, 0x07,
				     0x17, 0x11, 0x40);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd5, 0x19, 0x19, 0x18, 0x18, 0x1b, 0x1b,
				     0x1a, 0x1a, 0x04, 0x05, 0x06, 0x07, 0x00, 0x01,
				     0x02, 0x03, 0x20, 0x21, 0x18, 0x18, 0x22, 0x23,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd6, 0x18, 0x18, 0x19, 0x19, 0x1b, 0x1b,
				     0x1a, 0x1a, 0x03, 0x02, 0x01, 0x00, 0x07, 0x06,
				     0x05, 0x04, 0x23, 0x22, 0x18, 0x18, 0x21, 0x20,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
				     0x18, 0x18, 0x18, 0x18, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe0, 0x00, 0x03, 0x09, 0x11, 0x11, 0x14,
				     0x18, 0x16, 0x2e, 0x3d, 0x4d, 0x4d, 0x58, 0x6c,
				     0x72, 0x78, 0x88, 0x8b, 0x86, 0xa4, 0xb2, 0x58,
				     0x55, 0x59, 0x5b, 0x5d, 0x60, 0x64, 0x7f, 0x00,
				     0x03, 0x09, 0x0f, 0x11, 0x14, 0x18, 0x16, 0x2e,
				     0x3d, 0x4d, 0x4d, 0x58, 0x6d, 0x73, 0x78, 0x88,
				     0x8b, 0x87, 0xa5, 0xb2, 0x58, 0x55, 0x58, 0x5b,
				     0x5d, 0x61, 0x65, 0x7f);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xcc, 0x0b);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc0, 0x1f, 0x31);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb6, 0xc4, 0xc4);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbd, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb1, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbd, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc6, 0xef);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd4, 0x02);

	mipi_dsi_dcs_set_tear_on_multi(dsi_ctx, 1);
	mipi_dsi_msleep(dsi_ctx, 60);
}

static const struct drm_display_mode ltk050h3148w_mode = {
	.hdisplay	= 720,
	.hsync_start	= 720 + 12,
	.hsync_end	= 720 + 12 + 6,
	.htotal		= 720 + 12 + 6 + 24,
	.vdisplay	= 1280,
	.vsync_start	= 1280 + 9,
	.vsync_end	= 1280 + 9 + 2,
	.vtotal		= 1280 + 9 + 2 + 16,
	.clock		= 59756,
	.width_mm	= 62,
	.height_mm	= 110,
};

static const struct ltk050h3146w_desc ltk050h3148w_data = {
	.mode = &ltk050h3148w_mode,
	.init = ltk050h3148w_init_sequence,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_VIDEO_BURST,
};

static void ltk050h3146w_init_sequence(struct mipi_dsi_multi_context *dsi_ctx)
{
	/*
	 * Init sequence was supplied by the panel vendor without much
	 * documentation.
	 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdf, 0x93, 0x65, 0xf8);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb0, 0x01, 0x03, 0x02, 0x00, 0x64, 0x06,
				     0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb2, 0x00, 0xb5);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb3, 0x00, 0xb5);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb7, 0x00, 0xbf, 0x00, 0x00, 0xbf, 0x00);

	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9, 0x00, 0xc4, 0x23, 0x07);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbb, 0x02, 0x01, 0x24, 0x00, 0x28, 0x0f,
				     0x28, 0x04, 0xcc, 0xcc, 0xcc);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbc, 0x0f, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbe, 0x1e, 0xf2);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc0, 0x26, 0x03);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc1, 0x00, 0x12);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc3, 0x04, 0x02, 0x02, 0x76, 0x01, 0x80,
				     0x80);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc4, 0x24, 0x80, 0xb4, 0x81, 0x12, 0x0f,
				     0x16, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc8, 0x7f, 0x72, 0x67, 0x5d, 0x5d, 0x50,
				     0x56, 0x41, 0x59, 0x57, 0x55, 0x70, 0x5b, 0x5f,
				     0x4f, 0x47, 0x38, 0x23, 0x08, 0x7f, 0x72, 0x67,
				     0x5d, 0x5d, 0x50, 0x56, 0x41, 0x59, 0x57, 0x55,
				     0x70, 0x5b, 0x5f, 0x4f, 0x47, 0x38, 0x23, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd0, 0x1e, 0x1f, 0x57, 0x58, 0x48, 0x4a,
				     0x44, 0x46, 0x40, 0x1f, 0x42, 0x1f, 0x1f, 0x1f,
				     0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd1, 0x1e, 0x1f, 0x57, 0x58, 0x49, 0x4b,
				     0x45, 0x47, 0x41, 0x1f, 0x43, 0x1f, 0x1f, 0x1f,
				     0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2, 0x1f, 0x1e, 0x17, 0x18, 0x07, 0x05,
				     0x0b, 0x09, 0x03, 0x1f, 0x01, 0x1f, 0x1f, 0x1f,
				     0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd3, 0x1f, 0x1e, 0x17, 0x18, 0x06, 0x04,
				     0x0a, 0x08, 0x02, 0x1f, 0x00, 0x1f, 0x1f, 0x1f,
				     0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd4, 0x00, 0x00, 0x00, 0x0c, 0x06, 0x20,
				     0x01, 0x02, 0x00, 0x60, 0x15, 0xb0, 0x30, 0x03,
				     0x04, 0x00, 0x60, 0x72, 0x0a, 0x00, 0x60, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd5, 0x00, 0x06, 0x06, 0x00, 0x30, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0xbc, 0x50, 0x00, 0x05,
				     0x21, 0x00, 0x60);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdd, 0x2c, 0xa3, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xde, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb2, 0x32, 0x1c);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb7, 0x3b, 0x70, 0x00, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc1, 0x11);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbb, 0x21, 0x22, 0x23, 0x24, 0x36, 0x37);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc2, 0x20, 0x38, 0x1e, 0x84);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xde, 0x00);

	mipi_dsi_dcs_set_tear_on_multi(dsi_ctx, 1);
	mipi_dsi_msleep(dsi_ctx, 60);
}

static const struct drm_display_mode ltk050h3146w_mode = {
	.hdisplay	= 720,
	.hsync_start	= 720 + 42,
	.hsync_end	= 720 + 42 + 8,
	.htotal		= 720 + 42 + 8 + 42,
	.vdisplay	= 1280,
	.vsync_start	= 1280 + 12,
	.vsync_end	= 1280 + 12 + 4,
	.vtotal		= 1280 + 12 + 4 + 18,
	.clock		= 64018,
	.width_mm	= 62,
	.height_mm	= 110,
};

static const struct ltk050h3146w_desc ltk050h3146w_data = {
	.mode = &ltk050h3146w_mode,
	.init = ltk050h3146w_init_sequence,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET,
};

static void ltk050h3146w_a2_select_page(struct mipi_dsi_multi_context *dsi_ctx, int page)
{
	u8 d[4] = { 0xff, 0x98, 0x81, page };

	mipi_dsi_dcs_write_buffer_multi(dsi_ctx, d, ARRAY_SIZE(d));
}

static void ltk050h3146w_a2_write_page(struct mipi_dsi_multi_context *dsi_ctx, int page,
				      const struct ltk050h3146w_cmd *cmds,
				      int num)
{
	ltk050h3146w_a2_select_page(dsi_ctx, page);

	for (int i = 0; i < num; i++)
		mipi_dsi_generic_write_multi(dsi_ctx, &cmds[i],
					     sizeof(struct ltk050h3146w_cmd));
}

static void ltk050h3146w_a2_init_sequence(struct mipi_dsi_multi_context *dsi_ctx)
{
	/*
	 * Init sequence was supplied by the panel vendor without much
	 * documentation.
	 */
	ltk050h3146w_a2_write_page(dsi_ctx, 3, page3_cmds,
					 ARRAY_SIZE(page3_cmds));
	ltk050h3146w_a2_write_page(dsi_ctx, 4, page4_cmds,
					 ARRAY_SIZE(page4_cmds));
	ltk050h3146w_a2_write_page(dsi_ctx, 1, page1_cmds,
					 ARRAY_SIZE(page1_cmds));
	ltk050h3146w_a2_select_page(dsi_ctx, 0);

	/* vendor code called this without param, where there should be one */
	mipi_dsi_dcs_set_tear_on_multi(dsi_ctx, 0);

	mipi_dsi_msleep(dsi_ctx, 60);
}

static const struct drm_display_mode ltk050h3146w_a2_mode = {
	.hdisplay	= 720,
	.hsync_start	= 720 + 42,
	.hsync_end	= 720 + 42 + 10,
	.htotal		= 720 + 42 + 10 + 60,
	.vdisplay	= 1280,
	.vsync_start	= 1280 + 18,
	.vsync_end	= 1280 + 18 + 4,
	.vtotal		= 1280 + 18 + 4 + 12,
	.clock		= 65595,
	.width_mm	= 62,
	.height_mm	= 110,
};

static const struct ltk050h3146w_desc ltk050h3146w_a2_data = {
	.mode = &ltk050h3146w_a2_mode,
	.init = ltk050h3146w_a2_init_sequence,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET,
};

static int ltk050h3146w_unprepare(struct drm_panel *panel)
{
	struct ltk050h3146w *ctx = panel_to_ltk050h3146w(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	if (dsi_ctx.accum_err)
		return dsi_ctx.accum_err;

	regulator_disable(ctx->iovcc);
	regulator_disable(ctx->vci);

	return 0;
}

static int ltk050h3146w_prepare(struct drm_panel *panel)
{
	struct ltk050h3146w *ctx = panel_to_ltk050h3146w(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dev_dbg(ctx->dev, "Resetting the panel\n");
	dsi_ctx.accum_err = regulator_enable(ctx->vci);
	if (dsi_ctx.accum_err) {
		dev_err(ctx->dev, "Failed to enable vci supply: %d\n", dsi_ctx.accum_err);
		return dsi_ctx.accum_err;
	}
	dsi_ctx.accum_err = regulator_enable(ctx->iovcc);
	if (dsi_ctx.accum_err) {
		dev_err(ctx->dev, "Failed to enable iovcc supply: %d\n", dsi_ctx.accum_err);
		goto disable_vci;
	}

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);

	ctx->panel_desc->init(&dsi_ctx);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	/* T9: 120ms */
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);

	if (dsi_ctx.accum_err)
		goto disable_iovcc;

	return 0;

disable_iovcc:
	regulator_disable(ctx->iovcc);
disable_vci:
	regulator_disable(ctx->vci);
	return dsi_ctx.accum_err;
}

static int ltk050h3146w_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	struct ltk050h3146w *ctx = panel_to_ltk050h3146w(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->panel_desc->mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs ltk050h3146w_funcs = {
	.unprepare	= ltk050h3146w_unprepare,
	.prepare	= ltk050h3146w_prepare,
	.get_modes	= ltk050h3146w_get_modes,
};

static int ltk050h3146w_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ltk050h3146w *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->panel_desc = of_device_get_match_data(dev);
	if (!ctx->panel_desc)
		return -EINVAL;

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio), "cannot get reset gpio\n");

	ctx->vci = devm_regulator_get(dev, "vci");
	if (IS_ERR(ctx->vci))
		return dev_err_probe(dev, PTR_ERR(ctx->vci), "Failed to request vci regulator\n");

	ctx->iovcc = devm_regulator_get(dev, "iovcc");
	if (IS_ERR(ctx->iovcc))
		return dev_err_probe(dev, PTR_ERR(ctx->iovcc),
				     "Failed to request iovcc regulator\n");

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = ctx->panel_desc->mode_flags;

	drm_panel_init(&ctx->panel, &dsi->dev, &ltk050h3146w_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach failed: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void ltk050h3146w_remove(struct mipi_dsi_device *dsi)
{
	struct ltk050h3146w *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ltk050h3146w_of_match[] = {
	{
		.compatible = "leadtek,ltk050h3146w",
		.data = &ltk050h3146w_data,
	},
	{
		.compatible = "leadtek,ltk050h3146w-a2",
		.data = &ltk050h3146w_a2_data,
	},
	{
		.compatible = "leadtek,ltk050h3148w",
		.data = &ltk050h3148w_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ltk050h3146w_of_match);

static struct mipi_dsi_driver ltk050h3146w_driver = {
	.driver = {
		.name = "panel-leadtek-ltk050h3146w",
		.of_match_table = ltk050h3146w_of_match,
	},
	.probe	= ltk050h3146w_probe,
	.remove = ltk050h3146w_remove,
};
module_mipi_dsi_driver(ltk050h3146w_driver);

MODULE_AUTHOR("Heiko Stuebner <heiko.stuebner@theobroma-systems.com>");
MODULE_DESCRIPTION("DRM driver for Leadtek LTK050H3146W MIPI DSI panel");
MODULE_LICENSE("GPL v2");
