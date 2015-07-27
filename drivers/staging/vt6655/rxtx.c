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
#include "card.h"
#include "mac.h"
#include "baseband.h"
#include "rf.h"

/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Static Definitions -------------------------*/
#define CRITICAL_PACKET_LEN      256    /* if packet size < 256 -> in-direct send
                                            packet size >= 256 -> direct send */

static const unsigned short wTimeStampOff[2][MAX_RATE] = {
	{384, 288, 226, 209, 54, 43, 37, 31, 28, 25, 24, 23}, /* Long Preamble */
	{384, 192, 130, 113, 54, 43, 37, 31, 28, 25, 24, 23}, /* Short Preamble */
};

static const unsigned short wFB_Opt0[2][5] = {
	{RATE_12M, RATE_18M, RATE_24M, RATE_36M, RATE_48M}, /* fallback_rate0 */
	{RATE_12M, RATE_12M, RATE_18M, RATE_24M, RATE_36M}, /* fallback_rate1 */
};
static const unsigned short wFB_Opt1[2][5] = {
	{RATE_12M, RATE_18M, RATE_24M, RATE_24M, RATE_36M}, /* fallback_rate0 */
	{RATE_6M , RATE_6M,  RATE_12M, RATE_12M, RATE_18M}, /* fallback_rate1 */
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
s_vFillRTSHead(
	struct vnt_private *pDevice,
	unsigned char byPktType,
	void *pvRTS,
	unsigned int	cbFrameLength,
	bool bNeedAck,
	bool bDisCRC,
	struct ieee80211_hdr *hdr,
	unsigned short wCurrentRate,
	unsigned char byFBOption
);

static
void
s_vGenerateTxParameter(
	struct vnt_private *pDevice,
	unsigned char byPktType,
	struct vnt_tx_fifo_head *,
	void *pvRrvTime,
	void *pvRTS,
	void *pvCTS,
	unsigned int	cbFrameSize,
	bool bNeedACK,
	unsigned int	uDMAIdx,
	void *psEthHeader,
	unsigned short wCurrentRate
);

static unsigned int
s_cbFillTxBufHead(struct vnt_private *pDevice, unsigned char byPktType,
		  unsigned char *pbyTxBufferAddr,
		  unsigned int uDMAIdx, PSTxDesc pHeadTD,
		  unsigned int uNodeIndex);

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
	unsigned short wCurrentRate,
	bool is_pspoll
);

/*---------------------  Export Variables  --------------------------*/

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
	if (byPktType == PK_TYPE_11B) /* llb,CCK mode */
		uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, (unsigned short)pDevice->byTopCCKBasicRate);
	else /* 11g 2.4G OFDM mode & 11a 5G OFDM mode */
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

/* byFreqType: 0=>5GHZ 1=>2.4GHZ */
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
	unsigned int uRrvTime, uRTSTime, uCTSTime, uAckTime, uDataTime;

	uRrvTime = uRTSTime = uCTSTime = uAckTime = uDataTime = 0;

	uDataTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, cbFrameLength, wCurrentRate);
	if (byRTSRsvType == 0) { /* RTSTxRrvTime_bb */
		uRTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 20, pDevice->byTopCCKBasicRate);
		uCTSTime = uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
	} else if (byRTSRsvType == 1) { /* RTSTxRrvTime_ba, only in 2.4GHZ */
		uRTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 20, pDevice->byTopCCKBasicRate);
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
		uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
	} else if (byRTSRsvType == 2) { /* RTSTxRrvTime_aa */
		uRTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 20, pDevice->byTopOFDMBasicRate);
		uCTSTime = uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
	} else if (byRTSRsvType == 3) { /* CTSTxRrvTime_ba, only in 2.4GHZ */
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
		uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
		uRrvTime = uCTSTime + uAckTime + uDataTime + 2*pDevice->uSIFS;
		return cpu_to_le16((u16)uRrvTime);
	}

	/* RTSRrvTime */
	uRrvTime = uRTSTime + uCTSTime + uAckTime + uDataTime + 3*pDevice->uSIFS;
	return cpu_to_le16((u16)uRrvTime);
}

