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
#include "nvp6158_common.h"
#include "nvp6158_video_eq.h"
#include "nvp6158_video_eq_table.h"
#include "nvp6168_eq_table.h"
#include "nvp6158_audio.h"
#include "nvp6158_video_auto_detect.h"

extern unsigned int nvp6158_iic_addr[4];
extern int nvp6158_chip_id[4];

/*******************************************************************************
*	Description		: get eq stage(manual)
*	Argurments		: Ch(channel), pDistance(distance structure)
*	Return value	: distance( eq stage)
*	Modify			:
*	warning			:
*******************************************************************************/
CABLE_DISTANCE NVP6158_NC_VD_MANUAL_CABLE_DISTANCE_Get (unsigned char Ch, video_input_cable_dist *pDistance)
{
	unsigned char sGetDistCnt = 0;
	unsigned char sGetDist[10] = {0, };
	unsigned char sMaxGetDistVal;
	unsigned char sMaxDistVal;
	unsigned char ii;
	int sMaxDistCnt;

	if((nvp6158_chip_id[Ch/4] == NVP6168C_R0_ID) || (nvp6158_chip_id[Ch/4] == NVP6168_R0_ID))
	{
		sMaxDistCnt = 3;
	}
	else
	{
		sMaxDistCnt = 10;
	}

	/* Get Distance 10 Times */
	while(sGetDistCnt < sMaxDistCnt)
	{
		msleep(1);

		//NC_VD_MANUAL_CABLE_DISTANCE_Read(pDistance);
		if((nvp6158_chip_id[Ch/4] == NVP6168C_R0_ID) || (nvp6158_chip_id[Ch/4] == NVP6168_R0_ID))
			nvp6168_video_input_cable_manualdist_read(pDistance);
		else
			nvp6158_video_input_cable_manualdist_read(pDistance);

		sGetDist[ pDistance->dist ]++;

		sGetDistCnt++;
	}

	sMaxDistVal = sGetDist[0];
	sMaxGetDistVal = 0;

	for(ii = 1; ii < 6; ii++)
		{
		if( sMaxDistVal < sGetDist[ii] )
		{
			sMaxDistVal = sGetDist[ii];
			sMaxGetDistVal = ii;
		}
	}


	printk("TESTING... Get Distance Value : ");
	for(ii = 0; ii < 6; ii++)
		printk("[ stage: %d _ get_value: %d ]\n", ii, sGetDist[ii]);

	printk(" Distance distinguish result : [%d]\n", sMaxGetDistVal);
//	return (CABLE_DISTANCE)pDistance->Dist;
	return sMaxGetDistVal;
}

//0:video on; 1: video loss
static int nvp6158_IsChAlive(video_equalizer_info_s *ps_eq_info)
{
	unsigned char vloss;
	unsigned char vloss_ch;

	gpio_i2c_write(nvp6158_iic_addr[ps_eq_info->devnum], 0xFF, 0x00);
	vloss = gpio_i2c_read(nvp6158_iic_addr[ps_eq_info->devnum], 0xA8);
	vloss_ch = ((vloss>>ps_eq_info->Ch)&0x01);
	return vloss_ch;
}

/**************************************************************************************
* @desc
* 	Function to read cable distance for EQ setting according to cable distance.(manual)
*
* @param_in		(unsigned char)Ch						Video Channel
*
* @return   	(CABLE_DISTANCE) 0				Short ( < 2M )
* @return   	1								100M
* @return   	2								200M
* @return   	3								300M
* @return   	4								400M
* @return   	5								500M
***************************************************************************************/
//CABLE_DISTANCE NC_APP_VD_MANUAL_CABLE_DISTANCE_Get(unsigned char Ch, NC_VIVO_CH_FORMATDEF FmtDef )
CABLE_DISTANCE nvp6158_get_eq_dist(video_equalizer_info_s *ps_eq_info)
{
	unsigned int Waiting_AGC_Stable_cnt = 0;
	unsigned char oChannel = 0;
	CABLE_DISTANCE Distance=0;
	unsigned char oMaxTimeCnt = 20;

	video_input_hsync_accum Hsync_Accumulation;
	video_input_sam_val SAM;
	video_input_agc_val AGC;
	NC_VD_AUTO_CABLE_DIST_STR Cable_Distance;
	video_input_cable_dist sManualDistance;

	unsigned char oDevAddr = 0x00;
	unsigned int AGC_Stable_Check = 0;

	oChannel = ps_eq_info->Ch;
	oDevAddr = ps_eq_info->devnum;

	SAM.ch = oChannel;
	SAM.devnum = oDevAddr;
	Hsync_Accumulation.ch = oChannel;
	Hsync_Accumulation.devnum = oDevAddr;
	AGC.ch = oChannel;
	AGC.devnum = oDevAddr;
	Cable_Distance.Ch = oChannel;
	Cable_Distance.devnum = oDevAddr;
	if(nvp6158_chip_id[oChannel/4]==NVP6158_R0_ID || nvp6158_chip_id[oChannel/4]==NVP6158C_R0_ID)
	{
		while(1)
		{
			if(0==nvp6158_IsChAlive(ps_eq_info))  //when camera disconnect during eq caculation.
			{
				Distance = 0;
				ps_eq_info->distance = Distance;
				return Distance;
			}
			msleep(300);

			//NC_VD_AUTO_SAM_Get(oChannel, &SAM);
			nvp6158_video_input_sam_val_read(&SAM);
			//NC_VD_AUTO_HSYNC_Get(oChannel, &Hsync_Accumulation);
			nvp6158_video_input_hsync_accum_read(&Hsync_Accumulation);
			//NC_VD_AUTO_AGC_Get(oChannel, &AGC);
			nvp6158_video_input_agc_val_read(&AGC);
			//NC_VD_AUTO_ACC_GAIN_Get(Ch, ACC_GAIN_NORMAL);
			//nvp6158_video_input_acc_gain_val_read();

			//AGC_Stable_Check = NC_APP_VD_AGC_STABLE_Check(&Hsync_Accumulation, &AGC, &SAM);
			AGC_Stable_Check = ((Hsync_Accumulation.hsync_accum_result!=0)&&(SAM.sam_val!=0));

			if(AGC_Stable_Check || Waiting_AGC_Stable_cnt >= oMaxTimeCnt)
			{
				/* temp  by edward */
				msleep(500);
				//NC_VD_AUTO_HSYNC_Get(oChannel, &Hsync_Accumulation);
				nvp6158_video_input_hsync_accum_read(&Hsync_Accumulation);
				//NC_VD_AUTO_AGC_Get(oChannel, &AGC);
				nvp6158_video_input_agc_val_read(&AGC);
				//NC_VD_AUTO_SAM_Get(oChannel, &SAM);
				nvp6158_video_input_sam_val_read(&SAM);

				printk("CH:[%d] Hsync 1 : %08x\n", oChannel, Hsync_Accumulation.hsync_accum_val1);
				printk("CH:[%d] Hsync 2 : %08x\n", oChannel, Hsync_Accumulation.hsync_accum_val2);
				printk("CH:[%d] Hsync Result : %08x\n", oChannel, Hsync_Accumulation.hsync_accum_result);

				printk("CH:[%d] Waiting for AGC Stable >>> %d\n", oChannel, Waiting_AGC_Stable_cnt + 1);

				if(Waiting_AGC_Stable_cnt >= oMaxTimeCnt)
				{
					printk("CH:[%d] AGC Stable Fail\n", oChannel);
				}
				else
				{
					printk("CH:[%d] AGC Stable Success.\n", oChannel);
				}
				Waiting_AGC_Stable_cnt = 0;
				break;
			}

			Waiting_AGC_Stable_cnt++;
		}
	}
	/* convert vfc to formatDefine for APP and save videoloss information */
	sManualDistance.ch = oChannel;
	sManualDistance.FmtDef = ps_eq_info->FmtDef;
	sManualDistance.devnum = oDevAddr;
	sManualDistance.cabletype = 0; 		// Now, we use coaxial cable(0:coax, 1:utp, 2:reserved1, 3:reserved2

	Distance = NVP6158_NC_VD_MANUAL_CABLE_DISTANCE_Get(oChannel, &sManualDistance);
	ps_eq_info->distance = Distance;
	return Distance;
}

