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
#include "nvp6158_video_auto_detect.h"
#include "nvp6158_video.h"

#define ACC_GAIN_NORMAL 0
#define ACC_GAIN_DEBUG  1

extern int nvp6158_chip_id[4];
extern unsigned int nvp6158_iic_addr[4];

NC_VIVO_CH_FORMATDEF nvp6158_arrVfcType[0x100] = {
	/*  0x00 */	AHD20_SD_H960_2EX_Btype_NT,	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x10 */ 	AHD20_SD_H960_2EX_Btype_PAL,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x20 */	AHD20_720P_30P_EX_Btype, AHD20_720P_25P_EX_Btype, AHD20_720P_60P, AHD20_720P_50P,0,
	/*  0x25 */ 	TVI_HD_30P_EX, TVI_HD_25P_EX, TVI_HD_60P, TVI_HD_50P, TVI_HD_B_30P_EX, TVI_HD_B_25P_EX,
	/*  0x2B */	CVI_HD_30P_EX, CVI_HD_25P_EX, CVI_HD_60P, CVI_HD_50P,0,
	/*  0x30 */	AHD20_1080P_30P, AHD20_1080P_25P, 0,TVI_FHD_30P, TVI_FHD_25P,CVI_FHD_30P, CVI_FHD_25P, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x40 */	AHD30_3M_30P, AHD30_3M_25P, AHD30_3M_18P, 0,TVI_3M_18P, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, TVI_4M_15P,
	/*  0x50 */	AHD30_4M_30P, AHD30_4M_25P, AHD30_4M_15P, 0,CVI_4M_30P, CVI_4M_25P, 0,TVI_4M_30P, TVI_4M_25P, TVI_4M_15P, 0, 0, 0, 0, 0, 0,
	/*  0x60 */	AHD30_8M_X_30P, AHD30_8M_X_25P, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x70 */	AHD30_5M_20P, AHD30_5M_12_5P, AHD30_5_3M_20P, TVI_5M_12_5P, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x80 */	AHD30_8M_15P, AHD30_8M_7_5P, AHD30_8M_12_5P, CVI_8M_15P, CVI_8M_12_5P, TVI_8M_15P, TVI_8M_12_5P, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x90 */	AHD30_6M_18P, AHD30_6M_20P, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0xA0 */
};

NC_VIVO_CH_FORMATDEF nvp6158_arrVfcType_ahd[0x100] = {
	/*  0x00 */	AHD20_SD_H960_2EX_Btype_NT,	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x10 */ 	AHD20_SD_H960_2EX_Btype_PAL,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x20 */	AHD20_720P_30P_EX_Btype, AHD20_720P_25P_EX_Btype, AHD20_720P_60P, AHD20_720P_50P,0,
	/*  0x25 */ 	0, 0, 0, 0, 0, 0,
	/*  0x2B */	AHD20_720P_30P_EX_Btype, AHD20_720P_25P_EX_Btype, AHD20_720P_60P, AHD20_720P_50P,0,
	/*  0x30 */	AHD20_1080P_30P, AHD20_1080P_25P, 0,0, 0,AHD20_1080P_30P, AHD20_1080P_25P, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x40 */	AHD30_3M_30P, AHD30_3M_25P, AHD30_3M_18P, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x50 */	AHD30_4M_30P, AHD30_4M_25P, AHD30_4M_15P, 0,0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x60 */	AHD30_8M_X_30P, AHD30_8M_X_25P, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x70 */	AHD30_5M_20P, AHD30_5M_12_5P, AHD30_5_3M_20P, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x80 */	AHD30_8M_15P, AHD30_8M_7_5P, AHD30_8M_12_5P, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x90 */	AHD30_6M_18P, AHD30_6M_20P, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0xA0 */
};

NC_VIVO_CH_FORMATDEF nvp6158_arrVfcType_cvi[0x100] = {
	/*  0x00 */	AHD20_SD_H960_2EX_Btype_NT,	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x10 */ 	AHD20_SD_H960_2EX_Btype_PAL,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x20 */	CVI_HD_30P_EX, CVI_HD_25P_EX, CVI_HD_60P, CVI_HD_50P,0,
	/*  0x25 */ 	0, 0, 0, 0, 0, 0,
	/*  0x2B */	CVI_HD_30P_EX, CVI_HD_25P_EX, CVI_HD_60P, CVI_HD_50P,0,
	/*  0x30 */	CVI_FHD_30P, CVI_FHD_25P, 0,0, 0,CVI_FHD_30P, CVI_FHD_25P, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x40 */	0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x50 */	0, 0, 0, 0,CVI_4M_30P, CVI_4M_25P, 0,0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x60 */	0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x70 */	CVI_5M_20P, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x80 */	0, 0, 0, CVI_8M_15P, CVI_8M_12_5P, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x90 */	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0xA0 */
};

NC_VIVO_CH_FORMATDEF nvp6158_arrVfcType_tvi[0x100] = {
	/*  0x00 */	AHD20_SD_H960_2EX_Btype_NT,	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x10 */ 	AHD20_SD_H960_2EX_Btype_PAL,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x20 */	0, 0, 0, 0, 0,
	/*  0x25 */ 	TVI_HD_30P_EX, TVI_HD_25P_EX, TVI_HD_60P, TVI_HD_50P, TVI_HD_B_30P_EX, TVI_HD_B_25P_EX,
	/*  0x2B */	TVI_4M_15P, 0, 0, 0,0,
	/*  0x30 */	0, 0, 0,TVI_FHD_30P, TVI_FHD_25P,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x40 */	0, 0, 0, 0,TVI_3M_18P, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, TVI_4M_15P,
	/*  0x50 */	0, 0, 0, 0, 0, 0, 0, TVI_4M_30P, TVI_4M_25P, TVI_4M_15P, 0, 0, 0, 0, 0, 0,
	/*  0x60 */	0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x70 */	TVI_5M_20P, 0, 0, TVI_5M_12_5P, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x80 */	0, 0, 0, TVI_8M_15P, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x90 */	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0xA0 */
};

NC_VIVO_CH_FORMATDEF nvp6158_arrVfcType_raptor4[0x100] = {
	/*  0x00 */	AHD20_SD_H960_2EX_Btype_NT,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x10 */ AHD20_SD_H960_2EX_Btype_PAL,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x20 */	AHD20_720P_30P_EX_Btype, AHD20_720P_25P_EX_Btype, AHD20_720P_60P, AHD20_720P_50P,
				0,
	/*  0x25 */ TVI_HD_30P_EX, TVI_HD_25P_EX, TVI_HD_60P, TVI_HD_50P, TVI_HD_B_30P_EX, TVI_HD_B_25P_EX,
	/*  0x2B */	CVI_HD_30P_EX, CVI_HD_25P_EX, CVI_HD_60P, CVI_HD_50P,
				0,
	/*  0x30 */	AHD20_1080P_30P, AHD20_1080P_25P,
				0,
	/*	0x33 */ TVI_FHD_30P, TVI_FHD_25P,
	/*  0x35 */	CVI_FHD_30P, CVI_FHD_25P,
				0,
	/*  0x38 */ AHD20_1080P_60P, AHD20_1080P_50P,
	/*  0x3A */ TVI_FHD_60P, TVI_FHD_50P,
	/*  0x3C */ AHD20_1080P_15P_EX, AHD20_1080P_12_5P_EX,
				0, 0,
	/*  0x40 */	AHD30_3M_30P, AHD30_3M_25P, AHD30_3M_18P,
				0,
	/*  0x44 */ TVI_3M_18P,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x50 */	AHD30_4M_30P, AHD30_4M_25P, AHD30_4M_15P,
				0,
	/*  0x54 */	CVI_4M_30P, CVI_4M_25P,
				0,
	/*  0x57 */ TVI_4M_30P, TVI_4M_25P, TVI_4M_15P,
				0, 0, 0, 0, 0, 0,
	/*  0x60 */	AHD30_8M_X_30P, AHD30_8M_X_25P, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x70 */ AHD30_5M_20P, AHD30_5M_12_5P, AHD30_5_3M_20P,
	/*  0x73 */ TVI_5M_12_5P, TVI_5M_20P,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x80 */	AHD30_8M_15P, AHD30_8M_7_5P, AHD30_8M_12_5P, CVI_8M_15P, CVI_8M_12_5P, TVI_8M_15P, TVI_8M_12_5P,
				0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0x90 */	AHD30_6M_18P, AHD30_6M_20P,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*  0xA0 */ AHD20_960P_30P, AHD20_960P_25P, AHD20_960P_60P, AHD20_960P_50P,
};

//unsigned char current_fmt[16] = {NVP6158_DET_MODE_AUTO, NVP6158_DET_MODE_AUTO, NVP6158_DET_MODE_AUTO, NVP6158_DET_MODE_AUTO};
extern unsigned char nvp6158_det_mode[16];
NC_VIVO_CH_FORMATDEF NVP6158_NC_VD_AUTO_VFCtoFMTDEF(unsigned char ch, unsigned char VFC)
{
	if((nvp6158_chip_id[ch/4] == NVP6168C_R0_ID) ||
		(nvp6158_chip_id[ch/4] == NVP6168_R0_ID)) {
		return nvp6158_arrVfcType_raptor4[VFC];
	} else {
		if(nvp6158_det_mode[ch] == NVP6158_DET_MODE_AUTO)
			return nvp6158_arrVfcType[VFC];
		else if(nvp6158_det_mode[ch] == NVP6158_DET_MODE_CVI)
			return nvp6158_arrVfcType_cvi[VFC];
		else if(nvp6158_det_mode[ch] == NVP6158_DET_MODE_TVI)
			return nvp6158_arrVfcType_tvi[VFC];
		else
			return nvp6158_arrVfcType_ahd[VFC];
	}
}

