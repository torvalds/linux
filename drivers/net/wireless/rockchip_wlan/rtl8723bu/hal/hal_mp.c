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
#define _HAL_MP_C_
#ifdef CONFIG_MP_INCLUDED

#ifdef CONFIG_RTL8188E
#include <rtl8188e_hal.h>
#endif
#ifdef CONFIG_RTL8723B
#include <rtl8723b_hal.h>
#endif
#ifdef CONFIG_RTL8192E
#include <rtl8192e_hal.h>
#endif
#ifdef CONFIG_RTL8814A
#include <rtl8814a_hal.h>
#endif
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
#include <rtl8812a_hal.h>
#endif
#ifdef CONFIG_RTL8703B
#include <rtl8703b_hal.h>
#endif
#ifdef CONFIG_RTL8188F
#include <rtl8188f_hal.h>
#endif


u8 MgntQuery_NssTxRate(u16 Rate)
{
	u8	NssNum = RF_TX_NUM_NONIMPLEMENT;
	
	if ((Rate >= MGN_MCS8 && Rate <= MGN_MCS15) || 
		 (Rate >= MGN_VHT2SS_MCS0 && Rate <= MGN_VHT2SS_MCS9))
		NssNum = RF_2TX;
	else if ((Rate >= MGN_MCS16 && Rate <= MGN_MCS23) || 
		 (Rate >= MGN_VHT3SS_MCS0 && Rate <= MGN_VHT3SS_MCS9))
		NssNum = RF_3TX;
	else if ((Rate >= MGN_MCS24 && Rate <= MGN_MCS31) || 
		 (Rate >= MGN_VHT4SS_MCS0 && Rate <= MGN_VHT4SS_MCS9))
		NssNum = RF_4TX;
	else
		NssNum = RF_1TX;
		
	return NssNum;
}

void hal_mpt_SwitchRfSetting(PADAPTER	pAdapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.MptCtx);
	u8				ChannelToSw = pMptCtx->MptChannelToSw;
	ULONG				ulRateIdx = pMptCtx->MptRateIndex;
	ULONG				ulbandwidth = pMptCtx->MptBandWidth;
	
	/* <20120525, Kordan> Dynamic mechanism for APK, asked by Dennis.*/
	if (IS_HARDWARE_TYPE_8188ES(pAdapter) && (1 <= ChannelToSw && ChannelToSw <= 11) &&
		(ulRateIdx == MPT_RATE_MCS0 || ulRateIdx == MPT_RATE_1M || ulRateIdx == MPT_RATE_6M)) {
		pMptCtx->backup0x52_RF_A = (u1Byte)PHY_QueryRFReg(pAdapter, ODM_RF_PATH_A, RF_0x52, 0x000F0);
		pMptCtx->backup0x52_RF_B = (u1Byte)PHY_QueryRFReg(pAdapter, ODM_RF_PATH_B, RF_0x52, 0x000F0);
		
		if ((PlatformEFIORead4Byte(pAdapter, 0xF4)&BIT29) == BIT29) {
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_0x52, 0x000F0, 0xB);
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_0x52, 0x000F0, 0xB);
		} else {
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_0x52, 0x000F0, 0xD);
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_0x52, 0x000F0, 0xD);
		}
	} else if (IS_HARDWARE_TYPE_8188EE(pAdapter)) { /* <20140903, VincentL> Asked by RF Eason and Edlu*/
	
		if (ChannelToSw == 3 && ulbandwidth == MPT_BW_40MHZ) {
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_0x52, 0x000F0, 0xB); /*RF 0x52 = 0x0007E4BD*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_0x52, 0x000F0, 0xB); /*RF 0x52 = 0x0007E4BD*/
		} else {
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_0x52, 0x000F0, 0x9); /*RF 0x52 = 0x0007E49D*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_0x52, 0x000F0, 0x9); /*RF 0x52 = 0x0007E49D*/
		}
		
	} else if (IS_HARDWARE_TYPE_8188E(pAdapter)) {
		PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_0x52, 0x000F0, pMptCtx->backup0x52_RF_A);
		PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_0x52, 0x000F0, pMptCtx->backup0x52_RF_B);
	}
}

s32 hal_mpt_SetPowerTracking(PADAPTER padapter, u8 enable)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);


	if (!netif_running(padapter->pnetdev)) {
		RT_TRACE(_module_mp_, _drv_warning_, ("SetPowerTracking! Fail: interface not opened!\n"));
		return _FAIL;
	}

	if (check_fwstate(&padapter->mlmepriv, WIFI_MP_STATE) == _FALSE) {
		RT_TRACE(_module_mp_, _drv_warning_, ("SetPowerTracking! Fail: not in MP mode!\n"));
		return _FAIL;
	}
	if (enable)
		pDM_Odm->RFCalibrateInfo.TxPowerTrackControl = _TRUE;	
	else
		pDM_Odm->RFCalibrateInfo.TxPowerTrackControl = _FALSE;

	return _SUCCESS;
}

void hal_mpt_GetPowerTracking(PADAPTER padapter, u8 *enable)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);


	*enable = pDM_Odm->RFCalibrateInfo.TxPowerTrackControl;
}


void hal_mpt_CCKTxPowerAdjust(PADAPTER Adapter, BOOLEAN bInCH14)
{
	u32		TempVal = 0, TempVal2 = 0, TempVal3 = 0;
	u32		CurrCCKSwingVal = 0, CCKSwingIndex = 12;
	u8		i;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	
	PMPT_CONTEXT		pMptCtx = &(Adapter->mppriv.MptCtx);
	u1Byte				u1Channel = pHalData->CurrentChannel;
	ULONG				ulRateIdx = pMptCtx->MptRateIndex;
	u1Byte				DataRate = 0xFF;

	DataRate = MptToMgntRate(ulRateIdx);
	
	if (u1Channel == 14 && IS_CCK_RATE(DataRate))
		pHalData->bCCKinCH14 = TRUE;
	else
		pHalData->bCCKinCH14 = FALSE;	

	if (IS_HARDWARE_TYPE_8703B(Adapter)) {
			if ((u1Channel == 14) && IS_CCK_RATE(DataRate)) {
				/* Channel 14 in CCK, need to set 0xA26~0xA29 to 0 for 8703B */ 
				PHY_SetBBReg(Adapter, rCCK0_TxFilter2, bMaskHWord, 0);
				PHY_SetBBReg(Adapter, rCCK0_DebugPort, bMaskLWord, 0);

				RT_TRACE(_module_mp_, DBG_LOUD, ("MPT_CCKTxPowerAdjust 8703B CCK in Channel %u\n", u1Channel));
			} else {
				/* Normal setting for 8703B, just recover to the default setting. */
				/* This hardcore values reference from the parameter which BB team gave. */
				for (i = 0 ; i < 2 ; ++i)
					PHY_SetBBReg(Adapter, pHalData->RegForRecover[i].offset, bMaskDWord, pHalData->RegForRecover[i].value);

				RT_TRACE(_module_mp_, DBG_LOUD, ("MPT_CCKTxPowerAdjust 8703B in Channel %u restore to default setting\n", u1Channel));
			}
	} else if (IS_HARDWARE_TYPE_8188F(Adapter)) {
		/* No difference between CCK in CH14 and others, no need to change TX filter */
	} else {

		/* get current cck swing value and check 0xa22 & 0xa23 later to match the table.*/
		CurrCCKSwingVal = read_bbreg(Adapter, rCCK0_TxFilter1, bMaskHWord);

		if (!pHalData->bCCKinCH14) {
			/* Readback the current bb cck swing value and compare with the table to */
			/* get the current swing index */
			for (i = 0; i < CCK_TABLE_SIZE; i++) {
				if (((CurrCCKSwingVal&0xff) == (u32)CCKSwingTable_Ch1_Ch13[i][0]) &&
					(((CurrCCKSwingVal&0xff00)>>8) == (u32)CCKSwingTable_Ch1_Ch13[i][1])) {
					CCKSwingIndex = i;
					RT_TRACE(_module_mp_, DBG_LOUD, ("Ch1~13, Current reg0x%x = 0x%lx, CCKSwingIndex=0x%x\n",
						(rCCK0_TxFilter1+2), CurrCCKSwingVal, CCKSwingIndex));
					break;
				}
			}

		/*Write 0xa22 0xa23*/
		TempVal = CCKSwingTable_Ch1_Ch13[CCKSwingIndex][0] +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][1]<<8);


		/*Write 0xa24 ~ 0xa27*/
		TempVal2 = 0;
		TempVal2 = CCKSwingTable_Ch1_Ch13[CCKSwingIndex][2] +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][3]<<8) +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][4]<<16) +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][5]<<24);

		/*Write 0xa28  0xa29*/
		TempVal3 = 0;
		TempVal3 = CCKSwingTable_Ch1_Ch13[CCKSwingIndex][6] +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][7]<<8);
	}  else {
		for (i = 0; i < CCK_TABLE_SIZE; i++) {
			if (((CurrCCKSwingVal&0xff) == (u32)CCKSwingTable_Ch14[i][0]) &&
				(((CurrCCKSwingVal&0xff00)>>8) == (u32)CCKSwingTable_Ch14[i][1])) {
				CCKSwingIndex = i;
				RT_TRACE(_module_mp_, DBG_LOUD, ("Ch14, Current reg0x%x = 0x%lx, CCKSwingIndex=0x%x\n",
					(rCCK0_TxFilter1+2), CurrCCKSwingVal, CCKSwingIndex));
				break;
			}
		}

		/*Write 0xa22 0xa23*/
		TempVal = CCKSwingTable_Ch14[CCKSwingIndex][0] +
				(CCKSwingTable_Ch14[CCKSwingIndex][1]<<8);

		/*Write 0xa24 ~ 0xa27*/
		TempVal2 = 0;
		TempVal2 = CCKSwingTable_Ch14[CCKSwingIndex][2] +
				(CCKSwingTable_Ch14[CCKSwingIndex][3]<<8) +
				(CCKSwingTable_Ch14[CCKSwingIndex][4]<<16) +
				(CCKSwingTable_Ch14[CCKSwingIndex][5]<<24);

		/*Write 0xa28  0xa29*/
		TempVal3 = 0;
		TempVal3 = CCKSwingTable_Ch14[CCKSwingIndex][6] +
				(CCKSwingTable_Ch14[CCKSwingIndex][7]<<8);
	}

	write_bbreg(Adapter, rCCK0_TxFilter1, bMaskHWord, TempVal);
	write_bbreg(Adapter, rCCK0_TxFilter2, bMaskDWord, TempVal2);
	write_bbreg(Adapter, rCCK0_DebugPort, bMaskLWord, TempVal3);

	}

}

void hal_mpt_SetChannel(PADAPTER pAdapter)
{
	u8 eRFPath;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	struct mp_priv	*pmp = &pAdapter->mppriv;
	u8		channel = pmp->channel;
	u8		bandwidth = pmp->bandwidth;

	hal_mpt_SwitchRfSetting(pAdapter);
	
	SelectChannel(pAdapter, channel);
	
	pHalData->bSwChnl = _TRUE;
	pHalData->bSetChnlBW = _TRUE;
	rtw_hal_set_chnl_bw(pAdapter, channel, bandwidth, 0, 0);

	hal_mpt_CCKTxPowerAdjust(pAdapter, pHalData->bCCKinCH14);

}

/*
 * Notice
 *	Switch bandwitdth may change center frequency(channel)
 */
void hal_mpt_SetBandwidth(PADAPTER pAdapter)
{
	struct mp_priv *pmp = &pAdapter->mppriv;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	
	u8		channel = pmp->channel;
	u8		bandwidth = pmp->bandwidth;
	
	SetBWMode(pAdapter, pmp->bandwidth, pmp->prime_channel_offset);
	pHalData->bSwChnl = _TRUE;
	pHalData->bSetChnlBW = _TRUE;
	rtw_hal_set_chnl_bw(pAdapter, channel, bandwidth, 0, 0);
	
	hal_mpt_SwitchRfSetting(pAdapter);
}

