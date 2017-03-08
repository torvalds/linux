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

#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8822B_SUPPORT == 1)

BOOLEAN
GetMixModeTXAGCBBSWingOffset_8822b(
	PVOID				pDM_VOID,
	PWRTRACK_METHOD	Method,
	u1Byte				RFPath,
	u1Byte				TxPowerIndexOffest
	)
{
	PDM_ODM_T		pDM_Odm	=	(PDM_ODM_T)pDM_VOID;
	PODM_RF_CAL_T	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	u1Byte	BBSwingUpperBound = pRFCalibrateInfo->DefaultOfdmIndex + 10;
	u1Byte	BBSwingLowerBound = 0;

	s1Byte	TX_AGC_Index = 0;
	u1Byte	TX_BBSwing_Index = pRFCalibrateInfo->DefaultOfdmIndex;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				  ("Path_%d pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath]=%d, TxPowerIndexOffest=%d\n",
				   RFPath, pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath], TxPowerIndexOffest));

	if (TxPowerIndexOffest > 0XF)
		TxPowerIndexOffest = 0XF;

	if (pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath] >= 0 && pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath] <= TxPowerIndexOffest) {
		TX_AGC_Index = pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath];
		TX_BBSwing_Index = pRFCalibrateInfo->DefaultOfdmIndex;
	} else if (pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath] > TxPowerIndexOffest) {
		TX_AGC_Index = TxPowerIndexOffest;
		pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath] = pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath] - TxPowerIndexOffest;
		TX_BBSwing_Index = pRFCalibrateInfo->DefaultOfdmIndex + pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath];

		if (TX_BBSwing_Index > BBSwingUpperBound)
			TX_BBSwing_Index = BBSwingUpperBound;
	} else {
		TX_AGC_Index = 0;

		if (pRFCalibrateInfo->DefaultOfdmIndex > (pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath] * (-1)))
			TX_BBSwing_Index = pRFCalibrateInfo->DefaultOfdmIndex + pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath];
		else
			TX_BBSwing_Index = BBSwingLowerBound;

		if (TX_BBSwing_Index <  BBSwingLowerBound)
			TX_BBSwing_Index = BBSwingLowerBound;
	}

	pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath] = TX_AGC_Index;
	pRFCalibrateInfo->BbSwingIdxOfdm[RFPath] = TX_BBSwing_Index;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				  ("MixMode Offset Path_%d   pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath]=%d   pRFCalibrateInfo->BbSwingIdxOfdm[RFPath]=%d   TxPowerIndexOffest=%d\n",
				   RFPath, pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath], pRFCalibrateInfo->BbSwingIdxOfdm[RFPath] , TxPowerIndexOffest));

	return TRUE;
}


