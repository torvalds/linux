/* SPDX-License-Identifier: GPL-2.0 */
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

#define GC0310_NATIVE_WIDTH			656
#define GC0310_NATIVE_HEIGHT			496

#define GC0310_FPS				30
#define GC0310_SKIP_FRAMES			3

#define GC0310_FOCAL_LENGTH_NUM			278 /* 2.78mm */

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

/*
 * gc0310 device structure.
 */
struct gc0310_device {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct mutex input_lock;
	bool is_streaming;

	struct gpio_desc *reset;
	struct gpio_desc *powerdown;

	struct gc0310_mode {
		struct v4l2_mbus_framefmt fmt;
	} mode;

	struct gc0310_ctrls {
		struct v4l2_ctrl_handler handler;
		struct v4l2_ctrl *exposure;
		struct v4l2_ctrl *gain;
	} ctrls;
};

/**
 * struct gc0310_reg - MI sensor  register format
 * @reg: 16-bit offset to register
 * @val: 8/16/32-bit register value
 *
 * Define a structure for sensor register initialization values
 */
struct gc0310_reg {
	u8 reg;
	u8 val;	/* @set value for read/mod/write, @mask */
};

#define to_gc0310_sensor(x) container_of(x, struct gc0310_device, sd)

/*
 * Register settings for various resolution
 */
