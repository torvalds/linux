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
#include "mp_precomp.h"
#include "phydm_precomp.h"

VOID 
odm_DynamicTxPowerInit(
	IN		PVOID					pDM_VOID	
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER	Adapter = pDM_Odm->Adapter;
	PMGNT_INFO			pMgntInfo = &Adapter->MgntInfo;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	#if DEV_BUS_TYPE==RT_USB_INTERFACE					
	if(RT_GetInterfaceSelection(Adapter) == INTF_SEL1_USB_High_Power)
	{
		odm_DynamicTxPowerSavePowerIndex(pDM_Odm);
		pMgntInfo->bDynamicTxPowerEnable = TRUE;
	}		
	else	
	#else
	//so 92c pci do not need dynamic tx power? vivi check it later
	if(IS_HARDWARE_TYPE_8192D(Adapter))
		pMgntInfo->bDynamicTxPowerEnable = TRUE;
	else
		pMgntInfo->bDynamicTxPowerEnable = FALSE;
	#endif
	

	pHalData->LastDTPLvl = TxHighPwrLevel_Normal;
	pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;


#endif
	
}

VOID
odm_DynamicTxPowerSavePowerIndex(
	IN		PVOID					pDM_VOID	
	)
{	
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_WIN))
	u1Byte		index;
	u4Byte		Power_Index_REG[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};
	
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	
	PADAPTER	Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	
	for(index = 0; index< 6; index++)
		pHalData->PowerIndex_backup[index] = PlatformEFIORead1Byte(Adapter, Power_Index_REG[index]);
	
	
#endif
#endif
}

VOID
odm_DynamicTxPowerRestorePowerIndex(
	IN		PVOID					pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_WIN))
	u1Byte			index;
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u4Byte			Power_Index_REG[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	for(index = 0; index< 6; index++)
		PlatformEFIOWrite1Byte(Adapter, Power_Index_REG[index], pHalData->PowerIndex_backup[index]);


#endif
#endif
}

VOID
odm_DynamicTxPowerWritePowerIndex(
	IN		PVOID					pDM_VOID, 
	IN 	u1Byte		Value)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte			index;
	u4Byte			Power_Index_REG[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};
	
	for(index = 0; index< 6; index++)
		//PlatformEFIOWrite1Byte(Adapter, Power_Index_REG[index], Value);
		ODM_Write1Byte(pDM_Odm, Power_Index_REG[index], Value);

}


VOID 
odm_DynamicTxPower(
	IN		PVOID					pDM_VOID
	)
{
	// 
	// For AP/ADSL use prtl8192cd_priv
	// For CE/NIC use PADAPTER
	//
	//PADAPTER		pAdapter = pDM_Odm->Adapter;
//	prtl8192cd_priv	priv		= pDM_Odm->priv;
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	if (!(pDM_Odm->SupportAbility & ODM_BB_DYNAMIC_TXPWR))
		return;
	//
	// 2011/09/29 MH In HW integration first stage, we provide 4 different handle to operate
	// at the same time. In the stage2/3, we need to prive universal interface and merge all
	// HW dynamic mechanism.
	//
	switch	(pDM_Odm->SupportPlatform)
	{
		case	ODM_WIN:
		case	ODM_CE:
			odm_DynamicTxPowerNIC(pDM_Odm);
			break;	
		case	ODM_AP:
			odm_DynamicTxPowerAP(pDM_Odm);
			break;		

		case	ODM_ADSL:
			//odm_DIGAP(pDM_Odm);
			break;	
	}

	
}


