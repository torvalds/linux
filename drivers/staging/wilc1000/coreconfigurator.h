/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#ifndef CORECONFIGURATOR_H
#define CORECONFIGURATOR_H

#include "wilc_wlan_if.h"

#define NUM_RSSI                5

#define SET_CFG              0
#define GET_CFG              1

#define MAX_ASSOC_RESP_FRAME_SIZE   256

struct rssi_history_buffer {
	bool full;
	u8 index;
	s8 samples[NUM_RSSI];
};

struct network_info {
	s8 rssi;
	u16 cap_info;
	u8 ssid[MAX_SSID_LEN];
	u8 ssid_len;
	u8 bssid[6];
	u16 beacon_period;
	u8 dtim_period;
	u8 ch;
	unsigned long time_scan_cached;
	unsigned long time_scan;
	bool new_network;
	u8 found;
	u32 tsf_lo;
	u8 *ies;
	u16 ies_len;
	void *join_params;
	struct rssi_history_buffer rssi_history;
	u64 tsf_hi;
};

struct connect_info {
	u8 bssid[6];
	u8 *req_ies;
	size_t req_ies_len;
	u8 *resp_ies;
	u16 resp_ies_len;
	u16 status;
};

struct disconnect_info {
	u16 reason;
	u8 *ie;
	size_t ie_len;
};

struct assoc_resp {
	__le16 capab_info;
	__le16 status_code;
	__le16 aid;
} __packed;

void wilc_scan_complete_received(struct wilc *wilc, u8 *buffer, u32 length);
void wilc_network_info_received(struct wilc *wilc, u8 *buffer, u32 length);
void wilc_gnrl_async_info_received(struct wilc *wilc, u8 *buffer, u32 length);
#endif
