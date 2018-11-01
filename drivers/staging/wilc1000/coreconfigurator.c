// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include <net/cfg80211.h>

#include "coreconfigurator.h"

static inline u8 *get_bssid(struct ieee80211_mgmt *mgmt)
{
	if (ieee80211_has_fromds(mgmt->frame_control))
		return mgmt->sa;
	else if (ieee80211_has_tods(mgmt->frame_control))
		return mgmt->da;
	else
		return mgmt->bssid;
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
	struct network_info *info;
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

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->rssi = wid_val[0];

	msa = &wid_val[1];
	mgt = (struct ieee80211_mgmt *)&wid_val[1];
	rx_len = wid_len - 1;

	if (ieee80211_is_probe_resp(mgt->frame_control)) {
		info->cap_info = le16_to_cpu(mgt->u.probe_resp.capab_info);
		info->beacon_period = le16_to_cpu(mgt->u.probe_resp.beacon_int);
		info->tsf_hi = le64_to_cpu(mgt->u.probe_resp.timestamp);
		info->tsf_lo = (u32)info->tsf_hi;
		offset = offsetof(struct ieee80211_mgmt, u.probe_resp.variable);
	} else if (ieee80211_is_beacon(mgt->frame_control)) {
		info->cap_info = le16_to_cpu(mgt->u.beacon.capab_info);
		info->beacon_period = le16_to_cpu(mgt->u.beacon.beacon_int);
		info->tsf_hi = le64_to_cpu(mgt->u.beacon.timestamp);
		info->tsf_lo = (u32)info->tsf_hi;
		offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	} else {
		/* only process probe response and beacon frame */
		kfree(info);
		return -EIO;
	}

	ether_addr_copy(info->bssid, get_bssid(mgt));

	ies = mgt->u.beacon.variable;
	ies_len = rx_len - offset;
	if (ies_len <= 0) {
		kfree(info);
		return -EIO;
	}

	info->ies = kmemdup(ies, ies_len, GFP_KERNEL);
	if (!info->ies) {
		kfree(info);
		return -ENOMEM;
	}

	info->ies_len = ies_len;

	ssid_elm = cfg80211_find_ie(WLAN_EID_SSID, ies, ies_len);
	if (ssid_elm) {
		info->ssid_len = ssid_elm[1];
		if (info->ssid_len <= IEEE80211_MAX_SSID_LEN)
			memcpy(info->ssid, ssid_elm + 2, info->ssid_len);
		else
			info->ssid_len = 0;
	}

	ch_elm = cfg80211_find_ie(WLAN_EID_DS_PARAMS, ies, ies_len);
	if (ch_elm && ch_elm[1] > 0)
		info->ch = ch_elm[2];

	tim_elm = cfg80211_find_ie(WLAN_EID_TIM, ies, ies_len);
	if (tim_elm && tim_elm[1] >= 2)
		info->dtim_period = tim_elm[3];

	*ret_network_info = info;

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
