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
		//hwHeaderLen = ROUND_UP(pTxBlk->MpduHeaderLen-LENGTH_AMSDU_SUBFRAMEHEAD, 4)+LENGTH_AMSDU_SUBFRAMEHEAD;
		hwHeaderLen = pTxBlk->MpduHeaderLen - LENGTH_AMSDU_SUBFRAMEHEAD + pTxBlk->HdrPadLen + LENGTH_AMSDU_SUBFRAMEHEAD;
	}
	else
	{
		//hwHeaderLen = ROUND_UP(pTxBlk->MpduHeaderLen, 4);
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
	pTxD->SDPtr1 = PCI_MAP_SINGLE(pAd, pTxBlk, 0, 1, PCI_DMA_TODEVICE);
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
#ifdef RT_BIG_ENDIAN
    PTXD_STRUC      pDestTxD;
    TXD_STRUC       TxD;
#endif
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
	//hwHeaderLen = ROUND_UP(pTxBlk->MpduHeaderLen, 4);
	hwHeaderLen = pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;

	NdisMoveMemory(pDMAHeaderBufVA, pTxBlk->HeaderBuf, TXINFO_SIZE + TXWI_SIZE + hwHeaderLen);

	pTxRing->Cell[TxIdx].pNdisPacket = pTxBlk->pPacket;
	pTxRing->Cell[TxIdx].pNextNdisPacket = NULL;

	//
	// build Tx Descriptor
	//
#ifndef RT_BIG_ENDIAN
	pTxD = (PTXD_STRUC) pTxRing->Cell[TxIdx].AllocVa;
#else
	pDestTxD = (PTXD_STRUC) pTxRing->Cell[TxIdx].AllocVa;
	TxD = *pDestTxD;
	pTxD = &TxD;
#endif
	NdisZeroMemory(pTxD, TXD_SIZE);

	pTxD->SDPtr0 = BufBasePaLow;
	pTxD->SDLen0 = TXINFO_SIZE + TXWI_SIZE + hwHeaderLen; // include padding
	pTxD->SDPtr1 = PCI_MAP_SINGLE(pAd, pTxBlk, 0, 1, PCI_DMA_TODEVICE);;
	pTxD->SDLen1 = pTxBlk->SrcBufLen;
	pTxD->LastSec0 = 0;
	pTxD->LastSec1 = (bIsLast) ? 1 : 0;

	RTMPWriteTxDescriptor(pAd, pTxD, FALSE, FIFO_EDCA);
#ifdef RT_BIG_ENDIAN
	RTMPWIEndianChange((PUCHAR)(pDMAHeaderBufVA + TXINFO_SIZE), TYPE_TXWI);
	RTMPFrameEndianChange(pAd, (PUCHAR)(pDMAHeaderBufVA + TXINFO_SIZE + TXWI_SIZE), DIR_WRITE, FALSE);
	RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
	WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, FALSE, TYPE_TXD);
#endif // RT_BIG_ENDIAN //

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
#ifdef RT_BIG_ENDIAN
    PTXD_STRUC      pDestTxD;
    TXD_STRUC       TxD;
#endif
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
#ifndef RT_BIG_ENDIAN
	pTxD = (PTXD_STRUC) pTxRing->Cell[TxIdx].AllocVa;
#else
	pDestTxD = (PTXD_STRUC) pTxRing->Cell[TxIdx].AllocVa;
	TxD = *pDestTxD;
	pTxD = &TxD;
#endif
	NdisZeroMemory(pTxD, TXD_SIZE);

	pTxD->SDPtr0 = BufBasePaLow;
	pTxD->SDLen0 = firstDMALen; // include padding
	pTxD->SDPtr1 = PCI_MAP_SINGLE(pAd, pTxBlk, 0, 1, PCI_DMA_TODEVICE);;
	pTxD->SDLen1 = pTxBlk->SrcBufLen;
	pTxD->LastSec0 = 0;
	pTxD->LastSec1 = (bIsLast) ? 1 : 0;

	RTMPWriteTxDescriptor(pAd, pTxD, FALSE, FIFO_EDCA);

#ifdef RT_BIG_ENDIAN
	if (frameNum == 0)
		RTMPFrameEndianChange(pAd, (PUCHAR)(pDMAHeaderBufVA+ TXINFO_SIZE + TXWI_SIZE), DIR_WRITE, FALSE);

	if (frameNum != 0)
		RTMPWIEndianChange((PUCHAR)(pDMAHeaderBufVA + TXINFO_SIZE), TYPE_TXWI);

	RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
	WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, FALSE, TYPE_TXD);
#endif // RT_BIG_ENDIAN //

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
#ifdef RT_BIG_ENDIAN
	RTMPWIEndianChange((PUCHAR)pTxWI, TYPE_TXWI);
#endif // RT_BIG_ENDIAN //

}


VOID RtmpPCIDataLastTxIdx(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			QueIdx,
	IN	USHORT			LastTxIdx)
{
	PTXD_STRUC		pTxD;
#ifdef RT_BIG_ENDIAN
    PTXD_STRUC      pDestTxD;
    TXD_STRUC       TxD;
#endif
	PRTMP_TX_RING	pTxRing;

	//
	// get Tx Ring Resource
	//
	pTxRing = &pAd->TxRing[QueIdx];

	//
	// build Tx Descriptor
	//
#ifndef RT_BIG_ENDIAN
	pTxD = (PTXD_STRUC) pTxRing->Cell[LastTxIdx].AllocVa;
#else
	pDestTxD = (PTXD_STRUC) pTxRing->Cell[LastTxIdx].AllocVa;
	TxD = *pDestTxD;
	pTxD = &TxD;
#endif

	pTxD->LastSec1 = 1;

#ifdef RT_BIG_ENDIAN
	RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
	WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, FALSE, TYPE_TXD);
#endif // RT_BIG_ENDIAN //

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
#ifdef RT_BIG_ENDIAN
    PTXD_STRUC      pDestTxD;
    TXD_STRUC       TxD;
#endif
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
	//hwHeaderLen = ROUND_UP(pTxBlk->MpduHeaderLen, 4);
	hwHeaderLen = pTxBlk->MpduHeaderLen + pTxBlk->HdrPadLen;

	firstDMALen = TXINFO_SIZE + TXWI_SIZE + hwHeaderLen;
	NdisMoveMemory(pDMAHeaderBufVA, pTxBlk->HeaderBuf, firstDMALen);


	//
	// Build Tx Descriptor
	//
