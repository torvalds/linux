// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP Wireless LAN device driver: station event handling
 *
 * Copyright 2011-2024 NXP
 */

#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "cmdevt.h"
#include "wmm.h"
#include "11n.h"

static int
nxpwifi_sta_event_link_lost(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	adapter->dbg.num_event_link_lost++;
	if (priv->media_connected) {
		adapter->priv_link_lost = priv;
		adapter->host_mlme_link_lost = true;
		nxpwifi_queue_wiphy_work(adapter,
					 &adapter->host_mlme_work);
	}

	return 0;
}

static int
nxpwifi_sta_event_link_sensed(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	netif_carrier_on(priv->netdev);
	nxpwifi_wake_up_net_dev_queue(priv->netdev, adapter);

	return 0;
}

static int
nxpwifi_sta_event_deauthenticated(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	if (priv->wps.session_enable) {
		nxpwifi_dbg(adapter, INFO,
			    "info: receive deauth event in wps session\n");
	} else {
		adapter->dbg.num_event_deauth++;
		if (priv->media_connected) {
			priv->last_deauth_reason =
				get_unaligned_le16(priv->adapter->event_body);
			nxpwifi_queue_wiphy_work(priv->adapter,
						 &priv->reset_conn_state_work);
		}
	}

	return 0;
}

static int
nxpwifi_sta_event_disassociated(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	if (priv->wps.session_enable) {
		nxpwifi_dbg(adapter, INFO,
			    "info: receive disassoc event in wps session\n");
	} else {
		adapter->dbg.num_event_disassoc++;
		if (priv->media_connected) {
			priv->last_deauth_reason =
				get_unaligned_le16(priv->adapter->event_body);
			nxpwifi_queue_wiphy_work(priv->adapter,
						 &priv->reset_conn_state_work);
		}
	}

	return 0;
}

static int
nxpwifi_sta_event_ps_awake(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	if (!adapter->pps_uapsd_mode &&
	    priv->port_open &&
	    priv->media_connected && adapter->sleep_period.period) {
		adapter->pps_uapsd_mode = true;
		nxpwifi_dbg(adapter, EVENT,
			    "event: PPS/UAPSD mode activated\n");
	}
	adapter->tx_lock_flag = false;
	if (adapter->pps_uapsd_mode && adapter->gen_null_pkt) {
		if (nxpwifi_check_last_packet_indication(priv)) {
			if (adapter->data_sent) {
				adapter->ps_state = PS_STATE_AWAKE;
				adapter->pm_wakeup_card_req = false;
				adapter->pm_wakeup_fw_try = false;
				timer_delete(&adapter->wakeup_timer);
			} else {
				if (!nxpwifi_send_null_packet
				    (priv,
				     NXPWIFI_TxPD_POWER_MGMT_NULL_PACKET |
				     NXPWIFI_TxPD_POWER_MGMT_LAST_PACKET))
					adapter->ps_state = PS_STATE_SLEEP;
			}

			return 0;
		}
	}

	adapter->ps_state = PS_STATE_AWAKE;
	adapter->pm_wakeup_card_req = false;
	adapter->pm_wakeup_fw_try = false;
	timer_delete(&adapter->wakeup_timer);

	return 0;
}

static int
nxpwifi_sta_event_ps_sleep(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	adapter->ps_state = PS_STATE_PRE_SLEEP;
	nxpwifi_check_ps_cond(adapter);

	return 0;
}

static int
nxpwifi_sta_event_mic_err_multicast(struct nxpwifi_private *priv)
{
	cfg80211_michael_mic_failure(priv->netdev, priv->cfg_bssid,
				     NL80211_KEYTYPE_GROUP,
				     -1, NULL, GFP_KERNEL);

	return 0;
}

static int
nxpwifi_sta_event_mic_err_unicast(struct nxpwifi_private *priv)
{
	cfg80211_michael_mic_failure(priv->netdev, priv->cfg_bssid,
				     NL80211_KEYTYPE_PAIRWISE,
				     -1, NULL, GFP_KERNEL);

	return 0;
}

static int
nxpwifi_sta_event_deep_sleep_awake(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	adapter->if_ops.wakeup_complete(adapter);
	if (adapter->is_deep_sleep)
		adapter->is_deep_sleep = false;

	return 0;
}

