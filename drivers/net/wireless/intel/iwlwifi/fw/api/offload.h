/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_offload_h__
#define __iwl_fw_api_offload_h__

/**
 * enum iwl_prot_offload_subcmd_ids - protocol offload commands
 */
enum iwl_prot_offload_subcmd_ids {
	/**
	 * @STORED_BEACON_NTF: &struct iwl_stored_beacon_notif
	 */
	STORED_BEACON_NTF = 0xFF,
};

#define MAX_STORED_BEACON_SIZE 600

/**
 * struct iwl_stored_beacon_notif - Stored beacon notification
 *
 * @system_time: system time on air rise
 * @tsf: TSF on air rise
 * @beacon_timestamp: beacon on air rise
 * @band: band, matches &RX_RES_PHY_FLAGS_BAND_24 definition
 * @channel: channel this beacon was received on
 * @rates: rate in ucode internal format
 * @byte_count: frame's byte count
 * @data: beacon data, length in @byte_count
 */
struct iwl_stored_beacon_notif {
	__le32 system_time;
	__le64 tsf;
	__le32 beacon_timestamp;
	__le16 band;
	__le16 channel;
	__le32 rates;
	__le32 byte_count;
	u8 data[MAX_STORED_BEACON_SIZE];
} __packed; /* WOWLAN_STROED_BEACON_INFO_S_VER_2 */

#endif /* __iwl_fw_api_offload_h__ */
