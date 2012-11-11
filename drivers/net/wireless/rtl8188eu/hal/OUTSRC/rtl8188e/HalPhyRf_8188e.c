
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
#define		ODM_TXPWRTRACK_MAX_IDX_88E		6

/*---------------------------Define Local Constant---------------------------*/


//3============================================================
//3 Tx Power Tracking
//3============================================================
/*-----------------------------------------------------------------------------
 * Function:	ODM_TxPwrTrackAdjust88E()
 *
 * Overview:	88E we can not write 0xc80/c94/c4c/ 0xa2x. Instead of write TX agc.
 *				No matter OFDM & CCK use the same method.
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
 *	04/23/2012	MHC		Adjust TX agc directly not throughput BB digital.
 *
 *---------------------------------------------------------------------------*/
VOID
ODM_TxPwrTrackAdjust88E(
	PDM_ODM_T	pDM_Odm,
	u1Byte		Type,				// 0 = OFDM, 1 = CCK
	pu1Byte		pDirection,			// 1 = +(increase) 2 = -(decrease)
	pu4Byte		pOutWriteVal		// Tx tracking CCK/OFDM BB swing index adjust
	)
{
	u1Byte	pwr_value = 0;
	//
	// Tx power tracking BB swing table. 
	// The base index = 12. +((12-n)/2)dB 13~?? = decrease tx pwr by -((n-12)/2)dB
	//
	if (Type == 0)		// For OFDM afjust
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, 
		("BbSwingIdxOfdm = %d BbSwingFlagOfdm=%d\n", pDM_Odm->BbSwingIdxOfdm, pDM_Odm->BbSwingFlagOfdm));

		if (pDM_Odm->BbSwingIdxOfdm <= pDM_Odm->BbSwingIdxOfdmBase)
		{
			*pDirection 	= 1;
			pwr_value 		= (pDM_Odm->BbSwingIdxOfdmBase - pDM_Odm->BbSwingIdxOfdm);
		}
		else
		{
			*pDirection 	= 2;
			pwr_value 		= (pDM_Odm->BbSwingIdxOfdm - pDM_Odm->BbSwingIdxOfdmBase);
		}
		
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, 
		("BbSwingIdxOfdm = %d BbSwingFlagOfdm=%d\n", pDM_Odm->BbSwingIdxOfdm, pDM_Odm->BbSwingFlagOfdm));
		
	}
	else if (Type == 1)	// For CCK adjust.
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, 
		("pDM_Odm->BbSwingIdxCck = %d pDM_Odm->BbSwingIdxCckBase = %d\n", pDM_Odm->BbSwingIdxCck, pDM_Odm->BbSwingIdxCckBase));

		if (pDM_Odm->BbSwingIdxCck <= pDM_Odm->BbSwingIdxCckBase)
		{
			*pDirection 	= 1;
			pwr_value 		= (pDM_Odm->BbSwingIdxCckBase - pDM_Odm->BbSwingIdxCck);
		}
		else
		{
			*pDirection 	= 2;
			pwr_value 		= (pDM_Odm->BbSwingIdxCck - pDM_Odm->BbSwingIdxCckBase);
		}
	}

	//
	// 2012/04/25 MH According to Ed/Luke.Lees estimate for EVM the max tx power tracking
	// need to be less than 6 power index for 88E.
	//
	if (pwr_value >= ODM_TXPWRTRACK_MAX_IDX_88E && *pDirection == 1)
		pwr_value = ODM_TXPWRTRACK_MAX_IDX_88E;

	*pOutWriteVal = pwr_value | (pwr_value<<8) | (pwr_value<<16) | (pwr_value<<24);

}	// ODM_TxPwrTrackAdjust88E


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
odm_TxPwrTrackSetPwr88E(
	PDM_ODM_T	pDM_Odm
	)
{
	if (pDM_Odm->BbSwingFlagOfdm == TRUE || pDM_Odm->BbSwingFlagCck == TRUE)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("odm_TxPwrTrackSetPwr88E CH=%d\n", *(pDM_Odm->pChannel)));
#if (DM_ODM_SUPPORT_TYPE & (ODM_MP|ODM_CE ))
		PHY_SetTxPowerLevel8188E(pDM_Odm->Adapter, *(pDM_Odm->pChannel));
#endif
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
		PHY_RF6052SetCCKTxPower(pDM_Odm->priv, *(pDM_Odm->pChannel));
		PHY_RF6052SetOFDMTxPower(pDM_Odm->priv, *(pDM_Odm->pChannel));
#endif
		pDM_Odm->BbSwingFlagOfdm 	= FALSE;
		pDM_Odm->BbSwingFlagCck	= FALSE;
	}
}	// odm_TxPwrTrackSetPwr88E


//091212 chiyokolin
VOID
odm_TXPowerTrackingCallback_ThermalMeter_8188E(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm
#else
	IN PADAPTER	Adapter
#endif
	)
{

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	//PMGNT_INFO      		pMgntInfo = &Adapter->MgntInfo;
#endif
	
	u1Byte			ThermalValue = 0, delta, delta_LCK, delta_IQK, offset;
	u1Byte			ThermalValue_AVG_count = 0;
	u4Byte			ThermalValue_AVG = 0;	
	s4Byte			ele_A=0, ele_D, TempCCk, X, value32;
	s4Byte			Y, ele_C=0;
	s1Byte			OFDM_index[2], CCK_index=0, OFDM_index_old[2]={0,0}, CCK_index_old=0, index;
	u4Byte			i = 0, j = 0;
	BOOLEAN 		is2T = FALSE;
	BOOLEAN 		bInteralPA = FALSE;

	u1Byte			OFDM_min_index = 6, rf; //OFDM BB Swing should be less than +3.0dB, which is required by Arthur
	u1Byte			Indexforchannel = 0/*GetRightChnlPlaceforIQK(pHalData->CurrentChannel)*/;
	s1Byte			OFDM_index_mapping[2][index_mapping_NUM_88E] = { 
					{0,	0,	2,	3,	4,	4,			//2.4G, decrease power 
					5, 	6, 	7, 	7,	8,	9,					
					10,	10,	11}, // For lower temperature, 20120220 updated on 20120220.	
					{0,	0,	-1,	-2,	-3,	-4,			//2.4G, increase power 
					-4, 	-4, 	-4, 	-5,	-7,	-8,					
					-9,	-9,	-10},					
					};	
	u1Byte			Thermal_mapping[2][index_mapping_NUM_88E] = { 
					{0,	2,	4,	6,	8,	10,			//2.4G, decrease power 
					12, 	14, 	16, 	18,	20,	22,					
					24,	26,	27},	
					{0, 	2,	4,	6,	8,	10, 			//2.4G,, increase power 
					12, 	14, 	16, 	18, 	20, 	22, 
					25,	25,	25},					
					};		
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif

	// 2012/04/25 MH Add for tx power tracking to set tx power in tx agc for 88E.
	odm_TxPwrTrackSetPwr88E(pDM_Odm);
	
	pDM_Odm->RFCalibrateInfo.TXPowerTrackingCallbackCnt++; //cosa add for debug
	pDM_Odm->RFCalibrateInfo.bTXPowerTrackingInit = TRUE;
    
#if (MP_DRIVER == 1)      
#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
    pDM_Odm->RFCalibrateInfo.TxPowerTrackControl = pHalData->TxPowerTrackControl; // <Kordan> We should keep updating the control variable according to HalData.
#endif

    // <Kordan> RFCalibrateInfo.RegA24 will be initialized when ODM HW configuring, but MP configures with para files.
    pDM_Odm->RFCalibrateInfo.RegA24 = 0x090e1317; 
#endif

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("===>dm_TXPowerTrackingCallback_ThermalMeter_8188E txpowercontrol %d\n",  pDM_Odm->RFCalibrateInfo.TxPowerTrackControl));

	ThermalValue = (u1Byte)ODM_GetRFReg(pDM_Odm, RF_PATH_A, RF_T_METER_88E, 0xfc00);	//0x42: RF Reg[15:10] 88E

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", ThermalValue, pDM_Odm->RFCalibrateInfo.ThermalValue, pHalData->EEPROMThermalMeter));

	if(is2T)
		rf = 2;
	else
		rf = 1;
	
	if(ThermalValue)
	{
//		if(!pHalData->ThermalValue)
		{
			//Query OFDM path A default setting 		
			ele_D = ODM_GetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, bMaskDWord)&bMaskOFDM_D;
			for(i=0; i<OFDM_TABLE_SIZE_92D; i++)	//find the index
			{
				if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
				{
					OFDM_index_old[0] = (u1Byte)i;
					pDM_Odm->BbSwingIdxOfdmBase = (u1Byte)i;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("Initial pathA ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n", 
						rOFDM0_XATxIQImbalance, ele_D, OFDM_index_old[0]));
					break;
				}
			}

			//Query OFDM path B default setting 
			if(is2T)
			{
				ele_D = ODM_GetBBReg(pDM_Odm, rOFDM0_XBTxIQImbalance, bMaskDWord)&bMaskOFDM_D;
				for(i=0; i<OFDM_TABLE_SIZE_92D; i++)	//find the index
				{
					if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
					{
						OFDM_index_old[1] = (u1Byte)i;
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("Initial pathB ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n", 
							rOFDM0_XBTxIQImbalance, ele_D, OFDM_index_old[1]));
						break;
					}
				}
			}
			
#if (DM_ODM_SUPPORT_TYPE &  ODM_AP)
			CCK_index_old = get_CCK_swing_index(pDM_Odm->priv);
#else			
			{
				//Query CCK default setting From 0xa24
				TempCCk = pDM_Odm->RFCalibrateInfo.RegA24;

				for(i=0 ; i<CCK_TABLE_SIZE ; i++)
				{
					if(pDM_Odm->RFCalibrateInfo.bCCKinCH14)
					{
						if(ODM_CompareMemory(pDM_Odm,(void*)&TempCCk, (void*)&CCKSwingTable_Ch14[i][2], 4)==0)
						{
							CCK_index_old =(u1Byte) i;
							pDM_Odm->BbSwingIdxCckBase = (u1Byte)i;
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch 14 %d\n", 
								rCCK0_TxFilter2, TempCCk, CCK_index_old, pDM_Odm->RFCalibrateInfo.bCCKinCH14));
							break;
						}
					}
					else
					{
                        ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("RegA24: 0x%X, CCKSwingTable_Ch1_Ch13[%d][2]: CCKSwingTable_Ch1_Ch13[i][2]: 0x%X\n", TempCCk, i, CCKSwingTable_Ch1_Ch13[i][2]));
						if(ODM_CompareMemory(pDM_Odm,(void*)&TempCCk, (void*)&CCKSwingTable_Ch1_Ch13[i][2], 4)==0)
						{
							CCK_index_old =(u1Byte) i;
							pDM_Odm->BbSwingIdxCckBase = (u1Byte)i;
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch14 %d\n", 
								rCCK0_TxFilter2, TempCCk, CCK_index_old, pDM_Odm->RFCalibrateInfo.bCCKinCH14));
							break;
						}			
					}
				}
			}
#endif

			if(!pDM_Odm->RFCalibrateInfo.ThermalValue)
			{
#if (DM_ODM_SUPPORT_TYPE & (ODM_MP|ODM_CE))			
				pDM_Odm->RFCalibrateInfo.ThermalValue = pHalData->EEPROMThermalMeter;
#else
				pDM_Odm->RFCalibrateInfo.ThermalValue = pDM_Odm->priv->pmib->dot11RFEntry.ther;
#endif
				pDM_Odm->RFCalibrateInfo.ThermalValue_LCK = ThermalValue;				
				pDM_Odm->RFCalibrateInfo.ThermalValue_IQK = ThermalValue;								
				
				for(i = 0; i < rf; i++)
					pDM_Odm->RFCalibrateInfo.OFDM_index[i] = OFDM_index_old[i];
				pDM_Odm->RFCalibrateInfo.CCK_index = CCK_index_old;
			}			

			if(pDM_Odm->RFCalibrateInfo.bReloadtxpowerindex)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("reload ofdm index for band switch\n"));				
			}

			//calculate average thermal meter
			{
				pDM_Odm->RFCalibrateInfo.ThermalValue_AVG[pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index] = ThermalValue;
				pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index++;
				if(pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index == AVG_THERMAL_NUM_88E)
					pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index = 0;

				for(i = 0; i < AVG_THERMAL_NUM_88E; i++)
				{
					if(pDM_Odm->RFCalibrateInfo.ThermalValue_AVG[i])
					{
						ThermalValue_AVG += pDM_Odm->RFCalibrateInfo.ThermalValue_AVG[i];
						ThermalValue_AVG_count++;
					}
				}

				if(ThermalValue_AVG_count)
				{
					ThermalValue = (u1Byte)(ThermalValue_AVG / ThermalValue_AVG_count);
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("AVG Thermal Meter = 0x%x \n", ThermalValue));					
				}
			}			
		}

		if(pDM_Odm->RFCalibrateInfo.bReloadtxpowerindex)
		{
#if (DM_ODM_SUPPORT_TYPE & (ODM_MP|ODM_CE))			
			delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);				
