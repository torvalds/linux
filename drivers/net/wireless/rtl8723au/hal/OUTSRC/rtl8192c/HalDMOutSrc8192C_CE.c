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
// Description:
//
// This file is for 92CE/92CU dynamic mechanism only
//
//
//============================================================

//============================================================
// include files
//============================================================

#include "../odm_precomp.h"

#define		DPK_DELTA_MAPPING_NUM	13
#define		index_mapping_HP_NUM	15
//091212 chiyokolin
static	VOID
odm_TXPowerTrackingCallback_ThermalMeter_92C(
            IN PADAPTER	Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8			ThermalValue = 0, delta, delta_LCK, delta_IQK, delta_HP, TimeOut = 100;
	int 			ele_A, ele_D, TempCCk, X, value32;
	int			Y, ele_C;
	s8			OFDM_index[2], CCK_index = 0, OFDM_index_old[2], CCK_index_old = 0;
	int			i = 0;
	BOOLEAN		is2T = IS_92C_SERIAL(pHalData->VersionID);

#if MP_DRIVER == 1
	PMPT_CONTEXT	pMptCtx = &(Adapter->mppriv.MptCtx);	
	u8			*TxPwrLevel = pMptCtx->TxPwrLevel;
#endif
	u8			OFDM_min_index = 6, rf; //OFDM BB Swing should be less than +3.0dB, which is required by Arthur
#if 0
	u32			DPK_delta_mapping[2][DPK_DELTA_MAPPING_NUM] = {
					{0x1c, 0x1c, 0x1d, 0x1d, 0x1e, 
					 0x1f, 0x00, 0x00, 0x01, 0x01,
					 0x02, 0x02, 0x03},
					{0x1c, 0x1d, 0x1e, 0x1e, 0x1e,
					 0x1f, 0x00, 0x00, 0x01, 0x02,
					 0x02, 0x03, 0x03}};
#endif
#ifdef CONFIG_USB_HCI
	u8			ThermalValue_HP_count = 0;
	u32			ThermalValue_HP = 0;
	s32			index_mapping_HP[index_mapping_HP_NUM] = {
					0,	1,	3,	4,	6,	
					7,	9,	10,	12,	13,	
					15,	16,	18,	19,	21
					};

	s8			index_HP;
#endif

	pdmpriv->TXPowerTrackingCallbackCnt++;	//cosa add for debug
	pdmpriv->bTXPowerTrackingInit = _TRUE;

	if(pHalData->CurrentChannel == 14 && !pdmpriv->bCCKinCH14)
		pdmpriv->bCCKinCH14 = _TRUE;
	else if(pHalData->CurrentChannel != 14 && pdmpriv->bCCKinCH14)
		pdmpriv->bCCKinCH14 = _FALSE;

	//DBG_8192C("===>dm_TXPowerTrackingCallback_ThermalMeter_92C\n");

	ThermalValue = (u8)PHY_QueryRFReg(Adapter, RF_PATH_A, RF_T_METER, 0x1f);	// 0x24: RF Reg[4:0]	

	//DBG_8192C("\n\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n",ThermalValue,pdmpriv->ThermalValue,  pHalData->EEPROMThermalMeter);

	rtl8192c_PHY_APCalibrate(Adapter, (ThermalValue - pHalData->EEPROMThermalMeter));

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
			for(i=0; i<OFDM_TABLE_SIZE_92C; i++)	//find the index
			{
				if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
				{
					OFDM_index_old[0] = (u8)i;
					//DBG_8192C("Initial pathA ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n", rOFDM0_XATxIQImbalance, ele_D, OFDM_index_old[0]);
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
						OFDM_index_old[1] = (u8)i;
						//DBG_8192C("Initial pathB ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n",rOFDM0_XBTxIQImbalance, ele_D, OFDM_index_old[1]);
						break;
					}
				}
			}

			//Query CCK default setting From 0xa24
			TempCCk = PHY_QueryBBReg(Adapter, rCCK0_TxFilter2, bMaskDWord)&bMaskCCK;
			for(i=0 ; i<CCK_TABLE_SIZE ; i++)
			{
				if(pdmpriv->bCCKinCH14)
				{
					if(_rtw_memcmp((void*)&TempCCk, (void*)&CCKSwingTable_Ch14[i][2], 4)==_TRUE)
					{
						CCK_index_old =(u8)i;
						//DBG_8192C("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch 14 %d\n", rCCK0_TxFilter2, TempCCk, CCK_index_old, pdmpriv->bCCKinCH14);
						break;
					}
				}
				else
				{
					if(_rtw_memcmp((void*)&TempCCk, (void*)&CCKSwingTable_Ch1_Ch13[i][2], 4)==_TRUE)
					{
						CCK_index_old =(u8)i;
						//DBG_8192C("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch14 %d\n", rCCK0_TxFilter2, TempCCk, CCK_index_old, pdmpriv->bCCKinCH14);
						break;
					}			
				}
			}	

			if(!pdmpriv->ThermalValue)
			{
				pdmpriv->ThermalValue = pHalData->EEPROMThermalMeter;
				pdmpriv->ThermalValue_LCK = ThermalValue;
				pdmpriv->ThermalValue_IQK = ThermalValue;
				pdmpriv->ThermalValue_DPK = pHalData->EEPROMThermalMeter;

#ifdef CONFIG_USB_HCI
				for(i = 0; i < rf; i++)
					pdmpriv->OFDM_index_HP[i] = pdmpriv->OFDM_index[i] = OFDM_index_old[i];
				pdmpriv->CCK_index_HP = pdmpriv->CCK_index = CCK_index_old;
#else
				for(i = 0; i < rf; i++)
					pdmpriv->OFDM_index[i] = OFDM_index_old[i];
				pdmpriv->CCK_index = CCK_index_old;
#endif
			}

#ifdef CONFIG_USB_HCI
			if(pHalData->BoardType == BOARD_USB_High_PA)
			{
				pdmpriv->ThermalValue_HP[pdmpriv->ThermalValue_HP_index] = ThermalValue;
				pdmpriv->ThermalValue_HP_index++;
				if(pdmpriv->ThermalValue_HP_index == HP_THERMAL_NUM)
					pdmpriv->ThermalValue_HP_index = 0;

				for(i = 0; i < HP_THERMAL_NUM; i++)
				{
					if(pdmpriv->ThermalValue_HP[i])
					{
						ThermalValue_HP += pdmpriv->ThermalValue_HP[i];
						ThermalValue_HP_count++;
					}			
				}
		
				if(ThermalValue_HP_count)
					ThermalValue = (u8)(ThermalValue_HP / ThermalValue_HP_count);
			}
#endif
		}

		delta = (ThermalValue > pdmpriv->ThermalValue)?(ThermalValue - pdmpriv->ThermalValue):(pdmpriv->ThermalValue - ThermalValue);
#ifdef CONFIG_USB_HCI
		if(pHalData->BoardType == BOARD_USB_High_PA)
		{
			if(pdmpriv->bDoneTxpower)
				delta_HP = (ThermalValue > pdmpriv->ThermalValue)?(ThermalValue - pdmpriv->ThermalValue):(pdmpriv->ThermalValue - ThermalValue);
			else
				delta_HP = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);						
		}
		else
