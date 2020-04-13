/*
 * NXP Wireless LAN device driver: station event handling
 *
 * Copyright 2011-2020 NXP
 *
 * This software file (the "File") is distributed by NXP
 * under the terms of the GNU General Public License Version 2, June 1991
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

#define MWIFIEX_IBSS_CONNECT_EVT_FIX_SIZE    12

static int mwifiex_check_ibss_peer_capabilities(struct mwifiex_private *priv,
					        struct mwifiex_sta_node *sta_ptr,
					        struct sk_buff *event)
{
	int evt_len, ele_len;
	u8 *curr;
	struct ieee_types_header *ele_hdr;
	struct mwifiex_ie_types_mgmt_frame *tlv_mgmt_frame;
	const struct ieee80211_ht_cap *ht_cap;
	const struct ieee80211_vht_cap *vht_cap;

	skb_pull(event, MWIFIEX_IBSS_CONNECT_EVT_FIX_SIZE);
	evt_len = event->len;
	curr = event->data;

	mwifiex_dbg_dump(priv->adapter, EVT_D, "ibss peer capabilities:",
			 event->data, event->len);

	skb_push(event, MWIFIEX_IBSS_CONNECT_EVT_FIX_SIZE);

	tlv_mgmt_frame = (void *)curr;
	if (evt_len >= sizeof(*tlv_mgmt_frame) &&
	    le16_to_cpu(tlv_mgmt_frame->header.type) ==
	    TLV_TYPE_UAP_MGMT_FRAME) {
		/* Locate curr pointer to the start of beacon tlv,
		 * timestamp 8 bytes, beacon intervel 2 bytes,
		 * capability info 2 bytes, totally 12 byte beacon header
		 */
		evt_len = le16_to_cpu(tlv_mgmt_frame->header.len);
		curr += (sizeof(*tlv_mgmt_frame) + 12);
	} else {
		mwifiex_dbg(priv->adapter, MSG,
			    "management frame tlv not found!\n");
		return 0;
	}

	while (evt_len >= sizeof(*ele_hdr)) {
		ele_hdr = (struct ieee_types_header *)curr;
		ele_len = ele_hdr->len;

		if (evt_len < ele_len + sizeof(*ele_hdr))
			break;

		switch (ele_hdr->element_id) {
		case WLAN_EID_HT_CAPABILITY:
			sta_ptr->is_11n_enabled = true;
			ht_cap = (void *)(ele_hdr + 2);
			sta_ptr->max_amsdu = le16_to_cpu(ht_cap->cap_info) &
				IEEE80211_HT_CAP_MAX_AMSDU ?
				MWIFIEX_TX_DATA_BUF_SIZE_8K :
				MWIFIEX_TX_DATA_BUF_SIZE_4K;
			mwifiex_dbg(priv->adapter, INFO,
				    "11n enabled!, max_amsdu : %d\n",
				    sta_ptr->max_amsdu);
			break;

		case WLAN_EID_VHT_CAPABILITY:
			sta_ptr->is_11ac_enabled = true;
			vht_cap = (void *)(ele_hdr + 2);
			/* check VHT MAXMPDU capability */
			switch (le32_to_cpu(vht_cap->vht_cap_info) & 0x3) {
			case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454:
				sta_ptr->max_amsdu =
					MWIFIEX_TX_DATA_BUF_SIZE_12K;
				break;
			case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991:
				sta_ptr->max_amsdu =
					MWIFIEX_TX_DATA_BUF_SIZE_8K;
				break;
			case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895:
				sta_ptr->max_amsdu =
					MWIFIEX_TX_DATA_BUF_SIZE_4K;
			default:
				break;
			}

			mwifiex_dbg(priv->adapter, INFO,
				    "11ac enabled!, max_amsdu : %d\n",
				    sta_ptr->max_amsdu);
			break;
		default:
			break;
		}

		curr += (ele_len + sizeof(*ele_hdr));
		evt_len -= (ele_len + sizeof(*ele_hdr));
	}

	return 0;
}

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
void mwifiex_reset_connect_state(struct mwifiex_private *priv, u16 reason_code,
				 bool from_ap)
{
	struct mwifiex_adapter *adapter = priv->adapter;

	if (!priv->media_connected)
		return;

	mwifiex_dbg(adapter, INFO,
		    "info: handles disconnect event\n");

	priv->media_connected = false;

	priv->scan_block = false;
	priv->port_open = false;

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

	priv->assoc_resp_ht_param = 0;
	priv->ht_param_present = false;

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

	if (test_bit(MWIFIEX_IS_CMD_TIMEDOUT, &adapter->work_flags) &&
	    adapter->curr_cmd)
		return;
	priv->media_connected = false;
	mwifiex_dbg(adapter, MSG,
		    "info: successfully disconnected from %pM: reason code %d\n",
		    priv->cfg_bssid, reason_code);
	if (priv->bss_mode == NL80211_IFTYPE_STATION ||
	    priv->bss_mode == NL80211_IFTYPE_P2P_CLIENT) {
		cfg80211_disconnected(priv->netdev, reason_code, NULL, 0,
				      !from_ap, GFP_KERNEL);
	}
	eth_zero_addr(priv->cfg_bssid);

	mwifiex_stop_net_dev_queue(priv->netdev, adapter);
	if (netif_carrier_ok(priv->netdev))
		netif_carrier_off(priv->netdev);

	if (!ISSUPP_FIRMWARE_SUPPLICANT(priv->adapter->fw_cap_info))
		return;

	mwifiex_send_cmd(priv, HostCmd_CMD_GTK_REKEY_OFFLOAD_CFG,
			 HostCmd_ACT_GEN_REMOVE, 0, NULL, false);
}

