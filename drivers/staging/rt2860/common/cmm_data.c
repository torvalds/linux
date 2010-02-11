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
*/

#include "../rt_config.h"

u8 SNAP_802_1H[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
u8 SNAP_BRIDGE_TUNNEL[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };

/* Add Cisco Aironet SNAP heade for CCX2 support */
u8 SNAP_AIRONET[] = { 0xaa, 0xaa, 0x03, 0x00, 0x40, 0x96, 0x00, 0x00 };
u8 CKIP_LLC_SNAP[] = { 0xaa, 0xaa, 0x03, 0x00, 0x40, 0x96, 0x00, 0x02 };
u8 EAPOL_LLC_SNAP[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };
u8 EAPOL[] = { 0x88, 0x8e };
u8 TPID[] = { 0x81, 0x00 };	/* VLAN related */

u8 IPX[] = { 0x81, 0x37 };
u8 APPLE_TALK[] = { 0x80, 0xf3 };

u8 RateIdToPlcpSignal[12] = {
	0, /* RATE_1 */ 1, /* RATE_2 */ 2, /* RATE_5_5 */ 3,	/* RATE_11 *//* see BBP spec */
	11, /* RATE_6 */ 15, /* RATE_9 */ 10, /* RATE_12 */ 14,	/* RATE_18 *//* see IEEE802.11a-1999 p.14 */
	9, /* RATE_24 */ 13, /* RATE_36 */ 8, /* RATE_48 */ 12 /* RATE_54 */
};				/* see IEEE802.11a-1999 p.14 */

u8 OfdmSignalToRateId[16] = {
	RATE_54, RATE_54, RATE_54, RATE_54,	/* OFDM PLCP Signal = 0,  1,  2,  3 respectively */
	RATE_54, RATE_54, RATE_54, RATE_54,	/* OFDM PLCP Signal = 4,  5,  6,  7 respectively */
	RATE_48, RATE_24, RATE_12, RATE_6,	/* OFDM PLCP Signal = 8,  9,  10, 11 respectively */
	RATE_54, RATE_36, RATE_18, RATE_9,	/* OFDM PLCP Signal = 12, 13, 14, 15 respectively */
};

u8 OfdmRateToRxwiMCS[12] = {
	0, 0, 0, 0,
	0, 1, 2, 3,		/* OFDM rate 6,9,12,18 = rxwi mcs 0,1,2,3 */
	4, 5, 6, 7,		/* OFDM rate 24,36,48,54 = rxwi mcs 4,5,6,7 */
};

u8 RxwiMCSToOfdmRate[12] = {
	RATE_6, RATE_9, RATE_12, RATE_18,
	RATE_24, RATE_36, RATE_48, RATE_54,	/* OFDM rate 6,9,12,18 = rxwi mcs 0,1,2,3 */
	4, 5, 6, 7,		/* OFDM rate 24,36,48,54 = rxwi mcs 4,5,6,7 */
};

char *MCSToMbps[] =
    { "1Mbps", "2Mbps", "5.5Mbps", "11Mbps", "06Mbps", "09Mbps", "12Mbps",
"18Mbps", "24Mbps", "36Mbps", "48Mbps", "54Mbps", "MM-0", "MM-1", "MM-2", "MM-3",
"MM-4", "MM-5", "MM-6", "MM-7", "MM-8", "MM-9", "MM-10", "MM-11", "MM-12", "MM-13",
"MM-14", "MM-15", "MM-32", "ee1", "ee2", "ee3" };

u8 default_cwmin[] =
    { CW_MIN_IN_BITS, CW_MIN_IN_BITS, CW_MIN_IN_BITS - 1, CW_MIN_IN_BITS - 2 };
/*u8 default_cwmax[]={CW_MAX_IN_BITS, CW_MAX_IN_BITS, CW_MIN_IN_BITS, CW_MIN_IN_BITS-1}; */
u8 default_sta_aifsn[] = { 3, 7, 2, 2 };

u8 MapUserPriorityToAccessCategory[8] =
    { QID_AC_BE, QID_AC_BK, QID_AC_BK, QID_AC_BE, QID_AC_VI, QID_AC_VI,
QID_AC_VO, QID_AC_VO };

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
int MiniportMMRequest(struct rt_rtmp_adapter *pAd,
			      u8 QueIdx, u8 *pData, u32 Length)
{
	void *pPacket;
	int Status = NDIS_STATUS_SUCCESS;
	unsigned long FreeNum;
	u8 rtmpHwHdr[TXINFO_SIZE + TXWI_SIZE];	/*RTMP_HW_HDR_LEN]; */
#ifdef RTMP_MAC_PCI
	unsigned long IrqFlags = 0;
	u8 IrqState;
#endif /* RTMP_MAC_PCI // */
	BOOLEAN bUseDataQ = FALSE;
	int retryCnt = 0;

	ASSERT(Length <= MGMT_DMA_BUFFER_SIZE);

	if ((QueIdx & MGMT_USE_QUEUE_FLAG) == MGMT_USE_QUEUE_FLAG) {
		bUseDataQ = TRUE;
		QueIdx &= (~MGMT_USE_QUEUE_FLAG);
	}
#ifdef RTMP_MAC_PCI
	/* 2860C use Tx Ring */
	IrqState = pAd->irq_disabled;
	if (pAd->MACVersion == 0x28600100) {
		QueIdx = (bUseDataQ == TRUE ? QueIdx : 3);
		bUseDataQ = TRUE;
	}
	if (bUseDataQ && (!IrqState))
		RTMP_IRQ_LOCK(&pAd->irq_lock, IrqFlags);
#endif /* RTMP_MAC_PCI // */

	do {
		/* Reset is in progress, stop immediately */
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS) ||
		    RTMP_TEST_FLAG(pAd,
				   fRTMP_ADAPTER_HALT_IN_PROGRESS |
				   fRTMP_ADAPTER_NIC_NOT_EXIST)
		    || !RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_START_UP)) {
			Status = NDIS_STATUS_FAILURE;
			break;
		}
		/* Check Free priority queue */
		/* Since we use PBF Queue2 for management frame.  Its corresponding DMA ring should be using TxRing. */
#ifdef RTMP_MAC_PCI
		if (bUseDataQ) {
			retryCnt = MAX_DATAMM_RETRY;
			/* free Tx(QueIdx) resources */
			RTMPFreeTXDUponTxDmaDone(pAd, QueIdx);
			FreeNum = GET_TXRING_FREENO(pAd, QueIdx);
		} else
#endif /* RTMP_MAC_PCI // */
		{
			FreeNum = GET_MGMTRING_FREENO(pAd);
		}

		if ((FreeNum > 0)) {
			/* We need to reserve space for rtmp hardware header. i.e., TxWI for RT2860 and TxInfo+TxWI for RT2870 */
			NdisZeroMemory(&rtmpHwHdr, (TXINFO_SIZE + TXWI_SIZE));
			Status =
			    RTMPAllocateNdisPacket(pAd, &pPacket,
						   (u8 *)& rtmpHwHdr,
						   (TXINFO_SIZE + TXWI_SIZE),
						   pData, Length);
			if (Status != NDIS_STATUS_SUCCESS) {
				DBGPRINT(RT_DEBUG_WARN,
					 ("MiniportMMRequest (error:: can't allocate NDIS PACKET)\n"));
				break;
			}
			/*pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_CCK; */
			/*pAd->CommonCfg.MlmeRate = RATE_2; */

#ifdef RTMP_MAC_PCI
			if (bUseDataQ) {
				Status =
				    MlmeDataHardTransmit(pAd, QueIdx, pPacket);
				retryCnt--;
			} else
#endif /* RTMP_MAC_PCI // */
				Status = MlmeHardTransmit(pAd, QueIdx, pPacket);
			if (Status == NDIS_STATUS_SUCCESS)
				retryCnt = 0;
			else
				RTMPFreeNdisPacket(pAd, pPacket);
		} else {
			pAd->RalinkCounters.MgmtRingFullCount++;
#ifdef RTMP_MAC_PCI
			if (bUseDataQ) {
				retryCnt--;
				DBGPRINT(RT_DEBUG_TRACE,
					 ("retryCnt %d\n", retryCnt));
				if (retryCnt == 0) {
					DBGPRINT(RT_DEBUG_ERROR,
						 ("Qidx(%d), not enough space in DataRing, MgmtRingFullCount=%ld!\n",
						  QueIdx,
						  pAd->RalinkCounters.
						  MgmtRingFullCount));
				}
			}
#endif /* RTMP_MAC_PCI // */
			DBGPRINT(RT_DEBUG_ERROR,
				 ("Qidx(%d), not enough space in MgmtRing, MgmtRingFullCount=%ld!\n",
				  QueIdx,
				  pAd->RalinkCounters.MgmtRingFullCount));
		}
	} while (retryCnt > 0);

#ifdef RTMP_MAC_PCI
	if (bUseDataQ && (!IrqState))
		RTMP_IRQ_UNLOCK(&pAd->irq_lock, IrqFlags);
#endif /* RTMP_MAC_PCI // */

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
int MlmeHardTransmit(struct rt_rtmp_adapter *pAd,
			     u8 QueIdx, void *pPacket)
{
	struct rt_packet_info PacketInfo;
	u8 *pSrcBufVA;
	u32 SrcBufLen;
	struct rt_header_802_11 * pHeader_802_11;

	if ((pAd->CommonCfg.RadarDetect.RDMode != RD_NORMAL_MODE)
	    ) {
		return NDIS_STATUS_FAILURE;
	}

	RTMP_QueryPacketInfo(pPacket, &PacketInfo, &pSrcBufVA, &SrcBufLen);
	if (pSrcBufVA == NULL)
		return NDIS_STATUS_FAILURE;

	pHeader_802_11 = (struct rt_header_802_11 *) (pSrcBufVA + TXINFO_SIZE + TXWI_SIZE);

#ifdef RTMP_MAC_PCI
	if (pAd->MACVersion == 0x28600100)
		return MlmeHardTransmitTxRing(pAd, QueIdx, pPacket);
	else
#endif /* RTMP_MAC_PCI // */
		return MlmeHardTransmitMgmtRing(pAd, QueIdx, pPacket);

}