#else
			delta = (ThermalValue > pDM_Odm->priv->pmib->dot11RFEntry.ther)?(ThermalValue - pDM_Odm->priv->pmib->dot11RFEntry.ther):(pDM_Odm->priv->pmib->dot11RFEntry.ther - ThermalValue);	
#endif
			pDM_Odm->RFCalibrateInfo.bReloadtxpowerindex = FALSE;	
			pDM_Odm->RFCalibrateInfo.bDoneTxpower = FALSE;
		}
		else if(pDM_Odm->RFCalibrateInfo.bDoneTxpower)
		{
			delta = (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue)?(ThermalValue - pDM_Odm->RFCalibrateInfo.ThermalValue):(pDM_Odm->RFCalibrateInfo.ThermalValue - ThermalValue);
		}
		else
		{
#if (DM_ODM_SUPPORT_TYPE & (ODM_MP|ODM_CE))			
			delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);		
#else
			delta = (ThermalValue > pDM_Odm->priv->pmib->dot11RFEntry.ther)?(ThermalValue - (pDM_Odm->priv->pmib->dot11RFEntry.ther)):((pDM_Odm->priv->pmib->dot11RFEntry.ther) - ThermalValue);				
#endif		
		}
		delta_LCK = (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue_LCK)?(ThermalValue - pDM_Odm->RFCalibrateInfo.ThermalValue_LCK):(pDM_Odm->RFCalibrateInfo.ThermalValue_LCK - ThermalValue);
		delta_IQK = (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue_IQK)?(ThermalValue - pDM_Odm->RFCalibrateInfo.ThermalValue_IQK):(pDM_Odm->RFCalibrateInfo.ThermalValue_IQK - ThermalValue);
#if (DM_ODM_SUPPORT_TYPE & (ODM_MP|ODM_CE))	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x delta 0x%x delta_LCK 0x%x delta_IQK 0x%x \n",   ThermalValue, pDM_Odm->RFCalibrateInfo.ThermalValue, pHalData->EEPROMThermalMeter, delta, delta_LCK, delta_IQK));
#else
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x delta 0x%x delta_LCK 0x%x delta_IQK 0x%x \n",   ThermalValue, pDM_Odm->RFCalibrateInfo.ThermalValue, pDM_Odm->priv->pmib->dot11RFEntry.ther, delta, delta_LCK, delta_IQK));
#endif
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("pre thermal meter LCK 0x%x pre thermal meter IQK 0x%x delta_LCK_bound 0x%x delta_IQK_bound 0x%x\n",   pDM_Odm->RFCalibrateInfo.ThermalValue_LCK, pDM_Odm->RFCalibrateInfo.ThermalValue_IQK, pDM_Odm->RFCalibrateInfo.Delta_LCK, pDM_Odm->RFCalibrateInfo.Delta_IQK));

		
		
		//if((delta_LCK > pHalData->Delta_LCK) && (pHalData->Delta_LCK != 0))
		if ((delta_LCK >= 8)) // Delta temperature is equal to or larger than 20 centigrade.
		{
			pDM_Odm->RFCalibrateInfo.ThermalValue_LCK = ThermalValue;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			PHY_LCCalibrate_8188E(Adapter);
#else
			PHY_LCCalibrate_8188E(pDM_Odm);
#endif
		}
		
		if(delta > 0 && pDM_Odm->RFCalibrateInfo.TxPowerTrackControl)
		{
#if (DM_ODM_SUPPORT_TYPE & (ODM_MP|ODM_CE))			
			delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);		
#else
			delta = (ThermalValue > pDM_Odm->priv->pmib->dot11RFEntry.ther)?(ThermalValue - pDM_Odm->priv->pmib->dot11RFEntry.ther):(pDM_Odm->priv->pmib->dot11RFEntry.ther - ThermalValue);		
#endif
			//calculate new OFDM / CCK offset	
			{
				{							
#if (DM_ODM_SUPPORT_TYPE & (ODM_MP|ODM_CE))				
					if(ThermalValue > pHalData->EEPROMThermalMeter)
#else
					if(ThermalValue > pDM_Odm->priv->pmib->dot11RFEntry.ther)
#endif
						j = 1;
					else
						j = 0;

					for(offset = 0; offset < index_mapping_NUM_88E; offset++)
					{
						if(delta < Thermal_mapping[j][offset])
						{
							if(offset != 0)
								offset--;
							break;
						}
					}			
					if(offset >= index_mapping_NUM_88E)
						offset = index_mapping_NUM_88E-1;
					
					index = OFDM_index_mapping[j][offset];					
					
					for(i = 0; i < rf; i++) 		
						OFDM_index[i] = pDM_Odm->RFCalibrateInfo.OFDM_index[i] + OFDM_index_mapping[j][offset];
					CCK_index = pDM_Odm->RFCalibrateInfo.CCK_index + OFDM_index_mapping[j][offset];					
				}				
				
				if(is2T)
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("temp OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
						pDM_Odm->RFCalibrateInfo.OFDM_index[0], pDM_Odm->RFCalibrateInfo.OFDM_index[1], pDM_Odm->RFCalibrateInfo.CCK_index));			
				}
				else
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("temp OFDM_A_index=0x%x, CCK_index=0x%x\n", 
						pDM_Odm->RFCalibrateInfo.OFDM_index[0], pDM_Odm->RFCalibrateInfo.CCK_index)); 		
				}
				
				for(i = 0; i < rf; i++)
				{
					if(OFDM_index[i] > OFDM_TABLE_SIZE_92D-1)
					{
						OFDM_index[i] = OFDM_TABLE_SIZE_92D-1;
					}
					else if (OFDM_index[i] < OFDM_min_index)
					{
						OFDM_index[i] = OFDM_min_index;
					}
				}

				{
					if(CCK_index > CCK_TABLE_SIZE-1)
						CCK_index = CCK_TABLE_SIZE-1;
					else if (CCK_index < 0)
						CCK_index = 0;
				}

				if(is2T)
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("new OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
						OFDM_index[0], OFDM_index[1], CCK_index));
				}
				else
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("new OFDM_A_index=0x%x, CCK_index=0x%x\n", 
						OFDM_index[0], CCK_index)); 
				}
			}

			//2 temporarily remove bNOPG
			//Config by SwingTable
			if(pDM_Odm->RFCalibrateInfo.TxPowerTrackControl /*&& !pHalData->bNOPG*/)			
			{
				pDM_Odm->RFCalibrateInfo.bDoneTxpower = TRUE;			

				//Adujst OFDM Ant_A according to IQK result
				ele_D = (OFDMSwingTable[(u1Byte)OFDM_index[0]] & 0xFFC00000)>>22;		
				X = pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].Value[0][0];
				Y = pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].Value[0][1];

				//
				// Revse TX power table.
				//
				pDM_Odm->BbSwingIdxOfdm 	= (u1Byte)OFDM_index[0];
				pDM_Odm->BbSwingIdxCck 		= (u1Byte)CCK_index;
				
				//DbgPrint("pDM_Odm->BbSwingIdxOfdm = %d\n", pDM_Odm->BbSwingIdxOfdm);
				
				if (pDM_Odm->BbSwingIdxOfdmCurrent != pDM_Odm->BbSwingIdxOfdm)
				{
					pDM_Odm->BbSwingIdxOfdmCurrent = pDM_Odm->BbSwingIdxOfdm;
					pDM_Odm->BbSwingFlagOfdm		= TRUE;
				}

				if (pDM_Odm->BbSwingIdxCckCurrent != pDM_Odm->BbSwingIdxCck)
				{
					pDM_Odm->BbSwingIdxCckCurrent = pDM_Odm->BbSwingIdxCck;
					pDM_Odm->BbSwingFlagCck = TRUE;
				}
				
				if(X != 0)
				{
					if ((X & 0x00000200) != 0)
						X = X | 0xFFFFFC00;
					ele_A = ((X * ele_D)>>8)&0x000003FF;
						
					//new element C = element D x Y
					if ((Y & 0x00000200) != 0)
						Y = Y | 0xFFFFFC00;
					ele_C = ((Y * ele_D)>>8)&0x000003FF;
					
					//
					// 2012/04/23 MH According to Luke's suggestion, we can not write BB digital
					// to increase TX power. Otherwise, EVM will be bad.
					//
#if 0
					//wirte new elements A, C, D to regC80 and regC94, element B is always 0
					value32 = (ele_D<<22)|((ele_C&0x3F)<<16)|ele_A;
					ODM_SetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, bMaskDWord, value32);

					value32 = (ele_C&0x000003C0)>>6;
					ODM_SetBBReg(pDM_Odm, rOFDM0_XCTxAFE, bMaskH4Bits, value32);

					value32 = ((X * ele_D)>>7)&0x01;
					ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, BIT24, value32);
#endif					
				}
				else
				{
#if 0
					ODM_SetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable[(u1Byte)OFDM_index[0]]);				
					ODM_SetBBReg(pDM_Odm, rOFDM0_XCTxAFE, bMaskH4Bits, 0x00);
					ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, BIT24, 0x00);			
#endif
				}

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TxPwrTracking for path A: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x 0xe94 = 0x%x 0xe9c = 0x%x\n", 
					(u4Byte)X, (u4Byte)Y, (u4Byte)ele_A, (u4Byte)ele_C, (u4Byte)ele_D, (u4Byte)X, (u4Byte)Y)); 	
				
				{
					//
					// 2012/04/23 MH According to Luke's suggestion, we can not write BB digital
					// to increase TX power. Otherwise, EVM will be bad.
					//
#if 0
					//Adjust CCK according to IQK result
					if(!pDM_Odm->RFCalibrateInfo.bCCKinCH14){
						ODM_Write1Byte(pDM_Odm, 0xa22, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][0]);
						ODM_Write1Byte(pDM_Odm, 0xa23, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][1]);
						ODM_Write1Byte(pDM_Odm, 0xa24, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][2]);
						ODM_Write1Byte(pDM_Odm, 0xa25, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][3]);
						ODM_Write1Byte(pDM_Odm, 0xa26, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][4]);
						ODM_Write1Byte(pDM_Odm, 0xa27, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][5]);
						ODM_Write1Byte(pDM_Odm, 0xa28, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][6]);
						ODM_Write1Byte(pDM_Odm, 0xa29, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][7]);		
					}
					else{
						ODM_Write1Byte(pDM_Odm, 0xa22, CCKSwingTable_Ch14[(u1Byte)CCK_index][0]);
						ODM_Write1Byte(pDM_Odm, 0xa23, CCKSwingTable_Ch14[(u1Byte)CCK_index][1]);
						ODM_Write1Byte(pDM_Odm, 0xa24, CCKSwingTable_Ch14[(u1Byte)CCK_index][2]);
						ODM_Write1Byte(pDM_Odm, 0xa25, CCKSwingTable_Ch14[(u1Byte)CCK_index][3]);
						ODM_Write1Byte(pDM_Odm, 0xa26, CCKSwingTable_Ch14[(u1Byte)CCK_index][4]);
						ODM_Write1Byte(pDM_Odm, 0xa27, CCKSwingTable_Ch14[(u1Byte)CCK_index][5]);
						ODM_Write1Byte(pDM_Odm, 0xa28, CCKSwingTable_Ch14[(u1Byte)CCK_index][6]);
						ODM_Write1Byte(pDM_Odm, 0xa29, CCKSwingTable_Ch14[(u1Byte)CCK_index][7]);	
					}		
#endif
				}
				
				if(is2T)
				{						
					ele_D = (OFDMSwingTable[(u1Byte)OFDM_index[1]] & 0xFFC00000)>>22;
					
					//new element A = element D x X
					X = pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].Value[0][4];
					Y = pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].Value[0][5];
					
					//if(X != 0 && pHalData->CurrentBandType92D == ODM_BAND_ON_2_4G)
					if((X != 0) && (*(pDM_Odm->pBandType) == ODM_BAND_2_4G))
						
					{
						if ((X & 0x00000200) != 0)	//consider minus
							X = X | 0xFFFFFC00;
						ele_A = ((X * ele_D)>>8)&0x000003FF;
						
						//new element C = element D x Y
						if ((Y & 0x00000200) != 0)
							Y = Y | 0xFFFFFC00;
						ele_C = ((Y * ele_D)>>8)&0x00003FF;
						
						//wirte new elements A, C, D to regC88 and regC9C, element B is always 0
						value32=(ele_D<<22)|((ele_C&0x3F)<<16) |ele_A;
						ODM_SetBBReg(pDM_Odm, rOFDM0_XBTxIQImbalance, bMaskDWord, value32);

						value32 = (ele_C&0x000003C0)>>6;
						ODM_SetBBReg(pDM_Odm, rOFDM0_XDTxAFE, bMaskH4Bits, value32);	
						
						value32 = ((X * ele_D)>>7)&0x01;
						ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, BIT28, value32);

					}
					else
					{
						ODM_SetBBReg(pDM_Odm, rOFDM0_XBTxIQImbalance, bMaskDWord, OFDMSwingTable[(u1Byte)OFDM_index[1]]);										
						ODM_SetBBReg(pDM_Odm, rOFDM0_XDTxAFE, bMaskH4Bits, 0x00);	
						ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, BIT28, 0x00);				
					}

					ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TxPwrTracking path B: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x 0xeb4 = 0x%x 0xebc = 0x%x\n", 
						(u4Byte)X, (u4Byte)Y, (u4Byte)ele_A, (u4Byte)ele_C, (u4Byte)ele_D, (u4Byte)X, (u4Byte)Y));			
				}
				
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("TxPwrTracking 0xc80 = 0x%x, 0xc94 = 0x%x RF 0x24 = 0x%x\n", ODM_GetBBReg(pDM_Odm, 0xc80, bMaskDWord), ODM_GetBBReg(pDM_Odm, 0xc94, bMaskDWord), ODM_GetRFReg(pDM_Odm, RF_PATH_A, 0x24, bRFRegOffsetMask)));
			}			
		}
		
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	
		// if((delta_IQK > pHalData->Delta_IQK) && (pHalData->Delta_IQK != 0))
		if ((delta_IQK >= 8)) // Delta temperature is equal to or larger than 20 centigrade.
		{
			ODM_ResetIQKResult(pDM_Odm);		

#if(DM_ODM_SUPPORT_TYPE  & ODM_MP)
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
			PHY_IQCalibrate_8188E(Adapter, FALSE);

#if(DM_ODM_SUPPORT_TYPE  & ODM_MP)
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
#endif
		//update thermal meter value
		if(pDM_Odm->RFCalibrateInfo.TxPowerTrackControl)
		{
			//Adapter->HalFunc.SetHalDefVarHandler(Adapter, HAL_DEF_THERMAL_VALUE, &ThermalValue);
			pDM_Odm->RFCalibrateInfo.ThermalValue = ThermalValue;
		}
			
	}

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,("<===dm_TXPowerTrackingCallback_ThermalMeter_8188E\n"));
	
	pDM_Odm->RFCalibrateInfo.TXPowercount = 0;

}





