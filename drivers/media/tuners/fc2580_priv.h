/*
 * FCI FC2580 silicon tuner driver
 *
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef FC2580_PRIV_H
#define FC2580_PRIV_H

#include "fc2580.h"
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/regmap.h>
#include <linux/math64.h>

struct fc2580_reg_val {
	u8 reg;
	u8 val;
};

static const struct fc2580_reg_val fc2580_init_reg_vals[] = {
	{0x00, 0x00},
	{0x12, 0x86},
	{0x14, 0x5c},
	{0x16, 0x3c},
	{0x1f, 0xd2},
	{0x09, 0xd7},
	{0x0b, 0xd5},
	{0x0c, 0x32},
	{0x0e, 0x43},
	{0x21, 0x0a},
	{0x22, 0x82},
	{0x45, 0x10},
	{0x4c, 0x00},
	{0x3f, 0x88},
	{0x02, 0x0e},
	{0x58, 0x14},
};

struct fc2580_pll {
	u32 freq;
	u8 div_out;
	u8 band;
};

static const struct fc2580_pll fc2580_pll_lut[] = {
	/*                            VCO min    VCO max */
	{ 400000000, 12, 0x80}, /* .......... 4800000000 */
	{1000000000,  4, 0x00}, /* 1600000000 4000000000 */
	{0xffffffff,  2, 0x40}, /* 2000000000 .......... */
};

struct fc2580_if_filter {
	u32 freq;
	u8 r36_val;
	u8 r39_val;
};

static const struct fc2580_if_filter fc2580_if_filter_lut[] = {
	{   6000000, 0x18, 0x00},
	{   7000000, 0x18, 0x80},
	{   8000000, 0x18, 0x80},
	{0xffffffff, 0x18, 0x80},
};

struct fc2580_freq_regs {
	u32 freq;
	u8 r25_val;
	u8 r27_val;
	u8 r28_val;
	u8 r29_val;
	u8 r2b_val;
	u8 r2c_val;
	u8 r2d_val;
	u8 r30_val;
	u8 r44_val;
	u8 r50_val;
	u8 r53_val;
	u8 r5f_val;
	u8 r61_val;
	u8 r62_val;
	u8 r63_val;
	u8 r67_val;
	u8 r68_val;
	u8 r69_val;
	u8 r6a_val;
	u8 r6b_val;
	u8 r6c_val;
	u8 r6d_val;
	u8 r6e_val;
	u8 r6f_val;
};

/* XXX: 0xff is used for don't-care! */
static const struct fc2580_freq_regs fc2580_freq_regs_lut[] = {
	{ 400000000,
		0xff, 0x77, 0x33, 0x40, 0xff, 0xff, 0xff, 0x09, 0xff, 0x8c,
		0x50, 0x0f, 0x07, 0x00, 0x15, 0x03, 0x05, 0x10, 0x12, 0x08,
		0x0a, 0x78, 0x32, 0x54},
	{ 538000000,
		0xf0, 0x77, 0x53, 0x60, 0xff, 0xff, 0x9f, 0x09, 0xff, 0x8c,
		0x50, 0x13, 0x07, 0x06, 0x15, 0x06, 0x08, 0x10, 0x12, 0x0b,
		0x0c, 0x78, 0x32, 0x14},
	{ 794000000,
		0xf0, 0x77, 0x53, 0x60, 0xff, 0xff, 0x9f, 0x09, 0xff, 0x8c,
		0x50, 0x15, 0x03, 0x03, 0x15, 0x03, 0x05, 0x0c, 0x0e, 0x0b,
		0x0c, 0x78, 0x32, 0x14},
	{1000000000,
		0xf0, 0x77, 0x53, 0x60, 0xff, 0xff, 0x8f, 0x09, 0xff, 0x8c,
		0x50, 0x15, 0x07, 0x06, 0x15, 0x07, 0x09, 0x10, 0x12, 0x0b,
		0x0c, 0x78, 0x32, 0x14},
	{0xffffffff,
		0xff, 0xff, 0xff, 0xff, 0x70, 0x37, 0xe7, 0x09, 0x20, 0x8c,
		0x50, 0x0f, 0x0f, 0x00, 0x13, 0x00, 0x02, 0x0c, 0x0e, 0x08,
		0x0a, 0xa0, 0x50, 0x14},
};

struct fc2580_dev {
	u32 clk;
	struct i2c_client *client;
	struct regmap *regmap;
	struct v4l2_subdev subdev;
	bool active;
	unsigned int f_frequency;
	unsigned int f_bandwidth;

	/* Controls */
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *bandwidth_auto;
	struct v4l2_ctrl *bandwidth;
};

#endif
