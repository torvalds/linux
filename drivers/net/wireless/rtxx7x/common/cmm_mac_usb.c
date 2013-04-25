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


#ifdef RTMP_MAC_USB


#include	"rt_config.h"


static NDIS_STATUS RTMPAllocUsbBulkBufStruct(
	IN RTMP_ADAPTER *pAd,
	IN PURB *ppUrb,
	IN PVOID *ppXBuffer,
	IN INT	bufLen,
	IN ra_dma_addr_t *pDmaAddr,
	IN PSTRING pBufName)
{
	POS_COOKIE pObj = (POS_COOKIE) pAd->OS_Cookie;


	*ppUrb = RTUSB_ALLOC_URB(0);
	if (*ppUrb == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("<-- ERROR in Alloc urb struct for %s !\n", pBufName));
		return NDIS_STATUS_RESOURCES;
	}

	*ppXBuffer = RTUSB_URB_ALLOC_BUFFER(pObj->pUsb_Dev, bufLen, pDmaAddr);
	if (*ppXBuffer == NULL) {
		DBGPRINT(RT_DEBUG_ERROR, ("<-- ERROR in Alloc Bulk buffer for %s!\n", pBufName));
		return NDIS_STATUS_RESOURCES;
	}

	return NDIS_STATUS_SUCCESS;
}


static NDIS_STATUS RTMPFreeUsbBulkBufStruct(
	IN RTMP_ADAPTER *pAd,
	IN PURB *ppUrb,
	IN PUCHAR *ppXBuffer,
	IN INT bufLen,
	IN ra_dma_addr_t data_dma)
{
	POS_COOKIE pObj = (POS_COOKIE) pAd->OS_Cookie;

	if (NULL != *ppUrb) {
		RTUSB_UNLINK_URB(*ppUrb);
		RTUSB_FREE_URB(*ppUrb);
		*ppUrb = NULL;
	}

	if (NULL != *ppXBuffer) {
		RTUSB_URB_FREE_BUFFER(pObj->pUsb_Dev, bufLen,	*ppXBuffer, data_dma);
		*ppXBuffer = NULL;
	}

	return NDIS_STATUS_SUCCESS;
}


#ifdef RESOURCE_PRE_ALLOC
VOID RTMPResetTxRxRingMemory(
	IN RTMP_ADAPTER * pAd)
{
	UINT index, i, acidx;
	PTX_CONTEXT pNullContext   = &pAd->NullContext;
	PTX_CONTEXT pPsPollContext = &pAd->PsPollContext;
	unsigned int IrqFlags;

	/* Free TxSwQueue Packet*/
	for (index = 0; index < NUM_OF_TX_RING; index++)
	{
		PQUEUE_ENTRY pEntry;
		PNDIS_PACKET pPacket;
		PQUEUE_HEADER pQueue;

		RTMP_IRQ_LOCK(&pAd->irq_lock, IrqFlags);
		pQueue = &pAd->TxSwQueue[index];
		while (pQueue->Head)
		{
			pEntry = RemoveHeadQueue(pQueue);
			pPacket = QUEUE_ENTRY_TO_PACKET(pEntry);
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
		}
		 RTMP_IRQ_UNLOCK(&pAd->irq_lock, IrqFlags);
	}

	/* unlink all urbs for the RECEIVE buffer queue.*/
	for(i=0; i<(RX_RING_SIZE); i++)
	{
		PRX_CONTEXT  pRxContext = &(pAd->RxContext[i]);
		if (pRxContext->pUrb)
			RTUSB_UNLINK_URB(pRxContext->pUrb);
	}

	/* unlink PsPoll urb resource*/
	if (pPsPollContext && pPsPollContext->pUrb)
		RTUSB_UNLINK_URB(pPsPollContext->pUrb);

	/* Free NULL frame urb resource*/
	if (pNullContext && pNullContext->pUrb)
		RTUSB_UNLINK_URB(pNullContext->pUrb);


	/* Free mgmt frame resource*/
	for(i = 0; i < MGMT_RING_SIZE; i++)
	{
		PTX_CONTEXT pMLMEContext = (PTX_CONTEXT)pAd->MgmtRing.Cell[i].AllocVa;
		if (pMLMEContext)
		{
			if (NULL != pMLMEContext->pUrb)
			{
				RTUSB_UNLINK_URB(pMLMEContext->pUrb);
				RTUSB_FREE_URB(pMLMEContext->pUrb);
				pMLMEContext->pUrb = NULL;
			}
		}

		if (NULL != pAd->MgmtRing.Cell[i].pNdisPacket)
		{
			RTMPFreeNdisPacket(pAd, pAd->MgmtRing.Cell[i].pNdisPacket);
			pAd->MgmtRing.Cell[i].pNdisPacket = NULL;
			if (pMLMEContext)
				pMLMEContext->TransferBuffer = NULL;
		}

	}


	/* Free Tx frame resource*/
	for (acidx = 0; acidx < 4; acidx++)
	{
		PHT_TX_CONTEXT pHTTXContext = &(pAd->TxContext[acidx]);
		if (pHTTXContext && pHTTXContext->pUrb)
			RTUSB_UNLINK_URB(pHTTXContext->pUrb);
	}

	for(i=0; i<6; i++)
	{
		NdisFreeSpinLock(&pAd->BulkOutLock[i]);
	}

	NdisFreeSpinLock(&pAd->BulkInLock);
	NdisFreeSpinLock(&pAd->MLMEBulkOutLock);

	NdisFreeSpinLock(&pAd->CmdQLock);
#ifdef RALINK_ATE
	NdisFreeSpinLock(&pAd->GenericLock);
#endif /* RALINK_ATE */
	/* Clear all pending bulk-out request flags.*/
	RTUSB_CLEAR_BULK_FLAG(pAd, 0xffffffff);

	for (i = 0; i < NUM_OF_TX_RING; i++)
	{
		NdisFreeSpinLock(&pAd->TxContextQueueLock[i]);
	}

/*	NdisFreeSpinLock(&pAd->MacTabLock);*/

/*	for(i=0; i<MAX_LEN_OF_BA_REC_TABLE; i++)*/
/*	{*/
/*		NdisFreeSpinLock(&pAd->BATable.BARecEntry[i].RxReRingLock);*/
/*	}*/
}


/*
========================================================================
Routine Description:
	Calls USB_InterfaceStop and frees memory allocated for the URBs
    calls NdisMDeregisterDevice and frees the memory
    allocated in VNetInitialize for the Adapter Object

Arguments:
	*pAd				the raxx interface data pointer

Return Value:
	None

Note:
========================================================================
*/
VOID	RTMPFreeTxRxRingMemory(
	IN	PRTMP_ADAPTER	pAd)
{
	UINT                i, acidx;
	PTX_CONTEXT			pNullContext   = &pAd->NullContext;
	PTX_CONTEXT			pPsPollContext = &pAd->PsPollContext;
#ifdef USB_BULK_BUF_ALIGMENT2
	POS_COOKIE			pObj = (POS_COOKIE) pAd->OS_Cookie;
#endif /* USB_BULK_BUF_ALIGMENT2 */

	DBGPRINT(RT_DEBUG_ERROR, ("---> RTMPFreeTxRxRingMemory\n"));

	/* Free all resources for the RECEIVE buffer queue.*/
	for(i=0; i<(RX_RING_SIZE); i++)
	{
		PRX_CONTEXT  pRxContext = &(pAd->RxContext[i]);
		if (pRxContext)
			RTMPFreeUsbBulkBufStruct(pAd,
										&pRxContext->pUrb,
										&pRxContext->TransferBuffer,
										MAX_RXBULK_SIZE,
										pRxContext->data_dma);
	}

	/* Free PsPoll frame resource*/
	RTMPFreeUsbBulkBufStruct(pAd,
								&pPsPollContext->pUrb,
								(UCHAR **)&pPsPollContext->TransferBuffer, // warning!! by sw
								sizeof(TX_BUFFER),
								pPsPollContext->data_dma);

	/* Free NULL frame resource*/
	RTMPFreeUsbBulkBufStruct(pAd,
								&pNullContext->pUrb,
								(UCHAR **)&pNullContext->TransferBuffer, // warning!! by sw
								sizeof(TX_BUFFER),
								pNullContext->data_dma);

	/* Free mgmt frame resource*/
	for(i = 0; i < MGMT_RING_SIZE; i++)
	{
		PTX_CONTEXT pMLMEContext = (PTX_CONTEXT)pAd->MgmtRing.Cell[i].AllocVa;
		if (pMLMEContext)
		{
			if (NULL != pMLMEContext->pUrb)
			{
				RTUSB_UNLINK_URB(pMLMEContext->pUrb);
				RTUSB_FREE_URB(pMLMEContext->pUrb);
				pMLMEContext->pUrb = NULL;
			}
		}

		if (NULL != pAd->MgmtRing.Cell[i].pNdisPacket)
		{
			RTMPFreeNdisPacket(pAd, pAd->MgmtRing.Cell[i].pNdisPacket);
			pAd->MgmtRing.Cell[i].pNdisPacket = NULL;
			if (pMLMEContext)
				pMLMEContext->TransferBuffer = NULL;
		}
	}

	if (pAd->MgmtDescRing.AllocVa)
		os_free_mem(pAd, pAd->MgmtDescRing.AllocVa);


	/* Free Tx frame resource*/
	for (acidx = 0; acidx < 4; acidx++)
	{
		PHT_TX_CONTEXT pHTTXContext = &(pAd->TxContext[acidx]);
		if (pHTTXContext)
			RTMPFreeUsbBulkBufStruct(pAd,
										&pHTTXContext->pUrb,
										(UCHAR **)&pHTTXContext->TransferBuffer, // warning!! by sw
										sizeof(HTTX_BUFFER),
										pHTTXContext->data_dma);

#ifdef USB_BULK_BUF_ALIGMENT2
		if (pHTTXContext->pBuf != NULL)
		{

			RTUSB_URB_FREE_BUFFER(pObj->pUsb_Dev, 26624,
				pHTTXContext->pBuf,
				pHTTXContext->data_dma2);
				pHTTXContext->pBuf = NULL;
		}


#endif /* USB_BULK_BUF_ALIGMENT2 */
	}



	if (pAd->FragFrame.pFragPacket)
		RELEASE_NDIS_PACKET(pAd, pAd->FragFrame.pFragPacket, NDIS_STATUS_SUCCESS);


	DBGPRINT(RT_DEBUG_ERROR, ("<--- RTMPFreeTxRxRingMemory\n"));
}


