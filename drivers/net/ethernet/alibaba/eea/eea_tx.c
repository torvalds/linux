// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#include <net/netdev_queues.h>

#include "eea_net.h"
#include "eea_pci.h"
#include "eea_ring.h"

struct eea_sq_free_stats {
	u64 packets;
	u64 bytes;
};

struct eea_tx_meta {
	struct eea_tx_meta *next;

	u32 id;

	union {
		struct sk_buff *skb;
		void *data;
	};

	u32 num;

	dma_addr_t dma_addr;
	struct eea_tx_desc *desc;
	u32 dma_len;
	bool unmap;
	bool unmap_single;
};

static struct eea_tx_meta *eea_tx_meta_get(struct eea_net_tx *tx)
{
	struct eea_tx_meta *meta;

	if (!tx->free)
		return NULL;

	meta = tx->free;
	tx->free = meta->next;

	return meta;
}

static void eea_tx_meta_put_and_unmap(struct eea_net_tx *tx,
				      struct eea_tx_meta *meta)
{
	struct eea_tx_meta *head;

	head = meta;

	while (true) {
		if (meta->unmap) {
			if (meta->unmap_single)
				dma_unmap_single(tx->dma_dev, meta->dma_addr,
						 meta->dma_len, DMA_TO_DEVICE);
			else
				dma_unmap_page(tx->dma_dev, meta->dma_addr,
					       meta->dma_len, DMA_TO_DEVICE);
		}

		if (meta->next) {
			meta = meta->next;
			continue;
		}

		break;
	}

	meta->next = tx->free;
	tx->free = head;
}

static void eea_meta_free_xmit(struct eea_net_tx *tx,
			       struct eea_tx_meta *meta,
			       int budget,
			       struct eea_tx_cdesc *desc,
			       struct eea_sq_free_stats *stats)
{
	struct sk_buff *skb = meta->skb;

	if (unlikely((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) && desc)) {
		struct skb_shared_hwtstamps ts = {};

		ts.hwtstamp = EEA_DESC_TS(desc) + tx->enet->hw_ts_offset;
		skb_tstamp_tx(skb, &ts);
	}

	++stats->packets;
	stats->bytes += meta->skb->len;
	napi_consume_skb(meta->skb, budget);

	meta->data = NULL;
}

static int eea_clean_tx(struct eea_net_tx *tx, int budget)
{
	struct eea_sq_free_stats stats = {0};
	struct eea_tx_cdesc *desc;
	struct eea_tx_meta *meta;
	int desc_n;
	u16 id;

	while (stats.packets < budget) {
		desc = eea_ering_cq_get_desc(tx->ering);
		if (!desc)
			break;

		id = le16_to_cpu(desc->id);
		if (unlikely(id >= tx->ering->num)) {
			if (net_ratelimit())
				netdev_err(tx->enet->netdev, "tx invalid id %d\n",
					   id);
			eea_ering_cq_ack_desc(tx->ering, 1);
			continue;
		}

		meta = &tx->meta[id];

		if (meta->data) {
			eea_tx_meta_put_and_unmap(tx, meta);
			eea_meta_free_xmit(tx, meta, budget, desc, &stats);
			desc_n = meta->num;
		} else {
			if (net_ratelimit())
				netdev_err(tx->enet->netdev,
					   "tx meta->data is null. id %d num: %d\n",
					   meta->id, meta->num);
			desc_n = 1;
		}

		eea_ering_cq_ack_desc(tx->ering, desc_n);
	}

	if (stats.packets) {
		u64_stats_update_begin(&tx->stats.syncp);
		u64_stats_add(&tx->stats.bytes, stats.bytes);
		u64_stats_add(&tx->stats.packets, stats.packets);
		u64_stats_update_end(&tx->stats.syncp);
	}

	return stats.packets;
}

int eea_poll_tx(struct eea_net_tx *tx, int budget)
{
	struct eea_net *enet = tx->enet;
	u32 index = tx - enet->tx;
	struct netdev_queue *txq;
	int num;

	txq = netdev_get_tx_queue(enet->netdev, index);

	__netif_tx_lock(txq, smp_processor_id());

	num = eea_clean_tx(tx, budget);

	if (netif_tx_queue_stopped(txq) &&
	    tx->ering->num_free >= MAX_SKB_FRAGS + 2)
		netif_tx_wake_queue(txq);

	__netif_tx_unlock(txq);

	return num;
}

