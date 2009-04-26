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
	2870_rtmp_init.c

	Abstract:
	Miniport generic portion header file

	Revision History:
	Who         When          What
	--------    ----------    ----------------------------------------------
	Paul Lin    2002-08-01    created
    John Chang  2004-08-20    RT2561/2661 use scatter-gather scheme
    Jan Lee  	2006-09-15    RT2860. Change for 802.11n , EEPROM, Led, BA, HT.
	Sample Lin	2007-05-31    Merge RT2860 and RT2870 drivers.
*/

#include	"../rt_config.h"


static void rx_done_tasklet(unsigned long data);
static void rt2870_hcca_dma_done_tasklet(unsigned long data);
static void rt2870_ac3_dma_done_tasklet(unsigned long data);
static void rt2870_ac2_dma_done_tasklet(unsigned long data);
static void rt2870_ac1_dma_done_tasklet(unsigned long data);
static void rt2870_ac0_dma_done_tasklet(unsigned long data);
static void rt2870_mgmt_dma_done_tasklet(unsigned long data);
static void rt2870_null_frame_complete_tasklet(unsigned long data);
static void rt2870_rts_frame_complete_tasklet(unsigned long data);
static void rt2870_pspoll_frame_complete_tasklet(unsigned long data);
static void rt2870_dataout_complete_tasklet(unsigned long data);


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

	//InterlockedExchange(&pAd->PendingRx, 0);
	pAd->PendingRx = 0;
	pAd->NextRxBulkInReadIndex 	= 0;	// Next Rx Read index
	pAd->NextRxBulkInIndex		= 0 ; //RX_RING_SIZE -1; // Rx Bulk pointer
	pAd->NextRxBulkInPosition 	= 0;

	for (i = 0; i < (RX_RING_SIZE); i++)
	{
		PRX_CONTEXT  pRxContext = &(pAd->RxContext[i]);

		//Allocate URB
		pRxContext->pUrb = RTUSB_ALLOC_URB(0);
		if (pRxContext->pUrb == NULL)
		{
			Status = NDIS_STATUS_RESOURCES;
			goto out1;
		}

		// Allocate transfer buffer
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
		//pRxContext->ReorderInUse = FALSE;
		pRxContext->bRxHandling = FALSE;
		pRxContext->BulkInOffset = 0;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICInitRecv\n"));
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
#define LM_USB_ALLOC(pObj, Context, TB_Type, BufferSize, Status, msg1, err1, msg2, err2)	\
	Context->pUrb = RTUSB_ALLOC_URB(0);		\
	if (Context->pUrb == NULL) {			\
		DBGPRINT(RT_DEBUG_ERROR, msg1);		\
		Status = NDIS_STATUS_RESOURCES;		\
		goto err1; }						\
											\
	Context->TransferBuffer = 				\
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

	UCHAR			i, acidx;
	NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;
	PTX_CONTEXT		pNullContext   = &(pAd->NullContext);
	PTX_CONTEXT		pPsPollContext = &(pAd->PsPollContext);
	PTX_CONTEXT		pRTSContext    = &(pAd->RTSContext);
	PTX_CONTEXT		pMLMEContext = NULL;
//	PHT_TX_CONTEXT	pHTTXContext = NULL;
	POS_COOKIE		pObj = (POS_COOKIE) pAd->OS_Cookie;
	PVOID			RingBaseVa;
//	RTMP_TX_RING	*pTxRing;
	RTMP_MGMT_RING  *pMgmtRing;

	DBGPRINT(RT_DEBUG_TRACE, ("--> NICInitTransmit\n"));
	pObj = pObj;

	// Init 4 set of Tx parameters
	for(acidx = 0; acidx < NUM_OF_TX_RING; acidx++)
	{
		// Initialize all Transmit releated queues
		InitializeQueueHeader(&pAd->TxSwQueue[acidx]);

		// Next Local tx ring pointer waiting for buck out
		pAd->NextBulkOutIndex[acidx] = acidx;
		pAd->BulkOutPending[acidx] = FALSE; // Buck Out control flag
		//pAd->DataBulkDoneIdx[acidx] = 0;
	}

	//pAd->NextMLMEIndex	= 0;
	//pAd->PushMgmtIndex	= 0;
	//pAd->PopMgmtIndex	= 0;
	//InterlockedExchange(&pAd->MgmtQueueSize, 0);
	//InterlockedExchange(&pAd->TxCount, 0);

	//pAd->PrioRingFirstIndex	= 0;
	//pAd->PrioRingTxCnt		= 0;

	do
	{
		//
		// TX_RING_SIZE, 4 ACs
		//
#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		for(acidx=0; acidx<4; acidx++)
#endif // CONFIG_STA_SUPPORT //
		{
#if 1 //def DOT11_N_SUPPORT
			PHT_TX_CONTEXT	pHTTXContext = &(pAd->TxContext[acidx]);

			NdisZeroMemory(pHTTXContext, sizeof(HT_TX_CONTEXT));
			//Allocate URB
			LM_USB_ALLOC(pObj, pHTTXContext, PHTTX_BUFFER, sizeof(HTTX_BUFFER), Status,
							("<-- ERROR in Alloc TX TxContext[%d] urb!! \n", acidx),
							done,
							("<-- ERROR in Alloc TX TxContext[%d] HTTX_BUFFER !! \n", acidx),
							out1);

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
#endif // DOT11_N_SUPPORT //
			pAd->BulkOutPending[acidx] = FALSE;
		}


		//
		// MGMT_RING_SIZE
		//
		// Allocate MGMT ring descriptor's memory
		pAd->MgmtDescRing.AllocSize = MGMT_RING_SIZE * sizeof(TX_CONTEXT);
		RTMPAllocateMemory(&pAd->MgmtDescRing.AllocVa, pAd->MgmtDescRing.AllocSize);
		if (pAd->MgmtDescRing.AllocVa == NULL)
		{
			DBGPRINT_ERR(("Failed to allocate a big buffer for MgmtDescRing!\n"));
			Status = NDIS_STATUS_RESOURCES;
			goto out1;
		}
		NdisZeroMemory(pAd->MgmtDescRing.AllocVa, pAd->MgmtDescRing.AllocSize);
		RingBaseVa     = pAd->MgmtDescRing.AllocVa;

		// Initialize MGMT Ring and associated buffer memory
		pMgmtRing = &pAd->MgmtRing;
		for (i = 0; i < MGMT_RING_SIZE; i++)
		{
			// link the pre-allocated Mgmt buffer to MgmtRing.Cell
			pMgmtRing->Cell[i].AllocSize = sizeof(TX_CONTEXT);
			pMgmtRing->Cell[i].AllocVa = RingBaseVa;
			pMgmtRing->Cell[i].pNdisPacket = NULL;
			pMgmtRing->Cell[i].pNextNdisPacket = NULL;

			//Allocate URB for MLMEContext
			pMLMEContext = (PTX_CONTEXT) pAd->MgmtRing.Cell[i].AllocVa;
			pMLMEContext->pUrb = RTUSB_ALLOC_URB(0);
			if (pMLMEContext->pUrb == NULL)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("<-- ERROR in Alloc TX MLMEContext[%d] urb!! \n", i));
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

			// Offset to next ring descriptor address
			RingBaseVa = (PUCHAR) RingBaseVa + sizeof(TX_CONTEXT);
		}
		DBGPRINT(RT_DEBUG_TRACE, ("MGMT Ring: total %d entry allocated\n", i));

		//pAd->MgmtRing.TxSwFreeIdx = (MGMT_RING_SIZE - 1);
		pAd->MgmtRing.TxSwFreeIdx = MGMT_RING_SIZE;
		pAd->MgmtRing.TxCpuIdx = 0;
		pAd->MgmtRing.TxDmaIdx = 0;

		//
		// BEACON_RING_SIZE
		//
		for(i=0; i<BEACON_RING_SIZE; i++) // 2
		{
			PTX_CONTEXT	pBeaconContext = &(pAd->BeaconContext[i]);


			NdisZeroMemory(pBeaconContext, sizeof(TX_CONTEXT));

			//Allocate URB
			LM_USB_ALLOC(pObj, pBeaconContext, PTX_BUFFER, sizeof(TX_BUFFER), Status,
							("<-- ERROR in Alloc TX BeaconContext[%d] urb!! \n", i),
							out2,
							("<-- ERROR in Alloc TX BeaconContext[%d] TX_BUFFER !! \n", i),
							out3);

			pBeaconContext->pAd = pAd;
			pBeaconContext->pIrp = NULL;
			pBeaconContext->InUse = FALSE;
			pBeaconContext->IRPPending = FALSE;
		}

		//
		// NullContext
		//
		NdisZeroMemory(pNullContext, sizeof(TX_CONTEXT));

		//Allocate URB
		LM_USB_ALLOC(pObj, pNullContext, PTX_BUFFER, sizeof(TX_BUFFER), Status,
						("<-- ERROR in Alloc TX NullContext urb!! \n"),
						out3,
						("<-- ERROR in Alloc TX NullContext TX_BUFFER !! \n"),
						out4);

		pNullContext->pAd = pAd;
		pNullContext->pIrp = NULL;
		pNullContext->InUse = FALSE;
		pNullContext->IRPPending = FALSE;

		//
		// RTSContext
		//
		NdisZeroMemory(pRTSContext, sizeof(TX_CONTEXT));

		//Allocate URB
		LM_USB_ALLOC(pObj, pRTSContext, PTX_BUFFER, sizeof(TX_BUFFER), Status,
						("<-- ERROR in Alloc TX RTSContext urb!! \n"),
						out4,
						("<-- ERROR in Alloc TX RTSContext TX_BUFFER !! \n"),
						out5);

		pRTSContext->pAd = pAd;
		pRTSContext->pIrp = NULL;
		pRTSContext->InUse = FALSE;
		pRTSContext->IRPPending = FALSE;

		//
		// PsPollContext
		//
		//NdisZeroMemory(pPsPollContext, sizeof(TX_CONTEXT));
		//Allocate URB
		LM_USB_ALLOC(pObj, pPsPollContext, PTX_BUFFER, sizeof(TX_BUFFER), Status,
						("<-- ERROR in Alloc TX PsPollContext urb!! \n"),
						out5,
						("<-- ERROR in Alloc TX PsPollContext TX_BUFFER !! \n"),
						out6);

		pPsPollContext->pAd = pAd;
		pPsPollContext->pIrp = NULL;
		pPsPollContext->InUse = FALSE;
		pPsPollContext->IRPPending = FALSE;
		pPsPollContext->bAggregatible = FALSE;
		pPsPollContext->LastOne = TRUE;

	}   while (FALSE);


