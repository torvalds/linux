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
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_byteorder.h>

#include <hal_intf.h>

#include <rtl8192d_hal.h>
#include "../dm.h"
//avoid to warn in FreeBSD
u32 EDCAParam[maxAP][3] =
{          // UL			DL
	{0x5ea322, 0x00a630, 0x00a44f}, //atheros AP
	{0x5ea32b, 0x5ea42b, 0x5e4322}, //broadcom AP
	{0x3ea430, 0x00a630, 0x3ea44f}, //cisco AP
	{0x5ea44f, 0x00a44f, 0x5ea42b}, //marvell AP
	{0x5ea422, 0x00a44f, 0x00a44f}, //ralink AP
	//{0x5ea44f, 0x5ea44f, 0x5ea44f}, //realtek AP
	{0xa44f, 0x5ea44f, 0x5e431c}, //realtek AP
	{0x5ea42b, 0xa630, 0x5e431c}, //airgocap AP
	{0x5ea42b, 0x5ea42b, 0x5ea42b}, //unknown AP
//	{0x5e4322, 0x00a44f, 0x5ea44f}, //unknown AP
};

extern atomic_t GlobalMutexForGlobalAdapterList;

/*-----------------------------------------------------------------------------
 * Function:	dm_DIGInit()
 *
 * Overview:	Set DIG scheme init value.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *
 *---------------------------------------------------------------------------*/
static void dm_DIGInit(
	IN	PADAPTER	pAdapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	pDIG_T	pDM_DigTable = &pdmpriv->DM_DigTable;
	

	pDM_DigTable->Dig_Enable_Flag = _TRUE;
	pDM_DigTable->Dig_Ext_Port_Stage = DIG_EXT_PORT_STAGE_MAX;
	
	pDM_DigTable->CurIGValue = 0x20;
	pDM_DigTable->PreIGValue = 0x0;

	pDM_DigTable->CurSTAConnectState = pDM_DigTable->PreSTAConnectState = DIG_STA_DISCONNECT;
	pDM_DigTable->CurMultiSTAConnectState = DIG_MultiSTA_DISCONNECT;

	pDM_DigTable->RssiLowThresh 	= DM_DIG_THRESH_LOW;
	pDM_DigTable->RssiHighThresh 	= DM_DIG_THRESH_HIGH;

	pDM_DigTable->FALowThresh	= DM_FALSEALARM_THRESH_LOW;
	pDM_DigTable->FAHighThresh	= DM_FALSEALARM_THRESH_HIGH;

	
	pDM_DigTable->rx_gain_range_max = DM_DIG_MAX;
	pDM_DigTable->rx_gain_range_min = DM_DIG_MIN;
	pDM_DigTable->rx_gain_range_min_nolink = 0;

	pDM_DigTable->BackoffVal = DM_DIG_BACKOFF_DEFAULT;
	pDM_DigTable->BackoffVal_range_max = DM_DIG_BACKOFF_MAX;
	pDM_DigTable->BackoffVal_range_min = DM_DIG_BACKOFF_MIN;

	pDM_DigTable->PreCCKPDState = CCK_PD_STAGE_MAX;
	pDM_DigTable->CurCCKPDState = CCK_PD_STAGE_LowRssi;
	pDM_DigTable->ForbiddenIGI = DM_DIG_MIN;

	pDM_DigTable->LargeFAHit = 0;
	pDM_DigTable->Recover_cnt = 0;
}

#ifdef CONFIG_DUALMAC_CONCURRENT
static BOOLEAN
dm_DualMacGetParameterFromBuddyAdapter(
		PADAPTER	Adapter
)
{
	PADAPTER	BuddyAdapter = Adapter->pbuddy_adapter;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv *pmlmepriv = &(Adapter->mlmepriv);
	struct mlme_priv *pbuddy_mlmepriv = &(BuddyAdapter->mlmepriv);

	if(pHalData->MacPhyMode92D != DUALMAC_SINGLEPHY)
		return _FALSE;
	
	if(BuddyAdapter == NULL)
		return _FALSE;

	if(pHalData->bSlaveOfDMSP)
		return _FALSE;

//sherry sync with 92C_92D, 20110701
	if((check_fwstate(pbuddy_mlmepriv, _FW_LINKED)) && (!check_fwstate(pmlmepriv, _FW_LINKED))
		&& (!check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) ))
		return _TRUE;
	else
		return _FALSE;
}
#endif

static VOID 
odm_FalseAlarmCounterStatistics_ForSlaveOfDMSP(
	IN	PADAPTER	Adapter
)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
	PADAPTER	BuddyAdapter = Adapter->pbuddy_adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PFALSE_ALARM_STATISTICS FalseAlmCnt = &(pdmpriv->FalseAlmCnt);
	struct dm_priv	*Buddydmpriv;
	PFALSE_ALARM_STATISTICS	FlaseAlmCntBuddyAdapter;

	if(BuddyAdapter == NULL)
		return;

	if (Adapter->DualMacConcurrent == _FALSE)
		return;

	Buddydmpriv = &GET_HAL_DATA(BuddyAdapter)->dmpriv;
	FlaseAlmCntBuddyAdapter = &(Buddydmpriv->FalseAlmCnt);
	
	FalseAlmCnt->Cnt_Fast_Fsync =FlaseAlmCntBuddyAdapter->Cnt_Fast_Fsync;
	FalseAlmCnt->Cnt_SB_Search_fail =FlaseAlmCntBuddyAdapter->Cnt_SB_Search_fail;		
    	FalseAlmCnt->Cnt_Parity_Fail = FlaseAlmCntBuddyAdapter->Cnt_Parity_Fail;	
	FalseAlmCnt->Cnt_Rate_Illegal = FlaseAlmCntBuddyAdapter->Cnt_Rate_Illegal;
	FalseAlmCnt->Cnt_Crc8_fail = FlaseAlmCntBuddyAdapter->Cnt_Crc8_fail;
	FalseAlmCnt->Cnt_Mcs_fail = FlaseAlmCntBuddyAdapter->Cnt_Mcs_fail;

	FalseAlmCnt->Cnt_Ofdm_fail = 	FalseAlmCnt->Cnt_Parity_Fail + FalseAlmCnt->Cnt_Rate_Illegal +
								FalseAlmCnt->Cnt_Crc8_fail + FalseAlmCnt->Cnt_Mcs_fail +
								FalseAlmCnt->Cnt_Fast_Fsync + FalseAlmCnt->Cnt_SB_Search_fail;

	
	//hold cck counter
	FalseAlmCnt->Cnt_Cck_fail = FlaseAlmCntBuddyAdapter->Cnt_Cck_fail;

	FalseAlmCnt->Cnt_all = (	FalseAlmCnt->Cnt_Fast_Fsync + 
						FalseAlmCnt->Cnt_SB_Search_fail +
						FalseAlmCnt->Cnt_Parity_Fail +
						FalseAlmCnt->Cnt_Rate_Illegal +
						FalseAlmCnt->Cnt_Crc8_fail +
						FalseAlmCnt->Cnt_Mcs_fail +
						FalseAlmCnt->Cnt_Cck_fail);	

/*
	RT_TRACE(	COMP_DIG, DBG_LOUD, ("Cnt_Fast_Fsync = %d, Cnt_SB_Search_fail = %d\n", 
				FalseAlmCnt->Cnt_Fast_Fsync , FalseAlmCnt->Cnt_SB_Search_fail) );	
	RT_TRACE(	COMP_DIG, DBG_LOUD, ("Cnt_Parity_Fail = %d, Cnt_Rate_Illegal = %d, Cnt_Crc8_fail = %d, Cnt_Mcs_fail = %d\n", 
				FalseAlmCnt->Cnt_Parity_Fail, FalseAlmCnt->Cnt_Rate_Illegal, FalseAlmCnt->Cnt_Crc8_fail, FalseAlmCnt->Cnt_Mcs_fail) );	
	RT_TRACE(	COMP_DIG, DBG_LOUD, ("Cnt_Ofdm_fail = %d, Cnt_Cck_fail = %d, Cnt_all = %d\n", 
				FalseAlmCnt->Cnt_Ofdm_fail, FalseAlmCnt->Cnt_Cck_fail, FalseAlmCnt->Cnt_all) );		
*/
#endif
}

static VOID 
odm_FalseAlarmCounterStatistics(
	IN	PADAPTER	Adapter
	)
{
	u32	ret_value;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PFALSE_ALARM_STATISTICS FalseAlmCnt = &(pdmpriv->FalseAlmCnt);
	u8	BBReset;
#ifdef CONFIG_CONCURRENT_MODE
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
	HAL_DATA_TYPE	*pbuddy_pHalData = GET_HAL_DATA(pbuddy_adapter);
	struct mlme_priv	*pbuddy_pmlmepriv = &(pbuddy_adapter->mlmepriv);
#endif //CONFIG_CONCURRENT_MODE
	//hold ofdm counter
	PHY_SetBBReg(Adapter, rOFDM0_LSTF, BIT31, 1); //hold page C counter
	PHY_SetBBReg(Adapter, rOFDM1_LSTF, BIT31, 1); //hold page D counter
	
	ret_value = PHY_QueryBBReg(Adapter, rOFDM0_FrameSync, bMaskDWord);
	FalseAlmCnt->Cnt_Fast_Fsync = (ret_value&0xffff);
	FalseAlmCnt->Cnt_SB_Search_fail = ((ret_value&0xffff0000)>>16);		
	ret_value = PHY_QueryBBReg(Adapter, rOFDM_PHYCounter1, bMaskDWord);
	FalseAlmCnt->Cnt_Parity_Fail = ((ret_value&0xffff0000)>>16);	
	ret_value = PHY_QueryBBReg(Adapter, rOFDM_PHYCounter2, bMaskDWord);
	FalseAlmCnt->Cnt_Rate_Illegal = (ret_value&0xffff);
	FalseAlmCnt->Cnt_Crc8_fail = ((ret_value&0xffff0000)>>16);
	ret_value = PHY_QueryBBReg(Adapter, rOFDM_PHYCounter3, bMaskDWord);
	FalseAlmCnt->Cnt_Mcs_fail = (ret_value&0xffff);

	FalseAlmCnt->Cnt_Ofdm_fail = 	FalseAlmCnt->Cnt_Parity_Fail + FalseAlmCnt->Cnt_Rate_Illegal +
								FalseAlmCnt->Cnt_Crc8_fail + FalseAlmCnt->Cnt_Mcs_fail +
								FalseAlmCnt->Cnt_Fast_Fsync + FalseAlmCnt->Cnt_SB_Search_fail;

	if(pHalData->CurrentBandType92D != BAND_ON_5G)
	{
		//hold cck counter
		//AcquireCCKAndRWPageAControl(Adapter);
		//PHY_SetBBReg(Adapter, rCCK0_FalseAlarmReport, BIT14, 1);

		ret_value = PHY_QueryBBReg(Adapter, rCCK0_FACounterLower, bMaskByte0);
		FalseAlmCnt->Cnt_Cck_fail = ret_value;

		ret_value = PHY_QueryBBReg(Adapter, rCCK0_FACounterUpper, bMaskByte3);
		FalseAlmCnt->Cnt_Cck_fail +=  (ret_value& 0xff)<<8;
		//ReleaseCCKAndRWPageAControl(Adapter);
	}
	else
	{
		FalseAlmCnt->Cnt_Cck_fail = 0;
	}


	FalseAlmCnt->Cnt_all = (	FalseAlmCnt->Cnt_Fast_Fsync + 
						FalseAlmCnt->Cnt_SB_Search_fail +
						FalseAlmCnt->Cnt_Parity_Fail +
						FalseAlmCnt->Cnt_Rate_Illegal +
						FalseAlmCnt->Cnt_Crc8_fail +
						FalseAlmCnt->Cnt_Mcs_fail +
						FalseAlmCnt->Cnt_Cck_fail);	
	Adapter->recvpriv.FalseAlmCnt_all = FalseAlmCnt->Cnt_all;
#ifdef CONFIG_CONCURRENT_MODE
	if(pbuddy_adapter)
		pbuddy_adapter->recvpriv.FalseAlmCnt_all = FalseAlmCnt->Cnt_all;
#endif //CONFIG_CONCURRENT_MODE

#if 0 //Just for debug
	if(pDM_DigTable->CurIGValue < 0x25)
		FalseAlmCnt->Cnt_all = 12000;
	else if(pDM_DigTable->CurIGValue < 0x2A)
		FalseAlmCnt->Cnt_all = 20;
	else if(pDM_DigTable->CurIGValue < 0x2D)
		FalseAlmCnt->Cnt_all = 0;
#endif

	//reset false alarm counter registers
	PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0x08000000, 1);
	PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0x08000000, 0);
	//update ofdm counter
	PHY_SetBBReg(Adapter, rOFDM0_LSTF, BIT31, 0); //update page C counter
	PHY_SetBBReg(Adapter, rOFDM1_LSTF, BIT31, 0); //update page D counter
	if(pHalData->CurrentBandType92D != BAND_ON_5G)
	{
		//reset cck counter
		//AcquireCCKAndRWPageAControl(Adapter);
		//RT_TRACE(COMP_INIT,DBG_LOUD,("Acquiere mutex in dm_falsealarmcount 111 \n"));
		PHY_SetBBReg(Adapter, rCCK0_FalseAlarmReport, 0x0000c000, 0);
		//enable cck counter
		PHY_SetBBReg(Adapter, rCCK0_FalseAlarmReport, 0x0000c000, 2);
		//ReleaseCCKAndRWPageAControl(Adapter);
		//RT_TRACE(COMP_INIT,DBG_LOUD,("Release mutex in dm_falsealarmcount 111 \n"));
	}

	//BB Reset
	if(IS_HARDWARE_TYPE_8192D(Adapter))
	{
		if(pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
		{
			if((pHalData->CurrentBandType92D == BAND_ON_2_4G) && pHalData->bMasterOfDMSP && (check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE))
			{
				//before BB reset should do clock gated
				rtw_write32(Adapter, rFPGA0_XCD_RFParameter, rtw_read32(Adapter, rFPGA0_XCD_RFParameter)|(BIT31));
				BBReset = rtw_read8(Adapter, 0x02);
				rtw_write8(Adapter, 0x02, BBReset&(~BIT0));
				rtw_write8(Adapter, 0x02, BBReset|BIT0);
				//undo clock gated
				rtw_write32(Adapter, rFPGA0_XCD_RFParameter, rtw_read32(Adapter, rFPGA0_XCD_RFParameter)&(~BIT31));
			}
		}
		else
		{
			if((pHalData->CurrentBandType92D == BAND_ON_2_4G) &&(check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE)
#ifdef CONFIG_CONCURRENT_MODE
				 && (check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == _FALSE)
#endif //CONFIG_CONCURRENT_MODE
			)
			{
				//before BB reset should do clock gated
				rtw_write32(Adapter, rFPGA0_XCD_RFParameter, rtw_read32(Adapter, rFPGA0_XCD_RFParameter)|(BIT31));
				BBReset = rtw_read8(Adapter, 0x02);
				rtw_write8(Adapter, 0x02, BBReset&(~BIT0));
				rtw_write8(Adapter, 0x02, BBReset|BIT0);
				//undo clock gated
				rtw_write32(Adapter, rFPGA0_XCD_RFParameter, rtw_read32(Adapter, rFPGA0_XCD_RFParameter)&(~BIT31));
			}
		}
	}
	else if(check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE)
	{
		//before BB reset should do clock gated
		rtw_write32(Adapter, rFPGA0_XCD_RFParameter, rtw_read32(Adapter, rFPGA0_XCD_RFParameter)|(BIT31));
		BBReset = rtw_read8(Adapter, 0x02);
		rtw_write8(Adapter, 0x02, BBReset&(~BIT0));
		rtw_write8(Adapter, 0x02, BBReset|BIT0);
		//undo clock gated
		rtw_write32(Adapter, rFPGA0_XCD_RFParameter, rtw_read32(Adapter, rFPGA0_XCD_RFParameter)&(~BIT31));
	}
}

