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
#include "Mp_Precomp.h"
#include "phydm_precomp.h"


VOID
odm_RSSIMonitorInit(
	IN		PVOID		pDM_VOID
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pRA_T		pRA_Table = &pDM_Odm->DM_RA_Table;
   	pRA_Table->firstconnect = FALSE;
	
#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	pRA_Table->PT_collision_pre = TRUE;   //used in ODM_DynamicARFBSelect(WIN only)
#endif	
#endif
}


VOID
odm_RSSIMonitorCheck(
	IN		PVOID		pDM_VOID
	)
{
	// 
	// For AP/ADSL use prtl8192cd_priv
	// For CE/NIC use PADAPTER
	//
PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	if (!(pDM_Odm->SupportAbility & ODM_BB_RSSI_MONITOR))
		return;
	
	//
	// 2011/09/29 MH In HW integration first stage, we provide 4 different handle to operate
	// at the same time. In the stage2/3, we need to prive universal interface and merge all
	// HW dynamic mechanism.
	//
	switch	(pDM_Odm->SupportPlatform)
	{
		case	ODM_WIN:
			odm_RSSIMonitorCheckMP(pDM_Odm);
			break;

		case	ODM_CE:
			odm_RSSIMonitorCheckCE(pDM_Odm);
			break;

		case	ODM_AP:
			odm_RSSIMonitorCheckAP(pDM_Odm);
			break;		

		case	ODM_ADSL:
			//odm_DIGAP(pDM_Odm);
			break;	
	}
	
}	// odm_RSSIMonitorCheck

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
odm_RSSIDumpToRegister(
	IN	PVOID	pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER		Adapter = pDM_Odm->Adapter;

	if(pDM_Odm->SupportICType == ODM_RTL8812)
	{
		PlatformEFIOWrite1Byte(Adapter, rA_RSSIDump_Jaguar, Adapter->RxStats.RxRSSIPercentage[0]);
		PlatformEFIOWrite1Byte(Adapter, rB_RSSIDump_Jaguar, Adapter->RxStats.RxRSSIPercentage[1]);

		// Rx EVM
		PlatformEFIOWrite1Byte(Adapter, rS1_RXevmDump_Jaguar, Adapter->RxStats.RxEVMdbm[0]);
		PlatformEFIOWrite1Byte(Adapter, rS2_RXevmDump_Jaguar, Adapter->RxStats.RxEVMdbm[1]);

		// Rx SNR
		PlatformEFIOWrite1Byte(Adapter, rA_RXsnrDump_Jaguar, (u1Byte)(Adapter->RxStats.RxSNRdB[0]));
		PlatformEFIOWrite1Byte(Adapter, rB_RXsnrDump_Jaguar, (u1Byte)(Adapter->RxStats.RxSNRdB[1]));

		// Rx Cfo_Short
		PlatformEFIOWrite2Byte(Adapter, rA_CfoShortDump_Jaguar, Adapter->RxStats.RxCfoShort[0]);
		PlatformEFIOWrite2Byte(Adapter, rB_CfoShortDump_Jaguar, Adapter->RxStats.RxCfoShort[1]);

		// Rx Cfo_Tail
		PlatformEFIOWrite2Byte(Adapter, rA_CfoLongDump_Jaguar, Adapter->RxStats.RxCfoTail[0]);
		PlatformEFIOWrite2Byte(Adapter, rB_CfoLongDump_Jaguar, Adapter->RxStats.RxCfoTail[1]);
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8192E)
	{
		PlatformEFIOWrite1Byte(Adapter, rA_RSSIDump_92E, Adapter->RxStats.RxRSSIPercentage[0]);
		PlatformEFIOWrite1Byte(Adapter, rB_RSSIDump_92E, Adapter->RxStats.RxRSSIPercentage[1]);
		// Rx EVM
		PlatformEFIOWrite1Byte(Adapter, rS1_RXevmDump_92E, Adapter->RxStats.RxEVMdbm[0]);
		PlatformEFIOWrite1Byte(Adapter, rS2_RXevmDump_92E, Adapter->RxStats.RxEVMdbm[1]);
		// Rx SNR
		PlatformEFIOWrite1Byte(Adapter, rA_RXsnrDump_92E, (u1Byte)(Adapter->RxStats.RxSNRdB[0]));
		PlatformEFIOWrite1Byte(Adapter, rB_RXsnrDump_92E, (u1Byte)(Adapter->RxStats.RxSNRdB[1]));
		// Rx Cfo_Short
		PlatformEFIOWrite2Byte(Adapter, rA_CfoShortDump_92E, Adapter->RxStats.RxCfoShort[0]);
		PlatformEFIOWrite2Byte(Adapter, rB_CfoShortDump_92E, Adapter->RxStats.RxCfoShort[1]);
		// Rx Cfo_Tail
		PlatformEFIOWrite2Byte(Adapter, rA_CfoLongDump_92E, Adapter->RxStats.RxCfoTail[0]);
		PlatformEFIOWrite2Byte(Adapter, rB_CfoLongDump_92E, Adapter->RxStats.RxCfoTail[1]);
	 }
}
#endif

