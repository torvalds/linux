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

#include "rt_config.h"

#ifdef RALINK_ATE
UCHAR TemplateFrame[24] = {0x08/* Data type */,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0xAA,0xBB,0x12,0x34,0x56,0x00,0x11,0x22,0xAA,0xBB,0xCC,0x00,0x00};	// 802.11 MAC Header, Type:Data, Length:24bytes
extern RTMP_RF_REGS RF2850RegTable[];
extern UCHAR NUM_OF_2850_CHNL;

static CHAR CCKRateTable[] = {0, 1, 2, 3, 8, 9, 10, 11, -1}; /* CCK Mode. */
static CHAR OFDMRateTable[] = {0, 1, 2, 3, 4, 5, 6, 7, -1}; /* OFDM Mode. */
static CHAR HTMIXRateTable[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1}; /* HT Mix Mode. */

static INT TxDmaBusy(
	IN PRTMP_ADAPTER pAd);

static INT RxDmaBusy(
	IN PRTMP_ADAPTER pAd);

static VOID RtmpDmaEnable(
	IN PRTMP_ADAPTER pAd,
	IN INT Enable);

static VOID BbpSoftReset(
	IN PRTMP_ADAPTER pAd);

static VOID RtmpRfIoWrite(
	IN PRTMP_ADAPTER pAd);

static INT ATESetUpFrame(
	IN PRTMP_ADAPTER pAd,
	IN UINT32 TxIdx);

static INT ATETxPwrHandler(
	IN PRTMP_ADAPTER pAd,
	IN char index);

static INT ATECmdHandler(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg);

static int CheckMCSValid(
	IN UCHAR Mode,
	IN UCHAR Mcs);

static VOID ATEWriteTxWI(
	IN	PRTMP_ADAPTER	pAd,
	IN	PTXWI_STRUC 	pOutTxWI,
	IN	BOOLEAN			FRAG,
	IN	BOOLEAN			CFACK,
	IN	BOOLEAN			InsTimestamp,
	IN	BOOLEAN 		AMPDU,
	IN	BOOLEAN 		Ack,
	IN	BOOLEAN 		NSeq,		// HW new a sequence.
	IN	UCHAR			BASize,
	IN	UCHAR			WCID,
	IN	ULONG			Length,
	IN	UCHAR 			PID,
	IN	UCHAR			TID,
	IN	UCHAR			TxRate,
	IN	UCHAR			Txopmode,
	IN	BOOLEAN			CfAck,
	IN	HTTRANSMIT_SETTING	*pTransmit);


static VOID SetJapanFilter(
	IN	PRTMP_ADAPTER	pAd);

/*=========================end of prototype=========================*/

static INT TxDmaBusy(
	IN PRTMP_ADAPTER pAd)
{
	INT result;
	WPDMA_GLO_CFG_STRUC GloCfg;

	RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);	// disable DMA
	if (GloCfg.field.TxDMABusy)
		result = 1;
	else
		result = 0;

	return result;
}

static INT RxDmaBusy(
	IN PRTMP_ADAPTER pAd)
{
	INT result;
	WPDMA_GLO_CFG_STRUC GloCfg;

	RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);	// disable DMA
	if (GloCfg.field.RxDMABusy)
		result = 1;
	else
		result = 0;

	return result;
}

static VOID RtmpDmaEnable(
	IN PRTMP_ADAPTER pAd,
	IN INT Enable)
{
	BOOLEAN value;
	ULONG WaitCnt;
	WPDMA_GLO_CFG_STRUC GloCfg;

	value = Enable > 0 ? 1 : 0;

	// check DMA is in busy mode.
	WaitCnt = 0;
	while (TxDmaBusy(pAd) || RxDmaBusy(pAd))
	{
		RTMPusecDelay(10);
		if (WaitCnt++ > 100)
			break;
	}

	RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);	// disable DMA
	GloCfg.field.EnableTxDMA = value;
	GloCfg.field.EnableRxDMA = value;
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);	// abort all TX rings
	RTMPusecDelay(5000);

	return;
}


static VOID BbpSoftReset(
	IN PRTMP_ADAPTER pAd)
{
	UCHAR BbpData = 0;

	// Soft reset, set BBP R21 bit0=1->0
	ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R21, &BbpData);
	BbpData |= 0x00000001; //set bit0=1
	ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R21, BbpData);

	ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R21, &BbpData);
	BbpData &= ~(0x00000001); //set bit0=0
	ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R21, BbpData);

	return;
}

static VOID RtmpRfIoWrite(
	IN PRTMP_ADAPTER pAd)
{
	// Set RF value 1's set R3[bit2] = [0]
	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R1);
	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R2);
	RTMP_RF_IO_WRITE32(pAd, (pAd->LatchRfRegs.R3 & (~0x04)));
	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R4);

	RTMPusecDelay(200);

	// Set RF value 2's set R3[bit2] = [1]
	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R1);
	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R2);
	RTMP_RF_IO_WRITE32(pAd, (pAd->LatchRfRegs.R3 | 0x04));
	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R4);

	RTMPusecDelay(200);

	// Set RF value 3's set R3[bit2] = [0]
	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R1);
	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R2);
	RTMP_RF_IO_WRITE32(pAd, (pAd->LatchRfRegs.R3 & (~0x04)));
	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R4);

	return;
}

static int CheckMCSValid(
	UCHAR Mode,
	UCHAR Mcs)
{
	int i;
	PCHAR pRateTab;

	switch(Mode)
	{
		case 0:
			pRateTab = CCKRateTable;
			break;
		case 1:
			pRateTab = OFDMRateTable;
			break;
		case 2:
		case 3:
			pRateTab = HTMIXRateTable;
			break;
		default:
			ATEDBGPRINT(RT_DEBUG_ERROR, ("unrecognizable Tx Mode %d\n", Mode));
			return -1;
			break;
	}

	i = 0;
	while(pRateTab[i] != -1)
	{
		if (pRateTab[i] == Mcs)
			return 0;
		i++;
	}

	return -1;
}

#if 1
static INT ATETxPwrHandler(
	IN PRTMP_ADAPTER pAd,
	IN char index)
{
	ULONG R;
	CHAR TxPower;
	UCHAR Bbp94 = 0;
	BOOLEAN bPowerReduce = FALSE;

#ifdef RALINK_28xx_QA
	if ((pAd->ate.bQATxStart == TRUE) || (pAd->ate.bQARxStart == TRUE))
	{
		/* When QA is used for Tx, pAd->ate.TxPower0/1 and real tx power
		** are not synchronized.
		*/
/*
		pAd->ate.TxPower0 = pAd->LatchRfRegs.xxx;
		pAd->ate.TxPower1 = pAd->LatchRfRegs.xxx;
*/
		return 0;
	}
	else
#endif // RALINK_28xx_QA //
	{
		TxPower = index == 0 ? pAd->ate.TxPower0 : pAd->ate.TxPower1;

		if (pAd->ate.Channel <= 14)
		{
			if (TxPower > 31)
			{
				//
				// R3, R4 can't large than 31 (0x24), 31 ~ 36 used by BBP 94
				//
				R = 31;
				if (TxPower <= 36)
					Bbp94 = BBPR94_DEFAULT + (UCHAR)(TxPower - 31);
			}
			else if (TxPower < 0)
			{
				//
				// R3, R4 can't less than 0, -1 ~ -6 used by BBP 94
				//
				R = 0;
				if (TxPower >= -6)
					Bbp94 = BBPR94_DEFAULT + TxPower;
			}
			else
			{
				// 0 ~ 31
				R = (ULONG) TxPower;
				Bbp94 = BBPR94_DEFAULT;
			}

			ATEDBGPRINT(RT_DEBUG_TRACE, ("%s (TxPower=%d, R=%ld, BBP_R94=%d)\n", __func__, TxPower, R, Bbp94));
		}
		else// 5.5 GHz
		{
			if (TxPower > 15)
			{
				//
				// R3, R4 can't large than 15 (0x0F)
				//
				R = 15;
			}
			else if (TxPower < 0)
			{
				//
				// R3, R4 can't less than 0
				//
				// -1 ~ -7
				ASSERT((TxPower >= -7));
				R = (ULONG)(TxPower + 7);
				bPowerReduce = TRUE;
			}
			else
			{
				// 0 ~ 15
				R = (ULONG) TxPower;
			}

			ATEDBGPRINT(RT_DEBUG_TRACE, ("%s (TxPower=%d, R=%lu)\n", __func__, TxPower, R));
		}

		if (pAd->ate.Channel <= 14)
		{
			if (index == 0)
			{
				R = R << 9;		// shift TX power control to correct RF(R3) register bit position
				R |= (pAd->LatchRfRegs.R3 & 0xffffc1ff);
				pAd->LatchRfRegs.R3 = R;
			}
			else
			{
				R = R << 6;		// shift TX power control to correct RF(R4) register bit position
				R |= (pAd->LatchRfRegs.R4 & 0xfffff83f);
				pAd->LatchRfRegs.R4 = R;
			}
		}
		else// 5.5GHz
		{
			if (bPowerReduce == FALSE)
			{
				if (index == 0)
				{
					R = (R << 10) | (1 << 9);		// shift TX power control to correct RF(R3) register bit position
					R |= (pAd->LatchRfRegs.R3 & 0xffffc1ff);
					pAd->LatchRfRegs.R3 = R;
				}
				else
				{
					R = (R << 7) | (1 << 6);		// shift TX power control to correct RF(R4) register bit position
					R |= (pAd->LatchRfRegs.R4 & 0xfffff83f);
					pAd->LatchRfRegs.R4 = R;
				}
			}
			else
			{
				if (index == 0)
				{
					R = (R << 10);		// shift TX power control to correct RF(R3) register bit position
					R |= (pAd->LatchRfRegs.R3 & 0xffffc1ff);

					/* Clear bit 9 of R3 to reduce 7dB. */
					pAd->LatchRfRegs.R3 = (R & (~(1 << 9)));
				}
				else
				{
					R = (R << 7);		// shift TX power control to correct RF(R4) register bit position
					R |= (pAd->LatchRfRegs.R4 & 0xfffff83f);

					/* Clear bit 6 of R4 to reduce 7dB. */
					pAd->LatchRfRegs.R4 = (R & (~(1 << 6)));
				}
			}
		}

		RtmpRfIoWrite(pAd);

		return 0;
	}
}
#else// 1 //
static INT ATETxPwrHandler(
	IN PRTMP_ADAPTER pAd,
	IN char index)
{
	ULONG R;
	CHAR TxPower;
	UCHAR Bbp94 = 0;

#ifdef RALINK_28xx_QA
	if ((pAd->ate.bQATxStart == TRUE) || (pAd->ate.bQARxStart == TRUE))
	{
		// TODO: how to get current TxPower0/1 from pAd->LatchRfRegs ?
		/* When QA is used for Tx, pAd->ate.TxPower0/1 and real tx power
		** are not synchronized.
		*/
/*
		pAd->ate.TxPower0 = pAd->LatchRfRegs.xxx;
		pAd->ate.TxPower1 = pAd->LatchRfRegs.xxx;
*/
		return 0;
	}
	else
#endif // RALINK_28xx_QA //
	{
		TxPower = index == 0 ? pAd->ate.TxPower0 : pAd->ate.TxPower1;

	if (TxPower > 31)
	{
		//
		// R3, R4 can't large than 36 (0x24), 31 ~ 36 used by BBP 94
		//
		R = 31;
		if (TxPower <= 36)
			Bbp94 = BBPR94_DEFAULT + (UCHAR)(TxPower - 31);
	}
	else if (TxPower < 0)
	{
		//
		// R3, R4 can't less than 0, -1 ~ -6 used by BBP 94
		//
		R = 0;
		if (TxPower >= -6)
			Bbp94 = BBPR94_DEFAULT + TxPower;
	}
	else
	{
		// 0 ~ 31
		R = (ULONG) TxPower;
		Bbp94 = BBPR94_DEFAULT;
	}

	ATEDBGPRINT(RT_DEBUG_TRACE, ("%s (TxPower=%d, R3=%ld, BBP_R94=%d)\n", __func__, TxPower, R, Bbp94));

		if (pAd->ate.Channel <= 14)
		{
	if (index == 0)
	{
		R = R << 9;		// shift TX power control to correct RF(R3) register bit position
		R |= (pAd->LatchRfRegs.R3 & 0xffffc1ff);
		pAd->LatchRfRegs.R3 = R;
	}
	else
	{
		R = R << 6;		// shift TX power control to correct RF(R4) register bit position
		R |= (pAd->LatchRfRegs.R4 & 0xfffff83f);
		pAd->LatchRfRegs.R4 = R;
	}
		}
		else
		{
			if (index == 0)
			{
				R = (R << 10) | (1 << 9);		// shift TX power control to correct RF(R3) register bit position
				R |= (pAd->LatchRfRegs.R3 & 0xffffc1ff);
				pAd->LatchRfRegs.R3 = R;
			}
			else
			{
				R = (R << 7) | (1 << 6);		// shift TX power control to correct RF(R4) register bit position
				R |= (pAd->LatchRfRegs.R4 & 0xfffff83f);
				pAd->LatchRfRegs.R4 = R;
			}
		}

	RtmpRfIoWrite(pAd);

	return 0;
	}
}
#endif // 1 //
/*
    ==========================================================================
    Description:
        Set ATE operation mode to
        0. ATESTART  = Start ATE Mode
        1. ATESTOP   = Stop ATE Mode
        2. TXCONT    = Continuous Transmit
        3. TXCARR    = Transmit Carrier
        4. TXFRAME   = Transmit Frames
        5. RXFRAME   = Receive Frames
#ifdef RALINK_28xx_QA
        6. TXSTOP    = Stop Any Type of Transmition
        7. RXSTOP    = Stop Receiving Frames
#endif // RALINK_28xx_QA //
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT	ATECmdHandler(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	UINT32			Value = 0;
	UCHAR			BbpData;
	UINT32			MacData = 0;
	PTXD_STRUC		pTxD;
	INT				index;
	UINT			i=0, atemode;
	PRXD_STRUC		pRxD;
	PRTMP_TX_RING 	pTxRing = &pAd->TxRing[QID_AC_BE];
#ifndef UCOS
	NDIS_STATUS		Status = NDIS_STATUS_SUCCESS;
#endif // UCOS //
#ifdef	RT_BIG_ENDIAN
    PTXD_STRUC      pDestTxD;
    TXD_STRUC       TxD;
#endif
	ATEDBGPRINT(RT_DEBUG_TRACE, ("===> ATECmdHandler()\n"));

	ATEAsicSwitchChannel(pAd);
	AsicLockChannel(pAd, pAd->ate.Channel);

	RTMPusecDelay(5000);

	// read MAC_SYS_CTRL and backup MAC_SYS_CTRL value.
	RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &MacData);

	// Default value in BBP R22 is 0x0.
	BbpData = 0;

	// clean bit4 to stop continuous Tx production test.
	MacData &= 0xFFFFFFEF;

	if (!strcmp(arg, "ATESTART")) 		//Enter ATE mode and set Tx/Rx Idle
	{
		ATEDBGPRINT(RT_DEBUG_TRACE, ("ATE: ATESTART\n"));

#ifndef UCOS
		// check if we have removed the firmware
		if (!(ATE_ON(pAd)))
		{
			NICEraseFirmware(pAd);
		}
#endif // !UCOS //
		atemode = pAd->ate.Mode;
		pAd->ate.Mode = ATE_START;
//		pAd->ate.TxDoneCount = pAd->ate.TxCount;
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, MacData);

		if (atemode & ATE_TXCARR)
		{
			// No Carrier Test set BBP R22 bit7=0, bit6=0, bit[5~0]=0x0
			ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R22, &BbpData);
			BbpData &= 0xFFFFFF00; //clear bit7, bit6, bit[5~0]
		    ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R22, BbpData);
		}
		else if (atemode & ATE_TXCARRSUPP)
		{
			// No Cont. TX set BBP R22 bit7=0
			ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R22, &BbpData);
			BbpData &= ~(1 << 7); //set bit7=0
			ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R22, BbpData);

			// No Carrier Suppression set BBP R24 bit0=0
			ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R24, &BbpData);
			BbpData &= 0xFFFFFFFE; //clear bit0
		    ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R24, BbpData);
		}
		// We should free some resource which was allocated when ATE_TXFRAME , ATE_STOP, and ATE_TXCONT.
		else if ((atemode & ATE_TXFRAME) || (atemode == ATE_STOP))
		{
			PRTMP_TX_RING pTxRing = &pAd->TxRing[QID_AC_BE];

			if (atemode & ATE_TXCONT)
			{
				// No Cont. TX set BBP R22 bit7=0
				ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R22, &BbpData);
				BbpData &= ~(1 << 7); //set bit7=0
				ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R22, BbpData);
			}
			// Abort Tx, Rx DMA.
			RtmpDmaEnable(pAd, 0);
			for (i=0; i<TX_RING_SIZE; i++)
			{
				PNDIS_PACKET  pPacket;

#ifndef RT_BIG_ENDIAN
			    pTxD = (PTXD_STRUC)pAd->TxRing[QID_AC_BE].Cell[i].AllocVa;
#else
        		pDestTxD = (PTXD_STRUC)pAd->TxRing[QID_AC_BE].Cell[i].AllocVa;
        		TxD = *pDestTxD;
        		pTxD = &TxD;
        		RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
#endif
				pTxD->DMADONE = 0;
				pPacket = pTxRing->Cell[i].pNdisPacket;
				if (pPacket)
				{
					PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr0, pTxD->SDLen0, PCI_DMA_TODEVICE);
					RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
				}
				//Always assign pNdisPacket as NULL after clear
				pTxRing->Cell[i].pNdisPacket = NULL;

				pPacket = pTxRing->Cell[i].pNextNdisPacket;
				if (pPacket)
				{
					PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1, pTxD->SDLen1, PCI_DMA_TODEVICE);
					RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
				}
				//Always assign pNextNdisPacket as NULL after clear
				pTxRing->Cell[i].pNextNdisPacket = NULL;
#ifdef RT_BIG_ENDIAN
				RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
				WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, FALSE, TYPE_TXD);
#endif
			}
			// Start Tx, RX DMA
			RtmpDmaEnable(pAd, 1);
		}
		// reset Rx statistics.
		pAd->ate.LastSNR0 = 0;
		pAd->ate.LastSNR1 = 0;
		pAd->ate.LastRssi0 = 0;
		pAd->ate.LastRssi1 = 0;
		pAd->ate.LastRssi2 = 0;
		pAd->ate.AvgRssi0 = 0;
		pAd->ate.AvgRssi1 = 0;
		pAd->ate.AvgRssi2 = 0;
		pAd->ate.AvgRssi0X8 = 0;
		pAd->ate.AvgRssi1X8 = 0;
		pAd->ate.AvgRssi2X8 = 0;
		pAd->ate.NumOfAvgRssiSample = 0;

#ifdef RALINK_28xx_QA
		// Tx frame
		pAd->ate.bQATxStart = FALSE;
		pAd->ate.bQARxStart = FALSE;
		pAd->ate.seq = 0;

		// counters
		pAd->ate.U2M = 0;
		pAd->ate.OtherData = 0;
		pAd->ate.Beacon = 0;
		pAd->ate.OtherCount = 0;
		pAd->ate.TxAc0 = 0;
		pAd->ate.TxAc1 = 0;
		pAd->ate.TxAc2 = 0;
		pAd->ate.TxAc3 = 0;
		pAd->ate.TxHCCA = 0;
		pAd->ate.TxMgmt = 0;
		pAd->ate.RSSI0 = 0;
		pAd->ate.RSSI1 = 0;
		pAd->ate.RSSI2 = 0;
		pAd->ate.SNR0 = 0;
		pAd->ate.SNR1 = 0;

		// control
		pAd->ate.TxDoneCount = 0;
		pAd->ate.TxStatus = 0; // task Tx status // 0 --> task is idle, 1 --> task is running
#endif // RALINK_28xx_QA //

		// Soft reset BBP.
		BbpSoftReset(pAd);


#ifdef CONFIG_STA_SUPPORT
		//
		// LinkDown() has "AsicDisableSync();" and "RTMP_BBP_IO_R/W8_BY_REG_ID();" inside.
		//
//      LinkDown(pAd, FALSE);
//		AsicEnableBssSync(pAd);
#ifndef UCOS
		netif_stop_queue(pAd->net_dev);
#endif // !UCOS //
		//
		// If we skip "LinkDown()", we should disable protection
		// to prevent from sending out RTS or CTS-to-self.
		//
		ATEDisableAsicProtect(pAd);
		RTMPStationStop(pAd);
#endif // CONFIG_STA_SUPPORT //

		/* Disable Tx */
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value &= ~(1 << 2);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);

		/* Disable Rx */
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value &= ~(1 << 3);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);
	}
	else if (!strcmp(arg, "ATESTOP"))
	{
		ATEDBGPRINT(RT_DEBUG_TRACE, ("ATE: ATESTOP\n"));

		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R22, BbpData);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, MacData); // recover the MAC_SYS_CTRL register back.

		// Disable Tx, Rx
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value &= (0xfffffff3);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);

		// Abort Tx, RX DMA.
		RtmpDmaEnable(pAd, 0);

#ifndef UCOS
		pAd->ate.bFWLoading = TRUE;
		Status = NICLoadFirmware(pAd);
		if (Status != NDIS_STATUS_SUCCESS)
		{
			ATEDBGPRINT(RT_DEBUG_ERROR, ("NICLoadFirmware failed, Status[=0x%08x]\n", Status));
			return FALSE;
		}
#endif // !UCOS //
		pAd->ate.Mode = ATE_STOP;


#ifdef CONFIG_STA_SUPPORT
		//
		// Even the firmware has been loaded,
		// we still could use ATE_BBP_IO_READ8_BY_REG_ID().
		// But this is not suggested.
		//
		BbpSoftReset(pAd);
#endif // CONFIG_STA_SUPPORT //

		NICDisableInterrupt(pAd);

		NICInitializeAdapter(pAd, TRUE);


		// Reinitialize Rx Ring before Rx DMA is enabled.
		// The nightmare of >>>RxCoherent<<< was gone !
		for (index = 0; index < RX_RING_SIZE; index++)
		{
			pRxD = (PRXD_STRUC) pAd->RxRing.Cell[index].AllocVa;
			pRxD->DDONE = 0;
		}

		// We should read EEPROM for all cases.
		NICReadEEPROMParameters(pAd, NULL);
		NICInitAsicFromEEPROM(pAd);

		AsicSwitchChannel(pAd, pAd->CommonCfg.Channel, FALSE);
		AsicLockChannel(pAd, pAd->CommonCfg.Channel);

		//
		// Enable Interrupt
		//

		//
		// These steps are only for APAutoSelectChannel().
		//
