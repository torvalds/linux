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

/*
   All functions in this file must be USB-depended, or you should out your function
	in other files.

*/

#ifdef RTMP_MAC_USB

#include	"../rt_config.h"

/*
	We can do copy the frame into pTxContext when match following conditions.
		=>
		=>
		=>
*/
static inline int RtmpUSBCanDoWrite(struct rt_rtmp_adapter *pAd,
					    u8 QueIdx,
					    struct rt_ht_tx_context *pHTTXContext)
{
	int canWrite = NDIS_STATUS_RESOURCES;

	if (((pHTTXContext->CurWritePosition) <
	     pHTTXContext->NextBulkOutPosition)
	    && (pHTTXContext->CurWritePosition + LOCAL_TXBUF_SIZE) >
	    pHTTXContext->NextBulkOutPosition) {
		DBGPRINT(RT_DEBUG_ERROR, ("RtmpUSBCanDoWrite c1!\n"));
		RTUSB_SET_BULK_FLAG(pAd,
				    (fRTUSB_BULK_OUT_DATA_NORMAL << QueIdx));
	} else if ((pHTTXContext->CurWritePosition == 8)
		   && (pHTTXContext->NextBulkOutPosition < LOCAL_TXBUF_SIZE)) {
		DBGPRINT(RT_DEBUG_ERROR, ("RtmpUSBCanDoWrite c2!\n"));
		RTUSB_SET_BULK_FLAG(pAd,
				    (fRTUSB_BULK_OUT_DATA_NORMAL << QueIdx));
	} else if (pHTTXContext->bCurWriting == TRUE) {
		DBGPRINT(RT_DEBUG_ERROR, ("RtmpUSBCanDoWrite c3!\n"));
	} else {
		canWrite = NDIS_STATUS_SUCCESS;
	}

	return canWrite;
}

u16 RtmpUSB_WriteSubTxResource(struct rt_rtmp_adapter *pAd,
				  struct rt_tx_blk *pTxBlk,
				  IN BOOLEAN bIsLast, u16 * FreeNumber)
{

	/* Dummy function. Should be removed in the future. */
	return 0;

}

u16 RtmpUSB_WriteFragTxResource(struct rt_rtmp_adapter *pAd,
				   struct rt_tx_blk *pTxBlk,
				   u8 fragNum, u16 * FreeNumber)
{
	struct rt_ht_tx_context *pHTTXContext;
	u16 hwHdrLen;	/* The hwHdrLen consist of 802.11 header length plus the header padding length. */
	u32 fillOffset;
	struct rt_txinfo *pTxInfo;
	struct rt_txwi *pTxWI;
	u8 *pWirelessPacket = NULL;
	u8 QueIdx;
	int Status;
	unsigned long IrqFlags;
	u32 USBDMApktLen = 0, DMAHdrLen, padding;
	BOOLEAN TxQLastRound = FALSE;

	/* */
	/* get Tx Ring Resource & Dma Buffer address */
	/* */
	QueIdx = pTxBlk->QueIdx;
	pHTTXContext = &pAd->TxContext[QueIdx];

	RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[QueIdx], IrqFlags);

	pHTTXContext = &pAd->TxContext[QueIdx];
	fillOffset = pHTTXContext->CurWritePosition;

	if (fragNum == 0) {
		/* Check if we have enough space for this bulk-out batch. */
		Status = RtmpUSBCanDoWrite(pAd, QueIdx, pHTTXContext);
		if (Status == NDIS_STATUS_SUCCESS) {
			pHTTXContext->bCurWriting = TRUE;

			/* Reserve space for 8 bytes padding. */
			if ((pHTTXContext->ENextBulkOutPosition ==
			     pHTTXContext->CurWritePosition)) {
				pHTTXContext->ENextBulkOutPosition += 8;
				pHTTXContext->CurWritePosition += 8;
				fillOffset += 8;
			}
			pTxBlk->Priv = 0;
			pHTTXContext->CurWriteRealPos =
			    pHTTXContext->CurWritePosition;
		} else {
			RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[QueIdx],
					IrqFlags);

			RELEASE_NDIS_PACKET(pAd, pTxBlk->pPacket,
					    NDIS_STATUS_FAILURE);
			return (Status);
		}
	} else {
		/* For sub-sequent frames of this bulk-out batch. Just copy it to our bulk-out buffer. */
		Status =
		    ((pHTTXContext->bCurWriting ==
		      TRUE) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE);
		if (Status == NDIS_STATUS_SUCCESS) {
			fillOffset += pTxBlk->Priv;
		} else {
			RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[QueIdx],
					IrqFlags);

			RELEASE_NDIS_PACKET(pAd, pTxBlk->pPacket,
					    NDIS_STATUS_FAILURE);
			return (Status);
		}
	}

	NdisZeroMemory((u8 *)(&pTxBlk->HeaderBuf[0]), TXINFO_SIZE);
	pTxInfo = (struct rt_txinfo *)(&pTxBlk->HeaderBuf[0]);
	pTxWI = (struct rt_txwi *) (&pTxBlk->HeaderBuf[TXINFO_SIZE]);

	pWirelessPacket =
	    &pHTTXContext->TransferBuffer->field.WirelessPacket[fillOffset];

	/* copy TXWI + WLAN Header + LLC into DMA Header Buffer */
	/*hwHdrLen = ROUND_UP(pTxBlk->MpduHeaderLen, 4); */
	hwHdrLen = pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;

	/* Build our URB for USBD */
	DMAHdrLen = TXWI_SIZE + hwHdrLen;
	USBDMApktLen = DMAHdrLen + pTxBlk->SrcBufLen;
	padding = (4 - (USBDMApktLen % 4)) & 0x03;	/* round up to 4 byte alignment */
	USBDMApktLen += padding;

	pTxBlk->Priv += (TXINFO_SIZE + USBDMApktLen);

	/* For TxInfo, the length of USBDMApktLen = TXWI_SIZE + 802.11 header + payload */
	RTMPWriteTxInfo(pAd, pTxInfo, (u16)(USBDMApktLen), FALSE, FIFO_EDCA,
			FALSE /*NextValid */ , FALSE);

	if (fragNum == pTxBlk->TotalFragNum) {
		pTxInfo->USBDMATxburst = 0;
		if ((pHTTXContext->CurWritePosition + pTxBlk->Priv + 3906) >
		    MAX_TXBULK_LIMIT) {
			pTxInfo->SwUseLastRound = 1;
			TxQLastRound = TRUE;
		}
	} else {
		pTxInfo->USBDMATxburst = 1;
	}

	NdisMoveMemory(pWirelessPacket, pTxBlk->HeaderBuf,
		       TXINFO_SIZE + TXWI_SIZE + hwHdrLen);
	pWirelessPacket += (TXINFO_SIZE + TXWI_SIZE + hwHdrLen);
	pHTTXContext->CurWriteRealPos += (TXINFO_SIZE + TXWI_SIZE + hwHdrLen);

	RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[QueIdx], IrqFlags);

	NdisMoveMemory(pWirelessPacket, pTxBlk->pSrcBufData, pTxBlk->SrcBufLen);

	/*      Zero the last padding. */
	pWirelessPacket += pTxBlk->SrcBufLen;
	NdisZeroMemory(pWirelessPacket, padding + 8);

	if (fragNum == pTxBlk->TotalFragNum) {
		RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[QueIdx], IrqFlags);

		/* Update the pHTTXContext->CurWritePosition. 3906 used to prevent the NextBulkOut is a A-RALINK/A-MSDU Frame. */
		pHTTXContext->CurWritePosition += pTxBlk->Priv;
		if (TxQLastRound == TRUE)
			pHTTXContext->CurWritePosition = 8;
		pHTTXContext->CurWriteRealPos = pHTTXContext->CurWritePosition;

		/* Finally, set bCurWriting as FALSE */
		pHTTXContext->bCurWriting = FALSE;

		RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[QueIdx], IrqFlags);

		/* succeed and release the skb buffer */
		RELEASE_NDIS_PACKET(pAd, pTxBlk->pPacket, NDIS_STATUS_SUCCESS);
	}

	return (Status);

}

