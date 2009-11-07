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
	wpa.c

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Jan	Lee		03-07-22		Initial
	Paul Lin	03-11-28		Modify for supplicant
*/
#include "../rt_config.h"
/* WPA OUI */
u8 OUI_WPA_NONE_AKM[4] = { 0x00, 0x50, 0xF2, 0x00 };
u8 OUI_WPA_VERSION[4] = { 0x00, 0x50, 0xF2, 0x01 };
u8 OUI_WPA_WEP40[4] = { 0x00, 0x50, 0xF2, 0x01 };
u8 OUI_WPA_TKIP[4] = { 0x00, 0x50, 0xF2, 0x02 };
u8 OUI_WPA_CCMP[4] = { 0x00, 0x50, 0xF2, 0x04 };
u8 OUI_WPA_WEP104[4] = { 0x00, 0x50, 0xF2, 0x05 };
u8 OUI_WPA_8021X_AKM[4] = { 0x00, 0x50, 0xF2, 0x01 };
u8 OUI_WPA_PSK_AKM[4] = { 0x00, 0x50, 0xF2, 0x02 };

/* WPA2 OUI */
u8 OUI_WPA2_WEP40[4] = { 0x00, 0x0F, 0xAC, 0x01 };
u8 OUI_WPA2_TKIP[4] = { 0x00, 0x0F, 0xAC, 0x02 };
u8 OUI_WPA2_CCMP[4] = { 0x00, 0x0F, 0xAC, 0x04 };
u8 OUI_WPA2_8021X_AKM[4] = { 0x00, 0x0F, 0xAC, 0x01 };
u8 OUI_WPA2_PSK_AKM[4] = { 0x00, 0x0F, 0xAC, 0x02 };
u8 OUI_WPA2_WEP104[4] = { 0x00, 0x0F, 0xAC, 0x05 };

static void ConstructEapolKeyData(struct rt_mac_table_entry *pEntry,
				  u8 GroupKeyWepStatus,
				  u8 keyDescVer,
				  u8 MsgType,
				  u8 DefaultKeyIdx,
				  u8 * GTK,
				  u8 * RSNIE,
				  u8 RSNIE_LEN, struct rt_eapol_packet * pMsg);

static void CalculateMIC(u8 KeyDescVer,
			 u8 * PTK, struct rt_eapol_packet * pMsg);

static void WpaEAPPacketAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem);

static void WpaEAPOLASFAlertAction(struct rt_rtmp_adapter *pAd,
				   struct rt_mlme_queue_elem *Elem);

static void WpaEAPOLLogoffAction(struct rt_rtmp_adapter *pAd,
				 struct rt_mlme_queue_elem *Elem);

static void WpaEAPOLStartAction(struct rt_rtmp_adapter *pAd,
				struct rt_mlme_queue_elem *Elem);

static void WpaEAPOLKeyAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem);

/*
    ==========================================================================
    Description:
        association state machine init, including state transition and timer init
    Parameters:
        S - pointer to the association state machine
    ==========================================================================
 */
void WpaStateMachineInit(struct rt_rtmp_adapter *pAd,
			 struct rt_state_machine *S, OUT STATE_MACHINE_FUNC Trans[])
{
	StateMachineInit(S, (STATE_MACHINE_FUNC *) Trans, MAX_WPA_PTK_STATE,
			 MAX_WPA_MSG, (STATE_MACHINE_FUNC) Drop, WPA_PTK,
			 WPA_MACHINE_BASE);

	StateMachineSetAction(S, WPA_PTK, MT2_EAPPacket,
			      (STATE_MACHINE_FUNC) WpaEAPPacketAction);
	StateMachineSetAction(S, WPA_PTK, MT2_EAPOLStart,
			      (STATE_MACHINE_FUNC) WpaEAPOLStartAction);
	StateMachineSetAction(S, WPA_PTK, MT2_EAPOLLogoff,
			      (STATE_MACHINE_FUNC) WpaEAPOLLogoffAction);
	StateMachineSetAction(S, WPA_PTK, MT2_EAPOLKey,
			      (STATE_MACHINE_FUNC) WpaEAPOLKeyAction);
	StateMachineSetAction(S, WPA_PTK, MT2_EAPOLASFAlert,
			      (STATE_MACHINE_FUNC) WpaEAPOLASFAlertAction);
}

/*
    ==========================================================================
    Description:
        this is state machine function.
        When receiving EAP packets which is  for 802.1x authentication use.
        Not use in PSK case
    Return:
    ==========================================================================
*/
void WpaEAPPacketAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
}

void WpaEAPOLASFAlertAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
}

void WpaEAPOLLogoffAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
}

/*
    ==========================================================================
    Description:
       Start 4-way HS when rcv EAPOL_START which may create by our driver in assoc.c
    Return:
    ==========================================================================
*/
void WpaEAPOLStartAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	struct rt_mac_table_entry *pEntry;
	struct rt_header_802_11 * pHeader;

	DBGPRINT(RT_DEBUG_TRACE, ("WpaEAPOLStartAction ===> \n"));

	pHeader = (struct rt_header_802_11 *) Elem->Msg;

	/*For normaol PSK, we enqueue an EAPOL-Start command to trigger the process. */
	if (Elem->MsgLen == 6)
		pEntry = MacTableLookup(pAd, Elem->Msg);
	else {
		pEntry = MacTableLookup(pAd, pHeader->Addr2);
	}

	if (pEntry) {
		DBGPRINT(RT_DEBUG_TRACE,
			 (" PortSecured(%d), WpaState(%d), AuthMode(%d), PMKID_CacheIdx(%d) \n",
			  pEntry->PortSecured, pEntry->WpaState,
			  pEntry->AuthMode, pEntry->PMKID_CacheIdx));

		if ((pEntry->PortSecured == WPA_802_1X_PORT_NOT_SECURED)
		    && (pEntry->WpaState < AS_PTKSTART)
		    && ((pEntry->AuthMode == Ndis802_11AuthModeWPAPSK)
			|| (pEntry->AuthMode == Ndis802_11AuthModeWPA2PSK)
			|| ((pEntry->AuthMode == Ndis802_11AuthModeWPA2)
			    && (pEntry->PMKID_CacheIdx != ENTRY_NOT_FOUND)))) {
			pEntry->PrivacyFilter = Ndis802_11PrivFilter8021xWEP;
			pEntry->WpaState = AS_INITPSK;
			pEntry->PortSecured = WPA_802_1X_PORT_NOT_SECURED;
			NdisZeroMemory(pEntry->R_Counter,
				       sizeof(pEntry->R_Counter));
			pEntry->ReTryCounter = PEER_MSG1_RETRY_TIMER_CTR;

			WPAStart4WayHS(pAd, pEntry, PEER_MSG1_RETRY_EXEC_INTV);
		}
	}
}

/*
    ==========================================================================
    Description:
        This is state machine function.
        When receiving EAPOL packets which is  for 802.1x key management.
        Use both in WPA, and WPAPSK case.
        In this function, further dispatch to different functions according to the received packet.  3 categories are :
          1.  normal 4-way pairwisekey and 2-way groupkey handshake
          2.  MIC error (Countermeasures attack)  report packet from STA.
          3.  Request for pairwise/group key update from STA
    Return:
    ==========================================================================
*/
void WpaEAPOLKeyAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	struct rt_mac_table_entry *pEntry;
	struct rt_header_802_11 * pHeader;
	struct rt_eapol_packet * pEapol_packet;
	struct rt_key_info peerKeyInfo;

	DBGPRINT(RT_DEBUG_TRACE, ("WpaEAPOLKeyAction ===>\n"));

	pHeader = (struct rt_header_802_11 *) Elem->Msg;
	pEapol_packet =
	    (struct rt_eapol_packet *) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];

	NdisZeroMemory((u8 *)& peerKeyInfo, sizeof(peerKeyInfo));
	NdisMoveMemory((u8 *)& peerKeyInfo,
		       (u8 *)& pEapol_packet->KeyDesc.KeyInfo,
		       sizeof(struct rt_key_info));

	hex_dump("Received Eapol frame", (unsigned char *)pEapol_packet,
		 (Elem->MsgLen - LENGTH_802_11 - LENGTH_802_1_H));

	*((u16 *) & peerKeyInfo) = cpu2le16(*((u16 *) & peerKeyInfo));

	do {
		pEntry = MacTableLookup(pAd, pHeader->Addr2);

		if (!pEntry
		    || ((!pEntry->ValidAsCLI) && (!pEntry->ValidAsApCli)))
			break;

		if (pEntry->AuthMode < Ndis802_11AuthModeWPA)
			break;

		DBGPRINT(RT_DEBUG_TRACE,
			 ("Receive EAPoL-Key frame from STA %02X-%02X-%02X-%02X-%02X-%02X\n",
			  PRINT_MAC(pEntry->Addr)));

		if (((pEapol_packet->ProVer != EAPOL_VER)
		     && (pEapol_packet->ProVer != EAPOL_VER2))
		    || ((pEapol_packet->KeyDesc.Type != WPA1_KEY_DESC)
			&& (pEapol_packet->KeyDesc.Type != WPA2_KEY_DESC))) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("Key descripter does not match with WPA rule\n"));
			break;
		}
		/* The value 1 shall be used for all EAPOL-Key frames to and from a STA when */
		/* neither the group nor pairwise ciphers are CCMP for Key Descriptor 1. */
		if ((pEntry->WepStatus == Ndis802_11Encryption2Enabled)
		    && (peerKeyInfo.KeyDescVer != DESC_TYPE_TKIP)) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("Key descripter version not match(TKIP) \n"));
			break;
		}
		/* The value 2 shall be used for all EAPOL-Key frames to and from a STA when */
		/* either the pairwise or the group cipher is AES-CCMP for Key Descriptor 2. */
		else if ((pEntry->WepStatus == Ndis802_11Encryption3Enabled)
			 && (peerKeyInfo.KeyDescVer != DESC_TYPE_AES)) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("Key descripter version not match(AES) \n"));
			break;
		}
		/* Check if this STA is in class 3 state and the WPA state is started */
		if ((pEntry->Sst == SST_ASSOC)
		    && (pEntry->WpaState >= AS_INITPSK)) {
			/* Check the Key Ack (bit 7) of the Key Information to determine the Authenticator */
			/* or not. */
			/* An EAPOL-Key frame that is sent by the Supplicant in response to an EAPOL- */
			/* Key frame from the Authenticator must not have the Ack bit set. */
			if (peerKeyInfo.KeyAck == 1) {
				/* The frame is snet by Authenticator. */
				/* So the Supplicant side shall handle this. */

				if ((peerKeyInfo.Secure == 0)
				    && (peerKeyInfo.Request == 0)
				    && (peerKeyInfo.Error == 0)
				    && (peerKeyInfo.KeyType == PAIRWISEKEY)) {
					/* Process 1. the message 1 of 4-way HS in WPA or WPA2 */
					/*                        EAPOL-Key(0,0,1,0,P,0,0,ANonce,0,DataKD_M1) */
					/*                 2. the message 3 of 4-way HS in WPA */
					/*                        EAPOL-Key(0,1,1,1,P,0,KeyRSC,ANonce,MIC,DataKD_M3) */
					if (peerKeyInfo.KeyMic == 0)
						PeerPairMsg1Action(pAd, pEntry,
								   Elem);
					else
						PeerPairMsg3Action(pAd, pEntry,
								   Elem);
				} else if ((peerKeyInfo.Secure == 1)
					   && (peerKeyInfo.KeyMic == 1)
					   && (peerKeyInfo.Request == 0)
					   && (peerKeyInfo.Error == 0)) {
					/* Process 1. the message 3 of 4-way HS in WPA2 */
					/*                        EAPOL-Key(1,1,1,1,P,0,KeyRSC,ANonce,MIC,DataKD_M3) */
					/*                 2. the message 1 of group KS in WPA or WPA2 */
					/*                        EAPOL-Key(1,1,1,0,G,0,Key RSC,0, MIC,GTK[N]) */
					if (peerKeyInfo.KeyType == PAIRWISEKEY)
						PeerPairMsg3Action(pAd, pEntry,
								   Elem);
					else
						PeerGroupMsg1Action(pAd, pEntry,
								    Elem);
				}
			} else {
				/* The frame is snet by Supplicant. */
				/* So the Authenticator side shall handle this. */
				if ((peerKeyInfo.Request == 0) &&
				    (peerKeyInfo.Error == 0) &&
				    (peerKeyInfo.KeyMic == 1)) {
					if (peerKeyInfo.Secure == 0
					    && peerKeyInfo.KeyType ==
					    PAIRWISEKEY) {
						/* EAPOL-Key(0,1,0,0,P,0,0,SNonce,MIC,Data) */
						/* Process 1. message 2 of 4-way HS in WPA or WPA2 */
						/*                 2. message 4 of 4-way HS in WPA */
						if (CONV_ARRARY_TO_u16
						    (pEapol_packet->KeyDesc.
						     KeyDataLen) == 0) {
							PeerPairMsg4Action(pAd,
									   pEntry,
									   Elem);
						} else {
							PeerPairMsg2Action(pAd,
									   pEntry,
									   Elem);
						}
					} else if (peerKeyInfo.Secure == 1
						   && peerKeyInfo.KeyType ==
						   PAIRWISEKEY) {
						/* EAPOL-Key(1,1,0,0,P,0,0,0,MIC,0) */
						/* Process message 4 of 4-way HS in WPA2 */
						PeerPairMsg4Action(pAd, pEntry,
								   Elem);
					} else if (peerKeyInfo.Secure == 1
						   && peerKeyInfo.KeyType ==
						   GROUPKEY) {
						/* EAPOL-Key(1,1,0,0,G,0,0,0,MIC,0) */
						/* Process message 2 of Group key HS in WPA or WPA2 */
						PeerGroupMsg2Action(pAd, pEntry,
								    &Elem->
								    Msg
								    [LENGTH_802_11],
								    (Elem->
								     MsgLen -
								     LENGTH_802_11));
					}
				}
			}
		}
	} while (FALSE);
}

