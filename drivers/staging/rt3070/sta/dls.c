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
    dls.c

    Abstract:
    Handle WMM-DLS state machine

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
    Rory Chen   02-14-2006
	Arvin Tai	06-03-2008	  Modified for RT28xx
 */

#include "../rt_config.h"

/*
    ==========================================================================
    Description:
        dls state machine init, including state transition and timer init
    Parameters:
        Sm - pointer to the dls state machine
    Note:
        The state machine looks like this

                            DLS_IDLE
    MT2_MLME_DLS_REQUEST   MlmeDlsReqAction
    MT2_PEER_DLS_REQUEST   PeerDlsReqAction
    MT2_PEER_DLS_RESPONSE  PeerDlsRspAction
    MT2_MLME_DLS_TEARDOWN  MlmeTearDownAction
    MT2_PEER_DLS_TEARDOWN  PeerTearDownAction

	IRQL = PASSIVE_LEVEL

    ==========================================================================
 */
void DlsStateMachineInit(
    IN PRTMP_ADAPTER pAd,
    IN STATE_MACHINE *Sm,
    OUT STATE_MACHINE_FUNC Trans[])
{
	UCHAR	i;

    StateMachineInit(Sm, (STATE_MACHINE_FUNC*)Trans, MAX_DLS_STATE, MAX_DLS_MSG, (STATE_MACHINE_FUNC)Drop, DLS_IDLE, DLS_MACHINE_BASE);

    // the first column
    StateMachineSetAction(Sm, DLS_IDLE, MT2_MLME_DLS_REQ, (STATE_MACHINE_FUNC)MlmeDlsReqAction);
    StateMachineSetAction(Sm, DLS_IDLE, MT2_PEER_DLS_REQ, (STATE_MACHINE_FUNC)PeerDlsReqAction);
    StateMachineSetAction(Sm, DLS_IDLE, MT2_PEER_DLS_RSP, (STATE_MACHINE_FUNC)PeerDlsRspAction);
    StateMachineSetAction(Sm, DLS_IDLE, MT2_MLME_DLS_TEAR_DOWN, (STATE_MACHINE_FUNC)MlmeDlsTearDownAction);
    StateMachineSetAction(Sm, DLS_IDLE, MT2_PEER_DLS_TEAR_DOWN, (STATE_MACHINE_FUNC)PeerDlsTearDownAction);

	for (i=0; i<MAX_NUM_OF_DLS_ENTRY; i++)
	{
		pAd->StaCfg.DLSEntry[i].pAd = pAd;
		RTMPInitTimer(pAd, &pAd->StaCfg.DLSEntry[i].Timer, GET_TIMER_FUNCTION(DlsTimeoutAction), pAd, FALSE);
	}
}