/*
========================================================================
Routine Description:
    Initialize receive data structures.

Arguments:
    pAd					Pointer to our adapter

Return Value:
	NDIS_STATUS_SUCCESS
	NDIS_STATUS_RESOURCES

Note:
	Initialize all receive releated private buffer, include those define
	in RTMP_ADAPTER structure and all private data structures. The major
	work is to allocate buffer for each packet and chain buffer to
	NDIS packet descriptor.
========================================================================
*/
NDIS_STATUS	NICInitRecv(
	IN	PRTMP_ADAPTER	pAd)
{
	UCHAR				i;

	DBGPRINT(RT_DEBUG_TRACE, ("--> NICInitRecv\n"));


	pAd->PendingRx = 0;
	pAd->NextRxBulkInReadIndex 	= 0;	/* Next Rx Read index*/
	pAd->NextRxBulkInIndex		= 0 ; /*RX_RING_SIZE -1;  Rx Bulk pointer*/
	pAd->NextRxBulkInPosition 	= 0;

	for (i = 0; i < (RX_RING_SIZE); i++)
	{
		PRX_CONTEXT  pRxContext = &(pAd->RxContext[i]);

		ASSERT((pRxContext->TransferBuffer != NULL));
		ASSERT((pRxContext->pUrb != NULL));

		NdisZeroMemory(pRxContext->TransferBuffer, MAX_RXBULK_SIZE);

		pRxContext->pAd	= pAd;
		pRxContext->pIrp = NULL;
		pRxContext->InUse = FALSE;
		pRxContext->IRPPending = FALSE;
		pRxContext->Readable	= FALSE;
		pRxContext->bRxHandling = FALSE;
		pRxContext->BulkInOffset = 0;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICInitRecv()\n"));

	return NDIS_STATUS_SUCCESS;
}


/*
========================================================================
Routine Description:
    Initialize transmit data structures.

Arguments:
    pAd					Pointer to our adapter

Return Value:
	NDIS_STATUS_SUCCESS
	NDIS_STATUS_RESOURCES

Note:
========================================================================
*/
NDIS_STATUS	NICInitTransmit(
	IN	PRTMP_ADAPTER	pAd)
{
	UCHAR			i, acidx;
	NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;
	PTX_CONTEXT		pNullContext   = &(pAd->NullContext);
	PTX_CONTEXT		pPsPollContext = &(pAd->PsPollContext);
	PTX_CONTEXT		pMLMEContext = NULL;
	PVOID			RingBaseVa;
	RTMP_MGMT_RING  *pMgmtRing;
	PVOID pTransferBuffer;
	PURB	pUrb;
	ra_dma_addr_t data_dma;
#ifdef USB_BULK_BUF_ALIGMENT2
	ra_dma_addr_t data_dma2;
	POS_COOKIE              pObj = (POS_COOKIE) pAd->OS_Cookie;
#endif /* USB_BULK_BUF_ALIGMENT2 */
	DBGPRINT(RT_DEBUG_TRACE, ("--> NICInitTransmit\n"));


	/* Init 4 set of Tx parameters*/
	for(acidx = 0; acidx < NUM_OF_TX_RING; acidx++)
	{
		/* Initialize all Transmit releated queues*/
		InitializeQueueHeader(&pAd->TxSwQueue[acidx]);

		/* Next Local tx ring pointer waiting for buck out*/
		pAd->NextBulkOutIndex[acidx] = acidx;
		pAd->BulkOutPending[acidx] = FALSE; /* Buck Out control flag	*/
	}


	do
	{

		/* TX_RING_SIZE, 4 ACs*/

		for(acidx=0; acidx<4; acidx++)
		{
			PHT_TX_CONTEXT	pHTTXContext = &(pAd->TxContext[acidx]);

			pTransferBuffer = pHTTXContext->TransferBuffer;
			pUrb = pHTTXContext->pUrb;
			data_dma = pHTTXContext->data_dma;

			ASSERT( (pTransferBuffer != NULL));
			ASSERT( (pUrb != NULL));

			NdisZeroMemory(pHTTXContext, sizeof(HT_TX_CONTEXT));
			pHTTXContext->TransferBuffer = pTransferBuffer;
			pHTTXContext->pUrb = pUrb;
			pHTTXContext->data_dma = data_dma;

			NdisZeroMemory(pHTTXContext->TransferBuffer->Aggregation, 4);

			pHTTXContext->pAd = pAd;
			pHTTXContext->BulkOutPipeId = acidx;
			pHTTXContext->bRingEmpty = TRUE;
			pHTTXContext->bCopySavePad = FALSE;
			pAd->BulkOutPending[acidx] = FALSE;
#ifdef USB_BULK_BUF_ALIGMENT2
			if ((pHTTXContext->pBuf = RTUSB_URB_ALLOC_BUFFER(pObj->pUsb_Dev, 26624, &pHTTXContext->data_dma2)) == NULL)
			{
				printk("Unable to alloc Tx USB buffer.\n");
				pHTTXContext->pBuf = NULL;

			}
#endif /* USB_BULK_BUF_ALIGMENT2 */

		}



		/* MGMT_RING_SIZE*/

		NdisZeroMemory(pAd->MgmtDescRing.AllocVa, pAd->MgmtDescRing.AllocSize);
		RingBaseVa = pAd->MgmtDescRing.AllocVa;

		/* Initialize MGMT Ring and associated buffer memory*/
		pMgmtRing = &pAd->MgmtRing;
		for (i = 0; i < MGMT_RING_SIZE; i++)
		{
			/* link the pre-allocated Mgmt buffer to MgmtRing.Cell*/
			pMgmtRing->Cell[i].AllocSize = sizeof(TX_CONTEXT);
			pMgmtRing->Cell[i].AllocVa = RingBaseVa;
			pMgmtRing->Cell[i].pNdisPacket = NULL;
			pMgmtRing->Cell[i].pNextNdisPacket = NULL;

			/*Allocate URB for MLMEContext*/
			pMLMEContext = (PTX_CONTEXT) pAd->MgmtRing.Cell[i].AllocVa;
			pMLMEContext->pUrb = RTUSB_ALLOC_URB(0);
			if (pMLMEContext->pUrb == NULL)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("<-- ERROR in Alloc TX MLMEContext[%d] urb!! \n", i));
				Status = NDIS_STATUS_RESOURCES;
				goto err;
			}
			pMLMEContext->pAd = pAd;
			pMLMEContext->SelfIdx = i;

			/* Offset to next ring descriptor address*/
			RingBaseVa = (PUCHAR) RingBaseVa + sizeof(TX_CONTEXT);
		}
		DBGPRINT(RT_DEBUG_TRACE, ("MGMT Ring: total %d entry allocated\n", i));

		/*pAd->MgmtRing.TxSwFreeIdx = (MGMT_RING_SIZE - 1);*/
		pAd->MgmtRing.TxSwFreeIdx = MGMT_RING_SIZE;
		pAd->MgmtRing.TxCpuIdx = 0;
		pAd->MgmtRing.TxDmaIdx = 0;



		/* NullContext*/

		pTransferBuffer = pNullContext->TransferBuffer;
		pUrb = pNullContext->pUrb;
		data_dma = pNullContext->data_dma;

		NdisZeroMemory(pNullContext, sizeof(TX_CONTEXT));
		pNullContext->TransferBuffer = pTransferBuffer;
		pNullContext->pUrb = pUrb;
		pNullContext->data_dma = data_dma;
		pNullContext->pAd = pAd;



		/* PsPollContext*/

		pTransferBuffer = pPsPollContext->TransferBuffer;
		pUrb = pPsPollContext->pUrb;
		data_dma = pPsPollContext->data_dma;
		NdisZeroMemory(pPsPollContext, sizeof(TX_CONTEXT));
		pPsPollContext->TransferBuffer = pTransferBuffer;
		pPsPollContext->pUrb = pUrb;
		pPsPollContext->data_dma = data_dma;
		pPsPollContext->pAd = pAd;
		pPsPollContext->LastOne = TRUE;

	}   while (FALSE);


	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICInitTransmit(Status=%d)\n", Status));

	return Status;

	/* --------------------------- ERROR HANDLE --------------------------- */
err:
	if (pAd->MgmtDescRing.AllocVa)
	{
		pMgmtRing = &pAd->MgmtRing;
		for(i = 0; i < MGMT_RING_SIZE; i++)
		{
			pMLMEContext = (PTX_CONTEXT) pAd->MgmtRing.Cell[i].AllocVa;
			if (pMLMEContext)
				RTMPFreeUsbBulkBufStruct(pAd,
											&pMLMEContext->pUrb,
											(UCHAR **)&pMLMEContext->TransferBuffer, //warning!! by sw
											sizeof(TX_BUFFER),
											pMLMEContext->data_dma);
		}
		os_free_mem(pAd, pAd->MgmtDescRing.AllocVa);
		pAd->MgmtDescRing.AllocVa = NULL;
	}

	/* Here we didn't have any pre-allocated memory need to free.*/

	return Status;
}


