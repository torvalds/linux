/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __GP_DEVICE_LOCAL_H_INCLUDED__
#define __GP_DEVICE_LOCAL_H_INCLUDED__

#include "gp_device_global.h"

/* @ GP_REGS_BASE -> GP_DEVICE_BASE */
#define _REG_GP_SDRAM_WAKEUP_ADDR					0x00
#define _REG_GP_IDLE_ADDR							0x04
/* #define _REG_GP_IRQ_REQ0_ADDR					0x08 */
/* #define _REG_GP_IRQ_REQ1_ADDR					0x0C */
#define _REG_GP_SP_STREAM_STAT_ADDR					0x10
#define _REG_GP_SP_STREAM_STAT_B_ADDR				0x14
#define _REG_GP_ISP_STREAM_STAT_ADDR				0x18
#define _REG_GP_MOD_STREAM_STAT_ADDR				0x1C
#define _REG_GP_SP_STREAM_STAT_IRQ_COND_ADDR		0x20
#define _REG_GP_SP_STREAM_STAT_B_IRQ_COND_ADDR		0x24
#define _REG_GP_ISP_STREAM_STAT_IRQ_COND_ADDR		0x28
#define _REG_GP_MOD_STREAM_STAT_IRQ_COND_ADDR		0x2C
#define _REG_GP_SP_STREAM_STAT_IRQ_ENABLE_ADDR		0x30
#define _REG_GP_SP_STREAM_STAT_B_IRQ_ENABLE_ADDR	0x34
#define _REG_GP_ISP_STREAM_STAT_IRQ_ENABLE_ADDR		0x38
#define _REG_GP_MOD_STREAM_STAT_IRQ_ENABLE_ADDR		0x3C
/*
#define _REG_GP_SWITCH_IF_ADDR						0x40
#define _REG_GP_SWITCH_GDC1_ADDR					0x44
#define _REG_GP_SWITCH_GDC2_ADDR					0x48
*/
#define _REG_GP_SLV_REG_RST_ADDR					0x50
#define _REG_GP_SWITCH_ISYS2401_ADDR				0x54