static int mwifiex_parse_tdls_event(struct mwifiex_private *priv,
				    struct sk_buff *event_skb)
{
	int ret = 0;
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_sta_node *sta_ptr;
	struct mwifiex_tdls_generic_event *tdls_evt =
			(void *)event_skb->data + sizeof(adapter->event_cause);
	u8 *mac = tdls_evt->peer_mac;

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
	case TDLS_EVENT_CHAN_SWITCH_RESULT:
		mwifiex_dbg(adapter, EVENT, "tdls channel switch result :\n");
		mwifiex_dbg(adapter, EVENT,
			    "status=0x%x, reason=0x%x cur_chan=%d\n",
			    tdls_evt->u.switch_result.status,
			    tdls_evt->u.switch_result.reason,
			    tdls_evt->u.switch_result.cur_chan);

		/* tdls channel switch failed */
		if (tdls_evt->u.switch_result.status != 0) {
			switch (tdls_evt->u.switch_result.cur_chan) {
			case TDLS_BASE_CHANNEL:
				sta_ptr->tdls_status = TDLS_IN_BASE_CHAN;
				break;
			case TDLS_OFF_CHANNEL:
				sta_ptr->tdls_status = TDLS_IN_OFF_CHAN;
				break;
			default:
				break;
			}
			return ret;
		}

		/* tdls channel switch success */
		switch (tdls_evt->u.switch_result.cur_chan) {
		case TDLS_BASE_CHANNEL:
			if (sta_ptr->tdls_status == TDLS_IN_BASE_CHAN)
				break;
			mwifiex_update_ralist_tx_pause_in_tdls_cs(priv, mac,
								  false);
			sta_ptr->tdls_status = TDLS_IN_BASE_CHAN;
			break;
		case TDLS_OFF_CHANNEL:
			if (sta_ptr->tdls_status == TDLS_IN_OFF_CHAN)
				break;
			mwifiex_update_ralist_tx_pause_in_tdls_cs(priv, mac,
								  true);
			sta_ptr->tdls_status = TDLS_IN_OFF_CHAN;
			break;
		default:
			break;
		}

		break;
	case TDLS_EVENT_START_CHAN_SWITCH:
		mwifiex_dbg(adapter, EVENT, "tdls start channel switch...\n");
		sta_ptr->tdls_status = TDLS_CHAN_SWITCHING;
		break;
	case TDLS_EVENT_CHAN_SWITCH_STOPPED:
		mwifiex_dbg(adapter, EVENT,
			    "tdls chan switch stopped, reason=%d\n",
			    tdls_evt->u.cs_stop_reason);
		break;
	default:
		break;
	}

	return ret;
}

