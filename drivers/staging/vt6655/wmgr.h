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
 *
 * File: wmgr.h
 *
 * Purpose:
 *
 * Author: lyndon chen
 *
 * Date: Jan 2, 2003
 *
 * Functions:
 *
 * Revision History:
 *
 */

#ifndef __WMGR_H__
#define __WMGR_H__

#include "ttype.h"
#include "80211mgr.h"
#include "80211hdr.h"
#include "wcmd.h"
#include "bssdb.h"
#include "card.h"

/*---------------------  Export Definitions -------------------------*/

// Scan time
#define PROBE_DELAY                  100  // (us)
#define SWITCH_CHANNEL_DELAY         200 // (us)
#define WLAN_SCAN_MINITIME           25   // (ms)
#define WLAN_SCAN_MAXTIME            100  // (ms)
#define TRIVIAL_SYNC_DIFFERENCE      0    // (us)
#define DEFAULT_IBSS_BI              100  // (ms)

#define WCMD_ACTIVE_SCAN_TIME   50 //(ms)
#define WCMD_PASSIVE_SCAN_TIME  100 //(ms)

#define DEFAULT_MSDU_LIFETIME           512  // ms
#define DEFAULT_MSDU_LIFETIME_RES_64us  8000 // 64us

#define DEFAULT_MGN_LIFETIME            8    // ms
#define DEFAULT_MGN_LIFETIME_RES_64us   125  // 64us

#define MAKE_BEACON_RESERVED            10  //(us)

#define TIM_MULTICAST_MASK           0x01
#define TIM_BITMAPOFFSET_MASK        0xFE
#define DEFAULT_DTIM_PERIOD             1

#define AP_LONG_RETRY_LIMIT             4

#define DEFAULT_IBSS_CHANNEL            6  //2.4G

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Types  ------------------------------*/
#define timer_expire(timer, next_tick)   mod_timer(&timer, RUN_AT(next_tick))
typedef void (*TimerFunction)(unsigned long);

//+++ NDIS related

typedef unsigned char NDIS_802_11_MAC_ADDRESS[6];
typedef struct _NDIS_802_11_AI_REQFI {
	unsigned short Capabilities;
	unsigned short ListenInterval;
	NDIS_802_11_MAC_ADDRESS  CurrentAPAddress;
} NDIS_802_11_AI_REQFI, *PNDIS_802_11_AI_REQFI;

typedef struct _NDIS_802_11_AI_RESFI {
	unsigned short Capabilities;
	unsigned short StatusCode;
	unsigned short AssociationId;
} NDIS_802_11_AI_RESFI, *PNDIS_802_11_AI_RESFI;

typedef struct _NDIS_802_11_ASSOCIATION_INFORMATION {
	unsigned long Length;
	unsigned short          AvailableRequestFixedIEs;
	NDIS_802_11_AI_REQFI    RequestFixedIEs;
	unsigned long RequestIELength;
	unsigned long OffsetRequestIEs;
	unsigned short          AvailableResponseFixedIEs;
	NDIS_802_11_AI_RESFI    ResponseFixedIEs;
	unsigned long ResponseIELength;
	unsigned long OffsetResponseIEs;
} NDIS_802_11_ASSOCIATION_INFORMATION, *PNDIS_802_11_ASSOCIATION_INFORMATION;

typedef struct tagSAssocInfo {
	NDIS_802_11_ASSOCIATION_INFORMATION     AssocInfo;
	unsigned char abyIEs[WLAN_BEACON_FR_MAXLEN+WLAN_BEACON_FR_MAXLEN];
	// store ReqIEs set by OID_802_11_ASSOCIATION_INFORMATION
	unsigned long RequestIELength;
	unsigned char abyReqIEs[WLAN_BEACON_FR_MAXLEN];
} SAssocInfo, *PSAssocInfo;

#endif // __WMGR_H__
