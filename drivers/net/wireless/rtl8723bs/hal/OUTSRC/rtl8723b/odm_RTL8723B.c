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

#if (RTL8723B_SUPPORT == 1)

VOID
odm8723b_DigForBtHsMode(
	IN		PDM_ODM_T		pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	//PFALSE_ALARM_STATISTICS	pFalseAlmCnt = &pDM_Odm->FalseAlmCnt;
	pDIG_T					pDM_DigTable=&pDM_Odm->DM_DigTable;
	u1Byte					digForBtHs=0;
	//BOOLEAN					bChkFA=FALSE;
	
	if(pDM_Odm->bBtConnectProcess)
	{
		if(pDM_Odm->SupportICType&(ODM_RTL8723A))
			digForBtHs = 0x28;
		else
			digForBtHs = 0x22;
	}
	else
	{
		//
		// Decide DIG value by BT RSSI.
		//
		digForBtHs = pDM_Odm->btHsRssi+4;
#if 0
		// check if cur dig and pre dig diff is larger than 10
		
		if(digForBtHs > pDM_DigTable->PreIGValue)
		{
			if( (digForBtHs - pDM_DigTable->PreIGValue) > 10)
				pDM_DigTable->PreIGValue = digForBtHs;
			else
				bChkFA = TRUE;
		}
		else
		{
			if( (pDM_DigTable->PreIGValue -digForBtHs) > 10)
				pDM_DigTable->PreIGValue = digForBtHs;
			else
				bChkFA = TRUE;
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DigForBtHsMode() : pFalseAlmCnt->Cnt_all=0x%x\n", pFalseAlmCnt->Cnt_all));
		//Current IGI update by FA
		if(bChkFA)
		{				
			if(pFalseAlmCnt->Cnt_all > 0x150)
				pDM_DigTable->PreIGValue += 2;
			else if (pFalseAlmCnt->Cnt_all > 0x100)
				pDM_DigTable->PreIGValue += 1;
			else if(pFalseAlmCnt->Cnt_all < 0x20)
				pDM_DigTable->PreIGValue -= 1;

			digForBtHs = pDM_DigTable->PreIGValue;
		}
	
		if(digForBtHs > (pDM_Odm->btHsRssi+10))
			digForBtHs = (pDM_Odm->btHsRssi+10);
		else
		{
			if(pDM_Odm->btHsRssi < 10)
				digForBtHs = 0x1c;
			else
			{
				if(digForBtHs > (pDM_Odm->btHsRssi -10))
					digForBtHs = (pDM_Odm->btHsRssi -10);
			}
		}
#endif
		//DIG Bound
		if(digForBtHs > 0x3e)
			digForBtHs = 0x3e;
		if(digForBtHs < 0x1c)
			digForBtHs = 0x1c;

		// update Current IGI
		pDM_DigTable->BT30_CurIGI = digForBtHs;
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DigForBtHsMode() : set DigValue=0x%x\n", digForBtHs));
	//ODM_Write_DIG(pDM_Odm, digForBtHs);
#endif
}

VOID 
odm_DIG_8723(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	pDIG_T						pDM_DigTable = &pDM_Odm->DM_DigTable;
	PFALSE_ALARM_STATISTICS		pFalseAlmCnt = &pDM_Odm->FalseAlmCnt;
	//pRXHP_T						pRX_HP_Table  = &pDM_Odm->DM_RXHP_Table;
	u1Byte						DIG_Dynamic_MIN;
	BOOLEAN						FirstConnect, FirstDisConnect;
	u1Byte						dm_dig_max, dm_dig_min;
	u1Byte						CurrentIGI = pDM_DigTable->CurIGValue;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
// This should be moved out of OUTSRC
	PADAPTER		pAdapter	= pDM_Odm->Adapter;
#if OS_WIN_FROM_WIN7(OS_VERSION)
	if(IsAPModeExist( pAdapter) && pAdapter->bInHctTest)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: Is AP mode or In HCT Test \n"));
		return;
	}
#endif
	
	if(pDM_Odm->bBtHsOperation)
	{
		odm8723b_DigForBtHsMode(pDM_Odm);
	}

#if 0	     // Neil Chen No RX HP for 8723
	if(!(pDM_Odm->SupportICType &(ODM_RTL8723A|ODM_RTL8188E)))
	{
		if(pRX_HP_Table->RXHP_flag == 1)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: In RXHP Operation \n"));
			return;	
		}
	}
#endif


#endif

#if 0
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	prtl8192cd_priv	priv			= pDM_Odm->priv;	
	if (!((priv->up_time > 5) && (priv->up_time % 2)) )
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: Not In DIG Operation Period \n"));
		return;
	}
