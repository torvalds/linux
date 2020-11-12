/* SPDX-License-Identifier: GPL-2.0 */
/********************************************************************************
 *
 *  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
 *  Module		: Jaguar1 Device Driver
 *  Description	: MIPI
 *  Author		:
 *  Date         :
 *  Version		: Version 1.0
 *
 ********************************************************************************
 *  History      :
 *
 *
 ********************************************************************************/
#ifndef _JAGUAR1_CLOCK_
#define _JAGUAR1_CLOCK_

#include "jaguar1_video.h"

#define VD_DATA_TYPE_YUV422         (0x01)
#define VD_DATA_TYPE_YUV420         (0x02)
#define VD_DATA_TYPE_LEGACY420      (0x03)

typedef struct _mipi_vdfmt_set_s{
	unsigned char arb_scale;
	unsigned char mipi_frame_opt;
}mipi_vdfmt_set_s;

extern unsigned int jaguar1_mclk;
extern unsigned int jaguar1_lane;

void arb_init(int dev_num);
void arb_enable(int dev_num);
void arb_disable(int dev_num);
int mipi_datatype_set(unsigned char data_type);
void mipi_tx_init(int dev_num);
void mipi_video_format_set(video_input_init *dev_ch_info);
void disable_parallel(int dev_num);

#endif
