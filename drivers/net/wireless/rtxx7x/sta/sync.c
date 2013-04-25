/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#include "rt_config.h"


VOID MlmeForceJoinReqAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem);


VOID MlmeForceScanReqAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem);


#define ADHOC_ENTRY_BEACON_LOST_TIME	(2*OS_HZ)	/* 2 sec */

/*
	==========================================================================
	Description:
		The sync state machine, 
	Parameters:
		Sm - pointer to the state machine
	Note:
		the state machine looks like the following

	==========================================================================
 */
VOID SyncStateMachineInit(
	IN PRTMP_ADAPTER pAd, 
	IN STATE_MACHINE *Sm, 
	OUT STATE_MACHINE_FUNC Trans[]) 
{
	StateMachineInit(Sm, Trans, MAX_SYNC_STATE, MAX_SYNC_MSG, (STATE_MACHINE_FUNC)Drop, SYNC_IDLE, SYNC_MACHINE_BASE);

	/* column 1 */
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_SCAN_REQ, (STATE_MACHINE_FUNC)MlmeScanReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_FORCE_SCAN_REQ, (STATE_MACHINE_FUNC)MlmeForceScanReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_JOIN_REQ, (STATE_MACHINE_FUNC)MlmeJoinReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_FORCE_JOIN_REQ, (STATE_MACHINE_FUNC)MlmeForceJoinReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_START_REQ, (STATE_MACHINE_FUNC)MlmeStartReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_PEER_BEACON, (STATE_MACHINE_FUNC)PeerBeacon);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_PEER_PROBE_REQ, (STATE_MACHINE_FUNC)PeerProbeReqAction); 

	/* column 2 */
#ifdef ANDROID_SUPPORT
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_MLME_JOIN_REQ, (STATE_MACHINE_FUNC)MlmeJoinReqAction);
#else
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_MLME_SCAN_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenScan);
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_MLME_JOIN_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenJoin);
#endif /* ANDROID_SUPPORT */
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_MLME_START_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenStart);
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_PEER_BEACON, (STATE_MACHINE_FUNC)PeerBeaconAtJoinAction);
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_BEACON_TIMEOUT, (STATE_MACHINE_FUNC)BeaconTimeoutAtJoinAction);
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_PEER_PROBE_RSP, (STATE_MACHINE_FUNC)PeerBeaconAtScanAction);

	/* column 3 */
#ifdef ANDROID_SUPPORT
/*    ingore */
/*	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_MLME_SCAN_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenScan);*/
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_MLME_JOIN_REQ, (STATE_MACHINE_FUNC)MlmeJoinReqAction);
#else
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_MLME_SCAN_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenScan);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_MLME_JOIN_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenJoin);
#endif /* ANDROID_SUPPORT */

	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_MLME_START_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenStart);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_PEER_BEACON, (STATE_MACHINE_FUNC)PeerBeaconAtScanAction);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_PEER_PROBE_RSP, (STATE_MACHINE_FUNC)PeerBeaconAtScanAction);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_SCAN_TIMEOUT, (STATE_MACHINE_FUNC)ScanTimeoutAction);
	/* StateMachineSetAction(Sm, SCAN_LISTEN, MT2_MLME_SCAN_CNCL, (STATE_MACHINE_FUNC)ScanCnclAction); */

	/* resume scanning for fast-roaming */
	StateMachineSetAction(Sm, SCAN_PENDING, MT2_MLME_SCAN_REQ, (STATE_MACHINE_FUNC)MlmeScanReqAction);
       StateMachineSetAction(Sm, SCAN_PENDING, MT2_PEER_BEACON, (STATE_MACHINE_FUNC)PeerBeacon);

 


	/* timer init */
	RTMPInitTimer(pAd, &pAd->MlmeAux.BeaconTimer, GET_TIMER_FUNCTION(BeaconTimeout), pAd, FALSE);
	RTMPInitTimer(pAd, &pAd->MlmeAux.ScanTimer, GET_TIMER_FUNCTION(ScanTimeout), pAd, FALSE);
}

/* 
	==========================================================================
	Description:
		Beacon timeout handler, executed in timer thread

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID BeaconTimeout(
	IN PVOID SystemSpecific1, 
	IN PVOID FunctionContext, 
	IN PVOID SystemSpecific2, 
	IN PVOID SystemSpecific3) 
{
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *)FunctionContext;

	DBGPRINT(RT_DEBUG_TRACE,("SYNC - BeaconTimeout\n"));
	
	/*
	    Do nothing if the driver is starting halt state.
	    This might happen when timer already been fired before cancel timer with mlmehalt
	*/
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))
		return;
	
#ifdef DOT11_N_SUPPORT
	if ((pAd->CommonCfg.BBPCurrentBW == BW_40)
		)
	{
		UCHAR        BBPValue = 0;
		AsicSwitchChannel(pAd, pAd->CommonCfg.CentralChannel, FALSE);
		AsicLockChannel(pAd, pAd->CommonCfg.CentralChannel);
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
		BBPValue &= (~0x18);
		BBPValue |= 0x10;
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);
		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - End of SCAN, restore to 40MHz channel %d, Total BSS[%02d]\n",pAd->CommonCfg.CentralChannel, pAd->ScanTab.BssNr));
	}
#endif /* DOT11_N_SUPPORT */

	MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_BEACON_TIMEOUT, 0, NULL, 0);
	RTMP_MLME_HANDLER(pAd);
}

/* 
	==========================================================================
	Description:
		Scan timeout handler, executed in timer thread

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID ScanTimeout(
	IN PVOID SystemSpecific1, 
	IN PVOID FunctionContext, 
	IN PVOID SystemSpecific2, 
	IN PVOID SystemSpecific3) 
{
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *)FunctionContext;

	
	/* 
	    Do nothing if the driver is starting halt state.
	    This might happen when timer already been fired before cancel timer with mlmehalt
	*/
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))
		return;
	
	if (MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_SCAN_TIMEOUT, 0, NULL, 0))
	{
	RTMP_MLME_HANDLER(pAd);
}
	else
	{
		/* To prevent SyncMachine.CurrState is SCAN_LISTEN forever. */
		pAd->MlmeAux.Channel = 0;
		ScanNextChannel(pAd, OPMODE_STA);
			RTMPSendWirelessEvent(pAd, IW_SCAN_ENQUEUE_FAIL_EVENT_FLAG, NULL, BSS0, 0); 
	}
}


VOID MlmeForceJoinReqAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	UCHAR        BBPValue = 0;
	BSS_ENTRY    *pBss;
	BOOLEAN        TimerCancelled;
	HEADER_802_11 Hdr80211;	
	NDIS_STATUS   NStatus;
	ULONG         FrameLen = 0;	
	PUCHAR        pOutBuffer = NULL;
	PUCHAR        pSupRate = NULL;
	UCHAR         SupRateLen;
	PUCHAR        pExtRate = NULL;
	UCHAR         ExtRateLen;	
	UCHAR         ASupRate[] = {0x8C, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6C};
	UCHAR         ASupRateLen = sizeof(ASupRate)/sizeof(UCHAR);	
	MLME_JOIN_REQ_STRUCT *pInfo = (MLME_JOIN_REQ_STRUCT *)(Elem->Msg);

#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
	POS_COOKIE  pObj = (POS_COOKIE) pAd->OS_Cookie;
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */


	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeForeJoinReqAction(BSS #%ld)\n", pInfo->BssIdx));

#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND

		if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND))
		{
			if( (RTMP_Usb_AutoPM_Get_Interface(pObj->pUsb_Dev,pObj->intf)) == 1)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("MlmeForeJoinReqAction: autopm_resume success\n"));
				RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_SUSPEND);
			}
			else if ((RTMP_Usb_AutoPM_Get_Interface(pObj->pUsb_Dev,pObj->intf)) == (-1))
		{
				DBGPRINT(RT_DEBUG_ERROR, ("MlmeiForeJoinReqAction autopm_resume fail ------\n"));
				RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_SUSPEND);
				return;
		}
			else
				DBGPRINT(RT_DEBUG_TRACE, ("MlmeJoinReqAction: autopm_resume do nothing \n"));

	}
		else
	{
			DBGPRINT(RT_DEBUG_TRACE, ("MlmeJoinReqAction: fRTMP_ADAPTER_CPU_SUSPEND\n"));
		return;
	}

#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */



#ifdef PCIE_PS_SUPPORT
    if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE)) && 
        (IDLE_ON(pAd)) &&
		(pAd->StaCfg.bRadio == TRUE) &&
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF)))
	{
			RT28xxPciAsicRadioOn(pAd, GUI_IDLE_POWER_SAVE);
	}
#endif /* PCIE_PS_SUPPORT */

	/* reset all the timers */
	RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);
	RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);

/*	pBss = &pAd->MlmeAux.SsidBssTab.BssEntry[pInfo->BssIdx];*/


        DBGPRINT(RT_DEBUG_TRACE, ("force join %02x:%02x:%02x:%02x:%02x:%02x\n",
        pAd->StaARCfg.BssEntry.Bssid[0], pAd->StaARCfg.BssEntry.Bssid[1], pAd->StaARCfg.BssEntry.Bssid[2],
        pAd->StaARCfg.BssEntry.Bssid[3], pAd->StaARCfg.BssEntry.Bssid[4], pAd->StaARCfg.BssEntry.Bssid[5]));
	 printk("force join  ssid %s ssidlen %d bsstype %d channel %d\n"
		,pAd->StaARCfg.BssEntry.Ssid,pAd->StaARCfg.BssEntry.SsidLen,pAd->StaARCfg.BssEntry.BssType,pAd->StaARCfg.BssEntry.Channel);



	/* record the desired SSID & BSSID we're waiting for */

/*	COPY_MAC_ADDR(pAd->MlmeAux.Bssid, pBss->Bssid);*/

	
	/* If AP's SSID is not hidden, it is OK for updating ssid to MlmeAux again. */
/*ralink debug*/
/*	if (pBss->Hidden == 0)*/
	{
/*
		RTMPZeroMemory(pAd->MlmeAux.Ssid, MAX_LEN_OF_SSID);
		NdisMoveMemory(pAd->MlmeAux.Ssid, pBss->Ssid, pBss->SsidLen);	
		pAd->MlmeAux.SsidLen = pBss->SsidLen;
		*/
		RTMPZeroMemory(pAd->MlmeAux.Ssid, MAX_LEN_OF_SSID);
		NdisMoveMemory(pAd->MlmeAux.Ssid, pAd->StaARCfg.BssEntry.Ssid, pAd->StaARCfg.BssEntry.SsidLen);	
		pAd->MlmeAux.SsidLen = pAd->StaARCfg.BssEntry.SsidLen;
		
	}
		/*
	pAd->MlmeAux.BssType = pBss->BssType;
	pAd->MlmeAux.Channel = pBss->Channel;
	pAd->MlmeAux.CentralChannel = pBss->CentralChannel;
		*/
	pAd->MlmeAux.BssType = pAd->StaARCfg.BssEntry.BssType;
	pAd->MlmeAux.Channel = pAd->StaARCfg.BssEntry.Channel;
	pAd->MlmeAux.CentralChannel = pAd->StaARCfg.BssEntry.Channel;

/*ralink debug*/
#if 0
#ifdef EXT_BUILD_CHANNEL_LIST
	/* Country IE of the AP will be evaluated and will be used. */
	if ((pAd->StaCfg.IEEE80211dClientMode != Rt802_11_D_None) &&
		(pBss->bHasCountryIE == TRUE))
		{
		NdisMoveMemory(&pAd->CommonCfg.CountryCode[0], &pBss->CountryString[0], 2);
		if (pBss->CountryString[2] == 'I')
			pAd->CommonCfg.Geography = IDOR;
		else if (pBss->CountryString[2] == 'O')
			pAd->CommonCfg.Geography = ODOR;
		else
			pAd->CommonCfg.Geography = BOTH;
		BuildChannelListEx(pAd);
		}
#endif /* EXT_BUILD_CHANNEL_LIST */
#endif	
	/* Let BBP register at 20MHz to do scan */
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
	BBPValue &= (~0x18);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);
	pAd->CommonCfg.BBPCurrentBW = BW_20;
	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - BBP R4 to 20MHz.l\n"));

	/* switch channel and waiting for beacon timer */
	AsicSwitchChannel(pAd, pAd->MlmeAux.Channel, FALSE);
	AsicLockChannel(pAd, pAd->MlmeAux.Channel);
		

	RTMPSetTimer(&pAd->MlmeAux.BeaconTimer, JOIN_TIMEOUT);

    do
	{
		if (((pAd->CommonCfg.bIEEE80211H == 1) && 
            (pAd->MlmeAux.Channel > 14) && 
             RadarChannelCheck(pAd, pAd->MlmeAux.Channel))
#ifdef CARRIER_DETECTION_SUPPORT /* Roger sync Carrier */             
             || (pAd->CommonCfg.CarrierDetect.Enable == TRUE)
#endif /* CARRIER_DETECTION_SUPPORT */
            )
		{
			/*
			    We can't send any Probe request frame to meet 802.11h.
			*/
			/*if (pBss->Hidden == 0)*/
/*ralink debug*/
/*
			if (pBss->Hidden == 0)
			break;
*/
		}

		/*
	    send probe request
	*/
	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);
	if (NStatus == NDIS_STATUS_SUCCESS)
	{
		if (pAd->MlmeAux.Channel <= 14)
		{
			pSupRate = pAd->CommonCfg.SupRate;
			SupRateLen = pAd->CommonCfg.SupRateLen;
			pExtRate = pAd->CommonCfg.ExtRate;
			ExtRateLen = pAd->CommonCfg.ExtRateLen;
		}
		else
		{
			/*
		           Overwrite Support Rate, CCK rate are not allowed
		*/
			pSupRate = ASupRate;
			SupRateLen = ASupRateLen;
			ExtRateLen = 0;
		}

		if ((pAd->MlmeAux.BssType == BSS_INFRA)  && (!MAC_ADDR_EQUAL(ZERO_MAC_ADDR, pAd->StaARCfg.BssEntry.Bssid)))
		{
			COPY_MAC_ADDR(pAd->MlmeAux.Bssid, pAd->StaARCfg.BssEntry.Bssid);
			MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, pAd->MlmeAux.Bssid,
								pAd->MlmeAux.Bssid);
		}
		else
			MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, BROADCAST_ADDR,
								BROADCAST_ADDR);

		MakeOutgoingFrame(pOutBuffer,               &FrameLen,
						  sizeof(HEADER_802_11),    &Hdr80211,
						  1,                        &SsidIe,
						  1,                        &pAd->MlmeAux.SsidLen,
						  pAd->MlmeAux.SsidLen,	    pAd->MlmeAux.Ssid,
						  1,                        &SupRateIe,
						  1,                        &SupRateLen,
						  SupRateLen,               pSupRate,
						  END_OF_ARGS);

		if (ExtRateLen)
		{
			ULONG Tmp;
			MakeOutgoingFrame(pOutBuffer + FrameLen,            &Tmp,
							  1,                                &ExtRateIe,
							  1,                                &ExtRateLen,
							  ExtRateLen,                       pExtRate, 
							  END_OF_ARGS);
			FrameLen += Tmp;
	} 
		


