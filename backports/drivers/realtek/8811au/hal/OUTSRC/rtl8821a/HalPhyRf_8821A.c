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

//#include "Mp_Precomp.h"
#include "../odm_precomp.h"



/*---------------------------Define Local Constant---------------------------*/
// 2010/04/25 MH Define the max tx power tracking tx agc power.
#define		ODM_TXPWRTRACK_MAX_IDX8821A		6

/*---------------------------Define Local Constant---------------------------*/


//3 ============================================================
//3 Tx Power Tracking
//3 ============================================================


void setIqkMatrix_8821A(
	PDM_ODM_T	pDM_Odm,
	u1Byte 		OFDM_index,
	u1Byte 		RFPath,
	s4Byte 		IqkResult_X,
	s4Byte 		IqkResult_Y
	)
{
	s4Byte			ele_A=0, ele_D, ele_C=0, value32;

	ele_D = (OFDMSwingTable_New[OFDM_index] & 0xFFC00000)>>22;		
	
    //new element A = element D x X
	if((IqkResult_X != 0) && (*(pDM_Odm->pBandType) == ODM_BAND_2_4G))
	{
		if ((IqkResult_X & 0x00000200) != 0)	//consider minus
			IqkResult_X = IqkResult_X | 0xFFFFFC00;
		ele_A = ((IqkResult_X * ele_D)>>8)&0x000003FF;
			
		//new element C = element D x Y
		if ((IqkResult_Y & 0x00000200) != 0)
			IqkResult_Y = IqkResult_Y | 0xFFFFFC00;
		ele_C = ((IqkResult_Y * ele_D)>>8)&0x000003FF;

		//if (RFPath == ODM_RF_PATH_A)
		switch (RFPath)
		{
		case ODM_RF_PATH_A:
			//wirte new elements A, C, D to regC80 and regC94, element B is always 0
			value32 = (ele_D<<22)|((ele_C&0x3F)<<16)|ele_A;
			ODM_SetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, bMaskDWord, value32);

			value32 = (ele_C&0x000003C0)>>6;
			ODM_SetBBReg(pDM_Odm, rOFDM0_XCTxAFE, bMaskH4Bits, value32);

			value32 = ((IqkResult_X * ele_D)>>7)&0x01;
			ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, BIT24, value32);			
			break;
		default:
			break;
		}	
	}
	else
	{
		switch (RFPath)
		{
		case ODM_RF_PATH_A:
			ODM_SetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable_New[OFDM_index]);				
			ODM_SetBBReg(pDM_Odm, rOFDM0_XCTxAFE, bMaskH4Bits, 0x00);
			ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, BIT24, 0x00);			
			break;

		default:
			break;
		}		
	}

    ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("TxPwrTracking path B: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x 0xeb4 = 0x%x 0xebc = 0x%x\n", 
    (u4Byte)IqkResult_X, (u4Byte)IqkResult_Y, (u4Byte)ele_A, (u4Byte)ele_C, (u4Byte)ele_D, (u4Byte)IqkResult_X, (u4Byte)IqkResult_Y));				
}

void DoIQK_8821A(
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
	PHY_IQCalibrate_8821A(Adapter, FALSE);

    
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


VOID
ODM_TxPwrTrackSetPwr8821A(
	PDM_ODM_T			pDM_Odm,
	PWRTRACK_METHOD 	Method,
	u1Byte 				RFPath,
	u1Byte 				ChannelMappedIndex
	)
{
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

	u1Byte			PwrTrackingLimit = 26; //+1.0dB
	u1Byte			TxRate = 0xFF;
	u1Byte			Final_OFDM_Swing_Index = 0; 
	u1Byte			Final_CCK_Swing_Index = 0; 
	u1Byte			i = 0;
	u4Byte			finalBbSwingIdx[1];


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

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("===>ODM_TxPwrTrackSetPwr8821A\n"));

	if(TxRate != 0xFF)
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

		else			
			PwrTrackingLimit = 24;
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("TxRate=0x%x, PwrTrackingLimit=%d\n", TxRate, PwrTrackingLimit));

	if (Method == BBSWING)
	{
		if (RFPath == ODM_RF_PATH_A)
		{		
			finalBbSwingIdx[ODM_RF_PATH_A] = (pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_A] > PwrTrackingLimit) ? PwrTrackingLimit : pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_A];
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_A]=%d, pDM_Odm->RealBbSwingIdx[ODM_RF_PATH_A]=%d\n",
				pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_A], finalBbSwingIdx[ODM_RF_PATH_A]));
			
			ODM_SetBBReg(pDM_Odm, rA_TxScale_Jaguar, 0xFFE00000, TxScalingTable_Jaguar[finalBbSwingIdx[ODM_RF_PATH_A]]);
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
				pDM_Odm->Remnant_CCKSwingIdx= Final_CCK_Swing_Index - PwrTrackingLimit;
				pDM_Odm->Remnant_OFDMSwingIdx[RFPath] = Final_OFDM_Swing_Index - PwrTrackingLimit;            

				ODM_SetBBReg(pDM_Odm, rA_TxScale_Jaguar, 0xFFE00000, TxScalingTable_Jaguar[PwrTrackingLimit]);

				pDM_Odm->Modify_TxAGC_Flag_PathA= TRUE;

				PHY_SetTxPowerLevelByPath(Adapter, pHalData->CurrentChannel, ODM_RF_PATH_A);

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Path_A Over BBSwing Limit , PwrTrackingLimit = %d , Remnant TxAGC Value = %d \n", PwrTrackingLimit, pDM_Odm->Remnant_OFDMSwingIdx[RFPath]));
			}
			else if (Final_OFDM_Swing_Index < 0)
			{
				pDM_Odm->Remnant_CCKSwingIdx= Final_CCK_Swing_Index;
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
	}
	else
	{
		return;
	}
}	// odm_TxPwrTrackSetPwr88E

