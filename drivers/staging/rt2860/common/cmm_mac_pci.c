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

#ifdef RTMP_MAC_PCI
#include	"../rt_config.h"

/*
	========================================================================

	Routine Description:
		Allocate DMA memory blocks for send, receive

	Arguments:
		Adapter		Pointer to our adapter

	Return Value:
		NDIS_STATUS_SUCCESS
		NDIS_STATUS_FAILURE
		NDIS_STATUS_RESOURCES

	IRQL = PASSIVE_LEVEL

	Note:

	========================================================================
*/
int RTMPAllocTxRxRingMemory(struct rt_rtmp_adapter *pAd)
{
	int Status = NDIS_STATUS_SUCCESS;
	unsigned long RingBasePaHigh;
	unsigned long RingBasePaLow;
	void *RingBaseVa;
	int index, num;
	struct rt_txd * pTxD;
	struct rt_rxd * pRxD;
	unsigned long ErrorValue = 0;
	struct rt_rtmp_tx_ring *pTxRing;
	struct rt_rtmp_dmabuf *pDmaBuf;
	void *pPacket;
/*      PRTMP_REORDERBUF        pReorderBuf; */

	DBGPRINT(RT_DEBUG_TRACE, ("--> RTMPAllocTxRxRingMemory\n"));
	do {
		/* */
		/* Allocate all ring descriptors, include TxD, RxD, MgmtD. */
		/* Although each size is different, to prevent cacheline and alignment */
		/* issue, I intentional set them all to 64 bytes. */
		/* */
		for (num = 0; num < NUM_OF_TX_RING; num++) {
			unsigned long BufBasePaHigh;
			unsigned long BufBasePaLow;
			void *BufBaseVa;

			/* */
			/* Allocate Tx ring descriptor's memory (5 TX rings = 4 ACs + 1 HCCA) */
			/* */
			pAd->TxDescRing[num].AllocSize =
			    TX_RING_SIZE * TXD_SIZE;
			RTMP_AllocateTxDescMemory(pAd, num,
						  pAd->TxDescRing[num].
						  AllocSize, FALSE,
						  &pAd->TxDescRing[num].AllocVa,
						  &pAd->TxDescRing[num].
						  AllocPa);

			if (pAd->TxDescRing[num].AllocVa == NULL) {
				ErrorValue = ERRLOG_OUT_OF_SHARED_MEMORY;
				DBGPRINT_ERR(("Failed to allocate a big buffer\n"));
				Status = NDIS_STATUS_RESOURCES;
				break;
			}
			/* Zero init this memory block */
			NdisZeroMemory(pAd->TxDescRing[num].AllocVa,
				       pAd->TxDescRing[num].AllocSize);

			/* Save PA & VA for further operation */
			RingBasePaHigh =
			    RTMP_GetPhysicalAddressHigh(pAd->TxDescRing[num].
							AllocPa);
			RingBasePaLow =
			    RTMP_GetPhysicalAddressLow(pAd->TxDescRing[num].
						       AllocPa);
			RingBaseVa = pAd->TxDescRing[num].AllocVa;

			/* */
			/* Allocate all 1st TXBuf's memory for this TxRing */
			/* */
			pAd->TxBufSpace[num].AllocSize =
			    TX_RING_SIZE * TX_DMA_1ST_BUFFER_SIZE;
			RTMP_AllocateFirstTxBuffer(pAd, num,
						   pAd->TxBufSpace[num].
						   AllocSize, FALSE,
						   &pAd->TxBufSpace[num].
						   AllocVa,
						   &pAd->TxBufSpace[num].
						   AllocPa);

			if (pAd->TxBufSpace[num].AllocVa == NULL) {
				ErrorValue = ERRLOG_OUT_OF_SHARED_MEMORY;
				DBGPRINT_ERR(("Failed to allocate a big buffer\n"));
				Status = NDIS_STATUS_RESOURCES;
				break;
			}
			/* Zero init this memory block */
			NdisZeroMemory(pAd->TxBufSpace[num].AllocVa,
				       pAd->TxBufSpace[num].AllocSize);

			/* Save PA & VA for further operation */
			BufBasePaHigh =
			    RTMP_GetPhysicalAddressHigh(pAd->TxBufSpace[num].
							AllocPa);
			BufBasePaLow =
			    RTMP_GetPhysicalAddressLow(pAd->TxBufSpace[num].
						       AllocPa);
			BufBaseVa = pAd->TxBufSpace[num].AllocVa;

			/* */
			/* Initialize Tx Ring Descriptor and associated buffer memory */
			/* */
			pTxRing = &pAd->TxRing[num];
			for (index = 0; index < TX_RING_SIZE; index++) {
				pTxRing->Cell[index].pNdisPacket = NULL;
				pTxRing->Cell[index].pNextNdisPacket = NULL;
				/* Init Tx Ring Size, Va, Pa variables */
				pTxRing->Cell[index].AllocSize = TXD_SIZE;
				pTxRing->Cell[index].AllocVa = RingBaseVa;
				RTMP_SetPhysicalAddressHigh(pTxRing->
							    Cell[index].AllocPa,
							    RingBasePaHigh);
				RTMP_SetPhysicalAddressLow(pTxRing->Cell[index].
							   AllocPa,
							   RingBasePaLow);

				/* Setup Tx Buffer size & address. only 802.11 header will store in this space */
				pDmaBuf = &pTxRing->Cell[index].DmaBuf;
				pDmaBuf->AllocSize = TX_DMA_1ST_BUFFER_SIZE;
				pDmaBuf->AllocVa = BufBaseVa;
				RTMP_SetPhysicalAddressHigh(pDmaBuf->AllocPa,
							    BufBasePaHigh);
				RTMP_SetPhysicalAddressLow(pDmaBuf->AllocPa,
							   BufBasePaLow);

				/* link the pre-allocated TxBuf to TXD */
				pTxD =
				    (struct rt_txd *) pTxRing->Cell[index].AllocVa;
				pTxD->SDPtr0 = BufBasePaLow;
				/* advance to next ring descriptor address */
				pTxD->DMADONE = 1;
				RingBasePaLow += TXD_SIZE;
				RingBaseVa = (u8 *)RingBaseVa + TXD_SIZE;

				/* advance to next TxBuf address */
				BufBasePaLow += TX_DMA_1ST_BUFFER_SIZE;
				BufBaseVa =
				    (u8 *)BufBaseVa + TX_DMA_1ST_BUFFER_SIZE;
			}
			DBGPRINT(RT_DEBUG_TRACE,
				 ("TxRing[%d]: total %d entry allocated\n", num,
				  index));
		}
		if (Status == NDIS_STATUS_RESOURCES)
			break;

		/* */
		/* Allocate MGMT ring descriptor's memory except Tx ring which allocated eariler */
		/* */
		pAd->MgmtDescRing.AllocSize = MGMT_RING_SIZE * TXD_SIZE;
		RTMP_AllocateMgmtDescMemory(pAd,
					    pAd->MgmtDescRing.AllocSize,
					    FALSE,
					    &pAd->MgmtDescRing.AllocVa,
					    &pAd->MgmtDescRing.AllocPa);

		if (pAd->MgmtDescRing.AllocVa == NULL) {
			ErrorValue = ERRLOG_OUT_OF_SHARED_MEMORY;
			DBGPRINT_ERR(("Failed to allocate a big buffer\n"));
			Status = NDIS_STATUS_RESOURCES;
			break;
		}
		/* Zero init this memory block */
		NdisZeroMemory(pAd->MgmtDescRing.AllocVa,
			       pAd->MgmtDescRing.AllocSize);

		/* Save PA & VA for further operation */
		RingBasePaHigh =
		    RTMP_GetPhysicalAddressHigh(pAd->MgmtDescRing.AllocPa);
		RingBasePaLow =
		    RTMP_GetPhysicalAddressLow(pAd->MgmtDescRing.AllocPa);
		RingBaseVa = pAd->MgmtDescRing.AllocVa;

		/* */
		/* Initialize MGMT Ring and associated buffer memory */
		/* */
		for (index = 0; index < MGMT_RING_SIZE; index++) {
			pAd->MgmtRing.Cell[index].pNdisPacket = NULL;
			pAd->MgmtRing.Cell[index].pNextNdisPacket = NULL;
			/* Init MGMT Ring Size, Va, Pa variables */
			pAd->MgmtRing.Cell[index].AllocSize = TXD_SIZE;
			pAd->MgmtRing.Cell[index].AllocVa = RingBaseVa;
			RTMP_SetPhysicalAddressHigh(pAd->MgmtRing.Cell[index].
						    AllocPa, RingBasePaHigh);
			RTMP_SetPhysicalAddressLow(pAd->MgmtRing.Cell[index].
						   AllocPa, RingBasePaLow);

			/* Offset to next ring descriptor address */
			RingBasePaLow += TXD_SIZE;
			RingBaseVa = (u8 *)RingBaseVa + TXD_SIZE;

			/* link the pre-allocated TxBuf to TXD */
			pTxD = (struct rt_txd *) pAd->MgmtRing.Cell[index].AllocVa;
			pTxD->DMADONE = 1;

			/* no pre-allocated buffer required in MgmtRing for scatter-gather case */
		}
		DBGPRINT(RT_DEBUG_TRACE,
			 ("MGMT Ring: total %d entry allocated\n", index));

		/* */
		/* Allocate RX ring descriptor's memory except Tx ring which allocated eariler */
		/* */
		pAd->RxDescRing.AllocSize = RX_RING_SIZE * RXD_SIZE;
		RTMP_AllocateRxDescMemory(pAd,
					  pAd->RxDescRing.AllocSize,
					  FALSE,
					  &pAd->RxDescRing.AllocVa,
					  &pAd->RxDescRing.AllocPa);

		if (pAd->RxDescRing.AllocVa == NULL) {
			ErrorValue = ERRLOG_OUT_OF_SHARED_MEMORY;
			DBGPRINT_ERR(("Failed to allocate a big buffer\n"));
			Status = NDIS_STATUS_RESOURCES;
			break;
		}
		/* Zero init this memory block */
		NdisZeroMemory(pAd->RxDescRing.AllocVa,
			       pAd->RxDescRing.AllocSize);

		DBGPRINT(RT_DEBUG_OFF,
			 ("RX DESC %p  size = %ld\n", pAd->RxDescRing.AllocVa,
			  pAd->RxDescRing.AllocSize));

		/* Save PA & VA for further operation */
		RingBasePaHigh =
		    RTMP_GetPhysicalAddressHigh(pAd->RxDescRing.AllocPa);
		RingBasePaLow =
		    RTMP_GetPhysicalAddressLow(pAd->RxDescRing.AllocPa);
		RingBaseVa = pAd->RxDescRing.AllocVa;

		/* */
		/* Initialize Rx Ring and associated buffer memory */
		/* */
		for (index = 0; index < RX_RING_SIZE; index++) {
			/* Init RX Ring Size, Va, Pa variables */
			pAd->RxRing.Cell[index].AllocSize = RXD_SIZE;
			pAd->RxRing.Cell[index].AllocVa = RingBaseVa;
			RTMP_SetPhysicalAddressHigh(pAd->RxRing.Cell[index].
						    AllocPa, RingBasePaHigh);
			RTMP_SetPhysicalAddressLow(pAd->RxRing.Cell[index].
						   AllocPa, RingBasePaLow);

			/*NdisZeroMemory(RingBaseVa, RXD_SIZE); */

			/* Offset to next ring descriptor address */
			RingBasePaLow += RXD_SIZE;
			RingBaseVa = (u8 *)RingBaseVa + RXD_SIZE;

			/* Setup Rx associated Buffer size & allocate share memory */
			pDmaBuf = &pAd->RxRing.Cell[index].DmaBuf;
			pDmaBuf->AllocSize = RX_BUFFER_AGGRESIZE;
			pPacket = RTMP_AllocateRxPacketBuffer(pAd,
							      pDmaBuf->
							      AllocSize, FALSE,
							      &pDmaBuf->AllocVa,
							      &pDmaBuf->
							      AllocPa);

			/* keep allocated rx packet */
			pAd->RxRing.Cell[index].pNdisPacket = pPacket;

			/* Error handling */
			if (pDmaBuf->AllocVa == NULL) {
				ErrorValue = ERRLOG_OUT_OF_SHARED_MEMORY;
				DBGPRINT_ERR(("Failed to allocate RxRing's 1st buffer\n"));
				Status = NDIS_STATUS_RESOURCES;
				break;
			}
			/* Zero init this memory block */
			NdisZeroMemory(pDmaBuf->AllocVa, pDmaBuf->AllocSize);

			/* Write RxD buffer address & allocated buffer length */
			pRxD = (struct rt_rxd *) pAd->RxRing.Cell[index].AllocVa;
			pRxD->SDP0 =
			    RTMP_GetPhysicalAddressLow(pDmaBuf->AllocPa);
			pRxD->DDONE = 0;

		}

		DBGPRINT(RT_DEBUG_TRACE,
			 ("Rx Ring: total %d entry allocated\n", index));

	} while (FALSE);

	NdisZeroMemory(&pAd->FragFrame, sizeof(struct rt_fragment_frame));
	pAd->FragFrame.pFragPacket =
	    RTMP_AllocateFragPacketBuffer(pAd, RX_BUFFER_NORMSIZE);

	if (pAd->FragFrame.pFragPacket == NULL) {
		Status = NDIS_STATUS_RESOURCES;
	}

	if (Status != NDIS_STATUS_SUCCESS) {
		/* Log error inforamtion */
		NdisWriteErrorLogEntry(pAd->AdapterHandle,
				       NDIS_ERROR_CODE_OUT_OF_RESOURCES,
				       1, ErrorValue);
	}
	/* Following code segment get from original func:NICInitTxRxRingAndBacklogQueue(), now should integrate it to here. */
	{
		DBGPRINT(RT_DEBUG_TRACE,
			 ("--> NICInitTxRxRingAndBacklogQueue\n"));

/*
		// Disable DMA.
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);
		GloCfg.word &= 0xff0;
		GloCfg.field.EnTXWriteBackDDONE =1;
		RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);
*/

		/* Initialize all transmit related software queues */
		for (index = 0; index < NUM_OF_TX_RING; index++) {
			InitializeQueueHeader(&pAd->TxSwQueue[index]);
			/* Init TX rings index pointer */
			pAd->TxRing[index].TxSwFreeIdx = 0;
			pAd->TxRing[index].TxCpuIdx = 0;
			/*RTMP_IO_WRITE32(pAd, (TX_CTX_IDX0 + i * 0x10) ,  pAd->TxRing[i].TX_CTX_IDX); */
		}

		/* Init RX Ring index pointer */
		pAd->RxRing.RxSwReadIdx = 0;
		pAd->RxRing.RxCpuIdx = RX_RING_SIZE - 1;
		/*RTMP_IO_WRITE32(pAd, RX_CRX_IDX, pAd->RxRing.RX_CRX_IDX0); */

		/* init MGMT ring index pointer */
		pAd->MgmtRing.TxSwFreeIdx = 0;
		pAd->MgmtRing.TxCpuIdx = 0;

		pAd->PrivateInfo.TxRingFullCnt = 0;

		DBGPRINT(RT_DEBUG_TRACE,
			 ("<-- NICInitTxRxRingAndBacklogQueue\n"));
	}

	DBGPRINT_S(Status,
		   ("<-- RTMPAllocTxRxRingMemory, Status=%x\n", Status));
	return Status;
}