/* byFreqType 0: 5GHz, 1:2.4Ghz */
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
	bool bLastFrag = false;
	unsigned int uAckTime = 0, uNextPktTime = 0;

	if (uFragIdx == (uMACfragNum-1))
		bLastFrag = true;

	switch (byDurType) {
	case DATADUR_B:    /* DATADUR_B */
		if (((uMACfragNum == 1)) || bLastFrag) {/* Non Frag or Last Frag */
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
				return pDevice->uSIFS + uAckTime;
			} else {
				return 0;
			}
		} else {/* First Frag or Mid Frag */
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

	case DATADUR_A:    /* DATADUR_A */
		if (((uMACfragNum == 1)) || bLastFrag) {/* Non Frag or Last Frag */
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
				return pDevice->uSIFS + uAckTime;
			} else {
				return 0;
			}
		} else {/* First Frag or Mid Frag */
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

	case DATADUR_A_F0:    /* DATADUR_A_F0 */
		if (((uMACfragNum == 1)) || bLastFrag) {/* Non Frag or Last Frag */
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
				return pDevice->uSIFS + uAckTime;
			} else {
				return 0;
			}
		} else { /* First Frag or Mid Frag */
			if (byFBOption == AUTO_FB_0) {
				if (wRate < RATE_18M)
					wRate = RATE_18M;
				else if (wRate > RATE_54M)
					wRate = RATE_54M;

				if (uFragIdx == (uMACfragNum-2))
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
				else
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);

			} else { /* (byFBOption == AUTO_FB_1) */
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

	case DATADUR_A_F1:    /* DATADUR_A_F1 */
		if (((uMACfragNum == 1)) || bLastFrag) { /* Non Frag or Last Frag */
			if (bNeedAck) {
				uAckTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
				return pDevice->uSIFS + uAckTime;
			} else {
				return 0;
			}
		} else { /* First Frag or Mid Frag */
			if (byFBOption == AUTO_FB_0) {
				if (wRate < RATE_18M)
					wRate = RATE_18M;
				else if (wRate > RATE_54M)
					wRate = RATE_54M;

				if (uFragIdx == (uMACfragNum-2))
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbLastFragmentSize, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
				else
					uNextPktTime = s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);

			} else { /* (byFBOption == AUTO_FB_1) */
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

/* byFreqType: 0=>5GHZ 1=>2.4GHZ */
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
	case RTSDUR_BB:    /* RTSDuration_bb */
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
		uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);
		break;

	case RTSDUR_BA:    /* RTSDuration_ba */
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
		uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);
		break;

	case RTSDUR_AA:    /* RTSDuration_aa */
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
		uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);
		break;

	case CTSDUR_BA:    /* CTSDuration_ba */
		uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wRate, bNeedAck);
		break;

	case RTSDUR_BA_F0: /* RTSDuration_ba_f0 */
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2 * pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
		else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2 * pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);

		break;

	case RTSDUR_AA_F0: /* RTSDuration_aa_f0 */
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
		else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);

		break;

	case RTSDUR_BA_F1: /* RTSDuration_ba_f1 */
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopCCKBasicRate);
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
		else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);

		break;

	case RTSDUR_AA_F1: /* RTSDuration_aa_f1 */
		uCTSTime = BBuGetFrameTime(pDevice->byPreambleType, byPktType, 14, pDevice->byTopOFDMBasicRate);
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE1][wRate-RATE_18M], bNeedAck);
		else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = uCTSTime + 2*pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE1][wRate-RATE_18M], bNeedAck);

		break;

	case CTSDUR_BA_F0: /* CTSDuration_ba_f0 */
		if ((byFBOption == AUTO_FB_0) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt0[FB_RATE0][wRate-RATE_18M], bNeedAck);
		else if ((byFBOption == AUTO_FB_1) && (wRate >= RATE_18M) && (wRate <= RATE_54M))
			uDurTime = pDevice->uSIFS + s_uGetTxRsvTime(pDevice, byPktType, cbFrameLength, wFB_Opt1[FB_RATE0][wRate-RATE_18M], bNeedAck);

		break;

	case CTSDUR_BA_F1: /* CTSDuration_ba_f1 */
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
	unsigned short wCurrentRate,
	bool is_pspoll
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

			if (is_pspoll) {
				__le16 dur = cpu_to_le16(pDevice->current_aid | BIT(14) | BIT(15));

				buf->duration_a = dur;
				buf->duration_b = dur;
			} else {
				/* Get Duration and TimeStamp */
				buf->duration_a =
					cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength,
									    byPktType, wCurrentRate, bNeedAck, uFragIdx,
									    cbLastFragmentSize, uMACfragNum,
									    byFBOption));
				buf->duration_b =
					cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameLength,
									    PK_TYPE_11B, pDevice->byTopCCKBasicRate,
									    bNeedAck, uFragIdx, cbLastFragmentSize,
									    uMACfragNum, byFBOption));
			}

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
		} /* if (byFBOption == AUTO_FB_NONE) */
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

			if (is_pspoll) {
				__le16 dur = cpu_to_le16(pDevice->current_aid | BIT(14) | BIT(15));

				buf->duration = dur;
			} else {
				/* Get Duration and TimeStampOff */
				buf->duration =
					cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_A, cbFrameLength, byPktType,
									    wCurrentRate, bNeedAck, uFragIdx,
									    cbLastFragmentSize, uMACfragNum,
									    byFBOption));
			}

			buf->time_stamp_off = vnt_time_stamp_off(pDevice, wCurrentRate);
			return buf->duration;
		}
	} else {
		struct vnt_tx_datahead_ab *buf = pTxDataHead;
		/* Get SignalField, ServiceField & Length */
		vnt_get_phy_field(pDevice, cbFrameLength, wCurrentRate,
				  byPktType, &buf->ab);

		if (is_pspoll) {
			__le16 dur = cpu_to_le16(pDevice->current_aid | BIT(14) | BIT(15));

			buf->duration = dur;
		} else {
			/* Get Duration and TimeStampOff */
			buf->duration =
				cpu_to_le16((u16)s_uGetDataDuration(pDevice, DATADUR_B, cbFrameLength, byPktType,
								    wCurrentRate, bNeedAck, uFragIdx,
								    cbLastFragmentSize, uMACfragNum,
								    byFBOption));
		}

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
	struct ieee80211_hdr *hdr,
	unsigned short wCurrentRate,
	unsigned char byFBOption
)
{
	unsigned int uRTSFrameLen = 20;

	if (pvRTS == NULL)
		return;

	if (bDisCRC) {
		/* When CRCDIS bit is on, H/W forgot to generate FCS for RTS frame,
		 in this case we need to decrease its length by 4. */
		uRTSFrameLen -= 4;
	}

	/* Note: So far RTSHead doesn't appear in ATIM & Beacom DMA, so we don't need to take them into account.
	       Otherwise, we need to modify codes for them. */
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

			ether_addr_copy(buf->data.ra, hdr->addr1);
			ether_addr_copy(buf->data.ta, hdr->addr2);
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

			ether_addr_copy(buf->data.ra, hdr->addr1);
			ether_addr_copy(buf->data.ta, hdr->addr2);
		} /* if (byFBOption == AUTO_FB_NONE) */
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

			ether_addr_copy(buf->data.ra, hdr->addr1);
			ether_addr_copy(buf->data.ta, hdr->addr2);
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

			ether_addr_copy(buf->data.ra, hdr->addr1);
			ether_addr_copy(buf->data.ta, hdr->addr2);
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

		ether_addr_copy(buf->data.ra, hdr->addr1);
		ether_addr_copy(buf->data.ta, hdr->addr2);
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
		/* When CRCDIS bit is on, H/W forgot to generate FCS for CTS frame,
		 in this case we need to decrease its length by 4. */
		uCTSFrameLen -= 4;
	}

	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
		if (byFBOption != AUTO_FB_NONE && uDMAIdx != TYPE_ATIMDMA && uDMAIdx != TYPE_BEACONDMA) {
			/* Auto Fall back */
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

			ether_addr_copy(buf->data.ra,
					pDevice->abyCurrentNetAddr);
		} else { /* if (byFBOption != AUTO_FB_NONE && uDMAIdx != TYPE_ATIMDMA && uDMAIdx != TYPE_BEACONDMA) */
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
			ether_addr_copy(buf->data.ra,
					pDevice->abyCurrentNetAddr);
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
 -
 * unsigned int cbFrameSize, Hdr+Payload+FCS */
static
void
s_vGenerateTxParameter(
	struct vnt_private *pDevice,
	unsigned char byPktType,
	struct vnt_tx_fifo_head *tx_buffer_head,
	void *pvRrvTime,
	void *pvRTS,
	void *pvCTS,
	unsigned int cbFrameSize,
	bool bNeedACK,
	unsigned int uDMAIdx,
	void *psEthHeader,
	unsigned short wCurrentRate
)
{
	u16 fifo_ctl = le16_to_cpu(tx_buffer_head->fifo_ctl);
	bool bDisCRC = false;
	unsigned char byFBOption = AUTO_FB_NONE;

	tx_buffer_head->current_rate = cpu_to_le16(wCurrentRate);

	if (fifo_ctl & FIFOCTL_CRCDIS)
		bDisCRC = true;

	if (fifo_ctl & FIFOCTL_AUTO_FB_0)
		byFBOption = AUTO_FB_0;
	else if (fifo_ctl & FIFOCTL_AUTO_FB_1)
		byFBOption = AUTO_FB_1;

	if (!pvRrvTime)
		return;

	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {
		if (pvRTS != NULL) { /* RTS_need
			 Fill RsvTime */
			struct vnt_rrv_time_rts *buf = pvRrvTime;

			buf->rts_rrv_time_aa = s_uGetRTSCTSRsvTime(pDevice, 2, byPktType, cbFrameSize, wCurrentRate);
			buf->rts_rrv_time_ba = s_uGetRTSCTSRsvTime(pDevice, 1, byPktType, cbFrameSize, wCurrentRate);
			buf->rts_rrv_time_bb = s_uGetRTSCTSRsvTime(pDevice, 0, byPktType, cbFrameSize, wCurrentRate);
			buf->rrv_time_a = vnt_rxtx_rsvtime_le16(pDevice, byPktType, cbFrameSize, wCurrentRate, bNeedACK);
			buf->rrv_time_b = vnt_rxtx_rsvtime_le16(pDevice, PK_TYPE_11B, cbFrameSize, pDevice->byTopCCKBasicRate, bNeedACK);

			s_vFillRTSHead(pDevice, byPktType, pvRTS, cbFrameSize, bNeedACK, bDisCRC, psEthHeader, wCurrentRate, byFBOption);
		} else {/* RTS_needless, PCF mode */
			struct vnt_rrv_time_cts *buf = pvRrvTime;

			buf->rrv_time_a = vnt_rxtx_rsvtime_le16(pDevice, byPktType, cbFrameSize, wCurrentRate, bNeedACK);
			buf->rrv_time_b = vnt_rxtx_rsvtime_le16(pDevice, PK_TYPE_11B, cbFrameSize, pDevice->byTopCCKBasicRate, bNeedACK);
			buf->cts_rrv_time_ba = s_uGetRTSCTSRsvTime(pDevice, 3, byPktType, cbFrameSize, wCurrentRate);

			/* Fill CTS */
			s_vFillCTSHead(pDevice, uDMAIdx, byPktType, pvCTS, cbFrameSize, bNeedACK, bDisCRC, wCurrentRate, byFBOption);
		}
	} else if (byPktType == PK_TYPE_11A) {
		if (pvRTS != NULL) {/* RTS_need, non PCF mode */
			struct vnt_rrv_time_ab *buf = pvRrvTime;

			buf->rts_rrv_time = s_uGetRTSCTSRsvTime(pDevice, 2, byPktType, cbFrameSize, wCurrentRate);
			buf->rrv_time = vnt_rxtx_rsvtime_le16(pDevice, byPktType, cbFrameSize, wCurrentRate, bNeedACK);

			/* Fill RTS */
			s_vFillRTSHead(pDevice, byPktType, pvRTS, cbFrameSize, bNeedACK, bDisCRC, psEthHeader, wCurrentRate, byFBOption);
		} else if (pvRTS == NULL) {/* RTS_needless, non PCF mode */
			struct vnt_rrv_time_ab *buf = pvRrvTime;

			buf->rrv_time = vnt_rxtx_rsvtime_le16(pDevice, PK_TYPE_11A, cbFrameSize, wCurrentRate, bNeedACK);
		}
	} else if (byPktType == PK_TYPE_11B) {
		if ((pvRTS != NULL)) {/* RTS_need, non PCF mode */
			struct vnt_rrv_time_ab *buf = pvRrvTime;

			buf->rts_rrv_time = s_uGetRTSCTSRsvTime(pDevice, 0, byPktType, cbFrameSize, wCurrentRate);
			buf->rrv_time = vnt_rxtx_rsvtime_le16(pDevice, PK_TYPE_11B, cbFrameSize, wCurrentRate, bNeedACK);

			/* Fill RTS */
			s_vFillRTSHead(pDevice, byPktType, pvRTS, cbFrameSize, bNeedACK, bDisCRC, psEthHeader, wCurrentRate, byFBOption);
		} else { /* RTS_needless, non PCF mode */
			struct vnt_rrv_time_ab *buf = pvRrvTime;

			buf->rrv_time = vnt_rxtx_rsvtime_le16(pDevice, PK_TYPE_11B, cbFrameSize, wCurrentRate, bNeedACK);
		}
	}
}

static unsigned int
s_cbFillTxBufHead(struct vnt_private *pDevice, unsigned char byPktType,
		  unsigned char *pbyTxBufferAddr,
		  unsigned int uDMAIdx, PSTxDesc pHeadTD,
		  unsigned int is_pspoll)
{
	PDEVICE_TD_INFO td_info = pHeadTD->pTDInfo;
	struct sk_buff *skb = td_info->skb;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct vnt_tx_fifo_head *tx_buffer_head =
			(struct vnt_tx_fifo_head *)td_info->buf;
	u16 fifo_ctl = le16_to_cpu(tx_buffer_head->fifo_ctl);
	unsigned int cbFrameSize;
	__le16 uDuration;
	unsigned char *pbyBuffer;
	unsigned int uLength = 0;
	unsigned int cbMICHDR = 0;
	unsigned int uMACfragNum = 1;
	unsigned int uPadding = 0;
	unsigned int cbReqCount = 0;
	bool bNeedACK = (bool)(fifo_ctl & FIFOCTL_NEEDACK);
	bool bRTS = (bool)(fifo_ctl & FIFOCTL_RTS);
	PSTxDesc       ptdCurr;
	unsigned int cbHeaderLength = 0;
	void *pvRrvTime;
	struct vnt_mic_hdr *pMICHDR;
	void *pvRTS;
	void *pvCTS;
	void *pvTxDataHd;
	unsigned short wTxBufSize;   /* FFinfo size */
	unsigned char byFBOption = AUTO_FB_NONE;

	pvRrvTime = pMICHDR = pvRTS = pvCTS = pvTxDataHd = NULL;

	cbFrameSize = skb->len + 4;

	if (info->control.hw_key) {
		switch (info->control.hw_key->cipher) {
		case WLAN_CIPHER_SUITE_CCMP:
			cbMICHDR = sizeof(struct vnt_mic_hdr);
		default:
			break;
		}

		cbFrameSize += info->control.hw_key->icv_len;

		if (pDevice->byLocalID > REV_ID_VT3253_A1) {
			/* MAC Header should be padding 0 to DW alignment. */
			uPadding = 4 - (ieee80211_get_hdrlen_from_skb(skb) % 4);
			uPadding %= 4;
		}
	}

	/*
	* Use for AUTO FALL BACK
	*/
	if (fifo_ctl & FIFOCTL_AUTO_FB_0)
		byFBOption = AUTO_FB_0;
	else if (fifo_ctl & FIFOCTL_AUTO_FB_1)
		byFBOption = AUTO_FB_1;


	/* Set RrvTime/RTS/CTS Buffer */
	wTxBufSize = sizeof(STxBufHead);
	if (byPktType == PK_TYPE_11GB || byPktType == PK_TYPE_11GA) {/* 802.11g packet */

		if (byFBOption == AUTO_FB_NONE) {
			if (bRTS) {/* RTS_need */
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_rts));
				pvRTS = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_rts) + cbMICHDR);
				pvCTS = NULL;
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_rts) +
							cbMICHDR + sizeof(struct vnt_rts_g));
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_rts) +
							cbMICHDR + sizeof(struct vnt_rts_g) +
							sizeof(struct vnt_tx_datahead_g);
			} else { /* RTS_needless */
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
			/* Auto Fall Back */
			if (bRTS) {/* RTS_need */
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_rts));
				pvRTS = (void *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_rts) + cbMICHDR);
				pvCTS = NULL;
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_rts) +
					cbMICHDR + sizeof(struct vnt_rts_g_fb));
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_rts) +
					cbMICHDR + sizeof(struct vnt_rts_g_fb) + sizeof(struct vnt_tx_datahead_g_fb);
			} else { /* RTS_needless */
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_cts));
				pvRTS = NULL;
				pvCTS = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_cts) + cbMICHDR);
				pvTxDataHd = (void  *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_cts) +
					cbMICHDR + sizeof(struct vnt_cts_fb));
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_cts) +
					cbMICHDR + sizeof(struct vnt_cts_fb) + sizeof(struct vnt_tx_datahead_g_fb);
			}
		} /* Auto Fall Back */
	} else {/* 802.11a/b packet */

		if (byFBOption == AUTO_FB_NONE) {
			if (bRTS) {
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab));
				pvRTS = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab) + cbMICHDR);
				pvCTS = NULL;
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize +
					sizeof(struct vnt_rrv_time_ab) + cbMICHDR + sizeof(struct vnt_rts_ab));
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_ab) +
					cbMICHDR + sizeof(struct vnt_rts_ab) + sizeof(struct vnt_tx_datahead_ab);
			} else { /* RTS_needless, need MICHDR */
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab));
				pvRTS = NULL;
				pvCTS = NULL;
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab) + cbMICHDR);
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_ab) +
					cbMICHDR + sizeof(struct vnt_tx_datahead_ab);
			}
		} else {
			/* Auto Fall Back */
			if (bRTS) { /* RTS_need */
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *) (pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab));
				pvRTS = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab) + cbMICHDR);
				pvCTS = NULL;
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize +
					sizeof(struct vnt_rrv_time_ab) + cbMICHDR + sizeof(struct vnt_rts_a_fb));
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_ab) +
					cbMICHDR + sizeof(struct vnt_rts_a_fb) + sizeof(struct vnt_tx_datahead_a_fb);
			} else { /* RTS_needless */
				pvRrvTime = (void *)(pbyTxBufferAddr + wTxBufSize);
				pMICHDR = (struct vnt_mic_hdr *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab));
				pvRTS = NULL;
				pvCTS = NULL;
				pvTxDataHd = (void *)(pbyTxBufferAddr + wTxBufSize + sizeof(struct vnt_rrv_time_ab) + cbMICHDR);
				cbHeaderLength = wTxBufSize + sizeof(struct vnt_rrv_time_ab) +
					cbMICHDR + sizeof(struct vnt_tx_datahead_a_fb);
			}
		} /* Auto Fall Back */
	}

	td_info->mic_hdr = pMICHDR;

	memset((void *)(pbyTxBufferAddr + wTxBufSize), 0, (cbHeaderLength - wTxBufSize));

	/* Fill FIFO,RrvTime,RTS,and CTS */
	s_vGenerateTxParameter(pDevice, byPktType, tx_buffer_head, pvRrvTime, pvRTS, pvCTS,
			       cbFrameSize, bNeedACK, uDMAIdx, hdr, pDevice->wCurrentRate);
	/* Fill DataHead */
	uDuration = s_uFillDataHead(pDevice, byPktType, pvTxDataHd, cbFrameSize, uDMAIdx, bNeedACK,
				    0, 0, uMACfragNum, byFBOption, pDevice->wCurrentRate, is_pspoll);

	hdr->duration_id = uDuration;

	cbReqCount = cbHeaderLength + uPadding + skb->len;
	pbyBuffer = (unsigned char *)pHeadTD->pTDInfo->buf;
	uLength = cbHeaderLength + uPadding;

	/* Copy the Packet into a tx Buffer */
	memcpy((pbyBuffer + uLength), skb->data, skb->len);

	ptdCurr = (PSTxDesc)pHeadTD;

	ptdCurr->pTDInfo->dwReqCount = cbReqCount;
	ptdCurr->pTDInfo->dwHeaderLength = cbHeaderLength;
	ptdCurr->pTDInfo->skb_dma = ptdCurr->pTDInfo->buf_dma;

	return cbHeaderLength;
}

