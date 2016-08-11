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
#include "phydm_precomp.h"


#define 	CALCULATE_SWINGTALBE_OFFSET(_offset, _direction, _size, _deltaThermal) \
					do {\
						for(_offset = 0; _offset < _size; _offset++)\
						{\
							if(_deltaThermal < thermalThreshold[_direction][_offset])\
							{\
								if(_offset != 0)\
									_offset--;\
								break;\
							}\
						}			\
						if(_offset >= _size)\
							_offset = _size-1;\
					} while(0)

void ConfigureTxpowerTrack(
	IN		PVOID					pDM_VOID,
	OUT	PTXPWRTRACK_CFG	pConfig
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

#if RTL8192E_SUPPORT
	if(pDM_Odm->SupportICType==ODM_RTL8192E)
		ConfigureTxpowerTrack_8192E(pConfig);
#endif	
#if RTL8821A_SUPPORT
	if(pDM_Odm->SupportICType==ODM_RTL8821)
		ConfigureTxpowerTrack_8821A(pConfig);
#endif
#if RTL8812A_SUPPORT
	if(pDM_Odm->SupportICType==ODM_RTL8812)
		ConfigureTxpowerTrack_8812A(pConfig);
#endif
#if RTL8188E_SUPPORT
	if(pDM_Odm->SupportICType==ODM_RTL8188E)
		ConfigureTxpowerTrack_8188E(pConfig);
#endif 

#if RTL8723B_SUPPORT
	if(pDM_Odm->SupportICType==ODM_RTL8723B)
		ConfigureTxpowerTrack_8723B(pConfig);
#endif

#if RTL8814A_SUPPORT
	if (pDM_Odm->SupportICType == ODM_RTL8814A)
		ConfigureTxpowerTrack_8814A(pConfig);
#endif

#if RTL8703B_SUPPORT
	if(pDM_Odm->SupportICType==ODM_RTL8703B)
		ConfigureTxpowerTrack_8703B(pConfig);
#endif

#if RTL8188F_SUPPORT
	if (pDM_Odm->SupportICType == ODM_RTL8188F)
		ConfigureTxpowerTrack_8188F(pConfig);
#endif 
}

//======================================================================
// <20121113, Kordan> This function should be called when TxAGC changed.
// Otherwise the previous compensation is gone, because we record the 
// delta of temperature between two TxPowerTracking watch dogs.
//
// NOTE: If Tx BB swing or Tx scaling is varified during run-time, still 
//       need to call this function.
//======================================================================
VOID
ODM_ClearTxPowerTrackingState(
	IN		PVOID					pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(pDM_Odm->Adapter);
	u1Byte 			p = 0;
	PODM_RF_CAL_T	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
	
	pRFCalibrateInfo->BbSwingIdxCckBase = pRFCalibrateInfo->DefaultCckIndex;
	pRFCalibrateInfo->BbSwingIdxCck = pRFCalibrateInfo->DefaultCckIndex;
	pDM_Odm->RFCalibrateInfo.CCK_index = 0;
	
	for (p = ODM_RF_PATH_A; p < MAX_RF_PATH; ++p)
	{
		pRFCalibrateInfo->BbSwingIdxOfdmBase[p] = pRFCalibrateInfo->DefaultOfdmIndex;
		pRFCalibrateInfo->BbSwingIdxOfdm[p] = pRFCalibrateInfo->DefaultOfdmIndex;
		pRFCalibrateInfo->OFDM_index[p] = pRFCalibrateInfo->DefaultOfdmIndex;

		pRFCalibrateInfo->PowerIndexOffset[p] = 0;
		pRFCalibrateInfo->DeltaPowerIndex[p] = 0;
		pRFCalibrateInfo->DeltaPowerIndexLast[p] = 0;
		pRFCalibrateInfo->PowerIndexOffset[p] = 0;

		pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = 0;    /* Initial Mix mode power tracking*/
		pRFCalibrateInfo->Remnant_OFDMSwingIdx[p] = 0;			  
		pRFCalibrateInfo->KfreeOffset[p] = 0;
	}
	
	pRFCalibrateInfo->Modify_TxAGC_Flag_PathA = FALSE;       /*Initial at Modify Tx Scaling Mode*/
	pRFCalibrateInfo->Modify_TxAGC_Flag_PathB = FALSE;       /*Initial at Modify Tx Scaling Mode*/
	pRFCalibrateInfo->Modify_TxAGC_Flag_PathC = FALSE;       /*Initial at Modify Tx Scaling Mode*/
	pRFCalibrateInfo->Modify_TxAGC_Flag_PathD = FALSE;       /*Initial at Modify Tx Scaling Mode*/
	pRFCalibrateInfo->Remnant_CCKSwingIdx = 0;
	pRFCalibrateInfo->ThermalValue = pHalData->EEPROMThermalMeter;
	
	pRFCalibrateInfo->Modify_TxAGC_Value_CCK=0;			//modify by Mingzhi.Guo
	pRFCalibrateInfo->Modify_TxAGC_Value_OFDM=0;		//modify by Mingzhi.Guo
}