#if 0
		//pAd->bStaFifoTest = TRUE;
		pAd->int_enable_reg = ((DELAYINTMASK)  | (RxINT|TxDataInt|TxMgmtInt)) & ~(0x03);
		pAd->int_disable_mask = 0;
		pAd->int_pending = 0;
#endif
		RTMP_IO_WRITE32(pAd, INT_SOURCE_CSR, 0xffffffff);  // clear garbage interrupts
		NICEnableInterrupt(pAd);


/*=========================================================================*/
		/* restore RX_FILTR_CFG */
#ifdef CONFIG_STA_SUPPORT
		/* restore RX_FILTR_CFG due to that QA maybe set it to 0x3 */
		RTMP_IO_WRITE32(pAd, RX_FILTR_CFG, STANORMAL);
#endif // CONFIG_STA_SUPPORT //
/*=========================================================================*/

		// Enable Tx
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value |= (1 << 2);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);

		// Enable Tx, Rx DMA.
		RtmpDmaEnable(pAd, 1);

		// Enable Rx
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value |= (1 << 3);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);


#ifdef CONFIG_STA_SUPPORT
		RTMPStationStart(pAd);
#endif // CONFIG_STA_SUPPORT //
#ifndef UCOS
		netif_start_queue(pAd->net_dev);
#endif // !UCOS //
	}
	else if (!strcmp(arg, "TXCARR"))	// Tx Carrier
	{
		ATEDBGPRINT(RT_DEBUG_TRACE, ("ATE: TXCARR\n"));
		pAd->ate.Mode |= ATE_TXCARR;

		// QA has done the following steps if it is used.
		if (pAd->ate.bQATxStart == FALSE)
		{
			// Soft reset BBP.
			BbpSoftReset(pAd);

			// Carrier Test set BBP R22 bit7=1, bit6=1, bit[5~0]=0x01
			ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R22, &BbpData);
			BbpData &= 0xFFFFFF00; //clear bit7, bit6, bit[5~0]
			BbpData |= 0x000000C1; //set bit7=1, bit6=1, bit[5~0]=0x01
			ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R22, BbpData);

			// set MAC_SYS_CTRL(0x1004) Continuous Tx Production Test (bit4) = 1
			RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
			Value = Value | 0x00000010;
			RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);
		}
	}
	else if (!strcmp(arg, "TXCONT"))	// Tx Continue
	{
		if (pAd->ate.bQATxStart == TRUE)
		{
			/* set MAC_SYS_CTRL(0x1004) bit4(Continuous Tx Production Test)
			   and bit2(MAC TX enable) back to zero. */
			RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &MacData);
			MacData &= 0xFFFFFFEB;
			RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, MacData);

			// set BBP R22 bit7=0
			ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R22, &BbpData);
			BbpData &= 0xFFFFFF7F; //set bit7=0
			ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R22, BbpData);
		}

		/* for TxCont mode.
		** Step 1: Send 50 packets first then wait for a moment.
		** Step 2: Send more 50 packet then start continue mode.
		*/
		ATEDBGPRINT(RT_DEBUG_TRACE, ("ATE: TXCONT\n"));
		// Step 1: send 50 packets first.
		pAd->ate.Mode |= ATE_TXCONT;
		pAd->ate.TxCount = 50;
                /* Do it after Tx/Rx DMA is aborted. */
//		pAd->ate.TxDoneCount = 0;

		// Soft reset BBP.
		BbpSoftReset(pAd);

		// Abort Tx, RX DMA.
		RtmpDmaEnable(pAd, 0);

		// Fix can't smooth kick
		{
			RTMP_IO_READ32(pAd, TX_DTX_IDX0 + QID_AC_BE * 0x10,  &pTxRing->TxDmaIdx);
			pTxRing->TxSwFreeIdx = pTxRing->TxDmaIdx;
			pTxRing->TxCpuIdx = pTxRing->TxDmaIdx;
			RTMP_IO_WRITE32(pAd, TX_CTX_IDX0 + QID_AC_BE * 0x10, pTxRing->TxCpuIdx);
		}

		pAd->ate.TxDoneCount = 0;

		/* Only needed if we have to send some normal frames. */
		SetJapanFilter(pAd);

		for (i = 0; (i < TX_RING_SIZE-1) && (i < pAd->ate.TxCount); i++)
		{
			PNDIS_PACKET pPacket;
			UINT32 TxIdx = pTxRing->TxCpuIdx;

#ifndef RT_BIG_ENDIAN
			pTxD = (PTXD_STRUC)pTxRing->Cell[TxIdx].AllocVa;
#else
			pDestTxD = (PTXD_STRUC)pTxRing->Cell[TxIdx].AllocVa;
			TxD = *pDestTxD;
			pTxD = &TxD;
			RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
#endif
			// Clean current cell.
			pPacket = pTxRing->Cell[TxIdx].pNdisPacket;
			if (pPacket)
			{
				PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr0, pTxD->SDLen0, PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
			}
			//Always assign pNdisPacket as NULL after clear
			pTxRing->Cell[TxIdx].pNdisPacket = NULL;

			pPacket = pTxRing->Cell[TxIdx].pNextNdisPacket;
			if (pPacket)
			{
				PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1, pTxD->SDLen1, PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
			}
			//Always assign pNextNdisPacket as NULL after clear
			pTxRing->Cell[TxIdx].pNextNdisPacket = NULL;

#ifdef RT_BIG_ENDIAN
			RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
			WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, FALSE, TYPE_TXD);
#endif

			if (ATESetUpFrame(pAd, TxIdx) != 0)
				break;

			INC_RING_INDEX(pTxRing->TxCpuIdx, TX_RING_SIZE);
		}

                // Setup frame format.
		ATESetUpFrame(pAd, pTxRing->TxCpuIdx);

		// Start Tx, RX DMA.
		RtmpDmaEnable(pAd, 1);

		// Enable Tx
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value |= (1 << 2);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);

		// Disable Rx
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value &= ~(1 << 3);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);

#ifdef RALINK_28xx_QA
		if (pAd->ate.bQATxStart == TRUE)
		{
			pAd->ate.TxStatus = 1;
			//pAd->ate.Repeat = 0;
		}
#endif // RALINK_28xx_QA //

		// kick Tx-Ring.
		RTMP_IO_WRITE32(pAd, TX_CTX_IDX0 + QID_AC_BE * RINGREG_DIFF, pAd->TxRing[QID_AC_BE].TxCpuIdx);

		RTMPusecDelay(5000);


		// Step 2: send more 50 packets then start continue mode.
		// Abort Tx, RX DMA.
		RtmpDmaEnable(pAd, 0);

		// Cont. TX set BBP R22 bit7=1
		ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R22, &BbpData);
		BbpData |= 0x00000080; //set bit7=1
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R22, BbpData);

		pAd->ate.TxCount = 50;

		// Fix can't smooth kick
		{
			RTMP_IO_READ32(pAd, TX_DTX_IDX0 + QID_AC_BE * 0x10,  &pTxRing->TxDmaIdx);
			pTxRing->TxSwFreeIdx = pTxRing->TxDmaIdx;
			pTxRing->TxCpuIdx = pTxRing->TxDmaIdx;
			RTMP_IO_WRITE32(pAd, TX_CTX_IDX0 + QID_AC_BE * 0x10, pTxRing->TxCpuIdx);
		}

		pAd->ate.TxDoneCount = 0;

		SetJapanFilter(pAd);

		for (i = 0; (i < TX_RING_SIZE-1) && (i < pAd->ate.TxCount); i++)
		{
			PNDIS_PACKET pPacket;
			UINT32 TxIdx = pTxRing->TxCpuIdx;

#ifndef RT_BIG_ENDIAN
			pTxD = (PTXD_STRUC)pTxRing->Cell[TxIdx].AllocVa;
#else
			pDestTxD = (PTXD_STRUC)pTxRing->Cell[TxIdx].AllocVa;
			TxD = *pDestTxD;
			pTxD = &TxD;
			RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
#endif
			// clean current cell.
			pPacket = pTxRing->Cell[TxIdx].pNdisPacket;
			if (pPacket)
			{
				PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr0, pTxD->SDLen0, PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
			}
			//Always assign pNdisPacket as NULL after clear
			pTxRing->Cell[TxIdx].pNdisPacket = NULL;

			pPacket = pTxRing->Cell[TxIdx].pNextNdisPacket;
			if (pPacket)
			{
				PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1, pTxD->SDLen1, PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
			}
			//Always assign pNextNdisPacket as NULL after clear
			pTxRing->Cell[TxIdx].pNextNdisPacket = NULL;

#ifdef RT_BIG_ENDIAN
			RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
			WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, FALSE, TYPE_TXD);
#endif

			if (ATESetUpFrame(pAd, TxIdx) != 0)
				break;

			INC_RING_INDEX(pTxRing->TxCpuIdx, TX_RING_SIZE);
		}

		ATESetUpFrame(pAd, pTxRing->TxCpuIdx);

		// Start Tx, RX DMA.
		RtmpDmaEnable(pAd, 1);

		// Enable Tx
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value |= (1 << 2);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);

		// Disable Rx
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value &= ~(1 << 3);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);

#ifdef RALINK_28xx_QA
		if (pAd->ate.bQATxStart == TRUE)
		{
			pAd->ate.TxStatus = 1;
			//pAd->ate.Repeat = 0;
		}
#endif // RALINK_28xx_QA //

		// kick Tx-Ring.
		RTMP_IO_WRITE32(pAd, TX_CTX_IDX0 + QID_AC_BE * RINGREG_DIFF, pAd->TxRing[QID_AC_BE].TxCpuIdx);

		RTMPusecDelay(500);

		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &MacData);
		MacData |= 0x00000010;
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, MacData);
	}
	else if (!strcmp(arg, "TXFRAME")) // Tx Frames
	{
		ATEDBGPRINT(RT_DEBUG_TRACE, ("ATE: TXFRAME(Count=%d)\n", pAd->ate.TxCount));
		pAd->ate.Mode |= ATE_TXFRAME;
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R22, BbpData);

		// Soft reset BBP.
		BbpSoftReset(pAd);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, MacData);

		// Abort Tx, RX DMA.
		RtmpDmaEnable(pAd, 0);

		// Fix can't smooth kick
		{
			RTMP_IO_READ32(pAd, TX_DTX_IDX0 + QID_AC_BE * 0x10,  &pTxRing->TxDmaIdx);
			pTxRing->TxSwFreeIdx = pTxRing->TxDmaIdx;
			pTxRing->TxCpuIdx = pTxRing->TxDmaIdx;
			RTMP_IO_WRITE32(pAd, TX_CTX_IDX0 + QID_AC_BE * 0x10, pTxRing->TxCpuIdx);
		}

		pAd->ate.TxDoneCount = 0;

		SetJapanFilter(pAd);

		for (i = 0; (i < TX_RING_SIZE-1) && (i < pAd->ate.TxCount); i++)
		{
			PNDIS_PACKET pPacket;
			UINT32 TxIdx = pTxRing->TxCpuIdx;

#ifndef RT_BIG_ENDIAN
			pTxD = (PTXD_STRUC)pTxRing->Cell[TxIdx].AllocVa;
#else
			pDestTxD = (PTXD_STRUC)pTxRing->Cell[TxIdx].AllocVa;
			TxD = *pDestTxD;
			pTxD = &TxD;
			RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
#endif
			// Clean current cell.
			pPacket = pTxRing->Cell[TxIdx].pNdisPacket;
			if (pPacket)
			{
				PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr0, pTxD->SDLen0, PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
			}
			//Always assign pNdisPacket as NULL after clear
			pTxRing->Cell[TxIdx].pNdisPacket = NULL;

			pPacket = pTxRing->Cell[TxIdx].pNextNdisPacket;
			if (pPacket)
			{
				PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1, pTxD->SDLen1, PCI_DMA_TODEVICE);
				RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
			}
			//Always assign pNextNdisPacket as NULL after clear
			pTxRing->Cell[TxIdx].pNextNdisPacket = NULL;

#ifdef RT_BIG_ENDIAN
			RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
			WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, FALSE, TYPE_TXD);
#endif

			if (ATESetUpFrame(pAd, TxIdx) != 0)
				break;

			INC_RING_INDEX(pTxRing->TxCpuIdx, TX_RING_SIZE);

		}

		ATESetUpFrame(pAd, pTxRing->TxCpuIdx);

		// Start Tx, Rx DMA.
		RtmpDmaEnable(pAd, 1);

		// Enable Tx
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value |= (1 << 2);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);
#ifdef RALINK_28xx_QA
		// add this for LoopBack mode
		if (pAd->ate.bQARxStart == FALSE)
		{
			// Disable Rx
			RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
			Value &= ~(1 << 3);
			RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);
		}

		if (pAd->ate.bQATxStart == TRUE)
		{
			pAd->ate.TxStatus = 1;
			//pAd->ate.Repeat = 0;
		}
#else
		// Disable Rx
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value &= ~(1 << 3);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);
#endif // RALINK_28xx_QA //

		RTMP_IO_READ32(pAd, TX_DTX_IDX0 + QID_AC_BE * RINGREG_DIFF, &pAd->TxRing[QID_AC_BE].TxDmaIdx);
		// kick Tx-Ring.
		RTMP_IO_WRITE32(pAd, TX_CTX_IDX0 + QID_AC_BE * RINGREG_DIFF, pAd->TxRing[QID_AC_BE].TxCpuIdx);

		pAd->RalinkCounters.KickTxCount++;
	}
#ifdef RALINK_28xx_QA
	else if (!strcmp(arg, "TXSTOP"))
	{
		ATEDBGPRINT(RT_DEBUG_TRACE, ("ATE: TXSTOP\n"));
		atemode = pAd->ate.Mode;
		pAd->ate.Mode &= ATE_TXSTOP;
		pAd->ate.bQATxStart = FALSE;
//		pAd->ate.TxDoneCount = pAd->ate.TxCount;

		if (atemode & ATE_TXCARR)
		{
			// No Carrier Test set BBP R22 bit7=0, bit6=0, bit[5~0]=0x0
			ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R22, &BbpData);
			BbpData &= 0xFFFFFF00; //clear bit7, bit6, bit[5~0]
		    ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R22, BbpData);
		}
		else if (atemode & ATE_TXCARRSUPP)
		{
			// No Cont. TX set BBP R22 bit7=0
			ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R22, &BbpData);
			BbpData &= ~(1 << 7); //set bit7=0
			ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R22, BbpData);

			// No Carrier Suppression set BBP R24 bit0=0
			ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R24, &BbpData);
			BbpData &= 0xFFFFFFFE; //clear bit0
		    ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R24, BbpData);
		}
		// We should free some resource which allocate when ATE_TXFRAME , ATE_STOP, and ATE_TXCONT.
		else if ((atemode & ATE_TXFRAME) || (atemode == ATE_STOP))
		{

			PRTMP_TX_RING pTxRing = &pAd->TxRing[QID_AC_BE];

			if (atemode & ATE_TXCONT)
			{
				// No Cont. TX set BBP R22 bit7=0
				ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R22, &BbpData);
				BbpData &= ~(1 << 7); //set bit7=0
				ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R22, BbpData);
			}

			// Abort Tx, Rx DMA.
			RtmpDmaEnable(pAd, 0);
			for (i=0; i<TX_RING_SIZE; i++)
			{
				PNDIS_PACKET  pPacket;

#ifndef RT_BIG_ENDIAN
			    pTxD = (PTXD_STRUC)pAd->TxRing[QID_AC_BE].Cell[i].AllocVa;
#else
        		pDestTxD = (PTXD_STRUC)pAd->TxRing[QID_AC_BE].Cell[i].AllocVa;
        		TxD = *pDestTxD;
        		pTxD = &TxD;
        		RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
#endif
				pTxD->DMADONE = 0;
				pPacket = pTxRing->Cell[i].pNdisPacket;
				if (pPacket)
				{
					PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr0, pTxD->SDLen0, PCI_DMA_TODEVICE);
					RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
				}
				//Always assign pNdisPacket as NULL after clear
				pTxRing->Cell[i].pNdisPacket = NULL;

				pPacket = pTxRing->Cell[i].pNextNdisPacket;
				if (pPacket)
				{
					PCI_UNMAP_SINGLE(pAd, pTxD->SDPtr1, pTxD->SDLen1, PCI_DMA_TODEVICE);
					RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_SUCCESS);
				}
				//Always assign pNextNdisPacket as NULL after clear
				pTxRing->Cell[i].pNextNdisPacket = NULL;
#ifdef RT_BIG_ENDIAN
				RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
				WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, FALSE, TYPE_TXD);
#endif
			}
			// Enable Tx, Rx DMA
			RtmpDmaEnable(pAd, 1);

		}

		// control
//		pAd->ate.TxDoneCount = 0;
		pAd->ate.TxStatus = 0; // task Tx status // 0 --> task is idle, 1 --> task is running

		// Soft reset BBP.
		BbpSoftReset(pAd);

		// Disable Tx
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value &= ~(1 << 2);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);
	}
	else if (!strcmp(arg, "RXSTOP"))
	{
		ATEDBGPRINT(RT_DEBUG_TRACE, ("ATE: RXSTOP\n"));
		atemode = pAd->ate.Mode;
		pAd->ate.Mode &= ATE_RXSTOP;
		pAd->ate.bQARxStart = FALSE;
//		pAd->ate.TxDoneCount = pAd->ate.TxCount;

		if (atemode & ATE_TXCARR)
		{
			;
		}
		else if (atemode & ATE_TXCARRSUPP)
		{
			;
		}

		// We should free some resource which was allocated when ATE_TXFRAME , ATE_STOP, and ATE_TXCONT.
		else if ((atemode & ATE_TXFRAME) || (atemode == ATE_STOP))
		{
			if (atemode & ATE_TXCONT)
			{
				;
			}
		}

		// control
//		pAd->ate.TxDoneCount = 0;
//		pAd->ate.TxStatus = 0; // task Tx status // 0 --> task is idle, 1 --> task is running

		// Soft reset BBP.
		BbpSoftReset(pAd);

		// Disable Rx
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value &= ~(1 << 3);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);
	}
#endif // RALINK_28xx_QA //
	else if (!strcmp(arg, "RXFRAME")) // Rx Frames
	{
		ATEDBGPRINT(RT_DEBUG_TRACE, ("ATE: RXFRAME\n"));

		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R22, BbpData);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, MacData);

		pAd->ate.Mode |= ATE_RXFRAME;

		// Disable Tx of MAC block.
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value &= ~(1 << 2);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);

		// Enable Rx of MAC block.
		RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
		Value |= (1 << 3);
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);
	}
	else
	{
		ATEDBGPRINT(RT_DEBUG_TRACE, ("ATE: Invalid arg!\n"));
		return FALSE;
	}
	RTMPusecDelay(5000);

	ATEDBGPRINT(RT_DEBUG_TRACE, ("<=== ATECmdHandler()\n"));

	return TRUE;
}
/*                                                           */
/*                                                           */
/*=======================End of RT2860=======================*/


/*======================Start of RT2870======================*/
/*                                                           */
/*                                                           */


INT	Set_ATE_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	if (ATECmdHandler(pAd, arg))
	{
		ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_Proc Success\n"));


		return TRUE;
	}
	else
	{
		ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_Proc Failed\n"));
		return FALSE;
	}
}

