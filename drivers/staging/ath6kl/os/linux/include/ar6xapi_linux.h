//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
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
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef _AR6XAPI_LINUX_H
#define _AR6XAPI_LINUX_H
#ifdef __cplusplus
extern "C" {
#endif

struct ar6_softc;

void ar6000_ready_event(void *devt, A_UINT8 *datap, A_UINT8 phyCap,
                        A_UINT32 sw_ver, A_UINT32 abi_ver);
A_STATUS ar6000_control_tx(void *devt, void *osbuf, HTC_ENDPOINT_ID eid);
void ar6000_connect_event(struct ar6_softc *ar, A_UINT16 channel,
                          A_UINT8 *bssid, A_UINT16 listenInterval,
                          A_UINT16 beaconInterval, NETWORK_TYPE networkType,
                          A_UINT8 beaconIeLen, A_UINT8 assocReqLen,
                          A_UINT8 assocRespLen,A_UINT8 *assocInfo);
void ar6000_disconnect_event(struct ar6_softc *ar, A_UINT8 reason,
                             A_UINT8 *bssid, A_UINT8 assocRespLen,
                             A_UINT8 *assocInfo, A_UINT16 protocolReasonStatus);
void ar6000_tkip_micerr_event(struct ar6_softc *ar, A_UINT8 keyid,
                              A_BOOL ismcast);
void ar6000_bitrate_rx(void *devt, A_INT32 rateKbps);
void ar6000_channelList_rx(void *devt, A_INT8 numChan, A_UINT16 *chanList);
void ar6000_regDomain_event(struct ar6_softc *ar, A_UINT32 regCode);
void ar6000_txPwr_rx(void *devt, A_UINT8 txPwr);
void ar6000_keepalive_rx(void *devt, A_UINT8 configured);
void ar6000_neighborReport_event(struct ar6_softc *ar, int numAps,
                                 WMI_NEIGHBOR_INFO *info);
void ar6000_set_numdataendpts(struct ar6_softc *ar, A_UINT32 num);
void ar6000_scanComplete_event(struct ar6_softc *ar, A_STATUS status);
void ar6000_targetStats_event(struct ar6_softc *ar,  A_UINT8 *ptr, A_UINT32 len);
void ar6000_rssiThreshold_event(struct ar6_softc *ar,
                                WMI_RSSI_THRESHOLD_VAL newThreshold,
                                A_INT16 rssi);
void ar6000_reportError_event(struct ar6_softc *, WMI_TARGET_ERROR_VAL errorVal);
void ar6000_cac_event(struct ar6_softc *ar, A_UINT8 ac, A_UINT8 cac_indication,
                                A_UINT8 statusCode, A_UINT8 *tspecSuggestion);
void ar6000_channel_change_event(struct ar6_softc *ar, A_UINT16 oldChannel, A_UINT16 newChannel);
void ar6000_hbChallengeResp_event(struct ar6_softc *, A_UINT32 cookie, A_UINT32 source);
void
ar6000_roam_tbl_event(struct ar6_softc *ar, WMI_TARGET_ROAM_TBL *pTbl);

void
ar6000_roam_data_event(struct ar6_softc *ar, WMI_TARGET_ROAM_DATA *p);

void
ar6000_wow_list_event(struct ar6_softc *ar, A_UINT8 num_filters,
                      WMI_GET_WOW_LIST_REPLY *wow_reply);

void ar6000_pmkid_list_event(void *devt, A_UINT8 numPMKID,
                             WMI_PMKID *pmkidList, A_UINT8 *bssidList);

void ar6000_gpio_intr_rx(A_UINT32 intr_mask, A_UINT32 input_values);
void ar6000_gpio_data_rx(A_UINT32 reg_id, A_UINT32 value);
void ar6000_gpio_ack_rx(void);

A_INT32 rssi_compensation_calc_tcmd(A_UINT32 freq, A_INT32 rssi, A_UINT32 totalPkt);
A_INT16 rssi_compensation_calc(struct ar6_softc *ar, A_INT16 rssi);
A_INT16 rssi_compensation_reverse_calc(struct ar6_softc *ar, A_INT16 rssi, A_BOOL Above);

void ar6000_dbglog_init_done(struct ar6_softc *ar);

#ifdef SEND_EVENT_TO_APP
void ar6000_send_event_to_app(struct ar6_softc *ar, A_UINT16 eventId, A_UINT8 *datap, int len);
void ar6000_send_generic_event_to_app(struct ar6_softc *ar, A_UINT16 eventId, A_UINT8 *datap, int len);
#endif

#ifdef CONFIG_HOST_TCMD_SUPPORT
void ar6000_tcmd_rx_report_event(void *devt, A_UINT8 * results, int len);
#endif

void ar6000_tx_retry_err_event(void *devt);

void ar6000_snrThresholdEvent_rx(void *devt,
                                 WMI_SNR_THRESHOLD_VAL newThreshold,
                                 A_UINT8 snr);

void ar6000_lqThresholdEvent_rx(void *devt, WMI_LQ_THRESHOLD_VAL range, A_UINT8 lqVal);


void ar6000_ratemask_rx(void *devt, A_UINT32 ratemask);

A_STATUS ar6000_get_driver_cfg(struct net_device *dev,
                                A_UINT16 cfgParam,
                                void *result);
void ar6000_bssInfo_event_rx(struct ar6_softc *ar, A_UINT8 *data, int len);

void ar6000_dbglog_event(struct ar6_softc *ar, A_UINT32 dropped,
                         A_INT8 *buffer, A_UINT32 length);

int ar6000_dbglog_get_debug_logs(struct ar6_softc *ar);

void ar6000_peer_event(void *devt, A_UINT8 eventCode, A_UINT8 *bssid);

void ar6000_indicate_tx_activity(void *devt, A_UINT8 trafficClass, A_BOOL Active);
HTC_ENDPOINT_ID  ar6000_ac2_endpoint_id ( void * devt, A_UINT8 ac);
A_UINT8 ar6000_endpoint_id2_ac (void * devt, HTC_ENDPOINT_ID ep );

void ar6000_btcoex_config_event(struct ar6_softc *ar,  A_UINT8 *ptr, A_UINT32 len);

void ar6000_btcoex_stats_event(struct ar6_softc *ar,  A_UINT8 *ptr, A_UINT32 len) ;

void ar6000_dset_open_req(void *devt,
                          A_UINT32 id,
                          A_UINT32 targ_handle,
                          A_UINT32 targ_reply_fn,
                          A_UINT32 targ_reply_arg);
void ar6000_dset_close(void *devt, A_UINT32 access_cookie);
void ar6000_dset_data_req(void *devt,
                          A_UINT32 access_cookie,
                          A_UINT32 offset,
                          A_UINT32 length,
                          A_UINT32 targ_buf,
                          A_UINT32 targ_reply_fn,
                          A_UINT32 targ_reply_arg);


#if defined(CONFIG_TARGET_PROFILE_SUPPORT)
void prof_count_rx(unsigned int addr, unsigned int count);
#endif

A_UINT32 ar6000_getnodeAge (void);

A_UINT32 ar6000_getclkfreq (void);

int ar6000_ap_mode_profile_commit(struct ar6_softc *ar);

struct ieee80211req_wpaie;
A_STATUS
ar6000_ap_mode_get_wpa_ie(struct ar6_softc *ar, struct ieee80211req_wpaie *wpaie);

A_STATUS is_iwioctl_allowed(A_UINT8 mode, A_UINT16 cmd);

A_STATUS is_xioctl_allowed(A_UINT8 mode, int cmd);

void ar6000_pspoll_event(struct ar6_softc *ar,A_UINT8 aid);

void ar6000_dtimexpiry_event(struct ar6_softc *ar);

void ar6000_aggr_rcv_addba_req_evt(struct ar6_softc *ar, WMI_ADDBA_REQ_EVENT *cmd);
void ar6000_aggr_rcv_addba_resp_evt(struct ar6_softc *ar, WMI_ADDBA_RESP_EVENT *cmd);
void ar6000_aggr_rcv_delba_req_evt(struct ar6_softc *ar, WMI_DELBA_EVENT *cmd);
void ar6000_hci_event_rcv_evt(struct ar6_softc *ar, WMI_HCI_EVENT *cmd);

#ifdef WAPI_ENABLE
int ap_set_wapi_key(struct ar6_softc *ar, void *ik);
void ap_wapi_rekey_event(struct ar6_softc *ar, A_UINT8 type, A_UINT8 *mac);
#endif

A_STATUS ar6000_connect_to_ap(struct ar6_softc *ar);
A_STATUS ar6000_update_wlan_pwr_state(struct ar6_softc *ar, AR6000_WLAN_STATE state, A_BOOL suspending);
A_STATUS ar6000_set_wlan_state(struct ar6_softc *ar, AR6000_WLAN_STATE state);
A_STATUS ar6000_set_bt_hw_state(struct ar6_softc *ar, A_UINT32 state);

#ifdef CONFIG_PM
A_STATUS ar6000_suspend_ev(void *context);
A_STATUS ar6000_resume_ev(void *context);
A_STATUS ar6000_power_change_ev(void *context, A_UINT32 config);
void ar6000_check_wow_status(struct ar6_softc *ar, struct sk_buff *skb, A_BOOL isEvent);
#endif

void ar6000_pm_init(void);
void ar6000_pm_exit(void);

#ifdef CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT
A_STATUS ar6000_add_ap_interface(struct ar6_softc *ar, char *ifname);
A_STATUS ar6000_remove_ap_interface(struct ar6_softc *ar);
#endif /* CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT */

#ifdef __cplusplus
}
#endif

#endif
