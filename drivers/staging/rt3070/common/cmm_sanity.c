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


extern UCHAR	CISCO_OUI[];

extern UCHAR	WPA_OUI[];
extern UCHAR	RSN_OUI[];
extern UCHAR	WME_INFO_ELEM[];
extern UCHAR	WME_PARM_ELEM[];
extern UCHAR	Ccx2QosInfo[];
extern UCHAR	RALINK_OUI[];
extern UCHAR	BROADCOM_OUI[];
extern UCHAR    WPS_OUI[];

/*
    ==========================================================================
    Description:
        MLME message sanity check
    Return:
        TRUE if all parameters are OK, FALSE otherwise

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
BOOLEAN MlmeAddBAReqSanity(
    IN PRTMP_ADAPTER pAd,
    IN VOID *Msg,
    IN ULONG MsgLen,
    OUT PUCHAR pAddr2)
{
    PMLME_ADDBA_REQ_STRUCT   pInfo;

    pInfo = (MLME_ADDBA_REQ_STRUCT *)Msg;

    if ((MsgLen != sizeof(MLME_ADDBA_REQ_STRUCT)))
    {
        DBGPRINT(RT_DEBUG_TRACE, ("MlmeAddBAReqSanity fail - message lenght not correct.\n"));
        return FALSE;
    }

    if ((pInfo->Wcid >= MAX_LEN_OF_MAC_TABLE))
    {
        DBGPRINT(RT_DEBUG_TRACE, ("MlmeAddBAReqSanity fail - The peer Mac is not associated yet.\n"));
        return FALSE;
    }

	/*
    if ((pInfo->BaBufSize > MAX_RX_REORDERBUF) || (pInfo->BaBufSize < 2))
    {
        DBGPRINT(RT_DEBUG_TRACE, ("MlmeAddBAReqSanity fail - Rx Reordering buffer too big or too small\n"));
        return FALSE;
    }
	*/

    if ((pInfo->pAddr[0]&0x01) == 0x01)
    {
        DBGPRINT(RT_DEBUG_TRACE, ("MlmeAddBAReqSanity fail - broadcast address not support BA\n"));
        return FALSE;
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
BOOLEAN MlmeDelBAReqSanity(
    IN PRTMP_ADAPTER pAd,
    IN VOID *Msg,
    IN ULONG MsgLen)
{
	MLME_DELBA_REQ_STRUCT *pInfo;
	pInfo = (MLME_DELBA_REQ_STRUCT *)Msg;

    if ((MsgLen != sizeof(MLME_DELBA_REQ_STRUCT)))
    {
        DBGPRINT(RT_DEBUG_ERROR, ("MlmeDelBAReqSanity fail - message lenght not correct.\n"));
        return FALSE;
    }

    if ((pInfo->Wcid >= MAX_LEN_OF_MAC_TABLE))
    {
        DBGPRINT(RT_DEBUG_ERROR, ("MlmeDelBAReqSanity fail - The peer Mac is not associated yet.\n"));
        return FALSE;
    }

    if ((pInfo->TID & 0xf0))
    {
        DBGPRINT(RT_DEBUG_ERROR, ("MlmeDelBAReqSanity fail - The peer TID is incorrect.\n"));
        return FALSE;
    }

	if (NdisEqualMemory(pAd->MacTab.Content[pInfo->Wcid].Addr, pInfo->Addr, MAC_ADDR_LEN) == 0)
    {
        DBGPRINT(RT_DEBUG_ERROR, ("MlmeDelBAReqSanity fail - the peer addr dosen't exist.\n"));
        return FALSE;
    }

    return TRUE;
}

BOOLEAN PeerAddBAReqActionSanity(
    IN PRTMP_ADAPTER pAd,
    IN VOID *pMsg,
    IN ULONG MsgLen,
	OUT PUCHAR pAddr2)
{
	PFRAME_802_11 pFrame = (PFRAME_802_11)pMsg;
	PFRAME_ADDBA_REQ pAddFrame;
	pAddFrame = (PFRAME_ADDBA_REQ)(pMsg);
	if (MsgLen < (sizeof(FRAME_ADDBA_REQ)))
	{
		DBGPRINT(RT_DEBUG_ERROR,("PeerAddBAReqActionSanity: ADDBA Request frame length size = %ld incorrect\n", MsgLen));
		return FALSE;
	}
	// we support immediate BA.
	*(USHORT *)(&pAddFrame->BaParm) = cpu2le16(*(USHORT *)(&pAddFrame->BaParm));
	pAddFrame->TimeOutValue = cpu2le16(pAddFrame->TimeOutValue);
	pAddFrame->BaStartSeq.word = cpu2le16(pAddFrame->BaStartSeq.word);

	if (pAddFrame->BaParm.BAPolicy != IMMED_BA)
	{
		DBGPRINT(RT_DEBUG_ERROR,("PeerAddBAReqActionSanity: ADDBA Request Ba Policy[%d] not support\n", pAddFrame->BaParm.BAPolicy));
		DBGPRINT(RT_DEBUG_ERROR,("ADDBA Request. tid=%x, Bufsize=%x, AMSDUSupported=%x \n", pAddFrame->BaParm.TID, pAddFrame->BaParm.BufSize, pAddFrame->BaParm.AMSDUSupported));
		return FALSE;
	}

	// we support immediate BA.
	if (pAddFrame->BaParm.TID &0xfff0)
	{
		DBGPRINT(RT_DEBUG_ERROR,("PeerAddBAReqActionSanity: ADDBA Request incorrect TID = %d\n", pAddFrame->BaParm.TID));
		return FALSE;
	}
	COPY_MAC_ADDR(pAddr2, pFrame->Hdr.Addr2);
	return TRUE;
}

BOOLEAN PeerAddBARspActionSanity(
    IN PRTMP_ADAPTER pAd,
    IN VOID *pMsg,
    IN ULONG MsgLen)
{
	//PFRAME_802_11 pFrame = (PFRAME_802_11)pMsg;
	PFRAME_ADDBA_RSP pAddFrame;

	pAddFrame = (PFRAME_ADDBA_RSP)(pMsg);
	if (MsgLen < (sizeof(FRAME_ADDBA_RSP)))
	{
		DBGPRINT(RT_DEBUG_ERROR,("PeerAddBARspActionSanity: ADDBA Response frame length size = %ld incorrect\n", MsgLen));
		return FALSE;
	}
	// we support immediate BA.
	*(USHORT *)(&pAddFrame->BaParm) = cpu2le16(*(USHORT *)(&pAddFrame->BaParm));
	pAddFrame->StatusCode = cpu2le16(pAddFrame->StatusCode);
	pAddFrame->TimeOutValue = cpu2le16(pAddFrame->TimeOutValue);

	if (pAddFrame->BaParm.BAPolicy != IMMED_BA)
	{
		DBGPRINT(RT_DEBUG_ERROR,("PeerAddBAReqActionSanity: ADDBA Response Ba Policy[%d] not support\n", pAddFrame->BaParm.BAPolicy));
		return FALSE;
	}

	// we support immediate BA.
	if (pAddFrame->BaParm.TID &0xfff0)
	{
		DBGPRINT(RT_DEBUG_ERROR,("PeerAddBARspActionSanity: ADDBA Response incorrect TID = %d\n", pAddFrame->BaParm.TID));
		return FALSE;
	}
	return TRUE;

}

BOOLEAN PeerDelBAActionSanity(
    IN PRTMP_ADAPTER pAd,
    IN UCHAR Wcid,
    IN VOID *pMsg,
    IN ULONG MsgLen )
{
	//PFRAME_802_11 pFrame = (PFRAME_802_11)pMsg;
	PFRAME_DELBA_REQ  pDelFrame;
	if (MsgLen != (sizeof(FRAME_DELBA_REQ)))
		return FALSE;

	if (Wcid >= MAX_LEN_OF_MAC_TABLE)
		return FALSE;

	pDelFrame = (PFRAME_DELBA_REQ)(pMsg);

	*(USHORT *)(&pDelFrame->DelbaParm) = cpu2le16(*(USHORT *)(&pDelFrame->DelbaParm));
	pDelFrame->ReasonCode = cpu2le16(pDelFrame->ReasonCode);

	if (pDelFrame->DelbaParm.TID &0xfff0)
		return FALSE;

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
BOOLEAN PeerBeaconAndProbeRspSanity(
    IN PRTMP_ADAPTER pAd,
    IN VOID *Msg,
    IN ULONG MsgLen,
    IN UCHAR  MsgChannel,
    OUT PUCHAR pAddr2,
    OUT PUCHAR pBssid,
    OUT CHAR Ssid[],
    OUT UCHAR *pSsidLen,
    OUT UCHAR *pBssType,
    OUT USHORT *pBeaconPeriod,
    OUT UCHAR *pChannel,
    OUT UCHAR *pNewChannel,
    OUT LARGE_INTEGER *pTimestamp,
    OUT CF_PARM *pCfParm,
    OUT USHORT *pAtimWin,
    OUT USHORT *pCapabilityInfo,
    OUT UCHAR *pErp,
    OUT UCHAR *pDtimCount,
    OUT UCHAR *pDtimPeriod,
    OUT UCHAR *pBcastFlag,
    OUT UCHAR *pMessageToMe,
    OUT UCHAR SupRate[],
    OUT UCHAR *pSupRateLen,
    OUT UCHAR ExtRate[],
    OUT UCHAR *pExtRateLen,
    OUT	UCHAR *pCkipFlag,
    OUT	UCHAR *pAironetCellPowerLimit,
    OUT PEDCA_PARM       pEdcaParm,
    OUT PQBSS_LOAD_PARM  pQbssLoad,
    OUT PQOS_CAPABILITY_PARM pQosCapability,
    OUT ULONG *pRalinkIe,
    OUT UCHAR		 *pHtCapabilityLen,
#ifdef CONFIG_STA_SUPPORT
    OUT UCHAR		 *pPreNHtCapabilityLen,
#endif // CONFIG_STA_SUPPORT //
    OUT HT_CAPABILITY_IE *pHtCapability,
	OUT UCHAR		 *AddHtInfoLen,
	OUT ADD_HT_INFO_IE *AddHtInfo,
	OUT UCHAR *NewExtChannelOffset,		// Ht extension channel offset(above or below)
    OUT USHORT *LengthVIE,
    OUT	PNDIS_802_11_VARIABLE_IEs pVIE)
{
    CHAR				*Ptr;
#ifdef CONFIG_STA_SUPPORT
	CHAR 				TimLen;
#endif // CONFIG_STA_SUPPORT //
    PFRAME_802_11		pFrame;
    PEID_STRUCT         pEid;
    UCHAR				SubType;
    UCHAR				Sanity;
    //UCHAR				ECWMin, ECWMax;
    //MAC_CSR9_STRUC		Csr9;
    ULONG				Length = 0;

	// For some 11a AP which didn't have DS_IE, we use two conditions to decide the channel
	//	1. If the AP is 11n enabled, then check the control channel.
	//	2. If the AP didn't have any info about channel, use the channel we received this frame as the channel. (May inaccuracy!!)
	UCHAR			CtrlChannel = 0;

    // Add for 3 necessary EID field check
    Sanity = 0;

    *pAtimWin = 0;
    *pErp = 0;
    *pDtimCount = 0;
    *pDtimPeriod = 0;
    *pBcastFlag = 0;
    *pMessageToMe = 0;
    *pExtRateLen = 0;
    *pCkipFlag = 0;			        // Default of CkipFlag is 0
    *pAironetCellPowerLimit = 0xFF;  // Default of AironetCellPowerLimit is 0xFF
    *LengthVIE = 0;					// Set the length of VIE to init value 0
    *pHtCapabilityLen = 0;					// Set the length of VIE to init value 0
#ifdef CONFIG_STA_SUPPORT
	if (pAd->OpMode == OPMODE_STA)
		*pPreNHtCapabilityLen = 0;					// Set the length of VIE to init value 0
#endif // CONFIG_STA_SUPPORT //
    *AddHtInfoLen = 0;					// Set the length of VIE to init value 0
    *pRalinkIe = 0;
    *pNewChannel = 0;
    *NewExtChannelOffset = 0xff;	//Default 0xff means no such IE
    pCfParm->bValid = FALSE;        // default: no IE_CF found
    pQbssLoad->bValid = FALSE;      // default: no IE_QBSS_LOAD found
    pEdcaParm->bValid = FALSE;      // default: no IE_EDCA_PARAMETER found
    pQosCapability->bValid = FALSE; // default: no IE_QOS_CAPABILITY found

    pFrame = (PFRAME_802_11)Msg;

    // get subtype from header
    SubType = (UCHAR)pFrame->Hdr.FC.SubType;

    // get Addr2 and BSSID from header
    COPY_MAC_ADDR(pAddr2, pFrame->Hdr.Addr2);
    COPY_MAC_ADDR(pBssid, pFrame->Hdr.Addr3);

//	hex_dump("Beacon", Msg, MsgLen);

    Ptr = pFrame->Octet;
    Length += LENGTH_802_11;

    // get timestamp from payload and advance the pointer
    NdisMoveMemory(pTimestamp, Ptr, TIMESTAMP_LEN);

	pTimestamp->u.LowPart = cpu2le32(pTimestamp->u.LowPart);
	pTimestamp->u.HighPart = cpu2le32(pTimestamp->u.HighPart);

    Ptr += TIMESTAMP_LEN;
    Length += TIMESTAMP_LEN;

    // get beacon interval from payload and advance the pointer
    NdisMoveMemory(pBeaconPeriod, Ptr, 2);
    Ptr += 2;
    Length += 2;

    // get capability info from payload and advance the pointer
    NdisMoveMemory(pCapabilityInfo, Ptr, 2);
    Ptr += 2;
    Length += 2;

    if (CAP_IS_ESS_ON(*pCapabilityInfo))
        *pBssType = BSS_INFRA;
    else
        *pBssType = BSS_ADHOC;

    pEid = (PEID_STRUCT) Ptr;

    // get variable fields from payload and advance the pointer
    while ((Length + 2 + pEid->Len) <= MsgLen)
    {
        //
        // Secure copy VIE to VarIE[MAX_VIE_LEN] didn't overflow.
        //
        if ((*LengthVIE + pEid->Len + 2) >= MAX_VIE_LEN)
        {
            DBGPRINT(RT_DEBUG_WARN, ("PeerBeaconAndProbeRspSanity - Variable IEs out of resource [len(=%d) > MAX_VIE_LEN(=%d)]\n",
                    (*LengthVIE + pEid->Len + 2), MAX_VIE_LEN));
            break;
        }

        switch(pEid->Eid)
        {
            case IE_SSID:
                // Already has one SSID EID in this beacon, ignore the second one
                if (Sanity & 0x1)
                    break;
                if(pEid->Len <= MAX_LEN_OF_SSID)
                {
                    NdisMoveMemory(Ssid, pEid->Octet, pEid->Len);
                    *pSsidLen = pEid->Len;
                    Sanity |= 0x1;
                }
                else
                {
                    DBGPRINT(RT_DEBUG_TRACE, ("PeerBeaconAndProbeRspSanity - wrong IE_SSID (len=%d)\n",pEid->Len));
                    return FALSE;
                }
                break;

            case IE_SUPP_RATES:
                if(pEid->Len <= MAX_LEN_OF_SUPPORTED_RATES)
                {
                    Sanity |= 0x2;
                    NdisMoveMemory(SupRate, pEid->Octet, pEid->Len);
                    *pSupRateLen = pEid->Len;

                    // TODO: 2004-09-14 not a good design here, cause it exclude extra rates
                    // from ScanTab. We should report as is. And filter out unsupported
                    // rates in MlmeAux.
                    // Check against the supported rates
                    // RTMPCheckRates(pAd, SupRate, pSupRateLen);
                }
                else
                {
                    DBGPRINT(RT_DEBUG_TRACE, ("PeerBeaconAndProbeRspSanity - wrong IE_SUPP_RATES (len=%d)\n",pEid->Len));
                    return FALSE;
                }
                break;

            case IE_HT_CAP:
			if (pEid->Len >= SIZE_HT_CAP_IE)  //Note: allow extension.!!
			{
				NdisMoveMemory(pHtCapability, pEid->Octet, sizeof(HT_CAPABILITY_IE));
				*pHtCapabilityLen = SIZE_HT_CAP_IE;	// Nnow we only support 26 bytes.

				*(USHORT *)(&pHtCapability->HtCapInfo) = cpu2le16(*(USHORT *)(&pHtCapability->HtCapInfo));
				*(USHORT *)(&pHtCapability->ExtHtCapInfo) = cpu2le16(*(USHORT *)(&pHtCapability->ExtHtCapInfo));

#ifdef CONFIG_STA_SUPPORT
				IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
				{
					*pPreNHtCapabilityLen = 0;	// Nnow we only support 26 bytes.

					Ptr = (PUCHAR) pVIE;
					NdisMoveMemory(Ptr + *LengthVIE, &pEid->Eid, pEid->Len + 2);
					*LengthVIE += (pEid->Len + 2);
				}
#endif // CONFIG_STA_SUPPORT //
			}
			else
			{
				DBGPRINT(RT_DEBUG_WARN, ("PeerBeaconAndProbeRspSanity - wrong IE_HT_CAP. pEid->Len = %d\n", pEid->Len));
			}

		break;
            case IE_ADD_HT:
			if (pEid->Len >= sizeof(ADD_HT_INFO_IE))
			{
				// This IE allows extension, but we can ignore extra bytes beyond our knowledge , so only
				// copy first sizeof(ADD_HT_INFO_IE)
				NdisMoveMemory(AddHtInfo, pEid->Octet, sizeof(ADD_HT_INFO_IE));
				*AddHtInfoLen = SIZE_ADD_HT_INFO_IE;

				CtrlChannel = AddHtInfo->ControlChan;

				*(USHORT *)(&AddHtInfo->AddHtInfo2) = cpu2le16(*(USHORT *)(&AddHtInfo->AddHtInfo2));
				*(USHORT *)(&AddHtInfo->AddHtInfo3) = cpu2le16(*(USHORT *)(&AddHtInfo->AddHtInfo3));

#ifdef CONFIG_STA_SUPPORT
				IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
				{
			                Ptr = (PUCHAR) pVIE;
			                NdisMoveMemory(Ptr + *LengthVIE, &pEid->Eid, pEid->Len + 2);
			                *LengthVIE += (pEid->Len + 2);
				}
#endif // CONFIG_STA_SUPPORT //
			}
			else
			{
				DBGPRINT(RT_DEBUG_WARN, ("PeerBeaconAndProbeRspSanity - wrong IE_ADD_HT. \n"));
			}

		break;
            case IE_SECONDARY_CH_OFFSET:
			if (pEid->Len == 1)
			{
				*NewExtChannelOffset = pEid->Octet[0];
			}
			else
			{
				DBGPRINT(RT_DEBUG_WARN, ("PeerBeaconAndProbeRspSanity - wrong IE_SECONDARY_CH_OFFSET. \n"));
			}

		break;
            case IE_FH_PARM:
                DBGPRINT(RT_DEBUG_TRACE, ("PeerBeaconAndProbeRspSanity(IE_FH_PARM) \n"));
                break;

            case IE_DS_PARM:
                if(pEid->Len == 1)
                {
                    *pChannel = *pEid->Octet;
#ifdef CONFIG_STA_SUPPORT
					IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
					{
						if (ChannelSanity(pAd, *pChannel) == 0)
						{

							return FALSE;
						}
					}
#endif // CONFIG_STA_SUPPORT //
                    Sanity |= 0x4;
                }
                else
                {
                    DBGPRINT(RT_DEBUG_TRACE, ("PeerBeaconAndProbeRspSanity - wrong IE_DS_PARM (len=%d)\n",pEid->Len));
                    return FALSE;
                }
                break;

            case IE_CF_PARM:
                if(pEid->Len == 6)
                {
                    pCfParm->bValid = TRUE;
                    pCfParm->CfpCount = pEid->Octet[0];
                    pCfParm->CfpPeriod = pEid->Octet[1];
                    pCfParm->CfpMaxDuration = pEid->Octet[2] + 256 * pEid->Octet[3];
                    pCfParm->CfpDurRemaining = pEid->Octet[4] + 256 * pEid->Octet[5];
                }
                else
                {
                    DBGPRINT(RT_DEBUG_TRACE, ("PeerBeaconAndProbeRspSanity - wrong IE_CF_PARM\n"));
                    return FALSE;
                }
                break;

            case IE_IBSS_PARM:
                if(pEid->Len == 2)
                {
                    NdisMoveMemory(pAtimWin, pEid->Octet, pEid->Len);
                }
                else
                {
                    DBGPRINT(RT_DEBUG_TRACE, ("PeerBeaconAndProbeRspSanity - wrong IE_IBSS_PARM\n"));
                    return FALSE;
                }
                break;

#ifdef CONFIG_STA_SUPPORT
            case IE_TIM:
                if(INFRA_ON(pAd) && SubType == SUBTYPE_BEACON)
                {
                    GetTimBit((PUCHAR)pEid, pAd->StaActive.Aid, &TimLen, pBcastFlag, pDtimCount, pDtimPeriod, pMessageToMe);
                }
                break;
#endif // CONFIG_STA_SUPPORT //
            case IE_CHANNEL_SWITCH_ANNOUNCEMENT:
                if(pEid->Len == 3)
                {
                	*pNewChannel = pEid->Octet[1];	//extract new channel number
                }
                break;

            // New for WPA
            // CCX v2 has the same IE, we need to parse that too
            // Wifi WMM use the same IE vale, need to parse that too
            // case IE_WPA:
            case IE_VENDOR_SPECIFIC:
                // Check Broadcom/Atheros 802.11n OUI version, for HT Capability IE.
                // This HT IE is before IEEE draft set HT IE value.2006-09-28 by Jan.
                /*if (NdisEqualMemory(pEid->Octet, BROADCOM_OUI, 3) && (pEid->Len >= 4))
                {
                	if ((pEid->Octet[3] == OUI_BROADCOM_HT) && (pEid->Len >= 30))
            		{
				{
					NdisMoveMemory(pHtCapability, &pEid->Octet[4], sizeof(HT_CAPABILITY_IE));
					*pHtCapabilityLen = SIZE_HT_CAP_IE;	// Nnow we only support 26 bytes.
				}
         		}
                	if ((pEid->Octet[3] == OUI_BROADCOM_HT) && (pEid->Len >= 26))
            		{
				{
					NdisMoveMemory(AddHtInfo, &pEid->Octet[4], sizeof(ADD_HT_INFO_IE));
					*AddHtInfoLen = SIZE_ADD_HT_INFO_IE;	// Nnow we only support 26 bytes.
				}
         		}
                }
				*/
                // Check the OUI version, filter out non-standard usage
                if (NdisEqualMemory(pEid->Octet, RALINK_OUI, 3) && (pEid->Len == 7))
                {
                    //*pRalinkIe = pEid->Octet[3];
                    if (pEid->Octet[3] != 0)
        				*pRalinkIe = pEid->Octet[3];
        			else
        				*pRalinkIe = 0xf0000000; // Set to non-zero value (can't set bit0-2) to represent this is Ralink Chip. So at linkup, we will set ralinkchip flag.
                }
#ifdef CONFIG_STA_SUPPORT
#ifdef DOT11_N_SUPPORT
		// This HT IE is before IEEE draft set HT IE value.2006-09-28 by Jan.

                // Other vendors had production before IE_HT_CAP value is assigned. To backward support those old-firmware AP,
                // Check broadcom-defiend pre-802.11nD1.0 OUI for HT related IE, including HT Capatilities IE and HT Information IE
                else if ((*pHtCapabilityLen == 0) && NdisEqualMemory(pEid->Octet, PRE_N_HT_OUI, 3) && (pEid->Len >= 4) && (pAd->OpMode == OPMODE_STA))
                {
                    if ((pEid->Octet[3] == OUI_PREN_HT_CAP) && (pEid->Len >= 30) && (*pHtCapabilityLen == 0))
                    {
                        NdisMoveMemory(pHtCapability, &pEid->Octet[4], sizeof(HT_CAPABILITY_IE));
                        *pPreNHtCapabilityLen = SIZE_HT_CAP_IE;
                    }

                    if ((pEid->Octet[3] == OUI_PREN_ADD_HT) && (pEid->Len >= 26))
                    {
                        NdisMoveMemory(AddHtInfo, &pEid->Octet[4], sizeof(ADD_HT_INFO_IE));
                        *AddHtInfoLen = SIZE_ADD_HT_INFO_IE;
                    }
                }
#endif // DOT11_N_SUPPORT //
#endif // CONFIG_STA_SUPPORT //
                else if (NdisEqualMemory(pEid->Octet, WPA_OUI, 4))
                {
                    // Copy to pVIE which will report to microsoft bssid list.
                    Ptr = (PUCHAR) pVIE;
                    NdisMoveMemory(Ptr + *LengthVIE, &pEid->Eid, pEid->Len + 2);
                    *LengthVIE += (pEid->Len + 2);
                }
                else if (NdisEqualMemory(pEid->Octet, WME_PARM_ELEM, 6) && (pEid->Len == 24))
                {
                    PUCHAR ptr;
                    int i;

                    // parsing EDCA parameters
                    pEdcaParm->bValid          = TRUE;
                    pEdcaParm->bQAck           = FALSE; // pEid->Octet[0] & 0x10;
                    pEdcaParm->bQueueRequest   = FALSE; // pEid->Octet[0] & 0x20;
                    pEdcaParm->bTxopRequest    = FALSE; // pEid->Octet[0] & 0x40;
                    pEdcaParm->EdcaUpdateCount = pEid->Octet[6] & 0x0f;
                    pEdcaParm->bAPSDCapable    = (pEid->Octet[6] & 0x80) ? 1 : 0;
                    ptr = &pEid->Octet[8];
                    for (i=0; i<4; i++)
                    {
                        UCHAR aci = (*ptr & 0x60) >> 5; // b5~6 is AC INDEX
                        pEdcaParm->bACM[aci]  = (((*ptr) & 0x10) == 0x10);   // b5 is ACM
                        pEdcaParm->Aifsn[aci] = (*ptr) & 0x0f;               // b0~3 is AIFSN
                        pEdcaParm->Cwmin[aci] = *(ptr+1) & 0x0f;             // b0~4 is Cwmin
                        pEdcaParm->Cwmax[aci] = *(ptr+1) >> 4;               // b5~8 is Cwmax
                        pEdcaParm->Txop[aci]  = *(ptr+2) + 256 * (*(ptr+3)); // in unit of 32-us
                        ptr += 4; // point to next AC
                    }
                }
                else if (NdisEqualMemory(pEid->Octet, WME_INFO_ELEM, 6) && (pEid->Len == 7))
                {
                    // parsing EDCA parameters
                    pEdcaParm->bValid          = TRUE;
                    pEdcaParm->bQAck           = FALSE; // pEid->Octet[0] & 0x10;
                    pEdcaParm->bQueueRequest   = FALSE; // pEid->Octet[0] & 0x20;
                    pEdcaParm->bTxopRequest    = FALSE; // pEid->Octet[0] & 0x40;
                    pEdcaParm->EdcaUpdateCount = pEid->Octet[6] & 0x0f;
                    pEdcaParm->bAPSDCapable    = (pEid->Octet[6] & 0x80) ? 1 : 0;

                    // use default EDCA parameter
                    pEdcaParm->bACM[QID_AC_BE]  = 0;
                    pEdcaParm->Aifsn[QID_AC_BE] = 3;
                    pEdcaParm->Cwmin[QID_AC_BE] = CW_MIN_IN_BITS;
                    pEdcaParm->Cwmax[QID_AC_BE] = CW_MAX_IN_BITS;
                    pEdcaParm->Txop[QID_AC_BE]  = 0;

                    pEdcaParm->bACM[QID_AC_BK]  = 0;
                    pEdcaParm->Aifsn[QID_AC_BK] = 7;
                    pEdcaParm->Cwmin[QID_AC_BK] = CW_MIN_IN_BITS;
                    pEdcaParm->Cwmax[QID_AC_BK] = CW_MAX_IN_BITS;
                    pEdcaParm->Txop[QID_AC_BK]  = 0;

                    pEdcaParm->bACM[QID_AC_VI]  = 0;
                    pEdcaParm->Aifsn[QID_AC_VI] = 2;
                    pEdcaParm->Cwmin[QID_AC_VI] = CW_MIN_IN_BITS-1;
                    pEdcaParm->Cwmax[QID_AC_VI] = CW_MAX_IN_BITS;
                    pEdcaParm->Txop[QID_AC_VI]  = 96;   // AC_VI: 96*32us ~= 3ms

                    pEdcaParm->bACM[QID_AC_VO]  = 0;
                    pEdcaParm->Aifsn[QID_AC_VO] = 2;
                    pEdcaParm->Cwmin[QID_AC_VO] = CW_MIN_IN_BITS-2;
                    pEdcaParm->Cwmax[QID_AC_VO] = CW_MAX_IN_BITS-1;
                    pEdcaParm->Txop[QID_AC_VO]  = 48;   // AC_VO: 48*32us ~= 1.5ms
                }
#ifdef CONFIG_STA_SUPPORT
#endif // CONFIG_STA_SUPPORT //
                else
                {
                }

                break;

            case IE_EXT_SUPP_RATES:
                if (pEid->Len <= MAX_LEN_OF_SUPPORTED_RATES)
                {
                    NdisMoveMemory(ExtRate, pEid->Octet, pEid->Len);
                    *pExtRateLen = pEid->Len;

                    // TODO: 2004-09-14 not a good design here, cause it exclude extra rates
                    // from ScanTab. We should report as is. And filter out unsupported
                    // rates in MlmeAux.
                    // Check against the supported rates
                    // RTMPCheckRates(pAd, ExtRate, pExtRateLen);
                }
                break;

            case IE_ERP:
                if (pEid->Len == 1)
                {
                    *pErp = (UCHAR)pEid->Octet[0];
                }
                break;

            case IE_AIRONET_CKIP:
                // 0. Check Aironet IE length, it must be larger or equal to 28
                // Cisco AP350 used length as 28
                // Cisco AP12XX used length as 30
                if (pEid->Len < (CKIP_NEGOTIATION_LENGTH - 2))
                    break;

                // 1. Copy CKIP flag byte to buffer for process
                *pCkipFlag = *(pEid->Octet + 8);
                break;

            case IE_AP_TX_POWER:
                // AP Control of Client Transmit Power
                //0. Check Aironet IE length, it must be 6
                if (pEid->Len != 0x06)
                    break;

                // Get cell power limit in dBm
                if (NdisEqualMemory(pEid->Octet, CISCO_OUI, 3) == 1)
                    *pAironetCellPowerLimit = *(pEid->Octet + 4);
                break;

            // WPA2 & 802.11i RSN
            case IE_RSN:
                // There is no OUI for version anymore, check the group cipher OUI before copying
                if (RTMPEqualMemory(pEid->Octet + 2, RSN_OUI, 3))
                {
                    // Copy to pVIE which will report to microsoft bssid list.
                    Ptr = (PUCHAR) pVIE;
                    NdisMoveMemory(Ptr + *LengthVIE, &pEid->Eid, pEid->Len + 2);
                    *LengthVIE += (pEid->Len + 2);
                }
                break;

            default:
                break;
        }

        Length = Length + 2 + pEid->Len;  // Eid[1] + Len[1]+ content[Len]
        pEid = (PEID_STRUCT)((UCHAR*)pEid + 2 + pEid->Len);
    }

    // For some 11a AP. it did not have the channel EID, patch here
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		UCHAR LatchRfChannel = MsgChannel;
		if ((pAd->LatchRfRegs.Channel > 14) && ((Sanity & 0x4) == 0))
		{
			if (CtrlChannel != 0)
				*pChannel = CtrlChannel;
			else
				*pChannel = LatchRfChannel;
			Sanity |= 0x4;
		}
	}
#endif // CONFIG_STA_SUPPORT //

	if (Sanity != 0x7)
	{
		DBGPRINT(RT_DEBUG_WARN, ("PeerBeaconAndProbeRspSanity - missing field, Sanity=0x%02x\n", Sanity));
		return FALSE;
	}
	else
	{
		return TRUE;
	}

}

#ifdef DOT11N_DRAFT3
/*
	==========================================================================
	Description:
		MLME message sanity check for some IE addressed  in 802.11n d3.03.
	Return:
		TRUE if all parameters are OK, FALSE otherwise

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
BOOLEAN PeerBeaconAndProbeRspSanity2(
	IN PRTMP_ADAPTER pAd,
	IN VOID *Msg,
	IN ULONG MsgLen,
	OUT UCHAR 	*RegClass)
{
	CHAR				*Ptr;
	PFRAME_802_11		pFrame;
	PEID_STRUCT			pEid;
	ULONG				Length = 0;

	pFrame = (PFRAME_802_11)Msg;

	*RegClass = 0;
	Ptr = pFrame->Octet;
	Length += LENGTH_802_11;

	// get timestamp from payload and advance the pointer
	Ptr += TIMESTAMP_LEN;
	Length += TIMESTAMP_LEN;

	// get beacon interval from payload and advance the pointer
	Ptr += 2;
	Length += 2;

	// get capability info from payload and advance the pointer
	Ptr += 2;
	Length += 2;

	pEid = (PEID_STRUCT) Ptr;

	// get variable fields from payload and advance the pointer
	while ((Length + 2 + pEid->Len) <= MsgLen)
	{
		switch(pEid->Eid)
		{
			case IE_SUPP_REG_CLASS:
				if(pEid->Len > 0)
				{
					*RegClass = *pEid->Octet;
				}
				else
				{
					DBGPRINT(RT_DEBUG_TRACE, ("PeerBeaconAndProbeRspSanity - wrong IE_SSID (len=%d)\n",pEid->Len));
					return FALSE;
				}
				break;
		}

		Length = Length + 2 + pEid->Len;  // Eid[1] + Len[1]+ content[Len]
		pEid = (PEID_STRUCT)((UCHAR*)pEid + 2 + pEid->Len);
	}

	return TRUE;

}
#endif // DOT11N_DRAFT3 //

/*
    ==========================================================================
    Description:
        MLME message sanity check
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
 */
BOOLEAN MlmeScanReqSanity(
	IN PRTMP_ADAPTER pAd,
	IN VOID *Msg,
	IN ULONG MsgLen,
	OUT UCHAR *pBssType,
	OUT CHAR Ssid[],
	OUT UCHAR *pSsidLen,
	OUT UCHAR *pScanType)
{
	MLME_SCAN_REQ_STRUCT *Info;

	Info = (MLME_SCAN_REQ_STRUCT *)(Msg);
	*pBssType = Info->BssType;
	*pSsidLen = Info->SsidLen;
	NdisMoveMemory(Ssid, Info->Ssid, *pSsidLen);
	*pScanType = Info->ScanType;

	if ((*pBssType == BSS_INFRA || *pBssType == BSS_ADHOC || *pBssType == BSS_ANY)
		&& (*pScanType == SCAN_ACTIVE || *pScanType == SCAN_PASSIVE
#ifdef CONFIG_STA_SUPPORT
		|| *pScanType == SCAN_CISCO_PASSIVE || *pScanType == SCAN_CISCO_ACTIVE
		|| *pScanType == SCAN_CISCO_CHANNEL_LOAD || *pScanType == SCAN_CISCO_NOISE
#endif // CONFIG_STA_SUPPORT //
		))
	{
		return TRUE;
	}
	else
	{
		DBGPRINT(RT_DEBUG_TRACE, ("MlmeScanReqSanity fail - wrong BssType or ScanType\n"));
		return FALSE;
	}
}

// IRQL = DISPATCH_LEVEL
UCHAR ChannelSanity(
    IN PRTMP_ADAPTER pAd,
    IN UCHAR channel)
{
    int i;

    for (i = 0; i < pAd->ChannelListNum; i ++)
    {
        if (channel == pAd->ChannelList[i].Channel)
            return 1;
    }
    return 0;
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
BOOLEAN PeerDeauthSanity(
    IN PRTMP_ADAPTER pAd,
    IN VOID *Msg,
    IN ULONG MsgLen,
    OUT PUCHAR pAddr2,
    OUT USHORT *pReason)
{
    PFRAME_802_11 pFrame = (PFRAME_802_11)Msg;

    COPY_MAC_ADDR(pAddr2, pFrame->Hdr.Addr2);
    NdisMoveMemory(pReason, &pFrame->Octet[0], 2);

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
BOOLEAN PeerAuthSanity(
    IN PRTMP_ADAPTER pAd,
    IN VOID *Msg,
    IN ULONG MsgLen,
    OUT PUCHAR pAddr,
    OUT USHORT *pAlg,
    OUT USHORT *pSeq,
    OUT USHORT *pStatus,
    CHAR *pChlgText)
{
    PFRAME_802_11 pFrame = (PFRAME_802_11)Msg;

    COPY_MAC_ADDR(pAddr,   pFrame->Hdr.Addr2);
    NdisMoveMemory(pAlg,    &pFrame->Octet[0], 2);
    NdisMoveMemory(pSeq,    &pFrame->Octet[2], 2);
    NdisMoveMemory(pStatus, &pFrame->Octet[4], 2);

    if ((*pAlg == Ndis802_11AuthModeOpen)
      )
    {
        if (*pSeq == 1 || *pSeq == 2)
        {
            return TRUE;
        }
        else
        {
            DBGPRINT(RT_DEBUG_TRACE, ("PeerAuthSanity fail - wrong Seg#\n"));
            return FALSE;
        }
    }
    else if (*pAlg == Ndis802_11AuthModeShared)
    {
        if (*pSeq == 1 || *pSeq == 4)
        {
            return TRUE;
        }
        else if (*pSeq == 2 || *pSeq == 3)
        {
            NdisMoveMemory(pChlgText, &pFrame->Octet[8], CIPHER_TEXT_LEN);
            return TRUE;
        }
        else
        {
            DBGPRINT(RT_DEBUG_TRACE, ("PeerAuthSanity fail - wrong Seg#\n"));
            return FALSE;
        }
    }
    else
    {
        DBGPRINT(RT_DEBUG_TRACE, ("PeerAuthSanity fail - wrong algorithm\n"));
        return FALSE;
    }
}

/*
    ==========================================================================
    Description:
        MLME message sanity check
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
 */
BOOLEAN MlmeAuthReqSanity(
    IN PRTMP_ADAPTER pAd,
    IN VOID *Msg,
    IN ULONG MsgLen,
    OUT PUCHAR pAddr,
    OUT ULONG *pTimeout,
    OUT USHORT *pAlg)
{
    MLME_AUTH_REQ_STRUCT *pInfo;

    pInfo  = (MLME_AUTH_REQ_STRUCT *)Msg;
    COPY_MAC_ADDR(pAddr, pInfo->Addr);
    *pTimeout = pInfo->Timeout;
    *pAlg = pInfo->Alg;

    if (((*pAlg == Ndis802_11AuthModeShared) ||(*pAlg == Ndis802_11AuthModeOpen)
     	) &&
        ((*pAddr & 0x01) == 0))
    {
        return TRUE;
    }
    else
    {
        DBGPRINT(RT_DEBUG_TRACE, ("MlmeAuthReqSanity fail - wrong algorithm\n"));
        return FALSE;
    }
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
BOOLEAN MlmeAssocReqSanity(
    IN PRTMP_ADAPTER pAd,
    IN VOID *Msg,
    IN ULONG MsgLen,
    OUT PUCHAR pApAddr,
    OUT USHORT *pCapabilityInfo,
    OUT ULONG *pTimeout,
    OUT USHORT *pListenIntv)
{
    MLME_ASSOC_REQ_STRUCT *pInfo;

    pInfo = (MLME_ASSOC_REQ_STRUCT *)Msg;
    *pTimeout = pInfo->Timeout;                             // timeout
    COPY_MAC_ADDR(pApAddr, pInfo->Addr);                   // AP address
    *pCapabilityInfo = pInfo->CapabilityInfo;               // capability info
    *pListenIntv = pInfo->ListenIntv;

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
BOOLEAN PeerDisassocSanity(
    IN PRTMP_ADAPTER pAd,
    IN VOID *Msg,
    IN ULONG MsgLen,
    OUT PUCHAR pAddr2,
    OUT USHORT *pReason)
{
    PFRAME_802_11 pFrame = (PFRAME_802_11)Msg;

    COPY_MAC_ADDR(pAddr2, pFrame->Hdr.Addr2);
    NdisMoveMemory(pReason, &pFrame->Octet[0], 2);

    return TRUE;
}

/*
	========================================================================
	Routine Description:
		Sanity check NetworkType (11b, 11g or 11a)

	Arguments:
		pBss - Pointer to BSS table.

	Return Value:
        Ndis802_11DS .......(11b)
        Ndis802_11OFDM24....(11g)
        Ndis802_11OFDM5.....(11a)

	IRQL = DISPATCH_LEVEL

	========================================================================
*/
NDIS_802_11_NETWORK_TYPE NetworkTypeInUseSanity(
    IN PBSS_ENTRY pBss)
{
	NDIS_802_11_NETWORK_TYPE	NetWorkType;
	UCHAR						rate, i;

	NetWorkType = Ndis802_11DS;

	if (pBss->Channel <= 14)
	{
		//
		// First check support Rate.
		//
		for (i = 0; i < pBss->SupRateLen; i++)
		{
			rate = pBss->SupRate[i] & 0x7f; // Mask out basic rate set bit
			if ((rate == 2) || (rate == 4) || (rate == 11) || (rate == 22))
			{
				continue;
			}
			else
			{
				//
				// Otherwise (even rate > 108) means Ndis802_11OFDM24
				//
				NetWorkType = Ndis802_11OFDM24;
				break;
			}
		}

		//
		// Second check Extend Rate.
		//
		if (NetWorkType != Ndis802_11OFDM24)
		{
			for (i = 0; i < pBss->ExtRateLen; i++)
			{
				rate = pBss->SupRate[i] & 0x7f; // Mask out basic rate set bit
				if ((rate == 2) || (rate == 4) || (rate == 11) || (rate == 22))
				{
					continue;
				}
				else
				{
					//
					// Otherwise (even rate > 108) means Ndis802_11OFDM24
					//
					NetWorkType = Ndis802_11OFDM24;
					break;
				}
			}
		}
	}
	else
	{
		NetWorkType = Ndis802_11OFDM5;
	}

    if (pBss->HtCapabilityLen != 0)
    {
        if (NetWorkType == Ndis802_11OFDM5)
            NetWorkType = Ndis802_11OFDM5_N;
        else
            NetWorkType = Ndis802_11OFDM24_N;
    }

	return NetWorkType;
}

/*
    ==========================================================================
    Description:
        WPA message sanity check
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
 */
BOOLEAN PeerWpaMessageSanity(
    IN 	PRTMP_ADAPTER 		pAd,
    IN 	PEAPOL_PACKET 		pMsg,
    IN 	ULONG 				MsgLen,
    IN 	UCHAR				MsgType,
    IN 	MAC_TABLE_ENTRY  	*pEntry)
{
	UCHAR			mic[LEN_KEY_DESC_MIC], digest[80], KEYDATA[MAX_LEN_OF_RSNIE];
	BOOLEAN			bReplayDiff = FALSE;
	BOOLEAN			bWPA2 = FALSE;
	KEY_INFO		EapolKeyInfo;
	UCHAR			GroupKeyIndex = 0;


	NdisZeroMemory(mic, sizeof(mic));
	NdisZeroMemory(digest, sizeof(digest));
	NdisZeroMemory(KEYDATA, sizeof(KEYDATA));
	NdisZeroMemory((PUCHAR)&EapolKeyInfo, sizeof(EapolKeyInfo));

	NdisMoveMemory((PUCHAR)&EapolKeyInfo, (PUCHAR)&pMsg->KeyDesc.KeyInfo, sizeof(KEY_INFO));

	*((USHORT *)&EapolKeyInfo) = cpu2le16(*((USHORT *)&EapolKeyInfo));

	// Choose WPA2 or not
	if ((pEntry->AuthMode == Ndis802_11AuthModeWPA2) || (pEntry->AuthMode == Ndis802_11AuthModeWPA2PSK))
		bWPA2 = TRUE;

	// 0. Check MsgType
	if ((MsgType > EAPOL_GROUP_MSG_2) || (MsgType < EAPOL_PAIR_MSG_1))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("The message type is invalid(%d)! \n", MsgType));
		return FALSE;
	}

	// 1. Replay counter check
 	if (MsgType == EAPOL_PAIR_MSG_1 || MsgType == EAPOL_PAIR_MSG_3 || MsgType == EAPOL_GROUP_MSG_1)	// For supplicant
    {
    	// First validate replay counter, only accept message with larger replay counter.
		// Let equal pass, some AP start with all zero replay counter
		UCHAR	ZeroReplay[LEN_KEY_DESC_REPLAY];

        NdisZeroMemory(ZeroReplay, LEN_KEY_DESC_REPLAY);
		if ((RTMPCompareMemory(pMsg->KeyDesc.ReplayCounter, pEntry->R_Counter, LEN_KEY_DESC_REPLAY) != 1) &&
			(RTMPCompareMemory(pMsg->KeyDesc.ReplayCounter, ZeroReplay, LEN_KEY_DESC_REPLAY) != 0))
    	{
			bReplayDiff = TRUE;
    	}
 	}
	else if (MsgType == EAPOL_PAIR_MSG_2 || MsgType == EAPOL_PAIR_MSG_4 || MsgType == EAPOL_GROUP_MSG_2)	// For authenticator
	{
		// check Replay Counter coresponds to MSG from authenticator, otherwise discard
    	if (!NdisEqualMemory(pMsg->KeyDesc.ReplayCounter, pEntry->R_Counter, LEN_KEY_DESC_REPLAY))
    	{
			bReplayDiff = TRUE;
    	}
	}

	// Replay Counter different condition
	if (bReplayDiff)
	{
		// send wireless event - for replay counter different
		if (pAd->CommonCfg.bWirelessEvent)
			RTMPSendWirelessEvent(pAd, IW_REPLAY_COUNTER_DIFF_EVENT_FLAG, pEntry->Addr, pEntry->apidx, 0);

		if (MsgType < EAPOL_GROUP_MSG_1)
		{
           	DBGPRINT(RT_DEBUG_ERROR, ("Replay Counter Different in pairwise msg %d of 4-way handshake!\n", MsgType));
		}
		else
		{
			DBGPRINT(RT_DEBUG_ERROR, ("Replay Counter Different in group msg %d of 2-way handshake!\n", (MsgType - EAPOL_PAIR_MSG_4)));
		}

		hex_dump("Receive replay counter ", pMsg->KeyDesc.ReplayCounter, LEN_KEY_DESC_REPLAY);
		hex_dump("Current replay counter ", pEntry->R_Counter, LEN_KEY_DESC_REPLAY);
        return FALSE;
	}

	// 2. Verify MIC except Pairwise Msg1
	if (MsgType != EAPOL_PAIR_MSG_1)
	{
		UCHAR			rcvd_mic[LEN_KEY_DESC_MIC];

		// Record the received MIC for check later
		NdisMoveMemory(rcvd_mic, pMsg->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
		NdisZeroMemory(pMsg->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);

        if (pEntry->WepStatus == Ndis802_11Encryption2Enabled)	// TKIP
        {
            hmac_md5(pEntry->PTK, LEN_EAP_MICK, (PUCHAR)pMsg, MsgLen, mic);
        }
        else if (pEntry->WepStatus == Ndis802_11Encryption3Enabled)	// AES
        {
            HMAC_SHA1((PUCHAR)pMsg, MsgLen, pEntry->PTK, LEN_EAP_MICK, digest);
            NdisMoveMemory(mic, digest, LEN_KEY_DESC_MIC);
        }

        if (!NdisEqualMemory(rcvd_mic, mic, LEN_KEY_DESC_MIC))
        {
			// send wireless event - for MIC different
			if (pAd->CommonCfg.bWirelessEvent)
				RTMPSendWirelessEvent(pAd, IW_MIC_DIFF_EVENT_FLAG, pEntry->Addr, pEntry->apidx, 0);

			if (MsgType < EAPOL_GROUP_MSG_1)
			{
            	DBGPRINT(RT_DEBUG_ERROR, ("MIC Different in pairwise msg %d of 4-way handshake!\n", MsgType));
			}
			else
			{
				DBGPRINT(RT_DEBUG_ERROR, ("MIC Different in group msg %d of 2-way handshake!\n", (MsgType - EAPOL_PAIR_MSG_4)));
			}

			hex_dump("Received MIC", rcvd_mic, LEN_KEY_DESC_MIC);
			hex_dump("Desired  MIC", mic, LEN_KEY_DESC_MIC);

			return FALSE;
        }
	}

	// Extract the context of the Key Data field if it exist
	// The field in pairwise_msg_2_WPA1(WPA2) & pairwise_msg_3_WPA1 is un-encrypted.
	// The field in group_msg_1_WPA1(WPA2) & pairwise_msg_3_WPA2 is encrypted.
	if (pMsg->KeyDesc.KeyDataLen[1] > 0)
	{
		// Decrypt this field
		if ((MsgType == EAPOL_PAIR_MSG_3 && bWPA2) || (MsgType == EAPOL_GROUP_MSG_1))
		{
			if(pEntry->WepStatus == Ndis802_11Encryption3Enabled)
			{
				// AES
				AES_GTK_KEY_UNWRAP(&pEntry->PTK[16], KEYDATA, pMsg->KeyDesc.KeyDataLen[1],pMsg->KeyDesc.KeyData);
			}
			else
			{
				INT 	i;
				UCHAR   Key[32];
				// Decrypt TKIP GTK
				// Construct 32 bytes RC4 Key
				NdisMoveMemory(Key, pMsg->KeyDesc.KeyIv, 16);
				NdisMoveMemory(&Key[16], &pEntry->PTK[16], 16);
				ARCFOUR_INIT(&pAd->PrivateInfo.WEPCONTEXT, Key, 32);
				//discard first 256 bytes
				for(i = 0; i < 256; i++)
					ARCFOUR_BYTE(&pAd->PrivateInfo.WEPCONTEXT);
				// Decrypt GTK. Becareful, there is no ICV to check the result is correct or not
				ARCFOUR_DECRYPT(&pAd->PrivateInfo.WEPCONTEXT, KEYDATA, pMsg->KeyDesc.KeyData, pMsg->KeyDesc.KeyDataLen[1]);
			}

			if (!bWPA2 && (MsgType == EAPOL_GROUP_MSG_1))
				GroupKeyIndex = EapolKeyInfo.KeyIndex;

		}
		else if ((MsgType == EAPOL_PAIR_MSG_2) || (MsgType == EAPOL_PAIR_MSG_3 && !bWPA2))
		{
			NdisMoveMemory(KEYDATA, pMsg->KeyDesc.KeyData, pMsg->KeyDesc.KeyDataLen[1]);
		}
		else
		{

			return TRUE;
		}

		// Parse Key Data field to
		// 1. verify RSN IE for pairwise_msg_2_WPA1(WPA2) ,pairwise_msg_3_WPA1(WPA2)
		// 2. verify KDE format for pairwise_msg_3_WPA2, group_msg_1_WPA2
		// 3. update shared key for pairwise_msg_3_WPA2, group_msg_1_WPA1(WPA2)
		if (!RTMPParseEapolKeyData(pAd, KEYDATA, pMsg->KeyDesc.KeyDataLen[1], GroupKeyIndex, MsgType, bWPA2, pEntry))
		{
			return FALSE;
		}
	}

	return TRUE;

}
