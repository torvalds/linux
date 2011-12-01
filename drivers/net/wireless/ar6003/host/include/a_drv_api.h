//------------------------------------------------------------------------------
// <copyright file="a_drv_api.h" company="Atheros">
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
// Author(s): ="Atheros"
//==============================================================================
#ifndef _A_DRV_API_H_
#define _A_DRV_API_H_

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************/
/****************************************************************************/
/**                                                                        **/
/** WMI related hooks                                                      **/
/**                                                                        **/
/****************************************************************************/
/****************************************************************************/

#include <ar6000_api.h>

#define A_WMI_CHANNELLIST_RX(devt, numChan, chanList)   \
    ar6000_channelList_rx((devt), (numChan), (chanList))

#define A_WMI_SET_NUMDATAENDPTS(devt, num)  \
    ar6000_set_numdataendpts((devt), (num))

#define A_WMI_CONTROL_TX(devt, osbuf, streamID) \
    ar6000_control_tx((devt), (osbuf), (streamID))

#define A_WMI_TARGETSTATS_EVENT(devt, pStats, len)  \
    ar6000_targetStats_event((devt), (pStats), (len))

#define A_WMI_SCANCOMPLETE_EVENT(devt, status)  \
    ar6000_scanComplete_event((devt), (status))

#ifdef CONFIG_HOST_DSET_SUPPORT

#define A_WMI_DSET_DATA_REQ(devt, access_cookie, offset, length, targ_buf, targ_reply_fn, targ_reply_arg)   \
    ar6000_dset_data_req((devt), (access_cookie), (offset), (length), (targ_buf), (targ_reply_fn), (targ_reply_arg))

#define A_WMI_DSET_CLOSE(devt, access_cookie)   \
    ar6000_dset_close((devt), (access_cookie))

#endif

#define A_WMI_DSET_OPEN_REQ(devt, id, targ_handle, targ_reply_fn, targ_reply_arg) \
    ar6000_dset_open_req((devt), (id), (targ_handle), (targ_reply_fn), (targ_reply_arg))

#define A_WMI_CONNECT_EVENT(devt, pEvt) \
    ar6000_connect_event((devt), (pEvt))

#define A_WMI_PSPOLL_EVENT(devt, aid)\
    ar6000_pspoll_event((devt),(aid))

#define A_WMI_DTIMEXPIRY_EVENT(devt)\
    ar6000_dtimexpiry_event((devt))

#ifdef WAPI_ENABLE
#define A_WMI_WAPI_REKEY_EVENT(devt, type, mac)\
    ap_wapi_rekey_event((devt),(type),(mac))
#endif

#ifdef P2P
#define A_WMI_P2PGONEG_EVENT(devt, res, len)\
    p2p_go_neg_event((devt),(res), (len))

#define A_WMI_P2PGONEG_REQ_EVENT(devt, sa, dev_passwd_id)\
    p2p_go_neg_req_event((devt), (sa), (dev_passwd_id))

#define A_WMI_P2P_INVITE_SENT_RESULT_EVENT(devt, res, len)\
    p2p_invite_sent_result_event((devt), (res), (len))

#define A_WMI_P2P_INVITE_RCVD_RESULT_EVENT(devt, res, len)\
    p2p_invite_rcvd_result_event((devt), (res), (len))

#define A_WMI_P2PDEV_EVENT(devt, addr, dev_addr, \
     pri_dev_type, dev_name, dev_name_len, config_methods,\
     dev_capab, grp_capab)\
    ar6000_p2pdev_event((devt), (addr), (dev_addr),\
        (pri_dev_type), (dev_name), (dev_name_len), (config_methods),\
        (dev_capab), (grp_capab))

#define A_WMI_P2P_PROV_DISC_REQ_EVENT(devt, peer, config_methods, dev_addr, \
    pri_dev_type, dev_name, dev_name_len, supp_config_methods,\
        dev_capab, group_capab) \
    ar6000_p2p_prov_disc_req_event((devt), (peer), (config_methods), \
            (dev_addr), (pri_dev_type), (dev_name), (dev_name_len),\
            (supp_config_methods), (dev_capab), (group_capab))

#define A_WMI_P2P_PROV_DISC_RESP_EVENT(devt, peer, config_methods) \
    ar6000_p2p_prov_disc_resp_event((devt), (peer), (config_methods))

#define A_WMI_GET_P2P_CTX(devt) \
     get_p2p_ctx((devt))

#define A_WMI_GET_WMI_CTX(devt) \
     get_wmi_ctx((devt))

#define A_WMI_GET_DEV_NETWORK_SUBTYPE(devt) \
     get_network_subtype((devt))

#define A_WMI_P2P_SD_RX_EVENT(devt, ev) \
    ar6000_p2p_sd_rx_event((devt), (ev))
#endif /* P2P */

#ifdef CONFIG_WLAN_RFKILL
#define A_WMI_RFKILL_STATE_CHANGE_EVENT(devt,radiostate) \
    ar6000_rfkill_state_change_event((devt),(radiostate))

