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

void *wmi_init(void *devt);

void wmi_qos_state_init(struct wmi_t *wmip);
void wmi_shutdown(struct wmi_t *wmip);
HTC_ENDPOINT_ID wmi_get_control_ep(struct wmi_t * wmip);
void wmi_set_control_ep(struct wmi_t * wmip, HTC_ENDPOINT_ID eid);
u16 wmi_get_mapped_qos_queue(struct wmi_t *, u8 );
int wmi_dix_2_dot3(struct wmi_t *wmip, void *osbuf);
int wmi_data_hdr_add(struct wmi_t *wmip, void *osbuf, u8 msgType, bool bMoreData, WMI_DATA_HDR_DATA_TYPE data_type,u8 metaVersion, void *pTxMetaS);
int wmi_dot3_2_dix(void *osbuf);

int wmi_dot11_hdr_remove (struct wmi_t *wmip, void *osbuf);
int wmi_dot11_hdr_add(struct wmi_t *wmip, void *osbuf, NETWORK_TYPE mode);

int wmi_data_hdr_remove(struct wmi_t *wmip, void *osbuf);
int wmi_syncpoint(struct wmi_t *wmip);
int wmi_syncpoint_reset(struct wmi_t *wmip);
u8 wmi_implicit_create_pstream(struct wmi_t *wmip, void *osbuf, u32 layer2Priority, bool wmmEnabled);

u8 wmi_determine_userPriority (u8 *pkt, u32 layer2Pri);

int wmi_control_rx(struct wmi_t *wmip, void *osbuf);
void wmi_iterate_nodes(struct wmi_t *wmip, wlan_node_iter_func *f, void *arg);
void wmi_free_allnodes(struct wmi_t *wmip);
bss_t *wmi_find_node(struct wmi_t *wmip, const u8 *macaddr);
void wmi_free_node(struct wmi_t *wmip, const u8 *macaddr);


typedef enum {
    NO_SYNC_WMIFLAG = 0,
    SYNC_BEFORE_WMIFLAG,            /* transmit all queued data before cmd */
    SYNC_AFTER_WMIFLAG,             /* any new data waits until cmd execs */
    SYNC_BOTH_WMIFLAG,
    END_WMIFLAG                     /* end marker */
} WMI_SYNC_FLAG;

int wmi_cmd_send(struct wmi_t *wmip, void *osbuf, WMI_COMMAND_ID cmdId,
                      WMI_SYNC_FLAG flag);

int wmi_connect_cmd(struct wmi_t *wmip,
                         NETWORK_TYPE netType,
                         DOT11_AUTH_MODE dot11AuthMode,
                         AUTH_MODE authMode,
                         CRYPTO_TYPE pairwiseCrypto,
                         u8 pairwiseCryptoLen,
                         CRYPTO_TYPE groupCrypto,
                         u8 groupCryptoLen,
                         int ssidLength,
                         u8 *ssid,
                         u8 *bssid,
                         u16 channel,
                         u32 ctrl_flags);

int wmi_reconnect_cmd(struct wmi_t *wmip,
                           u8 *bssid,
                           u16 channel);
int wmi_disconnect_cmd(struct wmi_t *wmip);
int wmi_getrev_cmd(struct wmi_t *wmip);
int wmi_startscan_cmd(struct wmi_t *wmip, WMI_SCAN_TYPE scanType,
                           u32 forceFgScan, u32 isLegacy,
                           u32 homeDwellTime, u32 forceScanInterval,
                           s8 numChan, u16 *channelList);
int wmi_scanparams_cmd(struct wmi_t *wmip, u16 fg_start_sec,
                            u16 fg_end_sec, u16 bg_sec,
                            u16 minact_chdw_msec,
                            u16 maxact_chdw_msec, u16 pas_chdw_msec,
                            u8 shScanRatio, u8 scanCtrlFlags,
                            u32 max_dfsch_act_time,
                            u16 maxact_scan_per_ssid);
int wmi_bssfilter_cmd(struct wmi_t *wmip, u8 filter, u32 ieMask);
int wmi_probedSsid_cmd(struct wmi_t *wmip, u8 index, u8 flag,
                            u8 ssidLength, u8 *ssid);
