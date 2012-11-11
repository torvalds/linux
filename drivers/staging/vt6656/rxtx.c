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
 * File: rxtx.c
 *
 * Purpose: handle WMAC/802.3/802.11 rx & tx functions
 *
 * Author: Lyndon Chen
 *
 * Date: May 20, 2003
 *
 * Functions:
 *      s_vGenerateTxParameter - Generate tx dma required parameter.
 *      s_vGenerateMACHeader - Translate 802.3 to 802.11 header
 *      csBeacon_xmit - beacon tx function
 *      csMgmt_xmit - management tx function
 *      s_uGetDataDuration - get tx data required duration
 *      s_uFillDataHead- fulfill tx data duration header
 *      s_uGetRTSCTSDuration- get rtx/cts required duration
 *      s_uGetRTSCTSRsvTime- get rts/cts reserved time
 *      s_uGetTxRsvTime- get frame reserved time
 *      s_vFillCTSHead- fulfill CTS ctl header
 *      s_vFillFragParameter- Set fragment ctl parameter.
 *      s_vFillRTSHead- fulfill RTS ctl header
 *      s_vFillTxKey- fulfill tx encrypt key
 *      s_vSWencryption- Software encrypt header
 *      vDMA0_tx_80211- tx 802.11 frame via dma0
 *      vGenerateFIFOHeader- Generate tx FIFO ctl header
 *
 * Revision History:
 *
 */

#include "device.h"
#include "rxtx.h"
#include "tether.h"
#include "card.h"
#include "bssdb.h"
#include "mac.h"
#include "baseband.h"
#include "michael.h"
#include "tkip.h"
#include "tcrc.h"
#include "wctl.h"
#include "hostap.h"
#include "rf.h"
#include "datarate.h"
#include "usbpipe.h"
#include "iocmd.h"

/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/
static int          msglevel                = MSG_LEVEL_INFO;

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Static Definitions -------------------------*/

const WORD wTimeStampOff[2][MAX_RATE] = {
        {384, 288, 226, 209, 54, 43, 37, 31, 28, 25, 24, 23}, // Long Preamble
        {384, 192, 130, 113, 54, 43, 37, 31, 28, 25, 24, 23}, // Short Preamble
    };

const WORD wFB_Opt0[2][5] = {
        {RATE_12M, RATE_18M, RATE_24M, RATE_36M, RATE_48M}, // fallback_rate0
        {RATE_12M, RATE_12M, RATE_18M, RATE_24M, RATE_36M}, // fallback_rate1
    };
const WORD wFB_Opt1[2][5] = {
        {RATE_12M, RATE_18M, RATE_24M, RATE_24M, RATE_36M}, // fallback_rate0
        {RATE_6M , RATE_6M,  RATE_12M, RATE_12M, RATE_18M}, // fallback_rate1
    };


#define RTSDUR_BB       0
#define RTSDUR_BA       1
#define RTSDUR_AA       2
#define CTSDUR_BA       3
#define RTSDUR_BA_F0    4
#define RTSDUR_AA_F0    5
#define RTSDUR_BA_F1    6
#define RTSDUR_AA_F1    7
#define CTSDUR_BA_F0    8
#define CTSDUR_BA_F1    9
#define DATADUR_B       10
#define DATADUR_A       11
#define DATADUR_A_F0    12
#define DATADUR_A_F1    13

/*---------------------  Static Functions  --------------------------*/

static
void
s_vSaveTxPktInfo(
     PSDevice pDevice,
     BYTE byPktNum,
     PBYTE pbyDestAddr,
     WORD wPktLength,
     WORD wFIFOCtl
);

static
void *
s_vGetFreeContext(
    PSDevice pDevice
    );


static
void
s_vGenerateTxParameter(
     PSDevice         pDevice,
     BYTE             byPktType,
     WORD             wCurrentRate,
     void *pTxBufHead,
     void *pvRrvTime,
     void *pvRTS,
     void *pvCTS,
     unsigned int             cbFrameSize,
     BOOL             bNeedACK,
     unsigned int             uDMAIdx,
     PSEthernetHeader psEthHeader
    );


static unsigned int s_uFillDataHead(
     PSDevice pDevice,
     BYTE     byPktType,
     WORD     wCurrentRate,
     void *pTxDataHead,
     unsigned int     cbFrameLength,
     unsigned int     uDMAIdx,
     BOOL     bNeedAck,
     unsigned int     uFragIdx,
     unsigned int     cbLastFragmentSize,
     unsigned int     uMACfragNum,
     BYTE     byFBOption
    );




static
void
s_vGenerateMACHeader (
     PSDevice         pDevice,
     PBYTE            pbyBufferAddr,
     WORD             wDuration,
     PSEthernetHeader psEthHeader,
     BOOL             bNeedEncrypt,
     WORD             wFragType,
     unsigned int             uDMAIdx,
     unsigned int             uFragIdx
    );

static
void
s_vFillTxKey(
      PSDevice   pDevice,
      PBYTE      pbyBuf,
      PBYTE      pbyIVHead,
      PSKeyItem  pTransmitKey,
      PBYTE      pbyHdrBuf,
      WORD       wPayloadLen,
     PBYTE      pMICHDR
    );

static
void
s_vSWencryption (
      PSDevice         pDevice,
      PSKeyItem        pTransmitKey,
      PBYTE            pbyPayloadHead,
      WORD             wPayloadSize
    );

static unsigned int s_uGetTxRsvTime(
     PSDevice pDevice,
     BYTE     byPktType,
     unsigned int     cbFrameLength,
     WORD     wRate,
     BOOL     bNeedAck
    );


static unsigned int s_uGetRTSCTSRsvTime(
     PSDevice pDevice,
     BYTE byRTSRsvType,
     BYTE byPktType,
     unsigned int cbFrameLength,
     WORD wCurrentRate
    );

static
void
s_vFillCTSHead (
     PSDevice pDevice,
     unsigned int     uDMAIdx,
     BYTE     byPktType,
     void *pvCTS,
     unsigned int     cbFrameLength,
     BOOL     bNeedAck,
     BOOL     bDisCRC,
     WORD     wCurrentRate,
     BYTE     byFBOption
    );

static
void
s_vFillRTSHead(
     PSDevice         pDevice,
     BYTE             byPktType,
     void *pvRTS,
     unsigned int             cbFrameLength,
     BOOL             bNeedAck,
     BOOL             bDisCRC,
     PSEthernetHeader psEthHeader,
     WORD             wCurrentRate,
     BYTE             byFBOption
    );

static unsigned int s_uGetDataDuration(
     PSDevice pDevice,
     BYTE     byDurType,
     unsigned int     cbFrameLength,
     BYTE     byPktType,
     WORD     wRate,
     BOOL     bNeedAck,
     unsigned int     uFragIdx,
     unsigned int     cbLastFragmentSize,
     unsigned int     uMACfragNum,
     BYTE     byFBOption
    );


static
unsigned int
s_uGetRTSCTSDuration (
     PSDevice pDevice,
     BYTE byDurType,
     unsigned int cbFrameLength,
     BYTE byPktType,
     WORD wRate,
     BOOL bNeedAck,
     BYTE byFBOption
    );


/*---------------------  Export Variables  --------------------------*/

static
void *
s_vGetFreeContext(
    PSDevice pDevice
    )
{
    PUSB_SEND_CONTEXT   pContext = NULL;
    PUSB_SEND_CONTEXT   pReturnContext = NULL;
    unsigned int                ii;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"GetFreeContext()\n");

    for (ii = 0; ii < pDevice->cbTD; ii++) {
        pContext = pDevice->apTD[ii];
        if (pContext->bBoolInUse == FALSE) {
            pContext->bBoolInUse = TRUE;
            pReturnContext = pContext;
            break;
        }
    }
    if ( ii == pDevice->cbTD ) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"No Free Tx Context\n");
    }
    return (void *) pReturnContext;
}


static
void
s_vSaveTxPktInfo(PSDevice pDevice, BYTE byPktNum, PBYTE pbyDestAddr, WORD wPktLength, WORD wFIFOCtl)
{
    PSStatCounter           pStatistic=&(pDevice->scStatistic);

    if (is_broadcast_ether_addr(pbyDestAddr))
        pStatistic->abyTxPktInfo[byPktNum].byBroadMultiUni = TX_PKT_BROAD;
    else if (is_multicast_ether_addr(pbyDestAddr))
        pStatistic->abyTxPktInfo[byPktNum].byBroadMultiUni = TX_PKT_MULTI;
    else
        pStatistic->abyTxPktInfo[byPktNum].byBroadMultiUni = TX_PKT_UNI;

    pStatistic->abyTxPktInfo[byPktNum].wLength = wPktLength;
    pStatistic->abyTxPktInfo[byPktNum].wFIFOCtl = wFIFOCtl;
    memcpy(pStatistic->abyTxPktInfo[byPktNum].abyDestAddr,
	   pbyDestAddr,
	   ETH_ALEN);
}

static
void
s_vFillTxKey (
      PSDevice   pDevice,
      PBYTE      pbyBuf,
      PBYTE      pbyIVHead,
      PSKeyItem  pTransmitKey,
      PBYTE      pbyHdrBuf,
      WORD       wPayloadLen,
     PBYTE      pMICHDR
    )
{
    PDWORD          pdwIV = (PDWORD) pbyIVHead;
    PDWORD          pdwExtIV = (PDWORD) ((PBYTE)pbyIVHead+4);
    WORD            wValue;
    PS802_11Header  pMACHeader = (PS802_11Header)pbyHdrBuf;
    DWORD           dwRevIVCounter;



    //Fill TXKEY
    if (pTransmitKey == NULL)
        return;

    dwRevIVCounter = cpu_to_le32(pDevice->dwIVCounter);
    *pdwIV = pDevice->dwIVCounter;
    pDevice->byKeyIndex = pTransmitKey->dwKeyIndex & 0xf;

    if (pTransmitKey->byCipherSuite == KEY_CTL_WEP) {
        if (pTransmitKey->uKeyLength == WLAN_WEP232_KEYLEN ){
            memcpy(pDevice->abyPRNG, (PBYTE)&(dwRevIVCounter), 3);
            memcpy(pDevice->abyPRNG+3, pTransmitKey->abyKey, pTransmitKey->uKeyLength);
        } else {
            memcpy(pbyBuf, (PBYTE)&(dwRevIVCounter), 3);
            memcpy(pbyBuf+3, pTransmitKey->abyKey, pTransmitKey->uKeyLength);
            if(pTransmitKey->uKeyLength == WLAN_WEP40_KEYLEN) {
                memcpy(pbyBuf+8, (PBYTE)&(dwRevIVCounter), 3);
                memcpy(pbyBuf+11, pTransmitKey->abyKey, pTransmitKey->uKeyLength);
            }
            memcpy(pDevice->abyPRNG, pbyBuf, 16);
        }
        // Append IV after Mac Header
        *pdwIV &= WEP_IV_MASK;//00000000 11111111 11111111 11111111
        *pdwIV |= (unsigned long)pDevice->byKeyIndex << 30;
        *pdwIV = cpu_to_le32(*pdwIV);
        pDevice->dwIVCounter++;
        if (pDevice->dwIVCounter > WEP_IV_MASK) {
            pDevice->dwIVCounter = 0;
        }
    } else if (pTransmitKey->byCipherSuite == KEY_CTL_TKIP) {
        pTransmitKey->wTSC15_0++;
        if (pTransmitKey->wTSC15_0 == 0) {
            pTransmitKey->dwTSC47_16++;
        }
        TKIPvMixKey(pTransmitKey->abyKey, pDevice->abyCurrentNetAddr,
                    pTransmitKey->wTSC15_0, pTransmitKey->dwTSC47_16, pDevice->abyPRNG);
        memcpy(pbyBuf, pDevice->abyPRNG, 16);
        // Make IV
        memcpy(pdwIV, pDevice->abyPRNG, 3);

        *(pbyIVHead+3) = (BYTE)(((pDevice->byKeyIndex << 6) & 0xc0) | 0x20); // 0x20 is ExtIV
        // Append IV&ExtIV after Mac Header
        *pdwExtIV = cpu_to_le32(pTransmitKey->dwTSC47_16);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"vFillTxKey()---- pdwExtIV: %lx\n", *pdwExtIV);

    } else if (pTransmitKey->byCipherSuite == KEY_CTL_CCMP) {
        pTransmitKey->wTSC15_0++;
        if (pTransmitKey->wTSC15_0 == 0) {
            pTransmitKey->dwTSC47_16++;
        }
        memcpy(pbyBuf, pTransmitKey->abyKey, 16);

        // Make IV
        *pdwIV = 0;
        *(pbyIVHead+3) = (BYTE)(((pDevice->byKeyIndex << 6) & 0xc0) | 0x20); // 0x20 is ExtIV
        *pdwIV |= cpu_to_le16((WORD)(pTransmitKey->wTSC15_0));
        //Append IV&ExtIV after Mac Header
        *pdwExtIV = cpu_to_le32(pTransmitKey->dwTSC47_16);

        //Fill MICHDR0
        *pMICHDR = 0x59;
        *((PBYTE)(pMICHDR+1)) = 0; // TxPriority
        memcpy(pMICHDR+2, &(pMACHeader->abyAddr2[0]), 6);
        *((PBYTE)(pMICHDR+8)) = HIBYTE(HIWORD(pTransmitKey->dwTSC47_16));
        *((PBYTE)(pMICHDR+9)) = LOBYTE(HIWORD(pTransmitKey->dwTSC47_16));
        *((PBYTE)(pMICHDR+10)) = HIBYTE(LOWORD(pTransmitKey->dwTSC47_16));
        *((PBYTE)(pMICHDR+11)) = LOBYTE(LOWORD(pTransmitKey->dwTSC47_16));
        *((PBYTE)(pMICHDR+12)) = HIBYTE(pTransmitKey->wTSC15_0);
        *((PBYTE)(pMICHDR+13)) = LOBYTE(pTransmitKey->wTSC15_0);
        *((PBYTE)(pMICHDR+14)) = HIBYTE(wPayloadLen);
        *((PBYTE)(pMICHDR+15)) = LOBYTE(wPayloadLen);

        //Fill MICHDR1
        *((PBYTE)(pMICHDR+16)) = 0; // HLEN[15:8]
        if (pDevice->bLongHeader) {
            *((PBYTE)(pMICHDR+17)) = 28; // HLEN[7:0]
        } else {
            *((PBYTE)(pMICHDR+17)) = 22; // HLEN[7:0]
        }
        wValue = cpu_to_le16(pMACHeader->wFrameCtl & 0xC78F);
        memcpy(pMICHDR+18, (PBYTE)&wValue, 2); // MSKFRACTL
        memcpy(pMICHDR+20, &(pMACHeader->abyAddr1[0]), 6);
        memcpy(pMICHDR+26, &(pMACHeader->abyAddr2[0]), 6);

        //Fill MICHDR2
        memcpy(pMICHDR+32, &(pMACHeader->abyAddr3[0]), 6);
        wValue = pMACHeader->wSeqCtl;
        wValue &= 0x000F;
        wValue = cpu_to_le16(wValue);
        memcpy(pMICHDR+38, (PBYTE)&wValue, 2); // MSKSEQCTL
        if (pDevice->bLongHeader) {
            memcpy(pMICHDR+40, &(pMACHeader->abyAddr4[0]), 6);
        }
    }
}


static
void
s_vSWencryption (
      PSDevice            pDevice,
      PSKeyItem           pTransmitKey,
      PBYTE               pbyPayloadHead,
      WORD                wPayloadSize
    )
{
    unsigned int   cbICVlen = 4;
    DWORD  dwICV = 0xFFFFFFFFL;
    PDWORD pdwICV;

    if (pTransmitKey == NULL)
        return;

    if (pTransmitKey->byCipherSuite == KEY_CTL_WEP) {
        //=======================================================================
        // Append ICV after payload
        dwICV = CRCdwGetCrc32Ex(pbyPayloadHead, wPayloadSize, dwICV);//ICV(Payload)
        pdwICV = (PDWORD)(pbyPayloadHead + wPayloadSize);
        // finally, we must invert dwCRC to get the correct answer
        *pdwICV = cpu_to_le32(~dwICV);
        // RC4 encryption
        rc4_init(&pDevice->SBox, pDevice->abyPRNG, pTransmitKey->uKeyLength + 3);
        rc4_encrypt(&pDevice->SBox, pbyPayloadHead, pbyPayloadHead, wPayloadSize+cbICVlen);
        //=======================================================================
    } else if (pTransmitKey->byCipherSuite == KEY_CTL_TKIP) {
        //=======================================================================
        //Append ICV after payload
        dwICV = CRCdwGetCrc32Ex(pbyPayloadHead, wPayloadSize, dwICV);//ICV(Payload)
        pdwICV = (PDWORD)(pbyPayloadHead + wPayloadSize);
        // finally, we must invert dwCRC to get the correct answer
        *pdwICV = cpu_to_le32(~dwICV);
        // RC4 encryption
        rc4_init(&pDevice->SBox, pDevice->abyPRNG, TKIP_KEY_LEN);
        rc4_encrypt(&pDevice->SBox, pbyPayloadHead, pbyPayloadHead, wPayloadSize+cbICVlen);
        //=======================================================================
    }
}




