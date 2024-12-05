// SPDX-License-Identifier: GPL-2.0
/*
 * NewVision NV3052C IPS LCD panel driver
 *
 * Copyright (C) 2020, Paul Cercueil <paul@crapouillou.net>
 * Copyright (C) 2022, Christophe Branchereau <cbranchereau@gmail.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct nv3052c_reg {
	u8 cmd;
	u8 val;
};

struct nv3052c_panel_info {
	const struct drm_display_mode *display_modes;
	unsigned int num_modes;
	u16 width_mm, height_mm;
	u32 bus_format, bus_flags;
	const struct nv3052c_reg *panel_regs;
	unsigned int panel_regs_len;
};

struct nv3052c {
	struct device *dev;
	struct drm_panel panel;
	struct mipi_dbi dbi;
	const struct nv3052c_panel_info *panel_info;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
};

static const struct nv3052c_reg ltk035c5444t_panel_regs[] = {
	// EXTC Command set enable, select page 1
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x01 },
	// Mostly unknown registers
	{ 0xe3, 0x00 },
	{ 0x40, 0x00 },
	{ 0x03, 0x40 },
	{ 0x04, 0x00 },
	{ 0x05, 0x03 },
	{ 0x08, 0x00 },
	{ 0x09, 0x07 },
	{ 0x0a, 0x01 },
	{ 0x0b, 0x32 },
	{ 0x0c, 0x32 },
	{ 0x0d, 0x0b },
	{ 0x0e, 0x00 },
	{ 0x23, 0xa0 },
	{ 0x24, 0x0c },
	{ 0x25, 0x06 },
	{ 0x26, 0x14 },
	{ 0x27, 0x14 },
	{ 0x38, 0xcc }, // VCOM_ADJ1
	{ 0x39, 0xd7 }, // VCOM_ADJ2
	{ 0x3a, 0x4a }, // VCOM_ADJ3
	{ 0x28, 0x40 },
	{ 0x29, 0x01 },
	{ 0x2a, 0xdf },
	{ 0x49, 0x3c },
	{ 0x91, 0x77 }, // EXTPW_CTRL2
	{ 0x92, 0x77 }, // EXTPW_CTRL3
	{ 0xa0, 0x55 },
	{ 0xa1, 0x50 },
	{ 0xa4, 0x9c },
	{ 0xa7, 0x02 },
	{ 0xa8, 0x01 },
	{ 0xa9, 0x01 },
	{ 0xaa, 0xfc },
	{ 0xab, 0x28 },
	{ 0xac, 0x06 },
	{ 0xad, 0x06 },
	{ 0xae, 0x06 },
	{ 0xaf, 0x03 },
	{ 0xb0, 0x08 },
	{ 0xb1, 0x26 },
	{ 0xb2, 0x28 },
	{ 0xb3, 0x28 },
	{ 0xb4, 0x33 },
	{ 0xb5, 0x08 },
	{ 0xb6, 0x26 },
	{ 0xb7, 0x08 },
	{ 0xb8, 0x26 },
	{ 0xf0, 0x00 },
	{ 0xf6, 0xc0 },
	// EXTC Command set enable, select page 2
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x02 },
	// Set gray scale voltage to adjust gamma
	{ 0xb0, 0x0b }, // PGAMVR0
	{ 0xb1, 0x16 }, // PGAMVR1
	{ 0xb2, 0x17 }, // PGAMVR2
	{ 0xb3, 0x2c }, // PGAMVR3
	{ 0xb4, 0x32 }, // PGAMVR4
	{ 0xb5, 0x3b }, // PGAMVR5
	{ 0xb6, 0x29 }, // PGAMPR0
	{ 0xb7, 0x40 }, // PGAMPR1
	{ 0xb8, 0x0d }, // PGAMPK0
	{ 0xb9, 0x05 }, // PGAMPK1
	{ 0xba, 0x12 }, // PGAMPK2
	{ 0xbb, 0x10 }, // PGAMPK3
	{ 0xbc, 0x12 }, // PGAMPK4
	{ 0xbd, 0x15 }, // PGAMPK5
	{ 0xbe, 0x19 }, // PGAMPK6
	{ 0xbf, 0x0e }, // PGAMPK7
	{ 0xc0, 0x16 }, // PGAMPK8
	{ 0xc1, 0x0a }, // PGAMPK9
	// Set gray scale voltage to adjust gamma
	{ 0xd0, 0x0c }, // NGAMVR0
	{ 0xd1, 0x17 }, // NGAMVR0
	{ 0xd2, 0x14 }, // NGAMVR1
	{ 0xd3, 0x2e }, // NGAMVR2
	{ 0xd4, 0x32 }, // NGAMVR3
	{ 0xd5, 0x3c }, // NGAMVR4
	{ 0xd6, 0x22 }, // NGAMPR0
	{ 0xd7, 0x3d }, // NGAMPR1
	{ 0xd8, 0x0d }, // NGAMPK0
	{ 0xd9, 0x07 }, // NGAMPK1
	{ 0xda, 0x13 }, // NGAMPK2
	{ 0xdb, 0x13 }, // NGAMPK3
	{ 0xdc, 0x11 }, // NGAMPK4
	{ 0xdd, 0x15 }, // NGAMPK5
	{ 0xde, 0x19 }, // NGAMPK6
	{ 0xdf, 0x10 }, // NGAMPK7
	{ 0xe0, 0x17 }, // NGAMPK8
	{ 0xe1, 0x0a }, // NGAMPK9
	// EXTC Command set enable, select page 3
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x03 },
	// Set various timing settings
	{ 0x00, 0x2a }, // GIP_VST_1
	{ 0x01, 0x2a }, // GIP_VST_2
	{ 0x02, 0x2a }, // GIP_VST_3
	{ 0x03, 0x2a }, // GIP_VST_4
	{ 0x04, 0x61 }, // GIP_VST_5
	{ 0x05, 0x80 }, // GIP_VST_6
	{ 0x06, 0xc7 }, // GIP_VST_7
	{ 0x07, 0x01 }, // GIP_VST_8
	{ 0x08, 0x03 }, // GIP_VST_9
	{ 0x09, 0x04 }, // GIP_VST_10
	{ 0x70, 0x22 }, // GIP_ECLK1
	{ 0x71, 0x80 }, // GIP_ECLK2
	{ 0x30, 0x2a }, // GIP_CLK_1
	{ 0x31, 0x2a }, // GIP_CLK_2
	{ 0x32, 0x2a }, // GIP_CLK_3
	{ 0x33, 0x2a }, // GIP_CLK_4
	{ 0x34, 0x61 }, // GIP_CLK_5
	{ 0x35, 0xc5 }, // GIP_CLK_6
	{ 0x36, 0x80 }, // GIP_CLK_7
	{ 0x37, 0x23 }, // GIP_CLK_8
	{ 0x40, 0x03 }, // GIP_CLKA_1
	{ 0x41, 0x04 }, // GIP_CLKA_2
	{ 0x42, 0x05 }, // GIP_CLKA_3
	{ 0x43, 0x06 }, // GIP_CLKA_4
	{ 0x44, 0x11 }, // GIP_CLKA_5
	{ 0x45, 0xe8 }, // GIP_CLKA_6
	{ 0x46, 0xe9 }, // GIP_CLKA_7
	{ 0x47, 0x11 }, // GIP_CLKA_8
	{ 0x48, 0xea }, // GIP_CLKA_9
	{ 0x49, 0xeb }, // GIP_CLKA_10
	{ 0x50, 0x07 }, // GIP_CLKB_1
	{ 0x51, 0x08 }, // GIP_CLKB_2
	{ 0x52, 0x09 }, // GIP_CLKB_3
	{ 0x53, 0x0a }, // GIP_CLKB_4
	{ 0x54, 0x11 }, // GIP_CLKB_5
	{ 0x55, 0xec }, // GIP_CLKB_6
	{ 0x56, 0xed }, // GIP_CLKB_7
	{ 0x57, 0x11 }, // GIP_CLKB_8
	{ 0x58, 0xef }, // GIP_CLKB_9
	{ 0x59, 0xf0 }, // GIP_CLKB_10
	// Map internal GOA signals to GOA output pad
	{ 0xb1, 0x01 }, // PANELD2U2
	{ 0xb4, 0x15 }, // PANELD2U5
	{ 0xb5, 0x16 }, // PANELD2U6
	{ 0xb6, 0x09 }, // PANELD2U7
	{ 0xb7, 0x0f }, // PANELD2U8
	{ 0xb8, 0x0d }, // PANELD2U9
	{ 0xb9, 0x0b }, // PANELD2U10
	{ 0xba, 0x00 }, // PANELD2U11
	{ 0xc7, 0x02 }, // PANELD2U24
	{ 0xca, 0x17 }, // PANELD2U27
	{ 0xcb, 0x18 }, // PANELD2U28
	{ 0xcc, 0x0a }, // PANELD2U29
	{ 0xcd, 0x10 }, // PANELD2U30
	{ 0xce, 0x0e }, // PANELD2U31
	{ 0xcf, 0x0c }, // PANELD2U32
	{ 0xd0, 0x00 }, // PANELD2U33
	// Map internal GOA signals to GOA output pad
	{ 0x81, 0x00 }, // PANELU2D2
	{ 0x84, 0x15 }, // PANELU2D5
	{ 0x85, 0x16 }, // PANELU2D6
	{ 0x86, 0x10 }, // PANELU2D7
	{ 0x87, 0x0a }, // PANELU2D8
	{ 0x88, 0x0c }, // PANELU2D9
	{ 0x89, 0x0e }, // PANELU2D10
	{ 0x8a, 0x02 }, // PANELU2D11
	{ 0x97, 0x00 }, // PANELU2D24
	{ 0x9a, 0x17 }, // PANELU2D27
	{ 0x9b, 0x18 }, // PANELU2D28
	{ 0x9c, 0x0f }, // PANELU2D29
	{ 0x9d, 0x09 }, // PANELU2D30
	{ 0x9e, 0x0b }, // PANELU2D31
	{ 0x9f, 0x0d }, // PANELU2D32
	{ 0xa0, 0x01 }, // PANELU2D33
	// EXTC Command set enable, select page 2
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x02 },
	// Unknown registers
	{ 0x01, 0x01 },
	{ 0x02, 0xda },
	{ 0x03, 0xba },
	{ 0x04, 0xa8 },
	{ 0x05, 0x9a },
	{ 0x06, 0x70 },
	{ 0x07, 0xff },
	{ 0x08, 0x91 },
	{ 0x09, 0x90 },
	{ 0x0a, 0xff },
	{ 0x0b, 0x8f },
	{ 0x0c, 0x60 },
	{ 0x0d, 0x58 },
	{ 0x0e, 0x48 },
	{ 0x0f, 0x38 },
	{ 0x10, 0x2b },
	// EXTC Command set enable, select page 0
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x00 },
	// Display Access Control
	{ 0x36, 0x0a }, // bgr = 1, ss = 1, gs = 0
};

static const struct nv3052c_reg fs035vg158_panel_regs[] = {
	// EXTC Command set enable, select page 1
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x01 },
	// Mostly unknown registers
	{ 0xe3, 0x00 },
	{ 0x40, 0x00 },
	{ 0x03, 0x40 },
	{ 0x04, 0x00 },
	{ 0x05, 0x03 },
	{ 0x08, 0x00 },
	{ 0x09, 0x07 },
	{ 0x0a, 0x01 },
	{ 0x0b, 0x32 },
	{ 0x0c, 0x32 },
	{ 0x0d, 0x0b },
	{ 0x0e, 0x00 },
	{ 0x23, 0x20 }, // RGB interface control: DE MODE PCLK-N
	{ 0x24, 0x0c },
	{ 0x25, 0x06 },
	{ 0x26, 0x14 },
	{ 0x27, 0x14 },
	{ 0x38, 0x9c }, //VCOM_ADJ1, different to ltk035c5444t
	{ 0x39, 0xa7 }, //VCOM_ADJ2, different to ltk035c5444t
	{ 0x3a, 0x50 }, //VCOM_ADJ3, different to ltk035c5444t
	{ 0x28, 0x40 },
	{ 0x29, 0x01 },
	{ 0x2a, 0xdf },
	{ 0x49, 0x3c },
	{ 0x91, 0x57 }, //EXTPW_CTRL2, different to ltk035c5444t
	{ 0x92, 0x57 }, //EXTPW_CTRL3, different to ltk035c5444t
	{ 0xa0, 0x55 },
	{ 0xa1, 0x50 },
	{ 0xa4, 0x9c },
	{ 0xa7, 0x02 },
	{ 0xa8, 0x01 },
	{ 0xa9, 0x01 },
	{ 0xaa, 0xfc },
	{ 0xab, 0x28 },
	{ 0xac, 0x06 },
	{ 0xad, 0x06 },
	{ 0xae, 0x06 },
	{ 0xaf, 0x03 },
	{ 0xb0, 0x08 },
	{ 0xb1, 0x26 },
	{ 0xb2, 0x28 },
	{ 0xb3, 0x28 },
	{ 0xb4, 0x03 }, // Unknown, different to ltk035c5444
	{ 0xb5, 0x08 },
	{ 0xb6, 0x26 },
	{ 0xb7, 0x08 },
	{ 0xb8, 0x26 },
	{ 0xf0, 0x00 },
	{ 0xf6, 0xc0 },
	// EXTC Command set enable, select page 0
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x02 },
	// Set gray scale voltage to adjust gamma
	{ 0xb0, 0x0b }, // PGAMVR0
	{ 0xb1, 0x16 }, // PGAMVR1
	{ 0xb2, 0x17 }, // PGAMVR2
	{ 0xb3, 0x2c }, // PGAMVR3
	{ 0xb4, 0x32 }, // PGAMVR4
	{ 0xb5, 0x3b }, // PGAMVR5
	{ 0xb6, 0x29 }, // PGAMPR0
	{ 0xb7, 0x40 }, // PGAMPR1
	{ 0xb8, 0x0d }, // PGAMPK0
	{ 0xb9, 0x05 }, // PGAMPK1
	{ 0xba, 0x12 }, // PGAMPK2
	{ 0xbb, 0x10 }, // PGAMPK3
	{ 0xbc, 0x12 }, // PGAMPK4
	{ 0xbd, 0x15 }, // PGAMPK5
	{ 0xbe, 0x19 }, // PGAMPK6
	{ 0xbf, 0x0e }, // PGAMPK7
	{ 0xc0, 0x16 }, // PGAMPK8
	{ 0xc1, 0x0a }, // PGAMPK9
	// Set gray scale voltage to adjust gamma
	{ 0xd0, 0x0c }, // NGAMVR0
	{ 0xd1, 0x17 }, // NGAMVR0
	{ 0xd2, 0x14 }, // NGAMVR1
	{ 0xd3, 0x2e }, // NGAMVR2
	{ 0xd4, 0x32 }, // NGAMVR3
	{ 0xd5, 0x3c }, // NGAMVR4
	{ 0xd6, 0x22 }, // NGAMPR0
	{ 0xd7, 0x3d }, // NGAMPR1
	{ 0xd8, 0x0d }, // NGAMPK0
	{ 0xd9, 0x07 }, // NGAMPK1
	{ 0xda, 0x13 }, // NGAMPK2
	{ 0xdb, 0x13 }, // NGAMPK3
	{ 0xdc, 0x11 }, // NGAMPK4
	{ 0xdd, 0x15 }, // NGAMPK5
	{ 0xde, 0x19 }, // NGAMPK6
	{ 0xdf, 0x10 }, // NGAMPK7
	{ 0xe0, 0x17 }, // NGAMPK8
	{ 0xe1, 0x0a }, // NGAMPK9
	// EXTC Command set enable, select page 3
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x03 },
	// Set various timing settings
	{ 0x00, 0x2a }, // GIP_VST_1
	{ 0x01, 0x2a }, // GIP_VST_2
	{ 0x02, 0x2a }, // GIP_VST_3
	{ 0x03, 0x2a }, // GIP_VST_4
	{ 0x04, 0x61 }, // GIP_VST_5
	{ 0x05, 0x80 }, // GIP_VST_6
	{ 0x06, 0xc7 }, // GIP_VST_7
	{ 0x07, 0x01 }, // GIP_VST_8
	{ 0x08, 0x03 }, // GIP_VST_9
	{ 0x09, 0x04 }, // GIP_VST_10
	{ 0x70, 0x22 }, // GIP_ECLK1
	{ 0x71, 0x80 }, // GIP_ECLK2
	{ 0x30, 0x2a }, // GIP_CLK_1
	{ 0x31, 0x2a }, // GIP_CLK_2
	{ 0x32, 0x2a }, // GIP_CLK_3
	{ 0x33, 0x2a }, // GIP_CLK_4
	{ 0x34, 0x61 }, // GIP_CLK_5
	{ 0x35, 0xc5 }, // GIP_CLK_6
	{ 0x36, 0x80 }, // GIP_CLK_7
	{ 0x37, 0x23 }, // GIP_CLK_8
	{ 0x40, 0x03 }, // GIP_CLKA_1
	{ 0x41, 0x04 }, // GIP_CLKA_2
	{ 0x42, 0x05 }, // GIP_CLKA_3
	{ 0x43, 0x06 }, // GIP_CLKA_4
	{ 0x44, 0x11 }, // GIP_CLKA_5
	{ 0x45, 0xe8 }, // GIP_CLKA_6
	{ 0x46, 0xe9 }, // GIP_CLKA_7
	{ 0x47, 0x11 }, // GIP_CLKA_8
	{ 0x48, 0xea }, // GIP_CLKA_9
	{ 0x49, 0xeb }, // GIP_CLKA_10
	{ 0x50, 0x07 }, // GIP_CLKB_1
	{ 0x51, 0x08 }, // GIP_CLKB_2
	{ 0x52, 0x09 }, // GIP_CLKB_3
	{ 0x53, 0x0a }, // GIP_CLKB_4
	{ 0x54, 0x11 }, // GIP_CLKB_5
	{ 0x55, 0xec }, // GIP_CLKB_6
	{ 0x56, 0xed }, // GIP_CLKB_7
	{ 0x57, 0x11 }, // GIP_CLKB_8
	{ 0x58, 0xef }, // GIP_CLKB_9
	{ 0x59, 0xf0 }, // GIP_CLKB_10
	// Map internal GOA signals to GOA output pad
	{ 0xb1, 0x01 }, // PANELD2U2
	{ 0xb4, 0x15 }, // PANELD2U5
	{ 0xb5, 0x16 }, // PANELD2U6
	{ 0xb6, 0x09 }, // PANELD2U7
	{ 0xb7, 0x0f }, // PANELD2U8
	{ 0xb8, 0x0d }, // PANELD2U9
	{ 0xb9, 0x0b }, // PANELD2U10
	{ 0xba, 0x00 }, // PANELD2U11
	{ 0xc7, 0x02 }, // PANELD2U24
	{ 0xca, 0x17 }, // PANELD2U27
	{ 0xcb, 0x18 }, // PANELD2U28
	{ 0xcc, 0x0a }, // PANELD2U29
	{ 0xcd, 0x10 }, // PANELD2U30
	{ 0xce, 0x0e }, // PANELD2U31
	{ 0xcf, 0x0c }, // PANELD2U32
	{ 0xd0, 0x00 }, // PANELD2U33
	// Map internal GOA signals to GOA output pad
	{ 0x81, 0x00 }, // PANELU2D2
	{ 0x84, 0x15 }, // PANELU2D5
	{ 0x85, 0x16 }, // PANELU2D6
	{ 0x86, 0x10 }, // PANELU2D7
	{ 0x87, 0x0a }, // PANELU2D8
	{ 0x88, 0x0c }, // PANELU2D9
	{ 0x89, 0x0e }, // PANELU2D10
	{ 0x8a, 0x02 }, // PANELU2D11
	{ 0x97, 0x00 }, // PANELU2D24
	{ 0x9a, 0x17 }, // PANELU2D27
	{ 0x9b, 0x18 }, // PANELU2D28
	{ 0x9c, 0x0f }, // PANELU2D29
	{ 0x9d, 0x09 }, // PANELU2D30
	{ 0x9e, 0x0b }, // PANELU2D31
	{ 0x9f, 0x0d }, // PANELU2D32
	{ 0xa0, 0x01 }, // PANELU2D33
	// EXTC Command set enable, select page 2
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x02 },
	// Unknown registers
	{ 0x01, 0x01 },
	{ 0x02, 0xda },
	{ 0x03, 0xba },
	{ 0x04, 0xa8 },
	{ 0x05, 0x9a },
	{ 0x06, 0x70 },
	{ 0x07, 0xff },
	{ 0x08, 0x91 },
	{ 0x09, 0x90 },
	{ 0x0a, 0xff },
	{ 0x0b, 0x8f },
	{ 0x0c, 0x60 },
	{ 0x0d, 0x58 },
	{ 0x0e, 0x48 },
	{ 0x0f, 0x38 },
	{ 0x10, 0x2b },
	// EXTC Command set enable, select page 0
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x00 },
	// Display Access Control
	{ 0x36, 0x0a }, // bgr = 1, ss = 1, gs = 0
};


static const struct nv3052c_reg wl_355608_a8_panel_regs[] = {
	// EXTC Command set enable, select page 1
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x01 },
	// Mostly unknown registers
	{ 0xe3, 0x00 },
	{ 0x40, 0x00 },
	{ 0x03, 0x40 },
	{ 0x04, 0x00 },
	{ 0x05, 0x03 },
	{ 0x08, 0x00 },
	{ 0x09, 0x07 },
	{ 0x0a, 0x01 },
	{ 0x0b, 0x32 },
	{ 0x0c, 0x32 },
	{ 0x0d, 0x0b },
	{ 0x0e, 0x00 },
	{ 0x23, 0xa0 },
	{ 0x24, 0x0c },
	{ 0x25, 0x06 },
	{ 0x26, 0x14 },
	{ 0x27, 0x14 },
	{ 0x38, 0xcc }, // VCOM_ADJ1
	{ 0x39, 0xd7 }, // VCOM_ADJ2
	{ 0x3a, 0x44 }, // VCOM_ADJ3
	{ 0x28, 0x40 },
	{ 0x29, 0x01 },
	{ 0x2a, 0xdf },
	{ 0x49, 0x3c },
	{ 0x91, 0x77 }, // EXTPW_CTRL2
	{ 0x92, 0x77 }, // EXTPW_CTRL3
	{ 0xa0, 0x55 },
	{ 0xa1, 0x50 },
	{ 0xa4, 0x9c },
	{ 0xa7, 0x02 },
	{ 0xa8, 0x01 },
	{ 0xa9, 0x01 },
	{ 0xaa, 0xfc },
	{ 0xab, 0x28 },
	{ 0xac, 0x06 },
	{ 0xad, 0x06 },
	{ 0xae, 0x06 },
	{ 0xaf, 0x03 },
	{ 0xb0, 0x08 },
	{ 0xb1, 0x26 },
	{ 0xb2, 0x28 },
	{ 0xb3, 0x28 },
	{ 0xb4, 0x33 },
	{ 0xb5, 0x08 },
	{ 0xb6, 0x26 },
	{ 0xb7, 0x08 },
	{ 0xb8, 0x26 },
	{ 0xf0, 0x00 },
	{ 0xf6, 0xc0 },
	// EXTC Command set enable, select page 2
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x02 },
	// Set gray scale voltage to adjust gamma
	{ 0xb0, 0x0b }, // PGAMVR0
	{ 0xb1, 0x16 }, // PGAMVR1
	{ 0xb2, 0x17 }, // PGAMVR2
	{ 0xb3, 0x2c }, // PGAMVR3
	{ 0xb4, 0x32 }, // PGAMVR4
	{ 0xb5, 0x3b }, // PGAMVR5
	{ 0xb6, 0x29 }, // PGAMPR0
	{ 0xb7, 0x40 }, // PGAMPR1
	{ 0xb8, 0x0d }, // PGAMPK0
	{ 0xb9, 0x05 }, // PGAMPK1
	{ 0xba, 0x12 }, // PGAMPK2
	{ 0xbb, 0x10 }, // PGAMPK3
	{ 0xbc, 0x12 }, // PGAMPK4
	{ 0xbd, 0x15 }, // PGAMPK5
	{ 0xbe, 0x19 }, // PGAMPK6
	{ 0xbf, 0x0e }, // PGAMPK7
	{ 0xc0, 0x16 }, // PGAMPK8
	{ 0xc1, 0x0a }, // PGAMPK9
	// Set gray scale voltage to adjust gamma
	{ 0xd0, 0x0c }, // NGAMVR0
	{ 0xd1, 0x17 }, // NGAMVR0
	{ 0xd2, 0x14 }, // NGAMVR1
	{ 0xd3, 0x2e }, // NGAMVR2
	{ 0xd4, 0x32 }, // NGAMVR3
	{ 0xd5, 0x3c }, // NGAMVR4
	{ 0xd6, 0x22 }, // NGAMPR0
	{ 0xd7, 0x3d }, // NGAMPR1
	{ 0xd8, 0x0d }, // NGAMPK0
	{ 0xd9, 0x07 }, // NGAMPK1
	{ 0xda, 0x13 }, // NGAMPK2
	{ 0xdb, 0x13 }, // NGAMPK3
	{ 0xdc, 0x11 }, // NGAMPK4
	{ 0xdd, 0x15 }, // NGAMPK5
	{ 0xde, 0x19 }, // NGAMPK6
	{ 0xdf, 0x10 }, // NGAMPK7
	{ 0xe0, 0x17 }, // NGAMPK8
	{ 0xe1, 0x0a }, // NGAMPK9
	// EXTC Command set enable, select page 3
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x03 },
	// Set various timing settings
	{ 0x00, 0x2a }, // GIP_VST_1
	{ 0x01, 0x2a }, // GIP_VST_2
	{ 0x02, 0x2a }, // GIP_VST_3
	{ 0x03, 0x2a }, // GIP_VST_4
	{ 0x04, 0x61 }, // GIP_VST_5
	{ 0x05, 0x80 }, // GIP_VST_6
	{ 0x06, 0xc7 }, // GIP_VST_7
	{ 0x07, 0x01 }, // GIP_VST_8
	{ 0x08, 0x03 }, // GIP_VST_9
	{ 0x09, 0x04 }, // GIP_VST_10
	{ 0x70, 0x22 }, // GIP_ECLK1
	{ 0x71, 0x80 }, // GIP_ECLK2
	{ 0x30, 0x2a }, // GIP_CLK_1
	{ 0x31, 0x2a }, // GIP_CLK_2
	{ 0x32, 0x2a }, // GIP_CLK_3
	{ 0x33, 0x2a }, // GIP_CLK_4
	{ 0x34, 0x61 }, // GIP_CLK_5
	{ 0x35, 0xc5 }, // GIP_CLK_6
	{ 0x36, 0x80 }, // GIP_CLK_7
	{ 0x37, 0x23 }, // GIP_CLK_8
	{ 0x40, 0x03 }, // GIP_CLKA_1
	{ 0x41, 0x04 }, // GIP_CLKA_2
	{ 0x42, 0x05 }, // GIP_CLKA_3
	{ 0x43, 0x06 }, // GIP_CLKA_4
	{ 0x44, 0x11 }, // GIP_CLKA_5
	{ 0x45, 0xe8 }, // GIP_CLKA_6
	{ 0x46, 0xe9 }, // GIP_CLKA_7
	{ 0x47, 0x11 }, // GIP_CLKA_8
	{ 0x48, 0xea }, // GIP_CLKA_9
	{ 0x49, 0xeb }, // GIP_CLKA_10
	{ 0x50, 0x07 }, // GIP_CLKB_1
	{ 0x51, 0x08 }, // GIP_CLKB_2
	{ 0x52, 0x09 }, // GIP_CLKB_3
	{ 0x53, 0x0a }, // GIP_CLKB_4
	{ 0x54, 0x11 }, // GIP_CLKB_5
	{ 0x55, 0xec }, // GIP_CLKB_6
	{ 0x56, 0xed }, // GIP_CLKB_7
	{ 0x57, 0x11 }, // GIP_CLKB_8
	{ 0x58, 0xef }, // GIP_CLKB_9
	{ 0x59, 0xf0 }, // GIP_CLKB_10
	// Map internal GOA signals to GOA output pad
	{ 0xb1, 0x01 }, // PANELD2U2
	{ 0xb4, 0x15 }, // PANELD2U5
	{ 0xb5, 0x16 }, // PANELD2U6
	{ 0xb6, 0x09 }, // PANELD2U7
	{ 0xb7, 0x0f }, // PANELD2U8
	{ 0xb8, 0x0d }, // PANELD2U9
	{ 0xb9, 0x0b }, // PANELD2U10
	{ 0xba, 0x00 }, // PANELD2U11
	{ 0xc7, 0x02 }, // PANELD2U24
	{ 0xca, 0x17 }, // PANELD2U27
	{ 0xcb, 0x18 }, // PANELD2U28
	{ 0xcc, 0x0a }, // PANELD2U29
	{ 0xcd, 0x10 }, // PANELD2U30
	{ 0xce, 0x0e }, // PANELD2U31
	{ 0xcf, 0x0c }, // PANELD2U32
	{ 0xd0, 0x00 }, // PANELD2U33
	// Map internal GOA signals to GOA output pad
	{ 0x81, 0x00 }, // PANELU2D2
	{ 0x84, 0x15 }, // PANELU2D5
	{ 0x85, 0x16 }, // PANELU2D6
	{ 0x86, 0x10 }, // PANELU2D7
	{ 0x87, 0x0a }, // PANELU2D8
	{ 0x88, 0x0c }, // PANELU2D9
	{ 0x89, 0x0e }, // PANELU2D10
	{ 0x8a, 0x02 }, // PANELU2D11
	{ 0x97, 0x00 }, // PANELU2D24
	{ 0x9a, 0x17 }, // PANELU2D27
	{ 0x9b, 0x18 }, // PANELU2D28
	{ 0x9c, 0x0f }, // PANELU2D29
	{ 0x9d, 0x09 }, // PANELU2D30
	{ 0x9e, 0x0b }, // PANELU2D31
	{ 0x9f, 0x0d }, // PANELU2D32
	{ 0xa0, 0x01 }, // PANELU2D33
	// EXTC Command set enable, select page 2
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x02 },
	// Unknown registers
	{ 0x01, 0x01 },
	{ 0x02, 0xda },
	{ 0x03, 0xba },
	{ 0x04, 0xa8 },
	{ 0x05, 0x9a },
	{ 0x06, 0x70 },
	{ 0x07, 0xff },
	{ 0x08, 0x91 },
	{ 0x09, 0x90 },
	{ 0x0a, 0xff },
	{ 0x0b, 0x8f },
	{ 0x0c, 0x60 },
	{ 0x0d, 0x58 },
	{ 0x0e, 0x48 },
	{ 0x0f, 0x38 },
	{ 0x10, 0x2b },
	// EXTC Command set enable, select page 0
	{ 0xff, 0x30 }, { 0xff, 0x52 }, { 0xff, 0x00 },
	// Display Access Control
	{ 0x36, 0x0a }, // bgr = 1, ss = 1, gs = 0
};

static inline struct nv3052c *to_nv3052c(struct drm_panel *panel)
{
	return container_of(panel, struct nv3052c, panel);
}

static int nv3052c_prepare(struct drm_panel *panel)
{
	struct nv3052c *priv = to_nv3052c(panel);
	const struct nv3052c_reg *panel_regs = priv->panel_info->panel_regs;
	unsigned int panel_regs_len = priv->panel_info->panel_regs_len;
	struct mipi_dbi *dbi = &priv->dbi;
	unsigned int i;
	int err;

	err = regulator_enable(priv->supply);
	if (err) {
		dev_err(priv->dev, "Failed to enable power supply: %d\n", err);
		return err;
	}

	/* Reset the chip */
	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(10, 1000);
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(5000, 20000);

	for (i = 0; i < panel_regs_len; i++) {
		err = mipi_dbi_command(dbi, panel_regs[i].cmd,
				       panel_regs[i].val);

		if (err) {
			dev_err(priv->dev, "Unable to set register: %d\n", err);
			goto err_disable_regulator;
		}
	}

	err = mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	if (err) {
		dev_err(priv->dev, "Unable to exit sleep mode: %d\n", err);
		goto err_disable_regulator;
	}

	return 0;

