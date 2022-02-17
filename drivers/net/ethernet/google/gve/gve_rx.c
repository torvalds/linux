// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#include "gve.h"
#include "gve_adminq.h"
#include "gve_utils.h"
#include <linux/etherdevice.h>

static void gve_rx_free_buffer(struct device *dev,
			       struct gve_rx_slot_page_info *page_info,
			       union gve_rx_data_slot *data_slot)
{
	dma_addr_t dma = (dma_addr_t)(be64_to_cpu(data_slot->addr) &
				      GVE_DATA_SLOT_ADDR_PAGE_MASK);

	page_ref_sub(page_info->page, page_info->pagecnt_bias - 1);
	gve_free_page(dev, page_info->page, dma, DMA_FROM_DEVICE);
}

static void gve_rx_unfill_pages(struct gve_priv *priv, struct gve_rx_ring *rx)
{
	u32 slots = rx->mask + 1;
	int i;

	if (rx->data.raw_addressing) {
		for (i = 0; i < slots; i++)
			gve_rx_free_buffer(&priv->pdev->dev, &rx->data.page_info[i],
					   &rx->data.data_ring[i]);
	} else {
		for (i = 0; i < slots; i++)
			page_ref_sub(rx->data.page_info[i].page,
				     rx->data.page_info[i].pagecnt_bias - 1);
		gve_unassign_qpl(priv, rx->data.qpl->id);
		rx->data.qpl = NULL;
	}
	kvfree(rx->data.page_info);
	rx->data.page_info = NULL;
}

static void gve_rx_free_ring(struct gve_priv *priv, int idx)
{
	struct gve_rx_ring *rx = &priv->rx[idx];
	struct device *dev = &priv->pdev->dev;
	u32 slots = rx->mask + 1;
	size_t bytes;

	gve_rx_remove_from_block(priv, idx);

	bytes = sizeof(struct gve_rx_desc) * priv->rx_desc_cnt;
	dma_free_coherent(dev, bytes, rx->desc.desc_ring, rx->desc.bus);
	rx->desc.desc_ring = NULL;

	dma_free_coherent(dev, sizeof(*rx->q_resources),
			  rx->q_resources, rx->q_resources_bus);
	rx->q_resources = NULL;

	gve_rx_unfill_pages(priv, rx);

	bytes = sizeof(*rx->data.data_ring) * slots;
	dma_free_coherent(dev, bytes, rx->data.data_ring,
			  rx->data.data_bus);
	rx->data.data_ring = NULL;
	netif_dbg(priv, drv, priv->dev, "freed rx ring %d\n", idx);
}

static void gve_setup_rx_buffer(struct gve_rx_slot_page_info *page_info,
			     dma_addr_t addr, struct page *page, __be64 *slot_addr)
{
	page_info->page = page;
	page_info->page_offset = 0;
	page_info->page_address = page_address(page);
	*slot_addr = cpu_to_be64(addr);
	/* The page already has 1 ref */
	page_ref_add(page, INT_MAX - 1);
	page_info->pagecnt_bias = INT_MAX;
}

static int gve_rx_alloc_buffer(struct gve_priv *priv, struct device *dev,
			       struct gve_rx_slot_page_info *page_info,
			       union gve_rx_data_slot *data_slot)
{
	struct page *page;
	dma_addr_t dma;
	int err;

	err = gve_alloc_page(priv, dev, &page, &dma, DMA_FROM_DEVICE,
			     GFP_ATOMIC);
	if (err)
		return err;

	gve_setup_rx_buffer(page_info, dma, page, &data_slot->addr);
	return 0;
}