#endif
#endif

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG()==>\n"));
	//if(!(pDM_Odm->SupportAbility & (ODM_BB_DIG|ODM_BB_FA_CNT)))
	if((!(pDM_Odm->SupportAbility&ODM_BB_DIG)) ||(!(pDM_Odm->SupportAbility&ODM_BB_FA_CNT)))
	{
#if 0	     
		if(pDM_Odm->SupportPlatform & (ODM_AP|ODM_ADSL))
		{
			if ((pDM_Odm->SupportICType == ODM_RTL8192C) && (pDM_Odm->ExtLNA == 1))
				CurrentIGI = 0x30; //pDM_DigTable->CurIGValue  = 0x30;
			else
				CurrentIGI = 0x20; //pDM_DigTable->CurIGValue  = 0x20;
			ODM_Write_DIG(pDM_Odm, CurrentIGI);//ODM_Write_DIG(pDM_Odm, pDM_DigTable->CurIGValue);
		}
#endif		
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: SupportAbility ODM_BB_DIG or ODM_BB_FA_CNT is disabled\n"));
		return;
	}
		
	if(*(pDM_Odm->pbScanInProcess))
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: In Scan Progress \n"));
	    	return;
	}

	//add by Neil Chen to avoid PSD is processing
	if(pDM_Odm->SupportICType&(ODM_RTL8723A|ODM_RTL8723B))
	{
	        if(pDM_Odm->bDMInitialGainEnable == FALSE)
	        {
		        ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: PSD is Processing \n"));
		        return;
	        }
	}
		
	
	DIG_Dynamic_MIN = pDM_DigTable->DIG_Dynamic_MIN_0;
	FirstConnect = (pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == FALSE);
	FirstDisConnect = (!pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == TRUE);

	
#if 0 	
	if(pDM_Odm->SupportICType & (ODM_RTL8192C) &&(pDM_Odm->BoardType & (ODM_BOARD_EXT_LNA | ODM_BOARD_EXT_PA)))
	{
		if(pDM_Odm->SupportPlatform & (ODM_AP|ODM_ADSL))
		{

			dm_dig_max = DM_DIG_MAX_AP_HP;
			dm_dig_min = DM_DIG_MIN_AP_HP;
		}
		else
		{
			dm_dig_max = DM_DIG_MAX_NIC_HP;
			dm_dig_min = DM_DIG_MIN_NIC_HP;
		}
		DIG_MaxOfMin = DM_DIG_MAX_AP_HP;
	}
	else
	{
		if(pDM_Odm->SupportPlatform & (ODM_AP|ODM_ADSL))
		{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
#ifdef DFS
			if (!priv->pmib->dot11DFSEntry.disable_DFS &&
				(OPMODE & WIFI_AP_STATE) &&
				(((pDM_Odm->ControlChannel >= 52) &&
				(pDM_Odm->ControlChannel <= 64)) ||
				((pDM_Odm->ControlChannel >= 100) &&
				(pDM_Odm->ControlChannel <= 140))))
				dm_dig_max = 0x24;
			else
#endif
			if (priv->pmib->dot11RFEntry.tx2path) {
				if (*(pDM_Odm->pWirelessMode) == ODM_WM_B)//(priv->pmib->dot11BssType.net_work_type == WIRELESS_11B)
					dm_dig_max = 0x2A;
				else
					dm_dig_max = 0x32;
			}
			else
#endif				
			dm_dig_max = DM_DIG_MAX_AP;
			dm_dig_min = DM_DIG_MIN_AP;
			DIG_MaxOfMin = dm_dig_max;
		}
		else
		{
			if((pDM_Odm->SupportICType >= ODM_RTL8188E) && (pDM_Odm->SupportPlatform & (ODM_WIN|ODM_CE)))
				dm_dig_max = 0x5A;
			else
				dm_dig_max = DM_DIG_MAX_NIC;
			
			dm_dig_min = DM_DIG_MIN_NIC;
			DIG_MaxOfMin = DM_DIG_MAX_AP;
		}
	}
