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
#include "odm_precomp.h"


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
	IN 	PDM_ODM_T		pDM_Odm,
	OUT	PTXPWRTRACK_CFG	pConfig
	)
{
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
	IN PDM_ODM_T		pDM_Odm
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(pDM_Odm->Adapter);
	u1Byte 			p = 0;
	
	pDM_Odm->BbSwingIdxCckBase = pDM_Odm->DefaultCckIndex;
	pDM_Odm->BbSwingIdxCck = pDM_Odm->DefaultCckIndex;
	pDM_Odm->RFCalibrateInfo.CCK_index = 0;
	
	for (p = ODM_RF_PATH_A; p < MAX_RF_PATH; ++p)
	{
		pDM_Odm->BbSwingIdxOfdmBase[p] = pDM_Odm->DefaultOfdmIndex;
		pDM_Odm->BbSwingIdxOfdm[p] = pDM_Odm->DefaultOfdmIndex;
	   	pDM_Odm->RFCalibrateInfo.OFDM_index[p] = pDM_Odm->DefaultOfdmIndex;

		pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p] = 0;
		pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] = 0;
		pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[p] = 0;
		pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p] = 0;

		pDM_Odm->Absolute_OFDMSwingIdx[p] = 0;    // Initial Mix mode power tracking
		pDM_Odm->Remnant_OFDMSwingIdx[p] = 0;			  
	}
	
	pDM_Odm->Modify_TxAGC_Flag_PathA= FALSE;       //Initial at Modify Tx Scaling Mode
	pDM_Odm->Modify_TxAGC_Flag_PathB= FALSE;       //Initial at Modify Tx Scaling Mode
	pDM_Odm->Remnant_CCKSwingIdx= 0;
	pDM_Odm->RFCalibrateInfo.ThermalValue = pHalData->EEPROMThermalMeter;
	pDM_Odm->RFCalibrateInfo.ThermalValue_IQK = pHalData->EEPROMThermalMeter;
	pDM_Odm->RFCalibrateInfo.ThermalValue_LCK = pHalData->EEPROMThermalMeter;	
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
	
	u1Byte			ThermalValue = 0, delta, delta_LCK, delta_IQK, p = 0, i = 0;
	u1Byte			ThermalValue_AVG_count = 0;
	u4Byte			ThermalValue_AVG = 0;	

	u1Byte			OFDM_min_index = 0;  // OFDM BB Swing should be less than +3.0dB, which is required by Arthur
	u1Byte			Indexforchannel = 0; // GetRightChnlPlaceforIQK(pHalData->CurrentChannel)

	TXPWRTRACK_CFG 	c;


	//4 1. The following TWO tables decide the final index of OFDM/CCK swing table.
	pu1Byte			deltaSwingTableIdx_TUP_A;
	pu1Byte			deltaSwingTableIdx_TDOWN_A;
	pu1Byte			deltaSwingTableIdx_TUP_B;
	pu1Byte			deltaSwingTableIdx_TDOWN_B;
	
	//4 2. Initilization ( 7 steps in total )

	ConfigureTxpowerTrack(pDM_Odm, &c);

	(*c.GetDeltaSwingTable)(pDM_Odm, (pu1Byte*)&deltaSwingTableIdx_TUP_A, (pu1Byte*)&deltaSwingTableIdx_TDOWN_A,
									  (pu1Byte*)&deltaSwingTableIdx_TUP_B, (pu1Byte*)&deltaSwingTableIdx_TDOWN_B);	
	
	pDM_Odm->RFCalibrateInfo.TXPowerTrackingCallbackCnt++; //cosa add for debug
	pDM_Odm->RFCalibrateInfo.bTXPowerTrackingInit = TRUE;
    
#if (MP_DRIVER == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	pDM_Odm->RFCalibrateInfo.TxPowerTrackControl = pHalData->TxPowerTrackControl; // <Kordan> We should keep updating the control variable according to HalData.
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if ( *(pDM_Odm->mp_mode) == 1)
#endif
		// <Kordan> RFCalibrateInfo.RegA24 will be initialized when ODM HW configuring, but MP configures with para files.
		pDM_Odm->RFCalibrateInfo.RegA24 = 0x090e1317;
