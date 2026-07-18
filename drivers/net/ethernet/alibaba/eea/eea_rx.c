// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#include <net/netdev_rx_queue.h>
#include <net/page_pool/helpers.h>

#include "eea_adminq.h"
#include "eea_net.h"
#include "eea_pci.h"
#include "eea_ring.h"

#define EEA_ENABLE_F_NAPI        BIT(0)

#define EEA_PAGE_FRAGS_NUM 1024

#define EEA_RX_BUF_ALIGN 128

#define EEA_RX_BUF_MAX_LEN (10 * 1024)

struct eea_rx_ctx {
	u32 len;
	u32 hdr_len;

	u16 flags;
	bool more;

	struct eea_rx_meta *meta;

	struct eea_rx_ctx_stats stats;
};

static struct eea_rx_meta *eea_rx_meta_get(struct eea_net_rx *rx)
{
	struct eea_rx_meta *meta;

	if (!rx->free)
		return NULL;

	meta = rx->free;
	rx->free = meta->next;

	return meta;
}

static void eea_rx_meta_put(struct eea_net_rx *rx, struct eea_rx_meta *meta)
{
	meta->next = rx->free;
	rx->free = meta;
}

static void eea_free_rx_buffer(struct eea_net_rx *rx, struct eea_rx_meta *meta,
			       bool allow_direct)
{
	u32 drain_count;

	drain_count = EEA_PAGE_FRAGS_NUM - meta->frags;

	if (page_pool_unref_page(meta->page, drain_count) == 0)
		page_pool_put_unrefed_page(rx->pp, meta->page, -1,
					   allow_direct);

	meta->page = NULL;
}

static void eea_rx_meta_dma_sync_for_device(struct eea_net_rx *rx,
					    struct eea_rx_meta *meta)
{
	u32 len;

	if (meta->sync_for_cpu <= meta->offset + rx->headroom)
		return;

	len = meta->sync_for_cpu - meta->offset - rx->headroom;

	dma_sync_single_for_device(rx->enet->edev->dma_dev,
				   meta->dma + meta->offset + rx->headroom,
				   len, DMA_FROM_DEVICE);
	meta->sync_for_cpu = 0;
}

static void meta_align_offset(struct eea_net_rx *rx, struct eea_rx_meta *meta)
{
	int h, b;

	h = rx->headroom;
	b = meta->offset + h;

	/* For better performance, we align the buffer address to
	 * EEA_RX_BUF_ALIGN, as required by the device design.
	 */
	b = ALIGN(b, EEA_RX_BUF_ALIGN);

	meta->offset = b - h;
}

static int eea_alloc_rx_buffer(struct eea_net_rx *rx, struct eea_rx_meta *meta)
{
	struct page *page;

	if (meta->page) {
		eea_rx_meta_dma_sync_for_device(rx, meta);
		return 0;
	}

	page = page_pool_dev_alloc_pages(rx->pp);
	if (!page)
		return -ENOMEM;

	page_pool_fragment_page(page, EEA_PAGE_FRAGS_NUM);

	meta->page = page;
	meta->dma = page_pool_get_dma_addr(page);
	meta->offset = 0;
	meta->frags = 0;
	meta->sync_for_cpu = 0;

	meta_align_offset(rx, meta);

	return 0;
}

static u32 eea_consume_rx_buffer(struct eea_net_rx *rx,
				 struct eea_rx_meta *meta,
				 u32 consumed)
{
	u32 offset;
	int min;

	offset = meta->offset;

	meta->offset += consumed;
	++meta->frags;

	min = SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	min += rx->headroom;
	min += SKB_DATA_ALIGN(ETH_DATA_LEN);

	meta_align_offset(rx, meta);

	if (min + meta->offset > PAGE_SIZE) {
		eea_free_rx_buffer(rx, meta, true);
		return PAGE_SIZE - offset;
	}

	return meta->offset - offset;
}

