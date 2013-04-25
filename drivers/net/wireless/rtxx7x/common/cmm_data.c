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


UCHAR	SNAP_802_1H[] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};
UCHAR	SNAP_BRIDGE_TUNNEL[] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8};
UCHAR	EAPOL[] = {0x88, 0x8e};
UCHAR   TPID[] = {0x81, 0x00}; /* VLAN related */

UCHAR	IPX[] = {0x81, 0x37};
UCHAR	APPLE_TALK[] = {0x80, 0xf3};

UCHAR	 OfdmRateToRxwiMCS[12] = {
	0,  0,	0,  0,
	0,  1,	2,  3,	/* OFDM rate 6,9,12,18 = rxwi mcs 0,1,2,3*/
	4,  5,	6,  7,	/* OFDM rate 24,36,48,54 = rxwi mcs 4,5,6,7*/
};
UCHAR	 RxwiMCSToOfdmRate[12] = {
	RATE_6,  RATE_9,	RATE_12,  RATE_18,
	RATE_24,  RATE_36,	RATE_48,  RATE_54,	/* OFDM rate 6,9,12,18 = rxwi mcs 0,1,2,3*/
	4,  5,	6,  7,	/* OFDM rate 24,36,48,54 = rxwi mcs 4,5,6,7*/
};

UCHAR MapUserPriorityToAccessCategory[8] = {QID_AC_BE, QID_AC_BK, QID_AC_BK, QID_AC_BE, QID_AC_VI, QID_AC_VI, QID_AC_VO, QID_AC_VO};


/*
	========================================================================

	Routine Description:
		API for MLME to transmit management frame to AP (BSS Mode)
	or station (IBSS Mode)
		
	Arguments:
		pAd Pointer to our adapter
		pData		Pointer to the outgoing 802.11 frame
		Length		Size of outgoing management frame
		
	Return Value:
		NDIS_STATUS_FAILURE
		NDIS_STATUS_PENDING
		NDIS_STATUS_SUCCESS

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL
	
	Note:
	
	========================================================================
*/
NDIS_STATUS MiniportMMRequest(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			QueIdx,
	IN	PUCHAR			pData,
	IN	UINT			Length)
{
	PNDIS_PACKET	pPacket;
	NDIS_STATUS  	Status = NDIS_STATUS_SUCCESS;
	ULONG	 		FreeNum;
	UCHAR			rtmpHwHdr[TXINFO_SIZE + TXWI_SIZE]; /*RTMP_HW_HDR_LEN];*/
	BOOLEAN			bUseDataQ = FALSE, FlgDataQForce = FALSE, FlgIsLocked = FALSE;
	int 			retryCnt = 0;

	ASSERT(Length <= MGMT_DMA_BUFFER_SIZE);
	
	if ((QueIdx & MGMT_USE_QUEUE_FLAG) == MGMT_USE_QUEUE_FLAG)
	{
		bUseDataQ = TRUE;
		QueIdx &= (~MGMT_USE_QUEUE_FLAG);
	}


	do
	{
		/* Reset is in progress, stop immediately*/
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS) ||
			 RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST)||
			 !RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_START_UP))
		{
			Status = NDIS_STATUS_FAILURE;
			break;
		}

#ifdef CONFIG_STA_SUPPORT
#ifdef RTMP_MAC_USB

		if(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
		{
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
			if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND))
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */
			{
				RT28xxUsbAsicRadioOn(pAd);
			}
		}


#endif /* RTMP_MAC_USB */
#endif /* CONFIG_STA_SUPPORT */



		/* Check Free priority queue*/
		/* Since we use PBF Queue2 for management frame.  Its corresponding DMA ring should be using TxRing.*/
		{
			FreeNum = GET_MGMTRING_FREENO(pAd);
		}
		
		if ((FreeNum > 0))
		{
			/* We need to reserve space for rtmp hardware header. i.e., TxWI for RT2860 and TxInfo+TxWI for RT2870*/
			NdisZeroMemory(&rtmpHwHdr, (TXINFO_SIZE + TXWI_SIZE));
			Status = RTMPAllocateNdisPacket(pAd, &pPacket, (PUCHAR)&rtmpHwHdr, (TXINFO_SIZE + TXWI_SIZE), pData, Length);
			if (Status != NDIS_STATUS_SUCCESS)
			{
				DBGPRINT(RT_DEBUG_WARN, ("MiniportMMRequest (error:: can't allocate NDIS PACKET)\n"));
				break;
			}

			/*pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_CCK;*/
			/*pAd->CommonCfg.MlmeRate = RATE_2;*/



			Status = MlmeHardTransmit(pAd, QueIdx, pPacket, FlgDataQForce, FlgIsLocked);
			if (Status == NDIS_STATUS_SUCCESS)
				retryCnt = 0;
			else
				RTMPFreeNdisPacket(pAd, pPacket);
		}
		else
		{
			pAd->RalinkCounters.MgmtRingFullCount++;
			DBGPRINT(RT_DEBUG_ERROR, ("Qidx(%d), not enough space in MgmtRing, MgmtRingFullCount=%ld!\n",
										QueIdx, pAd->RalinkCounters.MgmtRingFullCount));
		}
	} while (retryCnt > 0);

	

	return Status;
}




/*
	========================================================================

	Routine Description:
		Copy frame from waiting queue into relative ring buffer and set
	appropriate ASIC register to kick hardware transmit function
	
	Arguments:
		pAd Pointer to our adapter
		pBuffer 	Pointer to	memory of outgoing frame
		Length		Size of outgoing management frame
		
	Return Value:
		NDIS_STATUS_FAILURE
		NDIS_STATUS_PENDING
		NDIS_STATUS_SUCCESS

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL
	
	Note:
	
	========================================================================
*/
NDIS_STATUS MlmeHardTransmit(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			QueIdx,
	IN	PNDIS_PACKET	pPacket,
	IN	BOOLEAN			FlgDataQForce,
	IN	BOOLEAN			FlgIsLocked)
{
	PACKET_INFO 	PacketInfo;
	PUCHAR			pSrcBufVA;
	UINT			SrcBufLen;


	if ((pAd->CommonCfg.RadarDetect.RDMode != RD_NORMAL_MODE)
#ifdef CARRIER_DETECTION_SUPPORT
#endif /* CARRIER_DETECTION_SUPPORT */
		)
	{
		return NDIS_STATUS_FAILURE;
	}

	RTMP_QueryPacketInfo(pPacket, &PacketInfo, &pSrcBufVA, &SrcBufLen);
	if (pSrcBufVA == NULL)
		return NDIS_STATUS_FAILURE;

    {
    		return MlmeHardTransmitMgmtRing(pAd,QueIdx,pPacket);
    }
}


NDIS_STATUS MlmeHardTransmitMgmtRing(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR	QueIdx,
	IN	PNDIS_PACKET	pPacket)
{
	PACKET_INFO 	PacketInfo;
	PUCHAR			pSrcBufVA;
	UINT			SrcBufLen;
	PHEADER_802_11	pHeader_802_11;
	BOOLEAN 		bAckRequired, bInsertTimestamp;
	UCHAR			MlmeRate;
	PTXWI_STRUC 	pFirstTxWI;
	MAC_TABLE_ENTRY	*pMacEntry = NULL;
	UCHAR			PID;

	RTMP_QueryPacketInfo(pPacket, &PacketInfo, &pSrcBufVA, &SrcBufLen);

	/* Make sure MGMT ring resource won't be used by other threads*/
	RTMP_SEM_LOCK(&pAd->MgmtRingLock);
	if (pSrcBufVA == NULL)
	{
		/* The buffer shouldn't be NULL*/
			RTMP_SEM_UNLOCK(&pAd->MgmtRingLock);
		return NDIS_STATUS_FAILURE;
	}

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		/* outgoing frame always wakeup PHY to prevent frame lost*/
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
			AsicForceWakeup(pAd, TRUE);
	}
#endif /* CONFIG_STA_SUPPORT */

	pFirstTxWI = (PTXWI_STRUC)(pSrcBufVA +  TXINFO_SIZE);
	pHeader_802_11 = (PHEADER_802_11) (pSrcBufVA + TXINFO_SIZE + TXWI_SIZE); /*TXWI_SIZE);*/
	
	if (pHeader_802_11->Addr1[0] & 0x01)
	{
		MlmeRate = pAd->CommonCfg.BasicMlmeRate;
	}
	else
	{
		MlmeRate = pAd->CommonCfg.MlmeRate;
	}
	
	/* Verify Mlme rate for a / g bands.*/
	if ((pAd->LatchRfRegs.Channel > 14) && (MlmeRate < RATE_6)) /* 11A band*/
		MlmeRate = RATE_6;

	if ((pHeader_802_11->FC.Type == BTYPE_DATA) &&
		(pHeader_802_11->FC.SubType == SUBTYPE_QOS_NULL))
	{
		pMacEntry = MacTableLookup(pAd, pHeader_802_11->Addr1);
	}

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		/* Fixed W52 with Activity scan issue in ABG_MIXED and ABGN_MIXED mode.*/
		if (pAd->CommonCfg.PhyMode == PHY_11ABG_MIXED
#ifdef DOT11_N_SUPPORT
			|| pAd->CommonCfg.PhyMode == PHY_11ABGN_MIXED
#endif /* DOT11_N_SUPPORT */
		)
		{
			if (pAd->LatchRfRegs.Channel > 14)
				pAd->CommonCfg.MlmeTransmit.field.MODE = 1;
			else
				pAd->CommonCfg.MlmeTransmit.field.MODE = 0;
		}
	}
#endif /* CONFIG_STA_SUPPORT */

	/* Should not be hard code to set PwrMgmt to 0 (PWR_ACTIVE)*/
	/* Snice it's been set to 0 while on MgtMacHeaderInit*/
	/* By the way this will cause frame to be send on PWR_SAVE failed.*/
	
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
		{
			/* We are in scan progress, just let the PwrMgmt bit keep as it orginally should be.*/
		}
		else
#endif /* CONFIG_STA_SUPPORT */
			pHeader_802_11->FC.PwrMgmt = PWR_ACTIVE; /* (pAd->StaCfg.Psm == PWR_SAVE);*/
#ifdef CONFIG_STA_SUPPORT
	}
#endif /* CONFIG_STA_SUPPORT */

#ifdef CONFIG_STA_SUPPORT
	
	/* In WMM-UAPSD, mlme frame should be set psm as power saving but probe request frame*/
	/* Data-Null packets alse pass through MMRequest in RT2860, however, we hope control the psm bit to pass APSD*/
/*	if ((pHeader_802_11->FC.Type != BTYPE_DATA) && (pHeader_802_11->FC.Type != BTYPE_CNTL))*/
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		if ((pHeader_802_11->FC.SubType == SUBTYPE_ACTION) ||
			((pHeader_802_11->FC.Type == BTYPE_DATA) &&
			((pHeader_802_11->FC.SubType == SUBTYPE_QOS_NULL) ||
			(pHeader_802_11->FC.SubType == SUBTYPE_NULL_FUNC))))
		{
			if (pAd->StaCfg.Psm == PWR_SAVE)
				pHeader_802_11->FC.PwrMgmt = PWR_SAVE;
			else if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED) && 
					INFRA_ON(pAd) && 
					RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
			{
				/* We are in scan progress, just let the PwrMgmt bit keep as it orginally should be.			*/
			}
			else
			{
				pHeader_802_11->FC.PwrMgmt = pAd->CommonCfg.bAPSDForcePowerSave;
			}
		}
	}
#endif /* CONFIG_STA_SUPPORT */
	



	if ((pHeader_802_11->FC.Type == BTYPE_DATA) &&
		(pHeader_802_11->FC.SubType == SUBTYPE_QOS_NULL))
	{
		printk("%s:: QoS NULL and PHY = %d.  MCS = %d.\n", __FUNCTION__, pAd->CommonCfg.MlmeTransmit.field.MODE, pAd->CommonCfg.MlmeTransmit.field.MCS);
	}

	bInsertTimestamp = FALSE;
	if (pHeader_802_11->FC.Type == BTYPE_CNTL) /* must be PS-POLL*/
	{
#ifdef CONFIG_STA_SUPPORT
		/*Set PM bit in ps-poll, to fix WLK 1.2  PowerSaveMode_ext failure issue.*/
		if ((pAd->OpMode == OPMODE_STA) && (pHeader_802_11->FC.SubType == SUBTYPE_PS_POLL))
		{
			pHeader_802_11->FC.PwrMgmt = PWR_SAVE;
		}
#endif /* CONFIG_STA_SUPPORT */
		bAckRequired = FALSE;
	}
	else /* BTYPE_MGMT or BTYPE_DATA(must be NULL frame)*/
	{
		if (pHeader_802_11->Addr1[0] & 0x01) /* MULTICAST, BROADCAST*/
		{
			bAckRequired = FALSE;
			pHeader_802_11->Duration = 0;
		}
		else
		{
			bAckRequired = TRUE;
			pHeader_802_11->Duration = RTMPCalcDuration(pAd, MlmeRate, 14);
			if ((pHeader_802_11->FC.SubType == SUBTYPE_PROBE_RSP) && (pHeader_802_11->FC.Type == BTYPE_MGMT))
			{
				bInsertTimestamp = TRUE;
				bAckRequired = FALSE; /* Disable ACK to prevent retry 0x1f for Probe Response*/
			}
			else if ((pHeader_802_11->FC.SubType == SUBTYPE_PROBE_REQ) && (pHeader_802_11->FC.Type == BTYPE_MGMT))
			{
				bAckRequired = FALSE; /* Disable ACK to prevent retry 0x1f for Probe Request*/
			}
		}
	}

	pHeader_802_11->Sequence = pAd->Sequence++;
	if (pAd->Sequence >0xfff)
		pAd->Sequence = 0;

	/* Before radar detection done, mgmt frame can not be sent but probe req*/
	/* Because we need to use probe req to trigger driver to send probe req in passive scan*/
	if ((pHeader_802_11->FC.SubType != SUBTYPE_PROBE_REQ)
		&& (pAd->CommonCfg.bIEEE80211H == 1)
		&& (pAd->CommonCfg.RadarDetect.RDMode != RD_NORMAL_MODE))
	{
		DBGPRINT(RT_DEBUG_ERROR,("MlmeHardTransmit --> radar detect not in normal mode !!!\n"));
/*		if (!IrqState)*/
		RTMP_SEM_UNLOCK(&pAd->MgmtRingLock);
		return (NDIS_STATUS_FAILURE);
	}


