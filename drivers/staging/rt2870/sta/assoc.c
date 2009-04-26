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
	assoc.c

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	John		2004-9-3		porting from RT2500
*/
#include "../rt_config.h"

UCHAR	CipherWpaTemplate[] = {
		0xdd, 					// WPA IE
		0x16,					// Length
		0x00, 0x50, 0xf2, 0x01,	// oui
		0x01, 0x00,				// Version
		0x00, 0x50, 0xf2, 0x02,	// Multicast
		0x01, 0x00,				// Number of unicast
		0x00, 0x50, 0xf2, 0x02,	// unicast
		0x01, 0x00,				// number of authentication method
		0x00, 0x50, 0xf2, 0x01	// authentication
		};

UCHAR	CipherWpa2Template[] = {
		0x30,					// RSN IE
		0x14,					// Length
		0x01, 0x00,				// Version
		0x00, 0x0f, 0xac, 0x02,	// group cipher, TKIP
		0x01, 0x00,				// number of pairwise
		0x00, 0x0f, 0xac, 0x02,	// unicast
		0x01, 0x00,				// number of authentication method
		0x00, 0x0f, 0xac, 0x02,	// authentication
		0x00, 0x00,				// RSN capability
		};

UCHAR	Ccx2IeInfo[] = { 0x00, 0x40, 0x96, 0x03, 0x02};

/*
	==========================================================================
	Description:
		association state machine init, including state transition and timer init
	Parameters:
		S - pointer to the association state machine

	IRQL = PASSIVE_LEVEL

	==========================================================================
 */
VOID AssocStateMachineInit(
	IN	PRTMP_ADAPTER	pAd,
	IN  STATE_MACHINE *S,
	OUT STATE_MACHINE_FUNC Trans[])
{
	StateMachineInit(S, Trans, MAX_ASSOC_STATE, MAX_ASSOC_MSG, (STATE_MACHINE_FUNC)Drop, ASSOC_IDLE, ASSOC_MACHINE_BASE);

	// first column
	StateMachineSetAction(S, ASSOC_IDLE, MT2_MLME_ASSOC_REQ, (STATE_MACHINE_FUNC)MlmeAssocReqAction);
	StateMachineSetAction(S, ASSOC_IDLE, MT2_MLME_REASSOC_REQ, (STATE_MACHINE_FUNC)MlmeReassocReqAction);
	StateMachineSetAction(S, ASSOC_IDLE, MT2_MLME_DISASSOC_REQ, (STATE_MACHINE_FUNC)MlmeDisassocReqAction);
	StateMachineSetAction(S, ASSOC_IDLE, MT2_PEER_DISASSOC_REQ, (STATE_MACHINE_FUNC)PeerDisassocAction);

	// second column
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_MLME_ASSOC_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenAssoc);
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_MLME_REASSOC_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenReassoc);
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_MLME_DISASSOC_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenDisassociate);
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_PEER_DISASSOC_REQ, (STATE_MACHINE_FUNC)PeerDisassocAction);
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_PEER_ASSOC_RSP, (STATE_MACHINE_FUNC)PeerAssocRspAction);
	//
	// Patch 3Com AP MOde:3CRWE454G72
	// We send Assoc request frame to this AP, it always send Reassoc Rsp not Associate Rsp.
	//
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_PEER_REASSOC_RSP, (STATE_MACHINE_FUNC)PeerAssocRspAction);
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_ASSOC_TIMEOUT, (STATE_MACHINE_FUNC)AssocTimeoutAction);

	// third column
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_MLME_ASSOC_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenAssoc);
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_MLME_REASSOC_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenReassoc);
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_MLME_DISASSOC_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenDisassociate);
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_PEER_DISASSOC_REQ, (STATE_MACHINE_FUNC)PeerDisassocAction);
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_PEER_REASSOC_RSP, (STATE_MACHINE_FUNC)PeerReassocRspAction);
	//
	// Patch, AP doesn't send Reassociate Rsp frame to Station.
	//
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_PEER_ASSOC_RSP, (STATE_MACHINE_FUNC)PeerReassocRspAction);
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_REASSOC_TIMEOUT, (STATE_MACHINE_FUNC)ReassocTimeoutAction);

	// fourth column
	StateMachineSetAction(S, DISASSOC_WAIT_RSP, MT2_MLME_ASSOC_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenAssoc);
	StateMachineSetAction(S, DISASSOC_WAIT_RSP, MT2_MLME_REASSOC_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenReassoc);
	StateMachineSetAction(S, DISASSOC_WAIT_RSP, MT2_MLME_DISASSOC_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenDisassociate);
	StateMachineSetAction(S, DISASSOC_WAIT_RSP, MT2_PEER_DISASSOC_REQ, (STATE_MACHINE_FUNC)PeerDisassocAction);
	StateMachineSetAction(S, DISASSOC_WAIT_RSP, MT2_DISASSOC_TIMEOUT, (STATE_MACHINE_FUNC)DisassocTimeoutAction);

	// initialize the timer
	RTMPInitTimer(pAd, &pAd->MlmeAux.AssocTimer, GET_TIMER_FUNCTION(AssocTimeout), pAd, FALSE);
	RTMPInitTimer(pAd, &pAd->MlmeAux.ReassocTimer, GET_TIMER_FUNCTION(ReassocTimeout), pAd, FALSE);
	RTMPInitTimer(pAd, &pAd->MlmeAux.DisassocTimer, GET_TIMER_FUNCTION(DisassocTimeout), pAd, FALSE);
}

