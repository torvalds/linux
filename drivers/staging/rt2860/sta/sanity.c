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
	sanity.c

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	John Chang  2004-09-01      add WMM support
*/
#include "../rt_config.h"

extern u8 CISCO_OUI[];

extern u8 WPA_OUI[];
extern u8 RSN_OUI[];
extern u8 WME_INFO_ELEM[];
extern u8 WME_PARM_ELEM[];
extern u8 Ccx2QosInfo[];
extern u8 RALINK_OUI[];
extern u8 BROADCOM_OUI[];

/*
    ==========================================================================
    Description:
        MLME message sanity check
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
 */
BOOLEAN MlmeStartReqSanity(struct rt_rtmp_adapter *pAd,
			   void * Msg,
			   unsigned long MsgLen,
			   char Ssid[], u8 * pSsidLen)
{
	struct rt_mlme_start_req *Info;

	Info = (struct rt_mlme_start_req *)(Msg);

	if (Info->SsidLen > MAX_LEN_OF_SSID) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("MlmeStartReqSanity fail - wrong SSID length\n"));
		return FALSE;
	}

	*pSsidLen = Info->SsidLen;
	NdisMoveMemory(Ssid, Info->Ssid, *pSsidLen);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        MLME message sanity check
    Return:
        TRUE if all parameters are OK, FALSE otherwise

    IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
BOOLEAN PeerAssocRspSanity(struct rt_rtmp_adapter *pAd, void * pMsg, unsigned long MsgLen, u8 *pAddr2, u16 * pCapabilityInfo, u16 * pStatus, u16 * pAid, u8 SupRate[], u8 * pSupRateLen, u8 ExtRate[], u8 * pExtRateLen, struct rt_ht_capability_ie * pHtCapability, struct rt_add_ht_info_ie * pAddHtInfo,	/* AP might use this additional ht info IE */
			   u8 * pHtCapabilityLen,
			   u8 * pAddHtInfoLen,
			   u8 * pNewExtChannelOffset,
			   struct rt_edca_parm *pEdcaParm, u8 * pCkipFlag)
{
	char IeType, *Ptr;
	struct rt_frame_802_11 * pFrame = (struct rt_frame_802_11 *) pMsg;
	struct rt_eid * pEid;
	unsigned long Length = 0;

	*pNewExtChannelOffset = 0xff;
	*pHtCapabilityLen = 0;
	*pAddHtInfoLen = 0;
	COPY_MAC_ADDR(pAddr2, pFrame->Hdr.Addr2);
	Ptr = (char *)pFrame->Octet;
	Length += LENGTH_802_11;

	NdisMoveMemory(pCapabilityInfo, &pFrame->Octet[0], 2);
	Length += 2;
	NdisMoveMemory(pStatus, &pFrame->Octet[2], 2);
	Length += 2;
	*pCkipFlag = 0;
	*pExtRateLen = 0;
	pEdcaParm->bValid = FALSE;

	if (*pStatus != MLME_SUCCESS)
		return TRUE;

	NdisMoveMemory(pAid, &pFrame->Octet[4], 2);
	Length += 2;

	/* Aid already swaped byte order in RTMPFrameEndianChange() for big endian platform */
	*pAid = (*pAid) & 0x3fff;	/* AID is low 14-bit */