u16 RtmpUSB_WriteSingleTxResource(struct rt_rtmp_adapter *pAd,
				     struct rt_tx_blk *pTxBlk,
				     IN BOOLEAN bIsLast,
				     u16 * FreeNumber)
{
	struct rt_ht_tx_context *pHTTXContext;
	u16 hwHdrLen;
	u32 fillOffset;
	struct rt_txinfo *pTxInfo;
	struct rt_txwi *pTxWI;
	u8 *pWirelessPacket;
	u8 QueIdx;
	unsigned long IrqFlags;
	int Status;
	u32 USBDMApktLen = 0, DMAHdrLen, padding;
	BOOLEAN bTxQLastRound = FALSE;

	/* For USB, didn't need PCI_MAP_SINGLE() */
	/*SrcBufPA = PCI_MAP_SINGLE(pAd, (char *) pTxBlk->pSrcBufData, pTxBlk->SrcBufLen, PCI_DMA_TODEVICE); */

	/* */
	/* get Tx Ring Resource & Dma Buffer address */
	/* */
	QueIdx = pTxBlk->QueIdx;

	RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[QueIdx], IrqFlags);
	pHTTXContext = &pAd->TxContext[QueIdx];
	fillOffset = pHTTXContext->CurWritePosition;

	/* Check ring full. */
	Status = RtmpUSBCanDoWrite(pAd, QueIdx, pHTTXContext);
	if (Status == NDIS_STATUS_SUCCESS) {
		pHTTXContext->bCurWriting = TRUE;

		pTxInfo = (struct rt_txinfo *)(&pTxBlk->HeaderBuf[0]);
		pTxWI = (struct rt_txwi *) (&pTxBlk->HeaderBuf[TXINFO_SIZE]);

		/* Reserve space for 8 bytes padding. */
		if ((pHTTXContext->ENextBulkOutPosition ==
		     pHTTXContext->CurWritePosition)) {
			pHTTXContext->ENextBulkOutPosition += 8;
			pHTTXContext->CurWritePosition += 8;
			fillOffset += 8;
		}
		pHTTXContext->CurWriteRealPos = pHTTXContext->CurWritePosition;

		pWirelessPacket =
		    &pHTTXContext->TransferBuffer->field.
		    WirelessPacket[fillOffset];

		/* copy TXWI + WLAN Header + LLC into DMA Header Buffer */
		/*hwHdrLen = ROUND_UP(pTxBlk->MpduHeaderLen, 4); */
		hwHdrLen = pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;

		/* Build our URB for USBD */
		DMAHdrLen = TXWI_SIZE + hwHdrLen;
		USBDMApktLen = DMAHdrLen + pTxBlk->SrcBufLen;
		padding = (4 - (USBDMApktLen % 4)) & 0x03;	/* round up to 4 byte alignment */
		USBDMApktLen += padding;

		pTxBlk->Priv = (TXINFO_SIZE + USBDMApktLen);

		/* For TxInfo, the length of USBDMApktLen = TXWI_SIZE + 802.11 header + payload */
		RTMPWriteTxInfo(pAd, pTxInfo, (u16)(USBDMApktLen), FALSE,
				FIFO_EDCA, FALSE /*NextValid */ , FALSE);

		if ((pHTTXContext->CurWritePosition + 3906 + pTxBlk->Priv) >
		    MAX_TXBULK_LIMIT) {
			pTxInfo->SwUseLastRound = 1;
			bTxQLastRound = TRUE;
		}
		NdisMoveMemory(pWirelessPacket, pTxBlk->HeaderBuf,
			       TXINFO_SIZE + TXWI_SIZE + hwHdrLen);
		pWirelessPacket += (TXINFO_SIZE + TXWI_SIZE + hwHdrLen);

		/* We unlock it here to prevent the first 8 bytes maybe over-writed issue. */
		/*      1. First we got CurWritePosition but the first 8 bytes still not write to the pTxcontext. */
		/*      2. An interrupt break our routine and handle bulk-out complete. */
		/*      3. In the bulk-out compllete, it need to do another bulk-out, */
		/*                      if the ENextBulkOutPosition is just the same as CurWritePosition, it will save the first 8 bytes from CurWritePosition, */
		/*                      but the payload still not copyed. the pTxContext->SavedPad[] will save as allzero. and set the bCopyPad = TRUE. */
		/*      4. Interrupt complete. */
		/*  5. Our interrupted routine go back and fill the first 8 bytes to pTxContext. */
		/*      6. Next time when do bulk-out, it found the bCopyPad==TRUE and will copy the SavedPad[] to pTxContext->NextBulkOutPosition. */
		/*              and the packet will wrong. */
		pHTTXContext->CurWriteRealPos +=
		    (TXINFO_SIZE + TXWI_SIZE + hwHdrLen);
		RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[QueIdx], IrqFlags);

		NdisMoveMemory(pWirelessPacket, pTxBlk->pSrcBufData,
			       pTxBlk->SrcBufLen);
		pWirelessPacket += pTxBlk->SrcBufLen;
		NdisZeroMemory(pWirelessPacket, padding + 8);

		RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[QueIdx], IrqFlags);

		pHTTXContext->CurWritePosition += pTxBlk->Priv;
		if (bTxQLastRound)
			pHTTXContext->CurWritePosition = 8;
		pHTTXContext->CurWriteRealPos = pHTTXContext->CurWritePosition;

		pHTTXContext->bCurWriting = FALSE;
	}

	RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[QueIdx], IrqFlags);

	/* succeed and release the skb buffer */
	RELEASE_NDIS_PACKET(pAd, pTxBlk->pPacket, NDIS_STATUS_SUCCESS);

	return (Status);

}

