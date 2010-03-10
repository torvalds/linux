/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __HW_H__
#define __HW_H__

#include "viamode.h"
#include "global.h"

/***************************************************
* Definition IGA1 Design Method of CRTC Registers *
****************************************************/
#define IGA1_HOR_TOTAL_FORMULA(x)           (((x)/8)-5)
#define IGA1_HOR_ADDR_FORMULA(x)            (((x)/8)-1)
#define IGA1_HOR_BLANK_START_FORMULA(x)     (((x)/8)-1)
#define IGA1_HOR_BLANK_END_FORMULA(x, y)     (((x+y)/8)-1)
#define IGA1_HOR_SYNC_START_FORMULA(x)      ((x)/8)
#define IGA1_HOR_SYNC_END_FORMULA(x, y)      ((x+y)/8)

#define IGA1_VER_TOTAL_FORMULA(x)           ((x)-2)
#define IGA1_VER_ADDR_FORMULA(x)            ((x)-1)
#define IGA1_VER_BLANK_START_FORMULA(x)     ((x)-1)
#define IGA1_VER_BLANK_END_FORMULA(x, y)     ((x+y)-1)
#define IGA1_VER_SYNC_START_FORMULA(x)      ((x)-1)
#define IGA1_VER_SYNC_END_FORMULA(x, y)      ((x+y)-1)

/***************************************************
** Definition IGA2 Design Method of CRTC Registers *
****************************************************/
#define IGA2_HOR_TOTAL_FORMULA(x)           ((x)-1)
#define IGA2_HOR_ADDR_FORMULA(x)            ((x)-1)
#define IGA2_HOR_BLANK_START_FORMULA(x)     ((x)-1)
#define IGA2_HOR_BLANK_END_FORMULA(x, y)     ((x+y)-1)
#define IGA2_HOR_SYNC_START_FORMULA(x)      ((x)-1)
#define IGA2_HOR_SYNC_END_FORMULA(x, y)      ((x+y)-1)

#define IGA2_VER_TOTAL_FORMULA(x)           ((x)-1)
#define IGA2_VER_ADDR_FORMULA(x)            ((x)-1)
#define IGA2_VER_BLANK_START_FORMULA(x)     ((x)-1)
#define IGA2_VER_BLANK_END_FORMULA(x, y)     ((x+y)-1)
#define IGA2_VER_SYNC_START_FORMULA(x)      ((x)-1)
#define IGA2_VER_SYNC_END_FORMULA(x, y)      ((x+y)-1)

/**********************************************************/
/* Definition IGA2 Design Method of CRTC Shadow Registers */
/**********************************************************/
#define IGA2_HOR_TOTAL_SHADOW_FORMULA(x)           ((x/8)-5)
#define IGA2_HOR_BLANK_END_SHADOW_FORMULA(x, y)     (((x+y)/8)-1)
#define IGA2_VER_TOTAL_SHADOW_FORMULA(x)           ((x)-2)
#define IGA2_VER_ADDR_SHADOW_FORMULA(x)            ((x)-1)
#define IGA2_VER_BLANK_START_SHADOW_FORMULA(x)     ((x)-1)
#define IGA2_VER_BLANK_END_SHADOW_FORMULA(x, y)     ((x+y)-1)
#define IGA2_VER_SYNC_START_SHADOW_FORMULA(x)      (x)
#define IGA2_VER_SYNC_END_SHADOW_FORMULA(x, y)      (x+y)

/* Define Register Number for IGA1 CRTC Timing */

/* location: {CR00,0,7},{CR36,3,3} */
#define IGA1_HOR_TOTAL_REG_NUM		2
/* location: {CR01,0,7} */
#define IGA1_HOR_ADDR_REG_NUM		1
/* location: {CR02,0,7} */
#define IGA1_HOR_BLANK_START_REG_NUM    1
/* location: {CR03,0,4},{CR05,7,7},{CR33,5,5} */
#define IGA1_HOR_BLANK_END_REG_NUM	3
/* location: {CR04,0,7},{CR33,4,4} */
#define IGA1_HOR_SYNC_START_REG_NUM	2
/* location: {CR05,0,4} */
#define IGA1_HOR_SYNC_END_REG_NUM       1
/* location: {CR06,0,7},{CR07,0,0},{CR07,5,5},{CR35,0,0} */
#define IGA1_VER_TOTAL_REG_NUM          4
/* location: {CR12,0,7},{CR07,1,1},{CR07,6,6},{CR35,2,2} */
#define IGA1_VER_ADDR_REG_NUM           4
/* location: {CR15,0,7},{CR07,3,3},{CR09,5,5},{CR35,3,3} */
#define IGA1_VER_BLANK_START_REG_NUM    4
/* location: {CR16,0,7} */
#define IGA1_VER_BLANK_END_REG_NUM      1
/* location: {CR10,0,7},{CR07,2,2},{CR07,7,7},{CR35,1,1} */
#define IGA1_VER_SYNC_START_REG_NUM     4
/* location: {CR11,0,3} */
#define IGA1_VER_SYNC_END_REG_NUM       1

/* Define Register Number for IGA2 Shadow CRTC Timing */

