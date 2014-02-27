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

//============================================================
// include files
//============================================================

#include "../odm_precomp.h"

#if (RTL8192C_SUPPORT == 1)

//#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
VOID
odm_ResetFACounter_92C(
	IN		PDM_ODM_T		pDM_Odm
	)
{
//	PADAPTER		pAdapter	= pDM_Odm->Adapter;
	u1Byte	BBReset;

	//reset false alarm counter registers
	ODM_SetBBReg(pDM_Odm, 0xd00, BIT27, 1);
	ODM_SetBBReg(pDM_Odm, 0xd00, BIT27, 0);
	//update ofdm counter
	ODM_SetBBReg(pDM_Odm, 0xc00, BIT31, 0); //update page C counter
	ODM_SetBBReg(pDM_Odm, 0xd00, BIT31, 0); //update page D counter

	//reset CCK CCA counter
	ODM_SetBBReg(pDM_Odm, 0xa2c, BIT13|BIT12, 0); 
	ODM_SetBBReg(pDM_Odm, 0xa2c, BIT13|BIT12, 2); 
	//reset CCK FA counter
	ODM_SetBBReg(pDM_Odm, 0xa2c, BIT15|BIT14, 0); 
	ODM_SetBBReg(pDM_Odm, 0xa2c, BIT15|BIT14, 2); 


	//BB Reset
	if(!pDM_Odm->bLinked)
	{
		BBReset = ODM_Read1Byte(pDM_Odm, 0x02);
		ODM_Write1Byte(pDM_Odm, 0x02, BBReset&(~BIT0));
		ODM_Write1Byte(pDM_Odm, 0x02, BBReset|BIT0);
	}

}
//#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_MP)



//
// ==================================================
// Tx power tracking relative code.
// ==================================================
//


//091212 chiyokolin
VOID
odm_TXPowerTrackingCallbackThermalMeter92C(
	IN PADAPTER	Adapter
	)
{

#if ((RT_PLATFORM == PLATFORM_WINDOWS) || (RT_PLATFORM == PLATFORM_LINUX)) && (HAL_CODE_BASE==RTL8192_C)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	u1Byte			ThermalValue = 0, delta, delta_LCK, delta_IQK, delta_HP, TimeOut = 100;
	s4Byte 			ele_A=0, ele_D, TempCCk, X, value32;
	s4Byte			Y, ele_C=0;
	s1Byte			OFDM_index[2], CCK_index=0, OFDM_index_old[2], CCK_index_old=0;
	int	    			i = 0;
	BOOLEAN			is2T = IS_92C_SERIAL(pHalData->VersionID);

#if MP_DRIVER == 1
	PMPT_CONTEXT	pMptCtx = &(Adapter->MptCtx);	
	pu1Byte			TxPwrLevel = pMptCtx->TxPwrLevel;
#endif	
	u1Byte			OFDM_min_index = 6, rf; //OFDM BB Swing should be less than +3.0dB, which is required by Arthur
	
	u4Byte			DPK_delta_mapping[2][DPK_DELTA_MAPPING_NUM] = {
					{0x1c, 0x1c, 0x1d, 0x1d, 0x1e, 
					 0x1f, 0x00, 0x00, 0x01, 0x01,
					 0x02, 0x02, 0x03},
					{0x1c, 0x1d, 0x1e, 0x1e, 0x1e,
					 0x1f, 0x00, 0x00, 0x01, 0x02,
					 0x02, 0x03, 0x03}};
	
#if DEV_BUS_TYPE==RT_USB_INTERFACE		
	u1Byte			ThermalValue_HP_count = 0;
	u4Byte			ThermalValue_HP = 0;
	s1Byte			index_mapping_HP[index_mapping_HP_NUM] = {
					0,	1,	3,	4,	6,	
					7,	9,	10,	12,	13,	
					15,	16,	18,	19,	21
					};

	s1Byte			index_HP;
#endif

	if (ODM_CheckPowerStatus(Adapter) == FALSE)
		return;
	
	pHalData->TXPowerTrackingCallbackCnt++;	//cosa add for debug
	pHalData->bTXPowerTrackingInit = TRUE;

	ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("===>odm_TXPowerTrackingCallbackThermalMeter92C\n"));

	ThermalValue = (u1Byte)PHY_QueryRFReg(Adapter, RF_PATH_A, RF_T_METER, 0x1f);	// 0x24: RF Reg[4:0]	
	ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", ThermalValue, pHalData->ThermalValue, pHalData->EEPROMThermalMeter));

	//if (IS_HARDWARE_TYPE_8188E(Adapter)/* ||
	//	is_ha*/)
	//{
	//	PHY_APCalibrate_8188E(Adapter, (ThermalValue - pHalData->EEPROMThermalMeter));
	//}
	//else if (IS_HARDWARE_TYPE_8192C(Adapter) || 
	//		IS_HARDWARE_TYPE_8192D(Adapter) ||
	//		IS_HARDWARE_TYPE_8723A(Adapter))
	{
		PHY_APCalibrate_8192C(Adapter, (ThermalValue - pHalData->EEPROMThermalMeter));
	}

	if(is2T)
		rf = 2;
	else
		rf = 1;
	
	while(PlatformAtomicExchange(&Adapter->IntrCCKRefCount, TRUE) == TRUE) 
	{
		PlatformSleepUs(100);
		TimeOut--;
		if(TimeOut <= 0)
		{
			RTPRINT(FINIT, INIT_TxPower, 
			 ("!!!odm_TXPowerTrackingCallbackThermalMeter92C Wait for check CCK gain index too long!!!\n" ));
			break;
		}
	}
	
	if(ThermalValue)
	{
//		if(!pHalData->ThermalValue)
		{
			//Query OFDM path A default setting 		
			ele_D = PHY_QueryBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord)&bMaskOFDM_D;
			for(i=0; i<OFDM_TABLE_SIZE_92C; i++)	//find the index
			{
				if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
				{
					OFDM_index_old[0] = (u1Byte)i;
					ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("Initial pathA ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n", 
						rOFDM0_XATxIQImbalance, ele_D, OFDM_index_old[0]));
					break;
				}
			}

			//Query OFDM path B default setting 
			if(is2T)
			{
				ele_D = PHY_QueryBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord)&bMaskOFDM_D;
				for(i=0; i<OFDM_TABLE_SIZE_92C; i++)	//find the index
				{
					if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
					{
						OFDM_index_old[1] = (u1Byte)i;
						ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("Initial pathB ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n", 
							rOFDM0_XBTxIQImbalance, ele_D, OFDM_index_old[1]));
						break;
					}
				}
			}

			//Query CCK default setting From 0xa24
			TempCCk = PHY_QueryBBReg(Adapter, rCCK0_TxFilter2, bMaskDWord)&bMaskCCK;
			for(i=0 ; i<CCK_TABLE_SIZE ; i++)
			{
				if(pHalData->bCCKinCH14)
				{
					if(PlatformCompareMemory((void*)&TempCCk, (void*)&CCKSwingTable_Ch14[i][2], 4)==0)
					{
						CCK_index_old =(u1Byte) i;
						ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch 14 %d\n", 
							rCCK0_TxFilter2, TempCCk, CCK_index_old, pHalData->bCCKinCH14));
						break;
					}
				}
				else
				{
					if(PlatformCompareMemory((void*)&TempCCk, (void*)&CCKSwingTable_Ch1_Ch13[i][2], 4)==0)
					{
						CCK_index_old =(u1Byte) i;
						ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch14 %d\n", 
							rCCK0_TxFilter2, TempCCk, CCK_index_old, pHalData->bCCKinCH14));
						break;
					}			
				}
			}	

			if(!pHalData->ThermalValue)
			{
				pHalData->ThermalValue = pHalData->EEPROMThermalMeter;
				pHalData->ThermalValue_LCK = ThermalValue;				
				pHalData->ThermalValue_IQK = ThermalValue;								
				pHalData->ThermalValue_DPK = pHalData->EEPROMThermalMeter;
				
#if DEV_BUS_TYPE==RT_USB_INTERFACE				
				for(i = 0; i < rf; i++)
					pHalData->OFDM_index_HP[i] = pHalData->OFDM_index[i] = OFDM_index_old[i];
				pHalData->CCK_index_HP = pHalData->CCK_index = CCK_index_old;
#else
				for(i = 0; i < rf; i++)
					pHalData->OFDM_index[i] = OFDM_index_old[i];
				pHalData->CCK_index = CCK_index_old;
#endif
			}	

#if DEV_BUS_TYPE==RT_USB_INTERFACE				
			if(RT_GetInterfaceSelection(Adapter) == INTF_SEL1_USB_High_Power)
			{
				pHalData->ThermalValue_HP[pHalData->ThermalValue_HP_index] = ThermalValue;
				pHalData->ThermalValue_HP_index++;
				if(pHalData->ThermalValue_HP_index == HP_THERMAL_NUM)
					pHalData->ThermalValue_HP_index = 0;

				for(i = 0; i < HP_THERMAL_NUM; i++)
				{
					if(pHalData->ThermalValue_HP[i])
					{
						ThermalValue_HP += pHalData->ThermalValue_HP[i];
						ThermalValue_HP_count++;
					}			
				}
		
				if(ThermalValue_HP_count)
					ThermalValue = (u1Byte)(ThermalValue_HP / ThermalValue_HP_count);
			}
#endif
		}
		
		delta = (ThermalValue > pHalData->ThermalValue)?(ThermalValue - pHalData->ThermalValue):(pHalData->ThermalValue - ThermalValue);
#if DEV_BUS_TYPE==RT_USB_INTERFACE				
		if(RT_GetInterfaceSelection(Adapter) == INTF_SEL1_USB_High_Power)
		{
			if(pHalData->bDoneTxpower)
				delta_HP = (ThermalValue > pHalData->ThermalValue)?(ThermalValue - pHalData->ThermalValue):(pHalData->ThermalValue - ThermalValue);
			else
				delta_HP = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);						
		}
		else
#endif	
		{
			delta_HP = 0;			
		}
		delta_LCK = (ThermalValue > pHalData->ThermalValue_LCK)?(ThermalValue - pHalData->ThermalValue_LCK):(pHalData->ThermalValue_LCK - ThermalValue);
		delta_IQK = (ThermalValue > pHalData->ThermalValue_IQK)?(ThermalValue - pHalData->ThermalValue_IQK):(pHalData->ThermalValue_IQK - ThermalValue);

		ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x delta 0x%x delta_LCK 0x%x delta_IQK 0x%x\n", ThermalValue, pHalData->ThermalValue, pHalData->EEPROMThermalMeter, delta, delta_LCK, delta_IQK));

		if(delta_LCK > 1)
		{
			pHalData->ThermalValue_LCK = ThermalValue;
			PHY_LCCalibrate(Adapter);
		}
		
		if((delta > 0 || delta_HP > 0)&& pHalData->TxPowerTrackControl)
		{
#if DEV_BUS_TYPE==RT_USB_INTERFACE		
			if(RT_GetInterfaceSelection(Adapter) == INTF_SEL1_USB_High_Power)
			{
				pHalData->bDoneTxpower = TRUE;
				delta_HP = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);						
				
				if(delta_HP > index_mapping_HP_NUM-1)					
					index_HP = index_mapping_HP[index_mapping_HP_NUM-1];
				else
					index_HP = index_mapping_HP[delta_HP];
				
				if(ThermalValue > pHalData->EEPROMThermalMeter)	//set larger Tx power
				{
					for(i = 0; i < rf; i++)
					 	OFDM_index[i] = pHalData->OFDM_index_HP[i] - index_HP;
					CCK_index = pHalData->CCK_index_HP -index_HP;						
				}
				else
				{
					for(i = 0; i < rf; i++)
						OFDM_index[i] = pHalData->OFDM_index_HP[i] + index_HP;
					CCK_index = pHalData->CCK_index_HP + index_HP;						
				}	
				
				delta_HP = (ThermalValue > pHalData->ThermalValue)?(ThermalValue - pHalData->ThermalValue):(pHalData->ThermalValue - ThermalValue);
				
			}
			else
