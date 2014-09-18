/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/skbuff.h>
#include <linux/ctype.h>

#include "core.h"
#include "htc.h"
#include "debug.h"
#include "wmi.h"
#include "mac.h"
#include "testmode.h"

/* MAIN WMI cmd track */
static struct wmi_cmd_map wmi_cmd_map = {
	.init_cmdid = WMI_INIT_CMDID,
	.start_scan_cmdid = WMI_START_SCAN_CMDID,
	.stop_scan_cmdid = WMI_STOP_SCAN_CMDID,
	.scan_chan_list_cmdid = WMI_SCAN_CHAN_LIST_CMDID,
	.scan_sch_prio_tbl_cmdid = WMI_SCAN_SCH_PRIO_TBL_CMDID,
	.pdev_set_regdomain_cmdid = WMI_PDEV_SET_REGDOMAIN_CMDID,
	.pdev_set_channel_cmdid = WMI_PDEV_SET_CHANNEL_CMDID,
	.pdev_set_param_cmdid = WMI_PDEV_SET_PARAM_CMDID,
	.pdev_pktlog_enable_cmdid = WMI_PDEV_PKTLOG_ENABLE_CMDID,
	.pdev_pktlog_disable_cmdid = WMI_PDEV_PKTLOG_DISABLE_CMDID,
	.pdev_set_wmm_params_cmdid = WMI_PDEV_SET_WMM_PARAMS_CMDID,
	.pdev_set_ht_cap_ie_cmdid = WMI_PDEV_SET_HT_CAP_IE_CMDID,
	.pdev_set_vht_cap_ie_cmdid = WMI_PDEV_SET_VHT_CAP_IE_CMDID,
	.pdev_set_dscp_tid_map_cmdid = WMI_PDEV_SET_DSCP_TID_MAP_CMDID,
	.pdev_set_quiet_mode_cmdid = WMI_PDEV_SET_QUIET_MODE_CMDID,
	.pdev_green_ap_ps_enable_cmdid = WMI_PDEV_GREEN_AP_PS_ENABLE_CMDID,
	.pdev_get_tpc_config_cmdid = WMI_PDEV_GET_TPC_CONFIG_CMDID,
	.pdev_set_base_macaddr_cmdid = WMI_PDEV_SET_BASE_MACADDR_CMDID,
	.vdev_create_cmdid = WMI_VDEV_CREATE_CMDID,
	.vdev_delete_cmdid = WMI_VDEV_DELETE_CMDID,
	.vdev_start_request_cmdid = WMI_VDEV_START_REQUEST_CMDID,
	.vdev_restart_request_cmdid = WMI_VDEV_RESTART_REQUEST_CMDID,
	.vdev_up_cmdid = WMI_VDEV_UP_CMDID,
	.vdev_stop_cmdid = WMI_VDEV_STOP_CMDID,
	.vdev_down_cmdid = WMI_VDEV_DOWN_CMDID,
	.vdev_set_param_cmdid = WMI_VDEV_SET_PARAM_CMDID,
	.vdev_install_key_cmdid = WMI_VDEV_INSTALL_KEY_CMDID,
	.peer_create_cmdid = WMI_PEER_CREATE_CMDID,
	.peer_delete_cmdid = WMI_PEER_DELETE_CMDID,
	.peer_flush_tids_cmdid = WMI_PEER_FLUSH_TIDS_CMDID,
	.peer_set_param_cmdid = WMI_PEER_SET_PARAM_CMDID,
	.peer_assoc_cmdid = WMI_PEER_ASSOC_CMDID,
	.peer_add_wds_entry_cmdid = WMI_PEER_ADD_WDS_ENTRY_CMDID,
	.peer_remove_wds_entry_cmdid = WMI_PEER_REMOVE_WDS_ENTRY_CMDID,
	.peer_mcast_group_cmdid = WMI_PEER_MCAST_GROUP_CMDID,
	.bcn_tx_cmdid = WMI_BCN_TX_CMDID,
	.pdev_send_bcn_cmdid = WMI_PDEV_SEND_BCN_CMDID,
	.bcn_tmpl_cmdid = WMI_BCN_TMPL_CMDID,
	.bcn_filter_rx_cmdid = WMI_BCN_FILTER_RX_CMDID,
	.prb_req_filter_rx_cmdid = WMI_PRB_REQ_FILTER_RX_CMDID,
	.mgmt_tx_cmdid = WMI_MGMT_TX_CMDID,
	.prb_tmpl_cmdid = WMI_PRB_TMPL_CMDID,
	.addba_clear_resp_cmdid = WMI_ADDBA_CLEAR_RESP_CMDID,
	.addba_send_cmdid = WMI_ADDBA_SEND_CMDID,
	.addba_status_cmdid = WMI_ADDBA_STATUS_CMDID,
	.delba_send_cmdid = WMI_DELBA_SEND_CMDID,
	.addba_set_resp_cmdid = WMI_ADDBA_SET_RESP_CMDID,
	.send_singleamsdu_cmdid = WMI_SEND_SINGLEAMSDU_CMDID,
	.sta_powersave_mode_cmdid = WMI_STA_POWERSAVE_MODE_CMDID,
	.sta_powersave_param_cmdid = WMI_STA_POWERSAVE_PARAM_CMDID,
	.sta_mimo_ps_mode_cmdid = WMI_STA_MIMO_PS_MODE_CMDID,
	.pdev_dfs_enable_cmdid = WMI_PDEV_DFS_ENABLE_CMDID,
	.pdev_dfs_disable_cmdid = WMI_PDEV_DFS_DISABLE_CMDID,
	.roam_scan_mode = WMI_ROAM_SCAN_MODE,
	.roam_scan_rssi_threshold = WMI_ROAM_SCAN_RSSI_THRESHOLD,
	.roam_scan_period = WMI_ROAM_SCAN_PERIOD,
	.roam_scan_rssi_change_threshold = WMI_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
	.roam_ap_profile = WMI_ROAM_AP_PROFILE,
	.ofl_scan_add_ap_profile = WMI_ROAM_AP_PROFILE,
	.ofl_scan_remove_ap_profile = WMI_OFL_SCAN_REMOVE_AP_PROFILE,
	.ofl_scan_period = WMI_OFL_SCAN_PERIOD,
	.p2p_dev_set_device_info = WMI_P2P_DEV_SET_DEVICE_INFO,
	.p2p_dev_set_discoverability = WMI_P2P_DEV_SET_DISCOVERABILITY,
	.p2p_go_set_beacon_ie = WMI_P2P_GO_SET_BEACON_IE,
	.p2p_go_set_probe_resp_ie = WMI_P2P_GO_SET_PROBE_RESP_IE,
	.p2p_set_vendor_ie_data_cmdid = WMI_P2P_SET_VENDOR_IE_DATA_CMDID,
	.ap_ps_peer_param_cmdid = WMI_AP_PS_PEER_PARAM_CMDID,
	.ap_ps_peer_uapsd_coex_cmdid = WMI_AP_PS_PEER_UAPSD_COEX_CMDID,
	.peer_rate_retry_sched_cmdid = WMI_PEER_RATE_RETRY_SCHED_CMDID,
	.wlan_profile_trigger_cmdid = WMI_WLAN_PROFILE_TRIGGER_CMDID,
	.wlan_profile_set_hist_intvl_cmdid =
				WMI_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
	.wlan_profile_get_profile_data_cmdid =
				WMI_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
	.wlan_profile_enable_profile_id_cmdid =
				WMI_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
	.wlan_profile_list_profile_id_cmdid =
				WMI_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,
	.pdev_suspend_cmdid = WMI_PDEV_SUSPEND_CMDID,
	.pdev_resume_cmdid = WMI_PDEV_RESUME_CMDID,
	.add_bcn_filter_cmdid = WMI_ADD_BCN_FILTER_CMDID,
	.rmv_bcn_filter_cmdid = WMI_RMV_BCN_FILTER_CMDID,
	.wow_add_wake_pattern_cmdid = WMI_WOW_ADD_WAKE_PATTERN_CMDID,
	.wow_del_wake_pattern_cmdid = WMI_WOW_DEL_WAKE_PATTERN_CMDID,
	.wow_enable_disable_wake_event_cmdid =
				WMI_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
	.wow_enable_cmdid = WMI_WOW_ENABLE_CMDID,
	.wow_hostwakeup_from_sleep_cmdid = WMI_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,
	.rtt_measreq_cmdid = WMI_RTT_MEASREQ_CMDID,
	.rtt_tsf_cmdid = WMI_RTT_TSF_CMDID,
	.vdev_spectral_scan_configure_cmdid =
				WMI_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID,
	.vdev_spectral_scan_enable_cmdid = WMI_VDEV_SPECTRAL_SCAN_ENABLE_CMDID,
	.request_stats_cmdid = WMI_REQUEST_STATS_CMDID,
	.set_arp_ns_offload_cmdid = WMI_SET_ARP_NS_OFFLOAD_CMDID,
	.network_list_offload_config_cmdid =
				WMI_NETWORK_LIST_OFFLOAD_CONFIG_CMDID,
	.gtk_offload_cmdid = WMI_GTK_OFFLOAD_CMDID,
	.csa_offload_enable_cmdid = WMI_CSA_OFFLOAD_ENABLE_CMDID,
	.csa_offload_chanswitch_cmdid = WMI_CSA_OFFLOAD_CHANSWITCH_CMDID,
	.chatter_set_mode_cmdid = WMI_CHATTER_SET_MODE_CMDID,
	.peer_tid_addba_cmdid = WMI_PEER_TID_ADDBA_CMDID,
	.peer_tid_delba_cmdid = WMI_PEER_TID_DELBA_CMDID,
	.sta_dtim_ps_method_cmdid = WMI_STA_DTIM_PS_METHOD_CMDID,
	.sta_uapsd_auto_trig_cmdid = WMI_STA_UAPSD_AUTO_TRIG_CMDID,
	.sta_keepalive_cmd = WMI_STA_KEEPALIVE_CMD,
	.echo_cmdid = WMI_ECHO_CMDID,
	.pdev_utf_cmdid = WMI_PDEV_UTF_CMDID,
	.dbglog_cfg_cmdid = WMI_DBGLOG_CFG_CMDID,
	.pdev_qvit_cmdid = WMI_PDEV_QVIT_CMDID,
	.pdev_ftm_intg_cmdid = WMI_PDEV_FTM_INTG_CMDID,
	.vdev_set_keepalive_cmdid = WMI_VDEV_SET_KEEPALIVE_CMDID,
	.vdev_get_keepalive_cmdid = WMI_VDEV_GET_KEEPALIVE_CMDID,
	.force_fw_hang_cmdid = WMI_FORCE_FW_HANG_CMDID,
	.gpio_config_cmdid = WMI_GPIO_CONFIG_CMDID,
	.gpio_output_cmdid = WMI_GPIO_OUTPUT_CMDID,
};

/* 10.X WMI cmd track */
static struct wmi_cmd_map wmi_10x_cmd_map = {
	.init_cmdid = WMI_10X_INIT_CMDID,
	.start_scan_cmdid = WMI_10X_START_SCAN_CMDID,
	.stop_scan_cmdid = WMI_10X_STOP_SCAN_CMDID,
	.scan_chan_list_cmdid = WMI_10X_SCAN_CHAN_LIST_CMDID,
	.scan_sch_prio_tbl_cmdid = WMI_CMD_UNSUPPORTED,
	.pdev_set_regdomain_cmdid = WMI_10X_PDEV_SET_REGDOMAIN_CMDID,
	.pdev_set_channel_cmdid = WMI_10X_PDEV_SET_CHANNEL_CMDID,
	.pdev_set_param_cmdid = WMI_10X_PDEV_SET_PARAM_CMDID,
	.pdev_pktlog_enable_cmdid = WMI_10X_PDEV_PKTLOG_ENABLE_CMDID,
	.pdev_pktlog_disable_cmdid = WMI_10X_PDEV_PKTLOG_DISABLE_CMDID,
	.pdev_set_wmm_params_cmdid = WMI_10X_PDEV_SET_WMM_PARAMS_CMDID,
	.pdev_set_ht_cap_ie_cmdid = WMI_10X_PDEV_SET_HT_CAP_IE_CMDID,
	.pdev_set_vht_cap_ie_cmdid = WMI_10X_PDEV_SET_VHT_CAP_IE_CMDID,
	.pdev_set_dscp_tid_map_cmdid = WMI_10X_PDEV_SET_DSCP_TID_MAP_CMDID,
	.pdev_set_quiet_mode_cmdid = WMI_10X_PDEV_SET_QUIET_MODE_CMDID,
	.pdev_green_ap_ps_enable_cmdid = WMI_10X_PDEV_GREEN_AP_PS_ENABLE_CMDID,
	.pdev_get_tpc_config_cmdid = WMI_10X_PDEV_GET_TPC_CONFIG_CMDID,
	.pdev_set_base_macaddr_cmdid = WMI_10X_PDEV_SET_BASE_MACADDR_CMDID,
	.vdev_create_cmdid = WMI_10X_VDEV_CREATE_CMDID,
	.vdev_delete_cmdid = WMI_10X_VDEV_DELETE_CMDID,
	.vdev_start_request_cmdid = WMI_10X_VDEV_START_REQUEST_CMDID,
	.vdev_restart_request_cmdid = WMI_10X_VDEV_RESTART_REQUEST_CMDID,
	.vdev_up_cmdid = WMI_10X_VDEV_UP_CMDID,
	.vdev_stop_cmdid = WMI_10X_VDEV_STOP_CMDID,
	.vdev_down_cmdid = WMI_10X_VDEV_DOWN_CMDID,
	.vdev_set_param_cmdid = WMI_10X_VDEV_SET_PARAM_CMDID,
	.vdev_install_key_cmdid = WMI_10X_VDEV_INSTALL_KEY_CMDID,
	.peer_create_cmdid = WMI_10X_PEER_CREATE_CMDID,
	.peer_delete_cmdid = WMI_10X_PEER_DELETE_CMDID,
	.peer_flush_tids_cmdid = WMI_10X_PEER_FLUSH_TIDS_CMDID,
	.peer_set_param_cmdid = WMI_10X_PEER_SET_PARAM_CMDID,
	.peer_assoc_cmdid = WMI_10X_PEER_ASSOC_CMDID,
	.peer_add_wds_entry_cmdid = WMI_10X_PEER_ADD_WDS_ENTRY_CMDID,
	.peer_remove_wds_entry_cmdid = WMI_10X_PEER_REMOVE_WDS_ENTRY_CMDID,
	.peer_mcast_group_cmdid = WMI_10X_PEER_MCAST_GROUP_CMDID,
	.bcn_tx_cmdid = WMI_10X_BCN_TX_CMDID,
	.pdev_send_bcn_cmdid = WMI_10X_PDEV_SEND_BCN_CMDID,
	.bcn_tmpl_cmdid = WMI_CMD_UNSUPPORTED,
	.bcn_filter_rx_cmdid = WMI_10X_BCN_FILTER_RX_CMDID,
	.prb_req_filter_rx_cmdid = WMI_10X_PRB_REQ_FILTER_RX_CMDID,
	.mgmt_tx_cmdid = WMI_10X_MGMT_TX_CMDID,
	.prb_tmpl_cmdid = WMI_CMD_UNSUPPORTED,
	.addba_clear_resp_cmdid = WMI_10X_ADDBA_CLEAR_RESP_CMDID,
	.addba_send_cmdid = WMI_10X_ADDBA_SEND_CMDID,
	.addba_status_cmdid = WMI_10X_ADDBA_STATUS_CMDID,
	.delba_send_cmdid = WMI_10X_DELBA_SEND_CMDID,
	.addba_set_resp_cmdid = WMI_10X_ADDBA_SET_RESP_CMDID,
	.send_singleamsdu_cmdid = WMI_10X_SEND_SINGLEAMSDU_CMDID,
	.sta_powersave_mode_cmdid = WMI_10X_STA_POWERSAVE_MODE_CMDID,
	.sta_powersave_param_cmdid = WMI_10X_STA_POWERSAVE_PARAM_CMDID,
	.sta_mimo_ps_mode_cmdid = WMI_10X_STA_MIMO_PS_MODE_CMDID,
	.pdev_dfs_enable_cmdid = WMI_10X_PDEV_DFS_ENABLE_CMDID,
	.pdev_dfs_disable_cmdid = WMI_10X_PDEV_DFS_DISABLE_CMDID,
	.roam_scan_mode = WMI_10X_ROAM_SCAN_MODE,
	.roam_scan_rssi_threshold = WMI_10X_ROAM_SCAN_RSSI_THRESHOLD,
	.roam_scan_period = WMI_10X_ROAM_SCAN_PERIOD,
	.roam_scan_rssi_change_threshold =
				WMI_10X_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
	.roam_ap_profile = WMI_10X_ROAM_AP_PROFILE,
	.ofl_scan_add_ap_profile = WMI_10X_OFL_SCAN_ADD_AP_PROFILE,
	.ofl_scan_remove_ap_profile = WMI_10X_OFL_SCAN_REMOVE_AP_PROFILE,
	.ofl_scan_period = WMI_10X_OFL_SCAN_PERIOD,
	.p2p_dev_set_device_info = WMI_10X_P2P_DEV_SET_DEVICE_INFO,
	.p2p_dev_set_discoverability = WMI_10X_P2P_DEV_SET_DISCOVERABILITY,
	.p2p_go_set_beacon_ie = WMI_10X_P2P_GO_SET_BEACON_IE,
	.p2p_go_set_probe_resp_ie = WMI_10X_P2P_GO_SET_PROBE_RESP_IE,
	.p2p_set_vendor_ie_data_cmdid = WMI_CMD_UNSUPPORTED,
	.ap_ps_peer_param_cmdid = WMI_10X_AP_PS_PEER_PARAM_CMDID,
	.ap_ps_peer_uapsd_coex_cmdid = WMI_CMD_UNSUPPORTED,
	.peer_rate_retry_sched_cmdid = WMI_10X_PEER_RATE_RETRY_SCHED_CMDID,
	.wlan_profile_trigger_cmdid = WMI_10X_WLAN_PROFILE_TRIGGER_CMDID,
	.wlan_profile_set_hist_intvl_cmdid =
				WMI_10X_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
	.wlan_profile_get_profile_data_cmdid =
				WMI_10X_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
	.wlan_profile_enable_profile_id_cmdid =
				WMI_10X_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
	.wlan_profile_list_profile_id_cmdid =
				WMI_10X_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,
	.pdev_suspend_cmdid = WMI_10X_PDEV_SUSPEND_CMDID,
	.pdev_resume_cmdid = WMI_10X_PDEV_RESUME_CMDID,
	.add_bcn_filter_cmdid = WMI_10X_ADD_BCN_FILTER_CMDID,
	.rmv_bcn_filter_cmdid = WMI_10X_RMV_BCN_FILTER_CMDID,
	.wow_add_wake_pattern_cmdid = WMI_10X_WOW_ADD_WAKE_PATTERN_CMDID,
	.wow_del_wake_pattern_cmdid = WMI_10X_WOW_DEL_WAKE_PATTERN_CMDID,
	.wow_enable_disable_wake_event_cmdid =
				WMI_10X_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
	.wow_enable_cmdid = WMI_10X_WOW_ENABLE_CMDID,
	.wow_hostwakeup_from_sleep_cmdid =
				WMI_10X_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,
	.rtt_measreq_cmdid = WMI_10X_RTT_MEASREQ_CMDID,
	.rtt_tsf_cmdid = WMI_10X_RTT_TSF_CMDID,
	.vdev_spectral_scan_configure_cmdid =
				WMI_10X_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID,
	.vdev_spectral_scan_enable_cmdid =
				WMI_10X_VDEV_SPECTRAL_SCAN_ENABLE_CMDID,
	.request_stats_cmdid = WMI_10X_REQUEST_STATS_CMDID,
	.set_arp_ns_offload_cmdid = WMI_CMD_UNSUPPORTED,
	.network_list_offload_config_cmdid = WMI_CMD_UNSUPPORTED,
	.gtk_offload_cmdid = WMI_CMD_UNSUPPORTED,
	.csa_offload_enable_cmdid = WMI_CMD_UNSUPPORTED,
	.csa_offload_chanswitch_cmdid = WMI_CMD_UNSUPPORTED,
	.chatter_set_mode_cmdid = WMI_CMD_UNSUPPORTED,
	.peer_tid_addba_cmdid = WMI_CMD_UNSUPPORTED,
	.peer_tid_delba_cmdid = WMI_CMD_UNSUPPORTED,
	.sta_dtim_ps_method_cmdid = WMI_CMD_UNSUPPORTED,
	.sta_uapsd_auto_trig_cmdid = WMI_CMD_UNSUPPORTED,
	.sta_keepalive_cmd = WMI_CMD_UNSUPPORTED,
	.echo_cmdid = WMI_10X_ECHO_CMDID,
	.pdev_utf_cmdid = WMI_10X_PDEV_UTF_CMDID,
	.dbglog_cfg_cmdid = WMI_10X_DBGLOG_CFG_CMDID,
	.pdev_qvit_cmdid = WMI_10X_PDEV_QVIT_CMDID,
	.pdev_ftm_intg_cmdid = WMI_CMD_UNSUPPORTED,
	.vdev_set_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
	.vdev_get_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
	.force_fw_hang_cmdid = WMI_CMD_UNSUPPORTED,
	.gpio_config_cmdid = WMI_10X_GPIO_CONFIG_CMDID,
	.gpio_output_cmdid = WMI_10X_GPIO_OUTPUT_CMDID,
};