static VOID
odm_FindMinimumRSSI_Dmsp(
	IN	PADAPTER	pAdapter
)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	s32	Rssi_val_min_back_for_mac0;
	BOOLEAN		bGetValueFromBuddyAdapter = dm_DualMacGetParameterFromBuddyAdapter(pAdapter);
	BOOLEAN		bRestoreRssi = _FALSE;
	PADAPTER	BuddyAdapter = pAdapter->pbuddy_adapter;
	struct dm_priv	*Buddydmpriv;

	if(pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
	{
		if(BuddyAdapter!= NULL)
		{
			Buddydmpriv = &GET_HAL_DATA(BuddyAdapter)->dmpriv;
			if(pHalData->bSlaveOfDMSP)
			{
				//DBG_871X("bSlavecase of dmsp\n");
				Buddydmpriv->RssiValMinForAnotherMacOfDMSP = pdmpriv->MinUndecoratedPWDBForDM;
			}
			else
			{
				if(bGetValueFromBuddyAdapter)
				{
					//DBG_871X("get new RSSI\n");
					bRestoreRssi = _TRUE;
					Rssi_val_min_back_for_mac0 = pdmpriv->MinUndecoratedPWDBForDM;
					pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->RssiValMinForAnotherMacOfDMSP;
				}
			}
		}
		
	}

	if(bRestoreRssi)
	{
		bRestoreRssi = _FALSE;
		pdmpriv->MinUndecoratedPWDBForDM = Rssi_val_min_back_for_mac0;
	}
#endif
}

static void
odm_FindMinimumRSSI_92D(
IN	PADAPTER	pAdapter
	)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_priv	*pmlmepriv = &pAdapter->mlmepriv;

	//1 1.Determine the minimum RSSI 
	if((check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE) &&
		(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		pdmpriv->MinUndecoratedPWDBForDM = 0;
		//RT_TRACE(COMP_BB_POWERSAVING, DBG_LOUD, ("Not connected to any \n"));
	}
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)	// Default port
	{
		if((check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE))
		{
			pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
			//RT_TRACE(COMP_BB_POWERSAVING, DBG_LOUD, ("AP Client PWDB = 0x%x \n", pHalData->MinUndecoratedPWDBForDM));
		}
		else
		{
			pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->UndecoratedSmoothedPWDB;
			//RT_TRACE(COMP_BB_POWERSAVING, DBG_LOUD, ("STA Default Port PWDB = 0x%x \n", pHalData->MinUndecoratedPWDBForDM));
		}
	}
	else // associated entry pwdb
	{	
		pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
		//RT_TRACE(COMP_BB_POWERSAVING, DBG_LOUD, ("AP Ext Port or disconnet PWDB = 0x%x \n", pHalData->MinUndecoratedPWDBForDM));
	}

	odm_FindMinimumRSSI_Dmsp(pAdapter);

	//RT_TRACE(COMP_DIG, DBG_LOUD, ("MinUndecoratedPWDBForDM =%d\n",pHalData->MinUndecoratedPWDBForDM));
}

static u8
odm_initial_gain_MinPWDB(
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	s32	Rssi_val_min = 0;
#if 0
	pDIG_T	pDM_DigTable = &pdmpriv->DM_DigTable;

	if(	(pDM_DigTable->CurMultiSTAConnectState == DIG_MultiSTA_CONNECT) &&
		(pDM_DigTable->CurSTAConnectState == DIG_STA_CONNECT) )
	{
		if(pHalData->EntryMinUndecoratedSmoothedPWDB != 0)
			Rssi_val_min  =  (pdmpriv->EntryMinUndecoratedSmoothedPWDB > pdmpriv->UndecoratedSmoothedPWDB)?
					pdmpriv->UndecoratedSmoothedPWDB:pdmpriv->EntryMinUndecoratedSmoothedPWDB;		
		else
			Rssi_val_min = pdmpriv->UndecoratedSmoothedPWDB;
	}
	else if(	pDM_DigTable->CurSTAConnectState == DIG_STA_CONNECT || 
			pDM_DigTable->CurSTAConnectState == DIG_STA_BEFORE_CONNECT) 
		Rssi_val_min = pdmpriv->UndecoratedSmoothedPWDB;
	else if(pDM_DigTable->CurMultiSTAConnectState == DIG_MultiSTA_CONNECT)
		Rssi_val_min = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
#endif
	if(pdmpriv->EntryMinUndecoratedSmoothedPWDB != 0)
		Rssi_val_min  =  (pdmpriv->EntryMinUndecoratedSmoothedPWDB > pdmpriv->UndecoratedSmoothedPWDB)?
					pdmpriv->UndecoratedSmoothedPWDB:pdmpriv->EntryMinUndecoratedSmoothedPWDB;
	else
		Rssi_val_min = pdmpriv->UndecoratedSmoothedPWDB;

	return (u8)Rssi_val_min;
}

static VOID
DM_Write_DIG_DMSP(
	IN	PADAPTER	pAdapter
	)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	pDIG_T		pDM_DigTable = &pdmpriv->DM_DigTable;
	PADAPTER	BuddyAdapter = pAdapter->pbuddy_adapter;
	BOOLEAN		bGetValueFromOtherMac = dm_DualMacGetParameterFromBuddyAdapter(pAdapter);
	struct dm_priv	*Buddydmpriv;

	//DBG_871X(("CurIGValue = 0x%x, PreIGValue = 0x%x\n", pDM_DigTable->CurIGValue, pDM_DigTable->PreIGValue);
	
	if(BuddyAdapter == NULL)
	{	
		//DBG_871X("DM_Write_DIG_DMSP(): not find buddyAdapter\n");
		if(pHalData->bMasterOfDMSP)
		{
			PHY_SetBBReg(pAdapter, rOFDM0_XAAGCCore1, 0x7f, pDM_DigTable->CurIGValue);
			PHY_SetBBReg(pAdapter, rOFDM0_XBAGCCore1, 0x7f, pDM_DigTable->CurIGValue);
			pDM_DigTable->PreIGValue = pDM_DigTable->CurIGValue;
		}
		else
		{
			pDM_DigTable->PreIGValue = pDM_DigTable->CurIGValue;
		}
		return;
	}

	//DBG_871X("bGetValueFromOtherMac %d \n",bGetValueFromOtherMac);
	if(bGetValueFromOtherMac)
	{
		//DBG_871X("DM_Write_DIG_DMSP(): mac 0 set mac 1 value \n");
		if(pdmpriv->bWriteDigForAnotherMacOfDMSP)
		{
			pdmpriv->bWriteDigForAnotherMacOfDMSP = _FALSE;
			PHY_SetBBReg(pAdapter, rOFDM0_XAAGCCore1, 0x7f, pdmpriv->CurDigValueForAnotherMacOfDMSP);
			PHY_SetBBReg(pAdapter, rOFDM0_XBAGCCore1, 0x7f, pdmpriv->CurDigValueForAnotherMacOfDMSP);
		}
	}

	Buddydmpriv = &GET_HAL_DATA(BuddyAdapter)->dmpriv;

	if(pDM_DigTable->PreIGValue != pDM_DigTable->CurIGValue)
	{
		// Set initial gain.
		// 20100211 Joseph: Set only BIT0~BIT6 for DIG. BIT7 is the function switch of Antenna diversity.
		// Just not to modified it for SD3 testing.
		//PHY_SetBBReg(pAdapter, rOFDM0_XAAGCCore1, bMaskByte0, pDM_DigTable->CurIGValue);
		//PHY_SetBBReg(pAdapter, rOFDM0_XBAGCCore1, bMaskByte0, pDM_DigTable->CurIGValue);
		 if(pHalData->bSlaveOfDMSP)
		 {
		 	//DBG_871X("DM_Write_DIG_DMSP(): slave case \n");
		 	Buddydmpriv->bWriteDigForAnotherMacOfDMSP = _TRUE;
			Buddydmpriv->CurDigValueForAnotherMacOfDMSP =  pDM_DigTable->CurIGValue; 
		 }
		else
		{
			//DBG_871X("DM_Write_DIG_DMSP(): master case \n");
			if(!bGetValueFromOtherMac)
			{
				//DBG_871X("DM_Write_DIG_DMSP(): mac 0 set mac 0 value \n");
				PHY_SetBBReg(pAdapter, rOFDM0_XAAGCCore1, 0x7f, pDM_DigTable->CurIGValue);
				PHY_SetBBReg(pAdapter, rOFDM0_XBAGCCore1, 0x7f, pDM_DigTable->CurIGValue);
			}
		}
		pDM_DigTable->PreIGValue = pDM_DigTable->CurIGValue;
	}
#endif
}

static VOID
DM_Write_DIG(
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	pDIG_T	pDM_DigTable = &pdmpriv->DM_DigTable;

	//RT_TRACE(	COMP_DIG, DBG_LOUD, ("CurIGValue = 0x%x, PreIGValue = 0x%x, BackoffVal = %d\n", 
	//			pDM_DigTable->CurIGValue, pDM_DigTable->PreIGValue, pDM_DigTable->BackoffVal));
	//DBG_871X("CurIGValue = 0x%x, PreIGValue = 0x%x\n", pDM_DigTable->CurIGValue, pDM_DigTable->PreIGValue);
	
	if (pDM_DigTable->Dig_Enable_Flag == _FALSE)
	{
		//DBG_871X("DIG is disabled\n");
		//pDM_DigTable->PreIGValue = 0x17;
		return;
	}
	
	if( (pDM_DigTable->PreIGValue != pDM_DigTable->CurIGValue) || ( pAdapter->bForceWriteInitGain ) )
	{
		// Set initial gain.
		// 20100211 Joseph: Set only BIT0~BIT6 for DIG. BIT7 is the function switch of Antenna diversity.
		// Just not to modified it for SD3 testing.
		//PHY_SetBBReg(pAdapter, rOFDM0_XAAGCCore1, bMaskByte0, DM_DigTable.CurIGValue);
		//PHY_SetBBReg(pAdapter, rOFDM0_XBAGCCore1, bMaskByte0, DM_DigTable.CurIGValue);
#if defined CONFIG_WIDI_DIG_3E && defined CONFIG_INTEL_WIDI
		if( pAdapter->mlmepriv.widi_enable == _TRUE )
		{
			PHY_SetBBReg(pAdapter, rOFDM0_XAAGCCore1, 0x7f, 0x3e);
			PHY_SetBBReg(pAdapter, rOFDM0_XBAGCCore1, 0x7f, 0x3e);
		}
		else
#endif //defined CONFIG_WIDI_DIG_3E && defined CONFIG_INTEL_WIDI
		PHY_SetBBReg(pAdapter, rOFDM0_XAAGCCore1, 0x7f, pDM_DigTable->CurIGValue);
		PHY_SetBBReg(pAdapter, rOFDM0_XBAGCCore1, 0x7f, pDM_DigTable->CurIGValue);
		if(pDM_DigTable->CurIGValue != 0x17)
			pDM_DigTable->PreIGValue = pDM_DigTable->CurIGValue;
	}
}

static void odm_DIG(
	IN	PADAPTER	pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct mlme_priv	*pmlmepriv = &(pAdapter->mlmepriv);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DM_ODM_T *odm = adapter_to_odm(pAdapter);
	struct registry_priv	 *pregistrypriv = &pAdapter->registrypriv;
	PFALSE_ALARM_STATISTICS FalseAlmCnt = &(pdmpriv->FalseAlmCnt);
	pDIG_T	pDM_DigTable = &pdmpriv->DM_DigTable;
	static u8	DIG_Dynamic_MIN_0 = 0x25;
	static u8	DIG_Dynamic_MIN_1 = 0x25;
	u8	DIG_Dynamic_MIN;
	static BOOLEAN	bMediaConnect_0 = _FALSE;
	static BOOLEAN	bMediaConnect_1 = _FALSE;
	BOOLEAN		FirstConnect;
	u8	TxRate = rtw_read8(pAdapter, REG_INIDATA_RATE_SEL);
#ifdef CONFIG_CONCURRENT_MODE
	PADAPTER pbuddy_adapter = pAdapter->pbuddy_adapter;
	HAL_DATA_TYPE	*pbuddy_pHalData = GET_HAL_DATA(pbuddy_adapter);
	struct mlme_priv	*pbuddy_pmlmepriv = &(pbuddy_adapter->mlmepriv);
	struct dm_priv	*pbuddy_pdmpriv = &pbuddy_pHalData->dmpriv;
#endif //CONFIG_CONCURRENT_MODE

	//RT_TRACE(COMP_DIG, DBG_LOUD, ("odm_DIG() ==>\n"));
	
	if(IS_HARDWARE_TYPE_8192D(pAdapter))
	{
		if(pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
		{
			if(pHalData->bMasterOfDMSP)
			{
				DIG_Dynamic_MIN = DIG_Dynamic_MIN_0;
				FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) && (bMediaConnect_0 == _FALSE);
				//DBG_871X("bMediaConnect_0=%d,  pMgntInfo->bMediaConnect=%d\n", bMediaConnect_0, check_fwstate(pmlmepriv, _FW_LINKED));
			}
			else
			{
				DIG_Dynamic_MIN = DIG_Dynamic_MIN_1;
				FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) && (bMediaConnect_1 == _FALSE);
				//DBG_871X("bMediaConnect_1=%d,  pMgntInfo->bMediaConnect=%d\n", bMediaConnect_1, check_fwstate(pmlmepriv, _FW_LINKED));
			}
			//DBG_871X("pHalData->CurrentBandType92D = %s\n",(pHalData->CurrentBandType92D==BAND_ON_2_4G)?"2.4G":"5G");
		}
		else
		{
			if(pHalData->CurrentBandType92D==BAND_ON_5G)
			{
				DIG_Dynamic_MIN = DIG_Dynamic_MIN_0;
#ifdef CONFIG_CONCURRENT_MODE
				FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE ||check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == _TRUE
				) && (bMediaConnect_0 == _FALSE);
#else
				FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) && (bMediaConnect_0 == _FALSE);
#endif //CONFIG_CONCURRENT_MODE
				//DBG_871X("bMediaConnect_5G=%d,  pMgntInfo->bMediaConnect=%d\n", bMediaConnect_0, check_fwstate(pmlmepriv, _FW_LINKED));
			}
			else
			{
				DIG_Dynamic_MIN = DIG_Dynamic_MIN_1;
#ifdef CONFIG_CONCURRENT_MODE
				FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == _TRUE
				) && (bMediaConnect_1 == _FALSE);
#else
				FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) && (bMediaConnect_1 == _FALSE);
#endif //CONFIG_CONCURRENT_MODE
				//DBG_871X("bMediaConnect_2.4G=%d,  pMgntInfo->bMediaConnect=%d\n", bMediaConnect_1, check_fwstate(pmlmepriv, _FW_LINKED));
			}
		}
	}
	else
	{	
		DIG_Dynamic_MIN = DIG_Dynamic_MIN_0;
#ifdef CONFIG_CONCURRENT_MODE
		FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == _TRUE
		) && (bMediaConnect_0 == _FALSE);
#else
		FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) && (bMediaConnect_0 == _FALSE);
#endif //CONFIG_CONCURRENT_MODE
		//DBG_871X("bMediaConnect=%d,  pMgntInfo->bMediaConnect=%d\n", bMediaConnect_0, check_fwstate(pmlmepriv, _FW_LINKED));
	}

	//DBG_871X("Cnt_Parity_Fail = %d, Cnt_Rate_Illegal = %d, Cnt_Crc8_fail = %d, Cnt_Mcs_fail = %d\n", 
	//			FalseAlmCnt->Cnt_Parity_Fail, FalseAlmCnt->Cnt_Rate_Illegal, FalseAlmCnt->Cnt_Crc8_fail, FalseAlmCnt->Cnt_Mcs_fail);
	//DBG_871X("Cnt_Fast_Fsync = %d, Cnt_SB_Search_fail = %d\n", 
	//			FalseAlmCnt->Cnt_Fast_Fsync, FalseAlmCnt->Cnt_SB_Search_fail);
	//DBG_871X("Cnt_Ofdm_fail = %d, Cnt_Cck_fail = %d, Cnt_all = %d\n", 
	//			FalseAlmCnt->Cnt_Ofdm_fail, FalseAlmCnt->Cnt_Cck_fail, FalseAlmCnt->Cnt_all);
	//DBG_871X("RSSI_A=%d, RSSI_B=%d, RSSI_Ave=%d, RSSI_CCK=%d\n",
	//	pAdapter->RxStats.RxRSSIPercentage[0], pAdapter->RxStats.RxRSSIPercentage[1], pdmpriv->UndecoratedSmoothedPWDB,pdmpriv->UndecoratedSmoothedCCK);
	//DBG_871X("RX Rate =  0x%x, TX Rate = 0x%x \n", pHalData->RxRate, TxRate);

