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
 *      vGenerateMACHeader - Translate 802.3 to 802.11 header
 *      cbGetFragCount - Calculate fragment number count
 *      csBeacon_xmit - beacon tx function
 *      csMgmt_xmit - management tx function
 *      s_cbFillTxBufHead - fulfill tx dma buffer header
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
#include "wroute.h"
#include "hostap.h"
#include "rf.h"

/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/
//static int          msglevel                =MSG_LEVEL_DEBUG;
static int msglevel = MSG_LEVEL_INFO;

#define	PLICE_DEBUG

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Static Definitions -------------------------*/
#define CRITICAL_PACKET_LEN      256    // if packet size < 256 -> in-direct send
                                        //    packet size >= 256 -> direct send

const unsigned short wTimeStampOff[2][MAX_RATE] = {
	{384, 288, 226, 209, 54, 43, 37, 31, 28, 25, 24, 23}, // Long Preamble
	{384, 192, 130, 113, 54, 43, 37, 31, 28, 25, 24, 23}, // Short Preamble
};

const unsigned short wFB_Opt0[2][5] = {
	{RATE_12M, RATE_18M, RATE_24M, RATE_36M, RATE_48M}, // fallback_rate0
	{RATE_12M, RATE_12M, RATE_18M, RATE_24M, RATE_36M}, // fallback_rate1
};
const unsigned short wFB_Opt1[2][5] = {
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
s_vFillTxKey(
	PSDevice   pDevice,
	unsigned char *pbyBuf,
	unsigned char *pbyIVHead,
	PSKeyItem  pTransmitKey,
	unsigned char *pbyHdrBuf,
	unsigned short wPayloadLen,
	unsigned char *pMICHDR
);

static
void
s_vFillRTSHead(
	PSDevice         pDevice,
	unsigned char byPktType,
	void *pvRTS,
	unsigned int	cbFrameLength,
	bool bNeedAck,
	bool bDisCRC,
	PSEthernetHeader psEthHeader,
	unsigned short wCurrentRate,
	unsigned char byFBOption
);

static
void
s_vGenerateTxParameter(
	PSDevice         pDevice,
	unsigned char byPktType,
	void *pTxBufHead,
	void *pvRrvTime,
	void *pvRTS,
	void *pvCTS,
	unsigned int	cbFrameSize,
	bool bNeedACK,
	unsigned int	uDMAIdx,
	PSEthernetHeader psEthHeader,
	unsigned short wCurrentRate
);

static void s_vFillFragParameter(
	PSDevice pDevice,
	unsigned char *pbyBuffer,
	unsigned int	uTxType,
	void *pvtdCurr,
	unsigned short wFragType,
	unsigned int	cbReqCount
);

static unsigned int
s_cbFillTxBufHead(PSDevice pDevice, unsigned char byPktType, unsigned char *pbyTxBufferAddr,
		  unsigned int cbFrameBodySize, unsigned int uDMAIdx, PSTxDesc pHeadTD,
		  PSEthernetHeader psEthHeader, unsigned char *pPacket, bool bNeedEncrypt,
		  PSKeyItem pTransmitKey, unsigned int uNodeIndex, unsigned int *puMACfragNum);

static
unsigned int
s_uFillDataHead(
	PSDevice pDevice,
	unsigned char byPktType,
	void *pTxDataHead,
	unsigned int cbFrameLength,
	unsigned int uDMAIdx,
	bool bNeedAck,
	unsigned int uFragIdx,
	unsigned int cbLastFragmentSize,
	unsigned int uMACfragNum,
	unsigned char byFBOption,
	unsigned short wCurrentRate
);

/*---------------------  Export Variables  --------------------------*/

static
void
s_vFillTxKey(
	PSDevice   pDevice,
	unsigned char *pbyBuf,
	unsigned char *pbyIVHead,
	PSKeyItem  pTransmitKey,
	unsigned char *pbyHdrBuf,
	unsigned short wPayloadLen,
	unsigned char *pMICHDR
)
{
	unsigned long *pdwIV = (unsigned long *)pbyIVHead;
	unsigned long *pdwExtIV = (unsigned long *)((unsigned char *)pbyIVHead+4);
	unsigned short wValue;
	PS802_11Header  pMACHeader = (PS802_11Header)pbyHdrBuf;
	unsigned long dwRevIVCounter;
	unsigned char byKeyIndex = 0;

	//Fill TXKEY
	if (pTransmitKey == NULL)
		return;

	dwRevIVCounter = cpu_to_le32(pDevice->dwIVCounter);
	*pdwIV = pDevice->dwIVCounter;
	byKeyIndex = pTransmitKey->dwKeyIndex & 0xf;

	if (pTransmitKey->byCipherSuite == KEY_CTL_WEP) {
		if (pTransmitKey->uKeyLength == WLAN_WEP232_KEYLEN) {
			memcpy(pDevice->abyPRNG, (unsigned char *)&(dwRevIVCounter), 3);
			memcpy(pDevice->abyPRNG+3, pTransmitKey->abyKey, pTransmitKey->uKeyLength);
		} else {
			memcpy(pbyBuf, (unsigned char *)&(dwRevIVCounter), 3);
			memcpy(pbyBuf+3, pTransmitKey->abyKey, pTransmitKey->uKeyLength);
			if (pTransmitKey->uKeyLength == WLAN_WEP40_KEYLEN) {
				memcpy(pbyBuf+8, (unsigned char *)&(dwRevIVCounter), 3);
				memcpy(pbyBuf+11, pTransmitKey->abyKey, pTransmitKey->uKeyLength);
			}
			memcpy(pDevice->abyPRNG, pbyBuf, 16);
		}
		// Append IV after Mac Header
		*pdwIV &= WEP_IV_MASK;//00000000 11111111 11111111 11111111
		*pdwIV |= (unsigned long)byKeyIndex << 30;
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

		*(pbyIVHead+3) = (unsigned char)(((byKeyIndex << 6) & 0xc0) | 0x20); // 0x20 is ExtIV
		// Append IV&ExtIV after Mac Header
		*pdwExtIV = cpu_to_le32(pTransmitKey->dwTSC47_16);
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "vFillTxKey()---- pdwExtIV: %lx\n", *pdwExtIV);

	} else if (pTransmitKey->byCipherSuite == KEY_CTL_CCMP) {
		pTransmitKey->wTSC15_0++;
		if (pTransmitKey->wTSC15_0 == 0) {
			pTransmitKey->dwTSC47_16++;
		}
		memcpy(pbyBuf, pTransmitKey->abyKey, 16);

		// Make IV
		*pdwIV = 0;
		*(pbyIVHead+3) = (unsigned char)(((byKeyIndex << 6) & 0xc0) | 0x20); // 0x20 is ExtIV
		*pdwIV |= cpu_to_le16((unsigned short)(pTransmitKey->wTSC15_0));
		//Append IV&ExtIV after Mac Header
		*pdwExtIV = cpu_to_le32(pTransmitKey->dwTSC47_16);

		//Fill MICHDR0
		*pMICHDR = 0x59;
		*((unsigned char *)(pMICHDR+1)) = 0; // TxPriority
		memcpy(pMICHDR+2, &(pMACHeader->abyAddr2[0]), 6);
		*((unsigned char *)(pMICHDR+8)) = HIBYTE(HIWORD(pTransmitKey->dwTSC47_16));
		*((unsigned char *)(pMICHDR+9)) = LOBYTE(HIWORD(pTransmitKey->dwTSC47_16));
		*((unsigned char *)(pMICHDR+10)) = HIBYTE(LOWORD(pTransmitKey->dwTSC47_16));
		*((unsigned char *)(pMICHDR+11)) = LOBYTE(LOWORD(pTransmitKey->dwTSC47_16));
		*((unsigned char *)(pMICHDR+12)) = HIBYTE(pTransmitKey->wTSC15_0);
		*((unsigned char *)(pMICHDR+13)) = LOBYTE(pTransmitKey->wTSC15_0);
		*((unsigned char *)(pMICHDR+14)) = HIBYTE(wPayloadLen);
		*((unsigned char *)(pMICHDR+15)) = LOBYTE(wPayloadLen);

		//Fill MICHDR1
		*((unsigned char *)(pMICHDR+16)) = 0; // HLEN[15:8]
		if (pDevice->bLongHeader) {
			*((unsigned char *)(pMICHDR+17)) = 28; // HLEN[7:0]
		} else {
			*((unsigned char *)(pMICHDR+17)) = 22; // HLEN[7:0]
		}
		wValue = cpu_to_le16(pMACHeader->wFrameCtl & 0xC78F);
		memcpy(pMICHDR+18, (unsigned char *)&wValue, 2); // MSKFRACTL
		memcpy(pMICHDR+20, &(pMACHeader->abyAddr1[0]), 6);
		memcpy(pMICHDR+26, &(pMACHeader->abyAddr2[0]), 6);

		//Fill MICHDR2
		memcpy(pMICHDR+32, &(pMACHeader->abyAddr3[0]), 6);
		wValue = pMACHeader->wSeqCtl;
		wValue &= 0x000F;
		wValue = cpu_to_le16(wValue);
		memcpy(pMICHDR+38, (unsigned char *)&wValue, 2); // MSKSEQCTL
		if (pDevice->bLongHeader) {
			memcpy(pMICHDR+40, &(pMACHeader->abyAddr4[0]), 6);
		}
	}
}

static
void
s_vSWencryption(
	PSDevice            pDevice,
	PSKeyItem           pTransmitKey,
	unsigned char *pbyPayloadHead,
	unsigned short wPayloadSize
)
{
	unsigned int cbICVlen = 4;
	unsigned long dwICV = 0xFFFFFFFFL;
	unsigned long *pdwICV;

	if (pTransmitKey == NULL)
		return;

	if (pTransmitKey->byCipherSuite == KEY_CTL_WEP) {
		//=======================================================================
		// Append ICV after payload
		dwICV = CRCdwGetCrc32Ex(pbyPayloadHead, wPayloadSize, dwICV);//ICV(Payload)
		pdwICV = (unsigned long *)(pbyPayloadHead + wPayloadSize);
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
		pdwICV = (unsigned long *)(pbyPayloadHead + wPayloadSize);
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
s_uGetTxRsvTime(
	PSDevice pDevice,
	unsigned char byPktType,
	unsigned int cbFrameLength,
	unsigned short wRate,
	bool bNeedAck
)
{
	unsigned int uDataTime, uAckTime;

	uDataTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, cbFrameLength, wRate);
	if (byPktType == PK_TYPE_11B) {//llb,CCK mode
		uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, (unsigned short)pDevice->byTopCCKBasicRate);
	} else {//11g 2.4G OFDM mode & 11a 5G OFDM mode
		uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, (unsigned short)pDevice->byTopOFDMBasicRate);
	}

	if (bNeedAck) {
		return uDataTime + pDevice->uSIFS + uAckTime;
	} else {
		return uDataTime;
	}
}

//byFreqType: 0=>5GHZ 1=>2.4GHZ
static
unsigned int
s_uGetRTSCTSRsvTime(
	PSDevice pDevice,
	unsigned char byRTSRsvType,
	unsigned char byPktType,
	unsigned int cbFrameLength,
	unsigned short wCurrentRate
)
{
	unsigned int uRrvTime  , uRTSTime, uCTSTime, uAckTime, uDataTime;

	uRrvTime = uRTSTime = uCTSTime = uAckTime = uDataTime = 0;

	uDataTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, cbFrameLength, wCurrentRate);
	if (byRTSRsvType == 0) { //RTSTxRrvTime_bb
		uRTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 20, pDevice->byTopCCKBasicRate);
		uCTSTime = uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
	} else if (byRTSRsvType == 1) { //RTSTxRrvTime_ba, only in 2.4GHZ
		uRTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 20, pDevice->byTopCCKBasicRate);
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
		uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
	} else if (byRTSRsvType == 2) { //RTSTxRrvTime_aa
		uRTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 20, pDevice->byTopOFDMBasicRate);
		uCTSTime = uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
	} else if (byRTSRsvType == 3) { //CTSTxRrvTime_ba, only in 2.4GHZ
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
s_uGetDataDuration(
	PSDevice pDevice,
	unsigned char byDurType,
	unsigned int cbFrameLength,
	unsigned char byPktType,
	unsigned short wRate,
	bool bNeedAck,
	unsigned int uFragIdx,
	unsigned int cbLastFragmentSize,
	unsigned int uMACfragNum,
	unsigned char byFBOption
)
{
	bool bLastFrag = 0;
	unsigned int uAckTime = 0, uNextPktTime = 0;

	if (uFragIdx == (uMACfragNum-1)) {
		bLastFrag = 1;
	}

	switch (byDurType) {
	case DATADUR_B:    //DATADUR_B
		if (((uMACfragNum == 1)) || (bLastFrag == 1)) {//Non Frag or Last Frag
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
				return pDevice->uSIFS + uAckTime;
			} else {
				return 0;
			}
		} else {//First Frag or Mid Frag
			if (uFragIdx == (uMACfragNum-2)) {
				uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wRate, bNeedAck);
			} else {
				uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);
			}
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
				return pDevice->uSIFS + uAckTime + uNextPktTime;
			} else {
				return pDevice->uSIFS + uNextPktTime;
			}
		}
		break;

	case DATADUR_A:    //DATADUR_A
		if (((uMACfragNum == 1)) || (bLastFrag == 1)) {//Non Frag or Last Frag
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
				return pDevice->uSIFS + uAckTime;
			} else {
				return 0;
			}
		} else {//First Frag or Mid Frag
			if (uFragIdx == (uMACfragNum-2)) {
				uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wRate, bNeedAck);
			} else {
				uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);
			}
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
				return pDevice->uSIFS + uAckTime + uNextPktTime;
			} else {
				return pDevice->uSIFS + uNextPktTime;
			}
		}
		break;

	case DATADUR_A_F0:    //DATADUR_A_F0
		if (((uMACfragNum == 1)) || (bLastFrag == 1)) {//Non Frag or Last Frag
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
				return pDevice->uSIFS + uAckTime;
			} else {
				return 0;
			}
		} else { //First Frag or Mid Frag
			if (byFBOption == AUTO_FB_0) {
				if (wRate < RATE_18M)
					wRate = RATE_18M;
				else if (wRate > RATE_54M)
					wRate = RATE_54M;

				if (uFragIdx == (uMACfragNum-2)) {
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
				} else {
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
				}
			} else { // (byFBOption == AUTO_FB_1)
				if (wRate < RATE_18M)
					wRate = RATE_18M;
				else if (wRate > RATE_54M)
					wRate = RATE_54M;

				if (uFragIdx == (uMACfragNum-2)) {
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);
				} else {
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);
				}
			}

			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
				return pDevice->uSIFS + uAckTime + uNextPktTime;
			} else {
				return pDevice->uSIFS + uNextPktTime;
			}
		}
		break;

	case DATADUR_A_F1:    //DATADUR_A_F1
		if (((uMACfragNum == 1)) || (bLastFrag == 1)) {//Non Frag or Last Frag
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
				return pDevice->uSIFS + uAckTime;
			} else {
				return 0;
			}
		} else { //First Frag or Mid Frag
			if (byFBOption == AUTO_FB_0) {
				if (wRate < RATE_18M)
					wRate = RATE_18M;
				else if (wRate > RATE_54M)
					wRate = RATE_54M;

				if (uFragIdx == (uMACfragNum-2)) {
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
				} else {
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
				}

			} else { // (byFBOption == AUTO_FB_1)
				if (wRate < RATE_18M)
					wRate = RATE_18M;
				else if (wRate > RATE_54M)
					wRate = RATE_54M;

				if (uFragIdx == (uMACfragNum-2)) {
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);
				} else {
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);
				}
			}
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
				return pDevice->uSIFS + uAckTime + uNextPktTime;
			} else {
				return pDevice->uSIFS + uNextPktTime;
			}
		}
		break;

	default:
		break;
	}

	ASSERT(false);
	return 0;
}

