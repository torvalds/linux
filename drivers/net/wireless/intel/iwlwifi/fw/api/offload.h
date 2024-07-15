/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 * Copyright (C) 2021-2024 Intel Corporation
 */
#ifndef __iwl_fw_api_offload_h__
#define __iwl_fw_api_offload_h__

/**
 * enum iwl_prot_offload_subcmd_ids - protocol offload commands
 */
enum iwl_prot_offload_subcmd_ids {
	/**
	 * @WOWLAN_WAKE_PKT_NOTIFICATION: Notification in &struct iwl_wowlan_wake_pkt_notif
	 */
	WOWLAN_WAKE_PKT_NOTIFICATION = 0xFC,

	/**
	 * @WOWLAN_INFO_NOTIFICATION: Notification in
	 * &struct iwl_wowlan_info_notif_v1, &struct iwl_wowlan_info_notif_v2,
	 * or &struct iwl_wowlan_info_notif
	 */
	WOWLAN_INFO_NOTIFICATION = 0xFD,

	/**
	 * @D3_END_NOTIFICATION: End D3 state notification
	 */
	D3_END_NOTIFICATION = 0xFE,

	/**
	 * @STORED_BEACON_NTF: &struct iwl_stored_beacon_notif_v2 or
	 *	&struct iwl_stored_beacon_notif_v3
	 */
	STORED_BEACON_NTF = 0xFF,
};

#define MAX_STORED_BEACON_SIZE 600

/**
 * struct iwl_stored_beacon_notif_common - Stored beacon notif common fields
 *
 * @system_time: system time on air rise
 * @tsf: TSF on air rise
 * @beacon_timestamp: beacon on air rise
 * @band: band, matches &RX_RES_PHY_FLAGS_BAND_24 definition
 * @channel: channel this beacon was received on
 * @rates: rate in ucode internal format
 * @byte_count: frame's byte count
 */
struct iwl_stored_beacon_notif_common {
	__le32 system_time;
	__le64 tsf;
	__le32 beacon_timestamp;
	__le16 band;
	__le16 channel;
	__le32 rates;
	__le32 byte_count;
} __packed;

/**
 * struct iwl_stored_beacon_notif - Stored beacon notification
 *
 * @common: fields common for all versions
 * @data: beacon data, length in @byte_count
 */
struct iwl_stored_beacon_notif_v2 {
	struct iwl_stored_beacon_notif_common common;
	u8 data[MAX_STORED_BEACON_SIZE];
} __packed; /* WOWLAN_STROED_BEACON_INFO_S_VER_2 */

/**
 * struct iwl_stored_beacon_notif_v3 - Stored beacon notification
 *
 * @common: fields common for all versions
 * @sta_id: station for which the beacon was received
 * @reserved: reserved for alignment
 * @data: beacon data, length in @byte_count
 */
struct iwl_stored_beacon_notif_v3 {
	struct iwl_stored_beacon_notif_common common;
	u8 sta_id;
	u8 reserved[3];
	u8 data[MAX_STORED_BEACON_SIZE];
} __packed; /* WOWLAN_STROED_BEACON_INFO_S_VER_3 */

#endif /* __iwl_fw_api_offload_h__ */