#ifndef CONFIG_CONCURRENT_MODE
	if(pdmpriv->bDMInitialGainEnable == _FALSE)
		return;
#endif //CONFIG_CONCURRENT_MODE
	
	//if (pDM_DigTable->Dig_Enable_Flag == FALSE)
		//return;
#ifdef CONFIG_CONCURRENT_MODE
	if(!(pdmpriv->DMFlag & DYNAMIC_FUNC_DIG) || !(pbuddy_pdmpriv->DMFlag & DYNAMIC_FUNC_DIG))
#else
	if(!(pdmpriv->DMFlag & DYNAMIC_FUNC_DIG))
#endif //CONFIG_CONCURRENT_MODE
		return;

#ifdef CONFIG_CONCURRENT_MODE
	if (pAdapter->mlmeextpriv.sitesurvey_res.state == SCAN_PROCESS || pbuddy_adapter->mlmeextpriv.sitesurvey_res.state == SCAN_PROCESS)
#else
	if (pAdapter->mlmeextpriv.sitesurvey_res.state == SCAN_PROCESS)
#endif //CONFIG_CONCURRENT_MODE
		return;

	//RTPRINT(FDM, DM_Monitor, ("odm_DIG() progress \n"));

	//odm_CtrlInitGainByTwoPort(pAdapter);
	//RTPRINT(FDM, DM_Monitor, ("odm_DIG() <==\n"));
	
	//1 Boundary Decision
#ifdef CONFIG_CONCURRENT_MODE
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == _TRUE)
#else
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
#endif //CONFIG_CONCURRENT_MODE
	{
#ifdef CONFIG_PLATFORM_RTK_DMP
		pDM_DigTable->rx_gain_range_max = DM_DIG_MAX;
#else
		//2 Get minimum RSSI value among associated devices
		pDM_DigTable->Rssi_val_min = odm_initial_gain_MinPWDB(pAdapter);
		//DBG_871X("pDM_DigTable->Rssi_val_min = 0x%x\n", pDM_DigTable->Rssi_val_min);

		//2 Modify DIG upper bound
		if((pDM_DigTable->Rssi_val_min + 20) > DM_DIG_MAX )
			pDM_DigTable->rx_gain_range_max = DM_DIG_MAX;
		else
			pDM_DigTable->rx_gain_range_max = pDM_DigTable->Rssi_val_min + 20;
		//2 Modify DIG lower bound
		if((FalseAlmCnt->Cnt_all > 500)&&(DIG_Dynamic_MIN < 0x25))
			DIG_Dynamic_MIN++;
		if((FalseAlmCnt->Cnt_all < 500)&&(DIG_Dynamic_MIN > DM_DIG_MIN))
			DIG_Dynamic_MIN--;
		if((pDM_DigTable->Rssi_val_min < 8) && (DIG_Dynamic_MIN > DM_DIG_MIN))
			DIG_Dynamic_MIN--;
#endif //CONFIG_PLATFORM_RTK_DMP
	}
	else
	{
		pDM_DigTable->rx_gain_range_max = DM_DIG_MAX;
		DIG_Dynamic_MIN = DM_DIG_MIN;
	}
#ifdef CONFIG_PLATFORM_RTK_DMP
	{
		u8 RSSI_tmp = odm_initial_gain_MinPWDB(pAdapter);
		if(RSSI_tmp <= DM_DIG_MIN)
			pDM_DigTable->rx_gain_range_min = DM_DIG_MIN;
		else if(RSSI_tmp >= DM_DIG_MAX)
			pDM_DigTable->rx_gain_range_min = DM_DIG_MAX;
		else
			pDM_DigTable->rx_gain_range_min = RSSI_tmp;
	}
#else //CONFIG_PLATFORM_RTK_DMP

	//1 Modify DIG lower bound, deal with abnorally large false alarm
	if(FalseAlmCnt->Cnt_all > 10000)
	{
		//DBG_871X("dm_DIG(): Abnornally false alarm case. \n");

		pDM_DigTable->LargeFAHit++;
		if(pDM_DigTable->ForbiddenIGI < pDM_DigTable->CurIGValue)
		{
			pDM_DigTable->ForbiddenIGI = pDM_DigTable->CurIGValue;
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
			if(pDM_DigTable->LargeFAHit == 0 )
			{
				if((pDM_DigTable->ForbiddenIGI -1) < DIG_Dynamic_MIN) //DM_DIG_MIN)
				{
					pDM_DigTable->ForbiddenIGI = DIG_Dynamic_MIN; //DM_DIG_MIN;
					pDM_DigTable->rx_gain_range_min = DIG_Dynamic_MIN; //DM_DIG_MIN;
				}
				else
				{
					pDM_DigTable->ForbiddenIGI --;
					pDM_DigTable->rx_gain_range_min = (pDM_DigTable->ForbiddenIGI + 1);
				}
			}
			else if(pDM_DigTable->LargeFAHit == 3 )
			{
				pDM_DigTable->LargeFAHit = 0;
			}
		}

	}
#endif //CONFIG_PLATFORM_RTK_DMP
	//DBG_871X("DM_DigTable.ForbiddenIGI = 0x%x, DM_DigTable.LargeFAHit = 0x%x\n",
		//pDM_DigTable->ForbiddenIGI, pDM_DigTable->LargeFAHit);
	//DBG_871X("DM_DigTable.rx_gain_range_max = 0x%x, DM_DigTable.rx_gain_range_min = 0x%x\n",
		//pDM_DigTable->rx_gain_range_max, pDM_DigTable->rx_gain_range_min);

	//1 Adjust initial gain by false alarm
#ifdef CONFIG_CONCURRENT_MODE
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == _TRUE)
#else
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
#endif //CONFIG_CONCURRENT_MODE
	{
		if(FirstConnect)
		{
			pDM_DigTable->CurIGValue = pDM_DigTable->Rssi_val_min;
			//DBG_871X("DIG: First Connect\n");
		}
		else
		{
			if(IS_HARDWARE_TYPE_8192D(pAdapter))
			{
				if(FalseAlmCnt->Cnt_all > DM_DIG_FA_TH2_92D)
					pDM_DigTable->CurIGValue = pDM_DigTable->PreIGValue+2;
				else if (FalseAlmCnt->Cnt_all > DM_DIG_FA_TH1_92D)
					pDM_DigTable->CurIGValue = pDM_DigTable->PreIGValue+1;
				else if(FalseAlmCnt->Cnt_all < DM_DIG_FA_TH0_92D)
					pDM_DigTable->CurIGValue =pDM_DigTable->PreIGValue-1;	
			}
			else
			{
				if(FalseAlmCnt->Cnt_all > DM_DIG_FA_TH2)
					pDM_DigTable->CurIGValue = pDM_DigTable->PreIGValue+2;
				else if (FalseAlmCnt->Cnt_all > DM_DIG_FA_TH1)
					pDM_DigTable->CurIGValue = pDM_DigTable->PreIGValue+1;
				else if(FalseAlmCnt->Cnt_all < DM_DIG_FA_TH0)
					pDM_DigTable->CurIGValue =pDM_DigTable->PreIGValue-1;	
			}
		}
	}	
	else
	{
		//	There is no network interface connects to AP.
		if ( 0 == pDM_DigTable->rx_gain_range_min_nolink )
		{
			//	First time to enter odm_DIG function and set the default value to rx_gain_range_min_nolink
			pDM_DigTable->rx_gain_range_min_nolink = 0x30;
		}
		else
		{
			if ( ( FalseAlmCnt->Cnt_all > 1000 ) && ( FalseAlmCnt->Cnt_all < 2000 ) )
			{
				pDM_DigTable->rx_gain_range_min_nolink = ( ( pDM_DigTable->rx_gain_range_min_nolink + 1 ) > 0x3e ) ?
							0x3e : ( pDM_DigTable->rx_gain_range_min_nolink + 1 ) ;
			}
			else if ( FalseAlmCnt->Cnt_all >= 2000 )
			{
				pDM_DigTable->rx_gain_range_min_nolink = ( ( pDM_DigTable->rx_gain_range_min_nolink + 2 ) > 0x3e ) ?
							0x3e : ( pDM_DigTable->rx_gain_range_min_nolink + 2 ) ;
			}
			else if ( FalseAlmCnt->Cnt_all < 500 )
			{
				pDM_DigTable->rx_gain_range_min_nolink = ( ( pDM_DigTable->rx_gain_range_min_nolink - 1 ) < 0x1e ) ?
							0x1e : ( pDM_DigTable->rx_gain_range_min_nolink - 1 ) ;
			}			
		}
		
		pDM_DigTable->CurIGValue = pDM_DigTable->rx_gain_range_min_nolink;
	}
	//1 Check initial gain by upper/lower bound
	if(pDM_DigTable->CurIGValue > pDM_DigTable->rx_gain_range_max)
		pDM_DigTable->CurIGValue = pDM_DigTable->rx_gain_range_max;
	
	if(pDM_DigTable->CurIGValue < pDM_DigTable->rx_gain_range_min)
		pDM_DigTable->CurIGValue = pDM_DigTable->rx_gain_range_min;

#ifdef CONFIG_ODM_ADAPTIVITY
	if((pdmpriv->DMFlag & DYNAMIC_FUNC_ADAPTIVITY) && odm->adaptivity_flag == _TRUE)
	{
		u8 Adap_IGI_Upper = odm->Adaptivity_IGI_upper;

		if(pDM_DigTable->CurIGValue > Adap_IGI_Upper)
			pDM_DigTable->CurIGValue = Adap_IGI_Upper;

		if(odm->IGI_LowerBound != 0)
		{
			if(pDM_DigTable->CurIGValue < odm->IGI_LowerBound)
				pDM_DigTable->CurIGValue = odm->IGI_LowerBound;
		}
		LOG_LEVEL(_drv_info_, FUNC_ADPT_FMT": odm->IGI_LowerBound = %d\n",
			FUNC_ADPT_ARG(pAdapter), odm->IGI_LowerBound);
	}
#endif /* CONFIG_ODM_ADAPTIVITY */

	if ( pAdapter->bRxRSSIDisplay )
	{
		DBG_871X("Modify DIG algorithm for DMP DIG: RxGainMin = %X, RxGainMax = %X\n",
			pDM_DigTable->rx_gain_range_min,
			pDM_DigTable->rx_gain_range_max );
	}

	if(IS_HARDWARE_TYPE_8192D(pAdapter))
	{
		//sherry  delete DualMacSmartConncurrent 20110517
		if(pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
		{
			DM_Write_DIG_DMSP(pAdapter);
			if(pHalData->bMasterOfDMSP)
			{
				bMediaConnect_0 = check_fwstate(pmlmepriv, _FW_LINKED);
				DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
			}
			else
			{
				bMediaConnect_1 = check_fwstate(pmlmepriv, _FW_LINKED);
				DIG_Dynamic_MIN_1 = DIG_Dynamic_MIN;
			}
		}
		else
		{
			//printk("%s CurIGValue(0x%02x)  FalseAlmCnt_all(%d) <====\n",__FUNCTION__,pDM_DigTable->CurIGValue, FalseAlmCnt->Cnt_all  );	
			DM_Write_DIG(pAdapter);
			if(pHalData->CurrentBandType92D==BAND_ON_5G)
			{
#ifdef CONFIG_CONCURRENT_MODE
				bMediaConnect_0 = (check_fwstate(pmlmepriv, _FW_LINKED) || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED));
#else
				bMediaConnect_0 = check_fwstate(pmlmepriv, _FW_LINKED);
#endif //CONFIG_CONCURRENT_MODE
				DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
			}
			else
			{
#ifdef CONFIG_CONCURRENT_MODE
				bMediaConnect_1 = (check_fwstate(pmlmepriv, _FW_LINKED) || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED));
#else
				bMediaConnect_1 = check_fwstate(pmlmepriv, _FW_LINKED);
#endif //CONFIG_CONCURRENT_MODE
				DIG_Dynamic_MIN_1 = DIG_Dynamic_MIN;
			}
		}
	}
	else
	{
		DM_Write_DIG(pAdapter);
#ifdef CONFIG_CONCURRENT_MODE
		bMediaConnect_0 = (check_fwstate(pmlmepriv, _FW_LINKED) || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED));
#else
		bMediaConnect_0 = check_fwstate(pmlmepriv, _FW_LINKED);
#endif //CONFIG_CONCURRENT_MODE
		DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
	}

	if((pregistrypriv->lowrate_two_xmit) && IS_HARDWARE_TYPE_8192D(pAdapter) &&
		(pHalData->MacPhyMode92D != DUALMAC_DUALPHY) && (!pregistrypriv->special_rf_path))	
	{
		//for Use 2 path Tx to transmit MCS0~7 and legacy mode
#ifdef CONFIG_CONCURRENT_MODE
		if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == _TRUE)
#else
		if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
#endif //CONFIG_CONCURRENT_MODE
		{
			if(pDM_DigTable->Rssi_val_min  <= 30)   //low rate 2T2R settings
			{
				//Reg90C=0x83321333 (OFDM 2T)
  				//RegA07=0xc1            (CCK 2T2R)
 				//RegA11=0x9b            (no switch polarity of two antenna)
 				//RegA20=0x10            (extend CS ratio as X1.125)
  				//RegA2E=0xdf             (MRC on)
  				//RegA2F=0x10            (CDD 90ns for path-B)
  				//RegA75=0x01            (antenna selection enable)
  				
				rtw_write32(pAdapter, 0x90C, 0x83321333);
				rtw_write8(pAdapter, 0xA07, 0xc1);
				rtw_write8(pAdapter, 0xA11, 0x9b);
				rtw_write8(pAdapter, 0xA20, 0x10);
				rtw_write8(pAdapter, 0xA2E, 0xdf);
				rtw_write8(pAdapter, 0xA2F, 0x10);
				rtw_write8(pAdapter, 0xA75, 0x01);				
  				
			}
			else if(pDM_DigTable->Rssi_val_min  >= 35)  //low rate 1T1R Settings
			{
  				//Reg90C=0x81121313 
  				//RegA07=0x80                   
  				//RegA11=0xbb            
  				//RegA20=0x00            
  				//RegA2E=0xd3   
  				//RegA2F=0x00          
  				//RegA75=0x00
  				
  				rtw_write32(pAdapter, 0x90C, 0x81121313);
				rtw_write8(pAdapter, 0xA07, 0x80);
				rtw_write8(pAdapter, 0xA11, 0xbb);
				rtw_write8(pAdapter, 0xA20, 0x00);
				rtw_write8(pAdapter, 0xA2E, 0xd3);
				rtw_write8(pAdapter, 0xA2F, 0x00);
				rtw_write8(pAdapter, 0xA75, 0x00);
			}
			
		}
		
	}	
	

	//RT_TRACE(COMP_DIG, DBG_LOUD, ("odm_DIG() <==\n"));
	
}

