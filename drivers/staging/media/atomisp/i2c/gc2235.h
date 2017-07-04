/*
 * Support for GalaxyCore GC2235 2M camera sensor.
 *
 * Copyright (c) 2014 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 *
 */

#ifndef __GC2235_H__
#define __GC2235_H__
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/spinlock.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <linux/v4l2-mediabus.h>
#include <media/media-entity.h>

#include "../include/linux/atomisp_platform.h"

#define GC2235_NAME		"gc2235"

/* Defines for register writes and register array processing */
#define I2C_MSG_LENGTH		0x2
#define I2C_RETRY_COUNT		5

#define GC2235_FOCAL_LENGTH_NUM	278	/*2.78mm*/
#define GC2235_FOCAL_LENGTH_DEM	100
#define GC2235_F_NUMBER_DEFAULT_NUM	26
#define GC2235_F_NUMBER_DEM	10

#define MAX_FMTS		1

/*
 * focal length bits definition:
 * bits 31-16: numerator, bits 15-0: denominator
 */
#define GC2235_FOCAL_LENGTH_DEFAULT 0x1160064

/*
 * current f-number bits definition:
 * bits 31-16: numerator, bits 15-0: denominator
 */
#define GC2235_F_NUMBER_DEFAULT 0x1a000a

/*
 * f-number range bits definition:
 * bits 31-24: max f-number numerator
 * bits 23-16: max f-number denominator
 * bits 15-8: min f-number numerator
 * bits 7-0: min f-number denominator
 */
#define GC2235_F_NUMBER_RANGE 0x1a0a1a0a
#define GC2235_ID	0x2235

#define GC2235_FINE_INTG_TIME_MIN 0
#define GC2235_FINE_INTG_TIME_MAX_MARGIN 0
#define GC2235_COARSE_INTG_TIME_MIN 1
#define GC2235_COARSE_INTG_TIME_MAX_MARGIN 6

/*
 * GC2235 System control registers
 */
/*
 * GC2235 System control registers
 */
#define GC2235_SENSOR_ID_H		0xF0
#define GC2235_SENSOR_ID_L		0xF1
#define GC2235_RESET_RELATED		0xFE
#define GC2235_SW_RESET			0x8
#define GC2235_MIPI_RESET		0x3
#define GC2235_RESET_BIT		0x4
#define GC2235_REGISTER_PAGE_0		0x0
#define GC2235_REGISTER_PAGE_3		0x3

#define GC2235_V_CROP_START_H		0x91
#define GC2235_V_CROP_START_L		0x92
#define GC2235_H_CROP_START_H		0x93
#define GC2235_H_CROP_START_L		0x94
#define GC2235_V_OUTSIZE_H		0x95
#define GC2235_V_OUTSIZE_L		0x96
#define GC2235_H_OUTSIZE_H		0x97
#define GC2235_H_OUTSIZE_L		0x98

#define GC2235_HB_H			0x5
#define GC2235_HB_L			0x6
#define GC2235_VB_H			0x7
#define GC2235_VB_L			0x8
#define GC2235_SH_DELAY_H		0x11
#define GC2235_SH_DELAY_L		0x12

#define GC2235_CSI2_MODE		0x10

#define GC2235_EXPOSURE_H		0x3
#define GC2235_EXPOSURE_L		0x4
#define GC2235_GLOBAL_GAIN		0xB0
#define GC2235_PRE_GAIN			0xB1
#define GC2235_AWB_R_GAIN		0xB3
#define GC2235_AWB_G_GAIN		0xB4
#define GC2235_AWB_B_GAIN		0xB5

#define GC2235_START_STREAMING		0x91
#define GC2235_STOP_STREAMING		0x0

struct regval_list {
	u16 reg_num;
	u8 value;
};

struct gc2235_resolution {
	u8 *desc;
	const struct gc2235_reg *regs;
	int res;
	int width;
	int height;
	int fps;
	int pix_clk_freq;
	u32 skip_frames;
	u16 pixels_per_line;
	u16 lines_per_frame;
	u8 bin_factor_x;
	u8 bin_factor_y;
	u8 bin_mode;
	bool used;
};

struct gc2235_format {
	u8 *desc;
	u32 pixelformat;
	struct gc2235_reg *regs;
};

