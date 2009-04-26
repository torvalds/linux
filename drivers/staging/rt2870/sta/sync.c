/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
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
 *************************************************************************

	Module Name:
	sync.c

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	John Chang	2004-09-01      modified for rt2561/2661
	Jan Lee		2006-08-01      modified for rt2860 for 802.11n
*/
#include "../rt_config.h"

#define ADHOC_ENTRY_BEACON_LOST_TIME	(2*OS_HZ)	// 2 sec

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

	// column 1
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_SCAN_REQ, (STATE_MACHINE_FUNC)MlmeScanReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_JOIN_REQ, (STATE_MACHINE_FUNC)MlmeJoinReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_START_REQ, (STATE_MACHINE_FUNC)MlmeStartReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_PEER_BEACON, (STATE_MACHINE_FUNC)PeerBeacon);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_PEER_PROBE_REQ, (STATE_MACHINE_FUNC)PeerProbeReqAction);

	//column 2
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_MLME_SCAN_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenScan);
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_MLME_JOIN_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenJoin);
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_MLME_START_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenStart);
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_PEER_BEACON, (STATE_MACHINE_FUNC)PeerBeaconAtJoinAction);
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_BEACON_TIMEOUT, (STATE_MACHINE_FUNC)BeaconTimeoutAtJoinAction);

	// column 3
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_MLME_SCAN_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenScan);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_MLME_JOIN_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenJoin);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_MLME_START_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenStart);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_PEER_BEACON, (STATE_MACHINE_FUNC)PeerBeaconAtScanAction);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_PEER_PROBE_RSP, (STATE_MACHINE_FUNC)PeerBeaconAtScanAction);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_SCAN_TIMEOUT, (STATE_MACHINE_FUNC)ScanTimeoutAction);

	// timer init
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

	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
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
#endif // DOT11_N_SUPPORT //

	MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_BEACON_TIMEOUT, 0, NULL);
	RT28XX_MLME_HANDLER(pAd);
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


	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))
		return;

	if (MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_SCAN_TIMEOUT, 0, NULL))
	{
		RT28XX_MLME_HANDLER(pAd);
	}
	else
	{
		// To prevent SyncMachine.CurrState is SCAN_LISTEN forever.
		pAd->MlmeAux.Channel = 0;
		ScanNextChannel(pAd);
		if (pAd->CommonCfg.bWirelessEvent)
		{
			RTMPSendWirelessEvent(pAd, IW_SCAN_ENQUEUE_FAIL_EVENT_FLAG, pAd->MacTab.Content[BSSID_WCID].Addr, BSS0, 0);
		}
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
	PHEADER_802_11 pHdr80211;
	PUCHAR         pOutBuffer = NULL;
	NDIS_STATUS    NStatus;

	// Check the total scan tries for one single OID command
	// If this is the CCX 2.0 Case, skip that!
	if ( !RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_START_UP))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeScanReqAction before Startup\n"));
		return;
	}

	// Increase the scan retry counters.
	pAd->StaCfg.ScanCnt++;


	// first check the parameter sanity
	if (MlmeScanReqSanity(pAd,
						  Elem->Msg,
						  Elem->MsgLen,
						  &BssType,
						  Ssid,
						  &SsidLen,
						  &ScanType))
	{

		// Check for channel load and noise hist request
		// Suspend MSDU only at scan request, not the last two mentioned
		if ((ScanType == SCAN_CISCO_NOISE) || (ScanType == SCAN_CISCO_CHANNEL_LOAD))
		{
			if (pAd->StaCfg.CCXScanChannel != pAd->CommonCfg.Channel)
				RTMPSuspendMsduTransmission(pAd);			// Suspend MSDU transmission here
		}
		else
		{
			// Suspend MSDU transmission here
			RTMPSuspendMsduTransmission(pAd);
		}

		//
		// To prevent data lost.
		// Send an NULL data with turned PSM bit on to current associated AP before SCAN progress.
		// And should send an NULL data with turned PSM bit off to AP, when scan progress done
		//
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED) && (INFRA_ON(pAd)))
		{
			NStatus = MlmeAllocateMemory(pAd, (PVOID)&pOutBuffer);
			if (NStatus	== NDIS_STATUS_SUCCESS)
			{
				pHdr80211 = (PHEADER_802_11) pOutBuffer;
				MgtMacHeaderInit(pAd, pHdr80211, SUBTYPE_NULL_FUNC, 1, pAd->CommonCfg.Bssid, pAd->CommonCfg.Bssid);
				pHdr80211->Duration = 0;
				pHdr80211->FC.Type = BTYPE_DATA;
				pHdr80211->FC.PwrMgmt = PWR_SAVE;

				// Send using priority queue
				MiniportMMRequest(pAd, 0, pOutBuffer, sizeof(HEADER_802_11));
				DBGPRINT(RT_DEBUG_TRACE, ("MlmeScanReqAction -- Send PSM Data frame for off channel RM\n"));
				MlmeFreeMemory(pAd, pOutBuffer);
				RTMPusecDelay(5000);
			}
		}

		NdisGetSystemUpTime(&Now);
		pAd->StaCfg.LastScanTime = Now;
		// reset all the timers
		RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);

		// record desired BSS parameters
		pAd->MlmeAux.BssType = BssType;
		pAd->MlmeAux.ScanType = ScanType;
		pAd->MlmeAux.SsidLen = SsidLen;
        NdisZeroMemory(pAd->MlmeAux.Ssid, MAX_LEN_OF_SSID);
		NdisMoveMemory(pAd->MlmeAux.Ssid, Ssid, SsidLen);

		// start from the first channel
		pAd->MlmeAux.Channel = FirstChannel(pAd);

		// Change the scan channel when dealing with CCX beacon report
		if ((ScanType == SCAN_CISCO_PASSIVE) || (ScanType == SCAN_CISCO_ACTIVE) ||
			(ScanType == SCAN_CISCO_CHANNEL_LOAD) || (ScanType == SCAN_CISCO_NOISE))
			pAd->MlmeAux.Channel = pAd->StaCfg.CCXScanChannel;

		// Let BBP register at 20MHz to do scan
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
		BBPValue &= (~0x18);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);
		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - BBP R4 to 20MHz.l\n"));
		ScanNextChannel(pAd);
	}
	else
	{
		DBGPRINT_ERR(("SYNC - MlmeScanReqAction() sanity check fail\n"));
		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_SCAN_CONF, 2, &Status);
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

	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeJoinReqAction(BSS #%ld)\n", pInfo->BssIdx));


	// reset all the timers
	RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);
	RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);

	pBss = &pAd->MlmeAux.SsidBssTab.BssEntry[pInfo->BssIdx];

	// record the desired SSID & BSSID we're waiting for
	COPY_MAC_ADDR(pAd->MlmeAux.Bssid, pBss->Bssid);

	// If AP's SSID is not hidden, it is OK for updating ssid to MlmeAux again.
	if (pBss->Hidden == 0)
	{
		NdisMoveMemory(pAd->MlmeAux.Ssid, pBss->Ssid, pBss->SsidLen);
		pAd->MlmeAux.SsidLen = pBss->SsidLen;
	}

	pAd->MlmeAux.BssType = pBss->BssType;
	pAd->MlmeAux.Channel = pBss->Channel;
	pAd->MlmeAux.CentralChannel = pBss->CentralChannel;

	// Let BBP register at 20MHz to do scan
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
	BBPValue &= (~0x18);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);
	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - BBP R4 to 20MHz.l\n"));

	// switch channel and waiting for beacon timer
	AsicSwitchChannel(pAd, pAd->MlmeAux.Channel, FALSE);
	AsicLockChannel(pAd, pAd->MlmeAux.Channel);
	RTMPSetTimer(&pAd->MlmeAux.BeaconTimer, JOIN_TIMEOUT);

    do
	{
		if (((pAd->CommonCfg.bIEEE80211H == 1) &&
            (pAd->MlmeAux.Channel > 14) &&
             RadarChannelCheck(pAd, pAd->MlmeAux.Channel))
            )
		{
			//
			// We can't send any Probe request frame to meet 802.11h.
			//
			if (pBss->Hidden == 0)
				break;
		}

		//
		// send probe request
		//
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
				//
				// Overwrite Support Rate, CCK rate are not allowed
				//
				pSupRate = ASupRate;
				SupRateLen = ASupRateLen;
				ExtRateLen = 0;
			}

			if (pAd->MlmeAux.BssType == BSS_INFRA)
				MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, pAd->MlmeAux.Bssid, pAd->MlmeAux.Bssid);
			else
				MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, BROADCAST_ADDR, BROADCAST_ADDR);

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

	// New for WPA security suites
	UCHAR						VarIE[MAX_VIE_LEN]; 	// Total VIE length = MAX_VIE_LEN - -5
	NDIS_802_11_VARIABLE_IEs	*pVIE = NULL;
	LARGE_INTEGER				TimeStamp;
	BOOLEAN Privacy;
	USHORT Status;

	// Init Variable IE structure
	pVIE = (PNDIS_802_11_VARIABLE_IEs) VarIE;
	pVIE->Length = 0;
	TimeStamp.u.LowPart  = 0;
	TimeStamp.u.HighPart = 0;

	if (MlmeStartReqSanity(pAd, Elem->Msg, Elem->MsgLen, Ssid, &SsidLen))
	{
		// reset all the timers
		RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);
		RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);

		//
		// Start a new IBSS. All IBSS parameters are decided now....
		//
		DBGPRINT(RT_DEBUG_TRACE, ("MlmeStartReqAction - Start a new IBSS. All IBSS parameters are decided now.... \n"));
		pAd->MlmeAux.BssType           = BSS_ADHOC;
		NdisMoveMemory(pAd->MlmeAux.Ssid, Ssid, SsidLen);
		pAd->MlmeAux.SsidLen           = SsidLen;

		// generate a radom number as BSSID
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
		if (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED)
		{
			RTMPUpdateHTIE(&pAd->CommonCfg.DesiredHtPhy, &pAd->StaCfg.DesiredHtPhyInfo.MCSSet[0], &pAd->MlmeAux.HtCapability, &pAd->MlmeAux.AddHtInfo);
			pAd->MlmeAux.HtCapabilityLen = sizeof(HT_CAPABILITY_IE);
			// Not turn pAd->StaActive.SupportedHtPhy.bHtEnable = TRUE here.
			DBGPRINT(RT_DEBUG_TRACE, ("SYNC -pAd->StaActive.SupportedHtPhy.bHtEnable = TRUE\n"));
		}
		else
