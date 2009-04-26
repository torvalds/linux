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
	rtusb_bulk.c

	Abstract:

	Revision History:
	Who			When		What
	--------	----------	----------------------------------------------
	Name		Date		Modification logs
	Paul Lin	06-25-2004	created

*/

#include "../rt_config.h"
// Match total 6 bulkout endpoint to corresponding queue.
UCHAR	EpToQueue[6]={FIFO_EDCA, FIFO_EDCA, FIFO_EDCA, FIFO_EDCA, FIFO_EDCA, FIFO_MGMT};

//static BOOLEAN SingleBulkOut = FALSE;

void RTUSB_FILL_BULK_URB (struct urb *pUrb,
	struct usb_device *pUsb_Dev,
	unsigned int bulkpipe,
	void *pTransferBuf,
	int BufSize,
	usb_complete_t Complete,
	void *pContext)
{

	usb_fill_bulk_urb(pUrb, pUsb_Dev, bulkpipe, pTransferBuf, BufSize, (usb_complete_t)Complete, pContext);

}

VOID	RTUSBInitTxDesc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PTX_CONTEXT		pTxContext,
	IN	UCHAR			BulkOutPipeId,
	IN	usb_complete_t	Func)
{
	PURB				pUrb;
	PUCHAR				pSrc = NULL;
	POS_COOKIE			pObj = (POS_COOKIE) pAd->OS_Cookie;

	pUrb = pTxContext->pUrb;
	ASSERT(pUrb);

	// Store BulkOut PipeId
	pTxContext->BulkOutPipeId = BulkOutPipeId;

	if (pTxContext->bAggregatible)
	{
		pSrc = &pTxContext->TransferBuffer->Aggregation[2];
	}
	else
	{
		pSrc = (PUCHAR) pTxContext->TransferBuffer->field.WirelessPacket;
	}


	//Initialize a tx bulk urb
	RTUSB_FILL_BULK_URB(pUrb,
						pObj->pUsb_Dev,
						usb_sndbulkpipe(pObj->pUsb_Dev, pAd->BulkOutEpAddr[BulkOutPipeId]),
						pSrc,
						pTxContext->BulkOutSize,
						Func,
						pTxContext);

	if (pTxContext->bAggregatible)
		pUrb->transfer_dma	= (pTxContext->data_dma + TX_BUFFER_NORMSIZE + 2);
	else
		pUrb->transfer_dma	= pTxContext->data_dma;

	pUrb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

}

VOID	RTUSBInitHTTxDesc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PHT_TX_CONTEXT	pTxContext,
	IN	UCHAR			BulkOutPipeId,
	IN	ULONG			BulkOutSize,
	IN	usb_complete_t	Func)
{
	PURB				pUrb;
	PUCHAR				pSrc = NULL;
	POS_COOKIE			pObj = (POS_COOKIE) pAd->OS_Cookie;

	pUrb = pTxContext->pUrb;
	ASSERT(pUrb);

	// Store BulkOut PipeId
	pTxContext->BulkOutPipeId = BulkOutPipeId;

	pSrc = &pTxContext->TransferBuffer->field.WirelessPacket[pTxContext->NextBulkOutPosition];


	//Initialize a tx bulk urb
	RTUSB_FILL_BULK_URB(pUrb,
						pObj->pUsb_Dev,
						usb_sndbulkpipe(pObj->pUsb_Dev, pAd->BulkOutEpAddr[BulkOutPipeId]),
						pSrc,
						BulkOutSize,
						Func,
						pTxContext);

	pUrb->transfer_dma	= (pTxContext->data_dma + pTxContext->NextBulkOutPosition);
	pUrb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

}

VOID	RTUSBInitRxDesc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PRX_CONTEXT		pRxContext)
{
	PURB				pUrb;
	POS_COOKIE			pObj = (POS_COOKIE) pAd->OS_Cookie;
	ULONG				RX_bulk_size;


	pUrb = pRxContext->pUrb;
	ASSERT(pUrb);

	if ( pAd->BulkInMaxPacketSize == 64)
		RX_bulk_size = 4096;
	else
		RX_bulk_size = MAX_RXBULK_SIZE;

	//Initialize a rx bulk urb
	RTUSB_FILL_BULK_URB(pUrb,
						pObj->pUsb_Dev,
						usb_rcvbulkpipe(pObj->pUsb_Dev, pAd->BulkInEpAddr),
						&(pRxContext->TransferBuffer[pAd->NextRxBulkInPosition]),
						RX_bulk_size - (pAd->NextRxBulkInPosition),
						(usb_complete_t)RTUSBBulkRxComplete,
						(void *)pRxContext);

	pUrb->transfer_dma	= pRxContext->data_dma + pAd->NextRxBulkInPosition;
	pUrb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;


}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

	========================================================================
*/

#define BULK_OUT_LOCK(pLock, IrqFlags)	\
		if(1 /*!(in_interrupt() & 0xffff0000)*/)	\
			RTMP_IRQ_LOCK((pLock), IrqFlags);

#define BULK_OUT_UNLOCK(pLock, IrqFlags)	\
		if(1 /*!(in_interrupt() & 0xffff0000)*/)	\
			RTMP_IRQ_UNLOCK((pLock), IrqFlags);