VOID 
odm_DynamicTxPowerNIC(
	IN		PVOID					pDM_VOID
	)
{	
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	
	if (!(pDM_Odm->SupportAbility & ODM_BB_DYNAMIC_TXPWR))
		return;
	
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))

	if(pDM_Odm->SupportICType == ODM_RTL8192C)	
	{
		odm_DynamicTxPower_92C(pDM_Odm);
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8192D)
	{
		odm_DynamicTxPower_92D(pDM_Odm);
	}
	else if (pDM_Odm->SupportICType == ODM_RTL8821)
	{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
		PADAPTER		Adapter	 =  pDM_Odm->Adapter;
		PMGNT_INFO		pMgntInfo = GetDefaultMgntInfo(Adapter);

		if (pMgntInfo->RegRspPwr == 1)
		{
			if(pDM_Odm->RSSI_Min > 60)
			{
				ODM_SetMACReg(pDM_Odm, ODM_REG_RESP_TX_11AC, BIT20|BIT19|BIT18, 1); // Resp TXAGC offset = -3dB

			}
			else if(pDM_Odm->RSSI_Min < 55)
			{
				ODM_SetMACReg(pDM_Odm, ODM_REG_RESP_TX_11AC, BIT20|BIT19|BIT18, 0); // Resp TXAGC offset = 0dB
			}
		}
#endif
	}
#endif	
}

VOID 
odm_DynamicTxPowerAP(
	IN		PVOID					pDM_VOID

	)
{	
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)

//#if ((RTL8192C_SUPPORT==1) || (RTL8192D_SUPPORT==1) || (RTL8188E_SUPPORT==1) || (RTL8812E_SUPPORT==1))


	prtl8192cd_priv	priv		= pDM_Odm->priv;
	s4Byte i;
	s2Byte pwr_thd = TX_POWER_NEAR_FIELD_THRESH_AP;

	if(!priv->pshare->rf_ft_var.tx_pwr_ctrl)
		return;
	
#if ((RTL8812E_SUPPORT==1) || (RTL8881A_SUPPORT==1) || (RTL8814A_SUPPORT==1))
	if (pDM_Odm->SupportICType & (ODM_RTL8812 | ODM_RTL8881A | ODM_RTL8814A))
		pwr_thd = TX_POWER_NEAR_FIELD_THRESH_8812;
#endif

#if defined(CONFIG_RTL_92D_SUPPORT) || defined(CONFIG_RTL_92C_SUPPORT)
	if(CHIP_VER_92X_SERIES(priv))
	{
#ifdef HIGH_POWER_EXT_PA
	if(pDM_Odm->ExtPA)
		tx_power_control(priv);
#endif		
	}
#endif	
	/*
	 *	Check if station is near by to use lower tx power
	 */

	if ((priv->up_time % 3) == 0 )  {
		int disable_pwr_ctrl = ((pDM_Odm->FalseAlmCnt.Cnt_all > 1000 ) || ((pDM_Odm->FalseAlmCnt.Cnt_all > 300 ) && ((RTL_R8(0xc50) & 0x7f) >= 0x32))) ? 1 : 0;
			
		for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++){
			PSTA_INFO_T pstat = pDM_Odm->pODM_StaInfo[i];
			if(IS_STA_VALID(pstat) ) {
					if(disable_pwr_ctrl)
						pstat->hp_level = 0;
					 else if ((pstat->hp_level == 0) && (pstat->rssi > pwr_thd))
					pstat->hp_level = 1;
						else if ((pstat->hp_level == 1) && (pstat->rssi < (pwr_thd-8)))
					pstat->hp_level = 0;
			}
		}

#if defined(CONFIG_WLAN_HAL_8192EE)
		if (GET_CHIP_VER(priv) == VERSION_8192E) {
			if( !disable_pwr_ctrl && (pDM_Odm->RSSI_Min != 0xff) ) {
				if(pDM_Odm->RSSI_Min > pwr_thd)
					RRSR_power_control_11n(priv,  1 );
				else if(pDM_Odm->RSSI_Min < (pwr_thd-8))
					RRSR_power_control_11n(priv,  0 );
			} else {
					RRSR_power_control_11n(priv,  0 );
			}
		}
#endif			

