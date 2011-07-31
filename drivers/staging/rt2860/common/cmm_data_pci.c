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
   All functions in this file must be PCI-depended, or you should out your function
	in other files.

*/
#include	"../rt_config.h"

u16 RtmpPCI_WriteTxResource(struct rt_rtmp_adapter *pAd,
			       struct rt_tx_blk *pTxBlk,
			       IN BOOLEAN bIsLast, u16 * FreeNumber)
{

	u8 *pDMAHeaderBufVA;
	u16 TxIdx, RetTxIdx;
	struct rt_txd * pTxD;
	u32 BufBasePaLow;
	struct rt_rtmp_tx_ring *pTxRing;
	u16 hwHeaderLen;

	/* */
	/* get Tx Ring Resource */
	/* */
	pTxRing = &pAd->TxRing[pTxBlk->QueIdx];
	TxIdx = pAd->TxRing[pTxBlk->QueIdx].TxCpuIdx;
	pDMAHeaderBufVA = (u8 *)pTxRing->Cell[TxIdx].DmaBuf.AllocVa;
	BufBasePaLow =
	    RTMP_GetPhysicalAddressLow(pTxRing->Cell[TxIdx].DmaBuf.AllocPa);

	/* copy TXINFO + TXWI + WLAN Header + LLC into DMA Header Buffer */
	if (pTxBlk->TxFrameType == TX_AMSDU_FRAME) {
		/*hwHeaderLen = ROUND_UP(pTxBlk->MpduHeaderLen-LENGTH_AMSDU_SUBFRAMEHEAD, 4)+LENGTH_AMSDU_SUBFRAMEHEAD; */
		hwHeaderLen =
		    pTxBlk->MpduHeaderLen - LENGTH_AMSDU_SUBFRAMEHEAD +
		    pTxBlk->HdrPadLen + LENGTH_AMSDU_SUBFRAMEHEAD;
	} else {
		/*hwHeaderLen = ROUND_UP(pTxBlk->MpduHeaderLen, 4); */
		hwHeaderLen = pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;
	}
	NdisMoveMemory(pDMAHeaderBufVA, pTxBlk->HeaderBuf,
		       TXINFO_SIZE + TXWI_SIZE + hwHeaderLen);

	pTxRing->Cell[TxIdx].pNdisPacket = pTxBlk->pPacket;
	pTxRing->Cell[TxIdx].pNextNdisPacket = NULL;

	/* */
	/* build Tx Descriptor */
	/* */

	pTxD = (struct rt_txd *) pTxRing->Cell[TxIdx].AllocVa;
	NdisZeroMemory(pTxD, TXD_SIZE);

	pTxD->SDPtr0 = BufBasePaLow;
	pTxD->SDLen0 = TXINFO_SIZE + TXWI_SIZE + hwHeaderLen;	/* include padding */
	pTxD->SDPtr1 = PCI_MAP_SINGLE(pAd, pTxBlk, 0, 1, PCI_DMA_TODEVICE);
	pTxD->SDLen1 = pTxBlk->SrcBufLen;
	pTxD->LastSec0 = 0;
	pTxD->LastSec1 = (bIsLast) ? 1 : 0;

	RTMPWriteTxDescriptor(pAd, pTxD, FALSE, FIFO_EDCA);

	RetTxIdx = TxIdx;
	/* */
	/* Update Tx index */
	/* */
	INC_RING_INDEX(TxIdx, TX_RING_SIZE);
	pTxRing->TxCpuIdx = TxIdx;

	*FreeNumber -= 1;

	return RetTxIdx;
}

u16 RtmpPCI_WriteSingleTxResource(struct rt_rtmp_adapter *pAd,
				     struct rt_tx_blk *pTxBlk,
				     IN BOOLEAN bIsLast,
				     u16 * FreeNumber)
{

	u8 *pDMAHeaderBufVA;
	u16 TxIdx, RetTxIdx;
	struct rt_txd * pTxD;
	u32 BufBasePaLow;
	struct rt_rtmp_tx_ring *pTxRing;
	u16 hwHeaderLen;

	/* */
	/* get Tx Ring Resource */
	/* */
	pTxRing = &pAd->TxRing[pTxBlk->QueIdx];
	TxIdx = pAd->TxRing[pTxBlk->QueIdx].TxCpuIdx;
	pDMAHeaderBufVA = (u8 *)pTxRing->Cell[TxIdx].DmaBuf.AllocVa;
	BufBasePaLow =
	    RTMP_GetPhysicalAddressLow(pTxRing->Cell[TxIdx].DmaBuf.AllocPa);

	/* copy TXINFO + TXWI + WLAN Header + LLC into DMA Header Buffer */
	/*hwHeaderLen = ROUND_UP(pTxBlk->MpduHeaderLen, 4); */
	hwHeaderLen = pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;

	NdisMoveMemory(pDMAHeaderBufVA, pTxBlk->HeaderBuf,
		       TXINFO_SIZE + TXWI_SIZE + hwHeaderLen);

	pTxRing->Cell[TxIdx].pNdisPacket = pTxBlk->pPacket;
	pTxRing->Cell[TxIdx].pNextNdisPacket = NULL;

	/* */
	/* build Tx Descriptor */
	/* */
	pTxD = (struct rt_txd *) pTxRing->Cell[TxIdx].AllocVa;
	NdisZeroMemory(pTxD, TXD_SIZE);

	pTxD->SDPtr0 = BufBasePaLow;
	pTxD->SDLen0 = TXINFO_SIZE + TXWI_SIZE + hwHeaderLen;	/* include padding */
	pTxD->SDPtr1 = PCI_MAP_SINGLE(pAd, pTxBlk, 0, 1, PCI_DMA_TODEVICE);;
	pTxD->SDLen1 = pTxBlk->SrcBufLen;
	pTxD->LastSec0 = 0;
	pTxD->LastSec1 = (bIsLast) ? 1 : 0;

	RTMPWriteTxDescriptor(pAd, pTxD, FALSE, FIFO_EDCA);

	RetTxIdx = TxIdx;
	/* */
	/* Update Tx index */
	/* */
	INC_RING_INDEX(TxIdx, TX_RING_SIZE);
	pTxRing->TxCpuIdx = TxIdx;

	*FreeNumber -= 1;

	return RetTxIdx;
}

