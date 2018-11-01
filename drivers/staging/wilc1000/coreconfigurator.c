// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include <net/cfg80211.h>

#include "coreconfigurator.h"

#define TAG_PARAM_OFFSET	(MAC_HDR_LEN + TIME_STAMP_LEN + \
				 BEACON_INTERVAL_LEN + CAP_INFO_LEN)

enum sub_frame_type {
	ASSOC_REQ             = 0x00,
	ASSOC_RSP             = 0x10,
	REASSOC_REQ           = 0x20,
	REASSOC_RSP           = 0x30,
	PROBE_REQ             = 0x40,
	PROBE_RSP             = 0x50,
	BEACON                = 0x80,
	ATIM                  = 0x90,
	DISASOC               = 0xA0,
	AUTH                  = 0xB0,
	DEAUTH                = 0xC0,
	ACTION                = 0xD0,
	PS_POLL               = 0xA4,
	RTS                   = 0xB4,
	CTS                   = 0xC4,
	ACK                   = 0xD4,
	CFEND                 = 0xE4,
	CFEND_ACK             = 0xF4,
	DATA                  = 0x08,
	DATA_ACK              = 0x18,
	DATA_POLL             = 0x28,
	DATA_POLL_ACK         = 0x38,
	NULL_FRAME            = 0x48,
	CFACK                 = 0x58,
	CFPOLL                = 0x68,
	CFPOLL_ACK            = 0x78,
	QOS_DATA              = 0x88,
	QOS_DATA_ACK          = 0x98,
	QOS_DATA_POLL         = 0xA8,
	QOS_DATA_POLL_ACK     = 0xB8,
	QOS_NULL_FRAME        = 0xC8,
	QOS_CFPOLL            = 0xE8,
	QOS_CFPOLL_ACK        = 0xF8,
	BLOCKACK_REQ          = 0x84,
	BLOCKACK              = 0x94,
	FRAME_SUBTYPE_FORCE_32BIT  = 0xFFFFFFFF
};

static inline u16 get_beacon_period(u8 *data)
{
	u16 bcn_per;

	bcn_per  = data[0];
	bcn_per |= (data[1] << 8);

	return bcn_per;
}

static inline u32 get_beacon_timestamp_lo(u8 *data)
{
	u32 time_stamp = 0;
	u32 index    = MAC_HDR_LEN;

	time_stamp |= data[index++];
	time_stamp |= (data[index++] << 8);
	time_stamp |= (data[index++] << 16);
	time_stamp |= (data[index]   << 24);

	return time_stamp;
}

static inline u32 get_beacon_timestamp_hi(u8 *data)
{
	u32 time_stamp = 0;
	u32 index    = (MAC_HDR_LEN + 4);

	time_stamp |= data[index++];
	time_stamp |= (data[index++] << 8);
	time_stamp |= (data[index++] << 16);
	time_stamp |= (data[index]   << 24);

	return time_stamp;
}

static inline enum sub_frame_type get_sub_type(u8 *header)
{
	return ((enum sub_frame_type)(header[0] & 0xFC));
}

static inline u8 get_to_ds(u8 *header)
{
	return (header[1] & 0x01);
}

static inline u8 get_from_ds(u8 *header)
{
	return ((header[1] & 0x02) >> 1);
}

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

static inline void get_ssid(u8 *data, u8 *ssid, u8 *p_ssid_len)
{
	u8 i, j, len;

	len = data[TAG_PARAM_OFFSET + 1];
	j   = TAG_PARAM_OFFSET + 2;

	if (len >= MAX_SSID_LEN)
		len = 0;

	for (i = 0; i < len; i++, j++)
		ssid[i] = data[j];

	ssid[len] = '\0';

	*p_ssid_len = len;
}

static inline u16 get_cap_info(u8 *data)
{
	u16 cap_info = 0;
	u16 index    = MAC_HDR_LEN;
	enum sub_frame_type st;

	st = get_sub_type(data);

	if (st == BEACON || st == PROBE_RSP)
		index += TIME_STAMP_LEN + BEACON_INTERVAL_LEN;

	cap_info  = data[index];
	cap_info |= (data[index + 1] << 8);

	return cap_info;
}

static inline u16 get_asoc_status(u8 *data)
{
	u16 asoc_status;

	asoc_status = data[3];
	return (asoc_status << 8) | data[2];
}

static u8 *get_tim_elm(u8 *msa, u16 rx_len, u16 tag_param_offset)
{
	u16 index;

	index = tag_param_offset;

	while (index < (rx_len - FCS_LEN)) {
		if (msa[index] == WLAN_EID_TIM)
			return &msa[index];
		index += (IE_HDR_LEN + msa[index + 1]);
	}

	return NULL;
}

static u8 get_current_channel_802_11n(u8 *msa, u16 rx_len)
{
	u16 index;

	index = TAG_PARAM_OFFSET;
	while (index < (rx_len - FCS_LEN)) {
		if (msa[index] == WLAN_EID_DS_PARAMS)
			return msa[index + 2];
		index += msa[index + 1] + IE_HDR_LEN;
	}

	return 0;
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
