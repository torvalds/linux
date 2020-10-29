// SPDX-License-Identifier: GPL-2.0
/*
 * gc2035 sensor driver
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd.
 * V0.0X01.0X01 add enum_frame_interval function.
 * V0.0X01.0X02 add quick stream on/off
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/videodev2.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x2)
#define DRIVER_NAME "gc2035"
#define GC2035_PIXEL_RATE		(70 * 1000 * 1000)

/*
 * GC2035 register definitions
 */

#define REG_SC_CHIP_ID_H		0xf0
#define REG_SC_CHIP_ID_L		0xf1
#define GC2035_ID_H			0x20
#define GC2035_ID_L			0x35
#define REG_NULL			0xFFFF	/* Array end token */

struct sensor_register {
	u16 addr;
	u8 value;
};

struct gc2035_framesize {
	u16 width;
	u16 height;
	struct v4l2_fract max_fps;
	const struct sensor_register *regs;
};

struct gc2035_pll_ctrl {
	u8 ctrl1;
	u8 ctrl2;
	u8 ctrl3;
};

struct gc2035_pixfmt {
	u32 code;
	/* Output format Register Value (REG_FORMAT_CTRL00) */
	struct sensor_register *format_ctrl_regs;
};

struct pll_ctrl_reg {
	unsigned int div;
	unsigned char reg;
};

static const char * const gc2035_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
};

#define GC2035_NUM_SUPPLIES ARRAY_SIZE(gc2035_supply_names)

struct gc2035 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	unsigned int fps;
	unsigned int xvclk_frequency;
	struct clk *xvclk;
	struct gpio_desc *pwdn_gpio;
	struct regulator_bulk_data supplies[GC2035_NUM_SUPPLIES];
	struct mutex lock; /* Protects streaming, format, interval */
	struct i2c_client *client;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *link_frequency;
	const struct gc2035_framesize *frame_size;
	int streaming;
	u32 module_index;
	const char *module_facing;
	const char *module_name;
	const char *len_name;
};