#ifdef WPA_SUPPLICANT_SUPPORT
		if ((pAd->OpMode == OPMODE_STA) &&
			(pAd->StaCfg.WpaSupplicantUP != WPA_SUPPLICANT_DISABLE) &&
			(pAd->StaCfg.WpsProbeReqIeLen != 0))
	{
			ULONG 		WpsTmpLen = 0;
			
			MakeOutgoingFrame(pOutBuffer + FrameLen,              &WpsTmpLen,
							pAd->StaCfg.WpsProbeReqIeLen,	pAd->StaCfg.pWpsProbeReqIe,
							END_OF_ARGS);

			FrameLen += WpsTmpLen;
		}
#endif /* WPA_SUPPLICANT_SUPPORT */

		MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
		MlmeFreeMemory(pAd, pOutBuffer);
	}
    } while (FALSE);

	DBGPRINT(0, ("FORCE JOIN SYNC - Switch to ch %d, Wait BEACON from %02x:%02x:%02x:%02x:%02x:%02x\n", 
		pAd->StaARCfg.BssEntry.Channel, pAd->StaARCfg.BssEntry.Bssid[0], pAd->StaARCfg.BssEntry.Bssid[1], pAd->StaARCfg.BssEntry.Bssid[2],
		pAd->StaARCfg.BssEntry.Bssid[3], pAd->StaARCfg.BssEntry.Bssid[4], pAd->StaARCfg.BssEntry.Bssid[5]));

	pAd->Mlme.SyncMachine.CurrState = JOIN_WAIT_BEACON;
}


VOID MlmeForceScanReqAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem)
{
	UCHAR          Ssid[MAX_LEN_OF_SSID], SsidLen, ScanType, BssType, BBPValue = 0;
	BOOLEAN        TimerCancelled;
	ULONG		   Now;
	USHORT         Status;
printk("MlmeForceScanReqAction\n");
#ifdef RTMP_MAC_USB
	if(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
	{
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
		if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND))
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */
		{
			RT28xxUsbAsicRadioOn(pAd);
		}
	}
#endif /* RTMP_MAC_USB */
       /*
	    Check the total scan tries for one single OID command
	    If this is the CCX 2.0 Case, skip that!
	*/
	if ( !RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_START_UP))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeForceScanReqAction before Startup\n"));
		return;
	}

#ifdef PCIE_PS_SUPPORT
    if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE)) && 
        (IDLE_ON(pAd)) &&
		(pAd->StaCfg.bRadio == TRUE) &&
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF)))
	{
	    if (pAd->StaCfg.PSControl.field.EnableNewPS == FALSE)
		{
			AsicSendCommandToMcu(pAd, 0x31, PowerWakeCID, 0x00, 0x02);   
			AsicCheckCommanOk(pAd, PowerWakeCID);	
			RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF);
			DBGPRINT(RT_DEBUG_TRACE, ("PSM - Issue Wake up command \n"));
		}
		else
		{
			RT28xxPciAsicRadioOn(pAd, GUI_IDLE_POWER_SAVE);
		}
	}
#endif /* PCIE_PS_SUPPORT */

	/* first check the parameter sanity */
	if (MlmeScanReqSanity(pAd, 
						  Elem->Msg, 
						  Elem->MsgLen, 
						  &BssType, 
						  (PCHAR)Ssid, 
						  &SsidLen, 
						  &ScanType)) 
	{

		/* 
		     Check for channel load and noise hist request
		     Suspend MSDU only at scan request, not the last two mentioned
		     Suspend MSDU transmission here
		*/
		RTMPSuspendMsduTransmission(pAd);
		
		/*
		    To prevent data lost.	
		    Send an NULL data with turned PSM bit on to current associated AP before SCAN progress.
		    And should send an NULL data with turned PSM bit off to AP, when scan progress done 
		*/
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED) && (INFRA_ON(pAd)))
		{
			RTMPSendNullFrame(pAd, 
							  pAd->CommonCfg.TxRate, 
							  (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) ? TRUE:FALSE));
			DBGPRINT(RT_DEBUG_TRACE, ("MlmeForceScanReqAction -- Send PSM Data frame for off channel RM, SCAN_IN_PROGRESS=%d!\n",
											RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)));
				OS_WAIT(20);
		}
		
			RTMPSendWirelessEvent(pAd, IW_SCANNING_EVENT_FLAG, NULL, BSS0, 0);

		NdisGetSystemUpTime(&Now);
		pAd->StaCfg.LastScanTime = Now;
		/* reset all the timers */
		RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);

		/* record desired BSS parameters */
		pAd->MlmeAux.BssType = BssType;
		pAd->MlmeAux.ScanType = ScanType;
		pAd->MlmeAux.SsidLen = SsidLen;
        NdisZeroMemory(pAd->MlmeAux.Ssid, MAX_LEN_OF_SSID);
		NdisMoveMemory(pAd->MlmeAux.Ssid, Ssid, SsidLen);

		/*
			Scanning was pending (for fast scanning)
		*/
		if ((pAd->StaCfg.bImprovedScan) && (pAd->Mlme.SyncMachine.CurrState == SCAN_PENDING))
		{
			pAd->MlmeAux.Channel = pAd->StaCfg.LastScanChannel;
		}
		else
		{
			if (pAd->StaCfg.bFastConnect && (pAd->CommonCfg.Channel != 0) && !pAd->StaCfg.bNotFirstScan)
			{
		pAd->MlmeAux.Channel = pAd->CommonCfg.Channel;
			}
			else
				/* start from the first channel */
				pAd->MlmeAux.Channel = FirstChannel(pAd);
		}

		/* Let BBP register at 20MHz to do scan */
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
		BBPValue &= (~0x18);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);
		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - BBP R4 to 20MHz.l\n"));
#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
		/* Before scan, reset trigger event table. */
		TriEventInit(pAd);
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */

		ScanNextChannel(pAd, OPMODE_STA);
		if(pAd->StaARCfg.BssEntry.Channel != 0)
			pAd->MlmeAux.Channel = 0;
		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_SCAN_FOR_CONNECT;
	} 
	else 
	{
		DBGPRINT_ERR(("SYNC - MlmeForceScanReqAction() sanity check fail\n"));
		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_SCAN_CONF, 2, &Status, 0);
	}
}

/* 
	==========================================================================
	Description:
		MLME SCAN req state machine procedure
	==========================================================================
 */
VOID MlmeScanReqAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	UCHAR          Ssid[MAX_LEN_OF_SSID], SsidLen, ScanType, BssType, BBPValue = 0;
	BOOLEAN        TimerCancelled;
	ULONG		   Now;
	USHORT         Status;
#ifdef RTMP_MAC_USB
	if(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
	{
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
		if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND))
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */
		{
			RT28xxUsbAsicRadioOn(pAd);
		}
	}
#endif /* RTMP_MAC_USB */
       /*
	    Check the total scan tries for one single OID command
	    If this is the CCX 2.0 Case, skip that!
	*/
	if ( !RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_START_UP))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeScanReqAction before Startup\n"));
		return;
	}

#ifdef PCIE_PS_SUPPORT
    if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE)) && 
        (IDLE_ON(pAd)) &&
		(pAd->StaCfg.bRadio == TRUE) &&
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF)))
	{
	    if (pAd->StaCfg.PSControl.field.EnableNewPS == FALSE)
		{
			AsicSendCommandToMcu(pAd, 0x31, PowerWakeCID, 0x00, 0x02);   
			AsicCheckCommanOk(pAd, PowerWakeCID);	
			RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF);
			DBGPRINT(RT_DEBUG_TRACE, ("PSM - Issue Wake up command \n"));
		}
		else
		{
			RT28xxPciAsicRadioOn(pAd, GUI_IDLE_POWER_SAVE);
		}
	}
#endif /* PCIE_PS_SUPPORT */

	/* first check the parameter sanity */
	if (MlmeScanReqSanity(pAd, 
						  Elem->Msg, 
						  Elem->MsgLen, 
						  &BssType, 
						  (PCHAR)Ssid, 
						  &SsidLen, 
						  &ScanType)) 
	{

		/* 
		     Check for channel load and noise hist request
		     Suspend MSDU only at scan request, not the last two mentioned
		     Suspend MSDU transmission here
		*/
		RTMPSuspendMsduTransmission(pAd);
		
		/*
		    To prevent data lost.	
		    Send an NULL data with turned PSM bit on to current associated AP before SCAN progress.
		    And should send an NULL data with turned PSM bit off to AP, when scan progress done 
		*/
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED) && (INFRA_ON(pAd)))
		{
			RTMPSendNullFrame(pAd, 
							  pAd->CommonCfg.TxRate, 
							  (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) ? TRUE:FALSE));
			DBGPRINT(RT_DEBUG_TRACE, ("MlmeScanReqAction -- Send PSM Data frame for off channel RM, SCAN_IN_PROGRESS=%d!\n",
											RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)));
				OS_WAIT(20);
		}
		
			RTMPSendWirelessEvent(pAd, IW_SCANNING_EVENT_FLAG, NULL, BSS0, 0);

		NdisGetSystemUpTime(&Now);
		pAd->StaCfg.LastScanTime = Now;
		/* reset all the timers */
		RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);

		/* record desired BSS parameters */
		pAd->MlmeAux.BssType = BssType;
		pAd->MlmeAux.ScanType = ScanType;
		pAd->MlmeAux.SsidLen = SsidLen;
        NdisZeroMemory(pAd->MlmeAux.Ssid, MAX_LEN_OF_SSID);
		NdisMoveMemory(pAd->MlmeAux.Ssid, Ssid, SsidLen);

		/*
			Scanning was pending (for fast scanning)
		*/
		if ((pAd->StaCfg.bImprovedScan) && (pAd->Mlme.SyncMachine.CurrState == SCAN_PENDING))
		{
			pAd->MlmeAux.Channel = pAd->StaCfg.LastScanChannel;
		}
		else
		{
			if (pAd->StaCfg.bFastConnect && (pAd->CommonCfg.Channel != 0) && !pAd->StaCfg.bNotFirstScan)
			{
				pAd->MlmeAux.Channel = pAd->CommonCfg.Channel;
			}
			else
				/* start from the first channel */
				pAd->MlmeAux.Channel = FirstChannel(pAd);
		}

		/* Let BBP register at 20MHz to do scan */
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
		BBPValue &= (~0x18);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);
		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - BBP R4 to 20MHz.l\n"));
#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
		/* Before scan, reset trigger event table. */
		TriEventInit(pAd);
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */


		ScanNextChannel(pAd, OPMODE_STA);
		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_LIST_SCAN;
	} 
	else 
	{
		DBGPRINT_ERR(("SYNC - MlmeScanReqAction() sanity check fail\n"));
		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_SCAN_CONF, 2, &Status, 0);
	}
}

/* 
	==========================================================================
	Description:
		MLME JOIN req state machine procedure
	==========================================================================
 */
VOID MlmeJoinReqAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	UCHAR        BBPValue = 0;
	BSS_ENTRY    *pBss;
	BOOLEAN       TimerCancelled;
	HEADER_802_11 Hdr80211;	
	NDIS_STATUS   NStatus;
	ULONG         FrameLen = 0;	
	PUCHAR        pOutBuffer = NULL;
	PUCHAR        pSupRate = NULL;
	UCHAR         SupRateLen;
	PUCHAR        pExtRate = NULL;
	UCHAR         ExtRateLen;	
	UCHAR         ASupRate[] = {0x8C, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6C};
	UCHAR         ASupRateLen = sizeof(ASupRate)/sizeof(UCHAR);	
	MLME_JOIN_REQ_STRUCT *pInfo = (MLME_JOIN_REQ_STRUCT *)(Elem->Msg);

#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
	POS_COOKIE  pObj = (POS_COOKIE) pAd->OS_Cookie;
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */


	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeJoinReqAction(BSS #%ld)\n", pInfo->BssIdx));

#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND

		if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND))
		{
			if( (RTMP_Usb_AutoPM_Get_Interface(pObj->pUsb_Dev,pObj->intf)) == 1)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("MlmeJoinReqAction: autopm_resume success\n"));
				RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_SUSPEND);
			}
			else if ((RTMP_Usb_AutoPM_Get_Interface(pObj->pUsb_Dev,pObj->intf)) == (-1))
			{
				DBGPRINT(RT_DEBUG_ERROR, ("MlmeJoinReqAction autopm_resume fail ------\n"));
				RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_SUSPEND);
				return;
			}
			else
				DBGPRINT(RT_DEBUG_TRACE, ("MlmeJoinReqAction: autopm_resume do nothing \n"));

		}
		else
		{
			DBGPRINT(RT_DEBUG_TRACE, ("MlmeJoinReqAction: fRTMP_ADAPTER_CPU_SUSPEND\n"));
			return;
		}

#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */



#ifdef PCIE_PS_SUPPORT
    if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE)) && 
        (IDLE_ON(pAd)) &&
		(pAd->StaCfg.bRadio == TRUE) &&
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF)))
	{
		RT28xxPciAsicRadioOn(pAd, GUI_IDLE_POWER_SAVE);
	}
#endif /* PCIE_PS_SUPPORT */

	/* reset all the timers */
	RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);
	RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);

	pBss = &pAd->MlmeAux.SsidBssTab.BssEntry[pInfo->BssIdx];

	/* record the desired SSID & BSSID we're waiting for */
	COPY_MAC_ADDR(pAd->MlmeAux.Bssid, pBss->Bssid);
	
	/* If AP's SSID is not hidden, it is OK for updating ssid to MlmeAux again. */
	if (pBss->Hidden == 0)
	{
		RTMPZeroMemory(pAd->MlmeAux.Ssid, MAX_LEN_OF_SSID);
		NdisMoveMemory(pAd->MlmeAux.Ssid, pBss->Ssid, pBss->SsidLen);	
	pAd->MlmeAux.SsidLen = pBss->SsidLen;
	}
	
	pAd->MlmeAux.BssType = pBss->BssType;
	pAd->MlmeAux.Channel = pBss->Channel;
	pAd->MlmeAux.CentralChannel = pBss->CentralChannel;
	
#ifdef EXT_BUILD_CHANNEL_LIST
	/* Country IE of the AP will be evaluated and will be used. */
	if ((pAd->StaCfg.IEEE80211dClientMode != Rt802_11_D_None) &&
		(pBss->bHasCountryIE == TRUE))
	{
		NdisMoveMemory(&pAd->CommonCfg.CountryCode[0], &pBss->CountryString[0], 2);
		if (pBss->CountryString[2] == 'I')
			pAd->CommonCfg.Geography = IDOR;
		else if (pBss->CountryString[2] == 'O')
			pAd->CommonCfg.Geography = ODOR;
		else
			pAd->CommonCfg.Geography = BOTH;
		BuildChannelListEx(pAd);
	}