/* location: {CR6D,0,7},{CR71,3,3} */
#define IGA2_SHADOW_HOR_TOTAL_REG_NUM       2
/* location: {CR6E,0,7} */
#define IGA2_SHADOW_HOR_BLANK_END_REG_NUM   1
/* location: {CR6F,0,7},{CR71,0,2} */
#define IGA2_SHADOW_VER_TOTAL_REG_NUM       2
/* location: {CR70,0,7},{CR71,4,6} */
#define IGA2_SHADOW_VER_ADDR_REG_NUM        2
/* location: {CR72,0,7},{CR74,4,6} */
#define IGA2_SHADOW_VER_BLANK_START_REG_NUM 2
/* location: {CR73,0,7},{CR74,0,2} */
#define IGA2_SHADOW_VER_BLANK_END_REG_NUM   2
/* location: {CR75,0,7},{CR76,4,6} */
#define IGA2_SHADOW_VER_SYNC_START_REG_NUM  2
/* location: {CR76,0,3} */
#define IGA2_SHADOW_VER_SYNC_END_REG_NUM    1

/* Define Register Number for IGA2 CRTC Timing */

/* location: {CR50,0,7},{CR55,0,3} */
#define IGA2_HOR_TOTAL_REG_NUM          2
/* location: {CR51,0,7},{CR55,4,6} */
#define IGA2_HOR_ADDR_REG_NUM           2
/* location: {CR52,0,7},{CR54,0,2} */
#define IGA2_HOR_BLANK_START_REG_NUM    2
/* location: CLE266: {CR53,0,7},{CR54,3,5} => CLE266's CR5D[6]
is reserved, so it may have problem to set 1600x1200 on IGA2. */
/*         	Others: {CR53,0,7},{CR54,3,5},{CR5D,6,6} */
#define IGA2_HOR_BLANK_END_REG_NUM      3
/* location: {CR56,0,7},{CR54,6,7},{CR5C,7,7} */
/* VT3314 and Later: {CR56,0,7},{CR54,6,7},{CR5C,7,7}, {CR5D,7,7} */
#define IGA2_HOR_SYNC_START_REG_NUM     4

/* location: {CR57,0,7},{CR5C,6,6} */
#define IGA2_HOR_SYNC_END_REG_NUM       2
/* location: {CR58,0,7},{CR5D,0,2} */
#define IGA2_VER_TOTAL_REG_NUM          2
/* location: {CR59,0,7},{CR5D,3,5} */
#define IGA2_VER_ADDR_REG_NUM           2
/* location: {CR5A,0,7},{CR5C,0,2} */
#define IGA2_VER_BLANK_START_REG_NUM    2
/* location: {CR5E,0,7},{CR5C,3,5} */
#define IGA2_VER_BLANK_END_REG_NUM      2
/* location: {CR5E,0,7},{CR5F,5,7} */
#define IGA2_VER_SYNC_START_REG_NUM     2
/* location: {CR5F,0,4} */
#define IGA2_VER_SYNC_END_REG_NUM       1

/* Define Fetch Count Register*/

/* location: {SR1C,0,7},{SR1D,0,1} */
#define IGA1_FETCH_COUNT_REG_NUM        2
/* 16 bytes alignment. */
#define IGA1_FETCH_COUNT_ALIGN_BYTE     16
/* x: H resolution, y: color depth */
#define IGA1_FETCH_COUNT_PATCH_VALUE    4
#define IGA1_FETCH_COUNT_FORMULA(x, y)   \
	(((x*y)/IGA1_FETCH_COUNT_ALIGN_BYTE) + IGA1_FETCH_COUNT_PATCH_VALUE)

/* location: {CR65,0,7},{CR67,2,3} */
#define IGA2_FETCH_COUNT_REG_NUM        2
#define IGA2_FETCH_COUNT_ALIGN_BYTE     16
#define IGA2_FETCH_COUNT_PATCH_VALUE    0
#define IGA2_FETCH_COUNT_FORMULA(x, y)   \
	(((x*y)/IGA2_FETCH_COUNT_ALIGN_BYTE) + IGA2_FETCH_COUNT_PATCH_VALUE)

/* Staring Address*/

/* location: {CR0C,0,7},{CR0D,0,7},{CR34,0,7},{CR48,0,1} */
#define IGA1_STARTING_ADDR_REG_NUM      4
/* location: {CR62,1,7},{CR63,0,7},{CR64,0,7} */
#define IGA2_STARTING_ADDR_REG_NUM      3

/* Define Display OFFSET*/
/* These value are by HW suggested value*/
/* location: {SR17,0,7} */
#define K800_IGA1_FIFO_MAX_DEPTH                384
/* location: {SR16,0,5},{SR16,7,7} */
#define K800_IGA1_FIFO_THRESHOLD                328
/* location: {SR18,0,5},{SR18,7,7} */
#define K800_IGA1_FIFO_HIGH_THRESHOLD           296
/* location: {SR22,0,4}. (128/4) =64, K800 must be set zero, */
				/* because HW only 5 bits */
#define K800_IGA1_DISPLAY_QUEUE_EXPIRE_NUM      0