/*byPktType : PK_TYPE_11A     0
             PK_TYPE_11B     1
             PK_TYPE_11GB    2
             PK_TYPE_11GA    3
*/
static
unsigned int
s_uGetTxRsvTime (
     PSDevice pDevice,
     BYTE     byPktType,
     unsigned int     cbFrameLength,
     WORD     wRate,
     BOOL     bNeedAck
    )
{
    unsigned int uDataTime, uAckTime;

    uDataTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, cbFrameLength, wRate);
    if (byPktType == PK_TYPE_11B) {//llb,CCK mode
        uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, (WORD)pDevice->byTopCCKBasicRate);
    } else {//11g 2.4G OFDM mode & 11a 5G OFDM mode
        uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, (WORD)pDevice->byTopOFDMBasicRate);
    }

    if (bNeedAck) {
        return (uDataTime + pDevice->uSIFS + uAckTime);
    }
    else {
        return uDataTime;
    }
}

//byFreqType: 0=>5GHZ 1=>2.4GHZ
static
unsigned int
s_uGetRTSCTSRsvTime (
     PSDevice pDevice,
     BYTE byRTSRsvType,
     BYTE byPktType,
     unsigned int cbFrameLength,
     WORD wCurrentRate
    )
{
    unsigned int uRrvTime  , uRTSTime, uCTSTime, uAckTime, uDataTime;

    uRrvTime = uRTSTime = uCTSTime = uAckTime = uDataTime = 0;


    uDataTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, cbFrameLength, wCurrentRate);
    if (byRTSRsvType == 0) { //RTSTxRrvTime_bb
        uRTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 20, pDevice->byTopCCKBasicRate);
        uCTSTime = uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
    }
    else if (byRTSRsvType == 1){ //RTSTxRrvTime_ba, only in 2.4GHZ
        uRTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 20, pDevice->byTopCCKBasicRate);
        uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
        uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
    }
    else if (byRTSRsvType == 2) { //RTSTxRrvTime_aa
        uRTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 20, pDevice->byTopOFDMBasicRate);
        uCTSTime = uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
    }
    else if (byRTSRsvType == 3) { //CTSTxRrvTime_ba, only in 2.4GHZ
        uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
        uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
        uRrvTime = uCTSTime + uAckTime + uDataTime + 2*pDevice->uSIFS;
        return uRrvTime;
    }

    //RTSRrvTime
    uRrvTime = uRTSTime + uCTSTime + uAckTime + uDataTime + 3*pDevice->uSIFS;
    return uRrvTime;
}

//byFreqType 0: 5GHz, 1:2.4Ghz
static
unsigned int
s_uGetDataDuration (
     PSDevice pDevice,
     BYTE     byDurType,
     unsigned int     cbFrameLength,
     BYTE     byPktType,
     WORD     wRate,
     BOOL     bNeedAck,
     unsigned int     uFragIdx,
     unsigned int     cbLastFragmentSize,
     unsigned int     uMACfragNum,
     BYTE     byFBOption
    )
{
    BOOL bLastFrag = 0;
    unsigned int uAckTime = 0, uNextPktTime = 0;

    if (uFragIdx == (uMACfragNum-1)) {
        bLastFrag = 1;
    }

    switch (byDurType) {

    case DATADUR_B:    //DATADUR_B
        if (((uMACfragNum == 1)) || (bLastFrag == 1)) {//Non Frag or Last Frag
            if (bNeedAck) {
            	uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
                return (pDevice->uSIFS + uAckTime);
            } else {
                return 0;
            }
        }
        else {//First Frag or Mid Frag
            if (uFragIdx == (uMACfragNum-2)) {
            	uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wRate, bNeedAck);
            } else {
                uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);
            }
            if (bNeedAck) {
            	uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
                return (pDevice->uSIFS + uAckTime + uNextPktTime);
            } else {
                return (pDevice->uSIFS + uNextPktTime);
            }
        }
        break;


    case DATADUR_A:    //DATADUR_A
        if (((uMACfragNum==1)) || (bLastFrag==1)) {//Non Frag or Last Frag
            if(bNeedAck){
            	uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
                return (pDevice->uSIFS + uAckTime);
            } else {
                return 0;
            }
        }
        else {//First Frag or Mid Frag
            if(uFragIdx == (uMACfragNum-2)){
            	uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wRate, bNeedAck);
            } else {
                uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);
            }
            if(bNeedAck){
            	uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
                return (pDevice->uSIFS + uAckTime + uNextPktTime);
            } else {
                return (pDevice->uSIFS + uNextPktTime);
            }
        }
        break;

    case DATADUR_A_F0:    //DATADUR_A_F0
	    if (((uMACfragNum==1)) || (bLastFrag==1)) {//Non Frag or Last Frag
            if(bNeedAck){
            	uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
                return (pDevice->uSIFS + uAckTime);
            } else {
                return 0;
            }
        }
	    else { //First Frag or Mid Frag
	        if (byFBOption == AUTO_FB_0) {
                if (wRate < RATE_18M)
                    wRate = RATE_18M;
                else if (wRate > RATE_54M)
                    wRate = RATE_54M;

	            if(uFragIdx == (uMACfragNum-2)){
            	    uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
                } else {
                    uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
                }
	        } else { // (byFBOption == AUTO_FB_1)
                if (wRate < RATE_18M)
                    wRate = RATE_18M;
                else if (wRate > RATE_54M)
                    wRate = RATE_54M;

	            if(uFragIdx == (uMACfragNum-2)){
            	    uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);
                } else {
                    uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);
                }
	        }

	        if(bNeedAck){
            	uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
                return (pDevice->uSIFS + uAckTime + uNextPktTime);
            } else {
                return (pDevice->uSIFS + uNextPktTime);
            }
	    }
        break;

    case DATADUR_A_F1:    //DATADUR_A_F1
        if (((uMACfragNum==1)) || (bLastFrag==1)) {//Non Frag or Last Frag
            if(bNeedAck){
            	uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
                return (pDevice->uSIFS + uAckTime);
            } else {
                return 0;
            }
        }
	    else { //First Frag or Mid Frag
	        if (byFBOption == AUTO_FB_0) {
                if (wRate < RATE_18M)
                    wRate = RATE_18M;
                else if (wRate > RATE_54M)
                    wRate = RATE_54M;

	            if(uFragIdx == (uMACfragNum-2)){
            	    uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
                } else {
                    uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
                }

	        } else { // (byFBOption == AUTO_FB_1)
                if (wRate < RATE_18M)
                    wRate = RATE_18M;
                else if (wRate > RATE_54M)
                    wRate = RATE_54M;

	            if(uFragIdx == (uMACfragNum-2)){
            	    uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);
                } else {
                    uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);
                }
	        }
	        if(bNeedAck){
            	uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
                return (pDevice->uSIFS + uAckTime + uNextPktTime);
            } else {
                return (pDevice->uSIFS + uNextPktTime);
            }
	    }
        break;

    default:
        break;
    }

	ASSERT(FALSE);
	return 0;
}


//byFreqType: 0=>5GHZ 1=>2.4GHZ
static
unsigned int
s_uGetRTSCTSDuration (
     PSDevice pDevice,
     BYTE byDurType,
     unsigned int cbFrameLength,
     BYTE byPktType,
     WORD wRate,
     BOOL bNeedAck,
     BYTE byFBOption
    )
{
    unsigned int uCTSTime = 0, uDurTime = 0;


    switch (byDurType) {

    case RTSDUR_BB:    //RTSDuration_bb
        uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
        uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);
        break;

    case RTSDUR_BA:    //RTSDuration_ba
        uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
        uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);
        break;

    case RTSDUR_AA:    //RTSDuration_aa
        uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
        uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);
        break;

    case CTSDUR_BA:    //CTSDuration_ba
        uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);
        break;

    case RTSDUR_BA_F0: //RTSDuration_ba_f0
        uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
        if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <=RATE_54M)) {
            uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
        } else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <=RATE_54M)) {
            uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);
        }
        break;

    case RTSDUR_AA_F0: //RTSDuration_aa_f0
        uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
        if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <=RATE_54M)) {
            uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
        } else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <=RATE_54M)) {
            uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);
        }
        break;

    case RTSDUR_BA_F1: //RTSDuration_ba_f1
        uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
        if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <=RATE_54M)) {
            uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
        } else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <=RATE_54M)) {
            uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);
        }
        break;

    case RTSDUR_AA_F1: //RTSDuration_aa_f1
        uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
        if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <=RATE_54M)) {
            uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
        } else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <=RATE_54M)) {
            uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);
        }
        break;

    case CTSDUR_BA_F0: //CTSDuration_ba_f0
        if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <=RATE_54M)) {
            uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
        } else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <=RATE_54M)) {
            uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);
        }
        break;

    case CTSDUR_BA_F1: //CTSDuration_ba_f1
        if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <=RATE_54M)) {
            uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
        } else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <=RATE_54M)) {
            uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);
        }
        break;

    default:
        break;
    }

    return uDurTime;

}




static
unsigned int
s_uFillDataHead (
     PSDevice pDevice,
     BYTE     byPktType,
     WORD     wCurrentRate,
     void *pTxDataHead,
     unsigned int     cbFrameLength,
     unsigned int     uDMAIdx,
     BOOL     bNeedAck,
     unsigned int     uFragIdx,
     unsigned int     cbLastFragmentSize,
     unsigned int     uMACfragNum,
     BYTE     byFBOption
    )
{

    if (pTxDataHead == NULL) {
        return 0;
    }

    if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
	if ((uDMAIdx == TYPE_ATIMDMA) || (uDMAIdx == TYPE_BEACONDMA)) {
		PSTxDataHead_ab pBuf = (PSTxDataHead_ab) pTxDataHead;
            //Get SignalField,ServiceField,Length
            BBvCalculateParameter(pDevice, cbFrameLength, wCurrentRate, byPktType,
                (PWORD)&(pBuf->wTransmitLength), (PBYTE)&(pBuf->byServiceField), (PBYTE)&(pBuf->bySignalField)
            );
            //Get Duration and TimeStampOff
            pBuf->wDuration = (WORD)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength, byPktType,
                                                       wCurrentRate, bNeedAck, uFragIdx,
                                                       cbLastFragmentSize, uMACfragNum,
                                                       byFBOption); //1: 2.4GHz
            if(uDMAIdx!=TYPE_ATIMDMA) {
                pBuf->wTimeStampOff = wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE];
            }
            return (pBuf->wDuration);
        }
        else { // DATA & MANAGE Frame
            if (byFBOption == AUTO_FB_NONE) {
                PSTxDataHead_g pBuf = (PSTxDataHead_g)pTxDataHead;
                //Get SignalField,ServiceField,Length
                BBvCalculateParameter(pDevice, cbFrameLength, wCurrentRate, byPktType,
                    (PWORD)&(pBuf->wTransmitLength_a), (PBYTE)&(pBuf->byServiceField_a), (PBYTE)&(pBuf->bySignalField_a)
                );
                BBvCalculateParameter(pDevice, cbFrameLength, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
                    (PWORD)&(pBuf->wTransmitLength_b), (PBYTE)&(pBuf->byServiceField_b), (PBYTE)&(pBuf->bySignalField_b)
                );
                //Get Duration and TimeStamp
                pBuf->wDuration_a = (WORD)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength,
                                                             byPktType, wCurrentRate, bNeedAck, uFragIdx,
                                                             cbLastFragmentSize, uMACfragNum,
                                                             byFBOption); //1: 2.4GHz
                pBuf->wDuration_b = (WORD)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameLength,
                                                             PK_TYPE_11B, pDevice->byTopCCKBasicRate,
                                                             bNeedAck, uFragIdx, cbLastFragmentSize,
                                                             uMACfragNum, byFBOption); //1: 2.4GHz

                pBuf->wTimeStampOff_a = wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE];
                pBuf->wTimeStampOff_b = wTimeStampOff[pDevice->byPreambleType%2][pDevice->byTopCCKBasicRate%MAX_RATE];
                return (pBuf->wDuration_a);
             } else {
                // Auto Fallback
                PSTxDataHead_g_FB pBuf = (PSTxDataHead_g_FB)pTxDataHead;
                //Get SignalField,ServiceField,Length
                BBvCalculateParameter(pDevice, cbFrameLength, wCurrentRate, byPktType,
                    (PWORD)&(pBuf->wTransmitLength_a), (PBYTE)&(pBuf->byServiceField_a), (PBYTE)&(pBuf->bySignalField_a)
                );
                BBvCalculateParameter(pDevice, cbFrameLength, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
                    (PWORD)&(pBuf->wTransmitLength_b), (PBYTE)&(pBuf->byServiceField_b), (PBYTE)&(pBuf->bySignalField_b)
                );
                //Get Duration and TimeStamp
                pBuf->wDuration_a = (WORD)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength, byPktType,
                                             wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption); //1: 2.4GHz
                pBuf->wDuration_b = (WORD)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameLength, PK_TYPE_11B,
                                             pDevice->byTopCCKBasicRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption); //1: 2.4GHz
                pBuf->wDuration_a_f0 = (WORD)s_uGetDataDuration(pDevice, DATADUR_A_F0, cbFrameLength, byPktType,
                                             wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption); //1: 2.4GHz
                pBuf->wDuration_a_f1 = (WORD)s_uGetDataDuration(pDevice, DATADUR_A_F1, cbFrameLength, byPktType,
                                             wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption); //1: 2.4GHz
                pBuf->wTimeStampOff_a = wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE];
                pBuf->wTimeStampOff_b = wTimeStampOff[pDevice->byPreambleType%2][pDevice->byTopCCKBasicRate%MAX_RATE];
                return (pBuf->wDuration_a);
            } //if (byFBOption == AUTO_FB_NONE)
        }
    }
    else if (byPktType == PK_TYPE_11A) {
        if ((byFBOption != AUTO_FB_NONE) && (uDMAIdx != TYPE_ATIMDMA) && (uDMAIdx != TYPE_BEACONDMA)) {
            // Auto Fallback
            PSTxDataHead_a_FB pBuf = (PSTxDataHead_a_FB)pTxDataHead;
            //Get SignalField,ServiceField,Length
            BBvCalculateParameter(pDevice, cbFrameLength, wCurrentRate, byPktType,
                (PWORD)&(pBuf->wTransmitLength), (PBYTE)&(pBuf->byServiceField), (PBYTE)&(pBuf->bySignalField)
            );
            //Get Duration and TimeStampOff
            pBuf->wDuration = (WORD)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength, byPktType,
                                        wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption); //0: 5GHz
            pBuf->wDuration_f0 = (WORD)s_uGetDataDuration(pDevice, DATADUR_A_F0, cbFrameLength, byPktType,
                                        wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption); //0: 5GHz
            pBuf->wDuration_f1 = (WORD)s_uGetDataDuration(pDevice, DATADUR_A_F1, cbFrameLength, byPktType,
                                        wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption); //0: 5GHz
            if(uDMAIdx!=TYPE_ATIMDMA) {
                pBuf->wTimeStampOff = wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE];
            }
            return (pBuf->wDuration);
        } else {
            PSTxDataHead_ab pBuf = (PSTxDataHead_ab)pTxDataHead;
            //Get SignalField,ServiceField,Length
            BBvCalculateParameter(pDevice, cbFrameLength, wCurrentRate, byPktType,
                (PWORD)&(pBuf->wTransmitLength), (PBYTE)&(pBuf->byServiceField), (PBYTE)&(pBuf->bySignalField)
            );
            //Get Duration and TimeStampOff
            pBuf->wDuration = (WORD)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength, byPktType,
                                                       wCurrentRate, bNeedAck, uFragIdx,
                                                       cbLastFragmentSize, uMACfragNum,
                                                       byFBOption);

            if(uDMAIdx!=TYPE_ATIMDMA) {
                pBuf->wTimeStampOff = wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE];
            }
            return (pBuf->wDuration);
        }
    }
    else if (byPktType == PK_TYPE_11B) {
            PSTxDataHead_ab pBuf = (PSTxDataHead_ab)pTxDataHead;
            //Get SignalField,ServiceField,Length
            BBvCalculateParameter(pDevice, cbFrameLength, wCurrentRate, byPktType,
                (PWORD)&(pBuf->wTransmitLength), (PBYTE)&(pBuf->byServiceField), (PBYTE)&(pBuf->bySignalField)
            );
            //Get Duration and TimeStampOff
            pBuf->wDuration = (WORD)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameLength, byPktType,
                                                       wCurrentRate, bNeedAck, uFragIdx,
                                                       cbLastFragmentSize, uMACfragNum,
                                                       byFBOption);
            if (uDMAIdx != TYPE_ATIMDMA) {
                pBuf->wTimeStampOff = wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE];
            }
            return (pBuf->wDuration);
    }
    return 0;
}