#ifdef RT_BIG_ENDIAN
	RTMPFrameEndianChange(pAd, (PUCHAR)pHeader_802_11, DIR_WRITE, FALSE);
#endif

	
	/* fill scatter-and-gather buffer list into TXD. Internally created NDIS PACKET*/
	/* should always has only one physical buffer, and the whole frame size equals*/
	/* to the first scatter buffer size*/
	

	/* Initialize TX Descriptor*/
	/* For inter-frame gap, the number is for this frame and next frame*/
	/* For MLME rate, we will fix as 2Mb to match other vendor's implement*/
/*	pAd->CommonCfg.MlmeTransmit.field.MODE = 1;*/
	
/* management frame doesn't need encryption. so use RESERVED_WCID no matter u are sending to specific wcid or not.*/
	PID = PID_MGMT;


	if (pMacEntry == NULL)
	{
		RTMPWriteTxWI(pAd, pFirstTxWI, FALSE, FALSE, bInsertTimestamp, FALSE, bAckRequired, FALSE,
		0, RESERVED_WCID, (SrcBufLen - TXINFO_SIZE - TXWI_SIZE), PID, 0,  (UCHAR)pAd->CommonCfg.MlmeTransmit.field.MCS, IFS_BACKOFF, FALSE, &pAd->CommonCfg.MlmeTransmit);
	}
	else
	{
		if (
			((pHeader_802_11->FC.Type == BTYPE_DATA) &&
		(pHeader_802_11->FC.SubType == SUBTYPE_QOS_NULL)))
		{
			printk("%s:: Using Low Rate to send QOS NULL!!\n", __FUNCTION__);
			pMacEntry->MaxHTPhyMode.field.MODE = 1;
			pMacEntry->MaxHTPhyMode.field.MCS = MCS_RATE_54;
		}
		/* dont use low rate to send QoS Null data frame */
		RTMPWriteTxWI(pAd, pFirstTxWI, FALSE, FALSE,
					bInsertTimestamp, FALSE, bAckRequired, FALSE,
					0, pMacEntry->Aid, (SrcBufLen - TXINFO_SIZE - TXWI_SIZE),
					pMacEntry->MaxHTPhyMode.field.MCS, 0,
					(UCHAR)pMacEntry->MaxHTPhyMode.field.MCS,
					IFS_BACKOFF, FALSE, &pMacEntry->MaxHTPhyMode);
	}

#ifdef RT_BIG_ENDIAN
	RTMPWIEndianChange((PUCHAR)pFirstTxWI, TYPE_TXWI);
#endif

	/* Now do hardware-depened kick out.*/
	HAL_KickOutMgmtTx(pAd, QueIdx, pPacket, pSrcBufVA, SrcBufLen);

	/* Make sure to release MGMT ring resource*/
/*	if (!IrqState)*/
		RTMP_SEM_UNLOCK(&pAd->MgmtRingLock);
	return NDIS_STATUS_SUCCESS;
}


/********************************************************************************
		
	New DeQueue Procedures.

 ********************************************************************************/

#define DEQUEUE_LOCK(lock, bIntContext, IrqFlags) 				\
			do{													\
				if (bIntContext == FALSE)						\
				RTMP_IRQ_LOCK((lock), IrqFlags);		\
			}while(0)

#define DEQUEUE_UNLOCK(lock, bIntContext, IrqFlags)				\
			do{													\
				if (bIntContext == FALSE)						\
					RTMP_IRQ_UNLOCK((lock), IrqFlags);	\
			}while(0)




/*
	========================================================================
	Tx Path design algorithm:
		Basically, we divide the packets into four types, Broadcast/Multicast, 11N Rate(AMPDU, AMSDU, Normal), B/G Rate(ARALINK, Normal),
		Specific Packet Type. Following show the classification rule and policy for each kinds of packets.
				Classification Rule=>
					Multicast: (*addr1 & 0x01) == 0x01
					Specific : bDHCPFrame, bARPFrame, bEAPOLFrame, etc.
					11N Rate : If peer support HT
								(1).AMPDU  -- If TXBA is negotiated.
								(2).AMSDU  -- If AMSDU is capable for both peer and ourself.
											*). AMSDU can embedded in a AMPDU, but now we didn't support it.
								(3).Normal -- Other packets which send as 11n rate.
								
					B/G Rate : If peer is b/g only.
								(1).ARALINK-- If both of peer/us supprot Ralink proprietary Aggregation and the TxRate is large than RATE_6
								(2).Normal -- Other packets which send as b/g rate.
					Fragment:
								The packet must be unicast, NOT A-RALINK, NOT A-MSDU, NOT 11n, then can consider about fragment.
								
				Classified Packet Handle Rule=>
					Multicast:
								No ACK, 		pTxBlk->bAckRequired = FALSE;
								No WMM, 		pTxBlk->bWMM = FALSE;
								No piggyback,   pTxBlk->bPiggyBack = FALSE;
								Force LowRate,  pTxBlk->bForceLowRate = TRUE;
					Specific :	Basically, for specific packet, we should handle it specifically, but now all specific packets are use
									the same policy to handle it.
								Force LowRate,  pTxBlk->bForceLowRate = TRUE;
								
					11N Rate :
								No piggyback,	pTxBlk->bPiggyBack = FALSE;
								
								(1).AMSDU
									pTxBlk->bWMM = TRUE;
								(2).AMPDU
									pTxBlk->bWMM = TRUE;
								(3).Normal
									
					B/G Rate :
								(1).ARALINK
									
								(2).Normal
	========================================================================
*/
static UCHAR TxPktClassification(
	IN RTMP_ADAPTER *pAd,
	IN PNDIS_PACKET  pPacket)
{
	UCHAR			TxFrameType = TX_UNKOWN_FRAME;
	UCHAR			Wcid;
	MAC_TABLE_ENTRY	*pMacEntry = NULL;
#ifdef DOT11_N_SUPPORT
	BOOLEAN			bHTRate = FALSE;
#endif /* DOT11_N_SUPPORT */

	Wcid = RTMP_GET_PACKET_WCID(pPacket);
	if (Wcid == MCAST_WCID)
	{	/* Handle for RA is Broadcast/Multicast Address.*/
		return TX_MCAST_FRAME;
	}

	/* Handle for unicast packets*/
	pMacEntry = &pAd->MacTab.Content[Wcid];
	if (RTMP_GET_PACKET_LOWRATE(pPacket))
	{	/* It's a specific packet need to force low rate, i.e., bDHCPFrame, bEAPOLFrame, bWAIFrame*/
		TxFrameType = TX_LEGACY_FRAME;
	}
#ifdef DOT11_N_SUPPORT
	else if (IS_HT_RATE(pMacEntry))
	{	/* it's a 11n capable packet*/

		/* Depends on HTPhyMode to check if the peer support the HTRate transmission.*/
		/* 	Currently didn't support A-MSDU embedded in A-MPDU*/
		bHTRate = TRUE;
		if (RTMP_GET_PACKET_MOREDATA(pPacket) || (pMacEntry->PsMode == PWR_SAVE))
			TxFrameType = TX_LEGACY_FRAME;
#ifdef UAPSD_AP_SUPPORT
		else if (RTMP_GET_PACKET_EOSP(pPacket))
			TxFrameType = TX_LEGACY_FRAME;
#endif /* UAPSD_AP_SUPPORT */
		else if((pMacEntry->TXBAbitmap & (1<<(RTMP_GET_PACKET_UP(pPacket)))) != 0)
			return TX_AMPDU_FRAME;
		else if(CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_AMSDU_INUSED))
			return TX_AMSDU_FRAME;
		else
			TxFrameType = TX_LEGACY_FRAME;
	}
#endif /* DOT11_N_SUPPORT */
	else
	{	/* it's a legacy b/g packet.*/
		if ((CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_AGGREGATION_CAPABLE) && pAd->CommonCfg.bAggregationCapable) &&
			(RTMP_GET_PACKET_TXRATE(pPacket) >= RATE_6) &&
			(!(OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) && CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_WMM_CAPABLE))))
		{	/* if peer support Ralink Aggregation, we use it.*/
			TxFrameType = TX_RALINK_FRAME;
		}
		else
		{
			TxFrameType = TX_LEGACY_FRAME;
		}
	}

	/* Currently, our fragment only support when a unicast packet send as NOT-ARALINK, NOT-AMSDU and NOT-AMPDU.*/
	if ((RTMP_GET_PACKET_FRAGMENTS(pPacket) > 1)
		 && (TxFrameType == TX_LEGACY_FRAME)
#ifdef DOT11_N_SUPPORT
		&& ((pMacEntry->TXBAbitmap & (1<<(RTMP_GET_PACKET_UP(pPacket)))) == 0)
#endif /* DOT11_N_SUPPORT */
		)
		TxFrameType = TX_FRAG_FRAME;

	return TxFrameType;
}


BOOLEAN RTMP_FillTxBlkInfo(
	IN RTMP_ADAPTER *pAd,
	IN TX_BLK *pTxBlk)
{
	PACKET_INFO			PacketInfo;
	PNDIS_PACKET		pPacket;
	PMAC_TABLE_ENTRY	pMacEntry = NULL;

	pPacket = pTxBlk->pPacket;
	RTMP_QueryPacketInfo(pPacket, &PacketInfo, &pTxBlk->pSrcBufHeader, &pTxBlk->SrcBufLen);

	pTxBlk->Wcid	 	 		= RTMP_GET_PACKET_WCID(pPacket);
	pTxBlk->apidx		 		= RTMP_GET_PACKET_IF(pPacket);
	pTxBlk->UserPriority 		= RTMP_GET_PACKET_UP(pPacket);
	pTxBlk->FrameGap = IFS_HTTXOP;		/* ASIC determine Frame Gap*/

	if (RTMP_GET_PACKET_CLEAR_EAP_FRAME(pTxBlk->pPacket))
		TX_BLK_SET_FLAG(pTxBlk, fTX_bClearEAPFrame);
	else
		TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bClearEAPFrame);
	
	/* Default to clear this flag*/
	TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bForceNonQoS);
	
	
	if (pTxBlk->Wcid == MCAST_WCID)
	{
		pTxBlk->pMacEntry = NULL;
		{
#ifdef MCAST_RATE_SPECIFIC
			PUCHAR pDA = GET_OS_PKT_DATAPTR(pPacket);
			if (((*pDA & 0x01) == 0x01) && (*pDA != 0xff))
				pTxBlk->pTransmit = &pAd->CommonCfg.MCastPhyMode;
			else
#endif /* MCAST_RATE_SPECIFIC */
				pTxBlk->pTransmit = &pAd->MacTab.Content[MCAST_WCID].HTPhyMode;
		}
		
		TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bAckRequired);	/* AckRequired = FALSE, when broadcast packet in Adhoc mode.*/
		/*TX_BLK_SET_FLAG(pTxBlk, fTX_bForceLowRate);*/
		TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bAllowFrag);
		TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bWMM);
		if (RTMP_GET_PACKET_MOREDATA(pPacket))
		{
			TX_BLK_SET_FLAG(pTxBlk, fTX_bMoreData);
		}
	}
	else
	{
		pTxBlk->pMacEntry = &pAd->MacTab.Content[pTxBlk->Wcid];
		pTxBlk->pTransmit = &pTxBlk->pMacEntry->HTPhyMode;

		pMacEntry = pTxBlk->pMacEntry;
		
		/* For all unicast packets, need Ack unless the Ack Policy is not set as NORMAL_ACK.*/
		if (pAd->CommonCfg.AckPolicy[pTxBlk->QueIdx] != NORMAL_ACK)
			TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bAckRequired);
		else
			TX_BLK_SET_FLAG(pTxBlk, fTX_bAckRequired);

#ifdef CONFIG_STA_SUPPORT
#ifdef XLINK_SUPPORT
		if ((pAd->OpMode == OPMODE_STA) &&
			(ADHOC_ON(pAd)) /*&& 
			(RX_FILTER_TEST_FLAG(pAd, fRX_FILTER_ACCEPT_PROMISCUOUS))*/)
		{
			if(pAd->StaCfg.PSPXlink)
				TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bAckRequired);
		}