/*
	========================================================================

	Routine Description:
		Reset NIC Asics. Call after rest DMA. So reset TX_CTX_IDX to zero.

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

	Note:
		Reset NIC to initial state AS IS system boot up time.

	========================================================================
*/
void RTMPRingCleanUp(struct rt_rtmp_adapter *pAd, u8 RingType)
{
	struct rt_txd * pTxD;
	struct rt_rxd * pRxD;
	struct rt_queue_entry *pEntry;
	void *pPacket;
	int i;
	struct rt_rtmp_tx_ring *pTxRing;
	unsigned long IrqFlags;
	/*u32                        RxSwReadIdx; */

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPRingCleanUp(RingIdx=%d, Pending-NDIS=%ld)\n", RingType,
		  pAd->RalinkCounters.PendingNdisPacketCount));
	switch (RingType) {
	case QID_AC_BK:
	case QID_AC_BE:
	case QID_AC_VI:
	case QID_AC_VO:

		pTxRing = &pAd->TxRing[RingType];

		RTMP_IRQ_LOCK(&pAd->irq_lock, IrqFlags);
		/* We have to clean all descriptors in case some error happened with reset */
		for (i = 0; i < TX_RING_SIZE; i++)	/* We have to scan all TX ring */
		{
			pTxD = (struct rt_txd *) pTxRing->Cell[i].AllocVa;

			pPacket = (void *)pTxRing->Cell[i].pNdisPacket;
			/* release scatter-and-gather char */
			if (pPacket) {
				RELEASE_NDIS_PACKET(pAd, pPacket,
						    NDIS_STATUS_FAILURE);
				pTxRing->Cell[i].pNdisPacket = NULL;
			}

			pPacket =
			    (void *)pTxRing->Cell[i].pNextNdisPacket;
			/* release scatter-and-gather char */
			if (pPacket) {
				RELEASE_NDIS_PACKET(pAd, pPacket,
						    NDIS_STATUS_FAILURE);
				pTxRing->Cell[i].pNextNdisPacket = NULL;
			}
		}

		RTMP_IO_READ32(pAd, TX_DTX_IDX0 + RingType * 0x10,
			       &pTxRing->TxDmaIdx);
		pTxRing->TxSwFreeIdx = pTxRing->TxDmaIdx;
		pTxRing->TxCpuIdx = pTxRing->TxDmaIdx;
		RTMP_IO_WRITE32(pAd, TX_CTX_IDX0 + RingType * 0x10,
				pTxRing->TxCpuIdx);

		RTMP_IRQ_UNLOCK(&pAd->irq_lock, IrqFlags);

		RTMP_IRQ_LOCK(&pAd->irq_lock, IrqFlags);
		while (pAd->TxSwQueue[RingType].Head != NULL) {
			pEntry = RemoveHeadQueue(&pAd->TxSwQueue[RingType]);
			pPacket = QUEUE_ENTRY_TO_PACKET(pEntry);
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Release 1 NDIS packet from s/w backlog queue\n"));
		}
		RTMP_IRQ_UNLOCK(&pAd->irq_lock, IrqFlags);
		break;

	case QID_MGMT:
		/* We have to clean all descriptors in case some error happened with reset */
		NdisAcquireSpinLock(&pAd->MgmtRingLock);

		for (i = 0; i < MGMT_RING_SIZE; i++) {
			pTxD = (struct rt_txd *) pAd->MgmtRing.Cell[i].AllocVa;

			pPacket =
			    (void *)pAd->MgmtRing.Cell[i].pNdisPacket;
			/* rlease scatter-and-gather char */
			if (pPacket) {
				PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr0,
						 pTxD->SDLen0,
						 PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket,
						    NDIS_STATUS_FAILURE);
			}
			pAd->MgmtRing.Cell[i].pNdisPacket = NULL;

			pPacket =
			    (void *)pAd->MgmtRing.Cell[i].
			    pNextNdisPacket;
			/* release scatter-and-gather char */
			if (pPacket) {
				PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1,
						 pTxD->SDLen1,
						 PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket,
						    NDIS_STATUS_FAILURE);
			}
			pAd->MgmtRing.Cell[i].pNextNdisPacket = NULL;

		}

		RTMP_IO_READ32(pAd, TX_MGMTDTX_IDX, &pAd->MgmtRing.TxDmaIdx);
		pAd->MgmtRing.TxSwFreeIdx = pAd->MgmtRing.TxDmaIdx;
		pAd->MgmtRing.TxCpuIdx = pAd->MgmtRing.TxDmaIdx;
		RTMP_IO_WRITE32(pAd, TX_MGMTCTX_IDX, pAd->MgmtRing.TxCpuIdx);

		NdisReleaseSpinLock(&pAd->MgmtRingLock);
		pAd->RalinkCounters.MgmtRingFullCount = 0;
		break;

	case QID_RX:
		/* We have to clean all descriptors in case some error happened with reset */
		NdisAcquireSpinLock(&pAd->RxRingLock);

		for (i = 0; i < RX_RING_SIZE; i++) {
			pRxD = (struct rt_rxd *) pAd->RxRing.Cell[i].AllocVa;
			pRxD->DDONE = 0;
		}

		RTMP_IO_READ32(pAd, RX_DRX_IDX, &pAd->RxRing.RxDmaIdx);
		pAd->RxRing.RxSwReadIdx = pAd->RxRing.RxDmaIdx;
		pAd->RxRing.RxCpuIdx =
		    ((pAd->RxRing.RxDmaIdx ==
		      0) ? (RX_RING_SIZE - 1) : (pAd->RxRing.RxDmaIdx - 1));
		RTMP_IO_WRITE32(pAd, RX_CRX_IDX, pAd->RxRing.RxCpuIdx);

		NdisReleaseSpinLock(&pAd->RxRingLock);
		break;

	default:
		break;
	}
}

