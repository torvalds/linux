/*
 * Driver for the s5k83a sensor
 *
 * Copyright (C) 2008 Erik Andrén
 * Copyright (C) 2007 Ilyes Gouta. Based on the m5603x Linux Driver Project.
 * Copyright (C) 2005 m5603x Linux Driver Project <m5602@x3ng.com.br>
 *
 * Portions of code to USB interface and ALi driver software,
 * Copyright (c) 2006 Willem Duinker
 * v4l2 interface modeled after the V4L2 driver
 * for SN9C10x PC Camera Controllers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 *
 */

#ifndef M5602_S5K83A_H_
#define M5602_S5K83A_H_

#include "m5602_sensor.h"

#define S5K83A_FLIP				0x01
#define S5K83A_HFLIP_TUNE			0x03
#define S5K83A_VFLIP_TUNE			0x05
#define S5K83A_WHITENESS			0x0a
#define S5K83A_GAIN				0x18
#define S5K83A_BRIGHTNESS			0x1b
#define S5K83A_PAGE_MAP				0xec

#define S5K83A_DEFAULT_BRIGHTNESS		0x71
#define S5K83A_DEFAULT_WHITENESS		0x7e
#define S5K83A_DEFAULT_GAIN			0x00
#define S5K83A_MAXIMUM_GAIN			0x3c
#define S5K83A_FLIP_MASK			0x10


/*****************************************************************************/

/* Kernel module parameters */
extern int force_sensor;
extern int dump_sensor;

int s5k83a_probe(struct sd *sd);
int s5k83a_init(struct sd *sd);
int s5k83a_power_down(struct sd *sd);

int s5k83a_set_brightness(struct gspca_dev *gspca_dev, __s32 val);
int s5k83a_get_brightness(struct gspca_dev *gspca_dev, __s32 *val);
int s5k83a_set_whiteness(struct gspca_dev *gspca_dev, __s32 val);
int s5k83a_get_whiteness(struct gspca_dev *gspca_dev, __s32 *val);
int s5k83a_set_gain(struct gspca_dev *gspca_dev, __s32 val);
int s5k83a_get_gain(struct gspca_dev *gspca_dev, __s32 *val);
int s5k83a_get_vflip(struct gspca_dev *gspca_dev, __s32 *val);
int s5k83a_set_vflip(struct gspca_dev *gspca_dev, __s32 val);
int s5k83a_get_hflip(struct gspca_dev *gspca_dev, __s32 *val);
int s5k83a_set_hflip(struct gspca_dev *gspca_dev, __s32 val);

static struct m5602_sensor s5k83a = {
	.name = "S5K83A",
	.probe = s5k83a_probe,
	.init = s5k83a_init,
	.power_down = s5k83a_power_down,
	.i2c_slave_id = 0x5a,
	.i2c_regW = 2,
	.nctrls = 5,
	.ctrls = {
	{
		{
			.id = V4L2_CID_BRIGHTNESS,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "brightness",
			.minimum = 0x00,
			.maximum = 0xff,
			.step = 0x01,
			.default_value = S5K83A_DEFAULT_BRIGHTNESS,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
			.set = s5k83a_set_brightness,
			.get = s5k83a_get_brightness

	}, {
		{
			.id = V4L2_CID_WHITENESS,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "whiteness",
			.minimum = 0x00,
			.maximum = 0xff,
			.step = 0x01,
			.default_value = S5K83A_DEFAULT_WHITENESS,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
			.set = s5k83a_set_whiteness,
			.get = s5k83a_get_whiteness,
	}, {
		{
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "gain",
			.minimum = 0x00,
			.maximum = S5K83A_MAXIMUM_GAIN,
			.step = 0x01,
			.default_value = S5K83A_DEFAULT_GAIN,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
			.set = s5k83a_set_gain,
			.get = s5k83a_get_gain
	}, {
		{
			.id         = V4L2_CID_HFLIP,
			.type       = V4L2_CTRL_TYPE_BOOLEAN,
			.name       = "horizontal flip",
			.minimum    = 0,
			.maximum    = 1,
			.step       = 1,
			.default_value  = 0
		},
			.set = s5k83a_set_hflip,
			.get = s5k83a_get_hflip
	}, {
		{
		 .id         = V4L2_CID_VFLIP,
		.type       = V4L2_CTRL_TYPE_BOOLEAN,
		.name       = "vertical flip",
		.minimum    = 0,
		.maximum    = 1,
		.step       = 1,
		.default_value  = 0
		},
		.set = s5k83a_set_vflip,
		.get = s5k83a_get_vflip
		}
	},
	.nmodes = 1,
	.modes = {
	{
		M5602_DEFAULT_FRAME_WIDTH,
		M5602_DEFAULT_FRAME_HEIGHT,
		V4L2_PIX_FMT_SBGGR8,
		V4L2_FIELD_NONE,
		.sizeimage =
			M5602_DEFAULT_FRAME_WIDTH * M5602_DEFAULT_FRAME_HEIGHT,
		.bytesperline = M5602_DEFAULT_FRAME_WIDTH,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1
	}
	}
};

static const unsigned char preinit_s5k83a[][4] =
{
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0d, 0x00},
	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00, 0x00},

	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x08, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0x80, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xf0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x1c, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x20, 0x00},

