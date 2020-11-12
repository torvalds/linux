// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
 *
 *  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
 *  Module		: video_input.c
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
#include "jaguar1_video.h"
#include "jaguar1_video_eq.h"
#include "jaguar1_video_table.h"
#include "jaguar1_coax_protocol.h"
#include "jaguar1_reg_set_def.h"


static unsigned char cur_bank = 0xff;
static int print_flag = 0;

/**************************************************************************************
 * Jaguar1 Video Input initialize value get from table
 ***************************************************************************************/
static NC_VD_VI_Init_STR *__NC_VD_VI_Init_Val_Get( NC_VIVO_CH_FORMATDEF def )
{
	NC_VD_VI_Init_STR *pRet = &vd_vi_init_list[def];
	if( pRet == NULL )
	{
		printk("[DRV]vd_vi_init_list Not Supported format Yet!!!(%d)\n",def);
	}
	return  pRet;
}

static NC_VD_VO_Init_STR *__NC_VD_VO_Init_Val_Get( NC_VIVO_CH_FORMATDEF def )
{
	NC_VD_VO_Init_STR *pRet = &vd_vo_init_list[def];
	if( pRet == NULL )
	{
		printk("[DRV]vd_vo_init_list Not Supported format Yet!!!(%d)\n",def);
	}
	return  pRet;
}

/**************************************************************************************
 * Jaguar1 Register Setting Function
 *
 *
 ***************************************************************************************/
void reg_val_print_flag_set( int set )
{
	print_flag = set;
}

static int reg_val_print_flag_get( void )
{
	return print_flag;
}

void current_bank_set( unsigned char bank )
{
	cur_bank = bank;
}

unsigned char current_bank_get( void )
{
	return cur_bank;
}

void vd_register_set( int dev, unsigned char bank, unsigned char addr, unsigned char val, int pos, int size )
{
	unsigned char ReadVal = 0x00;
	unsigned char Mask = 0x00;
	unsigned char rstbit = 0x01;
	unsigned char WriteVal = val;
	unsigned char cur_bank = 0x00;
	int ii =0;

	if( 8 < (pos + size) )
	{
		printk("vd_register_set Error!!dev[%d] Bank[0x%02X] Addr[0x%02X] pos[%d] size[%d]\n", dev, bank, addr, pos, size);
	}

	// Current Bank Get
	cur_bank = current_bank_get();
	if( cur_bank != bank )
	{
		JAGUAR1_BANK_CHANGE(bank);
		current_bank_set(bank);
	}

	// If Data Size 8 Bit, Register Read Skip
	if( !(pos == 0 && size == 8) )
	{
		for(ii=0; ii<size; ii++)
		{
			Mask = Mask|(rstbit<<(pos+ii));
		}
		Mask = ~Mask;
		WriteVal = WriteVal<<pos;

		ReadVal = gpio_i2c_read(jaguar1_i2c_addr[dev], addr);
		ReadVal = ReadVal & Mask;
		WriteVal = WriteVal | ReadVal;
	}

	gpio_i2c_write(jaguar1_i2c_addr[dev], addr, WriteVal);

	if( reg_val_print_flag_get() )
		printk("[DRV]%Xx%02X > 0x%02X\n", current_bank_get(), addr, WriteVal);

}

/**************************************************************************************
 * Jaguar1 Video Input Setting Function
 *
 *
 ***************************************************************************************/
