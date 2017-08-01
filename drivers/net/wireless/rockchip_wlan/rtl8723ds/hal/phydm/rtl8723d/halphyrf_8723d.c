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

#include "Mp_Precomp.h"
#include "../phydm_precomp.h"


#if (RTL8723D_SUPPORT == 1)

/*---------------------------Define Local Constant---------------------------*/
/*IQK*/
#define IQK_DELAY_TIME_8723D	10

/* 2010/04/25 MH Define the max tx power tracking tx agc power.*/
#define		ODM_TXPWRTRACK_MAX_IDX_8723D		6

#define     PATH_S1                         0 
#define     IDX_0xC94                       0
#define     IDX_0xC80                       1
#define     IDX_0xC4C                       2

#define     IDX_0xC14                       0
#define     IDX_0xCA0                       1


#define     PATH_S0                         1 
#define     IDX_0xCD0                       0
#define     IDX_0xCD4                       1

#define     IDX_0xCD8                       0
#define     IDX_0xCDC                       1

#define     KEY                             0
#define     VAL                             1



/*---------------------------Define Local Constant---------------------------*/


/* Tx Power Tracking*/


void setIqkMatrix_8723D(
		PDM_ODM_T	pDM_Odm,
		u1Byte		OFDM_index,
		u1Byte		RFPath,
		s4Byte		IqkResult_X,
		s4Byte		IqkResult_Y
	)
	{
		s4Byte			ele_A = 0, ele_D = 0, ele_C = 0, value32, tmp;
		s4Byte			ele_A_ext = 0, ele_C_ext = 0, ele_D_ext = 0;

		if (OFDM_index >= OFDM_TABLE_SIZE)
			OFDM_index = OFDM_TABLE_SIZE-1;
		else if (OFDM_index < 0)
			OFDM_index = 0;
	
		if ((IqkResult_X != 0) && (*(pDM_Odm->pBandType) == ODM_BAND_2_4G)) {
	
			/* new element D */
			ele_D = (OFDMSwingTable_New[OFDM_index] & 0xFFC00000)>>22;
			ele_D_ext = (((IqkResult_X * ele_D)>>7)&0x01);
			/* new element A */
			if ((IqkResult_X & 0x00000200) != 0)		/* consider minus */
				IqkResult_X = IqkResult_X | 0xFFFFFC00;
			ele_A = ((IqkResult_X * ele_D)>>8)&0x000003FF;
			ele_A_ext = ((IqkResult_X * ele_D)>>7) & 0x1;
			/* new element C */
			if ((IqkResult_Y & 0x00000200) != 0)
				IqkResult_Y = IqkResult_Y | 0xFFFFFC00;
			ele_C = ((IqkResult_Y * ele_D)>>8)&0x000003FF;
			ele_C_ext = ((IqkResult_Y * ele_D)>>7) & 0x1;
	
			switch (RFPath) {
			case ODM_RF_PATH_A:
				/* write new elements A, C, D to regC80, regC94, reg0xc4c, and element B is always 0 */
				/* write 0xc80 */
				value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
				ODM_SetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, bMaskDWord, value32);
				/* write 0xc94 */
				value32 = (ele_C & 0x000003C0) >> 6;
				ODM_SetBBReg(pDM_Odm, rOFDM0_XCTxAFE, bMaskH4Bits, value32);
				/* write 0xc4c */
				value32 = (ele_D_ext << 28) | (ele_A_ext << 31) | (ele_C_ext << 29);
				value32 = (ODM_GetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, bMaskDWord)&(~(BIT31|BIT29|BIT28))) | value32;
				ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, bMaskDWord, value32);
				break;
	
			case ODM_RF_PATH_B:
			/*wirte new elements A, C, D to regCd0 and regCd4, element B is always 0*/
				value32 = ele_D;
				ODM_SetBBReg(pDM_Odm, 0xCd4, 0x007FE000, value32);

				value32 = ele_C;
				ODM_SetBBReg(pDM_Odm, 0xCd4, 0x000007FE, value32);

				value32 = ele_A;
				ODM_SetBBReg(pDM_Odm, 0xCd0, 0x000007FE, value32);	

				ODM_SetBBReg(pDM_Odm, 0xCd4, BIT12, ele_D_ext);
				ODM_SetBBReg(pDM_Odm, 0xCd0, BIT0, ele_A_ext);				
				ODM_SetBBReg(pDM_Odm, 0xCd4, BIT0, ele_C_ext);
				break;
			default:
				break;
			}
		} else {
			switch (RFPath) {
			case ODM_RF_PATH_A:
				ODM_SetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable_New[OFDM_index]);
				ODM_SetBBReg(pDM_Odm, rOFDM0_XCTxAFE, bMaskH4Bits, 0x00);
				value32 = ODM_GetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, bMaskDWord)&(~(BIT31|BIT29|BIT28));
				ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, bMaskDWord, value32);
				break;
	
			case ODM_RF_PATH_B:
				/*image S1:c80 to S0:Cd0 and Cd4*/
				ODM_SetBBReg(pDM_Odm, 0xcd0, 0x000007FE, OFDMSwingTable_New[OFDM_index]&0x000003FF);
				ODM_SetBBReg(pDM_Odm, 0xcd0, 0x0007E000, (OFDMSwingTable_New[OFDM_index]&0x0000FC00)>>10);
				ODM_SetBBReg(pDM_Odm, 0xcd4, 0x0000007E, (OFDMSwingTable_New[OFDM_index]&0x003F0000)>>16);
				ODM_SetBBReg(pDM_Odm, 0xcd4, 0x007FE000, (OFDMSwingTable_New[OFDM_index]&0xFFC00000)>>22);
				
				ODM_SetBBReg(pDM_Odm, 0xcd4, 0x00000780, 0x00);

				ODM_SetBBReg(pDM_Odm, 0xcd4, BIT12, 0x0);
				ODM_SetBBReg(pDM_Odm, 0xcd4, BIT0, 0x0);
				ODM_SetBBReg(pDM_Odm, 0xcd0, BIT0, 0x0);				
				break;
			default:
				break;
			}
		}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("TxPwrTracking path %c: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x ele_A_ext = 0x%x ele_C_ext = 0x%x ele_D_ext = 0x%x\n",
					 (RFPath == ODM_RF_PATH_A ? 'A' : 'B'), (u4Byte)IqkResult_X, (u4Byte)IqkResult_Y, (u4Byte)ele_A, (u4Byte)ele_C, (u4Byte)ele_D, (u4Byte)ele_A_ext, (u4Byte)ele_C_ext, (u4Byte)ele_D_ext));
	}

VOID
setCCKFilterCoefficient_8723D(
	PDM_ODM_T	pDM_Odm,
	u1Byte 		CCKSwingIndex
)
{
	PODM_RF_CAL_T	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
	ODM_SetBBReg(pDM_Odm, 0xab4, 0x000007FF, CCKSwingTable_Ch1_Ch14_8723D[CCKSwingIndex]);
}

void DoIQK_8723D(
	PVOID		pDM_VOID,
	u1Byte 		DeltaThermalIndex,
	u1Byte		ThermalValue,	
	u1Byte 		Threshold
	)
{ 
	u4Byte  bIsBtEnable = 0;

	
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	PADAPTER 		Adapter = pDM_Odm->Adapter;
#endif


	if (pDM_Odm->mp_mode == FALSE)
		bIsBtEnable = ODM_GetMACReg(pDM_Odm, 0xa8, bMaskDWord) & BIT17; 

	if (bIsBtEnable) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Skip IQK because BT is enable\n"));
		return;
	} else {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Do IQK because BT is disable\n"));
	}

	ODM_ResetIQKResult(pDM_Odm);		

	pDM_Odm->RFCalibrateInfo.ThermalValue_IQK= ThermalValue;
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	PHY_IQCalibrate_8723D(pDM_Odm, FALSE);
#else 
	PHY_IQCalibrate_8723D(Adapter, FALSE);
#endif
 
}

/*-----------------------------------------------------------------------------
 * Function:	ODM_TxPwrTrackSetPwr_8723D()
 *
 * Overview:	8723D change all channel tx power accordign to flag.
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
ODM_TxPwrTrackSetPwr_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	PVOID		pDM_VOID,
#else
	IN PDM_ODM_T		pDM_VOID,
#endif
	PWRTRACK_METHOD 	Method,
	u1Byte 				RFPath,
	u1Byte 				ChannelMappedIndex
	)
{

	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;

	PADAPTER				Adapter = pDM_Odm->Adapter;
	PHAL_DATA_TYPE			pHalData = GET_HAL_DATA(Adapter);
	PODM_RF_CAL_T  			pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
	u1Byte					PwrTrackingLimit_OFDM = 30; 
	u1Byte					PwrTrackingLimit_CCK = 40;   
	u1Byte					TxRate = 0xFF;
	u1Byte					Final_OFDM_Swing_Index = 0; 
	u1Byte					Final_CCK_Swing_Index = 0; 
	u1Byte					i = 0;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
#if (MP_DRIVER == 1)
	PMPT_CONTEXT			pMptCtx = &(Adapter->MptCtx);

	TxRate = MptToMgntRate(pMptCtx->MptRateIndex);
#else
	PMGNT_INFO      		pMgntInfo = &(Adapter->MgntInfo);
	if (!pMgntInfo->ForcedDataRate) {
		if (pDM_Odm->TxRate != 0xFF)
			TxRate = Adapter->HalFunc.GetHwRateFromMRateHandler(pDM_Odm->TxRate); 
	} else {
		TxRate = (u1Byte) pMgntInfo->ForcedDataRate;
	}
#endif
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	if (pDM_Odm->mp_mode == TRUE) {	/*CE MP*/
		PMPT_CONTEXT		pMptCtx = &(Adapter->mppriv.MptCtx);

		TxRate = MptToMgntRate(pMptCtx->MptRateIndex);
	} else {	/*CE normal*/
		u2Byte	rate	 = *(pDM_Odm->pForcedDataRate);

		if (!rate) {	/*auto rate*/
			if (pDM_Odm->TxRate != 0xFF)
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
				TxRate = Adapter->HalFunc.GetHwRateFromMRateHandler(pDM_Odm->TxRate);
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
				if (pDM_Odm->number_linked_client != 0)
				TxRate = HwRateToMRate(pDM_Odm->TxRate);
#endif
		} else {	/*force rate*/
			TxRate = (u1Byte)rate;
		}
	}
#endif

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("===>ODM_TxPwrTrackSetPwr8723DA\n"));

	if (TxRate != 0xFF) {
		/*CCK*/
		if (((TxRate >= MGN_1M) && (TxRate <= MGN_5_5M)) || TxRate == MGN_11M)
			PwrTrackingLimit_CCK = 40;  
		/*OFDM*/
		else if ((TxRate >= MGN_6M) && (TxRate <= MGN_48M))
			PwrTrackingLimit_OFDM = 36; 
		else if (TxRate == MGN_54M)
			PwrTrackingLimit_OFDM = 34; 
			
		/* HT*/
		else if ((TxRate >= MGN_MCS0) && (TxRate <= MGN_MCS2))
			PwrTrackingLimit_OFDM = 38; 
		else if ((TxRate >= MGN_MCS3) && (TxRate <= MGN_MCS4)) 
			PwrTrackingLimit_OFDM = 36; 
		else if ((TxRate >= MGN_MCS5) && (TxRate <= MGN_MCS7)) 
			PwrTrackingLimit_OFDM = 34; 

		else if ((TxRate >= MGN_MCS8) && (TxRate <= MGN_MCS10))
			PwrTrackingLimit_OFDM = 38; 
		else if ((TxRate >= MGN_MCS11) && (TxRate <= MGN_MCS12)) 
			PwrTrackingLimit_OFDM = 36; 
		else if ((TxRate >= MGN_MCS13) && (TxRate <= MGN_MCS15)) 
			PwrTrackingLimit_OFDM = 34; 

		else	
				PwrTrackingLimit_OFDM =  pRFCalibrateInfo->DefaultOfdmIndex;   /*Default OFDM index = 30 */
	}
			
	if (Method == TXAGC) {
		u1Byte	rf = 0;
		u4Byte 	pwr = 0, TxAGC = 0;
		PADAPTER Adapter = pDM_Odm->Adapter;

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("odm_TxPwrTrackSetPwr_8723D CH=%d\n", *(pDM_Odm->pChannel)));

		pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath] = pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath];   //Remnant index equal to aboslute compensate value.
			
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE ))

#if (MP_DRIVER != 1)
		/*PHY_SetTxPowerLevelByPath8723D(Adapter, pHalData->CurrentChannel, RFPath);   //Using new set power function
		//PHY_SetTxPowerLevel8723D(pDM_Odm->Adapter, *pDM_Odm->pChannel);*/
		pRFCalibrateInfo->Modify_TxAGC_Flag_PathA = TRUE;
		pRFCalibrateInfo->Modify_TxAGC_Flag_PathB = TRUE;
		pRFCalibrateInfo->Modify_TxAGC_Flag_PathA_CCK = TRUE;
		if (RFPath == ODM_RF_PATH_A) {
			PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, CCK );
			PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, OFDM );
			PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, HT_MCS0_MCS7 );	
			PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, HT_MCS8_MCS15 );
		} else {
			PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, CCK );
			PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, OFDM );
			PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, HT_MCS0_MCS7 );	
			PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, HT_MCS8_MCS15 );
		}
#else

		if (RFPath == ODM_RF_PATH_A) {
			/*CCK Path S1*/
			pwr = PHY_QueryBBReg(Adapter, rTxAGC_A_Rate18_06, 0xFF);
		    pwr += pRFCalibrateInfo->PowerIndexOffset[ODM_RF_PATH_A];
		    PHY_SetBBReg(Adapter, rTxAGC_A_CCK1_Mcs32, bMaskByte1, pwr); 
			TxAGC = (pwr<<16)|(pwr<<8)|(pwr);
		    PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, 0x00ffffff, TxAGC);
			RT_DISP(FPHY, PHY_TXPWR, ("ODM_TxPwrTrackSetPwr_8723D: CCK Tx-rf(A) Power = 0x%x\n", TxAGC));

			/*OFDM Path S1*/
			pwr = PHY_QueryBBReg(Adapter, rTxAGC_A_Rate18_06, 0xFF);
			pwr += (pRFCalibrateInfo->BbSwingIdxOfdm[ODM_RF_PATH_A] - pRFCalibrateInfo->BbSwingIdxOfdmBase[ODM_RF_PATH_A]);
		    TxAGC = ((pwr<<24)|(pwr<<16)|(pwr<<8)|pwr);
		    PHY_SetBBReg(Adapter, rTxAGC_A_Rate18_06, bMaskDWord, TxAGC);
		    PHY_SetBBReg(Adapter, rTxAGC_A_Rate54_24, bMaskDWord, TxAGC);
		    PHY_SetBBReg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskDWord, TxAGC);
		    PHY_SetBBReg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskDWord, TxAGC);
		    PHY_SetBBReg(Adapter, rTxAGC_A_Mcs11_Mcs08, bMaskDWord, TxAGC);
		    PHY_SetBBReg(Adapter, rTxAGC_A_Mcs15_Mcs12, bMaskDWord, TxAGC);
			RT_DISP(FPHY, PHY_TXPWR, ("ODM_TxPwrTrackSetPwr_8723D: OFDM Tx-rf(A) Power = 0x%x\n", TxAGC));
		} else if (RFPath == ODM_RF_PATH_B) {
			
			pwr = PHY_QueryBBReg(Adapter, rTxAGC_B_Rate18_06, 0xFF);
		    pwr += pRFCalibrateInfo->PowerIndexOffset[ODM_RF_PATH_B];
			PHY_SetBBReg(Adapter, rTxAGC_B_CCK1_55_Mcs32, bMaskByte3, pwr);
			PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, 0xff000000, pwr);
			RT_DISP(FPHY, PHY_TXPWR, ("ODM_TxPwrTrackSetPwr_8723D: CCK Tx-rf(B) Power = 0x%x\n", pwr));		

			
			pwr = PHY_QueryBBReg(Adapter, rTxAGC_B_Rate18_06, 0xFF);
			pwr += (pRFCalibrateInfo->BbSwingIdxOfdm[ODM_RF_PATH_B] - pRFCalibrateInfo->BbSwingIdxOfdmBase[ODM_RF_PATH_B]);
		    TxAGC = ((pwr<<24)|(pwr<<16)|(pwr<<8)|pwr);
		    PHY_SetBBReg(Adapter, rTxAGC_B_Rate18_06, bMaskDWord, TxAGC);
		    PHY_SetBBReg(Adapter, rTxAGC_B_Rate54_24, bMaskDWord, TxAGC);
		    PHY_SetBBReg(Adapter, rTxAGC_B_Mcs03_Mcs00, bMaskDWord, TxAGC);
		    PHY_SetBBReg(Adapter, rTxAGC_B_Mcs07_Mcs04, bMaskDWord, TxAGC);
		    PHY_SetBBReg(Adapter, rTxAGC_B_Mcs11_Mcs08, bMaskDWord, TxAGC);
		    PHY_SetBBReg(Adapter, rTxAGC_B_Mcs15_Mcs12, bMaskDWord, TxAGC);
			RT_DISP(FPHY, PHY_TXPWR, ("ODM_TxPwrTrackSetPwr_8723D: OFDM Tx-rf(B) Power = 0x%x\n", TxAGC));
		}
#endif
		
#endif
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
		/*PHY_RF6052SetCCKTxPower(pDM_Odm->priv, *(pDM_Odm->pChannel));
		//PHY_RF6052SetOFDMTxPower(pDM_Odm->priv, *(pDM_Odm->pChannel));*/