/* MAIN WMI VDEV param map */
static struct wmi_vdev_param_map wmi_vdev_param_map = {
	.rts_threshold = WMI_VDEV_PARAM_RTS_THRESHOLD,
	.fragmentation_threshold = WMI_VDEV_PARAM_FRAGMENTATION_THRESHOLD,
	.beacon_interval = WMI_VDEV_PARAM_BEACON_INTERVAL,
	.listen_interval = WMI_VDEV_PARAM_LISTEN_INTERVAL,
	.multicast_rate = WMI_VDEV_PARAM_MULTICAST_RATE,
	.mgmt_tx_rate = WMI_VDEV_PARAM_MGMT_TX_RATE,
	.slot_time = WMI_VDEV_PARAM_SLOT_TIME,
	.preamble = WMI_VDEV_PARAM_PREAMBLE,
	.swba_time = WMI_VDEV_PARAM_SWBA_TIME,
	.wmi_vdev_stats_update_period = WMI_VDEV_STATS_UPDATE_PERIOD,
	.wmi_vdev_pwrsave_ageout_time = WMI_VDEV_PWRSAVE_AGEOUT_TIME,
	.wmi_vdev_host_swba_interval = WMI_VDEV_HOST_SWBA_INTERVAL,
	.dtim_period = WMI_VDEV_PARAM_DTIM_PERIOD,
	.wmi_vdev_oc_scheduler_air_time_limit =
					WMI_VDEV_OC_SCHEDULER_AIR_TIME_LIMIT,
	.wds = WMI_VDEV_PARAM_WDS,
	.atim_window = WMI_VDEV_PARAM_ATIM_WINDOW,
	.bmiss_count_max = WMI_VDEV_PARAM_BMISS_COUNT_MAX,
	.bmiss_first_bcnt = WMI_VDEV_PARAM_BMISS_FIRST_BCNT,
	.bmiss_final_bcnt = WMI_VDEV_PARAM_BMISS_FINAL_BCNT,
	.feature_wmm = WMI_VDEV_PARAM_FEATURE_WMM,
	.chwidth = WMI_VDEV_PARAM_CHWIDTH,
	.chextoffset = WMI_VDEV_PARAM_CHEXTOFFSET,
	.disable_htprotection =	WMI_VDEV_PARAM_DISABLE_HTPROTECTION,
	.sta_quickkickout = WMI_VDEV_PARAM_STA_QUICKKICKOUT,
	.mgmt_rate = WMI_VDEV_PARAM_MGMT_RATE,
	.protection_mode = WMI_VDEV_PARAM_PROTECTION_MODE,
	.fixed_rate = WMI_VDEV_PARAM_FIXED_RATE,
	.sgi = WMI_VDEV_PARAM_SGI,
	.ldpc = WMI_VDEV_PARAM_LDPC,
	.tx_stbc = WMI_VDEV_PARAM_TX_STBC,
	.rx_stbc = WMI_VDEV_PARAM_RX_STBC,
	.intra_bss_fwd = WMI_VDEV_PARAM_INTRA_BSS_FWD,
	.def_keyid = WMI_VDEV_PARAM_DEF_KEYID,
	.nss = WMI_VDEV_PARAM_NSS,
	.bcast_data_rate = WMI_VDEV_PARAM_BCAST_DATA_RATE,
	.mcast_data_rate = WMI_VDEV_PARAM_MCAST_DATA_RATE,
	.mcast_indicate = WMI_VDEV_PARAM_MCAST_INDICATE,
	.dhcp_indicate = WMI_VDEV_PARAM_DHCP_INDICATE,
	.unknown_dest_indicate = WMI_VDEV_PARAM_UNKNOWN_DEST_INDICATE,
	.ap_keepalive_min_idle_inactive_time_secs =
			WMI_VDEV_PARAM_AP_KEEPALIVE_MIN_IDLE_INACTIVE_TIME_SECS,
	.ap_keepalive_max_idle_inactive_time_secs =
			WMI_VDEV_PARAM_AP_KEEPALIVE_MAX_IDLE_INACTIVE_TIME_SECS,
	.ap_keepalive_max_unresponsive_time_secs =
			WMI_VDEV_PARAM_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS,
	.ap_enable_nawds = WMI_VDEV_PARAM_AP_ENABLE_NAWDS,
	.mcast2ucast_set = WMI_VDEV_PARAM_UNSUPPORTED,
	.enable_rtscts = WMI_VDEV_PARAM_ENABLE_RTSCTS,
	.txbf = WMI_VDEV_PARAM_TXBF,
	.packet_powersave = WMI_VDEV_PARAM_PACKET_POWERSAVE,
	.drop_unencry = WMI_VDEV_PARAM_DROP_UNENCRY,
	.tx_encap_type = WMI_VDEV_PARAM_TX_ENCAP_TYPE,
	.ap_detect_out_of_sync_sleeping_sta_time_secs =
					WMI_VDEV_PARAM_UNSUPPORTED,
};

/* 10.X WMI VDEV param map */
static struct wmi_vdev_param_map wmi_10x_vdev_param_map = {
	.rts_threshold = WMI_10X_VDEV_PARAM_RTS_THRESHOLD,
	.fragmentation_threshold = WMI_10X_VDEV_PARAM_FRAGMENTATION_THRESHOLD,
	.beacon_interval = WMI_10X_VDEV_PARAM_BEACON_INTERVAL,
	.listen_interval = WMI_10X_VDEV_PARAM_LISTEN_INTERVAL,
	.multicast_rate = WMI_10X_VDEV_PARAM_MULTICAST_RATE,
	.mgmt_tx_rate = WMI_10X_VDEV_PARAM_MGMT_TX_RATE,
	.slot_time = WMI_10X_VDEV_PARAM_SLOT_TIME,
	.preamble = WMI_10X_VDEV_PARAM_PREAMBLE,
	.swba_time = WMI_10X_VDEV_PARAM_SWBA_TIME,
	.wmi_vdev_stats_update_period = WMI_10X_VDEV_STATS_UPDATE_PERIOD,
	.wmi_vdev_pwrsave_ageout_time = WMI_10X_VDEV_PWRSAVE_AGEOUT_TIME,
	.wmi_vdev_host_swba_interval = WMI_10X_VDEV_HOST_SWBA_INTERVAL,
	.dtim_period = WMI_10X_VDEV_PARAM_DTIM_PERIOD,
	.wmi_vdev_oc_scheduler_air_time_limit =
				WMI_10X_VDEV_OC_SCHEDULER_AIR_TIME_LIMIT,
	.wds = WMI_10X_VDEV_PARAM_WDS,
	.atim_window = WMI_10X_VDEV_PARAM_ATIM_WINDOW,
	.bmiss_count_max = WMI_10X_VDEV_PARAM_BMISS_COUNT_MAX,
	.bmiss_first_bcnt = WMI_VDEV_PARAM_UNSUPPORTED,
	.bmiss_final_bcnt = WMI_VDEV_PARAM_UNSUPPORTED,
	.feature_wmm = WMI_10X_VDEV_PARAM_FEATURE_WMM,
	.chwidth = WMI_10X_VDEV_PARAM_CHWIDTH,
	.chextoffset = WMI_10X_VDEV_PARAM_CHEXTOFFSET,
	.disable_htprotection = WMI_10X_VDEV_PARAM_DISABLE_HTPROTECTION,
	.sta_quickkickout = WMI_10X_VDEV_PARAM_STA_QUICKKICKOUT,
	.mgmt_rate = WMI_10X_VDEV_PARAM_MGMT_RATE,
	.protection_mode = WMI_10X_VDEV_PARAM_PROTECTION_MODE,
	.fixed_rate = WMI_10X_VDEV_PARAM_FIXED_RATE,
	.sgi = WMI_10X_VDEV_PARAM_SGI,
	.ldpc = WMI_10X_VDEV_PARAM_LDPC,
	.tx_stbc = WMI_10X_VDEV_PARAM_TX_STBC,
	.rx_stbc = WMI_10X_VDEV_PARAM_RX_STBC,
	.intra_bss_fwd = WMI_10X_VDEV_PARAM_INTRA_BSS_FWD,
	.def_keyid = WMI_10X_VDEV_PARAM_DEF_KEYID,
	.nss = WMI_10X_VDEV_PARAM_NSS,
	.bcast_data_rate = WMI_10X_VDEV_PARAM_BCAST_DATA_RATE,
	.mcast_data_rate = WMI_10X_VDEV_PARAM_MCAST_DATA_RATE,
	.mcast_indicate = WMI_10X_VDEV_PARAM_MCAST_INDICATE,
	.dhcp_indicate = WMI_10X_VDEV_PARAM_DHCP_INDICATE,
	.unknown_dest_indicate = WMI_10X_VDEV_PARAM_UNKNOWN_DEST_INDICATE,
	.ap_keepalive_min_idle_inactive_time_secs =
		WMI_10X_VDEV_PARAM_AP_KEEPALIVE_MIN_IDLE_INACTIVE_TIME_SECS,
	.ap_keepalive_max_idle_inactive_time_secs =
		WMI_10X_VDEV_PARAM_AP_KEEPALIVE_MAX_IDLE_INACTIVE_TIME_SECS,
	.ap_keepalive_max_unresponsive_time_secs =
		WMI_10X_VDEV_PARAM_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS,
	.ap_enable_nawds = WMI_10X_VDEV_PARAM_AP_ENABLE_NAWDS,
	.mcast2ucast_set = WMI_10X_VDEV_PARAM_MCAST2UCAST_SET,
	.enable_rtscts = WMI_10X_VDEV_PARAM_ENABLE_RTSCTS,
	.txbf = WMI_VDEV_PARAM_UNSUPPORTED,
	.packet_powersave = WMI_VDEV_PARAM_UNSUPPORTED,
	.drop_unencry = WMI_VDEV_PARAM_UNSUPPORTED,
	.tx_encap_type = WMI_VDEV_PARAM_UNSUPPORTED,
	.ap_detect_out_of_sync_sleeping_sta_time_secs =
		WMI_10X_VDEV_PARAM_AP_DETECT_OUT_OF_SYNC_SLEEPING_STA_TIME_SECS,
};

static struct wmi_pdev_param_map wmi_pdev_param_map = {
	.tx_chain_mask = WMI_PDEV_PARAM_TX_CHAIN_MASK,
	.rx_chain_mask = WMI_PDEV_PARAM_RX_CHAIN_MASK,
	.txpower_limit2g = WMI_PDEV_PARAM_TXPOWER_LIMIT2G,
	.txpower_limit5g = WMI_PDEV_PARAM_TXPOWER_LIMIT5G,
	.txpower_scale = WMI_PDEV_PARAM_TXPOWER_SCALE,
	.beacon_gen_mode = WMI_PDEV_PARAM_BEACON_GEN_MODE,
	.beacon_tx_mode = WMI_PDEV_PARAM_BEACON_TX_MODE,
	.resmgr_offchan_mode = WMI_PDEV_PARAM_RESMGR_OFFCHAN_MODE,
	.protection_mode = WMI_PDEV_PARAM_PROTECTION_MODE,
	.dynamic_bw = WMI_PDEV_PARAM_DYNAMIC_BW,
	.non_agg_sw_retry_th = WMI_PDEV_PARAM_NON_AGG_SW_RETRY_TH,
	.agg_sw_retry_th = WMI_PDEV_PARAM_AGG_SW_RETRY_TH,
	.sta_kickout_th = WMI_PDEV_PARAM_STA_KICKOUT_TH,
	.ac_aggrsize_scaling = WMI_PDEV_PARAM_AC_AGGRSIZE_SCALING,
	.ltr_enable = WMI_PDEV_PARAM_LTR_ENABLE,
	.ltr_ac_latency_be = WMI_PDEV_PARAM_LTR_AC_LATENCY_BE,
	.ltr_ac_latency_bk = WMI_PDEV_PARAM_LTR_AC_LATENCY_BK,
	.ltr_ac_latency_vi = WMI_PDEV_PARAM_LTR_AC_LATENCY_VI,
	.ltr_ac_latency_vo = WMI_PDEV_PARAM_LTR_AC_LATENCY_VO,
	.ltr_ac_latency_timeout = WMI_PDEV_PARAM_LTR_AC_LATENCY_TIMEOUT,
	.ltr_sleep_override = WMI_PDEV_PARAM_LTR_SLEEP_OVERRIDE,
	.ltr_rx_override = WMI_PDEV_PARAM_LTR_RX_OVERRIDE,
	.ltr_tx_activity_timeout = WMI_PDEV_PARAM_LTR_TX_ACTIVITY_TIMEOUT,
	.l1ss_enable = WMI_PDEV_PARAM_L1SS_ENABLE,
	.dsleep_enable = WMI_PDEV_PARAM_DSLEEP_ENABLE,
	.pcielp_txbuf_flush = WMI_PDEV_PARAM_PCIELP_TXBUF_FLUSH,
	.pcielp_txbuf_watermark = WMI_PDEV_PARAM_PCIELP_TXBUF_TMO_EN,
	.pcielp_txbuf_tmo_en = WMI_PDEV_PARAM_PCIELP_TXBUF_TMO_EN,
	.pcielp_txbuf_tmo_value = WMI_PDEV_PARAM_PCIELP_TXBUF_TMO_VALUE,
	.pdev_stats_update_period = WMI_PDEV_PARAM_PDEV_STATS_UPDATE_PERIOD,
	.vdev_stats_update_period = WMI_PDEV_PARAM_VDEV_STATS_UPDATE_PERIOD,
	.peer_stats_update_period = WMI_PDEV_PARAM_PEER_STATS_UPDATE_PERIOD,
	.bcnflt_stats_update_period = WMI_PDEV_PARAM_BCNFLT_STATS_UPDATE_PERIOD,
	.pmf_qos = WMI_PDEV_PARAM_PMF_QOS,
	.arp_ac_override = WMI_PDEV_PARAM_ARP_AC_OVERRIDE,
	.dcs = WMI_PDEV_PARAM_DCS,
	.ani_enable = WMI_PDEV_PARAM_ANI_ENABLE,
	.ani_poll_period = WMI_PDEV_PARAM_ANI_POLL_PERIOD,
	.ani_listen_period = WMI_PDEV_PARAM_ANI_LISTEN_PERIOD,
	.ani_ofdm_level = WMI_PDEV_PARAM_ANI_OFDM_LEVEL,
	.ani_cck_level = WMI_PDEV_PARAM_ANI_CCK_LEVEL,
	.dyntxchain = WMI_PDEV_PARAM_DYNTXCHAIN,
	.proxy_sta = WMI_PDEV_PARAM_PROXY_STA,
	.idle_ps_config = WMI_PDEV_PARAM_IDLE_PS_CONFIG,
	.power_gating_sleep = WMI_PDEV_PARAM_POWER_GATING_SLEEP,
	.fast_channel_reset = WMI_PDEV_PARAM_UNSUPPORTED,
	.burst_dur = WMI_PDEV_PARAM_UNSUPPORTED,
	.burst_enable = WMI_PDEV_PARAM_UNSUPPORTED,
};

static struct wmi_pdev_param_map wmi_10x_pdev_param_map = {
	.tx_chain_mask = WMI_10X_PDEV_PARAM_TX_CHAIN_MASK,
	.rx_chain_mask = WMI_10X_PDEV_PARAM_RX_CHAIN_MASK,
	.txpower_limit2g = WMI_10X_PDEV_PARAM_TXPOWER_LIMIT2G,
	.txpower_limit5g = WMI_10X_PDEV_PARAM_TXPOWER_LIMIT5G,
	.txpower_scale = WMI_10X_PDEV_PARAM_TXPOWER_SCALE,
	.beacon_gen_mode = WMI_10X_PDEV_PARAM_BEACON_GEN_MODE,
	.beacon_tx_mode = WMI_10X_PDEV_PARAM_BEACON_TX_MODE,
	.resmgr_offchan_mode = WMI_10X_PDEV_PARAM_RESMGR_OFFCHAN_MODE,
	.protection_mode = WMI_10X_PDEV_PARAM_PROTECTION_MODE,
	.dynamic_bw = WMI_10X_PDEV_PARAM_DYNAMIC_BW,
	.non_agg_sw_retry_th = WMI_10X_PDEV_PARAM_NON_AGG_SW_RETRY_TH,
	.agg_sw_retry_th = WMI_10X_PDEV_PARAM_AGG_SW_RETRY_TH,
	.sta_kickout_th = WMI_10X_PDEV_PARAM_STA_KICKOUT_TH,
	.ac_aggrsize_scaling = WMI_10X_PDEV_PARAM_AC_AGGRSIZE_SCALING,
	.ltr_enable = WMI_10X_PDEV_PARAM_LTR_ENABLE,
	.ltr_ac_latency_be = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_BE,
	.ltr_ac_latency_bk = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_BK,
	.ltr_ac_latency_vi = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_VI,
	.ltr_ac_latency_vo = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_VO,
	.ltr_ac_latency_timeout = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_TIMEOUT,
	.ltr_sleep_override = WMI_10X_PDEV_PARAM_LTR_SLEEP_OVERRIDE,
	.ltr_rx_override = WMI_10X_PDEV_PARAM_LTR_RX_OVERRIDE,
	.ltr_tx_activity_timeout = WMI_10X_PDEV_PARAM_LTR_TX_ACTIVITY_TIMEOUT,
	.l1ss_enable = WMI_10X_PDEV_PARAM_L1SS_ENABLE,
	.dsleep_enable = WMI_10X_PDEV_PARAM_DSLEEP_ENABLE,
	.pcielp_txbuf_flush = WMI_PDEV_PARAM_UNSUPPORTED,
	.pcielp_txbuf_watermark = WMI_PDEV_PARAM_UNSUPPORTED,
	.pcielp_txbuf_tmo_en = WMI_PDEV_PARAM_UNSUPPORTED,
	.pcielp_txbuf_tmo_value = WMI_PDEV_PARAM_UNSUPPORTED,
	.pdev_stats_update_period = WMI_10X_PDEV_PARAM_PDEV_STATS_UPDATE_PERIOD,
	.vdev_stats_update_period = WMI_10X_PDEV_PARAM_VDEV_STATS_UPDATE_PERIOD,
	.peer_stats_update_period = WMI_10X_PDEV_PARAM_PEER_STATS_UPDATE_PERIOD,
	.bcnflt_stats_update_period =
				WMI_10X_PDEV_PARAM_BCNFLT_STATS_UPDATE_PERIOD,
	.pmf_qos = WMI_10X_PDEV_PARAM_PMF_QOS,
	.arp_ac_override = WMI_10X_PDEV_PARAM_ARPDHCP_AC_OVERRIDE,
	.dcs = WMI_10X_PDEV_PARAM_DCS,
	.ani_enable = WMI_10X_PDEV_PARAM_ANI_ENABLE,
	.ani_poll_period = WMI_10X_PDEV_PARAM_ANI_POLL_PERIOD,
	.ani_listen_period = WMI_10X_PDEV_PARAM_ANI_LISTEN_PERIOD,
	.ani_ofdm_level = WMI_10X_PDEV_PARAM_ANI_OFDM_LEVEL,
	.ani_cck_level = WMI_10X_PDEV_PARAM_ANI_CCK_LEVEL,
	.dyntxchain = WMI_10X_PDEV_PARAM_DYNTXCHAIN,
	.proxy_sta = WMI_PDEV_PARAM_UNSUPPORTED,
	.idle_ps_config = WMI_PDEV_PARAM_UNSUPPORTED,
	.power_gating_sleep = WMI_PDEV_PARAM_UNSUPPORTED,
	.fast_channel_reset = WMI_10X_PDEV_PARAM_FAST_CHANNEL_RESET,
	.burst_dur = WMI_10X_PDEV_PARAM_BURST_DUR,
	.burst_enable = WMI_10X_PDEV_PARAM_BURST_ENABLE,
};

/* firmware 10.2 specific mappings */
static struct wmi_cmd_map wmi_10_2_cmd_map = {
	.init_cmdid = WMI_10_2_INIT_CMDID,
	.start_scan_cmdid = WMI_10_2_START_SCAN_CMDID,
	.stop_scan_cmdid = WMI_10_2_STOP_SCAN_CMDID,
	.scan_chan_list_cmdid = WMI_10_2_SCAN_CHAN_LIST_CMDID,
	.scan_sch_prio_tbl_cmdid = WMI_CMD_UNSUPPORTED,
	.pdev_set_regdomain_cmdid = WMI_10_2_PDEV_SET_REGDOMAIN_CMDID,
	.pdev_set_channel_cmdid = WMI_10_2_PDEV_SET_CHANNEL_CMDID,
	.pdev_set_param_cmdid = WMI_10_2_PDEV_SET_PARAM_CMDID,
	.pdev_pktlog_enable_cmdid = WMI_10_2_PDEV_PKTLOG_ENABLE_CMDID,
	.pdev_pktlog_disable_cmdid = WMI_10_2_PDEV_PKTLOG_DISABLE_CMDID,
	.pdev_set_wmm_params_cmdid = WMI_10_2_PDEV_SET_WMM_PARAMS_CMDID,
	.pdev_set_ht_cap_ie_cmdid = WMI_10_2_PDEV_SET_HT_CAP_IE_CMDID,
	.pdev_set_vht_cap_ie_cmdid = WMI_10_2_PDEV_SET_VHT_CAP_IE_CMDID,
	.pdev_set_quiet_mode_cmdid = WMI_10_2_PDEV_SET_QUIET_MODE_CMDID,
	.pdev_green_ap_ps_enable_cmdid = WMI_10_2_PDEV_GREEN_AP_PS_ENABLE_CMDID,
	.pdev_get_tpc_config_cmdid = WMI_10_2_PDEV_GET_TPC_CONFIG_CMDID,
	.pdev_set_base_macaddr_cmdid = WMI_10_2_PDEV_SET_BASE_MACADDR_CMDID,
	.vdev_create_cmdid = WMI_10_2_VDEV_CREATE_CMDID,
	.vdev_delete_cmdid = WMI_10_2_VDEV_DELETE_CMDID,
	.vdev_start_request_cmdid = WMI_10_2_VDEV_START_REQUEST_CMDID,
	.vdev_restart_request_cmdid = WMI_10_2_VDEV_RESTART_REQUEST_CMDID,
	.vdev_up_cmdid = WMI_10_2_VDEV_UP_CMDID,
	.vdev_stop_cmdid = WMI_10_2_VDEV_STOP_CMDID,
	.vdev_down_cmdid = WMI_10_2_VDEV_DOWN_CMDID,
	.vdev_set_param_cmdid = WMI_10_2_VDEV_SET_PARAM_CMDID,
	.vdev_install_key_cmdid = WMI_10_2_VDEV_INSTALL_KEY_CMDID,
	.peer_create_cmdid = WMI_10_2_PEER_CREATE_CMDID,
	.peer_delete_cmdid = WMI_10_2_PEER_DELETE_CMDID,
	.peer_flush_tids_cmdid = WMI_10_2_PEER_FLUSH_TIDS_CMDID,
	.peer_set_param_cmdid = WMI_10_2_PEER_SET_PARAM_CMDID,
	.peer_assoc_cmdid = WMI_10_2_PEER_ASSOC_CMDID,
	.peer_add_wds_entry_cmdid = WMI_10_2_PEER_ADD_WDS_ENTRY_CMDID,
	.peer_remove_wds_entry_cmdid = WMI_10_2_PEER_REMOVE_WDS_ENTRY_CMDID,
	.peer_mcast_group_cmdid = WMI_10_2_PEER_MCAST_GROUP_CMDID,
	.bcn_tx_cmdid = WMI_10_2_BCN_TX_CMDID,
	.pdev_send_bcn_cmdid = WMI_10_2_PDEV_SEND_BCN_CMDID,
	.bcn_tmpl_cmdid = WMI_CMD_UNSUPPORTED,
	.bcn_filter_rx_cmdid = WMI_10_2_BCN_FILTER_RX_CMDID,
	.prb_req_filter_rx_cmdid = WMI_10_2_PRB_REQ_FILTER_RX_CMDID,
	.mgmt_tx_cmdid = WMI_10_2_MGMT_TX_CMDID,
	.prb_tmpl_cmdid = WMI_CMD_UNSUPPORTED,
	.addba_clear_resp_cmdid = WMI_10_2_ADDBA_CLEAR_RESP_CMDID,
	.addba_send_cmdid = WMI_10_2_ADDBA_SEND_CMDID,
	.addba_status_cmdid = WMI_10_2_ADDBA_STATUS_CMDID,
	.delba_send_cmdid = WMI_10_2_DELBA_SEND_CMDID,
	.addba_set_resp_cmdid = WMI_10_2_ADDBA_SET_RESP_CMDID,
	.send_singleamsdu_cmdid = WMI_10_2_SEND_SINGLEAMSDU_CMDID,
	.sta_powersave_mode_cmdid = WMI_10_2_STA_POWERSAVE_MODE_CMDID,
	.sta_powersave_param_cmdid = WMI_10_2_STA_POWERSAVE_PARAM_CMDID,
	.sta_mimo_ps_mode_cmdid = WMI_10_2_STA_MIMO_PS_MODE_CMDID,
	.pdev_dfs_enable_cmdid = WMI_10_2_PDEV_DFS_ENABLE_CMDID,
	.pdev_dfs_disable_cmdid = WMI_10_2_PDEV_DFS_DISABLE_CMDID,
	.roam_scan_mode = WMI_10_2_ROAM_SCAN_MODE,
	.roam_scan_rssi_threshold = WMI_10_2_ROAM_SCAN_RSSI_THRESHOLD,
	.roam_scan_period = WMI_10_2_ROAM_SCAN_PERIOD,
	.roam_scan_rssi_change_threshold =
				WMI_10_2_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
	.roam_ap_profile = WMI_10_2_ROAM_AP_PROFILE,
	.ofl_scan_add_ap_profile = WMI_10_2_OFL_SCAN_ADD_AP_PROFILE,
	.ofl_scan_remove_ap_profile = WMI_10_2_OFL_SCAN_REMOVE_AP_PROFILE,
	.ofl_scan_period = WMI_10_2_OFL_SCAN_PERIOD,
	.p2p_dev_set_device_info = WMI_10_2_P2P_DEV_SET_DEVICE_INFO,
	.p2p_dev_set_discoverability = WMI_10_2_P2P_DEV_SET_DISCOVERABILITY,
	.p2p_go_set_beacon_ie = WMI_10_2_P2P_GO_SET_BEACON_IE,
	.p2p_go_set_probe_resp_ie = WMI_10_2_P2P_GO_SET_PROBE_RESP_IE,
	.p2p_set_vendor_ie_data_cmdid = WMI_CMD_UNSUPPORTED,
	.ap_ps_peer_param_cmdid = WMI_10_2_AP_PS_PEER_PARAM_CMDID,
	.ap_ps_peer_uapsd_coex_cmdid = WMI_CMD_UNSUPPORTED,
	.peer_rate_retry_sched_cmdid = WMI_10_2_PEER_RATE_RETRY_SCHED_CMDID,
	.wlan_profile_trigger_cmdid = WMI_10_2_WLAN_PROFILE_TRIGGER_CMDID,
	.wlan_profile_set_hist_intvl_cmdid =
				WMI_10_2_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
	.wlan_profile_get_profile_data_cmdid =
				WMI_10_2_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
	.wlan_profile_enable_profile_id_cmdid =
				WMI_10_2_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
	.wlan_profile_list_profile_id_cmdid =
				WMI_10_2_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,
	.pdev_suspend_cmdid = WMI_10_2_PDEV_SUSPEND_CMDID,
	.pdev_resume_cmdid = WMI_10_2_PDEV_RESUME_CMDID,
	.add_bcn_filter_cmdid = WMI_10_2_ADD_BCN_FILTER_CMDID,
	.rmv_bcn_filter_cmdid = WMI_10_2_RMV_BCN_FILTER_CMDID,
	.wow_add_wake_pattern_cmdid = WMI_10_2_WOW_ADD_WAKE_PATTERN_CMDID,
	.wow_del_wake_pattern_cmdid = WMI_10_2_WOW_DEL_WAKE_PATTERN_CMDID,
	.wow_enable_disable_wake_event_cmdid =
				WMI_10_2_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
	.wow_enable_cmdid = WMI_10_2_WOW_ENABLE_CMDID,
	.wow_hostwakeup_from_sleep_cmdid =
				WMI_10_2_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,
	.rtt_measreq_cmdid = WMI_10_2_RTT_MEASREQ_CMDID,
	.rtt_tsf_cmdid = WMI_10_2_RTT_TSF_CMDID,
	.vdev_spectral_scan_configure_cmdid =
				WMI_10_2_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID,
	.vdev_spectral_scan_enable_cmdid =
				WMI_10_2_VDEV_SPECTRAL_SCAN_ENABLE_CMDID,
	.request_stats_cmdid = WMI_10_2_REQUEST_STATS_CMDID,
	.set_arp_ns_offload_cmdid = WMI_CMD_UNSUPPORTED,
	.network_list_offload_config_cmdid = WMI_CMD_UNSUPPORTED,
	.gtk_offload_cmdid = WMI_CMD_UNSUPPORTED,
	.csa_offload_enable_cmdid = WMI_CMD_UNSUPPORTED,
	.csa_offload_chanswitch_cmdid = WMI_CMD_UNSUPPORTED,
	.chatter_set_mode_cmdid = WMI_CMD_UNSUPPORTED,
	.peer_tid_addba_cmdid = WMI_CMD_UNSUPPORTED,
	.peer_tid_delba_cmdid = WMI_CMD_UNSUPPORTED,
	.sta_dtim_ps_method_cmdid = WMI_CMD_UNSUPPORTED,
	.sta_uapsd_auto_trig_cmdid = WMI_CMD_UNSUPPORTED,
	.sta_keepalive_cmd = WMI_CMD_UNSUPPORTED,
	.echo_cmdid = WMI_10_2_ECHO_CMDID,
	.pdev_utf_cmdid = WMI_10_2_PDEV_UTF_CMDID,
	.dbglog_cfg_cmdid = WMI_10_2_DBGLOG_CFG_CMDID,
	.pdev_qvit_cmdid = WMI_10_2_PDEV_QVIT_CMDID,
	.pdev_ftm_intg_cmdid = WMI_CMD_UNSUPPORTED,
	.vdev_set_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
	.vdev_get_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
	.force_fw_hang_cmdid = WMI_CMD_UNSUPPORTED,
	.gpio_config_cmdid = WMI_10_2_GPIO_CONFIG_CMDID,
	.gpio_output_cmdid = WMI_10_2_GPIO_OUTPUT_CMDID,
};

