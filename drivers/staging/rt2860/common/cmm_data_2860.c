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
#include "../rt_config.h"

extern RTMP_RF_REGS RF2850RegTable[];
extern UCHAR	NUM_OF_2850_CHNL;

USHORT RtmpPCI_WriteTxResource(
	IN	PRTMP_ADAPTER	pAd,
	IN	TX_BLK			*pTxBlk,
	IN	BOOLEAN			bIsLast,
	OUT	USHORT			*FreeNumber)
{

	UCHAR			*pDMAHeaderBufVA;
	USHORT			TxIdx, RetTxIdx;
	PTXD_STRUC		pTxD;
	UINT32			BufBasePaLow;
	PRTMP_TX_RING	pTxRing;
	USHORT			hwHeaderLen;

	//
	// get Tx Ring Resource
	//
	pTxRing = &pAd->TxRing[pTxBlk->QueIdx];
	TxIdx = pAd->TxRing[pTxBlk->QueIdx].TxCpuIdx;
	pDMAHeaderBufVA = (PUCHAR) pTxRing->Cell[TxIdx].DmaBuf.AllocVa;
	BufBasePaLow = RTMP_GetPhysicalAddressLow(pTxRing->Cell[TxIdx].DmaBuf.AllocPa);

	// copy TXINFO + TXWI + WLAN Header + LLC into DMA Header Buffer
	if (pTxBlk->TxFrameType == TX_AMSDU_FRAME)
	{
		hwHeaderLen = pTxBlk->MpduHeaderLen - LENGTH_AMSDU_SUBFRAMEHEAD + pTxBlk->HdrPadLen + LENGTH_AMSDU_SUBFRAMEHEAD;
	}
	else
	{
		hwHeaderLen = pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;
	}
	NdisMoveMemory(pDMAHeaderBufVA, pTxBlk->HeaderBuf, TXINFO_SIZE + TXWI_SIZE + hwHeaderLen);

	pTxRing->Cell[TxIdx].pNdisPacket = pTxBlk->pPacket;
	pTxRing->Cell[TxIdx].pNextNdisPacket = NULL;

	//
	// build Tx Descriptor
	//

	pTxD = (PTXD_STRUC) pTxRing->Cell[TxIdx].AllocVa;
	NdisZeroMemory(pTxD, TXD_SIZE);

	pTxD->SDPtr0 = BufBasePaLow;
	pTxD->SDLen0 = TXINFO_SIZE + TXWI_SIZE + hwHeaderLen; // include padding
	pTxD->SDPtr1 = PCI_MAP_SINGLE(pAd, pTxBlk, 0, 1, PCI_DMA_TODEVICE);;
	pTxD->SDLen1 = pTxBlk->SrcBufLen;
	pTxD->LastSec0 = 0;
	pTxD->LastSec1 = (bIsLast) ? 1 : 0;

	RTMPWriteTxDescriptor(pAd, pTxD, FALSE, FIFO_EDCA);

	RetTxIdx = TxIdx;
	//
	// Update Tx index
	//
	INC_RING_INDEX(TxIdx, TX_RING_SIZE);
	pTxRing->TxCpuIdx = TxIdx;

	*FreeNumber -= 1;

	return RetTxIdx;
}


USHORT RtmpPCI_WriteSingleTxResource(
	IN	PRTMP_ADAPTER	pAd,
	IN	TX_BLK			*pTxBlk,
	IN	BOOLEAN			bIsLast,
	OUT	USHORT			*FreeNumber)
{

	UCHAR			*pDMAHeaderBufVA;
	USHORT			TxIdx, RetTxIdx;
	PTXD_STRUC		pTxD;
	UINT32			BufBasePaLow;
	PRTMP_TX_RING	pTxRing;
	USHORT			hwHeaderLen;

	//
	// get Tx Ring Resource
	//
	pTxRing = &pAd->TxRing[pTxBlk->QueIdx];
	TxIdx = pAd->TxRing[pTxBlk->QueIdx].TxCpuIdx;
	pDMAHeaderBufVA = (PUCHAR) pTxRing->Cell[TxIdx].DmaBuf.AllocVa;
	BufBasePaLow = RTMP_GetPhysicalAddressLow(pTxRing->Cell[TxIdx].DmaBuf.AllocPa);

	// copy TXINFO + TXWI + WLAN Header + LLC into DMA Header Buffer
	hwHeaderLen = pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;

	NdisMoveMemory(pDMAHeaderBufVA, pTxBlk->HeaderBuf, TXINFO_SIZE + TXWI_SIZE + hwHeaderLen);

	pTxRing->Cell[TxIdx].pNdisPacket = pTxBlk->pPacket;
	pTxRing->Cell[TxIdx].pNextNdisPacket = NULL;

	//
	// build Tx Descriptor
	//
	pTxD = (PTXD_STRUC) pTxRing->Cell[TxIdx].AllocVa;

	NdisZeroMemory(pTxD, TXD_SIZE);

	pTxD->SDPtr0 = BufBasePaLow;
	pTxD->SDLen0 = TXINFO_SIZE + TXWI_SIZE + hwHeaderLen; // include padding
	pTxD->SDPtr1 = PCI_MAP_SINGLE(pAd, pTxBlk, 0, 1, PCI_DMA_TODEVICE);;
	pTxD->SDLen1 = pTxBlk->SrcBufLen;
	pTxD->LastSec0 = 0;
	pTxD->LastSec1 = (bIsLast) ? 1 : 0;

	RTMPWriteTxDescriptor(pAd, pTxD, FALSE, FIFO_EDCA);

	RetTxIdx = TxIdx;
	//
	// Update Tx index
	//
	INC_RING_INDEX(TxIdx, TX_RING_SIZE);
	pTxRing->TxCpuIdx = TxIdx;

	*FreeNumber -= 1;

	return RetTxIdx;
}


