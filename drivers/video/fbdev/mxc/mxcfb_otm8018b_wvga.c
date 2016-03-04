/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/mipi_dsi.h>
#include <linux/mxcfb.h>
#include <linux/backlight.h>
#include <video/mipi_display.h>

#include "mipi_dsi.h"

#define OTM8018B_TWO_DATA_LANE					(0x2)
#define OTM8018B_MAX_DPHY_CLK					(800)

#define CHECK_RETCODE(ret)					\
do {								\
	if (ret < 0) {						\
		dev_err(&mipi_dsi->pdev->dev,			\
			"%s ERR: ret:%d, line:%d.\n",		\
			__func__, ret, __LINE__);		\
		return ret;					\
	}							\
} while (0)

static void parse_variadic(int n, u8 *buf, ...)
{
	int i = 0;
	va_list args;

	if (unlikely(!n)) return;

	va_start(args, buf);

	for (i = 0; i < n; i++)
		buf[i + 1] = (u8)va_arg(args, int);

	va_end(args);
}

#define TC358763_DCS_write_1A_nP(n, addr, ...) {		\
	int err;						\
								\
	buf[0] = addr;						\
	parse_variadic(n, buf, ##__VA_ARGS__);			\
								\
	err = mipi_dsi->mipi_dsi_pkt_write(mipi_dsi,		\
		MIPI_DSI_GENERIC_LONG_WRITE, (u32*)buf, n + 1);	\
	CHECK_RETCODE(err);					\
}

#define TC358763_DCS_write_1A_0P(addr)		\
	TC358763_DCS_write_1A_nP(0, addr)

#define TC358763_DCS_write_1A_1P(addr, ...)	\
	TC358763_DCS_write_1A_nP(1, addr, __VA_ARGS__)

#define TC358763_DCS_write_1A_2P(addr, ...)	\
	TC358763_DCS_write_1A_nP(2, addr, __VA_ARGS__)

#define TC358763_DCS_write_1A_3P(addr, ...)	\
	TC358763_DCS_write_1A_nP(3, addr, __VA_ARGS__)

#define TC358763_DCS_write_1A_5P(addr, ...)	\
	TC358763_DCS_write_1A_nP(5, addr, __VA_ARGS__)

#define TC358763_DCS_write_1A_6P(addr, ...)	\
	TC358763_DCS_write_1A_nP(6, addr, __VA_ARGS__)

#define TC358763_DCS_write_1A_12P(addr, ...)	\
	TC358763_DCS_write_1A_nP(12, addr, __VA_ARGS__)

#define TC358763_DCS_write_1A_14P(addr, ...)	\
	TC358763_DCS_write_1A_nP(14, addr, __VA_ARGS__)

static int otm8018bbl_brightness;

static struct fb_videomode truly_lcd_modedb[] = {
	{
	 "TRULY-WVGA", 64, 480, 800, 37880,
	 8, 8,
	 6, 6,
	 8, 6,
	 FB_SYNC_OE_LOW_ACT,
	 FB_VMODE_NONINTERLACED,
	 0,
	},
};

static struct mipi_lcd_config lcd_config = {
	.virtual_ch	= 0x0,
	.data_lane_num  = OTM8018B_TWO_DATA_LANE,
	.max_phy_clk    = OTM8018B_MAX_DPHY_CLK,
	.dpi_fmt	= MIPI_RGB888,
};

void mipid_otm8018b_get_lcd_videomode(struct fb_videomode **mode, int *size,
		struct mipi_lcd_config **data)
{
	*mode = &truly_lcd_modedb[0];
	*size = ARRAY_SIZE(truly_lcd_modedb);
	*data = &lcd_config;
}