static void vd_vi_manual_set_seq1( unsigned char dev, unsigned char ch, void *p_param )
{
	/*====================================================================
	 * Bank 1x7c
	 *|   7   |   6   |   5   |    4   |   3        |   2        |   1        |   0        |
	 *|       |       |       |        | CLK_AUTO_4 | CLK_AUTO_3 | CLK_AUTO_2 | CLK_AUTO_1 |
	 *====================================================================*/
	/*====================================================================
	 * Bank 0x14
	 *|   7   |   6   |   5   |    4      |   3   |   2  |   1  |   0  |
	 *|       |       |       | FLD_INV_x |         CHID_VIN_x         |
	 *====================================================================*/
	/*====================================================================
	 * Bank 0x14
	 *|   7   |   6   |   5   |    4      |   3   |   2  |   1  |   0  |
	 *|       |       |       | FLD_INV_x |         CHID_VIN_x         |
	 *====================================================================*/
	/*====================================================================
	 * Bank 5x32
	 *|   7   |   6   |   5   |    4   |   3  |   2  |   1   |   0   |
	 *|       |       |  FLD_DET_MODE  |      |      |   NOVID_DET_A |
	 *====================================================================*/
	/*====================================================================
	 * Bank 13x30 ~ 33  - SK_ing
	 *|   7   |   6   |   5   |   4   |   3   |   2   |   1   |   0   |
	 *|       |       |det_en |det_en |det_en |det_en |det_en |det_en |
	 *====================================================================*/
	/*====================================================================
	 * Bank 9x44
	 *|   7   |   6   |   5   |   4   |   3   |   2   |   1   |   0   		|
	 *|       |       | 	  |		  |		  |		  |		  |FSC_EXT_EN_1 |
	 *====================================================================*/
	NC_VD_VI_Init_STR *param = (NC_VD_VI_Init_STR*)p_param;
	unsigned char val_13x30;
	unsigned char val_13x31;
	unsigned char val_13x32;

	if(ch == 0)
		REG_SET_1x7C_0_1_clk_auto_1( ch, 0x0 );
	else if(ch ==1)
		REG_SET_1x7C_1_1_clk_auto_2( ch, 0x0 );
	else if(ch ==2)
		REG_SET_1x7C_2_1_clk_auto_3( ch, 0x0 );
	else if(ch ==3)
		REG_SET_1x7C_3_1_clk_auto_4( ch, 0x0 );
	else
		printk("[DRV]Clock Auto Set Fail!!:: %x\n", ch);

	REG_SET_5x32_0_8_NOVIDEO_DET_A( ch, 0x10 );
	REG_SET_5xB9_0_8_HAFC_LPF_SEL( ch, 0xb2 );

	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xFF, 0x13);
	val_13x30 = gpio_i2c_read(jaguar1_i2c_addr[dev], 0x30);
	val_13x31 = gpio_i2c_read(jaguar1_i2c_addr[dev], 0x31);
	val_13x32 = gpio_i2c_read(jaguar1_i2c_addr[dev], 0x32);

	val_13x30 &= (~(1 << (ch + 4)) & (~(1 << ch)));
	val_13x31 &= (~(1 << (ch + 4)) & (~(1 << ch)));
	val_13x32 &= (~(1 << ch));

	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x30, val_13x30);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x31, val_13x31);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x32, val_13x32);

	REG_SET_9x44_0_8_FSC_EXT_EN( ch, 0x00 );
	REG_SET_5x6E_0_8_VBLK_END_SEL( ch, param->vblk_end_sel );
	REG_SET_5x6F_0_8_VBLK_END_EXT( ch, param->vblk_end_ext );

}

static void vd_vi_vafe_set_seq2( unsigned char dev, unsigned char ch )
{
	REG_SET_5x00_0_8_A_CMP_PW_MODE( ch, 0xd0 );
	REG_SET_5x02_0_8_A_CMP_TIMEUNIT( ch, 0x0c );
	REG_SET_5x1E_0_8_VAFEMD( ch, 0x00 );
	REG_SET_5x58_0_8_VAFE1_EQ_BAND_SEL( ch, 0x00 );
	REG_SET_5x59_0_8_LPF_BYPASS( ch, 0x00 );
	REG_SET_5x5A_0_8_VAFE_IMP_CNT( ch, 0x00 );
	REG_SET_5x5B_0_8_VAFE_DUTY( ch, 0x41 );
	REG_SET_5x5C_0_8_VAFE_B_LPF_SEL( ch, 0x78 );
	REG_SET_5x94_0_8_PWM_DELAY_H( ch, 0x00 );
	REG_SET_5x95_0_8_PWM_DELAY_L( ch, 0x00 );
	REG_SET_5x65_0_8_VAFE_CML_SPEED( ch, 0x80 );

}

static void vd_vi_format_set_seq3( unsigned char dev, unsigned char ch, void *p_param )
{
	/*============================================================================================
	 * Bank 0x10
	 *|   7   |   6   |   5   |   4   |   3  |  2  |   1  |  0  |
	 *|       |   BSF_MODE_1  |           VIDEO_FORMAT_1        |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 0x0c
	 *|   7   |   6   |   5   |   4   |   3  |  2  |   1  |  0  |
	 *|       |       |       |       |     SPECIAL_MODE        |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 0x04
	 *|   7   |   6   |   5   |   4   |   3  |  2  |   1  |  0  |
	 *|       |       |       |       |           SD_MD         |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 0x08
	 *|   7   |   6   |   5   |   4   |   3  |  2  |   1  |  0  |
	 *|       |       |       |       |           AHD_MD        |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5x69
	 *|   7          |   6   |         5         |   4    |   3  |  2  |   1  |      0      |
	 *| NO_VIDEO_OFF |       | OUTPUT PATTERN_ON | MEM_EN |      |     |      | SD_FREQ_SEL |
	 *============================================================================================*/
	NC_VD_VI_Init_STR *param = (NC_VD_VI_Init_STR*)p_param;

	if(ch>3)
	{
		printk("[DRV] %s CHID Error\n", __func__);
		return;
	}

	REG_SET_0x10_0_8_VD_FMT( ch, param->video_format );
	REG_SET_0x0C_0_8_SPL_MODE( ch, param->spl_mode );
	REG_SET_0x04_0_8_SD_MODE( ch, param->sd_mode );
	REG_SET_0x08_0_8_AHD_MODE( ch, param->ahd_mode );
	REG_SET_5x69_0_1_SD_FREQ_SEL( ch, param->sd_freq_sel );
	REG_SET_5x62_0_8_SYNC_SEL( ch, param->sync_sel );

}

