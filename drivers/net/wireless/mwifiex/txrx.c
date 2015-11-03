/*
 * Marvell Wireless LAN device driver: generic TX/RX data handling
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

/*
 * This function processes the received buffer.
 *
 * Main responsibility of this function is to parse the RxPD to
 * identify the correct interface this packet is headed for and
 * forwarding it to the associated handling function, where the
 * packet will be further processed and sent to kernel/upper layer
 * if required.
 */
int mwifiex_handle_rx_packet(struct mwifiex_adapter *adapter,
			     struct sk_buff *skb)
{
	struct mwifiex_private *priv =
		mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);
	struct rxpd *local_rx_pd;
	struct mwifiex_rxinfo *rx_info = MWIFIEX_SKB_RXCB(skb);
	int ret;

	local_rx_pd = (struct rxpd *) (skb->data);
	/* Get the BSS number from rxpd, get corresponding priv */
	priv = mwifiex_get_priv_by_id(adapter, local_rx_pd->bss_num &
				      BSS_NUM_MASK, local_rx_pd->bss_type);
	if (!priv)
		priv = mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);

	if (!priv) {
		mwifiex_dbg(adapter, ERROR,
			    "data: priv not found. Drop RX packet\n");
		dev_kfree_skb_any(skb);
		return -1;
	}

	mwifiex_dbg_dump(adapter, DAT_D, "rx pkt:", skb->data,
			 min_t(size_t, skb->len, DEBUG_DUMP_DATA_MAX_LEN));

	memset(rx_info, 0, sizeof(*rx_info));
	rx_info->bss_num = priv->bss_num;
	rx_info->bss_type = priv->bss_type;

	if (priv->bss_role == MWIFIEX_BSS_ROLE_UAP)
		ret = mwifiex_process_uap_rx_packet(priv, skb);
	else
		ret = mwifiex_process_sta_rx_packet(priv, skb);

	return ret;
}
EXPORT_SYMBOL_GPL(mwifiex_handle_rx_packet);

/*
 * This function sends a packet to device.
 *
 * It processes the packet to add the TxPD, checks condition and
 * sends the processed packet to firmware for transmission.
 *
 * On successful completion, the function calls the completion callback
 * and logs the time.
 */
int mwifiex_process_tx(struct mwifiex_private *priv, struct sk_buff *skb,
		       struct mwifiex_tx_param *tx_param)
{
	int hroom, ret = -1;
	struct mwifiex_adapter *adapter = priv->adapter;
	u8 *head_ptr;
	struct txpd *local_tx_pd = NULL;
	struct mwifiex_sta_node *dest_node;
	struct ethhdr *hdr = (void *)skb->data;

	hroom = (adapter->iface_type == MWIFIEX_USB) ? 0 : INTF_HEADER_LEN;

	if (priv->bss_role == MWIFIEX_BSS_ROLE_UAP) {
		dest_node = mwifiex_get_sta_entry(priv, hdr->h_dest);
		if (dest_node) {
			dest_node->stats.tx_bytes += skb->len;
			dest_node->stats.tx_packets++;
		}

		head_ptr = mwifiex_process_uap_txpd(priv, skb);
	} else {
		head_ptr = mwifiex_process_sta_txpd(priv, skb);
	}

	if ((adapter->data_sent || adapter->tx_lock_flag) && head_ptr) {
		skb_queue_tail(&adapter->tx_data_q, skb);
		atomic_inc(&adapter->tx_queued);
		return 0;
	}

	if (head_ptr) {
		if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_STA)
			local_tx_pd = (struct txpd *)(head_ptr + hroom);
		if (adapter->iface_type == MWIFIEX_USB) {
			adapter->data_sent = true;
			ret = adapter->if_ops.host_to_card(adapter,
							   MWIFIEX_USB_EP_DATA,
							   skb, NULL);
		} else {
			ret = adapter->if_ops.host_to_card(adapter,
							   MWIFIEX_TYPE_DATA,
							   skb, tx_param);
		}
	}
	mwifiex_dbg_dump(adapter, DAT_D, "tx pkt:", skb->data,
			 min_t(size_t, skb->len, DEBUG_DUMP_DATA_MAX_LEN));

	switch (ret) {
	case -ENOSR:
		mwifiex_dbg(adapter, ERROR, "data: -ENOSR is returned\n");
		break;
	case -EBUSY:
		if ((GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_STA) &&
		    (adapter->pps_uapsd_mode) && (adapter->tx_lock_flag)) {
				priv->adapter->tx_lock_flag = false;
				if (local_tx_pd)
					local_tx_pd->flags = 0;
		}
		mwifiex_dbg(adapter, ERROR, "data: -EBUSY is returned\n");
		break;
	case -1:
		if (adapter->iface_type != MWIFIEX_PCIE)
			adapter->data_sent = false;
		mwifiex_dbg(adapter, ERROR,
			    "mwifiex_write_data_async failed: 0x%X\n",
			    ret);
		adapter->dbg.num_tx_host_to_card_failure++;
		mwifiex_write_data_complete(adapter, skb, 0, ret);
		break;
	case -EINPROGRESS:
		if (adapter->iface_type != MWIFIEX_PCIE)
			adapter->data_sent = false;
		break;
	case 0:
		mwifiex_write_data_complete(adapter, skb, 0, ret);
		break;
	default:
		break;
	}

	return ret;
}

