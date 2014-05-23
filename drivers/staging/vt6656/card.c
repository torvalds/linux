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
 * File: card.c
 * Purpose: Provide functions to setup NIC operation mode
 * Functions:
 *      s_vSafeResetTx - Rest Tx
 *      CARDvSetRSPINF - Set RSPINF
 *      vUpdateIFS - Update slotTime,SIFS,DIFS, and EIFS
 *      CARDvUpdateBasicTopRate - Update BasicTopRate
 *      CARDbAddBasicRate - Add to BasicRateSet
 *      CARDbSetBasicRate - Set Basic Tx Rate
 *      CARDbIsOFDMinBasicRate - Check if any OFDM rate is in BasicRateSet
 *      CARDvSetLoopbackMode - Set Loopback mode
 *      CARDbSoftwareReset - Sortware reset NIC
 *      CARDqGetTSFOffset - Calculate TSFOffset
 *      CARDbGetCurrentTSF - Read Current NIC TSF counter
 *      CARDqGetNextTBTT - Calculate Next Beacon TSF counter
 *      CARDvSetFirstNextTBTT - Set NIC Beacon time
 *      CARDvUpdateNextTBTT - Sync. NIC Beacon time
 *      CARDbRadioPowerOff - Turn Off NIC Radio Power
 *      CARDbRadioPowerOn - Turn On NIC Radio Power
 *      CARDbSetWEPMode - Set NIC Wep mode
 *      CARDbSetTxPower - Set NIC tx power
 *
 * Revision History:
 *      06-10-2003 Bryan YC Fan:  Re-write codes to support VT3253 spec.
 *      08-26-2003 Kyle Hsu:      Modify the definition type of dwIoBase.
 *      09-01-2003 Bryan YC Fan:  Add vUpdateIFS().
 *
 */

#include "device.h"
#include "tmacro.h"
#include "card.h"
#include "baseband.h"
#include "mac.h"
#include "desc.h"
#include "rf.h"
#include "power.h"
#include "key.h"
#include "rc4.h"
#include "country.h"
#include "datarate.h"
#include "control.h"

//static int          msglevel                =MSG_LEVEL_DEBUG;
static int          msglevel                =MSG_LEVEL_INFO;

//const u16 cwRXBCNTSFOff[MAX_RATE] =
//{17, 34, 96, 192, 34, 23, 17, 11, 8, 5, 4, 3};

static const u16 cwRXBCNTSFOff[MAX_RATE] =
{192, 96, 34, 17, 34, 23, 17, 11, 8, 5, 4, 3};

/*
 * Description: Set NIC media channel
 *
 * Parameters:
 *  In:
 *      pDevice             - The adapter to be set
 *      connection_channel  - Channel to be set
 *  Out:
 *      none
 */
void CARDbSetMediaChannel(struct vnt_private *priv, u32 connection_channel)
{

	if (priv->byBBType == BB_TYPE_11A) {
		if ((connection_channel < (CB_MAX_CHANNEL_24G + 1)) ||
					(connection_channel > CB_MAX_CHANNEL))
			connection_channel = (CB_MAX_CHANNEL_24G + 1);
	} else {
		if ((connection_channel > CB_MAX_CHANNEL_24G) ||
						(connection_channel == 0))
			connection_channel = 1;
	}

	/* clear NAV */
	MACvRegBitsOn(priv, MAC_REG_MACCR, MACCR_CLRNAV);

	/* Set Channel[7] = 0 to tell H/W channel is changing now. */
	MACvRegBitsOff(priv, MAC_REG_CHANNEL, 0xb0);

	CONTROLnsRequestOut(priv, MESSAGE_TYPE_SELECT_CHANNLE,
					connection_channel, 0, 0, NULL);

	if (priv->byBBType == BB_TYPE_11A) {
		priv->byCurPwr = 0xff;
		RFbRawSetPower(priv,
			priv->abyOFDMAPwrTbl[connection_channel-15], RATE_54M);
	} else if (priv->byBBType == BB_TYPE_11G) {
		priv->byCurPwr = 0xff;
		RFbRawSetPower(priv,
			priv->abyOFDMPwrTbl[connection_channel-1], RATE_54M);
	} else {
		priv->byCurPwr = 0xff;
		RFbRawSetPower(priv,
			priv->abyCCKPwrTbl[connection_channel-1], RATE_1M);
	}

	ControlvWriteByte(priv, MESSAGE_REQUEST_MACREG, MAC_REG_CHANNEL,
		(u8)(connection_channel|0x80));
}