static void vnt_fill_txkey(struct ieee80211_hdr *hdr, u8 *key_buffer,
			   struct ieee80211_key_conf *tx_key,
			   struct sk_buff *skb,	u16 payload_len,
			   struct vnt_mic_hdr *mic_hdr)
{
	struct ieee80211_key_seq seq;
	u8 *iv = ((u8 *)hdr + ieee80211_get_hdrlen_from_skb(skb));

	/* strip header and icv len from payload */
	payload_len -= ieee80211_get_hdrlen_from_skb(skb);
	payload_len -= tx_key->icv_len;

	switch (tx_key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		memcpy(key_buffer, iv, 3);
		memcpy(key_buffer + 3, tx_key->key, tx_key->keylen);

		if (tx_key->keylen == WLAN_KEY_LEN_WEP40) {
			memcpy(key_buffer + 8, iv, 3);
			memcpy(key_buffer + 11,
			       tx_key->key, WLAN_KEY_LEN_WEP40);
		}

		break;
	case WLAN_CIPHER_SUITE_TKIP:
		ieee80211_get_tkip_p2k(tx_key, skb, key_buffer);

		break;
	case WLAN_CIPHER_SUITE_CCMP:

		if (!mic_hdr)
			return;

		mic_hdr->id = 0x59;
		mic_hdr->payload_len = cpu_to_be16(payload_len);
		ether_addr_copy(mic_hdr->mic_addr2, hdr->addr2);

		ieee80211_get_key_tx_seq(tx_key, &seq);

		memcpy(mic_hdr->ccmp_pn, seq.ccmp.pn, IEEE80211_CCMP_PN_LEN);

		if (ieee80211_has_a4(hdr->frame_control))
			mic_hdr->hlen = cpu_to_be16(28);
		else
			mic_hdr->hlen = cpu_to_be16(22);

		ether_addr_copy(mic_hdr->addr1, hdr->addr1);
		ether_addr_copy(mic_hdr->addr2, hdr->addr2);
		ether_addr_copy(mic_hdr->addr3, hdr->addr3);

		mic_hdr->frame_control = cpu_to_le16(
			le16_to_cpu(hdr->frame_control) & 0xc78f);
		mic_hdr->seq_ctrl = cpu_to_le16(
				le16_to_cpu(hdr->seq_ctrl) & 0xf);

		if (ieee80211_has_a4(hdr->frame_control))
			ether_addr_copy(mic_hdr->addr4, hdr->addr4);

		memcpy(key_buffer, tx_key->key, WLAN_KEY_LEN_CCMP);

		break;
	default:
		break;
	}
}

