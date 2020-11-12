// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
 *
 *  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
 *  Module		: video_auto_detect.c
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
#include <linux/string.h>
#include <linux/delay.h>

#include "jaguar1_common.h"
#include "jaguar1_video_eq.h"
#include "jaguar1_cableA_video_eq_table.h"
#include "jaguar1_reg_set_def.h"
#include "jaguar1_video.h"

//extern unsigned int jaguar1_i2c_addr[4];


static NC_JAGUAR1_EQ NC_VD_EQ_FindFormatDef( NC_VIVO_CH_FORMATDEF format_standard, NC_ANALOG_INPUT analog_input  )
{
	int ii;

	for(ii=0;ii<NC_EQ_SETTING_FMT_MAX;ii++)
	{
		_jaguar1_video_eq_value_table_s *pFmt = &equalizer_value_fmtdef_cableA[ ii ];

		if( pFmt->video_fmt == format_standard )
			if( pFmt->analog_input == analog_input )
				return ii;
	}

	printk("NC_VD_EQ_FindFormatDef UNKNOWN format!!!\n");

	return NC_EQ_SETTING_FMT_UNKNOWN;
}

static void __eq_base_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_base_s *pbase )
{
	unsigned char ch = pvin_eq_set->Ch;
	unsigned char dist = pvin_eq_set->stage;

	REG_SET_5x65_0_8_EQ_BYPASS( ch, pbase->eq_bypass[dist] );
	REG_SET_5x58_0_8_EQ_BAND_SEL( ch, pbase->eq_band_sel[dist] );
	REG_SET_5x5C_0_8_EQ_GAIN_SEL( ch, pbase->eq_gain_sel[dist] );
	REG_SET_Ax3D_0_8_EQ_DEQ_A_ON( ch, pbase->deq_a_on[dist] );
	REG_SET_Ax3C_0_8_EQ_DEQ_A_SEL( ch, pbase->deq_a_sel[dist] );

}

static void __eq_coeff_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_coeff_s *pcoeff )
{

	unsigned char ch = pvin_eq_set->Ch;
	unsigned char dist = pvin_eq_set->stage;

	REG_SET_Ax30_0_8_EQ_DEQ_A_01( ch, pcoeff->deqA_01[dist] );
	REG_SET_Ax31_0_8_EQ_DEQ_A_02( ch, pcoeff->deqA_02[dist] );
	REG_SET_Ax32_0_8_EQ_DEQ_A_03( ch, pcoeff->deqA_03[dist] );
	REG_SET_Ax33_0_8_EQ_DEQ_A_04( ch, pcoeff->deqA_04[dist] );
	REG_SET_Ax34_0_8_EQ_DEQ_A_05( ch, pcoeff->deqA_05[dist] );
	REG_SET_Ax35_0_8_EQ_DEQ_A_06( ch, pcoeff->deqA_06[dist] );
	REG_SET_Ax36_0_8_EQ_DEQ_A_07( ch, pcoeff->deqA_07[dist] );
	REG_SET_Ax37_0_8_EQ_DEQ_A_08( ch, pcoeff->deqA_08[dist] );
	REG_SET_Ax38_0_8_EQ_DEQ_A_09( ch, pcoeff->deqA_09[dist] );
	REG_SET_Ax39_0_8_EQ_DEQ_A_10( ch, pcoeff->deqA_10[dist] );
	REG_SET_Ax3A_0_8_EQ_DEQ_A_11( ch, pcoeff->deqA_11[dist] );
	REG_SET_Ax3B_0_8_EQ_DEQ_A_12( ch, pcoeff->deqA_12[dist] );

}

static void __eq_color_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_color_s *pcolor )
{
	unsigned char ch = pvin_eq_set->Ch;
	unsigned char dist = pvin_eq_set->stage;

	REG_SET_0x24_0_8_EQ_COLOR_CONTRAST( ch, pcolor->contrast[dist] );
	REG_SET_0x30_0_8_EQ_COLOR_H_PEAKING_1( ch, pcolor->y_peaking_mode[dist] );
	REG_SET_0x34_0_8_EQ_COLOR_H_PEAKING_2( ch, pcolor->y_fir_mode[dist] );


	REG_SET_5x31_0_8_EQ_COLOR_C_FILTER( ch, pcolor->c_filter[dist] );


	REG_SET_0x5c_0_8_EQ_PAL_CM_OFF( ch, pcolor->pal_cm_off[dist] );

	REG_SET_0x40_0_8_EQ_COLOR_HUE( ch, pcolor->hue[dist] );
	REG_SET_0x44_0_8_EQ_COLOR_U_GAIN( ch, pcolor->u_gain[dist] );
	REG_SET_0x48_0_8_EQ_COLOR_V_GAIN( ch, pcolor->v_gain[dist] );
	REG_SET_0x4C_0_8_EQ_COLOR_U_OFFSET( ch, pcolor->u_offset[dist] );
	REG_SET_0x50_0_8_EQ_COLOR_V_OFFSET( ch, pcolor->v_offset[dist] );
	REG_SET_0x28_0_8_EQ_COLOR_BLACK_LEVEL( ch, pcolor->black_level[dist] );

	REG_SET_5x27_0_8_EQ_COLOR_ACC_REF( ch, pcolor->acc_ref[dist] );
	REG_SET_5x28_0_8_EQ_COLOR_CTI_DELAY( ch, pcolor->cti_delay[dist] );
	REG_SET_5x2b_0_8_EQ_COLOR_SUB_SATURATION( ch, pcolor->saturation_b[dist] );
	REG_SET_5x24_0_8_EQ_COLOR_BURST_DEC_A( ch, pcolor->burst_dec_a[dist] );
	REG_SET_5x5F_0_8_EQ_COLOR_BURST_DEC_B( ch, pcolor->burst_dec_b[dist] );
	REG_SET_5xD1_0_8_EQ_COLOR_BURST_DEC_C( ch, pcolor->burst_dec_c[dist] );
	REG_SET_5xD5_0_8_EQ_COLOR_C_OPTION( ch, pcolor->c_option[dist] );
	REG_SET_Ax25_0_8_EQ_COLOR_Y_FILTER_B( ch, pcolor->y_filter_b[dist] );
	REG_SET_Ax27_0_8_EQ_COLOR_Y_FILTER_B_SEL( ch, pcolor->y_filter_b_sel[dist] );

}