int MlmeHardTransmitMgmtRing(struct rt_rtmp_adapter *pAd,
				     u8 QueIdx, void *pPacket)
{
	struct rt_packet_info PacketInfo;
	u8 *pSrcBufVA;
	u32 SrcBufLen;
	struct rt_header_802_11 * pHeader_802_11;
	BOOLEAN bAckRequired, bInsertTimestamp;
	u8 MlmeRate;
	struct rt_txwi * pFirstTxWI;
	struct rt_mac_table_entry *pMacEntry = NULL;
	u8 PID;

	RTMP_QueryPacketInfo(pPacket, &PacketInfo, &pSrcBufVA, &SrcBufLen);

	/* Make sure MGMT ring resource won't be used by other threads */
	RTMP_SEM_LOCK(&pAd->MgmtRingLock);
	if (pSrcBufVA == NULL) {
		/* The buffer shouldn't be NULL */
		RTMP_SEM_UNLOCK(&pAd->MgmtRingLock);
		return NDIS_STATUS_FAILURE;
	}

	{
		/* outgoing frame always wakeup PHY to prevent frame lost */
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
			AsicForceWakeup(pAd, TRUE);
	}

	pFirstTxWI = (struct rt_txwi *) (pSrcBufVA + TXINFO_SIZE);
	pHeader_802_11 = (struct rt_header_802_11 *) (pSrcBufVA + TXINFO_SIZE + TXWI_SIZE);	/*TXWI_SIZE); */

	if (pHeader_802_11->Addr1[0] & 0x01) {
		MlmeRate = pAd->CommonCfg.BasicMlmeRate;
	} else {
		MlmeRate = pAd->CommonCfg.MlmeRate;
	}

	/* Verify Mlme rate for a / g bands. */
	if ((pAd->LatchRfRegs.Channel > 14) && (MlmeRate < RATE_6))	/* 11A band */
		MlmeRate = RATE_6;

	if ((pHeader_802_11->FC.Type == BTYPE_DATA) &&
	    (pHeader_802_11->FC.SubType == SUBTYPE_QOS_NULL)) {
		pMacEntry = MacTableLookup(pAd, pHeader_802_11->Addr1);
	}

	{
		/* Fixed W52 with Activity scan issue in ABG_MIXED and ABGN_MIXED mode. */
		if (pAd->CommonCfg.PhyMode == PHY_11ABG_MIXED
		    || pAd->CommonCfg.PhyMode == PHY_11ABGN_MIXED) {
			if (pAd->LatchRfRegs.Channel > 14)
				pAd->CommonCfg.MlmeTransmit.field.MODE = 1;
			else
				pAd->CommonCfg.MlmeTransmit.field.MODE = 0;
		}
	}

	/* */
	/* Should not be hard code to set PwrMgmt to 0 (PWR_ACTIVE) */
	/* Snice it's been set to 0 while on MgtMacHeaderInit */
	/* By the way this will cause frame to be send on PWR_SAVE failed. */
	/* */
	pHeader_802_11->FC.PwrMgmt = PWR_ACTIVE;	/* (pAd->StaCfg.Psm == PWR_SAVE); */

	/* */
	/* In WMM-UAPSD, mlme frame should be set psm as power saving but probe request frame */
	/* Data-Null packets alse pass through MMRequest in RT2860, however, we hope control the psm bit to pass APSD */
/*      if ((pHeader_802_11->FC.Type != BTYPE_DATA) && (pHeader_802_11->FC.Type != BTYPE_CNTL)) */
	{
		if ((pHeader_802_11->FC.SubType == SUBTYPE_ACTION) ||
		    ((pHeader_802_11->FC.Type == BTYPE_DATA) &&
		     ((pHeader_802_11->FC.SubType == SUBTYPE_QOS_NULL) ||
		      (pHeader_802_11->FC.SubType == SUBTYPE_NULL_FUNC)))) {
			if (pAd->StaCfg.Psm == PWR_SAVE)
				pHeader_802_11->FC.PwrMgmt = PWR_SAVE;
			else
				pHeader_802_11->FC.PwrMgmt =
				    pAd->CommonCfg.bAPSDForcePowerSave;
		}
	}

	bInsertTimestamp = FALSE;
	if (pHeader_802_11->FC.Type == BTYPE_CNTL)	/* must be PS-POLL */
	{
		/*Set PM bit in ps-poll, to fix WLK 1.2  PowerSaveMode_ext failure issue. */
		if ((pAd->OpMode == OPMODE_STA)
		    && (pHeader_802_11->FC.SubType == SUBTYPE_PS_POLL)) {
			pHeader_802_11->FC.PwrMgmt = PWR_SAVE;
		}
		bAckRequired = FALSE;
	} else			/* BTYPE_MGMT or BTYPE_DATA(must be NULL frame) */
	{
		/*pAd->Sequence++; */
		/*pHeader_802_11->Sequence = pAd->Sequence; */

		if (pHeader_802_11->Addr1[0] & 0x01)	/* MULTICAST, BROADCAST */
		{
			bAckRequired = FALSE;
			pHeader_802_11->Duration = 0;
		} else {
			bAckRequired = TRUE;
			pHeader_802_11->Duration =
			    RTMPCalcDuration(pAd, MlmeRate, 14);
			if ((pHeader_802_11->FC.SubType == SUBTYPE_PROBE_RSP)
			    && (pHeader_802_11->FC.Type == BTYPE_MGMT)) {
				bInsertTimestamp = TRUE;
				bAckRequired = FALSE;	/* Disable ACK to prevent retry 0x1f for Probe Response */
			} else
			    if ((pHeader_802_11->FC.SubType ==
				 SUBTYPE_PROBE_REQ)
				&& (pHeader_802_11->FC.Type == BTYPE_MGMT)) {
				bAckRequired = FALSE;	/* Disable ACK to prevent retry 0x1f for Probe Request */
			}
		}
	}

	pHeader_802_11->Sequence = pAd->Sequence++;
	if (pAd->Sequence > 0xfff)
		pAd->Sequence = 0;

	/* Before radar detection done, mgmt frame can not be sent but probe req */
	/* Because we need to use probe req to trigger driver to send probe req in passive scan */
	if ((pHeader_802_11->FC.SubType != SUBTYPE_PROBE_REQ)
	    && (pAd->CommonCfg.bIEEE80211H == 1)
	    && (pAd->CommonCfg.RadarDetect.RDMode != RD_NORMAL_MODE)) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("MlmeHardTransmit --> radar detect not in normal mode!\n"));
/*              if (!IrqState) */
		RTMP_SEM_UNLOCK(&pAd->MgmtRingLock);
		return (NDIS_STATUS_FAILURE);
	}

	/* */
	/* fill scatter-and-gather buffer list into TXD. Internally created NDIS PACKET */
	/* should always has only one physical buffer, and the whole frame size equals */
	/* to the first scatter buffer size */
	/* */

	/* Initialize TX Descriptor */
	/* For inter-frame gap, the number is for this frame and next frame */
	/* For MLME rate, we will fix as 2Mb to match other vendor's implement */
/*      pAd->CommonCfg.MlmeTransmit.field.MODE = 1; */

/* management frame doesn't need encryption. so use RESERVED_WCID no matter u are sending to specific wcid or not. */
	PID = PID_MGMT;

	if (pMacEntry == NULL) {
		RTMPWriteTxWI(pAd, pFirstTxWI, FALSE, FALSE, bInsertTimestamp,
			      FALSE, bAckRequired, FALSE, 0, RESERVED_WCID,
			      (SrcBufLen - TXINFO_SIZE - TXWI_SIZE), PID, 0,
			      (u8)pAd->CommonCfg.MlmeTransmit.field.MCS,
			      IFS_BACKOFF, FALSE, &pAd->CommonCfg.MlmeTransmit);
	} else {
		/* dont use low rate to send QoS Null data frame */
		RTMPWriteTxWI(pAd, pFirstTxWI, FALSE, FALSE,
			      bInsertTimestamp, FALSE, bAckRequired, FALSE,
			      0, pMacEntry->Aid,
			      (SrcBufLen - TXINFO_SIZE - TXWI_SIZE),
			      pMacEntry->MaxHTPhyMode.field.MCS, 0,
			      (u8)pMacEntry->MaxHTPhyMode.field.MCS,
			      IFS_BACKOFF, FALSE, &pMacEntry->MaxHTPhyMode);
	}

	/* Now do hardware-depened kick out. */
	HAL_KickOutMgmtTx(pAd, QueIdx, pPacket, pSrcBufVA, SrcBufLen);

	/* Make sure to release MGMT ring resource */
/*      if (!IrqState) */
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
								No ACK, 		//pTxBlk->bAckRequired = FALSE;
								No WMM, 		//pTxBlk->bWMM = FALSE;
								No piggyback,   //pTxBlk->bPiggyBack = FALSE;
								Force LowRate,  //pTxBlk->bForceLowRate = TRUE;
					Specific :	Basically, for specific packet, we should handle it specifically, but now all specific packets are use
									the same policy to handle it.
								Force LowRate,  //pTxBlk->bForceLowRate = TRUE;

					11N Rate :
								No piggyback,	//pTxBlk->bPiggyBack = FALSE;

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
static u8 TxPktClassification(struct rt_rtmp_adapter *pAd, void *pPacket)
{
	u8 TxFrameType = TX_UNKOWN_FRAME;
	u8 Wcid;
	struct rt_mac_table_entry *pMacEntry = NULL;
	BOOLEAN bHTRate = FALSE;

	Wcid = RTMP_GET_PACKET_WCID(pPacket);
	if (Wcid == MCAST_WCID) {	/* Handle for RA is Broadcast/Multicast Address. */
		return TX_MCAST_FRAME;
	}
	/* Handle for unicast packets */
	pMacEntry = &pAd->MacTab.Content[Wcid];
	if (RTMP_GET_PACKET_LOWRATE(pPacket)) {	/* It's a specific packet need to force low rate, i.e., bDHCPFrame, bEAPOLFrame, bWAIFrame */
		TxFrameType = TX_LEGACY_FRAME;
	} else if (IS_HT_RATE(pMacEntry)) {	/* it's a 11n capable packet */

		/* Depends on HTPhyMode to check if the peer support the HTRate transmission. */
		/*      Currently didn't support A-MSDU embedded in A-MPDU */
		bHTRate = TRUE;
		if (RTMP_GET_PACKET_MOREDATA(pPacket)
		    || (pMacEntry->PsMode == PWR_SAVE))
			TxFrameType = TX_LEGACY_FRAME;
		else if ((pMacEntry->
			  TXBAbitmap & (1 << (RTMP_GET_PACKET_UP(pPacket)))) !=
			 0)
			return TX_AMPDU_FRAME;
		else if (CLIENT_STATUS_TEST_FLAG
			 (pMacEntry, fCLIENT_STATUS_AMSDU_INUSED))
			return TX_AMSDU_FRAME;
		else
			TxFrameType = TX_LEGACY_FRAME;
	} else {		/* it's a legacy b/g packet. */
		if ((CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_AGGREGATION_CAPABLE) && pAd->CommonCfg.bAggregationCapable) && (RTMP_GET_PACKET_TXRATE(pPacket) >= RATE_6) && (!(OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) && CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_WMM_CAPABLE)))) {	/* if peer support Ralink Aggregation, we use it. */
			TxFrameType = TX_RALINK_FRAME;
		} else {
			TxFrameType = TX_LEGACY_FRAME;
		}
	}

	/* Currently, our fragment only support when a unicast packet send as NOT-ARALINK, NOT-AMSDU and NOT-AMPDU. */
	if ((RTMP_GET_PACKET_FRAGMENTS(pPacket) > 1)
	    && (TxFrameType == TX_LEGACY_FRAME))
		TxFrameType = TX_FRAG_FRAME;

	return TxFrameType;
}