#endif				
			{
				if(ThermalValue > pHalData->ThermalValue)
				{ 
					for(i = 0; i < rf; i++)
					 	pHalData->OFDM_index[i] -= delta;
					pHalData->CCK_index -= delta;
				}
				else
				{
					for(i = 0; i < rf; i++)			
						pHalData->OFDM_index[i] += delta;
					pHalData->CCK_index += delta;
				}
			}
			
			if(is2T)
			{
				ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("temp OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
					pHalData->OFDM_index[0], pHalData->OFDM_index[1], pHalData->CCK_index));			
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("temp OFDM_A_index=0x%x, CCK_index=0x%x\n", 
					pHalData->OFDM_index[0], pHalData->CCK_index));			
			}
			
			//no adjust
#if DEV_BUS_TYPE==RT_USB_INTERFACE					
			if(RT_GetInterfaceSelection(Adapter) != INTF_SEL1_USB_High_Power)
#endif				
			{
				if(ThermalValue > pHalData->EEPROMThermalMeter)
				{
					for(i = 0; i < rf; i++)			
						OFDM_index[i] = pHalData->OFDM_index[i]+1;
					CCK_index = pHalData->CCK_index+1;			
				}
				else
				{
					for(i = 0; i < rf; i++)			
						OFDM_index[i] = pHalData->OFDM_index[i];
					CCK_index = pHalData->CCK_index;						
				}

#if MP_DRIVER == 1
				for(i = 0; i < rf; i++)
				{
					if(TxPwrLevel[i] >=0 && TxPwrLevel[i] <=26)
					{
						if(ThermalValue > pHalData->EEPROMThermalMeter)
						{
							if (delta < 5)
								OFDM_index[i] -= 1;					
							else 
								OFDM_index[i] -= 2;					
						}
						else if(delta > 5 && ThermalValue < pHalData->EEPROMThermalMeter)
						{
							OFDM_index[i] += 1;
						}
					}
					else if (TxPwrLevel[i] >= 27 && TxPwrLevel[i] <= 32 && ThermalValue > pHalData->EEPROMThermalMeter)
					{
						if (delta < 5)
							OFDM_index[i] -= 1;					
						else 
							OFDM_index[i] -= 2;								
					}
					else if (TxPwrLevel[i] >= 32 && TxPwrLevel[i] <= 38 && ThermalValue > pHalData->EEPROMThermalMeter && delta > 5)
					{
						OFDM_index[i] -= 1;								
					}
				}

				{
					if(TxPwrLevel[i] >=0 && TxPwrLevel[i] <=26)
					{
						if(ThermalValue > pHalData->EEPROMThermalMeter)
						{
							if (delta < 5)
								CCK_index -= 1; 				
							else 
								CCK_index -= 2; 				
						}
						else if(delta > 5 && ThermalValue < pHalData->EEPROMThermalMeter)
						{
							CCK_index += 1;
						}
					}
					else if (TxPwrLevel[i] >= 27 && TxPwrLevel[i] <= 32 && ThermalValue > pHalData->EEPROMThermalMeter)
					{
						if (delta < 5)
							CCK_index -= 1; 				
						else 
							CCK_index -= 2; 							
					}
					else if (TxPwrLevel[i] >= 32 && TxPwrLevel[i] <= 38 && ThermalValue > pHalData->EEPROMThermalMeter && delta > 5)
					{
						CCK_index -= 1; 							
					}
				}
#endif				
			}
			
			for(i = 0; i < rf; i++)
			{
				if(OFDM_index[i] > (OFDM_TABLE_SIZE_92C-1))
					OFDM_index[i] = (OFDM_TABLE_SIZE_92C-1);
				else if (OFDM_index[i] < OFDM_min_index)
					OFDM_index[i] = OFDM_min_index;
			}
						
			if(CCK_index > (CCK_TABLE_SIZE-1))
				CCK_index = (CCK_TABLE_SIZE-1);
			else if (CCK_index < 0)
				CCK_index = 0;		

			if(is2T)
			{
				ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("new OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
					OFDM_index[0], OFDM_index[1], CCK_index));
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("new OFDM_A_index=0x%x, CCK_index=0x%x\n", 
					OFDM_index[0], CCK_index));			
			}
		}

		if(pHalData->TxPowerTrackControl && (delta != 0 || delta_HP != 0))
		{
			//Adujst OFDM Ant_A according to IQK result
			ele_D = (OFDMSwingTable[OFDM_index[0]] & 0xFFC00000)>>22;
			X = pHalData->RegE94;
			Y = pHalData->RegE9C;		

			if(X != 0)
			{
				if ((X & 0x00000200) != 0)
					X = X | 0xFFFFFC00;
				ele_A = ((X * ele_D)>>8)&0x000003FF;
					
				//new element C = element D x Y
				if ((Y & 0x00000200) != 0)
					Y = Y | 0xFFFFFC00;
				ele_C = ((Y * ele_D)>>8)&0x000003FF;
				
				//wirte new elements A, C, D to regC80 and regC94, element B is always 0
				value32 = (ele_D<<22)|((ele_C&0x3F)<<16)|ele_A;
				PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, value32);
				
				value32 = (ele_C&0x000003C0)>>6;
				PHY_SetBBReg(Adapter, rOFDM0_XCTxAFE, bMaskH4Bits, value32);

				value32 = ((X * ele_D)>>7)&0x01;
				PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT31, value32);

				value32 = ((Y * ele_D)>>7)&0x01;
				PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT29, value32);
				
			}
			else
			{
				PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable[OFDM_index[0]]);				
				PHY_SetBBReg(Adapter, rOFDM0_XCTxAFE, bMaskH4Bits, 0x00);
				PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT31|BIT29, 0x00);			
			}

			RTPRINT(FINIT, INIT_IQK, ("TxPwrTracking path A: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x\n", X, Y, ele_A, ele_C, ele_D));		

			//Adjust CCK according to IQK result
			if(!pHalData->bCCKinCH14){
				PlatformEFIOWrite1Byte(Adapter, 0xa22, CCKSwingTable_Ch1_Ch13[CCK_index][0]);
				PlatformEFIOWrite1Byte(Adapter, 0xa23, CCKSwingTable_Ch1_Ch13[CCK_index][1]);
				PlatformEFIOWrite1Byte(Adapter, 0xa24, CCKSwingTable_Ch1_Ch13[CCK_index][2]);
				PlatformEFIOWrite1Byte(Adapter, 0xa25, CCKSwingTable_Ch1_Ch13[CCK_index][3]);
				PlatformEFIOWrite1Byte(Adapter, 0xa26, CCKSwingTable_Ch1_Ch13[CCK_index][4]);
				PlatformEFIOWrite1Byte(Adapter, 0xa27, CCKSwingTable_Ch1_Ch13[CCK_index][5]);
				PlatformEFIOWrite1Byte(Adapter, 0xa28, CCKSwingTable_Ch1_Ch13[CCK_index][6]);
				PlatformEFIOWrite1Byte(Adapter, 0xa29, CCKSwingTable_Ch1_Ch13[CCK_index][7]);		
			}
			else{
				PlatformEFIOWrite1Byte(Adapter, 0xa22, CCKSwingTable_Ch14[CCK_index][0]);
				PlatformEFIOWrite1Byte(Adapter, 0xa23, CCKSwingTable_Ch14[CCK_index][1]);
				PlatformEFIOWrite1Byte(Adapter, 0xa24, CCKSwingTable_Ch14[CCK_index][2]);
				PlatformEFIOWrite1Byte(Adapter, 0xa25, CCKSwingTable_Ch14[CCK_index][3]);
				PlatformEFIOWrite1Byte(Adapter, 0xa26, CCKSwingTable_Ch14[CCK_index][4]);
				PlatformEFIOWrite1Byte(Adapter, 0xa27, CCKSwingTable_Ch14[CCK_index][5]);
				PlatformEFIOWrite1Byte(Adapter, 0xa28, CCKSwingTable_Ch14[CCK_index][6]);
				PlatformEFIOWrite1Byte(Adapter, 0xa29, CCKSwingTable_Ch14[CCK_index][7]);	
			}		

			if(is2T)
			{						
				ele_D = (OFDMSwingTable[OFDM_index[1]] & 0xFFC00000)>>22;
				
				//new element A = element D x X
				X = pHalData->RegEB4;
				Y = pHalData->RegEBC;
				
				if(X != 0){
					if ((X & 0x00000200) != 0)	//consider minus
						X = X | 0xFFFFFC00;
					ele_A = ((X * ele_D)>>8)&0x000003FF;
					
					//new element C = element D x Y
					if ((Y & 0x00000200) != 0)
						Y = Y | 0xFFFFFC00;
					ele_C = ((Y * ele_D)>>8)&0x00003FF;
					
					//wirte new elements A, C, D to regC88 and regC9C, element B is always 0
					value32=(ele_D<<22)|((ele_C&0x3F)<<16) |ele_A;
					PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, value32);

					value32 = (ele_C&0x000003C0)>>6;
					PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, value32);	
					
					value32 = ((X * ele_D)>>7)&0x01;
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT27, value32);

					value32 = ((Y * ele_D)>>7)&0x01;
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT25, value32);

				}
				else{
					PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, OFDMSwingTable[OFDM_index[1]]);										
					PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, 0x00);	
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT27|BIT25, 0x00);				
				}

				RTPRINT(FINIT, INIT_IQK, ("TxPwrTracking path B: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x\n", X, Y, ele_A, ele_C, ele_D));			
			}

			RTPRINT(FINIT, INIT_IQK, ("TxPwrTracking 0xc80 = 0x%x, 0xc94 = 0x%x RF 0x24 = 0x%x\n", PHY_QueryBBReg(Adapter, 0xc80, bMaskDWord), PHY_QueryBBReg(Adapter, 0xc94, bMaskDWord), PHY_QueryRFReg(Adapter, RF_PATH_A, 0x24, bRFRegOffsetMask)));
		}

#if MP_DRIVER == 1
		if(delta_IQK > 1)
#else
		if(delta_IQK > 3)
#endif			
		{
			pHalData->ThermalValue_IQK = ThermalValue;
			PHY_IQCalibrate(Adapter, FALSE);
		}

		//update thermal meter value
		if(pHalData->TxPowerTrackControl)
			Adapter->HalFunc.SetHalDefVarHandler(Adapter, HAL_DEF_THERMAL_VALUE, &ThermalValue);
			
	}

	PlatformAtomicExchange(&Adapter->IntrCCKRefCount, FALSE);		
	pHalData->TXPowercount = 0;

	// 2011/08/23 MH Add for power tracking after S3/S4  turn off RF. In this case, we need to execute IQK again. Otherwise
	// The IQK scheme will use old value to save and cause incorrect BB value.
	{
		RT_RF_POWER_STATE 	rtState;

		Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_RF_STATE, (pu1Byte)(&rtState));	
		
		if(Adapter->bDriverStopped || Adapter->bDriverIsGoingToPnpSetPowerSleep || rtState == eRfOff)
		{
			ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("Incorrect pwrtrack point, re-iqk next time\n"));	
			pHalData->bIQKInitialized = FALSE;
		}
	}
	
	ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("<===odm_TXPowerTrackingCallbackThermalMeter92C\n"));	
