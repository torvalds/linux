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
#include "rndis.h"
#include "control.h"

/*---------------------  Static Definitions -------------------------*/

//static int          msglevel                =MSG_LEVEL_DEBUG;
static int          msglevel                =MSG_LEVEL_INFO;


/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/
//const WORD cwRXBCNTSFOff[MAX_RATE] =
//{17, 34, 96, 192, 34, 23, 17, 11, 8, 5, 4, 3};

const WORD cwRXBCNTSFOff[MAX_RATE] =
{192, 96, 34, 17, 34, 23, 17, 11, 8, 5, 4, 3};

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/
/*
 * Description: Set NIC media channel
 *
 * Parameters:
 *  In:
 *      pDevice             - The adapter to be set
 *      uConnectionChannel  - Channel to be set
 *  Out:
 *      none
 */
void CARDbSetMediaChannel(struct vnt_private *pDevice, u32 uConnectionChannel)
{

    if (pDevice->byBBType == BB_TYPE_11A) { // 15 ~ 38
        if ((uConnectionChannel < (CB_MAX_CHANNEL_24G+1)) || (uConnectionChannel > CB_MAX_CHANNEL))
            uConnectionChannel = (CB_MAX_CHANNEL_24G+1);
    } else {
        if ((uConnectionChannel > CB_MAX_CHANNEL_24G) || (uConnectionChannel == 0)) // 1 ~ 14
            uConnectionChannel = 1;
    }

    // clear NAV
    MACvRegBitsOn(pDevice, MAC_REG_MACCR, MACCR_CLRNAV);

    // Set Channel[7] = 0 to tell H/W channel is changing now.
    MACvRegBitsOff(pDevice, MAC_REG_CHANNEL, 0x80);

    //if (pMgmt->uCurrChannel == uConnectionChannel)
    //    return bResult;

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_SELECT_CHANNLE,
                        (WORD) uConnectionChannel,
                        0,
                        0,
                        NULL
                        );

    //{{ RobertYu: 20041202
    //// TX_PE will reserve 3 us for MAX2829 A mode only, it is for better TX throughput

    if (pDevice->byBBType == BB_TYPE_11A) {
        pDevice->byCurPwr = 0xFF;
        RFbRawSetPower(pDevice, pDevice->abyOFDMAPwrTbl[uConnectionChannel-15], RATE_54M);
    } else if (pDevice->byBBType == BB_TYPE_11G) {
        pDevice->byCurPwr = 0xFF;
        RFbRawSetPower(pDevice, pDevice->abyOFDMPwrTbl[uConnectionChannel-1], RATE_54M);
    } else {
        pDevice->byCurPwr = 0xFF;
        RFbRawSetPower(pDevice, pDevice->abyCCKPwrTbl[uConnectionChannel-1], RATE_1M);
    }
    ControlvWriteByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_CHANNEL,(u8)(uConnectionChannel|0x80));
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
 *  In:
 *      wRate           - Tx Rate
 *      byPktType       - Tx Packet type
 *  Out:
 *      pbyTxRate       - pointer to RSPINF TxRate field
 *      pbyRsvTime      - pointer to RSPINF RsvTime field
 *
 * Return Value: none
 *
 */