#endif
		
	} else if (Method == BBSWING) {
		Final_OFDM_Swing_Index = pRFCalibrateInfo->DefaultOfdmIndex + pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath];
		Final_CCK_Swing_Index = pRFCalibrateInfo->DefaultCckIndex + pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath];

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					 (" pRFCalibrateInfo->DefaultOfdmIndex=%d,  pRFCalibrateInfo->DefaultCCKIndex=%d, pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath]=%d, pRFCalibrateInfo->Remnant_CCKSwingIdx=%d   RF_Path = %d\n",
					  pRFCalibrateInfo->DefaultOfdmIndex, pRFCalibrateInfo->DefaultCckIndex, pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath], pRFCalibrateInfo->Remnant_CCKSwingIdx, RFPath));

		/* Adjust BB swing by OFDM IQ matrix */
		if (Final_OFDM_Swing_Index >= PwrTrackingLimit_OFDM)
			Final_OFDM_Swing_Index = PwrTrackingLimit_OFDM;
		else if (Final_OFDM_Swing_Index < 0)
			Final_OFDM_Swing_Index = 0;

		if (Final_CCK_Swing_Index >= CCK_TABLE_SIZE_8723D)
			Final_CCK_Swing_Index = CCK_TABLE_SIZE_8723D-1;
		else if (pRFCalibrateInfo->BbSwingIdxCck < 0)
			Final_CCK_Swing_Index = 0;

		setIqkMatrix_8723D(pDM_Odm, Final_OFDM_Swing_Index, ODM_RF_PATH_A,
					   pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][0],
					   pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][1]);

		setIqkMatrix_8723D(pDM_Odm, Final_OFDM_Swing_Index, ODM_RF_PATH_B,
					   pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][4],
					   pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][5]);
		
		setCCKFilterCoefficient_8723D(pDM_Odm, Final_CCK_Swing_Index);

		pRFCalibrateInfo->Modify_TxAGC_Flag_PathA = TRUE;
		
		PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, CCK);
		PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, OFDM);
		PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, HT_MCS0_MCS7);
		
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("Final_CCK_Swing_Index=%d\n", Final_CCK_Swing_Index));
			
	} else if (Method == MIX_MODE) {
		#if (MP_DRIVER == 1)
			u4Byte	TxAGC = 0;			  /*add by Mingzhi.Guo 2015-04-10*/
			s4Byte	pwr = 0;
		#endif
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("pDM_Odm->DefaultOfdmIndex=%d,  pDM_Odm->DefaultCCKIndex=%d, pDM_Odm->Absolute_OFDMSwingIdx[RFPath]=%d, RF_Path = %d\n",
			pRFCalibrateInfo->DefaultOfdmIndex, pRFCalibrateInfo->DefaultCckIndex, pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath], RFPath));
			
		Final_OFDM_Swing_Index = pRFCalibrateInfo->DefaultOfdmIndex + pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath];
			
		if (RFPath == ODM_RF_PATH_A) {
			Final_CCK_Swing_Index = pRFCalibrateInfo->DefaultCckIndex + pRFCalibrateInfo->Absolute_OFDMSwingIdx[RFPath];    /*CCK Follow Path-A and lower CCK index means higher power.*/
			
			if (Final_OFDM_Swing_Index > PwrTrackingLimit_OFDM) {
				pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath] = Final_OFDM_Swing_Index - PwrTrackingLimit_OFDM;            

				setIqkMatrix_8723D(pDM_Odm, PwrTrackingLimit_OFDM, ODM_RF_PATH_A, 
					pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][0],
					pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][1]);
				setIqkMatrix_8723D(pDM_Odm, PwrTrackingLimit_OFDM, ODM_RF_PATH_B,
					pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][4],
					pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][5]);

				pRFCalibrateInfo->Modify_TxAGC_Flag_PathA = TRUE;

				/*Set TxAGC Page C{};*/
				/*PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, OFDM);*/
			/*	PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, HT_MCS0_MCS7);*/

				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A Over BBSwing Limit , PwrTrackingLimit = %d , Remnant TxAGC Value = %d \n", PwrTrackingLimit_OFDM, pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath]));
			} else if (Final_OFDM_Swing_Index < 0) {
				pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath] = Final_OFDM_Swing_Index ;     

				setIqkMatrix_8723D(pDM_Odm, 0, ODM_RF_PATH_A, 
					pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][0],
					pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][1]);		
				setIqkMatrix_8723D(pDM_Odm, 0, ODM_RF_PATH_B,
					 pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][4],
					 pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][5]);

				pRFCalibrateInfo->Modify_TxAGC_Flag_PathA = TRUE;

				/*Set TxAGC Page C{};*/
				/*PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, OFDM);*/
			/*	PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, HT_MCS0_MCS7);*/

				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A Lower then BBSwing lower bound  0 , Remnant TxAGC Value = %d \n", pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath]));
			} else {
			    setIqkMatrix_8723D(pDM_Odm, Final_OFDM_Swing_Index, ODM_RF_PATH_A, 
					pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][0],
					pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][1]);		
				setIqkMatrix_8723D(pDM_Odm, Final_OFDM_Swing_Index, ODM_RF_PATH_B,
					pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][4],
					pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][5]);

				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A Compensate with BBSwing , Final_OFDM_Swing_Index = %d \n", Final_OFDM_Swing_Index));

				if (pRFCalibrateInfo->Modify_TxAGC_Flag_PathA) {
					pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath] = 0;     

					/*Set TxAGC Page C{};*/
				/*	PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, OFDM );
					PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, HT_MCS0_MCS7 );
					PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, HT_MCS8_MCS15 );*/

					pRFCalibrateInfo->Modify_TxAGC_Flag_PathA = FALSE;

					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A pDM_Odm->Modify_TxAGC_Flag = FALSE \n"));
				}
			}
#if (MP_DRIVER == 1)
			if ((pDM_Odm->mp_mode) == 1) {
				pwr = PHY_QueryBBReg(Adapter, rTxAGC_A_Rate18_06, 0xFF);
				pwr += (pRFCalibrateInfo->Remnant_OFDMSwingIdx[ODM_RF_PATH_A] - pRFCalibrateInfo->Modify_TxAGC_Value_OFDM);
	
				if (pwr > 0x3F)					
				pwr = 0x3F;				
				else if (pwr < 0)				
				pwr = 0;
	
				TxAGC |= ((pwr<<24)|(pwr<<16)|(pwr<<8)|pwr);
				PHY_SetBBReg(Adapter, rTxAGC_A_Rate18_06, bMaskDWord, TxAGC);
				PHY_SetBBReg(Adapter, rTxAGC_A_Rate54_24, bMaskDWord, TxAGC);
				PHY_SetBBReg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskDWord, TxAGC);
				PHY_SetBBReg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskDWord, TxAGC);
			
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("ODM_TxPwrTrackSetPwr8188F: OFDM Tx-rf(A) Power = 0x%x\n", TxAGC));
	
			} else
#endif
			{
			
				PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, OFDM);
				PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, HT_MCS0_MCS7);
			}
			pRFCalibrateInfo->Modify_TxAGC_Value_OFDM = pRFCalibrateInfo->Remnant_OFDMSwingIdx[ODM_RF_PATH_A] ;		 
				
			if (Final_CCK_Swing_Index > PwrTrackingLimit_CCK) {
				pRFCalibrateInfo->Remnant_CCKSwingIdx = Final_CCK_Swing_Index - PwrTrackingLimit_CCK;

				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A CCK Over Limit , PwrTrackingLimit_CCK = %d , pDM_Odm->Remnant_CCKSwingIdx  = %d \n", PwrTrackingLimit_CCK, pRFCalibrateInfo->Remnant_CCKSwingIdx));

				/* Adjust BB swing by CCK filter coefficient*/
				ODM_SetBBReg(pDM_Odm, 0xab4, 0x000007FF, CCKSwingTable_Ch1_Ch14_8723D[PwrTrackingLimit_CCK]);
				
				pRFCalibrateInfo->Modify_TxAGC_Flag_PathA_CCK = TRUE;

				/*Set TxAGC Page C{};*/
			/*	PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, CCK);
				PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, CCK);*/
					
			} else if (Final_CCK_Swing_Index < 0) {
				pRFCalibrateInfo->Remnant_CCKSwingIdx = Final_CCK_Swing_Index;

				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A CCK Under Limit , PwrTrackingLimit_CCK = %d , pDM_Odm->Remnant_CCKSwingIdx  = %d \n", 0, pRFCalibrateInfo->Remnant_CCKSwingIdx));
				
				ODM_SetBBReg(pDM_Odm, 0xab4, 0x000007FF, CCKSwingTable_Ch1_Ch14_8723D[0]);
	
				pRFCalibrateInfo->Modify_TxAGC_Flag_PathA_CCK = TRUE;

				
				/*PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, CCK);
				PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, CCK);*/

			} else {
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A CCK Compensate with BBSwing , Final_CCK_Swing_Index = %d \n", Final_CCK_Swing_Index));	

				ODM_SetBBReg(pDM_Odm, 0xab4, 0x000007FF, CCKSwingTable_Ch1_Ch14_8723D[Final_CCK_Swing_Index]);
				
			/*	if (pRFCalibrateInfo->Modify_TxAGC_Flag_PathA_CCK) {*/
					pRFCalibrateInfo->Remnant_CCKSwingIdx = 0;     

					
					/*PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, CCK );
					PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, CCK );*/

					pRFCalibrateInfo->Modify_TxAGC_Flag_PathA_CCK = FALSE;

					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A pDM_Odm->Modify_TxAGC_Flag_CCK = FALSE \n"));
				}
#if (MP_DRIVER == 1)
			if ((pDM_Odm->mp_mode) == 1) {
				pwr = PHY_QueryBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte1);  
				pwr += pRFCalibrateInfo->Remnant_CCKSwingIdx-pRFCalibrateInfo->Modify_TxAGC_Value_CCK;

				if (pwr > 0x3F)					
				pwr = 0x3F;				
				else if (pwr < 0)				
				pwr = 0;

				PHY_SetBBReg(Adapter, rTxAGC_A_CCK1_Mcs32, bMaskByte1, pwr);			  
				TxAGC = (pwr<<16)|(pwr<<8)|(pwr);
				PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, 0xffffff00, TxAGC); 			
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("ODM_TxPwrTrackSetPwr8723D: CCK Tx-rf(A) Power = 0x%x\n", TxAGC));
			} else
#endif
			
				PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_A, pHalData->CurrentChannel, CCK);

			pRFCalibrateInfo->Modify_TxAGC_Value_CCK = pRFCalibrateInfo->Remnant_CCKSwingIdx;
	}
#if 0
		if (RFPath == ODM_RF_PATH_B) {		
			if (Final_OFDM_Swing_Index > PwrTrackingLimit_OFDM) {			
				pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath] = Final_OFDM_Swing_Index - PwrTrackingLimit_OFDM;            

				setIqkMatrix_8723D(pDM_Odm, PwrTrackingLimit_OFDM, ODM_RF_PATH_B,
					 pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][4],
					 pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][5]);	

				pRFCalibrateInfo->Modify_TxAGC_Flag_PathA = TRUE;

				
				/*PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, OFDM);
				PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, HT_MCS0_MCS7);*/

				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_B Over BBSwing Limit , PwrTrackingLimit = %d , Remnant TxAGC Value = %d \n", PwrTrackingLimit_OFDM, pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath]));
			} else if (Final_OFDM_Swing_Index < 0) {
				pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath] = Final_OFDM_Swing_Index ;     

				setIqkMatrix_8723D(pDM_Odm, 0, ODM_RF_PATH_B,
					 pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][4],
					 pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][5]);	

				pRFCalibrateInfo->Modify_TxAGC_Flag_PathA = TRUE;

				
				/*PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, OFDM);
				PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, HT_MCS0_MCS7);*/

				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_B Lower then BBSwing lower bound  0 , Remnant TxAGC Value = %d \n", pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath]));
			} else {
				setIqkMatrix_8723D(pDM_Odm, Final_OFDM_Swing_Index, ODM_RF_PATH_B,
					 pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][4],
					 pRFCalibrateInfo->IQKMatrixRegSetting[ChannelMappedIndex].Value[0][5]);	

				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_B Compensate with BBSwing , Final_OFDM_Swing_Index = %d \n", Final_OFDM_Swing_Index));

				if (pRFCalibrateInfo->Modify_TxAGC_Flag_PathB) {
					pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath] = 0;     

					/*PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, OFDM);
					PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, HT_MCS0_MCS7);*/

					pRFCalibrateInfo->Modify_TxAGC_Flag_PathA = FALSE;

					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_B pDM_Odm->Modify_TxAGC_Flag = FALSE \n"));
				}
			}	
#if (MP_DRIVER == 1)
			if ((pDM_Odm->mp_mode) == 1) {
				pwr = PHY_QueryBBReg(Adapter, rTxAGC_A_Rate18_06, 0xFF);
				pwr += (pRFCalibrateInfo->Remnant_OFDMSwingIdx[ODM_RF_PATH_B] - pRFCalibrateInfo->Modify_TxAGC_Value_OFDM);
			
				if (pwr > 0x3F)					
				pwr = 0x3F;			
				else if (pwr < 0)				
				pwr = 0;
			
				TxAGC |= ((pwr<<24)|(pwr<<16)|(pwr<<8)|pwr);
				PHY_SetBBReg(Adapter, rTxAGC_A_Rate18_06, bMaskDWord, TxAGC);
				PHY_SetBBReg(Adapter, rTxAGC_A_Rate54_24, bMaskDWord, TxAGC);
				PHY_SetBBReg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskDWord, TxAGC);
				PHY_SetBBReg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskDWord, TxAGC);
				
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("ODM_TxPwrTrackSetPwr8723D: OFDM Tx-rf(A) Power = 0x%x\n", TxAGC));
			
			} else
#endif	
			{
			
				PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, OFDM);
				PHY_SetTxPowerIndexByRateSection(Adapter, ODM_RF_PATH_B, pHalData->CurrentChannel, HT_MCS0_MCS7);
			}
			pRFCalibrateInfo->Modify_TxAGC_Value_OFDM = pRFCalibrateInfo->Remnant_OFDMSwingIdx[ODM_RF_PATH_B] ;		
		}
#endif
} else {
		return;
	}	
}	

VOID
GetDeltaSwingTable_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	PVOID		pDM_VOID,
#else
	IN PDM_ODM_T		pDM_Odm,
#endif
	OUT pu1Byte 			*TemperatureUP_A,
	OUT pu1Byte 			*TemperatureDOWN_A,
	OUT pu1Byte 			*TemperatureUP_B,
	OUT pu1Byte 			*TemperatureDOWN_B	
	)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#endif
	PADAPTER		Adapter 		 = pDM_Odm->Adapter;
	PODM_RF_CAL_T	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
	HAL_DATA_TYPE	*pHalData		 = GET_HAL_DATA(Adapter);
	u1Byte			TxRate			= 0xFF;
	u1Byte			channel 		 = pHalData->CurrentChannel;

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
			if (rate != 0xFF) {
			#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
						TxRate = Adapter->HalFunc.GetHwRateFromMRateHandler(pDM_Odm->TxRate);
			#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
				if (pDM_Odm->number_linked_client != 0)
						TxRate = HwRateToMRate(pDM_Odm->TxRate);
			#endif
			}
		} else { /*force rate*/
			TxRate = (u1Byte)rate;
		}
	}
		
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("Power Tracking TxRate=0x%X\n", TxRate));

	if ( 1 <= channel && channel <= 14) {
		if (IS_CCK_RATE(TxRate)) {
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
	} else {
		*TemperatureUP_A   = (pu1Byte)DeltaSwingTableIdx_2GA_P_8188E;
		*TemperatureDOWN_A = (pu1Byte)DeltaSwingTableIdx_2GA_N_8188E;	
		*TemperatureUP_B   = (pu1Byte)DeltaSwingTableIdx_2GA_P_8188E;
		*TemperatureDOWN_B = (pu1Byte)DeltaSwingTableIdx_2GA_N_8188E;		
	}
	
	return;
}

VOID
GetDeltaSwingXtalTable_8723D(
	PVOID		pDM_VOID,
	ps1Byte		*TemperatureUP_Xtal,
	ps1Byte		*TemperatureDOWN_Xtal
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PODM_RF_CAL_T	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	*TemperatureUP_Xtal   = pRFCalibrateInfo->DeltaSwingTableXtal_P;
	*TemperatureDOWN_Xtal = pRFCalibrateInfo->DeltaSwingTableXtal_N;
}



VOID
ODM_TxXtalTrackSetXtal_8723D(
	PVOID		pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm		= (PDM_ODM_T)pDM_VOID;
	PODM_RF_CAL_T	pRFCalibrateInfo	= &(pDM_Odm->RFCalibrateInfo);
	PADAPTER		Adapter			= pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData		 = GET_HAL_DATA(Adapter);
	
	s1Byte	CrystalCap;

	
	CrystalCap = pHalData->CrystalCap & 0x3F;
	CrystalCap = CrystalCap + pRFCalibrateInfo->XtalOffset;

	if (CrystalCap < 0)
		CrystalCap = 0;
	else if (CrystalCap > 63)
		CrystalCap = 63;

	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, 
		("CrystalCap(%d)= pHalData->CrystalCap(%d) + pRFCalibrateInfo->XtalOffset(%d)\n", CrystalCap, pHalData->CrystalCap, pRFCalibrateInfo->XtalOffset));

	ODM_SetBBReg(pDM_Odm, REG_MAC_PHY_CTRL, 0xFFF000, (CrystalCap | (CrystalCap << 6)));
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, 
			("CrystalCap(0x2c)  0x%X\n", ODM_GetBBReg(pDM_Odm, REG_MAC_PHY_CTRL, 0xFFF000)));

}