/*
	========================================================================

	Routine	Description:
		Copy frame from waiting queue into relative ring buffer and set
	appropriate ASIC register to kick hardware encryption before really
	sent out to air.

	Arguments:
		pAd		Pointer	to our adapter
		void *	Pointer to outgoing Ndis frame
		NumberOfFrag	Number of fragment required

	Return Value:
		None

	Note:

	========================================================================
*/
void RTMPToWirelessSta(struct rt_rtmp_adapter *pAd,
		       struct rt_mac_table_entry *pEntry,
		       u8 *pHeader802_3,
		       u32 HdrLen,
		       u8 *pData, u32 DataLen, IN BOOLEAN bClearFrame)
{
	void *pPacket;
	int Status;

	if ((!pEntry) || ((!pEntry->ValidAsCLI) && (!pEntry->ValidAsApCli)))
		return;

	do {
		/* build a NDIS packet */
		Status =
		    RTMPAllocateNdisPacket(pAd, &pPacket, pHeader802_3, HdrLen,
					   pData, DataLen);
		if (Status != NDIS_STATUS_SUCCESS)
			break;

		if (bClearFrame)
			RTMP_SET_PACKET_CLEAR_EAP_FRAME(pPacket, 1);
		else
			RTMP_SET_PACKET_CLEAR_EAP_FRAME(pPacket, 0);
		{
			RTMP_SET_PACKET_SOURCE(pPacket, PKTSRC_NDIS);

			RTMP_SET_PACKET_NET_DEVICE_MBSSID(pPacket, MAIN_MBSSID);	/* set a default value */
			if (pEntry->apidx != 0)
				RTMP_SET_PACKET_NET_DEVICE_MBSSID(pPacket,
								  pEntry->
								  apidx);

			RTMP_SET_PACKET_WCID(pPacket, (u8)pEntry->Aid);
			RTMP_SET_PACKET_MOREDATA(pPacket, FALSE);
		}

		{
			/* send out the packet */
			Status = STASendPacket(pAd, pPacket);
			if (Status == NDIS_STATUS_SUCCESS) {
				u8 Index;

				/* Dequeue one frame from TxSwQueue0..3 queue and process it */
				/* There are three place calling dequeue for TX ring. */
				/* 1. Here, right after queueing the frame. */
				/* 2. At the end of TxRingTxDone service routine. */
				/* 3. Upon NDIS call RTMPSendPackets */
				if ((!RTMP_TEST_FLAG
				     (pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
				    &&
				    (!RTMP_TEST_FLAG
				     (pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS))) {
					for (Index = 0; Index < 5; Index++)
						if (pAd->TxSwQueue[Index].
						    Number > 0)
							RTMPDeQueuePacket(pAd,
									  FALSE,
									  Index,
									  MAX_TX_PROCESS);
				}
			}
		}

	} while (FALSE);
}

/*
    ==========================================================================
    Description:
        This is a function to initilize 4-way handshake

    Return:

    ==========================================================================
*/
void WPAStart4WayHS(struct rt_rtmp_adapter *pAd,
		    struct rt_mac_table_entry *pEntry, unsigned long TimeInterval)
{
	u8 Header802_3[14];
	struct rt_eapol_packet EAPOLPKT;
	u8 *pBssid = NULL;
	u8 group_cipher = Ndis802_11WEPDisabled;

	DBGPRINT(RT_DEBUG_TRACE, ("===> WPAStart4WayHS\n"));

	if (RTMP_TEST_FLAG
	    (pAd,
	     fRTMP_ADAPTER_RESET_IN_PROGRESS | fRTMP_ADAPTER_HALT_IN_PROGRESS))
	{
		DBGPRINT(RT_DEBUG_ERROR,
			 ("[ERROR]WPAStart4WayHS : The interface is closed...\n"));
		return;
	}

	if (pBssid == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("[ERROR]WPAStart4WayHS : No corresponding Authenticator.\n"));
		return;
	}
	/* Check the status */
	if ((pEntry->WpaState > AS_PTKSTART) || (pEntry->WpaState < AS_INITPMK)) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("[ERROR]WPAStart4WayHS : Not expect calling\n"));
		return;
	}

	/* Increment replay counter by 1 */
	ADD_ONE_To_64BIT_VAR(pEntry->R_Counter);

	/* Randomly generate ANonce */
	GenRandom(pAd, (u8 *) pBssid, pEntry->ANonce);

	/* Construct EAPoL message - Pairwise Msg 1 */
	/* EAPOL-Key(0,0,1,0,P,0,0,ANonce,0,DataKD_M1) */
	NdisZeroMemory(&EAPOLPKT, sizeof(struct rt_eapol_packet));
	ConstructEapolMsg(pEntry, group_cipher, EAPOL_PAIR_MSG_1, 0,	/* Default key index */
			  pEntry->ANonce, NULL,	/* TxRSC */
			  NULL,	/* GTK */
			  NULL,	/* RSNIE */
			  0,	/* RSNIE length */
			  &EAPOLPKT);

	/* Make outgoing frame */
	MAKE_802_3_HEADER(Header802_3, pEntry->Addr, pBssid, EAPOL);
	RTMPToWirelessSta(pAd, pEntry, Header802_3,
			  LENGTH_802_3, (u8 *)& EAPOLPKT,
			  CONV_ARRARY_TO_u16(EAPOLPKT.Body_Len) + 4,
			  (pEntry->PortSecured ==
			   WPA_802_1X_PORT_SECURED) ? FALSE : TRUE);

	/* Trigger Retry Timer */
	RTMPModTimer(&pEntry->RetryTimer, TimeInterval);

	/* Update State */
	pEntry->WpaState = AS_PTKSTART;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("<=== WPAStart4WayHS: send Msg1 of 4-way \n"));

}

/*
	========================================================================

	Routine Description:
		Process Pairwise key Msg-1 of 4-way handshaking and send Msg-2

	Arguments:
		pAd			Pointer	to our adapter
		Elem		Message body

	Return Value:
		None

	Note:

	========================================================================
*/
void PeerPairMsg1Action(struct rt_rtmp_adapter *pAd,
			struct rt_mac_table_entry *pEntry, struct rt_mlme_queue_elem *Elem)
{
	u8 PTK[80];
	u8 Header802_3[14];
	struct rt_eapol_packet * pMsg1;
	u32 MsgLen;
	struct rt_eapol_packet EAPOLPKT;
	u8 *pCurrentAddr = NULL;
	u8 *pmk_ptr = NULL;
	u8 group_cipher = Ndis802_11WEPDisabled;
	u8 *rsnie_ptr = NULL;
	u8 rsnie_len = 0;

	DBGPRINT(RT_DEBUG_TRACE, ("===> PeerPairMsg1Action \n"));

	if ((!pEntry) || ((!pEntry->ValidAsCLI) && (!pEntry->ValidAsApCli)))
		return;

	if (Elem->MsgLen <
	    (LENGTH_802_11 + LENGTH_802_1_H + LENGTH_EAPOL_H +
	     sizeof(struct rt_key_descripter) - MAX_LEN_OF_RSNIE - 2))
		return;

	{
		pCurrentAddr = pAd->CurrentAddress;
		pmk_ptr = pAd->StaCfg.PMK;
		group_cipher = pAd->StaCfg.GroupCipher;
		rsnie_ptr = pAd->StaCfg.RSN_IE;
		rsnie_len = pAd->StaCfg.RSNIE_Len;
	}

	/* Store the received frame */
	pMsg1 = (struct rt_eapol_packet *) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];
	MsgLen = Elem->MsgLen - LENGTH_802_11 - LENGTH_802_1_H;

	/* Sanity Check peer Pairwise message 1 - Replay Counter */
	if (PeerWpaMessageSanity(pAd, pMsg1, MsgLen, EAPOL_PAIR_MSG_1, pEntry)
	    == FALSE)
		return;

	/* Store Replay counter, it will use to verify message 3 and construct message 2 */
	NdisMoveMemory(pEntry->R_Counter, pMsg1->KeyDesc.ReplayCounter,
		       LEN_KEY_DESC_REPLAY);

	/* Store ANonce */
	NdisMoveMemory(pEntry->ANonce, pMsg1->KeyDesc.KeyNonce,
		       LEN_KEY_DESC_NONCE);

	/* Generate random SNonce */
	GenRandom(pAd, (u8 *) pCurrentAddr, pEntry->SNonce);

	{
		/* Calculate PTK(ANonce, SNonce) */
		WpaDerivePTK(pAd,
			     pmk_ptr,
			     pEntry->ANonce,
			     pEntry->Addr,
			     pEntry->SNonce, pCurrentAddr, PTK, LEN_PTK);

		/* Save key to PTK entry */
		NdisMoveMemory(pEntry->PTK, PTK, LEN_PTK);
	}

	/* Update WpaState */
	pEntry->WpaState = AS_PTKINIT_NEGOTIATING;

	/* Construct EAPoL message - Pairwise Msg 2 */
	/*  EAPOL-Key(0,1,0,0,P,0,0,SNonce,MIC,DataKD_M2) */
	NdisZeroMemory(&EAPOLPKT, sizeof(struct rt_eapol_packet));
	ConstructEapolMsg(pEntry, group_cipher, EAPOL_PAIR_MSG_2, 0,	/* DefaultKeyIdx */
			  pEntry->SNonce, NULL,	/* TxRsc */
			  NULL,	/* GTK */
			  (u8 *) rsnie_ptr, rsnie_len, &EAPOLPKT);

	/* Make outgoing frame */
	MAKE_802_3_HEADER(Header802_3, pEntry->Addr, pCurrentAddr, EAPOL);

	RTMPToWirelessSta(pAd, pEntry,
			  Header802_3, sizeof(Header802_3), (u8 *)& EAPOLPKT,
			  CONV_ARRARY_TO_u16(EAPOLPKT.Body_Len) + 4, TRUE);

	DBGPRINT(RT_DEBUG_TRACE,
		 ("<=== PeerPairMsg1Action: send Msg2 of 4-way \n"));
}

/*
    ==========================================================================
    Description:
        When receiving the second packet of 4-way pairwisekey handshake.
    Return:
    ==========================================================================
*/
void PeerPairMsg2Action(struct rt_rtmp_adapter *pAd,
			struct rt_mac_table_entry *pEntry, struct rt_mlme_queue_elem *Elem)
{
	u8 PTK[80];
	BOOLEAN Cancelled;
	struct rt_header_802_11 * pHeader;
	struct rt_eapol_packet EAPOLPKT;
	struct rt_eapol_packet * pMsg2;
	u32 MsgLen;
	u8 Header802_3[LENGTH_802_3];
	u8 TxTsc[6];
	u8 *pBssid = NULL;
	u8 *pmk_ptr = NULL;
	u8 *gtk_ptr = NULL;
	u8 default_key = 0;
	u8 group_cipher = Ndis802_11WEPDisabled;
	u8 *rsnie_ptr = NULL;
	u8 rsnie_len = 0;

	DBGPRINT(RT_DEBUG_TRACE, ("===> PeerPairMsg2Action \n"));

	if ((!pEntry) || (!pEntry->ValidAsCLI))
		return;

	if (Elem->MsgLen <
	    (LENGTH_802_11 + LENGTH_802_1_H + LENGTH_EAPOL_H +
	     sizeof(struct rt_key_descripter) - MAX_LEN_OF_RSNIE - 2))
		return;

	/* check Entry in valid State */
	if (pEntry->WpaState < AS_PTKSTART)
		return;

	/* pointer to 802.11 header */
	pHeader = (struct rt_header_802_11 *) Elem->Msg;

	/* skip 802.11_header(24-byte) and LLC_header(8) */
	pMsg2 = (struct rt_eapol_packet *) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];
	MsgLen = Elem->MsgLen - LENGTH_802_11 - LENGTH_802_1_H;

	/* Store SNonce */
	NdisMoveMemory(pEntry->SNonce, pMsg2->KeyDesc.KeyNonce,
		       LEN_KEY_DESC_NONCE);

	{
		/* Derive PTK */
		WpaDerivePTK(pAd, (u8 *) pmk_ptr, pEntry->ANonce,	/* ANONCE */
			     (u8 *) pBssid, pEntry->SNonce,	/* SNONCE */
			     pEntry->Addr, PTK, LEN_PTK);

		NdisMoveMemory(pEntry->PTK, PTK, LEN_PTK);
	}

	/* Sanity Check peer Pairwise message 2 - Replay Counter, MIC, RSNIE */
	if (PeerWpaMessageSanity(pAd, pMsg2, MsgLen, EAPOL_PAIR_MSG_2, pEntry)
	    == FALSE)
		return;

	do {
		/* delete retry timer */
		RTMPCancelTimer(&pEntry->RetryTimer, &Cancelled);

		/* Change state */
		pEntry->WpaState = AS_PTKINIT_NEGOTIATING;

		/* Increment replay counter by 1 */
		ADD_ONE_To_64BIT_VAR(pEntry->R_Counter);

		/* Construct EAPoL message - Pairwise Msg 3 */
		NdisZeroMemory(&EAPOLPKT, sizeof(struct rt_eapol_packet));
		ConstructEapolMsg(pEntry,
				  group_cipher,
				  EAPOL_PAIR_MSG_3,
				  default_key,
				  pEntry->ANonce,
				  TxTsc,
				  (u8 *) gtk_ptr,
				  (u8 *) rsnie_ptr, rsnie_len, &EAPOLPKT);

		/* Make outgoing frame */
		MAKE_802_3_HEADER(Header802_3, pEntry->Addr, pBssid, EAPOL);
		RTMPToWirelessSta(pAd, pEntry, Header802_3, LENGTH_802_3,
				  (u8 *)& EAPOLPKT,
				  CONV_ARRARY_TO_u16(EAPOLPKT.Body_Len) + 4,
				  (pEntry->PortSecured ==
				   WPA_802_1X_PORT_SECURED) ? FALSE : TRUE);

		pEntry->ReTryCounter = PEER_MSG3_RETRY_TIMER_CTR;
		RTMPSetTimer(&pEntry->RetryTimer, PEER_MSG3_RETRY_EXEC_INTV);

		/* Update State */
		pEntry->WpaState = AS_PTKINIT_NEGOTIATING;
	} while (FALSE);

	DBGPRINT(RT_DEBUG_TRACE,
		 ("<=== PeerPairMsg2Action: send Msg3 of 4-way \n"));
}

/*
	========================================================================

	Routine Description:
		Process Pairwise key Msg 3 of 4-way handshaking and send Msg 4

	Arguments:
		pAd	Pointer	to our adapter
		Elem		Message body

	Return Value:
		None

	Note:

	========================================================================
*/
void PeerPairMsg3Action(struct rt_rtmp_adapter *pAd,
			struct rt_mac_table_entry *pEntry, struct rt_mlme_queue_elem *Elem)
{
	struct rt_header_802_11 * pHeader;
	u8 Header802_3[14];
	struct rt_eapol_packet EAPOLPKT;
	struct rt_eapol_packet * pMsg3;
	u32 MsgLen;
	u8 *pCurrentAddr = NULL;
	u8 group_cipher = Ndis802_11WEPDisabled;