#endif /* EXT_BUILD_CHANNEL_LIST */
	
	/* Let BBP register at 20MHz to do scan */
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
	BBPValue &= (~0x18);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);
	pAd->CommonCfg.BBPCurrentBW = BW_20;
	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - BBP R4 to 20MHz.l\n"));

	/* switch channel and waiting for beacon timer */
	AsicSwitchChannel(pAd, pAd->MlmeAux.Channel, FALSE);
	AsicLockChannel(pAd, pAd->MlmeAux.Channel);


	RTMPSetTimer(&pAd->MlmeAux.BeaconTimer, JOIN_TIMEOUT);

    do
	{
		if (((pAd->CommonCfg.bIEEE80211H == 1) && 
            (pAd->MlmeAux.Channel > 14) && 
             RadarChannelCheck(pAd, pAd->MlmeAux.Channel))
#ifdef CARRIER_DETECTION_SUPPORT /* Roger sync Carrier */             
             || (pAd->CommonCfg.CarrierDetect.Enable == TRUE)
#endif /* CARRIER_DETECTION_SUPPORT */
            )
		{
			/*
			    We can't send any Probe request frame to meet 802.11h.
			*/
			if (pBss->Hidden == 0)
			break;
		}
        
	/*
	    send probe request
	*/
	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);
	if (NStatus == NDIS_STATUS_SUCCESS)
	{
		if (pAd->MlmeAux.Channel <= 14)
		{
			pSupRate = pAd->CommonCfg.SupRate;
			SupRateLen = pAd->CommonCfg.SupRateLen;
			pExtRate = pAd->CommonCfg.ExtRate;
			ExtRateLen = pAd->CommonCfg.ExtRateLen;
		}
		else
		{
			/*
		           Overwrite Support Rate, CCK rate are not allowed
			*/
			pSupRate = ASupRate;
			SupRateLen = ASupRateLen;
			ExtRateLen = 0;
		}

		if (pAd->MlmeAux.BssType == BSS_INFRA)
			MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, pAd->MlmeAux.Bssid,
								pAd->MlmeAux.Bssid);
		else
			MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, BROADCAST_ADDR,
								BROADCAST_ADDR);

		MakeOutgoingFrame(pOutBuffer,               &FrameLen,
						  sizeof(HEADER_802_11),    &Hdr80211,
						  1,                        &SsidIe,
						  1,                        &pAd->MlmeAux.SsidLen,
						  pAd->MlmeAux.SsidLen,	    pAd->MlmeAux.Ssid,
						  1,                        &SupRateIe,
						  1,                        &SupRateLen,
						  SupRateLen,               pSupRate,
						  END_OF_ARGS);

		if (ExtRateLen)
		{
			ULONG Tmp;
			MakeOutgoingFrame(pOutBuffer + FrameLen,            &Tmp,
							  1,                                &ExtRateIe,
							  1,                                &ExtRateLen,
							  ExtRateLen,                       pExtRate, 
							  END_OF_ARGS);
			FrameLen += Tmp;
		}
		


#ifdef WPA_SUPPLICANT_SUPPORT
		if ((pAd->OpMode == OPMODE_STA) &&
			(pAd->StaCfg.WpaSupplicantUP != WPA_SUPPLICANT_DISABLE) &&
			(pAd->StaCfg.WpsProbeReqIeLen != 0))
		{
			ULONG 		WpsTmpLen = 0;
			
			MakeOutgoingFrame(pOutBuffer + FrameLen,              &WpsTmpLen,
							pAd->StaCfg.WpsProbeReqIeLen,	pAd->StaCfg.pWpsProbeReqIe,
							END_OF_ARGS);

			FrameLen += WpsTmpLen;
		}
#endif /* WPA_SUPPLICANT_SUPPORT */

		MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
		MlmeFreeMemory(pAd, pOutBuffer);
	}
    } while (FALSE);

	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - Switch to ch %d, Wait BEACON from %02x:%02x:%02x:%02x:%02x:%02x\n", 
		pBss->Channel, pBss->Bssid[0], pBss->Bssid[1], pBss->Bssid[2], pBss->Bssid[3], pBss->Bssid[4], pBss->Bssid[5]));

	pAd->Mlme.SyncMachine.CurrState = JOIN_WAIT_BEACON;
}

/* 
	==========================================================================
	Description:
		MLME START Request state machine procedure, starting an IBSS
	==========================================================================
 */
VOID MlmeStartReqAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	UCHAR         Ssid[MAX_LEN_OF_SSID], SsidLen; 
	BOOLEAN       TimerCancelled;

	/* New for WPA security suites */
	UCHAR						*VarIE = NULL;
	NDIS_802_11_VARIABLE_IEs	*pVIE = NULL;
	LARGE_INTEGER				TimeStamp;
	BOOLEAN Privacy;
	USHORT Status;


	/* allocate memory */
	os_alloc_mem(NULL, (UCHAR **)&VarIE, MAX_VIE_LEN);
	if (VarIE == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));
		return;
	}

	/* Init Variable IE structure */
	pVIE = (PNDIS_802_11_VARIABLE_IEs) VarIE;
	pVIE->Length = 0;
	TimeStamp.u.LowPart  = 0;
	TimeStamp.u.HighPart = 0;

	if ((MlmeStartReqSanity(pAd, Elem->Msg, Elem->MsgLen, (PCHAR)Ssid, &SsidLen)) &&
		(CHAN_PropertyCheck(pAd, pAd->MlmeAux.Channel, CHANNEL_NO_IBSS) == FALSE))
	{
		/* reset all the timers */
		RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);
		RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);

		/*
		    Start a new IBSS. All IBSS parameters are decided now....
		*/
		DBGPRINT(RT_DEBUG_TRACE, ("MlmeStartReqAction - Start a new IBSS. All IBSS parameters are decided now.... \n"));
		pAd->MlmeAux.BssType           = BSS_ADHOC;
		NdisMoveMemory(pAd->MlmeAux.Ssid, Ssid, SsidLen); 
		pAd->MlmeAux.SsidLen           = SsidLen;

		/* generate a radom number as BSSID */
		MacAddrRandomBssid(pAd, pAd->MlmeAux.Bssid);
		DBGPRINT(RT_DEBUG_TRACE, ("MlmeStartReqAction - generate a radom number as BSSID \n"));

		Privacy = (pAd->StaCfg.WepStatus == Ndis802_11Encryption1Enabled) || 
				  (pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled) || 
				  (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled);
		pAd->MlmeAux.CapabilityInfo    = CAP_GENERATE(0,1,Privacy, (pAd->CommonCfg.TxPreamble == Rt802_11PreambleShort), 1, 0);
		pAd->MlmeAux.BeaconPeriod      = pAd->CommonCfg.BeaconPeriod;
		pAd->MlmeAux.AtimWin           = pAd->StaCfg.AtimWin;
		pAd->MlmeAux.Channel           = pAd->CommonCfg.Channel;

		pAd->CommonCfg.CentralChannel  = pAd->CommonCfg.Channel;
		pAd->MlmeAux.CentralChannel    = pAd->CommonCfg.CentralChannel;

		pAd->MlmeAux.SupRateLen= pAd->CommonCfg.SupRateLen;
		NdisMoveMemory(pAd->MlmeAux.SupRate, pAd->CommonCfg.SupRate, MAX_LEN_OF_SUPPORTED_RATES);
		RTMPCheckRates(pAd, pAd->MlmeAux.SupRate, &pAd->MlmeAux.SupRateLen);
		pAd->MlmeAux.ExtRateLen = pAd->CommonCfg.ExtRateLen;
		NdisMoveMemory(pAd->MlmeAux.ExtRate, pAd->CommonCfg.ExtRate, MAX_LEN_OF_SUPPORTED_RATES);
		RTMPCheckRates(pAd, pAd->MlmeAux.ExtRate, &pAd->MlmeAux.ExtRateLen);
#ifdef DOT11_N_SUPPORT
		if ((pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED) && (pAd->StaCfg.bAdhocN == TRUE))
		{
			RTMPUpdateHTIE(&pAd->CommonCfg.DesiredHtPhy, &pAd->StaCfg.DesiredHtPhyInfo.MCSSet[0], &pAd->MlmeAux.HtCapability, &pAd->MlmeAux.AddHtInfo);
			pAd->MlmeAux.HtCapabilityLen = sizeof(HT_CAPABILITY_IE);
			/* Not turn pAd->StaActive.SupportedHtPhy.bHtEnable = TRUE here. */
			DBGPRINT(RT_DEBUG_TRACE, ("SYNC -pAd->StaActive.SupportedHtPhy.bHtEnable = TRUE\n"));
		}
		else
#endif /* DOT11_N_SUPPORT */
		{
			pAd->MlmeAux.HtCapabilityLen = 0;
			pAd->StaActive.SupportedPhyInfo.bHtEnable = FALSE;
			NdisZeroMemory(&pAd->StaActive.SupportedPhyInfo.MCSSet[0], 16);
		}
		/* temporarily not support QOS in IBSS */
		NdisZeroMemory(&pAd->MlmeAux.APEdcaParm, sizeof(EDCA_PARM));
		NdisZeroMemory(&pAd->MlmeAux.APQbssLoad, sizeof(QBSS_LOAD_PARM));
		NdisZeroMemory(&pAd->MlmeAux.APQosCapability, sizeof(QOS_CAPABILITY_PARM));

		AsicSwitchChannel(pAd, pAd->MlmeAux.Channel, FALSE);
		AsicLockChannel(pAd, pAd->MlmeAux.Channel);

		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeStartReqAction(ch= %d,sup rates= %d, ext rates=%d)\n",
			pAd->MlmeAux.Channel, pAd->MlmeAux.SupRateLen, pAd->MlmeAux.ExtRateLen));

		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		Status = MLME_SUCCESS;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_START_CONF, 2, &Status, 0);
	} 
	else 
	{
		DBGPRINT_ERR(("SYNC - MlmeStartReqAction() sanity check fail.\n"));
		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_START_CONF, 2, &Status, 0);
	}

	if (VarIE != NULL)
		os_free_mem(NULL, VarIE);
}

/* 
	==========================================================================
	Description:
		peer sends beacon back when scanning
	==========================================================================
 */
VOID PeerBeaconAtScanAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	UCHAR           Bssid[MAC_ADDR_LEN], Addr2[MAC_ADDR_LEN];
	UCHAR           /* Ssid[MAX_LEN_OF_SSID], */ BssType, Channel = 0, NewChannel,
					SsidLen=0, DtimCount, DtimPeriod, BcastFlag, MessageToMe;
	UCHAR			*Ssid = NULL;
	CF_PARM         CfParm;
	USHORT          BeaconPeriod, AtimWin, CapabilityInfo;
	PFRAME_802_11   pFrame;
	LARGE_INTEGER   TimeStamp;
	UCHAR           Erp;
	UCHAR         	SupRate[MAX_LEN_OF_SUPPORTED_RATES], ExtRate[MAX_LEN_OF_SUPPORTED_RATES];
	UCHAR		  	SupRateLen, ExtRateLen;
	USHORT 			LenVIE;
	UCHAR			CkipFlag;
	UCHAR			AironetCellPowerLimit;
	EDCA_PARM       EdcaParm;
	QBSS_LOAD_PARM  QbssLoad;
	QOS_CAPABILITY_PARM QosCapability;
	ULONG						RalinkIe;
	UCHAR						*VarIE = NULL;
	NDIS_802_11_VARIABLE_IEs	*pVIE = NULL;
	HT_CAPABILITY_IE		*pHtCapability = NULL;
	ADD_HT_INFO_IE		*pAddHtInfo = NULL;	/* AP might use this additional ht info IE */
	UCHAR			HtCapabilityLen = 0, PreNHtCapabilityLen = 0;
	UCHAR			AddHtInfoLen;
	UCHAR			NewExtChannelOffset = 0xff;

	EXT_CAP_INFO_ELEMENT	ExtCapInfo;

	/* allocate memory */
	os_alloc_mem(NULL, (UCHAR **)&Ssid, MAX_LEN_OF_SSID);
	if (Ssid == NULL)
		goto LabelErr;
	os_alloc_mem(NULL, (UCHAR **)&VarIE, MAX_VIE_LEN);
	if (VarIE == NULL)
		goto LabelErr;
	os_alloc_mem(NULL, (UCHAR **)&pHtCapability, sizeof(HT_CAPABILITY_IE));
	if (pHtCapability == NULL)
		goto LabelErr;
	os_alloc_mem(NULL, (UCHAR **)&pAddHtInfo, sizeof(ADD_HT_INFO_IE));
	if (pAddHtInfo == NULL)
		goto LabelErr;

	NdisZeroMemory(Ssid, MAX_LEN_OF_SSID);
	pFrame = (PFRAME_802_11) Elem->Msg;
	/* Init Variable IE structure */
	pVIE = (PNDIS_802_11_VARIABLE_IEs) VarIE;
	pVIE->Length = 0;
#ifdef DOT11_N_SUPPORT
    RTMPZeroMemory(pHtCapability, sizeof(HT_CAPABILITY_IE));
	RTMPZeroMemory(pAddHtInfo, sizeof(ADD_HT_INFO_IE));
#endif /* DOT11_N_SUPPORT */

	NdisZeroMemory(&QbssLoad, sizeof(QBSS_LOAD_PARM)); /* woody */
	if (PeerBeaconAndProbeRspSanity(pAd, 
								Elem->Msg, 
								Elem->MsgLen, 
								Elem->Channel,
								Addr2, 
								Bssid, 
								(PCHAR)Ssid, 
								&SsidLen, 
								&BssType, 
								&BeaconPeriod, 
								&Channel, 
								&NewChannel,
								&TimeStamp, 
								&CfParm, 
								&AtimWin, 
								&CapabilityInfo, 
								&Erp,
								&DtimCount, 
								&DtimPeriod, 
								&BcastFlag, 
								&MessageToMe, 
								SupRate,
								&SupRateLen,
								ExtRate,
								&ExtRateLen,
								&CkipFlag,
								&AironetCellPowerLimit,
								&EdcaParm,
								&QbssLoad,
								&QosCapability,
								&RalinkIe,
								&HtCapabilityLen,
#ifdef CONFIG_STA_SUPPORT
								&PreNHtCapabilityLen,
#endif /* CONFIG_STA_SUPPORT */
								pHtCapability,
								&ExtCapInfo,
								&AddHtInfoLen,
								pAddHtInfo,
								&NewExtChannelOffset,
								&LenVIE,
								pVIE)) 
	{
		ULONG Idx = 0;
		CHAR Rssi = 0;

		Idx = BssTableSearch(&pAd->ScanTab, Bssid, Channel);
		if (Idx != BSS_NOT_FOUND) 
			Rssi = pAd->ScanTab.BssEntry[Idx].Rssi;

		Rssi = RTMPMaxRssi(pAd, ConvertToRssi(pAd, Elem->Rssi0, RSSI_0), ConvertToRssi(pAd, Elem->Rssi1, RSSI_1), ConvertToRssi(pAd, Elem->Rssi2, RSSI_2));


#ifdef DOT11_N_SUPPORT
		if ((HtCapabilityLen > 0) || (PreNHtCapabilityLen > 0))
			HtCapabilityLen = SIZE_HT_CAP_IE;
#endif /* DOT11_N_SUPPORT */

		Idx = BssTableSetEntry(pAd, &pAd->ScanTab, Bssid, (PCHAR)Ssid, SsidLen, BssType, BeaconPeriod,
					  &CfParm, AtimWin, CapabilityInfo, SupRate, SupRateLen, ExtRate, ExtRateLen,  pHtCapability,
					 pAddHtInfo, HtCapabilityLen, AddHtInfoLen, NewExtChannelOffset, Channel, Rssi, TimeStamp, CkipFlag, 
					 &EdcaParm, &QosCapability, &QbssLoad, LenVIE, pVIE);
#ifdef DOT11_N_SUPPORT
		/* TODO: Check for things need to do when enable "DOT11V_WNM_SUPPORT" */
#ifdef DOT11N_DRAFT3
		/* Check if this scan channel is the effeced channel */
		if (INFRA_ON(pAd) 
			&& (pAd->CommonCfg.bBssCoexEnable == TRUE) 
			&& ((Channel > 0) && (Channel <= 14)))
		{
			int chListIdx;

			/* 
				First we find the channel list idx by the channel number
			*/
			for (chListIdx = 0; chListIdx < pAd->ChannelListNum; chListIdx++)
			{
				if (Channel == pAd->ChannelList[chListIdx].Channel)
					break;
			}

			if (chListIdx < pAd->ChannelListNum)
			{
				/* 
					If this channel is effected channel for the 20/40 coex operation. Check the related IEs.
				*/
				if (pAd->ChannelList[chListIdx].bEffectedChannel == TRUE)
				{
					UCHAR RegClass;
					OVERLAP_BSS_SCAN_IE	BssScan;

					/* Read Beacon's Reg Class IE if any. */
					PeerBeaconAndProbeRspSanity2(pAd, Elem->Msg, Elem->MsgLen, &BssScan, &RegClass);
					TriEventTableSetEntry(pAd, &pAd->CommonCfg.TriggerEventTab, Bssid, pHtCapability, HtCapabilityLen, RegClass, Channel);
				}
			}
		}
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */
		if (Idx != BSS_NOT_FOUND)
		{
			PBSS_ENTRY	pBssEntry = &pAd->ScanTab.BssEntry[Idx];
			NdisMoveMemory(pBssEntry->PTSF, &Elem->Msg[24], 4);
			NdisMoveMemory(&pBssEntry->TTSF[0], &Elem->TimeStamp.u.LowPart, 4);
			NdisMoveMemory(&pBssEntry->TTSF[4], &Elem->TimeStamp.u.LowPart, 4);

			pBssEntry->MinSNR = Elem->Signal % 10;
			if (pBssEntry->MinSNR == 0)
				pBssEntry->MinSNR = -5;

			NdisMoveMemory(pBssEntry->MacAddr, Addr2, MAC_ADDR_LEN);

			if ((pFrame->Hdr.FC.SubType == SUBTYPE_PROBE_RSP) &&
				(LenVIE != 0))
			{
				pBssEntry->VarIeFromProbeRspLen = 0;
				if (pBssEntry->pVarIeFromProbRsp)
				{
					pBssEntry->VarIeFromProbeRspLen = LenVIE;
					RTMPZeroMemory(pBssEntry->pVarIeFromProbRsp, MAX_VIE_LEN);
					RTMPMoveMemory(pBssEntry->pVarIeFromProbRsp, pVIE, LenVIE);
				}
			}
		}

#ifdef LINUX
#ifdef RT_CFG80211_SUPPORT
		RT_CFG80211_SCANNING_INFORM(pAd, Idx, Elem->Channel, (UCHAR *)pFrame,
									Elem->MsgLen, Rssi);
#endif /* RT_CFG80211_SUPPORT */
#endif /* LINUX */
	}
	/* sanity check fail, ignored */
	goto LabelOK;