USHORT RtmpPCI_WriteMultiTxResource(
	IN	PRTMP_ADAPTER	pAd,
	IN	TX_BLK			*pTxBlk,
	IN	UCHAR			frameNum,
	OUT	USHORT			*FreeNumber)
{
	BOOLEAN bIsLast;
	UCHAR			*pDMAHeaderBufVA;
	USHORT			TxIdx, RetTxIdx;
	PTXD_STRUC		pTxD;
	UINT32			BufBasePaLow;
	PRTMP_TX_RING	pTxRing;
	USHORT			hwHdrLen;
	UINT32			firstDMALen;

	bIsLast = ((frameNum == (pTxBlk->TotalFrameNum - 1)) ? 1 : 0);

	//
	// get Tx Ring Resource
	//
	pTxRing = &pAd->TxRing[pTxBlk->QueIdx];
	TxIdx = pAd->TxRing[pTxBlk->QueIdx].TxCpuIdx;
	pDMAHeaderBufVA = (PUCHAR) pTxRing->Cell[TxIdx].DmaBuf.AllocVa;
	BufBasePaLow = RTMP_GetPhysicalAddressLow(pTxRing->Cell[TxIdx].DmaBuf.AllocPa);

	if (frameNum == 0)
	{
		// copy TXINFO + TXWI + WLAN Header + LLC into DMA Header Buffer
		if (pTxBlk->TxFrameType == TX_AMSDU_FRAME)
			//hwHdrLen = ROUND_UP(pTxBlk->MpduHeaderLen-LENGTH_AMSDU_SUBFRAMEHEAD, 4)+LENGTH_AMSDU_SUBFRAMEHEAD;
			hwHdrLen = pTxBlk->MpduHeaderLen - LENGTH_AMSDU_SUBFRAMEHEAD + pTxBlk->HdrPadLen + LENGTH_AMSDU_SUBFRAMEHEAD;
		else if (pTxBlk->TxFrameType == TX_RALINK_FRAME)
			//hwHdrLen = ROUND_UP(pTxBlk->MpduHeaderLen-LENGTH_ARALINK_HEADER_FIELD, 4)+LENGTH_ARALINK_HEADER_FIELD;
			hwHdrLen = pTxBlk->MpduHeaderLen - LENGTH_ARALINK_HEADER_FIELD + pTxBlk->HdrPadLen + LENGTH_ARALINK_HEADER_FIELD;
		else
			//hwHdrLen = ROUND_UP(pTxBlk->MpduHeaderLen, 4);
			hwHdrLen = pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;

		firstDMALen = TXINFO_SIZE + TXWI_SIZE + hwHdrLen;
	}
	else
	{
		firstDMALen = pTxBlk->MpduHeaderLen;
	}

	NdisMoveMemory(pDMAHeaderBufVA, pTxBlk->HeaderBuf, firstDMALen);

	pTxRing->Cell[TxIdx].pNdisPacket = pTxBlk->pPacket;
	pTxRing->Cell[TxIdx].pNextNdisPacket = NULL;

	//
	// build Tx Descriptor
	//
	pTxD = (PTXD_STRUC) pTxRing->Cell[TxIdx].AllocVa;

	NdisZeroMemory(pTxD, TXD_SIZE);

	pTxD->SDPtr0 = BufBasePaLow;
	pTxD->SDLen0 = firstDMALen; // include padding
	pTxD->SDPtr1 = PCI_MAP_SINGLE(pAd, pTxBlk, 0, 1, PCI_DMA_TODEVICE);;
	pTxD->SDLen1 = pTxBlk->SrcBufLen;
	pTxD->LastSec0 = 0;
	pTxD->LastSec1 = (bIsLast) ? 1 : 0;

	RTMPWriteTxDescriptor(pAd, pTxD, FALSE, FIFO_EDCA);

	RetTxIdx = TxIdx;
	//
	// Update Tx index
	//
	INC_RING_INDEX(TxIdx, TX_RING_SIZE);
	pTxRing->TxCpuIdx = TxIdx;

	*FreeNumber -= 1;

	return RetTxIdx;

}


VOID RtmpPCI_FinalWriteTxResource(
	IN	PRTMP_ADAPTER	pAd,
	IN	TX_BLK			*pTxBlk,
	IN	USHORT			totalMPDUSize,
	IN	USHORT			FirstTxIdx)
{

	PTXWI_STRUC		pTxWI;
	PRTMP_TX_RING	pTxRing;

	//
	// get Tx Ring Resource
	//
	pTxRing = &pAd->TxRing[pTxBlk->QueIdx];
	pTxWI = (PTXWI_STRUC) pTxRing->Cell[FirstTxIdx].DmaBuf.AllocVa;
	pTxWI->MPDUtotalByteCount = totalMPDUSize;
}


VOID RtmpPCIDataLastTxIdx(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			QueIdx,
	IN	USHORT			LastTxIdx)
{
	PTXD_STRUC		pTxD;
	PRTMP_TX_RING	pTxRing;

	//
	// get Tx Ring Resource
	//
	pTxRing = &pAd->TxRing[QueIdx];

	//
	// build Tx Descriptor
	//
	pTxD = (PTXD_STRUC) pTxRing->Cell[LastTxIdx].AllocVa;

	pTxD->LastSec1 = 1;
}