static void __eq_timing_a_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_timing_a_s *ptiming_a )
{
	unsigned char ch = pvin_eq_set->Ch;
	unsigned char dist = pvin_eq_set->stage;

	REG_SET_0x68_0_8_EQ_TIMING_A_H_DELAY_A(ch, ptiming_a->h_delay_a[dist] );
	REG_SET_5x38_0_8_EQ_TIMING_A_H_DELAY_B(ch, ptiming_a->h_delay_b[dist] );
	REG_SET_0x6C_0_4_EQ_TIMING_A_H_DELAY_C(ch, ptiming_a->h_delay_c[dist] );

	REG_SET_0x64_0_8_EQ_TIMING_A_Y_DELAY(ch ,  ptiming_a->y_delay[dist] );

}

static void __eq_clk_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_clk_s *pclk )
{
	unsigned char ch = pvin_eq_set->Ch;
	unsigned char dist = pvin_eq_set->stage;

	REG_SET_1x84_0_8_EQ_CLOCK_ADC_CLK( ch, pclk->clk_adc[dist] );
	REG_SET_1x88_0_8_EQ_CLOCK_PRE_CLK( ch, pclk->clk_adc_pre[dist] );
	REG_SET_1x8C_0_8_EQ_CLOCK_POST_CLK( ch, pclk->clk_adc_post[dist] );

}
static void __eq_timing_b_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_timing_b_s *ptiming_b )
{
	unsigned char ch = pvin_eq_set->Ch;
	unsigned char dist = pvin_eq_set->stage;

	REG_SET_9x96_0_8_EQ_TIMING_B_HSCALER_1( ch, ptiming_b->h_scaler1[dist] );
	REG_SET_9x97_0_8_EQ_TIMING_B_HSCALER_2( ch, ptiming_b->h_scaler2[dist] );
	REG_SET_9x98_0_8_EQ_TIMING_B_HSCALER_3( ch, ptiming_b->h_scaler3[dist] );
	REG_SET_9x99_0_8_EQ_TIMING_B_HSCALER_4( ch, ptiming_b->h_scaler4[dist] );
	REG_SET_9x9A_0_8_EQ_TIMING_B_HSCALER_5( ch, ptiming_b->h_scaler5[dist] );
	REG_SET_9x9B_0_8_EQ_TIMING_B_HSCALER_6( ch, ptiming_b->h_scaler6[dist] );
	REG_SET_9x9C_0_8_EQ_TIMING_B_HSCALER_7( ch, ptiming_b->h_scaler7[dist] );
	REG_SET_9x9D_0_8_EQ_TIMING_B_HSCALER_8( ch, ptiming_b->h_scaler8[dist] );
	REG_SET_9x9E_0_8_EQ_TIMING_B_HSCALER_9( ch, ptiming_b->h_scaler9[dist] );
	REG_SET_9x40_0_8_EQ_TIMING_B_PN_AUTO( ch, ptiming_b->pn_auto[dist] );
	REG_SET_5x90_0_8_EQ_TIMINING_B_COMB_MODE( ch, ptiming_b->comb_mode[dist] );
	REG_SET_5xB9_0_8_EQ_TIMING_B_HPLL_OP_A( ch, ptiming_b->h_pll_op_a[dist] );
	REG_SET_5x57_0_8_EQ_TIMING_B_MEM_PATH( ch, ptiming_b->mem_path[dist] );
	REG_SET_5x25_0_8_EQ_TIMING_B_FSC_LOCK_SPD( ch, ptiming_b->fsc_lock_speed[dist] );

	REG_SET_0x04_0_8_EQ_TIMING_B_SD_MD( ch, ptiming_b->sd_mode[dist] );
	REG_SET_0x08_0_8_EQ_TIMING_B_AHD_MD( ch, ptiming_b->ahd_mode[dist] );
	REG_SET_0x0C_0_8_EQ_TIMING_B_SPECIAL_MD( ch, ptiming_b->spl_mode[dist] );
	REG_SET_0x78_0_8_EQ_TIMING_B_VBLK_END( ch, ptiming_b->vblk_end[dist] );

	REG_SET_5x1D_0_8_EQ_AFE_G_SEL( ch, ptiming_b->afe_g_sel[dist] );
	REG_SET_5x01_0_8_EQ_AFE_CTR_CLP( ch, ptiming_b->afe_ctr_clp[dist] );
	REG_SET_5x05_0_8_EQ_D_AGC_OPTION( ch, ptiming_b->d_agc_option[dist] );

}