void RTMPFreeTxRxRingMemory(struct rt_rtmp_adapter *pAd)
{
	int index, num, j;
	struct rt_rtmp_tx_ring *pTxRing;
	struct rt_txd * pTxD;
	void *pPacket;
	unsigned int IrqFlags;

	/*struct os_cookie *pObj =(struct os_cookie *)pAd->OS_Cookie; */

	DBGPRINT(RT_DEBUG_TRACE, ("--> RTMPFreeTxRxRingMemory\n"));

	/* Free TxSwQueue Packet */
	for (index = 0; index < NUM_OF_TX_RING; index++) {
		struct rt_queue_entry *pEntry;
		void *pPacket;
		struct rt_queue_header *pQueue;

		RTMP_IRQ_LOCK(&pAd->irq_lock, IrqFlags);
		pQueue = &pAd->TxSwQueue[index];
		while (pQueue->Head) {
			pEntry = RemoveHeadQueue(pQueue);
			pPacket = QUEUE_ENTRY_TO_PACKET(pEntry);
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
		}
		RTMP_IRQ_UNLOCK(&pAd->irq_lock, IrqFlags);
	}

	/* Free Tx Ring Packet */
	for (index = 0; index < NUM_OF_TX_RING; index++) {
		pTxRing = &pAd->TxRing[index];

		for (j = 0; j < TX_RING_SIZE; j++) {
			pTxD = (struct rt_txd *) (pTxRing->Cell[j].AllocVa);
			pPacket = pTxRing->Cell[j].pNdisPacket;

			if (pPacket) {
				PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr0,
						 pTxD->SDLen0,
						 PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket,
						    NDIS_STATUS_SUCCESS);
			}
			/*Always assign pNdisPacket as NULL after clear */
			pTxRing->Cell[j].pNdisPacket = NULL;

			pPacket = pTxRing->Cell[j].pNextNdisPacket;

			if (pPacket) {
				PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1,
						 pTxD->SDLen1,
						 PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket,
						    NDIS_STATUS_SUCCESS);
			}
			/*Always assign pNextNdisPacket as NULL after clear */
			pTxRing->Cell[pTxRing->TxSwFreeIdx].pNextNdisPacket =
			    NULL;

		}
	}

	for (index = RX_RING_SIZE - 1; index >= 0; index--) {
		if ((pAd->RxRing.Cell[index].DmaBuf.AllocVa)
		    && (pAd->RxRing.Cell[index].pNdisPacket)) {
			PCI_UNMAP_SINGLE(pAd,
					 pAd->RxRing.Cell[index].DmaBuf.AllocPa,
					 pAd->RxRing.Cell[index].DmaBuf.
					 AllocSize, PCI_DMA_FROMDEVICE);
			RELEASE_NDIS_PACKET(pAd,
					    pAd->RxRing.Cell[index].pNdisPacket,
					    NDIS_STATUS_SUCCESS);
		}
	}
	NdisZeroMemory(pAd->RxRing.Cell, RX_RING_SIZE * sizeof(struct rt_rtmp_dmacb));

	if (pAd->RxDescRing.AllocVa) {
		RTMP_FreeDescMemory(pAd, pAd->RxDescRing.AllocSize,
				    pAd->RxDescRing.AllocVa,
				    pAd->RxDescRing.AllocPa);
	}
	NdisZeroMemory(&pAd->RxDescRing, sizeof(struct rt_rtmp_dmabuf));

	if (pAd->MgmtDescRing.AllocVa) {
		RTMP_FreeDescMemory(pAd, pAd->MgmtDescRing.AllocSize,
				    pAd->MgmtDescRing.AllocVa,
				    pAd->MgmtDescRing.AllocPa);
	}
	NdisZeroMemory(&pAd->MgmtDescRing, sizeof(struct rt_rtmp_dmabuf));

	for (num = 0; num < NUM_OF_TX_RING; num++) {
		if (pAd->TxBufSpace[num].AllocVa) {
			RTMP_FreeFirstTxBuffer(pAd,
					       pAd->TxBufSpace[num].AllocSize,
					       FALSE,
					       pAd->TxBufSpace[num].AllocVa,
					       pAd->TxBufSpace[num].AllocPa);
		}
		NdisZeroMemory(&pAd->TxBufSpace[num], sizeof(struct rt_rtmp_dmabuf));

		if (pAd->TxDescRing[num].AllocVa) {
			RTMP_FreeDescMemory(pAd, pAd->TxDescRing[num].AllocSize,
					    pAd->TxDescRing[num].AllocVa,
					    pAd->TxDescRing[num].AllocPa);
		}
		NdisZeroMemory(&pAd->TxDescRing[num], sizeof(struct rt_rtmp_dmabuf));
	}

	if (pAd->FragFrame.pFragPacket)
		RELEASE_NDIS_PACKET(pAd, pAd->FragFrame.pFragPacket,
				    NDIS_STATUS_SUCCESS);

	DBGPRINT(RT_DEBUG_TRACE, ("<-- RTMPFreeTxRxRingMemory\n"));
}