static void mwifiex_process_uap_tx_pause(struct mwifiex_private *priv,
					 struct mwifiex_ie_types_header *tlv)
{
	struct mwifiex_tx_pause_tlv *tp;
	struct mwifiex_sta_node *sta_ptr;

	tp = (void *)tlv;
	mwifiex_dbg(priv->adapter, EVENT,
		    "uap tx_pause: %pM pause=%d, pkts=%d\n",
		    tp->peermac, tp->tx_pause,
		    tp->pkt_cnt);

	if (ether_addr_equal(tp->peermac, priv->netdev->dev_addr)) {
		if (tp->tx_pause)
			priv->port_open = false;
		else
			priv->port_open = true;
	} else if (is_multicast_ether_addr(tp->peermac)) {
		mwifiex_update_ralist_tx_pause(priv, tp->peermac, tp->tx_pause);
	} else {
		spin_lock_bh(&priv->sta_list_spinlock);
		sta_ptr = mwifiex_get_sta_entry(priv, tp->peermac);
		if (sta_ptr && sta_ptr->tx_pause != tp->tx_pause) {
			sta_ptr->tx_pause = tp->tx_pause;
			mwifiex_update_ralist_tx_pause(priv, tp->peermac,
						       tp->tx_pause);
		}
		spin_unlock_bh(&priv->sta_list_spinlock);
	}
}

static void mwifiex_process_sta_tx_pause(struct mwifiex_private *priv,
					 struct mwifiex_ie_types_header *tlv)
{
	struct mwifiex_tx_pause_tlv *tp;
	struct mwifiex_sta_node *sta_ptr;
	int status;

	tp = (void *)tlv;
	mwifiex_dbg(priv->adapter, EVENT,
		    "sta tx_pause: %pM pause=%d, pkts=%d\n",
		    tp->peermac, tp->tx_pause,
		    tp->pkt_cnt);

	if (ether_addr_equal(tp->peermac, priv->cfg_bssid)) {
		if (tp->tx_pause)
			priv->port_open = false;
		else
			priv->port_open = true;
	} else {
		if (!ISSUPP_TDLS_ENABLED(priv->adapter->fw_cap_info))
			return;

		status = mwifiex_get_tdls_link_status(priv, tp->peermac);
		if (mwifiex_is_tdls_link_setup(status)) {
			spin_lock_bh(&priv->sta_list_spinlock);
			sta_ptr = mwifiex_get_sta_entry(priv, tp->peermac);
			if (sta_ptr && sta_ptr->tx_pause != tp->tx_pause) {
				sta_ptr->tx_pause = tp->tx_pause;
				mwifiex_update_ralist_tx_pause(priv,
							       tp->peermac,
							       tp->tx_pause);
			}
			spin_unlock_bh(&priv->sta_list_spinlock);
		}
	}
}

void mwifiex_process_multi_chan_event(struct mwifiex_private *priv,
				      struct sk_buff *event_skb)
{
	struct mwifiex_ie_types_multi_chan_info *chan_info;
	struct mwifiex_ie_types_mc_group_info *grp_info;
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_ie_types_header *tlv;
	u16 tlv_buf_left, tlv_type, tlv_len;
	int intf_num, bss_type, bss_num, i;
	struct mwifiex_private *intf_priv;

	tlv_buf_left = event_skb->len - sizeof(u32);
	chan_info = (void *)event_skb->data + sizeof(u32);

	if (le16_to_cpu(chan_info->header.type) != TLV_TYPE_MULTI_CHAN_INFO ||
	    tlv_buf_left < sizeof(struct mwifiex_ie_types_multi_chan_info)) {
		mwifiex_dbg(adapter, ERROR,
			    "unknown TLV in chan_info event\n");
		return;
	}