/* location: {CR68,4,7},{CR94,7,7},{CR95,7,7} */
#define K800_IGA2_FIFO_MAX_DEPTH                384
/* location: {CR68,0,3},{CR95,4,6} */
#define K800_IGA2_FIFO_THRESHOLD                328
/* location: {CR92,0,3},{CR95,0,2} */
#define K800_IGA2_FIFO_HIGH_THRESHOLD           296
/* location: {CR94,0,6} */
#define K800_IGA2_DISPLAY_QUEUE_EXPIRE_NUM      128

/* location: {SR17,0,7} */
#define P880_IGA1_FIFO_MAX_DEPTH                192
/* location: {SR16,0,5},{SR16,7,7} */
#define P880_IGA1_FIFO_THRESHOLD                128
/* location: {SR18,0,5},{SR18,7,7} */
#define P880_IGA1_FIFO_HIGH_THRESHOLD           64
/* location: {SR22,0,4}. (128/4) =64, K800 must be set zero, */
				/* because HW only 5 bits */
#define P880_IGA1_DISPLAY_QUEUE_EXPIRE_NUM      0

/* location: {CR68,4,7},{CR94,7,7},{CR95,7,7} */
#define P880_IGA2_FIFO_MAX_DEPTH                96
/* location: {CR68,0,3},{CR95,4,6} */
#define P880_IGA2_FIFO_THRESHOLD                64
/* location: {CR92,0,3},{CR95,0,2} */
#define P880_IGA2_FIFO_HIGH_THRESHOLD           32
/* location: {CR94,0,6} */
#define P880_IGA2_DISPLAY_QUEUE_EXPIRE_NUM      128

/* VT3314 chipset*/

/* location: {SR17,0,7} */
#define CN700_IGA1_FIFO_MAX_DEPTH               96
/* location: {SR16,0,5},{SR16,7,7} */
#define CN700_IGA1_FIFO_THRESHOLD               80
/* location: {SR18,0,5},{SR18,7,7} */
#define CN700_IGA1_FIFO_HIGH_THRESHOLD          64
/* location: {SR22,0,4}. (128/4) =64, P800 must be set zero,
				because HW only 5 bits */
#define CN700_IGA1_DISPLAY_QUEUE_EXPIRE_NUM     0
/* location: {CR68,4,7},{CR94,7,7},{CR95,7,7} */
#define CN700_IGA2_FIFO_MAX_DEPTH               96
/* location: {CR68,0,3},{CR95,4,6} */
#define CN700_IGA2_FIFO_THRESHOLD               80
/* location: {CR92,0,3},{CR95,0,2} */
#define CN700_IGA2_FIFO_HIGH_THRESHOLD          32
/* location: {CR94,0,6} */
#define CN700_IGA2_DISPLAY_QUEUE_EXPIRE_NUM     128

/* For VT3324, these values are suggested by HW */
/* location: {SR17,0,7} */
#define CX700_IGA1_FIFO_MAX_DEPTH               192
/* location: {SR16,0,5},{SR16,7,7} */
#define CX700_IGA1_FIFO_THRESHOLD               128
/* location: {SR18,0,5},{SR18,7,7} */
#define CX700_IGA1_FIFO_HIGH_THRESHOLD          128
/* location: {SR22,0,4} */
#define CX700_IGA1_DISPLAY_QUEUE_EXPIRE_NUM     124

/* location: {CR68,4,7},{CR94,7,7},{CR95,7,7} */
#define CX700_IGA2_FIFO_MAX_DEPTH               96
/* location: {CR68,0,3},{CR95,4,6} */
#define CX700_IGA2_FIFO_THRESHOLD               64
/* location: {CR92,0,3},{CR95,0,2} */
#define CX700_IGA2_FIFO_HIGH_THRESHOLD          32
/* location: {CR94,0,6} */
#define CX700_IGA2_DISPLAY_QUEUE_EXPIRE_NUM     128

/* VT3336 chipset*/
/* location: {SR17,0,7} */
#define K8M890_IGA1_FIFO_MAX_DEPTH               360
/* location: {SR16,0,5},{SR16,7,7} */
#define K8M890_IGA1_FIFO_THRESHOLD               328
/* location: {SR18,0,5},{SR18,7,7} */
#define K8M890_IGA1_FIFO_HIGH_THRESHOLD          296
/* location: {SR22,0,4}. */
#define K8M890_IGA1_DISPLAY_QUEUE_EXPIRE_NUM     124

/* location: {CR68,4,7},{CR94,7,7},{CR95,7,7} */
#define K8M890_IGA2_FIFO_MAX_DEPTH               360
/* location: {CR68,0,3},{CR95,4,6} */
#define K8M890_IGA2_FIFO_THRESHOLD               328
/* location: {CR92,0,3},{CR95,0,2} */
#define K8M890_IGA2_FIFO_HIGH_THRESHOLD          296
/* location: {CR94,0,6} */
#define K8M890_IGA2_DISPLAY_QUEUE_EXPIRE_NUM     124

