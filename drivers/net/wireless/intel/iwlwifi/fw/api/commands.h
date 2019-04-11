/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#ifndef __iwl_fw_api_commands_h__
#define __iwl_fw_api_commands_h__

/**
 * enum iwl_mvm_command_groups - command groups for the firmware
 * @LEGACY_GROUP: legacy group, uses command IDs from &enum iwl_legacy_cmds
 * @LONG_GROUP: legacy group with long header, also uses command IDs
 *	from &enum iwl_legacy_cmds
 * @SYSTEM_GROUP: system group, uses command IDs from
 *	&enum iwl_system_subcmd_ids
 * @MAC_CONF_GROUP: MAC configuration group, uses command IDs from
 *	&enum iwl_mac_conf_subcmd_ids
 * @PHY_OPS_GROUP: PHY operations group, uses command IDs from
 *	&enum iwl_phy_ops_subcmd_ids
 * @DATA_PATH_GROUP: data path group, uses command IDs from
 *	&enum iwl_data_path_subcmd_ids
 * @NAN_GROUP: NAN group, uses command IDs from &enum iwl_nan_subcmd_ids
 * @LOCATION_GROUP: location group, uses command IDs from
 *	&enum iwl_location_subcmd_ids
 * @PROT_OFFLOAD_GROUP: protocol offload group, uses command IDs from
 *	&enum iwl_prot_offload_subcmd_ids
 * @REGULATORY_AND_NVM_GROUP: regulatory/NVM group, uses command IDs from
 *	&enum iwl_regulatory_and_nvm_subcmd_ids
 * @DEBUG_GROUP: Debug group, uses command IDs from &enum iwl_debug_cmds
 */
enum iwl_mvm_command_groups {
	LEGACY_GROUP = 0x0,
	LONG_GROUP = 0x1,
	SYSTEM_GROUP = 0x2,
	MAC_CONF_GROUP = 0x3,
	PHY_OPS_GROUP = 0x4,
	DATA_PATH_GROUP = 0x5,
	NAN_GROUP = 0x7,
	LOCATION_GROUP = 0x8,
	PROT_OFFLOAD_GROUP = 0xb,
	REGULATORY_AND_NVM_GROUP = 0xc,
	DEBUG_GROUP = 0xf,
};

/**
 * enum iwl_legacy_cmds - legacy group command IDs
 */
enum iwl_legacy_cmds {
	/**
	 * @MVM_ALIVE:
	 * Alive data from the firmware, as described in
	 * &struct mvm_alive_resp_v3 or &struct mvm_alive_resp.
	 */
	MVM_ALIVE = 0x1,

	/**
	 * @REPLY_ERROR: Cause an error in the firmware, for testing purposes.
	 */
	REPLY_ERROR = 0x2,

	/**
	 * @ECHO_CMD: Send data to the device to have it returned immediately.
	 */
	ECHO_CMD = 0x3,

	/**
	 * @INIT_COMPLETE_NOTIF: Notification that initialization is complete.
	 */
	INIT_COMPLETE_NOTIF = 0x4,

	/**
	 * @PHY_CONTEXT_CMD:
	 * Add/modify/remove a PHY context, using &struct iwl_phy_context_cmd.
	 */
	PHY_CONTEXT_CMD = 0x8,

	/**
	 * @DBG_CFG: Debug configuration command.
	 */
	DBG_CFG = 0x9,

	/**
	 * @SCAN_ITERATION_COMPLETE_UMAC:
	 * Firmware indicates a scan iteration completed, using
	 * &struct iwl_umac_scan_iter_complete_notif.
	 */
	SCAN_ITERATION_COMPLETE_UMAC = 0xb5,

	/**
	 * @SCAN_CFG_CMD:
	 * uses &struct iwl_scan_config_v1 or &struct iwl_scan_config
	 */
	SCAN_CFG_CMD = 0xc,

	/**
	 * @SCAN_REQ_UMAC: uses &struct iwl_scan_req_umac
	 */
	SCAN_REQ_UMAC = 0xd,

	/**
	 * @SCAN_ABORT_UMAC: uses &struct iwl_umac_scan_abort
	 */
	SCAN_ABORT_UMAC = 0xe,

	/**
	 * @SCAN_COMPLETE_UMAC: uses &struct iwl_umac_scan_complete
	 */
	SCAN_COMPLETE_UMAC = 0xf,