void ConfigureTxpowerTrack_8723D(
	PTXPWRTRACK_CFG	pConfig
	)
{
	pConfig->SwingTableSize_CCK = CCK_TABLE_SIZE_8723D;
	pConfig->SwingTableSize_OFDM = OFDM_TABLE_SIZE;
	pConfig->Threshold_IQK = IQK_THRESHOLD;
	pConfig->AverageThermalNum = AVG_THERMAL_NUM_8723D;
	pConfig->RfPathCount = MAX_PATH_NUM_8723D;  
	pConfig->ThermalRegAddr = RF_T_METER_88E;
		
	pConfig->ODM_TxPwrTrackSetPwr = ODM_TxPwrTrackSetPwr_8723D;
	pConfig->DoIQK = DoIQK_8723D;
	pConfig->PHY_LCCalibrate = PHY_LCCalibrate_8723D;
	pConfig->GetDeltaSwingTable = GetDeltaSwingTable_8723D;
	pConfig->GetDeltaSwingXtalTable = GetDeltaSwingXtalTable_8723D;
	pConfig->ODM_TxXtalTrackSetXtal = ODM_TxXtalTrackSetXtal_8723D;
}


#define MAX_TOLERANCE		5
#define IQK_DELAY_TIME		1		

u1Byte			
phy_PathS1_IQK_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
		IN PDM_ODM_T		pDM_Odm
#else
		IN	PADAPTER	pAdapter,
#endif
		IN	BOOLEAN 	configPathS0
	)
{
		u4Byte regEAC, regE94, regE9C, tmp, Path_SEL_BB/*, regEA4*/;
		u1Byte result = 0x00, Ktime;
		u4Byte originalPath, originalGNT;
	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#endif
#endif

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("[IQK]Path S1 TXIQK!!\n"));	
		
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x67 @S1 TXIQK = 0x%x\n", ODM_GetMACReg(pDM_Odm, 0x64, bMaskByte3)));
		/*save RF path*/
		Path_SEL_BB = ODM_GetBBReg(pDM_Odm, 0x948, bMaskDWord);
		/*ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x1e6@S1 TXIQK = 0x%x\n", PlatformEFIORead1Byte(pAdapter, 0x1e6)));*/
		ODM_SetBBReg(pDM_Odm, 0x948, bMaskDWord, 0x99000000); 
			
		/*IQK setting*/		
		/*leave IQK mode*/
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);
		/* --- \A7\EF\BCgTXIQK mode table ---//*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xef, bRFRegOffsetMask, 0x80000);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x33, bRFRegOffsetMask, 0x00004);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x3e, bRFRegOffsetMask, 0x0005d);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x3f, bRFRegOffsetMask, 0xBFFE0);

		/*path-A IQK setting*/
		ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x08008c0c);	
		ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x38008c1c);	
		ODM_SetBBReg(pDM_Odm, rTx_IQK_PI_A, bMaskDWord, 0x8214019f);	
		ODM_SetBBReg(pDM_Odm, rRx_IQK_PI_A, bMaskDWord, 0x28160200);	
		ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, 0x01007c00); 	
		ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x01004800); 	 

		/*LO calibration setting*/
		ODM_SetBBReg(pDM_Odm, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);  
	
		/*PA, PAD setting*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xdf, 0x800, 0x1);   
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x56, 0x600, 0x0);   
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x56, 0x1E0, 0x3);   
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x8d, 0x1F, 0xf);	   
		
		/*LOK setting  added for 8723D*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xef, 0x10, 0x1);	
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x54, 0x1, 0x1);  
#if 1

		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, bRFRegOffsetMask, 0xe0d);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, bRFRegOffsetMask, 0x60d);
#endif

		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x1 @S1 TXIQK = 0x%x\n", ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, bRFRegOffsetMask)));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x2 @S1 TXIQK = 0x%x\n", ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, bRFRegOffsetMask)));
		
		/*enter IQK mode*/
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x808000);

#if 1
		/*backup Path & GNT value */
		originalPath = ODM_GetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, bMaskDWord);  /*save 0x70*/
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0038);
		ODM_delay_ms(1);
		originalGNT = ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord);  /*save 0x38*/
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", originalGNT));
			
		/*set GNT_WL=1/GNT_BT=1  and Path owner to WiFi for pause BT traffic*/
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, 0x0000ff00);
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc0020038);	/*0x38[15:8] = 0x77*/
		ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, BIT26, 0x1);
#endif	
	
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0054);
		ODM_delay_ms(1);
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]GNT_BT @S1 TXIQK = 0x%x\n", ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord)));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 @S1 TXIQK = 0x%x\n", ODM_GetBBReg(pDM_Odm, 0x948, bMaskDWord)));

		/*One shot, Path S1 LOK & IQK*/
		ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xfa000000);
		ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
		/* delay x ms */
		/*ODM_delay_ms(IQK_DELAY_TIME_8723D);*/

		Ktime = 0;
		while ((!ODM_GetBBReg(pDM_Odm, 0xeac, BIT26)) && Ktime < 10) {
			ODM_delay_ms(1);
			Ktime++;
		}

#if 1
		/*Restore GNT_WL/GNT_BT  and Path owner*/
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, originalGNT);
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc00f0038);
		ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, originalPath);
#endif


		/*reload RF path*/
		ODM_SetBBReg(pDM_Odm, 0x948, bMaskDWord, Path_SEL_BB);

		/*leave IQK mode*/
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);
		/*PA/PAD controlled by 0x0*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xdf, 0x800, 0x0);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, BIT0, 0x0);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, BIT0, 0x0);		
	
		/* Check failed*/
		regEAC = ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_A_2, bMaskDWord);
		regE94 = ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord);
		regE9C = ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", regEAC));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94 = 0x%x, 0xe9c = 0x%x\n", regE94, regE9C));
		/*monitor image power before & after IQK*/
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
					 ODM_GetBBReg(pDM_Odm, 0xe90, bMaskDWord), ODM_GetBBReg(pDM_Odm, 0xe98, bMaskDWord)));
	
		if (!(regEAC & BIT28) &&
		   (((regE94 & 0x03FF0000)>>16) != 0x142) &&
		   (((regE9C & 0x03FF0000)>>16) != 0x42))
	
			result |= 0x01;
		else							
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("S1 TXIQK FAIL\n"));

	
		return result;
	
	}

u1Byte			
phy_PathS1_RxIQK_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
			IN PDM_ODM_T		pDM_Odm
#else
			IN	PADAPTER	pAdapter,
#endif
			IN	BOOLEAN 	configPathS0
		)
{
		u4Byte regEAC, regE94, regE9C, regEA4, u4tmp, tmp, Path_SEL_BB;
		u1Byte result = 0x00, Ktime;
		u4Byte originalPath, originalGNT;
		
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#endif
#endif
		
		Path_SEL_BB = ODM_GetBBReg(pDM_Odm, 0x948, bMaskDWord);

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Path S1 RXIQK Step1!!\n"));
	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x67 @S1 RXIQK1 = 0x%x\n", ODM_GetMACReg(pDM_Odm, 0x64, bMaskByte3)));
		ODM_SetBBReg(pDM_Odm, 0x948, bMaskDWord, 0x99000000);		
		/*ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]0x1e6@S1 RXIQK1 = 0x%x\n", PlatformEFIORead1Byte(pAdapter, 0x1e6)));	*/	
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);	
		
		/*IQK setting*/
		ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, 0x01007c00);
		ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x01004800);
		
		/*path-A IQK setting*/
		ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x18008c1c);
		ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x38008c1c);
		ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_B, bMaskDWord, 0x38008c1c);
		ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_B, bMaskDWord, 0x38008c1c);
		
		ODM_SetBBReg(pDM_Odm, rTx_IQK_PI_A, bMaskDWord, 0x82160000);
		ODM_SetBBReg(pDM_Odm, rRx_IQK_PI_A, bMaskDWord, 0x28160000);
	
		/*LO calibration setting*/
		ODM_SetBBReg(pDM_Odm, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);
										
		/*modify RXIQK mode table*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xef, bRFRegOffsetMask, 0x80000);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x33, bRFRegOffsetMask, 0x00006);	
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x3e, bRFRegOffsetMask, 0x0005f);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x3f, bRFRegOffsetMask, 0xa7ffb);
		
		/*---------PA/PAD=0----------*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xdf, 0x800, 0x1);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x56, 0x600, 0x0);
#if 1		
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, bRFRegOffsetMask, 0xe0d);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, bRFRegOffsetMask, 0x60d);
#endif

		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x1@ Path S1 RXIQK1 = 0x%x\n", ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, bRFRegOffsetMask)));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x2@ Path S1 RXIQK1 = 0x%x\n", ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, bRFRegOffsetMask)));
		
		/*enter IQK mode*/
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x808000);

#if 1
		/*backup Path & GNT value */
		originalPath = ODM_GetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, bMaskDWord);  /*save 0x70*/
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0038);
		ODM_delay_ms(1);
		originalGNT = ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord);  /*save 0x38*/
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", originalGNT));
			
		/*set GNT_WL=1/GNT_BT=1  and Path owner to WiFi for pause BT traffic*/
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, 0x0000ff00);
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc0020038);	/*0x38[15:8] = 0x77*/
		ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, BIT26, 0x1);
#endif

		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0054);
		ODM_delay_ms(1);
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]GNT_BT @S1 RXIQK1 = 0x%x\n", ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord)));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 @S1 RXIQK1 = 0x%x\n", ODM_GetBBReg(pDM_Odm, 0x948, bMaskDWord)));

		/*One shot, Path S1 LOK & IQK*/
		ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
		ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
		
		/*delay x ms*/
		/*ODM_delay_ms(IQK_DELAY_TIME_8723D);*/
		
		Ktime = 0;
		while ((!ODM_GetBBReg(pDM_Odm, 0xeac, BIT26)) && Ktime < 10) {
			ODM_delay_ms(1);
			Ktime++;
		}
					
		
		regEAC = ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_A_2, bMaskDWord);
		regE94 = ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord);
		regE9C = ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", regEAC));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94 = 0x%x, 0xe9c = 0x%x\n", regE94, regE9C));
		/*monitor image power before & after IQK*/
		ODM_RT_TRACE(pDM_Odm , ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
						 ODM_GetBBReg(pDM_Odm, 0xe90, bMaskDWord), ODM_GetBBReg(pDM_Odm, 0xe98, bMaskDWord)));
		
		
		tmp = (regE9C & 0x03FF0000)>>16;
		if ((tmp & 0x200) > 0)
			tmp = 0x400 - tmp;
		
		if (!(regEAC & BIT28) &&
			 (((regE94 & 0x03FF0000)>>16) != 0x142) &&
			 (((regE9C & 0x03FF0000)>>16) != 0x42))
		
			result |= 0x01;
		else {							
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("S1 RXIQK STEP1 FAIL\n"));
#if 1
			/*Restore GNT_WL/GNT_BT  and Path owner*/
			ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, originalGNT);
			ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc00f0038);
			ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, originalPath);
#endif
			/*reload RF path*/
			ODM_SetBBReg(pDM_Odm, 0x948, bMaskDWord, Path_SEL_BB);
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xdf, 0x800, 0x0);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, BIT0, 0x0);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, BIT0, 0x0);			
			return result;
		}	
			
		u4tmp = 0x80007C00 | (regE94&0x3FF0000)  | ((regE9C&0x3FF0000) >> 16);
		ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, u4tmp);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe40 = 0x%x u4tmp = 0x%x\n", ODM_GetBBReg(pDM_Odm, rTx_IQK, bMaskDWord), u4tmp));
	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Path S1 RXIQK STEP2!!\n"));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x67 @S1 RXIQK2 = 0x%x\n", ODM_GetMACReg(pDM_Odm, 0x64, bMaskByte3)));
		/*ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]0x1e6@S1 RXIQK2 = 0x%x\n", PlatformEFIORead1Byte(pAdapter, 0x1e6)));	*/
		ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x01004800);
				
		ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x38008c1c);
		ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x18008c1c);
		ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_B, bMaskDWord, 0x38008c1c);
		ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_B, bMaskDWord, 0x38008c1c);
		
		ODM_SetBBReg(pDM_Odm, rTx_IQK_PI_A, bMaskDWord, 0x82170000);
		ODM_SetBBReg(pDM_Odm, rRx_IQK_PI_A, bMaskDWord, 0x28171400);
		
		/*LO calibration setting*/
		ODM_SetBBReg(pDM_Odm, rIQK_AGC_Rsp, bMaskDWord, 0x0046a8d1);
		
		
		/*modify RXIQK mode table*/
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000); 
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);				 
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x33, bRFRegOffsetMask, 0x00007);		 
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x3e, bRFRegOffsetMask, 0x0005f);		
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x3f, bRFRegOffsetMask, 0xb3fdb);		 

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("RF0x1 @S1 RXIQK2 = 0x%x\n", ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, bRFRegOffsetMask)));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("RF0x2 @S1 RXIQK2 = 0x%x\n", ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, bRFRegOffsetMask)));
		
		/*enter IQK mode*/
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x808000);

#if 1
		/*backup Path & GNT value */
		originalPath = ODM_GetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, bMaskDWord);  /*save 0x70*/
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0038);
		ODM_delay_ms(1);
		originalGNT = ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord);  /*save 0x38*/
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", originalGNT));
					
		/*set GNT_WL=1/GNT_BT=1  and Path owner to WiFi for pause BT traffic*/
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, 0x0000ff00);
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc0020038);	/*0x38[15:8] = 0x77*/
		ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, BIT26, 0x1);
#endif

		
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0054);
		ODM_delay_ms(1);
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]GNT_BT @S1 RXIQK2 = 0x%x\n", ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord)));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 @S1 RXIQK2 = 0x%x\n", ODM_GetBBReg(pDM_Odm, 0x948, bMaskDWord)));

		ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
		ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
		
	
		/*ODM_delay_ms(IQK_DELAY_TIME_8723D);*/

		Ktime = 0;
		while ((!ODM_GetBBReg(pDM_Odm, 0xeac, BIT26)) && Ktime < 10) {
			ODM_delay_ms(1);
			Ktime++;
		}						

#if 1
		/*Restore GNT_WL/GNT_BT  and Path owner*/
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, originalGNT);
		ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc00f0038);
		ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, originalPath);
#endif

		
		/*reload RF path*/
		ODM_SetBBReg(pDM_Odm, 0x948, bMaskDWord, Path_SEL_BB);

		/*leave IQK mode*/
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);
		/*	PA/PAD controlled by 0x0*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xdf, 0x800, 0x0);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, BIT0, 0x0);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, BIT0, 0x0);		
	
		regEAC = ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_A_2, bMaskDWord);
		regEA4 = ODM_GetBBReg(pDM_Odm, rRx_Power_Before_IQK_A_2, bMaskDWord);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("[IQK]0xeac = 0x%x\n", regEAC));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea4 = 0x%x, 0xeac = 0x%x\n", regEA4, regEAC));
		
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea0(before IQK)= 0x%x, 0xea8(afer IQK) = 0x%x\n",
		ODM_GetBBReg(pDM_Odm, 0xea0, bMaskDWord), ODM_GetBBReg(pDM_Odm, 0xea8, bMaskDWord)));
		
		
		tmp = (regEAC & 0x03FF0000)>>16;
		if ((tmp & 0x200) > 0)
			tmp = 0x400 - tmp;
		
		if (!(regEAC & BIT27) &&		/*if Tx is OK, check whether Rx is OK*/
			(((regEA4 & 0x03FF0000)>>16) != 0x132) &&
			(((regEAC & 0x03FF0000)>>16) != 0x36) &&
			(((regEA4 & 0x03FF0000)>>16) < 0x11a) &&
			(((regEA4 & 0x03FF0000)>>16) > 0xe6) &&
			(tmp < 0x1a))
			result |= 0x02;
		else							
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("S1 RXIQK STEP2 FAIL\n"));

		
		return result;
}
		

u1Byte				
phy_PathS0_IQK_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm
#else
	IN	PADAPTER	pAdapter
#endif
	)
{
	u4Byte regEA4, regEAC, regE94, regE9C, regE94_S0, regE9C_S0, regEA4_S0, regEAC_S0, tmp, Path_SEL_BB;
	u1Byte	result = 0x00, Ktime;
	u4Byte originalPath, originalGNT;
	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path S0 TXIQK!\n"));

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x67 @S0 TXIQK = 0x%x\n", ODM_GetMACReg(pDM_Odm, 0x64, bMaskByte3)));
	Path_SEL_BB = ODM_GetBBReg(pDM_Odm, 0x948, bMaskDWord);

	ODM_SetBBReg(pDM_Odm, 0x948, bMaskDWord, 0x99000280); /*10 od 0x948 0x1 [7] ; WL:S1 to S0;BT:S0 to S1;*/
	/*ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]0x1e6@S0 TXIQK = 0x%x\n", PlatformEFIORead1Byte(pAdapter, 0x1e6)));*/		
  
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);
	/*modify TXIQK mode table*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xee, bRFRegOffsetMask, 0x80000);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x33, bRFRegOffsetMask, 0x00004);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x3e, bRFRegOffsetMask, 0x0005d);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x3f, bRFRegOffsetMask, 0xBFFE0);

	/*path-A IQK setting*/
	ODM_SetBBReg(pDM_Odm, 0xe30, bMaskDWord, 0x08008c0c);
	ODM_SetBBReg(pDM_Odm, 0xe34, bMaskDWord, 0x38008c1c);
	ODM_SetBBReg(pDM_Odm, 0xe38, bMaskDWord, 0x8214018a);
	ODM_SetBBReg(pDM_Odm, 0xe3c, bMaskDWord, 0x28160200);
	ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, 0x01007c00); 	  
	ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x01004800); 	  

	/*LO calibration setting*/
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);

	/*PA, PAD setting*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xde, 0x800, 0x1);    
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x66, 0x600, 0x0);   
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x66, 0x1E0, 0x3);    
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x8d, 0x1F, 0xf);	  

	/*LOK setting	added for 8723D*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xee, 0x10, 0x1);	
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x64, 0x1, 0x1);
	
#if 1
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, bRFRegOffsetMask, 0xe6d);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, bRFRegOffsetMask, 0x66d);	
#endif

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("RF0x1 @S0 TXIQK = 0x%x\n", ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, bRFRegOffsetMask)));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("RF0x2 @S0 TXIQK = 0x%x\n", ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, bRFRegOffsetMask)));

	
	/*enter IQK mode*/
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x808000);