BOOLEAN RTMP_FillTxBlkInfo(struct rt_rtmp_adapter *pAd, struct rt_tx_blk *pTxBlk)
{
	struct rt_packet_info PacketInfo;
	void *pPacket;
	struct rt_mac_table_entry *pMacEntry = NULL;

	pPacket = pTxBlk->pPacket;
	RTMP_QueryPacketInfo(pPacket, &PacketInfo, &pTxBlk->pSrcBufHeader,
			     &pTxBlk->SrcBufLen);

	pTxBlk->Wcid = RTMP_GET_PACKET_WCID(pPacket);
	pTxBlk->apidx = RTMP_GET_PACKET_IF(pPacket);
	pTxBlk->UserPriority = RTMP_GET_PACKET_UP(pPacket);
	pTxBlk->FrameGap = IFS_HTTXOP;	/* ASIC determine Frame Gap */

	if (RTMP_GET_PACKET_CLEAR_EAP_FRAME(pTxBlk->pPacket))
		TX_BLK_SET_FLAG(pTxBlk, fTX_bClearEAPFrame);
	else
		TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bClearEAPFrame);

	/* Default to clear this flag */
	TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bForceNonQoS);

	if (pTxBlk->Wcid == MCAST_WCID) {
		pTxBlk->pMacEntry = NULL;
		{
			pTxBlk->pTransmit =
			    &pAd->MacTab.Content[MCAST_WCID].HTPhyMode;
		}

		TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bAckRequired);	/* AckRequired = FALSE, when broadcast packet in Adhoc mode. */
		/*TX_BLK_SET_FLAG(pTxBlk, fTX_bForceLowRate); */
		TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bAllowFrag);
		TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bWMM);
		if (RTMP_GET_PACKET_MOREDATA(pPacket)) {
			TX_BLK_SET_FLAG(pTxBlk, fTX_bMoreData);
		}

	} else {
		pTxBlk->pMacEntry = &pAd->MacTab.Content[pTxBlk->Wcid];
		pTxBlk->pTransmit = &pTxBlk->pMacEntry->HTPhyMode;

		pMacEntry = pTxBlk->pMacEntry;

		/* For all unicast packets, need Ack unless the Ack Policy is not set as NORMAL_ACK. */
		if (pAd->CommonCfg.AckPolicy[pTxBlk->QueIdx] != NORMAL_ACK)
			TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bAckRequired);
		else
			TX_BLK_SET_FLAG(pTxBlk, fTX_bAckRequired);

		if ((pAd->OpMode == OPMODE_STA) &&
		    (ADHOC_ON(pAd)) &&
		    (RX_FILTER_TEST_FLAG(pAd, fRX_FILTER_ACCEPT_PROMISCUOUS))) {
			if (pAd->CommonCfg.PSPXlink)
				TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bAckRequired);
		}

		{
			{

				/* If support WMM, enable it. */
				if (OPSTATUS_TEST_FLAG
				    (pAd, fOP_STATUS_WMM_INUSED)
				    && CLIENT_STATUS_TEST_FLAG(pMacEntry,
							       fCLIENT_STATUS_WMM_CAPABLE))
					TX_BLK_SET_FLAG(pTxBlk, fTX_bWMM);

/*                              if (pAd->StaCfg.bAutoTxRateSwitch) */
/*                                      TX_BLK_SET_FLAG(pTxBlk, fTX_AutoRateSwitch); */
			}
		}

		if (pTxBlk->TxFrameType == TX_LEGACY_FRAME) {
			if ((RTMP_GET_PACKET_LOWRATE(pPacket)) || ((pAd->OpMode == OPMODE_AP) && (pMacEntry->MaxHTPhyMode.field.MODE == MODE_CCK) && (pMacEntry->MaxHTPhyMode.field.MCS == RATE_1))) {	/* Specific packet, i.e., bDHCPFrame, bEAPOLFrame, bWAIFrame, need force low rate. */
				pTxBlk->pTransmit =
				    &pAd->MacTab.Content[MCAST_WCID].HTPhyMode;

				/* Modify the WMM bit for ICV issue. If we have a packet with EOSP field need to set as 1, how to handle it??? */
				if (IS_HT_STA(pTxBlk->pMacEntry) &&
				    (CLIENT_STATUS_TEST_FLAG
				     (pMacEntry, fCLIENT_STATUS_RALINK_CHIPSET))
				    && ((pAd->CommonCfg.bRdg == TRUE)
					&& CLIENT_STATUS_TEST_FLAG(pMacEntry,
								   fCLIENT_STATUS_RDG_CAPABLE)))
				{
					TX_BLK_CLEAR_FLAG(pTxBlk, fTX_bWMM);
					TX_BLK_SET_FLAG(pTxBlk,
							fTX_bForceNonQoS);
				}
			}

			if ((IS_HT_RATE(pMacEntry) == FALSE) && (CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_PIGGYBACK_CAPABLE))) {	/* Currently piggy-back only support when peer is operate in b/g mode. */
				TX_BLK_SET_FLAG(pTxBlk, fTX_bPiggyBack);
			}

			if (RTMP_GET_PACKET_MOREDATA(pPacket)) {
				TX_BLK_SET_FLAG(pTxBlk, fTX_bMoreData);
			}
		} else if (pTxBlk->TxFrameType == TX_FRAG_FRAME) {
			TX_BLK_SET_FLAG(pTxBlk, fTX_bAllowFrag);
		}

		pMacEntry->DebugTxCount++;
	}

	return TRUE;
}