#ifndef RT_BIG_ENDIAN
	pTxD = (PTXD_STRUC) pTxRing->Cell[TxIdx].AllocVa;
#else
	pDestTxD = (PTXD_STRUC) pTxRing->Cell[TxIdx].AllocVa;
	TxD = *pDestTxD;
	pTxD = &TxD;
#endif
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

#ifdef RT_BIG_ENDIAN
	RTMPWIEndianChange((PUCHAR)(pDMAHeaderBufVA + TXINFO_SIZE), TYPE_TXWI);
	RTMPFrameEndianChange(pAd, (PUCHAR)(pDMAHeaderBufVA + TXINFO_SIZE + TXWI_SIZE), DIR_WRITE, FALSE);
	RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
    WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, FALSE, TYPE_TXD);
#endif // RT_BIG_ENDIAN //

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
	IN RTMP_ADAPTER		*pAd,
	IN UCHAR			QueIdx,
	IN PNDIS_PACKET		pPacket,
	IN PUCHAR			pSrcBufVA,
	IN UINT				SrcBufLen)
{
	PTXD_STRUC		pTxD;
#ifdef RT_BIG_ENDIAN
    PTXD_STRUC      pDestTxD;
    TXD_STRUC       TxD;
#endif
	ULONG			SwIdx = pAd->MgmtRing.TxCpuIdx;

#ifdef RT_BIG_ENDIAN
    pDestTxD  = (PTXD_STRUC)pAd->MgmtRing.Cell[SwIdx].AllocVa;
    TxD = *pDestTxD;
    pTxD = &TxD;
    RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
#else
	pTxD  = (PTXD_STRUC) pAd->MgmtRing.Cell[SwIdx].AllocVa;
#endif

	pAd->MgmtRing.Cell[SwIdx].pNdisPacket = pPacket;
	pAd->MgmtRing.Cell[SwIdx].pNextNdisPacket = NULL;

	RTMPWriteTxDescriptor(pAd, pTxD, TRUE, FIFO_MGMT);
	pTxD->LastSec0 = 1;
	pTxD->LastSec1 = 1;
	pTxD->DMADONE = 0;
	pTxD->SDLen1 = 0;
	pTxD->SDPtr0 = PCI_MAP_SINGLE(pAd, pSrcBufVA, SrcBufLen, 0, PCI_DMA_TODEVICE);
	pTxD->SDLen0 = SrcBufLen;

#ifdef RT_BIG_ENDIAN
	RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
    WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, FALSE, TYPE_TXD);
#endif

//==================================================================
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
//=======================================================================

	pAd->RalinkCounters.KickTxCount++;
	pAd->RalinkCounters.OneSecTxDoneCount++;

	// Increase TX_CTX_IDX, but write to register later.
	INC_RING_INDEX(pAd->MgmtRing.TxCpuIdx, MGMT_RING_SIZE);

	RTMP_IO_WRITE32(pAd, TX_MGMTCTX_IDX,  pAd->MgmtRing.TxCpuIdx);

	return 0;
}


#ifdef CONFIG_STA_SUPPORT
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
NDIS_STATUS RTMPCheckRxError(
	IN	PRTMP_ADAPTER		pAd,
	IN	PHEADER_802_11		pHeader,
	IN	PRXWI_STRUC		pRxWI,
	IN  PRT28XX_RXD_STRUC	pRxD)
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
//			CipherName[pRxD->CipherAlg],
			pRxWI->KeyIndex));

		//
		// MIC Error
		//
		if (pRxD->CipherErr == 2)
		{
			pWpaKey = &pAd->SharedKey[BSS0][pRxWI->KeyIndex];
#ifdef WPA_SUPPLICANT_SUPPORT
            if (pAd->StaCfg.WpaSupplicantUP)
                WpaSendMicFailureToWpaSupplicant(pAd,
                                   (pWpaKey->Type == PAIRWISEKEY) ? TRUE:FALSE);
            else
#endif // WPA_SUPPLICANT_SUPPORT //
			    RTMPReportMicError(pAd, pWpaKey);

            if (((pRxD->CipherErr & 2) == 2) && pAd->CommonCfg.bWirelessEvent && INFRA_ON(pAd))
                RTMPSendWirelessEvent(pAd, IW_MIC_ERROR_EVENT_FLAG, pAd->MacTab.Content[BSSID_WCID].Addr, BSS0, 0);

			DBGPRINT_RAW(RT_DEBUG_ERROR,("Rx MIC Value error\n"));
		}

		if (pHeader == NULL)
			return(NDIS_STATUS_SUCCESS);
		/*if ((pRxD->CipherAlg == CIPHER_AES) &&
			(pHeader->Sequence == pAd->FragFrame.Sequence))
		{
			//
			// Acceptable since the First FragFrame no CipherErr problem.
			//
			return(NDIS_STATUS_SUCCESS);
		}*/

		return(NDIS_STATUS_FAILURE);
	}

	return(NDIS_STATUS_SUCCESS);
}
#endif // CONFIG_STA_SUPPORT //