err_disable_regulator:
	regulator_disable(priv->supply);
	return err;
}

static int nv3052c_unprepare(struct drm_panel *panel)
{
	struct nv3052c *priv = to_nv3052c(panel);
	struct mipi_dbi *dbi = &priv->dbi;
	int err;

	err = mipi_dbi_command(dbi, MIPI_DCS_ENTER_SLEEP_MODE);
	if (err)
		dev_err(priv->dev, "Unable to enter sleep mode: %d\n", err);

	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	regulator_disable(priv->supply);

	return 0;
}

static int nv3052c_enable(struct drm_panel *panel)
{
	struct nv3052c *priv = to_nv3052c(panel);
	struct mipi_dbi *dbi = &priv->dbi;
	int err;

	err = mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);
	if (err) {
		dev_err(priv->dev, "Unable to enable display: %d\n", err);
		return err;
	}

	if (panel->backlight) {
		/* Wait for the picture to be ready before enabling backlight */
		msleep(120);
	}

	return 0;
}

static int nv3052c_disable(struct drm_panel *panel)
{
	struct nv3052c *priv = to_nv3052c(panel);
	struct mipi_dbi *dbi = &priv->dbi;
	int err;

	err = mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);
	if (err) {
		dev_err(priv->dev, "Unable to disable display: %d\n", err);
		return err;
	}

	return 0;
}