static void _nvp6158_video_input_auto_detect_vafe_set(video_input_auto_detect *vin_auto_det)
{
	unsigned char val_1x7A;
	unsigned char val_5678x00;
	unsigned char val_5678x01;
	unsigned char val_5678x58;
	unsigned char val_5678x59;
	unsigned char val_5678x5B;
	unsigned char val_5678x5C;

	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xFF, 0x01);
	val_1x7A = gpio_i2c_read(nvp6158_iic_addr[vin_auto_det->devnum], 0x7A);
	val_1x7A |= (1 << vin_auto_det->ch);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x7A, val_1x7A);

	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xFF, 0x00);
	//B0 0x00/1/2/3 gain[4], powerdown[0]
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x00 + vin_auto_det->ch,
			((vin_auto_det->vafe.gain & 0x01) << 4) | (vin_auto_det->vafe.powerdown & 0x01));

	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xFF, 0x01);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x84 + vin_auto_det->ch, 0x00);

	///////////////////////////////////////////////////////////////////////////////////////////////////////////

	//B5/6/7/8
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xFF, 0x05 + vin_auto_det->ch);

	//B5/6/7/8 0x01 spd[2], lpf_back_band[1:0]
	val_5678x00 = gpio_i2c_read(nvp6158_iic_addr[vin_auto_det->devnum], 0x00);
	val_5678x00 &= ~(0xF << 4);
	val_5678x00 |= vin_auto_det->vafe.spd << 4;

	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x00, val_5678x00);

	val_5678x01 = ((vin_auto_det->vafe.ctrlreg << 6) | (vin_auto_det->vafe.ctrlibs << 4) |
			(vin_auto_det->vafe.adcspd << 2) | (vin_auto_det->vafe.clplevel));
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x01, val_5678x01 );

	//B5/6/7/8 0x58 eq_band[7:4], lpf_front_band[1:0]
	val_5678x58 = ((vin_auto_det->vafe.eq_band << 4) | (vin_auto_det->vafe.lpf_front_band));
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x58, val_5678x58);

	//B5/6/7/8 0x5B ref_vol[1:0]
	val_5678x59 = ((vin_auto_det->vafe.clpmode << 7) | (vin_auto_det->vafe.f_lpf_bypass << 4) |
			(vin_auto_det->vafe.clproff << 3) | (vin_auto_det->vafe.b_lpf_bypass));
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x59, val_5678x59);

	val_5678x5B = ((vin_auto_det->vafe.duty << 4) | (vin_auto_det->vafe.ref_vol));
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x5B, val_5678x5B);

	val_5678x5C = ((vin_auto_det->vafe.lpf_back_band << 4) |
		(vin_auto_det->vafe.clk_sel << 3) | (vin_auto_det->vafe.eq_gainsel));
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x5C, val_5678x5C);
}

void nvp6158_video_input_manual_mode_set(video_input_manual_mode *vin_manual_det)
{
	unsigned char val_0x30;
	unsigned char val_0x31;
	unsigned char val_0x32;

	unsigned char val_1x7A;

	unsigned char val_9x44;
	//B13 0x30 AUTO_FMT_SET_EN_2[3:0], AUTO_FMT_SET_EN    [3:0]
	//B13 0x31 AUTO_FMT_SET_EN_4[3:0], AUTO_FMT_SET_EN_3  [3:0]
	//B13 0x32 [	  RESERVED  	], NOVIDEO_VFC_INIT_EN[3:0]

	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0xFF, 0x01);
	val_1x7A = gpio_i2c_read(nvp6158_iic_addr[vin_manual_det->dev_num], 0x7A);
	val_1x7A &= ~(1 << vin_manual_det->ch);
	val_1x7A |= (1 << vin_manual_det->ch);
	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0x7A, val_1x7A);

	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0xFF, 0x00);
	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0x23 + (vin_manual_det->ch*4), 0x41);

	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0xFF, 0x13);
	val_0x30 = gpio_i2c_read(nvp6158_iic_addr[vin_manual_det->dev_num], 0x30);
	val_0x31 = gpio_i2c_read(nvp6158_iic_addr[vin_manual_det->dev_num], 0x31);
	val_0x32 = gpio_i2c_read(nvp6158_iic_addr[vin_manual_det->dev_num], 0x32);

	val_0x30 &= (~(1 << (vin_manual_det->ch + 4)) & (~(1 << vin_manual_det->ch)));
	val_0x31 &= (~(1 << (vin_manual_det->ch + 4)) & (~(1 << vin_manual_det->ch)));
	val_0x32 &= (~(1 << vin_manual_det->ch));

	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0x30, val_0x30);
	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0x31, val_0x31);
	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0x32, val_0x32);

	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0xFF, 0x05 + vin_manual_det->ch);
	//B5/6/7/8 0xB9 HAFC_LPF_SEL[7:6] GAIN1[5:4] GAIN2[3:0]
	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0xB9, 0xB2);


	// EXT PN VALUE Disable
	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0xFF,0x09);
	val_9x44 = gpio_i2c_read(nvp6158_iic_addr[vin_manual_det->dev_num], 0x44);
	val_9x44 &= ~(1 << vin_manual_det->ch);
	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0x44, val_9x44);
}

void nvp6168_video_input_manual_mode_set(video_input_manual_mode *vin_manual_det)
{
	unsigned char val_1x7A;
	unsigned char val_9x44;

	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0xFF, 0x01);
	val_1x7A = gpio_i2c_read(nvp6158_iic_addr[vin_manual_det->dev_num], 0x7A);
	val_1x7A &= ~(1 << vin_manual_det->ch);
	val_1x7A |= (1 << vin_manual_det->ch);
	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0x7A, val_1x7A);

	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0xFF, 0x13);
	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0x30, 0x00);
	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0x70, 0x00);

	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0xFF, 0x05 + vin_manual_det->ch);
	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0xB9, 0x72);

	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0xFF, 0x09);
	val_9x44 = gpio_i2c_read(nvp6158_iic_addr[vin_manual_det->dev_num], 0x44);
	val_9x44 &= ~(1 << vin_manual_det->ch);
	gpio_i2c_write(nvp6158_iic_addr[vin_manual_det->dev_num], 0x44, val_9x44);
}

void nvp6158_video_input_auto_detect_set(video_input_auto_detect *vin_auto_det)
{
	unsigned char val_0x30;
	unsigned char val_0x31;
	unsigned char val_0x32;

	vin_auto_det->vafe.powerdown 		= 0x00;
	vin_auto_det->vafe.gain		 		= 0x01;
	vin_auto_det->vafe.spd		 		= 0x0d;
	vin_auto_det->vafe.ctrlreg 	 		= 0x01;
	vin_auto_det->vafe.ctrlibs	 		= 0x02;
	vin_auto_det->vafe.adcspd	 		= 0x00;
	vin_auto_det->vafe.clplevel  		= 0x02;
	vin_auto_det->vafe.eq_band	 		= 0x00;
	vin_auto_det->vafe.lpf_front_band 	= 0x07;
	vin_auto_det->vafe.clpmode   		= 0x00;
	vin_auto_det->vafe.f_lpf_bypass 	= 0x01;
	vin_auto_det->vafe.clproff 			= 0x00;
	vin_auto_det->vafe.b_lpf_bypass 	= 0x00;
	vin_auto_det->vafe.duty				= 0x04;
	vin_auto_det->vafe.ref_vol			= 0x03;
	vin_auto_det->vafe.lpf_back_band	= 0x07;
	vin_auto_det->vafe.clk_sel			= 0x01;
	vin_auto_det->vafe.eq_gainsel		= 0x07;

	vin_auto_det->d_cmp					= 0x3f;
	vin_auto_det->slice_level			= 0x5a;
	vin_auto_det->stable_mode_1			= 0x04;
	vin_auto_det->stable_mode_2			= 0x00;
	vin_auto_det->novid_det				= 0x41;

	_nvp6158_video_input_auto_detect_vafe_set(vin_auto_det);

	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xFF, 0x05 + vin_auto_det->ch);

	//B5/6/7/8 0x03 Digital Clamp
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x03, vin_auto_det->d_cmp);
	//B5/6/7/8 0x08 Slice Level
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x08, vin_auto_det->slice_level);
	//B5/6/7/8 0xB9 HAFC_LPF_SEL[7:6] GAIN1[5:4] GAIN2[3:0]
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xB9, 0x72);

	//B5/6/7/8 0xCA ADV_V_DELAY_AD[4] ADV_V_DELAY_ON[0]
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xCA, 0x10);

	//B13 0x30 AUTO_FMT_SET_EN_2[3:0], AUTO_FMT_SET_EN    [3:0]
	//B13 0x31 AUTO_FMT_SET_EN_4[3:0], AUTO_FMT_SET_EN_3  [3:0]
	//B13 0x32 [	  RESERVED  	], NOVIDEO_VFC_INIT_EN[3:0]

	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xFF, 0x13);
	val_0x30 = gpio_i2c_read(nvp6158_iic_addr[vin_auto_det->devnum], 0x30);
	val_0x31 = gpio_i2c_read(nvp6158_iic_addr[vin_auto_det->devnum], 0x31);
	val_0x32 = gpio_i2c_read(nvp6158_iic_addr[vin_auto_det->devnum], 0x32);
	val_0x30 |= ((1 << (vin_auto_det->ch + 4)) | (1 << vin_auto_det->ch));
	val_0x31 |= ((1 << (vin_auto_det->ch + 4)) | (1 << vin_auto_det->ch));
	val_0x32 |= ((1 << vin_auto_det->ch) & 0xF);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x30, val_0x30);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x31, val_0x31);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x32, val_0x32);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x36, 0x0A);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x37, 0x82);

	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xFF, 0x00);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x81+vin_auto_det->ch, 0x0A);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x85+vin_auto_det->ch, 0x02);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////

	//B13
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xFF, 0x13);
	//B13 0x00  Stable Mode set
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x00, vin_auto_det->stable_mode_1);
	//B13 0x01  Stable Mode Set
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x01, ((vin_auto_det->stable_mode_2) & 0x3));
	//B13 0x40 VFC_EQ_BAND_SEL[7:4] VFC_LPF_F_SEL[1:0]
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x40, 0x07);
	//B13 0x41 VFC_REF_VTG
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x41, 0x01);
	//B13 0x42 VFC_D_CMP_SET
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x42, 0x3F);
	//B13 0x43 VFC_SLICE_VALUE
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x43, 0x5A);
	//B13 0x44 VFC_SLICE_MD2
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x44, 0x30);
	//B13 0x45 VFC_CONTROL_MODES
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x45, 0xEE);
	//B13 0x46 VFC_GDF_FIX_COEFF
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x46, 0xC6);
	//B13 0x47 VFC_DFE_REF_SEL_OLD[4] VFC_DFE_REF_SEL_NEW[0]
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x47, 0x00);
	//B13 0x48 VFC_D_BLK_CNT_NEW[[7:4] VFC_HAFC_BYPASS_NEW[1] VFC_UPDN_SEL[0]
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x48, 0x80);
	//B13 0x49 VFC_OLD_WPD_ON[4] VFC_NEW_WPD_ON[0]
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x49, 0x00);
	//B13 0x4A VFC_D_CMP_FZ_OLD[4] VFC_D_CMP_FZ_NEW[1]
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x4A, 0x11);
	//B13 0x4B VFC_AUTO_GNOS_MODE
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x4B, 0x7F);
	//B13 0x4C VFC_AUTO_SYNC_MODE
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x4C, 0x00);
	//B13 0x4D VFC_HAFC_BYPASS[7] ??? [6:0]
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x4D, 0xB9);
	//B13 0x4E VFC_VAFE_B_LPF_SEL[6:4] VFC_VAFE_CKSEL[3] VFC_VAFE_EQ_GAIN_SEL[2:0]
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x4E, 0x78);
	//B13 0x4F VFC_VAFE_CTRL_RES[7:6] VFC_VAFE_IBS_CTRL[5:4] VFC_VAFE_SPD[2] VFC_VAFE_CLP_LEVEL[1:0]
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x4F, 0x62);

	//B0  0x23/0x27/0x2B/0x2F No Video Detect
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xFF, 0x0);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x23 + ((vin_auto_det->ch) * 4), vin_auto_det->novid_det);

	/* clock set */
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xFF, 0x1);
    	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x84 + vin_auto_det->ch, 0x00);
    	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x8c + vin_auto_det->ch, 0x55);

}

