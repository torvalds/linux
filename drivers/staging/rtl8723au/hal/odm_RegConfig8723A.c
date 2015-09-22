/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#include "odm_precomp.h"
#include "usb_ops_linux.h"

void
odm_ConfigRFReg_8723A(
	struct dm_odm_t *pDM_Odm,
	u32					Addr,
	u32					Data,
	enum RF_RADIO_PATH     RF_PATH,
	u32				    RegAddr
	)
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
		ODM_SetRFReg(pDM_Odm, RF_PATH, RegAddr, bRFRegOffsetMask, Data);
		/*  Add 1us delay between BB/RF register setting. */
		udelay(1);
	}
}

void odm_ConfigMAC_8723A(struct dm_odm_t *pDM_Odm, u32 addr, u8	data)
{
	rtl8723au_write8(pDM_Odm->Adapter, addr, data);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD,
		     ("===> %s: [MAC_REG] %08X %08X\n", __func__, addr, data));
}

void odm_ConfigBB_AGC_8723A(struct dm_odm_t *pDM_Odm, u32 addr, u32 data)
{
	rtl8723au_write32(pDM_Odm->Adapter, addr, data);
	/*  Add 1us delay between BB/RF register setting. */
	udelay(1);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD,
		     ("===> %s: [AGC_TAB] %08X %08X\n", __func__, addr, data));
}

void
odm_ConfigBB_PHY_8723A(struct dm_odm_t *pDM_Odm, u32 addr, u32 data)
{
	if (addr == 0xfe)
		msleep(50);
	else if (addr == 0xfd)
		mdelay(5);
	else if (addr == 0xfc)
		mdelay(1);
	else if (addr == 0xfb)
		udelay(50);
	else if (addr == 0xfa)
		udelay(5);
	else if (addr == 0xf9)
		udelay(1);
	else if (addr == 0xa24)
		pDM_Odm->RFCalibrateInfo.RegA24 = data;
	rtl8723au_write32(pDM_Odm->Adapter, addr, data);

	/*  Add 1us delay between BB/RF register setting. */
	udelay(1);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD,
		     ("===> %s: [PHY_REG] %08X %08X\n", __func__, addr, data));
}