done:
	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICInitTransmit\n"));

	return Status;

	/* --------------------------- ERROR HANDLE --------------------------- */
out6:
	LM_URB_FREE(pObj, pPsPollContext, sizeof(TX_BUFFER));

out5:
	LM_URB_FREE(pObj, pRTSContext, sizeof(TX_BUFFER));

out4:
	LM_URB_FREE(pObj, pNullContext, sizeof(TX_BUFFER));

out3:
	for(i=0; i<BEACON_RING_SIZE; i++)
	{
		PTX_CONTEXT	pBeaconContext = &(pAd->BeaconContext[i]);
		if (pBeaconContext)
			LM_URB_FREE(pObj, pBeaconContext, sizeof(TX_BUFFER));
	}

out2:
	if (pAd->MgmtDescRing.AllocVa)
	{
		pMgmtRing = &pAd->MgmtRing;
	for(i=0; i<MGMT_RING_SIZE; i++)
	{
		pMLMEContext = (PTX_CONTEXT) pAd->MgmtRing.Cell[i].AllocVa;
		if (pMLMEContext)
			LM_URB_FREE(pObj, pMLMEContext, sizeof(TX_BUFFER));
	}
		NdisFreeMemory(pAd->MgmtDescRing.AllocVa, pAd->MgmtDescRing.AllocSize, 0);
		pAd->MgmtDescRing.AllocVa = NULL;
	}

out1:
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	for(acidx=0; acidx<4; acidx++)
#endif // CONFIG_STA_SUPPORT //
	{
		PHT_TX_CONTEXT pTxContext = &(pAd->TxContext[acidx]);
		if (pTxContext)
			LM_URB_FREE(pObj, pTxContext, sizeof(HTTX_BUFFER));
	}

	// Here we didn't have any pre-allocated memory need to free.

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
//	COUNTER_802_11	pCounter = &pAd->WlanCounters;
	NDIS_STATUS		Status;
	INT				num;


	DBGPRINT(RT_DEBUG_TRACE, ("--> RTMPAllocTxRxRingMemory\n"));


	do
	{
		// Init the CmdQ and CmdQLock
		NdisAllocateSpinLock(&pAd->CmdQLock);
		NdisAcquireSpinLock(&pAd->CmdQLock);
		RTUSBInitializeCmdQ(&pAd->CmdQ);
		NdisReleaseSpinLock(&pAd->CmdQLock);


		NdisAllocateSpinLock(&pAd->MLMEBulkOutLock);
		//NdisAllocateSpinLock(&pAd->MLMEWaitQueueLock);
		NdisAllocateSpinLock(&pAd->BulkOutLock[0]);
		NdisAllocateSpinLock(&pAd->BulkOutLock[1]);
		NdisAllocateSpinLock(&pAd->BulkOutLock[2]);
		NdisAllocateSpinLock(&pAd->BulkOutLock[3]);
		NdisAllocateSpinLock(&pAd->BulkOutLock[4]);
		NdisAllocateSpinLock(&pAd->BulkOutLock[5]);
		NdisAllocateSpinLock(&pAd->BulkInLock);

		for (num = 0; num < NUM_OF_TX_RING; num++)
		{
			NdisAllocateSpinLock(&pAd->TxContextQueueLock[num]);
		}

//		NdisAllocateSpinLock(&pAd->MemLock);	// Not used in RT28XX

//		NdisAllocateSpinLock(&pAd->MacTabLock); // init it in UserCfgInit()
//		NdisAllocateSpinLock(&pAd->BATabLock); // init it in BATableInit()

//		for(num=0; num<MAX_LEN_OF_BA_REC_TABLE; num++)
//		{
//			NdisAllocateSpinLock(&pAd->BATable.BARecEntry[num].RxReRingLock);
//		}

		//
		// Init Mac Table
		//
//		MacTableInitialize(pAd);

		//
		// Init send data structures and related parameters
		//
		Status = NICInitTransmit(pAd);
		if (Status != NDIS_STATUS_SUCCESS)
			break;

		//
		// Init receive data structures and related parameters
		//
		Status = NICInitRecv(pAd);
		if (Status != NDIS_STATUS_SUCCESS)
			break;

		pAd->PendingIoCount = 1;

	} while (FALSE);

	NdisZeroMemory(&pAd->FragFrame, sizeof(FRAGMENT_FRAME));
	pAd->FragFrame.pFragPacket =  RTMP_AllocateFragPacketBuffer(pAd, RX_BUFFER_NORMSIZE);

	if (pAd->FragFrame.pFragPacket == NULL)
	{
		Status = NDIS_STATUS_RESOURCES;
	}

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


	UINT                i, acidx;
	PTX_CONTEXT			pNullContext   = &pAd->NullContext;
	PTX_CONTEXT			pPsPollContext = &pAd->PsPollContext;
	PTX_CONTEXT			pRTSContext    = &pAd->RTSContext;