//byFreqType: 0=>5GHZ 1=>2.4GHZ
static
unsigned int
s_uGetRTSCTSDuration(
	PSDevice pDevice,
	unsigned char byDurType,
	unsigned int cbFrameLength,
	unsigned char byPktType,
	unsigned short wRate,
	bool bNeedAck,
	unsigned char byFBOption
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
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M)) {
			uDurTime = uCTSTime + 2 * pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
		} else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M)) {
			uDurTime = uCTSTime + 2 * pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);
		}
		break;

	case RTSDUR_AA_F0: //RTSDuration_aa_f0
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M)) {
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
		} else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M)) {
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);
		}
		break;

	case RTSDUR_BA_F1: //RTSDuration_ba_f1
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M)) {
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
		} else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M)) {
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);
		}
		break;

	case RTSDUR_AA_F1: //RTSDuration_aa_f1
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M)) {
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
		} else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M)) {
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);
		}
		break;

	case CTSDUR_BA_F0: //CTSDuration_ba_f0
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M)) {
			uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
		} else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M)) {
			uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);
		}
		break;

	case CTSDUR_BA_F1: //CTSDuration_ba_f1
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M)) {
			uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
		} else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M)) {
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
s_uFillDataHead(
	PSDevice pDevice,
	unsigned char byPktType,
	void *pTxDataHead,
	unsigned int cbFrameLength,
	unsigned int uDMAIdx,
	bool bNeedAck,
	unsigned int uFragIdx,
	unsigned int cbLastFragmentSize,
	unsigned int uMACfragNum,
	unsigned char byFBOption,
	unsigned short wCurrentRate
)
{
	unsigned short wLen = 0x0000;

	if (pTxDataHead == NULL) {
		return 0;
	}

	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
		if (byFBOption == AUTO_FB_NONE) {
			PSTxDataHead_g pBuf = (PSTxDataHead_g)pTxDataHead;
			//Get SignalField,ServiceField,Length
			BBvCalculateParameter(pDevice, cbFrameLength, wCurrentRate, byPktType,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField_a), (unsigned char *)&(pBuf->bySignalField_a)
);
			pBuf->wTransmitLength_a = cpu_to_le16(wLen);
			BBvCalculateParameter(pDevice, cbFrameLength, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField_b), (unsigned char *)&(pBuf->bySignalField_b)
);
			pBuf->wTransmitLength_b = cpu_to_le16(wLen);
			//Get Duration and TimeStamp
			pBuf->wDuration_a = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength,
											   byPktType, wCurrentRate, bNeedAck, uFragIdx,
											   cbLastFragmentSize, uMACfragNum,
											   byFBOption)); //1: 2.4GHz
			pBuf->wDuration_b = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameLength,
											   PK_TYPE_11B, pDevice->byTopCCKBasicRate,
											   bNeedAck, uFragIdx, cbLastFragmentSize,
											   uMACfragNum, byFBOption)); //1: 2.4

			pBuf->wTimeStampOff_a = cpu_to_le16(wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE]);
			pBuf->wTimeStampOff_b = cpu_to_le16(wTimeStampOff[pDevice->byPreambleType%2][pDevice->byTopCCKBasicRate%MAX_RATE]);

			return pBuf->wDuration_a;
		} else {
			// Auto Fallback
			PSTxDataHead_g_FB pBuf = (PSTxDataHead_g_FB)pTxDataHead;
			//Get SignalField,ServiceField,Length
			BBvCalculateParameter(pDevice, cbFrameLength, wCurrentRate, byPktType,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField_a), (unsigned char *)&(pBuf->bySignalField_a)
);
			pBuf->wTransmitLength_a = cpu_to_le16(wLen);
			BBvCalculateParameter(pDevice, cbFrameLength, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField_b), (unsigned char *)&(pBuf->bySignalField_b)
);
			pBuf->wTransmitLength_b = cpu_to_le16(wLen);
			//Get Duration and TimeStamp
			pBuf->wDuration_a = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength, byPktType,
											   wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption)); //1: 2.4GHz
			pBuf->wDuration_b = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameLength, PK_TYPE_11B,
											   pDevice->byTopCCKBasicRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption)); //1: 2.4GHz
			pBuf->wDuration_a_f0 = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_A_F0, cbFrameLength, byPktType,
											      wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption)); //1: 2.4GHz
			pBuf->wDuration_a_f1 = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_A_F1, cbFrameLength, byPktType,
											      wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption)); //1: 2.4GHz

			pBuf->wTimeStampOff_a = cpu_to_le16(wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE]);
			pBuf->wTimeStampOff_b = cpu_to_le16(wTimeStampOff[pDevice->byPreambleType%2][pDevice->byTopCCKBasicRate%MAX_RATE]);

			return pBuf->wDuration_a;
		} //if (byFBOption == AUTO_FB_NONE)
	} else if (byPktType == PK_TYPE_11A) {
		if ((byFBOption != AUTO_FB_NONE)) {
			// Auto Fallback
			PSTxDataHead_a_FB pBuf = (PSTxDataHead_a_FB)pTxDataHead;
			//Get SignalField,ServiceField,Length
			BBvCalculateParameter(pDevice, cbFrameLength, wCurrentRate, byPktType,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField), (unsigned char *)&(pBuf->bySignalField)
);
			pBuf->wTransmitLength = cpu_to_le16(wLen);
			//Get Duration and TimeStampOff

			pBuf->wDuration = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength, byPktType,
											 wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption)); //0: 5GHz
			pBuf->wDuration_f0 = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_A_F0, cbFrameLength, byPktType,
											    wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption)); //0: 5GHz
			pBuf->wDuration_f1 = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_A_F1, cbFrameLength, byPktType,
											    wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption)); //0: 5GHz
			pBuf->wTimeStampOff = cpu_to_le16(wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE]);
			return pBuf->wDuration;
		} else {
			PSTxDataHead_ab pBuf = (PSTxDataHead_ab)pTxDataHead;
			//Get SignalField,ServiceField,Length
			BBvCalculateParameter(pDevice, cbFrameLength, wCurrentRate, byPktType,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField), (unsigned char *)&(pBuf->bySignalField)
);
			pBuf->wTransmitLength = cpu_to_le16(wLen);
			//Get Duration and TimeStampOff

			pBuf->wDuration = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength, byPktType,
											 wCurrentRate, bNeedAck, uFragIdx,
											 cbLastFragmentSize, uMACfragNum,
											 byFBOption));

			pBuf->wTimeStampOff = cpu_to_le16(wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE]);
			return pBuf->wDuration;
		}
	} else {
		PSTxDataHead_ab pBuf = (PSTxDataHead_ab)pTxDataHead;
		//Get SignalField,ServiceField,Length
		BBvCalculateParameter(pDevice, cbFrameLength, wCurrentRate, byPktType,
				      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField), (unsigned char *)&(pBuf->bySignalField)
);
		pBuf->wTransmitLength = cpu_to_le16(wLen);
		//Get Duration and TimeStampOff
		pBuf->wDuration = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameLength, byPktType,
										 wCurrentRate, bNeedAck, uFragIdx,
										 cbLastFragmentSize, uMACfragNum,
										 byFBOption));
		pBuf->wTimeStampOff = cpu_to_le16(wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE]);
		return pBuf->wDuration;
	}
	return 0;
}