BOOLEAN  RTMPFreeTXDUponTxDmaDone(
	IN PRTMP_ADAPTER	pAd,
	IN UCHAR			QueIdx)
{
	PRTMP_TX_RING pTxRing;
	PTXD_STRUC	  pTxD;
#ifdef	RT_BIG_ENDIAN
    PTXD_STRUC      pDestTxD;
#endif
	PNDIS_PACKET  pPacket;
	UCHAR	FREE = 0;
	TXD_STRUC	TxD, *pOriTxD;
	//ULONG		IrqFlags;
	BOOLEAN			bReschedule = FALSE;


	ASSERT(QueIdx < NUM_OF_TX_RING);
	pTxRing = &pAd->TxRing[QueIdx];

	RTMP_IO_READ32(pAd, TX_DTX_IDX0 + QueIdx * RINGREG_DIFF, &pTxRing->TxDmaIdx);
	while (pTxRing->TxSwFreeIdx != pTxRing->TxDmaIdx)
	{
//		RTMP_IRQ_LOCK(&pAd->irq_lock, IrqFlags);
#ifdef RALINK_ATE
#ifdef RALINK_28xx_QA
		PHEADER_802_11	pHeader80211;

		if ((ATE_ON(pAd)) && (pAd->ate.bQATxStart == TRUE))
		{
			if (pAd->ate.QID == QueIdx)
			{
				pAd->ate.TxDoneCount++;
				pAd->RalinkCounters.KickTxCount++;

				/* always use QID_AC_BE and FIFO_EDCA */
				ASSERT(pAd->ate.QID == 0);
				pAd->ate.TxAc0++;

				FREE++;
#ifndef RT_BIG_ENDIAN
				pTxD = (PTXD_STRUC) (pTxRing->Cell[pTxRing->TxSwFreeIdx].AllocVa);
				pOriTxD = pTxD;
		        NdisMoveMemory(&TxD, pTxD, sizeof(TXD_STRUC));
				pTxD = &TxD;
#else
		        pDestTxD = (PTXD_STRUC) (pTxRing->Cell[pTxRing->TxSwFreeIdx].AllocVa);
		        pOriTxD = pDestTxD ;
		        TxD = *pDestTxD;
		        pTxD = &TxD;
		        RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
#endif
				pTxD->DMADONE = 0;

				pHeader80211 = pTxRing->Cell[pTxRing->TxSwFreeIdx].DmaBuf.AllocVa + sizeof(TXWI_STRUC);
#ifdef RT_BIG_ENDIAN
				RTMPFrameEndianChange(pAd, (PUCHAR)pHeader80211, DIR_READ, FALSE);
#endif
				pHeader80211->Sequence = ++pAd->ate.seq;
#ifdef RT_BIG_ENDIAN
				RTMPFrameEndianChange(pAd, (PUCHAR)pHeader80211, DIR_WRITE, FALSE);
#endif

				if  ((pAd->ate.bQATxStart == TRUE) && (pAd->ate.Mode & ATE_TXFRAME) && (pAd->ate.TxDoneCount < pAd->ate.TxCount))
				{
					pAd->RalinkCounters.TransmittedByteCount +=  (pTxD->SDLen1 + pTxD->SDLen0);
					pAd->RalinkCounters.OneSecTransmittedByteCount += (pTxD->SDLen1 + pTxD->SDLen0);
					pAd->RalinkCounters.OneSecDmaDoneCount[QueIdx] ++;
					INC_RING_INDEX(pTxRing->TxSwFreeIdx, TX_RING_SIZE);

					/* get TX_DTX_IDX again */
					RTMP_IO_READ32(pAd, TX_DTX_IDX0 + QueIdx * RINGREG_DIFF ,  &pTxRing->TxDmaIdx);
					goto kick_out;
				}
				else if ((pAd->ate.TxStatus == 1)/* or (pAd->ate.bQATxStart == TRUE) ??? */ && (pAd->ate.TxDoneCount == pAd->ate.TxCount))
				{
					DBGPRINT(RT_DEBUG_TRACE,("all Tx is done\n"));

					// Tx status enters idle mode.
					pAd->ate.TxStatus = 0;
				}
				else if (!(pAd->ate.Mode & ATE_TXFRAME))
				{
					/* not complete sending yet, but someone press the Stop TX botton */
					DBGPRINT(RT_DEBUG_ERROR,("not complete sending yet, but someone pressed the Stop TX bottom\n"));
					DBGPRINT(RT_DEBUG_ERROR,("pAd->ate.Mode = 0x%02x\n", pAd->ate.Mode));
				}
				else
				{
					DBGPRINT(RT_DEBUG_OFF,("pTxRing->TxSwFreeIdx = %d\n", pTxRing->TxSwFreeIdx));
				}

#ifndef RT_BIG_ENDIAN
			NdisMoveMemory(pOriTxD, pTxD, sizeof(TXD_STRUC));
#else
			RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
			*pDestTxD = TxD;
#endif // RT_BIG_ENDIAN //

				INC_RING_INDEX(pTxRing->TxSwFreeIdx, TX_RING_SIZE);
				continue;
			}
		}
#endif // RALINK_28xx_QA //
#endif // RALINK_ATE //

		// static rate also need NICUpdateFifoStaCounters() function.
		//if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED))
			NICUpdateFifoStaCounters(pAd);

		/* Note : If (pAd->ate.bQATxStart == TRUE), we will never reach here. */
		FREE++;
#ifndef RT_BIG_ENDIAN
                pTxD = (PTXD_STRUC) (pTxRing->Cell[pTxRing->TxSwFreeIdx].AllocVa);
		pOriTxD = pTxD;
                NdisMoveMemory(&TxD, pTxD, sizeof(TXD_STRUC));
		pTxD = &TxD;
#else
        pDestTxD = (PTXD_STRUC) (pTxRing->Cell[pTxRing->TxSwFreeIdx].AllocVa);
        pOriTxD = pDestTxD ;
        TxD = *pDestTxD;
        pTxD = &TxD;
        RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
#endif

		pTxD->DMADONE = 0;


#ifdef RALINK_ATE
		/* Execution of this block is not allowed when ATE is running. */
		if (!(ATE_ON(pAd)))
#endif // RALINK_ATE //
		{
			pPacket = pTxRing->Cell[pTxRing->TxSwFreeIdx].pNdisPacket;
			if (pPacket)
			{
#ifdef CONFIG_5VT_ENHANCE
				if (RTMP_GET_PACKET_5VT(pPacket))
					PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1, 16, PCI_DMA_TODEVICE);
				else
#endif // CONFIG_5VT_ENHANCE //
					PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1, pTxD->SDLen1, PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
			}
			//Always assign pNdisPacket as NULL after clear
			pTxRing->Cell[pTxRing->TxSwFreeIdx].pNdisPacket = NULL;

			pPacket = pTxRing->Cell[pTxRing->TxSwFreeIdx].pNextNdisPacket;

			ASSERT(pPacket == NULL);
			if (pPacket)
			{
#ifdef CONFIG_5VT_ENHANCE
				if (RTMP_GET_PACKET_5VT(pPacket))
					PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1, 16, PCI_DMA_TODEVICE);
				else
#endif // CONFIG_5VT_ENHANCE //
					PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1, pTxD->SDLen1, PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
			}
			//Always assign pNextNdisPacket as NULL after clear
			pTxRing->Cell[pTxRing->TxSwFreeIdx].pNextNdisPacket = NULL;
		}

		pAd->RalinkCounters.TransmittedByteCount +=  (pTxD->SDLen1 + pTxD->SDLen0);
		pAd->RalinkCounters.OneSecDmaDoneCount[QueIdx] ++;
		INC_RING_INDEX(pTxRing->TxSwFreeIdx, TX_RING_SIZE);
		/* get tx_tdx_idx again */
		RTMP_IO_READ32(pAd, TX_DTX_IDX0 + QueIdx * RINGREG_DIFF ,  &pTxRing->TxDmaIdx);