VOID
ODM_TXPowerTrackingCallback_ThermalMeter(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm
#else
	IN PADAPTER	Adapter
#endif
	)
{

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
#endif
	PODM_RF_CAL_T	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	u1Byte			ThermalValue = 0, delta, delta_LCK, delta_IQK, p = 0, i = 0;
	s1Byte			diff_DPK[4] = {0};
	u1Byte			ThermalValue_AVG_count = 0;
	u4Byte			ThermalValue_AVG = 0;	

	u1Byte			OFDM_min_index = 0;  // OFDM BB Swing should be less than +3.0dB, which is required by Arthur
	u1Byte			Indexforchannel = 0; // GetRightChnlPlaceforIQK(pHalData->CurrentChannel)
	BOOLEAN			bTSSIenable = FALSE;

	TXPWRTRACK_CFG 	c;

	//4 1. The following TWO tables decide the final index of OFDM/CCK swing table.
	pu1Byte			deltaSwingTableIdx_TUP_A;
	pu1Byte			deltaSwingTableIdx_TDOWN_A;
	pu1Byte			deltaSwingTableIdx_TUP_B;
	pu1Byte			deltaSwingTableIdx_TDOWN_B;
	/*for 8814 add by Yu Chen*/
	pu1Byte			deltaSwingTableIdx_TUP_C;
	pu1Byte			deltaSwingTableIdx_TDOWN_C;
	pu1Byte			deltaSwingTableIdx_TUP_D;
	pu1Byte			deltaSwingTableIdx_TDOWN_D;
	
	//4 2. Initilization ( 7 steps in total )

	ConfigureTxpowerTrack(pDM_Odm, &c);

	(*c.GetDeltaSwingTable)(pDM_Odm, (pu1Byte*)&deltaSwingTableIdx_TUP_A, (pu1Byte*)&deltaSwingTableIdx_TDOWN_A,
									  (pu1Byte*)&deltaSwingTableIdx_TUP_B, (pu1Byte*)&deltaSwingTableIdx_TDOWN_B);	
	
	if (pDM_Odm->SupportICType & ODM_RTL8814A)	/*for 8814 path C & D*/
		(*c.GetDeltaSwingTable8814only)(pDM_Odm, (pu1Byte *)&deltaSwingTableIdx_TUP_C, (pu1Byte *)&deltaSwingTableIdx_TDOWN_C,
			(pu1Byte *)&deltaSwingTableIdx_TUP_D, (pu1Byte *)&deltaSwingTableIdx_TDOWN_D);
	
	
	pDM_Odm->RFCalibrateInfo.TXPowerTrackingCallbackCnt++; //cosa add for debug
	pDM_Odm->RFCalibrateInfo.bTXPowerTrackingInit = TRUE;
    
#if (MP_DRIVER == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	pDM_Odm->RFCalibrateInfo.TxPowerTrackControl = pHalData->TxPowerTrackControl; // <Kordan> We should keep updating the control variable according to HalData.
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (pDM_Odm->mp_mode == TRUE)
#endif
		// <Kordan> RFCalibrateInfo.RegA24 will be initialized when ODM HW configuring, but MP configures with para files.
		pDM_Odm->RFCalibrateInfo.RegA24 = 0x090e1317;
#endif

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, 
		("===>ODM_TXPowerTrackingCallback_ThermalMeter Start\n pRFCalibrateInfo->BbSwingIdxCckBase: %d, pRFCalibrateInfo->BbSwingIdxOfdmBase[A]: %d, pRFCalibrateInfo->DefaultOfdmIndex: %d\n", 
		pRFCalibrateInfo->BbSwingIdxCckBase, pRFCalibrateInfo->BbSwingIdxOfdmBase[ODM_RF_PATH_A], pRFCalibrateInfo->DefaultOfdmIndex));

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, 
		("pDM_Odm->RFCalibrateInfo.TxPowerTrackControl %d,  pHalData->EEPROMThermalMeter %d\n", pDM_Odm->RFCalibrateInfo.TxPowerTrackControl,  pHalData->EEPROMThermalMeter));
	ThermalValue = (u1Byte)ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, c.ThermalRegAddr, 0xfc00);	//0x42: RF Reg[15:10] 88E
	if( ! pDM_Odm->RFCalibrateInfo.TxPowerTrackControl || pHalData->EEPROMThermalMeter == 0 || 
		pHalData->EEPROMThermalMeter == 0xFF)
		return;


	//4 3. Initialize ThermalValues of RFCalibrateInfo

	if(pDM_Odm->RFCalibrateInfo.bReloadtxpowerindex)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("reload ofdm index for band switch\n"));				
	}

	//4 4. Calculate average thermal meter
	
	pDM_Odm->RFCalibrateInfo.ThermalValue_AVG[pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index] = ThermalValue;
	pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index++;
	if(pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index == c.AverageThermalNum)   //Average times =  c.AverageThermalNum
		pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index = 0;

	for(i = 0; i < c.AverageThermalNum; i++)
	{
		if(pDM_Odm->RFCalibrateInfo.ThermalValue_AVG[i])
		{
			ThermalValue_AVG += pDM_Odm->RFCalibrateInfo.ThermalValue_AVG[i];
			ThermalValue_AVG_count++;
		}
	}

	if(ThermalValue_AVG_count)               //Calculate Average ThermalValue after average enough times
	{
		ThermalValue = (u1Byte)(ThermalValue_AVG / ThermalValue_AVG_count);
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("AVG Thermal Meter = 0x%X, EFUSE Thermal Base = 0x%X\n", ThermalValue, pHalData->EEPROMThermalMeter));					
	}
			
	//4 5. Calculate delta, delta_LCK, delta_IQK.

	//"delta" here is used to determine whether thermal value changes or not.
	delta 	  = (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue)?(ThermalValue - pDM_Odm->RFCalibrateInfo.ThermalValue):(pDM_Odm->RFCalibrateInfo.ThermalValue - ThermalValue);
	delta_LCK = (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue_LCK)?(ThermalValue - pDM_Odm->RFCalibrateInfo.ThermalValue_LCK):(pDM_Odm->RFCalibrateInfo.ThermalValue_LCK - ThermalValue);
	delta_IQK = (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue_IQK)?(ThermalValue - pDM_Odm->RFCalibrateInfo.ThermalValue_IQK):(pDM_Odm->RFCalibrateInfo.ThermalValue_IQK - ThermalValue);

	if (pDM_Odm->RFCalibrateInfo.ThermalValue_IQK == 0xff) {	/*no PG, use thermal value for IQK*/
		pDM_Odm->RFCalibrateInfo.ThermalValue_IQK = ThermalValue;
		delta_IQK = (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue_IQK)?(ThermalValue - pDM_Odm->RFCalibrateInfo.ThermalValue_IQK):(pDM_Odm->RFCalibrateInfo.ThermalValue_IQK - ThermalValue);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("no PG, use ThermalValue for IQK\n"));
	}
	
	for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
		diff_DPK[p] = (s1Byte)ThermalValue - (s1Byte)pDM_Odm->RFCalibrateInfo.DpkThermal[p];

	/*4 6. If necessary, do LCK.*/	
	if (!(pDM_Odm->SupportICType & ODM_RTL8821)) {

		if (pDM_Odm->RFCalibrateInfo.ThermalValue_LCK == 0xff) {
			/*no PG , do LCK at initial status*/
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("no PG, do LCK\n"));
			pDM_Odm->RFCalibrateInfo.ThermalValue_LCK = ThermalValue;
			if (c.PHY_LCCalibrate)
				(*c.PHY_LCCalibrate)(pDM_Odm);
			delta_LCK = (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue_LCK)?(ThermalValue - pDM_Odm->RFCalibrateInfo.ThermalValue_LCK):(pDM_Odm->RFCalibrateInfo.ThermalValue_LCK - ThermalValue);
		}

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("(delta, delta_LCK, delta_IQK) = (%d, %d, %d)\n", delta, delta_LCK, delta_IQK));
		/*DBG_871X("(delta, delta_LCK, delta_IQK) = (%d, %d, %d), %d\n", delta, delta_LCK, delta_IQK, c.Threshold_IQK);*/
		
		/* 4 6. If necessary, do LCK.*/
		
		if (delta_LCK >= c.Threshold_IQK) {
			/* Delta temperature is equal to or larger than 20 centigrade.*/
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_LCK(%d) >= Threshold_IQK(%d)\n", delta_LCK, c.Threshold_IQK));
			pDM_Odm->RFCalibrateInfo.ThermalValue_LCK = ThermalValue;
			if (c.PHY_LCCalibrate)
				(*c.PHY_LCCalibrate)(pDM_Odm);
		}
	}
	//3 7. If necessary, move the index of swing table to adjust Tx power.	
	
	if (delta > 0 && pDM_Odm->RFCalibrateInfo.TxPowerTrackControl)
	{
		//"delta" here is used to record the absolute value of differrence.
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))			
	    delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);		
