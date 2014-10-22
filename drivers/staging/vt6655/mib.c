/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: mib.c
 *
 * Purpose: Implement MIB Data Structure
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 * Functions:
 *      STAvClearAllCounter - Clear All MIB Counter
 *      STAvUpdateIstStatCounter - Update ISR statistic counter
 *      STAvUpdateRDStatCounter - Update Rx statistic counter
 *      STAvUpdateRDStatCounterEx - Update Rx statistic counter and copy rcv data
 *      STAvUpdateTDStatCounter - Update Tx statistic counter
 *      STAvUpdateTDStatCounterEx - Update Tx statistic counter and copy tx data
 *      STAvUpdate802_11Counter - Update 802.11 mib counter
 *
 * Revision History:
 *
 */

#include "upc.h"
#include "mac.h"
#include "tether.h"
#include "mib.h"
#include "wctl.h"
#include "baseband.h"

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

/*
 * Description: Clear All Statistic Counter
 *
 * Parameters:
 *  In:
 *      pStatistic  - Pointer to Statistic Counter Data Structure
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void STAvClearAllCounter(PSStatCounter pStatistic)
{
	// set memory to zero
	memset(pStatistic, 0, sizeof(SStatCounter));
}

/*
 * Description: Update Isr Statistic Counter
 *
 * Parameters:
 *  In:
 *      pStatistic  - Pointer to Statistic Counter Data Structure
 *      wisr        - Interrupt status
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void STAvUpdateIsrStatCounter(PSStatCounter pStatistic, unsigned long dwIsr)
{
	/**********************/
	/* ABNORMAL interrupt */
	/**********************/
	// not any IMR bit invoke irq

	if (dwIsr == 0) {
		pStatistic->ISRStat.dwIsrUnknown++;
		return;
	}

//Added by Kyle
	if (dwIsr & ISR_TXDMA0)               // ISR, bit0
		pStatistic->ISRStat.dwIsrTx0OK++;             // TXDMA0 successful

	if (dwIsr & ISR_AC0DMA)               // ISR, bit1
		pStatistic->ISRStat.dwIsrAC0TxOK++;           // AC0DMA successful

	if (dwIsr & ISR_BNTX)                 // ISR, bit2
		pStatistic->ISRStat.dwIsrBeaconTxOK++;        // BeaconTx successful

	if (dwIsr & ISR_RXDMA0)               // ISR, bit3
		pStatistic->ISRStat.dwIsrRx0OK++;             // Rx0 successful

	if (dwIsr & ISR_TBTT)                 // ISR, bit4
		pStatistic->ISRStat.dwIsrTBTTInt++;           // TBTT successful

	if (dwIsr & ISR_SOFTTIMER)            // ISR, bit6
		pStatistic->ISRStat.dwIsrSTIMERInt++;

	if (dwIsr & ISR_WATCHDOG)             // ISR, bit7
		pStatistic->ISRStat.dwIsrWatchDog++;

	if (dwIsr & ISR_FETALERR)             // ISR, bit8
		pStatistic->ISRStat.dwIsrUnrecoverableError++;

	if (dwIsr & ISR_SOFTINT)              // ISR, bit9
		pStatistic->ISRStat.dwIsrSoftInterrupt++;     // software interrupt

	if (dwIsr & ISR_MIBNEARFULL)          // ISR, bit10
		pStatistic->ISRStat.dwIsrMIBNearfull++;

	if (dwIsr & ISR_RXNOBUF)              // ISR, bit11
		pStatistic->ISRStat.dwIsrRxNoBuf++;           // Rx No Buff

	if (dwIsr & ISR_RXDMA1)               // ISR, bit12
		pStatistic->ISRStat.dwIsrRx1OK++;             // Rx1 successful

	if (dwIsr & ISR_SOFTTIMER1)           // ISR, bit21
		pStatistic->ISRStat.dwIsrSTIMER1Int++;
}