static const struct gc0310_reg gc0310_reset_register[] = {
/////////////////////////////////////////////////
/////////////////	system reg	/////////////////
/////////////////////////////////////////////////
	{ 0xfe, 0xf0 },
	{ 0xfe, 0xf0 },
	{ 0xfe, 0x00 },

	{ 0xfc, 0x0e }, /* 4e */
	{ 0xfc, 0x0e }, /* 16//4e // [0]apwd [6]regf_clk_gate */
	{ 0xf2, 0x80 }, /* sync output */
	{ 0xf3, 0x00 }, /* 1f//01 data output */
	{ 0xf7, 0x33 }, /* f9 */
	{ 0xf8, 0x05 }, /* 00 */
	{ 0xf9, 0x0e }, /* 0x8e //0f */
	{ 0xfa, 0x11 },

/////////////////////////////////////////////////
///////////////////   MIPI	 ////////////////////
/////////////////////////////////////////////////
	{ 0xfe, 0x03 },
	{ 0x01, 0x03 }, /* mipi 1lane */
	{ 0x02, 0x22 }, /* 0x33 */
	{ 0x03, 0x94 },
	{ 0x04, 0x01 }, /* fifo_prog */
	{ 0x05, 0x00 }, /* fifo_prog */
	{ 0x06, 0x80 }, /* b0  //YUV ISP data */
	{ 0x11, 0x2a }, /* 1e //LDI set YUV422 */
	{ 0x12, 0x90 }, /* 00 //04 //00 //04//00 //LWC[7:0] */
	{ 0x13, 0x02 }, /* 05 //05 //LWC[15:8] */
	{ 0x15, 0x12 }, /* 0x10 //DPHYY_MODE read_ready */
	{ 0x17, 0x01 },
	{ 0x40, 0x08 },
	{ 0x41, 0x00 },
	{ 0x42, 0x00 },
	{ 0x43, 0x00 },
	{ 0x21, 0x02 }, /* 0x01 */
	{ 0x22, 0x02 }, /* 0x01 */
	{ 0x23, 0x01 }, /* 0x05 //Nor:0x05 DOU:0x06 */
	{ 0x29, 0x00 },
	{ 0x2A, 0x25 }, /* 0x05 //data zero 0x7a de */
	{ 0x2B, 0x02 },

	{ 0xfe, 0x00 },

/////////////////////////////////////////////////
/////////////////	CISCTL reg	/////////////////
/////////////////////////////////////////////////
	{ 0x00, 0x2f }, /* 2f//0f//02//01 */
	{ 0x01, 0x0f }, /* 06 */
	{ 0x02, 0x04 },
	{ 0x4f, 0x00 }, /* AEC 0FF */
	{ 0x03, 0x01 }, /* 0x03 //04 */
	{ 0x04, 0xc0 }, /* 0xe8 //58 */
	{ 0x05, 0x00 },
	{ 0x06, 0xb2 }, /* 0x0a //HB */
	{ 0x07, 0x00 },
	{ 0x08, 0x0c }, /* 0x89 //VB */
	{ 0x09, 0x00 }, /* row start */
	{ 0x0a, 0x00 },
	{ 0x0b, 0x00 }, /* col start */
	{ 0x0c, 0x00 },
	{ 0x0d, 0x01 }, /* height */
	{ 0x0e, 0xf2 }, /* 0xf7 //height */
	{ 0x0f, 0x02 }, /* width */
	{ 0x10, 0x94 }, /* 0xa0 //height */
	{ 0x17, 0x14 },
	{ 0x18, 0x1a }, /* 0a//[4]double reset */
	{ 0x19, 0x14 }, /* AD pipeline */
	{ 0x1b, 0x48 },
	{ 0x1e, 0x6b }, /* 3b//col bias */
	{ 0x1f, 0x28 }, /* 20//00//08//txlow */
	{ 0x20, 0x89 }, /* 88//0c//[3:2]DA15 */
	{ 0x21, 0x49 }, /* 48//[3] txhigh */
	{ 0x22, 0xb0 },
	{ 0x23, 0x04 }, /* [1:0]vcm_r */
	{ 0x24, 0x16 }, /* 15 */
	{ 0x34, 0x20 }, /* [6:4] rsg high//range */

/////////////////////////////////////////////////
////////////////////   BLK	 ////////////////////
/////////////////////////////////////////////////
	{ 0x26, 0x23 }, /* [1]dark_current_en [0]offset_en */
	{ 0x28, 0xff }, /* BLK_limie_value */
	{ 0x29, 0x00 }, /* global offset */
	{ 0x33, 0x18 }, /* offset_ratio */
	{ 0x37, 0x20 }, /* dark_current_ratio */
	{ 0x2a, 0x00 },
	{ 0x2b, 0x00 },
	{ 0x2c, 0x00 },
	{ 0x2d, 0x00 },
	{ 0x2e, 0x00 },
	{ 0x2f, 0x00 },
	{ 0x30, 0x00 },
	{ 0x31, 0x00 },
	{ 0x47, 0x80 }, /* a7 */
	{ 0x4e, 0x66 }, /* select_row */
	{ 0xa8, 0x02 }, /* win_width_dark, same with crop_win_width */
	{ 0xa9, 0x80 },

/////////////////////////////////////////////////
//////////////////	 ISP reg  ///////////////////
/////////////////////////////////////////////////
	{ 0x40, 0x06 }, /* 0xff //ff //48 */
	{ 0x41, 0x00 }, /* 0x21 //00//[0]curve_en */
	{ 0x42, 0x04 }, /* 0xcf //0a//[1]awn_en */
	{ 0x44, 0x18 }, /* 0x18 //02 */
	{ 0x46, 0x02 }, /* 0x03 //sync */
	{ 0x49, 0x03 },
	{ 0x4c, 0x20 }, /* 00[5]pretect exp */
	{ 0x50, 0x01 }, /* crop enable */
	{ 0x51, 0x00 },
	{ 0x52, 0x00 },
	{ 0x53, 0x00 },
	{ 0x54, 0x01 },
	{ 0x55, 0x01 }, /* crop window height */
	{ 0x56, 0xf0 },
	{ 0x57, 0x02 }, /* crop window width */
	{ 0x58, 0x90 },

/////////////////////////////////////////////////
///////////////////   GAIN	 ////////////////////
/////////////////////////////////////////////////
	{ 0x70, 0x70 }, /* 70 //80//global gain */
	{ 0x71, 0x20 }, /* pregain gain */
	{ 0x72, 0x40 }, /* post gain */
	{ 0x5a, 0x84 }, /* 84//analog gain 0  */
	{ 0x5b, 0xc9 }, /* c9 */
	{ 0x5c, 0xed }, /* ed//not use pga gain highest level */
	{ 0x77, 0x40 }, /* R gain 0x74 //awb gain */
	{ 0x78, 0x40 }, /* G gain */
	{ 0x79, 0x40 }, /* B gain 0x5f */

	{ 0x48, 0x00 },
	{ 0xfe, 0x01 },
	{ 0x0a, 0x45 }, /* [7]col gain mode */

	{ 0x3e, 0x40 },
	{ 0x3f, 0x5c },
	{ 0x40, 0x7b },
	{ 0x41, 0xbd },
	{ 0x42, 0xf6 },
	{ 0x43, 0x63 },
	{ 0x03, 0x60 },
	{ 0x44, 0x03 },

/////////////////////////////////////////////////
/////////////////	dark sun   //////////////////
/////////////////////////////////////////////////
	{ 0xfe, 0x01 },
	{ 0x45, 0xa4 }, /* 0xf7 */
	{ 0x46, 0xf0 }, /* 0xff //f0//sun value th */
	{ 0x48, 0x03 }, /* sun mode */
	{ 0x4f, 0x60 }, /* sun_clamp */
	{ 0xfe, 0x00 },
};

static struct gc0310_reg const gc0310_VGA_30fps[] = {
	{ 0xfe, 0x00 },
	{ 0x0d, 0x01 }, /* height */
	{ 0x0e, 0xf2 }, /* 0xf7 //height */
	{ 0x0f, 0x02 }, /* width */
	{ 0x10, 0x94 }, /* 0xa0 //height */

	{ 0x50, 0x01 }, /* crop enable */
	{ 0x51, 0x00 },
	{ 0x52, 0x00 },
	{ 0x53, 0x00 },
	{ 0x54, 0x01 },
	{ 0x55, 0x01 }, /* crop window height */
	{ 0x56, 0xf0 },
	{ 0x57, 0x02 }, /* crop window width */
	{ 0x58, 0x90 },

	{ 0xfe, 0x03 },
	{ 0x12, 0x90 }, /* 00 //04 //00 //04//00 //LWC[7:0]  */
	{ 0x13, 0x02 }, /* 05 //05 //LWC[15:8] */

	{ 0xfe, 0x00 },
};

#endif