/*
    ==========================================================================
    Description:

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID MlmeDlsReqAction(
    IN PRTMP_ADAPTER pAd,
    IN MLME_QUEUE_ELEM *Elem)
{
	PUCHAR			pOutBuffer = NULL;
	NDIS_STATUS		NStatus;
	ULONG			FrameLen = 0;
	HEADER_802_11	DlsReqHdr;
	PRT_802_11_DLS	pDLS = NULL;
	UCHAR			Category = CATEGORY_DLS;
	UCHAR			Action = ACTION_DLS_REQUEST;
	ULONG			tmp;
	USHORT			reason;
	ULONG			Timeout;
	BOOLEAN			TimerCancelled;

	if(!MlmeDlsReqSanity(pAd, Elem->Msg, Elem->MsgLen, &pDLS, &reason))
		return;

	DBGPRINT(RT_DEBUG_TRACE,("DLS - MlmeDlsReqAction() \n"));

	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  //Get an unused nonpaged memory
	if (NStatus != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_ERROR,("DLS - MlmeDlsReqAction() allocate memory failed \n"));
		return;
	}

	ActHeaderInit(pAd, &DlsReqHdr, pAd->CommonCfg.Bssid, pAd->CurrentAddress, pAd->CommonCfg.Bssid);

	// Build basic frame first
	MakeOutgoingFrame(pOutBuffer,				&FrameLen,
					sizeof(HEADER_802_11),		&DlsReqHdr,
					1,							&Category,
					1,							&Action,
					6,							&pDLS->MacAddr,
					6,							pAd->CurrentAddress,
					2,							&pAd->StaActive.CapabilityInfo,
					2,							&pDLS->TimeOut,
					1,							&SupRateIe,
					1,							&pAd->MlmeAux.SupRateLen,
					pAd->MlmeAux.SupRateLen,	pAd->MlmeAux.SupRate,
					END_OF_ARGS);

	if (pAd->MlmeAux.ExtRateLen != 0)
	{
		MakeOutgoingFrame(pOutBuffer + FrameLen,	&tmp,
						  1,						&ExtRateIe,
						  1,						&pAd->MlmeAux.ExtRateLen,
						  pAd->MlmeAux.ExtRateLen,	pAd->MlmeAux.ExtRate,
						  END_OF_ARGS);
		FrameLen += tmp;
	}

#ifdef DOT11_N_SUPPORT
	if ((pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED))
	{
		UCHAR HtLen;

#ifdef RT_BIG_ENDIAN
		HT_CAPABILITY_IE HtCapabilityTmp;
#endif

		// add HT Capability IE
		HtLen = sizeof(HT_CAPABILITY_IE);
#ifndef RT_BIG_ENDIAN
		MakeOutgoingFrame(pOutBuffer + FrameLen,	&tmp,
							1,						&HtCapIe,
							1,						&HtLen,
							HtLen,					&pAd->CommonCfg.HtCapability,
							END_OF_ARGS);
#else
		NdisMoveMemory(&HtCapabilityTmp, &pAd->CommonCfg.HtCapability, HtLen);
							*(USHORT *)(&HtCapabilityTmp.HtCapInfo) = SWAP16(*(USHORT *)(&HtCapabilityTmp.HtCapInfo));
							*(USHORT *)(&HtCapabilityTmp.ExtHtCapInfo) = SWAP16(*(USHORT *)(&HtCapabilityTmp.ExtHtCapInfo));

		MakeOutgoingFrame(pOutBuffer + FrameLen,	&tmp,
							1,						&HtCapIe,
							1,						&HtLen,
							HtLen,					&HtCapabilityTmp,
							END_OF_ARGS);
#endif
		FrameLen = FrameLen + tmp;
	}
#endif // DOT11_N_SUPPORT //

	RTMPCancelTimer(&pDLS->Timer, &TimerCancelled);
	Timeout = DLS_TIMEOUT;
	RTMPSetTimer(&pDLS->Timer, Timeout);

	MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);
}

/*
    ==========================================================================
    Description:

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID PeerDlsReqAction(
    IN PRTMP_ADAPTER pAd,
    IN MLME_QUEUE_ELEM *Elem)
{
	PUCHAR			pOutBuffer = NULL;
	NDIS_STATUS		NStatus;
	ULONG			FrameLen = 0;
	USHORT			StatusCode = MLME_SUCCESS;
	HEADER_802_11	DlsRspHdr;
	UCHAR			Category = CATEGORY_DLS;
	UCHAR			Action = ACTION_DLS_RESPONSE;
	ULONG			tmp;
	USHORT			CapabilityInfo;
	UCHAR			DA[MAC_ADDR_LEN], SA[MAC_ADDR_LEN];
	USHORT			DLSTimeOut;
	SHORT			i;
	ULONG			Timeout;
	BOOLEAN			TimerCancelled;
	PRT_802_11_DLS	pDLS = NULL;
	UCHAR			MaxSupportedRateIn500Kbps = 0;
    UCHAR			SupportedRatesLen;
    UCHAR			SupportedRates[MAX_LEN_OF_SUPPORTED_RATES];
	UCHAR			HtCapabilityLen;
	HT_CAPABILITY_IE	HtCapability;

	if (!PeerDlsReqSanity(pAd, Elem->Msg, Elem->MsgLen, DA, SA, &CapabilityInfo, &DLSTimeOut,
							&SupportedRatesLen, &SupportedRates[0], &HtCapabilityLen, &HtCapability))
		return;

    // supported rates array may not be sorted. sort it and find the maximum rate
    for (i = 0; i < SupportedRatesLen; i++)
    {
        if (MaxSupportedRateIn500Kbps < (SupportedRates[i] & 0x7f))
            MaxSupportedRateIn500Kbps = SupportedRates[i] & 0x7f;
    }

	DBGPRINT(RT_DEBUG_TRACE,("DLS - PeerDlsReqAction() from %02x:%02x:%02x:%02x:%02x:%02x\n", SA[0], SA[1], SA[2], SA[3], SA[4], SA[5]));

	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  //Get an unused nonpaged memory
	if (NStatus != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_ERROR,("DLS - PeerDlsReqAction() allocate memory failed \n"));
		return;
	}

	if (!INFRA_ON(pAd))
	{
		StatusCode = MLME_REQUEST_DECLINED;
	}
	else if (!pAd->CommonCfg.bWmmCapable)
	{
		StatusCode = MLME_DEST_STA_IS_NOT_A_QSTA;
	}
	else if (!pAd->CommonCfg.bDLSCapable)
	{
		StatusCode = MLME_REQUEST_DECLINED;
	}
	else
	{
		// find table to update parameters
		for (i = (MAX_NUM_OF_DLS_ENTRY-1); i >= 0; i--)
		{
			if (pAd->StaCfg.DLSEntry[i].Valid && MAC_ADDR_EQUAL(SA, pAd->StaCfg.DLSEntry[i].MacAddr))
			{
				if (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPA)
					pAd->StaCfg.DLSEntry[i].Status = DLS_WAIT_KEY;
				else
				{
					RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &TimerCancelled);
					pAd->StaCfg.DLSEntry[i].Status = DLS_FINISH;
				}

				pAd->StaCfg.DLSEntry[i].Sequence = 0;
				pAd->StaCfg.DLSEntry[i].TimeOut = DLSTimeOut;
				pAd->StaCfg.DLSEntry[i].CountDownTimer = DLSTimeOut;
				if (HtCapabilityLen != 0)
					pAd->StaCfg.DLSEntry[i].bHTCap = TRUE;
				else
					pAd->StaCfg.DLSEntry[i].bHTCap = FALSE;
				pDLS = &pAd->StaCfg.DLSEntry[i];
				break;
			}
		}

		// can not find in table, create a new one
		if (i < 0)
		{
			DBGPRINT(RT_DEBUG_TRACE,("DLS - PeerDlsReqAction() can not find same entry \n"));
			for (i=(MAX_NUM_OF_DLS_ENTRY - 1); i >= MAX_NUM_OF_INIT_DLS_ENTRY; i--)
			{
				if (!pAd->StaCfg.DLSEntry[i].Valid)
				{
					MAC_TABLE_ENTRY *pEntry;
					UCHAR MaxSupportedRate = RATE_11;

					if (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPA)
					{
						pAd->StaCfg.DLSEntry[i].Status = DLS_WAIT_KEY;
					}
					else
					{
						RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &TimerCancelled);
						pAd->StaCfg.DLSEntry[i].Status = DLS_FINISH;
					}

					pAd->StaCfg.DLSEntry[i].Sequence = 0;
					pAd->StaCfg.DLSEntry[i].Valid = TRUE;
					pAd->StaCfg.DLSEntry[i].TimeOut = DLSTimeOut;
					pAd->StaCfg.DLSEntry[i].CountDownTimer = DLSTimeOut;
					NdisMoveMemory(pAd->StaCfg.DLSEntry[i].MacAddr, SA, MAC_ADDR_LEN);
					if (HtCapabilityLen != 0)
						pAd->StaCfg.DLSEntry[i].bHTCap = TRUE;
					else
						pAd->StaCfg.DLSEntry[i].bHTCap = FALSE;
					pDLS = &pAd->StaCfg.DLSEntry[i];
					pEntry = MacTableInsertDlsEntry(pAd, SA, i);

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

					pEntry->MaxSupportedRate = min(pAd->CommonCfg.MaxTxRate, MaxSupportedRate);

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

					pEntry->MaxHTPhyMode.field.BW = BW_20;
					pEntry->MinHTPhyMode.field.BW = BW_20;

#ifdef DOT11_N_SUPPORT
					pEntry->HTCapability.MCSSet[0] = 0;
					pEntry->HTCapability.MCSSet[1] = 0;

					// If this Entry supports 802.11n, upgrade to HT rate.
					if ((HtCapabilityLen != 0) && (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED))
					{
						UCHAR	j, bitmask; //k,bitmask;
						CHAR    ii;

						if ((HtCapability.HtCapInfo.GF) && (pAd->CommonCfg.DesiredHtPhy.GF))
						{
							pEntry->MaxHTPhyMode.field.MODE = MODE_HTGREENFIELD;
						}
						else
						{
							pEntry->MaxHTPhyMode.field.MODE = MODE_HTMIX;
							pAd->MacTab.fAnyStationNonGF = TRUE;
							pAd->CommonCfg.AddHTInfo.AddHtInfo2.NonGfPresent = 1;
						}

						if ((HtCapability.HtCapInfo.ChannelWidth) && (pAd->CommonCfg.DesiredHtPhy.ChannelWidth))
						{
							pEntry->MaxHTPhyMode.field.BW= BW_40;
							pEntry->MaxHTPhyMode.field.ShortGI = ((pAd->CommonCfg.DesiredHtPhy.ShortGIfor40)&(HtCapability.HtCapInfo.ShortGIfor40));
						}
						else
						{
							pEntry->MaxHTPhyMode.field.BW = BW_20;
							pEntry->MaxHTPhyMode.field.ShortGI = ((pAd->CommonCfg.DesiredHtPhy.ShortGIfor20)&(HtCapability.HtCapInfo.ShortGIfor20));
							pAd->MacTab.fAnyStation20Only = TRUE;
						}

						// find max fixed rate
						for (ii=15; ii>=0; ii--)
						{
							j = ii/8;
							bitmask = (1<<(ii-(j*8)));
							if ( (pAd->StaCfg.DesiredHtPhyInfo.MCSSet[j]&bitmask) && (HtCapability.MCSSet[j]&bitmask))
							{
								pEntry->MaxHTPhyMode.field.MCS = ii;
								break;
							}
							if (ii==0)
								break;
						}


						if (pAd->StaCfg.DesiredTransmitSetting.field.MCS != MCS_AUTO)
						{

							printk("@@@ pAd->CommonCfg.RegTransmitSetting.field.MCS = %d\n",
								pAd->StaCfg.DesiredTransmitSetting.field.MCS);
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

						pEntry->MaxHTPhyMode.field.STBC = (HtCapability.HtCapInfo.RxSTBC & (pAd->CommonCfg.DesiredHtPhy.TxSTBC));
						pEntry->MpduDensity = HtCapability.HtCapParm.MpduDensity;
						pEntry->MaxRAmpduFactor = HtCapability.HtCapParm.MaxRAmpduFactor;
						pEntry->MmpsMode = (UCHAR)HtCapability.HtCapInfo.MimoPs;
						pEntry->AMsduSize = (UCHAR)HtCapability.HtCapInfo.AMsduSize;
						pEntry->HTPhyMode.word = pEntry->MaxHTPhyMode.word;

						if (HtCapability.HtCapInfo.ShortGIfor20)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_SGI20_CAPABLE);
						if (HtCapability.HtCapInfo.ShortGIfor40)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_SGI40_CAPABLE);
						if (HtCapability.HtCapInfo.TxSTBC)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_TxSTBC_CAPABLE);
						if (HtCapability.HtCapInfo.RxSTBC)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_RxSTBC_CAPABLE);
						if (HtCapability.ExtHtCapInfo.PlusHTC)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_HTC_CAPABLE);
						if (pAd->CommonCfg.bRdg && HtCapability.ExtHtCapInfo.RDGSupport)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_RDG_CAPABLE);
						if (HtCapability.ExtHtCapInfo.MCSFeedback == 0x03)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_MCSFEEDBACK_CAPABLE);

						NdisMoveMemory(&pEntry->HTCapability, &HtCapability, sizeof(HT_CAPABILITY_IE));
					}
#endif // DOT11_N_SUPPORT //

					pEntry->HTPhyMode.word = pEntry->MaxHTPhyMode.word;
					pEntry->CurrTxRate = pEntry->MaxSupportedRate;
					CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_WMM_CAPABLE);

					if (pAd->StaCfg.bAutoTxRateSwitch == TRUE)
					{
						PUCHAR pTable;
						UCHAR TableSize = 0;

						MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize, &pEntry->CurrTxRateIndex);
						pEntry->bAutoTxRateSwitch = TRUE;
					}
					else
					{
						pEntry->HTPhyMode.field.MODE	= pAd->StaCfg.HTPhyMode.field.MODE;
						pEntry->HTPhyMode.field.MCS	= pAd->StaCfg.HTPhyMode.field.MCS;
						pEntry->bAutoTxRateSwitch = FALSE;

						RTMPUpdateLegacyTxSetting((UCHAR)pAd->StaCfg.DesiredTransmitSetting.field.FixedTxMode, pEntry);
					}
					pEntry->RateLen = SupportedRatesLen;

					break;
				}
			}
		}
		StatusCode = MLME_SUCCESS;

		// can not find in table, create a new one
		if (i < 0)
		{
			StatusCode = MLME_QOS_UNSPECIFY;
			DBGPRINT(RT_DEBUG_ERROR,("DLS - PeerDlsReqAction() DLSEntry table full(only can support %d DLS session) \n", MAX_NUM_OF_DLS_ENTRY - MAX_NUM_OF_INIT_DLS_ENTRY));
		}
		else
		{
			DBGPRINT(RT_DEBUG_TRACE,("DLS - PeerDlsReqAction() use entry(%d) %02x:%02x:%02x:%02x:%02x:%02x\n",
				i, SA[0], SA[1], SA[2], SA[3], SA[4], SA[5]));
		}
	}

	ActHeaderInit(pAd, &DlsRspHdr, pAd->CommonCfg.Bssid, pAd->CurrentAddress, pAd->CommonCfg.Bssid);

	// Build basic frame first
	if (StatusCode == MLME_SUCCESS)
	{
		MakeOutgoingFrame(pOutBuffer,				&FrameLen,
						sizeof(HEADER_802_11),		&DlsRspHdr,
						1,							&Category,
						1,							&Action,
						2,							&StatusCode,
						6,							SA,
						6,							pAd->CurrentAddress,
						2,							&pAd->StaActive.CapabilityInfo,
						1,							&SupRateIe,
						1,							&pAd->MlmeAux.SupRateLen,
						pAd->MlmeAux.SupRateLen,	pAd->MlmeAux.SupRate,
						END_OF_ARGS);

		if (pAd->MlmeAux.ExtRateLen != 0)
		{
			MakeOutgoingFrame(pOutBuffer + FrameLen,	&tmp,
							  1,						&ExtRateIe,
							  1,						&pAd->MlmeAux.ExtRateLen,
							  pAd->MlmeAux.ExtRateLen, 	pAd->MlmeAux.ExtRate,
							  END_OF_ARGS);
			FrameLen += tmp;
		}

#ifdef DOT11_N_SUPPORT
		if ((pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED))
		{
			UCHAR HtLen;

#ifdef RT_BIG_ENDIAN
			HT_CAPABILITY_IE HtCapabilityTmp;
#endif

			// add HT Capability IE
			HtLen = sizeof(HT_CAPABILITY_IE);
#ifndef RT_BIG_ENDIAN
			MakeOutgoingFrame(pOutBuffer + FrameLen,	&tmp,
								1,						&HtCapIe,
								1,						&HtLen,
								HtLen,					&pAd->CommonCfg.HtCapability,
								END_OF_ARGS);
#else
			NdisMoveMemory(&HtCapabilityTmp, &pAd->CommonCfg.HtCapability, HtLen);
								*(USHORT *)(&HtCapabilityTmp.HtCapInfo) = SWAP16(*(USHORT *)(&HtCapabilityTmp.HtCapInfo));
								*(USHORT *)(&HtCapabilityTmp.ExtHtCapInfo) = SWAP16(*(USHORT *)(&HtCapabilityTmp.ExtHtCapInfo));

			MakeOutgoingFrame(pOutBuffer + FrameLen,	&tmp,
								1,						&HtCapIe,
								1,						&HtLen,
								HtLen,					&HtCapabilityTmp,
								END_OF_ARGS);
#endif
			FrameLen = FrameLen + tmp;
		}
#endif // DOT11_N_SUPPORT //

		if (pDLS && (pDLS->Status != DLS_FINISH))
		{
			RTMPCancelTimer(&pDLS->Timer, &TimerCancelled);
			Timeout = DLS_TIMEOUT;
			RTMPSetTimer(&pDLS->Timer, Timeout);
		}
	}
	else
	{
		MakeOutgoingFrame(pOutBuffer,				&FrameLen,
						sizeof(HEADER_802_11),		&DlsRspHdr,
						1,							&Category,
						1,							&Action,
						2,							&StatusCode,
						6,							SA,
						6,							pAd->CurrentAddress,
						END_OF_ARGS);
	}

	MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);
}

/*
    ==========================================================================
    Description:

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID PeerDlsRspAction(
    IN PRTMP_ADAPTER pAd,
    IN MLME_QUEUE_ELEM *Elem)
{
	USHORT		CapabilityInfo;
	UCHAR		DA[MAC_ADDR_LEN], SA[MAC_ADDR_LEN];
	USHORT		StatusCode;
	SHORT		i;
	BOOLEAN		TimerCancelled;
	UCHAR		MaxSupportedRateIn500Kbps = 0;
    UCHAR		SupportedRatesLen;
    UCHAR		SupportedRates[MAX_LEN_OF_SUPPORTED_RATES];
	UCHAR		HtCapabilityLen;
	HT_CAPABILITY_IE	HtCapability;

	if (!pAd->CommonCfg.bDLSCapable)
		return;

	if (!INFRA_ON(pAd))
		return;

	if (!PeerDlsRspSanity(pAd, Elem->Msg, Elem->MsgLen, DA, SA, &CapabilityInfo, &StatusCode,
							&SupportedRatesLen, &SupportedRates[0], &HtCapabilityLen, &HtCapability))
		return;

    // supported rates array may not be sorted. sort it and find the maximum rate
    for (i=0; i<SupportedRatesLen; i++)
    {
        if (MaxSupportedRateIn500Kbps < (SupportedRates[i] & 0x7f))
            MaxSupportedRateIn500Kbps = SupportedRates[i] & 0x7f;
    }

	DBGPRINT(RT_DEBUG_TRACE,("DLS - PeerDlsRspAction() from %02x:%02x:%02x:%02x:%02x:%02x with StatusCode=%d, CapabilityInfo=0x%x\n",
		SA[0], SA[1], SA[2], SA[3], SA[4], SA[5], StatusCode, CapabilityInfo));

	for (i = 0; i < MAX_NUM_OF_INIT_DLS_ENTRY; i++)
	{
		if (pAd->StaCfg.DLSEntry[i].Valid && MAC_ADDR_EQUAL(SA, pAd->StaCfg.DLSEntry[i].MacAddr))
		{
			if (StatusCode == MLME_SUCCESS)
			{
				MAC_TABLE_ENTRY *pEntry;
				UCHAR MaxSupportedRate = RATE_11;

				pEntry = MacTableInsertDlsEntry(pAd, SA, i);

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

				pEntry->MaxSupportedRate = min(pAd->CommonCfg.MaxTxRate, MaxSupportedRate);

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

				pEntry->MaxHTPhyMode.field.BW = BW_20;
				pEntry->MinHTPhyMode.field.BW = BW_20;

#ifdef DOT11_N_SUPPORT
				pEntry->HTCapability.MCSSet[0] = 0;
				pEntry->HTCapability.MCSSet[1] = 0;

				// If this Entry supports 802.11n, upgrade to HT rate.
				if ((HtCapabilityLen != 0) && (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED))
				{
					UCHAR	j, bitmask; //k,bitmask;
					CHAR    ii;

					if ((HtCapability.HtCapInfo.GF) && (pAd->CommonCfg.DesiredHtPhy.GF))
					{
						pEntry->MaxHTPhyMode.field.MODE = MODE_HTGREENFIELD;
					}
					else
					{
						pEntry->MaxHTPhyMode.field.MODE = MODE_HTMIX;
						pAd->MacTab.fAnyStationNonGF = TRUE;
						pAd->CommonCfg.AddHTInfo.AddHtInfo2.NonGfPresent = 1;
					}

					if ((HtCapability.HtCapInfo.ChannelWidth) && (pAd->CommonCfg.DesiredHtPhy.ChannelWidth))
					{
						pEntry->MaxHTPhyMode.field.BW= BW_40;
						pEntry->MaxHTPhyMode.field.ShortGI = ((pAd->CommonCfg.DesiredHtPhy.ShortGIfor40)&(HtCapability.HtCapInfo.ShortGIfor40));
					}
					else
					{
						pEntry->MaxHTPhyMode.field.BW = BW_20;
						pEntry->MaxHTPhyMode.field.ShortGI = ((pAd->CommonCfg.DesiredHtPhy.ShortGIfor20)&(HtCapability.HtCapInfo.ShortGIfor20));
						pAd->MacTab.fAnyStation20Only = TRUE;
					}

					// find max fixed rate
					for (ii=15; ii>=0; ii--)
					{
						j = ii/8;
						bitmask = (1<<(ii-(j*8)));
						if ( (pAd->StaCfg.DesiredHtPhyInfo.MCSSet[j]&bitmask) && (HtCapability.MCSSet[j]&bitmask))
						{
							pEntry->MaxHTPhyMode.field.MCS = ii;
							break;
						}
						if (ii==0)
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

					pEntry->MaxHTPhyMode.field.STBC = (HtCapability.HtCapInfo.RxSTBC & (pAd->CommonCfg.DesiredHtPhy.TxSTBC));
					pEntry->MpduDensity = HtCapability.HtCapParm.MpduDensity;
					pEntry->MaxRAmpduFactor = HtCapability.HtCapParm.MaxRAmpduFactor;
					pEntry->MmpsMode = (UCHAR)HtCapability.HtCapInfo.MimoPs;
					pEntry->AMsduSize = (UCHAR)HtCapability.HtCapInfo.AMsduSize;
					pEntry->HTPhyMode.word = pEntry->MaxHTPhyMode.word;

					if (HtCapability.HtCapInfo.ShortGIfor20)
						CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_SGI20_CAPABLE);
					if (HtCapability.HtCapInfo.ShortGIfor40)
						CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_SGI40_CAPABLE);
					if (HtCapability.HtCapInfo.TxSTBC)
						CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_TxSTBC_CAPABLE);
					if (HtCapability.HtCapInfo.RxSTBC)
						CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_RxSTBC_CAPABLE);
					if (HtCapability.ExtHtCapInfo.PlusHTC)
						CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_HTC_CAPABLE);
					if (pAd->CommonCfg.bRdg && HtCapability.ExtHtCapInfo.RDGSupport)
						CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_RDG_CAPABLE);
					if (HtCapability.ExtHtCapInfo.MCSFeedback == 0x03)
						CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_MCSFEEDBACK_CAPABLE);

					NdisMoveMemory(&pEntry->HTCapability, &HtCapability, sizeof(HT_CAPABILITY_IE));
				}
#endif // DOT11_N_SUPPORT //
				pEntry->HTPhyMode.word = pEntry->MaxHTPhyMode.word;
				pEntry->CurrTxRate = pEntry->MaxSupportedRate;
				CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_WMM_CAPABLE);

				if (pAd->StaCfg.bAutoTxRateSwitch == TRUE)
				{
					PUCHAR pTable;
					UCHAR TableSize = 0;

					MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize, &pEntry->CurrTxRateIndex);
					pEntry->bAutoTxRateSwitch = TRUE;
				}
				else
				{
					pEntry->HTPhyMode.field.MODE	= pAd->StaCfg.HTPhyMode.field.MODE;
					pEntry->HTPhyMode.field.MCS	= pAd->StaCfg.HTPhyMode.field.MCS;
					pEntry->bAutoTxRateSwitch = FALSE;

					RTMPUpdateLegacyTxSetting((UCHAR)pAd->StaCfg.DesiredTransmitSetting.field.FixedTxMode, pEntry);
				}
				pEntry->RateLen = SupportedRatesLen;

				if (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPA)
				{
					// If support WPA or WPA2, start STAKey hand shake,
					// If failed hand shake, just tear down peer DLS
					if (RTMPSendSTAKeyRequest(pAd, pAd->StaCfg.DLSEntry[i].MacAddr) != NDIS_STATUS_SUCCESS)
					{
						MLME_DLS_REQ_STRUCT	MlmeDlsReq;
						USHORT				reason = REASON_QOS_CIPHER_NOT_SUPPORT;

						DlsParmFill(pAd, &MlmeDlsReq, &pAd->StaCfg.DLSEntry[i], reason);
						MlmeEnqueue(pAd, DLS_STATE_MACHINE, MT2_MLME_DLS_TEAR_DOWN, sizeof(MLME_DLS_REQ_STRUCT), &MlmeDlsReq);
						pAd->StaCfg.DLSEntry[i].Status = DLS_NONE;
						pAd->StaCfg.DLSEntry[i].Valid	= FALSE;
						DBGPRINT(RT_DEBUG_ERROR,("DLS - PeerDlsRspAction failed when call RTMPSendSTAKeyRequest \n"));
					}
					else
					{
						pAd->StaCfg.DLSEntry[i].Status = DLS_WAIT_KEY;
						DBGPRINT(RT_DEBUG_TRACE,("DLS - waiting for STAKey handshake procedure\n"));
					}
				}
				else
				{
					RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &TimerCancelled);
					pAd->StaCfg.DLSEntry[i].Status = DLS_FINISH;
					DBGPRINT(RT_DEBUG_TRACE,("DLS - PeerDlsRspAction() from %02x:%02x:%02x:%02x:%02x:%02x Succeed with WEP or no security\n", SA[0], SA[1], SA[2], SA[3], SA[4], SA[5]));
				}

				//initialize seq no for DLS frames.
				pAd->StaCfg.DLSEntry[i].Sequence = 0;
				if (HtCapabilityLen != 0)
					pAd->StaCfg.DLSEntry[i].bHTCap = TRUE;
				else
					pAd->StaCfg.DLSEntry[i].bHTCap = FALSE;
			}
			else
			{
				// DLS setup procedure failed.
				pAd->StaCfg.DLSEntry[i].Status = DLS_NONE;
				pAd->StaCfg.DLSEntry[i].Valid	= FALSE;
				RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &TimerCancelled);
				DBGPRINT(RT_DEBUG_ERROR,("DLS - PeerDlsRspAction failed with StatusCode=%d \n", StatusCode));
			}
		}
	}

	if (i >= MAX_NUM_OF_INIT_DLS_ENTRY)
	{
		DBGPRINT(RT_DEBUG_TRACE,("DLS - PeerDlsRspAction() update timeout value \n"));
		for (i=(MAX_NUM_OF_DLS_ENTRY-1); i>=MAX_NUM_OF_INIT_DLS_ENTRY; i--)
		{
			if (pAd->StaCfg.DLSEntry[i].Valid && MAC_ADDR_EQUAL(SA, pAd->StaCfg.DLSEntry[i].MacAddr))
			{
				if (StatusCode == MLME_SUCCESS)
				{
					MAC_TABLE_ENTRY *pEntry;
					UCHAR MaxSupportedRate = RATE_11;

					pEntry = MacTableInsertDlsEntry(pAd, SA, i);

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

					pEntry->MaxSupportedRate = min(pAd->CommonCfg.MaxTxRate, MaxSupportedRate);

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

					pEntry->MaxHTPhyMode.field.BW = BW_20;
					pEntry->MinHTPhyMode.field.BW = BW_20;

#ifdef DOT11_N_SUPPORT
					pEntry->HTCapability.MCSSet[0] = 0;
					pEntry->HTCapability.MCSSet[1] = 0;

					// If this Entry supports 802.11n, upgrade to HT rate.
					if ((HtCapabilityLen != 0) && (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED))
					{
						UCHAR	j, bitmask; //k,bitmask;
						CHAR    ii;

						if ((HtCapability.HtCapInfo.GF) && (pAd->CommonCfg.DesiredHtPhy.GF))
						{
							pEntry->MaxHTPhyMode.field.MODE = MODE_HTGREENFIELD;
						}
						else
						{
							pEntry->MaxHTPhyMode.field.MODE = MODE_HTMIX;
							pAd->MacTab.fAnyStationNonGF = TRUE;
							pAd->CommonCfg.AddHTInfo.AddHtInfo2.NonGfPresent = 1;
						}

						if ((HtCapability.HtCapInfo.ChannelWidth) && (pAd->CommonCfg.DesiredHtPhy.ChannelWidth))
						{
							pEntry->MaxHTPhyMode.field.BW= BW_40;
							pEntry->MaxHTPhyMode.field.ShortGI = ((pAd->CommonCfg.DesiredHtPhy.ShortGIfor40)&(HtCapability.HtCapInfo.ShortGIfor40));
						}
						else
						{
							pEntry->MaxHTPhyMode.field.BW = BW_20;
							pEntry->MaxHTPhyMode.field.ShortGI = ((pAd->CommonCfg.DesiredHtPhy.ShortGIfor20)&(HtCapability.HtCapInfo.ShortGIfor20));
							pAd->MacTab.fAnyStation20Only = TRUE;
						}

						// find max fixed rate
						for (ii=15; ii>=0; ii--)
						{
							j = ii/8;
							bitmask = (1<<(ii-(j*8)));
							if ( (pAd->StaCfg.DesiredHtPhyInfo.MCSSet[j]&bitmask) && (HtCapability.MCSSet[j]&bitmask))
							{
								pEntry->MaxHTPhyMode.field.MCS = ii;
								break;
							}
							if (ii==0)
								break;
						}

						if (pAd->StaCfg.DesiredTransmitSetting.field.MCS != MCS_AUTO)
						{
							printk("@@@ pAd->CommonCfg.RegTransmitSetting.field.MCS = %d\n",
								pAd->StaCfg.DesiredTransmitSetting.field.MCS);
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

						pEntry->MaxHTPhyMode.field.STBC = (HtCapability.HtCapInfo.RxSTBC & (pAd->CommonCfg.DesiredHtPhy.TxSTBC));
						pEntry->MpduDensity = HtCapability.HtCapParm.MpduDensity;
						pEntry->MaxRAmpduFactor = HtCapability.HtCapParm.MaxRAmpduFactor;
						pEntry->MmpsMode = (UCHAR)HtCapability.HtCapInfo.MimoPs;
						pEntry->AMsduSize = (UCHAR)HtCapability.HtCapInfo.AMsduSize;
						pEntry->HTPhyMode.word = pEntry->MaxHTPhyMode.word;

						if (HtCapability.HtCapInfo.ShortGIfor20)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_SGI20_CAPABLE);
						if (HtCapability.HtCapInfo.ShortGIfor40)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_SGI40_CAPABLE);
						if (HtCapability.HtCapInfo.TxSTBC)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_TxSTBC_CAPABLE);
						if (HtCapability.HtCapInfo.RxSTBC)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_RxSTBC_CAPABLE);
						if (HtCapability.ExtHtCapInfo.PlusHTC)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_HTC_CAPABLE);
						if (pAd->CommonCfg.bRdg && HtCapability.ExtHtCapInfo.RDGSupport)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_RDG_CAPABLE);
						if (HtCapability.ExtHtCapInfo.MCSFeedback == 0x03)
							CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_MCSFEEDBACK_CAPABLE);

						NdisMoveMemory(&pEntry->HTCapability, &HtCapability, sizeof(HT_CAPABILITY_IE));
					}
#endif // DOT11_N_SUPPORT //

					pEntry->HTPhyMode.word = pEntry->MaxHTPhyMode.word;
					pEntry->CurrTxRate = pEntry->MaxSupportedRate;
					CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_WMM_CAPABLE);

					if (pAd->StaCfg.bAutoTxRateSwitch == TRUE)
					{
						PUCHAR pTable;
						UCHAR TableSize = 0;

						MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize, &pEntry->CurrTxRateIndex);
						pEntry->bAutoTxRateSwitch = TRUE;
					}
					else
					{
						pEntry->HTPhyMode.field.MODE	= pAd->StaCfg.HTPhyMode.field.MODE;
						pEntry->HTPhyMode.field.MCS	= pAd->StaCfg.HTPhyMode.field.MCS;
						pEntry->bAutoTxRateSwitch = FALSE;

						RTMPUpdateLegacyTxSetting((UCHAR)pAd->StaCfg.DesiredTransmitSetting.field.FixedTxMode, pEntry);
					}
					pEntry->RateLen = SupportedRatesLen;

					if (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPA)
					{
						// If support WPA or WPA2, start STAKey hand shake,
						// If failed hand shake, just tear down peer DLS
						if (RTMPSendSTAKeyRequest(pAd, pAd->StaCfg.DLSEntry[i].MacAddr) != NDIS_STATUS_SUCCESS)
						{
							MLME_DLS_REQ_STRUCT	MlmeDlsReq;
							USHORT				reason = REASON_QOS_CIPHER_NOT_SUPPORT;

							DlsParmFill(pAd, &MlmeDlsReq, &pAd->StaCfg.DLSEntry[i], reason);
							MlmeEnqueue(pAd, DLS_STATE_MACHINE, MT2_MLME_DLS_TEAR_DOWN, sizeof(MLME_DLS_REQ_STRUCT), &MlmeDlsReq);
							pAd->StaCfg.DLSEntry[i].Status = DLS_NONE;
							pAd->StaCfg.DLSEntry[i].Valid	= FALSE;
							DBGPRINT(RT_DEBUG_ERROR,("DLS - PeerDlsRspAction failed when call RTMPSendSTAKeyRequest \n"));
						}
						else
						{
							pAd->StaCfg.DLSEntry[i].Status = DLS_WAIT_KEY;
							DBGPRINT(RT_DEBUG_TRACE,("DLS - waiting for STAKey handshake procedure\n"));
						}
					}
					else
					{
						RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &TimerCancelled);
						pAd->StaCfg.DLSEntry[i].Status = DLS_FINISH;
						DBGPRINT(RT_DEBUG_TRACE,("DLS - PeerDlsRspAction() from %02x:%02x:%02x:%02x:%02x:%02x Succeed with WEP or no security\n", SA[0], SA[1], SA[2], SA[3], SA[4], SA[5]));
					}
					pAd->StaCfg.DLSEntry[i].Sequence = 0;
					if (HtCapabilityLen != 0)
						pAd->StaCfg.DLSEntry[i].bHTCap = TRUE;
					else
						pAd->StaCfg.DLSEntry[i].bHTCap = FALSE;
				}
				else
				{
					// DLS setup procedure failed.
					pAd->StaCfg.DLSEntry[i].Status = DLS_NONE;
					pAd->StaCfg.DLSEntry[i].Valid	= FALSE;
					RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &TimerCancelled);
					DBGPRINT(RT_DEBUG_ERROR,("DLS - PeerDlsRspAction failed with StatusCode=%d \n", StatusCode));
				}
			}
		}
	}
}

/*
    ==========================================================================
    Description:

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID MlmeDlsTearDownAction(
    IN PRTMP_ADAPTER pAd,
    IN MLME_QUEUE_ELEM *Elem)
{
	PUCHAR			pOutBuffer = NULL;
	NDIS_STATUS		NStatus;
	ULONG			FrameLen = 0;
	UCHAR			Category = CATEGORY_DLS;
	UCHAR			Action = ACTION_DLS_TEARDOWN;
	USHORT			ReasonCode = REASON_QOS_UNSPECIFY;
	HEADER_802_11	DlsTearDownHdr;
	PRT_802_11_DLS	pDLS;
	BOOLEAN			TimerCancelled;
	UCHAR			i;

	if(!MlmeDlsReqSanity(pAd, Elem->Msg, Elem->MsgLen, &pDLS, &ReasonCode))
		return;

	DBGPRINT(RT_DEBUG_TRACE,("DLS - MlmeDlsTearDownAction() with ReasonCode=%d \n", ReasonCode));

	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  //Get an unused nonpaged memory
	if (NStatus != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_ERROR,("DLS - MlmeDlsTearDownAction() allocate memory failed \n"));
		return;
	}

	ActHeaderInit(pAd, &DlsTearDownHdr, pAd->CommonCfg.Bssid, pAd->CurrentAddress, pAd->CommonCfg.Bssid);

	// Build basic frame first
	MakeOutgoingFrame(pOutBuffer,				&FrameLen,
					sizeof(HEADER_802_11),		&DlsTearDownHdr,
					1,							&Category,
					1,							&Action,
					6,							&pDLS->MacAddr,
					6,							pAd->CurrentAddress,
					2,							&ReasonCode,
					END_OF_ARGS);

	MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);
	RTMPCancelTimer(&pDLS->Timer, &TimerCancelled);

	// Remove key in local dls table entry
	for (i = 0; i < MAX_NUM_OF_INIT_DLS_ENTRY; i++)
	{
		if (MAC_ADDR_EQUAL(pDLS->MacAddr, pAd->StaCfg.DLSEntry[i].MacAddr))
		{
			MacTableDeleteDlsEntry(pAd, pAd->StaCfg.DLSEntry[i].MacTabMatchWCID, pAd->StaCfg.DLSEntry[i].MacAddr);
		}
	}

	// clear peer dls table entry
	for (i = MAX_NUM_OF_INIT_DLS_ENTRY; i < MAX_NUM_OF_DLS_ENTRY; i++)
	{
		if (MAC_ADDR_EQUAL(pDLS->MacAddr, pAd->StaCfg.DLSEntry[i].MacAddr))
		{
			pAd->StaCfg.DLSEntry[i].Status	= DLS_NONE;
			pAd->StaCfg.DLSEntry[i].Valid	= FALSE;
			RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &TimerCancelled);
			MacTableDeleteDlsEntry(pAd, pAd->StaCfg.DLSEntry[i].MacTabMatchWCID, pAd->StaCfg.DLSEntry[i].MacAddr);
		}
	}
}

/*
    ==========================================================================
    Description:

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID PeerDlsTearDownAction(
    IN PRTMP_ADAPTER pAd,
    IN MLME_QUEUE_ELEM *Elem)
{
	UCHAR			DA[MAC_ADDR_LEN], SA[MAC_ADDR_LEN];
	USHORT			ReasonCode;
	UINT			i;
	BOOLEAN			TimerCancelled;

	if (!pAd->CommonCfg.bDLSCapable)
		return;

	if (!INFRA_ON(pAd))
		return;

	if (!PeerDlsTearDownSanity(pAd, Elem->Msg, Elem->MsgLen, DA, SA, &ReasonCode))
		return;

	DBGPRINT(RT_DEBUG_TRACE,("DLS - PeerDlsTearDownAction() from %02x:%02x:%02x:%02x:%02x:%02x with ReasonCode=%d\n", SA[0], SA[1], SA[2], SA[3], SA[4], SA[5], ReasonCode));

	// clear local dls table entry
	for (i=0; i<MAX_NUM_OF_INIT_DLS_ENTRY; i++)
	{
		if (pAd->StaCfg.DLSEntry[i].Valid && MAC_ADDR_EQUAL(SA, pAd->StaCfg.DLSEntry[i].MacAddr))
		{
			pAd->StaCfg.DLSEntry[i].Status	= DLS_NONE;
			pAd->StaCfg.DLSEntry[i].Valid	= FALSE;
			RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &TimerCancelled);
			//AsicDelWcidTab(pAd, pAd->StaCfg.DLSEntry[i].MacTabMatchWCID);
			//AsicRemovePairwiseKeyEntry(pAd, BSS0, pAd->StaCfg.DLSEntry[i].MacTabMatchWCID);
			MacTableDeleteDlsEntry(pAd, pAd->StaCfg.DLSEntry[i].MacTabMatchWCID, pAd->StaCfg.DLSEntry[i].MacAddr);
		}
	}

	// clear peer dls table entry
	for (i=MAX_NUM_OF_INIT_DLS_ENTRY; i<MAX_NUM_OF_DLS_ENTRY; i++)
	{
		if (pAd->StaCfg.DLSEntry[i].Valid && MAC_ADDR_EQUAL(SA, pAd->StaCfg.DLSEntry[i].MacAddr))
		{
			pAd->StaCfg.DLSEntry[i].Status	= DLS_NONE;
			pAd->StaCfg.DLSEntry[i].Valid	= FALSE;
			RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &TimerCancelled);
			//AsicDelWcidTab(pAd, pAd->StaCfg.DLSEntry[i].MacTabMatchWCID);
			//AsicRemovePairwiseKeyEntry(pAd, BSS0, pAd->StaCfg.DLSEntry[i].MacTabMatchWCID);
			MacTableDeleteDlsEntry(pAd, pAd->StaCfg.DLSEntry[i].MacTabMatchWCID, pAd->StaCfg.DLSEntry[i].MacAddr);
		}
	}
}

/*
    ==========================================================================
    Description:

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID RTMPCheckDLSTimeOut(
	IN PRTMP_ADAPTER	pAd)
{
	ULONG				i;
	MLME_DLS_REQ_STRUCT	MlmeDlsReq;
	USHORT				reason = REASON_QOS_UNSPECIFY;

	if (! pAd->CommonCfg.bDLSCapable)
		return;

	if (! INFRA_ON(pAd))
		return;

	// If timeout value is equaled to zero, it means always not be timeout.

	// update local dls table entry
	for (i=0; i<MAX_NUM_OF_INIT_DLS_ENTRY; i++)
	{
		if ((pAd->StaCfg.DLSEntry[i].Valid) && (pAd->StaCfg.DLSEntry[i].Status == DLS_FINISH)
			&& (pAd->StaCfg.DLSEntry[i].TimeOut != 0))
		{
			pAd->StaCfg.DLSEntry[i].CountDownTimer --;

			if (pAd->StaCfg.DLSEntry[i].CountDownTimer == 0)
			{
				reason = REASON_QOS_REQUEST_TIMEOUT;
				pAd->StaCfg.DLSEntry[i].Valid	= FALSE;
				pAd->StaCfg.DLSEntry[i].Status	= DLS_NONE;
				DlsParmFill(pAd, &MlmeDlsReq, &pAd->StaCfg.DLSEntry[i], reason);
				MlmeEnqueue(pAd, DLS_STATE_MACHINE, MT2_MLME_DLS_TEAR_DOWN, sizeof(MLME_DLS_REQ_STRUCT), &MlmeDlsReq);
			}
		}
	}

	// update peer dls table entry
	for (i=MAX_NUM_OF_INIT_DLS_ENTRY; i<MAX_NUM_OF_DLS_ENTRY; i++)
	{
		if ((pAd->StaCfg.DLSEntry[i].Valid) && (pAd->StaCfg.DLSEntry[i].Status == DLS_FINISH)
			&& (pAd->StaCfg.DLSEntry[i].TimeOut != 0))
		{
			pAd->StaCfg.DLSEntry[i].CountDownTimer --;

			if (pAd->StaCfg.DLSEntry[i].CountDownTimer == 0)
			{
				reason = REASON_QOS_REQUEST_TIMEOUT;
				pAd->StaCfg.DLSEntry[i].Valid	= FALSE;
				pAd->StaCfg.DLSEntry[i].Status	= DLS_NONE;
				DlsParmFill(pAd, &MlmeDlsReq, &pAd->StaCfg.DLSEntry[i], reason);
				MlmeEnqueue(pAd, DLS_STATE_MACHINE, MT2_MLME_DLS_TEAR_DOWN, sizeof(MLME_DLS_REQ_STRUCT), &MlmeDlsReq);
			}
		}
	}
}

/*
    ==========================================================================
    Description:

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
BOOLEAN RTMPRcvFrameDLSCheck(
	IN PRTMP_ADAPTER	pAd,
	IN PHEADER_802_11	pHeader,
	IN ULONG			Len,
	IN PRT28XX_RXD_STRUC	pRxD)
{
	ULONG			i;
	BOOLEAN			bFindEntry = FALSE;
	BOOLEAN			bSTAKeyFrame = FALSE;
	PEAPOL_PACKET	pEap;
	PUCHAR			pProto, pAddr = NULL;
	PUCHAR			pSTAKey = NULL;
	UCHAR			ZeroReplay[LEN_KEY_DESC_REPLAY];
	UCHAR			Mic[16], OldMic[16];
	UCHAR			digest[80];
	UCHAR			DlsPTK[80];
	UCHAR			temp[64];
	BOOLEAN			TimerCancelled;
	CIPHER_KEY		PairwiseKey;


	if (! pAd->CommonCfg.bDLSCapable)
		return bSTAKeyFrame;

	if (! INFRA_ON(pAd))
		return bSTAKeyFrame;

	if (! (pHeader->FC.SubType & 0x08))
		return bSTAKeyFrame;

	if (Len < LENGTH_802_11 + 6 + 2 + 2)
		return bSTAKeyFrame;

	pProto	= (PUCHAR)pHeader + LENGTH_802_11 + 2 + 6;	// QOS Control field , 0xAA 0xAA 0xAA 0x00 0x00 0x00
	pAddr	= pHeader->Addr2;

	// L2PAD bit on will pad 2 bytes at LLC
	if (pRxD->L2PAD)
	{
		pProto += 2;
	}

	if (RTMPEqualMemory(EAPOL, pProto, 2) && (pAd->StaCfg.AuthMode >=  Ndis802_11AuthModeWPA))
	{
		pEap = (PEAPOL_PACKET) (pProto + 2);

		DBGPRINT(RT_DEBUG_TRACE,("DLS - Sniff Len=%ld, DataLen=%d, KeyMic=%d, Install=%d, KeyAck=%d, Secure=%d, EKD_DL=%d, Error=%d, Request=%d\n", Len,
			                                                             (LENGTH_802_11 + 6 + 2 + 2 + sizeof(EAPOL_PACKET) - MAX_LEN_OF_RSNIE + 16),
			                                                             pEap->KeyDesc.KeyInfo.KeyMic,
			                                                             pEap->KeyDesc.KeyInfo.Install,
			                                                             pEap->KeyDesc.KeyInfo.KeyAck,
			                                                             pEap->KeyDesc.KeyInfo.Secure,
			                                                             pEap->KeyDesc.KeyInfo.EKD_DL,
			                                                             pEap->KeyDesc.KeyInfo.Error,
			                                                             pEap->KeyDesc.KeyInfo.Request));

		if ((Len >= (LENGTH_802_11 + 6 + 2 + 2 + sizeof(EAPOL_PACKET) - MAX_LEN_OF_RSNIE + 16)) && pEap->KeyDesc.KeyInfo.KeyMic
			&& pEap->KeyDesc.KeyInfo.Install && pEap->KeyDesc.KeyInfo.KeyAck && pEap->KeyDesc.KeyInfo.Secure
			&& pEap->KeyDesc.KeyInfo.EKD_DL && !pEap->KeyDesc.KeyInfo.Error && !pEap->KeyDesc.KeyInfo.Request)
		{
			// First validate replay counter, only accept message with larger replay counter
			// Let equal pass, some AP start with all zero replay counter
			NdisZeroMemory(ZeroReplay, LEN_KEY_DESC_REPLAY);
			if ((RTMPCompareMemory(pEap->KeyDesc.ReplayCounter, pAd->StaCfg.ReplayCounter, LEN_KEY_DESC_REPLAY) != 1) &&
				(RTMPCompareMemory(pEap->KeyDesc.ReplayCounter, ZeroReplay, LEN_KEY_DESC_REPLAY) != 0))
				return bSTAKeyFrame;

			//RTMPMoveMemory(pAd->StaCfg.ReplayCounter, pEap->KeyDesc.ReplayCounter, LEN_KEY_DESC_REPLAY);
			RTMPMoveMemory(pAd->StaCfg.DlsReplayCounter, pEap->KeyDesc.ReplayCounter, LEN_KEY_DESC_REPLAY);
			DBGPRINT(RT_DEBUG_TRACE,("DLS - Sniff replay counter (%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x) Len=%ld, KeyDataLen=%d\n",
				pAd->StaCfg.ReplayCounter[0], pAd->StaCfg.ReplayCounter[1], pAd->StaCfg.ReplayCounter[2],
				pAd->StaCfg.ReplayCounter[3], pAd->StaCfg.ReplayCounter[4],	pAd->StaCfg.ReplayCounter[5],
				pAd->StaCfg.ReplayCounter[6], pAd->StaCfg.ReplayCounter[7], Len, pEap->KeyDesc.KeyData[1]));

			// put these code segment to get the replay counter
			if (pAd->StaCfg.PortSecured == WPA_802_1X_PORT_NOT_SECURED)
				return bSTAKeyFrame;

			// Check MIC value
			// Save the MIC and replace with zero
			// use proprietary PTK
			NdisZeroMemory(temp, 64);
			NdisMoveMemory(temp, "IEEE802.11 WIRELESS ACCESS POINT", 32);
			WpaCountPTK(pAd, temp, temp, pAd->CommonCfg.Bssid, temp, pAd->CurrentAddress, DlsPTK, LEN_PTK);

			NdisMoveMemory(OldMic, pEap->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
			NdisZeroMemory(pEap->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
			if (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)
			{
				// AES
				HMAC_SHA1((PUCHAR) pEap, pEap->Body_Len[1] + 4, DlsPTK, LEN_EAP_MICK, digest);
				NdisMoveMemory(Mic,	digest,	LEN_KEY_DESC_MIC);
			}
			else
			{
				hmac_md5(DlsPTK, LEN_EAP_MICK, (PUCHAR) pEap, pEap->Body_Len[1] + 4, Mic);
			}

			if (!NdisEqualMemory(OldMic, Mic, LEN_KEY_DESC_MIC))
			{
				DBGPRINT(RT_DEBUG_ERROR, ("MIC Different in Msg1 of STAKey handshake! \n"));
				return bSTAKeyFrame;
			}
			else
				DBGPRINT(RT_DEBUG_TRACE, ("MIC VALID in Msg1 of STAKey handshake! \n"));
#if 1
			if ((pEap->KeyDesc.KeyData[0] == 0xDD) && (pEap->KeyDesc.KeyData[2] == 0x00) && (pEap->KeyDesc.KeyData[3] == 0x0C)
				&& (pEap->KeyDesc.KeyData[4] == 0x43) && (pEap->KeyDesc.KeyData[5] == 0x02))
			{
				pAddr			= pEap->KeyDesc.KeyData + 8;		// Tpe(1), Len(1), OUI(3), DataType(1), Reserved(2)
				pSTAKey			= pEap->KeyDesc.KeyData + 14;	// Tpe(1), Len(1), OUI(3), DataType(1), Reserved(2), STAKey_Mac_Addr(6)

				DBGPRINT(RT_DEBUG_TRACE,("DLS - Receive STAKey Message-1 from %02x:%02x:%02x:%02x:%02x:%02x Len=%ld, KeyDataLen=%d\n",
					pAddr[0], pAddr[1], pAddr[2], pAddr[3], pAddr[4], pAddr[5], Len, pEap->KeyDesc.KeyData[1]));

				bSTAKeyFrame = TRUE;
			}
#else
			if ((pEap->KeyDesc.KeyData[0] == 0xDD) && (pEap->KeyDesc.KeyData[2] == 0x00) && (pEap->KeyDesc.KeyData[3] == 0x0F)
				&& (pEap->KeyDesc.KeyData[4] == 0xAC) && (pEap->KeyDesc.KeyData[5] == 0x02))
			{
				pAddr			= pEap->KeyDesc.KeyData + 8;		// Tpe(1), Len(1), OUI(3), DataType(1), Reserved(2)
				pSTAKey			= pEap->KeyDesc.KeyData + 14;	// Tpe(1), Len(1), OUI(3), DataType(1), Reserved(2), STAKey_Mac_Addr(6)

				DBGPRINT(RT_DEBUG_TRACE,("DLS - Receive STAKey Message-1 from %02x:%02x:%02x:%02x:%02x:%02x Len=%d, KeyDataLen=%d\n",
					pAddr[0], pAddr[1], pAddr[2], pAddr[3], pAddr[4], pAddr[5], Len, pEap->KeyDesc.KeyData[1]));

				bSTAKeyFrame = TRUE;
			}
#endif

		}
		else if (Len >= (LENGTH_802_11 + 6 + 2 + 2 + sizeof(EAPOL_PACKET) - MAX_LEN_OF_RSNIE))
		{
			RTMPMoveMemory(pAd->StaCfg.DlsReplayCounter, pEap->KeyDesc.ReplayCounter, LEN_KEY_DESC_REPLAY);
			DBGPRINT(RT_DEBUG_TRACE,("DLS - Sniff replay counter 2(%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x) Len=%ld, KeyDataLen=%d\n",
				pAd->StaCfg.ReplayCounter[0], pAd->StaCfg.ReplayCounter[1], pAd->StaCfg.ReplayCounter[2],
				pAd->StaCfg.ReplayCounter[3], pAd->StaCfg.ReplayCounter[4],	pAd->StaCfg.ReplayCounter[5],
				pAd->StaCfg.ReplayCounter[6], pAd->StaCfg.ReplayCounter[7], Len, pEap->KeyDesc.KeyData[1]));

		}
	}

	// If timeout value is equaled to zero, it means always not be timeout.
	// update local dls table entry
	for (i= 0; i < MAX_NUM_OF_INIT_DLS_ENTRY; i++)
	{
		if (pAd->StaCfg.DLSEntry[i].Valid && MAC_ADDR_EQUAL(pAddr, pAd->StaCfg.DLSEntry[i].MacAddr))
		{
			if (bSTAKeyFrame)
			{
				PMAC_TABLE_ENTRY pEntry;

				// STAKey frame, add pairwise key table
				pAd->StaCfg.DLSEntry[i].Status = DLS_FINISH;
				RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &TimerCancelled);

				PairwiseKey.KeyLen = LEN_TKIP_EK;
				NdisMoveMemory(PairwiseKey.Key, &pSTAKey[0], LEN_TKIP_EK);
				NdisMoveMemory(PairwiseKey.TxMic, &pSTAKey[16], LEN_TKIP_RXMICK);
				NdisMoveMemory(PairwiseKey.RxMic, &pSTAKey[24], LEN_TKIP_TXMICK);

				PairwiseKey.CipherAlg = pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg;

				pEntry = DlsEntryTableLookup(pAd, pAd->StaCfg.DLSEntry[i].MacAddr, TRUE);
				//AsicAddKeyEntry(pAd, (USHORT)(i + 2), BSS0, 0, &PairwiseKey, TRUE, TRUE);	// reserve 0 for multicast, 1 for unicast
				//AsicUpdateRxWCIDTable(pAd, (USHORT)(i + 2), pAddr);
				// Add Pair-wise key to Asic
#ifdef RT2870
//Benson modified for USB interface, avoid in interrupt when write key, 20080724 -->
                                {
                                        RT_ADD_PAIRWISE_KEY_ENTRY KeyInfo;
                                        COPY_MAC_ADDR(KeyInfo.MacAddr,pAd->StaCfg.DLSEntry[i].MacAddr);
                                        KeyInfo.MacTabMatchWCID=pAd->StaCfg.DLSEntry[i].MacTabMatchWCID;
                                        NdisMoveMemory(&KeyInfo.CipherKey,  &PairwiseKey,sizeof(CIPHER_KEY));
                                        RTUSBEnqueueInternalCmd(pAd, RT_CMD_SET_KEY_TABLE, &KeyInfo, sizeof(RT_ADD_PAIRWISE_KEY_ENTRY));
                                }
                                {
                                        PMAC_TABLE_ENTRY pDLSEntry;
                                        pDLSEntry = DlsEntryTableLookup(pAd, pAd->StaCfg.DLSEntry[i].MacAddr, TRUE);
                                        pDLSEntry->PairwiseKey.CipherAlg=PairwiseKey.CipherAlg;
                                        RTUSBEnqueueInternalCmd(pAd, RT_CMD_SET_RX_WCID_TABLE, pDLSEntry, sizeof(MAC_TABLE_ENTRY));
                                }
//Benson modified for USB interface, avoid in interrupt when write key, 20080724 <--
#endif // RT2870 //
				NdisMoveMemory(&pEntry->PairwiseKey, &PairwiseKey, sizeof(CIPHER_KEY));
				DBGPRINT(RT_DEBUG_TRACE,("DLS - Receive STAKey Message-1 (Peer STA MAC Address STAKey) \n"));

				RTMPSendSTAKeyHandShake(pAd, pAd->StaCfg.DLSEntry[i].MacAddr);

				DBGPRINT(RT_DEBUG_TRACE,("DLS - Finish STAKey handshake procedure (Initiator side)\n"));
			}
			else
			{
				// Data frame, update timeout value
				if (pAd->StaCfg.DLSEntry[i].Status == DLS_FINISH)
				{
					pAd->StaCfg.DLSEntry[i].CountDownTimer = pAd->StaCfg.DLSEntry[i].TimeOut;
					//AsicUpdateRxWCIDTable(pAd, (USHORT)(i + 2), pAddr);
				}
			}

			bFindEntry = TRUE;
		}
	}

	// update peer dls table entry
	for (i=MAX_NUM_OF_INIT_DLS_ENTRY; i<MAX_NUM_OF_DLS_ENTRY; i++)
	{
		if (pAd->StaCfg.DLSEntry[i].Valid && MAC_ADDR_EQUAL(pAddr, pAd->StaCfg.DLSEntry[i].MacAddr))
		{
			if (bSTAKeyFrame)
			{
				PMAC_TABLE_ENTRY pEntry = NULL;

				// STAKey frame, add pairwise key table, and send STAkey Msg-2
				pAd->StaCfg.DLSEntry[i].Status = DLS_FINISH;
				RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &TimerCancelled);

				PairwiseKey.KeyLen = LEN_TKIP_EK;
				NdisMoveMemory(PairwiseKey.Key, &pSTAKey[0], LEN_TKIP_EK);
				NdisMoveMemory(PairwiseKey.TxMic, &pSTAKey[16], LEN_TKIP_RXMICK);
				NdisMoveMemory(PairwiseKey.RxMic, &pSTAKey[24], LEN_TKIP_TXMICK);

				PairwiseKey.CipherAlg = pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg;

				pEntry = DlsEntryTableLookup(pAd, pAd->StaCfg.DLSEntry[i].MacAddr, TRUE);
				//AsicAddKeyEntry(pAd, (USHORT)(i + 2), BSS0, 0, &PairwiseKey, TRUE, TRUE);	// reserve 0 for multicast, 1 for unicast
				//AsicUpdateRxWCIDTable(pAd, (USHORT)(i + 2), pAddr);
				// Add Pair-wise key to Asic
#ifdef RT2870
//Benson modified for USB interface, avoid in interrupt when write key, 20080724 -->
                                {
                                        RT_ADD_PAIRWISE_KEY_ENTRY KeyInfo;
                                        COPY_MAC_ADDR(KeyInfo.MacAddr,pAd->StaCfg.DLSEntry[i].MacAddr);
                                        KeyInfo.MacTabMatchWCID=pAd->StaCfg.DLSEntry[i].MacTabMatchWCID;
                                        NdisMoveMemory(&KeyInfo.CipherKey,  &PairwiseKey,sizeof(CIPHER_KEY));
                                        RTUSBEnqueueInternalCmd(pAd, RT_CMD_SET_KEY_TABLE, &KeyInfo, sizeof(RT_ADD_PAIRWISE_KEY_ENTRY));
                                }
                                {
                                        PMAC_TABLE_ENTRY pDLSEntry;
                                        pDLSEntry = DlsEntryTableLookup(pAd, pAd->StaCfg.DLSEntry[i].MacAddr, TRUE);
                                        pDLSEntry->PairwiseKey.CipherAlg=PairwiseKey.CipherAlg;
                                        RTUSBEnqueueInternalCmd(pAd, RT_CMD_SET_RX_WCID_TABLE, pDLSEntry, sizeof(MAC_TABLE_ENTRY));
                                }
//Benson modified for USB interface, avoid in interrupt when write key, 20080724 <--
#endif // RT2870 //
				NdisMoveMemory(&pEntry->PairwiseKey, &PairwiseKey, sizeof(CIPHER_KEY));
				DBGPRINT(RT_DEBUG_TRACE,("DLS - Receive STAKey Message-1 (Initiator STA MAC Address STAKey)\n"));

				// If support WPA or WPA2, start STAKey hand shake,
				// If failed hand shake, just tear down peer DLS
				if (RTMPSendSTAKeyHandShake(pAd, pAddr) != NDIS_STATUS_SUCCESS)
				{
					MLME_DLS_REQ_STRUCT	MlmeDlsReq;
					USHORT				reason = REASON_QOS_CIPHER_NOT_SUPPORT;

					pAd->StaCfg.DLSEntry[i].Valid	= FALSE;
					pAd->StaCfg.DLSEntry[i].Status	= DLS_NONE;
					DlsParmFill(pAd, &MlmeDlsReq, &pAd->StaCfg.DLSEntry[i], reason);
					MlmeEnqueue(pAd, DLS_STATE_MACHINE, MT2_MLME_DLS_TEAR_DOWN, sizeof(MLME_DLS_REQ_STRUCT), &MlmeDlsReq);
				}
				else
				{
					DBGPRINT(RT_DEBUG_TRACE,("DLS - Finish STAKey handshake procedure (Peer side)\n"));
				}
			}
			else
			{
				// Data frame, update timeout value
				if (pAd->StaCfg.DLSEntry[i].Status == DLS_FINISH)
				{
					pAd->StaCfg.DLSEntry[i].CountDownTimer = pAd->StaCfg.DLSEntry[i].TimeOut;
				}
			}

			bFindEntry = TRUE;
		}
	}


	return bSTAKeyFrame;
}

/*
	========================================================================

	Routine	Description:
		Check if the frame can be sent through DLS direct link interface

	Arguments:
		pAd		Pointer	to adapter

	Return Value:
		DLS entry index

	Note:

	========================================================================
*/
INT	RTMPCheckDLSFrame(
	IN	PRTMP_ADAPTER	pAd,
	IN  PUCHAR          pDA)
{
	INT rval = -1;
	INT	i;

	if (!pAd->CommonCfg.bDLSCapable)
		return rval;

	if (!INFRA_ON(pAd))
		return rval;

	do{
		// check local dls table entry
		for (i=0; i<MAX_NUM_OF_INIT_DLS_ENTRY; i++)
		{
			if (pAd->StaCfg.DLSEntry[i].Valid && (pAd->StaCfg.DLSEntry[i].Status == DLS_FINISH) &&
				MAC_ADDR_EQUAL(pDA, pAd->StaCfg.DLSEntry[i].MacAddr))
			{
				rval = i;
				break;
			}
		}

		// check peer dls table entry
		for (i=MAX_NUM_OF_INIT_DLS_ENTRY; i<MAX_NUM_OF_DLS_ENTRY; i++)
		{
			if (pAd->StaCfg.DLSEntry[i].Valid && (pAd->StaCfg.DLSEntry[i].Status == DLS_FINISH) &&
				MAC_ADDR_EQUAL(pDA, pAd->StaCfg.DLSEntry[i].MacAddr))
			{
				rval = i;
				break;
			}
		}
	} while (FALSE);

	return rval;
}