BOOLEAN CanDoAggregateTransmit(struct rt_rtmp_adapter *pAd,
			       char * pPacket, struct rt_tx_blk *pTxBlk)
{

	/*DBGPRINT(RT_DEBUG_TRACE, ("Check if can do aggregation! TxFrameType=%d!\n", pTxBlk->TxFrameType)); */

	if (RTMP_GET_PACKET_WCID(pPacket) == MCAST_WCID)
		return FALSE;

	if (RTMP_GET_PACKET_DHCP(pPacket) ||
	    RTMP_GET_PACKET_EAPOL(pPacket) || RTMP_GET_PACKET_WAI(pPacket))
		return FALSE;

	if ((pTxBlk->TxFrameType == TX_AMSDU_FRAME) && ((pTxBlk->TotalFrameLen + GET_OS_PKT_LEN(pPacket)) > (RX_BUFFER_AGGRESIZE - 100))) {	/* For AMSDU, allow the packets with total length < max-amsdu size */
		return FALSE;
	}

	if ((pTxBlk->TxFrameType == TX_RALINK_FRAME) && (pTxBlk->TxPacketList.Number == 2)) {	/* For RALINK-Aggregation, allow two frames in one batch. */
		return FALSE;
	}

	if ((INFRA_ON(pAd)) && (pAd->OpMode == OPMODE_STA))	/* must be unicast to AP */
		return TRUE;
	else
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
void RTMPDeQueuePacket(struct rt_rtmp_adapter *pAd, IN BOOLEAN bIntContext, u8 QIdx,	/* BulkOutPipeId */
		       u8 Max_Tx_Packets)
{
	struct rt_queue_entry *pEntry = NULL;
	void *pPacket;
	int Status = NDIS_STATUS_SUCCESS;
	u8 Count = 0;
	struct rt_queue_header *pQueue;
	unsigned long FreeNumber[NUM_OF_TX_RING];
	u8 QueIdx, sQIdx, eQIdx;
	unsigned long IrqFlags = 0;
	BOOLEAN hasTxDesc = FALSE;
	struct rt_tx_blk TxBlk;
	struct rt_tx_blk *pTxBlk;

	if (QIdx == NUM_OF_TX_RING) {
		sQIdx = 0;
		eQIdx = 3;	/* 4 ACs, start from 0. */
	} else {
		sQIdx = eQIdx = QIdx;
	}

	for (QueIdx = sQIdx; QueIdx <= eQIdx; QueIdx++) {
		Count = 0;

		RTMP_START_DEQUEUE(pAd, QueIdx, IrqFlags);

		while (1) {
			if ((RTMP_TEST_FLAG
			     (pAd,
			      (fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS |
			       fRTMP_ADAPTER_RADIO_OFF |
			       fRTMP_ADAPTER_RESET_IN_PROGRESS |
			       fRTMP_ADAPTER_HALT_IN_PROGRESS |
			       fRTMP_ADAPTER_NIC_NOT_EXIST)))) {
				RTMP_STOP_DEQUEUE(pAd, QueIdx, IrqFlags);
				return;
			}

			if (Count >= Max_Tx_Packets)
				break;

			DEQUEUE_LOCK(&pAd->irq_lock, bIntContext, IrqFlags);
			if (&pAd->TxSwQueue[QueIdx] == NULL) {
				DEQUEUE_UNLOCK(&pAd->irq_lock, bIntContext,
					       IrqFlags);
				break;
			}
#ifdef RTMP_MAC_PCI
			FreeNumber[QueIdx] = GET_TXRING_FREENO(pAd, QueIdx);

			if (FreeNumber[QueIdx] <= 5) {
				/* free Tx(QueIdx) resources */
				RTMPFreeTXDUponTxDmaDone(pAd, QueIdx);
				FreeNumber[QueIdx] =
				    GET_TXRING_FREENO(pAd, QueIdx);
			}
#endif /* RTMP_MAC_PCI // */

			/* probe the Queue Head */
			pQueue = &pAd->TxSwQueue[QueIdx];
			if ((pEntry = pQueue->Head) == NULL) {
				DEQUEUE_UNLOCK(&pAd->irq_lock, bIntContext,
					       IrqFlags);
				break;
			}

			pTxBlk = &TxBlk;
			NdisZeroMemory((u8 *)pTxBlk, sizeof(struct rt_tx_blk));
			/*InitializeQueueHeader(&pTxBlk->TxPacketList);         // Didn't need it because we already memzero it. */
			pTxBlk->QueIdx = QueIdx;

			pPacket = QUEUE_ENTRY_TO_PACKET(pEntry);

			/* Early check to make sure we have enoguh Tx Resource. */
			hasTxDesc =
			    RTMP_HAS_ENOUGH_FREE_DESC(pAd, pTxBlk,
						      FreeNumber[QueIdx],
						      pPacket);
			if (!hasTxDesc) {
				pAd->PrivateInfo.TxRingFullCnt++;

				DEQUEUE_UNLOCK(&pAd->irq_lock, bIntContext,
					       IrqFlags);

				break;
			}

			pTxBlk->TxFrameType = TxPktClassification(pAd, pPacket);
			pEntry = RemoveHeadQueue(pQueue);
			pTxBlk->TotalFrameNum++;
			pTxBlk->TotalFragNum += RTMP_GET_PACKET_FRAGMENTS(pPacket);	/* The real fragment number maybe vary */
			pTxBlk->TotalFrameLen += GET_OS_PKT_LEN(pPacket);
			pTxBlk->pPacket = pPacket;
			InsertTailQueue(&pTxBlk->TxPacketList,
					PACKET_TO_QUEUE_ENTRY(pPacket));

			if (pTxBlk->TxFrameType == TX_RALINK_FRAME
			    || pTxBlk->TxFrameType == TX_AMSDU_FRAME) {
				/* Enhance SW Aggregation Mechanism */
				if (NEED_QUEUE_BACK_FOR_AGG
				    (pAd, QueIdx, FreeNumber[QueIdx],
				     pTxBlk->TxFrameType)) {
					InsertHeadQueue(pQueue,
							PACKET_TO_QUEUE_ENTRY
							(pPacket));
					DEQUEUE_UNLOCK(&pAd->irq_lock,
						       bIntContext, IrqFlags);
					break;
				}

				do {
					if ((pEntry = pQueue->Head) == NULL)
						break;

					/* For TX_AMSDU_FRAME/TX_RALINK_FRAME, Need to check if next pakcet can do aggregation. */
					pPacket = QUEUE_ENTRY_TO_PACKET(pEntry);
					FreeNumber[QueIdx] =
					    GET_TXRING_FREENO(pAd, QueIdx);
					hasTxDesc =
					    RTMP_HAS_ENOUGH_FREE_DESC(pAd,
								      pTxBlk,
								      FreeNumber
								      [QueIdx],
								      pPacket);
					if ((hasTxDesc == FALSE)
					    ||
					    (CanDoAggregateTransmit
					     (pAd, pPacket, pTxBlk) == FALSE))
						break;

					/*Remove the packet from the TxSwQueue and insert into pTxBlk */
					pEntry = RemoveHeadQueue(pQueue);
					ASSERT(pEntry);
					pPacket = QUEUE_ENTRY_TO_PACKET(pEntry);
					pTxBlk->TotalFrameNum++;
					pTxBlk->TotalFragNum += RTMP_GET_PACKET_FRAGMENTS(pPacket);	/* The real fragment number maybe vary */
					pTxBlk->TotalFrameLen +=
					    GET_OS_PKT_LEN(pPacket);
					InsertTailQueue(&pTxBlk->TxPacketList,
							PACKET_TO_QUEUE_ENTRY
							(pPacket));
				} while (1);

				if (pTxBlk->TxPacketList.Number == 1)
					pTxBlk->TxFrameType = TX_LEGACY_FRAME;
			}
#ifdef RTMP_MAC_USB
			DEQUEUE_UNLOCK(&pAd->irq_lock, bIntContext, IrqFlags);
#endif /* RTMP_MAC_USB // */
			Count += pTxBlk->TxPacketList.Number;

			/* Do HardTransmit now. */
			Status = STAHardTransmit(pAd, pTxBlk, QueIdx);

#ifdef RTMP_MAC_PCI
			DEQUEUE_UNLOCK(&pAd->irq_lock, bIntContext, IrqFlags);
			/* static rate also need NICUpdateFifoStaCounters() function. */
			/*if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED)) */
			NICUpdateFifoStaCounters(pAd);
#endif /* RTMP_MAC_PCI // */

		}

		RTMP_STOP_DEQUEUE(pAd, QueIdx, IrqFlags);

#ifdef RTMP_MAC_USB
		if (!hasTxDesc)
			RTUSBKickBulkOut(pAd);
#endif /* RTMP_MAC_USB // */
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
u16 RTMPCalcDuration(struct rt_rtmp_adapter *pAd, u8 Rate, unsigned long Size)
{
	unsigned long Duration = 0;

	if (Rate < RATE_FIRST_OFDM_RATE)	/* CCK */
	{
		if ((Rate > RATE_1)
		    && OPSTATUS_TEST_FLAG(pAd,
					  fOP_STATUS_SHORT_PREAMBLE_INUSED))
			Duration = 96;	/* 72+24 preamble+plcp */
		else
			Duration = 192;	/* 144+48 preamble+plcp */

		Duration += (u16)((Size << 4) / RateIdTo500Kbps[Rate]);
		if ((Size << 4) % RateIdTo500Kbps[Rate])
			Duration++;
	} else if (Rate <= RATE_LAST_OFDM_RATE)	/* OFDM rates */
	{
		Duration = 20 + 6;	/* 16+4 preamble+plcp + Signal Extension */
		Duration +=
		    4 * (u16)((11 + Size * 4) / RateIdTo500Kbps[Rate]);
		if ((11 + Size * 4) % RateIdTo500Kbps[Rate])
			Duration += 4;
	} else			/*mimo rate */
	{
		Duration = 20 + 6;	/* 16+4 preamble+plcp + Signal Extension */
	}

	return (u16)Duration;
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

    See also : BASmartHardTransmit()    !

	========================================================================
*/
void RTMPWriteTxWI(struct rt_rtmp_adapter *pAd, struct rt_txwi * pOutTxWI, IN BOOLEAN FRAG, IN BOOLEAN CFACK, IN BOOLEAN InsTimestamp, IN BOOLEAN AMPDU, IN BOOLEAN Ack, IN BOOLEAN NSeq,	/* HW new a sequence. */
		   u8 BASize,
		   u8 WCID,
		   unsigned long Length,
		   u8 PID,
		   u8 TID,
		   u8 TxRate,
		   u8 Txopmode,
		   IN BOOLEAN CfAck, IN HTTRANSMIT_SETTING * pTransmit)
{
	struct rt_mac_table_entry *pMac = NULL;
	struct rt_txwi TxWI;
	struct rt_txwi * pTxWI;

	if (WCID < MAX_LEN_OF_MAC_TABLE)
		pMac = &pAd->MacTab.Content[WCID];

	/* */
	/* Always use Long preamble before verifiation short preamble functionality works well. */
	/* Todo: remove the following line if short preamble functionality works */
	/* */
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED);
	NdisZeroMemory(&TxWI, TXWI_SIZE);
	pTxWI = &TxWI;

	pTxWI->FRAG = FRAG;

	pTxWI->CFACK = CFACK;
	pTxWI->TS = InsTimestamp;
	pTxWI->AMPDU = AMPDU;
	pTxWI->ACK = Ack;
	pTxWI->txop = Txopmode;

	pTxWI->NSEQ = NSeq;
	/* John tune the performace with Intel Client in 20 MHz performance */
	BASize = pAd->CommonCfg.TxBASize;
	if (pAd->MACVersion == 0x28720200) {
		if (BASize > 13)
			BASize = 13;
	} else {
		if (BASize > 7)
			BASize = 7;
	}
	pTxWI->BAWinSize = BASize;
	pTxWI->ShortGI = pTransmit->field.ShortGI;
	pTxWI->STBC = pTransmit->field.STBC;

	pTxWI->WirelessCliID = WCID;
	pTxWI->MPDUtotalByteCount = Length;
	pTxWI->PacketId = PID;

	/* If CCK or OFDM, BW must be 20 */
	pTxWI->BW =
	    (pTransmit->field.MODE <=
	     MODE_OFDM) ? (BW_20) : (pTransmit->field.BW);

	pTxWI->MCS = pTransmit->field.MCS;
	pTxWI->PHYMODE = pTransmit->field.MODE;
	pTxWI->CFACK = CfAck;

	if (pMac) {
		if (pAd->CommonCfg.bMIMOPSEnable) {
			if ((pMac->MmpsMode == MMPS_DYNAMIC)
			    && (pTransmit->field.MCS > 7)) {
				/* Dynamic MIMO Power Save Mode */
				pTxWI->MIMOps = 1;
			} else if (pMac->MmpsMode == MMPS_STATIC) {
				/* Static MIMO Power Save Mode */
				if (pTransmit->field.MODE >= MODE_HTMIX
				    && pTransmit->field.MCS > 7) {
					pTxWI->MCS = 7;
					pTxWI->MIMOps = 0;
				}
			}
		}
		/*pTxWI->MIMOps = (pMac->PsMode == PWR_MMPS)? 1:0; */
		if (pMac->bIAmBadAtheros
		    && (pMac->WepStatus != Ndis802_11WEPDisabled)) {
			pTxWI->MpduDensity = 7;
		} else {
			pTxWI->MpduDensity = pMac->MpduDensity;
		}
	}

	pTxWI->PacketId = pTxWI->MCS;
	NdisMoveMemory(pOutTxWI, &TxWI, sizeof(struct rt_txwi));
}

void RTMPWriteTxWI_Data(struct rt_rtmp_adapter *pAd,
			struct rt_txwi * pTxWI, struct rt_tx_blk *pTxBlk)
{
	HTTRANSMIT_SETTING *pTransmit;
	struct rt_mac_table_entry *pMacEntry;
	u8 BASize;

	ASSERT(pTxWI);

	pTransmit = pTxBlk->pTransmit;
	pMacEntry = pTxBlk->pMacEntry;

	/* */
	/* Always use Long preamble before verifiation short preamble functionality works well. */
	/* Todo: remove the following line if short preamble functionality works */
	/* */
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED);
	NdisZeroMemory(pTxWI, TXWI_SIZE);

	pTxWI->FRAG = TX_BLK_TEST_FLAG(pTxBlk, fTX_bAllowFrag);
	pTxWI->ACK = TX_BLK_TEST_FLAG(pTxBlk, fTX_bAckRequired);
	pTxWI->txop = pTxBlk->FrameGap;

	pTxWI->WirelessCliID = pTxBlk->Wcid;

	pTxWI->MPDUtotalByteCount = pTxBlk->MpduHeaderLen + pTxBlk->SrcBufLen;
	pTxWI->CFACK = TX_BLK_TEST_FLAG(pTxBlk, fTX_bPiggyBack);

	/* If CCK or OFDM, BW must be 20 */
	pTxWI->BW =
	    (pTransmit->field.MODE <=
	     MODE_OFDM) ? (BW_20) : (pTransmit->field.BW);
	pTxWI->AMPDU = ((pTxBlk->TxFrameType == TX_AMPDU_FRAME) ? TRUE : FALSE);

	/* John tune the performace with Intel Client in 20 MHz performance */
	BASize = pAd->CommonCfg.TxBASize;
	if ((pTxBlk->TxFrameType == TX_AMPDU_FRAME) && (pMacEntry)) {
		u8 RABAOriIdx = 0;	/*The RA's BA Originator table index. */

		RABAOriIdx =
		    pTxBlk->pMacEntry->BAOriWcidArray[pTxBlk->UserPriority];
		BASize = pAd->BATable.BAOriEntry[RABAOriIdx].BAWinSize;
	}

	pTxWI->TxBF = pTransmit->field.TxBF;
	pTxWI->BAWinSize = BASize;
	pTxWI->ShortGI = pTransmit->field.ShortGI;
	pTxWI->STBC = pTransmit->field.STBC;

	pTxWI->MCS = pTransmit->field.MCS;
	pTxWI->PHYMODE = pTransmit->field.MODE;

	if (pMacEntry) {
		if ((pMacEntry->MmpsMode == MMPS_DYNAMIC)
		    && (pTransmit->field.MCS > 7)) {
			/* Dynamic MIMO Power Save Mode */
			pTxWI->MIMOps = 1;
		} else if (pMacEntry->MmpsMode == MMPS_STATIC) {
			/* Static MIMO Power Save Mode */
			if (pTransmit->field.MODE >= MODE_HTMIX
			    && pTransmit->field.MCS > 7) {
				pTxWI->MCS = 7;
				pTxWI->MIMOps = 0;
			}
		}

		if (pMacEntry->bIAmBadAtheros
		    && (pMacEntry->WepStatus != Ndis802_11WEPDisabled)) {
			pTxWI->MpduDensity = 7;
		} else {
			pTxWI->MpduDensity = pMacEntry->MpduDensity;
		}
	}

	/* for rate adapation */
	pTxWI->PacketId = pTxWI->MCS;
}

void RTMPWriteTxWI_Cache(struct rt_rtmp_adapter *pAd,
			 struct rt_txwi * pTxWI, struct rt_tx_blk *pTxBlk)
{
	PHTTRANSMIT_SETTING /*pTxHTPhyMode, */ pTransmit;
	struct rt_mac_table_entry *pMacEntry;

	/* */
	/* update TXWI */
	/* */
	pMacEntry = pTxBlk->pMacEntry;
	pTransmit = pTxBlk->pTransmit;

	/*if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED)) */
	/*if (RTMPCheckEntryEnableAutoRateSwitch(pAd, pMacEntry)) */
	/*if (TX_BLK_TEST_FLAG(pTxBlk, fTX_AutoRateSwitch)) */
	if (pMacEntry->bAutoTxRateSwitch) {
		pTxWI->txop = IFS_HTTXOP;

		/* If CCK or OFDM, BW must be 20 */
		pTxWI->BW =
		    (pTransmit->field.MODE <=
		     MODE_OFDM) ? (BW_20) : (pTransmit->field.BW);
		pTxWI->ShortGI = pTransmit->field.ShortGI;
		pTxWI->STBC = pTransmit->field.STBC;

		pTxWI->MCS = pTransmit->field.MCS;
		pTxWI->PHYMODE = pTransmit->field.MODE;

		/* set PID for TxRateSwitching */
		pTxWI->PacketId = pTransmit->field.MCS;
	}

	pTxWI->AMPDU = ((pMacEntry->NoBADataCountDown == 0) ? TRUE : FALSE);
	pTxWI->MIMOps = 0;

	if (pAd->CommonCfg.bMIMOPSEnable) {
		/* MIMO Power Save Mode */
		if ((pMacEntry->MmpsMode == MMPS_DYNAMIC)
		    && (pTransmit->field.MCS > 7)) {
			/* Dynamic MIMO Power Save Mode */
			pTxWI->MIMOps = 1;
		} else if (pMacEntry->MmpsMode == MMPS_STATIC) {
			/* Static MIMO Power Save Mode */
			if ((pTransmit->field.MODE >= MODE_HTMIX)
			    && (pTransmit->field.MCS > 7)) {
				pTxWI->MCS = 7;
				pTxWI->MIMOps = 0;
			}
		}
	}

	pTxWI->MPDUtotalByteCount = pTxBlk->MpduHeaderLen + pTxBlk->SrcBufLen;

}

/* should be called only when - */
/* 1. MEADIA_CONNECTED */
/* 2. AGGREGATION_IN_USED */
/* 3. Fragmentation not in used */
/* 4. either no previous frame (pPrevAddr1=NULL) .OR. previoud frame is aggregatible */
BOOLEAN TxFrameIsAggregatible(struct rt_rtmp_adapter *pAd,
			      u8 *pPrevAddr1, u8 *p8023hdr)
{

	/* can't aggregate EAPOL (802.1x) frame */
	if ((p8023hdr[12] == 0x88) && (p8023hdr[13] == 0x8e))
		return FALSE;

	/* can't aggregate multicast/broadcast frame */
	if (p8023hdr[0] & 0x01)
		return FALSE;

	if (INFRA_ON(pAd))	/* must be unicast to AP */
		return TRUE;
	else if ((pPrevAddr1 == NULL) || MAC_ADDR_EQUAL(pPrevAddr1, p8023hdr))	/* unicast to same STA */
		return TRUE;
	else
		return FALSE;
}

/*
	========================================================================

	Routine Description:
	   Check the MSDU Aggregation policy
	1.HT aggregation is A-MSDU
	2.legaacy rate aggregation is software aggregation by Ralink.

	Arguments:

	Return Value:

	Note:

	========================================================================
*/
BOOLEAN PeerIsAggreOn(struct rt_rtmp_adapter *pAd,
		      unsigned long TxRate, struct rt_mac_table_entry *pMacEntry)
{
	unsigned long AFlags =
	    (fCLIENT_STATUS_AMSDU_INUSED | fCLIENT_STATUS_AGGREGATION_CAPABLE);

	if (pMacEntry != NULL && CLIENT_STATUS_TEST_FLAG(pMacEntry, AFlags)) {
		if (pMacEntry->HTPhyMode.field.MODE >= MODE_HTMIX) {
			return TRUE;
		}
#ifdef AGGREGATION_SUPPORT
		if (TxRate >= RATE_6 && pAd->CommonCfg.bAggregationCapable && (!(OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) && CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_WMM_CAPABLE)))) {	/* legacy  Ralink Aggregation support */
			return TRUE;
		}
#endif /* AGGREGATION_SUPPORT // */
	}

	return FALSE;

}