	/**
	 * @BA_WINDOW_STATUS_NOTIFICATION_ID:
	 * uses &struct iwl_ba_window_status_notif
	 */
	BA_WINDOW_STATUS_NOTIFICATION_ID = 0x13,

	/**
	 * @ADD_STA_KEY:
	 * &struct iwl_mvm_add_sta_key_cmd_v1 or
	 * &struct iwl_mvm_add_sta_key_cmd.
	 */
	ADD_STA_KEY = 0x17,

	/**
	 * @ADD_STA:
	 * &struct iwl_mvm_add_sta_cmd or &struct iwl_mvm_add_sta_cmd_v7.
	 */
	ADD_STA = 0x18,

	/**
	 * @REMOVE_STA: &struct iwl_mvm_rm_sta_cmd
	 */
	REMOVE_STA = 0x19,

	/**
	 * @FW_GET_ITEM_CMD: uses &struct iwl_fw_get_item_cmd
	 */
	FW_GET_ITEM_CMD = 0x1a,

	/**
	 * @TX_CMD: uses &struct iwl_tx_cmd or &struct iwl_tx_cmd_gen2 or
	 *	&struct iwl_tx_cmd_gen3,
	 *	response in &struct iwl_mvm_tx_resp or
	 *	&struct iwl_mvm_tx_resp_v3
	 */
	TX_CMD = 0x1c,

	/**
	 * @TXPATH_FLUSH: &struct iwl_tx_path_flush_cmd
	 */
	TXPATH_FLUSH = 0x1e,

	/**
	 * @MGMT_MCAST_KEY:
	 * &struct iwl_mvm_mgmt_mcast_key_cmd or
	 * &struct iwl_mvm_mgmt_mcast_key_cmd_v1
	 */
	MGMT_MCAST_KEY = 0x1f,

	/* scheduler config */
	/**
	 * @SCD_QUEUE_CFG: &struct iwl_scd_txq_cfg_cmd for older hardware,
	 *	&struct iwl_tx_queue_cfg_cmd with &struct iwl_tx_queue_cfg_rsp
	 *	for newer (22000) hardware.
	 */
	SCD_QUEUE_CFG = 0x1d,

	/**
	 * @WEP_KEY: uses &struct iwl_mvm_wep_key_cmd
	 */
	WEP_KEY = 0x20,

	/**
	 * @SHARED_MEM_CFG:
	 * retrieve shared memory configuration - response in
	 * &struct iwl_shared_mem_cfg
	 */
	SHARED_MEM_CFG = 0x25,

	/**
	 * @TDLS_CHANNEL_SWITCH_CMD: uses &struct iwl_tdls_channel_switch_cmd
	 */
	TDLS_CHANNEL_SWITCH_CMD = 0x27,

	/**
	 * @TDLS_CHANNEL_SWITCH_NOTIFICATION:
	 * uses &struct iwl_tdls_channel_switch_notif
	 */
	TDLS_CHANNEL_SWITCH_NOTIFICATION = 0xaa,

	/**
	 * @TDLS_CONFIG_CMD:
	 * &struct iwl_tdls_config_cmd, response in &struct iwl_tdls_config_res
	 */
	TDLS_CONFIG_CMD = 0xa7,

	/**
	 * @MAC_CONTEXT_CMD: &struct iwl_mac_ctx_cmd
	 */
	MAC_CONTEXT_CMD = 0x28,

	/**
	 * @TIME_EVENT_CMD:
	 * &struct iwl_time_event_cmd, response in &struct iwl_time_event_resp
	 */
	TIME_EVENT_CMD = 0x29, /* both CMD and response */

	/**
	 * @TIME_EVENT_NOTIFICATION: &struct iwl_time_event_notif
	 */
	TIME_EVENT_NOTIFICATION = 0x2a,

	/**
	 * @BINDING_CONTEXT_CMD:
	 * &struct iwl_binding_cmd or &struct iwl_binding_cmd_v1
	 */
	BINDING_CONTEXT_CMD = 0x2b,

	/**
	 * @TIME_QUOTA_CMD: &struct iwl_time_quota_cmd
	 */
	TIME_QUOTA_CMD = 0x2c,

	/**
	 * @NON_QOS_TX_COUNTER_CMD:
	 * command is &struct iwl_nonqos_seq_query_cmd
	 */
	NON_QOS_TX_COUNTER_CMD = 0x2d,

