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

u8 CipherWpaTemplate[] = {
	0xdd,			/* WPA IE */
	0x16,			/* Length */
	0x00, 0x50, 0xf2, 0x01,	/* oui */
	0x01, 0x00,		/* Version */
	0x00, 0x50, 0xf2, 0x02,	/* Multicast */
	0x01, 0x00,		/* Number of unicast */
	0x00, 0x50, 0xf2, 0x02,	/* unicast */
	0x01, 0x00,		/* number of authentication method */
	0x00, 0x50, 0xf2, 0x01	/* authentication */
};

u8 CipherWpa2Template[] = {
	0x30,			/* RSN IE */
	0x14,			/* Length */
	0x01, 0x00,		/* Version */
	0x00, 0x0f, 0xac, 0x02,	/* group cipher, TKIP */
	0x01, 0x00,		/* number of pairwise */
	0x00, 0x0f, 0xac, 0x02,	/* unicast */
	0x01, 0x00,		/* number of authentication method */
	0x00, 0x0f, 0xac, 0x02,	/* authentication */
	0x00, 0x00,		/* RSN capability */
};

u8 Ccx2IeInfo[] = { 0x00, 0x40, 0x96, 0x03, 0x02 };

/*
	==========================================================================
	Description:
		association state machine init, including state transition and timer init
	Parameters:
		S - pointer to the association state machine

	IRQL = PASSIVE_LEVEL

	==========================================================================
 */
void AssocStateMachineInit(struct rt_rtmp_adapter *pAd,
			   struct rt_state_machine *S, OUT STATE_MACHINE_FUNC Trans[])
{
	StateMachineInit(S, Trans, MAX_ASSOC_STATE, MAX_ASSOC_MSG,
			 (STATE_MACHINE_FUNC) Drop, ASSOC_IDLE,
			 ASSOC_MACHINE_BASE);

	/* first column */
	StateMachineSetAction(S, ASSOC_IDLE, MT2_MLME_ASSOC_REQ,
			      (STATE_MACHINE_FUNC) MlmeAssocReqAction);
	StateMachineSetAction(S, ASSOC_IDLE, MT2_MLME_REASSOC_REQ,
			      (STATE_MACHINE_FUNC) MlmeReassocReqAction);
	StateMachineSetAction(S, ASSOC_IDLE, MT2_MLME_DISASSOC_REQ,
			      (STATE_MACHINE_FUNC) MlmeDisassocReqAction);
	StateMachineSetAction(S, ASSOC_IDLE, MT2_PEER_DISASSOC_REQ,
			      (STATE_MACHINE_FUNC) PeerDisassocAction);

