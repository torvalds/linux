/* SPDX-License-Identifier: GPL-2.0 */
/*
 * techpoint tp2855 regs
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef _TECHPOINT_TP2855_H
#define _TECHPOINT_TP2855_H

#include "techpoint_common.h"

#define TP2855_CHIP_ID_H_REG      0xFE
#define TP2855_CHIP_ID_H_VALUE    0x28
#define TP2855_CHIP_ID_L_REG      0xFF
#define TP2855_CHIP_ID_L_VALUE    0x55

#define TP2855_LINK_FREQ_297M		(297000000UL >> 1)
#define TP2855_LINK_FREQ_594M		(594000000UL >> 1)
#define TP2855_LANES				4
#define TP2855_BITS_PER_SAMPLE		8

enum tp2855_support_reso {
	TP2855_CVSTD_720P_60 = 0,
	TP2855_CVSTD_720P_50,
	TP2855_CVSTD_1080P_30,
	TP2855_CVSTD_1080P_25,
	TP2855_CVSTD_720P_30,
	TP2855_CVSTD_720P_25,
	TP2855_CVSTD_SD,
	TP2855_CVSTD_OTHER,
};

int tp2855_initialize(struct techpoint *techpoint);
int tp2855_get_channel_input_status(struct techpoint *techpoint, u8 ch);
int tp2855_get_all_input_status(struct techpoint *techpoint, u8 *detect_status);
int tp2855_set_decoder_mode(struct i2c_client *client, int ch, int status);
int tp2855_set_channel_reso(struct i2c_client *client, int ch,
			    enum techpoint_support_reso reso);
int tp2855_get_channel_reso(struct i2c_client *client, int ch);
int tp2855_set_quick_stream(struct techpoint *techpoint, u32 stream);

#endif // _TECHPOINT_TP2855_H
