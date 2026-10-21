// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP Wireless LAN device driver: generic TX/RX data handling
 *
 * Copyright 2011-2024 NXP
 */

#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "wmm.h"

/*
 * Parse the RxPD, select the target interface, and dispatch the packet for
 * handling.
 */
int nxpwifi_handle_rx_packet(struct nxpwifi_adapter *adapter,
			     struct sk_buff *skb)
{
	struct nxpwifi_private *priv =
		nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);
	struct rxpd *local_rx_pd;
	struct nxpwifi_rxinfo *rx_info = NXPWIFI_SKB_RXCB(skb);
	int ret;

	local_rx_pd = (struct rxpd *)(skb->data);
	/* Get the BSS number from rxpd, get corresponding priv */
	priv = nxpwifi_get_priv_by_id(adapter, local_rx_pd->bss_num &
				      BSS_NUM_MASK, local_rx_pd->bss_type);
	if (!priv)
		priv = nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);

	if (!priv) {
		nxpwifi_dbg(adapter, ERROR,
			    "data: priv not found. Drop RX packet\n");
		dev_kfree_skb_any(skb);
		return -EINVAL;
	}

	nxpwifi_dbg_dump(adapter, DAT_D, "rx pkt:", skb->data,
			 min_t(size_t, skb->len, DEBUG_DUMP_DATA_MAX_LEN));

	memset(rx_info, 0, sizeof(*rx_info));
	rx_info->bss_num = priv->bss_num;
	rx_info->bss_type = priv->bss_type;

	if (priv->bss_role == NXPWIFI_BSS_ROLE_UAP)
		ret = nxpwifi_process_uap_rx_packet(priv, skb);
	else
		ret = nxpwifi_process_sta_rx_packet(priv, skb);

	return ret;
}
EXPORT_SYMBOL_GPL(nxpwifi_handle_rx_packet);

/*
 * Add TxPD, validate, send the packet to firmware, then run completion
 * callback.
 */
int nxpwifi_process_tx(struct nxpwifi_private *priv, struct sk_buff *skb,
		       struct nxpwifi_tx_param *tx_param)
{
	int hroom, ret;
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct txpd *local_tx_pd = NULL;
	struct nxpwifi_sta_node *dest_node;
	struct ethhdr *hdr = (void *)skb->data;

	if (unlikely(!skb->len ||
		     skb_headroom(skb) < NXPWIFI_MIN_DATA_HEADER_LEN)) {
		ret = -EINVAL;
		goto out;
	}

	hroom = adapter->intf_hdr_len;

	if (priv->bss_role == NXPWIFI_BSS_ROLE_UAP) {
		rcu_read_lock();
		dest_node = nxpwifi_get_sta_entry(priv, hdr->h_dest);
		if (dest_node) {
			dest_node->stats.tx_bytes += skb->len;
			dest_node->stats.tx_packets++;
		}
		rcu_read_unlock();
		nxpwifi_process_uap_txpd(priv, skb);
	} else {
		nxpwifi_process_sta_txpd(priv, skb);
	}

	if ((adapter->data_sent || adapter->tx_lock_flag)) {
		skb_queue_tail(&adapter->tx_data_q, skb);
		atomic_inc(&adapter->tx_queued);
		return 0;
	}

	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA)
		local_tx_pd = (struct txpd *)(skb->data + hroom);
	ret = adapter->if_ops.host_to_card(adapter,
					   NXPWIFI_TYPE_DATA,
					   skb, tx_param);
	nxpwifi_dbg_dump(adapter, DAT_D, "tx pkt:", skb->data,
			 min_t(size_t, skb->len, DEBUG_DUMP_DATA_MAX_LEN));