#define A_WMI_RFKILL_GET_MODE_CMD_EVENT(devt,datap,len) \
    ar6000_rfkill_get_mode_cmd_event_rx((devt),(datap));

#endif

#define A_WMI_REGDOMAIN_EVENT(devt, regCode)    \
    ar6000_regDomain_event((devt), (regCode))

#define A_WMI_NEIGHBORREPORT_EVENT(devt, numAps, info)  \
    ar6000_neighborReport_event((devt), (numAps), (info))

#define A_WMI_DISCONNECT_EVENT(devt, reason, bssid, assocRespLen, assocInfo, protocolReasonStatus)  \
    ar6000_disconnect_event((devt), (reason), (bssid), (assocRespLen), (assocInfo), (protocolReasonStatus))

#ifdef ATH_SUPPORT_DFS
    
#define A_WMI_DFS_ATTACH_EVENT(devt, capinfo) \
    ar6000_dfs_attach_event((devt),(capinfo))

#define A_WMI_DFS_INIT_EVENT(devt, capinfo) \
    ar6000_dfs_init_event((devt),(capinfo))

#define A_WMI_DFS_PHYERR_EVENT(devt, info) \
    ar6000_dfs_phyerr_event((devt),(info))

#define A_WMI_DFS_RESET_DELAYLINES_EVENT(devt) \
    ar6000_dfs_reset_delaylines_event((devt))

#define A_WMI_DFS_RESET_RADARQ_EVENT(devt) \
    ar6000_dfs_reset_radarq_event((devt))

#define A_WMI_DFS_RESET_AR_EVENT(devt) \
    ar6000_dfs_reset_ar_event((devt))

#define A_WMI_DFS_RESET_ARQ_EVENT(devt) \
    ar6000_dfs_reset_arq_event((devt))

#define A_WMI_DFS_SET_DUR_MULTIPLIER_EVENT(devt, value) \
    ar6000_dfs_set_dur_multiplier_event((devt), (value))

#define A_WMI_DFS_SET_BANGRADAR_EVENT(devt, value) \
    ar6000_dfs_set_bangradar_event((devt), (value))

#define A_WMI_DFS_SET_DEBUGLEVEL_EVENT(devt, value) \
    ar6000_dfs_set_debuglevel_event((devt), (value))
    
#endif /* ATH_SUPPORT_DFS */ 

#define A_WMI_TKIP_MICERR_EVENT(devt, keyid, ismcast)   \
    ar6000_tkip_micerr_event((devt), (keyid), (ismcast))

#define A_WMI_BITRATE_RX(devt, rateKbps)    \
    ar6000_bitrate_rx((devt), (rateKbps))

#define A_WMI_TXPWR_RX(devt, txPwr) \
    ar6000_txPwr_rx((devt), (txPwr))

#define A_WMI_READY_EVENT(devt, datap, phyCap, sw_ver, abi_ver) \
    ar6000_ready_event((devt), (datap), (phyCap), (sw_ver), (abi_ver))

#define A_WMI_DBGLOG_INIT_DONE(devt) \
    ar6000_dbglog_init_done(devt);

#define A_WMI_RSSI_THRESHOLD_EVENT(devt, newThreshold, rssi)    \
    ar6000_rssiThreshold_event((devt), (newThreshold), (rssi))

#define A_WMI_REPORT_ERROR_EVENT(devt, errorVal)    \
    ar6000_reportError_event((devt), (errorVal))

#define A_WMI_ROAM_TABLE_EVENT(devt, pTbl) \
    ar6000_roam_tbl_event((devt), (pTbl))

#define A_WMI_ROAM_DATA_EVENT(devt, p) \
    ar6000_roam_data_event((devt), (p))

#define A_WMI_WOW_LIST_EVENT(devt, num_filters, wow_filters)    \
    ar6000_wow_list_event((devt), (num_filters), (wow_filters))

#define A_WMI_CAC_EVENT(devt, ac, cac_indication, statusCode, tspecSuggestion)  \
    ar6000_cac_event((devt), (ac), (cac_indication), (statusCode), (tspecSuggestion))

#define A_WMI_CHANNEL_CHANGE_EVENT(devt, oldChannel, newChannel)  \
    ar6000_channel_change_event((devt), (oldChannel), (newChannel))

#define A_WMI_PMKID_LIST_EVENT(devt, num_pmkid, pmkid_list, bssid_list) \
    ar6000_pmkid_list_event((devt), (num_pmkid), (pmkid_list), (bssid_list))

#define A_WMI_PEER_EVENT(devt, eventCode, bssid)    \
    ar6000_peer_event ((devt), (eventCode), (bssid))

#define A_WMI_WACINFO_EVENT(devt, pStats, len)  \
    ar6000_wacinfo_event((devt), (pStats), (len))

#ifdef CONFIG_HOST_GPIO_SUPPORT

#define A_WMI_GPIO_INTR_RX(devt, intr_mask, input_values) \
    ar6000_gpio_intr_rx((devt), (intr_mask), (input_values))