/*
    ==========================================================================
    Description:
        Set ATE ADDR1=DA for TxFrame(AP  : To DS = 0 ; From DS = 1)
        or
        Set ATE ADDR3=DA for TxFrame(STA : To DS = 1 ; From DS = 0)

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_DA_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	CHAR				*value;
	INT					i;

	if(strlen(arg) != 17)  //Mac address acceptable format 01:02:03:04:05:06 length 17
		return FALSE;

    for (i=0, value = rstrtok(arg, ":"); value; value = rstrtok(NULL, ":"))
	{
		if((strlen(value) != 2) || (!isxdigit(*value)) || (!isxdigit(*(value+1))) )
			return FALSE;  //Invalid


#ifdef CONFIG_STA_SUPPORT
		AtoH(value, &pAd->ate.Addr3[i++], 1);
#endif // CONFIG_STA_SUPPORT //
	}

	if(i != 6)
		return FALSE;  //Invalid


#ifdef CONFIG_STA_SUPPORT
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_DA_Proc (DA = %2X:%2X:%2X:%2X:%2X:%2X)\n", pAd->ate.Addr3[0],
		pAd->ate.Addr3[1], pAd->ate.Addr3[2], pAd->ate.Addr3[3], pAd->ate.Addr3[4], pAd->ate.Addr3[5]));
#endif // CONFIG_STA_SUPPORT //

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_DA_Proc Success\n"));

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE ADDR3=SA for TxFrame(AP  : To DS = 0 ; From DS = 1)
        or
        Set ATE ADDR2=SA for TxFrame(STA : To DS = 1 ; From DS = 0)

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_SA_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	CHAR				*value;
	INT					i;

	if(strlen(arg) != 17)  //Mac address acceptable format 01:02:03:04:05:06 length 17
		return FALSE;

    for (i=0, value = rstrtok(arg, ":"); value; value = rstrtok(NULL, ":"))
	{
		if((strlen(value) != 2) || (!isxdigit(*value)) || (!isxdigit(*(value+1))) )
			return FALSE;  //Invalid


#ifdef CONFIG_STA_SUPPORT
		AtoH(value, &pAd->ate.Addr2[i++], 1);
#endif // CONFIG_STA_SUPPORT //
	}

	if(i != 6)
		return FALSE;  //Invalid


#ifdef CONFIG_STA_SUPPORT
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_SA_Proc (SA = %2X:%2X:%2X:%2X:%2X:%2X)\n", pAd->ate.Addr2[0],
		pAd->ate.Addr2[1], pAd->ate.Addr2[2], pAd->ate.Addr2[3], pAd->ate.Addr2[4], pAd->ate.Addr2[5]));
#endif // CONFIG_STA_SUPPORT //

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_SA_Proc Success\n"));

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE ADDR2=BSSID for TxFrame(AP  : To DS = 0 ; From DS = 1)
        or
        Set ATE ADDR1=BSSID for TxFrame(STA : To DS = 1 ; From DS = 0)

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_BSSID_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	CHAR				*value;
	INT					i;

	if(strlen(arg) != 17)  //Mac address acceptable format 01:02:03:04:05:06 length 17
		return FALSE;

    for (i=0, value = rstrtok(arg, ":"); value; value = rstrtok(NULL, ":"))
	{
		if((strlen(value) != 2) || (!isxdigit(*value)) || (!isxdigit(*(value+1))) )
			return FALSE;  //Invalid


#ifdef CONFIG_STA_SUPPORT
		AtoH(value, &pAd->ate.Addr1[i++], 1);
#endif // CONFIG_STA_SUPPORT //
	}

	if(i != 6)
		return FALSE;  //Invalid


#ifdef CONFIG_STA_SUPPORT
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_BSSID_Proc (BSSID = %2X:%2X:%2X:%2X:%2X:%2X)\n",	pAd->ate.Addr1[0],
		pAd->ate.Addr1[1], pAd->ate.Addr1[2], pAd->ate.Addr1[3], pAd->ate.Addr1[4], pAd->ate.Addr1[5]));
#endif // CONFIG_STA_SUPPORT //

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_BSSID_Proc Success\n"));

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx Channel

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_CHANNEL_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	UCHAR channel;

	channel = simple_strtol(arg, 0, 10);

	if ((channel < 1) || (channel > 216))// to allow A band channel : ((channel < 1) || (channel > 14))
	{
		ATEDBGPRINT(RT_DEBUG_ERROR, ("Set_ATE_CHANNEL_Proc::Out of range, it should be in range of 1~14.\n"));
		return FALSE;
	}
	pAd->ate.Channel = channel;

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_CHANNEL_Proc (ATE Channel = %d)\n", pAd->ate.Channel));
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_CHANNEL_Proc Success\n"));


	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx Power0

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_TX_POWER0_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	CHAR TxPower;

	TxPower = simple_strtol(arg, 0, 10);

	if (pAd->ate.Channel <= 14)
	{
		if ((TxPower > 31) || (TxPower < 0))
		{
			ATEDBGPRINT(RT_DEBUG_ERROR, ("Set_ATE_TX_POWER0_Proc::Out of range (Value=%d)\n", TxPower));
			return FALSE;
		}
	}
	else// 5.5GHz
	{
		if ((TxPower > 15) || (TxPower < -7))
		{
			ATEDBGPRINT(RT_DEBUG_ERROR, ("Set_ATE_TX_POWER0_Proc::Out of range (Value=%d)\n", TxPower));
			return FALSE;
		}
	}

	pAd->ate.TxPower0 = TxPower;
	ATETxPwrHandler(pAd, 0);
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_TX_POWER0_Proc Success\n"));


	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx Power1

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_TX_POWER1_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	CHAR TxPower;

	TxPower = simple_strtol(arg, 0, 10);

	if (pAd->ate.Channel <= 14)
	{
	if ((TxPower > 31) || (TxPower < 0))
	{
		ATEDBGPRINT(RT_DEBUG_ERROR, ("Set_ATE_TX_POWER1_Proc::Out of range (Value=%d)\n", TxPower));
		return FALSE;
	}
	}
	else
	{
		if ((TxPower > 15) || (TxPower < -7))
		{
			ATEDBGPRINT(RT_DEBUG_ERROR, ("Set_ATE_TX_POWER1_Proc::Out of range (Value=%d)\n", TxPower));
			return FALSE;
		}
	}

	pAd->ate.TxPower1 = TxPower;
	ATETxPwrHandler(pAd, 1);
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_TX_POWER1_Proc Success\n"));


	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx Antenna

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_TX_Antenna_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	CHAR value;

	value = simple_strtol(arg, 0, 10);

	if ((value > 2) || (value < 0))
	{
		ATEDBGPRINT(RT_DEBUG_ERROR, ("Set_ATE_TX_Antenna_Proc::Out of range (Value=%d)\n", value));
		return FALSE;
	}

	pAd->ate.TxAntennaSel = value;

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_TX_Antenna_Proc (Antenna = %d)\n", pAd->ate.TxAntennaSel));
	ATEDBGPRINT(RT_DEBUG_TRACE,("Ralink: Set_ATE_TX_Antenna_Proc Success\n"));


	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Rx Antenna

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_RX_Antenna_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	CHAR value;

	value = simple_strtol(arg, 0, 10);

	if ((value > 3) || (value < 0))
	{
		ATEDBGPRINT(RT_DEBUG_ERROR, ("Set_ATE_RX_Antenna_Proc::Out of range (Value=%d)\n", value));
		return FALSE;
	}

	pAd->ate.RxAntennaSel = value;

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_RX_Antenna_Proc (Antenna = %d)\n", pAd->ate.RxAntennaSel));
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_RX_Antenna_Proc Success\n"));


	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE RF frequence offset

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_TX_FREQOFFSET_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	UCHAR RFFreqOffset;
	ULONG R4;

	RFFreqOffset = simple_strtol(arg, 0, 10);

	if(RFFreqOffset >= 64)
	{
		ATEDBGPRINT(RT_DEBUG_ERROR, ("Set_ATE_TX_FREQOFFSET_Proc::Out of range, it should be in range of 0~63.\n"));
		return FALSE;
	}

	pAd->ate.RFFreqOffset = RFFreqOffset;
	R4 = pAd->ate.RFFreqOffset << 15;		// shift TX power control to correct RF register bit position
	R4 |= (pAd->LatchRfRegs.R4 & ((~0x001f8000)));
	pAd->LatchRfRegs.R4 = R4;

	RtmpRfIoWrite(pAd);

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_TX_FREQOFFSET_Proc (RFFreqOffset = %d)\n", pAd->ate.RFFreqOffset));
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_TX_FREQOFFSET_Proc Success\n"));


	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE RF BW

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_TX_BW_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	int i;
	UCHAR value = 0;
	UCHAR BBPCurrentBW;

	BBPCurrentBW = simple_strtol(arg, 0, 10);

	if(BBPCurrentBW == 0)
		pAd->ate.TxWI.BW = BW_20;
	else
		pAd->ate.TxWI.BW = BW_40;

	if(pAd->ate.TxWI.BW == BW_20)
	{
		if(pAd->ate.Channel <= 14)
		{
 		for (i=0; i<5; i++)
 		{
				if (pAd->Tx20MPwrCfgGBand[i] != 0xffffffff)
				{
					RTMP_IO_WRITE32(pAd, TX_PWR_CFG_0 + i*4, pAd->Tx20MPwrCfgGBand[i]);
					RTMPusecDelay(5000);
				}
			}
		}
		else
		{
			for (i=0; i<5; i++)
			{
				if (pAd->Tx20MPwrCfgABand[i] != 0xffffffff)
 			{
					RTMP_IO_WRITE32(pAd, TX_PWR_CFG_0 + i*4, pAd->Tx20MPwrCfgABand[i]);
 				RTMPusecDelay(5000);
 			}
 		}
		}

		//Set BBP R4 bit[4:3]=0:0
 		ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &value);
 		value &= (~0x18);
 		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, value);

  		//Set BBP R66=0x3C
 		value = 0x3C;
 		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, value);
		//Set BBP R68=0x0B
		//to improve Rx sensitivity.
		value = 0x0B;
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R68, value);
		//Set BBP R69=0x16
		value = 0x16;
 		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, value);
		//Set BBP R70=0x08
		value = 0x08;
 		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, value);
		//Set BBP R73=0x11
		value = 0x11;
 		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, value);

	    // If Channel=14, Bandwidth=20M and Mode=CCK, Set BBP R4 bit5=1
	    // (Japan filter coefficients)
	    // This segment of code will only works when ATETXMODE and ATECHANNEL
	    // were set to MODE_CCK and 14 respectively before ATETXBW is set to 0.
	    //=====================================================================
		if (pAd->ate.Channel == 14)
		{
			int TxMode = pAd->ate.TxWI.PHYMODE;
			if (TxMode == MODE_CCK)
			{
				// when Channel==14 && Mode==CCK && BandWidth==20M, BBP R4 bit5=1
 				ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &value);
				value |= 0x20; //set bit5=1
 				ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, value);
			}
		}

	    //=====================================================================
		// If bandwidth != 40M, RF Reg4 bit 21 = 0.
		pAd->LatchRfRegs.R4 &= ~0x00200000;
		RtmpRfIoWrite(pAd);
	}
	else if(pAd->ate.TxWI.BW == BW_40)
	{
		if(pAd->ate.Channel <= 14)
		{
			for (i=0; i<5; i++)
			{
				if (pAd->Tx40MPwrCfgGBand[i] != 0xffffffff)
				{
					RTMP_IO_WRITE32(pAd, TX_PWR_CFG_0 + i*4, pAd->Tx40MPwrCfgGBand[i]);
					RTMPusecDelay(5000);
				}
			}
		}
		else
		{
			for (i=0; i<5; i++)
			{
				if (pAd->Tx40MPwrCfgABand[i] != 0xffffffff)
				{
					RTMP_IO_WRITE32(pAd, TX_PWR_CFG_0 + i*4, pAd->Tx40MPwrCfgABand[i]);
					RTMPusecDelay(5000);
				}
			}
#ifdef DOT11_N_SUPPORT
			if ((pAd->ate.TxWI.PHYMODE >= MODE_HTMIX) && (pAd->ate.TxWI.MCS == 7))
			{
    			value = 0x28;
    			ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R67, value);
			}
#endif // DOT11_N_SUPPORT //
		}

		//Set BBP R4 bit[4:3]=1:0
		ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &value);
		value &= (~0x18);
		value |= 0x10;
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, value);

		//Set BBP R66=0x3C
		value = 0x3C;
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, value);
		//Set BBP R68=0x0C
		//to improve Rx sensitivity.
		value = 0x0C;
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R68, value);
		//Set BBP R69=0x1A
		value = 0x1A;
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, value);
		//Set BBP R70=0x0A
		value = 0x0A;
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, value);
		//Set BBP R73=0x16
		value = 0x16;
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, value);

		// If bandwidth = 40M, set RF Reg4 bit 21 = 1.
		pAd->LatchRfRegs.R4 |= 0x00200000;
		RtmpRfIoWrite(pAd);
	}

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_TX_BW_Proc (BBPCurrentBW = %d)\n", pAd->ate.TxWI.BW));
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_TX_BW_Proc Success\n"));


	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx frame length

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_TX_LENGTH_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	pAd->ate.TxLength = simple_strtol(arg, 0, 10);

	if((pAd->ate.TxLength < 24) || (pAd->ate.TxLength > (MAX_FRAME_SIZE - 34/* == 2312 */)))
	{
		pAd->ate.TxLength = (MAX_FRAME_SIZE - 34/* == 2312 */);
		ATEDBGPRINT(RT_DEBUG_ERROR, ("Set_ATE_TX_LENGTH_Proc::Out of range, it should be in range of 24~%d.\n", (MAX_FRAME_SIZE - 34/* == 2312 */)));
		return FALSE;
	}

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_TX_LENGTH_Proc (TxLength = %d)\n", pAd->ate.TxLength));
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_TX_LENGTH_Proc Success\n"));


	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx frame count

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_TX_COUNT_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	pAd->ate.TxCount = simple_strtol(arg, 0, 10);

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_TX_COUNT_Proc (TxCount = %d)\n", pAd->ate.TxCount));
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_TX_COUNT_Proc Success\n"));


	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx frame MCS

        Return:
        	TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_TX_MCS_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	UCHAR MCS;
	int result;

	MCS = simple_strtol(arg, 0, 10);
	result = CheckMCSValid(pAd->ate.TxWI.PHYMODE, MCS);

	if (result != -1)
	{
		pAd->ate.TxWI.MCS = (UCHAR)MCS;
	}
	else
	{
		ATEDBGPRINT(RT_DEBUG_ERROR, ("Set_ATE_TX_MCS_Proc::Out of range, refer to rate table.\n"));
		return FALSE;
	}

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_TX_MCS_Proc (MCS = %d)\n", pAd->ate.TxWI.MCS));
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_TX_MCS_Proc Success\n"));


	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx frame Mode
        0: MODE_CCK
        1: MODE_OFDM
        2: MODE_HTMIX
        3: MODE_HTGREENFIELD

        Return:
        	TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_TX_MODE_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	pAd->ate.TxWI.PHYMODE = simple_strtol(arg, 0, 10);

	if(pAd->ate.TxWI.PHYMODE > 3)
	{
		pAd->ate.TxWI.PHYMODE = 0;
		ATEDBGPRINT(RT_DEBUG_ERROR, ("Set_ATE_TX_MODE_Proc::Out of range. it should be in range of 0~3\n"));
		ATEDBGPRINT(RT_DEBUG_ERROR, ("0: CCK, 1: OFDM, 2: HT_MIX, 3: HT_GREEN_FIELD.\n"));
		return FALSE;
	}

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_TX_MODE_Proc (TxMode = %d)\n", pAd->ate.TxWI.PHYMODE));
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_TX_MODE_Proc Success\n"));


	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx frame GI

        Return:
        	TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	Set_ATE_TX_GI_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	pAd->ate.TxWI.ShortGI = simple_strtol(arg, 0, 10);

	if(pAd->ate.TxWI.ShortGI > 1)
	{
		pAd->ate.TxWI.ShortGI = 0;
		ATEDBGPRINT(RT_DEBUG_ERROR, ("Set_ATE_TX_GI_Proc::Out of range\n"));
		return FALSE;
	}

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_TX_GI_Proc (GI = %d)\n", pAd->ate.TxWI.ShortGI));
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_TX_GI_Proc Success\n"));


	return TRUE;
}

/*
    ==========================================================================
    Description:
    ==========================================================================
 */
INT	Set_ATE_RX_FER_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	pAd->ate.bRxFer = simple_strtol(arg, 0, 10);

	if (pAd->ate.bRxFer == 1)
	{
		pAd->ate.RxCntPerSec = 0;
		pAd->ate.RxTotalCnt = 0;
	}

	ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_RX_FER_Proc (bRxFer = %d)\n", pAd->ate.bRxFer));
	ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_RX_FER_Proc Success\n"));


	return TRUE;
}

INT Set_ATE_Read_RF_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	ate_print(KERN_EMERG "R1 = %lx\n", pAd->LatchRfRegs.R1);
	ate_print(KERN_EMERG "R2 = %lx\n", pAd->LatchRfRegs.R2);
	ate_print(KERN_EMERG "R3 = %lx\n", pAd->LatchRfRegs.R3);
	ate_print(KERN_EMERG "R4 = %lx\n", pAd->LatchRfRegs.R4);

	return TRUE;
}

INT Set_ATE_Write_RF1_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	UINT32 value = simple_strtol(arg, 0, 16);

	pAd->LatchRfRegs.R1 = value;
	RtmpRfIoWrite(pAd);

	return TRUE;
}

INT Set_ATE_Write_RF2_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	UINT32 value = simple_strtol(arg, 0, 16);

	pAd->LatchRfRegs.R2 = value;
	RtmpRfIoWrite(pAd);

	return TRUE;
}

INT Set_ATE_Write_RF3_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	UINT32 value = simple_strtol(arg, 0, 16);

	pAd->LatchRfRegs.R3 = value;
	RtmpRfIoWrite(pAd);

	return TRUE;
}

INT Set_ATE_Write_RF4_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	UINT32 value = simple_strtol(arg, 0, 16);

	pAd->LatchRfRegs.R4 = value;
	RtmpRfIoWrite(pAd);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Load and Write EEPROM from a binary file prepared in advance.

        Return:
        	TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
#ifndef UCOS
INT Set_ATE_Load_E2P_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	BOOLEAN		    ret = FALSE;
	PUCHAR			src = EEPROM_BIN_FILE_NAME;
	struct file		*srcf;
	INT32 			retval, orgfsuid, orgfsgid;
   	mm_segment_t	orgfs;
	USHORT 			WriteEEPROM[(EEPROM_SIZE/2)];
	UINT32			FileLength = 0;
	UINT32 			value = simple_strtol(arg, 0, 10);

	ATEDBGPRINT(RT_DEBUG_ERROR, ("===> %s (value=%d)\n\n", __func__, value));

	if (value > 0)
	{
		/* zero the e2p buffer */
		NdisZeroMemory((PUCHAR)WriteEEPROM, EEPROM_SIZE);

		/* save uid and gid used for filesystem access.
	    ** set user and group to 0 (root)
	    */
		orgfsuid = current->fsuid;
		orgfsgid = current->fsgid;
		/* as root */
		current->fsuid = current->fsgid = 0;
    	orgfs = get_fs();
    	set_fs(KERNEL_DS);

		do
		{
			/* open the bin file */
			srcf = filp_open(src, O_RDONLY, 0);

			if (IS_ERR(srcf))
			{
				ate_print("%s - Error %ld opening %s\n", __func__, -PTR_ERR(srcf), src);
				break;
			}

			/* the object must have a read method */
			if ((srcf->f_op == NULL) || (srcf->f_op->read == NULL))
			{
				ate_print("%s - %s does not have a read method\n", __func__, src);
				break;
			}

			/* read the firmware from the file *.bin */
			FileLength = srcf->f_op->read(srcf,
										  (PUCHAR)WriteEEPROM,
										  EEPROM_SIZE,
										  &srcf->f_pos);

			if (FileLength != EEPROM_SIZE)
			{
				ate_print("%s: error file length (=%d) in e2p.bin\n",
					   __func__, FileLength);
				break;
			}
			else
			{
				/* write the content of .bin file to EEPROM */
				rt_ee_write_all(pAd, WriteEEPROM);
				ret = TRUE;
			}
			break;
		} while(TRUE);

		/* close firmware file */
		if (IS_ERR(srcf))
		{
				;
		}
		else
		{
			retval = filp_close(srcf, NULL);
			if (retval)
			{
				ATEDBGPRINT(RT_DEBUG_ERROR, ("--> Error %d closing %s\n", -retval, src));

			}
		}

		/* restore */
		set_fs(orgfs);
		current->fsuid = orgfsuid;
		current->fsgid = orgfsgid;
	}
    ATEDBGPRINT(RT_DEBUG_ERROR, ("<=== %s (ret=%d)\n", __func__, ret));

    return ret;

}
#else
INT Set_ATE_Load_E2P_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	USHORT 			WriteEEPROM[(EEPROM_SIZE/2)];
	struct iwreq	*wrq = (struct iwreq *)arg;

	ATEDBGPRINT(RT_DEBUG_TRACE, ("===> %s (wrq->u.data.length = %d)\n\n", __func__, wrq->u.data.length));

	if (wrq->u.data.length != EEPROM_SIZE)
	{
		ate_print("%s: error length (=%d) from host\n",
			   __func__, wrq->u.data.length);
		return FALSE;
	}
	else/* (wrq->u.data.length == EEPROM_SIZE) */
	{
		/* zero the e2p buffer */
		NdisZeroMemory((PUCHAR)WriteEEPROM, EEPROM_SIZE);

		/* fill the local buffer */
		NdisMoveMemory((PUCHAR)WriteEEPROM, wrq->u.data.pointer, wrq->u.data.length);

		do
		{
				/* write the content of .bin file to EEPROM */
				rt_ee_write_all(pAd, WriteEEPROM);

		} while(FALSE);
		}

    ATEDBGPRINT(RT_DEBUG_TRACE, ("<=== %s\n", __func__));

    return TRUE;

}
#endif // !UCOS //

INT Set_ATE_Read_E2P_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	USHORT buffer[EEPROM_SIZE/2];
	USHORT *p;
	int i;

	rt_ee_read_all(pAd, (USHORT *)buffer);
	p = buffer;
	for (i = 0; i < (EEPROM_SIZE/2); i++)
	{
		ate_print("%4.4x ", *p);
		if (((i+1) % 16) == 0)
			ate_print("\n");
		p++;
	}
	return TRUE;
}

INT	Set_ATE_Show_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	ate_print("Mode=%d\n", pAd->ate.Mode);
	ate_print("TxPower0=%d\n", pAd->ate.TxPower0);
	ate_print("TxPower1=%d\n", pAd->ate.TxPower1);
	ate_print("TxAntennaSel=%d\n", pAd->ate.TxAntennaSel);
	ate_print("RxAntennaSel=%d\n", pAd->ate.RxAntennaSel);
	ate_print("BBPCurrentBW=%d\n", pAd->ate.TxWI.BW);
	ate_print("GI=%d\n", pAd->ate.TxWI.ShortGI);
	ate_print("MCS=%d\n", pAd->ate.TxWI.MCS);
	ate_print("TxMode=%d\n", pAd->ate.TxWI.PHYMODE);
	ate_print("Addr1=%02x:%02x:%02x:%02x:%02x:%02x\n",
		pAd->ate.Addr1[0], pAd->ate.Addr1[1], pAd->ate.Addr1[2], pAd->ate.Addr1[3], pAd->ate.Addr1[4], pAd->ate.Addr1[5]);
	ate_print("Addr2=%02x:%02x:%02x:%02x:%02x:%02x\n",
		pAd->ate.Addr2[0], pAd->ate.Addr2[1], pAd->ate.Addr2[2], pAd->ate.Addr2[3], pAd->ate.Addr2[4], pAd->ate.Addr2[5]);
	ate_print("Addr3=%02x:%02x:%02x:%02x:%02x:%02x\n",
		pAd->ate.Addr3[0], pAd->ate.Addr3[1], pAd->ate.Addr3[2], pAd->ate.Addr3[3], pAd->ate.Addr3[4], pAd->ate.Addr3[5]);
	ate_print("Channel=%d\n", pAd->ate.Channel);
	ate_print("TxLength=%d\n", pAd->ate.TxLength);
	ate_print("TxCount=%u\n", pAd->ate.TxCount);
	ate_print("RFFreqOffset=%d\n", pAd->ate.RFFreqOffset);
	ate_print(KERN_EMERG "Set_ATE_Show_Proc Success\n");
	return TRUE;
}

INT	Set_ATE_Help_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	ate_print("ATE=ATESTART, ATESTOP, TXCONT, TXCARR, TXFRAME, RXFRAME\n");
	ate_print("ATEDA\n");
	ate_print("ATESA\n");
	ate_print("ATEBSSID\n");
	ate_print("ATECHANNEL, range:0~14(unless A band !)\n");
	ate_print("ATETXPOW0, set power level of antenna 1.\n");
	ate_print("ATETXPOW1, set power level of antenna 2.\n");
	ate_print("ATETXANT, set TX antenna. 0:all, 1:antenna one, 2:antenna two.\n");
	ate_print("ATERXANT, set RX antenna.0:all, 1:antenna one, 2:antenna two, 3:antenna three.\n");
	ate_print("ATETXFREQOFFSET, set frequency offset, range 0~63\n");
	ate_print("ATETXBW, set BandWidth, 0:20MHz, 1:40MHz.\n");
	ate_print("ATETXLEN, set Frame length, range 24~%d\n", (MAX_FRAME_SIZE - 34/* == 2312 */));
	ate_print("ATETXCNT, set how many frame going to transmit.\n");
	ate_print("ATETXMCS, set MCS, reference to rate table.\n");
	ate_print("ATETXMODE, set Mode 0:CCK, 1:OFDM, 2:HT-Mix, 3:GreenField, reference to rate table.\n");
	ate_print("ATETXGI, set GI interval, 0:Long, 1:Short\n");
	ate_print("ATERXFER, 0:disable Rx Frame error rate. 1:enable Rx Frame error rate.\n");
	ate_print("ATERRF, show all RF registers.\n");
	ate_print("ATEWRF1, set RF1 register.\n");
	ate_print("ATEWRF2, set RF2 register.\n");
	ate_print("ATEWRF3, set RF3 register.\n");
	ate_print("ATEWRF4, set RF4 register.\n");
	ate_print("ATELDE2P, load EEPROM from .bin file.\n");
	ate_print("ATERE2P, display all EEPROM content.\n");
	ate_print("ATESHOW, display all parameters of ATE.\n");
	ate_print("ATEHELP, online help.\n");

	return TRUE;
}