#ifdef RT_BIG_ENDIAN
        RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
        *pDestTxD = TxD;
#else
        NdisMoveMemory(pOriTxD, pTxD, sizeof(TXD_STRUC));
#endif

#ifdef RALINK_ATE
#ifdef RALINK_28xx_QA
kick_out:
#endif // RALINK_28xx_QA //

		/*
			ATE_TXCONT mode also need to send some normal frames, so let it in.
			ATE_STOP must be changed not to be 0xff
			to prevent it from running into this block.
		*/
		if ((pAd->ate.Mode & ATE_TXFRAME) && (pAd->ate.QID == QueIdx))
		{
			// TxDoneCount++ has been done if QA is used.
			if (pAd->ate.bQATxStart == FALSE)
			{
				pAd->ate.TxDoneCount++;
			}
			if (((pAd->ate.TxCount - pAd->ate.TxDoneCount + 1) >= TX_RING_SIZE))
			{
				/* Note : We increase TxCpuIdx here, not TxSwFreeIdx ! */
				INC_RING_INDEX(pAd->TxRing[QueIdx].TxCpuIdx, TX_RING_SIZE);
#ifndef RT_BIG_ENDIAN
				pTxD = (PTXD_STRUC) (pTxRing->Cell[pAd->TxRing[QueIdx].TxCpuIdx].AllocVa);
				pOriTxD = pTxD;
		        NdisMoveMemory(&TxD, pTxD, sizeof(TXD_STRUC));
				pTxD = &TxD;
#else
		        pDestTxD = (PTXD_STRUC) (pTxRing->Cell[pAd->TxRing[QueIdx].TxCpuIdx].AllocVa);
		        pOriTxD = pDestTxD ;
		        TxD = *pDestTxD;
		        pTxD = &TxD;
		        RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
#endif
				pTxD->DMADONE = 0;
#ifndef RT_BIG_ENDIAN
			NdisMoveMemory(pOriTxD, pTxD, sizeof(TXD_STRUC));
#else
			RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
			*pDestTxD = TxD;
#endif
				// kick Tx-Ring
				RTMP_IO_WRITE32(pAd, TX_CTX_IDX0 + QueIdx * RINGREG_DIFF, pAd->TxRing[QueIdx].TxCpuIdx);
				pAd->RalinkCounters.KickTxCount++;
			}
		}
#endif // RALINK_ATE //
//         RTMP_IRQ_UNLOCK(&pAd->irq_lock, IrqFlags);
	}


	return  bReschedule;

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
BOOLEAN	RTMPHandleTxRingDmaDoneInterrupt(
	IN	PRTMP_ADAPTER	pAd,
	IN	INT_SOURCE_CSR_STRUC TxRingBitmap)
{
//	UCHAR			Count = 0;
    unsigned long	IrqFlags;
	BOOLEAN			bReschedule = FALSE;

	// Make sure Tx ring resource won't be used by other threads
	//NdisAcquireSpinLock(&pAd->TxRingLock);

	RTMP_IRQ_LOCK(&pAd->irq_lock, IrqFlags);

	if (TxRingBitmap.field.Ac0DmaDone)
		bReschedule = RTMPFreeTXDUponTxDmaDone(pAd, QID_AC_BE);
/*
	if (TxRingBitmap.field.HccaDmaDone)
		bReschedule |= RTMPFreeTXDUponTxDmaDone(pAd, QID_HCCA);
*/

	if (TxRingBitmap.field.Ac3DmaDone)
		bReschedule |= RTMPFreeTXDUponTxDmaDone(pAd, QID_AC_VO);

	if (TxRingBitmap.field.Ac2DmaDone)
		bReschedule |= RTMPFreeTXDUponTxDmaDone(pAd, QID_AC_VI);

	if (TxRingBitmap.field.Ac1DmaDone)
		bReschedule |= RTMPFreeTXDUponTxDmaDone(pAd, QID_AC_BK);

	// Make sure to release Tx ring resource
	//NdisReleaseSpinLock(&pAd->TxRingLock);
	RTMP_IRQ_UNLOCK(&pAd->irq_lock, IrqFlags);

	// Dequeue outgoing frames from TxSwQueue[] and process it
	RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);

	return  bReschedule;
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
VOID	RTMPHandleMgmtRingDmaDoneInterrupt(
	IN	PRTMP_ADAPTER	pAd)
{
	PTXD_STRUC	 pTxD;
#ifdef RT_BIG_ENDIAN
    PTXD_STRUC      pDestTxD;
    TXD_STRUC       TxD;
#endif
	PNDIS_PACKET pPacket;
//	int		 i;
	UCHAR	FREE = 0;
	PRTMP_MGMT_RING pMgmtRing = &pAd->MgmtRing;

	NdisAcquireSpinLock(&pAd->MgmtRingLock);

	RTMP_IO_READ32(pAd, TX_MGMTDTX_IDX, &pMgmtRing->TxDmaIdx);
	while (pMgmtRing->TxSwFreeIdx!= pMgmtRing->TxDmaIdx)
	{
		FREE++;
#ifdef RT_BIG_ENDIAN
        pDestTxD = (PTXD_STRUC) (pMgmtRing->Cell[pAd->MgmtRing.TxSwFreeIdx].AllocVa);
        TxD = *pDestTxD;
        pTxD = &TxD;
		RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
#else
		pTxD = (PTXD_STRUC) (pMgmtRing->Cell[pAd->MgmtRing.TxSwFreeIdx].AllocVa);
#endif
		pTxD->DMADONE = 0;
		pPacket = pMgmtRing->Cell[pMgmtRing->TxSwFreeIdx].pNdisPacket;


		if (pPacket)
		{
			PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr0, pTxD->SDLen0, PCI_DMA_TODEVICE);
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
		}
		pMgmtRing->Cell[pMgmtRing->TxSwFreeIdx].pNdisPacket = NULL;

		pPacket = pMgmtRing->Cell[pMgmtRing->TxSwFreeIdx].pNextNdisPacket;
		if (pPacket)
		{
			PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1, pTxD->SDLen1, PCI_DMA_TODEVICE);
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
		}
		pMgmtRing->Cell[pMgmtRing->TxSwFreeIdx].pNextNdisPacket = NULL;
		INC_RING_INDEX(pMgmtRing->TxSwFreeIdx, MGMT_RING_SIZE);