#define A_WMI_GPIO_DATA_RX(devt, reg_id, value) \
    ar6000_gpio_data_rx((devt), (reg_id), (value))

#define A_WMI_GPIO_ACK_RX(devt) \
    ar6000_gpio_ack_rx((devt))

#endif

#ifdef SEND_EVENT_TO_APP

#define A_WMI_SEND_EVENT_TO_APP(ar, eventId, datap, len) \
    ar6000_send_event_to_app((ar), (eventId), (datap), (len))

#define A_WMI_SEND_GENERIC_EVENT_TO_APP(ar, eventId, datap, len) \
    ar6000_send_generic_event_to_app((ar), (eventId), (datap), (len))

#else

#define A_WMI_SEND_EVENT_TO_APP(ar, eventId, datap, len)
#define A_WMI_SEND_GENERIC_EVENT_TO_APP(ar, eventId, datap, len)

#endif

#ifdef CONFIG_HOST_TCMD_SUPPORT
#define A_WMI_TCMD_RX_REPORT_EVENT(devt, results, len) \
    ar6000_tcmd_rx_report_event((devt), (results), (len))
#endif

#define A_WMI_HBCHALLENGERESP_EVENT(devt, cookie, source)    \
    ar6000_hbChallengeResp_event((devt), (cookie), (source))

#define A_WMI_TX_RETRY_ERR_EVENT(devt) \
    ar6000_tx_retry_err_event((devt))

#define A_WMI_SNR_THRESHOLD_EVENT_RX(devt, newThreshold, snr) \
    ar6000_snrThresholdEvent_rx((devt), (newThreshold), (snr))

#define A_WMI_LQ_THRESHOLD_EVENT_RX(devt, range, lqVal) \
    ar6000_lqThresholdEvent_rx((devt), (range), (lqVal))

#define A_WMI_RATEMASK_RX(devt, ratemask) \
    ar6000_ratemask_rx((devt), (ratemask))

#define A_WMI_KEEPALIVE_RX(devt, configured)    \
    ar6000_keepalive_rx((devt), (configured))

#define A_WMI_BSSINFO_EVENT_RX(ar, datp, len)   \
    ar6000_bssInfo_event_rx((ar), (datap), (len))

#define A_WMI_DBGLOG_EVENT(ar, dropped, buffer, length) \
    ar6000_dbglog_event((ar), (dropped), (buffer), (length));

#define A_WMI_STREAM_TX_ACTIVE(devt,trafficClass) \
    ar6000_indicate_tx_activity((devt),(trafficClass), TRUE)

#define A_WMI_STREAM_TX_INACTIVE(devt,trafficClass) \
    ar6000_indicate_tx_activity((devt),(trafficClass), FALSE)
#define A_WMI_Ac2EndpointID(devht, ac)\
    ar6000_ac2_endpoint_id((devht), (ac))

#define A_WMI_AGGR_RECV_ADDBA_REQ_EVT(devt, cmd)\
    ar6000_aggr_rcv_addba_req_evt((devt), (cmd))
#define A_WMI_AGGR_RECV_ADDBA_RESP_EVT(devt, cmd)\
    ar6000_aggr_rcv_addba_resp_evt((devt), (cmd))
#define A_WMI_AGGR_RECV_DELBA_REQ_EVT(devt, cmd)\
    ar6000_aggr_rcv_delba_req_evt((devt), (cmd))
#define A_WMI_HCI_EVENT_EVT(devt, cmd)\
    ar6000_hci_event_rcv_evt((devt), (cmd))

#define A_WMI_Endpoint2Ac(devt, ep) \
    ar6000_endpoint_id2_ac((devt), (ep))

#define A_WMI_BTCOEX_CONFIG_EVENT(devt, evt, len)\
	ar6000_btcoex_config_event((devt), (evt), (len))

#define A_WMI_BTCOEX_STATS_EVENT(devt, datap, len)\
	ar6000_btcoex_stats_event((devt), (datap), (len))

#define A_WMI_PROBERESP_RECV_EVENT(devt, datap, len,  bssid)\
    ar6000_indicate_proberesp((devt), (datap), (len),  (bssid))

#define A_WMI_BEACON_RECV_EVENT(devt, datap, len,  bssid)\
    ar6000_indicate_beacon((devt), (datap), (len),  (bssid))

#define A_WMI_ASSOC_REQ_REPORT_EVENT(devt, status, rspType, datap, len)\
    ar6000_assoc_req_report_event((devt),(status),(rspType),(datap),(len))

#define A_WMI_GET_DEVICE_ADDR(devt, addr) \
    ar6000_get_device_addr((devt), (addr))

/****************************************************************************/
/****************************************************************************/
/**                                                                        **/
/** HTC related hooks                                                      **/
/**                                                                        **/
/****************************************************************************/
/****************************************************************************/

#if defined(CONFIG_TARGET_PROFILE_SUPPORT)
#define A_WMI_PROF_COUNT_RX(addr, count) prof_count_rx((addr), (count))
#endif /* CONFIG_TARGET_PROFILE_SUPPORT */

#ifdef __cplusplus
}
#endif

#endif
