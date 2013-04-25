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

/*
    ==========================================================================
    Description:
        authenticate state machine init, including state transition and timer init
    Parameters:
        Sm - pointer to the auth state machine
    Note:
        The state machine looks like this
        
                        AUTH_REQ_IDLE           AUTH_WAIT_SEQ2                   AUTH_WAIT_SEQ4
    MT2_MLME_AUTH_REQ   mlme_auth_req_action    invalid_state_when_auth          invalid_state_when_auth
    MT2_PEER_AUTH_EVEN  drop                    peer_auth_even_at_seq2_action    peer_auth_even_at_seq4_action
    MT2_AUTH_TIMEOUT    Drop                    auth_timeout_action              auth_timeout_action
        
	IRQL = PASSIVE_LEVEL

    ==========================================================================
 */

void AuthStateMachineInit(
	IN PRTMP_ADAPTER pAd,
	IN STATE_MACHINE *Sm,
	OUT STATE_MACHINE_FUNC Trans[])
{
	StateMachineInit(Sm, Trans, MAX_AUTH_STATE, MAX_AUTH_MSG,
			 (STATE_MACHINE_FUNC) Drop, AUTH_REQ_IDLE,
			 AUTH_MACHINE_BASE);

	/* the first column */
	StateMachineSetAction(Sm, AUTH_REQ_IDLE, MT2_MLME_AUTH_REQ,
			      (STATE_MACHINE_FUNC) MlmeAuthReqAction);

	/* the second column */
	StateMachineSetAction(Sm, AUTH_WAIT_SEQ2, MT2_MLME_AUTH_REQ,
			      (STATE_MACHINE_FUNC) InvalidStateWhenAuth);
	StateMachineSetAction(Sm, AUTH_WAIT_SEQ2, MT2_PEER_AUTH_EVEN,
			      (STATE_MACHINE_FUNC) PeerAuthRspAtSeq2Action);
	StateMachineSetAction(Sm, AUTH_WAIT_SEQ2, MT2_AUTH_TIMEOUT,
			      (STATE_MACHINE_FUNC) AuthTimeoutAction);

	/* the third column */
	StateMachineSetAction(Sm, AUTH_WAIT_SEQ4, MT2_MLME_AUTH_REQ,
			      (STATE_MACHINE_FUNC) InvalidStateWhenAuth);
	StateMachineSetAction(Sm, AUTH_WAIT_SEQ4, MT2_PEER_AUTH_EVEN,
			      (STATE_MACHINE_FUNC) PeerAuthRspAtSeq4Action);
	StateMachineSetAction(Sm, AUTH_WAIT_SEQ4, MT2_AUTH_TIMEOUT,
			      (STATE_MACHINE_FUNC) AuthTimeoutAction);

	RTMPInitTimer(pAd, &pAd->MlmeAux.AuthTimer,
		      GET_TIMER_FUNCTION(AuthTimeout), pAd, FALSE);
}