static
void
s_vFillRTSHead (
     PSDevice         pDevice,
     BYTE             byPktType,
     void *pvRTS,
     unsigned int             cbFrameLength,
     BOOL             bNeedAck,
     BOOL             bDisCRC,
     PSEthernetHeader psEthHeader,
     WORD             wCurrentRate,
     BYTE             byFBOption
    )
{
    unsigned int uRTSFrameLen = 20;
    WORD  wLen = 0x0000;

    if (pvRTS == NULL)
    	return;

    if (bDisCRC) {
        // When CRCDIS bit is on, H/W forgot to generate FCS for RTS frame,
        // in this case we need to decrease its length by 4.
        uRTSFrameLen -= 4;
    }

    // Note: So far RTSHead doesn't appear in ATIM & Beacom DMA, so we don't need to take them into account.
    //       Otherwise, we need to modified codes for them.
    if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
        if (byFBOption == AUTO_FB_NONE) {
            PSRTS_g pBuf = (PSRTS_g)pvRTS;
            //Get SignalField,ServiceField,Length
            BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
                (PWORD)&(wLen), (PBYTE)&(pBuf->byServiceField_b), (PBYTE)&(pBuf->bySignalField_b)
            );
            pBuf->wTransmitLength_b = cpu_to_le16(wLen);
            BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopOFDMBasicRate, byPktType,
                (PWORD)&(wLen), (PBYTE)&(pBuf->byServiceField_a), (PBYTE)&(pBuf->bySignalField_a)
            );
            pBuf->wTransmitLength_a = cpu_to_le16(wLen);
            //Get Duration
            pBuf->wDuration_bb = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_BB, cbFrameLength, PK_TYPE_11B, pDevice->byTopCCKBasicRate, bNeedAck, byFBOption));    //0:RTSDuration_bb, 1:2.4G, 1:CCKData
            pBuf->wDuration_aa = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //2:RTSDuration_aa, 1:2.4G, 2,3: 2.4G OFDMData
            pBuf->wDuration_ba = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_BA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //1:RTSDuration_ba, 1:2.4G, 2,3:2.4G OFDM Data

            pBuf->Data.wDurationID = pBuf->wDuration_aa;
            //Get RTS Frame body
            pBuf->Data.wFrameControl = TYPE_CTL_RTS;//0x00B4

	if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
	    (pDevice->eOPMode == OP_MODE_AP)) {
		memcpy(&(pBuf->Data.abyRA[0]),
		       &(psEthHeader->abyDstAddr[0]),
		       ETH_ALEN);
	}
            else {
		    memcpy(&(pBuf->Data.abyRA[0]),
			   &(pDevice->abyBSSID[0]),
			   ETH_ALEN);
	    }
	if (pDevice->eOPMode == OP_MODE_AP) {
		memcpy(&(pBuf->Data.abyTA[0]),
		       &(pDevice->abyBSSID[0]),
		       ETH_ALEN);
	}
            else {
		    memcpy(&(pBuf->Data.abyTA[0]),
			   &(psEthHeader->abySrcAddr[0]),
			   ETH_ALEN);
            }
        }
        else {
           PSRTS_g_FB pBuf = (PSRTS_g_FB)pvRTS;
            //Get SignalField,ServiceField,Length
            BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
                (PWORD)&(wLen), (PBYTE)&(pBuf->byServiceField_b), (PBYTE)&(pBuf->bySignalField_b)
            );
            pBuf->wTransmitLength_b = cpu_to_le16(wLen);
            BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopOFDMBasicRate, byPktType,
                (PWORD)&(wLen), (PBYTE)&(pBuf->byServiceField_a), (PBYTE)&(pBuf->bySignalField_a)
            );
            pBuf->wTransmitLength_a = cpu_to_le16(wLen);
            //Get Duration
            pBuf->wDuration_bb = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_BB, cbFrameLength, PK_TYPE_11B, pDevice->byTopCCKBasicRate, bNeedAck, byFBOption));    //0:RTSDuration_bb, 1:2.4G, 1:CCKData
            pBuf->wDuration_aa = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //2:RTSDuration_aa, 1:2.4G, 2,3:2.4G OFDMData
            pBuf->wDuration_ba = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_BA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //1:RTSDuration_ba, 1:2.4G, 2,3:2.4G OFDMData
            pBuf->wRTSDuration_ba_f0 = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_BA_F0, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption));    //4:wRTSDuration_ba_f0, 1:2.4G, 1:CCKData
            pBuf->wRTSDuration_aa_f0 = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA_F0, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption));    //5:wRTSDuration_aa_f0, 1:2.4G, 1:CCKData
            pBuf->wRTSDuration_ba_f1 = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_BA_F1, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption));    //6:wRTSDuration_ba_f1, 1:2.4G, 1:CCKData
            pBuf->wRTSDuration_aa_f1 = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA_F1, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption));    //7:wRTSDuration_aa_f1, 1:2.4G, 1:CCKData
            pBuf->Data.wDurationID = pBuf->wDuration_aa;
            //Get RTS Frame body
            pBuf->Data.wFrameControl = TYPE_CTL_RTS;//0x00B4

	if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
	    (pDevice->eOPMode == OP_MODE_AP)) {
		memcpy(&(pBuf->Data.abyRA[0]),
		       &(psEthHeader->abyDstAddr[0]),
		       ETH_ALEN);
	}
            else {
		memcpy(&(pBuf->Data.abyRA[0]),
		       &(pDevice->abyBSSID[0]),
		       ETH_ALEN);
            }

	if (pDevice->eOPMode == OP_MODE_AP) {
		memcpy(&(pBuf->Data.abyTA[0]),
		       &(pDevice->abyBSSID[0]),
		       ETH_ALEN);
	}
            else {
		    memcpy(&(pBuf->Data.abyTA[0]),
			   &(psEthHeader->abySrcAddr[0]),
			   ETH_ALEN);
            }

        } // if (byFBOption == AUTO_FB_NONE)
    }
    else if (byPktType == PK_TYPE_11A) {
        if (byFBOption == AUTO_FB_NONE) {
            PSRTS_ab pBuf = (PSRTS_ab)pvRTS;
            //Get SignalField,ServiceField,Length
            BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopOFDMBasicRate, byPktType,
                (PWORD)&(wLen), (PBYTE)&(pBuf->byServiceField), (PBYTE)&(pBuf->bySignalField)
            );
            pBuf->wTransmitLength = cpu_to_le16(wLen);
            //Get Duration
            pBuf->wDuration = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //0:RTSDuration_aa, 0:5G, 0: 5G OFDMData
    	    pBuf->Data.wDurationID = pBuf->wDuration;
            //Get RTS Frame body
            pBuf->Data.wFrameControl = TYPE_CTL_RTS;//0x00B4

	if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
	    (pDevice->eOPMode == OP_MODE_AP)) {
		memcpy(&(pBuf->Data.abyRA[0]),
		       &(psEthHeader->abyDstAddr[0]),
		       ETH_ALEN);
	} else {
		memcpy(&(pBuf->Data.abyRA[0]),
		       &(pDevice->abyBSSID[0]),
		       ETH_ALEN);
	}

	if (pDevice->eOPMode == OP_MODE_AP) {
		memcpy(&(pBuf->Data.abyTA[0]),
		       &(pDevice->abyBSSID[0]),
		       ETH_ALEN);
	} else {
		memcpy(&(pBuf->Data.abyTA[0]),
		       &(psEthHeader->abySrcAddr[0]),
		       ETH_ALEN);
	}

        }
        else {
            PSRTS_a_FB pBuf = (PSRTS_a_FB)pvRTS;
            //Get SignalField,ServiceField,Length
            BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopOFDMBasicRate, byPktType,
                (PWORD)&(wLen), (PBYTE)&(pBuf->byServiceField), (PBYTE)&(pBuf->bySignalField)
            );
            pBuf->wTransmitLength = cpu_to_le16(wLen);
            //Get Duration
            pBuf->wDuration = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //0:RTSDuration_aa, 0:5G, 0: 5G OFDMData
    	    pBuf->wRTSDuration_f0 = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA_F0, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //5:RTSDuration_aa_f0, 0:5G, 0: 5G OFDMData
    	    pBuf->wRTSDuration_f1 = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA_F1, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //7:RTSDuration_aa_f1, 0:5G, 0:
    	    pBuf->Data.wDurationID = pBuf->wDuration;
    	    //Get RTS Frame body
            pBuf->Data.wFrameControl = TYPE_CTL_RTS;//0x00B4

	if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
	    (pDevice->eOPMode == OP_MODE_AP)) {
		memcpy(&(pBuf->Data.abyRA[0]),
		       &(psEthHeader->abyDstAddr[0]),
		       ETH_ALEN);
	} else {
		memcpy(&(pBuf->Data.abyRA[0]),
		       &(pDevice->abyBSSID[0]),
		       ETH_ALEN);
	}
	if (pDevice->eOPMode == OP_MODE_AP) {
		memcpy(&(pBuf->Data.abyTA[0]),
		       &(pDevice->abyBSSID[0]),
		       ETH_ALEN);
	} else {
		memcpy(&(pBuf->Data.abyTA[0]),
		       &(psEthHeader->abySrcAddr[0]),
		       ETH_ALEN);
	}
        }
    }
    else if (byPktType == PK_TYPE_11B) {
        PSRTS_ab pBuf = (PSRTS_ab)pvRTS;
        //Get SignalField,ServiceField,Length
        BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
            (PWORD)&(wLen), (PBYTE)&(pBuf->byServiceField), (PBYTE)&(pBuf->bySignalField)
        );
        pBuf->wTransmitLength = cpu_to_le16(wLen);
        //Get Duration
        pBuf->wDuration = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, RTSDUR_BB, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //0:RTSDuration_bb, 1:2.4G, 1:CCKData
        pBuf->Data.wDurationID = pBuf->wDuration;
        //Get RTS Frame body
        pBuf->Data.wFrameControl = TYPE_CTL_RTS;//0x00B4

	if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
            (pDevice->eOPMode == OP_MODE_AP)) {
		memcpy(&(pBuf->Data.abyRA[0]),
		       &(psEthHeader->abyDstAddr[0]),
		       ETH_ALEN);
        }
        else {
		memcpy(&(pBuf->Data.abyRA[0]),
		       &(pDevice->abyBSSID[0]),
		       ETH_ALEN);
        }

        if (pDevice->eOPMode == OP_MODE_AP) {
		memcpy(&(pBuf->Data.abyTA[0]),
		       &(pDevice->abyBSSID[0]),
		       ETH_ALEN);
	} else {
		memcpy(&(pBuf->Data.abyTA[0]),
		       &(psEthHeader->abySrcAddr[0]),
		       ETH_ALEN);
        }
    }
}