static int mwifiex_host_to_card(struct mwifiex_adapter *adapter,
				struct sk_buff *skb,
				struct mwifiex_tx_param *tx_param)
{
	struct txpd *local_tx_pd = NULL;
	u8 *head_ptr = skb->data;
	int ret = 0;
	struct mwifiex_private *priv;
	struct mwifiex_txinfo *tx_info;

	tx_info = MWIFIEX_SKB_TXCB(skb);
	priv = mwifiex_get_priv_by_id(adapter, tx_info->bss_num,
				      tx_info->bss_type);
	if (!priv) {
		mwifiex_dbg(adapter, ERROR,
			    "data: priv not found. Drop TX packet\n");
		adapter->dbg.num_tx_host_to_card_failure++;
		mwifiex_write_data_complete(adapter, skb, 0, 0);
		return ret;
	}
	if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_STA) {
		if (adapter->iface_type == MWIFIEX_USB)
			local_tx_pd = (struct txpd *)head_ptr;
		else
			local_tx_pd = (struct txpd *) (head_ptr +
				INTF_HEADER_LEN);
	}

	if (adapter->iface_type == MWIFIEX_USB) {
		adapter->data_sent = true;
		ret = adapter->if_ops.host_to_card(adapter,
						   MWIFIEX_USB_EP_DATA,
						   skb, NULL);
	} else {
		ret = adapter->if_ops.host_to_card(adapter,
						   MWIFIEX_TYPE_DATA,
						   skb, tx_param);
	}
	switch (ret) {
	case -ENOSR:
		mwifiex_dbg(adapter, ERROR, "data: -ENOSR is returned\n");
		break;
	case -EBUSY:
		if ((GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_STA) &&
		    (adapter->pps_uapsd_mode) &&
		    (adapter->tx_lock_flag)) {
			priv->adapter->tx_lock_flag = false;
			if (local_tx_pd)
				local_tx_pd->flags = 0;
		}
		skb_queue_head(&adapter->tx_data_q, skb);
		if (tx_info->flags & MWIFIEX_BUF_FLAG_AGGR_PKT)
			atomic_add(tx_info->aggr_num, &adapter->tx_queued);
		else
			atomic_inc(&adapter->tx_queued);
		mwifiex_dbg(adapter, ERROR, "data: -EBUSY is returned\n");
		break;
	case -1:
		if (adapter->iface_type != MWIFIEX_PCIE)
			adapter->data_sent = false;
		mwifiex_dbg(adapter, ERROR,
			    "mwifiex_write_data_async failed: 0x%X\n", ret);
		adapter->dbg.num_tx_host_to_card_failure++;
		mwifiex_write_data_complete(adapter, skb, 0, ret);
		break;
	case -EINPROGRESS:
		if (adapter->iface_type != MWIFIEX_PCIE)
			adapter->data_sent = false;
		break;
	case 0:
		mwifiex_write_data_complete(adapter, skb, 0, ret);
		break;
	default:
		break;
	}
	return ret;
}

static int
mwifiex_dequeue_tx_queue(struct mwifiex_adapter *adapter)
{
	struct sk_buff *skb, *skb_next;
	struct mwifiex_txinfo *tx_info;
	struct mwifiex_tx_param tx_param;

	skb = skb_dequeue(&adapter->tx_data_q);
	if (!skb)
		return -1;

	tx_info = MWIFIEX_SKB_TXCB(skb);
	if (tx_info->flags & MWIFIEX_BUF_FLAG_AGGR_PKT)
		atomic_sub(tx_info->aggr_num, &adapter->tx_queued);
	else
		atomic_dec(&adapter->tx_queued);

	if (!skb_queue_empty(&adapter->tx_data_q))
		skb_next = skb_peek(&adapter->tx_data_q);
	else
		skb_next = NULL;
	tx_param.next_pkt_len = ((skb_next) ? skb_next->len : 0);
	if (!tx_param.next_pkt_len) {
		if (!mwifiex_wmm_lists_empty(adapter))
			tx_param.next_pkt_len = 1;
	}
	return mwifiex_host_to_card(adapter, skb, &tx_param);
}