out:
	switch (ret) {
	case -ENOSR:
		nxpwifi_dbg(adapter, DATA, "data: -ENOSR is returned\n");
		break;
	case -EBUSY:
		if ((GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA) &&
		    adapter->pps_uapsd_mode && adapter->tx_lock_flag) {
			priv->adapter->tx_lock_flag = false;
			if (local_tx_pd)
				local_tx_pd->flags = 0;
		}
		nxpwifi_dbg(adapter, ERROR, "data: -EBUSY is returned\n");
		break;
	case -EINPROGRESS:
		break;
	case -EINVAL:
		nxpwifi_dbg(adapter, ERROR,
			    "malformed skb (length: %u, headroom: %u)\n",
			    skb->len, skb_headroom(skb));
		fallthrough;
	case 0:
		nxpwifi_write_data_complete(adapter, skb, 0, ret);
		break;
	default:
		nxpwifi_dbg(adapter, ERROR,
			    "nxpwifi_write_data_async failed: 0x%X\n",
			    ret);
		adapter->dbg.num_tx_host_to_card_failure++;
		nxpwifi_write_data_complete(adapter, skb, 0, ret);
		break;
	}

	return ret;
}

static int nxpwifi_host_to_card(struct nxpwifi_adapter *adapter,
				struct sk_buff *skb,
				struct nxpwifi_tx_param *tx_param)
{
	struct txpd *local_tx_pd = NULL;
	u8 *head_ptr = skb->data;
	int ret = 0;
	struct nxpwifi_private *priv;
	struct nxpwifi_txinfo *tx_info;

	tx_info = NXPWIFI_SKB_TXCB(skb);
	priv = nxpwifi_get_priv_by_id(adapter, tx_info->bss_num,
				      tx_info->bss_type);
	if (!priv) {
		nxpwifi_dbg(adapter, ERROR,
			    "data: priv not found. Drop TX packet\n");
		adapter->dbg.num_tx_host_to_card_failure++;
		nxpwifi_write_data_complete(adapter, skb, 0, 0);
		return ret;
	}
	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA)
		local_tx_pd = (struct txpd *)(head_ptr + adapter->intf_hdr_len);

	ret = adapter->if_ops.host_to_card(adapter,
					   NXPWIFI_TYPE_DATA,
					   skb, tx_param);

	switch (ret) {
	case -ENOSR:
		nxpwifi_dbg(adapter, ERROR, "data: -ENOSR is returned\n");
		break;
	case -EBUSY:
		if ((GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA) &&
		    adapter->pps_uapsd_mode &&
		    adapter->tx_lock_flag) {
			priv->adapter->tx_lock_flag = false;
			if (local_tx_pd)
				local_tx_pd->flags = 0;
		}
		skb_queue_head(&adapter->tx_data_q, skb);
		if (tx_info->flags & NXPWIFI_BUF_FLAG_AGGR_PKT)
			atomic_add(tx_info->aggr_num, &adapter->tx_queued);
		else
			atomic_inc(&adapter->tx_queued);
		nxpwifi_dbg(adapter, ERROR, "data: -EBUSY is returned\n");
		break;
	case -EINPROGRESS:
		break;
	case 0:
		nxpwifi_write_data_complete(adapter, skb, 0, ret);
		break;
	default:
		nxpwifi_dbg(adapter, ERROR,
			    "nxpwifi_write_data_async failed: 0x%X\n", ret);
		adapter->dbg.num_tx_host_to_card_failure++;
		nxpwifi_write_data_complete(adapter, skb, 0, ret);
		break;
	}
	return ret;
}

static int
nxpwifi_dequeue_tx_queue(struct nxpwifi_adapter *adapter)
{
	struct sk_buff *skb, *skb_next;
	struct nxpwifi_txinfo *tx_info;
	struct nxpwifi_tx_param tx_param;

	skb = skb_dequeue(&adapter->tx_data_q);
	if (!skb)
		return -ENOMEM;

	tx_info = NXPWIFI_SKB_TXCB(skb);
	if (tx_info->flags & NXPWIFI_BUF_FLAG_AGGR_PKT)
		atomic_sub(tx_info->aggr_num, &adapter->tx_queued);
	else
		atomic_dec(&adapter->tx_queued);

	if (!skb_queue_empty(&adapter->tx_data_q))
		skb_next = skb_peek(&adapter->tx_data_q);
	else
		skb_next = NULL;
	tx_param.next_pkt_len = ((skb_next) ? skb_next->len : 0);
	if (!tx_param.next_pkt_len) {
		if (!nxpwifi_wmm_lists_empty(adapter))
			tx_param.next_pkt_len = 1;
	}
	return nxpwifi_host_to_card(adapter, skb, &tx_param);
}

