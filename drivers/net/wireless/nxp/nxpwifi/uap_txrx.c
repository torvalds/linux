// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP Wireless LAN device driver: AP TX and RX data handling
 *
 * Copyright 2011-2024 NXP
 */

#include "cfg.h"
#include "main.h"
#include "wmm.h"
#include "11n_aggr.h"
#include "11n_rxreorder.h"

/*
 * Drop bridged pkts from RA list until pending <= low threshold; return true if
 * any.
 */
static bool
nxpwifi_uap_del_tx_pkts_in_ralist(struct nxpwifi_private *priv,
				  struct list_head *ra_list_head,
				  int tid)
{
	struct nxpwifi_ra_list_tbl *ra_list;
	struct sk_buff *skb, *tmp;
	bool pkt_deleted = false;
	struct nxpwifi_txinfo *tx_info;
	struct nxpwifi_adapter *adapter = priv->adapter;

	list_for_each_entry(ra_list, ra_list_head, list) {
		if (skb_queue_empty(&ra_list->skb_head))
			continue;

		skb_queue_walk_safe(&ra_list->skb_head, skb, tmp) {
			tx_info = NXPWIFI_SKB_TXCB(skb);
			if (tx_info->flags & NXPWIFI_BUF_FLAG_BRIDGED_PKT) {
				__skb_unlink(skb, &ra_list->skb_head);
				nxpwifi_write_data_complete(adapter, skb, 0,
							    -1);
				if (ra_list->tx_paused)
					priv->wmm.pkts_paused[tid]--;
				else
					atomic_dec(&priv->wmm.tx_pkts_queued);
				pkt_deleted = true;
			}
			if ((atomic_read(&adapter->pending_bridged_pkts) <=
					     NXPWIFI_BRIDGED_PKTS_THR_LOW))
				break;
		}
	}

	return pkt_deleted;
}

/* Delete bridged pkts from one RA list; rotate index to keep fairness. */
static void nxpwifi_uap_cleanup_tx_queues(struct nxpwifi_private *priv)
{
	struct list_head *ra_list;
	int i;

	spin_lock_bh(&priv->wmm.ra_list_spinlock);

	for (i = 0; i < MAX_NUM_TID; i++, priv->del_list_idx++) {
		if (priv->del_list_idx == MAX_NUM_TID)
			priv->del_list_idx = 0;
		ra_list = &priv->wmm.tid_tbl_ptr[priv->del_list_idx].ra_list;
		if (nxpwifi_uap_del_tx_pkts_in_ralist(priv, ra_list, i)) {
			priv->del_list_idx++;
			break;
		}
	}

	spin_unlock_bh(&priv->wmm.ra_list_spinlock);
}