static int
nxpwifi_sta_event_wmm_status_change(struct nxpwifi_private *priv)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_WMM_GET_STATUS,
				0, 0, NULL, false);
}

static int
nxpwifi_sta_event_bs_scan_report(struct nxpwifi_private *priv)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_802_11_BG_SCAN_QUERY,
				HOST_ACT_GEN_GET, 0, NULL, false);
}

static int
nxpwifi_sta_event_rssi_low(struct nxpwifi_private *priv)
{
	cfg80211_cqm_rssi_notify(priv->netdev,
				 NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW,
				 0, GFP_KERNEL);
	priv->subsc_evt_rssi_state = RSSI_LOW_RECVD;

	return nxpwifi_send_cmd(priv, HOST_CMD_RSSI_INFO,
				HOST_ACT_GEN_GET, 0, NULL, false);
}

static int
nxpwifi_sta_event_rssi_high(struct nxpwifi_private *priv)
{
	cfg80211_cqm_rssi_notify(priv->netdev,
				 NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
				 0, GFP_KERNEL);
	priv->subsc_evt_rssi_state = RSSI_HIGH_RECVD;

	return nxpwifi_send_cmd(priv, HOST_CMD_RSSI_INFO,
				HOST_ACT_GEN_GET, 0, NULL, false);
}

static int
nxpwifi_sta_event_port_release(struct nxpwifi_private *priv)
{
	priv->port_open = true;

	return 0;
}

static int
nxpwifi_sta_event_addba(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	return nxpwifi_send_cmd(priv, HOST_CMD_11N_ADDBA_RSP,
				HOST_ACT_GEN_SET, 0,
				adapter->event_body, false);
}

static int
nxpwifi_sta_event_delba(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	nxpwifi_11n_delete_ba_stream(priv, adapter->event_body);

	return 0;
}

static int
nxpwifi_sta_event_bs_stream_timeout(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_ds_11n_batimeout *event =
		(struct host_cmd_ds_11n_batimeout *)adapter->event_body;

	nxpwifi_11n_ba_stream_timeout(priv, event);

	return 0;
}

static int
nxpwifi_sta_event_amsdu_aggr_ctrl(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u16 ctrl;

	ctrl = get_unaligned_le16(adapter->event_body);
	adapter->tx_buf_size = min_t(u16, adapter->curr_tx_buf_size, ctrl);

	return 0;
}

static int
nxpwifi_sta_event_hs_act_req(struct nxpwifi_private *priv)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_802_11_HS_CFG_ENH,
				0, 0, NULL, false);
}

static int
nxpwifi_sta_event_channel_switch_ann(struct nxpwifi_private *priv)
{
	struct nxpwifi_bssdescriptor *bss_desc;

	bss_desc = &priv->curr_bss_params.bss_descriptor;
	priv->csa_expire_time = jiffies + msecs_to_jiffies(DFS_CHAN_MOVE_TIME);
	priv->csa_chan = bss_desc->channel;
	return nxpwifi_send_cmd(priv, HOST_CMD_802_11_DEAUTHENTICATE,
				HOST_ACT_GEN_SET, 0,
				bss_desc->mac_address, false);
}

static int
nxpwifi_sta_event_radar_detected(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	return nxpwifi_11h_handle_radar_detected(priv, adapter->event_skb);
}

static int
nxpwifi_sta_event_channel_report_rdy(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	return nxpwifi_11h_handle_chanrpt_ready(priv, adapter->event_skb);
}

static int
nxpwifi_sta_event_tx_data_pause(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	nxpwifi_process_tx_pause_event(priv, adapter->event_skb);

	return 0;
}

static int
nxpwifi_sta_event_ext_scan_report(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	void *buf = adapter->event_skb->data;
	int ret = 0;

	/*
	 * We intend to skip this event during suspend, but handle
	 * it in interface disabled case
	 */
	if (adapter->ext_scan && (!priv->scan_aborting ||
				  !netif_running(priv->netdev)))
		ret = nxpwifi_handle_event_ext_scan_report(priv, buf);

	return ret;
}

static int
nxpwifi_sta_event_rxba_sync(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	nxpwifi_11n_rxba_sync_event(priv, adapter->event_body,
				    adapter->event_skb->len -
				    sizeof(adapter->event_cause));

	return 0;
}