void video_input_eq_val_set(video_equalizer_info_s *pvin_eq_set)
{
	NC_JAGUAR1_EQ eq_fmt;
	unsigned char ch = pvin_eq_set->Ch;
	int fmt = pvin_eq_set->FmtDef;
	int input = pvin_eq_set->Input;
	int cable = pvin_eq_set->Cable;
	/* int stage = pvin_eq_set->stage; */
	_jaguar1_video_eq_value_table_s eq_value;

	//	printk("[drv_eq]ch%d >> fmt(%d) cable(%d) stage(%d) input(%d)\n", ch, fmt, cable, stage, input);
	eq_fmt = NC_VD_EQ_FindFormatDef( fmt, input );

	if( cable == CABLE_A )
		eq_value = (_jaguar1_video_eq_value_table_s)equalizer_value_fmtdef_cableA[eq_fmt];
	else if( cable == CABLE_B )
		eq_value = (_jaguar1_video_eq_value_table_s)equalizer_value_fmtdef_cableA[eq_fmt];
	else if( cable == CABLE_C )
		eq_value = (_jaguar1_video_eq_value_table_s)equalizer_value_fmtdef_cableA[eq_fmt];
	else if( cable == CABLE_D )
		eq_value = (_jaguar1_video_eq_value_table_s)equalizer_value_fmtdef_cableA[eq_fmt];
	else
		eq_value = (_jaguar1_video_eq_value_table_s)equalizer_value_fmtdef_cableA[eq_fmt];

	if( eq_value.name == NULL )
	{
		printk("[drv_eq]Error - Unknown EQ Table!!\n");
		return;
	}
	else
	{
		/* set_eq_value */
		__eq_base_set_value( pvin_eq_set, &eq_value.eq_base );
		__eq_coeff_set_value( pvin_eq_set, &eq_value.eq_coeff );
		__eq_color_set_value( pvin_eq_set, &eq_value.eq_color);
		__eq_timing_a_set_value( pvin_eq_set, &eq_value.eq_timing_a );
		__eq_clk_set_value( pvin_eq_set, &eq_value.eq_clk );
		__eq_timing_b_set_value( pvin_eq_set, &eq_value.eq_timing_b );

		if( AHD20_SD_H960_2EX_Btype_NT_SINGLE_ENDED || AHD20_SD_H960_2EX_Btype_NT_DIFFERENTIAL )
		{

		}
		else if( AHD20_SD_H960_2EX_Btype_PAL_SINGLE_ENDED || AHD20_SD_H960_2EX_Btype_PAL_DIFFERENTIAL )
		{

		}
		else
		{

		}
		printk("[drv_eq]ch::%d >>> fmt::%s\n", ch, eq_value.name);
	}
}


void video_input_eq_cable_set(video_equalizer_info_s *pvin_eq_set)
{
	unsigned char ch = pvin_eq_set->Ch;
	int cable = pvin_eq_set->Cable;

	printk("[DRV]video_input_eq_cable_set::ch(%d) cable(%d)\n", ch, cable );
}

void video_input_eq_analog_input_set(video_equalizer_info_s *pvin_eq_set)
{
	unsigned char ch = pvin_eq_set->Ch;
	int input = pvin_eq_set->Input;

	REG_SET_0x18_0_8_EX_CBAR_ON( ch, 0x13 );

	if( input == DIFFERENTIAL )
	{
		REG_SET_5x00_0_8_CMP( ch, 0xd0 );
		REG_SET_5x01_0_8_CML( ch, 0x2c );
		REG_SET_5x1D_0_8_AFE( ch, 0x8c );
		REG_SET_5x92_0_8_PWM( ch, 0x00 );
	}
	else if( input == SINGLE_ENDED )
	{
		REG_SET_5x00_0_8_CMP( ch, 0xd0 );
		REG_SET_5x01_0_8_CML( ch, 0xa2 );
		//		REG_SET_5x1D_0_8_AFE( ch, 0x00 );
		REG_SET_5x92_0_8_PWM( ch, 0x00 );
	}
	else
	{
		printk("Jaguar1 Analog Input Setting Fail !!!\n");
	}

	printk("[DRV]video_input_eq_analog_input_set::ch(%d) input(%d)\n", ch, input );
}

