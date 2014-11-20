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

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Static Definitions -------------------------*/
#define CRITICAL_PACKET_LEN      256    // if packet size < 256 -> in-direct send
                                        //    packet size >= 256 -> direct send

static const unsigned short wTimeStampOff[2][MAX_RATE] = {
	{384, 288, 226, 209, 54, 43, 37, 31, 28, 25, 24, 23}, // Long Preamble
	{384, 192, 130, 113, 54, 43, 37, 31, 28, 25, 24, 23}, // Short Preamble
};

static const unsigned short wFB_Opt0[2][5] = {
	{RATE_12M, RATE_18M, RATE_24M, RATE_36M, RATE_48M}, // fallback_rate0
	{RATE_12M, RATE_12M, RATE_18M, RATE_24M, RATE_36M}, // fallback_rate1
};
static const unsigned short wFB_Opt1[2][5] = {
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
	struct vnt_private *pDevice,
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
	struct vnt_private *pDevice,
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
	struct vnt_private *pDevice,
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
	struct vnt_private *pDevice,
	unsigned char *pbyBuffer,
	unsigned int	uTxType,
	void *pvtdCurr,
	unsigned short wFragType,
	unsigned int	cbReqCount
);

static unsigned int
s_cbFillTxBufHead(struct vnt_private *pDevice, unsigned char byPktType,
		  unsigned char *pbyTxBufferAddr, unsigned int cbFrameBodySize,
		  unsigned int uDMAIdx, PSTxDesc pHeadTD,
		  PSEthernetHeader psEthHeader, unsigned char *pPacket,
		  bool bNeedEncrypt, PSKeyItem pTransmitKey,
		  unsigned int uNodeIndex, unsigned int *puMACfragNum);

static
__le16
s_uFillDataHead(
	struct vnt_private *pDevice,
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
	struct vnt_private *pDevice,
	unsigned char *pbyBuf,
	unsigned char *pbyIVHead,
	PSKeyItem  pTransmitKey,
	unsigned char *pbyHdrBuf,
	unsigned short wPayloadLen,
	unsigned char *pMICHDR
)
{
	struct vnt_mic_hdr *mic_hdr = (struct vnt_mic_hdr *)pMICHDR;
	unsigned long *pdwIV = (unsigned long *)pbyIVHead;
	unsigned long *pdwExtIV = (unsigned long *)((unsigned char *)pbyIVHead+4);
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
		if (pDevice->dwIVCounter > WEP_IV_MASK)
			pDevice->dwIVCounter = 0;

	} else if (pTransmitKey->byCipherSuite == KEY_CTL_TKIP) {
		pTransmitKey->wTSC15_0++;
		if (pTransmitKey->wTSC15_0 == 0)
			pTransmitKey->dwTSC47_16++;

		TKIPvMixKey(pTransmitKey->abyKey, pDevice->abyCurrentNetAddr,
			    pTransmitKey->wTSC15_0, pTransmitKey->dwTSC47_16, pDevice->abyPRNG);
		memcpy(pbyBuf, pDevice->abyPRNG, 16);
		// Make IV
		memcpy(pdwIV, pDevice->abyPRNG, 3);

		*(pbyIVHead+3) = (unsigned char)(((byKeyIndex << 6) & 0xc0) | 0x20); // 0x20 is ExtIV
		// Append IV&ExtIV after Mac Header
		*pdwExtIV = cpu_to_le32(pTransmitKey->dwTSC47_16);
		pr_debug("vFillTxKey()---- pdwExtIV: %lx\n", *pdwExtIV);

	} else if (pTransmitKey->byCipherSuite == KEY_CTL_CCMP) {
		pTransmitKey->wTSC15_0++;
		if (pTransmitKey->wTSC15_0 == 0)
			pTransmitKey->dwTSC47_16++;

		memcpy(pbyBuf, pTransmitKey->abyKey, 16);

		// Make IV
		*pdwIV = 0;
		*(pbyIVHead+3) = (unsigned char)(((byKeyIndex << 6) & 0xc0) | 0x20); // 0x20 is ExtIV
		*pdwIV |= cpu_to_le16((unsigned short)(pTransmitKey->wTSC15_0));
		//Append IV&ExtIV after Mac Header
		*pdwExtIV = cpu_to_le32(pTransmitKey->dwTSC47_16);

		/* MICHDR0 */
		mic_hdr->id = 0x59;
		mic_hdr->tx_priority = 0;
		memcpy(mic_hdr->mic_addr2, pMACHeader->abyAddr2, ETH_ALEN);

		/* ccmp pn big endian order */
		mic_hdr->ccmp_pn[0] = (u8)(pTransmitKey->dwTSC47_16 >> 24);
		mic_hdr->ccmp_pn[1] = (u8)(pTransmitKey->dwTSC47_16 >> 16);
		mic_hdr->ccmp_pn[2] = (u8)(pTransmitKey->dwTSC47_16 >> 8);
		mic_hdr->ccmp_pn[3] = (u8)pTransmitKey->dwTSC47_16;
		mic_hdr->ccmp_pn[4] = (u8)(pTransmitKey->wTSC15_0 >> 8);
		mic_hdr->ccmp_pn[5] = (u8)pTransmitKey->wTSC15_0;

		/* MICHDR1 */
		mic_hdr->payload_len = cpu_to_be16(wPayloadLen);

		if (pDevice->bLongHeader)
			mic_hdr->hlen = cpu_to_be16(28);
		else
			mic_hdr->hlen = cpu_to_be16(22);

		memcpy(mic_hdr->addr1, pMACHeader->abyAddr1, ETH_ALEN);
		memcpy(mic_hdr->addr2, pMACHeader->abyAddr2, ETH_ALEN);

		/* MICHDR2 */
		memcpy(mic_hdr->addr3, pMACHeader->abyAddr3, ETH_ALEN);
		mic_hdr->frame_control =
				cpu_to_le16(pMACHeader->wFrameCtl & 0xc78f);
		mic_hdr->seq_ctrl = cpu_to_le16(pMACHeader->wSeqCtl & 0xf);

		if (pDevice->bLongHeader)
			memcpy(mic_hdr->addr4, pMACHeader->abyAddr4, ETH_ALEN);
	}
}

static
void
s_vSWencryption(
	struct vnt_private *pDevice,
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

static __le16 vnt_time_stamp_off(struct vnt_private *priv, u16 rate)
{
	return cpu_to_le16(wTimeStampOff[priv->byPreambleType % 2]
							[rate % MAX_RATE]);
}

/*byPktType : PK_TYPE_11A     0
  PK_TYPE_11B     1
  PK_TYPE_11GB    2
  PK_TYPE_11GA    3
*/
static
unsigned int
s_uGetTxRsvTime(
	struct vnt_private *pDevice,
	unsigned char byPktType,
	unsigned int cbFrameLength,
	unsigned short wRate,
	bool bNeedAck
)
{
	unsigned int uDataTime, uAckTime;

	uDataTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, cbFrameLength, wRate);
	if (byPktType == PK_TYPE_11B) //llb,CCK mode
		uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, (unsigned short)pDevice->byTopCCKBasicRate);
	else //11g 2.4G OFDM mode & 11a 5G OFDM mode
		uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, (unsigned short)pDevice->byTopOFDMBasicRate);

	if (bNeedAck)
		return uDataTime + pDevice->uSIFS + uAckTime;
	else
		return uDataTime;
}

static __le16 vnt_rxtx_rsvtime_le16(struct vnt_private *priv, u8 pkt_type,
				    u32 frame_length, u16 rate, bool need_ack)
{
	return cpu_to_le16((u16)s_uGetTxRsvTime(priv, pkt_type,
						frame_length, rate, need_ack));
}

//byFreqType: 0=>5GHZ 1=>2.4GHZ
static
__le16
s_uGetRTSCTSRsvTime(
	struct vnt_private *pDevice,
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
		return cpu_to_le16((u16)uRrvTime);
	}

	//RTSRrvTime
	uRrvTime = uRTSTime + uCTSTime + uAckTime + uDataTime + 3*pDevice->uSIFS;
	return cpu_to_le16((u16)uRrvTime);
}

