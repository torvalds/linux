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

#define		WPARSNIE	0xdd
#define		WPA2RSNIE	0x30

//extern UCHAR BIT8[];
UCHAR	CipherWpaPskTkip[] = {
		0xDD, 0x16,				// RSN IE
		0x00, 0x50, 0xf2, 0x01,	// oui
		0x01, 0x00,				// Version
		0x00, 0x50, 0xf2, 0x02,	// Multicast
		0x01, 0x00,				// Number of unicast
		0x00, 0x50, 0xf2, 0x02,	// unicast
		0x01, 0x00,				// number of authentication method
		0x00, 0x50, 0xf2, 0x02	// authentication
		};
UCHAR	CipherWpaPskTkipLen = (sizeof(CipherWpaPskTkip) / sizeof(UCHAR));

UCHAR	CipherWpaPskAes[] = {
		0xDD, 0x16, 			// RSN IE
		0x00, 0x50, 0xf2, 0x01,	// oui
		0x01, 0x00,				// Version
		0x00, 0x50, 0xf2, 0x04,	// Multicast
		0x01, 0x00,				// Number of unicast
		0x00, 0x50, 0xf2, 0x04,	// unicast
		0x01, 0x00,				// number of authentication method
		0x00, 0x50, 0xf2, 0x02	// authentication
		};
UCHAR	CipherWpaPskAesLen = (sizeof(CipherWpaPskAes) / sizeof(UCHAR));

UCHAR	CipherSuiteCiscoCCKM[] = {
		0xDD, 0x16,				// RSN IE
		0x00, 0x50, 0xf2, 0x01, // oui
		0x01, 0x00,				// Version
		0x00, 0x40, 0x96, 0x01, // Multicast
		0x01, 0x00,				// Number of uicast
		0x00, 0x40, 0x96, 0x01, // unicast
		0x01, 0x00,				// number of authentication method
		0x00, 0x40, 0x96, 0x00  // Authentication
		};
UCHAR	CipherSuiteCiscoCCKMLen = (sizeof(CipherSuiteCiscoCCKM) / sizeof(UCHAR));

UCHAR	CipherSuiteCiscoCCKM24[] = {
		0xDD, 0x18,				// RSN IE
		0x00, 0x50, 0xf2, 0x01, // oui
		0x01, 0x00,				// Version
		0x00, 0x40, 0x96, 0x01, // Multicast
		0x01, 0x00,				// Number of uicast
		0x00, 0x40, 0x96, 0x01, // unicast
		0x01, 0x00,				// number of authentication method
		0x00, 0x40, 0x96, 0x00,
		0x28, 0x00// Authentication
		};

UCHAR	CipherSuiteCiscoCCKM24Len = (sizeof(CipherSuiteCiscoCCKM24) / sizeof(UCHAR));

UCHAR	CipherSuiteCCXTkip[] = {
		0xDD, 0x16,				// RSN IE
		0x00, 0x50, 0xf2, 0x01,	// oui
		0x01, 0x00,				// Version
		0x00, 0x50, 0xf2, 0x02,	// Multicast
		0x01, 0x00,				// Number of unicast
		0x00, 0x50, 0xf2, 0x02,	// unicast
		0x01, 0x00,				// number of authentication method
		0x00, 0x50, 0xf2, 0x01	// authentication
		};
UCHAR	CipherSuiteCCXTkipLen = (sizeof(CipherSuiteCCXTkip) / sizeof(UCHAR));

UCHAR	CCX_LLC_HDR[] = {0xAA, 0xAA, 0x03, 0x00, 0x40, 0x96, 0x00, 0x02};
UCHAR	LLC_NORMAL[] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00};

UCHAR	EAPOL_FRAME[] = {0x88, 0x8E};

BOOLEAN CheckRSNIE(
	IN  PRTMP_ADAPTER   pAd,
	IN  PUCHAR          pData,
	IN  UCHAR           DataLen,
	OUT	UCHAR			*Offset);