void mpt_SetTxPower_Old(PADAPTER pAdapter, MPT_TXPWR_DEF Rate, u8 *pTxPower)
{
	RT_TRACE(_module_mp_, DBG_LOUD, ("===>mpt_SetTxPower_Old(): Case = %d\n", Rate));
	switch (Rate) {
	case MPT_CCK:
			{
			u4Byte	TxAGC = 0, pwr = 0;
			u1Byte	rf;

			pwr = pTxPower[ODM_RF_PATH_A];
			if (pwr < 0x3f) {
				TxAGC = (pwr<<16)|(pwr<<8)|(pwr);
				PHY_SetBBReg(pAdapter, rTxAGC_A_CCK1_Mcs32, bMaskByte1, pTxPower[ODM_RF_PATH_A]);
				PHY_SetBBReg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, 0xffffff00, TxAGC);
			}
			pwr = pTxPower[ODM_RF_PATH_B];
			if (pwr < 0x3f) {
				TxAGC = (pwr<<16)|(pwr<<8)|(pwr);
				PHY_SetBBReg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte0, pTxPower[ODM_RF_PATH_B]);
				PHY_SetBBReg(pAdapter, rTxAGC_B_CCK1_55_Mcs32, 0xffffff00, TxAGC);
			}
		    
			} break;

	case MPT_OFDM_AND_HT:
			{
			u4Byte	TxAGC = 0;
			u1Byte	pwr = 0, rf;
			
			pwr = pTxPower[0];
			if (pwr < 0x3f) {
				TxAGC |= ((pwr<<24)|(pwr<<16)|(pwr<<8)|pwr);
				DBG_871X("HT Tx-rf(A) Power = 0x%x\n", TxAGC);
				
				PHY_SetBBReg(pAdapter, rTxAGC_A_Rate18_06, bMaskDWord, TxAGC);
				PHY_SetBBReg(pAdapter, rTxAGC_A_Rate54_24, bMaskDWord, TxAGC);
				PHY_SetBBReg(pAdapter, rTxAGC_A_Mcs03_Mcs00, bMaskDWord, TxAGC);
				PHY_SetBBReg(pAdapter, rTxAGC_A_Mcs07_Mcs04, bMaskDWord, TxAGC);
				PHY_SetBBReg(pAdapter, rTxAGC_A_Mcs11_Mcs08, bMaskDWord, TxAGC);
				PHY_SetBBReg(pAdapter, rTxAGC_A_Mcs15_Mcs12, bMaskDWord, TxAGC);
			}
			TxAGC = 0;
			pwr = pTxPower[1];
			if (pwr < 0x3f) {
				TxAGC |= ((pwr<<24)|(pwr<<16)|(pwr<<8)|pwr);
				DBG_871X("HT Tx-rf(B) Power = 0x%x\n", TxAGC);
				
				PHY_SetBBReg(pAdapter, rTxAGC_B_Rate18_06, bMaskDWord, TxAGC);
				PHY_SetBBReg(pAdapter, rTxAGC_B_Rate54_24, bMaskDWord, TxAGC);
				PHY_SetBBReg(pAdapter, rTxAGC_B_Mcs03_Mcs00, bMaskDWord, TxAGC);
				PHY_SetBBReg(pAdapter, rTxAGC_B_Mcs07_Mcs04, bMaskDWord, TxAGC);
				PHY_SetBBReg(pAdapter, rTxAGC_B_Mcs11_Mcs08, bMaskDWord, TxAGC);
				PHY_SetBBReg(pAdapter, rTxAGC_B_Mcs15_Mcs12, bMaskDWord, TxAGC);
			}
			} break;

	default:
		break;
	}	
		DBG_871X("<===mpt_SetTxPower_Old()\n");
}



void 
mpt_SetTxPower(
		PADAPTER		pAdapter,
		MPT_TXPWR_DEF	Rate,
		pu1Byte	pTxPower
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	u1Byte path = 0 , i = 0, MaxRate = MGN_6M;
	u1Byte StartPath = ODM_RF_PATH_A, EndPath = ODM_RF_PATH_B;
	
	if (IS_HARDWARE_TYPE_8814A(pAdapter))
		EndPath = ODM_RF_PATH_D;

	switch (Rate) {
	case MPT_CCK:
			{
			u1Byte rate[] = {MGN_1M, MGN_2M, MGN_5_5M, MGN_11M};

			for (path = StartPath; path <= EndPath; path++)
				for (i = 0; i < sizeof(rate); ++i)
					PHY_SetTxPowerIndex(pAdapter, pTxPower[path], path, rate[i]);	
			}
			break;
		
	case MPT_OFDM:
			{
			u1Byte rate[] = {
				MGN_6M, MGN_9M, MGN_12M, MGN_18M,
				MGN_24M, MGN_36M, MGN_48M, MGN_54M,
				};

			for (path = StartPath; path <= EndPath; path++)
				for (i = 0; i < sizeof(rate); ++i)
					PHY_SetTxPowerIndex(pAdapter, pTxPower[path], path, rate[i]);	
			} break;
		
	case MPT_HT:
			{
			u1Byte rate[] = {
			MGN_MCS0, MGN_MCS1, MGN_MCS2, MGN_MCS3, MGN_MCS4,
			MGN_MCS5, MGN_MCS6, MGN_MCS7, MGN_MCS8, MGN_MCS9,
			MGN_MCS10, MGN_MCS11, MGN_MCS12, MGN_MCS13, MGN_MCS14,
			MGN_MCS15, MGN_MCS16, MGN_MCS17, MGN_MCS18, MGN_MCS19,
			MGN_MCS20, MGN_MCS21, MGN_MCS22, MGN_MCS23, MGN_MCS24,
			MGN_MCS25, MGN_MCS26, MGN_MCS27, MGN_MCS28, MGN_MCS29,
			MGN_MCS30, MGN_MCS31,
			};
			if (pHalData->rf_type == RF_3T3R)
				MaxRate = MGN_MCS23;
			else if (pHalData->rf_type == RF_2T2R)
				MaxRate = MGN_MCS15;
			else
				MaxRate = MGN_MCS7;
			
			for (path = StartPath; path <= EndPath; path++) {
				for (i = 0; i < sizeof(rate); ++i) {
					if (rate[i] > MaxRate)
						break;					
				    PHY_SetTxPowerIndex(pAdapter, pTxPower[path], path, rate[i]);
				}
			}
			} break;
		
	case MPT_VHT:
			{
			u1Byte rate[] = {
			MGN_VHT1SS_MCS0, MGN_VHT1SS_MCS1, MGN_VHT1SS_MCS2, MGN_VHT1SS_MCS3, MGN_VHT1SS_MCS4,
			MGN_VHT1SS_MCS5, MGN_VHT1SS_MCS6, MGN_VHT1SS_MCS7, MGN_VHT1SS_MCS8, MGN_VHT1SS_MCS9,
			MGN_VHT2SS_MCS0, MGN_VHT2SS_MCS1, MGN_VHT2SS_MCS2, MGN_VHT2SS_MCS3, MGN_VHT2SS_MCS4,
			MGN_VHT2SS_MCS5, MGN_VHT2SS_MCS6, MGN_VHT2SS_MCS7, MGN_VHT2SS_MCS8, MGN_VHT2SS_MCS9,
			MGN_VHT3SS_MCS0, MGN_VHT3SS_MCS1, MGN_VHT3SS_MCS2, MGN_VHT3SS_MCS3, MGN_VHT3SS_MCS4,
			MGN_VHT3SS_MCS5, MGN_VHT3SS_MCS6, MGN_VHT3SS_MCS7, MGN_VHT3SS_MCS8, MGN_VHT3SS_MCS9,
			MGN_VHT4SS_MCS0, MGN_VHT4SS_MCS1, MGN_VHT4SS_MCS2, MGN_VHT4SS_MCS3, MGN_VHT4SS_MCS4,
			MGN_VHT4SS_MCS5, MGN_VHT4SS_MCS6, MGN_VHT4SS_MCS7, MGN_VHT4SS_MCS8, MGN_VHT4SS_MCS9,
			};
					
			if (pHalData->rf_type == RF_3T3R)
				MaxRate = MGN_VHT3SS_MCS9;
			else if (pHalData->rf_type == RF_2T2R || pHalData->rf_type == RF_2T4R)
				MaxRate = MGN_VHT2SS_MCS9;
			else
				MaxRate = MGN_VHT1SS_MCS9;

			for (path = StartPath; path <= EndPath; path++) {
				for (i = 0; i < sizeof(rate); ++i) {
					if (rate[i] > MaxRate)
						break;	
					PHY_SetTxPowerIndex(pAdapter, pTxPower[path], path, rate[i]);
				}
			}
			} break;
			
	default:
			DBG_871X("<===mpt_SetTxPower: Illegal channel!!\n");
			break;
	}

}


void hal_mpt_SetTxPower(PADAPTER pAdapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.MptCtx);
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;

	if (pHalData->rf_chip < RF_TYPE_MAX) {
		if (IS_HARDWARE_TYPE_8188E(pAdapter) || 
			IS_HARDWARE_TYPE_8723B(pAdapter) || 
			IS_HARDWARE_TYPE_8192E(pAdapter) || 
			IS_HARDWARE_TYPE_8703B(pAdapter) ||
			IS_HARDWARE_TYPE_8188F(pAdapter)) {
			u8 path = (pHalData->AntennaTxPath == ANTENNA_A) ? (ODM_RF_PATH_A) : (ODM_RF_PATH_B);

			DBG_8192C("===> MPT_ProSetTxPower: Old\n");

			RT_TRACE(_module_mp_, DBG_LOUD, ("===> MPT_ProSetTxPower[Old]:\n"));
			mpt_SetTxPower_Old(pAdapter, MPT_CCK, pMptCtx->TxPwrLevel);		
			mpt_SetTxPower_Old(pAdapter, MPT_OFDM_AND_HT, pMptCtx->TxPwrLevel);

		} else {
			DBG_871X("===> MPT_ProSetTxPower: Jaguar\n");
			mpt_SetTxPower(pAdapter, MPT_CCK, pMptCtx->TxPwrLevel);
			mpt_SetTxPower(pAdapter, MPT_OFDM, pMptCtx->TxPwrLevel);
			mpt_SetTxPower(pAdapter, MPT_HT, pMptCtx->TxPwrLevel);
			mpt_SetTxPower(pAdapter, MPT_VHT, pMptCtx->TxPwrLevel);

			}
	} else
		DBG_8192C("RFChipID < RF_TYPE_MAX, the RF chip is not supported - %d\n", pHalData->rf_chip);

	ODM_ClearTxPowerTrackingState(pDM_Odm);

}


void hal_mpt_SetDataRate(PADAPTER pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.MptCtx);
	u32 DataRate;
	
	DataRate = MptToMgntRate(pMptCtx->MptRateIndex);
	
	hal_mpt_SwitchRfSetting(pAdapter);

	hal_mpt_CCKTxPowerAdjust(pAdapter, pHalData->bCCKinCH14);
#ifdef CONFIG_RTL8723B
	if (IS_HARDWARE_TYPE_8723B(pAdapter) || IS_HARDWARE_TYPE_8188F(pAdapter)) {
		if (IS_CCK_RATE(DataRate)) {
			if (pMptCtx->MptRfPath == ODM_RF_PATH_A)
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, 0xF, 0x6);	
			else
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x71, 0xF, 0x6);
		} else {
			if (pMptCtx->MptRfPath == ODM_RF_PATH_A)
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, 0xF, 0xE);	
			else
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x71, 0xF, 0xE);		
		}
	}
	
	if ((IS_HARDWARE_TYPE_8723BS(pAdapter) && 
		  ((pHalData->PackageType == PACKAGE_TFBGA79) || (pHalData->PackageType == PACKAGE_TFBGA90)))) {
		if (pMptCtx->MptRfPath == ODM_RF_PATH_A)
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, 0xF, 0xE);	
		else
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x71, 0xF, 0xE);			
	}
#endif	
}


#define RF_PATH_AB	22

#ifdef CONFIG_RTL8814A
VOID mpt_ToggleIG_8814A(PADAPTER	pAdapter)
{
	u1Byte Path = 0;
	u4Byte IGReg = rA_IGI_Jaguar, IGvalue = 0;

	for (Path; Path <= ODM_RF_PATH_D; Path++) {
		switch (Path) {
		case ODM_RF_PATH_B:
			IGReg = rB_IGI_Jaguar;
			break;
		case ODM_RF_PATH_C:
			IGReg = rC_IGI_Jaguar2;
			break;
		case ODM_RF_PATH_D:
			IGReg = rD_IGI_Jaguar2;
			break;
		default:
			IGReg = rA_IGI_Jaguar;
			break;
		}

		IGvalue = PHY_QueryBBReg(pAdapter, IGReg, bMaskByte0);
		PHY_SetBBReg(pAdapter, IGReg, bMaskByte0, IGvalue+2);	   
		PHY_SetBBReg(pAdapter, IGReg, bMaskByte0, IGvalue);
	}
}