static int nv3052c_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct nv3052c *priv = to_nv3052c(panel);
	const struct nv3052c_panel_info *panel_info = priv->panel_info;
	struct drm_display_mode *mode;
	unsigned int i;

	for (i = 0; i < panel_info->num_modes; i++) {
		mode = drm_mode_duplicate(connector->dev,
					  &panel_info->display_modes[i]);
		if (!mode)
			return -ENOMEM;

		drm_mode_set_name(mode);

		mode->type = DRM_MODE_TYPE_DRIVER;
		if (panel_info->num_modes == 1)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.bpc = 8;
	connector->display_info.width_mm = panel_info->width_mm;
	connector->display_info.height_mm = panel_info->height_mm;

	drm_display_info_set_bus_formats(&connector->display_info,
					 &panel_info->bus_format, 1);
	connector->display_info.bus_flags = panel_info->bus_flags;

	return panel_info->num_modes;
}

static const struct drm_panel_funcs nv3052c_funcs = {
	.prepare	= nv3052c_prepare,
	.unprepare	= nv3052c_unprepare,
	.enable		= nv3052c_enable,
	.disable	= nv3052c_disable,
	.get_modes	= nv3052c_get_modes,
};

static int nv3052c_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct nv3052c *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	priv->panel_info = of_device_get_match_data(dev);
	if (!priv->panel_info)
		return -EINVAL;

	priv->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(priv->supply))
		return dev_err_probe(dev, PTR_ERR(priv->supply), "Failed to get power supply\n");

	priv->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->reset_gpio), "Failed to get reset GPIO\n");

	err = mipi_dbi_spi_init(spi, &priv->dbi, NULL);
	if (err)
		return dev_err_probe(dev, err, "MIPI DBI init failed\n");

	priv->dbi.read_commands = NULL;

	spi_set_drvdata(spi, priv);

	drm_panel_init(&priv->panel, dev, &nv3052c_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	err = drm_panel_of_backlight(&priv->panel);
	if (err)
		return dev_err_probe(dev, err, "Failed to attach backlight\n");

	drm_panel_add(&priv->panel);

	return 0;
}