/*
	========================================================================

	Routine Description:
		Check and fine the packet waiting in SW queue with highest priority

	Arguments:
		pAd Pointer to our adapter

	Return Value:
		pQueue		Pointer to Waiting Queue

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
struct rt_queue_header *RTMPCheckTxSwQueue(struct rt_rtmp_adapter *pAd, u8 *pQueIdx)
{

	unsigned long Number;
	/* 2004-11-15 to be removed. test aggregation only */
/*      if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_AGGREGATION_INUSED)) && (*pNumber < 2)) */
/*               return NULL; */

	Number = pAd->TxSwQueue[QID_AC_BK].Number
	    + pAd->TxSwQueue[QID_AC_BE].Number
	    + pAd->TxSwQueue[QID_AC_VI].Number
	    + pAd->TxSwQueue[QID_AC_VO].Number;

	if (pAd->TxSwQueue[QID_AC_VO].Head != NULL) {
		*pQueIdx = QID_AC_VO;
		return (&pAd->TxSwQueue[QID_AC_VO]);
	} else if (pAd->TxSwQueue[QID_AC_VI].Head != NULL) {
		*pQueIdx = QID_AC_VI;
		return (&pAd->TxSwQueue[QID_AC_VI]);
	} else if (pAd->TxSwQueue[QID_AC_BE].Head != NULL) {
		*pQueIdx = QID_AC_BE;
		return (&pAd->TxSwQueue[QID_AC_BE]);
	} else if (pAd->TxSwQueue[QID_AC_BK].Head != NULL) {
		*pQueIdx = QID_AC_BK;
		return (&pAd->TxSwQueue[QID_AC_BK]);
	}
	/* No packet pending in Tx Sw queue */
	*pQueIdx = QID_AC_BK;

	return (NULL);
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
void RTMPSuspendMsduTransmission(struct rt_rtmp_adapter *pAd)
{
	DBGPRINT(RT_DEBUG_TRACE, ("SCANNING, suspend MSDU transmission ...\n"));

	/* */
	/* Before BSS_SCAN_IN_PROGRESS, we need to keep Current R66 value and */
	/* use Lowbound as R66 value on ScanNextChannel(...) */
	/* */
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R66,
				    &pAd->BbpTuning.R66CurrentValue);

	/* set BBP_R66 to 0x30/0x40 when scanning (AsicSwitchChannel will set R66 according to channel when scanning) */
	/*RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, (0x26 + GET_LNA_GAIN(pAd))); */
	RTMPSetAGCInitValue(pAd, BW_20);

	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS);
	/*RTMP_IO_WRITE32(pAd, TX_CNTL_CSR, 0x000f0000);                // abort all TX rings */
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
void RTMPResumeMsduTransmission(struct rt_rtmp_adapter *pAd)
{
/*    u8                     IrqState; */

	DBGPRINT(RT_DEBUG_TRACE, ("SCAN done, resume MSDU transmission ...\n"));

	/* After finish BSS_SCAN_IN_PROGRESS, we need to restore Current R66 value */
	/* R66 should not be 0 */
	if (pAd->BbpTuning.R66CurrentValue == 0) {
		pAd->BbpTuning.R66CurrentValue = 0x38;
		DBGPRINT_ERR(("RTMPResumeMsduTransmission, R66CurrentValue=0...\n"));
	}

	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66,
				     pAd->BbpTuning.R66CurrentValue);

	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS);
/* sample, for IRQ LOCK to SEM LOCK */
/*    IrqState = pAd->irq_disabled; */
/*      if (IrqState) */
/*              RTMPDeQueuePacket(pAd, TRUE, NUM_OF_TX_RING, MAX_TX_PROCESS); */
/*    else */
	RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
}

u32 deaggregate_AMSDU_announce(struct rt_rtmp_adapter *pAd,
				void *pPacket,
				u8 *pData, unsigned long DataSize)
{
	u16 PayloadSize;
	u16 SubFrameSize;
	struct rt_header_802_3 * pAMSDUsubheader;
	u32 nMSDU;
	u8 Header802_3[14];

	u8 *pPayload, *pDA, *pSA, *pRemovedLLCSNAP;
	void *pClonePacket;

	nMSDU = 0;

	while (DataSize > LENGTH_802_3) {

		nMSDU++;

		/*hex_dump("subheader", pData, 64); */
		pAMSDUsubheader = (struct rt_header_802_3 *) pData;
		/*pData += LENGTH_802_3; */
		PayloadSize =
		    pAMSDUsubheader->Octet[1] +
		    (pAMSDUsubheader->Octet[0] << 8);
		SubFrameSize = PayloadSize + LENGTH_802_3;

		if ((DataSize < SubFrameSize) || (PayloadSize > 1518)) {
			break;
		}
		/*DBGPRINT(RT_DEBUG_TRACE,("%d subframe: Size = %d\n",  nMSDU, PayloadSize)); */

		pPayload = pData + LENGTH_802_3;
		pDA = pData;
		pSA = pData + MAC_ADDR_LEN;

		/* convert to 802.3 header */
		CONVERT_TO_802_3(Header802_3, pDA, pSA, pPayload, PayloadSize,
				 pRemovedLLCSNAP);

		if ((Header802_3[12] == 0x88) && (Header802_3[13] == 0x8E)) {
			/* avoid local heap overflow, use dyanamic allocation */
			struct rt_mlme_queue_elem *Elem =
			    (struct rt_mlme_queue_elem *)kmalloc(sizeof(struct rt_mlme_queue_elem),
							MEM_ALLOC_FLAG);
			if (Elem != NULL) {
				memmove(Elem->Msg +
					(LENGTH_802_11 + LENGTH_802_1_H),
					pPayload, PayloadSize);
				Elem->MsgLen =
				    LENGTH_802_11 + LENGTH_802_1_H +
				    PayloadSize;
				/*WpaEAPOLKeyAction(pAd, Elem); */
				REPORT_MGMT_FRAME_TO_MLME(pAd, BSSID_WCID,
							  Elem->Msg,
							  Elem->MsgLen, 0, 0, 0,
							  0);
				kfree(Elem);
			}
		}

		{
			if (pRemovedLLCSNAP) {
				pPayload -= LENGTH_802_3;
				PayloadSize += LENGTH_802_3;
				NdisMoveMemory(pPayload, &Header802_3[0],
					       LENGTH_802_3);
			}
		}

		pClonePacket = ClonePacket(pAd, pPacket, pPayload, PayloadSize);
		if (pClonePacket) {
			ANNOUNCE_OR_FORWARD_802_3_PACKET(pAd, pClonePacket,
							 RTMP_GET_PACKET_IF
							 (pPacket));
		}

		/* A-MSDU has padding to multiple of 4 including subframe header. */
		/* align SubFrameSize up to multiple of 4 */
		SubFrameSize = (SubFrameSize + 3) & (~0x3);

		if (SubFrameSize > 1528 || SubFrameSize < 32) {
			break;
		}

		if (DataSize > SubFrameSize) {
			pData += SubFrameSize;
			DataSize -= SubFrameSize;
		} else {
			/* end of A-MSDU */
			DataSize = 0;
		}
	}

	/* finally release original rx packet */
	RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);

	return nMSDU;
}

u32 BA_Reorder_AMSDU_Annnounce(struct rt_rtmp_adapter *pAd, void *pPacket)
{
	u8 *pData;
	u16 DataSize;
	u32 nMSDU = 0;

	pData = (u8 *)GET_OS_PKT_DATAPTR(pPacket);
	DataSize = (u16)GET_OS_PKT_LEN(pPacket);

	nMSDU = deaggregate_AMSDU_announce(pAd, pPacket, pData, DataSize);

	return nMSDU;
}