VOID mpt_SetRFPath_8814A(PADAPTER	pAdapter)
{

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.MptCtx;
	R_ANTENNA_SELECT_OFDM	*p_ofdm_tx;	/* OFDM Tx register */
	R_ANTENNA_SELECT_CCK	*p_cck_txrx;
	u8	ForcedDataRate = MptToMgntRate(pMptCtx->MptRateIndex);
	u8	HtStbcCap = pAdapter->registrypriv.stbc_cap;
	/*/PRT_HIGH_THROUGHPUT		pHTInfo = GET_HT_INFO(pMgntInfo);*/
	/*/PRT_VERY_HIGH_THROUGHPUT	pVHTInfo = GET_VHT_INFO(pMgntInfo);*/

	u32	ulAntennaTx = pHalData->AntennaTxPath;
	u32	ulAntennaRx = pHalData->AntennaRxPath;
	u8	NssforRate = MgntQuery_NssTxRate(ForcedDataRate);

	if ((NssforRate == RF_2TX) || ((NssforRate == RF_1TX) && IS_HT_RATE(ForcedDataRate)) || ((NssforRate == RF_1TX) && IS_VHT_RATE(ForcedDataRate))) {
		DBG_871X("===> SetAntenna 2T ForcedDataRate %d NssforRate %d AntennaTx %d\n", ForcedDataRate, NssforRate, ulAntennaTx);

		switch (ulAntennaTx) {
		case ANTENNA_BC:
				pMptCtx->MptRfPath = ODM_RF_PATH_BC;
				/*pHalData->ValidTxPath = 0x06; linux no use */
				PHY_SetBBReg(pAdapter, rTxAnt_23Nsts_Jaguar2, 0x0000fff0, 0x106);	/*/ 0x940[15:4]=12'b0000_0100_0011*/
				break;

		case ANTENNA_CD:
				pMptCtx->MptRfPath = ODM_RF_PATH_CD;
				/*pHalData->ValidTxPath = 0x0C;*/
				PHY_SetBBReg(pAdapter, rTxAnt_23Nsts_Jaguar2, 0x0000fff0, 0x40c);	/*/ 0x940[15:4]=12'b0000_0100_0011*/
				break;
		case ANTENNA_AB: default:
				pMptCtx->MptRfPath = ODM_RF_PATH_AB;
				/*pHalData->ValidTxPath = 0x03;*/
				PHY_SetBBReg(pAdapter, rTxAnt_23Nsts_Jaguar2, 0x0000fff0, 0x043);	/*/ 0x940[15:4]=12'b0000_0100_0011*/
				break;
		}

	} else if (NssforRate == RF_3TX) {
				DBG_871X("===> SetAntenna 3T ForcedDataRate %d NssforRate %d AntennaTx %d\n", ForcedDataRate, NssforRate, ulAntennaTx);

		switch (ulAntennaTx) {
		case ANTENNA_BCD:
				pMptCtx->MptRfPath = ODM_RF_PATH_BCD;
				/*pHalData->ValidTxPath = 0x0e;*/
				PHY_SetBBReg(pAdapter, rTxAnt_23Nsts_Jaguar2, 0x0fff0000, 0x90e);	/*/ 0x940[27:16]=12'b0010_0100_0111*/
				break;

		case ANTENNA_ABC: default:
				pMptCtx->MptRfPath = ODM_RF_PATH_ABC;
				/*pHalData->ValidTxPath = 0x0d;*/
				PHY_SetBBReg(pAdapter, rTxAnt_23Nsts_Jaguar2, 0x0fff0000, 0x247);	/*/ 0x940[27:16]=12'b0010_0100_0111*/
				break;
		}

	} else { /*/if(NssforRate == RF_1TX)*/
		DBG_871X("===> SetAntenna 1T ForcedDataRate %d NssforRate %d AntennaTx %d\n", ForcedDataRate, NssforRate, ulAntennaTx);
		switch (ulAntennaTx) {
		case ANTENNA_BCD:
				pMptCtx->MptRfPath = ODM_RF_PATH_BCD;
				/*pHalData->ValidTxPath = 0x0e;*/
				PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0xf0000000, 0x7);
				PHY_SetBBReg(pAdapter, rTxAnt_1Nsts_Jaguar2, 0x000f00000, 0xe);	
				PHY_SetBBReg(pAdapter, rTxPath_Jaguar, 0xf0, 0xe);
				break;

		case ANTENNA_BC:
				pMptCtx->MptRfPath = ODM_RF_PATH_BC;
				/*pHalData->ValidTxPath = 0x06;*/
				PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0xf0000000, 0x6);
				PHY_SetBBReg(pAdapter, rTxAnt_1Nsts_Jaguar2, 0x000f00000, 0x6);	
				PHY_SetBBReg(pAdapter, rTxPath_Jaguar, 0xf0, 0x6);
				break;
		case ANTENNA_B:
				pMptCtx->MptRfPath = ODM_RF_PATH_B;
				/*pHalData->ValidTxPath = 0x02;*/
				PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0xf0000000, 0x4);			/*/ 0xa07[7:4] = 4'b0100*/
				PHY_SetBBReg(pAdapter, rTxAnt_1Nsts_Jaguar2, 0xfff00000, 0x002);	/*/ 0x93C[31:20]=12'b0000_0000_0010*/
				PHY_SetBBReg(pAdapter, rTxPath_Jaguar, 0xf0, 0x2);					/* 0x80C[7:4] = 4'b0010*/
				break;

		case ANTENNA_C:
				pMptCtx->MptRfPath = ODM_RF_PATH_C;
				/*pHalData->ValidTxPath = 0x04;*/
				PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0xf0000000, 0x2);			/*/ 0xa07[7:4] = 4'b0010*/
				PHY_SetBBReg(pAdapter, rTxAnt_1Nsts_Jaguar2, 0xfff00000, 0x004);	/*/ 0x93C[31:20]=12'b0000_0000_0100*/
				PHY_SetBBReg(pAdapter, rTxPath_Jaguar, 0xf0, 0x4);					/*/ 0x80C[7:4] = 4'b0100*/
				break;

		case ANTENNA_D:
				pMptCtx->MptRfPath = ODM_RF_PATH_D;
				/*pHalData->ValidTxPath = 0x08;*/
				PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0xf0000000, 0x1);			/*/ 0xa07[7:4] = 4'b0001*/
				PHY_SetBBReg(pAdapter, rTxAnt_1Nsts_Jaguar2, 0xfff00000, 0x008);	/*/ 0x93C[31:20]=12'b0000_0000_1000*/
				PHY_SetBBReg(pAdapter, rTxPath_Jaguar, 0xf0, 0x8);					/*/ 0x80C[7:4] = 4'b1000*/
				break;

		case ANTENNA_A: default:
				pMptCtx->MptRfPath = ODM_RF_PATH_A;
				/*pHalData->ValidTxPath = 0x01;*/
				PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0xf0000000, 0x8);			/*/ 0xa07[7:4] = 4'b1000*/
				PHY_SetBBReg(pAdapter, rTxAnt_1Nsts_Jaguar2, 0xfff00000, 0x001);	/*/ 0x93C[31:20]=12'b0000_0000_0001*/
				PHY_SetBBReg(pAdapter, rTxPath_Jaguar, 0xf0, 0x1);					/*/ 0x80C[7:4] = 4'b0001*/
				break;
		}
	}

	switch (ulAntennaRx) {
	case ANTENNA_A:
			/*pHalData->ValidRxPath = 0x01;*/
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x2);
			PHY_SetBBReg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x11);
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x3);
			PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0x0C000000, 0x0);
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_A_0x0[19:16] = 3, RX mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_B_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_C_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_D_0x0[19:16] = 1, Standby mode*/
			/*/ CCA related PD_delay_th*/
			PHY_SetBBReg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x5);
			PHY_SetBBReg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0xA);
			break;

	case ANTENNA_B:
			/*pHalData->ValidRxPath = 0x02;*/
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x2);
			PHY_SetBBReg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x22);	
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x3);
			PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0x0C000000, 0x1);	
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_A_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_B_0x0[19:16] = 3, RX mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_C_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_D_0x0[19:16] = 1, Standby mode*/
			/*/ CCA related PD_delay_th*/
			PHY_SetBBReg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x5);
			PHY_SetBBReg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0xA);
			break;

	case ANTENNA_C:
			/*pHalData->ValidRxPath = 0x04;*/
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x2);
			PHY_SetBBReg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x44);	
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x3);
			PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0x0C000000, 0x2);
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_A_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_B_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_C_0x0[19:16] = 3, RX mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_D_0x0[19:16] = 1, Standby mode*/
			/*/ CCA related PD_delay_th*/
			PHY_SetBBReg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x5);
			PHY_SetBBReg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0xA);
			break;

	case ANTENNA_D:
			/*pHalData->ValidRxPath = 0x08;*/
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x2);
			PHY_SetBBReg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x88);	
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x3);
			PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0x0C000000, 0x3);
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_A_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_B_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_C_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_D_0x0[19:16] = 3, RX mode*/
			/*/ CCA related PD_delay_th*/
			PHY_SetBBReg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x5);
			PHY_SetBBReg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0xA);
			break;

	case ANTENNA_BC: 
			/*pHalData->ValidRxPath = 0x06;*/
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x2);
			PHY_SetBBReg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x66);
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x3);
			PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0x0f000000, 0x6);
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_A_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_B_0x0[19:16] = 3, RX mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_C_0x0[19:16] = 3, Rx mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_D_0x0[19:16] = 1, Standby mode*/
			/*/ CCA related PD_delay_th*/
			PHY_SetBBReg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x5);
			PHY_SetBBReg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0xA);
			break;

	case ANTENNA_CD: 
			/*pHalData->ValidRxPath = 0x0C;*/
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x2);
			PHY_SetBBReg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0xcc);
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x3);
			PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0x0f000000, 0xB);
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_A_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_B_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_C_0x0[19:16] = 3, Rx mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_D_0x0[19:16] = 3, RX mode*/
			/*/ CCA related PD_delay_th*/
			PHY_SetBBReg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x5);
			PHY_SetBBReg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0xA);
			break;

	case ANTENNA_BCD: 
			/*pHalData->ValidRxPath = 0x0e;*/
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x2);
			PHY_SetBBReg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0xee);
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x3);
			PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0x0f000000, 0x6);
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_A_0x0[19:16] = 1, Standby mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_B_0x0[19:16] = 3, RX mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_C_0x0[19:16] = 3, RX mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_D_0x0[19:16] = 3, Rx mode*/
			/*/ CCA related PD_delay_th*/
			PHY_SetBBReg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x3);
			PHY_SetBBReg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0x8);
			break;

	case ANTENNA_ABCD: 
			/*pHalData->ValidRxPath = 0x0f;*/
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x2);
			PHY_SetBBReg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0xff);
			PHY_SetBBReg(pAdapter, 0x1000, bMaskByte2, 0x3);
			PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, 0x0f000000, 0x1);
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_A_0x0[19:16] = 3, RX mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_B_0x0[19:16] = 3, RX mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_C_0x0[19:16] = 3, RX mode*/
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_D_0x0[19:16] = 3, RX mode*/
			/*/ CCA related PD_delay_th*/
			PHY_SetBBReg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x3);
			PHY_SetBBReg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0x8);
			break;

	default:
			RT_TRACE(_module_mp_, _drv_warning_, ("Unknown Rx antenna.\n"));
			break;
	}

	PHY_Set_SecCCATH_by_RXANT_8814A(pAdapter, ulAntennaRx);

	mpt_ToggleIG_8814A(pAdapter);
	RT_TRACE(_module_mp_, _drv_notice_, ("-SwitchAntenna: finished\n"));
}

