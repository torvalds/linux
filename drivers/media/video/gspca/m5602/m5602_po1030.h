/*
 * Driver for the po1030 sensor.
 *
 * Copyright (c) 2008 Erik Andrén
 * Copyright (c) 2007 Ilyes Gouta. Based on the m5603x Linux Driver Project.
 * Copyright (c) 2005 m5603x Linux Driver Project <m5602@x3ng.com.br>
 *
 * Portions of code to USB interface and ALi driver software,
 * Copyright (c) 2006 Willem Duinker
 * v4l2 interface modeled after the V4L2 driver
 * for SN9C10x PC Camera Controllers
 *
 * Register defines taken from Pascal Stangs Procyon Armlib
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 *
 */

#ifndef M5602_PO1030_H_
#define M5602_PO1030_H_

#include "m5602_sensor.h"

/*****************************************************************************/

#define PO1030_REG_DEVID_H		0x00
#define PO1030_REG_DEVID_L		0x01
#define PO1030_REG_FRAMEWIDTH_H		0x04
#define PO1030_REG_FRAMEWIDTH_L		0x05
#define PO1030_REG_FRAMEHEIGHT_H	0x06
#define PO1030_REG_FRAMEHEIGHT_L	0x07
#define PO1030_REG_WINDOWX_H		0x08
#define PO1030_REG_WINDOWX_L		0x09
#define PO1030_REG_WINDOWY_H		0x0a
#define PO1030_REG_WINDOWY_L		0x0b
#define PO1030_REG_WINDOWWIDTH_H	0x0c
#define PO1030_REG_WINDOWWIDTH_L	0x0d
#define PO1030_REG_WINDOWHEIGHT_H	0x0e
#define PO1030_REG_WINDOWHEIGHT_L	0x0f

#define PO1030_REG_GLOBALIBIAS		0x12
#define PO1030_REG_PIXELIBIAS		0x13

#define PO1030_REG_GLOBALGAIN		0x15
#define PO1030_REG_RED_GAIN		0x16
#define PO1030_REG_GREEN_1_GAIN		0x17
#define PO1030_REG_BLUE_GAIN		0x18
#define PO1030_REG_GREEN_2_GAIN		0x19

#define PO1030_REG_INTEGLINES_H		0x1a
#define PO1030_REG_INTEGLINES_M		0x1b
#define PO1030_REG_INTEGLINES_L		0x1c

#define PO1030_REG_CONTROL1		0x1d
#define PO1030_REG_CONTROL2		0x1e
#define PO1030_REG_CONTROL3		0x1f
#define PO1030_REG_CONTROL4		0x20

#define PO1030_REG_PERIOD50_H		0x23
#define PO1030_REG_PERIOD50_L		0x24
#define PO1030_REG_PERIOD60_H		0x25
#define PO1030_REG_PERIOD60_L		0x26
#define PO1030_REG_REGCLK167		0x27
#define PO1030_REG_DELTA50		0x28
#define PO1030_REG_DELTA60		0x29

#define PO1030_REG_ADCOFFSET		0x2c

/* Gamma Correction Coeffs */
#define PO1030_REG_GC0			0x2d
#define PO1030_REG_GC1			0x2e
#define PO1030_REG_GC2			0x2f
#define PO1030_REG_GC3			0x30
#define PO1030_REG_GC4			0x31
#define PO1030_REG_GC5			0x32
#define PO1030_REG_GC6			0x33
#define PO1030_REG_GC7			0x34

/* Color Transform Matrix */
#define PO1030_REG_CT0			0x35
#define PO1030_REG_CT1			0x36
#define PO1030_REG_CT2			0x37
#define PO1030_REG_CT3			0x38
#define PO1030_REG_CT4			0x39
#define PO1030_REG_CT5			0x3a
#define PO1030_REG_CT6			0x3b
#define PO1030_REG_CT7			0x3c
#define PO1030_REG_CT8			0x3d

#define PO1030_REG_AUTOCTRL1		0x3e
#define PO1030_REG_AUTOCTRL2		0x3f

#define PO1030_REG_YTARGET		0x40
#define PO1030_REG_GLOBALGAINMIN	0x41
#define PO1030_REG_GLOBALGAINMAX	0x42