#if 1
	/*backup Path & GNT value */
	originalPath = ODM_GetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, bMaskDWord);  /*save 0x70*/
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0038);
	ODM_delay_ms(1);
	originalGNT = ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord);  /*save 0x38*/
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", originalGNT));
				
	/*set GNT_WL=1/GNT_BT=1  and Path owner to WiFi for pause BT traffic*/
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, 0x0000ff00);
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc0020038);	/*0x38[15:8] = 0x77*/
	ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, BIT26, 0x1);
#endif

	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0054);
	ODM_delay_ms(1);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]GNT_BT @S0 TXIQK = 0x%x\n", ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord)));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 @S0 TXIQK = 0x%x\n", ODM_GetBBReg(pDM_Odm, 0x948, bMaskDWord)));

	/*One shot, Path S1 LOK & IQK*/
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	/*delay x ms*/
	/*ODM_delay_ms(IQK_DELAY_TIME_8723D);*/
	
	Ktime = 0;
	while ((!ODM_GetBBReg(pDM_Odm, 0xeac, BIT26)) && Ktime < 10) {
		ODM_delay_ms(1);
		Ktime++;
	}

#if 1
	/*Restore GNT_WL/GNT_BT  and Path owner*/
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, originalGNT);
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc00f0038);
	ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, originalPath);
#endif


	/*reload RF path*/
	ODM_SetBBReg(pDM_Odm, 0x948, bMaskDWord, Path_SEL_BB);

	/*leave IQK mode*/
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);
	/*PA/PAD controlled by 0x0*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xde, 0x800, 0x0);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, BIT0, 0x0);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, BIT0, 0x0);	
	
	/* Check failed*/
		regEAC_S0 = ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_A_2, bMaskDWord);
		regE94_S0 = ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord);
		regE9C_S0 = ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac_s0 = 0x%x\n", regEAC_S0));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94_s0 = 0x%x, 0xe9c_s0 = 0x%x\n", regE94_S0, regE9C_S0));
		/*monitor image power before & after IQK*/
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90_s0(before IQK)= 0x%x, 0xe98_s0(afer IQK) = 0x%x\n",
		ODM_GetBBReg(pDM_Odm, 0xe90, bMaskDWord), ODM_GetBBReg(pDM_Odm, 0xe98, bMaskDWord)));
	
		if (!(regEAC_S0 & BIT28) &&
		   (((regE94_S0 & 0x03FF0000)>>16) != 0x142) &&
		   (((regE9C_S0 & 0x03FF0000)>>16) != 0x42))
	
			result |= 0x01;
		else							
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("S0 TXIQK FAIL\n"));
	
		return result;
}



u1Byte			
phy_PathS0_RxIQK_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN 	configPathS0
	)
{
	u4Byte regE94, regE9C, regEA4, regEAC, regE94_S0, regE9C_S0, regEA4_S0, regEAC_S0, tmp, u4tmp, Path_SEL_BB;
	u1Byte result = 0x00, Ktime;
	u4Byte originalPath, originalGNT;

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	
	Path_SEL_BB = ODM_GetBBReg(pDM_Odm, 0x948, bMaskDWord);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S0 RxIQK Step1!!\n"));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x67 @S0 RXIQK1 = 0x%x\n", ODM_GetMACReg(pDM_Odm, 0x64, bMaskByte3)));
	ODM_SetBBReg(pDM_Odm, 0x948, bMaskDWord, 0x99000280); 
	/*ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]0x1e6@S0 RXIQK1 = 0x%x\n", PlatformEFIORead1Byte(pAdapter, 0x1e6)));*/
	
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);	
	
	ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, 0x01007c00);
	ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x01004800);
	
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x18008c1c);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x38008c1c);
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_B, bMaskDWord, 0x38008c1c);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_B, bMaskDWord, 0x38008c1c);

	ODM_SetBBReg(pDM_Odm, rTx_IQK_PI_A, bMaskDWord, 0x82160000);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_PI_A, bMaskDWord, 0x28160000);

	
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);
	
	
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xee, bRFRegOffsetMask, 0x80000);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x33, bRFRegOffsetMask, 0x00006);	
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x3e, bRFRegOffsetMask, 0x0005f);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x3f, bRFRegOffsetMask, 0xa7ffb);
	
	
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xde, 0x800, 0x1);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x66, 0x600, 0x0); 

#if 1	
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, bRFRegOffsetMask, 0xe6d);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, bRFRegOffsetMask, 0x66d);	
#endif

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("RF0x1 @S0 RXIQK1 = 0x%x\n", ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, bRFRegOffsetMask)));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("RF0x2 @S0 RXIQK1 = 0x%x\n", ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, bRFRegOffsetMask)));
	
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x808000);

#if 1
	/*backup Path & GNT value */
	originalPath = ODM_GetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, bMaskDWord);  /*save 0x70*/
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0038);
	ODM_delay_ms(1);
	originalGNT = ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord);  /*save 0x38*/
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", originalGNT));
				
	/*set GNT_WL=1/GNT_BT=1  and Path owner to WiFi for pause BT traffic*/
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, 0x0000ff00);
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc0020038);	/*0x38[15:8] = 0x77*/
	ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, BIT26, 0x1);
#endif


	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0054);
	ODM_delay_ms(1);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]GNT_BT @S0 RXIQK1 = 0x%x\n", ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord)));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 @S0 RXIQK1 = 0x%x\n", ODM_GetBBReg(pDM_Odm, 0x948, bMaskDWord)));

	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	
	/*ODM_delay_ms(IQK_DELAY_TIME_8723D);*/
	
	Ktime = 0;
	while ((!ODM_GetBBReg(pDM_Odm, 0xeac, BIT26)) && Ktime < 10) {
		ODM_delay_ms(1);
		Ktime++;
	}	
	
	regEAC_S0 = ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_A_2, bMaskDWord);
	regE94_S0 = ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord);
	regE9C_S0 = ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac_s0 = 0x%x\n", regEAC_S0));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94_s0 = 0x%x, 0xe9c_s0 = 0x%x\n", regE94_S0, regE9C_S0));
	/*monitor image power before & after IQK*/
	ODM_RT_TRACE(pDM_Odm , ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90_s0(before IQK)= 0x%x, 0xe98_s0(afer IQK) = 0x%x\n",
	ODM_GetBBReg(pDM_Odm, 0xe90, bMaskDWord), ODM_GetBBReg(pDM_Odm, 0xe98, bMaskDWord)));
			
	
	tmp = (regE9C_S0 & 0x03FF0000)>>16;
		if ((tmp & 0x200) > 0)
			tmp = 0x400 - tmp;
	
		if (!(regEAC_S0 & BIT28) &&
			   (((regE94_S0 & 0x03FF0000)>>16) != 0x142) &&
			   (((regE9C_S0 & 0x03FF0000)>>16) != 0x42))
		
				result |= 0x01;
		else {							
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("S0 RXIQK STEP1 FAIL\n"));
#if 1
			/*Restore GNT_WL/GNT_BT  and Path owner*/
			ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, originalGNT);
			ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc00f0038);
			ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, originalPath);
#endif

			/*reload RF path*/
			ODM_SetBBReg(pDM_Odm, 0x948, bMaskDWord, Path_SEL_BB);			
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xde, 0x800, 0x0);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, BIT0, 0x0);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, BIT0, 0x0);		
			return result;
		}
			
	u4tmp = 0x80007C00 | (regE94_S0&0x3FF0000)  | ((regE9C_S0&0x3FF0000) >> 16);
	ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, u4tmp);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe40_s0 = 0x%x u4tmp = 0x%x\n", ODM_GetBBReg(pDM_Odm, rTx_IQK, bMaskDWord), u4tmp));

	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Path S0 RXIQK STEP2!!\n\n"));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x67 @S0 RXIQK2 = 0x%x\n", ODM_GetMACReg(pDM_Odm, 0x64, bMaskByte3)));
	/*ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]0x1e6@S0 RXIQK2 = 0x%x\n", PlatformEFIORead1Byte(pAdapter, 0x1e6)));*/
	ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x01004800);
		
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x38008c1c);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x18008c1c);
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_B, bMaskDWord, 0x38008c1c);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_B, bMaskDWord, 0x38008c1c);
	
	ODM_SetBBReg(pDM_Odm, rTx_IQK_PI_A, bMaskDWord, 0x82170000);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_PI_A, bMaskDWord, 0x28171400);
	
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Rsp, bMaskDWord, 0x0046a8d1);
	
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000); 
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xee, 0x80000, 0x1);				 
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x33, bRFRegOffsetMask, 0x00007);		  
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x3e, bRFRegOffsetMask, 0x0005f);		
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x3f, bRFRegOffsetMask, 0xb3fdb);		 

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("RF0x1 @S0 RXIQK2 = 0x%x\n", ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, bRFRegOffsetMask)));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("RF0x2 @S0 RXIQK2 = 0x%x\n", ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, bRFRegOffsetMask)));
	/*enter IQK mode*/
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x808000);

#if 1
	/*backup Path & GNT value */
	originalPath = ODM_GetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, bMaskDWord);  /*save 0x70*/
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0038);
	ODM_delay_ms(1);
	originalGNT = ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord);  /*save 0x38*/
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", originalGNT));
				
	/*set GNT_WL=1/GNT_BT=1  and Path owner to WiFi for pause BT traffic*/
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, 0x0000ff00);
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc0020038);	/*0x38[15:8] = 0x77*/
	ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, BIT26, 0x1);
#endif


	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0054);
	ODM_delay_ms(1);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]GNT_BT @S0 RXIQK2 = 0x%x\n", ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord)));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 @S0 RXIQK2 = 0x%x\n", ODM_GetBBReg(pDM_Odm, 0x948, bMaskDWord)));
	
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	
	/*ODM_delay_ms(IQK_DELAY_TIME_8723D);*/
	Ktime = 0;
	while ((!ODM_GetBBReg(pDM_Odm, 0xeac, BIT26)) && Ktime < 10) {
		ODM_delay_ms(1);
		Ktime++;
	}

#if 1
	/*Restore GNT_WL/GNT_BT  and Path owner*/
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, originalGNT);
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc00f0038);
	ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, originalPath);
#endif

	/*reload RF path*/
	ODM_SetBBReg(pDM_Odm, 0x948, bMaskDWord, Path_SEL_BB);

	/*leave IQK mode*/
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);
	
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xde, 0x800, 0x0);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2, BIT0, 0x0);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, BIT0, 0x0);	
	
	regEAC_S0 = ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_A_2, bMaskDWord);
	regEA4_S0 = ODM_GetBBReg(pDM_Odm, rRx_Power_Before_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("[IQK]0xeac_s0 = 0x%x\n", regEAC_S0));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea4_s0 = 0x%x, 0xeac_s0 = 0x%x\n", regEA4_S0, regEAC_S0));
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea0_s0(before IQK)= 0x%x, 0xea8_s0(afer IQK) = 0x%x\n",
	ODM_GetBBReg(pDM_Odm, 0xea0, bMaskDWord), ODM_GetBBReg(pDM_Odm, 0xea8, bMaskDWord)));
	
	
	tmp = (regEAC_S0 & 0x03FF0000)>>16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;
	
	if (!(regEAC_S0 & BIT27) &&		/*if Tx is OK, check whether Rx is OK*/
	   (((regEA4_S0 & 0x03FF0000)>>16) != 0x132) &&
	   (((regEAC_S0 & 0x03FF0000)>>16) != 0x36) &&
	   (((regEA4_S0 & 0x03FF0000)>>16) < 0x11a) &&
	   (((regEA4_S0 & 0x03FF0000)>>16) > 0xe6) &&
	   (tmp < 0x1a))
		result |= 0x02;
	else							
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("S0 RXIQK STEP2 FAIL\n"));

	
	return result;
}


VOID
_PHY_PathS1FillIQKMatrix_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
		IN PDM_ODM_T		pDM_Odm,
#else
		IN	PADAPTER	pAdapter,
#endif
		IN	BOOLEAN 	bIQKOK,
		IN	s4Byte		result[][8],
		IN	u1Byte		final_candidate,
		IN	BOOLEAN 	bTxOnly
	)
	{
		u4Byte	Oldval_1, X, TX1_A, reg;
		s4Byte	Y, TX1_C;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#endif
#endif
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path S1 IQ Calibration %s !\n", (bIQKOK) ? "Success" : "Failed"));
	
		if (final_candidate == 0xFF)
			return;
	
		else if (bIQKOK) {
			Oldval_1 = (ODM_GetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, bMaskDWord) >> 22) & 0x3FF;
	
			X = result[final_candidate][0];

			if ((X & 0x00000200) != 0)
				X = X | 0xFFFFFC00;
			TX1_A = (X * Oldval_1) >> 8;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("X = 0x%x, TX1_A = 0x%x, Oldval_1 0x%x\n", X, TX1_A, Oldval_1));
			ODM_SetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, 0x3FF, TX1_A);
	
			ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, BIT(31), ((X * Oldval_1 >> 7) & 0x1));
	
			Y = result[final_candidate][1];
			if ((Y & 0x00000200) != 0)
				Y = Y | 0xFFFFFC00;

			TX1_C = (Y * Oldval_1) >> 8;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Y = 0x%x, TX1_C = 0x%x\n", Y, TX1_C));
			ODM_SetBBReg(pDM_Odm, rOFDM0_XCTxAFE, 0xF0000000, ((TX1_C & 0x3C0) >> 6));
			ODM_SetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, 0x003F0000, (TX1_C & 0x3F));
	
			ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, BIT(29), ((Y * Oldval_1 >> 7) & 0x1));

			if (bTxOnly) {
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("_PHY_PathS1FillIQKMatrix_8723D only Tx OK\n"));
				return;
			}
			reg = result[final_candidate][2];
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
			if (RTL_ABS(reg , 0x100) >= 16)
				reg = 0x100;
#endif
			ODM_SetBBReg(pDM_Odm, rOFDM0_XARxIQImbalance, 0x3FF, reg);
	
			reg = result[final_candidate][3] & 0x3F;
			ODM_SetBBReg(pDM_Odm, rOFDM0_XARxIQImbalance, 0xFC00, reg);

			reg = (result[final_candidate][3] >> 6) & 0xF;
			ODM_SetBBReg(pDM_Odm, rOFDM0_RxIQExtAnta, 0xF0000000, reg);
/*			
10 os 7201 10
10 id ea4 [25:16] p
10 os 7202 10
10 od c14 VarFromTmp [9:0] p

10 os 7201 11
10 id eac [25:22] p
10 os 7202 11
10 od ca0 VarFromTmp [31:28] p

10 os 7201 12
10 id eac [21:16] p
10 os 7202 12
10 od c14 VarFromTmp [15:10] p
*/
		}
	}

VOID
_PHY_PathS0FillIQKMatrix_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN  BOOLEAN   	bIQKOK,
	IN	s4Byte		result[][8],
	IN	u1Byte		final_candidate,
	IN	BOOLEAN		bTxOnly			
	)
{
	u4Byte	Oldval_0, X, TX0_A, reg;
	s4Byte	Y, TX0_C;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);		
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S0 IQ Calibration %s !\n", (bIQKOK)?"Success":"Failed"));

	if (final_candidate == 0xFF)
		return;

	else if (bIQKOK) {
		Oldval_0 = (ODM_GetBBReg(pDM_Odm, 0xcd4, bMaskDWord) >> 13) & 0x3FF;

		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;		
		TX0_A = (X * Oldval_0) >> 8;
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("X = 0x%x, TX0_A = 0x%x, Oldval_0 0x%x\n", X, TX0_A, Oldval_0));
		ODM_SetBBReg(pDM_Odm, 0xcd0, 0x7FE, TX0_A);

		ODM_SetBBReg(pDM_Odm, 0xcd0, BIT(0), ((X * Oldval_0>>7) & 0x1));

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;		

		TX0_C = (Y * Oldval_0) >> 8;
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Y = 0x%x, TX0_C = 0x%x\n", Y, TX0_C));
		ODM_SetBBReg(pDM_Odm, 0xcd4, 0x7FE, (TX0_C & 0x3FF));
		
		ODM_SetBBReg(pDM_Odm, 0xcd4, BIT(0), ((Y * Oldval_0>>7) & 0x1));

		if (bTxOnly)
			return;

		reg = result[final_candidate][6];
		ODM_SetBBReg(pDM_Odm, 0xcd8, 0x3FF, reg);
	
		reg = result[final_candidate][7];
		ODM_SetBBReg(pDM_Odm, 0xcd8, 0x003FF000, reg);
