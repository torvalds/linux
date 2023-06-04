/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for OmniVision OV2680 5M camera sensor.
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

#ifndef __OV2680_H__
#define __OV2680_H__
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

#define OV2680_NATIVE_WIDTH			1616
#define OV2680_NATIVE_HEIGHT			1216
#define OV2680_NATIVE_START_LEFT		0
#define OV2680_NATIVE_START_TOP			0
#define OV2680_ACTIVE_WIDTH			1600
#define OV2680_ACTIVE_HEIGHT			1200
#define OV2680_ACTIVE_START_LEFT		8
#define OV2680_ACTIVE_START_TOP			8
#define OV2680_MIN_CROP_WIDTH			2
#define OV2680_MIN_CROP_HEIGHT			2

/* 1704 * 1294 * 30fps = 66MHz pixel clock */
#define OV2680_PIXELS_PER_LINE			1704
#define OV2680_LINES_PER_FRAME			1294
#define OV2680_FPS				30
#define OV2680_SKIP_FRAMES			3

/* If possible send 16 extra rows / lines to the ISP as padding */
#define OV2680_END_MARGIN			16

#define OV2680_FOCAL_LENGTH_NUM			334	/*3.34mm*/

#define OV2680_INTEGRATION_TIME_MARGIN		8
#define OV2680_ID				0x2680

/*
 * OV2680 System control registers
 */
#define OV2680_SW_SLEEP				0x0100
#define OV2680_SW_RESET				0x0103
#define OV2680_SW_STREAM			0x0100

#define OV2680_SC_CMMN_CHIP_ID_H		0x300A
#define OV2680_SC_CMMN_CHIP_ID_L		0x300B
#define OV2680_SC_CMMN_SCCB_ID			0x302B /* 0x300C*/
#define OV2680_SC_CMMN_SUB_ID			0x302A /* process, version*/

#define OV2680_GROUP_ACCESS			0x3208 /*Bit[7:4] Group control, Bit[3:0] Group ID*/

#define OV2680_REG_EXPOSURE_PK_HIGH		0x3500
#define OV2680_REG_GAIN_PK			0x350a

#define OV2680_REG_SENSOR_CTRL_0A		0x370a

#define OV2680_HORIZONTAL_START_H		0x3800 /* Bit[11:8] */
#define OV2680_HORIZONTAL_START_L		0x3801 /* Bit[7:0]  */
#define OV2680_VERTICAL_START_H			0x3802 /* Bit[11:8] */
#define OV2680_VERTICAL_START_L			0x3803 /* Bit[7:0]  */
#define OV2680_HORIZONTAL_END_H			0x3804 /* Bit[11:8] */
#define OV2680_HORIZONTAL_END_L			0x3805 /* Bit[7:0]  */
#define OV2680_VERTICAL_END_H			0x3806 /* Bit[11:8] */
#define OV2680_VERTICAL_END_L			0x3807 /* Bit[7:0]  */
#define OV2680_HORIZONTAL_OUTPUT_SIZE_H		0x3808 /* Bit[11:8] */
#define OV2680_HORIZONTAL_OUTPUT_SIZE_L		0x3809 /* Bit[7:0]  */
#define OV2680_VERTICAL_OUTPUT_SIZE_H		0x380a /* Bit[11:8] */
#define OV2680_VERTICAL_OUTPUT_SIZE_L		0x380b /* Bit[7:0]  */
#define OV2680_HTS				0x380c
#define OV2680_VTS				0x380e
#define OV2680_ISP_X_WIN			0x3810
#define OV2680_ISP_Y_WIN			0x3812
#define OV2680_X_INC				0x3814
#define OV2680_Y_INC				0x3815

#define OV2680_FRAME_OFF_NUM			0x4202

/*Flip/Mirror*/
#define OV2680_REG_FORMAT1			0x3820
#define OV2680_REG_FORMAT2			0x3821

#define OV2680_MWB_RED_GAIN_H			0x5004/*0x3400*/
#define OV2680_MWB_GREEN_GAIN_H			0x5006/*0x3402*/
#define OV2680_MWB_BLUE_GAIN_H			0x5008/*0x3404*/
#define OV2680_MWB_GAIN_MAX			0x0fff

#define OV2680_REG_ISP_CTRL00			0x5080

#define OV2680_X_WIN				0x5704
#define OV2680_Y_WIN				0x5706
#define OV2680_WIN_CONTROL			0x5708

#define OV2680_START_STREAMING			0x01
#define OV2680_STOP_STREAMING			0x00

/*
 * ov2680 device structure.
 */