LabelErr:
	DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));

LabelOK:
	if (Ssid != NULL)
		os_free_mem(NULL, Ssid);
	if (VarIE != NULL)
		os_free_mem(NULL, VarIE);
	if (pHtCapability != NULL)
		os_free_mem(NULL, pHtCapability);
	if (pAddHtInfo != NULL)
		os_free_mem(NULL, pAddHtInfo);
	return;
}

/* 
	==========================================================================
	Description:
		When waiting joining the (I)BSS, beacon received from external
	==========================================================================
 */
VOID PeerBeaconAtJoinAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	UCHAR         Bssid[MAC_ADDR_LEN], Addr2[MAC_ADDR_LEN];
	UCHAR         /* Ssid[MAX_LEN_OF_SSID], */ SsidLen=0, BssType, Channel, MessageToMe, 
				  DtimCount, DtimPeriod, BcastFlag, NewChannel; 
	UCHAR			*Ssid = NULL;
	LARGE_INTEGER TimeStamp;
	USHORT        BeaconPeriod, AtimWin, CapabilityInfo;
	CF_PARM       Cf;
	BOOLEAN       TimerCancelled;
	UCHAR         Erp;
	UCHAR         SupRate[MAX_LEN_OF_SUPPORTED_RATES], ExtRate[MAX_LEN_OF_SUPPORTED_RATES];
	UCHAR		  SupRateLen, ExtRateLen;
	UCHAR         CkipFlag;
	USHORT 		  LenVIE;
	UCHAR		  AironetCellPowerLimit;
	EDCA_PARM       EdcaParm;
	QBSS_LOAD_PARM  QbssLoad;
	QOS_CAPABILITY_PARM QosCapability;
	USHORT        Status;
	UCHAR						*VarIE = NULL;
	NDIS_802_11_VARIABLE_IEs	*pVIE = NULL;
	ULONG           RalinkIe;
	ULONG         Idx = 0;
	CHAR			Rssi = 0;
	HT_CAPABILITY_IE		*pHtCapability = NULL;
	ADD_HT_INFO_IE		*pAddHtInfo = NULL;	/* AP might use this additional ht info IE */
	UCHAR				HtCapabilityLen = 0, PreNHtCapabilityLen = 0;
	UCHAR			AddHtInfoLen;
	UCHAR			NewExtChannelOffset = 0xff;
#ifdef DOT11_N_SUPPORT
	UCHAR			CentralChannel;
	BOOLEAN			bAllowNrate = FALSE;
#endif /* DOT11_N_SUPPORT */
	EXT_CAP_INFO_ELEMENT	ExtCapInfo;


	/* allocate memory */
	os_alloc_mem(NULL, (UCHAR **)&Ssid, MAX_LEN_OF_SSID);
	if (Ssid == NULL)
		goto LabelErr;
	os_alloc_mem(NULL, (UCHAR **)&VarIE, MAX_VIE_LEN);
	if (VarIE == NULL)
		goto LabelErr;
	os_alloc_mem(NULL, (UCHAR **)&pHtCapability, sizeof(HT_CAPABILITY_IE));
	if (pHtCapability == NULL)
		goto LabelErr;
	os_alloc_mem(NULL, (UCHAR **)&pAddHtInfo, sizeof(ADD_HT_INFO_IE));
	if (pAddHtInfo == NULL)
		goto LabelErr;

	/* Init Variable IE structure */
	pVIE = (PNDIS_802_11_VARIABLE_IEs) VarIE;
	pVIE->Length = 0;
    RTMPZeroMemory(pHtCapability, sizeof(HT_CAPABILITY_IE));
	RTMPZeroMemory(pAddHtInfo, sizeof(ADD_HT_INFO_IE));

	EdcaParm.bValid = FALSE;
	if (PeerBeaconAndProbeRspSanity(pAd, 
								Elem->Msg, 
								Elem->MsgLen, 
								Elem->Channel,
								Addr2, 
								Bssid, 
								(PCHAR)Ssid, 
								&SsidLen, 
								&BssType, 
								&BeaconPeriod, 
								&Channel, 
								&NewChannel,
								&TimeStamp, 
								&Cf, 
								&AtimWin, 
								&CapabilityInfo, 
								&Erp,
								&DtimCount, 
								&DtimPeriod, 
								&BcastFlag, 
								&MessageToMe, 
								SupRate,
								&SupRateLen,
								ExtRate,
								&ExtRateLen,
								&CkipFlag,
								&AironetCellPowerLimit,
								&EdcaParm,
								&QbssLoad,
								&QosCapability,
								&RalinkIe,
								&HtCapabilityLen,
								&PreNHtCapabilityLen,
								pHtCapability,
								&ExtCapInfo,
								&AddHtInfoLen,
								pAddHtInfo,
								&NewExtChannelOffset,
								&LenVIE,
								pVIE)) 
	{
		/* Disqualify 11b only adhoc when we are in 11g only adhoc mode */
		if ((BssType == BSS_ADHOC) && (pAd->CommonCfg.PhyMode == PHY_11G) && ((SupRateLen+ExtRateLen)< 12))
			goto LabelOK;

		/*
		    BEACON from desired BSS/IBSS found. We should be able to decide most
		    BSS parameters here.
		    Q. But what happen if this JOIN doesn't conclude a successful ASSOCIATEION?
		        Do we need to receover back all parameters belonging to previous BSS?
		    A. Should be not. There's no back-door recover to previous AP. It still need
		        a new JOIN-AUTH-ASSOC sequence.
		*/
		if (MAC_ADDR_EQUAL(pAd->MlmeAux.Bssid, Bssid)) 
		{
			DBGPRINT(RT_DEBUG_TRACE, ("SYNC - receive desired BEACON at JoinWaitBeacon... Channel = %d\n", Channel));
			RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);

			/* Update RSSI to prevent No signal display when cards first initialized */
			pAd->StaCfg.RssiSample.LastRssi0	= ConvertToRssi(pAd, Elem->Rssi0, RSSI_0);
			pAd->StaCfg.RssiSample.LastRssi1	= ConvertToRssi(pAd, Elem->Rssi1, RSSI_1);
			pAd->StaCfg.RssiSample.LastRssi2	= ConvertToRssi(pAd, Elem->Rssi2, RSSI_2);
			pAd->StaCfg.RssiSample.AvgRssi0	= pAd->StaCfg.RssiSample.LastRssi0;
			pAd->StaCfg.RssiSample.AvgRssi0X8	= pAd->StaCfg.RssiSample.AvgRssi0 << 3;
			pAd->StaCfg.RssiSample.AvgRssi1	= pAd->StaCfg.RssiSample.LastRssi1;
			pAd->StaCfg.RssiSample.AvgRssi1X8	= pAd->StaCfg.RssiSample.AvgRssi1 << 3;
			pAd->StaCfg.RssiSample.AvgRssi2	= pAd->StaCfg.RssiSample.LastRssi2;
			pAd->StaCfg.RssiSample.AvgRssi2X8	= pAd->StaCfg.RssiSample.AvgRssi2 << 3;

			/*
			  We need to check if SSID only set to any, then we can record the current SSID.
			  Otherwise will cause hidden SSID association failed. 
			*/
			if (pAd->MlmeAux.SsidLen == 0)
			{
				NdisMoveMemory(pAd->MlmeAux.Ssid, Ssid, SsidLen);
				pAd->MlmeAux.SsidLen = SsidLen;
			}
			else
			{
				Idx = BssSsidTableSearch(&pAd->ScanTab, Bssid, pAd->MlmeAux.Ssid, pAd->MlmeAux.SsidLen, Channel);

				if (Idx == BSS_NOT_FOUND)				
				{
					Rssi = RTMPMaxRssi(pAd, ConvertToRssi(pAd, Elem->Rssi0, RSSI_0), ConvertToRssi(pAd, Elem->Rssi1, RSSI_1), ConvertToRssi(pAd, Elem->Rssi2, RSSI_2));
					Idx = BssTableSetEntry(pAd, &pAd->ScanTab, Bssid, (CHAR *) Ssid, SsidLen, BssType, BeaconPeriod,
										&Cf, AtimWin, CapabilityInfo, SupRate, SupRateLen, ExtRate, ExtRateLen,  pHtCapability,
										pAddHtInfo, HtCapabilityLen, AddHtInfoLen, NewExtChannelOffset, Channel, Rssi, TimeStamp, CkipFlag,
										&EdcaParm, &QosCapability, &QbssLoad, LenVIE, pVIE);
					if (Idx != BSS_NOT_FOUND)
					{
						NdisMoveMemory(pAd->ScanTab.BssEntry[Idx].PTSF, &Elem->Msg[24], 4);
						NdisMoveMemory(&pAd->ScanTab.BssEntry[Idx].TTSF[0], &Elem->TimeStamp.u.LowPart, 4);
						NdisMoveMemory(&pAd->ScanTab.BssEntry[Idx].TTSF[4], &Elem->TimeStamp.u.LowPart, 4);
						CapabilityInfo = pAd->ScanTab.BssEntry[Idx].CapabilityInfo;

						pAd->ScanTab.BssEntry[Idx].MinSNR = Elem->Signal % 10;
						if (pAd->ScanTab.BssEntry[Idx].MinSNR == 0)
							pAd->ScanTab.BssEntry[Idx].MinSNR = -5;

						NdisMoveMemory(pAd->ScanTab.BssEntry[Idx].MacAddr, Addr2, MAC_ADDR_LEN);
					}
				}
				else
				{
#ifdef WPA_SUPPLICANT_SUPPORT
					if (pAd->StaCfg.WpaSupplicantUP & WPA_SUPPLICANT_ENABLE_WPS) ;
					else
#endif /* WPA_SUPPLICANT_SUPPORT */
					{

						/*
						    Check if AP privacy is different Staion, if yes, 
						    start a new scan and ignore the frame 
						    (often happen during AP change privacy at short time)
						*/
						if ((((pAd->StaCfg.WepStatus != Ndis802_11WEPDisabled) << 4) ^
							CapabilityInfo) &
							0x0010)
						{	
							MLME_SCAN_REQ_STRUCT ScanReq;
							DBGPRINT(RT_DEBUG_TRACE, ("%s:AP privacy %d is differenct from STA privacy%d\n", __FUNCTION__, (CapabilityInfo & 0x0010) >> 4 ,pAd->StaCfg.WepStatus != Ndis802_11WEPDisabled));
							ScanParmFill(pAd, &ScanReq, (PSTRING) pAd->MlmeAux.Ssid, pAd->MlmeAux.SsidLen, BSS_ANY, SCAN_ACTIVE);
							MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_SCAN_REQ, sizeof(MLME_SCAN_REQ_STRUCT), &ScanReq, 0);
							pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_LIST_SCAN;
							NdisGetSystemUpTime(&pAd->StaCfg.LastScanTime);
							goto LabelOK;
						}
					}

					/*
					    Multiple SSID case, used correct CapabilityInfo
					*/
					CapabilityInfo = pAd->ScanTab.BssEntry[Idx].CapabilityInfo;
				}
			}
			/*NdisMoveMemory(pAd->MlmeAux.Bssid, Bssid, MAC_ADDR_LEN);*/
			pAd->MlmeAux.CapabilityInfo = CapabilityInfo & SUPPORTED_CAPABILITY_INFO;
			pAd->MlmeAux.BssType = BssType;
			pAd->MlmeAux.BeaconPeriod = BeaconPeriod;

			/*
				Some AP may carrys wrong beacon interval (ex. 0) in Beacon IE.
				We need to check here for preventing divided by 0 error.
			*/
			if (pAd->MlmeAux.BeaconPeriod == 0)
				pAd->MlmeAux.BeaconPeriod = 100;
			
			pAd->MlmeAux.Channel = Channel;
			pAd->MlmeAux.AtimWin = AtimWin;
			pAd->MlmeAux.CfpPeriod = Cf.CfpPeriod;
			pAd->MlmeAux.CfpMaxDuration = Cf.CfpMaxDuration;
			pAd->MlmeAux.APRalinkIe = RalinkIe;

			/* 
			    Copy AP's supported rate to MlmeAux for creating assoication request
			    Also filter out not supported rate
			*/
			pAd->MlmeAux.SupRateLen = SupRateLen;
			NdisMoveMemory(pAd->MlmeAux.SupRate, SupRate, SupRateLen);
			RTMPCheckRates(pAd, pAd->MlmeAux.SupRate, &pAd->MlmeAux.SupRateLen);
			pAd->MlmeAux.ExtRateLen = ExtRateLen;
			NdisMoveMemory(pAd->MlmeAux.ExtRate, ExtRate, ExtRateLen);
			RTMPCheckRates(pAd, pAd->MlmeAux.ExtRate, &pAd->MlmeAux.ExtRateLen);

            NdisZeroMemory(pAd->StaActive.SupportedPhyInfo.MCSSet, 16);


			/*  Get the ext capability info element */
			NdisMoveMemory(&pAd->MlmeAux.ExtCapInfo, &ExtCapInfo,sizeof(ExtCapInfo));