static const struct sensor_register gc2035_init_regs[] = {
	{0xfe, 0x80},
	{0xfe, 0x80},
	{0xfe, 0x80},
	{0xfc, 0x06},
	{0xf2, 0x00},
	{0xf3, 0x00},
	{0xf4, 0x00},
	{0xf5, 0x00},
	{0xf9, 0xfe},
	{0xfa, 0x00},
	{0xf6, 0x00},
	{0xf7, 0x15},

	{0xf8, 0x85},
	{0xfe, 0x00},
	{0x82, 0x00},
	{0xb3, 0x60},
	{0xb4, 0x40},
	{0xb5, 0x60},

	{0x03, 0x02},
	{0x04, 0x80},
	/* measure window */
	{0xfe, 0x00},
	{0xec, 0x06},
	{0xed, 0x06},
	{0xee, 0x62},
	{0xef, 0x92},
	/*  analog */
	{0x0a, 0x00},
	{0x0c, 0x00},
	{0x0d, 0x04},
	{0x0e, 0xc0},
	{0x0f, 0x06},
	{0x10, 0x58},
	{0x17, 0x14},

	{0x18, 0x0e},
	{0x19, 0x0c},

	{0x18, 0x0a},
	{0x19, 0x0a},

	{0x1a, 0x01},
	{0x1b, 0x8b},
	{0x1e, 0x88},
	{0x1f, 0x08},
	{0x20, 0x05},
	{0x21, 0x0f},
	{0x22, 0xf0},
	{0x23, 0xc3},
	{0x24, 0x17},
	/*   AEC   */
	{0xfe, 0x01},
	{0x11, 0x20},
	{0x1f, 0xc0},
	{0x20, 0x60},
	{0x47, 0x30},
	{0x0b, 0x10},
	{0x13, 0x75},
	{0xfe, 0x00},

	{0x05, 0x01},
	{0x06, 0x11},
	{0x07, 0x00},
	{0x08, 0x50},
	{0xfe, 0x01},
	{0x27, 0x00},
	{0x28, 0xa0},
	{0x29, 0x05},
	{0x2a, 0x00},
	{0x2b, 0x05},
	{0x2c, 0x00},
	{0x2d, 0x06},
	{0x2e, 0xe0},
	{0x2f, 0x0a},
	{0x30, 0x00},
	{0x3e, 0x40},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xb6, 0x03},
	{0xfe, 0x00},
	/*   BLK   */
	{0x3f, 0x00},
	{0x40, 0x77},
	{0x42, 0x7f},
	{0x43, 0x30},
	{0x5c, 0x08},
	{0x5e, 0x20},
	{0x5f, 0x20},
	{0x60, 0x20},
	{0x61, 0x20},
	{0x62, 0x20},
	{0x63, 0x20},
	{0x64, 0x20},
	{0x65, 0x20},
	/*  block  */
	{0x80, 0xff},
	{0x81, 0x26},
	{0x87, 0x90},
	{0x84, 0x00},
	{0x86, 0x07},
	{0x8b, 0xbc},
	{0xb0, 0x80},
	{0xc0, 0x40},
	/*   lsc   */
	{0xfe, 0x01},
	{0xc2, 0x38},
	{0xc3, 0x25},
	{0xc4, 0x21},
	{0xc8, 0x19},
	{0xc9, 0x12},
	{0xca, 0x0e},
	{0xbc, 0x43},
	{0xbd, 0x18},
	{0xbe, 0x1b},
	{0xb6, 0x40},
	{0xb7, 0x2e},
	{0xb8, 0x26},
	{0xc5, 0x05},
	{0xc6, 0x03},
	{0xc7, 0x04},
	{0xcb, 0x00},
	{0xcc, 0x00},
	{0xcd, 0x00},
	{0xbf, 0x14},
	{0xc0, 0x22},
	{0xc1, 0x1b},
	{0xb9, 0x00},
	{0xba, 0x05},
	{0xbb, 0x05},
	{0xaa, 0x35},
	{0xab, 0x33},
	{0xac, 0x33},
	{0xad, 0x25},
	{0xae, 0x22},
	{0xaf, 0x27},
	{0xb0, 0x1d},
	{0xb1, 0x20},
	{0xb2, 0x22},
	{0xb3, 0x14},
	{0xb4, 0x15},
	{0xb5, 0x16},
	{0xd0, 0x00},
	{0xd2, 0x07},
	{0xd3, 0x08},
	{0xd8, 0x00},
	{0xda, 0x13},
	{0xdb, 0x17},
	{0xdc, 0x00},
	{0xde, 0x0a},
	{0xdf, 0x08},
	{0xd4, 0x00},
	{0xd6, 0x00},
	{0xd7, 0x0c},
	{0xa4, 0x00},
	{0xa5, 0x00},
	{0xa6, 0x00},
	{0xa7, 0x00},
	{0xa8, 0x00},
	{0xa9, 0x00},
	{0xa1, 0x80},
	{0xa2, 0x80},
	/*   cc    */
	{0xfe, 0x02},
	{0xc0, 0x01},
	{0xc1, 0x40},
	{0xc2, 0xfc},
	{0xc3, 0x05},
	{0xc4, 0xec},
	{0xc5, 0x42},
	{0xc6, 0xf8},
	{0xc7, 0x40},
	{0xc8, 0xf8},
	{0xc9, 0x06},
	{0xca, 0xfd},
	{0xcb, 0x3e},
	{0xcc, 0xf3},
	{0xcd, 0x36},
	{0xce, 0xf6},
	{0xcf, 0x04},
	{0xe3, 0x0c},
	{0xe4, 0x44},
	{0xe5, 0xe5},
	{0xfe, 0x00},

	{0xfe, 0x01},
	{0x4f, 0x00},
	{0x4d, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0x10},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0x20},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0x30},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0x40},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0x50},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0x60},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0x70},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0x80},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0x90},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0xa0},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0xb0},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0xc0},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4d, 0xd0},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4e, 0x00},
	{0x4f, 0x01},
	{0xfe, 0x01},
	{0x4f, 0x00},
	{0x4d, 0x30},
	{0x4e, 0x00},
	{0x4e, 0x80},
	{0x4e, 0x80},
	{0x4e, 0x02},
	{0x4e, 0x02},
	{0x4d, 0x40},
	{0x4e, 0x00},
	{0x4e, 0x80},
	{0x4e, 0x80},
	{0x4e, 0x02},
	{0x4e, 0x02},
	{0x4e, 0x02},
	{0x4d, 0x53},
	{0x4e, 0x08},
	{0x4e, 0x04},
	{0x4d, 0x62},
	{0x4e, 0x10},
	{0x4d, 0x72},
	{0x4e, 0x20},
	{0x4f, 0x01},
	/*   awb   */
	{0xfe, 0x01},
	{0x50, 0x88},
	{0x52, 0x40},
	{0x54, 0x60},
	{0x56, 0x06},
	{0x57, 0x20},
	{0x58, 0x01},
	{0x5b, 0x02},
	{0x61, 0xaa},
	{0x62, 0xaa},
	{0x71, 0x00},
	{0x74, 0x10},
	{0x77, 0x08},
	{0x78, 0xfd},
	{0x86, 0x30},
	{0x87, 0x00},
	{0x88, 0x04},
	{0x8a, 0xc0},
	{0x89, 0x75},
	{0x84, 0x08},
	{0x8b, 0x00},
	{0x8d, 0x70},
	{0x8e, 0x70},
	{0x8f, 0xf4},
	{0xfe, 0x00},
	{0x82, 0x02},
	/*   asde  */
	{0xfe, 0x01},
	{0x21, 0xbf},
	{0xfe, 0x02},
	{0xa4, 0x00},
	{0xa5, 0x40},
	{0xa2, 0xa0},
	{0xa6, 0x80},
	{0xa7, 0x80},
	{0xab, 0x31},
	{0xa9, 0x6f},
	{0xb0, 0x99},
	{0xb1, 0x34},
	{0xb3, 0x80},
	{0xde, 0xb6},
	{0x38, 0x0f},
	{0x39, 0x60},
	{0xfe, 0x00},
	{0x81, 0x26},
	{0xfe, 0x02},
	{0x83, 0x00},
	{0x84, 0x45},
	/*   YCP   */
	{0xd1, 0x38},
	{0xd2, 0x38},
	{0xd3,	0x40},
	{0xd4, 0x80},
	{0xd5, 0x00},
	{0xdc, 0x30},
	{0xdd, 0xb8},
	{0xfe, 0x00},
	/*   dndd  */
	{0xfe, 0x02},
	{0x88, 0x15},
	{0x8c, 0xf6},
	{0x89, 0x03},
	/*    EE   */
	{0xfe, 0x02},
	{0x90, 0x6c},
	{0x97, 0x45},
	/* RGB Gamma */
	{0xfe, 0x02},
	{0x15, 0x0a},
	{0x16, 0x12},
	{0x17, 0x19},
	{0x18, 0x1f},
	{0x19, 0x2c},
	{0x1a, 0x38},
	{0x1b, 0x42},
	{0x1c, 0x4e},
	{0x1d, 0x63},
	{0x1e, 0x76},
	{0x1f, 0x87},
	{0x20, 0x96},
	{0x21, 0xa2},
	{0x22, 0xb8},
	{0x23, 0xca},
	{0x24, 0xd8},
	{0x25, 0xe3},
	{0x26, 0xf0},
	{0x27, 0xf8},
	{0x28, 0xfd},
	{0x29, 0xff},
	/* y gamma */
	{0xfe, 0x02},
	{0x2b, 0x00},
	{0x2c, 0x04},
	{0x2d, 0x09},
	{0x2e, 0x18},
	{0x2f, 0x27},
	{0x30, 0x37},
	{0x31, 0x49},
	{0x32, 0x5c},
	{0x33, 0x7e},
	{0x34, 0xa0},
	{0x35, 0xc0},
	{0x36, 0xe0},
	{0x37, 0xff},
	{0xfe, 0x00},
	{0x90, 0x01},
	{0x95, 0x04},
	{0x96, 0xb0},
	{0x97, 0x06},
	{0x98, 0x40},
	{0xfe, 0x03},
	{0x42, 0x40},
	{0x43, 0x06},
	{0x41, 0x02},
	{0x40, 0x40},
	{0x17, 0x00},
	{0xfe, 0x00},
	/*   DVP   */
	{0xfe, 0x00},
	{0xb6, 0x03},
	{0xf7, 0x15},
	{0xc8, 0x00},
	{0x99, 0x22},
	{0x9a, 0x06},
	{0x9b, 0x00},
	{0x9c, 0x00},
	{0x9d, 0x00},
	{0x9e, 0x00},
	{0x9f, 0x00},
	{0xa0, 0x00},
	{0xa1, 0x00},
	{0xa2, 0x00},
	{0x90, 0x01},
	{0x94, 0x02},
	{0x95, 0x02},
	{0x96, 0x58},
	{0x97, 0x03},
	{0x98, 0x20},
	{0xfe, 0x00},
	{0x82, 0xfe},
	{REG_NULL, 0x00},
};