/*
========================================================================
Routine Description:
    Allocate DMA memory blocks for send, receive.

Arguments:
    pAd					Pointer to our adapter

Return Value:
	NDIS_STATUS_SUCCESS
	NDIS_STATUS_FAILURE
	NDIS_STATUS_RESOURCES

Note:
========================================================================
*/
NDIS_STATUS	RTMPAllocTxRxRingMemory(
	IN	PRTMP_ADAPTER	pAd)
{
	NDIS_STATUS Status = NDIS_STATUS_FAILURE;
	PTX_CONTEXT pNullContext   = &(pAd->NullContext);
	PTX_CONTEXT pPsPollContext = &(pAd->PsPollContext);
	INT i, acidx;


	DBGPRINT(RT_DEBUG_TRACE, ("--> RTMPAllocTxRxRingMemory\n"));

	do
	{

		/* Init send data structures and related parameters*/



		/* TX_RING_SIZE, 4 ACs*/

		for(acidx=0; acidx<4; acidx++)
		{
			PHT_TX_CONTEXT	pHTTXContext = &(pAd->TxContext[acidx]);

			NdisZeroMemory(pHTTXContext, sizeof(HT_TX_CONTEXT));
			/*Allocate URB and bulk buffer*/
			Status = RTMPAllocUsbBulkBufStruct(pAd,
												&pHTTXContext->pUrb,
												(PVOID *)&pHTTXContext->TransferBuffer, //warning!! by sw
												sizeof(HTTX_BUFFER),
												&pHTTXContext->data_dma,
												"HTTxContext");
			if (Status != NDIS_STATUS_SUCCESS)
				goto err;
		}



		/* MGMT_RING_SIZE*/

		/* Allocate MGMT ring descriptor's memory*/
		pAd->MgmtDescRing.AllocSize = MGMT_RING_SIZE * sizeof(TX_CONTEXT);
		os_alloc_mem(pAd, (PUCHAR *)(&pAd->MgmtDescRing.AllocVa), pAd->MgmtDescRing.AllocSize);
		if (pAd->MgmtDescRing.AllocVa == NULL)
		{
			DBGPRINT_ERR(("Failed to allocate a big buffer for MgmtDescRing!\n"));
			Status = NDIS_STATUS_RESOURCES;
			goto err;
		}



		/* NullContext*/

		NdisZeroMemory(pNullContext, sizeof(TX_CONTEXT));
		/*Allocate URB*/
		Status = RTMPAllocUsbBulkBufStruct(pAd,
											&pNullContext->pUrb,
											(PVOID *)&pNullContext->TransferBuffer, //warning!! by sw
											sizeof(TX_BUFFER),
											&pNullContext->data_dma,
											"TxNullContext");
		if (Status != NDIS_STATUS_SUCCESS)
			goto err;


		/* PsPollContext*/

		NdisZeroMemory(pPsPollContext, sizeof(TX_CONTEXT));
		/*Allocate URB*/
		Status = RTMPAllocUsbBulkBufStruct(pAd,
											&pPsPollContext->pUrb,
											(PVOID *)&pPsPollContext->TransferBuffer, //warning!! by sw
											sizeof(TX_BUFFER),
											&pPsPollContext->data_dma,
											"TxPsPollContext");
		if (Status != NDIS_STATUS_SUCCESS)
			goto err;



		/* Init receive data structures and related parameters*/

		for (i = 0; i < (RX_RING_SIZE); i++)
		{
			PRX_CONTEXT  pRxContext = &(pAd->RxContext[i]);

			/*Allocate URB*/
			Status = RTMPAllocUsbBulkBufStruct(pAd,
												&pRxContext->pUrb,
												(PVOID *)&pRxContext->TransferBuffer, //warning!! by sw
												MAX_RXBULK_SIZE,
												&pRxContext->data_dma,
												"RxContext");
			if (Status != NDIS_STATUS_SUCCESS)
				goto err;

		}

		NdisZeroMemory(&pAd->FragFrame, sizeof(FRAGMENT_FRAME));
		pAd->FragFrame.pFragPacket =  RTMP_AllocateFragPacketBuffer(pAd, RX_BUFFER_NORMSIZE);

		if (pAd->FragFrame.pFragPacket == NULL)
		{
			Status = NDIS_STATUS_RESOURCES;
		}
	} while (FALSE);

	DBGPRINT_S(Status, ("<-- RTMPAllocTxRxRingMemory, Status=%x\n", Status));
	return Status;

err:
	Status = NDIS_STATUS_RESOURCES;
	RTMPFreeTxRxRingMemory(pAd);

	return Status;
}


NDIS_STATUS RTMPInitTxRxRingMemory
	(IN RTMP_ADAPTER *pAd)
{
	INT				num;
	NDIS_STATUS		Status;

	/* Init the CmdQ and CmdQLock*/
	NdisAllocateSpinLock(pAd, &pAd->CmdQLock);
	NdisAcquireSpinLock(&pAd->CmdQLock);
	RTInitializeCmdQ(&pAd->CmdQ);
	NdisReleaseSpinLock(&pAd->CmdQLock);


	NdisAllocateSpinLock(pAd, &pAd->MLMEBulkOutLock);
	NdisAllocateSpinLock(pAd, &pAd->BulkInLock);
	for(num =0 ; num < 6; num++)
	{
		NdisAllocateSpinLock(pAd, &pAd->BulkOutLock[num]);
	}


	for (num = 0; num < NUM_OF_TX_RING; num++)
	{
		NdisAllocateSpinLock(pAd, &pAd->TxContextQueueLock[num]);
	}

#ifdef RALINK_ATE
	NdisAllocateSpinLock(pAd, &pAd->GenericLock);
#endif /* RALINK_ATE */

	NICInitRecv(pAd);


	Status = NICInitTransmit(pAd);

	return Status;

}


#else