/* VT3327 chipset*/
/* location: {SR17,0,7} */
#define P4M890_IGA1_FIFO_MAX_DEPTH               96
/* location: {SR16,0,5},{SR16,7,7} */
#define P4M890_IGA1_FIFO_THRESHOLD               76
/* location: {SR18,0,5},{SR18,7,7} */
#define P4M890_IGA1_FIFO_HIGH_THRESHOLD          64
/* location: {SR22,0,4}. (32/4) =8 */
#define P4M890_IGA1_DISPLAY_QUEUE_EXPIRE_NUM     32
/* location: {CR68,4,7},{CR94,7,7},{CR95,7,7} */
#define P4M890_IGA2_FIFO_MAX_DEPTH               96
/* location: {CR68,0,3},{CR95,4,6} */
#define P4M890_IGA2_FIFO_THRESHOLD               76
/* location: {CR92,0,3},{CR95,0,2} */
#define P4M890_IGA2_FIFO_HIGH_THRESHOLD          64
/* location: {CR94,0,6} */
#define P4M890_IGA2_DISPLAY_QUEUE_EXPIRE_NUM     32

/* VT3364 chipset*/
/* location: {SR17,0,7} */
#define P4M900_IGA1_FIFO_MAX_DEPTH               96
/* location: {SR16,0,5},{SR16,7,7} */
#define P4M900_IGA1_FIFO_THRESHOLD               76
/* location: {SR18,0,5},{SR18,7,7} */
#define P4M900_IGA1_FIFO_HIGH_THRESHOLD          76
/* location: {SR22,0,4}. */
#define P4M900_IGA1_DISPLAY_QUEUE_EXPIRE_NUM     32
/* location: {CR68,4,7},{CR94,7,7},{CR95,7,7} */
#define P4M900_IGA2_FIFO_MAX_DEPTH               96
/* location: {CR68,0,3},{CR95,4,6} */
#define P4M900_IGA2_FIFO_THRESHOLD               76
/* location: {CR92,0,3},{CR95,0,2} */
#define P4M900_IGA2_FIFO_HIGH_THRESHOLD          76
/* location: {CR94,0,6} */
#define P4M900_IGA2_DISPLAY_QUEUE_EXPIRE_NUM     32

/* For VT3353, these values are suggested by HW */
/* location: {SR17,0,7} */
#define VX800_IGA1_FIFO_MAX_DEPTH               192
/* location: {SR16,0,5},{SR16,7,7} */
#define VX800_IGA1_FIFO_THRESHOLD               152
/* location: {SR18,0,5},{SR18,7,7} */
#define VX800_IGA1_FIFO_HIGH_THRESHOLD          152
/* location: {SR22,0,4} */
#define VX800_IGA1_DISPLAY_QUEUE_EXPIRE_NUM      64
/* location: {CR68,4,7},{CR94,7,7},{CR95,7,7} */
#define VX800_IGA2_FIFO_MAX_DEPTH               96
/* location: {CR68,0,3},{CR95,4,6} */
#define VX800_IGA2_FIFO_THRESHOLD               64
/* location: {CR92,0,3},{CR95,0,2} */
#define VX800_IGA2_FIFO_HIGH_THRESHOLD          32
/* location: {CR94,0,6} */
#define VX800_IGA2_DISPLAY_QUEUE_EXPIRE_NUM     128

/* For VT3409 */
#define VX855_IGA1_FIFO_MAX_DEPTH               400
#define VX855_IGA1_FIFO_THRESHOLD               320
#define VX855_IGA1_FIFO_HIGH_THRESHOLD          320
#define VX855_IGA1_DISPLAY_QUEUE_EXPIRE_NUM     160

#define VX855_IGA2_FIFO_MAX_DEPTH               200
#define VX855_IGA2_FIFO_THRESHOLD               160
#define VX855_IGA2_FIFO_HIGH_THRESHOLD          160
#define VX855_IGA2_DISPLAY_QUEUE_EXPIRE_NUM     320

#define IGA1_FIFO_DEPTH_SELECT_REG_NUM          1
#define IGA1_FIFO_THRESHOLD_REG_NUM             2
#define IGA1_FIFO_HIGH_THRESHOLD_REG_NUM        2
#define IGA1_DISPLAY_QUEUE_EXPIRE_NUM_REG_NUM   1

#define IGA2_FIFO_DEPTH_SELECT_REG_NUM          3
#define IGA2_FIFO_THRESHOLD_REG_NUM             2
#define IGA2_FIFO_HIGH_THRESHOLD_REG_NUM        2
#define IGA2_DISPLAY_QUEUE_EXPIRE_NUM_REG_NUM   1

#define IGA1_FIFO_DEPTH_SELECT_FORMULA(x)                   ((x/2)-1)
#define IGA1_FIFO_THRESHOLD_FORMULA(x)                      (x/4)
#define IGA1_DISPLAY_QUEUE_EXPIRE_NUM_FORMULA(x)            (x/4)
#define IGA1_FIFO_HIGH_THRESHOLD_FORMULA(x)                 (x/4)
#define IGA2_FIFO_DEPTH_SELECT_FORMULA(x)                   (((x/2)/4)-1)
#define IGA2_FIFO_THRESHOLD_FORMULA(x)                      (x/4)
#define IGA2_DISPLAY_QUEUE_EXPIRE_NUM_FORMULA(x)            (x/4)
#define IGA2_FIFO_HIGH_THRESHOLD_FORMULA(x)                 (x/4)

/************************************************************************/
/*  LCD Timing                                                          */
/************************************************************************/