static void vd_vi_chroma_set_seq4( unsigned char dev, unsigned char ch, void *p_param )
{
	/*============================================================================================
	 * Bank 0x5c
	 *|   7        |   6   |   5   |     4    |  3  |  2  |   1  |  0  |
	 *| PAL_CM_OFF |       |       | COLOROFF |           C_KILL       |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5x28
	 *|      7        |    6    |   5    |    4   |  3  |  2  |   1  |  0  |
	 *| CTI_CORE_MODE | S_POINT |   CTI_DELAY_SEL |     |     |      |     |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5x25
	 *|  7  |  6  |  5  |  4  |  3  |  2  |   1  |  0  |
	 *|      FSC_LOCK_MODE    |      FSC_LOCK_SPD      |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5x90
	 *|     7      |  6  |   5   |   4   |  3  |  2  |  1  |  0  |
	 *| C_LH_SEL_1 |     |    YL_SEL_1   |      COMB_MODE_1      |
	 *============================================================================================*/
	NC_VD_VI_Init_STR *param = (NC_VD_VI_Init_STR*)p_param;

	if(ch>3)
	{
		printk("[DRV] %s CHID Error\n", __func__);
		return;
	}

	REG_SET_0x5C_0_8_PAL_CM_OFF( ch, param->pal_cm_off );
	REG_SET_5x28_0_8_S_POINT( ch, param->s_point );
	REG_SET_5x25_0_8_FSC_LOCK_MODE( ch, param->fsc_lock_mode );
	REG_SET_5x90_0_8_COMB_MODE( ch, param->comb_mode );

}

static void vd_vi_h_timing_set_seq5( unsigned char dev, unsigned char ch, void *p_param )
{
	/*============================================================================================
	 * Bank 0x68
	 *|  7  |  6  |  5  |  4  |  3  |  2  |   1  |  0  |
	 *|                     H_DELAY                    |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 0x60
	 *|  7  |  6  |  5  |  4  |  3  |  2  |   1  |  0  |
	 *|                 |            Y_DELAY           |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 0x78
	 *|  7  |  6  |  5  |  4  |  3  |  2  |   1  |  0  |
	 *|                      VBLK_END                  |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5x38
	 *|  7  |  6  |  5  |    4    |  3   |   2  |    1  |  0   |
	 *|                 | MASK_ON | MASK_SEL1 (Bank0 0x8E[3:0) |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 0x64
	 *|  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 *|       DF_CDELAY       |       DF_YDELAY       |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 0x14
	 *|  7  |  6  |  5  |  4      |  3  |  2  |  1  |  0  |
	 *|                 | FLD_INV |       CHID_VIN        |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5x64
	 *|  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 *|     |     |     |     |       MEM_RDP_01      |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5x47
	 *|  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 *|                 CONTROL_MODES                 |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5xa9
	 *|  7                    |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 *| SIGNED_ADV_STP_DELAY1 |             ADV_STP_DELAY1              |
	 *============================================================================================*/
	NC_VD_VI_Init_STR *param = (NC_VD_VI_Init_STR*)p_param;

	if(ch>3)
	{
		printk("[DRV] %s CHID Error\n", __func__);
		return;
	}

	REG_SET_0x68_0_8_H_DLY_LSB( ch, param->h_delay_lsb );
	REG_SET_0x6c_0_8_H_DLY_MSB( ch, param->h_dly_msb);
	REG_SET_0x60_0_8_Y_DLY( ch, param->y_delay );
	REG_SET_0x78_0_8_V_BLK_END_A( ch, param->v_blk_end_a );

	REG_SET_5x38_4_1_H_MASK_ON( ch, param->h_mask_on );
	REG_SET_5x38_0_4_H_MASK_SEL( ch, param->h_mask_sel );

	REG_SET_0x64_0_8_V_BLK_END_B( ch, param->v_blk_end_b );
	REG_SET_0x14_4_1_FLD_INV( ch, param->fld_inv );

	REG_SET_5x64_0_8_MEM_RDP( ch, param->mem_rdp );
	REG_SET_5x47_0_8_SYNC_RS( ch, param->sync_rs );
	REG_SET_5xA9_0_8_V_BLK_END_B( ch, param->v_blk_end_b );

}