/*
========================================================================
Routine Description:
    Initialize receive data structures.

Arguments:
    pAd					Pointer to our adapter

Return Value:
	NDIS_STATUS_SUCCESS
	NDIS_STATUS_RESOURCES

Note:
	Initialize all receive releated private buffer, include those define
	in RTMP_ADAPTER structure and all private data structures. The mahor
	work is to allocate buffer for each packet and chain buffer to
	NDIS packet descriptor.
========================================================================
*/
NDIS_STATUS	NICInitRecv(
	IN	PRTMP_ADAPTER	pAd)
{
	UCHAR				i;
	NDIS_STATUS			Status = NDIS_STATUS_SUCCESS;
	POS_COOKIE			pObj = (POS_COOKIE) pAd->OS_Cookie;


	DBGPRINT(RT_DEBUG_TRACE, ("--> NICInitRecv\n"));
	pObj = pObj;

	/*InterlockedExchange(&pAd->PendingRx, 0);*/
	pAd->PendingRx = 0;
	pAd->NextRxBulkInReadIndex 	= 0;	/* Next Rx Read index*/
	pAd->NextRxBulkInIndex		= 0 ; /*RX_RING_SIZE -1;  Rx Bulk pointer*/
	pAd->NextRxBulkInPosition 	= 0;

	for (i = 0; i < (RX_RING_SIZE); i++)
	{
		PRX_CONTEXT  pRxContext = &(pAd->RxContext[i]);

		/*Allocate URB*/
		pRxContext->pUrb = RTUSB_ALLOC_URB(0);
		if (pRxContext->pUrb == NULL)
		{
			Status = NDIS_STATUS_RESOURCES;
			goto out1;
		}

		/* Allocate transfer buffer*/
		pRxContext->TransferBuffer = RTUSB_URB_ALLOC_BUFFER(pObj->pUsb_Dev, MAX_RXBULK_SIZE, &pRxContext->data_dma);
		if (pRxContext->TransferBuffer == NULL)
		{
			Status = NDIS_STATUS_RESOURCES;
			goto out1;
		}

		NdisZeroMemory(pRxContext->TransferBuffer, MAX_RXBULK_SIZE);

		pRxContext->pAd	= pAd;
		pRxContext->pIrp = NULL;
		pRxContext->InUse		= FALSE;
		pRxContext->IRPPending	= FALSE;
		pRxContext->Readable	= FALSE;
		/*pRxContext->ReorderInUse = FALSE;*/
		pRxContext->bRxHandling = FALSE;
		pRxContext->BulkInOffset = 0;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICInitRecv(Status=%d)\n", Status));
	return Status;

out1:
	for (i = 0; i < (RX_RING_SIZE); i++)
	{
		PRX_CONTEXT  pRxContext = &(pAd->RxContext[i]);

		if (NULL != pRxContext->TransferBuffer)
		{
			RTUSB_URB_FREE_BUFFER(pObj->pUsb_Dev, MAX_RXBULK_SIZE,
								pRxContext->TransferBuffer, pRxContext->data_dma);
			pRxContext->TransferBuffer = NULL;
		}

		if (NULL != pRxContext->pUrb)
		{
			RTUSB_UNLINK_URB(pRxContext->pUrb);
			RTUSB_FREE_URB(pRxContext->pUrb);
			pRxContext->pUrb = NULL;
		}
	}

	return Status;
}


/*
========================================================================
Routine Description:
    Initialize transmit data structures.

Arguments:
    pAd					Pointer to our adapter

Return Value:
	NDIS_STATUS_SUCCESS
	NDIS_STATUS_RESOURCES

Note:
========================================================================
*/
NDIS_STATUS	NICInitTransmit(
	IN	PRTMP_ADAPTER	pAd)
{
	UCHAR			i, acidx;
	NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;
	PTX_CONTEXT		pNullContext   = &(pAd->NullContext);
	PTX_CONTEXT		pPsPollContext = &(pAd->PsPollContext);
	PTX_CONTEXT		pMLMEContext = NULL;
	POS_COOKIE		pObj = (POS_COOKIE) pAd->OS_Cookie;
	PVOID			RingBaseVa;
	RTMP_MGMT_RING  *pMgmtRing;

	DBGPRINT(RT_DEBUG_TRACE, ("--> NICInitTransmit\n"));
	pObj = pObj;

	/* Init 4 set of Tx parameters*/
	for(acidx = 0; acidx < NUM_OF_TX_RING; acidx++)
	{
		/* Initialize all Transmit releated queues*/
		InitializeQueueHeader(&pAd->TxSwQueue[acidx]);

		/* Next Local tx ring pointer waiting for buck out*/
		pAd->NextBulkOutIndex[acidx] = acidx;
		pAd->BulkOutPending[acidx] = FALSE; /* Buck Out control flag	*/
	}


	do
	{

		/* TX_RING_SIZE, 4 ACs*/

		for(acidx=0; acidx<4; acidx++)
		{
			PHT_TX_CONTEXT	pHTTXContext = &(pAd->TxContext[acidx]);

			NdisZeroMemory(pHTTXContext, sizeof(HT_TX_CONTEXT));
			/*Allocate URB*/
			Status = RTMPAllocUsbBulkBufStruct(pAd,
												&pHTTXContext->pUrb,
												&pHTTXContext->TransferBuffer,
												sizeof(HTTX_BUFFER),
												&pHTTXContext->data_dma,
												"HTTxContext");
			if (Status != NDIS_STATUS_SUCCESS)
				goto err;

			NdisZeroMemory(pHTTXContext->TransferBuffer->Aggregation, 4);
			pHTTXContext->pAd = pAd;
			pHTTXContext->pIrp = NULL;
			pHTTXContext->IRPPending = FALSE;
			pHTTXContext->NextBulkOutPosition = 0;
			pHTTXContext->ENextBulkOutPosition = 0;
			pHTTXContext->CurWritePosition = 0;
			pHTTXContext->CurWriteRealPos = 0;
			pHTTXContext->BulkOutSize = 0;
			pHTTXContext->BulkOutPipeId = acidx;
			pHTTXContext->bRingEmpty = TRUE;
			pHTTXContext->bCopySavePad = FALSE;
			pAd->BulkOutPending[acidx] = FALSE;
#ifdef USB_BULK_BUF_ALIGMENT2
			if ((pHTTXContext->pBuf = RTUSB_URB_ALLOC_BUFFER(pObj->pUsb_Dev, 26624, &pHTTXContext->data_dma2)) == NULL)
			{
				printk("Unable to alloc Tx USB buffer.\n");
				pHTTXContext->pBuf = NULL;
				goto out1;
			}
#endif /* USB_BULK_BUF_ALIGMENT2 */

		}



		/* MGMT Ring*/


		/* Allocate MGMT ring descriptor's memory*/
		pAd->MgmtDescRing.AllocSize = MGMT_RING_SIZE * sizeof(TX_CONTEXT);
		os_alloc_mem(pAd, (PUCHAR *)(&pAd->MgmtDescRing.AllocVa), pAd->MgmtDescRing.AllocSize);
		if (pAd->MgmtDescRing.AllocVa == NULL)
		{
			DBGPRINT_ERR(("Failed to allocate a big buffer for MgmtDescRing!\n"));
			Status = NDIS_STATUS_RESOURCES;
			goto err;
		}
		NdisZeroMemory(pAd->MgmtDescRing.AllocVa, pAd->MgmtDescRing.AllocSize);
		RingBaseVa     = pAd->MgmtDescRing.AllocVa;

		/* Initialize MGMT Ring and associated buffer memory*/
		pMgmtRing = &pAd->MgmtRing;
		for (i = 0; i < MGMT_RING_SIZE; i++)
		{
			/* link the pre-allocated Mgmt buffer to MgmtRing.Cell*/
			pMgmtRing->Cell[i].AllocSize = sizeof(TX_CONTEXT);
			pMgmtRing->Cell[i].AllocVa = RingBaseVa;
			pMgmtRing->Cell[i].pNdisPacket = NULL;
			pMgmtRing->Cell[i].pNextNdisPacket = NULL;

			/*Allocate URB for MLMEContext*/
			pMLMEContext = (PTX_CONTEXT) pAd->MgmtRing.Cell[i].AllocVa;
			pMLMEContext->pUrb = RTUSB_ALLOC_URB(0);
			if (pMLMEContext->pUrb == NULL)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("<-- ERROR in Alloc TX MLMEContext[%d] urb!! \n", i));
				Status = NDIS_STATUS_RESOURCES;
				goto err;
			}
			pMLMEContext->pAd = pAd;
			pMLMEContext->pIrp = NULL;
			pMLMEContext->TransferBuffer = NULL;
			pMLMEContext->InUse = FALSE;
			pMLMEContext->IRPPending = FALSE;
			pMLMEContext->bWaitingBulkOut = FALSE;
			pMLMEContext->BulkOutSize = 0;
			pMLMEContext->SelfIdx = i;

			/* Offset to next ring descriptor address*/
			RingBaseVa = (PUCHAR) RingBaseVa + sizeof(TX_CONTEXT);
		}
		DBGPRINT(RT_DEBUG_TRACE, ("MGMT Ring: total %d entry allocated\n", i));

		/*pAd->MgmtRing.TxSwFreeIdx = (MGMT_RING_SIZE - 1);*/
		pAd->MgmtRing.TxSwFreeIdx = MGMT_RING_SIZE;
		pAd->MgmtRing.TxCpuIdx = 0;
		pAd->MgmtRing.TxDmaIdx = 0;


		/* NullContext URB and usb buffer*/

		NdisZeroMemory(pNullContext, sizeof(TX_CONTEXT));
		Status = RTMPAllocUsbBulkBufStruct(pAd,
											&pNullContext->pUrb,
											&pNullContext->TransferBuffer,
											sizeof(TX_BUFFER),
											&pNullContext->data_dma,
											"TxNullContext");
		if (Status != NDIS_STATUS_SUCCESS)
			goto err;

		pNullContext->pAd = pAd;
		pNullContext->pIrp = NULL;
		pNullContext->InUse = FALSE;
		pNullContext->IRPPending = FALSE;


		/* PsPollContext URB and usb buffer*/

		Status = RTMPAllocUsbBulkBufStruct(pAd,
											&pPsPollContext->pUrb,
											&pPsPollContext->TransferBuffer,
											sizeof(TX_BUFFER),
											&pPsPollContext->data_dma,
											"TxPsPollContext");
		if (Status != NDIS_STATUS_SUCCESS)
			goto err;

		pPsPollContext->pAd = pAd;
		pPsPollContext->pIrp = NULL;
		pPsPollContext->InUse = FALSE;
		pPsPollContext->IRPPending = FALSE;
		pPsPollContext->bAggregatible = FALSE;
		pPsPollContext->LastOne = TRUE;

	}while (FALSE);


	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICInitTransmit(Status=%d)\n", Status));

	return Status;


	/* --------------------------- ERROR HANDLE --------------------------- */
err:
	/* Free PsPoll frame resource*/
	RTMPFreeUsbBulkBufStruct(pAd,
								&pPsPollContext->pUrb,
								&pPsPollContext->TransferBuffer,
								sizeof(TX_BUFFER),
								pPsPollContext->data_dma);

	/* Free NULL frame resource*/
	RTMPFreeUsbBulkBufStruct(pAd,
								&pNullContext->pUrb,
								&pNullContext->TransferBuffer,
								sizeof(TX_BUFFER),
								pNullContext->data_dma);

	/* MGMT Ring*/
	if (pAd->MgmtDescRing.AllocVa)
	{
		pMgmtRing = &pAd->MgmtRing;
		for(i=0; i<MGMT_RING_SIZE; i++)
		{
			pMLMEContext = (PTX_CONTEXT) pAd->MgmtRing.Cell[i].AllocVa;
			if (pMLMEContext)
			{
				RTMPFreeUsbBulkBufStruct(pAd,
											&pMLMEContext->pUrb,
											&pMLMEContext->TransferBuffer,
											sizeof(TX_BUFFER),
											pMLMEContext->data_dma);
			}
		}
		os_free_mem(pAd, pAd->MgmtDescRing.AllocVa);
		pAd->MgmtDescRing.AllocVa = NULL;
	}


	/* Tx Ring*/
	for (acidx = 0; acidx < 4; acidx++)
	{
		PHT_TX_CONTEXT pHTTxContext = &(pAd->TxContext[acidx]);
		if (pHTTxContext)
		{
			RTMPFreeUsbBulkBufStruct(pAd,
										&pHTTxContext->pUrb,
										&pHTTxContext->TransferBuffer,
										sizeof(HTTX_BUFFER),
										pHTTxContext->data_dma);
		}
	}

	/* Here we didn't have any pre-allocated memory need to free.*/

	return Status;
}


