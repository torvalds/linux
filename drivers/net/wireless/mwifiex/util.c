/*
 * Marvell Wireless LAN device driver: utility functions
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

static struct mwifiex_debug_data items[] = {
	{"debug_mask", item_size(debug_mask),
	 item_addr(debug_mask), 1},
	{"int_counter", item_size(int_counter),
	 item_addr(int_counter), 1},
	{"wmm_ac_vo", item_size(packets_out[WMM_AC_VO]),
	 item_addr(packets_out[WMM_AC_VO]), 1},
	{"wmm_ac_vi", item_size(packets_out[WMM_AC_VI]),
	 item_addr(packets_out[WMM_AC_VI]), 1},
	{"wmm_ac_be", item_size(packets_out[WMM_AC_BE]),
	 item_addr(packets_out[WMM_AC_BE]), 1},
	{"wmm_ac_bk", item_size(packets_out[WMM_AC_BK]),
	 item_addr(packets_out[WMM_AC_BK]), 1},
	{"tx_buf_size", item_size(tx_buf_size),
	 item_addr(tx_buf_size), 1},
	{"curr_tx_buf_size", item_size(curr_tx_buf_size),
	 item_addr(curr_tx_buf_size), 1},
	{"ps_mode", item_size(ps_mode),
	 item_addr(ps_mode), 1},
	{"ps_state", item_size(ps_state),
	 item_addr(ps_state), 1},
	{"is_deep_sleep", item_size(is_deep_sleep),
	 item_addr(is_deep_sleep), 1},
	{"wakeup_dev_req", item_size(pm_wakeup_card_req),
	 item_addr(pm_wakeup_card_req), 1},
	{"wakeup_tries", item_size(pm_wakeup_fw_try),
	 item_addr(pm_wakeup_fw_try), 1},
	{"hs_configured", item_size(is_hs_configured),
	 item_addr(is_hs_configured), 1},
	{"hs_activated", item_size(hs_activated),
	 item_addr(hs_activated), 1},
	{"num_tx_timeout", item_size(num_tx_timeout),
	 item_addr(num_tx_timeout), 1},
	{"is_cmd_timedout", item_size(is_cmd_timedout),
	 item_addr(is_cmd_timedout), 1},
	{"timeout_cmd_id", item_size(timeout_cmd_id),
	 item_addr(timeout_cmd_id), 1},
	{"timeout_cmd_act", item_size(timeout_cmd_act),
	 item_addr(timeout_cmd_act), 1},
	{"last_cmd_id", item_size(last_cmd_id),
	 item_addr(last_cmd_id), DBG_CMD_NUM},
	{"last_cmd_act", item_size(last_cmd_act),
	 item_addr(last_cmd_act), DBG_CMD_NUM},
	{"last_cmd_index", item_size(last_cmd_index),
	 item_addr(last_cmd_index), 1},
	{"last_cmd_resp_id", item_size(last_cmd_resp_id),
	 item_addr(last_cmd_resp_id), DBG_CMD_NUM},
	{"last_cmd_resp_index", item_size(last_cmd_resp_index),
	 item_addr(last_cmd_resp_index), 1},
	{"last_event", item_size(last_event),
	 item_addr(last_event), DBG_CMD_NUM},
	{"last_event_index", item_size(last_event_index),
	 item_addr(last_event_index), 1},
	{"num_cmd_h2c_fail", item_size(num_cmd_host_to_card_failure),
	 item_addr(num_cmd_host_to_card_failure), 1},
	{"num_cmd_sleep_cfm_fail",
	 item_size(num_cmd_sleep_cfm_host_to_card_failure),
	 item_addr(num_cmd_sleep_cfm_host_to_card_failure), 1},
	{"num_tx_h2c_fail", item_size(num_tx_host_to_card_failure),
	 item_addr(num_tx_host_to_card_failure), 1},
	{"num_evt_deauth", item_size(num_event_deauth),
	 item_addr(num_event_deauth), 1},
	{"num_evt_disassoc", item_size(num_event_disassoc),
	 item_addr(num_event_disassoc), 1},
	{"num_evt_link_lost", item_size(num_event_link_lost),
	 item_addr(num_event_link_lost), 1},
	{"num_cmd_deauth", item_size(num_cmd_deauth),
	 item_addr(num_cmd_deauth), 1},
	{"num_cmd_assoc_ok", item_size(num_cmd_assoc_success),
	 item_addr(num_cmd_assoc_success), 1},
	{"num_cmd_assoc_fail", item_size(num_cmd_assoc_failure),
	 item_addr(num_cmd_assoc_failure), 1},
	{"cmd_sent", item_size(cmd_sent),
	 item_addr(cmd_sent), 1},
	{"data_sent", item_size(data_sent),
	 item_addr(data_sent), 1},
	{"cmd_resp_received", item_size(cmd_resp_received),
	 item_addr(cmd_resp_received), 1},
	{"event_received", item_size(event_received),
	 item_addr(event_received), 1},

	/* variables defined in struct mwifiex_adapter */
	{"cmd_pending", adapter_item_size(cmd_pending),
	 adapter_item_addr(cmd_pending), 1},
	{"tx_pending", adapter_item_size(tx_pending),
	 adapter_item_addr(tx_pending), 1},
	{"rx_pending", adapter_item_size(rx_pending),
	 adapter_item_addr(rx_pending), 1},
};