static void vd_vi_h_scaler_mode_set_seq6( unsigned char dev, unsigned char ch, void *p_param )
{
	/*============================================================================================
	 * Bank 5x53
	 *|  7  |  6  |  5             |  4         |  3  |  2   |  1  |  0          |
	 *|     |     | PROTECTION_OFF | BT_601_SEL | LINEMEM_MD |     | C_DITHER_ON |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 9x96
	 *|  7  |  6  |  5  |  4                    |  3  |  2  |  1                   |  0                  |
	 *|     |     |     | CH1_H_DOWN_SCALER_EN  |     |     | CH1_H_SCALER_TRS_SEL | CH1_H_SCALER_ENABLE |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 9x97
	 *|  7  |  6  |  5  |  4   |  3       |  2        |  1                      |  0                |
	 *|     CH1_H_SCALER_MODE  | CH1_H_SCALER_RD_MODE | CH1_H_SCALER_AUTO_H_REF | CH1_H_SCALER_AUTO |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 9x98
	 *|  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 *|          CH1_H_SCALER_H_REF_BASE[7:0]         |
	 * Bank 9x99
	 *|  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 *|          CH1_H_SCALER_H_REF_BASE[15:8]        |
	 *============================================================================================*/

	NC_VD_VI_Init_STR *param = (NC_VD_VI_Init_STR*)p_param;

	if(ch>3)
	{
		printk("[DRV] %s CHID Error\n", __func__);
		return;
	}

	REG_SET_5x53_2_2_LINEMEM_MD( ch, param->line_mem_mode );

	REG_SET_9x96_0_8_H_DOWN_SCALER( ch, param->h_down_scaler );
	REG_SET_9x97_0_8_H_SCALER_MODE( ch, param->h_scaler_mode );
	REG_SET_9x98_0_8_REF_BASE_LSB( ch, param->ref_base_lsb );
	REG_SET_9x99_0_8_REF_BASE_MSB( ch, param->ref_base_msb );
	REG_SET_9x9E_0_8_H_SCALER_OUTPUT_H_ACTIVE( ch, param->h_scaler_active );
}

static void vd_vi_hpll_set_seq7( unsigned char dev, unsigned char ch, void *p_param )
{
	/*============================================================================================
	 * Bank 5x50
	 *|  7  |  6               |  5  |  4                |  3      |  2       |  1           |  0       |
	 *|     | NCO_GDF_COEFF_IV |     | NCO_GDF_COEFF_OFF | Y_TEMP_SEL(5T,15T) | HPLL_MASK_ON | CONT_SUB |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5xb8
	 *|  7          |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 *| HAFC_BYPASS | HAFC_HCOEFF_SEL |       HAFC_OP_MD      |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5xbb
	 *|  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 *|                 HPLL_MASK_END                 |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5xbb
	 *|  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 *|            HAFC_BYP_TH_S(write)               |
	 *============================================================================================*/
	NC_VD_VI_Init_STR *param = (NC_VD_VI_Init_STR*)p_param;

	if(ch>3)
	{
		printk("[DRV] %s CHID Error\n", __func__);
		return;
	}

	REG_SET_5x50_0_8_HPLL_MASK_ON( ch, param->hpll_mask_on );
	REG_SET_5xB8_0_8_HAFC_OP_MD( ch, param->hafc_op_md );
	REG_SET_5xBB_0_8_HAFC_BYP_TH_E( ch, param->hafc_byp_th_e );
	REG_SET_5xB7_0_8_HAFC_BYP_TH_S( ch, param->hafc_byp_th_s );

}