	DBGPRINT(RT_DEBUG_TRACE, ("===> PeerPairMsg3Action \n"));

	if ((!pEntry) || ((!pEntry->ValidAsCLI) && (!pEntry->ValidAsApCli)))
		return;

	if (Elem->MsgLen <
	    (LENGTH_802_11 + LENGTH_802_1_H + LENGTH_EAPOL_H +
	     sizeof(struct rt_key_descripter) - MAX_LEN_OF_RSNIE - 2))
		return;

	{
		pCurrentAddr = pAd->CurrentAddress;
		group_cipher = pAd->StaCfg.GroupCipher;

	}

	/* Record 802.11 header & the received EAPOL packet Msg3 */
	pHeader = (struct rt_header_802_11 *) Elem->Msg;
	pMsg3 = (struct rt_eapol_packet *) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];
	MsgLen = Elem->MsgLen - LENGTH_802_11 - LENGTH_802_1_H;

	/* Sanity Check peer Pairwise message 3 - Replay Counter, MIC, RSNIE */
	if (PeerWpaMessageSanity(pAd, pMsg3, MsgLen, EAPOL_PAIR_MSG_3, pEntry)
	    == FALSE)
		return;

	/* Save Replay counter, it will use construct message 4 */
	NdisMoveMemory(pEntry->R_Counter, pMsg3->KeyDesc.ReplayCounter,
		       LEN_KEY_DESC_REPLAY);

	/* Double check ANonce */
	if (!NdisEqualMemory
	    (pEntry->ANonce, pMsg3->KeyDesc.KeyNonce, LEN_KEY_DESC_NONCE)) {
		return;
	}
	/* Construct EAPoL message - Pairwise Msg 4 */
	NdisZeroMemory(&EAPOLPKT, sizeof(struct rt_eapol_packet));
	ConstructEapolMsg(pEntry, group_cipher, EAPOL_PAIR_MSG_4, 0,	/* group key index not used in message 4 */
			  NULL,	/* Nonce not used in message 4 */
			  NULL,	/* TxRSC not used in message 4 */
			  NULL,	/* GTK not used in message 4 */
			  NULL,	/* RSN IE not used in message 4 */
			  0, &EAPOLPKT);

	/* Update WpaState */
	pEntry->WpaState = AS_PTKINITDONE;

	/* Update pairwise key */
	{
		struct rt_cipher_key *pSharedKey;

		pSharedKey = &pAd->SharedKey[BSS0][0];

		NdisMoveMemory(pAd->StaCfg.PTK, pEntry->PTK, LEN_PTK);

		/* Prepare pair-wise key information into shared key table */
		NdisZeroMemory(pSharedKey, sizeof(struct rt_cipher_key));
		pSharedKey->KeyLen = LEN_TKIP_EK;
		NdisMoveMemory(pSharedKey->Key, &pAd->StaCfg.PTK[32],
			       LEN_TKIP_EK);
		NdisMoveMemory(pSharedKey->RxMic, &pAd->StaCfg.PTK[48],
			       LEN_TKIP_RXMICK);
		NdisMoveMemory(pSharedKey->TxMic,
			       &pAd->StaCfg.PTK[48 + LEN_TKIP_RXMICK],
			       LEN_TKIP_TXMICK);

		/* Decide its ChiperAlg */
		if (pAd->StaCfg.PairCipher == Ndis802_11Encryption2Enabled)
			pSharedKey->CipherAlg = CIPHER_TKIP;
		else if (pAd->StaCfg.PairCipher == Ndis802_11Encryption3Enabled)
			pSharedKey->CipherAlg = CIPHER_AES;
		else
			pSharedKey->CipherAlg = CIPHER_NONE;

		/* Update these related information to struct rt_mac_table_entry */
		pEntry = &pAd->MacTab.Content[BSSID_WCID];
		NdisMoveMemory(pEntry->PairwiseKey.Key, &pAd->StaCfg.PTK[32],
			       LEN_TKIP_EK);
		NdisMoveMemory(pEntry->PairwiseKey.RxMic, &pAd->StaCfg.PTK[48],
			       LEN_TKIP_RXMICK);
		NdisMoveMemory(pEntry->PairwiseKey.TxMic,
			       &pAd->StaCfg.PTK[48 + LEN_TKIP_RXMICK],
			       LEN_TKIP_TXMICK);
		pEntry->PairwiseKey.CipherAlg = pSharedKey->CipherAlg;

		/* Update pairwise key information to ASIC Shared Key Table */
		AsicAddSharedKeyEntry(pAd,
				      BSS0,
				      0,
				      pSharedKey->CipherAlg,
				      pSharedKey->Key,
				      pSharedKey->TxMic, pSharedKey->RxMic);

		/* Update ASIC WCID attribute table and IVEIV table */
		RTMPAddWcidAttributeEntry(pAd,
					  BSS0,
					  0, pSharedKey->CipherAlg, pEntry);

	}

	/* open 802.1x port control and privacy filter */
	if (pEntry->AuthMode == Ndis802_11AuthModeWPA2PSK ||
	    pEntry->AuthMode == Ndis802_11AuthModeWPA2) {
		pEntry->PortSecured = WPA_802_1X_PORT_SECURED;
		pEntry->PrivacyFilter = Ndis802_11PrivFilterAcceptAll;

		STA_PORT_SECURED(pAd);
		/* Indicate Connected for GUI */
		pAd->IndicateMediaState = NdisMediaStateConnected;
		DBGPRINT(RT_DEBUG_TRACE,
			 ("PeerPairMsg3Action: AuthMode(%s) PairwiseCipher(%s) GroupCipher(%s) \n",
			  GetAuthMode(pEntry->AuthMode),
			  GetEncryptType(pEntry->WepStatus),
			  GetEncryptType(group_cipher)));
	} else {
	}

	/* Init 802.3 header and send out */
	MAKE_802_3_HEADER(Header802_3, pEntry->Addr, pCurrentAddr, EAPOL);
	RTMPToWirelessSta(pAd, pEntry,
			  Header802_3, sizeof(Header802_3),
			  (u8 *)& EAPOLPKT,
			  CONV_ARRARY_TO_u16(EAPOLPKT.Body_Len) + 4, TRUE);

	DBGPRINT(RT_DEBUG_TRACE,
		 ("<=== PeerPairMsg3Action: send Msg4 of 4-way \n"));
}

/*
    ==========================================================================
    Description:
        When receiving the last packet of 4-way pairwisekey handshake.
        Initilize 2-way groupkey handshake following.
    Return:
    ==========================================================================
*/
void PeerPairMsg4Action(struct rt_rtmp_adapter *pAd,
			struct rt_mac_table_entry *pEntry, struct rt_mlme_queue_elem *Elem)
{
	struct rt_eapol_packet * pMsg4;
	struct rt_header_802_11 * pHeader;
	u32 MsgLen;
	BOOLEAN Cancelled;
	u8 group_cipher = Ndis802_11WEPDisabled;

	DBGPRINT(RT_DEBUG_TRACE, ("===> PeerPairMsg4Action\n"));

	do {
		if ((!pEntry) || (!pEntry->ValidAsCLI))
			break;

		if (Elem->MsgLen <
		    (LENGTH_802_11 + LENGTH_802_1_H + LENGTH_EAPOL_H +
		     sizeof(struct rt_key_descripter) - MAX_LEN_OF_RSNIE - 2))
			break;

		if (pEntry->WpaState < AS_PTKINIT_NEGOTIATING)
			break;

		/* pointer to 802.11 header */
		pHeader = (struct rt_header_802_11 *) Elem->Msg;

		/* skip 802.11_header(24-byte) and LLC_header(8) */
		pMsg4 =
		    (struct rt_eapol_packet *) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];
		MsgLen = Elem->MsgLen - LENGTH_802_11 - LENGTH_802_1_H;

		/* Sanity Check peer Pairwise message 4 - Replay Counter, MIC */
		if (PeerWpaMessageSanity
		    (pAd, pMsg4, MsgLen, EAPOL_PAIR_MSG_4, pEntry) == FALSE)
			break;

		/* 3. uses the MLME.SETKEYS.request to configure PTK into MAC */
		NdisZeroMemory(&pEntry->PairwiseKey, sizeof(struct rt_cipher_key));

		/* reset IVEIV in Asic */
		AsicUpdateWCIDIVEIV(pAd, pEntry->Aid, 1, 0);

		pEntry->PairwiseKey.KeyLen = LEN_TKIP_EK;
		NdisMoveMemory(pEntry->PairwiseKey.Key, &pEntry->PTK[32],
			       LEN_TKIP_EK);
		NdisMoveMemory(pEntry->PairwiseKey.RxMic,
			       &pEntry->PTK[TKIP_AP_RXMICK_OFFSET],
			       LEN_TKIP_RXMICK);
		NdisMoveMemory(pEntry->PairwiseKey.TxMic,
			       &pEntry->PTK[TKIP_AP_TXMICK_OFFSET],
			       LEN_TKIP_TXMICK);

		/* Set pairwise key to Asic */
		{
			pEntry->PairwiseKey.CipherAlg = CIPHER_NONE;
			if (pEntry->WepStatus == Ndis802_11Encryption2Enabled)
				pEntry->PairwiseKey.CipherAlg = CIPHER_TKIP;
			else if (pEntry->WepStatus ==
				 Ndis802_11Encryption3Enabled)
				pEntry->PairwiseKey.CipherAlg = CIPHER_AES;

			/* Add Pair-wise key to Asic */
			AsicAddPairwiseKeyEntry(pAd,
						pEntry->Addr,
						(u8)pEntry->Aid,
						&pEntry->PairwiseKey);

			/* update WCID attribute table and IVEIV table for this entry */
			RTMPAddWcidAttributeEntry(pAd,
						  pEntry->apidx,
						  0,
						  pEntry->PairwiseKey.CipherAlg,
						  pEntry);
		}

		/* 4. upgrade state */
		pEntry->PrivacyFilter = Ndis802_11PrivFilterAcceptAll;
		pEntry->WpaState = AS_PTKINITDONE;
		pEntry->PortSecured = WPA_802_1X_PORT_SECURED;

		if (pEntry->AuthMode == Ndis802_11AuthModeWPA2 ||
		    pEntry->AuthMode == Ndis802_11AuthModeWPA2PSK) {
			pEntry->GTKState = REKEY_ESTABLISHED;
			RTMPCancelTimer(&pEntry->RetryTimer, &Cancelled);

			/* send wireless event - for set key done WPA2 */
			if (pAd->CommonCfg.bWirelessEvent)
				RTMPSendWirelessEvent(pAd,
						      IW_SET_KEY_DONE_WPA2_EVENT_FLAG,
						      pEntry->Addr,
						      pEntry->apidx, 0);

			DBGPRINT(RT_DEBUG_OFF,
				 ("AP SETKEYS DONE - WPA2, AuthMode(%d)=%s, WepStatus(%d)=%s, GroupWepStatus(%d)=%s\n\n",
				  pEntry->AuthMode,
				  GetAuthMode(pEntry->AuthMode),
				  pEntry->WepStatus,
				  GetEncryptType(pEntry->WepStatus),
				  group_cipher, GetEncryptType(group_cipher)));
		} else {
			/* 5. init Group 2-way handshake if necessary. */
			WPAStart2WayGroupHS(pAd, pEntry);

			pEntry->ReTryCounter = GROUP_MSG1_RETRY_TIMER_CTR;
			RTMPModTimer(&pEntry->RetryTimer,
				     PEER_MSG3_RETRY_EXEC_INTV);
		}
	} while (FALSE);

}

/*
    ==========================================================================
    Description:
        This is a function to send the first packet of 2-way groupkey handshake
    Return:

    ==========================================================================
*/
void WPAStart2WayGroupHS(struct rt_rtmp_adapter *pAd, struct rt_mac_table_entry *pEntry)
{
	u8 Header802_3[14];
	u8 TxTsc[6];
	struct rt_eapol_packet EAPOLPKT;
	u8 group_cipher = Ndis802_11WEPDisabled;
	u8 default_key = 0;
	u8 *gnonce_ptr = NULL;
	u8 *gtk_ptr = NULL;
	u8 *pBssid = NULL;

	DBGPRINT(RT_DEBUG_TRACE, ("===> WPAStart2WayGroupHS\n"));

	if ((!pEntry) || (!pEntry->ValidAsCLI))
		return;

	do {
		/* Increment replay counter by 1 */
		ADD_ONE_To_64BIT_VAR(pEntry->R_Counter);

		/* Construct EAPoL message - Group Msg 1 */
		NdisZeroMemory(&EAPOLPKT, sizeof(struct rt_eapol_packet));
		ConstructEapolMsg(pEntry,
				  group_cipher,
				  EAPOL_GROUP_MSG_1,
				  default_key,
				  (u8 *) gnonce_ptr,
				  TxTsc, (u8 *) gtk_ptr, NULL, 0, &EAPOLPKT);

		/* Make outgoing frame */
		MAKE_802_3_HEADER(Header802_3, pEntry->Addr, pBssid, EAPOL);
		RTMPToWirelessSta(pAd, pEntry,
				  Header802_3, LENGTH_802_3,
				  (u8 *)& EAPOLPKT,
				  CONV_ARRARY_TO_u16(EAPOLPKT.Body_Len) + 4,
				  FALSE);

	} while (FALSE);

	DBGPRINT(RT_DEBUG_TRACE,
		 ("<=== WPAStart2WayGroupHS : send out Group Message 1 \n"));

	return;
}

/*
	========================================================================

	Routine Description:
		Process Group key 2-way handshaking

	Arguments:
		pAd	Pointer	to our adapter
		Elem		Message body

	Return Value:
		None

	Note:

	========================================================================
*/
void PeerGroupMsg1Action(struct rt_rtmp_adapter *pAd,
			 struct rt_mac_table_entry *pEntry, struct rt_mlme_queue_elem *Elem)
{
	u8 Header802_3[14];
	struct rt_eapol_packet EAPOLPKT;
	struct rt_eapol_packet * pGroup;
	u32 MsgLen;
	BOOLEAN Cancelled;
	u8 default_key = 0;
	u8 group_cipher = Ndis802_11WEPDisabled;
	u8 *pCurrentAddr = NULL;

	DBGPRINT(RT_DEBUG_TRACE, ("===> PeerGroupMsg1Action \n"));