//byFreqType 0: 5GHz, 1:2.4Ghz
static
unsigned int
s_uGetDataDuration(
	struct vnt_private *pDevice,
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

	if (uFragIdx == (uMACfragNum-1))
		bLastFrag = 1;

	switch (byDurType) {
	case DATADUR_B:    //DATADUR_B
		if (((uMACfragNum == 1)) || bLastFrag) {//Non Frag or Last Frag
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
				return pDevice->uSIFS + uAckTime;
			} else {
				return 0;
			}
		} else {//First Frag or Mid Frag
			if (uFragIdx == (uMACfragNum-2))
				uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wRate, bNeedAck);
			else
				uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);

			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
				return pDevice->uSIFS + uAckTime + uNextPktTime;
			} else {
				return pDevice->uSIFS + uNextPktTime;
			}
		}
		break;

	case DATADUR_A:    //DATADUR_A
		if (((uMACfragNum == 1)) || bLastFrag) {//Non Frag or Last Frag
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
				return pDevice->uSIFS + uAckTime;
			} else {
				return 0;
			}
		} else {//First Frag or Mid Frag
			if (uFragIdx == (uMACfragNum-2))
				uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wRate, bNeedAck);
			else
				uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);

			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
				return pDevice->uSIFS + uAckTime + uNextPktTime;
			} else {
				return pDevice->uSIFS + uNextPktTime;
			}
		}
		break;

	case DATADUR_A_F0:    //DATADUR_A_F0
		if (((uMACfragNum == 1)) || bLastFrag) {//Non Frag or Last Frag
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

				if (uFragIdx == (uMACfragNum-2))
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
				else
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);

			} else { // (byFBOption == AUTO_FB_1)
				if (wRate < RATE_18M)
					wRate = RATE_18M;
				else if (wRate > RATE_54M)
					wRate = RATE_54M;

				if (uFragIdx == (uMACfragNum-2))
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);
				else
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);

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
		if (((uMACfragNum == 1)) || bLastFrag) {//Non Frag or Last Frag
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

				if (uFragIdx == (uMACfragNum-2))
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
				else
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);

			} else { // (byFBOption == AUTO_FB_1)
				if (wRate < RATE_18M)
					wRate = RATE_18M;
				else if (wRate > RATE_54M)
					wRate = RATE_54M;

				if (uFragIdx == (uMACfragNum-2))
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);
				else
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);
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
__le16
s_uGetRTSCTSDuration(
	struct vnt_private *pDevice,
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
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2 * pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
		else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2 * pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);

		break;

	case RTSDUR_AA_F0: //RTSDuration_aa_f0
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
		else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);

		break;

	case RTSDUR_BA_F1: //RTSDuration_ba_f1
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
		else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);

		break;

	case RTSDUR_AA_F1: //RTSDuration_aa_f1
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
		else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);

		break;

	case CTSDUR_BA_F0: //CTSDuration_ba_f0
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
		else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);

		break;

	case CTSDUR_BA_F1: //CTSDuration_ba_f1
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
		else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);

		break;

	default:
		break;
	}

	return cpu_to_le16((u16)uDurTime);
}

static
__le16
s_uFillDataHead(
	struct vnt_private *pDevice,
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

	if (pTxDataHead == NULL)
		return 0;


	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
		if (byFBOption == AUTO_FB_NONE) {
			struct vnt_tx_datahead_g *buf = pTxDataHead;
			/* Get SignalField, ServiceField & Length */
			vnt_get_phy_field(pDevice, cbFrameLength, wCurrentRate,
					  byPktType, &buf->a);

			vnt_get_phy_field(pDevice, cbFrameLength,
					  pDevice->byTopCCKBasicRate,
					  PK_TYPE_11B, &buf->b);

			/* Get Duration and TimeStamp */
			buf->duration_a = cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength,
									      byPktType, wCurrentRate, bNeedAck, uFragIdx,
									      cbLastFragmentSize, uMACfragNum,
									      byFBOption));
			buf->duration_b = cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameLength,
									      PK_TYPE_11B, pDevice->byTopCCKBasicRate,
									      bNeedAck, uFragIdx, cbLastFragmentSize,
									      uMACfragNum, byFBOption));

			buf->time_stamp_off_a = vnt_time_stamp_off(pDevice, wCurrentRate);
			buf->time_stamp_off_b = vnt_time_stamp_off(pDevice, pDevice->byTopCCKBasicRate);

			return buf->duration_a;
		} else {
			/* Auto Fallback */
			struct vnt_tx_datahead_g_fb *buf = pTxDataHead;
			/* Get SignalField, ServiceField & Length */
			vnt_get_phy_field(pDevice, cbFrameLength, wCurrentRate,
					  byPktType, &buf->a);

			vnt_get_phy_field(pDevice, cbFrameLength,
					  pDevice->byTopCCKBasicRate,
					  PK_TYPE_11B, &buf->b);
			/* Get Duration and TimeStamp */
			buf->duration_a = cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength, byPktType,
									      wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption));
			buf->duration_b = cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameLength, PK_TYPE_11B,
									       pDevice->byTopCCKBasicRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption));
			buf->duration_a_f0 = cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_A_F0, cbFrameLength, byPktType,
										  wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption));
			buf->duration_a_f1 = cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_A_F1, cbFrameLength, byPktType,
										 wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption));

			buf->time_stamp_off_a = vnt_time_stamp_off(pDevice, wCurrentRate);
			buf->time_stamp_off_b = vnt_time_stamp_off(pDevice, pDevice->byTopCCKBasicRate);

			return buf->duration_a;
		} //if (byFBOption == AUTO_FB_NONE)
	} else if (byPktType == PK_TYPE_11A) {
		if ((byFBOption != AUTO_FB_NONE)) {
			/* Auto Fallback */
			struct vnt_tx_datahead_a_fb *buf = pTxDataHead;
			/* Get SignalField, ServiceField & Length */
			vnt_get_phy_field(pDevice, cbFrameLength, wCurrentRate,
					  byPktType, &buf->a);

			/* Get Duration and TimeStampOff */
			buf->duration = cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength, byPktType,
									    wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption));
			buf->duration_f0 = cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_A_F0, cbFrameLength, byPktType,
									       wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption));
			buf->duration_f1 = cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_A_F1, cbFrameLength, byPktType,
										wCurrentRate, bNeedAck, uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption));
			buf->time_stamp_off = vnt_time_stamp_off(pDevice, wCurrentRate);
			return buf->duration;
		} else {
			struct vnt_tx_datahead_ab *buf = pTxDataHead;
			/* Get SignalField, ServiceField & Length */
			vnt_get_phy_field(pDevice, cbFrameLength, wCurrentRate,
					  byPktType, &buf->ab);

			/* Get Duration and TimeStampOff */
			buf->duration = cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength, byPktType,
									    wCurrentRate, bNeedAck, uFragIdx,
									    cbLastFragmentSize, uMACfragNum,
									    byFBOption));

			buf->time_stamp_off = vnt_time_stamp_off(pDevice, wCurrentRate);
			return buf->duration;
		}
	} else {
		struct vnt_tx_datahead_ab *buf = pTxDataHead;
		/* Get SignalField, ServiceField & Length */
		vnt_get_phy_field(pDevice, cbFrameLength, wCurrentRate,
				  byPktType, &buf->ab);
		/* Get Duration and TimeStampOff */
		buf->duration = cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameLength, byPktType,
								    wCurrentRate, bNeedAck, uFragIdx,
								    cbLastFragmentSize, uMACfragNum,
								    byFBOption));
		buf->time_stamp_off = vnt_time_stamp_off(pDevice, wCurrentRate);
		return buf->duration;
	}
	return 0;
}