#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
			DBGPRINT(RT_DEBUG_TRACE, ("MlmeAux.ExtCapInfo=%d\n", pAd->MlmeAux.ExtCapInfo.BssCoexistMgmtSupport));
			if (pAd->CommonCfg.bBssCoexEnable == TRUE)
				pAd->CommonCfg.ExtCapIE.BssCoexistMgmtSupport = 1;
#endif /* DOT11N_DRAFT3 */

			if (((pAd->StaCfg.WepStatus != Ndis802_11WEPEnabled) && (pAd->StaCfg.WepStatus != Ndis802_11Encryption2Enabled))
				|| (pAd->CommonCfg.HT_DisallowTKIP == FALSE))
			{
				if ((pAd->StaCfg.BssType == BSS_INFRA) || 
					((pAd->StaCfg.BssType == BSS_ADHOC) && (pAd->StaCfg.bAdhocN == TRUE)))
				bAllowNrate = TRUE;			
			}
			
			pAd->MlmeAux.NewExtChannelOffset = NewExtChannelOffset;
			pAd->MlmeAux.HtCapabilityLen = HtCapabilityLen;

			RTMPZeroMemory(&pAd->MlmeAux.HtCapability, SIZE_HT_CAP_IE);
			/* filter out un-supported ht rates */
			if (((HtCapabilityLen > 0) || (PreNHtCapabilityLen > 0)) && 
				(pAd->StaCfg.DesiredHtPhyInfo.bHtEnable) &&
				((pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED) && bAllowNrate))
			{
   				RTMPMoveMemory(&pAd->MlmeAux.AddHtInfo, pAddHtInfo, SIZE_ADD_HT_INFO_IE);
				
                		/* StaActive.SupportedHtPhy.MCSSet stores Peer AP's 11n Rx capability */
				NdisMoveMemory(pAd->StaActive.SupportedPhyInfo.MCSSet, pHtCapability->MCSSet, 16);
				pAd->MlmeAux.NewExtChannelOffset = NewExtChannelOffset;
				pAd->MlmeAux.HtCapabilityLen = SIZE_HT_CAP_IE;
				pAd->StaActive.SupportedPhyInfo.bHtEnable = TRUE;
				if (PreNHtCapabilityLen > 0)
					pAd->StaActive.SupportedPhyInfo.bPreNHt = TRUE;
				RTMPCheckHt(pAd, BSSID_WCID, pHtCapability, pAddHtInfo);
				/* Copy AP Parameter to StaActive.  This is also in LinkUp. */
				DBGPRINT(RT_DEBUG_TRACE, ("PeerBeaconAtJoinAction! (MpduDensity=%d, MaxRAmpduFactor=%d, BW=%d)\n", 
					pAd->StaActive.SupportedHtPhy.MpduDensity, pAd->StaActive.SupportedHtPhy.MaxRAmpduFactor, pHtCapability->HtCapInfo.ChannelWidth));
				
				if (AddHtInfoLen > 0)
				{
					CentralChannel = pAddHtInfo->ControlChan;
		 			/* Check again the Bandwidth capability of this AP. */
		 			if ((pAddHtInfo->ControlChan > 2)&& (pAddHtInfo->AddHtInfo.ExtChanOffset == EXTCHA_BELOW) && (pHtCapability->HtCapInfo.ChannelWidth == BW_40))
		 			{
		 				CentralChannel = pAddHtInfo->ControlChan - 2;
		 			}
		 			else if ((pAddHtInfo->AddHtInfo.ExtChanOffset == EXTCHA_ABOVE) && (pHtCapability->HtCapInfo.ChannelWidth == BW_40))
		 			{
		 				CentralChannel = pAddHtInfo->ControlChan + 2;
		 			}
                    
                    /* Check Error . */
					if (pAd->MlmeAux.CentralChannel != CentralChannel)
		 				DBGPRINT(RT_DEBUG_ERROR, ("PeerBeaconAtJoinAction HT===>Beacon Central Channel = %d, Control Channel = %d. Mlmeaux CentralChannel = %d\n", CentralChannel, pAddHtInfo->ControlChan, pAd->MlmeAux.CentralChannel));

		 			DBGPRINT(RT_DEBUG_TRACE, ("PeerBeaconAtJoinAction HT===>Central Channel = %d, Control Channel = %d,  .\n", CentralChannel, pAddHtInfo->ControlChan));

				}
				
			}
			else
#endif /* DOT11_N_SUPPORT */
			{
   				/* To prevent error, let legacy AP must have same CentralChannel and Channel. */
				if ((HtCapabilityLen == 0) && (PreNHtCapabilityLen == 0))
					pAd->MlmeAux.CentralChannel = pAd->MlmeAux.Channel;

				pAd->StaActive.SupportedPhyInfo.bHtEnable = FALSE;
				pAd->MlmeAux.NewExtChannelOffset = 0xff;
				RTMPZeroMemory(&pAd->MlmeAux.HtCapability, SIZE_HT_CAP_IE);
				pAd->MlmeAux.HtCapabilityLen = 0;
				RTMPZeroMemory(&pAd->MlmeAux.AddHtInfo, SIZE_ADD_HT_INFO_IE);
			}

			RTMPUpdateMlmeRate(pAd);
	
			/* copy QOS related information */
			if ((pAd->CommonCfg.bWmmCapable)
#ifdef DOT11_N_SUPPORT
				 || (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED)
#endif /* DOT11_N_SUPPORT */
				)
			{
				NdisMoveMemory(&pAd->MlmeAux.APEdcaParm, &EdcaParm, sizeof(EDCA_PARM));
				NdisMoveMemory(&pAd->MlmeAux.APQbssLoad, &QbssLoad, sizeof(QBSS_LOAD_PARM));
				NdisMoveMemory(&pAd->MlmeAux.APQosCapability, &QosCapability, sizeof(QOS_CAPABILITY_PARM));
			}
			else
			{
				NdisZeroMemory(&pAd->MlmeAux.APEdcaParm, sizeof(EDCA_PARM));
				NdisZeroMemory(&pAd->MlmeAux.APQbssLoad, sizeof(QBSS_LOAD_PARM));
				NdisZeroMemory(&pAd->MlmeAux.APQosCapability, sizeof(QOS_CAPABILITY_PARM));
			}
			
			DBGPRINT(RT_DEBUG_TRACE, ("SYNC - after JOIN, SupRateLen=%d, ExtRateLen=%d\n", 
				pAd->MlmeAux.SupRateLen, pAd->MlmeAux.ExtRateLen));

			if (AironetCellPowerLimit != 0xFF)
			{
				/* We need to change our TxPower for CCX 2.0 AP Control of Client Transmit Power */
				ChangeToCellPowerLimit(pAd, AironetCellPowerLimit);
			}
			else  /* Used the default TX Power Percentage. */
				pAd->CommonCfg.TxPowerPercentage = pAd->CommonCfg.TxPowerDefault;

			if (pAd->StaCfg.BssType == BSS_INFRA)
			{
				/*
					Ad-hoc call this function in LinkUp
				*/
				InitChannelRelatedValue(pAd);
				{
					AsicSetBssid(pAd, pAd->MlmeAux.Bssid);
				}
				/* Add BSSID to WCID search table */
				AsicUpdateRxWCIDTable(pAd, BSSID_WCID, pAd->MlmeAux.Bssid);	
			}

			pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
			Status = MLME_SUCCESS;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_JOIN_CONF, 2, &Status, 0);

#ifdef LINUX
#ifdef RT_CFG80211_SUPPORT
			RT_CFG80211_SCANNING_INFORM(pAd, Idx, Elem->Channel, Elem->Msg,
										Elem->MsgLen, Rssi);
#endif /* RT_CFG80211_SUPPORT */
#endif /* LINUX */
		}
		/* not to me BEACON, ignored */
	} 
	/* sanity check fail, ignore this frame */

	goto LabelOK;

LabelErr:
	DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));

LabelOK:
	if (Ssid != NULL)
		os_free_mem(NULL, Ssid);
	if (VarIE != NULL)
		os_free_mem(NULL, VarIE);
	if (pHtCapability != NULL)
		os_free_mem(NULL, pHtCapability);
	if (pAddHtInfo != NULL)
		os_free_mem(NULL, pAddHtInfo);
	return;
}

/* 
	==========================================================================
	Description:
		receive BEACON from peer

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID PeerBeacon(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	UCHAR         Bssid[MAC_ADDR_LEN], Addr2[MAC_ADDR_LEN];
	CHAR          *Ssid = NULL;
	CF_PARM       CfParm;
	UCHAR         SsidLen=0, MessageToMe=0, BssType, Channel, NewChannel, index=0;
	UCHAR         DtimCount=0, DtimPeriod=0, BcastFlag=0;
	USHORT        CapabilityInfo, AtimWin, BeaconPeriod;
	LARGE_INTEGER TimeStamp;
	USHORT        TbttNumToNextWakeUp;
	UCHAR         Erp;
	UCHAR         SupRate[MAX_LEN_OF_SUPPORTED_RATES], ExtRate[MAX_LEN_OF_SUPPORTED_RATES];
	UCHAR		  SupRateLen, ExtRateLen;
	UCHAR		  CkipFlag;
	USHORT        LenVIE;
	UCHAR		  AironetCellPowerLimit;
	EDCA_PARM       *pEdcaParm = NULL;
	QBSS_LOAD_PARM  QbssLoad;
	QOS_CAPABILITY_PARM QosCapability;
	ULONG           RalinkIe;
	UCHAR						*VarIE = NULL;		/* Total VIE length = MAX_VIE_LEN - -5 */
	NDIS_802_11_VARIABLE_IEs	*pVIE = NULL;
	HT_CAPABILITY_IE		*pHtCapability = NULL;
	ADD_HT_INFO_IE		*pAddHtInfo = NULL;	/* AP might use this additional ht info IE */
	UCHAR			HtCapabilityLen, PreNHtCapabilityLen;
	UCHAR			AddHtInfoLen;
	UCHAR			NewExtChannelOffset = 0xff;
	EXT_CAP_INFO_ELEMENT	ExtCapInfo;


#ifdef RALINK_ATE
    if (ATE_ON(pAd))
    {
		return;
    }