#endif // DOT11_N_SUPPORT //
		{
			pAd->MlmeAux.HtCapabilityLen = 0;
			pAd->StaActive.SupportedPhyInfo.bHtEnable = FALSE;
		}
		// temporarily not support QOS in IBSS
		NdisZeroMemory(&pAd->MlmeAux.APEdcaParm, sizeof(EDCA_PARM));
		NdisZeroMemory(&pAd->MlmeAux.APQbssLoad, sizeof(QBSS_LOAD_PARM));
		NdisZeroMemory(&pAd->MlmeAux.APQosCapability, sizeof(QOS_CAPABILITY_PARM));

		AsicSwitchChannel(pAd, pAd->MlmeAux.Channel, FALSE);
		AsicLockChannel(pAd, pAd->MlmeAux.Channel);

		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeStartReqAction(ch= %d,sup rates= %d, ext rates=%d)\n",
			pAd->MlmeAux.Channel, pAd->MlmeAux.SupRateLen, pAd->MlmeAux.ExtRateLen));

		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		Status = MLME_SUCCESS;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_START_CONF, 2, &Status);
	}
	else
	{
		DBGPRINT_ERR(("SYNC - MlmeStartReqAction() sanity check fail.\n"));
		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_START_CONF, 2, &Status);
	}
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
	UCHAR           Ssid[MAX_LEN_OF_SSID], BssType, Channel, NewChannel,
					SsidLen, DtimCount, DtimPeriod, BcastFlag, MessageToMe;
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
	UCHAR						VarIE[MAX_VIE_LEN];		// Total VIE length = MAX_VIE_LEN - -5
	NDIS_802_11_VARIABLE_IEs	*pVIE = NULL;
	HT_CAPABILITY_IE		HtCapability;
	ADD_HT_INFO_IE		AddHtInfo;	// AP might use this additional ht info IE
	UCHAR			HtCapabilityLen = 0, PreNHtCapabilityLen = 0;
	UCHAR			AddHtInfoLen;
	UCHAR			NewExtChannelOffset = 0xff;


	// NdisFillMemory(Ssid, MAX_LEN_OF_SSID, 0x00);
	pFrame = (PFRAME_802_11) Elem->Msg;
	// Init Variable IE structure
	pVIE = (PNDIS_802_11_VARIABLE_IEs) VarIE;
	pVIE->Length = 0;