VOID	RTUSBBulkOutDataPacket(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			BulkOutPipeId,
	IN	UCHAR			Index)
{

	PHT_TX_CONTEXT	pHTTXContext;
	PURB			pUrb;
	int				ret = 0;
	PTXINFO_STRUC	pTxInfo, pLastTxInfo = NULL;
	PTXWI_STRUC             pTxWI;
	ULONG			TmpBulkEndPos, ThisBulkSize;
	unsigned long	IrqFlags = 0, IrqFlags2 = 0;
	PUCHAR			pWirelessPkt, pAppendant;
	BOOLEAN			bTxQLastRound = FALSE;
	UCHAR			allzero[4]= {0x0,0x0,0x0,0x0};

	BULK_OUT_LOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
	if ((pAd->BulkOutPending[BulkOutPipeId] == TRUE) || RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NEED_STOP_TX))
	{
		BULK_OUT_UNLOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
		return;
	}
	pAd->BulkOutPending[BulkOutPipeId] = TRUE;

	if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)
		)
	{
		pAd->BulkOutPending[BulkOutPipeId] = FALSE;
		BULK_OUT_UNLOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
		return;
	}
	BULK_OUT_UNLOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);


	pHTTXContext = &(pAd->TxContext[BulkOutPipeId]);

	BULK_OUT_LOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags2);
	if ((pHTTXContext->ENextBulkOutPosition == pHTTXContext->CurWritePosition)
		|| ((pHTTXContext->ENextBulkOutPosition-8) == pHTTXContext->CurWritePosition))
	{
		BULK_OUT_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags2);

		BULK_OUT_LOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
		pAd->BulkOutPending[BulkOutPipeId] = FALSE;

		// Clear Data flag
		RTUSB_CLEAR_BULK_FLAG(pAd, (fRTUSB_BULK_OUT_DATA_FRAG << BulkOutPipeId));
		RTUSB_CLEAR_BULK_FLAG(pAd, (fRTUSB_BULK_OUT_DATA_NORMAL << BulkOutPipeId));

		BULK_OUT_UNLOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
		return;
	}

	// Clear Data flag
	RTUSB_CLEAR_BULK_FLAG(pAd, (fRTUSB_BULK_OUT_DATA_FRAG << BulkOutPipeId));
	RTUSB_CLEAR_BULK_FLAG(pAd, (fRTUSB_BULK_OUT_DATA_NORMAL << BulkOutPipeId));

	//DBGPRINT(RT_DEBUG_TRACE,("BulkOut-B:I=0x%lx, CWPos=%ld, CWRPos=%ld, NBPos=%ld, ENBPos=%ld, bCopy=%d!\n", in_interrupt(),
	//							pHTTXContext->CurWritePosition, pHTTXContext->CurWriteRealPos, pHTTXContext->NextBulkOutPosition,
	//							pHTTXContext->ENextBulkOutPosition, pHTTXContext->bCopySavePad));
	pHTTXContext->NextBulkOutPosition = pHTTXContext->ENextBulkOutPosition;
	ThisBulkSize = 0;
	TmpBulkEndPos = pHTTXContext->NextBulkOutPosition;
	pWirelessPkt = &pHTTXContext->TransferBuffer->field.WirelessPacket[0];

	if ((pHTTXContext->bCopySavePad == TRUE))
	{
		if (RTMPEqualMemory(pHTTXContext->SavedPad, allzero,4))
		{
			DBGPRINT_RAW(RT_DEBUG_ERROR,("e1, allzero : %x  %x  %x  %x  %x  %x  %x  %x \n",
				pHTTXContext->SavedPad[0], pHTTXContext->SavedPad[1], pHTTXContext->SavedPad[2],pHTTXContext->SavedPad[3]
				,pHTTXContext->SavedPad[4], pHTTXContext->SavedPad[5], pHTTXContext->SavedPad[6],pHTTXContext->SavedPad[7]));
		}
		NdisMoveMemory(&pWirelessPkt[TmpBulkEndPos], pHTTXContext->SavedPad, 8);
		pHTTXContext->bCopySavePad = FALSE;
		if (pAd->bForcePrintTX == TRUE)
			DBGPRINT(RT_DEBUG_TRACE,("RTUSBBulkOutDataPacket --> COPY PAD. CurWrite = %ld, NextBulk = %ld.   ENextBulk = %ld.\n",   pHTTXContext->CurWritePosition, pHTTXContext->NextBulkOutPosition, pHTTXContext->ENextBulkOutPosition));
	}

	do
	{
		pTxInfo = (PTXINFO_STRUC)&pWirelessPkt[TmpBulkEndPos];
		pTxWI = (PTXWI_STRUC)&pWirelessPkt[TmpBulkEndPos + TXINFO_SIZE];

		if (pAd->bForcePrintTX == TRUE)
			DBGPRINT(RT_DEBUG_TRACE, ("RTUSBBulkOutDataPacket AMPDU = %d.\n",   pTxWI->AMPDU));

		// add by Iverson, limit BulkOut size to 4k to pass WMM b mode 2T1R test items
		//if ((ThisBulkSize != 0)  && (pTxWI->AMPDU == 0))
		if ((ThisBulkSize != 0) && (pTxWI->PHYMODE == MODE_CCK))
		{
			if (((ThisBulkSize&0xffff8000) != 0) || ((ThisBulkSize&0x1000) == 0x1000))
			{
				// Limit BulkOut size to about 4k bytes.
				pHTTXContext->ENextBulkOutPosition = TmpBulkEndPos;
				break;
			}
			else if (((pAd->BulkOutMaxPacketSize < 512) && ((ThisBulkSize&0xfffff800) != 0) ) /*|| ( (ThisBulkSize != 0)  && (pTxWI->AMPDU == 0))*/)
			{
				// For USB 1.1 or peer which didn't support AMPDU, limit the BulkOut size.
				// For performence in b/g mode, now just check for USB 1.1 and didn't care about the APMDU or not! 2008/06/04.
				pHTTXContext->ENextBulkOutPosition = TmpBulkEndPos;
				break;
			}
		}
		// end Iverson
		else
		{
			if (((ThisBulkSize&0xffff8000) != 0) || ((ThisBulkSize&0x6000) == 0x6000))
			{	// Limit BulkOut size to about 24k bytes.
				pHTTXContext->ENextBulkOutPosition = TmpBulkEndPos;
				break;
			}
			else if (((pAd->BulkOutMaxPacketSize < 512) && ((ThisBulkSize&0xfffff800) != 0) ) /*|| ( (ThisBulkSize != 0)  && (pTxWI->AMPDU == 0))*/)
			{	// For USB 1.1 or peer which didn't support AMPDU, limit the BulkOut size.
				// For performence in b/g mode, now just check for USB 1.1 and didn't care about the APMDU or not! 2008/06/04.
				pHTTXContext->ENextBulkOutPosition = TmpBulkEndPos;
				break;
			}
		}

		if (TmpBulkEndPos == pHTTXContext->CurWritePosition)
		{
			pHTTXContext->ENextBulkOutPosition = TmpBulkEndPos;
			break;
		}

		if (pTxInfo->QSEL != FIFO_EDCA)
		{
			printk("%s(): ====> pTxInfo->QueueSel(%d)!= FIFO_EDCA!!!!\n", __func__, pTxInfo->QSEL);
			printk("\tCWPos=%ld, NBPos=%ld, ENBPos=%ld, bCopy=%d!\n", pHTTXContext->CurWritePosition, pHTTXContext->NextBulkOutPosition, pHTTXContext->ENextBulkOutPosition, pHTTXContext->bCopySavePad);
			hex_dump("Wrong QSel Pkt:", (PUCHAR)&pWirelessPkt[TmpBulkEndPos], (pHTTXContext->CurWritePosition - pHTTXContext->NextBulkOutPosition));
		}

		if (pTxInfo->USBDMATxPktLen <= 8)
		{
			BULK_OUT_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags2);
			DBGPRINT(RT_DEBUG_ERROR /*RT_DEBUG_TRACE*/,("e2, USBDMATxPktLen==0, Size=%ld, bCSPad=%d, CWPos=%ld, NBPos=%ld, CWRPos=%ld!\n",
					pHTTXContext->BulkOutSize, pHTTXContext->bCopySavePad, pHTTXContext->CurWritePosition, pHTTXContext->NextBulkOutPosition, pHTTXContext->CurWriteRealPos));
			{
				DBGPRINT_RAW(RT_DEBUG_ERROR /*RT_DEBUG_TRACE*/,("%x  %x  %x  %x  %x  %x  %x  %x \n",
					pHTTXContext->SavedPad[0], pHTTXContext->SavedPad[1], pHTTXContext->SavedPad[2],pHTTXContext->SavedPad[3]
					,pHTTXContext->SavedPad[4], pHTTXContext->SavedPad[5], pHTTXContext->SavedPad[6],pHTTXContext->SavedPad[7]));
			}
			pAd->bForcePrintTX = TRUE;
			BULK_OUT_LOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
			pAd->BulkOutPending[BulkOutPipeId] = FALSE;
			BULK_OUT_UNLOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
			//DBGPRINT(RT_DEBUG_LOUD,("Out:pTxInfo->USBDMATxPktLen=%d!\n", pTxInfo->USBDMATxPktLen));
			return;
		}

			// Increase Total transmit byte counter
		pAd->RalinkCounters.OneSecTransmittedByteCount +=  pTxWI->MPDUtotalByteCount;
		pAd->RalinkCounters.TransmittedByteCount +=  pTxWI->MPDUtotalByteCount;

		pLastTxInfo = pTxInfo;

		// Make sure we use EDCA QUEUE.
		pTxInfo->QSEL = FIFO_EDCA;
		ThisBulkSize += (pTxInfo->USBDMATxPktLen+4);
		TmpBulkEndPos += (pTxInfo->USBDMATxPktLen+4);

		if (TmpBulkEndPos != pHTTXContext->CurWritePosition)
			pTxInfo->USBDMANextVLD = 1;

		if (pTxInfo->SwUseLastRound == 1)
		{
			if (pHTTXContext->CurWritePosition == 8)
				pTxInfo->USBDMANextVLD = 0;
			pTxInfo->SwUseLastRound = 0;

			bTxQLastRound = TRUE;
			pHTTXContext->ENextBulkOutPosition = 8;

			break;
		}
	}while (TRUE);

	// adjust the pTxInfo->USBDMANextVLD value of last pTxInfo.
	if (pLastTxInfo)
	{
		pLastTxInfo->USBDMANextVLD = 0;
	}

	/*
		We need to copy SavedPad when following condition matched!
			1. Not the last round of the TxQueue and
			2. any match of following cases:
				(1). The End Position of this bulk out is reach to the Currenct Write position and
						the TxInfo and related header already write to the CurWritePosition.
			   		=>(ENextBulkOutPosition == CurWritePosition) && (CurWriteRealPos > CurWritePosition)

				(2). The EndPosition of the bulk out is not reach to the Current Write Position.
					=>(ENextBulkOutPosition != CurWritePosition)
	*/
	if ((bTxQLastRound == FALSE) &&
		 (((pHTTXContext->ENextBulkOutPosition == pHTTXContext->CurWritePosition) && (pHTTXContext->CurWriteRealPos > pHTTXContext->CurWritePosition)) ||
		  (pHTTXContext->ENextBulkOutPosition != pHTTXContext->CurWritePosition))
		)
	{
		NdisMoveMemory(pHTTXContext->SavedPad, &pWirelessPkt[pHTTXContext->ENextBulkOutPosition], 8);
		pHTTXContext->bCopySavePad = TRUE;
		if (RTMPEqualMemory(pHTTXContext->SavedPad, allzero,4))
		{
			PUCHAR	pBuf = &pHTTXContext->SavedPad[0];
			DBGPRINT_RAW(RT_DEBUG_ERROR,("WARNING-Zero-3:%02x%02x%02x%02x%02x%02x%02x%02x,CWPos=%ld, CWRPos=%ld, bCW=%d, NBPos=%ld, TBPos=%ld, TBSize=%ld\n",
				pBuf[0], pBuf[1], pBuf[2],pBuf[3],pBuf[4], pBuf[5], pBuf[6],pBuf[7], pHTTXContext->CurWritePosition, pHTTXContext->CurWriteRealPos,
				pHTTXContext->bCurWriting, pHTTXContext->NextBulkOutPosition, TmpBulkEndPos, ThisBulkSize));

			pBuf = &pWirelessPkt[pHTTXContext->CurWritePosition];
			DBGPRINT_RAW(RT_DEBUG_ERROR,("\tCWPos=%02x%02x%02x%02x%02x%02x%02x%02x\n", pBuf[0], pBuf[1], pBuf[2],pBuf[3],pBuf[4], pBuf[5], pBuf[6],pBuf[7]));
		}
		//DBGPRINT(RT_DEBUG_LOUD,("ENPos==CWPos=%ld, CWRPos=%ld, bCSPad=%d!\n", pHTTXContext->CurWritePosition, pHTTXContext->CurWriteRealPos, pHTTXContext->bCopySavePad));
	}

	if (pAd->bForcePrintTX == TRUE)
		DBGPRINT(RT_DEBUG_TRACE,("BulkOut-A:Size=%ld, CWPos=%ld, NBPos=%ld, ENBPos=%ld, bCopy=%d!\n", ThisBulkSize, pHTTXContext->CurWritePosition, pHTTXContext->NextBulkOutPosition, pHTTXContext->ENextBulkOutPosition, pHTTXContext->bCopySavePad));
	//DBGPRINT(RT_DEBUG_LOUD,("BulkOut-A:Size=%ld, CWPos=%ld, CWRPos=%ld, NBPos=%ld, ENBPos=%ld, bCopy=%d, bLRound=%d!\n", ThisBulkSize, pHTTXContext->CurWritePosition, pHTTXContext->CurWriteRealPos, pHTTXContext->NextBulkOutPosition, pHTTXContext->ENextBulkOutPosition, pHTTXContext->bCopySavePad, bTxQLastRound));

		// USB DMA engine requires to pad extra 4 bytes. This pad doesn't count into real bulkoutsize.
	pAppendant = &pWirelessPkt[TmpBulkEndPos];
	NdisZeroMemory(pAppendant, 8);
		ThisBulkSize += 4;
		pHTTXContext->LastOne = TRUE;
		if ((ThisBulkSize % pAd->BulkOutMaxPacketSize) == 0)
			ThisBulkSize += 4;
	pHTTXContext->BulkOutSize = ThisBulkSize;

	pAd->watchDogTxPendingCnt[BulkOutPipeId] = 1;
	BULK_OUT_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags2);

	// Init Tx context descriptor
	RTUSBInitHTTxDesc(pAd, pHTTXContext, BulkOutPipeId, ThisBulkSize, (usb_complete_t)RTUSBBulkOutDataPacketComplete);

	pUrb = pHTTXContext->pUrb;
	if((ret = RTUSB_SUBMIT_URB(pUrb))!=0)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("RTUSBBulkOutDataPacket: Submit Tx URB failed %d\n", ret));

		BULK_OUT_LOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
		pAd->BulkOutPending[BulkOutPipeId] = FALSE;
		pAd->watchDogTxPendingCnt[BulkOutPipeId] = 0;
		BULK_OUT_UNLOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);

		return;
	}

	BULK_OUT_LOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
	pHTTXContext->IRPPending = TRUE;
	BULK_OUT_UNLOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
	pAd->BulkOutReq++;

}


