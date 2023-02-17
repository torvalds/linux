/* SPDX-License-Identifier: GPL-2.0 */
/*
 * techpoint tp9930 regs
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef _TECHPOINT_TP9930_H
#define _TECHPOINT_TP9930_H

#include "techpoint_common.h"

#define TP9930_CHIP_ID_H_REG        0xFE
#define TP9930_CHIP_ID_H_VALUE      0x28
#define TP9930_CHIP_ID_L_REG        0xFF
#define TP9930_CHIP_ID_L_VALUE      0x32

#define TP9930_LINK_FREQ_74M25      (74250000)
#define TP9930_LINK_FREQ_148M5      (148500000)
#define TP9930_LINK_FREQ_297M       (297000000)

#define INPUT_STATUS_MATCH			0x7a

enum tp9930_support_reso {
	TP9930_CVSTD_720P_60 = 0,
	TP9930_CVSTD_720P_50,
	TP9930_CVSTD_1080P_30,
	TP9930_CVSTD_1080P_25,
	TP9930_CVSTD_720P_30,
	TP9930_CVSTD_720P_25,
	TP9930_CVSTD_SD,
	TP9930_CVSTD_OTHER,
};

int tp9930_initialize(struct techpoint *techpoint);
int tp9930_do_reset_pll(struct i2c_client *client);
int tp9930_pll_reset(struct i2c_client *client);

int tp9930_set_decoder_mode(struct i2c_client *client, int ch, int status);
int tp9930_set_channel_reso(struct i2c_client *client, int ch,
			    enum techpoint_support_reso reso);
int tp9930_get_channel_reso(struct i2c_client *client, int ch);
int tp9930_get_channel_input_status(struct techpoint *techpoint, u8 index);
int tp9930_get_all_input_status(struct techpoint *techpoint, u8 *detect_status);

#endif // _TECHPOINT_TP9930_H