	if ((!pEntry) || ((!pEntry->ValidAsCLI) && (!pEntry->ValidAsApCli)))
		return;

	{
		pCurrentAddr = pAd->CurrentAddress;
		group_cipher = pAd->StaCfg.GroupCipher;
		default_key = pAd->StaCfg.DefaultKeyId;
	}

	/* Process Group Message 1 frame. skip 802.11 header(24) & LLC_SNAP header(8) */
	pGroup = (struct rt_eapol_packet *) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];
	MsgLen = Elem->MsgLen - LENGTH_802_11 - LENGTH_802_1_H;

	/* Sanity Check peer group message 1 - Replay Counter, MIC, RSNIE */
	if (PeerWpaMessageSanity(pAd, pGroup, MsgLen, EAPOL_GROUP_MSG_1, pEntry)
	    == FALSE)
		return;

	/* delete retry timer */
	RTMPCancelTimer(&pEntry->RetryTimer, &Cancelled);

	/* Save Replay counter, it will use to construct message 2 */
	NdisMoveMemory(pEntry->R_Counter, pGroup->KeyDesc.ReplayCounter,
		       LEN_KEY_DESC_REPLAY);

	/* Construct EAPoL message - Group Msg 2 */
	NdisZeroMemory(&EAPOLPKT, sizeof(struct rt_eapol_packet));
	ConstructEapolMsg(pEntry, group_cipher, EAPOL_GROUP_MSG_2, default_key, NULL,	/* Nonce not used */
			  NULL,	/* TxRSC not used */
			  NULL,	/* GTK not used */
			  NULL,	/* RSN IE not used */
			  0, &EAPOLPKT);

	/* open 802.1x port control and privacy filter */
	pEntry->PortSecured = WPA_802_1X_PORT_SECURED;
	pEntry->PrivacyFilter = Ndis802_11PrivFilterAcceptAll;

	STA_PORT_SECURED(pAd);
	/* Indicate Connected for GUI */
	pAd->IndicateMediaState = NdisMediaStateConnected;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("PeerGroupMsg1Action: AuthMode(%s) PairwiseCipher(%s) GroupCipher(%s) \n",
		  GetAuthMode(pEntry->AuthMode),
		  GetEncryptType(pEntry->WepStatus),
		  GetEncryptType(group_cipher)));

	/* init header and Fill Packet and send Msg 2 to authenticator */
	MAKE_802_3_HEADER(Header802_3, pEntry->Addr, pCurrentAddr, EAPOL);
	RTMPToWirelessSta(pAd, pEntry,
			  Header802_3, sizeof(Header802_3),
			  (u8 *)& EAPOLPKT,
			  CONV_ARRARY_TO_u16(EAPOLPKT.Body_Len) + 4, FALSE);

	DBGPRINT(RT_DEBUG_TRACE,
		 ("<=== PeerGroupMsg1Action: sned group message 2\n"));
}

/*
    ==========================================================================
    Description:
        When receiving the last packet of 2-way groupkey handshake.
    Return:
    ==========================================================================
*/
void PeerGroupMsg2Action(struct rt_rtmp_adapter *pAd,
			 struct rt_mac_table_entry *pEntry,
			 void * Msg, u32 MsgLen)
{
	u32 Len;
	u8 *pData;
	BOOLEAN Cancelled;
	struct rt_eapol_packet * pMsg2;
	u8 group_cipher = Ndis802_11WEPDisabled;

	DBGPRINT(RT_DEBUG_TRACE, ("===> PeerGroupMsg2Action \n"));

	do {
		if ((!pEntry) || (!pEntry->ValidAsCLI))
			break;

		if (MsgLen <
		    (LENGTH_802_1_H + LENGTH_EAPOL_H + sizeof(struct rt_key_descripter) -
		     MAX_LEN_OF_RSNIE - 2))
			break;

		if (pEntry->WpaState != AS_PTKINITDONE)
			break;

		pData = (u8 *)Msg;
		pMsg2 = (struct rt_eapol_packet *) (pData + LENGTH_802_1_H);
		Len = MsgLen - LENGTH_802_1_H;

		/* Sanity Check peer group message 2 - Replay Counter, MIC */
		if (PeerWpaMessageSanity
		    (pAd, pMsg2, Len, EAPOL_GROUP_MSG_2, pEntry) == FALSE)
			break;

		/* 3.  upgrade state */

		RTMPCancelTimer(&pEntry->RetryTimer, &Cancelled);
		pEntry->GTKState = REKEY_ESTABLISHED;

		if ((pEntry->AuthMode == Ndis802_11AuthModeWPA2)
		    || (pEntry->AuthMode == Ndis802_11AuthModeWPA2PSK)) {
			/* send wireless event - for set key done WPA2 */
			if (pAd->CommonCfg.bWirelessEvent)
				RTMPSendWirelessEvent(pAd,
						      IW_SET_KEY_DONE_WPA2_EVENT_FLAG,
						      pEntry->Addr,
						      pEntry->apidx, 0);

			DBGPRINT(RT_DEBUG_OFF,
				 ("AP SETKEYS DONE - WPA2, AuthMode(%d)=%s, WepStatus(%d)=%s, GroupWepStatus(%d)=%s\n\n",
				  pEntry->AuthMode,
				  GetAuthMode(pEntry->AuthMode),
				  pEntry->WepStatus,
				  GetEncryptType(pEntry->WepStatus),
				  group_cipher, GetEncryptType(group_cipher)));
		} else {
			/* send wireless event - for set key done WPA */
			if (pAd->CommonCfg.bWirelessEvent)
				RTMPSendWirelessEvent(pAd,
						      IW_SET_KEY_DONE_WPA1_EVENT_FLAG,
						      pEntry->Addr,
						      pEntry->apidx, 0);

			DBGPRINT(RT_DEBUG_OFF,
				 ("AP SETKEYS DONE - WPA1, AuthMode(%d)=%s, WepStatus(%d)=%s, GroupWepStatus(%d)=%s\n\n",
				  pEntry->AuthMode,
				  GetAuthMode(pEntry->AuthMode),
				  pEntry->WepStatus,
				  GetEncryptType(pEntry->WepStatus),
				  group_cipher, GetEncryptType(group_cipher)));
		}
	} while (FALSE);
}