u16 RtmpUSB_WriteMultiTxResource(struct rt_rtmp_adapter *pAd,
				    struct rt_tx_blk *pTxBlk,
				    u8 frameNum, u16 * FreeNumber)
{
	struct rt_ht_tx_context *pHTTXContext;
	u16 hwHdrLen;	/* The hwHdrLen consist of 802.11 header length plus the header padding length. */
	u32 fillOffset;
	struct rt_txinfo *pTxInfo;
	struct rt_txwi *pTxWI;
	u8 *pWirelessPacket = NULL;
	u8 QueIdx;
	int Status;
	unsigned long IrqFlags;
	/*u32                        USBDMApktLen = 0, DMAHdrLen, padding; */

	/* */
	/* get Tx Ring Resource & Dma Buffer address */
	/* */
	QueIdx = pTxBlk->QueIdx;
	pHTTXContext = &pAd->TxContext[QueIdx];

	RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[QueIdx], IrqFlags);

	if (frameNum == 0) {
		/* Check if we have enough space for this bulk-out batch. */
		Status = RtmpUSBCanDoWrite(pAd, QueIdx, pHTTXContext);
		if (Status == NDIS_STATUS_SUCCESS) {
			pHTTXContext->bCurWriting = TRUE;

			pTxInfo = (struct rt_txinfo *)(&pTxBlk->HeaderBuf[0]);
			pTxWI = (struct rt_txwi *) (&pTxBlk->HeaderBuf[TXINFO_SIZE]);

			/* Reserve space for 8 bytes padding. */
			if ((pHTTXContext->ENextBulkOutPosition ==
			     pHTTXContext->CurWritePosition)) {

				pHTTXContext->CurWritePosition += 8;
				pHTTXContext->ENextBulkOutPosition += 8;
			}
			fillOffset = pHTTXContext->CurWritePosition;
			pHTTXContext->CurWriteRealPos =
			    pHTTXContext->CurWritePosition;

			pWirelessPacket =
			    &pHTTXContext->TransferBuffer->field.
			    WirelessPacket[fillOffset];

			/* */
			/* Copy TXINFO + TXWI + WLAN Header + LLC into DMA Header Buffer */
			/* */
			if (pTxBlk->TxFrameType == TX_AMSDU_FRAME)
				/*hwHdrLen = ROUND_UP(pTxBlk->MpduHeaderLen-LENGTH_AMSDU_SUBFRAMEHEAD, 4)+LENGTH_AMSDU_SUBFRAMEHEAD; */
				hwHdrLen =
				    pTxBlk->MpduHeaderLen -
				    LENGTH_AMSDU_SUBFRAMEHEAD +
				    pTxBlk->HdrPadLen +
				    LENGTH_AMSDU_SUBFRAMEHEAD;
			else if (pTxBlk->TxFrameType == TX_RALINK_FRAME)
				/*hwHdrLen = ROUND_UP(pTxBlk->MpduHeaderLen-LENGTH_ARALINK_HEADER_FIELD, 4)+LENGTH_ARALINK_HEADER_FIELD; */
				hwHdrLen =
				    pTxBlk->MpduHeaderLen -
				    LENGTH_ARALINK_HEADER_FIELD +
				    pTxBlk->HdrPadLen +
				    LENGTH_ARALINK_HEADER_FIELD;
			else
				/*hwHdrLen = ROUND_UP(pTxBlk->MpduHeaderLen, 4); */
				hwHdrLen =
				    pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;

			/* Update the pTxBlk->Priv. */
			pTxBlk->Priv = TXINFO_SIZE + TXWI_SIZE + hwHdrLen;

			/*      pTxInfo->USBDMApktLen now just a temp value and will to correct latter. */
			RTMPWriteTxInfo(pAd, pTxInfo, (u16)(pTxBlk->Priv),
					FALSE, FIFO_EDCA, FALSE /*NextValid */ ,
					FALSE);

			/* Copy it. */
			NdisMoveMemory(pWirelessPacket, pTxBlk->HeaderBuf,
				       pTxBlk->Priv);
			pHTTXContext->CurWriteRealPos += pTxBlk->Priv;
			pWirelessPacket += pTxBlk->Priv;
		}
	} else {		/* For sub-sequent frames of this bulk-out batch. Just copy it to our bulk-out buffer. */

		Status =
		    ((pHTTXContext->bCurWriting ==
		      TRUE) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE);
		if (Status == NDIS_STATUS_SUCCESS) {
			fillOffset =
			    (pHTTXContext->CurWritePosition + pTxBlk->Priv);
			pWirelessPacket =
			    &pHTTXContext->TransferBuffer->field.
			    WirelessPacket[fillOffset];

			/*hwHdrLen = pTxBlk->MpduHeaderLen; */
			NdisMoveMemory(pWirelessPacket, pTxBlk->HeaderBuf,
				       pTxBlk->MpduHeaderLen);
			pWirelessPacket += (pTxBlk->MpduHeaderLen);
			pTxBlk->Priv += pTxBlk->MpduHeaderLen;
		} else {	/* It should not happened now unless we are going to shutdown. */
			DBGPRINT(RT_DEBUG_ERROR,
				 ("WriteMultiTxResource():bCurWriting is FALSE when handle sub-sequent frames.\n"));
			Status = NDIS_STATUS_FAILURE;
		}
	}

	/* We unlock it here to prevent the first 8 bytes maybe over-write issue. */
	/*      1. First we got CurWritePosition but the first 8 bytes still not write to the pTxContext. */
	/*      2. An interrupt break our routine and handle bulk-out complete. */
	/*      3. In the bulk-out compllete, it need to do another bulk-out, */
	/*                      if the ENextBulkOutPosition is just the same as CurWritePosition, it will save the first 8 bytes from CurWritePosition, */
	/*                      but the payload still not copyed. the pTxContext->SavedPad[] will save as allzero. and set the bCopyPad = TRUE. */
	/*      4. Interrupt complete. */
	/*  5. Our interrupted routine go back and fill the first 8 bytes to pTxContext. */
	/*      6. Next time when do bulk-out, it found the bCopyPad==TRUE and will copy the SavedPad[] to pTxContext->NextBulkOutPosition. */
	/*              and the packet will wrong. */
	RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[QueIdx], IrqFlags);

	if (Status != NDIS_STATUS_SUCCESS) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("WriteMultiTxResource: CWPos = %ld, NBOutPos = %ld.\n",
			  pHTTXContext->CurWritePosition,
			  pHTTXContext->NextBulkOutPosition));
		goto done;
	}
	/* Copy the frame content into DMA buffer and update the pTxBlk->Priv */
	NdisMoveMemory(pWirelessPacket, pTxBlk->pSrcBufData, pTxBlk->SrcBufLen);
	pWirelessPacket += pTxBlk->SrcBufLen;
	pTxBlk->Priv += pTxBlk->SrcBufLen;