VOID
odm_RSSIMonitorCheckMP(
	IN	PVOID	pDM_VOID
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PRT_WLAN_STA	pEntry = NULL;
	u1Byte			i;
	s4Byte			tmpEntryMaxPWDB=0, tmpEntryMinPWDB=0xff;
	u1Byte			H2C_Parameter[4] ={0};
	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	PMGNT_INFO		pDefaultMgntInfo = &Adapter->MgntInfo;
	u8Byte			curTxOkCnt = 0, curRxOkCnt = 0;	
	u1Byte			STBC_TX = 0;
	BOOLEAN			FirstConnect;                                                    
	pRA_T			pRA_Table = &pDM_Odm->DM_RA_Table; 
	pDIG_T			pDM_DigTable = &pDM_Odm->DM_DigTable;

#if (BEAMFORMING_SUPPORT == 1)	
	BEAMFORMING_CAP Beamform_cap = BEAMFORMING_CAP_NONE;
	u1Byte			TxBF_EN = 0;
#endif

	PADAPTER	pLoopAdapter = GetDefaultAdapter(Adapter);

	BOOLEAN		bExtRAInfo = FALSE;

	if(pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8812 || pDM_Odm->SupportICType == ODM_RTL8821 || pDM_Odm->SupportICType == ODM_RTL8723B)
		bExtRAInfo = TRUE;

	FirstConnect = (pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == FALSE);
	pRA_Table->firstconnect = pHalData->bLinked;                                               
       H2C_Parameter[3] |= FirstConnect << 5;

	if(pDM_Odm->SupportICType == ODM_RTL8188E && (pDefaultMgntInfo->CustomerID==RT_CID_819x_HP))
	{
		if(curRxOkCnt >(curTxOkCnt*6))
			PlatformEFIOWrite4Byte(Adapter, REG_ARFR0, 0x8f015);
		else
			PlatformEFIOWrite4Byte(Adapter, REG_ARFR0, 0xff015);
	}	

	if(pDM_Odm->SupportICType == ODM_RTL8812 || pDM_Odm->SupportICType == ODM_RTL8821)
	{
		if(curRxOkCnt >(curTxOkCnt*6))
			H2C_Parameter[3]=0x01;
		else
			H2C_Parameter[3]=0x00;
	}

	while(pLoopAdapter)
	{		
	
		if(pLoopAdapter != NULL){
			pMgntInfo = &pLoopAdapter->MgntInfo;			
			curTxOkCnt = pLoopAdapter->TxStats.NumTxBytesUnicast - pMgntInfo->lastTxOkCnt;
			curRxOkCnt = pLoopAdapter->RxStats.NumRxBytesUnicast - pMgntInfo->lastRxOkCnt;
			pMgntInfo->lastTxOkCnt = curTxOkCnt;
			pMgntInfo->lastRxOkCnt = curRxOkCnt;			
		}

		for(i = 0; i < ASSOCIATE_ENTRY_NUM; i++)
		{
		
			if(IsAPModeExist(pLoopAdapter))
			{
				if(GetFirstExtAdapter(pLoopAdapter) != NULL && 
					GetFirstExtAdapter(pLoopAdapter) == pLoopAdapter){	
					pEntry = AsocEntry_EnumStation(pLoopAdapter, i);		
				}
				else if(GetFirstGOPort(pLoopAdapter) != NULL && 
					IsFirstGoAdapter(pLoopAdapter)){
					pEntry = AsocEntry_EnumStation(pLoopAdapter, i);						
				}				
			}
			else
			{
					if(GetDefaultAdapter(pLoopAdapter) == pLoopAdapter){
						pEntry = AsocEntry_EnumStation(pLoopAdapter, i);					
					}
			}

		if(pEntry != NULL)
		{
			if(pEntry->bAssociated)
			{
			
				RT_DISP_ADDR(FDM, DM_PWDB, ("pEntry->MacAddr ="), pEntry->MacAddr);
				RT_DISP(FDM, DM_PWDB, ("pEntry->rssi = 0x%x(%d)\n", 
					pEntry->rssi_stat.UndecoratedSmoothedPWDB, pEntry->rssi_stat.UndecoratedSmoothedPWDB));

				if(bExtRAInfo)
				{

#if (BEAMFORMING_SUPPORT == 1)
					Beamform_cap = Beamforming_GetEntryBeamCapByMacId(Adapter, pEntry->AssociatedMacId);
					if(Beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT |BEAMFORMER_CAP_VHT_SU))
						TxBF_EN = 1;
					else
						TxBF_EN = 0;
	
					H2C_Parameter[3] |= TxBF_EN << 6; 
					
					if(TxBF_EN)
						STBC_TX = 0;
					else
#endif
					{
						if(IS_WIRELESS_MODE_AC(Adapter))
							STBC_TX = TEST_FLAG(pEntry->VHTInfo.STBC, STBC_VHT_ENABLE_TX);
						else
							STBC_TX = TEST_FLAG(pEntry->HTInfo.STBC, STBC_HT_ENABLE_TX);
					}

					H2C_Parameter[3] |= STBC_TX << 1;
				}

				if(pEntry->rssi_stat.UndecoratedSmoothedPWDB < tmpEntryMinPWDB)
					tmpEntryMinPWDB = pEntry->rssi_stat.UndecoratedSmoothedPWDB;
				if(pEntry->rssi_stat.UndecoratedSmoothedPWDB > tmpEntryMaxPWDB)
					tmpEntryMaxPWDB = pEntry->rssi_stat.UndecoratedSmoothedPWDB;

				H2C_Parameter[2] = (u1Byte)(pEntry->rssi_stat.UndecoratedSmoothedPWDB & 0xFF);
				H2C_Parameter[1] = 0x20;   // fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1
				H2C_Parameter[0] = (pEntry->AssociatedMacId);
				if(bExtRAInfo)
					ODM_FillH2CCmd(pDM_Odm, ODM_H2C_RSSI_REPORT, 4, H2C_Parameter);
				else
					ODM_FillH2CCmd(pDM_Odm, ODM_H2C_RSSI_REPORT, 3, H2C_Parameter);
			}
		}
		else
		{
			break;
		}
	}

		pLoopAdapter = GetNextExtAdapter(pLoopAdapter);
	}

	if(tmpEntryMaxPWDB != 0)	// If associated entry is found
	{
		pHalData->EntryMaxUndecoratedSmoothedPWDB = tmpEntryMaxPWDB;
		RT_DISP(FDM, DM_PWDB, ("EntryMaxPWDB = 0x%x(%d)\n",	tmpEntryMaxPWDB, tmpEntryMaxPWDB));
	}
	else
	{
		pHalData->EntryMaxUndecoratedSmoothedPWDB = 0;
	}
	
	if(tmpEntryMinPWDB != 0xff) // If associated entry is found
	{
		pHalData->EntryMinUndecoratedSmoothedPWDB = tmpEntryMinPWDB;
		RT_DISP(FDM, DM_PWDB, ("EntryMinPWDB = 0x%x(%d)\n", tmpEntryMinPWDB, tmpEntryMinPWDB));

	}
	else
	{
		pHalData->EntryMinUndecoratedSmoothedPWDB = 0;
	}

	// Indicate Rx signal strength to FW.
	if(pHalData->bUseRAMask)
	{
		if(bExtRAInfo)
		{
			PRT_HIGH_THROUGHPUT 		pHTInfo = GET_HT_INFO(pDefaultMgntInfo);
			PRT_VERY_HIGH_THROUGHPUT	pVHTInfo = GET_VHT_INFO(pDefaultMgntInfo);

#if (BEAMFORMING_SUPPORT == 1)
			
			Beamform_cap = Beamforming_GetEntryBeamCapByMacId(Adapter, pDefaultMgntInfo->mMacId);

			if(Beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT |BEAMFORMER_CAP_VHT_SU))
				TxBF_EN = 1;
			else
				TxBF_EN = 0;

			H2C_Parameter[3] |= TxBF_EN << 6; 

			if(TxBF_EN)
				STBC_TX = 0;
			else
#endif
			{
				if(IS_WIRELESS_MODE_AC(Adapter))
					STBC_TX = TEST_FLAG(pVHTInfo->VhtCurStbc, STBC_VHT_ENABLE_TX);
				else
					STBC_TX = TEST_FLAG(pHTInfo->HtCurStbc, STBC_HT_ENABLE_TX);
			}

			H2C_Parameter[3] |= STBC_TX << 1;
		}
		
		H2C_Parameter[2] = (u1Byte)(pHalData->UndecoratedSmoothedPWDB & 0xFF);
		H2C_Parameter[1] = 0x20;	// fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1
		H2C_Parameter[0] = 0;		// fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1
		if(bExtRAInfo)
			ODM_FillH2CCmd(pDM_Odm, ODM_H2C_RSSI_REPORT, 4, H2C_Parameter);
		else
			ODM_FillH2CCmd(pDM_Odm, ODM_H2C_RSSI_REPORT, 3, H2C_Parameter);
		
		// BT 3.0 HS mode Rssi
		if(pDM_Odm->bBtHsOperation)
		{
			H2C_Parameter[2] = pDM_Odm->btHsRssi;
			H2C_Parameter[1] = 0x0;
			H2C_Parameter[0] = 2;
			if(bExtRAInfo)
				ODM_FillH2CCmd(pDM_Odm, ODM_H2C_RSSI_REPORT, 4, H2C_Parameter);
			else	
				ODM_FillH2CCmd(pDM_Odm, ODM_H2C_RSSI_REPORT, 3, H2C_Parameter);
		}
	}
	else
	{
		PlatformEFIOWrite1Byte(Adapter, 0x4fe, (u1Byte)pHalData->UndecoratedSmoothedPWDB);
	}

	if((pDM_Odm->SupportICType == ODM_RTL8812)||(pDM_Odm->SupportICType == ODM_RTL8192E))
		odm_RSSIDumpToRegister(pDM_Odm);
		

	{
		PADAPTER pLoopAdapter = GetDefaultAdapter(Adapter);
		s4Byte	GlobalRSSI_min = 0xFF, LocalRSSI_Min;
		BOOLEAN		bLink= FALSE;
		
		while(pLoopAdapter)
		{
			LocalRSSI_Min = odm_FindMinimumRSSI(pLoopAdapter);
			//DbgPrint("pHalData->bLinked=%d, LocalRSSI_Min=%d\n", pHalData->bLinked, LocalRSSI_Min);
			if((LocalRSSI_Min < GlobalRSSI_min) && (LocalRSSI_Min != 0))
				GlobalRSSI_min = LocalRSSI_Min;			
			
			if(pHalData->bLinked)
				bLink = TRUE;
			
			pLoopAdapter = GetNextExtAdapter(pLoopAdapter);
		}

		pHalData->bLinked = bLink;
		ODM_CmnInfoUpdate(&pHalData->DM_OutSrc ,ODM_CMNINFO_LINK, (u8Byte)bLink);
		ODM_CmnInfoUpdate(&pHalData->DM_OutSrc ,ODM_CMNINFO_RSSI_MIN, (u8Byte)GlobalRSSI_min);
		
	}
	