#endif	
		{
			delta_HP = 0;			
		}
		delta_LCK = (ThermalValue > pdmpriv->ThermalValue_LCK)?(ThermalValue - pdmpriv->ThermalValue_LCK):(pdmpriv->ThermalValue_LCK - ThermalValue);
		delta_IQK = (ThermalValue > pdmpriv->ThermalValue_IQK)?(ThermalValue - pdmpriv->ThermalValue_IQK):(pdmpriv->ThermalValue_IQK - ThermalValue);

		//DBG_8192C("Readback Thermal Meter = 0x%lx pre thermal meter 0x%lx EEPROMthermalmeter 0x%lx delta 0x%lx delta_LCK 0x%lx delta_IQK 0x%lx\n", ThermalValue, pHalData->ThermalValue, pHalData->EEPROMThermalMeter, delta, delta_LCK, delta_IQK);

		if(delta_LCK > 1)
		{
			pdmpriv->ThermalValue_LCK = ThermalValue;
			rtl8192c_PHY_LCCalibrate(Adapter);
		}
		
		if((delta > 0 || delta_HP > 0) && pdmpriv->TxPowerTrackControl)
		{
#ifdef CONFIG_USB_HCI
			if(pHalData->BoardType == BOARD_USB_High_PA)
			{
				pdmpriv->bDoneTxpower = _TRUE;
				delta_HP = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);						
				
				if(delta_HP > index_mapping_HP_NUM-1)					
					index_HP = index_mapping_HP[index_mapping_HP_NUM-1];
				else
					index_HP = index_mapping_HP[delta_HP];
				
				if(ThermalValue > pHalData->EEPROMThermalMeter)	//set larger Tx power
				{
					for(i = 0; i < rf; i++)
					 	OFDM_index[i] = pdmpriv->OFDM_index_HP[i] - index_HP;
					CCK_index = pdmpriv->CCK_index_HP -index_HP;						
				}
				else
				{
					for(i = 0; i < rf; i++)
						OFDM_index[i] = pdmpriv->OFDM_index_HP[i] + index_HP;
					CCK_index = pdmpriv->CCK_index_HP + index_HP;						
				}	
				
				delta_HP = (ThermalValue > pdmpriv->ThermalValue)?(ThermalValue - pdmpriv->ThermalValue):(pdmpriv->ThermalValue - ThermalValue);
				
			}
			else
#endif
			{
				if(ThermalValue > pdmpriv->ThermalValue)
				{ 
					for(i = 0; i < rf; i++)
					 	pdmpriv->OFDM_index[i] -= delta;
					pdmpriv->CCK_index -= delta;
				}
				else
				{
					for(i = 0; i < rf; i++)			
						pdmpriv->OFDM_index[i] += delta;
					pdmpriv->CCK_index += delta;
				}
			}

			/*if(is2T)
			{
				DBG_8192C("temp OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
					pdmpriv->OFDM_index[0], pdmpriv->OFDM_index[1], pdmpriv->CCK_index);
			}
			else
			{
				DBG_8192C("temp OFDM_A_index=0x%x, CCK_index=0x%x\n",
					pdmpriv->OFDM_index[0], pdmpriv->CCK_index);
			}*/

			//no adjust
#ifdef CONFIG_USB_HCI
			if(pHalData->BoardType != BOARD_USB_High_PA)
#endif
			{
				if(ThermalValue > pHalData->EEPROMThermalMeter)
				{
					for(i = 0; i < rf; i++)			
						OFDM_index[i] = pdmpriv->OFDM_index[i]+1;
					CCK_index = pdmpriv->CCK_index+1;			
				}
				else
				{
					for(i = 0; i < rf; i++)			
						OFDM_index[i] = pdmpriv->OFDM_index[i];
					CCK_index = pdmpriv->CCK_index;						
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

			/*if(is2T)
			{
				DBG_8192C("new OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
					OFDM_index[0], OFDM_index[1], CCK_index);
			}
			else
			{
				DBG_8192C("new OFDM_A_index=0x%x, CCK_index=0x%x\n", 
					OFDM_index[0], CCK_index);
			}*/
		}

		if(pdmpriv->TxPowerTrackControl && (delta != 0 || delta_HP != 0))
		{
			//Adujst OFDM Ant_A according to IQK result
			ele_D = (OFDMSwingTable[OFDM_index[0]] & 0xFFC00000)>>22;
			X = pdmpriv->RegE94;
			Y = pdmpriv->RegE9C;		

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

			//RTPRINT(FINIT, INIT_IQK, ("TxPwrTracking path A: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x\n", X, Y, ele_A, ele_C, ele_D));		

			//Adjust CCK according to IQK result
			if(!pdmpriv->bCCKinCH14){
				rtw_write8(Adapter, 0xa22, CCKSwingTable_Ch1_Ch13[CCK_index][0]);
				rtw_write8(Adapter, 0xa23, CCKSwingTable_Ch1_Ch13[CCK_index][1]);
				rtw_write8(Adapter, 0xa24, CCKSwingTable_Ch1_Ch13[CCK_index][2]);
				rtw_write8(Adapter, 0xa25, CCKSwingTable_Ch1_Ch13[CCK_index][3]);
				rtw_write8(Adapter, 0xa26, CCKSwingTable_Ch1_Ch13[CCK_index][4]);
				rtw_write8(Adapter, 0xa27, CCKSwingTable_Ch1_Ch13[CCK_index][5]);
				rtw_write8(Adapter, 0xa28, CCKSwingTable_Ch1_Ch13[CCK_index][6]);
				rtw_write8(Adapter, 0xa29, CCKSwingTable_Ch1_Ch13[CCK_index][7]);
			}
			else{
				rtw_write8(Adapter, 0xa22, CCKSwingTable_Ch14[CCK_index][0]);
				rtw_write8(Adapter, 0xa23, CCKSwingTable_Ch14[CCK_index][1]);
				rtw_write8(Adapter, 0xa24, CCKSwingTable_Ch14[CCK_index][2]);
				rtw_write8(Adapter, 0xa25, CCKSwingTable_Ch14[CCK_index][3]);
				rtw_write8(Adapter, 0xa26, CCKSwingTable_Ch14[CCK_index][4]);
				rtw_write8(Adapter, 0xa27, CCKSwingTable_Ch14[CCK_index][5]);
				rtw_write8(Adapter, 0xa28, CCKSwingTable_Ch14[CCK_index][6]);
				rtw_write8(Adapter, 0xa29, CCKSwingTable_Ch14[CCK_index][7]);	
			}		

			if(is2T)
			{						
				ele_D = (OFDMSwingTable[(u8)OFDM_index[1]] & 0xFFC00000)>>22;
				
				//new element A = element D x X
				X = pdmpriv->RegEB4;
				Y = pdmpriv->RegEBC;
				
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

				//DBG_8192C("TxPwrTracking path B: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x\n", X, Y, ele_A, ele_C, ele_D);
			}

			/*
			DBG_8192C("TxPwrTracking 0xc80 = 0x%x, 0xc94 = 0x%x RF 0x24 = 0x%x\n", \
					PHY_QueryBBReg(Adapter, 0xc80, bMaskDWord),\
					PHY_QueryBBReg(Adapter, 0xc94, bMaskDWord), \
					PHY_QueryRFReg(Adapter, RF_PATH_A, 0x24, bMaskDWord));
			*/
		}

#if MP_DRIVER == 1
		if(delta_IQK > 1)
#else
		if(delta_IQK > 3)
#endif
		{
			pdmpriv->ThermalValue_IQK = ThermalValue;
			rtl8192c_PHY_IQCalibrate(Adapter,_FALSE);
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
		if(pdmpriv->TxPowerTrackControl)
			pdmpriv->ThermalValue = ThermalValue;

	}

	//DBG_8192C("<===dm_TXPowerTrackingCallback_ThermalMeter_92C\n");
	
	pdmpriv->TXPowercount = 0;

}