/*		
10 os 7201 10
10 id ea4 [25:16] p
10 os 7202 10
10 od cd8 VarFromTmp [9:0] p
	
10 os 7201 11
10 id eac [25:16] p
10 os 7202 11
10 od cd8 VarFromTmp [21:12] p	
		RegE94_S1 = result[i][0];
		RegE9C_S1 = result[i][1];
		RegEA4_S1 = result[i][2];
		RegEAC_S1 = result[i][3];
		RegE94_S0 = result[i][4];
		RegE9C_S0 = result[i][5];
		RegEA4_S0 = result[i][6];
		RegEAC_S0 = result[i][7];
*/
	}
}
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
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, ("ODM_CheckPowerStatus Return TRUE, due to initadapter"));
		return	TRUE;
	}
	
	//
	//	2011/07/19 MH We can not execute tx pwoer tracking/ LLC calibrate or IQK.
	//
	Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_RF_STATE, (pu1Byte)(&rtState));	
	if(Adapter->bDriverStopped || Adapter->bDriverIsGoingToPnpSetPowerSleep || rtState == eRfOff)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, ("ODM_CheckPowerStatus Return FALSE, due to %d/%d/%d\n", 
		Adapter->bDriverStopped, Adapter->bDriverIsGoingToPnpSetPowerSleep, rtState));
		return	FALSE;
	}
*/
	return	TRUE;
}
#endif

VOID
_PHY_SaveADDARegisters_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	pu4Byte		ADDAReg,
	IN	pu4Byte		ADDABackup,
	IN	u4Byte		RegisterNum
	)
{
	u4Byte	i;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
	
	if (ODM_CheckPowerStatus(pAdapter) == FALSE)
		return;
#endif
	

	for (i = 0 ; i < RegisterNum ; i++) {
		ADDABackup[i] = ODM_GetBBReg(pDM_Odm, ADDAReg[i], bMaskDWord);
	}
}


VOID
_PHY_SaveMACRegisters_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	pu4Byte		MACReg,
	IN	pu4Byte		MACBackup
	)
{
	u4Byte	i;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	

	for (i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++) {
		MACBackup[i] = ODM_Read1Byte(pDM_Odm, MACReg[i]);		
	}
	MACBackup[i] = ODM_Read4Byte(pDM_Odm, MACReg[i]);		

}


VOID
_PHY_ReloadADDARegisters_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	pu4Byte		ADDAReg,
	IN	pu4Byte		ADDABackup,
	IN	u4Byte		RegiesterNum
	)
{
	u4Byte	i;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Reload ADDA power saving parameters !\n"));
	for (i = 0 ; i < RegiesterNum; i++) {
		ODM_SetBBReg(pDM_Odm, ADDAReg[i], bMaskDWord, ADDABackup[i]);
	}
}

VOID
_PHY_ReloadMACRegisters_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	pu4Byte		MACReg,
	IN	pu4Byte		MACBackup
	)
{
	u4Byte	i;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Reload MAC parameters !\n"));
	for (i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++) {
		ODM_Write1Byte(pDM_Odm, MACReg[i], (u1Byte)MACBackup[i]);
	}
	ODM_Write4Byte(pDM_Odm, MACReg[i], MACBackup[i]);	
}


VOID
_PHY_PathADDAOn_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	pu4Byte		ADDAReg,
	IN	BOOLEAN		isPathAOn,
	IN	BOOLEAN		is2T
	)
{
	u4Byte	pathOn;
	u4Byte	i;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif


	pathOn = isPathAOn ? 0x03c00016 : 0x03c00016;

	if (FALSE == is2T) {
		pathOn = 0x03c00016;
		ODM_SetBBReg(pDM_Odm, ADDAReg[0], bMaskDWord, 0x03c00016);
	} else {
		ODM_SetBBReg(pDM_Odm, ADDAReg[0], bMaskDWord, pathOn);
	}
	
	for (i = 1 ; i < IQK_ADDA_REG_NUM ; i++) {
		ODM_SetBBReg(pDM_Odm,ADDAReg[i], bMaskDWord, pathOn);
	}
	
}

VOID
_PHY_MACSettingCalibration_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	pu4Byte		MACReg,
	IN	pu4Byte		MACBackup	
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

/*
	ODM_Write1Byte(pDM_Odm, MACReg[i], 0x3F);

	for(i = 1 ; i < (IQK_MAC_REG_NUM - 1); i++){
		ODM_Write1Byte(pDM_Odm, MACReg[i], (u1Byte)(MACBackup[i]&(~BIT3)));
	}
	ODM_Write1Byte(pDM_Odm, MACReg[i], (u1Byte)(MACBackup[i]&(~BIT5)));	
*/

	/*ODM_SetBBReg(pDM_Odm, 0x522, bMaskByte0, 0x7f);*/
	/*ODM_SetBBReg(pDM_Odm, 0x550, bMaskByte0, 0x15);*/
	/*ODM_SetBBReg(pDM_Odm, 0x551, bMaskByte0, 0x00);*/

	ODM_SetBBReg(pDM_Odm, 0x520, 0x00ff0000, 0xff);



}

VOID
_PHY_PathAStandBy_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm
#else
	IN PADAPTER	pAdapter
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
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path-S1 standby mode!\n"));

	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);
/*	ODM_SetBBReg(pDM_Odm, 0x840, bMaskDWord, 0x00010000);*/
	ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)0x0, 0x0, bRFRegOffsetMask, 0x10000);

	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x808000);
}

VOID
_PHY_PathBStandBy_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm
#else
	IN PADAPTER	pAdapter
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
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path-S0 standby mode!\n"));

	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);
	ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)0x1, 0x0, bRFRegOffsetMask, 0x10000);

	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x808000);
}


VOID
_PHY_PIModeSwitch_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN		PIMode
	)
{
	u4Byte	mode;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	


	mode = PIMode ? 0x01000100 : 0x01000000;
	ODM_SetBBReg(pDM_Odm, rFPGA0_XA_HSSIParameter1, bMaskDWord, mode);
	ODM_SetBBReg(pDM_Odm, rFPGA0_XB_HSSIParameter1, bMaskDWord, mode);
}

BOOLEAN							
phy_SimularityCompare_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	s4Byte 		result[][8],
	IN	u1Byte		 c1,
	IN	u1Byte		 c2
	)
{
	u4Byte		i, j, diff, SimularityBitMap, bound = 0;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	u1Byte		final_candidate[2] = {0xFF, 0xFF};	
	BOOLEAN		bResult = TRUE;
/*#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
//	BOOLEAN		is2T = IS_92C_SERIAL( pHalData->VersionID);
//#else*/
	BOOLEAN		is2T = TRUE;
/*#endif*/

	s4Byte tmp1 = 0,tmp2 = 0;

	if (is2T)
		bound = 8;
	else
		bound = 4;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("===> IQK:phy_SimularityCompare_8723D c1 %d c2 %d!!!\n", c1, c2));


	SimularityBitMap = 0;
	
	for (i = 0; i < bound; i++) {
		
		if ((i == 1) || (i == 3) || (i == 5) || (i == 7)) {
			if ((result[c1][i] & 0x00000200) != 0)
				tmp1 = result[c1][i] | 0xFFFFFC00; 
			else
				tmp1 = result[c1][i];

			if ((result[c2][i] & 0x00000200) != 0)
				tmp2 = result[c2][i] | 0xFFFFFC00; 
			else
				tmp2 = result[c2][i];
		} else {
			tmp1 = result[c1][i];	
			tmp2 = result[c2][i];
		}
		
		diff = (tmp1 > tmp2) ? (tmp1 - tmp2) : (tmp2 - tmp1);
		
		if (diff > MAX_TOLERANCE) {
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK:differnece overflow %d index %d compare1 0x%x compare2 0x%x!!!\n",  diff, i, result[c1][i], result[c2][i]));
		
			if ((i == 2 || i == 6) && !SimularityBitMap) {
				if (result[c1][i]+result[c1][i+1] == 0)
					final_candidate[(i/4)] = c2;
				else if (result[c2][i]+result[c2][i+1] == 0)
					final_candidate[(i/4)] = c1;
				else
					SimularityBitMap = SimularityBitMap|(1<<i);					
			} else
				SimularityBitMap = SimularityBitMap|(1<<i);
		}
	}

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:phy_SimularityCompare_8723D SimularityBitMap   %x !!!\n", SimularityBitMap));
	
	if (SimularityBitMap == 0) {
		for (i = 0; i < (bound/4); i++) {
			if (final_candidate[i] != 0xFF) {
				for (j = i*4; j < (i+1)*4-2; j++)
					result[3][j] = result[final_candidate[i]][j];
				bResult = FALSE;
			}
		}
		return bResult;
	} else {

	if (!(SimularityBitMap & 0x03)) {
		for(i = 0; i < 2; i++)
			result[3][i] = result[c1][i];
	}

	if (!(SimularityBitMap & 0x0c)) {
		for(i = 2; i < 4; i++)
			result[3][i] = result[c1][i];
	}

	if (!(SimularityBitMap & 0x30)) {
		for(i = 4; i < 6; i++)
			result[3][i] = result[c1][i];

	}

	if (!(SimularityBitMap & 0xc0)) {
		for(i = 6; i < 8; i++)
			result[3][i] = result[c1][i];
	}
		
		return FALSE;
	}

	
	
}



VOID	
phy_IQCalibrate_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	s4Byte 		result[][8],
	IN	u1Byte		t,
	IN	BOOLEAN		is2T
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
	u4Byte			i;
	u1Byte			PathS1_OK, PathS0_OK;
	u1Byte 			tmp0xc50 = (u1Byte)ODM_GetBBReg(pDM_Odm, 0xC50, bMaskByte0);
	u1Byte 			tmp0xc58 = (u1Byte)ODM_GetBBReg(pDM_Odm, 0xC58, bMaskByte0);	
	u4Byte			ADDA_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };
	u4Byte			IQK_MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};
					
	
	u4Byte	IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
							rOFDM0_TRxPathEnable, 		rOFDM0_TRMuxPar,	
							rFPGA0_XCD_RFInterfaceSW,	rConfig_AntA,	rConfig_AntB,
							rFPGA0_XAB_RFInterfaceSW,	rFPGA0_XA_RFInterfaceOE,	
							rFPGA0_XB_RFInterfaceOE, rCCK0_AFESetting	
							};	
	u4Byte	cnt_IQKFail = 0;

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	u4Byte	retryCount = 2;
#else
#if MP_DRIVER
	const u4Byte	retryCount = 9;
#else
	const u4Byte	retryCount = 2;
#endif
#endif

	

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
#ifdef MP_TEST
		if (pDM_Odm->priv->pshare->rf_ft_var.mp_specific)
			retryCount = 9; 
#endif
#endif


	if (t == 0) {

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	 	_PHY_SaveADDARegisters_8723D(pAdapter, ADDA_REG, pDM_Odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);
		_PHY_SaveMACRegisters_8723D(pAdapter, IQK_MAC_REG, pDM_Odm->RFCalibrateInfo.IQK_MAC_backup);
	 	_PHY_SaveADDARegisters_8723D(pAdapter, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);				
#else
	 	_PHY_SaveADDARegisters_8723D(pDM_Odm, ADDA_REG, pDM_Odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);
		_PHY_SaveMACRegisters_8723D(pDM_Odm, IQK_MAC_REG, pDM_Odm->RFCalibrateInfo.IQK_MAC_backup);
	 	_PHY_SaveADDARegisters_8723D(pDM_Odm, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);		
#endif
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQ Calibration for 1T1R_S0/S1 for %d times\n", t));
	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	
 	_PHY_PathADDAOn_8723D(pAdapter, ADDA_REG, TRUE, is2T);
#else
 	_PHY_PathADDAOn_8723D(pDM_Odm, ADDA_REG, TRUE, is2T);
#endif
		
  
/*
	if(t==0)
	{
		pDM_Odm->RFCalibrateInfo.bRfPiEnable = (u1Byte)ODM_GetBBReg(pDM_Odm, rFPGA0_XA_HSSIParameter1, BIT(8));
	}
	
	if(!pDM_Odm->RFCalibrateInfo.bRfPiEnable){
		// Switch BB to PI mode to do IQ Calibration.
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_PHY_PIModeSwitch_8723D(pAdapter, TRUE);
#else
		_PHY_PIModeSwitch_8723D(pDM_Odm, TRUE);
#endif
	}
*/	

		
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_MACSettingCalibration_8723D(pAdapter, IQK_MAC_REG, pDM_Odm->RFCalibrateInfo.IQK_MAC_backup);
#else
	_PHY_MACSettingCalibration_8723D(pDM_Odm, IQK_MAC_REG, pDM_Odm->RFCalibrateInfo.IQK_MAC_backup);
#endif

	/*BB setting*/
	/*ODM_SetBBReg(pDM_Odm, rFPGA0_RFMOD, BIT24, 0x00);*/
	ODM_SetBBReg(pDM_Odm, rCCK0_AFESetting, 0x0f000000, 0xf);
	ODM_SetBBReg(pDM_Odm, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05611);
	ODM_SetBBReg(pDM_Odm, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4); 
	ODM_SetBBReg(pDM_Odm, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x25204200);  
	
	/*IQ calibration setting*/
	/*ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK setting!\n"));	*/	
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x808000);
	ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, 0x01007c00);
	ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x01004800);

if (is2T) {

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_PHY_PathBStandBy_8723D(pAdapter);

		
		_PHY_PathADDAOn_8723D(pAdapter, ADDA_REG, FALSE, is2T);
#else
		_PHY_PathBStandBy_8723D(pDM_Odm);

		
		_PHY_PathADDAOn_8723D(pDM_Odm, ADDA_REG, FALSE, is2T);
#endif
	}


#if 1
	for (i = 0 ; i < retryCount ; i++) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		PathS1_OK = phy_PathS1_IQK_8723D(pAdapter, is2T);
#else
		PathS1_OK = phy_PathS1_IQK_8723D(pDM_Odm, is2T);
#endif

		if (PathS1_OK == 0x01) {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S1 Tx IQK Success!!\n"));
				result[t][0] = (ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
				result[t][1] = (ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			break;
		} else {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S1 Tx IQK Fail!!\n"));
			result[t][0] = 0x100;
			result[t][1] = 0x0;
			cnt_IQKFail++;
		}	
#if 0		
		else if (i == (retryCount-1) && PathS1_OK == 0x01)	
		{
			RT_DISP(FINIT, INIT_IQK, ("Path S1 IQK Only  Tx Success!!\n"));
			
			result[t][0] = (ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;			
		}
#endif		
	}
#endif


#if 1
	for (i = 0 ; i < retryCount ; i++) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		PathS1_OK = phy_PathS1_RxIQK_8723D(pAdapter, is2T);
#else
		PathS1_OK = phy_PathS1_RxIQK_8723D(pDM_Odm, is2T);
#endif
		if (PathS1_OK == 0x03) {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path S1 Rx IQK Success!!\n")); 
				result[t][2] = (ODM_GetBBReg(pDM_Odm, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][3] = (ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			break;
		} else {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S1 Rx IQK Fail!!\n"));	
			result[t][2] = 0x100;
			result[t][3] = 0x0;
			cnt_IQKFail++;
		}
	}

	if (0x00 == PathS1_OK) {

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S1 IQK failed!!\n"));		
	}

#endif

	if (is2T) {

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_PHY_PathAStandBy_8723D(pAdapter);

		
		_PHY_PathADDAOn_8723D(pAdapter, ADDA_REG, FALSE, is2T);
#else
		_PHY_PathAStandBy_8723D(pDM_Odm);

		
		_PHY_PathADDAOn_8723D(pDM_Odm, ADDA_REG, FALSE, is2T);
#endif

			
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x808000);
		ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, 0x01007c00);
		ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x01004800);


#if 1
		for (i = 0 ; i < retryCount ; i++) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			PathS0_OK = phy_PathS0_IQK_8723D(pAdapter);
#else
			PathS0_OK = phy_PathS0_IQK_8723D(pDM_Odm);
#endif

		if (PathS0_OK == 0x01) {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S0 Tx IQK Success!!\n"));
				result[t][4] = (ODM_GetBBReg(pDM_Odm, 0xe94, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (ODM_GetBBReg(pDM_Odm, 0xe9c, bMaskDWord)&0x3FF0000)>>16;
			break;
		} else {
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S0 Tx IQK Fail!!\n"));
				result[t][4] = 0x100;
				result[t][5] = 0x0;
				cnt_IQKFail++;
			}
#if 0		
		else if (i == (retryCount-1) && PathS1_OK == 0x01)	
		{
			RT_DISP(FINIT, INIT_IQK, ("Path S0 IQK Only  Tx Success!!\n"));
			
			result[t][0] = (ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;			
		}
#endif		
	}
#endif


#if 1

for (i = 0 ; i < retryCount ; i++) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		PathS0_OK = phy_PathS0_RxIQK_8723D(pAdapter, is2T);
#else
		PathS0_OK = phy_PathS0_RxIQK_8723D(pDM_Odm, is2T);
#endif
		if (PathS0_OK == 0x03) {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path S0 Rx IQK Success!!\n"));
/*				result[t][0] = (ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;*/
/*				result[t][1] = (ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;*/
				result[t][6] = (ODM_GetBBReg(pDM_Odm, 0xea4, bMaskDWord)&0x3FF0000)>>16;
				result[t][7] = (ODM_GetBBReg(pDM_Odm, 0xeac, bMaskDWord)&0x3FF0000)>>16;
			break;
		} else {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S0 Rx IQK Fail!!\n"));	
			result[t][6] = 0x100;
			result[t][7] = 0x0;
			cnt_IQKFail++;
		}
	}



		if (0x00 == PathS0_OK)	
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S0 IQK failed!!\n"));		
		
#endif
	}


	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:Back to BB mode, load original value!\n"));
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);

	if (t != 0) {

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

	 	
	 	_PHY_ReloadADDARegisters_8723D(pAdapter, ADDA_REG, pDM_Odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);

		/* Reload MAC parameters*/
		_PHY_ReloadMACRegisters_8723D(pAdapter, IQK_MAC_REG, pDM_Odm->RFCalibrateInfo.IQK_MAC_backup);
		
	 	_PHY_ReloadADDARegisters_8723D(pAdapter, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);
#else
	 	
	 	_PHY_ReloadADDARegisters_8723D(pDM_Odm, ADDA_REG, pDM_Odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);

		/* Reload MAC parameters*/
		_PHY_ReloadMACRegisters_8723D(pDM_Odm, IQK_MAC_REG, pDM_Odm->RFCalibrateInfo.IQK_MAC_backup);
		
	 	_PHY_ReloadADDARegisters_8723D(pDM_Odm, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);
#endif
		

			ODM_SetBBReg(pDM_Odm, 0xc50, bMaskByte0, 0x50);
			ODM_SetBBReg(pDM_Odm, 0xc50, bMaskByte0, tmp0xc50);
			if (is2T) {
				ODM_SetBBReg(pDM_Odm, 0xc58, bMaskByte0, 0x50);
				ODM_SetBBReg(pDM_Odm, 0xc58, bMaskByte0, tmp0xc58);
			}
		
		
		ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);		
		ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);				

		
	}

	pDM_Odm->nIQK_Cnt++;
	
	if (cnt_IQKFail == 0)
		pDM_Odm->nIQK_OK_Cnt++;
	else
		pDM_Odm->nIQK_Fail_Cnt = pDM_Odm->nIQK_Fail_Cnt + cnt_IQKFail;

	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_IQCalibrate_8723D() <==\n"));
	
}


