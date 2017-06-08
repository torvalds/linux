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

void odm_ConfigRFReg_8723B(
	PDM_ODM_T pDM_Odm,
	u32 Addr,
	u32 Data,
	ODM_RF_RADIO_PATH_E RF_PATH,
	u32 RegAddr
)
{
	if (Addr == 0xfe || Addr == 0xffe)
		msleep(50);
	else {
		PHY_SetRFReg(pDM_Odm->Adapter, RF_PATH, RegAddr, bRFRegOffsetMask, Data);
		/*  Add 1us delay between BB/RF register setting. */
		udelay(1);

		/* For disable/enable test in high temperature, the B6 value will fail to fill. Suggestion by BB Stanley, 2013.06.25. */
		if (Addr == 0xb6) {
			u32 getvalue = 0;
			u8 count = 0;

			getvalue = PHY_QueryRFReg(
				pDM_Odm->Adapter, RF_PATH, Addr, bMaskDWord
			);

			udelay(1);

			while ((getvalue>>8) != (Data>>8)) {
				count++;
				PHY_SetRFReg(pDM_Odm->Adapter, RF_PATH, RegAddr, bRFRegOffsetMask, Data);
				udelay(1);
				getvalue = PHY_QueryRFReg(pDM_Odm->Adapter, RF_PATH, Addr, bMaskDWord);
				ODM_RT_TRACE(
					pDM_Odm,
					ODM_COMP_INIT,
					ODM_DBG_TRACE,
					(
						"===> ODM_ConfigRFWithHeaderFile: [B6] getvalue 0x%x, Data 0x%x, count %d\n",
						getvalue,
						Data,
						count
					)
				);
				if (count > 5)
					break;
			}
		}

		if (Addr == 0xb2) {
			u32 getvalue = 0;
			u8 count = 0;

			getvalue = PHY_QueryRFReg(
				pDM_Odm->Adapter, RF_PATH, Addr, bMaskDWord
			);

			udelay(1);

			while (getvalue != Data) {
				count++;
				PHY_SetRFReg(
					pDM_Odm->Adapter,
					RF_PATH,
					RegAddr,
					bRFRegOffsetMask,
					Data
				);
				udelay(1);
				/* Do LCK againg */
				PHY_SetRFReg(
					pDM_Odm->Adapter,
					RF_PATH,
					0x18,
					bRFRegOffsetMask,
					0x0fc07
				);
				udelay(1);
				getvalue = PHY_QueryRFReg(
					pDM_Odm->Adapter, RF_PATH, Addr, bMaskDWord
				);
				ODM_RT_TRACE(
					pDM_Odm,
					ODM_COMP_INIT,
					ODM_DBG_TRACE,
					(
						"===> ODM_ConfigRFWithHeaderFile: [B2] getvalue 0x%x, Data 0x%x, count %d\n",
						getvalue,
						Data,
						count
					)
				);

				if (count > 5)
					break;
			}
		}
	}
}


void odm_ConfigRF_RadioA_8723B(PDM_ODM_T pDM_Odm, u32 Addr, u32 Data)
{
	u32  content = 0x1000; /*  RF_Content: radioa_txt */
	u32 maskforPhySet = (u32)(content&0xE000);

	odm_ConfigRFReg_8723B(
		pDM_Odm,
		Addr,
		Data,
		ODM_RF_PATH_A,
		Addr|maskforPhySet
	);

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_TRACE,
		(
			"===> ODM_ConfigRFWithHeaderFile: [RadioA] %08X %08X\n",
			Addr,
			Data
		)
	);
}

void odm_ConfigMAC_8723B(PDM_ODM_T pDM_Odm, u32 Addr, u8 Data)
{
	rtw_write8(pDM_Odm->Adapter, Addr, Data);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_TRACE,
		(
			"===> ODM_ConfigMACWithHeaderFile: [MAC_REG] %08X %08X\n",
			Addr,
			Data
		)
	);
}

void odm_ConfigBB_AGC_8723B(
	PDM_ODM_T pDM_Odm,
	u32 Addr,
	u32 Bitmask,
	u32 Data
)
{
	PHY_SetBBReg(pDM_Odm->Adapter, Addr, Bitmask, Data);
	/*  Add 1us delay between BB/RF register setting. */
	udelay(1);

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_TRACE,
		(
			"===> ODM_ConfigBBWithHeaderFile: [AGC_TAB] %08X %08X\n",
			Addr,
			Data
		)
	);
}

void odm_ConfigBB_PHY_REG_PG_8723B(
	PDM_ODM_T pDM_Odm,
	u32 Band,
	u32 RfPath,
	u32 TxNum,
	u32 Addr,
	u32 Bitmask,
	u32 Data
)
{
	if (Addr == 0xfe || Addr == 0xffe)
		msleep(50);
	else {
		PHY_StoreTxPowerByRate(pDM_Odm->Adapter, Band, RfPath, TxNum, Addr, Bitmask, Data);
	}
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_LOUD,
		(
			"===> ODM_ConfigBBWithHeaderFile: [PHY_REG] %08X %08X %08X\n",
			Addr,
			Bitmask,
			Data
		)
	);
}

void odm_ConfigBB_PHY_8723B(
	PDM_ODM_T pDM_Odm,
	u32 Addr,
	u32 Bitmask,
	u32 Data
)
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
	else {
		PHY_SetBBReg(pDM_Odm->Adapter, Addr, Bitmask, Data);
	}

	/*  Add 1us delay between BB/RF register setting. */
	udelay(1);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> ODM_ConfigBBWithHeaderFile: [PHY_REG] %08X %08X\n", Addr, Data));
}

void odm_ConfigBB_TXPWR_LMT_8723B(
	PDM_ODM_T pDM_Odm,
	u8 *Regulation,
	u8 *Band,
	u8 *Bandwidth,
	u8 *RateSection,
	u8 *RfPath,
	u8 *Channel,
	u8 *PowerLimit
)
{
	PHY_SetTxPowerLimit(
		pDM_Odm->Adapter,
		Regulation,
		Band,
		Bandwidth,
		RateSection,
		RfPath,
		Channel,
		PowerLimit
	);
}