int wmi_listeninterval_cmd(struct wmi_t *wmip, u16 listenInterval, u16 listenBeacons);
int wmi_bmisstime_cmd(struct wmi_t *wmip, u16 bmisstime, u16 bmissbeacons);
int wmi_associnfo_cmd(struct wmi_t *wmip, u8 ieType,
                           u8 ieLen, u8 *ieInfo);
int wmi_powermode_cmd(struct wmi_t *wmip, u8 powerMode);
int wmi_ibsspmcaps_cmd(struct wmi_t *wmip, u8 pmEnable, u8 ttl,
                            u16 atim_windows, u16 timeout_value);
int wmi_apps_cmd(struct wmi_t *wmip, u8 psType, u32 idle_time,
                   u32 ps_period, u8 sleep_period);
int wmi_pmparams_cmd(struct wmi_t *wmip, u16 idlePeriod,
                           u16 psPollNum, u16 dtimPolicy,
                           u16 wakup_tx_policy, u16 num_tx_to_wakeup,
                           u16 ps_fail_event_policy);
int wmi_disctimeout_cmd(struct wmi_t *wmip, u8 timeout);
int wmi_sync_cmd(struct wmi_t *wmip, u8 syncNumber);
int wmi_create_pstream_cmd(struct wmi_t *wmip, WMI_CREATE_PSTREAM_CMD *pstream);
int wmi_delete_pstream_cmd(struct wmi_t *wmip, u8 trafficClass, u8 streamID);
int wmi_set_framerate_cmd(struct wmi_t *wmip, u8 bEnable, u8 type, u8 subType, u16 rateMask);
int wmi_set_bitrate_cmd(struct wmi_t *wmip, s32 dataRate, s32 mgmtRate, s32 ctlRate);
int wmi_get_bitrate_cmd(struct wmi_t *wmip);
s8 wmi_validate_bitrate(struct wmi_t *wmip, s32 rate, s8 *rate_idx);
int wmi_get_regDomain_cmd(struct wmi_t *wmip);
int wmi_get_channelList_cmd(struct wmi_t *wmip);
int wmi_set_channelParams_cmd(struct wmi_t *wmip, u8 scanParam,
                                   WMI_PHY_MODE mode, s8 numChan,
                                   u16 *channelList);

int wmi_set_snr_threshold_params(struct wmi_t *wmip,
                                       WMI_SNR_THRESHOLD_PARAMS_CMD *snrCmd);
int wmi_set_rssi_threshold_params(struct wmi_t *wmip,
                                        WMI_RSSI_THRESHOLD_PARAMS_CMD *rssiCmd);
int wmi_clr_rssi_snr(struct wmi_t *wmip);
int wmi_set_lq_threshold_params(struct wmi_t *wmip,
                                      WMI_LQ_THRESHOLD_PARAMS_CMD *lqCmd);
int wmi_set_rts_cmd(struct wmi_t *wmip, u16 threshold);
int wmi_set_lpreamble_cmd(struct wmi_t *wmip, u8 status, u8 preamblePolicy);

int wmi_set_error_report_bitmask(struct wmi_t *wmip, u32 bitmask);

int wmi_get_challenge_resp_cmd(struct wmi_t *wmip, u32 cookie,
                                    u32 source);

int wmi_config_debug_module_cmd(struct wmi_t *wmip, u16 mmask,
                                     u16 tsr, bool rep, u16 size,
                                     u32 valid);

int wmi_get_stats_cmd(struct wmi_t *wmip);

int wmi_addKey_cmd(struct wmi_t *wmip, u8 keyIndex,
                        CRYPTO_TYPE keyType, u8 keyUsage,
                        u8 keyLength,u8 *keyRSC,
                        u8 *keyMaterial, u8 key_op_ctrl, u8 *mac,
                        WMI_SYNC_FLAG sync_flag);
int wmi_add_krk_cmd(struct wmi_t *wmip, u8 *krk);
int wmi_delete_krk_cmd(struct wmi_t *wmip);
int wmi_deleteKey_cmd(struct wmi_t *wmip, u8 keyIndex);
int wmi_set_akmp_params_cmd(struct wmi_t *wmip,
                                 WMI_SET_AKMP_PARAMS_CMD *akmpParams);