static void
nxpwifi_uap_queue_bridged_pkt(struct nxpwifi_private *priv,
			      struct sk_buff *skb)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct uap_rxpd *uap_rx_pd;
	struct rx_packet_hdr *rx_pkt_hdr;
	struct sk_buff *new_skb;
	struct nxpwifi_txinfo *tx_info;
	int hdr_chop;
	struct ethhdr *p_ethhdr;
	struct nxpwifi_sta_node *src_node;
	int index;

	uap_rx_pd = (struct uap_rxpd *)(skb->data);
	rx_pkt_hdr = (void *)uap_rx_pd + le16_to_cpu(uap_rx_pd->rx_pkt_offset);

	if ((atomic_read(&adapter->pending_bridged_pkts) >=
					     NXPWIFI_BRIDGED_PKTS_THR_HIGH)) {
		nxpwifi_dbg(adapter, ERROR,
			    "Tx: Bridge packet limit reached. Drop packet!\n");
		kfree_skb(skb);
		nxpwifi_uap_cleanup_tx_queues(priv);
		return;
	}

	if (sizeof(*rx_pkt_hdr) +
	    le16_to_cpu(uap_rx_pd->rx_pkt_offset) > skb->len) {
		priv->stats.rx_dropped++;
		dev_kfree_skb_any(skb);
		return;
	}

	if ((!memcmp(&rx_pkt_hdr->rfc1042_hdr, bridge_tunnel_header,
		     sizeof(bridge_tunnel_header))) ||
	    (!memcmp(&rx_pkt_hdr->rfc1042_hdr, rfc1042_header,
		     sizeof(rfc1042_header)) &&
	     rx_pkt_hdr->rfc1042_hdr.snap_type != htons(ETH_P_AARP) &&
	     rx_pkt_hdr->rfc1042_hdr.snap_type != htons(ETH_P_IPX))) {
		/*
		 * Replace the 803 header and rfc1042 header (llc/snap) with
		 * an Ethernet II header, keep the src/dst and snap_type
		 * (ethertype).
		 *
		 * The firmware only passes up SNAP frames converting all RX
		 * data from 802.11 to 802.2/LLC/SNAP frames.
		 *
		 * To create the Ethernet II, just move the src, dst address
		 * right before the snap_type.
		 */
		p_ethhdr = (struct ethhdr *)
			((u8 *)(&rx_pkt_hdr->eth803_hdr)
			 + sizeof(rx_pkt_hdr->eth803_hdr)
			 + sizeof(rx_pkt_hdr->rfc1042_hdr)
			 - sizeof(rx_pkt_hdr->eth803_hdr.h_dest)
			 - sizeof(rx_pkt_hdr->eth803_hdr.h_source)
			 - sizeof(rx_pkt_hdr->rfc1042_hdr.snap_type));
		memcpy(p_ethhdr->h_source, rx_pkt_hdr->eth803_hdr.h_source,
		       sizeof(p_ethhdr->h_source));
		memcpy(p_ethhdr->h_dest, rx_pkt_hdr->eth803_hdr.h_dest,
		       sizeof(p_ethhdr->h_dest));
		/*
		 * Chop off the rxpd + the excess memory from
		 * 802.2/llc/snap header that was removed.
		 */
		hdr_chop = (u8 *)p_ethhdr - (u8 *)uap_rx_pd;
	} else {
		/* Chop off the rxpd */
		hdr_chop = (u8 *)&rx_pkt_hdr->eth803_hdr - (u8 *)uap_rx_pd;
	}

	/*
	 * Chop off the leading header bytes so that it points
	 * to the start of either the reconstructed EthII frame
	 * or the 802.2/llc/snap frame.
	 */
	skb_pull(skb, hdr_chop);

	if (skb_headroom(skb) < NXPWIFI_MIN_DATA_HEADER_LEN) {
		nxpwifi_dbg(adapter, ERROR,
			    "data: Tx: insufficient skb headroom %d\n",
			    skb_headroom(skb));
		/* Insufficient skb headroom - allocate a new skb */
		new_skb =
			skb_realloc_headroom(skb, NXPWIFI_MIN_DATA_HEADER_LEN);
		if (unlikely(!new_skb)) {
			nxpwifi_dbg(adapter, ERROR,
				    "Tx: cannot allocate new_skb\n");
			kfree_skb(skb);
			priv->stats.tx_dropped++;
			return;
		}

		kfree_skb(skb);
		skb = new_skb;
		nxpwifi_dbg(adapter, INFO,
			    "info: new skb headroom %d\n",
			    skb_headroom(skb));
	}

	tx_info = NXPWIFI_SKB_TXCB(skb);
	memset(tx_info, 0, sizeof(*tx_info));
	tx_info->bss_num = priv->bss_num;
	tx_info->bss_type = priv->bss_type;
	tx_info->flags |= NXPWIFI_BUF_FLAG_BRIDGED_PKT;

	rcu_read_lock();
	src_node = nxpwifi_get_sta_entry(priv, rx_pkt_hdr->eth803_hdr.h_source);
	if (src_node) {
		src_node->stats.last_rx = jiffies;
		src_node->stats.rx_bytes += skb->len;
		src_node->stats.rx_packets++;
		src_node->stats.last_tx_rate = uap_rx_pd->rx_rate;
		src_node->stats.last_tx_htinfo = uap_rx_pd->ht_info;
	}
	rcu_read_unlock();

	if (is_unicast_ether_addr(rx_pkt_hdr->eth803_hdr.h_dest)) {
		/*
		 * Update bridge packet statistics as the
		 * packet is not going to kernel/upper layer.
		 */
		priv->stats.rx_bytes += skb->len;
		priv->stats.rx_packets++;

		/*
		 * Sending bridge packet to TX queue, so save the packet
		 * length in TXCB to update statistics in TX complete.
		 */
		tx_info->pkt_len = skb->len;
	}

	__net_timestamp(skb);

	index = nxpwifi_1d_to_wmm_queue[skb->priority];
	atomic_inc(&priv->wmm_tx_pending[index]);
	nxpwifi_wmm_add_buf_txqueue(priv, skb);
	atomic_inc(&adapter->tx_pending);
	atomic_inc(&adapter->pending_bridged_pkts);

	nxpwifi_queue_work(adapter, &adapter->main_work);
}