static void eea_free_rx_hdr(struct eea_net_rx *rx, struct eea_net_cfg *cfg)
{
	struct eea_rx_meta *meta;
	int i;

	for (i = 0; i < cfg->rx_ring_depth; ++i) {
		meta = &rx->meta[i];
		meta->hdr_addr = NULL;

		if (!meta->hdr_page)
			continue;

		dma_unmap_page(rx->dma_dev, meta->hdr_dma, PAGE_SIZE,
			       DMA_FROM_DEVICE);
		put_page(meta->hdr_page);

		meta->hdr_page = NULL;
	}
}

static int eea_alloc_rx_hdr(struct eea_net_init_ctx *ctx, struct eea_net_rx *rx)
{
	struct page *hdr_page = NULL;
	struct eea_rx_meta *meta;
	u32 offset = 0, hdrsize;
	struct device *dmadev;
	dma_addr_t dma;
	int i;

	dmadev = ctx->edev->dma_dev;
	hdrsize = ctx->cfg.split_hdr;

	for (i = 0; i < ctx->cfg.rx_ring_depth; ++i) {
		meta = &rx->meta[i];
		meta->hdr_page = NULL;

		if (!hdr_page || offset + hdrsize > PAGE_SIZE) {
			hdr_page = alloc_page(GFP_KERNEL);
			if (!hdr_page)
				goto err;

			dma = dma_map_page(dmadev, hdr_page, 0, PAGE_SIZE,
					   DMA_FROM_DEVICE);

			if (unlikely(dma_mapping_error(dmadev, dma))) {
				put_page(hdr_page);
				goto err;
			}

			offset = 0;
			meta->hdr_page = hdr_page;
		}

		meta->hdr_dma = dma + offset;
		meta->hdr_addr = page_address(hdr_page) + offset;
		offset += hdrsize;
	}

	return 0;

err:
	eea_free_rx_hdr(rx, &ctx->cfg);
	return -ENOMEM;
}

static void eea_rx_meta_dma_sync_for_cpu(struct eea_net_rx *rx,
					 struct eea_rx_meta *meta, u32 len)
{
	dma_sync_single_for_cpu(rx->enet->edev->dma_dev,
				meta->dma + meta->offset + meta->headroom,
				len, DMA_FROM_DEVICE);
	meta->sync_for_cpu = meta->offset + meta->headroom + len;
}

static int eea_harden_check_overflow(struct eea_rx_ctx *ctx,
				     struct eea_net *enet)
{
	u32 max_len;

	max_len = ctx->meta->truesize - ctx->meta->headroom -
		ctx->meta->tailroom;

	if (unlikely(ctx->len > max_len)) {
		pr_debug("%s: rx error: len %u exceeds truesize %u\n",
			 enet->netdev->name, ctx->len, max_len);
		++ctx->stats.length_errors;
		return -EINVAL;
	}

	return 0;
}

static int eea_harden_check_size(struct eea_rx_ctx *ctx, struct eea_net *enet)
{
	int err;

	err = eea_harden_check_overflow(ctx, enet);
	if (err)
		return err;

	if (ctx->hdr_len) {
		if (unlikely(ctx->hdr_len < ETH_HLEN)) {
			pr_debug("%s: short hdr %u\n", enet->netdev->name,
				 ctx->hdr_len);
			++ctx->stats.length_errors;
			return -EINVAL;
		}

		if (unlikely(ctx->hdr_len > enet->cfg.split_hdr)) {
			pr_debug("%s: rx error: hdr len %u exceeds hdr buffer size %u\n",
				 enet->netdev->name, ctx->hdr_len,
				 enet->cfg.split_hdr);
			++ctx->stats.length_errors;
			return -EINVAL;
		}

		return 0;
	}

	if (unlikely(ctx->len < ETH_HLEN)) {
		pr_debug("%s: short packet %u\n", enet->netdev->name, ctx->len);
		++ctx->stats.length_errors;
		return -EINVAL;
	}

	return 0;
}