#endif   // masked by neilchen to simpily 8723B case


	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): RSSI=0x%x\n",pDM_Odm->RSSI_Min));

	if((pDM_Odm->SupportICType >= ODM_RTL8723B) && (pDM_Odm->SupportPlatform & (ODM_WIN|ODM_CE)))
		dm_dig_max = 0x5A;
	else
		dm_dig_max = DM_DIG_MAX_NIC;

			
	dm_dig_min = DM_DIG_MIN_NIC_8723;
		
	if(pDM_Odm->bLinked)
	{
		if(pDM_Odm->SupportICType&(ODM_RTL8723B))
		{
			//BT is Concurrent
			if(pDM_Odm->bBtLimitedDig)
			{
				if(( pDM_Odm->RSSI_Min + 10) > DM_DIG_MAX_NIC )
					pDM_DigTable->rx_gain_range_max = DM_DIG_MAX_NIC;
				else if(( pDM_Odm->RSSI_Min + 10) < DM_DIG_MIN_NIC )
					pDM_DigTable->rx_gain_range_max = DM_DIG_MIN_NIC;
				else
					pDM_DigTable->rx_gain_range_max = pDM_Odm->RSSI_Min + 10;
			
				if(pDM_Odm->RSSI_Min>10)
				{
					if((pDM_Odm->RSSI_Min - 10) > DM_DIG_MAX_NIC)
						DIG_Dynamic_MIN = DM_DIG_MAX_NIC;
					else if((pDM_Odm->RSSI_Min - 10) < DM_DIG_MIN_NIC_8723)
						DIG_Dynamic_MIN = DM_DIG_MIN_NIC_8723;
					else
						DIG_Dynamic_MIN = pDM_Odm->RSSI_Min - 10;
				}
				else
					DIG_Dynamic_MIN=DM_DIG_MIN_NIC_8723;
			}
			else
			{
				if((pDM_Odm->RSSI_Min + 20) > dm_dig_max )
					pDM_DigTable->rx_gain_range_max = dm_dig_max;
				else if((pDM_Odm->RSSI_Min + 20) < dm_dig_min )
					pDM_DigTable->rx_gain_range_max = dm_dig_min;
				else
					pDM_DigTable->rx_gain_range_max = pDM_Odm->RSSI_Min + 20;


				if(pDM_Odm->RSSI_Min>20)
				{
					if((pDM_Odm->RSSI_Min - 20) > DM_DIG_MAX_NIC)
						DIG_Dynamic_MIN = DM_DIG_MAX_NIC;
					else if((pDM_Odm->RSSI_Min - 20) < DM_DIG_MIN_NIC_8723)
						DIG_Dynamic_MIN = DM_DIG_MIN_NIC_8723;
					else
						DIG_Dynamic_MIN = pDM_Odm->RSSI_Min -20;
				}
				else
					DIG_Dynamic_MIN=DM_DIG_MIN_NIC_8723;
				
				
			}
		}

	}
	else
	{
		pDM_DigTable->rx_gain_range_max = dm_dig_max;
		pDM_DigTable->rx_gain_range_min = DM_DIG_MIN_NIC_8723;
		DIG_Dynamic_MIN = dm_dig_min;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() : No Link\n"));
	}
	