static void vd_vi_color_set_seq8( unsigned char dev, unsigned char ch, void *p_param, NC_VIVO_CH_FORMATDEF fmt )
{
	/*============================================================================================
	 * gpio_i2c_write(jaguar1_i2c_addr[dev], 0x22 + (ch*4), 0x0B ); // Raptor3
	 * Bank 0x5c
	 *|  7         |  6  |  5  |   4      |  3  |  2  |  1  |  0  |
	 *| PAL_CM_OFF |     |     | COLOROFF |         C_KILL        |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5x26
	 *|  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 *|                 FSC_LOCK_SENSE                |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 5xb8
	 *|  7          |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 *| HAFC_BYPASS | HAFC_HCOEFF_SEL |       HAFC_OP_MD      |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 9x40
	 *|  7        |   6      |  5       |    4     |    3     |     2       |  1  |  0       |
	 *| FSC_DET_  | FSC_DET_ | FSC_DET_ | FSC_DET_ | FSC_DET_ |   FSC_DET_  |     | FSC_RST_ |
	 *| AUTO_RST1 | UNLIM1   | AUTO1    | PRESET1  | MODE1    | REFER_AUTO1 |     | STRB1    |
	 *============================================================================================*/

	NC_VD_VI_Init_STR *param = (NC_VD_VI_Init_STR*)p_param;

	REG_SET_0x20_0_8_BRIGHTNESS( ch, param->brightnees );
	REG_SET_0x24_0_8_CONTARST( ch, param->contrast );
	REG_SET_0x28_0_8_BLACK_LEVEL( ch, param->black_level );
	REG_SET_0x58_0_8_SATURATION_A( ch, param->saturation_a );
	REG_SET_0x40_0_8_HUE( ch, param->hue );
	REG_SET_0x44_0_8_U_GAIN( ch, param->u_gain );
	REG_SET_0x48_0_8_V_GAIN( ch, param->v_gain );
	REG_SET_0x4C_0_8_U_OFFSET( ch, param->u_offset );
	REG_SET_0x50_0_8_V_OFFSET( ch, param->v_offset );
	REG_SET_5x2B_0_8_SATURATION_B( ch, param->saturation_b );
	REG_SET_5x24_0_8_BURSET_DEC_A( ch, param->burst_dec_a );
	REG_SET_5x5F_0_8_BURSET_DEC_B( ch, param->burst_dec_b );
	REG_SET_5xD1_0_8_BURSET_DEC_C( ch, param->burst_dec_c );

	REG_SET_9x44_0_8_FSC_EXT_EN( ch, 0x00 );
	REG_SET_9x50_0_8_FSC_EXT_VAL_7_0( ch, 0x30 );
	REG_SET_9x51_0_8_FSC_EXT_VAL_15_8( ch, 0x6f );
	REG_SET_9x52_0_8_FSC_EXT_VAL_23_16( ch, 0x67 );
	REG_SET_9x53_0_8_FSC_EXT_VAL_31_24( ch, 0x48 );

	if(fmt == TVI_5M_12_5P)
	{
		REG_SET_5x26_0_8_FSC_LOCK_SENSE( ch, 0x20 );
	}
	else
		REG_SET_5x26_0_8_FSC_LOCK_SENSE( ch, 0x40 );

	if(fmt == AHD20_SD_H960_2EX_Btype_NT || fmt == AHD20_SD_H960_2EX_Btype_PAL)
	{
		REG_SET_5xB8_0_8_HPLL_MASK_END( ch, 0xb8 );
		REG_SET_9x40_0_8_FSC_DET_MODE( ch, 0x00);
	}
	else
	{
		REG_SET_5xB8_0_8_HPLL_MASK_END( ch, 0x39 );
		REG_SET_9x40_0_8_FSC_DET_MODE( ch, 0x00 );

		gpio_i2c_write(jaguar1_i2c_addr[dev], 0xff, 0x05 + ch);
		gpio_i2c_write(jaguar1_i2c_addr[dev], 0xb5, 0x80);  // HPLL Locking Ref. Range
	}

}

static void vd_vi_clock_set_seq9( unsigned char dev, unsigned char ch, void *p_param )
{
	/*============================================================================================
	 * Bank 1x84
	 *|  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 *|   VADC_CLK1_DLY_SEL   |     VADC_CLK1_SEL     |
	 *============================================================================================*/
	/*============================================================================================
	 * Bank 1x88
	 *|  7  |  6  |  5  |  4  |  3  |  2  |   1   |   0   |
	 *|     |     |     |     |     |     |  DEC_PRECLK   |
	 * Bank 1x8c
	 *|  7  |  6  |  5  |  4  |  3  |  2  |   1   |   0   |
	 *|     |     |     |     |     |     |  DEC_POSTCLK  |
	 *============================================================================================*/
	/*============================================================================================
	 * ADC -> PRE -> POST -> VCLK
	 * ADC_CLK 1x84[3:0]
	 * 0 ~ 3 : 37.125 MHz
	 * 4 ~ 5 : 74.25 MHz
	 * 8 ~ 9 : 148.5 MHz
	 * Pre_Clock 1x88 / Post Clock 1x8C
	 * 0 : 37.125
	 * 1 : 74.25
	 * 2 : 148.5
	 * VCLK 1xCC[7:4]
	 * 4 ~ 5 : 74.25 MHz
	 * 6 ~ 7 : 148.5 MHz
	 *============================================================================================*/

	NC_VD_VI_Init_STR *param = (NC_VD_VI_Init_STR*)p_param;

	REG_SET_1x84_0_8_CLK_ADC( ch, param->clk_adc );
	REG_SET_1x88_0_8_CLK_PRE( ch, param->clk_pre );
	REG_SET_1x8c_0_8_CLK_POST( ch, param->clk_post );

	REG_SET_5x01_0_8_CML_MODE( ch, param->cml_mode );
	REG_SET_5x05_0_8_AGC_OP( ch, param->agc_op );
	REG_SET_5x1D_0_8_G_SEL( ch, param->g_sel );

}

//==================================================================================================================

/**************************************************************************************
 * Jaguar1 Video Output Setting Function
 *
 *
 ***************************************************************************************/
void vd_vo_seq_set( unsigned char dev, unsigned char ch, void *p_param )
{
	/*
	 * BT656 or BT1120 Set????...
	 * */
	NC_VD_VO_Init_STR *param = (NC_VD_VO_Init_STR*)p_param;

	// BANK 1
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xFF, 0x01);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xc0 + (ch * 0x02), param->port_seq_ch01[ch]);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xc1 + (ch * 0x02), param->port_seq_ch23[ch]);

}