/*
    ==========================================================================
    Description:
        function to be executed at timer thread when auth timer expires
        
	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID AuthTimeout(
	IN PVOID SystemSpecific1,
	IN PVOID FunctionContext,
	IN PVOID SystemSpecific2,
	IN PVOID SystemSpecific3)
{
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *) FunctionContext;

	DBGPRINT(RT_DEBUG_TRACE, ("AUTH - AuthTimeout\n"));

	/* Do nothing if the driver is starting halt state. */
	/* This might happen when timer already been fired before cancel timer with mlmehalt */
	if (RTMP_TEST_FLAG
	    (pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

	/* send a de-auth to reset AP's state machine (Patch AP-Dir635) */
	if (pAd->Mlme.AuthMachine.CurrState == AUTH_WAIT_SEQ2)
		Cls2errAction(pAd, pAd->MlmeAux.Bssid);

	MlmeEnqueue(pAd, AUTH_STATE_MACHINE, MT2_AUTH_TIMEOUT, 0, NULL, 0);
	RTMP_MLME_HANDLER(pAd);
}

/*
    ==========================================================================
    Description:
        
	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID MlmeAuthReqAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	if (AUTH_ReqSend
	    (pAd, Elem, &pAd->MlmeAux.AuthTimer, "AUTH", 1, NULL, 0))
		pAd->Mlme.AuthMachine.CurrState = AUTH_WAIT_SEQ2;
	else {
		USHORT Status;

		pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_AUTH_CONF, 2,
			    &Status, 0);
	}
}

/*
    ==========================================================================
    Description:
        
	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID PeerAuthRspAtSeq2Action(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM * Elem)
{
	UCHAR Addr2[MAC_ADDR_LEN];
	USHORT Seq, Status, RemoteStatus, Alg;
	UCHAR iv_hdr[4];
/*    UCHAR         ChlgText[CIPHER_TEXT_LEN]; */
	UCHAR *ChlgText = NULL;
/*    UCHAR         CyperChlgText[CIPHER_TEXT_LEN + 8 + 8]; */
	UCHAR *CyperChlgText = NULL;
	ULONG c_len = 0;
	HEADER_802_11 AuthHdr;
	BOOLEAN TimerCancelled;
	PUCHAR pOutBuffer = NULL;
	NDIS_STATUS NStatus;
	ULONG FrameLen = 0;
	USHORT Status2;
	UCHAR ChallengeIe = IE_CHALLENGE_TEXT;
	UCHAR len_challengeText = CIPHER_TEXT_LEN;

	/* allocate memory */
	os_alloc_mem(NULL, (UCHAR **) & ChlgText, CIPHER_TEXT_LEN);
	if (ChlgText == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: ChlgText Allocate memory fail!!!\n",
			  __FUNCTION__));
		return;
	}

	os_alloc_mem(NULL, (UCHAR **) & CyperChlgText, CIPHER_TEXT_LEN + 8 + 8);
	if (CyperChlgText == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: CyperChlgText Allocate memory fail!!!\n",
			  __FUNCTION__));
		os_free_mem(NULL, ChlgText);
		return;
	}

	if (PeerAuthSanity
	    (pAd, Elem->Msg, Elem->MsgLen, Addr2, &Alg, &Seq, &Status,
	     (PCHAR) ChlgText)) {
		if (MAC_ADDR_EQUAL(pAd->MlmeAux.Bssid, Addr2) && Seq == 2) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("AUTH - Receive AUTH_RSP seq#2 to me (Alg=%d, Status=%d)\n",
				  Alg, Status));
			RTMPCancelTimer(&pAd->MlmeAux.AuthTimer,
					&TimerCancelled);

			if (Status == MLME_SUCCESS) {
				/* Authentication Mode "LEAP" has allow for CCX 1.X */
				if (pAd->MlmeAux.Alg == Ndis802_11AuthModeOpen) {
					pAd->Mlme.AuthMachine.CurrState =
					    AUTH_REQ_IDLE;
					MlmeEnqueue(pAd,
						    MLME_CNTL_STATE_MACHINE,
						    MT2_AUTH_CONF, 2, &Status,
						    0);
				} else {
					/* 2. shared key, need to be challenged */
					Seq++;
					RemoteStatus = MLME_SUCCESS;

					/* Get an unused nonpaged memory */
					NStatus =
					    MlmeAllocateMemory(pAd,
							       &pOutBuffer);
					if (NStatus != NDIS_STATUS_SUCCESS) {
						DBGPRINT(RT_DEBUG_TRACE,
							 ("AUTH - PeerAuthRspAtSeq2Action() allocate memory fail\n"));
						pAd->Mlme.AuthMachine.
						    CurrState = AUTH_REQ_IDLE;
						Status2 = MLME_FAIL_NO_RESOURCE;
						MlmeEnqueue(pAd,
							    MLME_CNTL_STATE_MACHINE,
							    MT2_AUTH_CONF, 2,
							    &Status2, 0);
						goto LabelOK;
					}

					DBGPRINT(RT_DEBUG_TRACE,
						 ("AUTH - Send AUTH request seq#3...\n"));
					MgtMacHeaderInit(pAd, &AuthHdr,
							 SUBTYPE_AUTH, 0, Addr2,
							 pAd->MlmeAux.Bssid);
					AuthHdr.FC.Wep = 1;

					/* TSC increment */
					INC_TX_TSC(pAd->
						   SharedKey[BSS0][pAd->StaCfg.
								   DefaultKeyId].
						   TxTsc, LEN_WEP_TSC);

					/* Construct the 4-bytes WEP IV header */
					RTMPConstructWEPIVHdr(pAd->StaCfg.
							      DefaultKeyId,
							      pAd->
							      SharedKey[BSS0]
							      [pAd->StaCfg.
							       DefaultKeyId].
							      TxTsc, iv_hdr);

					Alg = cpu2le16(*(USHORT *) & Alg);
					Seq = cpu2le16(*(USHORT *) & Seq);
					RemoteStatus =
					    cpu2le16(*(USHORT *) &
						     RemoteStatus);

					/* Construct message text */
					MakeOutgoingFrame(CyperChlgText, &c_len,
							  2, &Alg,
							  2, &Seq,
							  2, &RemoteStatus,
							  1, &ChallengeIe,
							  1, &len_challengeText,
							  len_challengeText,
							  ChlgText,
							  END_OF_ARGS);

					if (RTMPSoftEncryptWEP(pAd,
							       iv_hdr,
							       &pAd->
							       SharedKey[BSS0]
							       [pAd->StaCfg.
								DefaultKeyId],
							       CyperChlgText,
							       c_len) ==
					    FALSE) {
						MlmeFreeMemory(pAd, pOutBuffer);
						pAd->Mlme.AuthMachine.
						    CurrState = AUTH_REQ_IDLE;
						Status2 = MLME_FAIL_NO_RESOURCE;
						MlmeEnqueue(pAd,
							    MLME_CNTL_STATE_MACHINE,
							    MT2_AUTH_CONF, 2,
							    &Status2, 0);
						goto LabelOK;
					}

					/* Update the total length for 4-bytes ICV */
					c_len += LEN_ICV;

					MakeOutgoingFrame(pOutBuffer, &FrameLen,
							  sizeof
							  (HEADER_802_11),
							  &AuthHdr,
							  LEN_WEP_IV_HDR,
							  iv_hdr, c_len,
							  CyperChlgText,
							  END_OF_ARGS);

					MiniportMMRequest(pAd, 0, pOutBuffer,
							  FrameLen);
					MlmeFreeMemory(pAd, pOutBuffer);

					RTMPSetTimer(&pAd->MlmeAux.AuthTimer,
						     AUTH_TIMEOUT);
					pAd->Mlme.AuthMachine.CurrState =
					    AUTH_WAIT_SEQ4;
				}
			} else {
				pAd->StaCfg.AuthFailReason = Status;
				COPY_MAC_ADDR(pAd->StaCfg.AuthFailSta, Addr2);
				pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
				MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE,
					    MT2_AUTH_CONF, 2, &Status, 0);
			}
		}
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("AUTH - PeerAuthSanity() sanity check fail\n"));
	}

      LabelOK:
	if (ChlgText != NULL)
		os_free_mem(NULL, ChlgText);

	if (CyperChlgText != NULL)
		os_free_mem(NULL, CyperChlgText);
	return;
}