static
void
s_vFillRTSHead(
	PSDevice         pDevice,
	unsigned char byPktType,
	void *pvRTS,
	unsigned int cbFrameLength,
	bool bNeedAck,
	bool bDisCRC,
	PSEthernetHeader psEthHeader,
	unsigned short wCurrentRate,
	unsigned char byFBOption
)
{
	unsigned int uRTSFrameLen = 20;
	unsigned short wLen = 0x0000;

	if (pvRTS == NULL)
		return;

	if (bDisCRC) {
		// When CRCDIS bit is on, H/W forgot to generate FCS for RTS frame,
		// in this case we need to decrease its length by 4.
		uRTSFrameLen -= 4;
	}

	// Note: So far RTSHead dosen't appear in ATIM & Beacom DMA, so we don't need to take them into account.
	//       Otherwise, we need to modify codes for them.
	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
		if (byFBOption == AUTO_FB_NONE) {
			PSRTS_g pBuf = (PSRTS_g)pvRTS;
			//Get SignalField,ServiceField,Length
			BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField_b), (unsigned char *)&(pBuf->bySignalField_b)
);
			pBuf->wTransmitLength_b = cpu_to_le16(wLen);
			BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopOFDMBasicRate, byPktType,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField_a), (unsigned char *)&(pBuf->bySignalField_a)
);
			pBuf->wTransmitLength_a = cpu_to_le16(wLen);
			//Get Duration
			pBuf->wDuration_bb = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_BB, cbFrameLength, PK_TYPE_11B, pDevice->byTopCCKBasicRate, bNeedAck, byFBOption));    //0:RTSDuration_bb, 1:2.4G, 1:CCKData
			pBuf->wDuration_aa = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //2:RTSDuration_aa, 1:2.4G, 2,3: 2.4G OFDMData
			pBuf->wDuration_ba = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_BA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //1:RTSDuration_ba, 1:2.4G, 2,3:2.4G OFDM Data

			pBuf->Data.wDurationID = pBuf->wDuration_aa;
			//Get RTS Frame body
			pBuf->Data.wFrameControl = TYPE_CTL_RTS;//0x00B4
			if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
			    (pDevice->eOPMode == OP_MODE_AP)) {
				memcpy(&(pBuf->Data.abyRA[0]), &(psEthHeader->abyDstAddr[0]), ETH_ALEN);
			} else {
				memcpy(&(pBuf->Data.abyRA[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
			}
			if (pDevice->eOPMode == OP_MODE_AP) {
				memcpy(&(pBuf->Data.abyTA[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
			} else {
				memcpy(&(pBuf->Data.abyTA[0]), &(psEthHeader->abySrcAddr[0]), ETH_ALEN);
			}
		} else {
			PSRTS_g_FB pBuf = (PSRTS_g_FB)pvRTS;
			//Get SignalField,ServiceField,Length
			BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField_b), (unsigned char *)&(pBuf->bySignalField_b)
);
			pBuf->wTransmitLength_b = cpu_to_le16(wLen);
			BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopOFDMBasicRate, byPktType,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField_a), (unsigned char *)&(pBuf->bySignalField_a)
);
			pBuf->wTransmitLength_a = cpu_to_le16(wLen);

			//Get Duration
			pBuf->wDuration_bb = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_BB, cbFrameLength, PK_TYPE_11B, pDevice->byTopCCKBasicRate, bNeedAck, byFBOption));    //0:RTSDuration_bb, 1:2.4G, 1:CCKData
			pBuf->wDuration_aa = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //2:RTSDuration_aa, 1:2.4G, 2,3:2.4G OFDMData
			pBuf->wDuration_ba = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_BA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //1:RTSDuration_ba, 1:2.4G, 2,3:2.4G OFDMData
			pBuf->wRTSDuration_ba_f0 = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_BA_F0, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption));    //4:wRTSDuration_ba_f0, 1:2.4G, 1:CCKData
			pBuf->wRTSDuration_aa_f0 = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA_F0, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption));    //5:wRTSDuration_aa_f0, 1:2.4G, 1:CCKData
			pBuf->wRTSDuration_ba_f1 = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_BA_F1, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption));    //6:wRTSDuration_ba_f1, 1:2.4G, 1:CCKData
			pBuf->wRTSDuration_aa_f1 = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA_F1, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption));    //7:wRTSDuration_aa_f1, 1:2.4G, 1:CCKData
			pBuf->Data.wDurationID = pBuf->wDuration_aa;
			//Get RTS Frame body
			pBuf->Data.wFrameControl = TYPE_CTL_RTS;//0x00B4

			if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
			    (pDevice->eOPMode == OP_MODE_AP)) {
				memcpy(&(pBuf->Data.abyRA[0]), &(psEthHeader->abyDstAddr[0]), ETH_ALEN);
			} else {
				memcpy(&(pBuf->Data.abyRA[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
			}

			if (pDevice->eOPMode == OP_MODE_AP) {
				memcpy(&(pBuf->Data.abyTA[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
			} else {
				memcpy(&(pBuf->Data.abyTA[0]), &(psEthHeader->abySrcAddr[0]), ETH_ALEN);
			}

		} // if (byFBOption == AUTO_FB_NONE)
	} else if (byPktType == PK_TYPE_11A) {
		if (byFBOption == AUTO_FB_NONE) {
			PSRTS_ab pBuf = (PSRTS_ab)pvRTS;
			//Get SignalField,ServiceField,Length
			BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopOFDMBasicRate, byPktType,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField), (unsigned char *)&(pBuf->bySignalField)
);
			pBuf->wTransmitLength = cpu_to_le16(wLen);
			//Get Duration
			pBuf->wDuration = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //0:RTSDuration_aa, 0:5G, 0: 5G OFDMData
			pBuf->Data.wDurationID = pBuf->wDuration;
			//Get RTS Frame body
			pBuf->Data.wFrameControl = TYPE_CTL_RTS;//0x00B4

			if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
			    (pDevice->eOPMode == OP_MODE_AP)) {
				memcpy(&(pBuf->Data.abyRA[0]), &(psEthHeader->abyDstAddr[0]), ETH_ALEN);
			} else {
				memcpy(&(pBuf->Data.abyRA[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
			}

			if (pDevice->eOPMode == OP_MODE_AP) {
				memcpy(&(pBuf->Data.abyTA[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
			} else {
				memcpy(&(pBuf->Data.abyTA[0]), &(psEthHeader->abySrcAddr[0]), ETH_ALEN);
			}

		} else {
			PSRTS_a_FB pBuf = (PSRTS_a_FB)pvRTS;
			//Get SignalField,ServiceField,Length
			BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopOFDMBasicRate, byPktType,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField), (unsigned char *)&(pBuf->bySignalField)
);
			pBuf->wTransmitLength = cpu_to_le16(wLen);
			//Get Duration
			pBuf->wDuration = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //0:RTSDuration_aa, 0:5G, 0: 5G OFDMData
			pBuf->wRTSDuration_f0 = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA_F0, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //5:RTSDuration_aa_f0, 0:5G, 0: 5G OFDMData
			pBuf->wRTSDuration_f1 = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_AA_F1, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //7:RTSDuration_aa_f1, 0:5G, 0:
			pBuf->Data.wDurationID = pBuf->wDuration;
			//Get RTS Frame body
			pBuf->Data.wFrameControl = TYPE_CTL_RTS;//0x00B4

			if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
			    (pDevice->eOPMode == OP_MODE_AP)) {
				memcpy(&(pBuf->Data.abyRA[0]), &(psEthHeader->abyDstAddr[0]), ETH_ALEN);
			} else {
				memcpy(&(pBuf->Data.abyRA[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
			}
			if (pDevice->eOPMode == OP_MODE_AP) {
				memcpy(&(pBuf->Data.abyTA[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
			} else {
				memcpy(&(pBuf->Data.abyTA[0]), &(psEthHeader->abySrcAddr[0]), ETH_ALEN);
			}
		}
	} else if (byPktType == PK_TYPE_11B) {
		PSRTS_ab pBuf = (PSRTS_ab)pvRTS;
		//Get SignalField,ServiceField,Length
		BBvCalculateParameter(pDevice, uRTSFrameLen, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
				      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField), (unsigned char *)&(pBuf->bySignalField)
);
		pBuf->wTransmitLength = cpu_to_le16(wLen);
		//Get Duration
		pBuf->wDuration = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, RTSDUR_BB, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //0:RTSDuration_bb, 1:2.4G, 1:CCKData
		pBuf->Data.wDurationID = pBuf->wDuration;
		//Get RTS Frame body
		pBuf->Data.wFrameControl = TYPE_CTL_RTS;//0x00B4

		if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
		    (pDevice->eOPMode == OP_MODE_AP)) {
			memcpy(&(pBuf->Data.abyRA[0]), &(psEthHeader->abyDstAddr[0]), ETH_ALEN);
		} else {
			memcpy(&(pBuf->Data.abyRA[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
		}

		if (pDevice->eOPMode == OP_MODE_AP) {
			memcpy(&(pBuf->Data.abyTA[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
		} else {
			memcpy(&(pBuf->Data.abyTA[0]), &(psEthHeader->abySrcAddr[0]), ETH_ALEN);
		}
	}
}

static
void
s_vFillCTSHead(
	PSDevice pDevice,
	unsigned int uDMAIdx,
	unsigned char byPktType,
	void *pvCTS,
	unsigned int cbFrameLength,
	bool bNeedAck,
	bool bDisCRC,
	unsigned short wCurrentRate,
	unsigned char byFBOption
)
{
	unsigned int uCTSFrameLen = 14;
	unsigned short wLen = 0x0000;

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
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField_b), (unsigned char *)&(pBuf->bySignalField_b)
);

			pBuf->wTransmitLength_b = cpu_to_le16(wLen);

			pBuf->wDuration_ba = (unsigned short)s_uGetRTSCTSDuration(pDevice, CTSDUR_BA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption); //3:CTSDuration_ba, 1:2.4G, 2,3:2.4G OFDM Data
			pBuf->wDuration_ba += pDevice->wCTSDuration;
			pBuf->wDuration_ba = cpu_to_le16(pBuf->wDuration_ba);
			//Get CTSDuration_ba_f0
			pBuf->wCTSDuration_ba_f0 = (unsigned short)s_uGetRTSCTSDuration(pDevice, CTSDUR_BA_F0, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption); //8:CTSDuration_ba_f0, 1:2.4G, 2,3:2.4G OFDM Data
			pBuf->wCTSDuration_ba_f0 += pDevice->wCTSDuration;
			pBuf->wCTSDuration_ba_f0 = cpu_to_le16(pBuf->wCTSDuration_ba_f0);
			//Get CTSDuration_ba_f1
			pBuf->wCTSDuration_ba_f1 = (unsigned short)s_uGetRTSCTSDuration(pDevice, CTSDUR_BA_F1, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption); //9:CTSDuration_ba_f1, 1:2.4G, 2,3:2.4G OFDM Data
			pBuf->wCTSDuration_ba_f1 += pDevice->wCTSDuration;
			pBuf->wCTSDuration_ba_f1 = cpu_to_le16(pBuf->wCTSDuration_ba_f1);
			//Get CTS Frame body
			pBuf->Data.wDurationID = pBuf->wDuration_ba;
			pBuf->Data.wFrameControl = TYPE_CTL_CTS;//0x00C4
			pBuf->Data.wReserved = 0x0000;
			memcpy(&(pBuf->Data.abyRA[0]), &(pDevice->abyCurrentNetAddr[0]), ETH_ALEN);

		} else { //if (byFBOption != AUTO_FB_NONE && uDMAIdx != TYPE_ATIMDMA && uDMAIdx != TYPE_BEACONDMA)
			PSCTS pBuf = (PSCTS)pvCTS;
			//Get SignalField,ServiceField,Length
			BBvCalculateParameter(pDevice, uCTSFrameLen, pDevice->byTopCCKBasicRate, PK_TYPE_11B,
					      (unsigned short *)&(wLen), (unsigned char *)&(pBuf->byServiceField_b), (unsigned char *)&(pBuf->bySignalField_b)
);
			pBuf->wTransmitLength_b = cpu_to_le16(wLen);
			//Get CTSDuration_ba
			pBuf->wDuration_ba = cpu_to_le16((unsigned short)s_uGetRTSCTSDuration(pDevice, CTSDUR_BA, cbFrameLength, byPktType, wCurrentRate, bNeedAck, byFBOption)); //3:CTSDuration_ba, 1:2.4G, 2,3:2.4G OFDM Data
			pBuf->wDuration_ba += pDevice->wCTSDuration;
			pBuf->wDuration_ba = cpu_to_le16(pBuf->wDuration_ba);

			//Get CTS Frame body
			pBuf->Data.wDurationID = pBuf->wDuration_ba;
			pBuf->Data.wFrameControl = TYPE_CTL_CTS;//0x00C4
			pBuf->Data.wReserved = 0x0000;
			memcpy(&(pBuf->Data.abyRA[0]), &(pDevice->abyCurrentNetAddr[0]), ETH_ALEN);
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
 *      pDevice         - Pointer to adapter
 *      pTxDataHead     - Transmit Data Buffer
 *      pTxBufHead      - pTxBufHead
 *      pvRrvTime        - pvRrvTime
 *      pvRTS            - RTS Buffer
 *      pCTS            - CTS Buffer
 *      cbFrameSize     - Transmit Data Length (Hdr+Payload+FCS)
 *      bNeedACK        - If need ACK
 *      uDescIdx        - Desc Index
 *  Out:
 *      none
 *
 * Return Value: none
 *
 -*/
// unsigned int cbFrameSize,//Hdr+Payload+FCS
static
void
s_vGenerateTxParameter(
	PSDevice         pDevice,
	unsigned char byPktType,
	void *pTxBufHead,
	void *pvRrvTime,
	void *pvRTS,
	void *pvCTS,
	unsigned int cbFrameSize,
	bool bNeedACK,
	unsigned int uDMAIdx,
	PSEthernetHeader psEthHeader,
	unsigned short wCurrentRate
)
{
	unsigned int cbMACHdLen = WLAN_HDR_ADDR3_LEN; //24
	unsigned short wFifoCtl;
	bool bDisCRC = false;
	unsigned char byFBOption = AUTO_FB_NONE;
//    unsigned short wCurrentRate = pDevice->wCurrentRate;

	//DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "s_vGenerateTxParameter...\n");
	PSTxBufHead pFifoHead = (PSTxBufHead)pTxBufHead;
	pFifoHead->wReserved = wCurrentRate;
	wFifoCtl = pFifoHead->wFIFOCtl;

	if (wFifoCtl & FIFOCTL_CRCDIS) {
		bDisCRC = true;
	}

	if (wFifoCtl & FIFOCTL_AUTO_FB_0) {
		byFBOption = AUTO_FB_0;
	} else if (wFifoCtl & FIFOCTL_AUTO_FB_1) {
		byFBOption = AUTO_FB_1;
	}

	if (pDevice->bLongHeader)
		cbMACHdLen = WLAN_HDR_ADDR3_LEN + 6;

	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
		if (pvRTS != NULL) { //RTS_need
			//Fill RsvTime
			if (pvRrvTime) {
				PSRrvTime_gRTS pBuf = (PSRrvTime_gRTS)pvRrvTime;
				pBuf->wRTSTxRrvTime_aa = cpu_to_le16((unsigned short)s_uGetRTSCTSRsvTime(pDevice, 2, byPktType, cbFrameSize, wCurrentRate));//2:RTSTxRrvTime_aa, 1:2.4GHz
				pBuf->wRTSTxRrvTime_ba = cpu_to_le16((unsigned short)s_uGetRTSCTSRsvTime(pDevice, 1, byPktType, cbFrameSize, wCurrentRate));//1:RTSTxRrvTime_ba, 1:2.4GHz
				pBuf->wRTSTxRrvTime_bb = cpu_to_le16((unsigned short)s_uGetRTSCTSRsvTime(pDevice, 0, byPktType, cbFrameSize, wCurrentRate));//0:RTSTxRrvTime_bb, 1:2.4GHz
				pBuf->wTxRrvTime_a = cpu_to_le16((unsigned short) s_uGetTxRsvTime(pDevice, byPktType, cbFrameSize, wCurrentRate, bNeedACK));//2.4G OFDM
				pBuf->wTxRrvTime_b = cpu_to_le16((unsigned short) s_uGetTxRsvTime(pDevice, PK_TYPE_11B, cbFrameSize, pDevice->byTopCCKBasicRate, bNeedACK));//1:CCK
			}
			//Fill RTS
			s_vFillRTSHead(pDevice, byPktType, pvRTS, cbFrameSize, bNeedACK, bDisCRC, psEthHeader, wCurrentRate, byFBOption);
		} else {//RTS_needless, PCF mode

			//Fill RsvTime
			if (pvRrvTime) {
				PSRrvTime_gCTS pBuf = (PSRrvTime_gCTS)pvRrvTime;
				pBuf->wTxRrvTime_a = cpu_to_le16((unsigned short)s_uGetTxRsvTime(pDevice, byPktType, cbFrameSize, wCurrentRate, bNeedACK));//2.4G OFDM
				pBuf->wTxRrvTime_b = cpu_to_le16((unsigned short)s_uGetTxRsvTime(pDevice, PK_TYPE_11B, cbFrameSize, pDevice->byTopCCKBasicRate, bNeedACK));//1:CCK
				pBuf->wCTSTxRrvTime_ba = cpu_to_le16((unsigned short)s_uGetRTSCTSRsvTime(pDevice, 3, byPktType, cbFrameSize, wCurrentRate));//3:CTSTxRrvTime_Ba, 1:2.4GHz
			}

			//Fill CTS
			s_vFillCTSHead(pDevice, uDMAIdx, byPktType, pvCTS, cbFrameSize, bNeedACK, bDisCRC, wCurrentRate, byFBOption);
		}
	} else if (byPktType == PK_TYPE_11A) {
		if (pvRTS != NULL) {//RTS_need, non PCF mode
			//Fill RsvTime
			if (pvRrvTime) {
				PSRrvTime_ab pBuf = (PSRrvTime_ab)pvRrvTime;
				pBuf->wRTSTxRrvTime = cpu_to_le16((unsigned short)s_uGetRTSCTSRsvTime(pDevice, 2, byPktType, cbFrameSize, wCurrentRate));//2:RTSTxRrvTime_aa, 0:5GHz
				pBuf->wTxRrvTime = cpu_to_le16((unsigned short)s_uGetTxRsvTime(pDevice, byPktType, cbFrameSize, wCurrentRate, bNeedACK));//0:OFDM
			}
			//Fill RTS
			s_vFillRTSHead(pDevice, byPktType, pvRTS, cbFrameSize, bNeedACK, bDisCRC, psEthHeader, wCurrentRate, byFBOption);
		} else if (pvRTS == NULL) {//RTS_needless, non PCF mode
			//Fill RsvTime
			if (pvRrvTime) {
				PSRrvTime_ab pBuf = (PSRrvTime_ab)pvRrvTime;
				pBuf->wTxRrvTime = cpu_to_le16((unsigned short)s_uGetTxRsvTime(pDevice, PK_TYPE_11A, cbFrameSize, wCurrentRate, bNeedACK)); //0:OFDM
			}
		}
	} else if (byPktType == PK_TYPE_11B) {
		if ((pvRTS != NULL)) {//RTS_need, non PCF mode
			//Fill RsvTime
			if (pvRrvTime) {
				PSRrvTime_ab pBuf = (PSRrvTime_ab)pvRrvTime;
				pBuf->wRTSTxRrvTime = cpu_to_le16((unsigned short)s_uGetRTSCTSRsvTime(pDevice, 0, byPktType, cbFrameSize, wCurrentRate));//0:RTSTxRrvTime_bb, 1:2.4GHz
				pBuf->wTxRrvTime = cpu_to_le16((unsigned short)s_uGetTxRsvTime(pDevice, PK_TYPE_11B, cbFrameSize, wCurrentRate, bNeedACK));//1:CCK
			}
			//Fill RTS
			s_vFillRTSHead(pDevice, byPktType, pvRTS, cbFrameSize, bNeedACK, bDisCRC, psEthHeader, wCurrentRate, byFBOption);
		} else { //RTS_needless, non PCF mode
			//Fill RsvTime
			if (pvRrvTime) {
				PSRrvTime_ab pBuf = (PSRrvTime_ab)pvRrvTime;
				pBuf->wTxRrvTime = cpu_to_le16((unsigned short)s_uGetTxRsvTime(pDevice, PK_TYPE_11B, cbFrameSize, wCurrentRate, bNeedACK)); //1:CCK
			}
		}
	}
	//DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "s_vGenerateTxParameter END.\n");
}
/*
  unsigned char *pbyBuffer,//point to pTxBufHead
  unsigned short wFragType,//00:Non-Frag, 01:Start, 02:Mid, 03:Last
  unsigned int cbFragmentSize,//Hdr+payoad+FCS
*/
static
void
s_vFillFragParameter(
	PSDevice pDevice,
	unsigned char *pbyBuffer,
	unsigned int uTxType,
	void *pvtdCurr,
	unsigned short wFragType,
	unsigned int cbReqCount
)
{
	PSTxBufHead pTxBufHead = (PSTxBufHead) pbyBuffer;
	//DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "s_vFillFragParameter...\n");

	if (uTxType == TYPE_SYNCDMA) {
		//PSTxSyncDesc ptdCurr = (PSTxSyncDesc)s_pvGetTxDescHead(pDevice, uTxType, uCurIdx);
		PSTxSyncDesc ptdCurr = (PSTxSyncDesc)pvtdCurr;

		//Set FIFOCtl & TimeStamp in TxSyncDesc
		ptdCurr->m_wFIFOCtl = pTxBufHead->wFIFOCtl;
		ptdCurr->m_wTimeStamp = pTxBufHead->wTimeStamp;
		//Set TSR1 & ReqCount in TxDescHead
		ptdCurr->m_td1TD1.wReqCount = cpu_to_le16((unsigned short)(cbReqCount));
		if (wFragType == FRAGCTL_ENDFRAG) { //Last Fragmentation
			ptdCurr->m_td1TD1.byTCR |= (TCR_STP | TCR_EDP | EDMSDU);
		} else {
			ptdCurr->m_td1TD1.byTCR |= (TCR_STP | TCR_EDP);
		}
	} else {
		//PSTxDesc ptdCurr = (PSTxDesc)s_pvGetTxDescHead(pDevice, uTxType, uCurIdx);
		PSTxDesc ptdCurr = (PSTxDesc)pvtdCurr;
		//Set TSR1 & ReqCount in TxDescHead
		ptdCurr->m_td1TD1.wReqCount = cpu_to_le16((unsigned short)(cbReqCount));
		if (wFragType == FRAGCTL_ENDFRAG) { //Last Fragmentation
			ptdCurr->m_td1TD1.byTCR |= (TCR_STP | TCR_EDP | EDMSDU);
		} else {
			ptdCurr->m_td1TD1.byTCR |= (TCR_STP | TCR_EDP);
		}
	}

	pTxBufHead->wFragCtl |= (unsigned short)wFragType;//0x0001; //0000 0000 0000 0001

	//DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "s_vFillFragParameter END\n");
}

static unsigned int
s_cbFillTxBufHead(PSDevice pDevice, unsigned char byPktType, unsigned char *pbyTxBufferAddr,
		  unsigned int cbFrameBodySize, unsigned int uDMAIdx, PSTxDesc pHeadTD,
		  PSEthernetHeader psEthHeader, unsigned char *pPacket, bool bNeedEncrypt,
		  PSKeyItem pTransmitKey, unsigned int uNodeIndex, unsigned int *puMACfragNum)
{
	unsigned int cbMACHdLen;
	unsigned int cbFrameSize;
	unsigned int cbFragmentSize; //Hdr+(IV)+payoad+(MIC)+(ICV)+FCS
	unsigned int cbFragPayloadSize;
	unsigned int cbLastFragmentSize; //Hdr+(IV)+payoad+(MIC)+(ICV)+FCS
	unsigned int cbLastFragPayloadSize;
	unsigned int uFragIdx;
	unsigned char *pbyPayloadHead;
	unsigned char *pbyIVHead;
	unsigned char *pbyMacHdr;
	unsigned short wFragType; //00:Non-Frag, 01:Start, 10:Mid, 11:Last
	unsigned int uDuration;
	unsigned char *pbyBuffer;
//    unsigned int uKeyEntryIdx = NUM_KEY_ENTRY+1;
//    unsigned char byKeySel = 0xFF;
	unsigned int cbIVlen = 0;
	unsigned int cbICVlen = 0;
	unsigned int cbMIClen = 0;
	unsigned int cbFCSlen = 4;
	unsigned int cb802_1_H_len = 0;
	unsigned int uLength = 0;
	unsigned int uTmpLen = 0;
//    unsigned char abyTmp[8];
//    unsigned long dwCRC;
	unsigned int cbMICHDR = 0;
	unsigned long dwMICKey0, dwMICKey1;
	unsigned long dwMIC_Priority;
	unsigned long *pdwMIC_L;
	unsigned long *pdwMIC_R;
	unsigned long dwSafeMIC_L, dwSafeMIC_R; //Fix "Last Frag Size" < "MIC length".
	bool bMIC2Frag = false;
	unsigned int uMICFragLen = 0;
	unsigned int uMACfragNum = 1;
	unsigned int uPadding = 0;
	unsigned int cbReqCount = 0;

	bool bNeedACK;
	bool bRTS;
	bool bIsAdhoc;
	unsigned char *pbyType;
	PSTxDesc       ptdCurr;
	PSTxBufHead    psTxBufHd = (PSTxBufHead) pbyTxBufferAddr;
//    unsigned int tmpDescIdx;
	unsigned int cbHeaderLength = 0;
	void *pvRrvTime;
	PSMICHDRHead   pMICHDR;
	void *pvRTS;
	void *pvCTS;
	void *pvTxDataHd;
	unsigned short wTxBufSize;   // FFinfo size
	unsigned int uTotalCopyLength = 0;
	unsigned char byFBOption = AUTO_FB_NONE;
	bool bIsWEP256 = false;
	PSMgmtObject    pMgmt = pDevice->pMgmt;

	pvRrvTime = pMICHDR = pvRTS = pvCTS = pvTxDataHd = NULL;

	//DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "s_cbFillTxBufHead...\n");
	if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
	    (pDevice->eOPMode == OP_MODE_AP)) {
		if (is_multicast_ether_addr(&(psEthHeader->abyDstAddr[0])))
			bNeedACK = false;
		else
			bNeedACK = true;
		bIsAdhoc = true;
	} else {
		// MSDUs in Infra mode always need ACK
		bNeedACK = true;
		bIsAdhoc = false;
	}

	if (pDevice->bLongHeader)
		cbMACHdLen = WLAN_HDR_ADDR3_LEN + 6;
	else
		cbMACHdLen = WLAN_HDR_ADDR3_LEN;

	if ((bNeedEncrypt == true) && (pTransmitKey != NULL)) {
		if (pTransmitKey->byCipherSuite == KEY_CTL_WEP) {
			cbIVlen = 4;
			cbICVlen = 4;
			if (pTransmitKey->uKeyLength == WLAN_WEP232_KEYLEN) {
				bIsWEP256 = true;
			}
		}
		if (pTransmitKey->byCipherSuite == KEY_CTL_TKIP) {
			cbIVlen = 8;//IV+ExtIV
			cbMIClen = 8;
			cbICVlen = 4;
		}
		if (pTransmitKey->byCipherSuite == KEY_CTL_CCMP) {
			cbIVlen = 8;//RSN Header
			cbICVlen = 8;//MIC
			cbMICHDR = sizeof(SMICHDRHead);
		}
		if (pDevice->byLocalID > REV_ID_VT3253_A1) {
			//MAC Header should be padding 0 to DW alignment.
			uPadding = 4 - (cbMACHdLen%4);
			uPadding %= 4;
		}
	}

	cbFrameSize = cbMACHdLen + cbIVlen + (cbFrameBodySize + cbMIClen) + cbICVlen + cbFCSlen;

	if ((bNeedACK == false) ||
	    (cbFrameSize < pDevice->wRTSThreshold) ||
	    ((cbFrameSize >= pDevice->wFragmentationThreshold) && (pDevice->wFragmentationThreshold <= pDevice->wRTSThreshold))
) {
		bRTS = false;
	} else {
		bRTS = true;
		psTxBufHd->wFIFOCtl |= (FIFOCTL_RTS | FIFOCTL_LRETRY);
	}
	//
	// Use for AUTO FALL BACK
	//
	if (psTxBufHd->wFIFOCtl & FIFOCTL_AUTO_FB_0) {
		byFBOption = AUTO_FB_0;
	} else if (psTxBufHd->wFIFOCtl & FIFOCTL_AUTO_FB_1) {
		byFBOption = AUTO_FB_1;
	}

	//////////////////////////////////////////////////////
	//Set RrvTime/RTS/CTS Buffer
	wTxBufSize = sizeof(STxBufHead);
	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {//802.11g packet

		if (byFBOption == AUTO_FB_NONE) {
			if (bRTS == true) {//RTS_need
				pvRrvTime = (PSRrvTime_gRTS) (pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gRTS));
				pvRTS = (PSRTS_g) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gRTS) + cbMICHDR);
				pvCTS = NULL;
				pvTxDataHd = (PSTxDataHead_g) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gRTS) + cbMICHDR + sizeof(SRTS_g));
				cbHeaderLength = wTxBufSize + sizeof(SRrvTime_gRTS) + cbMICHDR + sizeof(SRTS_g) + sizeof(STxDataHead_g);
			} else { //RTS_needless
				pvRrvTime = (PSRrvTime_gCTS) (pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS));
				pvRTS = NULL;
				pvCTS = (PSCTS) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR);
				pvTxDataHd = (PSTxDataHead_g) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR + sizeof(SCTS));
				cbHeaderLength = wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR + sizeof(SCTS) + sizeof(STxDataHead_g);
			}
		} else {
			// Auto Fall Back
			if (bRTS == true) {//RTS_need
				pvRrvTime = (PSRrvTime_gRTS) (pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gRTS));
				pvRTS = (PSRTS_g_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gRTS) + cbMICHDR);
				pvCTS = NULL;
				pvTxDataHd = (PSTxDataHead_g_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gRTS) + cbMICHDR + sizeof(SRTS_g_FB));
				cbHeaderLength = wTxBufSize + sizeof(SRrvTime_gRTS) + cbMICHDR + sizeof(SRTS_g_FB) + sizeof(STxDataHead_g_FB);
			} else { //RTS_needless
				pvRrvTime = (PSRrvTime_gCTS) (pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS));
				pvRTS = NULL;
				pvCTS = (PSCTS_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR);
				pvTxDataHd = (PSTxDataHead_g_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR + sizeof(SCTS_FB));
				cbHeaderLength = wTxBufSize + sizeof(SRrvTime_gCTS) + cbMICHDR + sizeof(SCTS_FB) + sizeof(STxDataHead_g_FB);
			}
		} // Auto Fall Back
	} else {//802.11a/b packet

		if (byFBOption == AUTO_FB_NONE) {
			if (bRTS == true) {
				pvRrvTime = (PSRrvTime_ab) (pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab));
				pvRTS = (PSRTS_ab) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR);
				pvCTS = NULL;
				pvTxDataHd = (PSTxDataHead_ab) (pbyTxBufferAddr + wTxBufSize + sizeof(PSRrvTime_ab) + cbMICHDR + sizeof(SRTS_ab));
				cbHeaderLength = wTxBufSize + sizeof(PSRrvTime_ab) + cbMICHDR + sizeof(SRTS_ab) + sizeof(STxDataHead_ab);
			} else { //RTS_needless, need MICHDR
				pvRrvTime = (PSRrvTime_ab) (pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab));
				pvRTS = NULL;
				pvCTS = NULL;
				pvTxDataHd = (PSTxDataHead_ab) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR);
				cbHeaderLength = wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR + sizeof(STxDataHead_ab);
			}
		} else {
			// Auto Fall Back
			if (bRTS == true) {//RTS_need
				pvRrvTime = (PSRrvTime_ab) (pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab));
				pvRTS = (PSRTS_a_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR);
				pvCTS = NULL;
				pvTxDataHd = (PSTxDataHead_a_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(PSRrvTime_ab) + cbMICHDR + sizeof(SRTS_a_FB));
				cbHeaderLength = wTxBufSize + sizeof(PSRrvTime_ab) + cbMICHDR + sizeof(SRTS_a_FB) + sizeof(STxDataHead_a_FB);
			} else { //RTS_needless
				pvRrvTime = (PSRrvTime_ab) (pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab));
				pvRTS = NULL;
				pvCTS = NULL;
				pvTxDataHd = (PSTxDataHead_a_FB) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR);
				cbHeaderLength = wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR + sizeof(STxDataHead_a_FB);
			}
		} // Auto Fall Back
	}
	memset((void *)(pbyTxBufferAddr + wTxBufSize), 0, (cbHeaderLength - wTxBufSize));