void
CARDvCalculateOFDMRParameter (
      WORD wRate,
      u8 byBBType,
     u8 * pbyTxRate,
     u8 * pbyRsvTime
    )
{
    switch (wRate) {
    case RATE_6M :
        if (byBBType == BB_TYPE_11A) {//5GHZ
            *pbyTxRate = 0x9B;
            *pbyRsvTime = 24;
        }
        else {
            *pbyTxRate = 0x8B;
            *pbyRsvTime = 30;
        }
        break;

    case RATE_9M :
        if (byBBType == BB_TYPE_11A) {//5GHZ
            *pbyTxRate = 0x9F;
            *pbyRsvTime = 16;
        }
        else {
            *pbyTxRate = 0x8F;
            *pbyRsvTime = 22;
        }
        break;

   case RATE_12M :
        if (byBBType == BB_TYPE_11A) {//5GHZ
            *pbyTxRate = 0x9A;
            *pbyRsvTime = 12;
        }
        else {
            *pbyTxRate = 0x8A;
            *pbyRsvTime = 18;
        }
        break;

   case RATE_18M :
        if (byBBType == BB_TYPE_11A) {//5GHZ
            *pbyTxRate = 0x9E;
            *pbyRsvTime = 8;
        }
        else {
            *pbyTxRate = 0x8E;
            *pbyRsvTime = 14;
        }
        break;

    case RATE_36M :
        if (byBBType == BB_TYPE_11A) {//5GHZ
            *pbyTxRate = 0x9D;
            *pbyRsvTime = 4;
        }
        else {
            *pbyTxRate = 0x8D;
            *pbyRsvTime = 10;
        }
        break;

    case RATE_48M :
        if (byBBType == BB_TYPE_11A) {//5GHZ
            *pbyTxRate = 0x98;
            *pbyRsvTime = 4;
        }
        else {
            *pbyTxRate = 0x88;
            *pbyRsvTime = 10;
        }
        break;

    case RATE_54M :
        if (byBBType == BB_TYPE_11A) {//5GHZ
            *pbyTxRate = 0x9C;
            *pbyRsvTime = 4;
        }
        else {
            *pbyTxRate = 0x8C;
            *pbyRsvTime = 10;
        }
        break;

    case RATE_24M :
    default :
        if (byBBType == BB_TYPE_11A) {//5GHZ
            *pbyTxRate = 0x99;
            *pbyRsvTime = 8;
        }
        else {
            *pbyTxRate = 0x89;
            *pbyRsvTime = 14;
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
void CARDvSetRSPINF(struct vnt_private *pDevice, u8 byBBType)
{
	u8 abyServ[4] = {0, 0, 0, 0}; /* For CCK */
	u8 abySignal[4] = {0, 0, 0, 0};
	u16 awLen[4] = {0, 0, 0, 0};
	u8 abyTxRate[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0}; /* For OFDM */
	u8 abyRsvTime[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
	u8 abyData[34];
	int i;

    //RSPINF_b_1
    BBvCalculateParameter(pDevice,
                         14,
                         swGetCCKControlRate(pDevice, RATE_1M),
                         PK_TYPE_11B,
                         &awLen[0],
                         &abyServ[0],
                         &abySignal[0]
    );

    ///RSPINF_b_2
    BBvCalculateParameter(pDevice,
                         14,
                         swGetCCKControlRate(pDevice, RATE_2M),
                         PK_TYPE_11B,
                         &awLen[1],
                         &abyServ[1],
                         &abySignal[1]
    );

    //RSPINF_b_5
    BBvCalculateParameter(pDevice,
                         14,
                         swGetCCKControlRate(pDevice, RATE_5M),
                         PK_TYPE_11B,
                         &awLen[2],
                         &abyServ[2],
                         &abySignal[2]
    );

    //RSPINF_b_11
    BBvCalculateParameter(pDevice,
                         14,
                         swGetCCKControlRate(pDevice, RATE_11M),
                         PK_TYPE_11B,
                         &awLen[3],
                         &abyServ[3],
                         &abySignal[3]
    );

    //RSPINF_a_6
    CARDvCalculateOFDMRParameter (RATE_6M,
                                 byBBType,
                                 &abyTxRate[0],
                                 &abyRsvTime[0]);

    //RSPINF_a_9
    CARDvCalculateOFDMRParameter (RATE_9M,
                                 byBBType,
                                 &abyTxRate[1],
                                 &abyRsvTime[1]);

    //RSPINF_a_12
    CARDvCalculateOFDMRParameter (RATE_12M,
                                 byBBType,
                                 &abyTxRate[2],
                                 &abyRsvTime[2]);

    //RSPINF_a_18
    CARDvCalculateOFDMRParameter (RATE_18M,
                                 byBBType,
                                 &abyTxRate[3],
                                 &abyRsvTime[3]);

    //RSPINF_a_24
    CARDvCalculateOFDMRParameter (RATE_24M,
                                 byBBType,
                                 &abyTxRate[4],
                                 &abyRsvTime[4]);

    //RSPINF_a_36
    CARDvCalculateOFDMRParameter (swGetOFDMControlRate(pDevice, RATE_36M),
                                 byBBType,
                                 &abyTxRate[5],
                                 &abyRsvTime[5]);

    //RSPINF_a_48
    CARDvCalculateOFDMRParameter (swGetOFDMControlRate(pDevice, RATE_48M),
                                 byBBType,
                                 &abyTxRate[6],
                                 &abyRsvTime[6]);

    //RSPINF_a_54
    CARDvCalculateOFDMRParameter (swGetOFDMControlRate(pDevice, RATE_54M),
                                 byBBType,
                                 &abyTxRate[7],
                                 &abyRsvTime[7]);

    //RSPINF_a_72
    CARDvCalculateOFDMRParameter (swGetOFDMControlRate(pDevice, RATE_54M),
                                 byBBType,
                                 &abyTxRate[8],
                                 &abyRsvTime[8]);

    abyData[0] = (u8)(awLen[0]&0xFF);
    abyData[1] = (u8)(awLen[0]>>8);
    abyData[2] = abySignal[0];
    abyData[3] = abyServ[0];

    abyData[4] = (u8)(awLen[1]&0xFF);
    abyData[5] = (u8)(awLen[1]>>8);
    abyData[6] = abySignal[1];
    abyData[7] = abyServ[1];

    abyData[8] = (u8)(awLen[2]&0xFF);
    abyData[9] = (u8)(awLen[2]>>8);
    abyData[10] = abySignal[2];
    abyData[11] = abyServ[2];

    abyData[12] = (u8)(awLen[3]&0xFF);
    abyData[13] = (u8)(awLen[3]>>8);
    abyData[14] = abySignal[3];
    abyData[15] = abyServ[3];

    for (i = 0; i < 9; i++) {
	abyData[16+i*2] = abyTxRate[i];
	abyData[16+i*2+1] = abyRsvTime[i];
    }

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE,
                        MAC_REG_RSPINF_B_1,
                        MESSAGE_REQUEST_MACREG,
                        34,
                        &abyData[0]);

}

/*
 * Description: Update IFS
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
void vUpdateIFS(struct vnt_private *pDevice)
{
	u8 byMaxMin = 0;
	u8 byData[4];

    if (pDevice->byPacketType==PK_TYPE_11A) {//0000 0000 0000 0000,11a
        pDevice->uSlot = C_SLOT_SHORT;
        pDevice->uSIFS = C_SIFS_A;
        pDevice->uDIFS = C_SIFS_A + 2*C_SLOT_SHORT;
        pDevice->uCwMin = C_CWMIN_A;
        byMaxMin = 4;
    }
    else if (pDevice->byPacketType==PK_TYPE_11B) {//0000 0001 0000 0000,11b
        pDevice->uSlot = C_SLOT_LONG;
        pDevice->uSIFS = C_SIFS_BG;
        pDevice->uDIFS = C_SIFS_BG + 2*C_SLOT_LONG;
          pDevice->uCwMin = C_CWMIN_B;
        byMaxMin = 5;
    }
    else {// PK_TYPE_11GA & PK_TYPE_11GB
        u8 byRate = 0;
        bool bOFDMRate = false;
	unsigned int ii = 0;
        PWLAN_IE_SUPP_RATES pItemRates = NULL;

        pDevice->uSIFS = C_SIFS_BG;
        if (pDevice->bShortSlotTime) {
            pDevice->uSlot = C_SLOT_SHORT;
        } else {
            pDevice->uSlot = C_SLOT_LONG;
        }
        pDevice->uDIFS = C_SIFS_BG + 2*pDevice->uSlot;

	pItemRates = (PWLAN_IE_SUPP_RATES)pDevice->vnt_mgmt.abyCurrSuppRates;
        for (ii = 0; ii < pItemRates->len; ii++) {
            byRate = (u8)(pItemRates->abyRates[ii]&0x7F);
            if (RATEwGetRateIdx(byRate) > RATE_11M) {
                bOFDMRate = true;
                break;
            }
        }
        if (bOFDMRate == false) {
		pItemRates = (PWLAN_IE_SUPP_RATES)pDevice->vnt_mgmt
			.abyCurrExtSuppRates;
            for (ii = 0; ii < pItemRates->len; ii++) {
                byRate = (u8)(pItemRates->abyRates[ii]&0x7F);
                if (RATEwGetRateIdx(byRate) > RATE_11M) {
                    bOFDMRate = true;
                    break;
                }
            }
        }
        if (bOFDMRate == true) {
            pDevice->uCwMin = C_CWMIN_A;
            byMaxMin = 4;
        } else {
            pDevice->uCwMin = C_CWMIN_B;
            byMaxMin = 5;
        }
    }

    pDevice->uCwMax = C_CWMAX;
    pDevice->uEIFS = C_EIFS;

    byData[0] = (u8)pDevice->uSIFS;
    byData[1] = (u8)pDevice->uDIFS;
    byData[2] = (u8)pDevice->uEIFS;
    byData[3] = (u8)pDevice->uSlot;
    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE,
                        MAC_REG_SIFS,
                        MESSAGE_REQUEST_MACREG,
                        4,
                        &byData[0]);

    byMaxMin |= 0xA0;//1010 1111,C_CWMAX = 1023
    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE,
                        MAC_REG_CWMAXMIN0,
                        MESSAGE_REQUEST_MACREG,
                        1,
                        &byMaxMin);
}

void CARDvUpdateBasicTopRate(struct vnt_private *pDevice)
{
	u8 byTopOFDM = RATE_24M, byTopCCK = RATE_1M;
	u8 ii;

     //Determines the highest basic rate.
     for (ii = RATE_54M; ii >= RATE_6M; ii --) {
         if ( (pDevice->wBasicRate) & ((WORD)(1<<ii)) ) {
             byTopOFDM = ii;
             break;
         }
     }
     pDevice->byTopOFDMBasicRate = byTopOFDM;

     for (ii = RATE_11M;; ii --) {
         if ( (pDevice->wBasicRate) & ((WORD)(1<<ii)) ) {
             byTopCCK = ii;
             break;
         }
         if (ii == RATE_1M)
            break;
     }
     pDevice->byTopCCKBasicRate = byTopCCK;
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
void CARDbAddBasicRate(struct vnt_private *pDevice, u16 wRateIdx)
{
	u16 wRate = (1 << wRateIdx);

    pDevice->wBasicRate |= wRate;

    //Determines the highest basic rate.
    CARDvUpdateBasicTopRate(pDevice);
}

int CARDbIsOFDMinBasicRate(struct vnt_private *pDevice)
{
	int ii;

    for (ii = RATE_54M; ii >= RATE_6M; ii --) {
        if ((pDevice->wBasicRate) & ((WORD)(1<<ii)))
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
	WORD wRxBcnTSFOffst = 0;

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
u64 CARDqGetNextTBTT(u64 qwTSF, WORD wBeaconInterval)
{

    unsigned int    uLowNextTBTT;
    unsigned int    uHighRemain, uLowRemain;
    unsigned int    uBeaconInterval;

    uBeaconInterval = wBeaconInterval * 1024;
    // Next TBTT = ((local_current_TSF / beacon_interval) + 1 ) * beacon_interval
	uLowNextTBTT = ((qwTSF & 0xffffffffU) >> 10) << 10;
	uLowRemain = (uLowNextTBTT) % uBeaconInterval;
	uHighRemain = ((0x80000000 % uBeaconInterval) * 2 * (u32)(qwTSF >> 32))
		% uBeaconInterval;
	uLowRemain = (uHighRemain + uLowRemain) % uBeaconInterval;
	uLowRemain = uBeaconInterval - uLowRemain;

    // check if carry when add one beacon interval
	if ((~uLowNextTBTT) < uLowRemain)
		qwTSF = ((qwTSF >> 32) + 1) << 32;

	qwTSF = (qwTSF & 0xffffffff00000000UL) |
		(u64)(uLowNextTBTT + uLowRemain);

    return (qwTSF);
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
void CARDvSetFirstNextTBTT(struct vnt_private *pDevice, WORD wBeaconInterval)
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

/*
 *
 * Description:
 *    Do Channel Switch defined in 802.11h
 *
 * Parameters:
 *  In:
 *      hDeviceContext - device structure point
 *  Out:
 *      none
 *
 * Return Value: none.
 *
-*/
int CARDbChannelSwitch(struct vnt_private *pDevice, u8 byMode,
	u8 byNewChannel, u8 byCount)
{
	int bResult = true;

	if (byCount == 0) {
		pDevice->vnt_mgmt.uCurrChannel = byNewChannel;
		CARDbSetMediaChannel(pDevice, byNewChannel);
		return bResult;
	}
    pDevice->byChannelSwitchCount = byCount;
    pDevice->byNewChannel = byNewChannel;
    pDevice->bChannelSwitch = true;

    if (byMode == 1) {
        //bResult=CARDbStopTxPacket(pDevice, PKT_TYPE_802_11_ALL);
        pDevice->bStopDataPkt = true;
    }
	return bResult;
}