static
void
s_vFillRTSHead(
	struct vnt_private *pDevice,
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
			struct vnt_rts_g *buf = pvRTS;
			/* Get SignalField, ServiceField & Length */
			vnt_get_phy_field(pDevice, uRTSFrameLen,
					  pDevice->byTopCCKBasicRate,
					  PK_TYPE_11B, &buf->b);

			vnt_get_phy_field(pDevice, uRTSFrameLen,
					  pDevice->byTopOFDMBasicRate,
					  byPktType, &buf->a);
			/* Get Duration */
			buf->duration_bb =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_BB,
						     cbFrameLength, PK_TYPE_11B,
						     pDevice->byTopCCKBasicRate,
						     bNeedAck, byFBOption);
			buf->duration_aa =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_AA,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);
			buf->duration_ba =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_BA,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);

			buf->data.duration = buf->duration_aa;
			/* Get RTS Frame body */
			buf->data.frame_control =
					cpu_to_le16(IEEE80211_FTYPE_CTL |
						    IEEE80211_STYPE_RTS);


			if ((pDevice->op_mode == NL80211_IFTYPE_ADHOC) ||
			    (pDevice->op_mode == NL80211_IFTYPE_AP)) {
				memcpy(&buf->data.ra, psEthHeader->abyDstAddr, ETH_ALEN);
			} else {
				memcpy(&buf->data.ra, pDevice->abyBSSID, ETH_ALEN);
			}
			if (pDevice->op_mode == NL80211_IFTYPE_AP)
				memcpy(&buf->data.ta, pDevice->abyBSSID, ETH_ALEN);
			else
				memcpy(&buf->data.ta, psEthHeader->abySrcAddr, ETH_ALEN);

		} else {
			struct vnt_rts_g_fb *buf = pvRTS;
			/* Get SignalField, ServiceField & Length */
			vnt_get_phy_field(pDevice, uRTSFrameLen,
					  pDevice->byTopCCKBasicRate,
					  PK_TYPE_11B, &buf->b);

			vnt_get_phy_field(pDevice, uRTSFrameLen,
					  pDevice->byTopOFDMBasicRate,
					  byPktType, &buf->a);
			/* Get Duration */
			buf->duration_bb =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_BB,
						     cbFrameLength, PK_TYPE_11B,
						     pDevice->byTopCCKBasicRate,
						     bNeedAck, byFBOption);
			buf->duration_aa =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_AA,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);
			buf->duration_ba =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_BA,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);
			buf->rts_duration_ba_f0 =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_BA_F0,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);
			buf->rts_duration_aa_f0 =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_AA_F0,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);
			buf->rts_duration_ba_f1 =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_BA_F1,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);
			buf->rts_duration_aa_f1 =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_AA_F1,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);
			buf->data.duration = buf->duration_aa;
			/* Get RTS Frame body */
			buf->data.frame_control =
					cpu_to_le16(IEEE80211_FTYPE_CTL |
						    IEEE80211_STYPE_RTS);


			if ((pDevice->op_mode == NL80211_IFTYPE_ADHOC) ||
			    (pDevice->op_mode == NL80211_IFTYPE_AP)) {
				memcpy(&buf->data.ra, psEthHeader->abyDstAddr, ETH_ALEN);
			} else {
				memcpy(&buf->data.ra, pDevice->abyBSSID, ETH_ALEN);
			}

			if (pDevice->op_mode == NL80211_IFTYPE_AP)
				memcpy(&buf->data.ta, pDevice->abyBSSID, ETH_ALEN);
			else
				memcpy(&buf->data.ta, psEthHeader->abySrcAddr, ETH_ALEN);

		} // if (byFBOption == AUTO_FB_NONE)
	} else if (byPktType == PK_TYPE_11A) {
		if (byFBOption == AUTO_FB_NONE) {
			struct vnt_rts_ab *buf = pvRTS;
			/* Get SignalField, ServiceField & Length */
			vnt_get_phy_field(pDevice, uRTSFrameLen,
					  pDevice->byTopOFDMBasicRate,
					  byPktType, &buf->ab);
			/* Get Duration */
			buf->duration =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_AA,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);
			buf->data.duration = buf->duration;
			/* Get RTS Frame body */
			buf->data.frame_control =
					cpu_to_le16(IEEE80211_FTYPE_CTL |
						    IEEE80211_STYPE_RTS);


			if ((pDevice->op_mode == NL80211_IFTYPE_ADHOC) ||
			    (pDevice->op_mode == NL80211_IFTYPE_AP)) {
				memcpy(&buf->data.ra, psEthHeader->abyDstAddr, ETH_ALEN);
			} else {
				memcpy(&buf->data.ra, pDevice->abyBSSID, ETH_ALEN);
			}

			if (pDevice->op_mode == NL80211_IFTYPE_AP)
				memcpy(&buf->data.ta, pDevice->abyBSSID, ETH_ALEN);
			else
				memcpy(&buf->data.ta, psEthHeader->abySrcAddr, ETH_ALEN);

		} else {
			struct vnt_rts_a_fb *buf = pvRTS;
			/* Get SignalField, ServiceField & Length */
			vnt_get_phy_field(pDevice, uRTSFrameLen,
					  pDevice->byTopOFDMBasicRate,
					  byPktType, &buf->a);
			/* Get Duration */
			buf->duration =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_AA,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);
			buf->rts_duration_f0 =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_AA_F0,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);
			buf->rts_duration_f1 =
				s_uGetRTSCTSDuration(pDevice, RTSDUR_AA_F1,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);
			buf->data.duration = buf->duration;
			/* Get RTS Frame body */
			buf->data.frame_control =
					cpu_to_le16(IEEE80211_FTYPE_CTL |
						    IEEE80211_STYPE_RTS);

			if ((pDevice->op_mode == NL80211_IFTYPE_ADHOC) ||
			    (pDevice->op_mode == NL80211_IFTYPE_AP)) {
				memcpy(&buf->data.ra, psEthHeader->abyDstAddr, ETH_ALEN);
			} else {
				memcpy(&buf->data.ra, pDevice->abyBSSID, ETH_ALEN);
			}
			if (pDevice->op_mode == NL80211_IFTYPE_AP)
				memcpy(&buf->data.ta, pDevice->abyBSSID, ETH_ALEN);
			else
				memcpy(&buf->data.ta, psEthHeader->abySrcAddr, ETH_ALEN);
		}
	} else if (byPktType == PK_TYPE_11B) {
		struct vnt_rts_ab *buf = pvRTS;
		/* Get SignalField, ServiceField & Length */
		vnt_get_phy_field(pDevice, uRTSFrameLen,
				  pDevice->byTopCCKBasicRate,
				  PK_TYPE_11B, &buf->ab);
		/* Get Duration */
		buf->duration =
			s_uGetRTSCTSDuration(pDevice, RTSDUR_BB, cbFrameLength,
					     byPktType, wCurrentRate, bNeedAck,
					     byFBOption);

		buf->data.duration = buf->duration;
		/* Get RTS Frame body */
		buf->data.frame_control =
			cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS);

		if ((pDevice->op_mode == NL80211_IFTYPE_ADHOC) ||
		    (pDevice->op_mode == NL80211_IFTYPE_AP)) {
			memcpy(&buf->data.ra, psEthHeader->abyDstAddr, ETH_ALEN);
		} else {
			memcpy(&buf->data.ra, pDevice->abyBSSID, ETH_ALEN);
		}

		if (pDevice->op_mode == NL80211_IFTYPE_AP)
			memcpy(&buf->data.ta, pDevice->abyBSSID, ETH_ALEN);
		else
			memcpy(&buf->data.ta, psEthHeader->abySrcAddr, ETH_ALEN);
	}
}