#ifdef RT_BIG_ENDIAN
        RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
        WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, TRUE, TYPE_TXD);
#endif
	}
	NdisReleaseSpinLock(&pAd->MgmtRingLock);

#ifdef CONFIG_STA_SUPPORT
#endif // CONFIG_STA_SUPPORT //
}


/*
	========================================================================

	Routine Description:
	Arguments:
		Adapter		Pointer to our adapter. Dequeue all power safe delayed braodcast frames after beacon.

	IRQL = DISPATCH_LEVEL

	========================================================================
*/
VOID	RTMPHandleTBTTInterrupt(
	IN PRTMP_ADAPTER pAd)
{
	{
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
		{
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
VOID	RTMPHandlePreTBTTInterrupt(
	IN PRTMP_ADAPTER pAd)
{
	{
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
		{
			DBGPRINT(RT_DEBUG_TRACE, ("RTMPHandlePreTBTTInterrupt...\n"));
		}
	}


}

VOID	RTMPHandleRxCoherentInterrupt(
	IN	PRTMP_ADAPTER	pAd)
{
	WPDMA_GLO_CFG_STRUC	GloCfg;

	if (pAd == NULL)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("====> pAd is NULL, return.\n"));
		return;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("==> RTMPHandleRxCoherentInterrupt \n"));

	RTMP_IO_READ32(pAd, WPDMA_GLO_CFG , &GloCfg.word);

	GloCfg.field.EnTXWriteBackDDONE = 0;
	GloCfg.field.EnableRxDMA = 0;
	GloCfg.field.EnableTxDMA = 0;
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);

	RTMPRingCleanUp(pAd, QID_AC_BE);
	RTMPRingCleanUp(pAd, QID_AC_BK);
	RTMPRingCleanUp(pAd, QID_AC_VI);
	RTMPRingCleanUp(pAd, QID_AC_VO);
	/*RTMPRingCleanUp(pAd, QID_HCCA);*/
	RTMPRingCleanUp(pAd, QID_MGMT);
	RTMPRingCleanUp(pAd, QID_RX);

	RTMPEnableRxTx(pAd);

	DBGPRINT(RT_DEBUG_TRACE, ("<== RTMPHandleRxCoherentInterrupt \n"));
}




VOID DBGPRINT_TX_RING(
	IN PRTMP_ADAPTER  pAd,
	IN UCHAR          QueIdx)
{
	UINT32		Ac0Base;
	UINT32		Ac0HwIdx = 0, Ac0SwIdx = 0, AC0freeIdx;
	int			i;
//	PULONG		pTxD;
	PULONG	ptemp;

	DBGPRINT_RAW(RT_DEBUG_TRACE, ("=====================================================\n "  ));
	switch (QueIdx)
	{
		case QID_AC_BE:
			RTMP_IO_READ32(pAd, TX_BASE_PTR0, &Ac0Base);
			RTMP_IO_READ32(pAd, TX_CTX_IDX0, &Ac0SwIdx);
			RTMP_IO_READ32(pAd, TX_DTX_IDX0, &Ac0HwIdx);
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("All QID_AC_BE DESCRIPTOR  \n "  ));
			for (i=0;i<TX_RING_SIZE;i++)
			{
				ptemp= (PULONG)pAd->TxRing[QID_AC_BE].Cell[i].AllocVa;
				DBGPRINT_RAW(RT_DEBUG_TRACE, ("[%02d]  %08lx: %08lx: %08lx: %08lx\n " , i, *ptemp,*(ptemp+1),*(ptemp+2),*(ptemp+3)));
			}
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("  \n "  ));
			break;
		case QID_AC_BK:
			RTMP_IO_READ32(pAd, TX_BASE_PTR1, &Ac0Base);
			RTMP_IO_READ32(pAd, TX_CTX_IDX1, &Ac0SwIdx);
			RTMP_IO_READ32(pAd, TX_DTX_IDX1, &Ac0HwIdx);
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("All QID_AC_BK DESCRIPTOR  \n "  ));
			for (i=0;i<TX_RING_SIZE;i++)
			{
				ptemp= (PULONG)pAd->TxRing[QID_AC_BK].Cell[i].AllocVa;
				DBGPRINT_RAW(RT_DEBUG_TRACE, ("[%02d]  %08lx: %08lx: %08lx: %08lx\n " , i, *ptemp,*(ptemp+1),*(ptemp+2),*(ptemp+3)));
			}
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("  \n "  ));
			break;
		case QID_AC_VI:
			RTMP_IO_READ32(pAd, TX_BASE_PTR2, &Ac0Base);
			RTMP_IO_READ32(pAd, TX_CTX_IDX2, &Ac0SwIdx);
			RTMP_IO_READ32(pAd, TX_DTX_IDX2, &Ac0HwIdx);
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("All QID_AC_VI DESCRIPTOR \n "  ));
			for (i=0;i<TX_RING_SIZE;i++)
			{
				ptemp= (PULONG)pAd->TxRing[QID_AC_VI].Cell[i].AllocVa;
				DBGPRINT_RAW(RT_DEBUG_TRACE, ("[%02d]  %08lx: %08lx: %08lx: %08lx\n " , i, *ptemp,*(ptemp+1),*(ptemp+2),*(ptemp+3)));
			}
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("  \n "  ));
			break;
		case QID_AC_VO:
			RTMP_IO_READ32(pAd, TX_BASE_PTR3, &Ac0Base);
			RTMP_IO_READ32(pAd, TX_CTX_IDX3, &Ac0SwIdx);
			RTMP_IO_READ32(pAd, TX_DTX_IDX3, &Ac0HwIdx);
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("All QID_AC_VO DESCRIPTOR \n "  ));
			for (i=0;i<TX_RING_SIZE;i++)
			{
				ptemp= (PULONG)pAd->TxRing[QID_AC_VO].Cell[i].AllocVa;
				DBGPRINT_RAW(RT_DEBUG_TRACE, ("[%02d]  %08lx: %08lx: %08lx: %08lx\n " , i, *ptemp,*(ptemp+1),*(ptemp+2),*(ptemp+3)));
			}
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("  \n "  ));
			break;
		case QID_MGMT:
			RTMP_IO_READ32(pAd, TX_BASE_PTR5, &Ac0Base);
			RTMP_IO_READ32(pAd, TX_CTX_IDX5, &Ac0SwIdx);
			RTMP_IO_READ32(pAd, TX_DTX_IDX5, &Ac0HwIdx);
			DBGPRINT_RAW(RT_DEBUG_TRACE, (" All QID_MGMT  DESCRIPTOR \n "  ));
			for (i=0;i<MGMT_RING_SIZE;i++)
			{
				ptemp= (PULONG)pAd->MgmtRing.Cell[i].AllocVa;
				DBGPRINT_RAW(RT_DEBUG_TRACE, ("[%02d]  %08lx: %08lx: %08lx: %08lx\n " , i, *ptemp,*(ptemp+1),*(ptemp+2),*(ptemp+3)));
			}
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("  \n "  ));
			break;

		default:
			DBGPRINT_ERR(("DBGPRINT_TX_RING(Ring %d) not supported\n", QueIdx));
			break;
	}
	AC0freeIdx = pAd->TxRing[QueIdx].TxSwFreeIdx;

	DBGPRINT(RT_DEBUG_TRACE,("TxRing%d, TX_DTX_IDX=%d, TX_CTX_IDX=%d\n", QueIdx, Ac0HwIdx, Ac0SwIdx));
	DBGPRINT_RAW(RT_DEBUG_TRACE,("	TxSwFreeIdx[%d]", AC0freeIdx));
	DBGPRINT_RAW(RT_DEBUG_TRACE,("	pending-NDIS=%ld\n", pAd->RalinkCounters.PendingNdisPacketCount));


}