	/* -- get supported rates from payload and advance the pointer */
	IeType = pFrame->Octet[6];
	*pSupRateLen = pFrame->Octet[7];
	if ((IeType != IE_SUPP_RATES)
	    || (*pSupRateLen > MAX_LEN_OF_SUPPORTED_RATES)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("PeerAssocRspSanity fail - wrong SupportedRates IE\n"));
		return FALSE;
	} else
		NdisMoveMemory(SupRate, &pFrame->Octet[8], *pSupRateLen);

	Length = Length + 2 + *pSupRateLen;

	/* many AP implement proprietary IEs in non-standard order, we'd better */
	/* tolerate mis-ordered IEs to get best compatibility */
	pEid = (struct rt_eid *) & pFrame->Octet[8 + (*pSupRateLen)];

	/* get variable fields from payload and advance the pointer */
	while ((Length + 2 + pEid->Len) <= MsgLen) {
		switch (pEid->Eid) {
		case IE_EXT_SUPP_RATES:
			if (pEid->Len <= MAX_LEN_OF_SUPPORTED_RATES) {
				NdisMoveMemory(ExtRate, pEid->Octet, pEid->Len);
				*pExtRateLen = pEid->Len;
			}
			break;

		case IE_HT_CAP:
		case IE_HT_CAP2:
			if (pEid->Len >= SIZE_HT_CAP_IE)	/*Note: allow extension! */
			{
				NdisMoveMemory(pHtCapability, pEid->Octet,
					       SIZE_HT_CAP_IE);

				*(u16 *) (&pHtCapability->HtCapInfo) =
				    cpu2le16(*(u16 *)
					     (&pHtCapability->HtCapInfo));
				*(u16 *) (&pHtCapability->ExtHtCapInfo) =
				    cpu2le16(*(u16 *)
					     (&pHtCapability->ExtHtCapInfo));

				*pHtCapabilityLen = SIZE_HT_CAP_IE;
			} else {
				DBGPRINT(RT_DEBUG_WARN,
					 ("PeerAssocRspSanity - wrong IE_HT_CAP. \n"));
			}

			break;
		case IE_ADD_HT:
		case IE_ADD_HT2:
			if (pEid->Len >= sizeof(struct rt_add_ht_info_ie)) {
				/* This IE allows extension, but we can ignore extra bytes beyond our knowledge , so only */
				/* copy first sizeof(struct rt_add_ht_info_ie) */
				NdisMoveMemory(pAddHtInfo, pEid->Octet,
					       sizeof(struct rt_add_ht_info_ie));

				*(u16 *) (&pAddHtInfo->AddHtInfo2) =
				    cpu2le16(*(u16 *)
					     (&pAddHtInfo->AddHtInfo2));
				*(u16 *) (&pAddHtInfo->AddHtInfo3) =
				    cpu2le16(*(u16 *)
					     (&pAddHtInfo->AddHtInfo3));

				*pAddHtInfoLen = SIZE_ADD_HT_INFO_IE;
			} else {
				DBGPRINT(RT_DEBUG_WARN,
					 ("PeerAssocRspSanity - wrong IE_ADD_HT. \n"));
			}

			break;
		case IE_SECONDARY_CH_OFFSET:
			if (pEid->Len == 1) {
				*pNewExtChannelOffset = pEid->Octet[0];
			} else {
				DBGPRINT(RT_DEBUG_WARN,
					 ("PeerAssocRspSanity - wrong IE_SECONDARY_CH_OFFSET. \n"));
			}
			break;

		case IE_VENDOR_SPECIFIC:
			/* handle WME PARAMTER ELEMENT */
			if (NdisEqualMemory(pEid->Octet, WME_PARM_ELEM, 6)
			    && (pEid->Len == 24)) {
				u8 *ptr;
				int i;

				/* parsing EDCA parameters */
				pEdcaParm->bValid = TRUE;
				pEdcaParm->bQAck = FALSE;	/* pEid->Octet[0] & 0x10; */
				pEdcaParm->bQueueRequest = FALSE;	/* pEid->Octet[0] & 0x20; */
				pEdcaParm->bTxopRequest = FALSE;	/* pEid->Octet[0] & 0x40; */
				/*pEdcaParm->bMoreDataAck    = FALSE; // pEid->Octet[0] & 0x80; */
				pEdcaParm->EdcaUpdateCount =
				    pEid->Octet[6] & 0x0f;
				pEdcaParm->bAPSDCapable =
				    (pEid->Octet[6] & 0x80) ? 1 : 0;
				ptr = (u8 *)& pEid->Octet[8];
				for (i = 0; i < 4; i++) {
					u8 aci = (*ptr & 0x60) >> 5;	/* b5~6 is AC INDEX */
					pEdcaParm->bACM[aci] = (((*ptr) & 0x10) == 0x10);	/* b5 is ACM */
					pEdcaParm->Aifsn[aci] = (*ptr) & 0x0f;	/* b0~3 is AIFSN */
					pEdcaParm->Cwmin[aci] = *(ptr + 1) & 0x0f;	/* b0~4 is Cwmin */
					pEdcaParm->Cwmax[aci] = *(ptr + 1) >> 4;	/* b5~8 is Cwmax */
					pEdcaParm->Txop[aci] = *(ptr + 2) + 256 * (*(ptr + 3));	/* in unit of 32-us */
					ptr += 4;	/* point to next AC */
				}
			}
			break;
		default:
			DBGPRINT(RT_DEBUG_TRACE,
				 ("PeerAssocRspSanity - ignore unrecognized EID = %d\n",
				  pEid->Eid));
			break;
		}

		Length = Length + 2 + pEid->Len;
		pEid = (struct rt_eid *) ((u8 *) pEid + 2 + pEid->Len);
	}

	return TRUE;
}