#ifdef DOT11_N_SUPPORT
    RTMPZeroMemory(&HtCapability, sizeof(HtCapability));
	RTMPZeroMemory(&AddHtInfo, sizeof(ADD_HT_INFO_IE));
#endif // DOT11_N_SUPPORT //

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
								&EdcaParm,
								&QbssLoad,
								&QosCapability,
								&RalinkIe,
								&HtCapabilityLen,
								&PreNHtCapabilityLen,
								&HtCapability,
								&AddHtInfoLen,
								&AddHtInfo,
								&NewExtChannelOffset,
								&LenVIE,
								pVIE))
	{
		ULONG Idx;
		CHAR Rssi = 0;

		Idx = BssTableSearch(&pAd->ScanTab, Bssid, Channel);
		if (Idx != BSS_NOT_FOUND)
			Rssi = pAd->ScanTab.BssEntry[Idx].Rssi;

		Rssi = RTMPMaxRssi(pAd, ConvertToRssi(pAd, Elem->Rssi0, RSSI_0), ConvertToRssi(pAd, Elem->Rssi1, RSSI_1), ConvertToRssi(pAd, Elem->Rssi2, RSSI_2));


#ifdef DOT11_N_SUPPORT
		if ((HtCapabilityLen > 0) || (PreNHtCapabilityLen > 0))
			HtCapabilityLen = SIZE_HT_CAP_IE;
#endif // DOT11_N_SUPPORT //
		if ((pAd->StaCfg.CCXReqType != MSRN_TYPE_UNUSED) && (Channel == pAd->StaCfg.CCXScanChannel))
		{
			Idx = BssTableSetEntry(pAd, &pAd->StaCfg.CCXBssTab, Bssid, Ssid, SsidLen, BssType, BeaconPeriod,
						 &CfParm, AtimWin, CapabilityInfo, SupRate, SupRateLen,ExtRate, ExtRateLen, &HtCapability,
						 &AddHtInfo, HtCapabilityLen, AddHtInfoLen, NewExtChannelOffset, Channel, Rssi, TimeStamp, CkipFlag,
						 &EdcaParm, &QosCapability, &QbssLoad, LenVIE, pVIE);
			if (Idx != BSS_NOT_FOUND)
			{
				NdisMoveMemory(pAd->StaCfg.CCXBssTab.BssEntry[Idx].PTSF, &Elem->Msg[24], 4);
				NdisMoveMemory(&pAd->StaCfg.CCXBssTab.BssEntry[Idx].TTSF[0], &Elem->TimeStamp.u.LowPart, 4);
				NdisMoveMemory(&pAd->StaCfg.CCXBssTab.BssEntry[Idx].TTSF[4], &Elem->TimeStamp.u.LowPart, 4);
				if (pAd->StaCfg.CCXReqType == MSRN_TYPE_BEACON_REQ)
					AironetAddBeaconReport(pAd, Idx, Elem);
			}
		}
		else
		{
			Idx = BssTableSetEntry(pAd, &pAd->ScanTab, Bssid, Ssid, SsidLen, BssType, BeaconPeriod,
						  &CfParm, AtimWin, CapabilityInfo, SupRate, SupRateLen, ExtRate, ExtRateLen,  &HtCapability,
						 &AddHtInfo, HtCapabilityLen, AddHtInfoLen, NewExtChannelOffset, Channel, Rssi, TimeStamp, CkipFlag,
						 &EdcaParm, &QosCapability, &QbssLoad, LenVIE, pVIE);

			if (Idx != BSS_NOT_FOUND)
			{
				NdisMoveMemory(pAd->ScanTab.BssEntry[Idx].PTSF, &Elem->Msg[24], 4);
				NdisMoveMemory(&pAd->ScanTab.BssEntry[Idx].TTSF[0], &Elem->TimeStamp.u.LowPart, 4);
				NdisMoveMemory(&pAd->ScanTab.BssEntry[Idx].TTSF[4], &Elem->TimeStamp.u.LowPart, 4);
			}
		}
	}
	// sanity check fail, ignored
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
	UCHAR         Ssid[MAX_LEN_OF_SSID], SsidLen, BssType, Channel, MessageToMe,
				  DtimCount, DtimPeriod, BcastFlag, NewChannel;
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
	UCHAR						VarIE[MAX_VIE_LEN];		// Total VIE length = MAX_VIE_LEN - -5
	NDIS_802_11_VARIABLE_IEs	*pVIE = NULL;
	ULONG           RalinkIe;
	ULONG         Idx;
	HT_CAPABILITY_IE		HtCapability;
	ADD_HT_INFO_IE		AddHtInfo;	// AP might use this additional ht info IE
	UCHAR				HtCapabilityLen = 0, PreNHtCapabilityLen = 0;
	UCHAR			AddHtInfoLen;
	UCHAR			NewExtChannelOffset = 0xff;