static u8
dm_initial_gain_MinPWDB(
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	s32	Rssi_val_min = 0;
	pDIG_T		pDM_DigTable = &pdmpriv->DM_DigTable;

	
	if(	(pDM_DigTable->CurMultiSTAConnectState == DIG_MultiSTA_CONNECT) &&
		(pDM_DigTable->CurSTAConnectState == DIG_STA_CONNECT) )
	{
		if(pdmpriv->EntryMinUndecoratedSmoothedPWDB != 0)
			Rssi_val_min  =  (pdmpriv->EntryMinUndecoratedSmoothedPWDB > pdmpriv->UndecoratedSmoothedPWDB)?
					pdmpriv->UndecoratedSmoothedPWDB:pdmpriv->EntryMinUndecoratedSmoothedPWDB;		
		else
			Rssi_val_min = pdmpriv->UndecoratedSmoothedPWDB;
	}
	else if(	pDM_DigTable->CurSTAConnectState == DIG_STA_CONNECT || 
			pDM_DigTable->CurSTAConnectState == DIG_STA_BEFORE_CONNECT) 
		Rssi_val_min = pdmpriv->UndecoratedSmoothedPWDB;
	else if(pDM_DigTable->CurMultiSTAConnectState == DIG_MultiSTA_CONNECT)
		Rssi_val_min = pdmpriv->EntryMinUndecoratedSmoothedPWDB;

	return (u8)Rssi_val_min;
}

static void dm_CCK_PacketDetectionThresh_DMSP(
	IN	PADAPTER	pAdapter)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	pDIG_T		pDM_DigTable = &pdmpriv->DM_DigTable;
	PADAPTER	BuddyAdapter = pAdapter->pbuddy_adapter;
	BOOLEAN		bGetValueFromBuddyAdapter = dm_DualMacGetParameterFromBuddyAdapter(pAdapter);
	struct dm_priv	*Buddydmpriv;


	//if (pAdapter->DualMacSmartConcurrent == _FALSE)
	//	return;

	if(pDM_DigTable->CurSTAConnectState == DIG_STA_CONNECT)
	{
		pDM_DigTable->Rssi_val_min = dm_initial_gain_MinPWDB(pAdapter);
		if(pDM_DigTable->PreCCKPDState == CCK_PD_STAGE_LowRssi)
		{
			if(pDM_DigTable->Rssi_val_min <= 25)
				pDM_DigTable->CurCCKPDState = CCK_PD_STAGE_LowRssi;
			else
				pDM_DigTable->CurCCKPDState = CCK_PD_STAGE_HighRssi;
		}
		else{
			if(pDM_DigTable->Rssi_val_min <= 20)
				pDM_DigTable->CurCCKPDState = CCK_PD_STAGE_LowRssi;
			else
				pDM_DigTable->CurCCKPDState = CCK_PD_STAGE_HighRssi;
		}
	}
	else
		pDM_DigTable->CurCCKPDState=CCK_PD_STAGE_MAX;

	if(bGetValueFromBuddyAdapter)
	{
		DBG_871X("dm_CCK_PacketDetectionThresh_DMSP(): mac 1 connect,mac 0 disconnect case  \n");
		if(pdmpriv->bChangeCCKPDStateForAnotherMacOfDMSP)
		{
			DBG_871X("dm_CCK_PacketDetectionThresh_DMSP(): mac 0 set for mac1 \n");
			if(pdmpriv->CurCCKPDStateForAnotherMacOfDMSP == CCK_PD_STAGE_LowRssi)
			{
				//AcquireCCKAndRWPageAControl(pAdapter);
				//RT_TRACE(COMP_INIT,DBG_LOUD,("Acquiere mutex in dm_cck_packetdetection \n"));
				PHY_SetBBReg(pAdapter, rCCK0_CCA, bMaskByte2, 0x83);
				//RT_TRACE(COMP_INIT,DBG_LOUD,("Release mutex in dm_cck_packetdetection \n"));
				//ReleaseCCKAndRWPageAControl(pAdapter);
				//PHY_SetBBReg(pAdapter, rCCK0_System, bMaskByte1, 0x40);
				//if(IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID))
					//PHY_SetBBReg(pAdapter, rCCK0_FalseAlarmReport , bMaskByte2, 0xd7);
			}
			else
			{
				//AcquireCCKAndRWPageAControl(pAdapter);
				//RT_TRACE(COMP_INIT,DBG_LOUD,("Acquiere mutex in dm_cck_packetdetection \n"));
				PHY_SetBBReg(pAdapter, rCCK0_CCA, bMaskByte2, 0xcd);
				//ReleaseCCKAndRWPageAControl(pAdapter);
				//RT_TRACE(COMP_INIT,DBG_LOUD,("Release mutex in dm_cck_packetdetection \n"));

				//PHY_SetBBReg(pAdapter,rCCK0_System, bMaskByte1, 0x47);
				//if(IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID))
					//PHY_SetBBReg(pAdapter, rCCK0_FalseAlarmReport , bMaskByte2, 0xd3);
			}
			pdmpriv->bChangeCCKPDStateForAnotherMacOfDMSP = _FALSE;
		}
	}
	
	if(pDM_DigTable->PreCCKPDState != pDM_DigTable->CurCCKPDState)
	{
		if(BuddyAdapter == NULL)
		{
			DBG_871X("dm_CCK_PacketDetectionThresh_DMSP(): BuddyAdapter == NULL case \n");
			if(pHalData->bSlaveOfDMSP)
			{
				pDM_DigTable->PreCCKPDState = pDM_DigTable->CurCCKPDState;
			}
			else
			{	
				if(pDM_DigTable->CurCCKPDState == CCK_PD_STAGE_LowRssi)
				{
					//AcquireCCKAndRWPageAControl(pAdapter);
					//RT_TRACE(COMP_INIT,DBG_LOUD,("Acquiere mutex in dm_cck_packetdetection \n"));
					PHY_SetBBReg(pAdapter, rCCK0_CCA, bMaskByte2, 0x83);
					//RT_TRACE(COMP_INIT,DBG_LOUD,("Release mutex in dm_cck_packetdetection \n"));
					//ReleaseCCKAndRWPageAControl(pAdapter);
					//PHY_SetBBReg(pAdapter, rCCK0_System, bMaskByte1, 0x40);
					//if(IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID))
						//PHY_SetBBReg(pAdapter, rCCK0_FalseAlarmReport , bMaskByte2, 0xd7);
				}
				else
				{
					//AcquireCCKAndRWPageAControl(pAdapter);
					//RT_TRACE(COMP_INIT,DBG_LOUD,("Acquiere mutex in dm_cck_packetdetection \n"));
					PHY_SetBBReg(pAdapter, rCCK0_CCA, bMaskByte2, 0xcd);
					///ReleaseCCKAndRWPageAControl(pAdapter);
					//RT_TRACE(COMP_INIT,DBG_LOUD,("Release mutex in dm_cck_packetdetection \n"));

					//PHY_SetBBReg(pAdapter,rCCK0_System, bMaskByte1, 0x47);
					//if(IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID))
						//PHY_SetBBReg(pAdapter, rCCK0_FalseAlarmReport , bMaskByte2, 0xd3);
				}
				pDM_DigTable->PreCCKPDState = pDM_DigTable->CurCCKPDState;
			}
			return;
		}
	
		if(pHalData->bSlaveOfDMSP)
		{
			Buddydmpriv = &GET_HAL_DATA(BuddyAdapter)->dmpriv;
			DBG_871X("dm_CCK_PacketDetectionThresh_DMSP(): bslave case \n");
			Buddydmpriv->bChangeCCKPDStateForAnotherMacOfDMSP = _TRUE;
			Buddydmpriv->CurCCKPDStateForAnotherMacOfDMSP = pDM_DigTable->CurCCKPDState;
		}
		else
		{
			if(!bGetValueFromBuddyAdapter)
			{
				DBG_871X("dm_CCK_PacketDetectionThresh_DMSP(): mac 0 set for mac0 \n");
				if(pDM_DigTable->CurCCKPDState == CCK_PD_STAGE_LowRssi)
				{
					//AcquireCCKAndRWPageAControl(pAdapter);
					//RT_TRACE(COMP_INIT,DBG_LOUD,("Acquiere mutex in dm_cck_packetdetection \n"));
					PHY_SetBBReg(pAdapter, rCCK0_CCA, bMaskByte2, 0x83);
					//RT_TRACE(COMP_INIT,DBG_LOUD,("Release mutex in dm_cck_packetdetection \n"));
					//ReleaseCCKAndRWPageAControl(pAdapter);
					//PHY_SetBBReg(pAdapter, rCCK0_System, bMaskByte1, 0x40);
					//if(IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID))
						//PHY_SetBBReg(pAdapter, rCCK0_FalseAlarmReport , bMaskByte2, 0xd7);
				}
				else
				{
					//AcquireCCKAndRWPageAControl(pAdapter);
					//RT_TRACE(COMP_INIT,DBG_LOUD,("Acquiere mutex in dm_cck_packetdetection \n"));
					PHY_SetBBReg(pAdapter, rCCK0_CCA, bMaskByte2, 0xcd);
					//ReleaseCCKAndRWPageAControl(pAdapter);
					//RT_TRACE(COMP_INIT,DBG_LOUD,("Release mutex in dm_cck_packetdetection \n"));

					//PHY_SetBBReg(pAdapter,rCCK0_System, bMaskByte1, 0x47);
					//if(IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID))
						//PHY_SetBBReg(pAdapter, rCCK0_FalseAlarmReport , bMaskByte2, 0xd3);
				}
			}
		}
		pDM_DigTable->PreCCKPDState = pDM_DigTable->CurCCKPDState;
	}
	//DBG_871X("CCKPDStage=%x\n",pDM_DigTable->CurCCKPDState);
	//DBG_871X("is92C=%x\n",IS_92C_SERIAL(pHalData->VersionID));
	//DBG_871X("is92d single phy =%x\n",IS_92D_SINGLEPHY(pHalData->VersionID));
#endif
}

static void dm_CCK_PacketDetectionThresh(
	IN	PADAPTER	pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	pDIG_T		pDM_DigTable = &pdmpriv->DM_DigTable;

	if(pDM_DigTable->CurSTAConnectState == DIG_STA_CONNECT)
	{
		if(pDM_DigTable->PreCCKPDState == CCK_PD_STAGE_LowRssi)
		{
			if(pdmpriv->MinUndecoratedPWDBForDM <= 25)
				pDM_DigTable->CurCCKPDState = CCK_PD_STAGE_LowRssi;
			else
				pDM_DigTable->CurCCKPDState = CCK_PD_STAGE_HighRssi;
		}
		else{
			if(pdmpriv->MinUndecoratedPWDBForDM <= 20)
				pDM_DigTable->CurCCKPDState = CCK_PD_STAGE_LowRssi;
			else
				pDM_DigTable->CurCCKPDState = CCK_PD_STAGE_HighRssi;
		}
	}
	else
		pDM_DigTable->CurCCKPDState=CCK_PD_STAGE_LowRssi;
	
	if(pDM_DigTable->PreCCKPDState != pDM_DigTable->CurCCKPDState)
	{
		if(pDM_DigTable->CurCCKPDState == CCK_PD_STAGE_LowRssi)
		{
			//AcquireCCKAndRWPageAControl(pAdapter);
			//RT_TRACE(COMP_INIT,DBG_LOUD,("Acquiere mutex in dm_cck_packetdetection \n"));
			PHY_SetBBReg(pAdapter, rCCK0_CCA, bMaskByte2, 0x83);
			//RT_TRACE(COMP_INIT,DBG_LOUD,("Release mutex in dm_cck_packetdetection \n"));
			//ReleaseCCKAndRWPageAControl(pAdapter);
			//PHY_SetBBReg(pAdapter, rCCK0_System, bMaskByte1, 0x40);
			//if(IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID))
				//PHY_SetBBReg(pAdapter, rCCK0_FalseAlarmReport , bMaskByte2, 0xd7);
		}
		else
		{
			//AcquireCCKAndRWPageAControl(pAdapter);
			//RT_TRACE(COMP_INIT,DBG_LOUD,("Acquiere mutex in dm_cck_packetdetection \n"));
			PHY_SetBBReg(pAdapter, rCCK0_CCA, bMaskByte2, 0xcd);
			//ReleaseCCKAndRWPageAControl(pAdapter);
			//RT_TRACE(COMP_INIT,DBG_LOUD,("Release mutex in dm_cck_packetdetection \n"));

			//PHY_SetBBReg(pAdapter,rCCK0_System, bMaskByte1, 0x47);
			//if(IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID))
				//PHY_SetBBReg(pAdapter, rCCK0_FalseAlarmReport , bMaskByte2, 0xd3);
		}
		pDM_DigTable->PreCCKPDState = pDM_DigTable->CurCCKPDState;	
	}
	
	//RT_TRACE(	COMP_DIG, DBG_LOUD, ("CurSTAConnectState=%s\n",(pDigTable->CurSTAConnectState == DIG_STA_CONNECT?"DIG_STA_CONNECT ":"DIG_STA_DISCONNECT")));
	//RT_TRACE(	COMP_DIG, DBG_LOUD, ("CCKPDStage=%s\n",(pDigTable->CurCCKPDState==CCK_PD_STAGE_LowRssi?"Low RSSI ":"High RSSI ")));
	//RT_TRACE(	COMP_DIG, DBG_LOUD, ("is92d single phy =%x\n",IS_92D_SINGLEPHY(pHalData->VersionID)));
}

static void dm_1R_CCA(
	IN	PADAPTER	pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	pPS_T pDM_PSTable = &pdmpriv->DM_PSTable;
	struct registry_priv *pregistrypriv = &pAdapter->registrypriv;

#ifdef CONFIG_USB_HCI // Add by Gary for 92DU 1RCCA 5G->Enable, 2G->Disable 20110624
      if(pHalData->CurrentBandType92D == BAND_ON_5G)
      {
#endif
              //RT_TRACE(COMP_BB_POWERSAVING,DBG_LOUD,("=Gary=: 92D 5G 1RCCA on\n"));
		if(pdmpriv->MinUndecoratedPWDBForDM != 0)
		{
			if(pDM_PSTable->PreCCAState == CCA_2R || pDM_PSTable->PreCCAState == CCA_MAX)
			{
				if(pdmpriv->MinUndecoratedPWDBForDM >= 35)
					pDM_PSTable->CurCCAState = CCA_1R;
				else
					pDM_PSTable->CurCCAState = CCA_2R;
				
			}
			else{
				if(pdmpriv->MinUndecoratedPWDBForDM <= 30)
					pDM_PSTable->CurCCAState = CCA_2R;
				else
					pDM_PSTable->CurCCAState = CCA_1R;
			}
		}
		else	//disconnect
		{
			pDM_PSTable->CurCCAState=CCA_MAX;
		}
		
		if(pDM_PSTable->PreCCAState != pDM_PSTable->CurCCAState)
		{
			if(pDM_PSTable->CurCCAState == CCA_1R)
			{
				if(pHalData->rf_type == RF_2T2R)
				{
					if(pregistrypriv->special_rf_path == 1) // path A only
						PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x11);
					else if(pregistrypriv->special_rf_path == 2) //path B only
						PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x22);
					else
						PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x13);
					//PHY_SetBBReg(pAdapter, 0xe70, bMaskByte3, 0x20);
				}
				else
				{
					if(pregistrypriv->special_rf_path == 1) // path A only
						PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x11);
					else if(pregistrypriv->special_rf_path == 2) //path B only
						PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x22);
					else
						PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x23);
					//PHY_SetBBReg(pAdapter, 0xe70, 0x7fc00000, 0x10c); // Set RegE70[30:22] = 9b'100001100
				}
			}
			else if (pDM_PSTable->CurCCAState == CCA_2R || pDM_PSTable->CurCCAState == CCA_MAX)		
			{
				if(pregistrypriv->special_rf_path == 1) // path A only
					PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x11);
				else if(pregistrypriv->special_rf_path == 2) //path B only
					PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x22);
				else
					PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x33);
				//PHY_SetBBReg(pAdapter,0xe70, bMaskByte3, 0x63);
			}
			pDM_PSTable->PreCCAState = pDM_PSTable->CurCCAState;
		}
		//RT_TRACE(	COMP_BB_POWERSAVING|COMP_INIT, DBG_LOUD, ("CCAStage = %d\n",pDM_PSTable->CurCCAState));
