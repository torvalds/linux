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

#include "../odm_precomp.h"



/*---------------------------Define Local Constant---------------------------*/
// 2010/04/25 MH Define the max tx power tracking tx agc power.
#define		ODM_TXPWRTRACK_MAX_IDX8812A		6

/*---------------------------Define Local Constant---------------------------*/


//3============================================================
//3 Tx Power Tracking
//3============================================================


void DoIQK_8812A(
	PDM_ODM_T	pDM_Odm,
	u1Byte 		DeltaThermalIndex,
	u1Byte		ThermalValue,	
	u1Byte 		Threshold
	)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	PADAPTER 		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
#endif

	ODM_ResetIQKResult(pDM_Odm);		

#if(DM_ODM_SUPPORT_TYPE  & ODM_WIN)
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)	
#if USE_WORKITEM
	PlatformAcquireMutex(&pHalData->mxChnlBwControl);
#else
	PlatformAcquireSpinLock(Adapter, RT_CHANNEL_AND_BANDWIDTH_SPINLOCK);
#endif
#elif((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
	PlatformAcquireMutex(&pHalData->mxChnlBwControl);
#endif
#endif			


	pDM_Odm->RFCalibrateInfo.ThermalValue_IQK= ThermalValue;
	PHY_IQCalibrate_8812A(Adapter, FALSE);

    
#if(DM_ODM_SUPPORT_TYPE  & ODM_WIN)
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)	
#if USE_WORKITEM
	PlatformReleaseMutex(&pHalData->mxChnlBwControl);
#else
	PlatformReleaseSpinLock(Adapter, RT_CHANNEL_AND_BANDWIDTH_SPINLOCK);
#endif
#elif((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
	PlatformReleaseMutex(&pHalData->mxChnlBwControl);
#endif
#endif
}

/*-----------------------------------------------------------------------------
 * Function:	odm_TxPwrTrackSetPwr88E()
 *
 * Overview:	88E change all channel tx power accordign to flag.
 *				OFDM & CCK are all different.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	04/23/2012	MHC		Create Version 0.  
 *
 *---------------------------------------------------------------------------*/
VOID
ODM_TxPwrTrackSetPwr8812A(
	PDM_ODM_T			pDM_Odm,
	PWRTRACK_METHOD 	Method,
	u1Byte 				RFPath,
	u1Byte 				ChannelMappedIndex
	)
{
	u4Byte 	finalBbSwingIdx[2];
	
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

	u1Byte			PwrTrackingLimit = 26; //+1.0dB
	u1Byte			TxRate = 0xFF;
	u1Byte			Final_OFDM_Swing_Index = 0; 
	u1Byte			Final_CCK_Swing_Index = 0; 
	u1Byte			i = 0;

#if (MP_DRIVER==1)
	if ( *(pDM_Odm->mp_mode) == 1)
	{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE ))
	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
		PMPT_CONTEXT		pMptCtx = &(Adapter->MptCtx);
	#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
		PMPT_CONTEXT		pMptCtx = &(Adapter->mppriv.MptCtx);
	#endif
		TxRate = MptToMgntRate(pMptCtx->MptRateIndex);
#endif
	}
	else
#endif
	{
		u2Byte	rate	 = *(pDM_Odm->pForcedDataRate);
	
		if(!rate) //auto rate
		{
			if(pDM_Odm->TxRate != 0xFF)
				#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
				TxRate = Adapter->HalFunc.GetHwRateFromMRateHandler(pDM_Odm->TxRate);
				#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
				TxRate = HwRateToMRate(pDM_Odm->TxRate);
				#endif
		}
		else //force rate
		{
			TxRate = (u1Byte)rate;
		}
	}

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("===>ODM_TxPwrTrackSetPwr8812A\n"));

	if(TxRate != 0xFF)   //20130429 Mimic Modify High Rate BBSwing Limit.
	{
		//2 CCK
		if((TxRate >= MGN_1M)&&(TxRate <= MGN_11M))
			PwrTrackingLimit = 32; //+4dB
		//2 OFDM
		else if((TxRate >= MGN_6M)&&(TxRate <= MGN_48M))
			PwrTrackingLimit = 30; //+3dB
		else if(TxRate == MGN_54M)
			PwrTrackingLimit = 28; //+2dB
		//2 HT
		else if((TxRate >= MGN_MCS0)&&(TxRate <= MGN_MCS2)) //QPSK/BPSK
			PwrTrackingLimit = 34; //+5dB
		else if((TxRate >= MGN_MCS3)&&(TxRate <= MGN_MCS4)) //16QAM
			PwrTrackingLimit = 30; //+3dB
		else if((TxRate >= MGN_MCS5)&&(TxRate <= MGN_MCS7)) //64QAM
			PwrTrackingLimit = 28; //+2dB

		else if((TxRate >= MGN_MCS8)&&(TxRate <= MGN_MCS10)) //QPSK/BPSK
			PwrTrackingLimit = 34; //+5dB
		else if((TxRate >= MGN_MCS11)&&(TxRate <= MGN_MCS12)) //16QAM
			PwrTrackingLimit = 30; //+3dB
		else if((TxRate >= MGN_MCS13)&&(TxRate <= MGN_MCS15)) //64QAM
			PwrTrackingLimit = 28; //+2dB
			
		//2 VHT
		else if((TxRate >= MGN_VHT1SS_MCS0)&&(TxRate <= MGN_VHT1SS_MCS2)) //QPSK/BPSK
			PwrTrackingLimit = 34; //+5dB
		else if((TxRate >= MGN_VHT1SS_MCS3)&&(TxRate <= MGN_VHT1SS_MCS4)) //16QAM
			PwrTrackingLimit = 30; //+3dB
		else if((TxRate >= MGN_VHT1SS_MCS5)&&(TxRate <= MGN_VHT1SS_MCS6)) //64QAM
			PwrTrackingLimit = 28; //+2dB
		else if(TxRate == MGN_VHT1SS_MCS7) //64QAM
			PwrTrackingLimit = 26; //+1dB
		else if(TxRate == MGN_VHT1SS_MCS8) //256QAM
			PwrTrackingLimit = 24; //+0dB
		else if(TxRate == MGN_VHT1SS_MCS9) //256QAM
			PwrTrackingLimit = 22; //-1dB
			
		else if((TxRate >= MGN_VHT2SS_MCS0)&&(TxRate <= MGN_VHT2SS_MCS2)) //QPSK/BPSK
			PwrTrackingLimit = 34; //+5dB
		else if((TxRate >= MGN_VHT2SS_MCS3)&&(TxRate <= MGN_VHT2SS_MCS4)) //16QAM
			PwrTrackingLimit = 30; //+3dB
		else if((TxRate >= MGN_VHT2SS_MCS5)&&(TxRate <= MGN_VHT2SS_MCS6)) //64QAM
			PwrTrackingLimit = 28; //+2dB
		else if(TxRate == MGN_VHT2SS_MCS7) //64QAM
			PwrTrackingLimit = 26; //+1dB
		else if(TxRate == MGN_VHT2SS_MCS8) //256QAM
			PwrTrackingLimit = 24; //+0dB
		else if(TxRate == MGN_VHT2SS_MCS9) //256QAM
			PwrTrackingLimit = 22; //-1dB

		else			
			PwrTrackingLimit = 24;
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("TxRate=0x%x, PwrTrackingLimit=%d\n", TxRate, PwrTrackingLimit));

	
	if (Method == BBSWING)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("===>ODM_TxPwrTrackSetPwr8812A\n"));		

		if (RFPath == ODM_RF_PATH_A)
		{		
			finalBbSwingIdx[ODM_RF_PATH_A] = (pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_A] > PwrTrackingLimit) ? PwrTrackingLimit : pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_A];
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_A]=%d, pDM_Odm->RealBbSwingIdx[ODM_RF_PATH_A]=%d\n",
				pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_A], finalBbSwingIdx[ODM_RF_PATH_A]));
			
			ODM_SetBBReg(pDM_Odm, rA_TxScale_Jaguar, 0xFFE00000, TxScalingTable_Jaguar[finalBbSwingIdx[ODM_RF_PATH_A]]);
		}
		else
		{
			finalBbSwingIdx[ODM_RF_PATH_B] = (pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_B] > PwrTrackingLimit) ? PwrTrackingLimit : pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_B];
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_B]=%d, pDM_Odm->RealBbSwingIdx[ODM_RF_PATH_B]=%d\n",
				pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_B], finalBbSwingIdx[ODM_RF_PATH_B]));
			
			ODM_SetBBReg(pDM_Odm, rB_TxScale_Jaguar, 0xFFE00000, TxScalingTable_Jaguar[finalBbSwingIdx[ODM_RF_PATH_B]]);
		}
	}

	else if (Method == MIX_MODE)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("pDM_Odm->DefaultOfdmIndex=%d, pDM_Odm->Absolute_OFDMSwingIdx[RFPath]=%d, RF_Path = %d\n",
			pDM_Odm->DefaultOfdmIndex, pDM_Odm->Absolute_OFDMSwingIdx[RFPath],RFPath ));

		Final_CCK_Swing_Index = pDM_Odm->DefaultCckIndex + pDM_Odm->Absolute_OFDMSwingIdx[RFPath];
		Final_OFDM_Swing_Index = pDM_Odm->DefaultOfdmIndex + pDM_Odm->Absolute_OFDMSwingIdx[RFPath];

		if (RFPath == ODM_RF_PATH_A)  
		{
			if(Final_OFDM_Swing_Index > PwrTrackingLimit)     //BBSwing higher then Limit
			{
				pDM_Odm->Remnant_CCKSwingIdx= Final_CCK_Swing_Index - PwrTrackingLimit;            // CCK Follow the same compensate value as Path A
				pDM_Odm->Remnant_OFDMSwingIdx[RFPath] = Final_OFDM_Swing_Index - PwrTrackingLimit;            

				ODM_SetBBReg(pDM_Odm, rA_TxScale_Jaguar, 0xFFE00000, TxScalingTable_Jaguar[PwrTrackingLimit]);

				pDM_Odm->Modify_TxAGC_Flag_PathA= TRUE;

				PHY_SetTxPowerLevelByPath(Adapter, pHalData->CurrentChannel, ODM_RF_PATH_A);

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Path_A Over BBSwing Limit , PwrTrackingLimit = %d , Remnant TxAGC Value = %d \n", PwrTrackingLimit, pDM_Odm->Remnant_OFDMSwingIdx[RFPath]));
			}
			else if (Final_OFDM_Swing_Index < 0)
			{
				pDM_Odm->Remnant_CCKSwingIdx= Final_CCK_Swing_Index;            // CCK Follow the same compensate value as Path A
				pDM_Odm->Remnant_OFDMSwingIdx[RFPath] = Final_OFDM_Swing_Index;     

				ODM_SetBBReg(pDM_Odm, rA_TxScale_Jaguar, 0xFFE00000, TxScalingTable_Jaguar[0]);

				pDM_Odm->Modify_TxAGC_Flag_PathA= TRUE;

				PHY_SetTxPowerLevelByPath(Adapter, pHalData->CurrentChannel, ODM_RF_PATH_A);

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Path_A Lower then BBSwing lower bound  0 , Remnant TxAGC Value = %d \n", pDM_Odm->Remnant_OFDMSwingIdx[RFPath]));
			}
			else
			{
				ODM_SetBBReg(pDM_Odm, rA_TxScale_Jaguar, 0xFFE00000, TxScalingTable_Jaguar[Final_OFDM_Swing_Index]);

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Path_A Compensate with BBSwing , Final_OFDM_Swing_Index = %d \n", Final_OFDM_Swing_Index));

				if(pDM_Odm->Modify_TxAGC_Flag_PathA)  //If TxAGC has changed, reset TxAGC again
				{
					pDM_Odm->Remnant_CCKSwingIdx= 0;
					pDM_Odm->Remnant_OFDMSwingIdx[RFPath] = 0;     

					PHY_SetTxPowerLevelByPath(Adapter, pHalData->CurrentChannel, ODM_RF_PATH_A);

					pDM_Odm->Modify_TxAGC_Flag_PathA= FALSE;

					ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Path_A pDM_Odm->Modify_TxAGC_Flag = FALSE \n"));
				}
			}
		}

		if (RFPath == ODM_RF_PATH_B)  
		{
			if(Final_OFDM_Swing_Index > PwrTrackingLimit)     //BBSwing higher then Limit
			{
				pDM_Odm->Remnant_OFDMSwingIdx[RFPath] = Final_OFDM_Swing_Index - PwrTrackingLimit;            

				ODM_SetBBReg(pDM_Odm, rB_TxScale_Jaguar, 0xFFE00000, TxScalingTable_Jaguar[PwrTrackingLimit]);

				pDM_Odm->Modify_TxAGC_Flag_PathB= TRUE;

				PHY_SetTxPowerLevelByPath(Adapter, pHalData->CurrentChannel, ODM_RF_PATH_B);

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Path_B Over BBSwing Limit , PwrTrackingLimit = %d , Remnant TxAGC Value = %d \n", PwrTrackingLimit, pDM_Odm->Remnant_OFDMSwingIdx[RFPath]));
			}
			else if (Final_OFDM_Swing_Index < 0)
			{
				pDM_Odm->Remnant_OFDMSwingIdx[RFPath] = Final_OFDM_Swing_Index;     

				ODM_SetBBReg(pDM_Odm, rB_TxScale_Jaguar, 0xFFE00000, TxScalingTable_Jaguar[0]);

				pDM_Odm->Modify_TxAGC_Flag_PathB = TRUE;

				PHY_SetTxPowerLevelByPath(Adapter, pHalData->CurrentChannel, ODM_RF_PATH_B);

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Path_B Lower then BBSwing lower bound  0 , Remnant TxAGC Value = %d \n", pDM_Odm->Remnant_OFDMSwingIdx[RFPath]));
			}
			else
			{
				ODM_SetBBReg(pDM_Odm, rB_TxScale_Jaguar, 0xFFE00000, TxScalingTable_Jaguar[Final_OFDM_Swing_Index]);

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Path_B Compensate with BBSwing , Final_OFDM_Swing_Index = %d \n", Final_OFDM_Swing_Index));

				if(pDM_Odm->Modify_TxAGC_Flag_PathB)  //If TxAGC has changed, reset TxAGC again
				{
					pDM_Odm->Remnant_OFDMSwingIdx[RFPath] = 0;     

					PHY_SetTxPowerLevelByPath(Adapter, pHalData->CurrentChannel, ODM_RF_PATH_B);

					pDM_Odm->Modify_TxAGC_Flag_PathB = FALSE;

					ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Path_B pDM_Odm->Modify_TxAGC_Flag = FALSE \n"));
				}
			}
		}
		
	}
	else
	{
		return;
	}
}