static const struct sensor_register gc2035_svga_regs[] = {
	{0xfe, 0x00},
	{0xb6, 0x03},
	{0xf7, 0x15},
	{0xc8, 0x00},
	{0x99, 0x22},
	{0x9a, 0x06},
	{0x9b, 0x00},
	{0x9c, 0x00},
	{0x9d, 0x00},
	{0x9e, 0x00},
	{0x9f, 0x00},
	{0xa0, 0x00},
	{0xa1, 0x00},
	{0xa2, 0x00},
	{0x90, 0x01},
	{0x94, 0x02},
	{0x95, 0x02},
	{0x96, 0x58},
	{0x97, 0x03},
	{0x98, 0x20},
	{REG_NULL, 0x00},
};

static const struct sensor_register gc2035_full_regs[] = {
	{0xfe, 0x00},
	{0xc8, 0x00},
	{0xf7, 0x17},
	{0x99, 0x11},
	{0x9a, 0x06},
	{0x9b, 0x00},
	{0x9c, 0x00},
	{0x9d, 0x00},
	{0x9e, 0x00},
	{0x9f, 0x00},
	{0xa0, 0x00},
	{0xa1, 0x00},
	{0xa2, 0x00},
	{0x90, 0x01},
	{0x95, 0x04},
	{0x96, 0xb0},
	{0x97, 0x06},
	{0x98, 0x40},
	{REG_NULL, 0x00},
};