/* Output format control */
#define PO1030_REG_OUTFORMCTRL1		0x5a
#define PO1030_REG_OUTFORMCTRL2		0x5b
#define PO1030_REG_OUTFORMCTRL3		0x5c
#define PO1030_REG_OUTFORMCTRL4		0x5d
#define PO1030_REG_OUTFORMCTRL5		0x5e

/* Imaging coefficients */
#define PO1030_REG_YBRIGHT		0x73
#define PO1030_REG_YCONTRAST		0x74
#define PO1030_REG_YSATURATION		0x75

#define PO1030_HFLIP			(1 << 7)
#define PO1030_VFLIP			(1 << 6)

/*****************************************************************************/

#define PO1030_GLOBAL_GAIN_DEFAULT	0x12
#define PO1030_EXPOSURE_DEFAULT		0x0085
#define PO1030_BLUE_GAIN_DEFAULT 	0x40
#define PO1030_RED_GAIN_DEFAULT 	0x40

/*****************************************************************************/

/* Kernel module parameters */
extern int force_sensor;
extern int dump_sensor;

int po1030_probe(struct sd *sd);
int po1030_init(struct sd *sd);
int po1030_power_down(struct sd *sd);

int po1030_get_exposure(struct gspca_dev *gspca_dev, __s32 *val);
int po1030_set_exposure(struct gspca_dev *gspca_dev, __s32 val);
int po1030_get_gain(struct gspca_dev *gspca_dev, __s32 *val);
int po1030_set_gain(struct gspca_dev *gspca_dev, __s32 val);
int po1030_get_red_balance(struct gspca_dev *gspca_dev, __s32 *val);
int po1030_set_red_balance(struct gspca_dev *gspca_dev, __s32 val);
int po1030_get_blue_balance(struct gspca_dev *gspca_dev, __s32 *val);
int po1030_set_blue_balance(struct gspca_dev *gspca_dev, __s32 val);
int po1030_get_hflip(struct gspca_dev *gspca_dev, __s32 *val);
int po1030_set_hflip(struct gspca_dev *gspca_dev, __s32 val);
int po1030_get_vflip(struct gspca_dev *gspca_dev, __s32 *val);
int po1030_set_vflip(struct gspca_dev *gspca_dev, __s32 val);

static struct m5602_sensor po1030 = {
	.name = "PO1030",

	.i2c_slave_id = 0xdc,
	.i2c_regW = 1,

	.probe = po1030_probe,
	.init = po1030_init,
	.power_down = po1030_power_down,