#else
	    delta = (ThermalValue > pDM_Odm->priv->pmib->dot11RFEntry.ther)?(ThermalValue - pDM_Odm->priv->pmib->dot11RFEntry.ther):(pDM_Odm->priv->pmib->dot11RFEntry.ther - ThermalValue);		
#endif
		if (delta >= TXPWR_TRACK_TABLE_SIZE)
			delta = TXPWR_TRACK_TABLE_SIZE - 1;

		//4 7.1 The Final Power Index = BaseIndex + PowerIndexOffset
		
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))				
		if(ThermalValue > pHalData->EEPROMThermalMeter) {
#else
		if(ThermalValue > pDM_Odm->priv->pmib->dot11RFEntry.ther) {
#endif
			for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)  {
				pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[p] = pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p];	/* recording power index offset */
				switch (p) {
				case ODM_RF_PATH_B:
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("deltaSwingTableIdx_TUP_B[%d] = %d\n", delta, deltaSwingTableIdx_TUP_B[delta])); 

					pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] = deltaSwingTableIdx_TUP_B[delta];
					pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = deltaSwingTableIdx_TUP_B[delta];       /* Record delta swing for mix mode power tracking */
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is higher and pRFCalibrateInfo->Absolute_OFDMSwingIdx[ODM_RF_PATH_B] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));  

					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Temp is higher and pRFCalibrateInfo->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[ODM_RF_PATH_A]));  


				break;

				case ODM_RF_PATH_C:
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("deltaSwingTableIdx_TUP_C[%d] = %d\n", delta, deltaSwingTableIdx_TUP_C[delta]));
			
					pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] = deltaSwingTableIdx_TUP_C[delta];
					pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] =  deltaSwingTableIdx_TUP_C[delta];       /* Record delta swing for mix mode power tracking */
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is higher and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_C] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));  
				break;

				case ODM_RF_PATH_D:
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("deltaSwingTableIdx_TUP_D[%d] = %d\n", delta, deltaSwingTableIdx_TUP_D[delta]));

					pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] = deltaSwingTableIdx_TUP_D[delta];
					pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] =  deltaSwingTableIdx_TUP_D[delta];       /* Record delta swing for mix mode power tracking */
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is higher and pRFCalibrateInfo->Absolute_OFDMSwingIdx[ODM_RF_PATH_D] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));  
				break;

				default:
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, 
						("deltaSwingTableIdx_TUP_A[%d] = %d\n", delta, deltaSwingTableIdx_TUP_A[delta]));

					pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] = deltaSwingTableIdx_TUP_A[delta];
					pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = deltaSwingTableIdx_TUP_A[delta];        /* Record delta swing for mix mode power tracking */
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is higher and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));
				break;
				}		
			}
		} else {
			for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++) {
				pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[p] = pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p];	/* recording poer index offset */
				switch (p) {
				case ODM_RF_PATH_B:
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("deltaSwingTableIdx_TDOWN_B[%d] = %d\n", delta, deltaSwingTableIdx_TDOWN_B[delta]));  
					pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] = -1 * deltaSwingTableIdx_TDOWN_B[delta];
					pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = -1 * deltaSwingTableIdx_TDOWN_B[delta];        /* Record delta swing for mix mode power tracking */
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_B] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p])); 
				break;
					
				case ODM_RF_PATH_C:
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("deltaSwingTableIdx_TDOWN_C[%d] = %d\n", delta, deltaSwingTableIdx_TDOWN_C[delta]));  
					pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] = -1 * deltaSwingTableIdx_TDOWN_C[delta];
					pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = -1 * deltaSwingTableIdx_TDOWN_C[delta];        /* Record delta swing for mix mode power tracking */
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_C] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));   
				break;

				case ODM_RF_PATH_D:
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("deltaSwingTableIdx_TDOWN_D[%d] = %d\n", delta, deltaSwingTableIdx_TDOWN_D[delta]));  
					pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] = -1 * deltaSwingTableIdx_TDOWN_D[delta];
					pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] =  -1 * deltaSwingTableIdx_TDOWN_D[delta];        /* Record delta swing for mix mode power tracking */
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_D] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));  
				break;

				default:
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("deltaSwingTableIdx_TDOWN_A[%d] = %d\n", delta, deltaSwingTableIdx_TDOWN_A[delta]));  
					pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] = -1 * deltaSwingTableIdx_TDOWN_A[delta];
					pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] =  -1 * deltaSwingTableIdx_TDOWN_A[delta];        /* Record delta swing for mix mode power tracking */
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));  
				break;
				}	
			}
		}
		
		for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++) {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("\n\n=========================== [Path-%d] Calculating PowerIndexOffset===========================\n", p));  
			if (pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] == pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[p])         /* If Thermal value changes but lookup table value still the same */
				pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p] = 0;
			else
				pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p] = pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] - pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[p];      /* Power Index Diff between 2 times Power Tracking */

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("[Path-%d] PowerIndexOffset(%d) = DeltaPowerIndex(%d) - DeltaPowerIndexLast(%d)\n", p, pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p], pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p], pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[p]));		
		
			pDM_Odm->RFCalibrateInfo.OFDM_index[p] = pRFCalibrateInfo->BbSwingIdxOfdmBase[p] + pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p];
			pDM_Odm->RFCalibrateInfo.CCK_index = pRFCalibrateInfo->BbSwingIdxCckBase + pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p];

			pRFCalibrateInfo->BbSwingIdxCck = pDM_Odm->RFCalibrateInfo.CCK_index;	
			pRFCalibrateInfo->BbSwingIdxOfdm[p] = pDM_Odm->RFCalibrateInfo.OFDM_index[p];



			/* *************Print BB Swing Base and Index Offset************* */

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("The 'CCK' final index(%d) = BaseIndex(%d) + PowerIndexOffset(%d)\n", pRFCalibrateInfo->BbSwingIdxCck, pRFCalibrateInfo->BbSwingIdxCckBase, pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p]));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("The 'OFDM' final index(%d) = BaseIndex[%d](%d) + PowerIndexOffset(%d)\n", pRFCalibrateInfo->BbSwingIdxOfdm[p], p, pRFCalibrateInfo->BbSwingIdxOfdmBase[p], pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p]));

		    //4 7.1 Handle boundary conditions of index.
		
			if(pDM_Odm->RFCalibrateInfo.OFDM_index[p] > c.SwingTableSize_OFDM-1)
			{
				pDM_Odm->RFCalibrateInfo.OFDM_index[p] = c.SwingTableSize_OFDM-1;
			}
			else if (pDM_Odm->RFCalibrateInfo.OFDM_index[p] < OFDM_min_index)
			{
				pDM_Odm->RFCalibrateInfo.OFDM_index[p] = OFDM_min_index;
			}
		}
		
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("\n\n========================================================================================================\n"));  
		if(pDM_Odm->RFCalibrateInfo.CCK_index > c.SwingTableSize_CCK-1)
			pDM_Odm->RFCalibrateInfo.CCK_index = c.SwingTableSize_CCK-1;
		else if (pDM_Odm->RFCalibrateInfo.CCK_index <= 0)
			pDM_Odm->RFCalibrateInfo.CCK_index = 0;
	} else {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("The thermal meter is unchanged or TxPowerTracking OFF(%d): ThermalValue: %d , pDM_Odm->RFCalibrateInfo.ThermalValue: %d\n", 
			pDM_Odm->RFCalibrateInfo.TxPowerTrackControl, ThermalValue, pDM_Odm->RFCalibrateInfo.ThermalValue));
		
	    for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++) 		
		    pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p] = 0;
	}

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("TxPowerTracking: [CCK] Swing Current Index: %d, Swing Base Index: %d\n", 
		pDM_Odm->RFCalibrateInfo.CCK_index, pRFCalibrateInfo->BbSwingIdxCckBase));       /*Print Swing base & current*/
			
	for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("TxPowerTracking: [OFDM] Swing Current Index: %d, Swing Base Index[%d]: %d\n",
			pDM_Odm->RFCalibrateInfo.OFDM_index[p], p, pRFCalibrateInfo->BbSwingIdxOfdmBase[p]));
	}
	
	if ((pDM_Odm->RFCalibrateInfo.PowerIndexOffset[ODM_RF_PATH_A] != 0 ||
		pDM_Odm->RFCalibrateInfo.PowerIndexOffset[ODM_RF_PATH_B] != 0 ||
		pDM_Odm->RFCalibrateInfo.PowerIndexOffset[ODM_RF_PATH_C] != 0 ||
		pDM_Odm->RFCalibrateInfo.PowerIndexOffset[ODM_RF_PATH_D] != 0) && 
		pDM_Odm->RFCalibrateInfo.TxPowerTrackControl && (pHalData->EEPROMThermalMeter != 0xff))
	{
		//4 7.2 Configure the Swing Table to adjust Tx Power.
		
		pDM_Odm->RFCalibrateInfo.bTxPowerChanged = TRUE; // Always TRUE after Tx Power is adjusted by power tracking.			
		//
		// 2012/04/23 MH According to Luke's suggestion, we can not write BB digital
		// to increase TX power. Otherwise, EVM will be bad.
		//
		// 2012/04/25 MH Add for tx power tracking to set tx power in tx agc for 88E.
		if (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue)
		{
			for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++) {
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("Temperature Increasing(%d): delta_pi: %d , delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n", 
					p, pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p], delta, ThermalValue, pHalData->EEPROMThermalMeter, pDM_Odm->RFCalibrateInfo.ThermalValue));	
			}
		}
		else if (ThermalValue < pDM_Odm->RFCalibrateInfo.ThermalValue)// Low temperature
		{
			for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++) {
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("Temperature Decreasing(%d): delta_pi: %d , delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
					p, pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p], delta, ThermalValue, pHalData->EEPROMThermalMeter, pDM_Odm->RFCalibrateInfo.ThermalValue));				
			}
		}

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		if (ThermalValue > pHalData->EEPROMThermalMeter)
#else
		if (ThermalValue > pDM_Odm->priv->pmib->dot11RFEntry.ther)