static
void
s_vFillCTSHead(
	struct vnt_private *pDevice,
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

	if (pvCTS == NULL)
		return;

	if (bDisCRC) {
		// When CRCDIS bit is on, H/W forgot to generate FCS for CTS frame,
		// in this case we need to decrease its length by 4.
		uCTSFrameLen -= 4;
	}

	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
		if (byFBOption != AUTO_FB_NONE && uDMAIdx != TYPE_ATIMDMA && uDMAIdx != TYPE_BEACONDMA) {
			// Auto Fall back
			struct vnt_cts_fb *buf = pvCTS;
			/* Get SignalField, ServiceField & Length */
			vnt_get_phy_field(pDevice, uCTSFrameLen,
					  pDevice->byTopCCKBasicRate,
					  PK_TYPE_11B, &buf->b);

			buf->duration_ba =
				s_uGetRTSCTSDuration(pDevice, CTSDUR_BA,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);

			/* Get CTSDuration_ba_f0 */
			buf->cts_duration_ba_f0 =
				s_uGetRTSCTSDuration(pDevice, CTSDUR_BA_F0,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);

			/* Get CTSDuration_ba_f1 */
			buf->cts_duration_ba_f1 =
				s_uGetRTSCTSDuration(pDevice, CTSDUR_BA_F1,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);

			/* Get CTS Frame body */
			buf->data.duration = buf->duration_ba;

			buf->data.frame_control =
				cpu_to_le16(IEEE80211_FTYPE_CTL |
					    IEEE80211_STYPE_CTS);

			buf->reserved2 = 0x0;

			memcpy(&buf->data.ra, pDevice->abyCurrentNetAddr, ETH_ALEN);
		} else { //if (byFBOption != AUTO_FB_NONE && uDMAIdx != TYPE_ATIMDMA && uDMAIdx != TYPE_BEACONDMA)
			struct vnt_cts *buf = pvCTS;
			/* Get SignalField, ServiceField & Length */
			vnt_get_phy_field(pDevice, uCTSFrameLen,
					  pDevice->byTopCCKBasicRate,
					  PK_TYPE_11B, &buf->b);

			/* Get CTSDuration_ba */
			buf->duration_ba =
				s_uGetRTSCTSDuration(pDevice, CTSDUR_BA,
						     cbFrameLength, byPktType,
						     wCurrentRate, bNeedAck,
						     byFBOption);

			/* Get CTS Frame body */
			buf->data.duration = buf->duration_ba;

			buf->data.frame_control =
				cpu_to_le16(IEEE80211_FTYPE_CTL |
					    IEEE80211_STYPE_CTS);

			buf->reserved2 = 0x0;
			memcpy(&buf->data.ra, pDevice->abyCurrentNetAddr, ETH_ALEN);
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
	struct vnt_private *pDevice,
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

	PSTxBufHead pFifoHead = (PSTxBufHead)pTxBufHead;

	pFifoHead->wReserved = wCurrentRate;
	wFifoCtl = pFifoHead->wFIFOCtl;

	if (wFifoCtl & FIFOCTL_CRCDIS)
		bDisCRC = true;

	if (wFifoCtl & FIFOCTL_AUTO_FB_0)
		byFBOption = AUTO_FB_0;
	else if (wFifoCtl & FIFOCTL_AUTO_FB_1)
		byFBOption = AUTO_FB_1;

	if (pDevice->bLongHeader)
		cbMACHdLen = WLAN_HDR_ADDR3_LEN + 6;

	if (!pvRrvTime)
		return;

	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
		if (pvRTS != NULL) { //RTS_need
			/* Fill RsvTime */
			struct vnt_rrv_time_rts *buf = pvRrvTime;

			buf->rts_rrv_time_aa = s_uGetRTSCTSRsvTime(pDevice, 2, byPktType, cbFrameSize, wCurrentRate);
			buf->rts_rrv_time_ba = s_uGetRTSCTSRsvTime(pDevice, 1, byPktType, cbFrameSize, wCurrentRate);
			buf->rts_rrv_time_bb = s_uGetRTSCTSRsvTime(pDevice, 0, byPktType, cbFrameSize, wCurrentRate);
			buf->rrv_time_a = vnt_rxtx_rsvtime_le16(pDevice, byPktType, cbFrameSize, wCurrentRate, bNeedACK);
			buf->rrv_time_b = vnt_rxtx_rsvtime_le16(pDevice, PK_TYPE_11B, cbFrameSize, pDevice->byTopCCKBasicRate, bNeedACK);

			s_vFillRTSHead(pDevice, byPktType, pvRTS, cbFrameSize, bNeedACK, bDisCRC, psEthHeader, wCurrentRate, byFBOption);
		} else {//RTS_needless, PCF mode
			struct vnt_rrv_time_cts *buf = pvRrvTime;

			buf->rrv_time_a = vnt_rxtx_rsvtime_le16(pDevice, byPktType, cbFrameSize, wCurrentRate, bNeedACK);
			buf->rrv_time_b = vnt_rxtx_rsvtime_le16(pDevice, PK_TYPE_11B, cbFrameSize, pDevice->byTopCCKBasicRate, bNeedACK);
			buf->cts_rrv_time_ba = s_uGetRTSCTSRsvTime(pDevice, 3, byPktType, cbFrameSize, wCurrentRate);

			//Fill CTS
			s_vFillCTSHead(pDevice, uDMAIdx, byPktType, pvCTS, cbFrameSize, bNeedACK, bDisCRC, wCurrentRate, byFBOption);
		}
	} else if (byPktType == PK_TYPE_11A) {
		if (pvRTS != NULL) {//RTS_need, non PCF mode
			struct vnt_rrv_time_ab *buf = pvRrvTime;

			buf->rts_rrv_time = s_uGetRTSCTSRsvTime(pDevice, 2, byPktType, cbFrameSize, wCurrentRate);
			buf->rrv_time = vnt_rxtx_rsvtime_le16(pDevice, byPktType, cbFrameSize, wCurrentRate, bNeedACK);

			//Fill RTS
			s_vFillRTSHead(pDevice, byPktType, pvRTS, cbFrameSize, bNeedACK, bDisCRC, psEthHeader, wCurrentRate, byFBOption);
		} else if (pvRTS == NULL) {//RTS_needless, non PCF mode
			struct vnt_rrv_time_ab *buf = pvRrvTime;

			buf->rrv_time = vnt_rxtx_rsvtime_le16(pDevice, PK_TYPE_11A, cbFrameSize, wCurrentRate, bNeedACK);
		}
	} else if (byPktType == PK_TYPE_11B) {
		if ((pvRTS != NULL)) {//RTS_need, non PCF mode
			struct vnt_rrv_time_ab *buf = pvRrvTime;

			buf->rts_rrv_time = s_uGetRTSCTSRsvTime(pDevice, 0, byPktType, cbFrameSize, wCurrentRate);
			buf->rrv_time = vnt_rxtx_rsvtime_le16(pDevice, PK_TYPE_11B, cbFrameSize, wCurrentRate, bNeedACK);

			//Fill RTS
			s_vFillRTSHead(pDevice, byPktType, pvRTS, cbFrameSize, bNeedACK, bDisCRC, psEthHeader, wCurrentRate, byFBOption);
		} else { //RTS_needless, non PCF mode
			struct vnt_rrv_time_ab *buf = pvRrvTime;

			buf->rrv_time = vnt_rxtx_rsvtime_le16(pDevice, PK_TYPE_11B, cbFrameSize, wCurrentRate, bNeedACK);
		}
	}
}

static
void
s_vFillFragParameter(
	struct vnt_private *pDevice,
	unsigned char *pbyBuffer,
	unsigned int uTxType,
	void *pvtdCurr,
	unsigned short wFragType,
	unsigned int cbReqCount
)
{
	PSTxBufHead pTxBufHead = (PSTxBufHead) pbyBuffer;

	if (uTxType == TYPE_SYNCDMA) {
		PSTxSyncDesc ptdCurr = (PSTxSyncDesc)pvtdCurr;

		//Set FIFOCtl & TimeStamp in TxSyncDesc
		ptdCurr->m_wFIFOCtl = pTxBufHead->wFIFOCtl;
		ptdCurr->m_wTimeStamp = pTxBufHead->wTimeStamp;
		//Set TSR1 & ReqCount in TxDescHead
		ptdCurr->m_td1TD1.wReqCount = cpu_to_le16((unsigned short)(cbReqCount));
		if (wFragType == FRAGCTL_ENDFRAG) //Last Fragmentation
			ptdCurr->m_td1TD1.byTCR |= (TCR_STP | TCR_EDP | EDMSDU);
		else
			ptdCurr->m_td1TD1.byTCR |= (TCR_STP | TCR_EDP);
	} else {
		PSTxDesc ptdCurr = (PSTxDesc)pvtdCurr;
		//Set TSR1 & ReqCount in TxDescHead
		ptdCurr->m_td1TD1.wReqCount = cpu_to_le16((unsigned short)(cbReqCount));
		if (wFragType == FRAGCTL_ENDFRAG) //Last Fragmentation
			ptdCurr->m_td1TD1.byTCR |= (TCR_STP | TCR_EDP | EDMSDU);
		else
			ptdCurr->m_td1TD1.byTCR |= (TCR_STP | TCR_EDP);
	}

	pTxBufHead->wFragCtl |= (unsigned short)wFragType;//0x0001; //0000 0000 0000 0001
}

