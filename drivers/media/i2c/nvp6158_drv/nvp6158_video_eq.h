// SPDX-License-Identifier: GPL-2.0
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
#ifndef _RAPTOR3_VIDEO_EQ_H_
#define _RAPTOR3_VIDEO_EQ_H_

#include "nvp6158_common.h"
#include "nvp6158_video_auto_detect.h"
///////////////////////////////
typedef enum ACC_DEBUG
{
	ACC_GAIN_NORMAL,
	ACC_GAIN_DEBUG,

}ACC_DEBUG;

typedef enum DISTANCE
{
	SHORT_2M,
	LONG_100M,
	LONG_200M,
	LONG_300M,
	LONG_400M,
	LONG_500M,
}CABLE_DISTANCE;

typedef struct _NC_VD_AUTO_HSYNC_STR{ // 170207 Hsync Accumulation
 unsigned char Ch;
 unsigned char dev_addr;
 unsigned char h_lock;				// Bank 0 0xE2 [3:0] [Ch3:Ch0]
 unsigned int Hsync_Accum_Val1;		// Value 1  			// 170210 Add
 unsigned int Hsync_Accum_Val2;		// Value 2				// 170210 Add
 unsigned int Hsync_Accum_Result;	// Value 1 - Value 2	// 170210 Fix
}NC_VD_AUTO_HSYNC_STR;

typedef struct _NC_VD_AUTO_SAM_STR{ // 170207 SAM Value 항목 추가
 unsigned char Ch;
 unsigned char dev_addr;
 /*
 unsigned char SAMval_CD;			// B13 0xCD [7:0]
 unsigned char SAMval_CC;			// B13 0xCC [9:8]
 */
 unsigned int SAMval;
}NC_VD_AUTO_SAM_STR;

typedef struct _NC_VD_AUTO_AGC_STR{ // 170207 AGC Value 항목 추가
 unsigned char Ch;
 unsigned char devnum;
 unsigned char agc_lock;			// Bank 0 0xE0 [3:0] [Ch3:Ch0]
 unsigned char AGCval;				// B13 0xB8
}NC_VD_AUTO_AGC_STR;

typedef struct _NC_VD_AUTO_DIST_STR{ // 170207 Cable Distance 항목 추가
 unsigned char Ch;
 unsigned char devnum;
 unsigned char Dist;					// B13 0xA0
}NC_VD_AUTO_CABLE_DIST_STR;

typedef struct _NC_VD_MANUAL_DIST_STR{
 unsigned char Ch;
 unsigned char dev_addr;
 unsigned char Dist;
 unsigned char FmtDef;
 unsigned char cabletype;				// 0:coax, 1:utp, 2:reserved1, 3:reserved2
}NC_VD_MANUAL_CABLE_DIST_STR;

///////////////////////////////////
typedef struct _video_equalizer_hsync_stage_s{
	unsigned int hsync_stage[9];
}video_equalizer_hsync_stage_s;

typedef struct _video_equalizer_agc_stage_s{
	unsigned int agc_stage[9];
}video_equalizer_agc_stage_s;

typedef struct _video_equalizer_distance_table_s{
	video_equalizer_hsync_stage_s hsync_stage;
	video_equalizer_agc_stage_s   agc_stage;
} nvp6158_video_equalizer_distance_table_s;


typedef struct _video_equalizer_base_s{
	unsigned char eq_bypass[11];			// B5x01
	unsigned char eq_band_sel[11];		// B5x58
	unsigned char eq_gain_sel[11];		// B5x5C

	unsigned char deq_a_on[11];			// BAx3d
	unsigned char deq_a_sel[11];			// BAx3C
	unsigned char deq_b_sel[11];			// B9x80

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
	unsigned char h_peaking[11];			// Bank0 0x18
	unsigned char c_filter[11];			// Bank0 0x21
	unsigned char hue[11];				// Bank0 0x40
	unsigned char u_gain[11];			// Bank0 0x44
	unsigned char v_gain[11];			// Bank0 0x48
	unsigned char u_offset[11];			// Bank0 0x4c
	unsigned char v_offset[11];			// Bank0 0x50

	unsigned char black_level[11];		// Bank5 0x20
	unsigned char acc_ref[11];			// Bank5 0x27

	unsigned char cti_delay[11];			// Bank5 0x28
	unsigned char sub_saturation[11];    // Bank5 0x2B
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
	unsigned char clk_adc[11];			// Bank1 0x84
	unsigned char clk_dec[11];			// Bank1 0x8C

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

	unsigned char format_set1[11];	// B0x81
	unsigned char format_set2[11];	// B0x85

	unsigned char v_delay[11];		// B0x64
} video_equalizer_timing_b_s;


typedef struct _video_equalizer_value_table_s{
	video_equalizer_base_s		eq_base;
	video_equalizer_coeff_s 	eq_coeff;
	video_equalizer_color_s 	eq_color;

	video_equalizer_timing_a_s 	eq_timing_a;
	video_equalizer_clk_s		eq_clk;
	video_equalizer_timing_b_s	eq_timing_b;

} video_equalizer_value_table_s;

typedef struct _video_equalizer_info{
	unsigned char Ch;
	unsigned char devnum;
	unsigned char distance;
	unsigned char FmtDef;
} video_equalizer_info_s;

CABLE_DISTANCE NVP6158_NC_VD_MANUAL_CABLE_DISTANCE_Get (unsigned char Ch, video_input_cable_dist *pDistance);
CABLE_DISTANCE nvp6158_get_eq_dist(video_equalizer_info_s *ps_eq_info);
unsigned char __nvp6158_video_cable_manualdistance( unsigned char cabletype, video_input_hsync_accum *pvin_hsync_accum,
				video_input_acc_gain_val *pvin_acc_val, nvp6158_video_equalizer_distance_table_s *pdistance_value );
void __nvp6158_eq_base_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_base_s *pbase );
void __nvp6158_eq_coeff_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_coeff_s *pcoeff );
void __nvp6158_eq_color_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_color_s *pcolor );
void __nvp6158_eq_timing_a_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_timing_a_s *ptiming_a );
void __nvp6158_eq_clk_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_clk_s *pclk );
void __nvp6158_eq_timing_b_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_timing_b_s *ptiming_b );
unsigned int __nvp6158_get_acc_gain(unsigned char ch, unsigned char devnum);
unsigned int __nvp6158_get_yplus_slope(unsigned char ch, unsigned char devnum);
unsigned int __nvp6158_get_yminus_slope(unsigned char ch, unsigned char devnum);
unsigned int __nvp6158_get_sync_width( unsigned char ch, unsigned char devnum );
void __nvp6168_set_eq_ext_val(video_equalizer_info_s *pvin_eq_set);

void nvp6158_video_input_cable_manualdist_read(video_input_cable_dist *vin_cable_dist );
void nvp6168_video_input_cable_manualdist_read(video_input_cable_dist *vin_cable_dist );

int nvp6158_set_equalizer(video_equalizer_info_s *pvin_eq_set);
int nvp6168_set_equalizer(video_equalizer_info_s *pvin_eq_set);
int  nvp6158_video_input_cable_measure_way( unsigned char ch, unsigned char devnum );
CABLE_DISTANCE nvp6158_get_eq_dist(video_equalizer_info_s *ps_eq_info);

#endif /* _RAPTOR3_VIDEO_EQ_H_ */