	adapter->usb_mc_status = le16_to_cpu(chan_info->status);
	mwifiex_dbg(adapter, EVENT, "multi chan operation %s\n",
		    adapter->usb_mc_status ? "started" : "over");

	tlv_buf_left -= sizeof(struct mwifiex_ie_types_multi_chan_info);
	tlv = (struct mwifiex_ie_types_header *)chan_info->tlv_buffer;

	while (tlv_buf_left >= (int)sizeof(struct mwifiex_ie_types_header)) {
		tlv_type = le16_to_cpu(tlv->type);
		tlv_len  = le16_to_cpu(tlv->len);
		if ((sizeof(struct mwifiex_ie_types_header) + tlv_len) >
		    tlv_buf_left) {
			mwifiex_dbg(adapter, ERROR, "wrong tlv: tlvLen=%d,\t"
				    "tlvBufLeft=%d\n", tlv_len, tlv_buf_left);
			break;
		}
		if (tlv_type != TLV_TYPE_MC_GROUP_INFO) {
			mwifiex_dbg(adapter, ERROR, "wrong tlv type: 0x%x\n",
				    tlv_type);
			break;
		}

		grp_info = (struct mwifiex_ie_types_mc_group_info *)tlv;
		intf_num = grp_info->intf_num;
		for (i = 0; i < intf_num; i++) {
			bss_type = grp_info->bss_type_numlist[i] >> 4;
			bss_num = grp_info->bss_type_numlist[i] & BSS_NUM_MASK;
			intf_priv = mwifiex_get_priv_by_id(adapter, bss_num,
							   bss_type);
			if (!intf_priv) {
				mwifiex_dbg(adapter, ERROR,
					    "Invalid bss_type bss_num\t"
					    "in multi channel event\n");
				continue;
			}
			if (adapter->iface_type == MWIFIEX_USB) {
				u8 ep;

				ep = grp_info->hid_num.usb_ep_num;
				if (ep == MWIFIEX_USB_EP_DATA ||
				    ep == MWIFIEX_USB_EP_DATA_CH2)
					intf_priv->usb_port = ep;
			}
		}

		tlv_buf_left -= sizeof(struct mwifiex_ie_types_header) +
				tlv_len;
		tlv = (void *)((u8 *)tlv + tlv_len +
			       sizeof(struct mwifiex_ie_types_header));
	}

	if (adapter->iface_type == MWIFIEX_USB) {
		adapter->tx_lock_flag = true;
		adapter->usb_mc_setup = true;
		mwifiex_multi_chan_resync(adapter);
	}
}

void mwifiex_process_tx_pause_event(struct mwifiex_private *priv,
				    struct sk_buff *event_skb)
{
	struct mwifiex_ie_types_header *tlv;
	u16 tlv_type, tlv_len;
	int tlv_buf_left;

	if (!priv->media_connected) {
		mwifiex_dbg(priv->adapter, ERROR,
			    "tx_pause event while disconnected; bss_role=%d\n",
			    priv->bss_role);
		return;
	}

	tlv_buf_left = event_skb->len - sizeof(u32);
	tlv = (void *)event_skb->data + sizeof(u32);

	while (tlv_buf_left >= (int)sizeof(struct mwifiex_ie_types_header)) {
		tlv_type = le16_to_cpu(tlv->type);
		tlv_len  = le16_to_cpu(tlv->len);
		if ((sizeof(struct mwifiex_ie_types_header) + tlv_len) >
		    tlv_buf_left) {
			mwifiex_dbg(priv->adapter, ERROR,
				    "wrong tlv: tlvLen=%d, tlvBufLeft=%d\n",
				    tlv_len, tlv_buf_left);
			break;
		}
		if (tlv_type == TLV_TYPE_TX_PAUSE) {
			if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_STA)
				mwifiex_process_sta_tx_pause(priv, tlv);
			else
				mwifiex_process_uap_tx_pause(priv, tlv);
		}