static int gve_prefill_rx_pages(struct gve_rx_ring *rx)
{
	struct gve_priv *priv = rx->gve;
	u32 slots;
	int err;
	int i;

	/* Allocate one page per Rx queue slot. Each page is split into two
	 * packet buffers, when possible we "page flip" between the two.
	 */
	slots = rx->mask + 1;

	rx->data.page_info = kvzalloc(slots *
				      sizeof(*rx->data.page_info), GFP_KERNEL);
	if (!rx->data.page_info)
		return -ENOMEM;

	if (!rx->data.raw_addressing) {
		rx->data.qpl = gve_assign_rx_qpl(priv);
		if (!rx->data.qpl) {
			kvfree(rx->data.page_info);
			rx->data.page_info = NULL;
			return -ENOMEM;
		}
	}
	for (i = 0; i < slots; i++) {
		if (!rx->data.raw_addressing) {
			struct page *page = rx->data.qpl->pages[i];
			dma_addr_t addr = i * PAGE_SIZE;

			gve_setup_rx_buffer(&rx->data.page_info[i], addr, page,
					    &rx->data.data_ring[i].qpl_offset);
			continue;
		}
		err = gve_rx_alloc_buffer(priv, &priv->pdev->dev, &rx->data.page_info[i],
					  &rx->data.data_ring[i]);
		if (err)
			goto alloc_err;
	}

	return slots;
alloc_err:
	while (i--)
		gve_rx_free_buffer(&priv->pdev->dev,
				   &rx->data.page_info[i],
				   &rx->data.data_ring[i]);
	return err;
}

static void gve_rx_ctx_clear(struct gve_rx_ctx *ctx)
{
	ctx->curr_frag_cnt = 0;
	ctx->total_expected_size = 0;
	ctx->expected_frag_cnt = 0;
	ctx->skb_head = NULL;
	ctx->skb_tail = NULL;
	ctx->reuse_frags = false;
}

static int gve_rx_alloc_ring(struct gve_priv *priv, int idx)
{
	struct gve_rx_ring *rx = &priv->rx[idx];
	struct device *hdev = &priv->pdev->dev;
	u32 slots, npages;
	int filled_pages;
	size_t bytes;
	int err;

	netif_dbg(priv, drv, priv->dev, "allocating rx ring\n");
	/* Make sure everything is zeroed to start with */
	memset(rx, 0, sizeof(*rx));

	rx->gve = priv;
	rx->q_num = idx;

	slots = priv->rx_data_slot_cnt;
	rx->mask = slots - 1;
	rx->data.raw_addressing = priv->queue_format == GVE_GQI_RDA_FORMAT;

	/* alloc rx data ring */
	bytes = sizeof(*rx->data.data_ring) * slots;
	rx->data.data_ring = dma_alloc_coherent(hdev, bytes,
						&rx->data.data_bus,
						GFP_KERNEL);
	if (!rx->data.data_ring)
		return -ENOMEM;
	filled_pages = gve_prefill_rx_pages(rx);
	if (filled_pages < 0) {
		err = -ENOMEM;
		goto abort_with_slots;
	}
	rx->fill_cnt = filled_pages;
	/* Ensure data ring slots (packet buffers) are visible. */
	dma_wmb();

	/* Alloc gve_queue_resources */
	rx->q_resources =
		dma_alloc_coherent(hdev,
				   sizeof(*rx->q_resources),
				   &rx->q_resources_bus,
				   GFP_KERNEL);
	if (!rx->q_resources) {
		err = -ENOMEM;
		goto abort_filled;
	}
	netif_dbg(priv, drv, priv->dev, "rx[%d]->data.data_bus=%lx\n", idx,
		  (unsigned long)rx->data.data_bus);

	/* alloc rx desc ring */
	bytes = sizeof(struct gve_rx_desc) * priv->rx_desc_cnt;
	npages = bytes / PAGE_SIZE;
	if (npages * PAGE_SIZE != bytes) {
		err = -EIO;
		goto abort_with_q_resources;
	}

	rx->desc.desc_ring = dma_alloc_coherent(hdev, bytes, &rx->desc.bus,
						GFP_KERNEL);
	if (!rx->desc.desc_ring) {
		err = -ENOMEM;
		goto abort_with_q_resources;
	}
	rx->cnt = 0;
	rx->db_threshold = priv->rx_desc_cnt / 2;
	rx->desc.seqno = 1;

	/* Allocating half-page buffers allows page-flipping which is faster
	 * than copying or allocating new pages.
	 */
	rx->packet_buffer_size = PAGE_SIZE / 2;
	gve_rx_ctx_clear(&rx->ctx);
	gve_rx_add_to_block(priv, idx);

	return 0;

abort_with_q_resources:
	dma_free_coherent(hdev, sizeof(*rx->q_resources),
			  rx->q_resources, rx->q_resources_bus);
	rx->q_resources = NULL;
abort_filled:
	gve_rx_unfill_pages(priv, rx);
abort_with_slots:
	bytes = sizeof(*rx->data.data_ring) * slots;
	dma_free_coherent(hdev, bytes, rx->data.data_ring, rx->data.data_bus);
	rx->data.data_ring = NULL;

	return err;
}