u16 RtmpPCI_WriteMultiTxResource(struct rt_rtmp_adapter *pAd,
				    struct rt_tx_blk *pTxBlk,
				    u8 frameNum, u16 * FreeNumber)
{
	BOOLEAN bIsLast;
	u8 *pDMAHeaderBufVA;
	u16 TxIdx, RetTxIdx;
	struct rt_txd * pTxD;
	u32 BufBasePaLow;
	struct rt_rtmp_tx_ring *pTxRing;
	u16 hwHdrLen;
	u32 firstDMALen;

	bIsLast = ((frameNum == (pTxBlk->TotalFrameNum - 1)) ? 1 : 0);

	/* */
	/* get Tx Ring Resource */
	/* */
	pTxRing = &pAd->TxRing[pTxBlk->QueIdx];
	TxIdx = pAd->TxRing[pTxBlk->QueIdx].TxCpuIdx;
	pDMAHeaderBufVA = (u8 *)pTxRing->Cell[TxIdx].DmaBuf.AllocVa;
	BufBasePaLow =
	    RTMP_GetPhysicalAddressLow(pTxRing->Cell[TxIdx].DmaBuf.AllocPa);

	if (frameNum == 0) {
		/* copy TXINFO + TXWI + WLAN Header + LLC into DMA Header Buffer */
		if (pTxBlk->TxFrameType == TX_AMSDU_FRAME)
			/*hwHdrLen = ROUND_UP(pTxBlk->MpduHeaderLen-LENGTH_AMSDU_SUBFRAMEHEAD, 4)+LENGTH_AMSDU_SUBFRAMEHEAD; */
			hwHdrLen =
			    pTxBlk->MpduHeaderLen - LENGTH_AMSDU_SUBFRAMEHEAD +
			    pTxBlk->HdrPadLen + LENGTH_AMSDU_SUBFRAMEHEAD;
		else if (pTxBlk->TxFrameType == TX_RALINK_FRAME)
			/*hwHdrLen = ROUND_UP(pTxBlk->MpduHeaderLen-LENGTH_ARALINK_HEADER_FIELD, 4)+LENGTH_ARALINK_HEADER_FIELD; */
			hwHdrLen =
			    pTxBlk->MpduHeaderLen -
			    LENGTH_ARALINK_HEADER_FIELD + pTxBlk->HdrPadLen +
			    LENGTH_ARALINK_HEADER_FIELD;
		else
			/*hwHdrLen = ROUND_UP(pTxBlk->MpduHeaderLen, 4); */
			hwHdrLen = pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;

		firstDMALen = TXINFO_SIZE + TXWI_SIZE + hwHdrLen;
	} else {
		firstDMALen = pTxBlk->MpduHeaderLen;
	}

	NdisMoveMemory(pDMAHeaderBufVA, pTxBlk->HeaderBuf, firstDMALen);

	pTxRing->Cell[TxIdx].pNdisPacket = pTxBlk->pPacket;
	pTxRing->Cell[TxIdx].pNextNdisPacket = NULL;

	/* */
	/* build Tx Descriptor */
	/* */
	pTxD = (struct rt_txd *) pTxRing->Cell[TxIdx].AllocVa;
	NdisZeroMemory(pTxD, TXD_SIZE);

	pTxD->SDPtr0 = BufBasePaLow;
	pTxD->SDLen0 = firstDMALen;	/* include padding */
	pTxD->SDPtr1 = PCI_MAP_SINGLE(pAd, pTxBlk, 0, 1, PCI_DMA_TODEVICE);;
	pTxD->SDLen1 = pTxBlk->SrcBufLen;
	pTxD->LastSec0 = 0;
	pTxD->LastSec1 = (bIsLast) ? 1 : 0;

	RTMPWriteTxDescriptor(pAd, pTxD, FALSE, FIFO_EDCA);

	RetTxIdx = TxIdx;
	/* */
	/* Update Tx index */
	/* */
	INC_RING_INDEX(TxIdx, TX_RING_SIZE);
	pTxRing->TxCpuIdx = TxIdx;

	*FreeNumber -= 1;

	return RetTxIdx;

}

void RtmpPCI_FinalWriteTxResource(struct rt_rtmp_adapter *pAd,
				  struct rt_tx_blk *pTxBlk,
				  u16 totalMPDUSize, u16 FirstTxIdx)
{

	struct rt_txwi * pTxWI;
	struct rt_rtmp_tx_ring *pTxRing;

	/* */
	/* get Tx Ring Resource */
	/* */
	pTxRing = &pAd->TxRing[pTxBlk->QueIdx];
	pTxWI = (struct rt_txwi *) pTxRing->Cell[FirstTxIdx].DmaBuf.AllocVa;
	pTxWI->MPDUtotalByteCount = totalMPDUSize;

}