/*
static	VOID
odm_InitializeTXPowerTracking_ThermalMeter(
	IN	PADAPTER		Adapter)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	//pMgntInfo->bTXPowerTracking = _TRUE;
	pdmpriv->TXPowercount = 0;
	pdmpriv->bTXPowerTrackingInit = _FALSE;
	pdmpriv->ThermalValue = 0;
	
#if	(MP_DRIVER != 1)	//for mp driver, turn off txpwrtracking as default
	pdmpriv->TxPowerTrackControl = _TRUE;
#endif
	
	MSG_8192C("pdmpriv->TxPowerTrackControl = %d\n", pdmpriv->TxPowerTrackControl);
}


static VOID
ODM_InitializeTXPowerTracking(
	IN	PADAPTER		Adapter)
{
	odm_InitializeTXPowerTracking_ThermalMeter(Adapter);	
}	
*/
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
static VOID
ODM_TXPowerTracking92CDirectCall(
            IN	PADAPTER		Adapter)
{	
	odm_TXPowerTrackingCallback_ThermalMeter_92C(Adapter);
}

static VOID
odm_CheckTXPowerTracking_ThermalMeter(
	IN	PADAPTER		Adapter)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DM_ODM_T 		*podmpriv = &pHalData->odmpriv;
	//u1Byte					TxPowerCheckCnt = 5;	//10 sec
	//if(!pMgntInfo->bTXPowerTracking /*|| (!pdmpriv->TxPowerTrackControl && pdmpriv->bAPKdone)*/)
	if(!(podmpriv->SupportAbility & ODM_RF_TX_PWR_TRACK))
	{
		return;
	}

	if(!pdmpriv->TM_Trigger)		//at least delay 1 sec
	{
		//pHalData->TxPowerCheckCnt++;	//cosa add for debug
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_T_METER, bRFRegOffsetMask, 0x60);
		//DBG_8192C("Trigger 92C Thermal Meter!!\n");
		
		pdmpriv->TM_Trigger = 1;
		return;
		
	}
	else
	{
		//DBG_8192C("Schedule TxPowerTracking direct call!!\n");
		ODM_TXPowerTracking92CDirectCall(Adapter); //Using direct call is instead, added by Roger, 2009.06.18.
		pdmpriv->TM_Trigger = 0;
	}

}


VOID
rtl8192c_odm_CheckTXPowerTracking(
	IN	PADAPTER		Adapter)
{
	odm_CheckTXPowerTracking_ThermalMeter(Adapter);
}



#ifdef CONFIG_ANTENNA_DIVERSITY
// Add new function to reset the state of antenna diversity before link.
//
void odm_SwAntDivResetBeforeLink8192C(IN PDM_ODM_T pDM_Odm)
{
	SWAT_T *pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	
	pDM_SWAT_Table->SWAS_NoLink_State = 0;
}

// Compare RSSI for deciding antenna
void	odm_AntDivCompare8192C(PADAPTER Adapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src)
{
	//PADAPTER Adapter = pDM_Odm->Adapter ;
	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T pDM_Odm = &pHalData->odmpriv;
	if((0 != pHalData->AntDivCfg) && (!IS_92C_SERIAL(pHalData->VersionID)) )
	{
		//DBG_8192C("update_network=> orgRSSI(%d)(%d),newRSSI(%d)(%d)\n",dst->Rssi,query_rx_pwr_percentage(dst->Rssi),
		//	src->Rssi,query_rx_pwr_percentage(src->Rssi));
		//select optimum_antenna for before linked =>For antenna diversity
		if(dst->Rssi >=  src->Rssi )//keep org parameter
		{
			src->Rssi = dst->Rssi;
			src->PhyInfo.Optimum_antenna = dst->PhyInfo.Optimum_antenna;						
		}
	}
}

// Add new function to reset the state of antenna diversity before link.
u8 odm_AntDivBeforeLink8192C(PADAPTER Adapter )
{
	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	
	PDM_ODM_T 	pDM_Odm =&pHalData->odmpriv;
	SWAT_T		*pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	
	// Condition that does not need to use antenna diversity.
	if(IS_92C_SERIAL(pHalData->VersionID) ||(pHalData->AntDivCfg==0))
	{
		//DBG_8192C("odm_AntDivBeforeLink8192C(): No AntDiv Mechanism.\n");
		return _FALSE;
	}

	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)	
	{
		pDM_SWAT_Table->SWAS_NoLink_State = 0;
		return _FALSE;
	}
	// Since driver is going to set BB register, it shall check if there is another thread controlling BB/RF.
/*	
	if(pHalData->eRFPowerState!=eRfOn || pMgntInfo->RFChangeInProgress || pMgntInfo->bMediaConnect)
	{
	
	
		ODM_RT_TRACE(pDM_Odm,COMP_SWAS, DBG_LOUD, 
				("SwAntDivCheckBeforeLink8192C(): RFChangeInProgress(%x), eRFPowerState(%x)\n", 
				pMgntInfo->RFChangeInProgress,
				pHalData->eRFPowerState));
	
		pDM_SWAT_Table->SWAS_NoLink_State = 0;
		
		return FALSE;
	}
*/	
	
	if(pDM_SWAT_Table->SWAS_NoLink_State == 0){
		//switch channel
		pDM_SWAT_Table->SWAS_NoLink_State = 1;
		pDM_SWAT_Table->CurAntenna = (pDM_SWAT_Table->CurAntenna==Antenna_A)?Antenna_B:Antenna_A;

		//PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300, pDM_SWAT_Table->CurAntenna);
		rtw_antenna_select_cmd(Adapter, pDM_SWAT_Table->CurAntenna, _FALSE);
		//DBG_8192C("%s change antenna to ANT_( %s ).....\n",__FUNCTION__, (pDM_SWAT_Table->CurAntenna==Antenna_A)?"A":"B");
		return _TRUE;
	}
	else
	{
		pDM_SWAT_Table->SWAS_NoLink_State = 0;
		return _FALSE;
	}
		


}
#endif

//-------------------------------------------------------------------------
//
//	IQK
//
//-------------------------------------------------------------------------
#define MAX_TOLERANCE		5
#define IQK_DELAY_TIME		1 	//ms