//////////////////////////////////////////////////////////////////
	if ((bNeedEncrypt == true) && (pTransmitKey != NULL) && (pTransmitKey->byCipherSuite == KEY_CTL_TKIP)) {
		if (pDevice->pMgmt->eAuthenMode == WMAC_AUTH_WPANONE) {
			dwMICKey0 = *(unsigned long *)(&pTransmitKey->abyKey[16]);
			dwMICKey1 = *(unsigned long *)(&pTransmitKey->abyKey[20]);
		} else if ((pTransmitKey->dwKeyIndex & AUTHENTICATOR_KEY) != 0) {
			dwMICKey0 = *(unsigned long *)(&pTransmitKey->abyKey[16]);
			dwMICKey1 = *(unsigned long *)(&pTransmitKey->abyKey[20]);
		} else {
			dwMICKey0 = *(unsigned long *)(&pTransmitKey->abyKey[24]);
			dwMICKey1 = *(unsigned long *)(&pTransmitKey->abyKey[28]);
		}
		// DO Software Michael
		MIC_vInit(dwMICKey0, dwMICKey1);
		MIC_vAppend((unsigned char *)&(psEthHeader->abyDstAddr[0]), 12);
		dwMIC_Priority = 0;
		MIC_vAppend((unsigned char *)&dwMIC_Priority, 4);
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "MIC KEY: %lX, %lX\n", dwMICKey0, dwMICKey1);
	}