//1 7.	IQK
#define MAX_TOLERANCE		5
#define IQK_DELAY_TIME		1		//ms

u1Byte			//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
phy_PathA_IQK_8188E(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN		configPathB
	)
{
	u4Byte regEAC, regE94, regE9C, regEA4;
	u1Byte result = 0x00;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A IQK!\n"));

    //1 Tx IQK
	//path-A IQK setting
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path-A IQK setting!\n"));
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1c);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x30008c1c);
	ODM_SetBBReg(pDM_Odm, rTx_IQK_PI_A, bMaskDWord, 0x8214032a);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_PI_A, bMaskDWord, 0x28160000);

	//LO calibration setting
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("LO calibration setting!\n"));
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);

	//One shot, path A LOK & IQK
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("One shot, path A LOK & IQK!\n"));
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	// delay x ms
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME_88E));
	//PlatformStallExecution(IQK_DELAY_TIME_88E*1000);
	ODM_delay_ms(IQK_DELAY_TIME_88E);

	// Check failed
	regEAC = ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xeac = 0x%x\n", regEAC));
	regE94 = ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xe94 = 0x%x\n", regE94));
	regE9C= ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xe9c = 0x%x\n", regE9C));
	regEA4= ODM_GetBBReg(pDM_Odm, rRx_Power_Before_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xea4 = 0x%x\n", regEA4));

	if(!(regEAC & BIT28) &&		
		(((regE94 & 0x03FF0000)>>16) != 0x142) &&
		(((regE9C & 0x03FF0000)>>16) != 0x42) )
		result |= 0x01;
	else							//if Tx not OK, ignore Rx
		return result;

#if 0
	if(!(regEAC & BIT27) &&		//if Tx is OK, check whether Rx is OK
		(((regEA4 & 0x03FF0000)>>16) != 0x132) &&
		(((regEAC & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		RTPRINT(FINIT, INIT_IQK, ("Path A Rx IQK fail!!\n"));
#endif	

	return result;


	}

u1Byte			//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
phy_PathA_RxIQK(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN		configPathB
	)
{
	u4Byte regEAC, regE94, regE9C, regEA4, u4tmp;
	u1Byte result = 0x00;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A Rx IQK!\n"));

	//1 Get TXIMR setting
	//modify RXIQK mode table
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path-A Rx IQK modify RXIQK mode table!\n"));
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x00000000);	
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_WE_LUT, bRFRegOffsetMask, 0x800a0 );
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_RCK_OS, bRFRegOffsetMask, 0x30000 );
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_TXPA_G1, bRFRegOffsetMask, 0x0000f );
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_TXPA_G2, bRFRegOffsetMask, 0xf117B );
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x80800000);
	
	//IQK setting
	ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, 0x01007c00);
	ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x81004800);

	//path-A IQK setting
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1c);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x30008c1c);
	ODM_SetBBReg(pDM_Odm, rTx_IQK_PI_A, bMaskDWord, 0x82160804);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_PI_A, bMaskDWord, 0x28160000);	

	//LO calibration setting
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("LO calibration setting!\n"));
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);

	//One shot, path A LOK & IQK
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("One shot, path A LOK & IQK!\n"));
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	// delay x ms
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME_88E));
	//PlatformStallExecution(IQK_DELAY_TIME_88E*1000);
	ODM_delay_ms(IQK_DELAY_TIME_88E);
	

	// Check failed
	regEAC = ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xeac = 0x%x\n", regEAC));	
	regE94 = ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xe94 = 0x%x\n", regE94));
	regE9C= ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xe9c = 0x%x\n", regE9C));

	if(!(regEAC & BIT28) &&		
		(((regE94 & 0x03FF0000)>>16) != 0x142) &&
		(((regE9C & 0x03FF0000)>>16) != 0x42) )
		result |= 0x01;
	else							//if Tx not OK, ignore Rx
		return result;

	u4tmp = 0x80007C00 | (regE94&0x3FF0000)  | ((regE9C&0x3FF0000) >> 16);	
	ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, u4tmp);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xe40 = 0x%x u4tmp = 0x%x \n", ODM_GetBBReg(pDM_Odm, rTx_IQK, bMaskDWord), u4tmp));	
	

	//1 RX IQK
	//modify RXIQK mode table
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path-A Rx IQK modify RXIQK mode table 2!\n"));
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x00000000);		
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_WE_LUT, bRFRegOffsetMask, 0x800a0 );
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_RCK_OS, bRFRegOffsetMask, 0x30000 );
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_TXPA_G1, bRFRegOffsetMask, 0x0000f );
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_TXPA_G2, bRFRegOffsetMask, 0xf7ffa );
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x80800000);

	//IQK setting
	ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x01004800);

	//path-A IQK setting
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x30008c1c);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x10008c1c);
	ODM_SetBBReg(pDM_Odm, rTx_IQK_PI_A, bMaskDWord, 0x82160c05);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_PI_A, bMaskDWord, 0x28160c05);	

	//LO calibration setting
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("LO calibration setting!\n"));
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);

	//One shot, path A LOK & IQK
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("One shot, path A LOK & IQK!\n"));
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	// delay x ms
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME_88E));
	//PlatformStallExecution(IQK_DELAY_TIME_88E*1000);
	ODM_delay_ms(IQK_DELAY_TIME_88E);

	// Check failed
	regEAC = ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xeac = 0x%x\n", regEAC));
	regE94 = ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xe94 = 0x%x\n", regE94));
	regE9C= ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xe9c = 0x%x\n", regE9C));
	regEA4= ODM_GetBBReg(pDM_Odm, rRx_Power_Before_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xea4 = 0x%x\n", regEA4));

#if 0	
	if(!(regEAC & BIT28) &&		
		(((regE94 & 0x03FF0000)>>16) != 0x142) &&
		(((regE9C & 0x03FF0000)>>16) != 0x42) )
		result |= 0x01;
	else							//if Tx not OK, ignore Rx
		return result;
#endif	

	if(!(regEAC & BIT27) &&		//if Tx is OK, check whether Rx is OK
		(((regEA4 & 0x03FF0000)>>16) != 0x132) &&
		(((regEAC & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path A Rx IQK fail!!\n"));
	
	return result;


}

u1Byte				//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
phy_PathB_IQK_8188E(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm
#else
	IN	PADAPTER	pAdapter
#endif
	)
{
	u4Byte regEAC, regEB4, regEBC, regEC4, regECC;
	u1Byte	result = 0x00;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path B IQK!\n"));

	//One shot, path B LOK & IQK
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("One shot, path A LOK & IQK!\n"));
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

	// delay x ms
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Delay %d ms for One shot, path B LOK & IQK.\n", IQK_DELAY_TIME_88E));
	//PlatformStallExecution(IQK_DELAY_TIME_88E*1000);
	ODM_delay_ms(IQK_DELAY_TIME_88E);
	
	// Check failed
	regEAC = ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xeac = 0x%x\n", regEAC));
	regEB4 = ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_B, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xeb4 = 0x%x\n", regEB4));
	regEBC= ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_B, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xebc = 0x%x\n", regEBC));
	regEC4= ODM_GetBBReg(pDM_Odm, rRx_Power_Before_IQK_B_2, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xec4 = 0x%x\n", regEC4));
	regECC= ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_B_2, bMaskDWord);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xecc = 0x%x\n", regECC));

	if(!(regEAC & BIT31) &&
		(((regEB4 & 0x03FF0000)>>16) != 0x142) &&
		(((regEBC & 0x03FF0000)>>16) != 0x42))
		result |= 0x01;
	else
		return result;

	if(!(regEAC & BIT30) &&
		(((regEC4 & 0x03FF0000)>>16) != 0x132) &&
		(((regECC & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path B Rx IQK fail!!\n"));
	

	return result;

}

VOID
_PHY_PathAFillIQKMatrix(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN  BOOLEAN    	bIQKOK,
	IN	s4Byte		result[][8],
	IN	u1Byte		final_candidate,
	IN  BOOLEAN		bTxOnly
	)
{
	u4Byte	Oldval_0, X, TX0_A, reg;
	s4Byte	Y, TX0_C;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);		
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path A IQ Calibration %s !\n",(bIQKOK)?"Success":"Failed"));

	if(final_candidate == 0xFF)
		return;

	else if(bIQKOK)
	{
		Oldval_0 = (ODM_GetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;				
		TX0_A = (X * Oldval_0) >> 8;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("X = 0x%x, TX0_A = 0x%x, Oldval_0 0x%x\n", X, TX0_A, Oldval_0));
		ODM_SetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, 0x3FF, TX0_A);

		ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, BIT(31), ((X* Oldval_0>>7) & 0x1));
     
		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;		

	
		TX0_C = (Y * Oldval_0) >> 8;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Y = 0x%x, TX = 0x%x\n", Y, TX0_C));
		ODM_SetBBReg(pDM_Odm, rOFDM0_XCTxAFE, 0xF0000000, ((TX0_C&0x3C0)>>6));
		ODM_SetBBReg(pDM_Odm, rOFDM0_XATxIQImbalance, 0x003F0000, (TX0_C&0x3F));

		ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, BIT(29), ((Y* Oldval_0>>7) & 0x1));

		if(bTxOnly)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("_PHY_PathAFillIQKMatrix only Tx OK\n"));		
			return;
		}

		reg = result[final_candidate][2];
#if (DM_ODM_SUPPORT_TYPE==ODM_AP)		
		if( RTL_ABS(reg ,0x100) >= 16) 
			reg = 0x100;
#endif
		ODM_SetBBReg(pDM_Odm, rOFDM0_XARxIQImbalance, 0x3FF, reg);
	
		reg = result[final_candidate][3] & 0x3F;
		ODM_SetBBReg(pDM_Odm, rOFDM0_XARxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		ODM_SetBBReg(pDM_Odm, rOFDM0_RxIQExtAnta, 0xF0000000, reg);
	}
}

VOID
_PHY_PathBFillIQKMatrix(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN  BOOLEAN   	bIQKOK,
	IN	s4Byte		result[][8],
	IN	u1Byte		final_candidate,
	IN	BOOLEAN		bTxOnly			//do Tx only
	)
{
	u4Byte	Oldval_1, X, TX1_A, reg;
	s4Byte	Y, TX1_C;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);		
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path B IQ Calibration %s !\n",(bIQKOK)?"Success":"Failed"));

	if(final_candidate == 0xFF)
		return;

	else if(bIQKOK)
	{
		Oldval_1 = (ODM_GetBBReg(pDM_Odm, rOFDM0_XBTxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;		
		TX1_A = (X * Oldval_1) >> 8;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("X = 0x%x, TX1_A = 0x%x\n", X, TX1_A));
		ODM_SetBBReg(pDM_Odm, rOFDM0_XBTxIQImbalance, 0x3FF, TX1_A);

		ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, BIT(27), ((X* Oldval_1>>7) & 0x1));

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;		

		TX1_C = (Y * Oldval_1) >> 8;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Y = 0x%x, TX1_C = 0x%x\n", Y, TX1_C));
		ODM_SetBBReg(pDM_Odm, rOFDM0_XDTxAFE, 0xF0000000, ((TX1_C&0x3C0)>>6));
		ODM_SetBBReg(pDM_Odm, rOFDM0_XBTxIQImbalance, 0x003F0000, (TX1_C&0x3F));

		ODM_SetBBReg(pDM_Odm, rOFDM0_ECCAThreshold, BIT(25), ((Y* Oldval_1>>7) & 0x1));

		if(bTxOnly)
			return;

		reg = result[final_candidate][6];
		ODM_SetBBReg(pDM_Odm, rOFDM0_XBRxIQImbalance, 0x3FF, reg);
	
		reg = result[final_candidate][7] & 0x3F;
		ODM_SetBBReg(pDM_Odm, rOFDM0_XBRxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		ODM_SetBBReg(pDM_Odm, rOFDM0_AGCRSSITable, 0x0000F000, reg);
	}
}

//
// 2011/07/26 MH Add an API for testing IQK fail case.
//
// MP Already declare in odm.c 
#if !(DM_ODM_SUPPORT_TYPE & ODM_MP) 
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

VOID
_PHY_SaveADDARegisters(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
	
	if (ODM_CheckPowerStatus(pAdapter) == FALSE)
		return;
#endif
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Save ADDA parameters.\n"));
	for( i = 0 ; i < RegisterNum ; i++){
		ADDABackup[i] = ODM_GetBBReg(pDM_Odm, ADDAReg[i], bMaskDWord);
	}
}


VOID
_PHY_SaveMACRegisters(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Save MAC parameters.\n"));
	for( i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++){
		MACBackup[i] = ODM_Read1Byte(pDM_Odm, MACReg[i]);		
	}
	MACBackup[i] = ODM_Read4Byte(pDM_Odm, MACReg[i]);		

}


VOID
_PHY_ReloadADDARegisters(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Reload ADDA power saving parameters !\n"));
	for(i = 0 ; i < RegiesterNum; i++)
	{
		ODM_SetBBReg(pDM_Odm, ADDAReg[i], bMaskDWord, ADDABackup[i]);
	}
}

VOID
_PHY_ReloadMACRegisters(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Reload MAC parameters !\n"));
	for(i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++){
		ODM_Write1Byte(pDM_Odm, MACReg[i], (u1Byte)MACBackup[i]);
	}
	ODM_Write4Byte(pDM_Odm, MACReg[i], MACBackup[i]);	
}


VOID
_PHY_PathADDAOn(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("ADDA ON.\n"));

	pathOn = isPathAOn ? 0x04db25a4 : 0x0b1b25a4;
	if(FALSE == is2T){
		pathOn = 0x0bdb25a0;
		ODM_SetBBReg(pDM_Odm, ADDAReg[0], bMaskDWord, 0x0b1b25a0);
	}
	else{
		ODM_SetBBReg(pDM_Odm,ADDAReg[0], bMaskDWord, pathOn);
	}
	
	for( i = 1 ; i < IQK_ADDA_REG_NUM ; i++){
		ODM_SetBBReg(pDM_Odm,ADDAReg[i], bMaskDWord, pathOn);
	}
	
}

VOID
_PHY_MACSettingCalibration(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	pu4Byte		MACReg,
	IN	pu4Byte		MACBackup	
	)
{
	u4Byte	i = 0;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("MAC settings for Calibration.\n"));

	ODM_Write1Byte(pDM_Odm, MACReg[i], 0x3F);

	for(i = 1 ; i < (IQK_MAC_REG_NUM - 1); i++){
		ODM_Write1Byte(pDM_Odm, MACReg[i], (u1Byte)(MACBackup[i]&(~BIT3)));
	}
	ODM_Write1Byte(pDM_Odm, MACReg[i], (u1Byte)(MACBackup[i]&(~BIT5)));	

}