void RtmpPCIDataLastTxIdx(struct rt_rtmp_adapter *pAd,
			  u8 QueIdx, u16 LastTxIdx)
{
	struct rt_txd * pTxD;
	struct rt_rtmp_tx_ring *pTxRing;

	/* */
	/* get Tx Ring Resource */
	/* */
	pTxRing = &pAd->TxRing[QueIdx];

	/* */
	/* build Tx Descriptor */
	/* */
	pTxD = (struct rt_txd *) pTxRing->Cell[LastTxIdx].AllocVa;

	pTxD->LastSec1 = 1;

}

u16 RtmpPCI_WriteFragTxResource(struct rt_rtmp_adapter *pAd,
				   struct rt_tx_blk *pTxBlk,
				   u8 fragNum, u16 * FreeNumber)
{
	u8 *pDMAHeaderBufVA;
	u16 TxIdx, RetTxIdx;
	struct rt_txd * pTxD;
	u32 BufBasePaLow;
	struct rt_rtmp_tx_ring *pTxRing;
	u16 hwHeaderLen;
	u32 firstDMALen;

	/* */
	/* Get Tx Ring Resource */
	/* */
	pTxRing = &pAd->TxRing[pTxBlk->QueIdx];
	TxIdx = pAd->TxRing[pTxBlk->QueIdx].TxCpuIdx;
	pDMAHeaderBufVA = (u8 *)pTxRing->Cell[TxIdx].DmaBuf.AllocVa;
	BufBasePaLow =
	    RTMP_GetPhysicalAddressLow(pTxRing->Cell[TxIdx].DmaBuf.AllocPa);

	/* */
	/* Copy TXINFO + TXWI + WLAN Header + LLC into DMA Header Buffer */
	/* */
	/*hwHeaderLen = ROUND_UP(pTxBlk->MpduHeaderLen, 4); */
	hwHeaderLen = pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;

	firstDMALen = TXINFO_SIZE + TXWI_SIZE + hwHeaderLen;
	NdisMoveMemory(pDMAHeaderBufVA, pTxBlk->HeaderBuf, firstDMALen);

	/* */
	/* Build Tx Descriptor */
	/* */
	pTxD = (struct rt_txd *) pTxRing->Cell[TxIdx].AllocVa;
	NdisZeroMemory(pTxD, TXD_SIZE);

	if (fragNum == pTxBlk->TotalFragNum) {
		pTxRing->Cell[TxIdx].pNdisPacket = pTxBlk->pPacket;
		pTxRing->Cell[TxIdx].pNextNdisPacket = NULL;
	}

	pTxD->SDPtr0 = BufBasePaLow;
	pTxD->SDLen0 = firstDMALen;	/* include padding */
	pTxD->SDPtr1 = PCI_MAP_SINGLE(pAd, pTxBlk, 0, 1, PCI_DMA_TODEVICE);
	pTxD->SDLen1 = pTxBlk->SrcBufLen;
	pTxD->LastSec0 = 0;
	pTxD->LastSec1 = 1;

	RTMPWriteTxDescriptor(pAd, pTxD, FALSE, FIFO_EDCA);

	RetTxIdx = TxIdx;
	pTxBlk->Priv += pTxBlk->SrcBufLen;

	/* */
	/* Update Tx index */
	/* */
	INC_RING_INDEX(TxIdx, TX_RING_SIZE);
	pTxRing->TxCpuIdx = TxIdx;

	*FreeNumber -= 1;

	return RetTxIdx;

}

/*
	Must be run in Interrupt context
	This function handle PCI specific TxDesc and cpu index update and kick the packet out.
 */
int RtmpPCIMgmtKickOut(struct rt_rtmp_adapter *pAd,
		       u8 QueIdx,
		       void *pPacket,
		       u8 *pSrcBufVA, u32 SrcBufLen)
{
	struct rt_txd * pTxD;
	unsigned long SwIdx = pAd->MgmtRing.TxCpuIdx;

	pTxD = (struct rt_txd *) pAd->MgmtRing.Cell[SwIdx].AllocVa;

	pAd->MgmtRing.Cell[SwIdx].pNdisPacket = pPacket;
	pAd->MgmtRing.Cell[SwIdx].pNextNdisPacket = NULL;

	RTMPWriteTxDescriptor(pAd, pTxD, TRUE, FIFO_MGMT);
	pTxD->LastSec0 = 1;
	pTxD->LastSec1 = 1;
	pTxD->DMADONE = 0;
	pTxD->SDLen1 = 0;
	pTxD->SDPtr0 =
	    PCI_MAP_SINGLE(pAd, pSrcBufVA, SrcBufLen, 0, PCI_DMA_TODEVICE);
	pTxD->SDLen0 = SrcBufLen;

/*================================================================== */
/*	DBGPRINT_RAW(RT_DEBUG_TRACE, ("MLMEHardTransmit\n"));
	for (i = 0; i < (TXWI_SIZE+24); i++)
	{

		DBGPRINT_RAW(RT_DEBUG_TRACE, ("%x:", *(pSrcBufVA+i)));
		if ( i%4 == 3)
			DBGPRINT_RAW(RT_DEBUG_TRACE, (" :: "));
		if ( i%16 == 15)
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("\n      "));
	}
	DBGPRINT_RAW(RT_DEBUG_TRACE, ("\n      "));*/
/*======================================================================= */

	pAd->RalinkCounters.KickTxCount++;
	pAd->RalinkCounters.OneSecTxDoneCount++;

	/* Increase TX_CTX_IDX, but write to register later. */
	INC_RING_INDEX(pAd->MgmtRing.TxCpuIdx, MGMT_RING_SIZE);

	RTMP_IO_WRITE32(pAd, TX_MGMTCTX_IDX, pAd->MgmtRing.TxCpuIdx);

	return 0;
}