static const struct gc2035_framesize gc2035_framesizes[] = {
	{
		.width		= 800,
		.height		= 600,
		.max_fps = {
			.numerator = 10000,
			.denominator = 120000,
		},
		.regs		= gc2035_svga_regs,
	},
	{
		.width		= 1600,
		.height		= 1200,
		.max_fps = {
			.numerator = 10000,
			.denominator = 60000,
		},
		.regs		= gc2035_full_regs,
	}
};

static const struct gc2035_pixfmt gc2035_formats[] = {
	{
		.code = MEDIA_BUS_FMT_UYVY8_2X8,
	}
};

static inline struct gc2035 *to_gc2035(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc2035, sd);
}

/* sensor register write */
static int gc2035_write(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	dev_err(&client->dev,
		"gc2035 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

/* sensor register read */
static int gc2035_read(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[1];
	int ret;

	buf[0] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(&client->dev,
		"gc2035 read reg:0x%x failed!\n", reg);

	return ret;
}

static int gc2035_write_array(struct i2c_client *client,
			      const struct sensor_register *regs)
{
	int i, ret = 0;

	i = 0;
	while (regs[i].addr != REG_NULL) {
		ret = gc2035_write(client, regs[i].addr, regs[i].value);
		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}

		i++;
	}

	return ret;
}

static void gc2035_get_default_format(struct v4l2_mbus_framefmt *format)
{
	format->width = gc2035_framesizes[0].width;
	format->height = gc2035_framesizes[0].height;
	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->code = gc2035_formats[0].code;
	format->field = V4L2_FIELD_NONE;
}

static void gc2035_set_streaming(struct gc2035 *gc2035, int on)
{
	struct i2c_client *client = gc2035->client;
	int ret;

	dev_dbg(&client->dev, "%s: on: %d\n", __func__, on);

	ret = gc2035_write(client, 0xfe, 0x00);
	if (!on) {
		ret |= gc2035_write(client, 0xf2, 0x00);
		ret |= gc2035_write(client, 0xf3, 0x00);
		ret |= gc2035_write(client, 0xf5, 0x00);
	} else {
		ret |= gc2035_write(client, 0xf2, 0x70);
		ret |= gc2035_write(client, 0xf3, 0xff);
		ret |= gc2035_write(client, 0xf4, 0x00);
		ret |= gc2035_write(client, 0xf5, 0x30);
	}
	if (ret)
		dev_err(&client->dev, "gc2035 soft standby failed\n");
}

/*
 * V4L2 subdev video and pad level operations
 */

static int gc2035_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (code->index >= ARRAY_SIZE(gc2035_formats))
		return -EINVAL;

	code->code = gc2035_formats[code->index].code;

	return 0;
}