VOID
_PHY_PathAStandBy(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path-A standby mode!\n"));

	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x0);
	ODM_SetBBReg(pDM_Odm, 0x840, bMaskDWord, 0x00010000);
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x80800000);
}

VOID
_PHY_PIModeSwitch(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("BB Switch to %s mode!\n", (PIMode ? "PI" : "SI")));

	mode = PIMode ? 0x01000100 : 0x01000000;
	ODM_SetBBReg(pDM_Odm, rFPGA0_XA_HSSIParameter1, bMaskDWord, mode);
	ODM_SetBBReg(pDM_Odm, rFPGA0_XB_HSSIParameter1, bMaskDWord, mode);
}

BOOLEAN							
phy_SimularityCompare_8188E(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	u1Byte		final_candidate[2] = {0xFF, 0xFF};	//for path A and path B
	BOOLEAN		bResult = TRUE;
	BOOLEAN		is2T;
	if( (pDM_Odm->RFType ==ODM_2T2R )||(pDM_Odm->RFType ==ODM_2T3R )||(pDM_Odm->RFType ==ODM_2T4R ))
		is2T = TRUE;
	else
		is2T = FALSE;
	
	if(is2T)
		bound = 8;
	else
		bound = 4;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("===> IQK:phy_SimularityCompare_8188E c1 %d c2 %d!!!\n", c1, c2));


	SimularityBitMap = 0;
	
	for( i = 0; i < bound; i++ )
	{
		diff = (result[c1][i] > result[c2][i]) ? (result[c1][i] - result[c2][i]) : (result[c2][i] - result[c1][i]);
		if (diff > MAX_TOLERANCE)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK:phy_SimularityCompare_8188E differnece overflow index %d compare1 0x%x compare2 0x%x!!!\n",  i, result[c1][i], result[c2][i]));
		
			if((i == 2 || i == 6) && !SimularityBitMap)
			{
				if(result[c1][i]+result[c1][i+1] == 0)
					final_candidate[(i/4)] = c2;
				else if (result[c2][i]+result[c2][i+1] == 0)
					final_candidate[(i/4)] = c1;
				else
					SimularityBitMap = SimularityBitMap|(1<<i);					
			}
			else
				SimularityBitMap = SimularityBitMap|(1<<i);
		}
	}
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:phy_SimularityCompare_8188E SimularityBitMap   %d !!!\n", SimularityBitMap));
	
	if ( SimularityBitMap == 0)
	{
		for( i = 0; i < (bound/4); i++ )
		{
			if(final_candidate[i] != 0xFF)
			{
				for( j = i*4; j < (i+1)*4-2; j++)
					result[3][j] = result[final_candidate[i]][j];
				bResult = FALSE;
			}
		}
		return bResult;
	}
	else if (!(SimularityBitMap & 0x0F))			//path A OK
	{
		for(i = 0; i < 4; i++)
			result[3][i] = result[c1][i];
		return FALSE;
	}
	else if (!(SimularityBitMap & 0xF0) && is2T)	//path B OK
	{
		for(i = 4; i < 8; i++)
			result[3][i] = result[c1][i];
		return FALSE;
	}	
	else		
		return FALSE;
	
}



VOID	
phy_IQCalibrate_8188E(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	u4Byte			i;
	u1Byte			PathAOK, PathBOK;
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
					
	//since 92C & 92D have the different define in IQK_BB_REG	
	u4Byte	IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
							rOFDM0_TRxPathEnable, 		rOFDM0_TRMuxPar,	
							rFPGA0_XCD_RFInterfaceSW,	rConfig_AntA,	rConfig_AntB,
							rFPGA0_XAB_RFInterfaceSW,	rFPGA0_XA_RFInterfaceOE,	
							rFPGA0_XB_RFInterfaceOE,	rFPGA0_RFMOD	
							};	

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	u4Byte	retryCount = 2;
#else
#if MP_DRIVER
	const u4Byte	retryCount = 9;
#else
	const u4Byte	retryCount = 2;
#endif
#endif

	// Note: IQ calibration must be performed after loading 
	// 		PHY_REG.txt , and radio_a, radio_b.txt	
	
	//u4Byte bbvalue;

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
#ifdef MP_TEST
		if(pDM_Odm->priv->pshare->rf_ft_var.mp_specific)
			retryCount = 9; 