static struct sk_buff *eea_build_skb(void *buf, u32 buflen, u32 headroom,
				     u32 len)
{
	struct sk_buff *skb;

	skb = build_skb(buf, buflen);
	if (unlikely(!skb))
		return NULL;

	skb_reserve(skb, headroom);
	skb_put(skb, len);

	return skb;
}

static struct sk_buff *eea_rx_build_split_hdr_skb(struct eea_net_rx *rx,
						  struct eea_rx_ctx *ctx)
{
	struct eea_rx_meta *meta = ctx->meta;
	u32 truesize, offset;
	struct sk_buff *skb;
	struct page *page;

	dma_sync_single_for_cpu(rx->enet->edev->dma_dev, meta->hdr_dma,
				ctx->hdr_len, DMA_FROM_DEVICE);

	skb = napi_alloc_skb(rx->napi, ctx->hdr_len);
	if (unlikely(!skb))
		return NULL;

	skb_put_data(skb, ctx->meta->hdr_addr, ctx->hdr_len);

	if (ctx->len) {
		page = meta->page;
		offset = meta->offset + meta->headroom;

		truesize = eea_consume_rx_buffer(rx, meta,
						 meta->headroom + ctx->len);

		skb_add_rx_frag(skb, 0, page, offset, ctx->len, truesize);
	}

	skb_mark_for_recycle(skb);

	return skb;
}

static struct sk_buff *eea_rx_build_skb(struct eea_net_rx *rx,
					struct eea_rx_ctx *ctx)
{
	struct eea_rx_meta *meta = ctx->meta;
	u32 shinfo_size, bufsize, truesize;
	struct sk_buff *skb;
	struct page *page;
	void *buf;

	page = meta->page;

	shinfo_size = SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	buf = page_address(page) + meta->offset;
	bufsize = meta->headroom + SKB_DATA_ALIGN(ctx->len) + shinfo_size;

	skb = eea_build_skb(buf, bufsize, meta->headroom, ctx->len);
	if (unlikely(!skb))
		return NULL;

	truesize = eea_consume_rx_buffer(rx, meta, bufsize);
	skb_mark_for_recycle(skb);

	skb->truesize += truesize - bufsize;

	return skb;
}

static void process_remain_buf(struct eea_net_rx *rx, struct eea_rx_ctx *ctx)
{
	struct eea_net *enet = rx->enet;
	struct sk_buff *head_skb;
	u32 offset, truesize, nr_frags;
	struct page *page;

	if (eea_harden_check_overflow(ctx, enet))
		goto err;

	head_skb = rx->pkt.head_skb;

	nr_frags = skb_shinfo(head_skb)->nr_frags;
	if (unlikely(nr_frags >= MAX_SKB_FRAGS))
		goto err;

	offset = ctx->meta->offset + ctx->meta->headroom;
	page = ctx->meta->page;
	truesize = eea_consume_rx_buffer(rx, ctx->meta,
					 ctx->meta->headroom + ctx->len);

	skb_add_rx_frag(head_skb, nr_frags, page, offset, ctx->len, truesize);

	return;

err:
	dev_kfree_skb(rx->pkt.head_skb);
	++ctx->stats.drops;
	rx->pkt.do_drop = true;
	rx->pkt.head_skb = NULL;
}

static void process_first_buf(struct eea_net_rx *rx, struct eea_rx_ctx *ctx)
{
	struct eea_net *enet = rx->enet;
	struct sk_buff *skb = NULL;

	if (eea_harden_check_size(ctx, enet))
		goto err;

	rx->pkt.data_valid = ctx->flags & EEA_DESC_F_DATA_VALID;

	if (ctx->hdr_len)
		skb = eea_rx_build_split_hdr_skb(rx, ctx);
	else
		skb = eea_rx_build_skb(rx, ctx);

	if (unlikely(!skb))
		goto err;

	rx->pkt.head_skb = skb;

	return;

err:
	++ctx->stats.drops;
	rx->pkt.do_drop = true;
}

static void eea_submit_skb(struct eea_net_rx *rx, struct sk_buff *skb,
			   struct eea_rx_cdesc *desc)
{
	struct eea_net *enet = rx->enet;