void nvp6168_video_input_auto_detect_set(video_input_auto_detect *vin_auto_det)
{
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xFF, 0x13);

	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x12, 0x04);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x2E, 0x10);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x30, 0x7f);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x31, 0xff);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x32, 0xff);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x33, 0xff);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x77, 0xff);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x3a, 0xff);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x3b, 0xff);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x3c, 0xff);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x3d, 0xff);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x3e, 0xff);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x3f, 0xff);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x70, 0xf0);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x72, 0x05);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x7A, 0x10);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x61, 0x0A);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x62, 0x02);
	//gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x60, 0x01);
	//gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x60, 0x00);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x07, 0x47);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x59, 0x24);

	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x01, 0x0c);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x2f, 0xc8);

	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x73, 0x23);

	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xff, 0x09 );
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0x96, 0x03);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xB6, 0x03);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xD6, 0x03);
	gpio_i2c_write(nvp6158_iic_addr[vin_auto_det->devnum], 0xF6, 0x03);
}

void nvp6158_video_input_vfc_read(video_input_vfc *vin_vfc)
{
	gpio_i2c_write(nvp6158_iic_addr[vin_vfc->devnum], 0xFF, 0x13);
	vin_vfc->vfc = gpio_i2c_read(nvp6158_iic_addr[vin_vfc->devnum], 0xF0 + vin_vfc->ch);
}

void nvp6168_video_input_vfc_read(video_input_vfc *vin_vfc)
{
	gpio_i2c_write(nvp6158_iic_addr[vin_vfc->devnum], 0xFF, 0x05 + vin_vfc->ch);
	vin_vfc->vfc = gpio_i2c_read(nvp6158_iic_addr[vin_vfc->devnum], 0xF0);
}

void nvp6158_video_input_novid_read(video_input_novid *vin_novid)
{
	unsigned char val_0xA8;

	gpio_i2c_write(nvp6158_iic_addr[vin_novid->devnum], 0xFF, 0x00);
	val_0xA8 = gpio_i2c_read(nvp6158_iic_addr[vin_novid->devnum], 0xA8);

	vin_novid->novid = (((val_0xA8 >> vin_novid->ch) & 0x1)) ;
}


void nvp6158_video_input_no_video_set(video_input_novid *auto_novid)
{
	unsigned char val_13x30;
	unsigned char val_13x31;
	unsigned char val_13x32;
	unsigned char val_9x44;
	unsigned char val_1x7A;

	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x00);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x21 + (auto_novid->ch * 4), 0x82);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x23 + (auto_novid->ch * 4), 0x41);

	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x09);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x80 + (auto_novid->ch * 0x20), 0x00);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x81 + (auto_novid->ch * 0x20), 0x00);

	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x0A+auto_novid->ch/2);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x3D + ((auto_novid->ch%2) * 0x80), 0x00);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x3C + ((auto_novid->ch%2) * 0x80), 0x00);

	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x05 + auto_novid->ch);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x2C, 0x00);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x01, 0x62);

	/* Before 08/28 */
	//gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x58, 0x07);
	//gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x5C, 0x78);
	/* After 08/28 */
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x47, 0xEE);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x50, 0xc6);  //recovery to std value.
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x58, 0x47);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x5C, 0x7f);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x6E, 0x00);    //VBLK default setting
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x6F, 0x00);
        /* Low-Poass Filter (LPF) Bypass Enable  Bank5/6/7/8 0x59 */
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x59, 0x10);

	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xB8, 0xB9);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xB9, 0xB2);

	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x13);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x43, 0x5a);

	val_13x30 = gpio_i2c_read(nvp6158_iic_addr[auto_novid->devnum], 0x30);
	val_13x31 = gpio_i2c_read(nvp6158_iic_addr[auto_novid->devnum], 0x31);
	val_13x32 = gpio_i2c_read(nvp6158_iic_addr[auto_novid->devnum], 0x32);
	val_13x30 |= ((1 << (auto_novid->ch + 4)) | (1 << auto_novid->ch));
	val_13x31 |= ((1 << (auto_novid->ch + 4)) | (1 << auto_novid->ch));
	val_13x32 |= ((1 << auto_novid->ch) & 0xF);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x30, val_13x30);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x31, val_13x31);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x32, val_13x32);

	/* disable Bank11 0x00, if before setting format TVI 5M 20P when onvideo */
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x11);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x00 + ( auto_novid->ch * 0x20 ), 0x00);

	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x09);
	val_9x44 = gpio_i2c_read(nvp6158_iic_addr[auto_novid->devnum], 0x44);
	val_9x44 |= (1 << auto_novid->ch);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x50 + (auto_novid->ch*4) , 0x30);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x51 + (auto_novid->ch*4) , 0x6F);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x52 + (auto_novid->ch*4) , 0x67);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x53 + (auto_novid->ch*4) , 0x48);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x44 , val_9x44);

	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x00);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x81+auto_novid->ch, 0x0A);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x85+auto_novid->ch, 0x02);
	/* clock set */
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x01);
    	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x84 + auto_novid->ch, 0x00);
    	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x8c + auto_novid->ch, 0x55);
	val_1x7A = gpio_i2c_read(nvp6158_iic_addr[auto_novid->devnum], 0x7A);
	val_1x7A |= (1 << auto_novid->ch);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x7A, val_1x7A);
}

void nvp6168_video_input_no_video_set(video_input_novid *auto_novid)
{
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x00);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x21 + (auto_novid->ch * 4), 0x82);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x23 + (auto_novid->ch * 4), 0x41);

	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x09);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x80 + (auto_novid->ch * 0x20), 0x00);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x81 + (auto_novid->ch * 0x20), 0x00);

	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xff, (auto_novid->ch < 2 ? 0x0a : 0x0b) );
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x3d + (auto_novid->ch%2 * 0x80), 0x00);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x3c + (auto_novid->ch%2 * 0x80), 0x00);

	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x05 + auto_novid->ch);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x01, 0x62);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x2C, 0x00);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x50, 0xc6);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x58, 0x47);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x59, 0x10);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x5C, 0x7f);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xB8, 0xB8);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xB9, 0x72);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x6E, 0x00);    //VBLK default setting
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x6F, 0x00);

	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0xFF, 0x11);
	gpio_i2c_write(nvp6158_iic_addr[auto_novid->devnum], 0x00 + ( auto_novid->ch * 0x20 ), 0x00);
}