static void
ath10k_wmi_put_wmi_channel(struct wmi_channel *ch,
			   const struct wmi_channel_arg *arg)
{
	u32 flags = 0;

	memset(ch, 0, sizeof(*ch));

	if (arg->passive)
		flags |= WMI_CHAN_FLAG_PASSIVE;
	if (arg->allow_ibss)
		flags |= WMI_CHAN_FLAG_ADHOC_ALLOWED;
	if (arg->allow_ht)
		flags |= WMI_CHAN_FLAG_ALLOW_HT;
	if (arg->allow_vht)
		flags |= WMI_CHAN_FLAG_ALLOW_VHT;
	if (arg->ht40plus)
		flags |= WMI_CHAN_FLAG_HT40_PLUS;
	if (arg->chan_radar)
		flags |= WMI_CHAN_FLAG_DFS;

	ch->mhz = __cpu_to_le32(arg->freq);
	ch->band_center_freq1 = __cpu_to_le32(arg->band_center_freq1);
	ch->band_center_freq2 = 0;
	ch->min_power = arg->min_power;
	ch->max_power = arg->max_power;
	ch->reg_power = arg->max_reg_power;
	ch->antenna_max = arg->max_antenna_gain;

	/* mode & flags share storage */
	ch->mode = arg->mode;
	ch->flags |= __cpu_to_le32(flags);
}

int ath10k_wmi_wait_for_service_ready(struct ath10k *ar)
{
	int ret;

	ret = wait_for_completion_timeout(&ar->wmi.service_ready,
					  WMI_SERVICE_READY_TIMEOUT_HZ);
	return ret;
}

int ath10k_wmi_wait_for_unified_ready(struct ath10k *ar)
{
	int ret;

	ret = wait_for_completion_timeout(&ar->wmi.unified_ready,
					  WMI_UNIFIED_READY_TIMEOUT_HZ);
	return ret;
}

struct sk_buff *ath10k_wmi_alloc_skb(struct ath10k *ar, u32 len)
{
	struct sk_buff *skb;
	u32 round_len = roundup(len, 4);

	skb = ath10k_htc_alloc_skb(ar, WMI_SKB_HEADROOM + round_len);
	if (!skb)
		return NULL;

	skb_reserve(skb, WMI_SKB_HEADROOM);
	if (!IS_ALIGNED((unsigned long)skb->data, 4))
		ath10k_warn(ar, "Unaligned WMI skb\n");

	skb_put(skb, round_len);
	memset(skb->data, 0, round_len);

	return skb;
}

static void ath10k_wmi_htc_tx_complete(struct ath10k *ar, struct sk_buff *skb)
{
	dev_kfree_skb(skb);
}

static int ath10k_wmi_cmd_send_nowait(struct ath10k *ar, struct sk_buff *skb,
				      u32 cmd_id)
{
	struct ath10k_skb_cb *skb_cb = ATH10K_SKB_CB(skb);
	struct wmi_cmd_hdr *cmd_hdr;
	int ret;
	u32 cmd = 0;

	if (skb_push(skb, sizeof(struct wmi_cmd_hdr)) == NULL)
		return -ENOMEM;

	cmd |= SM(cmd_id, WMI_CMD_HDR_CMD_ID);

	cmd_hdr = (struct wmi_cmd_hdr *)skb->data;
	cmd_hdr->cmd_id = __cpu_to_le32(cmd);

	memset(skb_cb, 0, sizeof(*skb_cb));
	ret = ath10k_htc_send(&ar->htc, ar->wmi.eid, skb);
	trace_ath10k_wmi_cmd(ar, cmd_id, skb->data, skb->len, ret);

	if (ret)
		goto err_pull;

	return 0;

err_pull:
	skb_pull(skb, sizeof(struct wmi_cmd_hdr));
	return ret;
}

static void ath10k_wmi_tx_beacon_nowait(struct ath10k_vif *arvif)
{
	int ret;

	lockdep_assert_held(&arvif->ar->data_lock);

	if (arvif->beacon == NULL)
		return;

	if (arvif->beacon_sent)
		return;

	ret = ath10k_wmi_beacon_send_ref_nowait(arvif);
	if (ret)
		return;

	/* We need to retain the arvif->beacon reference for DMA unmapping and
	 * freeing the skbuff later. */
	arvif->beacon_sent = true;
}

static void ath10k_wmi_tx_beacons_iter(void *data, u8 *mac,
				       struct ieee80211_vif *vif)
{
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);

	ath10k_wmi_tx_beacon_nowait(arvif);
}

static void ath10k_wmi_tx_beacons_nowait(struct ath10k *ar)
{
	spin_lock_bh(&ar->data_lock);
	ieee80211_iterate_active_interfaces_atomic(ar->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   ath10k_wmi_tx_beacons_iter,
						   NULL);
	spin_unlock_bh(&ar->data_lock);
}

static void ath10k_wmi_op_ep_tx_credits(struct ath10k *ar)
{
	/* try to send pending beacons first. they take priority */
	ath10k_wmi_tx_beacons_nowait(ar);

	wake_up(&ar->wmi.tx_credits_wq);
}

int ath10k_wmi_cmd_send(struct ath10k *ar, struct sk_buff *skb, u32 cmd_id)
{
	int ret = -EOPNOTSUPP;

	might_sleep();

	if (cmd_id == WMI_CMD_UNSUPPORTED) {
		ath10k_warn(ar, "wmi command %d is not supported by firmware\n",
			    cmd_id);
		return ret;
	}

	wait_event_timeout(ar->wmi.tx_credits_wq, ({
		/* try to send pending beacons first. they take priority */
		ath10k_wmi_tx_beacons_nowait(ar);

		ret = ath10k_wmi_cmd_send_nowait(ar, skb, cmd_id);
		(ret != -EAGAIN);
	}), 3*HZ);

	if (ret)
		dev_kfree_skb_any(skb);

	return ret;
}

int ath10k_wmi_mgmt_tx(struct ath10k *ar, struct sk_buff *skb)
{
	int ret = 0;
	struct wmi_mgmt_tx_cmd *cmd;
	struct ieee80211_hdr *hdr;
	struct sk_buff *wmi_skb;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int len;
	u32 buf_len = skb->len;
	u16 fc;

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = le16_to_cpu(hdr->frame_control);

	if (WARN_ON_ONCE(!ieee80211_is_mgmt(hdr->frame_control)))
		return -EINVAL;

	len = sizeof(cmd->hdr) + skb->len;

	if ((ieee80211_is_action(hdr->frame_control) ||
	     ieee80211_is_deauth(hdr->frame_control) ||
	     ieee80211_is_disassoc(hdr->frame_control)) &&
	     ieee80211_has_protected(hdr->frame_control)) {
		len += IEEE80211_CCMP_MIC_LEN;
		buf_len += IEEE80211_CCMP_MIC_LEN;
	}

	len = round_up(len, 4);

	wmi_skb = ath10k_wmi_alloc_skb(ar, len);
	if (!wmi_skb)
		return -ENOMEM;

	cmd = (struct wmi_mgmt_tx_cmd *)wmi_skb->data;

	cmd->hdr.vdev_id = __cpu_to_le32(ATH10K_SKB_CB(skb)->vdev_id);
	cmd->hdr.tx_rate = 0;
	cmd->hdr.tx_power = 0;
	cmd->hdr.buf_len = __cpu_to_le32(buf_len);

	ether_addr_copy(cmd->hdr.peer_macaddr.addr, ieee80211_get_DA(hdr));
	memcpy(cmd->buf, skb->data, skb->len);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi mgmt tx skb %p len %d ftype %02x stype %02x\n",
		   wmi_skb, wmi_skb->len, fc & IEEE80211_FCTL_FTYPE,
		   fc & IEEE80211_FCTL_STYPE);

	/* Send the management frame buffer to the target */
	ret = ath10k_wmi_cmd_send(ar, wmi_skb, ar->wmi.cmd->mgmt_tx_cmdid);
	if (ret)
		return ret;

	/* TODO: report tx status to mac80211 - temporary just ACK */
	info->flags |= IEEE80211_TX_STAT_ACK;
	ieee80211_tx_status_irqsafe(ar->hw, skb);

	return ret;
}

static void ath10k_wmi_event_scan_started(struct ath10k *ar)
{
	lockdep_assert_held(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH10K_SCAN_IDLE:
	case ATH10K_SCAN_RUNNING:
	case ATH10K_SCAN_ABORTING:
		ath10k_warn(ar, "received scan started event in an invalid scan state: %s (%d)\n",
			    ath10k_scan_state_str(ar->scan.state),
			    ar->scan.state);
		break;
	case ATH10K_SCAN_STARTING:
		ar->scan.state = ATH10K_SCAN_RUNNING;

		if (ar->scan.is_roc)
			ieee80211_ready_on_channel(ar->hw);

		complete(&ar->scan.started);
		break;
	}
}

static void ath10k_wmi_event_scan_completed(struct ath10k *ar)
{
	lockdep_assert_held(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH10K_SCAN_IDLE:
	case ATH10K_SCAN_STARTING:
		/* One suspected reason scan can be completed while starting is
		 * if firmware fails to deliver all scan events to the host,
		 * e.g. when transport pipe is full. This has been observed
		 * with spectral scan phyerr events starving wmi transport
		 * pipe. In such case the "scan completed" event should be (and
		 * is) ignored by the host as it may be just firmware's scan
		 * state machine recovering.
		 */
		ath10k_warn(ar, "received scan completed event in an invalid scan state: %s (%d)\n",
			    ath10k_scan_state_str(ar->scan.state),
			    ar->scan.state);
		break;
	case ATH10K_SCAN_RUNNING:
	case ATH10K_SCAN_ABORTING:
		__ath10k_scan_finish(ar);
		break;
	}
}

static void ath10k_wmi_event_scan_bss_chan(struct ath10k *ar)
{
	lockdep_assert_held(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH10K_SCAN_IDLE:
	case ATH10K_SCAN_STARTING:
		ath10k_warn(ar, "received scan bss chan event in an invalid scan state: %s (%d)\n",
			    ath10k_scan_state_str(ar->scan.state),
			    ar->scan.state);
		break;
	case ATH10K_SCAN_RUNNING:
	case ATH10K_SCAN_ABORTING:
		ar->scan_channel = NULL;
		break;
	}
}

static void ath10k_wmi_event_scan_foreign_chan(struct ath10k *ar, u32 freq)
{
	lockdep_assert_held(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH10K_SCAN_IDLE:
	case ATH10K_SCAN_STARTING:
		ath10k_warn(ar, "received scan foreign chan event in an invalid scan state: %s (%d)\n",
			    ath10k_scan_state_str(ar->scan.state),
			    ar->scan.state);
		break;
	case ATH10K_SCAN_RUNNING:
	case ATH10K_SCAN_ABORTING:
		ar->scan_channel = ieee80211_get_channel(ar->hw->wiphy, freq);

		if (ar->scan.is_roc && ar->scan.roc_freq == freq)
			complete(&ar->scan.on_channel);
		break;
	}
}

static const char *
ath10k_wmi_event_scan_type_str(enum wmi_scan_event_type type,
			       enum wmi_scan_completion_reason reason)
{
	switch (type) {
	case WMI_SCAN_EVENT_STARTED:
		return "started";
	case WMI_SCAN_EVENT_COMPLETED:
		switch (reason) {
		case WMI_SCAN_REASON_COMPLETED:
			return "completed";
		case WMI_SCAN_REASON_CANCELLED:
			return "completed [cancelled]";
		case WMI_SCAN_REASON_PREEMPTED:
			return "completed [preempted]";
		case WMI_SCAN_REASON_TIMEDOUT:
			return "completed [timedout]";
		case WMI_SCAN_REASON_MAX:
			break;
		}
		return "completed [unknown]";
	case WMI_SCAN_EVENT_BSS_CHANNEL:
		return "bss channel";
	case WMI_SCAN_EVENT_FOREIGN_CHANNEL:
		return "foreign channel";
	case WMI_SCAN_EVENT_DEQUEUED:
		return "dequeued";
	case WMI_SCAN_EVENT_PREEMPTED:
		return "preempted";
	case WMI_SCAN_EVENT_START_FAILED:
		return "start failed";
	default:
		return "unknown";
	}
}

static int ath10k_wmi_event_scan(struct ath10k *ar, struct sk_buff *skb)
{
	struct wmi_scan_event *event = (struct wmi_scan_event *)skb->data;
	enum wmi_scan_event_type event_type;
	enum wmi_scan_completion_reason reason;
	u32 freq;
	u32 req_id;
	u32 scan_id;
	u32 vdev_id;

	event_type = __le32_to_cpu(event->event_type);
	reason     = __le32_to_cpu(event->reason);
	freq       = __le32_to_cpu(event->channel_freq);
	req_id     = __le32_to_cpu(event->scan_req_id);
	scan_id    = __le32_to_cpu(event->scan_id);
	vdev_id    = __le32_to_cpu(event->vdev_id);

	spin_lock_bh(&ar->data_lock);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "scan event %s type %d reason %d freq %d req_id %d scan_id %d vdev_id %d state %s (%d)\n",
		   ath10k_wmi_event_scan_type_str(event_type, reason),
		   event_type, reason, freq, req_id, scan_id, vdev_id,
		   ath10k_scan_state_str(ar->scan.state), ar->scan.state);

	switch (event_type) {
	case WMI_SCAN_EVENT_STARTED:
		ath10k_wmi_event_scan_started(ar);
		break;
	case WMI_SCAN_EVENT_COMPLETED:
		ath10k_wmi_event_scan_completed(ar);
		break;
	case WMI_SCAN_EVENT_BSS_CHANNEL:
		ath10k_wmi_event_scan_bss_chan(ar);
		break;
	case WMI_SCAN_EVENT_FOREIGN_CHANNEL:
		ath10k_wmi_event_scan_foreign_chan(ar, freq);
		break;
	case WMI_SCAN_EVENT_START_FAILED:
		ath10k_warn(ar, "received scan start failure event\n");
		break;
	case WMI_SCAN_EVENT_DEQUEUED:
	case WMI_SCAN_EVENT_PREEMPTED:
	default:
		break;
	}

	spin_unlock_bh(&ar->data_lock);
	return 0;
}

static inline enum ieee80211_band phy_mode_to_band(u32 phy_mode)
{
	enum ieee80211_band band;

	switch (phy_mode) {
	case MODE_11A:
	case MODE_11NA_HT20:
	case MODE_11NA_HT40:
	case MODE_11AC_VHT20:
	case MODE_11AC_VHT40:
	case MODE_11AC_VHT80:
		band = IEEE80211_BAND_5GHZ;
		break;
	case MODE_11G:
	case MODE_11B:
	case MODE_11GONLY:
	case MODE_11NG_HT20:
	case MODE_11NG_HT40:
	case MODE_11AC_VHT20_2G:
	case MODE_11AC_VHT40_2G:
	case MODE_11AC_VHT80_2G:
	default:
		band = IEEE80211_BAND_2GHZ;
	}

	return band;
}

static inline u8 get_rate_idx(u32 rate, enum ieee80211_band band)
{
	u8 rate_idx = 0;

	/* rate in Kbps */
	switch (rate) {
	case 1000:
		rate_idx = 0;
		break;
	case 2000:
		rate_idx = 1;
		break;
	case 5500:
		rate_idx = 2;
		break;
	case 11000:
		rate_idx = 3;
		break;
	case 6000:
		rate_idx = 4;
		break;
	case 9000:
		rate_idx = 5;
		break;
	case 12000:
		rate_idx = 6;
		break;
	case 18000:
		rate_idx = 7;
		break;
	case 24000:
		rate_idx = 8;
		break;
	case 36000:
		rate_idx = 9;
		break;
	case 48000:
		rate_idx = 10;
		break;
	case 54000:
		rate_idx = 11;
		break;
	default:
		break;
	}

	if (band == IEEE80211_BAND_5GHZ) {
		if (rate_idx > 3)
			/* Omit CCK rates */
			rate_idx -= 4;
		else
			rate_idx = 0;
	}

	return rate_idx;
}

static int ath10k_wmi_event_mgmt_rx(struct ath10k *ar, struct sk_buff *skb)
{
	struct wmi_mgmt_rx_event_v1 *ev_v1;
	struct wmi_mgmt_rx_event_v2 *ev_v2;
	struct wmi_mgmt_rx_hdr_v1 *ev_hdr;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_channel *ch;
	struct ieee80211_hdr *hdr;
	u32 rx_status;
	u32 channel;
	u32 phy_mode;
	u32 snr;
	u32 rate;
	u32 buf_len;
	u16 fc;
	int pull_len;

	if (test_bit(ATH10K_FW_FEATURE_EXT_WMI_MGMT_RX, ar->fw_features)) {
		ev_v2 = (struct wmi_mgmt_rx_event_v2 *)skb->data;
		ev_hdr = &ev_v2->hdr.v1;
		pull_len = sizeof(*ev_v2);
	} else {
		ev_v1 = (struct wmi_mgmt_rx_event_v1 *)skb->data;
		ev_hdr = &ev_v1->hdr;
		pull_len = sizeof(*ev_v1);
	}

	channel   = __le32_to_cpu(ev_hdr->channel);
	buf_len   = __le32_to_cpu(ev_hdr->buf_len);
	rx_status = __le32_to_cpu(ev_hdr->status);
	snr       = __le32_to_cpu(ev_hdr->snr);
	phy_mode  = __le32_to_cpu(ev_hdr->phy_mode);
	rate	  = __le32_to_cpu(ev_hdr->rate);

	memset(status, 0, sizeof(*status));

	ath10k_dbg(ar, ATH10K_DBG_MGMT,
		   "event mgmt rx status %08x\n", rx_status);

	if (test_bit(ATH10K_CAC_RUNNING, &ar->dev_flags)) {
		dev_kfree_skb(skb);
		return 0;
	}

	if (rx_status & WMI_RX_STATUS_ERR_DECRYPT) {
		dev_kfree_skb(skb);
		return 0;
	}

	if (rx_status & WMI_RX_STATUS_ERR_KEY_CACHE_MISS) {
		dev_kfree_skb(skb);
		return 0;
	}

	if (rx_status & WMI_RX_STATUS_ERR_CRC)
		status->flag |= RX_FLAG_FAILED_FCS_CRC;
	if (rx_status & WMI_RX_STATUS_ERR_MIC)
		status->flag |= RX_FLAG_MMIC_ERROR;

	/* HW can Rx CCK rates on 5GHz. In that case phy_mode is set to
	 * MODE_11B. This means phy_mode is not a reliable source for the band
	 * of mgmt rx. */

	ch = ar->scan_channel;
	if (!ch)
		ch = ar->rx_channel;

	if (ch) {
		status->band = ch->band;

		if (phy_mode == MODE_11B &&
		    status->band == IEEE80211_BAND_5GHZ)
			ath10k_dbg(ar, ATH10K_DBG_MGMT, "wmi mgmt rx 11b (CCK) on 5GHz\n");
	} else {
		ath10k_warn(ar, "using (unreliable) phy_mode to extract band for mgmt rx\n");
		status->band = phy_mode_to_band(phy_mode);
	}

	status->freq = ieee80211_channel_to_frequency(channel, status->band);
	status->signal = snr + ATH10K_DEFAULT_NOISE_FLOOR;
	status->rate_idx = get_rate_idx(rate, status->band);

	skb_pull(skb, pull_len);

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = le16_to_cpu(hdr->frame_control);

	/* FW delivers WEP Shared Auth frame with Protected Bit set and
	 * encrypted payload. However in case of PMF it delivers decrypted
	 * frames with Protected Bit set. */
	if (ieee80211_has_protected(hdr->frame_control) &&
	    !ieee80211_is_auth(hdr->frame_control)) {
		status->flag |= RX_FLAG_DECRYPTED;

		if (!ieee80211_is_action(hdr->frame_control) &&
		    !ieee80211_is_deauth(hdr->frame_control) &&
		    !ieee80211_is_disassoc(hdr->frame_control)) {
			status->flag |= RX_FLAG_IV_STRIPPED |
					RX_FLAG_MMIC_STRIPPED;
			hdr->frame_control = __cpu_to_le16(fc &
					~IEEE80211_FCTL_PROTECTED);
		}
	}

	ath10k_dbg(ar, ATH10K_DBG_MGMT,
		   "event mgmt rx skb %p len %d ftype %02x stype %02x\n",
		   skb, skb->len,
		   fc & IEEE80211_FCTL_FTYPE, fc & IEEE80211_FCTL_STYPE);

	ath10k_dbg(ar, ATH10K_DBG_MGMT,
		   "event mgmt rx freq %d band %d snr %d, rate_idx %d\n",
		   status->freq, status->band, status->signal,
		   status->rate_idx);

	/*
	 * packets from HTC come aligned to 4byte boundaries
	 * because they can originally come in along with a trailer
	 */
	skb_trim(skb, buf_len);

	ieee80211_rx(ar->hw, skb);
	return 0;
}