	/**
	 * @LEDS_CMD: command is &struct iwl_led_cmd
	 */
	LEDS_CMD = 0x48,

	/**
	 * @LQ_CMD: using &struct iwl_lq_cmd
	 */
	LQ_CMD = 0x4e,

	/**
	 * @FW_PAGING_BLOCK_CMD:
	 * &struct iwl_fw_paging_cmd
	 */
	FW_PAGING_BLOCK_CMD = 0x4f,

	/**
	 * @SCAN_OFFLOAD_REQUEST_CMD: uses &struct iwl_scan_req_lmac
	 */
	SCAN_OFFLOAD_REQUEST_CMD = 0x51,

	/**
	 * @SCAN_OFFLOAD_ABORT_CMD: abort the scan - no further contents
	 */
	SCAN_OFFLOAD_ABORT_CMD = 0x52,

	/**
	 * @HOT_SPOT_CMD: uses &struct iwl_hs20_roc_req
	 */
	HOT_SPOT_CMD = 0x53,

	/**
	 * @SCAN_OFFLOAD_COMPLETE:
	 * notification, &struct iwl_periodic_scan_complete
	 */
	SCAN_OFFLOAD_COMPLETE = 0x6D,

	/**
	 * @SCAN_OFFLOAD_UPDATE_PROFILES_CMD:
	 * update scan offload (scheduled scan) profiles/blacklist/etc.
	 */
	SCAN_OFFLOAD_UPDATE_PROFILES_CMD = 0x6E,

	/**
	 * @MATCH_FOUND_NOTIFICATION: scan match found
	 */
	MATCH_FOUND_NOTIFICATION = 0xd9,

	/**
	 * @SCAN_ITERATION_COMPLETE:
	 * uses &struct iwl_lmac_scan_complete_notif
	 */
	SCAN_ITERATION_COMPLETE = 0xe7,

	/* Phy */
	/**
	 * @PHY_CONFIGURATION_CMD: &struct iwl_phy_cfg_cmd
	 */
	PHY_CONFIGURATION_CMD = 0x6a,

	/**
	 * @CALIB_RES_NOTIF_PHY_DB: &struct iwl_calib_res_notif_phy_db
	 */
	CALIB_RES_NOTIF_PHY_DB = 0x6b,

	/**
	 * @PHY_DB_CMD: &struct iwl_phy_db_cmd
	 */
	PHY_DB_CMD = 0x6c,

	/**
	 * @POWER_TABLE_CMD: &struct iwl_device_power_cmd
	 */
	POWER_TABLE_CMD = 0x77,

	/**
	 * @PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION:
	 * &struct iwl_uapsd_misbehaving_ap_notif
	 */
	PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION = 0x78,

	/**
	 * @LTR_CONFIG: &struct iwl_ltr_config_cmd
	 */
	LTR_CONFIG = 0xee,

	/**
	 * @REPLY_THERMAL_MNG_BACKOFF:
	 * Thermal throttling command
	 */
	REPLY_THERMAL_MNG_BACKOFF = 0x7e,

	/**
	 * @DC2DC_CONFIG_CMD:
	 * Set/Get DC2DC frequency tune
	 * Command is &struct iwl_dc2dc_config_cmd,
	 * response is &struct iwl_dc2dc_config_resp
	 */
	DC2DC_CONFIG_CMD = 0x83,

	/**
	 * @NVM_ACCESS_CMD: using &struct iwl_nvm_access_cmd
	 */
	NVM_ACCESS_CMD = 0x88,

	/**
	 * @BEACON_NOTIFICATION: &struct iwl_extended_beacon_notif
	 */
	BEACON_NOTIFICATION = 0x90,

	/**
	 * @BEACON_TEMPLATE_CMD:
	 *	Uses one of &struct iwl_mac_beacon_cmd_v6,
	 *	&struct iwl_mac_beacon_cmd_v7 or &struct iwl_mac_beacon_cmd
	 *	depending on the device version.
	 */
	BEACON_TEMPLATE_CMD = 0x91,
	/**
	 * @TX_ANT_CONFIGURATION_CMD: &struct iwl_tx_ant_cfg_cmd
	 */
	TX_ANT_CONFIGURATION_CMD = 0x98,

	/**
	 * @STATISTICS_CMD:
	 * one of &struct iwl_statistics_cmd,
	 * &struct iwl_notif_statistics_v11,
	 * &struct iwl_notif_statistics_v10,
	 * &struct iwl_notif_statistics
	 */
	STATISTICS_CMD = 0x9c,