#endif

}

//#if (RT_PLATFORM == PLATFORM_WINDOWS) && (HAL_CODE_BASE==RTL8192_C)
VOID
odm_TXPowerTrackingCallbackRXGainThermalMeter92D(
	IN PADAPTER 	Adapter
	)
{
	u1Byte			index_mapping[Rx_index_mapping_NUM] = {
						0x0f,	0x0f,	0x0f,	0x0f,	0x0b,
						0x0a,	0x09,	0x08,	0x07,	0x06,
						0x05,	0x04,	0x04,	0x03,	0x02						
					};

#ifndef AP_BUILD_WORKAROUND
	u1Byte			eRFPath;
	u4Byte			u4tmp;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	
	u4tmp = (index_mapping[(pHalData->EEPROMThermalMeter - pHalData->ThermalValue_RxGain)]) << 12;

	ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("===>odm_TXPowerTrackingCallbackRXGainThermalMeter92D interface %u  Rx Gain %x\n", Adapter->interfaceIndex, u4tmp));
	
	for(eRFPath = RF_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
		PHY_SetRFReg(Adapter, (ODM_RF_RADIO_PATH_E)eRFPath, RF_RXRF_A3, bRFRegOffsetMask, (pHalData->RegRF3C[eRFPath]&(~(0xF000)))|u4tmp);
#endif	

};	


//091212 chiyokolin
VOID
odm_TXPowerTrackingCallbackThermalMeter92D(
            IN PADAPTER	Adapter
            )
{

#ifndef AP_BUILD_WORKAROUND

//#if (RT_PLATFORM == PLATFORM_WINDOWS) && (HAL_CODE_BASE==RTL8192_C)
#if (HAL_CODE_BASE==RTL8192_C)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	u1Byte			ThermalValue = 0, delta, delta_LCK, delta_IQK, delta_RxGain, index, offset;
	u1Byte			ThermalValue_AVG_count = 0;
	u4Byte			ThermalValue_AVG = 0;	
	s4Byte 			ele_A=0, ele_D, TempCCk, X, value32;
	s4Byte			Y, ele_C=0;
	s1Byte			OFDM_index[2], CCK_index=0, OFDM_index_old[2], CCK_index_old=0;
	u4Byte			i = 0;
	BOOLEAN			is2T = (IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID)) ;
	BOOLEAN			bInteralPA = FALSE;

	u1Byte			OFDM_min_index = 6, OFDM_min_index_internalPA = 3, rf; //OFDM BB Swing should be less than +3.0dB, which is required by Arthur
	u1Byte			Indexforchannel = GetRightChnlPlaceforIQK(pHalData->CurrentChannel);
	u1Byte			index_mapping[5][index_mapping_NUM] = {	
					{0,	1,	3,	6,	8,	9,				//5G, path A/MAC 0, decrease power 
					11,	13,	14,	16,	17,	18, 18},	
					{0,	2,	4,	5,	7,	10,				//5G, path A/MAC 0, increase power 
					12,	14,	16,	18,	18,	18,	18},					
					{0,	2,	3,	6,	8,	9,				//5G, path B/MAC 1, decrease power
					11,	13,	14,	16,	17,	18,	18},		
					{0,	2,	4,	5,	7,	10,				//5G, path B/MAC 1, increase power
					13,	16,	16,	18,	18,	18,	18},					
					{0,	1,	2,	3,	4,	5,				//2.4G, for decreas power
					6,	7,	7,	8,	9,	10,	10},												
					};

u1Byte				index_mapping_internalPA[8][index_mapping_NUM] = { 
					{0, 	1,	2,	4,	6,	7,				//5G, path A/MAC 0, ch36-64, decrease power 
					9, 	11, 	12, 	14, 	15, 	16, 	16},	
					{0, 	2,	4,	5,	7,	10, 				//5G, path A/MAC 0, ch36-64, increase power 
					12, 	14, 	16, 	18, 	18, 	18, 	18},					
					{0, 	1,	2,	3,	5,	6,				//5G, path A/MAC 0, ch100-165, decrease power 
					8,	10, 	11, 	13, 	14, 	15, 	15},	
					{0, 	2,	4,	5,	7,	10, 				//5G, path A/MAC 0, ch100-165, increase power 
					12, 	14, 	16, 	18, 	18, 	18, 	18},						
					{0, 	1,	2,	4,	6,	7,				//5G, path B/MAC 1, ch36-64, decrease power
					9,	11, 	12, 	14, 	15, 	16, 	16},		
					{0, 	2,	4,	5,	7,	10, 				//5G, path B/MAC 1, ch36-64, increase power
					13, 	16, 	16, 	18, 	18, 	18, 	18},					
					{0, 	1,	2,	3,	5,	6,				//5G, path B/MAC 1, ch100-165, decrease power
					8,	9,	10, 	12, 	13, 	14, 	14},		
					{0, 	2,	4,	5,	7,	10, 				//5G, path B/MAC 1, ch100-165, increase power
					13, 	16, 	16, 	18, 	18, 	18, 	18},																						
				};

//#if MP_DRIVER != 1
//	return;
//#endif
	

	pHalData->TXPowerTrackingCallbackCnt++;	//cosa add for debug
	pHalData->bTXPowerTrackingInit = TRUE;

	ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("===>dm_TXPowerTrackingCallback_ThermalMeter_92D interface %u txpowercontrol %d\n", Adapter->interfaceIndex, pHalData->TxPowerTrackControl));

	ThermalValue = (u1Byte)PHY_QueryRFReg(Adapter, RF_PATH_A, RF_T_METER_92D, 0xf800);	//0x42: RF Reg[15:11] 92D

	ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", ThermalValue, pHalData->ThermalValue, pHalData->EEPROMThermalMeter));

	//PHY_APCalibrate(Adapter, (ThermalValue - pHalData->EEPROMThermalMeter));
	//if (IS_HARDWARE_TYPE_8188E(Adapter)/* ||
	//	is_ha*/)
	//{
	//	PHY_APCalibrate_8188E(Adapter, (ThermalValue - pHalData->EEPROMThermalMeter));
	//}
	//else if (IS_HARDWARE_TYPE_8192C(Adapter) || 
	//		IS_HARDWARE_TYPE_8192D(Adapter) ||
	//		IS_HARDWARE_TYPE_8723A(Adapter))
	{
		PHY_APCalibrate_8192C(Adapter, (ThermalValue - pHalData->EEPROMThermalMeter));
	}