static int freq_to_idx(struct ath10k *ar, int freq)
{
	struct ieee80211_supported_band *sband;
	int band, ch, idx = 0;

	for (band = IEEE80211_BAND_2GHZ; band < IEEE80211_NUM_BANDS; band++) {
		sband = ar->hw->wiphy->bands[band];
		if (!sband)
			continue;

		for (ch = 0; ch < sband->n_channels; ch++, idx++)
			if (sband->channels[ch].center_freq == freq)
				goto exit;
	}

exit:
	return idx;
}

static void ath10k_wmi_event_chan_info(struct ath10k *ar, struct sk_buff *skb)
{
	struct wmi_chan_info_event *ev;
	struct survey_info *survey;
	u32 err_code, freq, cmd_flags, noise_floor, rx_clear_count, cycle_count;
	int idx;

	ev = (struct wmi_chan_info_event *)skb->data;

	err_code = __le32_to_cpu(ev->err_code);
	freq = __le32_to_cpu(ev->freq);
	cmd_flags = __le32_to_cpu(ev->cmd_flags);
	noise_floor = __le32_to_cpu(ev->noise_floor);
	rx_clear_count = __le32_to_cpu(ev->rx_clear_count);
	cycle_count = __le32_to_cpu(ev->cycle_count);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "chan info err_code %d freq %d cmd_flags %d noise_floor %d rx_clear_count %d cycle_count %d\n",
		   err_code, freq, cmd_flags, noise_floor, rx_clear_count,
		   cycle_count);

	spin_lock_bh(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH10K_SCAN_IDLE:
	case ATH10K_SCAN_STARTING:
		ath10k_warn(ar, "received chan info event without a scan request, ignoring\n");
		goto exit;
	case ATH10K_SCAN_RUNNING:
	case ATH10K_SCAN_ABORTING:
		break;
	}

	idx = freq_to_idx(ar, freq);
	if (idx >= ARRAY_SIZE(ar->survey)) {
		ath10k_warn(ar, "chan info: invalid frequency %d (idx %d out of bounds)\n",
			    freq, idx);
		goto exit;
	}

	if (cmd_flags & WMI_CHAN_INFO_FLAG_COMPLETE) {
		/* During scanning chan info is reported twice for each
		 * visited channel. The reported cycle count is global
		 * and per-channel cycle count must be calculated */

		cycle_count -= ar->survey_last_cycle_count;
		rx_clear_count -= ar->survey_last_rx_clear_count;

		survey = &ar->survey[idx];
		survey->channel_time = WMI_CHAN_INFO_MSEC(cycle_count);
		survey->channel_time_rx = WMI_CHAN_INFO_MSEC(rx_clear_count);
		survey->noise = noise_floor;
		survey->filled = SURVEY_INFO_CHANNEL_TIME |
				 SURVEY_INFO_CHANNEL_TIME_RX |
				 SURVEY_INFO_NOISE_DBM;
	}

	ar->survey_last_rx_clear_count = rx_clear_count;
	ar->survey_last_cycle_count = cycle_count;

exit:
	spin_unlock_bh(&ar->data_lock);
}

static void ath10k_wmi_event_echo(struct ath10k *ar, struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_ECHO_EVENTID\n");
}

static int ath10k_wmi_event_debug_mesg(struct ath10k *ar, struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi event debug mesg len %d\n",
		   skb->len);

	trace_ath10k_wmi_dbglog(ar, skb->data, skb->len);

	return 0;
}

static void ath10k_wmi_event_update_stats(struct ath10k *ar,
					  struct sk_buff *skb)
{
	struct wmi_stats_event *ev = (struct wmi_stats_event *)skb->data;

	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_UPDATE_STATS_EVENTID\n");

	ath10k_debug_read_target_stats(ar, ev);
}

static void ath10k_wmi_event_vdev_start_resp(struct ath10k *ar,
					     struct sk_buff *skb)
{
	struct wmi_vdev_start_response_event *ev;

	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_VDEV_START_RESP_EVENTID\n");

	ev = (struct wmi_vdev_start_response_event *)skb->data;

	if (WARN_ON(__le32_to_cpu(ev->status)))
		return;

	complete(&ar->vdev_setup_done);
}

static void ath10k_wmi_event_vdev_stopped(struct ath10k *ar,
					  struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_VDEV_STOPPED_EVENTID\n");
	complete(&ar->vdev_setup_done);
}

static void ath10k_wmi_event_peer_sta_kickout(struct ath10k *ar,
					      struct sk_buff *skb)
{
	struct wmi_peer_sta_kickout_event *ev;
	struct ieee80211_sta *sta;

	ev = (struct wmi_peer_sta_kickout_event *)skb->data;

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi event peer sta kickout %pM\n",
		   ev->peer_macaddr.addr);

	rcu_read_lock();

	sta = ieee80211_find_sta_by_ifaddr(ar->hw, ev->peer_macaddr.addr, NULL);
	if (!sta) {
		ath10k_warn(ar, "Spurious quick kickout for STA %pM\n",
			    ev->peer_macaddr.addr);
		goto exit;
	}

	ieee80211_report_low_ack(sta, 10);

exit:
	rcu_read_unlock();
}

/*
 * FIXME
 *
 * We don't report to mac80211 sleep state of connected
 * stations. Due to this mac80211 can't fill in TIM IE
 * correctly.
 *
 * I know of no way of getting nullfunc frames that contain
 * sleep transition from connected stations - these do not
 * seem to be sent from the target to the host. There also
 * doesn't seem to be a dedicated event for that. So the
 * only way left to do this would be to read tim_bitmap
 * during SWBA.
 *
 * We could probably try using tim_bitmap from SWBA to tell
 * mac80211 which stations are asleep and which are not. The
 * problem here is calling mac80211 functions so many times
 * could take too long and make us miss the time to submit
 * the beacon to the target.
 *
 * So as a workaround we try to extend the TIM IE if there
 * is unicast buffered for stations with aid > 7 and fill it
 * in ourselves.
 */
static void ath10k_wmi_update_tim(struct ath10k *ar,
				  struct ath10k_vif *arvif,
				  struct sk_buff *bcn,
				  struct wmi_bcn_info *bcn_info)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)bcn->data;
	struct ieee80211_tim_ie *tim;
	u8 *ies, *ie;
	u8 ie_len, pvm_len;
	__le32 t;
	u32 v;

	/* if next SWBA has no tim_changed the tim_bitmap is garbage.
	 * we must copy the bitmap upon change and reuse it later */
	if (__le32_to_cpu(bcn_info->tim_info.tim_changed)) {
		int i;

		BUILD_BUG_ON(sizeof(arvif->u.ap.tim_bitmap) !=
			     sizeof(bcn_info->tim_info.tim_bitmap));

		for (i = 0; i < sizeof(arvif->u.ap.tim_bitmap); i++) {
			t = bcn_info->tim_info.tim_bitmap[i / 4];
			v = __le32_to_cpu(t);
			arvif->u.ap.tim_bitmap[i] = (v >> ((i % 4) * 8)) & 0xFF;
		}

		/* FW reports either length 0 or 16
		 * so we calculate this on our own */
		arvif->u.ap.tim_len = 0;
		for (i = 0; i < sizeof(arvif->u.ap.tim_bitmap); i++)
			if (arvif->u.ap.tim_bitmap[i])
				arvif->u.ap.tim_len = i;

		arvif->u.ap.tim_len++;
	}

	ies = bcn->data;
	ies += ieee80211_hdrlen(hdr->frame_control);
	ies += 12; /* fixed parameters */

	ie = (u8 *)cfg80211_find_ie(WLAN_EID_TIM, ies,
				    (u8 *)skb_tail_pointer(bcn) - ies);
	if (!ie) {
		if (arvif->vdev_type != WMI_VDEV_TYPE_IBSS)
			ath10k_warn(ar, "no tim ie found;\n");
		return;
	}

	tim = (void *)ie + 2;
	ie_len = ie[1];
	pvm_len = ie_len - 3; /* exclude dtim count, dtim period, bmap ctl */

	if (pvm_len < arvif->u.ap.tim_len) {
		int expand_size = sizeof(arvif->u.ap.tim_bitmap) - pvm_len;
		int move_size = skb_tail_pointer(bcn) - (ie + 2 + ie_len);
		void *next_ie = ie + 2 + ie_len;

		if (skb_put(bcn, expand_size)) {
			memmove(next_ie + expand_size, next_ie, move_size);

			ie[1] += expand_size;
			ie_len += expand_size;
			pvm_len += expand_size;
		} else {
			ath10k_warn(ar, "tim expansion failed\n");
		}
	}

	if (pvm_len > sizeof(arvif->u.ap.tim_bitmap)) {
		ath10k_warn(ar, "tim pvm length is too great (%d)\n", pvm_len);
		return;
	}

	tim->bitmap_ctrl = !!__le32_to_cpu(bcn_info->tim_info.tim_mcast);
	memcpy(tim->virtual_map, arvif->u.ap.tim_bitmap, pvm_len);

	if (tim->dtim_count == 0) {
		ATH10K_SKB_CB(bcn)->bcn.dtim_zero = true;

		if (__le32_to_cpu(bcn_info->tim_info.tim_mcast) == 1)
			ATH10K_SKB_CB(bcn)->bcn.deliver_cab = true;
	}

	ath10k_dbg(ar, ATH10K_DBG_MGMT, "dtim %d/%d mcast %d pvmlen %d\n",
		   tim->dtim_count, tim->dtim_period,
		   tim->bitmap_ctrl, pvm_len);
}

static void ath10k_p2p_fill_noa_ie(u8 *data, u32 len,
				   struct wmi_p2p_noa_info *noa)
{
	struct ieee80211_p2p_noa_attr *noa_attr;
	u8  ctwindow_oppps = noa->ctwindow_oppps;
	u8 ctwindow = ctwindow_oppps >> WMI_P2P_OPPPS_CTWINDOW_OFFSET;
	bool oppps = !!(ctwindow_oppps & WMI_P2P_OPPPS_ENABLE_BIT);
	__le16 *noa_attr_len;
	u16 attr_len;
	u8 noa_descriptors = noa->num_descriptors;
	int i;

	/* P2P IE */
	data[0] = WLAN_EID_VENDOR_SPECIFIC;
	data[1] = len - 2;
	data[2] = (WLAN_OUI_WFA >> 16) & 0xff;
	data[3] = (WLAN_OUI_WFA >> 8) & 0xff;
	data[4] = (WLAN_OUI_WFA >> 0) & 0xff;
	data[5] = WLAN_OUI_TYPE_WFA_P2P;

	/* NOA ATTR */
	data[6] = IEEE80211_P2P_ATTR_ABSENCE_NOTICE;
	noa_attr_len = (__le16 *)&data[7]; /* 2 bytes */
	noa_attr = (struct ieee80211_p2p_noa_attr *)&data[9];

	noa_attr->index = noa->index;
	noa_attr->oppps_ctwindow = ctwindow;
	if (oppps)
		noa_attr->oppps_ctwindow |= IEEE80211_P2P_OPPPS_ENABLE_BIT;

	for (i = 0; i < noa_descriptors; i++) {
		noa_attr->desc[i].count =
			__le32_to_cpu(noa->descriptors[i].type_count);
		noa_attr->desc[i].duration = noa->descriptors[i].duration;
		noa_attr->desc[i].interval = noa->descriptors[i].interval;
		noa_attr->desc[i].start_time = noa->descriptors[i].start_time;
	}

	attr_len = 2; /* index + oppps_ctwindow */
	attr_len += noa_descriptors * sizeof(struct ieee80211_p2p_noa_desc);
	*noa_attr_len = __cpu_to_le16(attr_len);
}

static u32 ath10k_p2p_calc_noa_ie_len(struct wmi_p2p_noa_info *noa)
{
	u32 len = 0;
	u8 noa_descriptors = noa->num_descriptors;
	u8 opp_ps_info = noa->ctwindow_oppps;
	bool opps_enabled = !!(opp_ps_info & WMI_P2P_OPPPS_ENABLE_BIT);

	if (!noa_descriptors && !opps_enabled)
		return len;

	len += 1 + 1 + 4; /* EID + len + OUI */
	len += 1 + 2; /* noa attr  + attr len */
	len += 1 + 1; /* index + oppps_ctwindow */
	len += noa_descriptors * sizeof(struct ieee80211_p2p_noa_desc);

	return len;
}

static void ath10k_wmi_update_noa(struct ath10k *ar, struct ath10k_vif *arvif,
				  struct sk_buff *bcn,
				  struct wmi_bcn_info *bcn_info)
{
	struct wmi_p2p_noa_info *noa = &bcn_info->p2p_noa_info;
	u8 *new_data, *old_data = arvif->u.ap.noa_data;
	u32 new_len;

	if (arvif->vdev_subtype != WMI_VDEV_SUBTYPE_P2P_GO)
		return;

	ath10k_dbg(ar, ATH10K_DBG_MGMT, "noa changed: %d\n", noa->changed);
	if (noa->changed & WMI_P2P_NOA_CHANGED_BIT) {
		new_len = ath10k_p2p_calc_noa_ie_len(noa);
		if (!new_len)
			goto cleanup;

		new_data = kmalloc(new_len, GFP_ATOMIC);
		if (!new_data)
			goto cleanup;

		ath10k_p2p_fill_noa_ie(new_data, new_len, noa);

		spin_lock_bh(&ar->data_lock);
		arvif->u.ap.noa_data = new_data;
		arvif->u.ap.noa_len = new_len;
		spin_unlock_bh(&ar->data_lock);
		kfree(old_data);
	}

	if (arvif->u.ap.noa_data)
		if (!pskb_expand_head(bcn, 0, arvif->u.ap.noa_len, GFP_ATOMIC))
			memcpy(skb_put(bcn, arvif->u.ap.noa_len),
			       arvif->u.ap.noa_data,
			       arvif->u.ap.noa_len);
	return;

cleanup:
	spin_lock_bh(&ar->data_lock);
	arvif->u.ap.noa_data = NULL;
	arvif->u.ap.noa_len = 0;
	spin_unlock_bh(&ar->data_lock);
	kfree(old_data);
}

static void ath10k_wmi_event_host_swba(struct ath10k *ar, struct sk_buff *skb)
{
	struct wmi_host_swba_event *ev;
	u32 map;
	int i = -1;
	struct wmi_bcn_info *bcn_info;
	struct ath10k_vif *arvif;
	struct sk_buff *bcn;
	dma_addr_t paddr;
	int ret, vdev_id = 0;

	ev = (struct wmi_host_swba_event *)skb->data;
	map = __le32_to_cpu(ev->vdev_map);

	ath10k_dbg(ar, ATH10K_DBG_MGMT, "mgmt swba vdev_map 0x%x\n",
		   ev->vdev_map);

	for (; map; map >>= 1, vdev_id++) {
		if (!(map & 0x1))
			continue;

		i++;

		if (i >= WMI_MAX_AP_VDEV) {
			ath10k_warn(ar, "swba has corrupted vdev map\n");
			break;
		}

		bcn_info = &ev->bcn_info[i];

		ath10k_dbg(ar, ATH10K_DBG_MGMT,
			   "mgmt event bcn_info %d tim_len %d mcast %d changed %d num_ps_pending %d bitmap 0x%08x%08x%08x%08x\n",
			   i,
			   __le32_to_cpu(bcn_info->tim_info.tim_len),
			   __le32_to_cpu(bcn_info->tim_info.tim_mcast),
			   __le32_to_cpu(bcn_info->tim_info.tim_changed),
			   __le32_to_cpu(bcn_info->tim_info.tim_num_ps_pending),
			   __le32_to_cpu(bcn_info->tim_info.tim_bitmap[3]),
			   __le32_to_cpu(bcn_info->tim_info.tim_bitmap[2]),
			   __le32_to_cpu(bcn_info->tim_info.tim_bitmap[1]),
			   __le32_to_cpu(bcn_info->tim_info.tim_bitmap[0]));

		arvif = ath10k_get_arvif(ar, vdev_id);
		if (arvif == NULL) {
			ath10k_warn(ar, "no vif for vdev_id %d found\n",
				    vdev_id);
			continue;
		}

		/* There are no completions for beacons so wait for next SWBA
		 * before telling mac80211 to decrement CSA counter
		 *
		 * Once CSA counter is completed stop sending beacons until
		 * actual channel switch is done */
		if (arvif->vif->csa_active &&
		    ieee80211_csa_is_complete(arvif->vif)) {
			ieee80211_csa_finish(arvif->vif);
			continue;
		}

		bcn = ieee80211_beacon_get(ar->hw, arvif->vif);
		if (!bcn) {
			ath10k_warn(ar, "could not get mac80211 beacon\n");
			continue;
		}

		ath10k_tx_h_seq_no(arvif->vif, bcn);
		ath10k_wmi_update_tim(ar, arvif, bcn, bcn_info);
		ath10k_wmi_update_noa(ar, arvif, bcn, bcn_info);

		spin_lock_bh(&ar->data_lock);

		if (arvif->beacon) {
			if (!arvif->beacon_sent)
				ath10k_warn(ar, "SWBA overrun on vdev %d\n",
					    arvif->vdev_id);

			ath10k_mac_vif_beacon_free(arvif);
		}

		if (!arvif->beacon_buf) {
			paddr = dma_map_single(arvif->ar->dev, bcn->data,
					       bcn->len, DMA_TO_DEVICE);
			ret = dma_mapping_error(arvif->ar->dev, paddr);
			if (ret) {
				ath10k_warn(ar, "failed to map beacon: %d\n",
					    ret);
				dev_kfree_skb_any(bcn);
				goto skip;
			}

			ATH10K_SKB_CB(bcn)->paddr = paddr;
		} else {
			if (bcn->len > IEEE80211_MAX_FRAME_LEN) {
				ath10k_warn(ar, "trimming beacon %d -> %d bytes!\n",
					    bcn->len, IEEE80211_MAX_FRAME_LEN);
				skb_trim(bcn, IEEE80211_MAX_FRAME_LEN);
			}
			memcpy(arvif->beacon_buf, bcn->data, bcn->len);
			ATH10K_SKB_CB(bcn)->paddr = arvif->beacon_paddr;
		}

		arvif->beacon = bcn;
		arvif->beacon_sent = false;

		ath10k_wmi_tx_beacon_nowait(arvif);
skip:
		spin_unlock_bh(&ar->data_lock);
	}
}

static void ath10k_wmi_event_tbttoffset_update(struct ath10k *ar,
					       struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_TBTTOFFSET_UPDATE_EVENTID\n");
}

static void ath10k_dfs_radar_report(struct ath10k *ar,
				    struct wmi_single_phyerr_rx_event *event,
				    struct phyerr_radar_report *rr,
				    u64 tsf)
{
	u32 reg0, reg1, tsf32l;
	struct pulse_event pe;
	u64 tsf64;
	u8 rssi, width;

	reg0 = __le32_to_cpu(rr->reg0);
	reg1 = __le32_to_cpu(rr->reg1);

	ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
		   "wmi phyerr radar report chirp %d max_width %d agc_total_gain %d pulse_delta_diff %d\n",
		   MS(reg0, RADAR_REPORT_REG0_PULSE_IS_CHIRP),
		   MS(reg0, RADAR_REPORT_REG0_PULSE_IS_MAX_WIDTH),
		   MS(reg0, RADAR_REPORT_REG0_AGC_TOTAL_GAIN),
		   MS(reg0, RADAR_REPORT_REG0_PULSE_DELTA_DIFF));
	ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
		   "wmi phyerr radar report pulse_delta_pean %d pulse_sidx %d fft_valid %d agc_mb_gain %d subchan_mask %d\n",
		   MS(reg0, RADAR_REPORT_REG0_PULSE_DELTA_PEAK),
		   MS(reg0, RADAR_REPORT_REG0_PULSE_SIDX),
		   MS(reg1, RADAR_REPORT_REG1_PULSE_SRCH_FFT_VALID),
		   MS(reg1, RADAR_REPORT_REG1_PULSE_AGC_MB_GAIN),
		   MS(reg1, RADAR_REPORT_REG1_PULSE_SUBCHAN_MASK));
	ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
		   "wmi phyerr radar report pulse_tsf_offset 0x%X pulse_dur: %d\n",
		   MS(reg1, RADAR_REPORT_REG1_PULSE_TSF_OFFSET),
		   MS(reg1, RADAR_REPORT_REG1_PULSE_DUR));

	if (!ar->dfs_detector)
		return;

	/* report event to DFS pattern detector */
	tsf32l = __le32_to_cpu(event->hdr.tsf_timestamp);
	tsf64 = tsf & (~0xFFFFFFFFULL);
	tsf64 |= tsf32l;

	width = MS(reg1, RADAR_REPORT_REG1_PULSE_DUR);
	rssi = event->hdr.rssi_combined;

	/* hardware store this as 8 bit signed value,
	 * set to zero if negative number
	 */
	if (rssi & 0x80)
		rssi = 0;

	pe.ts = tsf64;
	pe.freq = ar->hw->conf.chandef.chan->center_freq;
	pe.width = width;
	pe.rssi = rssi;

	ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
		   "dfs add pulse freq: %d, width: %d, rssi %d, tsf: %llX\n",
		   pe.freq, pe.width, pe.rssi, pe.ts);

	ATH10K_DFS_STAT_INC(ar, pulses_detected);

	if (!ar->dfs_detector->add_pulse(ar->dfs_detector, &pe)) {
		ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
			   "dfs no pulse pattern detected, yet\n");
		return;
	}

	ath10k_dbg(ar, ATH10K_DBG_REGULATORY, "dfs radar detected\n");
	ATH10K_DFS_STAT_INC(ar, radar_detected);

	/* Control radar events reporting in debugfs file
	   dfs_block_radar_events */
	if (ar->dfs_block_radar_events) {
		ath10k_info(ar, "DFS Radar detected, but ignored as requested\n");
		return;
	}

	ieee80211_radar_detected(ar->hw);
}