int gve_rx_alloc_rings(struct gve_priv *priv)
{
	int err = 0;
	int i;

	for (i = 0; i < priv->rx_cfg.num_queues; i++) {
		err = gve_rx_alloc_ring(priv, i);
		if (err) {
			netif_err(priv, drv, priv->dev,
				  "Failed to alloc rx ring=%d: err=%d\n",
				  i, err);
			break;
		}
	}
	/* Unallocate if there was an error */
	if (err) {
		int j;

		for (j = 0; j < i; j++)
			gve_rx_free_ring(priv, j);
	}
	return err;
}

void gve_rx_free_rings_gqi(struct gve_priv *priv)
{
	int i;

	for (i = 0; i < priv->rx_cfg.num_queues; i++)
		gve_rx_free_ring(priv, i);
}

void gve_rx_write_doorbell(struct gve_priv *priv, struct gve_rx_ring *rx)
{
	u32 db_idx = be32_to_cpu(rx->q_resources->db_index);

	iowrite32be(rx->fill_cnt, &priv->db_bar2[db_idx]);
}

static enum pkt_hash_types gve_rss_type(__be16 pkt_flags)
{
	if (likely(pkt_flags & (GVE_RXF_TCP | GVE_RXF_UDP)))
		return PKT_HASH_TYPE_L4;
	if (pkt_flags & (GVE_RXF_IPV4 | GVE_RXF_IPV6))
		return PKT_HASH_TYPE_L3;
	return PKT_HASH_TYPE_L2;
}

static u16 gve_rx_ctx_padding(struct gve_rx_ctx *ctx)
{
	return (ctx->curr_frag_cnt == 0) ? GVE_RX_PAD : 0;
}

static struct sk_buff *gve_rx_add_frags(struct napi_struct *napi,
					struct gve_rx_slot_page_info *page_info,
					u16 packet_buffer_size, u16 len,
					struct gve_rx_ctx *ctx)
{
	u32 offset = page_info->page_offset +  gve_rx_ctx_padding(ctx);
	struct sk_buff *skb;

	if (!ctx->skb_head)
		ctx->skb_head = napi_get_frags(napi);

	if (unlikely(!ctx->skb_head))
		return NULL;

	skb = ctx->skb_head;
	skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page_info->page,
			offset, len, packet_buffer_size);

	return skb;
}

static void gve_rx_flip_buff(struct gve_rx_slot_page_info *page_info, __be64 *slot_addr)
{
	const __be64 offset = cpu_to_be64(PAGE_SIZE / 2);

	/* "flip" to other packet buffer on this page */
	page_info->page_offset ^= PAGE_SIZE / 2;
	*(slot_addr) ^= offset;
}

static int gve_rx_can_recycle_buffer(struct gve_rx_slot_page_info *page_info)
{
	int pagecount = page_count(page_info->page);

	/* This page is not being used by any SKBs - reuse */
	if (pagecount == page_info->pagecnt_bias)
		return 1;
	/* This page is still being used by an SKB - we can't reuse */
	else if (pagecount > page_info->pagecnt_bias)
		return 0;
	WARN(pagecount < page_info->pagecnt_bias,
	     "Pagecount should never be less than the bias.");
	return -1;
}