/***************************************************************************
  *
  *	register related procedures.
  *
  **************************************************************************/
/*
========================================================================
Routine Description:
    Disable DMA.

Arguments:
	*pAd				the raxx interface data pointer

Return Value:
	None

Note:
========================================================================
*/
void RT28XXDMADisable(struct rt_rtmp_adapter *pAd)
{
	WPDMA_GLO_CFG_STRUC GloCfg;

	RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);
	GloCfg.word &= 0xff0;
	GloCfg.field.EnTXWriteBackDDONE = 1;
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);
}

/*
========================================================================
Routine Description:
    Enable DMA.

Arguments:
	*pAd				the raxx interface data pointer

Return Value:
	None

Note:
========================================================================
*/
void RT28XXDMAEnable(struct rt_rtmp_adapter *pAd)
{
	WPDMA_GLO_CFG_STRUC GloCfg;
	int i = 0;

	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x4);
	do {
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);
		if ((GloCfg.field.TxDMABusy == 0)
		    && (GloCfg.field.RxDMABusy == 0))
			break;

		DBGPRINT(RT_DEBUG_TRACE, ("==>  DMABusy\n"));
		RTMPusecDelay(1000);
		i++;
	} while (i < 200);

	RTMPusecDelay(50);

	GloCfg.field.EnTXWriteBackDDONE = 1;
	GloCfg.field.WPDMABurstSIZE = 2;
	GloCfg.field.EnableRxDMA = 1;
	GloCfg.field.EnableTxDMA = 1;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("<== WRITE DMA offset 0x208 = 0x%x\n", GloCfg.word));
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);

}

BOOLEAN AsicCheckCommanOk(struct rt_rtmp_adapter *pAd, u8 Command)
{
	u32 CmdStatus = 0, CID = 0, i;
	u32 ThisCIDMask = 0;

	i = 0;
	do {
		RTMP_IO_READ32(pAd, H2M_MAILBOX_CID, &CID);
		/* Find where the command is. Because this is randomly specified by firmware. */
		if ((CID & CID0MASK) == Command) {
			ThisCIDMask = CID0MASK;
			break;
		} else if ((((CID & CID1MASK) >> 8) & 0xff) == Command) {
			ThisCIDMask = CID1MASK;
			break;
		} else if ((((CID & CID2MASK) >> 16) & 0xff) == Command) {
			ThisCIDMask = CID2MASK;
			break;
		} else if ((((CID & CID3MASK) >> 24) & 0xff) == Command) {
			ThisCIDMask = CID3MASK;
			break;
		}

		RTMPusecDelay(100);
		i++;
	} while (i < 200);

	/* Get CommandStatus Value */
	RTMP_IO_READ32(pAd, H2M_MAILBOX_STATUS, &CmdStatus);

	/* This command's status is at the same position as command. So AND command position's bitmask to read status. */
	if (i < 200) {
		/* If Status is 1, the comamnd is success. */
		if (((CmdStatus & ThisCIDMask) == 0x1)
		    || ((CmdStatus & ThisCIDMask) == 0x100)
		    || ((CmdStatus & ThisCIDMask) == 0x10000)
		    || ((CmdStatus & ThisCIDMask) == 0x1000000)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("--> AsicCheckCommanOk CID = 0x%x, CmdStatus= 0x%x \n",
				  CID, CmdStatus));
			RTMP_IO_WRITE32(pAd, H2M_MAILBOX_STATUS, 0xffffffff);
			RTMP_IO_WRITE32(pAd, H2M_MAILBOX_CID, 0xffffffff);
			return TRUE;
		}
		DBGPRINT(RT_DEBUG_TRACE,
			 ("--> AsicCheckCommanFail1 CID = 0x%x, CmdStatus= 0x%x \n",
			  CID, CmdStatus));
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("--> AsicCheckCommanFail2 Timeout Command = %d, CmdStatus= 0x%x \n",
			  Command, CmdStatus));
	}
	/* Clear Command and Status. */
	RTMP_IO_WRITE32(pAd, H2M_MAILBOX_STATUS, 0xffffffff);
	RTMP_IO_WRITE32(pAd, H2M_MAILBOX_CID, 0xffffffff);

	return FALSE;
}

/*
========================================================================
Routine Description:
    Write Beacon buffer to Asic.

Arguments:
	*pAd				the raxx interface data pointer

Return Value:
	None

Note:
========================================================================
*/
void RT28xx_UpdateBeaconToAsic(struct rt_rtmp_adapter *pAd,
			       int apidx,
			       unsigned long FrameLen, unsigned long UpdatePos)
{
	unsigned long CapInfoPos = 0;
	u8 *ptr, *ptr_update, *ptr_capinfo;
	u32 i;
	BOOLEAN bBcnReq = FALSE;
	u8 bcn_idx = 0;

	{
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s() : No valid Interface be found.\n", __func__));
		return;
	}

	/*if ((pAd->WdsTab.Mode == WDS_BRIDGE_MODE) */
	/*      || ((pAd->ApCfg.MBSSID[apidx].MSSIDDev == NULL) */
	/*              || !(pAd->ApCfg.MBSSID[apidx].MSSIDDev->flags & IFF_UP)) */
	/*      ) */
	if (bBcnReq == FALSE) {
		/* when the ra interface is down, do not send its beacon frame */
		/* clear all zero */
		for (i = 0; i < TXWI_SIZE; i += 4)
			RTMP_IO_WRITE32(pAd, pAd->BeaconOffset[bcn_idx] + i,
					0x00);
	} else {
		ptr = (u8 *)& pAd->BeaconTxWI;
		for (i = 0; i < TXWI_SIZE; i += 4)	/* 16-byte TXWI field */
		{
			u32 longptr =
			    *ptr + (*(ptr + 1) << 8) + (*(ptr + 2) << 16) +
			    (*(ptr + 3) << 24);
			RTMP_IO_WRITE32(pAd, pAd->BeaconOffset[bcn_idx] + i,
					longptr);
			ptr += 4;
		}

		/* Update CapabilityInfo in Beacon */
		for (i = CapInfoPos; i < (CapInfoPos + 2); i++) {
			RTMP_IO_WRITE8(pAd,
				       pAd->BeaconOffset[bcn_idx] + TXWI_SIZE +
				       i, *ptr_capinfo);
			ptr_capinfo++;
		}

		if (FrameLen > UpdatePos) {
			for (i = UpdatePos; i < (FrameLen); i++) {
				RTMP_IO_WRITE8(pAd,
					       pAd->BeaconOffset[bcn_idx] +
					       TXWI_SIZE + i, *ptr_update);
				ptr_update++;
			}
		}

	}

}