	.nctrls = 6,
	.ctrls = {
	{
		{
			.id 		= V4L2_CID_GAIN,
			.type 		= V4L2_CTRL_TYPE_INTEGER,
			.name 		= "gain",
			.minimum 	= 0x00,
			.maximum 	= 0x4f,
			.step 		= 0x1,
			.default_value 	= PO1030_GLOBAL_GAIN_DEFAULT,
			.flags         	= V4L2_CTRL_FLAG_SLIDER
		},
		.set = po1030_set_gain,
		.get = po1030_get_gain
	}, {
		{
			.id 		= V4L2_CID_EXPOSURE,
			.type 		= V4L2_CTRL_TYPE_INTEGER,
			.name 		= "exposure",
			.minimum 	= 0x00,
			.maximum 	= 0x02ff,
			.step 		= 0x1,
			.default_value 	= PO1030_EXPOSURE_DEFAULT,
			.flags         	= V4L2_CTRL_FLAG_SLIDER
		},
		.set = po1030_set_exposure,
		.get = po1030_get_exposure
	}, {
		{
			.id 		= V4L2_CID_RED_BALANCE,
			.type 		= V4L2_CTRL_TYPE_INTEGER,
			.name 		= "red balance",
			.minimum 	= 0x00,
			.maximum 	= 0xff,
			.step 		= 0x1,
			.default_value 	= PO1030_RED_GAIN_DEFAULT,
			.flags         	= V4L2_CTRL_FLAG_SLIDER
		},
		.set = po1030_set_red_balance,
		.get = po1030_get_red_balance
	}, {
		{
			.id 		= V4L2_CID_BLUE_BALANCE,
			.type 		= V4L2_CTRL_TYPE_INTEGER,
			.name 		= "blue balance",
			.minimum 	= 0x00,
			.maximum 	= 0xff,
			.step 		= 0x1,
			.default_value 	= PO1030_BLUE_GAIN_DEFAULT,
			.flags         	= V4L2_CTRL_FLAG_SLIDER
		},
		.set = po1030_set_blue_balance,
		.get = po1030_get_blue_balance
	}, {
		{
			.id 		= V4L2_CID_HFLIP,
			.type 		= V4L2_CTRL_TYPE_BOOLEAN,
			.name 		= "horizontal flip",
			.minimum 	= 0,
			.maximum 	= 1,
			.step 		= 1,
			.default_value 	= 0,
		},
		.set = po1030_set_hflip,
		.get = po1030_get_hflip
	}, {
		{
			.id 		= V4L2_CID_VFLIP,
			.type 		= V4L2_CTRL_TYPE_BOOLEAN,
			.name 		= "vertical flip",
			.minimum 	= 0,
			.maximum 	= 1,
			.step 		= 1,
			.default_value 	= 0,
		},
		.set = po1030_set_vflip,
		.get = po1030_get_vflip
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

static const unsigned char preinit_po1030[][3] =
{
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0d},
	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0c},

	{SENSOR, PO1030_REG_AUTOCTRL2, 0x24},

	{BRIDGE, M5602_XB_GPIO_DIR, 0x05},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x04},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x02},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x04},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0c},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82},
	{BRIDGE, M5602_XB_SIG_INI, 0x01},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x02},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x01},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0xec},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x02},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x02},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x87},
	{BRIDGE, M5602_XB_SIG_INI, 0x00},

	{SENSOR, PO1030_REG_AUTOCTRL2, 0x24},

	{BRIDGE, M5602_XB_GPIO_DIR, 0x05},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x04},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x02},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x04},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0c},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x05},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x00}
};

static const unsigned char init_po1030[][4] =
{
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0},
	/*sequence 1*/
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0d},

	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0c},
	/*end of sequence 1*/

	/*sequence 2 (same as stop sequence)*/
	{SENSOR, PO1030_REG_AUTOCTRL2, 0x24},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x05},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x04},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x02},

	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x04},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	/*end of sequence 2*/

	/*sequence 5*/
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0c},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82},
	{BRIDGE, M5602_XB_SIG_INI, 0x01},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x02},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x01},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0xec},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x02},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x02},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x87},
	{BRIDGE, M5602_XB_SIG_INI, 0x00},
	/*end of sequence 5*/

	/*sequence 2 stop */
	{SENSOR, PO1030_REG_AUTOCTRL2, 0x24},

	{BRIDGE, M5602_XB_GPIO_DIR, 0x05},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x04},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x02},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x04},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	/*end of sequence 2 stop */