#ifdef CONFIG_USB_HCI
	}  
	//else  // pHalData->CurrentBandType92D == BAND_ON_2_4G  92DU 2.4G 1RCCA Disable
	//	RT_TRACE(COMP_BB_POWERSAVING,DBG_LOUD,("=Gary=: 92D 2G 1RCCA off\n"));
#endif
}

static void dm_InitDynamicTxPower(IN	PADAPTER	Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	//pdmpriv->bDynamicTxPowerEnable = _TRUE;
	pdmpriv->bDynamicTxPowerEnable = _FALSE;

	pdmpriv->LastDTPLvl = TxHighPwrLevel_Normal;
	pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
}

static void odm_DynamicTxPower_92D(IN	PADAPTER	Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	int	UndecoratedSmoothedPWDB;

#ifdef CONFIG_DUALMAC_CONCURRENT
	PADAPTER	BuddyAdapter = Adapter->pbuddy_adapter;
	struct dm_priv	*pbuddy_dmpriv = NULL;
	BOOLEAN		bGetValueFromBuddyAdapter = dm_DualMacGetParameterFromBuddyAdapter(Adapter);
	u8		HighPowerLvlBackForMac0 = TxHighPwrLevel_Level1;
#endif

	// If dynamic high power is disabled.
	if( (pdmpriv->bDynamicTxPowerEnable != _TRUE) ||
		(!(pdmpriv->DMFlag & DYNAMIC_FUNC_HP)) )
	{
		pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		return;
	}

	// STA not connected and AP not connected
	if((check_fwstate(pmlmepriv, _FW_LINKED) != _TRUE) &&	
		(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("Not connected to any \n"));
		pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;

		//the LastDTPlvl should reset when disconnect, 
		//otherwise the tx power level wouldn't change when disconnect and connect again.
		// Maddest 20091220.
		pdmpriv->LastDTPLvl=TxHighPwrLevel_Normal;
		return;
	}
	
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)	// Default port
	{
		//todo: AP Mode
		if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
		       (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE))
		{
			UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
			//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("AP Client PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
		}
		else
		{
			UndecoratedSmoothedPWDB = pdmpriv->UndecoratedSmoothedPWDB;
			//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("STA Default Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
		}
	}
	else // associated entry pwdb
	{	
		UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
		//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("AP Ext Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
	}

	if(pHalData->CurrentBandType92D == BAND_ON_5G){
		if(UndecoratedSmoothedPWDB >= 0x33)
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
			//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("5G:TxHighPwrLevel_Level2 (TxPwr=0x0)\n"));
		}
		else if((UndecoratedSmoothedPWDB <0x33) &&
			(UndecoratedSmoothedPWDB >= 0x2b) )
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
			//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("5G:TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
		}
		else if(UndecoratedSmoothedPWDB < 0x2b)
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
			//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("5G:TxHighPwrLevel_Normal\n"));
		}
	}
	else
	{
		if(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL2)
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
			//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x0)\n"));
		}
		else if((UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL2-3)) &&
			(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL1) )
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
			//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
		}
		else if(UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL1-5))
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
			//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Normal\n"));
		}
	}

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(bGetValueFromBuddyAdapter)
	{
		DBG_871X("dm_DynamicTxPower() mac 0 for mac 1 \n");
		if(pdmpriv->bChangeTxHighPowerLvlForAnotherMacOfDMSP)
		{
			DBG_871X("dm_DynamicTxPower() change value \n");
			HighPowerLvlBackForMac0 = pdmpriv->DynamicTxHighPowerLvl;
			pdmpriv->DynamicTxHighPowerLvl = pdmpriv->CurTxHighLvlForAnotherMacOfDMSP;
			PHY_SetTxPowerLevel8192D(Adapter, pHalData->CurrentChannel);
			pdmpriv->DynamicTxHighPowerLvl = HighPowerLvlBackForMac0;
			pdmpriv->bChangeTxHighPowerLvlForAnotherMacOfDMSP = _FALSE;
		}						
	}
#endif

	if( (pdmpriv->DynamicTxHighPowerLvl != pdmpriv->LastDTPLvl) )
	{
		//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("PHY_SetTxPowerLevel8192D() Channel = %d \n" , pHalData->CurrentChannel));
#ifdef CONFIG_DUALMAC_CONCURRENT
		if(Adapter->DualMacConcurrent == _TRUE)
		{
			if(BuddyAdapter == NULL)
			{
				DBG_871X("dm_DynamicTxPower() BuddyAdapter == NULL case \n");
				if(!pHalData->bSlaveOfDMSP)
				{
					PHY_SetTxPowerLevel8192D(Adapter, pHalData->CurrentChannel);
				}
			}
			else
			{
				if(pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
				{
					DBG_871X("dm_DynamicTxPower() BuddyAdapter DMSP \n");
					if(pHalData->bSlaveOfDMSP)
					{
						DBG_871X("dm_DynamicTxPower() bslave case  \n");
						pbuddy_dmpriv = &GET_HAL_DATA(BuddyAdapter)->dmpriv;
						pbuddy_dmpriv->bChangeTxHighPowerLvlForAnotherMacOfDMSP = _TRUE;
						pbuddy_dmpriv->CurTxHighLvlForAnotherMacOfDMSP = pdmpriv->DynamicTxHighPowerLvl;
					}
					else
					{
						DBG_871X("dm_DynamicTxPower() master case  \n");
						if(!bGetValueFromBuddyAdapter)
						{
							DBG_871X("dm_DynamicTxPower() mac 0 for mac 0 \n");
							PHY_SetTxPowerLevel8192D(Adapter, pHalData->CurrentChannel);
						}
					}
				}
				else
				{
					DBG_871X("dm_DynamicTxPower() BuddyAdapter DMDP\n");
					PHY_SetTxPowerLevel8192D(Adapter, pHalData->CurrentChannel);
				}
			}
		}
		else
#endif
		{
			PHY_SetTxPowerLevel8192D(Adapter, pHalData->CurrentChannel);
		}
	}
	pdmpriv->LastDTPLvl = pdmpriv->DynamicTxHighPowerLvl;
	
}


static VOID PWDB_Monitor(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	int	i;
	int	tmpEntryMaxPWDB=0, tmpEntryMinPWDB=0xff;
	u8 	sta_cnt=0;
	u32	PWDB_rssi[NUM_STA]={0};//[0~15]:MACID, [16~31]:PWDB_rssi


	if(check_fwstate(&Adapter->mlmepriv, WIFI_AP_STATE|WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE)
	{
		_irqL irqL;
		_list	*plist, *phead;
		struct sta_info *psta;
		struct sta_priv *pstapriv = &Adapter->stapriv;
		u8 bcast_addr[ETH_ALEN]= {0xff,0xff,0xff,0xff,0xff,0xff};
	
		_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);

		for(i=0; i< NUM_STA; i++)
		{
			phead = &(pstapriv->sta_hash[i]);
			plist = get_next(phead);
		
			while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
			{
				psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

				plist = get_next(plist);

				if(_rtw_memcmp(psta	->hwaddr, bcast_addr, ETH_ALEN) || 
					_rtw_memcmp(psta->hwaddr, myid(&Adapter->eeprompriv), ETH_ALEN))
					continue;

				if(psta->state & WIFI_ASOC_STATE)
				{
					
					if(psta->rssi_stat.UndecoratedSmoothedPWDB < tmpEntryMinPWDB)
						tmpEntryMinPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					if(psta->rssi_stat.UndecoratedSmoothedPWDB > tmpEntryMaxPWDB)
						tmpEntryMaxPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					PWDB_rssi[sta_cnt++] = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16) | ((Adapter->stapriv.asoc_sta_count+1) << 8));
				}
			}
		}
	
		_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);

		if(pHalData->fw_ractrl == _TRUE)
		{
			// Report every sta's RSSI to FW
			for(i=0; i< sta_cnt; i++)
			{
				FillH2CCmd92D(Adapter, H2C_RSSI_REPORT, 3, (u8 *)(&PWDB_rssi[i]));
			}
		}
	}


	if(tmpEntryMaxPWDB != 0)	// If associated entry is found
	{
		pdmpriv->EntryMaxUndecoratedSmoothedPWDB = tmpEntryMaxPWDB;
	}
	else
	{
		pdmpriv->EntryMaxUndecoratedSmoothedPWDB = 0;
	}
	
	if(tmpEntryMinPWDB != 0xff) // If associated entry is found
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = tmpEntryMinPWDB;
	}
	else
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = 0;
	}

	if(check_fwstate(&Adapter->mlmepriv, WIFI_STATION_STATE) == _TRUE 
		&& check_fwstate(&Adapter->mlmepriv, _FW_LINKED) == _TRUE)
	{
		// Indicate Rx signal strength to FW.
		if(pHalData->fw_ractrl == _TRUE)
		{
			u32	temp = 0;
			//DBG_8192C("RxSS: %lx =%ld\n", pdmpriv->UndecoratedSmoothedPWDB, pdmpriv->UndecoratedSmoothedPWDB);

			temp = pdmpriv->UndecoratedSmoothedPWDB;
			temp = temp << 16;
			//temp = temp | 0x800000;

			// fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1

			// Commented by Kurt 20120705
			// We could set max mac_id to FW without checking how many STAs we allocated
			// It's recommanded by SD3
			// Original: temp = temp | ((Adapter->stapriv.asoc_sta_count+1) << 8);
			temp = temp | ((32) << 8);

			FillH2CCmd92D(Adapter, H2C_RSSI_REPORT, 3, (u8 *)(&temp));
		}
		else
		{
			rtw_write8(Adapter, 0x4fe, (u8)pdmpriv->UndecoratedSmoothedPWDB);
			//DBG_8192C("0x4fe write %x %d\n", pdmpriv->UndecoratedSmoothedPWDB, pdmpriv->UndecoratedSmoothedPWDB);
		}
	}
}

static void
DM_InitEdcaTurbo(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	pHalData->bCurrentTurboEDCA = _FALSE;
	Adapter->recvpriv.bIsAnyNonBEPkts = _FALSE;
}	// DM_InitEdcaTurbo

static void
dm_CheckEdcaTurbo(
	IN	PADAPTER	Adapter
	)
{
	u32 	trafficIndex;
	u32	edca_param;
	u64	cur_tx_bytes = 0;
	u64	cur_rx_bytes = 0;
	u32	EDCA_BE[2] = {0x5ea42b, 0x5ea42b};
	u8	bbtchange = _FALSE;
	u8	bLinked = _FALSE;
#ifdef CONFIG_CONCURRENT_MODE
	u8	buddy_bLinked = _FALSE;
#endif //CONFIG_CONCURRENT_MODE
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv		*pdmpriv = &pHalData->dmpriv;
	struct xmit_priv		*pxmitpriv = &(Adapter->xmitpriv);
	struct recv_priv		*precvpriv = &(Adapter->recvpriv);
	struct registry_priv	*pregpriv = &Adapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &(Adapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
#ifdef CONFIG_CONCURRENT_MODE
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
	struct xmit_priv		*pbuddy_pxmitpriv = &(pbuddy_adapter->xmitpriv);
      	struct recv_priv		*pbuddy_precvpriv = &(pbuddy_adapter->recvpriv);
	struct mlme_priv			*pbuddy_mlmepriv = &pbuddy_adapter->mlmepriv;
	struct mlme_ext_priv		*pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;
#endif //CONFIG_CONCURRENT_MODE

	if(IS_92D_SINGLEPHY(pHalData->VersionID))
	{
		EDCA_BE[UP_LINK] = 0x5ea42b;
		EDCA_BE[DOWN_LINK] = 0x5ea42b;
	}
	else
	{
		EDCA_BE[UP_LINK] = 0x6ea42b;
		EDCA_BE[DOWN_LINK] = 0x6ea42b;
	}

	if ((pregpriv->wifi_spec == 1) || (pmlmeext->cur_wireless_mode == WIRELESS_11B))
	{
		goto dm_CheckEdcaTurbo_EXIT;
	}

	if (pmlmeinfo->assoc_AP_vendor >= maxAP)
	{
		goto dm_CheckEdcaTurbo_EXIT;
	}

	if((check_fwstate(&Adapter->mlmepriv, _FW_LINKED) == _TRUE)&&(check_fwstate(&Adapter->mlmepriv, WIFI_STATION_STATE) == _TRUE))			
		bLinked = _TRUE;    

#ifdef CONFIG_CONCURRENT_MODE
	if((check_fwstate(&pbuddy_adapter->mlmepriv, _FW_LINKED) == _TRUE)&&(check_fwstate(&pbuddy_adapter->mlmepriv, WIFI_STATION_STATE) == _TRUE))			
		buddy_bLinked = _TRUE;
#endif //CONFIG_CONCURRENT_MODE
	if((!bLinked) 
#ifdef CONFIG_CONCURRENT_MODE
            && (!buddy_bLinked) 
#endif //CONFIG_CONCURRENT_MODE     
        )
	{
		goto dm_CheckEdcaTurbo_EXIT;
	}
      
	// Check if the status needs to be changed.
	if((bbtchange) || (!precvpriv->bIsAnyNonBEPkts) )
	{
		cur_tx_bytes = pxmitpriv->tx_bytes - pxmitpriv->last_tx_bytes;
		cur_rx_bytes = precvpriv->rx_bytes - precvpriv->last_rx_bytes;

#ifdef CONFIG_CONCURRENT_MODE
		cur_tx_bytes += pbuddy_pxmitpriv->tx_bytes - pbuddy_pxmitpriv->last_tx_bytes;
		cur_rx_bytes += pbuddy_precvpriv->rx_bytes - pbuddy_precvpriv->last_rx_bytes;

		if(_TRUE == buddy_bLinked)
                    pmlmeinfo = &(pbuddy_mlmeext->mlmext_info);
#endif //CONFIG_CONCURRENT_MODE
		//traffic, TX or RX
		if((pmlmeinfo->assoc_AP_vendor == ralinkAP)||(pmlmeinfo->assoc_AP_vendor == atherosAP))
		{
			if (cur_tx_bytes > (cur_rx_bytes << 2))
			{ // Uplink TP is present.
				trafficIndex = UP_LINK; 
			}
			else
			{ // Balance TP is present.
				trafficIndex = DOWN_LINK;
			}
		}
		else
		{
			if (cur_rx_bytes > (cur_tx_bytes << 2))
			{ // Downlink TP is present.
				trafficIndex = DOWN_LINK;
			}
			else
			{ // Balance TP is present.
				trafficIndex = UP_LINK;
			}
		}

		//if ((pdmpriv->prv_traffic_idx != trafficIndex) || (!pHalData->bCurrentTurboEDCA))
		{
#ifdef CONFIG_CONCURRENT_MODE		
			if((_TRUE == bLinked) && (_TRUE == buddy_bLinked))
			{
				edca_param = 0x5ea42b;
			}         
			else
#endif //CONFIG_CONCURRENT_MODE              
			{
				if((pmlmeinfo->assoc_AP_vendor == ciscoAP) && (pmlmeext->cur_wireless_mode & (WIRELESS_11_24N)))
				{
					edca_param = EDCAParam[pmlmeinfo->assoc_AP_vendor][trafficIndex];
				}
				else if((pmlmeinfo->assoc_AP_vendor == airgocapAP) &&
					((pmlmeext->cur_wireless_mode == WIRELESS_11G) ||(pmlmeext->cur_wireless_mode == WIRELESS_11BG)))
				{
					if(trafficIndex == DOWN_LINK)
						edca_param = 0xa630;
					else
						edca_param = EDCA_BE[trafficIndex];
				}
				else if( (pmlmeinfo->assoc_AP_vendor == atherosAP) && 
					(pmlmeext->cur_wireless_mode&WIRELESS_11_5N) )
				{
					if(trafficIndex == DOWN_LINK)
						edca_param = 0xa42b;
					else
						edca_param = EDCA_BE[trafficIndex];
				}
				else
				{
					edca_param = EDCA_BE[trafficIndex];
				}
			}

			rtw_write32(Adapter, REG_EDCA_BE_PARAM, edca_param);

			pdmpriv->prv_traffic_idx = trafficIndex;
		}
		
		pHalData->bCurrentTurboEDCA = _TRUE;
	}
	else
	{
		//
		// Turn Off EDCA turbo here.
		// Restore original EDCA according to the declaration of AP.
		//
		 if(pHalData->bCurrentTurboEDCA)
		{
			rtw_write32(Adapter, REG_EDCA_BE_PARAM, pHalData->AcParam_BE);
			pHalData->bCurrentTurboEDCA = _FALSE;
		}
	}

dm_CheckEdcaTurbo_EXIT:
	// Set variables for next time.
	precvpriv->bIsAnyNonBEPkts = _FALSE;
	pxmitpriv->last_tx_bytes = pxmitpriv->tx_bytes;
	precvpriv->last_rx_bytes = precvpriv->rx_bytes;
#ifdef CONFIG_CONCURRENT_MODE
	pbuddy_pxmitpriv->last_tx_bytes = pbuddy_pxmitpriv->tx_bytes;
	pbuddy_precvpriv->last_rx_bytes = pbuddy_precvpriv->rx_bytes;
#endif //CONFIG_CONCURRENT_MODE

}	// dm_CheckEdcaTurbo

static void dm_InitDynamicBBPowerSaving(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	pPS_T pDM_PSTable = &pdmpriv->DM_PSTable;
	
	pDM_PSTable->PreCCAState = CCA_MAX;
	pDM_PSTable->CurCCAState = CCA_MAX;
	pDM_PSTable->PreRFState = RF_MAX;
	pDM_PSTable->CurRFState = RF_MAX;
}

static void
dm_DynamicBBPowerSaving(
IN	PADAPTER	pAdapter
	)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	//struct dm_priv	*pdmpriv = &pHalData->dmpriv;
#ifdef CONFIG_DUALMAC_CONCURRENT
	//s32			Rssi_val_min_back_for_mac0;
	//BOOLEAN		bGetValueFromBuddyAdapter = dm_DualMacGetParameterFromBuddyAdapter(pAdapter);
	//BOOLEAN		bRestoreRssi = _FALSE;
	//PADAPTER	BuddyAdapter = pAdapter->pbuddy_adapter;
#endif

	
	//1 Power Saving for 92C
	if(IS_92D_SINGLEPHY(pHalData->VersionID))
	{
#ifdef CONFIG_DUALMAC_CONCURRENT
		if(!pHalData->bSlaveOfDMSP)
#endif
			dm_1R_CCA(pAdapter);
	}

#if 0
	if(bRestoreRssi)
	{
		bRestoreRssi = _FALSE;
		pdmpriv->MinUndecoratedPWDBForDM = Rssi_val_min_back_for_mac0;
	}
#endif

// 20100628 Joseph: Turn off BB power save for 88CE because it makesthroughput unstable.
#if 0
	//1 3.Power Saving for 88C
	if(!IS_92C_SERIAL(pHalData->VersionID))
	{
		dm_RF_Saving(pAdapter, FALSE);
	}
#endif
}