#endif /* XLINK_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */

		{

#ifdef CONFIG_STA_SUPPORT
			IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			{


				/* If support WMM, enable it.*/
				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) &&
					CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_WMM_CAPABLE))
					TX_BLK_SET_FLAG(pTxBlk, fTX_bWMM);
		
/*				if (pAd->StaCfg.bAutoTxRateSwitch)*/
/*					TX_BLK_SET_FLAG(pTxBlk, fTX_AutoRateSwitch);*/
			}
#endif /* CONFIG_STA_SUPPORT */
		}

		if (pTxBlk->TxFrameType == TX_LEGACY_FRAME)
		{
			if ( (RTMP_GET_PACKET_LOWRATE(pPacket)) ||
                ((pAd->OpMode == OPMODE_AP) && (pMacEntry->MaxHTPhyMode.field.MODE == MODE_CCK) && (pMacEntry->MaxHTPhyMode.field.MCS == RATE_1)))
			{	/* Specific packet, i.e., bDHCPFrame, bEAPOLFrame, bWAIFrame, need force low rate.*/
				pTxBlk->pTransmit = &pAd->MacTab.Content[MCAST_WCID].HTPhyMode;

#ifdef DOT11_N_SUPPORT
				/* Modify the WMM bit for ICV issue. If we have a packet with EOSP field need to set as 1, how to handle it???*/
				if (IS_HT_STA(pTxBlk->pMacEntry) &&
					(CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_RALINK_CHIPSET)) &&
					((pAd->CommonCfg.bRdg == TRUE) && CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_RDG_CAPABLE)))
				{
					TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bWMM);
					TX_BLK_SET_FLAG(pTxBlk, fTX_bForceNonQoS);
				}
#endif /* DOT11_N_SUPPORT */
			}
			
#ifdef DOT11_N_SUPPORT
			if ( (IS_HT_RATE(pMacEntry) == FALSE) &&
				(CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_PIGGYBACK_CAPABLE)))
			{	/* Currently piggy-back only support when peer is operate in b/g mode.*/
				TX_BLK_SET_FLAG(pTxBlk, fTX_bPiggyBack);
			}
#endif /* DOT11_N_SUPPORT */

			if (RTMP_GET_PACKET_MOREDATA(pPacket))
			{
				TX_BLK_SET_FLAG(pTxBlk, fTX_bMoreData);
			}
#ifdef UAPSD_AP_SUPPORT
			if (RTMP_GET_PACKET_EOSP(pPacket))
			{
				TX_BLK_SET_FLAG(pTxBlk, fTX_bWMM_UAPSD_EOSP);
			}
#endif /* UAPSD_AP_SUPPORT */
		}
		else if (pTxBlk->TxFrameType == TX_FRAG_FRAME)
		{
			TX_BLK_SET_FLAG(pTxBlk, fTX_bAllowFrag);
		}
		
		pMacEntry->DebugTxCount++;
	}

	pAd->LastTxRate = (USHORT)pTxBlk->pTransmit->word;

	return TRUE;
}


BOOLEAN CanDoAggregateTransmit(
	IN RTMP_ADAPTER *pAd,
	IN NDIS_PACKET *pPacket,
	IN TX_BLK		*pTxBlk)
{

	/*DBGPRINT(RT_DEBUG_TRACE, ("Check if can do aggregation! TxFrameType=%d!\n", pTxBlk->TxFrameType));*/
	
	if (RTMP_GET_PACKET_WCID(pPacket) == MCAST_WCID)
		return FALSE;

	if (RTMP_GET_PACKET_DHCP(pPacket) ||
		RTMP_GET_PACKET_EAPOL(pPacket) ||
		RTMP_GET_PACKET_WAI(pPacket))
		return FALSE;
	
	if ((pTxBlk->TxFrameType == TX_AMSDU_FRAME) &&
		((pTxBlk->TotalFrameLen + GET_OS_PKT_LEN(pPacket))> (RX_BUFFER_AGGRESIZE - 100)))
	{	/* For AMSDU, allow the packets with total length < max-amsdu size*/
		return FALSE;
	}
	
	if ((pTxBlk->TxFrameType == TX_RALINK_FRAME) &&
		(pTxBlk->TxPacketList.Number == 2))
	{	/* For RALINK-Aggregation, allow two frames in one batch.*/
		return FALSE;
	}

#ifdef CONFIG_STA_SUPPORT
	if ((INFRA_ON(pAd)) && (pAd->OpMode == OPMODE_STA)) /* must be unicast to AP*/
		return TRUE;
	else
#endif /* CONFIG_STA_SUPPORT */
		return FALSE;
	
}


/*
	========================================================================

	Routine Description:
		To do the enqueue operation and extract the first item of waiting
		list. If a number of available shared memory segments could meet
		the request of extracted item, the extracted item will be fragmented
		into shared memory segments.
	
	Arguments:
		pAd Pointer to our adapter
		pQueue		Pointer to Waiting Queue
		
	Return Value:
		None

	IRQL = DISPATCH_LEVEL
	
	Note:
	
	========================================================================
*/
VOID RTMPDeQueuePacket(
	IN  PRTMP_ADAPTER   pAd,
	IN  BOOLEAN         bIntContext,
	IN  UCHAR			QIdx, /* BulkOutPipeId */
	IN  UCHAR           Max_Tx_Packets)
{
	PQUEUE_ENTRY    pEntry = NULL;
	PNDIS_PACKET 	pPacket;
	NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;
	UCHAR           Count=0;
	PQUEUE_HEADER   pQueue;
	ULONG           FreeNumber[NUM_OF_TX_RING];
	UCHAR			QueIdx, sQIdx, eQIdx;
	unsigned long	IrqFlags = 0;
	BOOLEAN			hasTxDesc = FALSE;
	TX_BLK			TxBlk;
	TX_BLK			*pTxBlk;

#ifdef DBG_DIAGNOSE
	BOOLEAN			firstRound;
	RtmpDiagStruct	*pDiagStruct = &pAd->DiagStruct;
#endif


	if (QIdx == NUM_OF_TX_RING)
	{
		sQIdx = 0;
		eQIdx = 3;	/* 4 ACs, start from 0.*/
	}
	else
	{
		sQIdx = eQIdx = QIdx;
	}

	for (QueIdx=sQIdx; QueIdx <= eQIdx; QueIdx++)
	{
		Count=0;

		RTMP_START_DEQUEUE(pAd, QueIdx, IrqFlags);

#ifdef DBG_DIAGNOSE
		firstRound = ((QueIdx == 0) ? TRUE : FALSE);
#endif /* DBG_DIAGNOSE */

		while (1)
		{
			if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS |
										fRTMP_ADAPTER_RADIO_OFF |
										fRTMP_ADAPTER_RESET_IN_PROGRESS |
										fRTMP_ADAPTER_HALT_IN_PROGRESS |
										fRTMP_ADAPTER_NIC_NOT_EXIST)))
				)
			{
				RTMP_STOP_DEQUEUE(pAd, QueIdx, IrqFlags);
				return;
			}
			
			if (Count >= Max_Tx_Packets)
				break;
			
			DEQUEUE_LOCK(&pAd->irq_lock, bIntContext, IrqFlags);
			if (&pAd->TxSwQueue[QueIdx] == NULL)
			{
#ifdef DBG_DIAGNOSE
				if (firstRound == TRUE)
					pDiagStruct->TxSWQueCnt[pDiagStruct->ArrayCurIdx][0]++;
#endif /* DBG_DIAGNOSE */
				DEQUEUE_UNLOCK(&pAd->irq_lock, bIntContext, IrqFlags);
				break;
			}


			/* probe the Queue Head*/
			pQueue = &pAd->TxSwQueue[QueIdx];
			if ((pEntry = pQueue->Head) == NULL)
			{
				DEQUEUE_UNLOCK(&pAd->irq_lock, bIntContext, IrqFlags);
				break;
			}

			pTxBlk = &TxBlk;
			NdisZeroMemory((PUCHAR)pTxBlk, sizeof(TX_BLK));
			/*InitializeQueueHeader(&pTxBlk->TxPacketList);		 Didn't need it because we already memzero it.*/
			pTxBlk->QueIdx = QueIdx;
	
#ifdef VENDOR_FEATURE1_SUPPORT
			pTxBlk->HeaderBuf = (UCHAR *)pTxBlk->HeaderBuffer;
#endif /* VENDOR_FEATURE1_SUPPORT */

			pPacket = QUEUE_ENTRY_TO_PACKET(pEntry);

			
			/* Early check to make sure we have enoguh Tx Resource.*/
			hasTxDesc = RTMP_HAS_ENOUGH_FREE_DESC(pAd, pTxBlk, FreeNumber[QueIdx], pPacket);
			if (!hasTxDesc)
			{
				pAd->PrivateInfo.TxRingFullCnt++;

				DEQUEUE_UNLOCK(&pAd->irq_lock, bIntContext, IrqFlags);
				
				break;
			}

			pTxBlk->TxFrameType = TxPktClassification(pAd, pPacket);
			pEntry = RemoveHeadQueue(pQueue);
			pTxBlk->TotalFrameNum++;
			pTxBlk->TotalFragNum += RTMP_GET_PACKET_FRAGMENTS(pPacket);	/* The real fragment number maybe vary*/
			pTxBlk->TotalFrameLen += GET_OS_PKT_LEN(pPacket);
			pTxBlk->pPacket = pPacket;
			InsertTailQueue(&pTxBlk->TxPacketList, PACKET_TO_QUEUE_ENTRY(pPacket));

			if (pTxBlk->TxFrameType == TX_RALINK_FRAME || pTxBlk->TxFrameType == TX_AMSDU_FRAME)
			{
				// Enhance SW Aggregation Mechanism
				if (NEED_QUEUE_BACK_FOR_AGG(pAd, QueIdx, FreeNumber[QueIdx], pTxBlk->TxFrameType))
				{
					InsertHeadQueue(pQueue, PACKET_TO_QUEUE_ENTRY(pPacket));
					DEQUEUE_UNLOCK(&pAd->irq_lock, bIntContext, IrqFlags);
					break;
				}
			}


			if (pTxBlk->TxFrameType == TX_RALINK_FRAME || pTxBlk->TxFrameType == TX_AMSDU_FRAME)
			{
				do{
					if((pEntry = pQueue->Head) == NULL)
						break;

					/* For TX_AMSDU_FRAME/TX_RALINK_FRAME, Need to check if next pakcet can do aggregation.*/
					pPacket = QUEUE_ENTRY_TO_PACKET(pEntry);
					FreeNumber[QueIdx] = GET_TXRING_FREENO(pAd, QueIdx);
					hasTxDesc = RTMP_HAS_ENOUGH_FREE_DESC(pAd, pTxBlk, FreeNumber[QueIdx], pPacket);
					if ((hasTxDesc == FALSE) || (CanDoAggregateTransmit(pAd, pPacket, pTxBlk) == FALSE))
						break;

					/*Remove the packet from the TxSwQueue and insert into pTxBlk*/
					pEntry = RemoveHeadQueue(pQueue);
					ASSERT(pEntry);
					pPacket = QUEUE_ENTRY_TO_PACKET(pEntry);


					pTxBlk->TotalFrameNum++;
					pTxBlk->TotalFragNum += RTMP_GET_PACKET_FRAGMENTS(pPacket);	/* The real fragment number maybe vary*/
					pTxBlk->TotalFrameLen += GET_OS_PKT_LEN(pPacket);
					InsertTailQueue(&pTxBlk->TxPacketList, PACKET_TO_QUEUE_ENTRY(pPacket));
				}while(1);

				if (pTxBlk->TxPacketList.Number == 1)
					pTxBlk->TxFrameType = TX_LEGACY_FRAME;
			}

#ifdef RTMP_MAC_USB
			DEQUEUE_UNLOCK(&pAd->irq_lock, bIntContext, IrqFlags);
#endif /* RTMP_MAC_USB */
					
			Count += pTxBlk->TxPacketList.Number;

				/* Do HardTransmit now.*/
#ifdef CONFIG_STA_SUPPORT
			IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			{
				Status = STAHardTransmit(pAd, pTxBlk, QueIdx);
			}
#endif /* CONFIG_STA_SUPPORT */


		}

		RTMP_STOP_DEQUEUE(pAd, QueIdx, IrqFlags);

#ifdef RTMP_MAC_USB
		if (!hasTxDesc)
			RTUSBKickBulkOut(pAd);
#endif /* RTMP_MAC_USB */
		
#ifdef BLOCK_NET_IF
		if ((pAd->blockQueueTab[QueIdx].SwTxQueueBlockFlag == TRUE)
			&& (pAd->TxSwQueue[QueIdx].Number < 1))
		{
			releaseNetIf(&pAd->blockQueueTab[QueIdx]);
		}
#endif /* BLOCK_NET_IF */

	}

}