#endif
#endif


	if(t==0)
	{
//	 	 bbvalue = ODM_GetBBReg(pDM_Odm, rFPGA0_RFMOD, bMaskDWord);
//			RTPRINT(FINIT, INIT_IQK, ("phy_IQCalibrate_8188E()==>0x%08x\n",bbvalue));

			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQ Calibration for %s for %d times\n", (is2T ? "2T2R" : "1T1R"), t));
	
	 	// Save ADDA parameters, turn Path A ADDA on
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	 	_PHY_SaveADDARegisters(pAdapter, ADDA_REG, pDM_Odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);
		_PHY_SaveMACRegisters(pAdapter, IQK_MAC_REG, pDM_Odm->RFCalibrateInfo.IQK_MAC_backup);
	 	_PHY_SaveADDARegisters(pAdapter, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);				
#else
	 	_PHY_SaveADDARegisters(pDM_Odm, ADDA_REG, pDM_Odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);
		_PHY_SaveMACRegisters(pDM_Odm, IQK_MAC_REG, pDM_Odm->RFCalibrateInfo.IQK_MAC_backup);
	 	_PHY_SaveADDARegisters(pDM_Odm, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);		
#endif
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQ Calibration for %s for %d times\n", (is2T ? "2T2R" : "1T1R"), t));
	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	
 	_PHY_PathADDAOn(pAdapter, ADDA_REG, TRUE, is2T);
#else
 	_PHY_PathADDAOn(pDM_Odm, ADDA_REG, TRUE, is2T);
#endif
		
	
	if(t==0)
	{
		pDM_Odm->RFCalibrateInfo.bRfPiEnable = (u1Byte)ODM_GetBBReg(pDM_Odm, rFPGA0_XA_HSSIParameter1, BIT(8));
	}
	
	if(!pDM_Odm->RFCalibrateInfo.bRfPiEnable){
		// Switch BB to PI mode to do IQ Calibration.
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_PHY_PIModeSwitch(pAdapter, TRUE);
#else
		_PHY_PIModeSwitch(pDM_Odm, TRUE);
#endif
	}
	
	//BB setting
	ODM_SetBBReg(pDM_Odm, rFPGA0_RFMOD, BIT24, 0x00);		
	ODM_SetBBReg(pDM_Odm, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	ODM_SetBBReg(pDM_Odm, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	ODM_SetBBReg(pDM_Odm, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22204000);

	
	ODM_SetBBReg(pDM_Odm, rFPGA0_XAB_RFInterfaceSW, BIT10, 0x01);
	ODM_SetBBReg(pDM_Odm, rFPGA0_XAB_RFInterfaceSW, BIT26, 0x01);	
	ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, BIT10, 0x00);
	ODM_SetBBReg(pDM_Odm, rFPGA0_XB_RFInterfaceOE, BIT10, 0x00);	
	

	if(is2T)
	{
		ODM_SetBBReg(pDM_Odm, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00010000);
		ODM_SetBBReg(pDM_Odm, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00010000);
	}

	//MAC settings
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_MACSettingCalibration(pAdapter, IQK_MAC_REG, pDM_Odm->RFCalibrateInfo.IQK_MAC_backup);
#else
	_PHY_MACSettingCalibration(pDM_Odm, IQK_MAC_REG, pDM_Odm->RFCalibrateInfo.IQK_MAC_backup);
#endif


	//Page B init
	//AP or IQK
	ODM_SetBBReg(pDM_Odm, rConfig_AntA, bMaskDWord, 0x0f600000);
	
	if(is2T)
	{
		ODM_SetBBReg(pDM_Odm, rConfig_AntB, bMaskDWord, 0x0f600000);
	}

	// IQ calibration setting
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK setting!\n"));		
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x80800000);
	ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, 0x01007c00);
	ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x81004800);

	for(i = 0 ; i < retryCount ; i++){
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		PathAOK = phy_PathA_IQK_8188E(pAdapter, is2T);
#else
		PathAOK = phy_PathA_IQK_8188E(pDM_Odm, is2T);
#endif
//		if(PathAOK == 0x03){
		if(PathAOK == 0x01){
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A Tx IQK Success!!\n"));
				result[t][0] = (ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
				result[t][1] = (ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			break;
		}
#if 0		
		else if (i == (retryCount-1) && PathAOK == 0x01)	//Tx IQK OK
		{
			RTPRINT(FINIT, INIT_IQK, ("Path A IQK Only  Tx Success!!\n"));
			
			result[t][0] = (ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;			
		}
#endif		
	}

	for(i = 0 ; i < retryCount ; i++){
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		PathAOK = phy_PathA_RxIQK(pAdapter, is2T);
#else
		PathAOK = phy_PathA_RxIQK(pDM_Odm, is2T);
#endif
		if(PathAOK == 0x03){
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path A Rx IQK Success!!\n"));
//				result[t][0] = (ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
//				result[t][1] = (ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
				result[t][2] = (ODM_GetBBReg(pDM_Odm, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][3] = (ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			break;
		}
		else
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A Rx IQK Fail!!\n"));		
		}
	}

	if(0x00 == PathAOK){		
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A IQK failed!!\n"));		
	}

	if(is2T){
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_PHY_PathAStandBy(pAdapter);

		// Turn Path B ADDA on
		_PHY_PathADDAOn(pAdapter, ADDA_REG, FALSE, is2T);
#else
		_PHY_PathAStandBy(pDM_Odm);

		// Turn Path B ADDA on
		_PHY_PathADDAOn(pDM_Odm, ADDA_REG, FALSE, is2T);
#endif

		for(i = 0 ; i < retryCount ; i++){
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			PathBOK = phy_PathB_IQK_8188E(pAdapter);
#else
			PathBOK = phy_PathB_IQK_8188E(pDM_Odm);
#endif
			if(PathBOK == 0x03){
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path B IQK Success!!\n"));
				result[t][4] = (ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][6] = (ODM_GetBBReg(pDM_Odm, rRx_Power_Before_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][7] = (ODM_GetBBReg(pDM_Odm, rRx_Power_After_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				break;
			}
			else if (i == (retryCount - 1) && PathBOK == 0x01)	//Tx IQK OK
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path B Only Tx IQK Success!!\n"));
				result[t][4] = (ODM_GetBBReg(pDM_Odm, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (ODM_GetBBReg(pDM_Odm, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;				
			}
		}

		if(0x00 == PathBOK){		
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path B IQK failed!!\n"));		
		}
	}

	//Back to BB mode, load original value
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:Back to BB mode, load original value!\n"));
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0);

	if(t!=0)
	{
		if(!pDM_Odm->RFCalibrateInfo.bRfPiEnable){
			// Switch back BB to SI mode after finish IQ Calibration.
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			_PHY_PIModeSwitch(pAdapter, FALSE);
#else
			_PHY_PIModeSwitch(pDM_Odm, FALSE);
#endif
		}
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

	 	// Reload ADDA power saving parameters
	 	_PHY_ReloadADDARegisters(pAdapter, ADDA_REG, pDM_Odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);

		// Reload MAC parameters
		_PHY_ReloadMACRegisters(pAdapter, IQK_MAC_REG, pDM_Odm->RFCalibrateInfo.IQK_MAC_backup);
		
	 	_PHY_ReloadADDARegisters(pAdapter, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);
#else
	 	// Reload ADDA power saving parameters
	 	_PHY_ReloadADDARegisters(pDM_Odm, ADDA_REG, pDM_Odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);

		// Reload MAC parameters
		_PHY_ReloadMACRegisters(pDM_Odm, IQK_MAC_REG, pDM_Odm->RFCalibrateInfo.IQK_MAC_backup);
		
	 	_PHY_ReloadADDARegisters(pDM_Odm, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);
#endif
		

			// Restore RX initial gain
			ODM_SetBBReg(pDM_Odm, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00032ed3);
			if(is2T){
				ODM_SetBBReg(pDM_Odm, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00032ed3);
			}
		
		//load 0xe30 IQC default value
		ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);		
		ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);				
		
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_IQCalibrate_8188E() <==\n"));
	
}


VOID	
phy_LCCalibrate_8188E(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN		is2T
	)
{
	u1Byte	tmpReg;
	u4Byte	RF_Amode=0, RF_Bmode=0, LC_Cal;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	//Check continuous TX and Packet TX
	tmpReg = ODM_Read1Byte(pDM_Odm, 0xd03);

	if((tmpReg&0x70) != 0)			//Deal with contisuous TX case
		ODM_Write1Byte(pDM_Odm, 0xd03, tmpReg&0x8F);	//disable all continuous TX
	else							// Deal with Packet TX case
		ODM_Write1Byte(pDM_Odm, REG_TXPAUSE, 0xFF);			// block all queues

	if((tmpReg&0x70) != 0)
	{
		//1. Read original RF mode
		//Path-A
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		RF_Amode = PHY_QueryRFReg(pAdapter, RF_PATH_A, RF_AC, bMask12Bits);

		//Path-B
		if(is2T)
			RF_Bmode = PHY_QueryRFReg(pAdapter, RF_PATH_B, RF_AC, bMask12Bits);	
#else
		RF_Amode = ODM_GetRFReg(pDM_Odm, RF_PATH_A, RF_AC, bMask12Bits);

		//Path-B
		if(is2T)
			RF_Bmode = ODM_GetRFReg(pDM_Odm, RF_PATH_B, RF_AC, bMask12Bits);	
#endif	

		//2. Set RF mode = standby mode
		//Path-A
		ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_AC, bMask12Bits, (RF_Amode&0x8FFFF)|0x10000);

		//Path-B
		if(is2T)
			ODM_SetRFReg(pDM_Odm, RF_PATH_B, RF_AC, bMask12Bits, (RF_Bmode&0x8FFFF)|0x10000);			
	}
	
	//3. Read RF reg18
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	LC_Cal = PHY_QueryRFReg(pAdapter, RF_PATH_A, RF_CHNLBW, bMask12Bits);
#else
	LC_Cal = ODM_GetRFReg(pDM_Odm, RF_PATH_A, RF_CHNLBW, bMask12Bits);
#endif	
	
	//4. Set LC calibration begin	bit15
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_CHNLBW, bMask12Bits, LC_Cal|0x08000);

	ODM_delay_ms(100);		


	//Restore original situation
	if((tmpReg&0x70) != 0)	//Deal with contisuous TX case 
	{  
		//Path-A
		ODM_Write1Byte(pDM_Odm, 0xd03, tmpReg);
		ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_AC, bMask12Bits, RF_Amode);
		
		//Path-B
		if(is2T)
			ODM_SetRFReg(pDM_Odm, RF_PATH_B, RF_AC, bMask12Bits, RF_Bmode);
	}
	else // Deal with Packet TX case
	{
		ODM_Write1Byte(pDM_Odm, REG_TXPAUSE, 0x00);	
	}
}

//Analog Pre-distortion calibration
#define		APK_BB_REG_NUM	8
#define		APK_CURVE_REG_NUM 4
#define		PATH_NUM		2

VOID	
phy_APCalibrate_8188E(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
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
					{0x0852c, 0x0a52c, 0x3a52c, 0x5a52c, 0x5a52c},	//path settings equal to path b settings
					{0x0852c, 0x0a52c, 0x5a52c, 0x5a52c, 0x5a52c}
					};
	
	u4Byte			APK_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52014, 0x52013, 0x5200f, 0x5208d},
					{0x5201a, 0x52019, 0x52016, 0x52033, 0x52050}
					};

	u4Byte			APK_normal_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a},	//path settings equal to path b settings
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a}
					};

	u4Byte			AFE_on_off[PATH_NUM] = {
					0x04db25a4, 0x0b1b25a4};	//path A on path B off / path A off path B on

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
	
	u4Byte			APK_result[PATH_NUM][APK_BB_REG_NUM];	//val_1_1a, val_1_2a, val_2a, val_3a, val_4a
//	u4Byte			AP_curve[PATH_NUM][APK_CURVE_REG_NUM];

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

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("==>phy_APCalibrate_8188E() delta %d\n", delta));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("AP Calibration for %s\n", (is2T ? "2T2R" : "1T1R")));
	if(!is2T)
		pathbound = 1;

	//2 FOR NORMAL CHIP SETTINGS

// Temporarily do not allow normal driver to do the following settings because these offset
// and value will cause RF internal PA to be unpredictably disabled by HW, such that RF Tx signal
// will disappear after disable/enable card many times on 88CU. RF SD and DD have not find the
// root cause, so we remove these actions temporarily. Added by tynli and SD3 Allen. 2010.05.31.
#if MP_DRIVER != 1
	return;
#endif
	//settings adjust for normal chip
	for(index = 0; index < PATH_NUM; index ++)
	{
		APK_offset[index] = APK_normal_offset[index];
		APK_value[index] = APK_normal_value[index];
		AFE_on_off[index] = 0x6fdb25a4;
	}

	for(index = 0; index < APK_BB_REG_NUM; index ++)
	{
		for(path = 0; path < pathbound; path++)
		{
			APK_RF_init_value[path][index] = APK_normal_RF_init_value[path][index];
			APK_RF_value_0[path][index] = APK_normal_RF_value_0[path][index];
		}
		BB_AP_MODE[index] = BB_normal_AP_MODE[index];
	}			

	apkbound = 6;
	
	//save BB default value
	for(index = 0; index < APK_BB_REG_NUM ; index++)
	{
		if(index == 0)		//skip 
			continue;				
		BB_backup[index] = ODM_GetBBReg(pDM_Odm, BB_REG[index], bMaskDWord);
	}
	
	//save MAC default value													
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_SaveMACRegisters(pAdapter, MAC_REG, MAC_backup);
	
	//save AFE default value
	_PHY_SaveADDARegisters(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#else
	_PHY_SaveMACRegisters(pDM_Odm, MAC_REG, MAC_backup);
	
	//save AFE default value
	_PHY_SaveADDARegisters(pDM_Odm, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#endif

	for(path = 0; path < pathbound; path++)
	{


		if(path == RF_PATH_A)
		{
			//path A APK
			//load APK setting
			//path-A		
			offset = rPdp_AntA;
			for(index = 0; index < 11; index ++)			
			{
				ODM_SetBBReg(pDM_Odm, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0x%x value 0x%x\n", offset, ODM_GetBBReg(pDM_Odm, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}
			
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);
			
			offset = rConfig_AntA;
			for(; index < 13; index ++) 		
			{
				ODM_SetBBReg(pDM_Odm, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0x%x value 0x%x\n", offset, ODM_GetBBReg(pDM_Odm, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}	
			
			//page-B1
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x40000000);
		
			//path A
			offset = rPdp_AntA;
			for(index = 0; index < 16; index++)
			{
				ODM_SetBBReg(pDM_Odm, offset, bMaskDWord, APK_normal_setting_value_2[index]);		
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0x%x value 0x%x\n", offset, ODM_GetBBReg(pDM_Odm, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}				
			ODM_SetBBReg(pDM_Odm,  rFPGA0_IQK, bMaskDWord, 0x00000000);							
		}
		else if(path == RF_PATH_B)
		{
			//path B APK
			//load APK setting
			//path-B		
			offset = rPdp_AntB;
			for(index = 0; index < 10; index ++)			
			{
				ODM_SetBBReg(pDM_Odm, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0x%x value 0x%x\n", offset, ODM_GetBBReg(pDM_Odm, offset, bMaskDWord))); 	
				
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
			for(; index < 13; index ++) //offset 0xb68, 0xb6c		
			{
				ODM_SetBBReg(pDM_Odm, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("phy_APCalibrate_8188E() offset 0x%x value 0x%x\n", offset, ODM_GetBBReg(pDM_Odm, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}	
			
			//page-B1
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x40000000);
			
			//path B
			offset = 0xb60;
			for(index = 0; index < 16; index++)
			{
				ODM_SetBBReg(pDM_Odm, offset, bMaskDWord, APK_normal_setting_value_2[index]);		
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("phy_APCalibrate_8188E() offset 0x%x value 0x%x\n", offset, ODM_GetBBReg(pDM_Odm, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}				
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0);							
		}
	
		//save RF default value
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		regD[path] = PHY_QueryRFReg(pAdapter, path, RF_TXBIAS_A, bMaskDWord);
#else
		regD[path] = ODM_GetRFReg(pDM_Odm, path, RF_TXBIAS_A, bMaskDWord);
#endif
		
		//Path A AFE all on, path B AFE All off or vise versa
		for(index = 0; index < IQK_ADDA_REG_NUM ; index++)
			ODM_SetBBReg(pDM_Odm, AFE_REG[index], bMaskDWord, AFE_on_off[path]);
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0xe70 %x\n", ODM_GetBBReg(pDM_Odm, rRx_Wait_CCA, bMaskDWord)));		

		//BB to AP mode
		if(path == 0)
		{				
			for(index = 0; index < APK_BB_REG_NUM ; index++)
			{

				if(index == 0)		//skip 
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
		}
		else		//path B
		{
			ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_B, bMaskDWord, 0x01008c00);			
			ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_B, bMaskDWord, 0x01008c00);					
		
		}

		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0x800 %x\n", ODM_GetBBReg(pDM_Odm, 0x800, bMaskDWord)));				

		//MAC settings
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_PHY_MACSettingCalibration(pAdapter, MAC_REG, MAC_backup);
#else
		_PHY_MACSettingCalibration(pDM_Odm, MAC_REG, MAC_backup);
#endif
		
		if(path == RF_PATH_A)	//Path B to standby mode
		{
			ODM_SetRFReg(pDM_Odm, RF_PATH_B, RF_AC, bMaskDWord, 0x10000);			
		}
		else			//Path A to standby mode
		{
			ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_AC, bMaskDWord, 0x10000);			
			ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_MODE1, bMaskDWord, 0x1000f);			
			ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_MODE2, bMaskDWord, 0x20103);						
		}

		delta_offset = ((delta+14)/2);
		if(delta_offset < 0)
			delta_offset = 0;
		else if (delta_offset > 12)
			delta_offset = 12;
			
		//AP calibration
		for(index = 0; index < APK_BB_REG_NUM; index++)
		{
			if(index != 1)	//only DO PA11+PAD01001, AP RF setting
				continue;
					
			tmpReg = APK_RF_init_value[path][index];
#if 1			
			if(!pDM_Odm->RFCalibrateInfo.bAPKThermalMeterIgnore)
			{
				BB_offset = (tmpReg & 0xF0000) >> 16;

				if(!(tmpReg & BIT15)) //sign bit 0
				{
					BB_offset = -BB_offset;
				}

				delta_V = APK_delta_mapping[index][delta_offset];
				
				BB_offset += delta_V;

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() APK index %d tmpReg 0x%x delta_V %d delta_offset %d\n", index, tmpReg, delta_V, delta_offset));		
				
				if(BB_offset < 0)
				{
					tmpReg = tmpReg & (~BIT15);
					BB_offset = -BB_offset;
				}
				else
				{
					tmpReg = tmpReg | BIT15;
				}
				tmpReg = (tmpReg & 0xFFF0FFFF) | (BB_offset << 16);
			}
#endif

			ODM_SetRFReg(pDM_Odm, path, RF_IPA_A, bMaskDWord, 0x8992e);
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0xc %x\n", PHY_QueryRFReg(pAdapter, path, RF_IPA_A, bMaskDWord)));		
			ODM_SetRFReg(pDM_Odm, path, RF_AC, bMaskDWord, APK_RF_value_0[path][index]);
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("phy_APCalibrate_8188E() offset 0x0 %x\n", PHY_QueryRFReg(pAdapter, path, RF_AC, bMaskDWord)));		
			ODM_SetRFReg(pDM_Odm, path, RF_TXBIAS_A, bMaskDWord, tmpReg);
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0xd %x\n", PHY_QueryRFReg(pAdapter, path, RF_TXBIAS_A, bMaskDWord)));					
#else
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0xc %x\n", ODM_GetRFReg(pDM_Odm, path, RF_IPA_A, bMaskDWord)));		
			ODM_SetRFReg(pDM_Odm, path, RF_AC, bMaskDWord, APK_RF_value_0[path][index]);
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("phy_APCalibrate_8188E() offset 0x0 %x\n", ODM_GetRFReg(pDM_Odm, path, RF_AC, bMaskDWord)));		
			ODM_SetRFReg(pDM_Odm, path, RF_TXBIAS_A, bMaskDWord, tmpReg);
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0xd %x\n", ODM_GetRFReg(pDM_Odm, path, RF_TXBIAS_A, bMaskDWord)));					
#endif
			
			// PA11+PAD01111, one shot	
			i = 0;
			do
			{
				ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x80000000);
				{
					ODM_SetBBReg(pDM_Odm, APK_offset[path], bMaskDWord, APK_value[0]);		
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0x%x value 0x%x\n", APK_offset[path], ODM_GetBBReg(pDM_Odm, APK_offset[path], bMaskDWord)));
					ODM_delay_ms(3);				
					ODM_SetBBReg(pDM_Odm, APK_offset[path], bMaskDWord, APK_value[1]);
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0x%x value 0x%x\n", APK_offset[path], ODM_GetBBReg(pDM_Odm, APK_offset[path], bMaskDWord)));

					ODM_delay_ms(20);
				}
				ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x00000000);

				if(path == RF_PATH_A)
					tmpReg = ODM_GetBBReg(pDM_Odm, rAPK, 0x03E00000);
				else
					tmpReg = ODM_GetBBReg(pDM_Odm, rAPK, 0xF8000000);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_APCalibrate_8188E() offset 0xbd8[25:21] %x\n", tmpReg));		
				

				i++;
			}
			while(tmpReg > apkbound && i < 4);

			APK_result[path][index] = tmpReg;
		}
	}

	//reload MAC default value	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_ReloadMACRegisters(pAdapter, MAC_REG, MAC_backup);
#else
	_PHY_ReloadMACRegisters(pDM_Odm, MAC_REG, MAC_backup);
#endif
	
	//reload BB default value	
	for(index = 0; index < APK_BB_REG_NUM ; index++)
	{

		if(index == 0)		//skip 
			continue;					
		ODM_SetBBReg(pDM_Odm, BB_REG[index], bMaskDWord, BB_backup[index]);
	}

	//reload AFE default value
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_ReloadADDARegisters(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#else
	_PHY_ReloadADDARegisters(pDM_Odm, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#endif

	//reload RF path default value
	for(path = 0; path < pathbound; path++)
	{
		ODM_SetRFReg(pDM_Odm, path, 0xd, bMaskDWord, regD[path]);
		if(path == RF_PATH_B)
		{
			ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_MODE1, bMaskDWord, 0x1000f);			
			ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_MODE2, bMaskDWord, 0x20101);						
		}

		//note no index == 0
		if (APK_result[path][1] > 6)
			APK_result[path][1] = 6;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("apk path %d result %d 0x%x \t", path, 1, APK_result[path][1]));					
	}

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("\n"));
	

	for(path = 0; path < pathbound; path++)
	{
		ODM_SetRFReg(pDM_Odm, path, 0x3, bMaskDWord, 
		((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (APK_result[path][1] << 5) | APK_result[path][1]));
		if(path == RF_PATH_A)
			ODM_SetRFReg(pDM_Odm, path, 0x4, bMaskDWord, 
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x00 << 5) | 0x05));		
		else
		ODM_SetRFReg(pDM_Odm, path, 0x4, bMaskDWord, 
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x02 << 5) | 0x05));						
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		if(!IS_HARDWARE_TYPE_8723A(pAdapter))		
			ODM_SetRFReg(pDM_Odm, path, RF_BS_PA_APSET_G9_G11, bMaskDWord, 
			((0x08 << 15) | (0x08 << 10) | (0x08 << 5) | 0x08));			