		tlv_buf_left -= sizeof(struct mwifiex_ie_types_header) +
				tlv_len;
		tlv = (void *)((u8 *)tlv + tlv_len +
			       sizeof(struct mwifiex_ie_types_header));
	}

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
			adapter->coex_min_scan_time = le16_to_cpu(scantlv->min_scan_time);
			adapter->coex_max_scan_time = le16_to_cpu(scantlv->max_scan_time);
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

static void
mwifiex_fw_dump_info_event(struct mwifiex_private *priv,
			   struct sk_buff *event_skb)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_fw_dump_header *fw_dump_hdr =
				(void *)adapter->event_body;

	if (adapter->iface_type != MWIFIEX_USB) {
		mwifiex_dbg(adapter, MSG,
			    "event is not on usb interface, ignore it\n");
		return;
	}

	if (!adapter->devdump_data) {
		/* When receive the first event, allocate device dump
		 * buffer, dump driver info.
		 */
		adapter->devdump_data = vzalloc(MWIFIEX_FW_DUMP_SIZE);
		if (!adapter->devdump_data) {
			mwifiex_dbg(adapter, ERROR,
				    "vzalloc devdump data failure!\n");
			return;
		}

		mwifiex_drv_info_dump(adapter);

		/* If no proceeded event arrive in 10s, upload device
		 * dump data, this will be useful if the end of
		 * transmission event get lost, in this cornel case,
		 * user would still get partial of the dump.
		 */
		mod_timer(&adapter->devdump_timer,
			  jiffies + msecs_to_jiffies(MWIFIEX_TIMER_10S));
	}

	/* Overflow check */
	if (adapter->devdump_len + event_skb->len >= MWIFIEX_FW_DUMP_SIZE)
		goto upload_dump;

	memmove(adapter->devdump_data + adapter->devdump_len,
		adapter->event_skb->data, event_skb->len);
	adapter->devdump_len += event_skb->len;

	if (le16_to_cpu(fw_dump_hdr->type == FW_DUMP_INFO_ENDED)) {
		mwifiex_dbg(adapter, MSG,
			    "receive end of transmission flag event!\n");
		goto upload_dump;
	}
	return;

upload_dump:
	del_timer_sync(&adapter->devdump_timer);
	mwifiex_upload_device_dump(adapter);
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
 *      - EVENT_IBSS_STA_CONNECT
 *      - EVENT_IBSS_STA_DISCONNECT
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
 *      - EVENT_FW_DUMP_INFO
 */