//	if(!pHalData->TxPowerTrackControl)
//		return;

	if(is2T)
		rf = 2;
	else
		rf = 1;
	
	if(ThermalValue)
	{
//		if(!pHalData->ThermalValue)
		{
			//Query OFDM path A default setting 		
			ele_D = PHY_QueryBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord)&bMaskOFDM_D;
			for(i=0; i<OFDM_TABLE_SIZE_92D; i++)	//find the index
			{
				if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
				{
					OFDM_index_old[0] = (u1Byte)i;
					ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("Initial pathA ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n", 
						rOFDM0_XATxIQImbalance, ele_D, OFDM_index_old[0]));
					break;
				}
			}

			//Query OFDM path B default setting 
			if(is2T)
			{
				ele_D = PHY_QueryBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord)&bMaskOFDM_D;
				for(i=0; i<OFDM_TABLE_SIZE_92D; i++)	//find the index
				{
					if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
					{
						OFDM_index_old[1] = (u1Byte)i;
						ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("Initial pathB ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n", 
							rOFDM0_XBTxIQImbalance, ele_D, OFDM_index_old[1]));
						break;
					}
				}
			}
			
			if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
			{
				//Query CCK default setting From 0xa24
				TempCCk = pHalData->RegA24;

				for(i=0 ; i<CCK_TABLE_SIZE ; i++)
				{
					if(pHalData->bCCKinCH14)
					{
						if(PlatformCompareMemory((void*)&TempCCk, (void*)&CCKSwingTable_Ch14[i][2], 4)==0)
						{
							CCK_index_old =(u1Byte) i;
							ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch 14 %d\n", 
								rCCK0_TxFilter2, TempCCk, CCK_index_old, pHalData->bCCKinCH14));
							break;
						}
					}
					else
					{
						if(PlatformCompareMemory((void*)&TempCCk, (void*)&CCKSwingTable_Ch1_Ch13[i][2], 4)==0)
						{
							CCK_index_old =(u1Byte) i;
							ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch14 %d\n", 
								rCCK0_TxFilter2, TempCCk, CCK_index_old, pHalData->bCCKinCH14));
							break;
						}			
					}
				}
			}
			else
			{
				TempCCk = 0x090e1317;
				CCK_index_old = 12;
			}

			if(!pHalData->ThermalValue)
			{
				pHalData->ThermalValue = pHalData->EEPROMThermalMeter;
				pHalData->ThermalValue_LCK = ThermalValue;				
				pHalData->ThermalValue_IQK = ThermalValue;								
				pHalData->ThermalValue_RxGain = pHalData->EEPROMThermalMeter;		
				
				for(i = 0; i < rf; i++)
					pHalData->OFDM_index[i] = OFDM_index_old[i];
				pHalData->CCK_index = CCK_index_old;
			}			

			if(pHalData->bReloadtxpowerindex)
			{
				ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("reload ofdm index for band switch\n"));				
			}

			//calculate average thermal meter
			{
				pHalData->ThermalValue_AVG[pHalData->ThermalValue_AVG_index] = ThermalValue;
				pHalData->ThermalValue_AVG_index++;
				if(pHalData->ThermalValue_AVG_index == AVG_THERMAL_NUM)
					pHalData->ThermalValue_AVG_index = 0;

				for(i = 0; i < AVG_THERMAL_NUM; i++)
				{
					if(pHalData->ThermalValue_AVG[i])
					{
						ThermalValue_AVG += pHalData->ThermalValue_AVG[i];
						ThermalValue_AVG_count++;
					}
				}

				if(ThermalValue_AVG_count)
					ThermalValue = (u1Byte)(ThermalValue_AVG / ThermalValue_AVG_count);
			}			
		}

		if(pHalData->bReloadtxpowerindex)
		{
			delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);				
			pHalData->bReloadtxpowerindex = FALSE;	
			pHalData->bDoneTxpower = FALSE;
		}
		else if(pHalData->bDoneTxpower)
		{
			delta = (ThermalValue > pHalData->ThermalValue)?(ThermalValue - pHalData->ThermalValue):(pHalData->ThermalValue - ThermalValue);
		}
		else
		{
			delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);		
		}
		delta_LCK = (ThermalValue > pHalData->ThermalValue_LCK)?(ThermalValue - pHalData->ThermalValue_LCK):(pHalData->ThermalValue_LCK - ThermalValue);
		delta_IQK = (ThermalValue > pHalData->ThermalValue_IQK)?(ThermalValue - pHalData->ThermalValue_IQK):(pHalData->ThermalValue_IQK - ThermalValue);
		delta_RxGain = (ThermalValue > pHalData->ThermalValue_RxGain)?(ThermalValue - pHalData->ThermalValue_RxGain):(pHalData->ThermalValue_RxGain - ThermalValue);

		ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("interface %u Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x delta 0x%x delta_LCK 0x%x delta_IQK 0x%x delta_RxGain 0x%x\n",  Adapter->interfaceIndex, ThermalValue, pHalData->ThermalValue, pHalData->EEPROMThermalMeter, delta, delta_LCK, delta_IQK, delta_RxGain));
		ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("interface %u pre thermal meter LCK 0x%x pre thermal meter IQK 0x%x delta_LCK_bound 0x%x delta_IQK_bound 0x%x\n",  Adapter->interfaceIndex, pHalData->ThermalValue_LCK, pHalData->ThermalValue_IQK, pHalData->Delta_LCK, pHalData->Delta_IQK));

		if((delta_LCK > pHalData->Delta_LCK) && (pHalData->Delta_LCK != 0))
		{
			pHalData->ThermalValue_LCK = ThermalValue;
			PHY_LCCalibrate(Adapter);
		}
		
		if(delta > 0 && pHalData->TxPowerTrackControl)
		{
			delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);		

			//calculate new OFDM / CCK offset	
			{
				if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
				{
					offset = 4;
				
					if(delta > index_mapping_NUM-1)					
						index = index_mapping[offset][index_mapping_NUM-1];
					else
						index = index_mapping[offset][delta];
				
					if(ThermalValue > pHalData->EEPROMThermalMeter)
					{ 
						for(i = 0; i < rf; i++)
						 	OFDM_index[i] = pHalData->OFDM_index[i] -delta;
						CCK_index = pHalData->CCK_index -delta;
					}
					else
					{
						for(i = 0; i < rf; i++)			
							OFDM_index[i] = pHalData->OFDM_index[i] + index;
						CCK_index = pHalData->CCK_index + index;
					}
				}
				else if(pHalData->CurrentBandType92D == BAND_ON_5G)
				{
					for(i = 0; i < rf; i++)
					{
						if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY &&
							Adapter->interfaceIndex == 1)		//MAC 1 5G
							bInteralPA = pHalData->InternalPA5G[1];
						else
							bInteralPA = pHalData->InternalPA5G[i];	
					
						if(bInteralPA)
						{
							if(Adapter->interfaceIndex == 1 || i == rf)
								offset = 4;
							else
								offset = 0;

							if(pHalData->CurrentChannel >= 100 && pHalData->CurrentChannel <= 165)
								offset += 2;													
						}
						else
						{					
						if(Adapter->interfaceIndex == 1 || i == rf)
							offset = 2;
						else
							offset = 0;
						}

						if(ThermalValue > pHalData->EEPROMThermalMeter)	//set larger Tx power
							offset++;		
						
						if(bInteralPA)
						{
							if(delta > index_mapping_NUM-1)					
								index = index_mapping_internalPA[offset][index_mapping_NUM-1];
							else
								index = index_mapping_internalPA[offset][delta];						
						}
						else
						{						
						if(delta > index_mapping_NUM-1)					
							index = index_mapping[offset][index_mapping_NUM-1];
						else
							index = index_mapping[offset][delta];
						}
						
						if(ThermalValue > pHalData->EEPROMThermalMeter)	//set larger Tx power
						{
							if(bInteralPA && ThermalValue > 0x12)
							{
								 OFDM_index[i] = pHalData->OFDM_index[i] -((delta/2)*3+(delta%2));							
							}
							else	
							{
							 OFDM_index[i] = pHalData->OFDM_index[i] -index;
						}
						}
						else
						{				
							OFDM_index[i] = pHalData->OFDM_index[i] + index;
						}
					}
				}
				
				if(is2T)
				{
					ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("temp OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
						pHalData->OFDM_index[0], pHalData->OFDM_index[1], pHalData->CCK_index));			
				}
				else
				{
					ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("temp OFDM_A_index=0x%x, CCK_index=0x%x\n", 
						pHalData->OFDM_index[0], pHalData->CCK_index));			
				}
				
				for(i = 0; i < rf; i++)
				{
					if(OFDM_index[i] > OFDM_TABLE_SIZE_92D-1)
					{
						OFDM_index[i] = OFDM_TABLE_SIZE_92D-1;
					}
					else if(bInteralPA || pHalData->CurrentBandType92D == BAND_ON_2_4G)
					{
						if (OFDM_index[i] < OFDM_min_index_internalPA)
							OFDM_index[i] = OFDM_min_index_internalPA;
					}
					else if (OFDM_index[i] < OFDM_min_index)
					{
						OFDM_index[i] = OFDM_min_index;
				}
				}

				if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
				{
					if(CCK_index > CCK_TABLE_SIZE-1)
						CCK_index = CCK_TABLE_SIZE-1;
					else if (CCK_index < 0)
						CCK_index = 0;
				}

				if(is2T)
				{
					ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("new OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
						OFDM_index[0], OFDM_index[1], CCK_index));
				}
				else
				{
					ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("new OFDM_A_index=0x%x, CCK_index=0x%x\n", 
						OFDM_index[0], CCK_index));	
				}
			}

			//Config by SwingTable
			if(pHalData->TxPowerTrackControl && !pHalData->bNOPG)			
			{
				pHalData->bDoneTxpower = TRUE;			

				//Adujst OFDM Ant_A according to IQK result
				ele_D = (OFDMSwingTable[(u1Byte)OFDM_index[0]] & 0xFFC00000)>>22;
//				X = pHalData->RegE94;
//				Y = pHalData->RegE9C;		
				X = pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][0];
				Y = pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][1];

				if(X != 0 && pHalData->CurrentBandType92D == BAND_ON_2_4G)
				{
					if ((X & 0x00000200) != 0)
						X = X | 0xFFFFFC00;
					ele_A = ((X * ele_D)>>8)&0x000003FF;
						
					//new element C = element D x Y
					if ((Y & 0x00000200) != 0)
						Y = Y | 0xFFFFFC00;
					ele_C = ((Y * ele_D)>>8)&0x000003FF;
					
					//wirte new elements A, C, D to regC80 and regC94, element B is always 0
					value32 = (ele_D<<22)|((ele_C&0x3F)<<16)|ele_A;
					PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, value32);

					value32 = (ele_C&0x000003C0)>>6;
					PHY_SetBBReg(Adapter, rOFDM0_XCTxAFE, bMaskH4Bits, value32);

					value32 = ((X * ele_D)>>7)&0x01;
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT24, value32);
					
				}
				else
				{
					PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable[(u1Byte)OFDM_index[0]]);				
					PHY_SetBBReg(Adapter, rOFDM0_XCTxAFE, bMaskH4Bits, 0x00);
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT24, 0x00);			
				}

				ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD, ("TxPwrTracking for interface %u path A: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x 0xe94 = 0x%x 0xe9c = 0x%x\n", 
					(u1Byte)Adapter->interfaceIndex, (u4Byte)X, (u4Byte)Y, (u4Byte)ele_A, (u4Byte)ele_C, (u4Byte)ele_D, (u4Byte)X, (u4Byte)Y));		

				
				if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
				{
					//Adjust CCK according to IQK result
					if(!pHalData->bCCKinCH14){
						PlatformEFIOWrite1Byte(Adapter, 0xa22, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][0]);
						PlatformEFIOWrite1Byte(Adapter, 0xa23, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][1]);
						PlatformEFIOWrite1Byte(Adapter, 0xa24, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][2]);
						PlatformEFIOWrite1Byte(Adapter, 0xa25, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][3]);
						PlatformEFIOWrite1Byte(Adapter, 0xa26, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][4]);
						PlatformEFIOWrite1Byte(Adapter, 0xa27, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][5]);
						PlatformEFIOWrite1Byte(Adapter, 0xa28, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][6]);
						PlatformEFIOWrite1Byte(Adapter, 0xa29, CCKSwingTable_Ch1_Ch13[(u1Byte)CCK_index][7]);		
					}
					else{
						PlatformEFIOWrite1Byte(Adapter, 0xa22, CCKSwingTable_Ch14[(u1Byte)CCK_index][0]);
						PlatformEFIOWrite1Byte(Adapter, 0xa23, CCKSwingTable_Ch14[(u1Byte)CCK_index][1]);
						PlatformEFIOWrite1Byte(Adapter, 0xa24, CCKSwingTable_Ch14[(u1Byte)CCK_index][2]);
						PlatformEFIOWrite1Byte(Adapter, 0xa25, CCKSwingTable_Ch14[(u1Byte)CCK_index][3]);
						PlatformEFIOWrite1Byte(Adapter, 0xa26, CCKSwingTable_Ch14[(u1Byte)CCK_index][4]);
						PlatformEFIOWrite1Byte(Adapter, 0xa27, CCKSwingTable_Ch14[(u1Byte)CCK_index][5]);
						PlatformEFIOWrite1Byte(Adapter, 0xa28, CCKSwingTable_Ch14[(u1Byte)CCK_index][6]);
						PlatformEFIOWrite1Byte(Adapter, 0xa29, CCKSwingTable_Ch14[(u1Byte)CCK_index][7]);	
					}		
				}
				
				if(is2T)
				{						
					ele_D = (OFDMSwingTable[(u1Byte)OFDM_index[1]] & 0xFFC00000)>>22;
					
					//new element A = element D x X
//					X = pHalData->RegEB4;
//					Y = pHalData->RegEBC;
					X = pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][4];
					Y = pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][5];
					
					if(X != 0 && pHalData->CurrentBandType92D == BAND_ON_2_4G)
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
						PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, value32);

						value32 = (ele_C&0x000003C0)>>6;
						PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, value32);	
						
						value32 = ((X * ele_D)>>7)&0x01;
						PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT28, value32);

					}
					else
					{
						PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, OFDMSwingTable[(u1Byte)OFDM_index[1]]);										
						PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, 0x00);	
						PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT28, 0x00);				
					}

					ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD, ("TxPwrTracking path B: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x 0xeb4 = 0x%x 0xebc = 0x%x\n", 
						(u4Byte)X, (u4Byte)Y, (u4Byte)ele_A, (u4Byte)ele_C, (u4Byte)ele_D, (u4Byte)X, (u4Byte)Y));			
				}
				
				ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD, ("TxPwrTracking 0xc80 = 0x%x, 0xc94 = 0x%x RF 0x24 = 0x%x\n", PHY_QueryBBReg(Adapter, 0xc80, bMaskDWord), PHY_QueryBBReg(Adapter, 0xc94, bMaskDWord), PHY_QueryRFReg(Adapter, RF_PATH_A, 0x24, bRFRegOffsetMask)));
			}			
		}
		
		if((delta_IQK > pHalData->Delta_IQK) && (pHalData->Delta_IQK != 0))
		{
			PHY_ResetIQKResult(Adapter);		
			pHalData->ThermalValue_IQK = ThermalValue;
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)	
#if USE_WORKITEM
			PlatformAcquireMutex(&pHalData->mxChnlBwControl);
#else
			PlatformAcquireSpinLock(Adapter, RT_CHANNEL_AND_BANDWIDTH_SPINLOCK);
#endif
#elif((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
			PlatformAcquireMutex(&pHalData->mxChnlBwControl);
#endif
			
			PHY_IQCalibrate(Adapter, FALSE);

#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)	
#if USE_WORKITEM
			PlatformReleaseMutex(&pHalData->mxChnlBwControl);
#else
			PlatformReleaseSpinLock(Adapter, RT_CHANNEL_AND_BANDWIDTH_SPINLOCK);
#endif
#elif((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
			PlatformReleaseMutex(&pHalData->mxChnlBwControl);