VOID DBGPRINT_RX_RING(
	IN PRTMP_ADAPTER  pAd)
{
	UINT32		Ac0Base;
	UINT32		Ac0HwIdx = 0, Ac0SwIdx = 0, AC0freeIdx;
//	PULONG	 pTxD;
	int			i;
	UINT32	*ptemp;
//	PRXD_STRUC		pRxD;

	DBGPRINT_RAW(RT_DEBUG_TRACE, ("=====================================================\n "  ));
	RTMP_IO_READ32(pAd, RX_BASE_PTR, &Ac0Base);
	RTMP_IO_READ32(pAd, RX_CRX_IDX, &Ac0SwIdx);
	RTMP_IO_READ32(pAd, RX_DRX_IDX, &Ac0HwIdx);
	AC0freeIdx = pAd->RxRing.RxSwReadIdx;

	DBGPRINT_RAW(RT_DEBUG_TRACE, ("All RX DSP  \n "  ));
	for (i=0;i<RX_RING_SIZE;i++)
	{
		ptemp = (UINT32 *)pAd->RxRing.Cell[i].AllocVa;
		DBGPRINT_RAW(RT_DEBUG_TRACE, ("[%02d]  %08x: %08x: %08x: %08x\n " , i, *ptemp,*(ptemp+1),*(ptemp+2),*(ptemp+3)));
	}
	DBGPRINT(RT_DEBUG_TRACE,("RxRing, RX_DRX_IDX=%d, RX_CRX_IDX=%d \n", Ac0HwIdx, Ac0SwIdx));
	DBGPRINT_RAW(RT_DEBUG_TRACE,("	RxSwReadIdx [%d]=", AC0freeIdx));
	DBGPRINT_RAW(RT_DEBUG_TRACE,("	pending-NDIS=%ld\n", pAd->RalinkCounters.PendingNdisPacketCount));
}


PNDIS_PACKET GetPacketFromRxRing(
	IN		PRTMP_ADAPTER	pAd,
	OUT		PRT28XX_RXD_STRUC	pSaveRxD,
	OUT		BOOLEAN			*pbReschedule,
	IN OUT	UINT32			*pRxPending)
{
	PRXD_STRUC				pRxD;
#ifdef RT_BIG_ENDIAN
	PRXD_STRUC				pDestRxD;
	RXD_STRUC				RxD;
#endif
	PNDIS_PACKET			pRxPacket = NULL;
	PNDIS_PACKET			pNewPacket;
	PVOID					AllocVa;
	NDIS_PHYSICAL_ADDRESS	AllocPa;
	BOOLEAN					bReschedule = FALSE;

	RTMP_SEM_LOCK(&pAd->RxRingLock);

	if (*pRxPending == 0)
	{
		// Get how may packets had been received
		RTMP_IO_READ32(pAd, RX_DRX_IDX , &pAd->RxRing.RxDmaIdx);

		if (pAd->RxRing.RxSwReadIdx == pAd->RxRing.RxDmaIdx)
		{
			// no more rx packets
			bReschedule = FALSE;
			goto done;
		}

		// get rx pending count
		if (pAd->RxRing.RxDmaIdx > pAd->RxRing.RxSwReadIdx)
			*pRxPending = pAd->RxRing.RxDmaIdx - pAd->RxRing.RxSwReadIdx;
		else
			*pRxPending	= pAd->RxRing.RxDmaIdx + RX_RING_SIZE - pAd->RxRing.RxSwReadIdx;

	}

#ifdef RT_BIG_ENDIAN
	pDestRxD = (PRXD_STRUC) pAd->RxRing.Cell[pAd->RxRing.RxSwReadIdx].AllocVa;
	RxD = *pDestRxD;
	pRxD = &RxD;
	RTMPDescriptorEndianChange((PUCHAR)pRxD, TYPE_RXD);
#else
	// Point to Rx indexed rx ring descriptor
	pRxD = (PRXD_STRUC) pAd->RxRing.Cell[pAd->RxRing.RxSwReadIdx].AllocVa;
#endif

	if (pRxD->DDONE == 0)
	{
		*pRxPending = 0;
		// DMAIndx had done but DDONE bit not ready
		bReschedule = TRUE;
		goto done;
	}


	// return rx descriptor
	NdisMoveMemory(pSaveRxD, pRxD, RXD_SIZE);

	pNewPacket = RTMP_AllocateRxPacketBuffer(pAd, RX_BUFFER_AGGRESIZE, FALSE, &AllocVa, &AllocPa);

	if (pNewPacket)
	{
		// unmap the rx buffer
		PCI_UNMAP_SINGLE(pAd, pAd->RxRing.Cell[pAd->RxRing.RxSwReadIdx].DmaBuf.AllocPa,
					 pAd->RxRing.Cell[pAd->RxRing.RxSwReadIdx].DmaBuf.AllocSize, PCI_DMA_FROMDEVICE);
		pRxPacket = pAd->RxRing.Cell[pAd->RxRing.RxSwReadIdx].pNdisPacket;

		pAd->RxRing.Cell[pAd->RxRing.RxSwReadIdx].DmaBuf.AllocSize	= RX_BUFFER_AGGRESIZE;
		pAd->RxRing.Cell[pAd->RxRing.RxSwReadIdx].pNdisPacket		= (PNDIS_PACKET) pNewPacket;
		pAd->RxRing.Cell[pAd->RxRing.RxSwReadIdx].DmaBuf.AllocVa	= AllocVa;
		pAd->RxRing.Cell[pAd->RxRing.RxSwReadIdx].DmaBuf.AllocPa	= AllocPa;
		/* update SDP0 to new buffer of rx packet */
		pRxD->SDP0 = AllocPa;
	}
	else
	{
		//DBGPRINT(RT_DEBUG_TRACE,("No Rx Buffer\n"));
		pRxPacket = NULL;
		bReschedule = TRUE;
	}

	pRxD->DDONE = 0;

	// had handled one rx packet
	*pRxPending = *pRxPending - 1;

	// update rx descriptor and kick rx
#ifdef RT_BIG_ENDIAN
	RTMPDescriptorEndianChange((PUCHAR)pRxD, TYPE_RXD);
	WriteBackToDescriptor((PUCHAR)pDestRxD, (PUCHAR)pRxD, FALSE, TYPE_RXD);
#endif
	INC_RING_INDEX(pAd->RxRing.RxSwReadIdx, RX_RING_SIZE);

	pAd->RxRing.RxCpuIdx = (pAd->RxRing.RxSwReadIdx == 0) ? (RX_RING_SIZE-1) : (pAd->RxRing.RxSwReadIdx-1);
	RTMP_IO_WRITE32(pAd, RX_CRX_IDX, pAd->RxRing.RxCpuIdx);

done:
	RTMP_SEM_UNLOCK(&pAd->RxRingLock);
	*pbReschedule = bReschedule;
	return pRxPacket;
}