static int
nxpwifi_sta_event_remain_on_chan_expired(struct nxpwifi_private *priv)
{
	if (priv->auth_flag & HOST_MLME_AUTH_PENDING) {
		priv->auth_flag = 0;
		priv->auth_alg = WLAN_AUTH_NONE;
	} else {
		cfg80211_remain_on_channel_expired(&priv->wdev,
						   priv->roc_cfg.cookie,
						   &priv->roc_cfg.chan,
						   GFP_ATOMIC);
	}

	memset(&priv->roc_cfg, 0x00, sizeof(struct nxpwifi_roc_cfg));

	return 0;
}

static int
nxpwifi_sta_event_bg_scan_stopped(struct nxpwifi_private *priv)
{
	cfg80211_sched_scan_stopped(priv->wdev.wiphy, 0);
	if (priv->sched_scanning)
		priv->sched_scanning = false;

	return 0;
}

static int
nxpwifi_sta_event_multi_chan_info(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	nxpwifi_process_multi_chan_event(priv, adapter->event_skb);

	return 0;
}

static int
nxpwifi_sta_event_tx_status_report(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	nxpwifi_parse_tx_status_event(priv, adapter->event_body);

	return 0;
}

static int
nxpwifi_sta_event_bt_coex_wlan_para_change(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	if (!adapter->ignore_btcoex_events)
		nxpwifi_bt_coex_wlan_param_update_event(priv,
							adapter->event_skb);

	return 0;
}

static int
nxpwifi_sta_event_vdll_ind(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	return nxpwifi_process_vdll_event(priv, adapter->event_skb);
}

