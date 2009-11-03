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
#define CB_MAX_CHANNEL_5G       42 //[20050104] add channel9(5045MHz), 41==>42
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



/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

BOOL ChannelValid(UINT CountryCode, UINT ChannelIndex);
void CARDvSetRSPINF(PVOID pDeviceHandler, CARD_PHY_TYPE ePHYType);
void vUpdateIFS(PVOID pDeviceHandler);
void CARDvUpdateBasicTopRate(PVOID pDeviceHandler);
BOOL CARDbAddBasicRate(PVOID pDeviceHandler, WORD wRateIdx);
BOOL CARDbIsOFDMinBasicRate(PVOID pDeviceHandler);
void CARDvSetLoopbackMode(DWORD_PTR dwIoBase, WORD wLoopbackMode);
BOOL CARDbSoftwareReset(PVOID pDeviceHandler);
void CARDvSetFirstNextTBTT(DWORD_PTR dwIoBase, WORD wBeaconInterval);
void CARDvUpdateNextTBTT(DWORD_PTR dwIoBase, QWORD qwTSF, WORD wBeaconInterval);
BOOL CARDbGetCurrentTSF(DWORD_PTR dwIoBase, PQWORD pqwCurrTSF);
QWORD CARDqGetNextTBTT(QWORD qwTSF, WORD wBeaconInterval);
QWORD CARDqGetTSFOffset(BYTE byRxRate, QWORD qwTSF1, QWORD qwTSF2);
BOOL CARDbSetTxPower(PVOID pDeviceHandler, ULONG ulTxPower);
BYTE CARDbyGetPktType(PVOID pDeviceHandler);
VOID CARDvSafeResetTx(PVOID pDeviceHandler);
VOID CARDvSafeResetRx(PVOID pDeviceHandler);

//xxx
BOOL CARDbRadioPowerOff(PVOID pDeviceHandler);
BOOL CARDbRadioPowerOn(PVOID pDeviceHandler);
BOOL CARDbSetChannel(PVOID pDeviceHandler, UINT uConnectionChannel);
//BOOL CARDbSendPacket(PVOID pDeviceHandler, PVOID pPacket, CARD_PKT_TYPE ePktType, UINT uLength);
BOOL CARDbIsShortPreamble(PVOID pDeviceHandler);
BOOL CARDbIsShorSlotTime(PVOID pDeviceHandler);
BOOL CARDbSetPhyParameter(PVOID pDeviceHandler, CARD_PHY_TYPE ePHYType, WORD wCapInfo, BYTE byERPField, PVOID pvSupportRateIEs, PVOID pvExtSupportRateIEs);
BOOL CARDbUpdateTSF(PVOID pDeviceHandler, BYTE byRxRate, QWORD qwBSSTimestamp, QWORD qwLocalTSF);
BOOL CARDbStopTxPacket(PVOID pDeviceHandler, CARD_PKT_TYPE ePktType);
BOOL CARDbStartTxPacket(PVOID pDeviceHandler, CARD_PKT_TYPE ePktType);
BOOL CARDbSetBeaconPeriod(PVOID pDeviceHandler, WORD wBeaconInterval);
BOOL CARDbSetBSSID(PVOID pDeviceHandler, PBYTE pbyBSSID, CARD_OP_MODE eOPMode);

BOOL
CARDbPowerDown(
    PVOID   pDeviceHandler
    );

BOOL CARDbSetTxDataRate(
    PVOID   pDeviceHandler,
    WORD    wDataRate
    );


BOOL CARDbRemoveKey (PVOID pDeviceHandler, PBYTE pbyBSSID);

BOOL
CARDbAdd_PMKID_Candidate (
    IN PVOID            pDeviceHandler,
    IN PBYTE            pbyBSSID,
    IN BOOL             bRSNCapExist,
    IN WORD             wRSNCap
    );

PVOID
CARDpGetCurrentAddress (
    IN PVOID            pDeviceHandler
    );


VOID CARDvInitChannelTable(PVOID pDeviceHandler);
BYTE CARDbyGetChannelMapping(PVOID pDeviceHandler, BYTE byChannelNumber, CARD_PHY_TYPE ePhyType);

BOOL
CARDbStartMeasure (
    IN PVOID            pDeviceHandler,
    IN PVOID            pvMeasureEIDs,
    IN UINT             uNumOfMeasureEIDs
    );

BOOL
CARDbChannelSwitch (
    IN PVOID            pDeviceHandler,
    IN BYTE             byMode,
    IN BYTE             byNewChannel,
    IN BYTE             byCount
    );

BOOL
CARDbSetQuiet (
    IN PVOID            pDeviceHandler,
    IN BOOL             bResetQuiet,
    IN BYTE             byQuietCount,
    IN BYTE             byQuietPeriod,
    IN WORD             wQuietDuration,
    IN WORD             wQuietOffset
    );

BOOL
CARDbStartQuiet (
    IN PVOID            pDeviceHandler
    );

VOID
CARDvSetCountryInfo (
    IN PVOID            pDeviceHandler,
    IN CARD_PHY_TYPE    ePHYType,
    IN PVOID            pIE
    );

VOID
CARDvSetPowerConstraint (
    IN PVOID            pDeviceHandler,
    IN BYTE             byChannel,
    IN I8               byPower
    );

VOID
CARDvGetPowerCapability (
    IN PVOID            pDeviceHandler,
    OUT PBYTE           pbyMinPower,
    OUT PBYTE           pbyMaxPower
    );

BYTE
CARDbySetSupportChannels (
    IN PVOID            pDeviceHandler,
    IN OUT PBYTE        pbyIEs
    );

I8
CARDbyGetTransmitPower (
    IN PVOID            pDeviceHandler
    );

BOOL
CARDbChannelGetList (
    IN  UINT       uCountryCodeIdx,
    OUT PBYTE      pbyChannelTable
    );

VOID
CARDvSetCountryIE(
    IN PVOID        pDeviceHandler,
    IN PVOID        pIE
    );

BOOL
CARDbGetChannelMapInfo(
    IN PVOID        pDeviceHandler,
    IN UINT         uChannelIndex,
    OUT PBYTE       pbyChannelNumber,
    OUT PBYTE       pbyMap
    );

VOID
CARDvSetChannelMapInfo(
    IN PVOID        pDeviceHandler,
    IN UINT         uChannelIndex,
    IN BYTE         byMap
    );

VOID
CARDvClearChannelMapInfo(
    IN PVOID        pDeviceHandler
    );

BYTE
CARDbyAutoChannelSelect(
    IN PVOID        pDeviceHandler,
    CARD_PHY_TYPE   ePHYType
    );

BYTE CARDbyGetChannelNumber(PVOID pDeviceHandler, BYTE byChannelIndex);

#endif // __CARD_H__