NDIS_STATUS MlmeHardTransmitTxRing(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR	QueIdx,
	IN	PNDIS_PACKET	pPacket)
{
	PACKET_INFO	PacketInfo;
	PUCHAR			pSrcBufVA;
	UINT			SrcBufLen;
	PTXD_STRUC		pTxD;
#ifdef RT_BIG_ENDIAN
    PTXD_STRUC      pDestTxD;
    TXD_STRUC       TxD;
#endif
	PHEADER_802_11	pHeader_802_11;
	BOOLEAN			bAckRequired, bInsertTimestamp;
	ULONG			SrcBufPA;
	//UCHAR			TxBufIdx;
	UCHAR			MlmeRate;
	ULONG			SwIdx = pAd->TxRing[QueIdx].TxCpuIdx;
	PTXWI_STRUC	pFirstTxWI;
	//ULONG	i;
	//HTTRANSMIT_SETTING	MlmeTransmit;   //Rate for this MGMT frame.
	ULONG	 FreeNum;
	MAC_TABLE_ENTRY	*pMacEntry = NULL;


	RTMP_QueryPacketInfo(pPacket, &PacketInfo, &pSrcBufVA, &SrcBufLen);


	if (pSrcBufVA == NULL)
	{
		// The buffer shouldn't be NULL
		return NDIS_STATUS_FAILURE;
	}

	// Make sure MGMT ring resource won't be used by other threads
	//NdisAcquireSpinLock(&pAd->TxRingLock);

	FreeNum = GET_TXRING_FREENO(pAd, QueIdx);

	if (FreeNum == 0)
	{
		//NdisReleaseSpinLock(&pAd->TxRingLock);
		return NDIS_STATUS_FAILURE;
	}

	SwIdx = pAd->TxRing[QueIdx].TxCpuIdx;

#ifndef RT_BIG_ENDIAN
	pTxD  = (PTXD_STRUC) pAd->TxRing[QueIdx].Cell[SwIdx].AllocVa;
#else
    pDestTxD  = (PTXD_STRUC)pAd->TxRing[QueIdx].Cell[SwIdx].AllocVa;
    TxD = *pDestTxD;
    pTxD = &TxD;
    RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
#endif

	if (pAd->TxRing[QueIdx].Cell[SwIdx].pNdisPacket)
	{
		DBGPRINT(RT_DEBUG_OFF, ("MlmeHardTransmit Error\n"));
		//NdisReleaseSpinLock(&pAd->TxRingLock);
		return NDIS_STATUS_FAILURE;
	}


#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		// outgoing frame always wakeup PHY to prevent frame lost
		// if (pAd->StaCfg.Psm == PWR_SAVE)
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
			AsicForceWakeup(pAd, TRUE);
	}
#endif // CONFIG_STA_SUPPORT //
	pFirstTxWI	=(PTXWI_STRUC)pSrcBufVA;

	pHeader_802_11 = (PHEADER_802_11) (pSrcBufVA + TXWI_SIZE);
	if (pHeader_802_11->Addr1[0] & 0x01)
	{
		MlmeRate = pAd->CommonCfg.BasicMlmeRate;
	}
	else
	{
		MlmeRate = pAd->CommonCfg.MlmeRate;
	}

	if ((pHeader_802_11->FC.Type == BTYPE_DATA) &&
		(pHeader_802_11->FC.SubType == SUBTYPE_QOS_NULL))
	{
		pMacEntry = MacTableLookup(pAd, pHeader_802_11->Addr1);
	}

	// Verify Mlme rate for a / g bands.
	if ((pAd->LatchRfRegs.Channel > 14) && (MlmeRate < RATE_6)) // 11A band
		MlmeRate = RATE_6;

	//
	// Should not be hard code to set PwrMgmt to 0 (PWR_ACTIVE)
	// Snice it's been set to 0 while on MgtMacHeaderInit
	// By the way this will cause frame to be send on PWR_SAVE failed.
	//
	//
	// In WMM-UAPSD, mlme frame should be set psm as power saving but probe request frame
#ifdef CONFIG_STA_SUPPORT
    // Data-Null packets alse pass through MMRequest in RT2860, however, we hope control the psm bit to pass APSD
	if (pHeader_802_11->FC.Type != BTYPE_DATA)
    {
	if ((pHeader_802_11->FC.SubType == SUBTYPE_PROBE_REQ) || !(pAd->CommonCfg.bAPSDCapable && pAd->CommonCfg.APEdcaParm.bAPSDCapable))
	{
		pHeader_802_11->FC.PwrMgmt = PWR_ACTIVE;
	}
	else
	{
		pHeader_802_11->FC.PwrMgmt = pAd->CommonCfg.bAPSDForcePowerSave;
	}
    }