VOID
ODM_TxPwrTrackSetPwr8822B(
	PVOID				pDM_VOID,
	PWRTRACK_METHOD	Method,
	u1Byte				RFPath,
	u1Byte				ChannelMappedIndex
	)
{

#if 0
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER	Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PODM_RF_CAL_T	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
	u1Byte			Channel  = pHalData->CurrentChannel;
	u1Byte			BandWidth  = pHalData->CurrentChannelBW;
	u1Byte			TxPowerIndex = 0;
	u1Byte			TxRate = 0xFF;
	RT_STATUS		status = RT_STATUS_SUCCESS;

	PHALMAC_PWR_TRACKING_OPTION pPwr_tracking_opt = &(pRFCalibrateInfo->HALMAC_PWR_TRACKING_INFO);

	if (pDM_Odm->mp_mode == TRUE) {
		#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
			#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
				#if (MP_DRIVER == 1)
					PMPT_CONTEXT pMptCtx = &(Adapter->MptCtx);

					TxRate = MptToMgntRate(pMptCtx->MptRateIndex);
				#endif
			#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
				PMPT_CONTEXT pMptCtx = &(Adapter->mppriv.MptCtx);

				TxRate = MptToMgntRate(pMptCtx->MptRateIndex);
			#endif
		#endif
	} else {
		u2Byte	rate	 = *(pDM_Odm->pForcedDataRate);

		if (!rate) { /*auto rate*/
			#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
				TxRate = Adapter->HalFunc.GetHwRateFromMRateHandler(pDM_Odm->TxRate);
			#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
				if (pDM_Odm->number_linked_client != 0)
					TxRate = HwRateToMRate(pDM_Odm->TxRate);
			#endif
		} else { /*force rate*/
			TxRate = (u1Byte) rate;
		}
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("Call:%s TxRate=0x%X\n", __func__, TxRate));

	TxPowerIndex = PHY_GetTxPowerIndex(Adapter, (ODM_RF_RADIO_PATH_E) RFPath, TxRate, BandWidth, Channel);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
	   ("type=%d   TxPowerIndex=%d	 pRFCalibrateInfo->Absolute_OFDMSwingIdx=%d   pRFCalibrateInfo->DefaultOfdmIndex=%d   RFPath=%d\n", Method, TxPowerIndex, pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath], pRFCalibrateInfo->DefaultOfdmIndex, RFPath)); 

	pPwr_tracking_opt->type = Method;
	pPwr_tracking_opt->bbswing_index = pRFCalibrateInfo->DefaultOfdmIndex;
	pPwr_tracking_opt->pwr_tracking_para[RFPath].enable = 1;
	pPwr_tracking_opt->pwr_tracking_para[RFPath].tx_pwr_index = TxPowerIndex;
	pPwr_tracking_opt->pwr_tracking_para[RFPath].pwr_tracking_offset_value = pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath];
	pPwr_tracking_opt->pwr_tracking_para[RFPath].tssi_value = 0;


	if (RFPath == (MAX_PATH_NUM_8822B - 1)) {
		status = HAL_MAC_Send_PowerTracking_Info(&GET_HAL_MAC_INFO(Adapter), pPwr_tracking_opt);

		if (status == RT_STATUS_SUCCESS) {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Path A  0xC94=0x%X   0xC1C=0x%X\n",
				ODM_GetBBReg(pDM_Odm, 0xC94, BIT29 | BIT28 | BIT27 | BIT26 | BIT25),
				ODM_GetBBReg(pDM_Odm, 0xC1C, 0xFFE00000)
				));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Path B  0xE94=0x%X   0xE1C=0x%X\n",
				ODM_GetBBReg(pDM_Odm, 0xE94, BIT29 | BIT28 | BIT27 | BIT26 | BIT25),
				ODM_GetBBReg(pDM_Odm, 0xE1C, 0xFFE00000)
				));
		} else {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Power Tracking to FW Fail ret code = %d\n", status));
		}
	}

#endif

	PDM_ODM_T		pDM_Odm		= (PDM_ODM_T)pDM_VOID;
	PODM_RF_CAL_T	pRFCalibrateInfo	= &(pDM_Odm->RFCalibrateInfo);
	u1Byte			TxPowerIndexOffest = 0;
	u1Byte			TxPowerIndex = 0;

	
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u1Byte			Channel  = pHalData->CurrentChannel;
	u1Byte			BandWidth  = pHalData->CurrentChannelBW;
	u1Byte			TxRate = 0xFF;
	
	if (pDM_Odm->mp_mode == TRUE) {
	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			#if (MP_DRIVER == 1)
					PMPT_CONTEXT pMptCtx = &(Adapter->MptCtx);

					TxRate = MptToMgntRate(pMptCtx->MptRateIndex);
			#endif
		#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
				PMPT_CONTEXT pMptCtx = &(Adapter->mppriv.MptCtx);

				TxRate = MptToMgntRate(pMptCtx->MptRateIndex);
		#endif
	#endif
	} else {
		u2Byte	rate	 = *(pDM_Odm->pForcedDataRate);

		if (!rate) { /*auto rate*/
		#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
				TxRate = Adapter->HalFunc.GetHwRateFromMRateHandler(pDM_Odm->TxRate);
		#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
				if (pDM_Odm->number_linked_client != 0)
					TxRate = HwRateToMRate(pDM_Odm->TxRate);
		#endif
		} else { /*force rate*/
			TxRate = (u1Byte) rate;
		}
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("Call:%s TxRate=0x%X\n", __func__, TxRate));
	