USHORT	RtmpPCI_WriteFragTxResource(
	IN	PRTMP_ADAPTER	pAd,
	IN	TX_BLK			*pTxBlk,
	IN	UCHAR			fragNum,
	OUT	USHORT			*FreeNumber)
{
	UCHAR			*pDMAHeaderBufVA;
	USHORT			TxIdx, RetTxIdx;
	PTXD_STRUC		pTxD;
	UINT32			BufBasePaLow;
	PRTMP_TX_RING	pTxRing;
	USHORT			hwHeaderLen;
	UINT32			firstDMALen;

	//
	// Get Tx Ring Resource
	//
	pTxRing = &pAd->TxRing[pTxBlk->QueIdx];
	TxIdx = pAd->TxRing[pTxBlk->QueIdx].TxCpuIdx;
	pDMAHeaderBufVA = (PUCHAR) pTxRing->Cell[TxIdx].DmaBuf.AllocVa;
	BufBasePaLow = RTMP_GetPhysicalAddressLow(pTxRing->Cell[TxIdx].DmaBuf.AllocPa);

	//
	// Copy TXINFO + TXWI + WLAN Header + LLC into DMA Header Buffer
	//
	hwHeaderLen = pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;

	firstDMALen = TXINFO_SIZE + TXWI_SIZE + hwHeaderLen;
	NdisMoveMemory(pDMAHeaderBufVA, pTxBlk->HeaderBuf, firstDMALen);


	//
	// Build Tx Descriptor
	//
	pTxD = (PTXD_STRUC) pTxRing->Cell[TxIdx].AllocVa;

	NdisZeroMemory(pTxD, TXD_SIZE);

	if (fragNum == pTxBlk->TotalFragNum)
	{
		pTxRing->Cell[TxIdx].pNdisPacket = pTxBlk->pPacket;
		pTxRing->Cell[TxIdx].pNextNdisPacket = NULL;
	}

	pTxD->SDPtr0 = BufBasePaLow;
	pTxD->SDLen0 = firstDMALen; // include padding
	pTxD->SDPtr1 = PCI_MAP_SINGLE(pAd, pTxBlk, 0, 1, PCI_DMA_TODEVICE);
	pTxD->SDLen1 = pTxBlk->SrcBufLen;
	pTxD->LastSec0 = 0;
	pTxD->LastSec1 = 1;

	RTMPWriteTxDescriptor(pAd, pTxD, FALSE, FIFO_EDCA);

	RetTxIdx = TxIdx;
	pTxBlk->Priv += pTxBlk->SrcBufLen;

	//
	// Update Tx index
	//
	INC_RING_INDEX(TxIdx, TX_RING_SIZE);
	pTxRing->TxCpuIdx = TxIdx;

	*FreeNumber -= 1;

	return RetTxIdx;

}

/*
	Must be run in Interrupt context
	This function handle PCI specific TxDesc and cpu index update and kick the packet out.
 */
int RtmpPCIMgmtKickOut(
	IN RTMP_ADAPTER 	*pAd,
	IN UCHAR 			QueIdx,
	IN PNDIS_PACKET		pPacket,
	IN PUCHAR			pSrcBufVA,
	IN UINT 			SrcBufLen)
{
	PTXD_STRUC		pTxD;
	ULONG			SwIdx = pAd->MgmtRing.TxCpuIdx;

	pTxD  = (PTXD_STRUC) pAd->MgmtRing.Cell[SwIdx].AllocVa;
	if (!pTxD)
		return 0;

	pAd->MgmtRing.Cell[SwIdx].pNdisPacket = pPacket;
	pAd->MgmtRing.Cell[SwIdx].pNextNdisPacket = NULL;

	RTMPWriteTxDescriptor(pAd, pTxD, TRUE, FIFO_MGMT);
	pTxD->LastSec0 = 1;
	pTxD->LastSec1 = 1;
	pTxD->DMADONE = 0;
	pTxD->SDLen1 = 0;
	pTxD->SDPtr0 = PCI_MAP_SINGLE(pAd, pSrcBufVA, SrcBufLen, 0, PCI_DMA_TODEVICE);;
	pTxD->SDLen0 = SrcBufLen;

	pAd->RalinkCounters.KickTxCount++;
	pAd->RalinkCounters.OneSecTxDoneCount++;

	// Increase TX_CTX_IDX, but write to register later.
	INC_RING_INDEX(pAd->MgmtRing.TxCpuIdx, MGMT_RING_SIZE);

	RTMP_IO_WRITE32(pAd, TX_MGMTCTX_IDX,  pAd->MgmtRing.TxCpuIdx);

	return 0;
}