void nvp6158_video_input_cable_dist_read(video_input_cable_dist *vin_cable_dist)
{
	gpio_i2c_write(nvp6158_iic_addr[vin_cable_dist->devnum], 0xFF, 0x13);

	//B13 0xA0 [3:0] Cable Distance Value
	vin_cable_dist->dist = gpio_i2c_read(nvp6158_iic_addr[vin_cable_dist->devnum], 0xA0 + vin_cable_dist->ch ) & 0xF;
}



void nvp6158_video_input_sam_val_read(video_input_sam_val *vin_sam_val )
{
	unsigned char val1, val2;

	// Channel Change Sequence
	gpio_i2c_write(nvp6158_iic_addr[vin_sam_val->devnum], 0xFF, 0x13);
	gpio_i2c_write(nvp6158_iic_addr[vin_sam_val->devnum], 0x2B, vin_sam_val->ch);

	gpio_i2c_write(nvp6158_iic_addr[vin_sam_val->devnum], 0xFF, 0x13); /* + vin_sam_val->ch ); */
	//B13 0xC9 [7:0] SAM Value
	val1 = gpio_i2c_read(nvp6158_iic_addr[vin_sam_val->devnum], 0xC9) ;
	//B13 0xC8 [9:8] SAM Value
	val2 = gpio_i2c_read(nvp6158_iic_addr[vin_sam_val->devnum], 0xC8) & 0x3;

	vin_sam_val->sam_val = ((val2 << 8) | val1);
}

void nvp6158_video_input_hsync_accum_read(video_input_hsync_accum *vin_hsync_accum )
{
	unsigned char val01, val02, val03, val04;
	unsigned char val11, val12, val13, val14;

	unsigned char h_lock;
	unsigned int val_1;
	unsigned int val_2;
	unsigned int val_result;

	gpio_i2c_write(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xFF, 0x00);
	h_lock = (gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xE2) >> vin_hsync_accum->ch) & 0x1;

	vin_hsync_accum->h_lock = h_lock;

	gpio_i2c_write(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xFF, 0x13);
	gpio_i2c_write(nvp6158_iic_addr[vin_hsync_accum->devnum], 0x2B, vin_hsync_accum->ch);


	gpio_i2c_write(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xFF, 0x13); 	/* + vin_sam_val->ch  */

	//B13 0xB4 [ 7:0] Hsync Accumulation Value
	val01 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD0);	// 170214 0xB4 -> 0xD0 Fix
	//B13 0xB5 [15:8] Hsync Accumulation Value
	val02 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD1);	// 170214 0xB5 -> 0xD1 Fix
	//B13 0xB6 [23:16] Hsync Accumulation Value
	val03 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD2);	// 170214 0xB6 -> 0xD2 Fix
	//B13 0xB7 [31:24] Hsync Accumulation Value
	val04 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD3);	// 170214 0xB7 -> 0xD3 Fix

	//B13 0xB4 [ 7:0] Hsync Accumulation Value
	val11 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD4);	// 170214 0xB8 -> 0xD4 Fix
	//B13 0xB5 [15:8] Hsync Accumulation Value
	val12 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD5);	// 170214 0xB9 -> 0xD5 Fix
	//B13 0xB6 [23:16] Hsync Accumulation Value
	val13 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD6);	// 170214 0xBA -> 0xD6 Fix
	//B13 0xB7 [31:24] Hsync Accumulation Value
	val14 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD7);	// 170214 0xBB -> 0xD7 Fix

	val_1 = ((val04 << 24) | (val03 << 16) | (val02 << 8) | val01);
	val_2 = ((val14 << 24) | (val13 << 16) | (val12 << 8) | val11);

	val_result = val_1 - val_2;

	vin_hsync_accum->hsync_accum_val1 = val_1;
	vin_hsync_accum->hsync_accum_val2 = val_2;
	vin_hsync_accum->hsync_accum_result = val_result;
}

static int nvp6168_get_eq_read_cnt(unsigned char devnum, unsigned char ch)
{
	unsigned char vfc;
	int ret = 50;

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05 + ch);
	vfc = gpio_i2c_read(nvp6158_iic_addr[devnum], 0xF0);

	switch(vfc)
	{
		// 25fps, 3frame=120ms
		case 0x10 :
		case 0x21 :
		case 0x26 :
		case 0x2A :
		case 0x2C :
		case 0x31 :
		case 0x34 :
		case 0x36 :
		case 0x41 :
		case 0x51 :
		case 0x55 :
		case 0x58 :
		case 0x61 :
		case 0xA1 :
		case 0xA5 :
			ret = 24;
			break;

		// 30fps, 3frame=100ms
		case 0x00 :
		case 0x20 :
		case 0x25 :
		case 0x29 :
		case 0x2B :
		case 0x30 :
		case 0x33 :
		case 0x35 :
		case 0x40 :
		case 0x50 :
		case 0x54 :
		case 0x57 :
		case 0x60 :
		case 0xA0 :
		case 0xA4 :
			ret = 20;
			break;

		// 50fps, 3frame=60ms
		case 0x23 :
		case 0x28 :
		case 0x2E :
		case 0x39 :
		case 0x3B :
		case 0xA3 :
		case 0xA7 :
			ret = 12;
			break;

		// 60fps, 3frame=50ms
		case 0x22 :
		case 0x27 :
		case 0x2D :
		case 0x38 :
		case 0x3A :
		case 0xA2 :
		case 0xA6 :
			ret = 10;
			break;

		// 20fps, 3frame=150ms
		case 0x70 :
		case 0x72 :
		case 0x74 :
			ret = 30;
			break;

		// 18fps, 3frame=167ms
		case 0x42 :
		case 0x44 :
		case 0x90 :
			ret = 34;
			break;

		// 15fps, 3frame=200ms
		case 0x3C :
		case 0x52 :
		case 0x59 :
		case 0x80 :
		case 0x83 :
		case 0x85 :
			ret = 40;
			break;

		// 12.5fps, 3frame=240ms
		case 0x3D :
		case 0x71 :
		case 0x73 :
		case 0x82 :
		case 0x84 :
		case 0x86 :
			ret = 48;
			break;

		// 7.5fps, 3frame=400ms
		case 0x81 :
			ret = 80;
			break;

		default :
			break;
	}

	return ret;
}

void nvp6168_video_input_hsync_accum_read(video_input_hsync_accum *vin_hsync_accum )
{
	unsigned char val01, val02, val03, val04;
	unsigned char val11, val12, val13, val14;

	unsigned int val_1 = 0;
	unsigned int val_2 = 0;
	unsigned int val_result;

	static unsigned int pre_val_1;
	static unsigned int pre_val_2;

	unsigned char rst_reg = 1<<vin_hsync_accum->ch;
	unsigned char vfc, video_loss;
	int read_cnt=0, total_cnt;
	video_input_novid s_auto_novid;

	total_cnt = nvp6168_get_eq_read_cnt(vin_hsync_accum->devnum, vin_hsync_accum->ch);

	gpio_i2c_write(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xFF, 0x13);
	gpio_i2c_write(nvp6158_iic_addr[vin_hsync_accum->devnum], 0x2B, vin_hsync_accum->ch);

	while(read_cnt < total_cnt) {
		gpio_i2c_write(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xFF, 0x00);
		video_loss = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xA8);
		video_loss = (((video_loss >> vin_hsync_accum->ch) & 0x1)) ;

		gpio_i2c_write(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xFF, 0x05 + vin_hsync_accum->ch);
		vfc = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xF0);

		if((video_loss == 1) && (vfc == 0xFF)) {
			printk("[%s] CH:%d, video_loss:%02X, vfc:0x%X \r\n", __func__,
				vin_hsync_accum->ch, video_loss, vfc);
			vin_hsync_accum->hsync_accum_val1 = 0;
			vin_hsync_accum->hsync_accum_val2 = 0;
			vin_hsync_accum->hsync_accum_result = 0xffffffff;
			s_auto_novid.ch = vin_hsync_accum->ch;
			s_auto_novid.devnum = vin_hsync_accum->devnum;
			nvp6168_video_input_no_video_set(&s_auto_novid);
			return;
		}

		gpio_i2c_write(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xFF, 0x13);
		//B13 0xB4 [ 7:0] Hsync Accumulation Value
		val01 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD0);	// 170214 0xB4 -> 0xD0 Fix
		//B13 0xB5 [15:8] Hsync Accumulation Value
		val02 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD1);	// 170214 0xB5 -> 0xD1 Fix
		//B13 0xB6 [23:16] Hsync Accumulation Value
		val03 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD2);	// 170214 0xB6 -> 0xD2 Fix
		//B13 0xB7 [31:24] Hsync Accumulation Value
		val04 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD3);	// 170214 0xB7 -> 0xD3 Fix

		//B13 0xB4 [ 7:0] Hsync Accumulation Value
		val11 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD4);	// 170214 0xB8 -> 0xD4 Fix
		//B13 0xB5 [15:8] Hsync Accumulation Value
		val12 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD5);	// 170214 0xB9 -> 0xD5 Fix
		//B13 0xB6 [23:16] Hsync Accumulation Value
		val13 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD6);	// 170214 0xBA -> 0xD6 Fix
		//B13 0xB7 [31:24] Hsync Accumulation Value
		val14 = gpio_i2c_read(nvp6158_iic_addr[vin_hsync_accum->devnum], 0xD7);	// 170214 0xBB -> 0xD7 Fix

		val_1 = ((val04 << 24) | (val03 << 16) | (val02 << 8) | val01);
		val_2 = ((val14 << 24) | (val13 << 16) | (val12 << 8) | val11);

		//printk("[%s] CH:%d, video_loss:%02X, vfc:0x%X val1:%08X / val2:%08X \r\n", __func__, vin_hsync_accum->ch, video_loss, vfc, val_1, val_2);

		if((val_1 != 0) && (val_2 != 0)) {
			if((pre_val_1 != val_1) || (pre_val_2 != val_2)) {
				gpio_i2c_write(nvp6158_iic_addr[vin_hsync_accum->devnum], 0x7A, rst_reg);
				gpio_i2c_write(nvp6158_iic_addr[vin_hsync_accum->devnum], 0x7A, 0x10);
				pre_val_1 = val_1;
				pre_val_2 = val_2;
				break;
			}
		}
		msleep(10);
		read_cnt++;
	}
	val_result = val_1 - val_2;

	vin_hsync_accum->hsync_accum_val1 = val_1;
	vin_hsync_accum->hsync_accum_val2 = val_2;
	vin_hsync_accum->hsync_accum_result = val_result;
}