done:
	/* Release the skb buffer here */
	RELEASE_NDIS_PACKET(pAd, pTxBlk->pPacket, NDIS_STATUS_SUCCESS);

	return (Status);

}

void RtmpUSB_FinalWriteTxResource(struct rt_rtmp_adapter *pAd,
				  struct rt_tx_blk *pTxBlk,
				  u16 totalMPDUSize, u16 TxIdx)
{
	u8 QueIdx;
	struct rt_ht_tx_context *pHTTXContext;
	u32 fillOffset;
	struct rt_txinfo *pTxInfo;
	struct rt_txwi *pTxWI;
	u32 USBDMApktLen, padding;
	unsigned long IrqFlags;
	u8 *pWirelessPacket;

	QueIdx = pTxBlk->QueIdx;
	pHTTXContext = &pAd->TxContext[QueIdx];

	RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[QueIdx], IrqFlags);

	if (pHTTXContext->bCurWriting == TRUE) {
		fillOffset = pHTTXContext->CurWritePosition;
		if (((pHTTXContext->ENextBulkOutPosition ==
		      pHTTXContext->CurWritePosition)
		     || ((pHTTXContext->ENextBulkOutPosition - 8) ==
			 pHTTXContext->CurWritePosition))
		    && (pHTTXContext->bCopySavePad == TRUE))
			pWirelessPacket = (u8 *)(&pHTTXContext->SavedPad[0]);
		else
			pWirelessPacket =
			    (u8 *)(&pHTTXContext->TransferBuffer->field.
				      WirelessPacket[fillOffset]);

		/* */
		/* Update TxInfo->USBDMApktLen , */
		/*              the length = TXWI_SIZE + 802.11_hdr + 802.11_hdr_pad + payload_of_all_batch_frames + Bulk-Out-padding */
		/* */
		pTxInfo = (struct rt_txinfo *)(pWirelessPacket);

		/* Calculate the bulk-out padding */
		USBDMApktLen = pTxBlk->Priv - TXINFO_SIZE;
		padding = (4 - (USBDMApktLen % 4)) & 0x03;	/* round up to 4 byte alignment */
		USBDMApktLen += padding;

		pTxInfo->USBDMATxPktLen = USBDMApktLen;

		/* */
		/* Update TXWI->MPDUtotalByteCount , */
		/*              the length = 802.11 header + payload_of_all_batch_frames */
		pTxWI = (struct rt_txwi *) (pWirelessPacket + TXINFO_SIZE);
		pTxWI->MPDUtotalByteCount = totalMPDUSize;

		/* */
		/* Update the pHTTXContext->CurWritePosition */
		/* */
		pHTTXContext->CurWritePosition += (TXINFO_SIZE + USBDMApktLen);
		if ((pHTTXContext->CurWritePosition + 3906) > MAX_TXBULK_LIMIT) {	/* Add 3906 for prevent the NextBulkOut packet size is a A-RALINK/A-MSDU Frame. */
			pHTTXContext->CurWritePosition = 8;
			pTxInfo->SwUseLastRound = 1;
		}
		pHTTXContext->CurWriteRealPos = pHTTXContext->CurWritePosition;

		/* */
		/*      Zero the last padding. */
		/* */
		pWirelessPacket =
		    (&pHTTXContext->TransferBuffer->field.
		     WirelessPacket[fillOffset + pTxBlk->Priv]);
		NdisZeroMemory(pWirelessPacket, padding + 8);

		/* Finally, set bCurWriting as FALSE */
		pHTTXContext->bCurWriting = FALSE;

	} else {		/* It should not happened now unless we are going to shutdown. */
		DBGPRINT(RT_DEBUG_ERROR,
			 ("FinalWriteTxResource():bCurWriting is FALSE when handle last frames.\n"));
	}

	RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[QueIdx], IrqFlags);

}