///////////////////////////////////////////////////////////////////

	pbyMacHdr = (unsigned char *)(pbyTxBufferAddr + cbHeaderLength);
	pbyPayloadHead = (unsigned char *)(pbyMacHdr + cbMACHdLen + uPadding + cbIVlen);
	pbyIVHead = (unsigned char *)(pbyMacHdr + cbMACHdLen + uPadding);

	if ((cbFrameSize > pDevice->wFragmentationThreshold) && (bNeedACK == true) && (bIsWEP256 == false)) {
		// Fragmentation
		// FragThreshold = Fragment size(Hdr+(IV)+fragment payload+(MIC)+(ICV)+FCS)
		cbFragmentSize = pDevice->wFragmentationThreshold;
		cbFragPayloadSize = cbFragmentSize - cbMACHdLen - cbIVlen - cbICVlen - cbFCSlen;
		//FragNum = (FrameSize-(Hdr+FCS))/(Fragment Size -(Hrd+FCS)))
		uMACfragNum = (unsigned short) ((cbFrameBodySize + cbMIClen) / cbFragPayloadSize);
		cbLastFragPayloadSize = (cbFrameBodySize + cbMIClen) % cbFragPayloadSize;
		if (cbLastFragPayloadSize == 0) {
			cbLastFragPayloadSize = cbFragPayloadSize;
		} else {
			uMACfragNum++;
		}
		//[Hdr+(IV)+last fragment payload+(MIC)+(ICV)+FCS]
		cbLastFragmentSize = cbMACHdLen + cbLastFragPayloadSize + cbIVlen + cbICVlen + cbFCSlen;

		for (uFragIdx = 0; uFragIdx < uMACfragNum; uFragIdx++) {
			if (uFragIdx == 0) {
				//=========================
				//    Start Fragmentation
				//=========================
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Start Fragmentation...\n");
				wFragType = FRAGCTL_STAFRAG;

				//Fill FIFO,RrvTime,RTS,and CTS
				s_vGenerateTxParameter(pDevice, byPktType, (void *)psTxBufHd, pvRrvTime, pvRTS, pvCTS,
						       cbFragmentSize, bNeedACK, uDMAIdx, psEthHeader, pDevice->wCurrentRate);
				//Fill DataHead
				uDuration = s_uFillDataHead(pDevice, byPktType, pvTxDataHd, cbFragmentSize, uDMAIdx, bNeedACK,
							    uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption, pDevice->wCurrentRate);
				// Generate TX MAC Header
				vGenerateMACHeader(pDevice, pbyMacHdr, (unsigned short)uDuration, psEthHeader, bNeedEncrypt,
						   wFragType, uDMAIdx, uFragIdx);

				if (bNeedEncrypt == true) {
					//Fill TXKEY
					s_vFillTxKey(pDevice, (unsigned char *)(psTxBufHd->adwTxKey), pbyIVHead, pTransmitKey,
						     pbyMacHdr, (unsigned short)cbFragPayloadSize, (unsigned char *)pMICHDR);
					//Fill IV(ExtIV,RSNHDR)
					if (pDevice->bEnableHostWEP) {
						pMgmt->sNodeDBTable[uNodeIndex].dwTSC47_16 = pTransmitKey->dwTSC47_16;
						pMgmt->sNodeDBTable[uNodeIndex].wTSC15_0 = pTransmitKey->wTSC15_0;
					}
				}

				// 802.1H
				if (ntohs(psEthHeader->wType) > ETH_DATA_LEN) {
					if ((psEthHeader->wType == TYPE_PKT_IPX) ||
					    (psEthHeader->wType == cpu_to_le16(0xF380))) {
						memcpy((unsigned char *)(pbyPayloadHead), &pDevice->abySNAP_Bridgetunnel[0], 6);
					} else {
						memcpy((unsigned char *)(pbyPayloadHead), &pDevice->abySNAP_RFC1042[0], 6);
					}
					pbyType = (unsigned char *)(pbyPayloadHead + 6);
					memcpy(pbyType, &(psEthHeader->wType), sizeof(unsigned short));
					cb802_1_H_len = 8;
				}

				cbReqCount = cbHeaderLength + cbMACHdLen + uPadding + cbIVlen + cbFragPayloadSize;
				//---------------------------
				// S/W or H/W Encryption
				//---------------------------
				pbyBuffer = (unsigned char *)pHeadTD->pTDInfo->buf;

				uLength = cbHeaderLength + cbMACHdLen + uPadding + cbIVlen + cb802_1_H_len;
				//copy TxBufferHeader + MacHeader to desc
				memcpy(pbyBuffer, (void *)psTxBufHd, uLength);

				// Copy the Packet into a tx Buffer
				memcpy((pbyBuffer + uLength), (pPacket + 14), (cbFragPayloadSize - cb802_1_H_len));

				uTotalCopyLength += cbFragPayloadSize - cb802_1_H_len;

				if ((bNeedEncrypt == true) && (pTransmitKey != NULL) && (pTransmitKey->byCipherSuite == KEY_CTL_TKIP)) {
					DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Start MIC: %d\n", cbFragPayloadSize);
					MIC_vAppend((pbyBuffer + uLength - cb802_1_H_len), cbFragPayloadSize);

				}

				//---------------------------
				// S/W Encryption
				//---------------------------
				if ((pDevice->byLocalID <= REV_ID_VT3253_A1)) {
					if (bNeedEncrypt) {
						s_vSWencryption(pDevice, pTransmitKey, (pbyBuffer + uLength - cb802_1_H_len), (unsigned short)cbFragPayloadSize);
						cbReqCount += cbICVlen;
					}
				}

				ptdCurr = (PSTxDesc)pHeadTD;
				//--------------------
				//1.Set TSR1 & ReqCount in TxDescHead
				//2.Set FragCtl in TxBufferHead
				//3.Set Frame Control
				//4.Set Sequence Control
				//5.Get S/W generate FCS
				//--------------------
				s_vFillFragParameter(pDevice, pbyBuffer, uDMAIdx, (void *)ptdCurr, wFragType, cbReqCount);

				ptdCurr->pTDInfo->dwReqCount = cbReqCount - uPadding;
				ptdCurr->pTDInfo->dwHeaderLength = cbHeaderLength;
				ptdCurr->pTDInfo->skb_dma = ptdCurr->pTDInfo->buf_dma;
				ptdCurr->buff_addr = cpu_to_le32(ptdCurr->pTDInfo->skb_dma);
				pDevice->iTDUsed[uDMAIdx]++;
				pHeadTD = ptdCurr->next;
			} else if (uFragIdx == (uMACfragNum-1)) {
				//=========================
				//    Last Fragmentation
				//=========================
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Last Fragmentation...\n");
				//tmpDescIdx = (uDescIdx + uFragIdx) % pDevice->cbTD[uDMAIdx];

				wFragType = FRAGCTL_ENDFRAG;

				//Fill FIFO,RrvTime,RTS,and CTS
				s_vGenerateTxParameter(pDevice, byPktType, (void *)psTxBufHd, pvRrvTime, pvRTS, pvCTS,
						       cbLastFragmentSize, bNeedACK, uDMAIdx, psEthHeader, pDevice->wCurrentRate);
				//Fill DataHead
				uDuration = s_uFillDataHead(pDevice, byPktType, pvTxDataHd, cbLastFragmentSize, uDMAIdx, bNeedACK,
							    uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption, pDevice->wCurrentRate);

				// Generate TX MAC Header
				vGenerateMACHeader(pDevice, pbyMacHdr, (unsigned short)uDuration, psEthHeader, bNeedEncrypt,
						   wFragType, uDMAIdx, uFragIdx);

				if (bNeedEncrypt == true) {
					//Fill TXKEY
					s_vFillTxKey(pDevice, (unsigned char *)(psTxBufHd->adwTxKey), pbyIVHead, pTransmitKey,
						     pbyMacHdr, (unsigned short)cbLastFragPayloadSize, (unsigned char *)pMICHDR);

					if (pDevice->bEnableHostWEP) {
						pMgmt->sNodeDBTable[uNodeIndex].dwTSC47_16 = pTransmitKey->dwTSC47_16;
						pMgmt->sNodeDBTable[uNodeIndex].wTSC15_0 = pTransmitKey->wTSC15_0;
					}

				}

				cbReqCount = cbHeaderLength + cbMACHdLen + uPadding + cbIVlen + cbLastFragPayloadSize;
				//---------------------------
				// S/W or H/W Encryption
				//---------------------------

				pbyBuffer = (unsigned char *)pHeadTD->pTDInfo->buf;
				//pbyBuffer = (unsigned char *)pDevice->aamTxBuf[uDMAIdx][tmpDescIdx].pbyVAddr;

				uLength = cbHeaderLength + cbMACHdLen + uPadding + cbIVlen;

				//copy TxBufferHeader + MacHeader to desc
				memcpy(pbyBuffer, (void *)psTxBufHd, uLength);

				// Copy the Packet into a tx Buffer
				if (bMIC2Frag == false) {
					memcpy((pbyBuffer + uLength),
					       (pPacket + 14 + uTotalCopyLength),
					       (cbLastFragPayloadSize - cbMIClen)
);
					//TODO check uTmpLen !
					uTmpLen = cbLastFragPayloadSize - cbMIClen;

				}
				if ((bNeedEncrypt == true) && (pTransmitKey != NULL) && (pTransmitKey->byCipherSuite == KEY_CTL_TKIP)) {
					DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "LAST: uMICFragLen:%d, cbLastFragPayloadSize:%d, uTmpLen:%d\n",
						uMICFragLen, cbLastFragPayloadSize, uTmpLen);

					if (bMIC2Frag == false) {
						if (uTmpLen != 0)
							MIC_vAppend((pbyBuffer + uLength), uTmpLen);
						pdwMIC_L = (unsigned long *)(pbyBuffer + uLength + uTmpLen);
						pdwMIC_R = (unsigned long *)(pbyBuffer + uLength + uTmpLen + 4);
						MIC_vGetMIC(pdwMIC_L, pdwMIC_R);
						DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Last MIC:%lX, %lX\n", *pdwMIC_L, *pdwMIC_R);
					} else {
						if (uMICFragLen >= 4) {
							memcpy((pbyBuffer + uLength), ((unsigned char *)&dwSafeMIC_R + (uMICFragLen - 4)),
							       (cbMIClen - uMICFragLen));
							DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "LAST: uMICFragLen >= 4: %X, %d\n",
								*(unsigned char *)((unsigned char *)&dwSafeMIC_R + (uMICFragLen - 4)),
								(cbMIClen - uMICFragLen));

						} else {
							memcpy((pbyBuffer + uLength), ((unsigned char *)&dwSafeMIC_L + uMICFragLen),
							       (4 - uMICFragLen));
							memcpy((pbyBuffer + uLength + (4 - uMICFragLen)), &dwSafeMIC_R, 4);
							DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "LAST: uMICFragLen < 4: %X, %d\n",
								*(unsigned char *)((unsigned char *)&dwSafeMIC_R + uMICFragLen - 4),
								(cbMIClen - uMICFragLen));
						}
						/*
						  for (ii = 0; ii < cbLastFragPayloadSize + 8 + 24; ii++) {
						  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "%02x ", *((unsigned char *)((pbyBuffer + uLength) + ii - 8 - 24)));
						  }
						  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "\n\n");
						*/
					}
					MIC_vUnInit();
				} else {
					ASSERT(uTmpLen == (cbLastFragPayloadSize - cbMIClen));
				}

				//---------------------------
				// S/W Encryption
				//---------------------------
				if ((pDevice->byLocalID <= REV_ID_VT3253_A1)) {
					if (bNeedEncrypt) {
						s_vSWencryption(pDevice, pTransmitKey, (pbyBuffer + uLength), (unsigned short)cbLastFragPayloadSize);
						cbReqCount += cbICVlen;
					}
				}

				ptdCurr = (PSTxDesc)pHeadTD;

				//--------------------
				//1.Set TSR1 & ReqCount in TxDescHead
				//2.Set FragCtl in TxBufferHead
				//3.Set Frame Control
				//4.Set Sequence Control
				//5.Get S/W generate FCS
				//--------------------

				s_vFillFragParameter(pDevice, pbyBuffer, uDMAIdx, (void *)ptdCurr, wFragType, cbReqCount);

				ptdCurr->pTDInfo->dwReqCount = cbReqCount - uPadding;
				ptdCurr->pTDInfo->dwHeaderLength = cbHeaderLength;
				ptdCurr->pTDInfo->skb_dma = ptdCurr->pTDInfo->buf_dma;
				ptdCurr->buff_addr = cpu_to_le32(ptdCurr->pTDInfo->skb_dma);
				pDevice->iTDUsed[uDMAIdx]++;
				pHeadTD = ptdCurr->next;

			} else {
				//=========================
				//    Middle Fragmentation
				//=========================
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Middle Fragmentation...\n");
				//tmpDescIdx = (uDescIdx + uFragIdx) % pDevice->cbTD[uDMAIdx];

				wFragType = FRAGCTL_MIDFRAG;

				//Fill FIFO,RrvTime,RTS,and CTS
				s_vGenerateTxParameter(pDevice, byPktType, (void *)psTxBufHd, pvRrvTime, pvRTS, pvCTS,
						       cbFragmentSize, bNeedACK, uDMAIdx, psEthHeader, pDevice->wCurrentRate);
				//Fill DataHead
				uDuration = s_uFillDataHead(pDevice, byPktType, pvTxDataHd, cbFragmentSize, uDMAIdx, bNeedACK,
							    uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption, pDevice->wCurrentRate);

				// Generate TX MAC Header
				vGenerateMACHeader(pDevice, pbyMacHdr, (unsigned short)uDuration, psEthHeader, bNeedEncrypt,
						   wFragType, uDMAIdx, uFragIdx);

				if (bNeedEncrypt == true) {
					//Fill TXKEY
					s_vFillTxKey(pDevice, (unsigned char *)(psTxBufHd->adwTxKey), pbyIVHead, pTransmitKey,
						     pbyMacHdr, (unsigned short)cbFragPayloadSize, (unsigned char *)pMICHDR);

					if (pDevice->bEnableHostWEP) {
						pMgmt->sNodeDBTable[uNodeIndex].dwTSC47_16 = pTransmitKey->dwTSC47_16;
						pMgmt->sNodeDBTable[uNodeIndex].wTSC15_0 = pTransmitKey->wTSC15_0;
					}
				}

				cbReqCount = cbHeaderLength + cbMACHdLen + uPadding + cbIVlen + cbFragPayloadSize;
				//---------------------------
				// S/W or H/W Encryption
				//---------------------------

				pbyBuffer = (unsigned char *)pHeadTD->pTDInfo->buf;
				uLength = cbHeaderLength + cbMACHdLen + uPadding + cbIVlen;

				//copy TxBufferHeader + MacHeader to desc
				memcpy(pbyBuffer, (void *)psTxBufHd, uLength);

				// Copy the Packet into a tx Buffer
				memcpy((pbyBuffer + uLength),
				       (pPacket + 14 + uTotalCopyLength),
				       cbFragPayloadSize
);
				uTmpLen = cbFragPayloadSize;

				uTotalCopyLength += uTmpLen;

				if ((bNeedEncrypt == true) && (pTransmitKey != NULL) && (pTransmitKey->byCipherSuite == KEY_CTL_TKIP)) {
					MIC_vAppend((pbyBuffer + uLength), uTmpLen);

					if (uTmpLen < cbFragPayloadSize) {
						bMIC2Frag = true;
						uMICFragLen = cbFragPayloadSize - uTmpLen;
						ASSERT(uMICFragLen < cbMIClen);

						pdwMIC_L = (unsigned long *)(pbyBuffer + uLength + uTmpLen);
						pdwMIC_R = (unsigned long *)(pbyBuffer + uLength + uTmpLen + 4);
						MIC_vGetMIC(pdwMIC_L, pdwMIC_R);
						dwSafeMIC_L = *pdwMIC_L;
						dwSafeMIC_R = *pdwMIC_R;

						DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "MIDDLE: uMICFragLen:%d, cbFragPayloadSize:%d, uTmpLen:%d\n",
							uMICFragLen, cbFragPayloadSize, uTmpLen);
						DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Fill MIC in Middle frag [%d]\n", uMICFragLen);
						/*
						  for (ii = 0; ii < uMICFragLen; ii++) {
						  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "%02x ", *((unsigned char *)((pbyBuffer + uLength + uTmpLen) + ii)));
						  }
						  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "\n");
						*/
						DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Get MIC:%lX, %lX\n", *pdwMIC_L, *pdwMIC_R);
					}
					DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Middle frag len: %d\n", uTmpLen);
					/*
					  for (ii = 0; ii < uTmpLen; ii++) {
					  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "%02x ", *((unsigned char *)((pbyBuffer + uLength) + ii)));
					  }
					  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "\n\n");
					*/

				} else {
					ASSERT(uTmpLen == (cbFragPayloadSize));
				}

				if ((pDevice->byLocalID <= REV_ID_VT3253_A1)) {
					if (bNeedEncrypt) {
						s_vSWencryption(pDevice, pTransmitKey, (pbyBuffer + uLength), (unsigned short)cbFragPayloadSize);
						cbReqCount += cbICVlen;
					}
				}

				ptdCurr = (PSTxDesc)pHeadTD;

				//--------------------
				//1.Set TSR1 & ReqCount in TxDescHead
				//2.Set FragCtl in TxBufferHead
				//3.Set Frame Control
				//4.Set Sequence Control
				//5.Get S/W generate FCS
				//--------------------

				s_vFillFragParameter(pDevice, pbyBuffer, uDMAIdx, (void *)ptdCurr, wFragType, cbReqCount);

				ptdCurr->pTDInfo->dwReqCount = cbReqCount - uPadding;
				ptdCurr->pTDInfo->dwHeaderLength = cbHeaderLength;
				ptdCurr->pTDInfo->skb_dma = ptdCurr->pTDInfo->buf_dma;
				ptdCurr->buff_addr = cpu_to_le32(ptdCurr->pTDInfo->skb_dma);
				pDevice->iTDUsed[uDMAIdx]++;
				pHeadTD = ptdCurr->next;
			}
		}  // for (uMACfragNum)
	} else {
		//=========================
		//    No Fragmentation
		//=========================
		//DBG_PRTGRP03(("No Fragmentation...\n"));
		//DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "No Fragmentation...\n");
		wFragType = FRAGCTL_NONFRAG;

		//Set FragCtl in TxBufferHead
		psTxBufHd->wFragCtl |= (unsigned short)wFragType;

		//Fill FIFO,RrvTime,RTS,and CTS
		s_vGenerateTxParameter(pDevice, byPktType, (void *)psTxBufHd, pvRrvTime, pvRTS, pvCTS,
				       cbFrameSize, bNeedACK, uDMAIdx, psEthHeader, pDevice->wCurrentRate);
		//Fill DataHead
		uDuration = s_uFillDataHead(pDevice, byPktType, pvTxDataHd, cbFrameSize, uDMAIdx, bNeedACK,
					    0, 0, uMACfragNum, byFBOption, pDevice->wCurrentRate);

		// Generate TX MAC Header
		vGenerateMACHeader(pDevice, pbyMacHdr, (unsigned short)uDuration, psEthHeader, bNeedEncrypt,
				   wFragType, uDMAIdx, 0);

		if (bNeedEncrypt == true) {
			//Fill TXKEY
			s_vFillTxKey(pDevice, (unsigned char *)(psTxBufHd->adwTxKey), pbyIVHead, pTransmitKey,
				     pbyMacHdr, (unsigned short)cbFrameBodySize, (unsigned char *)pMICHDR);

			if (pDevice->bEnableHostWEP) {
				pMgmt->sNodeDBTable[uNodeIndex].dwTSC47_16 = pTransmitKey->dwTSC47_16;
				pMgmt->sNodeDBTable[uNodeIndex].wTSC15_0 = pTransmitKey->wTSC15_0;
			}
		}

		// 802.1H
		if (ntohs(psEthHeader->wType) > ETH_DATA_LEN) {
			if ((psEthHeader->wType == TYPE_PKT_IPX) ||
			    (psEthHeader->wType == cpu_to_le16(0xF380))) {
				memcpy((unsigned char *)(pbyPayloadHead), &pDevice->abySNAP_Bridgetunnel[0], 6);
			} else {
				memcpy((unsigned char *)(pbyPayloadHead), &pDevice->abySNAP_RFC1042[0], 6);
			}
			pbyType = (unsigned char *)(pbyPayloadHead + 6);
			memcpy(pbyType, &(psEthHeader->wType), sizeof(unsigned short));
			cb802_1_H_len = 8;
		}

		cbReqCount = cbHeaderLength + cbMACHdLen + uPadding + cbIVlen + (cbFrameBodySize + cbMIClen);
		//---------------------------
		// S/W or H/W Encryption
		//---------------------------
		pbyBuffer = (unsigned char *)pHeadTD->pTDInfo->buf;
		uLength = cbHeaderLength + cbMACHdLen + uPadding + cbIVlen + cb802_1_H_len;

		//copy TxBufferHeader + MacHeader to desc
		memcpy(pbyBuffer, (void *)psTxBufHd, uLength);

		// Copy the Packet into a tx Buffer
		memcpy((pbyBuffer + uLength),
		       (pPacket + 14),
		       cbFrameBodySize - cb802_1_H_len
);

		if ((bNeedEncrypt == true) && (pTransmitKey != NULL) && (pTransmitKey->byCipherSuite == KEY_CTL_TKIP)) {
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Length:%d, %d\n", cbFrameBodySize - cb802_1_H_len, uLength);
			/*
			  for (ii = 0; ii < (cbFrameBodySize - cb802_1_H_len); ii++) {
			  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "%02x ", *((unsigned char *)((pbyBuffer + uLength) + ii)));
			  }
			  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "\n");
			*/

			MIC_vAppend((pbyBuffer + uLength - cb802_1_H_len), cbFrameBodySize);

			pdwMIC_L = (unsigned long *)(pbyBuffer + uLength - cb802_1_H_len + cbFrameBodySize);
			pdwMIC_R = (unsigned long *)(pbyBuffer + uLength - cb802_1_H_len + cbFrameBodySize + 4);

			MIC_vGetMIC(pdwMIC_L, pdwMIC_R);
			MIC_vUnInit();

			if (pDevice->bTxMICFail == true) {
				*pdwMIC_L = 0;
				*pdwMIC_R = 0;
				pDevice->bTxMICFail = false;
			}

			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "uLength: %d, %d\n", uLength, cbFrameBodySize);
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "cbReqCount:%d, %d, %d, %d\n", cbReqCount, cbHeaderLength, uPadding, cbIVlen);
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "MIC:%lx, %lx\n", *pdwMIC_L, *pdwMIC_R);
/*
  for (ii = 0; ii < 8; ii++) {
  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "%02x ", *(((unsigned char *)(pdwMIC_L) + ii)));
  }
  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "\n");
*/

		}

		if ((pDevice->byLocalID <= REV_ID_VT3253_A1)) {
			if (bNeedEncrypt) {
				s_vSWencryption(pDevice, pTransmitKey, (pbyBuffer + uLength - cb802_1_H_len),
						(unsigned short)(cbFrameBodySize + cbMIClen));
				cbReqCount += cbICVlen;
			}
		}

		ptdCurr = (PSTxDesc)pHeadTD;

		ptdCurr->pTDInfo->dwReqCount = cbReqCount - uPadding;
		ptdCurr->pTDInfo->dwHeaderLength = cbHeaderLength;
		ptdCurr->pTDInfo->skb_dma = ptdCurr->pTDInfo->buf_dma;
		ptdCurr->buff_addr = cpu_to_le32(ptdCurr->pTDInfo->skb_dma);
		//Set TSR1 & ReqCount in TxDescHead
		ptdCurr->m_td1TD1.byTCR |= (TCR_STP | TCR_EDP | EDMSDU);
		ptdCurr->m_td1TD1.wReqCount = cpu_to_le16((unsigned short)(cbReqCount));

		pDevice->iTDUsed[uDMAIdx]++;