void nvp6158_video_input_agc_val_read(video_input_agc_val *vin_agc_val) 
{
	unsigned char agc_lock;

	gpio_i2c_write(nvp6158_iic_addr[vin_agc_val->devnum], 0xFF, 0x00);
	agc_lock = (gpio_i2c_read(nvp6158_iic_addr[vin_agc_val->devnum], 0xE0) >> vin_agc_val->ch) & 0x1;

	 vin_agc_val->agc_lock = agc_lock;

	gpio_i2c_write(nvp6158_iic_addr[vin_agc_val->devnum], 0xFF, 0x13);
	gpio_i2c_write(nvp6158_iic_addr[vin_agc_val->devnum], 0x2B, vin_agc_val->ch);

	gpio_i2c_write(nvp6158_iic_addr[vin_agc_val->devnum], 0xFF, 0x13); /* + vin_sam_val->ch ); */

	//B13 0xB8 [ 7:0] Hsync Accumulation Value
	vin_agc_val->agc_val = gpio_i2c_read(nvp6158_iic_addr[vin_agc_val->devnum], 0xC4); // 170213 0xA9 -> 0xC5 // 170310 0xC5 -> 0xC4
}

void nvp6158_video_input_fsc_val_read(video_input_fsc_val *vin_fsc_val)
{
	unsigned char val01, val02, val03, val04;
	unsigned char val11, val12, val13, val14;
	unsigned char val21, val22, val23, val24;

	unsigned int val_1, val_2, val_final;

	// Channel Change Sequence
	gpio_i2c_write(nvp6158_iic_addr[vin_fsc_val->devnum], 0xFF, 0x13);
	gpio_i2c_write(nvp6158_iic_addr[vin_fsc_val->devnum], 0x2B, vin_fsc_val->ch);

	gpio_i2c_write(nvp6158_iic_addr[vin_fsc_val->devnum], 0xFF, 0x13);

	//B13 0xB4 [ 7:0] r_fsc_line_diff_sts
	val01 = gpio_i2c_read(nvp6158_iic_addr[vin_fsc_val->devnum], 0xB4);
	//B13 0xB5 [15:8] r_fsc_line_diff_sts
	val02 = gpio_i2c_read(nvp6158_iic_addr[vin_fsc_val->devnum], 0xB5);
	//B13 0xB6 [23:16] r_fsc_line_diff_sts
	val03 = gpio_i2c_read(nvp6158_iic_addr[vin_fsc_val->devnum], 0xB6);
	//B13 0xB7 [31:24] r_fsc_line_diff_sts
	val04 = gpio_i2c_read(nvp6158_iic_addr[vin_fsc_val->devnum], 0xB7);

	//B13 0xB4 [ 7:0] r_fsc_line2_diff_sts
	val11 = gpio_i2c_read(nvp6158_iic_addr[vin_fsc_val->devnum], 0xB8);
	//B13 0xB5 [15:8] r_fsc_line2_diff_sts
	val12 = gpio_i2c_read(nvp6158_iic_addr[vin_fsc_val->devnum], 0xB9);
	//B13 0xB6 [23:16] r_fsc_line2_diff_sts
	val13 = gpio_i2c_read(nvp6158_iic_addr[vin_fsc_val->devnum], 0xBA);
	//B13 0xB7 [31:24] r_fsc_line2_diff_sts
	val14 = gpio_i2c_read(nvp6158_iic_addr[vin_fsc_val->devnum], 0xBB);

	//B13 0xB4 [ 7:0] r_fsc_line_diff_final
	val21 = gpio_i2c_read(nvp6158_iic_addr[vin_fsc_val->devnum], 0xBC);
	//B13 0xB5 [15:8] r_fsc_line_diff_final
	val22 = gpio_i2c_read(nvp6158_iic_addr[vin_fsc_val->devnum], 0xBD);
	//B13 0xB6 [23:16] r_fsc_line_diff_final
	val23 = gpio_i2c_read(nvp6158_iic_addr[vin_fsc_val->devnum], 0xBE);
	//B13 0xB7 [31:24] r_fsc_line_diff_final
	val24 = gpio_i2c_read(nvp6158_iic_addr[vin_fsc_val->devnum], 0xBF);


	val_1 = ((val04 << 24) | (val03 << 16) | (val02 << 8) | val01);
	val_2 = ((val14 << 24) | (val13 << 16) | (val12 << 8) | val11);
	val_final = ((val24 << 24) | (val23 << 16) | (val22 << 8) | val21);

	vin_fsc_val->fsc_val1 = val_1;
	vin_fsc_val->fsc_val2 = val_2;
	vin_fsc_val->fsc_final = val_final;
}

//  170420 RAPTOR3 DR2 DEMO ONLY
void nvp6158_video_input_aeq_val_set(video_input_aeq_set *vin_aeq_val) // 170214 aeq value set
{
//	if(vin_aeq_val->aeq_val == 0x00)
//	{
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0xFF, 0x05 + vin_aeq_val->ch);
//
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x58, 0x03);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x59, 0x00);
//	}
//
//	else if(vin_aeq_val->aeq_val == 0x02)
//	{
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0xFF, 0x05 + vin_aeq_val->ch);
//
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x58, 0xD3);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x59, 0x11);
//	}

}

//  170420 RAPTOR3 DR2 DEMO ONLY
void nvp6158_video_input_deq_val_set(video_input_deq_set *vin_deq_val) // 170214 deq value set
{
	// B9 0x80/0xA0/0xC0/0xE0 [3:0]
//	if(vin_deq_val->deq_val == 0x00)
//	{
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0xFF, 0x09);
//
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x80 + ((vin_deq_val->ch)*0x20), 0x00);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x84 + ((vin_deq_val->ch)*0x20), 0x21);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x85 + ((vin_deq_val->ch)*0x20), 0x60);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x86 + ((vin_deq_val->ch)*0x20), 0xF6);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x87 + ((vin_deq_val->ch)*0x20), 0x20);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x88 + ((vin_deq_val->ch)*0x20), 0x00);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x89 + ((vin_deq_val->ch)*0x20), 0xDC);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x8a + ((vin_deq_val->ch)*0x20), 0x02);
//
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0xFF, 0x00);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x30, 0x18);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x3C, 0x90);
//	}
//	else if(vin_deq_val->deq_val == 0x02)
//	{
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0xFF, 0x09);
//
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x80 + ((vin_deq_val->ch)*0x20), 0xA7);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x84 + ((vin_deq_val->ch)*0x20), 0x21);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x85 + ((vin_deq_val->ch)*0x20), 0x60);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x86 + ((vin_deq_val->ch)*0x20), 0xF6);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x87 + ((vin_deq_val->ch)*0x20), 0x20);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x88 + ((vin_deq_val->ch)*0x20), 0x00);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x89 + ((vin_deq_val->ch)*0x20), 0xDC);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x8a + ((vin_deq_val->ch)*0x20), 0x02);
//
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0xFF, 0x00);
//		gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x30, 0x16);
//	}

//	val = gpio_i2c_read(nvp6158_iic_addr[chip_num], 0x80 + ((vin_deq_val->ch)*0x20));
//	val = (vin_deq_val->deq_val & 0xF) | val;
//
//	gpio_i2c_write(nvp6158_iic_addr[chip_num], 0x80 + ((vin_deq_val->ch)*0x20), val);
}

void nvp6158_video_input_acc_gain_val_read(video_input_acc_gain_val *vin_acc_gain) // 170215 acc gain read
{
	unsigned char val1, val2;

	if(vin_acc_gain->func_sel == ACC_GAIN_NORMAL) {

		gpio_i2c_write(nvp6158_iic_addr[vin_acc_gain->devnum], 0xFF, 0x05 + vin_acc_gain->ch);

		val1 = gpio_i2c_read(nvp6158_iic_addr[vin_acc_gain->devnum],0xE2) & 0x7; // B5 0xE2 acc gain [10:8]
		val2 = gpio_i2c_read(nvp6158_iic_addr[vin_acc_gain->devnum],0xE3); 		 // B5 0xE3 acc gain [7:0]
	} else if(vin_acc_gain->func_sel == ACC_GAIN_DEBUG) { 	// DEBUG
		gpio_i2c_write(nvp6158_iic_addr[vin_acc_gain->devnum], 0xFF, 0x00);
		val1 = 0;
		val2 = gpio_i2c_read(nvp6158_iic_addr[vin_acc_gain->devnum],0xD8 + vin_acc_gain->ch); // B13 0xC6 acc gain [9:8]
	} else {
		gpio_i2c_write(nvp6158_iic_addr[vin_acc_gain->devnum], 0xFF, 0x05);

		val1 = gpio_i2c_read(nvp6158_iic_addr[vin_acc_gain->devnum],0xE2) & 0x7; // B5 0xE2 acc gain [10:8]
		val2 = gpio_i2c_read(nvp6158_iic_addr[vin_acc_gain->devnum],0xE3); 		 // B5 0xE3 acc gain [7:0]
	}

	vin_acc_gain->acc_gain_val = val1 << 8 | val2;
}