static int ath10k_dfs_fft_report(struct ath10k *ar,
				 struct wmi_single_phyerr_rx_event *event,
				 struct phyerr_fft_report *fftr,
				 u64 tsf)
{
	u32 reg0, reg1;
	u8 rssi, peak_mag;

	reg0 = __le32_to_cpu(fftr->reg0);
	reg1 = __le32_to_cpu(fftr->reg1);
	rssi = event->hdr.rssi_combined;

	ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
		   "wmi phyerr fft report total_gain_db %d base_pwr_db %d fft_chn_idx %d peak_sidx %d\n",
		   MS(reg0, SEARCH_FFT_REPORT_REG0_TOTAL_GAIN_DB),
		   MS(reg0, SEARCH_FFT_REPORT_REG0_BASE_PWR_DB),
		   MS(reg0, SEARCH_FFT_REPORT_REG0_FFT_CHN_IDX),
		   MS(reg0, SEARCH_FFT_REPORT_REG0_PEAK_SIDX));
	ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
		   "wmi phyerr fft report rel_pwr_db %d avgpwr_db %d peak_mag %d num_store_bin %d\n",
		   MS(reg1, SEARCH_FFT_REPORT_REG1_RELPWR_DB),
		   MS(reg1, SEARCH_FFT_REPORT_REG1_AVGPWR_DB),
		   MS(reg1, SEARCH_FFT_REPORT_REG1_PEAK_MAG),
		   MS(reg1, SEARCH_FFT_REPORT_REG1_NUM_STR_BINS_IB));

	peak_mag = MS(reg1, SEARCH_FFT_REPORT_REG1_PEAK_MAG);

	/* false event detection */
	if (rssi == DFS_RSSI_POSSIBLY_FALSE &&
	    peak_mag < 2 * DFS_PEAK_MAG_THOLD_POSSIBLY_FALSE) {
		ath10k_dbg(ar, ATH10K_DBG_REGULATORY, "dfs false pulse detected\n");
		ATH10K_DFS_STAT_INC(ar, pulses_discarded);
		return -EINVAL;
	}

	return 0;
}

static void ath10k_wmi_event_dfs(struct ath10k *ar,
				 struct wmi_single_phyerr_rx_event *event,
				 u64 tsf)
{
	int buf_len, tlv_len, res, i = 0;
	struct phyerr_tlv *tlv;
	struct phyerr_radar_report *rr;
	struct phyerr_fft_report *fftr;
	u8 *tlv_buf;

	buf_len = __le32_to_cpu(event->hdr.buf_len);
	ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
		   "wmi event dfs err_code %d rssi %d tsfl 0x%X tsf64 0x%llX len %d\n",
		   event->hdr.phy_err_code, event->hdr.rssi_combined,
		   __le32_to_cpu(event->hdr.tsf_timestamp), tsf, buf_len);

	/* Skip event if DFS disabled */
	if (!config_enabled(CONFIG_ATH10K_DFS_CERTIFIED))
		return;

	ATH10K_DFS_STAT_INC(ar, pulses_total);

	while (i < buf_len) {
		if (i + sizeof(*tlv) > buf_len) {
			ath10k_warn(ar, "too short buf for tlv header (%d)\n",
				    i);
			return;
		}

		tlv = (struct phyerr_tlv *)&event->bufp[i];
		tlv_len = __le16_to_cpu(tlv->len);
		tlv_buf = &event->bufp[i + sizeof(*tlv)];
		ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
			   "wmi event dfs tlv_len %d tlv_tag 0x%02X tlv_sig 0x%02X\n",
			   tlv_len, tlv->tag, tlv->sig);

		switch (tlv->tag) {
		case PHYERR_TLV_TAG_RADAR_PULSE_SUMMARY:
			if (i + sizeof(*tlv) + sizeof(*rr) > buf_len) {
				ath10k_warn(ar, "too short radar pulse summary (%d)\n",
					    i);
				return;
			}

			rr = (struct phyerr_radar_report *)tlv_buf;
			ath10k_dfs_radar_report(ar, event, rr, tsf);
			break;
		case PHYERR_TLV_TAG_SEARCH_FFT_REPORT:
			if (i + sizeof(*tlv) + sizeof(*fftr) > buf_len) {
				ath10k_warn(ar, "too short fft report (%d)\n",
					    i);
				return;
			}

			fftr = (struct phyerr_fft_report *)tlv_buf;
			res = ath10k_dfs_fft_report(ar, event, fftr, tsf);
			if (res)
				return;
			break;
		}

		i += sizeof(*tlv) + tlv_len;
	}
}

static void
ath10k_wmi_event_spectral_scan(struct ath10k *ar,
			       struct wmi_single_phyerr_rx_event *event,
			       u64 tsf)
{
	int buf_len, tlv_len, res, i = 0;
	struct phyerr_tlv *tlv;
	u8 *tlv_buf;
	struct phyerr_fft_report *fftr;
	size_t fftr_len;

	buf_len = __le32_to_cpu(event->hdr.buf_len);

	while (i < buf_len) {
		if (i + sizeof(*tlv) > buf_len) {
			ath10k_warn(ar, "failed to parse phyerr tlv header at byte %d\n",
				    i);
			return;
		}

		tlv = (struct phyerr_tlv *)&event->bufp[i];
		tlv_len = __le16_to_cpu(tlv->len);
		tlv_buf = &event->bufp[i + sizeof(*tlv)];

		if (i + sizeof(*tlv) + tlv_len > buf_len) {
			ath10k_warn(ar, "failed to parse phyerr tlv payload at byte %d\n",
				    i);
			return;
		}

		switch (tlv->tag) {
		case PHYERR_TLV_TAG_SEARCH_FFT_REPORT:
			if (sizeof(*fftr) > tlv_len) {
				ath10k_warn(ar, "failed to parse fft report at byte %d\n",
					    i);
				return;
			}

			fftr_len = tlv_len - sizeof(*fftr);
			fftr = (struct phyerr_fft_report *)tlv_buf;
			res = ath10k_spectral_process_fft(ar, event,
							  fftr, fftr_len,
							  tsf);
			if (res < 0) {
				ath10k_warn(ar, "failed to process fft report: %d\n",
					    res);
				return;
			}
			break;
		}

		i += sizeof(*tlv) + tlv_len;
	}
}

static void ath10k_wmi_event_phyerr(struct ath10k *ar, struct sk_buff *skb)
{
	struct wmi_comb_phyerr_rx_event *comb_event;
	struct wmi_single_phyerr_rx_event *event;
	u32 count, i, buf_len, phy_err_code;
	u64 tsf;
	int left_len = skb->len;

	ATH10K_DFS_STAT_INC(ar, phy_errors);

	/* Check if combined event available */
	if (left_len < sizeof(*comb_event)) {
		ath10k_warn(ar, "wmi phyerr combined event wrong len\n");
		return;
	}

	left_len -= sizeof(*comb_event);

	/* Check number of included events */
	comb_event = (struct wmi_comb_phyerr_rx_event *)skb->data;
	count = __le32_to_cpu(comb_event->hdr.num_phyerr_events);

	tsf = __le32_to_cpu(comb_event->hdr.tsf_u32);
	tsf <<= 32;
	tsf |= __le32_to_cpu(comb_event->hdr.tsf_l32);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi event phyerr count %d tsf64 0x%llX\n",
		   count, tsf);

	event = (struct wmi_single_phyerr_rx_event *)comb_event->bufp;
	for (i = 0; i < count; i++) {
		/* Check if we can read event header */
		if (left_len < sizeof(*event)) {
			ath10k_warn(ar, "single event (%d) wrong head len\n",
				    i);
			return;
		}

		left_len -= sizeof(*event);

		buf_len = __le32_to_cpu(event->hdr.buf_len);
		phy_err_code = event->hdr.phy_err_code;

		if (left_len < buf_len) {
			ath10k_warn(ar, "single event (%d) wrong buf len\n", i);
			return;
		}

		left_len -= buf_len;

		switch (phy_err_code) {
		case PHY_ERROR_RADAR:
			ath10k_wmi_event_dfs(ar, event, tsf);
			break;
		case PHY_ERROR_SPECTRAL_SCAN:
			ath10k_wmi_event_spectral_scan(ar, event, tsf);
			break;
		case PHY_ERROR_FALSE_RADAR_EXT:
			ath10k_wmi_event_dfs(ar, event, tsf);
			ath10k_wmi_event_spectral_scan(ar, event, tsf);
			break;
		default:
			break;
		}

		event += sizeof(*event) + buf_len;
	}
}

static void ath10k_wmi_event_roam(struct ath10k *ar, struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_ROAM_EVENTID\n");
}

static void ath10k_wmi_event_profile_match(struct ath10k *ar,
					   struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_PROFILE_MATCH\n");
}

static void ath10k_wmi_event_debug_print(struct ath10k *ar,
					 struct sk_buff *skb)
{
	char buf[101], c;
	int i;

	for (i = 0; i < sizeof(buf) - 1; i++) {
		if (i >= skb->len)
			break;

		c = skb->data[i];

		if (c == '\0')
			break;

		if (isascii(c) && isprint(c))
			buf[i] = c;
		else
			buf[i] = '.';
	}

	if (i == sizeof(buf) - 1)
		ath10k_warn(ar, "wmi debug print truncated: %d\n", skb->len);

	/* for some reason the debug prints end with \n, remove that */
	if (skb->data[i - 1] == '\n')
		i--;

	/* the last byte is always reserved for the null character */
	buf[i] = '\0';

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi event debug print '%s'\n", buf);
}

static void ath10k_wmi_event_pdev_qvit(struct ath10k *ar, struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_PDEV_QVIT_EVENTID\n");
}

static void ath10k_wmi_event_wlan_profile_data(struct ath10k *ar,
					       struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_WLAN_PROFILE_DATA_EVENTID\n");
}

static void ath10k_wmi_event_rtt_measurement_report(struct ath10k *ar,
						    struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_RTT_MEASUREMENT_REPORT_EVENTID\n");
}

static void ath10k_wmi_event_tsf_measurement_report(struct ath10k *ar,
						    struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_TSF_MEASUREMENT_REPORT_EVENTID\n");
}

static void ath10k_wmi_event_rtt_error_report(struct ath10k *ar,
					      struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_RTT_ERROR_REPORT_EVENTID\n");
}

static void ath10k_wmi_event_wow_wakeup_host(struct ath10k *ar,
					     struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_WOW_WAKEUP_HOST_EVENTID\n");
}

static void ath10k_wmi_event_dcs_interference(struct ath10k *ar,
					      struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_DCS_INTERFERENCE_EVENTID\n");
}

static void ath10k_wmi_event_pdev_tpc_config(struct ath10k *ar,
					     struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_PDEV_TPC_CONFIG_EVENTID\n");
}

static void ath10k_wmi_event_pdev_ftm_intg(struct ath10k *ar,
					   struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_PDEV_FTM_INTG_EVENTID\n");
}

static void ath10k_wmi_event_gtk_offload_status(struct ath10k *ar,
						struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_GTK_OFFLOAD_STATUS_EVENTID\n");
}

static void ath10k_wmi_event_gtk_rekey_fail(struct ath10k *ar,
					    struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_GTK_REKEY_FAIL_EVENTID\n");
}

static void ath10k_wmi_event_delba_complete(struct ath10k *ar,
					    struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_TX_DELBA_COMPLETE_EVENTID\n");
}

static void ath10k_wmi_event_addba_complete(struct ath10k *ar,
					    struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_TX_ADDBA_COMPLETE_EVENTID\n");
}

static void ath10k_wmi_event_vdev_install_key_complete(struct ath10k *ar,
						       struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_VDEV_INSTALL_KEY_COMPLETE_EVENTID\n");
}

static void ath10k_wmi_event_inst_rssi_stats(struct ath10k *ar,
					     struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_INST_RSSI_STATS_EVENTID\n");
}

static void ath10k_wmi_event_vdev_standby_req(struct ath10k *ar,
					      struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_VDEV_STANDBY_REQ_EVENTID\n");
}

static void ath10k_wmi_event_vdev_resume_req(struct ath10k *ar,
					     struct sk_buff *skb)
{
	ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_VDEV_RESUME_REQ_EVENTID\n");
}

static int ath10k_wmi_alloc_host_mem(struct ath10k *ar, u32 req_id,
				     u32 num_units, u32 unit_len)
{
	dma_addr_t paddr;
	u32 pool_size;
	int idx = ar->wmi.num_mem_chunks;

	pool_size = num_units * round_up(unit_len, 4);

	if (!pool_size)
		return -EINVAL;

	ar->wmi.mem_chunks[idx].vaddr = dma_alloc_coherent(ar->dev,
							   pool_size,
							   &paddr,
							   GFP_ATOMIC);
	if (!ar->wmi.mem_chunks[idx].vaddr) {
		ath10k_warn(ar, "failed to allocate memory chunk\n");
		return -ENOMEM;
	}

	memset(ar->wmi.mem_chunks[idx].vaddr, 0, pool_size);

	ar->wmi.mem_chunks[idx].paddr = paddr;
	ar->wmi.mem_chunks[idx].len = pool_size;
	ar->wmi.mem_chunks[idx].req_id = req_id;
	ar->wmi.num_mem_chunks++;

	return 0;
}

static void ath10k_wmi_service_ready_event_rx(struct ath10k *ar,
					      struct sk_buff *skb)
{
	struct wmi_service_ready_event *ev = (void *)skb->data;
	DECLARE_BITMAP(svc_bmap, WMI_SERVICE_MAX) = {};

	if (skb->len < sizeof(*ev)) {
		ath10k_warn(ar, "Service ready event was %d B but expected %zu B. Wrong firmware version?\n",
			    skb->len, sizeof(*ev));
		return;
	}

	ar->hw_min_tx_power = __le32_to_cpu(ev->hw_min_tx_power);
	ar->hw_max_tx_power = __le32_to_cpu(ev->hw_max_tx_power);
	ar->ht_cap_info = __le32_to_cpu(ev->ht_cap_info);
	ar->vht_cap_info = __le32_to_cpu(ev->vht_cap_info);
	ar->fw_version_major =
		(__le32_to_cpu(ev->sw_version) & 0xff000000) >> 24;
	ar->fw_version_minor = (__le32_to_cpu(ev->sw_version) & 0x00ffffff);
	ar->fw_version_release =
		(__le32_to_cpu(ev->sw_version_1) & 0xffff0000) >> 16;
	ar->fw_version_build = (__le32_to_cpu(ev->sw_version_1) & 0x0000ffff);
	ar->phy_capability = __le32_to_cpu(ev->phy_capability);
	ar->num_rf_chains = __le32_to_cpu(ev->num_rf_chains);

	/* only manually set fw features when not using FW IE format */
	if (ar->fw_api == 1 && ar->fw_version_build > 636)
		set_bit(ATH10K_FW_FEATURE_EXT_WMI_MGMT_RX, ar->fw_features);

	if (ar->num_rf_chains > WMI_MAX_SPATIAL_STREAM) {
		ath10k_warn(ar, "hardware advertises support for more spatial streams than it should (%d > %d)\n",
			    ar->num_rf_chains, WMI_MAX_SPATIAL_STREAM);
		ar->num_rf_chains = WMI_MAX_SPATIAL_STREAM;
	}

	ar->supp_tx_chainmask = (1 << ar->num_rf_chains) - 1;
	ar->supp_rx_chainmask = (1 << ar->num_rf_chains) - 1;

	ar->ath_common.regulatory.current_rd =
		__le32_to_cpu(ev->hal_reg_capabilities.eeprom_rd);

	wmi_main_svc_map(ev->wmi_service_bitmap, svc_bmap);
	ath10k_debug_read_service_map(ar, svc_bmap, sizeof(svc_bmap));
	ath10k_dbg_dump(ar, ATH10K_DBG_WMI, NULL, "wmi svc: ",
			ev->wmi_service_bitmap, sizeof(ev->wmi_service_bitmap));

	if (strlen(ar->hw->wiphy->fw_version) == 0) {
		snprintf(ar->hw->wiphy->fw_version,
			 sizeof(ar->hw->wiphy->fw_version),
			 "%u.%u.%u.%u",
			 ar->fw_version_major,
			 ar->fw_version_minor,
			 ar->fw_version_release,
			 ar->fw_version_build);
	}

	/* FIXME: it probably should be better to support this */
	if (__le32_to_cpu(ev->num_mem_reqs) > 0) {
		ath10k_warn(ar, "target requested %d memory chunks; ignoring\n",
			    __le32_to_cpu(ev->num_mem_reqs));
	}

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi event service ready sw_ver 0x%08x sw_ver1 0x%08x abi_ver %u phy_cap 0x%08x ht_cap 0x%08x vht_cap 0x%08x vht_supp_msc 0x%08x sys_cap_info 0x%08x mem_reqs %u num_rf_chains %u\n",
		   __le32_to_cpu(ev->sw_version),
		   __le32_to_cpu(ev->sw_version_1),
		   __le32_to_cpu(ev->abi_version),
		   __le32_to_cpu(ev->phy_capability),
		   __le32_to_cpu(ev->ht_cap_info),
		   __le32_to_cpu(ev->vht_cap_info),
		   __le32_to_cpu(ev->vht_supp_mcs),
		   __le32_to_cpu(ev->sys_cap_info),
		   __le32_to_cpu(ev->num_mem_reqs),
		   __le32_to_cpu(ev->num_rf_chains));

	complete(&ar->wmi.service_ready);
}

static void ath10k_wmi_10x_service_ready_event_rx(struct ath10k *ar,
						  struct sk_buff *skb)
{
	u32 num_units, req_id, unit_size, num_mem_reqs, num_unit_info, i;
	int ret;
	struct wmi_service_ready_event_10x *ev = (void *)skb->data;
	DECLARE_BITMAP(svc_bmap, WMI_SERVICE_MAX) = {};

	if (skb->len < sizeof(*ev)) {
		ath10k_warn(ar, "Service ready event was %d B but expected %zu B. Wrong firmware version?\n",
			    skb->len, sizeof(*ev));
		return;
	}

	ar->hw_min_tx_power = __le32_to_cpu(ev->hw_min_tx_power);
	ar->hw_max_tx_power = __le32_to_cpu(ev->hw_max_tx_power);
	ar->ht_cap_info = __le32_to_cpu(ev->ht_cap_info);
	ar->vht_cap_info = __le32_to_cpu(ev->vht_cap_info);
	ar->fw_version_major =
		(__le32_to_cpu(ev->sw_version) & 0xff000000) >> 24;
	ar->fw_version_minor = (__le32_to_cpu(ev->sw_version) & 0x00ffffff);
	ar->phy_capability = __le32_to_cpu(ev->phy_capability);
	ar->num_rf_chains = __le32_to_cpu(ev->num_rf_chains);

	if (ar->num_rf_chains > WMI_MAX_SPATIAL_STREAM) {
		ath10k_warn(ar, "hardware advertises support for more spatial streams than it should (%d > %d)\n",
			    ar->num_rf_chains, WMI_MAX_SPATIAL_STREAM);
		ar->num_rf_chains = WMI_MAX_SPATIAL_STREAM;
	}

	ar->supp_tx_chainmask = (1 << ar->num_rf_chains) - 1;
	ar->supp_rx_chainmask = (1 << ar->num_rf_chains) - 1;

	ar->ath_common.regulatory.current_rd =
		__le32_to_cpu(ev->hal_reg_capabilities.eeprom_rd);

	wmi_10x_svc_map(ev->wmi_service_bitmap, svc_bmap);
	ath10k_debug_read_service_map(ar, svc_bmap, sizeof(svc_bmap));
	ath10k_dbg_dump(ar, ATH10K_DBG_WMI, NULL, "wmi svc: ",
			ev->wmi_service_bitmap, sizeof(ev->wmi_service_bitmap));

	if (strlen(ar->hw->wiphy->fw_version) == 0) {
		snprintf(ar->hw->wiphy->fw_version,
			 sizeof(ar->hw->wiphy->fw_version),
			 "%u.%u",
			 ar->fw_version_major,
			 ar->fw_version_minor);
	}

	num_mem_reqs = __le32_to_cpu(ev->num_mem_reqs);

	if (num_mem_reqs > ATH10K_MAX_MEM_REQS) {
		ath10k_warn(ar, "requested memory chunks number (%d) exceeds the limit\n",
			    num_mem_reqs);
		return;
	}

	if (!num_mem_reqs)
		goto exit;

	ath10k_dbg(ar, ATH10K_DBG_WMI, "firmware has requested %d memory chunks\n",
		   num_mem_reqs);

	for (i = 0; i < num_mem_reqs; ++i) {
		req_id = __le32_to_cpu(ev->mem_reqs[i].req_id);
		num_units = __le32_to_cpu(ev->mem_reqs[i].num_units);
		unit_size = __le32_to_cpu(ev->mem_reqs[i].unit_size);
		num_unit_info = __le32_to_cpu(ev->mem_reqs[i].num_unit_info);

		if (num_unit_info & NUM_UNITS_IS_NUM_PEERS)
			/* number of units to allocate is number of
			 * peers, 1 extra for self peer on target */
			/* this needs to be tied, host and target
			 * can get out of sync */
			num_units = TARGET_10X_NUM_PEERS + 1;
		else if (num_unit_info & NUM_UNITS_IS_NUM_VDEVS)
			num_units = TARGET_10X_NUM_VDEVS + 1;

		ath10k_dbg(ar, ATH10K_DBG_WMI,
			   "wmi mem_req_id %d num_units %d num_unit_info %d unit size %d actual units %d\n",
			   req_id,
			   __le32_to_cpu(ev->mem_reqs[i].num_units),
			   num_unit_info,
			   unit_size,
			   num_units);

		ret = ath10k_wmi_alloc_host_mem(ar, req_id, num_units,
						unit_size);
		if (ret)
			return;
	}

exit:
	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi event service ready sw_ver 0x%08x abi_ver %u phy_cap 0x%08x ht_cap 0x%08x vht_cap 0x%08x vht_supp_msc 0x%08x sys_cap_info 0x%08x mem_reqs %u num_rf_chains %u\n",
		   __le32_to_cpu(ev->sw_version),
		   __le32_to_cpu(ev->abi_version),
		   __le32_to_cpu(ev->phy_capability),
		   __le32_to_cpu(ev->ht_cap_info),
		   __le32_to_cpu(ev->vht_cap_info),
		   __le32_to_cpu(ev->vht_supp_mcs),
		   __le32_to_cpu(ev->sys_cap_info),
		   __le32_to_cpu(ev->num_mem_reqs),
		   __le32_to_cpu(ev->num_rf_chains));

	complete(&ar->wmi.service_ready);
}

static int ath10k_wmi_ready_event_rx(struct ath10k *ar, struct sk_buff *skb)
{
	struct wmi_ready_event *ev = (struct wmi_ready_event *)skb->data;

	if (WARN_ON(skb->len < sizeof(*ev)))
		return -EINVAL;

	ether_addr_copy(ar->mac_addr, ev->mac_addr.addr);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi event ready sw_version %u abi_version %u mac_addr %pM status %d skb->len %i ev-sz %zu\n",
		   __le32_to_cpu(ev->sw_version),
		   __le32_to_cpu(ev->abi_version),
		   ev->mac_addr.addr,
		   __le32_to_cpu(ev->status), skb->len, sizeof(*ev));

	complete(&ar->wmi.unified_ready);
	return 0;
}