/* 500 ms = 500000 us */
#define LCD_POWER_SEQ_TD0               500000
/* 50 ms = 50000 us */
#define LCD_POWER_SEQ_TD1               50000
/* 0 us */
#define LCD_POWER_SEQ_TD2               0
/* 210 ms = 210000 us */
#define LCD_POWER_SEQ_TD3               210000
/* 2^10 * (1/14.31818M) = 71.475 us (K400.revA) */
#define CLE266_POWER_SEQ_UNIT           71
/* 2^11 * (1/14.31818M) = 142.95 us (K400.revB) */
#define K800_POWER_SEQ_UNIT             142
/* 2^13 * (1/14.31818M) = 572.1 us */
#define P880_POWER_SEQ_UNIT             572

#define CLE266_POWER_SEQ_FORMULA(x)     ((x)/CLE266_POWER_SEQ_UNIT)
#define K800_POWER_SEQ_FORMULA(x)       ((x)/K800_POWER_SEQ_UNIT)
#define P880_POWER_SEQ_FORMULA(x)       ((x)/P880_POWER_SEQ_UNIT)

/* location: {CR8B,0,7},{CR8F,0,3} */
#define LCD_POWER_SEQ_TD0_REG_NUM       2
/* location: {CR8C,0,7},{CR8F,4,7} */
#define LCD_POWER_SEQ_TD1_REG_NUM       2
/* location: {CR8D,0,7},{CR90,0,3} */
#define LCD_POWER_SEQ_TD2_REG_NUM       2
/* location: {CR8E,0,7},{CR90,4,7} */
#define LCD_POWER_SEQ_TD3_REG_NUM       2

/* LCD Scaling factor*/
/* x: indicate setting horizontal size*/
/* y: indicate panel horizontal size*/

/* Horizontal scaling factor 10 bits (2^10) */
#define CLE266_LCD_HOR_SCF_FORMULA(x, y)   (((x-1)*1024)/(y-1))
/* Vertical scaling factor 10 bits (2^10) */
#define CLE266_LCD_VER_SCF_FORMULA(x, y)   (((x-1)*1024)/(y-1))
/* Horizontal scaling factor 10 bits (2^12) */
#define K800_LCD_HOR_SCF_FORMULA(x, y)     (((x-1)*4096)/(y-1))
/* Vertical scaling factor 10 bits (2^11) */
#define K800_LCD_VER_SCF_FORMULA(x, y)     (((x-1)*2048)/(y-1))

/* location: {CR9F,0,1},{CR77,0,7},{CR79,4,5} */
#define LCD_HOR_SCALING_FACTOR_REG_NUM  3
/* location: {CR79,3,3},{CR78,0,7},{CR79,6,7} */
#define LCD_VER_SCALING_FACTOR_REG_NUM  3
/* location: {CR77,0,7},{CR79,4,5} */
#define LCD_HOR_SCALING_FACTOR_REG_NUM_CLE  2
/* location: {CR78,0,7},{CR79,6,7} */
#define LCD_VER_SCALING_FACTOR_REG_NUM_CLE  2

/************************************************
 *****     Define IGA1 Display Timing       *****
 ************************************************/
struct io_register {
	u8 io_addr;
	u8 start_bit;
	u8 end_bit;
};

/* IGA1 Horizontal Total */
struct iga1_hor_total {
	int reg_num;
	struct io_register reg[IGA1_HOR_TOTAL_REG_NUM];
};

/* IGA1 Horizontal Addressable Video */
struct iga1_hor_addr {
	int reg_num;
	struct io_register reg[IGA1_HOR_ADDR_REG_NUM];
};

/* IGA1 Horizontal Blank Start */
struct iga1_hor_blank_start {
	int reg_num;
	struct io_register reg[IGA1_HOR_BLANK_START_REG_NUM];
};

/* IGA1 Horizontal Blank End */
struct iga1_hor_blank_end {
	int reg_num;
	struct io_register reg[IGA1_HOR_BLANK_END_REG_NUM];
};

/* IGA1 Horizontal Sync Start */
struct iga1_hor_sync_start {
	int reg_num;
	struct io_register reg[IGA1_HOR_SYNC_START_REG_NUM];
};

/* IGA1 Horizontal Sync End */
struct iga1_hor_sync_end {
	int reg_num;
	struct io_register reg[IGA1_HOR_SYNC_END_REG_NUM];
};

/* IGA1 Vertical Total */
struct iga1_ver_total {
	int reg_num;
	struct io_register reg[IGA1_VER_TOTAL_REG_NUM];
};

/* IGA1 Vertical Addressable Video */
struct iga1_ver_addr {
	int reg_num;
	struct io_register reg[IGA1_VER_ADDR_REG_NUM];
};

/* IGA1 Vertical Blank Start */
struct iga1_ver_blank_start {
	int reg_num;
	struct io_register reg[IGA1_VER_BLANK_START_REG_NUM];
};

/* IGA1 Vertical Blank End */
struct iga1_ver_blank_end {
	int reg_num;
	struct io_register reg[IGA1_VER_BLANK_END_REG_NUM];
};

/* IGA1 Vertical Sync Start */
struct iga1_ver_sync_start {
	int reg_num;
	struct io_register reg[IGA1_VER_SYNC_START_REG_NUM];
};