static 	VOID
dm_RXGainTrackingCallback_ThermalMeter_92D(
	IN PADAPTER 	Adapter)
{
	u8			index_mapping[Rx_index_mapping_NUM] = {
						0x0f,	0x0f,	0x0f,	0x0f,	0x0b,
						0x0a,	0x09,	0x08,	0x07,	0x06,
						0x05,	0x04,	0x04,	0x03,	0x02
					};

	u8			eRFPath;
	u32			u4tmp;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	u4tmp = (index_mapping[(pHalData->EEPROMThermalMeter - pdmpriv->ThermalValue_RxGain)]) << 12;

	//DBG_8192C("===>dm_RXGainTrackingCallback_ThermalMeter_92D interface %d  Rx Gain %x\n", pHalData->interfaceIndex, u4tmp);
	
	for(eRFPath = RF_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++){
		PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, RF_RXRF_A3, bRFRegOffsetMask, (pdmpriv->RegRF3C[eRFPath]&(~(0xF000)))|u4tmp);
	}

};	

//091212 chiyokolin
static	VOID
dm_TXPowerTrackingCallback_ThermalMeter_92D(
            IN PADAPTER	Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8			ThermalValue = 0, delta, delta_LCK, delta_IQK, delta_RxGain, index, offset;
	u8			ThermalValue_AVG_count = 0;
	u32			ThermalValue_AVG = 0;
	s32			ele_A = 0, ele_D, TempCCk, X, value32;
	s32			Y, ele_C;
	s8			OFDM_index[2], CCK_index=0, OFDM_index_old[2], CCK_index_old=0;
	s32	    		i = 0;
	BOOLEAN		is2T = IS_92D_SINGLEPHY(pHalData->VersionID);
	BOOLEAN		bInteralPA = _FALSE;

	u8			OFDM_min_index = 6, OFDM_min_index_internalPA = 12, rf; //OFDM BB Swing should be less than +3.0dB, which is required by Arthur
	u8			Indexforchannel = rtl8192d_GetRightChnlPlaceforIQK(pHalData->CurrentChannel);
	u8			index_mapping[5][index_mapping_NUM] = {	
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

	u8			index_mapping_internalPA[8][index_mapping_NUM] = { 
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
	if(!pdmpriv->TxPowerTrackControl)
		return;
		
#ifdef CONFIG_MP_INCLUDED
	DBG_871X("%s() \n",__func__);
#endif

	pdmpriv->TXPowerTrackingCallbackCnt++;	//cosa add for debug
	pdmpriv->bTXPowerTrackingInit = _TRUE;

	if(pHalData->CurrentChannel == 14 && !pdmpriv->bCCKinCH14)
		pdmpriv->bCCKinCH14 = _TRUE;
	else if(pHalData->CurrentChannel != 14 && pdmpriv->bCCKinCH14)
		pdmpriv->bCCKinCH14 = _FALSE;

	//RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("===>dm_TXPowerTrackingCallback_ThermalMeter_92D interface %d txpowercontrol %d\n", pHalData->interfaceIndex, pdmpriv->TxPowerTrackControl));

	ThermalValue = (u8)PHY_QueryRFReg(Adapter, RF_PATH_A, RF_T_METER, 0xf800);	//0x42: RF Reg[15:11] 92D

	//RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Readback Thermal Meter = 0x%lx pre thermal meter 0x%lx EEPROMthermalmeter 0x%lx\n", ThermalValue, pHalData->ThermalValue, pHalData->EEPROMThermalMeter));

	rtl8192d_PHY_APCalibrate(Adapter, (ThermalValue - pHalData->EEPROMThermalMeter));//notes:EEPROMThermalMeter is a fixed value from efuse/eeprom

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
					OFDM_index_old[0] = (u8)i;
					//RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Initial pathA ele_D reg0x%x = 0x%lx, OFDM_index=0x%x\n", 
					//	rOFDM0_XATxIQImbalance, ele_D, OFDM_index_old[0]));
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
						OFDM_index_old[1] = (u8)i;
						//RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Initial pathB ele_D reg0x%x = 0x%lx, OFDM_index=0x%x\n", 
						//	rOFDM0_XBTxIQImbalance, ele_D, OFDM_index_old[1]));
						break;
					}
				}
			}

			if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
			{
				//Query CCK default setting From 0xa24
				TempCCk = pdmpriv->RegA24;

				for(i=0 ; i<CCK_TABLE_SIZE ; i++)
				{
					if(pdmpriv->bCCKinCH14)
					{
						if(_rtw_memcmp((void*)&TempCCk, (void*)&CCKSwingTable_Ch14[i][2], 4)==_TRUE)
						{
							CCK_index_old =(u8)i;
							//RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Initial reg0x%x = 0x%lx, CCK_index=0x%x, ch 14 %d\n", 
							//	rCCK0_TxFilter2, TempCCk, CCK_index_old, pdmpriv->bCCKinCH14));
							break;
						}
					}
					else
					{
						if(_rtw_memcmp((void*)&TempCCk, (void*)&CCKSwingTable_Ch1_Ch13[i][2], 4)==_TRUE)
						{
							CCK_index_old =(u8)i;
							//RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Initial reg0x%x = 0x%lx, CCK_index=0x%x, ch14 %d\n", 
							//	rCCK0_TxFilter2, TempCCk, CCK_index_old, pdmpriv->bCCKinCH14));
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

			if(!pdmpriv->ThermalValue)
			{
				pdmpriv->ThermalValue = pHalData->EEPROMThermalMeter;
				pdmpriv->ThermalValue_LCK = ThermalValue;
				pdmpriv->ThermalValue_IQK = ThermalValue;
				pdmpriv->ThermalValue_RxGain = pHalData->EEPROMThermalMeter;

				for(i = 0; i < rf; i++)
					pdmpriv->OFDM_index[i] = OFDM_index_old[i];
				pdmpriv->CCK_index = CCK_index_old;
			}

			if(pdmpriv->bReloadtxpowerindex)
			{
				DBG_871X("reload ofdm index for band switch\n");
			}

			//calculate average thermal meter
			{
				pdmpriv->ThermalValue_AVG[pdmpriv->ThermalValue_AVG_index] = ThermalValue;
				pdmpriv->ThermalValue_AVG_index++;
				if(pdmpriv->ThermalValue_AVG_index == AVG_THERMAL_NUM)
					pdmpriv->ThermalValue_AVG_index = 0;

				for(i = 0; i < AVG_THERMAL_NUM; i++)
				{
					if(pdmpriv->ThermalValue_AVG[i])
					{
						ThermalValue_AVG += pdmpriv->ThermalValue_AVG[i];
						ThermalValue_AVG_count++;
					}
				}

				if(ThermalValue_AVG_count)
					ThermalValue = (u8)(ThermalValue_AVG / ThermalValue_AVG_count);
			}
		}

		if(pdmpriv->bReloadtxpowerindex)
		{
			delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);				
			pdmpriv->bReloadtxpowerindex = _FALSE;	
			pdmpriv->bDoneTxpower = _FALSE;
		}
		else if(pdmpriv->bDoneTxpower)
		{
			delta = (ThermalValue > pdmpriv->ThermalValue)?(ThermalValue - pdmpriv->ThermalValue):(pdmpriv->ThermalValue - ThermalValue);
		}
		else
		{
			delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);		
		}
		delta_LCK = (ThermalValue > pdmpriv->ThermalValue_LCK)?(ThermalValue - pdmpriv->ThermalValue_LCK):(pdmpriv->ThermalValue_LCK - ThermalValue);
		delta_IQK = (ThermalValue > pdmpriv->ThermalValue_IQK)?(ThermalValue - pdmpriv->ThermalValue_IQK):(pdmpriv->ThermalValue_IQK - ThermalValue);
		delta_RxGain = (ThermalValue > pdmpriv->ThermalValue_RxGain)?(ThermalValue - pdmpriv->ThermalValue_RxGain):(pdmpriv->ThermalValue_RxGain - ThermalValue);

		//RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("interface %d Readback Thermal Meter = 0x%lx pre thermal meter 0x%lx EEPROMthermalmeter 0x%lx delta 0x%lx delta_LCK 0x%lx delta_IQK 0x%lx delta_RxGain 0x%lx\n",  pHalData->interfaceIndex, ThermalValue, pdmpriv->ThermalValue, pHalData->EEPROMThermalMeter, delta, delta_LCK, delta_IQK, delta_RxGain));
		//RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("interface %d pre thermal meter LCK 0x%lx pre thermal meter IQK 0x%lx delta_LCK_bound 0x%lx delta_IQK_bound 0x%lx\n",  pHalData->interfaceIndex, pdmpriv->ThermalValue_LCK, pdmpriv->ThermalValue_IQK, pdmpriv->Delta_LCK, pdmpriv->Delta_IQK));

		if((delta_LCK > pdmpriv->Delta_LCK) && (pdmpriv->Delta_LCK != 0))
		{
			if(ThermalValue > pdmpriv->ThermalValue_LCK)
				pdmpriv->ThermalValue_Variation = 1;//ThermalValue increase 
			else
				pdmpriv->ThermalValue_Variation = 0;//ThermalValue decrease 
			pdmpriv->ThermalValue_LCK = ThermalValue;
			rtl8192d_PHY_LCCalibrate(Adapter, _FALSE);
		}
		
		if(delta > 0 && pdmpriv->TxPowerTrackControl)
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
						 	OFDM_index[i] = pdmpriv->OFDM_index[i] -delta;
						CCK_index = pdmpriv->CCK_index -delta;
					}
					else
					{
						for(i = 0; i < rf; i++)			
							OFDM_index[i] = pdmpriv->OFDM_index[i] + index;
						CCK_index = pdmpriv->CCK_index + index;
					}
				}
				else if(pHalData->CurrentBandType92D == BAND_ON_5G)
				{
					for(i = 0; i < rf; i++)
					{
						if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY &&
							pHalData->interfaceIndex == 1)		//MAC 1 5G
							bInteralPA = pHalData->InternalPA5G[1];
						else
							bInteralPA = pHalData->InternalPA5G[i];

						if(bInteralPA)
						{
							if(pHalData->interfaceIndex == 1 || i == rf)
								offset = 4;
							else
								offset = 0;

							if(pHalData->CurrentChannel >= 100 && pHalData->CurrentChannel <= 165)
								offset += 2;													
						}
						else
						{
							if(pHalData->interfaceIndex == 1 || i == rf)
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
								OFDM_index[i] = pdmpriv->OFDM_index[i] -((delta/2)*3+(delta%2));
							}
							else	
							{
								OFDM_index[i] = pdmpriv->OFDM_index[i] -index;
							}
						}
						else
						{				
							OFDM_index[i] = pdmpriv->OFDM_index[i] + index;
						}
					}
				}
				
				/*if(is2T)
				{
					DBG_871X("temp OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
						pdmpriv->OFDM_index[0], pdmpriv->OFDM_index[1], pdmpriv->CCK_index);
				}
				else
				{
					DBG_871X("temp OFDM_A_index=0x%x, CCK_index=0x%x\n", 
						pdmpriv->OFDM_index[0], pdmpriv->CCK_index);
				}*/
				
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

				/*if(is2T)
				{
					DBG_871X("new OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
						OFDM_index[0], OFDM_index[1], CCK_index);
				}
				else
				{
					DBG_871X("new OFDM_A_index=0x%x, CCK_index=0x%x\n", 
						OFDM_index[0], CCK_index);
				}*/
			}

			//Config by SwingTable
			if(pdmpriv->TxPowerTrackControl && !pHalData->bNOPG)
			{
				pdmpriv->bDoneTxpower = _TRUE;
			
				//Adujst OFDM Ant_A according to IQK result
				ele_D = (OFDMSwingTable[(u8)OFDM_index[0]] & 0xFFC00000)>>22;
				//X = pdmpriv->RegE94;
				//Y = pdmpriv->RegE9C;
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
					PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable[(u8)OFDM_index[0]]);
					PHY_SetBBReg(Adapter, rOFDM0_XCTxAFE, bMaskH4Bits, 0x00);
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT24, 0x00);
				}

				//DBG_871X("TxPwrTracking for interface %d path A: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x 0xe94 = 0x%x 0xe9c = 0x%x\n", Adapter->interfaceIndex, X, Y, ele_A, ele_C, ele_D, X, Y);
				
				if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
				{
					//Adjust CCK according to IQK result
					if(!pdmpriv->bCCKinCH14){
						rtw_write8(Adapter, 0xa22, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][0]);
						rtw_write8(Adapter, 0xa23, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][1]);
						rtw_write8(Adapter, 0xa24, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][2]);
						rtw_write8(Adapter, 0xa25, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][3]);
						rtw_write8(Adapter, 0xa26, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][4]);
						rtw_write8(Adapter, 0xa27, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][5]);
						rtw_write8(Adapter, 0xa28, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][6]);
						rtw_write8(Adapter, 0xa29, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][7]);		
					}
					else{
						rtw_write8(Adapter, 0xa22, CCKSwingTable_Ch14[(u8)CCK_index][0]);
						rtw_write8(Adapter, 0xa23, CCKSwingTable_Ch14[(u8)CCK_index][1]);
						rtw_write8(Adapter, 0xa24, CCKSwingTable_Ch14[(u8)CCK_index][2]);
						rtw_write8(Adapter, 0xa25, CCKSwingTable_Ch14[(u8)CCK_index][3]);
						rtw_write8(Adapter, 0xa26, CCKSwingTable_Ch14[(u8)CCK_index][4]);
						rtw_write8(Adapter, 0xa27, CCKSwingTable_Ch14[(u8)CCK_index][5]);
						rtw_write8(Adapter, 0xa28, CCKSwingTable_Ch14[(u8)CCK_index][6]);
						rtw_write8(Adapter, 0xa29, CCKSwingTable_Ch14[(u8)CCK_index][7]);	
					}		
				}
				
				if(is2T)
				{						
					ele_D = (OFDMSwingTable[(u8)OFDM_index[1]] & 0xFFC00000)>>22;
					
					//new element A = element D x X
					//X = pdmpriv->RegEB4;
					//Y = pdmpriv->RegEBC;
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
						PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, OFDMSwingTable[(u8)OFDM_index[1]]);
						PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, 0x00);
						PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT28, 0x00);	
					}

					//DBG_871X("TxPwrTracking path B: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x 0xeb4 = 0x%x 0xebc = 0x%x\n", X, Y, ele_A, ele_C, ele_D, X, Y);
				}

				//DBG_871X("TxPwrTracking 0xc80 = 0x%x, 0xc94 = 0x%x RF 0x24 = 0x%x\n", PHY_QueryBBReg(Adapter, 0xc80, bMaskDWord), PHY_QueryBBReg(Adapter, 0xc94, bMaskDWord), PHY_QueryRFReg(Adapter, RF_PATH_A, 0x24, bRFRegOffsetMask));
			}
		}

		if((delta_IQK > pdmpriv->Delta_IQK) && (pdmpriv->Delta_IQK != 0))
		{
			rtl8192d_PHY_ResetIQKResult(Adapter);
#ifdef CONFIG_CONCURRENT_MODE
			if (rtw_buddy_adapter_up(Adapter)) {
				rtl8192d_PHY_ResetIQKResult(Adapter->pbuddy_adapter);
			}
#endif
			pdmpriv->ThermalValue_IQK = ThermalValue;
			rtl8192d_PHY_IQCalibrate(Adapter, _TRUE);
		}

		if(delta_RxGain > 0 && pHalData->CurrentBandType92D == BAND_ON_5G 
			&& ThermalValue <= pHalData->EEPROMThermalMeter && pHalData->bNOPG == _FALSE)
		{
			pdmpriv->ThermalValue_RxGain = ThermalValue;		
			dm_RXGainTrackingCallback_ThermalMeter_92D(Adapter);
		}

		//update thermal meter value
		if(pdmpriv->TxPowerTrackControl)
		{
			pdmpriv->ThermalValue = ThermalValue;
		}
	
	}

	//RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("<===dm_TXPowerTrackingCallback_ThermalMeter_92D\n"));
	
	pdmpriv->TXPowercount = 0;

}