/*
	========================================================================

	Routine Description:
		Check Rx descriptor, return NDIS_STATUS_FAILURE if any error dound

	Arguments:
		pRxD		Pointer to the Rx descriptor

	Return Value:
		NDIS_STATUS_SUCCESS	No err
		NDIS_STATUS_FAILURE	Error

	Note:

	========================================================================
*/
int RTMPCheckRxError(struct rt_rtmp_adapter *pAd,
			     struct rt_header_802_11 * pHeader,
			     struct rt_rxwi * pRxWI, IN PRT28XX_RXD_STRUC pRxD)
{
	struct rt_cipher_key *pWpaKey;
	int dBm;

	/* Phy errors & CRC errors */
	if ( /*(pRxD->PhyErr) || */ (pRxD->Crc)) {
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
	pAd->StaCfg.CLBusyBytes += (pRxD->SDL0 + 14);

	/* Drop ToDs promiscous frame, it is opened due to CCX 2 channel load statistics */
	if (pHeader != NULL) {
		if (pHeader->FC.ToDs) {
			return (NDIS_STATUS_FAILURE);
		}
	}
	/* Drop not U2M frames, cant's drop here because we will drop beacon in this case */
	/* I am kind of doubting the U2M bit operation */
	/* if (pRxD->U2M == 0) */
	/*      return(NDIS_STATUS_FAILURE); */

	/* drop decyption fail frame */
	if (pRxD->CipherErr) {
		if (pRxD->CipherErr == 2) {
			DBGPRINT_RAW(RT_DEBUG_TRACE,
				     ("pRxD ERROR: ICV ok but MICErr "));
		} else if (pRxD->CipherErr == 1) {
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("pRxD ERROR: ICV Err "));
		} else if (pRxD->CipherErr == 3)
			DBGPRINT_RAW(RT_DEBUG_TRACE,
				     ("pRxD ERROR: Key not valid "));

		if (((pRxD->CipherErr & 1) == 1)
		    && pAd->CommonCfg.bWirelessEvent && INFRA_ON(pAd))
			RTMPSendWirelessEvent(pAd, IW_ICV_ERROR_EVENT_FLAG,
					      pAd->MacTab.Content[BSSID_WCID].
					      Addr, BSS0, 0);

		DBGPRINT_RAW(RT_DEBUG_TRACE,
			     (" %d (len=%d, Mcast=%d, MyBss=%d, Wcid=%d, KeyId=%d)\n",
			      pRxD->CipherErr, pRxD->SDL0,
			      pRxD->Mcast | pRxD->Bcast, pRxD->MyBss,
			      pRxWI->WirelessCliID,
/*                      CipherName[pRxD->CipherAlg], */
			      pRxWI->KeyIndex));

		/* */
		/* MIC Error */
		/* */
		if (pRxD->CipherErr == 2) {
			pWpaKey = &pAd->SharedKey[BSS0][pRxWI->KeyIndex];
			if (pAd->StaCfg.WpaSupplicantUP)
				WpaSendMicFailureToWpaSupplicant(pAd,
								 (pWpaKey->
								  Type ==
								  PAIRWISEKEY) ?
								 TRUE : FALSE);
			else
				RTMPReportMicError(pAd, pWpaKey);

			if (((pRxD->CipherErr & 2) == 2)
			    && pAd->CommonCfg.bWirelessEvent && INFRA_ON(pAd))
				RTMPSendWirelessEvent(pAd,
						      IW_MIC_ERROR_EVENT_FLAG,
						      pAd->MacTab.
						      Content[BSSID_WCID].Addr,
						      BSS0, 0);

			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Rx MIC Value error\n"));
		}

		if (pHeader == NULL)
			return (NDIS_STATUS_SUCCESS);
		/*if ((pRxD->CipherAlg == CIPHER_AES) &&
		   (pHeader->Sequence == pAd->FragFrame.Sequence))
		   {
		   //
		   // Acceptable since the First FragFrame no CipherErr problem.
		   //
		   return(NDIS_STATUS_SUCCESS);
		   } */

		return (NDIS_STATUS_FAILURE);
	}

	return (NDIS_STATUS_SUCCESS);
}