#endif		
	}

	pDM_Odm->RFCalibrateInfo.bAPKdone = TRUE;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("<==phy_APCalibrate_8188E()\n"));
}



#define		DP_BB_REG_NUM		7
#define		DP_RF_REG_NUM		1
#define		DP_RETRY_LIMIT		10
#define		DP_PATH_NUM		2
#define		DP_DPK_NUM			3
#define		DP_DPK_VALUE_NUM	2





VOID
PHY_IQCalibrate_8188E(
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

	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;	
	#else  // (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;	
	#endif

	#if (MP_DRIVER == 1)
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)	
	PMPT_CONTEXT	pMptCtx = &(pAdapter->MptCtx);	
	#else// (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);		
	#endif	
	#endif//(MP_DRIVER == 1)
#endif	

	s4Byte			result[4][8];	//last is final result
	u1Byte			i, final_candidate, Indexforchannel;
    	u1Byte          channelToIQK = 7;
	BOOLEAN			bPathAOK, bPathBOK;
	s4Byte			RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC, RegTmp = 0;
	BOOLEAN			is12simular, is13simular, is23simular;	
	BOOLEAN 		bStartContTx = FALSE, bSingleTone = FALSE, bCarrierSuppression = FALSE;
	u4Byte			IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
					rOFDM0_XARxIQImbalance, 	rOFDM0_XBRxIQImbalance, 
					rOFDM0_ECCAThreshold, 	rOFDM0_AGCRSSITable,
					rOFDM0_XATxIQImbalance, 	rOFDM0_XBTxIQImbalance, 
					rOFDM0_XCTxAFE, 		rOFDM0_XDTxAFE, 
					rOFDM0_RxIQExtAnta};
	BOOLEAN		is2T;

	is2T = (pDM_Odm->RFType == ODM_2T2R)?TRUE:FALSE;
#if (DM_ODM_SUPPORT_TYPE & (ODM_MP|ODM_CE) )
	if (ODM_CheckPowerStatus(pAdapter) == FALSE)
		return;
#else
	prtl8192cd_priv	priv = pDM_Odm->priv;

#ifdef MP_TEST
	if(priv->pshare->rf_ft_var.mp_specific)
	{
		if((OPMODE & WIFI_MP_CTX_PACKET) || (OPMODE & WIFI_MP_CTX_ST))
			return;
	}
#endif

	if(priv->pshare->IQK_88E_done)
		bReCovery= 1;
	priv->pshare->IQK_88E_done = 1;

#endif	

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if(!(pDM_Odm->SupportAbility & ODM_RF_CALIBRATION))
	{
		return;
	}
#endif		

#if MP_DRIVER == 1	
	bStartContTx = pMptCtx->bStartContTx;
	bSingleTone = pMptCtx->bSingleTone;
	bCarrierSuppression = pMptCtx->bCarrierSuppression;	
#endif
	
	// 20120213<Kordan> Turn on when continuous Tx to pass lab testing. (required by Edlu)
	if(bSingleTone || bCarrierSuppression)
		return;

#if DISABLE_BB_RF
	return;
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_AP))
	if(bReCovery)
#else//for ODM_MP
	if(bReCovery && (!pAdapter->bInHctTest))  //YJ,add for PowerTest,120405
#endif	
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("PHY_IQCalibrate_8188E: Return due to bReCovery!\n"));
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_PHY_ReloadADDARegisters(pAdapter, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);
#else
		_PHY_ReloadADDARegisters(pDM_Odm, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);
#endif
		return;		
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK:Start!!!\n"));

#if 0//Suggested by Edlu,120413

    // IQK on channel 7, should switch back when completed.
	//originChannel = pHalData->CurrentChannel;
	originChannel = *(pDM_Odm->pChannel);

#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
   	pAdapter->HalFunc.SwChnlByTimerHandler(pAdapter, channelToIQK);	
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	pAdapter->HalFunc.set_channel_handler(pAdapter, channelToIQK);
#endif

#endif

	for(i = 0; i < 8; i++)
	{
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;
		result[3][i] = 0;
	}
	final_candidate = 0xff;
	bPathAOK = FALSE;
	bPathBOK = FALSE;
	is12simular = FALSE;
	is23simular = FALSE;
	is13simular = FALSE;


	//ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK !!!interface %d currentband %d ishardwareD %d \n", pDM_Odm->interfaceIndex, pHalData->CurrentBandType92D, IS_HARDWARE_TYPE_8192D(pAdapter)));
//	RT_TRACE(COMP_INIT,DBG_LOUD,("Acquire Mutex in IQCalibrate \n"));
	for (i=0; i<3; i++)
	{		
	 	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	 	phy_IQCalibrate_8188E(pAdapter, result, i, is2T);
#else
	 	phy_IQCalibrate_8188E(pDM_Odm, result, i, is2T);
#endif			
		
		
		if(i == 1)
		{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			is12simular = phy_SimularityCompare_8188E(pAdapter, result, 0, 1);
#else
			is12simular = phy_SimularityCompare_8188E(pDM_Odm, result, 0, 1);
#endif			
			if(is12simular)
			{
				final_candidate = 0;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: is12simular final_candidate is %x\n",final_candidate));				
				break;
			}
		}
		
		if(i == 2)
		{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			is13simular = phy_SimularityCompare_8188E(pAdapter, result, 0, 2);
#else
			is13simular = phy_SimularityCompare_8188E(pDM_Odm, result, 0, 2);
#endif			
			if(is13simular)
			{
				final_candidate = 0;			
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: is13simular final_candidate is %x\n",final_candidate));
				
				break;
			}
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			is23simular = phy_SimularityCompare_8188E(pAdapter, result, 1, 2);
#else
			is23simular = phy_SimularityCompare_8188E(pDM_Odm, result, 1, 2);
#endif			
			if(is23simular)
			{
				final_candidate = 1;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: is23simular final_candidate is %x\n",final_candidate));				
			}
			else
			{
				for(i = 0; i < 8; i++)
					RegTmp += result[3][i];

				if(RegTmp != 0)
					final_candidate = 3;			
				else
					final_candidate = 0xFF;
			}
		}
	}
//	RT_TRACE(COMP_INIT,DBG_LOUD,("Release Mutex in IQCalibrate \n"));

	for (i=0; i<4; i++)
	{
		RegE94 = result[i][0];
		RegE9C = result[i][1];
		RegEA4 = result[i][2];
		RegEAC = result[i][3];
		RegEB4 = result[i][4];
		RegEBC = result[i][5];
		RegEC4 = result[i][6];
		RegECC = result[i][7];
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: RegE94=%x RegE9C=%x RegEA4=%x RegEAC=%x RegEB4=%x RegEBC=%x RegEC4=%x RegECC=%x\n ", RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC));
	}
	
	if(final_candidate != 0xff)
	{
		pDM_Odm->RFCalibrateInfo.RegE94 = RegE94 = result[final_candidate][0];
		pDM_Odm->RFCalibrateInfo.RegE9C = RegE9C = result[final_candidate][1];
		RegEA4 = result[final_candidate][2];
		RegEAC = result[final_candidate][3];
		pDM_Odm->RFCalibrateInfo.RegEB4 = RegEB4 = result[final_candidate][4];
		pDM_Odm->RFCalibrateInfo.RegEBC = RegEBC = result[final_candidate][5];
		RegEC4 = result[final_candidate][6];
		RegECC = result[final_candidate][7];
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK: final_candidate is %x\n",final_candidate));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK: RegE94=%x RegE9C=%x RegEA4=%x RegEAC=%x RegEB4=%x RegEBC=%x RegEC4=%x RegECC=%x\n ", RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC));
		bPathAOK = bPathBOK = TRUE;
	}
	else
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK: FAIL use default value\n"));
	
		pDM_Odm->RFCalibrateInfo.RegE94 = pDM_Odm->RFCalibrateInfo.RegEB4 = 0x100;	//X default value
		pDM_Odm->RFCalibrateInfo.RegE9C = pDM_Odm->RFCalibrateInfo.RegEBC = 0x0;		//Y default value
	}
	
	if((RegE94 != 0)/*&&(RegEA4 != 0)*/)
	{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_PHY_PathAFillIQKMatrix(pAdapter, bPathAOK, result, final_candidate, (RegEA4 == 0));
#else
		_PHY_PathAFillIQKMatrix(pDM_Odm, bPathAOK, result, final_candidate, (RegEA4 == 0));
#endif
	}
	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	if (is2T)
	{
		if((RegEB4 != 0)/*&&(RegEC4 != 0)*/)
		{
			_PHY_PathBFillIQKMatrix(pAdapter, bPathBOK, result, final_candidate, (RegEC4 == 0));
		}
	}
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
    Indexforchannel = ODM_GetRightChnlPlaceforIQK(pHalData->CurrentChannel);