static const struct nxpwifi_evt_entry evt_table_sta[] = {
	{.event_cause = EVENT_LINK_LOST,
	.event_handler = nxpwifi_sta_event_link_lost},
	{.event_cause = EVENT_LINK_SENSED,
	.event_handler = nxpwifi_sta_event_link_sensed},
	{.event_cause = EVENT_DEAUTHENTICATED,
	.event_handler = nxpwifi_sta_event_deauthenticated},
	{.event_cause = EVENT_DISASSOCIATED,
	.event_handler = nxpwifi_sta_event_disassociated},
	{.event_cause = EVENT_PS_AWAKE,
	.event_handler = nxpwifi_sta_event_ps_awake},
	{.event_cause = EVENT_PS_SLEEP,
	.event_handler = nxpwifi_sta_event_ps_sleep},
	{.event_cause = EVENT_MIC_ERR_MULTICAST,
	.event_handler = nxpwifi_sta_event_mic_err_multicast},
	{.event_cause = EVENT_MIC_ERR_UNICAST,
	.event_handler = nxpwifi_sta_event_mic_err_unicast},
	{.event_cause = EVENT_DEEP_SLEEP_AWAKE,
	.event_handler = nxpwifi_sta_event_deep_sleep_awake},
	{.event_cause = EVENT_WMM_STATUS_CHANGE,
	.event_handler = nxpwifi_sta_event_wmm_status_change},
	{.event_cause = EVENT_BG_SCAN_REPORT,
	.event_handler = nxpwifi_sta_event_bs_scan_report},
	{.event_cause = EVENT_RSSI_LOW,
	.event_handler = nxpwifi_sta_event_rssi_low},
	{.event_cause = EVENT_RSSI_HIGH,
	.event_handler = nxpwifi_sta_event_rssi_high},
	{.event_cause = EVENT_PORT_RELEASE,
	.event_handler = nxpwifi_sta_event_port_release},
	{.event_cause = EVENT_ADDBA,
	.event_handler = nxpwifi_sta_event_addba},
	{.event_cause = EVENT_DELBA,
	.event_handler = nxpwifi_sta_event_delba},
	{.event_cause = EVENT_BA_STREAM_TIEMOUT,
	.event_handler = nxpwifi_sta_event_bs_stream_timeout},
	{.event_cause = EVENT_AMSDU_AGGR_CTRL,
	.event_handler = nxpwifi_sta_event_amsdu_aggr_ctrl},
	{.event_cause = EVENT_HS_ACT_REQ,
	.event_handler = nxpwifi_sta_event_hs_act_req},
	{.event_cause = EVENT_CHANNEL_SWITCH_ANN,
	.event_handler = nxpwifi_sta_event_channel_switch_ann},
	{.event_cause = EVENT_RADAR_DETECTED,
	.event_handler = nxpwifi_sta_event_radar_detected},
	{.event_cause = EVENT_CHANNEL_REPORT_RDY,
	.event_handler = nxpwifi_sta_event_channel_report_rdy},
	{.event_cause = EVENT_TX_DATA_PAUSE,
	.event_handler = nxpwifi_sta_event_tx_data_pause},
	{.event_cause = EVENT_EXT_SCAN_REPORT,
	.event_handler = nxpwifi_sta_event_ext_scan_report},
	{.event_cause = EVENT_RXBA_SYNC,
	.event_handler = nxpwifi_sta_event_rxba_sync},
	{.event_cause = EVENT_REMAIN_ON_CHAN_EXPIRED,
	.event_handler = nxpwifi_sta_event_remain_on_chan_expired},
	{.event_cause = EVENT_BG_SCAN_STOPPED,
	.event_handler = nxpwifi_sta_event_bg_scan_stopped},
	{.event_cause = EVENT_MULTI_CHAN_INFO,
	.event_handler = nxpwifi_sta_event_multi_chan_info},
	{.event_cause = EVENT_TX_STATUS_REPORT,
	.event_handler = nxpwifi_sta_event_tx_status_report},
	{.event_cause = EVENT_BT_COEX_WLAN_PARA_CHANGE,
	.event_handler = nxpwifi_sta_event_bt_coex_wlan_para_change},
	{.event_cause = EVENT_VDLL_IND,
	.event_handler = nxpwifi_sta_event_vdll_ind},
	{.event_cause = EVENT_DUMMY_HOST_WAKEUP_SIGNAL,
	.event_handler = NULL},
	{.event_cause = EVENT_MIB_CHANGED,
	.event_handler = NULL},
	{.event_cause = EVENT_INIT_DONE,
	.event_handler = NULL},
	{.event_cause = EVENT_SNR_LOW,
	.event_handler = NULL},
	{.event_cause = EVENT_MAX_FAIL,
	.event_handler = NULL},
	{.event_cause = EVENT_SNR_HIGH,
	.event_handler = NULL},
	{.event_cause = EVENT_DATA_RSSI_LOW,
	.event_handler = NULL},
	{.event_cause = EVENT_DATA_SNR_LOW,
	.event_handler = NULL},
	{.event_cause = EVENT_DATA_RSSI_HIGH,
	.event_handler = NULL},
	{.event_cause = EVENT_DATA_SNR_HIGH,
	.event_handler = NULL},
	{.event_cause = EVENT_LINK_QUALITY,
	.event_handler = NULL},
	{.event_cause = EVENT_PRE_BEACON_LOST,
	.event_handler = NULL},
	{.event_cause = EVENT_WEP_ICV_ERR,
	.event_handler = NULL},
	{.event_cause = EVENT_BW_CHANGE,
	.event_handler = NULL},
	{.event_cause = EVENT_HOSTWAKE_STAIE,
	.event_handler = NULL},
	{.event_cause = EVENT_UNKNOWN_DEBUG,
	.event_handler = NULL},
};

static void nxpwifi_process_uap_tx_pause(struct nxpwifi_private *priv,
					 struct nxpwifi_ie_types_header *tlv)
{
	struct nxpwifi_tx_pause_tlv *tp;
	struct nxpwifi_sta_node *sta_ptr;

	tp = (void *)tlv;
	nxpwifi_dbg(priv->adapter, EVENT,
		    "uap tx_pause: %pM pause=%d, pkts=%d\n",
		    tp->peermac, tp->tx_pause,
		    tp->pkt_cnt);

	if (ether_addr_equal(tp->peermac, priv->netdev->dev_addr)) {
		if (tp->tx_pause)
			priv->port_open = false;
		else
			priv->port_open = true;
	} else if (is_multicast_ether_addr(tp->peermac)) {
		nxpwifi_update_ralist_tx_pause(priv, tp->peermac, tp->tx_pause);
	} else {
		rcu_read_lock();
		sta_ptr = nxpwifi_get_sta_entry(priv, tp->peermac);
		if (sta_ptr && sta_ptr->tx_pause != tp->tx_pause) {
			sta_ptr->tx_pause = tp->tx_pause;
			nxpwifi_update_ralist_tx_pause(priv, tp->peermac,
						       tp->tx_pause);
		}
		rcu_read_unlock();
	}
}

