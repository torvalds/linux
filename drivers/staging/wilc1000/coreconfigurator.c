// SPDX-License-Identifier: GPL-2.0
#include <linux/ieee80211.h>

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

static inline void get_BSSID(u8 *data, u8 *bssid)
{
	if (get_from_ds(data) == 1)
		get_address2(data, bssid);
	else if (get_to_ds(data) == 1)
		get_address1(data, bssid);
	else
		get_address3(data, bssid);
}

static inline void get_ssid(u8 *data, u8 *ssid, u8 *p_ssid_len)
{
	u8 len = 0;
	u8 i   = 0;
	u8 j   = 0;

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
	struct network_info *network_info = NULL;
	u8 msg_type = 0;
	u16 wid_len  = 0;
	u8 *wid_val = NULL;
	u8 *msa = NULL;
	u16 rx_len = 0;
	u8 *tim_elm = NULL;
	u8 *ies = NULL;
	u16 ies_len = 0;
	u8 index = 0;
	u32 tsf_lo;
	u32 tsf_hi;

	msg_type = msg_buffer[0];

	if ('N' != msg_type)
		return -EFAULT;

	wid_len = MAKE_WORD16(msg_buffer[6], msg_buffer[7]);
	wid_val = &msg_buffer[8];

	network_info = kzalloc(sizeof(*network_info), GFP_KERNEL);
	if (!network_info)
		return -ENOMEM;

	network_info->rssi = wid_val[0];

	msa = &wid_val[1];

	rx_len = wid_len - 1;
	network_info->cap_info = get_cap_info(msa);
	network_info->tsf_lo = get_beacon_timestamp_lo(msa);

	tsf_lo = get_beacon_timestamp_lo(msa);
	tsf_hi = get_beacon_timestamp_hi(msa);

	network_info->tsf_hi = tsf_lo | ((u64)tsf_hi << 32);

	get_ssid(msa, network_info->ssid, &network_info->ssid_len);
	get_BSSID(msa, network_info->bssid);

	network_info->ch = get_current_channel_802_11n(msa, rx_len
						       + FCS_LEN);

	index = MAC_HDR_LEN + TIME_STAMP_LEN;

	network_info->beacon_period = get_beacon_period(msa + index);

	index += BEACON_INTERVAL_LEN + CAP_INFO_LEN;

	tim_elm = get_tim_elm(msa, rx_len + FCS_LEN, index);
	if (tim_elm)
		network_info->dtim_period = tim_elm[3];
	ies = &msa[TAG_PARAM_OFFSET];
	ies_len = rx_len - TAG_PARAM_OFFSET;

	if (ies_len > 0) {
		network_info->ies = kmemdup(ies, ies_len, GFP_KERNEL);
		if (!network_info->ies) {
			kfree(network_info);
			return -ENOMEM;
		}
	}
	network_info->ies_len = ies_len;

	*ret_network_info = network_info;

	return 0;
}

s32 wilc_parse_assoc_resp_info(u8 *buffer, u32 buffer_len,
			       struct connect_info *ret_conn_info)
{
	u8 *ies = NULL;
	u16 ies_len = 0;

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