VOID
mpt_SetSingleTone_8814A(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN	bSingleTone,
	IN	BOOLEAN	bEnPMacTx)
{

	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);
	u1Byte StartPath = ODM_RF_PATH_A,  EndPath = ODM_RF_PATH_A;
	static u4Byte		regIG0 = 0, regIG1 = 0, regIG2 = 0, regIG3 = 0;

	if (bSingleTone) {		
		regIG0 = PHY_QueryBBReg(pAdapter, rA_TxScale_Jaguar, bMaskDWord);		/*/ 0xC1C[31:21]*/
		regIG1 = PHY_QueryBBReg(pAdapter, rB_TxScale_Jaguar, bMaskDWord);		/*/ 0xE1C[31:21]*/
		regIG2 = PHY_QueryBBReg(pAdapter, rC_TxScale_Jaguar2, bMaskDWord);	/*/ 0x181C[31:21]*/
		regIG3 = PHY_QueryBBReg(pAdapter, rD_TxScale_Jaguar2, bMaskDWord);	/*/ 0x1A1C[31:21]*/

		switch (pMptCtx->MptRfPath) {
		case ODM_RF_PATH_A: case ODM_RF_PATH_B:
		case ODM_RF_PATH_C: case ODM_RF_PATH_D:
			StartPath = pMptCtx->MptRfPath;
			EndPath = pMptCtx->MptRfPath;
			break;
		case ODM_RF_PATH_AB:
			EndPath = ODM_RF_PATH_B;
			break;
		case ODM_RF_PATH_BC:
			StartPath = ODM_RF_PATH_B;
			EndPath = ODM_RF_PATH_C;
			break;
		case ODM_RF_PATH_ABC:
			EndPath = ODM_RF_PATH_C;
			break;
		case ODM_RF_PATH_BCD:
			StartPath = ODM_RF_PATH_B;
			EndPath = ODM_RF_PATH_D;
			break;
		case ODM_RF_PATH_ABCD:
			EndPath = ODM_RF_PATH_D;
			break;
		}

		if (bEnPMacTx == FALSE) {
			hal_mpt_SetOFDMContinuousTx(pAdapter, _TRUE);
			issue_nulldata(pAdapter, NULL, 1, 3, 500);
		}

		PHY_SetBBReg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x1); /*/ Disable CCA*/

		for (StartPath; StartPath <= EndPath; StartPath++) {
			PHY_SetRFReg(pAdapter, StartPath, RF_AC_Jaguar, 0xF0000, 0x2); /*/ Tx mode: RF0x00[19:16]=4'b0010 */
			PHY_SetRFReg(pAdapter, StartPath, RF_AC_Jaguar, 0x1F, 0x0); /*/ Lowest RF gain index: RF_0x0[4:0] = 0*/

			PHY_SetRFReg(pAdapter, StartPath, LNA_Low_Gain_3, BIT1, 0x1); /*/ RF LO enabled*/
		}

		PHY_SetBBReg(pAdapter, rA_TxScale_Jaguar, 0xFFE00000, 0); /*/ 0xC1C[31:21]*/
		PHY_SetBBReg(pAdapter, rB_TxScale_Jaguar, 0xFFE00000, 0); /*/ 0xE1C[31:21]*/
		PHY_SetBBReg(pAdapter, rC_TxScale_Jaguar2, 0xFFE00000, 0); /*/ 0x181C[31:21]*/
		PHY_SetBBReg(pAdapter, rD_TxScale_Jaguar2, 0xFFE00000, 0); /*/ 0x1A1C[31:21]*/
		
	} else {
	
		switch (pMptCtx->MptRfPath) {
		case ODM_RF_PATH_A: case ODM_RF_PATH_B:
		case ODM_RF_PATH_C: case ODM_RF_PATH_D:
				StartPath = pMptCtx->MptRfPath;
				EndPath = pMptCtx->MptRfPath;
				break;
		case ODM_RF_PATH_AB:
				EndPath = ODM_RF_PATH_B;
				break;
		case ODM_RF_PATH_BC:
				StartPath = ODM_RF_PATH_B;
				EndPath = ODM_RF_PATH_C;
				break;
		case ODM_RF_PATH_ABC:
				EndPath = ODM_RF_PATH_C;
				break;
		case ODM_RF_PATH_BCD:
				StartPath = ODM_RF_PATH_B;
				EndPath = ODM_RF_PATH_D;
				break;
		case ODM_RF_PATH_ABCD:
				EndPath = ODM_RF_PATH_D;
				break;
		}
		
		for (StartPath; StartPath <= EndPath; StartPath++)
			PHY_SetRFReg(pAdapter, StartPath, LNA_Low_Gain_3, BIT1, 0x0); /*// RF LO disabled*/

		
		PHY_SetBBReg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x0); /* Enable CCA*/

		if (bEnPMacTx == FALSE)
			hal_mpt_SetOFDMContinuousTx(pAdapter, _FALSE);

		PHY_SetBBReg(pAdapter, rA_TxScale_Jaguar, bMaskDWord, regIG0); /* 0xC1C[31:21]*/
		PHY_SetBBReg(pAdapter, rB_TxScale_Jaguar, bMaskDWord, regIG1); /* 0xE1C[31:21]*/
		PHY_SetBBReg(pAdapter, rC_TxScale_Jaguar2, bMaskDWord, regIG2); /* 0x181C[31:21]*/
		PHY_SetBBReg(pAdapter, rD_TxScale_Jaguar2, bMaskDWord, regIG3); /* 0x1A1C[31:21]*/
	}
}

#endif

#if	defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
void mpt_SetRFPath_8812A(PADAPTER pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.MptCtx;
	u32		ulAntennaTx, ulAntennaRx;

	ulAntennaTx = pHalData->AntennaTxPath;
	ulAntennaRx = pHalData->AntennaRxPath;

	switch (ulAntennaTx) {
	case ANTENNA_A:
			pMptCtx->MptRfPath = ODM_RF_PATH_A;
			PHY_SetBBReg(pAdapter, rTxPath_Jaguar, bMaskLWord, 0x1111);
			if (pHalData->RFEType == 3 && IS_HARDWARE_TYPE_8812(pAdapter))
				PHY_SetBBReg(pAdapter, r_ANTSEL_SW_Jaguar, bMask_AntselPathFollow_Jaguar, 0x0);	 
			break;
	case ANTENNA_B:
			pMptCtx->MptRfPath = ODM_RF_PATH_B;
			PHY_SetBBReg(pAdapter, rTxPath_Jaguar, bMaskLWord, 0x2222);
			if (pHalData->RFEType == 3 && IS_HARDWARE_TYPE_8812(pAdapter))
				PHY_SetBBReg(pAdapter,	r_ANTSEL_SW_Jaguar, bMask_AntselPathFollow_Jaguar, 0x1);
			break;
	case ANTENNA_AB:
			pMptCtx->MptRfPath = ODM_RF_PATH_AB;
			PHY_SetBBReg(pAdapter, rTxPath_Jaguar, bMaskLWord, 0x3333);
			if (pHalData->RFEType == 3 && IS_HARDWARE_TYPE_8812(pAdapter))
				PHY_SetBBReg(pAdapter, r_ANTSEL_SW_Jaguar, bMask_AntselPathFollow_Jaguar, 0x0);
			break;
	default:
			pMptCtx->MptRfPath = ODM_RF_PATH_AB;
			DBG_871X("Unknown Tx antenna.\n");
			break;
	}

	switch (ulAntennaRx) {
			u32 reg0xC50 = 0;
	case ANTENNA_A:
			PHY_SetBBReg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x11);	
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_B_0x0[19:16] = 1, Standby mode*/
			PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, bCCK_RX_Jaguar, 0x0);	   
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC_Jaguar, BIT19|BIT18|BIT17|BIT16, 0x3); 

			/*/ <20121101, Kordan> To prevent gain table from not switched, asked by Ynlin.*/
			reg0xC50 = PHY_QueryBBReg(pAdapter, rA_IGI_Jaguar, bMaskByte0);
			PHY_SetBBReg(pAdapter, rA_IGI_Jaguar, bMaskByte0, reg0xC50+2);	   
			PHY_SetBBReg(pAdapter, rA_IGI_Jaguar, bMaskByte0, reg0xC50);			
			break;
	case ANTENNA_B:
			PHY_SetBBReg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x22);
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1);/*/ RF_A_0x0[19:16] = 1, Standby mode */
			PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, bCCK_RX_Jaguar, 0x1);
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_AC_Jaguar, BIT19|BIT18|BIT17|BIT16, 0x3); 

			/*/ <20121101, Kordan> To prevent gain table from not switched, asked by Ynlin.*/
			reg0xC50 = PHY_QueryBBReg(pAdapter, rB_IGI_Jaguar, bMaskByte0);
			PHY_SetBBReg(pAdapter, rB_IGI_Jaguar, bMaskByte0, reg0xC50+2);	   
			PHY_SetBBReg(pAdapter, rB_IGI_Jaguar, bMaskByte0, reg0xC50);						
			break;
	case ANTENNA_AB:
			PHY_SetBBReg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x33);	
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_B_0x0[19:16] = 3, Rx mode*/
			PHY_SetBBReg(pAdapter, rCCK_RX_Jaguar, bCCK_RX_Jaguar, 0x0);	
			break;
	default:
			DBG_871X("Unknown Rx antenna.\n");
			break;
	}
	RT_TRACE(_module_mp_, _drv_notice_, ("-SwitchAntenna: finished\n"));
}
#endif


#ifdef CONFIG_RTL8723B
void mpt_SetRFPath_8723B(PADAPTER pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u8		p = 0, i = 0;
	u32		ulAntennaTx, ulAntennaRx;
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);
	PDM_ODM_T	pDM_Odm = &pHalData->odmpriv;
	PODM_RF_CAL_T	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	ulAntennaTx = pHalData->AntennaTxPath;
	ulAntennaRx = pHalData->AntennaRxPath;

	if (pHalData->rf_chip >= RF_TYPE_MAX) {
		DBG_8192C("This RF chip ID is not supported\n");
		return;
	}

	switch (pAdapter->mppriv.antenna_tx) {
	case ANTENNA_A: /*/ Actually path S1  (Wi-Fi)*/
			{
			pMptCtx->MptRfPath = ODM_RF_PATH_A;			
			PHY_SetBBReg(pAdapter, rS0S1_PathSwitch, BIT9|BIT8|BIT7, 0x0);
			PHY_SetBBReg(pAdapter, 0xB2C, BIT31, 0x0); /* AGC Table Sel*/

			/*/<20130522, Kordan> 0x51 and 0x71 should be set immediately after path switched, or they might be overwritten.*/
			if ((pHalData->PackageType == PACKAGE_TFBGA79) || (pHalData->PackageType == PACKAGE_TFBGA90))
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, bRFRegOffsetMask, 0x6B10E);
			else
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, bRFRegOffsetMask, 0x6B04E);


			for (i = 0; i < 3; ++i) {
				u4Byte offset = pRFCalibrateInfo->TxIQC_8723B[ODM_RF_PATH_A][i][0];
				u4Byte data = pRFCalibrateInfo->TxIQC_8723B[ODM_RF_PATH_A][i][1];
				
				if (offset != 0) {
					PHY_SetBBReg(pAdapter, offset, bMaskDWord, data);
					DBG_8192C("Switch to S1 TxIQC(offset, data) = (0x%X, 0x%X)\n", offset, data);
				}

			}
			for (i = 0; i < 2; ++i) {
				u4Byte offset = pRFCalibrateInfo->RxIQC_8723B[ODM_RF_PATH_A][i][0];
				u4Byte data = pRFCalibrateInfo->RxIQC_8723B[ODM_RF_PATH_A][i][1];
				
				if (offset != 0) {
					PHY_SetBBReg(pAdapter, offset, bMaskDWord, data);					
					DBG_8192C("Switch to S1 RxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
				}
			}
			}
			break;
	case ANTENNA_B: /*/ Actually path S0 (BT)*/
			{
			u4Byte offset;
			u4Byte data;
			
			pMptCtx->MptRfPath = ODM_RF_PATH_B;
			PHY_SetBBReg(pAdapter, rS0S1_PathSwitch, BIT9|BIT8|BIT7, 0x5);
			PHY_SetBBReg(pAdapter, 0xB2C, BIT31, 0x1); /*/ AGC Table Sel.*/
				
			/* <20130522, Kordan> 0x51 and 0x71 should be set immediately after path switched, or they might be overwritten.*/
			if ((pHalData->PackageType == PACKAGE_TFBGA79) || (pHalData->PackageType == PACKAGE_TFBGA90))
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, bRFRegOffsetMask, 0x6B10E);
			else
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, bRFRegOffsetMask, 0x6B04E);

			for (i = 0; i < 3; ++i) {
				/*/ <20130603, Kordan> Because BB suppors only 1T1R, we restore IQC  to S1 instead of S0.*/
				offset = pRFCalibrateInfo->TxIQC_8723B[ODM_RF_PATH_A][i][0];
				data = pRFCalibrateInfo->TxIQC_8723B[ODM_RF_PATH_B][i][1];
				if (pRFCalibrateInfo->TxIQC_8723B[ODM_RF_PATH_B][i][0] != 0) {
					PHY_SetBBReg(pAdapter, offset, bMaskDWord, data);
					DBG_8192C("Switch to S0 TxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
				}
			}
			/*/ <20130603, Kordan> Because BB suppors only 1T1R, we restore IQC to S1 instead of S0.*/
			for (i = 0; i < 2; ++i) {
				offset = pRFCalibrateInfo->RxIQC_8723B[ODM_RF_PATH_A][i][0];
				data = pRFCalibrateInfo->RxIQC_8723B[ODM_RF_PATH_B][i][1];
				
				if (pRFCalibrateInfo->RxIQC_8723B[ODM_RF_PATH_B][i][0] != 0) {
					PHY_SetBBReg(pAdapter, offset, bMaskDWord, data);
					DBG_8192C("Switch to S0 RxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
				}
			}
			}
			break;
	default:
		pMptCtx->MptRfPath = RF_PATH_AB;
		RT_TRACE(_module_mp_, _drv_notice_, ("Unknown Tx antenna.\n"));
		break;
	}
	RT_TRACE(_module_mp_, _drv_notice_, ("-SwitchAntenna: finished\n"));
}
#endif