VOID
GetDeltaSwingTable_8821A(
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

void ConfigureTxpowerTrack_8821A(
	PTXPWRTRACK_CFG	pConfig
	)
{
	pConfig->SwingTableSize_CCK = TXSCALE_TABLE_SIZE;
	pConfig->SwingTableSize_OFDM = TXSCALE_TABLE_SIZE;
	pConfig->Threshold_IQK = IQK_THRESHOLD;
	pConfig->AverageThermalNum = AVG_THERMAL_NUM_8812A;
	pConfig->RfPathCount = MAX_PATH_NUM_8821A;
	pConfig->ThermalRegAddr = RF_T_METER_8812A;
		
	pConfig->ODM_TxPwrTrackSetPwr = ODM_TxPwrTrackSetPwr8821A;
	pConfig->DoIQK = DoIQK_8821A;
	pConfig->PHY_LCCalibrate = PHY_LCCalibrate_8821A;
	pConfig->GetDeltaSwingTable = GetDeltaSwingTable_8821A;
}

//1 7.	IQK
#define MAX_TOLERANCE		5
#define IQK_DELAY_TIME		1		//ms

void _IQK_RX_FillIQC_8821A(
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
			ODM_SetBBReg(pDM_Odm, 0xc10, 0x000003ff, RX_X>>1);
			ODM_SetBBReg(pDM_Odm, 0xc10, 0x03ff0000, RX_Y>>1);
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RX_X = %x;;RX_Y = %x ====>fill to IQC\n", RX_X>>1, RX_Y>>1));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xc10 = %x ====>fill to IQC\n", ODM_Read4Byte(pDM_Odm, 0xc10)));
		}
		break;
	default:
		break;					
	};	
}

void _IQK_TX_FillIQC_8821A(
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
			ODM_Write4Byte(pDM_Odm, 0xc90, 0x00000080);
			ODM_Write4Byte(pDM_Odm, 0xcc4, 0x20040000);
			ODM_Write4Byte(pDM_Odm, 0xcc8, 0x20000000);
			ODM_SetBBReg(pDM_Odm, 0xccc, 0x000007ff, TX_Y);
			ODM_SetBBReg(pDM_Odm, 0xcd4, 0x000007ff, TX_X);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TX_X = %x;;TX_Y = %x =====> fill to IQC\n", TX_X, TX_Y));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xcd4 = %x;;0xccc = %x ====>fill to IQC\n", ODM_GetBBReg(pDM_Odm, 0xcd4, 0x000007ff), ODM_GetBBReg(pDM_Odm, 0xccc, 0x000007ff)));
		}
		break;
	default:
		break;					
	};	
}

