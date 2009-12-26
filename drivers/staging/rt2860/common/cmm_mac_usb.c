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

#ifdef RTMP_MAC_USB

#include	"../rt_config.h"

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
	in struct rt_rtmp_adapter structure and all private data structures. The mahor
	work is to allocate buffer for each packet and chain buffer to
	NDIS packet descriptor.
========================================================================
*/
int NICInitRecv(struct rt_rtmp_adapter *pAd)
{
	u8 i;
	int Status = NDIS_STATUS_SUCCESS;
	struct os_cookie *pObj = (struct os_cookie *)pAd->OS_Cookie;

	DBGPRINT(RT_DEBUG_TRACE, ("--> NICInitRecv\n"));
	pObj = pObj;

	/*InterlockedExchange(&pAd->PendingRx, 0); */
	pAd->PendingRx = 0;
	pAd->NextRxBulkInReadIndex = 0;	/* Next Rx Read index */
	pAd->NextRxBulkInIndex = 0;	/*RX_RING_SIZE -1; // Rx Bulk pointer */
	pAd->NextRxBulkInPosition = 0;

	for (i = 0; i < (RX_RING_SIZE); i++) {
		struct rt_rx_context *pRxContext = &(pAd->RxContext[i]);

		/*Allocate URB */
		pRxContext->pUrb = RTUSB_ALLOC_URB(0);
		if (pRxContext->pUrb == NULL) {
			Status = NDIS_STATUS_RESOURCES;
			goto out1;
		}
		/* Allocate transfer buffer */
		pRxContext->TransferBuffer =
		    RTUSB_URB_ALLOC_BUFFER(pObj->pUsb_Dev, MAX_RXBULK_SIZE,
					   &pRxContext->data_dma);
		if (pRxContext->TransferBuffer == NULL) {
			Status = NDIS_STATUS_RESOURCES;
			goto out1;
		}

		NdisZeroMemory(pRxContext->TransferBuffer, MAX_RXBULK_SIZE);

		pRxContext->pAd = pAd;
		pRxContext->pIrp = NULL;
		pRxContext->InUse = FALSE;
		pRxContext->IRPPending = FALSE;
		pRxContext->Readable = FALSE;
		/*pRxContext->ReorderInUse = FALSE; */
		pRxContext->bRxHandling = FALSE;
		pRxContext->BulkInOffset = 0;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICInitRecv(Status=%d)\n", Status));
	return Status;

out1:
	for (i = 0; i < (RX_RING_SIZE); i++) {
		struct rt_rx_context *pRxContext = &(pAd->RxContext[i]);

		if (NULL != pRxContext->TransferBuffer) {
			RTUSB_URB_FREE_BUFFER(pObj->pUsb_Dev, MAX_RXBULK_SIZE,
					      pRxContext->TransferBuffer,
					      pRxContext->data_dma);
			pRxContext->TransferBuffer = NULL;
		}

		if (NULL != pRxContext->pUrb) {
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
int NICInitTransmit(struct rt_rtmp_adapter *pAd)
{
#define LM_USB_ALLOC(pObj, Context, TB_Type, BufferSize, Status, msg1, err1, msg2, err2)	\
	Context->pUrb = RTUSB_ALLOC_URB(0);		\
	if (Context->pUrb == NULL) {			\
		DBGPRINT(RT_DEBUG_ERROR, msg1);		\
		Status = NDIS_STATUS_RESOURCES;		\
		goto err1; }						\
											\
	Context->TransferBuffer =				\
		(TB_Type)RTUSB_URB_ALLOC_BUFFER(pObj->pUsb_Dev, BufferSize, &Context->data_dma);	\
	if (Context->TransferBuffer == NULL) {	\
		DBGPRINT(RT_DEBUG_ERROR, msg2);		\
		Status = NDIS_STATUS_RESOURCES;		\
		goto err2; }

#define LM_URB_FREE(pObj, Context, BufferSize)				\
	if (NULL != Context->pUrb) {							\
		RTUSB_UNLINK_URB(Context->pUrb);					\
		RTUSB_FREE_URB(Context->pUrb);						\
		Context->pUrb = NULL; }								\
	if (NULL != Context->TransferBuffer) {				\
		RTUSB_URB_FREE_BUFFER(pObj->pUsb_Dev, BufferSize,	\
								Context->TransferBuffer,	\
								Context->data_dma);			\
		Context->TransferBuffer = NULL; }

	u8 i, acidx;
	int Status = NDIS_STATUS_SUCCESS;
	struct rt_tx_context *pNullContext = &(pAd->NullContext);
	struct rt_tx_context *pPsPollContext = &(pAd->PsPollContext);
	struct rt_tx_context *pRTSContext = &(pAd->RTSContext);
	struct rt_tx_context *pMLMEContext = NULL;
/*      struct rt_ht_tx_context *pHTTXContext = NULL; */
	struct os_cookie *pObj = (struct os_cookie *)pAd->OS_Cookie;
	void *RingBaseVa;
/*      struct rt_rtmp_tx_ring *pTxRing; */
	struct rt_rtmp_mgmt_ring *pMgmtRing;

	DBGPRINT(RT_DEBUG_TRACE, ("--> NICInitTransmit\n"));
	pObj = pObj;

	/* Init 4 set of Tx parameters */
	for (acidx = 0; acidx < NUM_OF_TX_RING; acidx++) {
		/* Initialize all Transmit releated queues */
		InitializeQueueHeader(&pAd->TxSwQueue[acidx]);

		/* Next Local tx ring pointer waiting for buck out */
		pAd->NextBulkOutIndex[acidx] = acidx;
		pAd->BulkOutPending[acidx] = FALSE;	/* Buck Out control flag */
		/*pAd->DataBulkDoneIdx[acidx] = 0; */
	}

	/*pAd->NextMLMEIndex    = 0; */
	/*pAd->PushMgmtIndex    = 0; */
	/*pAd->PopMgmtIndex     = 0; */
	/*InterlockedExchange(&pAd->MgmtQueueSize, 0); */
	/*InterlockedExchange(&pAd->TxCount, 0); */

	/*pAd->PrioRingFirstIndex       = 0; */
	/*pAd->PrioRingTxCnt            = 0; */

	do {
		/* */
		/* TX_RING_SIZE, 4 ACs */
		/* */
		for (acidx = 0; acidx < 4; acidx++) {
			struct rt_ht_tx_context *pHTTXContext = &(pAd->TxContext[acidx]);

			NdisZeroMemory(pHTTXContext, sizeof(struct rt_ht_tx_context));
			/*Allocate URB */
			LM_USB_ALLOC(pObj, pHTTXContext, struct rt_httx_buffer *,
				     sizeof(struct rt_httx_buffer), Status,
				     ("<-- ERROR in Alloc TX TxContext[%d] urb!\n",
				      acidx), done,
				     ("<-- ERROR in Alloc TX TxContext[%d] struct rt_httx_buffer!\n",
				      acidx), out1);

			NdisZeroMemory(pHTTXContext->TransferBuffer->
				       Aggregation, 4);
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
		}

		/* */
		/* MGMT_RING_SIZE */
		/* */

		/* Allocate MGMT ring descriptor's memory */
		pAd->MgmtDescRing.AllocSize =
		    MGMT_RING_SIZE * sizeof(struct rt_tx_context);
		os_alloc_mem(pAd, (u8 **) (&pAd->MgmtDescRing.AllocVa),
			     pAd->MgmtDescRing.AllocSize);
		if (pAd->MgmtDescRing.AllocVa == NULL) {
			DBGPRINT_ERR(("Failed to allocate a big buffer for MgmtDescRing!\n"));
			Status = NDIS_STATUS_RESOURCES;
			goto out1;
		}
		NdisZeroMemory(pAd->MgmtDescRing.AllocVa,
			       pAd->MgmtDescRing.AllocSize);
		RingBaseVa = pAd->MgmtDescRing.AllocVa;

		/* Initialize MGMT Ring and associated buffer memory */
		pMgmtRing = &pAd->MgmtRing;
		for (i = 0; i < MGMT_RING_SIZE; i++) {
			/* link the pre-allocated Mgmt buffer to MgmtRing.Cell */
			pMgmtRing->Cell[i].AllocSize = sizeof(struct rt_tx_context);
			pMgmtRing->Cell[i].AllocVa = RingBaseVa;
			pMgmtRing->Cell[i].pNdisPacket = NULL;
			pMgmtRing->Cell[i].pNextNdisPacket = NULL;

			/*Allocate URB for MLMEContext */
			pMLMEContext =
			    (struct rt_tx_context *)pAd->MgmtRing.Cell[i].AllocVa;
			pMLMEContext->pUrb = RTUSB_ALLOC_URB(0);
			if (pMLMEContext->pUrb == NULL) {
				DBGPRINT(RT_DEBUG_ERROR,
					 ("<-- ERROR in Alloc TX MLMEContext[%d] urb!\n",
					  i));
				Status = NDIS_STATUS_RESOURCES;
				goto out2;
			}
			pMLMEContext->pAd = pAd;
			pMLMEContext->pIrp = NULL;
			pMLMEContext->TransferBuffer = NULL;
			pMLMEContext->InUse = FALSE;
			pMLMEContext->IRPPending = FALSE;
			pMLMEContext->bWaitingBulkOut = FALSE;
			pMLMEContext->BulkOutSize = 0;
			pMLMEContext->SelfIdx = i;

			/* Offset to next ring descriptor address */
			RingBaseVa = (u8 *)RingBaseVa + sizeof(struct rt_tx_context);
		}
		DBGPRINT(RT_DEBUG_TRACE,
			 ("MGMT Ring: total %d entry allocated\n", i));

		/*pAd->MgmtRing.TxSwFreeIdx = (MGMT_RING_SIZE - 1); */
		pAd->MgmtRing.TxSwFreeIdx = MGMT_RING_SIZE;
		pAd->MgmtRing.TxCpuIdx = 0;
		pAd->MgmtRing.TxDmaIdx = 0;

		/* */
		/* BEACON_RING_SIZE */
		/* */
		for (i = 0; i < BEACON_RING_SIZE; i++)	/* 2 */
		{
			struct rt_tx_context *pBeaconContext = &(pAd->BeaconContext[i]);

			NdisZeroMemory(pBeaconContext, sizeof(struct rt_tx_context));

			/*Allocate URB */
			LM_USB_ALLOC(pObj, pBeaconContext, struct rt_tx_buffer *,
				     sizeof(struct rt_tx_buffer), Status,
				     ("<-- ERROR in Alloc TX BeaconContext[%d] urb!\n",
				      i), out2,
				     ("<-- ERROR in Alloc TX BeaconContext[%d] struct rt_tx_buffer!\n",
				      i), out3);

			pBeaconContext->pAd = pAd;
			pBeaconContext->pIrp = NULL;
			pBeaconContext->InUse = FALSE;
			pBeaconContext->IRPPending = FALSE;
		}

		/* */
		/* NullContext */
		/* */
		NdisZeroMemory(pNullContext, sizeof(struct rt_tx_context));

		/*Allocate URB */
		LM_USB_ALLOC(pObj, pNullContext, struct rt_tx_buffer *, sizeof(struct rt_tx_buffer),
			     Status,
			     ("<-- ERROR in Alloc TX NullContext urb!\n"),
			     out3,
			     ("<-- ERROR in Alloc TX NullContext struct rt_tx_buffer!\n"),
			     out4);

		pNullContext->pAd = pAd;
		pNullContext->pIrp = NULL;
		pNullContext->InUse = FALSE;
		pNullContext->IRPPending = FALSE;

		/* */
		/* RTSContext */
		/* */
		NdisZeroMemory(pRTSContext, sizeof(struct rt_tx_context));

		/*Allocate URB */
		LM_USB_ALLOC(pObj, pRTSContext, struct rt_tx_buffer *, sizeof(struct rt_tx_buffer),
			     Status,
			     ("<-- ERROR in Alloc TX RTSContext urb!\n"),
			     out4,
			     ("<-- ERROR in Alloc TX RTSContext struct rt_tx_buffer!\n"),
			     out5);

		pRTSContext->pAd = pAd;
		pRTSContext->pIrp = NULL;
		pRTSContext->InUse = FALSE;
		pRTSContext->IRPPending = FALSE;

		/* */
		/* PsPollContext */
		/* */
		/*NdisZeroMemory(pPsPollContext, sizeof(struct rt_tx_context)); */
		/*Allocate URB */
		LM_USB_ALLOC(pObj, pPsPollContext, struct rt_tx_buffer *,
			     sizeof(struct rt_tx_buffer), Status,
			     ("<-- ERROR in Alloc TX PsPollContext urb!\n"),
			     out5,
			     ("<-- ERROR in Alloc TX PsPollContext struct rt_tx_buffer!\n"),
			     out6);

		pPsPollContext->pAd = pAd;
		pPsPollContext->pIrp = NULL;
		pPsPollContext->InUse = FALSE;
		pPsPollContext->IRPPending = FALSE;
		pPsPollContext->bAggregatible = FALSE;
		pPsPollContext->LastOne = TRUE;

	} while (FALSE);

done:
	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICInitTransmit(Status=%d)\n", Status));

	return Status;

	/* --------------------------- ERROR HANDLE --------------------------- */
out6:
	LM_URB_FREE(pObj, pPsPollContext, sizeof(struct rt_tx_buffer));

out5:
	LM_URB_FREE(pObj, pRTSContext, sizeof(struct rt_tx_buffer));

out4:
	LM_URB_FREE(pObj, pNullContext, sizeof(struct rt_tx_buffer));

out3:
	for (i = 0; i < BEACON_RING_SIZE; i++) {
		struct rt_tx_context *pBeaconContext = &(pAd->BeaconContext[i]);
		if (pBeaconContext)
			LM_URB_FREE(pObj, pBeaconContext, sizeof(struct rt_tx_buffer));
	}

out2:
	if (pAd->MgmtDescRing.AllocVa) {
		pMgmtRing = &pAd->MgmtRing;
		for (i = 0; i < MGMT_RING_SIZE; i++) {
			pMLMEContext =
			    (struct rt_tx_context *)pAd->MgmtRing.Cell[i].AllocVa;
			if (pMLMEContext)
				LM_URB_FREE(pObj, pMLMEContext,
					    sizeof(struct rt_tx_buffer));
		}
		os_free_mem(pAd, pAd->MgmtDescRing.AllocVa);
		pAd->MgmtDescRing.AllocVa = NULL;
	}

out1:
	for (acidx = 0; acidx < 4; acidx++) {
		struct rt_ht_tx_context *pTxContext = &(pAd->TxContext[acidx]);
		if (pTxContext)
			LM_URB_FREE(pObj, pTxContext, sizeof(struct rt_httx_buffer));
	}

	/* Here we didn't have any pre-allocated memory need to free. */

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
int RTMPAllocTxRxRingMemory(struct rt_rtmp_adapter *pAd)
{
/*      struct rt_counter_802_11  pCounter = &pAd->WlanCounters; */
	int Status;
	int num;

	DBGPRINT(RT_DEBUG_TRACE, ("--> RTMPAllocTxRxRingMemory\n"));

	do {
		/* Init the struct rt_cmdq and CmdQLock */
		NdisAllocateSpinLock(&pAd->CmdQLock);
		NdisAcquireSpinLock(&pAd->CmdQLock);
		RTUSBInitializeCmdQ(&pAd->CmdQ);
		NdisReleaseSpinLock(&pAd->CmdQLock);

		NdisAllocateSpinLock(&pAd->MLMEBulkOutLock);
		/*NdisAllocateSpinLock(&pAd->MLMEWaitQueueLock); */
		NdisAllocateSpinLock(&pAd->BulkOutLock[0]);
		NdisAllocateSpinLock(&pAd->BulkOutLock[1]);
		NdisAllocateSpinLock(&pAd->BulkOutLock[2]);
		NdisAllocateSpinLock(&pAd->BulkOutLock[3]);
		NdisAllocateSpinLock(&pAd->BulkOutLock[4]);
		NdisAllocateSpinLock(&pAd->BulkOutLock[5]);
		NdisAllocateSpinLock(&pAd->BulkInLock);

		for (num = 0; num < NUM_OF_TX_RING; num++) {
			NdisAllocateSpinLock(&pAd->TxContextQueueLock[num]);
		}

/*              NdisAllocateSpinLock(&pAd->MemLock);    // Not used in RT28XX */

/*              NdisAllocateSpinLock(&pAd->MacTabLock); // init it in UserCfgInit() */
/*              NdisAllocateSpinLock(&pAd->BATabLock); // init it in BATableInit() */

/*              for(num=0; num<MAX_LEN_OF_BA_REC_TABLE; num++) */
/*              { */
/*                      NdisAllocateSpinLock(&pAd->BATable.BARecEntry[num].RxReRingLock); */
/*              } */

		/* */
		/* Init Mac Table */
		/* */
/*              MacTableInitialize(pAd); */

		/* */
		/* Init send data structures and related parameters */
		/* */
		Status = NICInitTransmit(pAd);
		if (Status != NDIS_STATUS_SUCCESS)
			break;

		/* */
		/* Init receive data structures and related parameters */
		/* */
		Status = NICInitRecv(pAd);
		if (Status != NDIS_STATUS_SUCCESS)
			break;

		pAd->PendingIoCount = 1;

	} while (FALSE);

	NdisZeroMemory(&pAd->FragFrame, sizeof(struct rt_fragment_frame));
	pAd->FragFrame.pFragPacket =
	    RTMP_AllocateFragPacketBuffer(pAd, RX_BUFFER_NORMSIZE);

	if (pAd->FragFrame.pFragPacket == NULL) {
		Status = NDIS_STATUS_RESOURCES;
	}

	DBGPRINT_S(Status,
		   ("<-- RTMPAllocTxRxRingMemory, Status=%x\n", Status));
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
void RTMPFreeTxRxRingMemory(struct rt_rtmp_adapter *pAd)
{
#define LM_URB_FREE(pObj, Context, BufferSize)				\
	if (NULL != Context->pUrb) {							\
		RTUSB_UNLINK_URB(Context->pUrb);					\
		RTUSB_FREE_URB(Context->pUrb);						\
		Context->pUrb = NULL; }								\
	if (NULL != Context->TransferBuffer) {					\
		RTUSB_URB_FREE_BUFFER(pObj->pUsb_Dev, BufferSize,	\
								Context->TransferBuffer,	\
								Context->data_dma);			\
		Context->TransferBuffer = NULL; }

	u32 i, acidx;
	struct rt_tx_context *pNullContext = &pAd->NullContext;
	struct rt_tx_context *pPsPollContext = &pAd->PsPollContext;
	struct rt_tx_context *pRTSContext = &pAd->RTSContext;
/*      struct rt_ht_tx_context *pHTTXContext; */
	/*PRTMP_REORDERBUF      pReorderBuf; */
	struct os_cookie *pObj = (struct os_cookie *)pAd->OS_Cookie;
/*      struct rt_rtmp_tx_ring *pTxRing; */

	DBGPRINT(RT_DEBUG_ERROR, ("---> RTMPFreeTxRxRingMemory\n"));
	pObj = pObj;

	/* Free all resources for the RECEIVE buffer queue. */
	for (i = 0; i < (RX_RING_SIZE); i++) {
		struct rt_rx_context *pRxContext = &(pAd->RxContext[i]);
		if (pRxContext)
			LM_URB_FREE(pObj, pRxContext, MAX_RXBULK_SIZE);
	}

	/* Free PsPoll frame resource */
	LM_URB_FREE(pObj, pPsPollContext, sizeof(struct rt_tx_buffer));

	/* Free NULL frame resource */
	LM_URB_FREE(pObj, pNullContext, sizeof(struct rt_tx_buffer));

	/* Free RTS frame resource */
	LM_URB_FREE(pObj, pRTSContext, sizeof(struct rt_tx_buffer));

	/* Free beacon frame resource */
	for (i = 0; i < BEACON_RING_SIZE; i++) {
		struct rt_tx_context *pBeaconContext = &(pAd->BeaconContext[i]);
		if (pBeaconContext)
			LM_URB_FREE(pObj, pBeaconContext, sizeof(struct rt_tx_buffer));
	}

	/* Free mgmt frame resource */
	for (i = 0; i < MGMT_RING_SIZE; i++) {
		struct rt_tx_context *pMLMEContext =
		    (struct rt_tx_context *)pAd->MgmtRing.Cell[i].AllocVa;
		/*LM_URB_FREE(pObj, pMLMEContext, sizeof(struct rt_tx_buffer)); */
		if (NULL != pAd->MgmtRing.Cell[i].pNdisPacket) {
			RTMPFreeNdisPacket(pAd,
					   pAd->MgmtRing.Cell[i].pNdisPacket);
			pAd->MgmtRing.Cell[i].pNdisPacket = NULL;
			pMLMEContext->TransferBuffer = NULL;
		}

		if (pMLMEContext) {
			if (NULL != pMLMEContext->pUrb) {
				RTUSB_UNLINK_URB(pMLMEContext->pUrb);
				RTUSB_FREE_URB(pMLMEContext->pUrb);
				pMLMEContext->pUrb = NULL;
			}
		}
	}
	if (pAd->MgmtDescRing.AllocVa)
		os_free_mem(pAd, pAd->MgmtDescRing.AllocVa);

	/* Free Tx frame resource */
	for (acidx = 0; acidx < 4; acidx++) {
		struct rt_ht_tx_context *pHTTXContext = &(pAd->TxContext[acidx]);
		if (pHTTXContext)
			LM_URB_FREE(pObj, pHTTXContext, sizeof(struct rt_httx_buffer));
	}

	if (pAd->FragFrame.pFragPacket)
		RELEASE_NDIS_PACKET(pAd, pAd->FragFrame.pFragPacket,
				    NDIS_STATUS_SUCCESS);

	for (i = 0; i < 6; i++) {
		NdisFreeSpinLock(&pAd->BulkOutLock[i]);
	}

	NdisFreeSpinLock(&pAd->BulkInLock);
	NdisFreeSpinLock(&pAd->MLMEBulkOutLock);

	NdisFreeSpinLock(&pAd->CmdQLock);
	/* Clear all pending bulk-out request flags. */
	RTUSB_CLEAR_BULK_FLAG(pAd, 0xffffffff);

/*      NdisFreeSpinLock(&pAd->MacTabLock); */

/*      for(i=0; i<MAX_LEN_OF_BA_REC_TABLE; i++) */
/*      { */
/*              NdisFreeSpinLock(&pAd->BATable.BARecEntry[i].RxReRingLock); */
/*      } */

	DBGPRINT(RT_DEBUG_ERROR, ("<--- RTMPFreeTxRxRingMemory\n"));
}

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
int RTUSBWriteHWMACAddress(struct rt_rtmp_adapter *pAd)
{
	MAC_DW0_STRUC StaMacReg0;
	MAC_DW1_STRUC StaMacReg1;
	int Status = NDIS_STATUS_SUCCESS;
	LARGE_INTEGER NOW;

	/* initialize the random number generator */
	RTMP_GetCurrentSystemTime(&NOW);

	if (pAd->bLocalAdminMAC != TRUE) {
		pAd->CurrentAddress[0] = pAd->PermanentAddress[0];
		pAd->CurrentAddress[1] = pAd->PermanentAddress[1];
		pAd->CurrentAddress[2] = pAd->PermanentAddress[2];
		pAd->CurrentAddress[3] = pAd->PermanentAddress[3];
		pAd->CurrentAddress[4] = pAd->PermanentAddress[4];
		pAd->CurrentAddress[5] = pAd->PermanentAddress[5];
	}
	/* Write New MAC address to MAC_CSR2 & MAC_CSR3 & let ASIC know our new MAC */
	StaMacReg0.field.Byte0 = pAd->CurrentAddress[0];
	StaMacReg0.field.Byte1 = pAd->CurrentAddress[1];
	StaMacReg0.field.Byte2 = pAd->CurrentAddress[2];
	StaMacReg0.field.Byte3 = pAd->CurrentAddress[3];
	StaMacReg1.field.Byte4 = pAd->CurrentAddress[4];
	StaMacReg1.field.Byte5 = pAd->CurrentAddress[5];
	StaMacReg1.field.U2MeMask = 0xff;
	DBGPRINT_RAW(RT_DEBUG_TRACE,
		     ("Local MAC = %02x:%02x:%02x:%02x:%02x:%02x\n",
		      pAd->CurrentAddress[0], pAd->CurrentAddress[1],
		      pAd->CurrentAddress[2], pAd->CurrentAddress[3],
		      pAd->CurrentAddress[4], pAd->CurrentAddress[5]));

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
void RT28XXDMADisable(struct rt_rtmp_adapter *pAd)
{
	/* no use */
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
	USB_DMA_CFG_STRUC UsbCfg;
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
	GloCfg.field.EnableRxDMA = 1;
	GloCfg.field.EnableTxDMA = 1;
	DBGPRINT(RT_DEBUG_TRACE,
		 ("<== WRITE DMA offset 0x208 = 0x%x\n", GloCfg.word));
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);

	UsbCfg.word = 0;
	UsbCfg.field.phyclear = 0;
	/* usb version is 1.1,do not use bulk in aggregation */
	if (pAd->BulkInMaxPacketSize == 512)
		UsbCfg.field.RxBulkAggEn = 1;
	/* for last packet, PBF might use more than limited, so minus 2 to prevent from error */
	UsbCfg.field.RxBulkAggLmt = (MAX_RXBULK_SIZE / 1024) - 3;
	UsbCfg.field.RxBulkAggTOut = 0x80;	/* 2006-10-18 */
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
void RT28xx_UpdateBeaconToAsic(struct rt_rtmp_adapter *pAd,
			       int apidx,
			       unsigned long FrameLen, unsigned long UpdatePos)
{
	u8 *pBeaconFrame = NULL;
	u8 *ptr;
	u32 i, padding;
	struct rt_beacon_sync *pBeaconSync = pAd->CommonCfg.pBeaconSync;
	u32 longValue;
/*      u16                  shortValue; */
	BOOLEAN bBcnReq = FALSE;
	u8 bcn_idx = 0;

	if (pBeaconFrame == NULL) {
		DBGPRINT(RT_DEBUG_ERROR, ("pBeaconFrame is NULL!\n"));
		return;
	}

	if (pBeaconSync == NULL) {
		DBGPRINT(RT_DEBUG_ERROR, ("pBeaconSync is NULL!\n"));
		return;
	}
	/*if ((pAd->WdsTab.Mode == WDS_BRIDGE_MODE) || */
	/*      ((pAd->ApCfg.MBSSID[apidx].MSSIDDev == NULL) || !(pAd->ApCfg.MBSSID[apidx].MSSIDDev->flags & IFF_UP)) */
	/*      ) */
	if (bBcnReq == FALSE) {
		/* when the ra interface is down, do not send its beacon frame */
		/* clear all zero */
		for (i = 0; i < TXWI_SIZE; i += 4) {
			RTMP_IO_WRITE32(pAd, pAd->BeaconOffset[bcn_idx] + i,
					0x00);
		}
		pBeaconSync->BeaconBitMap &=
		    (~(BEACON_BITMAP_MASK & (1 << bcn_idx)));
		NdisZeroMemory(pBeaconSync->BeaconTxWI[bcn_idx], TXWI_SIZE);
	} else {
		ptr = (u8 *)& pAd->BeaconTxWI;
		if (NdisEqualMemory(pBeaconSync->BeaconTxWI[bcn_idx], &pAd->BeaconTxWI, TXWI_SIZE) == FALSE) {	/* If BeaconTxWI changed, we need to rewrite the TxWI for the Beacon frames. */
			pBeaconSync->BeaconBitMap &=
			    (~(BEACON_BITMAP_MASK & (1 << bcn_idx)));
			NdisMoveMemory(pBeaconSync->BeaconTxWI[bcn_idx],
				       &pAd->BeaconTxWI, TXWI_SIZE);
		}

		if ((pBeaconSync->BeaconBitMap & (1 << bcn_idx)) !=
		    (1 << bcn_idx)) {
			for (i = 0; i < TXWI_SIZE; i += 4)	/* 16-byte TXWI field */
			{
				longValue =
				    *ptr + (*(ptr + 1) << 8) +
				    (*(ptr + 2) << 16) + (*(ptr + 3) << 24);
				RTMP_IO_WRITE32(pAd,
						pAd->BeaconOffset[bcn_idx] + i,
						longValue);
				ptr += 4;
			}
		}

		ptr = pBeaconSync->BeaconBuf[bcn_idx];
		padding = (FrameLen & 0x01);
		NdisZeroMemory((u8 *)(pBeaconFrame + FrameLen), padding);
		FrameLen += padding;
		for (i = 0; i < FrameLen /*HW_BEACON_OFFSET */ ; i += 2) {
			if (NdisEqualMemory(ptr, pBeaconFrame, 2) == FALSE) {
				NdisMoveMemory(ptr, pBeaconFrame, 2);
				/*shortValue = *ptr + (*(ptr+1)<<8); */
				/*RTMP_IO_WRITE8(pAd, pAd->BeaconOffset[bcn_idx] + TXWI_SIZE + i, shortValue); */
				RTUSBMultiWrite(pAd,
						pAd->BeaconOffset[bcn_idx] +
						TXWI_SIZE + i, ptr, 2);
			}
			ptr += 2;
			pBeaconFrame += 2;
		}

		pBeaconSync->BeaconBitMap |= (1 << bcn_idx);

		/* For AP interface, set the DtimBitOn so that we can send Bcast/Mcast frame out after this beacon frame. */
	}

}

void RTUSBBssBeaconStop(struct rt_rtmp_adapter *pAd)
{
	struct rt_beacon_sync *pBeaconSync;
	int i, offset;
	BOOLEAN Cancelled = TRUE;

	pBeaconSync = pAd->CommonCfg.pBeaconSync;
	if (pBeaconSync && pBeaconSync->EnableBeacon) {
		int NumOfBcn;

		{
			NumOfBcn = MAX_MESH_NUM;
		}

		RTMPCancelTimer(&pAd->CommonCfg.BeaconUpdateTimer, &Cancelled);

		for (i = 0; i < NumOfBcn; i++) {
			NdisZeroMemory(pBeaconSync->BeaconBuf[i],
				       HW_BEACON_OFFSET);
			NdisZeroMemory(pBeaconSync->BeaconTxWI[i], TXWI_SIZE);

			for (offset = 0; offset < HW_BEACON_OFFSET; offset += 4)
				RTMP_IO_WRITE32(pAd,
						pAd->BeaconOffset[i] + offset,
						0x00);

			pBeaconSync->CapabilityInfoLocationInBeacon[i] = 0;
			pBeaconSync->TimIELocationInBeacon[i] = 0;
		}
		pBeaconSync->BeaconBitMap = 0;
		pBeaconSync->DtimBitOn = 0;
	}
}

void RTUSBBssBeaconStart(struct rt_rtmp_adapter *pAd)
{
	int apidx;
	struct rt_beacon_sync *pBeaconSync;
/*      LARGE_INTEGER   tsfTime, deltaTime; */

	pBeaconSync = pAd->CommonCfg.pBeaconSync;
	if (pBeaconSync && pBeaconSync->EnableBeacon) {
		int NumOfBcn;

		{
			NumOfBcn = MAX_MESH_NUM;
		}

		for (apidx = 0; apidx < NumOfBcn; apidx++) {
			u8 CapabilityInfoLocationInBeacon = 0;
			u8 TimIELocationInBeacon = 0;

			NdisZeroMemory(pBeaconSync->BeaconBuf[apidx],
				       HW_BEACON_OFFSET);
			pBeaconSync->CapabilityInfoLocationInBeacon[apidx] =
			    CapabilityInfoLocationInBeacon;
			pBeaconSync->TimIELocationInBeacon[apidx] =
			    TimIELocationInBeacon;
			NdisZeroMemory(pBeaconSync->BeaconTxWI[apidx],
				       TXWI_SIZE);
		}
		pBeaconSync->BeaconBitMap = 0;
		pBeaconSync->DtimBitOn = 0;
		pAd->CommonCfg.BeaconUpdateTimer.Repeat = TRUE;

		pAd->CommonCfg.BeaconAdjust = 0;
		pAd->CommonCfg.BeaconFactor =
		    0xffffffff / (pAd->CommonCfg.BeaconPeriod << 10);
		pAd->CommonCfg.BeaconRemain =
		    (0xffffffff % (pAd->CommonCfg.BeaconPeriod << 10)) + 1;
		DBGPRINT(RT_DEBUG_TRACE,
			 ("RTUSBBssBeaconStart:BeaconFactor=%d, BeaconRemain=%d!\n",
			  pAd->CommonCfg.BeaconFactor,
			  pAd->CommonCfg.BeaconRemain));
		RTMPSetTimer(&pAd->CommonCfg.BeaconUpdateTimer,
			     10 /*pAd->CommonCfg.BeaconPeriod */ );

	}
}

void RTUSBBssBeaconInit(struct rt_rtmp_adapter *pAd)
{
	struct rt_beacon_sync *pBeaconSync;
	int i;

	os_alloc_mem(pAd, (u8 **) (&pAd->CommonCfg.pBeaconSync),
		     sizeof(struct rt_beacon_sync));
	/*NdisAllocMemory(pAd->CommonCfg.pBeaconSync, sizeof(struct rt_beacon_sync), MEM_ALLOC_FLAG); */
	if (pAd->CommonCfg.pBeaconSync) {
		pBeaconSync = pAd->CommonCfg.pBeaconSync;
		NdisZeroMemory(pBeaconSync, sizeof(struct rt_beacon_sync));
		for (i = 0; i < HW_BEACON_MAX_COUNT; i++) {
			NdisZeroMemory(pBeaconSync->BeaconBuf[i],
				       HW_BEACON_OFFSET);
			pBeaconSync->CapabilityInfoLocationInBeacon[i] = 0;
			pBeaconSync->TimIELocationInBeacon[i] = 0;
			NdisZeroMemory(pBeaconSync->BeaconTxWI[i], TXWI_SIZE);
		}
		pBeaconSync->BeaconBitMap = 0;

		/*RTMPInitTimer(pAd, &pAd->CommonCfg.BeaconUpdateTimer, GET_TIMER_FUNCTION(BeaconUpdateExec), pAd, TRUE); */
		pBeaconSync->EnableBeacon = TRUE;
	}
}

void RTUSBBssBeaconExit(struct rt_rtmp_adapter *pAd)
{
	struct rt_beacon_sync *pBeaconSync;
	BOOLEAN Cancelled = TRUE;
	int i;

	if (pAd->CommonCfg.pBeaconSync) {
		pBeaconSync = pAd->CommonCfg.pBeaconSync;
		pBeaconSync->EnableBeacon = FALSE;
		RTMPCancelTimer(&pAd->CommonCfg.BeaconUpdateTimer, &Cancelled);
		pBeaconSync->BeaconBitMap = 0;

		for (i = 0; i < HW_BEACON_MAX_COUNT; i++) {
			NdisZeroMemory(pBeaconSync->BeaconBuf[i],
				       HW_BEACON_OFFSET);
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
void BeaconUpdateExec(void *SystemSpecific1,
		      void *FunctionContext,
		      void *SystemSpecific2, void *SystemSpecific3)
{
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)FunctionContext;
	LARGE_INTEGER tsfTime_a;	/*, tsfTime_b, deltaTime_exp, deltaTime_ab; */
	u32 delta, delta2MS, period2US, remain, remain_low, remain_high;
/*      BOOLEAN                 positive; */

	if (pAd->CommonCfg.IsUpdateBeacon == TRUE) {
		ReSyncBeaconTime(pAd);

	}

	RTMP_IO_READ32(pAd, TSF_TIMER_DW0, &tsfTime_a.u.LowPart);
	RTMP_IO_READ32(pAd, TSF_TIMER_DW1, &tsfTime_a.u.HighPart);

	/*positive=getDeltaTime(tsfTime_a, expectedTime, &deltaTime_exp); */
	period2US = (pAd->CommonCfg.BeaconPeriod << 10);
	remain_high = pAd->CommonCfg.BeaconRemain * tsfTime_a.u.HighPart;
	remain_low = tsfTime_a.u.LowPart % (pAd->CommonCfg.BeaconPeriod << 10);
	remain =
	    (remain_high + remain_low) % (pAd->CommonCfg.BeaconPeriod << 10);
	delta = (pAd->CommonCfg.BeaconPeriod << 10) - remain;

	delta2MS = (delta >> 10);
	if (delta2MS > 150) {
		pAd->CommonCfg.BeaconUpdateTimer.TimerValue = 100;
		pAd->CommonCfg.IsUpdateBeacon = FALSE;
	} else {
		pAd->CommonCfg.BeaconUpdateTimer.TimerValue = delta2MS + 10;
		pAd->CommonCfg.IsUpdateBeacon = TRUE;
	}

}

/********************************************************************
  *
  *	2870 Radio on/off Related functions.
  *
  ********************************************************************/
void RT28xxUsbMlmeRadioOn(struct rt_rtmp_adapter *pAd)
{
	struct rt_rtmp_chip_op *pChipOps = &pAd->chipOps;

	DBGPRINT(RT_DEBUG_TRACE, ("RT28xxUsbMlmeRadioOn()\n"));

	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF))
		return;

	{
		AsicSendCommandToMcu(pAd, 0x31, 0xff, 0x00, 0x02);
		RTMPusecDelay(10000);
	}
	/*NICResetFromError(pAd); */

	/* Enable Tx/Rx */
	RTMPEnableRxTx(pAd);

	if (pChipOps->AsicReverseRfFromSleepMode)
		pChipOps->AsicReverseRfFromSleepMode(pAd);

	/* Clear Radio off flag */
	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

	RTUSBBulkReceive(pAd);

	/* Set LED */
	RTMPSetLED(pAd, LED_RADIO_ON);
}

void RT28xxUsbMlmeRadioOFF(struct rt_rtmp_adapter *pAd)
{
	WPDMA_GLO_CFG_STRUC GloCfg;
	u32 Value, i;

	DBGPRINT(RT_DEBUG_TRACE, ("RT28xxUsbMlmeRadioOFF()\n"));

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF))
		return;

	/* Clear PMKID cache. */
	pAd->StaCfg.SavedPMKNum = 0;
	RTMPZeroMemory(pAd->StaCfg.SavedPMK, (PMKID_NO * sizeof(struct rt_bssid_info)));

	/* Link down first if any association exists */
	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) {
		if (INFRA_ON(pAd) || ADHOC_ON(pAd)) {
			struct rt_mlme_disassoc_req DisReq;
			struct rt_mlme_queue_elem *pMsgElem =
			    (struct rt_mlme_queue_elem *)kmalloc(sizeof(struct rt_mlme_queue_elem),
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
	/* Set Radio off flag */
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

	{
		/* Link down first if any association exists */
		if (INFRA_ON(pAd) || ADHOC_ON(pAd))
			LinkDown(pAd, FALSE);
		RTMPusecDelay(10000);

		/*========================================== */
		/* Clean up old bss table */
		BssTableInit(&pAd->ScanTab);
	}

	/* Set LED */
	RTMPSetLED(pAd, LED_RADIO_OFF);

	if (pAd->CommonCfg.BBPCurrentBW == BW_40) {
		/* Must using 40MHz. */
		AsicTurnOffRFClk(pAd, pAd->CommonCfg.CentralChannel);
	} else {
		/* Must using 20MHz. */
		AsicTurnOffRFClk(pAd, pAd->CommonCfg.Channel);
	}

	/* Disable Tx/Rx DMA */
	RTUSBReadMACRegister(pAd, WPDMA_GLO_CFG, &GloCfg.word);	/* disable DMA */
	GloCfg.field.EnableTxDMA = 0;
	GloCfg.field.EnableRxDMA = 0;
	RTUSBWriteMACRegister(pAd, WPDMA_GLO_CFG, GloCfg.word);	/* abort all TX rings */

	/* Waiting for DMA idle */
	i = 0;
	do {
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);
		if ((GloCfg.field.TxDMABusy == 0)
		    && (GloCfg.field.RxDMABusy == 0))
			break;

		RTMPusecDelay(1000);
	} while (i++ < 100);

	/* Disable MAC Tx/Rx */
	RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
	Value &= (0xfffffff3);
	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);

	{
		AsicSendCommandToMcu(pAd, 0x30, 0xff, 0xff, 0x02);
	}
}

#endif /* RTMP_MAC_USB // */