#endif

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("===>ODM_TXPowerTrackingCallback_ThermalMeter, \
		 \n pDM_Odm->BbSwingIdxCckBase: %d, pDM_Odm->BbSwingIdxOfdmBase[A]: %d, pDM_Odm->DefaultOfdmIndex: %d\n", 
		pDM_Odm->BbSwingIdxCckBase, pDM_Odm->BbSwingIdxOfdmBase[ODM_RF_PATH_A], pDM_Odm->DefaultOfdmIndex));

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

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("(delta, delta_LCK, delta_IQK) = (%d, %d, %d)\n", delta, delta_LCK, delta_IQK));
	
	//4 6. If necessary, do LCK.	
	
	if ((delta_LCK >= c.Threshold_IQK)) // Delta temperature is equal to or larger than 20 centigrade.
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_LCK(%d) >= Threshold_IQK(%d)\n", delta_LCK, c.Threshold_IQK));
		pDM_Odm->RFCalibrateInfo.ThermalValue_LCK = ThermalValue;
		if(c.PHY_LCCalibrate)
			(*c.PHY_LCCalibrate)(pDM_Odm);
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
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
                ("deltaSwingTableIdx_TUP_A[%d] = %d\n", delta, deltaSwingTableIdx_TUP_A[delta]));
			pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[ODM_RF_PATH_A] = pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[ODM_RF_PATH_A];
			pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[ODM_RF_PATH_A] = deltaSwingTableIdx_TUP_A[delta];

			pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] =  deltaSwingTableIdx_TUP_A[delta];        // Record delta swing for mix mode power tracking

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Temp is higher and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] = %d\n", pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A]));  

			if(c.RfPathCount > 1)
			{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
	                                     ("deltaSwingTableIdx_TUP_B[%d] = %d\n", delta, deltaSwingTableIdx_TUP_B[delta]));  
			pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[ODM_RF_PATH_B] = pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[ODM_RF_PATH_B];
			pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[ODM_RF_PATH_B] = deltaSwingTableIdx_TUP_B[delta];

			pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_B] =  deltaSwingTableIdx_TUP_B[delta];       // Record delta swing for mix mode power tracking

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Temp is higher and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_B] = %d\n", pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_B]));  
			}
			
        } 
		else {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("deltaSwingTableIdx_TDOWN_A[%d] = %d\n", delta, deltaSwingTableIdx_TDOWN_A[delta]));  

			pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[ODM_RF_PATH_A] = pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[ODM_RF_PATH_A];
			pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[ODM_RF_PATH_A] = -1 * deltaSwingTableIdx_TDOWN_A[delta];

			pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] =  -1 * deltaSwingTableIdx_TDOWN_A[delta];        // Record delta swing for mix mode power tracking

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] = %d\n", pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A]));  

				if(c.RfPathCount > 1)
				{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
                		("deltaSwingTableIdx_TDOWN_B[%d] = %d\n", delta, deltaSwingTableIdx_TDOWN_B[delta]));  
					
			pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[ODM_RF_PATH_B] = pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[ODM_RF_PATH_B];
			pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[ODM_RF_PATH_B] = -1 * deltaSwingTableIdx_TDOWN_B[delta];

			pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_B] =  -1 * deltaSwingTableIdx_TDOWN_B[delta];       // Record delta swing for mix mode power tracking

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_B] = %d\n", pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_B]));  
        }
        }
		
	    for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++) 		
        {
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
                ("\n\n================================ [Path-%c] Calculating PowerIndexOffset ================================\n", (p == ODM_RF_PATH_A ? 'A' : 'B')));  
			
		    if (pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] == pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[p])         // If Thermal value changes but lookup table value still the same
		    	pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p] = 0;
		    else
		    	pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p] = pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] - pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[p];      // Power Index Diff between 2 times Power Tracking

		    ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("[Path-%c] PowerIndexOffset(%d) = DeltaPowerIndex(%d) - DeltaPowerIndexLast(%d)\n", 
                (p == ODM_RF_PATH_A ? 'A' : 'B'), pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p], pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p], 
                pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[p]));		
		
	    	pDM_Odm->RFCalibrateInfo.OFDM_index[p] = pDM_Odm->BbSwingIdxOfdmBase[p] + pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p];
		    pDM_Odm->RFCalibrateInfo.CCK_index = pDM_Odm->BbSwingIdxCckBase + pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p];

		    pDM_Odm->BbSwingIdxCck = pDM_Odm->RFCalibrateInfo.CCK_index;	
			pDM_Odm->BbSwingIdxOfdm[p] = pDM_Odm->RFCalibrateInfo.OFDM_index[p];

			// *************Print BB Swing Base and Index Offset*************

		    ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("The 'CCK' final index(%d) = BaseIndex(%d) + PowerIndexOffset(%d)\n", 
		    	pDM_Odm->BbSwingIdxCck, pDM_Odm->BbSwingIdxCckBase, pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p]));
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("The 'OFDM' final index(%d) = BaseIndex[%c](%d) + PowerIndexOffset(%d)\n", 
		   		pDM_Odm->BbSwingIdxOfdm[p], (p == ODM_RF_PATH_A ? 'A' : 'B'), pDM_Odm->BbSwingIdxOfdmBase[p], pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p]));

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
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
            ("\n\n========================================================================================================\n"));  
		if(pDM_Odm->RFCalibrateInfo.CCK_index > c.SwingTableSize_CCK-1)
			pDM_Odm->RFCalibrateInfo.CCK_index = c.SwingTableSize_CCK-1;
		//else if (pDM_Odm->RFCalibrateInfo.CCK_index < 0)
			//pDM_Odm->RFCalibrateInfo.CCK_index = 0;
	}
	else
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("The thermal meter is unchanged or TxPowerTracking OFF(%d): ThermalValue: %d , pDM_Odm->RFCalibrateInfo.ThermalValue: %d\n", 
			pDM_Odm->RFCalibrateInfo.TxPowerTrackControl, ThermalValue, pDM_Odm->RFCalibrateInfo.ThermalValue));
		
	    for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++) 		
		    pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p] = 0;
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("TxPowerTracking: [CCK] Swing Current Index: %d, Swing Base Index: %d\n", 
		pDM_Odm->RFCalibrateInfo.CCK_index, pDM_Odm->BbSwingIdxCckBase));       //Print Swing base & current
	for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("TxPowerTracking: [OFDM] Swing Current Index: %d, Swing Base Index[%c]: %d\n",
			pDM_Odm->RFCalibrateInfo.OFDM_index[p], (p == ODM_RF_PATH_A ? 'A' : 'B'), pDM_Odm->BbSwingIdxOfdmBase[p]));
	}
	
	if ((pDM_Odm->RFCalibrateInfo.PowerIndexOffset[ODM_RF_PATH_A] != 0 ||
		 pDM_Odm->RFCalibrateInfo.PowerIndexOffset[ODM_RF_PATH_B] != 0 ) && 
     	 pDM_Odm->RFCalibrateInfo.TxPowerTrackControl)
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
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Temperature Increasing(A): delta_pi: %d , delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n", 
				pDM_Odm->RFCalibrateInfo.PowerIndexOffset[ODM_RF_PATH_A], delta, ThermalValue, pHalData->EEPROMThermalMeter, pDM_Odm->RFCalibrateInfo.ThermalValue));	

			if(c.RfPathCount > 1)
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Temperature Increasing(B): delta_pi: %d , delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n", 
				pDM_Odm->RFCalibrateInfo.PowerIndexOffset[ODM_RF_PATH_B], delta, ThermalValue, pHalData->EEPROMThermalMeter, pDM_Odm->RFCalibrateInfo.ThermalValue));	

		}
		else if (ThermalValue < pDM_Odm->RFCalibrateInfo.ThermalValue)// Low temperature
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Temperature Decreasing(A): delta_pi: %d , delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
				pDM_Odm->RFCalibrateInfo.PowerIndexOffset[ODM_RF_PATH_A], delta, ThermalValue, pHalData->EEPROMThermalMeter, pDM_Odm->RFCalibrateInfo.ThermalValue));				

			if(c.RfPathCount > 1)
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Temperature Decreasing(B): delta_pi: %d , delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
				pDM_Odm->RFCalibrateInfo.PowerIndexOffset[ODM_RF_PATH_B], delta, ThermalValue, pHalData->EEPROMThermalMeter, pDM_Odm->RFCalibrateInfo.ThermalValue));				

		}
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		if (ThermalValue > pHalData->EEPROMThermalMeter)
#else
		if (ThermalValue > pDM_Odm->priv->pmib->dot11RFEntry.ther)