VOID	
phy_LCCalibrate_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	PVOID		pDM_VOID,
#else
	IN PDM_ODM_T		pDM_Odm,
#endif	
	IN	BOOLEAN 	is2T
	)
{
	u1Byte	tmpReg;
	u4Byte	RF_Amode=0, RF_Bmode=0, LC_Cal, cnt;
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#endif	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	PADAPTER pAdapter = pDM_Odm->Adapter;
#endif	


	tmpReg = ODM_Read1Byte(pDM_Odm, 0xd03);

	if ((tmpReg & 0x70) != 0)			
		ODM_Write1Byte(pDM_Odm, 0xd03, tmpReg & 0x8F);	
	else							
		ODM_Write1Byte(pDM_Odm, REG_TXPAUSE, 0xFF);			

	/*backup RF0x18*/

	LC_Cal = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask);

		
	/*Start LCK*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, LC_Cal | 0x08000);

	for (cnt = 0; cnt < 100; cnt++) {
	  if (ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x8000) != 0x1)
	  	break;	
	  ODM_delay_ms(10);
	}

	/* Recover channel number*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, LC_Cal);	

	/*Restore original situation*/	
	if ((tmpReg & 0x70) != 0)  
		ODM_Write1Byte(pDM_Odm, 0xd03, tmpReg);
	else 
		ODM_Write1Byte(pDM_Odm, REG_TXPAUSE, 0x00);		
}


#define		APK_BB_REG_NUM	8
#define		APK_CURVE_REG_NUM 4
#define		PATH_NUM		2

VOID	
phy_APCalibrate_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	s1Byte 		delta,
	IN	BOOLEAN		is2T
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
	u4Byte 			regD[PATH_NUM];
	u4Byte			tmpReg, index, offset,  apkbound;
	u1Byte			path, i, pathbound = PATH_NUM;		
	u4Byte			BB_backup[APK_BB_REG_NUM];
	u4Byte			BB_REG[APK_BB_REG_NUM] = {	
						rFPGA1_TxBlock, 	rOFDM0_TRxPathEnable, 
						rFPGA0_RFMOD, 	rOFDM0_TRMuxPar, 
						rFPGA0_XCD_RFInterfaceSW,	rFPGA0_XAB_RFInterfaceSW, 
						rFPGA0_XA_RFInterfaceOE, 	rFPGA0_XB_RFInterfaceOE	};
	u4Byte			BB_AP_MODE[APK_BB_REG_NUM] = {	
						0x00000020, 0x00a05430, 0x02040000, 
						0x000800e4, 0x00204000 };
	u4Byte			BB_normal_AP_MODE[APK_BB_REG_NUM] = {	
						0x00000020, 0x00a05430, 0x02040000, 
						0x000800e4, 0x22204000 };						

	u4Byte			AFE_backup[IQK_ADDA_REG_NUM];
	u4Byte			AFE_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };

	u4Byte			MAC_backup[IQK_MAC_REG_NUM];
	u4Byte			MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	u4Byte			APK_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
					{0x0852c, 0x1852c, 0x5852c, 0x1852c, 0x5852c},
					{0x2852e, 0x0852e, 0x3852e, 0x0852e, 0x0852e}
					};	

	u4Byte			APK_normal_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
					{0x0852c, 0x0a52c, 0x3a52c, 0x5a52c, 0x5a52c},	
					{0x0852c, 0x0a52c, 0x5a52c, 0x5a52c, 0x5a52c}
					};
	
	u4Byte			APK_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52014, 0x52013, 0x5200f, 0x5208d},
					{0x5201a, 0x52019, 0x52016, 0x52033, 0x52050}
					};

	u4Byte			APK_normal_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a},	
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a}
					};

	u4Byte			AFE_on_off[PATH_NUM] = {
					0x04db25a4, 0x0b1b25a4};	

	u4Byte			APK_offset[PATH_NUM] = {
					rConfig_AntA, rConfig_AntB};

	u4Byte			APK_normal_offset[PATH_NUM] = {
					rConfig_Pmpd_AntA, rConfig_Pmpd_AntB};
					
	u4Byte			APK_value[PATH_NUM] = {
					0x92fc0000, 0x12fc0000};					

	u4Byte			APK_normal_value[PATH_NUM] = {
					0x92680000, 0x12680000};					

	s1Byte			APK_delta_mapping[APK_BB_REG_NUM][13] = {
					{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},											
					{-6, -4, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-11, -9, -7, -5, -3, -1, 0, 0, 0, 0, 0, 0, 0}
					};
	
	u4Byte			APK_normal_setting_value_1[13] = {
					0x01017018, 0xf7ed8f84, 0x1b1a1816, 0x2522201e, 0x322e2b28,
					0x433f3a36, 0x5b544e49, 0x7b726a62, 0xa69a8f84, 0xdfcfc0b3,
					0x12680000, 0x00880000, 0x00880000
					};

	u4Byte			APK_normal_setting_value_2[16] = {
					0x01c7021d, 0x01670183, 0x01000123, 0x00bf00e2, 0x008d00a3,
					0x0068007b, 0x004d0059, 0x003a0042, 0x002b0031, 0x001f0025,
					0x0017001b, 0x00110014, 0x000c000f, 0x0009000b, 0x00070008,
					0x00050006
					};
	
	u4Byte			APK_result[PATH_NUM][APK_BB_REG_NUM];	
/*	u4Byte			AP_curve[PATH_NUM][APK_CURVE_REG_NUM];*/

	s4Byte			BB_offset, delta_V, delta_offset;

#if MP_DRIVER == 1
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);	
#else
	PMPT_CONTEXT	pMptCtx = &(pAdapter->MptCtx);	
#endif
	pMptCtx->APK_bound[0] = 45;
	pMptCtx->APK_bound[1] = 52;		

#endif

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("==>phy_APCalibrate_8723D() delta %d\n", delta));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("AP Calibration for %s\n", (is2T ? "2T2R" : "1T1R")));
	if (!is2T)
		pathbound = 1;

	

/* Temporarily do not allow normal driver to do the following settings because these offset
// and value will cause RF internal PA to be unpredictably disabled by HW, such that RF Tx signal
// will disappear after disable/enable card many times on 88CU. RF SD and DD have not find the
// root cause, so we remove these actions temporarily. Added by tynli and SD3 Allen. 2010.05.31.*/
#if MP_DRIVER != 1
	return;
#endif
	
	for (index = 0; index < PATH_NUM; index++) {
		APK_offset[index] = APK_normal_offset[index];
		APK_value[index] = APK_normal_value[index];
		AFE_on_off[index] = 0x6fdb25a4;
	}

	for (index = 0; index < APK_BB_REG_NUM; index++) {
		for (path = 0; path < pathbound; path++) {
			APK_RF_init_value[path][index] = APK_normal_RF_init_value[path][index];
			APK_RF_value_0[path][index] = APK_normal_RF_value_0[path][index];
		}
		BB_AP_MODE[index] = BB_normal_AP_MODE[index];
	}			

	apkbound = 6;
	
	
	for (index = 0; index < APK_BB_REG_NUM ; index++) {
		if (index == 0)		
			continue;				
		BB_backup[index] = ODM_GetBBReg(pDM_Odm, BB_REG[index], bMaskDWord);
	}
	
													
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_SaveMACRegisters_8723D(pAdapter, MAC_REG, MAC_backup);
	
	
	_PHY_SaveADDARegisters_8723D(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#else
	_PHY_SaveMACRegisters_8723D(pDM_Odm, MAC_REG, MAC_backup);
	
	
	_PHY_SaveADDARegisters_8723D(pDM_Odm, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#endif

	for (path = 0; path < pathbound; path++) {


		if (path == ODM_RF_PATH_A) {
					
			offset = rPdp_AntA;
			for (index = 0; index < 11; index++) {
				ODM_SetBBReg(pDM_Odm, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0x%x value 0x%x\n", offset, ODM_GetBBReg(pDM_Odm, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}
			
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);
			
			offset = rConfig_AntA;
			for (; index < 13; index++) {
				ODM_SetBBReg(pDM_Odm, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0x%x value 0x%x\n", offset, ODM_GetBBReg(pDM_Odm, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}	
			
			
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x400000);
		
			
			offset = rPdp_AntA;
			for (index = 0; index < 16; index++) {
				ODM_SetBBReg(pDM_Odm, offset, bMaskDWord, APK_normal_setting_value_2[index]);		
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0x%x value 0x%x\n", offset, ODM_GetBBReg(pDM_Odm, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}				
			ODM_SetBBReg(pDM_Odm,  rFPGA0_IQK, 0xffffff00, 0x000000);							
		} else if (path == ODM_RF_PATH_B) {
				
			offset = rPdp_AntB;
			for (index = 0; index < 10; index++) {
				ODM_SetBBReg(pDM_Odm, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0x%x value 0x%x\n", offset, ODM_GetBBReg(pDM_Odm, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x12680000);			
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);
#else
			PHY_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);
#endif
			
			offset = rConfig_AntA;
			index = 11;
			for (; index < 13; index++) {
				ODM_SetBBReg(pDM_Odm, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("phy_APCalibrate_8723D() offset 0x%x value 0x%x\n", offset, ODM_GetBBReg(pDM_Odm, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}	
			
			
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x400000);
			
			
			offset = 0xb60;
			for (index = 0; index < 16; index++) {
				ODM_SetBBReg(pDM_Odm, offset, bMaskDWord, APK_normal_setting_value_2[index]);		
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("phy_APCalibrate_8723D() offset 0x%x value 0x%x\n", offset, ODM_GetBBReg(pDM_Odm, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}				
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);							
		}
	
		
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		regD[path] = PHY_QueryRFReg(pAdapter, path, RF_TXBIAS_A, bMaskDWord);
#else
		regD[path] = ODM_GetRFReg(pDM_Odm, path, RF_TXBIAS_A, bMaskDWord);
#endif
		
		
		for (index = 0; index < IQK_ADDA_REG_NUM ; index++)
			ODM_SetBBReg(pDM_Odm, AFE_REG[index], bMaskDWord, AFE_on_off[path]);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0xe70 %x\n", ODM_GetBBReg(pDM_Odm, rRx_Wait_CCA, bMaskDWord)));		

		
		if (path == 0) {				
			for (index = 0; index < APK_BB_REG_NUM ; index++) {

				if (index == 0)		
					continue;			
				else if (index < 5)
				ODM_SetBBReg(pDM_Odm, BB_REG[index], bMaskDWord, BB_AP_MODE[index]);
				else if (BB_REG[index] == 0x870)
					ODM_SetBBReg(pDM_Odm, BB_REG[index], bMaskDWord, BB_backup[index]|BIT10|BIT26);
				else
					ODM_SetBBReg(pDM_Odm, BB_REG[index], BIT10, 0x0);					
			}

			ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);			
			ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);					
		} else {
			ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_B, bMaskDWord, 0x01008c00);			
			ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_B, bMaskDWord, 0x01008c00);					
		
		}

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0x800 %x\n", ODM_GetBBReg(pDM_Odm, 0x800, bMaskDWord)));				

		
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_PHY_MACSettingCalibration_8723D(pAdapter, MAC_REG, MAC_backup);
#else
		_PHY_MACSettingCalibration_8723D(pDM_Odm, MAC_REG, MAC_backup);
#endif
		
		if (path == ODM_RF_PATH_A) {
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_AC, bMaskDWord, 0x10000);			
		} else {
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_AC, bMaskDWord, 0x10000);			
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_MODE1, bMaskDWord, 0x1000f);			
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_MODE2, bMaskDWord, 0x20103);						
		}

		delta_offset = ((delta+14)/2);
		if (delta_offset < 0)
			delta_offset = 0;
		else if (delta_offset > 12)
			delta_offset = 12;
			
		
		for (index = 0; index < APK_BB_REG_NUM; index++) {
			if (index != 1)	
				continue;
					
			tmpReg = APK_RF_init_value[path][index];
#if 1			
			if (!pDM_Odm->RFCalibrateInfo.bAPKThermalMeterIgnore) {
				BB_offset = (tmpReg & 0xF0000) >> 16;

				if (!(tmpReg & BIT15)) {
					BB_offset = -BB_offset;
				}

				delta_V = APK_delta_mapping[index][delta_offset];
				
				BB_offset += delta_V;

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() APK index %d tmpReg 0x%x delta_V %d delta_offset %d\n", index, tmpReg, delta_V, delta_offset));		
				
				if (BB_offset < 0) {
					tmpReg = tmpReg & (~BIT15);
					BB_offset = -BB_offset;
				} else {
					tmpReg = tmpReg | BIT15;
				}
				tmpReg = (tmpReg & 0xFFF0FFFF) | (BB_offset << 16);
			}
#endif

			ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)path, RF_IPA_A, bMaskDWord, 0x8992e);
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0xc %x\n", PHY_QueryRFReg(pAdapter, path, RF_IPA_A, bMaskDWord)));		
			ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)path, RF_AC, bMaskDWord, APK_RF_value_0[path][index]);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("phy_APCalibrate_8723D() offset 0x0 %x\n", PHY_QueryRFReg(pAdapter, path, RF_AC, bMaskDWord)));		
			ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)path, RF_TXBIAS_A, bMaskDWord, tmpReg);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0xd %x\n", PHY_QueryRFReg(pAdapter, path, RF_TXBIAS_A, bMaskDWord)));					
#else
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0xc %x\n", ODM_GetRFReg(pDM_Odm, path, RF_IPA_A, bMaskDWord)));		
			ODM_SetRFReg(pDM_Odm, path, RF_AC, bMaskDWord, APK_RF_value_0[path][index]);
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("phy_APCalibrate_8723D() offset 0x0 %x\n", ODM_GetRFReg(pDM_Odm, path, RF_AC, bMaskDWord)));		
			ODM_SetRFReg(pDM_Odm, path, RF_TXBIAS_A, bMaskDWord, tmpReg);
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0xd %x\n", ODM_GetRFReg(pDM_Odm, path, RF_TXBIAS_A, bMaskDWord)));					
#endif
			
				
			i = 0;
			do
			{
				ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x800000);
				{
					ODM_SetBBReg(pDM_Odm, APK_offset[path], bMaskDWord, APK_value[0]);		
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0x%x value 0x%x\n", APK_offset[path], ODM_GetBBReg(pDM_Odm, APK_offset[path], bMaskDWord)));
					ODM_delay_ms(3);				
					ODM_SetBBReg(pDM_Odm, APK_offset[path], bMaskDWord, APK_value[1]);
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0x%x value 0x%x\n", APK_offset[path], ODM_GetBBReg(pDM_Odm, APK_offset[path], bMaskDWord)));

					ODM_delay_ms(20);
				}
				ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);

				if (path == ODM_RF_PATH_A)
					tmpReg = ODM_GetBBReg(pDM_Odm, rAPK, 0x03E00000);
				else
					tmpReg = ODM_GetBBReg(pDM_Odm, rAPK, 0xF8000000);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8723D() offset 0xbd8[25:21] %x\n", tmpReg));		
				

				i++;
			}
			while(tmpReg > apkbound && i < 4);

			APK_result[path][index] = tmpReg;
		}
	}

		
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_ReloadMACRegisters_8723D(pAdapter, MAC_REG, MAC_backup);
#else
	_PHY_ReloadMACRegisters_8723D(pDM_Odm, MAC_REG, MAC_backup);
#endif
	
	
	for (index = 0; index < APK_BB_REG_NUM ; index++) {

		if (index == 0)		
			continue;					
		ODM_SetBBReg(pDM_Odm, BB_REG[index], bMaskDWord, BB_backup[index]);
	}

	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_ReloadADDARegisters_8723D(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#else
	_PHY_ReloadADDARegisters_8723D(pDM_Odm, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#endif

	
	for (path = 0; path < pathbound; path++)
	{
		ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)path, 0xd, bMaskDWord, regD[path]);
		if (path == ODM_RF_PATH_B) {
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_MODE1, bMaskDWord, 0x1000f);			
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_MODE2, bMaskDWord, 0x20101);						
		}

		
		if (APK_result[path][1] > 6)
			APK_result[path][1] = 6;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("apk path %d result %d 0x%x \t", path, 1, APK_result[path][1]));					
	}

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("\n"));
	

	for (path = 0; path < pathbound; path++) {
		ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)path, 0x3, bMaskDWord, 
		((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (APK_result[path][1] << 5) | APK_result[path][1]));
		if (path == ODM_RF_PATH_A)
			ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)path, 0x4, bMaskDWord, 
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x00 << 5) | 0x05));		
		else
		ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)path, 0x4, bMaskDWord, 
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x02 << 5) | 0x05));						
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		if (!IS_HARDWARE_TYPE_8723A(pAdapter))		
			ODM_SetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G9_G11, bMaskDWord, 
			((0x08 << 15) | (0x08 << 10) | (0x08 << 5) | 0x08));			