VOID
GetDeltaSwingTable_8812A(
	IN 	PDM_ODM_T			pDM_Odm,
	OUT pu1Byte 			*TemperatureUP_A,
	OUT pu1Byte 			*TemperatureDOWN_A,
	OUT pu1Byte 			*TemperatureUP_B,
	OUT pu1Byte 			*TemperatureDOWN_B	
	)
{
    PADAPTER        Adapter   		 = pDM_Odm->Adapter;
	PODM_RF_CAL_T  	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
	HAL_DATA_TYPE  	*pHalData  		 = GET_HAL_DATA(Adapter);
	u2Byte			rate			 = *(pDM_Odm->pForcedDataRate);
	u1Byte         	channel   		 = pHalData->CurrentChannel;

	if ( 1 <= channel && channel <= 14) {
		if (IS_CCK_RATE(rate)) {
	        *TemperatureUP_A   = pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_P;
	        *TemperatureDOWN_A = pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_N;
	        *TemperatureUP_B   = pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_P;
	        *TemperatureDOWN_B = pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_N;		
		} else {
	        *TemperatureUP_A   = pRFCalibrateInfo->DeltaSwingTableIdx_2GA_P;
	        *TemperatureDOWN_A = pRFCalibrateInfo->DeltaSwingTableIdx_2GA_N;
	        *TemperatureUP_B   = pRFCalibrateInfo->DeltaSwingTableIdx_2GB_P;
	        *TemperatureDOWN_B = pRFCalibrateInfo->DeltaSwingTableIdx_2GB_N;			
		}
 	} else if ( 36 <= channel && channel <= 64) {
        *TemperatureUP_A   = pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P[0];
        *TemperatureDOWN_A = pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N[0];
        *TemperatureUP_B   = pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P[0];
        *TemperatureDOWN_B = pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N[0];
    } else if ( 100 <= channel && channel <= 140) {
        *TemperatureUP_A   = pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P[1];
        *TemperatureDOWN_A = pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N[1];
        *TemperatureUP_B   = pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P[1];
        *TemperatureDOWN_B = pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N[1];
    } else if ( 149 <= channel && channel <= 173) {
        *TemperatureUP_A   = pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P[2]; 
        *TemperatureDOWN_A = pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N[2]; 
        *TemperatureUP_B   = pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P[2]; 
        *TemperatureDOWN_B = pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N[2]; 
    } else {
	    *TemperatureUP_A   = (pu1Byte)DeltaSwingTableIdx_2GA_P_8188E;
	    *TemperatureDOWN_A = (pu1Byte)DeltaSwingTableIdx_2GA_N_8188E;	
	    *TemperatureUP_B   = (pu1Byte)DeltaSwingTableIdx_2GA_P_8188E;
	    *TemperatureDOWN_B = (pu1Byte)DeltaSwingTableIdx_2GA_N_8188E;		
    }
	
	return;
}

void ConfigureTxpowerTrack_8812A(
	PTXPWRTRACK_CFG	pConfig
	)
{
	pConfig->SwingTableSize_CCK = TXSCALE_TABLE_SIZE;
	pConfig->SwingTableSize_OFDM = TXSCALE_TABLE_SIZE;
	pConfig->Threshold_IQK = IQK_THRESHOLD;
	pConfig->AverageThermalNum = AVG_THERMAL_NUM_8812A;
	pConfig->RfPathCount = MAX_PATH_NUM_8812A;
	pConfig->ThermalRegAddr = RF_T_METER_8812A;
		
	pConfig->ODM_TxPwrTrackSetPwr = ODM_TxPwrTrackSetPwr8812A;
	pConfig->DoIQK = DoIQK_8812A;
	pConfig->PHY_LCCalibrate = PHY_LCCalibrate_8812A;
	pConfig->GetDeltaSwingTable = GetDeltaSwingTable_8812A;
}


//
// 2011/07/26 MH Add an API for testing IQK fail case.
//
// MP Already declare in odm.c 
#if !(DM_ODM_SUPPORT_TYPE & ODM_WIN) 
BOOLEAN
ODM_CheckPowerStatus(
	IN	PADAPTER		Adapter)
{
/*
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T			pDM_Odm = &pHalData->DM_OutSrc;
	RT_RF_POWER_STATE 	rtState;
	PMGNT_INFO			pMgntInfo	= &(Adapter->MgntInfo);

	// 2011/07/27 MH We are not testing ready~~!! We may fail to get correct value when init sequence.
	if (pMgntInfo->init_adpt_in_progress == TRUE)
	{
		ODM_RT_TRACE(pDM_Odm,COMP_INIT, DBG_LOUD, ("ODM_CheckPowerStatus Return TRUE, due to initadapter"));
		return	TRUE;
	}
	
	//
	//	2011/07/19 MH We can not execute tx pwoer tracking/ LLC calibrate or IQK.
	//
	Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_RF_STATE, (pu1Byte)(&rtState));	
	if(Adapter->bDriverStopped || Adapter->bDriverIsGoingToPnpSetPowerSleep || rtState == eRfOff)
	{
		ODM_RT_TRACE(pDM_Odm,COMP_INIT, DBG_LOUD, ("ODM_CheckPowerStatus Return FALSE, due to %d/%d/%d\n", 
		Adapter->bDriverStopped, Adapter->bDriverIsGoingToPnpSetPowerSleep, rtState));
		return	FALSE;
	}
*/
	return	TRUE;
}
#endif

#define BW_20M 	0
#define	BW_40M  1
#define	BW_80M	2

void _IQK_RX_FillIQC_8812A(
	IN PDM_ODM_T			pDM_Odm,
	IN ODM_RF_RADIO_PATH_E 	Path,
	IN unsigned int			RX_X,
	IN unsigned int			RX_Y
	) 
{
	switch (Path) {
	case ODM_RF_PATH_A:
		{
			ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
			if (RX_X>>1 ==0x112 || RX_Y>>1 == 0x3ee){
				ODM_SetBBReg(pDM_Odm, 0xc10, 0x000003ff, 0x100);
				ODM_SetBBReg(pDM_Odm, 0xc10, 0x03ff0000, 0);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RX_X = %x;;RX_Y = %x ====>fill to IQC\n", RX_X>>1&0x000003ff, RX_Y>>1&0x000003ff));
			}
			else{
				ODM_SetBBReg(pDM_Odm, 0xc10, 0x000003ff, RX_X>>1);
				ODM_SetBBReg(pDM_Odm, 0xc10, 0x03ff0000, RX_Y>>1);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RX_X = %x;;RX_Y = %x ====>fill to IQC\n", RX_X>>1&0x000003ff, RX_Y>>1&0x000003ff));
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xc10 = %x ====>fill to IQC\n", ODM_Read4Byte(pDM_Odm, 0xc10)));
			}
		}
		break;
	case ODM_RF_PATH_B:
		{	
			ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C	
			if (RX_X>>1 ==0x112 || RX_Y>>1 == 0x3ee){
				ODM_SetBBReg(pDM_Odm, 0xe10, 0x000003ff, 0x100);
				ODM_SetBBReg(pDM_Odm, 0xe10, 0x03ff0000, 0);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RX_X = %x;;RX_Y = %x ====>fill to IQC\n", RX_X>>1&0x000003ff, RX_Y>>1&0x000003ff));
			}
			else{
				ODM_SetBBReg(pDM_Odm, 0xe10, 0x000003ff, RX_X>>1);
				ODM_SetBBReg(pDM_Odm, 0xe10, 0x03ff0000, RX_Y>>1);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RX_X = %x;;RX_Y = %x====>fill to IQC\n ", RX_X>>1&0x000003ff, RX_Y>>1&0x000003ff));
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xe10 = %x====>fill to IQC\n", ODM_Read4Byte(pDM_Odm, 0xe10)));
			}
		}
		break;		
	default:
		break;					
	};	
}

void _IQK_TX_FillIQC_8812A(
	IN PDM_ODM_T			pDM_Odm,
	IN ODM_RF_RADIO_PATH_E 	Path,
	IN unsigned int			TX_X,
	IN unsigned int			TX_Y
	) 
{
	switch (Path) {
	case ODM_RF_PATH_A:
		{
			ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
			ODM_SetBBReg(pDM_Odm, 0xc90, BIT(7), 0x1);
			ODM_SetBBReg(pDM_Odm, 0xcc4, BIT(18), 0x1);
			if (!pDM_Odm->DPK_Done)
				ODM_SetBBReg(pDM_Odm, 0xcc4, BIT(29), 0x1);
			ODM_SetBBReg(pDM_Odm, 0xcc8, BIT(29), 0x1);	
			ODM_SetBBReg(pDM_Odm, 0xccc, 0x000007ff, TX_Y);
			ODM_SetBBReg(pDM_Odm, 0xcd4, 0x000007ff, TX_X);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TX_X = %x;;TX_Y = %x =====> fill to IQC\n", TX_X&0x000007ff, TX_Y&0x000007ff));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xcd4 = %x;;0xccc = %x ====>fill to IQC\n", ODM_GetBBReg(pDM_Odm, 0xcd4, 0x000007ff), ODM_GetBBReg(pDM_Odm, 0xccc, 0x000007ff)));
		}
		break;
	case ODM_RF_PATH_B:
		{
			ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
			ODM_SetBBReg(pDM_Odm, 0xe90, BIT(7), 0x1);
			ODM_SetBBReg(pDM_Odm, 0xec4, BIT(18), 0x1);
			if (!pDM_Odm->DPK_Done)
				ODM_SetBBReg(pDM_Odm, 0xec4, BIT(29), 0x1);
			ODM_SetBBReg(pDM_Odm, 0xec8, BIT(29), 0x1);	
			ODM_SetBBReg(pDM_Odm, 0xecc, 0x000007ff, TX_Y);
			ODM_SetBBReg(pDM_Odm, 0xed4, 0x000007ff, TX_X);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TX_X = %x;;TX_Y = %x =====> fill to IQC\n", TX_X&0x000007ff, TX_Y&0x000007ff));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xed4 = %x;;0xecc = %x ====>fill to IQC\n", ODM_GetBBReg(pDM_Odm, 0xed4, 0x000007ff), ODM_GetBBReg(pDM_Odm, 0xecc, 0x000007ff)));
		}
		break;		
	default:
		break;					
	};	
}

void _IQK_BackupMacBB_8812A(
	IN PDM_ODM_T	pDM_Odm,
	IN pu4Byte		MACBB_backup,
	IN pu4Byte		Backup_MACBB_REG, 
	IN u4Byte		MACBB_NUM
	)
{
	u4Byte i;
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	 //save MACBB default value
	for (i = 0; i < MACBB_NUM; i++){
		MACBB_backup[i] = ODM_Read4Byte(pDM_Odm, Backup_MACBB_REG[i]);
	}
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("BackupMacBB Success!!!!\n"));
}