/*
 * Description: Get CCK mode basic rate
 *
 * Parameters:
 *  In:
 *      pDevice             - The adapter to be set
 *      wRateIdx            - Receiving data rate
 *  Out:
 *      none
 *
 * Return Value: response Control frame rate
 *
 */
static u16 swGetCCKControlRate(struct vnt_private *pDevice, u16 wRateIdx)
{
	u16 ui = wRateIdx;

	while (ui > RATE_1M) {
		if (pDevice->wBasicRate & (1 << ui))
			return ui;
		ui--;
	}

	return RATE_1M;
}

/*
 * Description: Get OFDM mode basic rate
 *
 * Parameters:
 *  In:
 *      pDevice             - The adapter to be set
 *      wRateIdx            - Receiving data rate
 *  Out:
 *      none
 *
 * Return Value: response Control frame rate
 *
 */
static u16 swGetOFDMControlRate(struct vnt_private *pDevice, u16 wRateIdx)
{
	u16 ui = wRateIdx;

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"BASIC RATE: %X\n",
		pDevice->wBasicRate);

	if (!CARDbIsOFDMinBasicRate(pDevice)) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
			"swGetOFDMControlRate:(NO OFDM) %d\n", wRateIdx);
		if (wRateIdx > RATE_24M)
			wRateIdx = RATE_24M;
		return wRateIdx;
	}

	while (ui > RATE_11M) {
		if (pDevice->wBasicRate & (1 << ui)) {
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
				"swGetOFDMControlRate: %d\n", ui);
			return ui;
		}
		ui--;
	}

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"swGetOFDMControlRate: 6M\n");

	return RATE_24M;
}

/*
 * Description: Calculate TxRate and RsvTime fields for RSPINF in OFDM mode.
 *
 * Parameters:
 * In:
 *	rate	- Tx Rate
 *	bb_type	- Tx Packet type
 * Out:
 *	tx_rate	- pointer to RSPINF TxRate field
 *	rsv_time- pointer to RSPINF RsvTime field
 *
 * Return Value: none
 *
 */
void CARDvCalculateOFDMRParameter(u16 rate, u8 bb_type,
					u8 *tx_rate, u8 *rsv_time)
{

	switch (rate) {
	case RATE_6M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x9b;
			*rsv_time = 24;
		} else {
			*tx_rate = 0x8b;
			*rsv_time = 30;
		}
			break;
	case RATE_9M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x9f;
			*rsv_time = 16;
		} else {
			*tx_rate = 0x8f;
			*rsv_time = 22;
		}
		break;
	case RATE_12M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x9a;
			*rsv_time = 12;
		} else {
			*tx_rate = 0x8a;
			*rsv_time = 18;
		}
		break;
	case RATE_18M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x9e;
			*rsv_time = 8;
		} else {
			*tx_rate = 0x8e;
			*rsv_time = 14;
		}
		break;
	case RATE_36M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x9d;
			*rsv_time = 4;
		} else {
			*tx_rate = 0x8d;
			*rsv_time = 10;
		}
		break;
	case RATE_48M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x98;
			*rsv_time = 4;
		} else {
			*tx_rate = 0x88;
		*rsv_time = 10;
		}
		break;
	case RATE_54M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x9c;
			*rsv_time = 4;
		} else {
			*tx_rate = 0x8c;
			*rsv_time = 10;
		}
		break;
	case RATE_24M:
	default:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x99;
			*rsv_time = 8;
		} else {
			*tx_rate = 0x89;
			*rsv_time = 14;
		}
		break;
	}
}

/*
 * Description: Set RSPINF
 *
 * Parameters:
 *  In:
 *      pDevice             - The adapter to be set
 *  Out:
 *      none
 *
 * Return Value: None.
 *
 */