//   DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " ptdCurr->m_dwReserved0[%d] ptdCurr->m_dwReserved1[%d].\n", ptdCurr->pTDInfo->dwReqCount, ptdCurr->pTDInfo->dwHeaderLength);
//   DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " cbHeaderLength[%d]\n", cbHeaderLength);

	}
	*puMACfragNum = uMACfragNum;
	//DBG_PRTGRP03(("s_cbFillTxBufHead END\n"));
	return cbHeaderLength;
}

void
vGenerateFIFOHeader(PSDevice pDevice, unsigned char byPktType, unsigned char *pbyTxBufferAddr,
		    bool bNeedEncrypt, unsigned int cbPayloadSize, unsigned int uDMAIdx,
		    PSTxDesc pHeadTD, PSEthernetHeader psEthHeader, unsigned char *pPacket,
		    PSKeyItem pTransmitKey, unsigned int uNodeIndex, unsigned int *puMACfragNum,
		    unsigned int *pcbHeaderSize)
{
	unsigned int wTxBufSize;       // FFinfo size
	bool bNeedACK;
	bool bIsAdhoc;
	unsigned short cbMacHdLen;
	PSTxBufHead     pTxBufHead = (PSTxBufHead) pbyTxBufferAddr;

	wTxBufSize = sizeof(STxBufHead);

	memset(pTxBufHead, 0, wTxBufSize);
	//Set FIFOCTL_NEEDACK

	if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
	    (pDevice->eOPMode == OP_MODE_AP)) {
		if (is_multicast_ether_addr(&(psEthHeader->abyDstAddr[0]))) {
			bNeedACK = false;
			pTxBufHead->wFIFOCtl = pTxBufHead->wFIFOCtl & (~FIFOCTL_NEEDACK);
		} else {
			bNeedACK = true;
			pTxBufHead->wFIFOCtl |= FIFOCTL_NEEDACK;
		}
		bIsAdhoc = true;
	} else {
		// MSDUs in Infra mode always need ACK
		bNeedACK = true;
		pTxBufHead->wFIFOCtl |= FIFOCTL_NEEDACK;
		bIsAdhoc = false;
	}

	pTxBufHead->wFIFOCtl |= FIFOCTL_TMOEN;
	pTxBufHead->wTimeStamp = cpu_to_le16(DEFAULT_MSDU_LIFETIME_RES_64us);

	//Set FIFOCTL_LHEAD
	if (pDevice->bLongHeader)
		pTxBufHead->wFIFOCtl |= FIFOCTL_LHEAD;

	//Set FIFOCTL_GENINT

	pTxBufHead->wFIFOCtl |= FIFOCTL_GENINT;

	//Set FIFOCTL_ISDMA0
	if (TYPE_TXDMA0 == uDMAIdx) {
		pTxBufHead->wFIFOCtl |= FIFOCTL_ISDMA0;
	}

	//Set FRAGCTL_MACHDCNT
	if (pDevice->bLongHeader) {
		cbMacHdLen = WLAN_HDR_ADDR3_LEN + 6;
	} else {
		cbMacHdLen = WLAN_HDR_ADDR3_LEN;
	}
	pTxBufHead->wFragCtl |= cpu_to_le16((unsigned short)(cbMacHdLen << 10));

	//Set packet type
	if (byPktType == PK_TYPE_11A) {//0000 0000 0000 0000
		;
	} else if (byPktType == PK_TYPE_11B) {//0000 0001 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11B;
	} else if (byPktType == PK_TYPE_11GB) {//0000 0010 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11GB;
	} else if (byPktType == PK_TYPE_11GA) {//0000 0011 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11GA;
	}
	//Set FIFOCTL_GrpAckPolicy
	if (pDevice->bGrpAckPolicy == true) {//0000 0100 0000 0000
		pTxBufHead->wFIFOCtl |=	FIFOCTL_GRPACK;
	}

	//Set Auto Fallback Ctl
	if (pDevice->wCurrentRate >= RATE_18M) {
		if (pDevice->byAutoFBCtrl == AUTO_FB_0) {
			pTxBufHead->wFIFOCtl |= FIFOCTL_AUTO_FB_0;
		} else if (pDevice->byAutoFBCtrl == AUTO_FB_1) {
			pTxBufHead->wFIFOCtl |= FIFOCTL_AUTO_FB_1;
		}
	}

	//Set FRAGCTL_WEPTYP
	pDevice->bAES = false;

	//Set FRAGCTL_WEPTYP
	if (pDevice->byLocalID > REV_ID_VT3253_A1) {
		if ((bNeedEncrypt) && (pTransmitKey != NULL))  { //WEP enabled
			if (pTransmitKey->byCipherSuite == KEY_CTL_TKIP) {
				pTxBufHead->wFragCtl |= FRAGCTL_TKIP;
			} else if (pTransmitKey->byCipherSuite == KEY_CTL_WEP) { //WEP40 or WEP104
				if (pTransmitKey->uKeyLength != WLAN_WEP232_KEYLEN)
					pTxBufHead->wFragCtl |= FRAGCTL_LEGACY;
			} else if (pTransmitKey->byCipherSuite == KEY_CTL_CCMP) { //CCMP
				pTxBufHead->wFragCtl |= FRAGCTL_AES;
			}
		}
	}

#ifdef	PLICE_DEBUG
	RFbSetPower(pDevice, pDevice->wCurrentRate, pDevice->byCurrentCh);
#endif
	pTxBufHead->byTxPower = pDevice->byCurPwr;