#ifdef DOT11_N_SUPPORT
	UCHAR			CentralChannel;
#endif // DOT11_N_SUPPORT //

	// Init Variable IE structure
	pVIE = (PNDIS_802_11_VARIABLE_IEs) VarIE;
	pVIE->Length = 0;
    RTMPZeroMemory(&HtCapability, sizeof(HtCapability));
	RTMPZeroMemory(&AddHtInfo, sizeof(ADD_HT_INFO_IE));


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
								&HtCapability,
								&AddHtInfoLen,
								&AddHtInfo,
								&NewExtChannelOffset,
								&LenVIE,
								pVIE))
	{
		// Disqualify 11b only adhoc when we are in 11g only adhoc mode
		if ((BssType == BSS_ADHOC) && (pAd->CommonCfg.PhyMode == PHY_11G) && ((SupRateLen+ExtRateLen)< 12))
			return;

		// BEACON from desired BSS/IBSS found. We should be able to decide most
		// BSS parameters here.
		// Q. But what happen if this JOIN doesn't conclude a successful ASSOCIATEION?
		//    Do we need to receover back all parameters belonging to previous BSS?
		// A. Should be not. There's no back-door recover to previous AP. It still need
		//    a new JOIN-AUTH-ASSOC sequence.
		if (MAC_ADDR_EQUAL(pAd->MlmeAux.Bssid, Bssid))
		{
			DBGPRINT(RT_DEBUG_TRACE, ("SYNC - receive desired BEACON at JoinWaitBeacon... Channel = %d\n", Channel));
			RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);

			// Update RSSI to prevent No signal display when cards first initialized
			pAd->StaCfg.RssiSample.LastRssi0	= ConvertToRssi(pAd, Elem->Rssi0, RSSI_0);
			pAd->StaCfg.RssiSample.LastRssi1	= ConvertToRssi(pAd, Elem->Rssi1, RSSI_1);
			pAd->StaCfg.RssiSample.LastRssi2	= ConvertToRssi(pAd, Elem->Rssi2, RSSI_2);
			pAd->StaCfg.RssiSample.AvgRssi0	= pAd->StaCfg.RssiSample.LastRssi0;
			pAd->StaCfg.RssiSample.AvgRssi0X8	= pAd->StaCfg.RssiSample.AvgRssi0 << 3;
			pAd->StaCfg.RssiSample.AvgRssi1	= pAd->StaCfg.RssiSample.LastRssi1;
			pAd->StaCfg.RssiSample.AvgRssi1X8	= pAd->StaCfg.RssiSample.AvgRssi1 << 3;
			pAd->StaCfg.RssiSample.AvgRssi2	= pAd->StaCfg.RssiSample.LastRssi2;
			pAd->StaCfg.RssiSample.AvgRssi2X8	= pAd->StaCfg.RssiSample.AvgRssi2 << 3;

			//
			// We need to check if SSID only set to any, then we can record the current SSID.
			// Otherwise will cause hidden SSID association failed.
			//
			if (pAd->MlmeAux.SsidLen == 0)
			{
				NdisMoveMemory(pAd->MlmeAux.Ssid, Ssid, SsidLen);
				pAd->MlmeAux.SsidLen = SsidLen;
			}
			else
			{
				Idx = BssSsidTableSearch(&pAd->ScanTab, Bssid, pAd->MlmeAux.Ssid, pAd->MlmeAux.SsidLen, Channel);

				if (Idx != BSS_NOT_FOUND)
				{
					//
					// Multiple SSID case, used correct CapabilityInfo
					//
					CapabilityInfo = pAd->ScanTab.BssEntry[Idx].CapabilityInfo;
				}
			}
			NdisMoveMemory(pAd->MlmeAux.Bssid, Bssid, MAC_ADDR_LEN);
			pAd->MlmeAux.CapabilityInfo = CapabilityInfo & SUPPORTED_CAPABILITY_INFO;
			pAd->MlmeAux.BssType = BssType;
			pAd->MlmeAux.BeaconPeriod = BeaconPeriod;
			pAd->MlmeAux.Channel = Channel;
			pAd->MlmeAux.AtimWin = AtimWin;
			pAd->MlmeAux.CfpPeriod = Cf.CfpPeriod;
			pAd->MlmeAux.CfpMaxDuration = Cf.CfpMaxDuration;
			pAd->MlmeAux.APRalinkIe = RalinkIe;

			// Copy AP's supported rate to MlmeAux for creating assoication request
			// Also filter out not supported rate
			pAd->MlmeAux.SupRateLen = SupRateLen;
			NdisMoveMemory(pAd->MlmeAux.SupRate, SupRate, SupRateLen);
			RTMPCheckRates(pAd, pAd->MlmeAux.SupRate, &pAd->MlmeAux.SupRateLen);
			pAd->MlmeAux.ExtRateLen = ExtRateLen;
			NdisMoveMemory(pAd->MlmeAux.ExtRate, ExtRate, ExtRateLen);
			RTMPCheckRates(pAd, pAd->MlmeAux.ExtRate, &pAd->MlmeAux.ExtRateLen);

            NdisZeroMemory(pAd->StaActive.SupportedPhyInfo.MCSSet, 16);
#ifdef DOT11_N_SUPPORT
			pAd->MlmeAux.NewExtChannelOffset = NewExtChannelOffset;
			pAd->MlmeAux.HtCapabilityLen = HtCapabilityLen;

			// filter out un-supported ht rates
			if (((HtCapabilityLen > 0) || (PreNHtCapabilityLen > 0)) && (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED))
			{
				RTMPZeroMemory(&pAd->MlmeAux.HtCapability, SIZE_HT_CAP_IE);
   				RTMPMoveMemory(&pAd->MlmeAux.AddHtInfo, &AddHtInfo, SIZE_ADD_HT_INFO_IE);

				// StaActive.SupportedHtPhy.MCSSet stores Peer AP's 11n Rx capability
				NdisMoveMemory(pAd->StaActive.SupportedPhyInfo.MCSSet, HtCapability.MCSSet, 16);
				pAd->MlmeAux.NewExtChannelOffset = NewExtChannelOffset;
				pAd->MlmeAux.HtCapabilityLen = SIZE_HT_CAP_IE;
				pAd->StaActive.SupportedPhyInfo.bHtEnable = TRUE;
				if (PreNHtCapabilityLen > 0)
					pAd->StaActive.SupportedPhyInfo.bPreNHt = TRUE;
				RTMPCheckHt(pAd, BSSID_WCID, &HtCapability, &AddHtInfo);
				// Copy AP Parameter to StaActive.  This is also in LinkUp.
				DBGPRINT(RT_DEBUG_TRACE, ("PeerBeaconAtJoinAction! (MpduDensity=%d, MaxRAmpduFactor=%d, BW=%d)\n",
					pAd->StaActive.SupportedHtPhy.MpduDensity, pAd->StaActive.SupportedHtPhy.MaxRAmpduFactor, HtCapability.HtCapInfo.ChannelWidth));

				if (AddHtInfoLen > 0)
				{
					CentralChannel = AddHtInfo.ControlChan;
		 			// Check again the Bandwidth capability of this AP.
		 			if ((AddHtInfo.ControlChan > 2)&& (AddHtInfo.AddHtInfo.ExtChanOffset == EXTCHA_BELOW) && (HtCapability.HtCapInfo.ChannelWidth == BW_40))
		 			{
		 				CentralChannel = AddHtInfo.ControlChan - 2;
		 			}
		 			else if ((AddHtInfo.AddHtInfo.ExtChanOffset == EXTCHA_ABOVE) && (HtCapability.HtCapInfo.ChannelWidth == BW_40))
		 			{
		 				CentralChannel = AddHtInfo.ControlChan + 2;
		 			}

					// Check Error .
					if (pAd->MlmeAux.CentralChannel != CentralChannel)
		 				DBGPRINT(RT_DEBUG_ERROR, ("PeerBeaconAtJoinAction HT===>Beacon Central Channel = %d, Control Channel = %d. Mlmeaux CentralChannel = %d\n", CentralChannel, AddHtInfo.ControlChan, pAd->MlmeAux.CentralChannel));

		 			DBGPRINT(RT_DEBUG_TRACE, ("PeerBeaconAtJoinAction HT===>Central Channel = %d, Control Channel = %d,  .\n", CentralChannel, AddHtInfo.ControlChan));

				}

			}
			else
#endif // DOT11_N_SUPPORT //
			{
   				// To prevent error, let legacy AP must have same CentralChannel and Channel.
				if ((HtCapabilityLen == 0) && (PreNHtCapabilityLen == 0))
					pAd->MlmeAux.CentralChannel = pAd->MlmeAux.Channel;

				pAd->StaActive.SupportedPhyInfo.bHtEnable = FALSE;
				RTMPZeroMemory(&pAd->MlmeAux.HtCapability, SIZE_HT_CAP_IE);
				RTMPZeroMemory(&pAd->MlmeAux.AddHtInfo, SIZE_ADD_HT_INFO_IE);
			}

			RTMPUpdateMlmeRate(pAd);

			// copy QOS related information
			if ((pAd->CommonCfg.bWmmCapable)
#ifdef DOT11_N_SUPPORT
				 || (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED)
#endif // DOT11_N_SUPPORT //
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
				//We need to change our TxPower for CCX 2.0 AP Control of Client Transmit Power
				ChangeToCellPowerLimit(pAd, AironetCellPowerLimit);
			}
			else  //Used the default TX Power Percentage.
				pAd->CommonCfg.TxPowerPercentage = pAd->CommonCfg.TxPowerDefault;

			pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
			Status = MLME_SUCCESS;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_JOIN_CONF, 2, &Status);
		}
		// not to me BEACON, ignored
	}
	// sanity check fail, ignore this frame
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
	CHAR          Ssid[MAX_LEN_OF_SSID];
	CF_PARM       CfParm;
	UCHAR         SsidLen, MessageToMe=0, BssType, Channel, NewChannel, index=0;
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
	EDCA_PARM       EdcaParm;
	QBSS_LOAD_PARM  QbssLoad;
	QOS_CAPABILITY_PARM QosCapability;
	ULONG           RalinkIe;
	// New for WPA security suites
	UCHAR						VarIE[MAX_VIE_LEN];		// Total VIE length = MAX_VIE_LEN - -5
	NDIS_802_11_VARIABLE_IEs	*pVIE = NULL;
	HT_CAPABILITY_IE		HtCapability;
	ADD_HT_INFO_IE		AddHtInfo;	// AP might use this additional ht info IE
	UCHAR			HtCapabilityLen, PreNHtCapabilityLen;
	UCHAR			AddHtInfoLen;
	UCHAR			NewExtChannelOffset = 0xff;

	if (!(INFRA_ON(pAd) || ADHOC_ON(pAd)
		))
		return;

	// Init Variable IE structure
	pVIE = (PNDIS_802_11_VARIABLE_IEs) VarIE;
	pVIE->Length = 0;
    RTMPZeroMemory(&HtCapability, sizeof(HtCapability));
	RTMPZeroMemory(&AddHtInfo, sizeof(ADD_HT_INFO_IE));

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
								&EdcaParm,
								&QbssLoad,
								&QosCapability,
								&RalinkIe,
								&HtCapabilityLen,
								&PreNHtCapabilityLen,
								&HtCapability,
								&AddHtInfoLen,
								&AddHtInfo,
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


		// ignore BEACON not for my SSID
		if ((! is_my_ssid) && (! is_my_bssid))
			return;

		// It means STA waits disassoc completely from this AP, ignores this beacon.
		if (pAd->Mlme.CntlMachine.CurrState == CNTL_WAIT_DISASSOC)
			return;