static struct sk_buff *
gve_rx_raw_addressing(struct device *dev, struct net_device *netdev,
		      struct gve_rx_slot_page_info *page_info, u16 len,
		      struct napi_struct *napi,
		      union gve_rx_data_slot *data_slot,
		      u16 packet_buffer_size, struct gve_rx_ctx *ctx)
{
	struct sk_buff *skb = gve_rx_add_frags(napi, page_info, packet_buffer_size, len, ctx);

	if (!skb)
		return NULL;

	/* Optimistically stop the kernel from freeing the page.
	 * We will check again in refill to determine if we need to alloc a
	 * new page.
	 */
	gve_dec_pagecnt_bias(page_info);

	return skb;
}

static struct sk_buff *
gve_rx_qpl(struct device *dev, struct net_device *netdev,
	   struct gve_rx_ring *rx, struct gve_rx_slot_page_info *page_info,
	   u16 len, struct napi_struct *napi,
	   union gve_rx_data_slot *data_slot)
{
	struct gve_rx_ctx *ctx = &rx->ctx;
	struct sk_buff *skb;

	/* if raw_addressing mode is not enabled gvnic can only receive into
	 * registered segments. If the buffer can't be recycled, our only
	 * choice is to copy the data out of it so that we can return it to the
	 * device.
	 */
	if (ctx->reuse_frags) {
		skb = gve_rx_add_frags(napi, page_info, rx->packet_buffer_size, len, ctx);
		/* No point in recycling if we didn't get the skb */
		if (skb) {
			/* Make sure that the page isn't freed. */
			gve_dec_pagecnt_bias(page_info);
			gve_rx_flip_buff(page_info, &data_slot->qpl_offset);
		}
	} else {
		const u16 padding = gve_rx_ctx_padding(ctx);

		skb = gve_rx_copy(netdev, napi, page_info, len, padding, ctx);
		if (skb) {
			u64_stats_update_begin(&rx->statss);
			rx->rx_frag_copy_cnt++;
			u64_stats_update_end(&rx->statss);
		}
	}
	return skb;
}

#define GVE_PKTCONT_BIT_IS_SET(x) (GVE_RXF_PKT_CONT & (x))
static u16 gve_rx_get_fragment_size(struct gve_rx_ctx *ctx, struct gve_rx_desc *desc)
{
	return be16_to_cpu(desc->len) - gve_rx_ctx_padding(ctx);
}

static bool gve_rx_ctx_init(struct gve_rx_ctx *ctx, struct gve_rx_ring *rx)
{
	bool qpl_mode = !rx->data.raw_addressing, packet_size_error = false;
	bool buffer_error = false, desc_error = false, seqno_error = false;
	struct gve_rx_slot_page_info *page_info;
	struct gve_priv *priv = rx->gve;
	u32 idx = rx->cnt & rx->mask;
	bool reuse_frags, can_flip;
	struct gve_rx_desc *desc;
	u16 packet_size = 0;
	u16 n_frags = 0;
	int recycle;

	/** In QPL mode, we only flip buffers when all buffers containing the packet
	 * can be flipped. RDA can_flip decisions will be made later, per frag.
	 */
	can_flip = qpl_mode;
	reuse_frags = can_flip;
	do {
		u16 frag_size;

		n_frags++;
		desc = &rx->desc.desc_ring[idx];
		desc_error = unlikely(desc->flags_seq & GVE_RXF_ERR) || desc_error;
		if (GVE_SEQNO(desc->flags_seq) != rx->desc.seqno) {
			seqno_error = true;
			netdev_warn(priv->dev,
				    "RX seqno error: want=%d, got=%d, dropping packet and scheduling reset.",
				    rx->desc.seqno, GVE_SEQNO(desc->flags_seq));
		}
		frag_size = be16_to_cpu(desc->len);
		packet_size += frag_size;
		if (frag_size > rx->packet_buffer_size) {
			packet_size_error = true;
			netdev_warn(priv->dev,
				    "RX fragment error: packet_buffer_size=%d, frag_size=%d, droping packet.",
				    rx->packet_buffer_size, be16_to_cpu(desc->len));
		}
		page_info = &rx->data.page_info[idx];
		if (can_flip) {
			recycle = gve_rx_can_recycle_buffer(page_info);
			reuse_frags = reuse_frags && recycle > 0;
			buffer_error = buffer_error || unlikely(recycle < 0);
		}
		idx = (idx + 1) & rx->mask;
		rx->desc.seqno = gve_next_seqno(rx->desc.seqno);
	} while (GVE_PKTCONT_BIT_IS_SET(desc->flags_seq));

	prefetch(rx->desc.desc_ring + idx);

	ctx->curr_frag_cnt = 0;
	ctx->total_expected_size = packet_size - GVE_RX_PAD;
	ctx->expected_frag_cnt = n_frags;
	ctx->skb_head = NULL;
	ctx->reuse_frags = reuse_frags;

	if (ctx->expected_frag_cnt > 1) {
		u64_stats_update_begin(&rx->statss);
		rx->rx_cont_packet_cnt++;
		u64_stats_update_end(&rx->statss);
	}
	if (ctx->total_expected_size > priv->rx_copybreak && !ctx->reuse_frags && qpl_mode) {
		u64_stats_update_begin(&rx->statss);
		rx->rx_copied_pkt++;
		u64_stats_update_end(&rx->statss);
	}

	if (unlikely(buffer_error || seqno_error || packet_size_error)) {
		gve_schedule_reset(priv);
		return false;
	}

	if (unlikely(desc_error)) {
		u64_stats_update_begin(&rx->statss);
		rx->rx_desc_err_dropped_pkt++;
		u64_stats_update_end(&rx->statss);
		return false;
	}
	return true;
}

