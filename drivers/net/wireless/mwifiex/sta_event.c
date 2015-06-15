/*
 * Marvell Wireless LAN device driver: station event handling
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "wmm.h"
#include "11n.h"

/*
 * This function resets the connection state.
 *
 * The function is invoked after receiving a disconnect event from firmware,
 * and performs the following actions -
 *      - Set media status to disconnected
 *      - Clean up Tx and Rx packets
 *      - Resets SNR/NF/RSSI value in driver
 *      - Resets security configurations in driver
 *      - Enables auto data rate
 *      - Saves the previous SSID and BSSID so that they can
 *        be used for re-association, if required
 *      - Erases current SSID and BSSID information
 *      - Sends a disconnect event to upper layers/applications.
 */
void
mwifiex_reset_connect_state(struct mwifiex_private *priv, u16 reason_code)
{
	struct mwifiex_adapter *adapter = priv->adapter;

	if (!priv->media_connected)
		return;

	mwifiex_dbg(adapter, INFO,
		    "info: handles disconnect event\n");

	priv->media_connected = false;

	priv->scan_block = false;

	if ((GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_STA) &&
	    ISSUPP_TDLS_ENABLED(priv->adapter->fw_cap_info)) {
		mwifiex_disable_all_tdls_links(priv);

		if (priv->adapter->auto_tdls)
			mwifiex_clean_auto_tdls(priv);
	}

	/* Free Tx and Rx packets, report disconnect to upper layer */
	mwifiex_clean_txrx(priv);

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

	priv->sec_info.wapi_enabled = false;
	priv->wapi_ie_len = 0;
	priv->sec_info.wapi_key_on = false;

	priv->sec_info.encryption_mode = 0;

	/* Enable auto data rate */
	priv->is_data_rate_auto = true;
	priv->data_rate = 0;

	if ((GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_STA ||
	     GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_UAP) && priv->hist_data)
		mwifiex_hist_data_reset(priv);

	if (priv->bss_mode == NL80211_IFTYPE_ADHOC) {
		priv->adhoc_state = ADHOC_IDLE;
		priv->adhoc_is_link_sensed = false;
	}

	/*
	 * Memorize the previous SSID and BSSID so
	 * it could be used for re-assoc
	 */

	mwifiex_dbg(adapter, INFO,
		    "info: previous SSID=%s, SSID len=%u\n",
		    priv->prev_ssid.ssid, priv->prev_ssid.ssid_len);

	mwifiex_dbg(adapter, INFO,
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

	if (adapter->is_cmd_timedout && adapter->curr_cmd)
		return;
	priv->media_connected = false;
	mwifiex_dbg(adapter, MSG,
		    "info: successfully disconnected from %pM: reason code %d\n",
		    priv->cfg_bssid, reason_code);
	if (priv->bss_mode == NL80211_IFTYPE_STATION ||
	    priv->bss_mode == NL80211_IFTYPE_P2P_CLIENT) {
		cfg80211_disconnected(priv->netdev, reason_code, NULL, 0,
				      false, GFP_KERNEL);
	}
	eth_zero_addr(priv->cfg_bssid);

	mwifiex_stop_net_dev_queue(priv->netdev, adapter);
	if (netif_carrier_ok(priv->netdev))
		netif_carrier_off(priv->netdev);
}

static int mwifiex_parse_tdls_event(struct mwifiex_private *priv,
				    struct sk_buff *event_skb)
{
	int ret = 0;
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_sta_node *sta_ptr;
	struct mwifiex_tdls_generic_event *tdls_evt =
			(void *)event_skb->data + sizeof(adapter->event_cause);

	/* reserved 2 bytes are not mandatory in tdls event */
	if (event_skb->len < (sizeof(struct mwifiex_tdls_generic_event) -
			      sizeof(u16) - sizeof(adapter->event_cause))) {
		mwifiex_dbg(adapter, ERROR, "Invalid event length!\n");
		return -1;
	}

	sta_ptr = mwifiex_get_sta_entry(priv, tdls_evt->peer_mac);
	if (!sta_ptr) {
		mwifiex_dbg(adapter, ERROR, "cannot get sta entry!\n");
		return -1;
	}

	switch (le16_to_cpu(tdls_evt->type)) {
	case TDLS_EVENT_LINK_TEAR_DOWN:
		cfg80211_tdls_oper_request(priv->netdev,
					   tdls_evt->peer_mac,
					   NL80211_TDLS_TEARDOWN,
					   le16_to_cpu(tdls_evt->u.reason_code),
					   GFP_KERNEL);
		break;
	default:
		break;
	}

	return ret;
}

/*
* This function handles coex events generated by firmware
*/
void mwifiex_bt_coex_wlan_param_update_event(struct mwifiex_private *priv,
					     struct sk_buff *event_skb)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_ie_types_header *tlv;
	struct mwifiex_ie_types_btcoex_aggr_win_size *winsizetlv;
	struct mwifiex_ie_types_btcoex_scan_time *scantlv;
	s32 len = event_skb->len - sizeof(u32);
	u8 *cur_ptr = event_skb->data + sizeof(u32);
	u16 tlv_type, tlv_len;

	while (len >= sizeof(struct mwifiex_ie_types_header)) {
		tlv = (struct mwifiex_ie_types_header *)cur_ptr;
		tlv_len = le16_to_cpu(tlv->len);
		tlv_type = le16_to_cpu(tlv->type);

		if ((tlv_len + sizeof(struct mwifiex_ie_types_header)) > len)
			break;
		switch (tlv_type) {
		case TLV_BTCOEX_WL_AGGR_WINSIZE:
			winsizetlv =
			    (struct mwifiex_ie_types_btcoex_aggr_win_size *)tlv;
			adapter->coex_win_size = winsizetlv->coex_win_size;
			adapter->coex_tx_win_size =
				winsizetlv->tx_win_size;
			adapter->coex_rx_win_size =
				winsizetlv->rx_win_size;
			mwifiex_coex_ampdu_rxwinsize(adapter);
			mwifiex_update_ampdu_txwinsize(adapter);
			break;

		case TLV_BTCOEX_WL_SCANTIME:
			scantlv =
			    (struct mwifiex_ie_types_btcoex_scan_time *)tlv;
			adapter->coex_scan = scantlv->coex_scan;
			adapter->coex_min_scan_time = scantlv->min_scan_time;
			adapter->coex_max_scan_time = scantlv->max_scan_time;
			break;

		default:
			break;
		}

		len -= tlv_len + sizeof(struct mwifiex_ie_types_header);
		cur_ptr += tlv_len +
			sizeof(struct mwifiex_ie_types_header);
	}

	dev_dbg(adapter->dev, "coex_scan=%d min_scan=%d coex_win=%d, tx_win=%d rx_win=%d\n",
		adapter->coex_scan, adapter->coex_min_scan_time,
		adapter->coex_win_size, adapter->coex_tx_win_size,
		adapter->coex_rx_win_size);
}