/*
    ==========================================================================
    Description:
        MLME message sanity check
    Return:
        TRUE if all parameters are OK, FALSE otherwise

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
BOOLEAN PeerProbeReqSanity(struct rt_rtmp_adapter *pAd,
			   void * Msg,
			   unsigned long MsgLen,
			   u8 *pAddr2,
			   char Ssid[], u8 * pSsidLen)
{
	u8 Idx;
	u8 RateLen;
	char IeType;
	struct rt_frame_802_11 * pFrame = (struct rt_frame_802_11 *) Msg;

	COPY_MAC_ADDR(pAddr2, pFrame->Hdr.Addr2);

	if ((pFrame->Octet[0] != IE_SSID)
	    || (pFrame->Octet[1] > MAX_LEN_OF_SSID)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("PeerProbeReqSanity fail - wrong SSID IE(Type=%d,Len=%d)\n",
			  pFrame->Octet[0], pFrame->Octet[1]));
		return FALSE;
	}

	*pSsidLen = pFrame->Octet[1];
	NdisMoveMemory(Ssid, &pFrame->Octet[2], *pSsidLen);

	Idx = *pSsidLen + 2;

	/* -- get supported rates from payload and advance the pointer */
	IeType = pFrame->Octet[Idx];
	RateLen = pFrame->Octet[Idx + 1];
	if (IeType != IE_SUPP_RATES) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("PeerProbeReqSanity fail - wrong SupportRates IE(Type=%d,Len=%d)\n",
			  pFrame->Octet[Idx], pFrame->Octet[Idx + 1]));
		return FALSE;
	} else {
		if ((pAd->CommonCfg.PhyMode == PHY_11G) && (RateLen < 8))
			return (FALSE);
	}

	return TRUE;
}

/*
    ==========================================================================
    Description:

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
BOOLEAN GetTimBit(char * Ptr,
		  u16 Aid,
		  u8 * TimLen,
		  u8 * BcastFlag,
		  u8 * DtimCount,
		  u8 * DtimPeriod, u8 * MessageToMe)
{
	u8 BitCntl, N1, N2, MyByte, MyBit;
	char *IdxPtr;

	IdxPtr = Ptr;

	IdxPtr++;
	*TimLen = *IdxPtr;

	/* get DTIM Count from TIM element */
	IdxPtr++;
	*DtimCount = *IdxPtr;

	/* get DTIM Period from TIM element */
	IdxPtr++;
	*DtimPeriod = *IdxPtr;

	/* get Bitmap Control from TIM element */
	IdxPtr++;
	BitCntl = *IdxPtr;

	if ((*DtimCount == 0) && (BitCntl & 0x01))
		*BcastFlag = TRUE;
	else
		*BcastFlag = FALSE;

	/* Parse Partial Virtual Bitmap from TIM element */
	N1 = BitCntl & 0xfe;	/* N1 is the first bitmap byte# */
	N2 = *TimLen - 4 + N1;	/* N2 is the last bitmap byte# */

	if ((Aid < (N1 << 3)) || (Aid >= ((N2 + 1) << 3)))
		*MessageToMe = FALSE;
	else {
		MyByte = (Aid >> 3) - N1;	/* my byte position in the bitmap byte-stream */
		MyBit = Aid % 16 - ((MyByte & 0x01) ? 8 : 0);

		IdxPtr += (MyByte + 1);

		/*if (*IdxPtr) */
		/*    DBGPRINT(RT_DEBUG_WARN, ("TIM bitmap = 0x%02x\n", *IdxPtr)); */

		if (*IdxPtr & (0x01 << MyBit))
			*MessageToMe = TRUE;
		else
			*MessageToMe = FALSE;
	}

	return TRUE;
}
