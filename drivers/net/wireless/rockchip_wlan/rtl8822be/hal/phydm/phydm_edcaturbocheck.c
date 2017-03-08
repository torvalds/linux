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
ODM_EdcaTurboInit(
	IN 	PVOID	 	pDM_VOID)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

#if (DM_ODM_SUPPORT_TYPE==ODM_WIN)
	PADAPTER	Adapter = NULL;
	HAL_DATA_TYPE	*pHalData = NULL;

	if(pDM_Odm->Adapter==NULL)	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("EdcaTurboInit fail!!!\n"));
		return;
	}

	Adapter=pDM_Odm->Adapter;
	pHalData=GET_HAL_DATA(Adapter);

	pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA = FALSE;	
	pDM_Odm->DM_EDCA_Table.bIsCurRDLState = FALSE;
	pHalData->bIsAnyNonBEPkts = FALSE;
	
#elif(DM_ODM_SUPPORT_TYPE==ODM_CE)
	PADAPTER	Adapter = pDM_Odm->Adapter;	
	pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA = FALSE;	
	pDM_Odm->DM_EDCA_Table.bIsCurRDLState = FALSE;
	Adapter->recvpriv.bIsAnyNonBEPkts =FALSE;

#endif	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("Orginial VO PARAM: 0x%x\n",ODM_Read4Byte(pDM_Odm,ODM_EDCA_VO_PARAM)));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("Orginial VI PARAM: 0x%x\n",ODM_Read4Byte(pDM_Odm,ODM_EDCA_VI_PARAM)));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("Orginial BE PARAM: 0x%x\n",ODM_Read4Byte(pDM_Odm,ODM_EDCA_BE_PARAM)));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("Orginial BK PARAM: 0x%x\n",ODM_Read4Byte(pDM_Odm,ODM_EDCA_BK_PARAM)));

	
}	// ODM_InitEdcaTurbo

VOID
odm_EdcaTurboCheck(
	IN 	PVOID	 	pDM_VOID
	)
{
	// 
	// For AP/ADSL use prtl8192cd_priv
	// For CE/NIC use PADAPTER
	//

	//
	// 2011/09/29 MH In HW integration first stage, we provide 4 different handle to operate
	// at the same time. In the stage2/3, we need to prive universal interface and merge all
	// HW dynamic mechanism.
	//
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("odm_EdcaTurboCheck========================>\n"));

	if(!(pDM_Odm->SupportAbility& ODM_MAC_EDCA_TURBO ))
		return;

	switch	(pDM_Odm->SupportPlatform)
	{
		case	ODM_WIN:

#if(DM_ODM_SUPPORT_TYPE==ODM_WIN)
			odm_EdcaTurboCheckMP(pDM_Odm);
#endif
			break;

		case	ODM_CE:
#if(DM_ODM_SUPPORT_TYPE==ODM_CE)
			odm_EdcaTurboCheckCE(pDM_Odm);
#endif
			break;
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("<========================odm_EdcaTurboCheck\n"));

}	// odm_CheckEdcaTurbo

#if(DM_ODM_SUPPORT_TYPE==ODM_CE)