static int gc2035_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i = ARRAY_SIZE(gc2035_formats);

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (fse->index >= ARRAY_SIZE(gc2035_framesizes))
		return -EINVAL;

	while (--i)
		if (fse->code == gc2035_formats[i].code)
			break;

	fse->code = gc2035_formats[i].code;

	fse->min_width  = gc2035_framesizes[fse->index].width;
	fse->max_width  = fse->min_width;
	fse->max_height = gc2035_framesizes[fse->index].height;
	fse->min_height = fse->max_height;

	return 0;
}

static int gc2035_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc2035 *gc2035 = to_gc2035(sd);

	dev_dbg(&client->dev, "%s enter\n", __func__);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		struct v4l2_mbus_framefmt *mf;

		mf = v4l2_subdev_get_try_format(sd, cfg, 0);
		mutex_lock(&gc2035->lock);
		fmt->format = *mf;
		mutex_unlock(&gc2035->lock);
		return 0;
#else
	return -ENOTTY;
#endif
	}

	mutex_lock(&gc2035->lock);
	fmt->format = gc2035->format;
	mutex_unlock(&gc2035->lock);

	dev_dbg(&client->dev, "%s: %x %dx%d\n", __func__,
		gc2035->format.code, gc2035->format.width,
		gc2035->format.height);

	return 0;
}

static void __gc2035_try_frame_size_fps(struct v4l2_mbus_framefmt *mf,
				    const struct gc2035_framesize **size,
				    unsigned int fps)
{
	const struct gc2035_framesize *fsize = &gc2035_framesizes[0];
	const struct gc2035_framesize *match = NULL;
	unsigned int i = ARRAY_SIZE(gc2035_framesizes);
	unsigned int min_err = UINT_MAX;

	while (i--) {
		unsigned int err = abs(fsize->width - mf->width)
				+ abs(fsize->height - mf->height);
		if (err < min_err && fsize->regs[0].addr) {
			min_err = err;
			match = fsize;
		}
		fsize++;
	}

	if (!match) {
		match = &gc2035_framesizes[0];
	} else {
		fsize = &gc2035_framesizes[0];
		for (i = 0; i < ARRAY_SIZE(gc2035_framesizes); i++) {
			if (fsize->width == match->width &&
				fsize->height == match->height &&
				fps >= DIV_ROUND_CLOSEST(fsize->max_fps.denominator,
				fsize->max_fps.numerator))
				match = fsize;

			fsize++;
		}
	}

	mf->width  = match->width;
	mf->height = match->height;

	if (size)
		*size = match;
}

static int gc2035_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int index = ARRAY_SIZE(gc2035_formats);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	const struct gc2035_framesize *size = NULL;
	struct gc2035 *gc2035 = to_gc2035(sd);
	int ret = 0;

	dev_dbg(&client->dev, "%s enter\n", __func__);

	__gc2035_try_frame_size_fps(mf, &size, gc2035->fps);

	while (--index >= 0)
		if (gc2035_formats[index].code == mf->code)
			break;

	if (index < 0)
		return -EINVAL;

	mf->colorspace = V4L2_COLORSPACE_SRGB;
	mf->code = gc2035_formats[index].code;
	mf->field = V4L2_FIELD_NONE;

	mutex_lock(&gc2035->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*mf = fmt->format;
#else
		return -ENOTTY;
#endif
	} else {
		if (gc2035->streaming) {
			mutex_unlock(&gc2035->lock);
			return -EBUSY;
		}

		gc2035->frame_size = size;
		gc2035->format = fmt->format;
	}

	mutex_unlock(&gc2035->lock);
	return ret;
}

