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

#include <linux/types.h>
#include <linux/nl80211.h>

/*
 * Loopback mode
 *
 * LOBYTE is MAC LB mode, HIBYTE is MII LB mode
 */
#define CARD_LB_NONE            MAKEWORD(MAC_LB_NONE, 0)
#define CARD_LB_MAC             MAKEWORD(MAC_LB_INTERNAL, 0)   /* PHY must ISO, avoid MAC loopback packet go out */
#define CARD_LB_PHY             MAKEWORD(MAC_LB_EXT, 0)

#define DEFAULT_MSDU_LIFETIME           512  /* ms */
#define DEFAULT_MSDU_LIFETIME_RES_64us  8000 /* 64us */

#define DEFAULT_MGN_LIFETIME            8    /* ms */
#define DEFAULT_MGN_LIFETIME_RES_64us   125  /* 64us */

#define CB_MAX_CHANNEL_24G      14
#define CB_MAX_CHANNEL_5G       42
#define CB_MAX_CHANNEL          (CB_MAX_CHANNEL_24G+CB_MAX_CHANNEL_5G)

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

struct vnt_private;

void CARDvSetRSPINF(struct vnt_private *, u8);
void CARDvUpdateBasicTopRate(struct vnt_private *);
bool CARDbIsOFDMinBasicRate(struct vnt_private *);
void CARDvSetLoopbackMode(struct vnt_private *, unsigned short wLoopbackMode);
bool CARDbSoftwareReset(struct vnt_private *);
void CARDvSetFirstNextTBTT(struct vnt_private *, unsigned short wBeaconInterval);
void CARDvUpdateNextTBTT(struct vnt_private *, u64 qwTSF, unsigned short wBeaconInterval);
bool CARDbGetCurrentTSF(struct vnt_private *, u64 *pqwCurrTSF);
u64 CARDqGetNextTBTT(u64 qwTSF, unsigned short wBeaconInterval);
u64 CARDqGetTSFOffset(unsigned char byRxRate, u64 qwTSF1, u64 qwTSF2);
unsigned char CARDbyGetPktType(struct vnt_private *);
void CARDvSafeResetTx(struct vnt_private *);
void CARDvSafeResetRx(struct vnt_private *);
bool CARDbRadioPowerOff(struct vnt_private *);
bool CARDbRadioPowerOn(struct vnt_private *);
bool CARDbSetPhyParameter(struct vnt_private *, u8);
bool CARDbUpdateTSF(struct vnt_private *, unsigned char byRxRate,
		    u64 qwBSSTimestamp);
bool CARDbSetBeaconPeriod(struct vnt_private *, unsigned short wBeaconInterval);

#endif /* __CARD_H__ */