VOID RTUSBBulkOutDataPacketComplete(purbb_t pUrb, struct pt_regs *pt_regs)
{
#if 0 // sample, IRQ LOCK
	PRTMP_ADAPTER		pAd;
	POS_COOKIE			pObj;
	PHT_TX_CONTEXT		pHTTXContext;
	UCHAR				BulkOutPipeId;
	NTSTATUS			Status;
	unsigned long		IrqFlags;

	DBGPRINT_RAW(RT_DEBUG_INFO, ("--->RTUSBBulkOutDataPacketComplete\n"));

	pHTTXContext	= (PHT_TX_CONTEXT)pUrb->context;
	pAd			= pHTTXContext->pAd;
	pObj 		= (POS_COOKIE) pAd->OS_Cookie;
	Status		= pUrb->status;

	// Store BulkOut PipeId
	BulkOutPipeId = pHTTXContext->BulkOutPipeId;
	pAd->BulkOutDataOneSecCount++;

	//DBGPRINT(RT_DEBUG_LOUD, ("Done-B(%d):I=0x%lx, CWPos=%ld, NBPos=%ld, ENBPos=%ld, bCopy=%d!\n", BulkOutPipeId, in_interrupt(), pHTTXContext->CurWritePosition,
	//		pHTTXContext->NextBulkOutPosition, pHTTXContext->ENextBulkOutPosition, pHTTXContext->bCopySavePad));

	RTMP_IRQ_LOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
	pAd->BulkOutPending[BulkOutPipeId] = FALSE;
	pHTTXContext->IRPPending = FALSE;
	RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);

	if (Status == USB_ST_NOERROR)
	{
		pAd->BulkOutComplete++;

		pAd->Counters8023.GoodTransmits++;
		//RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);
		FREE_HTTX_RING(pAd, BulkOutPipeId, pHTTXContext);
		//RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags);


	}
	else	// STATUS_OTHER
	{
		PUCHAR	pBuf;

		pAd->BulkOutCompleteOther++;

		pBuf = &pHTTXContext->TransferBuffer->WirelessPacket[pHTTXContext->NextBulkOutPosition];

		DBGPRINT_RAW(RT_DEBUG_ERROR, ("BulkOutDataPacket failed: ReasonCode=%d!\n", Status));
		DBGPRINT_RAW(RT_DEBUG_ERROR, (">>BulkOut Req=0x%lx, Complete=0x%lx, Other=0x%lx\n", pAd->BulkOutReq, pAd->BulkOutComplete, pAd->BulkOutCompleteOther));
		DBGPRINT_RAW(RT_DEBUG_ERROR, (">>BulkOut Header:%x %x %x %x %x %x %x %x\n", pBuf[0], pBuf[1], pBuf[2], pBuf[3], pBuf[4], pBuf[5], pBuf[6], pBuf[7]));
		//DBGPRINT_RAW(RT_DEBUG_ERROR, (">>BulkOutCompleteCancel=0x%x, BulkOutCompleteOther=0x%x\n", pAd->BulkOutCompleteCancel, pAd->BulkOutCompleteOther));

		if (!RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
									fRTMP_ADAPTER_HALT_IN_PROGRESS |
									fRTMP_ADAPTER_NIC_NOT_EXIST |
									fRTMP_ADAPTER_BULKOUT_RESET)))
		{
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			pAd->bulkResetPipeid = BulkOutPipeId;
		}
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


	//DBGPRINT(RT_DEBUG_LOUD,("Done-A(%d):I=0x%lx, CWPos=%ld, NBPos=%ld, ENBPos=%ld, bCopy=%d\n", BulkOutPipeId, in_interrupt(),
	//		pHTTXContext->CurWritePosition, pHTTXContext->NextBulkOutPosition, pHTTXContext->ENextBulkOutPosition, pHTTXContext->bCopySavePad));

	switch (BulkOutPipeId)
	{
		case 0:
				pObj->ac0_dma_done_task.data = (unsigned long)pAd;
				tasklet_hi_schedule(&pObj->ac0_dma_done_task);
				break;
		case 1:
				pObj->ac1_dma_done_task.data = (unsigned long)pAd;
				tasklet_hi_schedule(&pObj->ac1_dma_done_task);
				break;
		case 2:
				pObj->ac2_dma_done_task.data = (unsigned long)pAd;
				tasklet_hi_schedule(&pObj->ac2_dma_done_task);
				break;
		case 3:
				pObj->ac3_dma_done_task.data = (unsigned long)pAd;
				tasklet_hi_schedule(&pObj->ac3_dma_done_task);
				break;
		case 4:
				pObj->hcca_dma_done_task.data = (unsigned long)pAd;
				tasklet_hi_schedule(&pObj->hcca_dma_done_task);
				break;
	}
#else