static int gc2035_s_stream(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc2035 *gc2035 = to_gc2035(sd);
	int ret = 0;

	dev_dbg(&client->dev, "%s: on: %d\n", __func__, on);

	mutex_lock(&gc2035->lock);

	on = !!on;

	if (gc2035->streaming == on)
		goto unlock;

	if (!on) {
		/* Stop Streaming Sequence */
		gc2035_set_streaming(gc2035, on);
		gc2035->streaming = on;
		if (!IS_ERR(gc2035->pwdn_gpio)) {
			gpiod_set_value_cansleep(gc2035->pwdn_gpio, 1);
			usleep_range(2000, 5000);
		}
		goto unlock;
	}

	if (!IS_ERR(gc2035->pwdn_gpio)) {
		gpiod_set_value_cansleep(gc2035->pwdn_gpio, 0);
		usleep_range(2000, 5000);
	}

	ret = gc2035_write_array(client, gc2035_init_regs);
	if (ret)
		goto unlock;

	ret = gc2035_write_array(client, gc2035->frame_size->regs);
	if (ret)
		goto unlock;

	gc2035_set_streaming(gc2035, on);
	gc2035->streaming = on;

unlock:
	mutex_unlock(&gc2035->lock);
	return ret;
}

static int gc2035_set_test_pattern(struct gc2035 *gc2035, int value)
{
	return 0;
}

static int gc2035_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc2035 *gc2035 =
			container_of(ctrl->handler, struct gc2035, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		return gc2035_set_test_pattern(gc2035, ctrl->val);
	}

	return 0;
}

static const struct v4l2_ctrl_ops gc2035_ctrl_ops = {
	.s_ctrl = gc2035_s_ctrl,
};

static const char * const gc2035_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bars",
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev internal operations
 */

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc2035_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);

	dev_dbg(&client->dev, "%s:\n", __func__);

	gc2035_get_default_format(format);

	return 0;
}
#endif

static int gc2035_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	config->type = V4L2_MBUS_PARALLEL;
	config->flags = V4L2_MBUS_HSYNC_ACTIVE_HIGH |
			V4L2_MBUS_VSYNC_ACTIVE_HIGH |
			V4L2_MBUS_PCLK_SAMPLE_RISING;

	return 0;
}

static int gc2035_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc2035 *gc2035 = to_gc2035(sd);

	mutex_lock(&gc2035->lock);
	fi->interval = gc2035->frame_size->max_fps;
	mutex_unlock(&gc2035->lock);

	return 0;
}

static int gc2035_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc2035 *gc2035 = to_gc2035(sd);
	const struct gc2035_framesize *size = NULL;
	struct v4l2_mbus_framefmt mf;
	unsigned int fps;
	int ret = 0;

	dev_dbg(&client->dev, "Setting %d/%d frame interval\n",
		fi->interval.numerator, fi->interval.denominator);

	mutex_lock(&gc2035->lock);
	fps = DIV_ROUND_CLOSEST(fi->interval.denominator,
		fi->interval.numerator);
	mf = gc2035->format;
	__gc2035_try_frame_size_fps(&mf, &size, fps);
	if (gc2035->frame_size != size) {
		ret = gc2035_write_array(client, size->regs);
		if (ret)
			goto unlock;
		gc2035->frame_size = size;
		gc2035->fps = fps;
	}
unlock:
	mutex_unlock(&gc2035->lock);

	return ret;
}

static void gc2035_get_module_inf(struct gc2035 *gc2035,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, DRIVER_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, gc2035->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, gc2035->len_name, sizeof(inf->base.lens));
}