static unsigned int
s_cbFillTxBufHead(struct vnt_private *pDevice, unsigned char byPktType,
		  unsigned char *pbyTxBufferAddr, unsigned int cbFrameBodySize,
		  unsigned int uDMAIdx, PSTxDesc pHeadTD,
		  PSEthernetHeader psEthHeader, unsigned char *pPacket,
		  bool bNeedEncrypt, PSKeyItem pTransmitKey,
		  unsigned int uNodeIndex, unsigned int *puMACfragNum)
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
	__le16 uDuration;
	unsigned char *pbyBuffer;
	unsigned int cbIVlen = 0;
	unsigned int cbICVlen = 0;
	unsigned int cbMIClen = 0;
	unsigned int cbFCSlen = 4;
	unsigned int cb802_1_H_len = 0;
	unsigned int uLength = 0;
	unsigned int uTmpLen = 0;
	unsigned int cbMICHDR = 0;
	u32 dwMICKey0, dwMICKey1;
	u32 dwMIC_Priority;
	u32 *pdwMIC_L;
	u32 *pdwMIC_R;
	u32 dwSafeMIC_L, dwSafeMIC_R; /* Fix "Last Frag Size" < "MIC length". */
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
	unsigned int cbHeaderLength = 0;
	void *pvRrvTime;
	struct vnt_mic_hdr *pMICHDR;
	void *pvRTS;
	void *pvCTS;
	void *pvTxDataHd;
	unsigned short wTxBufSize;   // FFinfo size
	unsigned int uTotalCopyLength = 0;
	unsigned char byFBOption = AUTO_FB_NONE;
	bool bIsWEP256 = false;
	PSMgmtObject    pMgmt = pDevice->pMgmt;

	pvRrvTime = pMICHDR = pvRTS = pvCTS = pvTxDataHd = NULL;

	if ((pDevice->op_mode == NL80211_IFTYPE_ADHOC) ||
	    (pDevice->op_mode == NL80211_IFTYPE_AP)) {
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
			if (pTransmitKey->uKeyLength == WLAN_WEP232_KEYLEN)
				bIsWEP256 = true;
		}
		if (pTransmitKey->byCipherSuite == KEY_CTL_TKIP) {
			cbIVlen = 8;//IV+ExtIV
			cbMIClen = 8;
			cbICVlen = 4;
		}
		if (pTransmitKey->byCipherSuite == KEY_CTL_CCMP) {
			cbIVlen = 8;//RSN Header
			cbICVlen = 8;//MIC
			cbMICHDR = sizeof(struct vnt_mic_hdr);
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
	if (psTxBufHd->wFIFOCtl & FIFOCTL_AUTO_FB_0)
		byFBOption = AUTO_FB_0;
	else if (psTxBufHd->wFIFOCtl & FIFOCTL_AUTO_FB_1)
		byFBOption = AUTO_FB_1;

	//////////////////////////////////////////////////////
	//Set RrvTime/RTS/CTS Buffer
	wTxBufSize = sizeof(STxBufHead);
	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {//802.11g packet

		if (byFBOption == AUTO_FB_NONE) {
			if (bRTS == true) {//RTS_need
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_rts));
				pvRTS = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_rts) + cbMICHDR);
				pvCTS = NULL;
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_rts) +
							cbMICHDR + sizeof(struct vnt_rts_g));
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_rts) +
							cbMICHDR + sizeof(struct vnt_rts_g) +
							sizeof(struct vnt_tx_datahead_g);
			} else { //RTS_needless
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_cts));
				pvRTS = NULL;
				pvCTS = (void *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_cts) + cbMICHDR);
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize +
						sizeof(struct vnt_rrv_time_cts) + cbMICHDR + sizeof(struct vnt_cts));
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_cts) +
							cbMICHDR + sizeof(struct vnt_cts) + sizeof(struct vnt_tx_datahead_g);
			}
		} else {
			// Auto Fall Back
			if (bRTS == true) {//RTS_need
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_rts));
				pvRTS = (void *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_rts) + cbMICHDR);
				pvCTS = NULL;
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_rts) +
					cbMICHDR + sizeof(struct vnt_rts_g_fb));
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_rts) +
					cbMICHDR + sizeof(struct vnt_rts_g_fb) + sizeof(struct vnt_tx_datahead_g_fb);
			} else { //RTS_needless
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_cts));
				pvRTS = NULL;
				pvCTS = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_cts) + cbMICHDR);
				pvTxDataHd = (void  *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_cts) +
					cbMICHDR + sizeof(struct vnt_cts_fb));
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_cts) +
					cbMICHDR + sizeof(struct vnt_cts_fb) + sizeof(struct vnt_tx_datahead_g_fb);
			}
		} // Auto Fall Back
	} else {//802.11a/b packet

		if (byFBOption == AUTO_FB_NONE) {
			if (bRTS == true) {
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab));
				pvRTS = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab) + cbMICHDR);
				pvCTS = NULL;
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize +
					sizeof(struct vnt_rrv_time_ab) + cbMICHDR + sizeof(struct vnt_rts_ab));
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_ab) +
					cbMICHDR + sizeof(struct vnt_rts_ab) + sizeof(struct vnt_tx_datahead_ab);
			} else { //RTS_needless, need MICHDR
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab));
				pvRTS = NULL;
				pvCTS = NULL;
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab) + cbMICHDR);
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_ab) +
					cbMICHDR + sizeof(struct vnt_tx_datahead_ab);
			}
		} else {
			// Auto Fall Back
			if (bRTS == true) {//RTS_need
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab));
				pvRTS = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab) + cbMICHDR);
				pvCTS = NULL;
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize +
					sizeof(struct vnt_rrv_time_ab) + cbMICHDR + sizeof(struct vnt_rts_a_fb));
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_ab) +
					cbMICHDR + sizeof(struct vnt_rts_a_fb) + sizeof(struct vnt_tx_datahead_a_fb);
			} else { //RTS_needless
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab));
				pvRTS = NULL;
				pvCTS = NULL;
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab) + cbMICHDR);
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_ab) +
					cbMICHDR + sizeof(struct vnt_tx_datahead_a_fb);
			}
		} // Auto Fall Back
	}
	memset((void *)(pbyTxBufferAddr + wTxBufSize), 0, (cbHeaderLength - wTxBufSize));