/*
	========================================================================

	Routine Description:
		Calculates the duration which is required to transmit out frames
	with given size and specified rate.
		
	Arguments:
		pAd 	Pointer to our adapter
		Rate			Transmit rate
		Size			Frame size in units of byte
		
	Return Value:
		Duration number in units of usec

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL
	
	Note:
	
	========================================================================
*/
USHORT	RTMPCalcDuration(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			Rate,
	IN	ULONG			Size)
{
	ULONG	Duration = 0;

	if (Rate < RATE_FIRST_OFDM_RATE) /* CCK*/
	{
		if ((Rate > RATE_1) && OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED))
			Duration = 96;	/* 72+24 preamble+plcp*/
		else
			Duration = 192; /* 144+48 preamble+plcp*/

		Duration += (USHORT)((Size << 4) / RateIdTo500Kbps[Rate]);
		if ((Size << 4) % RateIdTo500Kbps[Rate])
			Duration ++;
	}
	else if (Rate <= RATE_LAST_OFDM_RATE)/* OFDM rates*/
	{
		Duration = 20 + 6;		/* 16+4 preamble+plcp + Signal Extension*/
		Duration += 4 * (USHORT)((11 + Size * 4) / RateIdTo500Kbps[Rate]);
		if ((11 + Size * 4) % RateIdTo500Kbps[Rate])
			Duration += 4;
	}
	else	/*mimo rate*/
	{
		Duration = 20 + 6;		/* 16+4 preamble+plcp + Signal Extension*/
	}
	
	return (USHORT)Duration;
}


/*
	========================================================================
	
	Routine Description:
		Calculates the duration which is required to transmit out frames
	with given size and specified rate.
					  
	Arguments:
		pTxWI		Pointer to head of each MPDU to HW.
		Ack 		Setting for Ack requirement bit
		Fragment	Setting for Fragment bit
		RetryMode	Setting for retry mode
		Ifs 		Setting for IFS gap
		Rate		Setting for transmit rate
		Service 	Setting for service
		Length		Frame length
		TxPreamble	Short or Long preamble when using CCK rates
		QueIdx - 0-3, according to 802.11e/d4.4 June/2003
		
	Return Value:
		None
		
	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL
	
    See also : BASmartHardTransmit()    !!!
	
	========================================================================
*/
VOID RTMPWriteTxWI(
	IN	PRTMP_ADAPTER	pAd,
	IN	PTXWI_STRUC 	pOutTxWI,
	IN	BOOLEAN			FRAG,
	IN	BOOLEAN			CFACK,
	IN	BOOLEAN			InsTimestamp,
	IN	BOOLEAN 		AMPDU,
	IN	BOOLEAN 		Ack,
	IN	BOOLEAN 		NSeq,		/* HW new a sequence.*/
	IN	UCHAR			BASize,
	IN	UCHAR			WCID,
	IN	ULONG			Length,
	IN	UCHAR 			PID,
	IN	UCHAR			TID,
	IN	UCHAR			TxRate,
	IN	UCHAR			Txopmode,
	IN	BOOLEAN			CfAck,
	IN	HTTRANSMIT_SETTING	*pTransmit)
{
	PMAC_TABLE_ENTRY	pMac = NULL;
	TXWI_STRUC 		TxWI;
	PTXWI_STRUC 	pTxWI;

	if (WCID < MAX_LEN_OF_MAC_TABLE)
		pMac = &pAd->MacTab.Content[WCID];

	
	/* Always use Long preamble before verifiation short preamble functionality works well.*/
	/* Todo: remove the following line if short preamble functionality works*/
	
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED);
	NdisZeroMemory(&TxWI, TXWI_SIZE);
	pTxWI = &TxWI;

	pTxWI->FRAG= FRAG;

	pTxWI->CFACK = CFACK;
	pTxWI->TS= InsTimestamp;
	pTxWI->AMPDU = AMPDU;
	pTxWI->ACK = Ack;
	pTxWI->txop= Txopmode;
	
	pTxWI->NSEQ = NSeq;
	/* John tune the performace with Intel Client in 20 MHz performance*/
#ifdef DOT11_N_SUPPORT
	BASize = pAd->CommonCfg.TxBASize;
	if (pAd->MACVersion == 0x28720200)
	{
		if( BASize >13 )
			BASize =13;
	}
	else
	{
		if( BASize >7 )
			BASize =7;
	}

	pTxWI->BAWinSize = BASize;
	pTxWI->ShortGI = pTransmit->field.ShortGI;
	pTxWI->STBC = pTransmit->field.STBC;

#endif /* DOT11_N_SUPPORT */
		
	pTxWI->WirelessCliID = WCID;
	pTxWI->MPDUtotalByteCount = Length;
	pTxWI->PacketId = PID;
	
	/* If CCK or OFDM, BW must be 20*/
	pTxWI->BW = (pTransmit->field.MODE <= MODE_OFDM) ? (BW_20) : (pTransmit->field.BW);
#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
	if (pTxWI->BW)
		pTxWI->BW = (pAd->CommonCfg.AddHTInfo.AddHtInfo.RecomWidth == 0) ? (BW_20) : (pTransmit->field.BW);
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */
	
	/* P2P test case 6.1.12 */
		{
	pTxWI->MCS = pTransmit->field.MCS;
	pTxWI->PHYMODE = pTransmit->field.MODE;
		}
	pTxWI->CFACK = CfAck;

#ifdef DOT11_N_SUPPORT
	if (pMac)
	{
        if (pAd->CommonCfg.bMIMOPSEnable)
        {
    		if ((pMac->MmpsMode == MMPS_DYNAMIC) && (pTransmit->field.MCS > 7))
			{
				/* Dynamic MIMO Power Save Mode*/
				pTxWI->MIMOps = 1;
			}
			else if (pMac->MmpsMode == MMPS_STATIC)
			{
				/* Static MIMO Power Save Mode*/
				if (pTransmit->field.MODE >= MODE_HTMIX && pTransmit->field.MCS > 7)
				{
					pTxWI->MCS = 7;
					pTxWI->MIMOps = 0;
				}
			}
        }
		/*pTxWI->MIMOps = (pMac->PsMode == PWR_MMPS)? 1:0;*/
#ifndef DOT11N_SS3_SUPPORT
		if (pMac->bIAmBadAtheros && (pMac->WepStatus != Ndis802_11WEPDisabled))
		{
			pTxWI->MpduDensity = 7;
		}
		else
#endif /* DOT11N_SS3_SUPPORT */
		{
		pTxWI->MpduDensity = pMac->MpduDensity;
	}
	}
#endif /* DOT11_N_SUPPORT */


	pTxWI->PacketId = pTxWI->MCS;
	NdisMoveMemory(pOutTxWI, &TxWI, sizeof(TXWI_STRUC));
}


VOID RTMPWriteTxWI_Data(
	IN	PRTMP_ADAPTER		pAd,
	IN	OUT PTXWI_STRUC		pTxWI,
	IN	TX_BLK				*pTxBlk)
{
	HTTRANSMIT_SETTING	*pTransmit;
	PMAC_TABLE_ENTRY	pMacEntry;
#ifdef DOT11_N_SUPPORT
	UCHAR				BASize;
#endif /* DOT11_N_SUPPORT */


	ASSERT(pTxWI);

	pTransmit = pTxBlk->pTransmit;
	pMacEntry = pTxBlk->pMacEntry;


	
	/* Always use Long preamble before verifiation short preamble functionality works well.*/
	/* Todo: remove the following line if short preamble functionality works*/
	
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED);
	NdisZeroMemory(pTxWI, TXWI_SIZE);
	
	pTxWI->FRAG		= TX_BLK_TEST_FLAG(pTxBlk, fTX_bAllowFrag);
	pTxWI->ACK		= TX_BLK_TEST_FLAG(pTxBlk, fTX_bAckRequired);
	pTxWI->txop		= pTxBlk->FrameGap;

#ifdef CONFIG_STA_SUPPORT
#ifdef QOS_DLS_SUPPORT
	if (pMacEntry &&
		(pAd->StaCfg.BssType == BSS_INFRA) &&
		IS_ENTRY_DLS(pMacEntry))
		pTxWI->WirelessCliID = BSSID_WCID;
	else
#endif /* QOS_DLS_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */
	pTxWI->WirelessCliID		= pTxBlk->Wcid;

	pTxWI->MPDUtotalByteCount	= pTxBlk->MpduHeaderLen + pTxBlk->SrcBufLen;
	pTxWI->CFACK				= TX_BLK_TEST_FLAG(pTxBlk, fTX_bPiggyBack);

	/* If CCK or OFDM, BW must be 20*/
	pTxWI->BW = (pTransmit->field.MODE <= MODE_OFDM) ? (BW_20) : (pTransmit->field.BW);
#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
	if (pTxWI->BW)
		pTxWI->BW = (pAd->CommonCfg.AddHTInfo.AddHtInfo.RecomWidth == 0) ? (BW_20) : (pTransmit->field.BW);
#endif /* DOT11N_DRAFT3 */
	pTxWI->AMPDU	= ((pTxBlk->TxFrameType == TX_AMPDU_FRAME) ? TRUE : FALSE);

	BASize = pAd->CommonCfg.TxBASize;

	if((pTxBlk->TxFrameType == TX_AMPDU_FRAME) && (pMacEntry))
	{
		UCHAR		RABAOriIdx = 0;	/*The RA's BA Originator table index.*/
					
		RABAOriIdx = pTxBlk->pMacEntry->BAOriWcidArray[pTxBlk->UserPriority];
		BASize = pAd->BATable.BAOriEntry[RABAOriIdx].BAWinSize;
	}


	pTxWI->BAWinSize = BASize;
	pTxWI->ShortGI = pTransmit->field.ShortGI;
	pTxWI->STBC = pTransmit->field.STBC;

#endif /* DOT11_N_SUPPORT */
	
	pTxWI->MCS = pTransmit->field.MCS;
	pTxWI->PHYMODE = pTransmit->field.MODE;



#ifdef DOT11_N_SUPPORT
	if (pMacEntry)
	{
		if ((pMacEntry->MmpsMode == MMPS_DYNAMIC) && (pTransmit->field.MCS > 7))
		{
			/* Dynamic MIMO Power Save Mode*/
			pTxWI->MIMOps = 1;
		}
		else if (pMacEntry->MmpsMode == MMPS_STATIC)
		{
			/* Static MIMO Power Save Mode*/
			if (pTransmit->field.MODE >= MODE_HTMIX && pTransmit->field.MCS > 7)
			{
				pTxWI->MCS = 7;
				pTxWI->MIMOps = 0;
			}
		}
		
#ifndef DOT11N_SS3_SUPPORT
		if (pMacEntry->bIAmBadAtheros && (pMacEntry->WepStatus != Ndis802_11WEPDisabled))
		{
			pTxWI->MpduDensity = 7;
		}
		else
#endif /* DOT11N_SS3_SUPPORT */
		{
		pTxWI->MpduDensity = pMacEntry->MpduDensity;
	}
	}
#endif /* DOT11_N_SUPPORT */
	
	
#ifdef DBG_DIAGNOSE
		if (pTxBlk->QueIdx== 0)
		{
			pAd->DiagStruct.TxDataCnt[pAd->DiagStruct.ArrayCurIdx]++;
			pAd->DiagStruct.TxMcsCnt[pAd->DiagStruct.ArrayCurIdx][pTxWI->MCS]++;
		}
#endif /* DBG_DIAGNOSE */

	/* for rate adapation*/
	pTxWI->PacketId = pTxWI->MCS;
#ifdef INF_AMAZON_SE
/*Iverson patch for WMM A5-T07 ,WirelessStaToWirelessSta do not bulk out aggregate */
	if( RTMP_GET_PACKET_NOBULKOUT(pTxBlk->pPacket))
	{
		if(pTxWI->PHYMODE == MODE_CCK)
		{
			pTxWI->PacketId = 6;
		}
	}	
#endif /* INF_AMAZON_SE */	

}


VOID RTMPWriteTxWI_Cache(
	IN	PRTMP_ADAPTER		pAd,
	IN	OUT PTXWI_STRUC		pTxWI,
	IN	TX_BLK				*pTxBlk)
{
	PHTTRANSMIT_SETTING	/*pTxHTPhyMode,*/ pTransmit;
	PMAC_TABLE_ENTRY	pMacEntry;
#ifdef DOT11_N_SUPPORT
#endif /* DOT11_N_SUPPORT */
	
	
	/* update TXWI*/
	
	pMacEntry = pTxBlk->pMacEntry;
	pTransmit = pTxBlk->pTransmit;
	
	/*if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED))*/
	/*if (RTMPCheckEntryEnableAutoRateSwitch(pAd, pMacEntry))*/
	/*if (TX_BLK_TEST_FLAG(pTxBlk, fTX_AutoRateSwitch))*/
	if (pMacEntry->bAutoTxRateSwitch)
	{
		pTxWI->txop = IFS_HTTXOP;

		/* If CCK or OFDM, BW must be 20*/
		pTxWI->BW = (pTransmit->field.MODE <= MODE_OFDM) ? (BW_20) : (pTransmit->field.BW);
		pTxWI->ShortGI = pTransmit->field.ShortGI;
		pTxWI->STBC = pTransmit->field.STBC;


		pTxWI->MCS = pTransmit->field.MCS;
		pTxWI->PHYMODE = pTransmit->field.MODE;

		/* set PID for TxRateSwitching*/
		pTxWI->PacketId = pTransmit->field.MCS;
	}

#ifdef DOT11_N_SUPPORT
	pTxWI->AMPDU = ((pMacEntry->NoBADataCountDown == 0) ? TRUE: FALSE);
	pTxWI->MIMOps = 0;

#ifdef DOT11N_DRAFT3
	if (pTxWI->BW)
		pTxWI->BW = (pAd->CommonCfg.AddHTInfo.AddHtInfo.RecomWidth == 0) ? (BW_20) : (pTransmit->field.BW);
#endif /* DOT11N_DRAFT3 */

    if (pAd->CommonCfg.bMIMOPSEnable)
    {
		/* MIMO Power Save Mode*/
		if ((pMacEntry->MmpsMode == MMPS_DYNAMIC) && (pTransmit->field.MCS > 7))
		{
			/* Dynamic MIMO Power Save Mode*/
			pTxWI->MIMOps = 1;
		}
		else if (pMacEntry->MmpsMode == MMPS_STATIC)
		{
			/* Static MIMO Power Save Mode*/
			if ((pTransmit->field.MODE >= MODE_HTMIX) && (pTransmit->field.MCS > 7))
			{
				pTxWI->MCS = 7;
				pTxWI->MIMOps = 0;
			}
		}
    }

#endif /* DOT11_N_SUPPORT */

#ifdef DBG_DIAGNOSE
	if (pTxBlk->QueIdx== 0)
	{
		pAd->DiagStruct.TxDataCnt[pAd->DiagStruct.ArrayCurIdx]++;
		pAd->DiagStruct.TxMcsCnt[pAd->DiagStruct.ArrayCurIdx][pTxWI->MCS]++;
	}
#endif /* DBG_DIAGNOSE */


	pTxWI->MPDUtotalByteCount = pTxBlk->MpduHeaderLen + pTxBlk->SrcBufLen;

	
}


