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
#include "../rt_config.h"

extern  UCHAR Phy11BGNextRateUpward[]; // defined in mlme.c
extern UCHAR	EpToQueue[];

VOID REPORT_AMSDU_FRAMES_TO_LLC(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			pData,
	IN	ULONG			DataSize)
{
	PNDIS_PACKET	pPacket;
	UINT			nMSDU;
	struct			sk_buff *pSkb;

	nMSDU = 0;
	/* allocate a rx packet */
	pSkb = dev_alloc_skb(RX_BUFFER_AGGRESIZE);
	pPacket = (PNDIS_PACKET)OSPKT_TO_RTPKT(pSkb);
	if (pSkb)
	{

		/* convert 802.11 to 802.3 packet */
		pSkb->dev = get_netdev_from_bssid(pAd, BSS0);
		RTMP_SET_PACKET_SOURCE(pPacket, PKTSRC_NDIS);
		deaggregate_AMSDU_announce(pAd, pPacket, pData, DataSize);
	}
	else
	{
		DBGPRINT(RT_DEBUG_ERROR,("Can't allocate skb\n"));
	}
}

NDIS_STATUS	RTUSBFreeDescriptorRequest(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			BulkOutPipeId,
	IN	UINT32			NumberRequired)
{
//	UCHAR			FreeNumber = 0;
//	UINT			Index;
	NDIS_STATUS		Status = NDIS_STATUS_FAILURE;
	unsigned long   IrqFlags;
	HT_TX_CONTEXT	*pHTTXContext;


	pHTTXContext = &pAd->TxContext[BulkOutPipeId];
	RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);
	if ((pHTTXContext->CurWritePosition < pHTTXContext->NextBulkOutPosition) && ((pHTTXContext->CurWritePosition + NumberRequired + LOCAL_TXBUF_SIZE) > pHTTXContext->NextBulkOutPosition))
	{

		RTUSB_SET_BULK_FLAG(pAd, (fRTUSB_BULK_OUT_DATA_NORMAL << BulkOutPipeId));
	}
	else if ((pHTTXContext->CurWritePosition == 8) && (pHTTXContext->NextBulkOutPosition < (NumberRequired + LOCAL_TXBUF_SIZE)))
	{
		RTUSB_SET_BULK_FLAG(pAd, (fRTUSB_BULK_OUT_DATA_NORMAL << BulkOutPipeId));
	}
	else if (pHTTXContext->bCurWriting == TRUE)
	{
		DBGPRINT(RT_DEBUG_TRACE,("RTUSBFreeD c3 --> QueIdx=%d, CWPos=%ld, NBOutPos=%ld!\n", BulkOutPipeId, pHTTXContext->CurWritePosition, pHTTXContext->NextBulkOutPosition));
		RTUSB_SET_BULK_FLAG(pAd, (fRTUSB_BULK_OUT_DATA_NORMAL << BulkOutPipeId));
	}
	else
	{
		Status = NDIS_STATUS_SUCCESS;
	}
	RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);


	return (Status);
}

NDIS_STATUS RTUSBFreeDescriptorRelease(
	IN RTMP_ADAPTER *pAd,
	IN UCHAR		BulkOutPipeId)
{
	unsigned long   IrqFlags;
	HT_TX_CONTEXT	*pHTTXContext;

	pHTTXContext = &pAd->TxContext[BulkOutPipeId];
	RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);
	pHTTXContext->bCurWriting = FALSE;
	RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);

	return (NDIS_STATUS_SUCCESS);
}


BOOLEAN	RTUSBNeedQueueBackForAgg(
	IN RTMP_ADAPTER *pAd,
	IN UCHAR		BulkOutPipeId)
{
	unsigned long   IrqFlags;
	HT_TX_CONTEXT	*pHTTXContext;
	BOOLEAN			needQueBack = FALSE;

	pHTTXContext = &pAd->TxContext[BulkOutPipeId];

	RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);
	if ((pHTTXContext->IRPPending == TRUE)  /*&& (pAd->TxSwQueue[BulkOutPipeId].Number == 0) */)
	{
#if 0
		if ((pHTTXContext->CurWritePosition <= 8) &&
			(pHTTXContext->NextBulkOutPosition > 8 && (pHTTXContext->NextBulkOutPosition+MAX_AGGREGATION_SIZE) < MAX_TXBULK_LIMIT))
		{
			needQueBack = TRUE;
		}
		else if ((pHTTXContext->CurWritePosition < pHTTXContext->NextBulkOutPosition) &&
				 ((pHTTXContext->NextBulkOutPosition + MAX_AGGREGATION_SIZE) < MAX_TXBULK_LIMIT))
		{
			needQueBack = TRUE;
		}
#else
		if ((pHTTXContext->CurWritePosition < pHTTXContext->ENextBulkOutPosition) &&
			(((pHTTXContext->ENextBulkOutPosition+MAX_AGGREGATION_SIZE) < MAX_TXBULK_LIMIT) || (pHTTXContext->CurWritePosition > MAX_AGGREGATION_SIZE)))
		{
			needQueBack = TRUE;
		}
#endif
		else if ((pHTTXContext->CurWritePosition > pHTTXContext->ENextBulkOutPosition) &&
				 ((pHTTXContext->ENextBulkOutPosition + MAX_AGGREGATION_SIZE) < pHTTXContext->CurWritePosition))
		{
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
VOID	RTUSBRejectPendingPackets(
	IN	PRTMP_ADAPTER	pAd)
{
	UCHAR			Index;
	PQUEUE_ENTRY	pEntry;
	PNDIS_PACKET	pPacket;
	PQUEUE_HEADER	pQueue;


	for (Index = 0; Index < 4; Index++)
	{
		NdisAcquireSpinLock(&pAd->TxSwQueueLock[Index]);
		while (pAd->TxSwQueue[Index].Head != NULL)
		{
			pQueue = (PQUEUE_HEADER) &(pAd->TxSwQueue[Index]);
			pEntry = RemoveHeadQueue(pQueue);
			pPacket = QUEUE_ENTRY_TO_PACKET(pEntry);
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
		}
		NdisReleaseSpinLock(&pAd->TxSwQueueLock[Index]);

	}

}

VOID RTMPWriteTxInfo(
	IN	PRTMP_ADAPTER	pAd,
	IN	PTXINFO_STRUC 	pTxInfo,
	IN	  USHORT		USBDMApktLen,
	IN	  BOOLEAN		bWiv,
	IN	  UCHAR			QueueSel,
	IN	  UCHAR			NextValid,
	IN	  UCHAR			TxBurst)
{
	pTxInfo->USBDMATxPktLen = USBDMApktLen;
	pTxInfo->QSEL = QueueSel;
	if (QueueSel != FIFO_EDCA)
		DBGPRINT(RT_DEBUG_TRACE, ("====> QueueSel != FIFO_EDCA<============\n"));
	pTxInfo->USBDMANextVLD = FALSE; //NextValid;  // Need to check with Jan about this.
	pTxInfo->USBDMATxburst = TxBurst;
	pTxInfo->WIV = bWiv;
	pTxInfo->SwUseLastRound = 0;
	pTxInfo->rsv = 0;
	pTxInfo->rsv2 = 0;
}