/* IGA1 Vertical Sync End */
struct iga1_ver_sync_end {
	int reg_num;
	struct io_register reg[IGA1_VER_SYNC_END_REG_NUM];
};

/*****************************************************
**      Define IGA2 Shadow Display Timing         ****
*****************************************************/

/* IGA2 Shadow Horizontal Total */
struct iga2_shadow_hor_total {
	int reg_num;
	struct io_register reg[IGA2_SHADOW_HOR_TOTAL_REG_NUM];
};

/* IGA2 Shadow Horizontal Blank End */
struct iga2_shadow_hor_blank_end {
	int reg_num;
	struct io_register reg[IGA2_SHADOW_HOR_BLANK_END_REG_NUM];
};

/* IGA2 Shadow Vertical Total */
struct iga2_shadow_ver_total {
	int reg_num;
	struct io_register reg[IGA2_SHADOW_VER_TOTAL_REG_NUM];
};

/* IGA2 Shadow Vertical Addressable Video */
struct iga2_shadow_ver_addr {
	int reg_num;
	struct io_register reg[IGA2_SHADOW_VER_ADDR_REG_NUM];
};

/* IGA2 Shadow Vertical Blank Start */
struct iga2_shadow_ver_blank_start {
	int reg_num;
	struct io_register reg[IGA2_SHADOW_VER_BLANK_START_REG_NUM];
};

/* IGA2 Shadow Vertical Blank End */
struct iga2_shadow_ver_blank_end {
	int reg_num;
	struct io_register reg[IGA2_SHADOW_VER_BLANK_END_REG_NUM];
};

/* IGA2 Shadow Vertical Sync Start */
struct iga2_shadow_ver_sync_start {
	int reg_num;
	struct io_register reg[IGA2_SHADOW_VER_SYNC_START_REG_NUM];
};

/* IGA2 Shadow Vertical Sync End */
struct iga2_shadow_ver_sync_end {
	int reg_num;
	struct io_register reg[IGA2_SHADOW_VER_SYNC_END_REG_NUM];
};

/*****************************************************
**      Define IGA2 Display Timing                ****
******************************************************/

/* IGA2 Horizontal Total */
struct iga2_hor_total {
	int reg_num;
	struct io_register reg[IGA2_HOR_TOTAL_REG_NUM];
};

/* IGA2 Horizontal Addressable Video */
struct iga2_hor_addr {
	int reg_num;
	struct io_register reg[IGA2_HOR_ADDR_REG_NUM];
};

/* IGA2 Horizontal Blank Start */
struct iga2_hor_blank_start {
	int reg_num;
	struct io_register reg[IGA2_HOR_BLANK_START_REG_NUM];
};

/* IGA2 Horizontal Blank End */
struct iga2_hor_blank_end {
	int reg_num;
	struct io_register reg[IGA2_HOR_BLANK_END_REG_NUM];
};

/* IGA2 Horizontal Sync Start */
struct iga2_hor_sync_start {
	int reg_num;
	struct io_register reg[IGA2_HOR_SYNC_START_REG_NUM];
};

/* IGA2 Horizontal Sync End */
struct iga2_hor_sync_end {
	int reg_num;
	struct io_register reg[IGA2_HOR_SYNC_END_REG_NUM];
};

/* IGA2 Vertical Total */
struct iga2_ver_total {
	int reg_num;
	struct io_register reg[IGA2_VER_TOTAL_REG_NUM];
};

/* IGA2 Vertical Addressable Video */
struct iga2_ver_addr {
	int reg_num;
	struct io_register reg[IGA2_VER_ADDR_REG_NUM];
};

/* IGA2 Vertical Blank Start */
struct iga2_ver_blank_start {
	int reg_num;
	struct io_register reg[IGA2_VER_BLANK_START_REG_NUM];
};

/* IGA2 Vertical Blank End */
struct iga2_ver_blank_end {
	int reg_num;
	struct io_register reg[IGA2_VER_BLANK_END_REG_NUM];
};

/* IGA2 Vertical Sync Start */
struct iga2_ver_sync_start {
	int reg_num;
	struct io_register reg[IGA2_VER_SYNC_START_REG_NUM];
};

/* IGA2 Vertical Sync End */
struct iga2_ver_sync_end {
	int reg_num;
	struct io_register reg[IGA2_VER_SYNC_END_REG_NUM];
};

/* IGA1 Fetch Count Register */
struct iga1_fetch_count {
	int reg_num;
	struct io_register reg[IGA1_FETCH_COUNT_REG_NUM];
};

/* IGA2 Fetch Count Register */
struct iga2_fetch_count {
	int reg_num;
	struct io_register reg[IGA2_FETCH_COUNT_REG_NUM];
};

struct fetch_count {
	struct iga1_fetch_count iga1_fetch_count_reg;
	struct iga2_fetch_count iga2_fetch_count_reg;
};

/* Starting Address Register */
struct iga1_starting_addr {
	int reg_num;
	struct io_register reg[IGA1_STARTING_ADDR_REG_NUM];
};

struct iga2_starting_addr {
	int reg_num;
	struct io_register reg[IGA2_STARTING_ADDR_REG_NUM];
};