/*
    ==========================================================================
    Description:
        
	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID PeerAuthRspAtSeq4Action(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	UCHAR Addr2[MAC_ADDR_LEN];
	USHORT Alg, Seq, Status;
/*    CHAR          ChlgText[CIPHER_TEXT_LEN]; */
	CHAR *ChlgText = NULL;
	BOOLEAN TimerCancelled;

	/* allocate memory */
	os_alloc_mem(NULL, (UCHAR **) & ChlgText, CIPHER_TEXT_LEN);
	if (ChlgText == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: ChlgText Allocate memory fail!!!\n",
			  __FUNCTION__));
		return;
	}

	if (PeerAuthSanity
	    (pAd, Elem->Msg, Elem->MsgLen, Addr2, &Alg, &Seq, &Status,
	     ChlgText)) {
		if (MAC_ADDR_EQUAL(pAd->MlmeAux.Bssid, Addr2) && Seq == 4) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("AUTH - Receive AUTH_RSP seq#4 to me\n"));
			RTMPCancelTimer(&pAd->MlmeAux.AuthTimer,
					&TimerCancelled);

			if (Status != MLME_SUCCESS) {
				pAd->StaCfg.AuthFailReason = Status;
				COPY_MAC_ADDR(pAd->StaCfg.AuthFailSta, Addr2);
				RTMPSendWirelessEvent(pAd, IW_SHARED_WEP_FAIL,
						      NULL, BSS0, 0);
			}

			pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_AUTH_CONF,
				    2, &Status, 0);
		}
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("AUTH - PeerAuthRspAtSeq4Action() sanity check fail\n"));
	}

	if (ChlgText != NULL)
		os_free_mem(NULL, ChlgText);
}