/*
	========================================================================

	Routine Description:
		Check Rx descriptor, return NDIS_STATUS_FAILURE if any error dound

	Arguments:
		pRxD		Pointer to the Rx descriptor

	Return Value:
		NDIS_STATUS_SUCCESS 	No err
		NDIS_STATUS_FAILURE 	Error

	Note:

	========================================================================
*/
NDIS_STATUS RTMPCheckRxError(
	IN	PRTMP_ADAPTER		pAd,
	IN	PHEADER_802_11		pHeader,
	IN	PRXWI_STRUC 		pRxWI,
	IN  PRT28XX_RXD_STRUC 	pRxD)
{
	PCIPHER_KEY pWpaKey;
	INT dBm;

	// Phy errors & CRC errors
	if (/*(pRxD->PhyErr) ||*/ (pRxD->Crc))
	{
		// Check RSSI for Noise Hist statistic collection.
		dBm = (INT) (pRxWI->RSSI0) - pAd->BbpRssiToDbmDelta;
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

		return(NDIS_STATUS_FAILURE);
	}

	// Add Rx size to channel load counter, we should ignore error counts
	pAd->StaCfg.CLBusyBytes += (pRxD->SDL0 + 14);

	// Drop ToDs promiscous frame, it is opened due to CCX 2 channel load statistics
	if (pHeader != NULL)
	{
		if (pHeader->FC.ToDs)
		{
			return(NDIS_STATUS_FAILURE);
		}
	}

	// Drop not U2M frames, cant's drop here because we will drop beacon in this case
	// I am kind of doubting the U2M bit operation
	// if (pRxD->U2M == 0)
	//	return(NDIS_STATUS_FAILURE);

	// drop decyption fail frame
	if (pRxD->CipherErr)
	{
		if (pRxD->CipherErr == 2)
			{DBGPRINT_RAW(RT_DEBUG_TRACE,("pRxD ERROR: ICV ok but MICErr "));}
		else if (pRxD->CipherErr == 1)
			{DBGPRINT_RAW(RT_DEBUG_TRACE,("pRxD ERROR: ICV Err "));}
		else if (pRxD->CipherErr == 3)
			DBGPRINT_RAW(RT_DEBUG_TRACE,("pRxD ERROR: Key not valid "));

        if (((pRxD->CipherErr & 1) == 1) && pAd->CommonCfg.bWirelessEvent && INFRA_ON(pAd))
            RTMPSendWirelessEvent(pAd, IW_ICV_ERROR_EVENT_FLAG, pAd->MacTab.Content[BSSID_WCID].Addr, BSS0, 0);

		DBGPRINT_RAW(RT_DEBUG_TRACE,(" %d (len=%d, Mcast=%d, MyBss=%d, Wcid=%d, KeyId=%d)\n",
			pRxD->CipherErr,
			pRxD->SDL0,
			pRxD->Mcast | pRxD->Bcast,
			pRxD->MyBss,
			pRxWI->WirelessCliID,
			pRxWI->KeyIndex));

		//
		// MIC Error
		//
		if (pRxD->CipherErr == 2)
		{
			pWpaKey = &pAd->SharedKey[BSS0][pRxWI->KeyIndex];

            if (pAd->StaCfg.WpaSupplicantUP)
                WpaSendMicFailureToWpaSupplicant(pAd,
                                   (pWpaKey->Type == PAIRWISEKEY) ? TRUE:FALSE);
            else
			    RTMPReportMicError(pAd, pWpaKey);

            if (((pRxD->CipherErr & 2) == 2) && pAd->CommonCfg.bWirelessEvent && INFRA_ON(pAd))
                RTMPSendWirelessEvent(pAd, IW_MIC_ERROR_EVENT_FLAG, pAd->MacTab.Content[BSSID_WCID].Addr, BSS0, 0);

			DBGPRINT_RAW(RT_DEBUG_ERROR,("Rx MIC Value error\n"));
		}

		if (pHeader == NULL)
			return(NDIS_STATUS_SUCCESS);

		return(NDIS_STATUS_FAILURE);
	}

	return(NDIS_STATUS_SUCCESS);
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
VOID RT28xxPciAsicRadioOff(
	IN PRTMP_ADAPTER    pAd,
	IN UCHAR            Level,
	IN USHORT           TbttNumToNextWakeUp)
{
	WPDMA_GLO_CFG_STRUC	DmaCfg;
	UCHAR		i, tempBBP_R3 = 0;
	BOOLEAN		brc = FALSE, Cancelled;
    UINT32		TbTTTime = 0;
	UINT32		PsPollTime = 0, MACValue;
    ULONG		BeaconPeriodTime;
    UINT32		RxDmaIdx, RxCpuIdx;
	DBGPRINT(RT_DEBUG_TRACE, ("AsicRadioOff ===> TxCpuIdx = %d, TxDmaIdx = %d. RxCpuIdx = %d, RxDmaIdx = %d.\n", pAd->TxRing[0].TxCpuIdx, pAd->TxRing[0].TxDmaIdx, pAd->RxRing.RxCpuIdx, pAd->RxRing.RxDmaIdx));

    // Check Rx DMA busy status, if more than half is occupied, give up this radio off.
	RTMP_IO_READ32(pAd, RX_DRX_IDX , &RxDmaIdx);
	RTMP_IO_READ32(pAd, RX_CRX_IDX , &RxCpuIdx);
	if ((RxDmaIdx > RxCpuIdx) && ((RxDmaIdx - RxCpuIdx) > RX_RING_SIZE/3))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("AsicRadioOff ===> return1. RxDmaIdx = %d ,  RxCpuIdx = %d. \n", RxDmaIdx, RxCpuIdx));
		return;
	}
	else if ((RxCpuIdx >= RxDmaIdx) && ((RxCpuIdx - RxDmaIdx) < RX_RING_SIZE/3))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("AsicRadioOff ===> return2.  RxCpuIdx = %d. RxDmaIdx = %d ,  \n", RxCpuIdx, RxDmaIdx));
		return;
	}

    // Once go into this function, disable tx because don't want too many packets in queue to prevent HW stops.
	RTMP_SET_PSFLAG(pAd, fRTMP_PS_DISABLE_TX);

	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE))
	{
	    RTMPCancelTimer(&pAd->Mlme.RadioOnOffTimer,	&Cancelled);
	    RTMPCancelTimer(&pAd->Mlme.PsPollTimer,	&Cancelled);

	    if (Level == DOT11POWERSAVE)
		{
			RTMP_IO_READ32(pAd, TBTT_TIMER, &TbTTTime);
			TbTTTime &= 0x1ffff;
			// 00. check if need to do sleep in this DTIM period.   If next beacon will arrive within 30ms , ...doesn't necessarily sleep.
			// TbTTTime uint = 64us, LEAD_TIME unit = 1024us, PsPollTime unit = 1ms
	        if  (((64*TbTTTime) <((LEAD_TIME*1024) + 40000)) && (TbttNumToNextWakeUp == 0))
			{
				DBGPRINT(RT_DEBUG_TRACE, ("TbTTTime = 0x%x , give up this sleep. \n", TbTTTime));
	            OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
				RTMP_CLEAR_PSFLAG(pAd, fRTMP_PS_DISABLE_TX);
				return;
			}
			else
			{
				PsPollTime = (64*TbTTTime- LEAD_TIME*1024)/1000;
				PsPollTime -= 3;

	            BeaconPeriodTime = pAd->CommonCfg.BeaconPeriod*102/100;
				if (TbttNumToNextWakeUp > 0)
					PsPollTime += ((TbttNumToNextWakeUp -1) * BeaconPeriodTime);

	            pAd->Mlme.bPsPollTimerRunning = TRUE;
				RTMPSetTimer(&pAd->Mlme.PsPollTimer, PsPollTime);
			}
		}
	}

    // 0. Disable Tx DMA.
	RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &DmaCfg.word);
	DmaCfg.field.EnableTxDMA = 0;
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, DmaCfg.word);

	// 1. Wait DMA not busy
	i = 0;
	do
	{
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &DmaCfg.word);
		if ((DmaCfg.field.TxDMABusy == 0) && (DmaCfg.field.RxDMABusy == 0))
			break;
		RTMPusecDelay(20);
		i++;
	}while(i < 50);

	if (i >= 50)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("DMA keeps busy.  return on RT28xxPciAsicRadioOff ()\n"));
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &DmaCfg.word);
		DmaCfg.field.EnableTxDMA = 1;
		RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, DmaCfg.word);
		pAd->CheckDmaBusyCount++;
		return;
	}
	else
	{
		pAd->CheckDmaBusyCount = 0;
	}

    RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF);

    // Set to 1R.
	if (pAd->Antenna.field.RxPath > 1)
	{
		tempBBP_R3 = (pAd->StaCfg.BBPR3 & 0xE7);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, tempBBP_R3);
	}

	// In Radio Off, we turn off RF clk, So now need to call ASICSwitchChannel again.
	if (INFRA_ON(pAd) && (pAd->CommonCfg.CentralChannel != pAd->CommonCfg.Channel)
		&& (pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth == BW_40))
	{
		// Must using 40MHz.
		AsicTurnOffRFClk(pAd, pAd->CommonCfg.CentralChannel);
	}
	else
	{
		// Must using 20MHz.
		AsicTurnOffRFClk(pAd, pAd->CommonCfg.Channel);
	}

	if (Level != RTMP_HALT)
	{
		// Change Interrupt bitmask.
		RTMP_IO_WRITE32(pAd, INT_MASK_CSR, AutoWakeupInt);
	}
	else
	{
		NICDisableInterrupt(pAd);
	}

    RTMP_IO_WRITE32(pAd, RX_CRX_IDX, pAd->RxRing.RxCpuIdx);
	// Disable MAC Rx
	RTMP_IO_READ32(pAd, MAC_SYS_CTRL , &MACValue);
	MACValue &= 0xf7;
	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL , MACValue);

	//  2. Send Sleep command
	RTMP_IO_WRITE32(pAd, H2M_MAILBOX_STATUS, 0xffffffff);
	RTMP_IO_WRITE32(pAd, H2M_MAILBOX_CID, 0xffffffff);
	// send POWER-SAVE command to MCU. high-byte = 1 save power as much as possible. high byte = 0 save less power
	AsicSendCommandToMcu(pAd, 0x30, PowerSafeCID, 0xff, 0x1);
	//  2-1. Wait command success
	// Status = 1 : success, Status = 2, already sleep, Status = 3, Maybe MAC is busy so can't finish this task.
	brc = AsicCheckCommanOk(pAd, PowerSafeCID);

    if (brc == FALSE)
    {
        // try again
    	AsicSendCommandToMcu(pAd, 0x30, PowerSafeCID, 0xff, 0x01);   // send POWER-SAVE command to MCU. Timeout unit:40us.
    	//RTMPusecDelay(200);
    	brc = AsicCheckCommanOk(pAd, PowerSafeCID);
    }

	//  3. After 0x30 command is ok, send radio off command. lowbyte = 0 for power safe.
	// If 0x30 command is not ok this time, we can ignore 0x35 command. It will make sure not cause firmware'r problem.
	if ((Level == DOT11POWERSAVE) && (brc == TRUE))
	{
		AsicSendCommandToMcu(pAd, 0x35, PowerRadioOffCID, 0, 0x00);	// lowbyte = 0 means to do power safe, NOT turn off radio.
	 	//  3-1. Wait command success
	 	AsicCheckCommanOk(pAd, PowerRadioOffCID);
	}
	else if (brc == TRUE)
	{
		AsicSendCommandToMcu(pAd, 0x35, PowerRadioOffCID, 1, 0x00);	// lowbyte = 0 means to do power safe, NOT turn off radio.
	 	//  3-1. Wait command success
	 	AsicCheckCommanOk(pAd, PowerRadioOffCID);
	}

    // Wait DMA not busy
	i = 0;
	do
	{
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &DmaCfg.word);
		if ((DmaCfg.field.RxDMABusy == 0) && (DmaCfg.field.TxDMABusy == 0))
			break;
		RTMPusecDelay(20);
		i++;
	}while(i < 50);

	if (i >= 50)
	{
		pAd->CheckDmaBusyCount++;
		DBGPRINT(RT_DEBUG_TRACE, ("DMA Rx keeps busy.  on RT28xxPciAsicRadioOff ()\n"));
	}
	else
	{
		pAd->CheckDmaBusyCount = 0;
	}

	if (Level == DOT11POWERSAVE)
	{
		AUTO_WAKEUP_STRUC	AutoWakeupCfg;
		//RTMPSetTimer(&pAd->Mlme.PsPollTimer, 90);

		// we have decided to SLEEP, so at least do it for a BEACON period.
		if (TbttNumToNextWakeUp == 0)
			TbttNumToNextWakeUp = 1;

		AutoWakeupCfg.word = 0;
		RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);

		// 1. Set auto wake up timer.
		AutoWakeupCfg.field.NumofSleepingTbtt = TbttNumToNextWakeUp - 1;
		AutoWakeupCfg.field.EnableAutoWakeup = 1;
		AutoWakeupCfg.field.AutoLeadTime = LEAD_TIME;
		RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);
	}

	//  4-1. If it's to disable our device. Need to restore PCI Configuration Space to its original value.
	if (Level == RTMP_HALT)
	{
		if ((brc == TRUE) && (i < 50))
			RTMPPCIeLinkCtrlSetting(pAd, 0);
	}
	//  4. Set PCI configuration Space Link Comtrol fields.  Only Radio Off needs to call this function
	else
	{
		if ((brc == TRUE) && (i < 50))
			RTMPPCIeLinkCtrlSetting(pAd, 3);
	}

	RTMP_CLEAR_PSFLAG(pAd, fRTMP_PS_DISABLE_TX);
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
BOOLEAN RT28xxPciAsicRadioOn(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR     Level)
{
    WPDMA_GLO_CFG_STRUC	DmaCfg;
	BOOLEAN				Cancelled, brv = TRUE;
    UINT32			    MACValue;

	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE))
	{
	    pAd->Mlme.bPsPollTimerRunning = FALSE;
		RTMPCancelTimer(&pAd->Mlme.PsPollTimer,	&Cancelled);
		if ((Level == GUIRADIO_OFF) || (Level == GUI_IDLE_POWER_SAVE)
		|| (RTMP_TEST_PSFLAG(pAd, fRTMP_PS_SET_PCI_CLK_OFF_COMMAND)))
		{
			DBGPRINT(RT_DEBUG_TRACE, ("RT28xxPciAsicRadioOn ()\n"));
			// 1. Set PCI Link Control in Configuration Space.
			RTMPPCIeLinkCtrlValueRestore(pAd, RESTORE_WAKEUP);
			RTMPusecDelay(6000);
		}
	}

    pAd->bPCIclkOff = FALSE;
	RTMP_IO_WRITE32(pAd, PBF_SYS_CTRL, 0x3a80);
	// 2. Send wake up command.
	AsicSendCommandToMcu(pAd, 0x31, PowerWakeCID, 0x00, 0x02);

	// 2-1. wait command ok.
	brv = AsicCheckCommanOk(pAd, PowerWakeCID);
    if (brv)
    {
    	NICEnableInterrupt(pAd);

    	// 3. Enable Tx DMA.
    	RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &DmaCfg.word);
    	DmaCfg.field.EnableTxDMA = 1;
        DmaCfg.field.EnableRxDMA = 1;
    	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, DmaCfg.word);

        // Eable MAC Rx
    	RTMP_IO_READ32(pAd, MAC_SYS_CTRL , &MACValue);
    	MACValue |= 0x8;
    	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL , MACValue);

    	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF);
    	if (Level == GUI_IDLE_POWER_SAVE)
    	{
    		// In Radio Off, we turn off RF clk, So now need to call ASICSwitchChannel again.
    		if (INFRA_ON(pAd) && (pAd->CommonCfg.CentralChannel != pAd->CommonCfg.Channel)
    			&& (pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth == BW_40))
    		{
    			// Must using 40MHz.
    			AsicSwitchChannel(pAd, pAd->CommonCfg.CentralChannel, FALSE);
    			AsicLockChannel(pAd, pAd->CommonCfg.CentralChannel);
    		}
    		else
    		{
    			// Must using 20MHz.
    			AsicSwitchChannel(pAd, pAd->CommonCfg.Channel, FALSE);
    			AsicLockChannel(pAd, pAd->CommonCfg.Channel);
    		}
    	}
        return TRUE;
    }
    else
        return FALSE;
}

