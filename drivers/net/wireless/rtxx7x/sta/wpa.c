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

void inc_byte_array(UCHAR *counter, int len);

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

	/* Record Last MIC error time and count */
	NdisGetSystemUpTime(&Now);
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
			/* Update Last MIC error time, this did not violate two MIC errors within 60 seconds */
			pAd->StaCfg.LastMicErrorTime = Now; 		
		}
		else
		{

				RTMPSendWirelessEvent(pAd, IW_COUNTER_MEASURES_EVENT_FLAG, pAd->MacTab.Content[BSSID_WCID].Addr, BSS0, 0); 

			pAd->StaCfg.LastMicErrorTime = Now; 		
			/* Violate MIC error counts, MIC countermeasures kicks in */
			pAd->StaCfg.MicErrCnt++;
			/*
			 We shall block all reception
			 We shall clean all Tx ring and disassoicate from AP after next EAPOL frame
			 
			 No necessary to clean all Tx ring, on RTMPHardTransmit will stop sending non-802.1X EAPOL packets
			 if pAd->StaCfg.MicErrCnt greater than 2.
			*/
		}
	}
	else
	{
		/* MIC error count >= 2 */
		/* This should not happen */
		;
	}
    MlmeEnqueue(pAd, 
				MLME_CNTL_STATE_MACHINE,
				OID_802_11_MIC_FAILURE_REPORT_FRAME,
				1,
				&unicastKey, 0);

    if (pAd->StaCfg.MicErrCnt == 2)
    {
        RTMPSetTimer(&pAd->StaCfg.WpaDisassocAndBlockAssocTimer, 100);
    }
}


#ifdef WPA_SUPPLICANT_SUPPORT
#define	LENGTH_EAP_H    4
/* If the received frame is EAP-Packet ,find out its EAP-Code (Request(0x01), Response(0x02), Success(0x03), Failure(0x04)). */
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
		
	pData = pFrame + OffSet;		/* skip offset bytes */
 	
	if(*(pData+1) == EAPPacket) 	/* 802.1x header - Packet Type */
	{
		 result = *(pData+4);		/* EAP header - Code */
	}

	return result;
}

#endif /* WPA_SUPPLICANT_SUPPORT */