static int num_of_items = ARRAY_SIZE(items);

/*
 * Firmware initialization complete callback handler.
 *
 * This function wakes up the function waiting on the init
 * wait queue for the firmware initialization to complete.
 */
int mwifiex_init_fw_complete(struct mwifiex_adapter *adapter)
{

	adapter->init_wait_q_woken = true;
	wake_up_interruptible(&adapter->init_wait_q);
	return 0;
}

/*
 * Firmware shutdown complete callback handler.
 *
 * This function sets the hardware status to not ready and wakes up
 * the function waiting on the init wait queue for the firmware
 * shutdown to complete.
 */
int mwifiex_shutdown_fw_complete(struct mwifiex_adapter *adapter)
{
	adapter->hw_status = MWIFIEX_HW_STATUS_NOT_READY;
	adapter->init_wait_q_woken = true;
	wake_up_interruptible(&adapter->init_wait_q);
	return 0;
}

/*
 * This function sends init/shutdown command
 * to firmware.
 */
int mwifiex_init_shutdown_fw(struct mwifiex_private *priv,
			     u32 func_init_shutdown)
{
	u16 cmd;

	if (func_init_shutdown == MWIFIEX_FUNC_INIT) {
		cmd = HostCmd_CMD_FUNC_INIT;
	} else if (func_init_shutdown == MWIFIEX_FUNC_SHUTDOWN) {
		cmd = HostCmd_CMD_FUNC_SHUTDOWN;
	} else {
		mwifiex_dbg(priv->adapter, ERROR,
			    "unsupported parameter\n");
		return -1;
	}

	return mwifiex_send_cmd(priv, cmd, HostCmd_ACT_GEN_SET, 0, NULL, true);
}
EXPORT_SYMBOL_GPL(mwifiex_init_shutdown_fw);

/*
 * IOCTL request handler to set/get debug information.
 *
 * This function collates/sets the information from/to different driver
 * structures.
 */
int mwifiex_get_debug_info(struct mwifiex_private *priv,
			   struct mwifiex_debug_info *info)
{
	struct mwifiex_adapter *adapter = priv->adapter;