/*
========================================================================
Routine Description:
    Allocate DMA memory blocks for send, receive.

Arguments:
    pAd					Pointer to our adapter

Return Value:
	NDIS_STATUS_SUCCESS
	NDIS_STATUS_FAILURE
	NDIS_STATUS_RESOURCES

Note:
========================================================================
*/
NDIS_STATUS	RTMPAllocTxRxRingMemory(
	IN	PRTMP_ADAPTER	pAd)
{
/*	COUNTER_802_11	pCounter = &pAd->WlanCounters;*/
	NDIS_STATUS		Status = NDIS_STATUS_SUCCESS;
	INT				num;


	DBGPRINT(RT_DEBUG_TRACE, ("--> RTMPAllocTxRxRingMemory\n"));


	do
	{
		/* Init the CmdQ and CmdQLock*/
		NdisAllocateSpinLock(pAd, &pAd->CmdQLock);
		NdisAcquireSpinLock(&pAd->CmdQLock);
		RTInitializeCmdQ(&pAd->CmdQ);
		NdisReleaseSpinLock(&pAd->CmdQLock);


		NdisAllocateSpinLock(pAd, &pAd->MLMEBulkOutLock);
		NdisAllocateSpinLock(pAd, &pAd->BulkInLock);
		for(num =0 ; num < 6; num++)
		{
			NdisAllocateSpinLock(pAd, &pAd->BulkOutLock[num]);
		}

		for (num = 0; num < NUM_OF_TX_RING; num++)
		{
			NdisAllocateSpinLock(pAd, &pAd->TxContextQueueLock[num]);
		}

#ifdef RALINK_ATE
		NdisAllocateSpinLock(pAd, &pAd->GenericLock);
#endif /* RALINK_ATE */



		/* Init send data structures and related parameters*/

		Status = NICInitTransmit(pAd);
		if (Status != NDIS_STATUS_SUCCESS)
			break;


		/* Init receive data structures and related parameters*/

		Status = NICInitRecv(pAd);
		if (Status != NDIS_STATUS_SUCCESS)
			break;

		NdisZeroMemory(&pAd->FragFrame, sizeof(FRAGMENT_FRAME));
		pAd->FragFrame.pFragPacket =  RTMP_AllocateFragPacketBuffer(pAd, RX_BUFFER_NORMSIZE);

		if (pAd->FragFrame.pFragPacket == NULL)
		{
			Status = NDIS_STATUS_RESOURCES;
		}
	} while (FALSE);

	DBGPRINT_S(Status, ("<-- RTMPAllocTxRxRingMemory, Status=%x\n", Status));
	return Status;
}


/*
========================================================================
Routine Description:
	Calls USB_InterfaceStop and frees memory allocated for the URBs
    calls NdisMDeregisterDevice and frees the memory
    allocated in VNetInitialize for the Adapter Object

Arguments:
	*pAd				the raxx interface data pointer

Return Value:
	None

Note:
========================================================================
*/
VOID	RTMPFreeTxRxRingMemory(
	IN	PRTMP_ADAPTER	pAd)
{
	UINT                i, acidx;
	PTX_CONTEXT			pNullContext   = &pAd->NullContext;
	PTX_CONTEXT			pPsPollContext = &pAd->PsPollContext;


	DBGPRINT(RT_DEBUG_ERROR, ("---> RTMPFreeTxRxRingMemory\n"));


	/* Free all resources for the RxRing buffer queue.*/
	for(i=0; i<(RX_RING_SIZE); i++)
	{
		PRX_CONTEXT  pRxContext = &(pAd->RxContext[i]);
		if (pRxContext)
			RTMPFreeUsbBulkBufStruct(pAd,
										&pRxContext->pUrb,
										&pRxContext->TransferBuffer,
										MAX_RXBULK_SIZE,
										pRxContext->data_dma);
	}

	/* Free PsPoll frame resource*/
	RTMPFreeUsbBulkBufStruct(pAd,
								&pPsPollContext->pUrb,
								&pPsPollContext->TransferBuffer,
								sizeof(TX_BUFFER),
								pPsPollContext->data_dma);

	/* Free NULL frame resource*/
	RTMPFreeUsbBulkBufStruct(pAd,
								&pNullContext->pUrb,
								&pNullContext->TransferBuffer,
								sizeof(TX_BUFFER),
								pNullContext->data_dma);

	/* Free mgmt frame resource*/
	for(i = 0; i < MGMT_RING_SIZE; i++)
	{
		PTX_CONTEXT pMLMEContext = (PTX_CONTEXT)pAd->MgmtRing.Cell[i].AllocVa;
		if (pMLMEContext)
		{
			if (NULL != pMLMEContext->pUrb)
			{
				RTUSB_UNLINK_URB(pMLMEContext->pUrb);
				RTUSB_FREE_URB(pMLMEContext->pUrb);
				pMLMEContext->pUrb = NULL;
			}
		}

		if (NULL != pAd->MgmtRing.Cell[i].pNdisPacket)
		{
			RTMPFreeNdisPacket(pAd, pAd->MgmtRing.Cell[i].pNdisPacket);
			pAd->MgmtRing.Cell[i].pNdisPacket = NULL;
			if (pMLMEContext)
			pMLMEContext->TransferBuffer = NULL;
		}

	}
	if (pAd->MgmtDescRing.AllocVa)
		os_free_mem(pAd, pAd->MgmtDescRing.AllocVa);


	/* Free Tx frame resource*/
	for (acidx = 0; acidx < 4; acidx++)
		{
		PHT_TX_CONTEXT pHTTXContext = &(pAd->TxContext[acidx]);
			if (pHTTXContext)
			RTMPFreeUsbBulkBufStruct(pAd,
										&pHTTXContext->pUrb,
										&pHTTXContext->TransferBuffer,
										sizeof(HTTX_BUFFER),
										pHTTXContext->data_dma);
#ifdef USB_BULK_BUF_ALIGMENT2
			if (pHTTXContext->pBuf != NULL)
			{
				RTUSB_URB_FREE_BUFFER(pObj->pUsb_Dev, 26624,
					pHTTXContext->pBuf,
					pHTTXContext->data_dma2);
					pHTTXContext->pBuf = NULL;
			}

#endif /* USB_BULK_BUF_ALIGMENT2 */
		}

	/* Free fragement frame buffer*/
	if (pAd->FragFrame.pFragPacket)
		RELEASE_NDIS_PACKET(pAd, pAd->FragFrame.pFragPacket, NDIS_STATUS_SUCCESS);


	/* Free spinlocks*/
	for(i=0; i<6; i++)
	{
		NdisFreeSpinLock(&pAd->BulkOutLock[i]);
	}

	NdisFreeSpinLock(&pAd->BulkInLock);
	NdisFreeSpinLock(&pAd->MLMEBulkOutLock);

	NdisFreeSpinLock(&pAd->CmdQLock);
#ifdef RALINK_ATE
	NdisFreeSpinLock(&pAd->GenericLock);
#endif /* RALINK_ATE */

	/* Clear all pending bulk-out request flags.*/
	RTUSB_CLEAR_BULK_FLAG(pAd, 0xffffffff);

	for (i = 0; i < NUM_OF_TX_RING; i++)
	{
		NdisFreeSpinLock(&pAd->TxContextQueueLock[i]);
	}

	DBGPRINT(RT_DEBUG_ERROR, ("<--- RTMPFreeTxRxRingMemory\n"));
}

#endif /* RESOURCE_PRE_ALLOC */


/*
========================================================================
Routine Description:
    Write WLAN MAC address to USB 2870.

Arguments:
    pAd					Pointer to our adapter

Return Value:
	NDIS_STATUS_SUCCESS

Note:
========================================================================
*/
NDIS_STATUS	RTUSBWriteHWMACAddress(
	IN	PRTMP_ADAPTER		pAd)
{
	MAC_DW0_STRUC	StaMacReg0;
	MAC_DW1_STRUC	StaMacReg1;
	NDIS_STATUS		Status = NDIS_STATUS_SUCCESS;
	LARGE_INTEGER	NOW;


	/* initialize the random number generator*/
	RTMP_GetCurrentSystemTime(&NOW);

	/* Write New MAC address to MAC_CSR2 & MAC_CSR3 & let ASIC know our new MAC*/
	StaMacReg0.field.Byte0 = pAd->CurrentAddress[0];
	StaMacReg0.field.Byte1 = pAd->CurrentAddress[1];
	StaMacReg0.field.Byte2 = pAd->CurrentAddress[2];
	StaMacReg0.field.Byte3 = pAd->CurrentAddress[3];
	StaMacReg1.field.Byte4 = pAd->CurrentAddress[4];
	StaMacReg1.field.Byte5 = pAd->CurrentAddress[5];
	StaMacReg1.field.U2MeMask = 0xff;
	DBGPRINT_RAW(RT_DEBUG_TRACE, ("Local MAC = %02x:%02x:%02x:%02x:%02x:%02x\n",
			pAd->CurrentAddress[0], pAd->CurrentAddress[1], pAd->CurrentAddress[2],
			pAd->CurrentAddress[3], pAd->CurrentAddress[4], pAd->CurrentAddress[5]));

	RTUSBWriteMACRegister(pAd, MAC_ADDR_DW0, StaMacReg0.word);
	RTUSBWriteMACRegister(pAd, MAC_ADDR_DW1, StaMacReg1.word);
	return Status;
}


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
VOID RT28XXDMADisable(
	IN RTMP_ADAPTER 		*pAd)
{
	/* no use*/
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
VOID RT28XXDMAEnable(
	IN RTMP_ADAPTER 		*pAd)
{
	WPDMA_GLO_CFG_STRUC	GloCfg;
	USB_DMA_CFG_STRUC	UsbCfg;
	int					i = 0;


	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x4);
	do
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))
			return;
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);
		if ((GloCfg.field.TxDMABusy == 0)  && (GloCfg.field.RxDMABusy == 0))
			break;

		DBGPRINT(RT_DEBUG_TRACE, ("==>  DMABusy\n"));
		RTMPusecDelay(1000);
		i++;
	}while ( i <200);


	RTMPusecDelay(50);
	GloCfg.field.EnTXWriteBackDDONE = 1;
	GloCfg.field.EnableRxDMA = 1;
	GloCfg.field.EnableTxDMA = 1;
	DBGPRINT(RT_DEBUG_TRACE, ("<== WRITE DMA offset 0x208 = 0x%x\n", GloCfg.word));
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);

	UsbCfg.word = 0;
	UsbCfg.field.phyclear = 0;
	/* usb version is 1.1,do not use bulk in aggregation */
	if (pAd->BulkInMaxPacketSize == 512)
			UsbCfg.field.RxBulkAggEn = 1;
	/* for last packet, PBF might use more than limited, so minus 2 to prevent from error */
	UsbCfg.field.RxBulkAggLmt = (MAX_RXBULK_SIZE /1024)-3;
	UsbCfg.field.RxBulkAggTOut = 0x80; /* 2006-10-18 */
	UsbCfg.field.RxBulkEn = 1;
	UsbCfg.field.TxBulkEn = 1;

	RTUSBWriteMACRegister(pAd, USB_DMA_CFG, UsbCfg.word);

}