#if 0
	if(pFalseAlmCnt->Cnt_all > 10000)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("dm_DIG(): Abnornally false alarm case. \n"));

		if(pDM_DigTable->LargeFAHit != 3)
		        pDM_DigTable->LargeFAHit++;
		if(pDM_DigTable->ForbiddenIGI < CurrentIGI)//if(pDM_DigTable->ForbiddenIGI < pDM_DigTable->CurIGValue)
		{
			pDM_DigTable->ForbiddenIGI = CurrentIGI;//pDM_DigTable->ForbiddenIGI = pDM_DigTable->CurIGValue;
			pDM_DigTable->LargeFAHit = 1;
		}

		if(pDM_DigTable->LargeFAHit >= 3)
		{
			if((pDM_DigTable->ForbiddenIGI+1) >pDM_DigTable->rx_gain_range_max)
				pDM_DigTable->rx_gain_range_min = pDM_DigTable->rx_gain_range_max;
			else
				pDM_DigTable->rx_gain_range_min = (pDM_DigTable->ForbiddenIGI + 1);
			pDM_DigTable->Recover_cnt = 3600; //3600=2hr
		}

	}
	else
	{
		//Recovery mechanism for IGI lower bound
		if(pDM_DigTable->Recover_cnt != 0)
			pDM_DigTable->Recover_cnt --;
		else
		{
			if(pDM_DigTable->LargeFAHit < 3)
			{
				if((pDM_DigTable->ForbiddenIGI -1) < DIG_Dynamic_MIN) //DM_DIG_MIN)
				{
					pDM_DigTable->ForbiddenIGI = DIG_Dynamic_MIN; //DM_DIG_MIN;
					pDM_DigTable->rx_gain_range_min = DIG_Dynamic_MIN; //DM_DIG_MIN;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Normal Case: At Lower Bound\n"));
				}
				else
				{
					pDM_DigTable->ForbiddenIGI --;
					pDM_DigTable->rx_gain_range_min = (pDM_DigTable->ForbiddenIGI + 1);
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Normal Case: Approach Lower Bound\n"));
				}
			}
			else
			{
				pDM_DigTable->LargeFAHit = 0;
			}
		}
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): pDM_DigTable->LargeFAHit=%d\n",pDM_DigTable->LargeFAHit));
#endif

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG():pDM_DigTable->Recover_cnt=%d\n",pDM_DigTable->Recover_cnt));

	//1 Adjust initial gain by false alarm
	if(pDM_Odm->bLinked)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): DIG AfterLink\n"));
		if(FirstConnect)
		{
			CurrentIGI = pDM_Odm->RSSI_Min;
			ODM_RT_TRACE(pDM_Odm,	ODM_COMP_DIG, ODM_DBG_LOUD, ("DIG: First Connect\n"));
		}
		else
		{
			//FA for Combo IC--NeilChen--2012--09--28 
			if(pDM_Odm->SupportICType == ODM_RTL8723B)
			{
     					//WLAN and BT ConCurrent
				if(pDM_Odm->bBtLimitedDig)
				{
					if(pFalseAlmCnt->Cnt_all > 0x500)
						CurrentIGI = CurrentIGI + 4;
					else if (pFalseAlmCnt->Cnt_all > 0x300)
						CurrentIGI = CurrentIGI + 2;
					else if(pFalseAlmCnt->Cnt_all <0x150)
						CurrentIGI = CurrentIGI -2;
				}
				else //Not Concurrent
				{
					if(pFalseAlmCnt->Cnt_all > 0x400)
						CurrentIGI = CurrentIGI + 4;//pDM_DigTable->CurIGValue = pDM_DigTable->PreIGValue+2;
					else if (pFalseAlmCnt->Cnt_all > 0x200)
						CurrentIGI = CurrentIGI + 2;//pDM_DigTable->CurIGValue = pDM_DigTable->PreIGValue+1;
					else if(pFalseAlmCnt->Cnt_all < 0x100)
						CurrentIGI = CurrentIGI - 2;//pDM_DigTable->CurIGValue =pDM_DigTable->PreIGValue-1;	
				}
			}
		
		}
	}	
	else
	{
                CurrentIGI = pDM_DigTable->rx_gain_range_min;//pDM_DigTable->CurIGValue = pDM_DigTable->rx_gain_range_min
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): DIG BeforeLink\n"));
		if(FirstDisConnect)
		{
				CurrentIGI = pDM_DigTable->rx_gain_range_min;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): First DisConnect \n"));
		}
		else
		{
			//2012.03.30 LukeLee: enable DIG before link but with very high thresholds
	             if(pFalseAlmCnt->Cnt_all > 10000)
				CurrentIGI = CurrentIGI + 4;
			else if (pFalseAlmCnt->Cnt_all > 8000)
				CurrentIGI = CurrentIGI + 2;
			else if(pFalseAlmCnt->Cnt_all < 500)
				CurrentIGI = CurrentIGI - 2;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): England DIG \n"));
		}
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): DIG End Adjust IGI\n"));
	//1 Check initial gain by upper/lower bound

	if(CurrentIGI > pDM_DigTable->rx_gain_range_max)
		CurrentIGI = pDM_DigTable->rx_gain_range_max;
	if(CurrentIGI < pDM_DigTable->rx_gain_range_min)
		CurrentIGI = pDM_DigTable->rx_gain_range_min;
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): rx_gain_range_max=0x%x, rx_gain_range_min=0x%x\n", 
		pDM_DigTable->rx_gain_range_max, pDM_DigTable->rx_gain_range_min));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): TotalFA=%d\n", pFalseAlmCnt->Cnt_all));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): CurIGValue=0x%x\n", CurrentIGI));

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): RSSI=0x%x\n",pDM_Odm->RSSI_Min));

	//2 High power RSSI threshold
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)	
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pDM_Odm->Adapter);
	//----------------------------------------------------------end for LC Mocca issue
	if((pDM_Odm->SupportICType == ODM_RTL8723B)&& (pHalData->UndecoratedSmoothedPWDB > DM_DIG_HIGH_PWR_THRESHOLD))
	{
		// High power IGI lower bound
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): UndecoratedSmoothedPWDB(%#x)\n", pHalData->UndecoratedSmoothedPWDB));
		if(CurrentIGI < DM_DIG_HIGH_PWR_IGI_LOWER_BOUND)
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): CurIGValue(%#x)\n", pDM_DigTable->CurIGValue));
			//pDM_DigTable->CurIGValue = DM_DIG_HIGH_PWR_IGI_LOWER_BOUND;
			CurrentIGI=DM_DIG_HIGH_PWR_IGI_LOWER_BOUND;
		}
	}