#endif

		}

		if(delta_RxGain > 0 && pHalData->CurrentBandType92D == BAND_ON_5G 
			&& ThermalValue <= pHalData->EEPROMThermalMeter)
		{
			pHalData->ThermalValue_RxGain = ThermalValue;		
			odm_TXPowerTrackingCallbackRXGainThermalMeter92D(Adapter);
		}

		//update thermal meter value
		if(pHalData->TxPowerTrackControl)
		{
			Adapter->HalFunc.SetHalDefVarHandler(Adapter, HAL_DEF_THERMAL_VALUE, &ThermalValue);
		}
			
	}

	ODM_RT_TRACE(pDM_Odm,COMP_POWER_TRACKING, DBG_LOUD,("<===dm_TXPowerTrackingCallback_ThermalMeter_92D\n"));
	
	pHalData->TXPowercount = 0;
#endif
#endif
}


//
//	Description:
//		- Dispatch TxPower Tracking direct call ONLY for 92s.
//		- We shall NOT schedule Workitem within PASSIVE LEVEL, which will cause system resource
//		   leakage under some platform.
//
//	Assumption:
//		PASSIVE_LEVEL when this routine is called.
//
//	Added by Roger, 2009.06.18.
//
VOID
odm_TXPowerTrackingDirectCall92C(
	IN	PADAPTER		Adapter
	)
{
#ifndef AP_BUILD_WORKAROUND
	if(IS_HARDWARE_TYPE_8192D(Adapter))
		odm_TXPowerTrackingCallbackThermalMeter92D(Adapter);
	else
		odm_TXPowerTrackingCallbackThermalMeter92C(Adapter);
#endif	
}


VOID
odm_TXPowerTrackingCallback_ThermalMeter_92C(
	IN PADAPTER	Adapter
	)
{
#if ((RT_PLATFORM == PLATFORM_WINDOWS) || (RT_PLATFORM == PLATFORM_LINUX)) && (HAL_CODE_BASE==RTL8192_C)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u1Byte			ThermalValue = 0, delta, delta_LCK, delta_IQK, delta_HP, TimeOut = 100;
	s4Byte 			ele_A=0, ele_D, TempCCk, X, value32;
	s4Byte			Y, ele_C=0;
	s1Byte			OFDM_index[2], CCK_index=0, OFDM_index_old[2], CCK_index_old=0;
	int	    			i = 0;
	BOOLEAN			is2T = IS_92C_SERIAL(pHalData->VersionID);

#if MP_DRIVER == 1
	PMPT_CONTEXT	pMptCtx = &(Adapter->MptCtx);	
	pu1Byte			TxPwrLevel = pMptCtx->TxPwrLevel;
#endif	
	u1Byte			OFDM_min_index = 6, rf; //OFDM BB Swing should be less than +3.0dB, which is required by Arthur
#if 0	
	u4Byte			DPK_delta_mapping[2][DPK_DELTA_MAPPING_NUM] = {
					{0x1c, 0x1c, 0x1d, 0x1d, 0x1e, 
					 0x1f, 0x00, 0x00, 0x01, 0x01,
					 0x02, 0x02, 0x03},
					{0x1c, 0x1d, 0x1e, 0x1e, 0x1e,
					 0x1f, 0x00, 0x00, 0x01, 0x02,
					 0x02, 0x03, 0x03}};
#endif	
#if DEV_BUS_TYPE==RT_USB_INTERFACE		
	u1Byte			ThermalValue_HP_count = 0;
	u4Byte			ThermalValue_HP = 0;
	s1Byte			index_mapping_HP[index_mapping_HP_NUM] = {
					0,	1,	3,	4,	6,	
					7,	9,	10,	12,	13,	
					15,	16,	18,	19,	21
					};

	s1Byte			index_HP;
#endif

	if (ODM_CheckPowerStatus(Adapter) == FALSE)
		return;
	
	pHalData->TXPowerTrackingCallbackCnt++;	//cosa add for debug
	pHalData->bTXPowerTrackingInit = TRUE;

	RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("===>odm_TXPowerTrackingCallback_ThermalMeter_92C\n"));

	ThermalValue = (u1Byte)PHY_QueryRFReg(Adapter, RF_PATH_A, RF_T_METER, 0x1f);	// 0x24: RF Reg[4:0]	
	RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", ThermalValue, pHalData->ThermalValue, pHalData->EEPROMThermalMeter));

	PHY_APCalibrate_8192C(Adapter, (ThermalValue - pHalData->EEPROMThermalMeter));

	if(is2T)
		rf = 2;
	else
		rf = 1;
	
	while(PlatformAtomicExchange(&Adapter->IntrCCKRefCount, TRUE) == TRUE) 
	{
		PlatformSleepUs(100);
		TimeOut--;
		if(TimeOut <= 0)
		{
			RTPRINT(FINIT, INIT_TxPower, 
			 ("!!!odm_TXPowerTrackingCallback_ThermalMeter_92C Wait for check CCK gain index too long!!!\n" ));
			break;
		}
	}
	
	if(ThermalValue)
	{
//		if(!pHalData->ThermalValue)
		{
			//Query OFDM path A default setting 		
			ele_D = PHY_QueryBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord)&bMaskOFDM_D;
			for(i=0; i<OFDM_TABLE_SIZE_92C; i++)	//find the index
			{
				if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
				{
					OFDM_index_old[0] = (u1Byte)i;
					RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Initial pathA ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n", 
						rOFDM0_XATxIQImbalance, ele_D, OFDM_index_old[0]));
					break;
				}
			}

			//Query OFDM path B default setting 
			if(is2T)
			{
				ele_D = PHY_QueryBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord)&bMaskOFDM_D;
				for(i=0; i<OFDM_TABLE_SIZE_92C; i++)	//find the index
				{
					if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
					{
						OFDM_index_old[1] = (u1Byte)i;
						RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Initial pathB ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n", 
							rOFDM0_XBTxIQImbalance, ele_D, OFDM_index_old[1]));
						break;
					}
				}
			}

			//Query CCK default setting From 0xa24
			TempCCk = PHY_QueryBBReg(Adapter, rCCK0_TxFilter2, bMaskDWord)&bMaskCCK;
			for(i=0 ; i<CCK_TABLE_SIZE ; i++)
			{
				if(pHalData->bCCKinCH14)
				{
					if(PlatformCompareMemory((void*)&TempCCk, (void*)&CCKSwingTable_Ch14[i][2], 4)==0)
					{
						CCK_index_old =(u1Byte) i;
						RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch 14 %d\n", 
							rCCK0_TxFilter2, TempCCk, CCK_index_old, pHalData->bCCKinCH14));
						break;
					}
				}
				else
				{
					if(PlatformCompareMemory((void*)&TempCCk, (void*)&CCKSwingTable_Ch1_Ch13[i][2], 4)==0)
					{
						CCK_index_old =(u1Byte) i;
						RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch14 %d\n", 
							rCCK0_TxFilter2, TempCCk, CCK_index_old, pHalData->bCCKinCH14));
						break;
					}			
				}
			}	

			if(!pHalData->ThermalValue)
			{
				pHalData->ThermalValue = pHalData->EEPROMThermalMeter;
				pHalData->ThermalValue_LCK = ThermalValue;				
				pHalData->ThermalValue_IQK = ThermalValue;								
				pHalData->ThermalValue_DPK = pHalData->EEPROMThermalMeter;
				
#if DEV_BUS_TYPE==RT_USB_INTERFACE				
				for(i = 0; i < rf; i++)
					pHalData->OFDM_index_HP[i] = pHalData->OFDM_index[i] = OFDM_index_old[i];
				pHalData->CCK_index_HP = pHalData->CCK_index = CCK_index_old;
#else
				for(i = 0; i < rf; i++)
					pHalData->OFDM_index[i] = OFDM_index_old[i];
				pHalData->CCK_index = CCK_index_old;
#endif
			}	

#if DEV_BUS_TYPE==RT_USB_INTERFACE				
			if(RT_GetInterfaceSelection(Adapter) == INTF_SEL1_USB_High_Power)
			{
				pHalData->ThermalValue_HP[pHalData->ThermalValue_HP_index] = ThermalValue;
				pHalData->ThermalValue_HP_index++;
				if(pHalData->ThermalValue_HP_index == HP_THERMAL_NUM)
					pHalData->ThermalValue_HP_index = 0;

				for(i = 0; i < HP_THERMAL_NUM; i++)
				{
					if(pHalData->ThermalValue_HP[i])
					{
						ThermalValue_HP += pHalData->ThermalValue_HP[i];
						ThermalValue_HP_count++;
					}			
				}
		
				if(ThermalValue_HP_count)
					ThermalValue = (u1Byte)(ThermalValue_HP / ThermalValue_HP_count);
			}
#endif
		}
		
		delta = (ThermalValue > pHalData->ThermalValue)?(ThermalValue - pHalData->ThermalValue):(pHalData->ThermalValue - ThermalValue);
#if DEV_BUS_TYPE==RT_USB_INTERFACE				
		if(RT_GetInterfaceSelection(Adapter) == INTF_SEL1_USB_High_Power)
		{
			if(pHalData->bDoneTxpower)
				delta_HP = (ThermalValue > pHalData->ThermalValue)?(ThermalValue - pHalData->ThermalValue):(pHalData->ThermalValue - ThermalValue);
			else
				delta_HP = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);						
		}
		else
#endif	
		{
			delta_HP = 0;			
		}
		delta_LCK = (ThermalValue > pHalData->ThermalValue_LCK)?(ThermalValue - pHalData->ThermalValue_LCK):(pHalData->ThermalValue_LCK - ThermalValue);
		delta_IQK = (ThermalValue > pHalData->ThermalValue_IQK)?(ThermalValue - pHalData->ThermalValue_IQK):(pHalData->ThermalValue_IQK - ThermalValue);

		RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x delta 0x%x delta_LCK 0x%x delta_IQK 0x%x\n", ThermalValue, pHalData->ThermalValue, pHalData->EEPROMThermalMeter, delta, delta_LCK, delta_IQK));

		if(delta_LCK > 1)
		{
			pHalData->ThermalValue_LCK = ThermalValue;
			PHY_LCCalibrate_8192C(Adapter);
		}
		
		if((delta > 0 || delta_HP > 0)&& pHalData->TxPowerTrackControl)
		{
#if DEV_BUS_TYPE==RT_USB_INTERFACE		
			if(RT_GetInterfaceSelection(Adapter) == INTF_SEL1_USB_High_Power)
			{
				pHalData->bDoneTxpower = TRUE;
				delta_HP = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);						
				
				if(delta_HP > index_mapping_HP_NUM-1)					
					index_HP = index_mapping_HP[index_mapping_HP_NUM-1];
				else
					index_HP = index_mapping_HP[delta_HP];
				
				if(ThermalValue > pHalData->EEPROMThermalMeter)	//set larger Tx power
				{
					for(i = 0; i < rf; i++)
					 	OFDM_index[i] = pHalData->OFDM_index_HP[i] - index_HP;
					CCK_index = pHalData->CCK_index_HP -index_HP;						
				}
				else
				{
					for(i = 0; i < rf; i++)
						OFDM_index[i] = pHalData->OFDM_index_HP[i] + index_HP;
					CCK_index = pHalData->CCK_index_HP + index_HP;						
				}	
				
				delta_HP = (ThermalValue > pHalData->ThermalValue)?(ThermalValue - pHalData->ThermalValue):(pHalData->ThermalValue - ThermalValue);
				
			}
			else