/*
	========================================================================

	Routine Description:
		Suspend MSDU transmission
		
	Arguments:
		pAd 	Pointer to our adapter
		
	Return Value:
		None
		
	Note:
	
	========================================================================
*/
VOID	RTMPSuspendMsduTransmission(
	IN	PRTMP_ADAPTER	pAd)
{
	DBGPRINT(RT_DEBUG_TRACE,("SCANNING, suspend MSDU transmission ...\n"));


	
	/* Before BSS_SCAN_IN_PROGRESS, we need to keep Current R66 value and*/
	/* use Lowbound as R66 value on ScanNextChannel(...)*/
	
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R66, &pAd->BbpTuning.R66CurrentValue);
	
	/* set BBP_R66 to 0x30/0x40 when scanning (AsicSwitchChannel will set R66 according to channel when scanning)*/
	/*RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, (0x26 + GET_LNA_GAIN(pAd)));*/
	RTMPSetAGCInitValue(pAd, BW_20);
	
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS);
	/*RTMP_IO_WRITE32(pAd, TX_CNTL_CSR, 0x000f0000);		 abort all TX rings*/
}


/*
	========================================================================

	Routine Description:
		Resume MSDU transmission
		
	Arguments:
		pAd 	Pointer to our adapter
		
	Return Value:
		None
		
	IRQL = DISPATCH_LEVEL
	
	Note:
	
	========================================================================
*/
VOID RTMPResumeMsduTransmission(
	IN	PRTMP_ADAPTER	pAd)
{
/*    UCHAR			IrqState;*/
    
	DBGPRINT(RT_DEBUG_TRACE,("SCAN done, resume MSDU transmission ...\n"));


	/* After finish BSS_SCAN_IN_PROGRESS, we need to restore Current R66 value*/
	/* R66 should not be 0*/
	if (pAd->BbpTuning.R66CurrentValue == 0)
	{
		pAd->BbpTuning.R66CurrentValue = 0x38;
		DBGPRINT_ERR(("RTMPResumeMsduTransmission, R66CurrentValue=0...\n"));
	}

	RTMP_CHIP_MSDU_TRANSMISSION_RESUME(pAd);


	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS);
/* sample, for IRQ LOCK to SEM LOCK*/
/*    IrqState = pAd->irq_disabled;*/
/*	if (IrqState)*/
/*		RTMPDeQueuePacket(pAd, TRUE, NUM_OF_TX_RING, MAX_TX_PROCESS);*/
/*    else*/
	RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
}


#ifdef DOT11_N_SUPPORT
UINT deaggregate_AMSDU_announce(
	IN	PRTMP_ADAPTER	pAd,
	PNDIS_PACKET		pPacket,
	IN	PUCHAR			pData,
	IN	ULONG			DataSize,
	IN	UCHAR			OpMode)
{
	USHORT 			PayloadSize;
	USHORT 			SubFrameSize;
	PHEADER_802_3 	pAMSDUsubheader;
	UINT			nMSDU;
    UCHAR			Header802_3[14];

	PUCHAR			pPayload, pDA, pSA, pRemovedLLCSNAP;
	PNDIS_PACKET	pClonePacket;


	nMSDU = 0;

	while (DataSize > LENGTH_802_3)
	{

		nMSDU++;

		/*hex_dump("subheader", pData, 64);*/
		pAMSDUsubheader = (PHEADER_802_3)pData;
		/*pData += LENGTH_802_3;*/
		PayloadSize = pAMSDUsubheader->Octet[1] + (pAMSDUsubheader->Octet[0]<<8);
		SubFrameSize = PayloadSize + LENGTH_802_3;


		if ((DataSize < SubFrameSize) || (PayloadSize > 1518 ))
		{
			break;
		}

		/*DBGPRINT(RT_DEBUG_TRACE,("%d subframe: Size = %d\n",  nMSDU, PayloadSize));*/

		pPayload = pData + LENGTH_802_3;
		pDA = pData;
		pSA = pData + MAC_ADDR_LEN;

		/* convert to 802.3 header*/
        CONVERT_TO_802_3(Header802_3, pDA, pSA, pPayload, PayloadSize, pRemovedLLCSNAP);

#ifdef CONFIG_STA_SUPPORT
		if ((Header802_3[12] == 0x88) && (Header802_3[13] == 0x8E)
			)
		{
			/* avoid local heap overflow, use dyanamic allocation */
			MLME_QUEUE_ELEM *Elem; /* = (MLME_QUEUE_ELEM *) kmalloc(sizeof(MLME_QUEUE_ELEM), MEM_ALLOC_FLAG);*/

			os_alloc_mem(pAd, (UCHAR **)&Elem, sizeof(MLME_QUEUE_ELEM));
			if (Elem != NULL)
			{
				memmove(Elem->Msg+(LENGTH_802_11 + LENGTH_802_1_H), pPayload, PayloadSize);
				Elem->MsgLen = LENGTH_802_11 + LENGTH_802_1_H + PayloadSize;
				/*WpaEAPOLKeyAction(pAd, Elem);*/
				REPORT_MGMT_FRAME_TO_MLME(pAd, BSSID_WCID, Elem->Msg, Elem->MsgLen, 0, 0, 0, 0, OPMODE_STA);
/*				kfree(Elem);*/
				os_free_mem(NULL, Elem);
			}
		}
#endif /* CONFIG_STA_SUPPORT */

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
	        	if (pRemovedLLCSNAP)
	        	{
	    			pPayload -= LENGTH_802_3;
	    			PayloadSize += LENGTH_802_3;
	    			NdisMoveMemory(pPayload, &Header802_3[0], LENGTH_802_3);
	        	}
		}
#endif /* CONFIG_STA_SUPPORT */

		pClonePacket = ClonePacket(pAd, pPacket, pPayload, PayloadSize);
		if (pClonePacket)
		{
#ifdef CONFIG_STA_SUPPORT
			IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
				ANNOUNCE_OR_FORWARD_802_3_PACKET(pAd, pClonePacket, RTMP_GET_PACKET_IF(pPacket));
#endif /* CONFIG_STA_SUPPORT */
		}


		/* A-MSDU has padding to multiple of 4 including subframe header.*/
		/* align SubFrameSize up to multiple of 4*/
		SubFrameSize = (SubFrameSize+3)&(~0x3);


		if (SubFrameSize > 1528 || SubFrameSize < 32)
		{
			break;
		}

		if (DataSize > SubFrameSize)
		{
			pData += SubFrameSize;
			DataSize -= SubFrameSize;
		}
		else
		{
			/* end of A-MSDU*/
			DataSize = 0;
		}
	}
	
	/* finally release original rx packet*/
	RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);

	return nMSDU;
}


UINT BA_Reorder_AMSDU_Annnounce(
	IN	PRTMP_ADAPTER	pAd,
	IN	PNDIS_PACKET	pPacket,
	IN	UCHAR			OpMode)
{
	PUCHAR			pData;
	USHORT			DataSize;
	UINT			nMSDU = 0;

	pData = (PUCHAR) GET_OS_PKT_DATAPTR(pPacket);
	DataSize = (USHORT) GET_OS_PKT_LEN(pPacket);

	nMSDU = deaggregate_AMSDU_announce(pAd, pPacket, pData, DataSize, OpMode);

	return nMSDU;
}

VOID Indicate_AMSDU_Packet(
	IN	PRTMP_ADAPTER	pAd,
	IN	RX_BLK			*pRxBlk,
	IN	UCHAR			FromWhichBSSID)
{
	UINT			nMSDU;

	RTMP_UPDATE_OS_PACKET_INFO(pAd, pRxBlk, FromWhichBSSID);
	RTMP_SET_PACKET_IF(pRxBlk->pRxPacket, FromWhichBSSID);
	nMSDU = deaggregate_AMSDU_announce(pAd, pRxBlk->pRxPacket, pRxBlk->pData, pRxBlk->DataSize, pRxBlk->OpMode);
}
#endif /* DOT11_N_SUPPORT */

/*
	==========================================================================
	Description:
		Look up the MAC address in the MAC table. Return NULL if not found.
	Return:
		pEntry - pointer to the MAC entry; NULL is not found
	==========================================================================
*/
MAC_TABLE_ENTRY *MacTableLookup(
	IN PRTMP_ADAPTER pAd,
	PUCHAR pAddr)
{
	ULONG HashIdx;
	MAC_TABLE_ENTRY *pEntry = NULL;
	
	HashIdx = MAC_ADDR_HASH_INDEX(pAddr);
	pEntry = pAd->MacTab.Hash[HashIdx];

	while (pEntry && !IS_ENTRY_NONE(pEntry))
	{
		if (MAC_ADDR_EQUAL(pEntry->Addr, pAddr))
		{
			break;
		}
		else
			pEntry = pEntry->pNext;
	}

	return pEntry;
}