#endif
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Temperature(%d) higher than PG value(%d)\n", ThermalValue, pHalData->EEPROMThermalMeter));			

			if (pDM_Odm->SupportICType == ODM_RTL8188E || pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8821 ||
				pDM_Odm->SupportICType == ODM_RTL8812 || pDM_Odm->SupportICType == ODM_RTL8723B || pDM_Odm->SupportICType == ODM_RTL8814A ||
				pDM_Odm->SupportICType == ODM_RTL8822B || pDM_Odm->SupportICType == ODM_RTL8188F || pDM_Odm->SupportICType == ODM_RTL8703B) {
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter POWER Tracking MIX_MODE**********\n"));
				for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
						(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, MIX_MODE, p, 0);
			}
			else 
			{
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter POWER Tracking BBSWING_MODE**********\n"));
				for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
						(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, BBSWING, p, Indexforchannel);
			}
		}
		else
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Temperature(%d) lower than PG value(%d)\n", ThermalValue, pHalData->EEPROMThermalMeter));

			if (pDM_Odm->SupportICType == ODM_RTL8188E || pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8821 ||
				pDM_Odm->SupportICType == ODM_RTL8812 || pDM_Odm->SupportICType == ODM_RTL8723B || pDM_Odm->SupportICType == ODM_RTL8814A ||
				pDM_Odm->SupportICType == ODM_RTL8822B || pDM_Odm->SupportICType == ODM_RTL8188F || pDM_Odm->SupportICType == ODM_RTL8703B) {
			
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter POWER Tracking MIX_MODE**********\n"));
				for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
					(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, MIX_MODE, p, Indexforchannel);
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter POWER Tracking BBSWING_MODE**********\n"));
				for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
					(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, BBSWING, p, Indexforchannel);
			}
			
		}

		pRFCalibrateInfo->BbSwingIdxCckBase = pRFCalibrateInfo->BbSwingIdxCck;    /*Record last time Power Tracking result as base.*/
		for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
				pRFCalibrateInfo->BbSwingIdxOfdmBase[p] = pRFCalibrateInfo->BbSwingIdxOfdm[p];

	 	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("pDM_Odm->RFCalibrateInfo.ThermalValue = %d ThermalValue= %d\n", pDM_Odm->RFCalibrateInfo.ThermalValue, ThermalValue));
		
		pDM_Odm->RFCalibrateInfo.ThermalValue = ThermalValue;         /*Record last Power Tracking Thermal Value*/

	}

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		
	if (!IS_HARDWARE_TYPE_8723B(Adapter) && !IS_HARDWARE_TYPE_8192E(Adapter) && !IS_HARDWARE_TYPE_8703B(Adapter)) {
		/* Delta temperature is equal to or larger than 20 centigrade (When threshold is 8).*/
		if (delta_IQK >= c.Threshold_IQK) {
			if (!pDM_Odm->RFCalibrateInfo.bIQKInProgress) 
				(*c.DoIQK)(pDM_Odm, delta_IQK, ThermalValue, 8);
		}
	}	
	if (!(pDM_Odm->SupportICType & ODM_RTL8814A)) {
		if (pDM_Odm->RFCalibrateInfo.DpkThermal[ODM_RF_PATH_A] != 0) {
			if (diff_DPK[ODM_RF_PATH_A] >= c.Threshold_DPK) { 
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1);
				ODM_SetBBReg(pDM_Odm, 0xcc4, BIT14|BIT13|BIT12|BIT11|BIT10, (diff_DPK[ODM_RF_PATH_A] / c.Threshold_DPK));
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0);
			} else if ((diff_DPK[ODM_RF_PATH_A] <= -1 * c.Threshold_DPK)) {
				s4Byte value = 0x20 + (diff_DPK[ODM_RF_PATH_A] / c.Threshold_DPK);

				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1);
				ODM_SetBBReg(pDM_Odm, 0xcc4, BIT14|BIT13|BIT12|BIT11|BIT10, value);
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0);
			} else {
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1);
				ODM_SetBBReg(pDM_Odm, 0xcc4, BIT14|BIT13|BIT12|BIT11|BIT10, 0);
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0);	
			}
		}
		if (pDM_Odm->RFCalibrateInfo.DpkThermal[ODM_RF_PATH_B] != 0) {
			if (diff_DPK[ODM_RF_PATH_B] >= c.Threshold_DPK) { 
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1);
				ODM_SetBBReg(pDM_Odm, 0xec4, BIT14|BIT13|BIT12|BIT11|BIT10, (diff_DPK[ODM_RF_PATH_B] / c.Threshold_DPK));
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0);
			} else if ((diff_DPK[ODM_RF_PATH_B] <= -1 * c.Threshold_DPK)) {
				s4Byte value = 0x20 + (diff_DPK[ODM_RF_PATH_B] / c.Threshold_DPK);	
				
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1);
				ODM_SetBBReg(pDM_Odm, 0xec4, BIT14|BIT13|BIT12|BIT11|BIT10, value);
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0);
			} else {
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x1);
				ODM_SetBBReg(pDM_Odm, 0xec4, BIT14|BIT13|BIT12|BIT11|BIT10, 0);
				ODM_SetBBReg(pDM_Odm, 0x82c, BIT(31), 0x0);	
			}
		}
	}

