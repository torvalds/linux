/* SPDX-License-Identifier: GPL-2.0 */
/********************************************************************************
 *
 *  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
 *  Module		: Jaguar1 Device Driver
 *  Description	: coax_table.h
 *  Author		:
 *  Date         :
 *  Version		: Version 1.0
 *
 ********************************************************************************
 *  History      :
 *
 *
 ********************************************************************************/
#ifndef _JAGUAR1_REGISTER_SET_DEFINE_
#define _JAGUAR1_REGISTER_SET_DEFINE_

#include "jaguar1_video.h"


/*=================================================================================================
 *
 * REG_SET_BANKxADDR_StartBit_Size_RegName( Channel, Setting Value )
 *
 *=================================================================================================*/


// vd_jaguar1_init_set
#define REG_SET_0x00_0_8_EACH_SET(ch, val) vd_register_set ( 0 , 0x00 , 0x00 + ch , val , 0 , 8 )

// vd_jaguar1_single_differ_set
#define REG_SET_0x18_0_8_EX_CBAR_ON(ch, val) vd_register_set ( 0 , 0x00 , 0x18 + ch , val , 0 , 8 )
#define REG_SET_5x00_0_8_CMP(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x00 , val , 0 , 8 )
#define REG_SET_5x01_0_8_CML(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x01 , val , 0 , 8 )
#define REG_SET_5x1D_0_8_AFE(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x1d , val , 0 , 8 )
#define REG_SET_5x92_0_8_PWM(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x92 , val , 0 , 8 )

// vd_vo_port_y_c_merge_set
#define REG_SET_1xEC_0_8_yc_merge(ch, val) vd_register_set ( 0 , 0x01 , 0xec + ch , val , 0 , 8 )

// vd_vo_mux_mode_set
#define REG_SET_1xC8_0_8_out_sel(ch, val) vd_register_set ( 0 , 0x01 , 0xc8 + ch , val , 0 , 8 )

// vd_vo_manual_mode_set


// vd_vi_manual_set_seq1
#define REG_SET_1x7C_0_1_clk_auto_1(ch, val) vd_register_set ( 0 , 0x01 , 0x7c , val , 0 , 1 )
#define REG_SET_1x7C_1_1_clk_auto_2(ch, val) vd_register_set ( 0 , 0x01 , 0x7c , val , 1 , 1 )
#define REG_SET_1x7C_2_1_clk_auto_3(ch, val) vd_register_set ( 0 , 0x01 , 0x7c , val , 2 , 1 )
#define REG_SET_1x7C_3_1_clk_auto_4(ch, val) vd_register_set ( 0 , 0x01 , 0x7c , val , 3 , 1 )

#define REG_SET_5x32_0_8_NOVIDEO_DET_A(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x32 , val , 0 , 8 )
#define REG_SET_5xB9_0_8_HAFC_LPF_SEL(ch, val) vd_register_set ( 0 , 0x05 + ch , 0xb9 , val , 0 , 8 )
#define REG_SET_9x44_0_8_FSC_EXT_EN(ch, val) vd_register_set ( 0 , 0x09 , 0x44 + ch , val , 0 , 8 )
#define REG_SET_5x6E_0_8_VBLK_END_SEL(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x6e , val , 0 , 8 )
#define REG_SET_5x6F_0_8_VBLK_END_EXT(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x6f , val , 0 , 8 )

// afe_reg
#define REG_SET_5x00_0_8_A_CMP_PW_MODE(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x00 , val , 0 , 8 )
#define REG_SET_5x02_0_8_A_CMP_TIMEUNIT(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x02 , val , 0 , 8 )
#define REG_SET_5x1E_0_8_VAFEMD(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x1e , val , 0 , 8 )
#define REG_SET_5x58_0_8_VAFE1_EQ_BAND_SEL(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x58 , val , 0 , 8 )
#define REG_SET_5x59_0_8_LPF_BYPASS(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x59 , val , 0 , 8 )
#define REG_SET_5x5A_0_8_VAFE_IMP_CNT(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x5a , val , 0 , 8 )
#define REG_SET_5x5B_0_8_VAFE_DUTY(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x5b , val , 0 , 8 )
#define REG_SET_5x5C_0_8_VAFE_B_LPF_SEL(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x5c , val , 0 , 8 )
#define REG_SET_5x94_0_8_PWM_DELAY_H(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x94 , val , 0 , 8 )
#define REG_SET_5x95_0_8_PWM_DELAY_L(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x95 , val , 0 , 8 )
#define REG_SET_5x65_0_8_VAFE_CML_SPEED(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x65 , val , 0 , 8 )