void _IQK_BackupRF_8812A(
	IN PDM_ODM_T	pDM_Odm,
	IN pu4Byte		RFA_backup,
	IN pu4Byte		RFB_backup, 
	IN pu4Byte		Backup_RF_REG, 
	IN u4Byte		RF_NUM
	)	
{

	u4Byte i;
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	//Save RF Parameters
    	for (i = 0; i < RF_NUM; i++){
        	RFA_backup[i] = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, Backup_RF_REG[i], bMaskDWord);
        	RFB_backup[i] = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, Backup_RF_REG[i], bMaskDWord);
    	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("BackupRF Success!!!!\n"));
}

void _IQK_BackupAFE_8812A(
	IN PDM_ODM_T		pDM_Odm,
	IN pu4Byte		AFE_backup,
	IN pu4Byte		Backup_AFE_REG, 
	IN u4Byte		AFE_NUM
	)
{
	u4Byte i;
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	//Save AFE Parameters 
    	for (i = 0; i < AFE_NUM; i++){
        	AFE_backup[i] = ODM_Read4Byte(pDM_Odm, Backup_AFE_REG[i]);	
    	}
    	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("BackupAFE Success!!!!\n"));
}

void _IQK_RestoreMacBB_8812A(
	IN PDM_ODM_T		pDM_Odm,
	IN pu4Byte		MACBB_backup,
	IN pu4Byte		Backup_MACBB_REG, 
	IN u4Byte		MACBB_NUM
	)	
{
	u4Byte i;
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	//Reload MacBB Parameters 
    	for (i = 0; i < MACBB_NUM; i++){
        	ODM_Write4Byte(pDM_Odm, Backup_MACBB_REG[i], MACBB_backup[i]);
    	}
    	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RestoreMacBB Success!!!!\n"));
}

void _IQK_RestoreRF_8812A(
	IN PDM_ODM_T			pDM_Odm,
	IN ODM_RF_RADIO_PATH_E 	Path,
	IN pu4Byte			Backup_RF_REG,
	IN pu4Byte 			RF_backup,
	IN u4Byte			RF_REG_NUM
	)
{	
	u4Byte i;

	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
    	for (i = 0; i < RF_REG_NUM; i++)
        	ODM_SetRFReg(pDM_Odm, Path, Backup_RF_REG[i], bRFRegOffsetMask, RF_backup[i]);

	ODM_SetRFReg(pDM_Odm, Path, 0xef, bRFRegOffsetMask, 0x0);

	switch(Path){
	case ODM_RF_PATH_A:
       {
       	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RestoreRF Path A Success!!!!\n"));
       }
		break;
	case ODM_RF_PATH_B:
       {
       	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RestoreRF Path B Success!!!!\n"));
       }
		break;
	default:
		break;
	}
}

void _IQK_RestoreAFE_8812A(
	IN PDM_ODM_T		pDM_Odm,
	IN pu4Byte		AFE_backup,
	IN pu4Byte		Backup_AFE_REG, 
	IN u4Byte		AFE_NUM
	)
{
	u4Byte i;
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	//Reload AFE Parameters 
    	for (i = 0; i < AFE_NUM; i++){
        	ODM_Write4Byte(pDM_Odm, Backup_AFE_REG[i], AFE_backup[i]);
    	}
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
	ODM_Write4Byte(pDM_Odm, 0xc80, 0x0);
	ODM_Write4Byte(pDM_Odm, 0xc84, 0x0);
	ODM_Write4Byte(pDM_Odm, 0xc88, 0x0);
	ODM_Write4Byte(pDM_Odm, 0xc8c, 0x3c000000);
	ODM_SetBBReg(pDM_Odm, 0xc90, BIT(7), 0x1);
	ODM_SetBBReg(pDM_Odm, 0xcc4, BIT(18), 0x1);
	if (!pDM_Odm->DPK_Done)
		ODM_SetBBReg(pDM_Odm, 0xcc4, BIT(29), 0x1);
	ODM_SetBBReg(pDM_Odm, 0xcc8, BIT(29), 0x1);	
	//ODM_Write4Byte(pDM_Odm, 0xcb8, 0x0);
	ODM_Write4Byte(pDM_Odm, 0xe80, 0x0);
	ODM_Write4Byte(pDM_Odm, 0xe84, 0x0);
	ODM_Write4Byte(pDM_Odm, 0xe88, 0x0);
	ODM_Write4Byte(pDM_Odm, 0xe8c, 0x3c000000);
	ODM_SetBBReg(pDM_Odm, 0xe90, BIT(7), 0x1);
	ODM_SetBBReg(pDM_Odm, 0xec4, BIT(18), 0x1);
	if (!pDM_Odm->DPK_Done)
		ODM_SetBBReg(pDM_Odm, 0xec4, BIT(29), 0x1);
	ODM_SetBBReg(pDM_Odm, 0xec8, BIT(29), 0x1);	
	//ODM_Write4Byte(pDM_Odm, 0xeb8, 0x0);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RestoreAFE Success!!!!\n"));
}

void _IQK_ConfigureMAC_8812A(
	IN PDM_ODM_T		pDM_Odm
	)
{
	// ========MAC register setting========
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C	
	ODM_Write1Byte(pDM_Odm, 0x522, 0x3f);
	ODM_SetBBReg(pDM_Odm, 0x550, BIT(11)|BIT(3), 0x0);
	ODM_Write1Byte(pDM_Odm, 0x808, 0x00);		//		RX ante off
	ODM_SetBBReg(pDM_Odm, 0x838, 0xf, 0xc);		//		CCA off
}

#define cal_num 10