static void ath10k_wmi_main_process_rx(struct ath10k *ar, struct sk_buff *skb)
{
	struct wmi_cmd_hdr *cmd_hdr;
	enum wmi_event_id id;

	cmd_hdr = (struct wmi_cmd_hdr *)skb->data;
	id = MS(__le32_to_cpu(cmd_hdr->cmd_id), WMI_CMD_HDR_CMD_ID);

	if (skb_pull(skb, sizeof(struct wmi_cmd_hdr)) == NULL)
		return;

	trace_ath10k_wmi_event(ar, id, skb->data, skb->len);

	switch (id) {
	case WMI_MGMT_RX_EVENTID:
		ath10k_wmi_event_mgmt_rx(ar, skb);
		/* mgmt_rx() owns the skb now! */
		return;
	case WMI_SCAN_EVENTID:
		ath10k_wmi_event_scan(ar, skb);
		break;
	case WMI_CHAN_INFO_EVENTID:
		ath10k_wmi_event_chan_info(ar, skb);
		break;
	case WMI_ECHO_EVENTID:
		ath10k_wmi_event_echo(ar, skb);
		break;
	case WMI_DEBUG_MESG_EVENTID:
		ath10k_wmi_event_debug_mesg(ar, skb);
		break;
	case WMI_UPDATE_STATS_EVENTID:
		ath10k_wmi_event_update_stats(ar, skb);
		break;
	case WMI_VDEV_START_RESP_EVENTID:
		ath10k_wmi_event_vdev_start_resp(ar, skb);
		break;
	case WMI_VDEV_STOPPED_EVENTID:
		ath10k_wmi_event_vdev_stopped(ar, skb);
		break;
	case WMI_PEER_STA_KICKOUT_EVENTID:
		ath10k_wmi_event_peer_sta_kickout(ar, skb);
		break;
	case WMI_HOST_SWBA_EVENTID:
		ath10k_wmi_event_host_swba(ar, skb);
		break;
	case WMI_TBTTOFFSET_UPDATE_EVENTID:
		ath10k_wmi_event_tbttoffset_update(ar, skb);
		break;
	case WMI_PHYERR_EVENTID:
		ath10k_wmi_event_phyerr(ar, skb);
		break;
	case WMI_ROAM_EVENTID:
		ath10k_wmi_event_roam(ar, skb);
		break;
	case WMI_PROFILE_MATCH:
		ath10k_wmi_event_profile_match(ar, skb);
		break;
	case WMI_DEBUG_PRINT_EVENTID:
		ath10k_wmi_event_debug_print(ar, skb);
		break;
	case WMI_PDEV_QVIT_EVENTID:
		ath10k_wmi_event_pdev_qvit(ar, skb);
		break;
	case WMI_WLAN_PROFILE_DATA_EVENTID:
		ath10k_wmi_event_wlan_profile_data(ar, skb);
		break;
	case WMI_RTT_MEASUREMENT_REPORT_EVENTID:
		ath10k_wmi_event_rtt_measurement_report(ar, skb);
		break;
	case WMI_TSF_MEASUREMENT_REPORT_EVENTID:
		ath10k_wmi_event_tsf_measurement_report(ar, skb);
		break;
	case WMI_RTT_ERROR_REPORT_EVENTID:
		ath10k_wmi_event_rtt_error_report(ar, skb);
		break;
	case WMI_WOW_WAKEUP_HOST_EVENTID:
		ath10k_wmi_event_wow_wakeup_host(ar, skb);
		break;
	case WMI_DCS_INTERFERENCE_EVENTID:
		ath10k_wmi_event_dcs_interference(ar, skb);
		break;
	case WMI_PDEV_TPC_CONFIG_EVENTID:
		ath10k_wmi_event_pdev_tpc_config(ar, skb);
		break;
	case WMI_PDEV_FTM_INTG_EVENTID:
		ath10k_wmi_event_pdev_ftm_intg(ar, skb);
		break;
	case WMI_GTK_OFFLOAD_STATUS_EVENTID:
		ath10k_wmi_event_gtk_offload_status(ar, skb);
		break;
	case WMI_GTK_REKEY_FAIL_EVENTID:
		ath10k_wmi_event_gtk_rekey_fail(ar, skb);
		break;
	case WMI_TX_DELBA_COMPLETE_EVENTID:
		ath10k_wmi_event_delba_complete(ar, skb);
		break;
	case WMI_TX_ADDBA_COMPLETE_EVENTID:
		ath10k_wmi_event_addba_complete(ar, skb);
		break;
	case WMI_VDEV_INSTALL_KEY_COMPLETE_EVENTID:
		ath10k_wmi_event_vdev_install_key_complete(ar, skb);
		break;
	case WMI_SERVICE_READY_EVENTID:
		ath10k_wmi_service_ready_event_rx(ar, skb);
		break;
	case WMI_READY_EVENTID:
		ath10k_wmi_ready_event_rx(ar, skb);
		break;
	default:
		ath10k_warn(ar, "Unknown eventid: %d\n", id);
		break;
	}

	dev_kfree_skb(skb);
}

static void ath10k_wmi_10x_process_rx(struct ath10k *ar, struct sk_buff *skb)
{
	struct wmi_cmd_hdr *cmd_hdr;
	enum wmi_10x_event_id id;
	bool consumed;

	cmd_hdr = (struct wmi_cmd_hdr *)skb->data;
	id = MS(__le32_to_cpu(cmd_hdr->cmd_id), WMI_CMD_HDR_CMD_ID);

	if (skb_pull(skb, sizeof(struct wmi_cmd_hdr)) == NULL)
		return;

	trace_ath10k_wmi_event(ar, id, skb->data, skb->len);

	consumed = ath10k_tm_event_wmi(ar, id, skb);

	/* Ready event must be handled normally also in UTF mode so that we
	 * know the UTF firmware has booted, others we are just bypass WMI
	 * events to testmode.
	 */
	if (consumed && id != WMI_10X_READY_EVENTID) {
		ath10k_dbg(ar, ATH10K_DBG_WMI,
			   "wmi testmode consumed 0x%x\n", id);
		goto out;
	}

	switch (id) {
	case WMI_10X_MGMT_RX_EVENTID:
		ath10k_wmi_event_mgmt_rx(ar, skb);
		/* mgmt_rx() owns the skb now! */
		return;
	case WMI_10X_SCAN_EVENTID:
		ath10k_wmi_event_scan(ar, skb);
		break;
	case WMI_10X_CHAN_INFO_EVENTID:
		ath10k_wmi_event_chan_info(ar, skb);
		break;
	case WMI_10X_ECHO_EVENTID:
		ath10k_wmi_event_echo(ar, skb);
		break;
	case WMI_10X_DEBUG_MESG_EVENTID:
		ath10k_wmi_event_debug_mesg(ar, skb);
		break;
	case WMI_10X_UPDATE_STATS_EVENTID:
		ath10k_wmi_event_update_stats(ar, skb);
		break;
	case WMI_10X_VDEV_START_RESP_EVENTID:
		ath10k_wmi_event_vdev_start_resp(ar, skb);
		break;
	case WMI_10X_VDEV_STOPPED_EVENTID:
		ath10k_wmi_event_vdev_stopped(ar, skb);
		break;
	case WMI_10X_PEER_STA_KICKOUT_EVENTID:
		ath10k_wmi_event_peer_sta_kickout(ar, skb);
		break;
	case WMI_10X_HOST_SWBA_EVENTID:
		ath10k_wmi_event_host_swba(ar, skb);
		break;
	case WMI_10X_TBTTOFFSET_UPDATE_EVENTID:
		ath10k_wmi_event_tbttoffset_update(ar, skb);
		break;
	case WMI_10X_PHYERR_EVENTID:
		ath10k_wmi_event_phyerr(ar, skb);
		break;
	case WMI_10X_ROAM_EVENTID:
		ath10k_wmi_event_roam(ar, skb);
		break;
	case WMI_10X_PROFILE_MATCH:
		ath10k_wmi_event_profile_match(ar, skb);
		break;
	case WMI_10X_DEBUG_PRINT_EVENTID:
		ath10k_wmi_event_debug_print(ar, skb);
		break;
	case WMI_10X_PDEV_QVIT_EVENTID:
		ath10k_wmi_event_pdev_qvit(ar, skb);
		break;
	case WMI_10X_WLAN_PROFILE_DATA_EVENTID:
		ath10k_wmi_event_wlan_profile_data(ar, skb);
		break;
	case WMI_10X_RTT_MEASUREMENT_REPORT_EVENTID:
		ath10k_wmi_event_rtt_measurement_report(ar, skb);
		break;
	case WMI_10X_TSF_MEASUREMENT_REPORT_EVENTID:
		ath10k_wmi_event_tsf_measurement_report(ar, skb);
		break;
	case WMI_10X_RTT_ERROR_REPORT_EVENTID:
		ath10k_wmi_event_rtt_error_report(ar, skb);
		break;
	case WMI_10X_WOW_WAKEUP_HOST_EVENTID:
		ath10k_wmi_event_wow_wakeup_host(ar, skb);
		break;
	case WMI_10X_DCS_INTERFERENCE_EVENTID:
		ath10k_wmi_event_dcs_interference(ar, skb);
		break;
	case WMI_10X_PDEV_TPC_CONFIG_EVENTID:
		ath10k_wmi_event_pdev_tpc_config(ar, skb);
		break;
	case WMI_10X_INST_RSSI_STATS_EVENTID:
		ath10k_wmi_event_inst_rssi_stats(ar, skb);
		break;
	case WMI_10X_VDEV_STANDBY_REQ_EVENTID:
		ath10k_wmi_event_vdev_standby_req(ar, skb);
		break;
	case WMI_10X_VDEV_RESUME_REQ_EVENTID:
		ath10k_wmi_event_vdev_resume_req(ar, skb);
		break;
	case WMI_10X_SERVICE_READY_EVENTID:
		ath10k_wmi_10x_service_ready_event_rx(ar, skb);
		break;
	case WMI_10X_READY_EVENTID:
		ath10k_wmi_ready_event_rx(ar, skb);
		break;
	case WMI_10X_PDEV_UTF_EVENTID:
		/* ignore utf events */
		break;
	default:
		ath10k_warn(ar, "Unknown eventid: %d\n", id);
		break;
	}

out:
	dev_kfree_skb(skb);
}

static void ath10k_wmi_10_2_process_rx(struct ath10k *ar, struct sk_buff *skb)
{
	struct wmi_cmd_hdr *cmd_hdr;
	enum wmi_10_2_event_id id;

	cmd_hdr = (struct wmi_cmd_hdr *)skb->data;
	id = MS(__le32_to_cpu(cmd_hdr->cmd_id), WMI_CMD_HDR_CMD_ID);

	if (skb_pull(skb, sizeof(struct wmi_cmd_hdr)) == NULL)
		return;

	trace_ath10k_wmi_event(ar, id, skb->data, skb->len);

	switch (id) {
	case WMI_10_2_MGMT_RX_EVENTID:
		ath10k_wmi_event_mgmt_rx(ar, skb);
		/* mgmt_rx() owns the skb now! */
		return;
	case WMI_10_2_SCAN_EVENTID:
		ath10k_wmi_event_scan(ar, skb);
		break;
	case WMI_10_2_CHAN_INFO_EVENTID:
		ath10k_wmi_event_chan_info(ar, skb);
		break;
	case WMI_10_2_ECHO_EVENTID:
		ath10k_wmi_event_echo(ar, skb);
		break;
	case WMI_10_2_DEBUG_MESG_EVENTID:
		ath10k_wmi_event_debug_mesg(ar, skb);
		break;
	case WMI_10_2_UPDATE_STATS_EVENTID:
		ath10k_wmi_event_update_stats(ar, skb);
		break;
	case WMI_10_2_VDEV_START_RESP_EVENTID:
		ath10k_wmi_event_vdev_start_resp(ar, skb);
		break;
	case WMI_10_2_VDEV_STOPPED_EVENTID:
		ath10k_wmi_event_vdev_stopped(ar, skb);
		break;
	case WMI_10_2_PEER_STA_KICKOUT_EVENTID:
		ath10k_wmi_event_peer_sta_kickout(ar, skb);
		break;
	case WMI_10_2_HOST_SWBA_EVENTID:
		ath10k_wmi_event_host_swba(ar, skb);
		break;
	case WMI_10_2_TBTTOFFSET_UPDATE_EVENTID:
		ath10k_wmi_event_tbttoffset_update(ar, skb);
		break;
	case WMI_10_2_PHYERR_EVENTID:
		ath10k_wmi_event_phyerr(ar, skb);
		break;
	case WMI_10_2_ROAM_EVENTID:
		ath10k_wmi_event_roam(ar, skb);
		break;
	case WMI_10_2_PROFILE_MATCH:
		ath10k_wmi_event_profile_match(ar, skb);
		break;
	case WMI_10_2_DEBUG_PRINT_EVENTID:
		ath10k_wmi_event_debug_print(ar, skb);
		break;
	case WMI_10_2_PDEV_QVIT_EVENTID:
		ath10k_wmi_event_pdev_qvit(ar, skb);
		break;
	case WMI_10_2_WLAN_PROFILE_DATA_EVENTID:
		ath10k_wmi_event_wlan_profile_data(ar, skb);
		break;
	case WMI_10_2_RTT_MEASUREMENT_REPORT_EVENTID:
		ath10k_wmi_event_rtt_measurement_report(ar, skb);
		break;
	case WMI_10_2_TSF_MEASUREMENT_REPORT_EVENTID:
		ath10k_wmi_event_tsf_measurement_report(ar, skb);
		break;
	case WMI_10_2_RTT_ERROR_REPORT_EVENTID:
		ath10k_wmi_event_rtt_error_report(ar, skb);
		break;
	case WMI_10_2_WOW_WAKEUP_HOST_EVENTID:
		ath10k_wmi_event_wow_wakeup_host(ar, skb);
		break;
	case WMI_10_2_DCS_INTERFERENCE_EVENTID:
		ath10k_wmi_event_dcs_interference(ar, skb);
		break;
	case WMI_10_2_PDEV_TPC_CONFIG_EVENTID:
		ath10k_wmi_event_pdev_tpc_config(ar, skb);
		break;
	case WMI_10_2_INST_RSSI_STATS_EVENTID:
		ath10k_wmi_event_inst_rssi_stats(ar, skb);
		break;
	case WMI_10_2_VDEV_STANDBY_REQ_EVENTID:
		ath10k_wmi_event_vdev_standby_req(ar, skb);
		break;
	case WMI_10_2_VDEV_RESUME_REQ_EVENTID:
		ath10k_wmi_event_vdev_resume_req(ar, skb);
		break;
	case WMI_10_2_SERVICE_READY_EVENTID:
		ath10k_wmi_10x_service_ready_event_rx(ar, skb);
		break;
	case WMI_10_2_READY_EVENTID:
		ath10k_wmi_ready_event_rx(ar, skb);
		break;
	case WMI_10_2_RTT_KEEPALIVE_EVENTID:
	case WMI_10_2_GPIO_INPUT_EVENTID:
	case WMI_10_2_PEER_RATECODE_LIST_EVENTID:
	case WMI_10_2_GENERIC_BUFFER_EVENTID:
	case WMI_10_2_MCAST_BUF_RELEASE_EVENTID:
	case WMI_10_2_MCAST_LIST_AGEOUT_EVENTID:
	case WMI_10_2_WDS_PEER_EVENTID:
		ath10k_dbg(ar, ATH10K_DBG_WMI,
			   "received event id %d not implemented\n", id);
		break;
	default:
		ath10k_warn(ar, "Unknown eventid: %d\n", id);
		break;
	}

	dev_kfree_skb(skb);
}

static void ath10k_wmi_process_rx(struct ath10k *ar, struct sk_buff *skb)
{
	if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features)) {
		if (test_bit(ATH10K_FW_FEATURE_WMI_10_2, ar->fw_features))
			ath10k_wmi_10_2_process_rx(ar, skb);
		else
			ath10k_wmi_10x_process_rx(ar, skb);
	} else {
		ath10k_wmi_main_process_rx(ar, skb);
	}
}

int ath10k_wmi_connect(struct ath10k *ar)
{
	int status;
	struct ath10k_htc_svc_conn_req conn_req;
	struct ath10k_htc_svc_conn_resp conn_resp;

	memset(&conn_req, 0, sizeof(conn_req));
	memset(&conn_resp, 0, sizeof(conn_resp));

	/* these fields are the same for all service endpoints */
	conn_req.ep_ops.ep_tx_complete = ath10k_wmi_htc_tx_complete;
	conn_req.ep_ops.ep_rx_complete = ath10k_wmi_process_rx;
	conn_req.ep_ops.ep_tx_credits = ath10k_wmi_op_ep_tx_credits;

	/* connect to control service */
	conn_req.service_id = ATH10K_HTC_SVC_ID_WMI_CONTROL;

	status = ath10k_htc_connect_service(&ar->htc, &conn_req, &conn_resp);
	if (status) {
		ath10k_warn(ar, "failed to connect to WMI CONTROL service status: %d\n",
			    status);
		return status;
	}

	ar->wmi.eid = conn_resp.eid;
	return 0;
}

static int ath10k_wmi_main_pdev_set_regdomain(struct ath10k *ar, u16 rd,
					      u16 rd2g, u16 rd5g, u16 ctl2g,
					      u16 ctl5g)
{
	struct wmi_pdev_set_regdomain_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_set_regdomain_cmd *)skb->data;
	cmd->reg_domain = __cpu_to_le32(rd);
	cmd->reg_domain_2G = __cpu_to_le32(rd2g);
	cmd->reg_domain_5G = __cpu_to_le32(rd5g);
	cmd->conformance_test_limit_2G = __cpu_to_le32(ctl2g);
	cmd->conformance_test_limit_5G = __cpu_to_le32(ctl5g);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi pdev regdomain rd %x rd2g %x rd5g %x ctl2g %x ctl5g %x\n",
		   rd, rd2g, rd5g, ctl2g, ctl5g);

	return ath10k_wmi_cmd_send(ar, skb,
				   ar->wmi.cmd->pdev_set_regdomain_cmdid);
}

static int ath10k_wmi_10x_pdev_set_regdomain(struct ath10k *ar, u16 rd,
					     u16 rd2g, u16 rd5g,
					     u16 ctl2g, u16 ctl5g,
					     enum wmi_dfs_region dfs_reg)
{
	struct wmi_pdev_set_regdomain_cmd_10x *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_set_regdomain_cmd_10x *)skb->data;
	cmd->reg_domain = __cpu_to_le32(rd);
	cmd->reg_domain_2G = __cpu_to_le32(rd2g);
	cmd->reg_domain_5G = __cpu_to_le32(rd5g);
	cmd->conformance_test_limit_2G = __cpu_to_le32(ctl2g);
	cmd->conformance_test_limit_5G = __cpu_to_le32(ctl5g);
	cmd->dfs_domain = __cpu_to_le32(dfs_reg);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi pdev regdomain rd %x rd2g %x rd5g %x ctl2g %x ctl5g %x dfs_region %x\n",
		   rd, rd2g, rd5g, ctl2g, ctl5g, dfs_reg);

	return ath10k_wmi_cmd_send(ar, skb,
				   ar->wmi.cmd->pdev_set_regdomain_cmdid);
}

int ath10k_wmi_pdev_set_regdomain(struct ath10k *ar, u16 rd, u16 rd2g,
				  u16 rd5g, u16 ctl2g, u16 ctl5g,
				  enum wmi_dfs_region dfs_reg)
{
	if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features))
		return ath10k_wmi_10x_pdev_set_regdomain(ar, rd, rd2g, rd5g,
							ctl2g, ctl5g, dfs_reg);
	else
		return ath10k_wmi_main_pdev_set_regdomain(ar, rd, rd2g, rd5g,
							 ctl2g, ctl5g);
}

int ath10k_wmi_pdev_suspend_target(struct ath10k *ar, u32 suspend_opt)
{
	struct wmi_pdev_suspend_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_suspend_cmd *)skb->data;
	cmd->suspend_opt = __cpu_to_le32(suspend_opt);

	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->pdev_suspend_cmdid);
}

int ath10k_wmi_pdev_resume_target(struct ath10k *ar)
{
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, 0);
	if (skb == NULL)
		return -ENOMEM;

	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->pdev_resume_cmdid);
}

int ath10k_wmi_pdev_set_param(struct ath10k *ar, u32 id, u32 value)
{
	struct wmi_pdev_set_param_cmd *cmd;
	struct sk_buff *skb;

	if (id == WMI_PDEV_PARAM_UNSUPPORTED) {
		ath10k_warn(ar, "pdev param %d not supported by firmware\n",
			    id);
		return -EOPNOTSUPP;
	}

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_set_param_cmd *)skb->data;
	cmd->param_id    = __cpu_to_le32(id);
	cmd->param_value = __cpu_to_le32(value);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi pdev set param %d value %d\n",
		   id, value);
	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->pdev_set_param_cmdid);
}

static void ath10k_wmi_put_host_mem_chunks(struct ath10k *ar,
					   struct wmi_host_mem_chunks *chunks)
{
	struct host_memory_chunk *chunk;
	int i;

	chunks->count = __cpu_to_le32(ar->wmi.num_mem_chunks);

	for (i = 0; i < ar->wmi.num_mem_chunks; i++) {
		chunk = &chunks->items[i];
		chunk->ptr = __cpu_to_le32(ar->wmi.mem_chunks[i].paddr);
		chunk->size = __cpu_to_le32(ar->wmi.mem_chunks[i].len);
		chunk->req_id = __cpu_to_le32(ar->wmi.mem_chunks[i].req_id);

		ath10k_dbg(ar, ATH10K_DBG_WMI,
			   "wmi chunk %d len %d requested, addr 0x%llx\n",
			   i,
			   ar->wmi.mem_chunks[i].len,
			   (unsigned long long)ar->wmi.mem_chunks[i].paddr);
	}
}