int wmi_get_pmkid_list_cmd(struct wmi_t *wmip);
int wmi_set_pmkid_list_cmd(struct wmi_t *wmip,
                                WMI_SET_PMKID_LIST_CMD *pmkInfo);
int wmi_abort_scan_cmd(struct wmi_t *wmip);
int wmi_set_txPwr_cmd(struct wmi_t *wmip, u8 dbM);
int wmi_get_txPwr_cmd(struct wmi_t *wmip);
int wmi_addBadAp_cmd(struct wmi_t *wmip, u8 apIndex, u8 *bssid);
int wmi_deleteBadAp_cmd(struct wmi_t *wmip, u8 apIndex);
int wmi_set_tkip_countermeasures_cmd(struct wmi_t *wmip, bool en);
int wmi_setPmkid_cmd(struct wmi_t *wmip, u8 *bssid, u8 *pmkId,
                          bool set);
int wmi_set_access_params_cmd(struct wmi_t *wmip, u8 ac, u16 txop,
                                   u8 eCWmin, u8 eCWmax,
                                   u8 aifsn);
int wmi_set_retry_limits_cmd(struct wmi_t *wmip, u8 frameType,
                                  u8 trafficClass, u8 maxRetries,
                                  u8 enableNotify);

void wmi_get_current_bssid(struct wmi_t *wmip, u8 *bssid);

int wmi_get_roam_tbl_cmd(struct wmi_t *wmip);
int wmi_get_roam_data_cmd(struct wmi_t *wmip, u8 roamDataType);
int wmi_set_roam_ctrl_cmd(struct wmi_t *wmip, WMI_SET_ROAM_CTRL_CMD *p,
                               u8 size);
int wmi_set_powersave_timers_cmd(struct wmi_t *wmip,
                            WMI_POWERSAVE_TIMERS_POLICY_CMD *pCmd,
                            u8 size);

int wmi_set_opt_mode_cmd(struct wmi_t *wmip, u8 optMode);
int wmi_opt_tx_frame_cmd(struct wmi_t *wmip,
                              u8 frmType,
                              u8 *dstMacAddr,
                              u8 *bssid,
                              u16 optIEDataLen,
                              u8 *optIEData);

int wmi_set_adhoc_bconIntvl_cmd(struct wmi_t *wmip, u16 intvl);
int wmi_set_voice_pkt_size_cmd(struct wmi_t *wmip, u16 voicePktSize);
int wmi_set_max_sp_len_cmd(struct wmi_t *wmip, u8 maxSpLen);
u8 convert_userPriority_to_trafficClass(u8 userPriority);
u8 wmi_get_power_mode_cmd(struct wmi_t *wmip);
int wmi_verify_tspec_params(WMI_CREATE_PSTREAM_CMD *pCmd, int tspecCompliance);

#ifdef CONFIG_HOST_TCMD_SUPPORT
int wmi_test_cmd(struct wmi_t *wmip, u8 *buf, u32 len);
#endif

int wmi_set_bt_status_cmd(struct wmi_t *wmip, u8 streamType, u8 status);
int wmi_set_bt_params_cmd(struct wmi_t *wmip, WMI_SET_BT_PARAMS_CMD* cmd);

int wmi_set_btcoex_fe_ant_cmd(struct wmi_t *wmip, WMI_SET_BTCOEX_FE_ANT_CMD * cmd);

int wmi_set_btcoex_colocated_bt_dev_cmd(struct wmi_t *wmip,
		                                WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD * cmd);

int wmi_set_btcoex_btinquiry_page_config_cmd(struct wmi_t *wmip,
                               			WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD *cmd);

int wmi_set_btcoex_sco_config_cmd(struct wmi_t *wmip,
                   		              WMI_SET_BTCOEX_SCO_CONFIG_CMD * cmd);

int wmi_set_btcoex_a2dp_config_cmd(struct wmi_t *wmip,
		                                 WMI_SET_BTCOEX_A2DP_CONFIG_CMD* cmd);