void _IQK_Tx_8812A(
	IN PDM_ODM_T		pDM_Odm,
	IN u1Byte chnlIdx
	)
{
	u1Byte 		delay_count, cal = 0;
	u1Byte		cal0_retry, cal1_retry, TX0_Average = 0, TX1_Average = 0, RX0_Average = 0, RX1_Average = 0;
	int			TX_IQC_temp[10][4], TX_IQC[4]={};		//TX_IQC = [TX0_X, TX0_Y,TX1_X,TX1_Y]; for 3 times
	int			RX_IQC_temp[10][4], RX_IQC[4]={};		//RX_IQC = [RX0_X, RX0_Y,RX1_X,RX1_Y]; for 3 times
	BOOLEAN 	TX0_fail = TRUE, RX0_fail = TRUE, IQK0_ready = FALSE, TX0_finish = FALSE, RX0_finish = FALSE;
	BOOLEAN  	TX1_fail = TRUE, RX1_fail = TRUE, IQK1_ready = FALSE, TX1_finish = FALSE, RX1_finish = FALSE, VDF_enable = FALSE;
	int			i, ii, dx = 0, dy = 0;
	PODM_RF_CAL_T  pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("BandWidth = %d, ExtPA5G = %d, ExtPA2G = %d\n", *pDM_Odm->pBandWidth, pDM_Odm->ExtPA5G, pDM_Odm->ExtPA));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Interface = %d, RFE_Type = %d\n", pDM_Odm->SupportInterface, pDM_Odm->RFEType));
	if (*pDM_Odm->pBandWidth == 2){
		VDF_enable = TRUE;
	}
	VDF_enable = FALSE;

	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	// ========Path-A AFE all on========
	// Port 0 DAC/ADC on
	ODM_Write4Byte(pDM_Odm, 0xc60, 0x77777777);
	ODM_Write4Byte(pDM_Odm, 0xc64, 0x77777777);
	 
	// Port 1 DAC/ADC on
	ODM_Write4Byte(pDM_Odm, 0xe60, 0x77777777);
	ODM_Write4Byte(pDM_Odm, 0xe64, 0x77777777);

	ODM_Write4Byte(pDM_Odm, 0xc68, 0x19791979);
	ODM_Write4Byte(pDM_Odm, 0xe68, 0x19791979);
	ODM_SetBBReg(pDM_Odm, 0xc00, 0xf, 0x4);// 	hardware 3-wire off
	ODM_SetBBReg(pDM_Odm, 0xe00, 0xf, 0x4);// 	hardware 3-wire off

	// DAC/ADC sampling rate (160 MHz)
	ODM_SetBBReg(pDM_Odm, 0xc5c, BIT(26)|BIT(25)|BIT(24), 0x7);
	ODM_SetBBReg(pDM_Odm, 0xe5c, BIT(26)|BIT(25)|BIT(24), 0x7);
			
        //====== Path A TX IQK RF Setting ======
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xef, bRFRegOffsetMask, 0x80002);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x30, bRFRegOffsetMask, 0x20000);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x31, bRFRegOffsetMask, 0x3fffd);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x32, bRFRegOffsetMask, 0xfe83f);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x65, bRFRegOffsetMask, 0x931d5);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x8f, bRFRegOffsetMask, 0x8a001);
	//====== Path B TX IQK RF Setting ======
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0xef, bRFRegOffsetMask, 0x80002);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x30, bRFRegOffsetMask, 0x20000);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x31, bRFRegOffsetMask, 0x3fffd);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x32, bRFRegOffsetMask, 0xfe83f);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x65, bRFRegOffsetMask, 0x931d5);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x8f, bRFRegOffsetMask, 0x8a001);
	ODM_Write4Byte(pDM_Odm, 0x90c, 0x00008000);
	ODM_Write4Byte(pDM_Odm, 0xb00, 0x03000100);
	ODM_SetBBReg(pDM_Odm, 0xc94, BIT(0), 0x1);
	ODM_SetBBReg(pDM_Odm, 0xe94, BIT(0), 0x1);
	ODM_Write4Byte(pDM_Odm, 0x978, 0x29002000);// TX (X,Y)
	ODM_Write4Byte(pDM_Odm, 0x97c, 0xa9002000);// RX (X,Y)
	ODM_Write4Byte(pDM_Odm, 0x984, 0x00462910);// [0]:AGC_en, [15]:idac_K_Mask
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1

	if (pDM_Odm->ExtPA5G){
		if (pDM_Odm->SupportInterface == 1 && pDM_Odm->RFEType == 1){
			ODM_Write4Byte(pDM_Odm, 0xc88, 0x821403e3);
			ODM_Write4Byte(pDM_Odm, 0xe88, 0x821403e3);
		}
		else{
			ODM_Write4Byte(pDM_Odm, 0xc88, 0x821403f7);
			ODM_Write4Byte(pDM_Odm, 0xe88, 0x821403f7);
		}
	}
	else{
		ODM_Write4Byte(pDM_Odm, 0xc88, 0x821403f1);
		ODM_Write4Byte(pDM_Odm, 0xe88, 0x821403f1);
	}
	if (*pDM_Odm->pBandType){
		ODM_Write4Byte(pDM_Odm, 0xc8c, 0x68163e96);
		ODM_Write4Byte(pDM_Odm, 0xe8c, 0x68163e96);
	}
	else{
		ODM_Write4Byte(pDM_Odm, 0xc8c, 0x28163e96);
		ODM_Write4Byte(pDM_Odm, 0xe8c, 0x28163e96);
		if (pDM_Odm->RFEType == 3){	
			if (pDM_Odm->ExtPA)
				ODM_Write4Byte(pDM_Odm, 0xc88, 0x821403e3);
			else
				ODM_Write4Byte(pDM_Odm, 0xc88, 0x821403f7);
		}
	}

	if (VDF_enable){}
	else{
		ODM_Write4Byte(pDM_Odm, 0xc80, 0x18008c10);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
		ODM_Write4Byte(pDM_Odm, 0xc84, 0x38008c10);// RX_Tone_idx[9:0], RxK_Mask[29]
		ODM_Write4Byte(pDM_Odm, 0xce8, 0x00000000);
		ODM_Write4Byte(pDM_Odm, 0xe80, 0x18008c10);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
		ODM_Write4Byte(pDM_Odm, 0xe84, 0x38008c10);// RX_Tone_idx[9:0], RxK_Mask[29]
		ODM_Write4Byte(pDM_Odm, 0xee8, 0x00000000);

		cal0_retry = 0;
		cal1_retry = 0;
		while(1){
			// one shot
			ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00100000);// cb8[20] 將 SI/PI 使用權切給 iqk_dpk module
			ODM_Write4Byte(pDM_Odm, 0xeb8, 0x00100000);// cb8[20] 將 SI/PI 使用權切給 iqk_dpk module
			ODM_Write4Byte(pDM_Odm, 0x980, 0xfa000000);
			ODM_Write4Byte(pDM_Odm, 0x980, 0xf8000000);
			
			ODM_delay_ms(10); //Delay 25ms
			ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00000000);
			ODM_Write4Byte(pDM_Odm, 0xeb8, 0x00000000);
			delay_count = 0;
			while (1){
				if (!TX0_finish)
					IQK0_ready = (BOOLEAN) ODM_GetBBReg(pDM_Odm, 0xd00, BIT(10));
				if (!TX1_finish)
					IQK1_ready = (BOOLEAN) ODM_GetBBReg(pDM_Odm, 0xd40, BIT(10));
				if ((IQK0_ready && IQK1_ready) || (delay_count>20))
					break;
				else{
				ODM_delay_ms(1);
				delay_count++;
				}
			}
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TX delay_count = %d\n", delay_count));
			if (delay_count < 20){							// If 20ms No Result, then cal_retry++
				// ============TXIQK Check==============
				TX0_fail = (BOOLEAN) ODM_GetBBReg(pDM_Odm, 0xd00, BIT(12));
				TX1_fail = (BOOLEAN) ODM_GetBBReg(pDM_Odm, 0xd40, BIT(12));
				if (!(TX0_fail || TX0_finish)){
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x02000000);
					TX_IQC_temp[TX0_Average][0] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x04000000);
					TX_IQC_temp[TX0_Average][1] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TX_X0[%d] = %x ;; TX_Y0[%d] = %x\n", TX0_Average, (TX_IQC_temp[TX0_Average][0])>>21&0x000007ff, TX0_Average, (TX_IQC_temp[TX0_Average][1])>>21&0x000007ff));
					/*

					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x01000000);
					reg1 = ODM_GetBBReg(pDM_Odm, 0xd00, 0xffffffff);
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x02000000);
					reg2 = ODM_GetBBReg(pDM_Odm, 0xd00, 0x0000001f);
					Image_Power = (reg2<<32)+reg1;
					DbgPrint("Before PW = %d\n", Image_Power);
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x03000000);
					reg1 = ODM_GetBBReg(pDM_Odm, 0xd00, 0xffffffff);
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x04000000);
					reg2 = ODM_GetBBReg(pDM_Odm, 0xd00, 0x0000001f);
					Image_Power = (reg2<<32)+reg1;
					DbgPrint("After PW = %d\n", Image_Power);
					*/
					TX0_Average++;
			}
			else{
				cal0_retry++;
				if (cal0_retry == 10)
					break;
				}
			if (!(TX1_fail || TX1_finish)){
				ODM_Write4Byte(pDM_Odm, 0xeb8, 0x02000000);
				TX_IQC_temp[TX1_Average][2] = ODM_GetBBReg(pDM_Odm, 0xd40, 0x07ff0000)<<21;
				ODM_Write4Byte(pDM_Odm, 0xeb8, 0x04000000);
				TX_IQC_temp[TX1_Average][3] = ODM_GetBBReg(pDM_Odm, 0xd40, 0x07ff0000)<<21;
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TX_X1[%d] = %x ;; TX_Y1[%d] = %x\n", TX1_Average, (TX_IQC_temp[TX1_Average][2])>>21&0x000007ff, TX1_Average, (TX_IQC_temp[TX1_Average][3])>>21&0x000007ff));
				/*
				int			reg1 = 0, reg2 = 0, Image_Power = 0;
				ODM_Write4Byte(pDM_Odm, 0xeb8, 0x01000000);
				reg1 = ODM_GetBBReg(pDM_Odm, 0xd40, 0xffffffff);
				ODM_Write4Byte(pDM_Odm, 0xeb8, 0x02000000);
				reg2 = ODM_GetBBReg(pDM_Odm, 0xd40, 0x0000001f);
				Image_Power = (reg2<<32)+reg1;
				DbgPrint("Before PW = %d\n", Image_Power);
				ODM_Write4Byte(pDM_Odm, 0xeb8, 0x03000000);
				reg1 = ODM_GetBBReg(pDM_Odm, 0xd40, 0xffffffff);
				ODM_Write4Byte(pDM_Odm, 0xeb8, 0x04000000);
				reg2 = ODM_GetBBReg(pDM_Odm, 0xd40, 0x0000001f);
				Image_Power = (reg2<<32)+reg1;
				DbgPrint("After PW = %d\n", Image_Power);
				*/
				TX1_Average++;
				}
			else{
				cal1_retry++;
				if (cal1_retry == 10)
					break;
				}
			}
			else{
				cal0_retry++;
				cal1_retry++;
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Delay 20ms TX IQK Not Ready!!!!!\n"));
				if (cal0_retry == 10)
					break;	
			}
			if (TX0_Average >= 2){
				for (i = 0; i < TX0_Average; i++){
					for (ii = i+1; ii <TX0_Average; ii++){
						dx = (TX_IQC_temp[i][0]>>21) - (TX_IQC_temp[ii][0]>>21);
						if (dx < 4 && dx > -4){
							dy = (TX_IQC_temp[i][1]>>21) - (TX_IQC_temp[ii][1]>>21);
							if (dy < 4 && dy > -4){
								TX_IQC[0] = ((TX_IQC_temp[i][0]>>21) + (TX_IQC_temp[ii][0]>>21))/2;
								TX_IQC[1] = ((TX_IQC_temp[i][1]>>21) + (TX_IQC_temp[ii][1]>>21))/2;
								ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TXA_X = %x;;TXA_Y = %x\n", TX_IQC[0]&0x000007ff, TX_IQC[1]&0x000007ff));
								TX0_finish = TRUE;
							}
						}
					}
				}
			}
			if (TX1_Average >= 2){
				for (i = 0; i < TX1_Average; i++){
					for (ii = i+1; ii <TX1_Average; ii++){
						dx = (TX_IQC_temp[i][2]>>21) - (TX_IQC_temp[ii][2]>>21);
						if (dx < 4 && dx > -4){
							dy = (TX_IQC_temp[i][3]>>21) - (TX_IQC_temp[ii][3]>>21);
							if (dy < 4 && dy > -4){
								TX_IQC[2] = ((TX_IQC_temp[i][2]>>21) + (TX_IQC_temp[ii][2]>>21))/2;
								TX_IQC[3] = ((TX_IQC_temp[i][3]>>21) + (TX_IQC_temp[ii][3]>>21))/2;
								ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TXB_X = %x;;TXB_Y = %x\n", TX_IQC[2]&0x000007ff, TX_IQC[3]&0x000007ff));
								TX1_finish = TRUE;
							}
						}
					}
				}
			}
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TX0_Average = %d, TX1_Average = %d\n", TX0_Average, TX1_Average));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TX0_finish = %d, TX1_finish = %d\n", TX0_finish, TX1_finish));
			if (TX0_finish && TX1_finish)
				break;
			if ((cal0_retry + TX0_Average) >= 10 || (cal1_retry + TX1_Average) >= 10 )
				break;
		}
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TXA_cal_retry = %d\n", cal0_retry));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TXB_cal_retry = %d\n", cal1_retry));

	}

	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x58, 0x7fe00, ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x8, 0xffc00)); // Load LOK
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x58, 0x7fe00, ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x8, 0xffc00)); // Load LOK
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
	
	if (VDF_enable == 1){}
	else{
		ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
		if (TX0_finish){
		//====== Path A RX IQK RF Setting======
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xef, bRFRegOffsetMask, 0x80000);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x30, bRFRegOffsetMask, 0x30000);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x31, bRFRegOffsetMask, 0x3f7ff);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x32, bRFRegOffsetMask, 0xfe7bf);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x8f, bRFRegOffsetMask, 0x88001);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x65, bRFRegOffsetMask, 0x931d1);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xef, bRFRegOffsetMask, 0x00000);
		}
		if (TX1_finish){
		//====== Path B RX IQK RF Setting======
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0xef, bRFRegOffsetMask, 0x80000);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x30, bRFRegOffsetMask, 0x30000);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x31, bRFRegOffsetMask, 0x3f7ff);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x32, bRFRegOffsetMask, 0xfe7bf);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x8f, bRFRegOffsetMask, 0x88001);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x65, bRFRegOffsetMask, 0x931d1);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0xef, bRFRegOffsetMask, 0x00000);
		}
		ODM_SetBBReg(pDM_Odm, 0x978, BIT(31), 0x1);
		ODM_SetBBReg(pDM_Odm, 0x97c, BIT(31), 0x0);
		ODM_Write4Byte(pDM_Odm, 0x90c, 0x00008000);
                if (pDM_Odm->SupportInterface == 1 && pDM_Odm->RFEType == 1)
		     ODM_Write4Byte(pDM_Odm, 0x984, 0x0046a911);
                else
                     ODM_Write4Byte(pDM_Odm, 0x984, 0x0046a890);
		//ODM_Write4Byte(pDM_Odm, 0x984, 0x0046a890);
		ODM_Write4Byte(pDM_Odm, 0xcb0, 0x77777717);
		ODM_Write4Byte(pDM_Odm, 0xcb4, 0x00000077);
		ODM_Write4Byte(pDM_Odm, 0xeb0, 0x77777717);
		ODM_Write4Byte(pDM_Odm, 0xeb4, 0x00000077);
		
		ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
		if (TX0_finish){
		ODM_Write4Byte(pDM_Odm, 0xc80, 0x38008c10);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
		ODM_Write4Byte(pDM_Odm, 0xc84, 0x18008c10);// RX_Tone_idx[9:0], RxK_Mask[29]
		ODM_Write4Byte(pDM_Odm, 0xc88, 0x82140119);
		}
		if (TX1_finish){
		ODM_Write4Byte(pDM_Odm, 0xe80, 0x38008c10);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
		ODM_Write4Byte(pDM_Odm, 0xe84, 0x18008c10);// RX_Tone_idx[9:0], RxK_Mask[29]
		ODM_Write4Byte(pDM_Odm, 0xe88, 0x82140119);
		}
              cal0_retry = 0;
		cal1_retry = 0;
		while(1){
		    // one shot
		    	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
		    	if (TX0_finish){
			ODM_SetBBReg(pDM_Odm, 0x978, 0x03FF8000, (TX_IQC[0])&0x000007ff);
			ODM_SetBBReg(pDM_Odm, 0x978, 0x000007FF, (TX_IQC[1])&0x000007ff);
			ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
			if (pDM_Odm->SupportInterface == 1 && pDM_Odm->RFEType == 1){
				ODM_Write4Byte(pDM_Odm, 0xc8c, 0x28161500);
			}
			else{
				ODM_Write4Byte(pDM_Odm, 0xc8c, 0x28160cc0);
			}
			ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00300000);// cb8[20] 將 SI/PI 使用權切給 iqk_dpk module
			ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00100000);
			ODM_delay_ms(5); //Delay 5ms
			ODM_Write4Byte(pDM_Odm, 0xc8c, 0x3c000000);
			ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00000000);
		    	}
			if (TX1_finish){
			ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
			ODM_SetBBReg(pDM_Odm, 0x978, 0x03FF8000, (TX_IQC[2])&0x000007ff);
			ODM_SetBBReg(pDM_Odm, 0x978, 0x000007FF, (TX_IQC[3])&0x000007ff);
			ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
			if (pDM_Odm->SupportInterface == 1 && pDM_Odm->RFEType == 1){
				ODM_Write4Byte(pDM_Odm, 0xe8c, 0x28161900);
			}
			else{
				ODM_Write4Byte(pDM_Odm, 0xe8c, 0x28160ca0);
			}
			ODM_Write4Byte(pDM_Odm, 0xeb8, 0x00300000);// cb8[20] 將 SI/PI 使用權切給 iqk_dpk module
			ODM_Write4Byte(pDM_Odm, 0xeb8, 0x00100000);// cb8[20] 將 SI/PI 使用權切給 iqk_dpk module
			ODM_delay_ms(5); //Delay 5ms
			ODM_Write4Byte(pDM_Odm, 0xe8c, 0x3c000000);
			ODM_Write4Byte(pDM_Odm, 0xeb8, 0x00000000);
			}
			delay_count = 0;
			while (1){
				if (!RX0_finish && TX0_finish)
					IQK0_ready = (BOOLEAN) ODM_GetBBReg(pDM_Odm, 0xd00, BIT(10));
				if (!RX1_finish && TX1_finish)
					IQK1_ready = (BOOLEAN) ODM_GetBBReg(pDM_Odm, 0xd40, BIT(10));
				if ((IQK0_ready && IQK1_ready)||(delay_count>20))
					break;
				else{
					ODM_delay_ms(1);
					delay_count++;
				}
			}
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RX delay_count = %d\n", delay_count));
			if (delay_count < 20){	// If 20ms No Result, then cal_retry++
				// ============RXIQK Check==============
				RX0_fail = (BOOLEAN) ODM_GetBBReg(pDM_Odm, 0xd00, BIT(11));
				RX1_fail = (BOOLEAN) ODM_GetBBReg(pDM_Odm, 0xd40, BIT(11));
				if (!(RX0_fail || RX0_finish) && TX0_finish){
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x06000000);
					RX_IQC_temp[RX0_Average][0] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x08000000);
					RX_IQC_temp[RX0_Average][1] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RX_X0[%d] = %x ;; RX_Y0[%d] = %x\n", RX0_Average, (RX_IQC_temp[RX0_Average][0])>>21&0x000007ff, RX0_Average, (RX_IQC_temp[RX0_Average][1])>>21&0x000007ff));