#endif
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Temperature(%d) higher than PG value(%d)\n", ThermalValue, pHalData->EEPROMThermalMeter));			

			if (pDM_Odm->SupportICType == ODM_RTL8188E || pDM_Odm->SupportICType == ODM_RTL8192E || 
				pDM_Odm->SupportICType == ODM_RTL8821  || pDM_Odm->SupportICType == ODM_RTL8812  || pDM_Odm->SupportICType == ODM_RTL8723B)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("**********Enter POWER Tracking MIX_MODE**********\n"));
				for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
						(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, MIX_MODE, p, 0);
			}
			else 
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("**********Enter POWER Tracking BBSWING_MODE**********\n"));
				for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
						(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, BBSWING, p, Indexforchannel);
			}
		}
		else
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Temperature(%d) lower than PG value(%d)\n", ThermalValue, pHalData->EEPROMThermalMeter));

			if (pDM_Odm->SupportICType == ODM_RTL8188E || pDM_Odm->SupportICType == ODM_RTL8192E || 
				pDM_Odm->SupportICType == ODM_RTL8821  || pDM_Odm->SupportICType == ODM_RTL8812  || pDM_Odm->SupportICType == ODM_RTL8723B)
			{
	            ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("**********Enter POWER Tracking MIX_MODE**********\n"));
				for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
					(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, MIX_MODE, p, Indexforchannel);
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("**********Enter POWER Tracking BBSWING_MODE**********\n"));
				for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
					(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, BBSWING, p, Indexforchannel);
			}
			
		}

		pDM_Odm->BbSwingIdxCckBase = pDM_Odm->BbSwingIdxCck;   // Record last time Power Tracking result as base.
		for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
				pDM_Odm->BbSwingIdxOfdmBase[p] = pDM_Odm->BbSwingIdxOfdm[p];

	 	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("pDM_Odm->RFCalibrateInfo.ThermalValue = %d ThermalValue= %d\n", pDM_Odm->RFCalibrateInfo.ThermalValue, ThermalValue));
		
		pDM_Odm->RFCalibrateInfo.ThermalValue = ThermalValue;         //Record last Power Tracking Thermal Value

	}
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	// Delta temperature is equal to or larger than 20 centigrade (When threshold is 8).
	if ((delta_IQK >= c.Threshold_IQK)) {		
		if ( ! pDM_Odm->RFCalibrateInfo.bIQKInProgress) 
			(*c.DoIQK)(pDM_Odm, delta_IQK, ThermalValue, 8);
	}