VOID RT28xxPciStaAsicForceWakeup(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR    	 Level)
{
    AUTO_WAKEUP_STRUC	AutoWakeupCfg;

    if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WAKEUP_NOW))
    {
        DBGPRINT(RT_DEBUG_TRACE, ("waking up now!\n"));
        return;
    }

    OPSTATUS_SET_FLAG(pAd, fOP_STATUS_WAKEUP_NOW);
	RTMP_CLEAR_PSFLAG(pAd, fRTMP_PS_GO_TO_SLEEP_NOW);

    if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE))
    {
        // Support PCIe Advance Power Save
    	if (((Level == FROM_TX) && (pAd->Mlme.bPsPollTimerRunning == TRUE)) ||
			(Level == RTMP_HALT))
    	{
            pAd->Mlme.bPsPollTimerRunning = FALSE;
    		RTMPPCIeLinkCtrlValueRestore(pAd, RESTORE_WAKEUP);
    		RTMPusecDelay(5000);
            DBGPRINT(RT_DEBUG_TRACE, ("=======AsicForceWakeup===bFromTx\n"));
    	}

		AutoWakeupCfg.word = 0;
		RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);

		// If this is called from Halt. ALWAYS force wakeup!!!
		if (Level == RTMP_HALT)
		{
			RT28xxPciAsicRadioOn(pAd, RTMP_HALT);
		}
		else
		{
			if (RT28xxPciAsicRadioOn(pAd, DOT11POWERSAVE))
			{
				// In Radio Off, we turn off RF clk, So now need to call ASICSwitchChannel again.
				if (INFRA_ON(pAd) && (pAd->CommonCfg.CentralChannel != pAd->CommonCfg.Channel)
					&& (pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth == BW_40))
				{
					// Must using 40MHz.
					AsicSwitchChannel(pAd, pAd->CommonCfg.CentralChannel, FALSE);
					AsicLockChannel(pAd, pAd->CommonCfg.CentralChannel);
				}
				else
				{
					// Must using 20MHz.
					AsicSwitchChannel(pAd, pAd->CommonCfg.Channel, FALSE);
					AsicLockChannel(pAd, pAd->CommonCfg.Channel);
				}
			}
		}
    }
    else
    {
        // PCI, 2860-PCIe
        AsicSendCommandToMcu(pAd, 0x31, 0xff, 0x00, 0x00);
		AutoWakeupCfg.word = 0;
		RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);
    }

    OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
    OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_WAKEUP_NOW);
    DBGPRINT(RT_DEBUG_TRACE, ("<=======RT28xxPciStaAsicForceWakeup\n"));
}