/*
 * gc2235 device structure.
 */
struct gc2235_device {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	struct mutex input_lock;
	struct v4l2_ctrl_handler ctrl_handler;

	struct camera_sensor_platform_data *platform_data;
	int vt_pix_clk_freq_mhz;
	int fmt_idx;
	int run_mode;
	u8 res;
	u8 type;
};

enum gc2235_tok_type {
	GC2235_8BIT  = 0x0001,
	GC2235_16BIT = 0x0002,
	GC2235_32BIT = 0x0004,
	GC2235_TOK_TERM   = 0xf000,	/* terminating token for reg list */
	GC2235_TOK_DELAY  = 0xfe00,	/* delay token for reg list */
	GC2235_TOK_MASK = 0xfff0
};

/**
 * struct gc2235_reg - MI sensor  register format
 * @type: type of the register
 * @reg: 8-bit offset to register
 * @val: 8/16/32-bit register value
 *
 * Define a structure for sensor register initialization values
 */
struct gc2235_reg {
	enum gc2235_tok_type type;
	u8 reg;
	u32 val;	/* @set value for read/mod/write, @mask */
};

#define to_gc2235_sensor(x) container_of(x, struct gc2235_device, sd)

#define GC2235_MAX_WRITE_BUF_SIZE	30

struct gc2235_write_buffer {
	u8 addr;
	u8 data[GC2235_MAX_WRITE_BUF_SIZE];
};

struct gc2235_write_ctrl {
	int index;
	struct gc2235_write_buffer buffer;
};

static const struct i2c_device_id gc2235_id[] = {
	{GC2235_NAME, 0},
	{}
};

static struct gc2235_reg const gc2235_stream_on[] = {
	{ GC2235_8BIT, 0xfe, 0x03}, /* switch to P3 */
	{ GC2235_8BIT, 0x10, 0x91}, /* start mipi */
	{ GC2235_8BIT, 0xfe, 0x00}, /* switch to P0 */
	{ GC2235_TOK_TERM, 0, 0 }
};

static struct gc2235_reg const gc2235_stream_off[] = {
	{ GC2235_8BIT, 0xfe, 0x03}, /* switch to P3 */
	{ GC2235_8BIT, 0x10, 0x01}, /* stop mipi */
	{ GC2235_8BIT, 0xfe, 0x00}, /* switch to P0 */
	{ GC2235_TOK_TERM, 0, 0 }
};