// vd_vi_format_set_seq3
#define REG_SET_0x10_0_8_VD_FMT(ch, val) vd_register_set ( 0 , 0x00 , 0x10 + ch , val , 0 , 8 )
#define REG_SET_0x0C_0_8_SPL_MODE(ch, val) vd_register_set ( 0 , 0x00 , 0x0c + ch , val , 0 , 8 )
#define REG_SET_0x04_0_8_SD_MODE(ch, val) vd_register_set ( 0 , 0x00 , 0x04 + ch , val , 0 , 8 )
#define REG_SET_0x08_0_8_AHD_MODE(ch, val) vd_register_set ( 0 , 0x00 , 0x08 + ch , val , 0 , 8 )
#define REG_SET_5x69_0_1_SD_FREQ_SEL(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x69 , val , 0 , 1 )
#define REG_SET_5x62_0_8_SYNC_SEL(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x62 , val , 0 , 8 )


// vd_vi_chroma_set_seq4
#define REG_SET_0x5C_0_8_PAL_CM_OFF(ch, val) vd_register_set ( 0 , 0x00 , 0x5c + ch , val , 0 , 8 )
#define REG_SET_5x28_0_8_S_POINT(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x28 , val , 0 , 8 )
#define REG_SET_5x25_0_8_FSC_LOCK_MODE(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x25 , val , 0 , 8 )
#define REG_SET_5x90_0_8_COMB_MODE(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x90 , val , 0 , 8 )


// vd_vi_h_timing_set_seq5
#define REG_SET_0x68_0_8_H_DLY_LSB(ch, val) vd_register_set ( 0 , 0x00 , 0x68 + ch , val , 0 , 8 )
#define REG_SET_0x6c_0_8_H_DLY_MSB(ch, val) vd_register_set ( 0 , 0x00 , 0x6c + ch , val , 0 , 8 )
#define REG_SET_0x60_0_8_Y_DLY(ch, val) vd_register_set ( 0 , 0x00 , 0x60 + ch , val , 0 , 8 )
#define REG_SET_0x78_0_8_V_BLK_END_A(ch, val) vd_register_set ( 0 , 0x00 , 0x78 + ch , val , 0 , 8 )
#define REG_SET_5x38_4_1_H_MASK_ON(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x38 , val , 4 , 1 )
#define REG_SET_5x38_0_4_H_MASK_SEL(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x38 , val , 0 , 4 )
#define REG_SET_0x64_0_8_V_BLK_END_B(ch, val) vd_register_set ( 0 , 0x00 , 0x64 + ch , val , 0 , 8 )
#define REG_SET_0x14_4_1_FLD_INV(ch, val) vd_register_set ( 0 , 0x00 , 0x14 + ch , val , 4 , 1 )
#define REG_SET_5x64_0_8_MEM_RDP(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x64 , val , 0 , 8 )
#define REG_SET_5x47_0_8_SYNC_RS(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x47 , val , 0 , 8 )
#define REG_SET_5xA9_0_8_V_BLK_END_B(ch, val) vd_register_set ( 0 , 0x05 + ch , 0xa9 , val , 0 , 8 )


// vd_vi_h_scaler_mode_set_seq6
#define REG_SET_5x53_2_2_LINEMEM_MD(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x53 , val , 2 , 2 )
#define REG_SET_9x96_0_8_H_DOWN_SCALER(ch, val) vd_register_set ( 0 , 0x09 , 0x96 + (0x20 * ch) , val , 0 , 8 )
#define REG_SET_9x97_0_8_H_SCALER_MODE(ch, val) vd_register_set ( 0 , 0x09 , 0x97 + (0x20 * ch) , val , 0 , 8 )
#define REG_SET_9x98_0_8_REF_BASE_LSB(ch, val) vd_register_set ( 0 , 0x09 , 0x98 + (0x20 * ch) , val , 0 , 8 )
#define REG_SET_9x99_0_8_REF_BASE_MSB(ch, val) vd_register_set ( 0 , 0x09 , 0x99 + (0x20 * ch) , val , 0 , 8 )
#define REG_SET_9x9E_0_8_H_SCALER_OUTPUT_H_ACTIVE(ch, val) vd_register_set ( 0 , 0x09 , 0x9e + (0x20 * ch) , val , 0 , 8 )