void nvp6158_video_output_data_out_mode_set(video_output_data_out_mode *vo_data_out_mode)
{
	unsigned char temp_val = 0x0;

	//  Show/Hide mode is using register Bank 0 0x7A, 7B
	// 		   CH2	  CH1		    CH4    CH3
	//	0x7A [7 : 4][3 : 0]  0x7B [7 : 4][3 : 0]
	gpio_i2c_write(nvp6158_iic_addr[vo_data_out_mode->devnum], 0xFF, 0x00);

	switch(vo_data_out_mode -> ch) {
		case CH1 :
		case CH2 : temp_val = gpio_i2c_read(nvp6158_iic_addr[vo_data_out_mode->devnum], 0x7A);
					break;
		case CH3 :
		case CH4 : temp_val = gpio_i2c_read(nvp6158_iic_addr[vo_data_out_mode->devnum], 0x7B);
					break;
	}

	switch(vo_data_out_mode -> ch) {
		case CH1 :
		case CH3 :	temp_val = ((temp_val & 0xF0) | (vo_data_out_mode -> set_val & 0xF));
					break;
		case CH2 :
		case CH4 :  temp_val = ((temp_val & 0x0F) | ((vo_data_out_mode -> set_val & 0xF) << 4));
					break;
	}

	// printk("[%s:%s] : %s >>>> temp_val [ %x ]\n", __FILE__, __LINE__, __FUNCTION__,temp_val);
	switch(vo_data_out_mode -> ch) {
		case CH1 :
		case CH2 : gpio_i2c_write(nvp6158_iic_addr[vo_data_out_mode->devnum], 0x7A, temp_val);
				   break;
		case CH3 :
		case CH4 : gpio_i2c_write(nvp6158_iic_addr[vo_data_out_mode->devnum], 0x7B, temp_val);
				   break;
	}
}


unsigned char __nvp6158_IsOver3MRTVideoFormat( decoder_dev_ch_info_s *decoder_info )
{
	unsigned char ret = 0; //

	if((decoder_info->fmt_def == AHD30_3M_30P) ||
	   (decoder_info->fmt_def == AHD30_3M_25P) ||
	   (decoder_info->fmt_def == AHD30_4M_30P) ||
	   (decoder_info->fmt_def == AHD30_4M_25P) ||
	   (decoder_info->fmt_def == AHD30_5M_20P) ||
	   (decoder_info->fmt_def == AHD30_5_3M_20P) ||
	   (decoder_info->fmt_def == AHD30_6M_18P) ||
	   (decoder_info->fmt_def == AHD30_6M_20P) ||
	   (decoder_info->fmt_def == AHD30_8M_12_5P) ||
	   (decoder_info->fmt_def == AHD30_8M_15P) ||
	   (decoder_info->fmt_def == TVI_4M_30P) ||
	   (decoder_info->fmt_def == TVI_4M_25P) ||
	   (decoder_info->fmt_def == TVI_5M_20P) ||
	   (decoder_info->fmt_def == TVI_8M_12_5P) ||
	   (decoder_info->fmt_def == TVI_8M_15P) ||
	   (decoder_info->fmt_def == CVI_4M_25P) ||
	   (decoder_info->fmt_def == CVI_4M_30P) ||
	   (decoder_info->fmt_def == CVI_5M_20P) ||
	   (decoder_info->fmt_def == CVI_8M_15P) ||
	   (decoder_info->fmt_def == CVI_8M_12_5P)) {
		ret = 1;
	}
	return ret; // 0:Over 3M RT, 1:other formation
}

unsigned char nvp6158_s_only_onetime_run[32] = {0, };
void nvp6158_video_input_onvideo_set(decoder_dev_ch_info_s *decoder_info)
{
	unsigned char format_3M_RT;

	printk("onvideo_set dev_num[%x] ch_num[%x] fmt_def[%d]", 
		decoder_info->devnum, decoder_info->ch, decoder_info->fmt_def);

	/* after 09/12 */
	format_3M_RT = __nvp6158_IsOver3MRTVideoFormat(decoder_info);

	if(format_3M_RT) {
		/* DECI_FILTER_ON */
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x05 + decoder_info->ch);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x50, 0x76);
	} else {
		/* DECI_FILTER_OFF */
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x05 + decoder_info->ch);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x50, 0xc6);
	}


		if(decoder_info->fmt_def == CVI_HD_30P || decoder_info->fmt_def == CVI_HD_30P_EX ||
			decoder_info->fmt_def == AHD20_720P_30P	|| decoder_info->fmt_def == AHD20_720P_30P_EX ||
			decoder_info->fmt_def == AHD20_720P_30P_EX_Btype) {
			//meant to remove pre-connection issue. 07.31
			if( nvp6158_s_only_onetime_run[decoder_info->ch + 4 * decoder_info->devnum] == 0) {
				nvp6158_video_input_vafe_reset(decoder_info);
				nvp6158_s_only_onetime_run[decoder_info->ch + 4 * decoder_info->devnum] = 1;
			}
		} else {
			if( nvp6158_s_only_onetime_run[decoder_info->ch + 4 * decoder_info->devnum] == 0) {
				nvp6158_s_only_onetime_run[decoder_info->ch + 4 * decoder_info->devnum] = 1;
			}
		}
}

void nvp6168_video_input_onvideo_set(decoder_dev_ch_info_s *decoder_info)
{
//	unsigned char format_3M_RT;
	unsigned char ch = decoder_info->ch % 4;
	unsigned char devnum = decoder_info->devnum;
	unsigned char val_9x44;
#ifndef _NVP6168_USE_MANUAL_MODE_
	unsigned char set_done=0xF0;
#endif
	printk("onvideo_set dev_num[%x] ch_num[%x] fmt_def[%d]",
		decoder_info->devnum, decoder_info->ch, decoder_info->fmt_def);

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x09);
	val_9x44 = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x44);
	val_9x44 &= ~(1 << ch);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x44, val_9x44);

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x50 + (ch*4) , 0x30);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x51 + (ch*4) , 0x6F);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x52 + (ch*4) , 0x67);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x53 + (ch*4) , 0x48);

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x11);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00 + (ch*0x20), 0x00);
#ifndef _NVP6168_USE_MANUAL_MODE_
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
	set_done |= gpio_i2c_read(nvp6158_iic_addr[devnum], 0x70);
	set_done |= (1<<ch);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x70, set_done);

	set_done = gpio_i2c_read(nvp6158_iic_addr[devnum], 0x71);
	set_done |= (1<<ch);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x71, set_done);
#endif
}

void nvp6158_video_input_onvideo_check_data(video_input_vfc *vin_vfc)
{
	unsigned char val_5678xF0;
	gpio_i2c_write(nvp6158_iic_addr[vin_vfc->devnum], 0xFF, 0x05 + vin_vfc->ch);
	val_5678xF0 = gpio_i2c_read(nvp6158_iic_addr[vin_vfc->devnum], 0xF0);
	vin_vfc->vfc = val_5678xF0;
}

void nvp6158_video_input_auto_ch_sw_rst(decoder_dev_ch_info_s *decoder_info)
{
	unsigned char val_1x97;
	 //Software Reset
	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF,0x01);
	val_1x97 = gpio_i2c_read(nvp6158_iic_addr[decoder_info->devnum], 0x97);
	val_1x97 &= ~(1 << decoder_info->ch);
	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x97, val_1x97);
	msleep(10);
	val_1x97 = gpio_i2c_read(nvp6158_iic_addr[decoder_info->devnum], 0x97);
	val_1x97 |= (1 << decoder_info->ch);
	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x97, val_1x97);

	printk("[DRV] Decoder CH[%d] Software Reset done\n",decoder_info->ch);
}

void nvp6158_video_input_vafe_reset(decoder_dev_ch_info_s *decoder_info)
{
	unsigned char val_0x00;
	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x00);
	val_0x00 = gpio_i2c_read(nvp6158_iic_addr[decoder_info->devnum], 0x00 + decoder_info->ch);
	_SET_BIT(val_0x00, 0);
	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + decoder_info->ch, val_0x00);
	msleep(10);
	_CLE_BIT(val_0x00, 0);
	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + decoder_info->ch, val_0x00);
	printk("[DRV] AFE CH:[%d] Reset done\n", decoder_info->ch);
}

void nvp6158_video_input_manual_agc_stable_endi(decoder_dev_ch_info_s *decoder_info, int endi)
{
	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x05+decoder_info->ch);
	if( endi == 1 ) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x82, 0xff);
		printk("[DRV] MANUAL AGC STABLE ENABLE CH:[%d]\n", decoder_info->ch);
	} else {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x82, 0x00);
		printk("[DRV] MANUAL AGC STABLE ENABLE CH:[%d]\n", decoder_info->ch);
	}
}