static
void
s_vFillCTSHead (
     PSDevice pDevice,
     unsigned int     uDMAIdx,
     BYTE     byPktType,
     void *pvCTS,
     unsigned int     cbFrameLength,
     BOOL     bNeedAck,
     BOOL     bDisCRC,
     WORD     wCurrentRate,
     BYTE     byFBOption
    )
{
    unsigned int uCTSFrameLen = 14;
    WORD  wLen = 0x0000;

    if (pvCTS == NULL) {
        return;
    }

    if (bDisCRC) {
        // When CRCDIS bit is on, H/W forgot to generate FCS for CTS frame,
        // in this case we need to decrease its length by 4.
        uCTSFrameLen -= 4;
    }

    if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
        if (byFBOption != AUTO_FB_NONE && uDMAIdx != TYPE_ATIMDMA && uDMAIdx != TYPE_BEACONDMA) {
            // Auto Fall back
            PSCTS_FB pBuf = (PSCTS_FB)pvCTS;
            //Get SignalField,ServiceField,Length
            BBvCalculateParameter(pDevice, uCTSFrameLen, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
                (PWORD)&(wLen), (PBYTE)&(pBuf->byServiceField_b), (PBYTE)&(pBuf->bySignalField_b)
            );
            pBuf->wTransmitLength_b = cpu_to_le16(wLen);
            pBuf->wDuration_ba = (WORD)s_uGetRTSCTSDuration(pDevice, CTSDUR_BA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption); //3:CTSDuration_ba, 1:2.4G, 2,3:2.4G OFDM Data
            pBuf->wDuration_ba += pDevice->wCTSDuration;
            pBuf->wDuration_ba = cpu_to_le16(pBuf->wDuration_ba);
            //Get CTSDuration_ba_f0
            pBuf->wCTSDuration_ba_f0 = (WORD)s_uGetRTSCTSDuration(pDevice, CTSDUR_BA_F0, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption); //8:CTSDuration_ba_f0, 1:2.4G, 2,3:2.4G OFDM Data
            pBuf->wCTSDuration_ba_f0 += pDevice->wCTSDuration;
            pBuf->wCTSDuration_ba_f0 = cpu_to_le16(pBuf->wCTSDuration_ba_f0);
            //Get CTSDuration_ba_f1
            pBuf->wCTSDuration_ba_f1 = (WORD)s_uGetRTSCTSDuration(pDevice, CTSDUR_BA_F1, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption); //9:CTSDuration_ba_f1, 1:2.4G, 2,3:2.4G OFDM Data
            pBuf->wCTSDuration_ba_f1 += pDevice->wCTSDuration;
            pBuf->wCTSDuration_ba_f1 = cpu_to_le16(pBuf->wCTSDuration_ba_f1);
            //Get CTS Frame body
            pBuf->Data.wDurationID = pBuf->wDuration_ba;
            pBuf->Data.wFrameControl = TYPE_CTL_CTS;//0x00C4
            pBuf->Data.wReserved = 0x0000;
	memcpy(&(pBuf->Data.abyRA[0]),
	       &(pDevice->abyCurrentNetAddr[0]),
	       ETH_ALEN);
        } else { //if (byFBOption != AUTO_FB_NONE && uDMAIdx != TYPE_ATIMDMA && uDMAIdx != TYPE_BEACONDMA)
            PSCTS pBuf = (PSCTS)pvCTS;
            //Get SignalField,ServiceField,Length
            BBvCalculateParameter(pDevice, uCTSFrameLen, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
                (PWORD)&(wLen), (PBYTE)&(pBuf->byServiceField_b), (PBYTE)&(pBuf->bySignalField_b)
            );
            pBuf->wTransmitLength_b = cpu_to_le16(wLen);
            //Get CTSDuration_ba
            pBuf->wDuration_ba = cpu_to_le16((WORD)s_uGetRTSCTSDuration(pDevice, CTSDUR_BA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //3:CTSDuration_ba, 1:2.4G, 2,3:2.4G OFDM Data
            pBuf->wDuration_ba += pDevice->wCTSDuration;
            pBuf->wDuration_ba = cpu_to_le16(pBuf->wDuration_ba);

            //Get CTS Frame body
            pBuf->Data.wDurationID = pBuf->wDuration_ba;
            pBuf->Data.wFrameControl = TYPE_CTL_CTS;//0x00C4
            pBuf->Data.wReserved = 0x0000;
	memcpy(&(pBuf->Data.abyRA[0]),
	       &(pDevice->abyCurrentNetAddr[0]),
	       ETH_ALEN);
        }
    }
}

/*+
 *
 * Description:
 *      Generate FIFO control for MAC & Baseband controller
 *
 * Parameters:
 *  In:
 *      pDevice         - Pointer to adpater
 *      pTxDataHead     - Transmit Data Buffer
 *      pTxBufHead      - pTxBufHead
 *      pvRrvTime        - pvRrvTime
 *      pvRTS            - RTS Buffer
 *      pCTS            - CTS Buffer
 *      cbFrameSize     - Transmit Data Length (Hdr+Payload+FCS)
 *      bNeedACK        - If need ACK
 *      uDMAIdx         - DMA Index
 *  Out:
 *      none
 *
 * Return Value: none
 *
-*/

static
void
s_vGenerateTxParameter (
     PSDevice         pDevice,
     BYTE             byPktType,
     WORD             wCurrentRate,
     void *pTxBufHead,
     void *pvRrvTime,
     void *pvRTS,
     void *pvCTS,
     unsigned int             cbFrameSize,
     BOOL             bNeedACK,
     unsigned int             uDMAIdx,
     PSEthernetHeader psEthHeader
    )
{
	unsigned int cbMACHdLen = WLAN_HDR_ADDR3_LEN; /* 24 */
    WORD wFifoCtl;
    BOOL bDisCRC = FALSE;
    BYTE byFBOption = AUTO_FB_NONE;
//    WORD wCurrentRate = pDevice->wCurrentRate;

    //DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"s_vGenerateTxParameter...\n");
    PSTxBufHead pFifoHead = (PSTxBufHead)pTxBufHead;
    pFifoHead->wReserved = wCurrentRate;
    wFifoCtl = pFifoHead->wFIFOCtl;

    if (wFifoCtl & FIFOCTL_CRCDIS) {
        bDisCRC = TRUE;
    }

    if (wFifoCtl & FIFOCTL_AUTO_FB_0) {
        byFBOption = AUTO_FB_0;
    }
    else if (wFifoCtl & FIFOCTL_AUTO_FB_1) {
        byFBOption = AUTO_FB_1;
    }

    if (pDevice->bLongHeader)
        cbMACHdLen = WLAN_HDR_ADDR3_LEN + 6;

    if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {

        if (pvRTS != NULL) { //RTS_need
            //Fill RsvTime
            if (pvRrvTime) {
                PSRrvTime_gRTS pBuf = (PSRrvTime_gRTS)pvRrvTime;
                pBuf->wRTSTxRrvTime_aa = cpu_to_le16((WORD)s_uGetRTSCTSRsvTime(pDevice, 2, byPktType, cbFrameSize, wCurrentRate));//2:RTSTxRrvTime_aa, 1:2.4GHz
                pBuf->wRTSTxRrvTime_ba = cpu_to_le16((WORD)s_uGetRTSCTSRsvTime(pDevice, 1, byPktType, cbFrameSize, wCurrentRate));//1:RTSTxRrvTime_ba, 1:2.4GHz
                pBuf->wRTSTxRrvTime_bb = cpu_to_le16((WORD)s_uGetRTSCTSRsvTime(pDevice, 0, byPktType, cbFrameSize, wCurrentRate));//0:RTSTxRrvTime_bb, 1:2.4GHz
                pBuf->wTxRrvTime_a = cpu_to_le16((WORD) s_uGetTxRsvTime(pDevice, byPktType, cbFrameSize, wCurrentRate, bNeedACK));//2.4G OFDM
                pBuf->wTxRrvTime_b = cpu_to_le16((WORD) s_uGetTxRsvTime(pDevice, PK_TYPE_11B, cbFrameSize, pDevice->byTopCCKBasicRate, bNeedACK));//1:CCK
            }
            //Fill RTS
            s_vFillRTSHead(pDevice, byPktType, pvRTS, cbFrameSize, bNeedACK, bDisCRC, psEthHeader, wCurrentRate, byFBOption);
        }
        else {//RTS_needless, PCF mode

            //Fill RsvTime
            if (pvRrvTime) {
                PSRrvTime_gCTS pBuf = (PSRrvTime_gCTS)pvRrvTime;
                pBuf->wTxRrvTime_a = cpu_to_le16((WORD)s_uGetTxRsvTime(pDevice, byPktType, cbFrameSize, wCurrentRate, bNeedACK));//2.4G OFDM
                pBuf->wTxRrvTime_b = cpu_to_le16((WORD)s_uGetTxRsvTime(pDevice, PK_TYPE_11B, cbFrameSize, pDevice->byTopCCKBasicRate, bNeedACK));//1:CCK
                pBuf->wCTSTxRrvTime_ba = cpu_to_le16((WORD)s_uGetRTSCTSRsvTime(pDevice, 3, byPktType, cbFrameSize, wCurrentRate));//3:CTSTxRrvTime_Ba, 1:2.4GHz
            }
            //Fill CTS
            s_vFillCTSHead(pDevice, uDMAIdx, byPktType, pvCTS, cbFrameSize, bNeedACK, bDisCRC, wCurrentRate, byFBOption);
        }
    }
    else if (byPktType == PK_TYPE_11A) {

        if (pvRTS != NULL) {//RTS_need, non PCF mode
            //Fill RsvTime
            if (pvRrvTime) {
                PSRrvTime_ab pBuf = (PSRrvTime_ab)pvRrvTime;
                pBuf->wRTSTxRrvTime = cpu_to_le16((WORD)s_uGetRTSCTSRsvTime(pDevice, 2, byPktType, cbFrameSize, wCurrentRate));//2:RTSTxRrvTime_aa, 0:5GHz
                pBuf->wTxRrvTime = cpu_to_le16((WORD)s_uGetTxRsvTime(pDevice, byPktType, cbFrameSize, wCurrentRate, bNeedACK));//0:OFDM
            }
            //Fill RTS
            s_vFillRTSHead(pDevice, byPktType, pvRTS, cbFrameSize, bNeedACK, bDisCRC, psEthHeader, wCurrentRate, byFBOption);
        }
        else if (pvRTS == NULL) {//RTS_needless, non PCF mode
            //Fill RsvTime
            if (pvRrvTime) {
                PSRrvTime_ab pBuf = (PSRrvTime_ab)pvRrvTime;
                pBuf->wTxRrvTime = cpu_to_le16((WORD)s_uGetTxRsvTime(pDevice, PK_TYPE_11A, cbFrameSize, wCurrentRate, bNeedACK)); //0:OFDM
            }
        }
    }
    else if (byPktType == PK_TYPE_11B) {

        if ((pvRTS != NULL)) {//RTS_need, non PCF mode
            //Fill RsvTime
            if (pvRrvTime) {
                PSRrvTime_ab pBuf = (PSRrvTime_ab)pvRrvTime;
                pBuf->wRTSTxRrvTime = cpu_to_le16((WORD)s_uGetRTSCTSRsvTime(pDevice, 0, byPktType, cbFrameSize, wCurrentRate));//0:RTSTxRrvTime_bb, 1:2.4GHz
                pBuf->wTxRrvTime = cpu_to_le16((WORD)s_uGetTxRsvTime(pDevice, PK_TYPE_11B, cbFrameSize, wCurrentRate, bNeedACK));//1:CCK
            }
            //Fill RTS
            s_vFillRTSHead(pDevice, byPktType, pvRTS, cbFrameSize, bNeedACK, bDisCRC, psEthHeader, wCurrentRate, byFBOption);
        }
        else { //RTS_needless, non PCF mode
            //Fill RsvTime
            if (pvRrvTime) {
                PSRrvTime_ab pBuf = (PSRrvTime_ab)pvRrvTime;
                pBuf->wTxRrvTime = cpu_to_le16((WORD)s_uGetTxRsvTime(pDevice, PK_TYPE_11B, cbFrameSize, wCurrentRate, bNeedACK)); //1:CCK
            }
        }
    }
    //DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"s_vGenerateTxParameter END.\n");
}
/*
    PBYTE pbyBuffer,//point to pTxBufHead
    WORD  wFragType,//00:Non-Frag, 01:Start, 02:Mid, 03:Last
    unsigned int  cbFragmentSize,//Hdr+payoad+FCS
*/


BOOL
s_bPacketToWirelessUsb(
      PSDevice         pDevice,
      BYTE             byPktType,
      PBYTE            usbPacketBuf,
      BOOL             bNeedEncryption,
      unsigned int             uSkbPacketLen,
      unsigned int             uDMAIdx,
      PSEthernetHeader psEthHeader,
      PBYTE            pPacket,
      PSKeyItem        pTransmitKey,
      unsigned int             uNodeIndex,
      WORD             wCurrentRate,
     unsigned int             *pcbHeaderLen,
     unsigned int             *pcbTotalLen
    )
{
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);
    unsigned int cbFrameSize, cbFrameBodySize;
    PTX_BUFFER          pTxBufHead;
    unsigned int cb802_1_H_len;
    unsigned int cbIVlen = 0, cbICVlen = 0, cbMIClen = 0,
	    cbMACHdLen = 0, cbFCSlen = 4;
    unsigned int cbMICHDR = 0;
    BOOL                bNeedACK,bRTS;
    PBYTE               pbyType,pbyMacHdr,pbyIVHead,pbyPayloadHead,pbyTxBufferAddr;
    BYTE abySNAP_RFC1042[ETH_ALEN] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00};
    BYTE abySNAP_Bridgetunnel[ETH_ALEN] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0xF8};
    unsigned int uDuration;
    unsigned int cbHeaderLength = 0, uPadding = 0;
    void *pvRrvTime;
    PSMICHDRHead        pMICHDR;
    void *pvRTS;
    void *pvCTS;
    void *pvTxDataHd;
    BYTE                byFBOption = AUTO_FB_NONE,byFragType;
    WORD                wTxBufSize;
    DWORD               dwMICKey0,dwMICKey1,dwMIC_Priority,dwCRC;
    PDWORD              pdwMIC_L,pdwMIC_R;
    BOOL                bSoftWEP = FALSE;




    pvRrvTime = pMICHDR = pvRTS = pvCTS = pvTxDataHd = NULL;
	if (bNeedEncryption && pTransmitKey->pvKeyTable) {
		if (((PSKeyTable)&pTransmitKey->pvKeyTable)->bSoftWEP == TRUE)
			bSoftWEP = TRUE; /* WEP 256 */
	}

    pTxBufHead = (PTX_BUFFER) usbPacketBuf;
    memset(pTxBufHead, 0, sizeof(TX_BUFFER));

    // Get pkt type
    if (ntohs(psEthHeader->wType) > ETH_DATA_LEN) {
        if (pDevice->dwDiagRefCount == 0) {
            cb802_1_H_len = 8;
        } else {
            cb802_1_H_len = 2;
        }
    } else {
        cb802_1_H_len = 0;
    }

    cbFrameBodySize = uSkbPacketLen - ETH_HLEN + cb802_1_H_len;

    //Set packet type
    pTxBufHead->wFIFOCtl |= (WORD)(byPktType<<8);

    if (pDevice->dwDiagRefCount != 0) {
        bNeedACK = FALSE;
        pTxBufHead->wFIFOCtl = pTxBufHead->wFIFOCtl & (~FIFOCTL_NEEDACK);
    } else { //if (pDevice->dwDiagRefCount != 0) {
	if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
	    (pDevice->eOPMode == OP_MODE_AP)) {
		if (is_multicast_ether_addr(psEthHeader->abyDstAddr)) {
			bNeedACK = FALSE;
			pTxBufHead->wFIFOCtl =
				pTxBufHead->wFIFOCtl & (~FIFOCTL_NEEDACK);
		} else {
			bNeedACK = TRUE;
			pTxBufHead->wFIFOCtl |= FIFOCTL_NEEDACK;
		}
        }
        else {
            // MSDUs in Infra mode always need ACK
            bNeedACK = TRUE;
            pTxBufHead->wFIFOCtl |= FIFOCTL_NEEDACK;
        }
    } //if (pDevice->dwDiagRefCount != 0) {

    pTxBufHead->wTimeStamp = DEFAULT_MSDU_LIFETIME_RES_64us;

    //Set FIFOCTL_LHEAD
    if (pDevice->bLongHeader)
        pTxBufHead->wFIFOCtl |= FIFOCTL_LHEAD;

    if (pDevice->bSoftwareGenCrcErr) {
        pTxBufHead->wFIFOCtl |= FIFOCTL_CRCDIS; // set tx descriptors to NO hardware CRC
    }

    //Set FRAGCTL_MACHDCNT
    if (pDevice->bLongHeader) {
        cbMACHdLen = WLAN_HDR_ADDR3_LEN + 6;
    } else {
        cbMACHdLen = WLAN_HDR_ADDR3_LEN;
    }
    pTxBufHead->wFragCtl |= (WORD)(cbMACHdLen << 10);

    //Set FIFOCTL_GrpAckPolicy
    if (pDevice->bGrpAckPolicy == TRUE) {//0000 0100 0000 0000
        pTxBufHead->wFIFOCtl |=	FIFOCTL_GRPACK;
    }

    //Set Auto Fallback Ctl
    if (wCurrentRate >= RATE_18M) {
        if (pDevice->byAutoFBCtrl == AUTO_FB_0) {
            pTxBufHead->wFIFOCtl |= FIFOCTL_AUTO_FB_0;
            byFBOption = AUTO_FB_0;
        } else if (pDevice->byAutoFBCtrl == AUTO_FB_1) {
            pTxBufHead->wFIFOCtl |= FIFOCTL_AUTO_FB_1;
            byFBOption = AUTO_FB_1;
        }
    }

    if (bSoftWEP != TRUE) {
        if ((bNeedEncryption) && (pTransmitKey != NULL))  { //WEP enabled
            if (pTransmitKey->byCipherSuite == KEY_CTL_WEP) { //WEP40 or WEP104
                pTxBufHead->wFragCtl |= FRAGCTL_LEGACY;
            }
            if (pTransmitKey->byCipherSuite == KEY_CTL_TKIP) {
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Tx Set wFragCtl == FRAGCTL_TKIP\n");
                pTxBufHead->wFragCtl |= FRAGCTL_TKIP;
            }
            else if (pTransmitKey->byCipherSuite == KEY_CTL_CCMP) { //CCMP
                pTxBufHead->wFragCtl |= FRAGCTL_AES;
            }
        }
    }


    if ((bNeedEncryption) && (pTransmitKey != NULL))  {
        if (pTransmitKey->byCipherSuite == KEY_CTL_WEP) {
            cbIVlen = 4;
            cbICVlen = 4;
        }
        else if (pTransmitKey->byCipherSuite == KEY_CTL_TKIP) {
            cbIVlen = 8;//IV+ExtIV
            cbMIClen = 8;
            cbICVlen = 4;
        }
        if (pTransmitKey->byCipherSuite == KEY_CTL_CCMP) {
            cbIVlen = 8;//RSN Header
            cbICVlen = 8;//MIC
            cbMICHDR = sizeof(SMICHDRHead);
        }
        if (bSoftWEP == FALSE) {
            //MAC Header should be padding 0 to DW alignment.
            uPadding = 4 - (cbMACHdLen%4);
            uPadding %= 4;
        }
    }

    cbFrameSize = cbMACHdLen + cbIVlen + (cbFrameBodySize + cbMIClen) + cbICVlen + cbFCSlen;

    if ( (bNeedACK == FALSE) ||(cbFrameSize < pDevice->wRTSThreshold) ) {
        bRTS = FALSE;
    } else {
        bRTS = TRUE;
        pTxBufHead->wFIFOCtl |= (FIFOCTL_RTS | FIFOCTL_LRETRY);
    }

    pbyTxBufferAddr = (PBYTE) &(pTxBufHead->adwTxKey[0]);
    wTxBufSize = sizeof(STxBufHead);
    if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {//802.11g packet
        if (byFBOption == AUTO_FB_NONE) {
            if (bRTS == TRUE) {//RTS_need
                pvRrvTime = (PSRrvTime_gRTS) (pbyTxBufferAddr + wTxBufSize);
                pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gRTS));
                pvRTS = (PSRTS_g) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gRTS) + cbMICHDR);
                pvCTS = NULL;
                pvTxDataHd = (PSTxDataHead_g) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gRTS) + cbMICHDR + sizeof(SRTS_g));
                cbHeaderLength = wTxBufSize + sizeof(SRrvTime_gRTS) + cbMICHDR + sizeof(SRTS_g) + sizeof(STxDataHead_g);
            }
            else { //RTS_needless
                pvRrvTime = (PSRrvTime_gCTS) (pbyTxBufferAddr + wTxBufSize);
                pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS));
                pvRTS = NULL;
                pvCTS = (PSCTS) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR);
                pvTxDataHd = (PSTxDataHead_g) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR + sizeof(SCTS));
                cbHeaderLength = wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR + sizeof(SCTS) + sizeof(STxDataHead_g);
            }
        } else {
            // Auto Fall Back
            if (bRTS == TRUE) {//RTS_need
                pvRrvTime = (PSRrvTime_gRTS) (pbyTxBufferAddr + wTxBufSize);
                pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gRTS));
                pvRTS = (PSRTS_g_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gRTS) + cbMICHDR);
                pvCTS = NULL;
                pvTxDataHd = (PSTxDataHead_g_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gRTS) + cbMICHDR + sizeof(SRTS_g_FB));
                cbHeaderLength = wTxBufSize + sizeof(SRrvTime_gRTS) + cbMICHDR + sizeof(SRTS_g_FB) + sizeof(STxDataHead_g_FB);
            }
            else if (bRTS == FALSE) { //RTS_needless
                pvRrvTime = (PSRrvTime_gCTS) (pbyTxBufferAddr + wTxBufSize);
                pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS));
                pvRTS = NULL;
                pvCTS = (PSCTS_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR);
                pvTxDataHd = (PSTxDataHead_g_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR + sizeof(SCTS_FB));
                cbHeaderLength = wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR + sizeof(SCTS_FB) + sizeof(STxDataHead_g_FB);
            }
        } // Auto Fall Back
    }
    else {//802.11a/b packet
        if (byFBOption == AUTO_FB_NONE) {
            if (bRTS == TRUE) {//RTS_need
                pvRrvTime = (PSRrvTime_ab) (pbyTxBufferAddr + wTxBufSize);
                pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab));
                pvRTS = (PSRTS_ab) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR);
                pvCTS = NULL;
                pvTxDataHd = (PSTxDataHead_ab) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR + sizeof(SRTS_ab));
                cbHeaderLength = wTxBufSize + sizeof(PSRrvTime_ab) + cbMICHDR + sizeof(SRTS_ab) + sizeof(STxDataHead_ab);
            }
            else if (bRTS == FALSE) { //RTS_needless, no MICHDR
                pvRrvTime = (PSRrvTime_ab) (pbyTxBufferAddr + wTxBufSize);
                pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab));
                pvRTS = NULL;
                pvCTS = NULL;
                pvTxDataHd = (PSTxDataHead_ab) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR);
                cbHeaderLength = wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR + sizeof(STxDataHead_ab);
            }
        } else {
            // Auto Fall Back
            if (bRTS == TRUE) {//RTS_need
                pvRrvTime = (PSRrvTime_ab) (pbyTxBufferAddr + wTxBufSize);
                pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab));
                pvRTS = (PSRTS_a_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR);
                pvCTS = NULL;
                pvTxDataHd = (PSTxDataHead_a_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR + sizeof(SRTS_a_FB));
                cbHeaderLength = wTxBufSize + sizeof(PSRrvTime_ab) + cbMICHDR + sizeof(SRTS_a_FB) + sizeof(STxDataHead_a_FB);
            }
            else if (bRTS == FALSE) { //RTS_needless
                pvRrvTime = (PSRrvTime_ab) (pbyTxBufferAddr + wTxBufSize);
                pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab));
                pvRTS = NULL;
                pvCTS = NULL;
                pvTxDataHd = (PSTxDataHead_a_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR);
                cbHeaderLength = wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR + sizeof(STxDataHead_a_FB);
            }
        } // Auto Fall Back
    }

    pbyMacHdr = (PBYTE)(pbyTxBufferAddr + cbHeaderLength);
    pbyIVHead = (PBYTE)(pbyMacHdr + cbMACHdLen + uPadding);
    pbyPayloadHead = (PBYTE)(pbyMacHdr + cbMACHdLen + uPadding + cbIVlen);


    //=========================
    //    No Fragmentation
    //=========================
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"No Fragmentation...\n");
    byFragType = FRAGCTL_NONFRAG;
    //uDMAIdx = TYPE_AC0DMA;
    //pTxBufHead = (PSTxBufHead) &(pTxBufHead->adwTxKey[0]);


    //Fill FIFO,RrvTime,RTS,and CTS
    s_vGenerateTxParameter(pDevice, byPktType, wCurrentRate,
			   (void *)pbyTxBufferAddr, pvRrvTime, pvRTS, pvCTS,
                               cbFrameSize, bNeedACK, uDMAIdx, psEthHeader);
    //Fill DataHead
    uDuration = s_uFillDataHead(pDevice, byPktType, wCurrentRate, pvTxDataHd, cbFrameSize, uDMAIdx, bNeedACK,
                                    0, 0, 1/*uMACfragNum*/, byFBOption);
    // Generate TX MAC Header
    s_vGenerateMACHeader(pDevice, pbyMacHdr, (WORD)uDuration, psEthHeader, bNeedEncryption,
                           byFragType, uDMAIdx, 0);

    if (bNeedEncryption == TRUE) {
        //Fill TXKEY
        s_vFillTxKey(pDevice, (PBYTE)(pTxBufHead->adwTxKey), pbyIVHead, pTransmitKey,
                         pbyMacHdr, (WORD)cbFrameBodySize, (PBYTE)pMICHDR);

        if (pDevice->bEnableHostWEP) {
            pMgmt->sNodeDBTable[uNodeIndex].dwTSC47_16 = pTransmitKey->dwTSC47_16;
            pMgmt->sNodeDBTable[uNodeIndex].wTSC15_0 = pTransmitKey->wTSC15_0;
        }
    }

    // 802.1H
    if (ntohs(psEthHeader->wType) > ETH_DATA_LEN) {
	if (pDevice->dwDiagRefCount == 0) {
		if ((psEthHeader->wType == cpu_to_be16(ETH_P_IPX)) ||
		    (psEthHeader->wType == cpu_to_le16(0xF380))) {
			memcpy((PBYTE) (pbyPayloadHead),
			       abySNAP_Bridgetunnel, 6);
            } else {
                memcpy((PBYTE) (pbyPayloadHead), &abySNAP_RFC1042[0], 6);
            }
            pbyType = (PBYTE) (pbyPayloadHead + 6);
            memcpy(pbyType, &(psEthHeader->wType), sizeof(WORD));
        } else {
            memcpy((PBYTE) (pbyPayloadHead), &(psEthHeader->wType), sizeof(WORD));

        }

    }


    if (pPacket != NULL) {
        // Copy the Packet into a tx Buffer
        memcpy((pbyPayloadHead + cb802_1_H_len),
                 (pPacket + ETH_HLEN),
                 uSkbPacketLen - ETH_HLEN
                 );

    } else {
        // while bRelayPacketSend psEthHeader is point to header+payload
        memcpy((pbyPayloadHead + cb802_1_H_len), ((PBYTE)psEthHeader) + ETH_HLEN, uSkbPacketLen - ETH_HLEN);
    }

    ASSERT(uLength == cbNdisBodySize);

    if ((bNeedEncryption == TRUE) && (pTransmitKey != NULL) && (pTransmitKey->byCipherSuite == KEY_CTL_TKIP)) {

        ///////////////////////////////////////////////////////////////////

        if (pDevice->sMgmtObj.eAuthenMode == WMAC_AUTH_WPANONE) {
            dwMICKey0 = *(PDWORD)(&pTransmitKey->abyKey[16]);
            dwMICKey1 = *(PDWORD)(&pTransmitKey->abyKey[20]);
        }
        else if ((pTransmitKey->dwKeyIndex & AUTHENTICATOR_KEY) != 0) {
            dwMICKey0 = *(PDWORD)(&pTransmitKey->abyKey[16]);
            dwMICKey1 = *(PDWORD)(&pTransmitKey->abyKey[20]);
        }
        else {
            dwMICKey0 = *(PDWORD)(&pTransmitKey->abyKey[24]);
            dwMICKey1 = *(PDWORD)(&pTransmitKey->abyKey[28]);
        }
        // DO Software Michael
        MIC_vInit(dwMICKey0, dwMICKey1);
        MIC_vAppend((PBYTE)&(psEthHeader->abyDstAddr[0]), 12);
        dwMIC_Priority = 0;
        MIC_vAppend((PBYTE)&dwMIC_Priority, 4);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"MIC KEY: %lX, %lX\n", dwMICKey0, dwMICKey1);

        ///////////////////////////////////////////////////////////////////

        //DBG_PRN_GRP12(("Length:%d, %d\n", cbFrameBodySize, uFromHDtoPLDLength));
        //for (ii = 0; ii < cbFrameBodySize; ii++) {
        //    DBG_PRN_GRP12(("%02x ", *((PBYTE)((pbyPayloadHead + cb802_1_H_len) + ii))));
        //}
        //DBG_PRN_GRP12(("\n\n\n"));

        MIC_vAppend(pbyPayloadHead, cbFrameBodySize);

        pdwMIC_L = (PDWORD)(pbyPayloadHead + cbFrameBodySize);
        pdwMIC_R = (PDWORD)(pbyPayloadHead + cbFrameBodySize + 4);

        MIC_vGetMIC(pdwMIC_L, pdwMIC_R);
        MIC_vUnInit();

        if (pDevice->bTxMICFail == TRUE) {
            *pdwMIC_L = 0;
            *pdwMIC_R = 0;
            pDevice->bTxMICFail = FALSE;
        }
        //DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"uLength: %d, %d\n", uLength, cbFrameBodySize);
        //DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"cbReqCount:%d, %d, %d, %d\n", cbReqCount, cbHeaderLength, uPadding, cbIVlen);
        //DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"MIC:%lX, %lX\n", *pdwMIC_L, *pdwMIC_R);
    }


    if (bSoftWEP == TRUE) {

        s_vSWencryption(pDevice, pTransmitKey, (pbyPayloadHead), (WORD)(cbFrameBodySize + cbMIClen));

    } else if (  ((pDevice->eEncryptionStatus == Ndis802_11Encryption1Enabled) && (bNeedEncryption == TRUE))  ||
          ((pDevice->eEncryptionStatus == Ndis802_11Encryption2Enabled) && (bNeedEncryption == TRUE))   ||
          ((pDevice->eEncryptionStatus == Ndis802_11Encryption3Enabled) && (bNeedEncryption == TRUE))      ) {
        cbFrameSize -= cbICVlen;
    }

    if (pDevice->bSoftwareGenCrcErr == TRUE) {
	unsigned int cbLen;
        PDWORD pdwCRC;

        dwCRC = 0xFFFFFFFFL;
        cbLen = cbFrameSize - cbFCSlen;
        // calculate CRC, and wrtie CRC value to end of TD
        dwCRC = CRCdwGetCrc32Ex(pbyMacHdr, cbLen, dwCRC);
        pdwCRC = (PDWORD)(pbyMacHdr + cbLen);
        // finally, we must invert dwCRC to get the correct answer
        *pdwCRC = ~dwCRC;
        // Force Error
        *pdwCRC -= 1;
    } else {
        cbFrameSize -= cbFCSlen;
    }

    *pcbHeaderLen = cbHeaderLength;
    *pcbTotalLen = cbHeaderLength + cbFrameSize ;


    //Set FragCtl in TxBufferHead
    pTxBufHead->wFragCtl |= (WORD)byFragType;


    return TRUE;

}