/*
  if (pDevice->bEnableHostWEP)
  pTxBufHead->wFragCtl &=  ~(FRAGCTL_TKIP | FRAGCTL_LEGACY |FRAGCTL_AES);
*/
	*pcbHeaderSize = s_cbFillTxBufHead(pDevice, byPktType, pbyTxBufferAddr, cbPayloadSize,
					   uDMAIdx, pHeadTD, psEthHeader, pPacket, bNeedEncrypt,
					   pTransmitKey, uNodeIndex, puMACfragNum);

	return;
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
vGenerateMACHeader(
	PSDevice         pDevice,
	unsigned char *pbyBufferAddr,
	unsigned short wDuration,
	PSEthernetHeader psEthHeader,
	bool bNeedEncrypt,
	unsigned short wFragType,
	unsigned int uDMAIdx,
	unsigned int uFragIdx
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
		memcpy(&(pMACHeader->abyAddr1[0]), &(psEthHeader->abyDstAddr[0]), ETH_ALEN);
		memcpy(&(pMACHeader->abyAddr2[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
		memcpy(&(pMACHeader->abyAddr3[0]), &(psEthHeader->abySrcAddr[0]), ETH_ALEN);
		pMACHeader->wFrameCtl |= FC_FROMDS;
	} else {
		if (pDevice->eOPMode == OP_MODE_ADHOC) {
			memcpy(&(pMACHeader->abyAddr1[0]), &(psEthHeader->abyDstAddr[0]), ETH_ALEN);
			memcpy(&(pMACHeader->abyAddr2[0]), &(psEthHeader->abySrcAddr[0]), ETH_ALEN);
			memcpy(&(pMACHeader->abyAddr3[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
		} else {
			memcpy(&(pMACHeader->abyAddr3[0]), &(psEthHeader->abyDstAddr[0]), ETH_ALEN);
			memcpy(&(pMACHeader->abyAddr2[0]), &(psEthHeader->abySrcAddr[0]), ETH_ALEN);
			memcpy(&(pMACHeader->abyAddr1[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
			pMACHeader->wFrameCtl |= FC_TODS;
		}
	}

	if (bNeedEncrypt)
		pMACHeader->wFrameCtl |= cpu_to_le16((unsigned short)WLAN_SET_FC_ISWEP(1));

	pMACHeader->wDurationID = cpu_to_le16(wDuration);

	if (pDevice->bLongHeader) {
		PWLAN_80211HDR_A4 pMACA4Header  = (PWLAN_80211HDR_A4) pbyBufferAddr;
		pMACHeader->wFrameCtl |= (FC_TODS | FC_FROMDS);
		memcpy(pMACA4Header->abyAddr4, pDevice->abyBSSID, WLAN_ADDR_LEN);
	}
	pMACHeader->wSeqCtl = cpu_to_le16(pDevice->wSeqCounter << 4);

	//Set FragNumber in Sequence Control
	pMACHeader->wSeqCtl |= cpu_to_le16((unsigned short)uFragIdx);

	if ((wFragType == FRAGCTL_ENDFRAG) || (wFragType == FRAGCTL_NONFRAG)) {
		pDevice->wSeqCounter++;
		if (pDevice->wSeqCounter > 0x0fff)
			pDevice->wSeqCounter = 0;
	}

	if ((wFragType == FRAGCTL_STAFRAG) || (wFragType == FRAGCTL_MIDFRAG)) { //StartFrag or MidFrag
		pMACHeader->wFrameCtl |= FC_MOREFRAG;
	}
}

CMD_STATUS csMgmt_xmit(PSDevice pDevice, PSTxMgmtPacket pPacket) {
	PSTxDesc        pFrstTD;
	unsigned char byPktType;
	unsigned char *pbyTxBufferAddr;
	void *pvRTS;
	PSCTS           pCTS;
	void *pvTxDataHd;
	unsigned int uDuration;
	unsigned int cbReqCount;
	PS802_11Header  pMACHeader;
	unsigned int cbHeaderSize;
	unsigned int cbFrameBodySize;
	bool bNeedACK;
	bool bIsPSPOLL = false;
	PSTxBufHead     pTxBufHead;
	unsigned int cbFrameSize;
	unsigned int cbIVlen = 0;
	unsigned int cbICVlen = 0;
	unsigned int cbMIClen = 0;
	unsigned int cbFCSlen = 4;
	unsigned int uPadding = 0;
	unsigned short wTxBufSize;
	unsigned int cbMacHdLen;
	SEthernetHeader sEthHeader;
	void *pvRrvTime;
	void *pMICHDR;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	unsigned short wCurrentRate = RATE_1M;

	if (AVAIL_TD(pDevice, TYPE_TXDMA0) <= 0) {
		return CMD_STATUS_RESOURCES;
	}

	pFrstTD = pDevice->apCurrTD[TYPE_TXDMA0];
	pbyTxBufferAddr = (unsigned char *)pFrstTD->pTDInfo->buf;
	cbFrameBodySize = pPacket->cbPayloadLen;
	pTxBufHead = (PSTxBufHead) pbyTxBufferAddr;
	wTxBufSize = sizeof(STxBufHead);
	memset(pTxBufHead, 0, wTxBufSize);

	if (pDevice->eCurrentPHYType == PHY_TYPE_11A) {
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
	if (pDevice->pMgmt->eScanState != WMAC_NO_SCANNING) {
		RFbSetPower(pDevice, wCurrentRate, pDevice->byCurrentCh);
	} else {
		RFbSetPower(pDevice, wCurrentRate, pMgmt->uCurrChannel);
	}
	pTxBufHead->byTxPower = pDevice->byCurPwr;
	//+++++++++++++++++++++ Patch VT3253 A1 performance +++++++++++++++++++++++++++
	if (pDevice->byFOETuning) {
		if ((pPacket->p80211Header->sA3.wFrameCtl & TYPE_DATE_NULL) == TYPE_DATE_NULL) {
			wCurrentRate = RATE_24M;
			byPktType = PK_TYPE_11GA;
		}
	}

	//Set packet type
	if (byPktType == PK_TYPE_11A) {//0000 0000 0000 0000
		pTxBufHead->wFIFOCtl = 0;
	} else if (byPktType == PK_TYPE_11B) {//0000 0001 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11B;
	} else if (byPktType == PK_TYPE_11GB) {//0000 0010 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11GB;
	} else if (byPktType == PK_TYPE_11GA) {//0000 0011 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11GA;
	}

	pTxBufHead->wFIFOCtl |= FIFOCTL_TMOEN;
	pTxBufHead->wTimeStamp = cpu_to_le16(DEFAULT_MGN_LIFETIME_RES_64us);

	if (is_multicast_ether_addr(&(pPacket->p80211Header->sA3.abyAddr1[0])))
		bNeedACK = false;
	else {
		bNeedACK = true;
		pTxBufHead->wFIFOCtl |= FIFOCTL_NEEDACK;
	};

	if ((pMgmt->eCurrMode == WMAC_MODE_ESS_AP) ||
	    (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA)) {
		pTxBufHead->wFIFOCtl |= FIFOCTL_LRETRY;
	}

	pTxBufHead->wFIFOCtl |= (FIFOCTL_GENINT | FIFOCTL_ISDMA0);

	if ((pPacket->p80211Header->sA4.wFrameCtl & TYPE_SUBTYPE_MASK) == TYPE_CTL_PSPOLL) {
		bIsPSPOLL = true;
		cbMacHdLen = WLAN_HDR_ADDR2_LEN;
	} else {
		cbMacHdLen = WLAN_HDR_ADDR3_LEN;
	}

	//Set FRAGCTL_MACHDCNT
	pTxBufHead->wFragCtl |= cpu_to_le16((unsigned short)(cbMacHdLen << 10));

	// Notes:
	// Although spec says MMPDU can be fragmented; In most cases,
	// no one will send a MMPDU under fragmentation. With RTS may occur.
	pDevice->bAES = false;  //Set FRAGCTL_WEPTYP

	if (WLAN_GET_FC_ISWEP(pPacket->p80211Header->sA4.wFrameCtl) != 0) {
		if (pDevice->eEncryptionStatus == Ndis802_11Encryption1Enabled) {
			cbIVlen = 4;
			cbICVlen = 4;
			pTxBufHead->wFragCtl |= FRAGCTL_LEGACY;
		} else if (pDevice->eEncryptionStatus == Ndis802_11Encryption2Enabled) {
			cbIVlen = 8;//IV+ExtIV
			cbMIClen = 8;
			cbICVlen = 4;
			pTxBufHead->wFragCtl |= FRAGCTL_TKIP;
			//We need to get seed here for filling TxKey entry.
			//TKIPvMixKey(pTransmitKey->abyKey, pDevice->abyCurrentNetAddr,
			//            pTransmitKey->wTSC15_0, pTransmitKey->dwTSC47_16, pDevice->abyPRNG);
		} else if (pDevice->eEncryptionStatus == Ndis802_11Encryption3Enabled) {
			cbIVlen = 8;//RSN Header
			cbICVlen = 8;//MIC
			pTxBufHead->wFragCtl |= FRAGCTL_AES;
			pDevice->bAES = true;
		}
		//MAC Header should be padding 0 to DW alignment.
		uPadding = 4 - (cbMacHdLen%4);
		uPadding %= 4;
	}

	cbFrameSize = cbMacHdLen + cbFrameBodySize + cbIVlen + cbMIClen + cbICVlen + cbFCSlen;

	//Set FIFOCTL_GrpAckPolicy
	if (pDevice->bGrpAckPolicy == true) {//0000 0100 0000 0000
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
	} else { // 802.11a/b packet
		pvRrvTime = (PSRrvTime_ab) (pbyTxBufferAddr + wTxBufSize);
		pMICHDR = NULL;
		pvRTS = NULL;
		pCTS = NULL;
		pvTxDataHd = (PSTxDataHead_ab) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab));
		cbHeaderSize = wTxBufSize + sizeof(SRrvTime_ab) + sizeof(STxDataHead_ab);
	}

	memset((void *)(pbyTxBufferAddr + wTxBufSize), 0, (cbHeaderSize - wTxBufSize));

	memcpy(&(sEthHeader.abyDstAddr[0]), &(pPacket->p80211Header->sA3.abyAddr1[0]), ETH_ALEN);
	memcpy(&(sEthHeader.abySrcAddr[0]), &(pPacket->p80211Header->sA3.abyAddr2[0]), ETH_ALEN);
	//=========================
	//    No Fragmentation
	//=========================
	pTxBufHead->wFragCtl |= (unsigned short)FRAGCTL_NONFRAG;

	//Fill FIFO,RrvTime,RTS,and CTS
	s_vGenerateTxParameter(pDevice, byPktType, pbyTxBufferAddr, pvRrvTime, pvRTS, pCTS,
			       cbFrameSize, bNeedACK, TYPE_TXDMA0, &sEthHeader, wCurrentRate);

	//Fill DataHead
	uDuration = s_uFillDataHead(pDevice, byPktType, pvTxDataHd, cbFrameSize, TYPE_TXDMA0, bNeedACK,
				    0, 0, 1, AUTO_FB_NONE, wCurrentRate);

	pMACHeader = (PS802_11Header) (pbyTxBufferAddr + cbHeaderSize);

	cbReqCount = cbHeaderSize + cbMacHdLen + uPadding + cbIVlen + cbFrameBodySize;

	if (WLAN_GET_FC_ISWEP(pPacket->p80211Header->sA4.wFrameCtl) != 0) {
		unsigned char *pbyIVHead;
		unsigned char *pbyPayloadHead;
		unsigned char *pbyBSSID;
		PSKeyItem       pTransmitKey = NULL;

		pbyIVHead = (unsigned char *)(pbyTxBufferAddr + cbHeaderSize + cbMacHdLen + uPadding);
		pbyPayloadHead = (unsigned char *)(pbyTxBufferAddr + cbHeaderSize + cbMacHdLen + uPadding + cbIVlen);

		//Fill TXKEY
		//Kyle: Need fix: TKIP and AES did't encrypt Mnt Packet.
		//s_vFillTxKey(pDevice, (unsigned char *)pTxBufHead->adwTxKey, NULL);

		//Fill IV(ExtIV,RSNHDR)
		//s_vFillPrePayload(pDevice, pbyIVHead, NULL);
		//---------------------------
		// S/W or H/W Encryption
		//---------------------------
		do {
			if ((pDevice->eOPMode == OP_MODE_INFRASTRUCTURE) &&
			    (pDevice->bLinkPass == true)) {
				pbyBSSID = pDevice->abyBSSID;
				// get pairwise key
				if (KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, PAIRWISE_KEY, &pTransmitKey) == false) {
					// get group key
					if (KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, GROUP_KEY, &pTransmitKey) == true) {
						DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Get GTK.\n");
						break;
					}
				} else {
					DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Get PTK.\n");
					break;
				}
			}
			// get group key
			pbyBSSID = pDevice->abyBroadcastAddr;
			if (KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, GROUP_KEY, &pTransmitKey) == false) {
				pTransmitKey = NULL;
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "KEY is NULL. OP Mode[%d]\n", pDevice->eOPMode);
			} else {
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Get GTK.\n");
			}
		} while (false);
		//Fill TXKEY
		s_vFillTxKey(pDevice, (unsigned char *)(pTxBufHead->adwTxKey), pbyIVHead, pTransmitKey,
			     (unsigned char *)pMACHeader, (unsigned short)cbFrameBodySize, NULL);

		memcpy(pMACHeader, pPacket->p80211Header, cbMacHdLen);
		memcpy(pbyPayloadHead, ((unsigned char *)(pPacket->p80211Header) + cbMacHdLen),
		       cbFrameBodySize);
	} else {
		// Copy the Packet into a tx Buffer
		memcpy(pMACHeader, pPacket->p80211Header, pPacket->cbMPDULen);
	}

	pMACHeader->wSeqCtl = cpu_to_le16(pDevice->wSeqCounter << 4);
	pDevice->wSeqCounter++;
	if (pDevice->wSeqCounter > 0x0fff)
		pDevice->wSeqCounter = 0;

	if (bIsPSPOLL) {
		// The MAC will automatically replace the Duration-field of MAC header by Duration-field
		// of  FIFO control header.
		// This will cause AID-field of PS-POLL packet to be incorrect (Because PS-POLL's AID field is
		// in the same place of other packet's Duration-field).
		// And it will cause Cisco-AP to issue Disassociation-packet
		if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
			((PSTxDataHead_g)pvTxDataHd)->wDuration_a = cpu_to_le16(pPacket->p80211Header->sA2.wDurationID);
			((PSTxDataHead_g)pvTxDataHd)->wDuration_b = cpu_to_le16(pPacket->p80211Header->sA2.wDurationID);
		} else {
			((PSTxDataHead_ab)pvTxDataHd)->wDuration = cpu_to_le16(pPacket->p80211Header->sA2.wDurationID);
		}
	}

	// first TD is the only TD
	//Set TSR1 & ReqCount in TxDescHead
	pFrstTD->m_td1TD1.byTCR = (TCR_STP | TCR_EDP | EDMSDU);
	pFrstTD->pTDInfo->skb_dma = pFrstTD->pTDInfo->buf_dma;
	pFrstTD->m_td1TD1.wReqCount = cpu_to_le16((unsigned short)(cbReqCount));
	pFrstTD->buff_addr = cpu_to_le32(pFrstTD->pTDInfo->skb_dma);
	pFrstTD->pTDInfo->byFlags = 0;

	if (MACbIsRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_PS)) {
		// Disable PS
		MACbPSWakeup(pDevice->PortOffset);
	}
	pDevice->bPWBitOn = false;

	wmb();
	pFrstTD->m_td0TD0.f1Owner = OWNED_BY_NIC;
	wmb();

	pDevice->iTDUsed[TYPE_TXDMA0]++;

	if (AVAIL_TD(pDevice, TYPE_TXDMA0) <= 1) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " available td0 <= 1\n");
	}

	pDevice->apCurrTD[TYPE_TXDMA0] = pFrstTD->next;

#ifdef TxInSleep
	pDevice->nTxDataTimeCout = 0; //2008-8-21 chester <add> for send null packet
#endif

	// Poll Transmit the adapter
	MACvTransmit0(pDevice->PortOffset);

	return CMD_STATUS_PENDING;
}

CMD_STATUS csBeacon_xmit(PSDevice pDevice, PSTxMgmtPacket pPacket) {
	unsigned char byPktType;
	unsigned char *pbyBuffer = (unsigned char *)pDevice->tx_beacon_bufs;
	unsigned int cbFrameSize = pPacket->cbMPDULen + WLAN_FCS_LEN;
	unsigned int cbHeaderSize = 0;
	unsigned short wTxBufSize = sizeof(STxShortBufHead);
	PSTxShortBufHead pTxBufHead = (PSTxShortBufHead) pbyBuffer;
	PSTxDataHead_ab  pTxDataHead = (PSTxDataHead_ab) (pbyBuffer + wTxBufSize);
	PS802_11Header   pMACHeader;
	unsigned short wCurrentRate;
	unsigned short wLen = 0x0000;

	memset(pTxBufHead, 0, wTxBufSize);

	if (pDevice->eCurrentPHYType == PHY_TYPE_11A) {
		wCurrentRate = RATE_6M;
		byPktType = PK_TYPE_11A;
	} else {
		wCurrentRate = RATE_2M;
		byPktType = PK_TYPE_11B;
	}

	//Set Preamble type always long
	pDevice->byPreambleType = PREAMBLE_LONG;

	//Set FIFOCTL_GENINT

	pTxBufHead->wFIFOCtl |= FIFOCTL_GENINT;

	//Set packet type & Get Duration
	if (byPktType == PK_TYPE_11A) {//0000 0000 0000 0000
		pTxDataHead->wDuration = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameSize, byPktType,
											wCurrentRate, false, 0, 0, 1, AUTO_FB_NONE));
	} else if (byPktType == PK_TYPE_11B) {//0000 0001 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11B;
		pTxDataHead->wDuration = cpu_to_le16((unsigned short)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameSize, byPktType,
											wCurrentRate, false, 0, 0, 1, AUTO_FB_NONE));
	}

	BBvCalculateParameter(pDevice, cbFrameSize, wCurrentRate, byPktType,
			      (unsigned short *)&(wLen), (unsigned char *)&(pTxDataHead->byServiceField), (unsigned char *)&(pTxDataHead->bySignalField)
);
	pTxDataHead->wTransmitLength = cpu_to_le16(wLen);
	//Get TimeStampOff
	pTxDataHead->wTimeStampOff = cpu_to_le16(wTimeStampOff[pDevice->byPreambleType%2][wCurrentRate%MAX_RATE]);
	cbHeaderSize = wTxBufSize + sizeof(STxDataHead_ab);

	//Generate Beacon Header
	pMACHeader = (PS802_11Header)(pbyBuffer + cbHeaderSize);
	memcpy(pMACHeader, pPacket->p80211Header, pPacket->cbMPDULen);

	pMACHeader->wDurationID = 0;
	pMACHeader->wSeqCtl = cpu_to_le16(pDevice->wSeqCounter << 4);
	pDevice->wSeqCounter++;
	if (pDevice->wSeqCounter > 0x0fff)
		pDevice->wSeqCounter = 0;

	// Set Beacon buffer length
	pDevice->wBCNBufLen = pPacket->cbMPDULen + cbHeaderSize;

	MACvSetCurrBCNTxDescAddr(pDevice->PortOffset, (pDevice->tx_beacon_dma));

	MACvSetCurrBCNLength(pDevice->PortOffset, pDevice->wBCNBufLen);
	// Set auto Transmit on
	MACvRegBitsOn(pDevice->PortOffset, MAC_REG_TCR, TCR_AUTOBCNTX);
	// Poll Transmit the adapter
	MACvTransmitBCN(pDevice->PortOffset);

	return CMD_STATUS_PENDING;
}

unsigned int
cbGetFragCount(
	PSDevice         pDevice,
	PSKeyItem        pTransmitKey,
	unsigned int cbFrameBodySize,
	PSEthernetHeader psEthHeader
)
{
	unsigned int cbMACHdLen;
	unsigned int cbFrameSize;
	unsigned int cbFragmentSize; //Hdr+(IV)+payoad+(MIC)+(ICV)+FCS
	unsigned int cbFragPayloadSize;
	unsigned int cbLastFragPayloadSize;
	unsigned int cbIVlen = 0;
	unsigned int cbICVlen = 0;
	unsigned int cbMIClen = 0;
	unsigned int cbFCSlen = 4;
	unsigned int uMACfragNum = 1;
	bool bNeedACK;

	if ((pDevice->eOPMode == OP_MODE_ADHOC) ||
	    (pDevice->eOPMode == OP_MODE_AP)) {
		if (is_multicast_ether_addr(&(psEthHeader->abyDstAddr[0])))
			bNeedACK = false;
		else
			bNeedACK = true;
	} else {
		// MSDUs in Infra mode always need ACK
		bNeedACK = true;
	}

	if (pDevice->bLongHeader)
		cbMACHdLen = WLAN_HDR_ADDR3_LEN + 6;
	else
		cbMACHdLen = WLAN_HDR_ADDR3_LEN;

	if (pDevice->bEncryptionEnable == true) {
		if (pTransmitKey == NULL) {
			if ((pDevice->eEncryptionStatus == Ndis802_11Encryption1Enabled) ||
			    (pDevice->pMgmt->eAuthenMode < WMAC_AUTH_WPA)) {
				cbIVlen = 4;
				cbICVlen = 4;
			} else if (pDevice->eEncryptionStatus == Ndis802_11Encryption2Enabled) {
				cbIVlen = 8;//IV+ExtIV
				cbMIClen = 8;
				cbICVlen = 4;
			} else if (pDevice->eEncryptionStatus == Ndis802_11Encryption3Enabled) {
				cbIVlen = 8;//RSN Header
				cbICVlen = 8;//MIC
			}
		} else if (pTransmitKey->byCipherSuite == KEY_CTL_WEP) {
			cbIVlen = 4;
			cbICVlen = 4;
		} else if (pTransmitKey->byCipherSuite == KEY_CTL_TKIP) {
			cbIVlen = 8;//IV+ExtIV
			cbMIClen = 8;
			cbICVlen = 4;
		} else if (pTransmitKey->byCipherSuite == KEY_CTL_CCMP) {
			cbIVlen = 8;//RSN Header
			cbICVlen = 8;//MIC
		}
	}

	cbFrameSize = cbMACHdLen + cbIVlen + (cbFrameBodySize + cbMIClen) + cbICVlen + cbFCSlen;

	if ((cbFrameSize > pDevice->wFragmentationThreshold) && (bNeedACK == true)) {
		// Fragmentation
		cbFragmentSize = pDevice->wFragmentationThreshold;
		cbFragPayloadSize = cbFragmentSize - cbMACHdLen - cbIVlen - cbICVlen - cbFCSlen;
		uMACfragNum = (unsigned short) ((cbFrameBodySize + cbMIClen) / cbFragPayloadSize);
		cbLastFragPayloadSize = (cbFrameBodySize + cbMIClen) % cbFragPayloadSize;
		if (cbLastFragPayloadSize == 0) {
			cbLastFragPayloadSize = cbFragPayloadSize;
		} else {
			uMACfragNum++;
		}
	}
	return uMACfragNum;
}