void RtmpUSBDataLastTxIdx(struct rt_rtmp_adapter *pAd,
			  u8 QueIdx, u16 TxIdx)
{
	/* DO nothing for USB. */
}

/*
	When can do bulk-out:
		1. TxSwFreeIdx < TX_RING_SIZE;
			It means has at least one Ring entity is ready for bulk-out, kick it out.
		2. If TxSwFreeIdx == TX_RING_SIZE
			Check if the CurWriting flag is FALSE, if it's FALSE, we can do kick out.

*/
void RtmpUSBDataKickOut(struct rt_rtmp_adapter *pAd,
			struct rt_tx_blk *pTxBlk, u8 QueIdx)
{
	RTUSB_SET_BULK_FLAG(pAd, (fRTUSB_BULK_OUT_DATA_NORMAL << QueIdx));
	RTUSBKickBulkOut(pAd);

}

/*
	Must be run in Interrupt context
	This function handle RT2870 specific TxDesc and cpu index update and kick the packet out.
 */
int RtmpUSBMgmtKickOut(struct rt_rtmp_adapter *pAd,
		       u8 QueIdx,
		       void *pPacket,
		       u8 *pSrcBufVA, u32 SrcBufLen)
{
	struct rt_txinfo *pTxInfo;
	unsigned long BulkOutSize;
	u8 padLen;
	u8 *pDest;
	unsigned long SwIdx = pAd->MgmtRing.TxCpuIdx;
	struct rt_tx_context *pMLMEContext =
	    (struct rt_tx_context *)pAd->MgmtRing.Cell[SwIdx].AllocVa;
	unsigned long IrqFlags;

	pTxInfo = (struct rt_txinfo *)(pSrcBufVA);

	/* Build our URB for USBD */
	BulkOutSize = SrcBufLen;
	BulkOutSize = (BulkOutSize + 3) & (~3);
	RTMPWriteTxInfo(pAd, pTxInfo, (u16)(BulkOutSize - TXINFO_SIZE),
			TRUE, EpToQueue[MGMTPIPEIDX], FALSE, FALSE);

	BulkOutSize += 4;	/* Always add 4 extra bytes at every packet. */

	/* If BulkOutSize is multiple of BulkOutMaxPacketSize, add extra 4 bytes again. */
	if ((BulkOutSize % pAd->BulkOutMaxPacketSize) == 0)
		BulkOutSize += 4;

	padLen = BulkOutSize - SrcBufLen;
	ASSERT((padLen <= RTMP_PKT_TAIL_PADDING));

	/* Now memzero all extra padding bytes. */
	pDest = (u8 *)(pSrcBufVA + SrcBufLen);
	skb_put(GET_OS_PKT_TYPE(pPacket), padLen);
	NdisZeroMemory(pDest, padLen);

	RTMP_IRQ_LOCK(&pAd->MLMEBulkOutLock, IrqFlags);

	pAd->MgmtRing.Cell[pAd->MgmtRing.TxCpuIdx].pNdisPacket = pPacket;
	pMLMEContext->TransferBuffer =
	    (struct rt_tx_buffer *)(GET_OS_PKT_DATAPTR(pPacket));

	/* Length in TxInfo should be 8 less than bulkout size. */
	pMLMEContext->BulkOutSize = BulkOutSize;
	pMLMEContext->InUse = TRUE;
	pMLMEContext->bWaitingBulkOut = TRUE;

	/*for debug */
	/*hex_dump("RtmpUSBMgmtKickOut", &pMLMEContext->TransferBuffer->field.WirelessPacket[0], (pMLMEContext->BulkOutSize > 16 ? 16 : pMLMEContext->BulkOutSize)); */

	/*pAd->RalinkCounters.KickTxCount++; */
	/*pAd->RalinkCounters.OneSecTxDoneCount++; */

	/*if (pAd->MgmtRing.TxSwFreeIdx == MGMT_RING_SIZE) */
	/*      needKickOut = TRUE; */

	/* Decrease the TxSwFreeIdx and Increase the TX_CTX_IDX */
	pAd->MgmtRing.TxSwFreeIdx--;
	INC_RING_INDEX(pAd->MgmtRing.TxCpuIdx, MGMT_RING_SIZE);

	RTMP_IRQ_UNLOCK(&pAd->MLMEBulkOutLock, IrqFlags);

	RTUSB_SET_BULK_FLAG(pAd, fRTUSB_BULK_OUT_MLME);
	/*if (needKickOut) */
	RTUSBKickBulkOut(pAd);

	return 0;
}