void RT28xxPciStaAsicForceWakeup(struct rt_rtmp_adapter *pAd, IN BOOLEAN bFromTx)
{
	AUTO_WAKEUP_STRUC AutoWakeupCfg;

	if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
		return;

	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WAKEUP_NOW)) {
		DBGPRINT(RT_DEBUG_TRACE, ("waking up now!\n"));
		return;
	}

	OPSTATUS_SET_FLAG(pAd, fOP_STATUS_WAKEUP_NOW);

	RTMP_CLEAR_PSFLAG(pAd, fRTMP_PS_GO_TO_SLEEP_NOW);

	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)
	    && pAd->StaCfg.PSControl.field.EnableNewPS == TRUE) {
		/* Support PCIe Advance Power Save */
		if (bFromTx == TRUE && (pAd->Mlme.bPsPollTimerRunning == TRUE)) {
			pAd->Mlme.bPsPollTimerRunning = FALSE;
			RTMPPCIeLinkCtrlValueRestore(pAd, RESTORE_WAKEUP);
			RTMPusecDelay(3000);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("=======AsicForceWakeup===bFromTx\n"));
		}

		AutoWakeupCfg.word = 0;
		RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);

		if (RT28xxPciAsicRadioOn(pAd, DOT11POWERSAVE)) {
#ifdef PCIE_PS_SUPPORT
			/* add by johnli, RF power sequence setup, load RF normal operation-mode setup */
			if ((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
			    && IS_VERSION_AFTER_F(pAd)) {
				struct rt_rtmp_chip_op *pChipOps = &pAd->chipOps;

				if (pChipOps->AsicReverseRfFromSleepMode)
					pChipOps->
					    AsicReverseRfFromSleepMode(pAd);
			} else
#endif /* PCIE_PS_SUPPORT // */
			{
				/* end johnli */
				/* In Radio Off, we turn off RF clk, So now need to call ASICSwitchChannel again. */
				if (INFRA_ON(pAd)
				    && (pAd->CommonCfg.CentralChannel !=
					pAd->CommonCfg.Channel)
				    && (pAd->MlmeAux.HtCapability.HtCapInfo.
					ChannelWidth == BW_40)) {
					/* Must using 40MHz. */
					AsicSwitchChannel(pAd,
							  pAd->CommonCfg.
							  CentralChannel,
							  FALSE);
					AsicLockChannel(pAd,
							pAd->CommonCfg.
							CentralChannel);
				} else {
					/* Must using 20MHz. */
					AsicSwitchChannel(pAd,
							  pAd->CommonCfg.
							  Channel, FALSE);
					AsicLockChannel(pAd,
							pAd->CommonCfg.Channel);
				}
			}
		}
#ifdef PCIE_PS_SUPPORT
		/* 3090 MCU Wakeup command needs more time to be stable. */
		/* Before stable, don't issue other MCU command to prevent from firmware error. */
		if (((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
		     && IS_VERSION_AFTER_F(pAd)) && IS_VERSION_AFTER_F(pAd)
		    && (pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3)
		    && (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("<==RT28xxPciStaAsicForceWakeup::Release the MCU Lock(3090)\n"));
			RTMP_SEM_LOCK(&pAd->McuCmdLock);
			pAd->brt30xxBanMcuCmd = FALSE;
			RTMP_SEM_UNLOCK(&pAd->McuCmdLock);
		}
#endif /* PCIE_PS_SUPPORT // */
	} else {
		/* PCI, 2860-PCIe */
		DBGPRINT(RT_DEBUG_TRACE,
			 ("<==RT28xxPciStaAsicForceWakeup::Original PCI Power Saving\n"));
		AsicSendCommandToMcu(pAd, 0x31, 0xff, 0x00, 0x02);
		AutoWakeupCfg.word = 0;
		RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);
	}

	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_WAKEUP_NOW);
	DBGPRINT(RT_DEBUG_TRACE, ("<=======RT28xxPciStaAsicForceWakeup\n"));
}

void RT28xxPciStaAsicSleepThenAutoWakeup(struct rt_rtmp_adapter *pAd,
					 u16 TbttNumToNextWakeUp)
{
	BOOLEAN brc;

	if (pAd->StaCfg.bRadio == FALSE) {
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
		return;
	}
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)
	    && pAd->StaCfg.PSControl.field.EnableNewPS == TRUE) {
		unsigned long Now = 0;
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WAKEUP_NOW)) {
			DBGPRINT(RT_DEBUG_TRACE, ("waking up now!\n"));
			OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
			return;
		}

		NdisGetSystemUpTime(&Now);
		/* If last send NULL fram time is too close to this receiving beacon (within 8ms), don't go to sleep for this DTM. */
		/* Because Some AP can't queuing outgoing frames immediately. */
		if (((pAd->Mlme.LastSendNULLpsmTime + 8) >= Now)
		    && (pAd->Mlme.LastSendNULLpsmTime <= Now)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Now = %lu, LastSendNULLpsmTime=%lu :  RxCountSinceLastNULL = %lu. \n",
				  Now, pAd->Mlme.LastSendNULLpsmTime,
				  pAd->RalinkCounters.RxCountSinceLastNULL));
			return;
		} else if ((pAd->RalinkCounters.RxCountSinceLastNULL > 0)
			   &&
			   ((pAd->Mlme.LastSendNULLpsmTime +
			     pAd->CommonCfg.BeaconPeriod) >= Now)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Now = %lu, LastSendNULLpsmTime=%lu: RxCountSinceLastNULL = %lu > 0 \n",
				  Now, pAd->Mlme.LastSendNULLpsmTime,
				  pAd->RalinkCounters.RxCountSinceLastNULL));
			return;
		}

		brc =
		    RT28xxPciAsicRadioOff(pAd, DOT11POWERSAVE,
					  TbttNumToNextWakeUp);
		if (brc == TRUE)
			OPSTATUS_SET_FLAG(pAd, fOP_STATUS_DOZE);
	} else {
		AUTO_WAKEUP_STRUC AutoWakeupCfg;
		/* we have decided to SLEEP, so at least do it for a BEACON period. */
		if (TbttNumToNextWakeUp == 0)
			TbttNumToNextWakeUp = 1;

		/*RTMP_IO_WRITE32(pAd, INT_MASK_CSR, AutoWakeupInt); */

		AutoWakeupCfg.word = 0;
		RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);
		AutoWakeupCfg.field.NumofSleepingTbtt = TbttNumToNextWakeUp - 1;
		AutoWakeupCfg.field.EnableAutoWakeup = 1;
		AutoWakeupCfg.field.AutoLeadTime = 5;
		RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);
		AsicSendCommandToMcu(pAd, 0x30, 0xff, 0xff, 0x00);	/* send POWER-SAVE command to MCU. Timeout 40us. */
		OPSTATUS_SET_FLAG(pAd, fOP_STATUS_DOZE);
		DBGPRINT(RT_DEBUG_TRACE,
			 ("<-- %s, TbttNumToNextWakeUp=%d \n", __func__,
			  TbttNumToNextWakeUp));
	}

}

void PsPollWakeExec(void *SystemSpecific1,
		    void *FunctionContext,
		    void *SystemSpecific2, void *SystemSpecific3)
{
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)FunctionContext;
	unsigned long flags;

	DBGPRINT(RT_DEBUG_TRACE, ("-->PsPollWakeExec \n"));
	RTMP_INT_LOCK(&pAd->irq_lock, flags);
	if (pAd->Mlme.bPsPollTimerRunning) {
		RTMPPCIeLinkCtrlValueRestore(pAd, RESTORE_WAKEUP);
	}
	pAd->Mlme.bPsPollTimerRunning = FALSE;
	RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
#ifdef PCIE_PS_SUPPORT
	/* For rt30xx power solution 3, Use software timer to wake up in psm. So call */
	/* AsicForceWakeup here instead of handling twakeup interrupt. */
	if (((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
	     && IS_VERSION_AFTER_F(pAd))
	    && (pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3)
	    && (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("<--PsPollWakeExec::3090 calls AsicForceWakeup(pAd, DOT11POWERSAVE) in advance \n"));
		AsicForceWakeup(pAd, DOT11POWERSAVE);
	}
#endif /* PCIE_PS_SUPPORT // */
}

void RadioOnExec(void *SystemSpecific1,
		 void *FunctionContext,
		 void *SystemSpecific2, void *SystemSpecific3)
{
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)FunctionContext;
	struct rt_rtmp_chip_op *pChipOps = &pAd->chipOps;
	WPDMA_GLO_CFG_STRUC DmaCfg;
	BOOLEAN Cancelled;

	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("-->RadioOnExec() return on fOP_STATUS_DOZE == TRUE; \n"));
/*KH Debug: Add the compile flag "RT2860 and condition */
#ifdef RTMP_PCI_SUPPORT
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)
		    && pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)
			RTMPSetTimer(&pAd->Mlme.RadioOnOffTimer, 10);
#endif /* RTMP_PCI_SUPPORT // */
		return;
	}

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("-->RadioOnExec() return on SCAN_IN_PROGRESS; \n"));
#ifdef RTMP_PCI_SUPPORT
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)
		    && pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)
			RTMPSetTimer(&pAd->Mlme.RadioOnOffTimer, 10);
#endif /* RTMP_PCI_SUPPORT // */
		return;
	}
/*KH Debug: need to check. I add the compile flag "CONFIG_STA_SUPPORT" to enclose the following codes. */
#ifdef RTMP_PCI_SUPPORT
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)
	    && pAd->StaCfg.PSControl.field.EnableNewPS == TRUE) {
		pAd->Mlme.bPsPollTimerRunning = FALSE;
		RTMPCancelTimer(&pAd->Mlme.PsPollTimer, &Cancelled);
	}