//	PHT_TX_CONTEXT 		pHTTXContext;
	//PRTMP_REORDERBUF	pReorderBuf;
	POS_COOKIE			pObj = (POS_COOKIE) pAd->OS_Cookie;
//	RTMP_TX_RING		*pTxRing;

	DBGPRINT(RT_DEBUG_ERROR, ("---> RTMPFreeTxRxRingMemory\n"));
	pObj = pObj;

	// Free all resources for the RECEIVE buffer queue.
	for(i=0; i<(RX_RING_SIZE); i++)
	{
		PRX_CONTEXT  pRxContext = &(pAd->RxContext[i]);
		if (pRxContext)
			LM_URB_FREE(pObj, pRxContext, MAX_RXBULK_SIZE);
	}

	// Free PsPoll frame resource
	LM_URB_FREE(pObj, pPsPollContext, sizeof(TX_BUFFER));

	// Free NULL frame resource
	LM_URB_FREE(pObj, pNullContext, sizeof(TX_BUFFER));

	// Free RTS frame resource
	LM_URB_FREE(pObj, pRTSContext, sizeof(TX_BUFFER));


	// Free beacon frame resource
	for(i=0; i<BEACON_RING_SIZE; i++)
	{
		PTX_CONTEXT	pBeaconContext = &(pAd->BeaconContext[i]);
		if (pBeaconContext)
			LM_URB_FREE(pObj, pBeaconContext, sizeof(TX_BUFFER));
	}


	// Free mgmt frame resource
	for(i = 0; i < MGMT_RING_SIZE; i++)
	{
		PTX_CONTEXT pMLMEContext = (PTX_CONTEXT)pAd->MgmtRing.Cell[i].AllocVa;
		//LM_URB_FREE(pObj, pMLMEContext, sizeof(TX_BUFFER));
		if (NULL != pAd->MgmtRing.Cell[i].pNdisPacket)
		{
			RTMPFreeNdisPacket(pAd, pAd->MgmtRing.Cell[i].pNdisPacket);
			pAd->MgmtRing.Cell[i].pNdisPacket = NULL;
			pMLMEContext->TransferBuffer = NULL;
		}

		if (pMLMEContext)
		{
			if (NULL != pMLMEContext->pUrb)
			{
				RTUSB_UNLINK_URB(pMLMEContext->pUrb);
				RTUSB_FREE_URB(pMLMEContext->pUrb);
				pMLMEContext->pUrb = NULL;
			}
		}
	}
	if (pAd->MgmtDescRing.AllocVa)
		NdisFreeMemory(pAd->MgmtDescRing.AllocVa, pAd->MgmtDescRing.AllocSize, 0);


	// Free Tx frame resource
#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		for(acidx=0; acidx<4; acidx++)
#endif // CONFIG_STA_SUPPORT //
		{
		PHT_TX_CONTEXT pHTTXContext = &(pAd->TxContext[acidx]);
			if (pHTTXContext)
				LM_URB_FREE(pObj, pHTTXContext, sizeof(HTTX_BUFFER));
		}

	if (pAd->FragFrame.pFragPacket)
		RELEASE_NDIS_PACKET(pAd, pAd->FragFrame.pFragPacket, NDIS_STATUS_SUCCESS);

	for(i=0; i<6; i++)
	{
		NdisFreeSpinLock(&pAd->BulkOutLock[i]);
	}

	NdisFreeSpinLock(&pAd->BulkInLock);
	NdisFreeSpinLock(&pAd->MLMEBulkOutLock);

	NdisFreeSpinLock(&pAd->CmdQLock);

	// Clear all pending bulk-out request flags.
	RTUSB_CLEAR_BULK_FLAG(pAd, 0xffffffff);

//	NdisFreeSpinLock(&pAd->MacTabLock);

//	for(i=0; i<MAX_LEN_OF_BA_REC_TABLE; i++)
//	{
//		NdisFreeSpinLock(&pAd->BATable.BARecEntry[i].RxReRingLock);
//	}

	DBGPRINT(RT_DEBUG_ERROR, ("<--- ReleaseAdapter\n"));
}


/*
========================================================================
Routine Description:
    Allocate memory for adapter control block.

Arguments:
    pAd					Pointer to our adapter

Return Value:
	NDIS_STATUS_SUCCESS
	NDIS_STATUS_FAILURE
	NDIS_STATUS_RESOURCES

Note:
========================================================================
*/
NDIS_STATUS AdapterBlockAllocateMemory(
	IN PVOID	handle,
	OUT	PVOID	*ppAd)
{
	PUSB_DEV	usb_dev;
	POS_COOKIE	pObj = (POS_COOKIE) handle;


	usb_dev = pObj->pUsb_Dev;

	pObj->MLMEThr_pid	= NULL;
	pObj->RTUSBCmdThr_pid	= NULL;

	*ppAd = (PVOID)vmalloc(sizeof(RTMP_ADAPTER));

	if (*ppAd)
	{
		NdisZeroMemory(*ppAd, sizeof(RTMP_ADAPTER));
		((PRTMP_ADAPTER)*ppAd)->OS_Cookie = handle;
		return (NDIS_STATUS_SUCCESS);
	}
	else
	{
		return (NDIS_STATUS_FAILURE);
	}
}


/*
========================================================================
Routine Description:
    Create kernel threads & tasklets.

Arguments:
    *net_dev			Pointer to wireless net device interface

Return Value:
	NDIS_STATUS_SUCCESS
	NDIS_STATUS_FAILURE

Note:
========================================================================
*/
NDIS_STATUS	 CreateThreads(
	IN	struct net_device *net_dev)
{
	PRTMP_ADAPTER pAd = net_dev->ml_priv;
	POS_COOKIE pObj = (POS_COOKIE) pAd->OS_Cookie;
	pid_t pid_number;

	//init_MUTEX(&(pAd->usbdev_semaphore));

	init_MUTEX_LOCKED(&(pAd->mlme_semaphore));
	init_completion (&pAd->mlmeComplete);

	init_MUTEX_LOCKED(&(pAd->RTUSBCmd_semaphore));
	init_completion (&pAd->CmdQComplete);

	init_MUTEX_LOCKED(&(pAd->RTUSBTimer_semaphore));
	init_completion (&pAd->TimerQComplete);

	// Creat MLME Thread
	pObj->MLMEThr_pid = NULL;
	pid_number = kernel_thread(MlmeThread, pAd, CLONE_VM);
	if (pid_number < 0)
	{
		printk (KERN_WARNING "%s: unable to start Mlme thread\n",pAd->net_dev->name);
		return NDIS_STATUS_FAILURE;
	}
	pObj->MLMEThr_pid = find_get_pid(pid_number);
	// Wait for the thread to start
	wait_for_completion(&(pAd->mlmeComplete));

	// Creat Command Thread
	pObj->RTUSBCmdThr_pid = NULL;
	pid_number = kernel_thread(RTUSBCmdThread, pAd, CLONE_VM);
	if (pid_number < 0)
	{
		printk (KERN_WARNING "%s: unable to start RTUSBCmd thread\n",pAd->net_dev->name);
		return NDIS_STATUS_FAILURE;
	}
	pObj->RTUSBCmdThr_pid = find_get_pid(pid_number);
	wait_for_completion(&(pAd->CmdQComplete));

	pObj->TimerQThr_pid = NULL;
	pid_number = kernel_thread(TimerQThread, pAd, CLONE_VM);
	if (pid_number < 0)
	{
		printk (KERN_WARNING "%s: unable to start TimerQThread\n",pAd->net_dev->name);
		return NDIS_STATUS_FAILURE;
	}
	pObj->TimerQThr_pid = find_get_pid(pid_number);
	// Wait for the thread to start
	wait_for_completion(&(pAd->TimerQComplete));

	// Create receive tasklet
	tasklet_init(&pObj->rx_done_task, rx_done_tasklet, (ULONG)pAd);
	tasklet_init(&pObj->mgmt_dma_done_task, rt2870_mgmt_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->ac0_dma_done_task, rt2870_ac0_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->ac1_dma_done_task, rt2870_ac1_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->ac2_dma_done_task, rt2870_ac2_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->ac3_dma_done_task, rt2870_ac3_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->hcca_dma_done_task, rt2870_hcca_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->tbtt_task, tbtt_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->null_frame_complete_task, rt2870_null_frame_complete_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->rts_frame_complete_task, rt2870_rts_frame_complete_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->pspoll_frame_complete_task, rt2870_pspoll_frame_complete_tasklet, (unsigned long)pAd);

	return NDIS_STATUS_SUCCESS;
}