struct starting_addr {
	struct iga1_starting_addr iga1_starting_addr_reg;
	struct iga2_starting_addr iga2_starting_addr_reg;
};

/* LCD Power Sequence Timer */
struct lcd_pwd_seq_td0 {
	int reg_num;
	struct io_register reg[LCD_POWER_SEQ_TD0_REG_NUM];
};

struct lcd_pwd_seq_td1 {
	int reg_num;
	struct io_register reg[LCD_POWER_SEQ_TD1_REG_NUM];
};

struct lcd_pwd_seq_td2 {
	int reg_num;
	struct io_register reg[LCD_POWER_SEQ_TD2_REG_NUM];
};

struct lcd_pwd_seq_td3 {
	int reg_num;
	struct io_register reg[LCD_POWER_SEQ_TD3_REG_NUM];
};

struct _lcd_pwd_seq_timer {
	struct lcd_pwd_seq_td0 td0;
	struct lcd_pwd_seq_td1 td1;
	struct lcd_pwd_seq_td2 td2;
	struct lcd_pwd_seq_td3 td3;
};

/* LCD Scaling Factor */
struct _lcd_hor_scaling_factor {
	int reg_num;
	struct io_register reg[LCD_HOR_SCALING_FACTOR_REG_NUM];
};

struct _lcd_ver_scaling_factor {
	int reg_num;
	struct io_register reg[LCD_VER_SCALING_FACTOR_REG_NUM];
};

struct _lcd_scaling_factor {
	struct _lcd_hor_scaling_factor lcd_hor_scaling_factor;
	struct _lcd_ver_scaling_factor lcd_ver_scaling_factor;
};

struct pll_map {
	u32 clk;
	u32 cle266_pll;
	u32 k800_pll;
	u32 cx700_pll;
	u32 vx855_pll;
};

struct rgbLUT {
	u8 red;
	u8 green;
	u8 blue;
};

struct lcd_pwd_seq_timer {
	u16 td0;
	u16 td1;
	u16 td2;
	u16 td3;
};

/* Display FIFO Relation Registers*/
struct iga1_fifo_depth_select {
	int reg_num;
	struct io_register reg[IGA1_FIFO_DEPTH_SELECT_REG_NUM];
};

struct iga1_fifo_threshold_select {
	int reg_num;
	struct io_register reg[IGA1_FIFO_THRESHOLD_REG_NUM];
};

struct iga1_fifo_high_threshold_select {
	int reg_num;
	struct io_register reg[IGA1_FIFO_HIGH_THRESHOLD_REG_NUM];
};

struct iga1_display_queue_expire_num {
	int reg_num;
	struct io_register reg[IGA1_DISPLAY_QUEUE_EXPIRE_NUM_REG_NUM];
};

struct iga2_fifo_depth_select {
	int reg_num;
	struct io_register reg[IGA2_FIFO_DEPTH_SELECT_REG_NUM];
};

struct iga2_fifo_threshold_select {
	int reg_num;
	struct io_register reg[IGA2_FIFO_THRESHOLD_REG_NUM];
};

struct iga2_fifo_high_threshold_select {
	int reg_num;
	struct io_register reg[IGA2_FIFO_HIGH_THRESHOLD_REG_NUM];
};

struct iga2_display_queue_expire_num {
	int reg_num;
	struct io_register reg[IGA2_DISPLAY_QUEUE_EXPIRE_NUM_REG_NUM];
};

struct fifo_depth_select {
	struct iga1_fifo_depth_select iga1_fifo_depth_select_reg;
	struct iga2_fifo_depth_select iga2_fifo_depth_select_reg;
};

struct fifo_threshold_select {
	struct iga1_fifo_threshold_select iga1_fifo_threshold_select_reg;
	struct iga2_fifo_threshold_select iga2_fifo_threshold_select_reg;
};

struct fifo_high_threshold_select {
	struct iga1_fifo_high_threshold_select
	 iga1_fifo_high_threshold_select_reg;
	struct iga2_fifo_high_threshold_select
	 iga2_fifo_high_threshold_select_reg;
};

struct display_queue_expire_num {
	struct iga1_display_queue_expire_num
	 iga1_display_queue_expire_num_reg;
	struct iga2_display_queue_expire_num
	 iga2_display_queue_expire_num_reg;
};

struct iga1_crtc_timing {
	struct iga1_hor_total hor_total;
	struct iga1_hor_addr hor_addr;
	struct iga1_hor_blank_start hor_blank_start;
	struct iga1_hor_blank_end hor_blank_end;
	struct iga1_hor_sync_start hor_sync_start;
	struct iga1_hor_sync_end hor_sync_end;
	struct iga1_ver_total ver_total;
	struct iga1_ver_addr ver_addr;
	struct iga1_ver_blank_start ver_blank_start;
	struct iga1_ver_blank_end ver_blank_end;
	struct iga1_ver_sync_start ver_sync_start;
	struct iga1_ver_sync_end ver_sync_end;
};