void RtmpUSBNullFrameKickOut(struct rt_rtmp_adapter *pAd,
			     u8 QueIdx,
			     u8 * pNullFrame, u32 frameLen)
{
	if (pAd->NullContext.InUse == FALSE) {
		struct rt_tx_context *pNullContext;
		struct rt_txinfo *pTxInfo;
		struct rt_txwi * pTxWI;
		u8 *pWirelessPkt;

		pNullContext = &(pAd->NullContext);

		/* Set the in use bit */
		pNullContext->InUse = TRUE;
		pWirelessPkt =
		    (u8 *)& pNullContext->TransferBuffer->field.
		    WirelessPacket[0];

		RTMPZeroMemory(&pWirelessPkt[0], 100);
		pTxInfo = (struct rt_txinfo *)& pWirelessPkt[0];
		RTMPWriteTxInfo(pAd, pTxInfo,
				(u16)(sizeof(struct rt_header_802_11) + TXWI_SIZE),
				TRUE, EpToQueue[MGMTPIPEIDX], FALSE, FALSE);
		pTxInfo->QSEL = FIFO_EDCA;
		pTxWI = (struct rt_txwi *) & pWirelessPkt[TXINFO_SIZE];
		RTMPWriteTxWI(pAd, pTxWI, FALSE, FALSE, FALSE, FALSE, TRUE,
			      FALSE, 0, BSSID_WCID, (sizeof(struct rt_header_802_11)), 0,
			      0, (u8)pAd->CommonCfg.MlmeTransmit.field.MCS,
			      IFS_HTTXOP, FALSE, &pAd->CommonCfg.MlmeTransmit);

		RTMPMoveMemory(&pWirelessPkt[TXWI_SIZE + TXINFO_SIZE],
			       &pAd->NullFrame, sizeof(struct rt_header_802_11));
		pAd->NullContext.BulkOutSize =
		    TXINFO_SIZE + TXWI_SIZE + sizeof(pAd->NullFrame) + 4;

		/* Fill out frame length information for global Bulk out arbitor */
		/*pNullContext->BulkOutSize = TransferBufferLength; */
		DBGPRINT(RT_DEBUG_TRACE,
			 ("SYNC - send NULL Frame @%d Mbps...\n",
			  RateIdToMbps[pAd->CommonCfg.TxRate]));
		RTUSB_SET_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NULL);

		/* Kick bulk out */
		RTUSBKickBulkOut(pAd);
	}

}

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
void *GetPacketFromRxRing(struct rt_rtmp_adapter *pAd,
				 OUT PRT28XX_RXD_STRUC pSaveRxD,
				 OUT BOOLEAN * pbReschedule,
				 IN u32 * pRxPending)
{
	struct rt_rx_context *pRxContext;
	void *pSkb;
	u8 *pData;
	unsigned long ThisFrameLen;
	unsigned long RxBufferLength;
	struct rt_rxwi * pRxWI;

	pRxContext = &pAd->RxContext[pAd->NextRxBulkInReadIndex];
	if ((pRxContext->Readable == FALSE) || (pRxContext->InUse == TRUE))
		return NULL;

	RxBufferLength = pRxContext->BulkInOffset - pAd->ReadPosition;
	if (RxBufferLength <
	    (RT2870_RXDMALEN_FIELD_SIZE + sizeof(struct rt_rxwi) +
	     sizeof(struct rt_rxinfo))) {
		goto label_null;
	}

	pData = &pRxContext->TransferBuffer[pAd->ReadPosition];	/* 4KB */
	/* The RXDMA field is 4 bytes, now just use the first 2 bytes. The Length including the (RXWI + MSDU + Padding) */
	ThisFrameLen = *pData + (*(pData + 1) << 8);
	if (ThisFrameLen == 0) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("BIRIdx(%d): RXDMALen is zero.[%ld], BulkInBufLen = %ld)\n",
			  pAd->NextRxBulkInReadIndex, ThisFrameLen,
			  pRxContext->BulkInOffset));
		goto label_null;
	}
	if ((ThisFrameLen & 0x3) != 0) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("BIRIdx(%d): RXDMALen not multiple of 4.[%ld], BulkInBufLen = %ld)\n",
			  pAd->NextRxBulkInReadIndex, ThisFrameLen,
			  pRxContext->BulkInOffset));
		goto label_null;
	}

	if ((ThisFrameLen + 8) > RxBufferLength)	/* 8 for (RT2870_RXDMALEN_FIELD_SIZE + sizeof(struct rt_rxinfo)) */
	{
		DBGPRINT(RT_DEBUG_TRACE,
			 ("BIRIdx(%d):FrameLen(0x%lx) outranges. BulkInLen=0x%lx, remaining RxBufLen=0x%lx, ReadPos=0x%lx\n",
			  pAd->NextRxBulkInReadIndex, ThisFrameLen,
			  pRxContext->BulkInOffset, RxBufferLength,
			  pAd->ReadPosition));

		/* error frame. finish this loop */
		goto label_null;
	}
	/* skip USB frame length field */
	pData += RT2870_RXDMALEN_FIELD_SIZE;
	pRxWI = (struct rt_rxwi *) pData;
	if (pRxWI->MPDUtotalByteCount > ThisFrameLen) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s():pRxWIMPDUtotalByteCount(%d) large than RxDMALen(%ld)\n",
			  __FUNCTION__, pRxWI->MPDUtotalByteCount,
			  ThisFrameLen));
		goto label_null;
	}
	/* allocate a rx packet */
	pSkb = dev_alloc_skb(ThisFrameLen);
	if (pSkb == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s():Cannot Allocate sk buffer for this Bulk-In buffer!\n",
			  __FUNCTION__));
		goto label_null;
	}
	/* copy the rx packet */
	memcpy(skb_put(pSkb, ThisFrameLen), pData, ThisFrameLen);
	RTPKT_TO_OSPKT(pSkb)->dev = get_netdev_from_bssid(pAd, BSS0);
	RTMP_SET_PACKET_SOURCE(OSPKT_TO_RTPKT(pSkb), PKTSRC_NDIS);

	/* copy RxD */
	*pSaveRxD = *(struct rt_rxinfo *) (pData + ThisFrameLen);

	/* update next packet read position. */
	pAd->ReadPosition += (ThisFrameLen + RT2870_RXDMALEN_FIELD_SIZE + RXINFO_SIZE);	/* 8 for (RT2870_RXDMALEN_FIELD_SIZE + sizeof(struct rt_rxinfo)) */

	return pSkb;

