/*
 * Support for GalaxyCore GC0310 VGA camera sensor.
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
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
 *
 */

#ifndef __GC0310_H__
#define __GC0310_H__
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/spinlock.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <linux/v4l2-mediabus.h>
#include <media/media-entity.h>

#include "../include/linux/atomisp_platform.h"

/* Defines for register writes and register array processing */
#define I2C_MSG_LENGTH		1
#define I2C_RETRY_COUNT		5

#define GC0310_FOCAL_LENGTH_NUM	278	/*2.78mm*/
#define GC0310_FOCAL_LENGTH_DEM	100
#define GC0310_F_NUMBER_DEFAULT_NUM	26
#define GC0310_F_NUMBER_DEM	10

#define MAX_FMTS		1

/*
 * focal length bits definition:
 * bits 31-16: numerator, bits 15-0: denominator
 */
#define GC0310_FOCAL_LENGTH_DEFAULT 0x1160064

/*
 * current f-number bits definition:
 * bits 31-16: numerator, bits 15-0: denominator
 */
#define GC0310_F_NUMBER_DEFAULT 0x1a000a

/*
 * f-number range bits definition:
 * bits 31-24: max f-number numerator
 * bits 23-16: max f-number denominator
 * bits 15-8: min f-number numerator
 * bits 7-0: min f-number denominator
 */
#define GC0310_F_NUMBER_RANGE 0x1a0a1a0a
#define GC0310_ID	0xa310

#define GC0310_RESET_RELATED		0xFE
#define GC0310_REGISTER_PAGE_0		0x0
#define GC0310_REGISTER_PAGE_3		0x3

#define GC0310_FINE_INTG_TIME_MIN 0
#define GC0310_FINE_INTG_TIME_MAX_MARGIN 0
#define GC0310_COARSE_INTG_TIME_MIN 1
#define GC0310_COARSE_INTG_TIME_MAX_MARGIN 6

/*
 * GC0310 System control registers
 */
#define GC0310_SW_STREAM			0x10

#define GC0310_SC_CMMN_CHIP_ID_H		0xf0
#define GC0310_SC_CMMN_CHIP_ID_L		0xf1

#define GC0310_AEC_PK_EXPO_H			0x03
#define GC0310_AEC_PK_EXPO_L			0x04
#define GC0310_AGC_ADJ			0x48
#define GC0310_DGC_ADJ			0x71
#if 0
#define GC0310_GROUP_ACCESS			0x3208
#endif

#define GC0310_H_CROP_START_H			0x09
#define GC0310_H_CROP_START_L			0x0A
#define GC0310_V_CROP_START_H			0x0B
#define GC0310_V_CROP_START_L			0x0C
#define GC0310_H_OUTSIZE_H			0x0F
#define GC0310_H_OUTSIZE_L			0x10
#define GC0310_V_OUTSIZE_H			0x0D
#define GC0310_V_OUTSIZE_L			0x0E
#define GC0310_H_BLANKING_H			0x05
#define GC0310_H_BLANKING_L			0x06
#define GC0310_V_BLANKING_H			0x07
#define GC0310_V_BLANKING_L			0x08
#define GC0310_SH_DELAY			0x11

#define GC0310_START_STREAMING			0x94 /* 8-bit enable */
#define GC0310_STOP_STREAMING			0x0 /* 8-bit disable */

#define GC0310_BIN_FACTOR_MAX			3

struct regval_list {
	u16 reg_num;
	u8 value;
};

struct gc0310_resolution {
	u8 *desc;
	const struct gc0310_reg *regs;
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

struct gc0310_format {
	u8 *desc;
	u32 pixelformat;
	struct gc0310_reg *regs;
};

/*
 * gc0310 device structure.
 */
struct gc0310_device {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	struct mutex input_lock;
	struct v4l2_ctrl_handler ctrl_handler;