BOOLEAN RTMPFreeTXDUponTxDmaDone(struct rt_rtmp_adapter *pAd, u8 QueIdx)
{
	struct rt_rtmp_tx_ring *pTxRing;
	struct rt_txd * pTxD;
	void *pPacket;
	u8 FREE = 0;
	struct rt_txd TxD, *pOriTxD;
	/*unsigned long         IrqFlags; */
	BOOLEAN bReschedule = FALSE;

	ASSERT(QueIdx < NUM_OF_TX_RING);
	pTxRing = &pAd->TxRing[QueIdx];

	RTMP_IO_READ32(pAd, TX_DTX_IDX0 + QueIdx * RINGREG_DIFF,
		       &pTxRing->TxDmaIdx);
	while (pTxRing->TxSwFreeIdx != pTxRing->TxDmaIdx) {
/*              RTMP_IRQ_LOCK(&pAd->irq_lock, IrqFlags); */

		/* static rate also need NICUpdateFifoStaCounters() function. */
		/*if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED)) */
		NICUpdateFifoStaCounters(pAd);

		/* Note : If (pAd->ate.bQATxStart == TRUE), we will never reach here. */
		FREE++;
		pTxD =
		    (struct rt_txd *) (pTxRing->Cell[pTxRing->TxSwFreeIdx].AllocVa);
		pOriTxD = pTxD;
		NdisMoveMemory(&TxD, pTxD, sizeof(struct rt_txd));
		pTxD = &TxD;

		pTxD->DMADONE = 0;

		{
			pPacket =
			    pTxRing->Cell[pTxRing->TxSwFreeIdx].pNdisPacket;
			if (pPacket) {
				PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1,
						 pTxD->SDLen1,
						 PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket,
						    NDIS_STATUS_SUCCESS);
			}
			/*Always assign pNdisPacket as NULL after clear */
			pTxRing->Cell[pTxRing->TxSwFreeIdx].pNdisPacket = NULL;

			pPacket =
			    pTxRing->Cell[pTxRing->TxSwFreeIdx].pNextNdisPacket;

			ASSERT(pPacket == NULL);
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

		pAd->RalinkCounters.TransmittedByteCount +=
		    (pTxD->SDLen1 + pTxD->SDLen0);
		pAd->RalinkCounters.OneSecDmaDoneCount[QueIdx]++;
		INC_RING_INDEX(pTxRing->TxSwFreeIdx, TX_RING_SIZE);
		/* get tx_tdx_idx again */
		RTMP_IO_READ32(pAd, TX_DTX_IDX0 + QueIdx * RINGREG_DIFF,
			       &pTxRing->TxDmaIdx);
		NdisMoveMemory(pOriTxD, pTxD, sizeof(struct rt_txd));

/*         RTMP_IRQ_UNLOCK(&pAd->irq_lock, IrqFlags); */
	}

	return bReschedule;

}

/*
	========================================================================

	Routine Description:
		Process TX Rings DMA Done interrupt, running in DPC level

	Arguments:
		Adapter		Pointer to our adapter

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	========================================================================
*/
BOOLEAN RTMPHandleTxRingDmaDoneInterrupt(struct rt_rtmp_adapter *pAd,
					 INT_SOURCE_CSR_STRUC TxRingBitmap)
{
/*      u8                   Count = 0; */
	unsigned long IrqFlags;
	BOOLEAN bReschedule = FALSE;

	/* Make sure Tx ring resource won't be used by other threads */
	/*NdisAcquireSpinLock(&pAd->TxRingLock); */

	RTMP_IRQ_LOCK(&pAd->irq_lock, IrqFlags);

	if (TxRingBitmap.field.Ac0DmaDone)
		bReschedule = RTMPFreeTXDUponTxDmaDone(pAd, QID_AC_BE);

	if (TxRingBitmap.field.Ac3DmaDone)
		bReschedule |= RTMPFreeTXDUponTxDmaDone(pAd, QID_AC_VO);

	if (TxRingBitmap.field.Ac2DmaDone)
		bReschedule |= RTMPFreeTXDUponTxDmaDone(pAd, QID_AC_VI);

	if (TxRingBitmap.field.Ac1DmaDone)
		bReschedule |= RTMPFreeTXDUponTxDmaDone(pAd, QID_AC_BK);

	/* Make sure to release Tx ring resource */
	/*NdisReleaseSpinLock(&pAd->TxRingLock); */
	RTMP_IRQ_UNLOCK(&pAd->irq_lock, IrqFlags);

	/* Dequeue outgoing frames from TxSwQueue[] and process it */
	RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);

	return bReschedule;
}

/*
	========================================================================

	Routine Description:
		Process MGMT ring DMA done interrupt, running in DPC level

	Arguments:
		pAd	Pointer to our adapter

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
void RTMPHandleMgmtRingDmaDoneInterrupt(struct rt_rtmp_adapter *pAd)
{
	struct rt_txd * pTxD;
	void *pPacket;
/*      int              i; */
	u8 FREE = 0;
	struct rt_rtmp_mgmt_ring *pMgmtRing = &pAd->MgmtRing;

	NdisAcquireSpinLock(&pAd->MgmtRingLock);

	RTMP_IO_READ32(pAd, TX_MGMTDTX_IDX, &pMgmtRing->TxDmaIdx);
	while (pMgmtRing->TxSwFreeIdx != pMgmtRing->TxDmaIdx) {
		FREE++;
		pTxD =
		    (struct rt_txd *) (pMgmtRing->Cell[pAd->MgmtRing.TxSwFreeIdx].
				  AllocVa);
		pTxD->DMADONE = 0;
		pPacket = pMgmtRing->Cell[pMgmtRing->TxSwFreeIdx].pNdisPacket;

		if (pPacket) {
			PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr0, pTxD->SDLen0,
					 PCI_DMA_TODEVICE);
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
		}
		pMgmtRing->Cell[pMgmtRing->TxSwFreeIdx].pNdisPacket = NULL;

		pPacket =
		    pMgmtRing->Cell[pMgmtRing->TxSwFreeIdx].pNextNdisPacket;
		if (pPacket) {
			PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1, pTxD->SDLen1,
					 PCI_DMA_TODEVICE);
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
		}
		pMgmtRing->Cell[pMgmtRing->TxSwFreeIdx].pNextNdisPacket = NULL;
		INC_RING_INDEX(pMgmtRing->TxSwFreeIdx, MGMT_RING_SIZE);

	}
	NdisReleaseSpinLock(&pAd->MgmtRingLock);

}