#endif // CONFIG_STA_SUPPORT //

	bInsertTimestamp = FALSE;
	if (pHeader_802_11->FC.Type == BTYPE_CNTL) // must be PS-POLL
	{
		bAckRequired = FALSE;
	}
	else // BTYPE_MGMT or BTYPE_DATA(must be NULL frame)
	{
		if (pHeader_802_11->Addr1[0] & 0x01) // MULTICAST, BROADCAST
		{
			bAckRequired = FALSE;
			pHeader_802_11->Duration = 0;
		}
		else
		{
			bAckRequired = TRUE;
			pHeader_802_11->Duration = RTMPCalcDuration(pAd, MlmeRate, 14);
			if (pHeader_802_11->FC.SubType == SUBTYPE_PROBE_RSP)
			{
				bInsertTimestamp = TRUE;
			}
		}
	}
	pHeader_802_11->Sequence = pAd->Sequence++;
	if (pAd->Sequence > 0xfff)
		pAd->Sequence = 0;
	// Before radar detection done, mgmt frame can not be sent but probe req
	// Because we need to use probe req to trigger driver to send probe req in passive scan
	if ((pHeader_802_11->FC.SubType != SUBTYPE_PROBE_REQ)
		&& (pAd->CommonCfg.bIEEE80211H == 1)
		&& (pAd->CommonCfg.RadarDetect.RDMode != RD_NORMAL_MODE))
	{
		DBGPRINT(RT_DEBUG_ERROR,("MlmeHardTransmit --> radar detect not in normal mode !!!\n"));
		//NdisReleaseSpinLock(&pAd->TxRingLock);
		return (NDIS_STATUS_FAILURE);
	}

#ifdef RT_BIG_ENDIAN
	RTMPFrameEndianChange(pAd, (PUCHAR)pHeader_802_11, DIR_WRITE, FALSE);
#endif
	//
	// fill scatter-and-gather buffer list into TXD. Internally created NDIS PACKET
	// should always has only one ohysical buffer, and the whole frame size equals
	// to the first scatter buffer size
	//

	// Initialize TX Descriptor
	// For inter-frame gap, the number is for this frame and next frame
	// For MLME rate, we will fix as 2Mb to match other vendor's implement
//	pAd->CommonCfg.MlmeTransmit.field.MODE = 1;

// management frame doesn't need encryption. so use RESERVED_WCID no matter u are sending to specific wcid or not.
	// Only beacon use Nseq=TRUE. So here we use Nseq=FALSE.
	if (pMacEntry == NULL)
	{
	RTMPWriteTxWI(pAd, pFirstTxWI, FALSE, FALSE, bInsertTimestamp, FALSE, bAckRequired, FALSE,
		0, RESERVED_WCID, (SrcBufLen - TXWI_SIZE), PID_MGMT, 0,  (UCHAR)pAd->CommonCfg.MlmeTransmit.field.MCS, IFS_BACKOFF, FALSE, &pAd->CommonCfg.MlmeTransmit);
	}
	else
	{
		RTMPWriteTxWI(pAd, pFirstTxWI, FALSE, FALSE,
					bInsertTimestamp, FALSE, bAckRequired, FALSE,
					0, pMacEntry->Aid, (SrcBufLen - TXWI_SIZE),
					pMacEntry->MaxHTPhyMode.field.MCS, 0,
					(UCHAR)pMacEntry->MaxHTPhyMode.field.MCS,
					IFS_BACKOFF, FALSE, &pMacEntry->MaxHTPhyMode);
	}

	pAd->TxRing[QueIdx].Cell[SwIdx].pNdisPacket = pPacket;
	pAd->TxRing[QueIdx].Cell[SwIdx].pNextNdisPacket = NULL;
//	pFirstTxWI->MPDUtotalByteCount = SrcBufLen - TXWI_SIZE;
#ifdef RT_BIG_ENDIAN
	RTMPWIEndianChange((PUCHAR)pFirstTxWI, TYPE_TXWI);
#endif
	SrcBufPA = PCI_MAP_SINGLE(pAd, pSrcBufVA, SrcBufLen, 0, PCI_DMA_TODEVICE);


	RTMPWriteTxDescriptor(pAd, pTxD, TRUE, FIFO_EDCA);
	pTxD->LastSec0 = 1;
	pTxD->LastSec1 = 1;
	pTxD->SDLen0 = SrcBufLen;
	pTxD->SDLen1 = 0;
	pTxD->SDPtr0 = SrcBufPA;
	pTxD->DMADONE = 0;

#ifdef RT_BIG_ENDIAN
    RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
    WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, FALSE, TYPE_TXD);
#endif

	pAd->RalinkCounters.KickTxCount++;
	pAd->RalinkCounters.OneSecTxDoneCount++;

	// Increase TX_CTX_IDX, but write to register later.
	INC_RING_INDEX(pAd->TxRing[QueIdx].TxCpuIdx, TX_RING_SIZE);

	RTMP_IO_WRITE32(pAd, TX_CTX_IDX0 + QueIdx*0x10,  pAd->TxRing[QueIdx].TxCpuIdx);

	// Make sure to release MGMT ring resource
//	NdisReleaseSpinLock(&pAd->TxRingLock);

	return NDIS_STATUS_SUCCESS;
}


NDIS_STATUS MlmeDataHardTransmit(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR	QueIdx,
	IN	PNDIS_PACKET	pPacket)
{
	if ((pAd->CommonCfg.RadarDetect.RDMode != RD_NORMAL_MODE)
		)
	{
		return NDIS_STATUS_FAILURE;
	}

	return MlmeHardTransmitTxRing(pAd,QueIdx,pPacket);
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
VOID RTMPWriteTxDescriptor(
	IN	PRTMP_ADAPTER	pAd,
	IN	PTXD_STRUC		pTxD,
	IN	BOOLEAN			bWIV,
	IN	UCHAR			QueueSEL)
{
	//
	// Always use Long preamble before verifiation short preamble functionality works well.
	// Todo: remove the following line if short preamble functionality works
	//
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED);

	pTxD->WIV	= (bWIV) ? 1: 0;
	pTxD->QSEL= (QueueSEL);
	//RT2860c??  fixed using EDCA queue for test...  We doubt Queue1 has problem.  2006-09-26 Jan
	//pTxD->QSEL= FIFO_EDCA;
	/*
	if (pAd->bGenOneHCCA == TRUE)
		pTxD->QSEL= FIFO_HCCA;
	*/
	pTxD->DMADONE = 0;
}