static u8			//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
_PHY_PathA_IQK(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		configPathB
	)
{
	u32 regEAC, regE94, regE9C, regEA4;
	u8 result = 0x00;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	//RTPRINT(FINIT, INIT_IQK, ("Path A IQK!\n"));

	//path-A IQK setting
	//RTPRINT(FINIT, INIT_IQK, ("Path-A IQK setting!\n"));
	PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
	PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
	PHY_SetBBReg(pAdapter, rTx_IQK_PI_A, bMaskDWord, 0x82140102);

	PHY_SetBBReg(pAdapter, rRx_IQK_PI_A, bMaskDWord, configPathB ? 0x28160202 : 
		IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID)?0x28160202:0x28160502);

	//path-B IQK setting
	if(configPathB)
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(pAdapter, rTx_IQK_PI_B, bMaskDWord, 0x82140102);
		PHY_SetBBReg(pAdapter, rRx_IQK_PI_B, bMaskDWord, 0x28160202);
	}

	//LO calibration setting
	//RTPRINT(FINIT, INIT_IQK, ("LO calibration setting!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Rsp, bMaskDWord, 0x001028d1);

	//One shot, path A LOK & IQK
	//RTPRINT(FINIT, INIT_IQK, ("One shot, path A LOK & IQK!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	// delay x ms
	//RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME));
	rtw_udelay_os(IQK_DELAY_TIME*1000);//PlatformStallExecution(IQK_DELAY_TIME*1000);

	// Check failed
	regEAC = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xeac = 0x%x\n", regEAC));
	regE94 = PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xe94 = 0x%x\n", regE94));
	regE9C= PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xe9c = 0x%x\n", regE9C));
	regEA4= PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xea4 = 0x%x\n", regEA4));

        if(!(regEAC & BIT28) &&		
		(((regE94 & 0x03FF0000)>>16) != 0x142) &&
		(((regE9C & 0x03FF0000)>>16) != 0x42) )
		result |= 0x01;
	else							//if Tx not OK, ignore Rx
		return result;

	if(!(regEAC & BIT27) &&		//if Tx is OK, check whether Rx is OK
		(((regEA4 & 0x03FF0000)>>16) != 0x132) &&
		(((regEAC & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		DBG_8192C("Path A Rx IQK fail!!\n");
	
	return result;


}

static u8				//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
_PHY_PathB_IQK(
	IN	PADAPTER	pAdapter
	)
{
	u32 regEAC, regEB4, regEBC, regEC4, regECC;
	u8	result = 0x00;
	//RTPRINT(FINIT, INIT_IQK, ("Path B IQK!\n"));

	//One shot, path B LOK & IQK
	//RTPRINT(FINIT, INIT_IQK, ("One shot, path A LOK & IQK!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
	PHY_SetBBReg(pAdapter, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

	// delay x ms
	//RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for One shot, path B LOK & IQK.\n", IQK_DELAY_TIME));
	rtw_udelay_os(IQK_DELAY_TIME*1000);//PlatformStallExecution(IQK_DELAY_TIME*1000);

	// Check failed
	regEAC = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xeac = 0x%x\n", regEAC));
	regEB4 = PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xeb4 = 0x%x\n", regEB4));
	regEBC= PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xebc = 0x%x\n", regEBC));
	regEC4= PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xec4 = 0x%x\n", regEC4));
	regECC= PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_B_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xecc = 0x%x\n", regECC));

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
		DBG_8192C("Path B Rx IQK fail!!\n");
	

	return result;

}

static VOID
_PHY_PathAFillIQKMatrix(
	IN	PADAPTER	pAdapter,
	IN 	BOOLEAN    	bIQKOK,
	IN	int			result[][8],
	IN	u8			final_candidate,
	IN 	BOOLEAN		bTxOnly
	)
{
	u32	Oldval_0, X, TX0_A, reg;
	s32	Y, TX0_C;

	DBG_8192C("Path A IQ Calibration %s !\n",(bIQKOK)?"Success":"Failed");

	if(final_candidate == 0xFF)
		return;
	else if(bIQKOK)
	{
		Oldval_0 = (PHY_QueryBBReg(pAdapter, rOFDM0_XATxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX0_A = (X * Oldval_0) >> 8;
		//RTPRINT(FINIT, INIT_IQK, ("X = 0x%lx, TX0_A = 0x%lx, Oldval_0 0x%lx\n", X, TX0_A, Oldval_0));
		PHY_SetBBReg(pAdapter, rOFDM0_XATxIQImbalance, 0x3FF, TX0_A);
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(31), ((X* Oldval_0>>7) & 0x1));

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;
		TX0_C = (Y * Oldval_0) >> 8;
		//RTPRINT(FINIT, INIT_IQK, ("Y = 0x%lx, TX = 0x%lx\n", Y, TX0_C));
		PHY_SetBBReg(pAdapter, rOFDM0_XCTxAFE, 0xF0000000, ((TX0_C&0x3C0)>>6));
		PHY_SetBBReg(pAdapter, rOFDM0_XATxIQImbalance, 0x003F0000, (TX0_C&0x3F));
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(29), ((Y* Oldval_0>>7) & 0x1));

	        if(bTxOnly)
		{
			DBG_8192C("_PHY_PathAFillIQKMatrix only Tx OK\n");
			return;
		}

		reg = result[final_candidate][2];
		PHY_SetBBReg(pAdapter, rOFDM0_XARxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][3] & 0x3F;
		PHY_SetBBReg(pAdapter, rOFDM0_XARxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		PHY_SetBBReg(pAdapter, rOFDM0_RxIQExtAnta, 0xF0000000, reg);
	}
}

static VOID
_PHY_PathBFillIQKMatrix(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN   	bIQKOK,
	IN	int			result[][8],
	IN	u8			final_candidate,
	IN	BOOLEAN		bTxOnly			//do Tx only
	)
{
	u32	Oldval_1, X, TX1_A, reg;
	s32	Y, TX1_C;
	
	DBG_8192C("Path B IQ Calibration %s !\n",(bIQKOK)?"Success":"Failed");

	if(final_candidate == 0xFF)
		return;
	else if(bIQKOK)
	{
		Oldval_1 = (PHY_QueryBBReg(pAdapter, rOFDM0_XBTxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;		
		TX1_A = (X * Oldval_1) >> 8;
		//RTPRINT(FINIT, INIT_IQK, ("X = 0x%lx, TX1_A = 0x%lx\n", X, TX1_A));
		PHY_SetBBReg(pAdapter, rOFDM0_XBTxIQImbalance, 0x3FF, TX1_A);
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(27), ((X* Oldval_1>>7) & 0x1));

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;		
		TX1_C = (Y * Oldval_1) >> 8;
		//RTPRINT(FINIT, INIT_IQK, ("Y = 0x%lx, TX1_C = 0x%lx\n", Y, TX1_C));
		PHY_SetBBReg(pAdapter, rOFDM0_XDTxAFE, 0xF0000000, ((TX1_C&0x3C0)>>6));
		PHY_SetBBReg(pAdapter, rOFDM0_XBTxIQImbalance, 0x003F0000, (TX1_C&0x3F));
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(25), ((Y* Oldval_1>>7) & 0x1));

		if(bTxOnly)
			return;

		reg = result[final_candidate][6];
		PHY_SetBBReg(pAdapter, rOFDM0_XBRxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][7] & 0x3F;
		PHY_SetBBReg(pAdapter, rOFDM0_XBRxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		PHY_SetBBReg(pAdapter, rOFDM0_AGCRSSITable, 0x0000F000, reg);
	}
}

static VOID
_PHY_SaveADDARegisters(
	IN	PADAPTER	pAdapter,
	IN	u32*		ADDAReg,
	IN	u32*		ADDABackup,
	IN	u32			RegisterNum
	)
{
	u32	i;
	
	//RTPRINT(FINIT, INIT_IQK, ("Save ADDA parameters.\n"));
	for( i = 0 ; i < RegisterNum ; i++){
		ADDABackup[i] = PHY_QueryBBReg(pAdapter, ADDAReg[i], bMaskDWord);
	}
}

static VOID
_PHY_SaveMACRegisters(
	IN	PADAPTER	pAdapter,
	IN	u32*		MACReg,
	IN	u32*		MACBackup
	)
{
	u32	i;
	
	//RTPRINT(FINIT, INIT_IQK, ("Save MAC parameters.\n"));
	for( i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++){
		MACBackup[i] =rtw_read8(pAdapter, MACReg[i]);		
	}
	MACBackup[i] = rtw_read32(pAdapter, MACReg[i]);		

}

static VOID
_PHY_ReloadADDARegisters(
	IN	PADAPTER	pAdapter,
	IN	u32*		ADDAReg,
	IN	u32*		ADDABackup,
	IN	u32			RegiesterNum
	)
{
	u32	i;

	//RTPRINT(FINIT, INIT_IQK, ("Reload ADDA power saving parameters !\n"));
	for(i = 0 ; i < RegiesterNum ; i++){
		PHY_SetBBReg(pAdapter, ADDAReg[i], bMaskDWord, ADDABackup[i]);
	}
}

static VOID
_PHY_ReloadMACRegisters(
	IN	PADAPTER	pAdapter,
	IN	u32*		MACReg,
	IN	u32*		MACBackup
	)
{
	u32	i;

	//RTPRINT(FINIT, INIT_IQK, ("Reload MAC parameters !\n"));
	for(i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++){
		rtw_write8(pAdapter, MACReg[i], (u8)MACBackup[i]);
	}
	rtw_write32(pAdapter, MACReg[i], MACBackup[i]);	
}

static VOID
_PHY_PathADDAOn(
	IN	PADAPTER	pAdapter,
	IN	u32*		ADDAReg,
	IN	BOOLEAN		isPathAOn,
	IN	BOOLEAN		is2T
	)
{
	u32	pathOn;
	u32	i;

	//RTPRINT(FINIT, INIT_IQK, ("ADDA ON.\n"));

	pathOn = isPathAOn ? 0x04db25a4 : 0x0b1b25a4;
	if(_FALSE == is2T){
		pathOn = 0x0bdb25a0;
		PHY_SetBBReg(pAdapter, ADDAReg[0], bMaskDWord, 0x0b1b25a0);
	}
	else{
		PHY_SetBBReg(pAdapter, ADDAReg[0], bMaskDWord, pathOn);
	}
	
	for( i = 1 ; i < IQK_ADDA_REG_NUM ; i++){
		PHY_SetBBReg(pAdapter, ADDAReg[i], bMaskDWord, pathOn);
	}
	
}

static VOID
_PHY_MACSettingCalibration(
	IN	PADAPTER	pAdapter,
	IN	u32*		MACReg,
	IN	u32*		MACBackup	
	)
{
	u32	i = 0;

	//RTPRINT(FINIT, INIT_IQK, ("MAC settings for Calibration.\n"));

	rtw_write8(pAdapter, MACReg[i], 0x3F);

	for(i = 1 ; i < (IQK_MAC_REG_NUM - 1); i++){
		rtw_write8(pAdapter, MACReg[i], (u8)(MACBackup[i]&(~BIT3)));
	}
	rtw_write8(pAdapter, MACReg[i], (u8)(MACBackup[i]&(~BIT5)));	

}

static VOID
_PHY_PathAStandBy(
	IN	PADAPTER	pAdapter
	)
{
	//RTPRINT(FINIT, INIT_IQK, ("Path-A standby mode!\n"));

	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x0);
	PHY_SetBBReg(pAdapter, 0x840, bMaskDWord, 0x00010000);
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
}

static VOID
_PHY_PIModeSwitch(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		PIMode
	)
{
	u32	mode;

	//RTPRINT(FINIT, INIT_IQK, ("BB Switch to %s mode!\n", (PIMode ? "PI" : "SI")));

	mode = PIMode ? 0x01000100 : 0x01000000;
	PHY_SetBBReg(pAdapter, 0x820, bMaskDWord, mode);
	PHY_SetBBReg(pAdapter, 0x828, bMaskDWord, mode);
}

/*
return _FALSE => do IQK again
*/
static BOOLEAN							
_PHY_SimularityCompare(
	IN	PADAPTER	pAdapter,
	IN	int 		result[][8],
	IN	u8		 c1,
	IN	u8		 c2
	)
{
	u32		i, j, diff, SimularityBitMap, bound = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	
	u8		final_candidate[2] = {0xFF, 0xFF};	//for path A and path B
	BOOLEAN		bResult = _TRUE, is2T = IS_92C_SERIAL( pHalData->VersionID);
	
	if(is2T)
		bound = 8;
	else
		bound = 4;

	SimularityBitMap = 0;
	
	for( i = 0; i < bound; i++ )
	{
		diff = (result[c1][i] > result[c2][i]) ? (result[c1][i] - result[c2][i]) : (result[c2][i] - result[c1][i]);
		if (diff > MAX_TOLERANCE)
		{
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
	
	if ( SimularityBitMap == 0)
	{
		for( i = 0; i < (bound/4); i++ )
		{
			if(final_candidate[i] != 0xFF)
			{
				for( j = i*4; j < (i+1)*4-2; j++)
					result[3][j] = result[final_candidate[i]][j];
				bResult = _FALSE;
			}
		}
		return bResult;
	}
	else if (!(SimularityBitMap & 0x0F))			//path A OK
	{
		for(i = 0; i < 4; i++)
			result[3][i] = result[c1][i];
		return _FALSE;
	}
	else if (!(SimularityBitMap & 0xF0) && is2T)	//path B OK
	{
		for(i = 4; i < 8; i++)
			result[3][i] = result[c1][i];
		return _FALSE;
	}	
	else		
		return _FALSE;
	
}

static VOID	
_PHY_IQCalibrate(
	IN	PADAPTER	pAdapter,
	IN	int 		result[][8],
	IN	u8		t,
	IN	BOOLEAN		is2T
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32			i;
	u8			PathAOK, PathBOK;
	u32			ADDA_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };

	u32			IQK_MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	u32			IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
							rOFDM0_TRxPathEnable, 		rOFDM0_TRMuxPar,	
							rFPGA0_XCD_RFInterfaceSW,	rConfig_AntA,	rConfig_AntB,
							rFPGA0_XAB_RFInterfaceSW,	rFPGA0_XA_RFInterfaceOE,	
							rFPGA0_XB_RFInterfaceOE,	rFPGA0_RFMOD	
							};

#if MP_DRIVER
	const u32	retryCount = 9;
#else
	const u32	retryCount = 2;
#endif

	// Note: IQ calibration must be performed after loading 
	// 		PHY_REG.txt , and radio_a, radio_b.txt	
	
	u32 bbvalue;

	if(t==0)
	{
	 	bbvalue = PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("PHY_IQCalibrate()==>0x%08lx\n",bbvalue));

		//RTPRINT(FINIT, INIT_IQK, ("IQ Calibration for %s\n", (is2T ? "2T2R" : "1T1R")));
	
	 	// Save ADDA parameters, turn Path A ADDA on
	 	_PHY_SaveADDARegisters(pAdapter, ADDA_REG, pdmpriv->ADDA_backup,IQK_ADDA_REG_NUM);
		_PHY_SaveMACRegisters(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);
		_PHY_SaveADDARegisters(pAdapter, IQK_BB_REG_92C, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM);
	}
 	_PHY_PathADDAOn(pAdapter, ADDA_REG, _TRUE, is2T);

	if(t==0)
	{
		pdmpriv->bRfPiEnable = (u8)PHY_QueryBBReg(pAdapter, rFPGA0_XA_HSSIParameter1, BIT(8));
	}

	if(!pdmpriv->bRfPiEnable){
		// Switch BB to PI mode to do IQ Calibration.
		_PHY_PIModeSwitch(pAdapter, _TRUE);
	}

	PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, BIT24, 0x00);
	PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	PHY_SetBBReg(pAdapter, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22204000);
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0x01);
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0x01);
	PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, BIT10, 0x00);
	PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0x00);

	if(is2T)
	{
		PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00010000);
		PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00010000);
	}
	
	//MAC settings
	_PHY_MACSettingCalibration(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

	//Page B init
	PHY_SetBBReg(pAdapter, rConfig_AntA, bMaskDWord, 0x00080000);
	
	if(is2T)
	{
		PHY_SetBBReg(pAdapter, rConfig_AntB, bMaskDWord, 0x00080000);
	}
	
	// IQ calibration setting
	//RTPRINT(FINIT, INIT_IQK, ("IQK setting!\n"));		
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
	PHY_SetBBReg(pAdapter, rTx_IQK, bMaskDWord, 0x01007c00);
	PHY_SetBBReg(pAdapter, rRx_IQK, bMaskDWord, 0x01004800);

	for(i = 0 ; i < retryCount ; i++){
		PathAOK = _PHY_PathA_IQK(pAdapter, is2T);
		if(PathAOK == 0x03){
				DBG_8192C("Path A IQK Success!!\n");
				result[t][0] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
				result[t][1] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
				result[t][2] = (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][3] = (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			break;
		}
		else if (i == (retryCount-1) && PathAOK == 0x01)	//Tx IQK OK
		{
			DBG_8192C("Path A IQK Only  Tx Success!!\n");
			
			result[t][0] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;			
		}
	}

	if(0x00 == PathAOK){		
		DBG_8192C("Path A IQK failed!!\n");
	}

	if(is2T){
		_PHY_PathAStandBy(pAdapter);

		// Turn Path B ADDA on
		_PHY_PathADDAOn(pAdapter, ADDA_REG, _FALSE, is2T);

		for(i = 0 ; i < retryCount ; i++){
			PathBOK = _PHY_PathB_IQK(pAdapter);
			if(PathBOK == 0x03){
				DBG_8192C("Path B IQK Success!!\n");
				result[t][4] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][6] = (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][7] = (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				break;
			}
			else if (i == (retryCount - 1) && PathBOK == 0x01)	//Tx IQK OK
			{
				DBG_8192C("Path B Only Tx IQK Success!!\n");
				result[t][4] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
			}
		}

		if(0x00 == PathBOK){		
			DBG_8192C("Path B IQK failed!!\n");
		}
	}

	//Back to BB mode, load original value
	//RTPRINT(FINIT, INIT_IQK, ("IQK:Back to BB mode, load original value!\n"));
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0);

	if(t!=0)
	{
		if(!pdmpriv->bRfPiEnable){
			// Switch back BB to SI mode after finish IQ Calibration.
			_PHY_PIModeSwitch(pAdapter, _FALSE);
		}

		// Reload ADDA power saving parameters
	 	_PHY_ReloadADDARegisters(pAdapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);

		// Reload MAC parameters
		_PHY_ReloadMACRegisters(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

	 	// Reload BB parameters
	 	_PHY_ReloadADDARegisters(pAdapter, IQK_BB_REG_92C, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM);

		// Restore RX initial gain
		PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00032ed3);
		if(is2T){
			PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00032ed3);
		}

		//load 0xe30 IQC default value
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);

	}
	//RTPRINT(FINIT, INIT_IQK, ("_PHY_IQCalibrate() <==\n"));

}