/*
    ==========================================================================
    Description:

	AsicSwitchChannel() dedicated for ATE.

    ==========================================================================
*/
VOID ATEAsicSwitchChannel(
    IN PRTMP_ADAPTER pAd)
{
	UINT32 R2 = 0, R3 = DEFAULT_RF_TX_POWER, R4 = 0, Value = 0;
	CHAR TxPwer = 0, TxPwer2 = 0;
	UCHAR index, BbpValue = 0, R66 = 0x30;
	RTMP_RF_REGS *RFRegTable;
	UCHAR Channel;

#ifdef RALINK_28xx_QA
	if ((pAd->ate.bQATxStart == TRUE) || (pAd->ate.bQARxStart == TRUE))
	{
		if (pAd->ate.Channel != pAd->LatchRfRegs.Channel)
		{
			pAd->ate.Channel = pAd->LatchRfRegs.Channel;
		}
		return;
	}
	else
#endif // RALINK_28xx_QA //
	Channel = pAd->ate.Channel;

	// Select antenna
	AsicAntennaSelect(pAd, Channel);

	// fill Tx power value
	TxPwer = pAd->ate.TxPower0;
	TxPwer2 = pAd->ate.TxPower1;

	RFRegTable = RF2850RegTable;

	switch (pAd->RfIcType)
	{
		/* But only 2850 and 2750 support 5.5GHz band... */
		case RFIC_2820:
		case RFIC_2850:
		case RFIC_2720:
		case RFIC_2750:

			for (index = 0; index < NUM_OF_2850_CHNL; index++)
			{
				if (Channel == RFRegTable[index].Channel)
				{
					R2 = RFRegTable[index].R2;
					if (pAd->Antenna.field.TxPath == 1)
					{
						R2 |= 0x4000;	// If TXpath is 1, bit 14 = 1;
					}

					if (pAd->Antenna.field.RxPath == 2)
					{
						switch (pAd->ate.RxAntennaSel)
						{
							case 1:
								R2 |= 0x20040;
								ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BbpValue);
								BbpValue &= 0xE4;
								BbpValue |= 0x00;
								ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BbpValue);
								break;
							case 2:
								R2 |= 0x10040;
								ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BbpValue);
								BbpValue &= 0xE4;
								BbpValue |= 0x01;
								ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BbpValue);
								break;
							default:
								R2 |= 0x40;
								ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BbpValue);
								BbpValue &= 0xE4;
								/* Only enable two Antenna to receive. */
								BbpValue |= 0x08;
								ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BbpValue);
								break;
						}
					}
					else if (pAd->Antenna.field.RxPath == 1)
					{
						R2 |= 0x20040;	// write 1 to off RxPath
					}

					if (pAd->Antenna.field.TxPath == 2)
					{
						if (pAd->ate.TxAntennaSel == 1)
						{
							R2 |= 0x4000;	// If TX Antenna select is 1 , bit 14 = 1; Disable Ant 2
							ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R1, &BbpValue);
							BbpValue &= 0xE7;		//11100111B
							ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R1, BbpValue);
						}
						else if (pAd->ate.TxAntennaSel == 2)
						{
							R2 |= 0x8000;	// If TX Antenna select is 2 , bit 15 = 1; Disable Ant 1
							ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R1, &BbpValue);
							BbpValue &= 0xE7;
							BbpValue |= 0x08;
							ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R1, BbpValue);
						}
						else
						{
							ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R1, &BbpValue);
							BbpValue &= 0xE7;
							BbpValue |= 0x10;
							ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R1, BbpValue);
						}
					}
					if (pAd->Antenna.field.RxPath == 3)
					{
						switch (pAd->ate.RxAntennaSel)
						{
							case 1:
								R2 |= 0x20040;
								ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BbpValue);
								BbpValue &= 0xE4;
								BbpValue |= 0x00;
								ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BbpValue);
								break;
							case 2:
								R2 |= 0x10040;
								ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BbpValue);
								BbpValue &= 0xE4;
								BbpValue |= 0x01;
								ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BbpValue);
								break;
							case 3:
								R2 |= 0x30000;
								ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BbpValue);
								BbpValue &= 0xE4;
								BbpValue |= 0x02;
								ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BbpValue);
								break;
							default:
								ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BbpValue);
								BbpValue &= 0xE4;
								BbpValue |= 0x10;
								ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BbpValue);
								break;
						}
					}

					if (Channel > 14)
					{
						// initialize R3, R4
						R3 = (RFRegTable[index].R3 & 0xffffc1ff);
						R4 = (RFRegTable[index].R4 & (~0x001f87c0)) | (pAd->ate.RFFreqOffset << 15);

                        // According the Rory's suggestion to solve the middle range issue.
						// 5.5G band power range: 0xF9~0X0F, TX0 Reg3 bit9/TX1 Reg4 bit6="0" means the TX power reduce 7dB
						// R3
						if ((TxPwer >= -7) && (TxPwer < 0))
						{
							TxPwer = (7+TxPwer);
							TxPwer = (TxPwer > 0xF) ? (0xF) : (TxPwer);
							R3 |= (TxPwer << 10);
							ATEDBGPRINT(RT_DEBUG_TRACE, ("ATEAsicSwitchChannel: TxPwer=%d \n", TxPwer));
						}
						else
						{
							TxPwer = (TxPwer > 0xF) ? (0xF) : (TxPwer);
							R3 |= (TxPwer << 10) | (1 << 9);
						}

						// R4
						if ((TxPwer2 >= -7) && (TxPwer2 < 0))
						{
							TxPwer2 = (7+TxPwer2);
							TxPwer2 = (TxPwer2 > 0xF) ? (0xF) : (TxPwer2);
							R4 |= (TxPwer2 << 7);
							ATEDBGPRINT(RT_DEBUG_TRACE, ("ATEAsicSwitchChannel: TxPwer2=%d \n", TxPwer2));
						}
						else
						{
							TxPwer2 = (TxPwer2 > 0xF) ? (0xF) : (TxPwer2);
							R4 |= (TxPwer2 << 7) | (1 << 6);
						}
					}
					else
					{
						R3 = (RFRegTable[index].R3 & 0xffffc1ff) | (TxPwer << 9); // set TX power0
						R4 = (RFRegTable[index].R4 & (~0x001f87c0)) | (pAd->ate.RFFreqOffset << 15) | (TxPwer2 <<6);// Set freq offset & TxPwr1
					}

					// Based on BBP current mode before changing RF channel.
					if (pAd->ate.TxWI.BW == BW_40)
					{
						R4 |=0x200000;
					}

					// Update variables
					pAd->LatchRfRegs.Channel = Channel;
					pAd->LatchRfRegs.R1 = RFRegTable[index].R1;
					pAd->LatchRfRegs.R2 = R2;
					pAd->LatchRfRegs.R3 = R3;
					pAd->LatchRfRegs.R4 = R4;

					RtmpRfIoWrite(pAd);

					break;
				}
			}
			break;

		default:
			break;
	}

	// Change BBP setting during switch from a->g, g->a
	if (Channel <= 14)
	{
	    ULONG	TxPinCfg = 0x00050F0A;// 2007.10.09 by Brian : 0x0005050A ==> 0x00050F0A

		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, (0x37 - GET_LNA_GAIN(pAd)));
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, (0x37 - GET_LNA_GAIN(pAd)));
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, (0x37 - GET_LNA_GAIN(pAd)));

		/* For 1T/2R chip only... */
	    if (pAd->NicConfig2.field.ExternalLNAForG)
	    {
	        ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x62);
	    }
	    else
	    {
	        ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x84);
	    }

        // According the Rory's suggestion to solve the middle range issue.
		ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R86, &BbpValue);
		ASSERT((BbpValue == 0x00));
		if ((BbpValue != 0x00))
		{
			ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0x00);
		}

		// 5.5GHz band selection PIN, bit1 and bit2 are complement
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value &= (~0x6);
		Value |= (0x04);
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

        // Turn off unused PA or LNA when only 1T or 1R.
		if (pAd->Antenna.field.TxPath == 1)
		{
			TxPinCfg &= 0xFFFFFFF3;
		}
		if (pAd->Antenna.field.RxPath == 1)
		{
			TxPinCfg &= 0xFFFFF3FF;
		}

		RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);
	}
	else
	{
	    ULONG	TxPinCfg = 0x00050F05;//2007.10.09 by Brian : 0x00050505 ==> 0x00050F05

		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, (0x37 - GET_LNA_GAIN(pAd)));
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, (0x37 - GET_LNA_GAIN(pAd)));
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, (0x37 - GET_LNA_GAIN(pAd)));
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0xF2);

        // According the Rory's suggestion to solve the middle range issue.
		ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R86, &BbpValue);
		ASSERT((BbpValue == 0x00));
		if ((BbpValue != 0x00))
		{
			ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0x00);
		}
		ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R91, &BbpValue);
		ASSERT((BbpValue == 0x04));

		ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R92, &BbpValue);
		ASSERT((BbpValue == 0x00));

		// 5.5GHz band selection PIN, bit1 and bit2 are complement
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value &= (~0x6);
		Value |= (0x02);
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

        // Turn off unused PA or LNA when only 1T or 1R.
		if (pAd->Antenna.field.TxPath == 1)
		{
			TxPinCfg &= 0xFFFFFFF3;
	    }
		if (pAd->Antenna.field.RxPath == 1)
		{
			TxPinCfg &= 0xFFFFF3FF;
		}

		RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);
	}

    // R66 should be set according to Channel and use 20MHz when scanning
	if (Channel <= 14)
	{
		// BG band
		R66 = 0x2E + GET_LNA_GAIN(pAd);
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
	}
	else
	{
		// 5.5 GHz band
		if (pAd->ate.TxWI.BW == BW_20)
		{
			R66 = (UCHAR)(0x32 + (GET_LNA_GAIN(pAd)*5)/3);
    		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
		}
		else
		{
			R66 = (UCHAR)(0x3A + (GET_LNA_GAIN(pAd)*5)/3);
			ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
		}
	}

	//
	// On 11A, We should delay and wait RF/BBP to be stable
	// and the appropriate time should be 1000 micro seconds
	// 2005/06/05 - On 11G, We also need this delay time. Otherwise it's difficult to pass the WHQL.
	//
	RTMPusecDelay(1000);

	if (Channel > 14)
	{
		// When 5.5GHz band the LSB of TxPwr will be used to reduced 7dB or not.
		ATEDBGPRINT(RT_DEBUG_TRACE, ("SwitchChannel#%d(RF=%d, %dT) to , R1=0x%08lx, R2=0x%08lx, R3=0x%08lx, R4=0x%08lx\n",
								  Channel,
								  pAd->RfIcType,
								  pAd->Antenna.field.TxPath,
								  pAd->LatchRfRegs.R1,
								  pAd->LatchRfRegs.R2,
								  pAd->LatchRfRegs.R3,
								  pAd->LatchRfRegs.R4));
	}
	else
	{
		ATEDBGPRINT(RT_DEBUG_TRACE, ("SwitchChannel#%d(RF=%d, Pwr0=%u, Pwr1=%u, %dT) to , R1=0x%08lx, R2=0x%08lx, R3=0x%08lx, R4=0x%08lx\n",
								  Channel,
								  pAd->RfIcType,
								  (R3 & 0x00003e00) >> 9,
								  (R4 & 0x000007c0) >> 6,
								  pAd->Antenna.field.TxPath,
								  pAd->LatchRfRegs.R1,
								  pAd->LatchRfRegs.R2,
								  pAd->LatchRfRegs.R3,
								  pAd->LatchRfRegs.R4));
    }
}

//
// In fact, no one will call this routine so far !
//
/*
	==========================================================================
	Description:
		Gives CCK TX rate 2 more dB TX power.
		This routine works only in ATE mode.

		calculate desired Tx power in RF R3.Tx0~5,	should consider -
		0. if current radio is a noisy environment (pAd->DrsCounters.fNoisyEnvironment)
		1. TxPowerPercentage
		2. auto calibration based on TSSI feedback
		3. extra 2 db for CCK
		4. -10 db upon very-short distance (AvgRSSI >= -40db) to AP

	NOTE: Since this routine requires the value of (pAd->DrsCounters.fNoisyEnvironment),
		it should be called AFTER MlmeDynamicTxRateSwitching()
	==========================================================================
 */
VOID ATEAsicAdjustTxPower(
	IN PRTMP_ADAPTER pAd)
{
	INT			i, j;
	CHAR		DeltaPwr = 0;
	BOOLEAN		bAutoTxAgc = FALSE;
	UCHAR		TssiRef, *pTssiMinusBoundary, *pTssiPlusBoundary, TxAgcStep;
	UCHAR		BbpR49 = 0, idx;
	PCHAR		pTxAgcCompensate;
	ULONG		TxPwr[5];
	CHAR		Value;

	/* no one calls this procedure so far */
	if (pAd->ate.TxWI.BW == BW_40)
	{
		if (pAd->ate.Channel > 14)
		{
			TxPwr[0] = pAd->Tx40MPwrCfgABand[0];
			TxPwr[1] = pAd->Tx40MPwrCfgABand[1];
			TxPwr[2] = pAd->Tx40MPwrCfgABand[2];
			TxPwr[3] = pAd->Tx40MPwrCfgABand[3];
			TxPwr[4] = pAd->Tx40MPwrCfgABand[4];
		}
		else
		{
			TxPwr[0] = pAd->Tx40MPwrCfgGBand[0];
			TxPwr[1] = pAd->Tx40MPwrCfgGBand[1];
			TxPwr[2] = pAd->Tx40MPwrCfgGBand[2];
			TxPwr[3] = pAd->Tx40MPwrCfgGBand[3];
			TxPwr[4] = pAd->Tx40MPwrCfgGBand[4];
		}
	}
	else
	{
		if (pAd->ate.Channel > 14)
		{
			TxPwr[0] = pAd->Tx20MPwrCfgABand[0];
			TxPwr[1] = pAd->Tx20MPwrCfgABand[1];
			TxPwr[2] = pAd->Tx20MPwrCfgABand[2];
			TxPwr[3] = pAd->Tx20MPwrCfgABand[3];
			TxPwr[4] = pAd->Tx20MPwrCfgABand[4];
		}
		else
		{
			TxPwr[0] = pAd->Tx20MPwrCfgGBand[0];
			TxPwr[1] = pAd->Tx20MPwrCfgGBand[1];
			TxPwr[2] = pAd->Tx20MPwrCfgGBand[2];
			TxPwr[3] = pAd->Tx20MPwrCfgGBand[3];
			TxPwr[4] = pAd->Tx20MPwrCfgGBand[4];
		}
	}

	// TX power compensation for temperature variation based on TSSI.
	// Do it per 4 seconds.
	if (pAd->Mlme.OneSecPeriodicRound % 4 == 0)
	{
		if (pAd->ate.Channel <= 14)
		{
			/* bg channel */
			bAutoTxAgc         = pAd->bAutoTxAgcG;
			TssiRef            = pAd->TssiRefG;
			pTssiMinusBoundary = &pAd->TssiMinusBoundaryG[0];
			pTssiPlusBoundary  = &pAd->TssiPlusBoundaryG[0];
			TxAgcStep          = pAd->TxAgcStepG;
			pTxAgcCompensate   = &pAd->TxAgcCompensateG;
		}
		else
		{
			/* a channel */
			bAutoTxAgc         = pAd->bAutoTxAgcA;
			TssiRef            = pAd->TssiRefA;
			pTssiMinusBoundary = &pAd->TssiMinusBoundaryA[0];
			pTssiPlusBoundary  = &pAd->TssiPlusBoundaryA[0];
			TxAgcStep          = pAd->TxAgcStepA;
			pTxAgcCompensate   = &pAd->TxAgcCompensateA;
		}

		if (bAutoTxAgc)
		{
			/* BbpR49 is unsigned char */
			ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpR49);

			/* (p) TssiPlusBoundaryG[0] = 0 = (m) TssiMinusBoundaryG[0] */
			/* compensate: +4     +3   +2   +1    0   -1   -2   -3   -4 * steps */
			/* step value is defined in pAd->TxAgcStepG for tx power value */

			/* [4]+1+[4]   p4     p3   p2   p1   o1   m1   m2   m3   m4 */
			/* ex:         0x00 0x15 0x25 0x45 0x88 0xA0 0xB5 0xD0 0xF0
			   above value are examined in mass factory production */
			/*             [4]    [3]  [2]  [1]  [0]  [1]  [2]  [3]  [4] */

			/* plus is 0x10 ~ 0x40, minus is 0x60 ~ 0x90 */
			/* if value is between p1 ~ o1 or o1 ~ s1, no need to adjust tx power */
			/* if value is 0x65, tx power will be -= TxAgcStep*(2-1) */

			if (BbpR49 > pTssiMinusBoundary[1])
			{
				// Reading is larger than the reference value.
				// Check for how large we need to decrease the Tx power.
				for (idx = 1; idx < 5; idx++)
				{
					if (BbpR49 <= pTssiMinusBoundary[idx])  // Found the range
						break;
				}
				// The index is the step we should decrease, idx = 0 means there is nothing to compensate
//				if (R3 > (ULONG) (TxAgcStep * (idx-1)))
					*pTxAgcCompensate = -(TxAgcStep * (idx-1));
//				else
//					*pTxAgcCompensate = -((UCHAR)R3);

				DeltaPwr += (*pTxAgcCompensate);
				ATEDBGPRINT(RT_DEBUG_TRACE, ("-- Tx Power, BBP R1=%x, TssiRef=%x, TxAgcStep=%x, step = -%d\n",
					BbpR49, TssiRef, TxAgcStep, idx-1));
			}
			else if (BbpR49 < pTssiPlusBoundary[1])
			{
				// Reading is smaller than the reference value
				// check for how large we need to increase the Tx power
				for (idx = 1; idx < 5; idx++)
				{
					if (BbpR49 >= pTssiPlusBoundary[idx])   // Found the range
						break;
				}
				// The index is the step we should increase, idx = 0 means there is nothing to compensate
				*pTxAgcCompensate = TxAgcStep * (idx-1);
				DeltaPwr += (*pTxAgcCompensate);
				ATEDBGPRINT(RT_DEBUG_TRACE, ("++ Tx Power, BBP R1=%x, TssiRef=%x, TxAgcStep=%x, step = +%d\n",
					BbpR49, TssiRef, TxAgcStep, idx-1));
			}
			else
			{
				*pTxAgcCompensate = 0;
				ATEDBGPRINT(RT_DEBUG_TRACE, ("   Tx Power, BBP R1=%x, TssiRef=%x, TxAgcStep=%x, step = +%d\n",
					BbpR49, TssiRef, TxAgcStep, 0));
			}
		}
	}
	else
	{
		if (pAd->ate.Channel <= 14)
		{
			bAutoTxAgc         = pAd->bAutoTxAgcG;
			pTxAgcCompensate   = &pAd->TxAgcCompensateG;
		}
		else
		{
			bAutoTxAgc         = pAd->bAutoTxAgcA;
			pTxAgcCompensate   = &pAd->TxAgcCompensateA;
		}

		if (bAutoTxAgc)
			DeltaPwr += (*pTxAgcCompensate);
	}

	/* calculate delta power based on the percentage specified from UI */
	// E2PROM setting is calibrated for maximum TX power (i.e. 100%)
	// We lower TX power here according to the percentage specified from UI
	if (pAd->CommonCfg.TxPowerPercentage == 0xffffffff)       // AUTO TX POWER control
		;
	else if (pAd->CommonCfg.TxPowerPercentage > 90)  // 91 ~ 100% & AUTO, treat as 100% in terms of mW
		;
	else if (pAd->CommonCfg.TxPowerPercentage > 60)  // 61 ~ 90%, treat as 75% in terms of mW
	{
		DeltaPwr -= 1;
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 30)  // 31 ~ 60%, treat as 50% in terms of mW
	{
		DeltaPwr -= 3;
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 15)  // 16 ~ 30%, treat as 25% in terms of mW
	{
		DeltaPwr -= 6;
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 9)   // 10 ~ 15%, treat as 12.5% in terms of mW
	{
		DeltaPwr -= 9;
	}
	else                                           // 0 ~ 9 %, treat as MIN(~3%) in terms of mW
	{
		DeltaPwr -= 12;
	}

	/* reset different new tx power for different TX rate */
	for(i=0; i<5; i++)
	{
		if (TxPwr[i] != 0xffffffff)
		{
			for (j=0; j<8; j++)
			{
				Value = (CHAR)((TxPwr[i] >> j*4) & 0x0F); /* 0 ~ 15 */

				if ((Value + DeltaPwr) < 0)
				{
					Value = 0; /* min */
				}
				else if ((Value + DeltaPwr) > 0xF)
				{
					Value = 0xF; /* max */
				}
				else
				{
					Value += DeltaPwr; /* temperature compensation */
				}

				/* fill new value to CSR offset */
				TxPwr[i] = (TxPwr[i] & ~(0x0000000F << j*4)) | (Value << j*4);
			}

			/* write tx power value to CSR */
			/* TX_PWR_CFG_0 (8 tx rate) for	TX power for OFDM 12M/18M
											TX power for OFDM 6M/9M
											TX power for CCK5.5M/11M
											TX power for CCK1M/2M */
			/* TX_PWR_CFG_1 ~ TX_PWR_CFG_4 */
			RTMP_IO_WRITE32(pAd, TX_PWR_CFG_0 + i*4, TxPwr[i]);


		}
	}

}