#endif				
			{
				if(ThermalValue > pHalData->ThermalValue)
				{ 
					for(i = 0; i < rf; i++)
					 	pHalData->OFDM_index[i] -= delta;
					pHalData->CCK_index -= delta;
				}
				else
				{
					for(i = 0; i < rf; i++)			
						pHalData->OFDM_index[i] += delta;
					pHalData->CCK_index += delta;
				}
			}
			
			if(is2T)
			{
				RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("temp OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
					pHalData->OFDM_index[0], pHalData->OFDM_index[1], pHalData->CCK_index));			
			}
			else
			{
				RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("temp OFDM_A_index=0x%x, CCK_index=0x%x\n", 
					pHalData->OFDM_index[0], pHalData->CCK_index));			
			}
			
			//no adjust
#if DEV_BUS_TYPE==RT_USB_INTERFACE					
			if(RT_GetInterfaceSelection(Adapter) != INTF_SEL1_USB_High_Power)
#endif				
			{
				if(ThermalValue > pHalData->EEPROMThermalMeter)
				{
					for(i = 0; i < rf; i++)			
						OFDM_index[i] = pHalData->OFDM_index[i]+1;
					CCK_index = pHalData->CCK_index+1;			
				}
				else
				{
					for(i = 0; i < rf; i++)			
						OFDM_index[i] = pHalData->OFDM_index[i];
					CCK_index = pHalData->CCK_index;						
				}

#if MP_DRIVER == 1
				for(i = 0; i < rf; i++)
				{
					if(TxPwrLevel[i] >=0 && TxPwrLevel[i] <=26)
					{
						if(ThermalValue > pHalData->EEPROMThermalMeter)
						{
							if (delta < 5)
								OFDM_index[i] -= 1;					
							else 
								OFDM_index[i] -= 2;					
						}
						else if(delta > 5 && ThermalValue < pHalData->EEPROMThermalMeter)
						{
							OFDM_index[i] += 1;
						}
					}
					else if (TxPwrLevel[i] >= 27 && TxPwrLevel[i] <= 32 && ThermalValue > pHalData->EEPROMThermalMeter)
					{
						if (delta < 5)
							OFDM_index[i] -= 1;					
						else 
							OFDM_index[i] -= 2;								
					}
					else if (TxPwrLevel[i] >= 32 && TxPwrLevel[i] <= 38 && ThermalValue > pHalData->EEPROMThermalMeter && delta > 5)
					{
						OFDM_index[i] -= 1;								
					}
				}

				{
					if(TxPwrLevel[i] >=0 && TxPwrLevel[i] <=26)
					{
						if(ThermalValue > pHalData->EEPROMThermalMeter)
						{
							if (delta < 5)
								CCK_index -= 1; 				
							else 
								CCK_index -= 2; 				
						}
						else if(delta > 5 && ThermalValue < pHalData->EEPROMThermalMeter)
						{
							CCK_index += 1;
						}
					}
					else if (TxPwrLevel[i] >= 27 && TxPwrLevel[i] <= 32 && ThermalValue > pHalData->EEPROMThermalMeter)
					{
						if (delta < 5)
							CCK_index -= 1; 				
						else 
							CCK_index -= 2; 							
					}
					else if (TxPwrLevel[i] >= 32 && TxPwrLevel[i] <= 38 && ThermalValue > pHalData->EEPROMThermalMeter && delta > 5)
					{
						CCK_index -= 1; 							
					}
				}
#endif				
			}
			
			for(i = 0; i < rf; i++)
			{
				if(OFDM_index[i] > (OFDM_TABLE_SIZE_92C-1))
					OFDM_index[i] = (OFDM_TABLE_SIZE_92C-1);
				else if (OFDM_index[i] < OFDM_min_index)
					OFDM_index[i] = OFDM_min_index;
			}
						
			if(CCK_index > (CCK_TABLE_SIZE-1))
				CCK_index = (CCK_TABLE_SIZE-1);
			else if (CCK_index < 0)
				CCK_index = 0;		

			if(is2T)
			{
				RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("new OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
					OFDM_index[0], OFDM_index[1], CCK_index));
			}
			else
			{
				RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("new OFDM_A_index=0x%x, CCK_index=0x%x\n", 
					OFDM_index[0], CCK_index));			
			}
		}

		if(pHalData->TxPowerTrackControl && (delta != 0 || delta_HP != 0))
		{
			//Adujst OFDM Ant_A according to IQK result
			ele_D = (OFDMSwingTable[OFDM_index[0]] & 0xFFC00000)>>22;
			X = pHalData->RegE94;
			Y = pHalData->RegE9C;		

			if(X != 0)
			{
				if ((X & 0x00000200) != 0)
					X = X | 0xFFFFFC00;
				ele_A = ((X * ele_D)>>8)&0x000003FF;
					
				//new element C = element D x Y
				if ((Y & 0x00000200) != 0)
					Y = Y | 0xFFFFFC00;
				ele_C = ((Y * ele_D)>>8)&0x000003FF;
				
				//wirte new elements A, C, D to regC80 and regC94, element B is always 0
				value32 = (ele_D<<22)|((ele_C&0x3F)<<16)|ele_A;
				PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, value32);
				
				value32 = (ele_C&0x000003C0)>>6;
				PHY_SetBBReg(Adapter, rOFDM0_XCTxAFE, bMaskH4Bits, value32);

				value32 = ((X * ele_D)>>7)&0x01;
				PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT31, value32);

				value32 = ((Y * ele_D)>>7)&0x01;
				PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT29, value32);
				
			}
			else
			{
				PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable[OFDM_index[0]]);				
				PHY_SetBBReg(Adapter, rOFDM0_XCTxAFE, bMaskH4Bits, 0x00);
				PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT31|BIT29, 0x00);			
			}

			RTPRINT(FINIT, INIT_IQK, ("TxPwrTracking path A: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x\n", X, Y, ele_A, ele_C, ele_D));		

			//Adjust CCK according to IQK result
			if(!pHalData->bCCKinCH14){
				PlatformEFIOWrite1Byte(Adapter, 0xa22, CCKSwingTable_Ch1_Ch13[CCK_index][0]);
				PlatformEFIOWrite1Byte(Adapter, 0xa23, CCKSwingTable_Ch1_Ch13[CCK_index][1]);
				PlatformEFIOWrite1Byte(Adapter, 0xa24, CCKSwingTable_Ch1_Ch13[CCK_index][2]);
				PlatformEFIOWrite1Byte(Adapter, 0xa25, CCKSwingTable_Ch1_Ch13[CCK_index][3]);
				PlatformEFIOWrite1Byte(Adapter, 0xa26, CCKSwingTable_Ch1_Ch13[CCK_index][4]);
				PlatformEFIOWrite1Byte(Adapter, 0xa27, CCKSwingTable_Ch1_Ch13[CCK_index][5]);
				PlatformEFIOWrite1Byte(Adapter, 0xa28, CCKSwingTable_Ch1_Ch13[CCK_index][6]);
				PlatformEFIOWrite1Byte(Adapter, 0xa29, CCKSwingTable_Ch1_Ch13[CCK_index][7]);		
			}
			else{
				PlatformEFIOWrite1Byte(Adapter, 0xa22, CCKSwingTable_Ch14[CCK_index][0]);
				PlatformEFIOWrite1Byte(Adapter, 0xa23, CCKSwingTable_Ch14[CCK_index][1]);
				PlatformEFIOWrite1Byte(Adapter, 0xa24, CCKSwingTable_Ch14[CCK_index][2]);
				PlatformEFIOWrite1Byte(Adapter, 0xa25, CCKSwingTable_Ch14[CCK_index][3]);
				PlatformEFIOWrite1Byte(Adapter, 0xa26, CCKSwingTable_Ch14[CCK_index][4]);
				PlatformEFIOWrite1Byte(Adapter, 0xa27, CCKSwingTable_Ch14[CCK_index][5]);
				PlatformEFIOWrite1Byte(Adapter, 0xa28, CCKSwingTable_Ch14[CCK_index][6]);
				PlatformEFIOWrite1Byte(Adapter, 0xa29, CCKSwingTable_Ch14[CCK_index][7]);	
			}		

			if(is2T)
			{						
				ele_D = (OFDMSwingTable[OFDM_index[1]] & 0xFFC00000)>>22;
				
				//new element A = element D x X
				X = pHalData->RegEB4;
				Y = pHalData->RegEBC;
				
				if(X != 0){
					if ((X & 0x00000200) != 0)	//consider minus
						X = X | 0xFFFFFC00;
					ele_A = ((X * ele_D)>>8)&0x000003FF;
					
					//new element C = element D x Y
					if ((Y & 0x00000200) != 0)
						Y = Y | 0xFFFFFC00;
					ele_C = ((Y * ele_D)>>8)&0x00003FF;
					
					//wirte new elements A, C, D to regC88 and regC9C, element B is always 0
					value32=(ele_D<<22)|((ele_C&0x3F)<<16) |ele_A;
					PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, value32);

					value32 = (ele_C&0x000003C0)>>6;
					PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, value32);	
					
					value32 = ((X * ele_D)>>7)&0x01;
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT27, value32);

					value32 = ((Y * ele_D)>>7)&0x01;
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT25, value32);

				}
				else{
					PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, OFDMSwingTable[OFDM_index[1]]);										
					PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, 0x00);	
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT27|BIT25, 0x00);				
				}

				RTPRINT(FINIT, INIT_IQK, ("TxPwrTracking path B: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x\n", X, Y, ele_A, ele_C, ele_D));			
			}

			RTPRINT(FINIT, INIT_IQK, ("TxPwrTracking 0xc80 = 0x%x, 0xc94 = 0x%x RF 0x24 = 0x%x\n", PHY_QueryBBReg(Adapter, 0xc80, bMaskDWord), PHY_QueryBBReg(Adapter, 0xc94, bMaskDWord), PHY_QueryRFReg(Adapter, RF_PATH_A, 0x24, bRFRegOffsetMask)));
		}

#if MP_DRIVER == 1
		if(delta_IQK > 1)
#else
		if(delta_IQK > 3)
#endif
		{
			pHalData->ThermalValue_IQK = ThermalValue;
			PHY_IQCalibrate_8192C(Adapter, FALSE);
		}

#if 1
		if(delta > 0 && IS_HARDWARE_TYPE_8723A(Adapter))
		{
			if(ThermalValue >= 15)
				PHY_SetBBReg(Adapter, REG_AFE_XTAL_CTRL, bMaskDWord, 0x038180fd );
			else
				PHY_SetBBReg(Adapter, REG_AFE_XTAL_CTRL, bMaskDWord, 0x0381808d );				
		}
#endif

		//update thermal meter value
		if(pHalData->TxPowerTrackControl)
			Adapter->HalFunc.SetHalDefVarHandler(Adapter, HAL_DEF_THERMAL_VALUE, &ThermalValue);
			
	}

	PlatformAtomicExchange(&Adapter->IntrCCKRefCount, FALSE);		
	pHalData->TXPowercount = 0;

	// 2011/08/23 MH Add for power tracking after S3/S4  turn off RF. In this case, we need to execute IQK again. Otherwise
	// The IQK scheme will use old value to save and cause incorrect BB value.
	{
		RT_RF_POWER_STATE 	rtState;

		Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_RF_STATE, (pu1Byte)(&rtState));	
		
		if(Adapter->bDriverStopped || Adapter->bDriverIsGoingToPnpSetPowerSleep || rtState == eRfOff)
		{
			RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Incorrect pwrtrack point, re-iqk next time\n"));	
			pHalData->bIQKInitialized = FALSE;
		}
	}
	
	RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("<===odm_TXPowerTrackingCallback_ThermalMeter_92C\n"));	