void _IQK_BackupMacBB_8821A(
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
void _IQK_BackupRF_8821A(
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
    	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("BackupRF Success!!!!\n"));
}
void _IQK_BackupAFE_8821A(
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
void _IQK_RestoreMacBB_8821A(
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
void _IQK_RestoreRF_8821A(
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

	switch(Path){
	case ODM_RF_PATH_A:
       {
       	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RestoreRF Path A Success!!!!\n"));
       }
		break;
	default:
		break;
	}
}
void _IQK_RestoreAFE_8821A(
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
	ODM_Write4Byte(pDM_Odm, 0xc90, 0x00000080);
	ODM_Write4Byte(pDM_Odm, 0xc94, 0x00000000);
	ODM_Write4Byte(pDM_Odm, 0xcc4, 0x20040000);
	ODM_Write4Byte(pDM_Odm, 0xcc8, 0x20000000);
	ODM_Write4Byte(pDM_Odm, 0xcb8, 0x0);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RestoreAFE Success!!!!\n"));
}


void _IQK_ConfigureMAC_8821A(
	IN PDM_ODM_T		pDM_Odm
	)
{
	// ========MAC register setting========
	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
	ODM_Write1Byte(pDM_Odm, 0x522, 0x3f);
	ODM_SetBBReg(pDM_Odm, 0x550, BIT(11)|BIT(3), 0x0);
	ODM_Write1Byte(pDM_Odm, 0x808, 0x00);		//		RX ante off
	ODM_SetBBReg(pDM_Odm, 0x838, 0xf, 0xc);		//		CCA off
	ODM_Write1Byte(pDM_Odm, 0xa07, 0xf);		//  		CCK RX Path off
}

#define cal_num 3

void _IQK_Tx_8821A(
	IN PDM_ODM_T		pDM_Odm,
	IN ODM_RF_RADIO_PATH_E Path
	)
{
	u4Byte 		TX_fail, RX_fail, delay_count, IQK_ready, cal_retry, cal = 0, temp_reg65;
	int 		TX_X = 0, TX_Y = 0, RX_X = 0, RX_Y = 0, TX_Average = 0, RX_Average = 0;
	int 		TX_X0[cal_num], TX_Y0[cal_num], TX_X0_RXK[cal_num], TX_Y0_RXK[cal_num], RX_X0[cal_num], RX_Y0[cal_num];
    	BOOLEAN 	TX0IQKOK = FALSE, RX0IQKOK = FALSE;
	BOOLEAN  	VDF_enable = FALSE;
	int 			i, k, VDF_Y[3], VDF_X[3], Tx_dt[3], Rx_dt[3], ii, dx = 0, dy = 0, TX_finish = 0, RX_finish = 0;
	

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("BandWidth = %d\n", *pDM_Odm->pBandWidth));
	if (*pDM_Odm->pBandWidth == 2){
		VDF_enable = TRUE;
	}

	while (cal < cal_num){
		switch (Path) {
			case ODM_RF_PATH_A:
			{	
				temp_reg65 = ODM_GetRFReg(pDM_Odm, Path, 0x65, bMaskDWord);
				
				if (pDM_Odm->ExtPA){
					ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
					ODM_SetRFReg(pDM_Odm, Path, 0x65, bRFRegOffsetMask, 0x931d5);
				}
				
				//Path-A LOK
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
				// ========Path-A AFE all on========
				// Port 0 DAC/ADC on
				ODM_Write4Byte(pDM_Odm, 0xc60, 0x77777777);
				ODM_Write4Byte(pDM_Odm, 0xc64, 0x77777777);

				ODM_Write4Byte(pDM_Odm, 0xc68, 0x19791979);

				ODM_SetBBReg(pDM_Odm, 0xc00, 0xf, 0x4);// 	hardware 3-wire off

				// LOK Setting
				//====== LOK ======
				// 1. DAC/ADC sampling rate (160 MHz)
				ODM_SetBBReg(pDM_Odm, 0xc5c, BIT(26)|BIT(25)|BIT(24), 0x7);

				// 2. LoK RF Setting (at BW = 20M)
				ODM_SetRFReg(pDM_Odm, Path, 0xef, bRFRegOffsetMask, 0x80002);
				ODM_SetRFReg(pDM_Odm, Path, 0x18, 0x00c00, 0x3);     // BW 20M
				ODM_SetRFReg(pDM_Odm, Path, 0x30, bRFRegOffsetMask, 0x20000);
				ODM_SetRFReg(pDM_Odm, Path, 0x31, bRFRegOffsetMask, 0x0003f);
				ODM_SetRFReg(pDM_Odm, Path, 0x32, bRFRegOffsetMask, 0xf3fc3);
				ODM_SetRFReg(pDM_Odm, Path, 0x65, bRFRegOffsetMask, 0x931d5);
				ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0x8a001);
				ODM_Write4Byte(pDM_Odm, 0x90c, 0x00008000);
				ODM_Write4Byte(pDM_Odm, 0xb00, 0x03000100);
				ODM_SetBBReg(pDM_Odm, 0xc94, BIT(0), 0x1);
				ODM_Write4Byte(pDM_Odm, 0x978, 0x29002000);// TX (X,Y)
				ODM_Write4Byte(pDM_Odm, 0x97c, 0xa9002000);// RX (X,Y)
				ODM_Write4Byte(pDM_Odm, 0x984, 0x00462910);// [0]:AGC_en, [15]:idac_K_Mask

				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1

				if (pDM_Odm->ExtPA)
					ODM_Write4Byte(pDM_Odm, 0xc88, 0x821403f7);
				else
					ODM_Write4Byte(pDM_Odm, 0xc88, 0x821403f4);

				if (*pDM_Odm->pBandType)
					ODM_Write4Byte(pDM_Odm, 0xc8c, 0x68163e96);
				else
					ODM_Write4Byte(pDM_Odm, 0xc8c, 0x28163e96);

				ODM_Write4Byte(pDM_Odm, 0xc80, 0x18008c10);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
				ODM_Write4Byte(pDM_Odm, 0xc84, 0x38008c10);// RX_Tone_idx[9:0], RxK_Mask[29]
				ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00100000);// cb8[20] 將 SI/PI 使用權切給 iqk_dpk module
				ODM_Write4Byte(pDM_Odm, 0x980, 0xfa000000);
				ODM_Write4Byte(pDM_Odm, 0x980, 0xf8000000);
				
				ODM_delay_ms(10); //Delay 10ms
				ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00000000);
				
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
				ODM_SetRFReg(pDM_Odm, Path, 0x58, 0x7fe00, ODM_GetRFReg(pDM_Odm, Path, 0x8, 0xffc00)); // Load LOK
				switch (*pDM_Odm->pBandWidth)
					{
					case 1:
						{
						ODM_SetRFReg(pDM_Odm, Path, 0x18, 0x00c00, 0x1);
						}
						break;
					case 2:
						{
						ODM_SetRFReg(pDM_Odm, Path, 0x18, 0x00c00, 0x0);
						}
						break;
					default:
						break;
					
					}
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
				
				// 3. TX RF Setting
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
				ODM_SetRFReg(pDM_Odm, Path, 0xef, bRFRegOffsetMask, 0x80000);
				ODM_SetRFReg(pDM_Odm, Path, 0x30, bRFRegOffsetMask, 0x20000);
				ODM_SetRFReg(pDM_Odm, Path, 0x31, bRFRegOffsetMask, 0x0003f);
				ODM_SetRFReg(pDM_Odm, Path, 0x32, bRFRegOffsetMask, 0xf3fc3);
				ODM_SetRFReg(pDM_Odm, Path, 0x65, bRFRegOffsetMask, 0x931d5);
				ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0x8a001);
				ODM_SetRFReg(pDM_Odm, Path, 0xef, bRFRegOffsetMask, 0x00000);
				ODM_Write4Byte(pDM_Odm, 0x90c, 0x00008000);
				ODM_Write4Byte(pDM_Odm, 0xb00, 0x03000100);
				ODM_SetBBReg(pDM_Odm, 0xc94, BIT(0), 0x1);
				ODM_Write4Byte(pDM_Odm, 0x978, 0x29002000);// TX (X,Y)
				ODM_Write4Byte(pDM_Odm, 0x97c, 0xa9002000);// RX (X,Y)
				ODM_Write4Byte(pDM_Odm, 0x984, 0x0046a910);// [0]:AGC_en, [15]:idac_K_Mask

				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1

				if (pDM_Odm->ExtPA)
					ODM_Write4Byte(pDM_Odm, 0xc88, 0x821403f7);
				else
					ODM_Write4Byte(pDM_Odm, 0xc88, 0x821403f1);

				if (*pDM_Odm->pBandType)
					ODM_Write4Byte(pDM_Odm, 0xc8c, 0x40163e96);
				else
					ODM_Write4Byte(pDM_Odm, 0xc8c, 0x00163e96);

				if (VDF_enable == 1){
					DbgPrint("VDF_enable\n");
					for (k = 0;k <= 2; k++){
						switch (k){
							case 0:
								{
								ODM_Write4Byte(pDM_Odm, 0xc80, 0x18008c38);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
								ODM_Write4Byte(pDM_Odm, 0xc84, 0x38008c38);// RX_Tone_idx[9:0], RxK_Mask[29]
								ODM_SetBBReg(pDM_Odm, 0xce8, BIT(31), 0x0);
								}
								break;
							case 1:
								{
								ODM_SetBBReg(pDM_Odm, 0xc80, BIT(28), 0x0);
								ODM_SetBBReg(pDM_Odm, 0xc84, BIT(28), 0x0);
								ODM_SetBBReg(pDM_Odm, 0xce8, BIT(31), 0x0);
								}
								break;
							case 2:
								{
								ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("VDF_Y[1] = %x;;;VDF_Y[0] = %x\n", VDF_Y[1]>>21 & 0x00007ff, VDF_Y[0]>>21 & 0x00007ff));
								ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("VDF_X[1] = %x;;;VDF_X[0] = %x\n", VDF_X[1]>>21 & 0x00007ff, VDF_X[0]>>21 & 0x00007ff));
								Tx_dt[cal] = (VDF_Y[1]>>20)-(VDF_Y[0]>>20);
								Tx_dt[cal] = ((16*Tx_dt[cal])*10000/15708);
								Tx_dt[cal] = (Tx_dt[cal] >> 1 )+(Tx_dt[cal] & BIT(0));
								ODM_Write4Byte(pDM_Odm, 0xc80, 0x18008c20);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
								ODM_Write4Byte(pDM_Odm, 0xc84, 0x38008c20);// RX_Tone_idx[9:0], RxK_Mask[29]
								ODM_SetBBReg(pDM_Odm, 0xce8, BIT(31), 0x1);
								ODM_SetBBReg(pDM_Odm, 0xce8, 0x3fff0000, Tx_dt[cal] & 0x00003fff);
								}
								break;
							default:
								break;
						}
						ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00100000);// cb8[20] 將 SI/PI 使用權切給 iqk_dpk module
						cal_retry = 0;
						while(1){
							// one shot
							ODM_Write4Byte(pDM_Odm, 0x980, 0xfa000000);
							ODM_Write4Byte(pDM_Odm, 0x980, 0xf8000000);

							ODM_delay_ms(10); //Delay 10ms
							ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00000000);
							delay_count = 0;
				                    while (1){
				                    		IQK_ready = ODM_GetBBReg(pDM_Odm, 0xd00, BIT(10));
				                        	if ((~IQK_ready) || (delay_count>20)){
				                            	break;
				                        	}
								else{
				                            ODM_delay_ms(1);
				                            delay_count++;
				                        	}
				                    	}

							if (delay_count < 20){							// If 20ms No Result, then cal_retry++
					              	// ============TXIQK Check==============
								TX_fail = ODM_GetBBReg(pDM_Odm, 0xd00, BIT(12));
								
								if (~TX_fail){
									ODM_Write4Byte(pDM_Odm, 0xcb8, 0x02000000);
									VDF_X[k] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
									ODM_Write4Byte(pDM_Odm, 0xcb8, 0x04000000);
									VDF_Y[k] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
									TX0IQKOK = TRUE;
									break;
								}
								else{
									ODM_SetBBReg(pDM_Odm, 0xccc, 0x000007ff, 0x0);
									ODM_SetBBReg(pDM_Odm, 0xcd4, 0x000007ff, 0x200);
									TX0IQKOK = FALSE;
									cal_retry++;
									if (cal_retry == 10) {
										break;
									}
								}
							}
				                    else{
				                    		TX0IQKOK = FALSE;
				                        	cal_retry++;
				                        	if (cal_retry == 10){
				                            	break;
				                       	}
				                    }
				       	}
					}
					if (k == 3){
						TX_X0[cal] = VDF_X[k-1] ;
						TX_Y0[cal] = VDF_Y[k-1];
					}
				}
				else{
					ODM_Write4Byte(pDM_Odm, 0xc80, 0x18008c10);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
					ODM_Write4Byte(pDM_Odm, 0xc84, 0x38008c10);// RX_Tone_idx[9:0], RxK_Mask[29]
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00100000);// cb8[20] 將 SI/PI 使用權切給 iqk_dpk module
					cal_retry = 0;
					while(1){
						// one shot
						ODM_Write4Byte(pDM_Odm, 0x980, 0xfa000000);
						ODM_Write4Byte(pDM_Odm, 0x980, 0xf8000000);

						ODM_delay_ms(10); //Delay 10ms
						ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00000000);
						delay_count = 0;
						while (1){
							IQK_ready = ODM_GetBBReg(pDM_Odm, 0xd00, BIT(10));
							if ((~IQK_ready) || (delay_count>20)) {
						       	break;
						}
						else{
							ODM_delay_ms(1);
						       delay_count++;
						}
						}

						if (delay_count < 20){							// If 20ms No Result, then cal_retry++
					       // ============TXIQK Check==============
						TX_fail = ODM_GetBBReg(pDM_Odm, 0xd00, BIT(12));
								
							if (~TX_fail){
								ODM_Write4Byte(pDM_Odm, 0xcb8, 0x02000000);
								TX_X0[cal] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
								ODM_Write4Byte(pDM_Odm, 0xcb8, 0x04000000);
								TX_Y0[cal] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
								TX0IQKOK = TRUE;
								break;
							}
							else{
								ODM_SetBBReg(pDM_Odm, 0xccc, 0x000007ff, 0x0);
								ODM_SetBBReg(pDM_Odm, 0xcd4, 0x000007ff, 0x200);
								TX0IQKOK = FALSE;
								cal_retry++;
								if (cal_retry == 10) {
									break;
								}
							}
						}
		                    		else{
		                        		TX0IQKOK = FALSE;
		                        		cal_retry++;
		                        		if (cal_retry == 10)
		                            		break;	
		                    		}
		                	}
				}
				

			if (TX0IQKOK == FALSE)
				break;				// TXK fail, Don't do RXK
			
			if (VDF_enable == 1){
				ODM_SetBBReg(pDM_Odm, 0xce8, BIT(31), 0x0);    // TX VDF Disable
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RXVDF Start\n"));
				for (k = 0;k <= 2; k++){
					//====== RX mode TXK (RXK Step 1) ======
					ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
					// 1. TX RF Setting
					ODM_SetRFReg(pDM_Odm, Path, 0xef, bRFRegOffsetMask, 0x80000);
					ODM_SetRFReg(pDM_Odm, Path, 0x30, bRFRegOffsetMask, 0x30000);
					ODM_SetRFReg(pDM_Odm, Path, 0x31, bRFRegOffsetMask, 0x00029);
					ODM_SetRFReg(pDM_Odm, Path, 0x32, bRFRegOffsetMask, 0xd7ffb);
					ODM_SetRFReg(pDM_Odm, Path, 0x65, bRFRegOffsetMask, temp_reg65);
					ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0x8a001);
					ODM_SetRFReg(pDM_Odm, Path, 0xef, bRFRegOffsetMask, 0x00000);
					
					ODM_Write4Byte(pDM_Odm, 0x978, 0x29002000);// TX (X,Y)
					ODM_Write4Byte(pDM_Odm, 0x97c, 0xa9002000);// RX (X,Y)
					ODM_Write4Byte(pDM_Odm, 0x984, 0x0046a910);// [0]:AGC_en, [15]:idac_K_Mask
					ODM_Write4Byte(pDM_Odm, 0x90c, 0x00008000);
					ODM_Write4Byte(pDM_Odm, 0xb00, 0x03000100);
					ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
					switch (k){
						case 0:
							{
							ODM_Write4Byte(pDM_Odm, 0xc80, 0x18008c38);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
							ODM_Write4Byte(pDM_Odm, 0xc84, 0x38008c38);// RX_Tone_idx[9:0], RxK_Mask[29]
							ODM_SetBBReg(pDM_Odm, 0xce8, BIT(30), 0x0);
							}
							break;
						case 1:
							{
							ODM_Write4Byte(pDM_Odm, 0xc80, 0x08008c38);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
							ODM_Write4Byte(pDM_Odm, 0xc84, 0x28008c38);// RX_Tone_idx[9:0], RxK_Mask[29]
							ODM_SetBBReg(pDM_Odm, 0xce8, BIT(30), 0x0);
							}
							break;
						case 2:
							{
							ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("VDF_Y[1] = %x;;;VDF_Y[0] = %x\n", VDF_Y[1]>>21 & 0x00007ff, VDF_Y[0]>>21 & 0x00007ff));
							ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("VDF_X[1] = %x;;;VDF_X[0] = %x\n", VDF_X[1]>>21 & 0x00007ff, VDF_X[0]>>21 & 0x00007ff));
							Rx_dt[cal] = (VDF_Y[1]>>20)-(VDF_Y[0]>>20);
							ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Rx_dt = %d\n", Rx_dt[cal]));
							Rx_dt[cal] = ((16*Rx_dt[cal])*10000/13823);
							Rx_dt[cal] = (Rx_dt[cal] >> 1 )+(Rx_dt[cal] & BIT(0));
							ODM_Write4Byte(pDM_Odm, 0xc80, 0x18008c20);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
							ODM_Write4Byte(pDM_Odm, 0xc84, 0x38008c20);// RX_Tone_idx[9:0], RxK_Mask[29]
							ODM_SetBBReg(pDM_Odm, 0xce8, 0x00003fff, Rx_dt[cal] & 0x00003fff);
							}
							break;
						default:
							break;
					}
					ODM_Write4Byte(pDM_Odm, 0xc88, 0x821603e0);
					ODM_Write4Byte(pDM_Odm, 0xc8c, 0x68163e96);
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00100000);// cb8[20] 將 SI/PI 使用權切給 iqk_dpk module
					cal_retry = 0;
					while(1){
						// one shot
					    	ODM_Write4Byte(pDM_Odm, 0x980, 0xfa000000);
					    	ODM_Write4Byte(pDM_Odm, 0x980, 0xf8000000);

					    	ODM_delay_ms(10); //Delay 10ms
					    	ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00000000);
					    	delay_count = 0;
					    	while (1){
					   		IQK_ready = ODM_GetBBReg(pDM_Odm, 0xd00, BIT(10));
							if ((~IQK_ready)||(delay_count>20)){
								break;
							}
							else{
								ODM_delay_ms(1);
								delay_count++;
							}
						}
						
						if (delay_count < 20){							// If 20ms No Result, then cal_retry++
					      		// ============TXIQK Check==============
							TX_fail = ODM_GetBBReg(pDM_Odm, 0xd00, BIT(12));
							
							if (~TX_fail){
								ODM_Write4Byte(pDM_Odm, 0xcb8, 0x02000000);
								TX_X0_RXK[cal] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
								ODM_Write4Byte(pDM_Odm, 0xcb8, 0x04000000);
								TX_Y0_RXK[cal] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
								TX0IQKOK = TRUE;
								break;
							}
							else{
								TX0IQKOK = FALSE;
								cal_retry++;
								if (cal_retry == 10)
									break;
							}
						}
			                		else{
			                    		TX0IQKOK = FALSE;
			                    		cal_retry++;
			                    		if (cal_retry == 10)
			                        		break;
			                		}
			            	}

			            	if (TX0IQKOK == FALSE){   //If RX mode TXK fail, then take TXK Result
			                		TX_X0_RXK[cal] = TX_X0[cal];
			                		TX_Y0_RXK[cal] = TX_Y0[cal];
			                		TX0IQKOK = TRUE;
			                		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RXK Step 1 fail\n"));
			            	}

							
					//====== RX IQK ======
			            	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
					// 1. RX RF Setting
					ODM_SetRFReg(pDM_Odm, Path, 0xef, bRFRegOffsetMask, 0x80000);
					ODM_SetRFReg(pDM_Odm, Path, 0x30, bRFRegOffsetMask, 0x30000);
					ODM_SetRFReg(pDM_Odm, Path, 0x31, bRFRegOffsetMask, 0x0002f);
					ODM_SetRFReg(pDM_Odm, Path, 0x32, bRFRegOffsetMask, 0xfffbb);
					ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0x88001);
					ODM_SetRFReg(pDM_Odm, Path, 0x65, bRFRegOffsetMask, 0x931d8);
					ODM_SetRFReg(pDM_Odm, Path, 0xef, bRFRegOffsetMask, 0x00000);
					
					ODM_SetBBReg(pDM_Odm, 0x978, 0x03FF8000, (TX_X0_RXK[cal])>>21&0x000007ff);
			              ODM_SetBBReg(pDM_Odm, 0x978, 0x000007FF, (TX_Y0_RXK[cal])>>21&0x000007ff);
					ODM_SetBBReg(pDM_Odm, 0x978, BIT(31), 0x1);
					ODM_SetBBReg(pDM_Odm, 0x97c, BIT(31), 0x0);
					ODM_Write4Byte(pDM_Odm, 0x90c, 0x00008000);
					ODM_Write4Byte(pDM_Odm, 0x984, 0x0046a911);
					
					ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
					ODM_SetBBReg(pDM_Odm, 0xc80, BIT(29), 0x1); 
					ODM_SetBBReg(pDM_Odm, 0xc84, BIT(29), 0x0); 
					ODM_Write4Byte(pDM_Odm, 0xc88, 0x02140119);
					if (pDM_Odm->SupportInterface == 1)
						ODM_Write4Byte(pDM_Odm, 0xc8c, 0x28160d00);
					else
						ODM_Write4Byte(pDM_Odm, 0xc8c, 0x28161420);

					if (k==2){
						ODM_SetBBReg(pDM_Odm, 0xce8, BIT(30), 0x1);  //RX VDF Enable
						}
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00100000);// cb8[20] 將 SI/PI 使用權切給 iqk_dpk module
					
					cal_retry = 0;
					while(1){
						// one shot
						ODM_Write4Byte(pDM_Odm, 0x980, 0xfa000000);
						ODM_Write4Byte(pDM_Odm, 0x980, 0xf8000000);

						ODM_delay_ms(10); //Delay 10ms
						ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00000000);
						delay_count = 0;
						while (1){
							IQK_ready = ODM_GetBBReg(pDM_Odm, 0xd00, BIT(10));
							if ((~IQK_ready)||(delay_count>20)){
								break;
							}
							else{
								ODM_delay_ms(1);
								delay_count++;
							}
						}
							
						if (delay_count < 20){	// If 20ms No Result, then cal_retry++
							// ============RXIQK Check==============
							RX_fail = ODM_GetBBReg(pDM_Odm, 0xd00, BIT(11));
							if (RX_fail == 0){
								ODM_Write4Byte(pDM_Odm, 0xcb8, 0x06000000);
								VDF_X[k] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
								ODM_Write4Byte(pDM_Odm, 0xcb8, 0x08000000);
								VDF_Y[k] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
								RX0IQKOK = TRUE;
								break;
							}
							else{
								ODM_SetBBReg(pDM_Odm, 0xc10, 0x000003ff, 0x200>>1);
								ODM_SetBBReg(pDM_Odm, 0xc10, 0x03ff0000, 0x0>>1);
								RX0IQKOK = FALSE;
								cal_retry++;
								if (cal_retry == 10)
									break;
									
							}
						}
						else{
				                    	RX0IQKOK = FALSE;
				                    	cal_retry++;
				                    	if (cal_retry == 10)
				                     	break;
			                		}
			            	}
					
				}
				if (k == 3){
					RX_X0[cal] = VDF_X[k-1] ;
					RX_Y0[cal] = VDF_Y[k-1];
				}
				ODM_SetBBReg(pDM_Odm, 0xce8, BIT(31), 0x1);    // TX VDF Enable
			}
			else{
				//====== RX mode TXK (RXK Step 1) ======
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
				// 1. TX RF Setting
				ODM_SetRFReg(pDM_Odm, Path, 0xef, bRFRegOffsetMask, 0x80000);
				ODM_SetRFReg(pDM_Odm, Path, 0x30, bRFRegOffsetMask, 0x30000);
				ODM_SetRFReg(pDM_Odm, Path, 0x31, bRFRegOffsetMask, 0x00029);
				ODM_SetRFReg(pDM_Odm, Path, 0x32, bRFRegOffsetMask, 0xd7ffb);
				ODM_SetRFReg(pDM_Odm, Path, 0x65, bRFRegOffsetMask, temp_reg65);
				ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0x8a001);
				ODM_SetRFReg(pDM_Odm, Path, 0xef, bRFRegOffsetMask, 0x00000);
				ODM_Write4Byte(pDM_Odm, 0x90c, 0x00008000);
				ODM_Write4Byte(pDM_Odm, 0xb00, 0x03000100);
				ODM_Write4Byte(pDM_Odm, 0x984, 0x0046a910);// [0]:AGC_en, [15]:idac_K_Mask

				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
				ODM_Write4Byte(pDM_Odm, 0xc80, 0x18008c10);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
				ODM_Write4Byte(pDM_Odm, 0xc84, 0x38008c10);// RX_Tone_idx[9:0], RxK_Mask[29]
				ODM_Write4Byte(pDM_Odm, 0xc88, 0x821603e0);
				//ODM_Write4Byte(pDM_Odm, 0xc8c, 0x68163e96);
				ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00100000);// cb8[20] 將 SI/PI 使用權切給 iqk_dpk module
				cal_retry = 0;
				while(1){
					// one shot
				    	ODM_Write4Byte(pDM_Odm, 0x980, 0xfa000000);
				    	ODM_Write4Byte(pDM_Odm, 0x980, 0xf8000000);

				    	ODM_delay_ms(10); //Delay 10ms
				    	ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00000000);
				    	delay_count = 0;
				    	while (1){
				   		IQK_ready = ODM_GetBBReg(pDM_Odm, 0xd00, BIT(10));
						if ((~IQK_ready)||(delay_count>20)){
							break;
						}
						else{
							ODM_delay_ms(1);
							delay_count++;
						}
					}
					
					if (delay_count < 20){							// If 20ms No Result, then cal_retry++
				      		// ============TXIQK Check==============
						TX_fail = ODM_GetBBReg(pDM_Odm, 0xd00, BIT(12));
						
						if (~TX_fail){
							ODM_Write4Byte(pDM_Odm, 0xcb8, 0x02000000);
							TX_X0_RXK[cal] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
							ODM_Write4Byte(pDM_Odm, 0xcb8, 0x04000000);
							TX_Y0_RXK[cal] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
							TX0IQKOK = TRUE;
							break;
						}
						else{
							TX0IQKOK = FALSE;
							cal_retry++;
							if (cal_retry == 10)
								break;
						}
					}
	                    		else{
	                        		TX0IQKOK = FALSE;
	                        		cal_retry++;
	                        		if (cal_retry == 10)
	                            		break;
	                    		}
	                	}


	                	if (TX0IQKOK == FALSE){   //If RX mode TXK fail, then take TXK Result
	                    		TX_X0_RXK[cal] = TX_X0[cal];
	                    		TX_Y0_RXK[cal] = TX_Y0[cal];
	                    		TX0IQKOK = TRUE;
	                    		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("1"));
	                	}


	                	//====== RX IQK ======
	                	ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
				// 1. RX RF Setting
				ODM_SetRFReg(pDM_Odm, Path, 0xef, bRFRegOffsetMask, 0x80000);
				ODM_SetRFReg(pDM_Odm, Path, 0x30, bRFRegOffsetMask, 0x30000);
				ODM_SetRFReg(pDM_Odm, Path, 0x31, bRFRegOffsetMask, 0x0002f);
				ODM_SetRFReg(pDM_Odm, Path, 0x32, bRFRegOffsetMask, 0xfffbb);
				ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0x88001);
				ODM_SetRFReg(pDM_Odm, Path, 0x65, bRFRegOffsetMask, 0x931d8);
				ODM_SetRFReg(pDM_Odm, Path, 0xef, bRFRegOffsetMask, 0x00000);
				
				ODM_SetBBReg(pDM_Odm, 0x978, 0x03FF8000, (TX_X0_RXK[cal])>>21&0x000007ff);
	                     ODM_SetBBReg(pDM_Odm, 0x978, 0x000007FF, (TX_Y0_RXK[cal])>>21&0x000007ff);
				ODM_SetBBReg(pDM_Odm, 0x978, BIT(31), 0x1);
				ODM_SetBBReg(pDM_Odm, 0x97c, BIT(31), 0x0);
				ODM_Write4Byte(pDM_Odm, 0x90c, 0x00008000);
				ODM_Write4Byte(pDM_Odm, 0x984, 0x0046a911);
				
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1); // [31] = 1 --> Page C1
				ODM_Write4Byte(pDM_Odm, 0xc80, 0x38008c10);// TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16
				ODM_Write4Byte(pDM_Odm, 0xc84, 0x18008c10);// RX_Tone_idx[9:0], RxK_Mask[29]
				ODM_Write4Byte(pDM_Odm, 0xc88, 0x02140119);

				if (pDM_Odm->SupportInterface == 1)
					ODM_Write4Byte(pDM_Odm, 0xc8c, 0x28160d00);
				else
					ODM_Write4Byte(pDM_Odm, 0xc8c, 0x28161440);

				ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00100000);// cb8[20] 將 SI/PI 使用權切給 iqk_dpk module
				
				cal_retry = 0;
				while(1){
					// one shot
					ODM_Write4Byte(pDM_Odm, 0x980, 0xfa000000);
					ODM_Write4Byte(pDM_Odm, 0x980, 0xf8000000);

					ODM_delay_ms(10); //Delay 10ms
					ODM_Write4Byte(pDM_Odm, 0xcb8, 0x00000000);
					delay_count = 0;
					while (1){
						IQK_ready = ODM_GetBBReg(pDM_Odm, 0xd00, BIT(10));
						if ((~IQK_ready)||(delay_count>20)){
							break;
						}
						else{
							ODM_delay_ms(1);
							delay_count++;
						}
					}
						
					if (delay_count < 20){	// If 20ms No Result, then cal_retry++
						// ============RXIQK Check==============
						RX_fail = ODM_GetBBReg(pDM_Odm, 0xd00, BIT(11));
						if (RX_fail == 0){
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
							
							ODM_Write4Byte(pDM_Odm, 0xcb8, 0x06000000);
							RX_X0[cal] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
							ODM_Write4Byte(pDM_Odm, 0xcb8, 0x08000000);
							RX_Y0[cal] = ODM_GetBBReg(pDM_Odm, 0xd00, 0x07ff0000)<<21;
							RX0IQKOK = TRUE;
							break;
						}
						else{
							ODM_SetBBReg(pDM_Odm, 0xc10, 0x000003ff, 0x200>>1);
							ODM_SetBBReg(pDM_Odm, 0xc10, 0x03ff0000, 0x0>>1);
							RX0IQKOK = FALSE;
							cal_retry++;
							if (cal_retry == 10)
								break;
								
						}
					}
					else{
			                    	RX0IQKOK = FALSE;
			                    	cal_retry++;
			                    	if (cal_retry == 10)
			                     	break;
	                    		}
				}
			}	
                	if (TX0IQKOK)
                    		TX_Average++;
			if (RX0IQKOK)
				RX_Average++;
			ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0); // [31] = 0 --> Page C
			ODM_SetRFReg(pDM_Odm, Path, 0x65, bRFRegOffsetMask, temp_reg65);
		}
		break;
	default:
		break;					
		}
	cal++;
	}
	// FillIQK Result
	switch (Path){
	case ODM_RF_PATH_A:
       {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("========Path_A =======\n"));
		if (TX_Average == 0)
		    	break;
		
		for (i = 0; i < TX_Average; i++){
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, (" TX_X0_RXK[%d] = %x ;; TX_Y0_RXK[%d] = %x\n", i, (TX_X0_RXK[i])>>21&0x000007ff, i, (TX_Y0_RXK[i])>>21&0x000007ff));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TX_X0[%d] = %x ;; TX_Y0[%d] = %x\n", i, (TX_X0[i])>>21&0x000007ff, i, (TX_Y0[i])>>21&0x000007ff));
		}
		for (i = 0; i < TX_Average; i++){
			for (ii = i+1; ii <TX_Average; ii++){
				dx = (TX_X0[i]>>21) - (TX_X0[ii]>>21);
				if (dx < 3 && dx > -3){
					dy = (TX_Y0[i]>>21) - (TX_Y0[ii]>>21);
						if (dy < 3 && dy > -3){
							TX_X = ((TX_X0[i]>>21) + (TX_X0[ii]>>21))/2;
							TX_Y = ((TX_Y0[i]>>21) + (TX_Y0[ii]>>21))/2;
							TX_finish = 1;
							break;
						}
				}
			}
			if (TX_finish == 1)
				break;
		}	

		if (TX_finish == 1){
			_IQK_TX_FillIQC_8821A(pDM_Odm, Path, TX_X, TX_Y);
		}
		else{
			_IQK_TX_FillIQC_8821A(pDM_Odm, Path, 0x200, 0x0);
		}
		
		if (RX_Average == 0)
		    	break;
		
		for (i = 0; i < RX_Average; i++){
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RX_X0[%d] = %x ;; RX_Y0[%d] = %x\n", i, (RX_X0[i])>>21&0x000007ff, i, (RX_Y0[i])>>21&0x000007ff));
		}
		for (i = 0; i < RX_Average; i++){
			for (ii = i+1; ii <RX_Average; ii++){
				dx = (RX_X0[i]>>21) - (RX_X0[ii]>>21);
				if (dx < 4 && dx > -4){
					dy = (RX_Y0[i]>>21) - (RX_Y0[ii]>>21);
						if (dy < 4 && dy > -4){
							RX_X = ((RX_X0[i]>>21) + (RX_X0[ii]>>21))/2;
							RX_Y = ((RX_Y0[i]>>21) + (RX_Y0[ii]>>21))/2;
							RX_finish = 1;
							break;
						}
				}
			}
			if (RX_finish == 1)
				break;
		}	

		if (RX_finish == 1){
			_IQK_RX_FillIQC_8821A(pDM_Odm, Path, RX_X, RX_Y);
		}
		else{
			_IQK_RX_FillIQC_8821A(pDM_Odm, Path, 0x200, 0x0);
		}
		}
		break;
	default:
		break;
	}
}