MAC_TABLE_ENTRY *MacTableInsertEntry(
	IN  PRTMP_ADAPTER   pAd,
	IN  PUCHAR			pAddr,
	IN	UCHAR			apidx,
	IN	UCHAR			OpMode,
	IN BOOLEAN	CleanAll)
{
	UCHAR HashIdx;
	int i, FirstWcid;
	MAC_TABLE_ENTRY *pEntry = NULL, *pCurrEntry;
/*	USHORT	offset;*/
/*	ULONG	addr;*/
	BOOLEAN Cancelled;

	/* if FULL, return*/
	if (pAd->MacTab.Size >= MAX_LEN_OF_MAC_TABLE)
		return NULL;

	FirstWcid = 1;
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	if (pAd->StaCfg.BssType == BSS_INFRA)
		FirstWcid = 2;
#endif /* CONFIG_STA_SUPPORT */

	/* allocate one MAC entry*/
	NdisAcquireSpinLock(&pAd->MacTabLock);
	for (i = FirstWcid; i< MAX_LEN_OF_MAC_TABLE; i++)   /* skip entry#0 so that "entry index == AID" for fast lookup*/
	{
		/* pick up the first available vacancy*/
		if (IS_ENTRY_NONE(&pAd->MacTab.Content[i]))
		{
			pEntry = &pAd->MacTab.Content[i];

			/* ENTRY PREEMPTION: initialize the entry */
			RTMPCancelTimer(&pEntry->RetryTimer, &Cancelled);
			RTMPCancelTimer(&pEntry->EnqueueStartForPSKTimer, &Cancelled);

			NdisZeroMemory(pEntry, sizeof(MAC_TABLE_ENTRY));

			if (CleanAll == TRUE)
			{
				pEntry->MaxSupportedRate = RATE_11;
				pEntry->CurrTxRate = RATE_11;
				NdisZeroMemory(pEntry, sizeof(MAC_TABLE_ENTRY));
				pEntry->PairwiseKey.KeyLen = 0;
				pEntry->PairwiseKey.CipherAlg = CIPHER_NONE;
			}

			do
			{
#ifdef CONFIG_STA_SUPPORT
#ifdef QOS_DLS_SUPPORT
			if (apidx >= MIN_NET_DEVICE_FOR_DLS)
			{
				SET_ENTRY_DLS(pEntry);
				pEntry->isCached = FALSE;
				break;
			}
			else
#endif /* QOS_DLS_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */



						SET_ENTRY_CLIENT(pEntry);

		} while (FALSE);

			pEntry->bIAmBadAtheros = FALSE;

			RTMPInitTimer(pAd, &pEntry->EnqueueStartForPSKTimer, GET_TIMER_FUNCTION(EnqueueStartForPSKExec), pEntry, FALSE);

#ifdef CONFIG_STA_SUPPORT
#endif /* CONFIG_STA_SUPPORT */


			pEntry->pAd = pAd;
			pEntry->CMTimerRunning = FALSE;
			pEntry->EnqueueEapolStartTimerRunning = EAPOL_START_DISABLE;
			pEntry->RSNIE_Len = 0;
			NdisZeroMemory(pEntry->R_Counter, sizeof(pEntry->R_Counter));
			pEntry->ReTryCounter = PEER_MSG1_RETRY_TIMER_CTR;

			if (IS_ENTRY_MESH(pEntry))
				pEntry->apidx = (apidx - MIN_NET_DEVICE_FOR_MESH);
			else if (IS_ENTRY_APCLI(pEntry))
				pEntry->apidx = (apidx - MIN_NET_DEVICE_FOR_APCLI);
			else if (IS_ENTRY_WDS(pEntry))
				pEntry->apidx = (apidx - MIN_NET_DEVICE_FOR_WDS);
#ifdef CONFIG_STA_SUPPORT
#ifdef QOS_DLS_SUPPORT
			else if (IS_ENTRY_DLS(pEntry))
				pEntry->apidx = (apidx - MIN_NET_DEVICE_FOR_DLS);
#endif /* QOS_DLS_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */
			else
				pEntry->apidx = apidx;


			do
			{


#ifdef CONFIG_STA_SUPPORT
				IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
				{
					pEntry->AuthMode = pAd->StaCfg.AuthMode;
					pEntry->WepStatus = pAd->StaCfg.WepStatus;
					pEntry->PrivacyFilter = Ndis802_11PrivFilterAcceptAll;
				}
#endif /* CONFIG_STA_SUPPORT */
			} while (FALSE);

			pEntry->GTKState = REKEY_NEGOTIATING;
			pEntry->PairwiseKey.KeyLen = 0;
			pEntry->PairwiseKey.CipherAlg = CIPHER_NONE;
#ifdef CONFIG_STA_SUPPORT
#ifdef QOS_DLS_SUPPORT
			if (IS_ENTRY_DLS(pEntry))
				pEntry->PortSecured = WPA_802_1X_PORT_SECURED;
			else
#endif /* QOS_DLS_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */
				pEntry->PortSecured = WPA_802_1X_PORT_NOT_SECURED;

			pEntry->PMKID_CacheIdx = ENTRY_NOT_FOUND;
			COPY_MAC_ADDR(pEntry->Addr, pAddr);
			COPY_MAC_ADDR(pEntry->HdrAddr1, pAddr);

			do
			{
#ifdef APCLI_SUPPORT
				if (IS_ENTRY_APCLI(pEntry))
				{
					COPY_MAC_ADDR(pEntry->HdrAddr2, pAd->ApCfg.ApCliTab[pEntry->apidx].CurrentAddress);
					COPY_MAC_ADDR(pEntry->HdrAddr3, pAddr);
					break;
				}
#endif // APCLI_SUPPORT //
#ifdef CONFIG_STA_SUPPORT
				if (OpMode == OPMODE_STA)
				{
					COPY_MAC_ADDR(pEntry->HdrAddr2, pAd->CurrentAddress);
					COPY_MAC_ADDR(pEntry->HdrAddr3, pAddr);
					break;
				}
#endif // CONFIG_STA_SUPPORT //
			} while (FALSE);

			pEntry->Sst = SST_NOT_AUTH;
			pEntry->AuthState = AS_NOT_AUTH;
			pEntry->Aid = (USHORT)i;  /*0;*/
			pEntry->CapabilityInfo = 0;
			pEntry->PsMode = PWR_ACTIVE;
			pEntry->PsQIdleCount = 0;
			pEntry->NoDataIdleCount = 0;
			pEntry->AssocDeadLine = MAC_TABLE_ASSOC_TIMEOUT;
			pEntry->ContinueTxFailCnt = 0;
			pEntry->TimeStamp_toTxRing = 0;
			InitializeQueueHeader(&pEntry->PsQueue);



			pAd->MacTab.Size ++;


			/* Set the security mode of this entry as OPEN-NONE in ASIC */
			RTMP_REMOVE_PAIRWISE_KEY_ENTRY(pAd, (UCHAR)i);

			/* Add this entry into ASIC RX WCID search table */
			RTMP_STA_ENTRY_ADD(pAd, pEntry);





			DBGPRINT(RT_DEBUG_TRACE, ("MacTableInsertEntry - allocate entry #%d, Total= %d\n",i, pAd->MacTab.Size));
			break;
		}
	}

	/* add this MAC entry into HASH table*/
	if (pEntry)
	{
		HashIdx = MAC_ADDR_HASH_INDEX(pAddr);
		if (pAd->MacTab.Hash[HashIdx] == NULL)
		{
			pAd->MacTab.Hash[HashIdx] = pEntry;
		}
		else
		{
			pCurrEntry = pAd->MacTab.Hash[HashIdx];
			while (pCurrEntry->pNext != NULL)
				pCurrEntry = pCurrEntry->pNext;
			pCurrEntry->pNext = pEntry;
		}
	}

	NdisReleaseSpinLock(&pAd->MacTabLock);
	return pEntry;
}

/*
	==========================================================================
	Description:
		Delete a specified client from MAC table
	==========================================================================
 */
BOOLEAN MacTableDeleteEntry(
	IN PRTMP_ADAPTER pAd,
	IN USHORT wcid,
	IN PUCHAR pAddr)
{
	USHORT HashIdx;
	MAC_TABLE_ENTRY *pEntry, *pPrevEntry, *pProbeEntry;
	BOOLEAN Cancelled;
	/*USHORT	offset;	 unused variable*/
	/*UCHAR	j;			 unused variable*/

	if (wcid >= MAX_LEN_OF_MAC_TABLE)
		return FALSE;

	NdisAcquireSpinLock(&pAd->MacTabLock);

	HashIdx = MAC_ADDR_HASH_INDEX(pAddr);
	/*pEntry = pAd->MacTab.Hash[HashIdx];*/
	pEntry = &pAd->MacTab.Content[wcid];

	if (pEntry && !IS_ENTRY_NONE(pEntry))
	{
		/* ENTRY PREEMPTION: Cancel all timers */
		RTMPCancelTimer(&pEntry->RetryTimer, &Cancelled);
		RTMPCancelTimer(&pEntry->EnqueueStartForPSKTimer, &Cancelled);

		if (MAC_ADDR_EQUAL(pEntry->Addr, pAddr))
		{

			/* Delete this entry from ASIC on-chip WCID Table*/
			RTMP_STA_ENTRY_MAC_RESET(pAd, wcid);

#ifdef DOT11_N_SUPPORT
			/* free resources of BA*/
			BASessionTearDownALL(pAd, pEntry->Aid);
#endif /* DOT11_N_SUPPORT */

#ifdef CONFIG_STA_SUPPORT
#endif /* CONFIG_STA_SUPPORT */

           
			pPrevEntry = NULL;
			pProbeEntry = pAd->MacTab.Hash[HashIdx];
			ASSERT(pProbeEntry);
			if (pProbeEntry != NULL)
			{
				/* update Hash list*/
				do
				{
					if (pProbeEntry == pEntry)
					{
						if (pPrevEntry == NULL)
						{
							pAd->MacTab.Hash[HashIdx] = pEntry->pNext;
						}
						else
						{
							pPrevEntry->pNext = pEntry->pNext;
						}
						break;
					}

					pPrevEntry = pProbeEntry;
					pProbeEntry = pProbeEntry->pNext;
				} while (pProbeEntry);
			}

			/* not found !!!*/
			ASSERT(pProbeEntry != NULL);

			/*RTMP_REMOVE_PAIRWISE_KEY_ENTRY(pAd, wcid);*/


		if (pEntry->EnqueueEapolStartTimerRunning != EAPOL_START_DISABLE)
		{
            RTMPCancelTimer(&pEntry->EnqueueStartForPSKTimer, &Cancelled);
			pEntry->EnqueueEapolStartTimerRunning = EAPOL_START_DISABLE;
        }
		RTMPReleaseTimer(&pEntry->EnqueueStartForPSKTimer, &Cancelled);



//   			NdisZeroMemory(pEntry, sizeof(MAC_TABLE_ENTRY));
			/* invalidate the entry */
			SET_ENTRY_NONE(pEntry);
			pAd->MacTab.Size --;
			DBGPRINT(RT_DEBUG_TRACE, ("MacTableDeleteEntry1 - Total= %d\n", pAd->MacTab.Size));
		}
		else
		{
			DBGPRINT(RT_DEBUG_OFF, ("\n%s: Impossible Wcid = %d !!!!!\n", __FUNCTION__, wcid));
		}
	}

	NdisReleaseSpinLock(&pAd->MacTabLock);

	/*Reset operating mode when no Sta.*/
	if (pAd->MacTab.Size == 0)
	{
#ifdef DOT11_N_SUPPORT
		pAd->CommonCfg.AddHTInfo.AddHtInfo2.OperaionMode = 0;
#endif /* DOT11_N_SUPPORT */
		RTMP_UPDATE_PROTECT(pAd);  /* edit by johnli, fix "in_interrupt" error when call "MacTableDeleteEntry" in Rx tasklet*/
	}

	return TRUE;
}


/*
	==========================================================================
	Description:
		This routine reset the entire MAC table. All packets pending in
		the power-saving queues are freed here.
	==========================================================================
 */
VOID MacTableReset(
	IN  PRTMP_ADAPTER  pAd)
{
	int         i;
	BOOLEAN     Cancelled;    

	DBGPRINT(RT_DEBUG_TRACE, ("MacTableReset\n"));
	/*NdisAcquireSpinLock(&pAd->MacTabLock);*/


	for (i=1; i<MAX_LEN_OF_MAC_TABLE; i++)
	{
		if (IS_ENTRY_CLIENT(&pAd->MacTab.Content[i]))
	   {
	   		/* Delete a entry via WCID */

			/*MacTableDeleteEntry(pAd, i, pAd->MacTab.Content[i].Addr);*/
			RTMPReleaseTimer(&pAd->MacTab.Content[i].EnqueueStartForPSKTimer, &Cancelled);
#ifdef CONFIG_STA_SUPPORT
#endif /* CONFIG_STA_SUPPORT */
            pAd->MacTab.Content[i].EnqueueEapolStartTimerRunning = EAPOL_START_DISABLE;


			/* Delete a entry via WCID */
			MacTableDeleteEntry(pAd, i, pAd->MacTab.Content[i].Addr);
		}
		else
		{
			/* Delete a entry via WCID */
			MacTableDeleteEntry(pAd, i, pAd->MacTab.Content[i].Addr);
		}
	}

	return;
}

/*
	==========================================================================
	Description:

	IRQL = DISPATCH_LEVEL
	
	==========================================================================
*/
VOID AssocParmFill(
	IN PRTMP_ADAPTER pAd,
	IN OUT MLME_ASSOC_REQ_STRUCT *AssocReq,
	IN PUCHAR                     pAddr,
	IN USHORT                     CapabilityInfo,
	IN ULONG                      Timeout,
	IN USHORT                     ListenIntv)
{
	COPY_MAC_ADDR(AssocReq->Addr, pAddr);
	/* Add mask to support 802.11b mode only*/
	AssocReq->CapabilityInfo = CapabilityInfo & SUPPORTED_CAPABILITY_INFO; /* not cf-pollable, not cf-poll-request*/
	AssocReq->Timeout = Timeout;
	AssocReq->ListenIntv = ListenIntv;
}


/*
	==========================================================================
	Description:

	IRQL = DISPATCH_LEVEL
	
	==========================================================================
*/
VOID DisassocParmFill(
	IN PRTMP_ADAPTER pAd,
	IN OUT MLME_DISASSOC_REQ_STRUCT *DisassocReq,
	IN PUCHAR pAddr,
	IN USHORT Reason)
{
	COPY_MAC_ADDR(DisassocReq->Addr, pAddr);
	DisassocReq->Reason = Reason;
}