label_null:

	return NULL;
}

/*
	========================================================================

	Routine	Description:
		Check Rx descriptor, return NDIS_STATUS_FAILURE if any error dound

	Arguments:
		pRxD		Pointer	to the Rx descriptor

	Return Value:
		NDIS_STATUS_SUCCESS		No err
		NDIS_STATUS_FAILURE		Error

	Note:

	========================================================================
*/
int RTMPCheckRxError(struct rt_rtmp_adapter *pAd,
			     struct rt_header_802_11 * pHeader,
			     struct rt_rxwi * pRxWI, IN PRT28XX_RXD_STRUC pRxINFO)
{
	struct rt_cipher_key *pWpaKey;
	int dBm;

	if (pAd->bPromiscuous == TRUE)
		return (NDIS_STATUS_SUCCESS);
	if (pRxINFO == NULL)
		return (NDIS_STATUS_FAILURE);

	/* Phy errors & CRC errors */
	if (pRxINFO->Crc) {
		/* Check RSSI for Noise Hist statistic collection. */
		dBm = (int)(pRxWI->RSSI0) - pAd->BbpRssiToDbmDelta;
		if (dBm <= -87)
			pAd->StaCfg.RPIDensity[0] += 1;
		else if (dBm <= -82)
			pAd->StaCfg.RPIDensity[1] += 1;
		else if (dBm <= -77)
			pAd->StaCfg.RPIDensity[2] += 1;
		else if (dBm <= -72)
			pAd->StaCfg.RPIDensity[3] += 1;
		else if (dBm <= -67)
			pAd->StaCfg.RPIDensity[4] += 1;
		else if (dBm <= -62)
			pAd->StaCfg.RPIDensity[5] += 1;
		else if (dBm <= -57)
			pAd->StaCfg.RPIDensity[6] += 1;
		else if (dBm > -57)
			pAd->StaCfg.RPIDensity[7] += 1;

		return (NDIS_STATUS_FAILURE);
	}
	/* Add Rx size to channel load counter, we should ignore error counts */
	pAd->StaCfg.CLBusyBytes += (pRxWI->MPDUtotalByteCount + 14);