#endif

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, 
		("pRF->DefaultOfdmIndex=%d   pRF->DefaultCckIndex=%d\n", pRFCalibrateInfo->DefaultOfdmIndex, pRFCalibrateInfo->DefaultCckIndex));
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, 
		("pRF->Absolute_OFDMSwingIdx=%d   pRF->Remnant_OFDMSwingIdx=%d   pRF->Absolute_CCKSwingIdx=%d   pRF->Remnant_CCKSwingIdx=%d   RFPath=%d\n",
		pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath], pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath], pRFCalibrateInfo->Absolute_CCKSwingIdx[RFPath], pRFCalibrateInfo->Remnant_CCKSwingIdx, RFPath));

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	TxPowerIndex = PHY_GetTxPowerIndex(Adapter, (ODM_RF_RADIO_PATH_E) RFPath, TxRate, BandWidth, Channel);
#else
	TxPowerIndex = config_phydm_read_txagc_8822b(pDM_Odm, RFPath, 0x04); /*0x04(TX_AGC_OFDM_6M)*/
#endif

	if (TxPowerIndex >= 63)
		TxPowerIndex = 63;
	
	TxPowerIndexOffest = 63 - TxPowerIndex;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, 
		("TxPowerIndex=%d TxPowerIndexOffest=%d RFPath=%d\n", TxPowerIndex, TxPowerIndexOffest, RFPath));


	if (Method == MIX_MODE) {
		switch (RFPath) {
		case ODM_RF_PATH_A:
			GetMixModeTXAGCBBSWingOffset_8822b(pDM_Odm, Method, RFPath, TxPowerIndexOffest);
			ODM_SetBBReg(pDM_Odm, 0xC94, (BIT29 | BIT28 | BIT27 | BIT26 | BIT25), pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath]);
			ODM_SetBBReg(pDM_Odm, rA_TxScale_Jaguar, 0xFFE00000, TxScalingTable_Jaguar[pRFCalibrateInfo->BbSwingIdxOfdm[RFPath]]);

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("TXAGC(0xC94)=0x%x BBSwing(0xc1c)=0x%x BBSwingIndex=%d RFPath=%d\n",
						ODM_GetBBReg(pDM_Odm, 0xC94, (BIT29 | BIT28 | BIT27 | BIT26 | BIT25)),
						ODM_GetBBReg(pDM_Odm, 0xc1c, 0xFFE00000),
						pRFCalibrateInfo->BbSwingIdxOfdm[RFPath], RFPath));
		break;
					
		case ODM_RF_PATH_B:
			GetMixModeTXAGCBBSWingOffset_8822b(pDM_Odm, Method, RFPath, TxPowerIndexOffest);
			ODM_SetBBReg(pDM_Odm, 0xE94, (BIT29 | BIT28 | BIT27 | BIT26 | BIT25), pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath]);
			ODM_SetBBReg(pDM_Odm, rB_TxScale_Jaguar, 0xFFE00000, TxScalingTable_Jaguar[pRFCalibrateInfo->BbSwingIdxOfdm[RFPath]]);
				
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("TXAGC(0xE94)=0x%x BBSwing(0xe1c)=0x%x BBSwingIndex=%d RFPath=%d\n",
						ODM_GetBBReg(pDM_Odm, 0xE94, (BIT29 | BIT28 | BIT27 | BIT26 | BIT25)),
						ODM_GetBBReg(pDM_Odm, 0xe1c, 0xFFE00000),
						pRFCalibrateInfo->BbSwingIdxOfdm[RFPath], RFPath));
		break;
		
		default:
			break;
		}
	}	
}