/*					
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x05000000);
					reg1 = ODM_GetBBReg(pDM_Odm, 0xd00, 0xffffffff);
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x06000000);
					reg2 = ODM_GetBBReg(pDM_Odm, 0xd00, 0x0000001f);
					DbgPrint("reg1 = %d, reg2 = %d", reg1, reg2);
					Image_Power = (reg2<<32)+reg1;
					DbgPrint("Before PW = %d\n", Image_Power);
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x07000000);
					reg1 = ODM_GetBBReg(pDM_Odm, 0xd00, 0xffffffff);
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x08000000);
					reg2 = ODM_GetBBReg(pDM_Odm, 0xd00, 0x0000001f);
					Image_Power = (reg2<<32)+reg1;
					DbgPrint("After PW = %d\n", Image_Power);
*/					
					RX0_Average++;
				}
				else{
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("1. RXA_cal_retry = %d\n", cal0_retry));
					cal0_retry++;
					if (cal0_retry == 10)
					break;
				}
				if (!(RX1_fail || RX1_finish) && TX1_finish){
                            		ODM_Write4Byte(pDM_Odm, 0xeb8, 0x06000000);
                            		RX_IQC_temp[RX1_Average][2] = ODM_GetBBReg(pDM_Odm, 0xd40, 0x07ff0000)<<21;
                            		ODM_Write4Byte(pDM_Odm, 0xeb8, 0x08000000);
                            		RX_IQC_temp[RX1_Average][3] = ODM_GetBBReg(pDM_Odm, 0xd40, 0x07ff0000)<<21;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RX_X1[%d] = %x ;; RX_Y1[%d] = %x\n", RX1_Average, (RX_IQC_temp[RX1_Average][2])>>21&0x000007ff, RX1_Average, (RX_IQC_temp[RX1_Average][3])>>21&0x000007ff));
/*					
					ODM_Write4Byte(pDM_Odm, 0xeb8, 0x05000000);
					reg1 = ODM_GetBBReg(pDM_Odm, 0xd40, 0xffffffff);
					ODM_Write4Byte(pDM_Odm, 0xeb8, 0x06000000);
					reg2 = ODM_GetBBReg(pDM_Odm, 0xd40, 0x0000001f);
					DbgPrint("reg1 = %d, reg2 = %d", reg1, reg2);
					Image_Power = (reg2<<32)+reg1;
					DbgPrint("Before PW = %d\n", Image_Power);
					ODM_Write4Byte(pDM_Odm, 0xeb8, 0x07000000);
					reg1 = ODM_GetBBReg(pDM_Odm, 0xd40, 0xffffffff);
					ODM_Write4Byte(pDM_Odm, 0xeb8, 0x08000000);
					reg2 = ODM_GetBBReg(pDM_Odm, 0xd40, 0x0000001f);
					Image_Power = (reg2<<32)+reg1;
					DbgPrint("After PW = %d\n", Image_Power);
*/				
					RX1_Average++;
                        	}
                        	else{
                            		cal1_retry++;
                            		if (cal1_retry == 10)
                                		break;
                    		}
				
			}
			else{
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("2. RXA_cal_retry = %d\n", cal0_retry));
				cal0_retry++;
				cal1_retry++;
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Delay 20ms RX IQK Not Ready!!!!!\n"));
			    if (cal0_retry == 10)
			        break;
			}
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("3. RXA_cal_retry = %d\n", cal0_retry));
			if (RX0_Average >= 2){
				for (i = 0; i < RX0_Average; i++){
					for (ii = i+1; ii <RX0_Average; ii++){
					dx = (RX_IQC_temp[i][0]>>21) - (RX_IQC_temp[ii][0]>>21);
						if (dx < 4 && dx > -4){
						dy = (RX_IQC_temp[i][1]>>21) - (RX_IQC_temp[ii][1]>>21);
							if (dy < 4 && dy > -4){
								RX_IQC[0]= ((RX_IQC_temp[i][0]>>21) + (RX_IQC_temp[ii][0]>>21))/2;
								RX_IQC[1] = ((RX_IQC_temp[i][1]>>21) + (RX_IQC_temp[ii][1]>>21))/2;
								RX0_finish = TRUE;
								break;
							}
						}
					}
				}
			}
			if (RX1_Average >= 2){
				for (i = 0; i < RX1_Average; i++){
					for (ii = i+1; ii <RX1_Average; ii++){
					dx = (RX_IQC_temp[i][2]>>21) - (RX_IQC_temp[ii][2]>>21);
						if (dx < 4 && dx > -4){
						dy = (RX_IQC_temp[i][3]>>21) - (RX_IQC_temp[ii][3]>>21);
							if (dy < 4 && dy > -4){
								RX_IQC[2] = ((RX_IQC_temp[i][2]>>21) + (RX_IQC_temp[ii][2]>>21))/2;
								RX_IQC[3] = ((RX_IQC_temp[i][3]>>21) + (RX_IQC_temp[ii][3]>>21))/2;
								RX1_finish = TRUE;
								break;
							}
						}
					}
				}
			}
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RX0_Average = %d, RX1_Average = %d\n", RX0_Average, RX1_Average));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RX0_finish = %d, RX1_finish = %d\n", RX0_finish, RX1_finish));
			if ((RX0_finish|| !TX0_finish) && (RX1_finish || !TX1_finish) )
				break;
			if ((cal0_retry + RX0_Average) >= 10 || (cal1_retry + RX1_Average) >= 10 || RX0_Average == 3 || RX1_Average == 3)
				break;
		}
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RXA_cal_retry = %d\n", cal0_retry));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RXB_cal_retry = %d\n", cal1_retry));
	}

	// FillIQK Result
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("========Path_A =======\n"));

	if (TX0_finish){
		_IQK_TX_FillIQC_8812A(pDM_Odm, ODM_RF_PATH_A, TX_IQC[0], TX_IQC[1]);
	}
	else{
		_IQK_TX_FillIQC_8812A(pDM_Odm, ODM_RF_PATH_A, 0x200, 0x0);
	}



	if (RX0_finish == 1){
		_IQK_RX_FillIQC_8812A(pDM_Odm, ODM_RF_PATH_A, RX_IQC[0], RX_IQC[1]);
	}
	else{
		_IQK_RX_FillIQC_8812A(pDM_Odm, ODM_RF_PATH_A, 0x200, 0x0);
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("========Path_B =======\n"));

	if (TX1_finish){
		_IQK_TX_FillIQC_8812A(pDM_Odm, ODM_RF_PATH_B, TX_IQC[2], TX_IQC[3]);
	}
	else{
		_IQK_TX_FillIQC_8812A(pDM_Odm, ODM_RF_PATH_B, 0x200, 0x0);
	}



	if (RX1_finish == 1){
		_IQK_RX_FillIQC_8812A(pDM_Odm, ODM_RF_PATH_B, RX_IQC[2], RX_IQC[3]);
	}
	else{
		_IQK_RX_FillIQC_8812A(pDM_Odm, ODM_RF_PATH_B, 0x200, 0x0);
	}


		
}

#define MACBB_REG_NUM 10
#define AFE_REG_NUM 12
#define RF_REG_NUM 3

// Maintained by BB James.
VOID	
phy_IQCalibrate_8812A(
	IN PDM_ODM_T		pDM_Odm,
	IN u1Byte		Channel
	)
{
	u4Byte	MACBB_backup[MACBB_REG_NUM], AFE_backup[AFE_REG_NUM], RFA_backup[RF_REG_NUM], RFB_backup[RF_REG_NUM];
	u4Byte 	Backup_MACBB_REG[MACBB_REG_NUM] = {0xb00, 0x520, 0x550, 0x808, 0x90c, 0xc00, 0xe00, 0x838,  0x82c}; 
	u4Byte 	Backup_AFE_REG[AFE_REG_NUM] = {0xc5c, 0xc60, 0xc64, 0xc68, 0xcb0, 0xcb4,
		       	                                                   0xe5c, 0xe60, 0xe64, 0xe68, 0xeb0, 0xeb4}; 
	u4Byte	Reg_C1B8, Reg_E1B8;
	u4Byte 	Backup_RF_REG[RF_REG_NUM] = {0x65, 0x8f, 0x0}; 
	u1Byte 	chnlIdx = ODM_GetRightChnlPlaceforIQK(Channel);
	
	_IQK_BackupMacBB_8812A(pDM_Odm, MACBB_backup, Backup_MACBB_REG, MACBB_REG_NUM);
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1);
	Reg_C1B8 = ODM_Read4Byte(pDM_Odm, 0xcb8);
	Reg_E1B8 = ODM_Read4Byte(pDM_Odm, 0xeb8);
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0);
	_IQK_BackupAFE_8812A(pDM_Odm, AFE_backup, Backup_AFE_REG, AFE_REG_NUM);
	_IQK_BackupRF_8812A(pDM_Odm, RFA_backup, RFB_backup, Backup_RF_REG, RF_REG_NUM);
	
	_IQK_ConfigureMAC_8812A(pDM_Odm);
	_IQK_Tx_8812A(pDM_Odm, chnlIdx);
	_IQK_RestoreRF_8812A(pDM_Odm, ODM_RF_PATH_A, Backup_RF_REG, RFA_backup, RF_REG_NUM);
	_IQK_RestoreRF_8812A(pDM_Odm, ODM_RF_PATH_B, Backup_RF_REG, RFB_backup, RF_REG_NUM);
	
	_IQK_RestoreAFE_8812A(pDM_Odm, AFE_backup, Backup_AFE_REG, AFE_REG_NUM);
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1);
	ODM_Write4Byte(pDM_Odm, 0xcb8, Reg_C1B8);
	ODM_Write4Byte(pDM_Odm, 0xeb8, Reg_E1B8);
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0);
	_IQK_RestoreMacBB_8812A(pDM_Odm, MACBB_backup, Backup_MACBB_REG, MACBB_REG_NUM);


}

VOID	
phy_LCCalibrate_8812A(
	IN 	PDM_ODM_T	pDM_Odm,
	IN	BOOLEAN		is2T
	)
{
	u4Byte	/*RF_Amode=0, RF_Bmode=0,*/ LC_Cal = 0, tmp = 0;
	
	//Check continuous TX and Packet TX
	u4Byte	reg0x914 = ODM_Read4Byte(pDM_Odm, rSingleTone_ContTx_Jaguar);;

	// Backup RF reg18.
	LC_Cal = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask);

	if((reg0x914 & 0x70000) != 0)	//If contTx, disable all continuous TX. 0x914[18:16]
		// <20121121, Kordan> A workaround: If we set 0x914[18:16] as zero, BB turns off ContTx
		// until another packet comes in. To avoid ContTx being turned off, we skip this step.
		;//ODM_Write4Byte(pDM_Odm, rSingleTone_ContTx_Jaguar, reg0x914 & (~0x70000));	
	else							// If packet Tx-ing, pause Tx.
		ODM_Write1Byte(pDM_Odm, REG_TXPAUSE, 0xFF);			


/*
	//3 1. Read original RF mode
	RF_Amode = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_AC, bRFRegOffsetMask);
	if(is2T)
		RF_Bmode = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_AC, bRFRegOffsetMask);	


	//3 2. Set RF mode = standby mode
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_AC, bRFRegOffsetMask, (RF_Amode&0x8FFFF)|0x10000);
	if(is2T)
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_AC, bRFRegOffsetMask, (RF_Bmode&0x8FFFF)|0x10000);			
*/

	// Enter LCK mode
	tmp = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_LCK, bRFRegOffsetMask);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_LCK, bRFRegOffsetMask, tmp | BIT14);

	//3 3. Read RF reg18
	LC_Cal = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask);
	
	//3 4. Set LC calibration begin bit15
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, LC_Cal|0x08000);

	// Leave LCK mode
	tmp = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_LCK, bRFRegOffsetMask);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_LCK, bRFRegOffsetMask, tmp & ~BIT14);
	
	ODM_delay_ms(100);		

	//3 Restore original situation
	if((reg0x914 & 70000) != 0) 	//Deal with contisuous TX case, 0x914[18:16]
	{  
		// <20121121, Kordan> A workaround: If we set 0x914[18:16] as zero, BB turns off ContTx
		// until another packet comes in. To avoid ContTx being turned off, we skip this step.
		//ODM_Write4Byte(pDM_Odm, rSingleTone_ContTx_Jaguar, reg0x914); 
	}
	else // Deal with Packet TX case
	{
		ODM_Write1Byte(pDM_Odm, REG_TXPAUSE, 0x00);	
	}

	// Recover channel number
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, LC_Cal);

	/*
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_AC, bRFRegOffsetMask, RF_Amode);
	if(is2T)
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_AC, bRFRegOffsetMask, RF_Bmode);
		*/

}





