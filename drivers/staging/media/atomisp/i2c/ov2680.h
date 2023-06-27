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
#include <media/v4l2-cci.h>
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

#define OV2680_SKIP_FRAMES			3

/* If possible send 16 extra rows / lines to the ISP as padding */
#define OV2680_END_MARGIN			16

/*
 * ov2680 device structure.
 */
struct ov2680_dev {
	struct v4l2_subdev sd;
	struct media_pad pad;
	/* Protect against concurrent changes to controls */
	struct mutex lock;
	struct device *dev;
	struct regmap *regmap;
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

#define to_ov2680_sensor(x) container_of(x, struct ov2680_dev, sd)

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	struct ov2680_dev *sensor =
		container_of(ctrl->handler, struct ov2680_dev, ctrls.handler);

	return &sensor->sd;
}

static const struct reg_sequence ov2680_global_setting[] = {
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
};

#endif