/*+
 *
 * Description:
 *      Translate 802.3 to 802.11 header
 *
 * Parameters:
 *  In:
 *      pDevice         - Pointer to adapter
 *      dwTxBufferAddr  - Transmit Buffer
 *      pPacket         - Packet from upper layer
 *      cbPacketSize    - Transmit Data Length
 *  Out:
 *      pcbHeadSize         - Header size of MAC&Baseband control and 802.11 Header
 *      pcbAppendPayload    - size of append payload for 802.1H translation
 *
 * Return Value: none
 *
-*/

void
s_vGenerateMACHeader (
     PSDevice         pDevice,
     PBYTE            pbyBufferAddr,
     WORD             wDuration,
     PSEthernetHeader psEthHeader,
     BOOL             bNeedEncrypt,
     WORD             wFragType,
     unsigned int             uDMAIdx,
     unsigned int             uFragIdx
    )
{
    PS802_11Header  pMACHeader = (PS802_11Header)pbyBufferAddr;

    memset(pMACHeader, 0, (sizeof(S802_11Header)));  //- sizeof(pMACHeader->dwIV)));

    if (uDMAIdx == TYPE_ATIMDMA) {
    	pMACHeader->wFrameCtl = TYPE_802_11_ATIM;
    } else {
        pMACHeader->wFrameCtl = TYPE_802_11_DATA;
    }

    if (pDevice->eOPMode == OP_MODE_AP) {
	memcpy(&(pMACHeader->abyAddr1[0]),
	       &(psEthHeader->abyDstAddr[0]),
	       ETH_ALEN);
	memcpy(&(pMACHeader->abyAddr2[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
	memcpy(&(pMACHeader->abyAddr3[0]),
	       &(psEthHeader->abySrcAddr[0]),
	       ETH_ALEN);
        pMACHeader->wFrameCtl |= FC_FROMDS;
    } else {
	if (pDevice->eOPMode == OP_MODE_ADHOC) {
		memcpy(&(pMACHeader->abyAddr1[0]),
		       &(psEthHeader->abyDstAddr[0]),
		       ETH_ALEN);
		memcpy(&(pMACHeader->abyAddr2[0]),
		       &(psEthHeader->abySrcAddr[0]),
		       ETH_ALEN);
		memcpy(&(pMACHeader->abyAddr3[0]),
		       &(pDevice->abyBSSID[0]),
		       ETH_ALEN);
	} else {
		memcpy(&(pMACHeader->abyAddr3[0]),
		       &(psEthHeader->abyDstAddr[0]),
		       ETH_ALEN);
		memcpy(&(pMACHeader->abyAddr2[0]),
		       &(psEthHeader->abySrcAddr[0]),
		       ETH_ALEN);
		memcpy(&(pMACHeader->abyAddr1[0]),
		       &(pDevice->abyBSSID[0]),
		       ETH_ALEN);
            pMACHeader->wFrameCtl |= FC_TODS;
        }
    }

    if (bNeedEncrypt)
        pMACHeader->wFrameCtl |= cpu_to_le16((WORD)WLAN_SET_FC_ISWEP(1));

    pMACHeader->wDurationID = cpu_to_le16(wDuration);

    if (pDevice->bLongHeader) {
        PWLAN_80211HDR_A4 pMACA4Header  = (PWLAN_80211HDR_A4) pbyBufferAddr;
        pMACHeader->wFrameCtl |= (FC_TODS | FC_FROMDS);
        memcpy(pMACA4Header->abyAddr4, pDevice->abyBSSID, WLAN_ADDR_LEN);
    }
    pMACHeader->wSeqCtl = cpu_to_le16(pDevice->wSeqCounter << 4);

    //Set FragNumber in Sequence Control
    pMACHeader->wSeqCtl |= cpu_to_le16((WORD)uFragIdx);

    if ((wFragType == FRAGCTL_ENDFRAG) || (wFragType == FRAGCTL_NONFRAG)) {
        pDevice->wSeqCounter++;
        if (pDevice->wSeqCounter > 0x0fff)
            pDevice->wSeqCounter = 0;
    }

    if ((wFragType == FRAGCTL_STAFRAG) || (wFragType == FRAGCTL_MIDFRAG)) { //StartFrag or MidFrag
        pMACHeader->wFrameCtl |= FC_MOREFRAG;
    }
}



/*+
 *
 * Description:
 *      Request instructs a MAC to transmit a 802.11 management packet through
 *      the adapter onto the medium.
 *
 * Parameters:
 *  In:
 *      hDeviceContext  - Pointer to the adapter
 *      pPacket         - A pointer to a descriptor for the packet to transmit
 *  Out:
 *      none
 *
 * Return Value: CMD_STATUS_PENDING if MAC Tx resource available; otherwise FALSE
 *
-*/

CMD_STATUS csMgmt_xmit(
      PSDevice pDevice,
      PSTxMgmtPacket pPacket
    )
{
    BYTE            byPktType;
    PBYTE           pbyTxBufferAddr;
    void *pvRTS;
    PSCTS           pCTS;
    void *pvTxDataHd;
    unsigned int            uDuration;
    unsigned int            cbReqCount;
    PS802_11Header  pMACHeader;
    unsigned int            cbHeaderSize;
    unsigned int            cbFrameBodySize;
    BOOL            bNeedACK;
    BOOL            bIsPSPOLL = FALSE;
    PSTxBufHead     pTxBufHead;
    unsigned int            cbFrameSize;
    unsigned int            cbIVlen = 0;
    unsigned int            cbICVlen = 0;
    unsigned int            cbMIClen = 0;
    unsigned int            cbFCSlen = 4;
    unsigned int            uPadding = 0;
    WORD            wTxBufSize;
    unsigned int            cbMacHdLen;
    SEthernetHeader sEthHeader;
    void *pvRrvTime;
    void *pMICHDR;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    WORD            wCurrentRate = RATE_1M;
    PTX_BUFFER          pTX_Buffer;
    PUSB_SEND_CONTEXT   pContext;



    pContext = (PUSB_SEND_CONTEXT)s_vGetFreeContext(pDevice);

    if (NULL == pContext) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"ManagementSend TX...NO CONTEXT!\n");
        return CMD_STATUS_RESOURCES;
    }

    pTX_Buffer = (PTX_BUFFER) (&pContext->Data[0]);
    pbyTxBufferAddr = (PBYTE)&(pTX_Buffer->adwTxKey[0]);
    cbFrameBodySize = pPacket->cbPayloadLen;
    pTxBufHead = (PSTxBufHead) pbyTxBufferAddr;
    wTxBufSize = sizeof(STxBufHead);
    memset(pTxBufHead, 0, wTxBufSize);

    if (pDevice->byBBType == BB_TYPE_11A) {
        wCurrentRate = RATE_6M;
        byPktType = PK_TYPE_11A;
    } else {
        wCurrentRate = RATE_1M;
        byPktType = PK_TYPE_11B;
    }

    // SetPower will cause error power TX state for OFDM Date packet in TX buffer.
    // 2004.11.11 Kyle -- Using OFDM power to tx MngPkt will decrease the connection capability.
    //                    And cmd timer will wait data pkt TX finish before scanning so it's OK
    //                    to set power here.
    if (pMgmt->eScanState != WMAC_NO_SCANNING) {
        RFbSetPower(pDevice, wCurrentRate, pDevice->byCurrentCh);
    } else {
        RFbSetPower(pDevice, wCurrentRate, pMgmt->uCurrChannel);
    }
    pDevice->wCurrentRate = wCurrentRate;


    //Set packet type
    if (byPktType == PK_TYPE_11A) {//0000 0000 0000 0000
        pTxBufHead->wFIFOCtl = 0;
    }
    else if (byPktType == PK_TYPE_11B) {//0000 0001 0000 0000
        pTxBufHead->wFIFOCtl |= FIFOCTL_11B;
    }
    else if (byPktType == PK_TYPE_11GB) {//0000 0010 0000 0000
        pTxBufHead->wFIFOCtl |= FIFOCTL_11GB;
    }
    else if (byPktType == PK_TYPE_11GA) {//0000 0011 0000 0000
        pTxBufHead->wFIFOCtl |= FIFOCTL_11GA;
    }

    pTxBufHead->wFIFOCtl |= FIFOCTL_TMOEN;
    pTxBufHead->wTimeStamp = cpu_to_le16(DEFAULT_MGN_LIFETIME_RES_64us);

    if (is_multicast_ether_addr(pPacket->p80211Header->sA3.abyAddr1)) {
        bNeedACK = FALSE;
    }
    else {
        bNeedACK = TRUE;
        pTxBufHead->wFIFOCtl |= FIFOCTL_NEEDACK;
    };

    if ((pMgmt->eCurrMode == WMAC_MODE_ESS_AP) ||
        (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) ) {

        pTxBufHead->wFIFOCtl |= FIFOCTL_LRETRY;
        //Set Preamble type always long
        //pDevice->byPreambleType = PREAMBLE_LONG;
        // probe-response don't retry
        //if ((pPacket->p80211Header->sA4.wFrameCtl & TYPE_SUBTYPE_MASK) == TYPE_MGMT_PROBE_RSP) {
        //     bNeedACK = FALSE;
        //     pTxBufHead->wFIFOCtl  &= (~FIFOCTL_NEEDACK);
        //}
    }

    pTxBufHead->wFIFOCtl |= (FIFOCTL_GENINT | FIFOCTL_ISDMA0);

    if ((pPacket->p80211Header->sA4.wFrameCtl & TYPE_SUBTYPE_MASK) == TYPE_CTL_PSPOLL) {
        bIsPSPOLL = TRUE;
        cbMacHdLen = WLAN_HDR_ADDR2_LEN;
    } else {
        cbMacHdLen = WLAN_HDR_ADDR3_LEN;
    }

    //Set FRAGCTL_MACHDCNT
    pTxBufHead->wFragCtl |= cpu_to_le16((WORD)(cbMacHdLen << 10));

    // Notes:
    // Although spec says MMPDU can be fragmented; In most case,
    // no one will send a MMPDU under fragmentation. With RTS may occur.
    pDevice->bAES = FALSE;  //Set FRAGCTL_WEPTYP

    if (WLAN_GET_FC_ISWEP(pPacket->p80211Header->sA4.wFrameCtl) != 0) {
        if (pDevice->eEncryptionStatus == Ndis802_11Encryption1Enabled) {
            cbIVlen = 4;
            cbICVlen = 4;
    	    pTxBufHead->wFragCtl |= FRAGCTL_LEGACY;
        }
        else if (pDevice->eEncryptionStatus == Ndis802_11Encryption2Enabled) {
            cbIVlen = 8;//IV+ExtIV
            cbMIClen = 8;
            cbICVlen = 4;
    	    pTxBufHead->wFragCtl |= FRAGCTL_TKIP;
    	    //We need to get seed here for filling TxKey entry.
            //TKIPvMixKey(pTransmitKey->abyKey, pDevice->abyCurrentNetAddr,
            //            pTransmitKey->wTSC15_0, pTransmitKey->dwTSC47_16, pDevice->abyPRNG);
        }
        else if (pDevice->eEncryptionStatus == Ndis802_11Encryption3Enabled) {
            cbIVlen = 8;//RSN Header
            cbICVlen = 8;//MIC
            pTxBufHead->wFragCtl |= FRAGCTL_AES;
            pDevice->bAES = TRUE;
        }
        //MAC Header should be padding 0 to DW alignment.
        uPadding = 4 - (cbMacHdLen%4);
        uPadding %= 4;
    }

    cbFrameSize = cbMacHdLen + cbFrameBodySize + cbIVlen + cbMIClen + cbICVlen + cbFCSlen;

    //Set FIFOCTL_GrpAckPolicy
    if (pDevice->bGrpAckPolicy == TRUE) {//0000 0100 0000 0000
        pTxBufHead->wFIFOCtl |=	FIFOCTL_GRPACK;
    }
    //the rest of pTxBufHead->wFragCtl:FragTyp will be set later in s_vFillFragParameter()

    //Set RrvTime/RTS/CTS Buffer
    if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {//802.11g packet

        pvRrvTime = (PSRrvTime_gCTS) (pbyTxBufferAddr + wTxBufSize);
        pMICHDR = NULL;
        pvRTS = NULL;
        pCTS = (PSCTS) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS));
        pvTxDataHd = (PSTxDataHead_g) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS) + sizeof(SCTS));
        cbHeaderSize = wTxBufSize + sizeof(SRrvTime_gCTS) + sizeof(SCTS) + sizeof(STxDataHead_g);
    }
    else { // 802.11a/b packet
        pvRrvTime = (PSRrvTime_ab) (pbyTxBufferAddr + wTxBufSize);
        pMICHDR = NULL;
        pvRTS = NULL;
        pCTS = NULL;
        pvTxDataHd = (PSTxDataHead_ab) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab));
        cbHeaderSize = wTxBufSize + sizeof(SRrvTime_ab) + sizeof(STxDataHead_ab);
    }

    memset((void *)(pbyTxBufferAddr + wTxBufSize), 0,
	   (cbHeaderSize - wTxBufSize));

    memcpy(&(sEthHeader.abyDstAddr[0]),
	   &(pPacket->p80211Header->sA3.abyAddr1[0]),
	   ETH_ALEN);
    memcpy(&(sEthHeader.abySrcAddr[0]),
	   &(pPacket->p80211Header->sA3.abyAddr2[0]),
	   ETH_ALEN);
    //=========================
    //    No Fragmentation
    //=========================
    pTxBufHead->wFragCtl |= (WORD)FRAGCTL_NONFRAG;


    //Fill FIFO,RrvTime,RTS,and CTS
    s_vGenerateTxParameter(pDevice, byPktType, wCurrentRate,  pbyTxBufferAddr, pvRrvTime, pvRTS, pCTS,
                           cbFrameSize, bNeedACK, TYPE_TXDMA0, &sEthHeader);

    //Fill DataHead
    uDuration = s_uFillDataHead(pDevice, byPktType, wCurrentRate, pvTxDataHd, cbFrameSize, TYPE_TXDMA0, bNeedACK,
                                0, 0, 1, AUTO_FB_NONE);

    pMACHeader = (PS802_11Header) (pbyTxBufferAddr + cbHeaderSize);

    cbReqCount = cbHeaderSize + cbMacHdLen + uPadding + cbIVlen + cbFrameBodySize;

    if (WLAN_GET_FC_ISWEP(pPacket->p80211Header->sA4.wFrameCtl) != 0) {
        PBYTE           pbyIVHead;
        PBYTE           pbyPayloadHead;
        PBYTE           pbyBSSID;
        PSKeyItem       pTransmitKey = NULL;

        pbyIVHead = (PBYTE)(pbyTxBufferAddr + cbHeaderSize + cbMacHdLen + uPadding);
        pbyPayloadHead = (PBYTE)(pbyTxBufferAddr + cbHeaderSize + cbMacHdLen + uPadding + cbIVlen);
        do {
            if ((pDevice->eOPMode == OP_MODE_INFRASTRUCTURE) &&
                (pDevice->bLinkPass == TRUE)) {
                pbyBSSID = pDevice->abyBSSID;
                // get pairwise key
                if (KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, PAIRWISE_KEY, &pTransmitKey) == FALSE) {
                    // get group key
                    if(KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, GROUP_KEY, &pTransmitKey) == TRUE) {
                        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Get GTK.\n");
                        break;
                    }
                } else {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Get PTK.\n");
                    break;
                }
            }
            // get group key
            pbyBSSID = pDevice->abyBroadcastAddr;
            if(KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, GROUP_KEY, &pTransmitKey) == FALSE) {
                pTransmitKey = NULL;
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"KEY is NULL. OP Mode[%d]\n", pDevice->eOPMode);
            } else {
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Get GTK.\n");
            }
        } while(FALSE);
        //Fill TXKEY
        s_vFillTxKey(pDevice, (PBYTE)(pTxBufHead->adwTxKey), pbyIVHead, pTransmitKey,
                     (PBYTE)pMACHeader, (WORD)cbFrameBodySize, NULL);

        memcpy(pMACHeader, pPacket->p80211Header, cbMacHdLen);
        memcpy(pbyPayloadHead, ((PBYTE)(pPacket->p80211Header) + cbMacHdLen),
                 cbFrameBodySize);
    }
    else {
        // Copy the Packet into a tx Buffer
        memcpy(pMACHeader, pPacket->p80211Header, pPacket->cbMPDULen);
    }

    pMACHeader->wSeqCtl = cpu_to_le16(pDevice->wSeqCounter << 4);
    pDevice->wSeqCounter++ ;
    if (pDevice->wSeqCounter > 0x0fff)
        pDevice->wSeqCounter = 0;

    if (bIsPSPOLL) {
        // The MAC will automatically replace the Duration-field of MAC header by Duration-field
        // of FIFO control header.
        // This will cause AID-field of PS-POLL packet be incorrect (Because PS-POLL's AID field is
        // in the same place of other packet's Duration-field).
        // And it will cause Cisco-AP to issue Disassociation-packet
        if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
            ((PSTxDataHead_g)pvTxDataHd)->wDuration_a = cpu_to_le16(pPacket->p80211Header->sA2.wDurationID);
            ((PSTxDataHead_g)pvTxDataHd)->wDuration_b = cpu_to_le16(pPacket->p80211Header->sA2.wDurationID);
        } else {
            ((PSTxDataHead_ab)pvTxDataHd)->wDuration = cpu_to_le16(pPacket->p80211Header->sA2.wDurationID);
        }
    }


    pTX_Buffer->wTxByteCount = cpu_to_le16((WORD)(cbReqCount));
    pTX_Buffer->byPKTNO = (BYTE) (((wCurrentRate<<4) &0x00F0) | ((pDevice->wSeqCounter - 1) & 0x000F));
    pTX_Buffer->byType = 0x00;

    pContext->pPacket = NULL;
    pContext->Type = CONTEXT_MGMT_PACKET;
    pContext->uBufLen = (WORD)cbReqCount + 4;  //USB header

    if (WLAN_GET_FC_TODS(pMACHeader->wFrameCtl) == 0) {
        s_vSaveTxPktInfo(pDevice, (BYTE) (pTX_Buffer->byPKTNO & 0x0F), &(pMACHeader->abyAddr1[0]),(WORD)cbFrameSize,pTX_Buffer->wFIFOCtl);
    }
    else {
        s_vSaveTxPktInfo(pDevice, (BYTE) (pTX_Buffer->byPKTNO & 0x0F), &(pMACHeader->abyAddr3[0]),(WORD)cbFrameSize,pTX_Buffer->wFIFOCtl);
    }

    PIPEnsSendBulkOut(pDevice,pContext);
    return CMD_STATUS_PENDING;
}