int wmi_set_btcoex_aclcoex_config_cmd(struct wmi_t *wmip, WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD* cmd);

int wmi_set_btcoex_debug_cmd(struct wmi_t *wmip, WMI_SET_BTCOEX_DEBUG_CMD * cmd);

int wmi_set_btcoex_bt_operating_status_cmd(struct wmi_t * wmip,
							WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD * cmd);

int wmi_get_btcoex_config_cmd(struct wmi_t * wmip, WMI_GET_BTCOEX_CONFIG_CMD * cmd);

int wmi_get_btcoex_stats_cmd(struct wmi_t * wmip);

int wmi_SGI_cmd(struct wmi_t *wmip, u32 sgiMask, u8 sgiPERThreshold);

/*
 *  This function is used to configure the fix rates mask to the target.
 */
int wmi_set_fixrates_cmd(struct wmi_t *wmip, u32 fixRatesMask);
int wmi_get_ratemask_cmd(struct wmi_t *wmip);

int wmi_set_authmode_cmd(struct wmi_t *wmip, u8 mode);

int wmi_set_reassocmode_cmd(struct wmi_t *wmip, u8 mode);

int wmi_set_qos_supp_cmd(struct wmi_t *wmip,u8 status);
int wmi_set_wmm_cmd(struct wmi_t *wmip, WMI_WMM_STATUS status);
int wmi_set_wmm_txop(struct wmi_t *wmip, WMI_TXOP_CFG txEnable);
int wmi_set_country(struct wmi_t *wmip, u8 *countryCode);

int wmi_get_keepalive_configured(struct wmi_t *wmip);
u8 wmi_get_keepalive_cmd(struct wmi_t *wmip);
int wmi_set_keepalive_cmd(struct wmi_t *wmip, u8 keepaliveInterval);

int wmi_set_appie_cmd(struct wmi_t *wmip, u8 mgmtFrmType,
                           u8 ieLen,u8 *ieInfo);

int wmi_set_halparam_cmd(struct wmi_t *wmip, u8 *cmd, u16 dataLen);

s32 wmi_get_rate(s8 rateindex);

int wmi_set_ip_cmd(struct wmi_t *wmip, WMI_SET_IP_CMD *cmd);

/*Wake on Wireless WMI commands*/
int wmi_set_host_sleep_mode_cmd(struct wmi_t *wmip, WMI_SET_HOST_SLEEP_MODE_CMD *cmd);
int wmi_set_wow_mode_cmd(struct wmi_t *wmip, WMI_SET_WOW_MODE_CMD *cmd);
int wmi_get_wow_list_cmd(struct wmi_t *wmip, WMI_GET_WOW_LIST_CMD *cmd);
int wmi_add_wow_pattern_cmd(struct wmi_t *wmip,
                                 WMI_ADD_WOW_PATTERN_CMD *cmd, u8 *pattern, u8 *mask, u8 pattern_size);
int wmi_del_wow_pattern_cmd(struct wmi_t *wmip,
                                 WMI_DEL_WOW_PATTERN_CMD *cmd);
int wmi_set_wsc_status_cmd(struct wmi_t *wmip, u32 status);

int
wmi_set_params_cmd(struct wmi_t *wmip, u32 opcode, u32 length, char *buffer);

int
wmi_set_mcast_filter_cmd(struct wmi_t *wmip, u8 dot1, u8 dot2, u8 dot3, u8 dot4);

int
wmi_del_mcast_filter_cmd(struct wmi_t *wmip, u8 dot1, u8 dot2, u8 dot3, u8 dot4);

int
wmi_mcast_filter_cmd(struct wmi_t *wmip, u8 enable);

bss_t *
wmi_find_Ssidnode (struct wmi_t *wmip, u8 *pSsid,
                   u32 ssidLength, bool bIsWPA2, bool bMatchSSID);


void
wmi_node_return (struct wmi_t *wmip, bss_t *bss);

void
wmi_set_nodeage(struct wmi_t *wmip, u32 nodeAge);