static struct gc2235_reg const gc2235_init_settings[] = {
	/* Sysytem */
	{ GC2235_8BIT, 0xfe, 0x80 },
	{ GC2235_8BIT, 0xfe, 0x80 },
	{ GC2235_8BIT, 0xfe, 0x80 },
	{ GC2235_8BIT, 0xf2, 0x00 },
	{ GC2235_8BIT, 0xf6, 0x00 },
	{ GC2235_8BIT, 0xfc, 0x06 },
	{ GC2235_8BIT, 0xf7, 0x15 },
	{ GC2235_8BIT, 0xf8, 0x84 },
	{ GC2235_8BIT, 0xf9, 0xfe },
	{ GC2235_8BIT, 0xfa, 0x00 },
	{ GC2235_8BIT, 0xfe, 0x00 },
	/* Analog & cisctl */
	{ GC2235_8BIT, 0x03, 0x04 },
	{ GC2235_8BIT, 0x04, 0x9E },
	{ GC2235_8BIT, 0x05, 0x00 },
	{ GC2235_8BIT, 0x06, 0xfd },
	{ GC2235_8BIT, 0x07, 0x00 },
	{ GC2235_8BIT, 0x08, 0x14 },
	{ GC2235_8BIT, 0x0a, 0x02 }, /* row start */
	{ GC2235_8BIT, 0x0c, 0x00 }, /* col start */
	{ GC2235_8BIT, 0x0d, 0x04 }, /* win height 1232 */
	{ GC2235_8BIT, 0x0e, 0xd0 },
	{ GC2235_8BIT, 0x0f, 0x06 }, /* win width: 1616 */
	{ GC2235_8BIT, 0x10, 0x60 },
	{ GC2235_8BIT, 0x17, 0x15 }, /* mirror flip */
	{ GC2235_8BIT, 0x18, 0x1a },
	{ GC2235_8BIT, 0x19, 0x06 },
	{ GC2235_8BIT, 0x1a, 0x01 },
	{ GC2235_8BIT, 0x1b, 0x4d },
	{ GC2235_8BIT, 0x1e, 0x88 },
	{ GC2235_8BIT, 0x1f, 0x48 },
	{ GC2235_8BIT, 0x20, 0x03 },
	{ GC2235_8BIT, 0x21, 0x7f },
	{ GC2235_8BIT, 0x22, 0x83 },
	{ GC2235_8BIT, 0x23, 0x42 },
	{ GC2235_8BIT, 0x24, 0x16 },
	{ GC2235_8BIT, 0x26, 0x01 }, /*analog gain*/
	{ GC2235_8BIT, 0x27, 0x30 },
	{ GC2235_8BIT, 0x3f, 0x00 }, /* PRC */
	/* blk */
	{ GC2235_8BIT, 0x40, 0xa3 },
	{ GC2235_8BIT, 0x41, 0x82 },
	{ GC2235_8BIT, 0x43, 0x20 },
	{ GC2235_8BIT, 0x5e, 0x18 },
	{ GC2235_8BIT, 0x5f, 0x18 },
	{ GC2235_8BIT, 0x60, 0x18 },
	{ GC2235_8BIT, 0x61, 0x18 },
	{ GC2235_8BIT, 0x62, 0x18 },
	{ GC2235_8BIT, 0x63, 0x18 },
	{ GC2235_8BIT, 0x64, 0x18 },
	{ GC2235_8BIT, 0x65, 0x18 },
	{ GC2235_8BIT, 0x66, 0x20 },
	{ GC2235_8BIT, 0x67, 0x20 },
	{ GC2235_8BIT, 0x68, 0x20 },
	{ GC2235_8BIT, 0x69, 0x20 },
	/* Gain */
	{ GC2235_8BIT, 0xb2, 0x00 },
	{ GC2235_8BIT, 0xb3, 0x40 },
	{ GC2235_8BIT, 0xb4, 0x40 },
	{ GC2235_8BIT, 0xb5, 0x40 },
	/* Dark sun */
	{ GC2235_8BIT, 0xbc, 0x00 },

	{ GC2235_8BIT, 0xfe, 0x03 },
	{ GC2235_8BIT, 0x10, 0x01 }, /* disable mipi */
	{ GC2235_8BIT, 0xfe, 0x00 }, /* switch to P0 */
	{ GC2235_TOK_TERM, 0, 0 }
};
/*
 * Register settings for various resolution
 */
static struct gc2235_reg const gc2235_1296_736_30fps[] = {
	{ GC2235_8BIT, 0x8b, 0xa0 },
	{ GC2235_8BIT, 0x8c, 0x02 },

	{ GC2235_8BIT, 0x07, 0x01 }, /* VBI */
	{ GC2235_8BIT, 0x08, 0x44 },
	{ GC2235_8BIT, 0x09, 0x00 }, /* row start */
	{ GC2235_8BIT, 0x0a, 0xf0 },
	{ GC2235_8BIT, 0x0b, 0x00 }, /* col start */
	{ GC2235_8BIT, 0x0c, 0xa0 },
	{ GC2235_8BIT, 0x0d, 0x02 }, /* win height 736 */
	{ GC2235_8BIT, 0x0e, 0xf0 },
	{ GC2235_8BIT, 0x0f, 0x05 }, /* win width: 1296 */
	{ GC2235_8BIT, 0x10, 0x20 },

	{ GC2235_8BIT, 0x90, 0x01 },
	{ GC2235_8BIT, 0x92, 0x08 },
	{ GC2235_8BIT, 0x94, 0x08 },
	{ GC2235_8BIT, 0x95, 0x02 }, /* crop win height 736 */
	{ GC2235_8BIT, 0x96, 0xe0 },
	{ GC2235_8BIT, 0x97, 0x05 }, /* crop win width 1296 */
	{ GC2235_8BIT, 0x98, 0x10 },
	/* mimi init */
	{ GC2235_8BIT, 0xfe, 0x03 }, /* switch to P3 */
	{ GC2235_8BIT, 0x01, 0x07 },
	{ GC2235_8BIT, 0x02, 0x11 },
	{ GC2235_8BIT, 0x03, 0x11 },
	{ GC2235_8BIT, 0x06, 0x80 },
	{ GC2235_8BIT, 0x11, 0x2b },
	/* set mipi buffer */
	{ GC2235_8BIT, 0x12, 0x54 }, /* val_low = (width * 10 / 8) & 0xFF */
	{ GC2235_8BIT, 0x13, 0x06 }, /* val_high = (width * 10 / 8) >> 8 */