CMD_STATUS
csBeacon_xmit(
      PSDevice pDevice,
      PSTxMgmtPacket pPacket
    )
{

    unsigned int                cbFrameSize = pPacket->cbMPDULen + WLAN_FCS_LEN;
    unsigned int                cbHeaderSize = 0;
    WORD                wTxBufSize = sizeof(STxShortBufHead);
    PSTxShortBufHead    pTxBufHead;
    PS802_11Header      pMACHeader;
    PSTxDataHead_ab     pTxDataHead;
    WORD                wCurrentRate;
    unsigned int                cbFrameBodySize;
    unsigned int                cbReqCount;
    PBEACON_BUFFER      pTX_Buffer;
    PBYTE               pbyTxBufferAddr;
    PUSB_SEND_CONTEXT   pContext;
    CMD_STATUS          status;


    pContext = (PUSB_SEND_CONTEXT)s_vGetFreeContext(pDevice);
    if (NULL == pContext) {
        status = CMD_STATUS_RESOURCES;
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"ManagementSend TX...NO CONTEXT!\n");
        return status ;
    }
    pTX_Buffer = (PBEACON_BUFFER) (&pContext->Data[0]);
    pbyTxBufferAddr = (PBYTE)&(pTX_Buffer->wFIFOCtl);

    cbFrameBodySize = pPacket->cbPayloadLen;

    pTxBufHead = (PSTxShortBufHead) pbyTxBufferAddr;
    wTxBufSize = sizeof(STxShortBufHead);
    memset(pTxBufHead, 0, wTxBufSize);

    if (pDevice->byBBType == BB_TYPE_11A) {
        wCurrentRate = RATE_6M;
        pTxDataHead = (PSTxDataHead_ab) (pbyTxBufferAddr + wTxBufSize);
        //Get SignalField,ServiceField,Length
        BBvCalculateParameter(pDevice, cbFrameSize, wCurrentRate, PK_TYPE_11A,
            (PWORD)&(pTxDataHead->wTransmitLength), (PBYTE)&(pTxDataHead->byServiceField), (PBYTE)&(pTxDataHead->bySignalField)
        );
        //Get Duration and TimeStampOff
        pTxDataHead->wDuration = cpu_to_le16((WORD)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameSize, PK_TYPE_11A,
                                                          wCurrentRate, FALSE, 0, 0, 1, AUTO_FB_NONE));
        pTxDataHead->wTimeStampOff = wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE];
        cbHeaderSize = wTxBufSize + sizeof(STxDataHead_ab);
    } else {
        wCurrentRate = RATE_1M;
        pTxBufHead->wFIFOCtl |= FIFOCTL_11B;
        pTxDataHead = (PSTxDataHead_ab) (pbyTxBufferAddr + wTxBufSize);
        //Get SignalField,ServiceField,Length
        BBvCalculateParameter(pDevice, cbFrameSize, wCurrentRate, PK_TYPE_11B,
            (PWORD)&(pTxDataHead->wTransmitLength), (PBYTE)&(pTxDataHead->byServiceField), (PBYTE)&(pTxDataHead->bySignalField)
        );
        //Get Duration and TimeStampOff
        pTxDataHead->wDuration = cpu_to_le16((WORD)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameSize, PK_TYPE_11B,
                                                          wCurrentRate, FALSE, 0, 0, 1, AUTO_FB_NONE));
        pTxDataHead->wTimeStampOff = wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE];
        cbHeaderSize = wTxBufSize + sizeof(STxDataHead_ab);
    }

    //Generate Beacon Header
    pMACHeader = (PS802_11Header)(pbyTxBufferAddr + cbHeaderSize);
    memcpy(pMACHeader, pPacket->p80211Header, pPacket->cbMPDULen);

    pMACHeader->wDurationID = 0;
    pMACHeader->wSeqCtl = cpu_to_le16(pDevice->wSeqCounter << 4);
    pDevice->wSeqCounter++ ;
    if (pDevice->wSeqCounter > 0x0fff)
        pDevice->wSeqCounter = 0;

    cbReqCount = cbHeaderSize + WLAN_HDR_ADDR3_LEN + cbFrameBodySize;

    pTX_Buffer->wTxByteCount = (WORD)cbReqCount;
    pTX_Buffer->byPKTNO = (BYTE) (((wCurrentRate<<4) &0x00F0) | ((pDevice->wSeqCounter - 1) & 0x000F));
    pTX_Buffer->byType = 0x01;

    pContext->pPacket = NULL;
    pContext->Type = CONTEXT_MGMT_PACKET;
    pContext->uBufLen = (WORD)cbReqCount + 4;  //USB header

    PIPEnsSendBulkOut(pDevice,pContext);
    return CMD_STATUS_PENDING;

}





