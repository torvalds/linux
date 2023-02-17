/* SPDX-License-Identifier: GPL-2.0 */
/*
 * techpoint tp9950 regs
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef _TECHPOINT_TP9950_H
#define _TECHPOINT_TP9950_H

#include "techpoint_common.h"

#define DEBUG

#define TP9950_DEF_1080P 0
#define TP9950_DEF_720P 1
#define TP9950_DEF_PAL 0
#define TP9950_DEF_NTSC 0

#define STD_TVI 0
#define STD_HDA 1

#define TP9950_CHIP_ID_H_REG      0xFE
#define TP9950_CHIP_ID_H_VALUE    0x28
#define TP9950_CHIP_ID_L_REG      0xFF
#define TP9950_CHIP_ID_L_VALUE    0x50

#define TP9950_LINK_FREQ_148M		(148500000UL >> 1)
#define TP9950_LINK_FREQ_297M		(297000000UL >> 1)
#define TP9950_LINK_FREQ_594M		(594000000UL >> 1)
#define TP9950_LANES				2
#define TP9950_BITS_PER_SAMPLE		8

enum tp9950_support_reso {
	TP9950_CVSTD_720P_60 = 0,
	TP9950_CVSTD_720P_50,
	TP9950_CVSTD_1080P_30,
	TP9950_CVSTD_1080P_25,
	TP9950_CVSTD_720P_30,
	TP9950_CVSTD_720P_25,
	TP9950_CVSTD_SD,
	TP9950_CVSTD_OTHER,
	TP9950_CVSTD_PAL,
	TP9950_CVSTD_NTSC,
};

int tp9950_initialize(struct techpoint *techpoint);
int tp9950_get_channel_input_status(struct i2c_client *client, u8 ch);
int tp9950_get_all_input_status(struct i2c_client *client, u8 *detect_status);
int tp9950_set_channel_reso(struct i2c_client *client, int ch,
			    enum techpoint_support_reso reso);
int tp9950_get_channel_reso(struct i2c_client *client, int ch);
int tp9950_set_quick_stream(struct i2c_client *client, u32 stream);

#endif // _TECHPOINT_TP9950_H