	/* Drop ToDs promiscous frame, it is opened due to CCX 2 channel load statistics */
	if (pHeader->FC.ToDs) {
		DBGPRINT_RAW(RT_DEBUG_ERROR, ("Err;FC.ToDs\n"));
		return NDIS_STATUS_FAILURE;
	}
	/* Paul 04-03 for OFDM Rx length issue */
	if (pRxWI->MPDUtotalByteCount > MAX_AGGREGATION_SIZE) {
		DBGPRINT_RAW(RT_DEBUG_ERROR, ("received packet too long\n"));
		return NDIS_STATUS_FAILURE;
	}
	/* Drop not U2M frames, cant's drop here because we will drop beacon in this case */
	/* I am kind of doubting the U2M bit operation */
	/* if (pRxD->U2M == 0) */
	/*      return(NDIS_STATUS_FAILURE); */

	/* drop decyption fail frame */
	if (pRxINFO->Decrypted && pRxINFO->CipherErr) {

		if (((pRxINFO->CipherErr & 1) == 1)
		    && pAd->CommonCfg.bWirelessEvent && INFRA_ON(pAd))
			RTMPSendWirelessEvent(pAd, IW_ICV_ERROR_EVENT_FLAG,
					      pAd->MacTab.Content[BSSID_WCID].
					      Addr, BSS0, 0);

		if (((pRxINFO->CipherErr & 2) == 2)
		    && pAd->CommonCfg.bWirelessEvent && INFRA_ON(pAd))
			RTMPSendWirelessEvent(pAd, IW_MIC_ERROR_EVENT_FLAG,
					      pAd->MacTab.Content[BSSID_WCID].
					      Addr, BSS0, 0);
		/* */
		/* MIC Error */
		/* */
		if ((pRxINFO->CipherErr == 2) && pRxINFO->MyBss) {
			pWpaKey = &pAd->SharedKey[BSS0][pRxWI->KeyIndex];
			RTMPReportMicError(pAd, pWpaKey);
			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Rx MIC Value error\n"));
		}

		if (pRxINFO->Decrypted &&
		    (pAd->SharedKey[BSS0][pRxWI->KeyIndex].CipherAlg ==
		     CIPHER_AES)
		    && (pHeader->Sequence == pAd->FragFrame.Sequence)) {
			/* */
			/* Acceptable since the First FragFrame no CipherErr problem. */
			/* */
			return (NDIS_STATUS_SUCCESS);
		}

		return (NDIS_STATUS_FAILURE);
	}

	return (NDIS_STATUS_SUCCESS);
}

void RtmpUsbStaAsicForceWakeupTimeout(void *SystemSpecific1,
				      void *FunctionContext,
				      void *SystemSpecific2,
				      void *SystemSpecific3)
{
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)FunctionContext;

	if (pAd && pAd->Mlme.AutoWakeupTimerRunning) {
		AsicSendCommandToMcu(pAd, 0x31, 0xff, 0x00, 0x02);

		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
		pAd->Mlme.AutoWakeupTimerRunning = FALSE;
	}
}

void RT28xxUsbStaAsicForceWakeup(struct rt_rtmp_adapter *pAd, IN BOOLEAN bFromTx)
{
	BOOLEAN Canceled;

	if (pAd->Mlme.AutoWakeupTimerRunning)
		RTMPCancelTimer(&pAd->Mlme.AutoWakeupTimer, &Canceled);

	AsicSendCommandToMcu(pAd, 0x31, 0xff, 0x00, 0x02);

	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
}

void RT28xxUsbStaAsicSleepThenAutoWakeup(struct rt_rtmp_adapter *pAd,
					 u16 TbttNumToNextWakeUp)
{

	/* we have decided to SLEEP, so at least do it for a BEACON period. */
	if (TbttNumToNextWakeUp == 0)
		TbttNumToNextWakeUp = 1;

	RTMPSetTimer(&pAd->Mlme.AutoWakeupTimer, AUTO_WAKEUP_TIMEOUT);
	pAd->Mlme.AutoWakeupTimerRunning = TRUE;

	AsicSendCommandToMcu(pAd, 0x30, 0xff, 0xff, 0x02);	/* send POWER-SAVE command to MCU. Timeout 40us. */

	OPSTATUS_SET_FLAG(pAd, fOP_STATUS_DOZE);

}

#endif /* RTMP_MAC_USB // */