static VOID	
_PHY_LCCalibrate(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		is2T
	)
{
	u8	tmpReg;
	u32 	RF_Amode = 0, RF_Bmode = 0, LC_Cal;

	//Check continuous TX and Packet TX
	tmpReg = rtw_read8(pAdapter, 0xd03);

	if((tmpReg&0x70) != 0)			//Deal with contisuous TX case
		rtw_write8(pAdapter, 0xd03, tmpReg&0x8F);	//disable all continuous TX
	else 							// Deal with Packet TX case
		rtw_write8(pAdapter, REG_TXPAUSE, 0xFF);			// block all queues

	if((tmpReg&0x70) != 0)
	{
		//1. Read original RF mode
		//Path-A
		RF_Amode = PHY_QueryRFReg(pAdapter, RF_PATH_A, RF_AC, bMask12Bits);

		//Path-B
		if(is2T)
			RF_Bmode = PHY_QueryRFReg(pAdapter, RF_PATH_B, RF_AC, bMask12Bits);	

		//2. Set RF mode = standby mode
		//Path-A
		PHY_SetRFReg(pAdapter, RF_PATH_A, RF_AC, bMask12Bits, (RF_Amode&0x8FFFF)|0x10000);

		//Path-B
		if(is2T)
			PHY_SetRFReg(pAdapter, RF_PATH_B, RF_AC, bMask12Bits, (RF_Bmode&0x8FFFF)|0x10000);
	}
	
	//3. Read RF reg18
	LC_Cal = PHY_QueryRFReg(pAdapter, RF_PATH_A, RF_CHNLBW, bMask12Bits);
	
	//4. Set LC calibration begin
	PHY_SetRFReg(pAdapter, RF_PATH_A, RF_CHNLBW, bMask12Bits, LC_Cal|0x08000);

	#ifdef CONFIG_LONG_DELAY_ISSUE
	rtw_msleep_os(100);	
	#else
	rtw_mdelay_os(100);		
	#endif

	//Restore original situation
	if((tmpReg&0x70) != 0)	//Deal with contisuous TX case 
	{  
		//Path-A
		rtw_write8(pAdapter, 0xd03, tmpReg);
		PHY_SetRFReg(pAdapter, RF_PATH_A, RF_AC, bMask12Bits, RF_Amode);
		
		//Path-B
		if(is2T)
			PHY_SetRFReg(pAdapter, RF_PATH_B, RF_AC, bMask12Bits, RF_Bmode);
	}
	else // Deal with Packet TX case
	{
		rtw_write8(pAdapter, REG_TXPAUSE, 0x00);	
	}
	
}