static void vd_vo_output_seq_set( unsigned char dev, unsigned char port, unsigned char out_ch )
{
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xFF, 0x01);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xc0 + (port * 0x02), out_ch);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xc1 + (port * 0x02), out_ch);
}

static void vd_vo_port_y_c_merge_set( unsigned char dev, unsigned char ch, void *p_param)
{
	NC_VD_VO_Init_STR *param = (NC_VD_VO_Init_STR*)p_param;

	/*============================================================================================
	 * Address: 1xec
	 *|  7  |  6  |  5  |  4  |  3  |  2  |  1  |        0      |
	 *|     |     |     |     |     |     |     | MUX_YC_MERGE1 |
	 *============================================================================================*/
	REG_SET_1xEC_0_8_yc_merge( ch, param->mux_yc_merge );

}

static void vd_vo_port_ch_id_set( unsigned char dev, unsigned char ch, void *p_param )
{
	NC_VD_VO_Init_STR *param = (NC_VD_VO_Init_STR*)p_param;
	unsigned char val_0x14 = 0x00;

	/*============================================================================================
	 * Address: 0x14
	 *|  7  |  6  |  5  |      4    |  3  |  2  |  1  |  0  |
	 *|     |     |     | FLD_INV_1 |       CHID_VIN1       |
	 *============================================================================================*/
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xFF, 0x00);
	val_0x14 = gpio_i2c_read(jaguar1_i2c_addr[dev], 0x14 + ch);
	val_0x14 = val_0x14 & 0x10;
	val_0x14 = val_0x14 | param->chid_vin;
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x14 + ch, val_0x14);

}

static void vd_vo_mux_mode_set( unsigned char dev, unsigned char ch, void *p_param )
{
	NC_VD_VO_Init_STR *param = (NC_VD_VO_Init_STR*)p_param;

	/*============================================================================================
	 * Address: 1xc8
	 *|  7  |  6  |     5     |     4    |  3  |  2  |  1  |  0  |
	 *|     |     | VCLK_1_EN | VDO_1_EN |   VPORT_1_CH_OUT_SEL  |
	 *============================================================================================*/
	REG_SET_1xC8_0_8_out_sel( ch , param->vport_out_sel );

}

static void vd_vo_manual_mode_set(unsigned char dev, unsigned char ch, void *p_param )
{
	//NC_VD_VO_Init_STR *param = (NC_VD_VO_Init_STR*)p_param;

	unsigned char val_0x30;
	unsigned char val_0x31;
	unsigned char val_0x32;

	/*============================================================================================
	 * Address: 13x30
	 *|  7  |  6  |  5  |  4  |             3            |  2  |  1  |  0  |
	 *|     |     |     |     | NOVIDEO_VFC_INIT_EN[3:0] |     |     |     |
	 *============================================================================================*/
	/*============================================================================================
	 * Address: 13x31
	 *|  7  |  6  |       5       |        4      |       3       |       2       |       1       |        0      |
	 *|     |     | AHD_8M_det_en | AHD_5M_det_en | AHD_4M_det_en | AHD_3M_det_en | AHD_2M_det_en | AHD_1M_det_en |
	 *============================================================================================*/
	/*============================================================================================
	 * Address: 13x32
	 *|  7  |  6  |       5       |        4      |       3       |       2       |       1       |        0      |
	 *|     |     | CVI_8M_det_en | CVI_5M_det_en | CVI_4M_det_en | CVI_3M_det_en | CVI_2M_det_en | CVI_1M_det_en |
	 *============================================================================================*/

	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xFF, 0x13);
	val_0x30 = gpio_i2c_read(jaguar1_i2c_addr[dev], 0x30);
	val_0x31 = gpio_i2c_read(jaguar1_i2c_addr[dev], 0x31);
	val_0x32 = gpio_i2c_read(jaguar1_i2c_addr[dev], 0x32);

	val_0x30 &= (~(1 << (ch + 4)) & (~(1 << ch)));
	val_0x31 &= (~(1 << (ch + 4)) & (~(1 << ch)));
	val_0x32 &= (~(1 << ch));

	// 0x00 Set Test
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x30, val_0x30);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x31, val_0x31);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x32, val_0x32);

}

static void vd_jaguar1_single_differ_set( unsigned char dev, unsigned char ch, int input )
{
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
		//REG_SET_5x1D_0_8_AFE( ch, 0x00 );
		REG_SET_5x92_0_8_PWM( ch, 0x00 );
	}
	else
	{
		printk("Jaguar1 Analog Input Setting Fail !!!\n");
	}

}