/*
    ==========================================================================
    Description:

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID RTMPSendDLSTearDownFrame(
	IN	PRTMP_ADAPTER	pAd,
	IN  PUCHAR          pDA)
{
	PUCHAR			pOutBuffer = NULL;
	NDIS_STATUS		NStatus;
	HEADER_802_11	DlsTearDownHdr;
	ULONG			FrameLen = 0;
	USHORT			Reason = REASON_QOS_QSTA_LEAVING_QBSS;
	UCHAR			Category = CATEGORY_DLS;
	UCHAR			Action = ACTION_DLS_TEARDOWN;
	UCHAR			i = 0;

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS) ||
		RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))
		return;

	DBGPRINT(RT_DEBUG_TRACE, ("Send DLS TearDown Frame \n"));

	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  //Get an unused nonpaged memory
	if (NStatus != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_ERROR,("ASSOC - RTMPSendDLSTearDownFrame() allocate memory failed \n"));
		return;
	}

	ActHeaderInit(pAd, &DlsTearDownHdr, pAd->CommonCfg.Bssid, pAd->CurrentAddress, pAd->CommonCfg.Bssid);
	MakeOutgoingFrame(pOutBuffer,				&FrameLen,
					sizeof(HEADER_802_11),		&DlsTearDownHdr,
					1,							&Category,
					1,							&Action,
					6,							pDA,
					6,							pAd->CurrentAddress,
					2,							&Reason,
					END_OF_ARGS);

	MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);

	// Remove key in local dls table entry
	for (i = 0; i < MAX_NUM_OF_INIT_DLS_ENTRY; i++)
	{
		if (pAd->StaCfg.DLSEntry[i].Valid && (pAd->StaCfg.DLSEntry[i].Status == DLS_FINISH)
			&& MAC_ADDR_EQUAL(pDA, pAd->StaCfg.DLSEntry[i].MacAddr))
		{
			MacTableDeleteDlsEntry(pAd, pAd->StaCfg.DLSEntry[i].MacTabMatchWCID, pAd->StaCfg.DLSEntry[i].MacAddr);
		}
	}

	// Remove key in peer dls table entry
	for (i=MAX_NUM_OF_INIT_DLS_ENTRY; i<MAX_NUM_OF_DLS_ENTRY; i++)
	{
		if (pAd->StaCfg.DLSEntry[i].Valid && (pAd->StaCfg.DLSEntry[i].Status == DLS_FINISH)
			&& MAC_ADDR_EQUAL(pDA, pAd->StaCfg.DLSEntry[i].MacAddr))
		{
			MacTableDeleteDlsEntry(pAd, pAd->StaCfg.DLSEntry[i].MacTabMatchWCID, pAd->StaCfg.DLSEntry[i].MacAddr);
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, ("Send DLS TearDown Frame and remove key in (i=%d) \n", i));
}

/*
    ==========================================================================
    Description:

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
NDIS_STATUS RTMPSendSTAKeyRequest(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			pDA)
{
	UCHAR				Header802_3[14];
	NDIS_STATUS			NStatus;
	ULONG				FrameLen = 0;
	EAPOL_PACKET		Packet;
	UCHAR				Mic[16];
	UCHAR				digest[80];
	PUCHAR				pOutBuffer = NULL;
	PNDIS_PACKET		pNdisPacket;
	UCHAR				temp[64];
	UCHAR				DlsPTK[80];

	DBGPRINT(RT_DEBUG_TRACE,("DLS - RTMPSendSTAKeyRequest() to %02x:%02x:%02x:%02x:%02x:%02x\n", pDA[0], pDA[1], pDA[2], pDA[3], pDA[4], pDA[5]));

	pAd->Sequence ++;
	MAKE_802_3_HEADER(Header802_3, pAd->CommonCfg.Bssid, pAd->CurrentAddress, EAPOL);

	// Zero message body
	NdisZeroMemory(&Packet, sizeof(Packet));
	Packet.ProVer = EAPOL_VER;
	Packet.ProType    = EAPOLKey;
	Packet.Body_Len[1]  = sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE + 6 + MAC_ADDR_LEN;		// data field contain KDE andPeer MAC address

	// STAKey Message is as EAPOL-Key(1,1,0,0,G/0,0,0, MIC, 0,Peer MAC KDE)
	if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA) || (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPAPSK))
    {
        Packet.KeyDesc.Type = WPA1_KEY_DESC;
    }
    else if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2) || (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK))
    {
        Packet.KeyDesc.Type = WPA2_KEY_DESC;
    }

	// Key descriptor version
	Packet.KeyDesc.KeyInfo.KeyDescVer =
		(((pAd->StaCfg.PairCipher == Ndis802_11Encryption3Enabled) || (pAd->StaCfg.GroupCipher == Ndis802_11Encryption3Enabled)) ? (DESC_TYPE_AES) : (DESC_TYPE_TKIP));

	Packet.KeyDesc.KeyInfo.KeyMic	= 1;
	Packet.KeyDesc.KeyInfo.Secure	= 1;
	Packet.KeyDesc.KeyInfo.Request	= 1;

	Packet.KeyDesc.KeyDataLen[1]	= 12;

	// use our own OUI to distinguish proprietary with standard.
	Packet.KeyDesc.KeyData[0]		= 0xDD;
	Packet.KeyDesc.KeyData[1]		= 0x0A;
	Packet.KeyDesc.KeyData[2]		= 0x00;
	Packet.KeyDesc.KeyData[3]		= 0x0C;
	Packet.KeyDesc.KeyData[4]		= 0x43;
	Packet.KeyDesc.KeyData[5]		= 0x03;
	NdisMoveMemory(&Packet.KeyDesc.KeyData[6], pDA, MAC_ADDR_LEN);

	NdisMoveMemory(Packet.KeyDesc.ReplayCounter, pAd->StaCfg.DlsReplayCounter, LEN_KEY_DESC_REPLAY);

	// Allocate buffer for transmitting message
	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);
	if (NStatus	!= NDIS_STATUS_SUCCESS)
		return NStatus;

	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation
	MakeOutgoingFrame(pOutBuffer,           &FrameLen,
		              Packet.Body_Len[1] + 4,    &Packet,
		              END_OF_ARGS);

	// use proprietary PTK
	NdisZeroMemory(temp, 64);
	NdisMoveMemory(temp, "IEEE802.11 WIRELESS ACCESS POINT", 32);
	WpaCountPTK(pAd, temp, temp, pAd->CommonCfg.Bssid, temp, pAd->CurrentAddress, DlsPTK, LEN_PTK);

	// calculate MIC
	if (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)
	{
		// AES
		NdisZeroMemory(digest,	sizeof(digest));
		HMAC_SHA1(pOutBuffer, FrameLen, DlsPTK, LEN_EAP_MICK, digest);
		NdisMoveMemory(Packet.KeyDesc.KeyMic, digest, LEN_KEY_DESC_MIC);
	}
	else
	{
		NdisZeroMemory(Mic,	sizeof(Mic));
		hmac_md5(DlsPTK, LEN_EAP_MICK, pOutBuffer, FrameLen, Mic);
		NdisMoveMemory(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);
	}

	MakeOutgoingFrame(pOutBuffer,           &FrameLen,
	                  sizeof(Header802_3),	Header802_3,
		              Packet.Body_Len[1] + 4,	&Packet,
		              END_OF_ARGS);

	NStatus = RTMPAllocateNdisPacket(pAd, &pNdisPacket, NULL, 0, pOutBuffer, FrameLen);
	if (NStatus == NDIS_STATUS_SUCCESS)
	{
		RTMP_SET_PACKET_WCID(pNdisPacket, BSSID_WCID);
		STASendPacket(pAd, pNdisPacket);
		RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
	}

	MlmeFreeMemory(pAd, pOutBuffer);

	DBGPRINT(RT_DEBUG_TRACE, ("RTMPSendSTAKeyRequest- Send STAKey request (NStatus=%x, FrameLen=%ld)\n", NStatus, FrameLen));

	return NStatus;
}

/*
    ==========================================================================
    Description:

	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
NDIS_STATUS RTMPSendSTAKeyHandShake(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			pDA)
{
	UCHAR				Header802_3[14];
	NDIS_STATUS			NStatus;
	ULONG				FrameLen = 0;
	EAPOL_PACKET		Packet;
	UCHAR				Mic[16];
	UCHAR				digest[80];
	PUCHAR				pOutBuffer = NULL;
	PNDIS_PACKET		pNdisPacket;
	UCHAR				temp[64];
	UCHAR				DlsPTK[80];			// Due to dirver can not get PTK, use proprietary PTK

	DBGPRINT(RT_DEBUG_TRACE,("DLS - RTMPSendSTAKeyHandShake() to %02x:%02x:%02x:%02x:%02x:%02x\n", pDA[0], pDA[1], pDA[2], pDA[3], pDA[4], pDA[5]));

	pAd->Sequence ++;
	MAKE_802_3_HEADER(Header802_3, pAd->CommonCfg.Bssid, pAd->CurrentAddress, EAPOL);

	// Zero message body
	NdisZeroMemory(&Packet, sizeof(Packet));
	Packet.ProVer = EAPOL_VER;
	Packet.ProType    = EAPOLKey;
	Packet.Body_Len[1]  = sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE + 6 + MAC_ADDR_LEN;		// data field contain KDE and Peer MAC address

	// STAKey Message is as EAPOL-Key(1,1,0,0,G/0,0,0, MIC, 0,Peer MAC KDE)
	if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA) || (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPAPSK))
    {
        Packet.KeyDesc.Type = WPA1_KEY_DESC;
    }
    else if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2) || (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK))
    {
        Packet.KeyDesc.Type = WPA2_KEY_DESC;
    }

	// Key descriptor version
	Packet.KeyDesc.KeyInfo.KeyDescVer =
		(((pAd->StaCfg.PairCipher == Ndis802_11Encryption3Enabled) || (pAd->StaCfg.GroupCipher == Ndis802_11Encryption3Enabled)) ? (DESC_TYPE_AES) : (DESC_TYPE_TKIP));

	Packet.KeyDesc.KeyInfo.KeyMic	= 1;
	Packet.KeyDesc.KeyInfo.Secure	= 1;

	Packet.KeyDesc.KeyDataLen[1]	= 12;

	// use our own OUI to distinguish proprietary with standard.
	Packet.KeyDesc.KeyData[0]		= 0xDD;
	Packet.KeyDesc.KeyData[1]		= 0x0A;
	Packet.KeyDesc.KeyData[2]		= 0x00;
	Packet.KeyDesc.KeyData[3]		= 0x0C;
	Packet.KeyDesc.KeyData[4]		= 0x43;
	Packet.KeyDesc.KeyData[5]		= 0x03;
	NdisMoveMemory(&Packet.KeyDesc.KeyData[6], pDA, MAC_ADDR_LEN);

	NdisMoveMemory(Packet.KeyDesc.ReplayCounter, pAd->StaCfg.DlsReplayCounter, LEN_KEY_DESC_REPLAY);

	// Allocate buffer for transmitting message
	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);
	if (NStatus	!= NDIS_STATUS_SUCCESS)
		return NStatus;

	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation
	MakeOutgoingFrame(pOutBuffer,           &FrameLen,
		              Packet.Body_Len[1] + 4,    &Packet,
		              END_OF_ARGS);

	// use proprietary PTK
	NdisZeroMemory(temp, 64);
	NdisMoveMemory(temp, "IEEE802.11 WIRELESS ACCESS POINT", 32);
	WpaCountPTK(pAd, temp, temp, pAd->CommonCfg.Bssid, temp, pAd->CurrentAddress, DlsPTK, LEN_PTK);

	// calculate MIC
	if (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)
	{
		// AES
		NdisZeroMemory(digest,	sizeof(digest));
		HMAC_SHA1(pOutBuffer, FrameLen, DlsPTK, LEN_EAP_MICK, digest);
		NdisMoveMemory(Packet.KeyDesc.KeyMic, digest, LEN_KEY_DESC_MIC);
	}
	else
	{
		NdisZeroMemory(Mic,	sizeof(Mic));
		hmac_md5(DlsPTK, LEN_EAP_MICK, pOutBuffer, FrameLen, Mic);
		NdisMoveMemory(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);
	}

	MakeOutgoingFrame(pOutBuffer,           &FrameLen,
	                  sizeof(Header802_3),	Header802_3,
		              Packet.Body_Len[1] + 4,	&Packet,
		              END_OF_ARGS);

	NStatus = RTMPAllocateNdisPacket(pAd, &pNdisPacket, NULL, 0, pOutBuffer, FrameLen);
	if (NStatus == NDIS_STATUS_SUCCESS)
	{
		RTMP_SET_PACKET_WCID(pNdisPacket, BSSID_WCID);
		STASendPacket(pAd, pNdisPacket);
		RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
	}

	MlmeFreeMemory(pAd, pOutBuffer);

	DBGPRINT(RT_DEBUG_TRACE, ("RTMPSendSTAKeyHandShake- Send STAKey Message-2 (NStatus=%x, FrameLen=%ld)\n", NStatus, FrameLen));

	return NStatus;
}

VOID DlsTimeoutAction(
	IN PVOID SystemSpecific1,
	IN PVOID FunctionContext,
	IN PVOID SystemSpecific2,
	IN PVOID SystemSpecific3)
{
	MLME_DLS_REQ_STRUCT		MlmeDlsReq;
	USHORT					reason;
	PRT_802_11_DLS			pDLS = (PRT_802_11_DLS)FunctionContext;
	PRTMP_ADAPTER			pAd = pDLS->pAd;

	DBGPRINT(RT_DEBUG_TRACE, ("DlsTimeout - Tear down DLS links (%02x:%02x:%02x:%02x:%02x:%02x)\n",
		pDLS->MacAddr[0], pDLS->MacAddr[1], pDLS->MacAddr[2], pDLS->MacAddr[3], pDLS->MacAddr[4], pDLS->MacAddr[5]));

	if ((pDLS) && (pDLS->Valid))
	{
		reason			= REASON_QOS_REQUEST_TIMEOUT;
		pDLS->Valid		= FALSE;
		pDLS->Status	= DLS_NONE;
		DlsParmFill(pAd, &MlmeDlsReq, pDLS, reason);
		MlmeEnqueue(pAd, DLS_STATE_MACHINE, MT2_MLME_DLS_TEAR_DOWN, sizeof(MLME_DLS_REQ_STRUCT), &MlmeDlsReq);
		RT28XX_MLME_HANDLER(pAd);
	}
}

/*
================================================================
Description : because DLS and CLI share the same WCID table in ASIC.
Mesh entry also insert to pAd->MacTab.content[].  Such is marked as ValidAsDls = TRUE.
Also fills the pairwise key.
Because front MAX_AID_BA entries have direct mapping to BAEntry, which is only used as CLI. So we insert Dls
from index MAX_AID_BA.
================================================================
*/
MAC_TABLE_ENTRY *MacTableInsertDlsEntry(
	IN  PRTMP_ADAPTER   pAd,
	IN  PUCHAR	pAddr,
	IN  UINT	DlsEntryIdx)
{
	PMAC_TABLE_ENTRY pEntry = NULL;

	DBGPRINT(RT_DEBUG_TRACE, ("====> MacTableInsertDlsEntry\n"));
	// if FULL, return
	if (pAd->MacTab.Size >= MAX_LEN_OF_MAC_TABLE)
		return NULL;

	do
	{
		if((pEntry = DlsEntryTableLookup(pAd, pAddr, TRUE)) != NULL)
			break;

		// allocate one MAC entry
		pEntry = MacTableInsertEntry(pAd, pAddr, DlsEntryIdx + MIN_NET_DEVICE_FOR_DLS, TRUE);
		if (pEntry)
		{
			pAd->StaCfg.DLSEntry[DlsEntryIdx].MacTabMatchWCID = pEntry->Aid;
			pEntry->MatchDlsEntryIdx = DlsEntryIdx;
			pEntry->AuthMode = pAd->StaCfg.AuthMode;
			pEntry->WepStatus = pAd->StaCfg.WepStatus;

			DBGPRINT(RT_DEBUG_TRACE, ("MacTableInsertDlsEntry - allocate entry #%d, Total= %d\n",pEntry->Aid, pAd->MacTab.Size));

			// If legacy WEP is used, set pair-wise cipherAlg into WCID attribute table for this entry
			if ((pEntry->ValidAsDls) && (pEntry->WepStatus == Ndis802_11WEPEnabled))
			{
				UCHAR KeyIdx = 0;
				UCHAR CipherAlg = 0;

				KeyIdx	= pAd->StaCfg.DefaultKeyId;

				CipherAlg 	= pAd->SharedKey[BSS0][KeyIdx].CipherAlg;

				RTMPAddWcidAttributeEntry(pAd,
											BSS0,
											pAd->StaCfg.DefaultKeyId,
											pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].CipherAlg,
											pEntry);
			}

			break;
		}
	} while(FALSE);

	DBGPRINT(RT_DEBUG_TRACE, ("<==== MacTableInsertDlsEntry\n"));

	return pEntry;
}