/* AP fwd: mcast/bcast -> up + bridge; unicast -> bridge if RA assoc, else up. */
int nxpwifi_handle_uap_rx_forward(struct nxpwifi_private *priv,
				  struct sk_buff *skb)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct uap_rxpd *uap_rx_pd;
	struct rx_packet_hdr *rx_pkt_hdr;
	u8 ra[ETH_ALEN];
	struct sk_buff *skb_uap;
	struct nxpwifi_sta_node *node;

	uap_rx_pd = (struct uap_rxpd *)(skb->data);
	rx_pkt_hdr = (void *)uap_rx_pd + le16_to_cpu(uap_rx_pd->rx_pkt_offset);

	/* don't do packet forwarding in disconnected state */
	if (!priv->media_connected) {
		nxpwifi_dbg(adapter, ERROR,
			    "drop packet in disconnected state.\n");
		dev_kfree_skb_any(skb);
		return 0;
	}

	memcpy(ra, rx_pkt_hdr->eth803_hdr.h_dest, ETH_ALEN);

	if (is_multicast_ether_addr(ra)) {
		skb_uap = skb_copy(skb, GFP_ATOMIC);
		if (likely(skb_uap)) {
			nxpwifi_uap_queue_bridged_pkt(priv, skb_uap);
		} else {
			nxpwifi_dbg(adapter, ERROR,
				    "failed to copy skb for uAP\n");
				    priv->stats.rx_dropped++;
				    dev_kfree_skb_any(skb);
			return -ENOMEM;
		}
	} else {
		node = nxpwifi_get_sta_entry_rcu(priv, ra);
		if (node) {
			/* Requeue Intra-BSS packet */
			nxpwifi_uap_queue_bridged_pkt(priv, skb);
			return 0;
		}
	}

	/* Forward unicat/Inter-BSS packets to kernel. */
	return nxpwifi_process_rx_packet(priv, skb);
}