static int eea_fill_desc_from_skb(const struct sk_buff *skb,
				  struct eea_tx_desc *desc)
{
	if (skb_is_gso(skb)) {
		struct skb_shared_info *sinfo = skb_shinfo(skb);

		desc->gso_size = cpu_to_le16(sinfo->gso_size);
		if (sinfo->gso_type & SKB_GSO_TCPV4)
			desc->gso_type = EEA_TX_GSO_TCPV4;

		else if (sinfo->gso_type & SKB_GSO_TCPV6)
			desc->gso_type = EEA_TX_GSO_TCPV6;

		else if (sinfo->gso_type & SKB_GSO_UDP_L4)
			desc->gso_type = EEA_TX_GSO_UDP_L4;

		else
			return -EINVAL;

		if (sinfo->gso_type & SKB_GSO_TCP_ECN)
			desc->gso_type |= EEA_TX_GSO_ECN;
	} else {
		desc->gso_type = EEA_TX_GSO_NONE;
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		desc->csum_start = cpu_to_le16(skb_checksum_start_offset(skb));
		desc->csum_offset = cpu_to_le16(skb->csum_offset);
	}

	return 0;
}

static struct eea_tx_meta *__eea_tx_desc_fill(struct eea_net_tx *tx,
					      struct eea_tx_meta *head_meta,
					      dma_addr_t addr, u32 data_len,
					      u32 dma_len, bool last,
					      void *data, u16 flags,
					      bool unmap)
{
	struct eea_tx_meta *meta;
	struct eea_tx_desc *desc;

	meta = eea_tx_meta_get(tx);

	desc = eea_ering_sq_alloc_desc(tx->ering, meta->id, last, flags);
	desc->addr = cpu_to_le64(addr);
	desc->len = cpu_to_le16(data_len);

	meta->next     = NULL;
	meta->dma_len  = dma_len;
	meta->dma_addr = addr;
	meta->data     = data;
	meta->num      = 1;
	meta->desc     = desc;
	meta->unmap    = unmap;
	meta->unmap_single = false;

	if (head_meta) {
		meta->next = head_meta->next;
		head_meta->next = meta;
		++head_meta->num;
	}

	return meta;
}

static struct eea_tx_meta *eea_tx_desc_fill(struct eea_net_tx *tx,
					    struct eea_tx_meta *head_meta,
					    dma_addr_t addr, u32 length,
					    bool is_last, void *data, u16 flags)
{
	struct eea_tx_meta *meta;
	u16 len, last;

	WARN_ON_ONCE(length >= 2 * USHRT_MAX);

	/* Since eea does not support BIG TCP, the maximum GSO size is capped at
	 * 64KB. Consequently, a single skb buffer (head or fragment) will not
	 * require more than two descriptors
	 */
	if (length > USHRT_MAX) {
		len = USHRT_MAX;
		last = false;
	} else {
		len = length;
		last = is_last;
	}

	meta = __eea_tx_desc_fill(tx, head_meta, addr, len, length,
				  last, data, flags, true);

	if (length > USHRT_MAX) {
		if (!head_meta)
			head_meta = meta;

		addr += USHRT_MAX;
		len = length - USHRT_MAX;

		__eea_tx_desc_fill(tx, head_meta, addr, len, 0, is_last,
				   NULL, 0, false);
	}

	return meta;
}

static int eea_tx_add_skb_frag(struct eea_net_tx *tx,
			       struct eea_tx_meta *head_meta,
			       const skb_frag_t *frag, bool is_last)
{
	u32 len = skb_frag_size(frag);
	dma_addr_t addr;

	addr = skb_frag_dma_map(tx->dma_dev, frag, 0, len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(tx->dma_dev, addr)))
		return -ENOMEM;

	eea_tx_desc_fill(tx, head_meta, addr, len, is_last, NULL, 0);

	return 0;
}

static int eea_tx_post_skb(struct eea_net_tx *tx, struct sk_buff *skb)
{
	const struct skb_shared_info *shinfo = skb_shinfo(skb);
	u32 hlen = skb_headlen(skb);
	struct eea_tx_meta *meta;
	const skb_frag_t *frag;
	dma_addr_t addr;
	u32 len = hlen;
	int i, err;
	u16 flags;
	bool last;

	if (len) {
		addr = dma_map_single(tx->dma_dev, skb->data, len,
				      DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(tx->dma_dev, addr)))
			return -ENOMEM;

		last = !shinfo->nr_frags;
		i = 0;
	} else {
		/* The net stack will never submit an skb with an skb->len of
		 * 0. If the head len is 0, the number of frags must be greater
		 * than 0.
		 */
		frag = &shinfo->frags[0];
		len = skb_frag_size(frag);

		addr = skb_frag_dma_map(tx->dma_dev, frag, 0, len,
					DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(tx->dma_dev, addr)))
			return -ENOMEM;

		last = shinfo->nr_frags == 1;
		i = 1;
	}

	flags = skb->ip_summed == CHECKSUM_PARTIAL ? EEA_DESC_F_DO_CSUM : 0;

	meta = eea_tx_desc_fill(tx, NULL, addr, len, last, skb, flags);
	meta->unmap_single = !!hlen;

	err = eea_fill_desc_from_skb(skb, meta->desc);
	if (err)
		goto err_cancel;

	for (; i < shinfo->nr_frags; i++) {
		frag = &shinfo->frags[i];
		bool is_last = i == (shinfo->nr_frags - 1);

		err = eea_tx_add_skb_frag(tx, meta, frag, is_last);
		if (err)
			goto err_cancel;
	}

	eea_ering_sq_commit_desc(tx->ering);

	u64_stats_update_begin(&tx->stats.syncp);
	u64_stats_add(&tx->stats.descs, meta->num);
	u64_stats_update_end(&tx->stats.syncp);

	return 0;