VOID
odm_EdcaTurboCheckCE(
	IN 	PVOID	 	pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER		       Adapter = pDM_Odm->Adapter;
	u32	EDCA_BE_UL = 0x5ea42b;//Parameter suggested by Scott  //edca_setting_UL[pMgntInfo->IOTPeer];
	u32	EDCA_BE_DL = 0x5ea42b;//Parameter suggested by Scott  //edca_setting_DL[pMgntInfo->IOTPeer];
	u32	ICType=pDM_Odm->SupportICType;
	u32	IOTPeer=0;
	u8	WirelessMode=0xFF;                   //invalid value
	u32 	trafficIndex;
	u32	edca_param;
	u64	cur_tx_bytes = 0;
	u64	cur_rx_bytes = 0;
	u8	bbtchange = _FALSE;
	u8	bBiasOnRx = _FALSE;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct dvobj_priv		*pdvobjpriv = adapter_to_dvobj(Adapter);
	struct xmit_priv		*pxmitpriv = &(Adapter->xmitpriv);
	struct recv_priv		*precvpriv = &(Adapter->recvpriv);
	struct registry_priv	*pregpriv = &Adapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &(Adapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if(pDM_Odm->bLinked != _TRUE)
	{
		precvpriv->bIsAnyNonBEPkts = _FALSE;
		return;
	}

	if ((pregpriv->wifi_spec == 1) )//|| (pmlmeinfo->HT_enable == 0))
	{
		precvpriv->bIsAnyNonBEPkts = _FALSE;
		return;
	}

	if(pDM_Odm->pWirelessMode!=NULL)
		WirelessMode=*(pDM_Odm->pWirelessMode);

	IOTPeer = pmlmeinfo->assoc_AP_vendor;

	if (IOTPeer >=  HT_IOT_PEER_MAX)
	{
		precvpriv->bIsAnyNonBEPkts = _FALSE;
		return;
	}

	if (pDM_Odm->SupportICType & ODM_RTL8188E) {
		if((IOTPeer == HT_IOT_PEER_RALINK)||(IOTPeer == HT_IOT_PEER_ATHEROS))
			bBiasOnRx = _TRUE;
	}

	// Check if the status needs to be changed.
	if((bbtchange) || (!precvpriv->bIsAnyNonBEPkts) )
	{
		cur_tx_bytes = pdvobjpriv->traffic_stat.cur_tx_bytes;
		cur_rx_bytes = pdvobjpriv->traffic_stat.cur_rx_bytes;

		//traffic, TX or RX
		if(bBiasOnRx)
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

		//if ((pDM_Odm->DM_EDCA_Table.prv_traffic_idx != trafficIndex) || (!pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA))
		{
			if (pDM_Odm->SupportInterface == ODM_ITRF_PCIE) {
				EDCA_BE_UL = 0x6ea42b;
				EDCA_BE_DL = 0x6ea42b;
			}

			//92D txop can't be set to 0x3e for cisco1250
			if ((IOTPeer == HT_IOT_PEER_CISCO) && (WirelessMode == ODM_WM_N24G))
			{
				EDCA_BE_DL = edca_setting_DL[IOTPeer];
				EDCA_BE_UL = edca_setting_UL[IOTPeer];
			}
			//merge from 92s_92c_merge temp brunch v2445    20120215 
			else if((IOTPeer == HT_IOT_PEER_CISCO) &&((WirelessMode==ODM_WM_G)||(WirelessMode==(ODM_WM_B|ODM_WM_G))||(WirelessMode==ODM_WM_A)||(WirelessMode==ODM_WM_B)))
			{
				EDCA_BE_DL = edca_setting_DL_GMode[IOTPeer];
			}
			else if((IOTPeer== HT_IOT_PEER_AIRGO )&& ((WirelessMode==ODM_WM_G)||(WirelessMode==ODM_WM_A)))
			{
				EDCA_BE_DL = 0xa630;
			}
			else if(IOTPeer == HT_IOT_PEER_MARVELL)
			{
				EDCA_BE_DL = edca_setting_DL[IOTPeer];
				EDCA_BE_UL = edca_setting_UL[IOTPeer];
			}
			else if(IOTPeer == HT_IOT_PEER_ATHEROS)
			{
				// Set DL EDCA for Atheros peer to 0x3ea42b. Suggested by SD3 Wilson for ASUS TP issue. 
				EDCA_BE_DL = edca_setting_DL[IOTPeer];
			}

			if((ICType==ODM_RTL8812)||(ICType==ODM_RTL8821)||(ICType==ODM_RTL8192E))           //add 8812AU/8812AE
			{
				EDCA_BE_UL = 0x5ea42b;
				EDCA_BE_DL = 0x5ea42b;

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("8812A: EDCA_BE_UL=0x%x EDCA_BE_DL =0x%x",EDCA_BE_UL,EDCA_BE_DL));
			}

			if (ICType == ODM_RTL8822B) { 
				/* aggressively to send TCP ack while DL */
				EDCA_BE_UL = 0x6ea42b;
				EDCA_BE_DL = 0x6ea42b;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("8822B: EDCA_BE_UL=0x%x EDCA_BE_DL =0x%x",EDCA_BE_UL,EDCA_BE_DL));
			}

			if (trafficIndex == DOWN_LINK)
				edca_param = EDCA_BE_DL;
			else
				edca_param = EDCA_BE_UL;

			rtw_write32(Adapter, REG_EDCA_BE_PARAM, edca_param);

			pDM_Odm->DM_EDCA_Table.prv_traffic_idx = trafficIndex;
		}
		
		pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA = _TRUE;
	}
	else
	{
		//
		// Turn Off EDCA turbo here.
		// Restore original EDCA according to the declaration of AP.
		//
		 if(pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA)
		{
			rtw_write32(Adapter, REG_EDCA_BE_PARAM, pHalData->AcParam_BE);
			pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA = _FALSE;
		}
	}

}