static void nxpwifi_process_sta_tx_pause(struct nxpwifi_private *priv,
					 struct nxpwifi_ie_types_header *tlv)
{
	struct nxpwifi_tx_pause_tlv *tp;

	tp = (void *)tlv;
	nxpwifi_dbg(priv->adapter, EVENT,
		    "sta tx_pause: %pM pause=%d, pkts=%d\n",
		    tp->peermac, tp->tx_pause,
		    tp->pkt_cnt);

	if (ether_addr_equal(tp->peermac, priv->cfg_bssid)) {
		if (tp->tx_pause)
			priv->port_open = false;
		else
			priv->port_open = true;
	}
}

/*
 * Reset connection state after a firmware-triggered disconnect.
 * Clears link state, queues, RSSI/SNR and security settings,
 * saves previous SSID/BSSID for possible reassociation,
 * and notifies cfg80211.
 */
void nxpwifi_reset_connect_state(struct nxpwifi_private *priv, u16 reason_code,
				 bool from_ap)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	if (!priv->media_connected)
		return;

	nxpwifi_dbg(adapter, INFO,
		    "info: handles disconnect event\n");

	priv->media_connected = false;

	priv->auth_flag = 0;
	priv->auth_alg = WLAN_AUTH_NONE;

	priv->scan_block = false;
	priv->port_open = false;

	/* Free Tx and Rx packets, report disconnect to upper layer */
	nxpwifi_clean_txrx(priv);

	/* Reset SNR/NF/RSSI values */
	priv->data_rssi_last = 0;
	priv->data_nf_last = 0;
	priv->data_rssi_avg = 0;
	priv->data_nf_avg = 0;
	priv->bcn_rssi_last = 0;
	priv->bcn_nf_last = 0;
	priv->bcn_rssi_avg = 0;
	priv->bcn_nf_avg = 0;
	priv->rxpd_rate = 0;
	priv->rxpd_htinfo = 0;
	priv->sec_info.wpa_enabled = false;
	priv->sec_info.wpa2_enabled = false;
	priv->wpa_ie_len = 0;

	priv->sec_info.encryption_mode = 0;

	/* Enable auto data rate */
	priv->is_data_rate_auto = true;
	priv->data_rate = 0;

	priv->assoc_resp_ht_param = 0;
	priv->ht_param_present = false;

	if ((GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA ||
	     GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP) && priv->hist_data)
		nxpwifi_hist_data_reset(priv);

	/*
	 * Memorize the previous SSID and BSSID so
	 * it could be used for re-assoc
	 */

	nxpwifi_dbg(adapter, INFO,
		    "info: previous SSID=%s, SSID len=%u\n",
		    priv->prev_ssid.ssid, priv->prev_ssid.ssid_len);

	nxpwifi_dbg(adapter, INFO,
		    "info: current SSID=%s, SSID len=%u\n",
		    priv->curr_bss_params.bss_descriptor.ssid.ssid,
		    priv->curr_bss_params.bss_descriptor.ssid.ssid_len);

	memcpy(&priv->prev_ssid,
	       &priv->curr_bss_params.bss_descriptor.ssid,
	       sizeof(struct cfg80211_ssid));

	memcpy(priv->prev_bssid,
	       priv->curr_bss_params.bss_descriptor.mac_address, ETH_ALEN);

	/* Need to erase the current SSID and BSSID info */
	memset(&priv->curr_bss_params, 0x00, sizeof(priv->curr_bss_params));

	adapter->tx_lock_flag = false;
	adapter->pps_uapsd_mode = false;

	if (test_bit(NXPWIFI_IS_CMD_TIMEDOUT, &adapter->work_flags) &&
	    adapter->curr_cmd)
		return;

	priv->media_connected = false;
	nxpwifi_dbg(adapter, MSG,
		    "info: successfully disconnected from %pM: reason code %d\n",
		    priv->cfg_bssid, reason_code);

	if (priv->bss_mode == NL80211_IFTYPE_STATION) {
		if (adapter->host_mlme_link_lost)
			nxpwifi_host_mlme_disconnect(adapter->priv_link_lost,
						     reason_code, NULL);
		else
			cfg80211_disconnected(priv->netdev, reason_code, NULL,
					      0, !from_ap, GFP_KERNEL);
	}
	eth_zero_addr(priv->cfg_bssid);

	nxpwifi_stop_net_dev_queue(priv->netdev, adapter);
	netif_carrier_off(priv->netdev);

	if (!ISSUPP_FIRMWARE_SUPPLICANT(priv->adapter->fw_cap_info))
		return;

	nxpwifi_send_cmd(priv, HOST_CMD_GTK_REKEY_OFFLOAD_CFG,
			 HOST_ACT_GEN_REMOVE, 0, NULL, false);
}

