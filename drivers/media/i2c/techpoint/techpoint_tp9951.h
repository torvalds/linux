/* SPDX-License-Identifier: GPL-2.0 */
/*
 * techpoint tp9951 regs
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef _TECHPOINT_TP9951_H
#define _TECHPOINT_TP9951_H

#include "techpoint_common.h"

#define DEBUG

#define TP9951_DEF_1080P 1
#define TP9951_DEF_720P 0
#define TP9951_DEF_PAL 0
#define TP9951_DEF_NTSC 0

#define STD_TVI 0
#define STD_HDA 1

// device id 0x2860
#define TP9951_CHIP_ID_H_REG	  0xFE
#define TP9951_CHIP_ID_H_VALUE	  0x28
#define TP9951_CHIP_ID_L_REG	  0xFF
#define TP9951_CHIP_ID_L_VALUE	  0x60

#define TP9951_LINK_FREQ_148M		(148500000UL >> 1)
#define TP9951_LINK_FREQ_297M		(297000000UL >> 1)
#define TP9951_LINK_FREQ_594M		(594000000UL >> 1)
#define TP9951_LANES				2
#define TP9951_BITS_PER_SAMPLE		8

enum tp9952_support_mipi_lane {
	MIPI_2LANE,
	MIPI_1LANE,
};

#define CVBS_960H					(0) //1->960H 0->720H

enum tp9951_support_reso {
	TP9951_CVSTD_720P_60 = 0,
	TP9951_CVSTD_720P_50,
	TP9951_CVSTD_1080P_30,
	TP9951_CVSTD_1080P_25,
	TP9951_CVSTD_720P_30,
	TP9951_CVSTD_720P_25,
	TP9951_CVSTD_SD,
	TP9951_CVSTD_OTHER,
	TP9951_CVSTD_720P_275,
	TP9951_CVSTD_QHD30,	//960×540 only support with 2lane mode
	TP9951_CVSTD_QHD25,	 //960×540 only support with 2lane mode
	TP9951_CVSTD_PAL,
	TP9951_CVSTD_NTSC,
	TP9951_CVSTD_UVGA25,  //1280x960p25, must use with MIPI_4CH4LANE_445M
	TP9951_CVSTD_UVGA30,  //1280x960p30, must use with MIPI_4CH4LANE_445M
	TP9951_CVSTD_A_UVGA30,	//HDA 1280x960p30, must use with MIPI_4CH4LANE_378M
	TP9951_CVSTD_F_UVGA30,	//FH 1280x960p30, 1800x1000
	TP9951_CVSTD_HD30864, //total 1600x900 86.4M
	TP9951_CVSTD_HD30HDR, //special 720p30 with ISX019/SC120AT,total 1650x900
	TP9951_CVSTD_1080P_60,//only support with 2lane mode
	TP9951_CVSTD_1080P_50,//only support with 2lane mode
	TP9951_CVSTD_1080P_28,
	TP9951_CVSTD_1080P_275,
};

int tp9951_initialize(struct techpoint *techpoint);
int tp9951_get_channel_input_status(struct techpoint *techpoint, u8 ch);
int tp9951_get_all_input_status(struct techpoint *techpoint, u8 *detect_status);
int tp9951_set_channel_reso(struct i2c_client *client, int ch,
				enum techpoint_support_reso reso);
int tp9951_get_channel_reso(struct i2c_client *client, int ch);
int tp9951_set_quick_stream(struct i2c_client *client, u32 stream);

#endif // _TECHPOINT_TP9951_H