/*
	========================================================================

	Routine Description:
	Arguments:
		Adapter		Pointer to our adapter. Dequeue all power safe delayed braodcast frames after beacon.

	IRQL = DISPATCH_LEVEL

	========================================================================
*/
void RTMPHandleTBTTInterrupt(struct rt_rtmp_adapter *pAd)
{
	{
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE)) {
		}
	}
}

/*
	========================================================================

	Routine Description:
	Arguments:
		pAd		Pointer to our adapter. Rewrite beacon content before next send-out.

	IRQL = DISPATCH_LEVEL

	========================================================================
*/
void RTMPHandlePreTBTTInterrupt(struct rt_rtmp_adapter *pAd)
{
	{
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("RTMPHandlePreTBTTInterrupt...\n"));
		}
	}

}

void RTMPHandleRxCoherentInterrupt(struct rt_rtmp_adapter *pAd)
{
	WPDMA_GLO_CFG_STRUC GloCfg;

	if (pAd == NULL) {
		DBGPRINT(RT_DEBUG_TRACE, ("====> pAd is NULL, return.\n"));
		return;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("==> RTMPHandleRxCoherentInterrupt \n"));

	RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);

	GloCfg.field.EnTXWriteBackDDONE = 0;
	GloCfg.field.EnableRxDMA = 0;
	GloCfg.field.EnableTxDMA = 0;
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);

	RTMPRingCleanUp(pAd, QID_AC_BE);
	RTMPRingCleanUp(pAd, QID_AC_BK);
	RTMPRingCleanUp(pAd, QID_AC_VI);
	RTMPRingCleanUp(pAd, QID_AC_VO);
	RTMPRingCleanUp(pAd, QID_MGMT);
	RTMPRingCleanUp(pAd, QID_RX);

	RTMPEnableRxTx(pAd);

	DBGPRINT(RT_DEBUG_TRACE, ("<== RTMPHandleRxCoherentInterrupt \n"));
}

void *GetPacketFromRxRing(struct rt_rtmp_adapter *pAd,
				 OUT PRT28XX_RXD_STRUC pSaveRxD,
				 OUT BOOLEAN * pbReschedule,
				 IN u32 * pRxPending)
{
	struct rt_rxd * pRxD;
	void *pRxPacket = NULL;
	void *pNewPacket;
	void *AllocVa;
	dma_addr_t AllocPa;
	BOOLEAN bReschedule = FALSE;
	struct rt_rtmp_dmacb *pRxCell;

	RTMP_SEM_LOCK(&pAd->RxRingLock);

	if (*pRxPending == 0) {
		/* Get how may packets had been received */
		RTMP_IO_READ32(pAd, RX_DRX_IDX, &pAd->RxRing.RxDmaIdx);

		if (pAd->RxRing.RxSwReadIdx == pAd->RxRing.RxDmaIdx) {
			/* no more rx packets */
			bReschedule = FALSE;
			goto done;
		}
		/* get rx pending count */
		if (pAd->RxRing.RxDmaIdx > pAd->RxRing.RxSwReadIdx)
			*pRxPending =
			    pAd->RxRing.RxDmaIdx - pAd->RxRing.RxSwReadIdx;
		else
			*pRxPending =
			    pAd->RxRing.RxDmaIdx + RX_RING_SIZE -
			    pAd->RxRing.RxSwReadIdx;

	}

	pRxCell = &pAd->RxRing.Cell[pAd->RxRing.RxSwReadIdx];

	/* Point to Rx indexed rx ring descriptor */
	pRxD = (struct rt_rxd *) pRxCell->AllocVa;

	if (pRxD->DDONE == 0) {
		*pRxPending = 0;
		/* DMAIndx had done but DDONE bit not ready */
		bReschedule = TRUE;
		goto done;
	}

	/* return rx descriptor */
	NdisMoveMemory(pSaveRxD, pRxD, RXD_SIZE);

	pNewPacket =
	    RTMP_AllocateRxPacketBuffer(pAd, RX_BUFFER_AGGRESIZE, FALSE,
					&AllocVa, &AllocPa);

	if (pNewPacket) {
		/* unmap the rx buffer */
		PCI_UNMAP_SINGLE(pAd, pRxCell->DmaBuf.AllocPa,
				 pRxCell->DmaBuf.AllocSize, PCI_DMA_FROMDEVICE);
		pRxPacket = pRxCell->pNdisPacket;

		pRxCell->DmaBuf.AllocSize = RX_BUFFER_AGGRESIZE;
		pRxCell->pNdisPacket = (void *)pNewPacket;
		pRxCell->DmaBuf.AllocVa = AllocVa;
		pRxCell->DmaBuf.AllocPa = AllocPa;
		/* update SDP0 to new buffer of rx packet */
		pRxD->SDP0 = AllocPa;
	} else {
		/*DBGPRINT(RT_DEBUG_TRACE,("No Rx Buffer\n")); */
		pRxPacket = NULL;
		bReschedule = TRUE;
	}

	pRxD->DDONE = 0;

	/* had handled one rx packet */
	*pRxPending = *pRxPending - 1;

	/* update rx descriptor and kick rx */
	INC_RING_INDEX(pAd->RxRing.RxSwReadIdx, RX_RING_SIZE);

	pAd->RxRing.RxCpuIdx =
	    (pAd->RxRing.RxSwReadIdx ==
	     0) ? (RX_RING_SIZE - 1) : (pAd->RxRing.RxSwReadIdx - 1);
	RTMP_IO_WRITE32(pAd, RX_CRX_IDX, pAd->RxRing.RxCpuIdx);

done:
	RTMP_SEM_UNLOCK(&pAd->RxRingLock);
	*pbReschedule = bReschedule;
	return pRxPacket;
}

