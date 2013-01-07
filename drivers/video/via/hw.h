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

#include <linux/seq_file.h>

#include "viamode.h"
#include "global.h"
#include "via_modesetting.h"

#define viafb_read_reg(p, i)			via_read_reg(p, i)
#define viafb_write_reg(i, p, d)		via_write_reg(p, i, d)
#define viafb_write_reg_mask(i, p, d, m)	via_write_reg_mask(p, i, d, m)

/* VIA output devices */
#define VIA_LDVP0	0x00000001
#define VIA_LDVP1	0x00000002
#define VIA_DVP0	0x00000004
#define VIA_CRT		0x00000010
#define VIA_DVP1	0x00000020
#define VIA_LVDS1	0x00000040
#define VIA_LVDS2	0x00000080

/* VIA output device power states */
#define VIA_STATE_ON		0
#define VIA_STATE_STANDBY	1
#define VIA_STATE_SUSPEND	2
#define VIA_STATE_OFF		3

/* VIA output device sync polarity */
#define VIA_HSYNC_NEGATIVE	0x01
#define VIA_VSYNC_NEGATIVE	0x02

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

/* For VT3410 */
#define VX900_IGA1_FIFO_MAX_DEPTH               400
#define VX900_IGA1_FIFO_THRESHOLD               320
#define VX900_IGA1_FIFO_HIGH_THRESHOLD          320
#define VX900_IGA1_DISPLAY_QUEUE_EXPIRE_NUM     160

#define VX900_IGA2_FIFO_MAX_DEPTH               192
#define VX900_IGA2_FIFO_THRESHOLD               160
#define VX900_IGA2_FIFO_HIGH_THRESHOLD          160
#define VX900_IGA2_DISPLAY_QUEUE_EXPIRE_NUM     320

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

struct io_register {
	u8 io_addr;
	u8 start_bit;
	u8 end_bit;
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

struct pll_limit {
	u16 multiplier_min;
	u16 multiplier_max;
	u8 divisor;
	u8 rshift;
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

/* device ID */
#define CLE266_FUNCTION3    0x3123
#define KM400_FUNCTION3     0x3205
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
/* VT3410 chipset*/
#define VX900_FUNCTION3     0x3410

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

struct via_device_mapping {
	u32 device;
	const char *name;
};

extern int viafb_SAMM_ON;
extern int viafb_dual_fb;
extern int viafb_LCD2_ON;
extern int viafb_LCD_ON;
extern int viafb_DVI_ON;
extern int viafb_hotplug;

struct display_timing var_to_timing(const struct fb_var_screeninfo *var,
	u16 cxres, u16 cyres);
void viafb_fill_crtc_timing(const struct fb_var_screeninfo *var,
	u16 cxres, u16 cyres, int iga);
void viafb_set_vclock(u32 CLK, int set_iga);
void viafb_load_reg(int timing_value, int viafb_load_reg_num,
	struct io_register *reg,
	      int io_type);
void via_set_source(u32 devices, u8 iga);
void via_set_state(u32 devices, u8 state);
void via_set_sync_polarity(u32 devices, u8 polarity);
u32 via_parse_odev(char *input, char **end);
void via_odev_to_seq(struct seq_file *m, u32 odev);
void init_ad9389(void);
/* Access I/O Function */
void viafb_lock_crt(void);
void viafb_unlock_crt(void);
void viafb_load_fetch_count_reg(int h_addr, int bpp_byte, int set_iga);
void viafb_write_regx(struct io_reg RegTable[], int ItemNum);
void viafb_load_FIFO_reg(int set_iga, int hor_active, int ver_active);
void viafb_set_dpa_gfx(int output_interface, struct GFX_DPA_SETTING\
					*p_gfx_dpa_setting);

int viafb_setmode(void);
void viafb_fill_var_timing_info(struct fb_var_screeninfo *var,
	const struct fb_videomode *mode);
void viafb_init_chip_info(int chip_type);
void viafb_init_dac(int set_iga);
int viafb_get_refresh(int hres, int vres, u32 float_refresh);
void viafb_update_device_setting(int hres, int vres, int bpp, int flag);

void viafb_set_iga_path(void);
void viafb_set_primary_color_register(u8 index, u8 red, u8 green, u8 blue);
void viafb_set_secondary_color_register(u8 index, u8 red, u8 green, u8 blue);
void viafb_get_fb_info(unsigned int *fb_base, unsigned int *fb_len);

#endif /* __HW_H__ */