static long gc2035_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc2035 *gc2035 = to_gc2035(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc2035_get_module_inf(gc2035, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		gc2035_set_streaming(gc2035, !!stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc2035_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc2035_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = gc2035_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc2035_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int gc2035_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(gc2035_framesizes))
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_UYVY8_2X8)
		return -EINVAL;

	fie->width = gc2035_framesizes[fie->index].width;
	fie->height = gc2035_framesizes[fie->index].height;
	fie->interval = gc2035_framesizes[fie->index].max_fps;
	return 0;
}

static const struct v4l2_subdev_core_ops gc2035_subdev_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.ioctl = gc2035_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc2035_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc2035_subdev_video_ops = {
	.s_stream = gc2035_s_stream,
	.g_mbus_config = gc2035_g_mbus_config,
	.g_frame_interval = gc2035_g_frame_interval,
	.s_frame_interval = gc2035_s_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc2035_subdev_pad_ops = {
	.enum_mbus_code = gc2035_enum_mbus_code,
	.enum_frame_size = gc2035_enum_frame_sizes,
	.enum_frame_interval = gc2035_enum_frame_interval,
	.get_fmt = gc2035_get_fmt,
	.set_fmt = gc2035_set_fmt,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_ops gc2035_subdev_ops = {
	.core  = &gc2035_subdev_core_ops,
	.video = &gc2035_subdev_video_ops,
	.pad   = &gc2035_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops gc2035_subdev_internal_ops = {
	.open = gc2035_open,
};
#endif

static int gc2035_detect(struct gc2035 *gc2035)
{
	struct i2c_client *client = gc2035->client;
	u8 pidh = 0, pidl = 0;
	int ret;

	dev_dbg(&client->dev, "%s:\n", __func__);

	/* Check sensor revision */
	ret = gc2035_read(client, REG_SC_CHIP_ID_H, &pidh);
	ret |= gc2035_read(client, REG_SC_CHIP_ID_L, &pidl);
	if (!ret) {
		if (pidh == GC2035_ID_H &&
			pidl == GC2035_ID_L) {
			dev_info(&client->dev,
				"Found GC%02X%02X sensor\n",
				pidh, pidl);
			if (!IS_ERR(gc2035->pwdn_gpio))
				gpiod_set_value_cansleep(gc2035->pwdn_gpio, 1);
		} else {
			ret = -1;
			dev_err(&client->dev,
				"Sensor detection failed (%02x%02x, %d)\n",
				pidh, pidl, ret);
		}
	}

	return ret;
}

static int __gc2035_power_on(struct gc2035 *gc2035)
{
	int ret;
	struct device *dev = &gc2035->client->dev;

	if (!IS_ERR(gc2035->xvclk)) {
		ret = clk_set_rate(gc2035->xvclk, 24000000);
		if (ret < 0)
			dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
	}

	if (!IS_ERR(gc2035->pwdn_gpio)) {
		gpiod_set_value_cansleep(gc2035->pwdn_gpio, 1);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc2035->supplies)) {
		ret = regulator_bulk_enable(GC2035_NUM_SUPPLIES,
			gc2035->supplies);
		if (ret < 0)
			dev_err(dev, "Failed to enable regulators\n");

		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc2035->pwdn_gpio)) {
		gpiod_set_value_cansleep(gc2035->pwdn_gpio, 0);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc2035->xvclk)) {
		ret = clk_prepare_enable(gc2035->xvclk);
		if (ret < 0)
			dev_err(dev, "Failed to enable xvclk\n");
	}

	usleep_range(7000, 10000);

	return 0;
}

static void __gc2035_power_off(struct gc2035 *gc2035)
{
	if (!IS_ERR(gc2035->xvclk))
		clk_disable_unprepare(gc2035->xvclk);
	if (!IS_ERR(gc2035->supplies))
		regulator_bulk_disable(GC2035_NUM_SUPPLIES, gc2035->supplies);
	if (!IS_ERR(gc2035->pwdn_gpio))
		gpiod_set_value_cansleep(gc2035->pwdn_gpio, 1);
}

static int gc2035_configure_regulators(struct gc2035 *gc2035)
{
	unsigned int i;

	for (i = 0; i < GC2035_NUM_SUPPLIES; i++)
		gc2035->supplies[i].supply = gc2035_supply_names[i];

	return devm_regulator_bulk_get(&gc2035->client->dev,
				       GC2035_NUM_SUPPLIES,
				       gc2035->supplies);
}