#endif		
	}

	pDM_Odm->RFCalibrateInfo.bAPKdone = TRUE;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("<==phy_APCalibrate_8723D()\n"));
}



#define		DP_BB_REG_NUM		7
#define		DP_RF_REG_NUM		1
#define		DP_RETRY_LIMIT		10
#define		DP_PATH_NUM		2
#define		DP_DPK_NUM			3
#define		DP_DPK_VALUE_NUM	2





VOID
PHY_IQCalibrate_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN 	bReCovery
	)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;	
	#else  
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;	
	#endif

	#if (MP_DRIVER == 1)
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	
	PMPT_CONTEXT	pMptCtx = &(pAdapter->MptCtx);	
	#else
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);		
	#endif	
	#endif
#endif	


	u1Byte			u1bTmp;
	u2Byte			count = 0;
	s4Byte			result[4][8];	
	u1Byte			i, final_candidate, Indexforchannel;
	BOOLEAN			bPathS1_OK, bPathS0_OK;
	s4Byte			RegE94_S1, RegE9C_S1, RegEA4_S1, RegEAC_S1, RegE94_S0, RegE9C_S0, RegEA4_S0, RegEAC_S0, RegTmp = 0;
	s4Byte			RegC80, RegC94, RegC14, RegCA0, RegCd0, RegCd4, RegCd8;
	BOOLEAN			is12simular, is13simular, is23simular;	
	BOOLEAN 		bSingleTone = FALSE, bCarrierSuppression = FALSE, bContinousTx = FALSE;
	u4Byte			IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
					rOFDM0_XARxIQImbalance, 	rOFDM0_XBRxIQImbalance, 
					rOFDM0_ECCAThreshold, 	rOFDM0_AGCRSSITable,
					rOFDM0_XATxIQImbalance, 	rOFDM0_XBTxIQImbalance, 
					rOFDM0_XCTxAFE, 			rOFDM0_XDTxAFE, 
					rOFDM0_RxIQExtAnta};
	u4Byte			Path_SEL_BB_phyIQK;
	u4Byte 			originalPath, originalGNT, oriPathCtrl;
#if 1
ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("================ IQK Start ===================\n"));


#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE) )
	if (ODM_CheckPowerStatus(pAdapter) == FALSE)
		return;
#else
	prtl8192cd_priv	priv = pDM_Odm->priv;

#ifdef MP_TEST
	if (priv->pshare->rf_ft_var.mp_specific) {
		if ((OPMODE & WIFI_MP_CTX_PACKET) || (OPMODE & WIFI_MP_CTX_ST))
			return;
	}
#endif

	if (priv->pshare->IQK_88E_done)
		bReCovery= 1;
	priv->pshare->IQK_88E_done = 1;

#endif	

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (!(pDM_Odm->SupportAbility & ODM_RF_CALIBRATION)) {
		return;
	}
#endif		


#if MP_DRIVER == 1

/*#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/
		/* <VincentL, 131231> Add to determine IQK ON/OFF in certain case, Suggested by Cheng.*/
	/*	if (!pHalData->IQK_MP_Switch)
			return;*/
/*#endif*/
		
		bSingleTone = pMptCtx->bSingleTone;
		bCarrierSuppression = pMptCtx->bCarrierSuppression;
		bContinousTx = pMptCtx->bStartContTx;
#endif

		/* 20120213<Kordan> Turn on when continuous Tx to pass lab testing. (required by Edlu)*/
		if (bSingleTone || bCarrierSuppression || bContinousTx)
			return;

	

#if DISABLE_BB_RF
	return;
#endif

	if (pDM_Odm->RFCalibrateInfo.bIQKInProgress)
		return;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("=====>PHY_IQCalibrate_8723D\n"));

	
	Path_SEL_BB_phyIQK = ODM_GetBBReg(pDM_Odm, 0x948, bMaskDWord);

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_AP))
	if (bReCovery)
#else
	if (bReCovery && (!pAdapter->bInHctTest))  
#endif
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("PHY_IQCalibrate_8723D: Return due to bReCovery!\n"));
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_PHY_ReloadADDARegisters_8723D(pAdapter, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);
#else
		_PHY_ReloadADDARegisters_8723D(pDM_Odm, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);
#endif
		return;
	}

	/*Check & wait if BT is doing IQK*/

	if (pDM_Odm->mp_mode == FALSE) {
#if MP_DRIVER != 1
		SetFwWiFiCalibrationCmd_8723D(pAdapter, 1);

		
		count = 0;
		u1bTmp = PlatformEFIORead1Byte(pAdapter, 0x1e6);
		while (u1bTmp != 0x1 && count < 1000) {
			PlatformStallExecution(10);
			u1bTmp = PlatformEFIORead1Byte(pAdapter, 0x1e6);
			count++;
		}
		if (count >= 1000) {
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]Polling 0x1e6 to 1 for WiFi calibration H2C cmd FAIL! count(%d)", count));
		}

		
		u1bTmp = PlatformEFIORead1Byte(pAdapter, 0x1e7);
		while ((!(u1bTmp&BIT0)) && count < 6000) {
			PlatformStallExecution(50);
			u1bTmp = PlatformEFIORead1Byte(pAdapter, 0x1e7);
			count++;
		}
#endif
	}

	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK:Start!!!\n"));
	ODM_AcquireSpinLock(pDM_Odm, RT_IQK_SPINLOCK);
	pDM_Odm->RFCalibrateInfo.bIQKInProgress = TRUE;
	ODM_ReleaseSpinLock(pDM_Odm, RT_IQK_SPINLOCK);

	for (i = 0; i < 8; i++) {  
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;
		result[3][i] = 0;
	}
	
	final_candidate = 0xff;
	bPathS1_OK = FALSE;
	bPathS0_OK = FALSE;
	is12simular = FALSE;
	is23simular = FALSE;
	is13simular = FALSE;

for (i = 0; i < 3; i++) {

#if 1
	/*set path control to WL*/
	oriPathCtrl = ODM_GetMACReg(pDM_Odm, 0x64, bMaskByte3);  /*save 0x67*/
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]original 0x67 = 0x%x\n", oriPathCtrl));
	ODM_SetMACReg(pDM_Odm, 0x64, BIT31, 0x1);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]set 0x67 = 0x%x\n", ODM_GetMACReg(pDM_Odm, 0x64, bMaskByte3)));
	
	/*backup Path & GNT value */
	originalPath = ODM_GetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, bMaskDWord);  /*save 0x70*/
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0x800f0038);
	ODM_delay_ms(1);
	originalGNT = ODM_GetBBReg(pDM_Odm, REG_LTECOEX_READ_DATA, bMaskDWord);  /*save 0x38*/
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", originalGNT));
	
	/*set GNT_WL=1/GNT_BT=1  and Path owner to WiFi for pause BT traffic*/
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, 0x0000ff00);
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc0020038);	/*0x38[15:8] = 0x77*/
	ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, BIT26, 0x1);
#endif


#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	
			phy_IQCalibrate_8723D(pAdapter, result, i, TRUE);
#else
			phy_IQCalibrate_8723D(pDM_Odm, result, i, TRUE);
#endif			

#if 1
	/*Restore GNT_WL/GNT_BT  and Path owner*/
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_WRITE_DATA, bMaskDWord, originalGNT);
	ODM_SetBBReg(pDM_Odm, REG_LTECOEX_CTRL, bMaskDWord, 0xc00f0038);
	ODM_SetMACReg(pDM_Odm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, originalPath);

	/*Restore path control owner*/
	ODM_SetMACReg(pDM_Odm, 0x64, bMaskByte3, oriPathCtrl);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]restore 0x67 = 0x%x\n", ODM_GetMACReg(pDM_Odm, 0x64, bMaskByte3)));
#endif
		
		if (i == 1) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			is12simular = phy_SimularityCompare_8723D(pAdapter, result, 0, 1);
#else
			is12simular = phy_SimularityCompare_8723D(pDM_Odm, result, 0, 1);
#endif		
			
			if (is12simular) {
				final_candidate = 0;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: is12simular final_candidate is %x\n",final_candidate));				
				break;
			}
		}
			
		if (i == 2) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			is13simular = phy_SimularityCompare_8723D(pAdapter, result, 0, 2);
#else
			is13simular = phy_SimularityCompare_8723D(pDM_Odm, result, 0, 2);
#endif			

			if (is13simular) {
				final_candidate = 0;			
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: is13simular final_candidate is %x\n",final_candidate));
				
				break;
			}
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			is23simular = phy_SimularityCompare_8723D(pAdapter, result, 1, 2);
#else
			is23simular = phy_SimularityCompare_8723D(pDM_Odm, result, 1, 2);
#endif			

			if (is23simular) {
				final_candidate = 1;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: is23simular final_candidate is %x\n",final_candidate));				
			} else {
				for (i = 0; i < 8; i++)
					RegTmp += result[3][i];
				
					if (RegTmp != 0)
						final_candidate = 3;
					else
						final_candidate = 0xFF;

			}
		}
	}
	


	for (i = 0; i < 4; i++) {
		RegE94_S1 = result[i][0];
		RegE9C_S1 = result[i][1];
		RegEA4_S1 = result[i][2];
		RegEAC_S1 = result[i][3];
		RegE94_S0 = result[i][4];
		RegE9C_S0 = result[i][5];
		RegEA4_S0 = result[i][6];
		RegEAC_S0 = result[i][7];
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK] RegE94_S1=%x RegE9C_S1=%x RegEA4_S1=%x RegEAC_S1=%x RegE94_S0=%x RegE9C_S0=%x RegEA4_S0=%x RegEAC_S0=%x\n ", RegE94_S1, RegE9C_S1, RegEA4_S1, RegEAC_S1, RegE94_S0, RegE9C_S0, RegEA4_S0, RegEAC_S0));
	}
	
	if (final_candidate != 0xff) {
		pDM_Odm->RFCalibrateInfo.RegE94 = RegE94_S1 = result[final_candidate][0];
		pDM_Odm->RFCalibrateInfo.RegE9C = RegE9C_S1 = result[final_candidate][1];
		RegEA4_S1 = result[final_candidate][2];
		RegEAC_S1 = result[final_candidate][3];
		pDM_Odm->RFCalibrateInfo.RegEB4 = RegE94_S0 = result[final_candidate][4];
		pDM_Odm->RFCalibrateInfo.RegEBC = RegE9C_S0 = result[final_candidate][5];
		RegEA4_S0 = result[final_candidate][6];
		RegEAC_S0 = result[final_candidate][7];
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("[IQK] final_candidate is %x\n", final_candidate));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("[IQK] TX1_X=%x TX1_Y=%x RX1_X=%x RX1_Y=%x TX0_X=%x TX0_Y=%x RX0_X=%x RX0_Y=%x\n ", RegE94_S1, RegE9C_S1, RegEA4_S1, RegEAC_S1, RegE94_S0, RegE9C_S0, RegEA4_S0, RegEAC_S0));
		bPathS1_OK = bPathS0_OK = TRUE;
	} else {
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("[IQK] FAIL use default value\n"));
	
		pDM_Odm->RFCalibrateInfo.RegE94 = pDM_Odm->RFCalibrateInfo.RegEB4 = 0x100;	
		pDM_Odm->RFCalibrateInfo.RegE9C = pDM_Odm->RFCalibrateInfo.RegEBC = 0x0;	
	}

	
	if (RegE94_S1 != 0)
		_PHY_PathS1FillIQKMatrix_8723D(pAdapter, bPathS1_OK, result, final_candidate, (RegEA4_S1 == 0));
	if (RegE94_S0 != 0)
		_PHY_PathS0FillIQKMatrix_8723D(pAdapter, bPathS0_OK, result, final_candidate, (RegEA4_S0 == 0));

	
	RegC80 = ODM_GetBBReg(pDM_Odm, 0xc80, bMaskDWord);
	RegC94 = ODM_GetBBReg(pDM_Odm, 0xc94, bMaskDWord);
	RegC14 = ODM_GetBBReg(pDM_Odm, 0xc14, bMaskDWord);
	RegCA0 = ODM_GetBBReg(pDM_Odm, 0xca0, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xc80 = 0x%x 0xc94 = 0x%x 0xc14 = 0x%x 0xca0 = 0x%x\n", RegC80, RegC94, RegC14, RegCA0));

	
	RegCd0 = ODM_GetBBReg(pDM_Odm, 0xcd0, bMaskDWord);
	RegCd4 = ODM_GetBBReg(pDM_Odm, 0xcd4, bMaskDWord);
	RegCd8 = ODM_GetBBReg(pDM_Odm, 0xcd8, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xcd0 = 0x%x 0xcd4 = 0x%x 0xcd8 = 0x%x\n", RegCd0, RegCd4, RegCd8));

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
    Indexforchannel = ODM_GetRightChnlPlaceforIQK(pHalData->CurrentChannel);
#else
	Indexforchannel = 0;
#endif


	if (final_candidate < 4) {
		for (i = 0; i < IQK_Matrix_REG_NUM; i++)
			pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].Value[0][i] = result[final_candidate][i];
			pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].bIQKDone = TRUE;		
	}
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("\nIQK OK Indexforchannel %d.\n", Indexforchannel));
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

	_PHY_SaveADDARegisters_8723D(pAdapter, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);
#else
	_PHY_SaveADDARegisters_8723D(pDM_Odm, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup_recover, IQK_BB_REG_NUM);
#endif	

	if (pDM_Odm->mp_mode == FALSE) {
#if MP_DRIVER != 1
		SetFwWiFiCalibrationCmd_8723D(pAdapter, 0);
		
		
		count = 0;
		u1bTmp = PlatformEFIORead1Byte(pAdapter, 0x1e6);
		while (u1bTmp != 0 && count < 1000) {
			PlatformStallExecution(10);
			u1bTmp = PlatformEFIORead1Byte(pAdapter, 0x1e6);
			count++;
		}
		
		if (count >= 1000) {
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]Polling 0x1e6 to 0 for WiFi calibration H2C cmd FAIL! count(%d)", count));
		}
#endif
	}

	ODM_AcquireSpinLock(pDM_Odm, RT_IQK_SPINLOCK);
	pDM_Odm->RFCalibrateInfo.bIQKInProgress = FALSE;
	ODM_ReleaseSpinLock(pDM_Odm, RT_IQK_SPINLOCK);

	
	ODM_SetBBReg(pDM_Odm, 0x948, bMaskDWord, Path_SEL_BB_phyIQK);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK finished\n"));
#endif

}


VOID
PHY_LCCalibrate_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	PVOID		pDM_VOID
#else
	IN PDM_ODM_T		pDM_Odm
#endif
	)
{
	BOOLEAN 		bSingleTone = FALSE, bCarrierSuppression = FALSE;
	u4Byte			timeout = 2000, timecount = 0;

#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	PADAPTER  		pAdapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	

	#if (MP_DRIVER == 1)
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	
	PMPT_CONTEXT	pMptCtx = &(pAdapter->MptCtx);	
	#else
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);		
	#endif	
	#endif
#endif	

#if MP_DRIVER == 1	
	bSingleTone = pMptCtx->bSingleTone;
	bCarrierSuppression = pMptCtx->bCarrierSuppression;	
#endif


#if DISABLE_BB_RF
	return;
#endif




#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (!(pDM_Odm->SupportAbility & ODM_RF_CALIBRATION)) {
		return;
	}
#endif	
	
	if (bSingleTone || bCarrierSuppression)
		return;

	while(*(pDM_Odm->pbScanInProcess) && timecount < timeout)
	{
		ODM_delay_ms(50);
		timecount += 50;
	}	
	
	pDM_Odm->RFCalibrateInfo.bLCKInProgress = TRUE;

	phy_LCCalibrate_8723D(pDM_Odm, FALSE);

	pDM_Odm->RFCalibrateInfo.bLCKInProgress = FALSE;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("LCK:Finish!!!interface %d\n", pDM_Odm->InterfaceIndex));

}

VOID
PHY_APCalibrate_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	s1Byte 		delta	
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
#if DISABLE_BB_RF
	return;
#endif

	return;
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (!(pDM_Odm->SupportAbility & ODM_RF_CALIBRATION)) {
		return;
	}
#endif	

#if FOR_BRAZIL_PRETEST != 1
	if (pDM_Odm->RFCalibrateInfo.bAPKdone)
#endif		
		return;


#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		phy_APCalibrate_8723D(pAdapter, delta, FALSE);
#else
		phy_APCalibrate_8723D(pDM_Odm, delta, FALSE);
#endif
	}
VOID phy_SetRFPathSwitch_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN		bMain,
	IN	BOOLEAN		is2T
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#endif

	if (bMain) {	
		ODM_SetMACReg(pDM_Odm, 0x7C4, bMaskLWord, 0x7700);	
	} else {		
		ODM_SetMACReg(pDM_Odm, 0x7C4, bMaskLWord, 0xDD00);	
	}
	
	ODM_SetMACReg(pDM_Odm, 0x7C0, bMaskDWord, 0xC00F0038);
	ODM_SetMACReg(pDM_Odm, 0x70, BIT26, 1);
	ODM_SetMACReg(pDM_Odm, 0x64, BIT31, 1);
}