static int ath10k_wmi_main_cmd_init(struct ath10k *ar)
{
	struct wmi_init_cmd *cmd;
	struct sk_buff *buf;
	struct wmi_resource_config config = {};
	u32 len, val;

	config.num_vdevs = __cpu_to_le32(TARGET_NUM_VDEVS);
	config.num_peers = __cpu_to_le32(TARGET_NUM_PEERS + TARGET_NUM_VDEVS);
	config.num_offload_peers = __cpu_to_le32(TARGET_NUM_OFFLOAD_PEERS);

	config.num_offload_reorder_bufs =
		__cpu_to_le32(TARGET_NUM_OFFLOAD_REORDER_BUFS);

	config.num_peer_keys = __cpu_to_le32(TARGET_NUM_PEER_KEYS);
	config.num_tids = __cpu_to_le32(TARGET_NUM_TIDS);
	config.ast_skid_limit = __cpu_to_le32(TARGET_AST_SKID_LIMIT);
	config.tx_chain_mask = __cpu_to_le32(TARGET_TX_CHAIN_MASK);
	config.rx_chain_mask = __cpu_to_le32(TARGET_RX_CHAIN_MASK);
	config.rx_timeout_pri_vo = __cpu_to_le32(TARGET_RX_TIMEOUT_LO_PRI);
	config.rx_timeout_pri_vi = __cpu_to_le32(TARGET_RX_TIMEOUT_LO_PRI);
	config.rx_timeout_pri_be = __cpu_to_le32(TARGET_RX_TIMEOUT_LO_PRI);
	config.rx_timeout_pri_bk = __cpu_to_le32(TARGET_RX_TIMEOUT_HI_PRI);
	config.rx_decap_mode = __cpu_to_le32(TARGET_RX_DECAP_MODE);

	config.scan_max_pending_reqs =
		__cpu_to_le32(TARGET_SCAN_MAX_PENDING_REQS);

	config.bmiss_offload_max_vdev =
		__cpu_to_le32(TARGET_BMISS_OFFLOAD_MAX_VDEV);

	config.roam_offload_max_vdev =
		__cpu_to_le32(TARGET_ROAM_OFFLOAD_MAX_VDEV);

	config.roam_offload_max_ap_profiles =
		__cpu_to_le32(TARGET_ROAM_OFFLOAD_MAX_AP_PROFILES);

	config.num_mcast_groups = __cpu_to_le32(TARGET_NUM_MCAST_GROUPS);
	config.num_mcast_table_elems =
		__cpu_to_le32(TARGET_NUM_MCAST_TABLE_ELEMS);

	config.mcast2ucast_mode = __cpu_to_le32(TARGET_MCAST2UCAST_MODE);
	config.tx_dbg_log_size = __cpu_to_le32(TARGET_TX_DBG_LOG_SIZE);
	config.num_wds_entries = __cpu_to_le32(TARGET_NUM_WDS_ENTRIES);
	config.dma_burst_size = __cpu_to_le32(TARGET_DMA_BURST_SIZE);
	config.mac_aggr_delim = __cpu_to_le32(TARGET_MAC_AGGR_DELIM);

	val = TARGET_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK;
	config.rx_skip_defrag_timeout_dup_detection_check = __cpu_to_le32(val);

	config.vow_config = __cpu_to_le32(TARGET_VOW_CONFIG);

	config.gtk_offload_max_vdev =
		__cpu_to_le32(TARGET_GTK_OFFLOAD_MAX_VDEV);

	config.num_msdu_desc = __cpu_to_le32(TARGET_NUM_MSDU_DESC);
	config.max_frag_entries = __cpu_to_le32(TARGET_MAX_FRAG_ENTRIES);

	len = sizeof(*cmd) +
	      (sizeof(struct host_memory_chunk) * ar->wmi.num_mem_chunks);

	buf = ath10k_wmi_alloc_skb(ar, len);
	if (!buf)
		return -ENOMEM;

	cmd = (struct wmi_init_cmd *)buf->data;

	memcpy(&cmd->resource_config, &config, sizeof(config));
	ath10k_wmi_put_host_mem_chunks(ar, &cmd->mem_chunks);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi init\n");
	return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->init_cmdid);
}

static int ath10k_wmi_10x_cmd_init(struct ath10k *ar)
{
	struct wmi_init_cmd_10x *cmd;
	struct sk_buff *buf;
	struct wmi_resource_config_10x config = {};
	u32 len, val;

	config.num_vdevs = __cpu_to_le32(TARGET_10X_NUM_VDEVS);
	config.num_peers = __cpu_to_le32(TARGET_10X_NUM_PEERS);
	config.num_peer_keys = __cpu_to_le32(TARGET_10X_NUM_PEER_KEYS);
	config.num_tids = __cpu_to_le32(TARGET_10X_NUM_TIDS);
	config.ast_skid_limit = __cpu_to_le32(TARGET_10X_AST_SKID_LIMIT);
	config.tx_chain_mask = __cpu_to_le32(TARGET_10X_TX_CHAIN_MASK);
	config.rx_chain_mask = __cpu_to_le32(TARGET_10X_RX_CHAIN_MASK);
	config.rx_timeout_pri_vo = __cpu_to_le32(TARGET_10X_RX_TIMEOUT_LO_PRI);
	config.rx_timeout_pri_vi = __cpu_to_le32(TARGET_10X_RX_TIMEOUT_LO_PRI);
	config.rx_timeout_pri_be = __cpu_to_le32(TARGET_10X_RX_TIMEOUT_LO_PRI);
	config.rx_timeout_pri_bk = __cpu_to_le32(TARGET_10X_RX_TIMEOUT_HI_PRI);
	config.rx_decap_mode = __cpu_to_le32(TARGET_10X_RX_DECAP_MODE);

	config.scan_max_pending_reqs =
		__cpu_to_le32(TARGET_10X_SCAN_MAX_PENDING_REQS);

	config.bmiss_offload_max_vdev =
		__cpu_to_le32(TARGET_10X_BMISS_OFFLOAD_MAX_VDEV);

	config.roam_offload_max_vdev =
		__cpu_to_le32(TARGET_10X_ROAM_OFFLOAD_MAX_VDEV);

	config.roam_offload_max_ap_profiles =
		__cpu_to_le32(TARGET_10X_ROAM_OFFLOAD_MAX_AP_PROFILES);

	config.num_mcast_groups = __cpu_to_le32(TARGET_10X_NUM_MCAST_GROUPS);
	config.num_mcast_table_elems =
		__cpu_to_le32(TARGET_10X_NUM_MCAST_TABLE_ELEMS);

	config.mcast2ucast_mode = __cpu_to_le32(TARGET_10X_MCAST2UCAST_MODE);
	config.tx_dbg_log_size = __cpu_to_le32(TARGET_10X_TX_DBG_LOG_SIZE);
	config.num_wds_entries = __cpu_to_le32(TARGET_10X_NUM_WDS_ENTRIES);
	config.dma_burst_size = __cpu_to_le32(TARGET_10X_DMA_BURST_SIZE);
	config.mac_aggr_delim = __cpu_to_le32(TARGET_10X_MAC_AGGR_DELIM);

	val = TARGET_10X_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK;
	config.rx_skip_defrag_timeout_dup_detection_check = __cpu_to_le32(val);

	config.vow_config = __cpu_to_le32(TARGET_10X_VOW_CONFIG);

	config.num_msdu_desc = __cpu_to_le32(TARGET_10X_NUM_MSDU_DESC);
	config.max_frag_entries = __cpu_to_le32(TARGET_10X_MAX_FRAG_ENTRIES);

	len = sizeof(*cmd) +
	      (sizeof(struct host_memory_chunk) * ar->wmi.num_mem_chunks);

	buf = ath10k_wmi_alloc_skb(ar, len);
	if (!buf)
		return -ENOMEM;

	cmd = (struct wmi_init_cmd_10x *)buf->data;

	memcpy(&cmd->resource_config, &config, sizeof(config));
	ath10k_wmi_put_host_mem_chunks(ar, &cmd->mem_chunks);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi init 10x\n");
	return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->init_cmdid);
}

static int ath10k_wmi_10_2_cmd_init(struct ath10k *ar)
{
	struct wmi_init_cmd_10_2 *cmd;
	struct sk_buff *buf;
	struct wmi_resource_config_10x config = {};
	u32 len, val;

	config.num_vdevs = __cpu_to_le32(TARGET_10X_NUM_VDEVS);
	config.num_peers = __cpu_to_le32(TARGET_10X_NUM_PEERS);
	config.num_peer_keys = __cpu_to_le32(TARGET_10X_NUM_PEER_KEYS);
	config.num_tids = __cpu_to_le32(TARGET_10X_NUM_TIDS);
	config.ast_skid_limit = __cpu_to_le32(TARGET_10X_AST_SKID_LIMIT);
	config.tx_chain_mask = __cpu_to_le32(TARGET_10X_TX_CHAIN_MASK);
	config.rx_chain_mask = __cpu_to_le32(TARGET_10X_RX_CHAIN_MASK);
	config.rx_timeout_pri_vo = __cpu_to_le32(TARGET_10X_RX_TIMEOUT_LO_PRI);
	config.rx_timeout_pri_vi = __cpu_to_le32(TARGET_10X_RX_TIMEOUT_LO_PRI);
	config.rx_timeout_pri_be = __cpu_to_le32(TARGET_10X_RX_TIMEOUT_LO_PRI);
	config.rx_timeout_pri_bk = __cpu_to_le32(TARGET_10X_RX_TIMEOUT_HI_PRI);
	config.rx_decap_mode = __cpu_to_le32(TARGET_10X_RX_DECAP_MODE);

	config.scan_max_pending_reqs =
		__cpu_to_le32(TARGET_10X_SCAN_MAX_PENDING_REQS);

	config.bmiss_offload_max_vdev =
		__cpu_to_le32(TARGET_10X_BMISS_OFFLOAD_MAX_VDEV);

	config.roam_offload_max_vdev =
		__cpu_to_le32(TARGET_10X_ROAM_OFFLOAD_MAX_VDEV);

	config.roam_offload_max_ap_profiles =
		__cpu_to_le32(TARGET_10X_ROAM_OFFLOAD_MAX_AP_PROFILES);

	config.num_mcast_groups = __cpu_to_le32(TARGET_10X_NUM_MCAST_GROUPS);
	config.num_mcast_table_elems =
		__cpu_to_le32(TARGET_10X_NUM_MCAST_TABLE_ELEMS);

	config.mcast2ucast_mode = __cpu_to_le32(TARGET_10X_MCAST2UCAST_MODE);
	config.tx_dbg_log_size = __cpu_to_le32(TARGET_10X_TX_DBG_LOG_SIZE);
	config.num_wds_entries = __cpu_to_le32(TARGET_10X_NUM_WDS_ENTRIES);
	config.dma_burst_size = __cpu_to_le32(TARGET_10X_DMA_BURST_SIZE);
	config.mac_aggr_delim = __cpu_to_le32(TARGET_10X_MAC_AGGR_DELIM);

	val = TARGET_10X_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK;
	config.rx_skip_defrag_timeout_dup_detection_check = __cpu_to_le32(val);

	config.vow_config = __cpu_to_le32(TARGET_10X_VOW_CONFIG);

	config.num_msdu_desc = __cpu_to_le32(TARGET_10X_NUM_MSDU_DESC);
	config.max_frag_entries = __cpu_to_le32(TARGET_10X_MAX_FRAG_ENTRIES);

	len = sizeof(*cmd) +
	      (sizeof(struct host_memory_chunk) * ar->wmi.num_mem_chunks);

	buf = ath10k_wmi_alloc_skb(ar, len);
	if (!buf)
		return -ENOMEM;

	cmd = (struct wmi_init_cmd_10_2 *)buf->data;

	memcpy(&cmd->resource_config.common, &config, sizeof(config));
	ath10k_wmi_put_host_mem_chunks(ar, &cmd->mem_chunks);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi init 10.2\n");
	return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->init_cmdid);
}

int ath10k_wmi_cmd_init(struct ath10k *ar)
{
	int ret;

	if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features)) {
		if (test_bit(ATH10K_FW_FEATURE_WMI_10_2, ar->fw_features))
			ret = ath10k_wmi_10_2_cmd_init(ar);
		else
			ret = ath10k_wmi_10x_cmd_init(ar);
	} else {
		ret = ath10k_wmi_main_cmd_init(ar);
	}

	return ret;
}

static int ath10k_wmi_start_scan_calc_len(struct ath10k *ar,
					  const struct wmi_start_scan_arg *arg)
{
	int len;

	if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features))
		len = sizeof(struct wmi_start_scan_cmd_10x);
	else
		len = sizeof(struct wmi_start_scan_cmd);

	if (arg->ie_len) {
		if (!arg->ie)
			return -EINVAL;
		if (arg->ie_len > WLAN_SCAN_PARAMS_MAX_IE_LEN)
			return -EINVAL;

		len += sizeof(struct wmi_ie_data);
		len += roundup(arg->ie_len, 4);
	}

	if (arg->n_channels) {
		if (!arg->channels)
			return -EINVAL;
		if (arg->n_channels > ARRAY_SIZE(arg->channels))
			return -EINVAL;

		len += sizeof(struct wmi_chan_list);
		len += sizeof(__le32) * arg->n_channels;
	}

	if (arg->n_ssids) {
		if (!arg->ssids)
			return -EINVAL;
		if (arg->n_ssids > WLAN_SCAN_PARAMS_MAX_SSID)
			return -EINVAL;

		len += sizeof(struct wmi_ssid_list);
		len += sizeof(struct wmi_ssid) * arg->n_ssids;
	}

	if (arg->n_bssids) {
		if (!arg->bssids)
			return -EINVAL;
		if (arg->n_bssids > WLAN_SCAN_PARAMS_MAX_BSSID)
			return -EINVAL;

		len += sizeof(struct wmi_bssid_list);
		len += sizeof(struct wmi_mac_addr) * arg->n_bssids;
	}

	return len;
}

int ath10k_wmi_start_scan(struct ath10k *ar,
			  const struct wmi_start_scan_arg *arg)
{
	struct wmi_start_scan_cmd *cmd;
	struct sk_buff *skb;
	struct wmi_ie_data *ie;
	struct wmi_chan_list *channels;
	struct wmi_ssid_list *ssids;
	struct wmi_bssid_list *bssids;
	u32 scan_id;
	u32 scan_req_id;
	int off;
	int len = 0;
	int i;

	len = ath10k_wmi_start_scan_calc_len(ar, arg);
	if (len < 0)
		return len; /* len contains error code here */

	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return -ENOMEM;

	scan_id  = WMI_HOST_SCAN_REQ_ID_PREFIX;
	scan_id |= arg->scan_id;

	scan_req_id  = WMI_HOST_SCAN_REQUESTOR_ID_PREFIX;
	scan_req_id |= arg->scan_req_id;

	cmd = (struct wmi_start_scan_cmd *)skb->data;
	cmd->scan_id            = __cpu_to_le32(scan_id);
	cmd->scan_req_id        = __cpu_to_le32(scan_req_id);
	cmd->vdev_id            = __cpu_to_le32(arg->vdev_id);
	cmd->scan_priority      = __cpu_to_le32(arg->scan_priority);
	cmd->notify_scan_events = __cpu_to_le32(arg->notify_scan_events);
	cmd->dwell_time_active  = __cpu_to_le32(arg->dwell_time_active);
	cmd->dwell_time_passive = __cpu_to_le32(arg->dwell_time_passive);
	cmd->min_rest_time      = __cpu_to_le32(arg->min_rest_time);
	cmd->max_rest_time      = __cpu_to_le32(arg->max_rest_time);
	cmd->repeat_probe_time  = __cpu_to_le32(arg->repeat_probe_time);
	cmd->probe_spacing_time = __cpu_to_le32(arg->probe_spacing_time);
	cmd->idle_time          = __cpu_to_le32(arg->idle_time);
	cmd->max_scan_time      = __cpu_to_le32(arg->max_scan_time);
	cmd->probe_delay        = __cpu_to_le32(arg->probe_delay);
	cmd->scan_ctrl_flags    = __cpu_to_le32(arg->scan_ctrl_flags);

	/* TLV list starts after fields included in the struct */
	/* There's just one filed that differes the two start_scan
	 * structures - burst_duration, which we are not using btw,
	   no point to make the split here, just shift the buffer to fit with
	   given FW */
	if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features))
		off = sizeof(struct wmi_start_scan_cmd_10x);
	else
		off = sizeof(struct wmi_start_scan_cmd);

	if (arg->n_channels) {
		channels = (void *)skb->data + off;
		channels->tag = __cpu_to_le32(WMI_CHAN_LIST_TAG);
		channels->num_chan = __cpu_to_le32(arg->n_channels);

		for (i = 0; i < arg->n_channels; i++)
			channels->channel_list[i].freq =
				__cpu_to_le16(arg->channels[i]);

		off += sizeof(*channels);
		off += sizeof(__le32) * arg->n_channels;
	}

	if (arg->n_ssids) {
		ssids = (void *)skb->data + off;
		ssids->tag = __cpu_to_le32(WMI_SSID_LIST_TAG);
		ssids->num_ssids = __cpu_to_le32(arg->n_ssids);

		for (i = 0; i < arg->n_ssids; i++) {
			ssids->ssids[i].ssid_len =
				__cpu_to_le32(arg->ssids[i].len);
			memcpy(&ssids->ssids[i].ssid,
			       arg->ssids[i].ssid,
			       arg->ssids[i].len);
		}

		off += sizeof(*ssids);
		off += sizeof(struct wmi_ssid) * arg->n_ssids;
	}

	if (arg->n_bssids) {
		bssids = (void *)skb->data + off;
		bssids->tag = __cpu_to_le32(WMI_BSSID_LIST_TAG);
		bssids->num_bssid = __cpu_to_le32(arg->n_bssids);

		for (i = 0; i < arg->n_bssids; i++)
			memcpy(&bssids->bssid_list[i],
			       arg->bssids[i].bssid,
			       ETH_ALEN);

		off += sizeof(*bssids);
		off += sizeof(struct wmi_mac_addr) * arg->n_bssids;
	}

	if (arg->ie_len) {
		ie = (void *)skb->data + off;
		ie->tag = __cpu_to_le32(WMI_IE_TAG);
		ie->ie_len = __cpu_to_le32(arg->ie_len);
		memcpy(ie->ie_data, arg->ie, arg->ie_len);

		off += sizeof(*ie);
		off += roundup(arg->ie_len, 4);
	}

	if (off != skb->len) {
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi start scan\n");
	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->start_scan_cmdid);
}

void ath10k_wmi_start_scan_init(struct ath10k *ar,
				struct wmi_start_scan_arg *arg)
{
	/* setup commonly used values */
	arg->scan_req_id = 1;
	arg->scan_priority = WMI_SCAN_PRIORITY_LOW;
	arg->dwell_time_active = 50;
	arg->dwell_time_passive = 150;
	arg->min_rest_time = 50;
	arg->max_rest_time = 500;
	arg->repeat_probe_time = 0;
	arg->probe_spacing_time = 0;
	arg->idle_time = 0;
	arg->max_scan_time = 20000;
	arg->probe_delay = 5;
	arg->notify_scan_events = WMI_SCAN_EVENT_STARTED
		| WMI_SCAN_EVENT_COMPLETED
		| WMI_SCAN_EVENT_BSS_CHANNEL
		| WMI_SCAN_EVENT_FOREIGN_CHANNEL
		| WMI_SCAN_EVENT_DEQUEUED;
	arg->scan_ctrl_flags |= WMI_SCAN_ADD_OFDM_RATES;
	arg->scan_ctrl_flags |= WMI_SCAN_CHAN_STAT_EVENT;
	arg->n_bssids = 1;
	arg->bssids[0].bssid = "\xFF\xFF\xFF\xFF\xFF\xFF";
}

int ath10k_wmi_stop_scan(struct ath10k *ar, const struct wmi_stop_scan_arg *arg)
{
	struct wmi_stop_scan_cmd *cmd;
	struct sk_buff *skb;
	u32 scan_id;
	u32 req_id;

	if (arg->req_id > 0xFFF)
		return -EINVAL;
	if (arg->req_type == WMI_SCAN_STOP_ONE && arg->u.scan_id > 0xFFF)
		return -EINVAL;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	scan_id = arg->u.scan_id;
	scan_id |= WMI_HOST_SCAN_REQ_ID_PREFIX;

	req_id = arg->req_id;
	req_id |= WMI_HOST_SCAN_REQUESTOR_ID_PREFIX;

	cmd = (struct wmi_stop_scan_cmd *)skb->data;
	cmd->req_type    = __cpu_to_le32(arg->req_type);
	cmd->vdev_id     = __cpu_to_le32(arg->u.vdev_id);
	cmd->scan_id     = __cpu_to_le32(scan_id);
	cmd->scan_req_id = __cpu_to_le32(req_id);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi stop scan reqid %d req_type %d vdev/scan_id %d\n",
		   arg->req_id, arg->req_type, arg->u.scan_id);
	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->stop_scan_cmdid);
}

int ath10k_wmi_vdev_create(struct ath10k *ar, u32 vdev_id,
			   enum wmi_vdev_type type,
			   enum wmi_vdev_subtype subtype,
			   const u8 macaddr[ETH_ALEN])
{
	struct wmi_vdev_create_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_create_cmd *)skb->data;
	cmd->vdev_id      = __cpu_to_le32(vdev_id);
	cmd->vdev_type    = __cpu_to_le32(type);
	cmd->vdev_subtype = __cpu_to_le32(subtype);
	ether_addr_copy(cmd->vdev_macaddr.addr, macaddr);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "WMI vdev create: id %d type %d subtype %d macaddr %pM\n",
		   vdev_id, type, subtype, macaddr);

	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->vdev_create_cmdid);
}

int ath10k_wmi_vdev_delete(struct ath10k *ar, u32 vdev_id)
{
	struct wmi_vdev_delete_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_delete_cmd *)skb->data;
	cmd->vdev_id = __cpu_to_le32(vdev_id);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "WMI vdev delete id %d\n", vdev_id);

	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->vdev_delete_cmdid);
}

static int
ath10k_wmi_vdev_start_restart(struct ath10k *ar,
			      const struct wmi_vdev_start_request_arg *arg,
			      u32 cmd_id)
{
	struct wmi_vdev_start_request_cmd *cmd;
	struct sk_buff *skb;
	const char *cmdname;
	u32 flags = 0;

	if (cmd_id != ar->wmi.cmd->vdev_start_request_cmdid &&
	    cmd_id != ar->wmi.cmd->vdev_restart_request_cmdid)
		return -EINVAL;
	if (WARN_ON(arg->ssid && arg->ssid_len == 0))
		return -EINVAL;
	if (WARN_ON(arg->hidden_ssid && !arg->ssid))
		return -EINVAL;
	if (WARN_ON(arg->ssid_len > sizeof(cmd->ssid.ssid)))
		return -EINVAL;

	if (cmd_id == ar->wmi.cmd->vdev_start_request_cmdid)
		cmdname = "start";
	else if (cmd_id == ar->wmi.cmd->vdev_restart_request_cmdid)
		cmdname = "restart";
	else
		return -EINVAL; /* should not happen, we already check cmd_id */

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	if (arg->hidden_ssid)
		flags |= WMI_VDEV_START_HIDDEN_SSID;
	if (arg->pmf_enabled)
		flags |= WMI_VDEV_START_PMF_ENABLED;

	cmd = (struct wmi_vdev_start_request_cmd *)skb->data;
	cmd->vdev_id         = __cpu_to_le32(arg->vdev_id);
	cmd->disable_hw_ack  = __cpu_to_le32(arg->disable_hw_ack);
	cmd->beacon_interval = __cpu_to_le32(arg->bcn_intval);
	cmd->dtim_period     = __cpu_to_le32(arg->dtim_period);
	cmd->flags           = __cpu_to_le32(flags);
	cmd->bcn_tx_rate     = __cpu_to_le32(arg->bcn_tx_rate);
	cmd->bcn_tx_power    = __cpu_to_le32(arg->bcn_tx_power);

	if (arg->ssid) {
		cmd->ssid.ssid_len = __cpu_to_le32(arg->ssid_len);
		memcpy(cmd->ssid.ssid, arg->ssid, arg->ssid_len);
	}

	ath10k_wmi_put_wmi_channel(&cmd->chan, &arg->channel);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi vdev %s id 0x%x flags: 0x%0X, freq %d, mode %d, ch_flags: 0x%0X, max_power: %d\n",
		   cmdname, arg->vdev_id,
		   flags, arg->channel.freq, arg->channel.mode,
		   cmd->chan.flags, arg->channel.max_power);

	return ath10k_wmi_cmd_send(ar, skb, cmd_id);
}

int ath10k_wmi_vdev_start(struct ath10k *ar,
			  const struct wmi_vdev_start_request_arg *arg)
{
	u32 cmd_id = ar->wmi.cmd->vdev_start_request_cmdid;

	return ath10k_wmi_vdev_start_restart(ar, arg, cmd_id);
}

int ath10k_wmi_vdev_restart(struct ath10k *ar,
			    const struct wmi_vdev_start_request_arg *arg)
{
	u32 cmd_id = ar->wmi.cmd->vdev_restart_request_cmdid;

	return ath10k_wmi_vdev_start_restart(ar, arg, cmd_id);
}

int ath10k_wmi_vdev_stop(struct ath10k *ar, u32 vdev_id)
{
	struct wmi_vdev_stop_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_stop_cmd *)skb->data;
	cmd->vdev_id = __cpu_to_le32(vdev_id);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi vdev stop id 0x%x\n", vdev_id);

	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->vdev_stop_cmdid);
}