static int gc2035_parse_of(struct gc2035 *gc2035)
{
	struct device *dev = &gc2035->client->dev;
	int ret;

	gc2035->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc2035->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios, maybe no used\n");

	ret = gc2035_configure_regulators(gc2035);
	if (ret)
		dev_warn(dev, "Failed to get power regulators\n");

	return __gc2035_power_on(gc2035);
}

static int gc2035_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct v4l2_subdev *sd;
	struct gc2035 *gc2035;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	gc2035 = devm_kzalloc(&client->dev, sizeof(*gc2035), GFP_KERNEL);
	if (!gc2035)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc2035->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc2035->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc2035->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc2035->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	gc2035->client = client;
	gc2035->xvclk = devm_clk_get(&client->dev, "xvclk");
	if (IS_ERR(gc2035->xvclk)) {
		dev_err(&client->dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc2035_parse_of(gc2035);

	gc2035->xvclk_frequency = clk_get_rate(gc2035->xvclk);
	if (gc2035->xvclk_frequency < 6000000 ||
	    gc2035->xvclk_frequency > 27000000)
		return -EINVAL;

	v4l2_ctrl_handler_init(&gc2035->ctrls, 2);
	gc2035->link_frequency =
			v4l2_ctrl_new_std(&gc2035->ctrls, &gc2035_ctrl_ops,
					  V4L2_CID_PIXEL_RATE, 0,
					  GC2035_PIXEL_RATE, 1,
					  GC2035_PIXEL_RATE);

	v4l2_ctrl_new_std_menu_items(&gc2035->ctrls, &gc2035_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(gc2035_test_pattern_menu) - 1,
				     0, 0, gc2035_test_pattern_menu);
	gc2035->sd.ctrl_handler = &gc2035->ctrls;

	if (gc2035->ctrls.error) {
		dev_err(&client->dev, "%s: control initialization error %d\n",
			__func__, gc2035->ctrls.error);
		return  gc2035->ctrls.error;
	}

	sd = &gc2035->sd;
	client->flags |= I2C_CLIENT_SCCB;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	v4l2_i2c_subdev_init(sd, client, &gc2035_subdev_ops);

	sd->internal_ops = &gc2035_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	gc2035->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc2035->pad);
	if (ret < 0) {
		v4l2_ctrl_handler_free(&gc2035->ctrls);
		return ret;
	}
#endif

	mutex_init(&gc2035->lock);

	gc2035_get_default_format(&gc2035->format);
	gc2035->frame_size = &gc2035_framesizes[0];
	gc2035->format.width = gc2035_framesizes[0].width;
	gc2035->format.height = gc2035_framesizes[0].height;
	gc2035->fps = DIV_ROUND_CLOSEST(gc2035_framesizes[0].max_fps.denominator,
				gc2035_framesizes[0].max_fps.numerator);

	ret = gc2035_detect(gc2035);
	if (ret < 0)
		goto error;

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc2035->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc2035->module_index, facing,
		 DRIVER_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret)
		goto error;

	dev_info(&client->dev, "%s sensor driver registered !!\n", sd->name);

	return 0;

error:
	v4l2_ctrl_handler_free(&gc2035->ctrls);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&gc2035->lock);
	__gc2035_power_off(gc2035);
	return ret;
}

static int gc2035_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2035 *gc2035 = to_gc2035(sd);

	v4l2_ctrl_handler_free(&gc2035->ctrls);
	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&gc2035->lock);

	__gc2035_power_off(gc2035);

	return 0;
}

static const struct i2c_device_id gc2035_id[] = {
	{ "gc2035", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, gc2035_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc2035_of_match[] = {
	{ .compatible = "galaxycore,gc2035", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, gc2035_of_match);
#endif

static struct i2c_driver gc2035_i2c_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(gc2035_of_match),
	},
	.probe		= gc2035_probe,
	.remove		= gc2035_remove,
	.id_table	= gc2035_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc2035_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc2035_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("GC2035 CMOS Image Sensor driver");
MODULE_LICENSE("GPL v2");