#ifdef CONFIG_RTL8703B
void mpt_SetRFPath_8703B(PADAPTER pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u1Byte		p = 0, i = 0;
	u4Byte					ulAntennaTx, ulAntennaRx;
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.MptCtx);
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	PODM_RF_CAL_T			pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	ulAntennaTx = pHalData->AntennaTxPath;
	ulAntennaRx = pHalData->AntennaRxPath;

	if (pHalData->rf_chip >= RF_TYPE_MAX) {
		DBG_871X("This RF chip ID is not supported\n");
		return;
	}

	switch (pAdapter->mppriv.antenna_tx) {
	case ANTENNA_A: /* Actually path S1  (Wi-Fi) */
				{
				pMptCtx->MptRfPath = ODM_RF_PATH_A;			
				PHY_SetBBReg(pAdapter, rS0S1_PathSwitch, BIT9|BIT8|BIT7, 0x0);
				PHY_SetBBReg(pAdapter, 0xB2C, BIT31, 0x0); /* AGC Table Sel*/

				for (i = 0; i < 3; ++i) {
					u4Byte offset = pRFCalibrateInfo->TxIQC_8703B[i][0];
					u4Byte data = pRFCalibrateInfo->TxIQC_8703B[i][1];

					if (offset != 0) {
						PHY_SetBBReg(pAdapter, offset, bMaskDWord, data);
						DBG_871X("Switch to S1 TxIQC(offset, data) = (0x%X, 0x%X)\n", offset, data);
					}

				}
				for (i = 0; i < 2; ++i) {
					u4Byte offset = pRFCalibrateInfo->RxIQC_8703B[i][0];
					u4Byte data = pRFCalibrateInfo->RxIQC_8703B[i][1];

					if (offset != 0) {
						PHY_SetBBReg(pAdapter, offset, bMaskDWord, data);					
						DBG_871X("Switch to S1 RxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
					}
				}
				}
	break;
	case ANTENNA_B: /* Actually path S0 (BT)*/
				{
				pMptCtx->MptRfPath = ODM_RF_PATH_B;
				PHY_SetBBReg(pAdapter, rS0S1_PathSwitch, BIT9|BIT8|BIT7, 0x5);
				PHY_SetBBReg(pAdapter, 0xB2C, BIT31, 0x1); /* AGC Table Sel */

				for (i = 0; i < 3; ++i) {
					u4Byte offset = pRFCalibrateInfo->TxIQC_8703B[i][0];
					u4Byte data = pRFCalibrateInfo->TxIQC_8703B[i][1];

					if (pRFCalibrateInfo->TxIQC_8703B[i][0] != 0) {
						PHY_SetBBReg(pAdapter, offset, bMaskDWord, data);
						DBG_871X("Switch to S0 TxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
					}
				}
				for (i = 0; i < 2; ++i) {
					u4Byte offset = pRFCalibrateInfo->RxIQC_8703B[i][0];
					u4Byte data = pRFCalibrateInfo->RxIQC_8703B[i][1];

					if (pRFCalibrateInfo->RxIQC_8703B[i][0] != 0) {
						PHY_SetBBReg(pAdapter, offset, bMaskDWord, data);
						DBG_871X("Switch to S0 RxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
					}
				}
				}
	break;
	default:
			pMptCtx->MptRfPath = RF_PATH_AB; 
			RT_TRACE(_module_mp_, _drv_notice_, ("Unknown Tx antenna.\n"));
	break;
	}

	RT_TRACE(_module_mp_, _drv_notice_, ("-SwitchAntenna: finished\n"));
}
#endif


VOID mpt_SetRFPath_819X(PADAPTER	pAdapter)
{
	HAL_DATA_TYPE			*pHalData	= GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.MptCtx);
	u4Byte			ulAntennaTx, ulAntennaRx;
	R_ANTENNA_SELECT_OFDM	*p_ofdm_tx;	/* OFDM Tx register */
	R_ANTENNA_SELECT_CCK	*p_cck_txrx;
	u1Byte		r_rx_antenna_ofdm = 0, r_ant_select_cck_val = 0;
	u1Byte		chgTx = 0, chgRx = 0;
	u4Byte		r_ant_sel_cck_val = 0, r_ant_select_ofdm_val = 0, r_ofdm_tx_en_val = 0;

	ulAntennaTx = pHalData->AntennaTxPath;
	ulAntennaRx = pHalData->AntennaRxPath;
	
	p_ofdm_tx = (R_ANTENNA_SELECT_OFDM *)&r_ant_select_ofdm_val;
	p_cck_txrx = (R_ANTENNA_SELECT_CCK *)&r_ant_select_cck_val;

	p_ofdm_tx->r_ant_ht1			= 0x1;
	p_ofdm_tx->r_ant_ht2			= 0x2;/*Second TX RF path is A*/
	p_ofdm_tx->r_ant_non_ht			= 0x3;/*/ 0x1+0x2=0x3 */

	switch (ulAntennaTx) {
	case ANTENNA_A:
			p_ofdm_tx->r_tx_antenna		= 0x1;
			r_ofdm_tx_en_val		= 0x1;
			p_ofdm_tx->r_ant_l		= 0x1;
			p_ofdm_tx->r_ant_ht_s1		= 0x1;
			p_ofdm_tx->r_ant_non_ht_s1	= 0x1;
			p_cck_txrx->r_ccktx_enable	= 0x8;
			chgTx = 1;
			/*/ From SD3 Willis suggestion !!! Set RF A=TX and B as standby*/
			/*/if (IS_HARDWARE_TYPE_8192S(pAdapter))*/
			{
				PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 2);
				PHY_SetBBReg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 1);
				r_ofdm_tx_en_val			= 0x3;
				/*/ Power save*/
				/*/cosa r_ant_select_ofdm_val = 0x11111111;*/
				/*/ We need to close RFB by SW control*/
			if (pHalData->rf_type == RF_2T2R) {
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 1);
				PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT1, 1);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT17, 0);
			}
			}
			pMptCtx->MptRfPath = ODM_RF_PATH_A;
			break;
	case ANTENNA_B:
			p_ofdm_tx->r_tx_antenna		= 0x2;
			r_ofdm_tx_en_val		= 0x2;
			p_ofdm_tx->r_ant_l		= 0x2;
			p_ofdm_tx->r_ant_ht_s1		= 0x2;
			p_ofdm_tx->r_ant_non_ht_s1	= 0x2;
			p_cck_txrx->r_ccktx_enable	= 0x4;
			chgTx = 1;
			/*/ From SD3 Willis suggestion !!! Set RF A as standby*/
			/*/if (IS_HARDWARE_TYPE_8192S(pAdapter))*/
			{
				PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 1);
				PHY_SetBBReg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 2);

				/*/ 2008/10/31 MH From SD3 Willi's suggestion. We must read RF 1T table.*/
				/*/ 2009/01/08 MH From Sd3 Willis. We need to close RFA by SW control*/
			if (pHalData->rf_type == RF_2T2R || pHalData->rf_type == RF_1T2R) {
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 1);
				PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, BIT10, 0);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0);
				/*/PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0);*/
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT1, 0);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT17, 1);
			}
			}
			pMptCtx->MptRfPath = ODM_RF_PATH_B;		
			break;
	case ANTENNA_AB:/*/ For 8192S*/
			p_ofdm_tx->r_tx_antenna		= 0x3;
			r_ofdm_tx_en_val		= 0x3;
			p_ofdm_tx->r_ant_l		= 0x3;
			p_ofdm_tx->r_ant_ht_s1		= 0x3;
			p_ofdm_tx->r_ant_non_ht_s1	= 0x3;
			p_cck_txrx->r_ccktx_enable	= 0xC;
			chgTx = 1;
			/*/ From SD3Willis suggestion !!! Set RF B as standby*/
			/*/if (IS_HARDWARE_TYPE_8192S(pAdapter))*/
			{
			PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 2);
			PHY_SetBBReg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 2);
			/* Disable Power save*/			
			/*cosa r_ant_select_ofdm_val = 0x3321333;*/
			/* 2009/01/08 MH From Sd3 Willis. We need to enable RFA/B by SW control*/
			if (pHalData->rf_type == RF_2T2R) {
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0);

				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0);
				/*/PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0);*/
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT1, 1);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT17, 1);
			}
			}			
			pMptCtx->MptRfPath = ODM_RF_PATH_AB;
			break;
	default:
				break;
	}

	
	
/*// r_rx_antenna_ofdm, bit0=A, bit1=B, bit2=C, bit3=D
// r_cckrx_enable : CCK default, 0=A, 1=B, 2=C, 3=D
// r_cckrx_enable_2 : CCK option, 0=A, 1=B, 2=C, 3=D	*/
	switch (ulAntennaRx) {
	case ANTENNA_A:
		r_rx_antenna_ofdm		= 0x1;	/* A*/
		p_cck_txrx->r_cckrx_enable	= 0x0;	/* default: A*/
		p_cck_txrx->r_cckrx_enable_2	= 0x0;	/* option: A*/
		chgRx = 1;
		break;
	case ANTENNA_B:
		r_rx_antenna_ofdm			= 0x2;	/*/ B*/
		p_cck_txrx->r_cckrx_enable	= 0x1;	/*/ default: B*/
		p_cck_txrx->r_cckrx_enable_2	= 0x1;	/*/ option: B*/
		chgRx = 1;
		break;
	case ANTENNA_AB:/*/ For 8192S and 8192E/U...*/
		r_rx_antenna_ofdm		= 0x3;/*/ AB*/
		p_cck_txrx->r_cckrx_enable	= 0x0;/*/ default:A*/
		p_cck_txrx->r_cckrx_enable_2	= 0x1;/*/ option:B*/
		chgRx = 1;
		break;
	default:
		break;
	}


	if (chgTx && chgRx) {
		switch (pHalData->rf_chip) {
		case RF_8225:
		case RF_8256:
		case RF_6052:
				/*/r_ant_sel_cck_val = r_ant_select_cck_val;*/
				PHY_SetBBReg(pAdapter, rFPGA1_TxInfo, 0x7fffffff, r_ant_select_ofdm_val);		/*/OFDM Tx*/
				PHY_SetBBReg(pAdapter, rFPGA0_TxInfo, 0x0000000f, r_ofdm_tx_en_val);		/*/OFDM Tx*/
				PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable, 0x0000000f, r_rx_antenna_ofdm);	/*/OFDM Rx*/
				PHY_SetBBReg(pAdapter, rOFDM1_TRxPathEnable, 0x0000000f, r_rx_antenna_ofdm);	/*/OFDM Rx*/
				if (IS_HARDWARE_TYPE_8192E(pAdapter)) {
					PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable, 0x000000F0, r_rx_antenna_ofdm);	/*/OFDM Rx*/
					PHY_SetBBReg(pAdapter, rOFDM1_TRxPathEnable, 0x000000F0, r_rx_antenna_ofdm);	/*/OFDM Rx*/
				}
				PHY_SetBBReg(pAdapter, rCCK0_AFESetting, bMaskByte3, r_ant_select_cck_val);/*/r_ant_sel_cck_val); /CCK TxRx*/
				break;

		default:
				DBG_871X("Unsupported RFChipID for switching antenna.\n");
				break;
		}
	}
}	/* MPT_ProSetRFPath */


void hal_mpt_SetAntenna(PADAPTER	pAdapter)

{
	DBG_871X("Do %s\n", __func__);
#ifdef	CONFIG_RTL8814A
	if (IS_HARDWARE_TYPE_8814A(pAdapter)) {
		mpt_SetRFPath_8814A(pAdapter);
		return;
	}
#endif
#if	defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
	if (IS_HARDWARE_TYPE_JAGUAR(pAdapter)) {
		mpt_SetRFPath_8812A(pAdapter);
		return;
	}
#endif
#ifdef	CONFIG_RTL8723B
	if (IS_HARDWARE_TYPE_8723B(pAdapter)) {
		mpt_SetRFPath_8723B(pAdapter);
		return;
	}	
#endif	
#ifdef	CONFIG_RTL8703B
	if (IS_HARDWARE_TYPE_8703B(pAdapter)) {
		mpt_SetRFPath_8703B(pAdapter);
		return;
	}	
#endif	

/*	else if (IS_HARDWARE_TYPE_8821B(pAdapter))
		mpt_SetRFPath_8821B(pAdapter);
	Prepare for 8822B
	else if (IS_HARDWARE_TYPE_8822B(Context))
		mpt_SetRFPath_8822B(Context);
*/	
	mpt_SetRFPath_819X(pAdapter);
	DBG_871X("mpt_SetRFPath_819X Do %s\n", __func__);

}


s32 hal_mpt_SetThermalMeter(PADAPTER pAdapter, u8 target_ther)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);

	if (!netif_running(pAdapter->pnetdev)) {
		RT_TRACE(_module_mp_, _drv_warning_, ("SetThermalMeter! Fail: interface not opened!\n"));
		return _FAIL;
	}


	if (check_fwstate(&pAdapter->mlmepriv, WIFI_MP_STATE) == _FALSE) {
		RT_TRACE(_module_mp_, _drv_warning_, ("SetThermalMeter: Fail! not in MP mode!\n"));
		return _FAIL;
	}


	target_ther &= 0xff;
	if (target_ther < 0x07)
		target_ther = 0x07;
	else if (target_ther > 0x1d)
		target_ther = 0x1d;

	pHalData->EEPROMThermalMeter = target_ther;

	return _SUCCESS;
}


void hal_mpt_TriggerRFThermalMeter(PADAPTER pAdapter)
{
	PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x42, BIT17 | BIT16, 0x03);

}


u8 hal_mpt_ReadRFThermalMeter(PADAPTER pAdapter)

{
	u32 ThermalValue = 0;

	ThermalValue = (u1Byte)PHY_QueryRFReg(pAdapter, ODM_RF_PATH_A, 0x42, 0xfc00);	/*0x42: RF Reg[15:10]*/
	return (u8)ThermalValue;

}


void hal_mpt_GetThermalMeter(PADAPTER pAdapter, u8 *value)
{
#if 0
	fw_cmd(pAdapter, IOCMD_GET_THERMAL_METER);
	rtw_msleep_os(1000);
	fw_cmd_data(pAdapter, value, 1);
	*value &= 0xFF;
#else
	hal_mpt_TriggerRFThermalMeter(pAdapter);
	rtw_msleep_os(1000);
	*value = hal_mpt_ReadRFThermalMeter(pAdapter);
#endif

}