//vd_vi_hpll_set_seq7
#define REG_SET_5x50_0_8_HPLL_MASK_ON(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x50 , val , 0 , 8 )
#define REG_SET_5xB8_0_8_HAFC_OP_MD(ch, val) vd_register_set ( 0 , 0x05 + ch , 0xb8 , val , 0 , 8 )
#define REG_SET_5xBB_0_8_HAFC_BYP_TH_E(ch, val) vd_register_set ( 0 , 0x05 + ch , 0xbb , val , 0 , 8 )
#define REG_SET_5xB7_0_8_HAFC_BYP_TH_S(ch, val) vd_register_set ( 0 , 0x05 + ch , 0xb7 , val , 0 , 8 )

// vd_vi_color_set_seq8
#define REG_SET_0x20_0_8_BRIGHTNESS(ch, val) vd_register_set ( 0 , 0x00 , 0x20 + ch , val , 0 , 8 )
#define REG_SET_0x24_0_8_CONTARST(ch, val) vd_register_set ( 0 , 0x00 , 0x24 + ch , val , 0 , 8 )
#define REG_SET_0x28_0_8_BLACK_LEVEL(ch, val) vd_register_set ( 0 , 0x00 , 0x28 + ch , val , 0 , 8 )
#define REG_SET_0x58_0_8_SATURATION_A(ch, val) vd_register_set ( 0 , 0x00 , 0x58 + ch , val , 0 , 8 )
#define REG_SET_0x40_0_8_HUE(ch, val) vd_register_set ( 0 , 0x00 , 0x40 + ch , val , 0 , 8 )
#define REG_SET_0x44_0_8_U_GAIN(ch, val) vd_register_set ( 0 , 0x00 , 0x44 + ch , val , 0 , 8 )
#define REG_SET_0x48_0_8_V_GAIN(ch, val) vd_register_set ( 0 , 0x00 , 0x48 + ch , val , 0 , 8 )
#define REG_SET_0x4C_0_8_U_OFFSET(ch, val) vd_register_set ( 0 , 0x00 , 0x4c + ch , val , 0 , 8 )
#define REG_SET_0x50_0_8_V_OFFSET(ch, val) vd_register_set ( 0 , 0x00 , 0x50 + ch , val , 0 , 8 )
#define REG_SET_5x2B_0_8_SATURATION_B(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x2b , val , 0 , 8 )
#define REG_SET_5x24_0_8_BURSET_DEC_A(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x24 , val , 0 , 8 )
#define REG_SET_5x5F_0_8_BURSET_DEC_B(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x5f , val , 0 , 8 )
#define REG_SET_5xD1_0_8_BURSET_DEC_C(ch, val) vd_register_set ( 0 , 0x05 + ch , 0xd1 , val , 0 , 8 )
#define REG_SET_9x44_0_8_FSC_EXT_EN(ch, val) vd_register_set ( 0 , 0x09 , 0x44 + ch , val , 0 , 8 )
#define REG_SET_9x50_0_8_FSC_EXT_VAL_7_0(ch, val) vd_register_set ( 0 , 0x09 , 0x50 + (ch*4) , val , 0 , 8 )
#define REG_SET_9x51_0_8_FSC_EXT_VAL_15_8(ch, val) vd_register_set ( 0 , 0x09 , 0x51 + (ch*4) , val , 0 , 8 )
#define REG_SET_9x52_0_8_FSC_EXT_VAL_23_16(ch, val) vd_register_set ( 0 , 0x09 , 0x52 + (ch*4) , val , 0 , 8 )
#define REG_SET_9x53_0_8_FSC_EXT_VAL_31_24(ch, val) vd_register_set ( 0 , 0x09 , 0x53 + (ch*4) , val , 0 , 8 )
#define REG_SET_5x26_0_8_FSC_LOCK_SENSE(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x26 , val , 0 , 8 )
#define REG_SET_5xB8_0_8_HPLL_MASK_END(ch, val) vd_register_set ( 0 , 0x05 + ch , 0xb8 , val , 0 , 8 )
#define REG_SET_9x40_0_8_FSC_DET_MODE(ch, val) vd_register_set ( 0 , 0x09 , 0x40 + ch , val , 0 , 8 )