/*
 * Description: Update Rx Statistic Counter
 *
 * Parameters:
 *  In:
 *      pStatistic      - Pointer to Statistic Counter Data Structure
 *      byRSR           - Rx Status
 *      byNewRSR        - Rx Status
 *      pbyBuffer       - Rx Buffer
 *      cbFrameLength   - Rx Length
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void STAvUpdateRDStatCounter(PSStatCounter pStatistic,
			     unsigned char byRSR, unsigned char byNewRSR, unsigned char byRxRate,
			     unsigned char *pbyBuffer, unsigned int cbFrameLength)
{
	//need change
	PS802_11Header pHeader = (PS802_11Header)pbyBuffer;

	if (byRSR & RSR_ADDROK)
		pStatistic->dwRsrADDROk++;
	if (byRSR & RSR_CRCOK) {
		pStatistic->dwRsrCRCOk++;

		pStatistic->ullRsrOK++;

		if (cbFrameLength >= ETH_ALEN) {
			// update counters in case of successful transmit
			if (byRSR & RSR_ADDRBROAD) {
				pStatistic->ullRxBroadcastFrames++;
				pStatistic->ullRxBroadcastBytes += (unsigned long long) cbFrameLength;
			} else if (byRSR & RSR_ADDRMULTI) {
				pStatistic->ullRxMulticastFrames++;
				pStatistic->ullRxMulticastBytes += (unsigned long long) cbFrameLength;
			} else {
				pStatistic->ullRxDirectedFrames++;
				pStatistic->ullRxDirectedBytes += (unsigned long long) cbFrameLength;
			}
		}
	}

	if (byRxRate == 22) {
		pStatistic->CustomStat.ullRsr11M++;
		if (byRSR & RSR_CRCOK)
			pStatistic->CustomStat.ullRsr11MCRCOk++;

		pr_debug("11M: ALL[%d], OK[%d]:[%02x]\n",
			 (int)pStatistic->CustomStat.ullRsr11M,
			 (int)pStatistic->CustomStat.ullRsr11MCRCOk, byRSR);
	} else if (byRxRate == 11) {
		pStatistic->CustomStat.ullRsr5M++;
		if (byRSR & RSR_CRCOK)
			pStatistic->CustomStat.ullRsr5MCRCOk++;

		pr_debug(" 5M: ALL[%d], OK[%d]:[%02x]\n",
			 (int)pStatistic->CustomStat.ullRsr5M,
			 (int)pStatistic->CustomStat.ullRsr5MCRCOk, byRSR);
	} else if (byRxRate == 4) {
		pStatistic->CustomStat.ullRsr2M++;
		if (byRSR & RSR_CRCOK)
			pStatistic->CustomStat.ullRsr2MCRCOk++;

		pr_debug(" 2M: ALL[%d], OK[%d]:[%02x]\n",
			 (int)pStatistic->CustomStat.ullRsr2M,
			 (int)pStatistic->CustomStat.ullRsr2MCRCOk, byRSR);
	} else if (byRxRate == 2) {
		pStatistic->CustomStat.ullRsr1M++;
		if (byRSR & RSR_CRCOK)
			pStatistic->CustomStat.ullRsr1MCRCOk++;

		pr_debug(" 1M: ALL[%d], OK[%d]:[%02x]\n",
			 (int)pStatistic->CustomStat.ullRsr1M,
			 (int)pStatistic->CustomStat.ullRsr1MCRCOk, byRSR);
	} else if (byRxRate == 12) {
		pStatistic->CustomStat.ullRsr6M++;
		if (byRSR & RSR_CRCOK)
			pStatistic->CustomStat.ullRsr6MCRCOk++;

		pr_debug(" 6M: ALL[%d], OK[%d]\n",
			 (int)pStatistic->CustomStat.ullRsr6M,
			 (int)pStatistic->CustomStat.ullRsr6MCRCOk);
	} else if (byRxRate == 18) {
		pStatistic->CustomStat.ullRsr9M++;
		if (byRSR & RSR_CRCOK)
			pStatistic->CustomStat.ullRsr9MCRCOk++;

		pr_debug(" 9M: ALL[%d], OK[%d]\n",
			 (int)pStatistic->CustomStat.ullRsr9M,
			 (int)pStatistic->CustomStat.ullRsr9MCRCOk);
	} else if (byRxRate == 24) {
		pStatistic->CustomStat.ullRsr12M++;
		if (byRSR & RSR_CRCOK)
			pStatistic->CustomStat.ullRsr12MCRCOk++;

		pr_debug("12M: ALL[%d], OK[%d]\n",
			 (int)pStatistic->CustomStat.ullRsr12M,
			 (int)pStatistic->CustomStat.ullRsr12MCRCOk);
	} else if (byRxRate == 36) {
		pStatistic->CustomStat.ullRsr18M++;
		if (byRSR & RSR_CRCOK)
			pStatistic->CustomStat.ullRsr18MCRCOk++;

		pr_debug("18M: ALL[%d], OK[%d]\n",
			 (int)pStatistic->CustomStat.ullRsr18M,
			 (int)pStatistic->CustomStat.ullRsr18MCRCOk);
	} else if (byRxRate == 48) {
		pStatistic->CustomStat.ullRsr24M++;
		if (byRSR & RSR_CRCOK)
			pStatistic->CustomStat.ullRsr24MCRCOk++;

		pr_debug("24M: ALL[%d], OK[%d]\n",
			 (int)pStatistic->CustomStat.ullRsr24M,
			 (int)pStatistic->CustomStat.ullRsr24MCRCOk);
	} else if (byRxRate == 72) {
		pStatistic->CustomStat.ullRsr36M++;
		if (byRSR & RSR_CRCOK)
			pStatistic->CustomStat.ullRsr36MCRCOk++;

		pr_debug("36M: ALL[%d], OK[%d]\n",
			 (int)pStatistic->CustomStat.ullRsr36M,
			 (int)pStatistic->CustomStat.ullRsr36MCRCOk);
	} else if (byRxRate == 96) {
		pStatistic->CustomStat.ullRsr48M++;
		if (byRSR & RSR_CRCOK)
			pStatistic->CustomStat.ullRsr48MCRCOk++;

		pr_debug("48M: ALL[%d], OK[%d]\n",
			 (int)pStatistic->CustomStat.ullRsr48M,
			 (int)pStatistic->CustomStat.ullRsr48MCRCOk);
	} else if (byRxRate == 108) {
		pStatistic->CustomStat.ullRsr54M++;
		if (byRSR & RSR_CRCOK)
			pStatistic->CustomStat.ullRsr54MCRCOk++;

		pr_debug("54M: ALL[%d], OK[%d]\n",
			 (int)pStatistic->CustomStat.ullRsr54M,
			 (int)pStatistic->CustomStat.ullRsr54MCRCOk);
	} else {
		pr_debug("Unknown: Total[%d], CRCOK[%d]\n",
			 (int)pStatistic->dwRsrRxPacket+1,
			 (int)pStatistic->dwRsrCRCOk);
	}

	if (byRSR & RSR_BSSIDOK)
		pStatistic->dwRsrBSSIDOk++;

	if (byRSR & RSR_BCNSSIDOK)
		pStatistic->dwRsrBCNSSIDOk++;
	if (byRSR & RSR_IVLDLEN)  //invalid len (> 2312 byte)
		pStatistic->dwRsrLENErr++;
	if (byRSR & RSR_IVLDTYP)  //invalid packet type
		pStatistic->dwRsrTYPErr++;
	if (byRSR & (RSR_IVLDTYP | RSR_IVLDLEN))
		pStatistic->dwRsrErr++;

	if (byNewRSR & NEWRSR_DECRYPTOK)
		pStatistic->dwNewRsrDECRYPTOK++;
	if (byNewRSR & NEWRSR_CFPIND)
		pStatistic->dwNewRsrCFP++;
	if (byNewRSR & NEWRSR_HWUTSF)
		pStatistic->dwNewRsrUTSF++;
	if (byNewRSR & NEWRSR_BCNHITAID)
		pStatistic->dwNewRsrHITAID++;
	if (byNewRSR & NEWRSR_BCNHITAID0)
		pStatistic->dwNewRsrHITAID0++;

	// increase rx packet count
	pStatistic->dwRsrRxPacket++;
	pStatistic->dwRsrRxOctet += cbFrameLength;

	if (IS_TYPE_DATA(pbyBuffer))
		pStatistic->dwRsrRxData++;
	else if (IS_TYPE_MGMT(pbyBuffer))
		pStatistic->dwRsrRxManage++;
	else if (IS_TYPE_CONTROL(pbyBuffer))
		pStatistic->dwRsrRxControl++;

	if (byRSR & RSR_ADDRBROAD)
		pStatistic->dwRsrBroadcast++;
	else if (byRSR & RSR_ADDRMULTI)
		pStatistic->dwRsrMulticast++;
	else
		pStatistic->dwRsrDirected++;

	if (WLAN_GET_FC_MOREFRAG(pHeader->wFrameCtl))
		pStatistic->dwRsrRxFragment++;

	if (cbFrameLength < ETH_ZLEN + 4)
		pStatistic->dwRsrRunt++;
	else if (cbFrameLength == ETH_ZLEN + 4)
		pStatistic->dwRsrRxFrmLen64++;
	else if ((65 <= cbFrameLength) && (cbFrameLength <= 127))
		pStatistic->dwRsrRxFrmLen65_127++;
	else if ((128 <= cbFrameLength) && (cbFrameLength <= 255))
		pStatistic->dwRsrRxFrmLen128_255++;
	else if ((256 <= cbFrameLength) && (cbFrameLength <= 511))
		pStatistic->dwRsrRxFrmLen256_511++;
	else if ((512 <= cbFrameLength) && (cbFrameLength <= 1023))
		pStatistic->dwRsrRxFrmLen512_1023++;
	else if ((1024 <= cbFrameLength) && (cbFrameLength <= ETH_FRAME_LEN + 4))
		pStatistic->dwRsrRxFrmLen1024_1518++;
	else if (cbFrameLength > ETH_FRAME_LEN + 4)
		pStatistic->dwRsrLong++;
}

/*
 * Description: Update Rx Statistic Counter and copy Rx buffer
 *
 * Parameters:
 *  In:
 *      pStatistic      - Pointer to Statistic Counter Data Structure
 *      byRSR           - Rx Status
 *      byNewRSR        - Rx Status
 *      pbyBuffer       - Rx Buffer
 *      cbFrameLength   - Rx Length
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */

void
STAvUpdateRDStatCounterEx(
	PSStatCounter   pStatistic,
	unsigned char byRSR,
	unsigned char byNewRSR,
	unsigned char byRxRate,
	unsigned char *pbyBuffer,
	unsigned int cbFrameLength
)
{
	STAvUpdateRDStatCounter(
		pStatistic,
		byRSR,
		byNewRSR,
		byRxRate,
		pbyBuffer,
		cbFrameLength
);

	// rx length
	pStatistic->dwCntRxFrmLength = cbFrameLength;
	// rx pattern, we just see 10 bytes for sample
	memcpy(pStatistic->abyCntRxPattern, (unsigned char *)pbyBuffer, 10);
}

/*
 * Description: Update Tx Statistic Counter
 *
 * Parameters:
 *  In:
 *      pStatistic      - Pointer to Statistic Counter Data Structure
 *      byTSR0          - Tx Status
 *      byTSR1          - Tx Status
 *      pbyBuffer       - Tx Buffer
 *      cbFrameLength   - Tx Length
 *      uIdx            - Index of Tx DMA
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void
STAvUpdateTDStatCounter(
	PSStatCounter   pStatistic,
	unsigned char byTSR0,
	unsigned char byTSR1,
	unsigned char *pbyBuffer,
	unsigned int cbFrameLength,
	unsigned int uIdx
)
{
	PWLAN_80211HDR_A4   pHeader;
	unsigned char *pbyDestAddr;
	unsigned char byTSR0_NCR = byTSR0 & TSR0_NCR;

	pHeader = (PWLAN_80211HDR_A4) pbyBuffer;
	if (WLAN_GET_FC_TODS(pHeader->wFrameCtl) == 0)
		pbyDestAddr = &(pHeader->abyAddr1[0]);
	else
		pbyDestAddr = &(pHeader->abyAddr3[0]);

	// increase tx packet count
	pStatistic->dwTsrTxPacket[uIdx]++;
	pStatistic->dwTsrTxOctet[uIdx] += cbFrameLength;

	if (byTSR0_NCR != 0) {
		pStatistic->dwTsrRetry[uIdx]++;
		pStatistic->dwTsrTotalRetry[uIdx] += byTSR0_NCR;

		if (byTSR0_NCR == 1)
			pStatistic->dwTsrOnceRetry[uIdx]++;
		else
			pStatistic->dwTsrMoreThanOnceRetry[uIdx]++;
	}

	if ((byTSR1&(TSR1_TERR|TSR1_RETRYTMO|TSR1_TMO|ACK_DATA)) == 0) {
		pStatistic->ullTsrOK[uIdx]++;
		pStatistic->CustomStat.ullTsrAllOK =
			(pStatistic->ullTsrOK[TYPE_AC0DMA] + pStatistic->ullTsrOK[TYPE_TXDMA0]);
		// update counters in case that successful transmit
		if (is_broadcast_ether_addr(pbyDestAddr)) {
			pStatistic->ullTxBroadcastFrames[uIdx]++;
			pStatistic->ullTxBroadcastBytes[uIdx] += (unsigned long long) cbFrameLength;
		} else if (is_multicast_ether_addr(pbyDestAddr)) {
			pStatistic->ullTxMulticastFrames[uIdx]++;
			pStatistic->ullTxMulticastBytes[uIdx] += (unsigned long long) cbFrameLength;
		} else {
			pStatistic->ullTxDirectedFrames[uIdx]++;
			pStatistic->ullTxDirectedBytes[uIdx] += (unsigned long long) cbFrameLength;
		}
	} else {
		if (byTSR1 & TSR1_TERR)
			pStatistic->dwTsrErr[uIdx]++;
		if (byTSR1 & TSR1_RETRYTMO)
			pStatistic->dwTsrRetryTimeout[uIdx]++;
		if (byTSR1 & TSR1_TMO)
			pStatistic->dwTsrTransmitTimeout[uIdx]++;
		if (byTSR1 & ACK_DATA)
			pStatistic->dwTsrACKData[uIdx]++;
	}

	if (is_broadcast_ether_addr(pbyDestAddr))
		pStatistic->dwTsrBroadcast[uIdx]++;
	else if (is_multicast_ether_addr(pbyDestAddr))
		pStatistic->dwTsrMulticast[uIdx]++;
	else
		pStatistic->dwTsrDirected[uIdx]++;
}

/*
 * Description: Update Tx Statistic Counter and copy Tx buffer
 *
 * Parameters:
 *  In:
 *      pStatistic      - Pointer to Statistic Counter Data Structure
 *      pbyBuffer       - Tx Buffer
 *      cbFrameLength   - Tx Length
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void
STAvUpdateTDStatCounterEx(
	PSStatCounter   pStatistic,
	unsigned char *pbyBuffer,
	unsigned long cbFrameLength
)
{
	unsigned int uPktLength;

	uPktLength = (unsigned int)cbFrameLength;

	// tx length
	pStatistic->dwCntTxBufLength = uPktLength;
	// tx pattern, we just see 16 bytes for sample
	memcpy(pStatistic->abyCntTxPattern, pbyBuffer, 16);
}

/*
 * Description: Update 802.11 mib counter
 *
 * Parameters:
 *  In:
 *      p802_11Counter  - Pointer to 802.11 mib counter
 *      pStatistic      - Pointer to Statistic Counter Data Structure
 *      dwCounter       - hardware counter for 802.11 mib
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void
STAvUpdate802_11Counter(
	PSDot11Counters         p802_11Counter,
	PSStatCounter           pStatistic,
	unsigned long dwCounter
)
{
	p802_11Counter->MulticastTransmittedFrameCount = (unsigned long long) (pStatistic->dwTsrBroadcast[TYPE_AC0DMA] +
									       pStatistic->dwTsrBroadcast[TYPE_TXDMA0] +
									       pStatistic->dwTsrMulticast[TYPE_AC0DMA] +
									       pStatistic->dwTsrMulticast[TYPE_TXDMA0]);
	p802_11Counter->FailedCount = (unsigned long long) (pStatistic->dwTsrErr[TYPE_AC0DMA] + pStatistic->dwTsrErr[TYPE_TXDMA0]);
	p802_11Counter->RetryCount = (unsigned long long) (pStatistic->dwTsrRetry[TYPE_AC0DMA] + pStatistic->dwTsrRetry[TYPE_TXDMA0]);
	p802_11Counter->MultipleRetryCount = (unsigned long long) (pStatistic->dwTsrMoreThanOnceRetry[TYPE_AC0DMA] +
								   pStatistic->dwTsrMoreThanOnceRetry[TYPE_TXDMA0]);
	p802_11Counter->RTSSuccessCount += (unsigned long long)  (dwCounter & 0x000000ff);
	p802_11Counter->RTSFailureCount += (unsigned long long) ((dwCounter & 0x0000ff00) >> 8);
	p802_11Counter->ACKFailureCount += (unsigned long long) ((dwCounter & 0x00ff0000) >> 16);
	p802_11Counter->FCSErrorCount +=   (unsigned long long) ((dwCounter & 0xff000000) >> 24);
	p802_11Counter->MulticastReceivedFrameCount = (unsigned long long) (pStatistic->dwRsrBroadcast +
									    pStatistic->dwRsrMulticast);
}

/*
 * Description: Clear 802.11 mib counter
 *
 * Parameters:
 *  In:
 *      p802_11Counter  - Pointer to 802.11 mib counter
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void
STAvClear802_11Counter(PSDot11Counters p802_11Counter)
{
	// set memory to zero
	memset(p802_11Counter, 0, sizeof(SDot11Counters));
}