#ifdef CONFIG_STA_SUPPORT
/*
========================================================================
Routine Description:
 	As STA's BSSID is a WC too, it uses shared key table.
 	This function write correct unicast TX key to ASIC WCID.
 	And we still make a copy in our MacTab.Content[BSSID_WCID].PairwiseKey.
	Caller guarantee TKIP/AES always has keyidx = 0. (pairwise key)
	Caller guarantee WEP calls this function when set Txkey,  default key index=0~3.

Arguments:
	pAd 					Pointer to our adapter
	pKey					Pointer to the where the key stored

Return Value:
	NDIS_SUCCESS			Add key successfully

Note:
========================================================================
*/
VOID	RTMPAddBSSIDCipher(
	IN	PRTMP_ADAPTER		pAd,
	IN	UCHAR				Aid,
	IN	PNDIS_802_11_KEY	pKey,
	IN  UCHAR   			CipherAlg)
{
	PUCHAR		pTxMic, pRxMic;
	BOOLEAN 	bKeyRSC, bAuthenticator; // indicate the receive SC set by KeyRSC value
//	UCHAR		CipherAlg;
	UCHAR		i;
	ULONG		WCIDAttri;
	USHORT	 	offset;
	UCHAR		KeyIdx, IVEIV[8];
	UINT32		Value;

	DBGPRINT(RT_DEBUG_TRACE, ("RTMPAddBSSIDCipher==> Aid = %d\n",Aid));

	// Bit 29 of Add-key KeyRSC
	bKeyRSC 	   = (pKey->KeyIndex & 0x20000000) ? TRUE : FALSE;

	// Bit 28 of Add-key Authenticator
	bAuthenticator = (pKey->KeyIndex & 0x10000000) ? TRUE : FALSE;
	KeyIdx = (UCHAR)pKey->KeyIndex&0xff;

	if (KeyIdx > 4)
		return;


	if (pAd->MacTab.Content[Aid].PairwiseKey.CipherAlg == CIPHER_TKIP)
	{	if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPANone)
		{
			// for WPA-None Tx, Rx MIC is the same
			pTxMic = (PUCHAR) (&pKey->KeyMaterial) + 16;
			pRxMic = pTxMic;
		}
		else if (bAuthenticator == TRUE)
		{
			pTxMic = (PUCHAR) (&pKey->KeyMaterial) + 16;
			pRxMic = (PUCHAR) (&pKey->KeyMaterial) + 24;
		}
		else
		{
			pRxMic = (PUCHAR) (&pKey->KeyMaterial) + 16;
			pTxMic = (PUCHAR) (&pKey->KeyMaterial) + 24;
		}

		offset = PAIRWISE_KEY_TABLE_BASE + (Aid * HW_KEY_ENTRY_SIZE) + 0x10;
		for (i=0; i<8; )
		{
			Value = *(pTxMic+i);
			Value += (*(pTxMic+i+1)<<8);
			Value += (*(pTxMic+i+2)<<16);
			Value += (*(pTxMic+i+3)<<24);
			RTUSBWriteMACRegister(pAd, offset+i, Value);
			i+=4;
		}

		offset = PAIRWISE_KEY_TABLE_BASE + (Aid * HW_KEY_ENTRY_SIZE) + 0x18;
		for (i=0; i<8; )
		{
			Value = *(pRxMic+i);
			Value += (*(pRxMic+i+1)<<8);
			Value += (*(pRxMic+i+2)<<16);
			Value += (*(pRxMic+i+3)<<24);
			RTUSBWriteMACRegister(pAd, offset+i, Value);
			i+=4;
		}

		// Only Key lenth equal to TKIP key have these
		NdisMoveMemory(pAd->MacTab.Content[Aid].PairwiseKey.RxMic, pRxMic, 8);
		NdisMoveMemory(pAd->MacTab.Content[Aid].PairwiseKey.TxMic, pTxMic, 8);

		DBGPRINT(RT_DEBUG_TRACE,
				("	TxMIC  = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x \n",
				pTxMic[0],pTxMic[1],pTxMic[2],pTxMic[3],
				pTxMic[4],pTxMic[5],pTxMic[6],pTxMic[7]));
		DBGPRINT(RT_DEBUG_TRACE,
				("	RxMIC  = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x \n",
				pRxMic[0],pRxMic[1],pRxMic[2],pRxMic[3],
				pRxMic[4],pRxMic[5],pRxMic[6],pRxMic[7]));
	}

	// 2. Record Security Key.
	pAd->MacTab.Content[BSSID_WCID].PairwiseKey.KeyLen= (UCHAR)pKey->KeyLength;
	NdisMoveMemory(pAd->MacTab.Content[Aid].PairwiseKey.Key, &pKey->KeyMaterial, pKey->KeyLength);

	// 3. Check RxTsc. And used to init to ASIC IV.
	if (bKeyRSC == TRUE)
		NdisMoveMemory(pAd->MacTab.Content[Aid].PairwiseKey.RxTsc, &pKey->KeyRSC, 6);
	else
		NdisZeroMemory(pAd->MacTab.Content[Aid].PairwiseKey.RxTsc, 6);

	// 4. Init TxTsc to one based on WiFi WPA specs
	pAd->MacTab.Content[Aid].PairwiseKey.TxTsc[0] = 1;
	pAd->MacTab.Content[Aid].PairwiseKey.TxTsc[1] = 0;
	pAd->MacTab.Content[Aid].PairwiseKey.TxTsc[2] = 0;
	pAd->MacTab.Content[Aid].PairwiseKey.TxTsc[3] = 0;
	pAd->MacTab.Content[Aid].PairwiseKey.TxTsc[4] = 0;
	pAd->MacTab.Content[Aid].PairwiseKey.TxTsc[5] = 0;

	CipherAlg = pAd->MacTab.Content[Aid].PairwiseKey.CipherAlg;

	offset = PAIRWISE_KEY_TABLE_BASE + (Aid * HW_KEY_ENTRY_SIZE);
	RTUSBMultiWrite(pAd, (USHORT) offset, pKey->KeyMaterial,
				((pKey->KeyLength == LEN_TKIP_KEY) ? 16 : (USHORT)pKey->KeyLength));

	offset = SHARED_KEY_TABLE_BASE + (KeyIdx * HW_KEY_ENTRY_SIZE);
	RTUSBMultiWrite(pAd, (USHORT) offset, pKey->KeyMaterial, (USHORT)pKey->KeyLength);

	offset = PAIRWISE_IVEIV_TABLE_BASE + (Aid * HW_IVEIV_ENTRY_SIZE);
	NdisZeroMemory(IVEIV, 8);

	// IV/EIV
	if ((CipherAlg == CIPHER_TKIP) ||
		(CipherAlg == CIPHER_TKIP_NO_MIC) ||
		(CipherAlg == CIPHER_AES))
	{
		IVEIV[3] = 0x20; // Eiv bit on. keyid always 0 for pairwise key
	}
	// default key idx needs to set.
	// in TKIP/AES KeyIdx = 0 , WEP KeyIdx is default tx key.
	else
	{
		IVEIV[3] |= (KeyIdx<< 6);
	}
	RTUSBMultiWrite(pAd, (USHORT) offset, IVEIV, 8);

	// WCID Attribute UDF:3, BSSIdx:3, Alg:3, Keytable:1=PAIRWISE KEY, BSSIdx is 0
	if ((CipherAlg == CIPHER_TKIP) ||
		(CipherAlg == CIPHER_TKIP_NO_MIC) ||
		(CipherAlg == CIPHER_AES))
	{
		WCIDAttri = (CipherAlg<<1)|SHAREDKEYTABLE;
	}
	else
		WCIDAttri = (CipherAlg<<1)|SHAREDKEYTABLE;

	offset = MAC_WCID_ATTRIBUTE_BASE + (Aid* HW_WCID_ATTRI_SIZE);
	RTUSBWriteMACRegister(pAd, offset, WCIDAttri);
	RTUSBReadMACRegister(pAd, offset, &Value);

	DBGPRINT(RT_DEBUG_TRACE, ("BSSID_WCID : offset = %x, WCIDAttri = %lx\n",
			offset, WCIDAttri));

	// pAddr
	// Add Bssid mac address at linkup. not here.  check!
	/*offset = MAC_WCID_BASE + (BSSID_WCID * HW_WCID_ENTRY_SIZE);
	*for (i=0; i<MAC_ADDR_LEN; i++)
	{
		RTMP_IO_WRITE8(pAd, offset+i, pKey->BSSID[i]);
	}
	*/

	DBGPRINT(RT_DEBUG_ERROR, ("AddBSSIDasWCIDEntry: Alg=%s, KeyLength = %d\n",
			CipherName[CipherAlg], pKey->KeyLength));
	DBGPRINT(RT_DEBUG_TRACE, ("Key [idx=%x] [KeyLen = %d]\n",
			pKey->KeyIndex, pKey->KeyLength));
	for(i=0; i<pKey->KeyLength; i++)
		DBGPRINT_RAW(RT_DEBUG_TRACE,(" %x:", pKey->KeyMaterial[i]));
	DBGPRINT(RT_DEBUG_TRACE,("	 \n"));
}
#endif // CONFIG_STA_SUPPORT //