#ifdef CONFIG_WLAN_HAL_8814AE
		if (GET_CHIP_VER(priv) == VERSION_8814A) {
			if (!disable_pwr_ctrl && (pDM_Odm->RSSI_Min != 0xff)) {
				if (pDM_Odm->RSSI_Min > pwr_thd)
					RRSR_power_control_14(priv,  1);
				else if (pDM_Odm->RSSI_Min < (pwr_thd-8))
					RRSR_power_control_14(priv,  0);
			} else {
					RRSR_power_control_14(priv,  0);
			}
		}
#endif		

	}
//#endif	

#endif	
}


VOID 
odm_DynamicTxPower_92C(
	IN		PVOID					pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER Adapter = pDM_Odm->Adapter;
	PMGNT_INFO			pMgntInfo = &Adapter->MgntInfo;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	s4Byte				UndecoratedSmoothedPWDB;

	// 2012/01/12 MH According to Luke's suggestion, only high power will support the feature.
	if (pDM_Odm->ExtPA == FALSE)
		return;

	// STA not connected and AP not connected
	if((!pMgntInfo->bMediaConnect) &&	
		(pHalData->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("Not connected to any \n"));
		pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;

		//the LastDTPlvl should reset when disconnect, 
		//otherwise the tx power level wouldn't change when disconnect and connect again.
		// Maddest 20091220.
		 pHalData->LastDTPLvl=TxHighPwrLevel_Normal;
		return;
	}

#if (INTEL_PROXIMITY_SUPPORT == 1)
	// Intel set fixed tx power 
	if(pMgntInfo->IntelProximityModeInfo.PowerOutput > 0)
	{
		switch(pMgntInfo->IntelProximityModeInfo.PowerOutput){
			case 1:
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_100;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_100\n"));
				break;
			case 2:
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_70;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_70\n"));
				break;
			case 3:
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_50;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_50\n"));
				break;
			case 4:
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_35;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_35\n"));
				break;
			case 5:
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_15;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_15\n"));
				break;
			default:
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_100;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_100\n"));
				break;
		}		
	}
	else
#endif		
	{ 
		if(	(pMgntInfo->bDynamicTxPowerEnable != TRUE) ||
			pMgntInfo->IOTAction & HT_IOT_ACT_DISABLE_HIGH_POWER)
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		}
		else
		{
			if(pMgntInfo->bMediaConnect)	// Default port
			{
				if(ACTING_AS_AP(Adapter) || ACTING_AS_IBSS(Adapter))
				{
					UndecoratedSmoothedPWDB = pHalData->EntryMinUndecoratedSmoothedPWDB;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("AP Client PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
				}
				else
				{
					UndecoratedSmoothedPWDB = pHalData->UndecoratedSmoothedPWDB;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("STA Default Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
				}
			}
			else // associated entry pwdb
			{	
				UndecoratedSmoothedPWDB = pHalData->EntryMinUndecoratedSmoothedPWDB;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("AP Ext Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
			}
				
			if(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL2)
			{
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x0)\n"));
			}
			else if((UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL2-3)) &&
				(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL1) )
			{
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
			}
			else if(UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL1-5))
			{
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_Normal\n"));
			}
		}
	}
	if( pHalData->DynamicTxHighPowerLvl != pHalData->LastDTPLvl )
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("PHY_SetTxPowerLevel8192C() Channel = %d \n" , pHalData->CurrentChannel));
		PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
		if(	(pHalData->DynamicTxHighPowerLvl == TxHighPwrLevel_Normal) &&
			(pHalData->LastDTPLvl == TxHighPwrLevel_Level1 || pHalData->LastDTPLvl == TxHighPwrLevel_Level2)) //TxHighPwrLevel_Normal
			odm_DynamicTxPowerRestorePowerIndex(pDM_Odm);
		else if(pHalData->DynamicTxHighPowerLvl == TxHighPwrLevel_Level1)
			odm_DynamicTxPowerWritePowerIndex(pDM_Odm, 0x14);
		else if(pHalData->DynamicTxHighPowerLvl == TxHighPwrLevel_Level2)
			odm_DynamicTxPowerWritePowerIndex(pDM_Odm, 0x10);
	}
	pHalData->LastDTPLvl = pHalData->DynamicTxHighPowerLvl;

	