#if 0	
	if((pDM_Odm->SupportICType & ODM_RTL8723A) && 
			IS_WIRELESS_MODE_G(pAdapter))
		{
			if(pHalData->UndecoratedSmoothedPWDB > 0x28)
			{
				if(CurrentIGI < DM_DIG_Gmode_HIGH_PWR_IGI_LOWER_BOUND)
				{
			 		//pDM_DigTable->CurIGValue = DM_DIG_Gmode_HIGH_PWR_IGI_LOWER_BOUND;
					CurrentIGI = DM_DIG_Gmode_HIGH_PWR_IGI_LOWER_BOUND;
				}	
			} 
		}	

#endif  // end if 0 
}
#endif

	if(pDM_Odm->bBtHsOperation)
	{
		if(pDM_Odm->bLinked)
		{
			if(pDM_DigTable->BT30_CurIGI > (CurrentIGI))
			{
				ODM_Write_DIG(pDM_Odm, CurrentIGI);
				
			}	
			else
			{
				ODM_Write_DIG(pDM_Odm, pDM_DigTable->BT30_CurIGI);
			}
			pDM_DigTable->bMediaConnect_0 = pDM_Odm->bLinked;
			pDM_DigTable->DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
		}
		else
		{
			if(pDM_Odm->bLinkInProcess)
			{
				ODM_Write_DIG(pDM_Odm, 0x1c);
			}
			else if(pDM_Odm->bBtConnectProcess)
			{
				ODM_Write_DIG(pDM_Odm, 0x28);
			}
			else
			{
				ODM_Write_DIG(pDM_Odm, pDM_DigTable->BT30_CurIGI);//ODM_Write_DIG(pDM_Odm, pDM_DigTable->CurIGValue);	
			}
		}
	}	
	else		// BT is not using
	{
		ODM_Write_DIG(pDM_Odm, CurrentIGI);//ODM_Write_DIG(pDM_Odm, pDM_DigTable->CurIGValue);
		pDM_DigTable->bMediaConnect_0 = pDM_Odm->bLinked;
		pDM_DigTable->DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
	}	
}


 s1Byte
odm_CCKRSSI_8723B(
	IN		u1Byte	LNA_idx,
	IN		u1Byte	VGA_idx
	)
{
	s1Byte	rx_pwr_all=0x00;
	switch(LNA_idx)
	{
		//46  53 73 95 201301231630
		// 46 53 77 99 201301241630
		
		case 6:	
                        rx_pwr_all = -34 - (2 * VGA_idx);
			break;
		case 4:	
                        rx_pwr_all = -14 - (2 * VGA_idx);
			break;
		case 1:	
                        rx_pwr_all = 6 - (2 * VGA_idx);
			break;
		case 0:	
                        rx_pwr_all = 16 - (2 * VGA_idx);	
			break;
		default:
                        //rx_pwr_all = -53+(2*(31-VGA_idx));
                        //DbgPrint("wrong LNA index\n");
			break;
			
	}
	return	rx_pwr_all;
}

s1Byte
odm_RSSIOFDM_8723B(
	IN	s1Byte	rx_pwr_new)
{
	s1Byte	rx_pwr_all=0;
	
	if(rx_pwr_new >=63)
		rx_pwr_all	=rx_pwr_new -120+10;
	else if(rx_pwr_all >=54)
		rx_pwr_all=rx_pwr_new-120+6;
	else if(rx_pwr_all >=43)
		rx_pwr_all=rx_pwr_new-120+8;
	else if(rx_pwr_all >=33)
		rx_pwr_all=rx_pwr_new-120+8;
	else if(rx_pwr_all >=23)
		rx_pwr_all=rx_pwr_new-120+10;
	else 
		rx_pwr_all=rx_pwr_new-120+7;


	return rx_pwr_all;

}

#endif		// end if RTL8723B 