static struct sk_buff *gve_rx_skb(struct gve_priv *priv, struct gve_rx_ring *rx,
				  struct gve_rx_slot_page_info *page_info, struct napi_struct *napi,
				  u16 len, union gve_rx_data_slot *data_slot)
{
	struct net_device *netdev = priv->dev;
	struct gve_rx_ctx *ctx = &rx->ctx;
	struct sk_buff *skb = NULL;

	if (len <= priv->rx_copybreak && ctx->expected_frag_cnt == 1) {
		/* Just copy small packets */
		skb = gve_rx_copy(netdev, napi, page_info, len, GVE_RX_PAD, ctx);
		if (skb) {
			u64_stats_update_begin(&rx->statss);
			rx->rx_copied_pkt++;
			rx->rx_frag_copy_cnt++;
			rx->rx_copybreak_pkt++;
			u64_stats_update_end(&rx->statss);
		}
	} else {
		if (rx->data.raw_addressing) {
			int recycle = gve_rx_can_recycle_buffer(page_info);

			if (unlikely(recycle < 0)) {
				gve_schedule_reset(priv);
				return NULL;
			}
			page_info->can_flip = recycle;
			if (page_info->can_flip) {
				u64_stats_update_begin(&rx->statss);
				rx->rx_frag_flip_cnt++;
				u64_stats_update_end(&rx->statss);
			}
			skb = gve_rx_raw_addressing(&priv->pdev->dev, netdev,
						    page_info, len, napi,
						    data_slot,
						    rx->packet_buffer_size, ctx);
		} else {
			if (ctx->reuse_frags) {
				u64_stats_update_begin(&rx->statss);
				rx->rx_frag_flip_cnt++;
				u64_stats_update_end(&rx->statss);
			}
			skb = gve_rx_qpl(&priv->pdev->dev, netdev, rx,
					 page_info, len, napi, data_slot);
		}
	}
	return skb;
}