unsigned char __nvp6158_video_cable_manualdistance( unsigned char cabletype, video_input_hsync_accum *pvin_hsync_accum, video_input_acc_gain_val *pvin_acc_val, nvp6158_video_equalizer_distance_table_s *pdistance_value )
{
	int i = 0;
	unsigned char distance = 0; /* default : short(0) */

	/* for coaxial */
	if( cabletype == 0 )
	{
		for( i = 0; i < 6; i++ )
		{
				if( (pvin_hsync_accum->hsync_accum_result > pdistance_value->hsync_stage.hsync_stage[i]) )
			{
				distance = i;
				break;
			}

		}
		if( i == 6 )
		{
			distance = 5;
		}
	}

	if( pvin_hsync_accum->hsync_accum_result == 0 )
	{
		distance = 0; /* set default value(short:0) */
	}

	printk(">>>>> DRV[%s:%d] CH:%d, distance:%d\n", __func__, __LINE__, pvin_hsync_accum->ch, distance );

	return distance;
}

void __nvp6158_eq_base_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_base_s *pbase )
{
	unsigned char devnum = pvin_eq_set->devnum;
	unsigned char ch = pvin_eq_set->Ch;
	unsigned char dist = pvin_eq_set->distance;

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05 + ch );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x01, pbase->eq_bypass[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x58, pbase->eq_band_sel[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5C, pbase->eq_gain_sel[dist] );

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xff, (ch < 2 ? 0x0a : 0x0b) );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x3d + (ch%2 * 0x80), pbase->deq_a_on[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x3c + (ch%2 * 0x80), pbase->deq_a_sel[dist] );

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x09 );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x80 + (ch * 0x20), pbase->deq_b_sel[dist] );

#if 0 //test
	printk("ch[%d]: BASE, dist:%d, eq_bypass[%02x]\n", ch, dist, pbase->eq_bypass[dist] );
	printk("ch[%d]: BASE, dist:%d, eq_band_sel[%02x]\n", ch, dist, pbase->eq_band_sel[dist] );
	printk("ch[%d]: BASE, dist:%d, eq_gain_sel[%02x]\n", ch, dist, pbase->eq_gain_sel[dist] );
	printk("ch[%d]: BASE, dist:%d, deq_a_on[%02x]\n", ch, dist, pbase->deq_a_on[dist] );
	printk("ch[%d]: BASE, dist:%d, deq_a_sel[%02x]\n", ch, dist, pbase->deq_a_sel[dist] );
	printk("ch[%d]: BASE, dist:%d, deq_b_sel[%02x]\n", ch, dist, pbase->deq_b_sel[dist] );
#endif
}