#elif(DM_ODM_SUPPORT_TYPE==ODM_WIN)
VOID
odm_EdcaTurboCheckMP(
	IN 	PVOID	 	pDM_VOID
	)
{

	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER		       Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	PADAPTER 			pDefaultAdapter = GetDefaultAdapter(Adapter);
	PADAPTER 			pExtAdapter = GetFirstExtAdapter(Adapter);//NULL;
	PMGNT_INFO			pMgntInfo = &Adapter->MgntInfo;
	PSTA_QOS			pStaQos = Adapter->MgntInfo.pStaQos;
	//[Win7 Count Tx/Rx statistic for Extension Port] odm_CheckEdcaTurbo's Adapter is always Default. 2009.08.20, by Bohn
	u8Byte				Ext_curTxOkCnt = 0;
	u8Byte				Ext_curRxOkCnt = 0;	
	//For future Win7  Enable Default Port to modify AMPDU size dynamically, 2009.08.20, Bohn.	
	u1Byte TwoPortStatus = (u1Byte)TWO_PORT_STATUS__WITHOUT_ANY_ASSOCIATE;

	// Keep past Tx/Rx packet count for RT-to-RT EDCA turbo.
	u8Byte				curTxOkCnt = 0;
	u8Byte				curRxOkCnt = 0;	
	u4Byte				EDCA_BE_UL = 0x5ea42b;//Parameter suggested by Scott  //edca_setting_UL[pMgntInfo->IOTPeer];
	u4Byte				EDCA_BE_DL = 0x5ea42b;//Parameter suggested by Scott  //edca_setting_DL[pMgntInfo->IOTPeer];
	u4Byte                         EDCA_BE = 0x5ea42b;
	u1Byte                         IOTPeer=0;
	BOOLEAN                      *pbIsCurRDLState=NULL;
	BOOLEAN                      bLastIsCurRDLState=FALSE;
	BOOLEAN				 bBiasOnRx=FALSE;
	BOOLEAN				bEdcaTurboOn=FALSE;
	u1Byte				TxRate = 0xFF;
	u8Byte				value64;	

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("odm_EdcaTurboCheckMP========================>"));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("Orginial BE PARAM: 0x%x\n",ODM_Read4Byte(pDM_Odm,ODM_EDCA_BE_PARAM)));

////===============================
////list paramter for different platform
////===============================
	bLastIsCurRDLState=pDM_Odm->DM_EDCA_Table.bIsCurRDLState;
	pbIsCurRDLState=&(pDM_Odm->DM_EDCA_Table.bIsCurRDLState);	

	//2012/09/14 MH Add 
	if (pMgntInfo->NumNonBePkt > pMgntInfo->RegEdcaThresh && !(Adapter->MgntInfo.bWiFiConfg & RT_WIFI_LOGO))
		pHalData->bIsAnyNonBEPkts = TRUE;

	pMgntInfo->NumNonBePkt = 0;

       // Caculate TX/RX TP:
	curTxOkCnt = pDM_Odm->curTxOkCnt;
	curRxOkCnt = pDM_Odm->curRxOkCnt;


	if(pExtAdapter == NULL) 
		pExtAdapter = pDefaultAdapter;

	Ext_curTxOkCnt = pExtAdapter->TxStats.NumTxBytesUnicast - pMgntInfo->Ext_lastTxOkCnt;
	Ext_curRxOkCnt = pExtAdapter->RxStats.NumRxBytesUnicast - pMgntInfo->Ext_lastRxOkCnt;
	GetTwoPortSharedResource(Adapter,TWO_PORT_SHARED_OBJECT__STATUS,NULL,&TwoPortStatus);
	//For future Win7  Enable Default Port to modify AMPDU size dynamically, 2009.08.20, Bohn.
	if(TwoPortStatus == TWO_PORT_STATUS__EXTENSION_ONLY)
	{
		curTxOkCnt = Ext_curTxOkCnt ;
		curRxOkCnt = Ext_curRxOkCnt ;
	}
	//
	IOTPeer=pMgntInfo->IOTPeer;
	bBiasOnRx=(pMgntInfo->IOTAction & HT_IOT_ACT_EDCA_BIAS_ON_RX)?TRUE:FALSE;
	bEdcaTurboOn=((!pHalData->bIsAnyNonBEPkts))?TRUE:FALSE;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("bIsAnyNonBEPkts : 0x%lx  \n",pHalData->bIsAnyNonBEPkts));