void
vDMA0_tx_80211(PSDevice  pDevice, struct sk_buff *skb) {

    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    BYTE            byPktType;
    PBYTE           pbyTxBufferAddr;
    void *pvRTS;
    void *pvCTS;
    void *pvTxDataHd;
    unsigned int            uDuration;
    unsigned int            cbReqCount;
    PS802_11Header  pMACHeader;
    unsigned int            cbHeaderSize;
    unsigned int            cbFrameBodySize;
    BOOL            bNeedACK;
    BOOL            bIsPSPOLL = FALSE;
    PSTxBufHead     pTxBufHead;
    unsigned int            cbFrameSize;
    unsigned int            cbIVlen = 0;
    unsigned int            cbICVlen = 0;
    unsigned int            cbMIClen = 0;
    unsigned int            cbFCSlen = 4;
    unsigned int            uPadding = 0;
    unsigned int            cbMICHDR = 0;
    unsigned int            uLength = 0;
    DWORD           dwMICKey0, dwMICKey1;
    DWORD           dwMIC_Priority;
    PDWORD          pdwMIC_L;
    PDWORD          pdwMIC_R;
    WORD            wTxBufSize;
    unsigned int            cbMacHdLen;
    SEthernetHeader sEthHeader;
    void *pvRrvTime;
    void *pMICHDR;
    WORD            wCurrentRate = RATE_1M;
    PUWLAN_80211HDR  p80211Header;
    unsigned int             uNodeIndex = 0;
    BOOL            bNodeExist = FALSE;
    SKeyItem        STempKey;
    PSKeyItem       pTransmitKey = NULL;
    PBYTE           pbyIVHead;
    PBYTE           pbyPayloadHead;
    PBYTE           pbyMacHdr;
    unsigned int            cbExtSuppRate = 0;
    PTX_BUFFER          pTX_Buffer;
    PUSB_SEND_CONTEXT   pContext;
//    PWLAN_IE        pItem;


    pvRrvTime = pMICHDR = pvRTS = pvCTS = pvTxDataHd = NULL;

    if(skb->len <= WLAN_HDR_ADDR3_LEN) {
       cbFrameBodySize = 0;
    }
    else {
       cbFrameBodySize = skb->len - WLAN_HDR_ADDR3_LEN;
    }
    p80211Header = (PUWLAN_80211HDR)skb->data;

    pContext = (PUSB_SEND_CONTEXT)s_vGetFreeContext(pDevice);

    if (NULL == pContext) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"DMA0 TX...NO CONTEXT!\n");
        dev_kfree_skb_irq(skb);
        return ;
    }

    pTX_Buffer = (PTX_BUFFER)(&pContext->Data[0]);
    pbyTxBufferAddr = (PBYTE)(&pTX_Buffer->adwTxKey[0]);
    pTxBufHead = (PSTxBufHead) pbyTxBufferAddr;
    wTxBufSize = sizeof(STxBufHead);
    memset(pTxBufHead, 0, wTxBufSize);

    if (pDevice->byBBType == BB_TYPE_11A) {
        wCurrentRate = RATE_6M;
        byPktType = PK_TYPE_11A;
    } else {
        wCurrentRate = RATE_1M;
        byPktType = PK_TYPE_11B;
    }

    // SetPower will cause error power TX state for OFDM Date packet in TX buffer.
    // 2004.11.11 Kyle -- Using OFDM power to tx MngPkt will decrease the connection capability.
    //                    And cmd timer will wait data pkt TX finish before scanning so it's OK
    //                    to set power here.
    if (pMgmt->eScanState != WMAC_NO_SCANNING) {
        RFbSetPower(pDevice, wCurrentRate, pDevice->byCurrentCh);
    } else {
        RFbSetPower(pDevice, wCurrentRate, pMgmt->uCurrChannel);
    }

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"vDMA0_tx_80211: p80211Header->sA3.wFrameCtl = %x \n", p80211Header->sA3.wFrameCtl);

    //Set packet type
    if (byPktType == PK_TYPE_11A) {//0000 0000 0000 0000
        pTxBufHead->wFIFOCtl = 0;
    }
    else if (byPktType == PK_TYPE_11B) {//0000 0001 0000 0000
        pTxBufHead->wFIFOCtl |= FIFOCTL_11B;
    }
    else if (byPktType == PK_TYPE_11GB) {//0000 0010 0000 0000
        pTxBufHead->wFIFOCtl |= FIFOCTL_11GB;
    }
    else if (byPktType == PK_TYPE_11GA) {//0000 0011 0000 0000
        pTxBufHead->wFIFOCtl |= FIFOCTL_11GA;
    }

    pTxBufHead->wFIFOCtl |= FIFOCTL_TMOEN;
    pTxBufHead->wTimeStamp = cpu_to_le16(DEFAULT_MGN_LIFETIME_RES_64us);

    if (is_multicast_ether_addr(p80211Header->sA3.abyAddr1)) {
        bNeedACK = FALSE;
        if (pDevice->bEnableHostWEP) {
            uNodeIndex = 0;
            bNodeExist = TRUE;
        }
    }
    else {
        if (pDevice->bEnableHostWEP) {
            if (BSSbIsSTAInNodeDB(pDevice, (PBYTE)(p80211Header->sA3.abyAddr1), &uNodeIndex))
                bNodeExist = TRUE;
        }
        bNeedACK = TRUE;
        pTxBufHead->wFIFOCtl |= FIFOCTL_NEEDACK;
    };

    if ((pMgmt->eCurrMode == WMAC_MODE_ESS_AP) ||
        (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) ) {

        pTxBufHead->wFIFOCtl |= FIFOCTL_LRETRY;
        //Set Preamble type always long
        //pDevice->byPreambleType = PREAMBLE_LONG;

        // probe-response don't retry
        //if ((p80211Header->sA4.wFrameCtl & TYPE_SUBTYPE_MASK) == TYPE_MGMT_PROBE_RSP) {
        //     bNeedACK = FALSE;
        //     pTxBufHead->wFIFOCtl  &= (~FIFOCTL_NEEDACK);
        //}
    }

    pTxBufHead->wFIFOCtl |= (FIFOCTL_GENINT | FIFOCTL_ISDMA0);

    if ((p80211Header->sA4.wFrameCtl & TYPE_SUBTYPE_MASK) == TYPE_CTL_PSPOLL) {
        bIsPSPOLL = TRUE;
        cbMacHdLen = WLAN_HDR_ADDR2_LEN;
    } else {
        cbMacHdLen = WLAN_HDR_ADDR3_LEN;
    }

    // hostapd daemon ext support rate patch
    if (WLAN_GET_FC_FSTYPE(p80211Header->sA4.wFrameCtl) == WLAN_FSTYPE_ASSOCRESP) {

        if (((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates)->len != 0) {
            cbExtSuppRate += ((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates)->len + WLAN_IEHDR_LEN;
         }

        if (((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates)->len != 0) {
            cbExtSuppRate += ((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates)->len + WLAN_IEHDR_LEN;
         }

         if (cbExtSuppRate >0) {
            cbFrameBodySize = WLAN_ASSOCRESP_OFF_SUPP_RATES;
         }
    }


    //Set FRAGCTL_MACHDCNT
    pTxBufHead->wFragCtl |= cpu_to_le16((WORD)cbMacHdLen << 10);

    // Notes:
    // Although spec says MMPDU can be fragmented; In most case,
    // no one will send a MMPDU under fragmentation. With RTS may occur.
    pDevice->bAES = FALSE;  //Set FRAGCTL_WEPTYP


    if (WLAN_GET_FC_ISWEP(p80211Header->sA4.wFrameCtl) != 0) {
        if (pDevice->eEncryptionStatus == Ndis802_11Encryption1Enabled) {
            cbIVlen = 4;
            cbICVlen = 4;
    	    pTxBufHead->wFragCtl |= FRAGCTL_LEGACY;
        }
        else if (pDevice->eEncryptionStatus == Ndis802_11Encryption2Enabled) {
            cbIVlen = 8;//IV+ExtIV
            cbMIClen = 8;
            cbICVlen = 4;
    	    pTxBufHead->wFragCtl |= FRAGCTL_TKIP;
    	    //We need to get seed here for filling TxKey entry.
            //TKIPvMixKey(pTransmitKey->abyKey, pDevice->abyCurrentNetAddr,
            //            pTransmitKey->wTSC15_0, pTransmitKey->dwTSC47_16, pDevice->abyPRNG);
        }
        else if (pDevice->eEncryptionStatus == Ndis802_11Encryption3Enabled) {
            cbIVlen = 8;//RSN Header
            cbICVlen = 8;//MIC
            cbMICHDR = sizeof(SMICHDRHead);
            pTxBufHead->wFragCtl |= FRAGCTL_AES;
            pDevice->bAES = TRUE;
        }
        //MAC Header should be padding 0 to DW alignment.
        uPadding = 4 - (cbMacHdLen%4);
        uPadding %= 4;
    }

    cbFrameSize = cbMacHdLen + cbFrameBodySize + cbIVlen + cbMIClen + cbICVlen + cbFCSlen + cbExtSuppRate;

    //Set FIFOCTL_GrpAckPolicy
    if (pDevice->bGrpAckPolicy == TRUE) {//0000 0100 0000 0000
        pTxBufHead->wFIFOCtl |=	FIFOCTL_GRPACK;
    }
    //the rest of pTxBufHead->wFragCtl:FragTyp will be set later in s_vFillFragParameter()


    if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {//802.11g packet

        pvRrvTime = (PSRrvTime_gCTS) (pbyTxBufferAddr + wTxBufSize);
        pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS));
        pvRTS = NULL;
        pvCTS = (PSCTS) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR);
        pvTxDataHd = (PSTxDataHead_g) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR + sizeof(SCTS));
        cbHeaderSize = wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR + sizeof(SCTS) + sizeof(STxDataHead_g);

    }
    else {//802.11a/b packet

        pvRrvTime = (PSRrvTime_ab) (pbyTxBufferAddr + wTxBufSize);
        pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab));
        pvRTS = NULL;
        pvCTS = NULL;
        pvTxDataHd = (PSTxDataHead_ab) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR);
        cbHeaderSize = wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR + sizeof(STxDataHead_ab);
    }
    memset((void *)(pbyTxBufferAddr + wTxBufSize), 0,
	   (cbHeaderSize - wTxBufSize));
    memcpy(&(sEthHeader.abyDstAddr[0]),
	   &(p80211Header->sA3.abyAddr1[0]),
	   ETH_ALEN);
    memcpy(&(sEthHeader.abySrcAddr[0]),
	   &(p80211Header->sA3.abyAddr2[0]),
	   ETH_ALEN);
    //=========================
    //    No Fragmentation
    //=========================
    pTxBufHead->wFragCtl |= (WORD)FRAGCTL_NONFRAG;


    //Fill FIFO,RrvTime,RTS,and CTS
    s_vGenerateTxParameter(pDevice, byPktType, wCurrentRate, pbyTxBufferAddr, pvRrvTime, pvRTS, pvCTS,
                           cbFrameSize, bNeedACK, TYPE_TXDMA0, &sEthHeader);

    //Fill DataHead
    uDuration = s_uFillDataHead(pDevice, byPktType, wCurrentRate, pvTxDataHd, cbFrameSize, TYPE_TXDMA0, bNeedACK,
                                0, 0, 1, AUTO_FB_NONE);

    pMACHeader = (PS802_11Header) (pbyTxBufferAddr + cbHeaderSize);

    cbReqCount = cbHeaderSize + cbMacHdLen + uPadding + cbIVlen + (cbFrameBodySize + cbMIClen) + cbExtSuppRate;

    pbyMacHdr = (PBYTE)(pbyTxBufferAddr + cbHeaderSize);
    pbyPayloadHead = (PBYTE)(pbyMacHdr + cbMacHdLen + uPadding + cbIVlen);
    pbyIVHead = (PBYTE)(pbyMacHdr + cbMacHdLen + uPadding);

    // Copy the Packet into a tx Buffer
    memcpy(pbyMacHdr, skb->data, cbMacHdLen);

    // version set to 0, patch for hostapd deamon
    pMACHeader->wFrameCtl &= cpu_to_le16(0xfffc);
    memcpy(pbyPayloadHead, (skb->data + cbMacHdLen), cbFrameBodySize);

    // replace support rate, patch for hostapd daemon( only support 11M)
    if (WLAN_GET_FC_FSTYPE(p80211Header->sA4.wFrameCtl) == WLAN_FSTYPE_ASSOCRESP) {
        if (cbExtSuppRate != 0) {
            if (((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates)->len != 0)
                memcpy((pbyPayloadHead + cbFrameBodySize),
                        pMgmt->abyCurrSuppRates,
                        ((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates)->len + WLAN_IEHDR_LEN
                       );
             if (((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates)->len != 0)
                memcpy((pbyPayloadHead + cbFrameBodySize) + ((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates)->len + WLAN_IEHDR_LEN,
                        pMgmt->abyCurrExtSuppRates,
                        ((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates)->len + WLAN_IEHDR_LEN
                       );
         }
    }

    // Set wep
    if (WLAN_GET_FC_ISWEP(p80211Header->sA4.wFrameCtl) != 0) {

        if (pDevice->bEnableHostWEP) {
            pTransmitKey = &STempKey;
            pTransmitKey->byCipherSuite = pMgmt->sNodeDBTable[uNodeIndex].byCipherSuite;
            pTransmitKey->dwKeyIndex = pMgmt->sNodeDBTable[uNodeIndex].dwKeyIndex;
            pTransmitKey->uKeyLength = pMgmt->sNodeDBTable[uNodeIndex].uWepKeyLength;
            pTransmitKey->dwTSC47_16 = pMgmt->sNodeDBTable[uNodeIndex].dwTSC47_16;
            pTransmitKey->wTSC15_0 = pMgmt->sNodeDBTable[uNodeIndex].wTSC15_0;
            memcpy(pTransmitKey->abyKey,
                &pMgmt->sNodeDBTable[uNodeIndex].abyWepKey[0],
                pTransmitKey->uKeyLength
                );
        }

        if ((pTransmitKey != NULL) && (pTransmitKey->byCipherSuite == KEY_CTL_TKIP)) {

            dwMICKey0 = *(PDWORD)(&pTransmitKey->abyKey[16]);
            dwMICKey1 = *(PDWORD)(&pTransmitKey->abyKey[20]);

            // DO Software Michael
            MIC_vInit(dwMICKey0, dwMICKey1);
            MIC_vAppend((PBYTE)&(sEthHeader.abyDstAddr[0]), 12);
            dwMIC_Priority = 0;
            MIC_vAppend((PBYTE)&dwMIC_Priority, 4);
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"DMA0_tx_8021:MIC KEY: %lX, %lX\n", dwMICKey0, dwMICKey1);

            uLength = cbHeaderSize + cbMacHdLen + uPadding + cbIVlen;

            MIC_vAppend((pbyTxBufferAddr + uLength), cbFrameBodySize);

            pdwMIC_L = (PDWORD)(pbyTxBufferAddr + uLength + cbFrameBodySize);
            pdwMIC_R = (PDWORD)(pbyTxBufferAddr + uLength + cbFrameBodySize + 4);

            MIC_vGetMIC(pdwMIC_L, pdwMIC_R);
            MIC_vUnInit();

            if (pDevice->bTxMICFail == TRUE) {
                *pdwMIC_L = 0;
                *pdwMIC_R = 0;
                pDevice->bTxMICFail = FALSE;
            }

            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"uLength: %d, %d\n", uLength, cbFrameBodySize);
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"cbReqCount:%d, %d, %d, %d\n", cbReqCount, cbHeaderSize, uPadding, cbIVlen);
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"MIC:%lx, %lx\n", *pdwMIC_L, *pdwMIC_R);

        }

        s_vFillTxKey(pDevice, (PBYTE)(pTxBufHead->adwTxKey), pbyIVHead, pTransmitKey,
                     pbyMacHdr, (WORD)cbFrameBodySize, (PBYTE)pMICHDR);

        if (pDevice->bEnableHostWEP) {
            pMgmt->sNodeDBTable[uNodeIndex].dwTSC47_16 = pTransmitKey->dwTSC47_16;
            pMgmt->sNodeDBTable[uNodeIndex].wTSC15_0 = pTransmitKey->wTSC15_0;
        }

        if ((pDevice->byLocalID <= REV_ID_VT3253_A1)) {
            s_vSWencryption(pDevice, pTransmitKey, pbyPayloadHead, (WORD)(cbFrameBodySize + cbMIClen));
        }
    }

    pMACHeader->wSeqCtl = cpu_to_le16(pDevice->wSeqCounter << 4);
    pDevice->wSeqCounter++ ;
    if (pDevice->wSeqCounter > 0x0fff)
        pDevice->wSeqCounter = 0;


    if (bIsPSPOLL) {
        // The MAC will automatically replace the Duration-field of MAC header by Duration-field
        // of  FIFO control header.
        // This will cause AID-field of PS-POLL packet be incorrect (Because PS-POLL's AID field is
        // in the same place of other packet's Duration-field).
        // And it will cause Cisco-AP to issue Disassociation-packet
        if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
            ((PSTxDataHead_g)pvTxDataHd)->wDuration_a = cpu_to_le16(p80211Header->sA2.wDurationID);
            ((PSTxDataHead_g)pvTxDataHd)->wDuration_b = cpu_to_le16(p80211Header->sA2.wDurationID);
        } else {
            ((PSTxDataHead_ab)pvTxDataHd)->wDuration = cpu_to_le16(p80211Header->sA2.wDurationID);
        }
    }

    pTX_Buffer->wTxByteCount = cpu_to_le16((WORD)(cbReqCount));
    pTX_Buffer->byPKTNO = (BYTE) (((wCurrentRate<<4) &0x00F0) | ((pDevice->wSeqCounter - 1) & 0x000F));
    pTX_Buffer->byType = 0x00;

    pContext->pPacket = skb;
    pContext->Type = CONTEXT_MGMT_PACKET;
    pContext->uBufLen = (WORD)cbReqCount + 4;  //USB header

    if (WLAN_GET_FC_TODS(pMACHeader->wFrameCtl) == 0) {
        s_vSaveTxPktInfo(pDevice, (BYTE) (pTX_Buffer->byPKTNO & 0x0F), &(pMACHeader->abyAddr1[0]),(WORD)cbFrameSize,pTX_Buffer->wFIFOCtl);
    }
    else {
        s_vSaveTxPktInfo(pDevice, (BYTE) (pTX_Buffer->byPKTNO & 0x0F), &(pMACHeader->abyAddr3[0]),(WORD)cbFrameSize,pTX_Buffer->wFIFOCtl);
    }
    PIPEnsSendBulkOut(pDevice,pContext);
    return ;

}




//TYPE_AC0DMA data tx
/*
 * Description:
 *      Tx packet via AC0DMA(DMA1)
 *
 * Parameters:
 *  In:
 *      pDevice         - Pointer to the adapter
 *      skb             - Pointer to tx skb packet
 *  Out:
 *      void
 *
 * Return Value: NULL
 */

int nsDMA_tx_packet(PSDevice pDevice, unsigned int uDMAIdx, struct sk_buff *skb)
{
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    unsigned int BytesToWrite = 0, uHeaderLen = 0;
    unsigned int            uNodeIndex = 0;
    BYTE            byMask[8] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80};
    WORD            wAID;
    BYTE            byPktType;
    BOOL            bNeedEncryption = FALSE;
    PSKeyItem       pTransmitKey = NULL;
    SKeyItem        STempKey;
    unsigned int            ii;
    BOOL            bTKIP_UseGTK = FALSE;
    BOOL            bNeedDeAuth = FALSE;
    PBYTE           pbyBSSID;
    BOOL            bNodeExist = FALSE;
    PUSB_SEND_CONTEXT pContext;
    BOOL            fConvertedPacket;
    PTX_BUFFER      pTX_Buffer;
    unsigned int            status;
    WORD            wKeepRate = pDevice->wCurrentRate;
    struct net_device_stats* pStats = &pDevice->stats;
     BOOL            bTxeapol_key = FALSE;


    if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {

        if (pDevice->uAssocCount == 0) {
            dev_kfree_skb_irq(skb);
            return 0;
        }

	if (is_multicast_ether_addr((PBYTE)(skb->data))) {
            uNodeIndex = 0;
            bNodeExist = TRUE;
            if (pMgmt->sNodeDBTable[0].bPSEnable) {

                skb_queue_tail(&(pMgmt->sNodeDBTable[0].sTxPSQueue), skb);
                pMgmt->sNodeDBTable[0].wEnQueueCnt++;
                // set tx map
                pMgmt->abyPSTxMap[0] |= byMask[0];
                return 0;
            }
            // multicast/broadcast data rate

            if (pDevice->byBBType != BB_TYPE_11A)
                pDevice->wCurrentRate = RATE_2M;
            else
                pDevice->wCurrentRate = RATE_24M;
            // long preamble type
            pDevice->byPreambleType = PREAMBLE_SHORT;

        }else {

            if (BSSbIsSTAInNodeDB(pDevice, (PBYTE)(skb->data), &uNodeIndex)) {

                if (pMgmt->sNodeDBTable[uNodeIndex].bPSEnable) {

                    skb_queue_tail(&pMgmt->sNodeDBTable[uNodeIndex].sTxPSQueue, skb);

                    pMgmt->sNodeDBTable[uNodeIndex].wEnQueueCnt++;
                    // set tx map
                    wAID = pMgmt->sNodeDBTable[uNodeIndex].wAID;
                    pMgmt->abyPSTxMap[wAID >> 3] |=  byMask[wAID & 7];
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Set:pMgmt->abyPSTxMap[%d]= %d\n",
                             (wAID >> 3), pMgmt->abyPSTxMap[wAID >> 3]);

                    return 0;
                }
                // AP rate decided from node
                pDevice->wCurrentRate = pMgmt->sNodeDBTable[uNodeIndex].wTxDataRate;
                // tx preamble decided from node

                if (pMgmt->sNodeDBTable[uNodeIndex].bShortPreamble) {
                    pDevice->byPreambleType = pDevice->byShortPreamble;

                }else {
                    pDevice->byPreambleType = PREAMBLE_LONG;
                }
                bNodeExist = TRUE;
            }
        }

        if (bNodeExist == FALSE) {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG"Unknown STA not found in node DB \n");
            dev_kfree_skb_irq(skb);
            return 0;
        }
    }

    pContext = (PUSB_SEND_CONTEXT)s_vGetFreeContext(pDevice);

    if (pContext == NULL) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG" pContext == NULL\n");
        dev_kfree_skb_irq(skb);
        return STATUS_RESOURCES;
    }

    memcpy(pDevice->sTxEthHeader.abyDstAddr, (PBYTE)(skb->data), ETH_HLEN);