/*
	==========================================================================
	Description:
		Association timeout procedure. After association timeout, this function
		will be called and it will put a message into the MLME queue
	Parameters:
		Standard timer parameters

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID AssocTimeout(IN PVOID SystemSpecific1,
				 IN PVOID FunctionContext,
				 IN PVOID SystemSpecific2,
				 IN PVOID SystemSpecific3)
{
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *)FunctionContext;

	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

	MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_ASSOC_TIMEOUT, 0, NULL);
	RT28XX_MLME_HANDLER(pAd);
}

/*
	==========================================================================
	Description:
		Reassociation timeout procedure. After reassociation timeout, this
		function will be called and put a message into the MLME queue
	Parameters:
		Standard timer parameters

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID ReassocTimeout(IN PVOID SystemSpecific1,
					IN PVOID FunctionContext,
					IN PVOID SystemSpecific2,
					IN PVOID SystemSpecific3)
{
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *)FunctionContext;

	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

	MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_REASSOC_TIMEOUT, 0, NULL);
	RT28XX_MLME_HANDLER(pAd);
}

/*
	==========================================================================
	Description:
		Disassociation timeout procedure. After disassociation timeout, this
		function will be called and put a message into the MLME queue
	Parameters:
		Standard timer parameters

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID DisassocTimeout(IN PVOID SystemSpecific1,
					IN PVOID FunctionContext,
					IN PVOID SystemSpecific2,
					IN PVOID SystemSpecific3)
{
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *)FunctionContext;

	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

	MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_DISASSOC_TIMEOUT, 0, NULL);
	RT28XX_MLME_HANDLER(pAd);
}

/*
	==========================================================================
	Description:
		mlme assoc req handling procedure
	Parameters:
		Adapter - Adapter pointer
		Elem - MLME Queue Element
	Pre:
		the station has been authenticated and the following information is stored in the config
			-# SSID
			-# supported rates and their length
			-# listen interval (Adapter->StaCfg.default_listen_count)
			-# Transmit power  (Adapter->StaCfg.tx_power)
	Post  :
		-# An association request frame is generated and sent to the air
		-# Association timer starts
		-# Association state -> ASSOC_WAIT_RSP

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID MlmeAssocReqAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	UCHAR			ApAddr[6];
	HEADER_802_11	AssocHdr;
	UCHAR			Ccx2Len = 5;
	UCHAR			WmeIe[9] = {IE_VENDOR_SPECIFIC, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};
	USHORT			ListenIntv;
	ULONG			Timeout;
	USHORT			CapabilityInfo;
	BOOLEAN			TimerCancelled;
	PUCHAR			pOutBuffer = NULL;
	NDIS_STATUS		NStatus;
	ULONG			FrameLen = 0;
	ULONG			tmp;
	USHORT			VarIesOffset;
	UCHAR			CkipFlag;
	UCHAR			CkipNegotiationBuffer[CKIP_NEGOTIATION_LENGTH];
	UCHAR			AironetCkipIe = IE_AIRONET_CKIP;
	UCHAR			AironetCkipLen = CKIP_NEGOTIATION_LENGTH;
	UCHAR			AironetIPAddressIE = IE_AIRONET_IPADDRESS;
	UCHAR			AironetIPAddressLen = AIRONET_IPADDRESS_LENGTH;
	UCHAR			AironetIPAddressBuffer[AIRONET_IPADDRESS_LENGTH] = {0x00, 0x40, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00};
	USHORT			Status;

	// Block all authentication request durning WPA block period
	if (pAd->StaCfg.bBlockAssoc == TRUE)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - Block Assoc request durning WPA block period!\n"));
		pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
		Status = MLME_STATE_MACHINE_REJECT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_ASSOC_CONF, 2, &Status);
	}
	// check sanity first
	else if (MlmeAssocReqSanity(pAd, Elem->Msg, Elem->MsgLen, ApAddr, &CapabilityInfo, &Timeout, &ListenIntv))
	{
		RTMPCancelTimer(&pAd->MlmeAux.AssocTimer, &TimerCancelled);
		COPY_MAC_ADDR(pAd->MlmeAux.Bssid, ApAddr);

		// Get an unused nonpaged memory
		NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);
		if (NStatus != NDIS_STATUS_SUCCESS)
		{
			DBGPRINT(RT_DEBUG_TRACE,("ASSOC - MlmeAssocReqAction() allocate memory failed \n"));
			pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
			Status = MLME_FAIL_NO_RESOURCE;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_ASSOC_CONF, 2, &Status);
			return;
		}

		// Add by James 03/06/27
		pAd->StaCfg.AssocInfo.Length = sizeof(NDIS_802_11_ASSOCIATION_INFORMATION);
		// Association don't need to report MAC address
		pAd->StaCfg.AssocInfo.AvailableRequestFixedIEs =
			NDIS_802_11_AI_REQFI_CAPABILITIES | NDIS_802_11_AI_REQFI_LISTENINTERVAL;
		pAd->StaCfg.AssocInfo.RequestFixedIEs.Capabilities = CapabilityInfo;
		pAd->StaCfg.AssocInfo.RequestFixedIEs.ListenInterval = ListenIntv;
		// Only reassociate need this
		//COPY_MAC_ADDR(pAd->StaCfg.AssocInfo.RequestFixedIEs.CurrentAPAddress, ApAddr);
		pAd->StaCfg.AssocInfo.OffsetRequestIEs = sizeof(NDIS_802_11_ASSOCIATION_INFORMATION);

        NdisZeroMemory(pAd->StaCfg.ReqVarIEs, MAX_VIE_LEN);
		// First add SSID
		VarIesOffset = 0;
		NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset, &SsidIe, 1);
		VarIesOffset += 1;
		NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset, &pAd->MlmeAux.SsidLen, 1);
		VarIesOffset += 1;
		NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset, pAd->MlmeAux.Ssid, pAd->MlmeAux.SsidLen);
		VarIesOffset += pAd->MlmeAux.SsidLen;

		// Second add Supported rates
		NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset, &SupRateIe, 1);
		VarIesOffset += 1;
		NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset, &pAd->MlmeAux.SupRateLen, 1);
		VarIesOffset += 1;
		NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset, pAd->MlmeAux.SupRate, pAd->MlmeAux.SupRateLen);
		VarIesOffset += pAd->MlmeAux.SupRateLen;
		// End Add by James

        if ((pAd->CommonCfg.Channel > 14) &&
            (pAd->CommonCfg.bIEEE80211H == TRUE))
            CapabilityInfo |= 0x0100;

		DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - Send ASSOC request...\n"));
		MgtMacHeaderInit(pAd, &AssocHdr, SUBTYPE_ASSOC_REQ, 0, ApAddr, ApAddr);

		// Build basic frame first
		MakeOutgoingFrame(pOutBuffer,				&FrameLen,
						  sizeof(HEADER_802_11),	&AssocHdr,
						  2,						&CapabilityInfo,
						  2,						&ListenIntv,
						  1,						&SsidIe,
						  1,						&pAd->MlmeAux.SsidLen,
						  pAd->MlmeAux.SsidLen, 	pAd->MlmeAux.Ssid,
						  1,						&SupRateIe,
						  1,						&pAd->MlmeAux.SupRateLen,
						  pAd->MlmeAux.SupRateLen,  pAd->MlmeAux.SupRate,
						  END_OF_ARGS);

		if (pAd->MlmeAux.ExtRateLen != 0)
		{
			MakeOutgoingFrame(pOutBuffer + FrameLen,    &tmp,
							  1,                        &ExtRateIe,
							  1,                        &pAd->MlmeAux.ExtRateLen,
							  pAd->MlmeAux.ExtRateLen,  pAd->MlmeAux.ExtRate,
							  END_OF_ARGS);
			FrameLen += tmp;
		}

#ifdef DOT11_N_SUPPORT
		// HT
		if ((pAd->MlmeAux.HtCapabilityLen > 0) && (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED))
		{
			ULONG TmpLen;
			UCHAR HtLen;
			UCHAR BROADCOM[4] = {0x0, 0x90, 0x4c, 0x33};
			if (pAd->StaActive.SupportedPhyInfo.bPreNHt == TRUE)
			{
				HtLen = SIZE_HT_CAP_IE + 4;
				MakeOutgoingFrame(pOutBuffer + FrameLen,            &TmpLen,
							  1,                                &WpaIe,
							  1,                                &HtLen,
							  4,                                &BROADCOM[0],
							 pAd->MlmeAux.HtCapabilityLen,          &pAd->MlmeAux.HtCapability,
							  END_OF_ARGS);
			}
			else
			{
				MakeOutgoingFrame(pOutBuffer + FrameLen,            &TmpLen,
							  1,                                &HtCapIe,
							  1,                                &pAd->MlmeAux.HtCapabilityLen,
							 pAd->MlmeAux.HtCapabilityLen,          &pAd->MlmeAux.HtCapability,
							  END_OF_ARGS);
			}
			FrameLen += TmpLen;
		}
#endif // DOT11_N_SUPPORT //

		// add Ralink proprietary IE to inform AP this STA is going to use AGGREGATION or PIGGY-BACK+AGGREGATION
		// Case I: (Aggregation + Piggy-Back)
		// 1. user enable aggregation, AND
		// 2. Mac support piggy-back
		// 3. AP annouces it's PIGGY-BACK+AGGREGATION-capable in BEACON
		// Case II: (Aggregation)
		// 1. user enable aggregation, AND
		// 2. AP annouces it's AGGREGATION-capable in BEACON
		if (pAd->CommonCfg.bAggregationCapable)
		{
			if ((pAd->CommonCfg.bPiggyBackCapable) && ((pAd->MlmeAux.APRalinkIe & 0x00000003) == 3))
			{
				ULONG TmpLen;
				UCHAR RalinkIe[9] = {IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43, 0x03, 0x00, 0x00, 0x00};
				MakeOutgoingFrame(pOutBuffer+FrameLen,           &TmpLen,
								  9,                             RalinkIe,
								  END_OF_ARGS);
				FrameLen += TmpLen;
			}
			else if (pAd->MlmeAux.APRalinkIe & 0x00000001)
			{
				ULONG TmpLen;
				UCHAR RalinkIe[9] = {IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43, 0x01, 0x00, 0x00, 0x00};
				MakeOutgoingFrame(pOutBuffer+FrameLen,           &TmpLen,
								  9,                             RalinkIe,
								  END_OF_ARGS);
				FrameLen += TmpLen;
			}
		}
		else
		{
			ULONG TmpLen;
			UCHAR RalinkIe[9] = {IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43, 0x06, 0x00, 0x00, 0x00};
			MakeOutgoingFrame(pOutBuffer+FrameLen,		 &TmpLen,
							  9,						 RalinkIe,
							  END_OF_ARGS);
			FrameLen += TmpLen;
		}

		if (pAd->MlmeAux.APEdcaParm.bValid)
		{
			if (pAd->CommonCfg.bAPSDCapable && pAd->MlmeAux.APEdcaParm.bAPSDCapable)
			{
				QBSS_STA_INFO_PARM QosInfo;

				NdisZeroMemory(&QosInfo, sizeof(QBSS_STA_INFO_PARM));
				QosInfo.UAPSD_AC_BE = pAd->CommonCfg.bAPSDAC_BE;
				QosInfo.UAPSD_AC_BK = pAd->CommonCfg.bAPSDAC_BK;
				QosInfo.UAPSD_AC_VI = pAd->CommonCfg.bAPSDAC_VI;
				QosInfo.UAPSD_AC_VO = pAd->CommonCfg.bAPSDAC_VO;
				QosInfo.MaxSPLength = pAd->CommonCfg.MaxSPLength;
				WmeIe[8] |= *(PUCHAR)&QosInfo;
			}
			else
			{
                // The Parameter Set Count is set to ¡§0¡¨ in the association request frames
                // WmeIe[8] |= (pAd->MlmeAux.APEdcaParm.EdcaUpdateCount & 0x0f);
			}

			MakeOutgoingFrame(pOutBuffer + FrameLen,    &tmp,
							  9,                        &WmeIe[0],
							  END_OF_ARGS);
			FrameLen += tmp;
		}

		//
		// Let WPA(#221) Element ID on the end of this association frame.
		// Otherwise some AP will fail on parsing Element ID and set status fail on Assoc Rsp.
		// For example: Put Vendor Specific IE on the front of WPA IE.
		// This happens on AP (Model No:Linksys WRK54G)
		//
		if (((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPAPSK) ||
            (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK) ||
            (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA) ||
            (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2)
			)
            )
		{
			UCHAR RSNIe = IE_WPA;

			if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK) ||
                (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2))
			{
				RSNIe = IE_WPA2;
			}

            	RTMPMakeRSNIE(pAd, pAd->StaCfg.AuthMode, pAd->StaCfg.WepStatus, BSS0);

            // Check for WPA PMK cache list
			if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2)
			{
			    INT     idx;
                BOOLEAN FoundPMK = FALSE;
				// Search chched PMKID, append it if existed
				for (idx = 0; idx < PMKID_NO; idx++)
				{
					if (NdisEqualMemory(ApAddr, &pAd->StaCfg.SavedPMK[idx].BSSID, 6))
					{
						FoundPMK = TRUE;
						break;
					}
				}

				if (FoundPMK)
				{
					// Set PMK number
					*(PUSHORT) &pAd->StaCfg.RSN_IE[pAd->StaCfg.RSNIE_Len] = 1;
					NdisMoveMemory(&pAd->StaCfg.RSN_IE[pAd->StaCfg.RSNIE_Len + 2], &pAd->StaCfg.SavedPMK[idx].PMKID, 16);
                    pAd->StaCfg.RSNIE_Len += 18;
				}
			}

			{
				MakeOutgoingFrame(pOutBuffer + FrameLen,    		&tmp,
				              		1,                              &RSNIe,
		                        	1,                              &pAd->StaCfg.RSNIE_Len,
		                        	pAd->StaCfg.RSNIE_Len,			pAd->StaCfg.RSN_IE,
		                        	END_OF_ARGS);
			}

			FrameLen += tmp;

			{
	            // Append Variable IE
	            NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset, &RSNIe, 1);
	            VarIesOffset += 1;
	            NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset, &pAd->StaCfg.RSNIE_Len, 1);
	            VarIesOffset += 1;
			}
			NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset, pAd->StaCfg.RSN_IE, pAd->StaCfg.RSNIE_Len);
			VarIesOffset += pAd->StaCfg.RSNIE_Len;

			// Set Variable IEs Length
			pAd->StaCfg.ReqVarIELen = VarIesOffset;
		}

		// We have update that at PeerBeaconAtJoinRequest()
		CkipFlag = pAd->StaCfg.CkipFlag;
		if (CkipFlag != 0)
		{
			NdisZeroMemory(CkipNegotiationBuffer, CKIP_NEGOTIATION_LENGTH);
			CkipNegotiationBuffer[2] = 0x66;
			// Make it try KP & MIC, since we have to follow the result from AssocRsp
			CkipNegotiationBuffer[8] = 0x18;
			CkipNegotiationBuffer[CKIP_NEGOTIATION_LENGTH - 1] = 0x22;
			CkipFlag = 0x18;

			MakeOutgoingFrame(pOutBuffer + FrameLen, 	&tmp,
						1,						  		&AironetCkipIe,
						1,						  		&AironetCkipLen,
						AironetCkipLen, 		  		CkipNegotiationBuffer,
						END_OF_ARGS);
			FrameLen += tmp;
		}

		// Add CCX v2 request if CCX2 admin state is on
		if (pAd->StaCfg.CCXControl.field.Enable == 1)
		{

			//
			// Add AironetIPAddressIE for Cisco CCX 2.X
			// Add CCX Version
			//
			MakeOutgoingFrame(pOutBuffer + FrameLen, &tmp,
						1,							&AironetIPAddressIE,
						1,							&AironetIPAddressLen,
						AironetIPAddressLen,		AironetIPAddressBuffer,
						1,							&Ccx2Ie,
						1,							&Ccx2Len,
						Ccx2Len,				    Ccx2IeInfo,
						END_OF_ARGS);
			FrameLen += tmp;

			// Add by James 03/06/27
			// Set Variable IEs Length
			pAd->StaCfg.ReqVarIELen = VarIesOffset;
			pAd->StaCfg.AssocInfo.RequestIELength = VarIesOffset;

			// OffsetResponseIEs follow ReqVarIE
			pAd->StaCfg.AssocInfo.OffsetResponseIEs = sizeof(NDIS_802_11_ASSOCIATION_INFORMATION) + pAd->StaCfg.ReqVarIELen;
			// End Add by James
		}


		MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
		MlmeFreeMemory(pAd, pOutBuffer);

		RTMPSetTimer(&pAd->MlmeAux.AssocTimer, Timeout);
		pAd->Mlme.AssocMachine.CurrState = ASSOC_WAIT_RSP;
	}
	else
	{
		DBGPRINT(RT_DEBUG_TRACE,("ASSOC - MlmeAssocReqAction() sanity check failed. BUG!!!!!! \n"));
		pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_ASSOC_CONF, 2, &Status);
	}

}

/*
	==========================================================================
	Description:
		mlme reassoc req handling procedure
	Parameters:
		Elem -
	Pre:
		-# SSID  (Adapter->StaCfg.ssid[])
		-# BSSID (AP address, Adapter->StaCfg.bssid)
		-# Supported rates (Adapter->StaCfg.supported_rates[])
		-# Supported rates length (Adapter->StaCfg.supported_rates_len)
		-# Tx power (Adapter->StaCfg.tx_power)

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID MlmeReassocReqAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	UCHAR			ApAddr[6];
	HEADER_802_11	ReassocHdr;
	UCHAR			Ccx2Len = 5;
	UCHAR			WmeIe[9] = {IE_VENDOR_SPECIFIC, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};
	USHORT			CapabilityInfo, ListenIntv;
	ULONG			Timeout;
	ULONG			FrameLen = 0;
	BOOLEAN			TimerCancelled;
	NDIS_STATUS		NStatus;
	ULONG			tmp;
	PUCHAR			pOutBuffer = NULL;
	USHORT			Status;

	// Block all authentication request durning WPA block period
	if (pAd->StaCfg.bBlockAssoc == TRUE)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - Block ReAssoc request durning WPA block period!\n"));
		pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
		Status = MLME_STATE_MACHINE_REJECT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_REASSOC_CONF, 2, &Status);
	}
	// the parameters are the same as the association
	else if(MlmeAssocReqSanity(pAd, Elem->Msg, Elem->MsgLen, ApAddr, &CapabilityInfo, &Timeout, &ListenIntv))
	{
		RTMPCancelTimer(&pAd->MlmeAux.ReassocTimer, &TimerCancelled);

		NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  //Get an unused nonpaged memory
		if(NStatus != NDIS_STATUS_SUCCESS)
		{
			DBGPRINT(RT_DEBUG_TRACE,("ASSOC - MlmeReassocReqAction() allocate memory failed \n"));
			pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
			Status = MLME_FAIL_NO_RESOURCE;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_REASSOC_CONF, 2, &Status);
			return;
		}

		COPY_MAC_ADDR(pAd->MlmeAux.Bssid, ApAddr);

		// make frame, use bssid as the AP address??
		DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - Send RE-ASSOC request...\n"));
		MgtMacHeaderInit(pAd, &ReassocHdr, SUBTYPE_REASSOC_REQ, 0, ApAddr, ApAddr);
		MakeOutgoingFrame(pOutBuffer,               &FrameLen,
						  sizeof(HEADER_802_11),    &ReassocHdr,
						  2,                        &CapabilityInfo,
						  2,                        &ListenIntv,
						  MAC_ADDR_LEN,             ApAddr,
						  1,                        &SsidIe,
						  1,                        &pAd->MlmeAux.SsidLen,
						  pAd->MlmeAux.SsidLen,     pAd->MlmeAux.Ssid,
						  1,                        &SupRateIe,
						  1,						&pAd->MlmeAux.SupRateLen,
						  pAd->MlmeAux.SupRateLen,  pAd->MlmeAux.SupRate,
						  END_OF_ARGS);

		if (pAd->MlmeAux.ExtRateLen != 0)
		{
			MakeOutgoingFrame(pOutBuffer + FrameLen,        &tmp,
							  1,                            &ExtRateIe,
							  1,                            &pAd->MlmeAux.ExtRateLen,
							  pAd->MlmeAux.ExtRateLen,	    pAd->MlmeAux.ExtRate,
							  END_OF_ARGS);
			FrameLen += tmp;
		}

		if (pAd->MlmeAux.APEdcaParm.bValid)
		{
			if (pAd->CommonCfg.bAPSDCapable && pAd->MlmeAux.APEdcaParm.bAPSDCapable)
			{
				QBSS_STA_INFO_PARM QosInfo;

				NdisZeroMemory(&QosInfo, sizeof(QBSS_STA_INFO_PARM));
				QosInfo.UAPSD_AC_BE = pAd->CommonCfg.bAPSDAC_BE;
				QosInfo.UAPSD_AC_BK = pAd->CommonCfg.bAPSDAC_BK;
				QosInfo.UAPSD_AC_VI = pAd->CommonCfg.bAPSDAC_VI;
				QosInfo.UAPSD_AC_VO = pAd->CommonCfg.bAPSDAC_VO;
				QosInfo.MaxSPLength = pAd->CommonCfg.MaxSPLength;
				WmeIe[8] |= *(PUCHAR)&QosInfo;
			}

			MakeOutgoingFrame(pOutBuffer + FrameLen,    &tmp,
							  9,                        &WmeIe[0],
							  END_OF_ARGS);
			FrameLen += tmp;
		}

#ifdef DOT11_N_SUPPORT
		// HT
		if ((pAd->MlmeAux.HtCapabilityLen > 0) && (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED))
		{
			ULONG TmpLen;
			UCHAR HtLen;
			UCHAR BROADCOM[4] = {0x0, 0x90, 0x4c, 0x33};
			if (pAd->StaActive.SupportedPhyInfo.bPreNHt == TRUE)
			{
				HtLen = SIZE_HT_CAP_IE + 4;
				MakeOutgoingFrame(pOutBuffer + FrameLen,            &TmpLen,
							  1,                                &WpaIe,
							  1,                                &HtLen,
							  4,                                &BROADCOM[0],
							 pAd->MlmeAux.HtCapabilityLen,          &pAd->MlmeAux.HtCapability,
							  END_OF_ARGS);
			}
			else
			{
				MakeOutgoingFrame(pOutBuffer + FrameLen,            &TmpLen,
							  1,                                &HtCapIe,
							  1,                                &pAd->MlmeAux.HtCapabilityLen,
							 pAd->MlmeAux.HtCapabilityLen,          &pAd->MlmeAux.HtCapability,
							  END_OF_ARGS);
			}
			FrameLen += TmpLen;
		}
#endif // DOT11_N_SUPPORT //

		// add Ralink proprietary IE to inform AP this STA is going to use AGGREGATION or PIGGY-BACK+AGGREGATION
		// Case I: (Aggregation + Piggy-Back)
		// 1. user enable aggregation, AND
		// 2. Mac support piggy-back
		// 3. AP annouces it's PIGGY-BACK+AGGREGATION-capable in BEACON
		// Case II: (Aggregation)
		// 1. user enable aggregation, AND
		// 2. AP annouces it's AGGREGATION-capable in BEACON
		if (pAd->CommonCfg.bAggregationCapable)
		{
			if ((pAd->CommonCfg.bPiggyBackCapable) && ((pAd->MlmeAux.APRalinkIe & 0x00000003) == 3))
			{
				ULONG TmpLen;
				UCHAR RalinkIe[9] = {IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43, 0x03, 0x00, 0x00, 0x00};
				MakeOutgoingFrame(pOutBuffer+FrameLen,           &TmpLen,
								  9,                             RalinkIe,
								  END_OF_ARGS);
				FrameLen += TmpLen;
			}
			else if (pAd->MlmeAux.APRalinkIe & 0x00000001)
			{
				ULONG TmpLen;
				UCHAR RalinkIe[9] = {IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43, 0x01, 0x00, 0x00, 0x00};
				MakeOutgoingFrame(pOutBuffer+FrameLen,           &TmpLen,
								  9,                             RalinkIe,
								  END_OF_ARGS);
				FrameLen += TmpLen;
			}
		}
		else
		{
			ULONG TmpLen;
			UCHAR RalinkIe[9] = {IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43, 0x04, 0x00, 0x00, 0x00};
			MakeOutgoingFrame(pOutBuffer+FrameLen,		 &TmpLen,
							  9,						 RalinkIe,
							  END_OF_ARGS);
			FrameLen += TmpLen;
		}

		// Add CCX v2 request if CCX2 admin state is on
		if (pAd->StaCfg.CCXControl.field.Enable == 1)
		{
			//
			// Add CCX Version
			//
			MakeOutgoingFrame(pOutBuffer + FrameLen, &tmp,
						1,							&Ccx2Ie,
						1,							&Ccx2Len,
						Ccx2Len,				    Ccx2IeInfo,
						END_OF_ARGS);
			FrameLen += tmp;
		}

		MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
		MlmeFreeMemory(pAd, pOutBuffer);

		RTMPSetTimer(&pAd->MlmeAux.ReassocTimer, Timeout); /* in mSec */
		pAd->Mlme.AssocMachine.CurrState = REASSOC_WAIT_RSP;
	}
	else
	{
		DBGPRINT(RT_DEBUG_TRACE,("ASSOC - MlmeReassocReqAction() sanity check failed. BUG!!!! \n"));
		pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_REASSOC_CONF, 2, &Status);
	}
}

