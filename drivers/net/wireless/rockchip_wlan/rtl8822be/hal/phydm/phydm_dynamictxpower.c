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

	/*if (!IS_HARDWARE_TYPE_8814A(Adapter)) {*/
	/*	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, */
	/*	("odm_DynamicTxPowerInit DynamicTxPowerEnable=%d\n", pMgntInfo->bDynamicTxPowerEnable));*/
	/*	return;*/
	/*} else*/
	{
		pMgntInfo->bDynamicTxPowerEnable = TRUE;
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, 
		("odm_DynamicTxPowerInit DynamicTxPowerEnable=%d\n", pMgntInfo->bDynamicTxPowerEnable));
	}
		
	#if DEV_BUS_TYPE==RT_USB_INTERFACE					
	if(RT_GetInterfaceSelection(Adapter) == INTF_SEL1_USB_High_Power)
	{
		odm_DynamicTxPowerSavePowerIndex(pDM_Odm);
		pMgntInfo->bDynamicTxPowerEnable = TRUE;
	}		
	else	
	#else
	//so 92c pci do not need dynamic tx power? vivi check it later
	pMgntInfo->bDynamicTxPowerEnable = FALSE;
	#endif
	

	pHalData->LastDTPLvl = TxHighPwrLevel_Normal;
	pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
	
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

	pDM_Odm->LastDTPLvl = TxHighPwrLevel_Normal;
	pDM_Odm->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
	pDM_Odm->tx_agc_ofdm_18_6 = ODM_GetBBReg(pDM_Odm, 0xC24, bMaskDWord); /*TXAGC {18M 12M 9M 6M}*/

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
odm_DynamicTxPowerNIC_CE(
	IN		PVOID					pDM_VOID
	)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
#if (RTL8821A_SUPPORT == 1)
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte			val;
	u1Byte			rssi_tmp = pDM_Odm->RSSI_Min;

	if (!(pDM_Odm->SupportAbility & ODM_BB_DYNAMIC_TXPWR))
		return;

	if (rssi_tmp >= TX_POWER_NEAR_FIELD_THRESH_LVL2) {
		pDM_Odm->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
		/**/
	} else if (rssi_tmp >= TX_POWER_NEAR_FIELD_THRESH_LVL1) {
		pDM_Odm->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
		/**/
	} else if (rssi_tmp < (TX_POWER_NEAR_FIELD_THRESH_LVL1-5)) {
		pDM_Odm->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		/**/
	}

	if (pDM_Odm->LastDTPLvl != pDM_Odm->DynamicTxHighPowerLvl) {

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, ODM_DBG_LOUD, ("update_DTP_lv: ((%d)) -> ((%d))\n", pDM_Odm->LastDTPLvl, pDM_Odm->DynamicTxHighPowerLvl));

		pDM_Odm->LastDTPLvl = pDM_Odm->DynamicTxHighPowerLvl;

		if (pDM_Odm->SupportICType & (ODM_RTL8821)) {

			if (pDM_Odm->DynamicTxHighPowerLvl == TxHighPwrLevel_Level2) {

				ODM_SetMACReg(pDM_Odm, 0x6D8, BIT20|BIT19|BIT18, 1); /* Resp TXAGC offset = -3dB*/

				val = pDM_Odm->tx_agc_ofdm_18_6 & 0xff;
				if (val >= 0x20)
					val -= 0x16;

				ODM_SetBBReg(pDM_Odm, 0xC24, 0xff, val);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, ODM_DBG_LOUD, ("Set TX power: level 2\n"));
			} else if (pDM_Odm->DynamicTxHighPowerLvl == TxHighPwrLevel_Level1) {

				ODM_SetMACReg(pDM_Odm, 0x6D8, BIT20|BIT19|BIT18, 1); /* Resp TXAGC offset = -3dB*/

				val = pDM_Odm->tx_agc_ofdm_18_6 & 0xff;
				if (val >= 0x20)
					val -= 0x10;

				ODM_SetBBReg(pDM_Odm, 0xC24, 0xff, val);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, ODM_DBG_LOUD, ("Set TX power: level 1\n"));
			} else if (pDM_Odm->DynamicTxHighPowerLvl == TxHighPwrLevel_Normal) {
			
				ODM_SetMACReg(pDM_Odm, 0x6D8, BIT20|BIT19|BIT18, 0); /* Resp TXAGC offset = 0dB*/
				ODM_SetBBReg(pDM_Odm, 0xC24, bMaskDWord, pDM_Odm->tx_agc_ofdm_18_6);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, ODM_DBG_LOUD, ("Set TX power: normal\n"));
			}
		}
	}