// vd_vi_clock_set_seq9
#define REG_SET_1x84_0_8_CLK_ADC(ch, val) vd_register_set ( 0 , 0x01 , 0x84 + ch , val , 0 , 8 )
#define REG_SET_1x88_0_8_CLK_PRE(ch, val) vd_register_set ( 0 , 0x01 , 0x88 + ch , val , 0 , 8 )
#define REG_SET_1x8c_0_8_CLK_POST(ch, val) vd_register_set ( 0 , 0x01 , 0x8c + ch , val , 0 , 8 )

#define REG_SET_5x01_0_8_CML_MODE(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x01 , val , 0 , 8 )
#define REG_SET_5x05_0_8_AGC_OP(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x05 , val , 0 , 8 )
#define REG_SET_5x1D_0_8_G_SEL(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x1D , val , 0 , 8 )


// vd_jaguar1_sw_reset
#define REG_SET_1x81_0_1_VPLL_RST(ch, val) vd_register_set ( 0 , 0x01 , 0x81 , val , 0 , 1 )
#define REG_SET_1x80_0_1_VPLL_C(ch, val) vd_register_set ( 0 , 0x01 , 0x80 , val , 0 , 1 )


// __eq_base_set_value
#define REG_SET_5x65_0_8_EQ_BYPASS(ch, val) vd_register_set (   0, 0x05 + ch, 0x65 , val, 0, 8 )
#define REG_SET_5x58_0_8_EQ_BAND_SEL(ch, val) vd_register_set ( 0, 0x05 + ch, 0x58 , val, 0, 8 )
#define REG_SET_5x5C_0_8_EQ_GAIN_SEL(ch, val) vd_register_set ( 0 , 0x05 + ch , 0x5c , val , 0 , 8 )
#define REG_SET_Ax3D_0_8_EQ_DEQ_A_ON(ch, val) vd_register_set ( 0, 0x0a + ((ch%4)/2), 0x3d + (ch%2 * 0x80), val, 0 , 8 )
#define REG_SET_Ax3C_0_8_EQ_DEQ_A_SEL(ch, val) vd_register_set ( 0 , 0x0a + ((ch%4)/2) , 0x3c + (ch%2 * 0x80), val , 0 , 8 )
#define REG_SET_9x80_0_8_EQ_DEQ_B_SEL(ch, val) vd_register_set ( 0 , 0x09 , 0x80 + (ch * 0x20) , val , 0 , 8 )


// __eq_coeff_set_value
#define REG_SET_Ax30_0_8_EQ_DEQ_A_01(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x30 + (ch%2 * 0x80) , val , 0 , 8 )
#define REG_SET_Ax31_0_8_EQ_DEQ_A_02(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x31 + (ch%2 * 0x80) , val , 0 , 8 )
#define REG_SET_Ax32_0_8_EQ_DEQ_A_03(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x32 + (ch%2 * 0x80) , val , 0 , 8 )
#define REG_SET_Ax33_0_8_EQ_DEQ_A_04(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x33 + (ch%2 * 0x80) , val , 0 , 8 )
#define REG_SET_Ax34_0_8_EQ_DEQ_A_05(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x34 + (ch%2 * 0x80) , val , 0 , 8 )
#define REG_SET_Ax35_0_8_EQ_DEQ_A_06(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x35 + (ch%2 * 0x80) , val , 0 , 8 )
#define REG_SET_Ax36_0_8_EQ_DEQ_A_07(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x36 + (ch%2 * 0x80) , val , 0 , 8 )
#define REG_SET_Ax37_0_8_EQ_DEQ_A_08(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x37 + (ch%2 * 0x80) , val , 0 , 8 )
#define REG_SET_Ax38_0_8_EQ_DEQ_A_09(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x38 + (ch%2 * 0x80) , val , 0 , 8 )
#define REG_SET_Ax39_0_8_EQ_DEQ_A_10(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x39 + (ch%2 * 0x80) , val , 0 , 8 )
#define REG_SET_Ax3A_0_8_EQ_DEQ_A_11(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x3a + (ch%2 * 0x80) , val , 0 , 8 )
#define REG_SET_Ax3B_0_8_EQ_DEQ_A_12(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x3b + (ch%2 * 0x80) , val , 0 , 8 )