/********************************************************************
  *
  *	2870 Beacon Update Related functions.
  *
  ********************************************************************/

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
VOID RT28xx_UpdateBeaconToAsic(
	IN RTMP_ADAPTER		*pAd,
	IN INT				apidx,
	IN ULONG			FrameLen,
	IN ULONG			UpdatePos)
{
	PUCHAR        	pBeaconFrame = NULL;
	UCHAR  			*ptr;
	UINT  			i, padding;
	BEACON_SYNC_STRUCT	*pBeaconSync = pAd->CommonCfg.pBeaconSync;
	UINT32			longValue;
/*	USHORT			shortValue;*/
	BOOLEAN			bBcnReq = FALSE;
	UCHAR			bcn_idx = 0;
#ifdef SPECIFIC_BCN_BUF_SUPPORT
	unsigned long irqFlag;
#endif /* SPECIFIC_BCN_BUF_SUPPORT */


	if (pBeaconFrame == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR,("pBeaconFrame is NULL!\n"));
		return;
	}

	if (pBeaconSync == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR,("pBeaconSync is NULL!\n"));
		return;
	}

	if (bBcnReq == FALSE)
	{
#ifdef SPECIFIC_BCN_BUF_SUPPORT
		RTMP_MAC_SHR_MSEL_LOCK(pAd, HIGHER_SHRMEM, irqFlag);
#endif /* SPECIFIC_BCN_BUF_SUPPORT */
		/* when the ra interface is down, do not send its beacon frame */
		/* clear all zero */
		for(i=0; i<TXWI_SIZE; i+=4) {
			RTMP_IO_WRITE32(pAd, pAd->BeaconOffset[bcn_idx] + i, 0x00);
		}
#ifdef SPECIFIC_BCN_BUF_SUPPORT
		RTMP_MAC_SHR_MSEL_UNLOCK(pAd, LOWER_SHRMEM, irqFlag);
#endif /* SPECIFIC_BCN_BUF_SUPPORT */

		pBeaconSync->BeaconBitMap &= (~(BEACON_BITMAP_MASK & (1 << bcn_idx)));
		NdisZeroMemory(pBeaconSync->BeaconTxWI[bcn_idx], TXWI_SIZE);
	}
	else
	{
		ptr = (PUCHAR)&pAd->BeaconTxWI;
#ifdef RT_BIG_ENDIAN
		RTMPWIEndianChange(ptr, TYPE_TXWI);
#endif
		if (NdisEqualMemory(pBeaconSync->BeaconTxWI[bcn_idx], &pAd->BeaconTxWI, TXWI_SIZE) == FALSE)
		{	/* If BeaconTxWI changed, we need to rewrite the TxWI for the Beacon frames.*/
			pBeaconSync->BeaconBitMap &= (~(BEACON_BITMAP_MASK & (1 << bcn_idx)));
			NdisMoveMemory(pBeaconSync->BeaconTxWI[bcn_idx], &pAd->BeaconTxWI, TXWI_SIZE);
		}

#ifdef SPECIFIC_BCN_BUF_SUPPORT
		/*
			Shared memory access selection (higher 8KB shared memory)
		*/
		RTMP_MAC_SHR_MSEL_LOCK(pAd, HIGHER_SHRMEM, irqFlag);
#endif /* SPECIFIC_BCN_BUF_SUPPORT */
		if ((pBeaconSync->BeaconBitMap & (1 << bcn_idx)) != (1 << bcn_idx))
		{
			for (i=0; i<TXWI_SIZE; i+=4)  /* 16-byte TXWI field*/
			{
				longValue =  *ptr + (*(ptr+1)<<8) + (*(ptr+2)<<16) + (*(ptr+3)<<24);
				RTMP_IO_WRITE32(pAd, pAd->BeaconOffset[bcn_idx] + i, longValue);
				ptr += 4;
			}
		}

		ptr = pBeaconSync->BeaconBuf[bcn_idx];
		padding = (FrameLen & 0x01);
		NdisZeroMemory((PUCHAR)(pBeaconFrame + FrameLen), padding);
		FrameLen += padding;
		for (i = 0 ; i < FrameLen /*HW_BEACON_OFFSET*/; i += 2)
		{
			if (NdisEqualMemory(ptr, pBeaconFrame, 2) == FALSE)
			{
				NdisMoveMemory(ptr, pBeaconFrame, 2);
				/*shortValue = *ptr + (*(ptr+1)<<8);*/
				/*RTMP_IO_WRITE8(pAd, pAd->BeaconOffset[bcn_idx] + TXWI_SIZE + i, shortValue);*/
				RTUSBMultiWrite(pAd, pAd->BeaconOffset[bcn_idx] + TXWI_SIZE + i, ptr, 2);
			}
			ptr +=2;
			pBeaconFrame += 2;
		}

#ifdef SPECIFIC_BCN_BUF_SUPPORT
		/*
			Shared memory access selection (lower 16KB shared memory)
		*/
		RTMP_MAC_SHR_MSEL_UNLOCK(pAd, LOWER_SHRMEM, irqFlag);
#endif /* SPECIFIC_BCN_BUF_SUPPORT */

		pBeaconSync->BeaconBitMap |= (1 << bcn_idx);

		/* For AP interface, set the DtimBitOn so that we can send Bcast/Mcast frame out after this beacon frame.*/
}

}


VOID RTUSBBssBeaconStop(
	IN RTMP_ADAPTER *pAd)
{
	BEACON_SYNC_STRUCT	*pBeaconSync;
	int i, offset;
	BOOLEAN	Cancelled = TRUE;

	pBeaconSync = pAd->CommonCfg.pBeaconSync;
	if (pBeaconSync && pBeaconSync->EnableBeacon)
	{
		INT NumOfBcn = 0;


#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			NumOfBcn = MAX_MESH_NUM;
		}
#endif /* CONFIG_STA_SUPPORT */

		RTMPCancelTimer(&pAd->CommonCfg.BeaconUpdateTimer, &Cancelled);

		for(i=0; i<NumOfBcn; i++)
		{
			NdisZeroMemory(pBeaconSync->BeaconBuf[i], HW_BEACON_OFFSET);
			NdisZeroMemory(pBeaconSync->BeaconTxWI[i], TXWI_SIZE);

			for (offset=0; offset<HW_BEACON_OFFSET; offset+=4)
				RTMP_IO_WRITE32(pAd, pAd->BeaconOffset[i] + offset, 0x00);

			pBeaconSync->CapabilityInfoLocationInBeacon[i] = 0;
			pBeaconSync->TimIELocationInBeacon[i] = 0;
		}
		pBeaconSync->BeaconBitMap = 0;
		pBeaconSync->DtimBitOn = 0;
	}
}


VOID RTUSBBssBeaconStart(
	IN RTMP_ADAPTER *pAd)
{
	int apidx;
	BEACON_SYNC_STRUCT	*pBeaconSync;
/*	LARGE_INTEGER 	tsfTime, deltaTime;*/

	pBeaconSync = pAd->CommonCfg.pBeaconSync;
	if (pBeaconSync && pBeaconSync->EnableBeacon)
	{
		INT NumOfBcn = 0;


#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			NumOfBcn = MAX_MESH_NUM;
		}
#endif /* CONFIG_STA_SUPPORT */

		for(apidx=0; apidx<NumOfBcn; apidx++)
		{
			UCHAR CapabilityInfoLocationInBeacon = 0;
			UCHAR TimIELocationInBeacon = 0;

#ifdef CONFIG_STA_SUPPORT
#endif /* CONFIG_STA_SUPPORT */

			NdisZeroMemory(pBeaconSync->BeaconBuf[apidx], HW_BEACON_OFFSET);
			pBeaconSync->CapabilityInfoLocationInBeacon[apidx] = CapabilityInfoLocationInBeacon;
			pBeaconSync->TimIELocationInBeacon[apidx] = TimIELocationInBeacon;
			NdisZeroMemory(pBeaconSync->BeaconTxWI[apidx], TXWI_SIZE);
		}
		pBeaconSync->BeaconBitMap = 0;
		pBeaconSync->DtimBitOn = 0;
		pAd->CommonCfg.BeaconUpdateTimer.Repeat = TRUE;

		pAd->CommonCfg.BeaconAdjust = 0;
		pAd->CommonCfg.BeaconFactor = 0xffffffff / (pAd->CommonCfg.BeaconPeriod << 10);
		pAd->CommonCfg.BeaconRemain = (0xffffffff % (pAd->CommonCfg.BeaconPeriod << 10)) + 1;
		DBGPRINT(RT_DEBUG_TRACE, ("RTUSBBssBeaconStart:BeaconFactor=%d, BeaconRemain=%d!\n",
									pAd->CommonCfg.BeaconFactor, pAd->CommonCfg.BeaconRemain));
		RTMPSetTimer(&pAd->CommonCfg.BeaconUpdateTimer, 10 /*pAd->CommonCfg.BeaconPeriod*/);

	}
}