	if (rx->pkt.data_valid)
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (enet->cfg.ts_cfg.rx_filter == HWTSTAMP_FILTER_ALL)
		skb_hwtstamps(skb)->hwtstamp = EEA_DESC_TS(desc) +
			enet->hw_ts_offset;

	skb_record_rx_queue(skb, rx->index);
	skb->protocol = eth_type_trans(skb, enet->netdev);

	napi_gro_receive(rx->napi, skb);
}

static int eea_rx_desc_to_ctx(struct eea_net_rx *rx,
			      struct eea_rx_ctx *ctx,
			      struct eea_rx_cdesc *desc)
{
	u16 id;

	ctx->meta = NULL;

	id = le16_to_cpu(desc->id);
	if (unlikely(id >= rx->ering->num)) {
		if (net_ratelimit())
			netdev_err(rx->enet->netdev, "rx invalid id %d\n", id);
		return -EINVAL;
	}

	ctx->meta = &rx->meta[id];
	if (!ctx->meta->in_use) {
		if (net_ratelimit())
			netdev_err(rx->enet->netdev, "rx invalid id %d\n", id);
		ctx->meta = NULL;
		return -EINVAL;
	}

	ctx->meta->in_use = false;

	ctx->len = le16_to_cpu(desc->len);
	if (unlikely(ctx->len > ctx->meta->len)) {
		if (net_ratelimit())
			netdev_err(rx->enet->netdev, "rx invalid len(%d) id:%d\n",
				   ctx->len, id);
		return -EINVAL;
	}

	ctx->flags = le16_to_cpu(desc->flags);

	ctx->hdr_len = 0;
	if (ctx->flags & EEA_DESC_F_SPLIT_HDR) {
		ctx->hdr_len = le16_to_cpu(desc->len_ex) &
			EEA_RX_CDESC_HDR_LEN_MASK;
		ctx->stats.split_hdr_bytes += ctx->hdr_len;
		++ctx->stats.split_hdr_packets;
	}

	ctx->more = ctx->flags & EEA_RING_DESC_F_MORE;

	return 0;
}

static int eea_cleanrx(struct eea_net_rx *rx, int budget,
		       struct eea_rx_ctx *ctx)
{
	struct eea_rx_cdesc *desc;
	struct eea_rx_meta *meta;
	int recv, err;

	for (recv = 0; recv < budget; ) {
		desc = eea_ering_cq_get_desc(rx->ering);
		if (!desc)
			break;

		err = eea_rx_desc_to_ctx(rx, ctx, desc);
		if (unlikely(err)) {
			if (ctx->meta)
				eea_rx_meta_put(rx, ctx->meta);

			if (rx->pkt.head_skb) {
				dev_kfree_skb(rx->pkt.head_skb);
				++ctx->stats.drops;
			}

			/* A hardware error occurred; we are attempting to
			 * mitigate the impact. Subsequent packets may be
			 * corrupted.
			 */
			ctx->more = false;
			goto ack;
		}

		meta = ctx->meta;

		if (unlikely(rx->pkt.do_drop))
			goto skip;

		eea_rx_meta_dma_sync_for_cpu(rx, meta, ctx->len);

		rx->pkt.recv_len += ctx->len;
		rx->pkt.recv_len += ctx->hdr_len;

		if (!rx->pkt.idx)
			process_first_buf(rx, ctx);
		else
			process_remain_buf(rx, ctx);

		++rx->pkt.idx;

		if (!ctx->more && rx->pkt.head_skb) {
			eea_submit_skb(rx, rx->pkt.head_skb, desc);
			ctx->stats.bytes += rx->pkt.recv_len;
			++ctx->stats.packets;
		}

skip:
		eea_rx_meta_put(rx, meta);
ack:
		eea_ering_cq_ack_desc(rx->ering, 1);
		++ctx->stats.descs;

		if (!ctx->more) {
			memset(&rx->pkt, 0, sizeof(rx->pkt));
			++recv;
		}
	}