	{SENSOR, S5K83A_PAGE_MAP, 0x00, 0x00}
};

/* This could probably be considerably shortened.
   I don't have the hardware to experiment with it, patches welcome
*/
static const unsigned char init_s5k83a[][4] =
{
	{SENSOR, S5K83A_PAGE_MAP, 0x04, 0x00},
	{SENSOR, 0xaf, 0x01, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x00, 0x00},
	{SENSOR, 0x7b, 0xff, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},
	{SENSOR, 0x01, 0x50, 0x00},
	{SENSOR, 0x12, 0x20, 0x00},
	{SENSOR, 0x17, 0x40, 0x00},
	{SENSOR, S5K83A_BRIGHTNESS, 0x0f, 0x00},
	{SENSOR, 0x1c, 0x00, 0x00},
	{SENSOR, 0x02, 0x70, 0x00},
	{SENSOR, 0x03, 0x0b, 0x00},
	{SENSOR, 0x04, 0xf0, 0x00},
	{SENSOR, 0x05, 0x0b, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},

	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81, 0x00},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0xe4, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x87, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},

	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},
	{SENSOR, 0x06, 0x71, 0x00},
	{SENSOR, 0x07, 0xe8, 0x00},
	{SENSOR, 0x08, 0x02, 0x00},
	{SENSOR, 0x09, 0x88, 0x00},
	{SENSOR, 0x14, 0x00, 0x00},
	{SENSOR, 0x15, 0x20, 0x00},
	{SENSOR, 0x19, 0x00, 0x00},
	{SENSOR, 0x1a, 0x98, 0x00},
	{SENSOR, 0x0f, 0x02, 0x00},
	{SENSOR, 0x10, 0xe5, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},
	{SENSOR_LONG, 0x14, 0x00, 0x20},
	{SENSOR_LONG, 0x0d, 0x00, 0x7d},
	{SENSOR_LONG, 0x1b, 0x0d, 0x05},

	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81, 0x00},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0xe4, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x87, 0x00},

	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x08, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0x80, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xf0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x1c, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x20, 0x00},

	{SENSOR, S5K83A_PAGE_MAP, 0x04, 0x00},
	{SENSOR, 0xaf, 0x01, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},
	/* ff ( init value )is very dark) || 71 and f0 better */
	{SENSOR, 0x7b, 0xff, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},
	{SENSOR, 0x01, 0x50, 0x00},
	{SENSOR, 0x12, 0x20, 0x00},
	{SENSOR, 0x17, 0x40, 0x00},
	{SENSOR, S5K83A_BRIGHTNESS, 0x0f, 0x00},
	{SENSOR, 0x1c, 0x00, 0x00},
	{SENSOR, 0x02, 0x70, 0x00},
	/* some values like 0x10 give a blue-purple image */
	{SENSOR, 0x03, 0x0b, 0x00},
	{SENSOR, 0x04, 0xf0, 0x00},
	{SENSOR, 0x05, 0x0b, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},

	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06, 0x00},
	/* under 80 don't work, highter depend on value */
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},

	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81, 0x00},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0xe4, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x7f, 0x00},

	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},

	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},
	{SENSOR, 0x06, 0x71, 0x00},
	{SENSOR, 0x07, 0xe8, 0x00},
	{SENSOR, 0x08, 0x02, 0x00},
	{SENSOR, 0x09, 0x88, 0x00},
	{SENSOR, 0x14, 0x00, 0x00},
	{SENSOR, 0x15, 0x20, 0x00},
	{SENSOR, 0x19, 0x00, 0x00},
	{SENSOR, 0x1a, 0x98, 0x00},
	{SENSOR, 0x0f, 0x02, 0x00},
	{SENSOR, 0x10, 0xe5, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},
	{SENSOR_LONG, 0x14, 0x00, 0x20},
	{SENSOR_LONG, 0x0d, 0x00, 0x7d},
	{SENSOR_LONG, 0x1b, 0x0d, 0x05},

	/* The following sequence is useless after a clean boot
	   but is necessary after resume from suspend */
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x08, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0x80, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xf0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x1c, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x20, 0x00},

	{SENSOR, S5K83A_PAGE_MAP, 0x04, 0x00},
	{SENSOR, 0xaf, 0x01, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x00, 0x00},
	{SENSOR, 0x7b, 0xff, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},
	{SENSOR, 0x01, 0x50, 0x00},
	{SENSOR, 0x12, 0x20, 0x00},
	{SENSOR, 0x17, 0x40, 0x00},
	{SENSOR, S5K83A_BRIGHTNESS, 0x0f, 0x00},
	{SENSOR, 0x1c, 0x00, 0x00},
	{SENSOR, 0x02, 0x70, 0x00},
	{SENSOR, 0x03, 0x0b, 0x00},
	{SENSOR, 0x04, 0xf0, 0x00},
	{SENSOR, 0x05, 0x0b, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},

	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81, 0x00},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0xe4, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x7f, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},

	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},
	{SENSOR, 0x06, 0x71, 0x00},
	{SENSOR, 0x07, 0xe8, 0x00},
	{SENSOR, 0x08, 0x02, 0x00},
	{SENSOR, 0x09, 0x88, 0x00},
	{SENSOR, 0x14, 0x00, 0x00},
	{SENSOR, 0x15, 0x20, 0x00},
	{SENSOR, 0x19, 0x00, 0x00},
	{SENSOR, 0x1a, 0x98, 0x00},
	{SENSOR, 0x0f, 0x02, 0x00},

	{SENSOR, 0x10, 0xe5, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},
	{SENSOR_LONG, 0x14, 0x00, 0x20},
	{SENSOR_LONG, 0x0d, 0x00, 0x7d},
	{SENSOR_LONG, 0x1b, 0x0d, 0x05},

	/* normal colors
	   (this is value after boot, but after tries can be different) */
	{SENSOR, 0x00, 0x06, 0x00},

	/* set default brightness */
	{SENSOR_LONG, 0x14, 0x00, 0x20},
	{SENSOR_LONG, 0x0d, 0x01, 0x00},
	{SENSOR_LONG, 0x1b, S5K83A_DEFAULT_BRIGHTNESS >> 3,
			    S5K83A_DEFAULT_BRIGHTNESS >> 1},

	/* set default whiteness */
	{SENSOR, S5K83A_WHITENESS, S5K83A_DEFAULT_WHITENESS, 0x00},

	/* set default gain */
	{SENSOR_LONG, 0x18, 0x00, S5K83A_DEFAULT_GAIN},

	/* set default flip */
	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},
	{SENSOR, S5K83A_FLIP, 0x00 | S5K83A_FLIP_MASK, 0x00},
	{SENSOR, S5K83A_HFLIP_TUNE, 0x0b, 0x00},
	{SENSOR, S5K83A_VFLIP_TUNE, 0x0a, 0x00}

};

#endif