VOID	WpaMicFailureReportFrame(
	IN  PRTMP_ADAPTER   pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	PUCHAR              pOutBuffer = NULL;
	UCHAR               Header802_3[14];
	ULONG               FrameLen = 0;
	UCHAR				*mpool;
	PEAPOL_PACKET       pPacket;
	UCHAR               Mic[16];
    BOOLEAN             bUnicast;
        
	DBGPRINT(RT_DEBUG_TRACE, ("WpaMicFailureReportFrame ----->\n"));

    bUnicast = (Elem->Msg[0] == 1 ? TRUE:FALSE);
	pAd->Sequence = ((pAd->Sequence) + 1) & (MAX_SEQ_NUMBER);

	/* init 802.3 header and Fill Packet */
	MAKE_802_3_HEADER(Header802_3, pAd->CommonCfg.Bssid, pAd->CurrentAddress, EAPOL);	

	/* Allocate memory for output */
	os_alloc_mem(NULL, (PUCHAR *)&mpool, TX_EAPOL_BUFFER);
	if (mpool == NULL)
    {
        DBGPRINT(RT_DEBUG_ERROR, ("!!!%s : no memory!!!\n", __FUNCTION__));
        return;
    }

	pPacket = (PEAPOL_PACKET)mpool;
	NdisZeroMemory(pPacket, TX_EAPOL_BUFFER);
	
	pPacket->ProVer	= EAPOL_VER;
	pPacket->ProType	= EAPOLKey;
	
	pPacket->KeyDesc.Type = WPA1_KEY_DESC;

    /* Request field presented */
    pPacket->KeyDesc.KeyInfo.Request = 1;
    
	if(pAd->StaCfg.WepStatus  == Ndis802_11Encryption3Enabled)
	{
		pPacket->KeyDesc.KeyInfo.KeyDescVer = 2;
	} 
	else	  /* TKIP */
	{
		pPacket->KeyDesc.KeyInfo.KeyDescVer = 1;
	}

    pPacket->KeyDesc.KeyInfo.KeyType = (bUnicast ? PAIRWISEKEY : GROUPKEY);

	/* KeyMic field presented */
	pPacket->KeyDesc.KeyInfo.KeyMic  = 1;

    /* Error field presented */
	pPacket->KeyDesc.KeyInfo.Error  = 1;
    
	/* Update packet length after decide Key data payload */
	SET_UINT16_TO_ARRARY(pPacket->Body_Len, MIN_LEN_OF_EAPOL_KEY_MSG)

	/* Key Replay Count */
	NdisMoveMemory(pPacket->KeyDesc.ReplayCounter, pAd->StaCfg.ReplayCounter, LEN_KEY_DESC_REPLAY);
    inc_byte_array(pAd->StaCfg.ReplayCounter, 8);

	/* Convert to little-endian format. */
	*((USHORT *)&pPacket->KeyDesc.KeyInfo) = cpu2le16(*((USHORT *)&pPacket->KeyDesc.KeyInfo));


	MlmeAllocateMemory(pAd, (PUCHAR *)&pOutBuffer);  /* allocate memory */
	if(pOutBuffer == NULL)
	{
		os_free_mem(NULL, mpool);
		return;
	}
    
	/*
	   Prepare EAPOL frame for MIC calculation
	   Be careful, only EAPOL frame is counted for MIC calculation
	*/
	MakeOutgoingFrame(pOutBuffer,               &FrameLen,
		              CONV_ARRARY_TO_UINT16(pPacket->Body_Len) + 4,   pPacket,
		              END_OF_ARGS);

	/* Prepare and Fill MIC value */
	NdisZeroMemory(Mic, sizeof(Mic));
	if(pAd->StaCfg.WepStatus  == Ndis802_11Encryption3Enabled)
	{	/* AES */
        UCHAR digest[20] = {0};
		RT_HMAC_SHA1(pAd->StaCfg.PTK, LEN_PTK_KCK, pOutBuffer, FrameLen, digest, SHA1_DIGEST_SIZE);
		NdisMoveMemory(Mic, digest, LEN_KEY_DESC_MIC);
	} 
	else
	{	/* TKIP */
		RT_HMAC_MD5(pAd->StaCfg.PTK, LEN_PTK_KCK, pOutBuffer, FrameLen, Mic, MD5_DIGEST_SIZE);
	}
	NdisMoveMemory(pPacket->KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);

	/* copy frame to Tx ring and send MIC failure report frame to authenticator */
	RTMPToWirelessSta(pAd, &pAd->MacTab.Content[BSSID_WCID],
					  Header802_3, LENGTH_802_3, 
					  (PUCHAR)pPacket, 
					  CONV_ARRARY_TO_UINT16(pPacket->Body_Len) + 4, FALSE);

	MlmeFreeMemory(pAd, (PUCHAR)pOutBuffer);

	os_free_mem(NULL, mpool);

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

	/* disassoc from current AP first */
	DBGPRINT(RT_DEBUG_TRACE, ("RTMPReportMicError - disassociate with current AP after sending second continuous EAPOL frame\n"));
	DisassocParmFill(pAd, &DisassocReq, pAd->CommonCfg.Bssid, REASON_MIC_FAILURE);
	MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_MLME_DISASSOC_REQ, sizeof(MLME_DISASSOC_REQ_STRUCT), &DisassocReq, 0);

	pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_DISASSOC;
	pAd->StaCfg.bBlockAssoc = TRUE;
}

VOID WpaStaPairwiseKeySetting(
	IN	PRTMP_ADAPTER	pAd)
{
	PCIPHER_KEY pSharedKey;
	PMAC_TABLE_ENTRY pEntry;

	pEntry = &pAd->MacTab.Content[BSSID_WCID];
	
	/* Pairwise key shall use key#0  */
	pSharedKey = &pAd->SharedKey[BSS0][0];

	NdisMoveMemory(pAd->StaCfg.PTK, pEntry->PTK, LEN_PTK);
	
	/* Prepare pair-wise key information into shared key table */
	NdisZeroMemory(pSharedKey, sizeof(CIPHER_KEY));  
	pSharedKey->KeyLen = LEN_TK;
    NdisMoveMemory(pSharedKey->Key, &pAd->StaCfg.PTK[32], LEN_TK);
	NdisMoveMemory(pSharedKey->RxMic, &pAd->StaCfg.PTK[48], LEN_TKIP_MIC);
	NdisMoveMemory(pSharedKey->TxMic, &pAd->StaCfg.PTK[48+LEN_TKIP_MIC], LEN_TKIP_MIC);            

	/* Decide its ChiperAlg */
	if (pAd->StaCfg.PairCipher == Ndis802_11Encryption2Enabled)
		pSharedKey->CipherAlg = CIPHER_TKIP;
	else if (pAd->StaCfg.PairCipher == Ndis802_11Encryption3Enabled)
		pSharedKey->CipherAlg = CIPHER_AES;
	else
		pSharedKey->CipherAlg = CIPHER_NONE;

	/* Update these related information to MAC_TABLE_ENTRY */
	NdisMoveMemory(pEntry->PairwiseKey.Key, &pAd->StaCfg.PTK[32], LEN_TK);
	NdisMoveMemory(pEntry->PairwiseKey.RxMic, &pAd->StaCfg.PTK[48], LEN_TKIP_MIC);
	NdisMoveMemory(pEntry->PairwiseKey.TxMic, &pAd->StaCfg.PTK[48+LEN_TKIP_MIC], LEN_TKIP_MIC);            
	pEntry->PairwiseKey.CipherAlg = pSharedKey->CipherAlg;
	
	/* Update pairwise key information to ASIC Shared Key Table	 */
	RTMP_ASIC_SHARED_KEY_TABLE(pAd, 
						  BSS0, 
						  0, 
						  pSharedKey);

	/* Update ASIC WCID attribute table and IVEIV table */
	RTMP_SET_WCID_SEC_INFO(pAd, 
						BSS0, 
						0, 
						pSharedKey->CipherAlg, 
						BSSID_WCID,
						SHAREDKEYTABLE);

	RTMP_SET_PORT_SECURED(pAd);
	
	DBGPRINT(RT_DEBUG_TRACE, ("%s : AID(%d) port secured\n", __FUNCTION__, pEntry->Aid));

}