#endif
}

VOID
odm_TXPowerTrackingCallback_ThermalMeter_8723A(
            IN PADAPTER	Adapter)
{
#if ((RT_PLATFORM == PLATFORM_WINDOWS) || (RT_PLATFORM == PLATFORM_LINUX)) && (HAL_CODE_BASE==RTL8192_C)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u1Byte			ThermalValue = 0, delta, delta_LCK, delta_IQK, delta_HP, TimeOut = 100;
	s4Byte 			ele_A=0, ele_D, TempCCk, X, value32;
	s4Byte			Y, ele_C=0;
	s1Byte			OFDM_index[2], CCK_index=0, OFDM_index_old[2], CCK_index_old=0;
	int	    			i = 0;
	BOOLEAN			is2T = IS_92C_SERIAL(pHalData->VersionID);

#if MP_DRIVER == 1
	PMPT_CONTEXT	pMptCtx = &(Adapter->MptCtx);	
	pu1Byte			TxPwrLevel = pMptCtx->TxPwrLevel;
#endif	
	u1Byte			OFDM_min_index = 6, rf; //OFDM BB Swing should be less than +3.0dB, which is required by Arthur
#if 0	
	u4Byte			DPK_delta_mapping[2][DPK_DELTA_MAPPING_NUM] = {
					{0x1c, 0x1c, 0x1d, 0x1d, 0x1e, 
					 0x1f, 0x00, 0x00, 0x01, 0x01,
					 0x02, 0x02, 0x03},
					{0x1c, 0x1d, 0x1e, 0x1e, 0x1e,
					 0x1f, 0x00, 0x00, 0x01, 0x02,
					 0x02, 0x03, 0x03}};
#endif	
#if DEV_BUS_TYPE==RT_USB_INTERFACE		
	u1Byte			ThermalValue_HP_count = 0;
	u4Byte			ThermalValue_HP = 0;
	s1Byte			index_mapping_HP[index_mapping_HP_NUM] = {
					0,	1,	3,	4,	6,	
					7,	9,	10,	12,	13,	
					15,	16,	18,	19,	21
					};

	s1Byte			index_HP;
#endif

	if (ODM_CheckPowerStatus(Adapter) == FALSE)
		return;
	
	pHalData->TXPowerTrackingCallbackCnt++;	//cosa add for debug
	pHalData->bTXPowerTrackingInit = TRUE;

	RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("===>odm_TXPowerTrackingCallback_ThermalMeter_92C\n"));

	ThermalValue = (u1Byte)PHY_QueryRFReg(Adapter, RF_PATH_A, RF_T_METER, 0x1f);	// 0x24: RF Reg[4:0]	
	RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", ThermalValue, pHalData->ThermalValue, pHalData->EEPROMThermalMeter));


#if DEV_BUS_TYPE==RT_USB_INTERFACE
    if (ThermalValue <= 0x16)
    { // <20120307, Kordan> Asked by Alex.
        PlatformEFIOWrite2Byte(Adapter, REG_AFE_XTAL_CTRL,
                                   ((PlatformEFIORead2Byte(Adapter, REG_AFE_XTAL_CTRL))&~(BIT4|BIT5|BIT6|BIT7)) | (BIT7));
    }
    else 
    {
        PlatformEFIOWrite2Byte(Adapter, REG_AFE_XTAL_CTRL,
                                   ((PlatformEFIORead2Byte(Adapter, REG_AFE_XTAL_CTRL))&~(BIT4|BIT5|BIT6|BIT7)) | (BIT4|BIT5|BIT6|BIT7));
    }
#endif

	PHY_APCalibrate_8192C(Adapter, (ThermalValue - pHalData->EEPROMThermalMeter));

	if(is2T)
		rf = 2;
	else
		rf = 1;
	
	while(PlatformAtomicExchange(&Adapter->IntrCCKRefCount, TRUE) == TRUE) 
	{
		PlatformSleepUs(100);
		TimeOut--;
		if(TimeOut <= 0)
		{
			RTPRINT(FINIT, INIT_TxPower, 
			 ("!!!odm_TXPowerTrackingCallback_ThermalMeter_92C Wait for check CCK gain index too long!!!\n" ));
			break;
		}
	}
	
	if(ThermalValue)
	{
//		if(!pHalData->ThermalValue)
		{
			//Query OFDM path A default setting 		
			ele_D = PHY_QueryBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord)&bMaskOFDM_D;
			for(i=0; i<OFDM_TABLE_SIZE_92C; i++)	//find the index
			{
				if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
				{
					OFDM_index_old[0] = (u1Byte)i;
					RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Initial pathA ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n", 
						rOFDM0_XATxIQImbalance, ele_D, OFDM_index_old[0]));
					break;
				}
			}

			//Query OFDM path B default setting 
			if(is2T)
			{
				ele_D = PHY_QueryBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord)&bMaskOFDM_D;
				for(i=0; i<OFDM_TABLE_SIZE_92C; i++)	//find the index
				{
					if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
					{
						OFDM_index_old[1] = (u1Byte)i;
						RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Initial pathB ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n", 
							rOFDM0_XBTxIQImbalance, ele_D, OFDM_index_old[1]));
						break;
					}
				}
			}

			//Query CCK default setting From 0xa24
			TempCCk = PHY_QueryBBReg(Adapter, rCCK0_TxFilter2, bMaskDWord)&bMaskCCK;
			for(i=0 ; i<CCK_TABLE_SIZE ; i++)
			{
				if(pHalData->bCCKinCH14)
				{
					if(PlatformCompareMemory((void*)&TempCCk, (void*)&CCKSwingTable_Ch14[i][2], 4)==0)
					{
						CCK_index_old =(u1Byte) i;
						RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch 14 %d\n", 
							rCCK0_TxFilter2, TempCCk, CCK_index_old, pHalData->bCCKinCH14));
						break;
					}
				}
				else
				{
					if(PlatformCompareMemory((void*)&TempCCk, (void*)&CCKSwingTable_Ch1_Ch13[i][2], 4)==0)
					{
						CCK_index_old =(u1Byte) i;
						RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch14 %d\n", 
							rCCK0_TxFilter2, TempCCk, CCK_index_old, pHalData->bCCKinCH14));
						break;
					}			
				}
			}	

			if(!pHalData->ThermalValue)
			{
				pHalData->ThermalValue = pHalData->EEPROMThermalMeter;
				pHalData->ThermalValue_LCK = ThermalValue;				
				pHalData->ThermalValue_IQK = ThermalValue;								
				pHalData->ThermalValue_DPK = pHalData->EEPROMThermalMeter;
				
#if DEV_BUS_TYPE==RT_USB_INTERFACE				
				for(i = 0; i < rf; i++)
					pHalData->OFDM_index_HP[i] = pHalData->OFDM_index[i] = OFDM_index_old[i];
				pHalData->CCK_index_HP = pHalData->CCK_index = CCK_index_old;
#else
				for(i = 0; i < rf; i++)
					pHalData->OFDM_index[i] = OFDM_index_old[i];
				pHalData->CCK_index = CCK_index_old;
#endif
			}	

#if DEV_BUS_TYPE==RT_USB_INTERFACE				
			if(RT_GetInterfaceSelection(Adapter) == INTF_SEL1_USB_High_Power)
			{
				pHalData->ThermalValue_HP[pHalData->ThermalValue_HP_index] = ThermalValue;
				pHalData->ThermalValue_HP_index++;
				if(pHalData->ThermalValue_HP_index == HP_THERMAL_NUM)
					pHalData->ThermalValue_HP_index = 0;

				for(i = 0; i < HP_THERMAL_NUM; i++)
				{
					if(pHalData->ThermalValue_HP[i])
					{
						ThermalValue_HP += pHalData->ThermalValue_HP[i];
						ThermalValue_HP_count++;
					}			
				}
		
				if(ThermalValue_HP_count)
					ThermalValue = (u1Byte)(ThermalValue_HP / ThermalValue_HP_count);
			}
#endif
		}
		
		delta = (ThermalValue > pHalData->ThermalValue)?(ThermalValue - pHalData->ThermalValue):(pHalData->ThermalValue - ThermalValue);
#if DEV_BUS_TYPE==RT_USB_INTERFACE				
		if(RT_GetInterfaceSelection(Adapter) == INTF_SEL1_USB_High_Power)
		{
			if(pHalData->bDoneTxpower)
				delta_HP = (ThermalValue > pHalData->ThermalValue)?(ThermalValue - pHalData->ThermalValue):(pHalData->ThermalValue - ThermalValue);
			else
				delta_HP = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);						
		}
		else
#endif	
		{
			delta_HP = 0;			
		}
		delta_LCK = (ThermalValue > pHalData->ThermalValue_LCK)?(ThermalValue - pHalData->ThermalValue_LCK):(pHalData->ThermalValue_LCK - ThermalValue);
		delta_IQK = (ThermalValue > pHalData->ThermalValue_IQK)?(ThermalValue - pHalData->ThermalValue_IQK):(pHalData->ThermalValue_IQK - ThermalValue);

		RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x delta 0x%x delta_LCK 0x%x delta_IQK 0x%x\n", ThermalValue, pHalData->ThermalValue, pHalData->EEPROMThermalMeter, delta, delta_LCK, delta_IQK));

		if(delta_LCK > 1)
		{
			pHalData->ThermalValue_LCK = ThermalValue;
			PHY_LCCalibrate(Adapter);
		}
		
		if((delta > 0 || delta_HP > 0)&& pHalData->TxPowerTrackControl)
		{
#if DEV_BUS_TYPE==RT_USB_INTERFACE		
			if(RT_GetInterfaceSelection(Adapter) == INTF_SEL1_USB_High_Power)
			{
				pHalData->bDoneTxpower = TRUE;
				delta_HP = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);						
				
				if(delta_HP > index_mapping_HP_NUM-1)					
					index_HP = index_mapping_HP[index_mapping_HP_NUM-1];
				else
					index_HP = index_mapping_HP[delta_HP];
				
				if(ThermalValue > pHalData->EEPROMThermalMeter)	//set larger Tx power
				{
					for(i = 0; i < rf; i++)
					 	OFDM_index[i] = pHalData->OFDM_index_HP[i] - index_HP;
					CCK_index = pHalData->CCK_index_HP -index_HP;						
				}
				else
				{
					for(i = 0; i < rf; i++)
						OFDM_index[i] = pHalData->OFDM_index_HP[i] + index_HP;
					CCK_index = pHalData->CCK_index_HP + index_HP;						
				}	
				
				delta_HP = (ThermalValue > pHalData->ThermalValue)?(ThermalValue - pHalData->ThermalValue):(pHalData->ThermalValue - ThermalValue);
				
			}
			else
#endif				
			{
				if(ThermalValue > pHalData->ThermalValue)
				{ 
					for(i = 0; i < rf; i++)
					 	pHalData->OFDM_index[i] -= delta;
					pHalData->CCK_index -= delta;
				}
				else
				{
					for(i = 0; i < rf; i++)			
						pHalData->OFDM_index[i] += delta;
					pHalData->CCK_index += delta;
				}
			}
			
			if(is2T)
			{
				RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("temp OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
					pHalData->OFDM_index[0], pHalData->OFDM_index[1], pHalData->CCK_index));			
			}
			else
			{
				RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("temp OFDM_A_index=0x%x, CCK_index=0x%x\n", 
					pHalData->OFDM_index[0], pHalData->CCK_index));			
			}

			//no adjust