void hal_mpt_SetSingleCarrierTx(PADAPTER pAdapter, u8 bStart)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);
	
	pAdapter->mppriv.MptCtx.bSingleCarrier = bStart;
	
	if (bStart) {/*/ Start Single Carrier.*/
		RT_TRACE(_module_mp_, _drv_alert_, ("SetSingleCarrierTx: test start\n"));
		/*/ Start Single Carrier.*/
		/*/ 1. if OFDM block on?*/
		if (!PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
			PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 1); /*set OFDM block on*/

		/*/ 2. set CCK test mode off, set to CCK normal mode*/
		PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, 0);

		/*/ 3. turn on scramble setting*/
		PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, 1);

		/*/ 4. Turn On Continue Tx and turn off the other test modes.*/
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) ||  defined(CONFIG_RTL8814A)
		if (IS_HARDWARE_TYPE_JAGUAR(pAdapter) || IS_HARDWARE_TYPE_8814A(pAdapter) /*|| IS_HARDWARE_TYPE_8822B(pAdapter)*/)
			PHY_SetBBReg(pAdapter, rSingleTone_ContTx_Jaguar, BIT18|BIT17|BIT16, OFDM_SingleCarrier);
		else
#endif		
			PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_SingleCarrier);

	} else {
		/*/ Stop Single Carrier.*/
		/*/ Stop Single Carrier.*/
		/*/ Turn off all test modes.*/
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) ||  defined(CONFIG_RTL8814A)		
		if (IS_HARDWARE_TYPE_JAGUAR(pAdapter) || IS_HARDWARE_TYPE_8814A(pAdapter) /*|| IS_HARDWARE_TYPE_8822B(pAdapter)*/)
			PHY_SetBBReg(pAdapter, rSingleTone_ContTx_Jaguar, BIT18|BIT17|BIT16, OFDM_ALL_OFF);
		else
#endif
		
			PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_ALL_OFF);

		rtw_msleep_os(10);
		/*/BB Reset*/
	    PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
	    PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
	}
}


void hal_mpt_SetSingleToneTx(PADAPTER pAdapter, u8 bStart)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.MptCtx);
	u4Byte			ulAntennaTx = pHalData->AntennaTxPath;
	static u4Byte		regRF = 0, regBB0 = 0, regBB1 = 0, regBB2 = 0, regBB3 = 0;
	u8 rfPath;

	switch (ulAntennaTx) {
	case ANTENNA_B:
			rfPath = ODM_RF_PATH_B;
			break;
	case ANTENNA_C:
			rfPath = ODM_RF_PATH_C;
			break;
	case ANTENNA_D:
			rfPath = ODM_RF_PATH_D;
			break;
	case ANTENNA_A:
	default:	
			rfPath = ODM_RF_PATH_A;
			break;
	}

	pAdapter->mppriv.MptCtx.bSingleTone = bStart;
	if (bStart) {
		/*/ Start Single Tone.*/
		/*/ <20120326, Kordan> To amplify the power of tone for Xtal calibration. (asked by Edlu)*/
		if (IS_HARDWARE_TYPE_8188E(pAdapter)) {
			regRF = PHY_QueryRFReg(pAdapter, rfPath, LNA_Low_Gain_3, bRFRegOffsetMask);
			
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, LNA_Low_Gain_3, BIT1, 0x1); /*/ RF LO enabled*/	
			PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bCCKEn, 0x0);
			PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 0x0);
		} else if (IS_HARDWARE_TYPE_8192E(pAdapter)) { /*/ USB need to do RF LO disable first, PCIE isn't required to follow this order.*/
						/*/Set MAC REG 88C: Prevent SingleTone Fail*/
			PHY_SetMacReg(pAdapter, 0x88C, 0xF00000, 0xF);
			PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, LNA_Low_Gain_3, BIT1, 0x1); /*/ RF LO disabled*/
			PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, RF_AC, 0xF0000, 0x2); /*/ Tx mode*/
		} else if (IS_HARDWARE_TYPE_8723B(pAdapter)) {
			if (pMptCtx->MptRfPath == ODM_RF_PATH_A) {
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC, 0xF0000, 0x2); /*/ Tx mode*/
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x56, 0xF, 0x1); /*/ RF LO enabled*/
			} else { 
				/*/ S0/S1 both use PATH A to configure*/
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC, 0xF0000, 0x2); /*/ Tx mode*/
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x76, 0xF, 0x1); /*/ RF LO enabled*/
			}
		} else if (IS_HARDWARE_TYPE_8703B(pAdapter)) {
			if (pMptCtx->MptRfPath == ODM_RF_PATH_A) {
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC, 0xF0000, 0x2); /* Tx mode */
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x53, 0xF000, 0x1); /* RF LO enabled */
			}
		} else if (IS_HARDWARE_TYPE_8188F(pAdapter)) {
			/*Set BB REG 88C: Prevent SingleTone Fail*/
			PHY_SetBBReg(pAdapter, rFPGA0_AnalogParameter4, 0xF00000, 0xF);
			PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, LNA_Low_Gain_3, BIT1, 0x1);
			PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, RF_AC, 0xF0000, 0x2);

		} else if (IS_HARDWARE_TYPE_JAGUAR(pAdapter)) {
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
			u1Byte p = ODM_RF_PATH_A;
			
			regRF = PHY_QueryRFReg(pAdapter, ODM_RF_PATH_A, RF_AC_Jaguar, bRFRegOffsetMask);
			regBB0 = PHY_QueryBBReg(pAdapter, rA_RFE_Pinmux_Jaguar, bMaskDWord);
			regBB1 = PHY_QueryBBReg(pAdapter, rB_RFE_Pinmux_Jaguar, bMaskDWord);
			regBB2 = PHY_QueryBBReg(pAdapter, rA_RFE_Pinmux_Jaguar+4, bMaskDWord);
			regBB3 = PHY_QueryBBReg(pAdapter, rB_RFE_Pinmux_Jaguar+4, bMaskDWord);
			
			PHY_SetBBReg(pAdapter, rOFDMCCKEN_Jaguar, BIT29|BIT28, 0x0); /*/ Disable CCK and OFDM*/
			
			if (pMptCtx->MptRfPath == ODM_RF_PATH_AB) {
				for (p = ODM_RF_PATH_A; p <= ODM_RF_PATH_B; ++p) {					
					PHY_SetRFReg(pAdapter, p, RF_AC_Jaguar, 0xF0000, 0x2); /*/ Tx mode: RF0x00[19:16]=4'b0010 */
					PHY_SetRFReg(pAdapter, p, RF_AC_Jaguar, 0x1F, 0x0); /*/ Lowest RF gain index: RF_0x0[4:0] = 0*/
					PHY_SetRFReg(pAdapter, p, LNA_Low_Gain_3, BIT1, 0x1); /*/ RF LO enabled*/
				}
			} else {
				PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, RF_AC_Jaguar, 0xF0000, 0x2); /*/ Tx mode: RF0x00[19:16]=4'b0010 */
				PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, RF_AC_Jaguar, 0x1F, 0x0); /*/ Lowest RF gain index: RF_0x0[4:0] = 0*/
				PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, LNA_Low_Gain_3, BIT1, 0x1); /*/ RF LO enabled*/
			}			
			
			PHY_SetBBReg(pAdapter, rA_RFE_Pinmux_Jaguar, 0xFF00F0, 0x77007);  /*/ 0xCB0[[23:16, 7:4] = 0x77007*/
			PHY_SetBBReg(pAdapter, rB_RFE_Pinmux_Jaguar, 0xFF00F0, 0x77007);  /*/ 0xCB0[[23:16, 7:4] = 0x77007*/
			
			if (pHalData->ExternalPA_5G) {
				PHY_SetBBReg(pAdapter, rA_RFE_Pinmux_Jaguar+4, 0xFF00000, 0x12); /*/ 0xCB4[23:16] = 0x12*/
				PHY_SetBBReg(pAdapter, rB_RFE_Pinmux_Jaguar+4, 0xFF00000, 0x12); /*/ 0xEB4[23:16] = 0x12*/
			} else if (pHalData->ExternalPA_2G) {
				PHY_SetBBReg(pAdapter, rA_RFE_Pinmux_Jaguar+4, 0xFF00000, 0x11); /*/ 0xCB4[23:16] = 0x11*/
				PHY_SetBBReg(pAdapter, rB_RFE_Pinmux_Jaguar+4, 0xFF00000, 0x11); /*/ 0xEB4[23:16] = 0x11*/
			}
#endif
		}
#ifdef CONFIG_RTL8814A 
		else if (IS_HARDWARE_TYPE_8814A(pAdapter))
			mpt_SetSingleTone_8814A(pAdapter, TRUE, FALSE);
#endif
		else	/*/ Turn On SingleTone and turn off the other test modes.*/
			PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_SingleTone);			

		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	} else {/*/ Stop Single Ton e.*/

		if (IS_HARDWARE_TYPE_8188E(pAdapter)) {
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, LNA_Low_Gain_3, bRFRegOffsetMask, regRF);
			PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bCCKEn, 0x1);
			PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
		} else if (IS_HARDWARE_TYPE_8192E(pAdapter)) {
			PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, RF_AC, 0xF0000, 0x3);/*/ Tx mode*/
			PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, LNA_Low_Gain_3, BIT1, 0x0);/*/ RF LO disabled */
			/*/ RESTORE MAC REG 88C: Enable RF Functions*/
			PHY_SetMacReg(pAdapter, 0x88C, 0xF00000, 0x0);
		} else if (IS_HARDWARE_TYPE_8723B(pAdapter)) {
			if (pMptCtx->MptRfPath == ODM_RF_PATH_A) {
			
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC, 0xF0000, 0x3); /*/ Rx mode*/
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x56, 0xF, 0x0); /*/ RF LO disabled*/
			} else {
				/*/ S0/S1 both use PATH A to configure*/
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC, 0xF0000, 0x3); /*/ Rx mode*/
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x76, 0xF, 0x0); /*/ RF LO disabled*/
				}
		} else if (IS_HARDWARE_TYPE_8703B(pAdapter)) {
		
			if (pMptCtx->MptRfPath == ODM_RF_PATH_A) {
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC, 0xF0000, 0x3); /* Rx mode */
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x53, 0xF000, 0x0); /* RF LO disabled */
			}
		} else if (IS_HARDWARE_TYPE_8188F(pAdapter)) {
			PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, RF_AC, 0xF0000, 0x3); /*Tx mode*/
			PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, LNA_Low_Gain_3, BIT1, 0x0); /*RF LO disabled*/
			/*Set BB REG 88C: Prevent SingleTone Fail*/
			PHY_SetBBReg(pAdapter, rFPGA0_AnalogParameter4, 0xF00000, 0xc);	
		} else if (IS_HARDWARE_TYPE_JAGUAR(pAdapter)) {
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
			u1Byte p = ODM_RF_PATH_A;
			
			PHY_SetBBReg(pAdapter, rOFDMCCKEN_Jaguar, BIT29|BIT28, 0x3); /*/ Disable CCK and OFDM*/

			if (pMptCtx->MptRfPath == ODM_RF_PATH_AB) {
				for (p = ODM_RF_PATH_A; p <= ODM_RF_PATH_B; ++p) {					
					PHY_SetRFReg(pAdapter, p, RF_AC_Jaguar, bRFRegOffsetMask, regRF);
					PHY_SetRFReg(pAdapter, p, LNA_Low_Gain_3, BIT1, 0x0); /*/ RF LO disabled*/
				}
			} else {
				PHY_SetRFReg(pAdapter, p, RF_AC_Jaguar, bRFRegOffsetMask, regRF);
				PHY_SetRFReg(pAdapter, p, LNA_Low_Gain_3, BIT1, 0x0); /*/ RF LO disabled*/
			}
			
			PHY_SetBBReg(pAdapter, rA_RFE_Pinmux_Jaguar, bMaskDWord, regBB0); 
			PHY_SetBBReg(pAdapter, rB_RFE_Pinmux_Jaguar, bMaskDWord, regBB1); 
			PHY_SetBBReg(pAdapter, rA_RFE_Pinmux_Jaguar+4, bMaskDWord, regBB2);
			PHY_SetBBReg(pAdapter, rB_RFE_Pinmux_Jaguar+4, bMaskDWord, regBB3);
#endif
		}
#ifdef CONFIG_RTL8814A		
		else if (IS_HARDWARE_TYPE_8814A(pAdapter))
			mpt_SetSingleTone_8814A(pAdapter, FALSE, FALSE);

		 else/*/ Turn off all test modes.*/			
			PHY_SetBBReg(pAdapter, rSingleTone_ContTx_Jaguar, BIT18|BIT17|BIT16, OFDM_ALL_OFF);				   
#endif
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);

	}
}