BOOLEAN RTMPCheckEtherType(
	IN	PRTMP_ADAPTER	pAd,
	IN	PNDIS_PACKET	pPacket,
	IN	PMAC_TABLE_ENTRY pMacEntry,
	IN	UCHAR			OpMode,
	OUT PUCHAR pUserPriority,
	OUT PUCHAR pQueIdx)
{
	USHORT	TypeLen;
	UCHAR	Byte0, Byte1;
	PUCHAR	pSrcBuf;
	UINT32	pktLen;
	UINT16 	srcPort, dstPort;
	BOOLEAN bWmmReq;


	/*
		for bc/mc packets, if it has VLAN tag or DSCP field, we also need
		to get UP for IGMP use.
	*/
	bWmmReq = (
				OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED))
				&& ((pMacEntry) &&
					((VALID_WCID(pMacEntry->Aid) && CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_WMM_CAPABLE))
						|| (pMacEntry->Aid == MCAST_WCID)));

	pSrcBuf = GET_OS_PKT_DATAPTR(pPacket);
	pktLen = GET_OS_PKT_LEN(pPacket);

	ASSERT(pSrcBuf);

	RTMP_SET_PACKET_SPECIFIC(pPacket, 0);
	
	/* get Ethernet protocol field*/
	TypeLen = (pSrcBuf[12] << 8) | pSrcBuf[13];

	pSrcBuf += LENGTH_802_3;	/* Skip the Ethernet Header.*/
	
	if (TypeLen <= 1500)
	{	/* 802.3, 802.3 LLC*/
		/*
			DestMAC(6) + SrcMAC(6) + Lenght(2) +
			DSAP(1) + SSAP(1) + Control(1) +
			if the DSAP = 0xAA, SSAP=0xAA, Contorl = 0x03, it has a 5-bytes SNAP header.
				=> + SNAP (5, OriginationID(3) + etherType(2))
		*/
		if (pSrcBuf[0] == 0xAA && pSrcBuf[1] == 0xAA && pSrcBuf[2] == 0x03)
		{
			Sniff2BytesFromNdisBuffer((PNDIS_BUFFER)pSrcBuf, 6, &Byte0, &Byte1);
			RTMP_SET_PACKET_LLCSNAP(pPacket, 1);
			TypeLen = (USHORT)((Byte0 << 8) + Byte1);
			pSrcBuf += 8; /* Skip this LLC/SNAP header*/
		}
		else
		{
			/*It just has 3-byte LLC header, maybe a legacy ether type frame. we didn't handle it.*/
		}
	}
	
	/* If it's a VLAN packet, get the real Type/Length field.*/
	if (TypeLen == 0x8100)
	{

		RTMP_SET_PACKET_VLAN(pPacket, 1);
		Sniff2BytesFromNdisBuffer((PNDIS_BUFFER)pSrcBuf, 2, &Byte0, &Byte1);
		TypeLen = (USHORT)((Byte0 << 8) + Byte1);

		/* only use VLAN tag */
		if (bWmmReq)
		{
			*pUserPriority = (*pSrcBuf & 0xe0) >> 5;
			*pQueIdx = MapUserPriorityToAccessCategory[*pUserPriority];
		}

		pSrcBuf += 4; /* Skip the VLAN Header.*/
	}
	else if (TypeLen == 0x0800)
	{
		if (bWmmReq)
		{
			if ((*pSrcBuf & 0xf0) == 0x40) /* IPv4 */
			{
				/*
					Version - 4-bit Internet Protocol version number.
					Length - 4-bit IP header length.
					Traffic Class - 8-bit TOS field.
				*/
				*pUserPriority = (*(pSrcBuf + 1) & 0xe0) >> 5;
			}

			*pQueIdx = MapUserPriorityToAccessCategory[*pUserPriority];
		}
	}
	else if (TypeLen == 0x86dd)
	{
		if (bWmmReq)
		{
			if ((*pSrcBuf & 0xf0) == 0x60) /* IPv6 */
			{
				/*
					Version - 4-bit Internet Protocol version number.
					Traffic Class - 8-bit traffic class field.
				*/
				*pUserPriority = ((*pSrcBuf) & 0x0e) >> 1;
			}

			*pQueIdx = MapUserPriorityToAccessCategory[*pUserPriority];
		}
	}

	switch (TypeLen)
	{
		case 0x0800:
			{
				/* return AC_BE if packet is not IPv4*/
				if (bWmmReq && (*pSrcBuf & 0xf0) != 0x40)
				{
					*pUserPriority = 0;
					*pQueIdx = QID_AC_BE;
				}
				else
					RTMP_SET_PACKET_IPV4(pPacket, 1);

				ASSERT((pktLen > 34));
				if (*(pSrcBuf + 9) == 0x11)
				{	/* udp packet*/
					ASSERT((pktLen > 34));	/* 14 for ethernet header, 20 for IP header*/
					
					pSrcBuf += 20;	/* Skip the IP header*/
					srcPort = OS_NTOHS(get_unaligned((PUINT16)(pSrcBuf)));
					dstPort = OS_NTOHS(get_unaligned((PUINT16)(pSrcBuf+2)));
		
					if ((srcPort==0x44 && dstPort==0x43) || (srcPort==0x43 && dstPort==0x44))
					{	/*It's a BOOTP/DHCP packet*/
						RTMP_SET_PACKET_DHCP(pPacket, 1);
					}
				}
			}
			break;
		case 0x86dd:
			{
				/* return AC_BE if packet is not IPv6 */
				if (bWmmReq &&
					((*pSrcBuf & 0xf0) != 0x60))
				{
					*pUserPriority = 0;
					*pQueIdx = QID_AC_BE;
				}
			}
			break;
		case 0x0806:
			{
				/*ARP Packet.*/
				RTMP_SET_PACKET_DHCP(pPacket, 1);
			}
			break;
		case 0x888e:
			{
				/* EAPOL Packet.*/
				RTMP_SET_PACKET_EAPOL(pPacket, 1);
			}
			break;
		default:
			break;
	}

#ifdef VENDOR_FEATURE1_SUPPORT
	RTMP_SET_PACKET_PROTOCOL(pPacket, TypeLen);
#endif /* VENDOR_FEATURE1_SUPPORT */

	/* have to check ACM bit. downgrade UP & QueIdx before passing ACM*/
	/* NOTE: AP doesn't have to negotiate TSPEC. ACM is controlled purely via user setup, not protocol handshaking*/
	/*
		Under WMM ACM control, we dont need to check the bit;
		Or when a TSPEC is built for VO but we will change priority to
		BE here and when we issue a BA session, the BA session will
		be BE session, not VO session.
	*/
	if (pAd->CommonCfg.APEdcaParm.bACM[*pQueIdx])
	{
		*pUserPriority = 0;
		*pQueIdx		 = QID_AC_BE;
	}


	return TRUE;
	
}


VOID Update_Rssi_Sample(
	IN PRTMP_ADAPTER pAd,
	IN RSSI_SAMPLE  *pRssi,
	IN PRXWI_STRUC  pRxWI)
{
	CHAR rssi0 = pRxWI->RSSI0;
	CHAR rssi1 = pRxWI->RSSI1;
	CHAR rssi2 = pRxWI->RSSI2;
	UCHAR snr0 = pRxWI->SNR0;
	UCHAR snr1 = pRxWI->SNR1;
	CHAR Phymode = pRxWI->PHYMODE;
	BOOLEAN bInitial = FALSE;
 
	if (!(pRssi->AvgRssi0 | pRssi->AvgRssi0X8 | pRssi->LastRssi0))
	{
		bInitial = TRUE;
	}
 
	if (rssi0 != 0)
	{
 
		pRssi->LastRssi0 = ConvertToRssi(pAd, (CHAR)rssi0, RSSI_0);
 
		if (bInitial)
		{
			pRssi->AvgRssi0X8 = pRssi->LastRssi0 << 3;
			pRssi->AvgRssi0  = pRssi->LastRssi0;
		}
		else
		{
			pRssi->AvgRssi0X8 = (pRssi->AvgRssi0X8 - pRssi->AvgRssi0) + pRssi->LastRssi0;
		}
 
		pRssi->AvgRssi0 = pRssi->AvgRssi0X8 >> 3;
	}

	if (snr0 != 0 && Phymode != MODE_CCK)
	{			
		pRssi->LastSnr0 = ConvertToSnr(pAd, (UCHAR)snr0); 			
					
		if (bInitial)
		{
			pRssi->AvgSnr0X8 = pRssi->LastSnr0 << 3;
			pRssi->AvgSnr0  = pRssi->LastSnr0;
		}
		else
		{
			pRssi->AvgSnr0X8 = (pRssi->AvgSnr0X8 - pRssi->AvgSnr0) + pRssi->LastSnr0;
		}			

		pRssi->AvgSnr0 = pRssi->AvgSnr0X8 >> 3;
		/*pRssi->LastNoiseLevel0 = pRssi->AvgRssi0 - pRssi->AvgSnr0;*/
	}
 
	if (rssi1 != 0)
	{   
		pRssi->LastRssi1 = ConvertToRssi(pAd, (CHAR)rssi1, RSSI_1);

		if (bInitial)
		{
			pRssi->AvgRssi1X8 = pRssi->LastRssi1 << 3;
			pRssi->AvgRssi1  = pRssi->LastRssi1;
		}
		else
		{
			pRssi->AvgRssi1X8 = (pRssi->AvgRssi1X8 - pRssi->AvgRssi1) + pRssi->LastRssi1;
		}

		pRssi->AvgRssi1 = pRssi->AvgRssi1X8 >> 3;
	}

	if (snr1 != 0 && Phymode != MODE_CCK)
	{			
		pRssi->LastSnr1 = ConvertToSnr(pAd, (UCHAR)snr1);
					
		if (bInitial)
		{
			pRssi->AvgSnr1X8 = pRssi->LastSnr1 << 3;
			pRssi->AvgSnr1  = pRssi->LastSnr1;
		}
		else
		{
			pRssi->AvgSnr1X8 = (pRssi->AvgSnr1X8 - pRssi->AvgSnr1) + pRssi->LastSnr1;
		}			

		pRssi->AvgSnr1 = pRssi->AvgSnr1X8 >> 3;
		/*pRssi->LastNoiseLevel1 = pRssi->AvgRssi1 - pRssi->AvgSnr1;*/
	}

	if (rssi2 != 0)
	{
		pRssi->LastRssi2 = ConvertToRssi(pAd, (CHAR)rssi2, RSSI_2);

		if (bInitial)
		{
			pRssi->AvgRssi2X8 = pRssi->LastRssi2 << 3;
			pRssi->AvgRssi2  = pRssi->LastRssi2;
		}
		else
		{
			pRssi->AvgRssi2X8 = (pRssi->AvgRssi2X8 - pRssi->AvgRssi2) + pRssi->LastRssi2;
		}

		pRssi->AvgRssi2 = pRssi->AvgRssi2X8 >> 3;
	}
}



/* Normal legacy Rx packet indication*/
VOID Indicate_Legacy_Packet(
	IN	PRTMP_ADAPTER	pAd,
	IN	RX_BLK			*pRxBlk,
	IN	UCHAR			FromWhichBSSID)
{
	PNDIS_PACKET	pRxPacket = pRxBlk->pRxPacket;
	UCHAR			Header802_3[LENGTH_802_3];
	USHORT			VLAN_VID = 0, VLAN_Priority = 0;

	/* 1. get 802.3 Header*/
	/* 2. remove LLC*/
	/* 		a. pointer pRxBlk->pData to payload*/
	/*      b. modify pRxBlk->DataSize*/
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		RTMP_802_11_REMOVE_LLC_AND_CONVERT_TO_802_3(pRxBlk, Header802_3);
#endif /* CONFIG_STA_SUPPORT */

	if (pRxBlk->DataSize > MAX_RX_PKT_LEN)
	{

		/* release packet*/
		RELEASE_NDIS_PACKET(pAd, pRxPacket, NDIS_STATUS_FAILURE);
		return;
	}


	STATS_INC_RX_PACKETS(pAd, FromWhichBSSID);

#ifdef RTMP_MAC_USB
#ifdef DOT11_N_SUPPORT
	if (pAd->CommonCfg.bDisableReordering == 0)
	{
		PBA_REC_ENTRY		pBAEntry;
		ULONG				Now32;
		UCHAR				Wcid = pRxBlk->pRxWI->WirelessCliID;
		UCHAR				TID = pRxBlk->pRxWI->TID;
		USHORT				Idx;
		
#define REORDERING_PACKET_TIMEOUT		((100 * OS_HZ)/1000)	/* system ticks -- 100 ms*/

		if (Wcid < MAX_LEN_OF_MAC_TABLE)
		{
			Idx = pAd->MacTab.Content[Wcid].BARecWcidArray[TID];
			if (Idx != 0)
			{
				pBAEntry = &pAd->BATable.BARecEntry[Idx];
				/* update last rx time*/
				NdisGetSystemUpTime(&Now32);
				if ((pBAEntry->list.qlen > 0) &&
					 RTMP_TIME_AFTER((unsigned long)Now32, (unsigned long)(pBAEntry->LastIndSeqAtTimer+(REORDERING_PACKET_TIMEOUT)))
	   				)
				{
					DBGPRINT(RT_DEBUG_OFF, ("Indicate_Legacy_Packet():flush reordering_timeout_mpdus! RxWI->Flags=%d, pRxWI.TID=%d, RxD->AMPDU=%d!\n", 
												pRxBlk->Flags, pRxBlk->pRxWI->TID, pRxBlk->RxD.AMPDU));
					hex_dump("Dump the legacy Packet:", GET_OS_PKT_DATAPTR(pRxBlk->pRxPacket), 64);
					ba_flush_reordering_timeout_mpdus(pAd, pBAEntry, Now32);
				}
			}
		}
	}
#endif /* DOT11_N_SUPPORT */
#endif /* RTMP_MAC_USB */


	RT_80211_TO_8023_PACKET(pAd, VLAN_VID, VLAN_Priority,
							pRxBlk, Header802_3, FromWhichBSSID, TPID);
	
	/* pass this 802.3 packet to upper layer or forward this packet to WM directly*/
	
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		ANNOUNCE_OR_FORWARD_802_3_PACKET(pAd, pRxPacket, FromWhichBSSID);
#endif /* CONFIG_STA_SUPPORT */

}


/* Normal, AMPDU or AMSDU*/
VOID CmmRxnonRalinkFrameIndicate(
	IN	PRTMP_ADAPTER	pAd,
	IN	RX_BLK			*pRxBlk,
	IN	UCHAR			FromWhichBSSID)
{
#ifdef DOT11_N_SUPPORT
	if (RX_BLK_TEST_FLAG(pRxBlk, fRX_AMPDU) && (pAd->CommonCfg.bDisableReordering == 0))
	{
		Indicate_AMPDU_Packet(pAd, pRxBlk, FromWhichBSSID);
	}
	else
#endif /* DOT11_N_SUPPORT */
	{
#ifdef DOT11_N_SUPPORT
		if (RX_BLK_TEST_FLAG(pRxBlk, fRX_AMSDU))
		{
			/* handle A-MSDU*/
			Indicate_AMSDU_Packet(pAd, pRxBlk, FromWhichBSSID);
		}
		else
#endif /* DOT11_N_SUPPORT */
		{
			Indicate_Legacy_Packet(pAd, pRxBlk, FromWhichBSSID);
		}
	}
}