// __eq_color_set_value
#define REG_SET_0x24_0_8_EQ_COLOR_CONTRAST(ch, val) vd_register_set( 0, 0x00 , 0x24 + ch , val , 0 , 8 )
#define REG_SET_0x30_0_8_EQ_COLOR_H_PEAKING_1(ch, val) vd_register_set( 0, 0x00 , 0x30 + ch , val , 0 , 8 )
#define REG_SET_0x34_0_8_EQ_COLOR_H_PEAKING_2(ch, val) vd_register_set( 0, 0x00 , 0x34 + ch , val , 0 , 8 )
#define REG_SET_0x40_0_8_EQ_COLOR_HUE(ch, val) vd_register_set( 0, 0x00 , 0x40 + ch , val , 0 , 8 )
#define REG_SET_0x44_0_8_EQ_COLOR_U_GAIN(ch, val) vd_register_set( 0, 0x00 , 0x44 + ch , val , 0 , 8 )
#define REG_SET_0x48_0_8_EQ_COLOR_V_GAIN(ch, val) vd_register_set( 0, 0x00 , 0x48 + ch , val , 0 , 8 )
#define REG_SET_0x4C_0_8_EQ_COLOR_U_OFFSET(ch, val) vd_register_set( 0, 0x00 , 0x4c + ch , val , 0 , 8 )
#define REG_SET_0x50_0_8_EQ_COLOR_V_OFFSET(ch, val) vd_register_set( 0, 0x00 , 0x50 + ch , val , 0 , 8 )
#define REG_SET_0x28_0_8_EQ_COLOR_BLACK_LEVEL(ch, val) vd_register_set( 0, 0x00 , 0x28 + ch , val , 0 , 8 )
#define REG_SET_5x31_0_8_EQ_COLOR_C_FILTER(ch, val) vd_register_set( 0, 0x05 + ch , 0x31 , val , 0 , 8 )
#define REG_SET_5x27_0_8_EQ_COLOR_ACC_REF(ch, val) vd_register_set( 0, 0x05 + ch , 0x27 , val , 0 , 8 )
#define REG_SET_5x28_0_8_EQ_COLOR_CTI_DELAY(ch, val) vd_register_set( 0, 0x05 + ch , 0x28 , val , 0 , 8 )
#define REG_SET_5x2b_0_8_EQ_COLOR_SUB_SATURATION(ch, val) vd_register_set( 0, 0x05 + ch , 0x2b , val , 0 , 8 )
#define REG_SET_5x24_0_8_EQ_COLOR_BURST_DEC_A(ch, val) vd_register_set( 0, 0x05 + ch , 0x24 , val , 0 , 8 )
#define REG_SET_5x5F_0_8_EQ_COLOR_BURST_DEC_B(ch, val) vd_register_set( 0, 0x05 + ch , 0x5f , val , 0 , 8 )
#define REG_SET_5xD1_0_8_EQ_COLOR_BURST_DEC_C(ch, val) vd_register_set( 0, 0x05 + ch , 0xd1 , val , 0 , 8 )
#define REG_SET_5xD5_0_8_EQ_COLOR_C_OPTION(ch, val) vd_register_set( 0, 0x05 + ch , 0xd5 , val , 0 , 8 )
#define REG_SET_Ax25_0_8_EQ_COLOR_Y_FILTER_B(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x25 + (ch%2 * 0x80) , val , 0 , 8 )
#define REG_SET_Ax27_0_8_EQ_COLOR_Y_FILTER_B_SEL(ch, val) vd_register_set( 0, 0x0a + ((ch%4)/2) , 0x27 + (ch%2 * 0x80) , val , 0 , 8 )


// __eq_clk_set_value
#define REG_SET_1x84_0_8_EQ_CLOCK_ADC_CLK(ch, val) vd_register_set( 0, 0x01 , 0x84 + ch , val , 0 , 8 )
#define REG_SET_1x88_0_8_EQ_CLOCK_PRE_CLK(ch, val) vd_register_set( 0, 0x01 , 0x88 + ch , val , 0 , 8 )
#define REG_SET_1x8C_0_8_EQ_CLOCK_POST_CLK(ch, val) vd_register_set( 0, 0x01 , 0x8C + ch , val , 0 , 8 )