#if DEV_BUS_TYPE==RT_USB_INTERFACE					
			if(RT_GetInterfaceSelection(Adapter) != INTF_SEL1_USB_High_Power)
#endif				
			{
				if(ThermalValue > pHalData->EEPROMThermalMeter)
				{
					for(i = 0; i < rf; i++)			
						OFDM_index[i] = pHalData->OFDM_index[i]+1;
					CCK_index = pHalData->CCK_index+1;			
				}
				else
				{
					for(i = 0; i < rf; i++)			
						OFDM_index[i] = pHalData->OFDM_index[i];
					CCK_index = pHalData->CCK_index;						
				}

#if MP_DRIVER == 1
				for(i = 0; i < rf; i++)
				{
					if(TxPwrLevel[i] >=0 && TxPwrLevel[i] <=26)
					{
						if(ThermalValue > pHalData->EEPROMThermalMeter)
						{
							if (delta < 5)
								OFDM_index[i] -= 1;					
							else 
								OFDM_index[i] -= 2;					
						}
						else if(delta >= 5 && ThermalValue < pHalData->EEPROMThermalMeter)
						{
							OFDM_index[i] += 2;
						}
                        else if(delta < 5 && ThermalValue < pHalData->EEPROMThermalMeter)
						{
							OFDM_index[i] += 1;
						}
					}
					else if (TxPwrLevel[i] >= 27 && TxPwrLevel[i] <= 32 && ThermalValue > pHalData->EEPROMThermalMeter)
					{
						if (delta < 5)
							OFDM_index[i] -= 1;					
						else 
							OFDM_index[i] -= 2;								
					}
					else if (TxPwrLevel[i] >= 32 && TxPwrLevel[i] <= 38 && ThermalValue > pHalData->EEPROMThermalMeter && delta > 5)
					{
						OFDM_index[i] -= 1;								
					}
				}

				{
					if(TxPwrLevel[i] >=0 && TxPwrLevel[i] <=26)
					{
						if(ThermalValue > pHalData->EEPROMThermalMeter)
						{
							if (delta < 5)
								CCK_index -= 1; 				
							else 
								CCK_index -= 2; 				
						}
						else if(delta > 5 && ThermalValue < pHalData->EEPROMThermalMeter)
						{
							CCK_index += 1;
						}
					}
					else if (TxPwrLevel[i] >= 27 && TxPwrLevel[i] <= 32 && ThermalValue > pHalData->EEPROMThermalMeter)
					{
						if (delta < 5)
							CCK_index -= 1; 				
						else 
							CCK_index -= 2; 							
					}
					else if (TxPwrLevel[i] >= 32 && TxPwrLevel[i] <= 38 && ThermalValue > pHalData->EEPROMThermalMeter && delta > 5)
					{
						CCK_index -= 1; 							
					}
				}
#endif				
			}
			
			for(i = 0; i < rf; i++)
			{
				if(OFDM_index[i] > (OFDM_TABLE_SIZE_92C-1))
					OFDM_index[i] = (OFDM_TABLE_SIZE_92C-1);
				else if (OFDM_index[i] < OFDM_min_index)
					OFDM_index[i] = OFDM_min_index;
			}
						
			if(CCK_index > (CCK_TABLE_SIZE-1))
				CCK_index = (CCK_TABLE_SIZE-1);
			else if (CCK_index < 0)
				CCK_index = 0;		

			if(is2T)
			{
				RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("new OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
					OFDM_index[0], OFDM_index[1], CCK_index));
			}
			else
			{
				RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("new OFDM_A_index=0x%x, CCK_index=0x%x\n", 
					OFDM_index[0], CCK_index));			
			}
		}

		if(pHalData->TxPowerTrackControl && (delta != 0 || delta_HP != 0))
		{
			//Adujst OFDM Ant_A according to IQK result
			ele_D = (OFDMSwingTable[OFDM_index[0]] & 0xFFC00000)>>22;
			X = pHalData->RegE94;
			Y = pHalData->RegE9C;		

			if(X != 0)
			{
				if ((X & 0x00000200) != 0)
					X = X | 0xFFFFFC00;
				ele_A = ((X * ele_D)>>8)&0x000003FF;
					
				//new element C = element D x Y
				if ((Y & 0x00000200) != 0)
					Y = Y | 0xFFFFFC00;
				ele_C = ((Y * ele_D)>>8)&0x000003FF;
				
				//wirte new elements A, C, D to regC80 and regC94, element B is always 0
				value32 = (ele_D<<22)|((ele_C&0x3F)<<16)|ele_A;
				PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, value32);
				
				value32 = (ele_C&0x000003C0)>>6;
				PHY_SetBBReg(Adapter, rOFDM0_XCTxAFE, bMaskH4Bits, value32);

				value32 = ((X * ele_D)>>7)&0x01;
				PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT31, value32);

				value32 = ((Y * ele_D)>>7)&0x01;
				PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT29, value32);
				
			}
			else
			{
				PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable[OFDM_index[0]]);				
				PHY_SetBBReg(Adapter, rOFDM0_XCTxAFE, bMaskH4Bits, 0x00);
				PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT31|BIT29, 0x00);			
			}

			RTPRINT(FINIT, INIT_IQK, ("TxPwrTracking path A: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x\n", X, Y, ele_A, ele_C, ele_D));		

			//Adjust CCK according to IQK result
			if(!pHalData->bCCKinCH14){
				PlatformEFIOWrite1Byte(Adapter, 0xa22, CCKSwingTable_Ch1_Ch13[CCK_index][0]);
				PlatformEFIOWrite1Byte(Adapter, 0xa23, CCKSwingTable_Ch1_Ch13[CCK_index][1]);
				PlatformEFIOWrite1Byte(Adapter, 0xa24, CCKSwingTable_Ch1_Ch13[CCK_index][2]);
				PlatformEFIOWrite1Byte(Adapter, 0xa25, CCKSwingTable_Ch1_Ch13[CCK_index][3]);
				PlatformEFIOWrite1Byte(Adapter, 0xa26, CCKSwingTable_Ch1_Ch13[CCK_index][4]);
				PlatformEFIOWrite1Byte(Adapter, 0xa27, CCKSwingTable_Ch1_Ch13[CCK_index][5]);
				PlatformEFIOWrite1Byte(Adapter, 0xa28, CCKSwingTable_Ch1_Ch13[CCK_index][6]);
				PlatformEFIOWrite1Byte(Adapter, 0xa29, CCKSwingTable_Ch1_Ch13[CCK_index][7]);		
			}
			else{
				PlatformEFIOWrite1Byte(Adapter, 0xa22, CCKSwingTable_Ch14[CCK_index][0]);
				PlatformEFIOWrite1Byte(Adapter, 0xa23, CCKSwingTable_Ch14[CCK_index][1]);
				PlatformEFIOWrite1Byte(Adapter, 0xa24, CCKSwingTable_Ch14[CCK_index][2]);
				PlatformEFIOWrite1Byte(Adapter, 0xa25, CCKSwingTable_Ch14[CCK_index][3]);
				PlatformEFIOWrite1Byte(Adapter, 0xa26, CCKSwingTable_Ch14[CCK_index][4]);
				PlatformEFIOWrite1Byte(Adapter, 0xa27, CCKSwingTable_Ch14[CCK_index][5]);
				PlatformEFIOWrite1Byte(Adapter, 0xa28, CCKSwingTable_Ch14[CCK_index][6]);
				PlatformEFIOWrite1Byte(Adapter, 0xa29, CCKSwingTable_Ch14[CCK_index][7]);	
			}		

			if(is2T)
			{						
				ele_D = (OFDMSwingTable[OFDM_index[1]] & 0xFFC00000)>>22;
				
				//new element A = element D x X
				X = pHalData->RegEB4;
				Y = pHalData->RegEBC;
				
				if(X != 0){
					if ((X & 0x00000200) != 0)	//consider minus
						X = X | 0xFFFFFC00;
					ele_A = ((X * ele_D)>>8)&0x000003FF;
					
					//new element C = element D x Y
					if ((Y & 0x00000200) != 0)
						Y = Y | 0xFFFFFC00;
					ele_C = ((Y * ele_D)>>8)&0x00003FF;
					
					//wirte new elements A, C, D to regC88 and regC9C, element B is always 0
					value32=(ele_D<<22)|((ele_C&0x3F)<<16) |ele_A;
					PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, value32);

					value32 = (ele_C&0x000003C0)>>6;
					PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, value32);	
					
					value32 = ((X * ele_D)>>7)&0x01;
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT27, value32);

					value32 = ((Y * ele_D)>>7)&0x01;
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT25, value32);

				}
				else{
					PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, OFDMSwingTable[OFDM_index[1]]);										
					PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, 0x00);	
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT27|BIT25, 0x00);				
				}

				RTPRINT(FINIT, INIT_IQK, ("TxPwrTracking path B: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x\n", X, Y, ele_A, ele_C, ele_D));			
			}

			RTPRINT(FINIT, INIT_IQK, ("TxPwrTracking 0xc80 = 0x%x, 0xc94 = 0x%x RF 0x24 = 0x%x\n", PHY_QueryBBReg(Adapter, 0xc80, bMaskDWord), PHY_QueryBBReg(Adapter, 0xc94, bMaskDWord), PHY_QueryRFReg(Adapter, RF_PATH_A, 0x24, bRFRegOffsetMask)));
		}

#if MP_DRIVER == 1
		if(delta_IQK > 1)
#else
		if(delta_IQK > 3)
#endif			
		{
			pHalData->ThermalValue_IQK = ThermalValue;
			PHY_IQCalibrate(Adapter, FALSE);
		}

#if 1
		if(delta > 0)
		{
			if(ThermalValue >= 15)
				PHY_SetBBReg(Adapter, REG_AFE_XTAL_CTRL, bMaskDWord, 0x038180fd );
			else
				PHY_SetBBReg(Adapter, REG_AFE_XTAL_CTRL, bMaskDWord, 0x0381808d );				
		}
#endif
		//update thermal meter value
		if(pHalData->TxPowerTrackControl)
			Adapter->HalFunc.SetHalDefVarHandler(Adapter, HAL_DEF_THERMAL_VALUE, &ThermalValue);
			
	}

	PlatformAtomicExchange(&Adapter->IntrCCKRefCount, FALSE);		
	pHalData->TXPowercount = 0;

	// 2011/08/23 MH Add for power tracking after S3/S4  turn off RF. In this case, we need to execute IQK again. Otherwise
	// The IQK scheme will use old value to save and cause incorrect BB value.
	{
		RT_RF_POWER_STATE 	rtState;

		Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_RF_STATE, (pu1Byte)(&rtState));	
		
		if(Adapter->bDriverStopped || Adapter->bDriverIsGoingToPnpSetPowerSleep || rtState == eRfOff)
		{
			RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Incorrect pwrtrack point, re-iqk next time\n"));	
			pHalData->bIQKInitialized = FALSE;
		}
	}
	
	RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("<===odm_TXPowerTrackingCallback_ThermalMeter_92C\n"));	
#endif
}

//
// ==================================================
// Tx power tracking relative code.
// ==================================================
//


#endif

#else //#if (RTL8192C_SUPPORT == 1)
VOID
odm_TXPowerTrackingCallback_ThermalMeter_92C(
	IN PADAPTER	Adapter
	)
{
}
VOID
odm_TXPowerTrackingCallback_ThermalMeter_8723A(
            IN PADAPTER	Adapter)
{
}

#endif //#if (RTL8192C_SUPPORT == 1)