VOID
GetDeltaSwingTable_8822B(
	PVOID		pDM_VOID,
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	pu1Byte		*TemperatureUP_A,
	pu1Byte		*TemperatureDOWN_A,
	pu1Byte		*TemperatureUP_B,
	pu1Byte		*TemperatureDOWN_B,
	pu1Byte		*TemperatureUP_CCK_A,
	pu1Byte		*TemperatureDOWN_CCK_A,
	pu1Byte		*TemperatureUP_CCK_B,
	pu1Byte		*TemperatureDOWN_CCK_B
#else
	pu1Byte		*TemperatureUP_A,
	pu1Byte		*TemperatureDOWN_A,
	pu1Byte		*TemperatureUP_B,
	pu1Byte		*TemperatureDOWN_B
#endif	
	)
{
	PDM_ODM_T		pDM_Odm		= (PDM_ODM_T)pDM_VOID;
	PODM_RF_CAL_T	pRFCalibrateInfo	= &(pDM_Odm->RFCalibrateInfo);

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	u1Byte			channel			= *(pDM_Odm->pChannel);
#else
	PADAPTER		Adapter			= pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData		= GET_HAL_DATA(Adapter);
	u1Byte			channel			= pHalData->CurrentChannel;
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	*TemperatureUP_CCK_A   = pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_P;
	*TemperatureDOWN_CCK_A = pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_N;
	*TemperatureUP_CCK_B   = pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_P;
	*TemperatureDOWN_CCK_B = pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_N;
#endif
	
	*TemperatureUP_A   = pRFCalibrateInfo->DeltaSwingTableIdx_2GA_P;
	*TemperatureDOWN_A = pRFCalibrateInfo->DeltaSwingTableIdx_2GA_N;
	*TemperatureUP_B   = pRFCalibrateInfo->DeltaSwingTableIdx_2GB_P;
	*TemperatureDOWN_B = pRFCalibrateInfo->DeltaSwingTableIdx_2GB_N;
		
	if (36 <= channel && channel <= 64) {
		*TemperatureUP_A   = pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P[0];
		*TemperatureDOWN_A = pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N[0];
		*TemperatureUP_B   = pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P[0];
		*TemperatureDOWN_B = pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N[0];
	} else if (100 <= channel && channel <= 144)	{
		*TemperatureUP_A   = pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P[1];
		*TemperatureDOWN_A = pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N[1];
		*TemperatureUP_B   = pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P[1];
		*TemperatureDOWN_B = pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N[1];
	} else if (149 <= channel && channel <= 177)	{
		*TemperatureUP_A   = pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P[2];
		*TemperatureDOWN_A = pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N[2];
		*TemperatureUP_B   = pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P[2];
		*TemperatureDOWN_B = pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N[2];
	}
}