VOID RTUSBBssBeaconInit(
	IN RTMP_ADAPTER *pAd)
{
	BEACON_SYNC_STRUCT	*pBeaconSync;
	int i;

	os_alloc_mem(pAd, (PUCHAR *)(&pAd->CommonCfg.pBeaconSync), sizeof(BEACON_SYNC_STRUCT));
	/*NdisAllocMemory(pAd->CommonCfg.pBeaconSync, sizeof(BEACON_SYNC_STRUCT), MEM_ALLOC_FLAG);*/
	if (pAd->CommonCfg.pBeaconSync)
	{
		pBeaconSync = pAd->CommonCfg.pBeaconSync;
		NdisZeroMemory(pBeaconSync, sizeof(BEACON_SYNC_STRUCT));
		for(i=0; i < HW_BEACON_MAX_COUNT(pAd); i++)
		{
			NdisZeroMemory(pBeaconSync->BeaconBuf[i], HW_BEACON_OFFSET);
			pBeaconSync->CapabilityInfoLocationInBeacon[i] = 0;
			pBeaconSync->TimIELocationInBeacon[i] = 0;
			NdisZeroMemory(pBeaconSync->BeaconTxWI[i], TXWI_SIZE);
		}
		pBeaconSync->BeaconBitMap = 0;

		/*RTMPInitTimer(pAd, &pAd->CommonCfg.BeaconUpdateTimer, GET_TIMER_FUNCTION(BeaconUpdateExec), pAd, TRUE);*/
		pBeaconSync->EnableBeacon = TRUE;
	}
}


VOID RTUSBBssBeaconExit(
	IN RTMP_ADAPTER *pAd)
{
	BEACON_SYNC_STRUCT	*pBeaconSync;
	BOOLEAN	Cancelled = TRUE;
	int i;

	if (pAd->CommonCfg.pBeaconSync)
	{
		pBeaconSync = pAd->CommonCfg.pBeaconSync;
		pBeaconSync->EnableBeacon = FALSE;
		RTMPCancelTimer(&pAd->CommonCfg.BeaconUpdateTimer, &Cancelled);
		pBeaconSync->BeaconBitMap = 0;

		for(i=0; i<HW_BEACON_MAX_COUNT(pAd); i++)
		{
			NdisZeroMemory(pBeaconSync->BeaconBuf[i], HW_BEACON_OFFSET);
			pBeaconSync->CapabilityInfoLocationInBeacon[i] = 0;
			pBeaconSync->TimIELocationInBeacon[i] = 0;
			NdisZeroMemory(pBeaconSync->BeaconTxWI[i], TXWI_SIZE);
		}

		os_free_mem(pAd, pAd->CommonCfg.pBeaconSync);
		pAd->CommonCfg.pBeaconSync = NULL;
	}
}


/*
    ========================================================================
    Routine Description:
        For device work as AP mode but didn't have TBTT interrupt event, we need a mechanism
        to update the beacon context in each Beacon interval. Here we use a periodical timer
        to simulate the TBTT interrupt to handle the beacon context update.

    Arguments:
        SystemSpecific1         - Not used.
        FunctionContext         - Pointer to our Adapter context.
        SystemSpecific2         - Not used.
        SystemSpecific3         - Not used.

    Return Value:
        None

    ========================================================================
*/
VOID BeaconUpdateExec(
    IN PVOID SystemSpecific1,
    IN PVOID FunctionContext,
    IN PVOID SystemSpecific2,
    IN PVOID SystemSpecific3)
{
	PRTMP_ADAPTER	pAd = (PRTMP_ADAPTER)FunctionContext;
	LARGE_INTEGER	tsfTime_a;/*, tsfTime_b, deltaTime_exp, deltaTime_ab;*/
	UINT32			delta, delta2MS, period2US, remain, remain_low, remain_high;
/*	BOOLEAN			positive;*/

	if (pAd->CommonCfg.IsUpdateBeacon==TRUE)
	{
		ReSyncBeaconTime(pAd);


	}

	RTMP_IO_READ32(pAd, TSF_TIMER_DW0, &tsfTime_a.u.LowPart);
	RTMP_IO_READ32(pAd, TSF_TIMER_DW1, &tsfTime_a.u.HighPart);


	/*
		Calculate next beacon time to wake up to update.

		BeaconRemain = (0xffffffff % (pAd->CommonCfg.BeaconPeriod << 10)) + 1;

		Background: Timestamp (us) % Beacon Period (us) shall be 0 at TBTT

		Formula:	(a+b) mod m = ((a mod m) + (b mod m)) mod m
					(a*b) mod m = ((a mod m) * (b mod m)) mod m

		==> ((HighPart * 0xFFFFFFFF) + LowPart) mod Beacon_Period
		==> (((HighPart * 0xFFFFFFFF) mod Beacon_Period) +
			(LowPart mod (Beacon_Period))) mod Beacon_Period
		==> ((HighPart mod Beacon_Period) * (0xFFFFFFFF mod Beacon_Period)) mod
			Beacon_Period

		Steps:
		1. Calculate the delta time between now and next TBTT;

			delta time = (Beacon Period) - ((64-bit timestamp) % (Beacon Period))

			(1) If no overflow for LowPart, 32-bit, we can calcualte the delta
				time by using LowPart;

				delta time = LowPart % (Beacon Period)

			(2) If overflow for LowPart, we need to care about HighPart value;

				delta time = (BeaconRemain * HighPart + LowPart) % (Beacon Period)

				Ex: if the maximum value is 0x00 0xFF (255), Beacon Period = 100,
					TBTT timestamp will be 100, 200, 300, 400, ...
					when TBTT timestamp is 300 = 1*56 + 44, means HighPart = 1,
					Low Part = 44

		2. Adjust next update time of the timer to (delta time + 10ms).
	*/

	/*positive=getDeltaTime(tsfTime_a, expectedTime, &deltaTime_exp);*/
	period2US = (pAd->CommonCfg.BeaconPeriod << 10);
	remain_high = pAd->CommonCfg.BeaconRemain * tsfTime_a.u.HighPart;
	remain_low = tsfTime_a.u.LowPart % (pAd->CommonCfg.BeaconPeriod << 10);
	remain = (remain_high + remain_low)%(pAd->CommonCfg.BeaconPeriod << 10);
	delta = (pAd->CommonCfg.BeaconPeriod << 10) - remain;

	delta2MS = (delta>>10);
	if (delta2MS > 150)
	{
		pAd->CommonCfg.BeaconUpdateTimer.TimerValue = 100;
		pAd->CommonCfg.IsUpdateBeacon=FALSE;
	}
	else
	{
		pAd->CommonCfg.BeaconUpdateTimer.TimerValue = delta2MS + 10;
		pAd->CommonCfg.IsUpdateBeacon=TRUE;
	}

}


/********************************************************************
  *
  *	2870 Radio on/off Related functions.
  *
  ********************************************************************/
VOID RT28xxUsbMlmeRadioOn(
	IN PRTMP_ADAPTER pAd)
{

    DBGPRINT(RT_DEBUG_TRACE,("RT28xxUsbMlmeRadioOn()\n"));

	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF))
		return;

	RT28xxUsbAsicRadioOn(pAd);


	/* Clear Radio off flag*/
	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);


#ifdef LED_CONTROL_SUPPORT
	/* Set LED*/
	RTMPSetLED(pAd, LED_RADIO_ON);
#endif /* LED_CONTROL_SUPPORT */

#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	if (IS_RT5390(pAd))
	{
		if (pAd->NicConfig2.field.AntOpt == 1)
		{
			if (pAd->NicConfig2.field.AntDiversity == 0)
			{
			 /* Main antenna */
				AsicSetRxAnt(pAd, 0);
			}
			else
			{
			 /* Aux. antenna */
				AsicSetRxAnt(pAd, 1);
			}
		}
	}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
}


VOID RT28xxUsbMlmeRadioOFF(
	IN PRTMP_ADAPTER pAd)
{

	DBGPRINT(RT_DEBUG_TRACE,("RT28xxUsbMlmeRadioOFF()\n"));

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF))
		return;


#ifdef CONFIG_STA_SUPPORT
	/* Clear PMKID cache.*/
	pAd->StaCfg.SavedPMKNum = 0;
	RTMPZeroMemory(pAd->StaCfg.SavedPMK, (PMKID_NO * sizeof(BSSID_INFO)));

	/* Link down first if any association exists*/
	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))
	{
		if (INFRA_ON(pAd) || ADHOC_ON(pAd))
		{
			MLME_DISASSOC_REQ_STRUCT DisReq;
			MLME_QUEUE_ELEM *pMsgElem; /* = (MLME_QUEUE_ELEM *) kmalloc(sizeof(MLME_QUEUE_ELEM), MEM_ALLOC_FLAG);*/

			os_alloc_mem(pAd, (UCHAR **)&pMsgElem, sizeof(MLME_QUEUE_ELEM));
			if (pMsgElem)
			{
				COPY_MAC_ADDR(&DisReq.Addr, pAd->CommonCfg.Bssid);
				DisReq.Reason =  REASON_DISASSOC_STA_LEAVING;

				pMsgElem->Machine = ASSOC_STATE_MACHINE;
				pMsgElem->MsgType = MT2_MLME_DISASSOC_REQ;
				pMsgElem->MsgLen = sizeof(MLME_DISASSOC_REQ_STRUCT);
				NdisMoveMemory(pMsgElem->Msg, &DisReq, sizeof(MLME_DISASSOC_REQ_STRUCT));

				MlmeDisassocReqAction(pAd, pMsgElem);
/*				kfree(pMsgElem);*/
				os_free_mem(NULL, pMsgElem);

				RTMPusecDelay(1000);
			}
		}
	}
#endif /* CONFIG_STA_SUPPORT */

	/* Set Radio off flag*/
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		/* Link down first if any association exists*/
		if (INFRA_ON(pAd) || ADHOC_ON(pAd))
			LinkDown(pAd, FALSE);
		RTMPusecDelay(10000);

		/*==========================================*/
		/* Clean up old bss table*/
#ifndef ANDROID_SUPPORT
/* because abdroid will get scan table when interface down, so we not clean scan table */
		BssTableInit(&pAd->ScanTab);
#endif /* ANDROID_SUPPORT */
	}
#endif /* CONFIG_STA_SUPPORT */

#ifdef LED_CONTROL_SUPPORT
	/* Set LED*/
	RTMPSetLED(pAd, LED_RADIO_OFF);
#endif /* LED_CONTROL_SUPPORT */


	RT28xxUsbAsicRadioOff(pAd);

}