#endif
#endif
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
			odm_DynamicTxPowerNIC(pDM_Odm);
			break;
		case	ODM_CE:
			odm_DynamicTxPowerNIC_CE(pDM_Odm);
			break;	
		case	ODM_AP:
			odm_DynamicTxPowerAP(pDM_Odm);
			break;		
		default:
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
	
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

	if (pDM_Odm->SupportICType == ODM_RTL8814A) {
		odm_DynamicTxPower_8814A(pDM_Odm);
	} else if (pDM_Odm->SupportICType & ODM_RTL8821) {
		PADAPTER		Adapter	 =  pDM_Odm->Adapter;
		PMGNT_INFO		pMgntInfo = GetDefaultMgntInfo(Adapter);

		if (pMgntInfo->RegRspPwr == 1)	{
			if (pDM_Odm->RSSI_Min > 60)
				ODM_SetMACReg(pDM_Odm, ODM_REG_RESP_TX_11AC, BIT20|BIT19|BIT18, 1); /*Resp TXAGC offset = -3dB*/
			else if (pDM_Odm->RSSI_Min < 55)
				ODM_SetMACReg(pDM_Odm, ODM_REG_RESP_TX_11AC, BIT20|BIT19|BIT18, 0); /*Resp TXAGC offset = 0dB*/
		}
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
	s2Byte pwr_thd = 63;

	if(!priv->pshare->rf_ft_var.tx_pwr_ctrl)
		return;
	
#if ((RTL8812A_SUPPORT == 1) || (RTL8881A_SUPPORT == 1) || (RTL8814A_SUPPORT == 1))
	if (pDM_Odm->SupportICType & (ODM_RTL8812 | ODM_RTL8881A | ODM_RTL8814A))
		pwr_thd = TX_POWER_NEAR_FIELD_THRESH_LVL1;
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

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID 
odm_DynamicTxPower_8814A(
	IN		PVOID					pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER Adapter = pDM_Odm->Adapter;
	PMGNT_INFO			pMgntInfo = &Adapter->MgntInfo;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	s4Byte				UndecoratedSmoothedPWDB;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, 
	("TxLevel=%d pMgntInfo->IOTAction=%x pMgntInfo->bDynamicTxPowerEnable=%d\n", 
	pHalData->DynamicTxHighPowerLvl, pMgntInfo->IOTAction, pMgntInfo->bDynamicTxPowerEnable));
	
	/*STA not connected and AP not connected*/
	if ((!pMgntInfo->bMediaConnect) && (pHalData->EntryMinUndecoratedSmoothedPWDB == 0)) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("Not connected to any reset power lvl\n"));
		pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		return;
	}


	if ((pMgntInfo->bDynamicTxPowerEnable != TRUE) || pMgntInfo->IOTAction & HT_IOT_ACT_DISABLE_HIGH_POWER) {
		pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;		
	} else {
		if (pMgntInfo->bMediaConnect) {	/*Default port*/
			if (ACTING_AS_AP(Adapter) || ACTING_AS_IBSS(Adapter)) {
				UndecoratedSmoothedPWDB = pHalData->EntryMinUndecoratedSmoothedPWDB;
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("AP Client PWDB = 0x%x\n", UndecoratedSmoothedPWDB));
			} else {
				UndecoratedSmoothedPWDB = pHalData->UndecoratedSmoothedPWDB;
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("STA Default Port PWDB = 0x%x\n", UndecoratedSmoothedPWDB));
			}
		} else {/*associated entry pwdb*/
			UndecoratedSmoothedPWDB = pHalData->EntryMinUndecoratedSmoothedPWDB;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("AP Ext Port PWDB = 0x%x\n", UndecoratedSmoothedPWDB));
			}

		/*Should we separate as 2.4G/5G band?*/
			
		if (UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL2) {
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x0)\n"));
		} else if ((UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL2-3)) &&
			(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL1)) {
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
		} else if (UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL1-5)) {
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("TxHighPwrLevel_Normal\n"));
		}
	}


	if (pHalData->DynamicTxHighPowerLvl != pHalData->LastDTPLvl) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("odm_DynamicTxPower_8814A() Channel = %d\n" , pHalData->CurrentChannel));
		odm_SetTxPowerLevel8814(Adapter, pHalData->CurrentChannel, pHalData->DynamicTxHighPowerLvl);	
	}


	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, 
	("odm_DynamicTxPower_8814A() Channel = %d  TXpower lvl=%d/%d\n" , 
	pHalData->CurrentChannel, pHalData->LastDTPLvl, pHalData->DynamicTxHighPowerLvl));

	pHalData->LastDTPLvl = pHalData->DynamicTxHighPowerLvl;

}