VOID RT28xxPciStaAsicSleepThenAutoWakeup(
	IN PRTMP_ADAPTER pAd,
	IN USHORT TbttNumToNextWakeUp)
{
    if (pAd->StaCfg.bRadio == FALSE)
	{
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
		return;
	}
    if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE))
    {
    	ULONG	Now = 0;
        if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WAKEUP_NOW))
        {
            DBGPRINT(RT_DEBUG_TRACE, ("waking up now!\n"));
            OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
            return;
        }

		NdisGetSystemUpTime(&Now);
		// If last send NULL fram time is too close to this receiving beacon (within 8ms), don't go to sleep for this DTM.
		// Because Some AP can't queuing outgoing frames immediately.
		if (((pAd->Mlme.LastSendNULLpsmTime + 8) >= Now) && (pAd->Mlme.LastSendNULLpsmTime <= Now))
		{
			DBGPRINT(RT_DEBUG_TRACE, ("Now = %lu, LastSendNULLpsmTime=%lu :  RxCountSinceLastNULL = %lu. \n", Now, pAd->Mlme.LastSendNULLpsmTime, pAd->RalinkCounters.RxCountSinceLastNULL));
			return;
		}
		else if ((pAd->RalinkCounters.RxCountSinceLastNULL > 0) && ((pAd->Mlme.LastSendNULLpsmTime + pAd->CommonCfg.BeaconPeriod) >= Now))
		{
			DBGPRINT(RT_DEBUG_TRACE, ("Now = %lu, LastSendNULLpsmTime=%lu: RxCountSinceLastNULL = %lu > 0 \n", Now, pAd->Mlme.LastSendNULLpsmTime,  pAd->RalinkCounters.RxCountSinceLastNULL));
			return;
		}

        RT28xxPciAsicRadioOff(pAd, DOT11POWERSAVE, TbttNumToNextWakeUp);
    }
    else
    {
        AUTO_WAKEUP_STRUC	AutoWakeupCfg;
        // we have decided to SLEEP, so at least do it for a BEACON period.
        if (TbttNumToNextWakeUp == 0)
            TbttNumToNextWakeUp = 1;

        AutoWakeupCfg.word = 0;
        RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);
        AutoWakeupCfg.field.NumofSleepingTbtt = TbttNumToNextWakeUp - 1;
        AutoWakeupCfg.field.EnableAutoWakeup = 1;
        AutoWakeupCfg.field.AutoLeadTime = 5;
        RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);
        AsicSendCommandToMcu(pAd, 0x30, 0xff, 0xff, 0x00);   // send POWER-SAVE command to MCU. Timeout 40us.
        DBGPRINT(RT_DEBUG_TRACE, ("<-- %s, TbttNumToNextWakeUp=%d \n", __func__, TbttNumToNextWakeUp));
    }
    OPSTATUS_SET_FLAG(pAd, fOP_STATUS_DOZE);
}