/*
	==========================================================================
	Description:
		Upper layer issues disassoc request
	Parameters:
		Elem -

	IRQL = PASSIVE_LEVEL

	==========================================================================
 */
VOID MlmeDisassocReqAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	PMLME_DISASSOC_REQ_STRUCT pDisassocReq;
	HEADER_802_11         DisassocHdr;
	PHEADER_802_11        pDisassocHdr;
	PUCHAR                pOutBuffer = NULL;
	ULONG                 FrameLen = 0;
	NDIS_STATUS           NStatus;
	BOOLEAN               TimerCancelled;
	ULONG                 Timeout = 0;
	USHORT                Status;

	// skip sanity check
	pDisassocReq = (PMLME_DISASSOC_REQ_STRUCT)(Elem->Msg);

	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  //Get an unused nonpaged memory
	if (NStatus != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - MlmeDisassocReqAction() allocate memory failed\n"));
		pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
		Status = MLME_FAIL_NO_RESOURCE;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_DISASSOC_CONF, 2, &Status);
		return;
	}



	RTMPCancelTimer(&pAd->MlmeAux.DisassocTimer, &TimerCancelled);

	DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - Send DISASSOC request[BSSID::%02x:%02x:%02x:%02x:%02x:%02x (Reason=%d)\n",
				pDisassocReq->Addr[0], pDisassocReq->Addr[1], pDisassocReq->Addr[2],
				pDisassocReq->Addr[3], pDisassocReq->Addr[4], pDisassocReq->Addr[5], pDisassocReq->Reason));
	MgtMacHeaderInit(pAd, &DisassocHdr, SUBTYPE_DISASSOC, 0, pDisassocReq->Addr, pDisassocReq->Addr);	// patch peap ttls switching issue
	MakeOutgoingFrame(pOutBuffer,           &FrameLen,
					  sizeof(HEADER_802_11),&DisassocHdr,
					  2,                    &pDisassocReq->Reason,
					  END_OF_ARGS);
	MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);

	// To patch Instance and Buffalo(N) AP
	// Driver has to send deauth to Instance AP, but Buffalo(N) needs to send disassoc to reset Authenticator's state machine
	// Therefore, we send both of them.
	pDisassocHdr = (PHEADER_802_11)pOutBuffer;
	pDisassocHdr->FC.SubType = SUBTYPE_DEAUTH;
	MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);

	MlmeFreeMemory(pAd, pOutBuffer);

	pAd->StaCfg.DisassocReason = REASON_DISASSOC_STA_LEAVING;
	COPY_MAC_ADDR(pAd->StaCfg.DisassocSta, pDisassocReq->Addr);

	RTMPSetTimer(&pAd->MlmeAux.DisassocTimer, Timeout); /* in mSec */
	pAd->Mlme.AssocMachine.CurrState = DISASSOC_WAIT_RSP;

    {
        union iwreq_data    wrqu;
        memset(wrqu.ap_addr.sa_data, 0, MAC_ADDR_LEN);
        wireless_send_event(pAd->net_dev, SIOCGIWAP, &wrqu, NULL);
    }
}