void inc_byte_array(UCHAR *counter, int len);

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
BOOLEAN	WpaMsgTypeSubst(
	IN	UCHAR	EAPType,
	OUT	INT		*MsgType)
{
	switch (EAPType)
	{
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
	==========================================================================
	Description:
		association	state machine init,	including state	transition and timer init
	Parameters:
		S -	pointer	to the association state machine
	==========================================================================
 */
VOID WpaPskStateMachineInit(
	IN	PRTMP_ADAPTER	pAd,
	IN	STATE_MACHINE *S,
	OUT	STATE_MACHINE_FUNC Trans[])
{
	StateMachineInit(S,	Trans, MAX_WPA_PSK_STATE, MAX_WPA_PSK_MSG, (STATE_MACHINE_FUNC)Drop, WPA_PSK_IDLE, WPA_MACHINE_BASE);
	StateMachineSetAction(S, WPA_PSK_IDLE, MT2_EAPOLKey, (STATE_MACHINE_FUNC)WpaEAPOLKeyAction);
}

/*
	==========================================================================
	Description:
		This is	state machine function.
		When receiving EAPOL packets which is  for 802.1x key management.
		Use	both in	WPA, and WPAPSK	case.
		In this	function, further dispatch to different	functions according	to the received	packet.	 3 categories are :
		  1.  normal 4-way pairwisekey and 2-way groupkey handshake
		  2.  MIC error	(Countermeasures attack)  report packet	from STA.
		  3.  Request for pairwise/group key update	from STA
	Return:
	==========================================================================
*/
VOID WpaEAPOLKeyAction(
	IN	PRTMP_ADAPTER	pAd,
	IN	MLME_QUEUE_ELEM	*Elem)

{
	INT             MsgType = EAPOL_MSG_INVALID;
	PKEY_DESCRIPTER pKeyDesc;
	PHEADER_802_11  pHeader; //red
	UCHAR           ZeroReplay[LEN_KEY_DESC_REPLAY];
	UCHAR EapolVr;
	KEY_INFO		peerKeyInfo;

	DBGPRINT(RT_DEBUG_TRACE, ("-----> WpaEAPOLKeyAction\n"));

	// Get 802.11 header first
	pHeader = (PHEADER_802_11) Elem->Msg;

	// Get EAPoL-Key Descriptor
	pKeyDesc = (PKEY_DESCRIPTER) &Elem->Msg[(LENGTH_802_11 + LENGTH_802_1_H + LENGTH_EAPOL_H)];

	NdisZeroMemory((PUCHAR)&peerKeyInfo, sizeof(peerKeyInfo));
	NdisMoveMemory((PUCHAR)&peerKeyInfo, (PUCHAR)&pKeyDesc->KeyInfo, sizeof(KEY_INFO));

	*((USHORT *)&peerKeyInfo) = cpu2le16(*((USHORT *)&peerKeyInfo));


	// 1. Check EAPOL frame version and type
	EapolVr	= (UCHAR) Elem->Msg[LENGTH_802_11+LENGTH_802_1_H];

    if (((EapolVr != EAPOL_VER) && (EapolVr != EAPOL_VER2)) || ((pKeyDesc->Type != WPA1_KEY_DESC) && (pKeyDesc->Type != WPA2_KEY_DESC)))
	{
        DBGPRINT(RT_DEBUG_ERROR, ("Key descripter does not match with WPA rule\n"));
			return;
	}

	// First validate replay counter, only accept message with larger replay counter
	// Let equal pass, some AP start with all zero replay counter
	NdisZeroMemory(ZeroReplay, LEN_KEY_DESC_REPLAY);

	if((RTMPCompareMemory(pKeyDesc->ReplayCounter, pAd->StaCfg.ReplayCounter, LEN_KEY_DESC_REPLAY) != 1) &&
		(RTMPCompareMemory(pKeyDesc->ReplayCounter, ZeroReplay, LEN_KEY_DESC_REPLAY) != 0))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("   ReplayCounter not match   \n"));
		return;
	}

	// Process WPA2PSK frame
	if(pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK)
	{
		if((peerKeyInfo.KeyType == PAIRWISEKEY) &&
			(peerKeyInfo.EKD_DL == 0) &&
			(peerKeyInfo.KeyAck == 1) &&
			(peerKeyInfo.KeyMic == 0) &&
			(peerKeyInfo.Secure == 0) &&
			(peerKeyInfo.Error == 0) &&
			(peerKeyInfo.Request == 0))
		{
			MsgType = EAPOL_PAIR_MSG_1;
			DBGPRINT(RT_DEBUG_TRACE, ("Receive EAPOL Key Pairwise Message 1\n"));
		} else if((peerKeyInfo.KeyType == PAIRWISEKEY) &&
			(peerKeyInfo.EKD_DL  == 1) &&
			(peerKeyInfo.KeyAck == 1) &&
			(peerKeyInfo.KeyMic == 1) &&
			(peerKeyInfo.Secure == 1) &&
			(peerKeyInfo.Error == 0) &&
			(peerKeyInfo.Request == 0))
		{
			MsgType = EAPOL_PAIR_MSG_3;
			DBGPRINT(RT_DEBUG_TRACE, ("Receive EAPOL Key Pairwise Message 3\n"));
		} else if((peerKeyInfo.KeyType == GROUPKEY) &&
			(peerKeyInfo.EKD_DL == 1) &&
			(peerKeyInfo.KeyAck == 1) &&
			(peerKeyInfo.KeyMic == 1) &&
			(peerKeyInfo.Secure == 1) &&
			(peerKeyInfo.Error == 0) &&
			(peerKeyInfo.Request == 0))
		{
			MsgType = EAPOL_GROUP_MSG_1;
			DBGPRINT(RT_DEBUG_TRACE, ("Receive EAPOL Key Group Message 1\n"));
		}

		// We will assume link is up (assoc suceess and port not secured).
		// All state has to be able to process message from previous state
		switch(pAd->StaCfg.WpaState)
		{
		case SS_START:
			if(MsgType == EAPOL_PAIR_MSG_1)
			{
				Wpa2PairMsg1Action(pAd, Elem);
				pAd->StaCfg.WpaState = SS_WAIT_MSG_3;
			}
			break;

		case SS_WAIT_MSG_3:
			if(MsgType == EAPOL_PAIR_MSG_1)
			{
				Wpa2PairMsg1Action(pAd, Elem);
				pAd->StaCfg.WpaState = SS_WAIT_MSG_3;
			}
			else if(MsgType == EAPOL_PAIR_MSG_3)
			{
				Wpa2PairMsg3Action(pAd, Elem);
				pAd->StaCfg.WpaState = SS_WAIT_GROUP;
			}
			break;

		case SS_WAIT_GROUP:		// When doing group key exchange
		case SS_FINISH:			// This happened when update group key
			if(MsgType == EAPOL_PAIR_MSG_1)
			{
			    // Reset port secured variable
				pAd->StaCfg.PortSecured = WPA_802_1X_PORT_NOT_SECURED;
				Wpa2PairMsg1Action(pAd, Elem);
				pAd->StaCfg.WpaState = SS_WAIT_MSG_3;
			}
			else if(MsgType == EAPOL_PAIR_MSG_3)
			{
			    // Reset port secured variable
				pAd->StaCfg.PortSecured = WPA_802_1X_PORT_NOT_SECURED;
				Wpa2PairMsg3Action(pAd, Elem);
				pAd->StaCfg.WpaState = SS_WAIT_GROUP;
			}
			else if(MsgType == EAPOL_GROUP_MSG_1)
			{
				WpaGroupMsg1Action(pAd, Elem);
				pAd->StaCfg.WpaState = SS_FINISH;
			}
			break;

		default:
			break;
		}
	}
	// Process WPAPSK Frame
	// Classify message Type, either pairwise message 1, 3, or group message 1 for supplicant
	else if(pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPAPSK)
	{
		if((peerKeyInfo.KeyType == PAIRWISEKEY) &&
			(peerKeyInfo.KeyIndex == 0) &&
			(peerKeyInfo.KeyAck == 1) &&
			(peerKeyInfo.KeyMic == 0) &&
			(peerKeyInfo.Secure == 0) &&
			(peerKeyInfo.Error == 0) &&
			(peerKeyInfo.Request == 0))
		{
			MsgType = EAPOL_PAIR_MSG_1;
			DBGPRINT(RT_DEBUG_TRACE, ("Receive EAPOL Key Pairwise Message 1\n"));
		}
		else if((peerKeyInfo.KeyType == PAIRWISEKEY) &&
			(peerKeyInfo.KeyIndex == 0) &&
			(peerKeyInfo.KeyAck == 1) &&
			(peerKeyInfo.KeyMic == 1) &&
			(peerKeyInfo.Secure == 0) &&
			(peerKeyInfo.Error == 0) &&
			(peerKeyInfo.Request == 0))
		{
			MsgType = EAPOL_PAIR_MSG_3;
			DBGPRINT(RT_DEBUG_TRACE, ("Receive EAPOL Key Pairwise Message 3\n"));
		}
		else if((peerKeyInfo.KeyType == GROUPKEY) &&
			(peerKeyInfo.KeyIndex != 0) &&
			(peerKeyInfo.KeyAck == 1) &&
			(peerKeyInfo.KeyMic == 1) &&
			(peerKeyInfo.Secure == 1) &&
			(peerKeyInfo.Error == 0) &&
			(peerKeyInfo.Request == 0))
		{
			MsgType = EAPOL_GROUP_MSG_1;
			DBGPRINT(RT_DEBUG_TRACE, ("Receive EAPOL Key Group Message 1\n"));
		}

		// We will assume link is up (assoc suceess and port not secured).
		// All state has to be able to process message from previous state
		switch(pAd->StaCfg.WpaState)
		{
		case SS_START:
			if(MsgType == EAPOL_PAIR_MSG_1)
			{
				WpaPairMsg1Action(pAd, Elem);
				pAd->StaCfg.WpaState = SS_WAIT_MSG_3;
			}
			break;

		case SS_WAIT_MSG_3:
			if(MsgType == EAPOL_PAIR_MSG_1)
			{
				WpaPairMsg1Action(pAd, Elem);
				pAd->StaCfg.WpaState = SS_WAIT_MSG_3;
			}
			else if(MsgType == EAPOL_PAIR_MSG_3)
			{
				WpaPairMsg3Action(pAd, Elem);
				pAd->StaCfg.WpaState = SS_WAIT_GROUP;
			}
			break;

		case SS_WAIT_GROUP:		// When doing group key exchange
		case SS_FINISH:			// This happened when update group key
			if(MsgType == EAPOL_PAIR_MSG_1)
			{
				WpaPairMsg1Action(pAd, Elem);
				pAd->StaCfg.WpaState = SS_WAIT_MSG_3;
				// Reset port secured variable
				pAd->StaCfg.PortSecured = WPA_802_1X_PORT_NOT_SECURED;
			}
			else if(MsgType == EAPOL_PAIR_MSG_3)
			{
				WpaPairMsg3Action(pAd, Elem);
				pAd->StaCfg.WpaState = SS_WAIT_GROUP;
				// Reset port secured variable
				pAd->StaCfg.PortSecured = WPA_802_1X_PORT_NOT_SECURED;
			}
			else if(MsgType == EAPOL_GROUP_MSG_1)
			{
				WpaGroupMsg1Action(pAd, Elem);
				pAd->StaCfg.WpaState = SS_FINISH;
			}
			break;

		default:
			break;
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<----- WpaEAPOLKeyAction\n"));
}

/*
	========================================================================

	Routine Description:
		Process Pairwise key 4-way handshaking

	Arguments:
		pAd	Pointer	to our adapter
		Elem		Message body

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	WpaPairMsg1Action(
	IN  PRTMP_ADAPTER   pAd,
	IN  MLME_QUEUE_ELEM *Elem)
{
	PHEADER_802_11      pHeader;
	UCHAR				*mpool, *PTK, *digest;
	PUCHAR              pOutBuffer = NULL;
	UCHAR               Header802_3[14];
	ULONG               FrameLen = 0;
	PEAPOL_PACKET       pMsg1;
	EAPOL_PACKET        Packet;
	UCHAR               Mic[16];

	DBGPRINT(RT_DEBUG_TRACE, ("WpaPairMsg1Action ----->\n"));

	// allocate memory pool
	os_alloc_mem(pAd, (PUCHAR *)&mpool, 256);

	if (mpool == NULL)
		return;

	// PTK Len = 80.
	PTK = (UCHAR *) ROUND_UP(mpool, 4);
	// digest Len = 80.
	digest = (UCHAR *) ROUND_UP(PTK + 80, 4);

	pHeader = (PHEADER_802_11) Elem->Msg;

	// Process message 1 from authenticator
	pMsg1 = (PEAPOL_PACKET) &Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];

	// 1. Save Replay counter, it will use to verify message 3 and construct message 2
	NdisMoveMemory(pAd->StaCfg.ReplayCounter, pMsg1->KeyDesc.ReplayCounter, LEN_KEY_DESC_REPLAY);

	// 2. Save ANonce
	NdisMoveMemory(pAd->StaCfg.ANonce, pMsg1->KeyDesc.KeyNonce, LEN_KEY_DESC_NONCE);

	// Generate random SNonce
	GenRandom(pAd, pAd->CurrentAddress, pAd->StaCfg.SNonce);

	// Calc PTK(ANonce, SNonce)
	WpaCountPTK(pAd,
		pAd->StaCfg.PMK,
		pAd->StaCfg.ANonce,
		pAd->CommonCfg.Bssid,
		pAd->StaCfg.SNonce,
		pAd->CurrentAddress,
		PTK,
		LEN_PTK);

	// Save key to PTK entry
	NdisMoveMemory(pAd->StaCfg.PTK, PTK, LEN_PTK);

	// init 802.3 header and Fill Packet
	MAKE_802_3_HEADER(Header802_3, pAd->CommonCfg.Bssid, pAd->CurrentAddress, EAPOL);

	// Zero Message 2 body
	NdisZeroMemory(&Packet, sizeof(Packet));
	Packet.ProVer	= EAPOL_VER;
	Packet.ProType	= EAPOLKey;
	//
	// Message 2 as  EAPOL-Key(0,1,0,0,0,P,0,SNonce,MIC,RSN IE)
	//
	Packet.KeyDesc.Type = WPA1_KEY_DESC;
	// 1. Key descriptor version and appropriate RSN IE
	if(pAd->StaCfg.WepStatus  == Ndis802_11Encryption3Enabled)
	{
		Packet.KeyDesc.KeyInfo.KeyDescVer = 2;
	}
	else	  // TKIP
	{
		Packet.KeyDesc.KeyInfo.KeyDescVer = 1;
	}

	// fill in Data Material and its length
	Packet.KeyDesc.KeyData[0] = IE_WPA;
	Packet.KeyDesc.KeyData[1] = pAd->StaCfg.RSNIE_Len;
	Packet.KeyDesc.KeyDataLen[1] = pAd->StaCfg.RSNIE_Len + 2;
	NdisMoveMemory(&Packet.KeyDesc.KeyData[2], pAd->StaCfg.RSN_IE, pAd->StaCfg.RSNIE_Len);

	// Update packet length after decide Key data payload
	Packet.Body_Len[1]  = sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE + Packet.KeyDesc.KeyDataLen[1];

	// Update Key length
	Packet.KeyDesc.KeyLength[0] = pMsg1->KeyDesc.KeyLength[0];
	Packet.KeyDesc.KeyLength[1] = pMsg1->KeyDesc.KeyLength[1];
	// 2. Key Type PeerKey
	Packet.KeyDesc.KeyInfo.KeyType = PAIRWISEKEY;

	// 3. KeyMic field presented
	Packet.KeyDesc.KeyInfo.KeyMic  = 1;

	//Convert to little-endian format.
	*((USHORT *)&Packet.KeyDesc.KeyInfo) = cpu2le16(*((USHORT *)&Packet.KeyDesc.KeyInfo));


	// 4. Fill SNonce
	NdisMoveMemory(Packet.KeyDesc.KeyNonce, pAd->StaCfg.SNonce, LEN_KEY_DESC_NONCE);

	// 5. Key Replay Count
	NdisMoveMemory(Packet.KeyDesc.ReplayCounter, pAd->StaCfg.ReplayCounter, LEN_KEY_DESC_REPLAY);

	// Send EAPOL(0, 1, 0, 0, 0, P, 0, SNonce, MIC, RSN_IE)
	// Out buffer for transmitting message 2
	MlmeAllocateMemory(pAd, (PUCHAR *)&pOutBuffer);  // allocate memory
	if(pOutBuffer == NULL)
	{
		os_free_mem(pAd, mpool);
		return;
	}
	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation
	MakeOutgoingFrame(pOutBuffer,           &FrameLen,
		Packet.Body_Len[1] + 4,    &Packet,
		END_OF_ARGS);

	// 6. Prepare and Fill MIC value
	NdisZeroMemory(Mic, sizeof(Mic));
	if(pAd->StaCfg.WepStatus  == Ndis802_11Encryption3Enabled)
	{	// AES

		HMAC_SHA1(pOutBuffer, FrameLen, PTK, LEN_EAP_MICK, digest);
		NdisMoveMemory(Mic, digest, LEN_KEY_DESC_MIC);
	}
	else
	{	// TKIP
		hmac_md5(PTK,  LEN_EAP_MICK, pOutBuffer, FrameLen, Mic);
	}
	NdisMoveMemory(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);

	//hex_dump("MIC", Mic, LEN_KEY_DESC_MIC);

		MakeOutgoingFrame(pOutBuffer,           	&FrameLen,
			  			LENGTH_802_3,     			&Header802_3,
						Packet.Body_Len[1] + 4,    &Packet,
						END_OF_ARGS);


	// 5. Copy frame to Tx ring and send Msg 2 to authenticator
	RTMPToWirelessSta(pAd, Header802_3, LENGTH_802_3, (PUCHAR)&Packet, Packet.Body_Len[1] + 4, TRUE);

	MlmeFreeMemory(pAd, (PUCHAR)pOutBuffer);
	os_free_mem(pAd, (PUCHAR)mpool);

	DBGPRINT(RT_DEBUG_TRACE, ("WpaPairMsg1Action <-----\n"));
}

VOID Wpa2PairMsg1Action(
	IN  PRTMP_ADAPTER   pAd,
	IN  MLME_QUEUE_ELEM *Elem)
{
	PHEADER_802_11      pHeader;
	UCHAR				*mpool, *PTK, *digest;
	PUCHAR              pOutBuffer = NULL;
	UCHAR               Header802_3[14];
	ULONG               FrameLen = 0;
	PEAPOL_PACKET       pMsg1;
	EAPOL_PACKET        Packet;
	UCHAR               Mic[16];

	DBGPRINT(RT_DEBUG_TRACE, ("Wpa2PairMsg1Action ----->\n"));

	// allocate memory pool
	os_alloc_mem(pAd, (PUCHAR *)&mpool, 256);

	if (mpool == NULL)
		return;

	// PTK Len = 80.
	PTK = (UCHAR *) ROUND_UP(mpool, 4);
	// digest Len = 80.
	digest = (UCHAR *) ROUND_UP(PTK + 80, 4);

	pHeader = (PHEADER_802_11) Elem->Msg;

	// Process message 1 from authenticator
		pMsg1 = (PEAPOL_PACKET)	&Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];

	// 1. Save Replay counter, it will use to verify message 3 and construct message 2
	NdisMoveMemory(pAd->StaCfg.ReplayCounter, pMsg1->KeyDesc.ReplayCounter, LEN_KEY_DESC_REPLAY);

	// 2. Save ANonce
	NdisMoveMemory(pAd->StaCfg.ANonce, pMsg1->KeyDesc.KeyNonce, LEN_KEY_DESC_NONCE);

	// Generate random SNonce
	GenRandom(pAd, pAd->CurrentAddress, pAd->StaCfg.SNonce);

	if(pMsg1->KeyDesc.KeyDataLen[1] > 0 )
	{
		// cached PMKID
	}

	// Calc PTK(ANonce, SNonce)
	WpaCountPTK(pAd,
		pAd->StaCfg.PMK,
		pAd->StaCfg.ANonce,
		pAd->CommonCfg.Bssid,
		pAd->StaCfg.SNonce,
		pAd->CurrentAddress,
		PTK,
		LEN_PTK);

	// Save key to PTK entry
	NdisMoveMemory(pAd->StaCfg.PTK, PTK, LEN_PTK);

	// init 802.3 header and Fill Packet
	MAKE_802_3_HEADER(Header802_3, pAd->CommonCfg.Bssid, pAd->CurrentAddress, EAPOL);

	// Zero message 2 body
	NdisZeroMemory(&Packet, sizeof(Packet));
	Packet.ProVer	= EAPOL_VER;
	Packet.ProType	= EAPOLKey;
	//
	// Message 2 as  EAPOL-Key(0,1,0,0,0,P,0,SNonce,MIC,RSN IE)
	//
	Packet.KeyDesc.Type = WPA2_KEY_DESC;

	// 1. Key descriptor version and appropriate RSN IE
	if(pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)
	{
		Packet.KeyDesc.KeyInfo.KeyDescVer = 2;
	}
	else	  // TKIP
	{
		Packet.KeyDesc.KeyInfo.KeyDescVer = 1;
	}

	// fill in Data Material and its length
	Packet.KeyDesc.KeyData[0] = IE_WPA2;
	Packet.KeyDesc.KeyData[1] = pAd->StaCfg.RSNIE_Len;
	Packet.KeyDesc.KeyDataLen[1] = pAd->StaCfg.RSNIE_Len + 2;
	NdisMoveMemory(&Packet.KeyDesc.KeyData[2], pAd->StaCfg.RSN_IE, pAd->StaCfg.RSNIE_Len);

	// Update packet length after decide Key data payload
	Packet.Body_Len[1]  = sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE + Packet.KeyDesc.KeyDataLen[1];

	// 2. Key Type PeerKey
	Packet.KeyDesc.KeyInfo.KeyType = PAIRWISEKEY;

	// 3. KeyMic field presented
	Packet.KeyDesc.KeyInfo.KeyMic  = 1;

	// Update Key Length
	Packet.KeyDesc.KeyLength[0] = 0;
	Packet.KeyDesc.KeyLength[1] = pMsg1->KeyDesc.KeyLength[1];

	// 4. Fill SNonce
	NdisMoveMemory(Packet.KeyDesc.KeyNonce, pAd->StaCfg.SNonce, LEN_KEY_DESC_NONCE);

	// 5. Key Replay Count
	NdisMoveMemory(Packet.KeyDesc.ReplayCounter, pAd->StaCfg.ReplayCounter, LEN_KEY_DESC_REPLAY);

	// Convert to little-endian format.
	*((USHORT *)&Packet.KeyDesc.KeyInfo) = cpu2le16(*((USHORT *)&Packet.KeyDesc.KeyInfo));

	// Send EAPOL-Key(0,1,0,0,0,P,0,SNonce,MIC,RSN IE)
	// Out buffer for transmitting message 2
	MlmeAllocateMemory(pAd,  (PUCHAR *)&pOutBuffer);  // allocate memory
	if(pOutBuffer == NULL)
	{
		os_free_mem(pAd, mpool);
		return;
	}

	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation
	MakeOutgoingFrame(pOutBuffer,        &FrameLen,
		Packet.Body_Len[1] + 4, &Packet,
		END_OF_ARGS);

	// 6. Prepare and Fill MIC value
	NdisZeroMemory(Mic, sizeof(Mic));
	if(pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)
	{
		// AES
		HMAC_SHA1(pOutBuffer, FrameLen, PTK, LEN_EAP_MICK, digest);
		NdisMoveMemory(Mic, digest, LEN_KEY_DESC_MIC);
	}
	else
	{
		hmac_md5(PTK,  LEN_EAP_MICK, pOutBuffer, FrameLen, Mic);
	}
	NdisMoveMemory(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);


	// Make  Transmitting frame
	MakeOutgoingFrame(pOutBuffer,           	&FrameLen,
			  			LENGTH_802_3,     		&Header802_3,
						Packet.Body_Len[1] + 4, &Packet,
						END_OF_ARGS);


	// 5. Copy frame to Tx ring
	RTMPToWirelessSta(pAd, Header802_3, LENGTH_802_3, (PUCHAR)&Packet, Packet.Body_Len[1] + 4, TRUE);

	MlmeFreeMemory(pAd, pOutBuffer);
	os_free_mem(pAd, mpool);

	DBGPRINT(RT_DEBUG_TRACE, ("Wpa2PairMsg1Action <-----\n"));

}

/*
	========================================================================

	Routine Description:
		Process Pairwise key 4-way handshaking

	Arguments:
		pAd	Pointer	to our adapter
		Elem		Message body

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	WpaPairMsg3Action(
	IN	PRTMP_ADAPTER	pAd,
	IN	MLME_QUEUE_ELEM	*Elem)

{
	PHEADER_802_11      pHeader;
	PUCHAR          	pOutBuffer = NULL;
	UCHAR               Header802_3[14];
	ULONG           	FrameLen = 0;
	EAPOL_PACKET        Packet;
	PEAPOL_PACKET       pMsg3;
	UCHAR           	Mic[16], OldMic[16];
	MAC_TABLE_ENTRY 	*pEntry = NULL;
	UCHAR				skip_offset;
	KEY_INFO			peerKeyInfo;

	DBGPRINT(RT_DEBUG_TRACE, ("WpaPairMsg3Action ----->\n"));

	// Record 802.11 header & the received EAPOL packet Msg3
	pHeader = (PHEADER_802_11) Elem->Msg;
	pMsg3 = (PEAPOL_PACKET)	&Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];

	NdisZeroMemory((PUCHAR)&peerKeyInfo, sizeof(peerKeyInfo));
	NdisMoveMemory((PUCHAR)&peerKeyInfo, (PUCHAR)&pMsg3->KeyDesc.KeyInfo, sizeof(KEY_INFO));

	*((USHORT*)&peerKeyInfo) = cpu2le16(*((USHORT*)&peerKeyInfo));


	// 1. Verify cipher type match
	if (pAd->StaCfg.WepStatus  == Ndis802_11Encryption3Enabled && (peerKeyInfo.KeyDescVer != 2))
	{
		return;
	}
	else if(pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled && (peerKeyInfo.KeyDescVer != 1))
	{
		return;
	}

	// Verify RSN IE
	//if (!RTMPEqualMemory(pMsg3->KeyDesc.KeyData, pAd->MacTab.Content[BSSID_WCID].RSN_IE, pAd->MacTab.Content[BSSID_WCID].RSNIE_Len))
	if (!CheckRSNIE(pAd, pMsg3->KeyDesc.KeyData, pMsg3->KeyDesc.KeyDataLen[1], &skip_offset))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("RSN_IE Different in Msg 3 of WPA1 4-way handshake!! \n"));
		hex_dump("The original RSN_IE", pAd->MacTab.Content[BSSID_WCID].RSN_IE, pAd->MacTab.Content[BSSID_WCID].RSNIE_Len);
		hex_dump("The received RSN_IE", pMsg3->KeyDesc.KeyData, pMsg3->KeyDesc.KeyDataLen[1]);
		return;
	}
	else
		DBGPRINT(RT_DEBUG_TRACE, ("RSN_IE VALID in Msg 3 of WPA1 4-way handshake!! \n"));


	// 2. Check MIC value
	// Save the MIC and replace with zero
	NdisMoveMemory(OldMic, pMsg3->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
	NdisZeroMemory(pMsg3->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
	if(pAd->StaCfg.WepStatus  == Ndis802_11Encryption3Enabled)
	{
		// AES
		UCHAR digest[80];

		HMAC_SHA1((PUCHAR) pMsg3, pMsg3->Body_Len[1] + 4, pAd->StaCfg.PTK, LEN_EAP_MICK, digest);
		NdisMoveMemory(Mic, digest, LEN_KEY_DESC_MIC);
	}
	else	// TKIP
	{
		hmac_md5(pAd->StaCfg.PTK, LEN_EAP_MICK, (PUCHAR) pMsg3, pMsg3->Body_Len[1] + 4, Mic);
	}

	if(!NdisEqualMemory(OldMic, Mic, LEN_KEY_DESC_MIC))
	{
		DBGPRINT(RT_DEBUG_ERROR, (" MIC Different in msg 3 of 4-way handshake!!!!!!!!!! \n"));
		return;
	}
	else
		DBGPRINT(RT_DEBUG_TRACE, (" MIC VALID in msg 3 of 4-way handshake!!!!!!!!!! \n"));

	// 3. Check Replay Counter, it has to be larger than last one. No need to be exact one larger
	if(RTMPCompareMemory(pMsg3->KeyDesc.ReplayCounter, pAd->StaCfg.ReplayCounter, LEN_KEY_DESC_REPLAY) != 1)
		return;

	// Update new replay counter
	NdisMoveMemory(pAd->StaCfg.ReplayCounter, pMsg3->KeyDesc.ReplayCounter, LEN_KEY_DESC_REPLAY);

	// 4. Double check ANonce
	if(!NdisEqualMemory(pAd->StaCfg.ANonce, pMsg3->KeyDesc.KeyNonce, LEN_KEY_DESC_NONCE))
		return;

	// init 802.3 header and Fill Packet
	MAKE_802_3_HEADER(Header802_3, pAd->CommonCfg.Bssid, pAd->CurrentAddress, EAPOL);

	// Zero Message 4 body
	NdisZeroMemory(&Packet, sizeof(Packet));
	Packet.ProVer	= EAPOL_VER;
	Packet.ProType	= EAPOLKey;
	Packet.Body_Len[1]  = sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE;		// No data field

	//
	// Message 4 as  EAPOL-Key(0,1,0,0,0,P,0,0,MIC,0)
	//
	Packet.KeyDesc.Type = WPA1_KEY_DESC;

	// Key descriptor version and appropriate RSN IE
	Packet.KeyDesc.KeyInfo.KeyDescVer = peerKeyInfo.KeyDescVer;

	// Update Key Length
	Packet.KeyDesc.KeyLength[0] = pMsg3->KeyDesc.KeyLength[0];
	Packet.KeyDesc.KeyLength[1] = pMsg3->KeyDesc.KeyLength[1];

	// Key Type PeerKey
	Packet.KeyDesc.KeyInfo.KeyType = PAIRWISEKEY;

	// KeyMic field presented
	Packet.KeyDesc.KeyInfo.KeyMic  = 1;

	// In Msg3,  KeyInfo.secure =0 if Group Key HS to come. 1 if no group key HS
	// Station sends Msg4  KeyInfo.secure should be the same as that in Msg.3
	Packet.KeyDesc.KeyInfo.Secure= peerKeyInfo.Secure;

	// Convert to little-endian format.
	*((USHORT *)&Packet.KeyDesc.KeyInfo) = cpu2le16(*((USHORT *)&Packet.KeyDesc.KeyInfo));

	// Key Replay count
	NdisMoveMemory(Packet.KeyDesc.ReplayCounter, pMsg3->KeyDesc.ReplayCounter, LEN_KEY_DESC_REPLAY);

	// Out buffer for transmitting message 4
	MlmeAllocateMemory(pAd, (PUCHAR *)&pOutBuffer);  // allocate memory
	if(pOutBuffer == NULL)
		return;

	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation
	MakeOutgoingFrame(pOutBuffer,           &FrameLen,
		Packet.Body_Len[1] + 4,    &Packet,
		END_OF_ARGS);

	// Prepare and Fill MIC value
	NdisZeroMemory(Mic, sizeof(Mic));
	if(pAd->StaCfg.WepStatus  == Ndis802_11Encryption3Enabled)
	{
		// AES
		UCHAR digest[80];

		HMAC_SHA1(pOutBuffer, FrameLen, pAd->StaCfg.PTK, LEN_EAP_MICK, digest);
		NdisMoveMemory(Mic, digest, LEN_KEY_DESC_MIC);
	}
	else
	{
		hmac_md5(pAd->StaCfg.PTK, LEN_EAP_MICK, pOutBuffer, FrameLen, Mic);
	}
	NdisMoveMemory(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);

	// Update PTK
	// Prepare pair-wise key information into shared key table
	NdisZeroMemory(&pAd->SharedKey[BSS0][0], sizeof(CIPHER_KEY));
	pAd->SharedKey[BSS0][0].KeyLen = LEN_TKIP_EK;
    NdisMoveMemory(pAd->SharedKey[BSS0][0].Key, &pAd->StaCfg.PTK[32], LEN_TKIP_EK);
	NdisMoveMemory(pAd->SharedKey[BSS0][0].RxMic, &pAd->StaCfg.PTK[48], LEN_TKIP_RXMICK);
	NdisMoveMemory(pAd->SharedKey[BSS0][0].TxMic, &pAd->StaCfg.PTK[48+LEN_TKIP_RXMICK], LEN_TKIP_TXMICK);

	// Decide its ChiperAlg
	if (pAd->StaCfg.PairCipher == Ndis802_11Encryption2Enabled)
		pAd->SharedKey[BSS0][0].CipherAlg = CIPHER_TKIP;
	else if (pAd->StaCfg.PairCipher == Ndis802_11Encryption3Enabled)
		pAd->SharedKey[BSS0][0].CipherAlg = CIPHER_AES;
	else
		pAd->SharedKey[BSS0][0].CipherAlg = CIPHER_NONE;

	// Update these related information to MAC_TABLE_ENTRY
	pEntry = &pAd->MacTab.Content[BSSID_WCID];
	NdisMoveMemory(pEntry->PairwiseKey.Key, &pAd->StaCfg.PTK[32], LEN_TKIP_EK);
	NdisMoveMemory(pEntry->PairwiseKey.RxMic, &pAd->StaCfg.PTK[48], LEN_TKIP_RXMICK);
	NdisMoveMemory(pEntry->PairwiseKey.TxMic, &pAd->StaCfg.PTK[48+LEN_TKIP_RXMICK], LEN_TKIP_TXMICK);
	pEntry->PairwiseKey.CipherAlg = pAd->SharedKey[BSS0][0].CipherAlg;

	// Update pairwise key information to ASIC Shared Key Table
	AsicAddSharedKeyEntry(pAd,
						  BSS0,
						  0,
						  pAd->SharedKey[BSS0][0].CipherAlg,
						  pAd->SharedKey[BSS0][0].Key,
						  pAd->SharedKey[BSS0][0].TxMic,
						  pAd->SharedKey[BSS0][0].RxMic);

	// Update ASIC WCID attribute table and IVEIV table
	RTMPAddWcidAttributeEntry(pAd,
							  BSS0,
							  0,
							  pAd->SharedKey[BSS0][0].CipherAlg,
							  pEntry);

	// Make transmitting frame
	MakeOutgoingFrame(pOutBuffer,           	&FrameLen,
			  			LENGTH_802_3,     		&Header802_3,
						Packet.Body_Len[1] + 4, &Packet,
						END_OF_ARGS);


	// Copy frame to Tx ring and Send Message 4 to authenticator
	RTMPToWirelessSta(pAd, Header802_3, LENGTH_802_3, (PUCHAR)&Packet, Packet.Body_Len[1] + 4, TRUE);

	MlmeFreeMemory(pAd, (PUCHAR)pOutBuffer);

	DBGPRINT(RT_DEBUG_TRACE, ("WpaPairMsg3Action <-----\n"));
}

VOID    Wpa2PairMsg3Action(
	IN  PRTMP_ADAPTER   pAd,
	IN  MLME_QUEUE_ELEM *Elem)

{
	PHEADER_802_11      pHeader;
	PUCHAR              pOutBuffer = NULL;
	UCHAR               Header802_3[14];
	ULONG               FrameLen = 0;
	EAPOL_PACKET        Packet;
	PEAPOL_PACKET       pMsg3;
	UCHAR               Mic[16], OldMic[16];
	UCHAR               *mpool, *KEYDATA, *digest;
	UCHAR               Key[32];
	MAC_TABLE_ENTRY 	*pEntry = NULL;
	KEY_INFO			peerKeyInfo;

	// allocate memory
	os_alloc_mem(pAd, (PUCHAR *)&mpool, 1024);

	if(mpool == NULL)
		return;

	// KEYDATA Len = 512.
	KEYDATA = (UCHAR *) ROUND_UP(mpool, 4);
	// digest Len = 80.
	digest = (UCHAR *) ROUND_UP(KEYDATA + 512, 4);

	DBGPRINT(RT_DEBUG_TRACE, ("Wpa2PairMsg3Action ----->\n"));

	pHeader = (PHEADER_802_11) Elem->Msg;

	// Process message 3 frame.
	pMsg3 = (PEAPOL_PACKET)	&Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];

	NdisZeroMemory((PUCHAR)&peerKeyInfo, sizeof(peerKeyInfo));
	NdisMoveMemory((PUCHAR)&peerKeyInfo, (PUCHAR)&pMsg3->KeyDesc.KeyInfo, sizeof(KEY_INFO));

	*((USHORT*)&peerKeyInfo) = cpu2le16(*((USHORT*)&peerKeyInfo));

	// 1. Verify cipher type match
	if (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled && (peerKeyInfo.KeyDescVer!= 2))
	{
		os_free_mem(pAd, (PUCHAR)mpool);
		return;
	}
	else if(pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled && (peerKeyInfo.KeyDescVer != 1))
	{
		os_free_mem(pAd, (PUCHAR)mpool);
		return;
	}

	// 2. Check MIC value
	// Save the MIC and replace with zero
	NdisMoveMemory(OldMic, pMsg3->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
	NdisZeroMemory(pMsg3->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
	if (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)
	{
		// AES
		HMAC_SHA1((PUCHAR) pMsg3, pMsg3->Body_Len[1] + 4, pAd->StaCfg.PTK, LEN_EAP_MICK, digest);
		NdisMoveMemory(Mic, digest, LEN_KEY_DESC_MIC);
	}
	else
	{
		hmac_md5(pAd->StaCfg.PTK, LEN_EAP_MICK, (PUCHAR) pMsg3, pMsg3->Body_Len[1] + 4, Mic);
	}

	if(!NdisEqualMemory(OldMic, Mic, LEN_KEY_DESC_MIC))
	{
		DBGPRINT(RT_DEBUG_ERROR, (" MIC Different in msg 3 of 4-way handshake!!!!!!!!!! \n"));
		os_free_mem(pAd, (PUCHAR)mpool);
		return;
	}
	else
		DBGPRINT(RT_DEBUG_TRACE, (" MIC VALID in msg 3 of 4-way handshake!!!!!!!!!! \n"));

	// 3. Check Replay Counter, it has to be larger than last one. No need to be exact one larger
	if(RTMPCompareMemory(pMsg3->KeyDesc.ReplayCounter, pAd->StaCfg.ReplayCounter, LEN_KEY_DESC_REPLAY) != 1)
	{
		os_free_mem(pAd, (PUCHAR)mpool);
		return;
	}

	// Update new replay counter
	NdisMoveMemory(pAd->StaCfg.ReplayCounter, pMsg3->KeyDesc.ReplayCounter, LEN_KEY_DESC_REPLAY);

	// 4. Double check ANonce
	if(!NdisEqualMemory(pAd->StaCfg.ANonce, pMsg3->KeyDesc.KeyNonce, LEN_KEY_DESC_NONCE))
	{
		os_free_mem(pAd, (PUCHAR)mpool);
		return;
	}

	// Obtain GTK
	// 5. Decrypt GTK from Key Data
	DBGPRINT_RAW(RT_DEBUG_TRACE, ("EKD = %d\n", peerKeyInfo.EKD_DL));
	if(pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)
	{
		// Decrypt AES GTK
		AES_GTK_KEY_UNWRAP(&pAd->StaCfg.PTK[16], KEYDATA, pMsg3->KeyDesc.KeyDataLen[1],pMsg3->KeyDesc.KeyData);
	}
	else	  // TKIP
	{
		INT i;
		// Decrypt TKIP GTK
		// Construct 32 bytes RC4 Key
		NdisMoveMemory(Key, pMsg3->KeyDesc.KeyIv, 16);
		NdisMoveMemory(&Key[16], &pAd->StaCfg.PTK[16], 16);
		ARCFOUR_INIT(&pAd->PrivateInfo.WEPCONTEXT, Key, 32);
		//discard first 256 bytes
		for(i = 0; i < 256; i++)
			ARCFOUR_BYTE(&pAd->PrivateInfo.WEPCONTEXT);
		// Decrypt GTK. Becareful, there is no ICV to check the result is correct or not
		ARCFOUR_DECRYPT(&pAd->PrivateInfo.WEPCONTEXT, KEYDATA, pMsg3->KeyDesc.KeyData, pMsg3->KeyDesc.KeyDataLen[1]);
	}

	if (!ParseKeyData(pAd, KEYDATA, pMsg3->KeyDesc.KeyDataLen[1], 1))
	{
		os_free_mem(pAd, (PUCHAR)mpool);
		return;
	}

	// Update GTK to ASIC
	// Update group key information to ASIC Shared Key Table
	AsicAddSharedKeyEntry(pAd,
						  BSS0,
						  pAd->StaCfg.DefaultKeyId,
						  pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg,
						  pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].Key,
						  pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].TxMic,
						  pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].RxMic);

	// Update ASIC WCID attribute table and IVEIV table
	RTMPAddWcidAttributeEntry(pAd,
							  BSS0,
							  pAd->StaCfg.DefaultKeyId,
							  pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg,
							  NULL);

	// init 802.3 header and Fill Packet
	MAKE_802_3_HEADER(Header802_3, pAd->CommonCfg.Bssid, pAd->CurrentAddress, EAPOL);

	// Zero message 4 body
	NdisZeroMemory(&Packet, sizeof(Packet));
	Packet.ProVer	= EAPOL_VER;
	Packet.ProType	= EAPOLKey;
	Packet.Body_Len[1]  	= sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE;		// No data field

	//
	// Message 4 as  EAPOL-Key(0,1,0,0,0,P,0,0,MIC,0)
	//
	Packet.KeyDesc.Type = WPA2_KEY_DESC;

	// Key descriptor version and appropriate RSN IE
	Packet.KeyDesc.KeyInfo.KeyDescVer = peerKeyInfo.KeyDescVer;

	// Update Key Length
	Packet.KeyDesc.KeyLength[0] = pMsg3->KeyDesc.KeyLength[0];
	Packet.KeyDesc.KeyLength[1] = pMsg3->KeyDesc.KeyLength[1];

	// Key Type PeerKey
	Packet.KeyDesc.KeyInfo.KeyType = PAIRWISEKEY;

	// KeyMic field presented
	Packet.KeyDesc.KeyInfo.KeyMic  = 1;
	Packet.KeyDesc.KeyInfo.Secure = 1;

	// Convert to little-endian format.
	*((USHORT *)&Packet.KeyDesc.KeyInfo) = cpu2le16(*((USHORT *)&Packet.KeyDesc.KeyInfo));

	// Key Replay count
	NdisMoveMemory(Packet.KeyDesc.ReplayCounter, pMsg3->KeyDesc.ReplayCounter, LEN_KEY_DESC_REPLAY);

	// Out buffer for transmitting message 4
	MlmeAllocateMemory(pAd, (PUCHAR *)&pOutBuffer);  // allocate memory
	if(pOutBuffer == NULL)
	{
		os_free_mem(pAd, (PUCHAR)mpool);
		return;
	}

	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation
	MakeOutgoingFrame(pOutBuffer,           &FrameLen,
		Packet.Body_Len[1] + 4,    &Packet,
		END_OF_ARGS);

	// Prepare and Fill MIC value
	NdisZeroMemory(Mic, sizeof(Mic));
	if(pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)
	{
		// AES
		HMAC_SHA1(pOutBuffer, FrameLen, pAd->StaCfg.PTK, LEN_EAP_MICK, digest);
		NdisMoveMemory(Mic, digest, LEN_KEY_DESC_MIC);
	}
	else
	{
		hmac_md5(pAd->StaCfg.PTK, LEN_EAP_MICK, pOutBuffer, FrameLen, Mic);
	}
	NdisMoveMemory(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);

	// Update PTK
	// Prepare pair-wise key information into shared key table
	NdisZeroMemory(&pAd->SharedKey[BSS0][0], sizeof(CIPHER_KEY));
	pAd->SharedKey[BSS0][0].KeyLen = LEN_TKIP_EK;
    NdisMoveMemory(pAd->SharedKey[BSS0][0].Key, &pAd->StaCfg.PTK[32], LEN_TKIP_EK);
	NdisMoveMemory(pAd->SharedKey[BSS0][0].RxMic, &pAd->StaCfg.PTK[48], LEN_TKIP_RXMICK);
	NdisMoveMemory(pAd->SharedKey[BSS0][0].TxMic, &pAd->StaCfg.PTK[48+LEN_TKIP_RXMICK], LEN_TKIP_TXMICK);

	// Decide its ChiperAlg
	if (pAd->StaCfg.PairCipher == Ndis802_11Encryption2Enabled)
		pAd->SharedKey[BSS0][0].CipherAlg = CIPHER_TKIP;
	else if (pAd->StaCfg.PairCipher == Ndis802_11Encryption3Enabled)
		pAd->SharedKey[BSS0][0].CipherAlg = CIPHER_AES;
	else
		pAd->SharedKey[BSS0][0].CipherAlg = CIPHER_NONE;

	// Update these related information to MAC_TABLE_ENTRY
	pEntry = &pAd->MacTab.Content[BSSID_WCID];
	NdisMoveMemory(&pEntry->PairwiseKey.Key, &pAd->StaCfg.PTK[32], LEN_TKIP_EK);
	NdisMoveMemory(&pEntry->PairwiseKey.RxMic, &pAd->StaCfg.PTK[48], LEN_TKIP_RXMICK);
	NdisMoveMemory(&pEntry->PairwiseKey.TxMic, &pAd->StaCfg.PTK[48+LEN_TKIP_RXMICK], LEN_TKIP_TXMICK);
	pEntry->PairwiseKey.CipherAlg = pAd->SharedKey[BSS0][0].CipherAlg;

	// Update pairwise key information to ASIC Shared Key Table
	AsicAddSharedKeyEntry(pAd,
						  BSS0,
						  0,
						  pAd->SharedKey[BSS0][0].CipherAlg,
						  pAd->SharedKey[BSS0][0].Key,
						  pAd->SharedKey[BSS0][0].TxMic,
						  pAd->SharedKey[BSS0][0].RxMic);

	// Update ASIC WCID attribute table and IVEIV table
	RTMPAddWcidAttributeEntry(pAd,
							  BSS0,
							  0,
							  pAd->SharedKey[BSS0][0].CipherAlg,
							  pEntry);

	// Make  Transmitting frame
	MakeOutgoingFrame(pOutBuffer,           	&FrameLen,
			  			LENGTH_802_3,     		&Header802_3,
						Packet.Body_Len[1] + 4, &Packet,
						END_OF_ARGS);


	// Copy frame to Tx ring and Send Message 4 to authenticator
	RTMPToWirelessSta(pAd, Header802_3, LENGTH_802_3, (PUCHAR)&Packet, Packet.Body_Len[1] + 4, TRUE);

	// set 802.1x port control
	STA_PORT_SECURED(pAd);

    // Indicate Connected for GUI
    pAd->IndicateMediaState = NdisMediaStateConnected;

	MlmeFreeMemory(pAd, (PUCHAR)pOutBuffer);
	os_free_mem(pAd, (PUCHAR)mpool);


	// send wireless event - for set key done WPA2
	if (pAd->CommonCfg.bWirelessEvent)
		RTMPSendWirelessEvent(pAd, IW_SET_KEY_DONE_WPA2_EVENT_FLAG, pEntry->Addr, BSS0, 0);

	DBGPRINT(RT_DEBUG_ERROR, ("Wpa2PairMsg3Action <-----\n"));

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
VOID	WpaGroupMsg1Action(
	IN	PRTMP_ADAPTER	pAd,
	IN	MLME_QUEUE_ELEM	*Elem)

{
	PUCHAR              pOutBuffer = NULL;
	UCHAR               Header802_3[14];
	ULONG               FrameLen = 0;
	EAPOL_PACKET        Packet;
	PEAPOL_PACKET       pGroup;
	UCHAR               *mpool, *digest, *KEYDATA;
	UCHAR               Mic[16], OldMic[16];
	UCHAR               GTK[32], Key[32];
	KEY_INFO			peerKeyInfo;

	// allocate memory
	os_alloc_mem(pAd, (PUCHAR *)&mpool, 1024);

	if(mpool == NULL)
		return;

	// digest Len = 80.
	digest = (UCHAR *) ROUND_UP(mpool, 4);
	// KEYDATA Len = 512.
	KEYDATA = (UCHAR *) ROUND_UP(digest + 80, 4);

	DBGPRINT(RT_DEBUG_TRACE, ("WpaGroupMsg1Action ----->\n"));

	// Process Group Message 1 frame. skip 802.11 header(24) & LLC_SNAP header(8)
	pGroup = (PEAPOL_PACKET) &Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];

	NdisZeroMemory((PUCHAR)&peerKeyInfo, sizeof(peerKeyInfo));
	NdisMoveMemory((PUCHAR)&peerKeyInfo, (PUCHAR)&pGroup->KeyDesc.KeyInfo, sizeof(KEY_INFO));

	*((USHORT*)&peerKeyInfo) = cpu2le16(*((USHORT*)&peerKeyInfo));

	// 0. Check cipher type match
	if (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled && (peerKeyInfo.KeyDescVer != 2))
	{
		os_free_mem(pAd, (PUCHAR)mpool);
		return;
	}
	else if (pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled && (peerKeyInfo.KeyDescVer != 1))
	{
		os_free_mem(pAd, (PUCHAR)mpool);
		return;
	}

	// 1. Verify Replay counter
	//    Check Replay Counter, it has to be larger than last one. No need to be exact one larger
	if(RTMPCompareMemory(pGroup->KeyDesc.ReplayCounter, pAd->StaCfg.ReplayCounter, LEN_KEY_DESC_REPLAY) != 1)
	{
		os_free_mem(pAd, (PUCHAR)mpool);
		return;
	}

	// Update new replay counter
	NdisMoveMemory(pAd->StaCfg.ReplayCounter, pGroup->KeyDesc.ReplayCounter, LEN_KEY_DESC_REPLAY);

	// 2. Verify MIC is valid
	// Save the MIC and replace with zero
	NdisMoveMemory(OldMic, pGroup->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
	NdisZeroMemory(pGroup->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);

	if(pAd->StaCfg.WepStatus  == Ndis802_11Encryption3Enabled)
	{	// AES
		HMAC_SHA1((PUCHAR) pGroup, pGroup->Body_Len[1] + 4, pAd->StaCfg.PTK, LEN_EAP_MICK, digest);
		NdisMoveMemory(Mic, digest, LEN_KEY_DESC_MIC);
	}
	else
	{	// TKIP
		hmac_md5(pAd->StaCfg.PTK, LEN_EAP_MICK, (PUCHAR) pGroup, pGroup->Body_Len[1] + 4, Mic);
	}

	if(!NdisEqualMemory(OldMic, Mic, LEN_KEY_DESC_MIC))
	{
		DBGPRINT(RT_DEBUG_ERROR, (" MIC Different in group msg 1 of 2-way handshake!!!!!!!!!! \n"));
		MlmeFreeMemory(pAd, (PUCHAR)mpool);
		return;
	}
	else
		DBGPRINT(RT_DEBUG_TRACE, (" MIC VALID in group msg 1 of 2-way handshake!!!!!!!!!! \n"));


	// 3. Decrypt GTK from Key Data
	if (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)
	{
		// Decrypt AES GTK
		AES_GTK_KEY_UNWRAP(&pAd->StaCfg.PTK[16], KEYDATA,  pGroup->KeyDesc.KeyDataLen[1], pGroup->KeyDesc.KeyData);
	}
	else	// TKIP
	{
		INT i;

		// Decrypt TKIP GTK
		// Construct 32 bytes RC4 Key
		NdisMoveMemory(Key, pGroup->KeyDesc.KeyIv, 16);
		NdisMoveMemory(&Key[16], &pAd->StaCfg.PTK[16], 16);
		ARCFOUR_INIT(&pAd->PrivateInfo.WEPCONTEXT, Key, 32);
		//discard first 256 bytes
		for(i = 0; i < 256; i++)
			ARCFOUR_BYTE(&pAd->PrivateInfo.WEPCONTEXT);
		// Decrypt GTK. Becareful, there is no ICV to check the result is correct or not
		ARCFOUR_DECRYPT(&pAd->PrivateInfo.WEPCONTEXT, KEYDATA, pGroup->KeyDesc.KeyData, pGroup->KeyDesc.KeyDataLen[1]);
	}

	// Process decrypted key data material
	// Parse keyData to handle KDE format for WPA2PSK
	if (peerKeyInfo.EKD_DL)
	{
		if (!ParseKeyData(pAd, KEYDATA, pGroup->KeyDesc.KeyDataLen[1], 0))
		{
			os_free_mem(pAd, (PUCHAR)mpool);
			return;
		}
	}
	else	// WPAPSK
	{
		// set key material, TxMic and RxMic for WPAPSK
		NdisMoveMemory(GTK, KEYDATA, 32);
		NdisMoveMemory(pAd->StaCfg.GTK, GTK, 32);
		pAd->StaCfg.DefaultKeyId = peerKeyInfo.KeyIndex;

		// Prepare pair-wise key information into shared key table
		NdisZeroMemory(&pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId], sizeof(CIPHER_KEY));
		pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].KeyLen = LEN_TKIP_EK;
		NdisMoveMemory(pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].Key, GTK, LEN_TKIP_EK);
		NdisMoveMemory(pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].RxMic, &GTK[16], LEN_TKIP_RXMICK);
		NdisMoveMemory(pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].TxMic, &GTK[24], LEN_TKIP_TXMICK);

		// Update Shared Key CipherAlg
		pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg = CIPHER_NONE;
		if (pAd->StaCfg.GroupCipher == Ndis802_11Encryption2Enabled)
			pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg = CIPHER_TKIP;
		else if (pAd->StaCfg.GroupCipher == Ndis802_11Encryption3Enabled)
			pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg = CIPHER_AES;
		else if (pAd->StaCfg.GroupCipher == Ndis802_11GroupWEP40Enabled)
			pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg = CIPHER_WEP64;
		else if (pAd->StaCfg.GroupCipher == Ndis802_11GroupWEP104Enabled)
			pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg = CIPHER_WEP128;

    	//hex_dump("Group Key :", pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].Key, LEN_TKIP_EK);
	}

	// Update group key information to ASIC Shared Key Table
	AsicAddSharedKeyEntry(pAd,
						  BSS0,
						  pAd->StaCfg.DefaultKeyId,
						  pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg,
						  pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].Key,
						  pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].TxMic,
						  pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].RxMic);

	// Update ASIC WCID attribute table and IVEIV table
	RTMPAddWcidAttributeEntry(pAd,
							  BSS0,
							  pAd->StaCfg.DefaultKeyId,
							  pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg,
							  NULL);

	// set 802.1x port control
	STA_PORT_SECURED(pAd);

    // Indicate Connected for GUI
    pAd->IndicateMediaState = NdisMediaStateConnected;

	// init header and Fill Packet
	MAKE_802_3_HEADER(Header802_3, pAd->CommonCfg.Bssid, pAd->CurrentAddress, EAPOL);

	// Zero Group message 1 body
	NdisZeroMemory(&Packet, sizeof(Packet));
	Packet.ProVer	= EAPOL_VER;
	Packet.ProType	= EAPOLKey;
	Packet.Body_Len[1]  = sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE;		// No data field

	//
	// Group Message 2 as  EAPOL-Key(1,0,0,0,G,0,0,MIC,0)
	//
	if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK)
	{
		Packet.KeyDesc.Type = WPA2_KEY_DESC;
	}
	else
	{
		Packet.KeyDesc.Type = WPA1_KEY_DESC;
	}

	// Key descriptor version and appropriate RSN IE
	Packet.KeyDesc.KeyInfo.KeyDescVer = peerKeyInfo.KeyDescVer;

	// Update Key Length
	Packet.KeyDesc.KeyLength[0] = pGroup->KeyDesc.KeyLength[0];
	Packet.KeyDesc.KeyLength[1] = pGroup->KeyDesc.KeyLength[1];

	// Key Index as G-Msg 1
	if(pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPAPSK)
		Packet.KeyDesc.KeyInfo.KeyIndex = peerKeyInfo.KeyIndex;

	// Key Type Group key
	Packet.KeyDesc.KeyInfo.KeyType = GROUPKEY;

	// KeyMic field presented
	Packet.KeyDesc.KeyInfo.KeyMic  = 1;

	// Secure bit
	Packet.KeyDesc.KeyInfo.Secure  = 1;

	// Convert to little-endian format.
	*((USHORT *)&Packet.KeyDesc.KeyInfo) = cpu2le16(*((USHORT *)&Packet.KeyDesc.KeyInfo));

	// Key Replay count
	NdisMoveMemory(Packet.KeyDesc.ReplayCounter, pGroup->KeyDesc.ReplayCounter, LEN_KEY_DESC_REPLAY);

	// Out buffer for transmitting group message 2
	MlmeAllocateMemory(pAd, (PUCHAR *)&pOutBuffer);  // allocate memory
	if(pOutBuffer == NULL)
	{
		MlmeFreeMemory(pAd, (PUCHAR)mpool);
		return;
	}

	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation
	MakeOutgoingFrame(pOutBuffer,           &FrameLen,
		Packet.Body_Len[1] + 4,    &Packet,
		END_OF_ARGS);

	// Prepare and Fill MIC value
	NdisZeroMemory(Mic, sizeof(Mic));
	if(pAd->StaCfg.WepStatus  == Ndis802_11Encryption3Enabled)
	{
		// AES
		HMAC_SHA1(pOutBuffer, FrameLen, pAd->StaCfg.PTK, LEN_EAP_MICK, digest);
		NdisMoveMemory(Mic, digest, LEN_KEY_DESC_MIC);
	}
	else
	{
		hmac_md5(pAd->StaCfg.PTK, LEN_EAP_MICK, pOutBuffer, FrameLen, Mic);
	}
	NdisMoveMemory(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);


	MakeOutgoingFrame(pOutBuffer,       		&FrameLen,
						LENGTH_802_3,     		&Header802_3,
						Packet.Body_Len[1] + 4, &Packet,
						END_OF_ARGS);


	// 5. Copy frame to Tx ring and prepare for encryption
	RTMPToWirelessSta(pAd, Header802_3, LENGTH_802_3, (PUCHAR)&Packet, Packet.Body_Len[1] + 4, FALSE);

	// 6 Free allocated memory
	MlmeFreeMemory(pAd, (PUCHAR)pOutBuffer);
	os_free_mem(pAd, (PUCHAR)mpool);

	// send wireless event - for set key done WPA2
	if (pAd->CommonCfg.bWirelessEvent)
		RTMPSendWirelessEvent(pAd, IW_SET_KEY_DONE_WPA2_EVENT_FLAG, pAd->MacTab.Content[BSSID_WCID].Addr, BSS0, 0);

	DBGPRINT(RT_DEBUG_TRACE, ("WpaGroupMsg1Action <-----\n"));
}

/*
	========================================================================

	Routine Description:
		Init WPA MAC header

	Arguments:
		pAd	Pointer	to our adapter

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	WpaMacHeaderInit(
	IN		PRTMP_ADAPTER	pAd,
	IN OUT	PHEADER_802_11	pHdr80211,
	IN		UCHAR			wep,
	IN		PUCHAR		    pAddr1)
{
	NdisZeroMemory(pHdr80211, sizeof(HEADER_802_11));
	pHdr80211->FC.Type	= BTYPE_DATA;
	pHdr80211->FC.ToDs	= 1;
	if (wep	== 1)
		pHdr80211->FC.Wep = 1;

	 //	Addr1: BSSID, Addr2: SA, Addr3:	DA
	COPY_MAC_ADDR(pHdr80211->Addr1, pAddr1);
	COPY_MAC_ADDR(pHdr80211->Addr2, pAd->CurrentAddress);
	COPY_MAC_ADDR(pHdr80211->Addr3, pAd->CommonCfg.Bssid);
	pHdr80211->Sequence =	pAd->Sequence;
}

/*
	========================================================================

	Routine	Description:
		Copy frame from waiting queue into relative ring buffer and set
	appropriate ASIC register to kick hardware encryption before really
	sent out to air.

	Arguments:
		pAd		Pointer	to our adapter
		PNDIS_PACKET	Pointer to outgoing Ndis frame
		NumberOfFrag	Number of fragment required

	Return Value:
		None

	Note:

	========================================================================
*/
VOID    RTMPToWirelessSta(
	IN	PRTMP_ADAPTER	pAd,
	IN  PUCHAR          pHeader802_3,
    IN  UINT            HdrLen,
	IN  PUCHAR          pData,
    IN  UINT            DataLen,
    IN	BOOLEAN			is4wayFrame)

{
	NDIS_STATUS     Status;
	PNDIS_PACKET    pPacket;
	UCHAR   Index;

	do
	{
		// 1. build a NDIS packet and call RTMPSendPacket();
		//    be careful about how/when to release this internal allocated NDIS PACKET buffer
		Status = RTMPAllocateNdisPacket(pAd, &pPacket, pHeader802_3, HdrLen, pData, DataLen);
		if (Status != NDIS_STATUS_SUCCESS)
			break;

		if (is4wayFrame)
			RTMP_SET_PACKET_CLEAR_EAP_FRAME(pPacket, 1);
		else
			RTMP_SET_PACKET_CLEAR_EAP_FRAME(pPacket, 0);

		// 2. send out the packet
		Status = STASendPacket(pAd, pPacket);
		if(Status == NDIS_STATUS_SUCCESS)
		{
			// Dequeue one frame from TxSwQueue0..3 queue and process it
			// There are three place calling dequeue for TX ring.
			// 1. Here, right after queueing the frame.
			// 2. At the end of TxRingTxDone service routine.
			// 3. Upon NDIS call RTMPSendPackets
			if((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) &&
				(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)))
			{
				for(Index = 0; Index < 5; Index ++)
					if(pAd->TxSwQueue[Index].Number > 0)
						RTMPDeQueuePacket(pAd, FALSE, Index, MAX_TX_PROCESS);
			}
		}
	} while(FALSE);

}

/*
    ========================================================================

    Routine Description:
    Check Sanity RSN IE form AP

    Arguments:

    Return Value:


    ========================================================================
*/
BOOLEAN CheckRSNIE(
	IN  PRTMP_ADAPTER   pAd,
	IN  PUCHAR          pData,
	IN  UCHAR           DataLen,
	OUT	UCHAR			*Offset)
{
	PUCHAR              pVIE;
	UCHAR               len;
	PEID_STRUCT         pEid;
	BOOLEAN				result = FALSE;

	pVIE = pData;
	len	 = DataLen;
	*Offset = 0;

	while (len > sizeof(RSNIE2))
	{
		pEid = (PEID_STRUCT) pVIE;
		// WPA RSN IE
		if ((pEid->Eid == IE_WPA) && (NdisEqualMemory(pEid->Octet, WPA_OUI, 4)))
		{
			if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA || pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPAPSK) &&
				(NdisEqualMemory(pVIE, pAd->MacTab.Content[BSSID_WCID].RSN_IE, pAd->MacTab.Content[BSSID_WCID].RSNIE_Len)) &&
				(pAd->MacTab.Content[BSSID_WCID].RSNIE_Len == (pEid->Len + 2)))
			{
					DBGPRINT(RT_DEBUG_TRACE, ("CheckRSNIE ==> WPA/WPAPSK RSN IE matched in Msg 3, Length(%d) \n", (pEid->Len + 2)));
					result = TRUE;
			}

			*Offset += (pEid->Len + 2);
		}
		// WPA2 RSN IE
		else if ((pEid->Eid == IE_RSN) && (NdisEqualMemory(pEid->Octet + 2, RSN_OUI, 3)))
		{
			if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2 || pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK) &&
				(NdisEqualMemory(pVIE, pAd->MacTab.Content[BSSID_WCID].RSN_IE, pAd->MacTab.Content[BSSID_WCID].RSNIE_Len)) &&
				(pAd->MacTab.Content[BSSID_WCID].RSNIE_Len == (pEid->Len + 2)))
			{
					DBGPRINT(RT_DEBUG_TRACE, ("CheckRSNIE ==> WPA2/WPA2PSK RSN IE matched in Msg 3, Length(%d) \n", (pEid->Len + 2)));
					result = TRUE;
			}

			*Offset += (pEid->Len + 2);
		}
		else
		{
			break;
		}

		pVIE += (pEid->Len + 2);
		len  -= (pEid->Len + 2);
	}

	DBGPRINT(RT_DEBUG_TRACE, ("CheckRSNIE ==> skip_offset(%d) \n", *Offset));

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
BOOLEAN ParseKeyData(
	IN  PRTMP_ADAPTER   pAd,
	IN  PUCHAR          pKeyData,
	IN  UCHAR           KeyDataLen,
	IN	UCHAR			bPairewise)
{
    PKDE_ENCAP          pKDE = NULL;
    PUCHAR              pMyKeyData = pKeyData;
    UCHAR               KeyDataLength = KeyDataLen;
    UCHAR               GTKLEN;
	UCHAR				skip_offset;

	// Verify The RSN IE contained in Pairewise-Msg 3 and skip it
	if (bPairewise)
    {
		// Check RSN IE whether it is WPA2/WPA2PSK
		if (!CheckRSNIE(pAd, pKeyData, KeyDataLen, &skip_offset))
		{
			DBGPRINT(RT_DEBUG_ERROR, ("ParseKeyData ==> WPA2/WPA2PSK RSN IE mismatched \n"));
			hex_dump("Get KEYDATA :", pKeyData, KeyDataLen);
			return FALSE;
    	}
    	else
		{
			// skip RSN IE
			pMyKeyData += skip_offset;
			KeyDataLength -= skip_offset;

			//DBGPRINT(RT_DEBUG_TRACE, ("ParseKeyData ==> WPA2/WPA2PSK RSN IE matched in Msg 3, Length(%d) \n", skip_offset));
		}
	}

	DBGPRINT(RT_DEBUG_TRACE,("ParseKeyData ==> KeyDataLength %d without RSN_IE \n", KeyDataLength));

	// Parse EKD format
	if (KeyDataLength >= 8)
    {
        pKDE = (PKDE_ENCAP) pMyKeyData;
    }
	else
    {
		DBGPRINT(RT_DEBUG_ERROR, ("ERROR: KeyDataLength is too short \n"));
        return FALSE;
    }


	// Sanity check - shared key index should not be 0
	if (pKDE->GTKEncap.Kid == 0)
    {
        DBGPRINT(RT_DEBUG_ERROR, ("ERROR: GTK Key index zero \n"));
        return FALSE;
    }

	// Sanity check - KED length
	if (KeyDataLength < (pKDE->Len + 2))
    {
        DBGPRINT(RT_DEBUG_ERROR, ("ERROR: The len from KDE is too short \n"));
        return FALSE;
    }

	// Get GTK length - refer to IEEE 802.11i-2004 p.82
	GTKLEN = pKDE->Len -6;

	if (GTKLEN < LEN_AES_KEY)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("ERROR: GTK Key length is too short (%d) \n", GTKLEN));
        return FALSE;
	}
	else
		DBGPRINT(RT_DEBUG_TRACE, ("GTK Key with KDE formet got index=%d, len=%d \n", pKDE->GTKEncap.Kid, GTKLEN));

	// Update GTK
	// set key material, TxMic and RxMic for WPAPSK
	NdisMoveMemory(pAd->StaCfg.GTK, pKDE->GTKEncap.GTK, 32);
	pAd->StaCfg.DefaultKeyId = pKDE->GTKEncap.Kid;

	// Update shared key table
	NdisZeroMemory(&pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId], sizeof(CIPHER_KEY));
	pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].KeyLen = LEN_TKIP_EK;
	NdisMoveMemory(pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].Key, pKDE->GTKEncap.GTK, LEN_TKIP_EK);
	NdisMoveMemory(pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].RxMic, &pKDE->GTKEncap.GTK[16], LEN_TKIP_RXMICK);
	NdisMoveMemory(pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].TxMic, &pKDE->GTKEncap.GTK[24], LEN_TKIP_TXMICK);

	// Update Shared Key CipherAlg
	pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg = CIPHER_NONE;
	if (pAd->StaCfg.GroupCipher == Ndis802_11Encryption2Enabled)
		pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg = CIPHER_TKIP;
	else if (pAd->StaCfg.GroupCipher == Ndis802_11Encryption3Enabled)
		pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg = CIPHER_AES;
	else if (pAd->StaCfg.GroupCipher == Ndis802_11GroupWEP40Enabled)
		pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg = CIPHER_WEP64;
	else if (pAd->StaCfg.GroupCipher == Ndis802_11GroupWEP104Enabled)
		pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg = CIPHER_WEP128;

	return TRUE;

}