//Analog Pre-distortion calibration
#define		APK_BB_REG_NUM	8
#define		APK_CURVE_REG_NUM 4
#define		PATH_NUM		2

static VOID	
_PHY_APCalibrate(
	IN	PADAPTER	pAdapter,
	IN	char 		delta,
	IN	BOOLEAN		is2T
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	u32 			regD[PATH_NUM];
	u32			tmpReg, index, offset, i, apkbound;
	u8			path, pathbound = PATH_NUM;
	u32			BB_backup[APK_BB_REG_NUM];
	u32			BB_REG[APK_BB_REG_NUM] = {	
						rFPGA1_TxBlock, 	rOFDM0_TRxPathEnable, 
						rFPGA0_RFMOD, 	rOFDM0_TRMuxPar, 
						rFPGA0_XCD_RFInterfaceSW,	rFPGA0_XAB_RFInterfaceSW, 
						rFPGA0_XA_RFInterfaceOE, 	rFPGA0_XB_RFInterfaceOE	};
	u32			BB_AP_MODE[APK_BB_REG_NUM] = {	
						0x00000020, 0x00a05430, 0x02040000, 
						0x000800e4, 0x00204000 };
	u32			BB_normal_AP_MODE[APK_BB_REG_NUM] = {	
						0x00000020, 0x00a05430, 0x02040000, 
						0x000800e4, 0x22204000 };						

	u32			AFE_backup[IQK_ADDA_REG_NUM];
	u32			AFE_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };

	u32			MAC_backup[IQK_MAC_REG_NUM];
	u32			MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	u32			APK_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
					{0x0852c, 0x1852c, 0x5852c, 0x1852c, 0x5852c},
					{0x2852e, 0x0852e, 0x3852e, 0x0852e, 0x0852e}
					};	

	u32			APK_normal_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
					{0x0852c, 0x0a52c, 0x3a52c, 0x5a52c, 0x5a52c},	//path settings equal to path b settings
					{0x0852c, 0x0a52c, 0x5a52c, 0x5a52c, 0x5a52c}
					};
	
	u32			APK_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52014, 0x52013, 0x5200f, 0x5208d},
					{0x5201a, 0x52019, 0x52016, 0x52033, 0x52050}
					};

	u32			APK_normal_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a},	//path settings equal to path b settings
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a}
					};
#if 0
	u32			APK_RF_value_A[PATH_NUM][APK_BB_REG_NUM] = {
					{0x1adb0, 0x1adb0, 0x1ada0, 0x1ad90, 0x1ad80},		
					{0x00fb0, 0x00fb0, 0x00fa0, 0x00f90, 0x00f80}						
					};
#endif
	u32			AFE_on_off[PATH_NUM] = {
					0x04db25a4, 0x0b1b25a4};	//path A on path B off / path A off path B on

	u32			APK_offset[PATH_NUM] = {
					rConfig_AntA, rConfig_AntB};

	u32			APK_normal_offset[PATH_NUM] = {
					rConfig_Pmpd_AntA, rConfig_Pmpd_AntB};
					
	u32			APK_value[PATH_NUM] = {
					0x92fc0000, 0x12fc0000};					

	u32			APK_normal_value[PATH_NUM] = {
					0x92680000, 0x12680000};					

	char			APK_delta_mapping[APK_BB_REG_NUM][13] = {
					{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-6, -4, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-11, -9, -7, -5, -3, -1, 0, 0, 0, 0, 0, 0, 0}
					};
	
	u32			APK_normal_setting_value_1[13] = {
					0x01017018, 0xf7ed8f84, 0x1b1a1816, 0x2522201e, 0x322e2b28,
					0x433f3a36, 0x5b544e49, 0x7b726a62, 0xa69a8f84, 0xdfcfc0b3,
					0x12680000, 0x00880000, 0x00880000
					};

	u32			APK_normal_setting_value_2[16] = {
					0x01c7021d, 0x01670183, 0x01000123, 0x00bf00e2, 0x008d00a3,
					0x0068007b, 0x004d0059, 0x003a0042, 0x002b0031, 0x001f0025,
					0x0017001b, 0x00110014, 0x000c000f, 0x0009000b, 0x00070008,
					0x00050006
					};
	
	u32			APK_result[PATH_NUM][APK_BB_REG_NUM];	//val_1_1a, val_1_2a, val_2a, val_3a, val_4a
	//u32			AP_curve[PATH_NUM][APK_CURVE_REG_NUM];

	int			BB_offset, delta_V, delta_offset;

