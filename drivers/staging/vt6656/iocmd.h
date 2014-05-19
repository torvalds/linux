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
 * File: iocmd.h
 *
 * Purpose: Handles the viawget ioctl private interface functions
 *
 * Author: Lyndon Chen
 *
 * Date: May 8, 2002
 *
 */

#ifndef __IOCMD_H__
#define __IOCMD_H__

typedef enum tagWZONETYPE {
  ZoneType_USA = 0,
  ZoneType_Japan = 1,
  ZoneType_Europe = 2
} WZONETYPE;

#define SSID_MAXLEN            32
#define BSSID_LEN              6
#define WEP_NKEYS              4
#define WEP_KEYMAXLEN          29
#define WEP_40BIT_LEN          5
#define WEP_104BIT_LEN         13
#define WEP_232BIT_LEN         16

typedef struct tagSBSSIDItem {

	u32	    uChannel;
    u8      abyBSSID[BSSID_LEN];
    u8      abySSID[SSID_MAXLEN + 1];
    u16	    wBeaconInterval;
    u16	    wCapInfo;
    u8      byNetType;
    bool    bWEPOn;
    u32     uRSSI;

} __packed SBSSIDItem;

typedef struct tagSNodeItem {
    // STA info
    u16            wAID;
    u8             abyMACAddr[6];
    u16            wTxDataRate;
    u16            wInActiveCount;
    u16            wEnQueueCnt;
    u16            wFlags;
    bool           bPWBitOn;
    u8             byKeyIndex;
    u16            wWepKeyLength;
    u8            abyWepKey[WEP_KEYMAXLEN];
    // Auto rate fallback vars
    bool           bIsInFallback;
    u32            uTxFailures;
    u32            uTxAttempts;
    u16            wFailureRatio;

} __packed SNodeItem;

struct viawget_hostapd_param {
	u32 cmd;
	u8 sta_addr[6];
	union {
		struct {
			u16 aid;
			u16 capability;
			u8 tx_supp_rates;
		} add_sta;
		struct {
			u32 inactive_sec;
		} get_info_sta;
		struct {
			u8 alg;
			u32 flags;
			u32 err;
			u8 idx;
			u8 seq[8];
			u16 key_len;
			u8 key[0];
		} crypt;
		struct {
			u32 flags_and;
			u32 flags_or;
		} set_flags_sta;
		struct {
			u16 rid;
			u16 len;
			u8 data[0];
		} rid;
		struct {
			u8 len;
			u8 data[0];
		} generic_elem;
		struct {
			u16 cmd;
			u16 reason_code;
		} mlme;
		struct {
			u8 ssid_len;
			u8 ssid[32];
		} scan_req;
	} u;
} __packed;

#endif /* __IOCMD_H__ */