/*
========================================================================
Routine Description:
    Get a received packet.

Arguments:
	pAd					device control block
	pSaveRxD			receive descriptor information
	*pbReschedule		need reschedule flag
	*pRxPending			pending received packet flag

Return Value:
    the recieved packet

Note:
========================================================================
*/
#define RT2870_RXDMALEN_FIELD_SIZE			4
PNDIS_PACKET GetPacketFromRxRing(
	IN		PRTMP_ADAPTER		pAd,
	OUT		PRT28XX_RXD_STRUC	pSaveRxD,
	OUT		BOOLEAN				*pbReschedule,
	IN OUT	UINT32				*pRxPending)
{
	PRX_CONTEXT		pRxContext;
	PNDIS_PACKET	pSkb;
	PUCHAR			pData;
	ULONG			ThisFrameLen;
	ULONG			RxBufferLength;
	PRXWI_STRUC		pRxWI;

	pRxContext = &pAd->RxContext[pAd->NextRxBulkInReadIndex];
	if ((pRxContext->Readable == FALSE) || (pRxContext->InUse == TRUE))
		return NULL;

	RxBufferLength = pRxContext->BulkInOffset - pAd->ReadPosition;
	if (RxBufferLength < (RT2870_RXDMALEN_FIELD_SIZE + sizeof(RXWI_STRUC) + sizeof(RXINFO_STRUC)))
	{
		goto label_null;
	}

	pData = &pRxContext->TransferBuffer[pAd->ReadPosition]; /* 4KB */
	// The RXDMA field is 4 bytes, now just use the first 2 bytes. The Length including the (RXWI + MSDU + Padding)
	ThisFrameLen = *pData + (*(pData+1)<<8);
    if (ThisFrameLen == 0)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("BIRIdx(%d): RXDMALen is zero.[%ld], BulkInBufLen = %ld)\n",
								pAd->NextRxBulkInReadIndex, ThisFrameLen, pRxContext->BulkInOffset));
		goto label_null;
	}
	if ((ThisFrameLen&0x3) != 0)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("BIRIdx(%d): RXDMALen not multiple of 4.[%ld], BulkInBufLen = %ld)\n",
								pAd->NextRxBulkInReadIndex, ThisFrameLen, pRxContext->BulkInOffset));
		goto label_null;
	}

	if ((ThisFrameLen + 8)> RxBufferLength)	// 8 for (RT2870_RXDMALEN_FIELD_SIZE + sizeof(RXINFO_STRUC))
	{
		DBGPRINT(RT_DEBUG_TRACE,("BIRIdx(%d):FrameLen(0x%lx) outranges. BulkInLen=0x%lx, remaining RxBufLen=0x%lx, ReadPos=0x%lx\n",
						pAd->NextRxBulkInReadIndex, ThisFrameLen, pRxContext->BulkInOffset, RxBufferLength, pAd->ReadPosition));

		// error frame. finish this loop
		goto label_null;
	}

	// skip USB frame length field
	pData += RT2870_RXDMALEN_FIELD_SIZE;
	pRxWI = (PRXWI_STRUC)pData;

	if (pRxWI->MPDUtotalByteCount > ThisFrameLen)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s():pRxWIMPDUtotalByteCount(%d) large than RxDMALen(%ld)\n",
									__func__, pRxWI->MPDUtotalByteCount, ThisFrameLen));
		goto label_null;
	}

	// allocate a rx packet
	pSkb = dev_alloc_skb(ThisFrameLen);
	if (pSkb == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR,("%s():Cannot Allocate sk buffer for this Bulk-In buffer!\n", __func__));
		goto label_null;
	}

	// copy the rx packet
	memcpy(skb_put(pSkb, ThisFrameLen), pData, ThisFrameLen);
	RTPKT_TO_OSPKT(pSkb)->dev = get_netdev_from_bssid(pAd, BSS0);
	RTMP_SET_PACKET_SOURCE(OSPKT_TO_RTPKT(pSkb), PKTSRC_NDIS);

	// copy RxD
	*pSaveRxD = *(PRXINFO_STRUC)(pData + ThisFrameLen);

	// update next packet read position.
	pAd->ReadPosition += (ThisFrameLen + RT2870_RXDMALEN_FIELD_SIZE + RXINFO_SIZE);	// 8 for (RT2870_RXDMALEN_FIELD_SIZE + sizeof(RXINFO_STRUC))

	return pSkb;