struct ov2680_dev {
	struct v4l2_subdev sd;
	struct media_pad pad;
	/* Protect against concurrent changes to controls */
	struct mutex lock;
	struct i2c_client *client;
	struct gpio_desc *powerdown;
	struct fwnode_handle *ep_fwnode;
	bool is_streaming;

	struct ov2680_mode {
		struct v4l2_rect crop;
		struct v4l2_mbus_framefmt fmt;
		bool binning;
		u16 h_start;
		u16 v_start;
		u16 h_end;
		u16 v_end;
		u16 h_output_size;
		u16 v_output_size;
		u16 hts;
		u16 vts;
	} mode;

	struct ov2680_ctrls {
		struct v4l2_ctrl_handler handler;
		struct v4l2_ctrl *hflip;
		struct v4l2_ctrl *vflip;
		struct v4l2_ctrl *exposure;
		struct v4l2_ctrl *gain;
		struct v4l2_ctrl *test_pattern;
	} ctrls;
};

/**
 * struct ov2680_reg - MI sensor  register format
 * @type: type of the register
 * @reg: 16-bit offset to register
 * @val: 8/16/32-bit register value
 *
 * Define a structure for sensor register initialization values
 */
struct ov2680_reg {
	u16 reg;
	u32 val;	/* @set value for read/mod/write, @mask */
};

#define to_ov2680_sensor(x) container_of(x, struct ov2680_dev, sd)

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	struct ov2680_dev *sensor =
		container_of(ctrl->handler, struct ov2680_dev, ctrls.handler);

	return &sensor->sd;
}

static struct ov2680_reg const ov2680_global_setting[] = {
	/* MIPI PHY, 0x10 -> 0x1c enable bp_c_hs_en_lat and bp_d_hs_en_lat */
	{0x3016, 0x1c},

	/* PLL MULT bits 0-7, datasheet default 0x37 for 24MHz extclk, use 0x45 for 19.2 Mhz extclk */
	{0x3082, 0x45},

	/* R MANUAL set exposure (0x01) and gain (0x02) to manual (hw does not do auto) */
	{0x3503, 0x03},

	/* Analog control register tweaks */
	{0x3603, 0x39}, /* Reset value 0x99 */
	{0x3604, 0x24}, /* Reset value 0x74 */
	{0x3621, 0x37}, /* Reset value 0x44 */

	/* Sensor control register tweaks */
	{0x3701, 0x64}, /* Reset value 0x61 */
	{0x3705, 0x3c}, /* Reset value 0x21 */
	{0x370c, 0x50}, /* Reset value 0x10 */
	{0x370d, 0xc0}, /* Reset value 0x00 */
	{0x3718, 0x88}, /* Reset value 0x80 */

	/* PSRAM tweaks */
	{0x3781, 0x80}, /* Reset value 0x00 */
	{0x3784, 0x0c}, /* Reset value 0x00, based on OV2680_R1A_AM10.ovt */
	{0x3789, 0x60}, /* Reset value 0x50 */

	/* BLC CTRL00 0x01 -> 0x81 set avg_weight to 8 */
	{0x4000, 0x81},

	/* Set black level compensation range to 0 - 3 (default 0 - 11) */
	{0x4008, 0x00},
	{0x4009, 0x03},

	/* VFIFO R2 0x00 -> 0x02 set Frame reset enable */
	{0x4602, 0x02},

	/* MIPI ctrl CLK PREPARE MIN change from 0x26 (38) -> 0x36 (54) */
	{0x481f, 0x36},

	/* MIPI ctrl CLK LPX P MIN change from 0x32 (50) -> 0x36 (54) */
	{0x4825, 0x36},

	/* R ISP CTRL2 0x20 -> 0x30, set sof_sel bit */
	{0x5002, 0x30},

	/*
	 * Window CONTROL 0x00 -> 0x01, enable manual window control,
	 * this is necessary for full size flip and mirror support.
	 */
	{0x5708, 0x01},

	/*
	 * DPC CTRL0 0x14 -> 0x3e, set enable_tail, enable_3x3_cluster
	 * and enable_general_tail bits based OV2680_R1A_AM10.ovt.
	 */
	{0x5780, 0x3e},

	/* DPC MORE CONNECTION CASE THRE 0x0c (12) -> 0x02 (2) */
	{0x5788, 0x02},

	/* DPC GAIN LIST1 0x0f (15) -> 0x08 (8) */
	{0x578e, 0x08},

	/* DPC GAIN LIST2 0x3f (63) -> 0x0c (12) */
	{0x578f, 0x0c},

	/* DPC THRE RATIO 0x04 (4) -> 0x00 (0) */
	{0x5792, 0x00},

	{}
};

#endif