/* ---------------------------------
 * end of init - begin of start
 * --------------------------------- */

	/*sequence 3*/
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0c},
	/*end of sequence 3*/
	/*sequence 4*/
	{BRIDGE, M5602_XB_GPIO_DIR, 0x05},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0x00},

	{SENSOR, PO1030_REG_AUTOCTRL2, 0x04},

	/* Set the width to 751 */
	{SENSOR, PO1030_REG_FRAMEWIDTH_H, 0x02},
	{SENSOR, PO1030_REG_FRAMEWIDTH_L, 0xef},

	/* Set the height to 540 */
	{SENSOR, PO1030_REG_FRAMEHEIGHT_H, 0x02},
	{SENSOR, PO1030_REG_FRAMEHEIGHT_L, 0x1c},

	/* Set the x window to 1 */
	{SENSOR, PO1030_REG_WINDOWX_H, 0x00},
	{SENSOR, PO1030_REG_WINDOWX_L, 0x01},

	/* Set the y window to 1 */
	{SENSOR, PO1030_REG_WINDOWY_H, 0x00},
	{SENSOR, PO1030_REG_WINDOWY_L, 0x01},

	{SENSOR, PO1030_REG_WINDOWWIDTH_H, 0x02},
	{SENSOR, PO1030_REG_WINDOWWIDTH_L, 0x87},
	{SENSOR, PO1030_REG_WINDOWHEIGHT_H, 0x01},
	{SENSOR, PO1030_REG_WINDOWHEIGHT_L, 0xe3},

	{SENSOR, PO1030_REG_OUTFORMCTRL2, 0x04},
	{SENSOR, PO1030_REG_OUTFORMCTRL2, 0x04},
	{SENSOR, PO1030_REG_AUTOCTRL1, 0x08},
	{SENSOR, PO1030_REG_CONTROL2, 0x03},
	{SENSOR, 0x21, 0x90},
	{SENSOR, PO1030_REG_YTARGET, 0x60},
	{SENSOR, 0x59, 0x13},
	{SENSOR, PO1030_REG_OUTFORMCTRL1, 0x40},
	{SENSOR, 0x5f, 0x00},
	{SENSOR, 0x60, 0x80},
	{SENSOR, 0x78, 0x14},
	{SENSOR, 0x6f, 0x01},
	{SENSOR, PO1030_REG_CONTROL1, 0x18},
	{SENSOR, PO1030_REG_GLOBALGAINMAX, 0x14},
	{SENSOR, 0x63, 0x38},
	{SENSOR, 0x64, 0x38},
	{SENSOR, PO1030_REG_CONTROL1, 0x58},
	{SENSOR, PO1030_REG_RED_GAIN, 0x30},
	{SENSOR, PO1030_REG_GREEN_1_GAIN, 0x30},
	{SENSOR, PO1030_REG_BLUE_GAIN, 0x30},
	{SENSOR, PO1030_REG_GREEN_2_GAIN, 0x30},
	{SENSOR, PO1030_REG_GC0, 0x10},
	{SENSOR, PO1030_REG_GC1, 0x20},
	{SENSOR, PO1030_REG_GC2, 0x40},
	{SENSOR, PO1030_REG_GC3, 0x60},
	{SENSOR, PO1030_REG_GC4, 0x80},
	{SENSOR, PO1030_REG_GC5, 0xa0},
	{SENSOR, PO1030_REG_GC6, 0xc0},
	{SENSOR, PO1030_REG_GC7, 0xff},
	/*end of sequence 4*/
	/*sequence 5*/
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0c},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82},
	{BRIDGE, M5602_XB_SIG_INI, 0x01},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x02},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x01},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0xec},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x02},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x7e},
	{BRIDGE, M5602_XB_SIG_INI, 0x00},
	/*end of sequence 5*/

	/*sequence 6*/
	/* Changing 40 in f0 the image becomes green in bayer mode and red in
	 * rgb mode */
	{SENSOR, PO1030_REG_RED_GAIN, PO1030_RED_GAIN_DEFAULT},
	/* in changing 40 in f0 the image becomes green in bayer mode and red in
	 * rgb mode */
	{SENSOR, PO1030_REG_BLUE_GAIN, PO1030_BLUE_GAIN_DEFAULT},

	/* with a very low lighted environment increase the exposure but
	 * decrease the FPS (Frame Per Second) */
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},

	/* Controls high exposure more than SENSOR_LOW_EXPOSURE, use only in
	 * low lighted environment (f0 is more than ff ?)*/
	{SENSOR, PO1030_REG_INTEGLINES_H, ((PO1030_EXPOSURE_DEFAULT >> 2)
		& 0xff)},

	/* Controls middle exposure, use only in high lighted environment */
	{SENSOR, PO1030_REG_INTEGLINES_M, PO1030_EXPOSURE_DEFAULT & 0xff},

	/* Controls clarity (not sure) */
	{SENSOR, PO1030_REG_INTEGLINES_L, 0x00},
	/* Controls gain (the image is more lighted) */
	{SENSOR, PO1030_REG_GLOBALGAIN, PO1030_GLOBAL_GAIN_DEFAULT},

	/* Sets the width */
	{SENSOR, PO1030_REG_FRAMEWIDTH_H, 0x02},
	{SENSOR, PO1030_REG_FRAMEWIDTH_L, 0xef}
	/*end of sequence 6*/
};

#endif