VOID PHY_SetRFPathSwitch_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN		bMain
	)
{
HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#endif
	
#if DISABLE_BB_RF
	return;
#endif
	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		phy_SetRFPathSwitch_8723D(pAdapter, bMain, TRUE);
#endif	

}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

VOID	
phy_DigitalPredistortion_8723D(
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN	PADAPTER	pAdapter,
#else
	IN PDM_ODM_T	pDM_Odm,
#endif
	IN	BOOLEAN		is2T
	)
{
#if (RT_PLATFORM == PLATFORM_WINDOWS)
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	

	u4Byte			tmpReg, tmpReg2, index,  i;		
	u1Byte			path, pathbound = PATH_NUM;
	u4Byte			AFE_backup[IQK_ADDA_REG_NUM];
	u4Byte			AFE_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };

	u4Byte			BB_backup[DP_BB_REG_NUM];	
	u4Byte			BB_REG[DP_BB_REG_NUM] = {
						rOFDM0_TRxPathEnable, rFPGA0_RFMOD, 
						rOFDM0_TRMuxPar, 	rFPGA0_XCD_RFInterfaceSW,
						rFPGA0_XAB_RFInterfaceSW, rFPGA0_XA_RFInterfaceOE, 
						rFPGA0_XB_RFInterfaceOE};						
	u4Byte			BB_settings[DP_BB_REG_NUM] = {
						0x00a05430, 0x02040000, 0x000800e4, 0x22208000, 
						0x0, 0x0, 0x0};	

	u4Byte			RF_backup[DP_PATH_NUM][DP_RF_REG_NUM];
	u4Byte			RF_REG[DP_RF_REG_NUM] = {
						RF_TXBIAS_A};

	u4Byte			MAC_backup[IQK_MAC_REG_NUM];
	u4Byte			MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	u4Byte			Tx_AGC[DP_DPK_NUM][DP_DPK_VALUE_NUM] = {
						{0x1e1e1e1e, 0x03901e1e},
						{0x18181818, 0x03901818},
						{0x0e0e0e0e, 0x03900e0e}
					};

	u4Byte			AFE_on_off[PATH_NUM] = {
					0x04db25a4, 0x0b1b25a4};	

	u1Byte			RetryCount = 0;


	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("==>phy_DigitalPredistortion_8723D()\n"));
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_DigitalPredistortion_8723D for %s\n", (is2T ? "2T2R" : "1T1R")));

	
	for (index = 0; index < DP_BB_REG_NUM; index++)
		BB_backup[index] = ODM_GetBBReg(pDM_Odm, BB_REG[index], bMaskDWord);

	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_SaveMACRegisters_8723D(pAdapter, BB_REG, MAC_backup);
#else
	_PHY_SaveMACRegisters_8723D(pDM_Odm, BB_REG, MAC_backup);
#endif	

	
	for (path = 0; path < DP_PATH_NUM; path++) {
		for (index = 0; index < DP_RF_REG_NUM; index++)
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			RF_backup[path][index] = PHY_QueryRFReg(pAdapter, path, RF_REG[index], bMaskDWord);	
#else
			RF_backup[path][index] = ODM_GetRFReg(pAdapter, path, RF_REG[index], bMaskDWord);	
#endif	
	}	
	
	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_SaveADDARegisters_8723D(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#else
		_PHY_SaveADDARegisters_8723D(pDM_Odm, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#endif	
	
	
	for (index = 0; index < IQK_ADDA_REG_NUM ; index++)
		ODM_SetBBReg(pDM_Odm, AFE_REG[index], bMaskDWord, 0x6fdb25a4);

	
	for (index = 0; index < DP_BB_REG_NUM; index++) {
		if (index < 4)
			ODM_SetBBReg(pDM_Odm, BB_REG[index], bMaskDWord, BB_settings[index]);
		else if (index == 4)
			ODM_SetBBReg(pDM_Odm,BB_REG[index], bMaskDWord, BB_backup[index]|BIT10|BIT26);			
		else
			ODM_SetBBReg(pDM_Odm, BB_REG[index], BIT10, 0x00);			
	}

	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_MACSettingCalibration_8723D(pAdapter, MAC_REG, MAC_backup);
#else
	_PHY_MACSettingCalibration_8723D(pDM_Odm, MAC_REG, MAC_backup);
#endif	

	
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00); 		
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);	
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_B, bMaskDWord, 0x01008c00);	
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_B, bMaskDWord, 0x01008c00);	
	
	
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_AC, bMaskDWord, 0x10000);

	
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x400000);	
	ODM_SetBBReg(pDM_Odm, 0xbc0, bMaskDWord, 0x0005361f);		
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);	

	
	for (i = 0; i < 3; i++) {
		
		for (index = 0; index < 3; index++)
			ODM_SetBBReg(pDM_Odm, 0xe00+index*4, bMaskDWord, Tx_AGC[i][0]);			
		ODM_SetBBReg(pDM_Odm,0xe00+index*4, bMaskDWord, Tx_AGC[i][1]);			
		for (index = 0; index < 4; index++)
			ODM_SetBBReg(pDM_Odm, 0xe10+index*4, bMaskDWord, Tx_AGC[i][0]);			
	
		
		ODM_SetBBReg(pDM_Odm, rPdp_AntA, bMaskDWord, 0x02097098);
		ODM_SetBBReg(pDM_Odm, rPdp_AntA_4, bMaskDWord, 0xf76d9f84);
		ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x0004ab87);
		ODM_SetBBReg(pDM_Odm, rConfig_AntA, bMaskDWord, 0x00880000);		
		
		
		ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x80047788);
		ODM_delay_ms(1);
		ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x00047788);
		ODM_delay_ms(50);
	}

	
	for (index = 0; index < 3; index++)		
	ODM_SetBBReg(pDM_Odm, 0xe00+index*4, bMaskDWord, 0x34343434);	
	ODM_SetBBReg(pDM_Odm, 0xe08+index*4, bMaskDWord, 0x03903434);	
	for (index = 0; index < 4; index++)		
	ODM_SetBBReg(pDM_Odm, 0xe10+index*4, bMaskDWord, 0x34343434);	

	
	ODM_SetBBReg(pDM_Odm, rPdp_AntA, bMaskDWord, 0x02017098);
	ODM_SetBBReg(pDM_Odm, rPdp_AntA_4, bMaskDWord, 0xf76d9f84);
	ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x0004ab87);
	ODM_SetBBReg(pDM_Odm, rConfig_AntA, bMaskDWord, 0x00880000);		

	
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x0c, bMaskDWord, 0x8992b);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x0d, bMaskDWord, 0x0e52c); 	
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x00, bMaskDWord, 0x5205a );		

	
	ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x800477c0);
	ODM_delay_ms(1);
	ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x000477c0);
	ODM_delay_ms(50);

	while(RetryCount < DP_RETRY_LIMIT && !pDM_Odm->RFCalibrateInfo.bDPPathAOK)
	{
		
		ODM_SetBBReg(pDM_Odm, rPdp_AntA, bMaskDWord, 0x0c297018);
		tmpReg = ODM_GetBBReg(pDM_Odm, 0xbe0, bMaskDWord);
		ODM_delay_ms(10);
		ODM_SetBBReg(pDM_Odm, rPdp_AntA, bMaskDWord, 0x0c29701f);
		tmpReg2 = ODM_GetBBReg(pDM_Odm, 0xbe8, bMaskDWord);
		ODM_delay_ms(10);

		tmpReg = (tmpReg & bMaskHWord) >> 16;
		tmpReg2 = (tmpReg2 & bMaskHWord) >> 16;		
		if (tmpReg < 0xf0 || tmpReg > 0x105 || tmpReg2 > 0xff) {
			ODM_SetBBReg(pDM_Odm, rPdp_AntA, bMaskDWord, 0x02017098);
		
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x800000);
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);
			ODM_delay_ms(1);
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x800477c0);
			ODM_delay_ms(1);			
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x000477c0);			
			ODM_delay_ms(50);			
			RetryCount++;			
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S1 DPK RetryCount %d 0xbe0[31:16] %x 0xbe8[31:16] %x\n", RetryCount, tmpReg, tmpReg2));										
		} else {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S1 DPK Success\n"));		
			pDM_Odm->RFCalibrateInfo.bDPPathAOK = TRUE;
			break;
		}		
	}
	RetryCount = 0;
	
	
	if (pDM_Odm->RFCalibrateInfo.bDPPathAOK) {	
		
		ODM_SetBBReg(pDM_Odm, rPdp_AntA, bMaskDWord, 0x01017098);
		ODM_SetBBReg(pDM_Odm, rPdp_AntA_4, bMaskDWord, 0x776d9f84);
		ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x0004ab87);
		ODM_SetBBReg(pDM_Odm, rConfig_AntA, bMaskDWord, 0x00880000);
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x400000);

		for (i = rPdp_AntA; i <= 0xb3c; i += 4) {
			ODM_SetBBReg(pDM_Odm, i, bMaskDWord, 0x40004000);	
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path S1 ofsset = 0x%x\n", i));		
		}
		
		
		ODM_SetBBReg(pDM_Odm, 0xb40, bMaskDWord, 0x40404040);	
		ODM_SetBBReg(pDM_Odm, 0xb44, bMaskDWord, 0x28324040);			
		ODM_SetBBReg(pDM_Odm, 0xb48, bMaskDWord, 0x10141920);					

		for  (i = 0xb4c; i <= 0xb5c; i += 4) {
			ODM_SetBBReg(pDM_Odm, i, bMaskDWord, 0x0c0c0c0c);	
		}		

		
		ODM_SetBBReg(pDM_Odm, 0xbc0, bMaskDWord, 0x0005361f);	
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);					
	} else {
		ODM_SetBBReg(pDM_Odm, rPdp_AntA, bMaskDWord, 0x00000000);	
		ODM_SetBBReg(pDM_Odm, rPdp_AntA_4, bMaskDWord, 0x00000000);			
	}

	
	if (is2T) {
		
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_AC, bMaskDWord, 0x10000);
		
		
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x400000);	
		ODM_SetBBReg(pDM_Odm, 0xbc4, bMaskDWord, 0x0005361f);		
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);	

		
		for (i = 0; i < 3; i++) {
			
			for(index = 0; index < 4; index++)
				ODM_SetBBReg(pDM_Odm, 0x830+index*4, bMaskDWord, Tx_AGC[i][0]);			
			for(index = 0; index < 2; index++)
				ODM_SetBBReg(pDM_Odm, 0x848+index*4, bMaskDWord, Tx_AGC[i][0]);			
			for(index = 0; index < 2; index++)
				ODM_SetBBReg(pDM_Odm, 0x868+index*4, bMaskDWord, Tx_AGC[i][0]);			
		
			
			ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x02097098);
			ODM_SetBBReg(pDM_Odm, rPdp_AntB_4, bMaskDWord, 0xf76d9f84);
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x0004ab87);
			ODM_SetBBReg(pDM_Odm, rConfig_AntB, bMaskDWord, 0x00880000);		
			
			
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x80047788);
			ODM_delay_ms(1);
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x00047788);
			ODM_delay_ms(50);
		}

		
		for (index = 0; index < 4; index++)
			ODM_SetBBReg(pDM_Odm, 0x830+index*4, bMaskDWord, 0x34343434);	
		for (index = 0; index < 2; index++)
			ODM_SetBBReg(pDM_Odm, 0x848+index*4, bMaskDWord, 0x34343434);	
		for (index = 0; index < 2; index++)
			ODM_SetBBReg(pDM_Odm, 0x868+index*4, bMaskDWord, 0x34343434);	

		
		ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x02017098);		
		ODM_SetBBReg(pDM_Odm, rPdp_AntB_4, bMaskDWord, 0xf76d9f84);		
		ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x0004ab87);		
		ODM_SetBBReg(pDM_Odm, rConfig_AntB, bMaskDWord, 0x00880000);		

		
		ODM_SetBBReg(pDM_Odm, 0x840, bMaskDWord, 0x0101000f);		
		ODM_SetBBReg(pDM_Odm, 0x840, bMaskDWord, 0x01120103);		

		
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x0c, bMaskDWord, 0x8992b);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x0d, bMaskDWord, 0x0e52c);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_AC, bMaskDWord, 0x5205a); 

		
		ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x800477c0);		
		ODM_delay_ms(1);	
		ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x000477c0);		
		ODM_delay_ms(50);
		
		while(RetryCount < DP_RETRY_LIMIT && !pDM_Odm->RFCalibrateInfo.bDPPathBOK)
		{
			
			ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x0c297018);		
			tmpReg = ODM_GetBBReg(pDM_Odm, 0xbf0, bMaskDWord);
			ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x0c29701f);		
			tmpReg2 = ODM_GetBBReg(pDM_Odm, 0xbf8, bMaskDWord);
			
			tmpReg = (tmpReg & bMaskHWord) >> 16;
			tmpReg2 = (tmpReg2 & bMaskHWord) >> 16;
			
			if (tmpReg < 0xf0 || tmpReg > 0x105 || tmpReg2 > 0xff) {
				ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x02017098);		
			
				ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x800000);
				ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);
				ODM_delay_ms(1);
				ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x800477c0);		
				ODM_delay_ms(1);	
				ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x000477c0);		
				ODM_delay_ms(50);			
				RetryCount++;			
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("path B DPK RetryCount %d 0xbf0[31:16] %x, 0xbf8[31:16] %x\n", RetryCount , tmpReg, tmpReg2));														
			} else {
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path B DPK Success\n"));						
				pDM_Odm->RFCalibrateInfo.bDPPathBOK = TRUE;
				break;
			}						
		}
	
		
		if (pDM_Odm->RFCalibrateInfo.bDPPathBOK) {
			
			ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x01017098);
			ODM_SetBBReg(pDM_Odm, rPdp_AntB_4, bMaskDWord, 0x776d9f84);
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x0004ab87);
			ODM_SetBBReg(pDM_Odm, rConfig_AntB, bMaskDWord, 0x00880000);
			
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x400000);
			for (i = 0xb60; i <= 0xb9c; i += 4) {
				ODM_SetBBReg(pDM_Odm, i, bMaskDWord, 0x40004000);	
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path B ofsset = 0x%x\n", i));
			}

			
			ODM_SetBBReg(pDM_Odm, 0xba0, bMaskDWord, 0x40404040);	
			ODM_SetBBReg(pDM_Odm, 0xba4, bMaskDWord, 0x28324050);			
			ODM_SetBBReg(pDM_Odm, 0xba8, bMaskDWord, 0x0c141920);					

			for (i = 0xbac; i <= 0xbbc; i += 4) {
				ODM_SetBBReg(pDM_Odm, i, bMaskDWord, 0x0c0c0c0c);	
			}		
			
			
			ODM_SetBBReg(pDM_Odm, 0xbc4, bMaskDWord, 0x0005361f);	
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);			
			
		} else {
			ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x00000000);	
			ODM_SetBBReg(pDM_Odm, rPdp_AntB_4, bMaskDWord, 0x00000000);					
		}
	}
	
	
	for (index = 0; index < DP_BB_REG_NUM; index++)
		ODM_SetBBReg(pDM_Odm, BB_REG[index], bMaskDWord, BB_backup[index]);
	
	
	for (path = 0; path < DP_PATH_NUM; path++) {
		for (i = 0 ; i < DP_RF_REG_NUM ; i++) {
			ODM_SetRFReg(pDM_Odm, path, RF_REG[i], bMaskDWord, RF_backup[path][i]);
		}
	}
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_MODE1, bMaskDWord, 0x1000f);	
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_MODE2, bMaskDWord, 0x20101);		

	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_ReloadADDARegisters_8723D(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);	

	
	_PHY_ReloadMACRegisters_8723D(pAdapter, MAC_REG, MAC_backup);
#else
	_PHY_ReloadADDARegisters_8723D(pDM_Odm, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);	

	
	_PHY_ReloadMACRegisters_8723D(pDM_Odm, MAC_REG, MAC_backup);
#endif		

	pDM_Odm->RFCalibrateInfo.bDPdone = TRUE;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("<==phy_DigitalPredistortion_8723D()\n"));
#endif		
}

VOID
PHY_DigitalPredistortion_8723D(
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN	PADAPTER	pAdapter
#else
	IN PDM_ODM_T	pDM_Odm
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
#if DISABLE_BB_RF
	return;
#endif

	return;

	if (pDM_Odm->RFCalibrateInfo.bDPdone)
		return;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

	if (IS_92C_SERIAL(pHalData->VersionID)) {
		phy_DigitalPredistortion_8723D(pAdapter, TRUE);
	}
	else
#endif		
	{
		
		phy_DigitalPredistortion_8723D(pAdapter, FALSE);
	}
}
	




BOOLEAN phy_QueryRFPathSwitch_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN		is2T
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
	

	if (ODM_GetBBReg(pDM_Odm, 0x7C4, bMaskLWord) == 0x7700)
		return TRUE;
	else 
		return FALSE;

}




BOOLEAN PHY_QueryRFPathSwitch_8723D(	
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm
#else
	IN	PADAPTER	pAdapter
#endif
	)
{

#if DISABLE_BB_RF
	return TRUE;
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		return phy_QueryRFPathSwitch_8723D(pAdapter, FALSE);
#else
		return phy_QueryRFPathSwitch_8723D(pDM_Odm, FALSE);
#endif

}
#endif



#else	

VOID
PHY_IQCalibrate_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN 	bReCovery
	){}
VOID
PHY_LCCalibrate_8723D(
	IN PDM_ODM_T		pDM_Odm
	){}

VOID
ODM_TxPwrTrackSetPwr_8723D(
	PDM_ODM_T			pDM_Odm,
	PWRTRACK_METHOD 	Method,
	u1Byte 				RFPath,
	u1Byte 				ChannelMappedIndex
	){}
#endif	