void nxpwifi_reset_conn_state_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct nxpwifi_private *priv = container_of(work,
						    struct nxpwifi_private,
						    reset_conn_state_work);

	nxpwifi_reset_connect_state(priv, priv->last_deauth_reason, true);
}

void nxpwifi_process_multi_chan_event(struct nxpwifi_private *priv,
				      struct sk_buff *event_skb)
{
	struct nxpwifi_ie_types_multi_chan_info *chan_info;
	struct nxpwifi_ie_types_mc_group_info *grp_info;
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_ie_types_header *tlv;
	u16 tlv_buf_left, tlv_type, tlv_len;
	int intf_num, bss_type, bss_num, i;
	struct nxpwifi_private *intf_priv;

	tlv_buf_left = event_skb->len - sizeof(u32);
	chan_info = (void *)event_skb->data + sizeof(u32);

	if (le16_to_cpu(chan_info->header.type) != TLV_TYPE_MULTI_CHAN_INFO ||
	    tlv_buf_left < sizeof(struct nxpwifi_ie_types_multi_chan_info)) {
		nxpwifi_dbg(adapter, ERROR,
			    "unknown TLV in chan_info event\n");
		return;
	}

	adapter->usb_mc_status = le16_to_cpu(chan_info->status);
	nxpwifi_dbg(adapter, EVENT, "multi chan operation %s\n",
		    adapter->usb_mc_status ? "started" : "over");

	tlv_buf_left -= sizeof(struct nxpwifi_ie_types_multi_chan_info);
	tlv = (struct nxpwifi_ie_types_header *)chan_info->tlv_buffer;

	while (tlv_buf_left >= (int)sizeof(struct nxpwifi_ie_types_header)) {
		tlv_type = le16_to_cpu(tlv->type);
		tlv_len  = le16_to_cpu(tlv->len);
		if ((sizeof(struct nxpwifi_ie_types_header) + tlv_len) >
		    tlv_buf_left) {
			nxpwifi_dbg(adapter, ERROR, "wrong tlv: tlvLen=%d,\t"
				    "tlvBufLeft=%d\n", tlv_len, tlv_buf_left);
			break;
		}
		if (tlv_type != TLV_TYPE_MC_GROUP_INFO) {
			nxpwifi_dbg(adapter, ERROR, "wrong tlv type: 0x%x\n",
				    tlv_type);
			break;
		}

		grp_info = (struct nxpwifi_ie_types_mc_group_info *)tlv;
		intf_num = grp_info->intf_num;
		for (i = 0; i < intf_num; i++) {
			bss_type = grp_info->bss_type_numlist[i] >> 4;
			bss_num = grp_info->bss_type_numlist[i] & BSS_NUM_MASK;
			intf_priv = nxpwifi_get_priv_by_id(adapter, bss_num,
							   bss_type);
			if (!intf_priv) {
				nxpwifi_dbg(adapter, ERROR,
					    "Invalid bss_type bss_num\t"
					    "in multi channel event\n");
				continue;
			}
		}

		tlv_buf_left -= sizeof(struct nxpwifi_ie_types_header) +
				tlv_len;
		tlv = (void *)((u8 *)tlv + tlv_len +
			       sizeof(struct nxpwifi_ie_types_header));
	}
}

void nxpwifi_process_tx_pause_event(struct nxpwifi_private *priv,
				    struct sk_buff *event_skb)
{
	struct nxpwifi_ie_types_header *tlv;
	u16 tlv_type, tlv_len;
	int tlv_buf_left;