#endif		
			
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("<===ODM_TXPowerTrackingCallback_ThermalMeter End\n"));
	
	pDM_Odm->RFCalibrateInfo.TXPowercount = 0;
}


//3============================================================
//3 IQ Calibration
//3============================================================

VOID
ODM_ResetIQKResult(
	IN		PVOID					pDM_VOID
)
{
	return;

}
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
u1Byte ODM_GetRightChnlPlaceforIQK(u1Byte chnl)
{
	u1Byte	channel_all[ODM_TARGET_CHNL_NUM_2G_5G] = 
	{1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130,132,134,136,138,140,149,151,153,155,157,159,161,163,165};
	u1Byte	place = chnl;

	
	if(chnl > 14)
	{
		for(place = 14; place<sizeof(channel_all); place++)
		{
			if(channel_all[place] == chnl)
			{
				return place-13;
			}
		}
	}	
	return 0;

}
#endif

VOID
odm_IQCalibrate(
		IN	PDM_ODM_T	pDM_Odm 
		)
{
	PADAPTER	Adapter = pDM_Odm->Adapter;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	
	if (*pDM_Odm->pIsFcsModeEnable)
		return;
#endif
	
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))		
	if (!IS_HARDWARE_TYPE_JAGUAR(Adapter))
		return;
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	else if (IS_HARDWARE_TYPE_8812AU(Adapter))
		return;