/*
	==========================================================================
	Description:
		peer sends assoc rsp back
	Parameters:
		Elme - MLME message containing the received frame

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID PeerAssocRspAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT        CapabilityInfo, Status, Aid;
	UCHAR         SupRate[MAX_LEN_OF_SUPPORTED_RATES], SupRateLen;
	UCHAR         ExtRate[MAX_LEN_OF_SUPPORTED_RATES], ExtRateLen;
	UCHAR         Addr2[MAC_ADDR_LEN];
	BOOLEAN       TimerCancelled;
	UCHAR         CkipFlag;
	EDCA_PARM     EdcaParm;
	HT_CAPABILITY_IE		HtCapability;
	ADD_HT_INFO_IE		AddHtInfo;	// AP might use this additional ht info IE
	UCHAR			HtCapabilityLen;
	UCHAR			AddHtInfoLen;
	UCHAR			NewExtChannelOffset = 0xff;

	if (PeerAssocRspSanity(pAd, Elem->Msg, Elem->MsgLen, Addr2, &CapabilityInfo, &Status, &Aid, SupRate, &SupRateLen, ExtRate, &ExtRateLen,
		&HtCapability,&AddHtInfo, &HtCapabilityLen,&AddHtInfoLen,&NewExtChannelOffset, &EdcaParm, &CkipFlag))
	{
		// The frame is for me ?
		if(MAC_ADDR_EQUAL(Addr2, pAd->MlmeAux.Bssid))
		{
			DBGPRINT(RT_DEBUG_TRACE, ("PeerAssocRspAction():ASSOC - receive ASSOC_RSP to me (status=%d)\n", Status));
#ifdef DOT11_N_SUPPORT
			DBGPRINT(RT_DEBUG_TRACE, ("PeerAssocRspAction():MacTable [%d].AMsduSize = %d. ClientStatusFlags = 0x%lx \n",Elem->Wcid, pAd->MacTab.Content[BSSID_WCID].AMsduSize, pAd->MacTab.Content[BSSID_WCID].ClientStatusFlags));
#endif // DOT11_N_SUPPORT //
			RTMPCancelTimer(&pAd->MlmeAux.AssocTimer, &TimerCancelled);
			if(Status == MLME_SUCCESS)
			{
				UCHAR			MaxSupportedRateIn500Kbps = 0;
				UCHAR			idx;

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
				// go to procedure listed on page 376
				AssocPostProc(pAd, Addr2, CapabilityInfo, Aid, SupRate, SupRateLen, ExtRate, ExtRateLen,
					&EdcaParm, &HtCapability, HtCapabilityLen, &AddHtInfo);

				StaAddMacTableEntry(pAd, &pAd->MacTab.Content[BSSID_WCID], MaxSupportedRateIn500Kbps, &HtCapability, HtCapabilityLen, CapabilityInfo);

				pAd->StaCfg.CkipFlag = CkipFlag;
				if (CkipFlag & 0x18)
				{
					NdisZeroMemory(pAd->StaCfg.TxSEQ, 4);
					NdisZeroMemory(pAd->StaCfg.RxSEQ, 4);
					NdisZeroMemory(pAd->StaCfg.CKIPMIC, 4);
					pAd->StaCfg.GIV[0] = RandomByte(pAd);
					pAd->StaCfg.GIV[1] = RandomByte(pAd);
					pAd->StaCfg.GIV[2] = RandomByte(pAd);
					pAd->StaCfg.bCkipOn = TRUE;
					DBGPRINT(RT_DEBUG_TRACE, ("<CCX> pAd->StaCfg.CkipFlag = 0x%02x\n", pAd->StaCfg.CkipFlag));
				}
			}
			else
			{
			}
			pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_ASSOC_CONF, 2, &Status);
		}
	}
	else
	{
		DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - PeerAssocRspAction() sanity check fail\n"));
	}
}

/*
	==========================================================================
	Description:
		peer sends reassoc rsp
	Parametrs:
		Elem - MLME message cntaining the received frame

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID PeerReassocRspAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT      CapabilityInfo;
	USHORT      Status;
	USHORT      Aid;
	UCHAR       SupRate[MAX_LEN_OF_SUPPORTED_RATES], SupRateLen;
	UCHAR       ExtRate[MAX_LEN_OF_SUPPORTED_RATES], ExtRateLen;
	UCHAR       Addr2[MAC_ADDR_LEN];
	UCHAR       CkipFlag;
	BOOLEAN     TimerCancelled;
	EDCA_PARM   EdcaParm;
	HT_CAPABILITY_IE		HtCapability;
	ADD_HT_INFO_IE		AddHtInfo;	// AP might use this additional ht info IE
	UCHAR			HtCapabilityLen;
	UCHAR			AddHtInfoLen;
	UCHAR			NewExtChannelOffset = 0xff;

	if(PeerAssocRspSanity(pAd, Elem->Msg, Elem->MsgLen, Addr2, &CapabilityInfo, &Status, &Aid, SupRate, &SupRateLen, ExtRate, &ExtRateLen,
								&HtCapability,	&AddHtInfo, &HtCapabilityLen, &AddHtInfoLen,&NewExtChannelOffset, &EdcaParm, &CkipFlag))
	{
		if(MAC_ADDR_EQUAL(Addr2, pAd->MlmeAux.Bssid)) // The frame is for me ?
		{
			DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - receive REASSOC_RSP to me (status=%d)\n", Status));
			RTMPCancelTimer(&pAd->MlmeAux.ReassocTimer, &TimerCancelled);

			if(Status == MLME_SUCCESS)
			{
				// go to procedure listed on page 376
				AssocPostProc(pAd, Addr2, CapabilityInfo, Aid, SupRate, SupRateLen, ExtRate, ExtRateLen,
					 &EdcaParm, &HtCapability, HtCapabilityLen, &AddHtInfo);

                {
                    union iwreq_data    wrqu;
                    wext_notify_event_assoc(pAd);

                    memset(wrqu.ap_addr.sa_data, 0, MAC_ADDR_LEN);
                    memcpy(wrqu.ap_addr.sa_data, pAd->MlmeAux.Bssid, MAC_ADDR_LEN);
                    wireless_send_event(pAd->net_dev, SIOCGIWAP, &wrqu, NULL);

                }

			}

			{
				// CkipFlag is no use for reassociate
				pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
				MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_REASSOC_CONF, 2, &Status);
			}
		}
	}
	else
	{
		DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - PeerReassocRspAction() sanity check fail\n"));
	}

}

/*
	==========================================================================
	Description:
		procedures on IEEE 802.11/1999 p.376
	Parametrs:

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID AssocPostProc(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pAddr2,
	IN USHORT CapabilityInfo,
	IN USHORT Aid,
	IN UCHAR SupRate[],
	IN UCHAR SupRateLen,
	IN UCHAR ExtRate[],
	IN UCHAR ExtRateLen,
	IN PEDCA_PARM pEdcaParm,
	IN HT_CAPABILITY_IE		*pHtCapability,
	IN UCHAR HtCapabilityLen,
	IN ADD_HT_INFO_IE		*pAddHtInfo)	// AP might use this additional ht info IE
{
	ULONG Idx;

	pAd->MlmeAux.BssType = BSS_INFRA;
	COPY_MAC_ADDR(pAd->MlmeAux.Bssid, pAddr2);
	pAd->MlmeAux.Aid = Aid;
	pAd->MlmeAux.CapabilityInfo = CapabilityInfo & SUPPORTED_CAPABILITY_INFO;
#ifdef DOT11_N_SUPPORT
	// Some HT AP might lost WMM IE. We add WMM ourselves. beacuase HT requires QoS on.
	if ((HtCapabilityLen > 0) && (pEdcaParm->bValid == FALSE))
	{
		pEdcaParm->bValid = TRUE;
		pEdcaParm->Aifsn[0] = 3;
		pEdcaParm->Aifsn[1] = 7;
		pEdcaParm->Aifsn[2] = 2;
		pEdcaParm->Aifsn[3] = 2;

		pEdcaParm->Cwmin[0] = 4;
		pEdcaParm->Cwmin[1] = 4;
		pEdcaParm->Cwmin[2] = 3;
		pEdcaParm->Cwmin[3] = 2;

		pEdcaParm->Cwmax[0] = 10;
		pEdcaParm->Cwmax[1] = 10;
		pEdcaParm->Cwmax[2] = 4;
		pEdcaParm->Cwmax[3] = 3;

		pEdcaParm->Txop[0]  = 0;
		pEdcaParm->Txop[1]  = 0;
		pEdcaParm->Txop[2]  = 96;
		pEdcaParm->Txop[3]  = 48;

	}
#endif // DOT11_N_SUPPORT //

	NdisMoveMemory(&pAd->MlmeAux.APEdcaParm, pEdcaParm, sizeof(EDCA_PARM));

	// filter out un-supported rates
	pAd->MlmeAux.SupRateLen = SupRateLen;
	NdisMoveMemory(pAd->MlmeAux.SupRate, SupRate, SupRateLen);
	RTMPCheckRates(pAd, pAd->MlmeAux.SupRate, &pAd->MlmeAux.SupRateLen);

	// filter out un-supported rates
	pAd->MlmeAux.ExtRateLen = ExtRateLen;
	NdisMoveMemory(pAd->MlmeAux.ExtRate, ExtRate, ExtRateLen);
	RTMPCheckRates(pAd, pAd->MlmeAux.ExtRate, &pAd->MlmeAux.ExtRateLen);

#ifdef DOT11_N_SUPPORT
	if (HtCapabilityLen > 0)
	{
		RTMPCheckHt(pAd, BSSID_WCID, pHtCapability, pAddHtInfo);
	}
	DBGPRINT(RT_DEBUG_TRACE, ("AssocPostProc===>  AP.AMsduSize = %d. ClientStatusFlags = 0x%lx \n", pAd->MacTab.Content[BSSID_WCID].AMsduSize, pAd->MacTab.Content[BSSID_WCID].ClientStatusFlags));

	DBGPRINT(RT_DEBUG_TRACE, ("AssocPostProc===>    (Mmps=%d, AmsduSize=%d, )\n",
		pAd->MacTab.Content[BSSID_WCID].MmpsMode, pAd->MacTab.Content[BSSID_WCID].AMsduSize));
#endif // DOT11_N_SUPPORT //

	// Set New WPA information
	Idx = BssTableSearch(&pAd->ScanTab, pAddr2, pAd->MlmeAux.Channel);
	if (Idx == BSS_NOT_FOUND)
	{
		DBGPRINT_ERR(("ASSOC - Can't find BSS after receiving Assoc response\n"));
	}
	else
	{
		// Init variable
		pAd->MacTab.Content[BSSID_WCID].RSNIE_Len = 0;
		NdisZeroMemory(pAd->MacTab.Content[BSSID_WCID].RSN_IE, MAX_LEN_OF_RSNIE);

		// Store appropriate RSN_IE for WPA SM negotiation later
		if ((pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPA) && (pAd->ScanTab.BssEntry[Idx].VarIELen != 0))
		{
			PUCHAR              pVIE;
			USHORT              len;
			PEID_STRUCT         pEid;

			pVIE = pAd->ScanTab.BssEntry[Idx].VarIEs;
			len	 = pAd->ScanTab.BssEntry[Idx].VarIELen;

			while (len > 0)
			{
				pEid = (PEID_STRUCT) pVIE;
				// For WPA/WPAPSK
				if ((pEid->Eid == IE_WPA) && (NdisEqualMemory(pEid->Octet, WPA_OUI, 4))
					&& (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA || pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPAPSK))
				{
					NdisMoveMemory(pAd->MacTab.Content[BSSID_WCID].RSN_IE, pVIE, (pEid->Len + 2));
					pAd->MacTab.Content[BSSID_WCID].RSNIE_Len = (pEid->Len + 2);
					DBGPRINT(RT_DEBUG_TRACE, ("AssocPostProc===> Store RSN_IE for WPA SM negotiation \n"));
				}
				// For WPA2/WPA2PSK
				else if ((pEid->Eid == IE_RSN) && (NdisEqualMemory(pEid->Octet + 2, RSN_OUI, 3))
					&& (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2 || pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK))
				{
					NdisMoveMemory(pAd->MacTab.Content[BSSID_WCID].RSN_IE, pVIE, (pEid->Len + 2));
					pAd->MacTab.Content[BSSID_WCID].RSNIE_Len = (pEid->Len + 2);
					DBGPRINT(RT_DEBUG_TRACE, ("AssocPostProc===> Store RSN_IE for WPA2 SM negotiation \n"));
				}

				pVIE += (pEid->Len + 2);
				len  -= (pEid->Len + 2);
			}
		}

		if (pAd->MacTab.Content[BSSID_WCID].RSNIE_Len == 0)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("AssocPostProc===> no RSN_IE \n"));
		}
		else
		{
			hex_dump("RSN_IE", pAd->MacTab.Content[BSSID_WCID].RSN_IE, pAd->MacTab.Content[BSSID_WCID].RSNIE_Len);
		}
	}
}

/*
	==========================================================================
	Description:
		left part of IEEE 802.11/1999 p.374
	Parameters:
		Elem - MLME message containing the received frame

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID PeerDisassocAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	UCHAR         Addr2[MAC_ADDR_LEN];
	USHORT        Reason;

	DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - PeerDisassocAction()\n"));
	if(PeerDisassocSanity(pAd, Elem->Msg, Elem->MsgLen, Addr2, &Reason))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - PeerDisassocAction() Reason = %d\n", Reason));
		if (INFRA_ON(pAd) && MAC_ADDR_EQUAL(pAd->CommonCfg.Bssid, Addr2))
		{

			if (pAd->CommonCfg.bWirelessEvent)
			{
				RTMPSendWirelessEvent(pAd, IW_DISASSOC_EVENT_FLAG, pAd->MacTab.Content[BSSID_WCID].Addr, BSS0, 0);
			}

			//
			// Get Current System time and Turn on AdjacentAPReport
			//
			NdisGetSystemUpTime(&pAd->StaCfg.CCXAdjacentAPLinkDownTime);
			pAd->StaCfg.CCXAdjacentAPReportFlag = TRUE;
			LinkDown(pAd, TRUE);
			pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;

            {
                union iwreq_data    wrqu;
                memset(wrqu.ap_addr.sa_data, 0, MAC_ADDR_LEN);
                wireless_send_event(pAd->net_dev, SIOCGIWAP, &wrqu, NULL);
            }
		}
	}
	else
	{
		DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - PeerDisassocAction() sanity check fail\n"));
	}

}

/*
	==========================================================================
	Description:
		what the state machine will do after assoc timeout
	Parameters:
		Elme -

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID AssocTimeoutAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT  Status;
	DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - AssocTimeoutAction\n"));
	pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
	Status = MLME_REJ_TIMEOUT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_ASSOC_CONF, 2, &Status);
}

/*
	==========================================================================
	Description:
		what the state machine will do after reassoc timeout

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID ReassocTimeoutAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT  Status;
	DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - ReassocTimeoutAction\n"));
	pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
	Status = MLME_REJ_TIMEOUT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_REASSOC_CONF, 2, &Status);
}

/*
	==========================================================================
	Description:
		what the state machine will do after disassoc timeout

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID DisassocTimeoutAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT  Status;
	DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - DisassocTimeoutAction\n"));
	pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
	Status = MLME_SUCCESS;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_DISASSOC_CONF, 2, &Status);
}

VOID InvalidStateWhenAssoc(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT  Status;
	DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - InvalidStateWhenAssoc(state=%ld), reset ASSOC state machine\n",
		pAd->Mlme.AssocMachine.CurrState));
	pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_ASSOC_CONF, 2, &Status);
}

VOID InvalidStateWhenReassoc(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT Status;
	DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - InvalidStateWhenReassoc(state=%ld), reset ASSOC state machine\n",
		pAd->Mlme.AssocMachine.CurrState));
	pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_REASSOC_CONF, 2, &Status);
}

VOID InvalidStateWhenDisassociate(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT Status;
	DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - InvalidStateWhenDisassoc(state=%ld), reset ASSOC state machine\n",
		pAd->Mlme.AssocMachine.CurrState));
	pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_DISASSOC_CONF, 2, &Status);
}

/*
	==========================================================================
	Description:
		right part of IEEE 802.11/1999 page 374
	Note:
		This event should never cause ASSOC state machine perform state
		transition, and has no relationship with CNTL machine. So we separate
		this routine as a service outside of ASSOC state transition table.

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID Cls3errAction(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR        pAddr)
{
	HEADER_802_11         DisassocHdr;
	PHEADER_802_11        pDisassocHdr;
	PUCHAR                pOutBuffer = NULL;
	ULONG                 FrameLen = 0;
	NDIS_STATUS           NStatus;
	USHORT                Reason = REASON_CLS3ERR;

	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  //Get an unused nonpaged memory
	if (NStatus != NDIS_STATUS_SUCCESS)
		return;

	DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - Class 3 Error, Send DISASSOC frame\n"));
	MgtMacHeaderInit(pAd, &DisassocHdr, SUBTYPE_DISASSOC, 0, pAddr, pAd->CommonCfg.Bssid);	// patch peap ttls switching issue
	MakeOutgoingFrame(pOutBuffer,           &FrameLen,
					  sizeof(HEADER_802_11),&DisassocHdr,
					  2,                    &Reason,
					  END_OF_ARGS);
	MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);

	// To patch Instance and Buffalo(N) AP
	// Driver has to send deauth to Instance AP, but Buffalo(N) needs to send disassoc to reset Authenticator's state machine
	// Therefore, we send both of them.
	pDisassocHdr = (PHEADER_802_11)pOutBuffer;
	pDisassocHdr->FC.SubType = SUBTYPE_DEAUTH;
	MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);

	MlmeFreeMemory(pAd, pOutBuffer);

	pAd->StaCfg.DisassocReason = REASON_CLS3ERR;
	COPY_MAC_ADDR(pAd->StaCfg.DisassocSta, pAddr);
}

 /*
	 ==========================================================================
	 Description:
		 Switch between WEP and CKIP upon new association up.
	 Parameters:

	 IRQL = DISPATCH_LEVEL

	 ==========================================================================
  */
