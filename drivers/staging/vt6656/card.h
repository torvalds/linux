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

/* init card type */

typedef enum _CARD_PHY_TYPE {
    PHY_TYPE_AUTO = 0,
    PHY_TYPE_11B,
    PHY_TYPE_11G,
    PHY_TYPE_11A
} CARD_PHY_TYPE, *PCARD_PHY_TYPE;

typedef enum _CARD_OP_MODE {
    OP_MODE_INFRASTRUCTURE = 0,
    OP_MODE_ADHOC,
    OP_MODE_AP,
    OP_MODE_UNKNOWN
} CARD_OP_MODE, *PCARD_OP_MODE;

#define CB_MAX_CHANNEL_24G  14
/* #define CB_MAX_CHANNEL_5G   24 */
#define CB_MAX_CHANNEL_5G       42 /* add channel9(5045MHz), 41==>42 */
#define CB_MAX_CHANNEL      (CB_MAX_CHANNEL_24G+CB_MAX_CHANNEL_5G)

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

BOOL CARDbSetMediaChannel(void *pDeviceHandler,
			  unsigned int uConnectionChannel);
void CARDvSetRSPINF(void *pDeviceHandler, BYTE byBBType);
void vUpdateIFS(void *pDeviceHandler);
void CARDvUpdateBasicTopRate(void *pDeviceHandler);
BOOL CARDbAddBasicRate(void *pDeviceHandler, WORD wRateIdx);
BOOL CARDbIsOFDMinBasicRate(void *pDeviceHandler);
void CARDvAdjustTSF(void *pDeviceHandler, BYTE byRxRate,
		    QWORD qwBSSTimestamp, QWORD qwLocalTSF);
BOOL CARDbGetCurrentTSF(void *pDeviceHandler, PQWORD pqwCurrTSF);
BOOL CARDbClearCurrentTSF(void *pDeviceHandler);
void CARDvSetFirstNextTBTT(void *pDeviceHandler, WORD wBeaconInterval);
void CARDvUpdateNextTBTT(void *pDeviceHandler, QWORD qwTSF,
			 WORD wBeaconInterval);
QWORD CARDqGetNextTBTT(QWORD qwTSF, WORD wBeaconInterval);
QWORD CARDqGetTSFOffset(BYTE byRxRate, QWORD qwTSF1, QWORD qwTSF2);
BOOL CARDbRadioPowerOff(void *pDeviceHandler);
BOOL CARDbRadioPowerOn(void *pDeviceHandler);
BYTE CARDbyGetPktType(void *pDeviceHandler);
void CARDvSetBSSMode(void *pDeviceHandler);

BOOL CARDbChannelSwitch(void *pDeviceHandler,
			BYTE byMode,
			BYTE byNewChannel,
			BYTE byCount);

#endif /* __CARD_H__ */