#endif
#endif
	
#if (RTL8821A_SUPPORT == 1)
	if (pDM_Odm->bLinked) {
		if ((*pDM_Odm->pChannel != pDM_Odm->preChannel) && (!*pDM_Odm->pbScanInProcess)) {
			pDM_Odm->preChannel = *pDM_Odm->pChannel;
			pDM_Odm->LinkedInterval = 0;
		}

		if (pDM_Odm->LinkedInterval < 3)
			pDM_Odm->LinkedInterval++;
		
		if (pDM_Odm->LinkedInterval == 2) {
			/*Mark out IQK flow to prevent tx stuck. by Maddest 20130306*/
			/*Open it verified by James 20130715*/
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
			/*Change channel will do IQK , cancel duplicate doIQK by YiWei*/
			/*PHY_IQCalibrate_8821A(pDM_Odm, FALSE);*/
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			PHY_IQCalibrate(Adapter, FALSE);
#else
			PHY_IQCalibrate_8821A(Adapter, FALSE);
#endif
		}
	} else
		pDM_Odm->LinkedInterval = 0;
#endif
}

void phydm_rf_init(IN	PVOID		pDM_VOID)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	odm_TXPowerTrackingInit(pDM_Odm);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	ODM_ClearTxPowerTrackingState(pDM_Odm);	
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#if (RTL8814A_SUPPORT == 1)		
	if (pDM_Odm->SupportICType & ODM_RTL8814A)
		PHY_IQCalibrate_8814A_Init(pDM_Odm);
#endif	
#endif

}

void phydm_rf_watchdog(IN	PVOID		pDM_VOID)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	ODM_TXPowerTrackingCheck(pDM_Odm);
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
		odm_IQCalibrate(pDM_Odm);
#endif
}