#endif		
			
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("<===ODM_TXPowerTrackingCallback_ThermalMeter\n"));
	
	pDM_Odm->RFCalibrateInfo.TXPowercount = 0;
}




//3============================================================
//3 IQ Calibration
//3============================================================

VOID
ODM_ResetIQKResult(
	IN PDM_ODM_T	pDM_Odm 
)
{
	u1Byte		i;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN || DM_ODM_SUPPORT_TYPE == ODM_CE)
	PADAPTER	Adapter = pDM_Odm->Adapter;

	if (!IS_HARDWARE_TYPE_8192D(Adapter))
		return;
#endif
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("PHY_ResetIQKResult:: settings regs %d default regs %d\n", (u4Byte)(sizeof(pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting)/sizeof(IQK_MATRIX_REGS_SETTING)), IQK_Matrix_Settings_NUM));
	//0xe94, 0xe9c, 0xea4, 0xeac, 0xeb4, 0xebc, 0xec4, 0xecc

	for(i = 0; i < IQK_Matrix_Settings_NUM; i++)
	{
		{
			pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[i].Value[0][0] = 
				pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[i].Value[0][2] = 
				pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[i].Value[0][4] = 
				pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[i].Value[0][6] = 0x100;

			pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[i].Value[0][1] = 
				pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[i].Value[0][3] = 
				pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[i].Value[0][5] = 
				pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[i].Value[0][7] = 0x0;

			pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[i].bIQKDone = FALSE;

		}
	}

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