#if defined(CONFIG_TARGET_PROFILE_SUPPORT)
int wmi_prof_cfg_cmd(struct wmi_t *wmip, u32 period, u32 nbins);
int wmi_prof_addr_set_cmd(struct wmi_t *wmip, u32 addr);
int wmi_prof_start_cmd(struct wmi_t *wmip);
int wmi_prof_stop_cmd(struct wmi_t *wmip);
int wmi_prof_count_get_cmd(struct wmi_t *wmip);
#endif /* CONFIG_TARGET_PROFILE_SUPPORT */
#ifdef OS_ROAM_MANAGEMENT
void wmi_scan_indication (struct wmi_t *wmip);
#endif

int
wmi_set_target_event_report_cmd(struct wmi_t *wmip, WMI_SET_TARGET_EVENT_REPORT_CMD* cmd);

bss_t   *wmi_rm_current_bss (struct wmi_t *wmip, u8 *id);
int wmi_add_current_bss (struct wmi_t *wmip, u8 *id, bss_t *bss);


/*
 * AP mode
 */
int
wmi_ap_profile_commit(struct wmi_t *wmip, WMI_CONNECT_CMD *p);

int
wmi_ap_set_hidden_ssid(struct wmi_t *wmip, u8 hidden_ssid);

int
wmi_ap_set_num_sta(struct wmi_t *wmip, u8 num_sta);

int
wmi_ap_set_acl_policy(struct wmi_t *wmip, u8 policy);

int
wmi_ap_acl_mac_list(struct wmi_t *wmip, WMI_AP_ACL_MAC_CMD *a);

u8 acl_add_del_mac(WMI_AP_ACL *a, WMI_AP_ACL_MAC_CMD *acl);

int
wmi_ap_set_mlme(struct wmi_t *wmip, u8 cmd, u8 *mac, u16 reason);

int
wmi_set_pvb_cmd(struct wmi_t *wmip, u16 aid, bool flag);

int
wmi_ap_conn_inact_time(struct wmi_t *wmip, u32 period);

int
wmi_ap_bgscan_time(struct wmi_t *wmip, u32 period, u32 dwell);

int
wmi_ap_set_dtim(struct wmi_t *wmip, u8 dtim);

int
wmi_ap_set_rateset(struct wmi_t *wmip, u8 rateset);

int
wmi_set_ht_cap_cmd(struct wmi_t *wmip, WMI_SET_HT_CAP_CMD *cmd);

int
wmi_set_ht_op_cmd(struct wmi_t *wmip, u8 sta_chan_width);

int
wmi_send_hci_cmd(struct wmi_t *wmip, u8 *buf, u16 sz);

int
wmi_set_tx_select_rates_cmd(struct wmi_t *wmip, u32 *pMaskArray);

int
wmi_setup_aggr_cmd(struct wmi_t *wmip, u8 tid);

int
wmi_delete_aggr_cmd(struct wmi_t *wmip, u8 tid, bool uplink);

int
wmi_allow_aggr_cmd(struct wmi_t *wmip, u16 tx_tidmask, u16 rx_tidmask);

int
wmi_set_rx_frame_format_cmd(struct wmi_t *wmip, u8 rxMetaVersion, bool rxDot11Hdr, bool defragOnHost);

int
wmi_set_thin_mode_cmd(struct wmi_t *wmip, bool bThinMode);

int
wmi_set_wlan_conn_precedence_cmd(struct wmi_t *wmip, BT_WLAN_CONN_PRECEDENCE precedence);

int
wmi_set_pmk_cmd(struct wmi_t *wmip, u8 *pmk);

int
wmi_set_excess_tx_retry_thres_cmd(struct wmi_t *wmip, WMI_SET_EXCESS_TX_RETRY_THRES_CMD *cmd);

u16 wmi_ieee2freq (int chan);

u32 wmi_freq2ieee (u16 freq);

bss_t *
wmi_find_matching_Ssidnode (struct wmi_t *wmip, u8 *pSsid,
                   u32 ssidLength,
                   u32 dot11AuthMode, u32 authMode,
                   u32 pairwiseCryptoType, u32 grpwiseCryptoTyp);

#ifdef __cplusplus
}
#endif

#endif /* _WMI_API_H_ */