////===============================
////check if edca turbo is disabled
////===============================
	if(odm_IsEdcaTurboDisable(pDM_Odm))
	{
		pHalData->bIsAnyNonBEPkts = FALSE;
		pMgntInfo->lastTxOkCnt = Adapter->TxStats.NumTxBytesUnicast;
		pMgntInfo->lastRxOkCnt = Adapter->RxStats.NumRxBytesUnicast;
		pMgntInfo->Ext_lastTxOkCnt = pExtAdapter->TxStats.NumTxBytesUnicast;
		pMgntInfo->Ext_lastRxOkCnt = pExtAdapter->RxStats.NumRxBytesUnicast;

	}

////===============================
////remove iot case out
////===============================
	ODM_EdcaParaSelByIot(pDM_Odm, &EDCA_BE_UL, &EDCA_BE_DL);


////===============================
////Check if the status needs to be changed.
////===============================
	if(bEdcaTurboOn)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("bEdcaTurboOn : 0x%x bBiasOnRx : 0x%x\n",bEdcaTurboOn,bBiasOnRx));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("curTxOkCnt : 0x%lx \n",curTxOkCnt));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("curRxOkCnt : 0x%lx \n",curRxOkCnt));
		if(bBiasOnRx)
			odm_EdcaChooseTrafficIdx(pDM_Odm,curTxOkCnt, curRxOkCnt,   TRUE,  pbIsCurRDLState);
		else
			odm_EdcaChooseTrafficIdx(pDM_Odm,curTxOkCnt, curRxOkCnt,   FALSE,  pbIsCurRDLState);

//modify by Guo.Mingzhi 2011-12-29
			if( Adapter->AP_EDCA_PARAM[0] != EDCA_BE )
				EDCA_BE = Adapter->AP_EDCA_PARAM[0];
			else
				EDCA_BE=((*pbIsCurRDLState)==TRUE)?EDCA_BE_DL:EDCA_BE_UL;