void __nvp6158_eq_coeff_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_coeff_s *pcoeff )
{
	unsigned char devnum = pvin_eq_set->devnum;
	unsigned char ch = pvin_eq_set->Ch;
	unsigned char dist = pvin_eq_set->distance;

//	unsigned char val_0x30;
//	unsigned char val_0x31;
//	unsigned char val_0x32;

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xff, (ch < 2 ? 0x0a : 0x0b) );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + (ch%2 * 0x80), pcoeff->deqA_01[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x31 + (ch%2 * 0x80), pcoeff->deqA_02[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x32 + (ch%2 * 0x80), pcoeff->deqA_03[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x33 + (ch%2 * 0x80), pcoeff->deqA_04[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + (ch%2 * 0x80), pcoeff->deqA_05[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x35 + (ch%2 * 0x80), pcoeff->deqA_06[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x36 + (ch%2 * 0x80), pcoeff->deqA_07[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x37 + (ch%2 * 0x80), pcoeff->deqA_08[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x38 + (ch%2 * 0x80), pcoeff->deqA_09[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x39 + (ch%2 * 0x80), pcoeff->deqA_10[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x3a + (ch%2 * 0x80), pcoeff->deqA_11[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x3b + (ch%2 * 0x80), pcoeff->deqA_12[dist] );

#if 0
	printk("ch[%d]: COEFF, dist:%d, deqA_01[%02x]\n", ch, dist, pcoeff->deqA_01[dist] );
	printk("ch[%d]: COEFF, dist:%d, deqA_02[%02x]\n", ch, dist, pcoeff->deqA_02[dist] );
	printk("ch[%d]: COEFF, dist:%d, deqA_03[%02x]\n", ch, dist, pcoeff->deqA_03[dist] );
	printk("ch[%d]: COEFF, dist:%d, deqA_04[%02x]\n", ch, dist, pcoeff->deqA_04[dist] );
	printk("ch[%d]: COEFF, dist:%d, deqA_05[%02x]\n", ch, dist, pcoeff->deqA_05[dist] );
	printk("ch[%d]: COEFF, dist:%d, deqA_06[%02x]\n", ch, dist, pcoeff->deqA_06[dist] );
	printk("ch[%d]: COEFF, dist:%d, deqA_07[%02x]\n", ch, dist, pcoeff->deqA_07[dist] );
	printk("ch[%d]: COEFF, dist:%d, deqA_08[%02x]\n", ch, dist, pcoeff->deqA_08[dist] );
	printk("ch[%d]: COEFF, dist:%d, deqA_09[%02x]\n", ch, dist, pcoeff->deqA_09[dist] );
	printk("ch[%d]: COEFF, dist:%d, deqA_10[%02x]\n", ch, dist, pcoeff->deqA_10[dist] );
	printk("ch[%d]: COEFF, dist:%d, deqA_11[%02x]\n", ch, dist, pcoeff->deqA_11[dist] );
	printk("ch[%d]: COEFF, dist:%d, deqA_12[%02x]\n", ch, dist, pcoeff->deqA_12[dist] );
#endif
}

void __nvp6158_eq_color_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_color_s *pcolor )
{
	unsigned char devnum = pvin_eq_set->devnum;
	unsigned char ch = pvin_eq_set->Ch;
	unsigned char dist = pvin_eq_set->distance;

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xff, 0x00 );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x10 + ch, pcolor->contrast[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x18 + ch, pcolor->h_peaking[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x21 + ch*4, pcolor->c_filter[dist] );

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x40 + ch, pcolor->hue[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x44 + ch, pcolor->u_gain[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x48 + ch, pcolor->v_gain[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x4C + ch, pcolor->u_offset[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x50 + ch, pcolor->v_offset[dist] );

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xff, 0x05 + ch);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x20, pcolor->black_level[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x27, pcolor->acc_ref[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x28, pcolor->cti_delay[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x2b, pcolor->sub_saturation[dist] );

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x24, pcolor->burst_dec_a[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5f, pcolor->burst_dec_b[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xd1, pcolor->burst_dec_c[dist] );

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xd5, pcolor->c_option[dist] );

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xff, (ch < 2 ? 0x0a : 0x0b) );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x25 + (ch%2 * 0x80), pcolor->y_filter_b[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x27 + (ch%2 * 0x80), pcolor->y_filter_b_sel[dist] );

	if( pvin_eq_set->FmtDef == TVI_8M_15P || pvin_eq_set->FmtDef == TVI_8M_12_5P )
	{
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xff, 0x00 );

		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0c + ch, 0xf0 );
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x3c + ch, 0xB8 );
	}
	else
	{
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xff, 0x00 );

		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0c + ch, 0x00 );
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x3c + ch, 0x80 );
	}

#if 0
	printk("ch[%d]: COLOR, dist:%d, contrast[%02x]\n", ch, dist, pcolor->contrast[dist] );
	printk("ch[%d]: COLOR, dist:%d, h_peaking[%02x]\n", ch, dist, pcolor->h_peaking[dist] );
	printk("ch[%d]: COLOR, dist:%d, c_filter[%02x]\n", ch, dist, pcolor->c_filter[dist] );

	printk("ch[%d]: COLOR, dist:%d, hue[%02x]\n", ch, dist, pcolor->hue[dist] );
	printk("ch[%d]: COLOR, dist:%d, u_gain[%02x]\n", ch, dist, pcolor->u_gain[dist] );
	printk("ch[%d]: COLOR, dist:%d, v_gain[%02x]\n", ch, dist, pcolor->v_gain[dist] );
	printk("ch[%d]: COLOR, dist:%d, u_offset[%02x]\n", ch, dist, pcolor->u_offset[dist] );
	printk("ch[%d]: COLOR, dist:%d, v_offset[%02x]\n", ch, dist, pcolor->v_offset[dist] );

	printk("ch[%d]: COLOR, dist:%d, black_level[%02x]\n", ch, dist, pcolor->black_level[dist] );
	printk("ch[%d]: COLOR, dist:%d, cti_delay[%02x]\n", ch, dist, pcolor->cti_delay[dist] );
	printk("ch[%d]: COLOR, dist:%d, sub_saturation[%02x]\n", ch, dist, pcolor->sub_saturation[dist] );

	printk("ch[%d]: COLOR, dist:%d, burst_dec_a[%02x]\n", ch, dist, pcolor->burst_dec_a[dist] );
	printk("ch[%d]: COLOR, dist:%d, burst_dec_b[%02x]\n", ch, dist, pcolor->burst_dec_b[dist] );
	printk("ch[%d]: COLOR, dist:%d, burst_dec_c[%02x]\n", ch, dist, pcolor->burst_dec_c[dist] );

	printk("ch[%d]: COLOR, dist:%d, c_option[%02x]\n", ch, dist, pcolor->c_option[dist] );
#endif
}

void __nvp6158_eq_timing_a_set_value(video_equalizer_info_s * pvin_eq_set, video_equalizer_timing_a_s * ptiming_a)
{
	unsigned char devnum = pvin_eq_set->devnum;
	unsigned char ch = pvin_eq_set->Ch;
	unsigned char dist = pvin_eq_set->distance;

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00 );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x58 + ch, ptiming_a->h_delay_a[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x89 + ch, ptiming_a->h_delay_b[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x8e + ch, ptiming_a->h_delay_c[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xa0 + ch, ptiming_a->y_delay[dist] );

#if 0
	printk("ch[%d]: TIMING_A, dist:%d, h_delay_a[%02x]\n", ch, dist, ptiming_a->h_delay_a[dist] );
	printk("ch[%d]: TIMING_A, dist:%d, h_delay_b[%02x]\n", ch, dist, ptiming_a->h_delay_b[dist] );
	printk("ch[%d]: TIMING_A, dist:%d, h_delay_c[%02x]\n", ch, dist, ptiming_a->h_delay_c[dist] );
	printk("ch[%d]: TIMING_A, dist:%d, y_delay[%02x]\n", ch, dist, ptiming_a->y_delay[dist] );
#endif
}

void __nvp6158_eq_clk_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_clk_s *pclk )
{
	unsigned char devnum = pvin_eq_set->devnum;
	unsigned char ch = pvin_eq_set->Ch;
	unsigned char dist = pvin_eq_set->distance;

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x01 );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x84 + ch, pclk->clk_adc[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x8C + ch, pclk->clk_dec[dist] );
}
void __nvp6158_eq_timing_b_set_value( video_equalizer_info_s *pvin_eq_set, video_equalizer_timing_b_s *ptiming_b )
{
	unsigned char devnum = pvin_eq_set->devnum;
	unsigned char ch = pvin_eq_set->Ch;
	unsigned char dist = pvin_eq_set->distance;

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xff, 0x09 );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x96 + (ch * 0x20), ptiming_b->h_scaler1[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x97 + (ch * 0x20), ptiming_b->h_scaler2[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x98 + (ch * 0x20), ptiming_b->h_scaler3[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x99 + (ch * 0x20), ptiming_b->h_scaler4[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x9A + (ch * 0x20), ptiming_b->h_scaler5[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x9B + (ch * 0x20), ptiming_b->h_scaler6[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x9C + (ch * 0x20), ptiming_b->h_scaler7[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x9D + (ch * 0x20), ptiming_b->h_scaler8[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x9E + (ch * 0x20), ptiming_b->h_scaler9[dist] );

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x40 + ch , ptiming_b->pn_auto[dist] );

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xff, 0x05 + ch );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x90, ptiming_b->comb_mode[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xb9, ptiming_b->h_pll_op_a[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x57, ptiming_b->mem_path[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x25, ptiming_b->fsc_lock_speed[dist] );


	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xff, 0x00 );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x81 + ch, ptiming_b->format_set1[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x85 + ch, ptiming_b->format_set2[dist] );
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x64 + ch, ptiming_b->v_delay[dist] );

#if 0
	printk("ch[%d]: TIMING_B, dist:%d, h_scaler1[%02x]\n", ch, dist, ptiming_b->h_scaler1[dist] );
	printk("ch[%d]: TIMING_B, dist:%d, h_scaler2[%02x]\n", ch, dist, ptiming_b->h_scaler2[dist] );
	printk("ch[%d]: TIMING_B, dist:%d, h_scaler3[%02x]\n", ch, dist, ptiming_b->h_scaler3[dist] );
	printk("ch[%d]: TIMING_B, dist:%d, h_scaler4[%02x]\n", ch, dist, ptiming_b->h_scaler4[dist] );

	printk("ch[%d]: TIMING_B, dist:%d, pn_auto[%02x]\n", ch, dist, ptiming_b->pn_auto[dist] );
	printk("ch[%d]: TIMING_B, dist:%d, comb_mode[%02x]\n", ch, dist, ptiming_b->comb_mode[dist] );
	printk("ch[%d]: TIMING_B, dist:%d, h_pll_op_a[%02x]\n", ch, dist, ptiming_b->h_pll_op_a[dist] );
	printk("ch[%d]: TIMING_B, dist:%d, mem_path[%02x]\n", ch, dist, ptiming_b->mem_path[dist] );
	printk("ch[%d]: TIMING_B, dist:%d, format_set1[%02x]\n", ch, dist, ptiming_b->format_set1[dist] );
	printk("ch[%d]: TIMING_B, dist:%d, format_set2[%02x]\n", ch, dist, ptiming_b->format_set2[dist] );
	printk("ch[%d]: TIMING_B, dist:%d, v_delay[%02x]\n", ch, dist, ptiming_b->v_delay[dist] );
#endif

}

unsigned int __nvp6158_get_acc_gain(unsigned char ch, unsigned char devnum)
{
	unsigned int acc_gain_status;

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF,0x05+ch%4);
	acc_gain_status = gpio_i2c_read(nvp6158_iic_addr[devnum],0xE2);
	acc_gain_status <<= 8;
	acc_gain_status |= gpio_i2c_read(nvp6158_iic_addr[devnum],0xE3);

	return acc_gain_status;
}

unsigned int __nvp6158_get_yplus_slope(unsigned char ch, unsigned char devnum)
{
	unsigned int y_plus_slp_status;

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF,0x05+ch%4);
	y_plus_slp_status = gpio_i2c_read(nvp6158_iic_addr[devnum],0xE8)&0x07;
	y_plus_slp_status <<= 8;
	y_plus_slp_status |= gpio_i2c_read(nvp6158_iic_addr[devnum],0xE9);

	return y_plus_slp_status;
}

unsigned int __nvp6158_get_yminus_slope(unsigned char ch, unsigned char devnum)
{
	unsigned int y_minus_slp_status;

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF,0x05+ch%4);
	y_minus_slp_status = gpio_i2c_read(nvp6158_iic_addr[devnum],0xEA)&0x07;
	y_minus_slp_status <<= 8;
	y_minus_slp_status |= gpio_i2c_read(nvp6158_iic_addr[devnum],0xEB);

	return y_minus_slp_status;
}

unsigned int __nvp6158_get_sync_width( unsigned char ch, unsigned char devnum )
{
	unsigned char	 reg_B0_E0 = 0;
	unsigned char	 agc_stable = 0;
	unsigned int	 sync_width = 0;
	unsigned int 	 check_timeout = 0;

	while(agc_stable == 0)
	{
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
		reg_B0_E0 = gpio_i2c_read(nvp6158_iic_addr[devnum], 0xE0 )&0xF;
		agc_stable = reg_B0_E0 & (0x01 << (ch%4));

		if( check_timeout++ > 100 )
		{
			printk(">>>>> DRV[%s:%d] CH:%d, TimeOut, AGC_stable[%x] check[%x] in get sync width\n", __func__, __LINE__, ch, reg_B0_E0, agc_stable );
			break;
		}
		msleep(1);
	}

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+(ch%4));
	sync_width = gpio_i2c_read(nvp6158_iic_addr[devnum], 0xC4)&0x0F;
	sync_width <<=8;
	sync_width |= gpio_i2c_read(nvp6158_iic_addr[devnum], 0xC5);
	sync_width = sync_width & 0x0FFF;
	printk(">>>>> DRV[%s:%d] CH:%d, sync_width:0x%x\n", __func__, __LINE__, ch, sync_width );

	return sync_width;
}

int nvp6158_video_input_cable_measure_way( unsigned char ch, unsigned char devnum )
{
	unsigned int  acc_gain;
	unsigned int  y_slope;
	unsigned char y_plus_slope;
	unsigned char y_minus_slope;
	unsigned int  sync_width;

	acc_gain = __nvp6158_get_acc_gain(ch, devnum);
	y_plus_slope = __nvp6158_get_yplus_slope(ch, devnum);
    y_minus_slope = __nvp6158_get_yminus_slope(ch, devnum);
    y_slope = y_plus_slope + y_minus_slope;
    sync_width = __nvp6158_get_sync_width(ch, devnum);

    printk(">>>>> DRV[%s:%d] CH:%d, accgain=0x%x(%d), yslope=0x%x(%d), syncwidth=0x%x(%d)\n", \
    		__func__, __LINE__, ch, acc_gain, acc_gain, y_slope, y_slope, sync_width, sync_width );

    return 0;
}

void nvp6158_video_input_cable_manualdist_read(video_input_cable_dist *vin_cable_dist )
{
	video_input_acc_gain_val vin_acc;
	video_input_hsync_accum  vin_hsync_accum;

	/* cable type => 0:coaxial, 1:utp, 2:reserved1, 3:reserved2 */
	nvp6158_video_equalizer_distance_table_s distance_value = (nvp6158_video_equalizer_distance_table_s)equalizer_distance_fmtdef[vin_cable_dist->FmtDef];

	if( vin_cable_dist->FmtDef >= AHD20_SD_H960_NT && vin_cable_dist->FmtDef <= AHD20_SD_H960_2EX_Btype_PAL )
	{
		/* CVBS Resolution not need distance distinguish, because cvbs format has low color frequency */
		vin_cable_dist->dist = 0;
	}
	else if(distance_value.hsync_stage.hsync_stage[0] != 0)
	{
		/* get hsync*/
		vin_hsync_accum.ch = vin_cable_dist->ch;
		vin_hsync_accum.devnum = vin_cable_dist->devnum;
		nvp6158_video_input_hsync_accum_read(&vin_hsync_accum);

		/* get acc */
		vin_acc.ch = vin_cable_dist->ch;
		vin_acc.devnum = vin_cable_dist->devnum;
		vin_acc.func_sel = 0;
		/* 1 is ACC_GAIN_DEBUG
		   0 is ACC_GAIN_NORMAL */
		nvp6158_video_input_acc_gain_val_read(&vin_acc);

		/* measure eq */
		nvp6158_video_input_cable_measure_way(vin_cable_dist->ch, vin_cable_dist->devnum);

		/* decision distance using hsync and distance table */
		vin_cable_dist->dist = __nvp6158_video_cable_manualdistance( vin_cable_dist->cabletype, &vin_hsync_accum, &vin_acc, &distance_value );


		printk(">>>>> DRV, CH:%d, hsync : %08x\n", vin_cable_dist->ch, vin_hsync_accum.hsync_accum_result);
		printk(">>>>> DRV, CH:%d, eq stage:%d\n", vin_cable_dist->ch, vin_cable_dist->dist);
	}
	else
	{
		vin_cable_dist->dist = 0;

		printk(">>>>> DRV, CH:%d, This Format Not support Yet [%d] eq stage:%d\n", vin_cable_dist->ch, vin_cable_dist->FmtDef ,vin_cable_dist->dist);

	}
}

void nvp6168_video_input_cable_manualdist_read(video_input_cable_dist *vin_cable_dist )
{
	video_input_hsync_accum  vin_hsync_accum;
	nvp6158_video_equalizer_distance_table_s distance_value = (nvp6158_video_equalizer_distance_table_s)nvp6168_equalizer_distance_fmtdef[vin_cable_dist->FmtDef];

	if( vin_cable_dist->FmtDef >= AHD20_SD_H960_NT && vin_cable_dist->FmtDef <= AHD20_SD_H960_2EX_Btype_PAL )
	{
		/* CVBS Resolution not need distance distinguish, because cvbs format has low color frequency */
		vin_cable_dist->dist = 0;
		return;
	}

	/* get hsync*/
	vin_hsync_accum.ch = vin_cable_dist->ch;
	vin_hsync_accum.devnum = vin_cable_dist->devnum;
	nvp6168_video_input_hsync_accum_read(&vin_hsync_accum );

	if(((vin_hsync_accum.hsync_accum_val1|vin_hsync_accum.hsync_accum_val2) == 0) &&
			(vin_hsync_accum.hsync_accum_result == 0xffffffff))
	{
		vin_cable_dist->dist = 0xFF;
		return;
	}

	/* decision distance using hsync and distance table */
	vin_cable_dist->dist = __nvp6158_video_cable_manualdistance( vin_cable_dist->cabletype, &vin_hsync_accum, 0, &distance_value );

	if(vin_cable_dist->dist > 5)
		vin_cable_dist->dist = 5;

	printk(">>>>> DRV, CH:%d, hsync : %08x\n", vin_cable_dist->ch, vin_hsync_accum.hsync_accum_result);
	printk(">>>>> DRV, CH:%d, eq stage:%d\n", vin_cable_dist->ch, vin_cable_dist->dist);
}
int nvp6158_set_equalizer(video_equalizer_info_s *pvin_eq_set)
{
	int ii;
	unsigned char val_13x30;
	unsigned char val_13x31;
	unsigned char val_13x32;
	unsigned char val_0x54;
	//unsigned char val_5678x69;
	unsigned char val_9x44;

	unsigned char ch = pvin_eq_set->Ch;
	unsigned char devnum = pvin_eq_set->devnum;
	video_equalizer_value_table_s eq_value;

	/* cable type => 0:coaxial, 1:utp, 2:reserved1, 3:reserved2 */
	//video_equalizer_value_table_s eq_value = (video_equalizer_value_table_s)nvp6158_equalizer_value_fmtdef[pvin_eq_set->FmtDef];
	memset(&eq_value, 0xFF,sizeof(video_equalizer_value_table_s));
	memcpy(&eq_value,&nvp6158_equalizer_value_fmtdef[pvin_eq_set->FmtDef],sizeof(video_equalizer_value_table_s));
	if(0xFF == eq_value.eq_base.eq_band_sel[pvin_eq_set->distance] || 0x00 == eq_value.eq_base.eq_bypass[pvin_eq_set->distance]) //if 5x58==0xFF it's not a valid value.
	{
		printk(">>>>>>DRV %s not supported video format[%2x]\n\n\n", __func__, pvin_eq_set->FmtDef);
	   	/* Auto Mode ON */
		gpio_i2c_write(nvp6158_iic_addr[pvin_eq_set->devnum], 0xff, 0x13 );
		val_13x30 = gpio_i2c_read(nvp6158_iic_addr[pvin_eq_set->devnum], 0x30);
		val_13x30 |= (0x11 << pvin_eq_set->Ch);
		gpio_i2c_write(nvp6158_iic_addr[pvin_eq_set->devnum], 0x30, val_13x30 );

		val_13x31 = gpio_i2c_read(nvp6158_iic_addr[pvin_eq_set->devnum], 0x31);
		val_13x31 |= (0x11 << pvin_eq_set->Ch);
		gpio_i2c_write(nvp6158_iic_addr[pvin_eq_set->devnum], 0x31, val_13x31 );

		val_13x32 = gpio_i2c_read(nvp6158_iic_addr[pvin_eq_set->devnum], 0x32);
		val_13x32 |= (0x01 << pvin_eq_set->Ch);
		gpio_i2c_write(nvp6158_iic_addr[pvin_eq_set->devnum], 0x32, val_13x32 );
		return -1;
	}
    /* for verification by edward */

	if(pvin_eq_set->FmtDef == AHD20_720P_30P_EX_Btype 	|| pvin_eq_set->FmtDef == AHD20_720P_25P_EX_Btype 	||
	   pvin_eq_set->FmtDef == AHD20_720P_30P 				|| pvin_eq_set->FmtDef == AHD20_720P_25P 			||
	   pvin_eq_set->FmtDef == CVI_HD_30P_EX           		|| pvin_eq_set->FmtDef == CVI_HD_25P_EX				||
	   pvin_eq_set->FmtDef == CVI_HD_30P              			|| pvin_eq_set->FmtDef == CVI_HD_25P  				||
	   pvin_eq_set->FmtDef == TVI_HD_30P			  		|| pvin_eq_set->FmtDef == TVI_HD_25P			    		||
	   pvin_eq_set->FmtDef == TVI_HD_30P_EX			  	|| pvin_eq_set->FmtDef == TVI_HD_25P_EX				||
	   pvin_eq_set->FmtDef == TVI_HD_B_30P			  	|| pvin_eq_set->FmtDef == TVI_HD_B_25P 				||
	   pvin_eq_set->FmtDef == TVI_HD_B_30P_EX		  		|| pvin_eq_set->FmtDef == TVI_HD_B_25P_EX
	  )
	{
		printk("DRV >> This Format Support Maximum EQ Stage 10\n");
		printk("DRV >> Now Select EQ Stage %d\n", pvin_eq_set->distance);
	}
	else
	{
		if(pvin_eq_set->distance > 5)
		{
			printk("DRV >> This Format Only Support Maximum EQ Stage 5\n");
			printk("DRV >> Now Select EQ Stage %d\n", pvin_eq_set->distance);
			pvin_eq_set->distance = 5;
		}
	}

	/* set eq value */
	__nvp6158_eq_base_set_value( pvin_eq_set, &eq_value.eq_base );
	__nvp6158_eq_coeff_set_value( pvin_eq_set, &eq_value.eq_coeff );
	__nvp6158_eq_color_set_value( pvin_eq_set, &eq_value.eq_color);
	__nvp6158_eq_timing_a_set_value( pvin_eq_set, &eq_value.eq_timing_a );
	__nvp6158_eq_clk_set_value( pvin_eq_set, &eq_value.eq_clk );
	__nvp6158_eq_timing_b_set_value( pvin_eq_set, &eq_value.eq_timing_b );

	if( pvin_eq_set->FmtDef >= AHD20_SD_H960_NT && pvin_eq_set->FmtDef <= AHD20_SD_H960_2EX_Btype_PAL )
	{
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xff, 0x00);
		if((pvin_eq_set->FmtDef >= AHD20_SD_H960_NT)&&(pvin_eq_set->FmtDef <= AHD20_SD_H960_EX_PAL))
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);		/* line_mem_mode disable */
		else
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x01);		/* line_mem_mode Enable */

		if( (pvin_eq_set->FmtDef%2) == 0 ) //NTSC
		{
			val_0x54 = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x54);
			val_0x54 &= ~((0x1 << (ch+4)));
			val_0x54 |= ((0x1 << (ch+4)));
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x54, val_0x54);  /* Enable FLD_INV for CVBS NT format */

			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x08 + ch, 0xa0);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0xd0); /* Set V_Delay */
		}
		else //if( pvin_eq_set->FmtDef == AHD20_SD_H960_2EX_Btype_PAL )
		{
			val_0x54 = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x54);
			val_0x54 &= ~((0x1 << (ch+4)));
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x54, val_0x54);  /* Disable FLD_INV for CVBS PAL format */

			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x08 + ch, 0xdd);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0xbf);  /* Set V_Delay */

		}

		gpio_i2c_write( nvp6158_iic_addr[devnum], 0xff, 0x05 + ch );
		gpio_i2c_write( nvp6158_iic_addr[devnum], 0x2C, 0x08);
		gpio_i2c_write( nvp6158_iic_addr[devnum], 0x47, 0x04);
		if((pvin_eq_set->FmtDef >= AHD20_SD_H960_NT)&&(pvin_eq_set->FmtDef <= AHD20_SD_H960_EX_PAL))
			gpio_i2c_write( nvp6158_iic_addr[devnum], 0x64, 0x00 );         /* disable Mem_Path */
		else
			gpio_i2c_write( nvp6158_iic_addr[devnum], 0x64, 0x01 );         /* Enable Mem_Path */
	}
	else
	{
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xff, 0x00);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);		/* line_mem_mode Disable */
		val_0x54 = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x54);
		val_0x54 &= ~((0x1 << (ch+4)));
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x54, val_0x54);	/* Disable FLD_INV */

		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x08 + ch, 0x00);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x80);	/* Recovery V_Delay */

		gpio_i2c_write( nvp6158_iic_addr[devnum], 0xff, 0x05 + ch );
		gpio_i2c_write( nvp6158_iic_addr[devnum], 0x2C, 0x00);
		gpio_i2c_write( nvp6158_iic_addr[devnum], 0x47, 0xEE);
		gpio_i2c_write( nvp6158_iic_addr[devnum], 0x64, 0x00 );        /* Disable Mem_Path */

		if(pvin_eq_set->FmtDef == TVI_4M_15P )
		{
			gpio_i2c_write( nvp6158_iic_addr[devnum], 0x6E, 0x10 );    //VBLK setting
			gpio_i2c_write( nvp6158_iic_addr[devnum], 0x6F, 0x7e ); 
		}
		else
		{
			gpio_i2c_write( nvp6158_iic_addr[devnum], 0x6E, 0x00 );    //VBLK default setting
			gpio_i2c_write( nvp6158_iic_addr[devnum], 0x6F, 0x00 ); 
		}
	}

	/* Auto Mode Off */
	gpio_i2c_write(nvp6158_iic_addr[pvin_eq_set->devnum], 0xff, 0x13 );
	val_13x30 = gpio_i2c_read(nvp6158_iic_addr[pvin_eq_set->devnum], 0x30);
	val_13x30 &= ~(0x11 << pvin_eq_set->Ch);
	gpio_i2c_write(nvp6158_iic_addr[pvin_eq_set->devnum], 0x30, val_13x30 );

	val_13x31 = gpio_i2c_read(nvp6158_iic_addr[pvin_eq_set->devnum], 0x31);
	val_13x31 &= ~(0x11 << pvin_eq_set->Ch);
	gpio_i2c_write(nvp6158_iic_addr[pvin_eq_set->devnum], 0x31, val_13x31 );

	val_13x32 = gpio_i2c_read(nvp6158_iic_addr[pvin_eq_set->devnum], 0x32);
	val_13x32 &= ~(0x01 << pvin_eq_set->Ch);
	gpio_i2c_write(nvp6158_iic_addr[pvin_eq_set->devnum], 0x32, val_13x32 );

	gpio_i2c_write(nvp6158_iic_addr[pvin_eq_set->devnum], 0xff, 0x05 + ch);
	gpio_i2c_write(nvp6158_iic_addr[pvin_eq_set->devnum], 0x59, 0x00 );

	gpio_i2c_write(nvp6158_iic_addr[pvin_eq_set->devnum], 0xff, 0x00 );
	gpio_i2c_write(nvp6158_iic_addr[pvin_eq_set->devnum], 0x23 + (pvin_eq_set->Ch * 4), 0x41);

	for(ii=0;ii<0x16;ii++)
	{
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x11);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + ( ch * 0x20 ) + ii, 0x00);   //first set bank11 to default values.
	}

	if( pvin_eq_set->FmtDef == TVI_5M_20P || pvin_eq_set->FmtDef == TVI_5M_12_5P ||
		pvin_eq_set->FmtDef == TVI_4M_30P || pvin_eq_set->FmtDef == TVI_4M_25P	 ||
		pvin_eq_set->FmtDef == TVI_8M_15P || pvin_eq_set->FmtDef == TVI_8M_12_5P ||
		pvin_eq_set->FmtDef == TVI_4M_15P )

	{
		if(pvin_eq_set->FmtDef != TVI_4M_15P)
		{
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF,0x09);
			val_9x44 = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x44);
			val_9x44 &= ~(1 << ch);
			val_9x44 |= (1 << ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x44 , val_9x44);
		}

		if( pvin_eq_set->FmtDef == TVI_5M_20P)
		{
			/* TVI 5M 20P PN Value Set */
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x50 + ( ch * 4 ) , 0x36);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x51 + ( ch * 4 ) , 0x40);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x52 + ( ch * 4 ) , 0xa7);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x53 + ( ch * 4 ) , 0x74);

			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x02 + ( ch * 0x20 ), 0xdb);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x03 + ( ch * 0x20 ), 0x0a);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05 + ( ch * 0x20 ), 0x0e);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x06 + ( ch * 0x20 ), 0xa6);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x08 + ( ch * 0x20 ), 0x96);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0a + ( ch * 0x20 ), 0x07);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0b + ( ch * 0x20 ), 0x98);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0c + ( ch * 0x20 ), 0x07);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0d + ( ch * 0x20 ), 0xbc);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x11 + ( ch * 0x20 ), 0xa0);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x13 + ( ch * 0x20 ), 0xfa);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x15 + ( ch * 0x20 ), 0x65);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + ( ch * 0x20 ), 0x0f);
		}
		else if( pvin_eq_set->FmtDef == TVI_4M_15P)
		{
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x02 + ( ch * 0x20 ), 0xd0);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x03 + ( ch * 0x20 ), 0x0a);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05 + ( ch * 0x20 ), 0x97);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x06 + ( ch * 0x20 ), 0x70);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x08 + ( ch * 0x20 ), 0x78);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0a + ( ch * 0x20 ), 0x05);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0b + ( ch * 0x20 ), 0xa0);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0c + ( ch * 0x20 ), 0x06);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0d + ( ch * 0x20 ), 0x71);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x11 + ( ch * 0x20 ), 0x50);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x13 + ( ch * 0x20 ), 0x96);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x15 + ( ch * 0x20 ), 0x30);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + ( ch * 0x20 ), 0x0f);
		}
		else if( pvin_eq_set->FmtDef == TVI_5M_12_5P)
		{
			/* TVI 5M 12_5P PN Value Set */
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x50 + ( ch * 4 ) , 0x8b);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x51 + ( ch * 4 ) , 0xae);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x52 + ( ch * 4 ) , 0xbb);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x53 + ( ch * 4 ) , 0x48);
		}
		else if( pvin_eq_set->FmtDef == TVI_4M_30P || pvin_eq_set->FmtDef == TVI_4M_25P )
		{
			/* TVI 4M 30P PN Value Set */
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x50 + ( ch * 4 ) , 0x9e);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x51 + ( ch * 4 ) , 0x48);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x52 + ( ch * 4 ) , 0x59);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x53 + ( ch * 4 ) , 0x74);
		}
		else if( pvin_eq_set->FmtDef == TVI_8M_15P || pvin_eq_set->FmtDef == TVI_8M_12_5P )
		{
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x50 + ( ch * 4 ) , 0x73);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x51 + ( ch * 4 ) , 0x76);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x52 + ( ch * 4 ) , 0x58);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x53 + ( ch * 4 ) , 0x74);

			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x11);

			if( pvin_eq_set->FmtDef == TVI_8M_12_5P )
			{
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x02 + ( ch * 0x20 ), 0x9b);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x03 + ( ch * 0x20 ), 0x0f);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05 + ( ch * 0x20 ), 0x14);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x06 + ( ch * 0x20 ), 0xa0);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x08 + ( ch * 0x20 ), 0x80);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0a + ( ch * 0x20 ), 0x08);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0b + ( ch * 0x20 ), 0x70);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0c + ( ch * 0x20 ), 0x08);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0d + ( ch * 0x20 ), 0xca);
	//			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x11 + ( ch * 0x20 ), 0xa0);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x12 + ( ch * 0x20 ), 0x01);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x13 + ( ch * 0x20 ), 0xcc);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x15 + ( ch * 0x20 ), 0x3c);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + ( ch * 0x20 ), 0x0d);


				gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch%4);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x25, 0xda);
				msleep(100);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x2a, 0xd4);
				msleep(40);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x2a, 0xd2);
				printk("TVI_8M_12_5P adopted test\n");
			}
			else
			{
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x02 + ( ch * 0x20 ), 0x9b);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x03 + ( ch * 0x20 ), 0x0f);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05 + ( ch * 0x20 ), 0x11);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x06 + ( ch * 0x20 ), 0x30);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x08 + ( ch * 0x20 ), 0x80);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0a + ( ch * 0x20 ), 0x08);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0b + ( ch * 0x20 ), 0x70);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0c + ( ch * 0x20 ), 0x08);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0d + ( ch * 0x20 ), 0xca);
	//			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x11 + ( ch * 0x20 ), 0xa0);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x12 + ( ch * 0x20 ), 0x01);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x13 + ( ch * 0x20 ), 0xcc);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x15 + ( ch * 0x20 ), 0x3c);
				gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + ( ch * 0x20 ), 0x0d);
			}

		}

	}
	else
	{
		if( pvin_eq_set->FmtDef == CVI_5M_20P)
		{
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x11);

			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x01 + ( ch * 0x20 ), 0x01);	
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x02 + ( ch * 0x20 ), 0x30);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x03 + ( ch * 0x20 ), 0x0a);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x04 + ( ch * 0x20 ), 0x20);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05 + ( ch * 0x20 ), 0x0e);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x06 + ( ch * 0x20 ), 0xa6);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x07 + ( ch * 0x20 ), 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x08 + ( ch * 0x20 ), 0x96);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0a + ( ch * 0x20 ), 0x07);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0b + ( ch * 0x20 ), 0x98);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0c + ( ch * 0x20 ), 0x07);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0d + ( ch * 0x20 ), 0xbc);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0e + ( ch * 0x20 ), 0x07);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0f + ( ch * 0x20 ), 0xad);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x10 + ( ch * 0x20 ), 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x11 + ( ch * 0x20 ), 0xfa);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x12 + ( ch * 0x20 ), 0x01);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x13 + ( ch * 0x20 ), 0x22);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x14 + ( ch * 0x20 ), 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x15 + ( ch * 0x20 ), 0x6e);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + ( ch * 0x20 ), 0x0f);
		}
		else
		{
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x04 + ( ch * 0x20 ), 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + ( ch * 0x20 ), 0x00);
		}

		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF,0x09);
		val_9x44 = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x44);
		val_9x44 &= ~(1 << ch);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x44 , val_9x44);
	}

	if( pvin_eq_set->FmtDef == AHD20_1080P_15P_EX || pvin_eq_set->FmtDef == AHD20_1080P_12_5P_EX  )
	{
		unsigned char val_1x7a = 0x00;
		unsigned char val_11x00 = 0x00;

		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF,0x01);
		val_1x7a = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x7a);
		val_1x7a &= ~(0x1 << ch);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7A, val_1x7a);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF,0x11);
		val_11x00 = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x00 + (ch*0x20));
		val_11x00 |= 0x10;
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + (ch * 0x20) , val_11x00);
	}
	else
	{
		unsigned char val_11x00 = 0x00;
		unsigned char val_1x7a = 0x00;

		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF,0x01);
		val_1x7a = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x7a);
		val_1x7a |= (0x1 << ch);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7A, val_1x7a);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF,0x11);
		val_11x00 = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x00 + (ch*0x20));
		val_11x00 &= ~0x10;
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + (ch * 0x20) , val_11x00);
	}
	return 0;
}