/*
	==========================================================================
	Description:
		Delete all Mesh Entry in pAd->MacTab
	==========================================================================
 */
BOOLEAN MacTableDeleteDlsEntry(
	IN PRTMP_ADAPTER pAd,
	IN USHORT wcid,
	IN PUCHAR pAddr)
{
	DBGPRINT(RT_DEBUG_TRACE, ("====> MacTableDeleteDlsEntry\n"));

	if (!VALID_WCID(wcid))
		return FALSE;

	MacTableDeleteEntry(pAd, wcid, pAddr);

	DBGPRINT(RT_DEBUG_TRACE, ("<==== MacTableDeleteDlsEntry\n"));

	return TRUE;
}

MAC_TABLE_ENTRY *DlsEntryTableLookup(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR	pAddr,
	IN BOOLEAN	bResetIdelCount)
{
	ULONG HashIdx;
	MAC_TABLE_ENTRY *pEntry = NULL;

	RTMP_SEM_LOCK(&pAd->MacTabLock);
	HashIdx = MAC_ADDR_HASH_INDEX(pAddr);
	pEntry = pAd->MacTab.Hash[HashIdx];

	while (pEntry)
	{
		if ((pEntry->ValidAsDls == TRUE)
			&& MAC_ADDR_EQUAL(pEntry->Addr, pAddr))
		{
			if(bResetIdelCount)
				pEntry->NoDataIdleCount = 0;
			break;
		}
		else
			pEntry = pEntry->pNext;
	}

	RTMP_SEM_UNLOCK(&pAd->MacTabLock);
	return pEntry;
}

