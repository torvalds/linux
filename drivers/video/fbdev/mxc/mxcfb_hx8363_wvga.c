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

#define HX8363_TWO_DATA_LANE					(0x2)
#define HX8363_MAX_DPHY_CLK					(800)
#define HX8363_CMD_GETHXID					(0xF4)
#define HX8363_CMD_GETHXID_LEN					(0x4)
#define HX8363_ID						(0x84)
#define HX8363_ID_MASK						(0xFF)


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
	if (n >= 2)						\
		err = mipi_dsi->mipi_dsi_pkt_write(mipi_dsi,		\
			MIPI_DSI_DCS_LONG_WRITE, (u32*)buf, n + 1);	\
	else if (n == 1)					\
		err = mipi_dsi->mipi_dsi_pkt_write(mipi_dsi,	\
			MIPI_DSI_DCS_SHORT_WRITE_PARAM, (u32*)buf, 0);	\
	else if (n == 0)					\
	{							\
		buf[1] = 0;					\
		err = mipi_dsi->mipi_dsi_pkt_write(mipi_dsi,	\
			MIPI_DSI_DCS_SHORT_WRITE, (u32*)buf, 0);	\
	}							\
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

#define TC358763_DCS_write_1A_7P(addr, ...)	\
	TC358763_DCS_write_1A_nP(7, addr, __VA_ARGS__)

#define TC358763_DCS_write_1A_12P(addr, ...)	\
	TC358763_DCS_write_1A_nP(12, addr, __VA_ARGS__)

#define TC358763_DCS_write_1A_13P(addr, ...)	\
	TC358763_DCS_write_1A_nP(13, addr, __VA_ARGS__)

#define TC358763_DCS_write_1A_14P(addr, ...)	\
	TC358763_DCS_write_1A_nP(14, addr, __VA_ARGS__)

#define TC358763_DCS_write_1A_19P(addr, ...)	\
	TC358763_DCS_write_1A_nP(19, addr, __VA_ARGS__)

#define TC358763_DCS_write_1A_34P(addr, ...)	\
	TC358763_DCS_write_1A_nP(34, addr, __VA_ARGS__)

#define TC358763_DCS_write_1A_127P(addr, ...)	\
	TC358763_DCS_write_1A_nP(127, addr, __VA_ARGS__)

static int hx8363bl_brightness;

static struct fb_videomode truly_lcd_modedb[] = {
	{
	 "TRUULY-WVGA", 50, 480, 854, 41042,
	 40, 60,
	 3, 3,
	 8, 4,
	 FB_SYNC_OE_LOW_ACT,
	 FB_VMODE_NONINTERLACED,
	 0,
	},
};

static struct mipi_lcd_config lcd_config = {
	.virtual_ch	= 0x0,
	.data_lane_num  = HX8363_TWO_DATA_LANE,
	.max_phy_clk    = HX8363_MAX_DPHY_CLK,
	.dpi_fmt	= MIPI_RGB888,
};

void mipid_hx8363_get_lcd_videomode(struct fb_videomode **mode, int *size,
		struct mipi_lcd_config **data)
{
	*mode = &truly_lcd_modedb[0];
	*size = ARRAY_SIZE(truly_lcd_modedb);
	*data = &lcd_config;
}

int mipid_hx8363_lcd_setup(struct mipi_dsi_info *mipi_dsi)
{
	u8 buf[DSI_CMD_BUF_MAXSIZE];

	dev_dbg(&mipi_dsi->pdev->dev, "MIPI DSI LCD HX8363 setup.\n");

	TC358763_DCS_write_1A_3P(0xB9,0xFF,0x83,0x63);/* SET password */

	TC358763_DCS_write_1A_19P(0xB1,0x01,0x00,0x44,0x08,0x01,0x10,0x10,0x36,
				  0x3E,0x1A,0x1A,0x40,0x12,0x00,0xE6,0xE6,0xE6,0xE6,0xE6);/* Set Power */
	TC358763_DCS_write_1A_2P(0xB2,0x08,0x03);/* Set DISP */
	TC358763_DCS_write_1A_7P(0xB4,0x02,0x18,0x9C,0x08,0x18,0x04,0x6C);
	TC358763_DCS_write_1A_1P(0xB6,0x00);/* Set VCOM */
	TC358763_DCS_write_1A_1P(0xCC,0x0B);/* Set Panel */
	TC358763_DCS_write_1A_34P(0xE0,0x0E,0x15,0x19,0x30,0x31,0x3F,0x27,0x3C,0x88,0x8F,0xD1,0xD5,0xD7,0x16,0x16,
				  0x0C,0x1E,0x0E,0x15,0x19,0x30,0x31,0x3F,0x27,0x3C,0x88,0x8F,
				  0xD1,0xD5,0xD7,0x16,0x16,0x0C,0x1E);
	mdelay(5);

	TC358763_DCS_write_1A_1P(0x3A,0x77);/* 24bit */
	TC358763_DCS_write_1A_14P(0xBA,0x11,0x00,0x56,0xC6,0x10,0x89,0xFF,0x0F,0x32,0x6E,0x04,0x07,0x9A,0x92);
	TC358763_DCS_write_1A_0P(0x21);

	TC358763_DCS_write_1A_0P(0x11);
	msleep(10);

	TC358763_DCS_write_1A_0P(0x29);
	msleep(120);

	return 0;
}

static int mipid_bl_update_status(struct backlight_device *bl)
{
	return 0;
}

static int mipid_bl_get_brightness(struct backlight_device *bl)
{
	return hx8363bl_brightness;
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