/*
    ==========================================================================
    Description:
        
	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID MlmeDeauthReqAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	MLME_DEAUTH_REQ_STRUCT *pInfo;
	HEADER_802_11 DeauthHdr;
	PUCHAR pOutBuffer = NULL;
	NDIS_STATUS NStatus;
	ULONG FrameLen = 0;
	USHORT Status;

	pInfo = (MLME_DEAUTH_REQ_STRUCT *) Elem->Msg;

	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);	/*Get an unused nonpaged memory */
	if (NStatus != NDIS_STATUS_SUCCESS) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("AUTH - MlmeDeauthReqAction() allocate memory fail\n"));
		pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
		Status = MLME_FAIL_NO_RESOURCE;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_DEAUTH_CONF, 2,
			    &Status, 0);
		return;
	}

	DBGPRINT(RT_DEBUG_TRACE,
		 ("AUTH - Send DE-AUTH request (Reason=%d)...\n",
		  pInfo->Reason));
	MgtMacHeaderInit(pAd, &DeauthHdr, SUBTYPE_DEAUTH, 0, pInfo->Addr,
						pAd->MlmeAux.Bssid);
	MakeOutgoingFrame(pOutBuffer, &FrameLen, sizeof (HEADER_802_11),
			  &DeauthHdr, 2, &pInfo->Reason, END_OF_ARGS);
	MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);

	pAd->StaCfg.DeauthReason = pInfo->Reason;
	COPY_MAC_ADDR(pAd->StaCfg.DeauthSta, pInfo->Addr);
	pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
	Status = MLME_SUCCESS;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_DEAUTH_CONF, 2, &Status,
		    0);

	/* send wireless event - for deauthentication */
	RTMPSendWirelessEvent(pAd, IW_DEAUTH_EVENT_FLAG, NULL, BSS0, 0);
}