void
mwifiex_process_tx_queue(struct mwifiex_adapter *adapter)
{
	do {
		if (adapter->data_sent || adapter->tx_lock_flag)
			break;
		if (mwifiex_dequeue_tx_queue(adapter))
			break;
	} while (!skb_queue_empty(&adapter->tx_data_q));
}

/*
 * Packet send completion callback handler.
 *
 * It either frees the buffer directly or forwards it to another
 * completion callback which checks conditions, updates statistics,
 * wakes up stalled traffic queue if required, and then frees the buffer.
 */
int mwifiex_write_data_complete(struct mwifiex_adapter *adapter,
				struct sk_buff *skb, int aggr, int status)
{
	struct mwifiex_private *priv;
	struct mwifiex_txinfo *tx_info;
	struct netdev_queue *txq;
	int index;

	if (!skb)
		return 0;

	tx_info = MWIFIEX_SKB_TXCB(skb);
	priv = mwifiex_get_priv_by_id(adapter, tx_info->bss_num,
				      tx_info->bss_type);
	if (!priv)
		goto done;

	if (adapter->iface_type == MWIFIEX_USB)
		adapter->data_sent = false;

	mwifiex_set_trans_start(priv->netdev);
	if (!status) {
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += tx_info->pkt_len;
		if (priv->tx_timeout_cnt)
			priv->tx_timeout_cnt = 0;
	} else {
		priv->stats.tx_errors++;
	}

	if (tx_info->flags & MWIFIEX_BUF_FLAG_BRIDGED_PKT)
		atomic_dec_return(&adapter->pending_bridged_pkts);

	if (tx_info->flags & MWIFIEX_BUF_FLAG_AGGR_PKT)
		goto done;

	if (aggr)
		/* For skb_aggr, do not wake up tx queue */
		goto done;

	atomic_dec(&adapter->tx_pending);

	index = mwifiex_1d_to_wmm_queue[skb->priority];
	if (atomic_dec_return(&priv->wmm_tx_pending[index]) < LOW_TX_PENDING) {
		txq = netdev_get_tx_queue(priv->netdev, index);
		if (netif_tx_queue_stopped(txq)) {
			netif_tx_wake_queue(txq);
			mwifiex_dbg(adapter, DATA, "wake queue: %d\n", index);
		}
	}
done:
	dev_kfree_skb_any(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(mwifiex_write_data_complete);

void mwifiex_parse_tx_status_event(struct mwifiex_private *priv,
				   void *event_body)
{
	struct tx_status_event *tx_status = (void *)priv->adapter->event_body;
	struct sk_buff *ack_skb;
	unsigned long flags;
	struct mwifiex_txinfo *tx_info;

	if (!tx_status->tx_token_id)
		return;

	spin_lock_irqsave(&priv->ack_status_lock, flags);
	ack_skb = idr_find(&priv->ack_status_frames, tx_status->tx_token_id);
	if (ack_skb)
		idr_remove(&priv->ack_status_frames, tx_status->tx_token_id);
	spin_unlock_irqrestore(&priv->ack_status_lock, flags);

	if (ack_skb) {
		tx_info = MWIFIEX_SKB_TXCB(ack_skb);

		if (tx_info->flags & MWIFIEX_BUF_FLAG_EAPOL_TX_STATUS) {
			/* consumes ack_skb */
			skb_complete_wifi_ack(ack_skb, !tx_status->status);
		} else {
			/* Remove broadcast address which was added by driver */
			memmove(ack_skb->data +
				sizeof(struct ieee80211_hdr_3addr) +
				MWIFIEX_MGMT_FRAME_HEADER_SIZE + sizeof(u16),
				ack_skb->data +
				sizeof(struct ieee80211_hdr_3addr) +
				MWIFIEX_MGMT_FRAME_HEADER_SIZE + sizeof(u16) +
				ETH_ALEN, ack_skb->len -
				(sizeof(struct ieee80211_hdr_3addr) +
				MWIFIEX_MGMT_FRAME_HEADER_SIZE + sizeof(u16) +
				ETH_ALEN));
			ack_skb->len = ack_skb->len - ETH_ALEN;
			/* Remove driver's proprietary header including 2 bytes
			 * of packet length and pass actual management frame buffer
			 * to cfg80211.
			 */
			cfg80211_mgmt_tx_status(&priv->wdev, tx_info->cookie,
						ack_skb->data +
						MWIFIEX_MGMT_FRAME_HEADER_SIZE +
						sizeof(u16), ack_skb->len -
						(MWIFIEX_MGMT_FRAME_HEADER_SIZE
						 + sizeof(u16)),
						!tx_status->status, GFP_ATOMIC);
			dev_kfree_skb_any(ack_skb);
		}
	}
}