static bool gve_rx(struct gve_rx_ring *rx, netdev_features_t feat,
		   u64 *packet_size_bytes, u32 *work_done)
{
	struct gve_rx_slot_page_info *page_info;
	struct gve_rx_ctx *ctx = &rx->ctx;
	union gve_rx_data_slot *data_slot;
	struct gve_priv *priv = rx->gve;
	struct gve_rx_desc *first_desc;
	struct sk_buff *skb = NULL;
	struct gve_rx_desc *desc;
	struct napi_struct *napi;
	dma_addr_t page_bus;
	u32 work_cnt = 0;
	void *va;
	u32 idx;
	u16 len;

	idx = rx->cnt & rx->mask;
	first_desc = &rx->desc.desc_ring[idx];
	desc = first_desc;
	napi = &priv->ntfy_blocks[rx->ntfy_id].napi;

	if (unlikely(!gve_rx_ctx_init(ctx, rx)))
		goto skb_alloc_fail;

	while (ctx->curr_frag_cnt < ctx->expected_frag_cnt) {
		/* Prefetch two packet buffers ahead, we will need it soon. */
		page_info = &rx->data.page_info[(idx + 2) & rx->mask];
		va = page_info->page_address + page_info->page_offset;

		prefetch(page_info->page); /* Kernel page struct. */
		prefetch(va);              /* Packet header. */
		prefetch(va + 64);         /* Next cacheline too. */

		len = gve_rx_get_fragment_size(ctx, desc);

		page_info = &rx->data.page_info[idx];
		data_slot = &rx->data.data_ring[idx];
		page_bus = rx->data.raw_addressing ?
			   be64_to_cpu(data_slot->addr) - page_info->page_offset :
			   rx->data.qpl->page_buses[idx];
		dma_sync_single_for_cpu(&priv->pdev->dev, page_bus, PAGE_SIZE, DMA_FROM_DEVICE);

		skb = gve_rx_skb(priv, rx, page_info, napi, len, data_slot);
		if (!skb) {
			u64_stats_update_begin(&rx->statss);
			rx->rx_skb_alloc_fail++;
			u64_stats_update_end(&rx->statss);
			goto skb_alloc_fail;
		}

		ctx->curr_frag_cnt++;
		rx->cnt++;
		idx = rx->cnt & rx->mask;
		work_cnt++;
		desc = &rx->desc.desc_ring[idx];
	}

	if (likely(feat & NETIF_F_RXCSUM)) {
		/* NIC passes up the partial sum */
		if (first_desc->csum)
			skb->ip_summed = CHECKSUM_COMPLETE;
		else
			skb->ip_summed = CHECKSUM_NONE;
		skb->csum = csum_unfold(first_desc->csum);
	}

	/* parse flags & pass relevant info up */
	if (likely(feat & NETIF_F_RXHASH) &&
	    gve_needs_rss(first_desc->flags_seq))
		skb_set_hash(skb, be32_to_cpu(first_desc->rss_hash),
			     gve_rss_type(first_desc->flags_seq));

	*packet_size_bytes = skb->len + (skb->protocol ? ETH_HLEN : 0);
	*work_done = work_cnt;
	if (skb_is_nonlinear(skb))
		napi_gro_frags(napi);
	else
		napi_gro_receive(napi, skb);

	gve_rx_ctx_clear(ctx);
	return true;

skb_alloc_fail:
	if (napi->skb)
		napi_free_frags(napi);
	*packet_size_bytes = 0;
	*work_done = ctx->expected_frag_cnt;
	while (ctx->curr_frag_cnt < ctx->expected_frag_cnt) {
		rx->cnt++;
		ctx->curr_frag_cnt++;
	}
	gve_rx_ctx_clear(ctx);
	return false;
}

bool gve_rx_work_pending(struct gve_rx_ring *rx)
{
	struct gve_rx_desc *desc;
	__be16 flags_seq;
	u32 next_idx;

	next_idx = rx->cnt & rx->mask;
	desc = rx->desc.desc_ring + next_idx;

	flags_seq = desc->flags_seq;

	return (GVE_SEQNO(flags_seq) == rx->desc.seqno);
}