#else
	Indexforchannel = 0;	
#endif

//To Fix BSOD when final_candidate is 0xff
//by sherry 20120321
	if(final_candidate < 4)
	{
		for(i = 0; i < IQK_Matrix_REG_NUM; i++)
			pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].Value[0][i] = result[final_candidate][i];
		pDM_Odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].bIQKDone = TRUE;		
	}
	//RTPRINT(FINIT, INIT_IQK, ("\nIQK OK Indexforchannel %d.\n", Indexforchannel));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("\nIQK OK Indexforchannel %d.\n", Indexforchannel));
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

	_PHY_SaveADDARegisters(pAdapter, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);
#else
	_PHY_SaveADDARegisters(pDM_Odm, IQK_BB_REG_92C, pDM_Odm->RFCalibrateInfo.IQK_BB_backup_recover, IQK_BB_REG_NUM);
#endif	

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK finished\n"));
#if 0 //Suggested by Edlu,120413	

	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	pAdapter->HalFunc.SwChnlByTimerHandler(pAdapter, originChannel);	
	#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	pAdapter->HalFunc.set_channel_handler(pAdapter, originChannel);
	#endif

#endif	

}


VOID
PHY_LCCalibrate_8188E(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm
#else
	IN	PADAPTER	pAdapter
#endif
	)
{
	BOOLEAN 		bStartContTx = FALSE, bSingleTone = FALSE, bCarrierSuppression = FALSE;
	u4Byte			timeout = 2000, timecount = 0;
	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	

	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;	
	#else  // (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;	
	#endif

	#if (MP_DRIVER == 1)
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)	
	PMPT_CONTEXT	pMptCtx = &(pAdapter->MptCtx);	
	#else// (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);		
	#endif	
	#endif//(MP_DRIVER == 1)
#endif	




#if MP_DRIVER == 1	
	bStartContTx = pMptCtx->bStartContTx;
	bSingleTone = pMptCtx->bSingleTone;
	bCarrierSuppression = pMptCtx->bCarrierSuppression;	
#endif


#if DISABLE_BB_RF
	return;
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if(!(pDM_Odm->SupportAbility & ODM_RF_CALIBRATION))
	{
		return;
	}
#endif	
	// 20120213<Kordan> Turn on when continuous Tx to pass lab testing. (required by Edlu)
	if(bSingleTone || bCarrierSuppression)
		return;

	while(*(pDM_Odm->pbScanInProcess) && timecount < timeout)
	{
		ODM_delay_ms(50);
		timecount += 50;
	}	
	
	pDM_Odm->RFCalibrateInfo.bLCKInProgress = TRUE;

	//ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("LCK:Start!!!interface %d currentband %x delay %d ms\n", pDM_Odm->interfaceIndex, pHalData->CurrentBandType92D, timecount));
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	
	if(pDM_Odm->RFType == ODM_2T2R)
	{
		phy_LCCalibrate_8188E(pAdapter, TRUE);
	}
	else
#endif		
	{
		// For 88C 1T1R
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		phy_LCCalibrate_8188E(pAdapter, FALSE);
#else
		phy_LCCalibrate_8188E(pDM_Odm, FALSE);
#endif		
	}

	pDM_Odm->RFCalibrateInfo.bLCKInProgress = FALSE;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("LCK:Finish!!!interface %d\n", pDM_Odm->InterfaceIndex));

}

VOID
PHY_APCalibrate_8188E(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
#if DISABLE_BB_RF
	return;
#endif

	return;
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if(!(pDM_Odm->SupportAbility & ODM_RF_CALIBRATION))
	{
		return;
	}
#endif	

#if FOR_BRAZIL_PRETEST != 1
	if(pDM_Odm->RFCalibrateInfo.bAPKdone)
#endif		
		return;

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	if(pDM_Odm->RFType == ODM_2T2R){
		phy_APCalibrate_8188E(pAdapter, delta, TRUE);
	}
	else
#endif
	{
		// For 88C 1T1R
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		phy_APCalibrate_8188E(pAdapter, delta, FALSE);
#else
		phy_APCalibrate_8188E(pDM_Odm, delta, FALSE);
#endif
	}
}
VOID phy_SetRFPathSwitch_8188E(
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
	#elif (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif

	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	if(!pAdapter->bHWInitReady)	
	#elif  (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if(pAdapter->hw_init_completed == _FALSE)
	#endif
	{
		u1Byte	u1bTmp;
		u1bTmp = ODM_Read1Byte(pDM_Odm, REG_LEDCFG2) | BIT7;
		ODM_Write1Byte(pDM_Odm, REG_LEDCFG2, u1bTmp);
		//ODM_SetBBReg(pDM_Odm, REG_LEDCFG0, BIT23, 0x01);
		ODM_SetBBReg(pDM_Odm, rFPGA0_XAB_RFParameter, BIT13, 0x01);
	}
	
#endif		

	if(is2T)	//92C
	{
		if(bMain)
			ODM_SetBBReg(pDM_Odm, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6, 0x1);	//92C_Path_A			
		else
			ODM_SetBBReg(pDM_Odm, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6, 0x2);	//BT							
	}
	else			//88C
	{
	
		if(bMain)
			ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, BIT8|BIT9, 0x2);	//Main
		else
			ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, BIT8|BIT9, 0x1);	//Aux		
	}	
}
VOID PHY_SetRFPathSwitch_8188E(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	


#if DISABLE_BB_RF
	return;
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	if(pDM_Odm->RFType == ODM_2T2R)
	{
		phy_SetRFPathSwitch_8188E(pAdapter, bMain, TRUE);
	}
	else
#endif		
	{
		// For 88C 1T1R
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		phy_SetRFPathSwitch_8188E(pAdapter, bMain, FALSE);
#else
		phy_SetRFPathSwitch_8188E(pDM_Odm, bMain, FALSE);
#endif
	}
}

#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
//digital predistortion
VOID	
phy_DigitalPredistortion(
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN	PADAPTER	pAdapter,
#else
	IN PDM_ODM_T	pDM_Odm,
#endif
	IN	BOOLEAN		is2T
	)
{
#if ( RT_PLATFORM == PLATFORM_WINDOWS) 
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
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
					0x04db25a4, 0x0b1b25a4};	//path A on path B off / path A off path B on

	u1Byte			RetryCount = 0;


	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("==>phy_DigitalPredistortion()\n"));
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_DigitalPredistortion for %s %s\n", (is2T ? "2T2R" : "1T1R")));

	//save BB default value
	for(index=0; index<DP_BB_REG_NUM; index++)
		BB_backup[index] = ODM_GetBBReg(pDM_Odm, BB_REG[index], bMaskDWord);

	//save MAC default value
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_SaveMACRegisters(pAdapter, BB_REG, MAC_backup);
#else
	_PHY_SaveMACRegisters(pDM_Odm, BB_REG, MAC_backup);