	/* second column */
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_MLME_ASSOC_REQ,
			      (STATE_MACHINE_FUNC) InvalidStateWhenAssoc);
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_MLME_REASSOC_REQ,
			      (STATE_MACHINE_FUNC) InvalidStateWhenReassoc);
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_MLME_DISASSOC_REQ,
			      (STATE_MACHINE_FUNC)
			      InvalidStateWhenDisassociate);
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_PEER_DISASSOC_REQ,
			      (STATE_MACHINE_FUNC) PeerDisassocAction);
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_PEER_ASSOC_RSP,
			      (STATE_MACHINE_FUNC) PeerAssocRspAction);
	/* */
	/* Patch 3Com AP MOde:3CRWE454G72 */
	/* We send Assoc request frame to this AP, it always send Reassoc Rsp not Associate Rsp. */
	/* */
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_PEER_REASSOC_RSP,
			      (STATE_MACHINE_FUNC) PeerAssocRspAction);
	StateMachineSetAction(S, ASSOC_WAIT_RSP, MT2_ASSOC_TIMEOUT,
			      (STATE_MACHINE_FUNC) AssocTimeoutAction);

	/* third column */
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_MLME_ASSOC_REQ,
			      (STATE_MACHINE_FUNC) InvalidStateWhenAssoc);
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_MLME_REASSOC_REQ,
			      (STATE_MACHINE_FUNC) InvalidStateWhenReassoc);
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_MLME_DISASSOC_REQ,
			      (STATE_MACHINE_FUNC)
			      InvalidStateWhenDisassociate);
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_PEER_DISASSOC_REQ,
			      (STATE_MACHINE_FUNC) PeerDisassocAction);
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_PEER_REASSOC_RSP,
			      (STATE_MACHINE_FUNC) PeerReassocRspAction);
	/* */
	/* Patch, AP doesn't send Reassociate Rsp frame to Station. */
	/* */
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_PEER_ASSOC_RSP,
			      (STATE_MACHINE_FUNC) PeerReassocRspAction);
	StateMachineSetAction(S, REASSOC_WAIT_RSP, MT2_REASSOC_TIMEOUT,
			      (STATE_MACHINE_FUNC) ReassocTimeoutAction);

	/* fourth column */
	StateMachineSetAction(S, DISASSOC_WAIT_RSP, MT2_MLME_ASSOC_REQ,
			      (STATE_MACHINE_FUNC) InvalidStateWhenAssoc);
	StateMachineSetAction(S, DISASSOC_WAIT_RSP, MT2_MLME_REASSOC_REQ,
			      (STATE_MACHINE_FUNC) InvalidStateWhenReassoc);
	StateMachineSetAction(S, DISASSOC_WAIT_RSP, MT2_MLME_DISASSOC_REQ,
			      (STATE_MACHINE_FUNC)
			      InvalidStateWhenDisassociate);
	StateMachineSetAction(S, DISASSOC_WAIT_RSP, MT2_PEER_DISASSOC_REQ,
			      (STATE_MACHINE_FUNC) PeerDisassocAction);
	StateMachineSetAction(S, DISASSOC_WAIT_RSP, MT2_DISASSOC_TIMEOUT,
			      (STATE_MACHINE_FUNC) DisassocTimeoutAction);

	/* initialize the timer */
	RTMPInitTimer(pAd, &pAd->MlmeAux.AssocTimer,
		      GET_TIMER_FUNCTION(AssocTimeout), pAd, FALSE);
	RTMPInitTimer(pAd, &pAd->MlmeAux.ReassocTimer,
		      GET_TIMER_FUNCTION(ReassocTimeout), pAd, FALSE);
	RTMPInitTimer(pAd, &pAd->MlmeAux.DisassocTimer,
		      GET_TIMER_FUNCTION(DisassocTimeout), pAd, FALSE);
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
void AssocTimeout(void *SystemSpecific1,
		  void *FunctionContext,
		  void *SystemSpecific2, void *SystemSpecific3)
{
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)FunctionContext;

	/* Do nothing if the driver is starting halt state. */
	/* This might happen when timer already been fired before cancel timer with mlmehalt */
	if (RTMP_TEST_FLAG
	    (pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

	MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_ASSOC_TIMEOUT, 0, NULL);
	RTMP_MLME_HANDLER(pAd);
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
void ReassocTimeout(void *SystemSpecific1,
		    void *FunctionContext,
		    void *SystemSpecific2, void *SystemSpecific3)
{
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)FunctionContext;

	/* Do nothing if the driver is starting halt state. */
	/* This might happen when timer already been fired before cancel timer with mlmehalt */
	if (RTMP_TEST_FLAG
	    (pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

	MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_REASSOC_TIMEOUT, 0, NULL);
	RTMP_MLME_HANDLER(pAd);
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
void DisassocTimeout(void *SystemSpecific1,
		     void *FunctionContext,
		     void *SystemSpecific2, void *SystemSpecific3)
{
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)FunctionContext;

	/* Do nothing if the driver is starting halt state. */
	/* This might happen when timer already been fired before cancel timer with mlmehalt */
	if (RTMP_TEST_FLAG
	    (pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

	MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_DISASSOC_TIMEOUT, 0, NULL);
	RTMP_MLME_HANDLER(pAd);
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
void MlmeAssocReqAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	u8 ApAddr[6];
	struct rt_header_802_11 AssocHdr;
	u8 WmeIe[9] =
	    { IE_VENDOR_SPECIFIC, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01,
       0x00 };
	u16 ListenIntv;
	unsigned long Timeout;
	u16 CapabilityInfo;
	BOOLEAN TimerCancelled;
	u8 *pOutBuffer = NULL;
	int NStatus;
	unsigned long FrameLen = 0;
	unsigned long tmp;
	u16 VarIesOffset;
	u16 Status;

	/* Block all authentication request durning WPA block period */
	if (pAd->StaCfg.bBlockAssoc == TRUE) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("ASSOC - Block Assoc request durning WPA block period!\n"));
		pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
		Status = MLME_STATE_MACHINE_REJECT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_ASSOC_CONF, 2,
			    &Status);
	}
	/* check sanity first */
	else if (MlmeAssocReqSanity
		 (pAd, Elem->Msg, Elem->MsgLen, ApAddr, &CapabilityInfo,
		  &Timeout, &ListenIntv)) {
		RTMPCancelTimer(&pAd->MlmeAux.AssocTimer, &TimerCancelled);
		COPY_MAC_ADDR(pAd->MlmeAux.Bssid, ApAddr);

		/* Get an unused nonpaged memory */
		NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);
		if (NStatus != NDIS_STATUS_SUCCESS) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("ASSOC - MlmeAssocReqAction() allocate memory failed \n"));
			pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
			Status = MLME_FAIL_NO_RESOURCE;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE,
				    MT2_ASSOC_CONF, 2, &Status);
			return;
		}
		/* Add by James 03/06/27 */
		pAd->StaCfg.AssocInfo.Length =
		    sizeof(struct rt_ndis_802_11_association_information);
		/* Association don't need to report MAC address */
		pAd->StaCfg.AssocInfo.AvailableRequestFixedIEs =
		    NDIS_802_11_AI_REQFI_CAPABILITIES |
		    NDIS_802_11_AI_REQFI_LISTENINTERVAL;
		pAd->StaCfg.AssocInfo.RequestFixedIEs.Capabilities =
		    CapabilityInfo;
		pAd->StaCfg.AssocInfo.RequestFixedIEs.ListenInterval =
		    ListenIntv;
		/* Only reassociate need this */
		/*COPY_MAC_ADDR(pAd->StaCfg.AssocInfo.RequestFixedIEs.CurrentAPAddress, ApAddr); */
		pAd->StaCfg.AssocInfo.OffsetRequestIEs =
		    sizeof(struct rt_ndis_802_11_association_information);

		NdisZeroMemory(pAd->StaCfg.ReqVarIEs, MAX_VIE_LEN);
		/* First add SSID */
		VarIesOffset = 0;
		NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset, &SsidIe,
			       1);
		VarIesOffset += 1;
		NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset,
			       &pAd->MlmeAux.SsidLen, 1);
		VarIesOffset += 1;
		NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset,
			       pAd->MlmeAux.Ssid, pAd->MlmeAux.SsidLen);
		VarIesOffset += pAd->MlmeAux.SsidLen;

		/* Second add Supported rates */
		NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset, &SupRateIe,
			       1);
		VarIesOffset += 1;
		NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset,
			       &pAd->MlmeAux.SupRateLen, 1);
		VarIesOffset += 1;
		NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset,
			       pAd->MlmeAux.SupRate, pAd->MlmeAux.SupRateLen);
		VarIesOffset += pAd->MlmeAux.SupRateLen;
		/* End Add by James */

		if ((pAd->CommonCfg.Channel > 14) &&
		    (pAd->CommonCfg.bIEEE80211H == TRUE))
			CapabilityInfo |= 0x0100;

		DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - Send ASSOC request...\n"));
		MgtMacHeaderInit(pAd, &AssocHdr, SUBTYPE_ASSOC_REQ, 0, ApAddr,
				 ApAddr);

		/* Build basic frame first */
		MakeOutgoingFrame(pOutBuffer, &FrameLen,
				  sizeof(struct rt_header_802_11), &AssocHdr,
				  2, &CapabilityInfo,
				  2, &ListenIntv,
				  1, &SsidIe,
				  1, &pAd->MlmeAux.SsidLen,
				  pAd->MlmeAux.SsidLen, pAd->MlmeAux.Ssid,
				  1, &SupRateIe,
				  1, &pAd->MlmeAux.SupRateLen,
				  pAd->MlmeAux.SupRateLen, pAd->MlmeAux.SupRate,
				  END_OF_ARGS);

		if (pAd->MlmeAux.ExtRateLen != 0) {
			MakeOutgoingFrame(pOutBuffer + FrameLen, &tmp,
					  1, &ExtRateIe,
					  1, &pAd->MlmeAux.ExtRateLen,
					  pAd->MlmeAux.ExtRateLen,
					  pAd->MlmeAux.ExtRate, END_OF_ARGS);
			FrameLen += tmp;
		}
		/* HT */
		if ((pAd->MlmeAux.HtCapabilityLen > 0)
		    && (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED)) {
			unsigned long TmpLen;
			u8 HtLen;
			u8 BROADCOM[4] = { 0x0, 0x90, 0x4c, 0x33 };
			if (pAd->StaActive.SupportedPhyInfo.bPreNHt == TRUE) {
				HtLen = SIZE_HT_CAP_IE + 4;
				MakeOutgoingFrame(pOutBuffer + FrameLen,
						  &TmpLen, 1, &WpaIe, 1, &HtLen,
						  4, &BROADCOM[0],
						  pAd->MlmeAux.HtCapabilityLen,
						  &pAd->MlmeAux.HtCapability,
						  END_OF_ARGS);
			} else {
				MakeOutgoingFrame(pOutBuffer + FrameLen,
						  &TmpLen, 1, &HtCapIe, 1,
						  &pAd->MlmeAux.HtCapabilityLen,
						  pAd->MlmeAux.HtCapabilityLen,
						  &pAd->MlmeAux.HtCapability,
						  END_OF_ARGS);
			}
			FrameLen += TmpLen;
		}
		/* add Ralink proprietary IE to inform AP this STA is going to use AGGREGATION or PIGGY-BACK+AGGREGATION */
		/* Case I: (Aggregation + Piggy-Back) */
		/* 1. user enable aggregation, AND */
		/* 2. Mac support piggy-back */
		/* 3. AP annouces it's PIGGY-BACK+AGGREGATION-capable in BEACON */
		/* Case II: (Aggregation) */
		/* 1. user enable aggregation, AND */
		/* 2. AP annouces it's AGGREGATION-capable in BEACON */
		if (pAd->CommonCfg.bAggregationCapable) {
			if ((pAd->CommonCfg.bPiggyBackCapable)
			    && ((pAd->MlmeAux.APRalinkIe & 0x00000003) == 3)) {
				unsigned long TmpLen;
				u8 RalinkIe[9] =
				    { IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43,
			    0x03, 0x00, 0x00, 0x00 };
				MakeOutgoingFrame(pOutBuffer + FrameLen,
						  &TmpLen, 9, RalinkIe,
						  END_OF_ARGS);
				FrameLen += TmpLen;
			} else if (pAd->MlmeAux.APRalinkIe & 0x00000001) {
				unsigned long TmpLen;
				u8 RalinkIe[9] =
				    { IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43,
			    0x01, 0x00, 0x00, 0x00 };
				MakeOutgoingFrame(pOutBuffer + FrameLen,
						  &TmpLen, 9, RalinkIe,
						  END_OF_ARGS);
				FrameLen += TmpLen;
			}
		} else {
			unsigned long TmpLen;
			u8 RalinkIe[9] =
			    { IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43, 0x06,
		    0x00, 0x00, 0x00 };
			MakeOutgoingFrame(pOutBuffer + FrameLen, &TmpLen, 9,
					  RalinkIe, END_OF_ARGS);
			FrameLen += TmpLen;
		}

		if (pAd->MlmeAux.APEdcaParm.bValid) {
			if (pAd->CommonCfg.bAPSDCapable
			    && pAd->MlmeAux.APEdcaParm.bAPSDCapable) {
				struct rt_qbss_sta_info_parm QosInfo;

				NdisZeroMemory(&QosInfo,
					       sizeof(struct rt_qbss_sta_info_parm));
				QosInfo.UAPSD_AC_BE = pAd->CommonCfg.bAPSDAC_BE;
				QosInfo.UAPSD_AC_BK = pAd->CommonCfg.bAPSDAC_BK;
				QosInfo.UAPSD_AC_VI = pAd->CommonCfg.bAPSDAC_VI;
				QosInfo.UAPSD_AC_VO = pAd->CommonCfg.bAPSDAC_VO;
				QosInfo.MaxSPLength =
				    pAd->CommonCfg.MaxSPLength;
				WmeIe[8] |= *(u8 *)& QosInfo;
			} else {
				/* The Parameter Set Count is set to ¡§0¡¨ in the association request frames */
				/* WmeIe[8] |= (pAd->MlmeAux.APEdcaParm.EdcaUpdateCount & 0x0f); */
			}

			MakeOutgoingFrame(pOutBuffer + FrameLen, &tmp,
					  9, &WmeIe[0], END_OF_ARGS);
			FrameLen += tmp;
		}
		/* */
		/* Let WPA(#221) Element ID on the end of this association frame. */
		/* Otherwise some AP will fail on parsing Element ID and set status fail on Assoc Rsp. */
		/* For example: Put Vendor Specific IE on the front of WPA IE. */
		/* This happens on AP (Model No:Linksys WRK54G) */
		/* */
		if (((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPAPSK) ||
		     (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK) ||
		     (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA) ||
		     (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2)
		    )
		    ) {
			u8 RSNIe = IE_WPA;

			if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK)
			    || (pAd->StaCfg.AuthMode ==
				Ndis802_11AuthModeWPA2)) {
				RSNIe = IE_WPA2;
			}

			if ((pAd->StaCfg.WpaSupplicantUP !=
			     WPA_SUPPLICANT_ENABLE)
			    && (pAd->StaCfg.bRSN_IE_FromWpaSupplicant == FALSE))
				RTMPMakeRSNIE(pAd, pAd->StaCfg.AuthMode,
					      pAd->StaCfg.WepStatus, BSS0);

			/* Check for WPA PMK cache list */
			if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2) {
				int idx;
				BOOLEAN FoundPMK = FALSE;
				/* Search chched PMKID, append it if existed */
				for (idx = 0; idx < PMKID_NO; idx++) {
					if (NdisEqualMemory
					    (ApAddr,
					     &pAd->StaCfg.SavedPMK[idx].BSSID,
					     6)) {
						FoundPMK = TRUE;
						break;
					}
				}
				if (FoundPMK) {
					/* Set PMK number */
					*(u16 *)& pAd->StaCfg.RSN_IE[pAd->
									StaCfg.
									RSNIE_Len]
					    = 1;
					NdisMoveMemory(&pAd->StaCfg.
						       RSN_IE[pAd->StaCfg.
							      RSNIE_Len + 2],
						       &pAd->StaCfg.
						       SavedPMK[idx].PMKID, 16);
					pAd->StaCfg.RSNIE_Len += 18;
				}
			}

			if ((pAd->StaCfg.WpaSupplicantUP ==
			     WPA_SUPPLICANT_ENABLE)
			    && (pAd->StaCfg.bRSN_IE_FromWpaSupplicant ==
				TRUE)) {
				MakeOutgoingFrame(pOutBuffer + FrameLen, &tmp,
						  pAd->StaCfg.RSNIE_Len,
						  pAd->StaCfg.RSN_IE,
						  END_OF_ARGS);
			} else {
				MakeOutgoingFrame(pOutBuffer + FrameLen, &tmp,
						  1, &RSNIe,
						  1, &pAd->StaCfg.RSNIE_Len,
						  pAd->StaCfg.RSNIE_Len,
						  pAd->StaCfg.RSN_IE,
						  END_OF_ARGS);
			}

			FrameLen += tmp;

			if ((pAd->StaCfg.WpaSupplicantUP !=
			     WPA_SUPPLICANT_ENABLE)
			    || (pAd->StaCfg.bRSN_IE_FromWpaSupplicant ==
				FALSE)) {
				/* Append Variable IE */
				NdisMoveMemory(pAd->StaCfg.ReqVarIEs +
					       VarIesOffset, &RSNIe, 1);
				VarIesOffset += 1;
				NdisMoveMemory(pAd->StaCfg.ReqVarIEs +
					       VarIesOffset,
					       &pAd->StaCfg.RSNIE_Len, 1);
				VarIesOffset += 1;
			}
			NdisMoveMemory(pAd->StaCfg.ReqVarIEs + VarIesOffset,
				       pAd->StaCfg.RSN_IE,
				       pAd->StaCfg.RSNIE_Len);
			VarIesOffset += pAd->StaCfg.RSNIE_Len;

			/* Set Variable IEs Length */
			pAd->StaCfg.ReqVarIELen = VarIesOffset;
		}

		MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
		MlmeFreeMemory(pAd, pOutBuffer);

		RTMPSetTimer(&pAd->MlmeAux.AssocTimer, Timeout);
		pAd->Mlme.AssocMachine.CurrState = ASSOC_WAIT_RSP;
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("ASSOC - MlmeAssocReqAction() sanity check failed. BUG!\n"));
		pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_ASSOC_CONF, 2,
			    &Status);
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
void MlmeReassocReqAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	u8 ApAddr[6];
	struct rt_header_802_11 ReassocHdr;
	u8 WmeIe[9] =
	    { IE_VENDOR_SPECIFIC, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01,
       0x00 };
	u16 CapabilityInfo, ListenIntv;
	unsigned long Timeout;
	unsigned long FrameLen = 0;
	BOOLEAN TimerCancelled;
	int NStatus;
	unsigned long tmp;
	u8 *pOutBuffer = NULL;
	u16 Status;

	/* Block all authentication request durning WPA block period */
	if (pAd->StaCfg.bBlockAssoc == TRUE) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("ASSOC - Block ReAssoc request durning WPA block period!\n"));
		pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
		Status = MLME_STATE_MACHINE_REJECT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_REASSOC_CONF, 2,
			    &Status);
	}
	/* the parameters are the same as the association */
	else if (MlmeAssocReqSanity
		 (pAd, Elem->Msg, Elem->MsgLen, ApAddr, &CapabilityInfo,
		  &Timeout, &ListenIntv)) {
		RTMPCancelTimer(&pAd->MlmeAux.ReassocTimer, &TimerCancelled);

		NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);	/*Get an unused nonpaged memory */
		if (NStatus != NDIS_STATUS_SUCCESS) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("ASSOC - MlmeReassocReqAction() allocate memory failed \n"));
			pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
			Status = MLME_FAIL_NO_RESOURCE;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE,
				    MT2_REASSOC_CONF, 2, &Status);
			return;
		}

		COPY_MAC_ADDR(pAd->MlmeAux.Bssid, ApAddr);

		/* make frame, use bssid as the AP address?? */
		DBGPRINT(RT_DEBUG_TRACE,
			 ("ASSOC - Send RE-ASSOC request...\n"));
		MgtMacHeaderInit(pAd, &ReassocHdr, SUBTYPE_REASSOC_REQ, 0,
				 ApAddr, ApAddr);
		MakeOutgoingFrame(pOutBuffer, &FrameLen, sizeof(struct rt_header_802_11),
				  &ReassocHdr, 2, &CapabilityInfo, 2,
				  &ListenIntv, MAC_ADDR_LEN, ApAddr, 1, &SsidIe,
				  1, &pAd->MlmeAux.SsidLen,
				  pAd->MlmeAux.SsidLen, pAd->MlmeAux.Ssid, 1,
				  &SupRateIe, 1, &pAd->MlmeAux.SupRateLen,
				  pAd->MlmeAux.SupRateLen, pAd->MlmeAux.SupRate,
				  END_OF_ARGS);

		if (pAd->MlmeAux.ExtRateLen != 0) {
			MakeOutgoingFrame(pOutBuffer + FrameLen, &tmp,
					  1, &ExtRateIe,
					  1, &pAd->MlmeAux.ExtRateLen,
					  pAd->MlmeAux.ExtRateLen,
					  pAd->MlmeAux.ExtRate, END_OF_ARGS);
			FrameLen += tmp;
		}

		if (pAd->MlmeAux.APEdcaParm.bValid) {
			if (pAd->CommonCfg.bAPSDCapable
			    && pAd->MlmeAux.APEdcaParm.bAPSDCapable) {
				struct rt_qbss_sta_info_parm QosInfo;

				NdisZeroMemory(&QosInfo,
					       sizeof(struct rt_qbss_sta_info_parm));
				QosInfo.UAPSD_AC_BE = pAd->CommonCfg.bAPSDAC_BE;
				QosInfo.UAPSD_AC_BK = pAd->CommonCfg.bAPSDAC_BK;
				QosInfo.UAPSD_AC_VI = pAd->CommonCfg.bAPSDAC_VI;
				QosInfo.UAPSD_AC_VO = pAd->CommonCfg.bAPSDAC_VO;
				QosInfo.MaxSPLength =
				    pAd->CommonCfg.MaxSPLength;
				WmeIe[8] |= *(u8 *)& QosInfo;
			}

			MakeOutgoingFrame(pOutBuffer + FrameLen, &tmp,
					  9, &WmeIe[0], END_OF_ARGS);
			FrameLen += tmp;
		}
		/* HT */
		if ((pAd->MlmeAux.HtCapabilityLen > 0)
		    && (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED)) {
			unsigned long TmpLen;
			u8 HtLen;
			u8 BROADCOM[4] = { 0x0, 0x90, 0x4c, 0x33 };
			if (pAd->StaActive.SupportedPhyInfo.bPreNHt == TRUE) {
				HtLen = SIZE_HT_CAP_IE + 4;
				MakeOutgoingFrame(pOutBuffer + FrameLen,
						  &TmpLen, 1, &WpaIe, 1, &HtLen,
						  4, &BROADCOM[0],
						  pAd->MlmeAux.HtCapabilityLen,
						  &pAd->MlmeAux.HtCapability,
						  END_OF_ARGS);
			} else {
				MakeOutgoingFrame(pOutBuffer + FrameLen,
						  &TmpLen, 1, &HtCapIe, 1,
						  &pAd->MlmeAux.HtCapabilityLen,
						  pAd->MlmeAux.HtCapabilityLen,
						  &pAd->MlmeAux.HtCapability,
						  END_OF_ARGS);
			}
			FrameLen += TmpLen;
		}
		/* add Ralink proprietary IE to inform AP this STA is going to use AGGREGATION or PIGGY-BACK+AGGREGATION */
		/* Case I: (Aggregation + Piggy-Back) */
		/* 1. user enable aggregation, AND */
		/* 2. Mac support piggy-back */
		/* 3. AP annouces it's PIGGY-BACK+AGGREGATION-capable in BEACON */
		/* Case II: (Aggregation) */
		/* 1. user enable aggregation, AND */
		/* 2. AP annouces it's AGGREGATION-capable in BEACON */
		if (pAd->CommonCfg.bAggregationCapable) {
			if ((pAd->CommonCfg.bPiggyBackCapable)
			    && ((pAd->MlmeAux.APRalinkIe & 0x00000003) == 3)) {
				unsigned long TmpLen;
				u8 RalinkIe[9] =
				    { IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43,
			    0x03, 0x00, 0x00, 0x00 };
				MakeOutgoingFrame(pOutBuffer + FrameLen,
						  &TmpLen, 9, RalinkIe,
						  END_OF_ARGS);
				FrameLen += TmpLen;
			} else if (pAd->MlmeAux.APRalinkIe & 0x00000001) {
				unsigned long TmpLen;
				u8 RalinkIe[9] =
				    { IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43,
			    0x01, 0x00, 0x00, 0x00 };
				MakeOutgoingFrame(pOutBuffer + FrameLen,
						  &TmpLen, 9, RalinkIe,
						  END_OF_ARGS);
				FrameLen += TmpLen;
			}
		} else {
			unsigned long TmpLen;
			u8 RalinkIe[9] =
			    { IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43, 0x04,
		    0x00, 0x00, 0x00 };
			MakeOutgoingFrame(pOutBuffer + FrameLen, &TmpLen, 9,
					  RalinkIe, END_OF_ARGS);
			FrameLen += TmpLen;
		}

		MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
		MlmeFreeMemory(pAd, pOutBuffer);

		RTMPSetTimer(&pAd->MlmeAux.ReassocTimer, Timeout);	/* in mSec */
		pAd->Mlme.AssocMachine.CurrState = REASSOC_WAIT_RSP;
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("ASSOC - MlmeReassocReqAction() sanity check failed. BUG!\n"));
		pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_REASSOC_CONF, 2,
			    &Status);
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
void MlmeDisassocReqAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	struct rt_mlme_disassoc_req *pDisassocReq;
	struct rt_header_802_11 DisassocHdr;
	struct rt_header_802_11 * pDisassocHdr;
	u8 *pOutBuffer = NULL;
	unsigned long FrameLen = 0;
	int NStatus;
	BOOLEAN TimerCancelled;
	unsigned long Timeout = 500;
	u16 Status;

	/* skip sanity check */
	pDisassocReq = (struct rt_mlme_disassoc_req *)(Elem->Msg);

	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);	/*Get an unused nonpaged memory */
	if (NStatus != NDIS_STATUS_SUCCESS) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("ASSOC - MlmeDisassocReqAction() allocate memory failed\n"));
		pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
		Status = MLME_FAIL_NO_RESOURCE;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_DISASSOC_CONF, 2,
			    &Status);
		return;
	}

	RTMPCancelTimer(&pAd->MlmeAux.DisassocTimer, &TimerCancelled);

	DBGPRINT(RT_DEBUG_TRACE,
		 ("ASSOC - Send DISASSOC request[BSSID::%02x:%02x:%02x:%02x:%02x:%02x (Reason=%d)\n",
		  pDisassocReq->Addr[0], pDisassocReq->Addr[1],
		  pDisassocReq->Addr[2], pDisassocReq->Addr[3],
		  pDisassocReq->Addr[4], pDisassocReq->Addr[5],
		  pDisassocReq->Reason));
	MgtMacHeaderInit(pAd, &DisassocHdr, SUBTYPE_DISASSOC, 0, pDisassocReq->Addr, pDisassocReq->Addr);	/* patch peap ttls switching issue */
	MakeOutgoingFrame(pOutBuffer, &FrameLen,
			  sizeof(struct rt_header_802_11), &DisassocHdr,
			  2, &pDisassocReq->Reason, END_OF_ARGS);
	MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);

	/* To patch Instance and Buffalo(N) AP */
	/* Driver has to send deauth to Instance AP, but Buffalo(N) needs to send disassoc to reset Authenticator's state machine */
	/* Therefore, we send both of them. */
	pDisassocHdr = (struct rt_header_802_11 *) pOutBuffer;
	pDisassocHdr->FC.SubType = SUBTYPE_DEAUTH;
	MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);

	MlmeFreeMemory(pAd, pOutBuffer);

	pAd->StaCfg.DisassocReason = REASON_DISASSOC_STA_LEAVING;
	COPY_MAC_ADDR(pAd->StaCfg.DisassocSta, pDisassocReq->Addr);

	RTMPSetTimer(&pAd->MlmeAux.DisassocTimer, Timeout);	/* in mSec */
	pAd->Mlme.AssocMachine.CurrState = DISASSOC_WAIT_RSP;

	RtmpOSWrielessEventSend(pAd, SIOCGIWAP, -1, NULL, NULL, 0);

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
void PeerAssocRspAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	u16 CapabilityInfo, Status, Aid;
	u8 SupRate[MAX_LEN_OF_SUPPORTED_RATES], SupRateLen;
	u8 ExtRate[MAX_LEN_OF_SUPPORTED_RATES], ExtRateLen;
	u8 Addr2[MAC_ADDR_LEN];
	BOOLEAN TimerCancelled;
	u8 CkipFlag;
	struct rt_edca_parm EdcaParm;
	struct rt_ht_capability_ie HtCapability;
	struct rt_add_ht_info_ie AddHtInfo;	/* AP might use this additional ht info IE */
	u8 HtCapabilityLen = 0;
	u8 AddHtInfoLen;
	u8 NewExtChannelOffset = 0xff;

	if (PeerAssocRspSanity
	    (pAd, Elem->Msg, Elem->MsgLen, Addr2, &CapabilityInfo, &Status,
	     &Aid, SupRate, &SupRateLen, ExtRate, &ExtRateLen, &HtCapability,
	     &AddHtInfo, &HtCapabilityLen, &AddHtInfoLen, &NewExtChannelOffset,
	     &EdcaParm, &CkipFlag)) {
		/* The frame is for me ? */
		if (MAC_ADDR_EQUAL(Addr2, pAd->MlmeAux.Bssid)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("PeerAssocRspAction():ASSOC - receive ASSOC_RSP to me (status=%d)\n",
				  Status));
			DBGPRINT(RT_DEBUG_TRACE,
				 ("PeerAssocRspAction():MacTable [%d].AMsduSize = %d. ClientStatusFlags = 0x%lx \n",
				  Elem->Wcid,
				  pAd->MacTab.Content[BSSID_WCID].AMsduSize,
				  pAd->MacTab.Content[BSSID_WCID].
				  ClientStatusFlags));
			RTMPCancelTimer(&pAd->MlmeAux.AssocTimer,
					&TimerCancelled);

			if (Status == MLME_SUCCESS) {
				u8 MaxSupportedRateIn500Kbps = 0;
				u8 idx;

				/* supported rates array may not be sorted. sort it and find the maximum rate */
				for (idx = 0; idx < SupRateLen; idx++) {
					if (MaxSupportedRateIn500Kbps <
					    (SupRate[idx] & 0x7f))
						MaxSupportedRateIn500Kbps =
						    SupRate[idx] & 0x7f;
				}

				for (idx = 0; idx < ExtRateLen; idx++) {
					if (MaxSupportedRateIn500Kbps <
					    (ExtRate[idx] & 0x7f))
						MaxSupportedRateIn500Kbps =
						    ExtRate[idx] & 0x7f;
				}
				/* go to procedure listed on page 376 */
				AssocPostProc(pAd, Addr2, CapabilityInfo, Aid,
					      SupRate, SupRateLen, ExtRate,
					      ExtRateLen, &EdcaParm,
					      &HtCapability, HtCapabilityLen,
					      &AddHtInfo);

				StaAddMacTableEntry(pAd,
						    &pAd->MacTab.
						    Content[BSSID_WCID],
						    MaxSupportedRateIn500Kbps,
						    &HtCapability,
						    HtCapabilityLen, &AddHtInfo,
						    AddHtInfoLen,
						    CapabilityInfo);
			}
			pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE,
				    MT2_ASSOC_CONF, 2, &Status);
		}
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("ASSOC - PeerAssocRspAction() sanity check fail\n"));
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
void PeerReassocRspAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	u16 CapabilityInfo;
	u16 Status;
	u16 Aid;
	u8 SupRate[MAX_LEN_OF_SUPPORTED_RATES], SupRateLen;
	u8 ExtRate[MAX_LEN_OF_SUPPORTED_RATES], ExtRateLen;
	u8 Addr2[MAC_ADDR_LEN];
	u8 CkipFlag;
	BOOLEAN TimerCancelled;
	struct rt_edca_parm EdcaParm;
	struct rt_ht_capability_ie HtCapability;
	struct rt_add_ht_info_ie AddHtInfo;	/* AP might use this additional ht info IE */
	u8 HtCapabilityLen;
	u8 AddHtInfoLen;
	u8 NewExtChannelOffset = 0xff;

	if (PeerAssocRspSanity
	    (pAd, Elem->Msg, Elem->MsgLen, Addr2, &CapabilityInfo, &Status,
	     &Aid, SupRate, &SupRateLen, ExtRate, &ExtRateLen, &HtCapability,
	     &AddHtInfo, &HtCapabilityLen, &AddHtInfoLen, &NewExtChannelOffset,
	     &EdcaParm, &CkipFlag)) {
		if (MAC_ADDR_EQUAL(Addr2, pAd->MlmeAux.Bssid))	/* The frame is for me ? */
		{
			DBGPRINT(RT_DEBUG_TRACE,
				 ("ASSOC - receive REASSOC_RSP to me (status=%d)\n",
				  Status));
			RTMPCancelTimer(&pAd->MlmeAux.ReassocTimer,
					&TimerCancelled);

			if (Status == MLME_SUCCESS) {
				/* go to procedure listed on page 376 */
				AssocPostProc(pAd, Addr2, CapabilityInfo, Aid,
					      SupRate, SupRateLen, ExtRate,
					      ExtRateLen, &EdcaParm,
					      &HtCapability, HtCapabilityLen,
					      &AddHtInfo);

				{
					wext_notify_event_assoc(pAd);
					RtmpOSWrielessEventSend(pAd, SIOCGIWAP,
								-1,
								&pAd->MlmeAux.
								Bssid[0], NULL,
								0);
				}

			}
			/* CkipFlag is no use for reassociate */
			pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE,
				    MT2_REASSOC_CONF, 2, &Status);
		}
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("ASSOC - PeerReassocRspAction() sanity check fail\n"));
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
void AssocPostProc(struct rt_rtmp_adapter *pAd, u8 *pAddr2, u16 CapabilityInfo, u16 Aid, u8 SupRate[], u8 SupRateLen, u8 ExtRate[], u8 ExtRateLen, struct rt_edca_parm *pEdcaParm, struct rt_ht_capability_ie * pHtCapability, u8 HtCapabilityLen, struct rt_add_ht_info_ie * pAddHtInfo)	/* AP might use this additional ht info IE */
{
	unsigned long Idx;

	pAd->MlmeAux.BssType = BSS_INFRA;
	COPY_MAC_ADDR(pAd->MlmeAux.Bssid, pAddr2);
	pAd->MlmeAux.Aid = Aid;
	pAd->MlmeAux.CapabilityInfo =
	    CapabilityInfo & SUPPORTED_CAPABILITY_INFO;

	/* Some HT AP might lost WMM IE. We add WMM ourselves. beacuase HT requires QoS on. */
	if ((HtCapabilityLen > 0) && (pEdcaParm->bValid == FALSE)) {
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

		pEdcaParm->Txop[0] = 0;
		pEdcaParm->Txop[1] = 0;
		pEdcaParm->Txop[2] = 96;
		pEdcaParm->Txop[3] = 48;

	}

	NdisMoveMemory(&pAd->MlmeAux.APEdcaParm, pEdcaParm, sizeof(struct rt_edca_parm));

	/* filter out un-supported rates */
	pAd->MlmeAux.SupRateLen = SupRateLen;
	NdisMoveMemory(pAd->MlmeAux.SupRate, SupRate, SupRateLen);
	RTMPCheckRates(pAd, pAd->MlmeAux.SupRate, &pAd->MlmeAux.SupRateLen);

	/* filter out un-supported rates */
	pAd->MlmeAux.ExtRateLen = ExtRateLen;
	NdisMoveMemory(pAd->MlmeAux.ExtRate, ExtRate, ExtRateLen);
	RTMPCheckRates(pAd, pAd->MlmeAux.ExtRate, &pAd->MlmeAux.ExtRateLen);

	if (HtCapabilityLen > 0) {
		RTMPCheckHt(pAd, BSSID_WCID, pHtCapability, pAddHtInfo);
	}
	DBGPRINT(RT_DEBUG_TRACE,
		 ("AssocPostProc===>  AP.AMsduSize = %d. ClientStatusFlags = 0x%lx \n",
		  pAd->MacTab.Content[BSSID_WCID].AMsduSize,
		  pAd->MacTab.Content[BSSID_WCID].ClientStatusFlags));

	DBGPRINT(RT_DEBUG_TRACE,
		 ("AssocPostProc===>    (Mmps=%d, AmsduSize=%d, )\n",
		  pAd->MacTab.Content[BSSID_WCID].MmpsMode,
		  pAd->MacTab.Content[BSSID_WCID].AMsduSize));

	/* Set New WPA information */
	Idx = BssTableSearch(&pAd->ScanTab, pAddr2, pAd->MlmeAux.Channel);
	if (Idx == BSS_NOT_FOUND) {
		DBGPRINT_ERR(("ASSOC - Can't find BSS after receiving Assoc response\n"));
	} else {
		/* Init variable */
		pAd->MacTab.Content[BSSID_WCID].RSNIE_Len = 0;
		NdisZeroMemory(pAd->MacTab.Content[BSSID_WCID].RSN_IE,
			       MAX_LEN_OF_RSNIE);

		/* Store appropriate RSN_IE for WPA SM negotiation later */
		if ((pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPA)
		    && (pAd->ScanTab.BssEntry[Idx].VarIELen != 0)) {
			u8 *pVIE;
			u16 len;
			struct rt_eid * pEid;

			pVIE = pAd->ScanTab.BssEntry[Idx].VarIEs;
			len = pAd->ScanTab.BssEntry[Idx].VarIELen;
			/*KH need to check again */
			/* Don't allow to go to sleep mode if authmode is WPA-related. */
			/*This can make Authentication process more smoothly. */
			RTMP_CLEAR_PSFLAG(pAd, fRTMP_PS_CAN_GO_SLEEP);

			while (len > 0) {
				pEid = (struct rt_eid *) pVIE;
				/* For WPA/WPAPSK */
				if ((pEid->Eid == IE_WPA)
				    &&
				    (NdisEqualMemory(pEid->Octet, WPA_OUI, 4))
				    && (pAd->StaCfg.AuthMode ==
					Ndis802_11AuthModeWPA
					|| pAd->StaCfg.AuthMode ==
					Ndis802_11AuthModeWPAPSK)) {
					NdisMoveMemory(pAd->MacTab.
						       Content[BSSID_WCID].
						       RSN_IE, pVIE,
						       (pEid->Len + 2));
					pAd->MacTab.Content[BSSID_WCID].
					    RSNIE_Len = (pEid->Len + 2);
					DBGPRINT(RT_DEBUG_TRACE,
						 ("AssocPostProc===> Store RSN_IE for WPA SM negotiation \n"));
				}
				/* For WPA2/WPA2PSK */
				else if ((pEid->Eid == IE_RSN)
					 &&
					 (NdisEqualMemory
					  (pEid->Octet + 2, RSN_OUI, 3))
					 && (pAd->StaCfg.AuthMode ==
					     Ndis802_11AuthModeWPA2
					     || pAd->StaCfg.AuthMode ==
					     Ndis802_11AuthModeWPA2PSK)) {
					NdisMoveMemory(pAd->MacTab.
						       Content[BSSID_WCID].
						       RSN_IE, pVIE,
						       (pEid->Len + 2));
					pAd->MacTab.Content[BSSID_WCID].
					    RSNIE_Len = (pEid->Len + 2);
					DBGPRINT(RT_DEBUG_TRACE,
						 ("AssocPostProc===> Store RSN_IE for WPA2 SM negotiation \n"));
				}

				pVIE += (pEid->Len + 2);
				len -= (pEid->Len + 2);
			}

		}

		if (pAd->MacTab.Content[BSSID_WCID].RSNIE_Len == 0) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("AssocPostProc===> no RSN_IE \n"));
		} else {
			hex_dump("RSN_IE",
				 pAd->MacTab.Content[BSSID_WCID].RSN_IE,
				 pAd->MacTab.Content[BSSID_WCID].RSNIE_Len);
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
void PeerDisassocAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	u8 Addr2[MAC_ADDR_LEN];
	u16 Reason;

	DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - PeerDisassocAction()\n"));
	if (PeerDisassocSanity(pAd, Elem->Msg, Elem->MsgLen, Addr2, &Reason)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("ASSOC - PeerDisassocAction() Reason = %d\n",
			  Reason));
		if (INFRA_ON(pAd)
		    && MAC_ADDR_EQUAL(pAd->CommonCfg.Bssid, Addr2)) {

			if (pAd->CommonCfg.bWirelessEvent) {
				RTMPSendWirelessEvent(pAd,
						      IW_DISASSOC_EVENT_FLAG,
						      pAd->MacTab.
						      Content[BSSID_WCID].Addr,
						      BSS0, 0);
			}

			LinkDown(pAd, TRUE);
			pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;

			RtmpOSWrielessEventSend(pAd, SIOCGIWAP, -1, NULL, NULL,
						0);
		}
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("ASSOC - PeerDisassocAction() sanity check fail\n"));
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
void AssocTimeoutAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	u16 Status;
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
void ReassocTimeoutAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	u16 Status;
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
void DisassocTimeoutAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	u16 Status;
	DBGPRINT(RT_DEBUG_TRACE, ("ASSOC - DisassocTimeoutAction\n"));
	pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
	Status = MLME_SUCCESS;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_DISASSOC_CONF, 2,
		    &Status);
}