MAC_TABLE_ENTRY *DlsEntryTableLookupByWcid(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR	wcid,
	IN PUCHAR	pAddr,
	IN BOOLEAN	bResetIdelCount)
{
	ULONG DLsIndex;
	PMAC_TABLE_ENTRY pCurEntry = NULL;
	PMAC_TABLE_ENTRY pEntry = NULL;

	if (!VALID_WCID(wcid))
		return NULL;

	RTMP_SEM_LOCK(&pAd->MacTabLock);

	do
	{
		pCurEntry = &pAd->MacTab.Content[wcid];

		DLsIndex = 0xff;
		if ((pCurEntry) && (pCurEntry->ValidAsDls== TRUE))
		{
			DLsIndex = pCurEntry->MatchDlsEntryIdx;
		}

		if (DLsIndex == 0xff)
			break;

		if (MAC_ADDR_EQUAL(pCurEntry->Addr, pAddr))
		{
			if(bResetIdelCount)
				pCurEntry->NoDataIdleCount = 0;
			pEntry = pCurEntry;
			break;
		}
	} while(FALSE);

	RTMP_SEM_UNLOCK(&pAd->MacTabLock);

	return pEntry;
}

INT Set_DlsEntryInfo_Display_Proc(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR arg)
{
	INT i;

	printk("\n%-19s%-8s\n", "MAC", "TIMEOUT\n");
	for (i=0; i<MAX_NUM_OF_DLS_ENTRY; i++)
	{
		if ((pAd->StaCfg.DLSEntry[i].Valid) && (pAd->StaCfg.DLSEntry[i].Status == DLS_FINISH))
		{
			printk("%02x:%02x:%02x:%02x:%02x:%02x  ",
				pAd->StaCfg.DLSEntry[i].MacAddr[0], pAd->StaCfg.DLSEntry[i].MacAddr[1], pAd->StaCfg.DLSEntry[i].MacAddr[2],
				pAd->StaCfg.DLSEntry[i].MacAddr[3], pAd->StaCfg.DLSEntry[i].MacAddr[4], pAd->StaCfg.DLSEntry[i].MacAddr[5]);
			printk("%-8d\n", pAd->StaCfg.DLSEntry[i].TimeOut);
		}
	}

	return TRUE;
}

