//------------------------------------------------------------------------------
// <copyright file="wmi_api.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// This file contains the definitions for the Wireless Module Interface (WMI).
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _WMI_API_H_
#define _WMI_API_H_

#ifdef __cplusplus
extern "C" {
#endif

    /* WMI converts a dix frame with an ethernet payload (up to 1500 bytes)
     * to an 802.3 frame (adds SNAP header) and adds on a WMI data header */
#define WMI_MAX_TX_DATA_FRAME_LENGTH (1500 + sizeof(WMI_DATA_HDR) + sizeof(ATH_MAC_HDR) + sizeof(ATH_LLC_SNAP_HDR))

    /* A normal WMI data frame */
#define WMI_MAX_NORMAL_RX_DATA_FRAME_LENGTH (1500 + sizeof(WMI_DATA_HDR) + sizeof(ATH_MAC_HDR) + sizeof(ATH_LLC_SNAP_HDR))

    /* An AMSDU frame */ /* The MAX AMSDU length of AR6003 is 3839 */
#define WMI_MAX_AMSDU_RX_DATA_FRAME_LENGTH  (3840 + sizeof(WMI_DATA_HDR) + sizeof(ATH_MAC_HDR) + sizeof(ATH_LLC_SNAP_HDR))

/*
 * IP QoS Field definitions according to 802.1p
 */
#define BEST_EFFORT_PRI         0
#define BACKGROUND_PRI          1
#define EXCELLENT_EFFORT_PRI    3
#define CONTROLLED_LOAD_PRI     4
#define VIDEO_PRI               5
#define VOICE_PRI               6
#define NETWORK_CONTROL_PRI     7
#define MAX_NUM_PRI             8

#define UNDEFINED_PRI           (0xff)

#define WMI_IMPLICIT_PSTREAM_INACTIVITY_INT 5000 /* 5 seconds */

#define A_ROUND_UP(x, y)  ((((x) + ((y) - 1)) / (y)) * (y))

typedef enum {
    ATHEROS_COMPLIANCE = 0x1,
}TSPEC_PARAM_COMPLIANCE;

struct wmi_t;

void *wmi_init(void *devt, int devid);

void wmi_qos_state_init(struct wmi_t *wmip);
void wmi_shutdown(struct wmi_t *wmip);
HTC_ENDPOINT_ID wmi_get_control_ep(struct wmi_t * wmip);
void wmi_set_control_ep(struct wmi_t * wmip, HTC_ENDPOINT_ID eid);
A_UINT16  wmi_get_mapped_qos_queue(struct wmi_t *, A_UINT8);
A_STATUS wmi_dix_2_dot3(struct wmi_t *wmip, void *osbuf);
A_STATUS wmi_data_hdr_add(struct wmi_t *wmip, void *osbuf, A_UINT8 msgType, A_UINT32 flags , WMI_DATA_HDR_DATA_TYPE data_type,A_UINT8 metaVersion, void *pTxMetaS);
A_STATUS wmi_dot3_2_dix(void *osbuf);

A_STATUS wmi_dot11_hdr_remove (struct wmi_t *wmip, void *osbuf);
A_STATUS wmi_dot11_hdr_add(struct wmi_t *wmip, void *osbuf, NETWORK_TYPE mode);

A_STATUS wmi_data_hdr_remove(struct wmi_t *wmip, void *osbuf);
A_STATUS wmi_syncpoint(struct wmi_t *wmip);
A_STATUS wmi_syncpoint_reset(struct wmi_t *wmip);
A_UINT8 wmi_implicit_create_pstream(struct wmi_t *wmip, void *osbuf, A_UINT32 layer2Priority, A_BOOL wmmEnabled);

A_UINT8 wmi_determine_userPriority (A_UINT8 *pkt, A_UINT32 layer2Pri);

A_STATUS wmi_control_rx(struct wmi_t *wmip, void *osbuf);
void wmi_iterate_nodes(struct wmi_t *wmip, wlan_node_iter_func *f, void *arg);
void wmi_free_allnodes(struct wmi_t *wmip);
bss_t *wmi_find_node(struct wmi_t *wmip, const A_UINT8 *macaddr);
void wmi_free_node(struct wmi_t *wmip, const A_UINT8 *macaddr);


typedef enum {
    NO_SYNC_WMIFLAG = 0,
    SYNC_BEFORE_WMIFLAG,            /* transmit all queued data before cmd */
    SYNC_AFTER_WMIFLAG,             /* any new data waits until cmd execs */
    SYNC_BOTH_WMIFLAG,
    END_WMIFLAG                     /* end marker */
} WMI_SYNC_FLAG;

A_STATUS wmi_cmd_send(struct wmi_t *wmip, void *osbuf, WMI_COMMAND_ID cmdId,
                      WMI_SYNC_FLAG flag);

A_STATUS wmi_connect_cmd(struct wmi_t *wmip,
                         NETWORK_TYPE netType,
                         DOT11_AUTH_MODE dot11AuthMode,
                         AUTH_MODE authMode,
                         CRYPTO_TYPE pairwiseCrypto,
                         A_UINT8 pairwiseCryptoLen,
                         CRYPTO_TYPE groupCrypto,
                         A_UINT8 groupCryptoLen,
                         int ssidLength,
                         A_UCHAR *ssid,
                         A_UINT8 *bssid,
                         A_UINT16 channel,
                         A_UINT32 ctrl_flags);

A_STATUS wmi_set_div_param_cmd(struct wmi_t *wmip, A_UINT32 divIdleTime,
                A_UINT8   antRssiThresh, A_UINT8 divEnable, A_UINT16 active_treshold_rate);


A_STATUS wmi_reconnect_cmd(struct wmi_t *wmip,
                           A_UINT8 *bssid,
                           A_UINT16 channel);
A_STATUS wmi_disconnect_cmd(struct wmi_t *wmip);
A_STATUS wmi_getrev_cmd(struct wmi_t *wmip);
A_STATUS wmi_startscan_cmd(struct wmi_t *wmip, WMI_SCAN_TYPE scanType,
                           A_BOOL forceFgScan, A_BOOL isLegacy,
                           A_UINT32 homeDwellTime, A_UINT32 forceScanInterval,
                           A_INT8 numChan, A_UINT16 *channelList);
A_STATUS wmi_scanparams_cmd(struct wmi_t *wmip, A_UINT16 fg_start_sec,
                            A_UINT16 fg_end_sec, A_UINT16 bg_sec,
                            A_UINT16 minact_chdw_msec,
                            A_UINT16 maxact_chdw_msec, A_UINT16 pas_chdw_msec,
                            A_UINT8 shScanRatio, A_UINT8 scanCtrlFlags,
                            A_UINT32 max_dfsch_act_time,
                            A_UINT16 maxact_scan_per_ssid);
A_STATUS wmi_bssfilter_cmd(struct wmi_t *wmip, A_UINT8 filter, A_UINT32 ieMask);
A_STATUS wmi_probedSsid_cmd(struct wmi_t *wmip, A_UINT8 index, A_UINT8 flag,
                            A_UINT8 ssidLength, A_UCHAR *ssid);
A_STATUS wmi_listeninterval_cmd(struct wmi_t *wmip, A_UINT16 listenInterval, A_UINT16 listenBeacons);
A_STATUS wmi_bmisstime_cmd(struct wmi_t *wmip, A_UINT16 bmisstime, A_UINT16 bmissbeacons);
A_STATUS wmi_associnfo_cmd(struct wmi_t *wmip, A_UINT8 ieType,
                           A_UINT8 ieLen, A_UINT8 *ieInfo);
A_STATUS wmi_powermode_cmd(struct wmi_t *wmip, A_UINT8 powerMode);
A_STATUS wmi_ibsspmcaps_cmd(struct wmi_t *wmip, A_UINT8 pmEnable, A_UINT8 ttl,
                            A_UINT16 atim_windows, A_UINT16 timeout_value);
A_STATUS wmi_apps_cmd(struct wmi_t *wmip, A_UINT8 psType, A_UINT32 idle_time,
                   A_UINT32 ps_period, A_UINT8 sleep_period);
A_STATUS wmi_pmparams_cmd(struct wmi_t *wmip, A_UINT16 idlePeriod,
                           A_UINT16 psPollNum, A_UINT16 dtimPolicy,
                           A_UINT16 wakup_tx_policy, A_UINT16 num_tx_to_wakeup,
                           A_UINT16 ps_fail_event_policy);
A_STATUS wmi_disctimeout_cmd(struct wmi_t *wmip, A_UINT8 timeout);
A_STATUS wmi_sync_cmd(struct wmi_t *wmip, A_UINT8 syncNumber);
A_STATUS wmi_create_pstream_cmd(struct wmi_t *wmip, WMI_CREATE_PSTREAM_CMD *pstream);
A_STATUS wmi_delete_pstream_cmd(struct wmi_t *wmip, A_UINT8 trafficClass, A_UINT8 streamID);
A_STATUS wmi_set_framerate_cmd(struct wmi_t *wmip, A_UINT8 bEnable, A_UINT8 type, A_UINT8 subType, A_UINT16 rateMask);
A_STATUS wmi_set_bitrate_cmd(struct wmi_t *wmip, A_INT32 dataRate, A_INT32 mgmtRate, A_INT32 ctlRate);
A_STATUS wmi_get_bitrate_cmd(struct wmi_t *wmip);
A_INT8   wmi_validate_bitrate(struct wmi_t *wmip, A_INT32 rate, A_INT8 *rate_idx);
A_STATUS wmi_get_regDomain_cmd(struct wmi_t *wmip);
A_STATUS wmi_get_channelList_cmd(struct wmi_t *wmip);
A_STATUS wmi_set_channelParams_cmd(struct wmi_t *wmip, A_UINT8 scanParam,
                                   WMI_PHY_MODE mode, A_INT8 numChan,
                                   A_UINT16 *channelList);

A_STATUS wmi_set_snr_threshold_params(struct wmi_t *wmip,
                                       WMI_SNR_THRESHOLD_PARAMS_CMD *snrCmd);
A_STATUS wmi_set_rssi_threshold_params(struct wmi_t *wmip,
                                        WMI_RSSI_THRESHOLD_PARAMS_CMD *rssiCmd);
A_STATUS wmi_clr_rssi_snr(struct wmi_t *wmip);
A_STATUS wmi_set_lq_threshold_params(struct wmi_t *wmip,
                                      WMI_LQ_THRESHOLD_PARAMS_CMD *lqCmd);
A_STATUS wmi_set_rts_cmd(struct wmi_t *wmip, A_UINT16 threshold);
A_STATUS wmi_set_lpreamble_cmd(struct wmi_t *wmip, A_UINT8 status, A_UINT8 preamblePolicy);

A_STATUS wmi_set_error_report_bitmask(struct wmi_t *wmip, A_UINT32 bitmask);

A_STATUS wmi_get_challenge_resp_cmd(struct wmi_t *wmip, A_UINT32 cookie,
                                    A_UINT32 source);

A_STATUS wmi_config_debug_module_cmd(struct wmi_t *wmip, A_UINT16 mmask,
                                     A_UINT16 tsr, A_BOOL rep, A_UINT16 size,
                                     A_UINT32 valid);

A_STATUS wmi_get_stats_cmd(struct wmi_t *wmip);

A_STATUS wmi_addKey_cmd(struct wmi_t *wmip, A_UINT8 keyIndex,
                        CRYPTO_TYPE keyType, A_UINT8 keyUsage,
                        A_UINT8 keyLength,A_UINT8 *keyRSC,
                        A_UINT8 *keyMaterial, A_UINT8 key_op_ctrl, A_UINT8 *mac,
                        WMI_SYNC_FLAG sync_flag);
A_STATUS wmi_add_krk_cmd(struct wmi_t *wmip, A_UINT8 *krk);
A_STATUS wmi_delete_krk_cmd(struct wmi_t *wmip);
A_STATUS wmi_deleteKey_cmd(struct wmi_t *wmip, A_UINT8 keyIndex);
A_STATUS wmi_set_akmp_params_cmd(struct wmi_t *wmip,
                                 WMI_SET_AKMP_PARAMS_CMD *akmpParams);
A_STATUS wmi_get_pmkid_list_cmd(struct wmi_t *wmip);
A_STATUS wmi_set_pmkid_list_cmd(struct wmi_t *wmip,
                                WMI_SET_PMKID_LIST_CMD *pmkInfo);
A_STATUS wmi_abort_scan_cmd(struct wmi_t *wmip);
A_STATUS wmi_set_txPwr_cmd(struct wmi_t *wmip, A_UINT8 dbM);
A_STATUS wmi_get_txPwr_cmd(struct wmi_t *wmip);
A_STATUS wmi_addBadAp_cmd(struct wmi_t *wmip, A_UINT8 apIndex, A_UINT8 *bssid);
A_STATUS wmi_deleteBadAp_cmd(struct wmi_t *wmip, A_UINT8 apIndex);
A_STATUS wmi_set_tkip_countermeasures_cmd(struct wmi_t *wmip, A_BOOL en);
A_STATUS wmi_setPmkid_cmd(struct wmi_t *wmip, A_UINT8 *bssid, A_UINT8 *pmkId,
                          A_BOOL set);
A_STATUS wmi_set_access_params_cmd(struct wmi_t *wmip, A_UINT8 ac, A_UINT16 txop,
                                   A_UINT8 eCWmin, A_UINT8 eCWmax,
                                   A_UINT8 aifsn);
A_STATUS wmi_set_retry_limits_cmd(struct wmi_t *wmip, A_UINT8 frameType,
                                  A_UINT8 trafficClass, A_UINT8 maxRetries,
                                  A_UINT8 enableNotify);

void wmi_get_current_bssid(struct wmi_t *wmip, A_UINT8 *bssid);

A_STATUS wmi_get_roam_tbl_cmd(struct wmi_t *wmip);
A_STATUS wmi_get_roam_data_cmd(struct wmi_t *wmip, A_UINT8 roamDataType);
A_STATUS wmi_set_roam_ctrl_cmd(struct wmi_t *wmip, WMI_SET_ROAM_CTRL_CMD *p,
                               A_UINT8 size);
A_STATUS wmi_set_powersave_timers_cmd(struct wmi_t *wmip,
                            WMI_POWERSAVE_TIMERS_POLICY_CMD *pCmd,
                            A_UINT8 size);

A_STATUS wmi_set_opt_mode_cmd(struct wmi_t *wmip, A_UINT8 optMode);
A_STATUS wmi_opt_tx_frame_cmd(struct wmi_t *wmip,
                              A_UINT8 frmType,
                              A_UINT8 *dstMacAddr,
                              A_UINT8 *bssid,
                              A_UINT16 optIEDataLen,
                              A_UINT8 *optIEData);

A_STATUS wmi_set_adhoc_bconIntvl_cmd(struct wmi_t *wmip, A_UINT16 intvl);
A_STATUS wmi_set_voice_pkt_size_cmd(struct wmi_t *wmip, A_UINT16 voicePktSize);
A_STATUS wmi_set_max_sp_len_cmd(struct wmi_t *wmip, A_UINT8 maxSpLen);
A_UINT8  convert_userPriority_to_trafficClass(A_UINT8 userPriority);
A_UINT8 wmi_get_power_mode_cmd(struct wmi_t *wmip);
A_STATUS wmi_verify_tspec_params(WMI_CREATE_PSTREAM_CMD *pCmd, A_BOOL tspecCompliance);

#ifdef CONFIG_HOST_TCMD_SUPPORT
A_STATUS wmi_test_cmd(struct wmi_t *wmip, A_UINT8 *buf, A_UINT32  len);
#endif

A_STATUS wmi_set_bt_status_cmd(struct wmi_t *wmip, A_UINT8 streamType, A_UINT8 status);
A_STATUS wmi_set_bt_params_cmd(struct wmi_t *wmip, WMI_SET_BT_PARAMS_CMD* cmd);

A_STATUS wmi_set_btcoex_fe_ant_cmd(struct wmi_t *wmip, WMI_SET_BTCOEX_FE_ANT_CMD * cmd);

A_STATUS wmi_set_btcoex_colocated_bt_dev_cmd(struct wmi_t *wmip,
                                        WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD * cmd);

A_STATUS wmi_set_btcoex_btinquiry_page_config_cmd(struct wmi_t *wmip,
                                        WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD *cmd);

A_STATUS wmi_set_btcoex_sco_config_cmd(struct wmi_t *wmip,
                                      WMI_SET_BTCOEX_SCO_CONFIG_CMD * cmd);

A_STATUS wmi_set_btcoex_a2dp_config_cmd(struct wmi_t *wmip,
                                         WMI_SET_BTCOEX_A2DP_CONFIG_CMD* cmd);


A_STATUS wmi_set_btcoex_aclcoex_config_cmd(struct wmi_t *wmip, WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD* cmd);

A_STATUS wmi_set_btcoex_debug_cmd(struct wmi_t *wmip, WMI_SET_BTCOEX_DEBUG_CMD * cmd);

A_STATUS wmi_set_btcoex_bt_operating_status_cmd(struct wmi_t * wmip,
                            WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD * cmd);

A_STATUS wmi_get_btcoex_config_cmd(struct wmi_t * wmip, WMI_GET_BTCOEX_CONFIG_CMD * cmd);

A_STATUS wmi_get_btcoex_stats_cmd(struct wmi_t * wmip);

A_STATUS wmi_SGI_cmd(struct wmi_t *wmip, A_UINT32 *sgiMask, A_UINT8 sgiPERThreshold);

/*
 *  This function is used to configure the fix rates mask to the target.
 */
A_STATUS wmi_set_fixrates_cmd(struct wmi_t *wmip, A_UINT32 *fixRatesMask);
A_STATUS wmi_get_ratemask_cmd(struct wmi_t *wmip);

A_STATUS wmi_set_authmode_cmd(struct wmi_t *wmip, A_UINT8 mode);

A_STATUS wmi_set_reassocmode_cmd(struct wmi_t *wmip, A_UINT8 mode);

A_STATUS wmi_set_qos_supp_cmd(struct wmi_t *wmip,A_UINT8 status);
A_STATUS wmi_set_wmm_cmd(struct wmi_t *wmip, WMI_WMM_STATUS status);
A_STATUS wmi_set_wmm_txop(struct wmi_t *wmip, WMI_TXOP_CFG txEnable);
A_STATUS wmi_set_country(struct wmi_t *wmip, A_UCHAR *countryCode);

A_STATUS wmi_get_keepalive_configured(struct wmi_t *wmip);
A_UINT8 wmi_get_keepalive_cmd(struct wmi_t *wmip);
A_STATUS wmi_set_keepalive_cmd(struct wmi_t *wmip, A_UINT8 keepaliveInterval);

A_STATUS wmi_set_appie_cmd(struct wmi_t *wmip, A_UINT8 mgmtFrmType,
                           A_UINT8 ieLen,A_UINT8 *ieInfo);

A_STATUS wmi_set_halparam_cmd(struct wmi_t *wmip, A_UINT8 *cmd, A_UINT16 dataLen);

A_INT32 wmi_get_rate(A_INT8 rateindex);

A_STATUS wmi_set_ip_cmd(struct wmi_t *wmip, WMI_SET_IP_CMD *cmd);

/*Wake on Wireless WMI commands*/
A_STATUS wmi_set_host_sleep_mode_cmd(struct wmi_t *wmip, WMI_SET_HOST_SLEEP_MODE_CMD *cmd);
A_STATUS wmi_set_wow_mode_cmd(struct wmi_t *wmip, WMI_SET_WOW_MODE_CMD *cmd);
A_STATUS wmi_get_wow_list_cmd(struct wmi_t *wmip, WMI_GET_WOW_LIST_CMD *cmd);
A_STATUS wmi_add_wow_pattern_cmd(struct wmi_t *wmip,
                                 WMI_ADD_WOW_PATTERN_CMD *cmd, A_UINT8* pattern, A_UINT8* mask, A_UINT8 pattern_size);
A_STATUS wmi_del_wow_pattern_cmd(struct wmi_t *wmip,
                                 WMI_DEL_WOW_PATTERN_CMD *cmd);
A_STATUS wmi_set_wsc_status_cmd(struct wmi_t *wmip, A_UINT32 status);

A_STATUS
wmi_set_params_cmd(struct wmi_t *wmip, A_UINT32 opcode, A_UINT32 length, A_CHAR* buffer);

A_STATUS
wmi_set_mcast_filter_cmd(struct wmi_t *wmip, A_UINT8 *filter);

A_STATUS
wmi_del_mcast_filter_cmd(struct wmi_t *wmip, A_UINT8 *filter);

A_STATUS
wmi_mcast_filter_cmd(struct wmi_t *wmip, A_UINT8 enable);

bss_t *
wmi_find_Ssidnode (struct wmi_t *wmip, A_UCHAR *pSsid,
                   A_UINT32 ssidLength, A_BOOL bIsWPA2, A_BOOL bMatchSSID);


void
wmi_node_return (struct wmi_t *wmip, bss_t *bss);

void
wmi_node_update_timestamp(struct wmi_t *wmip, bss_t *bss);

void wmi_setup_node(struct wmi_t *wmip, bss_t *bss, const A_UINT8 *bssid);

bss_t *wmi_node_alloc(struct wmi_t *wmip, A_UINT8 len);

void
wmi_set_nodeage(struct wmi_t *wmip, A_UINT32 nodeAge);

#if defined(CONFIG_TARGET_PROFILE_SUPPORT)
A_STATUS wmi_prof_cfg_cmd(struct wmi_t *wmip, A_UINT32 period, A_UINT32 nbins);
A_STATUS wmi_prof_addr_set_cmd(struct wmi_t *wmip, A_UINT32 addr);
A_STATUS wmi_prof_start_cmd(struct wmi_t *wmip);
A_STATUS wmi_prof_stop_cmd(struct wmi_t *wmip);
A_STATUS wmi_prof_count_get_cmd(struct wmi_t *wmip);
#endif /* CONFIG_TARGET_PROFILE_SUPPORT */
#ifdef OS_ROAM_MANAGEMENT
void wmi_scan_indication (struct wmi_t *wmip);
#endif

A_STATUS
wmi_set_target_event_report_cmd(struct wmi_t *wmip, WMI_SET_TARGET_EVENT_REPORT_CMD* cmd);

bss_t   *wmi_rm_current_bss (struct wmi_t *wmip, A_UINT8 *id);
A_STATUS wmi_add_current_bss (struct wmi_t *wmip, A_UINT8 *id, bss_t *bss);


/*
 * AP mode
 */
A_STATUS
wmi_ap_profile_commit(struct wmi_t *wmip, WMI_CONNECT_CMD *p);

A_STATUS
wmi_ap_set_hidden_ssid(struct wmi_t *wmip, A_UINT8 hidden_ssid);

A_STATUS
wmi_ap_set_num_sta(struct wmi_t *wmip, A_UINT8 num_sta);

A_STATUS
wmi_ap_set_dfs(struct wmi_t *wmip, A_UINT8 enable);

A_STATUS
wmi_ap_set_acl_policy(struct wmi_t *wmip, A_UINT8 policy);

A_STATUS
wmi_ap_acl_mac_list(struct wmi_t *wmip, WMI_AP_ACL_MAC_CMD *a);

A_UINT8
acl_add_del_mac(WMI_AP_ACL *a, WMI_AP_ACL_MAC_CMD *acl);

A_STATUS
wmi_ap_set_mlme(struct wmi_t *wmip, A_UINT8 cmd, A_UINT8 *mac, A_UINT16 reason);

A_STATUS
wmi_set_pvb_cmd(struct wmi_t *wmip, A_UINT16 aid, A_BOOL flag);

A_STATUS
wmi_ap_conn_inact_time(struct wmi_t *wmip, A_UINT32 period);

A_STATUS
wmi_ap_bgscan_time(struct wmi_t *wmip, A_UINT32 period, A_UINT32 dwell);

A_STATUS
wmi_ap_set_dtim(struct wmi_t *wmip, A_UINT8 dtim);

A_STATUS
wmi_ap_set_rateset(struct wmi_t *wmip, A_UINT8 rateset);

A_STATUS
wmi_set_ht_cap_cmd(struct wmi_t *wmip, WMI_SET_HT_CAP_CMD *cmd);

A_STATUS
wmi_get_ht_cap_cmd(struct wmi_t *wmip, WMI_SET_HT_CAP_CMD *cmd);

A_STATUS
wmi_set_ht_op_cmd(struct wmi_t *wmip, A_UINT8 sta_chan_width);

A_STATUS
wmi_send_hci_cmd(struct wmi_t *wmip, A_UINT8 *buf, A_UINT16 sz);

A_STATUS
wmi_set_tx_select_rates_cmd(struct wmi_t *wmip, A_UINT32 *pMaskArray);

A_STATUS
wmi_setup_aggr_cmd(struct wmi_t *wmip, A_UINT8 tid);

A_STATUS
wmi_delete_aggr_cmd(struct wmi_t *wmip, A_UINT8 tid, A_BOOL uplink);

A_STATUS
wmi_allow_aggr_cmd(struct wmi_t *wmip, A_UINT16 tx_tidmask, A_UINT16 rx_tidmask);

A_STATUS
wmi_set_rx_frame_format_cmd(struct wmi_t *wmip, A_UINT8 rxMetaVersion, A_BOOL rxDot11Hdr, A_BOOL defragOnHost);

A_STATUS
wmi_set_thin_mode_cmd(struct wmi_t *wmip, A_BOOL bThinMode);

A_STATUS
wmi_set_wlan_conn_precedence_cmd(struct wmi_t *wmip, BT_WLAN_CONN_PRECEDENCE precedence);

A_STATUS
wmi_set_pmk_cmd(struct wmi_t *wmip, A_UINT8 *pmk);

A_STATUS
wmi_set_passphrase_cmd(struct wmi_t *wmip, WMI_SET_PASSPHRASE_CMD *cmd);

A_STATUS
wmi_set_excess_tx_retry_thres_cmd(struct wmi_t *wmip, WMI_SET_EXCESS_TX_RETRY_THRES_CMD *cmd);

A_STATUS
wmi_assoc_req_enable_cmd(struct wmi_t *wmip, A_UINT8 enable);

A_STATUS
wmi_assoc_req_report_cmd (struct wmi_t *wmip, A_UINT8 host_accept, A_UINT8 host_reaspncode, A_UINT8 target_status, A_UINT8 *sta_mac_addr, A_UINT8 rspType);

#ifdef P2P
A_STATUS
wmi_p2p_discover(struct wmi_t *wmip, WMI_P2P_FIND_CMD *find_param);

A_STATUS
wmi_p2p_stop_find(struct wmi_t *wmip);

A_STATUS
wmi_p2p_cancel(struct wmi_t *wmip);

A_STATUS
wmi_p2p_listen(struct wmi_t *wmip, A_UINT32 timeout);

A_STATUS
wmi_p2p_go_neg_start(struct wmi_t *wmip, WMI_P2P_GO_NEG_START_CMD *go_param);

A_STATUS wmi_p2p_sdpd_tx_cmd(struct wmi_t *wmip, WMI_P2P_SDPD_TX_CMD *buf);

A_STATUS wmi_p2p_stop_sdpd(struct wmi_t *wmip);

A_STATUS wmi_p2p_go_neg_rsp_cmd(struct wmi_t *wmip, A_UINT8 status,
     A_UINT8 go_intent, A_UINT32 wps_method, A_UINT16 listen_freq,
     A_UINT8 *wpsbuf, A_UINT32 wpslen, A_UINT8 *p2pbuf, A_UINT32 p2plen,
     A_UINT8 dialog_token, A_UINT8 persistent_grp);

A_STATUS
wmi_p2p_set_config(struct wmi_t *wmip, WMI_P2P_SET_CONFIG_CMD *config);

A_STATUS
wmi_wps_set_config(struct wmi_t *wmip, WMI_WPS_SET_CONFIG_CMD *wps_config);

A_STATUS wmi_p2p_grp_init_cmd(struct wmi_t *wmip, WMI_P2P_GRP_INIT_CMD *buf);

A_STATUS wmi_p2p_grp_done_cmd(struct wmi_t *wmip,
                                 WMI_P2P_GRP_FORMATION_DONE_CMD *buf);

A_STATUS wmi_p2p_invite_cmd(struct wmi_t *wmip, WMI_P2P_INVITE_CMD *buf);

A_STATUS wmi_p2p_invite_req_rsp_cmd(struct wmi_t *wmip, A_UINT8 status,
        A_INT8 is_go, A_UINT8 *grp_bssid, A_UINT8 *p2pbuf,
            A_UINT8 p2plen, A_UINT8 dialog_token);

A_STATUS wmi_p2p_prov_disc_cmd(struct wmi_t *wmip,
                        WMI_P2P_PROV_DISC_REQ_CMD *buf);

A_STATUS wmi_p2p_set_cmd(struct wmi_t *wmip, WMI_P2P_SET_CMD *buf);

#endif /* P2P */

A_UINT16
wmi_ieee2freq (int chan);

A_UINT32
wmi_freq2ieee (A_UINT16 freq);

bss_t *
wmi_find_matching_Ssidnode (struct wmi_t *wmip, A_UCHAR *pSsid,
                   A_UINT32 ssidLength,
                   A_UINT32 dot11AuthMode, A_UINT32 authMode,
                   A_UINT32 pairwiseCryptoType, A_UINT32 grpwiseCryptoTyp);

A_STATUS
wmi_radarDetected_cmd(struct wmi_t *wmip, A_INT16 chan_index, A_INT8 bang_radar);

A_STATUS
wmi_set_dfs_maxpulsedur_cmd(struct wmi_t *wmip, A_UINT32 value);

A_STATUS
wmi_set_dfs_minrssithresh_cmd(struct wmi_t *wmip, A_INT32 rssi);

A_STATUS
wmi_beacon2bssnode (struct wmi_t *wmip, A_UINT8 *datap, int len, A_UINT8 *bssid, A_UINT16 channel);

A_STATUS
wmi_set_tx_mac_rules_cmd (struct wmi_t *wmip, A_UINT32 rules);

A_STATUS
wmi_set_promiscuous_mode_cmd (struct wmi_t *wmip, A_BOOL bMode);

//WAC
A_STATUS wmi_wac_enable_cmd(struct wmi_t *wmip, WMI_WAC_ENABLE_CMD *param);

A_STATUS
wmi_wac_scan_reply_cmd(struct wmi_t *wmip, WAC_SUBCMD param);

A_STATUS
wmi_wac_ctrl_req_cmd(struct wmi_t *wmip, WMI_WAC_CTRL_REQ_CMD *param);

#ifdef CONFIG_WLAN_RFKILL
A_STATUS 
wmi_get_rfkill_mode_cmd(struct wmi_t *wmip);

A_STATUS 
wmi_set_rfkill_mode_cmd(struct wmi_t *wmip,WMI_RFKILL_MODE_CMD *pCmd);

#endif

A_STATUS
wmi_force_target_assert(struct wmi_t *wmip);

A_STATUS
wmi_ap_set_apsd(struct wmi_t *wmip, A_UINT8 enable);

A_STATUS
wmi_set_apsd_buffered_traffic_cmd(struct wmi_t *wmip, A_UINT16 aid, A_UINT16 bitmap, A_UINT32 flags);


#ifdef __cplusplus
}
#endif

#endif /* _WMI_API_H_ */