void InvalidStateWhenAssoc(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	u16 Status;
	DBGPRINT(RT_DEBUG_TRACE,
		 ("ASSOC - InvalidStateWhenAssoc(state=%ld), reset ASSOC state machine\n",
		  pAd->Mlme.AssocMachine.CurrState));
	pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_ASSOC_CONF, 2, &Status);
}

void InvalidStateWhenReassoc(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	u16 Status;
	DBGPRINT(RT_DEBUG_TRACE,
		 ("ASSOC - InvalidStateWhenReassoc(state=%ld), reset ASSOC state machine\n",
		  pAd->Mlme.AssocMachine.CurrState));
	pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_REASSOC_CONF, 2, &Status);
}

void InvalidStateWhenDisassociate(struct rt_rtmp_adapter *pAd,
				  struct rt_mlme_queue_elem *Elem)
{
	u16 Status;
	DBGPRINT(RT_DEBUG_TRACE,
		 ("ASSOC - InvalidStateWhenDisassoc(state=%ld), reset ASSOC state machine\n",
		  pAd->Mlme.AssocMachine.CurrState));
	pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_DISASSOC_CONF, 2,
		    &Status);
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
void Cls3errAction(struct rt_rtmp_adapter *pAd, u8 *pAddr)
{
	struct rt_header_802_11 DisassocHdr;
	struct rt_header_802_11 * pDisassocHdr;
	u8 *pOutBuffer = NULL;
	unsigned long FrameLen = 0;
	int NStatus;
	u16 Reason = REASON_CLS3ERR;

	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);	/*Get an unused nonpaged memory */
	if (NStatus != NDIS_STATUS_SUCCESS)
		return;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("ASSOC - Class 3 Error, Send DISASSOC frame\n"));
	MgtMacHeaderInit(pAd, &DisassocHdr, SUBTYPE_DISASSOC, 0, pAddr, pAd->CommonCfg.Bssid);	/* patch peap ttls switching issue */
	MakeOutgoingFrame(pOutBuffer, &FrameLen,
			  sizeof(struct rt_header_802_11), &DisassocHdr,
			  2, &Reason, END_OF_ARGS);
	MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);

	/* To patch Instance and Buffalo(N) AP */
	/* Driver has to send deauth to Instance AP, but Buffalo(N) needs to send disassoc to reset Authenticator's state machine */
	/* Therefore, we send both of them. */
	pDisassocHdr = (struct rt_header_802_11 *) pOutBuffer;
	pDisassocHdr->FC.SubType = SUBTYPE_DEAUTH;
	MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);

	MlmeFreeMemory(pAd, pOutBuffer);

	pAd->StaCfg.DisassocReason = REASON_CLS3ERR;
	COPY_MAC_ADDR(pAd->StaCfg.DisassocSta, pAddr);
}

