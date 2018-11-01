// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include <net/cfg80211.h>

#include "coreconfigurator.h"

static inline void get_address1(u8 *msa, u8 *addr)
{
	memcpy(addr, msa + 4, 6);
}

static inline void get_address2(u8 *msa, u8 *addr)
{
	memcpy(addr, msa + 10, 6);
}

static inline void get_address3(u8 *msa, u8 *addr)
{
	memcpy(addr, msa + 16, 6);
}

static inline void get_bssid(__le16 fc, u8 *data, u8 *bssid)
{
	if (ieee80211_has_fromds(fc))
		get_address2(data, bssid);
	else if (ieee80211_has_tods(fc))
		get_address1(data, bssid);
	else
		get_address3(data, bssid);
}

static inline u16 get_asoc_status(u8 *data)
{
	u16 asoc_status;

	asoc_status = data[3];
	return (asoc_status << 8) | data[2];
}

s32 wilc_parse_network_info(u8 *msg_buffer,
			    struct network_info **ret_network_info)
{
	struct network_info *network_info;
	struct ieee80211_mgmt *mgt;
	u8 *wid_val, *msa, *ies;
	u16 wid_len, rx_len, ies_len;
	u8 msg_type;
	size_t offset;
	const u8 *ch_elm, *tim_elm, *ssid_elm;

	msg_type = msg_buffer[0];
	if ('N' != msg_type)
		return -EFAULT;

	wid_len = get_unaligned_le16(&msg_buffer[6]);
	wid_val = &msg_buffer[8];

	network_info = kzalloc(sizeof(*network_info), GFP_KERNEL);
	if (!network_info)
		return -ENOMEM;

	network_info->rssi = wid_val[0];

	msa = &wid_val[1];
	mgt = (struct ieee80211_mgmt *)&wid_val[1];
	rx_len = wid_len - 1;

	if (ieee80211_is_probe_resp(mgt->frame_control)) {
		network_info->cap_info = le16_to_cpu(mgt->u.probe_resp.capab_info);
		network_info->beacon_period = le16_to_cpu(mgt->u.probe_resp.beacon_int);
		network_info->tsf_hi = le64_to_cpu(mgt->u.probe_resp.timestamp);
		network_info->tsf_lo = (u32)network_info->tsf_hi;
		offset = offsetof(struct ieee80211_mgmt, u.probe_resp.variable);
	} else if (ieee80211_is_beacon(mgt->frame_control)) {
		network_info->cap_info = le16_to_cpu(mgt->u.beacon.capab_info);
		network_info->beacon_period = le16_to_cpu(mgt->u.beacon.beacon_int);
		network_info->tsf_hi = le64_to_cpu(mgt->u.beacon.timestamp);
		network_info->tsf_lo = (u32)network_info->tsf_hi;
		offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	} else {
		/* only process probe response and beacon frame */
		kfree(network_info);
		return -EIO;
	}

	get_bssid(mgt->frame_control, msa, network_info->bssid);

	ies = mgt->u.beacon.variable;
	ies_len = rx_len - offset;
	if (ies_len <= 0) {
		kfree(network_info);
		return -EIO;
	}

	network_info->ies = kmemdup(ies, ies_len, GFP_KERNEL);
	if (!network_info->ies) {
		kfree(network_info);
		return -ENOMEM;
	}

	network_info->ies_len = ies_len;

	ssid_elm = cfg80211_find_ie(WLAN_EID_SSID, ies, ies_len);
	if (ssid_elm) {
		network_info->ssid_len = ssid_elm[1];
		if (network_info->ssid_len <= IEEE80211_MAX_SSID_LEN)
			memcpy(network_info->ssid, ssid_elm + 2,
			       network_info->ssid_len);
		else
			network_info->ssid_len = 0;
	}

	ch_elm = cfg80211_find_ie(WLAN_EID_DS_PARAMS, ies, ies_len);
	if (ch_elm && ch_elm[1] > 0)
		network_info->ch = ch_elm[2];

	tim_elm = cfg80211_find_ie(WLAN_EID_TIM, ies, ies_len);
	if (tim_elm && tim_elm[1] >= 2)
		network_info->dtim_period = tim_elm[3];

	*ret_network_info = network_info;

	return 0;
}

s32 wilc_parse_assoc_resp_info(u8 *buffer, u32 buffer_len,
			       struct connect_info *ret_conn_info)
{
	u8 *ies;
	u16 ies_len;

	ret_conn_info->status = get_asoc_status(buffer);
	if (ret_conn_info->status == WLAN_STATUS_SUCCESS) {
		ies = &buffer[CAP_INFO_LEN + STATUS_CODE_LEN + AID_LEN];
		ies_len = buffer_len - (CAP_INFO_LEN + STATUS_CODE_LEN +
					AID_LEN);

		ret_conn_info->resp_ies = kmemdup(ies, ies_len, GFP_KERNEL);
		if (!ret_conn_info->resp_ies)
			return -ENOMEM;

		ret_conn_info->resp_ies_len = ies_len;
	}

	return 0;
}
