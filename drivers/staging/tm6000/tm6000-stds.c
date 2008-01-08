/*
   tm6000-stds.c - driver for TM5600/TM6000 USB video capture devices

   Copyright (C) 2007 Mauro Carvalho Chehab <mchehab@redhat.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation version 2

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include "tm6000.h"
#include "tm6000-regs.h"

struct tm6000_reg_settings {
	unsigned char req;
	unsigned char reg;
	unsigned char value;
};

struct tm6000_std_tv_settings {
	v4l2_std_id id;
	struct tm6000_reg_settings sif[12];
	struct tm6000_reg_settings nosif[12];
	struct tm6000_reg_settings common[25];
};

struct tm6000_std_settings {
	v4l2_std_id id;
	struct tm6000_reg_settings common[37];
};

static struct tm6000_std_tv_settings tv_stds[] = {
	{
		.id = V4L2_STD_PAL_M,
		.sif = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf2},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x08},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x62},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfe},
			{REQ_07_SET_GET_AVREG, 0xfe, 0xcb},
			{0, 0, 0},
		},
		.nosif = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x0f},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x60},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8b},
			{0, 0, 0},
		},
		.common = {
			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x04},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0e},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x00},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x1e},
			{REQ_07_SET_GET_AVREG, 0x19, 0x83},
			{REQ_07_SET_GET_AVREG, 0x1a, 0x0a},
			{REQ_07_SET_GET_AVREG, 0x1b, 0xe0},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x88},
			{REQ_07_SET_GET_AVREG, 0x30, 0x20},
			{REQ_07_SET_GET_AVREG, 0x31, 0x61},
			{REQ_07_SET_GET_AVREG, 0x33, 0x0c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x82, 0x52},
			{REQ_07_SET_GET_AVREG, 0x83, 0x6F},

			{REQ_07_SET_GET_AVREG, 0x04, 0xdc},
			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	}, {
		.id = V4L2_STD_PAL_Nc,
		.sif = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf2},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x08},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x62},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfe},
			{REQ_07_SET_GET_AVREG, 0xfe, 0xcb},
			{0, 0, 0},
		},
		.nosif = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x0f},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x60},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8b},
			{0, 0, 0},
		},
		.common = {
			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x36},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0e},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x02},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x1e},
			{REQ_07_SET_GET_AVREG, 0x19, 0x91},
			{REQ_07_SET_GET_AVREG, 0x1a, 0x1f},
			{REQ_07_SET_GET_AVREG, 0x1b, 0x0c},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x8c},
			{REQ_07_SET_GET_AVREG, 0x30, 0x2c},
			{REQ_07_SET_GET_AVREG, 0x31, 0xc1},
			{REQ_07_SET_GET_AVREG, 0x33, 0x0c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x82, 0x52},
			{REQ_07_SET_GET_AVREG, 0x83, 0x6F},

			{REQ_07_SET_GET_AVREG, 0x04, 0xdc},
			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	}, {
		.id = V4L2_STD_PAL,
		.sif = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf2},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x08},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x62},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfe},
			{REQ_07_SET_GET_AVREG, 0xfe, 0xcb},
			{0, 0, 0}
		},
		.nosif = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x0f},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x60},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8b},
			{0, 0, 0},
		},
		.common = {
			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x32},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0e},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x02},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x25},
			{REQ_07_SET_GET_AVREG, 0x19, 0xd5},
			{REQ_07_SET_GET_AVREG, 0x1a, 0x63},
			{REQ_07_SET_GET_AVREG, 0x1b, 0x50},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x8c},
			{REQ_07_SET_GET_AVREG, 0x30, 0x2c},
			{REQ_07_SET_GET_AVREG, 0x31, 0xc1},
			{REQ_07_SET_GET_AVREG, 0x33, 0x0c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x82, 0x52},
			{REQ_07_SET_GET_AVREG, 0x83, 0x6F},

			{REQ_07_SET_GET_AVREG, 0x04, 0xdc},
			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	}, {
		.id = V4L2_STD_SECAM,
		.sif = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf2},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x08},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x62},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfe},
			{REQ_07_SET_GET_AVREG, 0xfe, 0xcb},
			{0, 0, 0},
		},
		.nosif = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x0f},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x60},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8b},
			{0, 0, 0},
		},
		.common = {
			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x38},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0e},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x02},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x24},
			{REQ_07_SET_GET_AVREG, 0x19, 0x92},
			{REQ_07_SET_GET_AVREG, 0x1a, 0xe8},
			{REQ_07_SET_GET_AVREG, 0x1b, 0xed},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x8c},
			{REQ_07_SET_GET_AVREG, 0x30, 0x2c},
			{REQ_07_SET_GET_AVREG, 0x31, 0xc1},
			{REQ_07_SET_GET_AVREG, 0x33, 0x2c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x18},
			{REQ_07_SET_GET_AVREG, 0x82, 0x42},
			{REQ_07_SET_GET_AVREG, 0x83, 0xFF},

			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	}, {
		.id = V4L2_STD_NTSC,
		.sif = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf2},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x08},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x62},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfe},
			{REQ_07_SET_GET_AVREG, 0xfe, 0xcb},
			{0, 0, 0},
		},
		.nosif = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x0f},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x60},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8b},
			{0, 0, 0},
		},
		.common = {
			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x00},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0f},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x00},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x1e},
			{REQ_07_SET_GET_AVREG, 0x19, 0x8b},
			{REQ_07_SET_GET_AVREG, 0x1a, 0xa2},
			{REQ_07_SET_GET_AVREG, 0x1b, 0xe9},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x88},
			{REQ_07_SET_GET_AVREG, 0x30, 0x22},
			{REQ_07_SET_GET_AVREG, 0x31, 0x61},
			{REQ_07_SET_GET_AVREG, 0x33, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x82, 0x42},
			{REQ_07_SET_GET_AVREG, 0x83, 0x6F},

			{REQ_07_SET_GET_AVREG, 0x04, 0xdd},
			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	},
};

static struct tm6000_std_settings composite_stds[] = {
	{
		.id = V4L2_STD_PAL_M,
		.common = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf4},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x0f},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x68},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8b},

			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x04},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0e},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x00},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x1e},
			{REQ_07_SET_GET_AVREG, 0x19, 0x83},
			{REQ_07_SET_GET_AVREG, 0x1a, 0x0a},
			{REQ_07_SET_GET_AVREG, 0x1b, 0xe0},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x88},
			{REQ_07_SET_GET_AVREG, 0x30, 0x20},
			{REQ_07_SET_GET_AVREG, 0x31, 0x61},
			{REQ_07_SET_GET_AVREG, 0x33, 0x0c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x82, 0x52},
			{REQ_07_SET_GET_AVREG, 0x83, 0x6F},

			{REQ_07_SET_GET_AVREG, 0x04, 0xdc},
			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	 }, {
		.id = V4L2_STD_PAL_Nc,
		.common = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf4},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x0f},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x68},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8b},

			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x36},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0e},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x02},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x1e},
			{REQ_07_SET_GET_AVREG, 0x19, 0x91},
			{REQ_07_SET_GET_AVREG, 0x1a, 0x1f},
			{REQ_07_SET_GET_AVREG, 0x1b, 0x0c},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x8c},
			{REQ_07_SET_GET_AVREG, 0x30, 0x2c},
			{REQ_07_SET_GET_AVREG, 0x31, 0xc1},
			{REQ_07_SET_GET_AVREG, 0x33, 0x0c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x82, 0x52},
			{REQ_07_SET_GET_AVREG, 0x83, 0x6F},

			{REQ_07_SET_GET_AVREG, 0x04, 0xdc},
			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	}, {
		.id = V4L2_STD_PAL,
		.common = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf4},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x0f},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x68},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8b},

			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x32},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0e},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x02},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x25},
			{REQ_07_SET_GET_AVREG, 0x19, 0xd5},
			{REQ_07_SET_GET_AVREG, 0x1a, 0x63},
			{REQ_07_SET_GET_AVREG, 0x1b, 0x50},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x8c},
			{REQ_07_SET_GET_AVREG, 0x30, 0x2c},
			{REQ_07_SET_GET_AVREG, 0x31, 0xc1},
			{REQ_07_SET_GET_AVREG, 0x33, 0x0c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x82, 0x52},
			{REQ_07_SET_GET_AVREG, 0x83, 0x6F},

			{REQ_07_SET_GET_AVREG, 0x04, 0xdc},
			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	 }, {
		.id = V4L2_STD_SECAM,
		.common = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf4},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x0f},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x68},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8b},

			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x38},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0e},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x02},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x24},
			{REQ_07_SET_GET_AVREG, 0x19, 0x92},
			{REQ_07_SET_GET_AVREG, 0x1a, 0xe8},
			{REQ_07_SET_GET_AVREG, 0x1b, 0xed},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x8c},
			{REQ_07_SET_GET_AVREG, 0x30, 0x2c},
			{REQ_07_SET_GET_AVREG, 0x31, 0xc1},
			{REQ_07_SET_GET_AVREG, 0x33, 0x2c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x18},
			{REQ_07_SET_GET_AVREG, 0x82, 0x42},
			{REQ_07_SET_GET_AVREG, 0x83, 0xFF},

			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	}, {
		.id = V4L2_STD_NTSC,
		.common = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf4},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf3},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x0f},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf1},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe8},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x68},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8b},

			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x00},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0f},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x00},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x1e},
			{REQ_07_SET_GET_AVREG, 0x19, 0x8b},
			{REQ_07_SET_GET_AVREG, 0x1a, 0xa2},
			{REQ_07_SET_GET_AVREG, 0x1b, 0xe9},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x88},
			{REQ_07_SET_GET_AVREG, 0x30, 0x22},
			{REQ_07_SET_GET_AVREG, 0x31, 0x61},
			{REQ_07_SET_GET_AVREG, 0x33, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x82, 0x42},
			{REQ_07_SET_GET_AVREG, 0x83, 0x6F},

			{REQ_07_SET_GET_AVREG, 0x04, 0xdd},
			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	},
};

static struct tm6000_std_settings svideo_stds[] = {
	{
		.id = V4L2_STD_PAL_M,
		.common = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xfc},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x00},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf2},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x68},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8a},

			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x05},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0e},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x04},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x1e},
			{REQ_07_SET_GET_AVREG, 0x19, 0x83},
			{REQ_07_SET_GET_AVREG, 0x1a, 0x0a},
			{REQ_07_SET_GET_AVREG, 0x1b, 0xe0},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x88},
			{REQ_07_SET_GET_AVREG, 0x30, 0x22},
			{REQ_07_SET_GET_AVREG, 0x31, 0x61},
			{REQ_07_SET_GET_AVREG, 0x33, 0x0c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x82, 0x52},
			{REQ_07_SET_GET_AVREG, 0x83, 0x6F},

			{REQ_07_SET_GET_AVREG, 0x04, 0xdc},
			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	}, {
		.id = V4L2_STD_PAL_Nc,
		.common = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xfc},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x00},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf2},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x68},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8a},

			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x37},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0e},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x04},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x1e},
			{REQ_07_SET_GET_AVREG, 0x19, 0x91},
			{REQ_07_SET_GET_AVREG, 0x1a, 0x1f},
			{REQ_07_SET_GET_AVREG, 0x1b, 0x0c},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x88},
			{REQ_07_SET_GET_AVREG, 0x30, 0x22},
			{REQ_07_SET_GET_AVREG, 0x31, 0xc1},
			{REQ_07_SET_GET_AVREG, 0x33, 0x0c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x82, 0x52},
			{REQ_07_SET_GET_AVREG, 0x83, 0x6F},

			{REQ_07_SET_GET_AVREG, 0x04, 0xdc},
			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	}, {
		.id = V4L2_STD_PAL,
		.common = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xfc},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x00},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf2},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x68},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8a},

			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x33},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0e},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x04},
			{REQ_07_SET_GET_AVREG, 0x07, 0x00},
			{REQ_07_SET_GET_AVREG, 0x18, 0x25},
			{REQ_07_SET_GET_AVREG, 0x19, 0xd5},
			{REQ_07_SET_GET_AVREG, 0x1a, 0x63},
			{REQ_07_SET_GET_AVREG, 0x1b, 0x50},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x8c},
			{REQ_07_SET_GET_AVREG, 0x30, 0x2a},
			{REQ_07_SET_GET_AVREG, 0x31, 0xc1},
			{REQ_07_SET_GET_AVREG, 0x33, 0x0c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x82, 0x52},
			{REQ_07_SET_GET_AVREG, 0x83, 0x6F},

			{REQ_07_SET_GET_AVREG, 0x04, 0xdc},
			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	 }, {
		.id = V4L2_STD_SECAM,
		.common = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xfc},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x00},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf2},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x68},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8a},

			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x39},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0e},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x03},
			{REQ_07_SET_GET_AVREG, 0x07, 0x01},
			{REQ_07_SET_GET_AVREG, 0x18, 0x24},
			{REQ_07_SET_GET_AVREG, 0x19, 0x92},
			{REQ_07_SET_GET_AVREG, 0x1a, 0xe8},
			{REQ_07_SET_GET_AVREG, 0x1b, 0xed},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x8c},
			{REQ_07_SET_GET_AVREG, 0x30, 0x2a},
			{REQ_07_SET_GET_AVREG, 0x31, 0xc1},
			{REQ_07_SET_GET_AVREG, 0x33, 0x2c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x18},
			{REQ_07_SET_GET_AVREG, 0x82, 0x42},
			{REQ_07_SET_GET_AVREG, 0x83, 0xFF},

			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	}, {
		.id = V4L2_STD_NTSC,
		.common = {
			{REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xfc},
			{REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf8},
			{REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x00},
			{REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf2},
			{REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xf0},
			{REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2},
			{REQ_08_SET_GET_AVREG_BIT, 0xed, 0xe0},
			{REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x68},
			{REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc},
			{REQ_07_SET_GET_AVREG, 0xfe, 0x8a},

			{REQ_07_SET_GET_AVREG, 0x3f, 0x01},
			{REQ_07_SET_GET_AVREG, 0x00, 0x01},
			{REQ_07_SET_GET_AVREG, 0x01, 0x0f},
			{REQ_07_SET_GET_AVREG, 0x02, 0x5f},
			{REQ_07_SET_GET_AVREG, 0x03, 0x03},
			{REQ_07_SET_GET_AVREG, 0x07, 0x00},
			{REQ_07_SET_GET_AVREG, 0x17, 0x8b},
			{REQ_07_SET_GET_AVREG, 0x18, 0x1e},
			{REQ_07_SET_GET_AVREG, 0x19, 0x8b},
			{REQ_07_SET_GET_AVREG, 0x1a, 0xa2},
			{REQ_07_SET_GET_AVREG, 0x1b, 0xe9},
			{REQ_07_SET_GET_AVREG, 0x1c, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x1d, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1e, 0xcc},
			{REQ_07_SET_GET_AVREG, 0x1f, 0xcd},
			{REQ_07_SET_GET_AVREG, 0x2e, 0x88},
			{REQ_07_SET_GET_AVREG, 0x30, 0x22},
			{REQ_07_SET_GET_AVREG, 0x31, 0x61},
			{REQ_07_SET_GET_AVREG, 0x33, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x35, 0x1c},
			{REQ_07_SET_GET_AVREG, 0x82, 0x42},
			{REQ_07_SET_GET_AVREG, 0x83, 0x6F},

			{REQ_07_SET_GET_AVREG, 0x04, 0xdd},
			{REQ_07_SET_GET_AVREG, 0x0d, 0x07},
			{REQ_07_SET_GET_AVREG, 0x3f, 0x00},
			{0, 0, 0},
		},
	},
};

void tm6000_get_std_res(struct tm6000_core *dev)
{
	/* Currently, those are the only supported resoltions */
	if (dev->norm & V4L2_STD_525_60) {
		dev->height = 480;
	} else {
		dev->height = 576;
	}
	dev->width = 720;
}

