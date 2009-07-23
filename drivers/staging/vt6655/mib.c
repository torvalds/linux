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


#if !defined(__UPC_H__)
#include "upc.h"
#endif
#if !defined(__MAC_H__)
#include "mac.h"
#endif
#if !defined(__TBIT_H__)
#include "tbit.h"
#endif
#if !defined(__TETHER_H__)
#include "tether.h"
#endif
#if !defined(__MIB_H__)
#include "mib.h"
#endif
#if !defined(__WCTL_H__)
#include "wctl.h"
#endif
#if !defined(__UMEM_H__)
#include "umem.h"
#endif
#if !defined(__BASEBAND_H__)
#include "baseband.h"
#endif

/*---------------------  Static Definitions -------------------------*/
static int          msglevel                =MSG_LEVEL_INFO;
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
void STAvClearAllCounter (PSStatCounter pStatistic)
{
    // set memory to zero
    ZERO_MEMORY(pStatistic, sizeof(SStatCounter));
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
void STAvUpdateIsrStatCounter (PSStatCounter pStatistic, DWORD dwIsr)
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
    if (BITbIsBitOn(dwIsr, ISR_TXDMA0))               // ISR, bit0
        pStatistic->ISRStat.dwIsrTx0OK++;             // TXDMA0 successful

    if (BITbIsBitOn(dwIsr, ISR_AC0DMA))               // ISR, bit1
        pStatistic->ISRStat.dwIsrAC0TxOK++;           // AC0DMA successful

    if (BITbIsBitOn(dwIsr, ISR_BNTX))                 // ISR, bit2
        pStatistic->ISRStat.dwIsrBeaconTxOK++;        // BeaconTx successful

    if (BITbIsBitOn(dwIsr, ISR_RXDMA0))               // ISR, bit3
        pStatistic->ISRStat.dwIsrRx0OK++;             // Rx0 successful

    if (BITbIsBitOn(dwIsr, ISR_TBTT))                 // ISR, bit4
        pStatistic->ISRStat.dwIsrTBTTInt++;           // TBTT successful

    if (BITbIsBitOn(dwIsr, ISR_SOFTTIMER))            // ISR, bit6
        pStatistic->ISRStat.dwIsrSTIMERInt++;

    if (BITbIsBitOn(dwIsr, ISR_WATCHDOG))             // ISR, bit7
        pStatistic->ISRStat.dwIsrWatchDog++;

    if (BITbIsBitOn(dwIsr, ISR_FETALERR))             // ISR, bit8
        pStatistic->ISRStat.dwIsrUnrecoverableError++;

    if (BITbIsBitOn(dwIsr, ISR_SOFTINT))              // ISR, bit9
        pStatistic->ISRStat.dwIsrSoftInterrupt++;     // software interrupt

    if (BITbIsBitOn(dwIsr, ISR_MIBNEARFULL))          // ISR, bit10
        pStatistic->ISRStat.dwIsrMIBNearfull++;

    if (BITbIsBitOn(dwIsr, ISR_RXNOBUF))              // ISR, bit11
        pStatistic->ISRStat.dwIsrRxNoBuf++;           // Rx No Buff

    if (BITbIsBitOn(dwIsr, ISR_RXDMA1))               // ISR, bit12
        pStatistic->ISRStat.dwIsrRx1OK++;             // Rx1 successful

//    if (BITbIsBitOn(dwIsr, ISR_ATIMTX))               // ISR, bit13
//        pStatistic->ISRStat.dwIsrATIMTxOK++;          // ATIMTX successful

//    if (BITbIsBitOn(dwIsr, ISR_SYNCTX))               // ISR, bit14
//        pStatistic->ISRStat.dwIsrSYNCTxOK++;          // SYNCTX successful

//    if (BITbIsBitOn(dwIsr, ISR_CFPEND))               // ISR, bit18
//        pStatistic->ISRStat.dwIsrCFPEnd++;

//    if (BITbIsBitOn(dwIsr, ISR_ATIMEND))              // ISR, bit19
//        pStatistic->ISRStat.dwIsrATIMEnd++;

//    if (BITbIsBitOn(dwIsr, ISR_SYNCFLUSHOK))          // ISR, bit20
//        pStatistic->ISRStat.dwIsrSYNCFlushOK++;

    if (BITbIsBitOn(dwIsr, ISR_SOFTTIMER1))           // ISR, bit21
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
void STAvUpdateRDStatCounter (PSStatCounter pStatistic,
                              BYTE byRSR, BYTE byNewRSR, BYTE byRxRate,
                              PBYTE pbyBuffer, UINT cbFrameLength)
{
    //need change
    PS802_11Header pHeader = (PS802_11Header)pbyBuffer;

    if (BITbIsBitOn(byRSR, RSR_ADDROK))
        pStatistic->dwRsrADDROk++;
    if (BITbIsBitOn(byRSR, RSR_CRCOK)) {
        pStatistic->dwRsrCRCOk++;

        pStatistic->ullRsrOK++;

        if (cbFrameLength >= U_ETHER_ADDR_LEN) {
            // update counters in case that successful transmit
            if (BITbIsBitOn(byRSR, RSR_ADDRBROAD)) {
                pStatistic->ullRxBroadcastFrames++;
                pStatistic->ullRxBroadcastBytes += (ULONGLONG)cbFrameLength;
            }
            else if (BITbIsBitOn(byRSR, RSR_ADDRMULTI)) {
                pStatistic->ullRxMulticastFrames++;
                pStatistic->ullRxMulticastBytes += (ULONGLONG)cbFrameLength;
            }
            else {
                pStatistic->ullRxDirectedFrames++;
                pStatistic->ullRxDirectedBytes += (ULONGLONG)cbFrameLength;
            }
        }
    }

    if(byRxRate==22) {
        pStatistic->CustomStat.ullRsr11M++;
        if(BITbIsBitOn(byRSR, RSR_CRCOK)) {
            pStatistic->CustomStat.ullRsr11MCRCOk++;
        }
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO"11M: ALL[%d], OK[%d]:[%02x]\n", (INT)pStatistic->CustomStat.ullRsr11M, (INT)pStatistic->CustomStat.ullRsr11MCRCOk, byRSR);
    }
    else if(byRxRate==11) {
        pStatistic->CustomStat.ullRsr5M++;
        if(BITbIsBitOn(byRSR, RSR_CRCOK)) {
            pStatistic->CustomStat.ullRsr5MCRCOk++;
        }
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO" 5M: ALL[%d], OK[%d]:[%02x]\n", (INT)pStatistic->CustomStat.ullRsr5M, (INT)pStatistic->CustomStat.ullRsr5MCRCOk, byRSR);
    }
    else if(byRxRate==4) {
        pStatistic->CustomStat.ullRsr2M++;
        if(BITbIsBitOn(byRSR, RSR_CRCOK)) {
            pStatistic->CustomStat.ullRsr2MCRCOk++;
        }
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO" 2M: ALL[%d], OK[%d]:[%02x]\n", (INT)pStatistic->CustomStat.ullRsr2M, (INT)pStatistic->CustomStat.ullRsr2MCRCOk, byRSR);
    }
    else if(byRxRate==2){
        pStatistic->CustomStat.ullRsr1M++;
        if(BITbIsBitOn(byRSR, RSR_CRCOK)) {
            pStatistic->CustomStat.ullRsr1MCRCOk++;
        }
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO" 1M: ALL[%d], OK[%d]:[%02x]\n", (INT)pStatistic->CustomStat.ullRsr1M, (INT)pStatistic->CustomStat.ullRsr1MCRCOk, byRSR);
    }
    else if(byRxRate==12){
        pStatistic->CustomStat.ullRsr6M++;
        if(BITbIsBitOn(byRSR, RSR_CRCOK)) {
            pStatistic->CustomStat.ullRsr6MCRCOk++;
        }
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO" 6M: ALL[%d], OK[%d]\n", (INT)pStatistic->CustomStat.ullRsr6M, (INT)pStatistic->CustomStat.ullRsr6MCRCOk);
    }
    else if(byRxRate==18){
        pStatistic->CustomStat.ullRsr9M++;
        if(BITbIsBitOn(byRSR, RSR_CRCOK)) {
            pStatistic->CustomStat.ullRsr9MCRCOk++;
        }
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO" 9M: ALL[%d], OK[%d]\n", (INT)pStatistic->CustomStat.ullRsr9M, (INT)pStatistic->CustomStat.ullRsr9MCRCOk);
    }
    else if(byRxRate==24){
        pStatistic->CustomStat.ullRsr12M++;
        if(BITbIsBitOn(byRSR, RSR_CRCOK)) {
            pStatistic->CustomStat.ullRsr12MCRCOk++;
        }
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO"12M: ALL[%d], OK[%d]\n", (INT)pStatistic->CustomStat.ullRsr12M, (INT)pStatistic->CustomStat.ullRsr12MCRCOk);
    }
    else if(byRxRate==36){
        pStatistic->CustomStat.ullRsr18M++;
        if(BITbIsBitOn(byRSR, RSR_CRCOK)) {
            pStatistic->CustomStat.ullRsr18MCRCOk++;
        }
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO"18M: ALL[%d], OK[%d]\n", (INT)pStatistic->CustomStat.ullRsr18M, (INT)pStatistic->CustomStat.ullRsr18MCRCOk);
    }
    else if(byRxRate==48){
        pStatistic->CustomStat.ullRsr24M++;
        if(BITbIsBitOn(byRSR, RSR_CRCOK)) {
            pStatistic->CustomStat.ullRsr24MCRCOk++;
        }
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO"24M: ALL[%d], OK[%d]\n", (INT)pStatistic->CustomStat.ullRsr24M, (INT)pStatistic->CustomStat.ullRsr24MCRCOk);
    }
    else if(byRxRate==72){
        pStatistic->CustomStat.ullRsr36M++;
        if(BITbIsBitOn(byRSR, RSR_CRCOK)) {
            pStatistic->CustomStat.ullRsr36MCRCOk++;
        }
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO"36M: ALL[%d], OK[%d]\n", (INT)pStatistic->CustomStat.ullRsr36M, (INT)pStatistic->CustomStat.ullRsr36MCRCOk);
    }
    else if(byRxRate==96){
        pStatistic->CustomStat.ullRsr48M++;
        if(BITbIsBitOn(byRSR, RSR_CRCOK)) {
            pStatistic->CustomStat.ullRsr48MCRCOk++;
        }
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO"48M: ALL[%d], OK[%d]\n", (INT)pStatistic->CustomStat.ullRsr48M, (INT)pStatistic->CustomStat.ullRsr48MCRCOk);
    }
    else if(byRxRate==108){
        pStatistic->CustomStat.ullRsr54M++;
        if(BITbIsBitOn(byRSR, RSR_CRCOK)) {
            pStatistic->CustomStat.ullRsr54MCRCOk++;
        }
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO"54M: ALL[%d], OK[%d]\n", (INT)pStatistic->CustomStat.ullRsr54M, (INT)pStatistic->CustomStat.ullRsr54MCRCOk);
    }
    else {
    	DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Unknown: Total[%d], CRCOK[%d]\n", (INT)pStatistic->dwRsrRxPacket+1, (INT)pStatistic->dwRsrCRCOk);
    }

    if (BITbIsBitOn(byRSR, RSR_BSSIDOK))
        pStatistic->dwRsrBSSIDOk++;

    if (BITbIsBitOn(byRSR, RSR_BCNSSIDOK))
        pStatistic->dwRsrBCNSSIDOk++;
    if (BITbIsBitOn(byRSR, RSR_IVLDLEN))  //invalid len (> 2312 byte)
        pStatistic->dwRsrLENErr++;
    if (BITbIsBitOn(byRSR, RSR_IVLDTYP))  //invalid packet type
        pStatistic->dwRsrTYPErr++;
    if (BITbIsBitOn(byRSR, (RSR_IVLDTYP | RSR_IVLDLEN)))
        pStatistic->dwRsrErr++;

    if (BITbIsBitOn(byNewRSR, NEWRSR_DECRYPTOK))
        pStatistic->dwNewRsrDECRYPTOK++;
    if (BITbIsBitOn(byNewRSR, NEWRSR_CFPIND))
        pStatistic->dwNewRsrCFP++;
    if (BITbIsBitOn(byNewRSR, NEWRSR_HWUTSF))
        pStatistic->dwNewRsrUTSF++;
    if (BITbIsBitOn(byNewRSR, NEWRSR_BCNHITAID))
        pStatistic->dwNewRsrHITAID++;
    if (BITbIsBitOn(byNewRSR, NEWRSR_BCNHITAID0))
        pStatistic->dwNewRsrHITAID0++;

    // increase rx packet count
    pStatistic->dwRsrRxPacket++;
    pStatistic->dwRsrRxOctet += cbFrameLength;


    if (IS_TYPE_DATA(pbyBuffer)) {
        pStatistic->dwRsrRxData++;
    } else if (IS_TYPE_MGMT(pbyBuffer)){
        pStatistic->dwRsrRxManage++;
    } else if (IS_TYPE_CONTROL(pbyBuffer)){
        pStatistic->dwRsrRxControl++;
    }

    if (BITbIsBitOn(byRSR, RSR_ADDRBROAD))
        pStatistic->dwRsrBroadcast++;
    else if (BITbIsBitOn(byRSR, RSR_ADDRMULTI))
        pStatistic->dwRsrMulticast++;
    else
        pStatistic->dwRsrDirected++;

    if (WLAN_GET_FC_MOREFRAG(pHeader->wFrameCtl))
        pStatistic->dwRsrRxFragment++;

    if (cbFrameLength < MIN_PACKET_LEN + 4) {
        pStatistic->dwRsrRunt++;
    }
    else if (cbFrameLength == MIN_PACKET_LEN + 4) {
        pStatistic->dwRsrRxFrmLen64++;
    }
    else if ((65 <= cbFrameLength) && (cbFrameLength <= 127)) {
        pStatistic->dwRsrRxFrmLen65_127++;
    }
    else if ((128 <= cbFrameLength) && (cbFrameLength <= 255)) {
        pStatistic->dwRsrRxFrmLen128_255++;
    }
    else if ((256 <= cbFrameLength) && (cbFrameLength <= 511)) {
        pStatistic->dwRsrRxFrmLen256_511++;
    }
    else if ((512 <= cbFrameLength) && (cbFrameLength <= 1023)) {
        pStatistic->dwRsrRxFrmLen512_1023++;
    }
    else if ((1024 <= cbFrameLength) && (cbFrameLength <= MAX_PACKET_LEN + 4)) {
        pStatistic->dwRsrRxFrmLen1024_1518++;
    } else if (cbFrameLength > MAX_PACKET_LEN + 4) {
        pStatistic->dwRsrLong++;
    }

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
STAvUpdateRDStatCounterEx (
    PSStatCounter   pStatistic,
    BYTE            byRSR,
    BYTE            byNewRSR,
    BYTE            byRxRate,
    PBYTE           pbyBuffer,
    UINT            cbFrameLength
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
    MEMvCopy(pStatistic->abyCntRxPattern, (PBYTE)pbyBuffer, 10);
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
STAvUpdateTDStatCounter (
    PSStatCounter   pStatistic,
    BYTE            byTSR0,
    BYTE            byTSR1,
    PBYTE           pbyBuffer,
    UINT            cbFrameLength,
    UINT            uIdx
    )
{
    PWLAN_80211HDR_A4   pHeader;
    PBYTE               pbyDestAddr;
    BYTE                byTSR0_NCR = byTSR0 & TSR0_NCR;



    pHeader = (PWLAN_80211HDR_A4) pbyBuffer;
    if (WLAN_GET_FC_TODS(pHeader->wFrameCtl) == 0) {
        pbyDestAddr = &(pHeader->abyAddr1[0]);
    }
    else {
        pbyDestAddr = &(pHeader->abyAddr3[0]);
    }
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
        if (IS_BROADCAST_ADDRESS(pbyDestAddr)) {
            pStatistic->ullTxBroadcastFrames[uIdx]++;
            pStatistic->ullTxBroadcastBytes[uIdx] += (ULONGLONG)cbFrameLength;
        }
        else if (IS_MULTICAST_ADDRESS(pbyDestAddr)) {
            pStatistic->ullTxMulticastFrames[uIdx]++;
            pStatistic->ullTxMulticastBytes[uIdx] += (ULONGLONG)cbFrameLength;
        }
        else {
            pStatistic->ullTxDirectedFrames[uIdx]++;
            pStatistic->ullTxDirectedBytes[uIdx] += (ULONGLONG)cbFrameLength;
        }
    }
    else {
        if (BITbIsBitOn(byTSR1, TSR1_TERR))
            pStatistic->dwTsrErr[uIdx]++;
        if (BITbIsBitOn(byTSR1, TSR1_RETRYTMO))
            pStatistic->dwTsrRetryTimeout[uIdx]++;
        if (BITbIsBitOn(byTSR1, TSR1_TMO))
            pStatistic->dwTsrTransmitTimeout[uIdx]++;
        if (BITbIsBitOn(byTSR1, ACK_DATA))
            pStatistic->dwTsrACKData[uIdx]++;
    }

    if (IS_BROADCAST_ADDRESS(pbyDestAddr))
        pStatistic->dwTsrBroadcast[uIdx]++;
    else if (IS_MULTICAST_ADDRESS(pbyDestAddr))
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
STAvUpdateTDStatCounterEx (
    PSStatCounter   pStatistic,
    PBYTE           pbyBuffer,
    DWORD           cbFrameLength
    )
{
    UINT    uPktLength;

    uPktLength = (UINT)cbFrameLength;

    // tx length
    pStatistic->dwCntTxBufLength = uPktLength;
    // tx pattern, we just see 16 bytes for sample
    MEMvCopy(pStatistic->abyCntTxPattern, pbyBuffer, 16);
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
    DWORD                   dwCounter
    )
{
    //p802_11Counter->TransmittedFragmentCount
    p802_11Counter->MulticastTransmittedFrameCount = (ULONGLONG) (pStatistic->dwTsrBroadcast[TYPE_AC0DMA] +
                                                                  pStatistic->dwTsrBroadcast[TYPE_TXDMA0] +
                                                                  pStatistic->dwTsrMulticast[TYPE_AC0DMA] +
                                                                  pStatistic->dwTsrMulticast[TYPE_TXDMA0]);
    p802_11Counter->FailedCount = (ULONGLONG) (pStatistic->dwTsrErr[TYPE_AC0DMA] + pStatistic->dwTsrErr[TYPE_TXDMA0]);
    p802_11Counter->RetryCount = (ULONGLONG) (pStatistic->dwTsrRetry[TYPE_AC0DMA] + pStatistic->dwTsrRetry[TYPE_TXDMA0]);
    p802_11Counter->MultipleRetryCount = (ULONGLONG) (pStatistic->dwTsrMoreThanOnceRetry[TYPE_AC0DMA] +
                                                          pStatistic->dwTsrMoreThanOnceRetry[TYPE_TXDMA0]);
    //p802_11Counter->FrameDuplicateCount
    p802_11Counter->RTSSuccessCount += (ULONGLONG)  (dwCounter & 0x000000ff);
    p802_11Counter->RTSFailureCount += (ULONGLONG) ((dwCounter & 0x0000ff00) >> 8);
    p802_11Counter->ACKFailureCount += (ULONGLONG) ((dwCounter & 0x00ff0000) >> 16);
    p802_11Counter->FCSErrorCount +=   (ULONGLONG) ((dwCounter & 0xff000000) >> 24);
    //p802_11Counter->ReceivedFragmentCount
    p802_11Counter->MulticastReceivedFrameCount = (ULONGLONG) (pStatistic->dwRsrBroadcast +
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
    ZERO_MEMORY(p802_11Counter, sizeof(SDot11Counters));
}