void hal_mpt_SetCarrierSuppressionTx(PADAPTER pAdapter, u8 bStart)
{
	u8 Rate;
	pAdapter->mppriv.MptCtx.bCarrierSuppression = bStart;

	Rate = HwRateToMPTRate(pAdapter->mppriv.rateidx);
	if (bStart) {/* Start Carrier Suppression.*/
		RT_TRACE(_module_mp_, _drv_alert_, ("SetCarrierSuppressionTx: test start\n"));
		if (Rate <= MPT_RATE_11M) {
			/*/ 1. if CCK block on?*/
			if (!read_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn))
				write_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn, bEnable);/*set CCK block on*/

			/*/Turn Off All Test Mode*/
			if (IS_HARDWARE_TYPE_JAGUAR(pAdapter) || IS_HARDWARE_TYPE_8814A(pAdapter) /*|| IS_HARDWARE_TYPE_8822B(pAdapter)*/)
				PHY_SetBBReg(pAdapter, 0x914, BIT18|BIT17|BIT16, OFDM_ALL_OFF);/* rSingleTone_ContTx_Jaguar*/
			else
				PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_ALL_OFF);

			write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);    /*/transmit mode*/
			write_bbreg(pAdapter, rCCK0_System, bCCKScramble, 0x0);  /*/turn off scramble setting*/

			/*/Set CCK Tx Test Rate*/
			write_bbreg(pAdapter, rCCK0_System, bCCKTxRate, 0x0);    /*/Set FTxRate to 1Mbps*/
		}

		 /*Set for dynamic set Power index*/
		 write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		 write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	} else {/* Stop Carrier Suppression.*/	
		RT_TRACE(_module_mp_, _drv_alert_, ("SetCarrierSuppressionTx: test stop\n"));

		if (Rate <= MPT_RATE_11M) {
			write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);    /*normal mode*/
			write_bbreg(pAdapter, rCCK0_System, bCCKScramble, 0x1);  /*turn on scramble setting*/

			/*BB Reset*/
			write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
			write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
		}
		/*Stop for dynamic set Power index*/
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}
	DBG_871X("\n MPT_ProSetCarrierSupp() is finished.\n");
}

void hal_mpt_SetCCKContinuousTx(PADAPTER pAdapter, u8 bStart)
{
	u32 cckrate;

	if (bStart) {
		RT_TRACE(_module_mp_, _drv_alert_,
			 ("SetCCKContinuousTx: test start\n"));

		/*/ 1. if CCK block on?*/
		if (!read_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn))
			write_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn, bEnable);/*set CCK block on*/

		/*/Turn Off All Test Mode*/
		if (IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(pAdapter))
			PHY_SetBBReg(pAdapter, 0x914, BIT18|BIT17|BIT16, OFDM_ALL_OFF);/*rSingleTone_ContTx_Jaguar*/
		else
			PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_ALL_OFF);

		/*/Set CCK Tx Test Rate*/

		cckrate  = pAdapter->mppriv.rateidx;

		write_bbreg(pAdapter, rCCK0_System, bCCKTxRate, cckrate);
		write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);	/*/transmit mode*/
		write_bbreg(pAdapter, rCCK0_System, bCCKScramble, bEnable);	/*/turn on scramble setting*/

		if (!IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(pAdapter)) {
			PHY_SetBBReg(pAdapter, 0xa14, 0x300, 0x3);  /* rCCK0_RxHP 0xa15[1:0] = 11 force cck rxiq = 0*/
			PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, 0x10000, 0x1);		/*/ 0xc08[16] = 1 force ofdm rxiq = ofdm txiq*/
			PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter2, BIT14, 1);
			PHY_SetBBReg(pAdapter, 0x0B34, BIT14, 1);
		}

		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	} else {
		RT_TRACE(_module_mp_, _drv_info_,
			 ("SetCCKContinuousTx: test stop\n"));

		write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);	/*/normal mode*/
		write_bbreg(pAdapter, rCCK0_System, bCCKScramble, bEnable);	/*/turn on scramble setting*/

		if (!IS_HARDWARE_TYPE_JAGUAR(pAdapter)  && !IS_HARDWARE_TYPE_8814A(pAdapter) /* && !IS_HARDWARE_TYPE_8822B(pAdapter) */) {
			PHY_SetBBReg(pAdapter, 0xa14, 0x300, 0x0);/* rCCK0_RxHP 0xa15[1:0] = 2b00*/
			PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, 0x10000, 0x0);		/*/ 0xc08[16] = 0*/
			
			PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter2, BIT14, 0);
			PHY_SetBBReg(pAdapter, 0x0B34, BIT14, 0);
		}
		
		/*/BB Reset*/
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}

	pAdapter->mppriv.MptCtx.bCckContTx = bStart;
	pAdapter->mppriv.MptCtx.bOfdmContTx = _FALSE;
}

void hal_mpt_SetOFDMContinuousTx(PADAPTER pAdapter, u8 bStart)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);

	if (bStart) {
		RT_TRACE(_module_mp_, _drv_info_, ("SetOFDMContinuousTx: test start\n"));/*/ 1. if OFDM block on?*/
		if (!PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
			PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 1);/*/set OFDM block on*/

		/*/ 2. set CCK test mode off, set to CCK normal mode*/
		PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, 0);

		/*/ 3. turn on scramble setting*/
		PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, 1);

		if (!IS_HARDWARE_TYPE_JAGUAR(pAdapter) && !IS_HARDWARE_TYPE_8814A(pAdapter) /*&& !IS_HARDWARE_TYPE_8822B(pAdapter)*/) {
			PHY_SetBBReg(pAdapter, 0xa14, 0x300, 0x3);			/* rCCK0_RxHP 0xa15[1:0] = 2b'11*/
			PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, 0x10000, 0x1);		/* 0xc08[16] = 1*/
		}

		/*/ 4. Turn On Continue Tx and turn off the other test modes.*/
		if (IS_HARDWARE_TYPE_JAGUAR(pAdapter) || IS_HARDWARE_TYPE_8814A(pAdapter) /*|| IS_HARDWARE_TYPE_8822B(pAdapter)*/)
			PHY_SetBBReg(pAdapter, 0x914, BIT18|BIT17|BIT16, OFDM_ContinuousTx);/*rSingleTone_ContTx_Jaguar*/
		else
			PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_ContinuousTx);

		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	} else {
		RT_TRACE(_module_mp_, _drv_info_, ("SetOFDMContinuousTx: test stop\n"));
		if (IS_HARDWARE_TYPE_JAGUAR(pAdapter) || IS_HARDWARE_TYPE_8814A(pAdapter) /*|| IS_HARDWARE_TYPE_8822B(pAdapter)*/)
			PHY_SetBBReg(pAdapter, 0x914, BIT18|BIT17|BIT16, OFDM_ALL_OFF);
		else
			PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_ALL_OFF);
		/*/Delay 10 ms*/
		rtw_msleep_os(10);
		
		if (!IS_HARDWARE_TYPE_JAGUAR(pAdapter) && !IS_HARDWARE_TYPE_8814A(pAdapter) /*&&! IS_HARDWARE_TYPE_8822B(pAdapter)*/) {
			PHY_SetBBReg(pAdapter, 0xa14, 0x300, 0x0);/*/ 0xa15[1:0] = 0*/
			PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, 0x10000, 0x0);/*/ 0xc08[16] = 0*/
		}
		
		/*/BB Reset*/
		PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
		PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}

	pAdapter->mppriv.MptCtx.bCckContTx = _FALSE;
	pAdapter->mppriv.MptCtx.bOfdmContTx = bStart;
}

void hal_mpt_SetContinuousTx(PADAPTER pAdapter, u8 bStart)
{
	u8 Rate;
	RT_TRACE(_module_mp_, _drv_info_,
		 ("SetContinuousTx: rate:%d\n", pAdapter->mppriv.rateidx));

	Rate = HwRateToMPTRate(pAdapter->mppriv.rateidx);
	pAdapter->mppriv.MptCtx.bStartContTx = bStart;

	if (Rate <= MPT_RATE_11M)
		hal_mpt_SetCCKContinuousTx(pAdapter, bStart);
	else if (Rate >= MPT_RATE_6M) 
		hal_mpt_SetOFDMContinuousTx(pAdapter, bStart);
}

#if	defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8821B) || defined(CONFIG_RTL8822B)
/* for HW TX mode */
static	VOID mpt_StopCckContTx(
	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);
	u1Byte			u1bReg;

	pMptCtx->bCckContTx = FALSE;
	pMptCtx->bOfdmContTx = FALSE;

	PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);	/*normal mode*/
	PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, 0x1);	/*turn on scramble setting*/

	if (!IS_HARDWARE_TYPE_JAGUAR(pAdapter) && !IS_HARDWARE_TYPE_JAGUAR2(pAdapter)) {
		PHY_SetBBReg(pAdapter, 0xa14, 0x300, 0x0);			/* 0xa15[1:0] = 2b00*/
		PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, 0x10000, 0x0);		/* 0xc08[16] = 0*/
		
		PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter2, BIT14, 0);
		PHY_SetBBReg(pAdapter, 0x0B34, BIT14, 0);
	}

	/*BB Reset*/
	PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
	PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

	PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
	PHY_SetBBReg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);

}	/* mpt_StopCckContTx */


static	VOID mpt_StopOfdmContTx(
	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);
	u1Byte			u1bReg;
	u4Byte			data;

	pMptCtx->bCckContTx = FALSE;
	pMptCtx->bOfdmContTx = FALSE;

	if (IS_HARDWARE_TYPE_JAGUAR(pAdapter) || IS_HARDWARE_TYPE_JAGUAR2(pAdapter))
		PHY_SetBBReg(pAdapter, 0x914, BIT18|BIT17|BIT16, OFDM_ALL_OFF);
	else
		PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_ALL_OFF);

	rtw_mdelay_os(10);

	if (!IS_HARDWARE_TYPE_JAGUAR(pAdapter) && !IS_HARDWARE_TYPE_JAGUAR2(pAdapter)) {
		PHY_SetBBReg(pAdapter, 0xa14, 0x300, 0x0);			/* 0xa15[1:0] = 0*/
		PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, 0x10000, 0x0);		/* 0xc08[16] = 0*/
	}

	/*BB Reset*/
	PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
	PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

	PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
	PHY_SetBBReg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
}	/* mpt_StopOfdmContTx */


static	VOID mpt_StartCckContTx(
	PADAPTER		pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);
	u4Byte			cckrate;

	/* 1. if CCK block on */
	if (!PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bCCKEn))
		PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bCCKEn, 1);/*set CCK block on*/

	/*Turn Off All Test Mode*/
	if (IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(pAdapter))
		PHY_SetBBReg(pAdapter, 0x914, BIT18|BIT17|BIT16, OFDM_ALL_OFF);
	else
		PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_ALL_OFF);

	cckrate  = pAdapter->mppriv.rateidx;

	PHY_SetBBReg(pAdapter, rCCK0_System, bCCKTxRate, cckrate);

	PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);	/*transmit mode*/
	PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, 0x1);	/*turn on scramble setting*/

	if (!IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(pAdapter)) {
		PHY_SetBBReg(pAdapter, 0xa14, 0x300, 0x3);			/* 0xa15[1:0] = 11 force cck rxiq = 0*/
		PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, 0x10000, 0x1);		/* 0xc08[16] = 1 force ofdm rxiq = ofdm txiq*/
		PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter2, BIT14, 1);
		PHY_SetBBReg(pAdapter, 0x0B34, BIT14, 1);
	}

	PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
	PHY_SetBBReg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	pMptCtx->bCckContTx = TRUE;
	pMptCtx->bOfdmContTx = FALSE;
	
}	/* mpt_StartCckContTx */


static	VOID mpt_StartOfdmContTx(
	PADAPTER		pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);

	/* 1. if OFDM block on?*/
	if (!PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
		PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 1);/*set OFDM block on*/

	/* 2. set CCK test mode off, set to CCK normal mode*/
	PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, 0);

	/* 3. turn on scramble setting*/
	PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, 1);

	if (!IS_HARDWARE_TYPE_JAGUAR(pAdapter) && !IS_HARDWARE_TYPE_JAGUAR2(pAdapter)) {
		PHY_SetBBReg(pAdapter, 0xa14, 0x300, 0x3);			/* 0xa15[1:0] = 2b'11*/
		PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, 0x10000, 0x1);		/* 0xc08[16] = 1*/
	}

	/* 4. Turn On Continue Tx and turn off the other test modes.*/
	if (IS_HARDWARE_TYPE_JAGUAR(pAdapter) || IS_HARDWARE_TYPE_JAGUAR2(pAdapter))
		PHY_SetBBReg(pAdapter, 0x914, BIT18|BIT17|BIT16, OFDM_ContinuousTx);
	else
		PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_ContinuousTx);
	
	PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
	PHY_SetBBReg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	pMptCtx->bCckContTx = FALSE;
	pMptCtx->bOfdmContTx = TRUE;
}	/* mpt_StartOfdmContTx */