/*
	========================================================================
	Routine Description:
		Write TxWI for ATE mode.

	Return Value:
		None
	========================================================================
*/
static VOID ATEWriteTxWI(
	IN	PRTMP_ADAPTER	pAd,
	IN	PTXWI_STRUC 	pOutTxWI,
	IN	BOOLEAN			FRAG,
	IN	BOOLEAN			CFACK,
	IN	BOOLEAN			InsTimestamp,
	IN	BOOLEAN 		AMPDU,
	IN	BOOLEAN 		Ack,
	IN	BOOLEAN 		NSeq,		// HW new a sequence.
	IN	UCHAR			BASize,
	IN	UCHAR			WCID,
	IN	ULONG			Length,
	IN	UCHAR 			PID,
	IN	UCHAR			TID,
	IN	UCHAR			TxRate,
	IN	UCHAR			Txopmode,
	IN	BOOLEAN			CfAck,
	IN	HTTRANSMIT_SETTING	*pTransmit)
{
	TXWI_STRUC 		TxWI;
	PTXWI_STRUC 	pTxWI;

	//
	// Always use Long preamble before verifiation short preamble functionality works well.
	// Todo: remove the following line if short preamble functionality works
	//
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED);
	NdisZeroMemory(&TxWI, TXWI_SIZE);
	pTxWI = &TxWI;

	pTxWI->FRAG= FRAG;

	pTxWI->CFACK = CFACK;
	pTxWI->TS= InsTimestamp;
	pTxWI->AMPDU = AMPDU;
	pTxWI->ACK = Ack;
	pTxWI->txop= Txopmode;

	pTxWI->NSEQ = NSeq;
	// John tune the performace with Intel Client in 20 MHz performance
	if( BASize >7 )
		BASize =7;

	pTxWI->BAWinSize = BASize;
	pTxWI->WirelessCliID = WCID;
	pTxWI->MPDUtotalByteCount = Length;
	pTxWI->PacketId = PID;

	// If CCK or OFDM, BW must be 20
	pTxWI->BW = (pTransmit->field.MODE <= MODE_OFDM) ? (BW_20) : (pTransmit->field.BW);
	pTxWI->ShortGI = pTransmit->field.ShortGI;
	pTxWI->STBC = pTransmit->field.STBC;

	pTxWI->MCS = pTransmit->field.MCS;
	pTxWI->PHYMODE = pTransmit->field.MODE;
	pTxWI->CFACK = CfAck;
	pTxWI->MIMOps = 0;
	pTxWI->MpduDensity = 0;

	pTxWI->PacketId = pTxWI->MCS;
	NdisMoveMemory(pOutTxWI, &TxWI, sizeof(TXWI_STRUC));

        return;
}

/*
	========================================================================

	Routine Description:
		Disable protection for ATE.
	========================================================================
*/
VOID ATEDisableAsicProtect(
	IN		PRTMP_ADAPTER	pAd)
{
	PROT_CFG_STRUC	ProtCfg, ProtCfg4;
	UINT32 Protect[6];
	USHORT			offset;
	UCHAR			i;
	UINT32 MacReg = 0;

	// Config ASIC RTS threshold register
	RTMP_IO_READ32(pAd, TX_RTS_CFG, &MacReg);
	MacReg &= 0xFF0000FF;
	MacReg |= (pAd->CommonCfg.RtsThreshold << 8);
	RTMP_IO_WRITE32(pAd, TX_RTS_CFG, MacReg);

	// Initial common protection settings
	RTMPZeroMemory(Protect, sizeof(Protect));
	ProtCfg4.word = 0;
	ProtCfg.word = 0;
	ProtCfg.field.TxopAllowGF40 = 1;
	ProtCfg.field.TxopAllowGF20 = 1;
	ProtCfg.field.TxopAllowMM40 = 1;
	ProtCfg.field.TxopAllowMM20 = 1;
	ProtCfg.field.TxopAllowOfdm = 1;
	ProtCfg.field.TxopAllowCck = 1;
	ProtCfg.field.RTSThEn = 1;
	ProtCfg.field.ProtectNav = ASIC_SHORTNAV;

	// Handle legacy(B/G) protection
	ProtCfg.field.ProtectRate = pAd->CommonCfg.RtsRate;
	ProtCfg.field.ProtectCtrl = 0;
	Protect[0] = ProtCfg.word;
	Protect[1] = ProtCfg.word;

	// NO PROTECT
	// 1.All STAs in the BSS are 20/40 MHz HT
	// 2. in ai 20/40MHz BSS
	// 3. all STAs are 20MHz in a 20MHz BSS
	// Pure HT. no protection.

	// MM20_PROT_CFG
	//	Reserved (31:27)
	// 	PROT_TXOP(25:20) -- 010111
	//	PROT_NAV(19:18)  -- 01 (Short NAV protection)
	//  PROT_CTRL(17:16) -- 00 (None)
	// 	PROT_RATE(15:0)  -- 0x4004 (OFDM 24M)
	Protect[2] = 0x01744004;

	// MM40_PROT_CFG
	//	Reserved (31:27)
	// 	PROT_TXOP(25:20) -- 111111
	//	PROT_NAV(19:18)  -- 01 (Short NAV protection)
	//  PROT_CTRL(17:16) -- 00 (None)
	// 	PROT_RATE(15:0)  -- 0x4084 (duplicate OFDM 24M)
	Protect[3] = 0x03f44084;

	// CF20_PROT_CFG
	//	Reserved (31:27)
	// 	PROT_TXOP(25:20) -- 010111
	//	PROT_NAV(19:18)  -- 01 (Short NAV protection)
	//  PROT_CTRL(17:16) -- 00 (None)
	// 	PROT_RATE(15:0)  -- 0x4004 (OFDM 24M)
	Protect[4] = 0x01744004;

	// CF40_PROT_CFG
	//	Reserved (31:27)
	// 	PROT_TXOP(25:20) -- 111111
	//	PROT_NAV(19:18)  -- 01 (Short NAV protection)
	//  PROT_CTRL(17:16) -- 00 (None)
	// 	PROT_RATE(15:0)  -- 0x4084 (duplicate OFDM 24M)
	Protect[5] = 0x03f44084;

	pAd->CommonCfg.IOTestParm.bRTSLongProtOn = FALSE;

	offset = CCK_PROT_CFG;
	for (i = 0;i < 6;i++)
		RTMP_IO_WRITE32(pAd, offset + i*4, Protect[i]);

}


/* There are two ways to convert Rssi */
#if 1
//
// The way used with GET_LNA_GAIN().
//
CHAR ATEConvertToRssi(
	IN PRTMP_ADAPTER pAd,
	IN	CHAR	Rssi,
	IN  UCHAR   RssiNumber)
{
	UCHAR	RssiOffset, LNAGain;

	// Rssi equals to zero should be an invalid value
	if (Rssi == 0)
		return -99;

	LNAGain = GET_LNA_GAIN(pAd);
	if (pAd->LatchRfRegs.Channel > 14)
	{
		if (RssiNumber == 0)
			RssiOffset = pAd->ARssiOffset0;
		else if (RssiNumber == 1)
			RssiOffset = pAd->ARssiOffset1;
		else
			RssiOffset = pAd->ARssiOffset2;
	}
	else
	{
		if (RssiNumber == 0)
			RssiOffset = pAd->BGRssiOffset0;
		else if (RssiNumber == 1)
			RssiOffset = pAd->BGRssiOffset1;
		else
			RssiOffset = pAd->BGRssiOffset2;
	}

	return (-12 - RssiOffset - LNAGain - Rssi);
}
#else
//
// The way originally used in ATE of rt2860ap.
//
CHAR ATEConvertToRssi(
	IN PRTMP_ADAPTER pAd,
	IN	CHAR			Rssi,
	IN  UCHAR   RssiNumber)
{
	UCHAR	RssiOffset, LNAGain;

	// Rssi equals to zero should be an invalid value
	if (Rssi == 0)
		return -99;

    if (pAd->LatchRfRegs.Channel > 14)
    {
        LNAGain = pAd->ALNAGain;
        if (RssiNumber == 0)
			RssiOffset = pAd->ARssiOffset0;
		else if (RssiNumber == 1)
			RssiOffset = pAd->ARssiOffset1;
		else
			RssiOffset = pAd->ARssiOffset2;
    }
    else
    {
        LNAGain = pAd->BLNAGain;
        if (RssiNumber == 0)
			RssiOffset = pAd->BGRssiOffset0;
		else if (RssiNumber == 1)
			RssiOffset = pAd->BGRssiOffset1;
		else
			RssiOffset = pAd->BGRssiOffset2;
    }

    return (-32 - RssiOffset + LNAGain - Rssi);
}
#endif /* end of #if 1 */

/*
	========================================================================

	Routine Description:
		Set Japan filter coefficients if needed.
	Note:
		This routine should only be called when
		entering TXFRAME mode or TXCONT mode.

	========================================================================
*/
static VOID SetJapanFilter(
	IN		PRTMP_ADAPTER	pAd)
{
	UCHAR			BbpData = 0;

	//
	// If Channel=14 and Bandwidth=20M and Mode=CCK, set BBP R4 bit5=1
	// (Japan Tx filter coefficients)when (TXFRAME or TXCONT).
	//
	ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BbpData);

    if ((pAd->ate.TxWI.PHYMODE == MODE_CCK) && (pAd->ate.Channel == 14) && (pAd->ate.TxWI.BW == BW_20))
    {
        BbpData |= 0x20;    // turn on
        ATEDBGPRINT(RT_DEBUG_TRACE, ("SetJapanFilter!!!\n"));
    }
    else
    {
		BbpData &= 0xdf;    // turn off
		ATEDBGPRINT(RT_DEBUG_TRACE, ("ClearJapanFilter!!!\n"));
    }

	ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BbpData);
}

VOID ATESampleRssi(
	IN PRTMP_ADAPTER	pAd,
	IN PRXWI_STRUC		pRxWI)
{
	/* There are two ways to collect RSSI. */
#if 1
	//pAd->LastRxRate = (USHORT)((pRxWI->MCS) + (pRxWI->BW <<7) + (pRxWI->ShortGI <<8)+ (pRxWI->PHYMODE <<14)) ;
	if (pRxWI->RSSI0 != 0)
	{
		pAd->ate.LastRssi0	= ATEConvertToRssi(pAd, (CHAR) pRxWI->RSSI0, RSSI_0);
		pAd->ate.AvgRssi0X8	= (pAd->ate.AvgRssi0X8 - pAd->ate.AvgRssi0) + pAd->ate.LastRssi0;
		pAd->ate.AvgRssi0  	= pAd->ate.AvgRssi0X8 >> 3;
	}
	if (pRxWI->RSSI1 != 0)
	{
		pAd->ate.LastRssi1	= ATEConvertToRssi(pAd, (CHAR) pRxWI->RSSI1, RSSI_1);
		pAd->ate.AvgRssi1X8	= (pAd->ate.AvgRssi1X8 - pAd->ate.AvgRssi1) + pAd->ate.LastRssi1;
		pAd->ate.AvgRssi1	= pAd->ate.AvgRssi1X8 >> 3;
	}
	if (pRxWI->RSSI2 != 0)
	{
		pAd->ate.LastRssi2	= ATEConvertToRssi(pAd, (CHAR) pRxWI->RSSI2, RSSI_2);
		pAd->ate.AvgRssi2X8	= (pAd->ate.AvgRssi2X8 - pAd->ate.AvgRssi2) + pAd->ate.LastRssi2;
		pAd->ate.AvgRssi2	= pAd->ate.AvgRssi2X8 >> 3;
	}

	pAd->ate.LastSNR0 = (CHAR)(pRxWI->SNR0);// CHAR ==> UCHAR ?
	pAd->ate.LastSNR1 = (CHAR)(pRxWI->SNR1);// CHAR ==> UCHAR ?

	pAd->ate.NumOfAvgRssiSample ++;
#else
	pAd->ate.LastSNR0 = (CHAR)(pRxWI->SNR0);
	pAd->ate.LastSNR1 = (CHAR)(pRxWI->SNR1);
	pAd->ate.RxCntPerSec++;
	pAd->ate.LastRssi0 = ATEConvertToRssi(pAd, (CHAR) pRxWI->RSSI0, RSSI_0);
	pAd->ate.LastRssi1 = ATEConvertToRssi(pAd, (CHAR) pRxWI->RSSI1, RSSI_1);
	pAd->ate.LastRssi2 = ATEConvertToRssi(pAd, (CHAR) pRxWI->RSSI2, RSSI_2);
	pAd->ate.AvgRssi0X8 = (pAd->ate.AvgRssi0X8 - pAd->ate.AvgRssi0) + pAd->ate.LastRssi0;
	pAd->ate.AvgRssi0 = pAd->ate.AvgRssi0X8 >> 3;
	pAd->ate.AvgRssi1X8 = (pAd->ate.AvgRssi1X8 - pAd->ate.AvgRssi1) + pAd->ate.LastRssi1;
	pAd->ate.AvgRssi1 = pAd->ate.AvgRssi1X8 >> 3;
	pAd->ate.AvgRssi2X8 = (pAd->ate.AvgRssi2X8 - pAd->ate.AvgRssi2) + pAd->ate.LastRssi2;
	pAd->ate.AvgRssi2 = pAd->ate.AvgRssi2X8 >> 3;
	pAd->ate.NumOfAvgRssiSample ++;
#endif
}

#ifdef CONFIG_STA_SUPPORT
VOID RTMPStationStop(
    IN  PRTMP_ADAPTER   pAd)
{
//	BOOLEAN       Cancelled;

    ATEDBGPRINT(RT_DEBUG_TRACE, ("==> RTMPStationStop\n"));

#if 0
	RTMPCancelTimer(&pAd->MlmeAux.AssocTimer,      &Cancelled);
	RTMPCancelTimer(&pAd->MlmeAux.ReassocTimer,    &Cancelled);
	RTMPCancelTimer(&pAd->MlmeAux.DisassocTimer,   &Cancelled);
	RTMPCancelTimer(&pAd->MlmeAux.AuthTimer,       &Cancelled);
	RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer,     &Cancelled);
	RTMPCancelTimer(&pAd->MlmeAux.ScanTimer,       &Cancelled);
#endif
	// For rx statistics, we need to keep this timer running.
//	RTMPCancelTimer(&pAd->Mlme.PeriodicTimer,      &Cancelled);

    ATEDBGPRINT(RT_DEBUG_TRACE, ("<== RTMPStationStop\n"));
}

VOID RTMPStationStart(
    IN  PRTMP_ADAPTER   pAd)
{
    ATEDBGPRINT(RT_DEBUG_TRACE, ("==> RTMPStationStart\n"));
epAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
	//
	// We did not cancel this timer when entering ATE mode.
	//
//	RTMPSetTimer(&pAd->Mlme.PeriodicTimer, MLME_TASK_EXEC_INTV);
	ATEDBGPRINT(RT_DEBUG_TRACE, ("<== RTMPStationStart\n"));
}
#endif // CONFIG_STA_SUPPORT //

/*
	==========================================================================
	Description:
		Setup Frame format.
	NOTE:
		This routine should only be used in ATE mode.
	==========================================================================
 */
static INT ATESetUpFrame(
	IN PRTMP_ADAPTER pAd,
	IN UINT32 TxIdx)
{
	UINT j;
	PTXD_STRUC pTxD;
#ifdef RT_BIG_ENDIAN
    PTXD_STRUC      pDestTxD;
    TXD_STRUC       TxD;
#endif
	PNDIS_PACKET pPacket;
	PUCHAR pDest;
	PVOID AllocVa;
	NDIS_PHYSICAL_ADDRESS AllocPa;
	HTTRANSMIT_SETTING	TxHTPhyMode;

	PRTMP_TX_RING pTxRing = &pAd->TxRing[QID_AC_BE];
	PTXWI_STRUC pTxWI = (PTXWI_STRUC) pTxRing->Cell[TxIdx].DmaBuf.AllocVa;
	PUCHAR pDMAHeaderBufVA = (PUCHAR) pTxRing->Cell[TxIdx].DmaBuf.AllocVa;

#ifdef RALINK_28xx_QA
	PHEADER_802_11	pHeader80211;
#endif // RALINK_28xx_QA //

	if (pAd->ate.bQATxStart == TRUE)
	{
		// always use QID_AC_BE and FIFO_EDCA

		// fill TxWI
		TxHTPhyMode.field.BW = pAd->ate.TxWI.BW;
		TxHTPhyMode.field.ShortGI = pAd->ate.TxWI.ShortGI;
		TxHTPhyMode.field.STBC = 0;
		TxHTPhyMode.field.MCS = pAd->ate.TxWI.MCS;
		TxHTPhyMode.field.MODE = pAd->ate.TxWI.PHYMODE;
		ATEWriteTxWI(pAd, pTxWI, pAd->ate.TxWI.FRAG, pAd->ate.TxWI.CFACK, pAd->ate.TxWI.TS,  pAd->ate.TxWI.AMPDU, pAd->ate.TxWI.ACK, pAd->ate.TxWI.NSEQ,
			pAd->ate.TxWI.BAWinSize, 0, pAd->ate.TxWI.MPDUtotalByteCount, pAd->ate.TxWI.PacketId, 0, 0, pAd->ate.TxWI.txop/*IFS_HTTXOP*/, pAd->ate.TxWI.CFACK/*FALSE*/, &TxHTPhyMode);
	}
	else
	{
		TxHTPhyMode.field.BW = pAd->ate.TxWI.BW;
		TxHTPhyMode.field.ShortGI = pAd->ate.TxWI.ShortGI;
		TxHTPhyMode.field.STBC = 0;
		TxHTPhyMode.field.MCS = pAd->ate.TxWI.MCS;
		TxHTPhyMode.field.MODE = pAd->ate.TxWI.PHYMODE;
		ATEWriteTxWI(pAd, pTxWI, FALSE, FALSE, FALSE,  FALSE, FALSE, FALSE,
			4, 0, pAd->ate.TxLength, 0, 0, 0, IFS_HTTXOP, FALSE, &TxHTPhyMode);
	}

	// fill 802.11 header.
#ifdef RALINK_28xx_QA
	if (pAd->ate.bQATxStart == TRUE)
	{
		NdisMoveMemory(pDMAHeaderBufVA+TXWI_SIZE, pAd->ate.Header, pAd->ate.HLen);
	}
	else
#endif // RALINK_28xx_QA //
	{
		NdisMoveMemory(pDMAHeaderBufVA+TXWI_SIZE, TemplateFrame, LENGTH_802_11);
		NdisMoveMemory(pDMAHeaderBufVA+TXWI_SIZE+4, pAd->ate.Addr1, ETH_LENGTH_OF_ADDRESS);
		NdisMoveMemory(pDMAHeaderBufVA+TXWI_SIZE+10, pAd->ate.Addr2, ETH_LENGTH_OF_ADDRESS);
		NdisMoveMemory(pDMAHeaderBufVA+TXWI_SIZE+16, pAd->ate.Addr3, ETH_LENGTH_OF_ADDRESS);
	}

#ifdef RT_BIG_ENDIAN
	RTMPFrameEndianChange(pAd, (((PUCHAR)pDMAHeaderBufVA)+TXWI_SIZE), DIR_READ, FALSE);
#endif // RT_BIG_ENDIAN //

	/* alloc buffer for payload */
#ifdef RALINK_28xx_QA
	if (pAd->ate.bQATxStart == TRUE)
	{
		/* Why not use RTMP_AllocateTxPacketBuffer() instead of RTMP_AllocateRxPacketBuffer()? */
		pPacket = RTMP_AllocateRxPacketBuffer(pAd, pAd->ate.DLen + 0x100, FALSE, &AllocVa, &AllocPa);
	}
	else
#endif // RALINK_28xx_QA //
	{
		/* Why not use RTMP_AllocateTxPacketBuffer() instead of RTMP_AllocateRxPacketBuffer()? */
		pPacket = RTMP_AllocateRxPacketBuffer(pAd, pAd->ate.TxLength, FALSE, &AllocVa, &AllocPa);
	}

	if (pPacket == NULL)
	{
		pAd->ate.TxCount = 0;
		ATEDBGPRINT(RT_DEBUG_TRACE, ("%s fail to alloc packet space.\n", __func__));
		return -1;
	}
	pTxRing->Cell[TxIdx].pNextNdisPacket = pPacket;

	pDest = (PUCHAR) AllocVa;

#ifdef RALINK_28xx_QA
	if (pAd->ate.bQATxStart == TRUE)
	{
		RTPKT_TO_OSPKT(pPacket)->len = pAd->ate.DLen;
	}
	else
#endif // RALINK_28xx_QA //
	{
		RTPKT_TO_OSPKT(pPacket)->len = pAd->ate.TxLength - LENGTH_802_11;
	}

	// Prepare frame payload
#ifdef RALINK_28xx_QA
	if (pAd->ate.bQATxStart == TRUE)
	{
		// copy pattern
		if ((pAd->ate.PLen != 0))
		{
			int j;

			for (j = 0; j < pAd->ate.DLen; j+=pAd->ate.PLen)
			{
				memcpy(RTPKT_TO_OSPKT(pPacket)->data + j, pAd->ate.Pattern, pAd->ate.PLen);
			}
		}
	}
	else
#endif // RALINK_28xx_QA //
	{
		for(j = 0; j < RTPKT_TO_OSPKT(pPacket)->len; j++)
			pDest[j] = 0xA5;
	}

	//
	// build Tx Descriptor
	//
#ifndef RT_BIG_ENDIAN
	pTxD = (PTXD_STRUC) pTxRing->Cell[TxIdx].AllocVa;
#else
    pDestTxD  = (PTXD_STRUC)pTxRing->Cell[TxIdx].AllocVa;
    TxD = *pDestTxD;
    pTxD = &TxD;
#endif // !RT_BIG_ENDIAN //

#ifdef RALINK_28xx_QA
	if (pAd->ate.bQATxStart == TRUE)
	{
		// prepare TxD
		NdisZeroMemory(pTxD, TXD_SIZE);
		RTMPWriteTxDescriptor(pAd, pTxD, FALSE, FIFO_EDCA);
		// build TX DESC
		pTxD->SDPtr0 = RTMP_GetPhysicalAddressLow(pTxRing->Cell[TxIdx].DmaBuf.AllocPa);
		pTxD->SDLen0 = TXWI_SIZE + pAd->ate.HLen;
		pTxD->LastSec0 = 0;
		pTxD->SDPtr1 = AllocPa;
		pTxD->SDLen1 = RTPKT_TO_OSPKT(pPacket)->len;
		pTxD->LastSec1 = 1;

		pDest = (PUCHAR)pTxWI;
		pDest += TXWI_SIZE;
		pHeader80211 = (PHEADER_802_11)pDest;

		// modify sequence number....
		if (pAd->ate.TxDoneCount == 0)
		{
			pAd->ate.seq = pHeader80211->Sequence;
		}
		else
			pHeader80211->Sequence = ++pAd->ate.seq;
	}
	else
#endif // RALINK_28xx_QA //
	{
		NdisZeroMemory(pTxD, TXD_SIZE);
		RTMPWriteTxDescriptor(pAd, pTxD, FALSE, FIFO_EDCA);
		// build TX DESC
		pTxD->SDPtr0 = RTMP_GetPhysicalAddressLow (pTxRing->Cell[TxIdx].DmaBuf.AllocPa);
		pTxD->SDLen0 = TXWI_SIZE + LENGTH_802_11;
		pTxD->LastSec0 = 0;
		pTxD->SDPtr1 = AllocPa;
		pTxD->SDLen1 = RTPKT_TO_OSPKT(pPacket)->len;
		pTxD->LastSec1 = 1;
	}

#ifdef RT_BIG_ENDIAN
	RTMPWIEndianChange((PUCHAR)pTxWI, TYPE_TXWI);
	RTMPFrameEndianChange(pAd, (((PUCHAR)pDMAHeaderBufVA)+TXWI_SIZE), DIR_WRITE, FALSE);
    RTMPDescriptorEndianChange((PUCHAR)pTxD, TYPE_TXD);
    WriteBackToDescriptor((PUCHAR)pDestTxD, (PUCHAR)pTxD, FALSE, TYPE_TXD);
#endif // RT_BIG_ENDIAN //
	return 0;
}
/*                                                           */
/*                                                           */
/*=======================End of RT2860=======================*/