{
	PHT_TX_CONTEXT	pHTTXContext;
	PRTMP_ADAPTER	pAd;
	POS_COOKIE 		pObj;
	UCHAR			BulkOutPipeId;


	pHTTXContext	= (PHT_TX_CONTEXT)pUrb->context;
	pAd 			= pHTTXContext->pAd;
	pObj 			= (POS_COOKIE) pAd->OS_Cookie;

	// Store BulkOut PipeId
	BulkOutPipeId	= pHTTXContext->BulkOutPipeId;
	pAd->BulkOutDataOneSecCount++;

	switch (BulkOutPipeId)
	{
		case 0:
				pObj->ac0_dma_done_task.data = (unsigned long)pUrb;
				tasklet_hi_schedule(&pObj->ac0_dma_done_task);
				break;
		case 1:
				pObj->ac1_dma_done_task.data = (unsigned long)pUrb;
				tasklet_hi_schedule(&pObj->ac1_dma_done_task);
				break;
		case 2:
				pObj->ac2_dma_done_task.data = (unsigned long)pUrb;
				tasklet_hi_schedule(&pObj->ac2_dma_done_task);
				break;
		case 3:
				pObj->ac3_dma_done_task.data = (unsigned long)pUrb;
				tasklet_hi_schedule(&pObj->ac3_dma_done_task);
				break;
		case 4:
				pObj->hcca_dma_done_task.data = (unsigned long)pUrb;
				tasklet_hi_schedule(&pObj->hcca_dma_done_task);
				break;
	}
}
#endif


}


/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note: NULL frame use BulkOutPipeId = 0

	========================================================================
*/
VOID	RTUSBBulkOutNullFrame(
	IN	PRTMP_ADAPTER	pAd)
{
	PTX_CONTEXT		pNullContext = &(pAd->NullContext);
	PURB			pUrb;
	int				ret = 0;
	unsigned long	IrqFlags;

	RTMP_IRQ_LOCK(&pAd->BulkOutLock[0], IrqFlags);
	if ((pAd->BulkOutPending[0] == TRUE) || RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NEED_STOP_TX))
	{
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], IrqFlags);
		return;
	}
	pAd->BulkOutPending[0] = TRUE;
	pAd->watchDogTxPendingCnt[0] = 1;
	pNullContext->IRPPending = TRUE;
	RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], IrqFlags);

	// Increase Total transmit byte counter
	pAd->RalinkCounters.TransmittedByteCount +=  pNullContext->BulkOutSize;


	// Clear Null frame bulk flag
	RTUSB_CLEAR_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NULL);

	// Init Tx context descriptor
	RTUSBInitTxDesc(pAd, pNullContext, 0, (usb_complete_t)RTUSBBulkOutNullFrameComplete);

	pUrb = pNullContext->pUrb;
	if((ret = RTUSB_SUBMIT_URB(pUrb))!=0)
	{
		RTMP_IRQ_LOCK(&pAd->BulkOutLock[0], IrqFlags);
		pAd->BulkOutPending[0] = FALSE;
		pAd->watchDogTxPendingCnt[0] = 0;
		pNullContext->IRPPending = FALSE;
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], IrqFlags);

		DBGPRINT(RT_DEBUG_ERROR, ("RTUSBBulkOutNullFrame: Submit Tx URB failed %d\n", ret));
		return;
	}

}

// NULL frame use BulkOutPipeId = 0
VOID RTUSBBulkOutNullFrameComplete(purbb_t pUrb, struct pt_regs *pt_regs)
{
	PRTMP_ADAPTER		pAd;
	PTX_CONTEXT			pNullContext;
	NTSTATUS			Status;
#if 0 // sample, IRQ LOCK
	unsigned long		IrqFlags;
#endif
	POS_COOKIE			pObj;


	pNullContext	= (PTX_CONTEXT)pUrb->context;
	pAd 			= pNullContext->pAd;
	Status 			= pUrb->status;

#if 0 // sample, IRQ LOCK
	// Reset Null frame context flags
	pNullContext->IRPPending 	= FALSE;
	pNullContext->InUse 		= FALSE;

	if (Status == USB_ST_NOERROR)
	{
		// Don't worry about the queue is empty or not, this function will check itself
		//RTMPUSBDeQueuePacket(pAd, 0);
		RTMPDeQueuePacket(pAd, TRUE, NUM_OF_TX_RING, MAX_TX_PROCESS);
	}
	else	// STATUS_OTHER
	{
		if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET)))
		{
			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Bulk Out Null Frame Failed\n"));
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT, NULL, 0);
		}
	}

	RTMP_IRQ_LOCK(&pAd->BulkOutLock[0], IrqFlags);
	pAd->BulkOutPending[0] = FALSE;
	RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], IrqFlags);

	// Always call Bulk routine, even reset bulk.
	// The protectioon of rest bulk should be in BulkOut routine
	RTUSBKickBulkOut(pAd);
#else

	pObj = (POS_COOKIE) pAd->OS_Cookie;
	pObj->null_frame_complete_task.data = (unsigned long)pUrb;
	tasklet_hi_schedule(&pObj->null_frame_complete_task);
#endif

}

#if 0	// For RT2870, RTS frame not used now, but maybe will use it latter.
/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note: RTS frame use BulkOutPipeId = 0

	========================================================================
*/
VOID	RTUSBBulkOutRTSFrame(
	IN	PRTMP_ADAPTER	pAd)
{
	PTX_CONTEXT		pRTSContext = &(pAd->RTSContext);
	PURB			pUrb;
	int				ret = 0;
	unsigned long	IrqFlags;
	UCHAR			PipeID=0;

	if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL_4))
		PipeID= 3;
	else if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL_3))
		PipeID= 2;
	else if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL_2))
		PipeID= 1;
	else if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL))
		PipeID= 0;

	RTMP_IRQ_LOCK(&pAd->BulkOutLock[PipeID], IrqFlags);
	if ((pAd->BulkOutPending[PipeID] == TRUE) || RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NEED_STOP_TX))
	{
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[PipeID], IrqFlags);
		return;
	}
	pAd->BulkOutPending[PipeID] = TRUE;
	RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[PipeID], IrqFlags);

	// Increase Total transmit byte counter
	pAd->RalinkCounters.TransmittedByteCount +=  pRTSContext->BulkOutSize;

	DBGPRINT_RAW(RT_DEBUG_INFO, ("--->RTUSBBulkOutRTSFrame \n"));

	// Clear RTS frame bulk flag
	RTUSB_CLEAR_BULK_FLAG(pAd, fRTUSB_BULK_OUT_RTS);

	// Init Tx context descriptor
	RTUSBInitTxDesc(pAd, pRTSContext, PipeID, (usb_complete_t)RTUSBBulkOutRTSFrameComplete);
	pRTSContext->IRPPending = TRUE;

	pUrb = pRTSContext->pUrb;
	if((ret = RTUSB_SUBMIT_URB(pUrb))!=0)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("RTUSBBulkOutRTSFrame: Submit Tx URB failed %d\n", ret));
		return;
	}

	DBGPRINT_RAW(RT_DEBUG_INFO, ("<---RTUSBBulkOutRTSFrame \n"));

}

// RTS frame use BulkOutPipeId = 0
VOID RTUSBBulkOutRTSFrameComplete(purbb_t pUrb, struct pt_regs *pt_regs)
{
	PRTMP_ADAPTER	pAd;
	PTX_CONTEXT		pRTSContext;
	NTSTATUS		Status;
#if 0 // sample, IRQ LOCK
	unsigned long	IrqFlags;
#endif
	POS_COOKIE		pObj;

	DBGPRINT_RAW(RT_DEBUG_INFO, ("--->RTUSBBulkOutRTSFrameComplete\n"));

	pRTSContext	= (PTX_CONTEXT)pUrb->context;
	pAd			= pRTSContext->pAd;
	Status		= pUrb->status;

#if 0 // sample, IRQ LOCK
	// Reset RTS frame context flags
	pRTSContext->IRPPending = FALSE;
	pRTSContext->InUse		= FALSE;

	if (Status == USB_ST_NOERROR)
	{
		// Don't worry about the queue is empty or not, this function will check itself
		//RTMPUSBDeQueuePacket(pAd, pRTSContext->BulkOutPipeId);
		RTMPDeQueuePacket(pAd, TRUE, NUM_OF_TX_RING, MAX_TX_PROCESS);
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
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT, NULL, 0);
		}
	}

	RTMP_IRQ_LOCK(&pAd->BulkOutLock[pRTSContext->BulkOutPipeId], IrqFlags);
	pAd->BulkOutPending[pRTSContext->BulkOutPipeId] = FALSE;
	RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[pRTSContext->BulkOutPipeId], IrqFlags);

	// Always call Bulk routine, even reset bulk.
	// The protectioon of rest bulk should be in BulkOut routine
	RTUSBKickBulkOut(pAd);
