// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/drv_types.h"

void odm_ConfigBB_AGC_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr, u32 Bitmask, u32 Data)
{
	rtl8188e_PHY_SetBBReg(pDM_Odm->Adapter, Addr, Bitmask, Data);
	/*  Add 1us delay between BB/RF register setting. */
	udelay(1);
}

void odm_ConfigBB_PHY_REG_PG_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr,
				   u32 Bitmask, u32 Data)
{
	if (Addr == 0xfe)
		msleep(50);
	else if (Addr == 0xfd)
		mdelay(5);
	else if (Addr == 0xfc)
		mdelay(1);
	else if (Addr == 0xfb)
		udelay(50);
	else if (Addr == 0xfa)
		udelay(5);
	else if (Addr == 0xf9)
		udelay(1);
	else
		storePwrIndexDiffRateOffset(pDM_Odm->Adapter, Addr, Bitmask, Data);
}

void odm_ConfigBB_PHY_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr, u32 Bitmask, u32 Data)
{
	if (Addr == 0xfe) {
		msleep(50);
	} else if (Addr == 0xfd) {
		mdelay(5);
	} else if (Addr == 0xfc) {
		mdelay(1);
	} else if (Addr == 0xfb) {
		udelay(50);
	} else if (Addr == 0xfa) {
		udelay(5);
	} else if (Addr == 0xf9) {
		udelay(1);
	} else {
		if (Addr == 0xa24)
			pDM_Odm->RFCalibrateInfo.RegA24 = Data;
		rtl8188e_PHY_SetBBReg(pDM_Odm->Adapter, Addr, Bitmask, Data);

		/*  Add 1us delay between BB/RF register setting. */
		udelay(1);
	}
}