//mike add:station mode check eapol-key challenge--->
{
    BYTE  Protocol_Version;    //802.1x Authentication
    BYTE  Packet_Type;           //802.1x Authentication
    BYTE  Descriptor_type;
    WORD Key_info;

    Protocol_Version = skb->data[ETH_HLEN];
    Packet_Type = skb->data[ETH_HLEN+1];
    Descriptor_type = skb->data[ETH_HLEN+1+1+2];
    Key_info = (skb->data[ETH_HLEN+1+1+2+1] << 8)|(skb->data[ETH_HLEN+1+1+2+2]);
	if (pDevice->sTxEthHeader.wType == cpu_to_be16(ETH_P_PAE)) {
		/* 802.1x OR eapol-key challenge frame transfer */
		if (((Protocol_Version == 1) || (Protocol_Version == 2)) &&
			(Packet_Type == 3)) {
                        bTxeapol_key = TRUE;
                       if(!(Key_info & BIT3) &&  //WPA or RSN group-key challenge
			   (Key_info & BIT8) && (Key_info & BIT9)) {    //send 2/2 key
			  if(Descriptor_type==254) {
                               pDevice->fWPA_Authened = TRUE;
			     PRINT_K("WPA ");
			  }
			  else {
                               pDevice->fWPA_Authened = TRUE;
			     PRINT_K("WPA2(re-keying) ");
			  }
			  PRINT_K("Authentication completed!!\n");
                        }
		    else if((Key_info & BIT3) && (Descriptor_type==2) &&  //RSN pairwise-key challenge
			       (Key_info & BIT8) && (Key_info & BIT9)) {
			  pDevice->fWPA_Authened = TRUE;
                            PRINT_K("WPA2 Authentication completed!!\n");
		     }
             }
   }
}
//mike add:station mode check eapol-key challenge<---

    if (pDevice->bEncryptionEnable == TRUE) {
        bNeedEncryption = TRUE;
        // get Transmit key
        do {
            if ((pMgmt->eCurrMode == WMAC_MODE_ESS_STA) &&
                (pMgmt->eCurrState == WMAC_STATE_ASSOC)) {
                pbyBSSID = pDevice->abyBSSID;
                // get pairwise key
                if (KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, PAIRWISE_KEY, &pTransmitKey) == FALSE) {
                    // get group key
                    if(KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, GROUP_KEY, &pTransmitKey) == TRUE) {
                        bTKIP_UseGTK = TRUE;
                        DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG"Get GTK.\n");
                        break;
                    }
                } else {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG"Get PTK.\n");
                    break;
                }
            }else if (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) {

                pbyBSSID = pDevice->sTxEthHeader.abyDstAddr;  //TO_DS = 0 and FROM_DS = 0 --> 802.11 MAC Address1
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG"IBSS Serach Key: \n");
                for (ii = 0; ii< 6; ii++)
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG"%x \n", *(pbyBSSID+ii));
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG"\n");

                // get pairwise key
                if(KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, PAIRWISE_KEY, &pTransmitKey) == TRUE)
                    break;
            }
            // get group key
            pbyBSSID = pDevice->abyBroadcastAddr;
            if(KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, GROUP_KEY, &pTransmitKey) == FALSE) {
                pTransmitKey = NULL;
                if (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG"IBSS and KEY is NULL. [%d]\n", pMgmt->eCurrMode);
                }
                else
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG"NOT IBSS and KEY is NULL. [%d]\n", pMgmt->eCurrMode);
            } else {
                bTKIP_UseGTK = TRUE;
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG"Get GTK.\n");
            }
        } while(FALSE);
    }

    if (pDevice->bEnableHostWEP) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG"acdma0: STA index %d\n", uNodeIndex);
        if (pDevice->bEncryptionEnable == TRUE) {
            pTransmitKey = &STempKey;
            pTransmitKey->byCipherSuite = pMgmt->sNodeDBTable[uNodeIndex].byCipherSuite;
            pTransmitKey->dwKeyIndex = pMgmt->sNodeDBTable[uNodeIndex].dwKeyIndex;
            pTransmitKey->uKeyLength = pMgmt->sNodeDBTable[uNodeIndex].uWepKeyLength;
            pTransmitKey->dwTSC47_16 = pMgmt->sNodeDBTable[uNodeIndex].dwTSC47_16;
            pTransmitKey->wTSC15_0 = pMgmt->sNodeDBTable[uNodeIndex].wTSC15_0;
            memcpy(pTransmitKey->abyKey,
                &pMgmt->sNodeDBTable[uNodeIndex].abyWepKey[0],
                pTransmitKey->uKeyLength
                );
         }
    }

    byPktType = (BYTE)pDevice->byPacketType;

    if (pDevice->bFixRate) {
        if (pDevice->byBBType == BB_TYPE_11B) {
            if (pDevice->uConnectionRate >= RATE_11M) {
                pDevice->wCurrentRate = RATE_11M;
            } else {
                pDevice->wCurrentRate = (WORD)pDevice->uConnectionRate;
            }
        } else {
            if ((pDevice->byBBType == BB_TYPE_11A) &&
                (pDevice->uConnectionRate <= RATE_6M)) {
                pDevice->wCurrentRate = RATE_6M;
            } else {
                if (pDevice->uConnectionRate >= RATE_54M)
                    pDevice->wCurrentRate = RATE_54M;
                else
                    pDevice->wCurrentRate = (WORD)pDevice->uConnectionRate;
            }
        }
    }
    else {
        if (pDevice->eOPMode == OP_MODE_ADHOC) {
            // Adhoc Tx rate decided from node DB
	    if (is_multicast_ether_addr(pDevice->sTxEthHeader.abyDstAddr)) {
                // Multicast use highest data rate
                pDevice->wCurrentRate = pMgmt->sNodeDBTable[0].wTxDataRate;
                // preamble type
                pDevice->byPreambleType = pDevice->byShortPreamble;
            }
            else {
                if(BSSbIsSTAInNodeDB(pDevice, &(pDevice->sTxEthHeader.abyDstAddr[0]), &uNodeIndex)) {
                    pDevice->wCurrentRate = pMgmt->sNodeDBTable[uNodeIndex].wTxDataRate;
                    if (pMgmt->sNodeDBTable[uNodeIndex].bShortPreamble) {
                        pDevice->byPreambleType = pDevice->byShortPreamble;

                    }
                    else {
                        pDevice->byPreambleType = PREAMBLE_LONG;
                    }
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Found Node Index is [%d]  Tx Data Rate:[%d]\n",uNodeIndex, pDevice->wCurrentRate);
                }
                else {
                    if (pDevice->byBBType != BB_TYPE_11A)
                       pDevice->wCurrentRate = RATE_2M;
                    else
                       pDevice->wCurrentRate = RATE_24M; // refer to vMgrCreateOwnIBSS()'s
                                                         // abyCurrExtSuppRates[]
                    pDevice->byPreambleType = PREAMBLE_SHORT;
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Not Found Node use highest basic Rate.....\n");
                }
            }
        }
        if (pDevice->eOPMode == OP_MODE_INFRASTRUCTURE) {
            // Infra STA rate decided from AP Node, index = 0
            pDevice->wCurrentRate = pMgmt->sNodeDBTable[0].wTxDataRate;
        }
    }

	if (pDevice->sTxEthHeader.wType == cpu_to_be16(ETH_P_PAE)) {
		if (pDevice->byBBType != BB_TYPE_11A) {
			pDevice->wCurrentRate = RATE_1M;
			pDevice->byACKRate = RATE_1M;
			pDevice->byTopCCKBasicRate = RATE_1M;
			pDevice->byTopOFDMBasicRate = RATE_6M;
		} else {
			pDevice->wCurrentRate = RATE_6M;
			pDevice->byACKRate = RATE_6M;
			pDevice->byTopCCKBasicRate = RATE_1M;
			pDevice->byTopOFDMBasicRate = RATE_6M;
		}
	}

    DBG_PRT(MSG_LEVEL_DEBUG,
	    KERN_INFO "dma_tx: pDevice->wCurrentRate = %d\n",
	    pDevice->wCurrentRate);

    if (wKeepRate != pDevice->wCurrentRate) {
	bScheduleCommand((void *) pDevice, WLAN_CMD_SETPOWER, NULL);
    }

    if (pDevice->wCurrentRate <= RATE_11M) {
        byPktType = PK_TYPE_11B;
    }

    if (bNeedEncryption == TRUE) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"ntohs Pkt Type=%04x\n", ntohs(pDevice->sTxEthHeader.wType));
	if ((pDevice->sTxEthHeader.wType) == cpu_to_be16(ETH_P_PAE)) {
		bNeedEncryption = FALSE;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Pkt Type=%04x\n", (pDevice->sTxEthHeader.wType));
            if ((pMgmt->eCurrMode == WMAC_MODE_ESS_STA) && (pMgmt->eCurrState == WMAC_STATE_ASSOC)) {
                if (pTransmitKey == NULL) {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Don't Find TX KEY\n");
                }
                else {
                    if (bTKIP_UseGTK == TRUE) {
                        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"error: KEY is GTK!!~~\n");
                    }
                    else {
                        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Find PTK [%lX]\n", pTransmitKey->dwKeyIndex);
                        bNeedEncryption = TRUE;
                    }
                }
            }

            if (pDevice->bEnableHostWEP) {
                if ((uNodeIndex != 0) &&
                    (pMgmt->sNodeDBTable[uNodeIndex].dwKeyIndex & PAIRWISE_KEY)) {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Find PTK [%lX]\n", pTransmitKey->dwKeyIndex);
                    bNeedEncryption = TRUE;
                 }
             }
        }
        else {

            if (pTransmitKey == NULL) {
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"return no tx key\n");
		pContext->bBoolInUse = FALSE;
                dev_kfree_skb_irq(skb);
                pStats->tx_dropped++;
                return STATUS_FAILURE;
            }
        }
    }

    fConvertedPacket = s_bPacketToWirelessUsb(pDevice, byPktType,
                        (PBYTE)(&pContext->Data[0]), bNeedEncryption,
                        skb->len, uDMAIdx, &pDevice->sTxEthHeader,
                        (PBYTE)skb->data, pTransmitKey, uNodeIndex,
                        pDevice->wCurrentRate,
                        &uHeaderLen, &BytesToWrite
                       );

    if (fConvertedPacket == FALSE) {
        pContext->bBoolInUse = FALSE;
        dev_kfree_skb_irq(skb);
        return STATUS_FAILURE;
    }

    if ( pDevice->bEnablePSMode == TRUE ) {
        if ( !pDevice->bPSModeTxBurst ) {
		bScheduleCommand((void *) pDevice,
				 WLAN_CMD_MAC_DISPOWERSAVING,
				 NULL);
            pDevice->bPSModeTxBurst = TRUE;
        }
    }

    pTX_Buffer = (PTX_BUFFER)&(pContext->Data[0]);
    pTX_Buffer->byPKTNO = (BYTE) (((pDevice->wCurrentRate<<4) &0x00F0) | ((pDevice->wSeqCounter - 1) & 0x000F));
    pTX_Buffer->wTxByteCount = (WORD)BytesToWrite;

    pContext->pPacket = skb;
    pContext->Type = CONTEXT_DATA_PACKET;
    pContext->uBufLen = (WORD)BytesToWrite + 4 ; //USB header

    s_vSaveTxPktInfo(pDevice, (BYTE) (pTX_Buffer->byPKTNO & 0x0F), &(pContext->sEthHeader.abyDstAddr[0]),(WORD) (BytesToWrite-uHeaderLen),pTX_Buffer->wFIFOCtl);

    status = PIPEnsSendBulkOut(pDevice,pContext);

    if (bNeedDeAuth == TRUE) {
        WORD wReason = WLAN_MGMT_REASON_MIC_FAILURE;

	bScheduleCommand((void *) pDevice, WLAN_CMD_DEAUTH, (PBYTE) &wReason);
    }

  if(status!=STATUS_PENDING) {
     pContext->bBoolInUse = FALSE;
    dev_kfree_skb_irq(skb);
    return STATUS_FAILURE;
  }
  else
    return 0;

}



/*
 * Description:
 *      Relay packet send (AC1DMA) from rx dpc.
 *
 * Parameters:
 *  In:
 *      pDevice         - Pointer to the adapter
 *      pPacket         - Pointer to rx packet
 *      cbPacketSize    - rx ethernet frame size
 *  Out:
 *      TURE, FALSE
 *
 * Return Value: Return TRUE if packet is copy to dma1; otherwise FALSE
 */


BOOL
bRelayPacketSend (
      PSDevice pDevice,
      PBYTE    pbySkbData,
      unsigned int     uDataLen,
      unsigned int     uNodeIndex
    )
{
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    unsigned int BytesToWrite = 0, uHeaderLen = 0;
    BYTE            byPktType = PK_TYPE_11B;
    BOOL            bNeedEncryption = FALSE;
    SKeyItem        STempKey;
    PSKeyItem       pTransmitKey = NULL;
    PBYTE           pbyBSSID;
    PUSB_SEND_CONTEXT   pContext;
    BYTE            byPktTyp;
    BOOL            fConvertedPacket;
    PTX_BUFFER      pTX_Buffer;
    unsigned int            status;
    WORD            wKeepRate = pDevice->wCurrentRate;



    pContext = (PUSB_SEND_CONTEXT)s_vGetFreeContext(pDevice);

    if (NULL == pContext) {
        return FALSE;
    }

    memcpy(pDevice->sTxEthHeader.abyDstAddr, (PBYTE)pbySkbData, ETH_HLEN);

    if (pDevice->bEncryptionEnable == TRUE) {
        bNeedEncryption = TRUE;
        // get group key
        pbyBSSID = pDevice->abyBroadcastAddr;
        if(KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, GROUP_KEY, &pTransmitKey) == FALSE) {
            pTransmitKey = NULL;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG"KEY is NULL. [%d]\n", pMgmt->eCurrMode);
        } else {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_DEBUG"Get GTK.\n");
        }
    }

    if (pDevice->bEnableHostWEP) {
        if (uNodeIndex < MAX_NODE_NUM + 1) {
            pTransmitKey = &STempKey;
            pTransmitKey->byCipherSuite = pMgmt->sNodeDBTable[uNodeIndex].byCipherSuite;
            pTransmitKey->dwKeyIndex = pMgmt->sNodeDBTable[uNodeIndex].dwKeyIndex;
            pTransmitKey->uKeyLength = pMgmt->sNodeDBTable[uNodeIndex].uWepKeyLength;
            pTransmitKey->dwTSC47_16 = pMgmt->sNodeDBTable[uNodeIndex].dwTSC47_16;
            pTransmitKey->wTSC15_0 = pMgmt->sNodeDBTable[uNodeIndex].wTSC15_0;
            memcpy(pTransmitKey->abyKey,
                    &pMgmt->sNodeDBTable[uNodeIndex].abyWepKey[0],
                    pTransmitKey->uKeyLength
                  );
        }
    }

    if ( bNeedEncryption && (pTransmitKey == NULL) ) {
        pContext->bBoolInUse = FALSE;
        return FALSE;
    }

    byPktTyp = (BYTE)pDevice->byPacketType;

    if (pDevice->bFixRate) {
        if (pDevice->byBBType == BB_TYPE_11B) {
            if (pDevice->uConnectionRate >= RATE_11M) {
                pDevice->wCurrentRate = RATE_11M;
            } else {
                pDevice->wCurrentRate = (WORD)pDevice->uConnectionRate;
            }
        } else {
            if ((pDevice->byBBType == BB_TYPE_11A) &&
                (pDevice->uConnectionRate <= RATE_6M)) {
                pDevice->wCurrentRate = RATE_6M;
            } else {
                if (pDevice->uConnectionRate >= RATE_54M)
                    pDevice->wCurrentRate = RATE_54M;
                else
                    pDevice->wCurrentRate = (WORD)pDevice->uConnectionRate;
            }
        }
    }
    else {
        pDevice->wCurrentRate = pMgmt->sNodeDBTable[uNodeIndex].wTxDataRate;
    }

    if (wKeepRate != pDevice->wCurrentRate) {
	bScheduleCommand((void *) pDevice, WLAN_CMD_SETPOWER, NULL);
    }

    if (pDevice->wCurrentRate <= RATE_11M)
        byPktType = PK_TYPE_11B;

    BytesToWrite = uDataLen + ETH_FCS_LEN;

    // Convert the packet to an usb frame and copy into our buffer
    // and send the irp.

    fConvertedPacket = s_bPacketToWirelessUsb(pDevice, byPktType,
                         (PBYTE)(&pContext->Data[0]), bNeedEncryption,
                         uDataLen, TYPE_AC0DMA, &pDevice->sTxEthHeader,
                         pbySkbData, pTransmitKey, uNodeIndex,
                         pDevice->wCurrentRate,
                         &uHeaderLen, &BytesToWrite
                        );

    if (fConvertedPacket == FALSE) {
        pContext->bBoolInUse = FALSE;
        return FALSE;
    }

    pTX_Buffer = (PTX_BUFFER)&(pContext->Data[0]);
    pTX_Buffer->byPKTNO = (BYTE) (((pDevice->wCurrentRate<<4) &0x00F0) | ((pDevice->wSeqCounter - 1) & 0x000F));
    pTX_Buffer->wTxByteCount = (WORD)BytesToWrite;

    pContext->pPacket = NULL;
    pContext->Type = CONTEXT_DATA_PACKET;
    pContext->uBufLen = (WORD)BytesToWrite + 4 ; //USB header

    s_vSaveTxPktInfo(pDevice, (BYTE) (pTX_Buffer->byPKTNO & 0x0F), &(pContext->sEthHeader.abyDstAddr[0]),(WORD) (BytesToWrite-uHeaderLen),pTX_Buffer->wFIFOCtl);

    status = PIPEnsSendBulkOut(pDevice,pContext);

    return TRUE;
}