// For TPLINK 8188EU test 
			if ((IS_HARDWARE_TYPE_8188EU(Adapter))&&(pHalData->UndecoratedSmoothedPWDB < 28)) { // Set to origimal EDCA 0x5EA42B now need to update. 
			} 
			else
			{
				// Use TPLINk prefered EDCA parameters. 
				EDCA_BE = pMgntInfo->EDCABEPara ;
			} 

			if(IS_HARDWARE_TYPE_8821U(Adapter))
			{
				if(pMgntInfo->RegTxDutyEnable)
				{
					//2013.01.23 LukeLee: debug for 8811AU thermal issue (reduce Tx duty cycle)
					if(!pMgntInfo->ForcedDataRate) //auto rate
					{
						if(pDM_Odm->TxRate != 0xFF)
							TxRate = Adapter->HalFunc.GetHwRateFromMRateHandler(pDM_Odm->TxRate); 
					}
					else //force rate
					{
						TxRate = (u1Byte) pMgntInfo->ForcedDataRate;
					}

					value64 = (curRxOkCnt<<2);
					if(curTxOkCnt < value64) //Downlink
						ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,EDCA_BE);
					else //Uplink
					{
						/*DbgPrint("pRFCalibrateInfo->ThermalValue = 0x%X\n", pRFCalibrateInfo->ThermalValue);*/
						/*if(pRFCalibrateInfo->ThermalValue < pHalData->EEPROMThermalMeter)*/
						if((pDM_Odm->RFCalibrateInfo.ThermalValue < 0x2c) || (*pDM_Odm->pBandType == BAND_ON_2_4G))
							ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,EDCA_BE);
						else
						{
							switch (TxRate)
							{
								case MGN_VHT1SS_MCS6:
								case MGN_VHT1SS_MCS5:
								case MGN_MCS6:
								case MGN_MCS5:
								case MGN_48M:
								case MGN_54M:
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,0x1ea42b);
								break;
								case MGN_VHT1SS_MCS4:
								case MGN_MCS4:
								case MGN_36M:
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,0xa42b);
								break;
								case MGN_VHT1SS_MCS3:
								case MGN_MCS3:
								case MGN_24M:
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,0xa47f);
								break;
								case MGN_VHT1SS_MCS2:
								case MGN_MCS2:
								case MGN_18M:
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,0xa57f);
								break;
								case MGN_VHT1SS_MCS1:
								case MGN_MCS1:
								case MGN_9M:
								case MGN_12M:
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,0xa77f);
								break;
								case MGN_VHT1SS_MCS0:
								case MGN_MCS0:
								case MGN_6M:
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,0xa87f);
								break;
								default:
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,EDCA_BE);
								break;
							}
						}
					}				
				}
				else
				{
					ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,EDCA_BE);
				}

			}
			else if (IS_HARDWARE_TYPE_8812AU(Adapter)){
				if(pMgntInfo->RegTxDutyEnable)
				{
					//2013.07.26 Wilson: debug for 8812AU thermal issue (reduce Tx duty cycle)
					// it;s the same issue as 8811AU
					if(!pMgntInfo->ForcedDataRate) //auto rate
					{
						if(pDM_Odm->TxRate != 0xFF)
							TxRate = Adapter->HalFunc.GetHwRateFromMRateHandler(pDM_Odm->TxRate); 
					}
					else //force rate
					{
						TxRate = (u1Byte) pMgntInfo->ForcedDataRate;
					}

					value64 = (curRxOkCnt<<2);
					if(curTxOkCnt < value64) //Downlink
						ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,EDCA_BE);
					else //Uplink
					{
						/*DbgPrint("pRFCalibrateInfo->ThermalValue = 0x%X\n", pRFCalibrateInfo->ThermalValue);*/
						/*if(pRFCalibrateInfo->ThermalValue < pHalData->EEPROMThermalMeter)*/
						if((pDM_Odm->RFCalibrateInfo.ThermalValue < 0x2c) || (*pDM_Odm->pBandType == BAND_ON_2_4G))
							ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,EDCA_BE);
						else
						{
							switch (TxRate)
							{
								case MGN_VHT2SS_MCS9:
								case MGN_VHT1SS_MCS9:									
								case MGN_VHT1SS_MCS8:
								case MGN_MCS15:
								case MGN_MCS7:									
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,0x1ea44f);							
								case MGN_VHT2SS_MCS8:
								case MGN_VHT1SS_MCS7:
								case MGN_MCS14:
								case MGN_MCS6:
								case MGN_54M:									
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,0xa44f);
								case MGN_VHT2SS_MCS7:
								case MGN_VHT2SS_MCS6:
								case MGN_VHT1SS_MCS6:
								case MGN_VHT1SS_MCS5:
								case MGN_MCS13:
								case MGN_MCS5:
								case MGN_48M:
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,0xa630);
								break;
								case MGN_VHT2SS_MCS5:
								case MGN_VHT2SS_MCS4:
								case MGN_VHT1SS_MCS4:
								case MGN_VHT1SS_MCS3:	
								case MGN_MCS12:
								case MGN_MCS4:	
								case MGN_MCS3:	
								case MGN_36M:
								case MGN_24M:	
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,0xa730);
								break;
								case MGN_VHT2SS_MCS3:
								case MGN_VHT2SS_MCS2:
								case MGN_VHT2SS_MCS1:
								case MGN_VHT1SS_MCS2:
								case MGN_VHT1SS_MCS1:	
								case MGN_MCS11:	
								case MGN_MCS10:	
								case MGN_MCS9:		
								case MGN_MCS2:	
								case MGN_MCS1:
								case MGN_18M:	
								case MGN_12M:
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,0xa830);
								break;
								case MGN_VHT2SS_MCS0:
								case MGN_VHT1SS_MCS0:
								case MGN_MCS0:	
								case MGN_MCS8:
								case MGN_9M:	
								case MGN_6M:
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,0xa87f);
								break;
								default:
									ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,EDCA_BE);
								break;
							}
						}
					}				
				}
				else
				{
					ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,EDCA_BE);
				}
			}
			else
				ODM_Write4Byte(pDM_Odm,ODM_EDCA_BE_PARAM,EDCA_BE);

		ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("EDCA Turbo on: EDCA_BE:0x%lx\n",EDCA_BE));

		pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA = TRUE;
		
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("EDCA_BE_DL : 0x%lx  EDCA_BE_UL : 0x%lx  EDCA_BE : 0x%lx  \n",EDCA_BE_DL,EDCA_BE_UL,EDCA_BE));

	}
	else
	{
		// Turn Off EDCA turbo here.
		// Restore original EDCA according to the declaration of AP.
		 if(pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA)
		{
			Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_AC_PARAM, GET_WMM_PARAM_ELE_SINGLE_AC_PARAM(pStaQos->WMMParamEle, AC0_BE) );

			pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA = FALSE;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("Restore EDCA BE: 0x%lx  \n",pDM_Odm->WMMEDCA_BE));

		}
	}

}