VOID SwitchBetweenWepAndCkip(
	IN PRTMP_ADAPTER pAd)
{
	int            i;
	SHAREDKEY_MODE_STRUC  csr1;

	// if KP is required. change the CipherAlg in hardware shard key table from WEP
	// to CKIP. else remain as WEP
	if (pAd->StaCfg.bCkipOn && (pAd->StaCfg.CkipFlag & 0x10))
	{
		// modify hardware key table so that MAC use correct algorithm to decrypt RX
		RTMP_IO_READ32(pAd, SHARED_KEY_MODE_BASE, &csr1.word);
		if (csr1.field.Bss0Key0CipherAlg == CIPHER_WEP64)
			csr1.field.Bss0Key0CipherAlg = CIPHER_CKIP64;
		else if (csr1.field.Bss0Key0CipherAlg == CIPHER_WEP128)
			csr1.field.Bss0Key0CipherAlg = CIPHER_CKIP128;

		if (csr1.field.Bss0Key1CipherAlg == CIPHER_WEP64)
			csr1.field.Bss0Key1CipherAlg = CIPHER_CKIP64;
		else if (csr1.field.Bss0Key1CipherAlg == CIPHER_WEP128)
			csr1.field.Bss0Key1CipherAlg = CIPHER_CKIP128;

		if (csr1.field.Bss0Key2CipherAlg == CIPHER_WEP64)
			csr1.field.Bss0Key2CipherAlg = CIPHER_CKIP64;
		else if (csr1.field.Bss0Key2CipherAlg == CIPHER_WEP128)
			csr1.field.Bss0Key2CipherAlg = CIPHER_CKIP128;

		if (csr1.field.Bss0Key3CipherAlg == CIPHER_WEP64)
			csr1.field.Bss0Key3CipherAlg = CIPHER_CKIP64;
		else if (csr1.field.Bss0Key3CipherAlg == CIPHER_WEP128)
			csr1.field.Bss0Key3CipherAlg = CIPHER_CKIP128;
		RTMP_IO_WRITE32(pAd, SHARED_KEY_MODE_BASE, csr1.word);
		DBGPRINT(RT_DEBUG_TRACE, ("SwitchBetweenWepAndCkip: modify BSS0 cipher to %s\n", CipherName[csr1.field.Bss0Key0CipherAlg]));

		// modify software key table so that driver can specify correct algorithm in TXD upon TX
		for (i=0; i<SHARE_KEY_NUM; i++)
		{
			if (pAd->SharedKey[BSS0][i].CipherAlg == CIPHER_WEP64)
				pAd->SharedKey[BSS0][i].CipherAlg = CIPHER_CKIP64;
			else if (pAd->SharedKey[BSS0][i].CipherAlg == CIPHER_WEP128)
				pAd->SharedKey[BSS0][i].CipherAlg = CIPHER_CKIP128;
		}
	}

	// else if KP NOT inused. change the CipherAlg in hardware shard key table from CKIP
	// to WEP.
	else
	{
		// modify hardware key table so that MAC use correct algorithm to decrypt RX
		RTMP_IO_READ32(pAd, SHARED_KEY_MODE_BASE, &csr1.word);
		if (csr1.field.Bss0Key0CipherAlg == CIPHER_CKIP64)
			csr1.field.Bss0Key0CipherAlg = CIPHER_WEP64;
		else if (csr1.field.Bss0Key0CipherAlg == CIPHER_CKIP128)
			csr1.field.Bss0Key0CipherAlg = CIPHER_WEP128;

		if (csr1.field.Bss0Key1CipherAlg == CIPHER_CKIP64)
			csr1.field.Bss0Key1CipherAlg = CIPHER_WEP64;
		else if (csr1.field.Bss0Key1CipherAlg == CIPHER_CKIP128)
			csr1.field.Bss0Key1CipherAlg = CIPHER_WEP128;

		if (csr1.field.Bss0Key2CipherAlg == CIPHER_CKIP64)
			csr1.field.Bss0Key2CipherAlg = CIPHER_WEP64;
		else if (csr1.field.Bss0Key2CipherAlg == CIPHER_CKIP128)
			csr1.field.Bss0Key2CipherAlg = CIPHER_WEP128;

		if (csr1.field.Bss0Key3CipherAlg == CIPHER_CKIP64)
			csr1.field.Bss0Key3CipherAlg = CIPHER_WEP64;
		else if (csr1.field.Bss0Key3CipherAlg == CIPHER_CKIP128)
			csr1.field.Bss0Key3CipherAlg = CIPHER_WEP128;

		// modify software key table so that driver can specify correct algorithm in TXD upon TX
		for (i=0; i<SHARE_KEY_NUM; i++)
		{
			if (pAd->SharedKey[BSS0][i].CipherAlg == CIPHER_CKIP64)
				pAd->SharedKey[BSS0][i].CipherAlg = CIPHER_WEP64;
			else if (pAd->SharedKey[BSS0][i].CipherAlg == CIPHER_CKIP128)
				pAd->SharedKey[BSS0][i].CipherAlg = CIPHER_WEP128;
		}

		//
		// On WPA-NONE, must update CipherAlg.
		// Because the OID_802_11_WEP_STATUS was been set after OID_802_11_ADD_KEY
		// and CipherAlg will be CIPHER_NONE by Windows ZeroConfig.
		// So we need to update CipherAlg after connect.
		//
		if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPANone)
		{
			for (i = 0; i < SHARE_KEY_NUM; i++)
			{
				if (pAd->SharedKey[BSS0][i].KeyLen != 0)
				{
					if (pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled)
					{
						pAd->SharedKey[BSS0][i].CipherAlg = CIPHER_TKIP;
					}
					else if (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)
					{
						pAd->SharedKey[BSS0][i].CipherAlg = CIPHER_AES;
					}
				}
				else
				{
					pAd->SharedKey[BSS0][i].CipherAlg = CIPHER_NONE;
				}
			}

			csr1.field.Bss0Key0CipherAlg = pAd->SharedKey[BSS0][0].CipherAlg;
			csr1.field.Bss0Key1CipherAlg = pAd->SharedKey[BSS0][1].CipherAlg;
			csr1.field.Bss0Key2CipherAlg = pAd->SharedKey[BSS0][2].CipherAlg;
			csr1.field.Bss0Key3CipherAlg = pAd->SharedKey[BSS0][3].CipherAlg;
		}
		RTMP_IO_WRITE32(pAd, SHARED_KEY_MODE_BASE, csr1.word);
		DBGPRINT(RT_DEBUG_TRACE, ("SwitchBetweenWepAndCkip: modify BSS0 cipher to %s\n", CipherName[csr1.field.Bss0Key0CipherAlg]));
	}
}