	return recv;
}

static void eea_rx_dma_sync_hdr(struct eea_net_rx *rx, dma_addr_t addr)
{
	dma_sync_single_for_device(rx->dma_dev, addr,
				   rx->enet->cfg.split_hdr,
				   DMA_FROM_DEVICE);
}

/* Only be called from napi. */
static void eea_rx_post(struct eea_net_rx *rx, struct eea_rx_ctx *ctx)
{
	u32 tailroom, headroom, room, len;
	struct eea_rx_meta *meta;
	struct eea_rx_desc *desc;
	int err = 0, num = 0;
	dma_addr_t addr;

	tailroom = SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	headroom = rx->headroom;
	room = headroom + tailroom;

	while (true) {
		meta = eea_rx_meta_get(rx);
		if (!meta)
			break;

		err = eea_alloc_rx_buffer(rx, meta);
		if (err) {
			eea_rx_meta_put(rx, meta);
			break;
		}

		len = min_t(u32, PAGE_SIZE - meta->offset - room,
			    EEA_RX_BUF_MAX_LEN);

		len = ALIGN_DOWN(len, SMP_CACHE_BYTES);

		addr = meta->dma + meta->offset + headroom;

		desc = eea_ering_sq_alloc_desc(rx->ering, meta->id, true, 0);
		desc->addr = cpu_to_le64(addr);
		desc->len = cpu_to_le16(len);

		if (meta->hdr_addr) {
			eea_rx_dma_sync_hdr(rx, meta->hdr_dma);
			desc->hdr_addr = cpu_to_le64(meta->hdr_dma);
		}

		eea_ering_sq_commit_desc(rx->ering);

		meta->truesize = len + room;
		meta->headroom = headroom;
		meta->tailroom = tailroom;
		meta->len = len;
		meta->in_use = true;
		++num;
	}

	if (num) {
		eea_ering_kick(rx->ering);
		++ctx->stats.kicks;
	}
}

static int eea_poll(struct napi_struct *napi, int budget)
{
	struct eea_irq_blk *blk = container_of(napi, struct eea_irq_blk, napi);
	struct eea_net_rx *rx = blk->rx;
	struct eea_net_tx *tx = &rx->enet->tx[rx->index];
	struct eea_rx_ctx ctx = {};
	bool busy = false;
	u32 received;

	busy |= eea_poll_tx(tx, budget) >= budget;

	received = eea_cleanrx(rx, budget, &ctx);

	if (rx->ering->num_free > budget) {
		/* Due to the hardware design, there is no notification when
		 * buffers are exhausted. Therefore, we should proactively
		 * pre-fill the buffers to avoid starvation.
		 */
		eea_rx_post(rx, &ctx);

		if (rx->ering->num - rx->ering->num_free < budget)
			busy = true;
	}

	eea_update_rx_stats(&rx->stats, &ctx.stats);

	busy |= received >= budget;

	if (busy)
		return budget;

	if (napi_complete_done(napi, received))
		eea_ering_irq_active(rx->ering, tx->ering);

	return received;
}

static void eea_free_rx_buffers(struct eea_net_rx *rx, struct eea_net_cfg *cfg)
{
	struct eea_rx_meta *meta;
	u32 i;

	if (rx->pkt.head_skb) {
		dev_kfree_skb(rx->pkt.head_skb);
		rx->pkt.head_skb = NULL;
	}

	for (i = 0; i < cfg->rx_ring_depth; ++i) {
		meta = &rx->meta[i];
		if (!meta->page)
			continue;

		eea_free_rx_buffer(rx, meta, false);
	}
}

static struct page_pool *eea_create_pp(struct eea_net_init_ctx *ctx, u32 idx)
{
	struct page_pool_params pp_params = {0};