#ifdef DOT11_N_SUPPORT
		// Copy Control channel for this BSSID.
		if (AddHtInfoLen != 0)
			Channel = AddHtInfo.ControlChan;

		if ((HtCapabilityLen > 0) || (PreNHtCapabilityLen > 0))
			HtCapabilityLen = SIZE_HT_CAP_IE;
#endif // DOT11_N_SUPPORT //

		//
		// Housekeeping "SsidBssTab" table for later-on ROAMing usage.
		//
		Bssidx = BssTableSearch(&pAd->ScanTab, Bssid, Channel);
		if (Bssidx == BSS_NOT_FOUND)
		{
			// discover new AP of this network, create BSS entry
			Bssidx = BssTableSetEntry(pAd, &pAd->ScanTab, Bssid, Ssid, SsidLen, BssType, BeaconPeriod,
						 &CfParm, AtimWin, CapabilityInfo, SupRate, SupRateLen, ExtRate, ExtRateLen,
						&HtCapability, &AddHtInfo,HtCapabilityLen,AddHtInfoLen,NewExtChannelOffset, Channel,
						RealRssi, TimeStamp, CkipFlag, &EdcaParm, &QosCapability,
						&QbssLoad, LenVIE, pVIE);
			if (Bssidx == BSS_NOT_FOUND) // return if BSS table full
				return;

			NdisMoveMemory(pAd->ScanTab.BssEntry[Bssidx].PTSF, &Elem->Msg[24], 4);
			NdisMoveMemory(&pAd->ScanTab.BssEntry[Bssidx].TTSF[0], &Elem->TimeStamp.u.LowPart, 4);
			NdisMoveMemory(&pAd->ScanTab.BssEntry[Bssidx].TTSF[4], &Elem->TimeStamp.u.LowPart, 4);



		}

		if ((pAd->CommonCfg.bIEEE80211H == 1) && (NewChannel != 0) && (Channel != NewChannel))
		{
			// Switching to channel 1 can prevent from rescanning the current channel immediately (by auto reconnection).
			// In addition, clear the MLME queue and the scan table to discard the RX packets and previous scanning results.
			AsicSwitchChannel(pAd, 1, FALSE);
			AsicLockChannel(pAd, 1);
		    LinkDown(pAd, FALSE);
			MlmeQueueInit(&pAd->Mlme.Queue);
			BssTableInit(&pAd->ScanTab);
		    RTMPusecDelay(1000000);		// use delay to prevent STA do reassoc

			// channel sanity check
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

		// if the ssid matched & bssid unmatched, we should select the bssid with large value.
		// This might happened when two STA start at the same time
		if ((! is_my_bssid) && ADHOC_ON(pAd))
		{
			INT	i;

			// Add the safeguard against the mismatch of adhoc wep status
			if (pAd->StaCfg.WepStatus != pAd->ScanTab.BssEntry[Bssidx].WepStatus)
			{
				return;
			}

			// collapse into the ADHOC network which has bigger BSSID value.
			for (i = 0; i < 6; i++)
			{
				if (Bssid[i] > pAd->CommonCfg.Bssid[i])
				{
					DBGPRINT(RT_DEBUG_TRACE, ("SYNC - merge to the IBSS with bigger BSSID=%02x:%02x:%02x:%02x:%02x:%02x\n",
						Bssid[0], Bssid[1], Bssid[2], Bssid[3], Bssid[4], Bssid[5]));
					AsicDisableSync(pAd);
					COPY_MAC_ADDR(pAd->CommonCfg.Bssid, Bssid);
					AsicSetBssid(pAd, pAd->CommonCfg.Bssid);
					MakeIbssBeacon(pAd);        // re-build BEACON frame
					AsicEnableIbssSync(pAd);    // copy BEACON frame to on-chip memory
					is_my_bssid = TRUE;
					break;
				}
				else if (Bssid[i] < pAd->CommonCfg.Bssid[i])
					break;
			}
		}


		NdisGetSystemUpTime(&Now);
		pBss = &pAd->ScanTab.BssEntry[Bssidx];
		pBss->Rssi = RealRssi;       // lastest RSSI
		pBss->LastBeaconRxTime = Now;   // last RX timestamp

		//
		// BEACON from my BSSID - either IBSS or INFRA network
		//
		if (is_my_bssid)
		{
			RXWI_STRUC	RxWI;

			pAd->StaCfg.DtimCount = DtimCount;
			pAd->StaCfg.DtimPeriod = DtimPeriod;
			pAd->StaCfg.LastBeaconRxTime = Now;


			RxWI.RSSI0 = Elem->Rssi0;
			RxWI.RSSI1 = Elem->Rssi1;
			RxWI.RSSI2 = Elem->Rssi2;

			Update_Rssi_Sample(pAd, &pAd->StaCfg.RssiSample, &RxWI);
			if (AironetCellPowerLimit != 0xFF)
			{
				//
				// We get the Cisco (ccx) "TxPower Limit" required
				// Changed to appropriate TxPower Limit for Ciso Compatible Extensions
				//
				ChangeToCellPowerLimit(pAd, AironetCellPowerLimit);
			}
			else
			{
				//
				// AironetCellPowerLimit equal to 0xFF means the Cisco (ccx) "TxPower Limit" not exist.
				// Used the default TX Power Percentage, that set from UI.
				//
				pAd->CommonCfg.TxPowerPercentage = pAd->CommonCfg.TxPowerDefault;
			}

			if (ADHOC_ON(pAd) && (CAP_IS_IBSS_ON(CapabilityInfo)))
			{
				UCHAR			MaxSupportedRateIn500Kbps = 0;
				UCHAR			idx;
				MAC_TABLE_ENTRY *pEntry;

				// supported rates array may not be sorted. sort it and find the maximum rate
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

				// look up the existing table
				pEntry = MacTableLookup(pAd, Addr2);

				// Ad-hoc mode is using MAC address as BA session. So we need to continuously find newly joined adhoc station by receiving beacon.
				// To prevent always check this, we use wcid == RESERVED_WCID to recognize it as newly joined adhoc station.
				if ((ADHOC_ON(pAd) && (Elem->Wcid == RESERVED_WCID)) ||
					(pEntry && ((pEntry->LastBeaconRxTime + ADHOC_ENTRY_BEACON_LOST_TIME) < Now)))
				{
					if (pEntry == NULL)
						// Another adhoc joining, add to our MAC table.
						pEntry = MacTableInsertEntry(pAd, Addr2, BSS0, FALSE);

					if (StaAddMacTableEntry(pAd, pEntry, MaxSupportedRateIn500Kbps, &HtCapability, HtCapabilityLen, CapabilityInfo) == FALSE)
					{
						DBGPRINT(RT_DEBUG_TRACE, ("ADHOC - Add Entry failed.\n"));
						return;
					}

					if (pEntry &&
						(Elem->Wcid == RESERVED_WCID))
					{
						idx = pAd->StaCfg.DefaultKeyId;
						RT28XX_STA_SECURITY_INFO_ADD(pAd, BSS0, idx, pEntry);
					}
				}

				if (pEntry && pEntry->ValidAsCLI)
					pEntry->LastBeaconRxTime = Now;

				// At least another peer in this IBSS, declare MediaState as CONNECTED
				if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
				{
					OPSTATUS_SET_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED);

					pAd->IndicateMediaState = NdisMediaStateConnected;
					RTMP_IndicateMediaState(pAd);
	                pAd->ExtraInfo = GENERAL_LINK_UP;
					AsicSetBssid(pAd, pAd->CommonCfg.Bssid);

					// 2003/03/12 - john
					// Make sure this entry in "ScanTab" table, thus complies to Microsoft's policy that
					// "site survey" result should always include the current connected network.
					//
					Bssidx = BssTableSearch(&pAd->ScanTab, Bssid, Channel);
					if (Bssidx == BSS_NOT_FOUND)
					{
						Bssidx = BssTableSetEntry(pAd, &pAd->ScanTab, Bssid, Ssid, SsidLen, BssType, BeaconPeriod,
									&CfParm, AtimWin, CapabilityInfo, SupRate, SupRateLen, ExtRate, ExtRateLen, &HtCapability,
									&AddHtInfo, HtCapabilityLen, AddHtInfoLen, NewExtChannelOffset, Channel, RealRssi, TimeStamp, 0,
									&EdcaParm, &QosCapability, &QbssLoad, LenVIE, pVIE);
					}
					DBGPRINT(RT_DEBUG_TRACE, ("ADHOC  fOP_STATUS_MEDIA_STATE_CONNECTED.\n"));
				}
			}

			if (INFRA_ON(pAd))
			{
				BOOLEAN bUseShortSlot, bUseBGProtection;

				// decide to use/change to -
				//      1. long slot (20 us) or short slot (9 us) time
				//      2. turn on/off RTS/CTS and/or CTS-to-self protection
				//      3. short preamble

				//bUseShortSlot = pAd->CommonCfg.bUseShortSlotTime && CAP_IS_SHORT_SLOT(CapabilityInfo);
				bUseShortSlot = CAP_IS_SHORT_SLOT(CapabilityInfo);
				if (bUseShortSlot != OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_SHORT_SLOT_INUSED))
					AsicSetSlotTime(pAd, bUseShortSlot);

				bUseBGProtection = (pAd->CommonCfg.UseBGProtection == 1) ||    // always use
								   ((pAd->CommonCfg.UseBGProtection == 0) && ERP_IS_USE_PROTECTION(Erp));

				if (pAd->CommonCfg.Channel > 14) // always no BG protection in A-band. falsely happened when switching A/G band to a dual-band AP
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
				// check Ht protection mode. and adhere to the Non-GF device indication by AP.
				if ((AddHtInfoLen != 0) &&
					((AddHtInfo.AddHtInfo2.OperaionMode != pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode) ||
					(AddHtInfo.AddHtInfo2.NonGfPresent != pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent)))
				{
					pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent = AddHtInfo.AddHtInfo2.NonGfPresent;
					pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode = AddHtInfo.AddHtInfo2.OperaionMode;
					if (pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent == 1)
				{
						AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode, ALLN_SETPROTECT, FALSE, TRUE);
					}
					else
						AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode, ALLN_SETPROTECT, FALSE, FALSE);

					DBGPRINT(RT_DEBUG_TRACE, ("SYNC - AP changed N OperaionMode to %d\n", pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode));
				}