/*
	========================================================================

	Routine Description:
		Classify WPA EAP message type

	Arguments:
		EAPType		Value of EAP message type
		MsgType		Internal Message definition for MLME state machine

	Return Value:
		TRUE		Found appropriate message type
		FALSE		No appropriate message type

	IRQL = DISPATCH_LEVEL

	Note:
		All these constants are defined in wpa.h
		For supplicant, there is only EAPOL Key message avaliable

	========================================================================
*/
BOOLEAN WpaMsgTypeSubst(u8 EAPType, int * MsgType)
{
	switch (EAPType) {
	case EAPPacket:
		*MsgType = MT2_EAPPacket;
		break;
	case EAPOLStart:
		*MsgType = MT2_EAPOLStart;
		break;
	case EAPOLLogoff:
		*MsgType = MT2_EAPOLLogoff;
		break;
	case EAPOLKey:
		*MsgType = MT2_EAPOLKey;
		break;
	case EAPOLASFAlert:
		*MsgType = MT2_EAPOLASFAlert;
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

/*
	========================================================================

	Routine Description:
		The pseudo-random function(PRF) that hashes various inputs to
		derive a pseudo-random value. To add liveness to the pseudo-random
		value, a nonce should be one of the inputs.

		It is used to generate PTK, GTK or some specific random value.

	Arguments:
		u8	*key,		-	the key material for HMAC_SHA1 use
		int		key_len		-	the length of key
		u8	*prefix		-	a prefix label
		int		prefix_len	-	the length of the label
		u8	*data		-	a specific data with variable length
		int		data_len	-	the length of a specific data
		int		len			-	the output lenght

	Return Value:
		u8	*output		-	the calculated result

	Note:
		802.11i-2004	Annex H.3

	========================================================================
*/
void PRF(u8 * key,
	 int key_len,
	 u8 * prefix,
	 int prefix_len,
	 u8 * data, int data_len, u8 * output, int len)
{
	int i;
	u8 *input;
	int currentindex = 0;
	int total_len;

	/* Allocate memory for input */
	os_alloc_mem(NULL, (u8 **) & input, 1024);

	if (input == NULL) {
		DBGPRINT(RT_DEBUG_ERROR, ("PRF: no memory!\n"));
		return;
	}
	/* Generate concatenation input */
	NdisMoveMemory(input, prefix, prefix_len);

	/* Concatenate a single octet containing 0 */
	input[prefix_len] = 0;

	/* Concatenate specific data */
	NdisMoveMemory(&input[prefix_len + 1], data, data_len);
	total_len = prefix_len + 1 + data_len;

	/* Concatenate a single octet containing 0 */
	/* This octet shall be update later */
	input[total_len] = 0;
	total_len++;

	/* Iterate to calculate the result by hmac-sha-1 */
	/* Then concatenate to last result */
	for (i = 0; i < (len + 19) / 20; i++) {
		HMAC_SHA1(key, key_len, input, total_len, &output[currentindex],
			  SHA1_DIGEST_SIZE);
		currentindex += 20;

		/* update the last octet */
		input[total_len - 1]++;
	}
	os_free_mem(NULL, input);
}

/*
* F(P, S, c, i) = U1 xor U2 xor ... Uc
* U1 = PRF(P, S || Int(i))
* U2 = PRF(P, U1)
* Uc = PRF(P, Uc-1)
*/

static void F(char *password, unsigned char *ssid, int ssidlength,
	      int iterations, int count, unsigned char *output)
{
	unsigned char digest[36], digest1[SHA1_DIGEST_SIZE];
	int i, j;

	/* U1 = PRF(P, S || int(i)) */
	memcpy(digest, ssid, ssidlength);
	digest[ssidlength] = (unsigned char)((count >> 24) & 0xff);
	digest[ssidlength + 1] = (unsigned char)((count >> 16) & 0xff);
	digest[ssidlength + 2] = (unsigned char)((count >> 8) & 0xff);
	digest[ssidlength + 3] = (unsigned char)(count & 0xff);
	HMAC_SHA1((unsigned char *)password, (int)strlen(password), digest, ssidlength + 4, digest1, SHA1_DIGEST_SIZE);	/* for WPA update */

	/* output = U1 */
	memcpy(output, digest1, SHA1_DIGEST_SIZE);

	for (i = 1; i < iterations; i++) {
		/* Un = PRF(P, Un-1) */
		HMAC_SHA1((unsigned char *)password, (int)strlen(password), digest1, SHA1_DIGEST_SIZE, digest, SHA1_DIGEST_SIZE);	/* for WPA update */
		memcpy(digest1, digest, SHA1_DIGEST_SIZE);

		/* output = output xor Un */
		for (j = 0; j < SHA1_DIGEST_SIZE; j++) {
			output[j] ^= digest[j];
		}
	}
}

/*
* password - ascii string up to 63 characters in length
* ssid - octet string up to 32 octets
* ssidlength - length of ssid in octets
* output must be 40 octets in length and outputs 256 bits of key
*/
int PasswordHash(char *password, u8 *ssid, int ssidlength, u8 *output)
{
	if ((strlen(password) > 63) || (ssidlength > 32))
		return 0;

	F(password, ssid, ssidlength, 4096, 1, output);
	F(password, ssid, ssidlength, 4096, 2, &output[SHA1_DIGEST_SIZE]);
	return 1;
}

/*
	========================================================================

	Routine Description:
		It utilizes PRF-384 or PRF-512 to derive session-specific keys from a PMK.
		It shall be called by 4-way handshake processing.

	Arguments:
		pAd	-	pointer to our pAdapter context
		PMK		-	pointer to PMK
		ANonce	-	pointer to ANonce
		AA		-	pointer to Authenticator Address
		SNonce	-	pointer to SNonce
		SA		-	pointer to Supplicant Address
		len		-	indicate the length of PTK (octet)

	Return Value:
		Output		pointer to the PTK

	Note:
		Refer to IEEE 802.11i-2004 8.5.1.2

	========================================================================
*/
void WpaDerivePTK(struct rt_rtmp_adapter *pAd,
		  u8 * PMK,
		  u8 * ANonce,
		  u8 * AA,
		  u8 * SNonce,
		  u8 * SA, u8 * output, u32 len)
{
	u8 concatenation[76];
	u32 CurrPos = 0;
	u8 temp[32];
	u8 Prefix[] =
	    { 'P', 'a', 'i', 'r', 'w', 'i', 's', 'e', ' ', 'k', 'e', 'y', ' ',
		'e', 'x', 'p', 'a', 'n', 's', 'i', 'o', 'n'
	};

	/* initiate the concatenation input */
	NdisZeroMemory(temp, sizeof(temp));
	NdisZeroMemory(concatenation, 76);

	/* Get smaller address */
	if (RTMPCompareMemory(SA, AA, 6) == 1)
		NdisMoveMemory(concatenation, AA, 6);
	else
		NdisMoveMemory(concatenation, SA, 6);
	CurrPos += 6;

	/* Get larger address */
	if (RTMPCompareMemory(SA, AA, 6) == 1)
		NdisMoveMemory(&concatenation[CurrPos], SA, 6);
	else
		NdisMoveMemory(&concatenation[CurrPos], AA, 6);

	/* store the larger mac address for backward compatible of */
	/* ralink proprietary STA-key issue */
	NdisMoveMemory(temp, &concatenation[CurrPos], MAC_ADDR_LEN);
	CurrPos += 6;

	/* Get smaller Nonce */
	if (RTMPCompareMemory(ANonce, SNonce, 32) == 0)
		NdisMoveMemory(&concatenation[CurrPos], temp, 32);	/* patch for ralink proprietary STA-key issue */
	else if (RTMPCompareMemory(ANonce, SNonce, 32) == 1)
		NdisMoveMemory(&concatenation[CurrPos], SNonce, 32);
	else
		NdisMoveMemory(&concatenation[CurrPos], ANonce, 32);
	CurrPos += 32;

	/* Get larger Nonce */
	if (RTMPCompareMemory(ANonce, SNonce, 32) == 0)
		NdisMoveMemory(&concatenation[CurrPos], temp, 32);	/* patch for ralink proprietary STA-key issue */
	else if (RTMPCompareMemory(ANonce, SNonce, 32) == 1)
		NdisMoveMemory(&concatenation[CurrPos], ANonce, 32);
	else
		NdisMoveMemory(&concatenation[CurrPos], SNonce, 32);
	CurrPos += 32;

	hex_dump("concatenation=", concatenation, 76);

	/* Use PRF to generate PTK */
	PRF(PMK, LEN_MASTER_KEY, Prefix, 22, concatenation, 76, output, len);

}

/*
	========================================================================

	Routine Description:
		Generate random number by software.

	Arguments:
		pAd		-	pointer to our pAdapter context
		macAddr	-	pointer to local MAC address

	Return Value:

	Note:
		802.1ii-2004  Annex H.5

	========================================================================
*/
void GenRandom(struct rt_rtmp_adapter *pAd, u8 * macAddr, u8 * random)
{
	int i, curr;
	u8 local[80], KeyCounter[32];
	u8 result[80];
	unsigned long CurrentTime;
	u8 prefix[] =
	    { 'I', 'n', 'i', 't', ' ', 'C', 'o', 'u', 'n', 't', 'e', 'r' };

	/* Zero the related information */
	NdisZeroMemory(result, 80);
	NdisZeroMemory(local, 80);
	NdisZeroMemory(KeyCounter, 32);

	for (i = 0; i < 32; i++) {
		/* copy the local MAC address */
		COPY_MAC_ADDR(local, macAddr);
		curr = MAC_ADDR_LEN;

		/* concatenate the current time */
		NdisGetSystemUpTime(&CurrentTime);
		NdisMoveMemory(&local[curr], &CurrentTime, sizeof(CurrentTime));
		curr += sizeof(CurrentTime);

		/* concatenate the last result */
		NdisMoveMemory(&local[curr], result, 32);
		curr += 32;

		/* concatenate a variable */
		NdisMoveMemory(&local[curr], &i, 2);
		curr += 2;

		/* calculate the result */
		PRF(KeyCounter, 32, prefix, 12, local, curr, result, 32);
	}

	NdisMoveMemory(random, result, 32);
}

/*
	========================================================================

	Routine Description:
		Build cipher suite in RSN-IE.
		It only shall be called by RTMPMakeRSNIE.

	Arguments:
		pAd			-	pointer to our pAdapter context
	ElementID	-	indicate the WPA1 or WPA2
	WepStatus	-	indicate the encryption type
		bMixCipher	-	a boolean to indicate the pairwise cipher and group
						cipher are the same or not

	Return Value:

	Note:

	========================================================================
*/
static void RTMPMakeRsnIeCipher(struct rt_rtmp_adapter *pAd,
				u8 ElementID,
				u32 WepStatus,
				IN BOOLEAN bMixCipher,
				u8 FlexibleCipher,
				u8 *pRsnIe, u8 * rsn_len)
{
	u8 PairwiseCnt;

	*rsn_len = 0;

	/* decide WPA2 or WPA1 */
	if (ElementID == Wpa2Ie) {
		struct rt_rsnie2 *pRsnie_cipher = (struct rt_rsnie2 *)pRsnIe;

		/* Assign the verson as 1 */
		pRsnie_cipher->version = 1;

		switch (WepStatus) {
			/* TKIP mode */
		case Ndis802_11Encryption2Enabled:
			NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA2_TKIP, 4);
			pRsnie_cipher->ucount = 1;
			NdisMoveMemory(pRsnie_cipher->ucast[0].oui,
				       OUI_WPA2_TKIP, 4);
			*rsn_len = sizeof(struct rt_rsnie2);
			break;

			/* AES mode */
		case Ndis802_11Encryption3Enabled:
			if (bMixCipher)
				NdisMoveMemory(pRsnie_cipher->mcast,
					       OUI_WPA2_TKIP, 4);
			else
				NdisMoveMemory(pRsnie_cipher->mcast,
					       OUI_WPA2_CCMP, 4);
			pRsnie_cipher->ucount = 1;
			NdisMoveMemory(pRsnie_cipher->ucast[0].oui,
				       OUI_WPA2_CCMP, 4);
			*rsn_len = sizeof(struct rt_rsnie2);
			break;

			/* TKIP-AES mix mode */
		case Ndis802_11Encryption4Enabled:
			NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA2_TKIP, 4);

			PairwiseCnt = 1;
			/* Insert WPA2 TKIP as the first pairwise cipher */
			if (MIX_CIPHER_WPA2_TKIP_ON(FlexibleCipher)) {
				NdisMoveMemory(pRsnie_cipher->ucast[0].oui,
					       OUI_WPA2_TKIP, 4);
				/* Insert WPA2 AES as the secondary pairwise cipher */
				if (MIX_CIPHER_WPA2_AES_ON(FlexibleCipher)) {
					NdisMoveMemory(pRsnie_cipher->ucast[0].
						       oui + 4, OUI_WPA2_CCMP,
						       4);
					PairwiseCnt = 2;
				}
			} else {
				/* Insert WPA2 AES as the first pairwise cipher */
				NdisMoveMemory(pRsnie_cipher->ucast[0].oui,
					       OUI_WPA2_CCMP, 4);
			}

			pRsnie_cipher->ucount = PairwiseCnt;
			*rsn_len = sizeof(struct rt_rsnie2) + (4 * (PairwiseCnt - 1));
			break;
		}

		if ((pAd->OpMode == OPMODE_STA) &&
		    (pAd->StaCfg.GroupCipher != Ndis802_11Encryption2Enabled) &&
		    (pAd->StaCfg.GroupCipher != Ndis802_11Encryption3Enabled)) {
			u32 GroupCipher = pAd->StaCfg.GroupCipher;
			switch (GroupCipher) {
			case Ndis802_11GroupWEP40Enabled:
				NdisMoveMemory(pRsnie_cipher->mcast,
					       OUI_WPA2_WEP40, 4);
				break;
			case Ndis802_11GroupWEP104Enabled:
				NdisMoveMemory(pRsnie_cipher->mcast,
					       OUI_WPA2_WEP104, 4);
				break;
			}
		}
		/* swap for big-endian platform */
		pRsnie_cipher->version = cpu2le16(pRsnie_cipher->version);
		pRsnie_cipher->ucount = cpu2le16(pRsnie_cipher->ucount);
	} else {
		struct rt_rsnie *pRsnie_cipher = (struct rt_rsnie *)pRsnIe;

		/* Assign OUI and version */
		NdisMoveMemory(pRsnie_cipher->oui, OUI_WPA_VERSION, 4);
		pRsnie_cipher->version = 1;

		switch (WepStatus) {
			/* TKIP mode */
		case Ndis802_11Encryption2Enabled:
			NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA_TKIP, 4);
			pRsnie_cipher->ucount = 1;
			NdisMoveMemory(pRsnie_cipher->ucast[0].oui,
				       OUI_WPA_TKIP, 4);
			*rsn_len = sizeof(struct rt_rsnie);
			break;

			/* AES mode */
		case Ndis802_11Encryption3Enabled:
			if (bMixCipher)
				NdisMoveMemory(pRsnie_cipher->mcast,
					       OUI_WPA_TKIP, 4);
			else
				NdisMoveMemory(pRsnie_cipher->mcast,
					       OUI_WPA_CCMP, 4);
			pRsnie_cipher->ucount = 1;
			NdisMoveMemory(pRsnie_cipher->ucast[0].oui,
				       OUI_WPA_CCMP, 4);
			*rsn_len = sizeof(struct rt_rsnie);
			break;

			/* TKIP-AES mix mode */
		case Ndis802_11Encryption4Enabled:
			NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA_TKIP, 4);

			PairwiseCnt = 1;
			/* Insert WPA TKIP as the first pairwise cipher */
			if (MIX_CIPHER_WPA_TKIP_ON(FlexibleCipher)) {
				NdisMoveMemory(pRsnie_cipher->ucast[0].oui,
					       OUI_WPA_TKIP, 4);
				/* Insert WPA AES as the secondary pairwise cipher */
				if (MIX_CIPHER_WPA_AES_ON(FlexibleCipher)) {
					NdisMoveMemory(pRsnie_cipher->ucast[0].
						       oui + 4, OUI_WPA_CCMP,
						       4);
					PairwiseCnt = 2;
				}
			} else {
				/* Insert WPA AES as the first pairwise cipher */
				NdisMoveMemory(pRsnie_cipher->ucast[0].oui,
					       OUI_WPA_CCMP, 4);
			}

			pRsnie_cipher->ucount = PairwiseCnt;
			*rsn_len = sizeof(struct rt_rsnie) + (4 * (PairwiseCnt - 1));
			break;
		}

		if ((pAd->OpMode == OPMODE_STA) &&
		    (pAd->StaCfg.GroupCipher != Ndis802_11Encryption2Enabled) &&
		    (pAd->StaCfg.GroupCipher != Ndis802_11Encryption3Enabled)) {
			u32 GroupCipher = pAd->StaCfg.GroupCipher;
			switch (GroupCipher) {
			case Ndis802_11GroupWEP40Enabled:
				NdisMoveMemory(pRsnie_cipher->mcast,
					       OUI_WPA_WEP40, 4);
				break;
			case Ndis802_11GroupWEP104Enabled:
				NdisMoveMemory(pRsnie_cipher->mcast,
					       OUI_WPA_WEP104, 4);
				break;
			}
		}
		/* swap for big-endian platform */
		pRsnie_cipher->version = cpu2le16(pRsnie_cipher->version);
		pRsnie_cipher->ucount = cpu2le16(pRsnie_cipher->ucount);
	}
}

/*
	========================================================================

	Routine Description:
		Build AKM suite in RSN-IE.
		It only shall be called by RTMPMakeRSNIE.

	Arguments:
		pAd			-	pointer to our pAdapter context
	ElementID	-	indicate the WPA1 or WPA2
	AuthMode	-	indicate the authentication mode
		apidx		-	indicate the interface index

	Return Value:

	Note:

	========================================================================
*/
static void RTMPMakeRsnIeAKM(struct rt_rtmp_adapter *pAd,
			     u8 ElementID,
			     u32 AuthMode,
			     u8 apidx,
			     u8 *pRsnIe, u8 * rsn_len)
{
	struct rt_rsnie_auth *pRsnie_auth;
	u8 AkmCnt = 1;	/* default as 1 */

	pRsnie_auth = (struct rt_rsnie_auth *) (pRsnIe + (*rsn_len));

	/* decide WPA2 or WPA1 */
	if (ElementID == Wpa2Ie) {

		switch (AuthMode) {
		case Ndis802_11AuthModeWPA2:
		case Ndis802_11AuthModeWPA1WPA2:
			NdisMoveMemory(pRsnie_auth->auth[0].oui,
				       OUI_WPA2_8021X_AKM, 4);
			break;

		case Ndis802_11AuthModeWPA2PSK:
		case Ndis802_11AuthModeWPA1PSKWPA2PSK:
			NdisMoveMemory(pRsnie_auth->auth[0].oui,
				       OUI_WPA2_PSK_AKM, 4);
			break;
		default:
			AkmCnt = 0;
			break;

		}
	} else {
		switch (AuthMode) {
		case Ndis802_11AuthModeWPA:
		case Ndis802_11AuthModeWPA1WPA2:
			NdisMoveMemory(pRsnie_auth->auth[0].oui,
				       OUI_WPA_8021X_AKM, 4);
			break;

		case Ndis802_11AuthModeWPAPSK:
		case Ndis802_11AuthModeWPA1PSKWPA2PSK:
			NdisMoveMemory(pRsnie_auth->auth[0].oui,
				       OUI_WPA_PSK_AKM, 4);
			break;

		case Ndis802_11AuthModeWPANone:
			NdisMoveMemory(pRsnie_auth->auth[0].oui,
				       OUI_WPA_NONE_AKM, 4);
			break;
		default:
			AkmCnt = 0;
			break;
		}
	}

	pRsnie_auth->acount = AkmCnt;
	pRsnie_auth->acount = cpu2le16(pRsnie_auth->acount);

	/* update current RSNIE length */
	(*rsn_len) += (sizeof(struct rt_rsnie_auth) + (4 * (AkmCnt - 1)));

}

/*
	========================================================================

	Routine Description:
		Build capability in RSN-IE.
		It only shall be called by RTMPMakeRSNIE.

	Arguments:
		pAd			-	pointer to our pAdapter context
	ElementID	-	indicate the WPA1 or WPA2
		apidx		-	indicate the interface index

	Return Value:

	Note:

	========================================================================
*/
static void RTMPMakeRsnIeCap(struct rt_rtmp_adapter *pAd,
			     u8 ElementID,
			     u8 apidx,
			     u8 *pRsnIe, u8 * rsn_len)
{
	RSN_CAPABILITIES *pRSN_Cap;

	/* it could be ignored in WPA1 mode */
	if (ElementID == WpaIe)
		return;

	pRSN_Cap = (RSN_CAPABILITIES *) (pRsnIe + (*rsn_len));

	pRSN_Cap->word = cpu2le16(pRSN_Cap->word);

	(*rsn_len) += sizeof(RSN_CAPABILITIES);	/* update current RSNIE length */

}