int wext_notify_event_assoc(
	IN  RTMP_ADAPTER *pAd)
{
    union iwreq_data    wrqu;
    char custom[IW_CUSTOM_MAX] = {0};

#if WIRELESS_EXT > 17
    if (pAd->StaCfg.ReqVarIELen <= IW_CUSTOM_MAX)
    {
        wrqu.data.length = pAd->StaCfg.ReqVarIELen;
        memcpy(custom, pAd->StaCfg.ReqVarIEs, pAd->StaCfg.ReqVarIELen);
        wireless_send_event(pAd->net_dev, IWEVASSOCREQIE, &wrqu, custom);
    }
    else
        DBGPRINT(RT_DEBUG_TRACE, ("pAd->StaCfg.ReqVarIELen > MAX_CUSTOM_LEN\n"));
#else
    if (((pAd->StaCfg.ReqVarIELen*2) + 17) <= IW_CUSTOM_MAX)
    {
        UCHAR   idx;
        wrqu.data.length = (pAd->StaCfg.ReqVarIELen*2) + 17;
        sprintf(custom, "ASSOCINFO(ReqIEs=");
        for (idx=0; idx<pAd->StaCfg.ReqVarIELen; idx++)
                sprintf(custom + strlen(custom), "%02x", pAd->StaCfg.ReqVarIEs[idx]);
        wireless_send_event(pAd->net_dev, IWEVCUSTOM, &wrqu, custom);
    }
    else
        DBGPRINT(RT_DEBUG_TRACE, ("(pAd->StaCfg.ReqVarIELen*2) + 17 > MAX_CUSTOM_LEN\n"));
#endif

	return 0;

}