	{ GC2235_8BIT, 0x15, 0x12 }, /* DPHY mode*/
	{ GC2235_8BIT, 0x04, 0x10 },
	{ GC2235_8BIT, 0x05, 0x00 },
	{ GC2235_8BIT, 0x17, 0x01 },

	{ GC2235_8BIT, 0x22, 0x01 },
	{ GC2235_8BIT, 0x23, 0x05 },
	{ GC2235_8BIT, 0x24, 0x10 },
	{ GC2235_8BIT, 0x25, 0x10 },
	{ GC2235_8BIT, 0x26, 0x02 },
	{ GC2235_8BIT, 0x21, 0x10 },
	{ GC2235_8BIT, 0x29, 0x01 },
	{ GC2235_8BIT, 0x2a, 0x02 },
	{ GC2235_8BIT, 0x2b, 0x02 },

	{ GC2235_8BIT, 0x10, 0x01 }, /* disable mipi */
	{ GC2235_8BIT, 0xfe, 0x00 }, /* switch to P0 */
	{ GC2235_TOK_TERM, 0, 0 }
};

static struct gc2235_reg const gc2235_960_640_30fps[] = {
	{ GC2235_8BIT, 0x8b, 0xa0 },
	{ GC2235_8BIT, 0x8c, 0x02 },

	{ GC2235_8BIT, 0x07, 0x02 }, /* VBI */
	{ GC2235_8BIT, 0x08, 0xA4 },
	{ GC2235_8BIT, 0x09, 0x01 }, /* row start */
	{ GC2235_8BIT, 0x0a, 0x18 },
	{ GC2235_8BIT, 0x0b, 0x01 }, /* col start */
	{ GC2235_8BIT, 0x0c, 0x40 },
	{ GC2235_8BIT, 0x0d, 0x02 }, /* win height 656 */
	{ GC2235_8BIT, 0x0e, 0x90 },
	{ GC2235_8BIT, 0x0f, 0x03 }, /* win width: 976 */
	{ GC2235_8BIT, 0x10, 0xd0 },

	{ GC2235_8BIT, 0x90, 0x01 },
	{ GC2235_8BIT, 0x92, 0x02 },
	{ GC2235_8BIT, 0x94, 0x06 },
	{ GC2235_8BIT, 0x95, 0x02 }, /* crop win height 640 */
	{ GC2235_8BIT, 0x96, 0x80 },
	{ GC2235_8BIT, 0x97, 0x03 }, /* crop win width 960 */
	{ GC2235_8BIT, 0x98, 0xc0 },
	/* mimp init */
	{ GC2235_8BIT, 0xfe, 0x03 }, /* switch to P3 */
	{ GC2235_8BIT, 0x01, 0x07 },
	{ GC2235_8BIT, 0x02, 0x11 },
	{ GC2235_8BIT, 0x03, 0x11 },
	{ GC2235_8BIT, 0x06, 0x80 },
	{ GC2235_8BIT, 0x11, 0x2b },
	/* set mipi buffer */
	{ GC2235_8BIT, 0x12, 0xb0 }, /* val_low = (width * 10 / 8) & 0xFF */
	{ GC2235_8BIT, 0x13, 0x04 }, /* val_high = (width * 10 / 8) >> 8 */