#endif	

	//save RF default value
	for(path=0; path<DP_PATH_NUM; path++)
	{
		for(index=0; index<DP_RF_REG_NUM; index++)
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			RF_backup[path][index] = PHY_QueryRFReg(pAdapter, path, RF_REG[index], bMaskDWord);	
#else
			RF_backup[path][index] = ODM_GetRFReg(pAdapter, path, RF_REG[index], bMaskDWord);	
#endif	
	}	
	
	//save AFE default value
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_SaveADDARegisters(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#else
	RF_backup[path][index] = ODM_GetRFReg(pAdapter, path, RF_REG[index], bMaskDWord);	
#endif	
	
	//Path A/B AFE all on
	for(index = 0; index < IQK_ADDA_REG_NUM ; index++)
		ODM_SetBBReg(pDM_Odm, AFE_REG[index], bMaskDWord, 0x6fdb25a4);

	//BB register setting
	for(index = 0; index < DP_BB_REG_NUM; index++)
	{
		if(index < 4)
			ODM_SetBBReg(pDM_Odm, BB_REG[index], bMaskDWord, BB_settings[index]);
		else if (index == 4)
			ODM_SetBBReg(pDM_Odm,BB_REG[index], bMaskDWord, BB_backup[index]|BIT10|BIT26);			
		else
			ODM_SetBBReg(pDM_Odm, BB_REG[index], BIT10, 0x00);			
	}

	//MAC register setting
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_MACSettingCalibration(pAdapter, MAC_REG, MAC_backup);
#else
	_PHY_MACSettingCalibration(pDM_Odm, MAC_REG, MAC_backup);
#endif	

	//PAGE-E IQC setting	
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00); 		
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);	
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_B, bMaskDWord, 0x01008c00);	
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_B, bMaskDWord, 0x01008c00);	
	
	//path_A DPK
	//Path B to standby mode
	ODM_SetRFReg(pDM_Odm, RF_PATH_B, RF_AC, bMaskDWord, 0x10000);

	// PA gain = 11 & PAD1 => tx_agc 1f ~11
	// PA gain = 11 & PAD2 => tx_agc 10~0e
	// PA gain = 01 => tx_agc 0b~0d
	// PA gain = 00 => tx_agc 0a~00
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x40000000);	
	ODM_SetBBReg(pDM_Odm, 0xbc0, bMaskDWord, 0x0005361f);		
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x00000000);	

	//do inner loopback DPK 3 times 
	for(i = 0; i < 3; i++)
	{
		//PA gain = 11 & PAD2 => tx_agc = 0x0f/0x0c/0x07
		for(index = 0; index < 3; index++)
			ODM_SetBBReg(pDM_Odm, 0xe00+index*4, bMaskDWord, Tx_AGC[i][0]);			
		ODM_SetBBReg(pDM_Odm,0xe00+index*4, bMaskDWord, Tx_AGC[i][1]);			
		for(index = 0; index < 4; index++)
			ODM_SetBBReg(pDM_Odm,0xe10+index*4, bMaskDWord, Tx_AGC[i][0]);			
	
		// PAGE_B for Path-A inner loopback DPK setting
		ODM_SetBBReg(pDM_Odm,rPdp_AntA, bMaskDWord, 0x02097098);
		ODM_SetBBReg(pDM_Odm,rPdp_AntA_4, bMaskDWord, 0xf76d9f84);
		ODM_SetBBReg(pDM_Odm,rConfig_Pmpd_AntA, bMaskDWord, 0x0004ab87);
		ODM_SetBBReg(pDM_Odm,rConfig_AntA, bMaskDWord, 0x00880000);		
		
		//----send one shot signal----//
		// Path A
		ODM_SetBBReg(pDM_Odm,rConfig_Pmpd_AntA, bMaskDWord, 0x80047788);
		ODM_delay_ms(1);
		ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x00047788);
		ODM_delay_ms(50);
	}

	//PA gain = 11 => tx_agc = 1a
	for(index = 0; index < 3; index++)		
		ODM_SetBBReg(pDM_Odm,0xe00+index*4, bMaskDWord, 0x34343434);	
	ODM_SetBBReg(pDM_Odm,0xe08+index*4, bMaskDWord, 0x03903434);	
	for(index = 0; index < 4; index++)		
		ODM_SetBBReg(pDM_Odm,0xe10+index*4, bMaskDWord, 0x34343434);	

	//====================================
	// PAGE_B for Path-A DPK setting
	//====================================
	// open inner loopback @ b00[19]:10 od 0xb00 0x01097018
	ODM_SetBBReg(pDM_Odm,rPdp_AntA, bMaskDWord, 0x02017098);
	ODM_SetBBReg(pDM_Odm,rPdp_AntA_4, bMaskDWord, 0xf76d9f84);
	ODM_SetBBReg(pDM_Odm,rConfig_Pmpd_AntA, bMaskDWord, 0x0004ab87);
	ODM_SetBBReg(pDM_Odm,rConfig_AntA, bMaskDWord, 0x00880000);		

	//rf_lpbk_setup
	//1.rf 00:5205a, rf 0d:0e52c
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, 0x0c, bMaskDWord, 0x8992b);
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, 0x0d, bMaskDWord, 0x0e52c); 	
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, 0x00, bMaskDWord, 0x5205a );		

	//----send one shot signal----//
	// Path A
	ODM_SetBBReg(pDM_Odm,rConfig_Pmpd_AntA, bMaskDWord, 0x800477c0);
	ODM_delay_ms(1);
	ODM_SetBBReg(pDM_Odm,rConfig_Pmpd_AntA, bMaskDWord, 0x000477c0);
	ODM_delay_ms(50);

	while(RetryCount < DP_RETRY_LIMIT && !pDM_Odm->RFCalibrateInfo.bDPPathAOK)
	{
		//----read back measurement results----//
		ODM_SetBBReg(pDM_Odm, rPdp_AntA, bMaskDWord, 0x0c297018);
		tmpReg = ODM_GetBBReg(pDM_Odm, 0xbe0, bMaskDWord);
		ODM_delay_ms(10);
		ODM_SetBBReg(pDM_Odm, rPdp_AntA, bMaskDWord, 0x0c29701f);
		tmpReg2 = ODM_GetBBReg(pDM_Odm, 0xbe8, bMaskDWord);
		ODM_delay_ms(10);

		tmpReg = (tmpReg & bMaskHWord) >> 16;
		tmpReg2 = (tmpReg2 & bMaskHWord) >> 16;		
		if(tmpReg < 0xf0 || tmpReg > 0x105 || tmpReg2 > 0xff )
		{
			ODM_SetBBReg(pDM_Odm, rPdp_AntA, bMaskDWord, 0x02017098);
		
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x80000000);
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x00000000);
			ODM_delay_ms(1);
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x800477c0);
			ODM_delay_ms(1);			
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x000477c0);			
			ODM_delay_ms(50);			
			RetryCount++;			
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path A DPK RetryCount %d 0xbe0[31:16] %x 0xbe8[31:16] %x\n", RetryCount, tmpReg, tmpReg2));										
		}
		else
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path A DPK Sucess\n"));		
			pDM_Odm->RFCalibrateInfo.bDPPathAOK = TRUE;
			break;
		}		
	}
	RetryCount = 0;
	
	//DPP path A
	if(pDM_Odm->RFCalibrateInfo.bDPPathAOK)
	{	
		// DP settings
		ODM_SetBBReg(pDM_Odm, rPdp_AntA, bMaskDWord, 0x01017098);
		ODM_SetBBReg(pDM_Odm, rPdp_AntA_4, bMaskDWord, 0x776d9f84);
		ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntA, bMaskDWord, 0x0004ab87);
		ODM_SetBBReg(pDM_Odm, rConfig_AntA, bMaskDWord, 0x00880000);
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x40000000);

		for(i=rPdp_AntA; i<=0xb3c; i+=4)
		{
			ODM_SetBBReg(pDM_Odm, i, bMaskDWord, 0x40004000);	
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path A ofsset = 0x%x\n", i));		
		}
		
		//pwsf
		ODM_SetBBReg(pDM_Odm, 0xb40, bMaskDWord, 0x40404040);	
		ODM_SetBBReg(pDM_Odm, 0xb44, bMaskDWord, 0x28324040);			
		ODM_SetBBReg(pDM_Odm, 0xb48, bMaskDWord, 0x10141920);					

		for(i=0xb4c; i<=0xb5c; i+=4)
		{
			ODM_SetBBReg(pDM_Odm, i, bMaskDWord, 0x0c0c0c0c);	
		}		

		//TX_AGC boundary
		ODM_SetBBReg(pDM_Odm, 0xbc0, bMaskDWord, 0x0005361f);	
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x00000000);					
	}
	else
	{
		ODM_SetBBReg(pDM_Odm, rPdp_AntA, bMaskDWord, 0x00000000);	
		ODM_SetBBReg(pDM_Odm, rPdp_AntA_4, bMaskDWord, 0x00000000);			
	}

	//DPK path B
	if(is2T)
	{
		//Path A to standby mode
		ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_AC, bMaskDWord, 0x10000);
		
		// LUTs => tx_agc
		// PA gain = 11 & PAD1, => tx_agc 1f ~11
		// PA gain = 11 & PAD2, => tx_agc 10 ~0e
		// PA gain = 01 => tx_agc 0b ~0d
		// PA gain = 00 => tx_agc 0a ~00
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x40000000);	
		ODM_SetBBReg(pDM_Odm, 0xbc4, bMaskDWord, 0x0005361f);		
		ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x00000000);	

		//do inner loopback DPK 3 times 
		for(i = 0; i < 3; i++)
		{
			//PA gain = 11 & PAD2 => tx_agc = 0x0f/0x0c/0x07
			for(index = 0; index < 4; index++)
				ODM_SetBBReg(pDM_Odm, 0x830+index*4, bMaskDWord, Tx_AGC[i][0]);			
			for(index = 0; index < 2; index++)
				ODM_SetBBReg(pDM_Odm, 0x848+index*4, bMaskDWord, Tx_AGC[i][0]);			
			for(index = 0; index < 2; index++)
				ODM_SetBBReg(pDM_Odm, 0x868+index*4, bMaskDWord, Tx_AGC[i][0]);			
		
			// PAGE_B for Path-A inner loopback DPK setting
			ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x02097098);
			ODM_SetBBReg(pDM_Odm, rPdp_AntB_4, bMaskDWord, 0xf76d9f84);
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x0004ab87);
			ODM_SetBBReg(pDM_Odm, rConfig_AntB, bMaskDWord, 0x00880000);		
			
			//----send one shot signal----//
			// Path B
			ODM_SetBBReg(pDM_Odm,rConfig_Pmpd_AntB, bMaskDWord, 0x80047788);
			ODM_delay_ms(1);
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x00047788);
			ODM_delay_ms(50);
		}

		// PA gain = 11 => tx_agc = 1a	
		for(index = 0; index < 4; index++)
			ODM_SetBBReg(pDM_Odm, 0x830+index*4, bMaskDWord, 0x34343434);	
		for(index = 0; index < 2; index++)
			ODM_SetBBReg(pDM_Odm, 0x848+index*4, bMaskDWord, 0x34343434);	
		for(index = 0; index < 2; index++)
			ODM_SetBBReg(pDM_Odm, 0x868+index*4, bMaskDWord, 0x34343434);	

		// PAGE_B for Path-B DPK setting
		ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x02017098);		
		ODM_SetBBReg(pDM_Odm, rPdp_AntB_4, bMaskDWord, 0xf76d9f84);		
		ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x0004ab87);		
		ODM_SetBBReg(pDM_Odm, rConfig_AntB, bMaskDWord, 0x00880000);		

		// RF lpbk switches on
		ODM_SetBBReg(pDM_Odm, 0x840, bMaskDWord, 0x0101000f);		
		ODM_SetBBReg(pDM_Odm, 0x840, bMaskDWord, 0x01120103);		

		//Path-B RF lpbk
		ODM_SetRFReg(pDM_Odm, RF_PATH_B, 0x0c, bMaskDWord, 0x8992b);
		ODM_SetRFReg(pDM_Odm, RF_PATH_B, 0x0d, bMaskDWord, 0x0e52c);
		ODM_SetRFReg(pDM_Odm, RF_PATH_B, RF_AC, bMaskDWord, 0x5205a); 

		//----send one shot signal----//
		ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x800477c0);		
		ODM_delay_ms(1);	
		ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x000477c0);		
		ODM_delay_ms(50);
		
		while(RetryCount < DP_RETRY_LIMIT && !pDM_Odm->RFCalibrateInfo.bDPPathBOK)
		{
			//----read back measurement results----//
			ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x0c297018);		
			tmpReg = ODM_GetBBReg(pDM_Odm, 0xbf0, bMaskDWord);
			ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x0c29701f);		
			tmpReg2 = ODM_GetBBReg(pDM_Odm, 0xbf8, bMaskDWord);
			
			tmpReg = (tmpReg & bMaskHWord) >> 16;
			tmpReg2 = (tmpReg2 & bMaskHWord) >> 16;
			
			if(tmpReg < 0xf0 || tmpReg > 0x105 || tmpReg2 > 0xff)
			{
				ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x02017098);		
			
				ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x80000000);
				ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x00000000);
				ODM_delay_ms(1);
				ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x800477c0);		
				ODM_delay_ms(1);	
				ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x000477c0);		
				ODM_delay_ms(50);			
				RetryCount++;			
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("path B DPK RetryCount %d 0xbf0[31:16] %x, 0xbf8[31:16] %x\n", RetryCount , tmpReg, tmpReg2));														
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path B DPK Success\n"));						
				pDM_Odm->RFCalibrateInfo.bDPPathBOK = TRUE;
				break;
			}						
		}
	
		//DPP path B
		if(pDM_Odm->RFCalibrateInfo.bDPPathBOK)
		{
			// DP setting
			// LUT by SRAM
			ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x01017098);
			ODM_SetBBReg(pDM_Odm, rPdp_AntB_4, bMaskDWord, 0x776d9f84);
			ODM_SetBBReg(pDM_Odm, rConfig_Pmpd_AntB, bMaskDWord, 0x0004ab87);
			ODM_SetBBReg(pDM_Odm, rConfig_AntB, bMaskDWord, 0x00880000);
			
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x40000000);
			for(i=0xb60; i<=0xb9c; i+=4)
			{
				ODM_SetBBReg(pDM_Odm, i, bMaskDWord, 0x40004000);	
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path B ofsset = 0x%x\n", i));
			}

			// PWSF
			ODM_SetBBReg(pDM_Odm, 0xba0, bMaskDWord, 0x40404040);	
			ODM_SetBBReg(pDM_Odm, 0xba4, bMaskDWord, 0x28324050);			
			ODM_SetBBReg(pDM_Odm, 0xba8, bMaskDWord, 0x0c141920);					

			for(i=0xbac; i<=0xbbc; i+=4)
			{
				ODM_SetBBReg(pDM_Odm, i, bMaskDWord, 0x0c0c0c0c);	
			}		
			
			// tx_agc boundary
			ODM_SetBBReg(pDM_Odm, 0xbc4, bMaskDWord, 0x0005361f);	
			ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskDWord, 0x00000000);			
			
		}
		else
		{
			ODM_SetBBReg(pDM_Odm, rPdp_AntB, bMaskDWord, 0x00000000);	
			ODM_SetBBReg(pDM_Odm, rPdp_AntB_4, bMaskDWord, 0x00000000);					
		}
	}
	
	//reload BB default value
	for(index=0; index<DP_BB_REG_NUM; index++)
		ODM_SetBBReg(pDM_Odm, BB_REG[index], bMaskDWord, BB_backup[index]);
	
	//reload RF default value
	for(path = 0; path<DP_PATH_NUM; path++)
	{
		for( i = 0 ; i < DP_RF_REG_NUM ; i++){
			ODM_SetRFReg(pDM_Odm, path, RF_REG[i], bMaskDWord, RF_backup[path][i]);
		}
	}
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_MODE1, bMaskDWord, 0x1000f);	//standby mode
	ODM_SetRFReg(pDM_Odm, RF_PATH_A, RF_MODE2, bMaskDWord, 0x20101);		//RF lpbk switches off

	//reload AFE default value
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_PHY_ReloadADDARegisters(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);	

	//reload MAC default value	
	_PHY_ReloadMACRegisters(pAdapter, MAC_REG, MAC_backup);
#else
	_PHY_ReloadADDARegisters(pDM_Odm, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);	

	//reload MAC default value	
	_PHY_ReloadMACRegisters(pDM_Odm, MAC_REG, MAC_backup);
#endif		

	pDM_Odm->RFCalibrateInfo.bDPdone = TRUE;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("<==phy_DigitalPredistortion()\n"));
#endif		
}

VOID
PHY_DigitalPredistortion_8188E(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
#if DISABLE_BB_RF
	return;
#endif

	return;

	if(pDM_Odm->RFCalibrateInfo.bDPdone)
		return;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

	if(pDM_Odm->RFType == ODM_2T2R){
		phy_DigitalPredistortion(pAdapter, TRUE);
	}
	else
#endif		
	{
		// For 88C 1T1R
		phy_DigitalPredistortion(pAdapter, FALSE);
	}
}
	


//return value TRUE => Main; FALSE => Aux

BOOLEAN phy_QueryRFPathSwitch_8188E(
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
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	
	if(!pAdapter->bHWInitReady)	
	{
		u1Byte	u1bTmp;
		u1bTmp = ODM_Read1Byte(pDM_Odm, REG_LEDCFG2) | BIT7;
		ODM_Write1Byte(pDM_Odm, REG_LEDCFG2, u1bTmp);
		//ODM_SetBBReg(pDM_Odm, REG_LEDCFG0, BIT23, 0x01);
		ODM_SetBBReg(pDM_Odm, rFPGA0_XAB_RFParameter, BIT13, 0x01);
	}

	if(is2T)		//
	{
		if(ODM_GetBBReg(pDM_Odm, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6) == 0x01)
			return TRUE;
		else 
			return FALSE;
	}
	else
	{
		if(ODM_GetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, BIT8|BIT9) == 0x02)
			return TRUE;
		else 
			return FALSE;
	}
}



//return value TRUE => Main; FALSE => Aux
BOOLEAN PHY_QueryRFPathSwitch_8188E(	
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm
#else
	IN	PADAPTER	pAdapter
#endif
	)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif
#endif	


#if DISABLE_BB_RF
	return TRUE;
#endif
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

	//if(IS_92C_SERIAL( pHalData->VersionID)){
	if(pDM_Odm->RFType == ODM_2T2R){
		return phy_QueryRFPathSwitch_8188E(pAdapter, TRUE);
	}
	else
#endif		
	{
		// For 88C 1T1R
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		return phy_QueryRFPathSwitch_8188E(pAdapter, FALSE);
#else
		return phy_QueryRFPathSwitch_8188E(pDM_Odm, FALSE);
#endif
	}
}
#endif