BOOLEAN StaAddMacTableEntry(
	IN  PRTMP_ADAPTER		pAd,
	IN  PMAC_TABLE_ENTRY	pEntry,
	IN  UCHAR				MaxSupportedRateIn500Kbps,
	IN  HT_CAPABILITY_IE	*pHtCapability,
	IN  UCHAR				HtCapabilityLen,
	IN  USHORT        		CapabilityInfo)
{
	UCHAR            MaxSupportedRate = RATE_11;

	if (ADHOC_ON(pAd))
		CLIENT_STATUS_CLEAR_FLAG(pEntry, fCLIENT_STATUS_WMM_CAPABLE);

	switch (MaxSupportedRateIn500Kbps)
    {
        case 108: MaxSupportedRate = RATE_54;   break;
        case 96:  MaxSupportedRate = RATE_48;   break;
        case 72:  MaxSupportedRate = RATE_36;   break;
        case 48:  MaxSupportedRate = RATE_24;   break;
        case 36:  MaxSupportedRate = RATE_18;   break;
        case 24:  MaxSupportedRate = RATE_12;   break;
        case 18:  MaxSupportedRate = RATE_9;    break;
        case 12:  MaxSupportedRate = RATE_6;    break;
        case 22:  MaxSupportedRate = RATE_11;   break;
        case 11:  MaxSupportedRate = RATE_5_5;  break;
        case 4:   MaxSupportedRate = RATE_2;    break;
        case 2:   MaxSupportedRate = RATE_1;    break;
        default:  MaxSupportedRate = RATE_11;   break;
    }

    if ((pAd->CommonCfg.PhyMode == PHY_11G) && (MaxSupportedRate < RATE_FIRST_OFDM_RATE))
        return FALSE;

#ifdef DOT11_N_SUPPORT
	// 11n only
	if (((pAd->CommonCfg.PhyMode == PHY_11N_2_4G) || (pAd->CommonCfg.PhyMode == PHY_11N_5G))&& (HtCapabilityLen == 0))
		return FALSE;
#endif // DOT11_N_SUPPORT //

	if (!pEntry)
        return FALSE;

	NdisAcquireSpinLock(&pAd->MacTabLock);
	if (pEntry)
	{
		pEntry->PortSecured = WPA_802_1X_PORT_SECURED;
		if ((MaxSupportedRate < RATE_FIRST_OFDM_RATE) ||
			(pAd->CommonCfg.PhyMode == PHY_11B))
		{
			pEntry->RateLen = 4;
			if (MaxSupportedRate >= RATE_FIRST_OFDM_RATE)
				MaxSupportedRate = RATE_11;
		}
		else
			pEntry->RateLen = 12;

		pEntry->MaxHTPhyMode.word = 0;
		pEntry->MinHTPhyMode.word = 0;
		pEntry->HTPhyMode.word = 0;
		pEntry->MaxSupportedRate = MaxSupportedRate;
		if (pEntry->MaxSupportedRate < RATE_FIRST_OFDM_RATE)
		{
			pEntry->MaxHTPhyMode.field.MODE = MODE_CCK;
			pEntry->MaxHTPhyMode.field.MCS = pEntry->MaxSupportedRate;
			pEntry->MinHTPhyMode.field.MODE = MODE_CCK;
			pEntry->MinHTPhyMode.field.MCS = pEntry->MaxSupportedRate;
			pEntry->HTPhyMode.field.MODE = MODE_CCK;
			pEntry->HTPhyMode.field.MCS = pEntry->MaxSupportedRate;
		}
		else
		{
			pEntry->MaxHTPhyMode.field.MODE = MODE_OFDM;
			pEntry->MaxHTPhyMode.field.MCS = OfdmRateToRxwiMCS[pEntry->MaxSupportedRate];
			pEntry->MinHTPhyMode.field.MODE = MODE_OFDM;
			pEntry->MinHTPhyMode.field.MCS = OfdmRateToRxwiMCS[pEntry->MaxSupportedRate];
			pEntry->HTPhyMode.field.MODE = MODE_OFDM;
			pEntry->HTPhyMode.field.MCS = OfdmRateToRxwiMCS[pEntry->MaxSupportedRate];
		}
		pEntry->CapabilityInfo = CapabilityInfo;
		CLIENT_STATUS_CLEAR_FLAG(pEntry, fCLIENT_STATUS_AGGREGATION_CAPABLE);
		CLIENT_STATUS_CLEAR_FLAG(pEntry, fCLIENT_STATUS_PIGGYBACK_CAPABLE);
	}

#ifdef DOT11_N_SUPPORT
	// If this Entry supports 802.11n, upgrade to HT rate.
	if ((HtCapabilityLen != 0) && (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED))
	{
		UCHAR	j, bitmask; //k,bitmask;
		CHAR    i;

		if (ADHOC_ON(pAd))
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_WMM_CAPABLE);
		if ((pHtCapability->HtCapInfo.GF) && (pAd->CommonCfg.DesiredHtPhy.GF))
		{
			pEntry->MaxHTPhyMode.field.MODE = MODE_HTGREENFIELD;
		}
		else
		{
			pEntry->MaxHTPhyMode.field.MODE = MODE_HTMIX;
			pAd->MacTab.fAnyStationNonGF = TRUE;
			pAd->CommonCfg.AddHTInfo.AddHtInfo2.NonGfPresent = 1;
		}

		if ((pHtCapability->HtCapInfo.ChannelWidth) && (pAd->CommonCfg.DesiredHtPhy.ChannelWidth))
		{
			pEntry->MaxHTPhyMode.field.BW= BW_40;
			pEntry->MaxHTPhyMode.field.ShortGI = ((pAd->CommonCfg.DesiredHtPhy.ShortGIfor40)&(pHtCapability->HtCapInfo.ShortGIfor40));
		}
		else
		{
			pEntry->MaxHTPhyMode.field.BW = BW_20;
			pEntry->MaxHTPhyMode.field.ShortGI = ((pAd->CommonCfg.DesiredHtPhy.ShortGIfor20)&(pHtCapability->HtCapInfo.ShortGIfor20));
			pAd->MacTab.fAnyStation20Only = TRUE;
		}

		// 3*3
		if (pAd->MACVersion >= RALINK_2883_VERSION && pAd->MACVersion < RALINK_3070_VERSION)
			pEntry->MaxHTPhyMode.field.TxBF = pAd->CommonCfg.RegTransmitSetting.field.TxBF;

		// find max fixed rate
		for (i=23; i>=0; i--) // 3*3
		{
			j = i/8;
			bitmask = (1<<(i-(j*8)));
			if ((pAd->StaCfg.DesiredHtPhyInfo.MCSSet[j] & bitmask) && (pHtCapability->MCSSet[j] & bitmask))
			{
				pEntry->MaxHTPhyMode.field.MCS = i;
				break;
			}
			if (i==0)
				break;
		}


		if (pAd->StaCfg.DesiredTransmitSetting.field.MCS != MCS_AUTO)
		{
			if (pAd->StaCfg.DesiredTransmitSetting.field.MCS == 32)
			{
				// Fix MCS as HT Duplicated Mode
				pEntry->MaxHTPhyMode.field.BW = 1;
				pEntry->MaxHTPhyMode.field.MODE = MODE_HTMIX;
				pEntry->MaxHTPhyMode.field.STBC = 0;
				pEntry->MaxHTPhyMode.field.ShortGI = 0;
				pEntry->MaxHTPhyMode.field.MCS = 32;
			}
			else if (pEntry->MaxHTPhyMode.field.MCS > pAd->StaCfg.HTPhyMode.field.MCS)
			{
				// STA supports fixed MCS
				pEntry->MaxHTPhyMode.field.MCS = pAd->StaCfg.HTPhyMode.field.MCS;
			}
		}

		pEntry->MaxHTPhyMode.field.STBC = (pHtCapability->HtCapInfo.RxSTBC & (pAd->CommonCfg.DesiredHtPhy.TxSTBC));
		pEntry->MpduDensity = pHtCapability->HtCapParm.MpduDensity;
		pEntry->MaxRAmpduFactor = pHtCapability->HtCapParm.MaxRAmpduFactor;
		pEntry->MmpsMode = (UCHAR)pHtCapability->HtCapInfo.MimoPs;
		pEntry->AMsduSize = (UCHAR)pHtCapability->HtCapInfo.AMsduSize;
		pEntry->HTPhyMode.word = pEntry->MaxHTPhyMode.word;

		if (pAd->CommonCfg.DesiredHtPhy.AmsduEnable && (pAd->CommonCfg.REGBACapability.field.AutoBA == FALSE))
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_AMSDU_INUSED);
		if (pHtCapability->HtCapInfo.ShortGIfor20)
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_SGI20_CAPABLE);
		if (pHtCapability->HtCapInfo.ShortGIfor40)
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_SGI40_CAPABLE);
		if (pHtCapability->HtCapInfo.TxSTBC)
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_TxSTBC_CAPABLE);
		if (pHtCapability->HtCapInfo.RxSTBC)
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_RxSTBC_CAPABLE);
		if (pHtCapability->ExtHtCapInfo.PlusHTC)
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_HTC_CAPABLE);
		if (pAd->CommonCfg.bRdg && pHtCapability->ExtHtCapInfo.RDGSupport)
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_RDG_CAPABLE);
		if (pHtCapability->ExtHtCapInfo.MCSFeedback == 0x03)
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_MCSFEEDBACK_CAPABLE);
	}
	else
	{
		pAd->MacTab.fAnyStationIsLegacy = TRUE;
	}

	NdisMoveMemory(&pEntry->HTCapability, pHtCapability, sizeof(HT_CAPABILITY_IE));