#define MACBB_REG_NUM 12
#define AFE_REG_NUM 4
#define RF_REG_NUM 3

VOID
phy_IQCalibrate_By_FW_8821A(
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
phy_IQCalibrate_8821A(
	IN PDM_ODM_T		pDM_Odm
	)
{
	u4Byte	MACBB_backup[MACBB_REG_NUM], AFE_backup[AFE_REG_NUM], RFA_backup[RF_REG_NUM], RFB_backup[RF_REG_NUM];
	u4Byte 	Backup_MACBB_REG[MACBB_REG_NUM] = {0xb00, 0x520, 0x550, 0x808, 0xa04, 0x90c, 0xc00, 0xc50, 0xe00, 0xe50, 0x838, 0x82c}; 
	u4Byte 	Backup_AFE_REG[AFE_REG_NUM] = {0xc5c, 0xc60, 0xc64, 0xc68}; 
	u4Byte 	Backup_RF_REG[RF_REG_NUM] = {0x65, 0x8f, 0x0}; 
	
	_IQK_BackupMacBB_8821A(pDM_Odm, MACBB_backup, Backup_MACBB_REG, MACBB_REG_NUM);
	_IQK_BackupAFE_8821A(pDM_Odm, AFE_backup, Backup_AFE_REG, AFE_REG_NUM);
	_IQK_BackupRF_8821A(pDM_Odm, RFA_backup, RFB_backup, Backup_RF_REG, RF_REG_NUM);
	
	_IQK_ConfigureMAC_8821A(pDM_Odm);
	_IQK_Tx_8821A(pDM_Odm, ODM_RF_PATH_A);
	_IQK_RestoreRF_8821A(pDM_Odm, ODM_RF_PATH_A, Backup_RF_REG, RFA_backup, RF_REG_NUM);
	
	_IQK_RestoreAFE_8821A(pDM_Odm, AFE_backup, Backup_AFE_REG, AFE_REG_NUM);
	_IQK_RestoreMacBB_8821A(pDM_Odm, MACBB_backup, Backup_MACBB_REG, MACBB_REG_NUM);

	//_IQK_Exit_8821A(pDM_Odm);
	//_IQK_TX_CheckResult_8821A

}




#define		DP_BB_REG_NUM		7
#define		DP_RF_REG_NUM		1
#define		DP_RETRY_LIMIT		10
#define		DP_PATH_NUM		2
#define		DP_DPK_NUM		3
#define		DP_DPK_VALUE_NUM	2





VOID
PHY_IQCalibrate_8821A(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN 	bReCovery
	)
{
	u4Byte		StartTime; 
	s4Byte		ProgressingTime;

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;	
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

#if 0 //ODM_CheckPowerStatus always return TRUE currently!
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE) )
	if (ODM_CheckPowerStatus(pAdapter) == FALSE)
		return;
#endif
#endif //gtemp

	StartTime = ODM_GetCurrentTime( pDM_Odm);

#if MP_DRIVER == 1	
	if( ! (pMptCtx->bSingleTone || pMptCtx->bCarrierSuppression) )
#endif
	{
		if(pHalData->RegIQKFWOffload)
		{
			phy_IQCalibrate_By_FW_8821A(pAdapter);
		}
		else
		{
			phy_IQCalibrate_8821A(pDM_Odm);
		}
	}
	ProgressingTime = ODM_GetProgressingTime( pDM_Odm, StartTime);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK ProgressingTime = %d\n", ProgressingTime));
}


VOID
PHY_LCCalibrate_8821A(
	IN PDM_ODM_T		pDM_Odm
	)
{
	u4Byte		StartTime; 
	s4Byte		ProgressingTime;

	StartTime = ODM_GetCurrentTime( pDM_Odm);
	PHY_LCCalibrate_8812A(pDM_Odm);
	ProgressingTime = ODM_GetProgressingTime( pDM_Odm, StartTime);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("LCK ProgressingTime = %d\n", ProgressingTime));
}