#endif /* RTMP_PCI_SUPPORT // */
	if (pAd->StaCfg.bRadio == TRUE) {
		pAd->bPCIclkOff = FALSE;
		RTMPRingCleanUp(pAd, QID_AC_BK);
		RTMPRingCleanUp(pAd, QID_AC_BE);
		RTMPRingCleanUp(pAd, QID_AC_VI);
		RTMPRingCleanUp(pAd, QID_AC_VO);
		RTMPRingCleanUp(pAd, QID_MGMT);
		RTMPRingCleanUp(pAd, QID_RX);

		/* 2. Send wake up command. */
		AsicSendCommandToMcu(pAd, 0x31, PowerWakeCID, 0x00, 0x02);
		/* 2-1. wait command ok. */
		AsicCheckCommanOk(pAd, PowerWakeCID);

		/* When PCI clock is off, don't want to service interrupt. So when back to clock on, enable interrupt. */
		/*RTMP_IO_WRITE32(pAd, INT_MASK_CSR, (DELAYINTMASK|RxINT)); */
		RTMP_ASIC_INTERRUPT_ENABLE(pAd);

		/* 3. Enable Tx DMA. */
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &DmaCfg.word);
		DmaCfg.field.EnableTxDMA = 1;
		RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, DmaCfg.word);

		/* In Radio Off, we turn off RF clk, So now need to call ASICSwitchChannel again. */
		if (INFRA_ON(pAd)
		    && (pAd->CommonCfg.CentralChannel != pAd->CommonCfg.Channel)
		    && (pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth ==
			BW_40)) {
			/* Must using 40MHz. */
			AsicSwitchChannel(pAd, pAd->CommonCfg.CentralChannel,
					  FALSE);
			AsicLockChannel(pAd, pAd->CommonCfg.CentralChannel);
		} else {
			/* Must using 20MHz. */
			AsicSwitchChannel(pAd, pAd->CommonCfg.Channel, FALSE);
			AsicLockChannel(pAd, pAd->CommonCfg.Channel);
		}

/*KH Debug:The following codes should be enclosed by RT3090 compile flag */
		if (pChipOps->AsicReverseRfFromSleepMode)
			pChipOps->AsicReverseRfFromSleepMode(pAd);

#ifdef PCIE_PS_SUPPORT
/* 3090 MCU Wakeup command needs more time to be stable. */
/* Before stable, don't issue other MCU command to prevent from firmware error. */
		if ((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
		    && IS_VERSION_AFTER_F(pAd)
		    && (pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3)
		    && (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)) {
			RTMP_SEM_LOCK(&pAd->McuCmdLock);
			pAd->brt30xxBanMcuCmd = FALSE;
			RTMP_SEM_UNLOCK(&pAd->McuCmdLock);
		}
#endif /* PCIE_PS_SUPPORT // */

		/* Clear Radio off flag */
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

		/* Set LED */
		RTMPSetLED(pAd, LED_RADIO_ON);

		if (pAd->StaCfg.Psm == PWR_ACTIVE) {
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3,
						     pAd->StaCfg.BBPR3);
		}
	} else {
		RT28xxPciAsicRadioOff(pAd, GUIRADIO_OFF, 0);
	}
}

/*
	==========================================================================
	Description:
		This routine sends command to firmware and turn our chip to wake up mode from power save mode.
		Both RadioOn and .11 power save function needs to call this routine.
	Input:
		Level = GUIRADIO_OFF : call this function is from Radio Off to Radio On.  Need to restore PCI host value.
		Level = other value : normal wake up function.

	==========================================================================
 */
BOOLEAN RT28xxPciAsicRadioOn(struct rt_rtmp_adapter *pAd, u8 Level)
{
	/*WPDMA_GLO_CFG_STRUC       DmaCfg; */
	BOOLEAN Cancelled;
	/*u32                        MACValue; */

	if (pAd->OpMode == OPMODE_AP && Level == DOT11POWERSAVE)
		return FALSE;

	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)) {
		if (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE) {
			pAd->Mlme.bPsPollTimerRunning = FALSE;
			RTMPCancelTimer(&pAd->Mlme.PsPollTimer, &Cancelled);
		}
		if ((pAd->StaCfg.PSControl.field.EnableNewPS == TRUE &&
		     (Level == GUIRADIO_OFF || Level == GUI_IDLE_POWER_SAVE)) ||
		    RTMP_TEST_PSFLAG(pAd, fRTMP_PS_SET_PCI_CLK_OFF_COMMAND)) {
			/* Some chips don't need to delay 6ms, so copy RTMPPCIePowerLinkCtrlRestore */
			/* return condition here. */
			/*
			   if (((pAd->MACVersion&0xffff0000) != 0x28600000)
			   && ((pAd->DeviceID == NIC2860_PCIe_DEVICE_ID)
			   ||(pAd->DeviceID == NIC2790_PCIe_DEVICE_ID)))
			 */
			{
				DBGPRINT(RT_DEBUG_TRACE,
					 ("RT28xxPciAsicRadioOn ()\n"));
				/* 1. Set PCI Link Control in Configuration Space. */
				RTMPPCIeLinkCtrlValueRestore(pAd,
							     RESTORE_WAKEUP);
				RTMPusecDelay(6000);
			}
		}
	}
#ifdef PCIE_PS_SUPPORT
	if (!
	    (((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
	      && IS_VERSION_AFTER_F(pAd)
	      && (pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3)
	      && (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE))))
#endif /* PCIE_PS_SUPPORT // */
	{
		pAd->bPCIclkOff = FALSE;
		DBGPRINT(RT_DEBUG_TRACE,
			 ("PSM :309xbPCIclkOff == %d\n", pAd->bPCIclkOff));
	}
	/* 2. Send wake up command. */
	AsicSendCommandToMcu(pAd, 0x31, PowerWakeCID, 0x00, 0x02);
	pAd->bPCIclkOff = FALSE;
	/* 2-1. wait command ok. */
	AsicCheckCommanOk(pAd, PowerWakeCID);
	RTMP_ASIC_INTERRUPT_ENABLE(pAd);

	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF);
	if (Level == GUI_IDLE_POWER_SAVE) {
#ifdef  PCIE_PS_SUPPORT

		/* add by johnli, RF power sequence setup, load RF normal operation-mode setup */
		if ((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))) {
			struct rt_rtmp_chip_op *pChipOps = &pAd->chipOps;

			if (pChipOps->AsicReverseRfFromSleepMode)
				pChipOps->AsicReverseRfFromSleepMode(pAd);
			/* 3090 MCU Wakeup command needs more time to be stable. */
			/* Before stable, don't issue other MCU command to prevent from firmware error. */
			if ((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
			    && IS_VERSION_AFTER_F(pAd)
			    && (pAd->StaCfg.PSControl.field.rt30xxPowerMode ==
				3)
			    && (pAd->StaCfg.PSControl.field.EnableNewPS ==
				TRUE)) {
				RTMP_SEM_LOCK(&pAd->McuCmdLock);
				pAd->brt30xxBanMcuCmd = FALSE;
				RTMP_SEM_UNLOCK(&pAd->McuCmdLock);
			}
		} else
			/* end johnli */
#endif /* PCIE_PS_SUPPORT // */
		{
			/* In Radio Off, we turn off RF clk, So now need to call ASICSwitchChannel again. */
			{
				if (INFRA_ON(pAd)
				    && (pAd->CommonCfg.CentralChannel !=
					pAd->CommonCfg.Channel)
				    && (pAd->MlmeAux.HtCapability.HtCapInfo.
					ChannelWidth == BW_40)) {
					/* Must using 40MHz. */
					AsicSwitchChannel(pAd,
							  pAd->CommonCfg.
							  CentralChannel,
							  FALSE);
					AsicLockChannel(pAd,
							pAd->CommonCfg.
							CentralChannel);
				} else {
					/* Must using 20MHz. */
					AsicSwitchChannel(pAd,
							  pAd->CommonCfg.
							  Channel, FALSE);
					AsicLockChannel(pAd,
							pAd->CommonCfg.Channel);
				}
			}

		}
	}
	return TRUE;

}

/*
	==========================================================================
	Description:
		This routine sends command to firmware and turn our chip to power save mode.
		Both RadioOff and .11 power save function needs to call this routine.
	Input:
		Level = GUIRADIO_OFF  : GUI Radio Off mode
		Level = DOT11POWERSAVE  : 802.11 power save mode
		Level = RTMP_HALT  : When Disable device.

	==========================================================================
 */