/*
	========================================================================

	Routine Description:
		Cisco CCKM PRF function

	Arguments:
		key				Cisco Base Transient Key (BTK)
		key_len			The key length of the BTK
		data			Ruquest Number(RN) + BSSID
		data_len		The length of the data
		output			Store for PTK(Pairwise transient keys)
		len				The length of the output
	Return Value:
		None

	Note:
		802.1i	Annex F.9

	========================================================================
*/
VOID CCKMPRF(
	IN	UCHAR	*key,
	IN	INT		key_len,
	IN	UCHAR	*data,
	IN	INT		data_len,
	OUT	UCHAR	*output,
	IN	INT		len)
{
	INT		i;
	UCHAR	input[1024];
	INT		currentindex = 0;
	INT		total_len;

	NdisMoveMemory(input, data, data_len);
	total_len = data_len;
	input[total_len] = 0;
	total_len++;
	for	(i = 0;	i <	(len + 19) / 20; i++)
	{
		HMAC_SHA1(input, total_len,	key, key_len, &output[currentindex]);
		currentindex +=	20;
		input[total_len - 1]++;
	}
}

/*
	========================================================================

	Routine Description:
		Process MIC error indication and record MIC error timer.

	Arguments:
		pAd 	Pointer to our adapter
		pWpaKey 		Pointer to the WPA key structure

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
VOID	RTMPReportMicError(
	IN	PRTMP_ADAPTER	pAd,
	IN	PCIPHER_KEY 	pWpaKey)
{
	ULONG	Now;
    UCHAR   unicastKey = (pWpaKey->Type == PAIRWISE_KEY ? 1:0);

	// Record Last MIC error time and count
	Now = jiffies;
	if (pAd->StaCfg.MicErrCnt == 0)
	{
		pAd->StaCfg.MicErrCnt++;
		pAd->StaCfg.LastMicErrorTime = Now;
        NdisZeroMemory(pAd->StaCfg.ReplayCounter, 8);
	}
	else if (pAd->StaCfg.MicErrCnt == 1)
	{
		if ((pAd->StaCfg.LastMicErrorTime + (60 * OS_HZ)) < Now)
		{
			// Update Last MIC error time, this did not violate two MIC errors within 60 seconds
			pAd->StaCfg.LastMicErrorTime = Now;
		}
		else
		{

			if (pAd->CommonCfg.bWirelessEvent)
				RTMPSendWirelessEvent(pAd, IW_COUNTER_MEASURES_EVENT_FLAG, pAd->MacTab.Content[BSSID_WCID].Addr, BSS0, 0);

			pAd->StaCfg.LastMicErrorTime = Now;
			// Violate MIC error counts, MIC countermeasures kicks in
			pAd->StaCfg.MicErrCnt++;
		}
	}
	else
	{
		// MIC error count >= 2
		// This should not happen
		;
	}
    MlmeEnqueue(pAd,
				MLME_CNTL_STATE_MACHINE,
				OID_802_11_MIC_FAILURE_REPORT_FRAME,
				1,
				&unicastKey);

    if (pAd->StaCfg.MicErrCnt == 2)
    {
        RTMPSetTimer(&pAd->StaCfg.WpaDisassocAndBlockAssocTimer, 100);
    }
}

#define	LENGTH_EAP_H    4
// If the received frame is EAP-Packet ,find out its EAP-Code (Request(0x01), Response(0x02), Success(0x03), Failure(0x04)).
INT	    WpaCheckEapCode(
	IN  PRTMP_ADAPTER   		pAd,
	IN  PUCHAR				pFrame,
	IN  USHORT				FrameLen,
	IN  USHORT				OffSet)
{

	PUCHAR	pData;
	INT	result = 0;

	if( FrameLen < OffSet + LENGTH_EAPOL_H + LENGTH_EAP_H )
		return result;

	pData = pFrame + OffSet; // skip offset bytes

	if(*(pData+1) == EAPPacket) 	// 802.1x header - Packet Type
	{
		 result = *(pData+4);		// EAP header - Code
	}

	return result;
}

VOID    WpaSendMicFailureToWpaSupplicant(
    IN  PRTMP_ADAPTER    pAd,
    IN  BOOLEAN          bUnicast)
{
    union iwreq_data    wrqu;
    char custom[IW_CUSTOM_MAX] = {0};

    sprintf(custom, "MLME-MICHAELMICFAILURE.indication");
    if (bUnicast)
        sprintf(custom, "%s unicast", custom);
    wrqu.data.length = strlen(custom);
    wireless_send_event(pAd->net_dev, IWEVCUSTOM, &wrqu, custom);

    return;
}

VOID	WpaMicFailureReportFrame(
	IN  PRTMP_ADAPTER   pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	PUCHAR              pOutBuffer = NULL;
	UCHAR               Header802_3[14];
	ULONG               FrameLen = 0;
	EAPOL_PACKET        Packet;
	UCHAR               Mic[16];
    BOOLEAN             bUnicast;

	DBGPRINT(RT_DEBUG_TRACE, ("WpaMicFailureReportFrame ----->\n"));

    bUnicast = (Elem->Msg[0] == 1 ? TRUE:FALSE);
	pAd->Sequence = ((pAd->Sequence) + 1) & (MAX_SEQ_NUMBER);

	// init 802.3 header and Fill Packet
	MAKE_802_3_HEADER(Header802_3, pAd->CommonCfg.Bssid, pAd->CurrentAddress, EAPOL);

	NdisZeroMemory(&Packet, sizeof(Packet));
	Packet.ProVer	= EAPOL_VER;
	Packet.ProType	= EAPOLKey;

	Packet.KeyDesc.Type = WPA1_KEY_DESC;

    // Request field presented
    Packet.KeyDesc.KeyInfo.Request = 1;

	if(pAd->StaCfg.WepStatus  == Ndis802_11Encryption3Enabled)
	{
		Packet.KeyDesc.KeyInfo.KeyDescVer = 2;
	}
	else	  // TKIP
	{
		Packet.KeyDesc.KeyInfo.KeyDescVer = 1;
	}

    Packet.KeyDesc.KeyInfo.KeyType = (bUnicast ? PAIRWISEKEY : GROUPKEY);

	// KeyMic field presented
	Packet.KeyDesc.KeyInfo.KeyMic  = 1;

    // Error field presented
	Packet.KeyDesc.KeyInfo.Error  = 1;

	// Update packet length after decide Key data payload
	Packet.Body_Len[1]  = sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE;

	// Key Replay Count
	NdisMoveMemory(Packet.KeyDesc.ReplayCounter, pAd->StaCfg.ReplayCounter, LEN_KEY_DESC_REPLAY);
    inc_byte_array(pAd->StaCfg.ReplayCounter, 8);

	// Convert to little-endian format.
	*((USHORT *)&Packet.KeyDesc.KeyInfo) = cpu2le16(*((USHORT *)&Packet.KeyDesc.KeyInfo));


	MlmeAllocateMemory(pAd, (PUCHAR *)&pOutBuffer);  // allocate memory
	if(pOutBuffer == NULL)
	{
		return;
	}

	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation
	MakeOutgoingFrame(pOutBuffer,               &FrameLen,
		              Packet.Body_Len[1] + 4,   &Packet,
		              END_OF_ARGS);

	// Prepare and Fill MIC value
	NdisZeroMemory(Mic, sizeof(Mic));
	if(pAd->StaCfg.WepStatus  == Ndis802_11Encryption3Enabled)
	{	// AES
        UCHAR digest[20] = {0};
		HMAC_SHA1(pOutBuffer, FrameLen, pAd->StaCfg.PTK, LEN_EAP_MICK, digest);
		NdisMoveMemory(Mic, digest, LEN_KEY_DESC_MIC);
	}
	else
	{	// TKIP
		hmac_md5(pAd->StaCfg.PTK,  LEN_EAP_MICK, pOutBuffer, FrameLen, Mic);
	}
	NdisMoveMemory(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);

    MakeOutgoingFrame(pOutBuffer,           	&FrameLen,
    	  			LENGTH_802_3,     			&Header802_3,
    				Packet.Body_Len[1] + 4,     &Packet,
    				END_OF_ARGS);

	// opy frame to Tx ring and send MIC failure report frame to authenticator
	RTMPToWirelessSta(pAd, Header802_3, LENGTH_802_3, (PUCHAR)&Packet, Packet.Body_Len[1] + 4, FALSE);

	MlmeFreeMemory(pAd, (PUCHAR)pOutBuffer);

	DBGPRINT(RT_DEBUG_TRACE, ("WpaMicFailureReportFrame <-----\n"));
}

/** from wpa_supplicant
 * inc_byte_array - Increment arbitrary length byte array by one
 * @counter: Pointer to byte array
 * @len: Length of the counter in bytes
 *
 * This function increments the last byte of the counter by one and continues
 * rolling over to more significant bytes if the byte was incremented from
 * 0xff to 0x00.
 */
void inc_byte_array(UCHAR *counter, int len)
{
	int pos = len - 1;
	while (pos >= 0) {
		counter[pos]++;
		if (counter[pos] != 0)
			break;
		pos--;
	}
}

VOID WpaDisassocApAndBlockAssoc(
    IN PVOID SystemSpecific1,
    IN PVOID FunctionContext,
    IN PVOID SystemSpecific2,
    IN PVOID SystemSpecific3)
{
    RTMP_ADAPTER                *pAd = (PRTMP_ADAPTER)FunctionContext;
    MLME_DISASSOC_REQ_STRUCT    DisassocReq;

	// disassoc from current AP first
	DBGPRINT(RT_DEBUG_TRACE, ("RTMPReportMicError - disassociate with current AP after sending second continuous EAPOL frame\n"));
	DisassocParmFill(pAd, &DisassocReq, pAd->CommonCfg.Bssid, REASON_MIC_FAILURE);
	MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_MLME_DISASSOC_REQ, sizeof(MLME_DISASSOC_REQ_STRUCT), &DisassocReq);

	pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_DISASSOC;
	pAd->StaCfg.bBlockAssoc = TRUE;
}