/**/
/*For normal driver we always use the FW method to configure TX power index to reduce I/O transaction.*/
/**/
/**/
VOID
odm_SetTxPowerLevel8814(
	IN	PADAPTER		Adapter,
	IN	u1Byte			Channel,
	IN	u1Byte			PwrLvl
	)
{
#if (DEV_BUS_TYPE == RT_USB_INTERFACE)
	u4Byte			i, j, k = 0;
	u4Byte			value[264] = {0};
	u4Byte			path = 0, PowerIndex, txagc_table_wd = 0x00801000;

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	u1Byte	jaguar2Rates[][4] = { {MGN_1M, MGN_2M, MGN_5_5M, MGN_11M}, 
								{MGN_6M, MGN_9M, MGN_12M, MGN_18M}, 
								{MGN_24M, MGN_36M, MGN_48M, MGN_54M}, 
								{MGN_MCS0, MGN_MCS1, MGN_MCS2, MGN_MCS3},
								{MGN_MCS4, MGN_MCS5, MGN_MCS6, MGN_MCS7}, 
								{MGN_MCS8, MGN_MCS9, MGN_MCS10, MGN_MCS11},
								{MGN_MCS12, MGN_MCS13, MGN_MCS14, MGN_MCS15},
								{MGN_MCS16, MGN_MCS17, MGN_MCS18, MGN_MCS19}, 
								{MGN_MCS20, MGN_MCS21, MGN_MCS22, MGN_MCS23},
								{MGN_VHT1SS_MCS0, MGN_VHT1SS_MCS1, MGN_VHT1SS_MCS2, MGN_VHT1SS_MCS3}, 
								{MGN_VHT1SS_MCS4, MGN_VHT1SS_MCS5, MGN_VHT1SS_MCS6, MGN_VHT1SS_MCS7}, 
								{MGN_VHT2SS_MCS8, MGN_VHT2SS_MCS9, MGN_VHT2SS_MCS0, MGN_VHT2SS_MCS1}, 
								{MGN_VHT2SS_MCS2, MGN_VHT2SS_MCS3, MGN_VHT2SS_MCS4, MGN_VHT2SS_MCS5}, 
								{MGN_VHT2SS_MCS6, MGN_VHT2SS_MCS7, MGN_VHT2SS_MCS8, MGN_VHT2SS_MCS9},
								{MGN_VHT3SS_MCS0, MGN_VHT3SS_MCS1, MGN_VHT3SS_MCS2, MGN_VHT3SS_MCS3}, 
								{MGN_VHT3SS_MCS4, MGN_VHT3SS_MCS5, MGN_VHT3SS_MCS6, MGN_VHT3SS_MCS7}, 
								{MGN_VHT3SS_MCS8, MGN_VHT3SS_MCS9, 0, 0} };	
	
	for (path = ODM_RF_PATH_A; path <= ODM_RF_PATH_D; ++path) {
		
		u1Byte	usb_host = UsbModeQueryHubUsbType(Adapter);
		u1Byte	usb_rfset = UsbModeQueryRfSet(Adapter);
		u1Byte	usb_rf_type = RT_GetRFType(Adapter);
			
		for (i = 0; i <= 16; i++) {
			for (j = 0; j <= 3; j++) {
				if (jaguar2Rates[i][j] == 0)
					continue;
				
				txagc_table_wd =  0x00801000;
				PowerIndex =  (u4Byte) PHY_GetTxPowerIndex(Adapter, (u1Byte)path, jaguar2Rates[i][j], pHalData->CurrentChannelBW, Channel);		

				/*for Query bus type to recude tx power.*/
				if (usb_host != USB_MODE_U3 && usb_rfset == 1 && IS_HARDWARE_TYPE_8814AU(Adapter) && usb_rf_type == RF_3T3R) {
					if (Channel <= 14) {
						if (PowerIndex >= 16)
							PowerIndex -= 16;
						else
							PowerIndex = 0;
					} else
						PowerIndex = 0;
				}

				if (PwrLvl == TxHighPwrLevel_Level1) {
					if (PowerIndex >= 0x10)
						PowerIndex -= 0x10;
					else
						PowerIndex = 0;
				} else if (PwrLvl == TxHighPwrLevel_Level2) {
					PowerIndex = 0;
				}
				
				txagc_table_wd |= (path << 8) | MRateToHwRate(jaguar2Rates[i][j]) | (PowerIndex << 24);

				PHY_SetTxPowerIndexShadow(Adapter, (u1Byte)PowerIndex, (u1Byte)path, jaguar2Rates[i][j]);

				value[k++] = txagc_table_wd;
			}
		}
	}

	if (Adapter->MgntInfo.bScanInProgress == FALSE &&  Adapter->MgntInfo.RegFWOffload == 2)
		HalDownloadTxPowerLevel8814(Adapter, value);
#endif
}
#endif