#define		DP_BB_REG_NUM		7
#define		DP_RF_REG_NUM		1
#define		DP_RETRY_LIMIT		10
#define		DP_PATH_NUM		2
#define		DP_DPK_NUM			3
#define		DP_DPK_VALUE_NUM	2

VOID	
phy_ReloadIQKSetting_8812A(
 	IN	PDM_ODM_T	pDM_Odm,
	IN	u1Byte		Channel
 	)
{
	PODM_RF_CAL_T  pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	u1Byte chnlIdx = ODM_GetRightChnlPlaceforIQK(Channel);
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
	ODM_SetBBReg(pDM_Odm, 0xccc, 0x000007ff, pRFCalibrateInfo->IQKMatrixRegSetting[chnlIdx].Value[*pDM_Odm->pBandWidth][0]&0x7ff);
	ODM_SetBBReg(pDM_Odm, 0xcd4, 0x000007ff, (pRFCalibrateInfo->IQKMatrixRegSetting[chnlIdx].Value[*pDM_Odm->pBandWidth][0]&0x7ff0000)>>16);
	ODM_SetBBReg(pDM_Odm, 0xecc, 0x000007ff, pRFCalibrateInfo->IQKMatrixRegSetting[chnlIdx].Value[*pDM_Odm->pBandWidth][2]&0x7ff);
	ODM_SetBBReg(pDM_Odm, 0xed4, 0x000007ff, (pRFCalibrateInfo->IQKMatrixRegSetting[chnlIdx].Value[*pDM_Odm->pBandWidth][2]&0x7ff0000)>>16);

	if (*pDM_Odm->pBandWidth != 2){
		ODM_Write4Byte(pDM_Odm, 0xce8, 0x0);
		ODM_Write4Byte(pDM_Odm, 0xee8, 0x0);
	}
	else{
		ODM_Write4Byte(pDM_Odm, 0xce8, pRFCalibrateInfo->IQKMatrixRegSetting[chnlIdx].Value[*pDM_Odm->pBandWidth][4]);
		ODM_Write4Byte(pDM_Odm, 0xee8, pRFCalibrateInfo->IQKMatrixRegSetting[chnlIdx].Value[*pDM_Odm->pBandWidth][5]);
	}
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	ODM_SetBBReg(pDM_Odm, 0xc10, 0x000003ff, (pRFCalibrateInfo->IQKMatrixRegSetting[chnlIdx].Value[*pDM_Odm->pBandWidth][1]&0x7ff0000)>>17);
	ODM_SetBBReg(pDM_Odm, 0xc10, 0x03ff0000, (pRFCalibrateInfo->IQKMatrixRegSetting[chnlIdx].Value[*pDM_Odm->pBandWidth][1]&0x7ff)>>1);
	ODM_SetBBReg(pDM_Odm, 0xe10, 0x000003ff, (pRFCalibrateInfo->IQKMatrixRegSetting[chnlIdx].Value[*pDM_Odm->pBandWidth][3]&0x7ff0000)>>17);
	ODM_SetBBReg(pDM_Odm, 0xe10, 0x03ff0000, (pRFCalibrateInfo->IQKMatrixRegSetting[chnlIdx].Value[*pDM_Odm->pBandWidth][3]&0x7ff)>>1);
	

}

VOID 
PHY_ResetIQKResult_8812A(
	IN	PDM_ODM_T	pDM_Odm
)
{
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
	ODM_SetBBReg(pDM_Odm, 0xccc, 0x000007ff, 0x0);
	ODM_SetBBReg(pDM_Odm, 0xcd4, 0x000007ff, 0x200);
	ODM_SetBBReg(pDM_Odm, 0xecc, 0x000007ff, 0x0);
	ODM_SetBBReg(pDM_Odm, 0xed4, 0x000007ff, 0x200);
	ODM_Write4Byte(pDM_Odm, 0xce8, 0x0);
	ODM_Write4Byte(pDM_Odm, 0xee8, 0x0);
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	ODM_SetBBReg(pDM_Odm, 0xc10, 0x000003ff, 0x100);
	ODM_SetBBReg(pDM_Odm, 0xe10, 0x000003ff, 0x100);
}

VOID
phy_IQCalibrate_By_FW_8812A(
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u1Byte			IQKcmd[3] = {pHalData->CurrentChannel, 0x0, 0x0};
	u1Byte			Buf1 = 0x0;
	u1Byte			Buf2 = 0x0;

//Byte 2, Bit 4 ~ Bit 5 : BandType
	if(pHalData->CurrentBandType)
		Buf1 = 0x2<<4;
	else
		Buf1 = 0x1<<4;
	
//Byte 2, Bit 0 ~ Bit 3 : Bandwidth
	if(pHalData->CurrentChannelBW == CHANNEL_WIDTH_20)
		Buf2 = 0x1;
	else if(pHalData->CurrentChannelBW == CHANNEL_WIDTH_40)
		Buf2 = 0x1<<1;
	else if(pHalData->CurrentChannelBW == CHANNEL_WIDTH_80)
		Buf2 = 0x1<<2;
	else
		Buf2 = 0x1<<3;
	
	IQKcmd[1] = Buf1 | Buf2;
	IQKcmd[2] = pHalData->ExternalPA_5G | pHalData->ExternalLNA_5G<<1;

	DBG_871X("== IQK Start ==\n");

	FillH2CCmd_8812(pAdapter, 0x45, 3, IQKcmd);

	rtl8812_iqk_wait(pAdapter, 500);
}

VOID
PHY_IQCalibrate_8812A(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN 	bReCovery
	)
{
	u4Byte			counter = 0;

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	PMGNT_INFO		pMgntInfo = &(pAdapter->MgntInfo);
	#else  // (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;	
	#endif
#endif	

#if (MP_DRIVER == 1)
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	
	PMPT_CONTEXT	pMptCtx = &(pAdapter->MptCtx);	
	#else// (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);		
	#endif	
#endif//(MP_DRIVER == 1)

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE) )
	if (ODM_CheckPowerStatus(pAdapter) == FALSE)
		return;
#endif

#if MP_DRIVER == 1	
	if( ! (pMptCtx->bSingleTone || pMptCtx->bCarrierSuppression) )
#endif		
	{

		if(pHalData->RegIQKFWOffload)
		{
			phy_IQCalibrate_By_FW_8812A(pAdapter);
		}
		else
		{
			phy_IQCalibrate_8812A(pDM_Odm, pHalData->CurrentChannel);
		}
	}
}


VOID
PHY_LCCalibrate_8812A(
	IN PDM_ODM_T		pDM_Odm
	)
{
	BOOLEAN 		bStartContTx = FALSE, bSingleTone = FALSE, bCarrierSuppression = FALSE;
	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	PADAPTER 		pAdapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	

	#if (MP_DRIVER == 1)
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PMPT_CONTEXT	pMptCtx = &(pAdapter->MptCtx);	
	#else// (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);		
	#endif
	bStartContTx = pMptCtx->bStartContTx;
	bSingleTone = pMptCtx->bSingleTone;
	bCarrierSuppression = pMptCtx->bCarrierSuppression;
	#endif//(MP_DRIVER == 1)
#endif	

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("===> PHY_LCCalibrate_8812A\n"));

#if (MP_DRIVER == 1)	
	phy_LCCalibrate_8812A(pDM_Odm, TRUE);
#endif 

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("<=== PHY_LCCalibrate_8812A\n"));

}

VOID phy_SetRFPathSwitch_8812A(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN		bMain,
	IN	BOOLEAN		is2T
	)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif

#endif		

	if (IS_HARDWARE_TYPE_8821(pAdapter)) 
	{
		if(bMain)
			ODM_SetBBReg(pDM_Odm, rA_RFE_Pinmux_Jaguar+4, BIT29|BIT28, 0x1);	//Main
		else
			ODM_SetBBReg(pDM_Odm, rA_RFE_Pinmux_Jaguar+4, BIT29|BIT28, 0x2);	//Aux
	}
	else if (IS_HARDWARE_TYPE_8812(pAdapter)) 
	{
		if (pHalData->RFEType == 5)
		{
			if(bMain) {
				//WiFi 
				ODM_SetBBReg(pDM_Odm, r_ANTSEL_SW_Jaguar, BIT1|BIT0, 0x2);  
            	ODM_SetBBReg(pDM_Odm, r_ANTSEL_SW_Jaguar, BIT9|BIT8, 0x3);  
			} else {
				// BT
				ODM_SetBBReg(pDM_Odm, r_ANTSEL_SW_Jaguar, BIT1|BIT0, 0x1);  
            	ODM_SetBBReg(pDM_Odm, r_ANTSEL_SW_Jaguar, BIT9|BIT8, 0x3);  
			}
		}
	}

}

VOID PHY_SetRFPathSwitch_8812A(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN		bMain
	)
{

#if DISABLE_BB_RF
	return;
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

		phy_SetRFPathSwitch_8812A(pAdapter, bMain, TRUE);

#endif		
}


VOID
_DPK_ThermalCompensation(
	IN 	PDM_ODM_T	pDM_Odm
	)
{
}

VOID
_DPK_parareload(
	IN 	PDM_ODM_T	pDM_Odm,
	IN pu4Byte		MACBB_backup,
	IN pu4Byte		Backup_MACBB_REG, 
	IN u4Byte		MACBB_NUM

	
	)
{
	u4Byte i;

	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	 //save MACBB default value
	for (i = 0; i < MACBB_NUM; i++){
		ODM_Write4Byte(pDM_Odm, Backup_MACBB_REG[i], MACBB_backup[i]);
	}
}


VOID
_DPK_parabackup(
	IN 	PDM_ODM_T	pDM_Odm,
	IN pu4Byte		MACBB_backup,
	IN pu4Byte		Backup_MACBB_REG, 
	IN u4Byte		MACBB_NUM

	
	)
{
	u4Byte i;

	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	 //save MACBB default value
	for (i = 0; i < MACBB_NUM; i++){
		MACBB_backup[i] = ODM_Read4Byte(pDM_Odm, Backup_MACBB_REG[i]);
	}
}

VOID
_DPK_Globalparaset(
	IN 	PDM_ODM_T	pDM_Odm	
	)
{

    //***************************************//
	//set MAC register
	//***************************************//
	
	//TX pause
	ODM_Write4Byte(pDM_Odm, 0x520, 0x007f3F0F);

	//***************************************//
	//set BB register
	//***************************************//
	
	// reg82c[31] = b'0, 切換到 page C
	ODM_Write4Byte(pDM_Odm, 0x82c, 0x002083d5);

	// test pin in/out control
	ODM_Write4Byte(pDM_Odm, 0x970, 0x00000000);

	// path A regcb8[3:0] = h'd, TRSW to TX
	ODM_Write4Byte(pDM_Odm, 0xcb8, 0x0050824d);	

	// path B regeb8[3:0] = h'd, TRSW to TX
	ODM_Write4Byte(pDM_Odm, 0xeb8, 0x0050824d);	
	
	// reg838[3:0] = h'c, CCA off
	ODM_Write4Byte(pDM_Odm, 0x838, 0x06c8d24c);	

	// path A 3-wire off
	ODM_Write4Byte(pDM_Odm, 0xc00, 0x00000004);	
	
	// path B 3-wire off
	ODM_Write4Byte(pDM_Odm, 0xe00, 0x00000004);	
	
	// reg90c[15] = b'1, DAC fifo reset by CSWU
	ODM_Write4Byte(pDM_Odm, 0x90c, 0x00008000);	
	
	// reset DPK circuit
	ODM_Write4Byte(pDM_Odm, 0xb00, 0x03000100);		
	
	// path A regc94[0] = b'1 (r_gothrough_iqkdpk), 將 DPK 切進 normal path
	ODM_Write4Byte(pDM_Odm, 0xc94, 0x01000001);	

	// path B rege94[0] = b'1 (r_gothrough_iqkdpk), 將 DPK 切進 normal path
	ODM_Write4Byte(pDM_Odm, 0xe94, 0x01000001);			

	//***************************************//
	//set AFE register
	//***************************************//

	//path A
 	//regc68 到 regc84應該是要跟正常 Tx mode 時的設定一致

	ODM_Write4Byte(pDM_Odm, 0xc68, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xc6c, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xc70, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xc74, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xc78, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xc7c, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xc80, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xc84, 0x19791979);	

	// force DAC/ADC power on
	ODM_Write4Byte(pDM_Odm, 0xc60, 0x77777777);	
	ODM_Write4Byte(pDM_Odm, 0xc64, 0x77777777);	

	//path B
 	//rege68 到 rege84應該是要跟正常 Tx mode 時的設定一致

	ODM_Write4Byte(pDM_Odm, 0xe68, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xe6c, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xe70, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xe74, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xe78, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xe7c, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xe80, 0x19791979);	
	ODM_Write4Byte(pDM_Odm, 0xe84, 0x19791979);	

	// force DAC/ADC power on
	ODM_Write4Byte(pDM_Odm, 0xe60, 0x77777777);	
	ODM_Write4Byte(pDM_Odm, 0xe64, 0x77777777);	

}