label_null:

	return NULL;
}


/*
========================================================================
Routine Description:
    Handle received packets.

Arguments:
	data				- URB information pointer

Return Value:
    None

Note:
========================================================================
*/
static void rx_done_tasklet(unsigned long data)
{
	purbb_t 			pUrb;
	PRX_CONTEXT			pRxContext;
	PRTMP_ADAPTER		pAd;
	NTSTATUS			Status;
	unsigned int		IrqFlags;

	pUrb		= (purbb_t)data;
	pRxContext	= (PRX_CONTEXT)pUrb->context;
	pAd 		= pRxContext->pAd;
	Status = pUrb->status;


	RTMP_IRQ_LOCK(&pAd->BulkInLock, IrqFlags);
	pRxContext->InUse = FALSE;
	pRxContext->IRPPending = FALSE;
	pRxContext->BulkInOffset += pUrb->actual_length;
	//NdisInterlockedDecrement(&pAd->PendingRx);
	pAd->PendingRx--;

	if (Status == USB_ST_NOERROR)
	{
		pAd->BulkInComplete++;
		pAd->NextRxBulkInPosition = 0;
		if (pRxContext->BulkInOffset)	// As jan's comment, it may bulk-in success but size is zero.
		{
			pRxContext->Readable = TRUE;
			INC_RING_INDEX(pAd->NextRxBulkInIndex, RX_RING_SIZE);
		}
		RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);
	}
	else	 // STATUS_OTHER
	{
		pAd->BulkInCompleteFail++;
		// Still read this packet although it may comtain wrong bytes.
		pRxContext->Readable = FALSE;
		RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);

		// Parsing all packets. because after reset, the index will reset to all zero.
		if ((!RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
									fRTMP_ADAPTER_BULKIN_RESET |
									fRTMP_ADAPTER_HALT_IN_PROGRESS |
									fRTMP_ADAPTER_NIC_NOT_EXIST))))
		{

			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Bulk In Failed. Status=%d, BIIdx=0x%x, BIRIdx=0x%x, actual_length= 0x%x\n",
							Status, pAd->NextRxBulkInIndex, pAd->NextRxBulkInReadIndex, pRxContext->pUrb->actual_length));

			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKIN_RESET);
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_IN, NULL, 0);
		}
	}

	ASSERT((pRxContext->InUse == pRxContext->IRPPending));

	RTUSBBulkReceive(pAd);

	return;

}


static void rt2870_mgmt_dma_done_tasklet(unsigned long data)
{
	PRTMP_ADAPTER 	pAd;
	PTX_CONTEXT		pMLMEContext;
	int				index;
	PNDIS_PACKET	pPacket;
	purbb_t			pUrb;
	NTSTATUS		Status;
	unsigned long	IrqFlags;


	pUrb			= (purbb_t)data;
	pMLMEContext	= (PTX_CONTEXT)pUrb->context;
	pAd 			= pMLMEContext->pAd;
	Status			= pUrb->status;
	index 			= pMLMEContext->SelfIdx;

	ASSERT((pAd->MgmtRing.TxDmaIdx == index));

	RTMP_IRQ_LOCK(&pAd->BulkOutLock[MGMTPIPEIDX], IrqFlags);


	if (Status != USB_ST_NOERROR)
	{
		//Bulk-Out fail status handle
		if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET)))
		{
			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Bulk Out MLME Failed, Status=%d!\n", Status));
			// TODO: How to handle about the MLMEBulkOut failed issue. Need to resend the mgmt pkt?
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			pAd->bulkResetPipeid = (MGMTPIPEIDX | BULKOUT_MGMT_RESET_FLAG);
		}
	}

	pAd->BulkOutPending[MGMTPIPEIDX] = FALSE;
	RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[MGMTPIPEIDX], IrqFlags);

	RTMP_IRQ_LOCK(&pAd->MLMEBulkOutLock, IrqFlags);
	// Reset MLME context flags
	pMLMEContext->IRPPending = FALSE;
	pMLMEContext->InUse = FALSE;
	pMLMEContext->bWaitingBulkOut = FALSE;
	pMLMEContext->BulkOutSize = 0;

	pPacket = pAd->MgmtRing.Cell[index].pNdisPacket;
	pAd->MgmtRing.Cell[index].pNdisPacket = NULL;

	// Increase MgmtRing Index
	INC_RING_INDEX(pAd->MgmtRing.TxDmaIdx, MGMT_RING_SIZE);
	pAd->MgmtRing.TxSwFreeIdx++;
	RTMP_IRQ_UNLOCK(&pAd->MLMEBulkOutLock, IrqFlags);

	// No-matter success or fail, we free the mgmt packet.
	if (pPacket)
		RTMPFreeNdisPacket(pAd, pPacket);

	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
								fRTMP_ADAPTER_HALT_IN_PROGRESS |
								fRTMP_ADAPTER_NIC_NOT_EXIST))))
	{
		// do nothing and return directly.
	}
	else
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET) &&
			((pAd->bulkResetPipeid & BULKOUT_MGMT_RESET_FLAG) == BULKOUT_MGMT_RESET_FLAG))
		{	// For Mgmt Bulk-Out failed, ignore it now.
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT, NULL, 0);
		}
		else
		{

			// Always call Bulk routine, even reset bulk.
			// The protectioon of rest bulk should be in BulkOut routine
			if (pAd->MgmtRing.TxSwFreeIdx < MGMT_RING_SIZE /* pMLMEContext->bWaitingBulkOut == TRUE */)
			{
				RTUSB_SET_BULK_FLAG(pAd, fRTUSB_BULK_OUT_MLME);
			}
				RTUSBKickBulkOut(pAd);
			}
		}

}


static void rt2870_hcca_dma_done_tasklet(unsigned long data)
{
	PRTMP_ADAPTER		pAd;
	PHT_TX_CONTEXT		pHTTXContext;
	UCHAR				BulkOutPipeId = 4;
	purbb_t				pUrb;


	pUrb			= (purbb_t)data;
	pHTTXContext	= (PHT_TX_CONTEXT)pUrb->context;
	pAd				= pHTTXContext->pAd;

	rt2870_dataout_complete_tasklet((unsigned long)pUrb);

	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
								fRTMP_ADAPTER_HALT_IN_PROGRESS |
								fRTMP_ADAPTER_NIC_NOT_EXIST))))
	{
		// do nothing and return directly.
	}
	else
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET))
		{
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT, NULL, 0);
		}
		else
		{	pHTTXContext = &pAd->TxContext[BulkOutPipeId];
			if ((pAd->TxSwQueue[BulkOutPipeId].Number > 0) &&
				/*((pHTTXContext->CurWritePosition > (pHTTXContext->NextBulkOutPosition + 0x6000)) || (pHTTXContext->NextBulkOutPosition > pHTTXContext->CurWritePosition + 0x6000)) && */
				(pAd->DeQueueRunning[BulkOutPipeId] == FALSE) &&
				(pHTTXContext->bCurWriting == FALSE))
			{
				RTMPDeQueuePacket(pAd, FALSE, BulkOutPipeId, MAX_TX_PROCESS);
			}

			RTUSB_SET_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL<<4);
			RTUSBKickBulkOut(pAd);
		}
	}


		return;
}