void
nxpwifi_process_tx_queue(struct nxpwifi_adapter *adapter)
{
	do {
		if (adapter->data_sent || adapter->tx_lock_flag)
			break;
		if (nxpwifi_dequeue_tx_queue(adapter))
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
int nxpwifi_write_data_complete(struct nxpwifi_adapter *adapter,
				struct sk_buff *skb, int aggr, int status)
{
	struct nxpwifi_private *priv;
	struct nxpwifi_txinfo *tx_info;
	struct netdev_queue *txq;
	int index;

	if (!skb)
		return 0;

	tx_info = NXPWIFI_SKB_TXCB(skb);
	priv = nxpwifi_get_priv_by_id(adapter, tx_info->bss_num,
				      tx_info->bss_type);
	if (!priv)
		goto done;

	nxpwifi_set_trans_start(priv->netdev);

	if (tx_info->flags & NXPWIFI_BUF_FLAG_BRIDGED_PKT)
		atomic_dec_return(&adapter->pending_bridged_pkts);

	if (tx_info->flags & NXPWIFI_BUF_FLAG_AGGR_PKT)
		goto done;

	if (!status) {
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += tx_info->pkt_len;
		if (priv->tx_timeout_cnt)
			priv->tx_timeout_cnt = 0;
	} else {
		priv->stats.tx_errors++;
	}

	if (aggr)
		/* For skb_aggr, do not wake up tx queue */
		goto done;

	atomic_dec(&adapter->tx_pending);

	index = nxpwifi_1d_to_wmm_queue[skb->priority];
	if (atomic_dec_return(&priv->wmm_tx_pending[index]) < LOW_TX_PENDING) {
		txq = netdev_get_tx_queue(priv->netdev, index);
		if (netif_tx_queue_stopped(txq)) {
			netif_tx_wake_queue(txq);
			nxpwifi_dbg(adapter, DATA, "wake queue: %d\n", index);
		}
	}
done:
	dev_kfree_skb_any(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(nxpwifi_write_data_complete);

void nxpwifi_parse_tx_status_event(struct nxpwifi_private *priv,
				   void *event_body)
{
	struct tx_status_event *tx_status = (void *)priv->adapter->event_body;
	struct sk_buff *ack_skb;
	struct nxpwifi_txinfo *tx_info;

	if (!tx_status->tx_token_id)
		return;

	spin_lock_bh(&priv->ack_status_lock);
	ack_skb = xa_erase(&priv->ack_status_frames, tx_status->tx_token_id);
	spin_unlock_bh(&priv->ack_status_lock);

	if (ack_skb) {
		tx_info = NXPWIFI_SKB_TXCB(ack_skb);

		if (tx_info->flags & NXPWIFI_BUF_FLAG_EAPOL_TX_STATUS) {
			/* consumes ack_skb */
			skb_complete_wifi_ack(ack_skb, !tx_status->status);
		} else {
			/* Remove broadcast address which was added by driver */
			memmove(ack_skb->data +
				sizeof(struct ieee80211_hdr_3addr) +
				NXPWIFI_MGMT_FRAME_HEADER_SIZE + sizeof(u16),
				ack_skb->data +
				sizeof(struct ieee80211_hdr_3addr) +
				NXPWIFI_MGMT_FRAME_HEADER_SIZE + sizeof(u16) +
				ETH_ALEN, ack_skb->len -
				(sizeof(struct ieee80211_hdr_3addr) +
				NXPWIFI_MGMT_FRAME_HEADER_SIZE + sizeof(u16) +
				ETH_ALEN));
			ack_skb->len = ack_skb->len - ETH_ALEN;
			/*
			 * Remove driver's proprietary header including 2 bytes
			 * of packet length and pass actual management frame buffer
			 * to cfg80211.
			 */
			cfg80211_mgmt_tx_status(&priv->wdev, tx_info->cookie,
						ack_skb->data +
						NXPWIFI_MGMT_FRAME_HEADER_SIZE +
						sizeof(u16), ack_skb->len -
						(NXPWIFI_MGMT_FRAME_HEADER_SIZE
						 + sizeof(u16)),
						!tx_status->status, GFP_ATOMIC);
			dev_kfree_skb_any(ack_skb);
		}
	}
}