	if (info) {
		info->debug_mask = adapter->debug_mask;
		memcpy(info->packets_out,
		       priv->wmm.packets_out,
		       sizeof(priv->wmm.packets_out));
		info->curr_tx_buf_size = (u32) adapter->curr_tx_buf_size;
		info->tx_buf_size = (u32) adapter->tx_buf_size;
		info->rx_tbl_num = mwifiex_get_rx_reorder_tbl(priv,
							      info->rx_tbl);
		info->tx_tbl_num = mwifiex_get_tx_ba_stream_tbl(priv,
								info->tx_tbl);
		info->tdls_peer_num = mwifiex_get_tdls_list(priv,
							    info->tdls_list);
		info->ps_mode = adapter->ps_mode;
		info->ps_state = adapter->ps_state;
		info->is_deep_sleep = adapter->is_deep_sleep;
		info->pm_wakeup_card_req = adapter->pm_wakeup_card_req;
		info->pm_wakeup_fw_try = adapter->pm_wakeup_fw_try;
		info->is_hs_configured = adapter->is_hs_configured;
		info->hs_activated = adapter->hs_activated;
		info->is_cmd_timedout = adapter->is_cmd_timedout;
		info->num_cmd_host_to_card_failure
				= adapter->dbg.num_cmd_host_to_card_failure;
		info->num_cmd_sleep_cfm_host_to_card_failure
			= adapter->dbg.num_cmd_sleep_cfm_host_to_card_failure;
		info->num_tx_host_to_card_failure
				= adapter->dbg.num_tx_host_to_card_failure;
		info->num_event_deauth = adapter->dbg.num_event_deauth;
		info->num_event_disassoc = adapter->dbg.num_event_disassoc;
		info->num_event_link_lost = adapter->dbg.num_event_link_lost;
		info->num_cmd_deauth = adapter->dbg.num_cmd_deauth;
		info->num_cmd_assoc_success =
					adapter->dbg.num_cmd_assoc_success;
		info->num_cmd_assoc_failure =
					adapter->dbg.num_cmd_assoc_failure;
		info->num_tx_timeout = adapter->dbg.num_tx_timeout;
		info->timeout_cmd_id = adapter->dbg.timeout_cmd_id;
		info->timeout_cmd_act = adapter->dbg.timeout_cmd_act;
		memcpy(info->last_cmd_id, adapter->dbg.last_cmd_id,
		       sizeof(adapter->dbg.last_cmd_id));
		memcpy(info->last_cmd_act, adapter->dbg.last_cmd_act,
		       sizeof(adapter->dbg.last_cmd_act));
		info->last_cmd_index = adapter->dbg.last_cmd_index;
		memcpy(info->last_cmd_resp_id, adapter->dbg.last_cmd_resp_id,
		       sizeof(adapter->dbg.last_cmd_resp_id));
		info->last_cmd_resp_index = adapter->dbg.last_cmd_resp_index;
		memcpy(info->last_event, adapter->dbg.last_event,
		       sizeof(adapter->dbg.last_event));
		info->last_event_index = adapter->dbg.last_event_index;
		info->data_sent = adapter->data_sent;
		info->cmd_sent = adapter->cmd_sent;
		info->cmd_resp_received = adapter->cmd_resp_received;
	}

	return 0;
}

int mwifiex_debug_info_to_buffer(struct mwifiex_private *priv, char *buf,
				 struct mwifiex_debug_info *info)
{
	char *p = buf;
	struct mwifiex_debug_data *d = &items[0];
	size_t size, addr;
	long val;
	int i, j;

	if (!info)
		return 0;

	for (i = 0; i < num_of_items; i++) {
		p += sprintf(p, "%s=", d[i].name);

		size = d[i].size / d[i].num;

		if (i < (num_of_items - 3))
			addr = d[i].addr + (size_t)info;
		else /* The last 3 items are struct mwifiex_adapter variables */
			addr = d[i].addr + (size_t)priv->adapter;

		for (j = 0; j < d[i].num; j++) {
			switch (size) {
			case 1:
				val = *((u8 *)addr);
				break;
			case 2:
				val = *((u16 *)addr);
				break;
			case 4:
				val = *((u32 *)addr);
				break;
			case 8:
				val = *((long long *)addr);
				break;
			default:
				val = -1;
				break;
			}

			p += sprintf(p, "%#lx ", val);
			addr += size;
		}

		p += sprintf(p, "\n");
	}