int mwifiex_process_sta_event(struct mwifiex_private *priv)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	int ret = 0, i;
	u32 eventcause = adapter->event_cause;
	u16 ctrl, reason_code;
	u8 ibss_sta_addr[ETH_ALEN];
	struct mwifiex_sta_node *sta_ptr;

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
				get_unaligned_le16(adapter->event_body);
			mwifiex_reset_connect_state(priv, reason_code, true);
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
				get_unaligned_le16(adapter->event_body);
			mwifiex_reset_connect_state(priv, reason_code, true);
		}
		break;

	case EVENT_LINK_LOST:
		mwifiex_dbg(adapter, EVENT, "event: Link lost\n");
		adapter->dbg.num_event_link_lost++;
		if (priv->media_connected) {
			reason_code =
				get_unaligned_le16(adapter->event_body);
			mwifiex_reset_connect_state(priv, reason_code, true);
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
		    (priv->port_open ||
		     (priv->bss_mode == NL80211_IFTYPE_ADHOC)) &&
		    priv->media_connected && adapter->sleep_period.period) {
			adapter->pps_uapsd_mode = true;
			mwifiex_dbg(adapter, EVENT,
				    "event: PPS/UAPSD mode activated\n");
		}
		adapter->tx_lock_flag = false;
		if (adapter->pps_uapsd_mode && adapter->gen_null_pkt) {
			if (mwifiex_check_last_packet_indication(priv)) {
				if (adapter->data_sent ||
				    (adapter->if_ops.is_port_ready &&
				     !adapter->if_ops.is_port_ready(priv))) {
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

	case EVENT_BG_SCAN_STOPPED:
		dev_dbg(adapter->dev, "event: BGS_STOPPED\n");
		cfg80211_sched_scan_stopped(priv->wdev.wiphy, 0);
		if (priv->sched_scanning)
			priv->sched_scanning = false;
		break;

	case EVENT_PORT_RELEASE:
		mwifiex_dbg(adapter, EVENT, "event: PORT RELEASE\n");
		priv->port_open = true;
		break;

	case EVENT_EXT_SCAN_REPORT:
		mwifiex_dbg(adapter, EVENT, "event: EXT_SCAN Report\n");
		/* We intend to skip this event during suspend, but handle
		 * it in interface disabled case
		 */
		if (adapter->ext_scan && (!priv->scan_aborting ||
					  !netif_running(priv->netdev)))
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
					 0, GFP_KERNEL);
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
					 0, GFP_KERNEL);
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
	case EVENT_IBSS_STA_CONNECT:
		ether_addr_copy(ibss_sta_addr, adapter->event_body + 2);
		mwifiex_dbg(adapter, EVENT, "event: IBSS_STA_CONNECT %pM\n",
			    ibss_sta_addr);
		sta_ptr = mwifiex_add_sta_entry(priv, ibss_sta_addr);
		if (sta_ptr && adapter->adhoc_11n_enabled) {
			mwifiex_check_ibss_peer_capabilities(priv, sta_ptr,
							     adapter->event_skb);
			if (sta_ptr->is_11n_enabled)
				for (i = 0; i < MAX_NUM_TID; i++)
					sta_ptr->ampdu_sta[i] =
					priv->aggr_prio_tbl[i].ampdu_user;
			else
				for (i = 0; i < MAX_NUM_TID; i++)
					sta_ptr->ampdu_sta[i] =
						BA_STREAM_NOT_ALLOWED;
			memset(sta_ptr->rx_seq, 0xff, sizeof(sta_ptr->rx_seq));
		}

		break;
	case EVENT_IBSS_STA_DISCONNECT:
		ether_addr_copy(ibss_sta_addr, adapter->event_body + 2);
		mwifiex_dbg(adapter, EVENT, "event: IBSS_STA_DISCONNECT %pM\n",
			    ibss_sta_addr);
		sta_ptr = mwifiex_get_sta_entry(priv, ibss_sta_addr);
		if (sta_ptr && sta_ptr->is_11n_enabled) {
			mwifiex_11n_del_rx_reorder_tbl_by_ta(priv,
							     ibss_sta_addr);
			mwifiex_del_tx_ba_stream_tbl_by_ra(priv, ibss_sta_addr);
		}
		mwifiex_wmm_del_peer_ra_list(priv, ibss_sta_addr);
		mwifiex_del_sta_entry(priv, ibss_sta_addr);
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
		ctrl = get_unaligned_le16(adapter->event_body);
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

	case EVENT_TX_DATA_PAUSE:
		mwifiex_dbg(adapter, EVENT, "event: TX DATA PAUSE\n");
		mwifiex_process_tx_pause_event(priv, adapter->event_skb);
		break;

	case EVENT_MULTI_CHAN_INFO:
		mwifiex_dbg(adapter, EVENT, "event: multi-chan info\n");
		mwifiex_process_multi_chan_event(priv, adapter->event_skb);
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
	case EVENT_RXBA_SYNC:
		dev_dbg(adapter->dev, "EVENT: RXBA_SYNC\n");
		mwifiex_11n_rxba_sync_event(priv, adapter->event_body,
					    adapter->event_skb->len -
					    sizeof(eventcause));
		break;
	case EVENT_FW_DUMP_INFO:
		mwifiex_dbg(adapter, EVENT, "event: firmware debug info\n");
		mwifiex_fw_dump_info_event(priv, adapter->event_skb);
		break;
	/* Debugging event; not used, but let's not print an ERROR for it. */
	case EVENT_UNKNOWN_DEBUG:
		mwifiex_dbg(adapter, EVENT, "event: debug\n");
		break;
	default:
		mwifiex_dbg(adapter, ERROR, "event: unknown event id: %#x\n",
			    eventcause);
		break;
	}

	return ret;
}