int nxpwifi_uap_recv_packet(struct nxpwifi_private *priv,
			    struct sk_buff *skb)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_sta_node *src_node, *dst_node;
	struct ethhdr *p_ethhdr;
	struct sk_buff *skb_uap;
	struct nxpwifi_txinfo *tx_info;

	if (!skb)
		return -ENOMEM;

	p_ethhdr = (void *)skb->data;
	rcu_read_lock();
	src_node = nxpwifi_get_sta_entry(priv, p_ethhdr->h_source);
	if (src_node) {
		src_node->stats.last_rx = jiffies;
		src_node->stats.rx_bytes += skb->len;
		src_node->stats.rx_packets++;
	}
	dst_node = nxpwifi_get_sta_entry(priv, p_ethhdr->h_dest);
	rcu_read_unlock();

	if (is_multicast_ether_addr(p_ethhdr->h_dest) || dst_node) {
		if (skb_headroom(skb) < NXPWIFI_MIN_DATA_HEADER_LEN)
			skb_uap =
			skb_realloc_headroom(skb, NXPWIFI_MIN_DATA_HEADER_LEN);
		else
			skb_uap = skb_copy(skb, GFP_ATOMIC);

		if (likely(skb_uap)) {
			tx_info = NXPWIFI_SKB_TXCB(skb_uap);
			memset(tx_info, 0, sizeof(*tx_info));
			tx_info->bss_num = priv->bss_num;
			tx_info->bss_type = priv->bss_type;
			tx_info->flags |= NXPWIFI_BUF_FLAG_BRIDGED_PKT;
			__net_timestamp(skb_uap);
			nxpwifi_wmm_add_buf_txqueue(priv, skb_uap);
			atomic_inc(&adapter->tx_pending);
			atomic_inc(&adapter->pending_bridged_pkts);
			if ((atomic_read(&adapter->pending_bridged_pkts) >=
					NXPWIFI_BRIDGED_PKTS_THR_HIGH)) {
				nxpwifi_dbg(adapter, ERROR,
					    "Tx: Bridge packet limit reached. Drop packet!\n");
				nxpwifi_uap_cleanup_tx_queues(priv);
			}

		} else {
			nxpwifi_dbg(adapter, ERROR, "failed to allocate skb_uap");
		}

		nxpwifi_queue_work(adapter, &adapter->main_work);
		/* Don't forward Intra-BSS unicast packet to upper layer*/

		if (dst_node)
			return 0;
	}

	skb->dev = priv->netdev;
	skb->protocol = eth_type_trans(skb, priv->netdev);
	skb->ip_summed = CHECKSUM_NONE;

	/* Forward multicast/broadcast packet to upper layer*/
	netif_rx(skb);
	return 0;
}

/* Process AP RX: check RxPD/len, handle mgmt or 11n reorder/AMSDU, then forward. */
int nxpwifi_process_uap_rx_packet(struct nxpwifi_private *priv,
				  struct sk_buff *skb)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	int ret;
	struct uap_rxpd *uap_rx_pd;
	struct rx_packet_hdr *rx_pkt_hdr;
	u16 rx_pkt_type;
	u8 ta[ETH_ALEN], pkt_type;
	struct nxpwifi_sta_node *node;

	uap_rx_pd = (struct uap_rxpd *)(skb->data);
	rx_pkt_type = le16_to_cpu(uap_rx_pd->rx_pkt_type);
	rx_pkt_hdr = (void *)uap_rx_pd + le16_to_cpu(uap_rx_pd->rx_pkt_offset);

	if (le16_to_cpu(uap_rx_pd->rx_pkt_offset) +
	    sizeof(rx_pkt_hdr->eth803_hdr) > skb->len) {
		nxpwifi_dbg(adapter, ERROR,
			    "wrong rx packet for struct ethhdr: len=%d, offset=%d\n",
			    skb->len, le16_to_cpu(uap_rx_pd->rx_pkt_offset));
		priv->stats.rx_dropped++;
		dev_kfree_skb_any(skb);
		return 0;
	}

	ether_addr_copy(ta, rx_pkt_hdr->eth803_hdr.h_source);

	if ((le16_to_cpu(uap_rx_pd->rx_pkt_offset) +
	     le16_to_cpu(uap_rx_pd->rx_pkt_length)) > (u16)skb->len) {
		nxpwifi_dbg(adapter, ERROR,
			    "wrong rx packet: len=%d, offset=%d, length=%d\n",
			    skb->len, le16_to_cpu(uap_rx_pd->rx_pkt_offset),
			    le16_to_cpu(uap_rx_pd->rx_pkt_length));
		priv->stats.rx_dropped++;
		rcu_read_lock();
		node = nxpwifi_get_sta_entry(priv, ta);
		if (node)
			node->stats.tx_failed++;
		rcu_read_unlock();

		dev_kfree_skb_any(skb);
		return 0;
	}

	if (rx_pkt_type == PKT_TYPE_MGMT) {
		ret = nxpwifi_process_mgmt_packet(priv, skb);
		if (ret && (ret != -EINPROGRESS))
			nxpwifi_dbg(adapter, DATA, "Rx of mgmt packet failed");
		if (ret != -EINPROGRESS)
			dev_kfree_skb_any(skb);
		return ret;
	}

	if (rx_pkt_type != PKT_TYPE_BAR && uap_rx_pd->priority < MAX_NUM_TID) {
		rcu_read_lock();
		node = nxpwifi_get_sta_entry(priv, ta);
		if (node)
			node->rx_seq[uap_rx_pd->priority] =
						le16_to_cpu(uap_rx_pd->seq_num);
		rcu_read_unlock();
	}

	if (!priv->ap_11n_enabled ||
	    (!nxpwifi_11n_get_rx_reorder_tbl(priv, uap_rx_pd->priority, ta) &&
	    (le16_to_cpu(uap_rx_pd->rx_pkt_type) != PKT_TYPE_AMSDU))) {
		ret = nxpwifi_handle_uap_rx_forward(priv, skb);
		return ret;
	}

	/* Reorder and send to kernel */
	pkt_type = (u8)le16_to_cpu(uap_rx_pd->rx_pkt_type);
	ret = nxpwifi_11n_rx_reorder_pkt(priv, le16_to_cpu(uap_rx_pd->seq_num),
					 uap_rx_pd->priority, ta, pkt_type, skb);

	if (ret || rx_pkt_type == PKT_TYPE_BAR)
		dev_kfree_skb_any(skb);

	if (ret)
		priv->stats.rx_dropped++;

	return ret;
}