VOID PsPollWakeExec(
	IN PVOID SystemSpecific1,
	IN PVOID FunctionContext,
	IN PVOID SystemSpecific2,
	IN PVOID SystemSpecific3)
{
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *)FunctionContext;
	unsigned long flags;

    DBGPRINT(RT_DEBUG_TRACE,("-->PsPollWakeExec \n"));
	RTMP_INT_LOCK(&pAd->irq_lock, flags);
    if (pAd->Mlme.bPsPollTimerRunning)
    {
	    RTMPPCIeLinkCtrlValueRestore(pAd, RESTORE_WAKEUP);
    }
    pAd->Mlme.bPsPollTimerRunning = FALSE;
	RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
}

VOID  RadioOnExec(
	IN PVOID SystemSpecific1,
	IN PVOID FunctionContext,
	IN PVOID SystemSpecific2,
	IN PVOID SystemSpecific3)
{
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *)FunctionContext;
	WPDMA_GLO_CFG_STRUC	DmaCfg;
	BOOLEAN				Cancelled;

	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
	{
		DBGPRINT(RT_DEBUG_TRACE,("-->RadioOnExec() return on fOP_STATUS_DOZE == TRUE; \n"));
		RTMPSetTimer(&pAd->Mlme.RadioOnOffTimer, 10);
		return;
	}

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
	{
		DBGPRINT(RT_DEBUG_TRACE,("-->RadioOnExec() return on SCAN_IN_PROGRESS; \n"));
		RTMPSetTimer(&pAd->Mlme.RadioOnOffTimer, 10);
		return;
	}
    pAd->Mlme.bPsPollTimerRunning = FALSE;
	RTMPCancelTimer(&pAd->Mlme.PsPollTimer,	&Cancelled);
	if (pAd->StaCfg.bRadio == TRUE)
	{
		pAd->bPCIclkOff = FALSE;
        RTMPRingCleanUp(pAd, QID_AC_BK);
		RTMPRingCleanUp(pAd, QID_AC_BE);
		RTMPRingCleanUp(pAd, QID_AC_VI);
		RTMPRingCleanUp(pAd, QID_AC_VO);
		RTMPRingCleanUp(pAd, QID_HCCA);
		RTMPRingCleanUp(pAd, QID_MGMT);
		RTMPRingCleanUp(pAd, QID_RX);

		// 2. Send wake up command.
		AsicSendCommandToMcu(pAd, 0x31, PowerWakeCID, 0x00, 0x02);
		// 2-1. wait command ok.
		AsicCheckCommanOk(pAd, PowerWakeCID);

		// When PCI clock is off, don't want to service interrupt. So when back to clock on, enable interrupt.
		NICEnableInterrupt(pAd);

		// 3. Enable Tx DMA.
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &DmaCfg.word);
		DmaCfg.field.EnableTxDMA = 1;
		RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, DmaCfg.word);

		// In Radio Off, we turn off RF clk, So now need to call ASICSwitchChannel again.
		if (INFRA_ON(pAd) && (pAd->CommonCfg.CentralChannel != pAd->CommonCfg.Channel)
			&& (pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth == BW_40))
		{
			// Must using 40MHz.
			AsicSwitchChannel(pAd, pAd->CommonCfg.CentralChannel, FALSE);
			AsicLockChannel(pAd, pAd->CommonCfg.CentralChannel);
		}
		else
		{
			// Must using 20MHz.
			AsicSwitchChannel(pAd, pAd->CommonCfg.Channel, FALSE);
			AsicLockChannel(pAd, pAd->CommonCfg.Channel);
		}

		// Clear Radio off flag
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

		// Set LED
		RTMPSetLED(pAd, LED_RADIO_ON);

        if (pAd->StaCfg.Psm == PWR_ACTIVE)
        {
    		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, pAd->StaCfg.BBPR3);
        }
	}
	else
	{
		RT28xxPciAsicRadioOff(pAd, GUIRADIO_OFF, 0);
	}
}

