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
 * File: iowpa.h
 *
 * Purpose: Handles wpa supplicant ioctl interface
 *
 * Author: Lyndon Chen
 *
 * Date: May 8, 2002
 *
 */

#ifndef __IOWPA_H__
#define __IOWPA_H__

#define WPA_IE_LEN 64

//WPA related

enum {
	VIAWGET_SET_WPA = 1,
	VIAWGET_SET_KEY = 2,
	VIAWGET_SET_SCAN = 3,
	VIAWGET_GET_SCAN = 4,
	VIAWGET_GET_SSID = 5,
	VIAWGET_GET_BSSID = 6,
	VIAWGET_SET_DROP_UNENCRYPT = 7,
	VIAWGET_SET_DEAUTHENTICATE = 8,
	VIAWGET_SET_ASSOCIATE = 9,
	VIAWGET_SET_DISASSOCIATE = 10
};

enum {
	VIAWGET_ASSOC_MSG = 1,
	VIAWGET_DISASSOC_MSG = 2,
	VIAWGET_PTK_MIC_MSG = 3,
	VIAWGET_GTK_MIC_MSG = 4,
	VIAWGET_CCKM_ROAM_MSG = 5,
	VIAWGET_DEVICECLOSE_MSG = 6
};

#pragma pack(1)
typedef struct viawget_wpa_header {
	u8 type;
	u16 req_ie_len;
	u16 resp_ie_len;
} viawget_wpa_header;

struct viawget_wpa_param {
	u32 cmd;
	u8 addr[6];
	union {
		struct {
			u8 len;
			u8 data[0];
		} generic_elem;

		struct {
			u8 bssid[6];
			u8 ssid[32];
			u8 ssid_len;
			u8 __user *wpa_ie;
			u16 wpa_ie_len;
			int pairwise_suite;
			int group_suite;
			int key_mgmt_suite;
			int auth_alg;
			int mode;

		} wpa_associate;

		struct {
			int alg_name;
			u16 key_index;
			u16 set_tx;
			u8 *seq;
			u16 seq_len;
			u8 *key;
			u16 key_len;
		} wpa_key;

		struct {
			u8 ssid_len;
			u8 ssid[32];
		} scan_req;

		struct {
			u16 scan_count;
			u8 __user *buf;
		} scan_results;

	} u;
};

#pragma pack(1)
struct viawget_scan_result {
	u8 bssid[6];
	u8 ssid[32];
	u16 ssid_len;
	u8 wpa_ie[WPA_IE_LEN];
	u16 wpa_ie_len;
	u8 rsn_ie[WPA_IE_LEN];
	u16 rsn_ie_len;
	int freq; // MHz
	int caps; // e.g. privacy
	int qual; // signal quality
	int noise;
	int level;
	int maxrate;
};

#pragma pack()

#endif //__IOWPA_H__