BOOLEAN RT28xxPciAsicRadioOff(struct rt_rtmp_adapter *pAd,
			      u8 Level, u16 TbttNumToNextWakeUp)
{
	WPDMA_GLO_CFG_STRUC DmaCfg;
	u8 i, tempBBP_R3 = 0;
	BOOLEAN brc = FALSE, Cancelled;
	u32 TbTTTime = 0;
	u32 PsPollTime = 0 /*, MACValue */ ;
	unsigned long BeaconPeriodTime;
	u32 RxDmaIdx, RxCpuIdx;
	DBGPRINT(RT_DEBUG_TRACE,
		 ("AsicRadioOff ===> Lv= %d, TxCpuIdx = %d, TxDmaIdx = %d. RxCpuIdx = %d, RxDmaIdx = %d.\n",
		  Level, pAd->TxRing[0].TxCpuIdx, pAd->TxRing[0].TxDmaIdx,
		  pAd->RxRing.RxCpuIdx, pAd->RxRing.RxDmaIdx));

	if (pAd->OpMode == OPMODE_AP && Level == DOT11POWERSAVE)
		return FALSE;

	/* Check Rx DMA busy status, if more than half is occupied, give up this radio off. */
	RTMP_IO_READ32(pAd, RX_DRX_IDX, &RxDmaIdx);
	RTMP_IO_READ32(pAd, RX_CRX_IDX, &RxCpuIdx);
	if ((RxDmaIdx > RxCpuIdx) && ((RxDmaIdx - RxCpuIdx) > RX_RING_SIZE / 3)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("AsicRadioOff ===> return1. RxDmaIdx = %d ,  RxCpuIdx = %d. \n",
			  RxDmaIdx, RxCpuIdx));
		return FALSE;
	} else if ((RxCpuIdx >= RxDmaIdx)
		   && ((RxCpuIdx - RxDmaIdx) < RX_RING_SIZE / 3)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("AsicRadioOff ===> return2.  RxCpuIdx = %d. RxDmaIdx = %d ,  \n",
			  RxCpuIdx, RxDmaIdx));
		return FALSE;
	}
	/* Once go into this function, disable tx because don't want too many packets in queue to prevent HW stops. */
	/*pAd->bPCIclkOffDisableTx = TRUE; */
	RTMP_SET_PSFLAG(pAd, fRTMP_PS_DISABLE_TX);
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)
	    && pAd->OpMode == OPMODE_STA
	    && pAd->StaCfg.PSControl.field.EnableNewPS == TRUE) {
		RTMPCancelTimer(&pAd->Mlme.RadioOnOffTimer, &Cancelled);
		RTMPCancelTimer(&pAd->Mlme.PsPollTimer, &Cancelled);

		if (Level == DOT11POWERSAVE) {
			RTMP_IO_READ32(pAd, TBTT_TIMER, &TbTTTime);
			TbTTTime &= 0x1ffff;
			/* 00. check if need to do sleep in this DTIM period.   If next beacon will arrive within 30ms , ...doesn't necessarily sleep. */
			/* TbTTTime uint = 64us, LEAD_TIME unit = 1024us, PsPollTime unit = 1ms */
			if (((64 * TbTTTime) < ((LEAD_TIME * 1024) + 40000))
			    && (TbttNumToNextWakeUp == 0)) {
				DBGPRINT(RT_DEBUG_TRACE,
					 ("TbTTTime = 0x%x , give up this sleep. \n",
					  TbTTTime));
				OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
				/*pAd->bPCIclkOffDisableTx = FALSE; */
				RTMP_CLEAR_PSFLAG(pAd, fRTMP_PS_DISABLE_TX);
				return FALSE;
			} else {
				PsPollTime =
				    (64 * TbTTTime - LEAD_TIME * 1024) / 1000;
#ifdef PCIE_PS_SUPPORT
				if ((IS_RT3090(pAd) || IS_RT3572(pAd)
				     || IS_RT3390(pAd))
				    && IS_VERSION_AFTER_F(pAd)
				    && (pAd->StaCfg.PSControl.field.
					rt30xxPowerMode == 3)
				    && (pAd->StaCfg.PSControl.field.
					EnableNewPS == TRUE)) {
					PsPollTime -= 5;
				} else
#endif /* PCIE_PS_SUPPORT // */
					PsPollTime -= 3;

				BeaconPeriodTime =
				    pAd->CommonCfg.BeaconPeriod * 102 / 100;
				if (TbttNumToNextWakeUp > 0)
					PsPollTime +=
					    ((TbttNumToNextWakeUp -
					      1) * BeaconPeriodTime);

				pAd->Mlme.bPsPollTimerRunning = TRUE;
				RTMPSetTimer(&pAd->Mlme.PsPollTimer,
					     PsPollTime);
			}
		}
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("RT28xxPciAsicRadioOff::Level!=DOT11POWERSAVE \n"));
	}

	pAd->bPCIclkOffDisableTx = FALSE;

	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF);

	/* Set to 1R. */
	if (pAd->Antenna.field.RxPath > 1 && pAd->OpMode == OPMODE_STA) {
		tempBBP_R3 = (pAd->StaCfg.BBPR3 & 0xE7);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, tempBBP_R3);
	}
	/* In Radio Off, we turn off RF clk, So now need to call ASICSwitchChannel again. */
	if ((INFRA_ON(pAd) || pAd->OpMode == OPMODE_AP)
	    && (pAd->CommonCfg.CentralChannel != pAd->CommonCfg.Channel)
	    && (pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth == BW_40)) {
		/* Must using 40MHz. */
		AsicTurnOffRFClk(pAd, pAd->CommonCfg.CentralChannel);
	} else {
		/* Must using 20MHz. */
		AsicTurnOffRFClk(pAd, pAd->CommonCfg.Channel);
	}

	if (Level != RTMP_HALT) {
		/* Change Interrupt bitmask. */
		/* When PCI clock is off, don't want to service interrupt. */
		RTMP_IO_WRITE32(pAd, INT_MASK_CSR, AutoWakeupInt);
	} else {
		RTMP_ASIC_INTERRUPT_DISABLE(pAd);
	}

	RTMP_IO_WRITE32(pAd, RX_CRX_IDX, pAd->RxRing.RxCpuIdx);
	/*  2. Send Sleep command */
	RTMP_IO_WRITE32(pAd, H2M_MAILBOX_STATUS, 0xffffffff);
	RTMP_IO_WRITE32(pAd, H2M_MAILBOX_CID, 0xffffffff);
	/* send POWER-SAVE command to MCU. high-byte = 1 save power as much as possible. high byte = 0 save less power */
	AsicSendCommandToMcu(pAd, 0x30, PowerSafeCID, 0xff, 0x1);
	/*  2-1. Wait command success */
	/* Status = 1 : success, Status = 2, already sleep, Status = 3, Maybe MAC is busy so can't finish this task. */
	brc = AsicCheckCommanOk(pAd, PowerSafeCID);

	/*  3. After 0x30 command is ok, send radio off command. lowbyte = 0 for power safe. */
	/* If 0x30 command is not ok this time, we can ignore 0x35 command. It will make sure not cause firmware'r problem. */
	if ((Level == DOT11POWERSAVE) && (brc == TRUE)) {
		AsicSendCommandToMcu(pAd, 0x35, PowerRadioOffCID, 0, 0x00);	/* lowbyte = 0 means to do power safe, NOT turn off radio. */
		/*  3-1. Wait command success */
		AsicCheckCommanOk(pAd, PowerRadioOffCID);
	} else if (brc == TRUE) {
		AsicSendCommandToMcu(pAd, 0x35, PowerRadioOffCID, 1, 0x00);	/* lowbyte = 0 means to do power safe, NOT turn off radio. */
		/*  3-1. Wait command success */
		AsicCheckCommanOk(pAd, PowerRadioOffCID);
	}
	/* 1. Wait DMA not busy */
	i = 0;
	do {
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &DmaCfg.word);
		if ((DmaCfg.field.RxDMABusy == 0)
		    && (DmaCfg.field.TxDMABusy == 0))
			break;
		RTMPusecDelay(20);
		i++;
	} while (i < 50);

	/*
	   if (i >= 50)
	   {
	   pAd->CheckDmaBusyCount++;
	   DBGPRINT(RT_DEBUG_TRACE, ("DMA Rx keeps busy.  return on AsicRadioOff () CheckDmaBusyCount = %d \n", pAd->CheckDmaBusyCount));
	   }
	   else
	   {
	   pAd->CheckDmaBusyCount = 0;
	   }
	 */