/*
	========================================================================

	Routine Description:
		Build RSN IE context. It is not included element-ID and length.

	Arguments:
		pAd			-	pointer to our pAdapter context
	AuthMode	-	indicate the authentication mode
	WepStatus	-	indicate the encryption type
		apidx		-	indicate the interface index

	Return Value:

	Note:

	========================================================================
*/
void RTMPMakeRSNIE(struct rt_rtmp_adapter *pAd,
		   u32 AuthMode, u32 WepStatus, u8 apidx)
{
	u8 *pRsnIe = NULL;	/* primary RSNIE */
	u8 *rsnielen_cur_p = 0;	/* the length of the primary RSNIE */
	u8 *rsnielen_ex_cur_p = 0;	/* the length of the secondary RSNIE */
	u8 PrimaryRsnie;
	BOOLEAN bMixCipher = FALSE;	/* indicate the pairwise and group cipher are different */
	u8 p_offset;
	WPA_MIX_PAIR_CIPHER FlexibleCipher = WPA_TKIPAES_WPA2_TKIPAES;	/* it provide the more flexible cipher combination in WPA-WPA2 and TKIPAES mode */

	rsnielen_cur_p = NULL;
	rsnielen_ex_cur_p = NULL;

	{
		{
			if (pAd->StaCfg.WpaSupplicantUP !=
			    WPA_SUPPLICANT_DISABLE) {
				if (AuthMode < Ndis802_11AuthModeWPA)
					return;
			} else {
				/* Support WPAPSK or WPA2PSK in STA-Infra mode */
				/* Support WPANone in STA-Adhoc mode */
				if ((AuthMode != Ndis802_11AuthModeWPAPSK) &&
				    (AuthMode != Ndis802_11AuthModeWPA2PSK) &&
				    (AuthMode != Ndis802_11AuthModeWPANone)
				    )
					return;
			}

			DBGPRINT(RT_DEBUG_TRACE, ("==> RTMPMakeRSNIE(STA)\n"));

			/* Zero RSNIE context */
			pAd->StaCfg.RSNIE_Len = 0;
			NdisZeroMemory(pAd->StaCfg.RSN_IE, MAX_LEN_OF_RSNIE);

			/* Pointer to RSNIE */
			rsnielen_cur_p = &pAd->StaCfg.RSNIE_Len;
			pRsnIe = pAd->StaCfg.RSN_IE;

			bMixCipher = pAd->StaCfg.bMixCipher;
		}
	}

	/* indicate primary RSNIE as WPA or WPA2 */
	if ((AuthMode == Ndis802_11AuthModeWPA) ||
	    (AuthMode == Ndis802_11AuthModeWPAPSK) ||
	    (AuthMode == Ndis802_11AuthModeWPANone) ||
	    (AuthMode == Ndis802_11AuthModeWPA1WPA2) ||
	    (AuthMode == Ndis802_11AuthModeWPA1PSKWPA2PSK))
		PrimaryRsnie = WpaIe;
	else
		PrimaryRsnie = Wpa2Ie;

	{
		/* Build the primary RSNIE */
		/* 1. insert cipher suite */
		RTMPMakeRsnIeCipher(pAd, PrimaryRsnie, WepStatus, bMixCipher,
				    FlexibleCipher, pRsnIe, &p_offset);

		/* 2. insert AKM */
		RTMPMakeRsnIeAKM(pAd, PrimaryRsnie, AuthMode, apidx, pRsnIe,
				 &p_offset);

		/* 3. insert capability */
		RTMPMakeRsnIeCap(pAd, PrimaryRsnie, apidx, pRsnIe, &p_offset);
	}

	/* 4. update the RSNIE length */
	*rsnielen_cur_p = p_offset;

	hex_dump("The primary RSNIE", pRsnIe, (*rsnielen_cur_p));

}

/*
    ==========================================================================
    Description:
		Check whether the received frame is EAP frame.

	Arguments:
		pAd				-	pointer to our pAdapter context
		pEntry			-	pointer to active entry
		pData			-	the received frame
		DataByteCount	-	the received frame's length
		FromWhichBSSID	-	indicate the interface index

    Return:
         TRUE			-	This frame is EAP frame
         FALSE			-	otherwise
    ==========================================================================
*/
BOOLEAN RTMPCheckWPAframe(struct rt_rtmp_adapter *pAd,
			  struct rt_mac_table_entry *pEntry,
			  u8 *pData,
			  unsigned long DataByteCount, u8 FromWhichBSSID)
{
	unsigned long Body_len;
	BOOLEAN Cancelled;

	if (DataByteCount < (LENGTH_802_1_H + LENGTH_EAPOL_H))
		return FALSE;

	/* Skip LLC header */
	if (NdisEqualMemory(SNAP_802_1H, pData, 6) ||
	    /* Cisco 1200 AP may send packet with SNAP_BRIDGE_TUNNEL */
	    NdisEqualMemory(SNAP_BRIDGE_TUNNEL, pData, 6)) {
		pData += 6;
	}
	/* Skip 2-bytes EAPoL type */
	if (NdisEqualMemory(EAPOL, pData, 2)) {
		pData += 2;
	} else
		return FALSE;

	switch (*(pData + 1)) {
	case EAPPacket:
		Body_len = (*(pData + 2) << 8) | (*(pData + 3));
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Receive EAP-Packet frame, TYPE = 0, Length = %ld\n",
			  Body_len));
		break;
	case EAPOLStart:
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Receive EAPOL-Start frame, TYPE = 1 \n"));
		if (pEntry->EnqueueEapolStartTimerRunning !=
		    EAPOL_START_DISABLE) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Cancel the EnqueueEapolStartTimerRunning \n"));
			RTMPCancelTimer(&pEntry->EnqueueStartForPSKTimer,
					&Cancelled);
			pEntry->EnqueueEapolStartTimerRunning =
			    EAPOL_START_DISABLE;
		}
		break;
	case EAPOLLogoff:
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Receive EAPOLLogoff frame, TYPE = 2 \n"));
		break;
	case EAPOLKey:
		Body_len = (*(pData + 2) << 8) | (*(pData + 3));
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Receive EAPOL-Key frame, TYPE = 3, Length = %ld\n",
			  Body_len));
		break;
	case EAPOLASFAlert:
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Receive EAPOLASFAlert frame, TYPE = 4 \n"));
		break;
	default:
		return FALSE;

	}
	return TRUE;
}

/*
    ==========================================================================
    Description:
		Report the EAP message type

	Arguments:
		msg		-	EAPOL_PAIR_MSG_1
					EAPOL_PAIR_MSG_2
					EAPOL_PAIR_MSG_3
					EAPOL_PAIR_MSG_4
					EAPOL_GROUP_MSG_1
					EAPOL_GROUP_MSG_2

    Return:
         message type string

    ==========================================================================
*/
char *GetEapolMsgType(char msg)
{
	if (msg == EAPOL_PAIR_MSG_1)
		return "Pairwise Message 1";
	else if (msg == EAPOL_PAIR_MSG_2)
		return "Pairwise Message 2";
	else if (msg == EAPOL_PAIR_MSG_3)
		return "Pairwise Message 3";
	else if (msg == EAPOL_PAIR_MSG_4)
		return "Pairwise Message 4";
	else if (msg == EAPOL_GROUP_MSG_1)
		return "Group Message 1";
	else if (msg == EAPOL_GROUP_MSG_2)
		return "Group Message 2";
	else
		return "Invalid Message";
}

/*
	========================================================================

	Routine Description:
    Check Sanity RSN IE of EAPoL message

	Arguments:

	Return Value:

	========================================================================
*/
BOOLEAN RTMPCheckRSNIE(struct rt_rtmp_adapter *pAd,
		       u8 *pData,
		       u8 DataLen,
		       struct rt_mac_table_entry *pEntry, u8 * Offset)
{
	u8 *pVIE;
	u8 len;
	struct rt_eid * pEid;
	BOOLEAN result = FALSE;

	pVIE = pData;
	len = DataLen;
	*Offset = 0;

	while (len > sizeof(struct rt_rsnie2)) {
		pEid = (struct rt_eid *) pVIE;
		/* WPA RSN IE */
		if ((pEid->Eid == IE_WPA)
		    && (NdisEqualMemory(pEid->Octet, WPA_OUI, 4))) {
			if ((pEntry->AuthMode == Ndis802_11AuthModeWPA
			     || pEntry->AuthMode == Ndis802_11AuthModeWPAPSK)
			    &&
			    (NdisEqualMemory
			     (pVIE, pEntry->RSN_IE, pEntry->RSNIE_Len))
			    && (pEntry->RSNIE_Len == (pEid->Len + 2))) {
				result = TRUE;
			}

			*Offset += (pEid->Len + 2);
		}
		/* WPA2 RSN IE */
		else if ((pEid->Eid == IE_RSN)
			 && (NdisEqualMemory(pEid->Octet + 2, RSN_OUI, 3))) {
			if ((pEntry->AuthMode == Ndis802_11AuthModeWPA2
			     || pEntry->AuthMode == Ndis802_11AuthModeWPA2PSK)
			    && (pEid->Eid == pEntry->RSN_IE[0])
			    && ((pEid->Len + 2) >= pEntry->RSNIE_Len)
			    &&
			    (NdisEqualMemory
			     (pEid->Octet, &pEntry->RSN_IE[2],
			      pEntry->RSNIE_Len - 2))) {

				result = TRUE;
			}

			*Offset += (pEid->Len + 2);
		} else {
			break;
		}

		pVIE += (pEid->Len + 2);
		len -= (pEid->Len + 2);
	}

	return result;

}

/*
	========================================================================

	Routine Description:
    Parse KEYDATA field.  KEYDATA[] May contain 2 RSN IE and optionally GTK.
    GTK  is encaptulated in KDE format at  p.83 802.11i D10

	Arguments:

	Return Value:

	Note:
        802.11i D10

	========================================================================
*/
BOOLEAN RTMPParseEapolKeyData(struct rt_rtmp_adapter *pAd,
			      u8 *pKeyData,
			      u8 KeyDataLen,
			      u8 GroupKeyIndex,
			      u8 MsgType,
			      IN BOOLEAN bWPA2, struct rt_mac_table_entry *pEntry)
{
	struct rt_kde_encap * pKDE = NULL;
	u8 *pMyKeyData = pKeyData;
	u8 KeyDataLength = KeyDataLen;
	u8 GTKLEN = 0;
	u8 DefaultIdx = 0;
	u8 skip_offset;

	/* Verify The RSN IE contained in pairewise_msg_2 && pairewise_msg_3 and skip it */
	if (MsgType == EAPOL_PAIR_MSG_2 || MsgType == EAPOL_PAIR_MSG_3) {
		/* Check RSN IE whether it is WPA2/WPA2PSK */
		if (!RTMPCheckRSNIE
		    (pAd, pKeyData, KeyDataLen, pEntry, &skip_offset)) {
			/* send wireless event - for RSN IE different */
			if (pAd->CommonCfg.bWirelessEvent)
				RTMPSendWirelessEvent(pAd,
						      IW_RSNIE_DIFF_EVENT_FLAG,
						      pEntry->Addr,
						      pEntry->apidx, 0);

			DBGPRINT(RT_DEBUG_ERROR,
				 ("RSN_IE Different in msg %d of 4-way handshake!\n",
				  MsgType));
			hex_dump("Receive RSN_IE ", pKeyData, KeyDataLen);
			hex_dump("Desired RSN_IE ", pEntry->RSN_IE,
				 pEntry->RSNIE_Len);

			return FALSE;
		} else {
			if (bWPA2 && MsgType == EAPOL_PAIR_MSG_3) {
				WpaShowAllsuite(pMyKeyData, skip_offset);

				/* skip RSN IE */
				pMyKeyData += skip_offset;
				KeyDataLength -= skip_offset;
				DBGPRINT(RT_DEBUG_TRACE,
					 ("RTMPParseEapolKeyData ==> WPA2/WPA2PSK RSN IE matched in Msg 3, Length(%d) \n",
					  skip_offset));
			} else
				return TRUE;
		}
	}

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPParseEapolKeyData ==> KeyDataLength %d without RSN_IE \n",
		  KeyDataLength));
	/*hex_dump("remain data", pMyKeyData, KeyDataLength); */

	/* Parse EKD format in pairwise_msg_3_WPA2 && group_msg_1_WPA2 */
	if (bWPA2
	    && (MsgType == EAPOL_PAIR_MSG_3 || MsgType == EAPOL_GROUP_MSG_1)) {
		if (KeyDataLength >= 8)	/* KDE format exclude GTK length */
		{
			pKDE = (struct rt_kde_encap *) pMyKeyData;

			DefaultIdx = pKDE->GTKEncap.Kid;

			/* Sanity check - KED length */
			if (KeyDataLength < (pKDE->Len + 2)) {
				DBGPRINT(RT_DEBUG_ERROR,
					 ("ERROR: The len from KDE is too short \n"));
				return FALSE;
			}
			/* Get GTK length - refer to IEEE 802.11i-2004 p.82 */
			GTKLEN = pKDE->Len - 6;
			if (GTKLEN < LEN_AES_KEY) {
				DBGPRINT(RT_DEBUG_ERROR,
					 ("ERROR: GTK Key length is too short (%d) \n",
					  GTKLEN));
				return FALSE;
			}

		} else {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("ERROR: KDE format length is too short \n"));
			return FALSE;
		}

		DBGPRINT(RT_DEBUG_TRACE,
			 ("GTK in KDE format ,DefaultKeyID=%d, KeyLen=%d \n",
			  DefaultIdx, GTKLEN));
		/* skip it */
		pMyKeyData += 8;
		KeyDataLength -= 8;

	} else if (!bWPA2 && MsgType == EAPOL_GROUP_MSG_1) {
		DefaultIdx = GroupKeyIndex;
		DBGPRINT(RT_DEBUG_TRACE,
			 ("GTK DefaultKeyID=%d \n", DefaultIdx));
	}
	/* Sanity check - shared key index must be 1 ~ 3 */
	if (DefaultIdx < 1 || DefaultIdx > 3) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("ERROR: GTK Key index(%d) is invalid in %s %s \n",
			  DefaultIdx, ((bWPA2) ? "WPA2" : "WPA"),
			  GetEapolMsgType(MsgType)));
		return FALSE;
	}

	{
		struct rt_cipher_key *pSharedKey;

		/* set key material, TxMic and RxMic */
		NdisMoveMemory(pAd->StaCfg.GTK, pMyKeyData, 32);
		pAd->StaCfg.DefaultKeyId = DefaultIdx;

		pSharedKey = &pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId];

		/* Prepare pair-wise key information into shared key table */
		NdisZeroMemory(pSharedKey, sizeof(struct rt_cipher_key));
		pSharedKey->KeyLen = LEN_TKIP_EK;
		NdisMoveMemory(pSharedKey->Key, pAd->StaCfg.GTK, LEN_TKIP_EK);
		NdisMoveMemory(pSharedKey->RxMic, &pAd->StaCfg.GTK[16],
			       LEN_TKIP_RXMICK);
		NdisMoveMemory(pSharedKey->TxMic, &pAd->StaCfg.GTK[24],
			       LEN_TKIP_TXMICK);

		/* Update Shared Key CipherAlg */
		pSharedKey->CipherAlg = CIPHER_NONE;
		if (pAd->StaCfg.GroupCipher == Ndis802_11Encryption2Enabled)
			pSharedKey->CipherAlg = CIPHER_TKIP;
		else if (pAd->StaCfg.GroupCipher ==
			 Ndis802_11Encryption3Enabled)
			pSharedKey->CipherAlg = CIPHER_AES;
		else if (pAd->StaCfg.GroupCipher == Ndis802_11GroupWEP40Enabled)
			pSharedKey->CipherAlg = CIPHER_WEP64;
		else if (pAd->StaCfg.GroupCipher ==
			 Ndis802_11GroupWEP104Enabled)
			pSharedKey->CipherAlg = CIPHER_WEP128;

		/* Update group key information to ASIC Shared Key Table */
		AsicAddSharedKeyEntry(pAd,
				      BSS0,
				      pAd->StaCfg.DefaultKeyId,
				      pSharedKey->CipherAlg,
				      pSharedKey->Key,
				      pSharedKey->TxMic, pSharedKey->RxMic);

		/* Update ASIC WCID attribute table and IVEIV table */
		RTMPAddWcidAttributeEntry(pAd,
					  BSS0,
					  pAd->StaCfg.DefaultKeyId,
					  pSharedKey->CipherAlg, NULL);
	}

	return TRUE;

}