#endif /* RALINK_ATE */

	if (!(INFRA_ON(pAd) || ADHOC_ON(pAd)
		))
		return;

	/* allocate memory */
	os_alloc_mem(NULL, (UCHAR **)&Ssid, MAX_LEN_OF_SSID);
	if (Ssid == NULL)
		goto LabelErr;
	os_alloc_mem(NULL, (UCHAR **)&pEdcaParm, sizeof(EDCA_PARM));
	if (pEdcaParm == NULL)
		goto LabelErr;
	os_alloc_mem(NULL, (UCHAR **)&VarIE, MAX_VIE_LEN);
	if (VarIE == NULL)
		goto LabelErr;
	os_alloc_mem(NULL, (UCHAR **)&pHtCapability, sizeof(HT_CAPABILITY_IE));
	if (pHtCapability == NULL)
		goto LabelErr;
	os_alloc_mem(NULL, (UCHAR **)&pAddHtInfo, sizeof(ADD_HT_INFO_IE));
	if (pAddHtInfo == NULL)
		goto LabelErr;

	/* Init Variable IE structure */
	pVIE = (PNDIS_802_11_VARIABLE_IEs) VarIE;
	pVIE->Length = 0;
    RTMPZeroMemory(pHtCapability, sizeof(HT_CAPABILITY_IE));
	RTMPZeroMemory(pAddHtInfo, sizeof(ADD_HT_INFO_IE));
	RTMPZeroMemory(&ExtCapInfo, sizeof(ExtCapInfo));

	if (PeerBeaconAndProbeRspSanity(pAd, 
								Elem->Msg, 
								Elem->MsgLen, 
								Elem->Channel,
								Addr2, 
								Bssid, 
								Ssid, 
								&SsidLen, 
								&BssType, 
								&BeaconPeriod, 
								&Channel, 
								&NewChannel,
								&TimeStamp, 
								&CfParm, 
								&AtimWin, 
								&CapabilityInfo, 
								&Erp,
								&DtimCount, 
								&DtimPeriod, 
								&BcastFlag, 
								&MessageToMe, 
								SupRate,
								&SupRateLen,
								ExtRate,
								&ExtRateLen,
								&CkipFlag,
								&AironetCellPowerLimit,
								pEdcaParm,
								&QbssLoad,
								&QosCapability,
								&RalinkIe,
								&HtCapabilityLen,
#ifdef CONFIG_STA_SUPPORT
								&PreNHtCapabilityLen,
#endif /* CONFIG_STA_SUPPORT */
								pHtCapability,
								&ExtCapInfo,
								&AddHtInfoLen,
								pAddHtInfo,
								&NewExtChannelOffset,
								&LenVIE,
								pVIE)) 
	{
		BOOLEAN is_my_bssid, is_my_ssid;
		ULONG   Bssidx, Now;
		BSS_ENTRY *pBss;
		CHAR		RealRssi = RTMPMaxRssi(pAd, ConvertToRssi(pAd, Elem->Rssi0, RSSI_0), ConvertToRssi(pAd, Elem->Rssi1, RSSI_1), ConvertToRssi(pAd, Elem->Rssi2, RSSI_2));

		is_my_bssid = MAC_ADDR_EQUAL(Bssid, pAd->CommonCfg.Bssid)? TRUE : FALSE;
		is_my_ssid = SSID_EQUAL(Ssid, SsidLen, pAd->CommonCfg.Ssid, pAd->CommonCfg.SsidLen)? TRUE:FALSE;


		/* ignore BEACON not for my SSID */
		if ((!is_my_ssid) && (!is_my_bssid))
			goto LabelOK;

		/* It means STA waits disassoc completely from this AP, ignores this beacon. */
		if (pAd->Mlme.CntlMachine.CurrState == CNTL_WAIT_DISASSOC)
			goto LabelOK;
		
#ifdef DOT11_N_SUPPORT
		/* Copy Control channel for this BSSID. */	
		if (AddHtInfoLen != 0)
			Channel = pAddHtInfo->ControlChan;

		if ((HtCapabilityLen > 0) || (PreNHtCapabilityLen > 0))
			HtCapabilityLen = SIZE_HT_CAP_IE;
#endif /* DOT11_N_SUPPORT */

		/*
		   Housekeeping "SsidBssTab" table for later-on ROAMing usage. 
		*/
		Bssidx = BssTableSearchWithSSID(&pAd->MlmeAux.SsidBssTab, Bssid, Ssid, SsidLen, Channel);
		if (Bssidx == BSS_NOT_FOUND)
		{			
			/* discover new AP of this network, create BSS entry */
			Bssidx = BssTableSetEntry(pAd, &pAd->MlmeAux.SsidBssTab, Bssid, Ssid, SsidLen, BssType, BeaconPeriod,
						 &CfParm, AtimWin, CapabilityInfo, SupRate, SupRateLen, ExtRate, ExtRateLen, 
						pHtCapability, pAddHtInfo, HtCapabilityLen,AddHtInfoLen,NewExtChannelOffset, Channel, 
						RealRssi, TimeStamp, CkipFlag, pEdcaParm, &QosCapability, 
						&QbssLoad, LenVIE, pVIE);
			if (Bssidx == BSS_NOT_FOUND)
				;
			else
			{
				PBSS_ENTRY	pBssEntry = &pAd->MlmeAux.SsidBssTab.BssEntry[Bssidx];
				NdisMoveMemory(&pBssEntry->PTSF[0], &Elem->Msg[24], 4);
				NdisMoveMemory(&pBssEntry->TTSF[0], &Elem->TimeStamp.u.LowPart, 4);
				NdisMoveMemory(&pBssEntry->TTSF[4], &Elem->TimeStamp.u.LowPart, 4);
				pBssEntry->Rssi = RealRssi;

				NdisMoveMemory(pBssEntry->MacAddr, Addr2, MAC_ADDR_LEN);
				

			}
		}			
		
		/*
			Update ScanTab
		*/
		Bssidx = BssTableSearch(&pAd->ScanTab, Bssid, Channel);
		if (Bssidx == BSS_NOT_FOUND)
		{
			/* discover new AP of this network, create BSS entry */
			Bssidx = BssTableSetEntry(pAd, &pAd->ScanTab, Bssid, Ssid, SsidLen, BssType, BeaconPeriod,
						 &CfParm, AtimWin, CapabilityInfo, SupRate, SupRateLen, ExtRate, ExtRateLen, 
						pHtCapability, pAddHtInfo, HtCapabilityLen,AddHtInfoLen,NewExtChannelOffset, Channel, 
						RealRssi, TimeStamp, CkipFlag, pEdcaParm, &QosCapability, 
						&QbssLoad, LenVIE, pVIE);
			if (Bssidx == BSS_NOT_FOUND) /* return if BSS table full */
				goto LabelOK;  
			
			NdisMoveMemory(pAd->ScanTab.BssEntry[Bssidx].PTSF, &Elem->Msg[24], 4);
			NdisMoveMemory(&pAd->ScanTab.BssEntry[Bssidx].TTSF[0], &Elem->TimeStamp.u.LowPart, 4);
			NdisMoveMemory(&pAd->ScanTab.BssEntry[Bssidx].TTSF[4], &Elem->TimeStamp.u.LowPart, 4);
			pAd->ScanTab.BssEntry[Bssidx].MinSNR = Elem->Signal % 10;
			if (pAd->ScanTab.BssEntry[Bssidx].MinSNR == 0)
				pAd->ScanTab.BssEntry[Bssidx].MinSNR = -5;
			
			NdisMoveMemory(pAd->ScanTab.BssEntry[Bssidx].MacAddr, Addr2, MAC_ADDR_LEN);
			
			
			
		}

		/* 
		    if the ssid matched & bssid unmatched, we should select the bssid with large value.
		    This might happened when two STA start at the same time
		*/
		if ((! is_my_bssid) && ADHOC_ON(pAd))
		{
			INT	i;
			/* Add the safeguard against the mismatch of adhoc wep status */
			if ((pAd->StaCfg.WepStatus != pAd->ScanTab.BssEntry[Bssidx].WepStatus) ||
				(pAd->StaCfg.AuthMode != pAd->ScanTab.BssEntry[Bssidx].AuthMode))
			{
				goto LabelOK;
			}
			/* collapse into the ADHOC network which has bigger BSSID value. */
			for (i = 0; i < 6; i++)
			{
				if (Bssid[i] > pAd->CommonCfg.Bssid[i])
				{
					DBGPRINT(RT_DEBUG_TRACE, ("SYNC - merge to the IBSS with bigger BSSID=%02x:%02x:%02x:%02x:%02x:%02x\n", 
						Bssid[0], Bssid[1], Bssid[2], Bssid[3], Bssid[4], Bssid[5]));
					AsicDisableSync(pAd);
					COPY_MAC_ADDR(pAd->CommonCfg.Bssid, Bssid);
					AsicSetBssid(pAd, pAd->CommonCfg.Bssid); 
					MakeIbssBeacon(pAd);        /* re-build BEACON frame */
					AsicEnableIbssSync(pAd);    /* copy BEACON frame to on-chip memory */
					is_my_bssid = TRUE;
					break;
				}
				else if (Bssid[i] < pAd->CommonCfg.Bssid[i])
					break;
			}
		}


		NdisGetSystemUpTime(&Now);
		pBss = &pAd->ScanTab.BssEntry[Bssidx];
		pBss->Rssi = RealRssi;       /* lastest RSSI */
		pBss->LastBeaconRxTime = Now;   /* last RX timestamp */

		/*
		   BEACON from my BSSID - either IBSS or INFRA network
		*/ 
		if (is_my_bssid)
		{
			RXWI_STRUC	RxWI;

#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
			OVERLAP_BSS_SCAN_IE	BssScan;
			UCHAR					RegClass;
			BOOLEAN					brc;

			/* Read Beacon's Reg Class IE if any. */
			brc = PeerBeaconAndProbeRspSanity2(pAd, Elem->Msg, Elem->MsgLen, &BssScan, &RegClass);
			if (brc == TRUE)
			{
				UpdateBssScanParm(pAd, BssScan);
				pAd->StaCfg.RegClass = RegClass;
			}
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */

			pAd->StaCfg.DtimCount = DtimCount;
			pAd->StaCfg.DtimPeriod = DtimPeriod;
			pAd->StaCfg.LastBeaconRxTime = Now;


			NdisZeroMemory(&RxWI, sizeof(RXWI_STRUC));
			RxWI.RSSI0 = Elem->Rssi0;
			RxWI.RSSI1 = Elem->Rssi1;
			RxWI.RSSI2 = Elem->Rssi2;
			RxWI.PHYMODE = 0; /* Prevent SNR calculate error. */
			
			Update_Rssi_Sample(pAd, &pAd->StaCfg.RssiSample, &RxWI);

			if ((pAd->CommonCfg.bIEEE80211H == 1) && (NewChannel != 0) && (Channel != NewChannel))
			{
				/* Switching to channel 1 can prevent from rescanning the current channel immediately (by auto reconnection). */
				/* In addition, clear the MLME queue and the scan table to discard the RX packets and previous scanning results. */
				AsicSwitchChannel(pAd, 1, FALSE);
				AsicLockChannel(pAd, 1);
			    LinkDown(pAd, FALSE);
				MlmeQueueInit(pAd, &pAd->Mlme.Queue);
				BssTableInit(&pAd->ScanTab);
			    RTMPusecDelay(1000000);		/* use delay to prevent STA do reassoc */
						
				/* channel sanity check */
				for (index = 0 ; index < pAd->ChannelListNum; index++)
				{
					if (pAd->ChannelList[index].Channel == NewChannel)
					{
						pAd->ScanTab.BssEntry[Bssidx].Channel = NewChannel;
						pAd->CommonCfg.Channel = NewChannel;
						AsicSwitchChannel(pAd, pAd->CommonCfg.Channel, FALSE);
						AsicLockChannel(pAd, pAd->CommonCfg.Channel);
						DBGPRINT(RT_DEBUG_TRACE, ("PeerBeacon - STA receive channel switch announcement IE (New Channel =%d)\n", NewChannel));
						break;
					}
				}

				if (index >= pAd->ChannelListNum)
				{
					DBGPRINT_ERR(("PeerBeacon(can not find New Channel=%d in ChannelList[%d]\n", pAd->CommonCfg.Channel, pAd->ChannelListNum));
				}
			}

#ifdef WPA_SUPPLICANT_SUPPORT
			if (pAd->StaCfg.WpaSupplicantUP & WPA_SUPPLICANT_ENABLE_WPS) ;
			else
#endif /* WPA_SUPPLICANT_SUPPORT */
			{
				if ((((pAd->StaCfg.WepStatus != Ndis802_11WEPDisabled) << 4) ^
					CapabilityInfo) &
					0x0010)
				{
					/*
						To prevent STA connect to OPEN/WEP AP when STA is OPEN/NONE or 
						STA connect to OPEN/NONE AP when STA is OPEN/WEP AP.
					*/
					DBGPRINT(RT_DEBUG_TRACE, ("%s:AP privacy:%x is differenct from STA privacy:%x\n", __FUNCTION__, (CapabilityInfo & 0x0010) >> 4 , pAd->StaCfg.WepStatus != Ndis802_11WEPDisabled));
					if (INFRA_ON(pAd))
					{
						LinkDown(pAd,FALSE);
						BssTableInit(&pAd->ScanTab);	
					}
					goto LabelOK;
				}
			}

#ifdef LINUX
#ifdef RT_CFG80211_SUPPORT
/*			CFG80211_BeaconCountryRegionParse(pAd, pVIE, LenVIE); */
#endif /* RT_CFG80211_SUPPORT */
#endif /* LINUX */

			if (AironetCellPowerLimit != 0xFF)
			{
				/*
				   We get the Cisco (ccx) "TxPower Limit" required
				   Changed to appropriate TxPower Limit for Ciso Compatible Extensions
				*/
				ChangeToCellPowerLimit(pAd, AironetCellPowerLimit);
			}
			else
			{
				/*
				   AironetCellPowerLimit equal to 0xFF means the Cisco (ccx) "TxPower Limit" not exist.
				   Used the default TX Power Percentage, that set from UI.	
				*/
				pAd->CommonCfg.TxPowerPercentage = pAd->CommonCfg.TxPowerDefault;	
			}

			if (ADHOC_ON(pAd) && (CAP_IS_IBSS_ON(CapabilityInfo)))   
			{
				UCHAR			MaxSupportedRateIn500Kbps = 0;
				UCHAR			idx;
				MAC_TABLE_ENTRY *pEntry;
	
				/* supported rates array may not be sorted. sort it and find the maximum rate */
			    for (idx=0; idx<SupRateLen; idx++)
				{
			        if (MaxSupportedRateIn500Kbps < (SupRate[idx] & 0x7f)) 
			            MaxSupportedRateIn500Kbps = SupRate[idx] & 0x7f;
						}
				
				for (idx=0; idx<ExtRateLen; idx++)
			    {
			        if (MaxSupportedRateIn500Kbps < (ExtRate[idx] & 0x7f)) 
			            MaxSupportedRateIn500Kbps = ExtRate[idx] & 0x7f;
					}
						
				/* look up the existing table */
				pEntry = MacTableLookup(pAd, Addr2);

				/*
				   Ad-hoc mode is using MAC address as BA session. So we need to continuously find newly joined adhoc station by receiving beacon.
				   To prevent always check this, we use wcid == RESERVED_WCID to recognize it as newly joined adhoc station.
				*/
				if ((ADHOC_ON(pAd) && ((!pEntry) || (pEntry && IS_ENTRY_NONE(pEntry)))) ||
					(pEntry && RTMP_TIME_AFTER(Now, pEntry->LastBeaconRxTime + ADHOC_ENTRY_BEACON_LOST_TIME)))
				{
					if (pEntry == NULL)
						/* Another adhoc joining, add to our MAC table. */
						pEntry = MacTableInsertEntry(pAd, Addr2, BSS0, OPMODE_STA, FALSE);

					if (pEntry == NULL)
						goto LabelOK;

					if (StaAddMacTableEntry(pAd, 
											pEntry, 
											MaxSupportedRateIn500Kbps, 
											pHtCapability, 
											HtCapabilityLen, 
											pAddHtInfo,
											AddHtInfoLen,
											CapabilityInfo) == FALSE)
					{
						DBGPRINT(RT_DEBUG_TRACE, ("ADHOC - Add Entry failed.\n"));
						goto LabelOK;
					}
					pEntry->LastBeaconRxTime = 0;


					if (pEntry &&
						(Elem->Wcid == RESERVED_WCID))
					{
						idx = pAd->StaCfg.DefaultKeyId;
							RTMP_SET_WCID_SEC_INFO(pAd, BSS0, idx, 
												   pAd->SharedKey[BSS0][idx].CipherAlg,
												   pEntry->Aid,
												   SHAREDKEYTABLE);
					}
				}

				if (pEntry && IS_ENTRY_CLIENT(pEntry))
					pEntry->LastBeaconRxTime = Now;

				/* At least another peer in this IBSS, declare MediaState as CONNECTED */
				if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
				{
					OPSTATUS_SET_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED); 
					RTMP_IndicateMediaState(pAd, NdisMediaStateConnected);
	                pAd->ExtraInfo = GENERAL_LINK_UP;
					DBGPRINT(RT_DEBUG_TRACE, ("ADHOC  fOP_STATUS_MEDIA_STATE_CONNECTED.\n"));
				}	
			}

			if (INFRA_ON(pAd))
			{
				BOOLEAN bUseShortSlot, bUseBGProtection;

				/*
				   decide to use/change to - 
				      1. long slot (20 us) or short slot (9 us) time
				      2. turn on/off RTS/CTS and/or CTS-to-self protection
				      3. short preamble
				*/

					
				/* bUseShortSlot = pAd->CommonCfg.bUseShortSlotTime && CAP_IS_SHORT_SLOT(CapabilityInfo); */
				bUseShortSlot = CAP_IS_SHORT_SLOT(CapabilityInfo);
				if (bUseShortSlot != OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_SHORT_SLOT_INUSED))
					AsicSetSlotTime(pAd, bUseShortSlot);

				bUseBGProtection = (pAd->CommonCfg.UseBGProtection == 1) ||    /* always use */
								   ((pAd->CommonCfg.UseBGProtection == 0) && ERP_IS_USE_PROTECTION(Erp));

				if (pAd->CommonCfg.Channel > 14)  /* always no BG protection in A-band. falsely happened when switching A/G band to a dual-band AP */
					bUseBGProtection = FALSE;

				if (bUseBGProtection != OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED))
				{
					if (bUseBGProtection)
					{
						OPSTATUS_SET_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED);
						AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode, (OFDMSETPROTECT|CCKSETPROTECT|ALLN_SETPROTECT),FALSE,(pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent == 1));
					}
					else
					{
						OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED);
						AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode, (OFDMSETPROTECT|CCKSETPROTECT|ALLN_SETPROTECT),TRUE,(pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent == 1));
					}
					
					DBGPRINT(RT_DEBUG_WARN, ("SYNC - AP changed B/G protection to %d\n", bUseBGProtection));
				}
				
#ifdef DOT11_N_SUPPORT
				/* check Ht protection mode. and adhere to the Non-GF device indication by AP. */
				if ((AddHtInfoLen != 0) && 
					((pAddHtInfo->AddHtInfo2.OperaionMode != pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode) ||
					(pAddHtInfo->AddHtInfo2.NonGfPresent != pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent)))
				{
					pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent = pAddHtInfo->AddHtInfo2.NonGfPresent;
					pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode = pAddHtInfo->AddHtInfo2.OperaionMode;
					if (pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent == 1)
				{
						AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode, ALLN_SETPROTECT, FALSE, TRUE);
					}
					else
						AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode, ALLN_SETPROTECT, FALSE, FALSE);

					DBGPRINT(RT_DEBUG_TRACE, ("SYNC - AP changed N OperaionMode to %d\n", pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode));
				}