//check if edca turbo is disabled
BOOLEAN
odm_IsEdcaTurboDisable(
	IN 	PVOID	 	pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER		       Adapter = pDM_Odm->Adapter;
	PMGNT_INFO			pMgntInfo = &Adapter->MgntInfo;
	u4Byte				IOTPeer=pMgntInfo->IOTPeer;

	if(pDM_Odm->bBtDisableEdcaTurbo)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD, ("EdcaTurboDisable for BT!!\n"));
		return TRUE;
	}

	if((!(pDM_Odm->SupportAbility& ODM_MAC_EDCA_TURBO ))||
		(pDM_Odm->WIFITest & RT_WIFI_LOGO)||
		(IOTPeer>= HT_IOT_PEER_MAX))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD, ("EdcaTurboDisable\n"));
		return TRUE;
	}


	// 1. We do not turn on EDCA turbo mode for some AP that has IOT issue
	// 2. User may disable EDCA Turbo mode with OID settings.
	if(pMgntInfo->IOTAction & HT_IOT_ACT_DISABLE_EDCA_TURBO){
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD, ("IOTAction:EdcaTurboDisable\n"));
		return	TRUE;
		}
		
	return	FALSE;
	

}

//add iot case here: for MP/CE
VOID 
ODM_EdcaParaSelByIot(
	IN 	PVOID	 	pDM_VOID,
	OUT	u4Byte		*EDCA_BE_UL,
	OUT u4Byte		*EDCA_BE_DL
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER		       Adapter = pDM_Odm->Adapter;
	u4Byte                         IOTPeer=0;
	u4Byte                         ICType=pDM_Odm->SupportICType;
	u1Byte                         WirelessMode=0xFF;                   //invalid value
	u4Byte                         IOTPeerSubType = 0;

	PMGNT_INFO			pMgntInfo = &Adapter->MgntInfo;
	u1Byte 				TwoPortStatus = (u1Byte)TWO_PORT_STATUS__WITHOUT_ANY_ASSOCIATE;

	if(pDM_Odm->pWirelessMode!=NULL)
		WirelessMode=*(pDM_Odm->pWirelessMode);
		
///////////////////////////////////////////////////////////
////list paramter for different platform

	IOTPeer=pMgntInfo->IOTPeer;
	IOTPeerSubType=pMgntInfo->IOTPeerSubtype;
	GetTwoPortSharedResource(Adapter,TWO_PORT_SHARED_OBJECT__STATUS,NULL,&TwoPortStatus);

////============================
/// IOT case for MP
////============================	
	if (pDM_Odm->SupportInterface == ODM_ITRF_PCIE) {
			(*EDCA_BE_UL) = 0x6ea42b;
			(*EDCA_BE_DL) = 0x6ea42b;
	}
 
	if(TwoPortStatus == TWO_PORT_STATUS__EXTENSION_ONLY)
	{
		(*EDCA_BE_UL) = 0x5ea42b;//Parameter suggested by Scott  //edca_setting_UL[ExtAdapter->MgntInfo.IOTPeer];
		(*EDCA_BE_DL) = 0x5ea42b;//Parameter suggested by Scott  //edca_setting_DL[ExtAdapter->MgntInfo.IOTPeer];
	}
     
	#if (INTEL_PROXIMITY_SUPPORT == 1)
	if(pMgntInfo->IntelClassModeInfo.bEnableCA == TRUE)
	{
		(*EDCA_BE_UL) = (*EDCA_BE_DL) = 0xa44f;
	}
	else
	#endif		
	{
		if((pMgntInfo->IOTAction & (HT_IOT_ACT_FORCED_ENABLE_BE_TXOP|HT_IOT_ACT_AMSDU_ENABLE)))
		{// To check whether we shall force turn on TXOP configuration.
			if(!((*EDCA_BE_UL) & 0xffff0000))
				(*EDCA_BE_UL) |= 0x005e0000; // Force TxOP limit to 0x005e for UL.
			if(!((*EDCA_BE_DL) & 0xffff0000))
				(*EDCA_BE_DL) |= 0x005e0000; // Force TxOP limit to 0x005e for DL.
		}
		
		//92D txop can't be set to 0x3e for cisco1250
		if ((IOTPeer == HT_IOT_PEER_CISCO) && (WirelessMode == ODM_WM_N24G))
		{
			(*EDCA_BE_DL) = edca_setting_DL[IOTPeer];
			(*EDCA_BE_UL) = edca_setting_UL[IOTPeer];
		}
		//merge from 92s_92c_merge temp brunch v2445    20120215 
		else if((IOTPeer == HT_IOT_PEER_CISCO) &&((WirelessMode==ODM_WM_G)||(WirelessMode==(ODM_WM_B|ODM_WM_G))||(WirelessMode==ODM_WM_A)||(WirelessMode==ODM_WM_B)))
		{
			(*EDCA_BE_DL) = edca_setting_DL_GMode[IOTPeer];
		}
		else if((IOTPeer== HT_IOT_PEER_AIRGO )&& ((WirelessMode==ODM_WM_G)||(WirelessMode==ODM_WM_A)))
		{
			(*EDCA_BE_DL) = 0xa630;
		}

		else if(IOTPeer == HT_IOT_PEER_MARVELL)
		{
			(*EDCA_BE_DL) = edca_setting_DL[IOTPeer];
			(*EDCA_BE_UL) = edca_setting_UL[IOTPeer];
		}
		else if(IOTPeer == HT_IOT_PEER_ATHEROS && IOTPeerSubType != HT_IOT_PEER_TPLINK_AC1750)
		{
			// Set DL EDCA for Atheros peer to 0x3ea42b. Suggested by SD3 Wilson for ASUS TP issue. 
			if(WirelessMode==ODM_WM_G)
				(*EDCA_BE_DL) = edca_setting_DL_GMode[IOTPeer];
			else
				(*EDCA_BE_DL) = edca_setting_DL[IOTPeer];

			if(ICType == ODM_RTL8821)
				 (*EDCA_BE_DL) = 0x5ea630;
			
		}
	}

	if((ICType==ODM_RTL8812)||(ICType==ODM_RTL8192E))           //add 8812AU/8812AE
	{
		(*EDCA_BE_UL) = 0x5ea42b;
		(*EDCA_BE_DL) = 0x5ea42b;

		ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("8812A: EDCA_BE_UL=0x%lx EDCA_BE_DL =0x%lx\n",(*EDCA_BE_UL),(*EDCA_BE_DL)));
	}

	if((ICType==ODM_RTL8814A) && (IOTPeer == HT_IOT_PEER_REALTEK))           /*8814AU and 8814AR*/
	{
		(*EDCA_BE_UL) = 0x5ea42b;
		(*EDCA_BE_DL) = 0xa42b;

		ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("8814A: EDCA_BE_UL=0x%lx EDCA_BE_DL =0x%lx\n",(*EDCA_BE_UL),(*EDCA_BE_DL)));
	}

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("Special: EDCA_BE_UL=0x%lx EDCA_BE_DL =0x%lx, IOTPeer = %d\n",(*EDCA_BE_UL),(*EDCA_BE_DL), IOTPeer));

}


VOID
odm_EdcaChooseTrafficIdx( 
	IN 	PVOID	 	pDM_VOID,
	IN	u8Byte  			cur_tx_bytes,  
	IN	u8Byte  			cur_rx_bytes, 
	IN	BOOLEAN 		bBiasOnRx,
	OUT BOOLEAN 		*pbIsCurRDLState
	)
{	
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	
	if(bBiasOnRx)
	{
	  
		if(cur_tx_bytes>(cur_rx_bytes*4))
		{
			*pbIsCurRDLState=FALSE;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("Uplink Traffic\n "));

		}
		else
		{
			*pbIsCurRDLState=TRUE;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("Balance Traffic\n"));

		}
	}
	else
	{
		if(cur_rx_bytes>(cur_tx_bytes*4))
		{
			*pbIsCurRDLState=TRUE;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("Downlink	Traffic\n"));

		}
		else
		{
			*pbIsCurRDLState=FALSE;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_EDCA_TURBO,ODM_DBG_LOUD,("Balance Traffic\n"));
		}
	}

	return ;
}

#endif