	struct camera_sensor_platform_data *platform_data;
	int vt_pix_clk_freq_mhz;
	int fmt_idx;
	u8 res;
	u8 type;
};

enum gc0310_tok_type {
	GC0310_8BIT  = 0x0001,
	GC0310_TOK_TERM   = 0xf000,	/* terminating token for reg list */
	GC0310_TOK_DELAY  = 0xfe00,	/* delay token for reg list */
	GC0310_TOK_MASK = 0xfff0
};

/**
 * struct gc0310_reg - MI sensor  register format
 * @type: type of the register
 * @reg: 16-bit offset to register
 * @val: 8/16/32-bit register value
 *
 * Define a structure for sensor register initialization values
 */
struct gc0310_reg {
	enum gc0310_tok_type type;
	u8 reg;
	u8 val;	/* @set value for read/mod/write, @mask */
};

#define to_gc0310_sensor(x) container_of(x, struct gc0310_device, sd)

#define GC0310_MAX_WRITE_BUF_SIZE	30

struct gc0310_write_buffer {
	u8 addr;
	u8 data[GC0310_MAX_WRITE_BUF_SIZE];
};

struct gc0310_write_ctrl {
	int index;
	struct gc0310_write_buffer buffer;
};

/*
 * Register settings for various resolution
 */
static const struct gc0310_reg gc0310_reset_register[] = {
/////////////////////////////////////////////////
/////////////////	system reg	/////////////////
/////////////////////////////////////////////////
	{GC0310_8BIT, 0xfe, 0xf0},
	{GC0310_8BIT, 0xfe, 0xf0},
	{GC0310_8BIT, 0xfe, 0x00},

	{GC0310_8BIT, 0xfc, 0x0e}, //4e
	{GC0310_8BIT, 0xfc, 0x0e}, //16//4e // [0]apwd [6]regf_clk_gate
	{GC0310_8BIT, 0xf2, 0x80}, //sync output
	{GC0310_8BIT, 0xf3, 0x00}, //1f//01 data output
	{GC0310_8BIT, 0xf7, 0x33}, //f9
	{GC0310_8BIT, 0xf8, 0x05}, //00
	{GC0310_8BIT, 0xf9, 0x0e}, // 0x8e //0f
	{GC0310_8BIT, 0xfa, 0x11},

/////////////////////////////////////////////////
///////////////////   MIPI	 ////////////////////
/////////////////////////////////////////////////
	{GC0310_8BIT, 0xfe, 0x03},
	{GC0310_8BIT, 0x01, 0x03}, ///mipi 1lane
	{GC0310_8BIT, 0x02, 0x22}, // 0x33
	{GC0310_8BIT, 0x03, 0x94},
	{GC0310_8BIT, 0x04, 0x01}, // fifo_prog
	{GC0310_8BIT, 0x05, 0x00}, //fifo_prog
	{GC0310_8BIT, 0x06, 0x80}, //b0  //YUV ISP data
	{GC0310_8BIT, 0x11, 0x2a},//1e //LDI set YUV422
	{GC0310_8BIT, 0x12, 0x90},//00 //04 //00 //04//00 //LWC[7:0]  //
	{GC0310_8BIT, 0x13, 0x02},//05 //05 //LWC[15:8]
	{GC0310_8BIT, 0x15, 0x12}, // 0x10 //DPHYY_MODE read_ready
	{GC0310_8BIT, 0x17, 0x01},
	{GC0310_8BIT, 0x40, 0x08},
	{GC0310_8BIT, 0x41, 0x00},
	{GC0310_8BIT, 0x42, 0x00},
	{GC0310_8BIT, 0x43, 0x00},
	{GC0310_8BIT, 0x21, 0x02}, // 0x01
	{GC0310_8BIT, 0x22, 0x02}, // 0x01
	{GC0310_8BIT, 0x23, 0x01}, // 0x05 //Nor:0x05 DOU:0x06
	{GC0310_8BIT, 0x29, 0x00},
	{GC0310_8BIT, 0x2A, 0x25}, // 0x05 //data zero 0x7a de
	{GC0310_8BIT, 0x2B, 0x02},

	{GC0310_8BIT, 0xfe, 0x00},

/////////////////////////////////////////////////
/////////////////	CISCTL reg	/////////////////
/////////////////////////////////////////////////
	{GC0310_8BIT, 0x00, 0x2f}, //2f//0f//02//01
	{GC0310_8BIT, 0x01, 0x0f}, //06
	{GC0310_8BIT, 0x02, 0x04},
	{GC0310_8BIT, 0x4f, 0x00}, //AEC 0FF
	{GC0310_8BIT, 0x03, 0x01}, // 0x03 //04
	{GC0310_8BIT, 0x04, 0xc0}, // 0xe8 //58
	{GC0310_8BIT, 0x05, 0x00},
	{GC0310_8BIT, 0x06, 0xb2}, // 0x0a //HB
	{GC0310_8BIT, 0x07, 0x00},
	{GC0310_8BIT, 0x08, 0x0c}, // 0x89 //VB
	{GC0310_8BIT, 0x09, 0x00}, //row start
	{GC0310_8BIT, 0x0a, 0x00}, //
	{GC0310_8BIT, 0x0b, 0x00}, //col start
	{GC0310_8BIT, 0x0c, 0x00},
	{GC0310_8BIT, 0x0d, 0x01}, //height
	{GC0310_8BIT, 0x0e, 0xf2}, // 0xf7 //height
	{GC0310_8BIT, 0x0f, 0x02}, //width
	{GC0310_8BIT, 0x10, 0x94}, // 0xa0 //height
	{GC0310_8BIT, 0x17, 0x14},
	{GC0310_8BIT, 0x18, 0x1a}, //0a//[4]double reset
	{GC0310_8BIT, 0x19, 0x14}, //AD pipeline
	{GC0310_8BIT, 0x1b, 0x48},
	{GC0310_8BIT, 0x1e, 0x6b}, //3b//col bias
	{GC0310_8BIT, 0x1f, 0x28}, //20//00//08//txlow
	{GC0310_8BIT, 0x20, 0x89}, //88//0c//[3:2]DA15
	{GC0310_8BIT, 0x21, 0x49}, //48//[3] txhigh
	{GC0310_8BIT, 0x22, 0xb0},
	{GC0310_8BIT, 0x23, 0x04}, //[1:0]vcm_r
	{GC0310_8BIT, 0x24, 0x16}, //15
	{GC0310_8BIT, 0x34, 0x20}, //[6:4] rsg high//range

/////////////////////////////////////////////////
////////////////////   BLK	 ////////////////////
/////////////////////////////////////////////////
	{GC0310_8BIT, 0x26, 0x23}, //[1]dark_current_en [0]offset_en
	{GC0310_8BIT, 0x28, 0xff}, //BLK_limie_value
	{GC0310_8BIT, 0x29, 0x00}, //global offset
	{GC0310_8BIT, 0x33, 0x18}, //offset_ratio
	{GC0310_8BIT, 0x37, 0x20}, //dark_current_ratio
	{GC0310_8BIT, 0x2a, 0x00},
	{GC0310_8BIT, 0x2b, 0x00},
	{GC0310_8BIT, 0x2c, 0x00},
	{GC0310_8BIT, 0x2d, 0x00},
	{GC0310_8BIT, 0x2e, 0x00},
	{GC0310_8BIT, 0x2f, 0x00},
	{GC0310_8BIT, 0x30, 0x00},
	{GC0310_8BIT, 0x31, 0x00},
	{GC0310_8BIT, 0x47, 0x80}, //a7
	{GC0310_8BIT, 0x4e, 0x66}, //select_row
	{GC0310_8BIT, 0xa8, 0x02}, //win_width_dark, same with crop_win_width
	{GC0310_8BIT, 0xa9, 0x80},

/////////////////////////////////////////////////
//////////////////	 ISP reg  ///////////////////
/////////////////////////////////////////////////
	{GC0310_8BIT, 0x40, 0x06}, // 0xff //ff //48
	{GC0310_8BIT, 0x41, 0x00}, // 0x21 //00//[0]curve_en
	{GC0310_8BIT, 0x42, 0x04}, // 0xcf //0a//[1]awn_en
	{GC0310_8BIT, 0x44, 0x18}, // 0x18 //02
	{GC0310_8BIT, 0x46, 0x02}, // 0x03 //sync
	{GC0310_8BIT, 0x49, 0x03},
	{GC0310_8BIT, 0x4c, 0x20}, //00[5]pretect exp
	{GC0310_8BIT, 0x50, 0x01}, //crop enable
	{GC0310_8BIT, 0x51, 0x00},
	{GC0310_8BIT, 0x52, 0x00},
	{GC0310_8BIT, 0x53, 0x00},
	{GC0310_8BIT, 0x54, 0x01},
	{GC0310_8BIT, 0x55, 0x01}, //crop window height
	{GC0310_8BIT, 0x56, 0xf0},
	{GC0310_8BIT, 0x57, 0x02}, //crop window width
	{GC0310_8BIT, 0x58, 0x90},

/////////////////////////////////////////////////
///////////////////   GAIN	 ////////////////////
/////////////////////////////////////////////////
	{GC0310_8BIT, 0x70, 0x70}, //70 //80//global gain
	{GC0310_8BIT, 0x71, 0x20}, // pregain gain
	{GC0310_8BIT, 0x72, 0x40}, // post gain
	{GC0310_8BIT, 0x5a, 0x84}, //84//analog gain 0
	{GC0310_8BIT, 0x5b, 0xc9}, //c9
	{GC0310_8BIT, 0x5c, 0xed}, //ed//not use pga gain highest level
	{GC0310_8BIT, 0x77, 0x40}, // R gain 0x74 //awb gain
	{GC0310_8BIT, 0x78, 0x40}, // G gain
	{GC0310_8BIT, 0x79, 0x40}, // B gain 0x5f

	{GC0310_8BIT, 0x48, 0x00},
	{GC0310_8BIT, 0xfe, 0x01},
	{GC0310_8BIT, 0x0a, 0x45}, //[7]col gain mode

	{GC0310_8BIT, 0x3e, 0x40},
	{GC0310_8BIT, 0x3f, 0x5c},
	{GC0310_8BIT, 0x40, 0x7b},
	{GC0310_8BIT, 0x41, 0xbd},
	{GC0310_8BIT, 0x42, 0xf6},
	{GC0310_8BIT, 0x43, 0x63},
	{GC0310_8BIT, 0x03, 0x60},
	{GC0310_8BIT, 0x44, 0x03},

/////////////////////////////////////////////////
/////////////////	dark sun   //////////////////
/////////////////////////////////////////////////
	{GC0310_8BIT, 0xfe, 0x01},
	{GC0310_8BIT, 0x45, 0xa4}, // 0xf7
	{GC0310_8BIT, 0x46, 0xf0}, // 0xff //f0//sun vaule th
	{GC0310_8BIT, 0x48, 0x03}, //sun mode
	{GC0310_8BIT, 0x4f, 0x60}, //sun_clamp
	{GC0310_8BIT, 0xfe, 0x00},

	{GC0310_TOK_TERM, 0, 0},
};

static struct gc0310_reg const gc0310_VGA_30fps[] = {
	{GC0310_8BIT, 0xfe, 0x00},
	{GC0310_8BIT, 0x0d, 0x01}, //height
	{GC0310_8BIT, 0x0e, 0xf2}, // 0xf7 //height
	{GC0310_8BIT, 0x0f, 0x02}, //width
	{GC0310_8BIT, 0x10, 0x94}, // 0xa0 //height