VOID rt_ee_read_all(PRTMP_ADAPTER pAd, USHORT *Data)
{
	USHORT i;
	USHORT value;

	for (i = 0 ; i < EEPROM_SIZE/2 ; )
	{
		/* "value" is expecially for some compilers... */
		RT28xx_EEPROM_READ16(pAd, i*2, value);
		Data[i] = value;
		i++;
	}
}

VOID rt_ee_write_all(PRTMP_ADAPTER pAd, USHORT *Data)
{
	USHORT i;
	USHORT value;

	for (i = 0 ; i < EEPROM_SIZE/2 ; )
	{
		/* "value" is expecially for some compilers... */
		value = Data[i];
		RT28xx_EEPROM_WRITE16(pAd, i*2, value);
		i ++;
	}
}
#ifdef RALINK_28xx_QA
VOID ATE_QA_Statistics(
	IN PRTMP_ADAPTER			pAd,
	IN PRXWI_STRUC				pRxWI,
	IN PRT28XX_RXD_STRUC		pRxD,
	IN PHEADER_802_11			pHeader)
{
	// update counter first
	if (pHeader != NULL)
	{
		if (pHeader->FC.Type == BTYPE_DATA)
		{
			if (pRxD->U2M)
				pAd->ate.U2M++;
			else
				pAd->ate.OtherData++;
		}
		else if (pHeader->FC.Type == BTYPE_MGMT)
		{
			if (pHeader->FC.SubType == SUBTYPE_BEACON)
				pAd->ate.Beacon++;
			else
				pAd->ate.OtherCount++;
		}
		else if (pHeader->FC.Type == BTYPE_CNTL)
		{
			pAd->ate.OtherCount++;
		}
	}
	pAd->ate.RSSI0 = pRxWI->RSSI0;
	pAd->ate.RSSI1 = pRxWI->RSSI1;
	pAd->ate.RSSI2 = pRxWI->RSSI2;
	pAd->ate.SNR0 = pRxWI->SNR0;
	pAd->ate.SNR1 = pRxWI->SNR1;
}

/* command id with Cmd Type == 0x0008(for 28xx)/0x0005(for iNIC) */
#define RACFG_CMD_RF_WRITE_ALL		0x0000
#define RACFG_CMD_E2PROM_READ16		0x0001
#define RACFG_CMD_E2PROM_WRITE16	0x0002
#define RACFG_CMD_E2PROM_READ_ALL	0x0003
#define RACFG_CMD_E2PROM_WRITE_ALL	0x0004
#define RACFG_CMD_IO_READ			0x0005
#define RACFG_CMD_IO_WRITE			0x0006
#define RACFG_CMD_IO_READ_BULK		0x0007
#define RACFG_CMD_BBP_READ8			0x0008
#define RACFG_CMD_BBP_WRITE8		0x0009
#define RACFG_CMD_BBP_READ_ALL		0x000a
#define RACFG_CMD_GET_COUNTER		0x000b
#define RACFG_CMD_CLEAR_COUNTER		0x000c

#define RACFG_CMD_RSV1				0x000d
#define RACFG_CMD_RSV2				0x000e
#define RACFG_CMD_RSV3				0x000f

#define RACFG_CMD_TX_START			0x0010
#define RACFG_CMD_GET_TX_STATUS		0x0011
#define RACFG_CMD_TX_STOP			0x0012
#define RACFG_CMD_RX_START			0x0013
#define RACFG_CMD_RX_STOP			0x0014
#define RACFG_CMD_GET_NOISE_LEVEL	0x0015

#define RACFG_CMD_ATE_START			0x0080
#define RACFG_CMD_ATE_STOP			0x0081

#define RACFG_CMD_ATE_START_TX_CARRIER		0x0100
#define RACFG_CMD_ATE_START_TX_CONT			0x0101
#define RACFG_CMD_ATE_START_TX_FRAME		0x0102
#define RACFG_CMD_ATE_SET_BW	            0x0103
#define RACFG_CMD_ATE_SET_TX_POWER0	        0x0104
#define RACFG_CMD_ATE_SET_TX_POWER1			0x0105
#define RACFG_CMD_ATE_SET_FREQ_OFFSET		0x0106
#define RACFG_CMD_ATE_GET_STATISTICS		0x0107
#define RACFG_CMD_ATE_RESET_COUNTER			0x0108
#define RACFG_CMD_ATE_SEL_TX_ANTENNA		0x0109
#define RACFG_CMD_ATE_SEL_RX_ANTENNA		0x010a
#define RACFG_CMD_ATE_SET_PREAMBLE			0x010b
#define RACFG_CMD_ATE_SET_CHANNEL			0x010c
#define RACFG_CMD_ATE_SET_ADDR1				0x010d
#define RACFG_CMD_ATE_SET_ADDR2				0x010e
#define RACFG_CMD_ATE_SET_ADDR3				0x010f
#define RACFG_CMD_ATE_SET_RATE				0x0110
#define RACFG_CMD_ATE_SET_TX_FRAME_LEN		0x0111
#define RACFG_CMD_ATE_SET_TX_FRAME_COUNT	0x0112
#define RACFG_CMD_ATE_START_RX_FRAME		0x0113
#define RACFG_CMD_ATE_E2PROM_READ_BULK	0x0114
#define RACFG_CMD_ATE_E2PROM_WRITE_BULK	0x0115
#define RACFG_CMD_ATE_IO_WRITE_BULK		0x0116
#define RACFG_CMD_ATE_BBP_READ_BULK		0x0117
#define RACFG_CMD_ATE_BBP_WRITE_BULK	0x0118
#define RACFG_CMD_ATE_RF_READ_BULK		0x0119
#define RACFG_CMD_ATE_RF_WRITE_BULK		0x011a



#define A2Hex(_X, _p) 				\
{									\
	UCHAR *p;						\
	_X = 0;							\
	p = _p;							\
	while (((*p >= 'a') && (*p <= 'f')) || ((*p >= 'A') && (*p <= 'F')) || ((*p >= '0') && (*p <= '9')))		\
	{												\
		if ((*p >= 'a') && (*p <= 'f'))				\
			_X = _X * 16 + *p - 87;					\
		else if ((*p >= 'A') && (*p <= 'F'))		\
			_X = _X * 16 + *p - 55;					\
		else if ((*p >= '0') && (*p <= '9'))		\
			_X = _X * 16 + *p - 48;					\
		p++;										\
	}												\
}


static VOID memcpy_exl(PRTMP_ADAPTER pAd, UCHAR *dst, UCHAR *src, ULONG len);
static VOID memcpy_exs(PRTMP_ADAPTER pAd, UCHAR *dst, UCHAR *src, ULONG len);
static VOID RTMP_IO_READ_BULK(PRTMP_ADAPTER pAd, UCHAR *dst, UCHAR *src, UINT32 len);

#ifdef UCOS
int ate_copy_to_user(
	IN PUCHAR payload,
	IN PUCHAR msg,
	IN INT    len)
{
	memmove(payload, msg, len);
	return 0;
}

#undef	copy_to_user
#define copy_to_user(x,y,z) ate_copy_to_user((PUCHAR)x, (PUCHAR)y, z)
#endif // UCOS //

#define	LEN_OF_ARG 16

VOID RtmpDoAte(
	IN	PRTMP_ADAPTER	pAdapter,
	IN	struct iwreq	*wrq)
{
	unsigned short Command_Id;
	struct ate_racfghdr *pRaCfg;
	INT	Status = NDIS_STATUS_SUCCESS;



	if((pRaCfg = kmalloc(sizeof(struct ate_racfghdr), GFP_KERNEL)) == NULL)
	{
		Status = -EINVAL;
		return;
	}

	NdisZeroMemory(pRaCfg, sizeof(struct ate_racfghdr));

    if (copy_from_user((PUCHAR)pRaCfg, wrq->u.data.pointer, wrq->u.data.length))
	{
		Status = -EFAULT;
		kfree(pRaCfg);
		return;
	}


	Command_Id = ntohs(pRaCfg->command_id);

	ATEDBGPRINT(RT_DEBUG_TRACE,("\n%s: Command_Id = 0x%04x !\n", __func__, Command_Id));

	switch (Command_Id)
	{
 		// We will get this command when QA starts.
		case RACFG_CMD_ATE_START:
			{
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_START\n"));

				// prepare feedback as soon as we can to avoid QA timeout.
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);

				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

				ATEDBGPRINT(RT_DEBUG_TRACE, ("wrq->u.data.length = %d\n", wrq->u.data.length));

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_TRACE, ("copy_to_user() fail in case RACFG_CMD_ATE_START\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_START is done !\n"));
				}
				Set_ATE_Proc(pAdapter, "ATESTART");
			}
			break;

 		// We will get this command either QA is closed or ated is killed by user.
		case RACFG_CMD_ATE_STOP:
			{
#ifndef UCOS
				INT32 ret;
#endif // !UCOS //

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_STOP\n"));

				// Distinguish this command came from QA(via ated)
				// or ate daemon according to the existence of pid in payload.
				// No need to prepare feedback if this cmd came directly from ate daemon.
				pRaCfg->length = ntohs(pRaCfg->length);

				if (pRaCfg->length == sizeof(pAdapter->ate.AtePid))
				{
					// This command came from QA.
					// Get the pid of ATE daemon.
					memcpy((UCHAR *)&pAdapter->ate.AtePid,
						(&pRaCfg->data[0]) - 2/* == &(pRaCfg->status) */,
						sizeof(pAdapter->ate.AtePid));

					// prepare feedback as soon as we can to avoid QA timeout.
					pRaCfg->length = htons(2);
					pRaCfg->status = htons(0);

					wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
										+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
										+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

					ATEDBGPRINT(RT_DEBUG_TRACE, ("wrq->u.data.length = %d\n", wrq->u.data.length));

	            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
	            	{
	            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_STOP\n"));
	                    Status = -EFAULT;
	            	}

					//
					// kill ATE daemon when leaving ATE mode.
					// We must kill ATE daemon first before setting ATESTOP,
					// or Microsoft will report sth. wrong.
#ifndef UCOS
					ret = KILL_THREAD_PID(pAdapter->ate.AtePid, SIGTERM, 1);
					if (ret)
					{
						ATEDBGPRINT(RT_DEBUG_ERROR, ("%s: unable to signal thread\n", pAdapter->net_dev->name));
					}
#endif // !UCOS //
				}

				// AP might have in ATE_STOP mode due to cmd from QA.
				if (ATE_ON(pAdapter))
				{
					// Someone has killed ate daemon while QA GUI is still open.
					Set_ATE_Proc(pAdapter, "ATESTOP");
					ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_AP_START is done !\n"));
				}
			}
			break;

		case RACFG_CMD_RF_WRITE_ALL:
			{
				UINT32 R1, R2, R3, R4;
				USHORT channel;

				memcpy(&R1, pRaCfg->data-2, 4);
				memcpy(&R2, pRaCfg->data+2, 4);
				memcpy(&R3, pRaCfg->data+6, 4);
				memcpy(&R4, pRaCfg->data+10, 4);
				memcpy(&channel, pRaCfg->data+14, 2);

				pAdapter->LatchRfRegs.R1 = ntohl(R1);
				pAdapter->LatchRfRegs.R2 = ntohl(R2);
				pAdapter->LatchRfRegs.R3 = ntohl(R3);
				pAdapter->LatchRfRegs.R4 = ntohl(R4);
				pAdapter->LatchRfRegs.Channel = ntohs(channel);

				RTMP_RF_IO_WRITE32(pAdapter, pAdapter->LatchRfRegs.R1);
				RTMP_RF_IO_WRITE32(pAdapter, pAdapter->LatchRfRegs.R2);
				RTMP_RF_IO_WRITE32(pAdapter, pAdapter->LatchRfRegs.R3);
				RTMP_RF_IO_WRITE32(pAdapter, pAdapter->LatchRfRegs.R4);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);

				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

				ATEDBGPRINT(RT_DEBUG_TRACE, ("wrq->u.data.length = %d\n", wrq->u.data.length));
            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_RF_WRITE_ALL\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_RF_WRITE_ALL is done !\n"));
				}
			}
            break;

		case RACFG_CMD_E2PROM_READ16:
			{
				USHORT	offset, value, tmp;

				offset = ntohs(pRaCfg->status);
				/* "tmp" is expecially for some compilers... */
				RT28xx_EEPROM_READ16(pAdapter, offset, tmp);
				value = tmp;
				value = htons(value);

				ATEDBGPRINT(RT_DEBUG_TRACE,("EEPROM Read offset = 0x%04x, value = 0x%04x\n", offset, value));

				// prepare feedback
				pRaCfg->length = htons(4);
				pRaCfg->status = htons(0);
				memcpy(pRaCfg->data, &value, 2);

				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

				ATEDBGPRINT(RT_DEBUG_TRACE, ("sizeof(struct ate_racfghdr) = %d\n", sizeof(struct ate_racfghdr)));
				ATEDBGPRINT(RT_DEBUG_TRACE, ("wrq->u.data.length = %d\n", wrq->u.data.length));

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_E2PROM_READ16\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_E2PROM_READ16 is done !\n"));
				}
           	}
			break;

		case RACFG_CMD_E2PROM_WRITE16:
			{
				USHORT	offset, value;

				offset = ntohs(pRaCfg->status);
				memcpy(&value, pRaCfg->data, 2);
				value = ntohs(value);
				RT28xx_EEPROM_WRITE16(pAdapter, offset, value);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_E2PROM_WRITE16\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_E2PROM_WRITE16 is done !\n"));
				}
			}
			break;

		case RACFG_CMD_E2PROM_READ_ALL:
			{
				USHORT buffer[EEPROM_SIZE/2];

				rt_ee_read_all(pAdapter,(USHORT *)buffer);
				memcpy_exs(pAdapter, pRaCfg->data, (UCHAR *)buffer, EEPROM_SIZE);

				// prepare feedback
				pRaCfg->length = htons(2+EEPROM_SIZE);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_E2PROM_READ_ALL\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_E2PROM_READ_ALL is done !\n"));
				}
			}
			break;

		case RACFG_CMD_E2PROM_WRITE_ALL:
			{
				USHORT buffer[EEPROM_SIZE/2];

				NdisZeroMemory((UCHAR *)buffer, EEPROM_SIZE);
				memcpy_exs(pAdapter, (UCHAR *)buffer, (UCHAR *)&pRaCfg->status, EEPROM_SIZE);
				rt_ee_write_all(pAdapter,(USHORT *)buffer);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_E2PROM_WRITE_ALL\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_ERROR, ("RACFG_CMD_E2PROM_WRITE_ALL is done !\n"));
				}

			}
			break;

		case RACFG_CMD_IO_READ:
			{
				UINT32	offset;
				UINT32	value;

				memcpy(&offset, &pRaCfg->status, 4);
				offset = ntohl(offset);

				// We do not need the base address.
				// So just extract the offset out.
				offset &= 0x0000FFFF;
				RTMP_IO_READ32(pAdapter, offset, &value);
				value = htonl(value);

				// prepare feedback
				pRaCfg->length = htons(6);
				pRaCfg->status = htons(0);
				memcpy(pRaCfg->data, &value, 4);

				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_IO_READ\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_IO_READ is done !\n"));
				}
			}
			break;

		case RACFG_CMD_IO_WRITE:
			{
				UINT32	offset, value;

				memcpy(&offset, pRaCfg->data-2, 4);
				memcpy(&value, pRaCfg->data+2, 4);

				offset = ntohl(offset);

				// We do not need the base address.
				// So just extract out the offset.
				offset &= 0x0000FFFF;
				value = ntohl(value);
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_IO_WRITE: offset = %x, value = %x\n", offset, value));
				RTMP_IO_WRITE32(pAdapter, offset, value);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_IO_WRITE\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_IO_WRITE is done !\n"));
				}
			}
			break;

		case RACFG_CMD_IO_READ_BULK:
			{
				UINT32	offset;
				USHORT	len;

				memcpy(&offset, &pRaCfg->status, 4);
				offset = ntohl(offset);

				// We do not need the base address.
				// So just extract the offset.
				offset &= 0x0000FFFF;
				memcpy(&len, pRaCfg->data+2, 2);
				len = ntohs(len);

				if (len > 371)
				{
					ATEDBGPRINT(RT_DEBUG_TRACE,("len is too large, make it smaller\n"));
					pRaCfg->length = htons(2);
					pRaCfg->status = htons(1);
					break;
				}

				RTMP_IO_READ_BULK(pAdapter, pRaCfg->data, (UCHAR *)offset, len*4);// unit in four bytes

				// prepare feedback
				pRaCfg->length = htons(2+len*4);// unit in four bytes
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_IO_READ_BULK\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_IO_READ_BULK is done !\n"));
				}
			}
			break;

		case RACFG_CMD_BBP_READ8:
			{
				USHORT	offset;
				UCHAR	value;

				value = 0;
				offset = ntohs(pRaCfg->status);

				if (ATE_ON(pAdapter))
				{
					ATE_BBP_IO_READ8_BY_REG_ID(pAdapter, offset,  &value);
				}
				else
				{
					RTMP_BBP_IO_READ8_BY_REG_ID(pAdapter, offset,  &value);
				}
				// prepare feedback
				pRaCfg->length = htons(3);
				pRaCfg->status = htons(0);
				pRaCfg->data[0] = value;

				ATEDBGPRINT(RT_DEBUG_TRACE,("BBP value = %x\n", value));

				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_BBP_READ8\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_BBP_READ8 is done !\n"));
				}
			}
			break;
		case RACFG_CMD_BBP_WRITE8:
			{
				USHORT	offset;
				UCHAR	value;

				offset = ntohs(pRaCfg->status);
				memcpy(&value, pRaCfg->data, 1);

				if (ATE_ON(pAdapter))
				{
					ATE_BBP_IO_WRITE8_BY_REG_ID(pAdapter, offset,  value);
				}
				else
				{
					RTMP_BBP_IO_WRITE8_BY_REG_ID(pAdapter, offset,  value);
				}

				if ((offset == BBP_R1) || (offset == BBP_R3))
				{
					SyncTxRxConfig(pAdapter, offset, value);
				}

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_BBP_WRITE8\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_BBP_WRITE8 is done !\n"));
				}
			}
			break;

		case RACFG_CMD_BBP_READ_ALL:
			{
				USHORT j;

				for (j = 0; j < 137; j++)
				{
					pRaCfg->data[j] = 0;

					if (ATE_ON(pAdapter))
					{
						ATE_BBP_IO_READ8_BY_REG_ID(pAdapter, j,  &pRaCfg->data[j]);
					}
					else
					{
						RTMP_BBP_IO_READ8_BY_REG_ID(pAdapter, j,  &pRaCfg->data[j]);
					}
				}

				// prepare feedback
				pRaCfg->length = htons(2+137);
				pRaCfg->status = htons(0);

				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_BBP_READ_ALL\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_BBP_READ_ALL is done !\n"));
				}
			}

			break;

		case RACFG_CMD_ATE_E2PROM_READ_BULK:
		{
			USHORT offset;
			USHORT len;
			USHORT buffer[EEPROM_SIZE/2];

			offset = ntohs(pRaCfg->status);
			memcpy(&len, pRaCfg->data, 2);
			len = ntohs(len);

			rt_ee_read_all(pAdapter,(USHORT *)buffer);
			if (offset + len <= EEPROM_SIZE)
				memcpy_exs(pAdapter, pRaCfg->data, (UCHAR *)buffer+offset, len);
			else
				ATEDBGPRINT(RT_DEBUG_ERROR, ("exceed EEPROM size\n"));

			// prepare feedback
			pRaCfg->length = htons(2+len);
			pRaCfg->status = htons(0);
			wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            {
            	ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_E2PROM_READ_BULK\n"));
                Status = -EFAULT;
            }
			else
			{
               	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_E2PROM_READ_BULK is done !\n"));
			}

		}
			break;

		case RACFG_CMD_ATE_E2PROM_WRITE_BULK:
		{
			USHORT offset;
			USHORT len;
			USHORT buffer[EEPROM_SIZE/2];

			offset = ntohs(pRaCfg->status);
			memcpy(&len, pRaCfg->data, 2);
			len = ntohs(len);

			rt_ee_read_all(pAdapter,(USHORT *)buffer);
			memcpy_exs(pAdapter, (UCHAR *)buffer + offset, (UCHAR *)pRaCfg->data + 2, len);
			rt_ee_write_all(pAdapter,(USHORT *)buffer);

			// prepare feedback
			pRaCfg->length = htons(2);
			pRaCfg->status = htons(0);
			wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
								+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
								+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);
            if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            {
            	ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_E2PROM_WRITE_BULK\n"));
                   Status = -EFAULT;
            }
			else
			{
               	ATEDBGPRINT(RT_DEBUG_ERROR, ("RACFG_CMD_ATE_E2PROM_WRITE_BULK is done !\n"));
			}

		}
			break;

		case RACFG_CMD_ATE_IO_WRITE_BULK:
		{
			UINT32 offset, i, value;
			USHORT len;

			memcpy(&offset, &pRaCfg->status, 4);
			offset = ntohl(offset);
			memcpy(&len, pRaCfg->data+2, 2);
			len = ntohs(len);

			for (i = 0; i < len; i += 4)
			{
				memcpy_exl(pAdapter, (UCHAR *)&value, pRaCfg->data+4+i, 4);
				printk("Write %x %x\n", offset + i, value);
				RTMP_IO_WRITE32(pAdapter, (offset +i) & 0xffff, value);
			}

			// prepare feedback
			pRaCfg->length = htons(2);
			pRaCfg->status = htons(0);
			wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
								+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
								+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);
            if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            {
            	ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_IO_WRITE_BULK\n"));
                   Status = -EFAULT;
            }
			else
			{
               	ATEDBGPRINT(RT_DEBUG_ERROR, ("RACFG_CMD_ATE_IO_WRITE_BULK is done !\n"));
			}

		}
			break;

		case RACFG_CMD_ATE_BBP_READ_BULK:
		{
			USHORT offset;
			USHORT len;
			USHORT j;

			offset = ntohs(pRaCfg->status);
			memcpy(&len, pRaCfg->data, 2);
			len = ntohs(len);


			for (j = offset; j < (offset+len); j++)
			{
				pRaCfg->data[j - offset] = 0;

				if (pAdapter->ate.Mode == ATE_STOP)
				{
					RTMP_BBP_IO_READ8_BY_REG_ID(pAdapter, j,  &pRaCfg->data[j - offset]);
				}
				else
				{
					ATE_BBP_IO_READ8_BY_REG_ID(pAdapter, j,  &pRaCfg->data[j - offset]);
				}
			}

			// prepare feedback
			pRaCfg->length = htons(2+len);
			pRaCfg->status = htons(0);
			wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
								+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
								+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            {
            	ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_BBP_READ_BULK\n"));
                   Status = -EFAULT;
            }
			else
			{
               	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_BBP_READ_BULK is done !\n"));
			}

		}
			break;

		case RACFG_CMD_ATE_BBP_WRITE_BULK:
		{
			USHORT offset;
			USHORT len;
			USHORT j;
			UCHAR *value;

			offset = ntohs(pRaCfg->status);
			memcpy(&len, pRaCfg->data, 2);
			len = ntohs(len);

			for (j = offset; j < (offset+len); j++)
			{
				value = pRaCfg->data + 2 + (j - offset);
				if (pAdapter->ate.Mode == ATE_STOP)
				{
					RTMP_BBP_IO_WRITE8_BY_REG_ID(pAdapter, j,  *value);
				}
				else
				{
					ATE_BBP_IO_WRITE8_BY_REG_ID(pAdapter, j,  *value);
				}
			}

			// prepare feedback
			pRaCfg->length = htons(2);
			pRaCfg->status = htons(0);
			wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
								+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
								+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            {
            	ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_BBP_WRITE_BULK\n"));
                   Status = -EFAULT;
            }
			else
			{
               	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_BBP_WRITE_BULK is done !\n"));
			}
		}
			break;