/*
 * This function handles events generated by firmware.
 *
 * This is a generic function and handles all events.
 *
 * Event specific routines are called by this function based
 * upon the generated event cause.
 *
 * For the following events, the function just forwards them to upper
 * layers, optionally recording the change -
 *      - EVENT_LINK_SENSED
 *      - EVENT_MIC_ERR_UNICAST
 *      - EVENT_MIC_ERR_MULTICAST
 *      - EVENT_PORT_RELEASE
 *      - EVENT_RSSI_LOW
 *      - EVENT_SNR_LOW
 *      - EVENT_MAX_FAIL
 *      - EVENT_RSSI_HIGH
 *      - EVENT_SNR_HIGH
 *      - EVENT_DATA_RSSI_LOW
 *      - EVENT_DATA_SNR_LOW
 *      - EVENT_DATA_RSSI_HIGH
 *      - EVENT_DATA_SNR_HIGH
 *      - EVENT_LINK_QUALITY
 *      - EVENT_PRE_BEACON_LOST
 *      - EVENT_IBSS_COALESCED
 *      - EVENT_WEP_ICV_ERR
 *      - EVENT_BW_CHANGE
 *      - EVENT_HOSTWAKE_STAIE
  *
 * For the following events, no action is taken -
 *      - EVENT_MIB_CHANGED
 *      - EVENT_INIT_DONE
 *      - EVENT_DUMMY_HOST_WAKEUP_SIGNAL
 *
 * Rest of the supported events requires driver handling -
 *      - EVENT_DEAUTHENTICATED
 *      - EVENT_DISASSOCIATED
 *      - EVENT_LINK_LOST
 *      - EVENT_PS_SLEEP
 *      - EVENT_PS_AWAKE
 *      - EVENT_DEEP_SLEEP_AWAKE
 *      - EVENT_HS_ACT_REQ
 *      - EVENT_ADHOC_BCN_LOST
 *      - EVENT_BG_SCAN_REPORT
 *      - EVENT_WMM_STATUS_CHANGE
 *      - EVENT_ADDBA
 *      - EVENT_DELBA
 *      - EVENT_BA_STREAM_TIEMOUT
 *      - EVENT_AMSDU_AGGR_CTRL
 */
