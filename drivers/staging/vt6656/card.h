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
#include "device.h"

/* init card type */

typedef enum _CARD_PHY_TYPE {
    PHY_TYPE_AUTO = 0,
    PHY_TYPE_11B,
    PHY_TYPE_11G,
    PHY_TYPE_11A
} CARD_PHY_TYPE, *PCARD_PHY_TYPE;

#define CB_MAX_CHANNEL_24G  14
#define CB_MAX_CHANNEL_5G       42 /* add channel9(5045MHz), 41==>42 */
#define CB_MAX_CHANNEL      (CB_MAX_CHANNEL_24G+CB_MAX_CHANNEL_5G)

struct vnt_private;

void CARDbSetMediaChannel(struct vnt_private *pDevice, u32 uConnectionChannel);
void CARDvSetRSPINF(struct vnt_private *pDevice, u8 byBBType);
void vUpdateIFS(struct vnt_private *pDevice);
void CARDvUpdateBasicTopRate(struct vnt_private *pDevice);
void CARDbAddBasicRate(struct vnt_private *pDevice, u16 wRateIdx);
int CARDbIsOFDMinBasicRate(struct vnt_private *pDevice);
void CARDvAdjustTSF(struct vnt_private *pDevice, u8 byRxRate,
		u64 qwBSSTimestamp, u64 qwLocalTSF);
bool CARDbGetCurrentTSF(struct vnt_private *pDevice, u64 *pqwCurrTSF);
bool CARDbClearCurrentTSF(struct vnt_private *pDevice);
void CARDvSetFirstNextTBTT(struct vnt_private *pDevice, u16 wBeaconInterval);
void CARDvUpdateNextTBTT(struct vnt_private *pDevice, u64 qwTSF,
			 u16 wBeaconInterval);
u64 CARDqGetNextTBTT(u64 qwTSF, u16 wBeaconInterval);
u64 CARDqGetTSFOffset(u8 byRxRate, u64 qwTSF1, u64 qwTSF2);
int CARDbRadioPowerOff(struct vnt_private *pDevice);
int CARDbRadioPowerOn(struct vnt_private *pDevice);
u8 CARDbyGetPktType(struct vnt_private *pDevice);
void CARDvSetBSSMode(struct vnt_private *pDevice);

#endif /* __CARD_H__ */