VOID CmmRxRalinkFrameIndicate(
	IN	PRTMP_ADAPTER	pAd,
	IN	MAC_TABLE_ENTRY	*pEntry,
	IN	RX_BLK			*pRxBlk,
	IN	UCHAR			FromWhichBSSID)
{
	UCHAR			Header802_3[LENGTH_802_3];
	UINT16			Msdu2Size;
	UINT16 			Payload1Size, Payload2Size;
	PUCHAR 			pData2;
	PNDIS_PACKET	pPacket2 = NULL;
	USHORT			VLAN_VID = 0, VLAN_Priority = 0;


	Msdu2Size = *(pRxBlk->pData) + (*(pRxBlk->pData+1) << 8);

	if ((Msdu2Size <= 1536) && (Msdu2Size < pRxBlk->DataSize))
	{
		/* skip two byte MSDU2 len */
		pRxBlk->pData += 2;
		pRxBlk->DataSize -= 2;
	}
	else
	{
		/* release packet*/
		RELEASE_NDIS_PACKET(pAd, pRxBlk->pRxPacket, NDIS_STATUS_FAILURE);
		return;
	}

	/* get 802.3 Header and  remove LLC*/
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		RTMP_802_11_REMOVE_LLC_AND_CONVERT_TO_802_3(pRxBlk, Header802_3);
#endif /* CONFIG_STA_SUPPORT */


	ASSERT(pRxBlk->pRxPacket);

	/* Ralink Aggregation frame*/
	pAd->RalinkCounters.OneSecRxAggregationCount ++;
	Payload1Size = pRxBlk->DataSize - Msdu2Size;
	Payload2Size = Msdu2Size - LENGTH_802_3;

	pData2 = pRxBlk->pData + Payload1Size + LENGTH_802_3;
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		pPacket2 = duplicate_pkt(get_netdev_from_bssid(pAd, FromWhichBSSID),
								(pData2-LENGTH_802_3), LENGTH_802_3, pData2,
								Payload2Size, FromWhichBSSID);
#endif /* CONFIG_STA_SUPPORT */

	if (!pPacket2)
	{
		/* release packet*/
		RELEASE_NDIS_PACKET(pAd, pRxBlk->pRxPacket, NDIS_STATUS_FAILURE);
		return;
	}

	/* update payload size of 1st packet*/
	pRxBlk->DataSize = Payload1Size;
	RT_80211_TO_8023_PACKET(pAd, VLAN_VID, VLAN_Priority,
							pRxBlk, Header802_3, FromWhichBSSID, TPID);

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		ANNOUNCE_OR_FORWARD_802_3_PACKET(pAd, pRxBlk->pRxPacket, FromWhichBSSID);
#endif /* CONFIG_STA_SUPPORT */

	if (pPacket2)
	{
#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			ANNOUNCE_OR_FORWARD_802_3_PACKET(pAd, pPacket2, FromWhichBSSID);
#endif /* CONFIG_STA_SUPPORT */
	}
}


#define RESET_FRAGFRAME(_fragFrame) \
	{								\
		_fragFrame.RxSize = 0;		\
		_fragFrame.Sequence = 0;	\
		_fragFrame.LastFrag = 0;	\
		_fragFrame.Flags = 0;		\
	}


PNDIS_PACKET RTMPDeFragmentDataFrame(
	IN	PRTMP_ADAPTER	pAd,
	IN	RX_BLK			*pRxBlk)
{
	PHEADER_802_11	pHeader = pRxBlk->pHeader;
	PNDIS_PACKET	pRxPacket = pRxBlk->pRxPacket;
	UCHAR			*pData = pRxBlk->pData;
	USHORT			DataSize = pRxBlk->DataSize;
	PNDIS_PACKET	pRetPacket = NULL;
	UCHAR			*pFragBuffer = NULL;
	BOOLEAN 		bReassDone = FALSE;
	UCHAR			HeaderRoom = 0;


	ASSERT(pHeader);

	HeaderRoom = pData - (UCHAR *)pHeader;

	/* Re-assemble the fragmented packets*/
	if (pHeader->Frag == 0)		/* Frag. Number is 0 : First frag or only one pkt*/
	{
		/* the first pkt of fragment, record it.*/
		if (pHeader->FC.MoreFrag)
		{
			ASSERT(pAd->FragFrame.pFragPacket);
			pFragBuffer = GET_OS_PKT_DATAPTR(pAd->FragFrame.pFragPacket);
			pAd->FragFrame.RxSize   = DataSize + HeaderRoom;
			NdisMoveMemory(pFragBuffer,	 pHeader, pAd->FragFrame.RxSize);
			pAd->FragFrame.Sequence = pHeader->Sequence;
			pAd->FragFrame.LastFrag = pHeader->Frag;	   /* Should be 0*/
			ASSERT(pAd->FragFrame.LastFrag == 0);
			goto done;	/* end of processing this frame*/
		}
	}
	else	/*Middle & End of fragment*/
	{
		if ((pHeader->Sequence != pAd->FragFrame.Sequence) ||
			(pHeader->Frag != (pAd->FragFrame.LastFrag + 1)))
		{
			/* Fragment is not the same sequence or out of fragment number order*/
			/* Reset Fragment control blk*/
			RESET_FRAGFRAME(pAd->FragFrame);
			DBGPRINT(RT_DEBUG_ERROR, ("Fragment is not the same sequence or out of fragment number order.\n"));
			goto done; /* give up this frame*/
		}
		else if ((pAd->FragFrame.RxSize + DataSize) > MAX_FRAME_SIZE)
		{
			/* Fragment frame is too large, it exeeds the maximum frame size.*/
			/* Reset Fragment control blk*/
			RESET_FRAGFRAME(pAd->FragFrame);
			DBGPRINT(RT_DEBUG_ERROR, ("Fragment frame is too large, it exeeds the maximum frame size.\n"));
			goto done; /* give up this frame*/
		}

        
		/* Broadcom AP(BCM94704AGR) will send out LLC in fragment's packet, LLC only can accpet at first fragment.*/
		/* In this case, we will dropt it.*/
		
		if (NdisEqualMemory(pData, SNAP_802_1H, sizeof(SNAP_802_1H)))
		{
			DBGPRINT(RT_DEBUG_ERROR, ("Find another LLC at Middle or End fragment(SN=%d, Frag=%d)\n", pHeader->Sequence, pHeader->Frag));
			goto done; /* give up this frame*/
		}

		pFragBuffer = GET_OS_PKT_DATAPTR(pAd->FragFrame.pFragPacket);

		/* concatenate this fragment into the re-assembly buffer*/
		NdisMoveMemory((pFragBuffer + pAd->FragFrame.RxSize), pData, DataSize);
		pAd->FragFrame.RxSize  += DataSize;
		pAd->FragFrame.LastFrag = pHeader->Frag;	   /* Update fragment number*/

		/* Last fragment*/
		if (pHeader->FC.MoreFrag == FALSE)
		{
			bReassDone = TRUE;
		}
	}

done:
	/* always release rx fragmented packet*/
	RELEASE_NDIS_PACKET(pAd, pRxPacket, NDIS_STATUS_FAILURE);

	/* return defragmented packet if packet is reassembled completely*/
	/* otherwise return NULL*/
	if (bReassDone)
	{
		PNDIS_PACKET pNewFragPacket;

		/* allocate a new packet buffer for fragment*/
		pNewFragPacket = RTMP_AllocateFragPacketBuffer(pAd, RX_BUFFER_NORMSIZE);
		if (pNewFragPacket)
		{
			/* update RxBlk*/
			pRetPacket = pAd->FragFrame.pFragPacket;
			pAd->FragFrame.pFragPacket = pNewFragPacket;
			pRxBlk->pHeader = (PHEADER_802_11) GET_OS_PKT_DATAPTR(pRetPacket);
			pRxBlk->pData = (UCHAR *)pRxBlk->pHeader + HeaderRoom;
			pRxBlk->DataSize = pAd->FragFrame.RxSize - HeaderRoom;
			pRxBlk->pRxPacket = pRetPacket;
		}
		else
		{
			RESET_FRAGFRAME(pAd->FragFrame);
		}
	}

	return pRetPacket;
}

VOID Indicate_EAPOL_Packet(
	IN	PRTMP_ADAPTER	pAd,
	IN	RX_BLK			*pRxBlk,
	IN	UCHAR			FromWhichBSSID)
{
	MAC_TABLE_ENTRY *pEntry = NULL;

	
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		{
		pEntry = &pAd->MacTab.Content[BSSID_WCID];
		STARxEAPOLFrameIndicate(pAd, pEntry, pRxBlk, FromWhichBSSID);
		return;
	}
	}
#endif /* CONFIG_STA_SUPPORT */

	if (pEntry == NULL)
	{
		DBGPRINT(RT_DEBUG_WARN, ("Indicate_EAPOL_Packet: drop and release the invalid packet.\n"));
		/* release packet*/
		RELEASE_NDIS_PACKET(pAd, pRxBlk->pRxPacket, NDIS_STATUS_FAILURE);
		return;
	}
}

#define BCN_TBTT_OFFSET		64	/*defer 64 us*/
VOID ReSyncBeaconTime(
	IN  PRTMP_ADAPTER   pAd)
{

	UINT32  Offset;


	Offset = (pAd->TbttTickCount) % (BCN_TBTT_OFFSET);

	pAd->TbttTickCount++;

	
	/* The updated BeaconInterval Value will affect Beacon Interval after two TBTT*/
	/* beacasue the original BeaconInterval had been loaded into next TBTT_TIMER*/
	
	if (Offset == (BCN_TBTT_OFFSET-2))
	{
		BCN_TIME_CFG_STRUC csr;
		RTMP_IO_READ32(pAd, BCN_TIME_CFG, &csr.word);
		csr.field.BeaconInterval = (pAd->CommonCfg.BeaconPeriod << 4) - 1 ;	/* ASIC register in units of 1/16 TU = 64us*/
		RTMP_IO_WRITE32(pAd, BCN_TIME_CFG, csr.word);
	}
	else
	{
		if (Offset == (BCN_TBTT_OFFSET-1))
		{
			BCN_TIME_CFG_STRUC csr;

			RTMP_IO_READ32(pAd, BCN_TIME_CFG, &csr.word);
			csr.field.BeaconInterval = (pAd->CommonCfg.BeaconPeriod) << 4; /* ASIC register in units of 1/16 TU*/
			RTMP_IO_WRITE32(pAd, BCN_TIME_CFG, csr.word);
		}
	}
}

#ifdef SOFT_ENCRYPT
BOOLEAN RTMPExpandPacketForSwEncrypt(
	IN  PRTMP_ADAPTER   pAd,
	IN	PTX_BLK			pTxBlk)
{
	PACKET_INFO		PacketInfo;
	UINT32	ex_head = 0, ex_tail = 0;
	UCHAR 	NumberOfFrag = RTMP_GET_PACKET_FRAGMENTS(pTxBlk->pPacket);

	if (pTxBlk->CipherAlg == CIPHER_AES)
		ex_tail = LEN_CCMP_MIC;

	ex_tail = (NumberOfFrag * ex_tail);

	pTxBlk->pPacket = ExpandPacket(pAd, pTxBlk->pPacket, ex_head, ex_tail);
	if (pTxBlk->pPacket == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: out of resource.\n", __FUNCTION__));
		return FALSE;
	}
	RTMP_QueryPacketInfo(pTxBlk->pPacket, &PacketInfo, &pTxBlk->pSrcBufHeader, &pTxBlk->SrcBufLen);									

	return TRUE;
}

VOID RTMPUpdateSwCacheCipherInfo(	
	IN  PRTMP_ADAPTER   pAd,
	IN	PTX_BLK			pTxBlk,
	IN	PUCHAR			pHdr)
{
	PHEADER_802_11 		pHeader_802_11;
	PMAC_TABLE_ENTRY	pMacEntry;

	pHeader_802_11 = (HEADER_802_11 *) pHdr;
	pMacEntry = pTxBlk->pMacEntry;

	if (pMacEntry && pHeader_802_11->FC.Wep && 
		CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_SOFTWARE_ENCRYPT))	
	{
		PCIPHER_KEY pKey = &pMacEntry->PairwiseKey;
	
		TX_BLK_SET_FLAG(pTxBlk, fTX_bSwEncrypt);

		pTxBlk->CipherAlg = pKey->CipherAlg;
		pTxBlk->pKey = pKey;
		if ((pKey->CipherAlg == CIPHER_WEP64) || (pKey->CipherAlg == CIPHER_WEP128))
			inc_iv_byte(pKey->TxTsc, LEN_WEP_TSC, 1);
		else if ((pKey->CipherAlg == CIPHER_TKIP) || (pKey->CipherAlg == CIPHER_AES))
			inc_iv_byte(pKey->TxTsc, LEN_WPA_TSC, 1);
		
	}

}

#endif /* SOFT_ENCRYPT */