	{GC0310_8BIT, 0x50, 0x01}, //crop enable
	{GC0310_8BIT, 0x51, 0x00},
	{GC0310_8BIT, 0x52, 0x00},
	{GC0310_8BIT, 0x53, 0x00},
	{GC0310_8BIT, 0x54, 0x01},
	{GC0310_8BIT, 0x55, 0x01}, //crop window height
	{GC0310_8BIT, 0x56, 0xf0},
	{GC0310_8BIT, 0x57, 0x02}, //crop window width
	{GC0310_8BIT, 0x58, 0x90},

	{GC0310_8BIT, 0xfe, 0x03},
	{GC0310_8BIT, 0x12, 0x90},//00 //04 //00 //04//00 //LWC[7:0]  //
	{GC0310_8BIT, 0x13, 0x02},//05 //05 //LWC[15:8]

	{GC0310_8BIT, 0xfe, 0x00},

	{GC0310_TOK_TERM, 0, 0},
};

static struct gc0310_resolution gc0310_res_preview[] = {
	{
		.desc = "gc0310_VGA_30fps",
		.width = 656, // 648,
		.height = 496, // 488,
		.fps = 30,
		//.pix_clk_freq = 73,
		.used = 0,
#if 0
		.pixels_per_line = 0x0314,
		.lines_per_frame = 0x0213,
#endif
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.bin_mode = 0,
		.skip_frames = 2,
		.regs = gc0310_VGA_30fps,
	},
};
#define N_RES_PREVIEW (ARRAY_SIZE(gc0310_res_preview))

static struct gc0310_resolution *gc0310_res = gc0310_res_preview;
static unsigned long N_RES = N_RES_PREVIEW;
#endif

