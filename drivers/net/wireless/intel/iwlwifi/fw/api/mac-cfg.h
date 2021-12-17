/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014, 2018-2019, 2021 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_mac_cfg_h__
#define __iwl_fw_api_mac_cfg_h__

/**
 * enum iwl_mac_conf_subcmd_ids - mac configuration command IDs
 */
enum iwl_mac_conf_subcmd_ids {
	/**
	 * @LOW_LATENCY_CMD: &struct iwl_mac_low_latency_cmd
	 */
	LOW_LATENCY_CMD = 0x3,
	/**
	 * @CHANNEL_SWITCH_TIME_EVENT_CMD: &struct iwl_chan_switch_te_cmd
	 */
	CHANNEL_SWITCH_TIME_EVENT_CMD = 0x4,
	/**
	 * @MISSED_VAP_NOTIF: &struct iwl_missed_vap_notif
	 */
	MISSED_VAP_NOTIF = 0xFA,
	/**
	 * @SESSION_PROTECTION_CMD: &struct iwl_mvm_session_prot_cmd
	 */
	SESSION_PROTECTION_CMD = 0x5,

	/**
	 * @SESSION_PROTECTION_NOTIF: &struct iwl_mvm_session_prot_notif
	 */
	SESSION_PROTECTION_NOTIF = 0xFB,

	/**
	 * @PROBE_RESPONSE_DATA_NOTIF: &struct iwl_probe_resp_data_notif
	 */
	PROBE_RESPONSE_DATA_NOTIF = 0xFC,

	/**
	 * @CHANNEL_SWITCH_START_NOTIF: &struct iwl_channel_switch_start_notif
	 */
	CHANNEL_SWITCH_START_NOTIF = 0xFF,
};

#define IWL_P2P_NOA_DESC_COUNT	(2)

/**
 * struct iwl_p2p_noa_attr - NOA attr contained in probe resp FW notification
 *
 * @id: attribute id
 * @len_low: length low half
 * @len_high: length high half
 * @idx: instance of NoA timing
 * @ctwin: GO's ct window and pwer save capability
 * @desc: NoA descriptor
 * @reserved: reserved for alignment purposes
 */
struct iwl_p2p_noa_attr {
	u8 id;
	u8 len_low;
	u8 len_high;
	u8 idx;
	u8 ctwin;
	struct ieee80211_p2p_noa_desc desc[IWL_P2P_NOA_DESC_COUNT];
	u8 reserved;
} __packed;

#define IWL_PROBE_RESP_DATA_NO_CSA (0xff)

/**
 * struct iwl_probe_resp_data_notif - notification with NOA and CSA counter
 *
 * @mac_id: the mac which should send the probe response
 * @noa_active: notifies if the noa attribute should be handled
 * @noa_attr: P2P NOA attribute
 * @csa_counter: current csa counter
 * @reserved: reserved for alignment purposes
 */
struct iwl_probe_resp_data_notif {
	__le32 mac_id;
	__le32 noa_active;
	struct iwl_p2p_noa_attr noa_attr;
	u8 csa_counter;
	u8 reserved[3];
} __packed; /* PROBE_RESPONSE_DATA_NTFY_API_S_VER_1 */

/**
 * struct iwl_missed_vap_notif - notification of missing vap detection
 *
 * @mac_id: the mac for which the ucode sends the notification for
 * @num_beacon_intervals_elapsed: beacons elpased with no vap profile inside
 * @profile_periodicity: beacons period to have our profile inside
 * @reserved: reserved for alignment purposes
 */
struct iwl_missed_vap_notif {
	__le32 mac_id;
	u8 num_beacon_intervals_elapsed;
	u8 profile_periodicity;
	u8 reserved[2];
} __packed; /* MISSED_VAP_NTFY_API_S_VER_1 */

/**
 * struct iwl_channel_switch_start_notif - Channel switch start notification
 *
 * @id_and_color: ID and color of the MAC
 */
struct iwl_channel_switch_start_notif {
	__le32 id_and_color;
} __packed; /* CHANNEL_SWITCH_START_NTFY_API_S_VER_1 */

/**
 * struct iwl_chan_switch_te_cmd - Channel Switch Time Event command
 *
 * @mac_id: MAC ID for channel switch
 * @action: action to perform, one of FW_CTXT_ACTION_*
 * @tsf: beacon tsf
 * @cs_count: channel switch count from CSA/eCSA IE
 * @cs_delayed_bcn_count: if set to N (!= 0) GO/AP can delay N beacon intervals
 *	at the new channel after the channel switch, otherwise (N == 0) expect
 *	beacon right after the channel switch.
 * @cs_mode: 1 - quiet, 0 - otherwise
 * @reserved: reserved for alignment purposes
 */
struct iwl_chan_switch_te_cmd {
	__le32 mac_id;
	__le32 action;
	__le32 tsf;
	u8 cs_count;
	u8 cs_delayed_bcn_count;
	u8 cs_mode;
	u8 reserved;
} __packed; /* MAC_CHANNEL_SWITCH_TIME_EVENT_S_VER_2 */

/**
 * struct iwl_mac_low_latency_cmd - set/clear mac to 'low-latency mode'
 *
 * @mac_id: MAC ID to whom to apply the low-latency configurations
 * @low_latency_rx: 1/0 to set/clear Rx low latency direction
 * @low_latency_tx: 1/0 to set/clear Tx low latency direction
 * @reserved: reserved for alignment purposes
 */
struct iwl_mac_low_latency_cmd {
	__le32 mac_id;
	u8 low_latency_rx;
	u8 low_latency_tx;
	__le16 reserved;
} __packed; /* MAC_LOW_LATENCY_API_S_VER_1 */

#endif /* __iwl_fw_api_mac_cfg_h__ */