	if (!priv->media_connected) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "tx_pause event while disconnected; bss_role=%d\n",
			    priv->bss_role);
		return;
	}

	tlv_buf_left = event_skb->len - sizeof(u32);
	tlv = (void *)event_skb->data + sizeof(u32);

	while (tlv_buf_left >= (int)sizeof(struct nxpwifi_ie_types_header)) {
		tlv_type = le16_to_cpu(tlv->type);
		tlv_len  = le16_to_cpu(tlv->len);
		if ((sizeof(struct nxpwifi_ie_types_header) + tlv_len) >
		    tlv_buf_left) {
			nxpwifi_dbg(priv->adapter, ERROR,
				    "wrong tlv: tlvLen=%d, tlvBufLeft=%d\n",
				    tlv_len, tlv_buf_left);
			break;
		}
		if (tlv_type == TLV_TYPE_TX_PAUSE) {
			if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA)
				nxpwifi_process_sta_tx_pause(priv, tlv);
			else
				nxpwifi_process_uap_tx_pause(priv, tlv);
		}

		tlv_buf_left -= sizeof(struct nxpwifi_ie_types_header) +
				tlv_len;
		tlv = (void *)((u8 *)tlv + tlv_len +
			       sizeof(struct nxpwifi_ie_types_header));
	}
}

/*
 * Handle BT coexistence event. Parse TLVs and update
 * coexistence aggregation window and scan timing parameters.
 */
void nxpwifi_bt_coex_wlan_param_update_event(struct nxpwifi_private *priv,
					     struct sk_buff *event_skb)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_ie_types_header *tlv;
	struct nxpwifi_ie_types_btcoex_aggr_win_size *winsizetlv;
	struct nxpwifi_ie_types_btcoex_scan_time *scantlv;
	s32 len = event_skb->len - sizeof(u32);
	u8 *cur_ptr = event_skb->data + sizeof(u32);
	u16 tlv_type, tlv_len;

	while (len >= sizeof(struct nxpwifi_ie_types_header)) {
		tlv = (struct nxpwifi_ie_types_header *)cur_ptr;
		tlv_len = le16_to_cpu(tlv->len);
		tlv_type = le16_to_cpu(tlv->type);

		if ((tlv_len + sizeof(struct nxpwifi_ie_types_header)) > len)
			break;
		switch (tlv_type) {
		case TLV_BTCOEX_WL_AGGR_WINSIZE:
			winsizetlv =
			    (struct nxpwifi_ie_types_btcoex_aggr_win_size *)tlv;
			adapter->coex_win_size = winsizetlv->coex_win_size;
			adapter->coex_tx_win_size =
				winsizetlv->tx_win_size;
			adapter->coex_rx_win_size =
				winsizetlv->rx_win_size;
			nxpwifi_coex_ampdu_rxwinsize(adapter);
			nxpwifi_update_ampdu_txwinsize(adapter);
			break;

		case TLV_BTCOEX_WL_SCANTIME:
			scantlv =
			    (struct nxpwifi_ie_types_btcoex_scan_time *)tlv;
			adapter->coex_scan = scantlv->coex_scan;
			adapter->coex_min_scan_time = le16_to_cpu(scantlv->min_scan_time);
			adapter->coex_max_scan_time = le16_to_cpu(scantlv->max_scan_time);
			break;

		default:
			break;
		}

		len -= tlv_len + sizeof(struct nxpwifi_ie_types_header);
		cur_ptr += tlv_len +
			sizeof(struct nxpwifi_ie_types_header);
	}

	nxpwifi_dbg(adapter, INFO, "coex_scan=%d min_scan=%d coex_win=%d, tx_win=%d rx_win=%d\n",
		    adapter->coex_scan, adapter->coex_min_scan_time,
		    adapter->coex_win_size, adapter->coex_tx_win_size,
		    adapter->coex_rx_win_size);
}

/*
 * Dispatch station firmware event based on event_cause.
 * Looks up the handler in the station event table and invokes it.
 */
int nxpwifi_process_sta_event(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u32 eventcause = adapter->event_cause;
	int evt, ret = 0;

	for (evt = 0; evt < ARRAY_SIZE(evt_table_sta); evt++) {
		if (eventcause == evt_table_sta[evt].event_cause) {
			if (evt_table_sta[evt].event_handler)
				ret = evt_table_sta[evt].event_handler(priv);
			break;
		}
	}

	if (evt == ARRAY_SIZE(evt_table_sta))
		nxpwifi_dbg(adapter, EVENT,
			    "%s: unknown event id: %#x\n",
			    __func__, eventcause);
	else
		nxpwifi_dbg(adapter, EVENT,
			    "%s: event id: %#x\n",
			    __func__, eventcause);

	return ret;
}
