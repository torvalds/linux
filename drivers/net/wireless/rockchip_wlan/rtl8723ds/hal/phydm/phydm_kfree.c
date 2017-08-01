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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

/*============================================================*/
/*include files*/
/*============================================================*/
#include "mp_precomp.h"
#include "phydm_precomp.h"


/*<YuChen, 150720> Add for KFree Feature Requested by RF David.*/
/*This is a phydm API*/

VOID
phydm_SetKfreeToRF_8814A(
	IN	PVOID		pDM_VOID,
	IN	u1Byte		eRFPath,
	IN	u1Byte		Data
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PODM_RF_CAL_T	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
	BOOLEAN bOdd;
	
	if ((Data%2) != 0) {	/*odd -> positive*/
		Data = Data - 1;
		ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT19, 1);
		bOdd = TRUE;
	} else {		/*even -> negative*/
		ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT19, 0);
		bOdd = FALSE;
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): RF_0x55[19]= %d\n", bOdd));
		switch (Data) {
		case 0:
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT14, 0);
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT17|BIT16|BIT15, 0);
			pRFCalibrateInfo->KfreeOffset[eRFPath] = 0;
		break;
		case 2:
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT14, 1);
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT17|BIT16|BIT15, 0);
			pRFCalibrateInfo->KfreeOffset[eRFPath] = 0;
		break;
		case 4:
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT14, 0);
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT17|BIT16|BIT15, 1);
			pRFCalibrateInfo->KfreeOffset[eRFPath] = 1;
		break;
		case 6:
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT14, 1);
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT17|BIT16|BIT15, 1);
			pRFCalibrateInfo->KfreeOffset[eRFPath] = 1;
		break;
		case 8:
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT14, 0);
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT17|BIT16|BIT15, 2);
			pRFCalibrateInfo->KfreeOffset[eRFPath] = 2;
		break;
		case 10:
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT14, 1);
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT17|BIT16|BIT15, 2);
			pRFCalibrateInfo->KfreeOffset[eRFPath] = 2;
		break;
		case 12:
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT14, 0);
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT17|BIT16|BIT15, 3);
			pRFCalibrateInfo->KfreeOffset[eRFPath] = 3;
		break;
		case 14:
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT14, 1);
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT17|BIT16|BIT15, 3);
			pRFCalibrateInfo->KfreeOffset[eRFPath] = 3;
		break;
		case 16:
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT14, 0);
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT17|BIT16|BIT15, 4);
			pRFCalibrateInfo->KfreeOffset[eRFPath] = 4;
		break;
		case 18:
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT14, 1);
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT17|BIT16|BIT15, 4);
			pRFCalibrateInfo->KfreeOffset[eRFPath] = 4;
		break;
		case 20:
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT14, 0);
			ODM_SetRFReg(pDM_Odm, eRFPath, rRF_TxGainOffset, BIT17|BIT16|BIT15, 5);
			pRFCalibrateInfo->KfreeOffset[eRFPath] = 5;
		break;

		default:
		break;
	}

	if (bOdd == FALSE) {
	/*that means Kfree offset is negative, we need to record it.*/
		pRFCalibrateInfo->KfreeOffset[eRFPath] = (-1)*pRFCalibrateInfo->KfreeOffset[eRFPath];
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): KfreeOffset = %d\n", pRFCalibrateInfo->KfreeOffset[eRFPath]));
	} else
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): KfreeOffset = %d\n", pRFCalibrateInfo->KfreeOffset[eRFPath]));
	
}


VOID
phydm_SetKfreeToRF(
	IN	PVOID		pDM_VOID,
	IN	u1Byte		eRFPath,
	IN	u1Byte		Data
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	
	if (pDM_Odm->SupportICType & ODM_RTL8814A)
		phydm_SetKfreeToRF_8814A(pDM_Odm, eRFPath, Data);
}

VOID
phydm_ConfigKFree(
	IN	PVOID	pDM_VOID,
	IN	u1Byte	channelToSW,
	IN	pu1Byte	kfreeTable
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PODM_RF_CAL_T	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
	u1Byte			rfpath = 0, maxRFpath = 0;
	u1Byte			channelIdx = 0;

	if (pDM_Odm->SupportICType & ODM_RTL8814A)
		maxRFpath = 4;	/*0~3*/
	else if (pDM_Odm->SupportICType & (ODM_RTL8812 | ODM_RTL8192E | ODM_RTL8822B))
		maxRFpath = 2;	/*0~1*/
	else
		maxRFpath = 1;
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_MP, ODM_DBG_LOUD, ("===>phy_ConfigKFree8814A()\n"));
	
	if (pRFCalibrateInfo->RegRfKFreeEnable == 2) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): RegRfKFreeEnable == 2, Disable\n"));
		return;
	} else if (pRFCalibrateInfo->RegRfKFreeEnable == 1 || pRFCalibrateInfo->RegRfKFreeEnable == 0) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): RegRfKFreeEnable == TRUE\n"));
		/*Make sure the targetval is defined*/
		if (((pRFCalibrateInfo->RegRfKFreeEnable == 1) && (kfreeTable[0] != 0xFF)) || (pRFCalibrateInfo->RfKFreeEnable == TRUE)) {
			/*if kfreeTable[0] == 0xff, means no Kfree*/
			if (*pDM_Odm->pBandType == ODM_BAND_2_4G) {
				if (channelToSW <= 14 && channelToSW >= 1)
					channelIdx = PHYDM_2G;
			} else if (*pDM_Odm->pBandType == ODM_BAND_5G) {
				if (channelToSW >= 36 && channelToSW <= 48)
					channelIdx = PHYDM_5GLB1;
				if (channelToSW >= 52 && channelToSW <= 64)
					channelIdx = PHYDM_5GLB2;
				if (channelToSW >= 100 && channelToSW <= 120)
					channelIdx = PHYDM_5GMB1;
				if (channelToSW >= 124 && channelToSW <= 144)
					channelIdx = PHYDM_5GMB2;
				if (channelToSW >= 149 && channelToSW <= 177)
					channelIdx = PHYDM_5GHB;
			}

			for (rfpath = ODM_RF_PATH_A;  rfpath < maxRFpath; rfpath++) {
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phydm_kfree(): PATH_%d: %#x\n", rfpath, kfreeTable[channelIdx*maxRFpath + rfpath]));
				phydm_SetKfreeToRF(pDM_Odm, rfpath, kfreeTable[channelIdx*maxRFpath + rfpath]);
			}
		} else {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): targetval not defined, Don't execute KFree Process.\n"));
			return;
		}
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_MP, ODM_DBG_LOUD, ("<===phy_ConfigKFree8814A()\n"));
}