int MlmeHardTransmitTxRing(struct rt_rtmp_adapter *pAd,
				   u8 QueIdx, void *pPacket)
{
	struct rt_packet_info PacketInfo;
	u8 *pSrcBufVA;
	u32 SrcBufLen;
	struct rt_txd * pTxD;
	struct rt_header_802_11 * pHeader_802_11;
	BOOLEAN bAckRequired, bInsertTimestamp;
	unsigned long SrcBufPA;
	/*u8                 TxBufIdx; */
	u8 MlmeRate;
	unsigned long SwIdx = pAd->TxRing[QueIdx].TxCpuIdx;
	struct rt_txwi * pFirstTxWI;
	/*unsigned long i; */
	/*HTTRANSMIT_SETTING    MlmeTransmit;   //Rate for this MGMT frame. */
	unsigned long FreeNum;
	struct rt_mac_table_entry *pMacEntry = NULL;

	RTMP_QueryPacketInfo(pPacket, &PacketInfo, &pSrcBufVA, &SrcBufLen);

	if (pSrcBufVA == NULL) {
		/* The buffer shouldn't be NULL */
		return NDIS_STATUS_FAILURE;
	}
	/* Make sure MGMT ring resource won't be used by other threads */
	/*NdisAcquireSpinLock(&pAd->TxRingLock); */

	FreeNum = GET_TXRING_FREENO(pAd, QueIdx);

	if (FreeNum == 0) {
		/*NdisReleaseSpinLock(&pAd->TxRingLock); */
		return NDIS_STATUS_FAILURE;
	}

	SwIdx = pAd->TxRing[QueIdx].TxCpuIdx;

	pTxD = (struct rt_txd *) pAd->TxRing[QueIdx].Cell[SwIdx].AllocVa;

	if (pAd->TxRing[QueIdx].Cell[SwIdx].pNdisPacket) {
		DBGPRINT(RT_DEBUG_OFF, ("MlmeHardTransmit Error\n"));
		/*NdisReleaseSpinLock(&pAd->TxRingLock); */
		return NDIS_STATUS_FAILURE;
	}

	{
		/* outgoing frame always wakeup PHY to prevent frame lost */
		/* if (pAd->StaCfg.Psm == PWR_SAVE) */
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
			AsicForceWakeup(pAd, TRUE);
	}
	pFirstTxWI = (struct rt_txwi *) pSrcBufVA;

	pHeader_802_11 = (struct rt_header_802_11 *) (pSrcBufVA + TXWI_SIZE);
	if (pHeader_802_11->Addr1[0] & 0x01) {
		MlmeRate = pAd->CommonCfg.BasicMlmeRate;
	} else {
		MlmeRate = pAd->CommonCfg.MlmeRate;
	}

	if ((pHeader_802_11->FC.Type == BTYPE_DATA) &&
	    (pHeader_802_11->FC.SubType == SUBTYPE_QOS_NULL)) {
		pMacEntry = MacTableLookup(pAd, pHeader_802_11->Addr1);
	}
	/* Verify Mlme rate for a / g bands. */
	if ((pAd->LatchRfRegs.Channel > 14) && (MlmeRate < RATE_6))	/* 11A band */
		MlmeRate = RATE_6;

	/* */
	/* Should not be hard code to set PwrMgmt to 0 (PWR_ACTIVE) */
	/* Snice it's been set to 0 while on MgtMacHeaderInit */
	/* By the way this will cause frame to be send on PWR_SAVE failed. */
	/* */
	/* */
	/* In WMM-UAPSD, mlme frame should be set psm as power saving but probe request frame */
	/* Data-Null packets alse pass through MMRequest in RT2860, however, we hope control the psm bit to pass APSD */
	if (pHeader_802_11->FC.Type != BTYPE_DATA) {
		if ((pHeader_802_11->FC.SubType == SUBTYPE_PROBE_REQ)
		    || !(pAd->CommonCfg.bAPSDCapable
			 && pAd->CommonCfg.APEdcaParm.bAPSDCapable)) {
			pHeader_802_11->FC.PwrMgmt = PWR_ACTIVE;
		} else {
			pHeader_802_11->FC.PwrMgmt =
			    pAd->CommonCfg.bAPSDForcePowerSave;
		}
	}

	bInsertTimestamp = FALSE;
	if (pHeader_802_11->FC.Type == BTYPE_CNTL)	/* must be PS-POLL */
	{
		bAckRequired = FALSE;
	} else			/* BTYPE_MGMT or BTYPE_DATA(must be NULL frame) */
	{
		if (pHeader_802_11->Addr1[0] & 0x01)	/* MULTICAST, BROADCAST */
		{
			bAckRequired = FALSE;
			pHeader_802_11->Duration = 0;
		} else {
			bAckRequired = TRUE;
			pHeader_802_11->Duration =
			    RTMPCalcDuration(pAd, MlmeRate, 14);
			if (pHeader_802_11->FC.SubType == SUBTYPE_PROBE_RSP) {
				bInsertTimestamp = TRUE;
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
		/*NdisReleaseSpinLock(&pAd->TxRingLock); */
		return (NDIS_STATUS_FAILURE);
	}
	/* */
	/* fill scatter-and-gather buffer list into TXD. Internally created NDIS PACKET */
	/* should always has only one ohysical buffer, and the whole frame size equals */
	/* to the first scatter buffer size */
	/* */

	/* Initialize TX Descriptor */
	/* For inter-frame gap, the number is for this frame and next frame */
	/* For MLME rate, we will fix as 2Mb to match other vendor's implement */
/*      pAd->CommonCfg.MlmeTransmit.field.MODE = 1; */

/* management frame doesn't need encryption. so use RESERVED_WCID no matter u are sending to specific wcid or not. */
	/* Only beacon use Nseq=TRUE. So here we use Nseq=FALSE. */
	if (pMacEntry == NULL) {
		RTMPWriteTxWI(pAd, pFirstTxWI, FALSE, FALSE, bInsertTimestamp,
			      FALSE, bAckRequired, FALSE, 0, RESERVED_WCID,
			      (SrcBufLen - TXWI_SIZE), PID_MGMT, 0,
			      (u8)pAd->CommonCfg.MlmeTransmit.field.MCS,
			      IFS_BACKOFF, FALSE, &pAd->CommonCfg.MlmeTransmit);
	} else {
		RTMPWriteTxWI(pAd, pFirstTxWI, FALSE, FALSE,
			      bInsertTimestamp, FALSE, bAckRequired, FALSE,
			      0, pMacEntry->Aid, (SrcBufLen - TXWI_SIZE),
			      pMacEntry->MaxHTPhyMode.field.MCS, 0,
			      (u8)pMacEntry->MaxHTPhyMode.field.MCS,
			      IFS_BACKOFF, FALSE, &pMacEntry->MaxHTPhyMode);
	}

	pAd->TxRing[QueIdx].Cell[SwIdx].pNdisPacket = pPacket;
	pAd->TxRing[QueIdx].Cell[SwIdx].pNextNdisPacket = NULL;
/*      pFirstTxWI->MPDUtotalByteCount = SrcBufLen - TXWI_SIZE; */
	SrcBufPA =
	    PCI_MAP_SINGLE(pAd, pSrcBufVA, SrcBufLen, 0, PCI_DMA_TODEVICE);

	RTMPWriteTxDescriptor(pAd, pTxD, TRUE, FIFO_EDCA);
	pTxD->LastSec0 = 1;
	pTxD->LastSec1 = 1;
	pTxD->SDLen0 = SrcBufLen;
	pTxD->SDLen1 = 0;
	pTxD->SDPtr0 = SrcBufPA;
	pTxD->DMADONE = 0;

	pAd->RalinkCounters.KickTxCount++;
	pAd->RalinkCounters.OneSecTxDoneCount++;

	/* Increase TX_CTX_IDX, but write to register later. */
	INC_RING_INDEX(pAd->TxRing[QueIdx].TxCpuIdx, TX_RING_SIZE);

	RTMP_IO_WRITE32(pAd, TX_CTX_IDX0 + QueIdx * 0x10,
			pAd->TxRing[QueIdx].TxCpuIdx);

	/* Make sure to release MGMT ring resource */
/*      NdisReleaseSpinLock(&pAd->TxRingLock); */

	return NDIS_STATUS_SUCCESS;
}

int MlmeDataHardTransmit(struct rt_rtmp_adapter *pAd,
				 u8 QueIdx, void *pPacket)
{
	if ((pAd->CommonCfg.RadarDetect.RDMode != RD_NORMAL_MODE)
	    ) {
		return NDIS_STATUS_FAILURE;
	}

	return MlmeHardTransmitTxRing(pAd, QueIdx, pPacket);
}

/*
	========================================================================

	Routine Description:
		Calculates the duration which is required to transmit out frames
	with given size and specified rate.

	Arguments:
		pTxD		Pointer to transmit descriptor
		Ack		Setting for Ack requirement bit
		Fragment	Setting for Fragment bit
		RetryMode	Setting for retry mode
		Ifs		Setting for IFS gap
		Rate		Setting for transmit rate
		Service		Setting for service
		Length		Frame length
		TxPreamble	Short or Long preamble when using CCK rates
		QueIdx - 0-3, according to 802.11e/d4.4 June/2003

	Return Value:
		None

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

	========================================================================
*/
void RTMPWriteTxDescriptor(struct rt_rtmp_adapter *pAd,
			   struct rt_txd * pTxD,
			   IN BOOLEAN bWIV, u8 QueueSEL)
{
	/* */
	/* Always use Long preamble before verifiation short preamble functionality works well. */
	/* Todo: remove the following line if short preamble functionality works */
	/* */
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED);

	pTxD->WIV = (bWIV) ? 1 : 0;
	pTxD->QSEL = (QueueSEL);
	/*RT2860c??  fixed using EDCA queue for test...  We doubt Queue1 has problem.  2006-09-26 Jan */
	/*pTxD->QSEL= FIFO_EDCA; */
	pTxD->DMADONE = 0;
}