void nvp6158_video_input_vafe_control(decoder_dev_ch_info_s *decoder_info, int cmd)
{
	unsigned char val_0x00;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x00);

	if(cmd == 0) {
		val_0x00 = gpio_i2c_read(nvp6158_iic_addr[decoder_info->devnum], 0x00 + decoder_info->ch);
		_SET_BIT(val_0x00, 0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + decoder_info->ch, val_0x00);

		printk("[DRV] [Ch:%d] AFE Power Down ... \n", decoder_info->ch);

		msleep(10);
	} else if(cmd == 1) {
		val_0x00 = gpio_i2c_read(nvp6158_iic_addr[decoder_info->devnum], 0x00 + decoder_info->ch);
		_CLE_BIT(val_0x00, 0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + decoder_info->ch, val_0x00);

		printk("[DRV] [Ch:%d] AFE Power Up ... \n", decoder_info->ch);

		msleep(10);
	}
}

static __maybe_unused unsigned int __nvp6158_s_max_min_exclude_avg_func(unsigned int* input_arry, int cnt)
{
	unsigned int max, min, sum = 0, result = 0;
	unsigned int ii;

	max = input_arry[0];
	min = input_arry[0];

	for(ii = 0; ii < cnt; ii++) {
		max = max > input_arry[ii] ? max : input_arry[ii];
		min = min > input_arry[ii] ? input_arry[ii] : min;

		sum += input_arry[ii];
	}

	result = sum - (max + min);

	if(result == 0) {
		return 0;
	} else {
		result /= ( cnt - 2 );
	}

	return result;
}

static unsigned int __nvp6158_s_distinguish_5M_ahd_tvi_func(unsigned int* input_arry, int cnt)
{
	unsigned int chk1, chk2;
	unsigned int max, max_idx = 0;
	unsigned int calc_array[10][10] = { {0, 0},  };
	unsigned int need_update = 0;
	unsigned int find_idx = 0;
	unsigned int ii, ij;
	unsigned int inner_idx = 0;

	chk1 = input_arry[0];

	for(ii = 0; ii < cnt; ii++) {
		chk2 = input_arry[ii];

		if( chk1 == chk2) {
			calc_array[0][inner_idx] += 1;
			calc_array[1][inner_idx] = chk1;
		} else if( chk1 != chk2 ) {
			for(ij = 0; ij < ii; ij++) {
				if( calc_array[1][ij] == chk2 ) {
					find_idx = ij;
					calc_array[0][find_idx] += 1;
					calc_array[1][find_idx] = chk2;
					need_update = 0;
					break;
				}
				need_update = 1;
			}

			if(need_update) {
				inner_idx += 1;
				calc_array[0][inner_idx] += 1;
				calc_array[1][inner_idx] = chk2;
			}
		}
		chk1 = chk2;
	}

	max = calc_array[0][0];

	for(ii = 0; ii < cnt; ii++) {
		if( max < calc_array[0][ii] ) {
			max_idx = ii;
			max = calc_array[0][ii];
		}
	}


	for(ii = 0; ii < cnt; ii++) {
		printk("[DRV] [ idx %d ] [ num %d ] [ val %x ]\n", ii, calc_array[0][ii], calc_array[1][ii]);
	}

	printk("[DRV] [ max_idx : %d ]\n", max_idx);
	printk("[DRV] [ inner_idx : %d ]\n", inner_idx);

	return calc_array[1][max_idx];
}


void nvp6158_video_input_ahd_tvi_distinguish(decoder_dev_ch_info_s *decoder_info)
{
	unsigned char ch = decoder_info->ch;
	unsigned char devnum = decoder_info->devnum;
	unsigned char fmtdef = decoder_info->fmt_def;
	unsigned char ii;
	unsigned int check_point;

	unsigned char check_time = 10;


#if 1
	unsigned int B5xF5_F4[10];

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05 + ch);

	for(ii = 0; ii < check_time; ii++) {
		msleep(100);
		B5xF5_F4[ii] = ( gpio_i2c_read(nvp6158_iic_addr[devnum], 0xF5) << 8 ) |
			gpio_i2c_read(nvp6158_iic_addr[devnum], 0xF4);
		printk("[DRV] [Ch:%d] %d time B5xF3_F4 : %x \n", ch, ii, B5xF5_F4[ii]);
	}

	check_point = __nvp6158_s_distinguish_5M_ahd_tvi_func( B5xF5_F4, check_time );

	if( fmtdef == AHD30_5M_20P ) {
		if( ( check_point & 0xfff ) == 0x7c2) {
			decoder_info->fmt_def = TVI_5M_20P;
			printk("[DRV] [Ch:%d] Get Format : AHD 5M 20P, Change Format : TVI 5M 20P\n", decoder_info->ch);
		} else if( ( check_point & 0xfff ) == 0x7c4) {
			decoder_info->fmt_def = CVI_5M_20P;
			printk("[DRV] [Ch:%d] Get Format : AHD 5M 20P, Change Format : CVI 5M 20P\n", decoder_info->ch);
		} else {

			printk("[DRV] [Ch:%d] Get Format : AHD 5M 20P, Not Change Format\n", decoder_info->ch);
		}
	} else if( fmtdef == 0x2B) {
		if( ( check_point & 0xfff ) >= 0x673) {
			decoder_info->fmt_def = TVI_4M_15P;
			printk("[DRV] [Ch:%d] Get Format : AHD 4M15P #0P, Change Format : TVI 4M 15P\n", decoder_info->ch);
		}
	} else {
		decoder_info->fmt_def = fmtdef;
	}


#else
	unsigned int B5xE8_E9[10] = {0, };
	unsigned int B5xEA_EB[10] = {0, };

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05 + ch);


	for(ii = 0; ii < check_time; ii++)
	{
	msleep(10);
		B5xE8_E9[ii] = ( gpio_i2c_read(nvp6158_iic_addr[devnum], 0xE8) << 8 ) | gpio_i2c_read(nvp6158_iic_addr[devnum], 0xE9);
		B5xEA_EB[ii] = ( gpio_i2c_read(nvp6158_iic_addr[devnum], 0xEA) << 8 ) | gpio_i2c_read(nvp6158_iic_addr[devnum], 0xEB);
		printk("[DRV] [Ch:%d] %d time 0xE8_0xE9 : %x \n", decoder_info->ch, ii, B5xE8_E9[ii]);
		printk("[DRV] [Ch:%d] %d time 0xEA_0xEB : %x \n", decoder_info->ch, ii, B5xEA_EB[ii]);
	}

	check_point1 = __nvp6158_s_max_min_exclude_avg_func( B5xE8_E9, check_time );
	check_point2 = __nvp6158_s_max_min_exclude_avg_func( B5xEA_EB, check_time );

	printk("[DRV] [Ch:%d] AVG 0xE8_0xE9 : %x \n", decoder_info->ch, check_point1);
	printk("[DRV] [Ch:%d] AVG 0xEA_0xEB : %x \n", decoder_info->ch, check_point2);

	if( ( check_point1 < 0x30 ) && ( check_point2 < 0x30 ) )
	{
		if( fmtdef == AHD30_5M_20P )
		{
			decoder_info->fmt_def = TVI_5M_20P;

			printk("[DRV] [Ch:%d] Get Format : AHD 5M 20P, Change Format : TVI 5M 20P\n", decoder_info->ch);
		}
		else if( fmtdef == AHD30_3M_30P ) /* Not Yet Support , only add item */
		{
			decoder_info->fmt_def = TVI_4M_15P;

			printk("[DRV] [Ch:%d] Get Format : AHD 3M #0P, Change Format : TVI 4M 15P\n", decoder_info->ch);
		}
		else
		{
			decoder_info->fmt_def = fmtdef;
		}
	}
	else
	{
		decoder_info->fmt_def = fmtdef;
	}
#endif
}

void nvp6168_video_input_cvi_tvi_5M20p_distinguish(decoder_dev_ch_info_s *decoder_info)
{
	unsigned char ch = decoder_info->ch;
	unsigned char devnum = decoder_info->devnum;
//	unsigned char fmtdef = decoder_info->fmt_def;
	unsigned char ii;
	unsigned int check_point;

	unsigned char check_time = 10;


	unsigned int B5xF5_F4[10];

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05 + ch);

	for(ii = 0; ii < check_time; ii++) {
		msleep(100);
		B5xF5_F4[ii] = ( gpio_i2c_read(nvp6158_iic_addr[devnum], 0xF5) << 8 ) |
						gpio_i2c_read(nvp6158_iic_addr[devnum], 0xF4);
		printk("[DRV] [Ch:%d] %d time B5xF3_F4 : %x \n", ch, ii, B5xF5_F4[ii]);
	}

	check_point = __nvp6158_s_distinguish_5M_ahd_tvi_func( B5xF5_F4, check_time );

 	if( ( check_point & 0xfff ) == 0x7c4) {
		decoder_info->fmt_def = CVI_5M_20P;
		printk("[DRV] [Ch:%d] Get Format : AHD 5M 20P, Change Format : CVI 5M 20P\n", decoder_info->ch);
	} else {

		decoder_info->fmt_def = TVI_5M_20P;
		printk("[DRV] [Ch:%d] Get Format : AHD 5M 20P, Change Format : TVI 5M 20P\n", decoder_info->ch);
	}

}