err_cancel:
	eea_ering_sq_cancel(tx->ering);
	eea_tx_meta_put_and_unmap(tx, meta);
	meta->data = NULL;
	return err;
}

static void eea_tx_kick(struct eea_net_tx *tx)
{
	eea_ering_kick(tx->ering);

	u64_stats_update_begin(&tx->stats.syncp);
	u64_stats_inc(&tx->stats.kicks);
	u64_stats_update_end(&tx->stats.syncp);
}

static int eea_tx_check_free_num(struct eea_net_tx *tx,
				 struct netdev_queue *txq)
{
	int n;

	/* MAX_SKB_FRAGS + 1: Covers the skb linear head and all paged fragments
	 * 1: Extra slot for a head or fragment that exceeds 64KB.
	 */
	n = MAX_SKB_FRAGS + 2;
	return netif_txq_maybe_stop(txq, tx->ering->num_free, n, n);
}

netdev_tx_t eea_tx_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct eea_net *enet = netdev_priv(netdev);
	int qnum = skb_get_queue_mapping(skb);
	struct eea_net_tx *tx = &enet->tx[qnum];
	struct netdev_queue *txq;
	int err, enable;

	txq = netdev_get_tx_queue(netdev, qnum);

	enable = eea_tx_check_free_num(tx, txq);
	if (!enable)
		return NETDEV_TX_BUSY;

	err = eea_tx_post_skb(tx, skb);
	if (unlikely(err)) {
		u64_stats_update_begin(&tx->stats.syncp);
		u64_stats_inc(&tx->stats.drops);
		u64_stats_update_end(&tx->stats.syncp);

		dev_kfree_skb_any(skb);
	} else {
		if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		skb_tx_timestamp(skb);
	}

	/* NETDEV_TX_BUSY is expensive. So stop advancing the TX queue. */
	eea_tx_check_free_num(tx, txq);

	if (!netdev_xmit_more() || netif_xmit_stopped(txq))
		eea_tx_kick(tx);

	return NETDEV_TX_OK;
}

static void eea_free_meta(struct eea_net_tx *tx, struct eea_net_cfg *cfg)
{
	struct eea_sq_free_stats stats = {0};
	struct eea_tx_meta *meta;
	int i;

	while ((meta = eea_tx_meta_get(tx)))
		meta->skb = NULL;

	for (i = 0; i < cfg->tx_ring_depth; i++) {
		meta = &tx->meta[i];

		if (!meta->skb)
			continue;

		eea_tx_meta_put_and_unmap(tx, meta);

		eea_meta_free_xmit(tx, meta, 0, NULL, &stats);
	}

	kvfree(tx->meta);
	tx->meta = NULL;
}

/* Maybe called before eea_bind_q_and_cfg. So the cfg must be passed. */
void eea_free_tx(struct eea_net_tx *tx, struct eea_net_cfg *cfg)
{
	if (!tx)
		return;

	if (tx->ering) {
		eea_ering_free(tx->ering);
		tx->ering = NULL;
	}

	if (tx->meta)
		eea_free_meta(tx, cfg);
}

int eea_alloc_tx(struct eea_net_init_ctx *ctx, struct eea_net_tx *tx, u32 idx)
{
	struct eea_tx_meta *meta;
	struct eea_ring *ering;
	u32 i;

	u64_stats_init(&tx->stats.syncp);

	snprintf(tx->name, sizeof(tx->name), "tx.%u", idx);

	ering = eea_ering_alloc(idx * 2 + 1, ctx->cfg.tx_ring_depth, ctx->edev,
				ctx->cfg.tx_sq_desc_size,
				ctx->cfg.tx_cq_desc_size,
				tx->name);
	if (!ering)
		goto err_free_tx;

	tx->ering = ering;
	tx->index = idx;
	tx->dma_dev = ctx->edev->dma_dev;

	/* meta */
	tx->meta = kvcalloc(ctx->cfg.tx_ring_depth,
			    sizeof(*tx->meta), GFP_KERNEL);
	if (!tx->meta)
		goto err_free_tx;

	for (i = 0; i < ctx->cfg.tx_ring_depth; ++i) {
		meta = &tx->meta[i];
		meta->id = i;
		meta->next = tx->free;
		tx->free = meta;
	}

	return 0;

err_free_tx:
	eea_free_tx(tx, &ctx->cfg);
	return -ENOMEM;
}