int vnt_generate_fifo_header(struct vnt_private *priv, u32 dma_idx,
			     PSTxDesc head_td, struct sk_buff *skb)
{
	PDEVICE_TD_INFO td_info = head_td->pTDInfo;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *tx_rate = &info->control.rates[0];
	struct ieee80211_rate *rate;
	struct ieee80211_key_conf *tx_key;
	struct ieee80211_hdr *hdr;
	struct vnt_tx_fifo_head *tx_buffer_head =
			(struct vnt_tx_fifo_head *)td_info->buf;
	u16 tx_body_size = skb->len, current_rate;
	u8 pkt_type;
	bool is_pspoll = false;

	memset(tx_buffer_head, 0, sizeof(*tx_buffer_head));

	hdr = (struct ieee80211_hdr *)(skb->data);

	rate = ieee80211_get_tx_rate(priv->hw, info);

	current_rate = rate->hw_value;
	if (priv->wCurrentRate != current_rate &&
			!(priv->hw->conf.flags & IEEE80211_CONF_OFFCHANNEL)) {
		priv->wCurrentRate = current_rate;

		RFbSetPower(priv, priv->wCurrentRate,
			    priv->hw->conf.chandef.chan->hw_value);
	}

	if (current_rate > RATE_11M) {
		if (info->band == IEEE80211_BAND_5GHZ) {
			pkt_type = PK_TYPE_11A;
		} else {
			if (tx_rate->flags & IEEE80211_TX_RC_USE_CTS_PROTECT)
				pkt_type = PK_TYPE_11GB;
			else
				pkt_type = PK_TYPE_11GA;
		}
	} else {
		pkt_type = PK_TYPE_11B;
	}

	/*Set fifo controls */
	if (pkt_type == PK_TYPE_11A)
		tx_buffer_head->fifo_ctl = 0;
	else if (pkt_type == PK_TYPE_11B)
		tx_buffer_head->fifo_ctl = cpu_to_le16(FIFOCTL_11B);
	else if (pkt_type == PK_TYPE_11GB)
		tx_buffer_head->fifo_ctl = cpu_to_le16(FIFOCTL_11GB);
	else if (pkt_type == PK_TYPE_11GA)
		tx_buffer_head->fifo_ctl = cpu_to_le16(FIFOCTL_11GA);

	/* generate interrupt */
	tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_GENINT);

	if (!ieee80211_is_data(hdr->frame_control)) {
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_TMOEN);
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_ISDMA0);
		tx_buffer_head->time_stamp =
			cpu_to_le16(DEFAULT_MGN_LIFETIME_RES_64us);
	} else {
		tx_buffer_head->time_stamp =
			cpu_to_le16(DEFAULT_MSDU_LIFETIME_RES_64us);
	}

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_NEEDACK);

	if (ieee80211_has_retry(hdr->frame_control))
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_LRETRY);

	if (tx_rate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
		priv->byPreambleType = PREAMBLE_SHORT;
	else
		priv->byPreambleType = PREAMBLE_LONG;

	if (tx_rate->flags & IEEE80211_TX_RC_USE_RTS_CTS)
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_RTS);

	if (ieee80211_has_a4(hdr->frame_control)) {
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_LHEAD);
		priv->bLongHeader = true;
	}

	if (info->flags & IEEE80211_TX_CTL_NO_PS_BUFFER)
		is_pspoll = true;

	tx_buffer_head->frag_ctl =
			cpu_to_le16(ieee80211_get_hdrlen_from_skb(skb) << 10);

	if (info->control.hw_key) {
		tx_key = info->control.hw_key;

		switch (info->control.hw_key->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			tx_buffer_head->frag_ctl |= cpu_to_le16(FRAGCTL_LEGACY);
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			tx_buffer_head->frag_ctl |= cpu_to_le16(FRAGCTL_TKIP);
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			tx_buffer_head->frag_ctl |= cpu_to_le16(FRAGCTL_AES);
		default:
			break;
		}
	}

	tx_buffer_head->current_rate = cpu_to_le16(current_rate);

	/* legacy rates TODO use ieee80211_tx_rate */
	if (current_rate >= RATE_18M && ieee80211_is_data(hdr->frame_control)) {
		if (priv->byAutoFBCtrl == AUTO_FB_0)
			tx_buffer_head->fifo_ctl |=
						cpu_to_le16(FIFOCTL_AUTO_FB_0);
		else if (priv->byAutoFBCtrl == AUTO_FB_1)
			tx_buffer_head->fifo_ctl |=
						cpu_to_le16(FIFOCTL_AUTO_FB_1);

	}

	tx_buffer_head->frag_ctl |= cpu_to_le16(FRAGCTL_NONFRAG);

	s_cbFillTxBufHead(priv, pkt_type, (u8 *)tx_buffer_head,
			  dma_idx, head_td, is_pspoll);

	if (info->control.hw_key) {
		tx_key = info->control.hw_key;
		if (tx_key->keylen > 0)
			vnt_fill_txkey(hdr, tx_buffer_head->tx_key,
				tx_key, skb, tx_body_size, td_info->mic_hdr);
	}

	return 0;
}