struct iga2_shadow_crtc_timing {
	struct iga2_shadow_hor_total hor_total_shadow;
	struct iga2_shadow_hor_blank_end hor_blank_end_shadow;
	struct iga2_shadow_ver_total ver_total_shadow;
	struct iga2_shadow_ver_addr ver_addr_shadow;
	struct iga2_shadow_ver_blank_start ver_blank_start_shadow;
	struct iga2_shadow_ver_blank_end ver_blank_end_shadow;
	struct iga2_shadow_ver_sync_start ver_sync_start_shadow;
	struct iga2_shadow_ver_sync_end ver_sync_end_shadow;
};

struct iga2_crtc_timing {
	struct iga2_hor_total hor_total;
	struct iga2_hor_addr hor_addr;
	struct iga2_hor_blank_start hor_blank_start;
	struct iga2_hor_blank_end hor_blank_end;
	struct iga2_hor_sync_start hor_sync_start;
	struct iga2_hor_sync_end hor_sync_end;
	struct iga2_ver_total ver_total;
	struct iga2_ver_addr ver_addr;
	struct iga2_ver_blank_start ver_blank_start;
	struct iga2_ver_blank_end ver_blank_end;
	struct iga2_ver_sync_start ver_sync_start;
	struct iga2_ver_sync_end ver_sync_end;
};

/* device ID */
#define CLE266              0x3123
#define KM400               0x3205
#define CN400_FUNCTION2     0x2259
#define CN400_FUNCTION3     0x3259
/* support VT3314 chipset */
#define CN700_FUNCTION2     0x2314
#define CN700_FUNCTION3     0x3208
/* VT3324 chipset */
#define CX700_FUNCTION2     0x2324
#define CX700_FUNCTION3     0x3324
/* VT3204 chipset*/
#define KM800_FUNCTION3      0x3204
/* VT3336 chipset*/
#define KM890_FUNCTION3      0x3336
/* VT3327 chipset*/
#define P4M890_FUNCTION3     0x3327
/* VT3293 chipset*/
#define CN750_FUNCTION3     0x3208
/* VT3364 chipset*/
#define P4M900_FUNCTION3    0x3364
/* VT3353 chipset*/
#define VX800_FUNCTION3     0x3353
/* VT3409 chipset*/
#define VX855_FUNCTION3     0x3409

#define NUM_TOTAL_PLL_TABLE ARRAY_SIZE(pll_value)

struct IODATA {
	u8 Index;
	u8 Mask;
	u8 Data;
};

struct pci_device_id_info {
	u32 vendor;
	u32 device;
	u32 chip_index;
};

extern unsigned int viafb_second_virtual_xres;
extern int viafb_SAMM_ON;
extern int viafb_dual_fb;
extern int viafb_LCD2_ON;
extern int viafb_LCD_ON;
extern int viafb_DVI_ON;
extern int viafb_hotplug;

void viafb_write_reg_mask(u8 index, int io_port, u8 data, u8 mask);
void viafb_set_output_path(int device, int set_iga,
	int output_interface);

void viafb_fill_crtc_timing(struct crt_mode_table *crt_table,
	struct VideoModeTable *video_mode, int bpp_byte, int set_iga);

void viafb_set_vclock(u32 CLK, int set_iga);
void viafb_load_reg(int timing_value, int viafb_load_reg_num,
	struct io_register *reg,
	      int io_type);
void viafb_crt_disable(void);
void viafb_crt_enable(void);
void init_ad9389(void);
/* Access I/O Function */
void viafb_write_reg(u8 index, u16 io_port, u8 data);
u8 viafb_read_reg(int io_port, u8 index);
void viafb_lock_crt(void);
void viafb_unlock_crt(void);
void viafb_load_fetch_count_reg(int h_addr, int bpp_byte, int set_iga);
void viafb_write_regx(struct io_reg RegTable[], int ItemNum);
u32 viafb_get_clk_value(int clk);
void viafb_load_FIFO_reg(int set_iga, int hor_active, int ver_active);
void viafb_set_dpa_gfx(int output_interface, struct GFX_DPA_SETTING\
					*p_gfx_dpa_setting);

int viafb_setmode(struct VideoModeTable *vmode_tbl, int video_bpp,
	struct VideoModeTable *vmode_tbl1, int video_bpp1);
void viafb_fill_var_timing_info(struct fb_var_screeninfo *var, int refresh,
	struct VideoModeTable *vmode_tbl);
void viafb_init_chip_info(struct pci_dev *pdev,
			  const struct pci_device_id *pdi);
void viafb_init_dac(int set_iga);
int viafb_get_pixclock(int hres, int vres, int vmode_refresh);
int viafb_get_refresh(int hres, int vres, u32 float_refresh);
void viafb_update_device_setting(int hres, int vres, int bpp,
			   int vmode_refresh, int flag);

int viafb_get_fb_size_from_pci(void);
void viafb_set_iga_path(void);
void viafb_set_primary_address(u32 addr);
void viafb_set_secondary_address(u32 addr);
void viafb_set_primary_pitch(u32 pitch);
void viafb_set_secondary_pitch(u32 pitch);
void viafb_set_primary_color_register(u8 index, u8 red, u8 green, u8 blue);
void viafb_set_secondary_color_register(u8 index, u8 red, u8 green, u8 blue);
void viafb_get_fb_info(unsigned int *fb_base, unsigned int *fb_len);

#endif /* __HW_H__ */