#ifdef CONFIG_RALINK_RT3052
		case RACFG_CMD_ATE_RF_READ_BULK:
		{
			USHORT offset;
			USHORT len;
			USHORT j;

			offset = ntohs(pRaCfg->status);
			memcpy(&len, pRaCfg->data, 2);
			len = ntohs(len);

			for (j = offset; j < (offset+len); j++)
			{
				pRaCfg->data[j - offset] = 0;
				RT30xxReadRFRegister(pAdapter, j,  &pRaCfg->data[j - offset]);
			}

			// prepare feedback
			pRaCfg->length = htons(2+len);
			pRaCfg->status = htons(0);
			wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
								+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
								+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            {
            	ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_RF_READ_BULK\n"));
                   Status = -EFAULT;
            }
			else
			{
               	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_RF_READ_BULK is done !\n"));
			}

		}
			break;

		case RACFG_CMD_ATE_RF_WRITE_BULK:
		{
			USHORT offset;
			USHORT len;
			USHORT j;
			UCHAR *value;

			offset = ntohs(pRaCfg->status);
			memcpy(&len, pRaCfg->data, 2);
			len = ntohs(len);

			for (j = offset; j < (offset+len); j++)
			{
				value = pRaCfg->data + 2 + (j - offset);
				RT30xxWriteRFRegister(pAdapter, j,  *value);
			}

			// prepare feedback
			pRaCfg->length = htons(2);
			pRaCfg->status = htons(0);
			wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
								+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
								+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            {
            	ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_RF_WRITE_BULK\n"));
                   Status = -EFAULT;
            }
			else
			{
               	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_RF_WRITE_BULK is done !\n"));
			}

		}
			break;
#endif


		case RACFG_CMD_GET_NOISE_LEVEL:
			{
				UCHAR	channel;
				INT32   buffer[3][10];/* 3 : RxPath ; 10 : no. of per rssi samples */

				channel = (ntohs(pRaCfg->status) & 0x00FF);
				CalNoiseLevel(pAdapter, channel, buffer);
				memcpy_exl(pAdapter, (UCHAR *)pRaCfg->data, (UCHAR *)&(buffer[0][0]), (sizeof(INT32)*3*10));

				// prepare feedback
				pRaCfg->length = htons(2 + (sizeof(INT32)*3*10));
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_GET_NOISE_LEVEL\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_GET_NOISE_LEVEL is done !\n"));
				}
			}
			break;

		case RACFG_CMD_GET_COUNTER:
			{
				memcpy_exl(pAdapter, &pRaCfg->data[0], (UCHAR *)&pAdapter->ate.U2M, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[4], (UCHAR *)&pAdapter->ate.OtherData, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[8], (UCHAR *)&pAdapter->ate.Beacon, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[12], (UCHAR *)&pAdapter->ate.OtherCount, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[16], (UCHAR *)&pAdapter->ate.TxAc0, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[20], (UCHAR *)&pAdapter->ate.TxAc1, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[24], (UCHAR *)&pAdapter->ate.TxAc2, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[28], (UCHAR *)&pAdapter->ate.TxAc3, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[32], (UCHAR *)&pAdapter->ate.TxHCCA, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[36], (UCHAR *)&pAdapter->ate.TxMgmt, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[40], (UCHAR *)&pAdapter->ate.RSSI0, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[44], (UCHAR *)&pAdapter->ate.RSSI1, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[48], (UCHAR *)&pAdapter->ate.RSSI2, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[52], (UCHAR *)&pAdapter->ate.SNR0, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[56], (UCHAR *)&pAdapter->ate.SNR1, 4);

				pRaCfg->length = htons(2+60);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_GET_COUNTER\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_GET_COUNTER is done !\n"));
				}
			}
			break;

		case RACFG_CMD_CLEAR_COUNTER:
			{
				pAdapter->ate.U2M = 0;
				pAdapter->ate.OtherData = 0;
				pAdapter->ate.Beacon = 0;
				pAdapter->ate.OtherCount = 0;
				pAdapter->ate.TxAc0 = 0;
				pAdapter->ate.TxAc1 = 0;
				pAdapter->ate.TxAc2 = 0;
				pAdapter->ate.TxAc3 = 0;
				pAdapter->ate.TxHCCA = 0;
				pAdapter->ate.TxMgmt = 0;
				pAdapter->ate.TxDoneCount = 0;

				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);

				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_CLEAR_COUNTER\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_CLEAR_COUNTER is done !\n"));
				}
			}

			break;

		case RACFG_CMD_TX_START:
			{
				USHORT *p;
				USHORT	err = 1;
				UCHAR   Bbp22Value = 0, Bbp24Value = 0;

				if ((pAdapter->ate.TxStatus != 0) && (pAdapter->ate.Mode & ATE_TXFRAME))
				{
					ATEDBGPRINT(RT_DEBUG_TRACE,("Ate Tx is already running, to run next Tx, you must stop it first\n"));
					err = 2;
					goto TX_START_ERROR;
				}
				else if ((pAdapter->ate.TxStatus != 0) && !(pAdapter->ate.Mode & ATE_TXFRAME))
				{
					int i = 0;

					while ((i++ < 10) && (pAdapter->ate.TxStatus != 0))
					{
						RTMPusecDelay(5000);
					}

					// force it to stop
					pAdapter->ate.TxStatus = 0;
					pAdapter->ate.TxDoneCount = 0;
					//pAdapter->ate.Repeat = 0;
					pAdapter->ate.bQATxStart = FALSE;
				}

				// If pRaCfg->length == 0, this "RACFG_CMD_TX_START" is for Carrier test or Carrier Suppression.
				if (ntohs(pRaCfg->length) != 0)
				{
					// Get frame info

					NdisMoveMemory(&pAdapter->ate.TxWI, pRaCfg->data + 2, 16);
#ifdef RT_BIG_ENDIAN
					RTMPWIEndianChange((PUCHAR)&pAdapter->ate.TxWI, TYPE_TXWI);
#endif // RT_BIG_ENDIAN //

					NdisMoveMemory(&pAdapter->ate.TxCount, pRaCfg->data + 18, 4);
					pAdapter->ate.TxCount = ntohl(pAdapter->ate.TxCount);

					p = (USHORT *)(&pRaCfg->data[22]);
					//p = pRaCfg->data + 22;
					// always use QID_AC_BE
					pAdapter->ate.QID = 0;
					p = (USHORT *)(&pRaCfg->data[24]);
					//p = pRaCfg->data + 24;
					pAdapter->ate.HLen = ntohs(*p);

					if (pAdapter->ate.HLen > 32)
					{
						ATEDBGPRINT(RT_DEBUG_ERROR,("pAdapter->ate.HLen > 32\n"));
						err = 3;
						goto TX_START_ERROR;
					}

					NdisMoveMemory(&pAdapter->ate.Header, pRaCfg->data + 26, pAdapter->ate.HLen);


					pAdapter->ate.PLen = ntohs(pRaCfg->length) - (pAdapter->ate.HLen + 28);

					if (pAdapter->ate.PLen > 32)
					{
						ATEDBGPRINT(RT_DEBUG_ERROR,("pAdapter->ate.PLen > 32\n"));
						err = 4;
						goto TX_START_ERROR;
					}

					NdisMoveMemory(&pAdapter->ate.Pattern, pRaCfg->data + 26 + pAdapter->ate.HLen, pAdapter->ate.PLen);
					pAdapter->ate.DLen = pAdapter->ate.TxWI.MPDUtotalByteCount - pAdapter->ate.HLen;
				}

				ATE_BBP_IO_READ8_BY_REG_ID(pAdapter, BBP_R22, &Bbp22Value);

				switch (Bbp22Value)
				{
					case BBP22_TXFRAME:
						{
							if (pAdapter->ate.TxCount == 0)
							{
								pAdapter->ate.TxCount = 0xFFFFFFFF;
							}
							ATEDBGPRINT(RT_DEBUG_TRACE,("START TXFRAME\n"));
							pAdapter->ate.bQATxStart = TRUE;
							Set_ATE_Proc(pAdapter, "TXFRAME");
						}
						break;

					case BBP22_TXCONT_OR_CARRSUPP:
						{
							ATEDBGPRINT(RT_DEBUG_TRACE,("BBP22_TXCONT_OR_CARRSUPP\n"));
							ATE_BBP_IO_READ8_BY_REG_ID(pAdapter, 24, &Bbp24Value);

							switch (Bbp24Value)
							{
								case BBP24_TXCONT:
									{
										ATEDBGPRINT(RT_DEBUG_TRACE,("START TXCONT\n"));
										pAdapter->ate.bQATxStart = TRUE;
										Set_ATE_Proc(pAdapter, "TXCONT");
									}
									break;

								case BBP24_CARRSUPP:
									{
										ATEDBGPRINT(RT_DEBUG_TRACE,("START TXCARRSUPP\n"));
										pAdapter->ate.bQATxStart = TRUE;
										pAdapter->ate.Mode |= ATE_TXCARRSUPP;
									}
									break;

								default:
									{
										ATEDBGPRINT(RT_DEBUG_ERROR,("Unknown Start TX subtype !"));
									}
									break;
							}
						}
						break;

					case BBP22_TXCARR:
						{
							ATEDBGPRINT(RT_DEBUG_TRACE,("START TXCARR\n"));
							pAdapter->ate.bQATxStart = TRUE;
							Set_ATE_Proc(pAdapter, "TXCARR");
						}
						break;

					default:
						{
							ATEDBGPRINT(RT_DEBUG_ERROR,("Unknown Start TX subtype !"));
						}
						break;
				}

				if (pAdapter->ate.bQATxStart == TRUE)
				{
					// prepare feedback
					pRaCfg->length = htons(2);
					pRaCfg->status = htons(0);

					wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
										+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
										+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

	            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
	            	{
	            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() was failed in case RACFG_CMD_TX_START\n"));
	                    Status = -EFAULT;
	            	}
					else
					{
	                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_TX_START is done !\n"));
					}
					break;
				}

TX_START_ERROR:
				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(err);

				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);
            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_TX_START\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("feedback of TX_START_ERROR is done !\n"));
				}
			}
			break;

		case RACFG_CMD_GET_TX_STATUS:
			{
				UINT32 count;

				// prepare feedback
				pRaCfg->length = htons(6);
				pRaCfg->status = htons(0);
				count = htonl(pAdapter->ate.TxDoneCount);
				NdisMoveMemory(pRaCfg->data, &count, 4);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_GET_TX_STATUS\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_GET_TX_STATUS is done !\n"));
				}
			}
			break;

		case RACFG_CMD_TX_STOP:
			{
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_TX_STOP\n"));

				Set_ATE_Proc(pAdapter, "TXSTOP");

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_TRACE, ("copy_to_user() fail in case RACFG_CMD_TX_STOP\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_TX_STOP is done !\n"));
				}
			}
			break;

		case RACFG_CMD_RX_START:
			{
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_RX_START\n"));

				pAdapter->ate.bQARxStart = TRUE;
				Set_ATE_Proc(pAdapter, "RXFRAME");

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_RX_START\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_RX_START is done !\n"));
				}
			}
			break;

		case RACFG_CMD_RX_STOP:
			{
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_RX_STOP\n"));

				Set_ATE_Proc(pAdapter, "RXSTOP");

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_RX_STOP\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_RX_STOP is done !\n"));
				}
			}
			break;

		/* The following cases are for new ATE GUI(not QA). */
		/*==================================================*/
		case RACFG_CMD_ATE_START_TX_CARRIER:
			{
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_START_TX_CARRIER\n"));

				Set_ATE_Proc(pAdapter, "TXCARR");

				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);

				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

				ATEDBGPRINT(RT_DEBUG_TRACE, ("wrq->u.data.length = %d\n", wrq->u.data.length));

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_START_TX_CARRIER\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_START_TX_CARRIER is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_START_TX_CONT:
			{
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_START_TX_CONT\n"));

				Set_ATE_Proc(pAdapter, "TXCONT");

				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);

				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

				ATEDBGPRINT(RT_DEBUG_TRACE, ("wrq->u.data.length = %d\n", wrq->u.data.length));

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_START_TX_CONT\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_START_TX_CONT is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_START_TX_FRAME:
			{
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_START_TX_FRAME\n"));

				Set_ATE_Proc(pAdapter, "TXFRAME");

				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);

				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

				ATEDBGPRINT(RT_DEBUG_TRACE, ("wrq->u.data.length = %d\n", wrq->u.data.length));

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_START_TX_FRAME\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_START_TX_FRAME is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_SET_BW:
			{
				SHORT    value = 0;
				UCHAR    str[LEN_OF_ARG];

				NdisZeroMemory(str, LEN_OF_ARG);

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SET_BW\n"));

				memcpy((PUCHAR)&value, (PUCHAR)&(pRaCfg->status), 2);
				value = ntohs(value);
				sprintf((PCHAR)str, "%d", value);

				Set_ATE_TX_BW_Proc(pAdapter, str);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SET_BW\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SET_BW is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_SET_TX_POWER0:
			{
				SHORT    value = 0;
				UCHAR    str[LEN_OF_ARG];

				NdisZeroMemory(str, LEN_OF_ARG);

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SET_TX_POWER0\n"));

				memcpy((PUCHAR)&value, (PUCHAR)&(pRaCfg->status), 2);
				value = ntohs(value);
				sprintf((PCHAR)str, "%d", value);
				Set_ATE_TX_POWER0_Proc(pAdapter, str);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SET_TX_POWER0\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SET_TX_POWER0 is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_SET_TX_POWER1:
			{
				SHORT    value = 0;
				UCHAR    str[LEN_OF_ARG];

				NdisZeroMemory(str, LEN_OF_ARG);

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SET_TX_POWER1\n"));

				memcpy((PUCHAR)&value, (PUCHAR)&(pRaCfg->status), 2);
				value = ntohs(value);
				sprintf((PCHAR)str, "%d", value);
				Set_ATE_TX_POWER1_Proc(pAdapter, str);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SET_TX_POWER1\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SET_TX_POWER1 is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_SET_FREQ_OFFSET:
			{
				SHORT    value = 0;
				UCHAR    str[LEN_OF_ARG];

				NdisZeroMemory(str, LEN_OF_ARG);

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SET_FREQ_OFFSET\n"));

				memcpy((PUCHAR)&value, (PUCHAR)&(pRaCfg->status), 2);
				value = ntohs(value);
				sprintf((PCHAR)str, "%d", value);
				Set_ATE_TX_FREQOFFSET_Proc(pAdapter, str);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SET_FREQ_OFFSET\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SET_FREQ_OFFSET is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_GET_STATISTICS:
			{
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_GET_STATISTICS\n"));

				memcpy_exl(pAdapter, &pRaCfg->data[0], (UCHAR *)&pAdapter->ate.TxDoneCount, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[4], (UCHAR *)&pAdapter->WlanCounters.RetryCount.u.LowPart, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[8], (UCHAR *)&pAdapter->WlanCounters.FailedCount.u.LowPart, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[12], (UCHAR *)&pAdapter->WlanCounters.RTSSuccessCount.u.LowPart, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[16], (UCHAR *)&pAdapter->WlanCounters.RTSFailureCount.u.LowPart, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[20], (UCHAR *)&pAdapter->WlanCounters.ReceivedFragmentCount.QuadPart, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[24], (UCHAR *)&pAdapter->WlanCounters.FCSErrorCount.u.LowPart, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[28], (UCHAR *)&pAdapter->Counters8023.RxNoBuffer, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[32], (UCHAR *)&pAdapter->WlanCounters.FrameDuplicateCount.u.LowPart, 4);
				memcpy_exl(pAdapter, &pRaCfg->data[36], (UCHAR *)&pAdapter->RalinkCounters.OneSecFalseCCACnt, 4);

				if (pAdapter->ate.RxAntennaSel == 0)
				{
					INT32 RSSI0 = 0;
					INT32 RSSI1 = 0;
					INT32 RSSI2 = 0;

					RSSI0 = (INT32)(pAdapter->ate.LastRssi0 - pAdapter->BbpRssiToDbmDelta);
					RSSI1 = (INT32)(pAdapter->ate.LastRssi1 - pAdapter->BbpRssiToDbmDelta);
					RSSI2 = (INT32)(pAdapter->ate.LastRssi2 - pAdapter->BbpRssiToDbmDelta);
					memcpy_exl(pAdapter, &pRaCfg->data[40], (UCHAR *)&RSSI0, 4);
					memcpy_exl(pAdapter, &pRaCfg->data[44], (UCHAR *)&RSSI1, 4);
					memcpy_exl(pAdapter, &pRaCfg->data[48], (UCHAR *)&RSSI2, 4);
					pRaCfg->length = htons(2+52);
				}
				else
				{
					INT32 RSSI0 = 0;

					RSSI0 = (INT32)(pAdapter->ate.LastRssi0 - pAdapter->BbpRssiToDbmDelta);
					memcpy_exl(pAdapter, &pRaCfg->data[40], (UCHAR *)&RSSI0, 4);
					pRaCfg->length = htons(2+44);
				}
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_GET_STATISTICS\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_GET_STATISTICS is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_RESET_COUNTER:
			{
				SHORT    value = 1;
				UCHAR    str[LEN_OF_ARG];

				NdisZeroMemory(str, LEN_OF_ARG);

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_RESET_COUNTER\n"));

				sprintf((PCHAR)str, "%d", value);
				Set_ResetStatCounter_Proc(pAdapter, str);

				pAdapter->ate.TxDoneCount = 0;

				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);

				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_RESET_COUNTER\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_RESET_COUNTER is done !\n"));
				}
			}

			break;

		case RACFG_CMD_ATE_SEL_TX_ANTENNA:
			{
				SHORT    value = 0;
				UCHAR    str[LEN_OF_ARG];

				NdisZeroMemory(str, LEN_OF_ARG);

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SEL_TX_ANTENNA\n"));

				memcpy((PUCHAR)&value, (PUCHAR)&(pRaCfg->status), 2);
				value = ntohs(value);
				sprintf((PCHAR)str, "%d", value);
				Set_ATE_TX_Antenna_Proc(pAdapter, str);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SEL_TX_ANTENNA\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SEL_TX_ANTENNA is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_SEL_RX_ANTENNA:
			{
				SHORT    value = 0;
				UCHAR    str[LEN_OF_ARG];

				NdisZeroMemory(str, LEN_OF_ARG);

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SEL_RX_ANTENNA\n"));

				memcpy((PUCHAR)&value, (PUCHAR)&(pRaCfg->status), 2);
				value = ntohs(value);
				sprintf((PCHAR)str, "%d", value);
				Set_ATE_RX_Antenna_Proc(pAdapter, str);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SEL_RX_ANTENNA\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SEL_RX_ANTENNA is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_SET_PREAMBLE:
			{
				SHORT    value = 0;
				UCHAR    str[LEN_OF_ARG];

				NdisZeroMemory(str, LEN_OF_ARG);

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SET_PREAMBLE\n"));

				memcpy((PUCHAR)&value, (PUCHAR)&(pRaCfg->status), 2);
				value = ntohs(value);
				sprintf((PCHAR)str, "%d", value);
				Set_ATE_TX_MODE_Proc(pAdapter, str);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SET_PREAMBLE\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SET_PREAMBLE is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_SET_CHANNEL:
			{
				SHORT    value = 0;
				UCHAR    str[LEN_OF_ARG];

				NdisZeroMemory(str, LEN_OF_ARG);

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SET_CHANNEL\n"));

				memcpy((PUCHAR)&value, (PUCHAR)&(pRaCfg->status), 2);
				value = ntohs(value);
				sprintf((PCHAR)str, "%d", value);
				Set_ATE_CHANNEL_Proc(pAdapter, str);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SET_CHANNEL\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SET_CHANNEL is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_SET_ADDR1:
			{
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SET_ADDR1\n"));

				// Addr is an array of UCHAR,
				// so no need to perform endian swap.
				memcpy(pAdapter->ate.Addr1, (PUCHAR)(pRaCfg->data - 2), MAC_ADDR_LEN);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SET_ADDR1\n"));
                    Status = -EFAULT;
            	}
				else
				{
					ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SET_ADDR1 is done !\n (ADDR1 = %2X:%2X:%2X:%2X:%2X:%2X)\n", pAdapter->ate.Addr1[0],
						pAdapter->ate.Addr1[1], pAdapter->ate.Addr1[2], pAdapter->ate.Addr1[3], pAdapter->ate.Addr1[4], pAdapter->ate.Addr1[5]));
				}
			}
			break;

		case RACFG_CMD_ATE_SET_ADDR2:
			{
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SET_ADDR2\n"));

				// Addr is an array of UCHAR,
				// so no need to perform endian swap.
				memcpy(pAdapter->ate.Addr2, (PUCHAR)(pRaCfg->data - 2), MAC_ADDR_LEN);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SET_ADDR2\n"));
                    Status = -EFAULT;
            	}
				else
				{
					ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SET_ADDR2 is done !\n (ADDR2 = %2X:%2X:%2X:%2X:%2X:%2X)\n", pAdapter->ate.Addr2[0],
						pAdapter->ate.Addr2[1], pAdapter->ate.Addr2[2], pAdapter->ate.Addr2[3], pAdapter->ate.Addr2[4], pAdapter->ate.Addr2[5]));
				}
			}
			break;

		case RACFG_CMD_ATE_SET_ADDR3:
			{
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SET_ADDR3\n"));

				// Addr is an array of UCHAR,
				// so no need to perform endian swap.
				memcpy(pAdapter->ate.Addr3, (PUCHAR)(pRaCfg->data - 2), MAC_ADDR_LEN);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SET_ADDR3\n"));
                    Status = -EFAULT;
            	}
				else
				{
					ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SET_ADDR3 is done !\n (ADDR3 = %2X:%2X:%2X:%2X:%2X:%2X)\n", pAdapter->ate.Addr3[0],
						pAdapter->ate.Addr3[1], pAdapter->ate.Addr3[2], pAdapter->ate.Addr3[3], pAdapter->ate.Addr3[4], pAdapter->ate.Addr3[5]));
				}
			}
			break;

		case RACFG_CMD_ATE_SET_RATE:
			{
				SHORT    value = 0;
				UCHAR    str[LEN_OF_ARG];

				NdisZeroMemory(str, LEN_OF_ARG);

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SET_RATE\n"));

				memcpy((PUCHAR)&value, (PUCHAR)&(pRaCfg->status), 2);
				value = ntohs(value);
				sprintf((PCHAR)str, "%d", value);
				Set_ATE_TX_MCS_Proc(pAdapter, str);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SET_RATE\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SET_RATE is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_SET_TX_FRAME_LEN:
			{
				SHORT    value = 0;
				UCHAR    str[LEN_OF_ARG];

				NdisZeroMemory(str, LEN_OF_ARG);

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SET_TX_FRAME_LEN\n"));

				memcpy((PUCHAR)&value, (PUCHAR)&(pRaCfg->status), 2);
				value = ntohs(value);
				sprintf((PCHAR)str, "%d", value);
				Set_ATE_TX_LENGTH_Proc(pAdapter, str);

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SET_TX_FRAME_LEN\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SET_TX_FRAME_LEN is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_SET_TX_FRAME_COUNT:
			{
				USHORT    value = 0;
				UCHAR    str[LEN_OF_ARG];

				NdisZeroMemory(str, LEN_OF_ARG);

				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_ATE_SET_TX_FRAME_COUNT\n"));

				memcpy((PUCHAR)&value, (PUCHAR)&(pRaCfg->status), 2);
				value = ntohs(value);
				/* TX_FRAME_COUNT == 0 means tx infinitely */
				if (value == 0)
				{
					/* Use TxCount = 0xFFFFFFFF to approximate the infinity. */
					pAdapter->ate.TxCount = 0xFFFFFFFF;
					ATEDBGPRINT(RT_DEBUG_TRACE, ("Set_ATE_TX_COUNT_Proc (TxCount = %d)\n", pAdapter->ate.TxCount));
					ATEDBGPRINT(RT_DEBUG_TRACE, ("Ralink: Set_ATE_TX_COUNT_Proc Success\n"));


				}
				else
				{
					sprintf((PCHAR)str, "%d", value);
					Set_ATE_TX_COUNT_Proc(pAdapter, str);
				}

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_ATE_SET_TX_FRAME_COUNT\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_ATE_SET_TX_FRAME_COUNT is done !\n"));
				}
			}
			break;

		case RACFG_CMD_ATE_START_RX_FRAME:
			{
				ATEDBGPRINT(RT_DEBUG_TRACE,("RACFG_CMD_RX_START\n"));

				Set_ATE_Proc(pAdapter, "RXFRAME");

				// prepare feedback
				pRaCfg->length = htons(2);
				pRaCfg->status = htons(0);
				wrq->u.data.length = sizeof(pRaCfg->magic_no) + sizeof(pRaCfg->command_type)
									+ sizeof(pRaCfg->command_id) + sizeof(pRaCfg->length)
									+ sizeof(pRaCfg->sequence) + ntohs(pRaCfg->length);

            	if (copy_to_user(wrq->u.data.pointer, pRaCfg, wrq->u.data.length))
            	{
            		ATEDBGPRINT(RT_DEBUG_ERROR, ("copy_to_user() fail in case RACFG_CMD_RX_START\n"));
                    Status = -EFAULT;
            	}
				else
				{
                	ATEDBGPRINT(RT_DEBUG_TRACE, ("RACFG_CMD_RX_START is done !\n"));
				}
			}
			break;
		default:
			break;
	}
    ASSERT(pRaCfg != NULL);
    if (pRaCfg != NULL)
    {
    kfree(pRaCfg);
    }
	return;
}

