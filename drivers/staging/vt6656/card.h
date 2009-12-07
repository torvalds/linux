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

/*---------------------  Export Definitions -------------------------*/


/*---------------------  Export Classes  ----------------------------*/

// Init card type

typedef enum _CARD_PHY_TYPE {

    PHY_TYPE_AUTO=0,
    PHY_TYPE_11B,
    PHY_TYPE_11G,
    PHY_TYPE_11A
} CARD_PHY_TYPE, *PCARD_PHY_TYPE;

typedef enum _CARD_OP_MODE {

    OP_MODE_INFRASTRUCTURE=0,
    OP_MODE_ADHOC,
    OP_MODE_AP,
    OP_MODE_UNKNOWN
} CARD_OP_MODE, *PCARD_OP_MODE;

#define CB_MAX_CHANNEL_24G  14
//#define CB_MAX_CHANNEL_5G   24
#define CB_MAX_CHANNEL_5G       42 //[20050104] add channel9(5045MHz), 41==>42
#define CB_MAX_CHANNEL      (CB_MAX_CHANNEL_24G+CB_MAX_CHANNEL_5G)

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

BOOL CARDbSetMediaChannel(PVOID pDeviceHandler, UINT uConnectionChannel);
void CARDvSetRSPINF(PVOID pDeviceHandler, BYTE byBBType);
void vUpdateIFS(PVOID pDeviceHandler);
void CARDvUpdateBasicTopRate(PVOID pDeviceHandler);
BOOL CARDbAddBasicRate(PVOID pDeviceHandler, WORD wRateIdx);
BOOL CARDbIsOFDMinBasicRate(PVOID pDeviceHandler);
void CARDvAdjustTSF(PVOID pDeviceHandler, BYTE byRxRate, QWORD qwBSSTimestamp, QWORD qwLocalTSF);
BOOL CARDbGetCurrentTSF (PVOID pDeviceHandler, PQWORD pqwCurrTSF);
BOOL CARDbClearCurrentTSF(PVOID pDeviceHandler);
void CARDvSetFirstNextTBTT(PVOID pDeviceHandler, WORD wBeaconInterval);
void CARDvUpdateNextTBTT(PVOID pDeviceHandler, QWORD qwTSF, WORD wBeaconInterval);
QWORD CARDqGetNextTBTT(QWORD qwTSF, WORD wBeaconInterval);
QWORD CARDqGetTSFOffset(BYTE byRxRate, QWORD qwTSF1, QWORD qwTSF2);
BOOL CARDbRadioPowerOff(PVOID pDeviceHandler);
BOOL CARDbRadioPowerOn(PVOID pDeviceHandler);
BYTE CARDbyGetPktType(PVOID pDeviceHandler);
void CARDvSetBSSMode(PVOID pDeviceHandler);

BOOL
CARDbChannelSwitch (
    IN PVOID            pDeviceHandler,
    IN BYTE             byMode,
    IN BYTE             byNewChannel,
    IN BYTE             byCount
    );

#endif // __CARD_H__