/*
	==========================================================================
	Description:
		Look up the MAC address in the MAC table. Return NULL if not found.
	Return:
		pEntry - pointer to the MAC entry; NULL is not found
	==========================================================================
*/
struct rt_mac_table_entry *MacTableLookup(struct rt_rtmp_adapter *pAd, u8 *pAddr)
{
	unsigned long HashIdx;
	struct rt_mac_table_entry *pEntry = NULL;

	HashIdx = MAC_ADDR_HASH_INDEX(pAddr);
	pEntry = pAd->MacTab.Hash[HashIdx];

	while (pEntry
	       && (pEntry->ValidAsCLI || pEntry->ValidAsWDS
		   || pEntry->ValidAsApCli || pEntry->ValidAsMesh)) {
		if (MAC_ADDR_EQUAL(pEntry->Addr, pAddr)) {
			break;
		} else
			pEntry = pEntry->pNext;
	}

	return pEntry;
}

struct rt_mac_table_entry *MacTableInsertEntry(struct rt_rtmp_adapter *pAd,
				     u8 *pAddr,
				     u8 apidx, IN BOOLEAN CleanAll)
{
	u8 HashIdx;
	int i, FirstWcid;
	struct rt_mac_table_entry *pEntry = NULL, *pCurrEntry;
/*      u16  offset; */
/*      unsigned long   addr; */

	/* if FULL, return */
	if (pAd->MacTab.Size >= MAX_LEN_OF_MAC_TABLE)
		return NULL;

	FirstWcid = 1;

	if (pAd->StaCfg.BssType == BSS_INFRA)
		FirstWcid = 2;

	/* allocate one MAC entry */
	NdisAcquireSpinLock(&pAd->MacTabLock);
	for (i = FirstWcid; i < MAX_LEN_OF_MAC_TABLE; i++)	/* skip entry#0 so that "entry index == AID" for fast lookup */
	{
		/* pick up the first available vacancy */
		if ((pAd->MacTab.Content[i].ValidAsCLI == FALSE) &&
		    (pAd->MacTab.Content[i].ValidAsWDS == FALSE) &&
		    (pAd->MacTab.Content[i].ValidAsApCli == FALSE) &&
		    (pAd->MacTab.Content[i].ValidAsMesh == FALSE)
		    ) {
			pEntry = &pAd->MacTab.Content[i];
			if (CleanAll == TRUE) {
				pEntry->MaxSupportedRate = RATE_11;
				pEntry->CurrTxRate = RATE_11;
				NdisZeroMemory(pEntry, sizeof(struct rt_mac_table_entry));
				pEntry->PairwiseKey.KeyLen = 0;
				pEntry->PairwiseKey.CipherAlg = CIPHER_NONE;
			}
			{
				{
					pEntry->ValidAsCLI = TRUE;
					pEntry->ValidAsWDS = FALSE;
					pEntry->ValidAsApCli = FALSE;
					pEntry->ValidAsMesh = FALSE;
					pEntry->ValidAsDls = FALSE;
				}
			}

			pEntry->bIAmBadAtheros = FALSE;
			pEntry->pAd = pAd;
			pEntry->CMTimerRunning = FALSE;
			pEntry->EnqueueEapolStartTimerRunning =
			    EAPOL_START_DISABLE;
			pEntry->RSNIE_Len = 0;
			NdisZeroMemory(pEntry->R_Counter,
				       sizeof(pEntry->R_Counter));
			pEntry->ReTryCounter = PEER_MSG1_RETRY_TIMER_CTR;

			if (pEntry->ValidAsMesh)
				pEntry->apidx =
				    (apidx - MIN_NET_DEVICE_FOR_MESH);
			else if (pEntry->ValidAsApCli)
				pEntry->apidx =
				    (apidx - MIN_NET_DEVICE_FOR_APCLI);
			else if (pEntry->ValidAsWDS)
				pEntry->apidx =
				    (apidx - MIN_NET_DEVICE_FOR_WDS);
			else
				pEntry->apidx = apidx;

			{
				{
					pEntry->AuthMode = pAd->StaCfg.AuthMode;
					pEntry->WepStatus =
					    pAd->StaCfg.WepStatus;
					pEntry->PrivacyFilter =
					    Ndis802_11PrivFilterAcceptAll;
#ifdef RTMP_MAC_PCI
					AsicRemovePairwiseKeyEntry(pAd,
								   pEntry->
								   apidx,
								   (u8)i);
#endif /* RTMP_MAC_PCI // */
				}
			}

			pEntry->GTKState = REKEY_NEGOTIATING;
			pEntry->PairwiseKey.KeyLen = 0;
			pEntry->PairwiseKey.CipherAlg = CIPHER_NONE;
			pEntry->PortSecured = WPA_802_1X_PORT_NOT_SECURED;

			pEntry->PMKID_CacheIdx = ENTRY_NOT_FOUND;
			COPY_MAC_ADDR(pEntry->Addr, pAddr);
			pEntry->Sst = SST_NOT_AUTH;
			pEntry->AuthState = AS_NOT_AUTH;
			pEntry->Aid = (u16)i;	/*0; */
			pEntry->CapabilityInfo = 0;
			pEntry->PsMode = PWR_ACTIVE;
			pEntry->PsQIdleCount = 0;
			pEntry->NoDataIdleCount = 0;
			pEntry->AssocDeadLine = MAC_TABLE_ASSOC_TIMEOUT;
			pEntry->ContinueTxFailCnt = 0;
			InitializeQueueHeader(&pEntry->PsQueue);

			pAd->MacTab.Size++;
			/* Add this entry into ASIC RX WCID search table */
			RTMP_STA_ENTRY_ADD(pAd, pEntry);

			DBGPRINT(RT_DEBUG_TRACE,
				 ("MacTableInsertEntry - allocate entry #%d, Total= %d\n",
				  i, pAd->MacTab.Size));
			break;
		}
	}

	/* add this MAC entry into HASH table */
	if (pEntry) {
		HashIdx = MAC_ADDR_HASH_INDEX(pAddr);
		if (pAd->MacTab.Hash[HashIdx] == NULL) {
			pAd->MacTab.Hash[HashIdx] = pEntry;
		} else {
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
BOOLEAN MacTableDeleteEntry(struct rt_rtmp_adapter *pAd,
			    u16 wcid, u8 *pAddr)
{
	u16 HashIdx;
	struct rt_mac_table_entry *pEntry, *pPrevEntry, *pProbeEntry;
	BOOLEAN Cancelled;
	/*u16        offset; // unused variable */
	/*u8 j;                      // unused variable */

	if (wcid >= MAX_LEN_OF_MAC_TABLE)
		return FALSE;

	NdisAcquireSpinLock(&pAd->MacTabLock);

	HashIdx = MAC_ADDR_HASH_INDEX(pAddr);
	/*pEntry = pAd->MacTab.Hash[HashIdx]; */
	pEntry = &pAd->MacTab.Content[wcid];

	if (pEntry
	    && (pEntry->ValidAsCLI || pEntry->ValidAsApCli || pEntry->ValidAsWDS
		|| pEntry->ValidAsMesh)) {
		if (MAC_ADDR_EQUAL(pEntry->Addr, pAddr)) {

			/* Delete this entry from ASIC on-chip WCID Table */
			RTMP_STA_ENTRY_MAC_RESET(pAd, wcid);

			/* free resources of BA */
			BASessionTearDownALL(pAd, pEntry->Aid);

			pPrevEntry = NULL;
			pProbeEntry = pAd->MacTab.Hash[HashIdx];
			ASSERT(pProbeEntry);

			/* update Hash list */
			do {
				if (pProbeEntry == pEntry) {
					if (pPrevEntry == NULL) {
						pAd->MacTab.Hash[HashIdx] =
						    pEntry->pNext;
					} else {
						pPrevEntry->pNext =
						    pEntry->pNext;
					}
					break;
				}

				pPrevEntry = pProbeEntry;
				pProbeEntry = pProbeEntry->pNext;
			} while (pProbeEntry);

			/* not found ! */
			ASSERT(pProbeEntry != NULL);

			RTMP_STA_ENTRY_KEY_DEL(pAd, BSS0, wcid);

			if (pEntry->EnqueueEapolStartTimerRunning !=
			    EAPOL_START_DISABLE) {
				RTMPCancelTimer(&pEntry->
						EnqueueStartForPSKTimer,
						&Cancelled);
				pEntry->EnqueueEapolStartTimerRunning =
				    EAPOL_START_DISABLE;
			}

			NdisZeroMemory(pEntry, sizeof(struct rt_mac_table_entry));
			pAd->MacTab.Size--;
			DBGPRINT(RT_DEBUG_TRACE,
				 ("MacTableDeleteEntry1 - Total= %d\n",
				  pAd->MacTab.Size));
		} else {
			DBGPRINT(RT_DEBUG_OFF,
				 ("\n%s: Impossible Wcid = %d !\n",
				  __func__, wcid));
		}
	}

	NdisReleaseSpinLock(&pAd->MacTabLock);

	/*Reset operating mode when no Sta. */
	if (pAd->MacTab.Size == 0) {
		pAd->CommonCfg.AddHTInfo.AddHtInfo2.OperaionMode = 0;
		RTMP_UPDATE_PROTECT(pAd);	/* edit by johnli, fix "in_interrupt" error when call "MacTableDeleteEntry" in Rx tasklet */
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
void MacTableReset(struct rt_rtmp_adapter *pAd)
{
	int i;

	DBGPRINT(RT_DEBUG_TRACE, ("MacTableReset\n"));
	/*NdisAcquireSpinLock(&pAd->MacTabLock); */

	for (i = 1; i < MAX_LEN_OF_MAC_TABLE; i++) {
#ifdef RTMP_MAC_PCI
		RTMP_STA_ENTRY_MAC_RESET(pAd, i);
#endif /* RTMP_MAC_PCI // */
		if (pAd->MacTab.Content[i].ValidAsCLI == TRUE) {

			/* free resources of BA */
			BASessionTearDownALL(pAd, i);

			pAd->MacTab.Content[i].ValidAsCLI = FALSE;

#ifdef RTMP_MAC_USB
			NdisZeroMemory(pAd->MacTab.Content[i].Addr, 6);
			RTMP_STA_ENTRY_MAC_RESET(pAd, i);
#endif /* RTMP_MAC_USB // */

			/*AsicDelWcidTab(pAd, i); */
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
void AssocParmFill(struct rt_rtmp_adapter *pAd,
		   struct rt_mlme_assoc_req *AssocReq,
		   u8 *pAddr,
		   u16 CapabilityInfo,
		   unsigned long Timeout, u16 ListenIntv)
{
	COPY_MAC_ADDR(AssocReq->Addr, pAddr);
	/* Add mask to support 802.11b mode only */
	AssocReq->CapabilityInfo = CapabilityInfo & SUPPORTED_CAPABILITY_INFO;	/* not cf-pollable, not cf-poll-request */
	AssocReq->Timeout = Timeout;
	AssocReq->ListenIntv = ListenIntv;
}

/*
	==========================================================================
	Description:

	IRQL = DISPATCH_LEVEL

	==========================================================================
*/
void DisassocParmFill(struct rt_rtmp_adapter *pAd,
		      struct rt_mlme_disassoc_req *DisassocReq,
		      u8 *pAddr, u16 Reason)
{
	COPY_MAC_ADDR(DisassocReq->Addr, pAddr);
	DisassocReq->Reason = Reason;
}

/*
	========================================================================

	Routine Description:
		Check the out going frame, if this is an DHCP or ARP datagram
	will be duplicate another frame at low data rate transmit.

	Arguments:
		pAd 		Pointer to our adapter
		pPacket 	Pointer to outgoing Ndis frame

	Return Value:
		TRUE		To be duplicate at Low data rate transmit. (1mb)
		FALSE		Do nothing.

	IRQL = DISPATCH_LEVEL

	Note:

		MAC header + IP Header + UDP Header
		  14 Bytes	  20 Bytes

		UDP Header
		00|01|02|03|04|05|06|07|08|09|10|11|12|13|14|15|
						Source Port
		16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|
					Destination Port

		port 0x43 means Bootstrap Protocol, server.
		Port 0x44 means Bootstrap Protocol, client.

	========================================================================
*/

BOOLEAN RTMPCheckDHCPFrame(struct rt_rtmp_adapter *pAd, void *pPacket)
{
	struct rt_packet_info PacketInfo;
	unsigned long NumberOfBytesRead = 0;
	unsigned long CurrentOffset = 0;
	void *pVirtualAddress = NULL;
	u32 NdisBufferLength;
	u8 *pSrc;
	u16 Protocol;
	u8 ByteOffset36 = 0;
	u8 ByteOffset38 = 0;
	BOOLEAN ReadFirstParm = TRUE;

	RTMP_QueryPacketInfo(pPacket, &PacketInfo, (u8 **) & pVirtualAddress,
			     &NdisBufferLength);

	NumberOfBytesRead += NdisBufferLength;
	pSrc = (u8 *)pVirtualAddress;
	Protocol = *(pSrc + 12) * 256 + *(pSrc + 13);

	/* */
	/* Check DHCP & BOOTP protocol */
	/* */
	while (NumberOfBytesRead <= PacketInfo.TotalPacketLength) {
		if ((NumberOfBytesRead >= 35) && (ReadFirstParm == TRUE)) {
			CurrentOffset =
			    35 - (NumberOfBytesRead - NdisBufferLength);
			ByteOffset36 = *(pSrc + CurrentOffset);
			ReadFirstParm = FALSE;
		}

		if (NumberOfBytesRead >= 37) {
			CurrentOffset =
			    37 - (NumberOfBytesRead - NdisBufferLength);
			ByteOffset38 = *(pSrc + CurrentOffset);
			/*End of Read */
			break;
		}
		return FALSE;
	}

	/* Check for DHCP & BOOTP protocol */
	if ((ByteOffset36 != 0x44) || (ByteOffset38 != 0x43)) {
		/* */
		/* 2054 (hex 0806) for ARP datagrams */
		/* if this packet is not ARP datagrams, then do nothing */
		/* ARP datagrams will also be duplicate at 1mb broadcast frames */
		/* */
		if (Protocol != 0x0806)
			return FALSE;
	}

	return TRUE;
}

BOOLEAN RTMPCheckEtherType(struct rt_rtmp_adapter *pAd, void *pPacket)
{
	u16 TypeLen;
	u8 Byte0, Byte1;
	u8 *pSrcBuf;
	u32 pktLen;
	u16 srcPort, dstPort;
	BOOLEAN status = TRUE;

	pSrcBuf = GET_OS_PKT_DATAPTR(pPacket);
	pktLen = GET_OS_PKT_LEN(pPacket);

	ASSERT(pSrcBuf);

	RTMP_SET_PACKET_SPECIFIC(pPacket, 0);

	/* get Ethernet protocol field */
	TypeLen = (pSrcBuf[12] << 8) | pSrcBuf[13];

	pSrcBuf += LENGTH_802_3;	/* Skip the Ethernet Header. */

	if (TypeLen <= 1500) {	/* 802.3, 802.3 LLC */
		/*
		   DestMAC(6) + SrcMAC(6) + Lenght(2) +
		   DSAP(1) + SSAP(1) + Control(1) +
		   if the DSAP = 0xAA, SSAP=0xAA, Contorl = 0x03, it has a 5-bytes SNAP header.
		   => + SNAP (5, OriginationID(3) + etherType(2))
		 */
		if (pSrcBuf[0] == 0xAA && pSrcBuf[1] == 0xAA
		    && pSrcBuf[2] == 0x03) {
			Sniff2BytesFromNdisBuffer((char *)pSrcBuf, 6,
						  &Byte0, &Byte1);
			RTMP_SET_PACKET_LLCSNAP(pPacket, 1);
			TypeLen = (u16)((Byte0 << 8) + Byte1);
			pSrcBuf += 8;	/* Skip this LLC/SNAP header */
		} else {
			/*It just has 3-byte LLC header, maybe a legacy ether type frame. we didn't handle it. */
		}
	}
	/* If it's a VLAN packet, get the real Type/Length field. */
	if (TypeLen == 0x8100) {
		/* 0x8100 means VLAN packets */

		/* Dest. MAC Address (6-bytes) +
		   Source MAC Address (6-bytes) +
		   Length/Type = 802.1Q Tag Type (2-byte) +
		   Tag Control Information (2-bytes) +
		   Length / Type (2-bytes) +
		   data payload (0-n bytes) +
		   Pad (0-p bytes) +
		   Frame Check Sequence (4-bytes) */

		RTMP_SET_PACKET_VLAN(pPacket, 1);
		Sniff2BytesFromNdisBuffer((char *)pSrcBuf, 2, &Byte0,
					  &Byte1);
		TypeLen = (u16)((Byte0 << 8) + Byte1);

		pSrcBuf += 4;	/* Skip the VLAN Header. */
	}

	switch (TypeLen) {
	case 0x0800:
		{
			ASSERT((pktLen > 34));
			if (*(pSrcBuf + 9) == 0x11) {	/* udp packet */
				ASSERT((pktLen > 34));	/* 14 for ethernet header, 20 for IP header */

				pSrcBuf += 20;	/* Skip the IP header */
				srcPort =
				    OS_NTOHS(get_unaligned
					     ((u16 *)(pSrcBuf)));
				dstPort =
				    OS_NTOHS(get_unaligned
					     ((u16 *)(pSrcBuf + 2)));

				if ((srcPort == 0x44 && dstPort == 0x43) || (srcPort == 0x43 && dstPort == 0x44)) {	/*It's a BOOTP/DHCP packet */
					RTMP_SET_PACKET_DHCP(pPacket, 1);
				}
			}
		}
		break;
	case 0x0806:
		{
			/*ARP Packet. */
			RTMP_SET_PACKET_DHCP(pPacket, 1);
		}
		break;
	case 0x888e:
		{
			/* EAPOL Packet. */
			RTMP_SET_PACKET_EAPOL(pPacket, 1);
		}
		break;
	default:
		status = FALSE;
		break;
	}

	return status;

}

void Update_Rssi_Sample(struct rt_rtmp_adapter *pAd,
			struct rt_rssi_sample *pRssi, struct rt_rxwi * pRxWI)
{
	char rssi0 = pRxWI->RSSI0;
	char rssi1 = pRxWI->RSSI1;
	char rssi2 = pRxWI->RSSI2;

	if (rssi0 != 0) {
		pRssi->LastRssi0 = ConvertToRssi(pAd, (char)rssi0, RSSI_0);
		pRssi->AvgRssi0X8 =
		    (pRssi->AvgRssi0X8 - pRssi->AvgRssi0) + pRssi->LastRssi0;
		pRssi->AvgRssi0 = pRssi->AvgRssi0X8 >> 3;
	}

	if (rssi1 != 0) {
		pRssi->LastRssi1 = ConvertToRssi(pAd, (char)rssi1, RSSI_1);
		pRssi->AvgRssi1X8 =
		    (pRssi->AvgRssi1X8 - pRssi->AvgRssi1) + pRssi->LastRssi1;
		pRssi->AvgRssi1 = pRssi->AvgRssi1X8 >> 3;
	}

	if (rssi2 != 0) {
		pRssi->LastRssi2 = ConvertToRssi(pAd, (char)rssi2, RSSI_2);
		pRssi->AvgRssi2X8 =
		    (pRssi->AvgRssi2X8 - pRssi->AvgRssi2) + pRssi->LastRssi2;
		pRssi->AvgRssi2 = pRssi->AvgRssi2X8 >> 3;
	}
}

/* Normal legacy Rx packet indication */
void Indicate_Legacy_Packet(struct rt_rtmp_adapter *pAd,
			    struct rt_rx_blk *pRxBlk, u8 FromWhichBSSID)
{
	void *pRxPacket = pRxBlk->pRxPacket;
	u8 Header802_3[LENGTH_802_3];

	/* 1. get 802.3 Header */
	/* 2. remove LLC */
	/*              a. pointer pRxBlk->pData to payload */
	/*      b. modify pRxBlk->DataSize */
	RTMP_802_11_REMOVE_LLC_AND_CONVERT_TO_802_3(pRxBlk, Header802_3);

	if (pRxBlk->DataSize > MAX_RX_PKT_LEN) {

		/* release packet */
		RELEASE_NDIS_PACKET(pAd, pRxPacket, NDIS_STATUS_FAILURE);
		return;
	}

	STATS_INC_RX_PACKETS(pAd, FromWhichBSSID);

#ifdef RTMP_MAC_USB
	if (pAd->CommonCfg.bDisableReordering == 0) {
		struct rt_ba_rec_entry *pBAEntry;
		unsigned long Now32;
		u8 Wcid = pRxBlk->pRxWI->WirelessCliID;
		u8 TID = pRxBlk->pRxWI->TID;
		u16 Idx;

#define REORDERING_PACKET_TIMEOUT		((100 * OS_HZ)/1000)	/* system ticks -- 100 ms */

		if (Wcid < MAX_LEN_OF_MAC_TABLE) {
			Idx = pAd->MacTab.Content[Wcid].BARecWcidArray[TID];
			if (Idx != 0) {
				pBAEntry = &pAd->BATable.BARecEntry[Idx];
				/* update last rx time */
				NdisGetSystemUpTime(&Now32);
				if ((pBAEntry->list.qlen > 0) &&
				    RTMP_TIME_AFTER((unsigned long)Now32,
						    (unsigned long)(pBAEntry->
								    LastIndSeqAtTimer
								    +
								    (REORDERING_PACKET_TIMEOUT)))
				    ) {
					DBGPRINT(RT_DEBUG_OFF,
						 ("Indicate_Legacy_Packet():flush reordering_timeout_mpdus! RxWI->Flags=%d, pRxWI.TID=%d, RxD->AMPDU=%d!\n",
						  pRxBlk->Flags,
						  pRxBlk->pRxWI->TID,
						  pRxBlk->RxD.AMPDU));
					hex_dump("Dump the legacy Packet:",
						 GET_OS_PKT_DATAPTR(pRxBlk->
								    pRxPacket),
						 64);
					ba_flush_reordering_timeout_mpdus(pAd,
									  pBAEntry,
									  Now32);
				}
			}
		}
	}
#endif /* RTMP_MAC_USB // */

	wlan_802_11_to_802_3_packet(pAd, pRxBlk, Header802_3, FromWhichBSSID);

	/* */
	/* pass this 802.3 packet to upper layer or forward this packet to WM directly */
	/* */
	ANNOUNCE_OR_FORWARD_802_3_PACKET(pAd, pRxPacket, FromWhichBSSID);
}

/* Normal, AMPDU or AMSDU */
void CmmRxnonRalinkFrameIndicate(struct rt_rtmp_adapter *pAd,
				 struct rt_rx_blk *pRxBlk, u8 FromWhichBSSID)
{
	if (RX_BLK_TEST_FLAG(pRxBlk, fRX_AMPDU)
	    && (pAd->CommonCfg.bDisableReordering == 0)) {
		Indicate_AMPDU_Packet(pAd, pRxBlk, FromWhichBSSID);
	} else {
		if (RX_BLK_TEST_FLAG(pRxBlk, fRX_AMSDU)) {
			/* handle A-MSDU */
			Indicate_AMSDU_Packet(pAd, pRxBlk, FromWhichBSSID);
		} else {
			Indicate_Legacy_Packet(pAd, pRxBlk, FromWhichBSSID);
		}
	}
}

void CmmRxRalinkFrameIndicate(struct rt_rtmp_adapter *pAd,
			      struct rt_mac_table_entry *pEntry,
			      struct rt_rx_blk *pRxBlk, u8 FromWhichBSSID)
{
	u8 Header802_3[LENGTH_802_3];
	u16 Msdu2Size;
	u16 Payload1Size, Payload2Size;
	u8 *pData2;
	void *pPacket2 = NULL;

	Msdu2Size = *(pRxBlk->pData) + (*(pRxBlk->pData + 1) << 8);

	if ((Msdu2Size <= 1536) && (Msdu2Size < pRxBlk->DataSize)) {
		/* skip two byte MSDU2 len */
		pRxBlk->pData += 2;
		pRxBlk->DataSize -= 2;
	} else {
		/* release packet */
		RELEASE_NDIS_PACKET(pAd, pRxBlk->pRxPacket,
				    NDIS_STATUS_FAILURE);
		return;
	}

	/* get 802.3 Header and  remove LLC */
	RTMP_802_11_REMOVE_LLC_AND_CONVERT_TO_802_3(pRxBlk, Header802_3);

	ASSERT(pRxBlk->pRxPacket);

	/* Ralink Aggregation frame */
	pAd->RalinkCounters.OneSecRxAggregationCount++;
	Payload1Size = pRxBlk->DataSize - Msdu2Size;
	Payload2Size = Msdu2Size - LENGTH_802_3;

	pData2 = pRxBlk->pData + Payload1Size + LENGTH_802_3;

	pPacket2 =
	    duplicate_pkt(pAd, (pData2 - LENGTH_802_3), LENGTH_802_3, pData2,
			  Payload2Size, FromWhichBSSID);

	if (!pPacket2) {
		/* release packet */
		RELEASE_NDIS_PACKET(pAd, pRxBlk->pRxPacket,
				    NDIS_STATUS_FAILURE);
		return;
	}
	/* update payload size of 1st packet */
	pRxBlk->DataSize = Payload1Size;
	wlan_802_11_to_802_3_packet(pAd, pRxBlk, Header802_3, FromWhichBSSID);

	ANNOUNCE_OR_FORWARD_802_3_PACKET(pAd, pRxBlk->pRxPacket,
					 FromWhichBSSID);

	if (pPacket2) {
		ANNOUNCE_OR_FORWARD_802_3_PACKET(pAd, pPacket2, FromWhichBSSID);
	}
}

#define RESET_FRAGFRAME(_fragFrame) \
	{								\
		_fragFrame.RxSize = 0;		\
		_fragFrame.Sequence = 0;	\
		_fragFrame.LastFrag = 0;	\
		_fragFrame.Flags = 0;		\
	}

void *RTMPDeFragmentDataFrame(struct rt_rtmp_adapter *pAd, struct rt_rx_blk *pRxBlk)
{
	struct rt_header_802_11 * pHeader = pRxBlk->pHeader;
	void *pRxPacket = pRxBlk->pRxPacket;
	u8 *pData = pRxBlk->pData;
	u16 DataSize = pRxBlk->DataSize;
	void *pRetPacket = NULL;
	u8 *pFragBuffer = NULL;
	BOOLEAN bReassDone = FALSE;
	u8 HeaderRoom = 0;

	ASSERT(pHeader);

	HeaderRoom = pData - (u8 *) pHeader;

	/* Re-assemble the fragmented packets */
	if (pHeader->Frag == 0)	/* Frag. Number is 0 : First frag or only one pkt */
	{
		/* the first pkt of fragment, record it. */
		if (pHeader->FC.MoreFrag) {
			ASSERT(pAd->FragFrame.pFragPacket);
			pFragBuffer =
			    GET_OS_PKT_DATAPTR(pAd->FragFrame.pFragPacket);
			pAd->FragFrame.RxSize = DataSize + HeaderRoom;
			NdisMoveMemory(pFragBuffer, pHeader,
				       pAd->FragFrame.RxSize);
			pAd->FragFrame.Sequence = pHeader->Sequence;
			pAd->FragFrame.LastFrag = pHeader->Frag;	/* Should be 0 */
			ASSERT(pAd->FragFrame.LastFrag == 0);
			goto done;	/* end of processing this frame */
		}
	} else			/*Middle & End of fragment */
	{
		if ((pHeader->Sequence != pAd->FragFrame.Sequence) ||
		    (pHeader->Frag != (pAd->FragFrame.LastFrag + 1))) {
			/* Fragment is not the same sequence or out of fragment number order */
			/* Reset Fragment control blk */
			RESET_FRAGFRAME(pAd->FragFrame);
			DBGPRINT(RT_DEBUG_ERROR,
				 ("Fragment is not the same sequence or out of fragment number order.\n"));
			goto done;	/* give up this frame */
		} else if ((pAd->FragFrame.RxSize + DataSize) > MAX_FRAME_SIZE) {
			/* Fragment frame is too large, it exeeds the maximum frame size. */
			/* Reset Fragment control blk */
			RESET_FRAGFRAME(pAd->FragFrame);
			DBGPRINT(RT_DEBUG_ERROR,
				 ("Fragment frame is too large, it exeeds the maximum frame size.\n"));
			goto done;	/* give up this frame */
		}
		/* */
		/* Broadcom AP(BCM94704AGR) will send out LLC in fragment's packet, LLC only can accpet at first fragment. */
		/* In this case, we will dropt it. */
		/* */
		if (NdisEqualMemory(pData, SNAP_802_1H, sizeof(SNAP_802_1H))) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("Find another LLC at Middle or End fragment(SN=%d, Frag=%d)\n",
				  pHeader->Sequence, pHeader->Frag));
			goto done;	/* give up this frame */
		}

		pFragBuffer = GET_OS_PKT_DATAPTR(pAd->FragFrame.pFragPacket);

		/* concatenate this fragment into the re-assembly buffer */
		NdisMoveMemory((pFragBuffer + pAd->FragFrame.RxSize), pData,
			       DataSize);
		pAd->FragFrame.RxSize += DataSize;
		pAd->FragFrame.LastFrag = pHeader->Frag;	/* Update fragment number */

		/* Last fragment */
		if (pHeader->FC.MoreFrag == FALSE) {
			bReassDone = TRUE;
		}
	}

done:
	/* always release rx fragmented packet */
	RELEASE_NDIS_PACKET(pAd, pRxPacket, NDIS_STATUS_FAILURE);

	/* return defragmented packet if packet is reassembled completely */
	/* otherwise return NULL */
	if (bReassDone) {
		void *pNewFragPacket;

		/* allocate a new packet buffer for fragment */
		pNewFragPacket =
		    RTMP_AllocateFragPacketBuffer(pAd, RX_BUFFER_NORMSIZE);
		if (pNewFragPacket) {
			/* update RxBlk */
			pRetPacket = pAd->FragFrame.pFragPacket;
			pAd->FragFrame.pFragPacket = pNewFragPacket;
			pRxBlk->pHeader =
			    (struct rt_header_802_11 *) GET_OS_PKT_DATAPTR(pRetPacket);
			pRxBlk->pData = (u8 *) pRxBlk->pHeader + HeaderRoom;
			pRxBlk->DataSize = pAd->FragFrame.RxSize - HeaderRoom;
			pRxBlk->pRxPacket = pRetPacket;
		} else {
			RESET_FRAGFRAME(pAd->FragFrame);
		}
	}

	return pRetPacket;
}

void Indicate_AMSDU_Packet(struct rt_rtmp_adapter *pAd,
			   struct rt_rx_blk *pRxBlk, u8 FromWhichBSSID)
{
	u32 nMSDU;

	update_os_packet_info(pAd, pRxBlk, FromWhichBSSID);
	RTMP_SET_PACKET_IF(pRxBlk->pRxPacket, FromWhichBSSID);
	nMSDU =
	    deaggregate_AMSDU_announce(pAd, pRxBlk->pRxPacket, pRxBlk->pData,
				       pRxBlk->DataSize);
}

void Indicate_EAPOL_Packet(struct rt_rtmp_adapter *pAd,
			   struct rt_rx_blk *pRxBlk, u8 FromWhichBSSID)
{
	struct rt_mac_table_entry *pEntry = NULL;

	{
		pEntry = &pAd->MacTab.Content[BSSID_WCID];
		STARxEAPOLFrameIndicate(pAd, pEntry, pRxBlk, FromWhichBSSID);
		return;
	}

	if (pEntry == NULL) {
		DBGPRINT(RT_DEBUG_WARN,
			 ("Indicate_EAPOL_Packet: drop and release the invalid packet.\n"));
		/* release packet */
		RELEASE_NDIS_PACKET(pAd, pRxBlk->pRxPacket,
				    NDIS_STATUS_FAILURE);
		return;
	}
}

#define BCN_TBTT_OFFSET		64	/*defer 64 us */
void ReSyncBeaconTime(struct rt_rtmp_adapter *pAd)
{

	u32 Offset;

	Offset = (pAd->TbttTickCount) % (BCN_TBTT_OFFSET);

	pAd->TbttTickCount++;

	/* */
	/* The updated BeaconInterval Value will affect Beacon Interval after two TBTT */
	/* beacasue the original BeaconInterval had been loaded into next TBTT_TIMER */
	/* */
	if (Offset == (BCN_TBTT_OFFSET - 2)) {
		BCN_TIME_CFG_STRUC csr;
		RTMP_IO_READ32(pAd, BCN_TIME_CFG, &csr.word);
		csr.field.BeaconInterval = (pAd->CommonCfg.BeaconPeriod << 4) - 1;	/* ASIC register in units of 1/16 TU = 64us */
		RTMP_IO_WRITE32(pAd, BCN_TIME_CFG, csr.word);
	} else {
		if (Offset == (BCN_TBTT_OFFSET - 1)) {
			BCN_TIME_CFG_STRUC csr;

			RTMP_IO_READ32(pAd, BCN_TIME_CFG, &csr.word);
			csr.field.BeaconInterval = (pAd->CommonCfg.BeaconPeriod) << 4;	/* ASIC register in units of 1/16 TU */
			RTMP_IO_WRITE32(pAd, BCN_TIME_CFG, csr.word);
		}
	}
}