static bool gve_rx_refill_buffers(struct gve_priv *priv, struct gve_rx_ring *rx)
{
	int refill_target = rx->mask + 1;
	u32 fill_cnt = rx->fill_cnt;

	while (fill_cnt - rx->cnt < refill_target) {
		struct gve_rx_slot_page_info *page_info;
		u32 idx = fill_cnt & rx->mask;

		page_info = &rx->data.page_info[idx];
		if (page_info->can_flip) {
			/* The other half of the page is free because it was
			 * free when we processed the descriptor. Flip to it.
			 */
			union gve_rx_data_slot *data_slot =
						&rx->data.data_ring[idx];

			gve_rx_flip_buff(page_info, &data_slot->addr);
			page_info->can_flip = 0;
		} else {
			/* It is possible that the networking stack has already
			 * finished processing all outstanding packets in the buffer
			 * and it can be reused.
			 * Flipping is unnecessary here - if the networking stack still
			 * owns half the page it is impossible to tell which half. Either
			 * the whole page is free or it needs to be replaced.
			 */
			int recycle = gve_rx_can_recycle_buffer(page_info);

			if (recycle < 0) {
				if (!rx->data.raw_addressing)
					gve_schedule_reset(priv);
				return false;
			}
			if (!recycle) {
				/* We can't reuse the buffer - alloc a new one*/
				union gve_rx_data_slot *data_slot =
						&rx->data.data_ring[idx];
				struct device *dev = &priv->pdev->dev;
				gve_rx_free_buffer(dev, page_info, data_slot);
				page_info->page = NULL;
				if (gve_rx_alloc_buffer(priv, dev, page_info,
							data_slot)) {
					u64_stats_update_begin(&rx->statss);
					rx->rx_buf_alloc_fail++;
					u64_stats_update_end(&rx->statss);
					break;
				}
			}
		}
		fill_cnt++;
	}
	rx->fill_cnt = fill_cnt;
	return true;
}

static int gve_clean_rx_done(struct gve_rx_ring *rx, int budget,
			     netdev_features_t feat)
{
	u32 work_done = 0, total_packet_cnt = 0, ok_packet_cnt = 0;
	struct gve_priv *priv = rx->gve;
	u32 idx = rx->cnt & rx->mask;
	struct gve_rx_desc *desc;
	u64 bytes = 0;

	desc = &rx->desc.desc_ring[idx];
	while ((GVE_SEQNO(desc->flags_seq) == rx->desc.seqno) &&
	       work_done < budget) {
		u64 packet_size_bytes = 0;
		u32 work_cnt = 0;
		bool dropped;

		netif_info(priv, rx_status, priv->dev,
			   "[%d] idx=%d desc=%p desc->flags_seq=0x%x\n",
			   rx->q_num, idx, desc, desc->flags_seq);
		netif_info(priv, rx_status, priv->dev,
			   "[%d] seqno=%d rx->desc.seqno=%d\n",
			   rx->q_num, GVE_SEQNO(desc->flags_seq),
			   rx->desc.seqno);

		dropped = !gve_rx(rx, feat, &packet_size_bytes, &work_cnt);
		if (!dropped) {
			bytes += packet_size_bytes;
			ok_packet_cnt++;
		}
		total_packet_cnt++;
		idx = rx->cnt & rx->mask;
		desc = &rx->desc.desc_ring[idx];
		work_done += work_cnt;
	}

	if (!work_done && rx->fill_cnt - rx->cnt > rx->db_threshold)
		return 0;

	if (work_done) {
		u64_stats_update_begin(&rx->statss);
		rx->rpackets += ok_packet_cnt;
		rx->rbytes += bytes;
		u64_stats_update_end(&rx->statss);
	}

	/* restock ring slots */
	if (!rx->data.raw_addressing) {
		/* In QPL mode buffs are refilled as the desc are processed */
		rx->fill_cnt += work_done;
	} else if (rx->fill_cnt - rx->cnt <= rx->db_threshold) {
		/* In raw addressing mode buffs are only refilled if the avail
		 * falls below a threshold.
		 */
		if (!gve_rx_refill_buffers(priv, rx))
			return 0;

		/* If we were not able to completely refill buffers, we'll want
		 * to schedule this queue for work again to refill buffers.
		 */
		if (rx->fill_cnt - rx->cnt <= rx->db_threshold) {
			gve_rx_write_doorbell(priv, rx);
			return budget;
		}
	}

	gve_rx_write_doorbell(priv, rx);
	return total_packet_cnt;
}

int gve_rx_poll(struct gve_notify_block *block, int budget)
{
	struct gve_rx_ring *rx = block->rx;
	netdev_features_t feat;
	int work_done = 0;

	feat = block->napi.dev->features;

	/* If budget is 0, do all the work */
	if (budget == 0)
		budget = INT_MAX;

	if (budget > 0)
		work_done = gve_clean_rx_done(rx, budget, feat);

	return work_done;
}