static int vnt_beacon_xmit(struct vnt_private *priv,
			   struct sk_buff *skb)
{
	struct vnt_tx_short_buf_head *short_head =
		(struct vnt_tx_short_buf_head *)priv->tx_beacon_bufs;
	struct ieee80211_mgmt *mgmt_hdr = (struct ieee80211_mgmt *)
				(priv->tx_beacon_bufs + sizeof(*short_head));
	struct ieee80211_tx_info *info;
	u32 frame_size = skb->len + 4;
	u16 current_rate;

	memset(priv->tx_beacon_bufs, 0, sizeof(*short_head));

	if (priv->byBBType == BB_TYPE_11A) {
		current_rate = RATE_6M;

		/* Get SignalField,ServiceField,Length */
		vnt_get_phy_field(priv, frame_size, current_rate,
				  PK_TYPE_11A, &short_head->ab);

		/* Get Duration and TimeStampOff */
		short_head->duration =
			cpu_to_le16((u16)s_uGetDataDuration(priv, DATADUR_B,
				    frame_size, PK_TYPE_11A, current_rate,
				    false, 0, 0, 1, AUTO_FB_NONE));

		short_head->time_stamp_off =
				vnt_time_stamp_off(priv, current_rate);
	} else {
		current_rate = RATE_1M;
		short_head->fifo_ctl |= cpu_to_le16(FIFOCTL_11B);

		/* Get SignalField,ServiceField,Length */
		vnt_get_phy_field(priv, frame_size, current_rate,
				  PK_TYPE_11B, &short_head->ab);

		/* Get Duration and TimeStampOff */
		short_head->duration =
			cpu_to_le16((u16)s_uGetDataDuration(priv, DATADUR_B,
				    frame_size, PK_TYPE_11B, current_rate,
				    false, 0, 0, 1, AUTO_FB_NONE));

		short_head->time_stamp_off =
			vnt_time_stamp_off(priv, current_rate);
	}