static void vd_jaguar1_960p_30P_test_set( unsigned char dev, unsigned char ch )
{
	printk("[drv]vd_jaguar1_960p_30P_test_set >>> ch%d!!\n", ch);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xff, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x68 + ch, 0x4E);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x69 + ch, 0x80);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x6a + ch, 0x80);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x6b + ch, 0x80);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x04 + ch, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x08 + ch, 0x02);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x0c + ch, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x18 + ch, 0x01);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x64 + ch, 0x06);

	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xff, 0x01);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x84 + ch, 0x04);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x88 + ch, 0x01);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x8c + ch, 0x02);

	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xff, 0x05 + ch);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x6e, 0x10);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x6f, 0x82);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x76, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x77, 0x80);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x78, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x79, 0x11);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xB5, 0x80);

	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xff, 0x11);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x00 + ( ch * 0x20 ), 0x0f);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x01 + ( ch * 0x20 ), 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x02 + ( ch * 0x20 ), 0x9d);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x03 + ( ch * 0x20 ), 0x05);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x04 + ( ch * 0x20 ), 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x05 + ( ch * 0x20 ), 0x08);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x06 + ( ch * 0x20 ), 0xca);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x0a + ( ch * 0x20 ), 0x03);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x0b + ( ch * 0x20 ), 0xc0);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x0c + ( ch * 0x20 ), 0x04);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x0d + ( ch * 0x20 ), 0x4b);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x10 + ( ch * 0x20 ), 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x11 + ( ch * 0x20 ), 0x96);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x12 + ( ch * 0x20 ), 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x13 + ( ch * 0x20 ), 0x82);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x14 + ( ch * 0x20 ), 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x15 + ( ch * 0x20 ), 0x30);

}

static void vd_jaguar1_960p_25P_test_set( unsigned char dev, unsigned char ch )
{
	printk("[drv]vd_jaguar1_960p_25P_test_set >>> ch%d!!\n", ch);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xff, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x68 + ch, 0x59);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x69 + ch, 0x80);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x6a + ch, 0x80);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x6b + ch, 0x80);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x04 + ch, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x08 + ch, 0x03);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x0c + ch, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x18 + ch, 0x01);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x64 + ch, 0x06);

	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xff, 0x01);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x84 + ch, 0x04);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x88 + ch, 0x01);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x8c + ch, 0x02);

	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xff, 0x05 + ch);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x6e, 0x10);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x6f, 0x82);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x76, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x77, 0x80);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x78, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x79, 0x11);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xB5, 0x80);

	// Only AHD20_720P_960P_25P
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xff, 0x09);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x53 + (ch * 0x04), 0x52);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x52 + (ch * 0x04), 0xd2);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x51 + (ch * 0x04), 0x1c);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x50 + (ch * 0x04), 0x10);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x44 + ch, 0x01);

	gpio_i2c_write(jaguar1_i2c_addr[dev], 0xff, 0x11);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x00 + ( ch * 0x20 ), 0x0f);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x01 + ( ch * 0x20 ), 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x02 + ( ch * 0x20 ), 0x97);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x03 + ( ch * 0x20 ), 0x05);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x04 + ( ch * 0x20 ), 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x05 + ( ch * 0x20 ), 0x0a);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x06 + ( ch * 0x20 ), 0x8c);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x0a + ( ch * 0x20 ), 0x03);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x0b + ( ch * 0x20 ), 0xc0);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x0c + ( ch * 0x20 ), 0x04);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x0d + ( ch * 0x20 ), 0x4c);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x10 + ( ch * 0x20 ), 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x11 + ( ch * 0x20 ), 0x96);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x12 + ( ch * 0x20 ), 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x13 + ( ch * 0x20 ), 0x82);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x14 + ( ch * 0x20 ), 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev], 0x15 + ( ch * 0x20 ), 0x30);

}

/*****************************************************************************************************************************************
 * Jaguar1 Video ioctl function
 * video vi_vo initialize
 *
 ******************************************************************************************************************************************/
void vd_jaguar1_vo_ch_seq_set( void *p_param)
{
	video_output_init *vo_seq = (video_output_init*)p_param;
	unsigned char dev = 0;
	unsigned char port   = vo_seq->port;
	unsigned char out_ch = vo_seq->out_ch;

	vd_vo_output_seq_set( dev, port, out_ch );
}