	{ GC2235_8BIT, 0x15, 0x12 }, /* DPHY mode*/
	{ GC2235_8BIT, 0x04, 0x10 },
	{ GC2235_8BIT, 0x05, 0x00 },
	{ GC2235_8BIT, 0x17, 0x01 },
	{ GC2235_8BIT, 0x22, 0x01 },
	{ GC2235_8BIT, 0x23, 0x05 },
	{ GC2235_8BIT, 0x24, 0x10 },
	{ GC2235_8BIT, 0x25, 0x10 },
	{ GC2235_8BIT, 0x26, 0x02 },
	{ GC2235_8BIT, 0x21, 0x10 },
	{ GC2235_8BIT, 0x29, 0x01 },
	{ GC2235_8BIT, 0x2a, 0x02 },
	{ GC2235_8BIT, 0x2b, 0x02 },
	{ GC2235_8BIT, 0x10, 0x01 }, /* disable mipi */
	{ GC2235_8BIT, 0xfe, 0x00 }, /* switch to P0 */
	{ GC2235_TOK_TERM, 0, 0 }
};

static struct gc2235_reg const gc2235_1600_900_30fps[] = {
	{ GC2235_8BIT, 0x8b, 0xa0 },
	{ GC2235_8BIT, 0x8c, 0x02 },

	{ GC2235_8BIT, 0x0d, 0x03 }, /* win height 932 */
	{ GC2235_8BIT, 0x0e, 0xa4 },
	{ GC2235_8BIT, 0x0f, 0x06 }, /* win width: 1632 */
	{ GC2235_8BIT, 0x10, 0x50 },

	{ GC2235_8BIT, 0x90, 0x01 },
	{ GC2235_8BIT, 0x92, 0x02 },
	{ GC2235_8BIT, 0x94, 0x06 },
	{ GC2235_8BIT, 0x95, 0x03 }, /* crop win height 900 */
	{ GC2235_8BIT, 0x96, 0x84 },
	{ GC2235_8BIT, 0x97, 0x06 }, /* crop win width 1600 */
	{ GC2235_8BIT, 0x98, 0x40 },
	/* mimi init */
	{ GC2235_8BIT, 0xfe, 0x03 }, /* switch to P3 */
	{ GC2235_8BIT, 0x01, 0x07 },
	{ GC2235_8BIT, 0x02, 0x11 },
	{ GC2235_8BIT, 0x03, 0x11 },
	{ GC2235_8BIT, 0x06, 0x80 },
	{ GC2235_8BIT, 0x11, 0x2b },
	/* set mipi buffer */
	{ GC2235_8BIT, 0x12, 0xd0 }, /* val_low = (width * 10 / 8) & 0xFF */
	{ GC2235_8BIT, 0x13, 0x07 }, /* val_high = (width * 10 / 8) >> 8 */

	{ GC2235_8BIT, 0x15, 0x12 }, /* DPHY mode*/
	{ GC2235_8BIT, 0x04, 0x10 },
	{ GC2235_8BIT, 0x05, 0x00 },
	{ GC2235_8BIT, 0x17, 0x01 },
	{ GC2235_8BIT, 0x22, 0x01 },
	{ GC2235_8BIT, 0x23, 0x05 },
	{ GC2235_8BIT, 0x24, 0x10 },
	{ GC2235_8BIT, 0x25, 0x10 },
	{ GC2235_8BIT, 0x26, 0x02 },
	{ GC2235_8BIT, 0x21, 0x10 },
	{ GC2235_8BIT, 0x29, 0x01 },
	{ GC2235_8BIT, 0x2a, 0x02 },
	{ GC2235_8BIT, 0x2b, 0x02 },
	{ GC2235_8BIT, 0x10, 0x01 }, /* disable mipi */
	{ GC2235_8BIT, 0xfe, 0x00 }, /* switch to P0 */
	{ GC2235_TOK_TERM, 0, 0 }
};

static struct gc2235_reg const gc2235_1616_1082_30fps[] = {
	{ GC2235_8BIT, 0x8b, 0xa0 },
	{ GC2235_8BIT, 0x8c, 0x02 },

	{ GC2235_8BIT, 0x0d, 0x04 }, /* win height 1232 */
	{ GC2235_8BIT, 0x0e, 0xd0 },
	{ GC2235_8BIT, 0x0f, 0x06 }, /* win width: 1616 */
	{ GC2235_8BIT, 0x10, 0x50 },