	short_head->fifo_ctl |= cpu_to_le16(FIFOCTL_GENINT);

	/* Copy Beacon */
	memcpy(mgmt_hdr, skb->data, skb->len);

	/* time stamp always 0 */
	mgmt_hdr->u.beacon.timestamp = 0;

	info = IEEE80211_SKB_CB(skb);
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)mgmt_hdr;

		hdr->duration_id = 0;
		hdr->seq_ctrl = cpu_to_le16(priv->wSeqCounter << 4);
	}

	priv->wSeqCounter++;
	if (priv->wSeqCounter > 0x0fff)
		priv->wSeqCounter = 0;

	priv->wBCNBufLen = sizeof(*short_head) + skb->len;

	MACvSetCurrBCNTxDescAddr(priv->PortOffset, priv->tx_beacon_dma);

	MACvSetCurrBCNLength(priv->PortOffset, priv->wBCNBufLen);
	/* Set auto Transmit on */
	MACvRegBitsOn(priv->PortOffset, MAC_REG_TCR, TCR_AUTOBCNTX);
	/* Poll Transmit the adapter */
	MACvTransmitBCN(priv->PortOffset);

	return 0;
}

int vnt_beacon_make(struct vnt_private *priv, struct ieee80211_vif *vif)
{
	struct sk_buff *beacon;

	beacon = ieee80211_beacon_get(priv->hw, vif);
	if (!beacon)
		return -ENOMEM;

	if (vnt_beacon_xmit(priv, beacon)) {
		ieee80211_free_txskb(priv->hw, beacon);
		return -ENODEV;
	}

	return 0;
}

int vnt_beacon_enable(struct vnt_private *priv, struct ieee80211_vif *vif,
		      struct ieee80211_bss_conf *conf)
{
	VNSvOutPortB(priv->PortOffset + MAC_REG_TFTCTL, TFTCTL_TSFCNTRST);

	VNSvOutPortB(priv->PortOffset + MAC_REG_TFTCTL, TFTCTL_TSFCNTREN);

	CARDvSetFirstNextTBTT(priv, conf->beacon_int);

	CARDbSetBeaconPeriod(priv, conf->beacon_int);

	return vnt_beacon_make(priv, vif);
}