#else

	pObj = (POS_COOKIE) pAd->OS_Cookie;
	pObj->rts_frame_complete_task.data = (unsigned long)pUrb;
	tasklet_hi_schedule(&pObj->rts_frame_complete_task);
#endif

	DBGPRINT_RAW(RT_DEBUG_INFO, ("<---RTUSBBulkOutRTSFrameComplete\n"));

}
#endif

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note: MLME use BulkOutPipeId = 0

	========================================================================
*/
VOID	RTUSBBulkOutMLMEPacket(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			Index)
{
	PTX_CONTEXT		pMLMEContext;
	PURB			pUrb;
	int				ret = 0;
	unsigned long	IrqFlags;

	pMLMEContext = (PTX_CONTEXT)pAd->MgmtRing.Cell[pAd->MgmtRing.TxDmaIdx].AllocVa;
	pUrb = pMLMEContext->pUrb;

	if ((pAd->MgmtRing.TxSwFreeIdx >= MGMT_RING_SIZE) ||
		(pMLMEContext->InUse == FALSE) ||
		(pMLMEContext->bWaitingBulkOut == FALSE))
	{


		// Clear MLME bulk flag
		RTUSB_CLEAR_BULK_FLAG(pAd, fRTUSB_BULK_OUT_MLME);

		return;
	}


	RTMP_IRQ_LOCK(&pAd->BulkOutLock[MGMTPIPEIDX], IrqFlags);
	if ((pAd->BulkOutPending[MGMTPIPEIDX] == TRUE) || RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NEED_STOP_TX))
	{
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[MGMTPIPEIDX], IrqFlags);
		return;
	}

	pAd->BulkOutPending[MGMTPIPEIDX] = TRUE;
	pAd->watchDogTxPendingCnt[MGMTPIPEIDX] = 1;
	pMLMEContext->IRPPending = TRUE;
	pMLMEContext->bWaitingBulkOut = FALSE;
	RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[MGMTPIPEIDX], IrqFlags);

	// Increase Total transmit byte counter
	pAd->RalinkCounters.TransmittedByteCount +=  pMLMEContext->BulkOutSize;

	// Clear MLME bulk flag
	RTUSB_CLEAR_BULK_FLAG(pAd, fRTUSB_BULK_OUT_MLME);


	//DBGPRINT_RAW(RT_DEBUG_INFO, ("--->RTUSBBulkOutMLMEPacket\n"));
#if 0 // for debug
{
	printk("MLME-Out, C=%d!, D=%d, F=%d!\n", pAd->MgmtRing.TxCpuIdx, pAd->MgmtRing.TxDmaIdx, pAd->MgmtRing.TxSwFreeIdx);

	//TODO: Need to remove it when formal release
	PTXINFO_STRUC pTxInfo;

	pTxInfo = (PTXINFO_STRUC)pMLMEContext->TransferBuffer;
	if (pTxInfo->QSEL != FIFO_EDCA)
	{
			printk("%s(): ====> pTxInfo->QueueSel(%d)!= FIFO_EDCA!!!!\n", __func__, pTxInfo->QSEL);
			printk("\tMLME_Index=%d!\n", Index);
			hex_dump("Wrong QSel Pkt:", (PUCHAR)pMLMEContext->TransferBuffer, pTxInfo->USBDMATxPktLen);
	}
}
#endif

	// Init Tx context descriptor
	RTUSBInitTxDesc(pAd, pMLMEContext, MGMTPIPEIDX, (usb_complete_t)RTUSBBulkOutMLMEPacketComplete);

	//For mgmt urb buffer, because we use sk_buff, so we need to notify the USB controller do dma mapping.
	pUrb->transfer_dma	= 0;
	pUrb->transfer_flags &= (~URB_NO_TRANSFER_DMA_MAP);

	pUrb = pMLMEContext->pUrb;
	if((ret = RTUSB_SUBMIT_URB(pUrb))!=0)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("RTUSBBulkOutMLMEPacket: Submit MLME URB failed %d\n", ret));
		RTMP_IRQ_LOCK(&pAd->BulkOutLock[MGMTPIPEIDX], IrqFlags);
		pAd->BulkOutPending[MGMTPIPEIDX] = FALSE;
		pAd->watchDogTxPendingCnt[MGMTPIPEIDX] = 0;
		pMLMEContext->IRPPending = FALSE;
		pMLMEContext->bWaitingBulkOut = TRUE;
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[MGMTPIPEIDX], IrqFlags);

		return;
	}

	//DBGPRINT_RAW(RT_DEBUG_INFO, ("<---RTUSBBulkOutMLMEPacket \n"));
//	printk("<---RTUSBBulkOutMLMEPacket,Cpu=%d!, Dma=%d, SwIdx=%d!\n", pAd->MgmtRing.TxCpuIdx, pAd->MgmtRing.TxDmaIdx, pAd->MgmtRing.TxSwFreeIdx);
}


VOID RTUSBBulkOutMLMEPacketComplete(purbb_t pUrb, struct pt_regs *pt_regs)
{
	PTX_CONTEXT			pMLMEContext;
	PRTMP_ADAPTER		pAd;
	NTSTATUS			Status;
	POS_COOKIE 			pObj;
	int					index;
#if 0 // sample, IRQ LOCK
	unsigned long		IrqFlags;
	PNDIS_PACKET		pPacket;
#endif


	//DBGPRINT_RAW(RT_DEBUG_INFO, ("--->RTUSBBulkOutMLMEPacketComplete\n"));
	pMLMEContext	= (PTX_CONTEXT)pUrb->context;
	pAd 			= pMLMEContext->pAd;
	pObj 			= (POS_COOKIE)pAd->OS_Cookie;
	Status			= pUrb->status;
	index 			= pMLMEContext->SelfIdx;


#if 0 // sample, IRQ LOCK
	ASSERT((pAd->MgmtRing.TxDmaIdx == index));
	//printk("MLME-Done-B: C=%d, D=%d, F=%d, Self=%d!\n", pAd->MgmtRing.TxCpuIdx, pAd->MgmtRing.TxDmaIdx, pAd->MgmtRing.TxSwFreeIdx, pMLMEContext->SelfIdx);

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

#if 0
	//Bulk-Out fail status handle
	if (Status != USB_ST_NOERROR)
	{
		if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET)))
		{
			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Bulk Out MLME Failed, Status=%d!\n", Status));
			// TODO: How to handle about the MLMEBulkOut failed issue. Need to reset the endpoint?
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			pAd->bulkResetPipeid = (MGMTPIPEIDX | BULKOUT_MGMT_RESET_FLAG);
		}
	}
#endif

	//printk("MLME-Done-A: C=%d, D=%d, F=%d!\n", pAd->MgmtRing.TxCpuIdx, pAd->MgmtRing.TxDmaIdx, pAd->MgmtRing.TxSwFreeIdx);

	pObj->mgmt_dma_done_task.data = (unsigned long)pAd;
	tasklet_hi_schedule(&pObj->mgmt_dma_done_task);

	//DBGPRINT_RAW(RT_DEBUG_INFO, ("<---RTUSBBulkOutMLMEPacketComplete\n"));
//	printk("<---RTUSBBulkOutMLMEPacketComplete, Cpu=%d, Dma=%d, SwIdx=%d!\n",
//				pAd->MgmtRing.TxCpuIdx, pAd->MgmtRing.TxDmaIdx, pAd->MgmtRing.TxSwFreeIdx);

#else

	pObj->mgmt_dma_done_task.data = (unsigned long)pUrb;
	tasklet_hi_schedule(&pObj->mgmt_dma_done_task);
#endif
}


/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note: PsPoll use BulkOutPipeId = 0

	========================================================================
*/
VOID	RTUSBBulkOutPsPoll(
	IN	PRTMP_ADAPTER	pAd)
{
	PTX_CONTEXT		pPsPollContext = &(pAd->PsPollContext);
	PURB			pUrb;
	int				ret = 0;
	unsigned long	IrqFlags;

	RTMP_IRQ_LOCK(&pAd->BulkOutLock[0], IrqFlags);
	if ((pAd->BulkOutPending[0] == TRUE) || RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NEED_STOP_TX))
	{
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], IrqFlags);
		return;
	}
	pAd->BulkOutPending[0] = TRUE;
	pAd->watchDogTxPendingCnt[0] = 1;
	pPsPollContext->IRPPending = TRUE;
	RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], IrqFlags);


	// Clear PS-Poll bulk flag
	RTUSB_CLEAR_BULK_FLAG(pAd, fRTUSB_BULK_OUT_PSPOLL);

	// Init Tx context descriptor
	RTUSBInitTxDesc(pAd, pPsPollContext, MGMTPIPEIDX, (usb_complete_t)RTUSBBulkOutPsPollComplete);

	pUrb = pPsPollContext->pUrb;
	if((ret = RTUSB_SUBMIT_URB(pUrb))!=0)
	{
		RTMP_IRQ_LOCK(&pAd->BulkOutLock[0], IrqFlags);
		pAd->BulkOutPending[0] = FALSE;
		pAd->watchDogTxPendingCnt[0] = 0;
		pPsPollContext->IRPPending = FALSE;
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], IrqFlags);

		DBGPRINT(RT_DEBUG_ERROR, ("RTUSBBulkOutPsPoll: Submit Tx URB failed %d\n", ret));
		return;
	}

}