void mpt_ProSetPMacTx(PADAPTER	Adapter)
{
	PMPT_CONTEXT	pMptCtx		=	&(Adapter->mppriv.MptCtx);
	RT_PMAC_TX_INFO	PMacTxInfo	=	pMptCtx->PMacTxInfo;
	u32			u4bTmp;

	DbgPrint("SGI %d bSPreamble %d bSTBC %d bLDPC %d NDP_sound %d\n", PMacTxInfo.bSGI, PMacTxInfo.bSPreamble, PMacTxInfo.bSTBC, PMacTxInfo.bLDPC, PMacTxInfo.NDP_sound);
	DbgPrint("TXSC %d BandWidth %d PacketPeriod %d PacketCount %d PacketLength %d PacketPattern %d\n", PMacTxInfo.TX_SC, PMacTxInfo.BandWidth, PMacTxInfo.PacketPeriod, PMacTxInfo.PacketCount, PMacTxInfo.PacketLength, PMacTxInfo.PacketPattern);
#if 0
	PRINT_DATA("LSIG ", PMacTxInfo.LSIG, 3);
	PRINT_DATA("HT_SIG", PMacTxInfo.HT_SIG, 6);
	PRINT_DATA("VHT_SIG_A", PMacTxInfo.VHT_SIG_A, 6);
	PRINT_DATA("VHT_SIG_B", PMacTxInfo.VHT_SIG_B, 4);
	DbgPrint("VHT_SIG_B_CRC %x\n", PMacTxInfo.VHT_SIG_B_CRC);
	PRINT_DATA("VHT_Delimiter", PMacTxInfo.VHT_Delimiter, 4);

	PRINT_DATA("Src Address", Adapter->mac_addr, 6);
	PRINT_DATA("Dest Address", PMacTxInfo.MacAddress, 6);
#endif

	if (PMacTxInfo.bEnPMacTx == FALSE) {
		if (PMacTxInfo.Mode == CONTINUOUS_TX) {
			PHY_SetBBReg(Adapter, 0xb04, 0xf, 2);			/*	TX Stop*/
			if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE))
				mpt_StopCckContTx(Adapter);
			else
				mpt_StopOfdmContTx(Adapter);
		} else if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE)) {
			u4bTmp = PHY_QueryBBReg(Adapter, 0xf50, bMaskLWord);
			PHY_SetBBReg(Adapter, 0xb1c, bMaskLWord, u4bTmp+50);
			PHY_SetBBReg(Adapter, 0xb04, 0xf, 2);		/*TX Stop*/
		} else
			PHY_SetBBReg(Adapter, 0xb04, 0xf, 2);		/*	TX Stop*/

		if (PMacTxInfo.Mode == OFDM_Single_Tone_TX) {
			/* Stop HW TX -> Stop Continuous TX -> Stop RF Setting*/
			if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE))
				mpt_StopCckContTx(Adapter);
			else
				mpt_StopOfdmContTx(Adapter);

			mpt_SetSingleTone_8814A(Adapter, FALSE, TRUE);
		}

		return;
	}

	if (PMacTxInfo.Mode == CONTINUOUS_TX) {
		PMacTxInfo.PacketCount = 1;

		if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE))
			mpt_StartCckContTx(Adapter);
		else
			mpt_StartOfdmContTx(Adapter);
	} else if (PMacTxInfo.Mode == OFDM_Single_Tone_TX) {
		/* Continuous TX -> HW TX -> RF Setting */
		PMacTxInfo.PacketCount = 1;

		if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE))
			mpt_StartCckContTx(Adapter);
		else
			mpt_StartOfdmContTx(Adapter);
	} else if (PMacTxInfo.Mode == PACKETS_TX) {
		if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE) && PMacTxInfo.PacketCount == 0)
			PMacTxInfo.PacketCount = 0xffff;
	}

	if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE)) {
		/* 0xb1c[0:15] TX packet count 0xb1C[31:16]	SFD*/
		u4bTmp = PMacTxInfo.PacketCount|(PMacTxInfo.SFD << 16);
		PHY_SetBBReg(Adapter, 0xb1c, bMaskDWord, u4bTmp);
		/* 0xb40 7:0 SIGNAL	15:8 SERVICE	31:16 LENGTH*/
		u4bTmp = PMacTxInfo.SignalField|(PMacTxInfo.ServiceField << 8)|(PMacTxInfo.LENGTH << 16);
		PHY_SetBBReg(Adapter, 0xb40, bMaskDWord, u4bTmp);
		u4bTmp = PMacTxInfo.CRC16[0] | (PMacTxInfo.CRC16[1] << 8);
		PHY_SetBBReg(Adapter, 0xb44, bMaskLWord, u4bTmp);

		if (PMacTxInfo.bSPreamble)
			PHY_SetBBReg(Adapter, 0xb0c, BIT27, 0);	
		else
			PHY_SetBBReg(Adapter, 0xb0c, BIT27, 1);	
	} else {
		PHY_SetBBReg(Adapter, 0xb18, 0xfffff, PMacTxInfo.PacketCount);

		u4bTmp = PMacTxInfo.LSIG[0]|((PMacTxInfo.LSIG[1]) << 8)|((PMacTxInfo.LSIG[2]) << 16)|((PMacTxInfo.PacketPattern) << 24);
		PHY_SetBBReg(Adapter, 0xb08, bMaskDWord, u4bTmp);	/*	Set 0xb08[23:0] = LSIG, 0xb08[31:24] =  Data init octet*/

		if (PMacTxInfo.PacketPattern == 0x12)
			u4bTmp = 0x3000000;
		else
			u4bTmp = 0;
	}

	if (IS_MPT_HT_RATE(PMacTxInfo.TX_RATE)) {
		u4bTmp |= PMacTxInfo.HT_SIG[0]|((PMacTxInfo.HT_SIG[1]) << 8)|((PMacTxInfo.HT_SIG[2]) << 16);
		PHY_SetBBReg(Adapter, 0xb0c, bMaskDWord, u4bTmp);
		u4bTmp = PMacTxInfo.HT_SIG[3]|((PMacTxInfo.HT_SIG[4]) << 8)|((PMacTxInfo.HT_SIG[5]) << 16);
		PHY_SetBBReg(Adapter, 0xb10, 0xffffff, u4bTmp);
	} else if (IS_MPT_VHT_RATE(PMacTxInfo.TX_RATE)) {
		u4bTmp |= PMacTxInfo.VHT_SIG_A[0]|((PMacTxInfo.VHT_SIG_A[1]) << 8)|((PMacTxInfo.VHT_SIG_A[2]) << 16);
		PHY_SetBBReg(Adapter, 0xb0c, bMaskDWord, u4bTmp);
		u4bTmp = PMacTxInfo.VHT_SIG_A[3]|((PMacTxInfo.VHT_SIG_A[4]) << 8)|((PMacTxInfo.VHT_SIG_A[5]) << 16);
		PHY_SetBBReg(Adapter, 0xb10, 0xffffff, u4bTmp);

		_rtw_memcpy(&u4bTmp, PMacTxInfo.VHT_SIG_B, 4);
		PHY_SetBBReg(Adapter, 0xb14, bMaskDWord, u4bTmp);
	}

	if (IS_MPT_VHT_RATE(PMacTxInfo.TX_RATE)) {
		u4bTmp = (PMacTxInfo.VHT_SIG_B_CRC << 24)|PMacTxInfo.PacketPeriod;	/* for TX interval */
		PHY_SetBBReg(Adapter, 0xb20, bMaskDWord, u4bTmp);

		_rtw_memcpy(&u4bTmp, PMacTxInfo.VHT_Delimiter, 4);
		PHY_SetBBReg(Adapter, 0xb24, bMaskDWord, u4bTmp);

		/* 0xb28 - 0xb34 24 byte Probe Request MAC Header*/
		/*& Duration & Frame control*/
		PHY_SetBBReg(Adapter, 0xb28, bMaskDWord, 0x00000040);

		/* Address1 [0:3]*/
		u4bTmp = PMacTxInfo.MacAddress[0]|(PMacTxInfo.MacAddress[1] << 8)|(PMacTxInfo.MacAddress[2] << 16)|(PMacTxInfo.MacAddress[3] << 24);
		PHY_SetBBReg(Adapter, 0xb2C, bMaskDWord, u4bTmp);

		/* Address3 [3:0]*/
		PHY_SetBBReg(Adapter, 0xb38, bMaskDWord, u4bTmp);

		/* Address2[0:1] & Address1 [5:4]*/
		u4bTmp = PMacTxInfo.MacAddress[4]|(PMacTxInfo.MacAddress[5] << 8)|(Adapter->mac_addr[0] << 16)|(Adapter->mac_addr[1] << 24);
		PHY_SetBBReg(Adapter, 0xb30, bMaskDWord, u4bTmp);

		/* Address2 [5:2]*/
		u4bTmp = Adapter->mac_addr[2]|(Adapter->mac_addr[3] << 8)|(Adapter->mac_addr[4] << 16)|(Adapter->mac_addr[5] << 24);
		PHY_SetBBReg(Adapter, 0xb34, bMaskDWord, u4bTmp);

		/* Sequence Control & Address3 [5:4]*/
		/*u4bTmp = PMacTxInfo.MacAddress[4]|(PMacTxInfo.MacAddress[5] << 8) ;*/
		/*PHY_SetBBReg(Adapter, 0xb38, bMaskDWord, u4bTmp);*/
	} else {
		PHY_SetBBReg(Adapter, 0xb20, bMaskDWord, PMacTxInfo.PacketPeriod);	/* for TX interval*/
		/* & Duration & Frame control */
		PHY_SetBBReg(Adapter, 0xb24, bMaskDWord, 0x00000040);

		/* 0xb24 - 0xb38 24 byte Probe Request MAC Header*/
		/* Address1 [0:3]*/
		u4bTmp = PMacTxInfo.MacAddress[0]|(PMacTxInfo.MacAddress[1] << 8)|(PMacTxInfo.MacAddress[2] << 16)|(PMacTxInfo.MacAddress[3] << 24);
		PHY_SetBBReg(Adapter, 0xb28, bMaskDWord, u4bTmp);

		/* Address3 [3:0]*/
		PHY_SetBBReg(Adapter, 0xb34, bMaskDWord, u4bTmp);

		/* Address2[0:1] & Address1 [5:4]*/
		u4bTmp = PMacTxInfo.MacAddress[4]|(PMacTxInfo.MacAddress[5] << 8)|(Adapter->mac_addr[0] << 16)|(Adapter->mac_addr[1] << 24);
		PHY_SetBBReg(Adapter, 0xb2c, bMaskDWord, u4bTmp);

		/* Address2 [5:2] */
		u4bTmp = Adapter->mac_addr[2]|(Adapter->mac_addr[3] << 8)|(Adapter->mac_addr[4] << 16)|(Adapter->mac_addr[5] << 24);
		PHY_SetBBReg(Adapter, 0xb30, bMaskDWord, u4bTmp);

		/* Sequence Control & Address3 [5:4]*/
		u4bTmp = PMacTxInfo.MacAddress[4] | (PMacTxInfo.MacAddress[5] << 8);
		PHY_SetBBReg(Adapter, 0xb38, bMaskDWord, u4bTmp);
	}

	PHY_SetBBReg(Adapter, 0xb48, bMaskByte3, PMacTxInfo.TX_RATE_HEX);

	/* 0xb4c 3:0 TXSC	5:4	BW	7:6 m_STBC	8 NDP_Sound*/
	u4bTmp = (PMacTxInfo.TX_SC)|((PMacTxInfo.BandWidth) << 4)|((PMacTxInfo.m_STBC - 1) << 6)|((PMacTxInfo.NDP_sound) << 8);
	PHY_SetBBReg(Adapter, 0xb4c, 0x1ff, u4bTmp);

	if (IS_HARDWARE_TYPE_8814A(Adapter) || IS_HARDWARE_TYPE_8822B(Adapter)) {
		u4Byte offset = 0xb44;

		if (IS_MPT_OFDM_RATE(PMacTxInfo.TX_RATE))
			PHY_SetBBReg(Adapter, offset, 0xc0000000, 0);
		else if (IS_MPT_HT_RATE(PMacTxInfo.TX_RATE))
			PHY_SetBBReg(Adapter, offset, 0xc0000000, 1);
		else if (IS_MPT_VHT_RATE(PMacTxInfo.TX_RATE))
			PHY_SetBBReg(Adapter, offset, 0xc0000000, 2);
	}

	PHY_SetBBReg(Adapter, 0xb00, BIT8, 1);		/*	Turn on PMAC*/
/*	//PHY_SetBBReg(Adapter, 0xb04, 0xf, 2);				//TX Stop*/
	if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE)) {
		PHY_SetBBReg(Adapter, 0xb04, 0xf, 8);		/*TX CCK ON*/	
		PHY_SetBBReg(Adapter, 0xA84, BIT31, 0);
	} else
		PHY_SetBBReg(Adapter, 0xb04, 0xf, 4);		/*	TX Ofdm ON	*/

	if (PMacTxInfo.Mode == OFDM_Single_Tone_TX)
		mpt_SetSingleTone_8814A(Adapter, TRUE, TRUE);

}
#endif

#endif /* CONFIG_MP_INCLUDE*/