VOID WpaStaGroupKeySetting(
	IN	PRTMP_ADAPTER	pAd)
{
	PCIPHER_KEY 		pSharedKey;

	pSharedKey = &pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId];

	/* Prepare pair-wise key information into shared key table */
	NdisZeroMemory(pSharedKey, sizeof(CIPHER_KEY));  
	pSharedKey->KeyLen = LEN_TK;
	NdisMoveMemory(pSharedKey->Key, pAd->StaCfg.GTK, LEN_TK);
	NdisMoveMemory(pSharedKey->RxMic, &pAd->StaCfg.GTK[16], LEN_TKIP_MIC);
	NdisMoveMemory(pSharedKey->TxMic, &pAd->StaCfg.GTK[24], LEN_TKIP_MIC);

	/* Update Shared Key CipherAlg */
	pSharedKey->CipherAlg = CIPHER_NONE;
	if (pAd->StaCfg.GroupCipher == Ndis802_11Encryption2Enabled)
		pSharedKey->CipherAlg = CIPHER_TKIP;
	else if (pAd->StaCfg.GroupCipher == Ndis802_11Encryption3Enabled)
		pSharedKey->CipherAlg = CIPHER_AES;
	else if (pAd->StaCfg.GroupCipher == Ndis802_11GroupWEP40Enabled)
		pSharedKey->CipherAlg = CIPHER_WEP64;
	else if (pAd->StaCfg.GroupCipher == Ndis802_11GroupWEP104Enabled)
		pSharedKey->CipherAlg = CIPHER_WEP128;

	/* Update group key information to ASIC Shared Key Table */
	RTMP_ASIC_SHARED_KEY_TABLE(pAd, 
						  	BSS0, 
						  	pAd->StaCfg.DefaultKeyId, 
							pSharedKey);

	/* STA doesn't need to set WCID attribute for group key */
}


/*
	========================================================================
	
	Routine Description:
		Send EAPoL-Start packet to AP.

	Arguments:
		pAd         - NIC Adapter pointer
		
	Return Value:
		None
		
	IRQL = DISPATCH_LEVEL
	
	Note:
		Actions after link up
		1. Change the correct parameters
		2. Send EAPOL - START
		
	========================================================================
*/
VOID    WpaSendEapolStart(
	IN	PRTMP_ADAPTER	pAd,
	IN  PUCHAR          pBssid)
{
	IEEE8021X_FRAME		Packet;
	UCHAR               Header802_3[14];
	
	DBGPRINT(RT_DEBUG_TRACE, ("-----> WpaSendEapolStart\n"));

	NdisZeroMemory(Header802_3,sizeof(UCHAR)*14);

	MAKE_802_3_HEADER(Header802_3, pBssid, &pAd->CurrentAddress[0], EAPOL);
	
	/* Zero message 2 body */
	NdisZeroMemory(&Packet, sizeof(Packet));
	Packet.Version = EAPOL_VER;
	Packet.Type    = EAPOLStart;
	Packet.Length  = cpu2be16(0);
	
	/* Copy frame to Tx ring */
	RTMPToWirelessSta((PRTMP_ADAPTER)pAd, &pAd->MacTab.Content[BSSID_WCID],
					 Header802_3, LENGTH_802_3, (PUCHAR)&Packet, 4, TRUE);

	DBGPRINT(RT_DEBUG_TRACE, ("<----- WpaSendEapolStart\n"));
}