void CARDvSetRSPINF(struct vnt_private *priv, u8 bb_type)
{
	struct vnt_phy_field phy[4];
	u8 tx_rate[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0}; /* For OFDM */
	u8 rsv_time[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
	u8 data[34];
	int i;

	/*RSPINF_b_1*/
	BBvCalculateParameter(priv, 14,
		swGetCCKControlRate(priv, RATE_1M), PK_TYPE_11B, &phy[0]);

	/*RSPINF_b_2*/
	BBvCalculateParameter(priv, 14,
		swGetCCKControlRate(priv, RATE_2M), PK_TYPE_11B, &phy[1]);

	/*RSPINF_b_5*/
	BBvCalculateParameter(priv, 14,
		swGetCCKControlRate(priv, RATE_5M), PK_TYPE_11B, &phy[2]);

	/*RSPINF_b_11*/
	BBvCalculateParameter(priv, 14,
		swGetCCKControlRate(priv, RATE_11M), PK_TYPE_11B, &phy[3]);


	/*RSPINF_a_6*/
	CARDvCalculateOFDMRParameter(RATE_6M, bb_type,
						&tx_rate[0], &rsv_time[0]);

	/*RSPINF_a_9*/
	CARDvCalculateOFDMRParameter(RATE_9M, bb_type,
						&tx_rate[1], &rsv_time[1]);

	/*RSPINF_a_12*/
	CARDvCalculateOFDMRParameter(RATE_12M, bb_type,
						&tx_rate[2], &rsv_time[2]);

	/*RSPINF_a_18*/
	CARDvCalculateOFDMRParameter(RATE_18M, bb_type,
						&tx_rate[3], &rsv_time[3]);

	/*RSPINF_a_24*/
	CARDvCalculateOFDMRParameter(RATE_24M, bb_type,
						&tx_rate[4], &rsv_time[4]);

	/*RSPINF_a_36*/
	CARDvCalculateOFDMRParameter(swGetOFDMControlRate(priv, RATE_36M),
					bb_type, &tx_rate[5], &rsv_time[5]);

	/*RSPINF_a_48*/
	CARDvCalculateOFDMRParameter(swGetOFDMControlRate(priv, RATE_48M),
					bb_type, &tx_rate[6], &rsv_time[6]);

	/*RSPINF_a_54*/
	CARDvCalculateOFDMRParameter(swGetOFDMControlRate(priv, RATE_54M),
					bb_type, &tx_rate[7], &rsv_time[7]);

	/*RSPINF_a_72*/
	CARDvCalculateOFDMRParameter(swGetOFDMControlRate(priv, RATE_54M),
					bb_type, &tx_rate[8], &rsv_time[8]);

	put_unaligned(phy[0].len, (u16 *)&data[0]);
	data[2] = phy[0].signal;
	data[3] = phy[0].service;

	put_unaligned(phy[1].len, (u16 *)&data[4]);
	data[6] = phy[1].signal;
	data[7] = phy[1].service;

	put_unaligned(phy[2].len, (u16 *)&data[8]);
	data[10] = phy[2].signal;
	data[11] = phy[2].service;

	put_unaligned(phy[3].len, (u16 *)&data[12]);
	data[14] = phy[3].signal;
	data[15] = phy[3].service;

	for (i = 0; i < 9; i++) {
		data[16 + i * 2] = tx_rate[i];
		data[16 + i * 2 + 1] = rsv_time[i];
	}

	CONTROLnsRequestOut(priv, MESSAGE_TYPE_WRITE,
		MAC_REG_RSPINF_B_1, MESSAGE_REQUEST_MACREG, 34, &data[0]);
}

/*
 * Description: Update IFS
 *
 * Parameters:
 *  In:
 *	priv - The adapter to be set
 * Out:
 *	none
 *
 * Return Value: None.
 *
 */
void vUpdateIFS(struct vnt_private *priv)
{
	u8 max_min = 0;
	u8 data[4];

	if (priv->byPacketType == PK_TYPE_11A) {
		priv->uSlot = C_SLOT_SHORT;
		priv->uSIFS = C_SIFS_A;
		priv->uDIFS = C_SIFS_A + 2 * C_SLOT_SHORT;
		priv->uCwMin = C_CWMIN_A;
		max_min = 4;
	} else if (priv->byPacketType == PK_TYPE_11B) {
		priv->uSlot = C_SLOT_LONG;
		priv->uSIFS = C_SIFS_BG;
		priv->uDIFS = C_SIFS_BG + 2 * C_SLOT_LONG;
		priv->uCwMin = C_CWMIN_B;
		max_min = 5;
	} else {/* PK_TYPE_11GA & PK_TYPE_11GB */
		u8 rate = 0;
		bool ofdm_rate = false;
		unsigned int ii = 0;
		PWLAN_IE_SUPP_RATES item_rates = NULL;

		priv->uSIFS = C_SIFS_BG;

		if (priv->bShortSlotTime)
			priv->uSlot = C_SLOT_SHORT;
		else
			priv->uSlot = C_SLOT_LONG;

		priv->uDIFS = C_SIFS_BG + 2 * priv->uSlot;

		item_rates =
			(PWLAN_IE_SUPP_RATES)priv->vnt_mgmt.abyCurrSuppRates;

		for (ii = 0; ii < item_rates->len; ii++) {
			rate = (u8)(item_rates->abyRates[ii] & 0x7f);
			if (RATEwGetRateIdx(rate) > RATE_11M) {
				ofdm_rate = true;
				break;
			}
		}

		if (ofdm_rate == false) {
			item_rates = (PWLAN_IE_SUPP_RATES)priv->vnt_mgmt
				.abyCurrExtSuppRates;
			for (ii = 0; ii < item_rates->len; ii++) {
				rate = (u8)(item_rates->abyRates[ii] & 0x7f);
				if (RATEwGetRateIdx(rate) > RATE_11M) {
					ofdm_rate = true;
					break;
				}
			}
		}

		if (ofdm_rate == true) {
			priv->uCwMin = C_CWMIN_A;
			max_min = 4;
		} else {
			priv->uCwMin = C_CWMIN_B;
			max_min = 5;
			}
	}

	priv->uCwMax = C_CWMAX;
	priv->uEIFS = C_EIFS;

	data[0] = (u8)priv->uSIFS;
	data[1] = (u8)priv->uDIFS;
	data[2] = (u8)priv->uEIFS;
	data[3] = (u8)priv->uSlot;

	CONTROLnsRequestOut(priv, MESSAGE_TYPE_WRITE, MAC_REG_SIFS,
		MESSAGE_REQUEST_MACREG, 4, &data[0]);

	max_min |= 0xa0;

	CONTROLnsRequestOut(priv, MESSAGE_TYPE_WRITE, MAC_REG_CWMAXMIN0,
		MESSAGE_REQUEST_MACREG, 1, &max_min);
}

void CARDvUpdateBasicTopRate(struct vnt_private *priv)
{
	u8 top_ofdm = RATE_24M, top_cck = RATE_1M;
	u8 i;

	/*Determines the highest basic rate.*/
	for (i = RATE_54M; i >= RATE_6M; i--) {
		if (priv->wBasicRate & (u16)(1 << i)) {
			top_ofdm = i;
			break;
		}
	}

	priv->byTopOFDMBasicRate = top_ofdm;

	for (i = RATE_11M;; i--) {
		if (priv->wBasicRate & (u16)(1 << i)) {
			top_cck = i;
			break;
		}
		if (i == RATE_1M)
			break;
	}

	priv->byTopCCKBasicRate = top_cck;
 }

/*
 * Description: Set NIC Tx Basic Rate
 *
 * Parameters:
 *  In:
 *      pDevice         - The adapter to be set
 *      wBasicRate      - Basic Rate to be set
 *  Out:
 *      none
 *
 * Return Value: true if succeeded; false if failed.
 *
 */
void CARDbAddBasicRate(struct vnt_private *priv, u16 rate_idx)
{

	priv->wBasicRate |= (1 << rate_idx);

	/*Determines the highest basic rate.*/
	CARDvUpdateBasicTopRate(priv);
}

int CARDbIsOFDMinBasicRate(struct vnt_private *pDevice)
{
	int ii;

    for (ii = RATE_54M; ii >= RATE_6M; ii --) {
        if ((pDevice->wBasicRate) & ((u16)(1<<ii)))
            return true;
    }
    return false;
}

u8 CARDbyGetPktType(struct vnt_private *pDevice)
{

    if (pDevice->byBBType == BB_TYPE_11A || pDevice->byBBType == BB_TYPE_11B) {
        return (u8)pDevice->byBBType;
    }
    else if (CARDbIsOFDMinBasicRate(pDevice)) {
        return PK_TYPE_11GA;
    }
    else {
        return PK_TYPE_11GB;
    }
}

/*
 * Description: Calculate TSF offset of two TSF input
 *              Get TSF Offset from RxBCN's TSF and local TSF
 *
 * Parameters:
 *  In:
 *      pDevice         - The adapter to be sync.
 *      qwTSF1          - Rx BCN's TSF
 *      qwTSF2          - Local TSF
 *  Out:
 *      none
 *
 * Return Value: TSF Offset value
 *
 */
u64 CARDqGetTSFOffset(u8 byRxRate, u64 qwTSF1, u64 qwTSF2)
{
	u64 qwTSFOffset = 0;
	u16 wRxBcnTSFOffst = 0;

	wRxBcnTSFOffst = cwRXBCNTSFOff[byRxRate % MAX_RATE];

	qwTSF2 += (u64)wRxBcnTSFOffst;

	qwTSFOffset = qwTSF1 - qwTSF2;

	return qwTSFOffset;
}

/*
 * Description: Sync. TSF counter to BSS
 *              Get TSF offset and write to HW
 *
 * Parameters:
 *  In:
 *      pDevice         - The adapter to be sync.
 *      qwBSSTimestamp  - Rx BCN's TSF
 *      qwLocalTSF      - Local TSF
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void CARDvAdjustTSF(struct vnt_private *pDevice, u8 byRxRate,
		u64 qwBSSTimestamp, u64 qwLocalTSF)
{
	u64 qwTSFOffset = 0;
	u8 pbyData[8];

    qwTSFOffset = CARDqGetTSFOffset(byRxRate, qwBSSTimestamp, qwLocalTSF);
    // adjust TSF
    // HW's TSF add TSF Offset reg

	pbyData[0] = (u8)qwTSFOffset;
	pbyData[1] = (u8)(qwTSFOffset >> 8);
	pbyData[2] = (u8)(qwTSFOffset >> 16);
	pbyData[3] = (u8)(qwTSFOffset >> 24);
	pbyData[4] = (u8)(qwTSFOffset >> 32);
	pbyData[5] = (u8)(qwTSFOffset >> 40);
	pbyData[6] = (u8)(qwTSFOffset >> 48);
	pbyData[7] = (u8)(qwTSFOffset >> 56);

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_SET_TSFTBTT,
                        MESSAGE_REQUEST_TSF,
                        0,
                        8,
                        pbyData
                        );

}
/*
 * Description: Read NIC TSF counter
 *              Get local TSF counter
 *
 * Parameters:
 *  In:
 *      pDevice         - The adapter to be read
 *  Out:
 *      qwCurrTSF       - Current TSF counter
 *
 * Return Value: true if success; otherwise false
 *
 */
bool CARDbGetCurrentTSF(struct vnt_private *pDevice, u64 *pqwCurrTSF)
{

	*pqwCurrTSF = pDevice->qwCurrTSF;

	return true;
}

/*
 * Description: Clear NIC TSF counter
 *              Clear local TSF counter
 *
 * Parameters:
 *  In:
 *      pDevice         - The adapter to be read
 *
 * Return Value: true if success; otherwise false
 *
 */
bool CARDbClearCurrentTSF(struct vnt_private *pDevice)
{

	MACvRegBitsOn(pDevice, MAC_REG_TFTCTL, TFTCTL_TSFCNTRST);

	pDevice->qwCurrTSF = 0;

	return true;
}

/*
 * Description: Read NIC TSF counter
 *              Get NEXTTBTT from adjusted TSF and Beacon Interval
 *
 * Parameters:
 *  In:
 *      qwTSF           - Current TSF counter
 *      wbeaconInterval - Beacon Interval
 *  Out:
 *      qwCurrTSF       - Current TSF counter
 *
 * Return Value: TSF value of next Beacon
 *
 */
u64 CARDqGetNextTBTT(u64 qwTSF, u16 wBeaconInterval)
{
	u32 uBeaconInterval;

	uBeaconInterval = wBeaconInterval * 1024;

	/* Next TBTT =
	*	((local_current_TSF / beacon_interval) + 1) * beacon_interval
	*/
	if (uBeaconInterval) {
		do_div(qwTSF, uBeaconInterval);
		qwTSF += 1;
		qwTSF *= uBeaconInterval;
	}

	return qwTSF;
}

/*
 * Description: Set NIC TSF counter for first Beacon time
 *              Get NEXTTBTT from adjusted TSF and Beacon Interval
 *
 * Parameters:
 *  In:
 *      dwIoBase        - IO Base
 *      wBeaconInterval - Beacon Interval
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void CARDvSetFirstNextTBTT(struct vnt_private *pDevice, u16 wBeaconInterval)
{
	u64 qwNextTBTT = 0;
	u8 pbyData[8];

	CARDbClearCurrentTSF(pDevice);
    //CARDbGetCurrentTSF(pDevice, &qwNextTBTT); //Get Local TSF counter
	qwNextTBTT = CARDqGetNextTBTT(qwNextTBTT, wBeaconInterval);
    // Set NextTBTT

	pbyData[0] = (u8)qwNextTBTT;
	pbyData[1] = (u8)(qwNextTBTT >> 8);
	pbyData[2] = (u8)(qwNextTBTT >> 16);
	pbyData[3] = (u8)(qwNextTBTT >> 24);
	pbyData[4] = (u8)(qwNextTBTT >> 32);
	pbyData[5] = (u8)(qwNextTBTT >> 40);
	pbyData[6] = (u8)(qwNextTBTT >> 48);
	pbyData[7] = (u8)(qwNextTBTT >> 56);

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_SET_TSFTBTT,
                        MESSAGE_REQUEST_TBTT,
                        0,
                        8,
                        pbyData
                        );

    return;
}

/*
 * Description: Sync NIC TSF counter for Beacon time
 *              Get NEXTTBTT and write to HW
 *
 * Parameters:
 *  In:
 *      pDevice         - The adapter to be set
 *      qwTSF           - Current TSF counter
 *      wBeaconInterval - Beacon Interval
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void CARDvUpdateNextTBTT(struct vnt_private *pDevice, u64 qwTSF,
			u16 wBeaconInterval)
{
	u8 pbyData[8];

    qwTSF = CARDqGetNextTBTT(qwTSF, wBeaconInterval);

    // Set NextTBTT

	pbyData[0] = (u8)qwTSF;
	pbyData[1] = (u8)(qwTSF >> 8);
	pbyData[2] = (u8)(qwTSF >> 16);
	pbyData[3] = (u8)(qwTSF >> 24);
	pbyData[4] = (u8)(qwTSF >> 32);
	pbyData[5] = (u8)(qwTSF >> 40);
	pbyData[6] = (u8)(qwTSF >> 48);
	pbyData[7] = (u8)(qwTSF >> 56);

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_SET_TSFTBTT,
                        MESSAGE_REQUEST_TBTT,
                        0,
                        8,
                        pbyData
                        );

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
		"Card:Update Next TBTT[%8lx]\n", (unsigned long)qwTSF);

    return;
}

/*
 * Description: Turn off Radio power
 *
 * Parameters:
 *  In:
 *      pDevice         - The adapter to be turned off
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
int CARDbRadioPowerOff(struct vnt_private *pDevice)
{
	int bResult = true;

    //if (pDevice->bRadioOff == true)
    //    return true;

    pDevice->bRadioOff = true;

    switch (pDevice->byRFType) {
        case RF_AL2230:
        case RF_AL2230S:
        case RF_AIROHA7230:
        case RF_VT3226:     //RobertYu:20051111
        case RF_VT3226D0:
        case RF_VT3342A0:   //RobertYu:20060609
            MACvRegBitsOff(pDevice, MAC_REG_SOFTPWRCTL, (SOFTPWRCTL_SWPE2 | SOFTPWRCTL_SWPE3));
            break;
    }

    MACvRegBitsOff(pDevice, MAC_REG_HOSTCR, HOSTCR_RXON);

    BBvSetDeepSleep(pDevice);

    return bResult;
}

/*
 * Description: Turn on Radio power
 *
 * Parameters:
 *  In:
 *      pDevice         - The adapter to be turned on
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
int CARDbRadioPowerOn(struct vnt_private *pDevice)
{
	int bResult = true;

    if ((pDevice->bHWRadioOff == true) || (pDevice->bRadioControlOff == true)) {
        return false;
    }

    //if (pDevice->bRadioOff == false)
    //    return true;

    pDevice->bRadioOff = false;

    BBvExitDeepSleep(pDevice);

    MACvRegBitsOn(pDevice, MAC_REG_HOSTCR, HOSTCR_RXON);

    switch (pDevice->byRFType) {
        case RF_AL2230:
        case RF_AL2230S:
        case RF_AIROHA7230:
        case RF_VT3226:     //RobertYu:20051111
        case RF_VT3226D0:
        case RF_VT3342A0:   //RobertYu:20060609
            MACvRegBitsOn(pDevice, MAC_REG_SOFTPWRCTL, (SOFTPWRCTL_SWPE2 | SOFTPWRCTL_SWPE3));
            break;
    }

    return bResult;
}

void CARDvSetBSSMode(struct vnt_private *pDevice)
{
    // Set BB and packet type at the same time.//{{RobertYu:20050222, AL7230 have two TX PA output, only connet to b/g now
    // so in 11a mode need to set the MAC Reg0x4C to 11b/g mode to turn on PA
    if( (pDevice->byRFType == RF_AIROHA7230 ) && (pDevice->byBBType == BB_TYPE_11A) )
    {
        MACvSetBBType(pDevice, BB_TYPE_11G);
    }
    else
    {
        MACvSetBBType(pDevice, pDevice->byBBType);
    }
    pDevice->byPacketType = CARDbyGetPktType(pDevice);

    if (pDevice->byBBType == BB_TYPE_11A) {
        ControlvWriteByte(pDevice, MESSAGE_REQUEST_BBREG, 0x88, 0x03);
    } else if (pDevice->byBBType == BB_TYPE_11B) {
        ControlvWriteByte(pDevice, MESSAGE_REQUEST_BBREG, 0x88, 0x02);
    } else if (pDevice->byBBType == BB_TYPE_11G) {
        ControlvWriteByte(pDevice, MESSAGE_REQUEST_BBREG, 0x88, 0x08);
    }

    vUpdateIFS(pDevice);
    CARDvSetRSPINF(pDevice, (u8)pDevice->byBBType);

    if ( pDevice->byBBType == BB_TYPE_11A ) {
        //request by Jack 2005-04-26
        if (pDevice->byRFType == RF_AIROHA7230) {
            pDevice->abyBBVGA[0] = 0x20;
            ControlvWriteByte(pDevice, MESSAGE_REQUEST_BBREG, 0xE7, pDevice->abyBBVGA[0]);
        }
        pDevice->abyBBVGA[2] = 0x10;
        pDevice->abyBBVGA[3] = 0x10;
    } else {
        //request by Jack 2005-04-26
        if (pDevice->byRFType == RF_AIROHA7230) {
            pDevice->abyBBVGA[0] = 0x1C;
            ControlvWriteByte(pDevice, MESSAGE_REQUEST_BBREG, 0xE7, pDevice->abyBBVGA[0]);
        }
        pDevice->abyBBVGA[2] = 0x0;
        pDevice->abyBBVGA[3] = 0x0;
    }
}