#endif	// #if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

}


VOID 
odm_DynamicTxPower_92D(
	IN		PVOID					pDM_VOID
	)
{
#if (RTL8192D_SUPPORT==1)
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER Adapter = pDM_Odm->Adapter;
	PMGNT_INFO			pMgntInfo = &Adapter->MgntInfo;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	s4Byte				UndecoratedSmoothedPWDB;

	PADAPTER	BuddyAdapter = Adapter->BuddyAdapter;
	BOOLEAN		bGetValueFromBuddyAdapter = dm_DualMacGetParameterFromBuddyAdapter(Adapter);
	u1Byte		HighPowerLvlBackForMac0 = TxHighPwrLevel_Level1;

	// 2012/01/12 MH According to Luke's suggestion, only high power will support the feature.
	if (pDM_Odm->ExtPA == FALSE)
		return;

	// If dynamic high power is disabled.
	if( (pMgntInfo->bDynamicTxPowerEnable != TRUE) ||
		pMgntInfo->IOTAction & HT_IOT_ACT_DISABLE_HIGH_POWER)
	{
		pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		return;
	}

	// STA not connected and AP not connected
	if((!pMgntInfo->bMediaConnect) &&	
		(pHalData->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("Not connected to any \n"));
		pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;

		//the LastDTPlvl should reset when disconnect, 
		//otherwise the tx power level wouldn't change when disconnect and connect again.
		// Maddest 20091220.
		 pHalData->LastDTPLvl=TxHighPwrLevel_Normal;
		return;
	}
	
	if(pMgntInfo->bMediaConnect)	// Default port
	{
		if(ACTING_AS_AP(Adapter) || pMgntInfo->mIbss)
		{
			UndecoratedSmoothedPWDB = pHalData->EntryMinUndecoratedSmoothedPWDB;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("AP Client PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
		}
		else
		{
			UndecoratedSmoothedPWDB = pHalData->UndecoratedSmoothedPWDB;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("STA Default Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
		}
	}
	else // associated entry pwdb
	{	
		UndecoratedSmoothedPWDB = pHalData->EntryMinUndecoratedSmoothedPWDB;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("AP Ext Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
	}
	
	if(IS_HARDWARE_TYPE_8192D(Adapter) && GET_HAL_DATA(Adapter)->CurrentBandType == 1){
		if(UndecoratedSmoothedPWDB >= 0x33)
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("5G:TxHighPwrLevel_Level2 (TxPwr=0x0)\n"));
		}
		else if((UndecoratedSmoothedPWDB <0x33) &&
			(UndecoratedSmoothedPWDB >= 0x2b) )
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("5G:TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
		}
		else if(UndecoratedSmoothedPWDB < 0x2b)
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("5G:TxHighPwrLevel_Normal\n"));
		}

	}
	else
	
	{
		if(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL2)
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x0)\n"));
		}
		else if((UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL2-3)) &&
			(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL1) )
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
		}
		else if(UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL1-5))
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_Normal\n"));
		}

	}