#endif /* DOT11_N_SUPPORT */
				
				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED) && 
					ERP_IS_USE_BARKER_PREAMBLE(Erp))
				{
					MlmeSetTxPreamble(pAd, Rt802_11PreambleLong);
					DBGPRINT(RT_DEBUG_TRACE, ("SYNC - AP forced to use LONG preamble\n"));
				}

				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED)    &&
					(pEdcaParm->bValid == TRUE)                          &&
					(pEdcaParm->EdcaUpdateCount != pAd->CommonCfg.APEdcaParm.EdcaUpdateCount))
				{
					DBGPRINT(RT_DEBUG_TRACE, ("SYNC - AP change EDCA parameters(from %d to %d)\n", 
						pAd->CommonCfg.APEdcaParm.EdcaUpdateCount,
						pEdcaParm->EdcaUpdateCount));
					AsicSetEdcaParm(pAd, pEdcaParm);
				}

				/* copy QOS related information */
				NdisMoveMemory(&pAd->CommonCfg.APQbssLoad, &QbssLoad, sizeof(QBSS_LOAD_PARM));
				NdisMoveMemory(&pAd->CommonCfg.APQosCapability, &QosCapability, sizeof(QOS_CAPABILITY_PARM));
#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
				/* 
				   2009: PF#1: 20/40 Coexistence in 2.4 GHz Band
				   When AP changes "STA Channel Width" and "Secondary Channel Offset" fields of HT Operation Element in the Beacon to 0
				*/
				if ((AddHtInfoLen != 0) && INFRA_ON(pAd))
				{
					BOOLEAN bChangeBW = FALSE;
					/*
					     1) HT Information
					     2) Secondary Channel Offset Element
					
					     40 -> 20 case
					*/
					if (pAd->CommonCfg.BBPCurrentBW == BW_40)
					{
						if (((pAddHtInfo->AddHtInfo.ExtChanOffset == EXTCHA_NONE) && (pAddHtInfo->AddHtInfo.RecomWidth == 0)) 
							||(NewExtChannelOffset==0x0)
						)
						{
							bChangeBW = TRUE;
							pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel;
							pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.BW = 0;
							DBGPRINT(RT_DEBUG_TRACE, ("FallBack from 40MHz to 20MHz(CtrlCh=%d, CentralCh=%d)\n", 
														pAd->CommonCfg.Channel, pAd->CommonCfg.CentralChannel));
							CntlChannelWidth(pAd, pAd->CommonCfg.Channel, pAd->CommonCfg.CentralChannel, BW_20, 0);
						}
					}
					/*
					    20 -> 40 case
					    1.) Supported Channel Width Set Field of the HT Capabilities element of both STAs is set to a non-zero
					    2.) Secondary Channel Offset field is SCA or SCB
					    3.) 40MHzRegulatoryClass is TRUE (not implement it)
					*/
					else if (((pAd->CommonCfg.BBPCurrentBW == BW_20) ||(NewExtChannelOffset!=0x0)) &&
							(pAd->CommonCfg.DesiredHtPhy.ChannelWidth != BW_20)
						)
					{
						if ((pAddHtInfo->AddHtInfo.ExtChanOffset != EXTCHA_NONE) && (HtCapabilityLen>0) && (pHtCapability->HtCapInfo.ChannelWidth == 1))
						{
							if ((pAddHtInfo->ControlChan > 2)&& (pAddHtInfo->AddHtInfo.ExtChanOffset == EXTCHA_BELOW))
							{
								pAd->CommonCfg.CentralChannel = pAddHtInfo->ControlChan - 2;
								bChangeBW = TRUE;
							}
							else if ((pAddHtInfo->AddHtInfo.ExtChanOffset == EXTCHA_ABOVE))
							{
								pAd->CommonCfg.CentralChannel = pAddHtInfo->ControlChan + 2;
								bChangeBW = TRUE;
							}
							
							if (bChangeBW)
							{
								pAd->CommonCfg.Channel = pAddHtInfo->ControlChan;
								DBGPRINT(RT_DEBUG_TRACE, ("FallBack from 20MHz to 40MHz(CtrlCh=%d, CentralCh=%d)\n", 
															pAd->CommonCfg.Channel, pAd->CommonCfg.CentralChannel));
								CntlChannelWidth(pAd, pAd->CommonCfg.Channel, pAd->CommonCfg.CentralChannel, BW_40, pAddHtInfo->AddHtInfo.ExtChanOffset);
								pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.BW = 1;
							}
						}
					}

					if (bChangeBW)
					{
						pAd->CommonCfg.BSSCoexist2040.word = 0;
						TriEventInit(pAd);
						BuildEffectedChannelList(pAd);
					}
				}
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */
			}

			/* only INFRASTRUCTURE mode support power-saving feature */
			if ((INFRA_ON(pAd) && (pAd->StaCfg.Psm == PWR_SAVE)) || (pAd->CommonCfg.bAPSDForcePowerSave))
			{
				UCHAR FreeNumber;
				/*
				     1. AP has backlogged unicast-to-me frame, stay AWAKE, send PSPOLL
				     2. AP has backlogged broadcast/multicast frame and we want those frames, stay AWAKE
				     3. we have outgoing frames in TxRing or MgmtRing, better stay AWAKE
				     4. Psm change to PWR_SAVE, but AP not been informed yet, we better stay AWAKE
				     5. otherwise, put PHY back to sleep to save battery.
				*/
				if (MessageToMe)
				{
#ifdef PCIE_PS_SUPPORT
					if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE))
					{
						/* Restore to correct BBP R3 value */
						if (pAd->Antenna.field.RxPath > 1)
						RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, pAd->StaCfg.BBPR3);
						/* Turn clk to 80Mhz. */
					}
#endif /* PCIE_PS_SUPPORT */
					if (pAd->CommonCfg.bAPSDCapable && pAd->CommonCfg.APEdcaParm.bAPSDCapable &&
						pAd->CommonCfg.bAPSDAC_BE && pAd->CommonCfg.bAPSDAC_BK && pAd->CommonCfg.bAPSDAC_VI && pAd->CommonCfg.bAPSDAC_VO)
					{
						pAd->CommonCfg.bNeedSendTriggerFrame = TRUE;
					}
					else
					{
						if (pAd->StaCfg.WindowsBatteryPowerMode == Ndis802_11PowerModeFast_PSP)
						{
							/* wake up and send a NULL frame with PM = 0 to the AP */
							RTMP_SET_PSM_BIT(pAd, PWR_ACTIVE);
							RTMPSendNullFrame(pAd, 
											  pAd->CommonCfg.TxRate, 
											  (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) ? TRUE:FALSE));
						}
						else
						{
							/* use PS-Poll to get any buffered packet */
							RTMP_PS_POLL_ENQUEUE(pAd);
						}
					}
				}
				else if (BcastFlag && (DtimCount == 0) && OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_RECEIVE_DTIM))
				{
#ifdef PCIE_PS_SUPPORT
					if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE))
					{
						if (pAd->Antenna.field.RxPath > 1)
						RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, pAd->StaCfg.BBPR3);
					}
#endif /* PCIE_PS_SUPPORT */
				} 
				else if ((pAd->TxSwQueue[QID_AC_BK].Number != 0)													||
						(pAd->TxSwQueue[QID_AC_BE].Number != 0)														||
						(pAd->TxSwQueue[QID_AC_VI].Number != 0)														||
						(pAd->TxSwQueue[QID_AC_VO].Number != 0)														||
						(RTMPFreeTXDRequest(pAd, QID_AC_BK, TX_RING_SIZE - 1, &FreeNumber) != NDIS_STATUS_SUCCESS)	||
						(RTMPFreeTXDRequest(pAd, QID_AC_BE, TX_RING_SIZE - 1, &FreeNumber) != NDIS_STATUS_SUCCESS)	||
						(RTMPFreeTXDRequest(pAd, QID_AC_VI, TX_RING_SIZE - 1, &FreeNumber) != NDIS_STATUS_SUCCESS)	||
						(RTMPFreeTXDRequest(pAd, QID_AC_VO, TX_RING_SIZE - 1, &FreeNumber) != NDIS_STATUS_SUCCESS)	||
						(RTMPFreeTXDRequest(pAd, QID_MGMT, MGMT_RING_SIZE - 1, &FreeNumber) != NDIS_STATUS_SUCCESS)) 
				{
					/* TODO: consider scheduled HCCA. might not be proper to use traditional DTIM-based power-saving scheme */
					/* can we cheat here (i.e. just check MGMT & AC_BE) for better performance? */
#ifdef PCIE_PS_SUPPORT
					if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE))
					{
						if (pAd->Antenna.field.RxPath > 1)
						RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, pAd->StaCfg.BBPR3);
					}
#endif /* PCIE_PS_SUPPORT */
				}
				else 
				{
					if ((pAd->CommonCfg.bACMAPSDTr[QID_AC_VO]) ||
						(pAd->CommonCfg.bACMAPSDTr[QID_AC_VI]) ||
						(pAd->CommonCfg.bACMAPSDTr[QID_AC_BK]) ||
						(pAd->CommonCfg.bACMAPSDTr[QID_AC_BE]))
					{
						/*
							WMM Spec v1.0 3.6.2.4,
							The WMM STA shall remain awake until it receives a
							QoS Data or Null frame addressed to it, with the
							EOSP subfield in QoS Control field set to 1.

							So we can not sleep here or we will suffer a case:

							PS Management Frame -->
							Trigger frame -->
							Beacon (TIM=0) (Beacon is closer to Trig frame) -->
							Station goes to sleep -->
							AP delivery queued UAPSD packets -->
							Station can NOT receive the reply

							Maybe we need a timeout timer to avoid that we do
							NOT receive the EOSP frame.

							We can not use More Data to check if SP is ended
							due to MaxSPLength.
						*/
					}
					else
					{
						USHORT NextDtim = DtimCount;


						if (NextDtim == 0) 
							NextDtim = DtimPeriod;

						TbttNumToNextWakeUp = pAd->StaCfg.DefaultListenCount;
						if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_RECEIVE_DTIM) && (TbttNumToNextWakeUp > NextDtim))
							TbttNumToNextWakeUp = NextDtim;

						if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
						{
							/* Set a flag to go to sleep . Then after parse this RxDoneInterrupt, will go to sleep mode. */
							pAd->ThisTbttNumToNextWakeUp = TbttNumToNextWakeUp;
		                                        AsicSleepThenAutoWakeup(pAd, pAd->ThisTbttNumToNextWakeUp);
							
						}
					}
				}
			}
		}
		/* not my BSSID, ignore it */
	}
	/* sanity check fail, ignore this frame */
	goto LabelOK;

LabelErr:
	DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));

LabelOK:
	if (Ssid != NULL)
		os_free_mem(NULL, Ssid);
	if (pEdcaParm != NULL)
		os_free_mem(NULL, pEdcaParm);
	if (VarIE != NULL)
		os_free_mem(NULL, VarIE);
	if (pHtCapability != NULL)
		os_free_mem(NULL, pHtCapability);
	if (pAddHtInfo != NULL)
		os_free_mem(NULL, pAddHtInfo);
	return;
}

/* 
	==========================================================================
	Description:
		Receive PROBE REQ from remote peer when operating in IBSS mode
	==========================================================================
 */
VOID PeerProbeReqAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	UCHAR         Addr2[MAC_ADDR_LEN];
	CHAR          Ssid[MAX_LEN_OF_SSID];
	UCHAR         SsidLen;
#ifdef DOT11_N_SUPPORT
	UCHAR		  HtLen, AddHtLen, NewExtLen;
#endif /* DOT11_N_SUPPORT */
	HEADER_802_11 ProbeRspHdr;
	NDIS_STATUS   NStatus;
	PUCHAR        pOutBuffer = NULL;
	ULONG         FrameLen = 0;
	LARGE_INTEGER FakeTimestamp;
	UCHAR         DsLen = 1, IbssLen = 2;
	UCHAR         LocalErpIe[3] = {IE_ERP, 1, 0};
	BOOLEAN       Privacy;
	USHORT        CapabilityInfo;


	if (! ADHOC_ON(pAd))
		return;

	if (PeerProbeReqSanity(pAd, Elem->Msg, Elem->MsgLen, Addr2, Ssid, &SsidLen, NULL))
	{
		if ((SsidLen == 0) || SSID_EQUAL(Ssid, SsidLen, pAd->CommonCfg.Ssid, pAd->CommonCfg.SsidLen))
		{			
			/* allocate and send out ProbeRsp frame */
			NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  /* Get an unused nonpaged memory */
			if (NStatus != NDIS_STATUS_SUCCESS)
				return;

			MgtMacHeaderInit(pAd, &ProbeRspHdr, SUBTYPE_PROBE_RSP, 0, Addr2,
								pAd->CommonCfg.Bssid);

			Privacy = (pAd->StaCfg.WepStatus == Ndis802_11Encryption1Enabled) || 
					  (pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled) || 
					  (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled);
			CapabilityInfo = CAP_GENERATE(0, 1, Privacy, (pAd->CommonCfg.TxPreamble == Rt802_11PreambleShort), 0, 0);

			MakeOutgoingFrame(pOutBuffer,                   &FrameLen, 
							  sizeof(HEADER_802_11),        &ProbeRspHdr, 
							  TIMESTAMP_LEN,                &FakeTimestamp,
							  2,                            &pAd->CommonCfg.BeaconPeriod,
							  2,                            &CapabilityInfo,
							  1,                            &SsidIe, 
							  1,                            &pAd->CommonCfg.SsidLen, 
							  pAd->CommonCfg.SsidLen,       pAd->CommonCfg.Ssid,
							  1,                            &SupRateIe, 
							  1,                            &pAd->StaActive.SupRateLen,
							  pAd->StaActive.SupRateLen,    pAd->StaActive.SupRate, 
							  1,                            &DsIe, 
							  1,                            &DsLen, 
							  1,                            &pAd->CommonCfg.Channel,
							  1,                            &IbssIe, 
							  1,                            &IbssLen, 
							  2,                            &pAd->StaActive.AtimWin,
							  END_OF_ARGS);

			if (pAd->StaActive.ExtRateLen)
			{
				ULONG tmp;
				MakeOutgoingFrame(pOutBuffer + FrameLen,        &tmp, 
								  3,                            LocalErpIe,
								  1,                            &ExtRateIe,
								  1,                            &pAd->StaActive.ExtRateLen,
								  pAd->StaActive.ExtRateLen,    &pAd->StaActive.ExtRate,
								  END_OF_ARGS);
				FrameLen += tmp;
			}

        	/* Modify by Eddy, support WPA2PSK in Adhoc mode */
        	if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPANone)
                )
        	{
        		ULONG   tmp;
       	        UCHAR   RSNIe = IE_WPA;

                
        		MakeOutgoingFrame(pOutBuffer + FrameLen,        	&tmp,
        						  1,                              	&RSNIe,
        						  1,                            	&pAd->StaCfg.RSNIE_Len,
        						  pAd->StaCfg.RSNIE_Len,      		pAd->StaCfg.RSN_IE,
        						  END_OF_ARGS);
        		FrameLen += tmp;	
        	}

#ifdef DOT11_N_SUPPORT
			if (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED)
			{
				ULONG TmpLen;
				USHORT  epigram_ie_len;
				UCHAR	BROADCOM[4] = {0x0, 0x90, 0x4c, 0x33};
				HtLen = sizeof(pAd->CommonCfg.HtCapability);
				AddHtLen = sizeof(pAd->CommonCfg.AddHTInfo);
				NewExtLen = 1;
				/* New extension channel offset IE is included in Beacon, Probe Rsp or channel Switch Announcement Frame */
				if (pAd->bBroadComHT == TRUE)
				{
					epigram_ie_len = pAd->MlmeAux.HtCapabilityLen + 4;
					MakeOutgoingFrame(pOutBuffer + FrameLen,            &TmpLen,
								  1,                                &WpaIe,
								  1,          						&epigram_ie_len,
								  4,                                &BROADCOM[0],
								 pAd->MlmeAux.HtCapabilityLen,          &pAd->MlmeAux.HtCapability, 
								  END_OF_ARGS);
				}
				else				
				{
				MakeOutgoingFrame(pOutBuffer + FrameLen,            &TmpLen,
								  1,                                &HtCapIe,
								  1,                                &HtLen,
								 sizeof(HT_CAPABILITY_IE),          &pAd->CommonCfg.HtCapability, 
								  1,                                &AddHtInfoIe,
								  1,                                &AddHtLen,
								 sizeof(ADD_HT_INFO_IE),          &pAd->CommonCfg.AddHTInfo, 
								  1,                                &NewExtChanIe,
								  1,                                &NewExtLen,
								 sizeof(NEW_EXT_CHAN_IE),          &pAd->CommonCfg.NewExtChanOffset, 
								  END_OF_ARGS);
				}
				FrameLen += TmpLen;
			}