static void rt2870_ac3_dma_done_tasklet(unsigned long data)
{
	PRTMP_ADAPTER		pAd;
	PHT_TX_CONTEXT		pHTTXContext;
	UCHAR				BulkOutPipeId = 3;
	purbb_t				pUrb;


	pUrb			= (purbb_t)data;
	pHTTXContext	= (PHT_TX_CONTEXT)pUrb->context;
	pAd				= pHTTXContext->pAd;

	rt2870_dataout_complete_tasklet((unsigned long)pUrb);

	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
								fRTMP_ADAPTER_HALT_IN_PROGRESS |
								fRTMP_ADAPTER_NIC_NOT_EXIST))))
	{
		// do nothing and return directly.
	}
	else
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET))
		{
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT, NULL, 0);
		}
		else
		{	pHTTXContext = &pAd->TxContext[BulkOutPipeId];
			if ((pAd->TxSwQueue[BulkOutPipeId].Number > 0) &&
				/*((pHTTXContext->CurWritePosition > (pHTTXContext->NextBulkOutPosition + 0x6000)) || (pHTTXContext->NextBulkOutPosition > pHTTXContext->CurWritePosition + 0x6000)) && */
				(pAd->DeQueueRunning[BulkOutPipeId] == FALSE) &&
				(pHTTXContext->bCurWriting == FALSE))
			{
				RTMPDeQueuePacket(pAd, FALSE, BulkOutPipeId, MAX_TX_PROCESS);
			}

			RTUSB_SET_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL<<3);
			RTUSBKickBulkOut(pAd);
		}
	}


		return;
}


static void rt2870_ac2_dma_done_tasklet(unsigned long data)
{
	PRTMP_ADAPTER		pAd;
	PHT_TX_CONTEXT		pHTTXContext;
	UCHAR				BulkOutPipeId = 2;
	purbb_t				pUrb;


	pUrb			= (purbb_t)data;
	pHTTXContext	= (PHT_TX_CONTEXT)pUrb->context;
	pAd				= pHTTXContext->pAd;

	rt2870_dataout_complete_tasklet((unsigned long)pUrb);

	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
								fRTMP_ADAPTER_HALT_IN_PROGRESS |
								fRTMP_ADAPTER_NIC_NOT_EXIST))))
	{
		// do nothing and return directly.
	}
	else
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET))
		{
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT, NULL, 0);
		}
		else
		{	pHTTXContext = &pAd->TxContext[BulkOutPipeId];
			if ((pAd->TxSwQueue[BulkOutPipeId].Number > 0) &&
				/*((pHTTXContext->CurWritePosition > (pHTTXContext->NextBulkOutPosition + 0x6000)) || (pHTTXContext->NextBulkOutPosition > pHTTXContext->CurWritePosition + 0x6000)) && */
				(pAd->DeQueueRunning[BulkOutPipeId] == FALSE) &&
				(pHTTXContext->bCurWriting == FALSE))
			{
				RTMPDeQueuePacket(pAd, FALSE, BulkOutPipeId, MAX_TX_PROCESS);
			}

			RTUSB_SET_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL<<2);
			RTUSBKickBulkOut(pAd);
		}
	}

		return;
}


static void rt2870_ac1_dma_done_tasklet(unsigned long data)
{
	PRTMP_ADAPTER		pAd;
	PHT_TX_CONTEXT		pHTTXContext;
	UCHAR				BulkOutPipeId = 1;
	purbb_t				pUrb;


	pUrb			= (purbb_t)data;
	pHTTXContext	= (PHT_TX_CONTEXT)pUrb->context;
	pAd				= pHTTXContext->pAd;

	rt2870_dataout_complete_tasklet((unsigned long)pUrb);

	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
								fRTMP_ADAPTER_HALT_IN_PROGRESS |
								fRTMP_ADAPTER_NIC_NOT_EXIST))))
	{
		// do nothing and return directly.
	}
	else
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET))
		{
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT, NULL, 0);
		}
		else
		{	pHTTXContext = &pAd->TxContext[BulkOutPipeId];
			if ((pAd->TxSwQueue[BulkOutPipeId].Number > 0) &&
				/*((pHTTXContext->CurWritePosition > (pHTTXContext->NextBulkOutPosition + 0x6000)) || (pHTTXContext->NextBulkOutPosition > pHTTXContext->CurWritePosition + 0x6000)) && */
				(pAd->DeQueueRunning[BulkOutPipeId] == FALSE) &&
				(pHTTXContext->bCurWriting == FALSE))
			{
				RTMPDeQueuePacket(pAd, FALSE, BulkOutPipeId, MAX_TX_PROCESS);
			}

			RTUSB_SET_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL<<1);
			RTUSBKickBulkOut(pAd);
		}
	}


	return;
}


static void rt2870_ac0_dma_done_tasklet(unsigned long data)
{
	PRTMP_ADAPTER		pAd;
	PHT_TX_CONTEXT		pHTTXContext;
	UCHAR				BulkOutPipeId = 0;
	purbb_t				pUrb;


	pUrb			= (purbb_t)data;
	pHTTXContext	= (PHT_TX_CONTEXT)pUrb->context;
	pAd				= pHTTXContext->pAd;

	rt2870_dataout_complete_tasklet((unsigned long)pUrb);

	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
								fRTMP_ADAPTER_HALT_IN_PROGRESS |
								fRTMP_ADAPTER_NIC_NOT_EXIST))))
	{
		// do nothing and return directly.
	}
	else
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET))
		{
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT, NULL, 0);
		}
		else
		{	pHTTXContext = &pAd->TxContext[BulkOutPipeId];
			if ((pAd->TxSwQueue[BulkOutPipeId].Number > 0) &&
				/*  ((pHTTXContext->CurWritePosition > (pHTTXContext->NextBulkOutPosition + 0x6000)) || (pHTTXContext->NextBulkOutPosition > pHTTXContext->CurWritePosition + 0x6000)) && */
				(pAd->DeQueueRunning[BulkOutPipeId] == FALSE) &&
				(pHTTXContext->bCurWriting == FALSE))
			{
				RTMPDeQueuePacket(pAd, FALSE, BulkOutPipeId, MAX_TX_PROCESS);
			}

			RTUSB_SET_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL);
			RTUSBKickBulkOut(pAd);
		}
	}


	return;

}


static void rt2870_null_frame_complete_tasklet(unsigned long data)
{
	PRTMP_ADAPTER	pAd;
	PTX_CONTEXT		pNullContext;
	purbb_t			pUrb;
	NTSTATUS		Status;
	unsigned long	irqFlag;


	pUrb			= (purbb_t)data;
	pNullContext	= (PTX_CONTEXT)pUrb->context;
	pAd 			= pNullContext->pAd;
	Status 			= pUrb->status;

	// Reset Null frame context flags
	RTMP_IRQ_LOCK(&pAd->BulkOutLock[0], irqFlag);
	pNullContext->IRPPending 	= FALSE;
	pNullContext->InUse 		= FALSE;
	pAd->BulkOutPending[0] = FALSE;
	pAd->watchDogTxPendingCnt[0] = 0;

	if (Status == USB_ST_NOERROR)
	{
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], irqFlag);

		RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
	}
	else	// STATUS_OTHER
	{
		if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET)))
		{
			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Bulk Out Null Frame Failed, ReasonCode=%d!\n", Status));
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			pAd->bulkResetPipeid = (MGMTPIPEIDX | BULKOUT_MGMT_RESET_FLAG);
			RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], irqFlag);
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT, NULL, 0);
		}
		else
		{
			RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], irqFlag);
		}
	}

	// Always call Bulk routine, even reset bulk.
	// The protectioon of rest bulk should be in BulkOut routine
	RTUSBKickBulkOut(pAd);

}