	{ GC2235_8BIT, 0x90, 0x01 },
	{ GC2235_8BIT, 0x92, 0x4a },
	{ GC2235_8BIT, 0x94, 0x00 },
	{ GC2235_8BIT, 0x95, 0x04 }, /* crop win height 1082 */
	{ GC2235_8BIT, 0x96, 0x3a },
	{ GC2235_8BIT, 0x97, 0x06 }, /* crop win width 1616 */
	{ GC2235_8BIT, 0x98, 0x50 },
	/* mimp init */
	{ GC2235_8BIT, 0xfe, 0x03 }, /* switch to P3 */
	{ GC2235_8BIT, 0x01, 0x07 },
	{ GC2235_8BIT, 0x02, 0x11 },
	{ GC2235_8BIT, 0x03, 0x11 },
	{ GC2235_8BIT, 0x06, 0x80 },
	{ GC2235_8BIT, 0x11, 0x2b },
	/* set mipi buffer */
	{ GC2235_8BIT, 0x12, 0xe4 }, /* val_low = (width * 10 / 8) & 0xFF */
	{ GC2235_8BIT, 0x13, 0x07 }, /* val_high = (width * 10 / 8) >> 8 */

	{ GC2235_8BIT, 0x15, 0x12 }, /* DPHY mode*/
	{ GC2235_8BIT, 0x04, 0x10 },
	{ GC2235_8BIT, 0x05, 0x00 },
	{ GC2235_8BIT, 0x17, 0x01 },
	{ GC2235_8BIT, 0x22, 0x01 },
	{ GC2235_8BIT, 0x23, 0x05 },
	{ GC2235_8BIT, 0x24, 0x10 },
	{ GC2235_8BIT, 0x25, 0x10 },
	{ GC2235_8BIT, 0x26, 0x02 },
	{ GC2235_8BIT, 0x21, 0x10 },
	{ GC2235_8BIT, 0x29, 0x01 },
	{ GC2235_8BIT, 0x2a, 0x02 },
	{ GC2235_8BIT, 0x2b, 0x02 },
	{ GC2235_8BIT, 0x10, 0x01 }, /* disable mipi */
	{ GC2235_8BIT, 0xfe, 0x00 }, /* switch to P0 */
	{ GC2235_TOK_TERM, 0, 0 }
};

static struct gc2235_reg const gc2235_1616_1216_30fps[] = {
	{ GC2235_8BIT, 0x8b, 0xa0 },
	{ GC2235_8BIT, 0x8c, 0x02 },

	{ GC2235_8BIT, 0x0d, 0x04 }, /* win height 1232 */
	{ GC2235_8BIT, 0x0e, 0xd0 },
	{ GC2235_8BIT, 0x0f, 0x06 }, /* win width: 1616 */
	{ GC2235_8BIT, 0x10, 0x50 },

	{ GC2235_8BIT, 0x90, 0x01 },
	{ GC2235_8BIT, 0x92, 0x02 },
	{ GC2235_8BIT, 0x94, 0x00 },
	{ GC2235_8BIT, 0x95, 0x04 }, /* crop win height 1216 */
	{ GC2235_8BIT, 0x96, 0xc0 },
	{ GC2235_8BIT, 0x97, 0x06 }, /* crop win width 1616 */
	{ GC2235_8BIT, 0x98, 0x50 },
	/* mimi init */
	{ GC2235_8BIT, 0xfe, 0x03 }, /* switch to P3 */
	{ GC2235_8BIT, 0x01, 0x07 },
	{ GC2235_8BIT, 0x02, 0x11 },
	{ GC2235_8BIT, 0x03, 0x11 },
	{ GC2235_8BIT, 0x06, 0x80 },
	{ GC2235_8BIT, 0x11, 0x2b },
	/* set mipi buffer */
	{ GC2235_8BIT, 0x12, 0xe4 }, /* val_low = (width * 10 / 8) & 0xFF */
	{ GC2235_8BIT, 0x13, 0x07 }, /* val_high = (width * 10 / 8) >> 8 */
	{ GC2235_8BIT, 0x15, 0x12 }, /* DPHY mode*/
	{ GC2235_8BIT, 0x04, 0x10 },
	{ GC2235_8BIT, 0x05, 0x00 },
	{ GC2235_8BIT, 0x17, 0x01 },
	{ GC2235_8BIT, 0x22, 0x01 },
	{ GC2235_8BIT, 0x23, 0x05 },
	{ GC2235_8BIT, 0x24, 0x10 },
	{ GC2235_8BIT, 0x25, 0x10 },
	{ GC2235_8BIT, 0x26, 0x02 },
	{ GC2235_8BIT, 0x21, 0x10 },
	{ GC2235_8BIT, 0x29, 0x01 },
	{ GC2235_8BIT, 0x2a, 0x02 },
	{ GC2235_8BIT, 0x2b, 0x02 },
	{ GC2235_8BIT, 0x10, 0x01 }, /* disable mipi */
	{ GC2235_8BIT, 0xfe, 0x00 }, /* switch to P0 */
	{ GC2235_TOK_TERM, 0, 0 }
};