static unsigned int __nvp6158_s_distinguish_8M_cvi_tvi_func(unsigned int* input_arry, int cnt)
{
	unsigned int chk1, chk2;
	unsigned int max, max_idx = 0;
	unsigned int calc_array[10][10] = { {0, 0},  };
	unsigned int need_update = 0;
	unsigned int find_idx = 0;
	unsigned int ii, ij;
	unsigned int inner_idx = 0;

	chk1 = input_arry[0];

	for(ii = 0; ii < cnt; ii++) {
		chk2 = input_arry[ii];

		if( chk1 == chk2) {
			calc_array[0][inner_idx] += 1;
			calc_array[1][inner_idx] = chk1;
		} else if( chk1 != chk2 ) {
			for(ij = 0; ij < ii; ij++) {
				if( calc_array[1][ij] == chk2 ) {
					find_idx = ij;
					calc_array[0][find_idx] += 1;
					calc_array[1][find_idx] = chk2;
					need_update = 0;
					break;
				}
				need_update = 1;
			}

			if(need_update) {
				inner_idx += 1;
				calc_array[0][inner_idx] += 1;
				calc_array[1][inner_idx] = chk2;
			}
		}
		chk1 = chk2;
	}

	max = calc_array[0][0];

	for(ii = 0; ii < cnt; ii++) {
		if( max < calc_array[0][ii] ) {
			max_idx = ii;
			max = calc_array[0][ii];
		}
	}


	for(ii = 0; ii < cnt; ii++) {
		printk("[DRV] [ idx %d ] [ num %d ] [ val %x ]\n", ii, calc_array[0][ii], calc_array[1][ii]);
	}

	printk("[DRV] [ max_idx : %d ]\n", max_idx);
	printk("[DRV] [ inner_idx : %d ]\n", inner_idx);

	return calc_array[1][max_idx];
}


int nvp6158_video_input_cvi_tvi_distinguish(decoder_dev_ch_info_s *decoder_info)
{
	unsigned char ch = decoder_info->ch;
	unsigned char devnum = decoder_info->devnum;
	unsigned char fmtdef = decoder_info->fmt_def;
	unsigned char ii;
	unsigned int check_point;

	unsigned char check_time = 10;

	unsigned int B13xAB[10], B13xAB_zerocnt=0;

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x2B, ch % 4 );

	for(ii = 0; ii < check_time; ii++) {
		msleep(100);
		B13xAB[ii] = gpio_i2c_read(nvp6158_iic_addr[devnum], 0xAB);
		if(B13xAB[ii] == 0) {
			B13xAB_zerocnt ++;
		}
		printk("[DRV] [Ch:%d] %d time B13xAB : %x \n", ch, ii, B13xAB[ii]);
	}

	if(B13xAB_zerocnt > 2)
		return -1;

	check_point = __nvp6158_s_distinguish_8M_cvi_tvi_func( B13xAB, check_time );

	if( fmtdef == CVI_8M_15P || fmtdef == CVI_8M_12_5P ) {
		if( ( check_point & 0xff ) > 0x1A ) {
			if( fmtdef == CVI_8M_12_5P ) {
				decoder_info->fmt_def = TVI_8M_12_5P;
				printk("[DRV] [Ch:%d] Get Format : CVI 8M 12_5P, Change Format : TVI 8M 12_5P\n", decoder_info->ch);
			} else {
				decoder_info->fmt_def = TVI_8M_15P;
				printk("[DRV] [Ch:%d] Get Format : CVI 8M 15P, Change Format : TVI 8M 15P\n", decoder_info->ch);
			}
		} else {
			printk("[DRV] [Ch:%d] Get Format : CVI 8M, Not Change Format\n", decoder_info->ch);
		}
	} else {
		decoder_info->fmt_def = fmtdef;
	}
	return 0;
}

static unsigned int __nvp6158_s_distinguish_ahd_nrt_func(unsigned int* input_arry, int cnt)
{
	unsigned int chk1, chk2;
	unsigned int max, max_idx = 0;
	unsigned int calc_array[10][10] = { {0, 0},  };
	unsigned int need_update = 0;
	unsigned int find_idx = 0;
	unsigned int ii, ij;
	unsigned int inner_idx = 0;

	chk1 = input_arry[0];

	for(ii = 0; ii < cnt; ii++) {
		chk2 = input_arry[ii];

		if( chk1 == chk2) {
			calc_array[0][inner_idx] += 1;
			calc_array[1][inner_idx] = chk1;
		} else if( chk1 != chk2 ) {
			for(ij = 0; ij < ii; ij++) {
				if( calc_array[1][ij] == chk2 ) {
					find_idx = ij;
					calc_array[0][find_idx] += 1;
					calc_array[1][find_idx] = chk2;
					need_update = 0;
					break;
				}
				need_update = 1;
			}

			if(need_update) {
				inner_idx += 1;
				calc_array[0][inner_idx] += 1;
				calc_array[1][inner_idx] = chk2;
			}
		}
		chk1 = chk2;
	}

	max = calc_array[0][0];

	for(ii = 0; ii < cnt; ii++) {
		if( max < calc_array[0][ii] ) {
			max_idx = ii;
			max = calc_array[0][ii];
		}
	}


	for(ii = 0; ii < cnt; ii++) {
		printk("[DRV] [ idx %d ] [ num %d ] [ val %x ]\n", ii, calc_array[0][ii], calc_array[1][ii]);
	}

	printk("[DRV] [ max_idx : %d ]\n", max_idx);
	printk("[DRV] [ inner_idx : %d ]\n", inner_idx);

	return calc_array[1][max_idx];
}


void nvp6158_video_input_ahd_nrt_distinguish(decoder_dev_ch_info_s *decoder_info)
{
	unsigned char ch = decoder_info->ch;
	unsigned char devnum = decoder_info->devnum;
	unsigned char ii;
	unsigned int check_point;

	unsigned char check_time = 10;

	unsigned int B5xF3[10];

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x05 + ch);

	for(ii = 0; ii < check_time; ii++) {
		msleep(30);
		B5xF3[ii] = gpio_i2c_read(nvp6158_iic_addr[devnum], 0xF3);
		printk("[DRV] [Ch:%d] %d time B5xF3 : %x \n", ch, ii, B5xF3[ii]);
	}

	check_point = __nvp6158_s_distinguish_ahd_nrt_func( B5xF3, check_time );

	if( ( check_point & 0xff ) == 0x14 ) {
		decoder_info->fmt_def = AHD20_1080P_12_5P_EX;
		printk("[DRV] [Ch:%d] Get Format : AHD 1080P 12.5P\n", decoder_info->ch);
	} else if( ( check_point & 0xff ) == 0x11 ) {
		decoder_info->fmt_def = AHD20_1080P_15P_EX;
		printk("[DRV] [Ch:%d] Get Format : AHD 1080P 15P\n", decoder_info->ch);
	} else {
		decoder_info->fmt_def = NC_VIVO_CH_FORMATDEF_UNKNOWN;
		printk("[DRV] [Ch:%d] Get Format : Unknown Format \n", decoder_info->ch);
	}
}

static unsigned int __nvp6158_s_distinguish_2M_cvi_ahd_func(unsigned int* input_arry, int cnt)
{
	unsigned int chk1, chk2;
	unsigned int max, max_idx = 0;
	unsigned int calc_array[10][10] = { {0, 0},  };
	unsigned int need_update = 0;
	unsigned int find_idx = 0;
	unsigned int ii, ij;
	unsigned int inner_idx = 0;

	chk1 = input_arry[0];

	for(ii = 0; ii < cnt; ii++) {
		chk2 = input_arry[ii];

		if( chk1 == chk2) {
			calc_array[0][inner_idx] += 1;
			calc_array[1][inner_idx] = chk1;
		} else if( chk1 != chk2 ) {
			for(ij = 0; ij < ii; ij++) {
				if( calc_array[1][ij] == chk2 ) {
					find_idx = ij;
					calc_array[0][find_idx] += 1;
					calc_array[1][find_idx] = chk2;
					need_update = 0;
					break;
				}
				need_update = 1;
			}

			if(need_update) {
				inner_idx += 1;
				calc_array[0][inner_idx] += 1;
				calc_array[1][inner_idx] = chk2;
			}
		}

		chk1 = chk2;
	}

	max = calc_array[0][0];

	for(ii = 0; ii < cnt; ii++) {
		if( max < calc_array[0][ii] ) {
			max_idx = ii;
			max = calc_array[0][ii];
		}
	}


	for(ii = 0; ii < cnt; ii++) {
		printk("[DRV] [ idx %d ] [ num %d ] [ val %x ]\n", ii, calc_array[0][ii], calc_array[1][ii]);
	}

	printk("[DRV] [ max_idx : %d ]\n", max_idx);
	printk("[DRV] [ inner_idx : %d ]\n", inner_idx);

	return calc_array[1][max_idx];
}


void nvp6158_video_input_cvi_ahd_1080p_distinguish(decoder_dev_ch_info_s *decoder_info)
{
	unsigned char ch = decoder_info->ch;
	unsigned char devnum = decoder_info->devnum;
	unsigned char fmtdef = decoder_info->fmt_def;
	unsigned char ii;
	unsigned int check_point;

	unsigned char check_time = 10;

	unsigned int B13xAB[10];

	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x13);
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0x2B, ch % 4 );

	for(ii = 0; ii < check_time; ii++) {
		msleep(100);
		B13xAB[ii] = gpio_i2c_read(nvp6158_iic_addr[devnum], 0xAB);
		printk("[DRV] [Ch:%d] %d time B13xAB : %x \n", ch, ii, B13xAB[ii]);
	}

	check_point = __nvp6158_s_distinguish_2M_cvi_ahd_func( B13xAB, check_time );

	if( fmtdef == CVI_FHD_25P ) {
		if( ( check_point & 0xff ) <= 0x09 ) {

			decoder_info->fmt_def = AHD20_1080P_25P;
			printk("[DRV] [Ch:%d] Get Format : CVI 2M 25P, Change Format : AHD 2M 25P\n", decoder_info->ch);
		} else {
			printk("[DRV] [Ch:%d] Get Format : CVI 2M, Not Change Format\n", decoder_info->ch);
		}
	} else {
		decoder_info->fmt_def = fmtdef;
	}
}


