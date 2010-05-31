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
	rtusb_data.c

	Abstract:
	Ralink USB driver Tx/Rx functions.

	Revision History:
	Who         When          What
	--------    ----------    ----------------------------------------------
	Jan            03-25-2006    created

*/

#ifdef RTMP_MAC_USB

#include "../rt_config.h"

extern u8 Phy11BGNextRateUpward[];	/* defined in mlme.c */
extern u8 EpToQueue[];

void REPORT_AMSDU_FRAMES_TO_LLC(struct rt_rtmp_adapter *pAd,
				u8 *pData, unsigned long DataSize)
{
	void *pPacket;
	u32 nMSDU;
	struct sk_buff *pSkb;

	nMSDU = 0;
	/* allocate a rx packet */
	pSkb = dev_alloc_skb(RX_BUFFER_AGGRESIZE);
	pPacket = (void *)OSPKT_TO_RTPKT(pSkb);
	if (pSkb) {

		/* convert 802.11 to 802.3 packet */
		pSkb->dev = get_netdev_from_bssid(pAd, BSS0);
		RTMP_SET_PACKET_SOURCE(pPacket, PKTSRC_NDIS);
		deaggregate_AMSDU_announce(pAd, pPacket, pData, DataSize);
	} else {
		DBGPRINT(RT_DEBUG_ERROR, ("Can't allocate skb\n"));
	}
}

/*
	========================================================================

	Routine	Description:
		This subroutine will scan through releative ring descriptor to find
		out avaliable free ring descriptor and compare with request size.

	Arguments:
		pAd	Pointer	to our adapter
		RingType	Selected Ring

	Return Value:
		NDIS_STATUS_FAILURE		Not enough free descriptor
		NDIS_STATUS_SUCCESS		Enough free descriptor

	Note:

	========================================================================
*/
int RTUSBFreeDescriptorRequest(struct rt_rtmp_adapter *pAd,
				       u8 BulkOutPipeId,
				       u32 NumberRequired)
{
/*      u8                   FreeNumber = 0; */
/*      u32                    Index; */
	int Status = NDIS_STATUS_FAILURE;
	unsigned long IrqFlags;
	struct rt_ht_tx_context *pHTTXContext;

	pHTTXContext = &pAd->TxContext[BulkOutPipeId];
	RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);
	if ((pHTTXContext->CurWritePosition < pHTTXContext->NextBulkOutPosition)
	    &&
	    ((pHTTXContext->CurWritePosition + NumberRequired +
	      LOCAL_TXBUF_SIZE) > pHTTXContext->NextBulkOutPosition)) {

		RTUSB_SET_BULK_FLAG(pAd,
				    (fRTUSB_BULK_OUT_DATA_NORMAL <<
				     BulkOutPipeId));
	} else if ((pHTTXContext->CurWritePosition == 8)
		   && (pHTTXContext->NextBulkOutPosition <
		       (NumberRequired + LOCAL_TXBUF_SIZE))) {
		RTUSB_SET_BULK_FLAG(pAd,
				    (fRTUSB_BULK_OUT_DATA_NORMAL <<
				     BulkOutPipeId));
	} else if (pHTTXContext->bCurWriting == TRUE) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("RTUSBFreeD c3 --> QueIdx=%d, CWPos=%ld, NBOutPos=%ld!\n",
			  BulkOutPipeId, pHTTXContext->CurWritePosition,
			  pHTTXContext->NextBulkOutPosition));
		RTUSB_SET_BULK_FLAG(pAd,
				    (fRTUSB_BULK_OUT_DATA_NORMAL <<
				     BulkOutPipeId));
	} else {
		Status = NDIS_STATUS_SUCCESS;
	}
	RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);

	return Status;
}

int RTUSBFreeDescriptorRelease(struct rt_rtmp_adapter *pAd,
				       u8 BulkOutPipeId)
{
	unsigned long IrqFlags;
	struct rt_ht_tx_context *pHTTXContext;

	pHTTXContext = &pAd->TxContext[BulkOutPipeId];
	RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);
	pHTTXContext->bCurWriting = FALSE;
	RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);

	return NDIS_STATUS_SUCCESS;
}