#endif	// #if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
}

#if(DM_ODM_SUPPORT_TYPE==ODM_CE)
//
//sherry move from DUSC to here 20110517
//
static VOID
FindMinimumRSSI_Dmsp(
	IN	PADAPTER	pAdapter
)
{
#if 0
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	s32	Rssi_val_min_back_for_mac0;
	BOOLEAN		bGetValueFromBuddyAdapter = dm_DualMacGetParameterFromBuddyAdapter(pAdapter);
	BOOLEAN		bRestoreRssi = _FALSE;
	PADAPTER	BuddyAdapter = pAdapter->BuddyAdapter;

	if(pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
	{
		if(BuddyAdapter!= NULL)
		{
			if(pHalData->bSlaveOfDMSP)
			{
				//ODM_RT_TRACE(pDM_Odm,COMP_EASY_CONCURRENT,DBG_LOUD,("bSlavecase of dmsp\n"));
				BuddyAdapter->DualMacDMSPControl.RssiValMinForAnotherMacOfDMSP = pdmpriv->MinUndecoratedPWDBForDM;
			}
			else
			{
				if(bGetValueFromBuddyAdapter)
				{
					//ODM_RT_TRACE(pDM_Odm,COMP_EASY_CONCURRENT,DBG_LOUD,("get new RSSI\n"));
					bRestoreRssi = _TRUE;
					Rssi_val_min_back_for_mac0 = pdmpriv->MinUndecoratedPWDBForDM;
					pdmpriv->MinUndecoratedPWDBForDM = pAdapter->DualMacDMSPControl.RssiValMinForAnotherMacOfDMSP;
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
FindMinimumRSSI(
IN	PADAPTER	pAdapter
	)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;	
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);

	//1 1.Determine the minimum RSSI 

	if((pDM_Odm->bLinked != _TRUE) &&
		(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		pdmpriv->MinUndecoratedPWDBForDM = 0;
		//ODM_RT_TRACE(pDM_Odm,COMP_BB_POWERSAVING, DBG_LOUD, ("Not connected to any \n"));
	}
	else
	{
		pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
	}

	//DBG_8192C("%s=>MinUndecoratedPWDBForDM(%d)\n",__FUNCTION__,pdmpriv->MinUndecoratedPWDBForDM);
	//ODM_RT_TRACE(pDM_Odm,COMP_DIG, DBG_LOUD, ("MinUndecoratedPWDBForDM =%d\n",pHalData->MinUndecoratedPWDBForDM));
}
#endif

VOID
odm_RSSIMonitorCheckCE(
	IN		PVOID		pDM_VOID
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER	Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(Adapter);
	int	i;
	int	tmpEntryMaxPWDB=0, tmpEntryMinPWDB=0xff;
	u8 	sta_cnt=0;
	u32	UL_DL_STATE = 0, STBC_TX = 0, TxBF_EN = 0;
	u32	PWDB_rssi[NUM_STA]={0};//[0~15]:MACID, [16~31]:PWDB_rssi
	BOOLEAN			FirstConnect = FALSE;
	pRA_T			pRA_Table = &pDM_Odm->DM_RA_Table;

	if(pDM_Odm->bLinked != _TRUE)
		return;

	#if((RTL8812A_SUPPORT==1)||(RTL8821A_SUPPORT==1))
	if((pDM_Odm->SupportICType == ODM_RTL8812)||(pDM_Odm->SupportICType == ODM_RTL8821))
	{
		u64	curTxOkCnt = pdvobjpriv->traffic_stat.cur_tx_bytes;
		u64	curRxOkCnt = pdvobjpriv->traffic_stat.cur_rx_bytes;

		if(curRxOkCnt >(curTxOkCnt*6))
			UL_DL_STATE = 1;
		else
			UL_DL_STATE = 0;
	}
	#endif

       FirstConnect = (pDM_Odm->bLinked) && (pRA_Table->firstconnect == FALSE);    
	pRA_Table->firstconnect = pDM_Odm->bLinked;

	//if(check_fwstate(&Adapter->mlmepriv, WIFI_AP_STATE|WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE)
	{
		#if 1
		struct sta_info *psta;
		
		for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++) {
			if (IS_STA_VALID(psta = pDM_Odm->pODM_StaInfo[i]))
			{
                        		if(IS_MCAST( psta->hwaddr))  //if(psta->mac_id ==1)
						 continue;
								
					if(psta->rssi_stat.UndecoratedSmoothedPWDB == (-1))
						 continue;
								
					if(psta->rssi_stat.UndecoratedSmoothedPWDB < tmpEntryMinPWDB)
						tmpEntryMinPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					if(psta->rssi_stat.UndecoratedSmoothedPWDB > tmpEntryMaxPWDB)
						tmpEntryMaxPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					#if 0
					DBG_871X("%s mac_id:%u, mac:"MAC_FMT", rssi:%d\n", __func__,
						psta->mac_id, MAC_ARG(psta->hwaddr), psta->rssi_stat.UndecoratedSmoothedPWDB);
					#endif

					if(psta->rssi_stat.UndecoratedSmoothedPWDB != (-1)) {

#ifdef CONFIG_80211N_HT
						if(pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8812)
						{
#ifdef CONFIG_BEAMFORMING
							BEAMFORMING_CAP Beamform_cap = beamforming_get_entry_beam_cap_by_mac_id(&Adapter->mlmepriv, psta->mac_id);

							if(Beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT |BEAMFORMER_CAP_VHT_SU))
								TxBF_EN = 1;
							else
								TxBF_EN = 0;

							if (TxBF_EN) {
								STBC_TX = 0;
							}
							else
#endif
							{
#ifdef CONFIG_80211AC_VHT
								if(IsSupportedVHT(psta->wireless_mode))
									STBC_TX = TEST_FLAG(psta->vhtpriv.stbc_cap, STBC_VHT_ENABLE_TX);
								else	
#endif
									STBC_TX = TEST_FLAG(psta->htpriv.stbc_cap, STBC_HT_ENABLE_TX);
							}
						}
#endif

						if(pDM_Odm->SupportICType == ODM_RTL8192D)
							PWDB_rssi[sta_cnt++] = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16) | ((Adapter->stapriv.asoc_sta_count+1) << 8));
						else if ((pDM_Odm->SupportICType == ODM_RTL8192E)||(pDM_Odm->SupportICType == ODM_RTL8812)||(pDM_Odm->SupportICType == ODM_RTL8821))
							PWDB_rssi[sta_cnt++] = (((u8)(psta->mac_id&0xFF)) | ((psta->rssi_stat.UndecoratedSmoothedPWDB&0x7F)<<16) |(STBC_TX << 25) | (FirstConnect << 29) | (TxBF_EN << 30));
						else
							PWDB_rssi[sta_cnt++] = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16) );
					}
			}
		}
		#else
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

				if(_rtw_memcmp(psta->hwaddr, bcast_addr, ETH_ALEN) || 
					_rtw_memcmp(psta->hwaddr, myid(&Adapter->eeprompriv), ETH_ALEN))
					continue;

				if(psta->state & WIFI_ASOC_STATE)
				{
					
					if(psta->rssi_stat.UndecoratedSmoothedPWDB < tmpEntryMinPWDB)
						tmpEntryMinPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					if(psta->rssi_stat.UndecoratedSmoothedPWDB > tmpEntryMaxPWDB)
						tmpEntryMaxPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					if(psta->rssi_stat.UndecoratedSmoothedPWDB != (-1)){
						//printk("%s==> mac_id(%d),rssi(%d)\n",__FUNCTION__,psta->mac_id,psta->rssi_stat.UndecoratedSmoothedPWDB);
						#if(RTL8192D_SUPPORT==1)
						PWDB_rssi[sta_cnt++] = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16) | ((Adapter->stapriv.asoc_sta_count+1) << 8));
						#else
						PWDB_rssi[sta_cnt++] = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16) );
						#endif
					}
				}
			
			}

		}
	
		_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);
		#endif

		//printk("%s==> sta_cnt(%d)\n",__FUNCTION__,sta_cnt);

		for(i=0; i< sta_cnt; i++)
		{
			if(PWDB_rssi[i] != (0)){
				if(pHalData->fw_ractrl == _TRUE)// Report every sta's RSSI to FW
				{
					#if(RTL8192D_SUPPORT==1)
					if(pDM_Odm->SupportICType == ODM_RTL8192D){
						FillH2CCmd92D(Adapter, H2C_RSSI_REPORT, 3, (u8 *)(&PWDB_rssi[i]));		
					}
					#endif
					
					#if((RTL8192C_SUPPORT==1)||(RTL8723A_SUPPORT==1))
					if((pDM_Odm->SupportICType == ODM_RTL8192C)||(pDM_Odm->SupportICType == ODM_RTL8723A)){
						rtl8192c_set_rssi_cmd(Adapter, (u8*)&PWDB_rssi[i]);
					}
					#endif
					
					#if((RTL8812A_SUPPORT==1)||(RTL8821A_SUPPORT==1))
					if((pDM_Odm->SupportICType == ODM_RTL8812)||(pDM_Odm->SupportICType == ODM_RTL8821)){	
						PWDB_rssi[i] |= (UL_DL_STATE << 24);
						rtl8812_set_rssi_cmd(Adapter, (u8 *)(&PWDB_rssi[i]));
					}
					#endif
					#if(RTL8192E_SUPPORT==1)
					if(pDM_Odm->SupportICType == ODM_RTL8192E){
						rtl8192e_set_rssi_cmd(Adapter, (u8 *)(&PWDB_rssi[i]));
					}
					#endif
					#if(RTL8723B_SUPPORT==1)
					if(pDM_Odm->SupportICType == ODM_RTL8723B){
						rtl8723b_set_rssi_cmd(Adapter, (u8 *)(&PWDB_rssi[i]));
					}
					#endif

					#if(RTL8188E_SUPPORT==1)
					if(pDM_Odm->SupportICType == ODM_RTL8188E){
						rtl8188e_set_rssi_cmd(Adapter, (u8 *)(&PWDB_rssi[i]));
					}
					#endif
										
				}
				else{
					#if((RTL8188E_SUPPORT==1)&&(RATE_ADAPTIVE_SUPPORT == 1))
					if(pDM_Odm->SupportICType == ODM_RTL8188E){
						ODM_RA_SetRSSI_8188E(
						&(pHalData->odmpriv), (PWDB_rssi[i]&0xFF), (u8)((PWDB_rssi[i]>>16) & 0xFF));
					}
					#endif
				}
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

	FindMinimumRSSI(Adapter);//get pdmpriv->MinUndecoratedPWDBForDM

	#if(RTL8192D_SUPPORT==1)
	FindMinimumRSSI_Dmsp(Adapter);
	#endif
	pDM_Odm->RSSI_Min = pdmpriv->MinUndecoratedPWDBForDM;
	//ODM_CmnInfoUpdate(&pHalData->odmpriv ,ODM_CMNINFO_RSSI_MIN, pdmpriv->MinUndecoratedPWDBForDM);
#endif//if (DM_ODM_SUPPORT_TYPE == ODM_CE)
}


VOID
odm_RSSIMonitorCheckAP(
	IN		PVOID		pDM_VOID
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
#if defined(CONFIG_RTL_92C_SUPPORT) || defined(CONFIG_RTL_92D_SUPPORT) ||defined(CONFIG_RTL_8812_SUPPORT)||defined(CONFIG_WLAN_HAL_8881A)||defined(CONFIG_WLAN_HAL_8192EE)
	{
		PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
		prtl8192cd_priv	priv		= pDM_Odm->priv;		
		u4Byte i;
		PSTA_INFO_T pstat;
		static u1Byte H2C_Parameter[5];
		u1Byte	  TxBF_EN = 0;
		pBDC_T	pDM_BdcTable = &pDM_Odm->DM_BdcTable;

		if( priv->up_time % 2 )
			return;

		pDM_BdcTable->num_Txbfee_Client=0;
		pDM_BdcTable->num_Txbfer_Client=0;
		//pDM_BdcTable->num_Client=0;
		
		for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
		{
			pstat = pDM_Odm->pODM_StaInfo[i];
			if(IS_STA_VALID(pstat) )
			{			
#ifdef BEAMFORMING_SUPPORT
				BEAMFORMING_CAP Beamform_cap = Beamforming_GetEntryBeamCapByMacId(priv, pstat->aid);
				if(Beamform_cap == BEAMFORMER_CAP_HT_EXPLICIT || Beamform_cap == BEAMFORMER_CAP_VHT_SU ||
					 Beamform_cap == (BEAMFORMER_CAP_HT_EXPLICIT|BEAMFORMEE_CAP_HT_EXPLICIT) ||
					 Beamform_cap == (BEAMFORMER_CAP_VHT_SU|BEAMFORMEE_CAP_VHT_SU))
				{
					TxBF_EN = (1<< 6);
					pDM_BdcTable->w_BFee_Client[i]=1; //AP act as BFer
					pDM_BdcTable->num_Txbfee_Client++;
				}
				else 
				{
					pDM_BdcTable->w_BFee_Client[i]=0; //AP act as BFer
				}
				
				if((Beamform_cap & BEAMFORMEE_CAP_HT_EXPLICIT) || (Beamform_cap & BEAMFORMEE_CAP_VHT_SU) )
				{
					pDM_BdcTable->w_BFer_Client[i]=1; //AP act as BFee
					pDM_BdcTable->num_Txbfer_Client++;
				}
				else 
				{
					pDM_BdcTable->w_BFer_Client[i]=0; //AP act as BFer
				}

					
				//pDM_BdcTable->num_Client++;
		
				
				
#endif			
//#ifdef STA_EXT
//				if (GET_CHIP_VER(priv)==VERSION_8812E && REMAP_AID(pstat) < (RTL8812_NUM_STAT - 1))
//#endif
				{
#ifdef CONFIG_RTL_8812_SUPPORT
#ifdef STA_EXT
					if(REMAP_AID(pstat) < (RTL8812_NUM_STAT - 1))
#endif					

					if(pDM_Odm->SupportICType == ODM_RTL8812) {
						memset(H2C_Parameter,0,5);						
						H2C_Parameter[2] = (u1Byte)(pstat->rssi & 0x7F);
						H2C_Parameter[0] = REMAP_AID(pstat);				
						if ((priv->pmib->dot11nConfigEntry.dot11nSTBC) && (
							(pstat->ht_cap_buf.ht_cap_info & cpu_to_le16(_HTCAP_RX_STBC_CAP_)
#ifdef RTK_AC_SUPPORT			
							|| (pstat->vht_cap_buf.vht_cap_info & cpu_to_le32(_VHTCAP_RX_STBC_CAP_))
#endif
							)))	
							H2C_Parameter[3] |= 2;
						H2C_Parameter[3] |= TxBF_EN ; 
						FillH2CCmd8812(pDM_Odm->priv, H2C_8812_RSSI_REPORT, 4, H2C_Parameter);
					}
#endif
				}
//#ifdef STA_EXT
//				else if (GET_CHIP_VER(priv)!=VERSION_8812E && REMAP_AID(pstat) < (FW_NUM_STAT - 1)) 
//#endif
				{
#if defined(CONFIG_WLAN_HAL_8881A) || defined(CONFIG_WLAN_HAL_8192EE)
#ifdef STA_EXT
					if(REMAP_AID(pstat) < (RTL8812_NUM_STAT - 1))
#endif	
					if(pDM_Odm->SupportICType == ODM_RTL8881A || pDM_Odm->SupportICType == ODM_RTL8192E) {
//						u1Byte	H2C_Parameter[5] ={0};	
						u1Byte	cmdlen = 3;
						memset(H2C_Parameter, 0, 5);
						H2C_Parameter[2] = (u1Byte)(pstat->rssi & 0xFF);
						H2C_Parameter[0] = REMAP_AID(pstat);
						if(pDM_Odm->SupportICType == ODM_RTL8192E) {
							cmdlen = 4;
							if ((priv->pmib->dot11nConfigEntry.dot11nSTBC) && (pstat->ht_cap_buf.ht_cap_info & cpu_to_le16(_HTCAP_RX_STBC_CAP_)))	
								H2C_Parameter[3] |= 2;		
							 H2C_Parameter[3] |= TxBF_EN; 

						} 
                        GET_HAL_INTERFACE(pDM_Odm->priv)->FillH2CCmdHandler(pDM_Odm->priv, H2C_88XX_RSSI_REPORT, cmdlen, H2C_Parameter);
					}
#endif
				
#if defined(CONFIG_RTL_92C_SUPPORT) || defined(CONFIG_RTL_92D_SUPPORT)	
#ifdef STA_EXT
					if(REMAP_AID(pstat) < (FW_NUM_STAT - 1))
#endif
					if(pDM_Odm->SupportICType == ODM_RTL8192C || pDM_Odm->SupportICType == ODM_RTL8192D) 
						add_update_rssi(pDM_Odm->priv, pstat);
#endif
				}

			}		
		}
	}
#endif
#endif

}


VOID
odm_RateAdaptiveMaskInit(
	IN	PVOID	pDM_VOID	
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PODM_RATE_ADAPTIVE	pOdmRA = &pDM_Odm->RateAdaptive;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PMGNT_INFO		pMgntInfo = &pDM_Odm->Adapter->MgntInfo;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pDM_Odm->Adapter);

	pMgntInfo->Ratr_State = DM_RATR_STA_INIT;

	if (pMgntInfo->DM_Type == DM_Type_ByDriver)
		pHalData->bUseRAMask = TRUE;
	else
		pHalData->bUseRAMask = FALSE;	

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	pOdmRA->Type = DM_Type_ByDriver;
	if (pOdmRA->Type == DM_Type_ByDriver)
		pDM_Odm->bUseRAMask = _TRUE;
	else
		pDM_Odm->bUseRAMask = _FALSE;	
#endif

	pOdmRA->RATRState = DM_RATR_STA_INIT;
	
#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	if(pDM_Odm->SupportICType == ODM_RTL8812)
		pOdmRA->LdpcThres = 50;		
	else
		pOdmRA->LdpcThres = 35;
		
	pOdmRA->RtsThres = 35;
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	pOdmRA->LdpcThres = 35;
	pOdmRA->bUseLdpc = FALSE;
	
#else
	pOdmRA->UltraLowRSSIThresh = 9;	
	
#endif

	pOdmRA->HighRSSIThresh = 50;
	pOdmRA->LowRSSIThresh = 20;
}
/*-----------------------------------------------------------------------------
 * Function:	odm_RefreshRateAdaptiveMask()
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
VOID
odm_RefreshRateAdaptiveMask(
	IN	PVOID	pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("odm_RefreshRateAdaptiveMask()---------->\n"));	
	if (!(pDM_Odm->SupportAbility & ODM_BB_RA_MASK))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("odm_RefreshRateAdaptiveMask(): Return cos not supported\n"));
		return;	
	}
	//
	// 2011/09/29 MH In HW integration first stage, we provide 4 different handle to operate
	// at the same time. In the stage2/3, we need to prive universal interface and merge all
	// HW dynamic mechanism.
	//
	switch	(pDM_Odm->SupportPlatform)
	{
		case	ODM_WIN:
			odm_RefreshRateAdaptiveMaskMP(pDM_Odm);
			break;

		case	ODM_CE:
			odm_RefreshRateAdaptiveMaskCE(pDM_Odm);
			break;

		case	ODM_AP:
		case	ODM_ADSL:
			odm_RefreshRateAdaptiveMaskAPADSL(pDM_Odm);
			break;
	}
	
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
odm_RefreshLdpcRtsMP(
	IN	PADAPTER			pAdapter,
	IN	PDM_ODM_T			pDM_Odm,
	IN	u1Byte				mMacId,
	IN	u1Byte				IOTPeer,
	IN	s4Byte				UndecoratedSmoothedPWDB	
	)
{
	BOOLEAN					bCtlLdpc = FALSE;
	PMGNT_INFO				pMgntInfo = GetDefaultMgntInfo(pAdapter);
	PODM_RATE_ADAPTIVE		pRA = &pDM_Odm->RateAdaptive;

	if(pDM_Odm->SupportICType != ODM_RTL8821 && pDM_Odm->SupportICType != ODM_RTL8812)
		return;

	if((pDM_Odm->SupportICType == ODM_RTL8821) && (pDM_Odm->CutVersion == ODM_CUT_A))
		bCtlLdpc = TRUE;
	else if(	pDM_Odm->SupportICType == ODM_RTL8812 && 
			IOTPeer == HT_IOT_PEER_REALTEK_JAGUAR_CCUTAP)
				bCtlLdpc = TRUE;

	if(bCtlLdpc)
	{
		if(UndecoratedSmoothedPWDB < (pRA->LdpcThres-5))
			MgntSet_TX_LDPC(pAdapter, mMacId, TRUE);
		else if(UndecoratedSmoothedPWDB > pRA->LdpcThres)
			MgntSet_TX_LDPC(pAdapter, mMacId, FALSE);
	}	

	if(UndecoratedSmoothedPWDB < (pRA->RtsThres-5))
		pRA->bLowerRtsRate = TRUE;
	else if(UndecoratedSmoothedPWDB > pRA->RtsThres)
		pRA->bLowerRtsRate = FALSE;
}
#endif


VOID
odm_RefreshRateAdaptiveMaskMP(
	IN		PVOID		pDM_VOID
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER				pAdapter	 =  pDM_Odm->Adapter;
	PADAPTER 				pTargetAdapter = NULL;
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(pAdapter);
	PMGNT_INFO				pMgntInfo = GetDefaultMgntInfo(pAdapter);

	if(pAdapter->bDriverStopped)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("<---- odm_RefreshRateAdaptiveMask(): driver is going to unload\n"));
		return;
	}

	if(!pHalData->bUseRAMask)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("<---- odm_RefreshRateAdaptiveMask(): driver does not control rate adaptive mask\n"));
		return;
	}

	// if default port is connected, update RA table for default port (infrastructure mode only)
	if(pMgntInfo->mAssoc && (!ACTING_AS_AP(pAdapter)))
	{	
		odm_RefreshLdpcRtsMP(pAdapter, pDM_Odm, pMgntInfo->mMacId,  pMgntInfo->IOTPeer, pHalData->UndecoratedSmoothedPWDB);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("odm_RefreshRateAdaptiveMask(): Infrasture Mode\n"));
		if( ODM_RAStateCheck(pDM_Odm, pHalData->UndecoratedSmoothedPWDB, pMgntInfo->bSetTXPowerTrainingByOid, &pMgntInfo->Ratr_State) )
		{
			ODM_PRINT_ADDR(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target AP addr : "), pMgntInfo->Bssid);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", pHalData->UndecoratedSmoothedPWDB, pMgntInfo->Ratr_State));
			pAdapter->HalFunc.UpdateHalRAMaskHandler(pAdapter, pMgntInfo->mMacId, NULL, pMgntInfo->Ratr_State);
		}
		else if(pDM_Odm->bChangeState)
		{
			ODM_PRINT_ADDR(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target AP addr : "), pMgntInfo->Bssid);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Change Power Training State, bDisablePowerTraining = %d\n", pDM_Odm->bDisablePowerTraining));
			pAdapter->HalFunc.UpdateHalRAMaskHandler(pAdapter, pMgntInfo->mMacId, NULL, pMgntInfo->Ratr_State);
		}
	}

	//
	// The following part configure AP/VWifi/IBSS rate adaptive mask.
	//

	if(pMgntInfo->mIbss) 	// Target: AP/IBSS peer.
		pTargetAdapter = GetDefaultAdapter(pAdapter);
	else
		pTargetAdapter = GetFirstAPAdapter(pAdapter);

	// if extension port (softap) is started, updaet RA table for more than one clients associate
	if(pTargetAdapter != NULL)
	{
		int	i;
		PRT_WLAN_STA	pEntry;

		for(i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
		{
			pEntry = AsocEntry_EnumStation(pTargetAdapter, i);
			if(NULL != pEntry)
			{
				if(pEntry->bAssociated)
				{
					odm_RefreshLdpcRtsMP(pAdapter, pDM_Odm, pEntry->AssociatedMacId, pEntry->IOTPeer, pEntry->rssi_stat.UndecoratedSmoothedPWDB);

					if(ODM_RAStateCheck(pDM_Odm, pEntry->rssi_stat.UndecoratedSmoothedPWDB, pMgntInfo->bSetTXPowerTrainingByOid, &pEntry->Ratr_State) )
					{
						ODM_PRINT_ADDR(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target STA addr : "), pEntry->MacAddr);
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", pEntry->rssi_stat.UndecoratedSmoothedPWDB, pEntry->Ratr_State));
						pAdapter->HalFunc.UpdateHalRAMaskHandler(pTargetAdapter, pEntry->AssociatedMacId, pEntry, pEntry->Ratr_State);
					}
					else if(pDM_Odm->bChangeState)
					{
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Change Power Training State, bDisablePowerTraining = %d\n", pDM_Odm->bDisablePowerTraining));
						pAdapter->HalFunc.UpdateHalRAMaskHandler(pAdapter, pMgntInfo->mMacId, NULL, pMgntInfo->Ratr_State);
					}
				}
			}
		}
	}

	if(pMgntInfo->bSetTXPowerTrainingByOid)
		pMgntInfo->bSetTXPowerTrainingByOid = FALSE;	
#endif	// #if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
}


VOID
odm_RefreshRateAdaptiveMaskCE(
	IN	PVOID	pDM_VOID	
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	i;
	PADAPTER	pAdapter	 =  pDM_Odm->Adapter;
	PODM_RATE_ADAPTIVE		pRA = &pDM_Odm->RateAdaptive;

	if(pAdapter->bDriverStopped)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("<---- odm_RefreshRateAdaptiveMask(): driver is going to unload\n"));
		return;
	}

	if(!pDM_Odm->bUseRAMask)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("<---- odm_RefreshRateAdaptiveMask(): driver does not control rate adaptive mask\n"));
		return;
	}

	//printk("==> %s \n",__FUNCTION__);

	for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++){
		PSTA_INFO_T pstat = pDM_Odm->pODM_StaInfo[i];
		if(IS_STA_VALID(pstat) ) {
			if(IS_MCAST( pstat->hwaddr))  //if(psta->mac_id ==1)
				 continue;
			if(IS_MCAST( pstat->hwaddr))
				continue;

			#if((RTL8812A_SUPPORT==1)||(RTL8821A_SUPPORT==1))
			if((pDM_Odm->SupportICType == ODM_RTL8812)||(pDM_Odm->SupportICType == ODM_RTL8821))
			{
				if(pstat->rssi_stat.UndecoratedSmoothedPWDB < pRA->LdpcThres)
				{
					pRA->bUseLdpc = TRUE;
					pRA->bLowerRtsRate = TRUE;
					if((pDM_Odm->SupportICType == ODM_RTL8821) && (pDM_Odm->CutVersion == ODM_CUT_A))
						Set_RA_LDPC_8812(pstat, TRUE);
					//DbgPrint("RSSI=%d, bUseLdpc = TRUE\n", pHalData->UndecoratedSmoothedPWDB);
				}
				else if(pstat->rssi_stat.UndecoratedSmoothedPWDB > (pRA->LdpcThres-5))
				{
					pRA->bUseLdpc = FALSE;
					pRA->bLowerRtsRate = FALSE;
					if((pDM_Odm->SupportICType == ODM_RTL8821) && (pDM_Odm->CutVersion == ODM_CUT_A))
						Set_RA_LDPC_8812(pstat, FALSE);
					//DbgPrint("RSSI=%d, bUseLdpc = FALSE\n", pHalData->UndecoratedSmoothedPWDB);
				}
			}
			#endif

			if( TRUE == ODM_RAStateCheck(pDM_Odm, pstat->rssi_stat.UndecoratedSmoothedPWDB, FALSE , &pstat->rssi_level) )
			{
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", pstat->rssi_stat.UndecoratedSmoothedPWDB, pstat->rssi_level));
				//printk("RSSI:%d, RSSI_LEVEL:%d\n", pstat->rssi_stat.UndecoratedSmoothedPWDB, pstat->rssi_level);
				rtw_hal_update_ra_mask(pstat, pstat->rssi_level);
			}
			else if(pDM_Odm->bChangeState)
			{
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Change Power Training State, bDisablePowerTraining = %d\n", pDM_Odm->bDisablePowerTraining));
				rtw_hal_update_ra_mask(pstat, pstat->rssi_level);
			}
		
		}
	}			
	
#endif
}

VOID
odm_RefreshRateAdaptiveMaskAPADSL(
	IN	PVOID	pDM_VOID
	)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	struct rtl8192cd_priv *priv = pDM_Odm->priv;
	struct aid_obj *aidarray;
	u4Byte i;
	PSTA_INFO_T pstat;

	if(priv->up_time % 2)
		return;	

	for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++) {
		pstat = pDM_Odm->pODM_StaInfo[i];

		if(IS_STA_VALID(pstat) )
		{			
#if defined(UNIVERSAL_REPEATER) || defined(MBSSID)
			aidarray = container_of(pstat, struct aid_obj, station);
			priv = aidarray->priv;
#endif

			if (!priv->pmib->dot11StationConfigEntry.autoRate) 
				continue;

			if(ODM_RAStateCheck(pDM_Odm, (s4Byte)pstat->rssi, FALSE, &pstat->rssi_level) ) {
				ODM_PRINT_ADDR(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target STA addr : "), pstat->hwaddr);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", pstat->rssi, pstat->rssi_level));

#if defined(CONFIG_PCI_HCI)
#ifdef CONFIG_WLAN_HAL
				if (IS_HAL_CHIP(priv)) {
#ifdef WDS
					if(!(pstat->state & WIFI_WDS))//if WDS donot setting
#endif
						GET_HAL_INTERFACE(priv)->UpdateHalRAMaskHandler(priv, pstat, pstat->rssi_level);
				} else
#endif
#ifdef CONFIG_RTL_8812_SUPPORT
				if(GET_CHIP_VER(priv)== VERSION_8812E) {
					UpdateHalRAMask8812(priv, pstat, 3);
				} else
#endif
#ifdef CONFIG_RTL_88E_SUPPORT
				if (GET_CHIP_VER(priv)==VERSION_8188E) {
#ifdef TXREPORT
					add_RATid(priv, pstat);
#endif
				} else
#endif
				{
#if defined(CONFIG_RTL_92D_SUPPORT) || defined(CONFIG_RTL_92C_SUPPORT)
					add_update_RATid(priv, pstat);
#endif
				}
#elif defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
				update_STA_RATid(priv, pstat);
#endif
			}
		}
	}
#endif
}


// Return Value: BOOLEAN
// - TRUE: RATRState is changed.
BOOLEAN 
ODM_RAStateCheck(
	IN		PVOID			pDM_VOID,
	IN		s4Byte			RSSI,
	IN		BOOLEAN			bForceUpdate,
	OUT		pu1Byte			pRATRState
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PODM_RATE_ADAPTIVE pRA = &pDM_Odm->RateAdaptive;
	const u1Byte GoUpGap = 5;
	u1Byte HighRSSIThreshForRA = pRA->HighRSSIThresh;
	u1Byte LowRSSIThreshForRA = pRA->LowRSSIThresh;
	u1Byte RATRState;

	// Threshold Adjustment: 
	// when RSSI state trends to go up one or two levels, make sure RSSI is high enough.
	// Here GoUpGap is added to solve the boundary's level alternation issue.
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	u1Byte UltraLowRSSIThreshForRA = pRA->UltraLowRSSIThresh;
	if(pDM_Odm->SupportICType == ODM_RTL8881A)		
		LowRSSIThreshForRA = 30;		// for LDPC / BCC switch
#endif

	switch (*pRATRState)
	{
		case DM_RATR_STA_INIT:
		case DM_RATR_STA_HIGH:
			break;

		case DM_RATR_STA_MIDDLE:
			HighRSSIThreshForRA += GoUpGap;
			break;

		case DM_RATR_STA_LOW:
			HighRSSIThreshForRA += GoUpGap;
			LowRSSIThreshForRA += GoUpGap;
			break;
			
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
		case DM_RATR_STA_ULTRA_LOW:
			HighRSSIThreshForRA += GoUpGap;
			LowRSSIThreshForRA += GoUpGap;
			UltraLowRSSIThreshForRA += GoUpGap;
			break;
#endif

		default: 
			ODM_RT_ASSERT(pDM_Odm, FALSE, ("wrong rssi level setting %d !", *pRATRState) );
			break;
	}

	// Decide RATRState by RSSI.
	if(RSSI > HighRSSIThreshForRA)
		RATRState = DM_RATR_STA_HIGH;
	else if(RSSI > LowRSSIThreshForRA)
		RATRState = DM_RATR_STA_MIDDLE;

#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	else if(RSSI > UltraLowRSSIThreshForRA)
		RATRState = DM_RATR_STA_LOW;
	else
		RATRState = DM_RATR_STA_ULTRA_LOW;
#else
	else
		RATRState = DM_RATR_STA_LOW;
#endif
	//printk("==>%s,RATRState:0x%02x ,RSSI:%d \n",__FUNCTION__,RATRState,RSSI);

	if( *pRATRState!=RATRState || bForceUpdate)
	{
		ODM_RT_TRACE( pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI Level %d -> %d\n", *pRATRState, RATRState) );
		*pRATRState = RATRState;
		return TRUE;
	}

	return FALSE;
}

VOID
odm_RefreshBasicRateMask(
	IN	PVOID	pDM_VOID
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER		Adapter	 =  pDM_Odm->Adapter;
	static u1Byte		Stage = 0;
	u1Byte			CurStage = 0;
	OCTET_STRING 	osRateSet;
	PMGNT_INFO		pMgntInfo = GetDefaultMgntInfo(Adapter);
	u1Byte 			RateSet[5] = {MGN_1M, MGN_2M, MGN_5_5M, MGN_11M, MGN_6M};

	if(pDM_Odm->SupportICType != ODM_RTL8812 && pDM_Odm->SupportICType != ODM_RTL8821 )
		return;

	if(pDM_Odm->bLinked == FALSE)	// unlink Default port information
		CurStage = 0;	
	else if(pDM_Odm->RSSI_Min < 40)	// link RSSI  < 40%
		CurStage = 1;
	else if(pDM_Odm->RSSI_Min > 45)	// link RSSI > 45%
		CurStage = 3;	
	else
		CurStage = 2;					// link  25% <= RSSI <= 30%

	if(CurStage != Stage)
	{
		if(CurStage == 1)
		{
			FillOctetString(osRateSet, RateSet, 5);
			FilterSupportRate(pMgntInfo->mBrates, &osRateSet, FALSE);
			Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_BASIC_RATE, (pu1Byte)&osRateSet);
		}
		else if(CurStage == 3 && (Stage == 1 || Stage == 2))
		{
			Adapter->HalFunc.SetHwRegHandler( Adapter, HW_VAR_BASIC_RATE, (pu1Byte)(&pMgntInfo->mBrates) );
		}
	}
	
	Stage = CurStage;
#endif
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
ODM_DynamicARFBSelect(
	IN		PVOID		pDM_VOID,
	IN 		u1Byte			rate,
	IN  		BOOLEAN			Collision_State	
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pRA_T			pRA_Table = &pDM_Odm->DM_RA_Table;

	if(pDM_Odm->SupportICType != ODM_RTL8192E)
		return;

	if(Collision_State == pRA_Table->PT_collision_pre)
		return;

	if (rate >= DESC_RATEMCS8  && rate <= DESC_RATEMCS12){
		if (Collision_State == 1){
			if(rate == DESC_RATEMCS12){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x0);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x07060501);	
			}
			else if(rate == DESC_RATEMCS11){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x0);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x07070605);	
			}
			else if(rate == DESC_RATEMCS10){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x0);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x08080706);	
			}
			else if(rate == DESC_RATEMCS9){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x0);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x08080707);	
			}
			else{

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x0);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x09090808);	
			}
		}
		else{   // Collision_State == 0
			if(rate == DESC_RATEMCS12){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x05010000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x09080706);	
			}
			else if(rate == DESC_RATEMCS11){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x06050000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x09080807);	
			}
			else if(rate == DESC_RATEMCS10){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x07060000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x0a090908);	
			}
			else if(rate == DESC_RATEMCS9){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x07070000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x0a090808);	
			}
			else{

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x08080000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x0b0a0909);	
			}
		}
	}
	else{  // MCS13~MCS15,  1SS, G-mode
		if (Collision_State == 1){
			if(rate == DESC_RATEMCS15){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x00000000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x05040302);	
			}
			else if(rate == DESC_RATEMCS14){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x00000000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x06050302);	
			}
			else if(rate == DESC_RATEMCS13){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x00000000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x07060502);	
			}
			else{

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x00000000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x06050402);	
			}
		}
		else{   // Collision_State == 0
  			if(rate == DESC_RATEMCS15){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x03020000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x07060504);	
			}
			else if(rate == DESC_RATEMCS14){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x03020000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x08070605);	
			}
			else if(rate == DESC_RATEMCS13){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x05020000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x09080706);	
			}
			else{

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x04020000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x08070605);	
			}


		}

	}	
	 pRA_Table->PT_collision_pre = Collision_State;	
}

VOID
ODM_RateAdaptiveStateApInit(	
	IN	PVOID		PADAPTER_VOID,
	IN	PRT_WLAN_STA  	pEntry
	)
{
	PADAPTER		Adapter = (PADAPTER)PADAPTER_VOID;
	pEntry->Ratr_State = DM_RATR_STA_INIT;
}
#endif //#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)


#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
u4Byte 
ODM_Get_Rate_Bitmap(
	IN	PVOID		pDM_VOID,	
	IN	u4Byte		macid,
	IN	u4Byte 		ra_mask,	
	IN	u1Byte 		rssi_level
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PSTA_INFO_T   	pEntry;
	u4Byte 	rate_bitmap = 0;
	u1Byte 	WirelessMode;
	//u1Byte 	WirelessMode =*(pDM_Odm->pWirelessMode);
	
	
	pEntry = pDM_Odm->pODM_StaInfo[macid];
	if(!IS_STA_VALID(pEntry))
		return ra_mask;

	WirelessMode = pEntry->wireless_mode;
	
	switch(WirelessMode)
	{
		case ODM_WM_B:
			if(ra_mask & 0x0000000c)		//11M or 5.5M enable				
				rate_bitmap = 0x0000000d;
			else
				rate_bitmap = 0x0000000f;
			break;
			
		case (ODM_WM_G):
		case (ODM_WM_A):
			if(rssi_level == DM_RATR_STA_HIGH)
				rate_bitmap = 0x00000f00;
			else
				rate_bitmap = 0x00000ff0;
			break;
			
		case (ODM_WM_B|ODM_WM_G):
			if(rssi_level == DM_RATR_STA_HIGH)
				rate_bitmap = 0x00000f00;
			else if(rssi_level == DM_RATR_STA_MIDDLE)
				rate_bitmap = 0x00000ff0;
			else
				rate_bitmap = 0x00000ff5;
			break;		

		case (ODM_WM_B|ODM_WM_G|ODM_WM_N24G)	:
		case (ODM_WM_B|ODM_WM_N24G)	:
		case (ODM_WM_G|ODM_WM_N24G)	:
		case (ODM_WM_A|ODM_WM_N5G)	:
			{					
				if (	pDM_Odm->RFType == ODM_1T2R ||pDM_Odm->RFType == ODM_1T1R)
				{
					if(rssi_level == DM_RATR_STA_HIGH)
					{
						rate_bitmap = 0x000f0000;
					}
					else if(rssi_level == DM_RATR_STA_MIDDLE)
					{
						rate_bitmap = 0x000ff000;
					}
					else{
						if (*(pDM_Odm->pBandWidth) == ODM_BW40M)
							rate_bitmap = 0x000ff015;
						else
							rate_bitmap = 0x000ff005;
					}				
				}
				else
				{
					if(rssi_level == DM_RATR_STA_HIGH)
					{		
						rate_bitmap = 0x0f8f0000;
					}
					else if(rssi_level == DM_RATR_STA_MIDDLE)
					{
						rate_bitmap = 0x0f8ff000;
					}
					else
					{
						if (*(pDM_Odm->pBandWidth) == ODM_BW40M)
							rate_bitmap = 0x0f8ff015;
						else
							rate_bitmap = 0x0f8ff005;
					}					
				}
			}
			break;

		case (ODM_WM_AC|ODM_WM_G):
			if(rssi_level == 1)
				rate_bitmap = 0xfc3f0000;
			else if(rssi_level == 2)
				rate_bitmap = 0xfffff000;
			else
				rate_bitmap = 0xffffffff;
			break;

		case (ODM_WM_AC|ODM_WM_A):

			if (pDM_Odm->RFType == RF_1T1R)
			{
				if(rssi_level == 1)				// add by Gary for ac-series
					rate_bitmap = 0x003f8000;
				else if (rssi_level == 2)
					rate_bitmap = 0x003ff000;
				else
					rate_bitmap = 0x003ff010;
			}
			else
			{
				if(rssi_level == 1)				// add by Gary for ac-series
					rate_bitmap = 0xfe3f8000;       // VHT 2SS MCS3~9
				else if (rssi_level == 2)
					rate_bitmap = 0xfffff000;       // VHT 2SS MCS0~9
				else
					rate_bitmap = 0xfffff010;       // All
			}
			break;
			
		default:
			if(pDM_Odm->RFType == RF_1T2R)
				rate_bitmap = 0x000fffff;
			else
				rate_bitmap = 0x0fffffff;
			break;	

	}

	//printk("%s ==> rssi_level:0x%02x, WirelessMode:0x%02x, rate_bitmap:0x%08x \n",__FUNCTION__,rssi_level,WirelessMode,rate_bitmap);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, (" ==> rssi_level:0x%02x, WirelessMode:0x%02x, rate_bitmap:0x%08x \n",rssi_level,WirelessMode,rate_bitmap));

	return (ra_mask&rate_bitmap);
	
}	

#endif //#if (DM_ODM_SUPPORT_TYPE == ODM_CE)