//sherry  delete flag 20110517
	if(bGetValueFromBuddyAdapter)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR,DBG_LOUD,("dm_DynamicTxPower() mac 0 for mac 1 \n"));
		if(Adapter->DualMacDMSPControl.bChangeTxHighPowerLvlForAnotherMacOfDMSP)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR,DBG_LOUD,("dm_DynamicTxPower() change value \n"));
			HighPowerLvlBackForMac0 = pHalData->DynamicTxHighPowerLvl;
			pHalData->DynamicTxHighPowerLvl = Adapter->DualMacDMSPControl.CurTxHighLvlForAnotherMacOfDMSP;
			PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
			pHalData->DynamicTxHighPowerLvl = HighPowerLvlBackForMac0;
			Adapter->DualMacDMSPControl.bChangeTxHighPowerLvlForAnotherMacOfDMSP = FALSE;
		}						
	}

	if( (pHalData->DynamicTxHighPowerLvl != pHalData->LastDTPLvl) )
	{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("PHY_SetTxPowerLevel8192S() Channel = %d \n" , pHalData->CurrentChannel));
			if(Adapter->DualMacSmartConcurrent == TRUE)
			{
				if(BuddyAdapter == NULL)
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR,DBG_LOUD,("dm_DynamicTxPower() BuddyAdapter == NULL case \n"));
					if(!Adapter->bSlaveOfDMSP)
					{
						PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
					}
				}
				else
				{
					if(pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
					{
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR,DBG_LOUD,("dm_DynamicTxPower() BuddyAdapter DMSP \n"));
						if(Adapter->bSlaveOfDMSP)
						{
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR,DBG_LOUD,("dm_DynamicTxPower() bslave case  \n"));
							BuddyAdapter->DualMacDMSPControl.bChangeTxHighPowerLvlForAnotherMacOfDMSP = TRUE;
							BuddyAdapter->DualMacDMSPControl.CurTxHighLvlForAnotherMacOfDMSP = pHalData->DynamicTxHighPowerLvl;
						}
						else
						{
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR,DBG_LOUD,("dm_DynamicTxPower() master case  \n"));					
							if(!bGetValueFromBuddyAdapter)
							{
								ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR,DBG_LOUD,("dm_DynamicTxPower() mac 0 for mac 0 \n"));
								PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
							}
						}
					}
					else
					{
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR,DBG_LOUD,("dm_DynamicTxPower() BuddyAdapter DMDP\n"));
						PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
					}
				}
			}
			else
			{
				PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
			}

		}
	pHalData->LastDTPLvl = pHalData->DynamicTxHighPowerLvl;


#endif	// #if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#endif
}

VOID 
odm_DynamicTxPower_8821(
	IN		PVOID			pDM_VOID,	
	IN		pu1Byte			pDesc,
	IN		u1Byte			macId	
	)
{
#if (RTL8821A_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PSTA_INFO_T		pEntry;
	u1Byte			reg0xc56_byte;
	u1Byte			reg0xe56_byte;
	u1Byte			txpwr_offset = 0;
	
	pEntry = pDM_Odm->pODM_StaInfo[macId];	

	reg0xc56_byte = ODM_Read1Byte(pDM_Odm, 0xc56);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("reg0xc56_byte=%d\n", reg0xc56_byte));

	if (pEntry[macId].rssi_stat.UndecoratedSmoothedPWDB > 85) {

		/* Avoid TXAGC error after TX power offset is applied.
		For example: Reg0xc56=0x6, if txpwr_offset=3( reduce 11dB )
		Total power = 6-11= -5( overflow!! ), PA may be burned !
		so txpwr_offset should be adjusted by Reg0xc56*/
		
		if (reg0xc56_byte < 7)
			txpwr_offset = 1;
		else if (reg0xc56_byte < 11)
			txpwr_offset = 2;
		else
			txpwr_offset = 3;
		
		SET_TX_DESC_TX_POWER_OFFSET_8812(pDesc, txpwr_offset);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("odm_DynamicTxPower_8821: RSSI=%d, txpwr_offset=%d\n", pEntry[macId].rssi_stat.UndecoratedSmoothedPWDB, txpwr_offset));

	} else{
		SET_TX_DESC_TX_POWER_OFFSET_8812(pDesc, txpwr_offset);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("odm_DynamicTxPower_8821: RSSI=%d, txpwr_offset=%d\n", pEntry[macId].rssi_stat.UndecoratedSmoothedPWDB, txpwr_offset));

	}
#endif	/*#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/
#endif	/*#if (RTL8821A_SUPPORT==1)*/
}

