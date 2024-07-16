/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Elonics E4000 silicon tuner driver
 *
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
 */

#ifndef E4000_PRIV_H
#define E4000_PRIV_H

#include "e4000.h"
#include <linux/math64.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/regmap.h>

struct e4000_dev {
	struct i2c_client *client;
	struct regmap *regmap;
	u32 clk;
	struct dvb_frontend *fe;
	struct v4l2_subdev sd;
	bool active;
	unsigned int f_frequency;
	unsigned int f_bandwidth;

	/* Controls */
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *bandwidth_auto;
	struct v4l2_ctrl *bandwidth;
	struct v4l2_ctrl *lna_gain_auto;
	struct v4l2_ctrl *lna_gain;
	struct v4l2_ctrl *mixer_gain_auto;
	struct v4l2_ctrl *mixer_gain;
	struct v4l2_ctrl *if_gain_auto;
	struct v4l2_ctrl *if_gain;
	struct v4l2_ctrl *pll_lock;
};

struct e4000_pll {
	u32 freq;
	u8 div_out_reg;
	u8 div_out;
};

static const struct e4000_pll e4000_pll_lut[] = {
/*                                      VCO min    VCO max */
	{   72400000, 0x0f, 48 }, /* .......... 3475200000 */
	{   81200000, 0x0e, 40 }, /* 2896000000 3248000000 */
	{  108300000, 0x0d, 32 }, /* 2598400000 3465600000 */
	{  162500000, 0x0c, 24 }, /* 2599200000 3900000000 */
	{  216600000, 0x0b, 16 }, /* 2600000000 3465600000 */
	{  325000000, 0x0a, 12 }, /* 2599200000 3900000000 */
	{  350000000, 0x09,  8 }, /* 2600000000 2800000000 */
	{  432000000, 0x03,  8 }, /* 2800000000 3456000000 */
	{  667000000, 0x02,  6 }, /* 2592000000 4002000000 */
	{ 1200000000, 0x01,  4 }, /* 2668000000 4800000000 */
	{ 0xffffffff, 0x00,  2 }, /* 2400000000 .......... */
};

struct e4000_lna_filter {
	u32 freq;
	u8 val;
};

static const struct e4000_lna_filter e400_lna_filter_lut[] = {
	{  370000000,  0 },
	{  392500000,  1 },
	{  415000000,  2 },
	{  437500000,  3 },
	{  462500000,  4 },
	{  490000000,  5 },
	{  522500000,  6 },
	{  557500000,  7 },
	{  595000000,  8 },
	{  642500000,  9 },
	{  695000000, 10 },
	{  740000000, 11 },
	{  800000000, 12 },
	{  865000000, 13 },
	{  930000000, 14 },
	{ 1000000000, 15 },
	{ 1310000000,  0 },
	{ 1340000000,  1 },
	{ 1385000000,  2 },
	{ 1427500000,  3 },
	{ 1452500000,  4 },
	{ 1475000000,  5 },
	{ 1510000000,  6 },
	{ 1545000000,  7 },
	{ 1575000000,  8 },
	{ 1615000000,  9 },
	{ 1650000000, 10 },
	{ 1670000000, 11 },
	{ 1690000000, 12 },
	{ 1710000000, 13 },
	{ 1735000000, 14 },
	{ 0xffffffff, 15 },
};

struct e4000_band {
	u32 freq;
	u8 reg07_val;
	u8 reg78_val;
};

static const struct e4000_band e4000_band_lut[] = {
	{  140000000, 0x01, 0x03 },
	{  350000000, 0x03, 0x03 },
	{ 1000000000, 0x05, 0x03 },
	{ 0xffffffff, 0x07, 0x00 },
};

struct e4000_if_filter {
	u32 freq;
	u8 reg11_val;
	u8 reg12_val;
};

static const struct e4000_if_filter e4000_if_filter_lut[] = {
	{    4300000, 0xfd, 0x1f },
	{    4400000, 0xfd, 0x1e },
	{    4480000, 0xfc, 0x1d },
	{    4560000, 0xfc, 0x1c },
	{    4600000, 0xfc, 0x1b },
	{    4800000, 0xfc, 0x1a },
	{    4900000, 0xfc, 0x19 },
	{    5000000, 0xfc, 0x18 },
	{    5100000, 0xfc, 0x17 },
	{    5200000, 0xfc, 0x16 },
	{    5400000, 0xfc, 0x15 },
	{    5500000, 0xfc, 0x14 },
	{    5600000, 0xfc, 0x13 },
	{    5800000, 0xfb, 0x12 },
	{    5900000, 0xfb, 0x11 },
	{    6000000, 0xfb, 0x10 },
	{    6200000, 0xfb, 0x0f },
	{    6400000, 0xfa, 0x0e },
	{    6600000, 0xfa, 0x0d },
	{    6800000, 0xf9, 0x0c },
	{    7200000, 0xf9, 0x0b },
	{    7400000, 0xf9, 0x0a },
	{    7600000, 0xf8, 0x09 },
	{    7800000, 0xf8, 0x08 },
	{    8200000, 0xf8, 0x07 },
	{    8600000, 0xf7, 0x06 },
	{    8800000, 0xf7, 0x05 },
	{    9200000, 0xf7, 0x04 },
	{    9600000, 0xf6, 0x03 },
	{   10000000, 0xf6, 0x02 },
	{   10600000, 0xf5, 0x01 },
	{   11000000, 0xf5, 0x00 },
	{ 0xffffffff, 0x00, 0x20 },
};

struct e4000_if_gain {
	u8 reg16_val;
	u8 reg17_val;
};

static const struct e4000_if_gain e4000_if_gain_lut[] = {
	{0x00, 0x00},
	{0x20, 0x00},
	{0x40, 0x00},
	{0x02, 0x00},
	{0x22, 0x00},
	{0x42, 0x00},
	{0x04, 0x00},
	{0x24, 0x00},
	{0x44, 0x00},
	{0x01, 0x00},
	{0x21, 0x00},
	{0x41, 0x00},
	{0x03, 0x00},
	{0x23, 0x00},
	{0x43, 0x00},
	{0x05, 0x00},
	{0x25, 0x00},
	{0x45, 0x00},
	{0x07, 0x00},
	{0x27, 0x00},
	{0x47, 0x00},
	{0x0f, 0x00},
	{0x2f, 0x00},
	{0x4f, 0x00},
	{0x17, 0x00},
	{0x37, 0x00},
	{0x57, 0x00},
	{0x1f, 0x00},
	{0x3f, 0x00},
	{0x5f, 0x00},
	{0x1f, 0x01},
	{0x3f, 0x01},
	{0x5f, 0x01},
	{0x1f, 0x02},
	{0x3f, 0x02},
	{0x5f, 0x02},
	{0x1f, 0x03},
	{0x3f, 0x03},
	{0x5f, 0x03},
	{0x1f, 0x04},
	{0x3f, 0x04},
	{0x5f, 0x04},
	{0x1f, 0x0c},
	{0x3f, 0x0c},
	{0x5f, 0x0c},
	{0x1f, 0x14},
	{0x3f, 0x14},
	{0x5f, 0x14},
	{0x1f, 0x1c},
	{0x3f, 0x1c},
	{0x5f, 0x1c},
	{0x1f, 0x24},
	{0x3f, 0x24},
	{0x5f, 0x24},
	{0x7f, 0x24},
};

#endif