// PS-Poll frame use BulkOutPipeId = 0
VOID RTUSBBulkOutPsPollComplete(purbb_t pUrb,struct pt_regs *pt_regs)
{
	PRTMP_ADAPTER		pAd;
	PTX_CONTEXT			pPsPollContext;
	NTSTATUS			Status;
#if 0 // sample, IRQ LOCK
	unsigned long		IrqFlags;
#endif
	POS_COOKIE			pObj;


	pPsPollContext= (PTX_CONTEXT)pUrb->context;
	pAd = pPsPollContext->pAd;
	Status = pUrb->status;

#if 0 // sample, IRQ LOCK
	// Reset PsPoll context flags
	pPsPollContext->IRPPending	= FALSE;
	pPsPollContext->InUse		= FALSE;

	if (Status == USB_ST_NOERROR)
	{
		// Don't worry about the queue is empty or not, this function will check itself
		RTMPDeQueuePacket(pAd, TRUE, NUM_OF_TX_RING, MAX_TX_PROCESS);
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
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT, NULL, 0);
		}
	}

	RTMP_IRQ_LOCK(&pAd->BulkOutLock[0], IrqFlags);
	pAd->BulkOutPending[0] = FALSE;
	RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], IrqFlags);

	// Always call Bulk routine, even reset bulk.
	// The protectioon of rest bulk should be in BulkOut routine
	RTUSBKickBulkOut(pAd);
#else

	pObj = (POS_COOKIE) pAd->OS_Cookie;
	pObj->pspoll_frame_complete_task.data = (unsigned long)pUrb;
	tasklet_hi_schedule(&pObj->pspoll_frame_complete_task);
#endif
}


#if 0
/*
	========================================================================

	Routine Description:
	USB_RxPacket initializes a URB and uses the Rx IRP to submit it
	to USB. It checks if an Rx Descriptor is available and passes the
	the coresponding buffer to be filled. If no descriptor is available
	fails the request. When setting the completion routine we pass our
	Adapter Object as Context.

	Arguments:

	Return Value:
		TRUE			found matched tuple cache
		FALSE			no matched found

	Note:

	========================================================================
*/
VOID	RTUSBBulkReceive(
	IN	PRTMP_ADAPTER	pAd)
{
	PRX_CONTEXT		pRxContext;
	PURB			pUrb;
	int				ret = 0;
	unsigned long	IrqFlags;


	/* device had been closed */
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_REMOVE_IN_PROGRESS))
		return;

	RTMP_IRQ_LOCK(&pAd->BulkInLock, IrqFlags);

	// Last is time point between 2 separate URB.
	if (pAd->NextRxBulkInPosition == 0)
	{
		//pAd->NextRxBulkInIndex = (pAd->NextRxBulkInIndex + 1) % (RX_RING_SIZE);
		INC_RING_INDEX(pAd->NextRxBulkInIndex, RX_RING_SIZE);
	}
	else if ((pAd->NextRxBulkInPosition&0x1ff) != 0)
	{
		//pAd->NextRxBulkInIndex = (pAd->NextRxBulkInIndex + 1) % (RX_RING_SIZE);
		INC_RING_INDEX(pAd->NextRxBulkInIndex, RX_RING_SIZE);
		DBGPRINT_RAW(RT_DEBUG_TRACE, ("pAd->NextRxBulkInPosition = 0x%lx.  End of URB.\n", pAd->NextRxBulkInPosition ));
		pAd->NextRxBulkInPosition = 0;
	}

	if (pAd->NextRxBulkInPosition == MAX_RXBULK_SIZE)
		pAd->NextRxBulkInPosition = 0;

	pRxContext = &(pAd->RxContext[pAd->NextRxBulkInIndex]);

	// TODO: Why need to check if pRxContext->InUsed == TRUE?
	//if ((pRxContext->InUse == TRUE) || (pRxContext->Readable == TRUE))
	if ((pRxContext->InUse == FALSE) && (pRxContext->Readable == TRUE))
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE, ("pRxContext[%d] InUse = %d.pRxContext->Readable = %d.  Return.\n", pAd->NextRxBulkInIndex,pRxContext->InUse, pRxContext->Readable ));
		RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);

		// read RxContext, Since not
#ifdef CONFIG_STA_SUPPORT
		STARxDoneInterruptHandle(pAd, TRUE);
#endif // CONFIG_STA_SUPPORT //

		//return;
	}
	pRxContext->InUse = TRUE;
	pRxContext->IRPPending= TRUE;

	RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);

	// Init Rx context descriptor
	NdisZeroMemory(pRxContext->TransferBuffer, BUFFER_SIZE);
	RTUSBInitRxDesc(pAd, pRxContext);

	pUrb = pRxContext->pUrb;
	if ((ret = RTUSB_SUBMIT_URB(pUrb))!=0)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("RTUSBBulkReceive: Submit Rx URB failed %d\n", ret));
		return;
	}
	else // success
	{
		NdisInterlockedIncrement(&pAd->PendingRx);
		pAd->BulkInReq++;
	}

	// read RxContext, Since not
#ifdef CONFIG_STA_SUPPORT
	STARxDoneInterruptHandle(pAd, FALSE);
#endif // CONFIG_STA_SUPPORT //
}

/*
	========================================================================

	Routine Description:
		This routine process Rx Irp and call rx complete function.

	Arguments:
		DeviceObject	Pointer to the device object for next lower
						device. DeviceObject passed in here belongs to
						the next lower driver in the stack because we
						were invoked via IoCallDriver in USB_RxPacket
						AND it is not OUR device object
	  Irp				Ptr to completed IRP
	  Context			Ptr to our Adapter object (context specified
						in IoSetCompletionRoutine

	Return Value:
		Always returns STATUS_MORE_PROCESSING_REQUIRED

	Note:
		Always returns STATUS_MORE_PROCESSING_REQUIRED
	========================================================================
*/
VOID RTUSBBulkRxComplete(purbb_t pUrb, struct pt_regs *pt_regs)
{
#if 0
	PRX_CONTEXT			pRxContext;
	PRTMP_ADAPTER		pAd;
	NTSTATUS			Status;
//	POS_COOKIE			pObj;

	pRxContext	= (PRX_CONTEXT)pUrb->context;
	pAd 		= pRxContext->pAd;
//	pObj		= (POS_COOKIE) pAd->OS_Cookie;


	Status = pUrb->status;
	//pRxContext->pIrp = NULL;

	pRxContext->InUse = FALSE;
	pRxContext->IRPPending = FALSE;

	if (Status == USB_ST_NOERROR)
	{
		pAd->BulkInComplete++;
		pRxContext->Readable = TRUE;
		pAd->NextRxBulkInPosition = 0;

	}
	else	 // STATUS_OTHER
	{
		pAd->BulkInCompleteFail++;
		// Still read this packet although it may comtain wrong bytes.
		pRxContext->Readable = FALSE;
		// Parsing all packets. because after reset, the index will reset to all zero.

		if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKIN_RESET)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
		{

			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Bulk In Failed. Status = %d\n", Status));
			DBGPRINT_RAW(RT_DEBUG_ERROR, ("==>NextRxBulkInIndex=0x%x, NextRxBulkInReadIndex=0x%x, TransferBufferLength= 0x%x\n",
				pAd->NextRxBulkInIndex, pAd->NextRxBulkInReadIndex, pRxContext->pUrb->actual_length));

			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKIN_RESET);
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_IN, NULL, 0);
		}
		//pUrb = NULL;
	}

	if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
		(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKIN_RESET)) &&