static int tm6000_load_std(struct tm6000_core *dev,
			   struct tm6000_reg_settings *set, int max_size)
{
	int i, rc;

	/* Load board's initialization table */
	for (i = 0; max_size; i++) {
		if (!set[i].req)
			return 0;

		if ((dev->dev_type != TM6010) &&
		    (set[i].req == REQ_08_SET_GET_AVREG_BIT))
				continue;

		rc = tm6000_set_reg(dev, set[i].req, set[i].reg, set[i].value);
		if (rc < 0) {
			printk(KERN_ERR "Error %i while setting "
			       "req %d, reg %d to value %d\n",
			       rc, set[i].req, set[i].reg, set[i].value);
			return rc;
		}
	}

	return 0;
}

static int tm6000_set_tv(struct tm6000_core *dev, int pos)
{
	int rc;

	/* FIXME: This code is for tm6010 - not tested yet - doesn't work with
	   tm5600
	 */

	/* FIXME: This is tuner-dependent */
	int nosif = 0;

	if (nosif) {
		rc = tm6000_load_std(dev, tv_stds[pos].nosif,
				     sizeof(tv_stds[pos].nosif));
	} else {
		rc = tm6000_load_std(dev, tv_stds[pos].sif,
				     sizeof(tv_stds[pos].sif));
	}
	if (rc < 0)
		return rc;
	rc = tm6000_load_std(dev, tv_stds[pos].common,
			     sizeof(tv_stds[pos].common));

	return rc;
}