/*
    ==========================================================================
    Description:
        
	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID AuthTimeoutAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT Status;
	DBGPRINT(RT_DEBUG_TRACE, ("AUTH - AuthTimeoutAction\n"));
	pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
	Status = MLME_REJ_TIMEOUT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_AUTH_CONF, 2, &Status, 0);
}

/*
    ==========================================================================
    Description:
        
	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID InvalidStateWhenAuth(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT Status;
	DBGPRINT(RT_DEBUG_TRACE,
		 ("AUTH - InvalidStateWhenAuth (state=%ld), reset AUTH state machine\n",
		  pAd->Mlme.AuthMachine.CurrState));
	pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_AUTH_CONF, 2, &Status, 0);
}

/*
    ==========================================================================
    Description:
        Some STA/AP
    Note:
        This action should never trigger AUTH state transition, therefore we
        separate it from AUTH state machine, and make it as a standalone service
        
	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID Cls2errAction(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pAddr)
{
	HEADER_802_11 DeauthHdr;
	PUCHAR pOutBuffer = NULL;
	NDIS_STATUS NStatus;
	ULONG FrameLen = 0;
	USHORT Reason = REASON_CLS2ERR;

	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);	/*Get an unused nonpaged memory */
	if (NStatus != NDIS_STATUS_SUCCESS)
		return;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("AUTH - Class 2 error, Send DEAUTH frame...\n"));
	MgtMacHeaderInit(pAd, &DeauthHdr, SUBTYPE_DEAUTH, 0, pAddr,
						pAd->MlmeAux.Bssid);
	MakeOutgoingFrame(pOutBuffer, &FrameLen, sizeof (HEADER_802_11),
			  &DeauthHdr, 2, &Reason, END_OF_ARGS);
	MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);

	pAd->StaCfg.DeauthReason = Reason;
	COPY_MAC_ADDR(pAd->StaCfg.DeauthSta, pAddr);
}

BOOLEAN AUTH_ReqSend(
	IN PRTMP_ADAPTER pAd,
	IN PMLME_QUEUE_ELEM pElem,
	IN PRALINK_TIMER_STRUCT pAuthTimer,
	IN PSTRING pSMName,
	IN USHORT SeqNo,
	IN PUCHAR pNewElement,
	IN ULONG ElementLen)
{
	USHORT Alg, Seq, Status;
	UCHAR Addr[6];
	ULONG Timeout;
	HEADER_802_11 AuthHdr;
	BOOLEAN TimerCancelled;
	NDIS_STATUS NStatus;
	PUCHAR pOutBuffer = NULL;
	ULONG FrameLen = 0, tmp = 0;

	/* Block all authentication request durning WPA block period */
	if (pAd->StaCfg.bBlockAssoc == TRUE) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s - Block Auth request durning WPA block period!\n",
			  pSMName));
		pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
		Status = MLME_STATE_MACHINE_REJECT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_AUTH_CONF, 2,
			    &Status, 0);
	} else
	    if (MlmeAuthReqSanity
		(pAd, pElem->Msg, pElem->MsgLen, Addr, &Timeout, &Alg)) {
		/* reset timer */
		RTMPCancelTimer(pAuthTimer, &TimerCancelled);

		COPY_MAC_ADDR(pAd->MlmeAux.Bssid, Addr);
		pAd->MlmeAux.Alg = Alg;
		Seq = SeqNo;
		Status = MLME_SUCCESS;

		NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);	/*Get an unused nonpaged memory */
		if (NStatus != NDIS_STATUS_SUCCESS) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("%s - MlmeAuthReqAction(Alg:%d) allocate memory failed\n",
				  pSMName, Alg));
			pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
			Status = MLME_FAIL_NO_RESOURCE;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_AUTH_CONF,
				    2, &Status, 0);
			return FALSE;
		}

		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s - Send AUTH request seq#1 (Alg=%d)...\n", pSMName,
			  Alg));
		MgtMacHeaderInit(pAd, &AuthHdr, SUBTYPE_AUTH, 0, Addr,
							pAd->MlmeAux.Bssid);
		MakeOutgoingFrame(pOutBuffer, &FrameLen, sizeof (HEADER_802_11),
				  &AuthHdr, 2, &Alg, 2, &Seq, 2, &Status,
				  END_OF_ARGS);

		if (pNewElement && ElementLen) {
			MakeOutgoingFrame(pOutBuffer + FrameLen, &tmp,
					  ElementLen, pNewElement, END_OF_ARGS);
			FrameLen += tmp;
		}

		MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
		MlmeFreeMemory(pAd, pOutBuffer);

		RTMPSetTimer(pAuthTimer, Timeout);
		return TRUE;
	} else {
		DBGPRINT_ERR(("%s - MlmeAuthReqAction() sanity check failed\n",
			      pSMName));
		return FALSE;
	}

	return TRUE;
}