#endif // DOT11_N_SUPPORT //

	pEntry->HTPhyMode.word = pEntry->MaxHTPhyMode.word;
	pEntry->CurrTxRate = pEntry->MaxSupportedRate;

	// Set asic auto fall back
	if (pAd->StaCfg.bAutoTxRateSwitch == TRUE)
	{
		PUCHAR					pTable;
		UCHAR					TableSize = 0;

		MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize, &pEntry->CurrTxRateIndex);
		pEntry->bAutoTxRateSwitch = TRUE;
	}
	else
	{
		pEntry->HTPhyMode.field.MODE	= pAd->StaCfg.HTPhyMode.field.MODE;
		pEntry->HTPhyMode.field.MCS	= pAd->StaCfg.HTPhyMode.field.MCS;
		pEntry->bAutoTxRateSwitch = FALSE;

		// If the legacy mode is set, overwrite the transmit setting of this entry.
		RTMPUpdateLegacyTxSetting((UCHAR)pAd->StaCfg.DesiredTransmitSetting.field.FixedTxMode, pEntry);
	}

	pEntry->PortSecured = WPA_802_1X_PORT_SECURED;
	pEntry->Sst = SST_ASSOC;
	pEntry->AuthState = AS_AUTH_OPEN;
	pEntry->AuthMode = pAd->StaCfg.AuthMode;
	pEntry->WepStatus = pAd->StaCfg.WepStatus;

	NdisReleaseSpinLock(&pAd->MacTabLock);

    {
        union iwreq_data    wrqu;
        wext_notify_event_assoc(pAd);

        memset(wrqu.ap_addr.sa_data, 0, MAC_ADDR_LEN);
        memcpy(wrqu.ap_addr.sa_data, pAd->MlmeAux.Bssid, MAC_ADDR_LEN);
        wireless_send_event(pAd->net_dev, SIOCGIWAP, &wrqu, NULL);

    }
	return TRUE;
}