VOID RT28xxUsbAsicRadioOff(
	IN PRTMP_ADAPTER pAd)
{
	WPDMA_GLO_CFG_STRUC	GloCfg;
       INT                              i;
	UINT32				Value;

	DBGPRINT(RT_DEBUG_TRACE, ("--> %s\n", __FUNCTION__));

	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF);

	if (pAd->CommonCfg.BBPCurrentBW == BW_40)
	{
		/* Must using 40MHz.*/
		AsicTurnOffRFClk(pAd, pAd->CommonCfg.CentralChannel);
	}
	else
	{
		/* Must using 20MHz.*/
		AsicTurnOffRFClk(pAd, pAd->CommonCfg.Channel);
	}

	/* Disable Tx/Rx DMA*/
	RTUSBReadMACRegister(pAd, WPDMA_GLO_CFG, &GloCfg.word);	   /* disable DMA */
	GloCfg.field.EnableTxDMA = 0;
	GloCfg.field.EnableRxDMA = 0;
	RTUSBWriteMACRegister(pAd, WPDMA_GLO_CFG, GloCfg.word);	   /* abort all TX rings*/

	/* Waiting for DMA idle*/
	i = 0;
	do
	{
		RTUSBReadMACRegister(pAd, WPDMA_GLO_CFG, &GloCfg.word);
		if ((GloCfg.field.TxDMABusy == 0) && (GloCfg.field.RxDMABusy == 0))
			break;

		RTMPusecDelay(1000);
	}while (i++ < 100);

	/* Disable MAC Tx/Rx*/
	RTUSBReadMACRegister(pAd, MAC_SYS_CTRL, &Value);
	Value &= (0xfffffff3);
	RTUSBWriteMACRegister(pAd, MAC_SYS_CTRL, Value);

#ifdef CONFIG_STA_SUPPORT
	//AsicSendCommandToMcu(pAd, 0x30, 0xff, 0xff, 0x02);   /* send POWER-SAVE command to MCU. Timeout 40us.*/

	/* Stop bulkin pipe*/
	if((pAd->PendingRx > 0) && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
	{
		RTUSBCancelPendingBulkInIRP(pAd);
		pAd->PendingRx = 0;
	}
#endif /* CONFIG_STA_SUPPORT */
	DBGPRINT(RT_DEBUG_TRACE, ("<== %s\n", __FUNCTION__));

}


VOID RT28xxUsbAsicRadioOn(
	IN PRTMP_ADAPTER pAd)
{
	UINT32                MACValue = 0;
	BOOLEAN              brc;
	UINT                 RetryRound = 0;
	UINT32 rx_filter_flag;
	WPDMA_GLO_CFG_STRUC	GloCfg;
	INT i=0;
	UCHAR	rfreg;
	RTMP_CHIP_OP *pChipOps = &pAd->chipOps;

#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
	POS_COOKIE  pObj = (POS_COOKIE) pAd->OS_Cookie;


	DBGPRINT(RT_DEBUG_TRACE, ("--> %s\n", __FUNCTION__));

	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND))
	{
		if( (RTMP_Usb_AutoPM_Get_Interface(pObj->pUsb_Dev,pObj->intf)) == 1)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("RT28xxUsbAsicRadioOn: autopm_resume success\n"));
			RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_SUSPEND);
		}
		else if ((RTMP_Usb_AutoPM_Get_Interface(pObj->pUsb_Dev,pObj->intf)) == (-1))
		{
			DBGPRINT(RT_DEBUG_ERROR, ("RT28xxUsbAsicRadioOn autopm_resume fail ------\n"));
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_SUSPEND);
			return;
		}
		else
			DBGPRINT(RT_DEBUG_TRACE, ("RT28xxUsbAsicRadioOn: autopm_resume do nothing \n"));

	}
	else
	{
		DBGPRINT(RT_DEBUG_TRACE, ("RT28xxUsbAsicRadioOn: fRTMP_ADAPTER_CPU_SUSPEND\n"));
		return;
	}

#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */


	/* make some traffic to invoke EvtDeviceD0Entry callback function*/


	RTUSBReadMACRegister(pAd,0x1000,&MACValue);
	DBGPRINT(RT_DEBUG_TRACE,("A MAC query to invoke EvtDeviceD0Entry, MACValue = 0x%x\n",MACValue));

	/* 1. Send wake up command.*/
	RetryRound = 0;

#if 0
	do
	{
		brc = AsicSendCommandToMcu(pAd, 0x31, PowerWakeCID, 0x00, 0x02);
		if (brc)
		{
			/* Wait command ok.*/
			brc = AsicCheckCommandOk(pAd, PowerWakeCID);
		}
		if(brc){
			break;      /* PowerWakeCID cmd successed*/
		}
		DBGPRINT(RT_DEBUG_WARN, ("PSM :WakeUp Cmd Failed, retry %d\n", RetryRound));

		/* try 10 times at most*/
		if ((RetryRound++) > 10)
			break;
		/* delay and try again*/
		RTMPusecDelay(200);
	} while (TRUE);
	if (RetryRound > 10)
		DBGPRINT(RT_DEBUG_WARN, ("PSM :ASIC 0x31 WakeUp Cmd may Fail %d*******\n", RetryRound));
#endif


	/* 2. Enable Tx DMA.*/

	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x4);
	do
	{
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);
		if ((GloCfg.field.TxDMABusy == 0)  && (GloCfg.field.RxDMABusy == 0))
			break;

		DBGPRINT(RT_DEBUG_TRACE, ("==>  DMABusy\n"));
		RTMPusecDelay(1000);
		i++;
	}while ( i <200);


	RTMPusecDelay(50);
	GloCfg.field.EnTXWriteBackDDONE = 1;
	GloCfg.field.EnableRxDMA = 1;
	GloCfg.field.EnableTxDMA = 1;
	DBGPRINT(RT_DEBUG_TRACE, ("<== WRITE DMA offset 0x208 = 0x%x\n", GloCfg.word));
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);


	/* enable RX of MAC block*/



#ifdef XLINK_SUPPORT
		if (pAd->StaCfg.PSPXlink)
			rx_filter_flag = PSPXLINK;
		else
#endif /* XLINK_SUPPORT */
			rx_filter_flag = STANORMAL;     /* Staion not drop control frame will fail WiFi Certification.*/
		RTMP_IO_WRITE32(pAd, RX_FILTR_CFG, rx_filter_flag);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0xc);

	/* 3. Turn on RF*/
/*	RT28xxUsbAsicRFOn(pAd);*/
	if (pChipOps->AsicReverseRfFromSleepMode)
		pChipOps->AsicReverseRfFromSleepMode(pAd, FALSE);

#ifdef RTMP_RF_RW_SUPPORT
/*for 3xxx ? need to reset R07 for VO......*/
           RT30xxReadRFRegister(pAd, RF_R07, &rfreg);
           rfreg = rfreg | 0x1;
           RT30xxWriteRFRegister(pAd, RF_R07, rfreg);
#endif /* RTMP_RF_RW_SUPPORT */

	/* 4. Clear idle flag*/
	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF);





	/* Send Bulkin IRPs after flag fRTMP_ADAPTER_IDLE_RADIO_OFF is cleared.*/
	/*	*/
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		RTUSBBulkReceive(pAd);
#endif /* CONFIG_STA_SUPPORT */
	DBGPRINT(RT_DEBUG_TRACE, ("<== %s\n", __FUNCTION__));


}


BOOLEAN AsicCheckCommandOk(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR		 Command)
{
	UINT32	CmdStatus, CID, i;
	UINT32	ThisCIDMask = 0;

	i = 0;
	do
	{
		RTUSBReadMACRegister(pAd, H2M_MAILBOX_CID, &CID);
		if ((CID & CID0MASK) == Command)
		{
			ThisCIDMask = CID0MASK;
			break;
		}
		else if ((((CID & CID1MASK)>>8) & 0xff) == Command)
		{
			ThisCIDMask = CID1MASK;
			break;
		}
		else if ((((CID & CID2MASK)>>16) & 0xff) == Command)
		{
			ThisCIDMask = CID2MASK;
			break;
		}
		else if ((((CID & CID3MASK)>>24) & 0xff) == Command)
		{
			ThisCIDMask = CID3MASK;
			break;
		}

		RTMPusecDelay(100);
		i++;
	}while (i < 200);

	RTUSBReadMACRegister(pAd, H2M_MAILBOX_STATUS, &CmdStatus);
	if (i < 200)
	{
		if (((CmdStatus & ThisCIDMask) == 0x1) || ((CmdStatus & ThisCIDMask) == 0x100)
			|| ((CmdStatus & ThisCIDMask) == 0x10000) || ((CmdStatus & ThisCIDMask) == 0x1000000))
		{
			DBGPRINT(RT_DEBUG_TRACE, ("PSM : --> AsicCheckCommandOk CID = 0x%x, CmdStatus= 0x%x \n", CID, CmdStatus));
			RTUSBWriteMACRegister(pAd, H2M_MAILBOX_STATUS, 0xffffffff);
			RTUSBWriteMACRegister(pAd, H2M_MAILBOX_CID, 0xffffffff);
			return TRUE;
		}
		DBGPRINT(RT_DEBUG_TRACE, ("--> AsicCheckCommanFail1 CID = 0x%x, CmdStatus= 0x%x \n", CID, CmdStatus));
	}
	else
	{
		DBGPRINT(RT_DEBUG_TRACE, ("--> AsicCheckCommanFail2 Timeout Command = %d, CmdStatus= 0x%x \n", Command, CmdStatus));
	}
	RTUSBWriteMACRegister(pAd, H2M_MAILBOX_STATUS, 0xffffffff);
	RTUSBWriteMACRegister(pAd, H2M_MAILBOX_CID, 0xffffffff);

	return FALSE;

}


#endif /* RTMP_MAC_USB */