	pp_params.order     = 0;
	pp_params.flags     = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV;
	pp_params.pool_size = ctx->cfg.rx_ring_depth;
	pp_params.nid       = dev_to_node(ctx->edev->dma_dev);
	pp_params.dev       = ctx->edev->dma_dev;
	pp_params.netdev    = ctx->netdev;
	pp_params.dma_dir   = DMA_FROM_DEVICE;
	pp_params.max_len   = PAGE_SIZE;
	pp_params.queue_idx = idx;

	return page_pool_create(&pp_params);
}

static void eea_destroy_page_pool(struct eea_net_rx *rx)
{
	if (rx->pp)
		page_pool_destroy(rx->pp);
}

void enet_rx_stop(struct eea_net_rx *rx)
{
	if (rx->flags & EEA_ENABLE_F_NAPI) {
		rx->flags &= ~EEA_ENABLE_F_NAPI;

		disable_irq(rx->enet->irq_blks[rx->index].irq);
		napi_disable(rx->napi);

		page_pool_disable_direct_recycling(rx->pp);
		netif_napi_del(rx->napi);
	}
}

void enet_rx_start(struct eea_net_rx *rx)
{
	netif_napi_add(rx->enet->netdev, rx->napi, eea_poll);

	page_pool_enable_direct_recycling(rx->pp, rx->napi);

	napi_enable(rx->napi);

	rx->flags |= EEA_ENABLE_F_NAPI;

	local_bh_disable();
	napi_schedule(rx->napi);
	local_bh_enable();

	enable_irq(rx->enet->irq_blks[rx->index].irq);
}

/* Maybe called before eea_bind_q_and_cfg. So the cfg must be passed. */
void eea_free_rx(struct eea_net_rx *rx, struct eea_net_cfg *cfg)
{
	if (!rx)
		return;

	if (rx->ering) {
		eea_ering_free(rx->ering);
		rx->ering = NULL;
	}

	if (rx->meta) {
		eea_free_rx_buffers(rx, cfg);
		eea_free_rx_hdr(rx, cfg);
		kvfree(rx->meta);
		rx->meta = NULL;
	}

	if (rx->pp) {
		eea_destroy_page_pool(rx);
		rx->pp = NULL;
	}

	kfree(rx);
}

static void eea_rx_meta_init(struct eea_net_rx *rx, u32 num)
{
	struct eea_rx_meta *meta;
	int i;

	rx->free = NULL;

	for (i = 0; i < num; ++i) {
		meta = &rx->meta[i];
		meta->id = i;
		meta->next = rx->free;
		rx->free = meta;
	}
}

struct eea_net_rx *eea_alloc_rx(struct eea_net_init_ctx *ctx, u32 idx)
{
	struct eea_ring *ering;
	struct eea_net_rx *rx;
	int err;

	rx = kzalloc(sizeof(*rx), GFP_KERNEL);
	if (!rx)
		return rx;

	rx->index = idx;
	snprintf(rx->name, sizeof(rx->name), "rx.%u", idx);

	u64_stats_init(&rx->stats.syncp);

	/* ering */
	ering = eea_ering_alloc(idx * 2, ctx->cfg.rx_ring_depth, ctx->edev,
				ctx->cfg.rx_sq_desc_size,
				ctx->cfg.rx_cq_desc_size,
				rx->name);
	if (!ering)
		goto err_free_rx;

	rx->ering = ering;

	rx->dma_dev = ctx->edev->dma_dev;

	/* meta */
	rx->meta = kvcalloc(ctx->cfg.rx_ring_depth,
			    sizeof(*rx->meta), GFP_KERNEL);
	if (!rx->meta)
		goto err_free_rx;

	eea_rx_meta_init(rx, ctx->cfg.rx_ring_depth);

	if (ctx->cfg.split_hdr) {
		err = eea_alloc_rx_hdr(ctx, rx);
		if (err)
			goto err_free_rx;
	}

	rx->pp = eea_create_pp(ctx, idx);
	if (IS_ERR(rx->pp)) {
		err = PTR_ERR(rx->pp);
		rx->pp = NULL;
		goto err_free_rx;
	}

	return rx;

err_free_rx:
	eea_free_rx(rx, &ctx->cfg);
	return NULL;
}