static	VOID
dm_InitializeTXPowerTracking_ThermalMeter(
	IN	PADAPTER		Adapter)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	//if(IS_HARDWARE_TYPE_8192C(pHalData))
	{
		pdmpriv->bTXPowerTracking = _TRUE;
		pdmpriv->TXPowercount = 0;
		pdmpriv->bTXPowerTrackingInit = _FALSE;
#if	(MP_DRIVER != 1)		//for mp driver, turn off txpwrtracking as default
		pdmpriv->TxPowerTrackControl = _TRUE;
#endif
	}
	MSG_8192C("pdmpriv->TxPowerTrackControl = %d\n", pdmpriv->TxPowerTrackControl);
}


static VOID
DM_InitializeTXPowerTracking(
	IN	PADAPTER		Adapter)
{
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	//if(IS_HARDWARE_TYPE_8192C(pHalData))
	{
		dm_InitializeTXPowerTracking_ThermalMeter(Adapter);
	}
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
static VOID
DM_TXPowerTracking92CDirectCall(
            IN	PADAPTER		Adapter)
{	
	dm_TXPowerTrackingCallback_ThermalMeter_92D(Adapter);
}

static VOID
dm_CheckTXPowerTracking_ThermalMeter(
	IN	PADAPTER		Adapter)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	//u1Byte					TxPowerCheckCnt = 5;	//10 sec

	if(!(pdmpriv->DMFlag & DYNAMIC_FUNC_SS))
	{
		return;
	}

	if(!pdmpriv->bTXPowerTracking /*|| (!pHalData->TxPowerTrackControl && pHalData->bAPKdone)*/)
	{
		return;
	}

	if(!pdmpriv->TM_Trigger)		//at least delay 1 sec
	{
		//pHalData->TxPowerCheckCnt++;	//cosa add for debug

		PHY_SetRFReg(Adapter, RF_PATH_A, RF_T_METER, BIT17 | BIT16, 0x03);
		//RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Trigger 92C Thermal Meter!!\n"));
		pdmpriv->TM_Trigger = 1;
		return;
	}
	else
	{
		//RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Schedule TxPowerTracking direct call!!\n"));		
		DM_TXPowerTracking92CDirectCall(Adapter); //Using direct call is instead, added by Roger, 2009.06.18.
		pdmpriv->TM_Trigger = 0;
	}

}


VOID
rtl8192d_dm_CheckTXPowerTracking(
	IN	PADAPTER		Adapter)
{
	//RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("dm_CheckTXPowerTracking!!\n"));

#if DISABLE_BB_RF
	return;
#endif
	dm_CheckTXPowerTracking_ThermalMeter(Adapter);
}


/*-----------------------------------------------------------------------------
 * Function:	dm_CheckRfCtrlGPIO()
 *
 * Overview:	Copy 8187B template for 9xseries.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	01/10/2008	MHC		Create Version 0.  
 *
 *---------------------------------------------------------------------------*/
#ifndef PLATFORM_FREEBSD
static VOID
dm_CheckRfCtrlGPIO(
	IN	PADAPTER	Adapter
	)
{

}	/* dm_CheckRfCtrlGPIO */
#endif //PLATFORM_FREEBSD

/*-----------------------------------------------------------------------------
 * Function:	dm_CheckPbcGPIO()
 *
 * Overview:	Check if PBC button is pressed.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	03/11/2008	hpfan	Create Version 0.  
 *
 *---------------------------------------------------------------------------*/
static void	dm_CheckPbcGPIO(IN PADAPTER padapter)
{
	u8	tmp1byte;
	u8	bPbcPressed = _FALSE;
	int i=0;

	if(!padapter->registrypriv.hw_wps_pbc)
		return;

	do
	{
		i++;
#ifdef CONFIG_USB_HCI
	tmp1byte = rtw_read8(padapter, GPIO_IO_SEL);
	tmp1byte |= (HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter, GPIO_IO_SEL, tmp1byte);	//enable GPIO[2] as output mode

	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter,  GPIO_IN, tmp1byte);		//reset the floating voltage level

	tmp1byte = rtw_read8(padapter, GPIO_IO_SEL);
	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter, GPIO_IO_SEL, tmp1byte);	//enable GPIO[2] as input mode

	tmp1byte =rtw_read8(padapter, GPIO_IN);
	
	if (tmp1byte == 0xff)
	{
		bPbcPressed = _FALSE;
		break ;
	}

	if (tmp1byte&HAL_8192C_HW_GPIO_WPS_BIT)
	{		
		bPbcPressed = _TRUE;

		if(i<=3)
			rtw_msleep_os(50);
	}
#else
	tmp1byte = rtw_read8(padapter, GPIO_IN);
	//RT_TRACE(COMP_IO, DBG_TRACE, ("dm_CheckPbcGPIO - %x\n", tmp1byte));

	if (tmp1byte == 0xff || padapter->init_adpt_in_progress)
	{
		bPbcPressed = _FALSE;
		break ;
	}

	if((tmp1byte&HAL_8192C_HW_GPIO_WPS_BIT)==0)
	{
		bPbcPressed = _TRUE;
		
		if(i<=3)
			rtw_msleep_os(50);
	}
#endif
	}while(i<=3 && bPbcPressed == _TRUE);

	if( _TRUE == bPbcPressed)
	{
		// Here we only set bPbcPressed to true
		// After trigger PBC, the variable will be set to false		
		DBG_8192C("CheckPbcGPIO - PBC is pressed, try_cnt=%d\n", i-1);
                
#ifdef RTK_DMP_PLATFORM
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,12))
		kobject_uevent(&padapter->pnetdev->dev.kobj, KOBJ_NET_PBC);
#else
		kobject_hotplug(&padapter->pnetdev->class_dev.kobj, KOBJ_NET_PBC);
#endif
#else

		if ( padapter->pid[0] == 0 )
		{	//	0 is the default value and it means the application monitors the HW PBC doesn't privde its pid to driver.
			return;
		}

#ifdef PLATFORM_LINUX
		rtw_signal_process(padapter->pid[0], SIGUSR1);
#endif
#endif
	}
}

static VOID
dm_InitRateAdaptiveMask(
	IN	PADAPTER	Adapter	
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PRATE_ADAPTIVE	pRA = (PRATE_ADAPTIVE)&pdmpriv->RateAdaptive;
	
	pRA->RATRState = DM_RATR_STA_INIT;
	pRA->PreRATRState = DM_RATR_STA_INIT;

	if (pdmpriv->DM_Type == DM_Type_ByDriver)
		pdmpriv->bUseRAMask = _TRUE;
	else
		pdmpriv->bUseRAMask = _FALSE;	
}


/*-----------------------------------------------------------------------------
 * Function:	dm_RefreshRateAdaptiveMask()
 *
 * Overview:	Update rate table mask according to rssi
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	05/27/2009	hpfan	Create Version 0.  
 *
 *---------------------------------------------------------------------------*/
static VOID
dm_RefreshRateAdaptiveMask(	IN	PADAPTER	pAdapter)
{
#if 0
	PADAPTER 				pTargetAdapter;
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(pAdapter);
	PMGNT_INFO				pMgntInfo = &(ADJUST_TO_ADAPTIVE_ADAPTER(pAdapter, TRUE)->MgntInfo);
	PRATE_ADAPTIVE			pRA = (PRATE_ADAPTIVE)&pMgntInfo->RateAdaptive;
	u4Byte					LowRSSIThreshForRA = 0, HighRSSIThreshForRA = 0;

	if(pAdapter->bDriverStopped)
	{
		RT_TRACE(COMP_RATR, DBG_TRACE, ("<---- dm_RefreshRateAdaptiveMask(): driver is going to unload\n"));
		return;
	}

	if(!pMgntInfo->bUseRAMask)
	{
		RT_TRACE(COMP_RATR, DBG_LOUD, ("<---- dm_RefreshRateAdaptiveMask(): driver does not control rate adaptive mask\n"));
		return;
	}

	// if default port is connected, update RA table for default port (infrastructure mode only)
	if(pAdapter->MgntInfo.mAssoc && (!ACTING_AS_AP(pAdapter)))
	{
		
		// decide rastate according to rssi
		switch (pRA->PreRATRState)
		{
			case DM_RATR_STA_HIGH:
				HighRSSIThreshForRA = 50;
				LowRSSIThreshForRA = 20;
				break;
			
			case DM_RATR_STA_MIDDLE:
				HighRSSIThreshForRA = 55;
				LowRSSIThreshForRA = 20;
				break;
			
			case DM_RATR_STA_LOW:
				HighRSSIThreshForRA = 50;
				LowRSSIThreshForRA = 25;
				break;

			default:
				HighRSSIThreshForRA = 50;
				LowRSSIThreshForRA = 20;
				break;
		}

		if(pHalData->UndecoratedSmoothedPWDB > (s4Byte)HighRSSIThreshForRA)
			pRA->RATRState = DM_RATR_STA_HIGH;
		else if(pHalData->UndecoratedSmoothedPWDB > (s4Byte)LowRSSIThreshForRA)
			pRA->RATRState = DM_RATR_STA_MIDDLE;
		else
			pRA->RATRState = DM_RATR_STA_LOW;

		if(pRA->PreRATRState != pRA->RATRState)
		{
			RT_PRINT_ADDR(COMP_RATR, DBG_LOUD, ("Target AP addr : "), pMgntInfo->Bssid);
			RT_TRACE(COMP_RATR, DBG_LOUD, ("RSSI = %ld\n", pHalData->UndecoratedSmoothedPWDB));
			RT_TRACE(COMP_RATR, DBG_LOUD, ("RSSI_LEVEL = %d\n", pRA->RATRState));
			RT_TRACE(COMP_RATR, DBG_LOUD, ("PreState = %d, CurState = %d\n", pRA->PreRATRState, pRA->RATRState));
			pAdapter->HalFunc.UpdateHalRAMaskHandler(
									pAdapter,
									FALSE,
									0,
									NULL,
									NULL,
									pRA->RATRState);
			pRA->PreRATRState = pRA->RATRState;
		}
	}

	//
	// The following part configure AP/VWifi/IBSS rate adaptive mask.
	//
	if(ACTING_AS_AP(pAdapter) || ACTING_AS_IBSS(pAdapter))
	{
		pTargetAdapter = pAdapter;
	}
	else
	{
		pTargetAdapter = ADJUST_TO_ADAPTIVE_ADAPTER(pAdapter, FALSE);
		if(!ACTING_AS_AP(pTargetAdapter))
			pTargetAdapter = NULL;
	}

	// if extension port (softap) is started, updaet RA table for more than one clients associate
	if(pTargetAdapter != NULL)
	{
		int	i;
		PRT_WLAN_STA	pEntry;
		PRATE_ADAPTIVE     pEntryRA;

		for(i = 0; i < ASSOCIATE_ENTRY_NUM; i++)
		{
			if(	pTargetAdapter->MgntInfo.AsocEntry[i].bUsed && pTargetAdapter->MgntInfo.AsocEntry[i].bAssociated)
			{
				pEntry = pTargetAdapter->MgntInfo.AsocEntry+i;
				pEntryRA = &pEntry->RateAdaptive;

				switch (pEntryRA->PreRATRState)
				{
					case DM_RATR_STA_HIGH:
					{
						HighRSSIThreshForRA = 50;
						LowRSSIThreshForRA = 20;
					}
					break;
					
					case DM_RATR_STA_MIDDLE:
					{
						HighRSSIThreshForRA = 55;
						LowRSSIThreshForRA = 20;
					}
					break;
					
					case DM_RATR_STA_LOW:
					{
						HighRSSIThreshForRA = 50;
						LowRSSIThreshForRA = 25;
					}
					break;

					default:
					{
						HighRSSIThreshForRA = 50;
						LowRSSIThreshForRA = 20;
					}
				}

				if(pEntry->rssi_stat.UndecoratedSmoothedPWDB > (s4Byte)HighRSSIThreshForRA)
					pEntryRA->RATRState = DM_RATR_STA_HIGH;
				else if(pEntry->rssi_stat.UndecoratedSmoothedPWDB > (s4Byte)LowRSSIThreshForRA)
					pEntryRA->RATRState = DM_RATR_STA_MIDDLE;
				else
					pEntryRA->RATRState = DM_RATR_STA_LOW;

				if(pEntryRA->PreRATRState != pEntryRA->RATRState)
				{
					RT_PRINT_ADDR(COMP_RATR, DBG_LOUD, ("AsocEntry addr : "), pEntry->MacAddr);
					RT_TRACE(COMP_RATR, DBG_LOUD, ("RSSI = %ld\n", pEntry->rssi_stat.UndecoratedSmoothedPWDB));
					RT_TRACE(COMP_RATR, DBG_LOUD, ("RSSI_LEVEL = %d\n", pEntryRA->RATRState));
					RT_TRACE(COMP_RATR, DBG_LOUD, ("PreState = %d, CurState = %d\n", pEntryRA->PreRATRState, pEntryRA->RATRState));
					pAdapter->HalFunc.UpdateHalRAMaskHandler(
											pTargetAdapter,
											FALSE,
											pEntry->AID+1,
											pEntry->MacAddr,
											pEntry,
											pEntryRA->RATRState);
					pEntryRA->PreRATRState = pEntryRA->RATRState;
				}

			}
		}
	}
#endif	
}
#ifndef PLATFORM_FREEBSD
static VOID
dm_CheckProtection(
	IN	PADAPTER	Adapter
	)
{
#if 0
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	u1Byte			CurRate, RateThreshold;

	if(pMgntInfo->pHTInfo->bCurBW40MHz)
		RateThreshold = MGN_MCS1;
	else
		RateThreshold = MGN_MCS3;

	if(Adapter->TxStats.CurrentInitTxRate <= RateThreshold)
	{
		pMgntInfo->bDmDisableProtect = TRUE;
		DbgPrint("Forced disable protect: %x\n", Adapter->TxStats.CurrentInitTxRate);
	}
	else
	{
		pMgntInfo->bDmDisableProtect = FALSE;
		DbgPrint("Enable protect: %x\n", Adapter->TxStats.CurrentInitTxRate);
	}
#endif
}
#endif //PLATFORM_FREEBSD