VOID BubbleSort(INT32 n, INT32 a[])
{
	INT32 k, j, temp;

	for (k = n-1;  k>0;  k--)
	{
		for (j = 0; j<k; j++)
		{
			if(a[j] > a[j+1])
			{
				temp = a[j];
				a[j]=a[j+1];
				a[j+1]=temp;
			}
		}
	}
}

VOID CalNoiseLevel(PRTMP_ADAPTER pAd, UCHAR channel, INT32 RSSI[3][10])
{
	INT32		RSSI0, RSSI1, RSSI2;
 	CHAR		Rssi0Offset, Rssi1Offset, Rssi2Offset;
	UCHAR		BbpR50Rssi0 = 0, BbpR51Rssi1 = 0, BbpR52Rssi2 = 0;
	UCHAR		Org_BBP66value = 0, Org_BBP69value = 0, Org_BBP70value = 0, data = 0;
	USHORT		LNA_Gain = 0;
	INT32       j = 0;
	UCHAR		Org_Channel = pAd->ate.Channel;
	USHORT	    GainValue = 0, OffsetValue = 0;

	ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R66, &Org_BBP66value);
	ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R69, &Org_BBP69value);
	ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R70, &Org_BBP70value);

	//**********************************************************************
	// Read the value of LNA gain and Rssi offset
	//**********************************************************************
	RT28xx_EEPROM_READ16(pAd, EEPROM_LNA_OFFSET, GainValue);

	// for Noise Level
	if (channel <= 14)
	{
		LNA_Gain = GainValue & 0x00FF;

		RT28xx_EEPROM_READ16(pAd, EEPROM_RSSI_BG_OFFSET, OffsetValue);
		Rssi0Offset = OffsetValue & 0x00FF;
		Rssi1Offset = (OffsetValue & 0xFF00) >> 8;
		RT28xx_EEPROM_READ16(pAd, (EEPROM_RSSI_BG_OFFSET + 2)/* 0x48 */, OffsetValue);
		Rssi2Offset = OffsetValue & 0x00FF;
	}
	else
	{
		LNA_Gain = (GainValue & 0xFF00) >> 8;

		RT28xx_EEPROM_READ16(pAd, EEPROM_RSSI_A_OFFSET, OffsetValue);
		Rssi0Offset = OffsetValue & 0x00FF;
		Rssi1Offset = (OffsetValue & 0xFF00) >> 8;
		RT28xx_EEPROM_READ16(pAd, (EEPROM_RSSI_A_OFFSET + 2)/* 0x4C */, OffsetValue);
		Rssi2Offset = OffsetValue & 0x00FF;
	}
	//**********************************************************************
	{
		pAd->ate.Channel = channel;
		ATEAsicSwitchChannel(pAd);
		mdelay(5);

		data = 0x10;
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, data);
		data = 0x40;
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, data);
		data = 0x40;
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, data);
		mdelay(5);

		// Start Rx
		pAd->ate.bQARxStart = TRUE;
		Set_ATE_Proc(pAd, "RXFRAME");

		mdelay(5);

		for (j = 0; j < 10; j++)
		{
			ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R50, &BbpR50Rssi0);
			ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R51, &BbpR51Rssi1);
			ATE_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R52, &BbpR52Rssi2);

			mdelay(10);

			// Calculate RSSI 0
			if (BbpR50Rssi0 == 0)
			{
				RSSI0 = -100;
			}
			else
			{
				RSSI0 = (INT32)(-12 - BbpR50Rssi0 - LNA_Gain - Rssi0Offset);
			}
			RSSI[0][j] = RSSI0;

			if ( pAd->Antenna.field.RxPath >= 2 ) // 2R
			{
				// Calculate RSSI 1
				if (BbpR51Rssi1 == 0)
				{
					RSSI1 = -100;
				}
				else
				{
					RSSI1 = (INT32)(-12 - BbpR51Rssi1 - LNA_Gain - Rssi1Offset);
				}
				RSSI[1][j] = RSSI1;
			}

			if ( pAd->Antenna.field.RxPath >= 3 ) // 3R
			{
				// Calculate RSSI 2
				if (BbpR52Rssi2 == 0)
					RSSI2 = -100;
				else
					RSSI2 = (INT32)(-12 - BbpR52Rssi2 - LNA_Gain - Rssi2Offset);

				RSSI[2][j] = RSSI2;
			}
		}

		// Stop Rx
		Set_ATE_Proc(pAd, "RXSTOP");

		mdelay(5);

#if 0// Debug Message................
		ate_print("\n**********************************************************\n");
		ate_print("Noise Level: Channel %d\n", channel);
		ate_print("RSSI0 = %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
			RSSI[0][0], RSSI[0][1], RSSI[0][2],
			RSSI[0][3], RSSI[0][4], RSSI[0][5],
			RSSI[0][6], RSSI[0][7], RSSI[0][8],
			RSSI[0][9]);
		if ( pAd->Antenna.field.RxPath >= 2 ) // 2R
		{
			ate_print("RSSI1 = %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
				RSSI[1][0], RSSI[1][1], RSSI[1][2],
				RSSI[1][3], RSSI[1][4], RSSI[1][5],
				RSSI[1][6], RSSI[1][7], RSSI[1][8],
				RSSI[1][9]);
		}
		if ( pAd->Antenna.field.RxPath >= 3 ) // 3R
		{
			ate_print("RSSI2 = %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
				RSSI[2][0], RSSI[2][1], RSSI[2][2],
				RSSI[2][3], RSSI[2][4], RSSI[2][5],
				RSSI[2][6], RSSI[2][7], RSSI[2][8],
				RSSI[2][9]);
		}
#endif // 0 //
		BubbleSort(10, RSSI[0]);	// 1R

		if ( pAd->Antenna.field.RxPath >= 2 ) // 2R
		{
			BubbleSort(10, RSSI[1]);
		}

		if ( pAd->Antenna.field.RxPath >= 3 ) // 3R
		{
			BubbleSort(10, RSSI[2]);
		}

#if 0// Debug Message................
		ate_print("\nAfter Sorting....Channel %d\n", channel);
		ate_print("RSSI0 = %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
			RSSI[0][0], RSSI[0][1], RSSI[0][2],
			RSSI[0][3], RSSI[0][4], RSSI[0][5],
			RSSI[0][6], RSSI[0][7], RSSI[0][8],
			RSSI[0][9]);
		if ( pAd->Antenna.field.RxPath >= 2 ) // 2R
		{
			ate_print("RSSI1 = %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
				RSSI[1][0], RSSI[1][1], RSSI[1][2],
				RSSI[1][3], RSSI[1][4], RSSI[1][5],
				RSSI[1][6], RSSI[1][7], RSSI[1][8],
				RSSI[1][9]);
		}
		if ( pAd->Antenna.field.RxPath >= 3 ) // 3R
		{
			ate_print("RSSI2 = %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
				RSSI[2][0], RSSI[2][1], RSSI[2][2],
				RSSI[2][3], RSSI[2][4], RSSI[2][5],
				RSSI[2][6], RSSI[2][7], RSSI[2][8],
				RSSI[2][9]);
		}
		ate_print("**********************************************************\n");
#endif // 0 //
	}

	pAd->ate.Channel = Org_Channel;
	ATEAsicSwitchChannel(pAd);

	// Restore original value
    ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, Org_BBP66value);
    ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, Org_BBP69value);
    ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, Org_BBP70value);

	return;
}

BOOLEAN SyncTxRxConfig(PRTMP_ADAPTER pAd, USHORT offset, UCHAR value)
{
	UCHAR tmp = 0, bbp_data = 0;

	if (ATE_ON(pAd))
	{
		ATE_BBP_IO_READ8_BY_REG_ID(pAd, offset, &bbp_data);
	}
	else
	{
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, offset, &bbp_data);
	}

	/* confirm again */
	ASSERT(bbp_data == value);

	switch(offset)
	{
		case BBP_R1:
			/* Need to sync. tx configuration with legacy ATE. */
			tmp = (bbp_data & ((1 << 4) | (1 << 3))/* 0x18 */) >> 3;
		    switch(tmp)
		    {
				/* The BBP R1 bit[4:3] = 2 :: Both DACs will be used by QA. */
		        case 2:
					/* All */
					pAd->ate.TxAntennaSel = 0;
		            break;
				/* The BBP R1 bit[4:3] = 0 :: DAC 0 will be used by QA. */
		        case 0:
					/* Antenna one */
					pAd->ate.TxAntennaSel = 1;
		            break;
				/* The BBP R1 bit[4:3] = 1 :: DAC 1 will be used by QA. */
		        case 1:
					/* Antenna two */
					pAd->ate.TxAntennaSel = 2;
		            break;
		        default:
		            DBGPRINT(RT_DEBUG_TRACE, ("%s -- Sth. wrong!  : return FALSE; \n", __func__));
		            return FALSE;
		    }
			break;/* case BBP_R1 */

		case BBP_R3:
			/* Need to sync. rx configuration with legacy ATE. */
			tmp = (bbp_data & ((1 << 1) | (1 << 0))/* 0x03 */);
		    switch(tmp)
		    {
				/* The BBP R3 bit[1:0] = 3 :: All ADCs will be used by QA. */
		        case 3:
					/* All */
					pAd->ate.RxAntennaSel = 0;
		            break;
				/* The BBP R3 bit[1:0] = 0 :: ADC 0 will be used by QA, */
				/* unless the BBP R3 bit[4:3] = 2 */
		        case 0:
					/* Antenna one */
					pAd->ate.RxAntennaSel = 1;
					tmp = ((bbp_data & ((1 << 4) | (1 << 3))/* 0x03 */) >> 3);
					if (tmp == 2)// 3R
					{
						/* Default : All ADCs will be used by QA */
						pAd->ate.RxAntennaSel = 0;
					}
		            break;
				/* The BBP R3 bit[1:0] = 1 :: ADC 1 will be used by QA. */
		        case 1:
					/* Antenna two */
					pAd->ate.RxAntennaSel = 2;
		            break;
				/* The BBP R3 bit[1:0] = 2 :: ADC 2 will be used by QA. */
		        case 2:
					/* Antenna three */
					pAd->ate.RxAntennaSel = 3;
		            break;
		        default:
		            DBGPRINT(RT_DEBUG_ERROR, ("%s -- Impossible!  : return FALSE; \n", __func__));
		            return FALSE;
		    }
			break;/* case BBP_R3 */

        default:
            DBGPRINT(RT_DEBUG_ERROR, ("%s -- Sth. wrong!  : return FALSE; \n", __func__));
            return FALSE;

	}
	return TRUE;
}

static VOID memcpy_exl(PRTMP_ADAPTER pAd, UCHAR *dst, UCHAR *src, ULONG len)
{
	ULONG i, Value = 0;
	ULONG *pDst, *pSrc;
	UCHAR *p8;

	p8 = src;
	pDst = (ULONG *) dst;
	pSrc = (ULONG *) src;

	for (i = 0 ; i < (len/4); i++)
	{
		/* For alignment issue, we need a variable "Value". */
		memmove(&Value, pSrc, 4);
		Value = htonl(Value);
		memmove(pDst, &Value, 4);
		pDst++;
		pSrc++;
	}
	if ((len % 4) != 0)
	{
		/* wish that it will never reach here */
		memmove(&Value, pSrc, (len % 4));
		Value = htonl(Value);
		memmove(pDst, &Value, (len % 4));
	}
}

static VOID memcpy_exs(PRTMP_ADAPTER pAd, UCHAR *dst, UCHAR *src, ULONG len)
{
	ULONG i;
	UCHAR *pDst, *pSrc;

	pDst = dst;
	pSrc = src;

	for (i = 0; i < (len/2); i++)
	{
		memmove(pDst, pSrc, 2);
		*((USHORT *)pDst) = htons(*((USHORT *)pDst));
		pDst+=2;
		pSrc+=2;
	}

	if ((len % 2) != 0)
	{
		memmove(pDst, pSrc, 1);
	}
}

static VOID RTMP_IO_READ_BULK(PRTMP_ADAPTER pAd, UCHAR *dst, UCHAR *src, UINT32 len)
{
	UINT32 i, Value;
	UINT32 *pDst, *pSrc;

	pDst = (UINT32 *) dst;
	pSrc = (UINT32 *) src;

	for (i = 0 ; i < (len/4); i++)
	{
		RTMP_IO_READ32(pAd, (ULONG)pSrc, &Value);
		Value = htonl(Value);
		memmove(pDst, &Value, 4);
		pDst++;
		pSrc++;
	}
	return;
}

// TODO:
#if 0
/* These work only when RALINK_ATE is defined */
INT Set_TxStart_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	ULONG value = simple_strtol(arg, 0, 10);
	UCHAR buffer[26] = {0x88, 0x02, 0x2c, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x00, 0x55, 0x44, 0x33, 0x22, 0x11, 0xc0, 0x22, 0x00, 0x00};
	POS_COOKIE pObj;

	if (pAd->ate.TxStatus != 0)
		return FALSE;

	pAd->ate.TxInfo = 0x04000000;
	bzero(&pAd->ate.TxWI, sizeof(TXWI_STRUC));
	pAd->ate.TxWI.PHYMODE = 0;// MODE_CCK
	pAd->ate.TxWI.MPDUtotalByteCount = 1226;
	pAd->ate.TxWI.MCS = 3;
	//pAd->ate.Mode = ATE_START;
	pAd->ate.Mode |= ATE_TXFRAME;
	pAd->ate.TxCount = value;
	pAd->ate.QID = 0;
	pAd->ate.HLen = 26;
	pAd->ate.PLen = 0;
	pAd->ate.DLen = 1200;
	memcpy(pAd->ate.Header, buffer, 26);
	pAd->ate.bQATxStart = TRUE;
	//pObj = (POS_COOKIE) pAd->OS_Cookie;
	//tasklet_hi_schedule(&pObj->AteTxTask);
	return TRUE;
}
#endif  /* end of #if 0 */

INT Set_TxStop_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	ATEDBGPRINT(RT_DEBUG_TRACE,("Set_TxStop_Proc\n"));

	if (Set_ATE_Proc(pAd, "TXSTOP"))
	{
	return TRUE;
}
	else
	{
		return FALSE;
	}
}

INT Set_RxStop_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	ATEDBGPRINT(RT_DEBUG_TRACE,("Set_RxStop_Proc\n"));

	if (Set_ATE_Proc(pAd, "RXSTOP"))
	{
	return TRUE;
}
	else
	{
		return FALSE;
	}
}

#if 0
INT Set_EEWrite_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	USHORT offset = 0, value;
	PUCHAR p2 = arg;

	while((*p2 != ':') && (*p2 != '\0'))
	{
		p2++;
	}

	if (*p2 == ':')
	{
		A2Hex(offset, arg);
		A2Hex(value, p2+ 1);
	}
	else
	{
		A2Hex(value, arg);
	}

	if (offset >= EEPROM_SIZE)
	{
		ate_print("Offset can not exceed EEPROM_SIZE( == 0x%04x)\n", EEPROM_SIZE);
		return FALSE;
	}

	RTMP_EEPROM_WRITE16(pAd, offset, value);

	return TRUE;
}

INT Set_BBPRead_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	UCHAR value = 0, offset;

	A2Hex(offset, arg);

	if (ATE_ON(pAd))
	{
		ATE_BBP_IO_READ8_BY_REG_ID(pAd, offset,  &value);
	}
	else
	{
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, offset,  &value);
	}

	ate_print("%x\n", value);

	return TRUE;
}


INT Set_BBPWrite_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	USHORT offset = 0;
	PUCHAR p2 = arg;
	UCHAR value;

	while((*p2 != ':') && (*p2 != '\0'))
	{
		p2++;
	}

	if (*p2 == ':')
	{
		A2Hex(offset, arg);
		A2Hex(value, p2+ 1);
	}
	else
	{
		A2Hex(value, arg);
	}

	if (ATE_ON(pAd))
	{
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, offset,  value);
	}
	else
	{
		RTNP_BBP_IO_WRITE8_BY_REG_ID(pAd, offset,  value);
	}

	return TRUE;
}

INT Set_RFWrite_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
{
	PUCHAR p2, p3, p4;
	ULONG R1, R2, R3, R4;

	p2 = arg;

	while((*p2 != ':') && (*p2 != '\0'))
	{
		p2++;
	}

	if (*p2 != ':')
		return FALSE;

	p3 = p2 + 1;

	while((*p3 != ':') && (*p3 != '\0'))
	{
		p3++;
	}

	if (*p3 != ':')
		return FALSE;

	p4 = p3 + 1;

	while((*p4 != ':') && (*p4 != '\0'))
	{
		p4++;
	}

	if (*p4 != ':')
		return FALSE;


	A2Hex(R1, arg);
	A2Hex(R2, p2 + 1);
	A2Hex(R3, p3 + 1);
	A2Hex(R4, p4 + 1);

	RTMP_RF_IO_WRITE32(pAd, R1);
	RTMP_RF_IO_WRITE32(pAd, R2);
	RTMP_RF_IO_WRITE32(pAd, R3);
	RTMP_RF_IO_WRITE32(pAd, R4);

	return TRUE;
}
#endif  // end of #if 0 //
#endif	// RALINK_28xx_QA //

#endif	// RALINK_ATE //