static void nv3052c_remove(struct spi_device *spi)
{
	struct nv3052c *priv = spi_get_drvdata(spi);

	drm_panel_remove(&priv->panel);
	drm_panel_disable(&priv->panel);
	drm_panel_unprepare(&priv->panel);
}

static const struct drm_display_mode ltk035c5444t_modes[] = {
	{ /* 60 Hz */
		.clock = 24000,
		.hdisplay = 640,
		.hsync_start = 640 + 96,
		.hsync_end = 640 + 96 + 16,
		.htotal = 640 + 96 + 16 + 48,
		.vdisplay = 480,
		.vsync_start = 480 + 5,
		.vsync_end = 480 + 5 + 2,
		.vtotal = 480 + 5 + 2 + 13,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
	{ /* 50 Hz */
		.clock = 18000,
		.hdisplay = 640,
		.hsync_start = 640 + 39,
		.hsync_end = 640 + 39 + 2,
		.htotal = 640 + 39 + 2 + 39,
		.vdisplay = 480,
		.vsync_start = 480 + 5,
		.vsync_end = 480 + 5 + 2,
		.vtotal = 480 + 5 + 2 + 13,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
};

static const struct drm_display_mode fs035vg158_modes[] = {
	{ /* 60 Hz */
		.clock = 21000,
		.hdisplay = 640,
		.hsync_start = 640 + 34,
		.hsync_end = 640 + 34 + 4,
		.htotal = 640 + 34 + 4 + 20,
		.vdisplay = 480,
		.vsync_start = 480 + 12,
		.vsync_end = 480 + 12 + 4,
		.vtotal = 480 + 12 + 4 + 6,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
};

static const struct drm_display_mode wl_355608_a8_mode[] = {
	{
		.clock = 24000,
		.hdisplay = 640,
		.hsync_start = 640 + 64,
		.hsync_end = 640 + 64 + 20,
		.htotal = 640 + 64 + 20 + 46,
		.vdisplay = 480,
		.vsync_start = 480 + 21,
		.vsync_end = 480 + 21 + 4,
		.vtotal = 480 + 21 + 4 + 15,
		.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
	},
};

static const struct nv3052c_panel_info ltk035c5444t_panel_info = {
	.display_modes = ltk035c5444t_modes,
	.num_modes = ARRAY_SIZE(ltk035c5444t_modes),
	.width_mm = 77,
	.height_mm = 64,
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE,
	.panel_regs = ltk035c5444t_panel_regs,
	.panel_regs_len = ARRAY_SIZE(ltk035c5444t_panel_regs),
};

static const struct nv3052c_panel_info fs035vg158_panel_info = {
	.display_modes = fs035vg158_modes,
	.num_modes = ARRAY_SIZE(fs035vg158_modes),
	.width_mm = 70,
	.height_mm = 53,
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE,
	.panel_regs = fs035vg158_panel_regs,
	.panel_regs_len = ARRAY_SIZE(fs035vg158_panel_regs),
};

static const struct nv3052c_panel_info wl_355608_a8_panel_info = {
	.display_modes = wl_355608_a8_mode,
	.num_modes = ARRAY_SIZE(wl_355608_a8_mode),
	.width_mm = 150,
	.height_mm = 94,
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE,
	.panel_regs = wl_355608_a8_panel_regs,
	.panel_regs_len = ARRAY_SIZE(wl_355608_a8_panel_regs),
};

static const struct spi_device_id nv3052c_ids[] = {
	{ "ltk035c5444t", },
	{ "fs035vg158", },
	{ "rg35xx-plus-panel", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, nv3052c_ids);

static const struct of_device_id nv3052c_of_match[] = {
	{ .compatible = "leadtek,ltk035c5444t", .data = &ltk035c5444t_panel_info },
	{ .compatible = "fascontek,fs035vg158", .data = &fs035vg158_panel_info },
	{ .compatible = "anbernic,rg35xx-plus-panel", .data = &wl_355608_a8_panel_info },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nv3052c_of_match);

static struct spi_driver nv3052c_driver = {
	.driver = {
		.name = "nv3052c",
		.of_match_table = nv3052c_of_match,
	},
	.id_table = nv3052c_ids,
	.probe = nv3052c_probe,
	.remove = nv3052c_remove,
};
module_spi_driver(nv3052c_driver);

MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_AUTHOR("Christophe Branchereau <cbranchereau@gmail.com>");
MODULE_AUTHOR("Ryan Walklin <ryan@testtoast.com");
MODULE_DESCRIPTION("NewVision NV3052C IPS LCD panel driver");
MODULE_LICENSE("GPL v2");