VOID
_DPK_GetGainLoss(
	IN 	PDM_ODM_T	pDM_Odm, 
	IN u1Byte path
	)
{
	u4Byte GL_I=0,GL_Q=0;
	u4Byte GL_I_tmp=0,GL_Q_tmp=0;
	
	u4Byte Power_GL;
	u2Byte Scaler[]={0x4000, 0x41db, 0x43c7, 0x45c3, 0x47cf, 0x49ec, 0x4c19, 0x4e46, 0x5093,0x52f2,  //10
					 0x5560, 0x57cf, 0x5a7f, 0x5d0e, 0x5fbe
						};
	u1Byte sindex=0;
	u4Byte pagesel = 0,regsel = 0;

	if(path == 0)  //pathA
		{
		pagesel = 0;
		regsel = 0;
		}
	else	//pathB
		{
		pagesel = 0x200;
		regsel = 0x40;
		}

	ODM_Write4Byte(pDM_Odm, 0xc90+pagesel, 0x0601f0bf);	
	ODM_Write4Byte(pDM_Odm, 0xcb8+pagesel, 0x0c000000);	



	GL_I_tmp = ODM_GetBBReg(pDM_Odm, 0xd00+regsel, 0xffff0000);
	GL_Q_tmp = ODM_GetBBReg(pDM_Odm, 0xd00+regsel, 0x0000ffff);

    if(GL_I_tmp >= 0x8000)
		GL_I = (GL_I_tmp-0x8000+0x1);
	else
		GL_I = GL_I_tmp;


    if(GL_Q_tmp >= 0x8000)
		GL_Q = (GL_Q_tmp-0x8000+0x1);
	else
		GL_Q = GL_Q_tmp;	
	
 	Power_GL = ((GL_I*GL_I)+(GL_Q*GL_Q))/4;
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Power_GL = 0x%x", Power_GL));

	if (Power_GL > 63676){
		sindex = 0;
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 0 dB\n"));
		}
	else if (63676 >= Power_GL && Power_GL > 60114){
		sindex = 1;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	0.25 dB\n"));
		}		
	else if (60114 >= Power_GL && Power_GL> 56751){
		sindex = 2;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	0.5 dB\n"));
		}
	else if (56751 >= Power_GL && Power_GL> 53577){
		sindex = 3;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	0.75 dB\n"));
		}
	else if (53577 >= Power_GL && Power_GL> 49145){
		sindex = 4;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	1 dB\n"));
		}
	else if (49145 >= Power_GL && Power_GL> 47750){
		sindex = 5;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	1.25 dB\n"));
		}
	else if (47750 >= Power_GL && Power_GL> 45079){
		sindex = 6;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	1.5 dB\n"));
		}
	else if (45079 >= Power_GL && Power_GL> 42557){
		sindex = 7;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	1.75 dB\n"));
		}
	else if (42557 >= Power_GL && Power_GL> 40177){
		sindex = 8;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	2 dB\n"));
		}
	else if (40177 >= Power_GL && Power_GL> 37929){
		sindex = 9;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	2.25 dB\n"));
		}
	else if (37929 >= Power_GL && Power_GL> 35807){
		sindex = 10;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	2.5 dB\n"));
		}
	else if (35807 >= Power_GL && Power_GL> 33804){
		sindex = 11;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	2.75 dB\n"));
		}
	else if (33804 >= Power_GL && Power_GL> 31913){
		sindex = 12;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	3 dB\n"));
		}
	else if (31913 >= Power_GL && Power_GL> 30128){
		sindex = 13;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	3.25 dB\n"));
		}
	else if (30128 >= Power_GL){
		sindex = 14;	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Gainloss = 	3.5 dB\n"));
		}


	ODM_Write4Byte(pDM_Odm, 0xc98+pagesel, (Scaler[sindex] << 16) | Scaler[sindex]);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Set Gainloss reg 0xc98(0xe98)= 0x%x\n",ODM_Read4Byte(pDM_Odm, 0xc98+pagesel)));

}


VOID	
_DPK_EnableDP(
	IN 	PDM_ODM_T	pDM_Odm,
	IN u1Byte path,
	IN u4Byte TXindex
	)
{

	//***************************************//
	//Enable DP
	//***************************************// 

	//PWSF[6] = 0x40 = 0dB, set the address represented TXindex as 0dB
	u1Byte PWSF[] = { 0xff, 0xca, 0xa1, 0x80, 0x65, 0x51, 0x40,  //6~0dB
				  0x33, 0x28, 0x20, 0x19, 0x14, 0x10, 0x0d,  //-1~-7dB
				  0x0a, 0x08, 0x06, 0x05, 0x04, 0x03, 0x03,  //-8~-14dB
				  0x02, 0x02, 0x01, 0x01,
				  };
	u1Byte zeropoint;
	u1Byte pwsf1,pwsf2;
	u1Byte i;
	u4Byte pagesel = 0,regsel = 0;

	if(path == 0)
		{
		pagesel = 0;
		regsel = 0;
		}
	else
		{
		pagesel = 0x200;
		regsel = 0x40;
		}


	//=========//
	// DPK setting	//
	//=========//
	// reg82c[31] = b'1, 切換到 page C1
	ODM_Write4Byte(pDM_Odm, 0x82c, 0x802083d5); 


	ODM_Write4Byte(pDM_Odm, 0xc90+pagesel, 0x0000f098); 
	ODM_Write4Byte(pDM_Odm, 0xc94+pagesel, 0x776c9f84); 
	ODM_Write4Byte(pDM_Odm, 0xcc4+pagesel, 0x08840000); 
	ODM_Write4Byte(pDM_Odm, 0xcc8+pagesel, 0x20000000); 		
	ODM_Write4Byte(pDM_Odm, 0xc8c+pagesel, 0x3c000000); 


	// 寫PWSF table in 1st SRAM for PA = 11 use
	ODM_Write4Byte(pDM_Odm, 0xc20+pagesel, 0x00000800); 

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Write PWSF table\n"));


	if(TXindex == 0x1f)
		zeropoint = 0;
	else if(TXindex == 0x1e)
		zeropoint = 1;
	else if(TXindex == 0x1d)
		zeropoint = 2;
	else if(TXindex == 0x1c)
		zeropoint = 3;
	else if(TXindex == 0x1b)
		zeropoint = 4;
	else if(TXindex == 0x1a)
		zeropoint = 5;
	else if(TXindex == 0x19)
		zeropoint = 6;
	else
		zeropoint = 6;

	

	for(i=0;i<16;i++)
		{
			if(((6-zeropoint)+i*2) > 24)
				pwsf1 = 24;
			else
				pwsf1 = (6-zeropoint)+i*2;

			if(((6-zeropoint-1)+i*2) > 24)
				pwsf2 = 24;
			else
				pwsf2 = (6-zeropoint-1)+i*2;

			ODM_Write4Byte(pDM_Odm, 0xce4+pagesel, 0x00000001 | i<<1 | (PWSF[pwsf1]<<8) | (PWSF[pwsf2]<<16));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0x%x\n", ODM_Read4Byte(pDM_Odm, 0xce4+pagesel)));
			ODM_SetBBReg(pDM_Odm, 0xce4+pagesel, 0xff, 0x0);	
	}

	ODM_Write4Byte(pDM_Odm, 0xce4+pagesel, 0x00000000); 

	// reg82c[31] = b'0, 切換到 page C
	ODM_Write4Byte(pDM_Odm, 0x82c, 0x002083d5); 

}