void __nvp6168_set_eq_ext_val(video_equalizer_info_s *pvin_eq_set)
{
	int devnum = pvin_eq_set->devnum;
	int ch = pvin_eq_set->Ch;
	unsigned char tmp_val;

	// 0x54 : FIELD_INV
	// 0x69 : SD_FREQ - always 0
	// 0x22 : COLOR_OFF/C_KILL
	// 0x30 : Y_DELAY_1
	// 0x5C : V_DELAY_1
	// 5x05 : About AGC
	// 5x7B : SD:0x00, Others:0x11
	// 13x74/75 : HSYNC_FALLING MIN/MAX
	// 13x76/77 : HSYNC_RISING MIN/MAX
	// 13x78/79 : SHORT-HSYNC_FALLING MIN/MAX

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
	tmp_val = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x54);
	tmp_val &= ~(0x01<<(ch+4));
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x54, tmp_val);

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x69, 0x00);

	switch(pvin_eq_set->FmtDef)
	{
		// CVBS
		case AHD20_SD_H960_2EX_Btype_NT :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x10);
			tmp_val = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x54);
			tmp_val |= 0x01<<(ch+4);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x54, tmp_val);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0xD0);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD20_SD_H960_2EX_Btype_PAL :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x10);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0xBF);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;

		// AHD
		case AHD20_720P_30P_EX_Btype :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x12);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD20_720P_25P_EX_Btype :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x12);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD20_1080P_30P :
		case AHD20_1080P_15P_EX :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD20_1080P_25P :
		case AHD20_1080P_12_5P_EX :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD20_1080P_60P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case AHD20_1080P_50P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case AHD30_4M_30P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD30_4M_25P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD30_4M_15P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x16);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD30_3M_30P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD30_3M_25P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD30_3M_18P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x16);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD30_5M_12_5P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x16);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD30_5M_20P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD30_5_3M_20P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD30_8M_12_5P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD30_8M_15P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case AHD20_960P_30P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD20_960P_25P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case AHD20_960P_60P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case AHD20_960P_50P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;

		// TVI
		case TVI_FHD_30P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_FHD_25P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_HD_30P_EX :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_HD_25P_EX :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_HD_B_30P_EX :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x16);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_HD_B_25P_EX :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x16);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_HD_60P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x16);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_HD_50P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x16);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_3M_18P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x16);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_5M_12_5P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x16);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_5M_20P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_4M_30P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_4M_25P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_4M_15P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x16);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_8M_15P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;
		case TVI_8M_12_5P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
			break;

		// CVI
		case CVI_FHD_30P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			break;
		case CVI_FHD_25P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case CVI_HD_60P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case CVI_HD_50P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case CVI_HD_30P_EX :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x16);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case CVI_HD_25P_EX :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x16);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case CVI_4M_30P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case CVI_4M_25P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case CVI_8M_15P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;
		case CVI_8M_12_5P :
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x22+(ch*4), 0x02);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x30 + ch, 0x17);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x34 + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x5c + ch, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05+ch);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05, 0x24);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x7B, 0x11);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB5, 0x80);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x77, 0xff);
			break;

		case AHD20_SD_H960_NT :
		case AHD20_SD_H960_PAL :
		case AHD20_SD_SH720_NT :
		case AHD20_SD_SH720_PAL :
		case AHD20_SD_H1280_NT :
		case AHD20_SD_H1280_PAL :
		case AHD20_SD_H1440_NT :
		case AHD20_SD_H1440_PAL :
		case AHD20_SD_H960_EX_NT :
		case AHD20_SD_H960_EX_PAL :
		case AHD20_SD_H960_2EX_NT :
		case AHD20_SD_H960_2EX_PAL :
		case AHD20_720P_60P :
		case AHD20_720P_50P :
		case AHD20_720P_30P :
		case AHD20_720P_25P :
		case AHD20_720P_30P_EX :
		case AHD20_720P_25P_EX :
		case AHD30_6M_18P :
		case AHD30_6M_20P :
		case AHD30_8M_X_30P :
		case AHD30_8M_X_25P :
		case AHD30_8M_7_5P :
		case TVI_HD_30P :
		case TVI_HD_25P :
		case TVI_HD_B_30P :
		case TVI_HD_B_25P :
		case CVI_HD_30P :
		case CVI_HD_25P :
		case TVI_FHD_60P :