	if (info->tx_tbl_num) {
		p += sprintf(p, "Tx BA stream table:\n");
		for (i = 0; i < info->tx_tbl_num; i++)
			p += sprintf(p, "tid = %d, ra = %pM\n",
				     info->tx_tbl[i].tid, info->tx_tbl[i].ra);
	}

	if (info->rx_tbl_num) {
		p += sprintf(p, "Rx reorder table:\n");
		for (i = 0; i < info->rx_tbl_num; i++) {
			p += sprintf(p, "tid = %d, ta = %pM, ",
				     info->rx_tbl[i].tid,
				     info->rx_tbl[i].ta);
			p += sprintf(p, "start_win = %d, ",
				     info->rx_tbl[i].start_win);
			p += sprintf(p, "win_size = %d, buffer: ",
				     info->rx_tbl[i].win_size);

			for (j = 0; j < info->rx_tbl[i].win_size; j++)
				p += sprintf(p, "%c ",
					     info->rx_tbl[i].buffer[j] ?
					     '1' : '0');

			p += sprintf(p, "\n");
		}
	}

	if (info->tdls_peer_num) {
		p += sprintf(p, "TDLS peer table:\n");
		for (i = 0; i < info->tdls_peer_num; i++) {
			p += sprintf(p, "peer = %pM",
				     info->tdls_list[i].peer_addr);
			p += sprintf(p, "\n");
		}
	}

	return p - buf;
}

static int
mwifiex_parse_mgmt_packet(struct mwifiex_private *priv, u8 *payload, u16 len,
			  struct rxpd *rx_pd)
{
	u16 stype;
	u8 category, action_code, *addr2;
	struct ieee80211_hdr *ieee_hdr = (void *)payload;

	stype = (le16_to_cpu(ieee_hdr->frame_control) & IEEE80211_FCTL_STYPE);

	switch (stype) {
	case IEEE80211_STYPE_ACTION:
		category = *(payload + sizeof(struct ieee80211_hdr));
		switch (category) {
		case WLAN_CATEGORY_PUBLIC:
			action_code = *(payload + sizeof(struct ieee80211_hdr)
					+ 1);
			if (action_code == WLAN_PUB_ACTION_TDLS_DISCOVER_RES) {
				addr2 = ieee_hdr->addr2;
				mwifiex_dbg(priv->adapter, INFO,
					    "TDLS discovery response %pM nf=%d, snr=%d\n",
					    addr2, rx_pd->nf, rx_pd->snr);
				mwifiex_auto_tdls_update_peer_signal(priv,
								     addr2,
								     rx_pd->snr,
								     rx_pd->nf);
			}
			break;
		case WLAN_CATEGORY_BACK:
			/*we dont indicate BACK action frames to cfg80211*/
			mwifiex_dbg(priv->adapter, INFO,
				    "drop BACK action frames");
			return -1;
		default:
			mwifiex_dbg(priv->adapter, INFO,
				    "unknown public action frame category %d\n",
				    category);
		}
	default:
		mwifiex_dbg(priv->adapter, INFO,
		    "unknown mgmt frame subtype %#x\n", stype);
		return 0;
	}

	return 0;
}
/*
 * This function processes the received management packet and send it
 * to the kernel.
 */
int
mwifiex_process_mgmt_packet(struct mwifiex_private *priv,
			    struct sk_buff *skb)
{
	struct rxpd *rx_pd;
	u16 pkt_len;
	struct ieee80211_hdr *ieee_hdr;

	if (!skb)
		return -1;

	if (!priv->mgmt_frame_mask ||
	    priv->wdev.iftype == NL80211_IFTYPE_UNSPECIFIED) {
		mwifiex_dbg(priv->adapter, ERROR,
			    "do not receive mgmt frames on uninitialized intf");
		return -1;
	}

	rx_pd = (struct rxpd *)skb->data;

	skb_pull(skb, le16_to_cpu(rx_pd->rx_pkt_offset));
	skb_pull(skb, sizeof(pkt_len));

	pkt_len = le16_to_cpu(rx_pd->rx_pkt_length);