/*
	========================================================================

	Routine Description:
		Construct EAPoL message for WPA handshaking
		Its format is below,

		+--------------------+
		| Protocol Version	 |  1 octet
		+--------------------+
		| Protocol Type		 |	1 octet
		+--------------------+
		| Body Length		 |  2 octets
		+--------------------+
		| Descriptor Type	 |	1 octet
		+--------------------+
		| Key Information    |	2 octets
		+--------------------+
		| Key Length	     |  1 octet
		+--------------------+
		| Key Repaly Counter |	8 octets
		+--------------------+
		| Key Nonce		     |  32 octets
		+--------------------+
		| Key IV			 |  16 octets
		+--------------------+
		| Key RSC			 |  8 octets
		+--------------------+
		| Key ID or Reserved |	8 octets
		+--------------------+
		| Key MIC			 |	16 octets
		+--------------------+
		| Key Data Length	 |	2 octets
		+--------------------+
		| Key Data			 |	n octets
		+--------------------+

	Arguments:
		pAd			Pointer	to our adapter

	Return Value:
		None

	Note:

	========================================================================
*/
void ConstructEapolMsg(struct rt_mac_table_entry *pEntry,
		       u8 GroupKeyWepStatus,
		       u8 MsgType,
		       u8 DefaultKeyIdx,
		       u8 * KeyNonce,
		       u8 * TxRSC,
		       u8 * GTK,
		       u8 * RSNIE,
		       u8 RSNIE_Len, struct rt_eapol_packet * pMsg)
{
	BOOLEAN bWPA2 = FALSE;
	u8 KeyDescVer;

	/* Choose WPA2 or not */
	if ((pEntry->AuthMode == Ndis802_11AuthModeWPA2) ||
	    (pEntry->AuthMode == Ndis802_11AuthModeWPA2PSK))
		bWPA2 = TRUE;

	/* Init Packet and Fill header */
	pMsg->ProVer = EAPOL_VER;
	pMsg->ProType = EAPOLKey;

	/* Default 95 bytes, the EAPoL-Key descriptor exclude Key-data field */
	SET_u16_TO_ARRARY(pMsg->Body_Len, LEN_EAPOL_KEY_MSG);

	/* Fill in EAPoL descriptor */
	if (bWPA2)
		pMsg->KeyDesc.Type = WPA2_KEY_DESC;
	else
		pMsg->KeyDesc.Type = WPA1_KEY_DESC;

	/* Key Descriptor Version (bits 0-2) specifies the key descriptor version type */
	{
		/* Fill in Key information, refer to IEEE Std 802.11i-2004 page 78 */
		/* When either the pairwise or the group cipher is AES, the DESC_TYPE_AES(2) shall be used. */
		KeyDescVer =
		    (((pEntry->WepStatus == Ndis802_11Encryption3Enabled)
		      || (GroupKeyWepStatus ==
			  Ndis802_11Encryption3Enabled)) ? (DESC_TYPE_AES)
		     : (DESC_TYPE_TKIP));
	}

	pMsg->KeyDesc.KeyInfo.KeyDescVer = KeyDescVer;

	/* Specify Key Type as Group(0) or Pairwise(1) */
	if (MsgType >= EAPOL_GROUP_MSG_1)
		pMsg->KeyDesc.KeyInfo.KeyType = GROUPKEY;
	else
		pMsg->KeyDesc.KeyInfo.KeyType = PAIRWISEKEY;

	/* Specify Key Index, only group_msg1_WPA1 */
	if (!bWPA2 && (MsgType >= EAPOL_GROUP_MSG_1))
		pMsg->KeyDesc.KeyInfo.KeyIndex = DefaultKeyIdx;

	if (MsgType == EAPOL_PAIR_MSG_3)
		pMsg->KeyDesc.KeyInfo.Install = 1;

	if ((MsgType == EAPOL_PAIR_MSG_1) || (MsgType == EAPOL_PAIR_MSG_3)
	    || (MsgType == EAPOL_GROUP_MSG_1))
		pMsg->KeyDesc.KeyInfo.KeyAck = 1;

	if (MsgType != EAPOL_PAIR_MSG_1)
		pMsg->KeyDesc.KeyInfo.KeyMic = 1;

	if ((bWPA2 && (MsgType >= EAPOL_PAIR_MSG_3)) ||
	    (!bWPA2 && (MsgType >= EAPOL_GROUP_MSG_1))) {
		pMsg->KeyDesc.KeyInfo.Secure = 1;
	}

	if (bWPA2 && ((MsgType == EAPOL_PAIR_MSG_3) ||
		      (MsgType == EAPOL_GROUP_MSG_1))) {
		pMsg->KeyDesc.KeyInfo.EKD_DL = 1;
	}
	/* key Information element has done. */
	*(u16 *) (&pMsg->KeyDesc.KeyInfo) =
	    cpu2le16(*(u16 *) (&pMsg->KeyDesc.KeyInfo));

	/* Fill in Key Length */
	{
		if (MsgType >= EAPOL_GROUP_MSG_1) {
			/* the length of group key cipher */
			pMsg->KeyDesc.KeyLength[1] =
			    ((GroupKeyWepStatus ==
			      Ndis802_11Encryption2Enabled) ? TKIP_GTK_LENGTH :
			     LEN_AES_KEY);
		} else {
			/* the length of pairwise key cipher */
			pMsg->KeyDesc.KeyLength[1] =
			    ((pEntry->WepStatus ==
			      Ndis802_11Encryption2Enabled) ? LEN_TKIP_KEY :
			     LEN_AES_KEY);
		}
	}

	/* Fill in replay counter */
	NdisMoveMemory(pMsg->KeyDesc.ReplayCounter, pEntry->R_Counter,
		       LEN_KEY_DESC_REPLAY);

	/* Fill Key Nonce field */
	/* ANonce : pairwise_msg1 & pairwise_msg3 */
	/* SNonce : pairwise_msg2 */
	/* GNonce : group_msg1_wpa1 */
	if ((MsgType <= EAPOL_PAIR_MSG_3)
	    || ((!bWPA2 && (MsgType == EAPOL_GROUP_MSG_1))))
		NdisMoveMemory(pMsg->KeyDesc.KeyNonce, KeyNonce,
			       LEN_KEY_DESC_NONCE);

	/* Fill key IV - WPA2 as 0, WPA1 as random */
	if (!bWPA2 && (MsgType == EAPOL_GROUP_MSG_1)) {
		/* Suggest IV be random number plus some number, */
		NdisMoveMemory(pMsg->KeyDesc.KeyIv, &KeyNonce[16],
			       LEN_KEY_DESC_IV);
		pMsg->KeyDesc.KeyIv[15] += 2;
	}
	/* Fill Key RSC field */
	/* It contains the RSC for the GTK being installed. */
	if ((MsgType == EAPOL_PAIR_MSG_3 && bWPA2)
	    || (MsgType == EAPOL_GROUP_MSG_1)) {
		NdisMoveMemory(pMsg->KeyDesc.KeyRsc, TxRSC, 6);
	}
	/* Clear Key MIC field for MIC calculation later */
	NdisZeroMemory(pMsg->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);

	ConstructEapolKeyData(pEntry,
			      GroupKeyWepStatus,
			      KeyDescVer,
			      MsgType,
			      DefaultKeyIdx, GTK, RSNIE, RSNIE_Len, pMsg);

	/* Calculate MIC and fill in KeyMic Field except Pairwise Msg 1. */
	if (MsgType != EAPOL_PAIR_MSG_1) {
		CalculateMIC(KeyDescVer, pEntry->PTK, pMsg);
	}

	DBGPRINT(RT_DEBUG_TRACE,
		 ("===> ConstructEapolMsg for %s %s\n",
		  ((bWPA2) ? "WPA2" : "WPA"), GetEapolMsgType(MsgType)));
	DBGPRINT(RT_DEBUG_TRACE,
		 ("	     Body length = %d \n",
		  CONV_ARRARY_TO_u16(pMsg->Body_Len)));
	DBGPRINT(RT_DEBUG_TRACE,
		 ("	     Key length  = %d \n",
		  CONV_ARRARY_TO_u16(pMsg->KeyDesc.KeyLength)));

}

/*
	========================================================================

	Routine Description:
		Construct the Key Data field of EAPoL message

	Arguments:
		pAd			Pointer	to our adapter
		Elem		Message body

	Return Value:
		None

	Note:

	========================================================================
*/
void ConstructEapolKeyData(struct rt_mac_table_entry *pEntry,
			   u8 GroupKeyWepStatus,
			   u8 keyDescVer,
			   u8 MsgType,
			   u8 DefaultKeyIdx,
			   u8 * GTK,
			   u8 * RSNIE,
			   u8 RSNIE_LEN, struct rt_eapol_packet * pMsg)
{
	u8 *mpool, *Key_Data, *Rc4GTK;
	u8 ekey[(LEN_KEY_DESC_IV + LEN_EAP_EK)];
	unsigned long data_offset;
	BOOLEAN bWPA2Capable = FALSE;
	struct rt_rtmp_adapter *pAd = pEntry->pAd;
	BOOLEAN GTK_Included = FALSE;

	/* Choose WPA2 or not */
	if ((pEntry->AuthMode == Ndis802_11AuthModeWPA2) ||
	    (pEntry->AuthMode == Ndis802_11AuthModeWPA2PSK))
		bWPA2Capable = TRUE;

	if (MsgType == EAPOL_PAIR_MSG_1 ||
	    MsgType == EAPOL_PAIR_MSG_4 || MsgType == EAPOL_GROUP_MSG_2)
		return;

	/* allocate memory pool */
	os_alloc_mem(NULL, (u8 **) & mpool, 1500);

	if (mpool == NULL)
		return;

	/* Rc4GTK Len = 512 */
	Rc4GTK = (u8 *) ROUND_UP(mpool, 4);
	/* Key_Data Len = 512 */
	Key_Data = (u8 *) ROUND_UP(Rc4GTK + 512, 4);

	NdisZeroMemory(Key_Data, 512);
	SET_u16_TO_ARRARY(pMsg->KeyDesc.KeyDataLen, 0);
	data_offset = 0;

	/* Encapsulate RSNIE in pairwise_msg2 & pairwise_msg3 */
	if (RSNIE_LEN
	    && ((MsgType == EAPOL_PAIR_MSG_2)
		|| (MsgType == EAPOL_PAIR_MSG_3))) {
		u8 *pmkid_ptr = NULL;
		u8 pmkid_len = 0;

		RTMPInsertRSNIE(&Key_Data[data_offset],
				&data_offset,
				RSNIE, RSNIE_LEN, pmkid_ptr, pmkid_len);
	}

	/* Encapsulate KDE format in pairwise_msg3_WPA2 & group_msg1_WPA2 */
	if (bWPA2Capable
	    && ((MsgType == EAPOL_PAIR_MSG_3)
		|| (MsgType == EAPOL_GROUP_MSG_1))) {
		/* Key Data Encapsulation (KDE) format - 802.11i-2004  Figure-43w and Table-20h */
		Key_Data[data_offset + 0] = 0xDD;

		if (GroupKeyWepStatus == Ndis802_11Encryption3Enabled) {
			Key_Data[data_offset + 1] = 0x16;	/* 4+2+16(OUI+DataType+DataField) */
		} else {
			Key_Data[data_offset + 1] = 0x26;	/* 4+2+32(OUI+DataType+DataField) */
		}

		Key_Data[data_offset + 2] = 0x00;
		Key_Data[data_offset + 3] = 0x0F;
		Key_Data[data_offset + 4] = 0xAC;
		Key_Data[data_offset + 5] = 0x01;

		/* GTK KDE format - 802.11i-2004  Figure-43x */
		Key_Data[data_offset + 6] = (DefaultKeyIdx & 0x03);
		Key_Data[data_offset + 7] = 0x00;	/* Reserved Byte */

		data_offset += 8;
	}

	/* Encapsulate GTK */
	/* Only for pairwise_msg3_WPA2 and group_msg1 */
	if ((MsgType == EAPOL_PAIR_MSG_3 && bWPA2Capable)
	    || (MsgType == EAPOL_GROUP_MSG_1)) {
		/* Fill in GTK */
		if (GroupKeyWepStatus == Ndis802_11Encryption3Enabled) {
			NdisMoveMemory(&Key_Data[data_offset], GTK,
				       LEN_AES_KEY);
			data_offset += LEN_AES_KEY;
		} else {
			NdisMoveMemory(&Key_Data[data_offset], GTK,
				       TKIP_GTK_LENGTH);
			data_offset += TKIP_GTK_LENGTH;
		}

		GTK_Included = TRUE;
	}

	/* This whole key-data field shall be encrypted if a GTK is included. */
	/* Encrypt the data material in key data field with KEK */
	if (GTK_Included) {
		/*hex_dump("GTK_Included", Key_Data, data_offset); */

		if ((keyDescVer == DESC_TYPE_AES)) {
			u8 remainder = 0;
			u8 pad_len = 0;

			/* Key Descriptor Version 2 or 3: AES key wrap, defined in IETF RFC 3394, */
			/* shall be used to encrypt the Key Data field using the KEK field from */
			/* the derived PTK. */

			/* If the Key Data field uses the NIST AES key wrap, then the Key Data field */
			/* shall be padded before encrypting if the key data length is less than 16 */
			/* octets or if it is not a multiple of 8. The padding consists of appending */
			/* a single octet 0xdd followed by zero or more 0x00 octets. */
			if ((remainder = data_offset & 0x07) != 0) {
				int i;

				pad_len = (8 - remainder);
				Key_Data[data_offset] = 0xDD;
				for (i = 1; i < pad_len; i++)
					Key_Data[data_offset + i] = 0;

				data_offset += pad_len;
			}

			AES_GTK_KEY_WRAP(&pEntry->PTK[16], Key_Data,
					 data_offset, Rc4GTK);
			/* AES wrap function will grow 8 bytes in length */
			data_offset += 8;
		} else {
			/*      Key Descriptor Version 1: ARC4 is used to encrypt the Key Data field
			   using the KEK field from the derived PTK. */

			/* PREPARE Encrypted  "Key DATA" field.  (Encrypt GTK with RC4, usinf PTK[16]->[31] as Key, IV-field as IV) */
			/* put TxTsc in Key RSC field */
			pAd->PrivateInfo.FCSCRC32 = PPPINITFCS32;	/*Init crc32. */

			/* ekey is the contanetion of IV-field, and PTK[16]->PTK[31] */
			NdisMoveMemory(ekey, pMsg->KeyDesc.KeyIv,
				       LEN_KEY_DESC_IV);
			NdisMoveMemory(&ekey[LEN_KEY_DESC_IV], &pEntry->PTK[16],
				       LEN_EAP_EK);
			ARCFOUR_INIT(&pAd->PrivateInfo.WEPCONTEXT, ekey, sizeof(ekey));	/*INIT SBOX, KEYLEN+3(IV) */
			pAd->PrivateInfo.FCSCRC32 =
			    RTMP_CALC_FCS32(pAd->PrivateInfo.FCSCRC32, Key_Data,
					    data_offset);
			WPAARCFOUR_ENCRYPT(&pAd->PrivateInfo.WEPCONTEXT, Rc4GTK,
					   Key_Data, data_offset);
		}

		NdisMoveMemory(pMsg->KeyDesc.KeyData, Rc4GTK, data_offset);
	} else {
		NdisMoveMemory(pMsg->KeyDesc.KeyData, Key_Data, data_offset);
	}

	/* Update key data length field and total body length */
	SET_u16_TO_ARRARY(pMsg->KeyDesc.KeyDataLen, data_offset);
	INC_u16_TO_ARRARY(pMsg->Body_Len, data_offset);

	os_free_mem(NULL, mpool);

}