#if (MP_DRIVER == 1)
	PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.MptCtx;

	pMptCtx->APK_bound[0] = 45;
	pMptCtx->APK_bound[1] = 52;
#endif

	//RTPRINT(FINIT, INIT_IQK, ("==>PHY_APCalibrate() delta %d\n", delta));
	
	//RTPRINT(FINIT, INIT_IQK, ("AP Calibration for %s %s\n", (is2T ? "2T2R" : "1T1R"), (isNormal ? "Normal chip" : "Test chip")));

	if(!is2T)
		pathbound = 1;

	//2 FOR NORMAL CHIP SETTINGS

// Temporarily do not allow normal driver to do the following settings because these offset
// and value will cause RF internal PA to be unpredictably disabled by HW, such that RF Tx signal
// will disappear after disable/enable card many times on 88CU. RF SD and DD have not find the
// root cause, so we remove these actions temporarily. Added by tynli and SD3 Allen. 2010.05.31.
#if (MP_DRIVER != 1)
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
		BB_backup[index] = PHY_QueryBBReg(pAdapter, BB_REG[index], bMaskDWord);
	}

	//save MAC default value													
	_PHY_SaveMACRegisters(pAdapter, MAC_REG, MAC_backup);

	//save AFE default value
	_PHY_SaveADDARegisters(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

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
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}
			
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);
			
			offset = rConfig_AntA;
			for(; index < 13; index ++) 		
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}	
			
			//page-B1
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x40000000);
			
			//path A
			offset = rPdp_AntA;
			for(index = 0; index < 16; index++)
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_2[index]);		
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}				
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);							
		}
		else if(path == RF_PATH_B)
		{
			//path B APK
			//load APK setting
			//path-B		
			offset = rPdp_AntB;
			for(index = 0; index < 10; index ++)			
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntA, bMaskDWord, 0x12680000);
			
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);
			
			offset = rConfig_AntA;
			index = 11;
			for(; index < 13; index ++) //offset 0xb68, 0xb6c		
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}	
			
			//page-B1
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x40000000);
			
			//path B
			offset = 0xb60;
			for(index = 0; index < 16; index++)
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_2[index]);		
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}				
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);							
		}		

		//save RF default value
		regD[path] = PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask);
		
		//Path A AFE all on, path B AFE All off or vise versa
		for(index = 0; index < IQK_ADDA_REG_NUM ; index++)
			PHY_SetBBReg(pAdapter, AFE_REG[index], bMaskDWord, AFE_on_off[path]);
		//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0xe70 %x\n", PHY_QueryBBReg(pAdapter, 0xe70, bMaskDWord)));		

		//BB to AP mode
		if(path == 0)
		{
			for(index = 0; index < APK_BB_REG_NUM ; index++)
			{
				if(index == 0)		//skip 
					continue;
				else if (index < 5)
					PHY_SetBBReg(pAdapter, BB_REG[index], bMaskDWord, BB_AP_MODE[index]);
				else if (BB_REG[index] == 0x870)
					PHY_SetBBReg(pAdapter, BB_REG[index], bMaskDWord, BB_backup[index]|BIT10|BIT26);
				else
					PHY_SetBBReg(pAdapter, BB_REG[index], BIT10, 0x0);
			}
			PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);
			PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);
		}
		else		//path B
		{
			PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x01008c00);
			PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x01008c00);
		}

		//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x800 %x\n", PHY_QueryBBReg(pAdapter, 0x800, bMaskDWord)));				

		//MAC settings
		_PHY_MACSettingCalibration(pAdapter, MAC_REG, MAC_backup);

		if(path == RF_PATH_A)	//Path B to standby mode
		{
			PHY_SetRFReg(pAdapter, RF_PATH_B, RF_AC, bRFRegOffsetMask, 0x10000);			
		}
		else			//Path A to standby mode
		{
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x10000);			
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE1, bRFRegOffsetMask, 0x1000f);			
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE2, bRFRegOffsetMask, 0x20103);						
		}

		delta_offset = ((delta+14)/2);
		if(delta_offset < 0)
			delta_offset = 0;
		else if (delta_offset > 12)
			delta_offset = 12;
			
		//AP calibration
		for(index = 0; index < APK_BB_REG_NUM; index++)
		{
			if(index != 1)		//only DO PA11+PAD01001, AP RF setting
				continue;
					
			tmpReg = APK_RF_init_value[path][index];
#if 1			
			if(!pdmpriv->bAPKThermalMeterIgnore)
			{
				BB_offset = (tmpReg & 0xF0000) >> 16;

				if(!(tmpReg & BIT15)) //sign bit 0
				{
					BB_offset = -BB_offset;
				}

				delta_V = APK_delta_mapping[index][delta_offset];
				
				BB_offset += delta_V;

				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() APK num %d delta_V %d delta_offset %d\n", index, delta_V, delta_offset));		
				
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

#ifdef CONFIG_PCI_HCI
			if(IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID))
				PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_IPA_A, bRFRegOffsetMask, 0x894ae);
			else
#endif
				PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_IPA_A, bRFRegOffsetMask, 0x8992e);
			//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0xc %x\n", PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)path, 0xc, bMaskDWord)));
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_AC, bRFRegOffsetMask, APK_RF_value_0[path][index]);
			//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x0 %x\n", PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)path, 0x0, bMaskDWord)));		
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask, tmpReg);
			//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0xd %x\n", PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)path, 0xd, bMaskDWord)));

			// PA11+PAD01111, one shot	
			i = 0;
			do
			{
				PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80000000);
				{
					PHY_SetBBReg(pAdapter, APK_offset[path], bMaskDWord, APK_value[0]);		
					//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", APK_offset[path], PHY_QueryBBReg(pAdapter, APK_offset[path], bMaskDWord)));
					rtw_mdelay_os(3);				
					PHY_SetBBReg(pAdapter, APK_offset[path], bMaskDWord, APK_value[1]);
					//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", APK_offset[path], PHY_QueryBBReg(pAdapter, APK_offset[path], bMaskDWord)));
					#ifdef CONFIG_LONG_DELAY_ISSUE
					rtw_msleep_os(20);
					#else
					rtw_mdelay_os(20);
					#endif
				}
				PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);
				
				if(path == RF_PATH_A)
					tmpReg = PHY_QueryBBReg(pAdapter, rAPK, 0x03E00000);
				else
					tmpReg = PHY_QueryBBReg(pAdapter, rAPK, 0xF8000000);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0xbd8[25:21] %x\n", tmpReg));

				i++;
			}
			while(tmpReg > apkbound && i < 4);

			APK_result[path][index] = tmpReg;
		}
	}

	//reload MAC default value	
	_PHY_ReloadMACRegisters(pAdapter, MAC_REG, MAC_backup);

	//reload BB default value	
	for(index = 0; index < APK_BB_REG_NUM ; index++)
	{
		if(index == 0)		//skip 
			continue;
		PHY_SetBBReg(pAdapter, BB_REG[index], bMaskDWord, BB_backup[index]);
	}

	//reload AFE default value
	_PHY_ReloadADDARegisters(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	//reload RF path default value
	for(path = 0; path < pathbound; path++)
	{
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask, regD[path]);
		if(path == RF_PATH_B)
		{
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE1, bRFRegOffsetMask, 0x1000f);			
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE2, bRFRegOffsetMask, 0x20101);						
		}

		//note no index == 0
		if (APK_result[path][1] > 6)
			APK_result[path][1] = 6;
		//RTPRINT(FINIT, INIT_IQK, ("apk path %d result %d 0x%x \t", path, 1, APK_result[path][1]));
	}

	//RTPRINT(FINIT, INIT_IQK, ("\n"));
	

	for(path = 0; path < pathbound; path++)
	{
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G1_G4, bRFRegOffsetMask, 
		((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (APK_result[path][1] << 5) | APK_result[path][1]));
		if(path == RF_PATH_A)
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G5_G8, bRFRegOffsetMask, 
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x00 << 5) | 0x05));
		else
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G5_G8, bRFRegOffsetMask, 
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x02 << 5) | 0x05));
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G9_G11, bRFRegOffsetMask, 
		((0x08 << 15) | (0x08 << 10) | (0x08 << 5) | 0x08));
	}

	pdmpriv->bAPKdone = _TRUE;

	//RTPRINT(FINIT, INIT_IQK, ("<==PHY_APCalibrate()\n"));
}