	ieee_hdr = (void *)skb->data;
	if (ieee80211_is_mgmt(ieee_hdr->frame_control)) {
		if (mwifiex_parse_mgmt_packet(priv, (u8 *)ieee_hdr,
					      pkt_len, rx_pd))
			return -1;
	}
	/* Remove address4 */
	memmove(skb->data + sizeof(struct ieee80211_hdr_3addr),
		skb->data + sizeof(struct ieee80211_hdr),
		pkt_len - sizeof(struct ieee80211_hdr));

	pkt_len -= ETH_ALEN + sizeof(pkt_len);
	rx_pd->rx_pkt_length = cpu_to_le16(pkt_len);

	cfg80211_rx_mgmt(&priv->wdev, priv->roc_cfg.chan.center_freq,
			 CAL_RSSI(rx_pd->snr, rx_pd->nf), skb->data, pkt_len,
			 0);

	return 0;
}

/*
 * This function processes the received packet before sending it to the
 * kernel.
 *
 * It extracts the SKB from the received buffer and sends it to kernel.
 * In case the received buffer does not contain the data in SKB format,
 * the function creates a blank SKB, fills it with the data from the
 * received buffer and then sends this new SKB to the kernel.
 */
int mwifiex_recv_packet(struct mwifiex_private *priv, struct sk_buff *skb)
{
	struct mwifiex_sta_node *src_node;
	struct ethhdr *p_ethhdr;

	if (!skb)
		return -1;

	priv->stats.rx_bytes += skb->len;
	priv->stats.rx_packets++;

	if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_UAP) {
		p_ethhdr = (void *)skb->data;
		src_node = mwifiex_get_sta_entry(priv, p_ethhdr->h_source);
		if (src_node) {
			src_node->stats.last_rx = jiffies;
			src_node->stats.rx_bytes += skb->len;
			src_node->stats.rx_packets++;
		}
	}

	skb->dev = priv->netdev;
	skb->protocol = eth_type_trans(skb, priv->netdev);
	skb->ip_summed = CHECKSUM_NONE;

	/* This is required only in case of 11n and USB/PCIE as we alloc
	 * a buffer of 4K only if its 11N (to be able to receive 4K
	 * AMSDU packets). In case of SD we allocate buffers based
	 * on the size of packet and hence this is not needed.
	 *
	 * Modifying the truesize here as our allocation for each
	 * skb is 4K but we only receive 2K packets and this cause
	 * the kernel to start dropping packets in case where
	 * application has allocated buffer based on 2K size i.e.
	 * if there a 64K packet received (in IP fragments and
	 * application allocates 64K to receive this packet but
	 * this packet would almost double up because we allocate
	 * each 1.5K fragment in 4K and pass it up. As soon as the
	 * 64K limit hits kernel will start to drop rest of the
	 * fragments. Currently we fail the Filesndl-ht.scr script
	 * for UDP, hence this fix
	 */
	if ((priv->adapter->iface_type == MWIFIEX_USB ||
	     priv->adapter->iface_type == MWIFIEX_PCIE) &&
	    (skb->truesize > MWIFIEX_RX_DATA_BUF_SIZE))
		skb->truesize += (skb->len - MWIFIEX_RX_DATA_BUF_SIZE);

	if (in_interrupt())
		netif_rx(skb);
	else
		netif_rx_ni(skb);

	return 0;
}

/*
 * IOCTL completion callback handler.
 *
 * This function is called when a pending IOCTL is completed.
 *
 * If work queue support is enabled, the function wakes up the
 * corresponding waiting function. Otherwise, it processes the
 * IOCTL response and frees the response buffer.
 */
int mwifiex_complete_cmd(struct mwifiex_adapter *adapter,
			 struct cmd_ctrl_node *cmd_node)
{
	mwifiex_dbg(adapter, CMD,
		    "cmd completed: status=%d\n",
		    adapter->cmd_wait_q.status);

	*(cmd_node->condition) = true;

	if (adapter->cmd_wait_q.status == -ETIMEDOUT)
		mwifiex_dbg(adapter, ERROR, "cmd timeout\n");
	else
		wake_up_interruptible(&adapter->cmd_wait_q.wait);