int tm6000_set_standard(struct tm6000_core *dev, v4l2_std_id * norm)
{
	int i, rc = 0;

	dev->norm = *norm;
	tm6000_get_std_res(dev);

	switch (dev->input) {
	case TM6000_INPUT_TV:
		for (i = 0; i < ARRAY_SIZE(tv_stds); i++) {
			if (*norm & tv_stds[i].id) {
				rc = tm6000_set_tv(dev, i);
				goto ret;
			}
		}
		return -EINVAL;
	case TM6000_INPUT_SVIDEO:
		for (i = 0; i < ARRAY_SIZE(svideo_stds); i++) {
			if (*norm & svideo_stds[i].id) {
				rc = tm6000_load_std(dev, svideo_stds[i].common,
						     sizeof(svideo_stds[i].
							    common));
				goto ret;
			}
		}
		return -EINVAL;
	case TM6000_INPUT_COMPOSITE:
		for (i = 0; i < ARRAY_SIZE(composite_stds); i++) {
			if (*norm & composite_stds[i].id) {
				rc = tm6000_load_std(dev,
						     composite_stds[i].common,
						     sizeof(composite_stds[i].
							    common));
				goto ret;
			}
		}
		return -EINVAL;
	}

ret:
	if (rc < 0)
		return rc;

	msleep(40);


	return 0;
}