VOID	
phy_LCCalibrate_8822B(
	PDM_ODM_T	pDM_Odm
	)
{
	u4Byte LC_Cal = 0, cnt = 0;

	/*backup RF0x18*/
	LC_Cal = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask);
	
	/*Start LCK*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, LC_Cal | 0x08000);

	ODM_delay_ms(100);		

	for (cnt = 0; cnt < 100; cnt++) {
		if (ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x8000) != 0x1)
			break;	
		ODM_delay_ms(10);
	}

	/*Recover channel number*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, LC_Cal);
}



VOID
PHY_LCCalibrate_8822B(
	PVOID	pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	BOOLEAN	bStartContTx = FALSE, bSingleTone = FALSE, bCarrierSuppression = FALSE;
	u8Byte		StartTime;
	u8Byte		ProgressingTime;


#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	PADAPTER		pAdapter = pDM_Odm->Adapter;
	
#if (MP_DRIVER == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	
	PMPT_CONTEXT	pMptCtx = &(pAdapter->MptCtx);
#else
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);		
#endif	
	bStartContTx = pMptCtx->bStartContTx;
	bSingleTone = pMptCtx->bSingleTone;
	bCarrierSuppression = pMptCtx->bCarrierSuppression;
#endif
#endif

	if (bStartContTx || bSingleTone || bCarrierSuppression) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[LCK]continues TX ing !!! LCK return\n"));
		return;
	}

	StartTime = ODM_GetCurrentTime(pDM_Odm);
	phy_LCCalibrate_8822B(pDM_Odm);
	ProgressingTime = ODM_GetProgressingTime(pDM_Odm, StartTime);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("[LCK]LCK ProgressingTime = %lld\n", ProgressingTime));
}



void ConfigureTxpowerTrack_8822B(
	PTXPWRTRACK_CFG	pConfig
	)
{
	pConfig->SwingTableSize_CCK = TXSCALE_TABLE_SIZE;
	pConfig->SwingTableSize_OFDM = TXSCALE_TABLE_SIZE;
	pConfig->Threshold_IQK = IQK_THRESHOLD;
	pConfig->Threshold_DPK = DPK_THRESHOLD;	
	pConfig->AverageThermalNum = AVG_THERMAL_NUM_8822B;
	pConfig->RfPathCount = MAX_PATH_NUM_8822B;
	pConfig->ThermalRegAddr = RF_T_METER_8822B;
		
	pConfig->ODM_TxPwrTrackSetPwr = ODM_TxPwrTrackSetPwr8822B;
	pConfig->DoIQK = DoIQK_8822B;
	pConfig->PHY_LCCalibrate = PHY_LCCalibrate_8822B;
	
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	pConfig->GetDeltaAllSwingTable = GetDeltaSwingTable_8822B;
#else
	pConfig->GetDeltaSwingTable = GetDeltaSwingTable_8822B;
#endif
}


VOID PHY_SetRFPathSwitch_8822B(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN		bMain
	)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif

#if 0
	ODM_SetBBReg(pDM_Odm, 0xCB4, bMaskDWord, 0x00004577);
	ODM_SetBBReg(pDM_Odm, 0x1900, bMaskDWord, 0x00001000);
	ODM_SetBBReg(pDM_Odm, 0x4C, bMaskDWord, 0x01628202);

	if (bMain)
		ODM_SetBBReg(pDM_Odm, 0xCBC, bMaskDWord, 0x00000200);	/*WiFi */
	else
		ODM_SetBBReg(pDM_Odm, 0xCBC, bMaskDWord, 0x00000100);	/* BT*/
#else
	/*BY SY Request */
	ODM_SetBBReg(pDM_Odm, 0x4C, (BIT24 | BIT23), 0x2);
	ODM_SetBBReg(pDM_Odm, 0x974, 0xff, 0xff);
	
	/*ODM_SetBBReg(pDM_Odm, 0x1991, 0x3, 0x0);*/
	ODM_SetBBReg(pDM_Odm, 0x1990, (BIT9 | BIT8), 0x0);
	
	/*ODM_SetBBReg(pDM_Odm, 0xCBE, 0x8, 0x0);*/
	ODM_SetBBReg(pDM_Odm, 0xCBC, BIT19, 0x0);
	
	ODM_SetBBReg(pDM_Odm, 0xCB4, 0xff, 0x77);
	
	if (bMain) {
		/*ODM_SetBBReg(pDM_Odm, 0xCBD, 0x3, 0x2);		WiFi */
		ODM_SetBBReg(pDM_Odm, 0xCBC, (BIT9 | BIT8), 0x2);		/*WiFi */
	} else {
		/*ODM_SetBBReg(pDM_Odm, 0xCBD, 0x3, 0x1);	 BT*/
		ODM_SetBBReg(pDM_Odm, 0xCBC, (BIT9 | BIT8), 0x1);	 /*BT*/
	}

#endif
}

BOOLEAN 
phy_QueryRFPathSwitch_8822B(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	PDM_ODM_T	pDM_Odm
#else
	PADAPTER	pAdapter
#endif
	)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif

	if (ODM_GetBBReg(pDM_Odm, 0xCBC, (BIT9 | BIT8)) == 0x2)	/*WiFi */
		return TRUE;
	else
		return FALSE;
}


BOOLEAN PHY_QueryRFPathSwitch_8822B(	
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	PDM_ODM_T		pDM_Odm
#else
	PADAPTER	pAdapter
#endif
	)
{

#if DISABLE_BB_RF
	return TRUE;
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	return phy_QueryRFPathSwitch_8822B(pDM_Odm);
#else
	return phy_QueryRFPathSwitch_8822B(pAdapter);
#endif
}


#endif	/* (RTL8822B_SUPPORT == 0)*/