VOID RT28xxPciMlmeRadioOn(
	IN PRTMP_ADAPTER pAd)
{
    if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF))
   		return;

    DBGPRINT(RT_DEBUG_TRACE,("%s===>\n", __func__));

    if ((pAd->OpMode == OPMODE_AP) ||
        ((pAd->OpMode == OPMODE_STA) && (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE))))
    {
    	NICResetFromError(pAd);

    	/*
    	RTMPRingCleanUp(pAd, QID_AC_BK);
    	RTMPRingCleanUp(pAd, QID_AC_BE);
    	RTMPRingCleanUp(pAd, QID_AC_VI);
    	RTMPRingCleanUp(pAd, QID_AC_VO);
    	RTMPRingCleanUp(pAd, QID_HCCA);
    	RTMPRingCleanUp(pAd, QID_MGMT);
    	RTMPRingCleanUp(pAd, QID_RX);
		*/

    	// Enable Tx/Rx
    	RTMPEnableRxTx(pAd);

    	// Clear Radio off flag
    	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

	    // Set LED
	    RTMPSetLED(pAd, LED_RADIO_ON);
    }

    if ((pAd->OpMode == OPMODE_STA) &&
        (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE)))
    {
        BOOLEAN		Cancelled;

    	RTMPPCIeLinkCtrlValueRestore(pAd, RESTORE_WAKEUP);

        pAd->Mlme.bPsPollTimerRunning = FALSE;
    	RTMPCancelTimer(&pAd->Mlme.PsPollTimer,	&Cancelled);
    	RTMPCancelTimer(&pAd->Mlme.RadioOnOffTimer,	&Cancelled);
    	RTMPSetTimer(&pAd->Mlme.RadioOnOffTimer, 10);
    }
}

VOID RT28xxPciMlmeRadioOFF(
	IN PRTMP_ADAPTER pAd)
{
    WPDMA_GLO_CFG_STRUC	GloCfg;
	UINT32	i;

	if (pAd->StaCfg.bRadio == TRUE)
	{
		DBGPRINT(RT_DEBUG_TRACE,("-->MlmeRadioOff() return on bRadio == TRUE; \n"));
		return;
	}

    if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF))
    	return;

    DBGPRINT(RT_DEBUG_TRACE,("%s===>\n", __func__));

	// Set LED
	RTMPSetLED(pAd, LED_RADIO_OFF);

    {
    	BOOLEAN		Cancelled;

    	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
    	{
			RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &Cancelled);
			RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS);
    	}

		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE))
        {
            BOOLEAN Cancelled;

			// Always radio on since the NIC needs to set the MCU command (LED_RADIO_OFF).
			if ((pAd->OpMode == OPMODE_STA) && 
			     (IDLE_ON(pAd)) && 
			     (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF)))
			{
				RT28xxPciAsicRadioOn(pAd, GUI_IDLE_POWER_SAVE);
			}

            pAd->Mlme.bPsPollTimerRunning = FALSE;
            RTMPCancelTimer(&pAd->Mlme.PsPollTimer,	&Cancelled);
	        RTMPCancelTimer(&pAd->Mlme.RadioOnOffTimer,	&Cancelled);
        }

        // Link down first if any association exists
        if (INFRA_ON(pAd) || ADHOC_ON(pAd))
            LinkDown(pAd, FALSE);
        RTMPusecDelay(10000);
        //==========================================
        // Clean up old bss table
        BssTableInit(&pAd->ScanTab);

		RTMPRingCleanUp(pAd, QID_AC_BK);
    	RTMPRingCleanUp(pAd, QID_AC_BE);
    	RTMPRingCleanUp(pAd, QID_AC_VI);
    	RTMPRingCleanUp(pAd, QID_AC_VO);
    	RTMPRingCleanUp(pAd, QID_HCCA);
    	RTMPRingCleanUp(pAd, QID_MGMT);
    	RTMPRingCleanUp(pAd, QID_RX);

		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE))
		{
			RTMPSetTimer(&pAd->Mlme.RadioOnOffTimer, 500);
			return;
		}
    }

	// Set Radio off flag
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

	// Disable Tx/Rx DMA
	RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);	   // disable DMA
	GloCfg.field.EnableTxDMA = 0;
	GloCfg.field.EnableRxDMA = 0;
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);	   // abort all TX rings


	// MAC_SYS_CTRL => value = 0x0 => 40mA
	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0);

	// PWR_PIN_CFG => value = 0x0 => 40mA
	RTMP_IO_WRITE32(pAd, PWR_PIN_CFG, 0);

	// TX_PIN_CFG => value = 0x0 => 20mA
	RTMP_IO_WRITE32(pAd, TX_PIN_CFG, 0);

	if (pAd->CommonCfg.BBPCurrentBW == BW_40)
	{
		// Must using 40MHz.
		AsicTurnOffRFClk(pAd, pAd->CommonCfg.CentralChannel);
	}
	else
	{
		// Must using 20MHz.
		AsicTurnOffRFClk(pAd, pAd->CommonCfg.Channel);
	}

	// Waiting for DMA idle
	i = 0;
	do
	{
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);
		if ((GloCfg.field.TxDMABusy == 0) && (GloCfg.field.RxDMABusy == 0))
			break;

		RTMPusecDelay(1000);
	}while (i++ < 100);
}
