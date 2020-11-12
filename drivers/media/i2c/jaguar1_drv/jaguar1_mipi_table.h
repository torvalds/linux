/* SPDX-License-Identifier: GPL-2.0 */
/********************************************************************************
 *
 *  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
 *  Module		: Jaguar1 Device Driver
 *  Description	: arb_mipi_table.h
 *  Author		:
 *  Date         :
 *  Version		: Version 1.0
 *
 ********************************************************************************
 *  History      :
 *
 *
 ********************************************************************************/
#ifndef _ARB_MIPI_TABLE_H_
#define _ARB_MIPI_TABLE_H_

#include "jaguar1_common.h"

/* -----------------------------------------------------------------------------
 * arb_scale(20x01)                  : SD=2(1/4), HD=1(1/2), FHD=0(bypass)
 * mipi_frame_opt(21x3E, 21x3F)      : SD only [TBD]
 *-----------------------------------------------------------------------------*/


mipi_vdfmt_set_s decoder_mipi_fmtdef[ NC_VIVO_CH_FORMATDEF_MAX ] =
{
	[ AHD20_SD_H960_2EX_Btype_NT ] = {
		.arb_scale = 0x02,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_H960_2EX_Btype_PAL ] = {
		.arb_scale = 0x02,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_SH720_NT] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_SH720_PAL] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_H960_NT ] = {
		.arb_scale = 0x02,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_H960_PAL ] = {
		.arb_scale = 0x02,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_H1280_NT ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_H1280_PAL ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_H1440_NT ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_H1440_PAL ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_H960_EX_NT ] = {
		.arb_scale = 0x02,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_H960_EX_PAL ] = {
		.arb_scale = 0x02,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_H960_2EX_NT ] = {
		.arb_scale = 0x02,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_SD_H960_2EX_PAL ] = {
		.arb_scale = 0x02,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_1080P_30P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_1080P_25P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_720P_60P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_720P_50P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_720P_30P ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_720P_25P ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_720P_30P_EX ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_720P_25P_EX ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_720P_30P_EX_Btype ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_720P_25P_EX_Btype ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_720P_960P_30P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD20_720P_960P_25P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_3M_30P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_3M_25P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_3M_18P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_4M_30P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_4M_25P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_4M_15P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_5M_20P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_5M_12_5P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_5_3M_20P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_6M_18P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_6M_20P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_8M_X_30P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_8M_X_25P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_8M_7_5P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_8M_12_5P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ AHD30_8M_15P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},

	/* TVI */
	[ TVI_FHD_30P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_FHD_25P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_HD_60P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_HD_50P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_HD_30P ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_HD_25P ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_HD_30P_EX ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_HD_25P_EX ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_HD_B_30P ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_HD_B_25P ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_HD_B_30P_EX ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_HD_B_25P_EX ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_3M_18P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_5M_12_5P  ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_4M_30P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ TVI_4M_25P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},

	/* CVI */
	[ CVI_FHD_30P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ CVI_FHD_25P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ CVI_HD_60P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ CVI_HD_50P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ CVI_HD_30P ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ CVI_HD_25P ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ CVI_HD_30P_EX ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ CVI_HD_25P_EX ] = {
		.arb_scale = 0x01,
		.mipi_frame_opt = 0x00,
	},
	[ CVI_4M_30P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ CVI_4M_25P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ CVI_8M_12_5P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
	[ CVI_8M_15P ] = {
		.arb_scale = 0x00,
		.mipi_frame_opt = 0x00,
	},
};

#endif /* VIDEO_DECODER_JAGUAR1_DRV_ARB_MIPI_TABLE_H_ */