#endif // DOT11_N_SUPPORT //

				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED) &&
					ERP_IS_USE_BARKER_PREAMBLE(Erp))
				{
					MlmeSetTxPreamble(pAd, Rt802_11PreambleLong);
					DBGPRINT(RT_DEBUG_TRACE, ("SYNC - AP forced to use LONG preamble\n"));
				}

				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED)    &&
					(EdcaParm.bValid == TRUE)                          &&
					(EdcaParm.EdcaUpdateCount != pAd->CommonCfg.APEdcaParm.EdcaUpdateCount))
				{
					DBGPRINT(RT_DEBUG_TRACE, ("SYNC - AP change EDCA parameters(from %d to %d)\n",
						pAd->CommonCfg.APEdcaParm.EdcaUpdateCount,
						EdcaParm.EdcaUpdateCount));
					AsicSetEdcaParm(pAd, &EdcaParm);
				}

				// copy QOS related information
				NdisMoveMemory(&pAd->CommonCfg.APQbssLoad, &QbssLoad, sizeof(QBSS_LOAD_PARM));
				NdisMoveMemory(&pAd->CommonCfg.APQosCapability, &QosCapability, sizeof(QOS_CAPABILITY_PARM));
			}

			// only INFRASTRUCTURE mode support power-saving feature
			if ((INFRA_ON(pAd) && (pAd->StaCfg.Psm == PWR_SAVE)) || (pAd->CommonCfg.bAPSDForcePowerSave))
			{
				UCHAR FreeNumber;
				//  1. AP has backlogged unicast-to-me frame, stay AWAKE, send PSPOLL
				//  2. AP has backlogged broadcast/multicast frame and we want those frames, stay AWAKE
				//  3. we have outgoing frames in TxRing or MgmtRing, better stay AWAKE
				//  4. Psm change to PWR_SAVE, but AP not been informed yet, we better stay AWAKE
				//  5. otherwise, put PHY back to sleep to save battery.
				if (MessageToMe)
				{
					if (pAd->CommonCfg.bAPSDCapable && pAd->CommonCfg.APEdcaParm.bAPSDCapable &&
						pAd->CommonCfg.bAPSDAC_BE && pAd->CommonCfg.bAPSDAC_BK && pAd->CommonCfg.bAPSDAC_VI && pAd->CommonCfg.bAPSDAC_VO)
					{
						pAd->CommonCfg.bNeedSendTriggerFrame = TRUE;
					}
					else
						RT28XX_PS_POLL_ENQUEUE(pAd);
				}
				else if (BcastFlag && (DtimCount == 0) && OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_RECEIVE_DTIM))
				{
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
					// TODO: consider scheduled HCCA. might not be proper to use traditional DTIM-based power-saving scheme
					// can we cheat here (i.e. just check MGMT & AC_BE) for better performance?
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
						AsicSleepThenAutoWakeup(pAd, TbttNumToNextWakeUp);
					}
				}
			}
		}
		// not my BSSID, ignore it
	}
	// sanity check fail, ignore this frame
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
#endif // DOT11_N_SUPPORT //
	HEADER_802_11 ProbeRspHdr;
	NDIS_STATUS   NStatus;
	PUCHAR        pOutBuffer = NULL;
	ULONG         FrameLen = 0;
	LARGE_INTEGER FakeTimestamp;
	UCHAR         DsLen = 1, IbssLen = 2;
	UCHAR         LocalErpIe[3] = {IE_ERP, 1, 0};
	BOOLEAN       Privacy;
	USHORT        CapabilityInfo;
	UCHAR		  RSNIe = IE_WPA;

	if (! ADHOC_ON(pAd))
		return;

	if (PeerProbeReqSanity(pAd, Elem->Msg, Elem->MsgLen, Addr2, Ssid, &SsidLen))
	{
		if ((SsidLen == 0) || SSID_EQUAL(Ssid, SsidLen, pAd->CommonCfg.Ssid, pAd->CommonCfg.SsidLen))
		{
			// allocate and send out ProbeRsp frame
			NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  //Get an unused nonpaged memory
			if (NStatus != NDIS_STATUS_SUCCESS)
				return;

			//pAd->StaCfg.AtimWin = 0;  // ??????

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

			// If adhoc secruity is set for WPA-None, append the cipher suite IE
			if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPANone)
			{
				ULONG tmp;
				MakeOutgoingFrame(pOutBuffer + FrameLen,        	&tmp,
						  			1,                              &RSNIe,
						  			1,                            	&pAd->StaCfg.RSNIE_Len,
						  			pAd->StaCfg.RSNIE_Len,      	pAd->StaCfg.RSN_IE,
						  			END_OF_ARGS);
				FrameLen += tmp;
			}
#ifdef DOT11_N_SUPPORT
			if (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED)
			{
				ULONG TmpLen;
				UCHAR	BROADCOM[4] = {0x0, 0x90, 0x4c, 0x33};
				HtLen = sizeof(pAd->CommonCfg.HtCapability);
				AddHtLen = sizeof(pAd->CommonCfg.AddHTInfo);
				NewExtLen = 1;
				//New extension channel offset IE is included in Beacon, Probe Rsp or channel Switch Announcement Frame
				if (pAd->bBroadComHT == TRUE)
				{
					MakeOutgoingFrame(pOutBuffer + FrameLen,            &TmpLen,
								  1,                                &WpaIe,
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
#endif // DOT11_N_SUPPORT //
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
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_JOIN_CONF, 2, &Status);
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
	pAd->MlmeAux.Channel = NextChannel(pAd, pAd->MlmeAux.Channel);

	// Only one channel scanned for CISCO beacon request
	if ((pAd->MlmeAux.ScanType == SCAN_CISCO_ACTIVE) ||
		(pAd->MlmeAux.ScanType == SCAN_CISCO_PASSIVE) ||
		(pAd->MlmeAux.ScanType == SCAN_CISCO_NOISE) ||
		(pAd->MlmeAux.ScanType == SCAN_CISCO_CHANNEL_LOAD))
		pAd->MlmeAux.Channel = 0;

	// this routine will stop if pAd->MlmeAux.Channel == 0
	ScanNextChannel(pAd);
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
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_SCAN_CONF, 2, &Status);
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
	DBGPRINT(RT_DEBUG_TRACE, ("InvalidStateWhenJoin(state=%ld). Reset SYNC machine\n", pAd->Mlme.SyncMachine.CurrState));
	pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_JOIN_CONF, 2, &Status);
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
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_START_CONF, 2, &Status);
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
	if (pAd->StaCfg.WindowsPowerMode == Ndis802_11PowerModeLegacy_PSP)
    	pAd->PsPollFrame.FC.PwrMgmt = PWR_SAVE;
	MiniportMMRequest(pAd, 0, (PUCHAR)&pAd->PsPollFrame, sizeof(PSPOLL_FRAME));
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

	NState = MlmeAllocateMemory(pAd, &pOutBuffer);  //Get an unused nonpaged memory
	if (NState == NDIS_STATUS_SUCCESS)
	{
		MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, BROADCAST_ADDR, BROADCAST_ADDR);

		// this ProbeRequest explicitly specify SSID to reduce unwanted ProbeResponse
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

BOOLEAN ScanRunning(
		IN PRTMP_ADAPTER pAd)
{
	return (pAd->Mlme.SyncMachine.CurrState == SCAN_LISTEN) ? TRUE : FALSE;
}