#endif /* DOT11_N_SUPPORT */



			MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
			MlmeFreeMemory(pAd, pOutBuffer);
		}
	}
}

VOID BeaconTimeoutAtJoinAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	USHORT Status;
	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - BeaconTimeoutAtJoinAction\n"));
	pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
	Status = MLME_REJ_TIMEOUT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_JOIN_CONF, 2, &Status, 0);
}

/* 
	==========================================================================
	Description:
		Scan timeout procedure. basically add channel index by 1 and rescan
	==========================================================================
 */
VOID ScanTimeoutAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{

#ifdef RTMP_MAC_USB
	/*
	    To prevent data lost.
	    Send an NULL data with turned PSM bit on to current associated AP when SCAN in the channel where
	    associated AP located.
	*/
	if ((pAd->CommonCfg.Channel == pAd->MlmeAux.Channel) && 
		(pAd->MlmeAux.ScanType == SCAN_ACTIVE) && 
		(INFRA_ON(pAd)) && 
		OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)
	)
	{
		RTMPSendNullFrame(pAd, 
						  pAd->CommonCfg.TxRate, 
						  (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) ? TRUE:FALSE));
	}
#endif /* RTMP_MAC_USB */

	if (pAd->StaCfg.bFastConnect && !pAd->StaCfg.bNotFirstScan)
	{
		pAd->MlmeAux.Channel = 0;
		pAd->StaCfg.bNotFirstScan = TRUE;
	}
	else
		pAd->MlmeAux.Channel = NextChannel(pAd, pAd->MlmeAux.Channel);

	/* Only one channel scanned for CISCO beacon request */
	if ((pAd->MlmeAux.ScanType == SCAN_CISCO_ACTIVE) || 
		(pAd->MlmeAux.ScanType == SCAN_CISCO_PASSIVE) ||
		(pAd->MlmeAux.ScanType == SCAN_CISCO_NOISE) ||
		(pAd->MlmeAux.ScanType == SCAN_CISCO_CHANNEL_LOAD))
		pAd->MlmeAux.Channel = 0;

	/* this routine will stop if pAd->MlmeAux.Channel == 0 */
	ScanNextChannel(pAd, OPMODE_STA); 
}

/* 
	==========================================================================
	Description:
	==========================================================================
 */
VOID InvalidStateWhenScan(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	USHORT Status;

	DBGPRINT(RT_DEBUG_TRACE, ("AYNC - InvalidStateWhenScan(state=%ld). Reset SYNC machine\n", pAd->Mlme.SyncMachine.CurrState));
	pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_SCAN_CONF, 2, &Status, 0);
}

/* 
	==========================================================================
	Description:
	==========================================================================
 */
VOID InvalidStateWhenJoin(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	USHORT Status;
	DBGPRINT(RT_DEBUG_TRACE, ("InvalidStateWhenJoin(state=%ld, msg=%ld). Reset SYNC machine\n", 
								pAd->Mlme.SyncMachine.CurrState,
								Elem->MsgType));
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
	{
		RTMPResumeMsduTransmission(pAd);
	}
	pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_JOIN_CONF, 2, &Status, 0);
}

/* 
	==========================================================================
	Description:
	==========================================================================
 */
VOID InvalidStateWhenStart(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	USHORT Status;
	DBGPRINT(RT_DEBUG_TRACE, ("InvalidStateWhenStart(state=%ld). Reset SYNC machine\n", pAd->Mlme.SyncMachine.CurrState));
	pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_START_CONF, 2, &Status, 0);
}

/* 
	==========================================================================
	Description:

	IRQL = DISPATCH_LEVEL
	
	==========================================================================
 */
VOID EnqueuePsPoll(
	IN PRTMP_ADAPTER pAd) 
{
#ifdef RALINK_ATE
    if (ATE_ON(pAd))
    {
		return;
    }
#endif /* RALINK_ATE */

	
	if (pAd->StaCfg.WindowsPowerMode == Ndis802_11PowerModeLegacy_PSP)
    	pAd->PsPollFrame.FC.PwrMgmt = PWR_SAVE;
	MiniportMMRequest(pAd, 0, (PUCHAR)&pAd->PsPollFrame, sizeof(PSPOLL_FRAME));
#ifdef RTMP_MAC_USB
	/* Keep Waking up */
	if (pAd->CountDowntoPsm == 0)
		pAd->CountDowntoPsm = 2;	/* 100 ms; stay awake 200ms at most, average will be 1xx ms */
#endif /* RTMP_MAC_USB */

}


/* 
	==========================================================================
	Description:
	==========================================================================
 */
VOID EnqueueProbeRequest(
	IN PRTMP_ADAPTER pAd) 
{
	NDIS_STATUS     NState;
	PUCHAR          pOutBuffer;
	ULONG           FrameLen = 0;
	HEADER_802_11   Hdr80211;

	DBGPRINT(RT_DEBUG_TRACE, ("force out a ProbeRequest ...\n"));

	NState = MlmeAllocateMemory(pAd, &pOutBuffer);  /* Get an unused nonpaged memory */
	if (NState == NDIS_STATUS_SUCCESS) 
	{
		MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, BROADCAST_ADDR,
							BROADCAST_ADDR);

		/* this ProbeRequest explicitly specify SSID to reduce unwanted ProbeResponse */
		MakeOutgoingFrame(pOutBuffer,                     &FrameLen,
						  sizeof(HEADER_802_11),          &Hdr80211,
						  1,                              &SsidIe,
						  1,                              &pAd->CommonCfg.SsidLen,
						  pAd->CommonCfg.SsidLen,		  pAd->CommonCfg.Ssid,
						  1,                              &SupRateIe,
						  1,                              &pAd->StaActive.SupRateLen,
						  pAd->StaActive.SupRateLen,      pAd->StaActive.SupRate, 
						  END_OF_ARGS);
		MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
		MlmeFreeMemory(pAd, pOutBuffer);
	}

}

#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
VOID BuildEffectedChannelList(
	IN PRTMP_ADAPTER pAd)
{
	UCHAR		EChannel[11];
	UCHAR		i, j, k;
	UCHAR		UpperChannel = 0, LowerChannel = 0;
	
	RTMPZeroMemory(EChannel, 11);
	DBGPRINT(RT_DEBUG_TRACE, ("BuildEffectedChannelList:CtrlCh=%d,CentCh=%d,AuxCtrlCh=%d,AuxExtCh=%d\n", 
								pAd->CommonCfg.Channel, pAd->CommonCfg.CentralChannel, 
								pAd->MlmeAux.AddHtInfo.ControlChan, 
								pAd->MlmeAux.AddHtInfo.AddHtInfo.ExtChanOffset));

	/* 802.11n D4 11.14.3.3: If no secondary channel has been selected, all channels in the frequency band shall be scanned. */
	{
		for (k = 0;k < pAd->ChannelListNum;k++)
		{
			if (pAd->ChannelList[k].Channel <=14 )
			pAd->ChannelList[k].bEffectedChannel = TRUE;
		}
		return;
	}	
	
	i = 0;
	/* Find upper and lower channel according to 40MHz current operation. */
	if (pAd->CommonCfg.CentralChannel < pAd->CommonCfg.Channel)
	{
		UpperChannel = pAd->CommonCfg.Channel;
		LowerChannel = pAd->CommonCfg.CentralChannel-2;
	}
	else if (pAd->CommonCfg.CentralChannel > pAd->CommonCfg.Channel)
	{
		UpperChannel = pAd->CommonCfg.CentralChannel+2;
		LowerChannel = pAd->CommonCfg.Channel;
	}
	else
	{
		DBGPRINT(RT_DEBUG_TRACE, ("LinkUP 20MHz . No Effected Channel \n"));
		/* Now operating in 20MHz, doesn't find 40MHz effected channels */
		return;
	}

	DeleteEffectedChannelList(pAd);	

	DBGPRINT(RT_DEBUG_TRACE, ("BuildEffectedChannelList!LowerChannel ~ UpperChannel; %d ~ %d \n", LowerChannel, UpperChannel));

	/* Find all channels that are below lower channel.. */
	if (LowerChannel > 1)
	{
		EChannel[0] = LowerChannel - 1;
		i = 1;
		if (LowerChannel > 2)
		{
			EChannel[1] = LowerChannel - 2;
			i = 2;
			if (LowerChannel > 3)
			{
				EChannel[2] = LowerChannel - 3;
				i = 3;
			}
		}
	}
	/* Find all channels that are between  lower channel and upper channel. */
	for (k = LowerChannel;k <= UpperChannel;k++)
	{
		EChannel[i] = k;
		i++;
	}
	/* Find all channels that are above upper channel.. */
	if (UpperChannel < 14)
	{
		EChannel[i] = UpperChannel + 1;
		i++;
		if (UpperChannel < 13)
		{
			EChannel[i] = UpperChannel + 2;
			i++;
			if (UpperChannel < 12)
			{
				EChannel[i] = UpperChannel + 3;
				i++;
			}
		}
	}
	/* 
	    Total i channels are effected channels. 
	    Now find corresponding channel in ChannelList array.  Then set its bEffectedChannel= TRUE
	*/
	for (j = 0;j < i;j++)
	{
		for (k = 0;k < pAd->ChannelListNum;k++)
		{
			if (pAd->ChannelList[k].Channel == EChannel[j])
			{
				pAd->ChannelList[k].bEffectedChannel = TRUE;
				DBGPRINT(RT_DEBUG_TRACE,(" EffectedChannel[%d]( =%d)\n", k, EChannel[j]));
				break;
			}
		}
	}
}


VOID DeleteEffectedChannelList(
	IN PRTMP_ADAPTER pAd)
{
	UCHAR		i;
	/*Clear all bEffectedChannel in ChannelList array. */
 	for (i = 0; i < pAd->ChannelListNum; i++)		
	{
		pAd->ChannelList[i].bEffectedChannel = FALSE;
	}	
}


/*
	========================================================================
	
	Routine Description:
		Control Primary&Central Channel, ChannelWidth and Second Channel Offset

	Arguments:
		pAd						Pointer to our adapter
		PrimaryChannel			Primary Channel
		CentralChannel			Central Channel
		ChannelWidth				BW_20 or BW_40
		SecondaryChannelOffset	EXTCHA_NONE, EXTCHA_ABOVE and EXTCHA_BELOW
		
	Return Value:
		None
		
	Note:
		
	========================================================================
*/
VOID CntlChannelWidth(
	IN PRTMP_ADAPTER	pAd,
	IN UCHAR			PrimaryChannel,
	IN UCHAR			CentralChannel,	
	IN UCHAR			ChannelWidth,
	IN UCHAR			SecondaryChannelOffset) 
{
	UCHAR	Value = 0;
	UINT32	Data = 0;


	DBGPRINT(RT_DEBUG_TRACE, ("%s: PrimaryChannel[%d] \n",__FUNCTION__,PrimaryChannel));
	DBGPRINT(RT_DEBUG_TRACE, ("%s: CentralChannel[%d] \n",__FUNCTION__,CentralChannel));
	DBGPRINT(RT_DEBUG_TRACE, ("%s: ChannelWidth[%d] \n",__FUNCTION__,ChannelWidth));
	DBGPRINT(RT_DEBUG_TRACE, ("%s: SecondaryChannelOffset[%d] \n",__FUNCTION__,SecondaryChannelOffset));

#ifdef DOT11_N_SUPPORT
	/*Change to AP channel */
	if (ChannelWidth == BW_40)
	{
		if(SecondaryChannelOffset == EXTCHA_ABOVE)
		{	
			/* Must using 40MHz. */
			pAd->CommonCfg.BBPCurrentBW = BW_40;
			AsicSwitchChannel(pAd, CentralChannel, FALSE);
			AsicLockChannel(pAd, CentralChannel);

			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &Value);
			Value &= (~0x18);
			Value |= 0x10;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, Value);

			/*  RX : control channel at lower */ 
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &Value);
			Value &= (~0x20);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, Value);

			RTMP_IO_READ32(pAd, TX_BAND_CFG, &Data);
			Data &= 0xfffffffe;
			RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Data);

			if (pAd->MACVersion == 0x28600100)
			{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x1A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x16);
			DBGPRINT(RT_DEBUG_TRACE, ("!!!rt2860C !!! \n" ));
			}	

			DBGPRINT(RT_DEBUG_TRACE, ("!!!40MHz Lower !!! Control Channel at Below. Central = %d \n", pAd->CommonCfg.CentralChannel ));
		}
		else if (SecondaryChannelOffset == EXTCHA_BELOW)
		{	
			/* Must using 40MHz. */
			pAd->CommonCfg.BBPCurrentBW = BW_40;
			AsicSwitchChannel(pAd, CentralChannel, FALSE);
			AsicLockChannel(pAd, CentralChannel);

			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &Value);
			Value &= (~0x18);
			Value |= 0x10;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, Value);

			RTMP_IO_READ32(pAd, TX_BAND_CFG, &Data);
			Data |= 0x1;
			RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Data);

			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &Value);
			Value |= (0x20);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, Value);

			if (pAd->MACVersion == 0x28600100)
			{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x1A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x16);
			DBGPRINT(RT_DEBUG_TRACE, ("!!!rt2860C !!! \n" ));
			}

			DBGPRINT(RT_DEBUG_TRACE, ("!!! 40MHz Upper !!! Control Channel at UpperCentral = %d \n", CentralChannel));
		}
	}
	else
#endif /* DOT11_N_SUPPORT */
	{
		pAd->CommonCfg.BBPCurrentBW = BW_20;
		AsicSwitchChannel(pAd, PrimaryChannel, FALSE);
		AsicLockChannel(pAd, PrimaryChannel);

		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &Value);
		Value &= (~0x18);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, Value);

		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Data);
		Data &= 0xfffffffe;
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Data);

		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &Value);
		Value &= (~0x20);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, Value);

		if (pAd->MACVersion == 0x28600100)
		{
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x16);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x08);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x11);
		DBGPRINT(RT_DEBUG_TRACE, ("!!!rt2860C !!! \n" ));
		}

		DBGPRINT(RT_DEBUG_TRACE, ("!!! 20MHz !!! \n" ));
	}

	RTMPSetAGCInitValue(pAd, pAd->CommonCfg.BBPCurrentBW);

	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R66, &pAd->BbpTuning.R66CurrentValue);
}


#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */

/*
    ==========================================================================
    Description:
        MLME Cancel the SCAN req state machine procedure
    ==========================================================================
 */
VOID ScanCnclAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	BOOLEAN Cancelled;

	RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &Cancelled);
	pAd->MlmeAux.Channel = 0;
	ScanNextChannel(pAd, OPMODE_STA);

	return;
}