INT	Set_DlsAddEntry_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
    UCHAR	mac[MAC_ADDR_LEN];
	USHORT	Timeout;
	char *token, sepValue[] = ":", DASH = '-';
	INT i;
    RT_802_11_DLS	Dls;

    if(strlen(arg) < 19)  //Mac address acceptable format 01:02:03:04:05:06 length 17 plus the "-" and timeout value in decimal format.
		return FALSE;

	token = strchr(arg, DASH);
	if ((token != NULL) && (strlen(token)>1))
	{
		Timeout = simple_strtol((token+1), 0, 10);

		*token = '\0';
		for (i = 0, token = rstrtok(arg, &sepValue[0]); token; token = rstrtok(NULL, &sepValue[0]), i++)
		{
			if((strlen(token) != 2) || (!isxdigit(*token)) || (!isxdigit(*(token+1))))
				return FALSE;
			AtoH(token, (PUCHAR)(&mac[i]), 1);
		}
		if(i != 6)
			return FALSE;

	    printk("\n%02x:%02x:%02x:%02x:%02x:%02x-%d", mac[0], mac[1],
	           mac[2], mac[3], mac[4], mac[5], (int)Timeout);

		NdisZeroMemory(&Dls, sizeof(RT_802_11_DLS));
		Dls.TimeOut = Timeout;
		COPY_MAC_ADDR(Dls.MacAddr, mac);
		Dls.Valid = 1;

		MlmeEnqueue(pAd,
					MLME_CNTL_STATE_MACHINE,
					RT_OID_802_11_SET_DLS_PARAM,
					sizeof(RT_802_11_DLS),
					&Dls);

		return TRUE;
	}

	return FALSE;

}

INT	Set_DlsTearDownEntry_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	UCHAR			macAddr[MAC_ADDR_LEN];
	CHAR			*value;
	INT				i;
	RT_802_11_DLS	Dls;

	if(strlen(arg) != 17)  //Mac address acceptable format 01:02:03:04:05:06 length 17
		return FALSE;

	for (i=0, value = rstrtok(arg,":"); value; value = rstrtok(NULL,":"))
	{
		if((strlen(value) != 2) || (!isxdigit(*value)) || (!isxdigit(*(value+1))) )
			return FALSE;  //Invalid

		AtoH(value, &macAddr[i++], 2);
	}

	printk("\n%02x:%02x:%02x:%02x:%02x:%02x", macAddr[0], macAddr[1],
	           macAddr[2], macAddr[3], macAddr[4], macAddr[5]);

	NdisZeroMemory(&Dls, sizeof(RT_802_11_DLS));
	COPY_MAC_ADDR(Dls.MacAddr, macAddr);
	Dls.Valid = 0;

	MlmeEnqueue(pAd,
				MLME_CNTL_STATE_MACHINE,
				RT_OID_802_11_SET_DLS_PARAM,
				sizeof(RT_802_11_DLS),
				&Dls);

	return TRUE;
}