int ath10k_wmi_vdev_up(struct ath10k *ar, u32 vdev_id, u32 aid, const u8 *bssid)
{
	struct wmi_vdev_up_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_up_cmd *)skb->data;
	cmd->vdev_id       = __cpu_to_le32(vdev_id);
	cmd->vdev_assoc_id = __cpu_to_le32(aid);
	ether_addr_copy(cmd->vdev_bssid.addr, bssid);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi mgmt vdev up id 0x%x assoc id %d bssid %pM\n",
		   vdev_id, aid, bssid);

	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->vdev_up_cmdid);
}

int ath10k_wmi_vdev_down(struct ath10k *ar, u32 vdev_id)
{
	struct wmi_vdev_down_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_down_cmd *)skb->data;
	cmd->vdev_id = __cpu_to_le32(vdev_id);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi mgmt vdev down id 0x%x\n", vdev_id);

	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->vdev_down_cmdid);
}

int ath10k_wmi_vdev_set_param(struct ath10k *ar, u32 vdev_id,
			      u32 param_id, u32 param_value)
{
	struct wmi_vdev_set_param_cmd *cmd;
	struct sk_buff *skb;

	if (param_id == WMI_VDEV_PARAM_UNSUPPORTED) {
		ath10k_dbg(ar, ATH10K_DBG_WMI,
			   "vdev param %d not supported by firmware\n",
			    param_id);
		return -EOPNOTSUPP;
	}

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_set_param_cmd *)skb->data;
	cmd->vdev_id     = __cpu_to_le32(vdev_id);
	cmd->param_id    = __cpu_to_le32(param_id);
	cmd->param_value = __cpu_to_le32(param_value);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi vdev id 0x%x set param %d value %d\n",
		   vdev_id, param_id, param_value);

	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->vdev_set_param_cmdid);
}

int ath10k_wmi_vdev_install_key(struct ath10k *ar,
				const struct wmi_vdev_install_key_arg *arg)
{
	struct wmi_vdev_install_key_cmd *cmd;
	struct sk_buff *skb;

	if (arg->key_cipher == WMI_CIPHER_NONE && arg->key_data != NULL)
		return -EINVAL;
	if (arg->key_cipher != WMI_CIPHER_NONE && arg->key_data == NULL)
		return -EINVAL;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd) + arg->key_len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_install_key_cmd *)skb->data;
	cmd->vdev_id       = __cpu_to_le32(arg->vdev_id);
	cmd->key_idx       = __cpu_to_le32(arg->key_idx);
	cmd->key_flags     = __cpu_to_le32(arg->key_flags);
	cmd->key_cipher    = __cpu_to_le32(arg->key_cipher);
	cmd->key_len       = __cpu_to_le32(arg->key_len);
	cmd->key_txmic_len = __cpu_to_le32(arg->key_txmic_len);
	cmd->key_rxmic_len = __cpu_to_le32(arg->key_rxmic_len);

	if (arg->macaddr)
		ether_addr_copy(cmd->peer_macaddr.addr, arg->macaddr);
	if (arg->key_data)
		memcpy(cmd->key_data, arg->key_data, arg->key_len);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi vdev install key idx %d cipher %d len %d\n",
		   arg->key_idx, arg->key_cipher, arg->key_len);
	return ath10k_wmi_cmd_send(ar, skb,
				   ar->wmi.cmd->vdev_install_key_cmdid);
}

int ath10k_wmi_vdev_spectral_conf(struct ath10k *ar,
				  const struct wmi_vdev_spectral_conf_arg *arg)
{
	struct wmi_vdev_spectral_conf_cmd *cmd;
	struct sk_buff *skb;
	u32 cmdid;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_spectral_conf_cmd *)skb->data;
	cmd->vdev_id = __cpu_to_le32(arg->vdev_id);
	cmd->scan_count = __cpu_to_le32(arg->scan_count);
	cmd->scan_period = __cpu_to_le32(arg->scan_period);
	cmd->scan_priority = __cpu_to_le32(arg->scan_priority);
	cmd->scan_fft_size = __cpu_to_le32(arg->scan_fft_size);
	cmd->scan_gc_ena = __cpu_to_le32(arg->scan_gc_ena);
	cmd->scan_restart_ena = __cpu_to_le32(arg->scan_restart_ena);
	cmd->scan_noise_floor_ref = __cpu_to_le32(arg->scan_noise_floor_ref);
	cmd->scan_init_delay = __cpu_to_le32(arg->scan_init_delay);
	cmd->scan_nb_tone_thr = __cpu_to_le32(arg->scan_nb_tone_thr);
	cmd->scan_str_bin_thr = __cpu_to_le32(arg->scan_str_bin_thr);
	cmd->scan_wb_rpt_mode = __cpu_to_le32(arg->scan_wb_rpt_mode);
	cmd->scan_rssi_rpt_mode = __cpu_to_le32(arg->scan_rssi_rpt_mode);
	cmd->scan_rssi_thr = __cpu_to_le32(arg->scan_rssi_thr);
	cmd->scan_pwr_format = __cpu_to_le32(arg->scan_pwr_format);
	cmd->scan_rpt_mode = __cpu_to_le32(arg->scan_rpt_mode);
	cmd->scan_bin_scale = __cpu_to_le32(arg->scan_bin_scale);
	cmd->scan_dbm_adj = __cpu_to_le32(arg->scan_dbm_adj);
	cmd->scan_chn_mask = __cpu_to_le32(arg->scan_chn_mask);

	cmdid = ar->wmi.cmd->vdev_spectral_scan_configure_cmdid;
	return ath10k_wmi_cmd_send(ar, skb, cmdid);
}

int ath10k_wmi_vdev_spectral_enable(struct ath10k *ar, u32 vdev_id, u32 trigger,
				    u32 enable)
{
	struct wmi_vdev_spectral_enable_cmd *cmd;
	struct sk_buff *skb;
	u32 cmdid;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_spectral_enable_cmd *)skb->data;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	cmd->trigger_cmd = __cpu_to_le32(trigger);
	cmd->enable_cmd = __cpu_to_le32(enable);

	cmdid = ar->wmi.cmd->vdev_spectral_scan_enable_cmdid;
	return ath10k_wmi_cmd_send(ar, skb, cmdid);
}

int ath10k_wmi_peer_create(struct ath10k *ar, u32 vdev_id,
			   const u8 peer_addr[ETH_ALEN])
{
	struct wmi_peer_create_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_peer_create_cmd *)skb->data;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	ether_addr_copy(cmd->peer_macaddr.addr, peer_addr);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi peer create vdev_id %d peer_addr %pM\n",
		   vdev_id, peer_addr);
	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->peer_create_cmdid);
}

int ath10k_wmi_peer_delete(struct ath10k *ar, u32 vdev_id,
			   const u8 peer_addr[ETH_ALEN])
{
	struct wmi_peer_delete_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_peer_delete_cmd *)skb->data;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	ether_addr_copy(cmd->peer_macaddr.addr, peer_addr);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi peer delete vdev_id %d peer_addr %pM\n",
		   vdev_id, peer_addr);
	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->peer_delete_cmdid);
}

int ath10k_wmi_peer_flush(struct ath10k *ar, u32 vdev_id,
			  const u8 peer_addr[ETH_ALEN], u32 tid_bitmap)
{
	struct wmi_peer_flush_tids_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_peer_flush_tids_cmd *)skb->data;
	cmd->vdev_id         = __cpu_to_le32(vdev_id);
	cmd->peer_tid_bitmap = __cpu_to_le32(tid_bitmap);
	ether_addr_copy(cmd->peer_macaddr.addr, peer_addr);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi peer flush vdev_id %d peer_addr %pM tids %08x\n",
		   vdev_id, peer_addr, tid_bitmap);
	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->peer_flush_tids_cmdid);
}

int ath10k_wmi_peer_set_param(struct ath10k *ar, u32 vdev_id,
			      const u8 *peer_addr, enum wmi_peer_param param_id,
			      u32 param_value)
{
	struct wmi_peer_set_param_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_peer_set_param_cmd *)skb->data;
	cmd->vdev_id     = __cpu_to_le32(vdev_id);
	cmd->param_id    = __cpu_to_le32(param_id);
	cmd->param_value = __cpu_to_le32(param_value);
	ether_addr_copy(cmd->peer_macaddr.addr, peer_addr);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi vdev %d peer 0x%pM set param %d value %d\n",
		   vdev_id, peer_addr, param_id, param_value);

	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->peer_set_param_cmdid);
}

int ath10k_wmi_set_psmode(struct ath10k *ar, u32 vdev_id,
			  enum wmi_sta_ps_mode psmode)
{
	struct wmi_sta_powersave_mode_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_sta_powersave_mode_cmd *)skb->data;
	cmd->vdev_id     = __cpu_to_le32(vdev_id);
	cmd->sta_ps_mode = __cpu_to_le32(psmode);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi set powersave id 0x%x mode %d\n",
		   vdev_id, psmode);

	return ath10k_wmi_cmd_send(ar, skb,
				   ar->wmi.cmd->sta_powersave_mode_cmdid);
}

int ath10k_wmi_set_sta_ps_param(struct ath10k *ar, u32 vdev_id,
				enum wmi_sta_powersave_param param_id,
				u32 value)
{
	struct wmi_sta_powersave_param_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_sta_powersave_param_cmd *)skb->data;
	cmd->vdev_id     = __cpu_to_le32(vdev_id);
	cmd->param_id    = __cpu_to_le32(param_id);
	cmd->param_value = __cpu_to_le32(value);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi sta ps param vdev_id 0x%x param %d value %d\n",
		   vdev_id, param_id, value);
	return ath10k_wmi_cmd_send(ar, skb,
				   ar->wmi.cmd->sta_powersave_param_cmdid);
}

int ath10k_wmi_set_ap_ps_param(struct ath10k *ar, u32 vdev_id, const u8 *mac,
			       enum wmi_ap_ps_peer_param param_id, u32 value)
{
	struct wmi_ap_ps_peer_cmd *cmd;
	struct sk_buff *skb;

	if (!mac)
		return -EINVAL;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_ap_ps_peer_cmd *)skb->data;
	cmd->vdev_id = __cpu_to_le32(vdev_id);
	cmd->param_id = __cpu_to_le32(param_id);
	cmd->param_value = __cpu_to_le32(value);
	ether_addr_copy(cmd->peer_macaddr.addr, mac);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi ap ps param vdev_id 0x%X param %d value %d mac_addr %pM\n",
		   vdev_id, param_id, value, mac);

	return ath10k_wmi_cmd_send(ar, skb,
				   ar->wmi.cmd->ap_ps_peer_param_cmdid);
}

int ath10k_wmi_scan_chan_list(struct ath10k *ar,
			      const struct wmi_scan_chan_list_arg *arg)
{
	struct wmi_scan_chan_list_cmd *cmd;
	struct sk_buff *skb;
	struct wmi_channel_arg *ch;
	struct wmi_channel *ci;
	int len;
	int i;

	len = sizeof(*cmd) + arg->n_channels * sizeof(struct wmi_channel);

	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return -EINVAL;

	cmd = (struct wmi_scan_chan_list_cmd *)skb->data;
	cmd->num_scan_chans = __cpu_to_le32(arg->n_channels);

	for (i = 0; i < arg->n_channels; i++) {
		ch = &arg->channels[i];
		ci = &cmd->chan_info[i];

		ath10k_wmi_put_wmi_channel(ci, ch);
	}

	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->scan_chan_list_cmdid);
}

static void
ath10k_wmi_peer_assoc_fill(struct ath10k *ar, void *buf,
			   const struct wmi_peer_assoc_complete_arg *arg)
{
	struct wmi_common_peer_assoc_complete_cmd *cmd = buf;

	cmd->vdev_id            = __cpu_to_le32(arg->vdev_id);
	cmd->peer_new_assoc     = __cpu_to_le32(arg->peer_reassoc ? 0 : 1);
	cmd->peer_associd       = __cpu_to_le32(arg->peer_aid);
	cmd->peer_flags         = __cpu_to_le32(arg->peer_flags);
	cmd->peer_caps          = __cpu_to_le32(arg->peer_caps);
	cmd->peer_listen_intval = __cpu_to_le32(arg->peer_listen_intval);
	cmd->peer_ht_caps       = __cpu_to_le32(arg->peer_ht_caps);
	cmd->peer_max_mpdu      = __cpu_to_le32(arg->peer_max_mpdu);
	cmd->peer_mpdu_density  = __cpu_to_le32(arg->peer_mpdu_density);
	cmd->peer_rate_caps     = __cpu_to_le32(arg->peer_rate_caps);
	cmd->peer_nss           = __cpu_to_le32(arg->peer_num_spatial_streams);
	cmd->peer_vht_caps      = __cpu_to_le32(arg->peer_vht_caps);
	cmd->peer_phymode       = __cpu_to_le32(arg->peer_phymode);

	ether_addr_copy(cmd->peer_macaddr.addr, arg->addr);

	cmd->peer_legacy_rates.num_rates =
		__cpu_to_le32(arg->peer_legacy_rates.num_rates);
	memcpy(cmd->peer_legacy_rates.rates, arg->peer_legacy_rates.rates,
	       arg->peer_legacy_rates.num_rates);

	cmd->peer_ht_rates.num_rates =
		__cpu_to_le32(arg->peer_ht_rates.num_rates);
	memcpy(cmd->peer_ht_rates.rates, arg->peer_ht_rates.rates,
	       arg->peer_ht_rates.num_rates);

	cmd->peer_vht_rates.rx_max_rate =
		__cpu_to_le32(arg->peer_vht_rates.rx_max_rate);
	cmd->peer_vht_rates.rx_mcs_set =
		__cpu_to_le32(arg->peer_vht_rates.rx_mcs_set);
	cmd->peer_vht_rates.tx_max_rate =
		__cpu_to_le32(arg->peer_vht_rates.tx_max_rate);
	cmd->peer_vht_rates.tx_mcs_set =
		__cpu_to_le32(arg->peer_vht_rates.tx_mcs_set);
}

static void
ath10k_wmi_peer_assoc_fill_main(struct ath10k *ar, void *buf,
				const struct wmi_peer_assoc_complete_arg *arg)
{
	struct wmi_main_peer_assoc_complete_cmd *cmd = buf;

	ath10k_wmi_peer_assoc_fill(ar, buf, arg);
	memset(cmd->peer_ht_info, 0, sizeof(cmd->peer_ht_info));
}

static void
ath10k_wmi_peer_assoc_fill_10_1(struct ath10k *ar, void *buf,
				const struct wmi_peer_assoc_complete_arg *arg)
{
	ath10k_wmi_peer_assoc_fill(ar, buf, arg);
}

static void
ath10k_wmi_peer_assoc_fill_10_2(struct ath10k *ar, void *buf,
				const struct wmi_peer_assoc_complete_arg *arg)
{
	struct wmi_10_2_peer_assoc_complete_cmd *cmd = buf;
	int max_mcs, max_nss;
	u32 info0;

	/* TODO: Is using max values okay with firmware? */
	max_mcs = 0xf;
	max_nss = 0xf;

	info0 = SM(max_mcs, WMI_PEER_ASSOC_INFO0_MAX_MCS_IDX) |
		SM(max_nss, WMI_PEER_ASSOC_INFO0_MAX_NSS);

	ath10k_wmi_peer_assoc_fill(ar, buf, arg);
	cmd->info0 = __cpu_to_le32(info0);
}

int ath10k_wmi_peer_assoc(struct ath10k *ar,
			  const struct wmi_peer_assoc_complete_arg *arg)
{
	struct sk_buff *skb;
	int len;

	if (arg->peer_mpdu_density > 16)
		return -EINVAL;
	if (arg->peer_legacy_rates.num_rates > MAX_SUPPORTED_RATES)
		return -EINVAL;
	if (arg->peer_ht_rates.num_rates > MAX_SUPPORTED_RATES)
		return -EINVAL;

	if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features)) {
		if (test_bit(ATH10K_FW_FEATURE_WMI_10_2, ar->fw_features))
			len = sizeof(struct wmi_10_2_peer_assoc_complete_cmd);
		else
			len = sizeof(struct wmi_10_1_peer_assoc_complete_cmd);
	} else {
		len = sizeof(struct wmi_main_peer_assoc_complete_cmd);
	}

	skb = ath10k_wmi_alloc_skb(ar, len);
	if (!skb)
		return -ENOMEM;

	if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features)) {
		if (test_bit(ATH10K_FW_FEATURE_WMI_10_2, ar->fw_features))
			ath10k_wmi_peer_assoc_fill_10_1(ar, skb->data, arg);
		else
			ath10k_wmi_peer_assoc_fill_10_2(ar, skb->data, arg);
	} else {
		ath10k_wmi_peer_assoc_fill_main(ar, skb->data, arg);
	}

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi peer assoc vdev %d addr %pM (%s)\n",
		   arg->vdev_id, arg->addr,
		   arg->peer_reassoc ? "reassociate" : "new");
	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->peer_assoc_cmdid);
}

/* This function assumes the beacon is already DMA mapped */
int ath10k_wmi_beacon_send_ref_nowait(struct ath10k_vif *arvif)
{
	struct wmi_bcn_tx_ref_cmd *cmd;
	struct sk_buff *skb;
	struct sk_buff *beacon = arvif->beacon;
	struct ath10k *ar = arvif->ar;
	struct ieee80211_hdr *hdr;
	int ret;
	u16 fc;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	hdr = (struct ieee80211_hdr *)beacon->data;
	fc = le16_to_cpu(hdr->frame_control);

	cmd = (struct wmi_bcn_tx_ref_cmd *)skb->data;
	cmd->vdev_id = __cpu_to_le32(arvif->vdev_id);
	cmd->data_len = __cpu_to_le32(beacon->len);
	cmd->data_ptr = __cpu_to_le32(ATH10K_SKB_CB(beacon)->paddr);
	cmd->msdu_id = 0;
	cmd->frame_control = __cpu_to_le32(fc);
	cmd->flags = 0;
	cmd->antenna_mask = __cpu_to_le32(WMI_BCN_TX_REF_DEF_ANTENNA);

	if (ATH10K_SKB_CB(beacon)->bcn.dtim_zero)
		cmd->flags |= __cpu_to_le32(WMI_BCN_TX_REF_FLAG_DTIM_ZERO);

	if (ATH10K_SKB_CB(beacon)->bcn.deliver_cab)
		cmd->flags |= __cpu_to_le32(WMI_BCN_TX_REF_FLAG_DELIVER_CAB);

	ret = ath10k_wmi_cmd_send_nowait(ar, skb,
					 ar->wmi.cmd->pdev_send_bcn_cmdid);

	if (ret)
		dev_kfree_skb(skb);

	return ret;
}

static void ath10k_wmi_pdev_set_wmm_param(struct wmi_wmm_params *params,
					  const struct wmi_wmm_params_arg *arg)
{
	params->cwmin  = __cpu_to_le32(arg->cwmin);
	params->cwmax  = __cpu_to_le32(arg->cwmax);
	params->aifs   = __cpu_to_le32(arg->aifs);
	params->txop   = __cpu_to_le32(arg->txop);
	params->acm    = __cpu_to_le32(arg->acm);
	params->no_ack = __cpu_to_le32(arg->no_ack);
}

int ath10k_wmi_pdev_set_wmm_params(struct ath10k *ar,
				   const struct wmi_pdev_set_wmm_params_arg *arg)
{
	struct wmi_pdev_set_wmm_params *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_set_wmm_params *)skb->data;
	ath10k_wmi_pdev_set_wmm_param(&cmd->ac_be, &arg->ac_be);
	ath10k_wmi_pdev_set_wmm_param(&cmd->ac_bk, &arg->ac_bk);
	ath10k_wmi_pdev_set_wmm_param(&cmd->ac_vi, &arg->ac_vi);
	ath10k_wmi_pdev_set_wmm_param(&cmd->ac_vo, &arg->ac_vo);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi pdev set wmm params\n");
	return ath10k_wmi_cmd_send(ar, skb,
				   ar->wmi.cmd->pdev_set_wmm_params_cmdid);
}

int ath10k_wmi_request_stats(struct ath10k *ar, enum wmi_stats_id stats_id)
{
	struct wmi_request_stats_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_request_stats_cmd *)skb->data;
	cmd->stats_id = __cpu_to_le32(stats_id);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi request stats %d\n", (int)stats_id);
	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->request_stats_cmdid);
}

int ath10k_wmi_force_fw_hang(struct ath10k *ar,
			     enum wmi_force_fw_hang_type type, u32 delay_ms)
{
	struct wmi_force_fw_hang_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_force_fw_hang_cmd *)skb->data;
	cmd->type = __cpu_to_le32(type);
	cmd->delay_ms = __cpu_to_le32(delay_ms);

	ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi force fw hang %d delay %d\n",
		   type, delay_ms);
	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->force_fw_hang_cmdid);
}

int ath10k_wmi_dbglog_cfg(struct ath10k *ar, u32 module_enable)
{
	struct wmi_dbglog_cfg_cmd *cmd;
	struct sk_buff *skb;
	u32 cfg;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_dbglog_cfg_cmd *)skb->data;

	if (module_enable) {
		cfg = SM(ATH10K_DBGLOG_LEVEL_VERBOSE,
			 ATH10K_DBGLOG_CFG_LOG_LVL);
	} else {
		/* set back defaults, all modules with WARN level */
		cfg = SM(ATH10K_DBGLOG_LEVEL_WARN,
			 ATH10K_DBGLOG_CFG_LOG_LVL);
		module_enable = ~0;
	}

	cmd->module_enable = __cpu_to_le32(module_enable);
	cmd->module_valid = __cpu_to_le32(~0);
	cmd->config_enable = __cpu_to_le32(cfg);
	cmd->config_valid = __cpu_to_le32(ATH10K_DBGLOG_CFG_LOG_LVL_MASK);

	ath10k_dbg(ar, ATH10K_DBG_WMI,
		   "wmi dbglog cfg modules %08x %08x config %08x %08x\n",
		   __le32_to_cpu(cmd->module_enable),
		   __le32_to_cpu(cmd->module_valid),
		   __le32_to_cpu(cmd->config_enable),
		   __le32_to_cpu(cmd->config_valid));

	return ath10k_wmi_cmd_send(ar, skb, ar->wmi.cmd->dbglog_cfg_cmdid);
}

int ath10k_wmi_attach(struct ath10k *ar)
{
	if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features)) {
		if (test_bit(ATH10K_FW_FEATURE_WMI_10_2, ar->fw_features))
			ar->wmi.cmd = &wmi_10_2_cmd_map;
		else
			ar->wmi.cmd = &wmi_10x_cmd_map;

		ar->wmi.vdev_param = &wmi_10x_vdev_param_map;
		ar->wmi.pdev_param = &wmi_10x_pdev_param_map;
	} else {
		ar->wmi.cmd = &wmi_cmd_map;
		ar->wmi.vdev_param = &wmi_vdev_param_map;
		ar->wmi.pdev_param = &wmi_pdev_param_map;
	}

	init_completion(&ar->wmi.service_ready);
	init_completion(&ar->wmi.unified_ready);
	init_waitqueue_head(&ar->wmi.tx_credits_wq);

	return 0;
}

void ath10k_wmi_detach(struct ath10k *ar)
{
	int i;

	/* free the host memory chunks requested by firmware */
	for (i = 0; i < ar->wmi.num_mem_chunks; i++) {
		dma_free_coherent(ar->dev,
				  ar->wmi.mem_chunks[i].len,
				  ar->wmi.mem_chunks[i].vaddr,
				  ar->wmi.mem_chunks[i].paddr);
	}

	ar->wmi.num_mem_chunks = 0;
}