/*
	========================================================================

	Routine Description:
		Calcaulate MIC. It is used during 4-ways handsharking.

	Arguments:
		pAd			-	pointer to our pAdapter context
	PeerWepStatus	-	indicate the encryption type

	Return Value:

	Note:

	========================================================================
*/
static void CalculateMIC(u8 KeyDescVer,
			 u8 * PTK, struct rt_eapol_packet * pMsg)
{
	u8 *OutBuffer;
	unsigned long FrameLen = 0;
	u8 mic[LEN_KEY_DESC_MIC];
	u8 digest[80];

	/* allocate memory for MIC calculation */
	os_alloc_mem(NULL, (u8 **) & OutBuffer, 512);

	if (OutBuffer == NULL) {
		DBGPRINT(RT_DEBUG_ERROR, ("CalculateMIC: no memory!\n"));
		return;
	}
	/* make a frame for calculating MIC. */
	MakeOutgoingFrame(OutBuffer, &FrameLen,
			  CONV_ARRARY_TO_u16(pMsg->Body_Len) + 4, pMsg,
			  END_OF_ARGS);

	NdisZeroMemory(mic, sizeof(mic));

	/* Calculate MIC */
	if (KeyDescVer == DESC_TYPE_AES) {
		HMAC_SHA1(PTK, LEN_EAP_MICK, OutBuffer, FrameLen, digest,
			  SHA1_DIGEST_SIZE);
		NdisMoveMemory(mic, digest, LEN_KEY_DESC_MIC);
	} else {
		HMAC_MD5(PTK, LEN_EAP_MICK, OutBuffer, FrameLen, mic,
			 MD5_DIGEST_SIZE);
	}

	/* store the calculated MIC */
	NdisMoveMemory(pMsg->KeyDesc.KeyMic, mic, LEN_KEY_DESC_MIC);

	os_free_mem(NULL, OutBuffer);
}

/*
	========================================================================

	Routine Description:
		Some received frames can't decrypt by Asic, so decrypt them by software.

	Arguments:
		pAd			-	pointer to our pAdapter context
	PeerWepStatus	-	indicate the encryption type

	Return Value:
		NDIS_STATUS_SUCCESS		-	decryption successful
		NDIS_STATUS_FAILURE		-	decryption failure

	========================================================================
*/
int RTMPSoftDecryptBroadCastData(struct rt_rtmp_adapter *pAd,
					 struct rt_rx_blk *pRxBlk,
					 IN NDIS_802_11_ENCRYPTION_STATUS
					 GroupCipher, struct rt_cipher_key *pShard_key)
{
	struct rt_rxwi * pRxWI = pRxBlk->pRxWI;

	/* handle WEP decryption */
	if (GroupCipher == Ndis802_11Encryption1Enabled) {
		if (RTMPSoftDecryptWEP
		    (pAd, pRxBlk->pData, pRxWI->MPDUtotalByteCount,
		     pShard_key)) {

			/*Minus IV[4] & ICV[4] */
			pRxWI->MPDUtotalByteCount -= 8;
		} else {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("ERROR : Software decrypt WEP data fails.\n"));
			/* give up this frame */
			return NDIS_STATUS_FAILURE;
		}
	}
	/* handle TKIP decryption */
	else if (GroupCipher == Ndis802_11Encryption2Enabled) {
		if (RTMPSoftDecryptTKIP
		    (pAd, pRxBlk->pData, pRxWI->MPDUtotalByteCount, 0,
		     pShard_key)) {

			/*Minus 8 bytes MIC, 8 bytes IV/EIV, 4 bytes ICV */
			pRxWI->MPDUtotalByteCount -= 20;
		} else {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("ERROR : RTMPSoftDecryptTKIP Failed\n"));
			/* give up this frame */
			return NDIS_STATUS_FAILURE;
		}
	}
	/* handle AES decryption */
	else if (GroupCipher == Ndis802_11Encryption3Enabled) {
		if (RTMPSoftDecryptAES
		    (pAd, pRxBlk->pData, pRxWI->MPDUtotalByteCount,
		     pShard_key)) {

			/*8 bytes MIC, 8 bytes IV/EIV (CCMP Header) */
			pRxWI->MPDUtotalByteCount -= 16;
		} else {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("ERROR : RTMPSoftDecryptAES Failed\n"));
			/* give up this frame */
			return NDIS_STATUS_FAILURE;
		}
	} else {
		/* give up this frame */
		return NDIS_STATUS_FAILURE;
	}

	return NDIS_STATUS_SUCCESS;

}

u8 *GetSuiteFromRSNIE(u8 *rsnie,
			 u32 rsnie_len, u8 type, u8 * count)
{
	struct rt_eid * pEid;
	int len;
	u8 *pBuf;
	int offset = 0;
	struct rt_rsnie_auth *pAkm;
	u16 acount;
	BOOLEAN isWPA2 = FALSE;

	pEid = (struct rt_eid *) rsnie;
	len = rsnie_len - 2;	/* exclude IE and length */
	pBuf = (u8 *)& pEid->Octet[0];

	/* set default value */
	*count = 0;

	/* Check length */
	if ((len <= 0) || (pEid->Len != len)) {
		DBGPRINT_ERR(("%s : The length is invalid\n", __func__));
		return NULL;
	}
	/* Check WPA or WPA2 */
	if (pEid->Eid == IE_WPA) {
		struct rt_rsnie *pRsnie = (struct rt_rsnie *)pBuf;
		u16 ucount;

		if (len < sizeof(struct rt_rsnie)) {
			DBGPRINT_ERR(("%s : The length is too short for WPA\n",
				      __func__));
			return NULL;
		}
		/* Get the count of pairwise cipher */
		ucount = cpu2le16(pRsnie->ucount);
		if (ucount > 2) {
			DBGPRINT_ERR(("%s : The count(%d) of pairwise cipher is invlaid\n", __func__, ucount));
			return NULL;
		}
		/* Get the group cipher */
		if (type == GROUP_SUITE) {
			*count = 1;
			return pRsnie->mcast;
		}
		/* Get the pairwise cipher suite */
		else if (type == PAIRWISE_SUITE) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("%s : The count of pairwise cipher is %d\n",
				  __func__, ucount));
			*count = ucount;
			return pRsnie->ucast[0].oui;
		}

		offset = sizeof(struct rt_rsnie) + (4 * (ucount - 1));

	} else if (pEid->Eid == IE_RSN) {
		struct rt_rsnie2 *pRsnie = (struct rt_rsnie2 *)pBuf;
		u16 ucount;

		isWPA2 = TRUE;

		if (len < sizeof(struct rt_rsnie2)) {
			DBGPRINT_ERR(("%s : The length is too short for WPA2\n",
				      __func__));
			return NULL;
		}
		/* Get the count of pairwise cipher */
		ucount = cpu2le16(pRsnie->ucount);
		if (ucount > 2) {
			DBGPRINT_ERR(("%s : The count(%d) of pairwise cipher is invlaid\n", __func__, ucount));
			return NULL;
		}
		/* Get the group cipher */
		if (type == GROUP_SUITE) {
			*count = 1;
			return pRsnie->mcast;
		}
		/* Get the pairwise cipher suite */
		else if (type == PAIRWISE_SUITE) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("%s : The count of pairwise cipher is %d\n",
				  __func__, ucount));
			*count = ucount;
			return pRsnie->ucast[0].oui;
		}

		offset = sizeof(struct rt_rsnie2) + (4 * (ucount - 1));

	} else {
		DBGPRINT_ERR(("%s : Unknown IE (%d)\n", __func__, pEid->Eid));
		return NULL;
	}

	/* skip group cipher and pairwise cipher suite */
	pBuf += offset;
	len -= offset;

	if (len < sizeof(struct rt_rsnie_auth)) {
		DBGPRINT_ERR(("%s : The length of RSNIE is too short\n",
			      __func__));
		return NULL;
	}
	/* pointer to AKM count */
	pAkm = (struct rt_rsnie_auth *)pBuf;

	/* Get the count of pairwise cipher */
	acount = cpu2le16(pAkm->acount);
	if (acount > 2) {
		DBGPRINT_ERR(("%s : The count(%d) of AKM is invlaid\n",
			      __func__, acount));
		return NULL;
	}
	/* Get the AKM suite */
	if (type == AKM_SUITE) {
		DBGPRINT(RT_DEBUG_TRACE, ("%s : The count of AKM is %d\n",
					  __func__, acount));
		*count = acount;
		return pAkm->auth[0].oui;
	}
	offset = sizeof(struct rt_rsnie_auth) + (4 * (acount - 1));

	pBuf += offset;
	len -= offset;

	/* The remaining length must larger than (RSN-Capability(2) + PMKID-Count(2) + PMKID(16~)) */
	if (len >= (sizeof(RSN_CAPABILITIES) + 2 + LEN_PMKID)) {
		/* Skip RSN capability and PMKID-Count */
		pBuf += (sizeof(RSN_CAPABILITIES) + 2);
		len -= (sizeof(RSN_CAPABILITIES) + 2);

		/* Get PMKID */
		if (type == PMKID_LIST) {
			*count = 1;
			return pBuf;
		}
	} else {
		DBGPRINT_ERR(("%s : it can't get any more information beyond AKM \n", __func__));
		return NULL;
	}

	*count = 0;
	/*DBGPRINT_ERR(("%s : The type(%d) doesn't support \n", __func__, type)); */
	return NULL;

}

void WpaShowAllsuite(u8 *rsnie, u32 rsnie_len)
{
	u8 *pSuite = NULL;
	u8 count;

	hex_dump("RSNIE", rsnie, rsnie_len);

	/* group cipher */
	if ((pSuite =
	     GetSuiteFromRSNIE(rsnie, rsnie_len, GROUP_SUITE,
			       &count)) != NULL) {
		hex_dump("group cipher", pSuite, 4 * count);
	}
	/* pairwise cipher */
	if ((pSuite =
	     GetSuiteFromRSNIE(rsnie, rsnie_len, PAIRWISE_SUITE,
			       &count)) != NULL) {
		hex_dump("pairwise cipher", pSuite, 4 * count);
	}
	/* AKM */
	if ((pSuite =
	     GetSuiteFromRSNIE(rsnie, rsnie_len, AKM_SUITE, &count)) != NULL) {
		hex_dump("AKM suite", pSuite, 4 * count);
	}
	/* PMKID */
	if ((pSuite =
	     GetSuiteFromRSNIE(rsnie, rsnie_len, PMKID_LIST, &count)) != NULL) {
		hex_dump("PMKID", pSuite, LEN_PMKID);
	}

}

void RTMPInsertRSNIE(u8 *pFrameBuf,
		     unsigned long *pFrameLen,
		     u8 *rsnie_ptr,
		     u8 rsnie_len,
		     u8 *pmkid_ptr, u8 pmkid_len)
{
	u8 *pTmpBuf;
	unsigned long TempLen = 0;
	u8 extra_len = 0;
	u16 pmk_count = 0;
	u8 ie_num;
	u8 total_len = 0;
	u8 WPA2_OUI[3] = { 0x00, 0x0F, 0xAC };

	pTmpBuf = pFrameBuf;

	/* PMKID-List Must larger than 0 and the multiple of 16. */
	if (pmkid_len > 0 && ((pmkid_len & 0x0f) == 0)) {
		extra_len = sizeof(u16)+ pmkid_len;

		pmk_count = (pmkid_len >> 4);
		pmk_count = cpu2le16(pmk_count);
	} else {
		DBGPRINT(RT_DEBUG_WARN,
			 ("%s : The length is PMKID-List is invalid (%d), so don't insert it.\n",
			  __func__, pmkid_len));
	}

	if (rsnie_len != 0) {
		ie_num = IE_WPA;
		total_len = rsnie_len;

		if (NdisEqualMemory(rsnie_ptr + 2, WPA2_OUI, sizeof(WPA2_OUI))) {
			ie_num = IE_RSN;
			total_len += extra_len;
		}

		/* construct RSNIE body */
		MakeOutgoingFrame(pTmpBuf, &TempLen,
				  1, &ie_num,
				  1, &total_len,
				  rsnie_len, rsnie_ptr, END_OF_ARGS);

		pTmpBuf += TempLen;
		*pFrameLen = *pFrameLen + TempLen;

		if (ie_num == IE_RSN) {
			/* Insert PMKID-List field */
			if (extra_len > 0) {
				MakeOutgoingFrame(pTmpBuf, &TempLen,
						  2, &pmk_count,
						  pmkid_len, pmkid_ptr,
						  END_OF_ARGS);

				pTmpBuf += TempLen;
				*pFrameLen = *pFrameLen + TempLen;
			}
		}
	}

	return;
}