VOID
rtl8192c_PHY_IQCalibrate(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN 	bReCovery
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	s32			result[4][8];	//last is final result
	u8			i, final_candidate;
	BOOLEAN		bPathAOK, bPathBOK;
	s32			RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC, RegTmp = 0;
	BOOLEAN		is12simular, is13simular, is23simular;	
	BOOLEAN 	bStartContTx = _FALSE, bSingleTone = _FALSE, bCarrierSuppression = _FALSE;
	u32			IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
					rOFDM0_XARxIQImbalance, 	rOFDM0_XBRxIQImbalance, 
					rOFDM0_ECCAThreshold, 	rOFDM0_AGCRSSITable,
					rOFDM0_XATxIQImbalance, 	rOFDM0_XBTxIQImbalance, 
					rOFDM0_XCTxAFE, 			rOFDM0_XDTxAFE, 
					rOFDM0_RxIQExtAnta};


#if MP_DRIVER == 1	
	bStartContTx = pAdapter->mppriv.MptCtx.bStartContTx;
	bSingleTone = pAdapter->mppriv.MptCtx.bSingleTone;
	bCarrierSuppression = pAdapter->mppriv.MptCtx.bCarrierSuppression;	
#endif

	//ignore IQK when continuous Tx
	if(bStartContTx || bSingleTone || bCarrierSuppression)
		return;

#if DISABLE_BB_RF
	return;
#endif

	if(bReCovery)
	{
		_PHY_ReloadADDARegisters(pAdapter, IQK_BB_REG_92C, pdmpriv->IQK_BB_backup_recover, 9);
		return;
	}
	DBG_8192C("IQK:Start!!!\n");

	for(i = 0; i < 8; i++)
	{
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;
		result[3][i] = 0;
	}
	final_candidate = 0xff;
	bPathAOK = _FALSE;
	bPathBOK = _FALSE;
	is12simular = _FALSE;
	is23simular = _FALSE;
	is13simular = _FALSE;

	for (i=0; i<3; i++)
	{
	 	if(IS_92C_SERIAL( pHalData->VersionID)){
			 _PHY_IQCalibrate(pAdapter, result, i, _TRUE);
	 	}
	 	else{
	 		// For 88C 1T1R
	 		_PHY_IQCalibrate(pAdapter, result, i, _FALSE);
 		}
		
		if(i == 1)
		{
			is12simular = _PHY_SimularityCompare(pAdapter, result, 0, 1);
			if(is12simular)
			{
				final_candidate = 0;
				break;
			}
		}
		
		if(i == 2)
		{
			is13simular = _PHY_SimularityCompare(pAdapter, result, 0, 2);
			if(is13simular)
			{
				final_candidate = 0;			
				break;
			}
			
			is23simular = _PHY_SimularityCompare(pAdapter, result, 1, 2);
			if(is23simular)
				final_candidate = 1;
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
		//RTPRINT(FINIT, INIT_IQK, ("IQK: RegE94=%lx RegE9C=%lx RegEA4=%lx RegEAC=%lx RegEB4=%lx RegEBC=%lx RegEC4=%lx RegECC=%lx\n ", RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC));
	}

	if(final_candidate != 0xff)
	{
		pdmpriv->RegE94 = RegE94 = result[final_candidate][0];
		pdmpriv->RegE9C = RegE9C = result[final_candidate][1];
		RegEA4 = result[final_candidate][2];
		RegEAC = result[final_candidate][3];
		pdmpriv->RegEB4 = RegEB4 = result[final_candidate][4];
		pdmpriv->RegEBC = RegEBC = result[final_candidate][5];
		RegEC4 = result[final_candidate][6];
		RegECC = result[final_candidate][7];
		DBG_8192C("IQK: final_candidate is %x\n", final_candidate);
		DBG_8192C("IQK: RegE94=%x RegE9C=%x RegEA4=%x RegEAC=%x RegEB4=%x RegEBC=%x RegEC4=%x RegECC=%x\n ", RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC);
		bPathAOK = bPathBOK = _TRUE;
	}
	else
	{
		RegE94 = RegEB4 = pdmpriv->RegE94 = pdmpriv->RegEB4 = 0x100;	//X default value
		RegE9C = RegEBC = pdmpriv->RegE9C = pdmpriv->RegEBC = 0x0;		//Y default value
	}
	
	if((RegE94 != 0)/*&&(RegEA4 != 0)*/)
		_PHY_PathAFillIQKMatrix(pAdapter, bPathAOK, result, final_candidate, (RegEA4 == 0));
	
	if(IS_92C_SERIAL( pHalData->VersionID)){
		if((RegEB4 != 0)/*&&(RegEC4 != 0)*/)
		_PHY_PathBFillIQKMatrix(pAdapter, bPathBOK, result, final_candidate, (RegEC4 == 0));
	}

	_PHY_SaveADDARegisters(pAdapter, IQK_BB_REG_92C, pdmpriv->IQK_BB_backup_recover, 9);

}


VOID
rtl8192c_PHY_LCCalibrate(
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct mlme_ext_priv	*pmlmeext = &pAdapter->mlmeextpriv;
	BOOLEAN 	bStartContTx = _FALSE, bSingleTone = _FALSE, bCarrierSuppression = _FALSE;

#if MP_DRIVER == 1
	bStartContTx = pAdapter->mppriv.MptCtx.bStartContTx;
	bSingleTone = pAdapter->mppriv.MptCtx.bSingleTone;
	bCarrierSuppression = pAdapter->mppriv.MptCtx.bCarrierSuppression;
#endif

#if DISABLE_BB_RF
	return;
#endif

	//ignore IQK when continuous Tx
	if(bStartContTx || bSingleTone || bCarrierSuppression)
		return;

	if(pmlmeext->sitesurvey_res.state == SCAN_PROCESS)
		return;

	if(IS_92C_SERIAL( pHalData->VersionID)){
		_PHY_LCCalibrate(pAdapter, _TRUE);
	}
	else{
		// For 88C 1T1R
		_PHY_LCCalibrate(pAdapter, _FALSE);
	}
}

VOID
rtl8192c_PHY_APCalibrate(
	IN	PADAPTER	pAdapter,
	IN	char 		delta	
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	//default disable APK, because Tx NG issue, suggest by Jenyu, 2011.11.25
	return;

#if DISABLE_BB_RF
	return;
#endif

	if(pdmpriv->bAPKdone)
		return;

	if(IS_92C_SERIAL( pHalData->VersionID)){
		_PHY_APCalibrate(pAdapter, delta, _TRUE);
	}
	else{
		// For 88C 1T1R
		_PHY_APCalibrate(pAdapter, delta, _FALSE);
	}
}