// eq_timing_b_set_value
#define REG_SET_9x96_0_8_EQ_TIMING_B_HSCALER_1(ch, val) vd_register_set( 0, 0x09 , 0x96 + (ch * 0x20) , val , 0 , 8 )
#define REG_SET_9x97_0_8_EQ_TIMING_B_HSCALER_2(ch, val) vd_register_set( 0, 0x09 , 0x97 + (ch * 0x20) , val , 0 , 8 )
#define REG_SET_9x98_0_8_EQ_TIMING_B_HSCALER_3(ch, val) vd_register_set( 0, 0x09 , 0x98 + (ch * 0x20) , val , 0 , 8 )
#define REG_SET_9x99_0_8_EQ_TIMING_B_HSCALER_4(ch, val) vd_register_set( 0, 0x09 , 0x99 + (ch * 0x20) , val , 0 , 8 )
#define REG_SET_9x9A_0_8_EQ_TIMING_B_HSCALER_5(ch, val) vd_register_set( 0, 0x09 , 0x9a + (ch * 0x20) , val , 0 , 8 )
#define REG_SET_9x9B_0_8_EQ_TIMING_B_HSCALER_6(ch, val) vd_register_set( 0, 0x09 , 0x9b + (ch * 0x20) , val , 0 , 8 )
#define REG_SET_9x9C_0_8_EQ_TIMING_B_HSCALER_7(ch, val) vd_register_set( 0, 0x09 , 0x9c + (ch * 0x20) , val , 0 , 8 )
#define REG_SET_9x9D_0_8_EQ_TIMING_B_HSCALER_8(ch, val) vd_register_set( 0, 0x09 , 0x9d + (ch * 0x20) , val , 0 , 8 )
#define REG_SET_9x9E_0_8_EQ_TIMING_B_HSCALER_9(ch, val) vd_register_set( 0, 0x09 , 0x9e + (ch * 0x20) , val , 0 , 8 )
#define REG_SET_9x40_0_8_EQ_TIMING_B_PN_AUTO(ch, val) vd_register_set( 0, 0x09 , 0x40 + ch , val , 0 , 8 )
#define REG_SET_5x90_0_8_EQ_TIMINING_B_COMB_MODE(ch, val) vd_register_set( 0, 0x05 + ch , 0x90 , val , 0 , 8 )
#define REG_SET_5xB9_0_8_EQ_TIMING_B_HPLL_OP_A(ch, val) vd_register_set( 0, 0x05 + ch , 0xb9 , val , 0 , 8 )
#define REG_SET_5x57_0_8_EQ_TIMING_B_MEM_PATH(ch, val) vd_register_set( 0, 0x05 + ch , 0x57 , val , 0 , 8 )
#define REG_SET_5x25_0_8_EQ_TIMING_B_FSC_LOCK_SPD(ch, val) vd_register_set( 0, 0x05 + ch , 0x25 , val , 0 , 8 )
#define REG_SET_0x04_0_8_EQ_TIMING_B_SD_MD(ch, val) vd_register_set( 0, 0x00 , 0x04 + ch , val , 0 , 8 )
#define REG_SET_0x08_0_8_EQ_TIMING_B_AHD_MD(ch, val) vd_register_set( 0, 0x00 , 0x08 + ch , val , 0 , 8 )
#define REG_SET_0x0C_0_8_EQ_TIMING_B_SPECIAL_MD(ch, val) vd_register_set( 0, 0x00 , 0x0c + ch , val , 0 , 8 )
#define REG_SET_0x78_0_8_EQ_TIMING_B_VBLK_END(ch, val) vd_register_set( 0, 0x00 , 0x78 + ch , val , 0 , 8 )


#define REG_SET_5x53_2_2_EQ_SD_LINE_MEM_MD(ch, val) vd_register_set( 0, 0x05 + ch , 0x53 , val , 2 , 2 )
#define REG_SET_0x14_4_1_EQ_SD_FLD_INV(ch, val) vd_register_set( 0, 0x00 , 0x14 + ch , val , 4 , 1 )
#define REG_SET_5x2F_7_1_EQ_SD_AUTO(ch, val) vd_register_set( 0, 0x05 + ch , 0x2f , val , 7 , 1 )
#define REG_SET_0x10_0_8_EQ_VIDEO_FORMAT(ch, val) vd_register_set( 0, 0x00 , 0x10 + ch , val , 0 , 8 )
#define REG_SET_5x64_0_8_EQ_MEM_RDP(ch, val) vd_register_set( 0, 0x05 + ch , 0x64 + ch , val , 0 , 8 )
#define REG_SET_5x69_0_1_EQ_SD_FREQ_SEL(ch, val) vd_register_set( 0, 0x05 + ch , 0x69 , val , 0 , 1 )