void vd_jaguar1_init_set( void *p_param )
{
	video_input_init *video_init = (video_input_init*)p_param;
	unsigned char ch  = video_init->ch % 4;
	unsigned char fmt = video_init->format;
	int analog_input  = video_init->input;

	video_equalizer_info_s eq_set;
	NC_VD_COAX_STR coax_init;
	NC_VD_VI_Init_STR *vi_param;
	NC_VD_VO_Init_STR *vo_param;

	int dev =  ch / 4 ; //{0x64, 0x60, 0x62, 0x66}//

	vi_param = __NC_VD_VI_Init_Val_Get(fmt);
	vo_param = __NC_VD_VO_Init_Val_Get(AHD20_1080P_30P);

	// Each_Mode_Set
	REG_SET_0x00_0_8_EACH_SET(ch, 0x10);
	/*=====================================================
	 * vd_Analog Input Setting
	 *=====================================================*/
	vd_jaguar1_single_differ_set(dev, ch, analog_input);

	/*=====================================================
	 * vd_vo Setting
	 *=====================================================*/
	vd_vo_port_y_c_merge_set( dev, ch, vo_param );
	vd_vo_mux_mode_set( dev, ch, vo_param );
	vd_vo_manual_mode_set(dev, ch, vo_param);

	/*=====================================================
	 * vd_vi Setting
	 *=====================================================*/

	vd_vi_manual_set_seq1( dev, ch, vi_param );
	vd_vi_vafe_set_seq2( dev, ch );
	vd_vi_format_set_seq3( dev, ch, vi_param );
	vd_vi_chroma_set_seq4( dev, ch, vi_param );
	vd_vi_h_timing_set_seq5( dev, ch, vi_param );
	vd_vi_h_scaler_mode_set_seq6( dev, ch, vi_param );

	vd_vi_hpll_set_seq7( dev, ch, vi_param );
	vd_vi_color_set_seq8( dev, ch, vi_param, fmt);
	vd_vo_port_ch_id_set( dev, ch, vo_param );
	vd_vi_clock_set_seq9( dev, ch, vi_param );

	/*=====================================================
	 * AHD 1280x960P Test
	 *
	 *=====================================================*/
	if( fmt == AHD20_720P_960P_30P )
	{
		vd_jaguar1_960p_30P_test_set( 0, ch);
		current_bank_set(0xFF);
	}
	else if( fmt == AHD20_720P_960P_25P)
	{
		vd_jaguar1_960p_25P_test_set( 0, ch);
		current_bank_set(0xFF);
	}
	else if( fmt == AHD20_SD_H960_2EX_Btype_PAL )
	{
		REG_SET_0x70_0_8_V_DELAY( ch, 0x3F );
	}
	else if( fmt == AHD20_SD_SH720_PAL || fmt == AHD20_SD_SH720_NT || fmt == AHD20_SD_H1440_PAL || fmt == AHD20_SD_H1440_NT )
	{
		REG_SET_0x14_0_8_FLD_INV_CHID(ch, 0x00);
		REG_SET_0x34_0_8_Y_FIR_MODE(ch, 0x00);
		REG_SET_1xCC_0_8_VPORT_OCLK_SEL_VPORT_OVCLK_DLY_SEL(ch, 0x40);
		REG_SET_1xA0_0_8_TM_CLK_EN_SET(ch, 0x10);
		REG_SET_5x21_0_8_CONT_SUB(ch, 0x24);
		REG_SET_5x55_0_8_C_MEM_CLK_SEL(ch, 0x00);
		REG_SET_5x56_0_8_FREQ_MEM_CLK_SEL(ch, 0x00);
		REG_SET_5x57_0_8_LINE_MEM_CLK_INV(ch, 0x00);
		REG_SET_5xB5_0_8_HAFC_MASK_SEL(ch, 0x00);
		REG_SET_5xB8_0_8_HAFC_HCOEFF_SEL(ch, 0x39);
		REG_SET_0x7C_0_8_HZOOM(ch, 0x8F);
	}
	else
		printk("\n");

	printk("[drv_vi]ch::%d >>> fmt::%s\n", ch, vi_param->name);

	/*=====================================================
	 * EQ Stage 0 Setting
	 *
	 *=====================================================*/
#if 1
	eq_set.Ch     = ch;
	eq_set.FmtDef = fmt;
	eq_set.Cable  = CABLE_A;
	eq_set.Input  = SINGLE_ENDED;
	eq_set.stage  = STAGE_0;
	video_input_eq_val_set( &eq_set );
#endif

	printk("[drv_vi]ch::%d >>> fmt::%s\n", ch, vi_param->name);
	current_bank_set(0xFF);

	/*=====================================================
	 * Coaxial Initialize
	 *
	 *=====================================================*/
	coax_init.ch       = ch;
	coax_init.vivo_fmt = fmt;
	coax_init.vd_dev   = dev;
	coax_tx_init( &coax_init );
	if(acp_mode_enable == 0)
		coax_tx_16bit_init( &coax_init );
	coax_rx_init( &coax_init );

}

void vd_jaguar1_get_novideo( video_video_loss_s *vidloss )
{
	gpio_i2c_write(jaguar1_i2c_addr[vidloss->devnum], 0xFF, 0x00);
	vidloss->videoloss = gpio_i2c_read(jaguar1_i2c_addr[vidloss->devnum], 0xA0);
}

void vd_jaguar1_sw_reset( void *p_param )
{
	//video_input_init *sw_rst = (video_input_init*)p_param;

	REG_SET_1x81_0_1_VPLL_RST( 0, 0x1 );
	REG_SET_1x80_0_1_VPLL_C( 0, 0x1 );
	REG_SET_1x80_0_1_VPLL_C( 0, 0x0 );
	REG_SET_1x81_0_1_VPLL_RST( 0, 0x0 );
	printk("[drv]jaguar1_sw_reset complete!!\n");
}

