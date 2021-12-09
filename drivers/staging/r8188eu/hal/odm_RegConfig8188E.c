// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/odm_precomp.h"

void odm_ConfigRFReg_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr,
			   u32 Data, enum rf_radio_path RF_PATH,
			   u32 RegAddr)
{
    if (Addr == 0xffe) {
		ODM_sleep_ms(50);
	} else if (Addr == 0xfd) {
		ODM_delay_ms(5);
	} else if (Addr == 0xfc) {
		ODM_delay_ms(1);
	} else if (Addr == 0xfb) {
		ODM_delay_us(50);
	} else if (Addr == 0xfa) {
		ODM_delay_us(5);
	} else if (Addr == 0xf9) {
		ODM_delay_us(1);
	} else {
		ODM_SetRFReg(pDM_Odm, RF_PATH, RegAddr, bRFRegOffsetMask, Data);
		/*  Add 1us delay between BB/RF register setting. */
		ODM_delay_us(1);
	}
}

void odm_ConfigRF_RadioA_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr, u32 Data)
{
	u32  content = 0x1000; /*  RF_Content: radioa_txt */
	u32 maskforPhySet = (u32)(content & 0xE000);

	odm_ConfigRFReg_8188E(pDM_Odm, Addr, Data, RF_PATH_A, Addr | maskforPhySet);
}

void odm_ConfigRF_RadioB_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr, u32 Data)
{
	u32  content = 0x1001; /*  RF_Content: radiob_txt */
	u32 maskforPhySet = (u32)(content & 0xE000);

	odm_ConfigRFReg_8188E(pDM_Odm, Addr, Data, RF_PATH_B, Addr | maskforPhySet);
}

void odm_ConfigMAC_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr, u8 Data)
{
	ODM_Write1Byte(pDM_Odm, Addr, Data);
}

void odm_ConfigBB_AGC_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr, u32 Bitmask, u32 Data)
{
	ODM_SetBBReg(pDM_Odm, Addr, Bitmask, Data);
	/*  Add 1us delay between BB/RF register setting. */
	ODM_delay_us(1);
}

void odm_ConfigBB_PHY_REG_PG_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr,
				   u32 Bitmask, u32 Data)
{
	if (Addr == 0xfe)
		ODM_sleep_ms(50);
	else if (Addr == 0xfd)
		ODM_delay_ms(5);
	else if (Addr == 0xfc)
		ODM_delay_ms(1);
	else if (Addr == 0xfb)
		ODM_delay_us(50);
	else if (Addr == 0xfa)
		ODM_delay_us(5);
	else if (Addr == 0xf9)
		ODM_delay_us(1);
	else
		storePwrIndexDiffRateOffset(pDM_Odm->Adapter, Addr, Bitmask, Data);
}

void odm_ConfigBB_PHY_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr, u32 Bitmask, u32 Data)
{
	if (Addr == 0xfe) {
		ODM_sleep_ms(50);
	} else if (Addr == 0xfd) {
		ODM_delay_ms(5);
	} else if (Addr == 0xfc) {
		ODM_delay_ms(1);
	} else if (Addr == 0xfb) {
		ODM_delay_us(50);
	} else if (Addr == 0xfa) {
		ODM_delay_us(5);
	} else if (Addr == 0xf9) {
		ODM_delay_us(1);
	} else {
		if (Addr == 0xa24)
			pDM_Odm->RFCalibrateInfo.RegA24 = Data;
		ODM_SetBBReg(pDM_Odm, Addr, Bitmask, Data);

		/*  Add 1us delay between BB/RF register setting. */
		ODM_delay_us(1);
	}
}