VOID
_DPK_pathABDPK(
	IN 	PDM_ODM_T	pDM_Odm	
	)
{
	u4Byte TXindex = 0;
	u1Byte path = 0;
	u4Byte pagesel = 0,regsel = 0;
	u4Byte i=0,j=0;
	
	for(path=0;path<2;path ++)	//path A = 0; path B = 1;
		{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path %s DPK start!!!\n", (path==0)?"A":"B"));

		if(path == 0)
			{
			pagesel = 0;
			regsel = 0;
			}
		else
			{
			pagesel = 0x200;
			regsel = 0x40;
			}
		
    //***************************************//
	//find compress-2.5dB TX index
	//***************************************//

	
	// reg82c[31] = b'1, 切換到 page C1
	ODM_Write4Byte(pDM_Odm, 0x82c, 0x802083d5);

	// regc20[15:13] = dB sel, 告訴 Gain Loss function 去尋找 dB_sel 所設定的PA gain loss目標所對應的 Tx AGC 為何.
	// dB_sel = b'000 ' 1.0 dB PA gain loss
	// dB_sel = b'001 ' 1.5 dB PA gain loss
	// dB_sel = b'010 ' 2.0 dB PA gain loss
	// dB_sel = b'011 ' 2.5 dB PA gain loss
	// dB_sel = b'100 ' 3.0 dB PA gain loss
	// dB_sel = b'101 ' 3.5 dB PA gain loss
	// dB_sel = b'110 ' 4.0 dB PA gain loss
	ODM_Write4Byte(pDM_Odm, 0xc20+pagesel, 0x00006000);

	ODM_Write4Byte(pDM_Odm, 0xc90+pagesel, 0x0401e038);	
	ODM_Write4Byte(pDM_Odm, 0xc94+pagesel, 0xf76c9f84);	
	ODM_Write4Byte(pDM_Odm, 0xcc8+pagesel, 0x000c5599);	
	ODM_Write4Byte(pDM_Odm, 0xcc4+pagesel, 0x148b0000);	
	ODM_Write4Byte(pDM_Odm, 0xc8c+pagesel, 0x3c000000);	

	// tx_amp ' 決定 Ramp 中各弦波的振幅大小
	ODM_Write4Byte(pDM_Odm, 0xc98+pagesel, 0x41382e21);	
	ODM_Write4Byte(pDM_Odm, 0xc9c+pagesel, 0x5b554f48);	
	ODM_Write4Byte(pDM_Odm, 0xca0+pagesel, 0x6f6b6661);	
	ODM_Write4Byte(pDM_Odm, 0xca4+pagesel, 0x817d7874);	
	ODM_Write4Byte(pDM_Odm, 0xca8+pagesel, 0x908c8884);	
	ODM_Write4Byte(pDM_Odm, 0xcac+pagesel, 0x9d9a9793);	
	ODM_Write4Byte(pDM_Odm, 0xcb0+pagesel, 0xaaa7a4a1);	
	ODM_Write4Byte(pDM_Odm, 0xcb4+pagesel, 0xb6b3b0ad);
	
	// tx_inverse ' Ramp 中各弦波power 的倒數, 以計算出 PA 的 gain report??
	ODM_Write4Byte(pDM_Odm, 0xc40+pagesel, 0x02ce03e9);	
	ODM_Write4Byte(pDM_Odm, 0xc44+pagesel, 0x01fd0249);	
	ODM_Write4Byte(pDM_Odm, 0xc48+pagesel, 0x01a101c9);	
	ODM_Write4Byte(pDM_Odm, 0xc4c+pagesel, 0x016a0181);	
	ODM_Write4Byte(pDM_Odm, 0xc50+pagesel, 0x01430155);	
	ODM_Write4Byte(pDM_Odm, 0xc54+pagesel, 0x01270135);	
	ODM_Write4Byte(pDM_Odm, 0xc58+pagesel, 0x0112011c);	
	ODM_Write4Byte(pDM_Odm, 0xc5c+pagesel, 0x01000108);
	ODM_Write4Byte(pDM_Odm, 0xc60+pagesel, 0x00f100f8);	
	ODM_Write4Byte(pDM_Odm, 0xc64+pagesel, 0x00e500eb);	
	ODM_Write4Byte(pDM_Odm, 0xc68+pagesel, 0x00db00e0);	
	ODM_Write4Byte(pDM_Odm, 0xc6c+pagesel, 0x00d100d5);	
	ODM_Write4Byte(pDM_Odm, 0xc70+pagesel, 0x00c900cd);	
	ODM_Write4Byte(pDM_Odm, 0xc74+pagesel, 0x00c200c5);	
	ODM_Write4Byte(pDM_Odm, 0xc78+pagesel, 0x00bb00be);	
	ODM_Write4Byte(pDM_Odm, 0xc7c+pagesel, 0x00b500b8);

	//============//
	// RF setting for DPK //
	//============//

	//pathA,pathB standby mode
    ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)0x0, 0x0, bRFRegOffsetMask, 0x10000);
	ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)0x1, 0x0, bRFRegOffsetMask, 0x10000);
		
	// 00[4:0] = Tx AGC, 00[9:5] = Rx AGC (BB), 00[12:10] = Rx AGC (LNA)
    ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)(0x0+path), 0x0, bRFRegOffsetMask, 0x50bff);


	// 64[14:12] = loop back attenuation
	ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)(0x0+path), 0x64, bRFRegOffsetMask, 0x19aac);

	// 8f[14:13] = PGA2 gain
	ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)(0x0+path), 0x8f, bRFRegOffsetMask, 0x8e001);

	
	// one shot
	ODM_Write4Byte(pDM_Odm, 0xcc8+pagesel, 0x800c5599);	
	ODM_Write4Byte(pDM_Odm, 0xcc8+pagesel, 0x000c5599);	

	
	// delay 100 ms
	ODM_delay_ms(100);

	
	// read back
	ODM_Write4Byte(pDM_Odm, 0xc90+pagesel, 0x0109f018);	
	ODM_Write4Byte(pDM_Odm, 0xcb8+pagesel, 0x09000000);		
	// 可以在 d00[3:0] 中讀回, dB_sel 中所設定的 gain loss 會落在哪一個 Tx AGC 設定
	// 讀回d00[3:0] = h'1 ' Tx AGC = 15
	// 讀回d00[3:0] = h'2 ' Tx AGC = 16
	// 讀回d00[3:0] = h'3 ' Tx AGC = 17
	// 讀回d00[3:0] = h'4 ' Tx AGC = 18
	// 讀回d00[3:0] = h'5 ' Tx AGC = 19
	// 讀回d00[3:0] = h'6 ' Tx AGC = 1a
	// 讀回d00[3:0] = h'7 ' Tx AGC = 1b
	// 讀回d00[3:0] = h'8 ' Tx AGC = 1c
	// 讀回d00[3:0] = h'9 ' Tx AGC = 1d
	// 讀回d00[3:0] = h'a ' Tx AGC = 1e
	// 讀回d00[3:0] = h'b ' Tx AGC = 1f

	TXindex = ODM_GetBBReg(pDM_Odm, 0xd00+regsel, 0x0000000f);


	//***************************************//
	//get LUT
	//***************************************//

	ODM_Write4Byte(pDM_Odm, 0xc90+pagesel, 0x0001e038);	
	ODM_Write4Byte(pDM_Odm, 0xc94+pagesel, 0xf76c9f84);	
	ODM_Write4Byte(pDM_Odm, 0xcc8+pagesel, 0x400c5599);	
	ODM_Write4Byte(pDM_Odm, 0xcc4+pagesel, 0x11930080);		//0xcc4[9:4]= DPk fail threshold
	ODM_Write4Byte(pDM_Odm, 0xc8c+pagesel, 0x3c000000);	

	
	// tx_amp ' 決定 Ramp 中各弦波的振幅大小

	ODM_Write4Byte(pDM_Odm, 0xc98+pagesel, 0x41382e21);	
	ODM_Write4Byte(pDM_Odm, 0xc9c+pagesel, 0x5b554f48);	
	ODM_Write4Byte(pDM_Odm, 0xca0+pagesel, 0x6f6b6661);	
	ODM_Write4Byte(pDM_Odm, 0xca4+pagesel, 0x817d7874);	
	ODM_Write4Byte(pDM_Odm, 0xca8+pagesel, 0x908c8884);	
	ODM_Write4Byte(pDM_Odm, 0xcac+pagesel, 0x9d9a9793);	
	ODM_Write4Byte(pDM_Odm, 0xcb0+pagesel, 0xaaa7a4a1);	
	ODM_Write4Byte(pDM_Odm, 0xcb4+pagesel, 0xb6b3b0ad);	
	
	// tx_inverse ' Ramp 中各弦波power 的倒數, 以計算出 PA 的 gain 
	ODM_Write4Byte(pDM_Odm, 0xc40+pagesel, 0x02ce03e9);	
	ODM_Write4Byte(pDM_Odm, 0xc44+pagesel, 0x01fd0249);	
	ODM_Write4Byte(pDM_Odm, 0xc48+pagesel, 0x01a101c9);	
	ODM_Write4Byte(pDM_Odm, 0xc4c+pagesel, 0x016a0181);	
	ODM_Write4Byte(pDM_Odm, 0xc50+pagesel, 0x01430155);	
	ODM_Write4Byte(pDM_Odm, 0xc54+pagesel, 0x01270135);	
	ODM_Write4Byte(pDM_Odm, 0xc58+pagesel, 0x0112011c);	
	ODM_Write4Byte(pDM_Odm, 0xc5c+pagesel, 0x01000108);	
	ODM_Write4Byte(pDM_Odm, 0xc60+pagesel, 0x00f100f8);	
	ODM_Write4Byte(pDM_Odm, 0xc64+pagesel, 0x00e500eb);	
	ODM_Write4Byte(pDM_Odm, 0xc68+pagesel, 0x00db00e0);	
	ODM_Write4Byte(pDM_Odm, 0xc6c+pagesel, 0x00d100d5);	
	ODM_Write4Byte(pDM_Odm, 0xc70+pagesel, 0x00c900cd);	
	ODM_Write4Byte(pDM_Odm, 0xc74+pagesel, 0x00c200c5);	
	ODM_Write4Byte(pDM_Odm, 0xc78+pagesel, 0x00bb00be);	
	ODM_Write4Byte(pDM_Odm, 0xc7c+pagesel, 0x00b500b8);	
	
	//fill BB TX index for the DPK reference 
	// reg82c[31] =1b'0, 切換到 page C
	ODM_Write4Byte(pDM_Odm, 0x82c, 0x002083d5);	

	ODM_Write4Byte(pDM_Odm, 0xc20+pagesel, 0x3c3c3c3c);	
	ODM_Write4Byte(pDM_Odm, 0xc24+pagesel, 0x3c3c3c3c);	
	ODM_Write4Byte(pDM_Odm, 0xc28+pagesel, 0x3c3c3c3c);	
	ODM_Write4Byte(pDM_Odm, 0xc2c+pagesel, 0x3c3c3c3c);	
	ODM_Write4Byte(pDM_Odm, 0xc30+pagesel, 0x3c3c3c3c);	
	ODM_Write4Byte(pDM_Odm, 0xc34+pagesel, 0x3c3c3c3c);	
	ODM_Write4Byte(pDM_Odm, 0xc38+pagesel, 0x3c3c3c3c);	
	ODM_Write4Byte(pDM_Odm, 0xc3c+pagesel, 0x3c3c3c3c);	
	ODM_Write4Byte(pDM_Odm, 0xc40+pagesel, 0x3c3c3c3c);	
	ODM_Write4Byte(pDM_Odm, 0xc44+pagesel, 0x3c3c3c3c);	
	ODM_Write4Byte(pDM_Odm, 0xc48+pagesel, 0x3c3c3c3c);	
	ODM_Write4Byte(pDM_Odm, 0xc4c+pagesel, 0x3c3c3c3c);	

	// reg82c[31] =1b'1, 切換到 page C1
	ODM_Write4Byte(pDM_Odm, 0x82c, 0x802083d5);	

	
	
	// r_agc_boudary
	// PA gain = 11 對應 tx_agc 從1f 到11  boundary = b'11111 ' PageC1 的 bc0[4:0] = 11111
	// PA gain = 10 對應 tx_agc 從11 到11 ? boundary = b'10011 ' PageC1 的 bc0[9:5] = 10001
	// PA gain = 01 對應 tx_agc 從10 到0e ? boundary = b'10000 ' PageC1 的 bc0[14:10] = 10000
	// PA gain = 00 對應 tx_agc 從0d 到00 ? boundary = b'01101 ' PageC1 的 bc0[19:15] = 01101
	ODM_Write4Byte(pDM_Odm, 0xcbc+pagesel, 0x0006c23f);	
	
	// r_bnd, 另外4塊 PWSF (power scaling factor) 的 boundary, 因為目前只有在 PA gain = 11 時才做補償, 所以設成 h'fffff 即可.
	ODM_Write4Byte(pDM_Odm, 0xcb8+pagesel, 0x000fffff);

	//============//
	// RF setting for DPK //
	//============//
	// 00[4:0] = Tx AGC, 00[9:5] = Rx AGC (BB), 00[12:10] = Rx AGC (LNA)
	// 此處 reg00[4:0] = h'1d, 是由前面 gain loss function 得到的結果.
    ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)(0x0+path), 0x0, bRFRegOffsetMask, 0x517e0 | TXindex);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF 0x0 = 0x%x\n", 0x517e0 | TXindex));

	// one shot
	ODM_Write4Byte(pDM_Odm, 0xcc8+pagesel, 0xc00c5599);
	ODM_Write4Byte(pDM_Odm, 0xcc8+pagesel, 0x400c5599);

	// delay 100 ms
	ODM_delay_ms(100);
	
	// read back dp_fail report
	ODM_Write4Byte(pDM_Odm, 0xcb8+pagesel, 0x00000000);

	//if d00[6] = 1, DPK fail
	if(ODM_GetBBReg(pDM_Odm, 0xd00+regsel, BIT6))
		{
			//bypass DPK
			ODM_Write4Byte(pDM_Odm, 0xcc4+pagesel, 0x28848000);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path %s DPK fail!!!!!!!!!!!!!!!!!!!!!\n", (path==0)?"A":"B"));

			return;
		}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path %s DPK ok!!!!!!!!!!!!!!!!!!!!!\n", (path==0)?"A":"B"));



	//read LMS table -->debug message only 
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("read LMS\n"));
	
	for(i=0;i<8;i++){
		ODM_Write4Byte(pDM_Odm, 0xc90+pagesel, 0x0601f0b8+i);
		for(j=0;j<4;j++){
			ODM_Write4Byte(pDM_Odm, 0xcb8+pagesel, 0x09000000+(j<<24));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0x%x", ODM_Read4Byte(pDM_Odm, 0xd00+regsel)));
			}
		}
		
	 
	//***************************************//
	//get gain loss
	//***************************************//

	_DPK_GetGainLoss(pDM_Odm,path);


	//***************************************//
	//Enable DP
	//***************************************//	

	_DPK_EnableDP(pDM_Odm, path, TXindex);


}

    
}



VOID	
phy_DPCalibrate_8812A(
	IN 	PDM_ODM_T	pDM_Odm
	)
{
    u4Byte backupRegAddrs[] = {
        0x970, 0xcb8, 0x838, 0xc00, 0x90c, 0xb00, 0xc94, 0x82c, 0x520, 0xc60, // 10
        0xc64, 0xc68, 0xc6c, 0xc70, 0xc74, 0xc78, 0xc7c, 0xc80, 0xc84, 0xc50, // 20
        0xc20, 0xc24, 0xc28, 0xc2c, 0xc30, 0xc34, 0xc38, 0xc3c, 0xc40, 0xc44, // 30
        0xc48, 0xc4c, 0xe50, 0xe20, 0xe24, 0xe28, 0xe2c, 0xe30, 0xe34, 0xe38, // 40
        0xe3c, 0xe40, 0xe44, 0xe48, 0xe4c, 0xeb8, 0xe00, 0xe94, 0xe60, 0xe64, //50
        0xe68, 0xe6c, 0xe70, 0xe74, 0xe78, 0xe7c, 0xe80, 0xe84                      
    };

    u4Byte backupRegData[sizeof(backupRegAddrs)/sizeof(u4Byte)];


	//backup BB&MAC default value

	_DPK_parabackup(pDM_Odm, backupRegData, backupRegAddrs, sizeof(backupRegAddrs)/sizeof(u4Byte));

	//set global parameters
	_DPK_Globalparaset(pDM_Odm);

	//DPK
	_DPK_pathABDPK(pDM_Odm);

	// TH_DPK=thermalmeter


	//reload BB&MAC defaul value;
	_DPK_parareload(pDM_Odm, backupRegData, backupRegAddrs, sizeof(backupRegAddrs)/sizeof(u4Byte));
                                                         
}                                                        
                                                         
VOID	                                                 
PHY_DPCalibrate_8812A(                                   
	IN 	PDM_ODM_T	pDM_Odm                             
	)                                                    
{   
	pDM_Odm->DPK_Done = TRUE;
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("===> PHY_DPCalibrate_8812A\n"));
	phy_DPCalibrate_8812A(pDM_Odm);  
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("<=== PHY_DPCalibrate_8812A\n"));
}                                               
                                   
                                                          
                                                          
                                                          
                                                          
