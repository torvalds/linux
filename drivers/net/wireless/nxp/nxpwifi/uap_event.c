// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP Wireless LAN device driver: AP event handling
 *
 * Copyright 2011-2024 NXP
 */

#include "cfg.h"
#include "main.h"
#include "cmdevt.h"
#include "11n.h"

#define NXPWIFI_BSS_START_EVT_FIX_SIZE    12

static int
nxpwifi_uap_event_ps_awake(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	if (!adapter->pps_uapsd_mode &&
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

	return 0;
}

static int
nxpwifi_uap_event_ps_sleep(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	adapter->ps_state = PS_STATE_PRE_SLEEP;
	nxpwifi_check_ps_cond(adapter);

	return 0;
}

static int
nxpwifi_uap_event_sta_deauth(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u8 *deauth_mac;

	deauth_mac = adapter->event_body +
		     NXPWIFI_UAP_EVENT_EXTRA_HEADER;
	cfg80211_del_sta(priv->netdev->ieee80211_ptr, deauth_mac, GFP_KERNEL);

	if (priv->ap_11n_enabled) {
		nxpwifi_11n_del_rx_reorder_tbl_by_ta(priv, deauth_mac);
		nxpwifi_del_tx_ba_stream_tbl_by_ra(priv, deauth_mac);
	}
	nxpwifi_wmm_del_peer_ra_list(priv, deauth_mac);

	return 0;
}

static int
nxpwifi_uap_event_sta_assoc(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct station_info *sinfo;
	struct nxpwifi_assoc_event *event;
	struct nxpwifi_sta_node *node;
	int len, i;

	sinfo = kzalloc_obj(*sinfo);
	if (!sinfo)
		return -ENOMEM;

	event = (struct nxpwifi_assoc_event *)
		(adapter->event_body + NXPWIFI_UAP_EVENT_EXTRA_HEADER);
	if (le16_to_cpu(event->type) == TLV_TYPE_UAP_MGMT_FRAME) {
		len = -1;

		if (ieee80211_is_assoc_req(event->frame_control))
			len = 0;
		else if (ieee80211_is_reassoc_req(event->frame_control))
			/*
			 * There will be ETH_ALEN bytes of
			 * current_ap_addr before the re-assoc ies.
			 */
			len = ETH_ALEN;

		if (len != -1) {
			u16 evt_len = le16_to_cpu(event->len);

			sinfo->assoc_req_ies = &event->data[len];
			len = (u8 *)sinfo->assoc_req_ies -
			      (u8 *)&event->frame_control;

			/*
			 * event->len is reported by the device firmware
			 * and is not otherwise validated. Reject a length
			 * that underflows the header or that would place
			 * the association request IEs outside the fixed
			 * event_body[] buffer.
			 */
			if (evt_len < len ||
			    (u8 *)&event->frame_control + evt_len >
			    adapter->event_body + MAX_EVENT_SIZE) {
				nxpwifi_dbg(adapter, ERROR,
					    "invalid STA assoc event length\n");
				kfree(sinfo);
				return -EINVAL;
			}

			sinfo->assoc_req_ies_len = evt_len - (u16)len;
		}
	}
	cfg80211_new_sta(priv->netdev->ieee80211_ptr, event->sta_addr, sinfo,
			 GFP_KERNEL);

	node = nxpwifi_add_sta_entry(priv, event->sta_addr);
	if (!node) {
		nxpwifi_dbg(adapter, ERROR,
			    "could not create station entry!\n");
		kfree(sinfo);
		return -ENOENT;
	}

	if (!priv->ap_11n_enabled) {
		kfree(sinfo);
		return 0;
	}

	nxpwifi_set_sta_ht_cap(priv, sinfo->assoc_req_ies,
			       sinfo->assoc_req_ies_len, node);

	for (i = 0; i < MAX_NUM_TID; i++) {
		if (node->is_11n_enabled || node->is_11ax_enabled)
			node->ampdu_sta[i] =
				      priv->aggr_prio_tbl[i].ampdu_user;
		else
			node->ampdu_sta[i] = BA_STREAM_NOT_ALLOWED;
	}
	memset(node->rx_seq, 0xff, sizeof(node->rx_seq));
	kfree(sinfo);

	return 0;
}

static int
nxpwifi_check_uap_capabilities(struct nxpwifi_private *priv,
			       struct sk_buff *event)
{
	int evt_len;
	u8 *curr;
	u16 tlv_len;
	struct nxpwifi_ie_types_data *tlv_hdr;
	struct ieee80211_wmm_param_ie *wmm_param_ie = NULL;
	int mask = IEEE80211_WMM_IE_AP_QOSINFO_PARAM_SET_CNT_MASK;

	priv->wmm_enabled = false;
	skb_pull(event, NXPWIFI_BSS_START_EVT_FIX_SIZE);
	evt_len = event->len;
	curr = event->data;

	nxpwifi_dbg_dump(priv->adapter, EVT_D, "uap capabilities:",
			 event->data, event->len);

	skb_push(event, NXPWIFI_BSS_START_EVT_FIX_SIZE);

	while ((evt_len >= sizeof(tlv_hdr->header))) {
		tlv_hdr = (struct nxpwifi_ie_types_data *)curr;
		tlv_len = le16_to_cpu(tlv_hdr->header.len);

		if (evt_len < tlv_len + sizeof(tlv_hdr->header))
			break;

		switch (le16_to_cpu(tlv_hdr->header.type)) {
		case WLAN_EID_HT_CAPABILITY:
			priv->ap_11n_enabled = true;
			break;

		case WLAN_EID_VHT_CAPABILITY:
			priv->ap_11ac_enabled = true;
			break;

		case WLAN_EID_VENDOR_SPECIFIC:
			/*
			 * Point the regular IEEE element 2 bytes into the NXP element
			 * and setup the IEEE element type and length byte fields
			 */
			wmm_param_ie = (void *)(curr + 2);
			wmm_param_ie->len = (u8)tlv_len;
			wmm_param_ie->element_id =
						WLAN_EID_VENDOR_SPECIFIC;
			nxpwifi_dbg(priv->adapter, EVENT,
				    "info: check uap capabilities:\t"
				    "wmm parameter set count: %d\n",
				    wmm_param_ie->qos_info & mask);

			nxpwifi_wmm_setup_ac_downgrade(priv);
			priv->wmm_enabled = true;
			nxpwifi_wmm_setup_queue_priorities(priv, wmm_param_ie);
			break;

		default:
			break;
		}

		curr += (tlv_len + sizeof(tlv_hdr->header));
		evt_len -= (tlv_len + sizeof(tlv_hdr->header));
	}

	return 0;
}

static int
nxpwifi_uap_event_bss_start(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	priv->port_open = false;
	eth_hw_addr_set(priv->netdev, adapter->event_body + 2);
	if (priv->hist_data)
		nxpwifi_hist_data_reset(priv);
	return nxpwifi_check_uap_capabilities(priv, adapter->event_skb);
}

static int
nxpwifi_uap_event_addba(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	if (priv->media_connected)
		nxpwifi_send_cmd(priv, HOST_CMD_11N_ADDBA_RSP,
				 HOST_ACT_GEN_SET, 0,
				 adapter->event_body, false);

	return 0;
}

static int
nxpwifi_uap_event_delba(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	if (priv->media_connected)
		nxpwifi_11n_delete_ba_stream(priv, adapter->event_body);

	return 0;
}

static int
nxpwifi_uap_event_ba_stream_timeout(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_ds_11n_batimeout *ba_timeout;

	if (priv->media_connected) {
		ba_timeout = (void *)adapter->event_body;
		nxpwifi_11n_ba_stream_timeout(priv, ba_timeout);
	}

	return 0;
}

static int
nxpwifi_uap_event_amsdu_aggr_ctrl(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u16 ctrl;

	ctrl = get_unaligned_le16(adapter->event_body);
	nxpwifi_dbg(adapter, EVENT,
		    "event: AMSDU_AGGR_CTRL %d\n", ctrl);

	if (priv->media_connected) {
		adapter->tx_buf_size =
			min_t(u16, adapter->curr_tx_buf_size, ctrl);
		nxpwifi_dbg(adapter, EVENT,
			    "event: tx_buf_size %d\n",
			    adapter->tx_buf_size);
	}

	return 0;
}

static int
nxpwifi_uap_event_bss_idle(struct nxpwifi_private *priv)
{
	priv->media_connected = false;
	priv->port_open = false;
	nxpwifi_clean_txrx(priv);
	nxpwifi_del_all_sta_list(priv);

	return 0;
}

static int
nxpwifi_uap_event_bss_active(struct nxpwifi_private *priv)
{
	priv->media_connected = true;
	priv->port_open = true;

	return 0;
}

static int
nxpwifi_uap_event_mic_countermeasures(struct nxpwifi_private *priv)
{
	/* For future development */

	return 0;
}

static int
nxpwifi_uap_event_radar_detected(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	return nxpwifi_11h_handle_radar_detected(priv, adapter->event_skb);
}

static int
nxpwifi_uap_event_channel_report_rdy(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	return nxpwifi_11h_handle_chanrpt_ready(priv, adapter->event_skb);
}

static int
nxpwifi_uap_event_tx_data_pause(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	nxpwifi_process_tx_pause_event(priv, adapter->event_skb);

	return 0;
}

static int
nxpwifi_uap_event_ext_scan_report(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	void *buf = adapter->event_skb->data;
	int ret = 0;

	if (adapter->ext_scan)
		ret = nxpwifi_handle_event_ext_scan_report(priv, buf);

	return ret;
}

static int
nxpwifi_uap_event_rxba_sync(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	nxpwifi_11n_rxba_sync_event(priv, adapter->event_body,
				    adapter->event_skb->len -
				    sizeof(adapter->event_cause));

	return 0;
}

static int
nxpwifi_uap_event_remain_on_chan_expired(struct nxpwifi_private *priv)
{
	cfg80211_remain_on_channel_expired(&priv->wdev,
					   priv->roc_cfg.cookie,
					   &priv->roc_cfg.chan,
					   GFP_ATOMIC);
	memset(&priv->roc_cfg, 0x00, sizeof(struct nxpwifi_roc_cfg));

	return 0;
}

static int
nxpwifi_uap_event_multi_chan_info(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	nxpwifi_process_multi_chan_event(priv, adapter->event_skb);

	return 0;
}

static int
nxpwifi_uap_event_tx_status_report(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	nxpwifi_parse_tx_status_event(priv, adapter->event_body);

	return 0;
}

static int
nxpwifi_uap_event_bt_coex_wlan_para_change(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	nxpwifi_bt_coex_wlan_param_update_event(priv, adapter->event_skb);

	return 0;
}

static int
nxpwifi_uap_event_vdll_ind(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	return nxpwifi_process_vdll_event(priv, adapter->event_skb);
}

static const struct nxpwifi_evt_entry evt_table_uap[] = {
	{.event_cause = EVENT_PS_AWAKE,
	 .event_handler = nxpwifi_uap_event_ps_awake},
	{.event_cause = EVENT_PS_SLEEP,
	 .event_handler = nxpwifi_uap_event_ps_sleep},
	{.event_cause = EVENT_UAP_STA_DEAUTH,
	 .event_handler = nxpwifi_uap_event_sta_deauth},
	{.event_cause = EVENT_UAP_STA_ASSOC,
	 .event_handler = nxpwifi_uap_event_sta_assoc},
	{.event_cause = EVENT_UAP_BSS_START,
	 .event_handler = nxpwifi_uap_event_bss_start},
	{.event_cause = EVENT_ADDBA,
	 .event_handler = nxpwifi_uap_event_addba},
	{.event_cause = EVENT_DELBA,
	 .event_handler = nxpwifi_uap_event_delba},
	{.event_cause = EVENT_BA_STREAM_TIEMOUT,
	 .event_handler = nxpwifi_uap_event_ba_stream_timeout},
	{.event_cause = EVENT_AMSDU_AGGR_CTRL,
	 .event_handler = nxpwifi_uap_event_amsdu_aggr_ctrl},
	{.event_cause = EVENT_UAP_BSS_IDLE,
	 .event_handler = nxpwifi_uap_event_bss_idle},
	{.event_cause = EVENT_UAP_BSS_ACTIVE,
	 .event_handler = nxpwifi_uap_event_bss_active},
	{.event_cause = EVENT_UAP_MIC_COUNTERMEASURES,
	 .event_handler = nxpwifi_uap_event_mic_countermeasures},
	{.event_cause = EVENT_RADAR_DETECTED,
	 .event_handler = nxpwifi_uap_event_radar_detected},
	{.event_cause = EVENT_CHANNEL_REPORT_RDY,
	 .event_handler = nxpwifi_uap_event_channel_report_rdy},
	{.event_cause = EVENT_TX_DATA_PAUSE,
	 .event_handler = nxpwifi_uap_event_tx_data_pause},
	{.event_cause = EVENT_EXT_SCAN_REPORT,
	 .event_handler = nxpwifi_uap_event_ext_scan_report},
	{.event_cause = EVENT_RXBA_SYNC,
	 .event_handler = nxpwifi_uap_event_rxba_sync},
	{.event_cause = EVENT_REMAIN_ON_CHAN_EXPIRED,
	 .event_handler = nxpwifi_uap_event_remain_on_chan_expired},
	{.event_cause = EVENT_MULTI_CHAN_INFO,
	 .event_handler = nxpwifi_uap_event_multi_chan_info},
	{.event_cause = EVENT_TX_STATUS_REPORT,
	 .event_handler = nxpwifi_uap_event_tx_status_report},
	{.event_cause = EVENT_BT_COEX_WLAN_PARA_CHANGE,
	 .event_handler = nxpwifi_uap_event_bt_coex_wlan_para_change},
	{.event_cause = EVENT_VDLL_IND,
	 .event_handler = nxpwifi_uap_event_vdll_ind},
};

/* Handle AP‑interface events by dispatching them to event‑specific routines. */
int nxpwifi_process_uap_event(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u32 eventcause = adapter->event_cause;
	int evt, ret = 0;

	for (evt = 0; evt < ARRAY_SIZE(evt_table_uap); evt++) {
		if (eventcause == evt_table_uap[evt].event_cause) {
			if (evt_table_uap[evt].event_handler)
				ret = evt_table_uap[evt].event_handler(priv);
			break;
		}
	}

	if (evt == ARRAY_SIZE(evt_table_uap))
		nxpwifi_dbg(adapter, EVENT,
			    "%s: unknown event id: %#x\n",
			    __func__, eventcause);
	else
		nxpwifi_dbg(adapter, EVENT,
			    "%s: event id: %#x\n",
			    __func__, eventcause);

	return ret;
}