static void rt2870_rts_frame_complete_tasklet(unsigned long data)
{
	PRTMP_ADAPTER	pAd;
	PTX_CONTEXT		pRTSContext;
	purbb_t			pUrb;
	NTSTATUS		Status;
	unsigned long	irqFlag;


	pUrb		= (purbb_t)data;
	pRTSContext	= (PTX_CONTEXT)pUrb->context;
	pAd			= pRTSContext->pAd;
	Status		= pUrb->status;

	// Reset RTS frame context flags
	RTMP_IRQ_LOCK(&pAd->BulkOutLock[0], irqFlag);
	pRTSContext->IRPPending = FALSE;
	pRTSContext->InUse		= FALSE;

	if (Status == USB_ST_NOERROR)
	{
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], irqFlag);
		RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
	}
	else	// STATUS_OTHER
	{
		if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET)))
		{
			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Bulk Out RTS Frame Failed\n"));
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			pAd->bulkResetPipeid = (MGMTPIPEIDX | BULKOUT_MGMT_RESET_FLAG);
			RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], irqFlag);
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT, NULL, 0);
		}
		else
		{
			RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], irqFlag);
		}
	}

	RTMP_SEM_LOCK(&pAd->BulkOutLock[pRTSContext->BulkOutPipeId]);
	pAd->BulkOutPending[pRTSContext->BulkOutPipeId] = FALSE;
	RTMP_SEM_UNLOCK(&pAd->BulkOutLock[pRTSContext->BulkOutPipeId]);

	// Always call Bulk routine, even reset bulk.
	// The protectioon of rest bulk should be in BulkOut routine
	RTUSBKickBulkOut(pAd);

}


static void rt2870_pspoll_frame_complete_tasklet(unsigned long data)
{
	PRTMP_ADAPTER	pAd;
	PTX_CONTEXT		pPsPollContext;
	purbb_t			pUrb;
	NTSTATUS		Status;


	pUrb			= (purbb_t)data;
	pPsPollContext	= (PTX_CONTEXT)pUrb->context;
	pAd				= pPsPollContext->pAd;
	Status			= pUrb->status;

	// Reset PsPoll context flags
	pPsPollContext->IRPPending	= FALSE;
	pPsPollContext->InUse		= FALSE;
	pAd->watchDogTxPendingCnt[0] = 0;

	if (Status == USB_ST_NOERROR)
	{
		RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
	}
	else // STATUS_OTHER
	{
		if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET)))
		{
			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Bulk Out PSPoll Failed\n"));
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			pAd->bulkResetPipeid = (MGMTPIPEIDX | BULKOUT_MGMT_RESET_FLAG);
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT, NULL, 0);
		}
	}

	RTMP_SEM_LOCK(&pAd->BulkOutLock[0]);
	pAd->BulkOutPending[0] = FALSE;
	RTMP_SEM_UNLOCK(&pAd->BulkOutLock[0]);

	// Always call Bulk routine, even reset bulk.
	// The protectioon of rest bulk should be in BulkOut routine
	RTUSBKickBulkOut(pAd);

}


static void rt2870_dataout_complete_tasklet(unsigned long data)
{
	PRTMP_ADAPTER		pAd;
	purbb_t				pUrb;
	POS_COOKIE			pObj;
	PHT_TX_CONTEXT		pHTTXContext;
	UCHAR				BulkOutPipeId;
	NTSTATUS			Status;
	unsigned long		IrqFlags;


	pUrb			= (purbb_t)data;
	pHTTXContext	= (PHT_TX_CONTEXT)pUrb->context;
	pAd				= pHTTXContext->pAd;
	pObj 			= (POS_COOKIE) pAd->OS_Cookie;
	Status			= pUrb->status;

	// Store BulkOut PipeId
	BulkOutPipeId = pHTTXContext->BulkOutPipeId;
	pAd->BulkOutDataOneSecCount++;

	//DBGPRINT(RT_DEBUG_LOUD, ("Done-B(%d):I=0x%lx, CWPos=%ld, NBPos=%ld, ENBPos=%ld, bCopy=%d!\n", BulkOutPipeId, in_interrupt(), pHTTXContext->CurWritePosition,
	//		pHTTXContext->NextBulkOutPosition, pHTTXContext->ENextBulkOutPosition, pHTTXContext->bCopySavePad));

	RTMP_IRQ_LOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
	pAd->BulkOutPending[BulkOutPipeId] = FALSE;
	pHTTXContext->IRPPending = FALSE;
	pAd->watchDogTxPendingCnt[BulkOutPipeId] = 0;

	if (Status == USB_ST_NOERROR)
	{
		pAd->BulkOutComplete++;

		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);

		pAd->Counters8023.GoodTransmits++;
		//RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);
		FREE_HTTX_RING(pAd, BulkOutPipeId, pHTTXContext);
		//RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);


	}
	else	// STATUS_OTHER
	{
		PUCHAR	pBuf;

		pAd->BulkOutCompleteOther++;

		pBuf = &pHTTXContext->TransferBuffer->field.WirelessPacket[pHTTXContext->NextBulkOutPosition];

		if (!RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
									fRTMP_ADAPTER_HALT_IN_PROGRESS |
									fRTMP_ADAPTER_NIC_NOT_EXIST |
									fRTMP_ADAPTER_BULKOUT_RESET)))
		{
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			pAd->bulkResetPipeid = BulkOutPipeId;
			pAd->bulkResetReq[BulkOutPipeId] = pAd->BulkOutReq;
		}
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);

		DBGPRINT_RAW(RT_DEBUG_ERROR, ("BulkOutDataPacket failed: ReasonCode=%d!\n", Status));
		DBGPRINT_RAW(RT_DEBUG_ERROR, ("\t>>BulkOut Req=0x%lx, Complete=0x%lx, Other=0x%lx\n", pAd->BulkOutReq, pAd->BulkOutComplete, pAd->BulkOutCompleteOther));
		DBGPRINT_RAW(RT_DEBUG_ERROR, ("\t>>BulkOut Header:%x %x %x %x %x %x %x %x\n", pBuf[0], pBuf[1], pBuf[2], pBuf[3], pBuf[4], pBuf[5], pBuf[6], pBuf[7]));
		//DBGPRINT_RAW(RT_DEBUG_ERROR, (">>BulkOutCompleteCancel=0x%x, BulkOutCompleteOther=0x%x\n", pAd->BulkOutCompleteCancel, pAd->BulkOutCompleteOther));

	}

	//
	// bInUse = TRUE, means some process are filling TX data, after that must turn on bWaitingBulkOut
	// bWaitingBulkOut = TRUE, means the TX data are waiting for bulk out.
	//
	//RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);
	if ((pHTTXContext->ENextBulkOutPosition != pHTTXContext->CurWritePosition) &&
		(pHTTXContext->ENextBulkOutPosition != (pHTTXContext->CurWritePosition+8)) &&
		!RTUSB_TEST_BULK_FLAG(pAd, (fRTUSB_BULK_OUT_DATA_FRAG << BulkOutPipeId)))
	{
		// Indicate There is data avaliable
		RTUSB_SET_BULK_FLAG(pAd, (fRTUSB_BULK_OUT_DATA_NORMAL << BulkOutPipeId));
	}
	//RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);

	// Always call Bulk routine, even reset bulk.
	// The protection of rest bulk should be in BulkOut routine
	RTUSBKickBulkOut(pAd);
}

/* End of 2870_rtmp_init.c */