#define REG_SET_0x68_0_8_EQ_TIMING_A_H_DELAY_A(ch, val) vd_register_set( 0, 0x00 , 0x68 + ch , val , 0 , 8 )
#define REG_SET_5x38_0_8_EQ_TIMING_A_H_DELAY_B(ch, val) vd_register_set( 0, 0x05 + ch , 0x38 , val , 0 , 8 )
#define REG_SET_0x6C_0_4_EQ_TIMING_A_H_DELAY_C(ch, val) vd_register_set( 0, 0x00 , 0x6C + ch , val , 0 , 4 )
#define REG_SET_0x64_0_8_EQ_TIMING_A_Y_DELAY(ch, val) vd_register_set( 0, 0x00 , 0x64 + ch , val , 0 , 8 )

// ADD
#define REG_SET_0x7C_0_8_HZOOM(ch, val) vd_register_set( 0, 0x00 , 0x7c + ch , val , 0 , 8 )
#define REG_SET_5x31_0_8_EQ_C_FILTER(ch, val) vd_register_set( 0, 0x05 + ch , 0x31 , val , 0 , 8 )
#define REG_SET_0x5c_0_8_EQ_PAL_CM_OFF(ch, val) vd_register_set( 0, 0x00 , 0x5c + ch , val , 0 , 8 )

#define REG_SET_5x1D_0_8_EQ_AFE_G_SEL(ch, val) vd_register_set( 0, 0x05 + ch , 0x1d , val , 0 , 8 )
#define REG_SET_5x01_0_8_EQ_AFE_CTR_CLP(ch, val) vd_register_set( 0, 0x05 + ch , 0x01 , val , 0 , 8 )
#define REG_SET_5x05_0_8_EQ_D_AGC_OPTION(ch, val) vd_register_set( 0, 0x05 + ch , 0x05 , val , 0 , 8 )

#define REG_SET_0x70_0_8_V_DELAY(ch, val) vd_register_set( 0, 0x00 , 0x70 + ch , val , 0 , 8 )

#define REG_SET_0x14_0_8_FLD_INV_CHID(ch, val) vd_register_set( 0, 0x00, 0x14 + ch, val + ch, 0, 8)
#define REG_SET_0x34_0_8_Y_FIR_MODE(ch, val) vd_register_set( 0, 0x00, 0x34 + ch, val, 0, 8)
#define REG_SET_1xA0_0_8_TM_CLK_EN_SET(ch, val) vd_register_set( 0, 0x01, 0xA0 + ch, val, 0, 8 )
#define REG_SET_1xCC_0_8_VPORT_OCLK_SEL_VPORT_OVCLK_DLY_SEL(ch, val) vd_register_set( 0, 0x01, 0xCC + ch, val, 0, 8 )
#define REG_SET_5x21_0_8_CONT_SUB(ch, val) vd_register_set( 0, 0x05 + ch, 0x21, val, 0, 8 )
#define REG_SET_5x55_0_8_C_MEM_CLK_SEL(ch, val) vd_register_set( 0, 0x05 + ch, 0x55, val, 0, 8 )
#define REG_SET_5x56_0_8_FREQ_MEM_CLK_SEL(ch, val) vd_register_set( 0, 0x05 + ch, 0x56, val, 0, 8 )
#define REG_SET_5x57_0_8_LINE_MEM_CLK_INV(ch, val) vd_register_set( 0, 0x05 + ch, 0x57, val, 0, 8 )
#define REG_SET_5xB5_0_8_HAFC_MASK_SEL(ch, val) vd_register_set( 0, 0x05 + ch, 0xB5, val, 0, 8)
#define REG_SET_5xB8_0_8_HAFC_HCOEFF_SEL(ch, val) vd_register_set( 0, 0x05 + ch, 0xB8, val, 0, 8)

/********************************************************************
 *  End of file
 ********************************************************************/

#endif