//		(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET)) &&
		(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF)) &&
		(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) &&
		(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
	{
		RTUSBBulkReceive(pAd);
#if 0
#if 1
		STARxDoneInterruptHandle(pAd, FALSE);
#else
		pObj->rx_bh.data = (unsigned long)pUrb;
		tasklet_schedule(&pObj->rx_bh);
#endif
#endif
	}

	// Call RxPacket to process packet and return the status
	NdisInterlockedDecrement(&pAd->PendingRx);
#else


	// use a receive tasklet to handle received packets;
	// or sometimes hardware IRQ will be disabled here, so we can not
	// use spin_lock_bh()/spin_unlock_bh() after IRQ is disabled. :<
	PRX_CONTEXT		pRxContext;
	PRTMP_ADAPTER	pAd;
	POS_COOKIE 		pObj;


	pRxContext	= (PRX_CONTEXT)pUrb->context;
	pAd 		= pRxContext->pAd;
	pObj 		= (POS_COOKIE) pAd->OS_Cookie;

	pObj->rx_done_task.data = (unsigned long)pUrb;
	tasklet_hi_schedule(&pObj->rx_done_task);
#endif
}

#else

VOID DoBulkIn(IN RTMP_ADAPTER *pAd)
{
	PRX_CONTEXT		pRxContext;
	PURB			pUrb;
	int				ret = 0;
	unsigned long	IrqFlags;

	RTMP_IRQ_LOCK(&pAd->BulkInLock, IrqFlags);
	pRxContext = &(pAd->RxContext[pAd->NextRxBulkInIndex]);
	if ((pAd->PendingRx > 0) || (pRxContext->Readable == TRUE) || (pRxContext->InUse == TRUE))
	{
		RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);
		return;
	}
	pRxContext->InUse = TRUE;
	pRxContext->IRPPending = TRUE;
	pAd->PendingRx++;
	pAd->BulkInReq++;
	RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);

	// Init Rx context descriptor
	NdisZeroMemory(pRxContext->TransferBuffer, pRxContext->BulkInOffset);
	RTUSBInitRxDesc(pAd, pRxContext);

	pUrb = pRxContext->pUrb;
	if ((ret = RTUSB_SUBMIT_URB(pUrb))!=0)
	{	// fail

		RTMP_IRQ_LOCK(&pAd->BulkInLock, IrqFlags);
		pRxContext->InUse = FALSE;
		pRxContext->IRPPending = FALSE;
		pAd->PendingRx--;
		pAd->BulkInReq--;
		RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);
		DBGPRINT(RT_DEBUG_ERROR, ("RTUSBBulkReceive: Submit Rx URB failed %d\n", ret));
	}
	else
	{	// success
#if 0
		RTMP_IRQ_LOCK(&pAd->BulkInLock, IrqFlags);
		pRxContext->IRPPending = TRUE;
		//NdisInterlockedIncrement(&pAd->PendingRx);
		pAd->PendingRx++;
		RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);
		pAd->BulkInReq++;
#endif
		ASSERT((pRxContext->InUse == pRxContext->IRPPending));
		//printk("BIDone, Pend=%d,BIIdx=%d,BIRIdx=%d!\n", pAd->PendingRx, pAd->NextRxBulkInIndex, pAd->NextRxBulkInReadIndex);
	}
}


/*
	========================================================================

	Routine Description:
	USB_RxPacket initializes a URB and uses the Rx IRP to submit it
	to USB. It checks if an Rx Descriptor is available and passes the
	the coresponding buffer to be filled. If no descriptor is available
	fails the request. When setting the completion routine we pass our
	Adapter Object as Context.

	Arguments:

	Return Value:
		TRUE			found matched tuple cache
		FALSE			no matched found

	Note:

	========================================================================
*/
#define fRTMP_ADAPTER_NEED_STOP_RX		\
		(fRTMP_ADAPTER_NIC_NOT_EXIST | fRTMP_ADAPTER_HALT_IN_PROGRESS |	\
		 fRTMP_ADAPTER_RADIO_OFF | fRTMP_ADAPTER_RESET_IN_PROGRESS | \
		 fRTMP_ADAPTER_REMOVE_IN_PROGRESS | fRTMP_ADAPTER_BULKIN_RESET)

#define fRTMP_ADAPTER_NEED_STOP_HANDLE_RX	\
		(fRTMP_ADAPTER_NIC_NOT_EXIST | fRTMP_ADAPTER_HALT_IN_PROGRESS |	\
		 fRTMP_ADAPTER_RADIO_OFF | fRTMP_ADAPTER_RESET_IN_PROGRESS | \
		 fRTMP_ADAPTER_REMOVE_IN_PROGRESS)

VOID	RTUSBBulkReceive(
	IN	PRTMP_ADAPTER	pAd)
{
	PRX_CONTEXT		pRxContext;
	unsigned long	IrqFlags;


	/* sanity check */
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NEED_STOP_HANDLE_RX))
		return;

	while(1)
	{

		RTMP_IRQ_LOCK(&pAd->BulkInLock, IrqFlags);
		pRxContext = &(pAd->RxContext[pAd->NextRxBulkInReadIndex]);
		if (((pRxContext->InUse == FALSE) && (pRxContext->Readable == TRUE)) &&
			(pRxContext->bRxHandling == FALSE))
		{
			pRxContext->bRxHandling = TRUE;
			RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);

			// read RxContext, Since not
#ifdef CONFIG_STA_SUPPORT
			IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
				STARxDoneInterruptHandle(pAd, TRUE);
#endif // CONFIG_STA_SUPPORT //

			// Finish to handle this bulkIn buffer.
			RTMP_IRQ_LOCK(&pAd->BulkInLock, IrqFlags);
			pRxContext->BulkInOffset = 0;
			pRxContext->Readable = FALSE;
			pRxContext->bRxHandling = FALSE;
			pAd->ReadPosition = 0;
			pAd->TransferBufferLength = 0;
			INC_RING_INDEX(pAd->NextRxBulkInReadIndex, RX_RING_SIZE);
			RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);

		}
		else
		{
			RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);
			break;
		}
	}

	if (!(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NEED_STOP_RX)))
		DoBulkIn(pAd);

}


/*
	========================================================================

	Routine Description:
		This routine process Rx Irp and call rx complete function.

	Arguments:
		DeviceObject	Pointer to the device object for next lower
						device. DeviceObject passed in here belongs to
						the next lower driver in the stack because we
						were invoked via IoCallDriver in USB_RxPacket
						AND it is not OUR device object
	  Irp				Ptr to completed IRP
	  Context			Ptr to our Adapter object (context specified
						in IoSetCompletionRoutine

	Return Value:
		Always returns STATUS_MORE_PROCESSING_REQUIRED

	Note:
		Always returns STATUS_MORE_PROCESSING_REQUIRED
	========================================================================
*/
VOID RTUSBBulkRxComplete(purbb_t pUrb, struct pt_regs *pt_regs)
{
	// use a receive tasklet to handle received packets;
	// or sometimes hardware IRQ will be disabled here, so we can not
	// use spin_lock_bh()/spin_unlock_bh() after IRQ is disabled. :<
	PRX_CONTEXT		pRxContext;
	PRTMP_ADAPTER	pAd;
	POS_COOKIE 		pObj;


	pRxContext	= (PRX_CONTEXT)pUrb->context;
	pAd 		= pRxContext->pAd;
	pObj 		= (POS_COOKIE) pAd->OS_Cookie;

	pObj->rx_done_task.data = (unsigned long)pUrb;
	tasklet_hi_schedule(&pObj->rx_done_task);

}

#endif



/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

	========================================================================