/*KH Debug:My original codes have the follwoing codes, but currecnt codes do not have it. */
/* Disable for stability. If PCIE Link Control is modified for advance power save, re-covery this code segment. */
	RTMP_IO_WRITE32(pAd, PBF_SYS_CTRL, 0x1280);
/*OPSTATUS_SET_FLAG(pAd, fOP_STATUS_CLKSELECT_40MHZ); */

#ifdef PCIE_PS_SUPPORT
	if ((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
	    && IS_VERSION_AFTER_F(pAd)
	    && (pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3)
	    && (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("RT28xxPciAsicRadioOff::3090 return to skip the following TbttNumToNextWakeUp setting for 279x\n"));
		pAd->bPCIclkOff = TRUE;
		RTMP_CLEAR_PSFLAG(pAd, fRTMP_PS_DISABLE_TX);
		/* For this case, doesn't need to below actions, so return here. */
		return brc;
	}
#endif /* PCIE_PS_SUPPORT // */

	if (Level == DOT11POWERSAVE) {
		AUTO_WAKEUP_STRUC AutoWakeupCfg;
		/*RTMPSetTimer(&pAd->Mlme.PsPollTimer, 90); */

		/* we have decided to SLEEP, so at least do it for a BEACON period. */
		if (TbttNumToNextWakeUp == 0)
			TbttNumToNextWakeUp = 1;

		AutoWakeupCfg.word = 0;
		RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);

		/* 1. Set auto wake up timer. */
		AutoWakeupCfg.field.NumofSleepingTbtt = TbttNumToNextWakeUp - 1;
		AutoWakeupCfg.field.EnableAutoWakeup = 1;
		AutoWakeupCfg.field.AutoLeadTime = LEAD_TIME;
		RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);
	}
	/*  4-1. If it's to disable our device. Need to restore PCI Configuration Space to its original value. */
	if (Level == RTMP_HALT && pAd->OpMode == OPMODE_STA) {
		if ((brc == TRUE) && (i < 50))
			RTMPPCIeLinkCtrlSetting(pAd, 1);
	}
	/*  4. Set PCI configuration Space Link Comtrol fields.  Only Radio Off needs to call this function */
	else if (pAd->OpMode == OPMODE_STA) {
		if ((brc == TRUE) && (i < 50))
			RTMPPCIeLinkCtrlSetting(pAd, 3);
	}
	/*pAd->bPCIclkOffDisableTx = FALSE; */
	RTMP_CLEAR_PSFLAG(pAd, fRTMP_PS_DISABLE_TX);
	return TRUE;
}

void RT28xxPciMlmeRadioOn(struct rt_rtmp_adapter *pAd)
{
	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF))
		return;

	DBGPRINT(RT_DEBUG_TRACE, ("%s===>\n", __func__));

	if ((pAd->OpMode == OPMODE_AP) || ((pAd->OpMode == OPMODE_STA)
					   &&
					   (!OPSTATUS_TEST_FLAG
					    (pAd, fOP_STATUS_PCIE_DEVICE)
					    || pAd->StaCfg.PSControl.field.
					    EnableNewPS == FALSE))) {
		RT28xxPciAsicRadioOn(pAd, GUI_IDLE_POWER_SAVE);
		/*NICResetFromError(pAd); */

		RTMPRingCleanUp(pAd, QID_AC_BK);
		RTMPRingCleanUp(pAd, QID_AC_BE);
		RTMPRingCleanUp(pAd, QID_AC_VI);
		RTMPRingCleanUp(pAd, QID_AC_VO);
		RTMPRingCleanUp(pAd, QID_MGMT);
		RTMPRingCleanUp(pAd, QID_RX);

		/* Enable Tx/Rx */
		RTMPEnableRxTx(pAd);

		/* Clear Radio off flag */
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF);

		/* Set LED */
		RTMPSetLED(pAd, LED_RADIO_ON);
	}

	if ((pAd->OpMode == OPMODE_STA) &&
	    (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE))
	    && (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)) {
		BOOLEAN Cancelled;

		RTMPPCIeLinkCtrlValueRestore(pAd, RESTORE_WAKEUP);

		pAd->Mlme.bPsPollTimerRunning = FALSE;
		RTMPCancelTimer(&pAd->Mlme.PsPollTimer, &Cancelled);
		RTMPCancelTimer(&pAd->Mlme.RadioOnOffTimer, &Cancelled);
		RTMPSetTimer(&pAd->Mlme.RadioOnOffTimer, 40);
	}
}

void RT28xxPciMlmeRadioOFF(struct rt_rtmp_adapter *pAd)
{
	BOOLEAN brc = TRUE;

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF))
		return;

	/* Link down first if any association exists */
	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) {
		if (INFRA_ON(pAd) || ADHOC_ON(pAd)) {
			struct rt_mlme_disassoc_req DisReq;
			struct rt_mlme_queue_elem *pMsgElem =
			    kmalloc(sizeof(struct rt_mlme_queue_elem),
							MEM_ALLOC_FLAG);

			if (pMsgElem) {
				COPY_MAC_ADDR(&DisReq.Addr,
					      pAd->CommonCfg.Bssid);
				DisReq.Reason = REASON_DISASSOC_STA_LEAVING;

				pMsgElem->Machine = ASSOC_STATE_MACHINE;
				pMsgElem->MsgType = MT2_MLME_DISASSOC_REQ;
				pMsgElem->MsgLen =
				    sizeof(struct rt_mlme_disassoc_req);
				NdisMoveMemory(pMsgElem->Msg, &DisReq,
					       sizeof
					       (struct rt_mlme_disassoc_req));

				MlmeDisassocReqAction(pAd, pMsgElem);
				kfree(pMsgElem);

				RTMPusecDelay(1000);
			}
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, ("%s===>\n", __func__));

	/* Set Radio off flag */
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

	{
		BOOLEAN Cancelled;
		if (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE) {
			if (RTMP_TEST_FLAG
			    (pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) {
				RTMPCancelTimer(&pAd->MlmeAux.ScanTimer,
						&Cancelled);
				RTMP_CLEAR_FLAG(pAd,
						fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS);
			}
			/* If during power safe mode. */
			if (pAd->StaCfg.bRadio == TRUE) {
				DBGPRINT(RT_DEBUG_TRACE,
					 ("-->MlmeRadioOff() return on bRadio == TRUE; \n"));
				return;
			}
			/* Always radio on since the NIC needs to set the MCU command (LED_RADIO_OFF). */
			if (IDLE_ON(pAd) &&
			    (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF)))
			{
				RT28xxPciAsicRadioOn(pAd, GUI_IDLE_POWER_SAVE);
			}
			if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)) {
				BOOLEAN Cancelled;
				pAd->Mlme.bPsPollTimerRunning = FALSE;
				RTMPCancelTimer(&pAd->Mlme.PsPollTimer,
						&Cancelled);
				RTMPCancelTimer(&pAd->Mlme.RadioOnOffTimer,
						&Cancelled);
			}
		}
		/* Link down first if any association exists */
		if (INFRA_ON(pAd) || ADHOC_ON(pAd))
			LinkDown(pAd, FALSE);
		RTMPusecDelay(10000);
		/*========================================== */
		/* Clean up old bss table */
		BssTableInit(&pAd->ScanTab);

		/*
		   if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE))
		   {
		   RTMPSetTimer(&pAd->Mlme.RadioOnOffTimer, 10);
		   return;
		   }
		 */
	}

	/* Set LED.Move to here for fixing LED bug. This flag must be called after LinkDown */
	RTMPSetLED(pAd, LED_RADIO_OFF);

/*KH Debug:All PCIe devices need to use timer to execute radio off function, or the PCIe&&EnableNewPS needs. */
/*KH Ans:It is right, because only when the PCIe and EnableNewPs is true, we need to delay the RadioOffTimer */
/*to avoid the deadlock with PCIe Power saving function. */
	if (pAd->OpMode == OPMODE_STA &&
	    OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE) &&
	    pAd->StaCfg.PSControl.field.EnableNewPS == TRUE) {
		RTMPSetTimer(&pAd->Mlme.RadioOnOffTimer, 10);
	} else {
		brc = RT28xxPciAsicRadioOff(pAd, GUIRADIO_OFF, 0);

		if (brc == FALSE) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("%s call RT28xxPciAsicRadioOff fail!\n",
				  __func__));
		}
	}
/*
*/
}

#endif /* RTMP_MAC_PCI // */