/*
 * Build TxPD for AP TX: push aligned TxPD; set bss, len/off, prio, delay, txctl,
 * flags.
 */
void nxpwifi_process_uap_txpd(struct nxpwifi_private *priv,
			      struct sk_buff *skb)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct uap_txpd *txpd;
	struct nxpwifi_txinfo *tx_info = NXPWIFI_SKB_TXCB(skb);
	int pad;
	u16 pkt_type, pkt_offset;
	int hroom = adapter->intf_hdr_len;

	pkt_type = nxpwifi_is_skb_mgmt_frame(skb) ? PKT_TYPE_MGMT : 0;

	pad = ((uintptr_t)skb->data - (sizeof(*txpd) + hroom)) &
	       (NXPWIFI_DMA_ALIGN_SZ - 1);

	skb_push(skb, sizeof(*txpd) + pad);

	txpd = (struct uap_txpd *)skb->data;
	memset(txpd, 0, sizeof(*txpd));
	txpd->bss_num = priv->bss_num;
	txpd->bss_type = priv->bss_type;
	txpd->tx_pkt_length = cpu_to_le16((u16)(skb->len - (sizeof(*txpd) +
						pad)));
	txpd->priority = (u8)skb->priority;

	txpd->pkt_delay_2ms = nxpwifi_wmm_compute_drv_pkt_delay(priv, skb);

	if (tx_info->flags & NXPWIFI_BUF_FLAG_EAPOL_TX_STATUS ||
	    tx_info->flags & NXPWIFI_BUF_FLAG_ACTION_TX_STATUS) {
		txpd->tx_token_id = tx_info->ack_frame_id;
		txpd->flags |= NXPWIFI_TXPD_FLAGS_REQ_TX_STATUS;
	}

	if (txpd->priority < ARRAY_SIZE(priv->wmm.user_pri_pkt_tx_ctrl))
		/*
		 * Set the priority specific tx_control field, setting of 0 will
		 * cause the default value to be used later in this function.
		 */
		txpd->tx_control =
		    cpu_to_le32(priv->wmm.user_pri_pkt_tx_ctrl[txpd->priority]);

	/* Offset of actual data */
	pkt_offset = sizeof(*txpd) + pad;
	if (pkt_type == PKT_TYPE_MGMT) {
		/* Set the packet type and add header for management frame */
		txpd->tx_pkt_type = cpu_to_le16(pkt_type);
		pkt_offset += NXPWIFI_MGMT_FRAME_HEADER_SIZE;
	}

	txpd->tx_pkt_offset = cpu_to_le16(pkt_offset);

	/* make space for adapter->intf_hdr_len */
	skb_push(skb, hroom);

	if (!txpd->tx_control)
		/* TxCtrl set by user or default */
		txpd->tx_control = cpu_to_le32(priv->pkt_tx_ctrl);
}