int mwifiex_process_sta_event(struct mwifiex_private *priv)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	int ret = 0;
	u32 eventcause = adapter->event_cause;
	u16 ctrl, reason_code;

	switch (eventcause) {
	case EVENT_DUMMY_HOST_WAKEUP_SIGNAL:
		mwifiex_dbg(adapter, ERROR,
			    "invalid EVENT: DUMMY_HOST_WAKEUP_SIGNAL, ignore it\n");
		break;
	case EVENT_LINK_SENSED:
		mwifiex_dbg(adapter, EVENT, "event: LINK_SENSED\n");
		if (!netif_carrier_ok(priv->netdev))
			netif_carrier_on(priv->netdev);
		mwifiex_wake_up_net_dev_queue(priv->netdev, adapter);
		break;

	case EVENT_DEAUTHENTICATED:
		mwifiex_dbg(adapter, EVENT, "event: Deauthenticated\n");
		if (priv->wps.session_enable) {
			mwifiex_dbg(adapter, INFO,
				    "info: receive deauth event in wps session\n");
			break;
		}
		adapter->dbg.num_event_deauth++;
		if (priv->media_connected) {
			reason_code =
				le16_to_cpu(*(__le16 *)adapter->event_body);
			mwifiex_reset_connect_state(priv, reason_code);
		}
		break;

	case EVENT_DISASSOCIATED:
		mwifiex_dbg(adapter, EVENT, "event: Disassociated\n");
		if (priv->wps.session_enable) {
			mwifiex_dbg(adapter, INFO,
				    "info: receive disassoc event in wps session\n");
			break;
		}
		adapter->dbg.num_event_disassoc++;
		if (priv->media_connected) {
			reason_code =
				le16_to_cpu(*(__le16 *)adapter->event_body);
			mwifiex_reset_connect_state(priv, reason_code);
		}
		break;

	case EVENT_LINK_LOST:
		mwifiex_dbg(adapter, EVENT, "event: Link lost\n");
		adapter->dbg.num_event_link_lost++;
		if (priv->media_connected) {
			reason_code =
				le16_to_cpu(*(__le16 *)adapter->event_body);
			mwifiex_reset_connect_state(priv, reason_code);
		}
		break;

	case EVENT_PS_SLEEP:
		mwifiex_dbg(adapter, EVENT, "info: EVENT: SLEEP\n");

		adapter->ps_state = PS_STATE_PRE_SLEEP;

		mwifiex_check_ps_cond(adapter);
		break;

	case EVENT_PS_AWAKE:
		mwifiex_dbg(adapter, EVENT, "info: EVENT: AWAKE\n");
		if (!adapter->pps_uapsd_mode &&
		    priv->media_connected && adapter->sleep_period.period) {
				adapter->pps_uapsd_mode = true;
				mwifiex_dbg(adapter, EVENT,
					    "event: PPS/UAPSD mode activated\n");
		}
		adapter->tx_lock_flag = false;
		if (adapter->pps_uapsd_mode && adapter->gen_null_pkt) {
			if (mwifiex_check_last_packet_indication(priv)) {
				if (adapter->data_sent) {
					adapter->ps_state = PS_STATE_AWAKE;
					adapter->pm_wakeup_card_req = false;
					adapter->pm_wakeup_fw_try = false;
					del_timer(&adapter->wakeup_timer);
					break;
				}
				if (!mwifiex_send_null_packet
					(priv,
					 MWIFIEX_TxPD_POWER_MGMT_NULL_PACKET |
					 MWIFIEX_TxPD_POWER_MGMT_LAST_PACKET))
						adapter->ps_state =
							PS_STATE_SLEEP;
					return 0;
			}
		}
		adapter->ps_state = PS_STATE_AWAKE;
		adapter->pm_wakeup_card_req = false;
		adapter->pm_wakeup_fw_try = false;
		del_timer(&adapter->wakeup_timer);

		break;

	case EVENT_DEEP_SLEEP_AWAKE:
		adapter->if_ops.wakeup_complete(adapter);
		mwifiex_dbg(adapter, EVENT, "event: DS_AWAKE\n");
		if (adapter->is_deep_sleep)
			adapter->is_deep_sleep = false;
		break;

	case EVENT_HS_ACT_REQ:
		mwifiex_dbg(adapter, EVENT, "event: HS_ACT_REQ\n");
		ret = mwifiex_send_cmd(priv, HostCmd_CMD_802_11_HS_CFG_ENH,
				       0, 0, NULL, false);
		break;

	case EVENT_MIC_ERR_UNICAST:
		mwifiex_dbg(adapter, EVENT, "event: UNICAST MIC ERROR\n");
		cfg80211_michael_mic_failure(priv->netdev, priv->cfg_bssid,
					     NL80211_KEYTYPE_PAIRWISE,
					     -1, NULL, GFP_KERNEL);
		break;

	case EVENT_MIC_ERR_MULTICAST:
		mwifiex_dbg(adapter, EVENT, "event: MULTICAST MIC ERROR\n");
		cfg80211_michael_mic_failure(priv->netdev, priv->cfg_bssid,
					     NL80211_KEYTYPE_GROUP,
					     -1, NULL, GFP_KERNEL);
		break;
	case EVENT_MIB_CHANGED:
	case EVENT_INIT_DONE:
		break;

	case EVENT_ADHOC_BCN_LOST:
		mwifiex_dbg(adapter, EVENT, "event: ADHOC_BCN_LOST\n");
		priv->adhoc_is_link_sensed = false;
		mwifiex_clean_txrx(priv);
		mwifiex_stop_net_dev_queue(priv->netdev, adapter);
		if (netif_carrier_ok(priv->netdev))
			netif_carrier_off(priv->netdev);
		break;

	case EVENT_BG_SCAN_REPORT:
		mwifiex_dbg(adapter, EVENT, "event: BGS_REPORT\n");
		ret = mwifiex_send_cmd(priv, HostCmd_CMD_802_11_BG_SCAN_QUERY,
				       HostCmd_ACT_GEN_GET, 0, NULL, false);
		break;

	case EVENT_PORT_RELEASE:
		mwifiex_dbg(adapter, EVENT, "event: PORT RELEASE\n");
		break;

	case EVENT_EXT_SCAN_REPORT:
		mwifiex_dbg(adapter, EVENT, "event: EXT_SCAN Report\n");
		if (adapter->ext_scan)
			ret = mwifiex_handle_event_ext_scan_report(priv,
						adapter->event_skb->data);

		break;

	case EVENT_WMM_STATUS_CHANGE:
		mwifiex_dbg(adapter, EVENT, "event: WMM status changed\n");
		ret = mwifiex_send_cmd(priv, HostCmd_CMD_WMM_GET_STATUS,
				       0, 0, NULL, false);
		break;

	case EVENT_RSSI_LOW:
		cfg80211_cqm_rssi_notify(priv->netdev,
					 NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW,
					 GFP_KERNEL);
		mwifiex_send_cmd(priv, HostCmd_CMD_RSSI_INFO,
				 HostCmd_ACT_GEN_GET, 0, NULL, false);
		priv->subsc_evt_rssi_state = RSSI_LOW_RECVD;
		mwifiex_dbg(adapter, EVENT, "event: Beacon RSSI_LOW\n");
		break;
	case EVENT_SNR_LOW:
		mwifiex_dbg(adapter, EVENT, "event: Beacon SNR_LOW\n");
		break;
	case EVENT_MAX_FAIL:
		mwifiex_dbg(adapter, EVENT, "event: MAX_FAIL\n");
		break;
	case EVENT_RSSI_HIGH:
		cfg80211_cqm_rssi_notify(priv->netdev,
					 NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
					 GFP_KERNEL);
		mwifiex_send_cmd(priv, HostCmd_CMD_RSSI_INFO,
				 HostCmd_ACT_GEN_GET, 0, NULL, false);
		priv->subsc_evt_rssi_state = RSSI_HIGH_RECVD;
		mwifiex_dbg(adapter, EVENT, "event: Beacon RSSI_HIGH\n");
		break;
	case EVENT_SNR_HIGH:
		mwifiex_dbg(adapter, EVENT, "event: Beacon SNR_HIGH\n");
		break;
	case EVENT_DATA_RSSI_LOW:
		mwifiex_dbg(adapter, EVENT, "event: Data RSSI_LOW\n");
		break;
	case EVENT_DATA_SNR_LOW:
		mwifiex_dbg(adapter, EVENT, "event: Data SNR_LOW\n");
		break;
	case EVENT_DATA_RSSI_HIGH:
		mwifiex_dbg(adapter, EVENT, "event: Data RSSI_HIGH\n");
		break;
	case EVENT_DATA_SNR_HIGH:
		mwifiex_dbg(adapter, EVENT, "event: Data SNR_HIGH\n");
		break;
	case EVENT_LINK_QUALITY:
		mwifiex_dbg(adapter, EVENT, "event: Link Quality\n");
		break;
	case EVENT_PRE_BEACON_LOST:
		mwifiex_dbg(adapter, EVENT, "event: Pre-Beacon Lost\n");
		break;
	case EVENT_IBSS_COALESCED:
		mwifiex_dbg(adapter, EVENT, "event: IBSS_COALESCED\n");
		ret = mwifiex_send_cmd(priv,
				HostCmd_CMD_802_11_IBSS_COALESCING_STATUS,
				HostCmd_ACT_GEN_GET, 0, NULL, false);
		break;
	case EVENT_ADDBA:
		mwifiex_dbg(adapter, EVENT, "event: ADDBA Request\n");
		mwifiex_send_cmd(priv, HostCmd_CMD_11N_ADDBA_RSP,
				 HostCmd_ACT_GEN_SET, 0,
				 adapter->event_body, false);
		break;
	case EVENT_DELBA:
		mwifiex_dbg(adapter, EVENT, "event: DELBA Request\n");
		mwifiex_11n_delete_ba_stream(priv, adapter->event_body);
		break;
	case EVENT_BA_STREAM_TIEMOUT:
		mwifiex_dbg(adapter, EVENT, "event:  BA Stream timeout\n");
		mwifiex_11n_ba_stream_timeout(priv,
					      (struct host_cmd_ds_11n_batimeout
					       *)
					      adapter->event_body);
		break;
	case EVENT_AMSDU_AGGR_CTRL:
		ctrl = le16_to_cpu(*(__le16 *)adapter->event_body);
		mwifiex_dbg(adapter, EVENT,
			    "event: AMSDU_AGGR_CTRL %d\n", ctrl);

		adapter->tx_buf_size =
				min_t(u16, adapter->curr_tx_buf_size, ctrl);
		mwifiex_dbg(adapter, EVENT, "event: tx_buf_size %d\n",
			    adapter->tx_buf_size);
		break;

	case EVENT_WEP_ICV_ERR:
		mwifiex_dbg(adapter, EVENT, "event: WEP ICV error\n");
		break;

	case EVENT_BW_CHANGE:
		mwifiex_dbg(adapter, EVENT, "event: BW Change\n");
		break;

	case EVENT_HOSTWAKE_STAIE:
		mwifiex_dbg(adapter, EVENT,
			    "event: HOSTWAKE_STAIE %d\n", eventcause);
		break;

	case EVENT_REMAIN_ON_CHAN_EXPIRED:
		mwifiex_dbg(adapter, EVENT,
			    "event: Remain on channel expired\n");
		cfg80211_remain_on_channel_expired(&priv->wdev,
						   priv->roc_cfg.cookie,
						   &priv->roc_cfg.chan,
						   GFP_ATOMIC);

		memset(&priv->roc_cfg, 0x00, sizeof(struct mwifiex_roc_cfg));

		break;

	case EVENT_CHANNEL_SWITCH_ANN:
		mwifiex_dbg(adapter, EVENT, "event: Channel Switch Announcement\n");
		priv->csa_expire_time =
				jiffies + msecs_to_jiffies(DFS_CHAN_MOVE_TIME);
		priv->csa_chan = priv->curr_bss_params.bss_descriptor.channel;
		ret = mwifiex_send_cmd(priv, HostCmd_CMD_802_11_DEAUTHENTICATE,
			HostCmd_ACT_GEN_SET, 0,
			priv->curr_bss_params.bss_descriptor.mac_address,
			false);
		break;

	case EVENT_TDLS_GENERIC_EVENT:
		ret = mwifiex_parse_tdls_event(priv, adapter->event_skb);
		break;

	case EVENT_TX_STATUS_REPORT:
		mwifiex_dbg(adapter, EVENT, "event: TX_STATUS Report\n");
		mwifiex_parse_tx_status_event(priv, adapter->event_body);
		break;

	case EVENT_CHANNEL_REPORT_RDY:
		mwifiex_dbg(adapter, EVENT, "event: Channel Report\n");
		ret = mwifiex_11h_handle_chanrpt_ready(priv,
						       adapter->event_skb);
		break;
	case EVENT_RADAR_DETECTED:
		mwifiex_dbg(adapter, EVENT, "event: Radar detected\n");
		ret = mwifiex_11h_handle_radar_detected(priv,
							adapter->event_skb);
		break;
	case EVENT_BT_COEX_WLAN_PARA_CHANGE:
		dev_dbg(adapter->dev, "EVENT: BT coex wlan param update\n");
		mwifiex_bt_coex_wlan_param_update_event(priv,
							adapter->event_skb);
		break;
	default:
		mwifiex_dbg(adapter, ERROR, "event: unknown event id: %#x\n",
			    eventcause);
		break;
	}

	return ret;
}