	/**
	 * @STATISTICS_NOTIFICATION:
	 * one of &struct iwl_notif_statistics_v10,
	 * &struct iwl_notif_statistics_v11,
	 * &struct iwl_notif_statistics
	 */
	STATISTICS_NOTIFICATION = 0x9d,

	/**
	 * @EOSP_NOTIFICATION:
	 * Notify that a service period ended,
	 * &struct iwl_mvm_eosp_notification
	 */
	EOSP_NOTIFICATION = 0x9e,

	/**
	 * @REDUCE_TX_POWER_CMD:
	 * &struct iwl_dev_tx_power_cmd_v3 or &struct iwl_dev_tx_power_cmd_v4
	 * or &struct iwl_dev_tx_power_cmd
	 */
	REDUCE_TX_POWER_CMD = 0x9f,

	/**
	 * @CARD_STATE_NOTIFICATION:
	 * Card state (RF/CT kill) notification,
	 * uses &struct iwl_card_state_notif
	 */
	CARD_STATE_NOTIFICATION = 0xa1,

	/**
	 * @MISSED_BEACONS_NOTIFICATION: &struct iwl_missed_beacons_notif
	 */
	MISSED_BEACONS_NOTIFICATION = 0xa2,

	/**
	 * @MAC_PM_POWER_TABLE: using &struct iwl_mac_power_cmd
	 */
	MAC_PM_POWER_TABLE = 0xa9,

	/**
	 * @MFUART_LOAD_NOTIFICATION: &struct iwl_mfuart_load_notif
	 */
	MFUART_LOAD_NOTIFICATION = 0xb1,

	/**
	 * @RSS_CONFIG_CMD: &struct iwl_rss_config_cmd
	 */
	RSS_CONFIG_CMD = 0xb3,

	/**
	 * @REPLY_RX_PHY_CMD: &struct iwl_rx_phy_info
	 */
	REPLY_RX_PHY_CMD = 0xc0,

	/**
	 * @REPLY_RX_MPDU_CMD:
	 * &struct iwl_rx_mpdu_res_start or &struct iwl_rx_mpdu_desc
	 */
	REPLY_RX_MPDU_CMD = 0xc1,

	/**
	 * @FRAME_RELEASE:
	 * Frame release (reorder helper) notification, uses
	 * &struct iwl_frame_release
	 */
	FRAME_RELEASE = 0xc3,

	/**
	 * @BA_NOTIF:
	 * BlockAck notification, uses &struct iwl_mvm_compressed_ba_notif
	 * or &struct iwl_mvm_ba_notif depending on the HW
	 */
	BA_NOTIF = 0xc5,

	/* Location Aware Regulatory */
	/**
	 * @MCC_UPDATE_CMD: using &struct iwl_mcc_update_cmd
	 */
	MCC_UPDATE_CMD = 0xc8,

	/**
	 * @MCC_CHUB_UPDATE_CMD: using &struct iwl_mcc_chub_notif
	 */
	MCC_CHUB_UPDATE_CMD = 0xc9,

	/**
	 * @MARKER_CMD: trace marker command, uses &struct iwl_mvm_marker
	 * with &struct iwl_mvm_marker_rsp
	 */
	MARKER_CMD = 0xcb,

	/**
	 * @BT_PROFILE_NOTIFICATION: &struct iwl_bt_coex_profile_notif
	 */
	BT_PROFILE_NOTIFICATION = 0xce,

	/**
	 * @BT_CONFIG: &struct iwl_bt_coex_cmd
	 */
	BT_CONFIG = 0x9b,

	/**
	 * @BT_COEX_UPDATE_REDUCED_TXP:
	 * &struct iwl_bt_coex_reduced_txp_update_cmd
	 */
	BT_COEX_UPDATE_REDUCED_TXP = 0x5c,

	/**
	 * @BT_COEX_CI: &struct iwl_bt_coex_ci_cmd
	 */
	BT_COEX_CI = 0x5d,

	/**
	 * @REPLY_SF_CFG_CMD: &struct iwl_sf_cfg_cmd
	 */
	REPLY_SF_CFG_CMD = 0xd1,
	/**
	 * @REPLY_BEACON_FILTERING_CMD: &struct iwl_beacon_filter_cmd
	 */
	REPLY_BEACON_FILTERING_CMD = 0xd2,