static struct gc2235_resolution gc2235_res_preview[] = {

	{
		.desc = "gc2235_1600_900_30fps",
		.width = 1600,
		.height = 900,
		.pix_clk_freq = 30,
		.fps = 30,
		.used = 0,
		.pixels_per_line = 2132,
		.lines_per_frame = 1068,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.bin_mode = 0,
		.skip_frames = 3,
		.regs = gc2235_1600_900_30fps,
	},

	{
		.desc = "gc2235_1600_1066_30fps",
		.width = 1616,
		.height = 1082,
		.pix_clk_freq = 30,
		.fps = 30,
		.used = 0,
		.pixels_per_line = 2132,
		.lines_per_frame = 1368,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.bin_mode = 0,
		.skip_frames = 3,
		.regs = gc2235_1616_1082_30fps,
	},
	{
		.desc = "gc2235_1600_1200_30fps",
		.width = 1616,
		.height = 1216,
		.pix_clk_freq = 30,
		.fps = 30,
		.used = 0,
		.pixels_per_line = 2132,
		.lines_per_frame = 1368,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.bin_mode = 0,
		.skip_frames = 3,
		.regs = gc2235_1616_1216_30fps,
	},

};
#define N_RES_PREVIEW (ARRAY_SIZE(gc2235_res_preview))

static struct gc2235_resolution gc2235_res_still[] = {
	{
		.desc = "gc2235_1600_900_30fps",
		.width = 1600,
		.height = 900,
		.pix_clk_freq = 30,
		.fps = 30,
		.used = 0,
		.pixels_per_line = 2132,
		.lines_per_frame = 1068,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.bin_mode = 0,
		.skip_frames = 3,
		.regs = gc2235_1600_900_30fps,
	},
	{
		.desc = "gc2235_1600_1066_30fps",
		.width = 1616,
		.height = 1082,
		.pix_clk_freq = 30,
		.fps = 30,
		.used = 0,
		.pixels_per_line = 2132,
		.lines_per_frame = 1368,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.bin_mode = 0,
		.skip_frames = 3,
		.regs = gc2235_1616_1082_30fps,
	},
	{
		.desc = "gc2235_1600_1200_30fps",
		.width = 1616,
		.height = 1216,
		.pix_clk_freq = 30,
		.fps = 30,
		.used = 0,
		.pixels_per_line = 2132,
		.lines_per_frame = 1368,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.bin_mode = 0,
		.skip_frames = 3,
		.regs = gc2235_1616_1216_30fps,
	},

};
#define N_RES_STILL (ARRAY_SIZE(gc2235_res_still))

static struct gc2235_resolution gc2235_res_video[] = {
	{
		.desc = "gc2235_1296_736_30fps",
		.width = 1296,
		.height = 736,
		.pix_clk_freq = 30,
		.fps = 30,
		.used = 0,
		.pixels_per_line = 1828,
		.lines_per_frame = 888,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.bin_mode = 0,
		.skip_frames = 3,
		.regs = gc2235_1296_736_30fps,
	},
	{
		.desc = "gc2235_960_640_30fps",
		.width = 960,
		.height = 640,
		.pix_clk_freq = 30,
		.fps = 30,
		.used = 0,
		.pixels_per_line = 1492,
		.lines_per_frame = 792,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.bin_mode = 0,
		.skip_frames = 3,
		.regs = gc2235_960_640_30fps,
	},

};
#define N_RES_VIDEO (ARRAY_SIZE(gc2235_res_video))

static struct gc2235_resolution *gc2235_res = gc2235_res_preview;
static int N_RES = N_RES_PREVIEW;
#endif