int wext_notify_event_assoc(struct rt_rtmp_adapter *pAd)
{
	char custom[IW_CUSTOM_MAX] = { 0 };

	if (pAd->StaCfg.ReqVarIELen <= IW_CUSTOM_MAX) {
		NdisMoveMemory(custom, pAd->StaCfg.ReqVarIEs,
			       pAd->StaCfg.ReqVarIELen);
		RtmpOSWrielessEventSend(pAd, IWEVASSOCREQIE, -1, NULL, custom,
					pAd->StaCfg.ReqVarIELen);
	} else
		DBGPRINT(RT_DEBUG_TRACE,
			 ("pAd->StaCfg.ReqVarIELen > MAX_CUSTOM_LEN\n"));

	return 0;

}

BOOLEAN StaAddMacTableEntry(struct rt_rtmp_adapter *pAd,
			    struct rt_mac_table_entry *pEntry,
			    u8 MaxSupportedRateIn500Kbps,
			    struct rt_ht_capability_ie * pHtCapability,
			    u8 HtCapabilityLen,
			    struct rt_add_ht_info_ie * pAddHtInfo,
			    u8 AddHtInfoLen, u16 CapabilityInfo)
{
	u8 MaxSupportedRate = RATE_11;

	if (ADHOC_ON(pAd))
		CLIENT_STATUS_CLEAR_FLAG(pEntry, fCLIENT_STATUS_WMM_CAPABLE);

	switch (MaxSupportedRateIn500Kbps) {
	case 108:
		MaxSupportedRate = RATE_54;
		break;
	case 96:
		MaxSupportedRate = RATE_48;
		break;
	case 72:
		MaxSupportedRate = RATE_36;
		break;
	case 48:
		MaxSupportedRate = RATE_24;
		break;
	case 36:
		MaxSupportedRate = RATE_18;
		break;
	case 24:
		MaxSupportedRate = RATE_12;
		break;
	case 18:
		MaxSupportedRate = RATE_9;
		break;
	case 12:
		MaxSupportedRate = RATE_6;
		break;
	case 22:
		MaxSupportedRate = RATE_11;
		break;
	case 11:
		MaxSupportedRate = RATE_5_5;
		break;
	case 4:
		MaxSupportedRate = RATE_2;
		break;
	case 2:
		MaxSupportedRate = RATE_1;
		break;
	default:
		MaxSupportedRate = RATE_11;
		break;
	}

	if ((pAd->CommonCfg.PhyMode == PHY_11G)
	    && (MaxSupportedRate < RATE_FIRST_OFDM_RATE))
		return FALSE;

	/* 11n only */
	if (((pAd->CommonCfg.PhyMode == PHY_11N_2_4G)
	     || (pAd->CommonCfg.PhyMode == PHY_11N_5G))
	    && (HtCapabilityLen == 0))
		return FALSE;

	if (!pEntry)
		return FALSE;

	NdisAcquireSpinLock(&pAd->MacTabLock);
	if (pEntry) {
		pEntry->PortSecured = WPA_802_1X_PORT_SECURED;
		if ((MaxSupportedRate < RATE_FIRST_OFDM_RATE) ||
		    (pAd->CommonCfg.PhyMode == PHY_11B)) {
			pEntry->RateLen = 4;
			if (MaxSupportedRate >= RATE_FIRST_OFDM_RATE)
				MaxSupportedRate = RATE_11;
		} else
			pEntry->RateLen = 12;

		pEntry->MaxHTPhyMode.word = 0;
		pEntry->MinHTPhyMode.word = 0;
		pEntry->HTPhyMode.word = 0;
		pEntry->MaxSupportedRate = MaxSupportedRate;
		if (pEntry->MaxSupportedRate < RATE_FIRST_OFDM_RATE) {
			pEntry->MaxHTPhyMode.field.MODE = MODE_CCK;
			pEntry->MaxHTPhyMode.field.MCS =
			    pEntry->MaxSupportedRate;
			pEntry->MinHTPhyMode.field.MODE = MODE_CCK;
			pEntry->MinHTPhyMode.field.MCS =
			    pEntry->MaxSupportedRate;
			pEntry->HTPhyMode.field.MODE = MODE_CCK;
			pEntry->HTPhyMode.field.MCS = pEntry->MaxSupportedRate;
		} else {
			pEntry->MaxHTPhyMode.field.MODE = MODE_OFDM;
			pEntry->MaxHTPhyMode.field.MCS =
			    OfdmRateToRxwiMCS[pEntry->MaxSupportedRate];
			pEntry->MinHTPhyMode.field.MODE = MODE_OFDM;
			pEntry->MinHTPhyMode.field.MCS =
			    OfdmRateToRxwiMCS[pEntry->MaxSupportedRate];
			pEntry->HTPhyMode.field.MODE = MODE_OFDM;
			pEntry->HTPhyMode.field.MCS =
			    OfdmRateToRxwiMCS[pEntry->MaxSupportedRate];
		}
		pEntry->CapabilityInfo = CapabilityInfo;
		CLIENT_STATUS_CLEAR_FLAG(pEntry,
					 fCLIENT_STATUS_AGGREGATION_CAPABLE);
		CLIENT_STATUS_CLEAR_FLAG(pEntry,
					 fCLIENT_STATUS_PIGGYBACK_CAPABLE);
	}

	NdisZeroMemory(&pEntry->HTCapability, sizeof(pEntry->HTCapability));
	/* If this Entry supports 802.11n, upgrade to HT rate. */
	if ((HtCapabilityLen != 0)
	    && (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED)) {
		u8 j, bitmask;	/*k,bitmask; */
		char i;

		if (ADHOC_ON(pAd))
			CLIENT_STATUS_SET_FLAG(pEntry,
					       fCLIENT_STATUS_WMM_CAPABLE);
		if ((pHtCapability->HtCapInfo.GF)
		    && (pAd->CommonCfg.DesiredHtPhy.GF)) {
			pEntry->MaxHTPhyMode.field.MODE = MODE_HTGREENFIELD;
		} else {
			pEntry->MaxHTPhyMode.field.MODE = MODE_HTMIX;
			pAd->MacTab.fAnyStationNonGF = TRUE;
			pAd->CommonCfg.AddHTInfo.AddHtInfo2.NonGfPresent = 1;
		}

		if ((pHtCapability->HtCapInfo.ChannelWidth) &&
		    (pAd->CommonCfg.DesiredHtPhy.ChannelWidth) &&
		    ((pAd->StaCfg.BssType == BSS_INFRA)
		     || ((pAd->StaCfg.BssType == BSS_ADHOC)
			 && (pAddHtInfo->AddHtInfo.ExtChanOffset ==
			     pAd->CommonCfg.AddHTInfo.AddHtInfo.
			     ExtChanOffset)))) {
			pEntry->MaxHTPhyMode.field.BW = BW_40;
			pEntry->MaxHTPhyMode.field.ShortGI =
			    ((pAd->CommonCfg.DesiredHtPhy.
			      ShortGIfor40) & (pHtCapability->HtCapInfo.
					       ShortGIfor40));
		} else {
			pEntry->MaxHTPhyMode.field.BW = BW_20;
			pEntry->MaxHTPhyMode.field.ShortGI =
			    ((pAd->CommonCfg.DesiredHtPhy.
			      ShortGIfor20) & (pHtCapability->HtCapInfo.
					       ShortGIfor20));
			pAd->MacTab.fAnyStation20Only = TRUE;
		}

		/* 3*3 */
		if (pAd->MACVersion >= RALINK_2883_VERSION
		    && pAd->MACVersion < RALINK_3070_VERSION)
			pEntry->MaxHTPhyMode.field.TxBF =
			    pAd->CommonCfg.RegTransmitSetting.field.TxBF;

		/* find max fixed rate */
		for (i = 23; i >= 0; i--)	/* 3*3 */
		{
			j = i / 8;
			bitmask = (1 << (i - (j * 8)));
			if ((pAd->StaCfg.DesiredHtPhyInfo.MCSSet[j] & bitmask)
			    && (pHtCapability->MCSSet[j] & bitmask)) {
				pEntry->MaxHTPhyMode.field.MCS = i;
				break;
			}
			if (i == 0)
				break;
		}

		if (pAd->StaCfg.DesiredTransmitSetting.field.MCS != MCS_AUTO) {
			if (pAd->StaCfg.DesiredTransmitSetting.field.MCS == 32) {
				/* Fix MCS as HT Duplicated Mode */
				pEntry->MaxHTPhyMode.field.BW = 1;
				pEntry->MaxHTPhyMode.field.MODE = MODE_HTMIX;
				pEntry->MaxHTPhyMode.field.STBC = 0;
				pEntry->MaxHTPhyMode.field.ShortGI = 0;
				pEntry->MaxHTPhyMode.field.MCS = 32;
			} else if (pEntry->MaxHTPhyMode.field.MCS >
				   pAd->StaCfg.HTPhyMode.field.MCS) {
				/* STA supports fixed MCS */
				pEntry->MaxHTPhyMode.field.MCS =
				    pAd->StaCfg.HTPhyMode.field.MCS;
			}
		}

		pEntry->MaxHTPhyMode.field.STBC =
		    (pHtCapability->HtCapInfo.
		     RxSTBC & (pAd->CommonCfg.DesiredHtPhy.TxSTBC));
		pEntry->MpduDensity = pHtCapability->HtCapParm.MpduDensity;
		pEntry->MaxRAmpduFactor =
		    pHtCapability->HtCapParm.MaxRAmpduFactor;
		pEntry->MmpsMode = (u8)pHtCapability->HtCapInfo.MimoPs;
		pEntry->AMsduSize = (u8)pHtCapability->HtCapInfo.AMsduSize;
		pEntry->HTPhyMode.word = pEntry->MaxHTPhyMode.word;

		if (pAd->CommonCfg.DesiredHtPhy.AmsduEnable
		    && (pAd->CommonCfg.REGBACapability.field.AutoBA == FALSE))
			CLIENT_STATUS_SET_FLAG(pEntry,
					       fCLIENT_STATUS_AMSDU_INUSED);
		if (pHtCapability->HtCapInfo.ShortGIfor20)
			CLIENT_STATUS_SET_FLAG(pEntry,
					       fCLIENT_STATUS_SGI20_CAPABLE);
		if (pHtCapability->HtCapInfo.ShortGIfor40)
			CLIENT_STATUS_SET_FLAG(pEntry,
					       fCLIENT_STATUS_SGI40_CAPABLE);
		if (pHtCapability->HtCapInfo.TxSTBC)
			CLIENT_STATUS_SET_FLAG(pEntry,
					       fCLIENT_STATUS_TxSTBC_CAPABLE);
		if (pHtCapability->HtCapInfo.RxSTBC)
			CLIENT_STATUS_SET_FLAG(pEntry,
					       fCLIENT_STATUS_RxSTBC_CAPABLE);
		if (pHtCapability->ExtHtCapInfo.PlusHTC)
			CLIENT_STATUS_SET_FLAG(pEntry,
					       fCLIENT_STATUS_HTC_CAPABLE);
		if (pAd->CommonCfg.bRdg
		    && pHtCapability->ExtHtCapInfo.RDGSupport)
			CLIENT_STATUS_SET_FLAG(pEntry,
					       fCLIENT_STATUS_RDG_CAPABLE);
		if (pHtCapability->ExtHtCapInfo.MCSFeedback == 0x03)
			CLIENT_STATUS_SET_FLAG(pEntry,
					       fCLIENT_STATUS_MCSFEEDBACK_CAPABLE);
		NdisMoveMemory(&pEntry->HTCapability, pHtCapability,
			       HtCapabilityLen);
	} else {
		pAd->MacTab.fAnyStationIsLegacy = TRUE;
	}

	pEntry->HTPhyMode.word = pEntry->MaxHTPhyMode.word;
	pEntry->CurrTxRate = pEntry->MaxSupportedRate;

	/* Set asic auto fall back */
	if (pAd->StaCfg.bAutoTxRateSwitch == TRUE) {
		u8 *pTable;
		u8 TableSize = 0;

		MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize,
				      &pEntry->CurrTxRateIndex);
		pEntry->bAutoTxRateSwitch = TRUE;
	} else {
		pEntry->HTPhyMode.field.MODE = pAd->StaCfg.HTPhyMode.field.MODE;
		pEntry->HTPhyMode.field.MCS = pAd->StaCfg.HTPhyMode.field.MCS;
		pEntry->bAutoTxRateSwitch = FALSE;

		/* If the legacy mode is set, overwrite the transmit setting of this entry. */
		RTMPUpdateLegacyTxSetting((u8)pAd->StaCfg.
					  DesiredTransmitSetting.field.
					  FixedTxMode, pEntry);
	}

	pEntry->PortSecured = WPA_802_1X_PORT_SECURED;
	pEntry->Sst = SST_ASSOC;
	pEntry->AuthState = AS_AUTH_OPEN;
	pEntry->AuthMode = pAd->StaCfg.AuthMode;
	pEntry->WepStatus = pAd->StaCfg.WepStatus;

	NdisReleaseSpinLock(&pAd->MacTabLock);

	{
		union iwreq_data wrqu;
		wext_notify_event_assoc(pAd);

		memset(wrqu.ap_addr.sa_data, 0, MAC_ADDR_LEN);
		memcpy(wrqu.ap_addr.sa_data, pAd->MlmeAux.Bssid, MAC_ADDR_LEN);
		wireless_send_event(pAd->net_dev, SIOCGIWAP, &wrqu, NULL);

	}
	return TRUE;
}