//////////////////////////////////////////////////////////////////
	if ((bNeedEncrypt == true) && (pTransmitKey != NULL) && (pTransmitKey->byCipherSuite == KEY_CTL_TKIP)) {
		if (pDevice->pMgmt->eAuthenMode == WMAC_AUTH_WPANONE) {
			dwMICKey0 = *(u32 *)(&pTransmitKey->abyKey[16]);
			dwMICKey1 = *(u32 *)(&pTransmitKey->abyKey[20]);
		} else if ((pTransmitKey->dwKeyIndex & AUTHENTICATOR_KEY) != 0) {
			dwMICKey0 = *(u32 *)(&pTransmitKey->abyKey[16]);
			dwMICKey1 = *(u32 *)(&pTransmitKey->abyKey[20]);
		} else {
			dwMICKey0 = *(u32 *)(&pTransmitKey->abyKey[24]);
			dwMICKey1 = *(u32 *)(&pTransmitKey->abyKey[28]);
		}
		// DO Software Michael
		MIC_vInit(dwMICKey0, dwMICKey1);
		MIC_vAppend((unsigned char *)&(psEthHeader->abyDstAddr[0]), 12);
		dwMIC_Priority = 0;
		MIC_vAppend((unsigned char *)&dwMIC_Priority, 4);
		pr_debug("MIC KEY: %X, %X\n", dwMICKey0, dwMICKey1);
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
		if (cbLastFragPayloadSize == 0)
			cbLastFragPayloadSize = cbFragPayloadSize;
		else
			uMACfragNum++;

		//[Hdr+(IV)+last fragment payload+(MIC)+(ICV)+FCS]
		cbLastFragmentSize = cbMACHdLen + cbLastFragPayloadSize + cbIVlen + cbICVlen + cbFCSlen;

		for (uFragIdx = 0; uFragIdx < uMACfragNum; uFragIdx++) {
			if (uFragIdx == 0) {
				//=========================
				//    Start Fragmentation
				//=========================
				pr_debug("Start Fragmentation...\n");
				wFragType = FRAGCTL_STAFRAG;

				//Fill FIFO,RrvTime,RTS,and CTS
				s_vGenerateTxParameter(pDevice, byPktType, (void *)psTxBufHd, pvRrvTime, pvRTS, pvCTS,
						       cbFragmentSize, bNeedACK, uDMAIdx, psEthHeader, pDevice->wCurrentRate);
				//Fill DataHead
				uDuration = s_uFillDataHead(pDevice, byPktType, pvTxDataHd, cbFragmentSize, uDMAIdx, bNeedACK,
							    uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption, pDevice->wCurrentRate);
				// Generate TX MAC Header
				vGenerateMACHeader(pDevice, pbyMacHdr, uDuration, psEthHeader, bNeedEncrypt,
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
					pr_debug("Start MIC: %d\n",
						 cbFragPayloadSize);
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
				pr_debug("Last Fragmentation...\n");

				wFragType = FRAGCTL_ENDFRAG;

				//Fill FIFO,RrvTime,RTS,and CTS
				s_vGenerateTxParameter(pDevice, byPktType, (void *)psTxBufHd, pvRrvTime, pvRTS, pvCTS,
						       cbLastFragmentSize, bNeedACK, uDMAIdx, psEthHeader, pDevice->wCurrentRate);
				//Fill DataHead
				uDuration = s_uFillDataHead(pDevice, byPktType, pvTxDataHd, cbLastFragmentSize, uDMAIdx, bNeedACK,
							    uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption, pDevice->wCurrentRate);

				// Generate TX MAC Header
				vGenerateMACHeader(pDevice, pbyMacHdr, uDuration, psEthHeader, bNeedEncrypt,
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
					pr_debug("LAST: uMICFragLen:%d, cbLastFragPayloadSize:%d, uTmpLen:%d\n",
						 uMICFragLen,
						 cbLastFragPayloadSize,
						 uTmpLen);

					if (bMIC2Frag == false) {
						if (uTmpLen != 0)
							MIC_vAppend((pbyBuffer + uLength), uTmpLen);
						pdwMIC_L = (u32 *)(pbyBuffer + uLength + uTmpLen);
						pdwMIC_R = (u32 *)(pbyBuffer + uLength + uTmpLen + 4);
						MIC_vGetMIC(pdwMIC_L, pdwMIC_R);
						pr_debug("Last MIC:%X, %X\n",
							 *pdwMIC_L, *pdwMIC_R);
					} else {
						if (uMICFragLen >= 4) {
							memcpy((pbyBuffer + uLength), ((unsigned char *)&dwSafeMIC_R + (uMICFragLen - 4)),
							       (cbMIClen - uMICFragLen));
							pr_debug("LAST: uMICFragLen >= 4: %X, %d\n",
								 *(unsigned char *)((unsigned char *)&dwSafeMIC_R + (uMICFragLen - 4)),
								 (cbMIClen - uMICFragLen));

						} else {
							memcpy((pbyBuffer + uLength), ((unsigned char *)&dwSafeMIC_L + uMICFragLen),
							       (4 - uMICFragLen));
							memcpy((pbyBuffer + uLength + (4 - uMICFragLen)), &dwSafeMIC_R, 4);
							pr_debug("LAST: uMICFragLen < 4: %X, %d\n",
								 *(unsigned char *)((unsigned char *)&dwSafeMIC_R + uMICFragLen - 4),
								 (cbMIClen - uMICFragLen));
						}
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
				pr_debug("Middle Fragmentation...\n");

				wFragType = FRAGCTL_MIDFRAG;

				//Fill FIFO,RrvTime,RTS,and CTS
				s_vGenerateTxParameter(pDevice, byPktType, (void *)psTxBufHd, pvRrvTime, pvRTS, pvCTS,
						       cbFragmentSize, bNeedACK, uDMAIdx, psEthHeader, pDevice->wCurrentRate);
				//Fill DataHead
				uDuration = s_uFillDataHead(pDevice, byPktType, pvTxDataHd, cbFragmentSize, uDMAIdx, bNeedACK,
							    uFragIdx, cbLastFragmentSize, uMACfragNum, byFBOption, pDevice->wCurrentRate);

				// Generate TX MAC Header
				vGenerateMACHeader(pDevice, pbyMacHdr, uDuration, psEthHeader, bNeedEncrypt,
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

						pdwMIC_L = (u32 *)(pbyBuffer + uLength + uTmpLen);
						pdwMIC_R = (u32 *)(pbyBuffer + uLength + uTmpLen + 4);
						MIC_vGetMIC(pdwMIC_L, pdwMIC_R);
						dwSafeMIC_L = *pdwMIC_L;
						dwSafeMIC_R = *pdwMIC_R;

						pr_debug("MIDDLE: uMICFragLen:%d, cbFragPayloadSize:%d, uTmpLen:%d\n",
							 uMICFragLen,
							 cbFragPayloadSize,
							 uTmpLen);
						pr_debug("Fill MIC in Middle frag [%d]\n",
							 uMICFragLen);
						pr_debug("Get MIC:%X, %X\n",
							 *pdwMIC_L, *pdwMIC_R);
					}
					pr_debug("Middle frag len: %d\n",
						 uTmpLen);

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
		vGenerateMACHeader(pDevice, pbyMacHdr, uDuration, psEthHeader, bNeedEncrypt,
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
			pr_debug("Length:%d, %d\n",
				 cbFrameBodySize - cb802_1_H_len, uLength);

			MIC_vAppend((pbyBuffer + uLength - cb802_1_H_len), cbFrameBodySize);

			pdwMIC_L = (u32 *)(pbyBuffer + uLength - cb802_1_H_len + cbFrameBodySize);
			pdwMIC_R = (u32 *)(pbyBuffer + uLength - cb802_1_H_len + cbFrameBodySize + 4);

			MIC_vGetMIC(pdwMIC_L, pdwMIC_R);
			MIC_vUnInit();

			if (pDevice->bTxMICFail == true) {
				*pdwMIC_L = 0;
				*pdwMIC_R = 0;
				pDevice->bTxMICFail = false;
			}

			pr_debug("uLength: %d, %d\n", uLength, cbFrameBodySize);
			pr_debug("cbReqCount:%d, %d, %d, %d\n",
				 cbReqCount, cbHeaderLength, uPadding, cbIVlen);
			pr_debug("MIC:%x, %x\n", *pdwMIC_L, *pdwMIC_R);

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

	}
	*puMACfragNum = uMACfragNum;

	return cbHeaderLength;
}

void
vGenerateFIFOHeader(struct vnt_private *pDevice, unsigned char byPktType,
		    unsigned char *pbyTxBufferAddr, bool bNeedEncrypt,
		    unsigned int cbPayloadSize, unsigned int uDMAIdx,
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

	if ((pDevice->op_mode == NL80211_IFTYPE_ADHOC) ||
	    (pDevice->op_mode == NL80211_IFTYPE_AP)) {
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
	if (TYPE_TXDMA0 == uDMAIdx)
		pTxBufHead->wFIFOCtl |= FIFOCTL_ISDMA0;

	//Set FRAGCTL_MACHDCNT
	if (pDevice->bLongHeader)
		cbMacHdLen = WLAN_HDR_ADDR3_LEN + 6;
	else
		cbMacHdLen = WLAN_HDR_ADDR3_LEN;

	pTxBufHead->wFragCtl |= cpu_to_le16((unsigned short)(cbMacHdLen << 10));

	//Set packet type
	if (byPktType == PK_TYPE_11A) //0000 0000 0000 0000
		;
	else if (byPktType == PK_TYPE_11B) //0000 0001 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11B;
	else if (byPktType == PK_TYPE_11GB) //0000 0010 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11GB;
	else if (byPktType == PK_TYPE_11GA) //0000 0011 0000 0000
		pTxBufHead->wFIFOCtl |= FIFOCTL_11GA;

	//Set FIFOCTL_GrpAckPolicy
	if (pDevice->bGrpAckPolicy == true) //0000 0100 0000 0000
		pTxBufHead->wFIFOCtl |=	FIFOCTL_GRPACK;

	//Set Auto Fallback Ctl
	if (pDevice->wCurrentRate >= RATE_18M) {
		if (pDevice->byAutoFBCtrl == AUTO_FB_0)
			pTxBufHead->wFIFOCtl |= FIFOCTL_AUTO_FB_0;
		else if (pDevice->byAutoFBCtrl == AUTO_FB_1)
			pTxBufHead->wFIFOCtl |= FIFOCTL_AUTO_FB_1;
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

	RFbSetPower(pDevice, pDevice->wCurrentRate, pDevice->byCurrentCh);

	pTxBufHead->byTxPower = pDevice->byCurPwr;

	*pcbHeaderSize = s_cbFillTxBufHead(pDevice, byPktType, pbyTxBufferAddr, cbPayloadSize,
					   uDMAIdx, pHeadTD, psEthHeader, pPacket, bNeedEncrypt,
					   pTransmitKey, uNodeIndex, puMACfragNum);
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
	struct vnt_private *pDevice,
	unsigned char *pbyBufferAddr,
	__le16 wDuration,
	PSEthernetHeader psEthHeader,
	bool bNeedEncrypt,
	unsigned short wFragType,
	unsigned int uDMAIdx,
	unsigned int uFragIdx
)
{
	PS802_11Header  pMACHeader = (PS802_11Header)pbyBufferAddr;

	memset(pMACHeader, 0, (sizeof(S802_11Header)));

	if (uDMAIdx == TYPE_ATIMDMA)
		pMACHeader->wFrameCtl = TYPE_802_11_ATIM;
	else
		pMACHeader->wFrameCtl = TYPE_802_11_DATA;

	if (pDevice->op_mode == NL80211_IFTYPE_AP) {
		memcpy(&(pMACHeader->abyAddr1[0]), &(psEthHeader->abyDstAddr[0]), ETH_ALEN);
		memcpy(&(pMACHeader->abyAddr2[0]), &(pDevice->abyBSSID[0]), ETH_ALEN);
		memcpy(&(pMACHeader->abyAddr3[0]), &(psEthHeader->abySrcAddr[0]), ETH_ALEN);
		pMACHeader->wFrameCtl |= FC_FROMDS;
	} else {
		if (pDevice->op_mode == NL80211_IFTYPE_ADHOC) {
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

	pMACHeader->wDurationID = le16_to_cpu(wDuration);

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

	if ((wFragType == FRAGCTL_STAFRAG) || (wFragType == FRAGCTL_MIDFRAG)) //StartFrag or MidFrag
		pMACHeader->wFrameCtl |= FC_MOREFRAG;
}

CMD_STATUS csMgmt_xmit(struct vnt_private *pDevice, PSTxMgmtPacket pPacket)
{
	PSTxDesc        pFrstTD;
	unsigned char byPktType;
	unsigned char *pbyTxBufferAddr;
	void *pvRTS;
	struct vnt_cts *pCTS;
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

	if (AVAIL_TD(pDevice, TYPE_TXDMA0) <= 0)
		return CMD_STATUS_RESOURCES;

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
	if (pDevice->pMgmt->eScanState != WMAC_NO_SCANNING)
		RFbSetPower(pDevice, wCurrentRate, pDevice->byCurrentCh);
	else
		RFbSetPower(pDevice, wCurrentRate, pMgmt->uCurrChannel);

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
	}

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
	if (pDevice->bGrpAckPolicy == true) //0000 0100 0000 0000
		pTxBufHead->wFIFOCtl |=	FIFOCTL_GRPACK;

	//the rest of pTxBufHead->wFragCtl:FragTyp will be set later in s_vFillFragParameter()

	//Set RrvTime/RTS/CTS Buffer
	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {//802.11g packet
		pvRrvTime = (void *) (pbyTxBufferAddr + wTxBufSize);
		pMICHDR = NULL;
		pvRTS = NULL;
		pCTS = (struct vnt_cts *)(pbyTxBufferAddr + wTxBufSize +
					sizeof(struct vnt_rrv_time_cts));
		pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize +
				sizeof(struct vnt_rrv_time_cts) + sizeof(struct vnt_cts));
		cbHeaderSize = wTxBufSize + sizeof(struct vnt_rrv_time_cts) +
				sizeof(struct vnt_cts) + sizeof(struct vnt_tx_datahead_g);
	} else { // 802.11a/b packet
		pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
		pMICHDR = NULL;
		pvRTS = NULL;
		pCTS = NULL;
		pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize +
			sizeof(struct vnt_rrv_time_ab));
		cbHeaderSize = wTxBufSize + sizeof(struct vnt_rrv_time_ab) +
			sizeof(struct vnt_tx_datahead_ab);
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
			if ((pDevice->op_mode == NL80211_IFTYPE_STATION) &&
			    (pDevice->bLinkPass == true)) {
				pbyBSSID = pDevice->abyBSSID;
				// get pairwise key
				if (KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, PAIRWISE_KEY, &pTransmitKey) == false) {
					// get group key
					if (KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, GROUP_KEY, &pTransmitKey) == true) {
						pr_debug("Get GTK\n");
						break;
					}
				} else {
					pr_debug("Get PTK\n");
					break;
				}
			}
			// get group key
			pbyBSSID = pDevice->abyBroadcastAddr;
			if (KeybGetTransmitKey(&(pDevice->sKey), pbyBSSID, GROUP_KEY, &pTransmitKey) == false) {
				pTransmitKey = NULL;
				pr_debug("KEY is NULL. OP Mode[%d]\n",
					 pDevice->op_mode);
			} else {
				pr_debug("Get GTK\n");
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
			((struct vnt_tx_datahead_g *)pvTxDataHd)->duration_a = cpu_to_le16(pPacket->p80211Header->sA2.wDurationID);
			((struct vnt_tx_datahead_g *)pvTxDataHd)->duration_b = cpu_to_le16(pPacket->p80211Header->sA2.wDurationID);
		} else {
			((struct vnt_tx_datahead_ab *)pvTxDataHd)->duration = cpu_to_le16(pPacket->p80211Header->sA2.wDurationID);
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

	if (AVAIL_TD(pDevice, TYPE_TXDMA0) <= 1)
		pr_debug(" available td0 <= 1\n");

	pDevice->apCurrTD[TYPE_TXDMA0] = pFrstTD->next;

	pDevice->nTxDataTimeCout = 0; //2008-8-21 chester <add> for send null packet

	// Poll Transmit the adapter
	MACvTransmit0(pDevice->PortOffset);

	return CMD_STATUS_PENDING;
}

CMD_STATUS csBeacon_xmit(struct vnt_private *pDevice, PSTxMgmtPacket pPacket)
{
	unsigned char byPktType;
	unsigned char *pbyBuffer = (unsigned char *)pDevice->tx_beacon_bufs;
	unsigned int cbFrameSize = pPacket->cbMPDULen + WLAN_FCS_LEN;
	unsigned int cbHeaderSize = 0;
	struct vnt_tx_short_buf_head *short_head =
				(struct vnt_tx_short_buf_head *)pbyBuffer;
	PS802_11Header   pMACHeader;
	unsigned short wCurrentRate;

	memset(short_head, 0, sizeof(*short_head));

	if (pDevice->eCurrentPHYType == PHY_TYPE_11A) {
		wCurrentRate = RATE_6M;
		byPktType = PK_TYPE_11A;
	} else {
		wCurrentRate = RATE_2M;
		byPktType = PK_TYPE_11B;
	}

	//Set Preamble type always long
	pDevice->byPreambleType = PREAMBLE_LONG;

	/* Set FIFOCTL_GENINT */
	short_head->fifo_ctl |= cpu_to_le16(FIFOCTL_GENINT);

	/* Set packet type & Get Duration */
	if (byPktType == PK_TYPE_11A) {//0000 0000 0000 0000
		short_head->duration =
			cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_A,
				    cbFrameSize, byPktType, wCurrentRate, false,
				    0, 0, 1, AUTO_FB_NONE));
	} else if (byPktType == PK_TYPE_11B) {//0000 0001 0000 0000
		short_head->fifo_ctl |= cpu_to_le16(FIFOCTL_11B);

		short_head->duration =
			cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_B,
				    cbFrameSize, byPktType, wCurrentRate, false,
				    0, 0, 1, AUTO_FB_NONE));
	}

	vnt_get_phy_field(pDevice, cbFrameSize,
			  wCurrentRate, byPktType, &short_head->ab);

	/* Get TimeStampOff */
	short_head->time_stamp_off = vnt_time_stamp_off(pDevice, wCurrentRate);
	cbHeaderSize = sizeof(struct vnt_tx_short_buf_head);

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
	struct vnt_private *pDevice,
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

	if ((pDevice->op_mode == NL80211_IFTYPE_ADHOC) ||
	    (pDevice->op_mode == NL80211_IFTYPE_AP)) {
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
		if (cbLastFragPayloadSize == 0)
			cbLastFragPayloadSize = cbFragPayloadSize;
		else
			uMACfragNum++;
	}
	return uMACfragNum;
}

void vDMA0_tx_80211(struct vnt_private *pDevice, struct sk_buff *skb,
		    unsigned char *pbMPDU, unsigned int cbMPDULen)
{
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
	u32 dwMICKey0, dwMICKey1;
	u32 dwMIC_Priority;
	u32 *pdwMIC_L;
	u32 *pdwMIC_R;
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

	pvRrvTime = pMICHDR = pvRTS = pvCTS = pvTxDataHd = NULL;

	if (cbMPDULen <= WLAN_HDR_ADDR3_LEN)
		cbFrameBodySize = 0;
	else
		cbFrameBodySize = cbMPDULen - WLAN_HDR_ADDR3_LEN;

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
	if (pDevice->pMgmt->eScanState != WMAC_NO_SCANNING)
		RFbSetPower(pDevice, wCurrentRate, pDevice->byCurrentCh);
	else
		RFbSetPower(pDevice, wCurrentRate, pMgmt->uCurrChannel);

	pTxBufHead->byTxPower = pDevice->byCurPwr;

	//+++++++++++++++++++++ Patch VT3253 A1 performance +++++++++++++++++++++++++++
	if (pDevice->byFOETuning) {
		if ((p80211Header->sA3.wFrameCtl & TYPE_DATE_NULL) == TYPE_DATE_NULL) {
			wCurrentRate = RATE_24M;
			byPktType = PK_TYPE_11GA;
		}
	}

	pr_debug("vDMA0_tx_80211: p80211Header->sA3.wFrameCtl = %x\n",
		 p80211Header->sA3.wFrameCtl);

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
	}

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
		if (((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates)->len != 0)
			cbExtSuppRate += ((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates)->len + WLAN_IEHDR_LEN;

		if (((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates)->len != 0)
			cbExtSuppRate += ((PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates)->len + WLAN_IEHDR_LEN;

		if (cbExtSuppRate > 0)
			cbFrameBodySize = WLAN_ASSOCRESP_OFF_SUPP_RATES;
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
		} else if (pDevice->eEncryptionStatus == Ndis802_11Encryption3Enabled) {
			cbIVlen = 8;//RSN Header
			cbICVlen = 8;//MIC
			cbMICHDR = sizeof(struct vnt_mic_hdr);
			pTxBufHead->wFragCtl |= FRAGCTL_AES;
			pDevice->bAES = true;
		}
		//MAC Header should be padding 0 to DW alignment.
		uPadding = 4 - (cbMacHdLen%4);
		uPadding %= 4;
	}

	cbFrameSize = cbMacHdLen + cbFrameBodySize + cbIVlen + cbMIClen + cbICVlen + cbFCSlen + cbExtSuppRate;

	//Set FIFOCTL_GrpAckPolicy
	if (pDevice->bGrpAckPolicy == true) //0000 0100 0000 0000
		pTxBufHead->wFIFOCtl |=	FIFOCTL_GRPACK;

	//the rest of pTxBufHead->wFragCtl:FragTyp will be set later in s_vFillFragParameter()

	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {//802.11g packet

		pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
		pMICHDR = (struct vnt_mic_hdr *)(pbyTxBufferAddr + wTxBufSize +
					sizeof(struct vnt_rrv_time_cts));
		pvRTS = NULL;
		pvCTS = (struct vnt_cts *)(pbyTxBufferAddr + wTxBufSize +
				sizeof(struct vnt_rrv_time_cts) + cbMICHDR);
		pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize +
			sizeof(struct vnt_rrv_time_cts) + cbMICHDR + sizeof(struct vnt_cts));
		cbHeaderSize = wTxBufSize + sizeof(struct vnt_rrv_time_cts) +
				cbMICHDR + sizeof(struct vnt_cts) + sizeof(struct vnt_tx_datahead_g);

	} else {//802.11a/b packet

		pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
		pMICHDR = (struct vnt_mic_hdr *) (pbyTxBufferAddr +
				wTxBufSize + sizeof(struct vnt_rrv_time_ab));
		pvRTS = NULL;
		pvCTS = NULL;
		pvTxDataHd = (void *)(pbyTxBufferAddr +
			wTxBufSize + sizeof(struct vnt_rrv_time_ab) + cbMICHDR);
		cbHeaderSize = wTxBufSize + sizeof(struct vnt_rrv_time_ab) +
				cbMICHDR + sizeof(struct vnt_tx_datahead_ab);

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
			dwMICKey0 = *(u32 *)(&pTransmitKey->abyKey[16]);
			dwMICKey1 = *(u32 *)(&pTransmitKey->abyKey[20]);

			// DO Software Michael
			MIC_vInit(dwMICKey0, dwMICKey1);
			MIC_vAppend((unsigned char *)&(sEthHeader.abyDstAddr[0]), 12);
			dwMIC_Priority = 0;
			MIC_vAppend((unsigned char *)&dwMIC_Priority, 4);
			pr_debug("DMA0_tx_8021:MIC KEY: %X, %X\n",
				 dwMICKey0, dwMICKey1);

			uLength = cbHeaderSize + cbMacHdLen + uPadding + cbIVlen;

			MIC_vAppend((pbyTxBufferAddr + uLength), cbFrameBodySize);

			pdwMIC_L = (u32 *)(pbyTxBufferAddr + uLength + cbFrameBodySize);
			pdwMIC_R = (u32 *)(pbyTxBufferAddr + uLength + cbFrameBodySize + 4);

			MIC_vGetMIC(pdwMIC_L, pdwMIC_R);
			MIC_vUnInit();

			if (pDevice->bTxMICFail == true) {
				*pdwMIC_L = 0;
				*pdwMIC_R = 0;
				pDevice->bTxMICFail = false;
			}

			pr_debug("uLength: %d, %d\n", uLength, cbFrameBodySize);
			pr_debug("cbReqCount:%d, %d, %d, %d\n",
				 cbReqCount, cbHeaderSize, uPadding, cbIVlen);
			pr_debug("MIC:%x, %x\n", *pdwMIC_L, *pdwMIC_R);

		}

		s_vFillTxKey(pDevice, (unsigned char *)(pTxBufHead->adwTxKey), pbyIVHead, pTransmitKey,
			     pbyMacHdr, (unsigned short)cbFrameBodySize, (unsigned char *)pMICHDR);

		if (pDevice->bEnableHostWEP) {
			pMgmt->sNodeDBTable[uNodeIndex].dwTSC47_16 = pTransmitKey->dwTSC47_16;
			pMgmt->sNodeDBTable[uNodeIndex].wTSC15_0 = pTransmitKey->wTSC15_0;
		}

		if ((pDevice->byLocalID <= REV_ID_VT3253_A1))
			s_vSWencryption(pDevice, pTransmitKey, pbyPayloadHead, (unsigned short)(cbFrameBodySize + cbMIClen));
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
			((struct vnt_tx_datahead_g *)pvTxDataHd)->duration_a = cpu_to_le16(p80211Header->sA2.wDurationID);
			((struct vnt_tx_datahead_g *)pvTxDataHd)->duration_b = cpu_to_le16(p80211Header->sA2.wDurationID);
		} else {
			((struct vnt_tx_datahead_ab *)pvTxDataHd)->duration = cpu_to_le16(p80211Header->sA2.wDurationID);
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

	if (AVAIL_TD(pDevice, TYPE_TXDMA0) <= 1)
		pr_debug(" available td0 <= 1\n");

	pDevice->apCurrTD[TYPE_TXDMA0] = pFrstTD->next;

	// Poll Transmit the adapter
	MACvTransmit0(pDevice->PortOffset);
}
