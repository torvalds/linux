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
 * File: card.h
 *
 * Purpose: Provide functions to setup NIC operation mode
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 */

#ifndef __CARD_H__
#define __CARD_H__

#include "ttype.h"
#include <linux/types.h>

//
// Loopback mode
//
// LOBYTE is MAC LB mode, HIBYTE is MII LB mode
#define CARD_LB_NONE            MAKEWORD(MAC_LB_NONE, 0)
#define CARD_LB_MAC             MAKEWORD(MAC_LB_INTERNAL, 0)   // PHY must ISO, avoid MAC loopback packet go out
#define CARD_LB_PHY             MAKEWORD(MAC_LB_EXT, 0)

#define DEFAULT_MSDU_LIFETIME           512  // ms
#define DEFAULT_MSDU_LIFETIME_RES_64us  8000 // 64us

#define DEFAULT_MGN_LIFETIME            8    // ms
#define DEFAULT_MGN_LIFETIME_RES_64us   125  // 64us

#define CB_MAX_CHANNEL_24G      14
#define CB_MAX_CHANNEL_5G       42
#define CB_MAX_CHANNEL          (CB_MAX_CHANNEL_24G+CB_MAX_CHANNEL_5G)

typedef enum _CARD_PHY_TYPE {
	PHY_TYPE_AUTO,
	PHY_TYPE_11B,
	PHY_TYPE_11G,
	PHY_TYPE_11A
} CARD_PHY_TYPE, *PCARD_PHY_TYPE;

typedef enum _CARD_PKT_TYPE {
	PKT_TYPE_802_11_BCN,
	PKT_TYPE_802_11_MNG,
	PKT_TYPE_802_11_DATA,
	PKT_TYPE_802_11_ALL
} CARD_PKT_TYPE, *PCARD_PKT_TYPE;

typedef enum _CARD_STATUS_TYPE {
	CARD_STATUS_MEDIA_CONNECT,
	CARD_STATUS_MEDIA_DISCONNECT,
	CARD_STATUS_PMKID
} CARD_STATUS_TYPE, *PCARD_STATUS_TYPE;

typedef enum _CARD_OP_MODE {
	OP_MODE_INFRASTRUCTURE,
	OP_MODE_ADHOC,
	OP_MODE_AP,
	OP_MODE_UNKNOWN
} CARD_OP_MODE, *PCARD_OP_MODE;

void CARDvSetRSPINF(void *pDeviceHandler, CARD_PHY_TYPE ePHYType);
void vUpdateIFS(void *pDeviceHandler);
void CARDvUpdateBasicTopRate(void *pDeviceHandler);
bool CARDbAddBasicRate(void *pDeviceHandler, unsigned short wRateIdx);
bool CARDbIsOFDMinBasicRate(void *pDeviceHandler);
void CARDvSetLoopbackMode(void __iomem *dwIoBase, unsigned short wLoopbackMode);
bool CARDbSoftwareReset(void *pDeviceHandler);
void CARDvSetFirstNextTBTT(void __iomem *dwIoBase, unsigned short wBeaconInterval);
void CARDvUpdateNextTBTT(void __iomem *dwIoBase, QWORD qwTSF, unsigned short wBeaconInterval);
bool CARDbGetCurrentTSF(void __iomem *dwIoBase, PQWORD pqwCurrTSF);
QWORD CARDqGetNextTBTT(QWORD qwTSF, unsigned short wBeaconInterval);
QWORD CARDqGetTSFOffset(unsigned char byRxRate, QWORD qwTSF1, QWORD qwTSF2);
bool CARDbSetTxPower(void *pDeviceHandler, unsigned long ulTxPower);
unsigned char CARDbyGetPktType(void *pDeviceHandler);
void CARDvSafeResetTx(void *pDeviceHandler);
void CARDvSafeResetRx(void *pDeviceHandler);

bool CARDbRadioPowerOff(void *pDeviceHandler);
bool CARDbRadioPowerOn(void *pDeviceHandler);
bool CARDbIsShortPreamble(void *pDeviceHandler);
bool CARDbIsShorSlotTime(void *pDeviceHandler);
bool CARDbSetPhyParameter(void *pDeviceHandler, CARD_PHY_TYPE ePHYType, unsigned short wCapInfo, unsigned char byERPField, void *pvSupportRateIEs, void *pvExtSupportRateIEs);
bool CARDbUpdateTSF(void *pDeviceHandler, unsigned char byRxRate, QWORD qwBSSTimestamp, QWORD qwLocalTSF);
bool CARDbStopTxPacket(void *pDeviceHandler, CARD_PKT_TYPE ePktType);
bool CARDbStartTxPacket(void *pDeviceHandler, CARD_PKT_TYPE ePktType);
bool CARDbSetBeaconPeriod(void *pDeviceHandler, unsigned short wBeaconInterval);
bool CARDbSetBSSID(void *pDeviceHandler, unsigned char *pbyBSSID, CARD_OP_MODE eOPMode);

bool
CARDbPowerDown(
	void *pDeviceHandler
);

bool CARDbSetTxDataRate(
	void *pDeviceHandler,
	unsigned short wDataRate
);

bool CARDbRemoveKey(void *pDeviceHandler, unsigned char *pbyBSSID);

bool
CARDbAdd_PMKID_Candidate(
	void *pDeviceHandler,
	unsigned char *pbyBSSID,
	bool bRSNCapExist,
	unsigned short wRSNCap
);

void *
CARDpGetCurrentAddress(
	void *pDeviceHandler
);

bool
CARDbStartMeasure(
	void *pDeviceHandler,
	void *pvMeasureEIDs,
	unsigned int uNumOfMeasureEIDs
);

bool
CARDbChannelSwitch(
	void *pDeviceHandler,
	unsigned char byMode,
	unsigned char byNewChannel,
	unsigned char byCount
);

bool
CARDbSetQuiet(
	void *pDeviceHandler,
	bool bResetQuiet,
	unsigned char byQuietCount,
	unsigned char byQuietPeriod,
	unsigned short wQuietDuration,
	unsigned short wQuietOffset
);

bool
CARDbStartQuiet(
	void *pDeviceHandler
);

void
CARDvSetPowerConstraint(
	void *pDeviceHandler,
	unsigned char byChannel,
	char byPower
);

void
CARDvGetPowerCapability(
	void *pDeviceHandler,
	unsigned char *pbyMinPower,
	unsigned char *pbyMaxPower
);

char
CARDbyGetTransmitPower(
	void *pDeviceHandler
);

#endif // __CARD_H__