	return 0;
}

/* This function will return the pointer to station entry in station list
 * table which matches specified mac address.
 * This function should be called after acquiring RA list spinlock.
 * NULL is returned if station entry is not found in associated STA list.
 */
struct mwifiex_sta_node *
mwifiex_get_sta_entry(struct mwifiex_private *priv, const u8 *mac)
{
	struct mwifiex_sta_node *node;

	if (!mac)
		return NULL;

	list_for_each_entry(node, &priv->sta_list, list) {
		if (!memcmp(node->mac_addr, mac, ETH_ALEN))
			return node;
	}

	return NULL;
}

static struct mwifiex_sta_node *
mwifiex_get_tdls_sta_entry(struct mwifiex_private *priv, u8 status)
{
	struct mwifiex_sta_node *node;

	list_for_each_entry(node, &priv->sta_list, list) {
		if (node->tdls_status == status)
			return node;
	}

	return NULL;
}

/* If tdls channel switching is on-going, tx data traffic should be
 * blocked until the switching stage completed.
 */
u8 mwifiex_is_tdls_chan_switching(struct mwifiex_private *priv)
{
	struct mwifiex_sta_node *sta_ptr;

	if (!priv || !ISSUPP_TDLS_ENABLED(priv->adapter->fw_cap_info))
		return false;

	sta_ptr = mwifiex_get_tdls_sta_entry(priv, TDLS_CHAN_SWITCHING);
	if (sta_ptr)
		return true;

	return false;
}

u8 mwifiex_is_tdls_off_chan(struct mwifiex_private *priv)
{
	struct mwifiex_sta_node *sta_ptr;

	if (!priv || !ISSUPP_TDLS_ENABLED(priv->adapter->fw_cap_info))
		return false;

	sta_ptr = mwifiex_get_tdls_sta_entry(priv, TDLS_IN_OFF_CHAN);
	if (sta_ptr)
		return true;

	return false;
}

/* If tdls channel switching is on-going or tdls operate on off-channel,
 * cmd path should be blocked until tdls switched to base-channel.
 */
u8 mwifiex_is_send_cmd_allowed(struct mwifiex_private *priv)
{
	if (!priv || !ISSUPP_TDLS_ENABLED(priv->adapter->fw_cap_info))
		return true;

	if (mwifiex_is_tdls_chan_switching(priv) ||
	    mwifiex_is_tdls_off_chan(priv))
		return false;

	return true;
}

/* This function will add a sta_node entry to associated station list
 * table with the given mac address.
 * If entry exist already, existing entry is returned.
 * If received mac address is NULL, NULL is returned.
 */
struct mwifiex_sta_node *
mwifiex_add_sta_entry(struct mwifiex_private *priv, const u8 *mac)
{
	struct mwifiex_sta_node *node;
	unsigned long flags;

	if (!mac)
		return NULL;

	spin_lock_irqsave(&priv->sta_list_spinlock, flags);
	node = mwifiex_get_sta_entry(priv, mac);
	if (node)
		goto done;

	node = kzalloc(sizeof(*node), GFP_ATOMIC);
	if (!node)
		goto done;

	memcpy(node->mac_addr, mac, ETH_ALEN);
	list_add_tail(&node->list, &priv->sta_list);

done:
	spin_unlock_irqrestore(&priv->sta_list_spinlock, flags);
	return node;
}

/* This function will search for HT IE in association request IEs
 * and set station HT parameters accordingly.
 */
void
mwifiex_set_sta_ht_cap(struct mwifiex_private *priv, const u8 *ies,
		       int ies_len, struct mwifiex_sta_node *node)
{
	struct ieee_types_header *ht_cap_ie;
	const struct ieee80211_ht_cap *ht_cap;

	if (!ies)
		return;

	ht_cap_ie = (void *)cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, ies,
					     ies_len);
	if (ht_cap_ie) {
		ht_cap = (void *)(ht_cap_ie + 1);
		node->is_11n_enabled = 1;
		node->max_amsdu = le16_to_cpu(ht_cap->cap_info) &
				  IEEE80211_HT_CAP_MAX_AMSDU ?
				  MWIFIEX_TX_DATA_BUF_SIZE_8K :
				  MWIFIEX_TX_DATA_BUF_SIZE_4K;
	} else {
		node->is_11n_enabled = 0;
	}

	return;
}