BOOLEAN RTUSBNeedQueueBackForAgg(struct rt_rtmp_adapter *pAd, u8 BulkOutPipeId)
{
	unsigned long IrqFlags;
	struct rt_ht_tx_context *pHTTXContext;
	BOOLEAN needQueBack = FALSE;

	pHTTXContext = &pAd->TxContext[BulkOutPipeId];

	RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);
	if ((pHTTXContext->IRPPending ==
	     TRUE) /*&& (pAd->TxSwQueue[BulkOutPipeId].Number == 0) */) {
		if ((pHTTXContext->CurWritePosition <
		     pHTTXContext->ENextBulkOutPosition)
		    &&
		    (((pHTTXContext->ENextBulkOutPosition +
		       MAX_AGGREGATION_SIZE) < MAX_TXBULK_LIMIT)
		     || (pHTTXContext->CurWritePosition >
			 MAX_AGGREGATION_SIZE))) {
			needQueBack = TRUE;
		} else
		    if ((pHTTXContext->CurWritePosition >
			 pHTTXContext->ENextBulkOutPosition)
			&&
			((pHTTXContext->ENextBulkOutPosition +
			  MAX_AGGREGATION_SIZE) <
			 pHTTXContext->CurWritePosition)) {
			needQueBack = TRUE;
		}
	}
	RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);

	return needQueBack;

}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
void RTUSBRejectPendingPackets(struct rt_rtmp_adapter *pAd)
{
	u8 Index;
	struct rt_queue_entry *pEntry;
	void *pPacket;
	struct rt_queue_header *pQueue;

	for (Index = 0; Index < 4; Index++) {
		NdisAcquireSpinLock(&pAd->TxSwQueueLock[Index]);
		while (pAd->TxSwQueue[Index].Head != NULL) {
			pQueue = (struct rt_queue_header *)&(pAd->TxSwQueue[Index]);
			pEntry = RemoveHeadQueue(pQueue);
			pPacket = QUEUE_ENTRY_TO_PACKET(pEntry);
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
		}
		NdisReleaseSpinLock(&pAd->TxSwQueueLock[Index]);

	}

}

/*
	========================================================================

	Routine	Description:
		Calculates the duration which is required to transmit out frames
	with given size and specified rate.

	Arguments:
		pTxD		Pointer to transmit descriptor
		Ack			Setting for Ack requirement bit
		Fragment	Setting for Fragment bit
		RetryMode	Setting for retry mode
		Ifs			Setting for IFS gap
		Rate		Setting for transmit rate
		Service		Setting for service
		Length		Frame length
		TxPreamble  Short or Long preamble when using CCK rates
		QueIdx - 0-3, according to 802.11e/d4.4 June/2003

	Return Value:
		None

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

	========================================================================
*/

void RTMPWriteTxInfo(struct rt_rtmp_adapter *pAd,
		     struct rt_txinfo *pTxInfo,
		     u16 USBDMApktLen,
		     IN BOOLEAN bWiv,
		     u8 QueueSel, u8 NextValid, u8 TxBurst)
{
	pTxInfo->USBDMATxPktLen = USBDMApktLen;
	pTxInfo->QSEL = QueueSel;
	if (QueueSel != FIFO_EDCA)
		DBGPRINT(RT_DEBUG_TRACE,
			 ("====> QueueSel != FIFO_EDCA<============\n"));
	pTxInfo->USBDMANextVLD = FALSE;	/*NextValid;  // Need to check with Jan about this. */
	pTxInfo->USBDMATxburst = TxBurst;
	pTxInfo->WIV = bWiv;
	pTxInfo->SwUseLastRound = 0;
	pTxInfo->rsv = 0;
	pTxInfo->rsv2 = 0;
}

#endif /* RTMP_MAC_USB // */