*/
VOID	RTUSBKickBulkOut(
	IN	PRTMP_ADAPTER pAd)
{
	// BulkIn Reset will reset whole USB PHY. So we need to make sure fRTMP_ADAPTER_BULKIN_RESET not flaged.
	if (!RTMP_TEST_FLAG(pAd ,fRTMP_ADAPTER_NEED_STOP_TX)
		)
	{
#if 0	// not used now in RT28xx, but may used latter.
		// 1. Data Fragment has highest priority
		if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_FRAG))
		{
			if (((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) ||
				(!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
				))
			{
				RTUSBBulkOutDataPacket(pAd, 0, pAd->NextBulkOutIndex[0]);
			}
		}
		if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_FRAG_2))
		{
			if (((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) ||
				(!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
				))
			{
				RTUSBBulkOutDataPacket(pAd, 1, pAd->NextBulkOutIndex[1]);
			}
		}
		if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_FRAG_3))
		{
			if (((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) ||
				(!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
				))
			{
				RTUSBBulkOutDataPacket(pAd, 2, pAd->NextBulkOutIndex[2]);
			}
		}
		if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_FRAG_4))
		{
			if (((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) ||
				(!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
				))
			{
				RTUSBBulkOutDataPacket(pAd, 3, pAd->NextBulkOutIndex[3]);
			}
		}
#endif

		// 2. PS-Poll frame is next
		if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_PSPOLL))
		{
			RTUSBBulkOutPsPoll(pAd);
		}

		// 5. Mlme frame is next
		else if ((RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_MLME)) &&
				 (pAd->MgmtRing.TxSwFreeIdx < MGMT_RING_SIZE))
		{
			RTUSBBulkOutMLMEPacket(pAd, pAd->MgmtRing.TxDmaIdx);
		}

		// 6. Data frame normal is next
		if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL))
		{
			if (((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) ||
				(!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
				))
			{
				RTUSBBulkOutDataPacket(pAd, 0, pAd->NextBulkOutIndex[0]);
			}
		}
		if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL_2))
		{
			if (((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) ||
				(!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
				))
			{
				RTUSBBulkOutDataPacket(pAd, 1, pAd->NextBulkOutIndex[1]);
			}
		}
		if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL_3))
		{
			if (((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) ||
				(!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
				))
			{
				RTUSBBulkOutDataPacket(pAd, 2, pAd->NextBulkOutIndex[2]);
			}
		}
		if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL_4))
		{
			if (((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) ||
				(!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
				))
			{
				RTUSBBulkOutDataPacket(pAd, 3, pAd->NextBulkOutIndex[3]);
			}
		}

		// 7. Null frame is the last
		else if (RTUSB_TEST_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NULL))
		{
			if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
			{
				RTUSBBulkOutNullFrame(pAd);
			}
		}

		// 8. No data avaliable
		else
		{

		}
	}
}

/*
	========================================================================

	Routine Description:
	Call from Reset action after BulkOut failed.
	Arguments:

	Return Value:

	Note:

	========================================================================
*/
VOID	RTUSBCleanUpDataBulkOutQueue(
	IN	PRTMP_ADAPTER	pAd)
{
	UCHAR			Idx;
	PHT_TX_CONTEXT	pTxContext;

	DBGPRINT(RT_DEBUG_TRACE, ("--->CleanUpDataBulkOutQueue\n"));

	for (Idx = 0; Idx < 4; Idx++)
	{
		pTxContext = &pAd->TxContext[Idx];

		pTxContext->CurWritePosition = pTxContext->NextBulkOutPosition;
		pTxContext->LastOne = FALSE;
		NdisAcquireSpinLock(&pAd->BulkOutLock[Idx]);
		pAd->BulkOutPending[Idx] = FALSE;
		NdisReleaseSpinLock(&pAd->BulkOutLock[Idx]);
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<---CleanUpDataBulkOutQueue\n"));
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

	========================================================================
*/
VOID	RTUSBCleanUpMLMEBulkOutQueue(
	IN	PRTMP_ADAPTER	pAd)
{
	DBGPRINT(RT_DEBUG_TRACE, ("--->CleanUpMLMEBulkOutQueue\n"));

#if 0	// Do nothing!
	NdisAcquireSpinLock(&pAd->MLMEBulkOutLock);
	while (pAd->PrioRingTxCnt > 0)
	{
		pAd->MLMEContext[pAd->PrioRingFirstIndex].InUse = FALSE;

		pAd->PrioRingFirstIndex++;
		if (pAd->PrioRingFirstIndex >= MGMT_RING_SIZE)
		{
			pAd->PrioRingFirstIndex = 0;
		}

		pAd->PrioRingTxCnt--;
	}
	NdisReleaseSpinLock(&pAd->MLMEBulkOutLock);
#endif

	DBGPRINT(RT_DEBUG_TRACE, ("<---CleanUpMLMEBulkOutQueue\n"));
}


/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:


	Note:

	========================================================================
*/
VOID	RTUSBCancelPendingIRPs(
	IN	PRTMP_ADAPTER	pAd)
{
	RTUSBCancelPendingBulkInIRP(pAd);
	RTUSBCancelPendingBulkOutIRP(pAd);
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

	========================================================================
*/
VOID	RTUSBCancelPendingBulkInIRP(
	IN	PRTMP_ADAPTER	pAd)
{
	PRX_CONTEXT		pRxContext;
	UINT			i;

	DBGPRINT_RAW(RT_DEBUG_TRACE, ("--->RTUSBCancelPendingBulkInIRP\n"));
	for ( i = 0; i < (RX_RING_SIZE); i++)
	{
		pRxContext = &(pAd->RxContext[i]);
		if(pRxContext->IRPPending == TRUE)
		{
			RTUSB_UNLINK_URB(pRxContext->pUrb);
			pRxContext->IRPPending = FALSE;
			pRxContext->InUse = FALSE;
			//NdisInterlockedDecrement(&pAd->PendingRx);
			//pAd->PendingRx--;
		}
	}
	DBGPRINT_RAW(RT_DEBUG_TRACE, ("<---RTUSBCancelPendingBulkInIRP\n"));
}


/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

	========================================================================
*/
VOID	RTUSBCancelPendingBulkOutIRP(
	IN	PRTMP_ADAPTER	pAd)
{
	PHT_TX_CONTEXT		pHTTXContext;
	PTX_CONTEXT			pMLMEContext;
	PTX_CONTEXT			pBeaconContext;
	PTX_CONTEXT			pNullContext;
	PTX_CONTEXT			pPsPollContext;
	PTX_CONTEXT			pRTSContext;
	UINT				i, Idx;
//	unsigned int 		IrqFlags;
//	NDIS_SPIN_LOCK		*pLock;
//	BOOLEAN				*pPending;


//	pLock = &pAd->BulkOutLock[MGMTPIPEIDX];
//	pPending = &pAd->BulkOutPending[MGMTPIPEIDX];

	for (Idx = 0; Idx < 4; Idx++)
	{
		pHTTXContext = &(pAd->TxContext[Idx]);

		if (pHTTXContext->IRPPending == TRUE)
		{

			// Get the USB_CONTEXT and cancel it's IRP; the completion routine will itself
			// remove it from the HeadPendingSendList and NULL out HeadPendingSendList
			//	when the last IRP on the list has been	cancelled; that's how we exit this loop
			//

			RTUSB_UNLINK_URB(pHTTXContext->pUrb);

			// Sleep 200 microseconds to give cancellation time to work
			RTMPusecDelay(200);
		}

		pAd->BulkOutPending[Idx] = FALSE;
	}

	//RTMP_IRQ_LOCK(pLock, IrqFlags);
	for (i = 0; i < MGMT_RING_SIZE; i++)
	{
		pMLMEContext = (PTX_CONTEXT)pAd->MgmtRing.Cell[i].AllocVa;
		if(pMLMEContext && (pMLMEContext->IRPPending == TRUE))
		{

			// Get the USB_CONTEXT and cancel it's IRP; the completion routine will itself
			// remove it from the HeadPendingSendList and NULL out HeadPendingSendList
			//	when the last IRP on the list has been	cancelled; that's how we exit this loop
			//

			RTUSB_UNLINK_URB(pMLMEContext->pUrb);
			pMLMEContext->IRPPending = FALSE;

			// Sleep 200 microsecs to give cancellation time to work
			RTMPusecDelay(200);
		}
	}
	pAd->BulkOutPending[MGMTPIPEIDX] = FALSE;
	//RTMP_IRQ_UNLOCK(pLock, IrqFlags);


	for (i = 0; i < BEACON_RING_SIZE; i++)
	{
		pBeaconContext = &(pAd->BeaconContext[i]);

		if(pBeaconContext->IRPPending == TRUE)
		{

			// Get the USB_CONTEXT and cancel it's IRP; the completion routine will itself
			// remove it from the HeadPendingSendList and NULL out HeadPendingSendList
			//	when the last IRP on the list has been	cancelled; that's how we exit this loop
			//

			RTUSB_UNLINK_URB(pBeaconContext->pUrb);

			// Sleep 200 microsecs to give cancellation time to work
			RTMPusecDelay(200);
		}
	}

	pNullContext = &(pAd->NullContext);
	if (pNullContext->IRPPending == TRUE)
		RTUSB_UNLINK_URB(pNullContext->pUrb);

	pRTSContext = &(pAd->RTSContext);
	if (pRTSContext->IRPPending == TRUE)
		RTUSB_UNLINK_URB(pRTSContext->pUrb);

	pPsPollContext = &(pAd->PsPollContext);
	if (pPsPollContext->IRPPending == TRUE)
		RTUSB_UNLINK_URB(pPsPollContext->pUrb);

	for (Idx = 0; Idx < 4; Idx++)
	{
		NdisAcquireSpinLock(&pAd->BulkOutLock[Idx]);
		pAd->BulkOutPending[Idx] = FALSE;
		NdisReleaseSpinLock(&pAd->BulkOutLock[Idx]);
	}
}