/* This function will delete a station entry from station list */
void mwifiex_del_sta_entry(struct mwifiex_private *priv, const u8 *mac)
{
	struct mwifiex_sta_node *node;
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_list_spinlock, flags);

	node = mwifiex_get_sta_entry(priv, mac);
	if (node) {
		list_del(&node->list);
		kfree(node);
	}

	spin_unlock_irqrestore(&priv->sta_list_spinlock, flags);
	return;
}

/* This function will delete all stations from associated station list. */
void mwifiex_del_all_sta_list(struct mwifiex_private *priv)
{
	struct mwifiex_sta_node *node, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_list_spinlock, flags);

	list_for_each_entry_safe(node, tmp, &priv->sta_list, list) {
		list_del(&node->list);
		kfree(node);
	}

	INIT_LIST_HEAD(&priv->sta_list);
	spin_unlock_irqrestore(&priv->sta_list_spinlock, flags);
	return;
}

/* This function adds histogram data to histogram array*/
void mwifiex_hist_data_add(struct mwifiex_private *priv,
			   u8 rx_rate, s8 snr, s8 nflr)
{
	struct mwifiex_histogram_data *phist_data = priv->hist_data;

	if (atomic_read(&phist_data->num_samples) > MWIFIEX_HIST_MAX_SAMPLES)
		mwifiex_hist_data_reset(priv);
	mwifiex_hist_data_set(priv, rx_rate, snr, nflr);
}

/* function to add histogram record */
void mwifiex_hist_data_set(struct mwifiex_private *priv, u8 rx_rate, s8 snr,
			   s8 nflr)
{
	struct mwifiex_histogram_data *phist_data = priv->hist_data;

	atomic_inc(&phist_data->num_samples);
	atomic_inc(&phist_data->rx_rate[rx_rate]);
	atomic_inc(&phist_data->snr[snr]);
	atomic_inc(&phist_data->noise_flr[128 + nflr]);
	atomic_inc(&phist_data->sig_str[nflr - snr]);
}

/* function to reset histogram data during init/reset */
void mwifiex_hist_data_reset(struct mwifiex_private *priv)
{
	int ix;
	struct mwifiex_histogram_data *phist_data = priv->hist_data;

	atomic_set(&phist_data->num_samples, 0);
	for (ix = 0; ix < MWIFIEX_MAX_AC_RX_RATES; ix++)
		atomic_set(&phist_data->rx_rate[ix], 0);
	for (ix = 0; ix < MWIFIEX_MAX_SNR; ix++)
		atomic_set(&phist_data->snr[ix], 0);
	for (ix = 0; ix < MWIFIEX_MAX_NOISE_FLR; ix++)
		atomic_set(&phist_data->noise_flr[ix], 0);
	for (ix = 0; ix < MWIFIEX_MAX_SIG_STRENGTH; ix++)
		atomic_set(&phist_data->sig_str[ix], 0);
}

void *mwifiex_alloc_dma_align_buf(int rx_len, gfp_t flags)
{
	struct sk_buff *skb;
	int buf_len, pad;

	buf_len = rx_len + MWIFIEX_RX_HEADROOM + MWIFIEX_DMA_ALIGN_SZ;

	skb = __dev_alloc_skb(buf_len, flags);

	if (!skb)
		return NULL;

	skb_reserve(skb, MWIFIEX_RX_HEADROOM);

	pad = MWIFIEX_ALIGN_ADDR(skb->data, MWIFIEX_DMA_ALIGN_SZ) -
	      (long)skb->data;

	skb_reserve(skb, pad);

	return skb;
}
EXPORT_SYMBOL_GPL(mwifiex_alloc_dma_align_buf);