/* @ INPUT_FORMATTER_BASE -> GP_DEVICE_BASE */
/*
#define _REG_GP_IFMT_input_switch_lut_reg0			0x00030800
#define _REG_GP_IFMT_input_switch_lut_reg1			0x00030804
#define _REG_GP_IFMT_input_switch_lut_reg2			0x00030808
#define _REG_GP_IFMT_input_switch_lut_reg3			0x0003080C
#define _REG_GP_IFMT_input_switch_lut_reg4			0x00030810
#define _REG_GP_IFMT_input_switch_lut_reg5			0x00030814
#define _REG_GP_IFMT_input_switch_lut_reg6			0x00030818
#define _REG_GP_IFMT_input_switch_lut_reg7			0x0003081C
#define _REG_GP_IFMT_input_switch_fsync_lut			0x00030820
#define _REG_GP_IFMT_srst							0x00030824
#define _REG_GP_IFMT_slv_reg_srst					0x00030828
#define _REG_GP_IFMT_input_switch_ch_id_fmt_type	0x0003082C
*/
/* @ GP_DEVICE_BASE */
/*
#define _REG_GP_SYNCGEN_ENABLE_ADDR					0x00090000
#define _REG_GP_SYNCGEN_FREE_RUNNING_ADDR			0x00090004
#define _REG_GP_SYNCGEN_PAUSE_ADDR					0x00090008
#define _REG_GP_NR_FRAMES_ADDR						0x0009000C
#define _REG_GP_SYNGEN_NR_PIX_ADDR					0x00090010
#define _REG_GP_SYNGEN_NR_LINES_ADDR				0x00090014
#define _REG_GP_SYNGEN_HBLANK_CYCLES_ADDR			0x00090018
#define _REG_GP_SYNGEN_VBLANK_CYCLES_ADDR			0x0009001C
#define _REG_GP_ISEL_SOF_ADDR						0x00090020
#define _REG_GP_ISEL_EOF_ADDR						0x00090024
#define _REG_GP_ISEL_SOL_ADDR						0x00090028
#define _REG_GP_ISEL_EOL_ADDR						0x0009002C
#define _REG_GP_ISEL_LFSR_ENABLE_ADDR				0x00090030
#define _REG_GP_ISEL_LFSR_ENABLE_B_ADDR				0x00090034
#define _REG_GP_ISEL_LFSR_RESET_VALUE_ADDR			0x00090038
#define _REG_GP_ISEL_TPG_ENABLE_ADDR				0x0009003C
#define _REG_GP_ISEL_TPG_ENABLE_B_ADDR				0x00090040
#define _REG_GP_ISEL_HOR_CNT_MASK_ADDR				0x00090044
#define _REG_GP_ISEL_VER_CNT_MASK_ADDR				0x00090048
#define _REG_GP_ISEL_XY_CNT_MASK_ADDR				0x0009004C
#define _REG_GP_ISEL_HOR_CNT_DELTA_ADDR				0x00090050
#define _REG_GP_ISEL_VER_CNT_DELTA_ADDR				0x00090054
#define _REG_GP_ISEL_TPG_MODE_ADDR					0x00090058
#define _REG_GP_ISEL_TPG_RED1_ADDR					0x0009005C
#define _REG_GP_ISEL_TPG_GREEN1_ADDR				0x00090060
#define _REG_GP_ISEL_TPG_BLUE1_ADDR					0x00090064
#define _REG_GP_ISEL_TPG_RED2_ADDR					0x00090068
#define _REG_GP_ISEL_TPG_GREEN2_ADDR				0x0009006C
#define _REG_GP_ISEL_TPG_BLUE2_ADDR					0x00090070
#define _REG_GP_ISEL_CH_ID_ADDR						0x00090074
#define _REG_GP_ISEL_FMT_TYPE_ADDR					0x00090078
#define _REG_GP_ISEL_DATA_SEL_ADDR					0x0009007C
#define _REG_GP_ISEL_SBAND_SEL_ADDR					0x00090080
#define _REG_GP_ISEL_SYNC_SEL_ADDR					0x00090084
#define _REG_GP_SYNCGEN_HOR_CNT_ADDR				0x00090088
#define _REG_GP_SYNCGEN_VER_CNT_ADDR				0x0009008C
#define _REG_GP_SYNCGEN_FRAME_CNT_ADDR				0x00090090
#define _REG_GP_SOFT_RESET_ADDR						0x00090094
*/

struct gp_device_state_s {
	int syncgen_enable;
	int syncgen_free_running;
	int syncgen_pause;
	int nr_frames;
	int syngen_nr_pix;
	int syngen_nr_lines;
	int syngen_hblank_cycles;
	int syngen_vblank_cycles;
	int isel_sof;
	int isel_eof;
	int isel_sol;
	int isel_eol;
	int isel_lfsr_enable;
	int isel_lfsr_enable_b;
	int isel_lfsr_reset_value;
	int isel_tpg_enable;
	int isel_tpg_enable_b;
	int isel_hor_cnt_mask;
	int isel_ver_cnt_mask;
	int isel_xy_cnt_mask;
	int isel_hor_cnt_delta;
	int isel_ver_cnt_delta;
	int isel_tpg_mode;
	int isel_tpg_red1;
	int isel_tpg_green1;
	int isel_tpg_blue1;
	int isel_tpg_red2;
	int isel_tpg_green2;
	int isel_tpg_blue2;
	int isel_ch_id;
	int isel_fmt_type;
	int isel_data_sel;
	int isel_sband_sel;
	int isel_sync_sel;
	int syncgen_hor_cnt;
	int syncgen_ver_cnt;
	int syncgen_frame_cnt;
	int soft_reset;
};

#endif /* __GP_DEVICE_LOCAL_H_INCLUDED__ */