static VOID
dm_CheckStatistics(
	IN	PADAPTER	Adapter
	)
{
#if 0
	if(!Adapter->MgntInfo.bMediaConnect)
		return;

	//2008.12.10 tynli Add for getting Current_Tx_Rate_Reg flexibly.
	rtw_hal_get_hwreg( Adapter, HW_VAR_INIT_TX_RATE, (pu1Byte)(&Adapter->TxStats.CurrentInitTxRate) );

	// Calculate current Tx Rate(Successful transmited!!)

	// Calculate current Rx Rate(Successful received!!)
	
	//for tx tx retry count
	rtw_hal_get_hwreg( Adapter, HW_VAR_RETRY_COUNT, (pu1Byte)(&Adapter->TxStats.NumTxRetryCount) );
#endif	
}

//
// Initialize GPIO setting registers
//
static void
dm_InitGPIOSetting(
	IN	PADAPTER	Adapter
	)
{
	u8	tmp1byte;
	
	tmp1byte = rtw_read8(Adapter, REG_GPIO_MUXCFG);
	tmp1byte &= (GPIOSEL_GPIO | ~GPIOSEL_ENBT);
	rtw_write8(Adapter, REG_GPIO_MUXCFG, tmp1byte);

}

//============================================================
// functions
//============================================================
void rtl8192d_init_dm_priv(IN PADAPTER Adapter)
{
	//PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	//struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	//_rtw_memset(pdmpriv, 0, sizeof(struct dm_priv));

}

void rtl8192d_deinit_dm_priv(IN PADAPTER Adapter)
{
	//PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	//struct dm_priv	*pdmpriv = &pHalData->dmpriv;

}

void
rtl8192d_InitHalDm(
	IN	PADAPTER	Adapter
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T odm = adapter_to_odm(Adapter);
	u8	i;

	pdmpriv->DM_Type = DM_Type_ByDriver;	
	pdmpriv->DMFlag = DYNAMIC_FUNC_DISABLE;
	pdmpriv->UndecoratedSmoothedPWDB = 0;
	//pdmpriv->UndecoratedSmoothedCCK = (-1);
	
	//.1 DIG INIT
	pdmpriv->bDMInitialGainEnable = _TRUE;
	pdmpriv->DMFlag |= DYNAMIC_FUNC_DIG;
	dm_DIGInit(Adapter);

	//.2 DynamicTxPower INIT
	pdmpriv->DMFlag |= DYNAMIC_FUNC_HP;
	dm_InitDynamicTxPower(Adapter);

	//.3
	DM_InitEdcaTurbo(Adapter);//moved to  linked_status_chk()

	//.4 RateAdaptive INIT
	dm_InitRateAdaptiveMask(Adapter);

	//.5 Tx Power Tracking Init.
	pdmpriv->DMFlag |= DYNAMIC_FUNC_SS;
	DM_InitializeTXPowerTracking(Adapter);

	dm_InitDynamicBBPowerSaving(Adapter);

	dm_InitGPIOSetting(Adapter);
	
	pdmpriv->DMFlag_tmp = pdmpriv->DMFlag;

	// Save REG_INIDATA_RATE_SEL value for TXDESC.
	for(i = 0 ; i<32 ; i++)
	{
		pdmpriv->INIDATA_RATE[i] = rtw_read8(Adapter, REG_INIDATA_RATE_SEL+i) & 0x3f;
	}

#ifdef CONFIG_ODM_ADAPTIVITY
	pdmpriv->DMFlag |= DYNAMIC_FUNC_ADAPTIVITY;
	odm_AdaptivityInit(odm);
#endif

	if(pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
		odm->write_dig = DM_Write_DIG_DMSP;
	else
		odm->write_dig = DM_Write_DIG;
}

#ifdef CONFIG_CONCURRENT_MODE
static void FindMinimumRSSI(PADAPTER Adapter)
{
	PHAL_DATA_TYPE	pbuddy_HalData;
	struct dm_priv *pbuddy_dmpriv;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;

	if(!rtw_buddy_adapter_up(Adapter))
		return;
	
	pbuddy_HalData = GET_HAL_DATA(pbuddy_adapter);
	pbuddy_dmpriv = &pbuddy_HalData->dmpriv;

	//get min. [PWDB] when both interfaces are connected
	if((check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE 
		&& Adapter->stapriv.asoc_sta_count > 2 
		&& check_buddy_fwstate(Adapter, _FW_LINKED)) || 
		(check_buddy_fwstate(Adapter, WIFI_AP_STATE) == _TRUE 
		&& pbuddy_adapter->stapriv.asoc_sta_count > 2
		&& check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_STATION_STATE) 
		&& check_fwstate(pmlmepriv, _FW_LINKED)
		&& check_buddy_fwstate(Adapter,WIFI_STATION_STATE) 
		&& check_buddy_fwstate(Adapter,_FW_LINKED)))
	{
		//select smaller PWDB
		if(pdmpriv->UndecoratedSmoothedPWDB > pbuddy_dmpriv->UndecoratedSmoothedPWDB)
			pdmpriv->UndecoratedSmoothedPWDB = pbuddy_dmpriv->UndecoratedSmoothedPWDB;
	}//secondary interface is connected
	else if((check_buddy_fwstate(Adapter, WIFI_AP_STATE) == _TRUE 
		&& pbuddy_adapter->stapriv.asoc_sta_count > 2) || 
		(check_buddy_fwstate(Adapter,WIFI_STATION_STATE) 
		&& check_buddy_fwstate(Adapter,_FW_LINKED)))
	{
		//select buddy PWDB
		pdmpriv->UndecoratedSmoothedPWDB = pbuddy_dmpriv->UndecoratedSmoothedPWDB;
	}
	//primary is connected
	else if((check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE 
		&& Adapter->stapriv.asoc_sta_count > 2) || 
		(check_fwstate(pmlmepriv, WIFI_STATION_STATE) 
		&& check_fwstate(pmlmepriv, _FW_LINKED)))
	{
		//set buddy PWDB to 0
		pbuddy_dmpriv->UndecoratedSmoothedPWDB = 0;
	}
	//both interfaces are not connected
	else
	{
		pdmpriv->UndecoratedSmoothedPWDB = 0;
		pbuddy_dmpriv->UndecoratedSmoothedPWDB = 0;
	}
		
	//primary interface is ap mode
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE && Adapter->stapriv.asoc_sta_count > 2)
	{
		pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB = 0;
	}//secondary interface is ap mode
	else if(check_buddy_fwstate(Adapter, WIFI_AP_STATE) == _TRUE && pbuddy_adapter->stapriv.asoc_sta_count > 2)
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB;
	}
	else //both interfaces are not ap mode
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB = 0;
	}
}
#endif //CONFIG_CONCURRENT_MODE

VOID
rtl8192d_HalDmWatchDog(
	IN	PADAPTER	Adapter
	)
{
	BOOLEAN		bFwCurrentInPSMode = _FALSE;
	BOOLEAN		bFwPSAwake = _TRUE;
	u8 hw_init_completed = _FALSE;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
#ifdef CONFIG_CONCURRENT_MODE
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
#endif //CONFIG_CONCURRENT_MODE

#ifdef CONFIG_DUALMAC_CONCURRENT
	if((pHalData->bInModeSwitchProcess))
	{
		DBG_871X("HalDmWatchDog(): During dual mac mode switch or slave mac \n");
		return;
	}
#endif

	#if defined(CONFIG_CONCURRENT_MODE)
	if (Adapter->isprimary == _FALSE && pbuddy_adapter) {
		hw_init_completed = pbuddy_adapter->hw_init_completed;
	} else
	#endif
	{
		hw_init_completed = Adapter->hw_init_completed;
	}

	if (hw_init_completed == _FALSE)
		goto skip_dm;

#ifdef CONFIG_LPS
	#if defined(CONFIG_CONCURRENT_MODE)
	if (Adapter->iface_type != IFACE_PORT0 && pbuddy_adapter) {
		bFwCurrentInPSMode = pbuddy_adapter->pwrctrlpriv.bFwCurrentInPSMode;
		rtw_hal_get_hwreg(pbuddy_adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&bFwPSAwake));
	} else
	#endif
	{
		bFwCurrentInPSMode = Adapter->pwrctrlpriv.bFwCurrentInPSMode;
		rtw_hal_get_hwreg(Adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&bFwPSAwake));
	}
#endif

#ifdef CONFIG_P2P_PS
	// Fw is under p2p powersaving mode, driver should stop dynamic mechanism.
	// modifed by thomas. 2011.06.11.
	if(Adapter->wdinfo.p2p_ps_mode)
		bFwPSAwake = _FALSE;
#endif // CONFIG_P2P_PS


	// Stop dynamic mechanism when:
	// 1. RF is OFF. (No need to do DM.)
	// 2. Fw is under power saving mode for FwLPS. (Prevent from SW/FW I/O racing.)
	// 3. IPS workitem is scheduled. (Prevent from IPS sequence to be swapped with DM.
	//     Sometimes DM execution time is longer than 100ms such that the assertion
	//     in MgntActSet_RF_State() called by InactivePsWorkItem will be triggered by 
	//     wating to long for RFChangeInProgress.)
	// 4. RFChangeInProgress is TRUE. (Prevent from broken by IPS/HW/SW Rf off.)
	// Noted by tynli. 2010.06.01.
	//if(rfState == eRfOn)
	if( (hw_init_completed == _TRUE) 
		&& ((!bFwCurrentInPSMode) && bFwPSAwake))
	{
		//
		// Calculate Tx/Rx statistics.
		//
		dm_CheckStatistics(Adapter);

		//
		// For PWDB monitor and record some value for later use.
		//
		PWDB_Monitor(Adapter);
#ifdef CONFIG_CONCURRENT_MODE
		if(Adapter->adapter_type > PRIMARY_ADAPTER)
			goto _record_initrate;
		FindMinimumRSSI(Adapter);
#endif //CONFIG_CONCURRENT_MODE
		//
		// Dynamic Initial Gain mechanism.
		//
//sherry delete flag 20110517		
#ifdef CONFIG_PCI_HCI
//		ACQUIRE_GLOBAL_SPINLOCK(&GlobalSpinlockForGlobalAdapterList);
#else
		ACQUIRE_GLOBAL_MUTEX(GlobalMutexForGlobalAdapterList);
#endif
		if(pHalData->bSlaveOfDMSP)
		{
			odm_FalseAlarmCounterStatistics_ForSlaveOfDMSP(Adapter);
		}
		else
		{
			odm_FalseAlarmCounterStatistics(Adapter);
		}
#ifdef CONFIG_PCI_HCI
//		RELEASE_GLOBAL_SPINLOCK(&GlobalSpinlockForGlobalAdapterList);
#else
		RELEASE_GLOBAL_MUTEX(GlobalMutexForGlobalAdapterList);
#endif
		//odm_RSSIMonitorCheck(Adapter);
#ifndef CONFIG_CONCURRENT_MODE
		odm_FindMinimumRSSI_92D(Adapter);
#endif //CONFIG_CONCURRENT_MODE
		odm_DIG(Adapter);
		odm_Adaptivity(adapter_to_odm(Adapter));
		rtw_dm_check_rxfifo_full(Adapter);
		//PHY_SetBBReg( Adapter, 0xC50, 0x7f, 0x32 );
		//PHY_SetBBReg( Adapter, 0xC58, 0x7f, 0x32 );
		if(pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
		{
			if(pHalData->CurrentBandType92D != BAND_ON_5G)
				dm_CCK_PacketDetectionThresh_DMSP(Adapter);
		}
		else
		{
			if(pHalData->CurrentBandType92D != BAND_ON_5G)
				dm_CCK_PacketDetectionThresh(Adapter);
		}

//#ifdef CONFIG_PCI_HCI
//		odm_DynamicEarlyMode(Adapter);
//#endif

		//
		// Dynamic Tx Power mechanism.
		//
		odm_DynamicTxPower_92D(Adapter);

		//
		// Tx Power Tracking.
		//
		//TX power tracking will make 92de DMDP MAC0's throughput bad.
#ifdef CONFIG_DUALMAC_CONCURRENT
		if(!pHalData->bSlaveOfDMSP || Adapter->DualMacConcurrent == _FALSE)
#endif
			rtl8192d_dm_CheckTXPowerTracking(Adapter);

		//
		// Rate Adaptive by Rx Signal Strength mechanism.
		//
		dm_RefreshRateAdaptiveMask(Adapter);

//#ifdef CONFIG_PCI_HCI
		//BT-Coexist
//		dm_BTCoexist(Adapter);
//#endif

#ifdef CONFIG_PCI_HCI
		// Tx Migration settings.
		//migration, 92d just for normal chip, vivi, 20100708
		//dm_InterruptMigration(Adapter);
#endif

		// EDCA turbo
		//update the EDCA paramter according to the Tx/RX mode
		//update_EDCA_param(Adapter);
		dm_CheckEdcaTurbo(Adapter);

		//
		// Dynamically switch RTS/CTS protection.
		//
		//dm_CheckProtection(Adapter);

		//
		//Dynamic BB Power Saving Mechanism
		//vivi, 20101014, to pass DTM item: softap_excludeunencrypted_ext.htm 
		//temporarily disable it advised for performance test by yn,2010-11-03.
		//if(!Adapter->bInHctTest)
			dm_DynamicBBPowerSaving(Adapter);

_record_initrate:

		// Read REG_INIDATA_RATE_SEL value for TXDESC.
		if(check_fwstate(&Adapter->mlmepriv, WIFI_STATION_STATE) == _TRUE)
		{
			pdmpriv->INIDATA_RATE[0] = rtw_read8(Adapter, REG_INIDATA_RATE_SEL) & 0x3f;

#ifdef CONFIG_TDLS
			if(Adapter->tdlsinfo.setup_state == TDLS_LINKED_STATE)
			{
				u8 i=1;
				for(; i < (Adapter->tdlsinfo.macid_index) ; i++)
				{
					pdmpriv->INIDATA_RATE[i] = rtw_read8(Adapter, (REG_INIDATA_RATE_SEL+i)) & 0x3f;
				}
			}
#endif //CONFIG_TDLS

		}
		else
		{
			u8	i;
			for(i=1 ; i < (Adapter->stapriv.asoc_sta_count + 1); i++)
			{
				pdmpriv->INIDATA_RATE[i] = rtw_read8(Adapter, (REG_INIDATA_RATE_SEL+i)) & 0x3f;
			}
		}
	}

skip_dm:

	// Check GPIO to determine current RF on/off and Pbc status.
	// Not enable for 92CU now!!!
	// Check Hardware Radio ON/OFF or not	
	//if(Adapter->MgntInfo.PowerSaveControl.bGpioRfSw)
	//{
		//RTPRINT(FPWR, PWRHW, ("dm_CheckRfCtrlGPIO \n"));
	//	dm_CheckRfCtrlGPIO(Adapter);
	//}
	
	dm_CheckPbcGPIO(Adapter);				// Add by hpfan 2008-03-11

	//RTPRINT(FDM, DM_Monitor, ("HalDmWatchDog() <==\n"));
}