int mipid_otm8018b_lcd_setup(struct mipi_dsi_info *mipi_dsi)
{
	u8 buf[DSI_CMD_BUF_MAXSIZE];

	dev_dbg(&mipi_dsi->pdev->dev, "MIPI DSI LCD setup.\n");

	TC358763_DCS_write_1A_3P(0xFF,0x80,0x09,0x01);
	TC358763_DCS_write_1A_1P(0x00,0x80);
	TC358763_DCS_write_1A_2P(0xFF,0x80,0x09);

	TC358763_DCS_write_1A_1P(0x00,0x03);
	TC358763_DCS_write_1A_1P(0xff,0x01);

	TC358763_DCS_write_1A_1P(0x00,0xb4);
	TC358763_DCS_write_1A_1P(0xc0,0x10);

	TC358763_DCS_write_1A_1P(0x00,0x82);
	TC358763_DCS_write_1A_1P(0xC5,0xa3);

	TC358763_DCS_write_1A_1P(0x00,0x90);
	TC358763_DCS_write_1A_2P(0xC5,0x96,0x76);

	TC358763_DCS_write_1A_1P(0x00,0x00);
	TC358763_DCS_write_1A_2P(0xD8,0x75,0x73);

	TC358763_DCS_write_1A_1P(0x00,0x00);
	TC358763_DCS_write_1A_1P(0xD9,0x5e);

	TC358763_DCS_write_1A_1P(0x00,0x81);
	TC358763_DCS_write_1A_1P(0xC1,0x66);

	TC358763_DCS_write_1A_1P(0x00,0xA1);
	TC358763_DCS_write_1A_1P(0xC1,0x08);

	TC358763_DCS_write_1A_1P(0x00,0x89);
	TC358763_DCS_write_1A_1P(0xC4,0x08);

	TC358763_DCS_write_1A_1P(0x00,0xA2);
	TC358763_DCS_write_1A_3P(0xC0,0x1B,0x00,0x02);

	TC358763_DCS_write_1A_1P(0x00,0x81);
	TC358763_DCS_write_1A_1P(0xC4,0x83);

	TC358763_DCS_write_1A_1P(0x00,0x92);
	TC358763_DCS_write_1A_1P(0xC5,0x01);

	TC358763_DCS_write_1A_1P(0x00,0xb1);
	TC358763_DCS_write_1A_1P(0xC5,0xa9);

	TC358763_DCS_write_1A_1P(0x00,0x90);
	TC358763_DCS_write_1A_6P(0xC0,0x00,0x44,0x00,0x00,0x00,0x03);

	TC358763_DCS_write_1A_1P(0x00,0xA6);
	TC358763_DCS_write_1A_3P(0xC1,0x00,0x00,0x00);

	TC358763_DCS_write_1A_1P(0x00,0x80);
	TC358763_DCS_write_1A_12P(0xCE,0x87,0x03,0x00,0x85,0x03,0x00,
				0x86,0x03,0x00,0x84,0x03,0x00);

	TC358763_DCS_write_1A_1P(0x00,0xA0);
	TC358763_DCS_write_1A_14P(0xCE,0x38,0x03,0x03,0x58,0x00,0x00,
				0x00,0x38,0x02,0x03,0x59,0x00,0x00,0x00);

	TC358763_DCS_write_1A_1P(0x00,0xB0);
	TC358763_DCS_write_1A_14P(0xCE,0x38,0x01,0x03,0x5a,0x00,0x00,
				0x00,0x38,0x00,0x03,0x5b,0x00,0x00,0x00);

	TC358763_DCS_write_1A_1P(0x00,0xC0);
	TC358763_DCS_write_1A_14P(0xCE,0x30,0x00,0x03,0x5c,0x00,0x00,
				0x00,0x30,0x01,0x03,0x5d,0x00,0x00,0x00);

	TC358763_DCS_write_1A_1P(0x00,0xD0);
	TC358763_DCS_write_1A_14P(0xCE,0x30,0x02,0x03,0x5e,0x00,0x00,
				0x00,0x30,0x03,0x03,0x5f,0x00,0x00,0x00);

	TC358763_DCS_write_1A_1P(0x00,0xC7);
	TC358763_DCS_write_1A_1P(0xCF,0x00);

	TC358763_DCS_write_1A_1P(0x00,0xC9);
	TC358763_DCS_write_1A_1P(0xCF,0x00);

	TC358763_DCS_write_1A_1P(0x00,0xC4);
	TC358763_DCS_write_1A_6P(0xCB,0x04,0x04,0x04,0x04,0x04,0x04);

	TC358763_DCS_write_1A_1P(0x00,0xd9);
	TC358763_DCS_write_1A_6P(0xCB,0x04,0x04,0x04,0x04,0x04,0x04);

	TC358763_DCS_write_1A_1P(0x00,0x84);
	TC358763_DCS_write_1A_6P(0xCc,0x0c,0x0a,0x10,0x0e,0x03,0x04);

	TC358763_DCS_write_1A_1P(0x00,0x9e);
	TC358763_DCS_write_1A_1P(0xCc,0x0b);

	TC358763_DCS_write_1A_1P(0x00,0xA0);
	TC358763_DCS_write_1A_5P(0xCC,0x09,0x0f,0x0d,0x01,0x02);

	TC358763_DCS_write_1A_1P(0x00,0xb4);
	TC358763_DCS_write_1A_5P(0xCC,0x0d,0x09,0x0b,0x02,0x01);

	TC358763_DCS_write_1A_1P(0x00,0xce);
	TC358763_DCS_write_1A_1P(0xCc,0x0e);

	TC358763_DCS_write_1A_1P(0x00,0xD0);
	TC358763_DCS_write_1A_5P(0xCC,0x10,0x0a,0x0c,0x04,0x03);

	TC358763_DCS_write_1A_1P(0x00,0x00);
	TC358763_DCS_write_1A_1P(0x3a,0x77);

	TC358763_DCS_write_1A_0P(0x11);

	msleep(200);

	TC358763_DCS_write_1A_0P(0x29);

	TC358763_DCS_write_1A_0P(0x2C);

	return 0;
}

static int mipid_bl_update_status(struct backlight_device *bl)
{
	return 0;
}

static int mipid_bl_get_brightness(struct backlight_device *bl)
{
	return otm8018bbl_brightness;
}

static int mipi_bl_check_fb(struct backlight_device *bl, struct fb_info *fbi)
{
	return 0;
}

static const struct backlight_ops mipid_lcd_bl_ops = {
	.update_status = mipid_bl_update_status,
	.get_brightness = mipid_bl_get_brightness,
	.check_fb = mipi_bl_check_fb,
};