	/**
	 * @DTS_MEASUREMENT_NOTIFICATION:
	 * &struct iwl_dts_measurement_notif_v1 or
	 * &struct iwl_dts_measurement_notif_v2
	 */
	DTS_MEASUREMENT_NOTIFICATION = 0xdd,

	/**
	 * @LDBG_CONFIG_CMD: configure continuous trace recording
	 */
	LDBG_CONFIG_CMD = 0xf6,

	/**
	 * @DEBUG_LOG_MSG: Debugging log data from firmware
	 */
	DEBUG_LOG_MSG = 0xf7,

	/**
	 * @BCAST_FILTER_CMD: &struct iwl_bcast_filter_cmd
	 */
	BCAST_FILTER_CMD = 0xcf,

	/**
	 * @MCAST_FILTER_CMD: &struct iwl_mcast_filter_cmd
	 */
	MCAST_FILTER_CMD = 0xd0,

	/**
	 * @D3_CONFIG_CMD: &struct iwl_d3_manager_config
	 */
	D3_CONFIG_CMD = 0xd3,

	/**
	 * @PROT_OFFLOAD_CONFIG_CMD: Depending on firmware, uses one of
	 * &struct iwl_proto_offload_cmd_v1, &struct iwl_proto_offload_cmd_v2,
	 * &struct iwl_proto_offload_cmd_v3_small,
	 * &struct iwl_proto_offload_cmd_v3_large
	 */
	PROT_OFFLOAD_CONFIG_CMD = 0xd4,

	/**
	 * @OFFLOADS_QUERY_CMD:
	 * No data in command, response in &struct iwl_wowlan_status
	 */
	OFFLOADS_QUERY_CMD = 0xd5,

	/**
	 * @REMOTE_WAKE_CONFIG_CMD: &struct iwl_wowlan_remote_wake_config
	 */
	REMOTE_WAKE_CONFIG_CMD = 0xd6,

	/**
	 * @D0I3_END_CMD: End D0i3/D3 state, no command data
	 */
	D0I3_END_CMD = 0xed,

	/**
	 * @WOWLAN_PATTERNS: &struct iwl_wowlan_patterns_cmd
	 */
	WOWLAN_PATTERNS = 0xe0,

	/**
	 * @WOWLAN_CONFIGURATION: &struct iwl_wowlan_config_cmd
	 */
	WOWLAN_CONFIGURATION = 0xe1,

	/**
	 * @WOWLAN_TSC_RSC_PARAM: &struct iwl_wowlan_rsc_tsc_params_cmd
	 */
	WOWLAN_TSC_RSC_PARAM = 0xe2,

	/**
	 * @WOWLAN_TKIP_PARAM: &struct iwl_wowlan_tkip_params_cmd
	 */
	WOWLAN_TKIP_PARAM = 0xe3,

	/**
	 * @WOWLAN_KEK_KCK_MATERIAL: &struct iwl_wowlan_kek_kck_material_cmd
	 */
	WOWLAN_KEK_KCK_MATERIAL = 0xe4,

	/**
	 * @WOWLAN_GET_STATUSES: response in &struct iwl_wowlan_status
	 */
	WOWLAN_GET_STATUSES = 0xe5,

	/**
	 * @SCAN_OFFLOAD_PROFILES_QUERY_CMD:
	 * No command data, response is &struct iwl_scan_offload_profiles_query
	 */
	SCAN_OFFLOAD_PROFILES_QUERY_CMD = 0x56,
};

/**
 * enum iwl_system_subcmd_ids - system group command IDs
 */
enum iwl_system_subcmd_ids {
	/**
	 * @SHARED_MEM_CFG_CMD:
	 * response in &struct iwl_shared_mem_cfg or
	 * &struct iwl_shared_mem_cfg_v2
	 */
	SHARED_MEM_CFG_CMD = 0x0,

	/**
	 * @INIT_EXTENDED_CFG_CMD: &struct iwl_init_extended_cfg_cmd
	 */
	INIT_EXTENDED_CFG_CMD = 0x03,

	/**
	 * @FW_ERROR_RECOVERY_CMD: &struct iwl_fw_error_recovery_cmd
	 */
	FW_ERROR_RECOVERY_CMD = 0x7,
};

#endif /* __iwl_fw_api_commands_h__ */