void
vDMA0_tx_80211(PSDevice  pDevice, struct sk_buff *skb, unsigned char *pbMPDU, unsigned int cbMPDULen) {
	PSTxDesc        pFrstTD;
	unsigned char byPktType;
	unsigned char *pbyTxBufferAddr;
	void *pvRTS;
	void *pvCTS;
	void *pvTxDataHd;
	unsigned int uDuration;
	unsigned int cbReqCount;
	PS802_11Header  pMACHeader;
	unsigned int cbHeaderSize;
	unsigned int cbFrameBodySize;
	bool bNeedACK;
	bool bIsPSPOLL = false;
	PSTxBufHead     pTxBufHead;
	unsigned int cbFrameSize;
	unsigned int cbIVlen = 0;
	unsigned int cbICVlen = 0;
	unsigned int cbMIClen = 0;
	unsigned int cbFCSlen = 4;
	unsigned int uPadding = 0;
	unsigned int cbMICHDR = 0;
	unsigned int uLength = 0;
	unsigned long dwMICKey0, dwMICKey1;
	unsigned long dwMIC_Priority;
	unsigned long *pdwMIC_L;
	unsigned long *pdwMIC_R;
	unsigned short wTxBufSize;
	unsigned int cbMacHdLen;
	SEthernetHeader sEthHeader;
	void *pvRrvTime;
	void *pMICHDR;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	unsigned short wCurrentRate = RATE_1M;
	PUWLAN_80211HDR  p80211Header;
	unsigned int uNodeIndex = 0;
	bool bNodeExist = false;
	SKeyItem        STempKey;
	PSKeyItem       pTransmitKey = NULL;
	unsigned char *pbyIVHead;
	unsigned char *pbyPayloadHead;
	unsigned char *pbyMacHdr;

	unsigned int cbExtSuppRate = 0;
//    PWLAN_IE        pItem;

	pvRrvTime = pMICHDR = pvRTS = pvCTS = pvTxDataHd = NULL;

	if (cbMPDULen <= WLAN_HDR_ADDR3_LEN) {
		cbFrameBodySize = 0;
	} else {
		cbFrameBodySize = cbMPDULen - WLAN_HDR_ADDR3_LEN;
	}
	p80211Header = (PUWLAN_80211HDR)pbMPDU;

	pFrstTD = pDevice->apCurrTD[TYPE_TXDMA0];
	pbyTxBufferAddr = (unsigned char *)pFrstTD->pTDInfo->buf;
	pTxBufHead = (PSTxBufHead) pbyTxBufferAddr;
	wTxBufSize = sizeof(STxBufHead);
	memset(pTxBufHead, 0, wTxBufSize);

	if (pDevice->eCurrentPHYType == PHY_TYPE_11A) {
		wCurrentRate = RATE_6M;
		byPktType = PK_TYPE_11A;
	} else {
		wCurrentRate = RATE_1M;
		byPktType = PK_TYPE_11B;
	}

	// SetPower will cause error power TX state for OFDM Date packet in TX buffer.
	// 2004.11.11 Kyle -- Using OFDM power to tx MngPkt will decrease the connection capability.
	//                    And cmd timer will wait data pkt TX to finish before scanning so it's OK
	//                    to set power here.
	if (pDevice->pMgmt->eScanState != WMAC_NO_SCANNING) {
		RFbSetPower(pDevice, wCurrentRate, pDevice->byCurrentCh);
	} else {
		RFbSetPower(pDevice, wCurrentRate, pMgmt->uCurrChannel);
	}
	pTxBufHead->byTxPower = pDevice->byCurPwr;

	//+++++++++++++++++++++ Patch VT3253 A1 performance +++++++++++++++++++++++++++
	if (pDevice->byFOETuning) {
		if ((p80211Header->sA3.wFrameCtl & TYPE_DATE_NULL) == TYPE_DATE_NULL) {
			wCurrentRate = RATE_24M;
			byPktType = PK_TYPE_11GA;
		}
	}

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "vDMA0_tx_80211: p80211Header->sA3.wFrameCtl = %x \n", p80211Header->sA3.wFrameCtl);

	//Set packet type
	if (byPktType == PK_TYPE_11A) {//0000 0000 0000 0000
		pTxBufHead->wFIFOCtl = 0;
	} else if (byPktType == PK_TYPE_11B) {//0000 0001 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11B;
	} else if (byPktType == PK_TYPE_11GB) {//0000 0010 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11GB;
	} else if (byPktType == PK_TYPE_11GA) {//0000 0011 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11GA;
	}

	pTxBufHead->wFIFOCtl |= FIFOCTL_TMOEN;
	pTxBufHead->wTimeStamp = cpu_to_le16(DEFAULT_MGN_LIFETIME_RES_64us);

	if (is_multicast_ether_addr(&(p80211Header->sA3.abyAddr1[0]))) {
		bNeedACK = false;
		if (pDevice->bEnableHostWEP) {
			uNodeIndex = 0;
			bNodeExist = true;
		}
	} else {
		if (pDevice->bEnableHostWEP) {
			if (BSSDBbIsSTAInNodeDB(pDevice->pMgmt, (unsigned char *)(p80211Header->sA3.abyAddr1), &uNodeIndex))
				bNodeExist = true;
		}
		bNeedACK = true;
		pTxBufHead->wFIFOCtl |= FIFOCTL_NEEDACK;
	};

	if ((pMgmt->eCurrMode == WMAC_MODE_ESS_AP) ||
	    (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA)) {
		pTxBufHead->wFIFOCtl |= FIFOCTL_LRETRY;
	}

	pTxBufHead->wFIFOCtl |= (FIFOCTL_GENINT | FIFOCTL_ISDMA0);

	if ((p80211Header->sA4.wFrameCtl & TYPE_SUBTYPE_MASK) == TYPE_CTL_PSPOLL) {
		bIsPSPOLL = true;
		cbMacHdLen = WLAN_HDR_ADDR2_LEN;
	} else {
		cbMacHdLen = WLAN_HDR_ADDR3_LEN;
	}

	// hostapd deamon ext support rate patch
	if (WLAN_GET_FC_FSTYPE(p80211Header->sA4.wFrameCtl) == WLAN_FSTYPE_ASSOCRESP) {
		if (((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates)->len != 0) {
			cbExtSuppRate += ((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates)->len + WLAN_IEHDR_LEN;
		}

		if (((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates)->len != 0) {
			cbExtSuppRate += ((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates)->len + WLAN_IEHDR_LEN;
		}

		if (cbExtSuppRate > 0) {
			cbFrameBodySize = WLAN_ASSOCRESP_OFF_SUPP_RATES;
		}
	}

	//Set FRAGCTL_MACHDCNT
	pTxBufHead->wFragCtl |= cpu_to_le16((unsigned short)cbMacHdLen << 10);

	// Notes:
	// Although spec says MMPDU can be fragmented; In most cases,
	// no one will send a MMPDU under fragmentation. With RTS may occur.
	pDevice->bAES = false;  //Set FRAGCTL_WEPTYP

	if (WLAN_GET_FC_ISWEP(p80211Header->sA4.wFrameCtl) != 0) {
		if (pDevice->eEncryptionStatus == Ndis802_11Encryption1Enabled) {
			cbIVlen = 4;
			cbICVlen = 4;
			pTxBufHead->wFragCtl |= FRAGCTL_LEGACY;
		} else if (pDevice->eEncryptionStatus == Ndis802_11Encryption2Enabled) {
			cbIVlen = 8;//IV+ExtIV
			cbMIClen = 8;
			cbICVlen = 4;
			pTxBufHead->wFragCtl |= FRAGCTL_TKIP;
			//We need to get seed here for filling TxKey entry.
			//TKIPvMixKey(pTransmitKey->abyKey, pDevice->abyCurrentNetAddr,
			//            pTransmitKey->wTSC15_0, pTransmitKey->dwTSC47_16, pDevice->abyPRNG);
		} else if (pDevice->eEncryptionStatus == Ndis802_11Encryption3Enabled) {
			cbIVlen = 8;//RSN Header
			cbICVlen = 8;//MIC
			cbMICHDR = sizeof(SMICHDRHead);
			pTxBufHead->wFragCtl |= FRAGCTL_AES;
			pDevice->bAES = true;
		}
		//MAC Header should be padding 0 to DW alignment.
		uPadding = 4 - (cbMacHdLen%4);
		uPadding %= 4;
	}

	cbFrameSize = cbMacHdLen + cbFrameBodySize + cbIVlen + cbMIClen + cbICVlen + cbFCSlen + cbExtSuppRate;

	//Set FIFOCTL_GrpAckPolicy
	if (pDevice->bGrpAckPolicy == true) {//0000 0100 0000 0000
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

	} else {//802.11a/b packet

		pvRrvTime = (PSRrvTime_ab) (pbyTxBufferAddr + wTxBufSize);
		pMICHDR = (PSMICHDRHead) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab));
		pvRTS = NULL;
		pvCTS = NULL;
		pvTxDataHd = (PSTxDataHead_ab) (pbyTxBufferAddr + wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR);
		cbHeaderSize = wTxBufSize + sizeof(SRrvTime_ab) + cbMICHDR + sizeof(STxDataHead_ab);

	}

	memset((void *)(pbyTxBufferAddr + wTxBufSize), 0, (cbHeaderSize - wTxBufSize));
	memcpy(&(sEthHeader.abyDstAddr[0]), &(p80211Header->sA3.abyAddr1[0]), ETH_ALEN);
	memcpy(&(sEthHeader.abySrcAddr[0]), &(p80211Header->sA3.abyAddr2[0]), ETH_ALEN);
	//=========================
	//    No Fragmentation
	//=========================
	pTxBufHead->wFragCtl |= (unsigned short)FRAGCTL_NONFRAG;

	//Fill FIFO,RrvTime,RTS,and CTS
	s_vGenerateTxParameter(pDevice, byPktType, pbyTxBufferAddr, pvRrvTime, pvRTS, pvCTS,
			       cbFrameSize, bNeedACK, TYPE_TXDMA0, &sEthHeader, wCurrentRate);

	//Fill DataHead
	uDuration = s_uFillDataHead(pDevice, byPktType, pvTxDataHd, cbFrameSize, TYPE_TXDMA0, bNeedACK,
				    0, 0, 1, AUTO_FB_NONE, wCurrentRate);

	pMACHeader = (PS802_11Header) (pbyTxBufferAddr + cbHeaderSize);

	cbReqCount = cbHeaderSize + cbMacHdLen + uPadding + cbIVlen + (cbFrameBodySize + cbMIClen) + cbExtSuppRate;

	pbyMacHdr = (unsigned char *)(pbyTxBufferAddr + cbHeaderSize);
	pbyPayloadHead = (unsigned char *)(pbyMacHdr + cbMacHdLen + uPadding + cbIVlen);
	pbyIVHead = (unsigned char *)(pbyMacHdr + cbMacHdLen + uPadding);

	// Copy the Packet into a tx Buffer
	memcpy(pbyMacHdr, pbMPDU, cbMacHdLen);

	// version set to 0, patch for hostapd deamon
	pMACHeader->wFrameCtl &= cpu_to_le16(0xfffc);
	memcpy(pbyPayloadHead, (pbMPDU + cbMacHdLen), cbFrameBodySize);

	// replace support rate, patch for hostapd deamon(only support 11M)
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
			dwMICKey0 = *(unsigned long *)(&pTransmitKey->abyKey[16]);
			dwMICKey1 = *(unsigned long *)(&pTransmitKey->abyKey[20]);

			// DO Software Michael
			MIC_vInit(dwMICKey0, dwMICKey1);
			MIC_vAppend((unsigned char *)&(sEthHeader.abyDstAddr[0]), 12);
			dwMIC_Priority = 0;
			MIC_vAppend((unsigned char *)&dwMIC_Priority, 4);
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "DMA0_tx_8021:MIC KEY: %lX, %lX\n", dwMICKey0, dwMICKey1);

			uLength = cbHeaderSize + cbMacHdLen + uPadding + cbIVlen;

			MIC_vAppend((pbyTxBufferAddr + uLength), cbFrameBodySize);

			pdwMIC_L = (unsigned long *)(pbyTxBufferAddr + uLength + cbFrameBodySize);
			pdwMIC_R = (unsigned long *)(pbyTxBufferAddr + uLength + cbFrameBodySize + 4);

			MIC_vGetMIC(pdwMIC_L, pdwMIC_R);
			MIC_vUnInit();

			if (pDevice->bTxMICFail == true) {
				*pdwMIC_L = 0;
				*pdwMIC_R = 0;
				pDevice->bTxMICFail = false;
			}

			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "uLength: %d, %d\n", uLength, cbFrameBodySize);
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "cbReqCount:%d, %d, %d, %d\n", cbReqCount, cbHeaderSize, uPadding, cbIVlen);
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "MIC:%lx, %lx\n", *pdwMIC_L, *pdwMIC_R);

		}

		s_vFillTxKey(pDevice, (unsigned char *)(pTxBufHead->adwTxKey), pbyIVHead, pTransmitKey,
			     pbyMacHdr, (unsigned short)cbFrameBodySize, (unsigned char *)pMICHDR);

		if (pDevice->bEnableHostWEP) {
			pMgmt->sNodeDBTable[uNodeIndex].dwTSC47_16 = pTransmitKey->dwTSC47_16;
			pMgmt->sNodeDBTable[uNodeIndex].wTSC15_0 = pTransmitKey->wTSC15_0;
		}

		if ((pDevice->byLocalID <= REV_ID_VT3253_A1)) {
			s_vSWencryption(pDevice, pTransmitKey, pbyPayloadHead, (unsigned short)(cbFrameBodySize + cbMIClen));
		}
	}

	pMACHeader->wSeqCtl = cpu_to_le16(pDevice->wSeqCounter << 4);
	pDevice->wSeqCounter++;
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

	// first TD is the only TD
	//Set TSR1 & ReqCount in TxDescHead
	pFrstTD->pTDInfo->skb = skb;
	pFrstTD->m_td1TD1.byTCR = (TCR_STP | TCR_EDP | EDMSDU);
	pFrstTD->pTDInfo->skb_dma = pFrstTD->pTDInfo->buf_dma;
	pFrstTD->m_td1TD1.wReqCount = cpu_to_le16(cbReqCount);
	pFrstTD->buff_addr = cpu_to_le32(pFrstTD->pTDInfo->skb_dma);
	pFrstTD->pTDInfo->byFlags = 0;
	pFrstTD->pTDInfo->byFlags |= TD_FLAGS_PRIV_SKB;

	if (MACbIsRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_PS)) {
		// Disable PS
		MACbPSWakeup(pDevice->PortOffset);
	}
	pDevice->bPWBitOn = false;

	wmb();
	pFrstTD->m_td0TD0.f1Owner = OWNED_BY_NIC;
	wmb();

	pDevice->iTDUsed[TYPE_TXDMA0]++;

	if (AVAIL_TD(pDevice, TYPE_TXDMA0) <= 1) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " available td0 <= 1\n");
	}

	pDevice->apCurrTD[TYPE_TXDMA0] = pFrstTD->next;

	// Poll Transmit the adapter
	MACvTransmit0(pDevice->PortOffset);

	return;
}
