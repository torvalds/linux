/* SPDX-License-Identifier: GPL-2.0 */
/********************************************************************************
 *
 *  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
 *  Module		: video_eq.h
 *  Description	:
 *  Author		:
 *  Date         :
 *  Version		: Version 1.0
 *
 ********************************************************************************
 *  History      :
 *
 *
 ********************************************************************************/
#ifndef _JAGUAR1_VIDEO_EQ_H_
#define _JAGUAR1_VIDEO_EQ_H_

#include "jaguar1_common.h"

typedef struct _video_equalizer_hsync_stage_s{
	unsigned int hsync_stage[6];
}video_equalizer_hsync_stage_s;

typedef struct _video_equalizer_agc_stage_s{
	unsigned int agc_stage[6];
}video_equalizer_agc_stage_s;

typedef struct _video_equalizer_distance_table_s{
	video_equalizer_hsync_stage_s hsync_stage;
	video_equalizer_agc_stage_s   agc_stage;
} video_equalizer_distance_table_s;


typedef struct _video_equalizer_base_s{
	unsigned char eq_bypass[11];			// B5x01
	unsigned char eq_band_sel[11];		// B5x58
	unsigned char eq_gain_sel[11];		// B5x5C

	unsigned char deq_a_on[11];			// BAx3d
	unsigned char deq_a_sel[11];			// BAx3C

} video_equalizer_base_s;

typedef struct _video_equalizer_coeff_s{

	unsigned char deqA_01[11];	// BankA 0x30
	unsigned char deqA_02[11];	// BankA 0x31
	unsigned char deqA_03[11];   // BankA 0x32
	unsigned char deqA_04[11];   // BankA 0x33
	unsigned char deqA_05[11];   // BankA 0x34
	unsigned char deqA_06[11];   // BankA 0x35
	unsigned char deqA_07[11];   // BankA 0x36
	unsigned char deqA_08[11];   // BankA 0x37
	unsigned char deqA_09[11];   // BankA 0x38
	unsigned char deqA_10[11];   // BankA 0x39
	unsigned char deqA_11[11];   // BankA 0x3A
	unsigned char deqA_12[11];   // BankA 0x3B

} video_equalizer_coeff_s;

typedef struct _video_equalizer_color_s{
	unsigned char contrast[11];			// Bank0 0x10
	unsigned char y_peaking_mode[11];			// Bank0 0x18
	unsigned char y_fir_mode [11];
	unsigned char c_filter[11];			// Bank0 0x21
	unsigned char pal_cm_off[11];			// Bank0 0x21
	unsigned char hue[11];				// Bank0 0x40
	unsigned char u_gain[11];			// Bank0 0x44
	unsigned char v_gain[11];			// Bank0 0x48
	unsigned char u_offset[11];			// Bank0 0x4c
	unsigned char v_offset[11];			// Bank0 0x50

	unsigned char black_level[11];		// Bank5 0x20
	unsigned char acc_ref[11];			// Bank5 0x27

	unsigned char cti_delay[11];			// Bank5 0x28
	unsigned char saturation_b[11];    // Bank5 0x2B
	unsigned char burst_dec_a[11];       // Bank5 0x24
	unsigned char burst_dec_b[11];       // Bank5 0x5F
	unsigned char burst_dec_c[11];       // Bank5 0xD1
	unsigned char c_option[11];          // Bank5 0xD5

	unsigned char y_filter_b[11];		// BankA 0x25
	unsigned char y_filter_b_sel[11];	// BankA 0x27

} video_equalizer_color_s;

typedef struct _video_equalizer_timing_a_s{
	unsigned char h_delay_a[11];			// Bank0 0x58
	unsigned char h_delay_b[11];			// Bank0 0x89
	unsigned char h_delay_c[11];			// Bank0 0x8E
	unsigned char y_delay[11];			// Bank0 0xA0

} video_equalizer_timing_a_s;

typedef struct _video_equalizer_clk_s{
	unsigned char clk_adc_pre[11];			// Bank1 0x84
	unsigned char clk_adc_post[11];			// Bank1 0x8C
	unsigned char clk_adc[11];			// Bank1 0x8C

} video_equalizer_clk_s;

typedef struct _video_equalizer_timing_b_s{
	unsigned char h_scaler1[11];		// B9x96 + ch*0x20
	unsigned char h_scaler2[11];		// B9x97 + ch*0x20
	unsigned char h_scaler3[11];		// B9x98 + ch*0x20
	unsigned char h_scaler4[11];		// B9x99 + ch*0x20
	unsigned char h_scaler5[11];		// B9x9a + ch*0x20
	unsigned char h_scaler6[11];		// B9x9b + ch*0x20
	unsigned char h_scaler7[11];		// B9x9c + ch*0x20
	unsigned char h_scaler8[11];		// B9x9d + ch*0x20
	unsigned char h_scaler9[11];		// B9x9e + ch*0x20

	unsigned char pn_auto[11];		// B9x40 + ch

	unsigned char comb_mode[11];		// B5x90
	unsigned char h_pll_op_a[11];	// B5xB9
	unsigned char mem_path[11];		// B5x57
	unsigned char fsc_lock_speed[11]; //B5x25

	unsigned char ahd_mode[11];
	unsigned char sd_mode[11];
	unsigned char spl_mode[11];
	unsigned char vblk_end[11];
	unsigned char afe_g_sel[11];
	unsigned char afe_ctr_clp[11];
	unsigned char d_agc_option[11];
} video_equalizer_timing_b_s;


typedef struct _video_equalizer_value_table_s{
	video_equalizer_base_s		eq_base;
	video_equalizer_coeff_s 	eq_coeff;
	video_equalizer_color_s 	eq_color;

	video_equalizer_timing_a_s 	eq_timing_a;
	video_equalizer_clk_s		eq_clk;
	video_equalizer_timing_b_s	eq_timing_b;

} video_equalizer_value_table_s;

typedef struct _jaguar1_video_eq_value_table_s{
	char *name;
	NC_VIVO_CH_FORMATDEF		video_fmt;
	NC_ANALOG_INPUT 			analog_input;
	video_equalizer_base_s		eq_base;
	video_equalizer_coeff_s 	eq_coeff;
	video_equalizer_color_s 	eq_color;

	video_equalizer_timing_a_s 	eq_timing_a;
	video_equalizer_clk_s		eq_clk;
	video_equalizer_timing_b_s	eq_timing_b;

} _jaguar1_video_eq_value_table_s;

typedef struct _video_equalizer_info{
	unsigned char Ch;
	unsigned char devnum;
	unsigned char stage;
	unsigned char FmtDef;
	unsigned char Cable;
	unsigned char Input;
} video_equalizer_info_s;

void video_input_eq_val_set(video_equalizer_info_s *pvin_eq_set);
void video_input_eq_cable_set(video_equalizer_info_s *pvin_eq_set);
void video_input_eq_analog_input_set(video_equalizer_info_s *pvin_eq_set);

#endif /* _JAGUAR1_VIDEO_EQ_H_ */