#if 0
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
#endif
		case TVI_FHD_50P :
#if 0
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x79, 0xff);
#endif

		default :
			break;
	}
}

int nvp6168_set_equalizer(video_equalizer_info_s *pvin_eq_set)
{
	unsigned char val_9x44, ii;
	unsigned char ch = pvin_eq_set->Ch;
	unsigned char devnum = pvin_eq_set->devnum;

	decoder_dev_ch_info_s pDecoder_info;

	video_equalizer_value_table_s eq_value;// = (video_equalizer_value_table_s)nvp6168_equalizer_value_fmtdef[pvin_eq_set->FmtDef];

	/* cable type => 0:coaxial, 1:utp, 2:reserved1, 3:reserved2 */
	//video_equalizer_value_table_s eq_value = (video_equalizer_value_table_s)nvp6158_equalizer_value_fmtdef[pvin_eq_set->FmtDef];
	memset(&eq_value, 0xFF,sizeof(video_equalizer_value_table_s));
	memcpy(&eq_value,&nvp6168_equalizer_value_fmtdef[pvin_eq_set->FmtDef],sizeof(video_equalizer_value_table_s));
	if(0xFF == eq_value.eq_base.eq_band_sel[pvin_eq_set->distance] || 0x00 == eq_value.eq_base.eq_bypass[pvin_eq_set->distance]) //if 5x58==0xFF it's not a valid value.
	{
		printk("func[%s] eq_value[fmt:%d] not found\n", __func__, pvin_eq_set->FmtDef);
		return -1;
	}

	/* set eq value */
	__nvp6158_eq_base_set_value( pvin_eq_set, &eq_value.eq_base );
	__nvp6158_eq_coeff_set_value( pvin_eq_set, &eq_value.eq_coeff );
	__nvp6158_eq_color_set_value( pvin_eq_set, &eq_value.eq_color);
	__nvp6158_eq_timing_a_set_value( pvin_eq_set, &eq_value.eq_timing_a );
	__nvp6158_eq_clk_set_value( pvin_eq_set, &eq_value.eq_clk );
	__nvp6158_eq_timing_b_set_value( pvin_eq_set, &eq_value.eq_timing_b );

	__nvp6168_set_eq_ext_val(pvin_eq_set);

	if(nvp6158_audio_in_type_get() == NC_AD_AOC)
	{
		pDecoder_info.ch = ch;
		pDecoder_info.devnum = devnum;
		pDecoder_info.fmt_def = pvin_eq_set->FmtDef;

		nvp6158_audio_set_aoc_format(&pDecoder_info);
	}

	for(ii=0;ii<0x16;ii++)
	{
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x11);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + ( ch * 0x20 ) + ii, 0x00);   //first set bank11 to default values.
	}


	if( pvin_eq_set->FmtDef == CVI_5M_20P)
	{
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x11);

		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x01 + ( ch * 0x20 ), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x02 + ( ch * 0x20 ), 0x30);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x03 + ( ch * 0x20 ), 0x0a);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x04 + ( ch * 0x20 ), 0x20);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x05 + ( ch * 0x20 ), 0x0e);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x06 + ( ch * 0x20 ), 0xa6);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x07 + ( ch * 0x20 ), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x08 + ( ch * 0x20 ), 0x96);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0a + ( ch * 0x20 ), 0x07);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0b + ( ch * 0x20 ), 0x98);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0c + ( ch * 0x20 ), 0x07);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0d + ( ch * 0x20 ), 0xbc);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0e + ( ch * 0x20 ), 0x07);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x0f + ( ch * 0x20 ), 0xad);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x10 + ( ch * 0x20 ), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x11 + ( ch * 0x20 ), 0xfa);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x12 + ( ch * 0x20 ), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x13 + ( ch * 0x20 ), 0x22);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x14 + ( ch * 0x20 ), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x15 + ( ch * 0x20 ), 0x6e);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + ( ch * 0x20 ), 0x0f);
	}
	else if(pvin_eq_set->FmtDef == AHD20_1080P_15P_EX || pvin_eq_set->FmtDef == AHD20_1080P_12_5P_EX)
	{
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x11);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + ( ch * 0x20 ), 0x10);
	}

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF,0x09);
	val_9x44 = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x44);
	val_9x44 &= ~(1 << ch);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x44 , val_9x44);

	if(pvin_eq_set->FmtDef == TVI_4M_15P )
	{
		gpio_i2c_write( nvp6158_iic_addr[devnum], 0xFF, 0x05 + ch );
		gpio_i2c_write( nvp6158_iic_addr[devnum], 0x6E, 0x10 );    //VBLK setting
		gpio_i2c_write( nvp6158_iic_addr[devnum], 0x6F, 0x7e );
	}
	else
	{
		gpio_i2c_write( nvp6158_iic_addr[devnum], 0xFF, 0x05 + ch );
		gpio_i2c_write( nvp6158_iic_addr[devnum], 0x6E, 0x00 );    //VBLK default setting
		gpio_i2c_write( nvp6158_iic_addr[devnum], 0x6F, 0x00 );
	}

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05 + ch);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xB8, 0x39);

	return 0;
}

