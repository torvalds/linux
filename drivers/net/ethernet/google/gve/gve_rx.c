// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#include "gve.h"
#include "gve_adminq.h"
#include "gve_utils.h"
#include <linux/etherdevice.h>
#include <linux/filter.h>
#include <net/xdp.h>
#include <net/xdp_sock_drv.h>

static void gve_rx_free_buffer(struct device *dev,
			       struct gve_rx_slot_page_info *page_info,
			       union gve_rx_data_slot *data_slot)
{
	dma_addr_t dma = (dma_addr_t)(be64_to_cpu(data_slot->addr) &
				      GVE_DATA_SLOT_ADDR_PAGE_MASK);

	page_ref_sub(page_info->page, page_info->pagecnt_bias - 1);
	gve_free_page(dev, page_info->page, dma, DMA_FROM_DEVICE);
}

static void gve_rx_unfill_pages(struct gve_priv *priv,
				struct gve_rx_ring *rx,
				struct gve_rx_alloc_rings_cfg *cfg)
{
	u32 slots = rx->mask + 1;
	int i;

	if (!rx->data.page_info)
		return;

	if (rx->data.raw_addressing) {
		for (i = 0; i < slots; i++)
			gve_rx_free_buffer(&priv->pdev->dev, &rx->data.page_info[i],
					   &rx->data.data_ring[i]);
	} else {
		for (i = 0; i < slots; i++)
			page_ref_sub(rx->data.page_info[i].page,
				     rx->data.page_info[i].pagecnt_bias - 1);

		for (i = 0; i < rx->qpl_copy_pool_mask + 1; i++) {
			page_ref_sub(rx->qpl_copy_pool[i].page,
				     rx->qpl_copy_pool[i].pagecnt_bias - 1);
			put_page(rx->qpl_copy_pool[i].page);
		}
	}
	kvfree(rx->data.page_info);
	rx->data.page_info = NULL;
}

static void gve_rx_ctx_clear(struct gve_rx_ctx *ctx)
{
	ctx->skb_head = NULL;
	ctx->skb_tail = NULL;
	ctx->total_size = 0;
	ctx->frag_cnt = 0;
	ctx->drop_pkt = false;
}

static void gve_rx_init_ring_state_gqi(struct gve_rx_ring *rx)
{
	rx->desc.seqno = 1;
	rx->cnt = 0;
	gve_rx_ctx_clear(&rx->ctx);
}

static void gve_rx_reset_ring_gqi(struct gve_priv *priv, int idx)
{
	struct gve_rx_ring *rx = &priv->rx[idx];
	const u32 slots = priv->rx_desc_cnt;
	size_t size;

	/* Reset desc ring */
	if (rx->desc.desc_ring) {
		size = slots * sizeof(rx->desc.desc_ring[0]);
		memset(rx->desc.desc_ring, 0, size);
	}

	/* Reset q_resources */
	if (rx->q_resources)
		memset(rx->q_resources, 0, sizeof(*rx->q_resources));

	gve_rx_init_ring_state_gqi(rx);
}

void gve_rx_stop_ring_gqi(struct gve_priv *priv, int idx)
{
	int ntfy_idx = gve_rx_idx_to_ntfy(priv, idx);

	if (!gve_rx_was_added_to_block(priv, idx))
		return;

	gve_remove_napi(priv, ntfy_idx);
	gve_rx_remove_from_block(priv, idx);
	gve_rx_reset_ring_gqi(priv, idx);
}

void gve_rx_free_ring_gqi(struct gve_priv *priv, struct gve_rx_ring *rx,
			  struct gve_rx_alloc_rings_cfg *cfg)
{
	struct device *dev = &priv->pdev->dev;
	u32 slots = rx->mask + 1;
	int idx = rx->q_num;
	size_t bytes;
	u32 qpl_id;

	if (rx->desc.desc_ring) {
		bytes = sizeof(struct gve_rx_desc) * cfg->ring_size;
		dma_free_coherent(dev, bytes, rx->desc.desc_ring, rx->desc.bus);
		rx->desc.desc_ring = NULL;
	}

	if (rx->q_resources) {
		dma_free_coherent(dev, sizeof(*rx->q_resources),
				  rx->q_resources, rx->q_resources_bus);
		rx->q_resources = NULL;
	}

	gve_rx_unfill_pages(priv, rx, cfg);

	if (rx->data.data_ring) {
		bytes = sizeof(*rx->data.data_ring) * slots;
		dma_free_coherent(dev, bytes, rx->data.data_ring,
				  rx->data.data_bus);
		rx->data.data_ring = NULL;
	}

	kvfree(rx->qpl_copy_pool);
	rx->qpl_copy_pool = NULL;

	if (rx->data.qpl) {
		qpl_id = gve_get_rx_qpl_id(cfg->qcfg_tx, idx);
		gve_free_queue_page_list(priv, rx->data.qpl, qpl_id);
		rx->data.qpl = NULL;
	}

	netif_dbg(priv, drv, priv->dev, "freed rx ring %d\n", idx);
}

static void gve_setup_rx_buffer(struct gve_rx_ring *rx,
				struct gve_rx_slot_page_info *page_info,
				dma_addr_t addr, struct page *page,
				__be64 *slot_addr)
{
	page_info->page = page;
	page_info->page_offset = 0;
	page_info->page_address = page_address(page);
	page_info->buf_size = rx->packet_buffer_size;
	*slot_addr = cpu_to_be64(addr);
	/* The page already has 1 ref */
	page_ref_add(page, INT_MAX - 1);
	page_info->pagecnt_bias = INT_MAX;
}

static int gve_rx_alloc_buffer(struct gve_priv *priv, struct device *dev,
			       struct gve_rx_slot_page_info *page_info,
			       union gve_rx_data_slot *data_slot,
			       struct gve_rx_ring *rx)
{
	struct page *page;
	dma_addr_t dma;
	int err;

	err = gve_alloc_page(priv, dev, &page, &dma, DMA_FROM_DEVICE,
			     GFP_ATOMIC);
	if (err) {
		u64_stats_update_begin(&rx->statss);
		rx->rx_buf_alloc_fail++;
		u64_stats_update_end(&rx->statss);
		return err;
	}

	gve_setup_rx_buffer(rx, page_info, dma, page, &data_slot->addr);
	return 0;
}

static int gve_rx_prefill_pages(struct gve_rx_ring *rx,
				struct gve_rx_alloc_rings_cfg *cfg)
{
	struct gve_priv *priv = rx->gve;
	u32 slots;
	int err;
	int i;
	int j;

	/* Allocate one page per Rx queue slot. Each page is split into two
	 * packet buffers, when possible we "page flip" between the two.
	 */
	slots = rx->mask + 1;

	rx->data.page_info = kvzalloc(slots *
				      sizeof(*rx->data.page_info), GFP_KERNEL);
	if (!rx->data.page_info)
		return -ENOMEM;

	for (i = 0; i < slots; i++) {
		if (!rx->data.raw_addressing) {
			struct page *page = rx->data.qpl->pages[i];
			dma_addr_t addr = i * PAGE_SIZE;

			gve_setup_rx_buffer(rx, &rx->data.page_info[i], addr,
					    page,
					    &rx->data.data_ring[i].qpl_offset);
			continue;
		}
		err = gve_rx_alloc_buffer(priv, &priv->pdev->dev,
					  &rx->data.page_info[i],
					  &rx->data.data_ring[i], rx);
		if (err)
			goto alloc_err_rda;
	}

	if (!rx->data.raw_addressing) {
		for (j = 0; j < rx->qpl_copy_pool_mask + 1; j++) {
			struct page *page = alloc_page(GFP_KERNEL);

			if (!page) {
				err = -ENOMEM;
				goto alloc_err_qpl;
			}

			rx->qpl_copy_pool[j].page = page;
			rx->qpl_copy_pool[j].page_offset = 0;
			rx->qpl_copy_pool[j].page_address = page_address(page);
			rx->qpl_copy_pool[j].buf_size = rx->packet_buffer_size;

			/* The page already has 1 ref. */
			page_ref_add(page, INT_MAX - 1);
			rx->qpl_copy_pool[j].pagecnt_bias = INT_MAX;
		}
	}

	return slots;

alloc_err_qpl:
	/* Fully free the copy pool pages. */
	while (j--) {
		page_ref_sub(rx->qpl_copy_pool[j].page,
			     rx->qpl_copy_pool[j].pagecnt_bias - 1);
		put_page(rx->qpl_copy_pool[j].page);
	}

	/* Do not fully free QPL pages - only remove the bias added in this
	 * function with gve_setup_rx_buffer.
	 */
	while (i--)
		page_ref_sub(rx->data.page_info[i].page,
			     rx->data.page_info[i].pagecnt_bias - 1);

	return err;

alloc_err_rda:
	while (i--)
		gve_rx_free_buffer(&priv->pdev->dev,
				   &rx->data.page_info[i],
				   &rx->data.data_ring[i]);
	return err;
}

void gve_rx_start_ring_gqi(struct gve_priv *priv, int idx)
{
	int ntfy_idx = gve_rx_idx_to_ntfy(priv, idx);

	gve_rx_add_to_block(priv, idx);
	gve_add_napi(priv, ntfy_idx, gve_napi_poll);
}

int gve_rx_alloc_ring_gqi(struct gve_priv *priv,
			  struct gve_rx_alloc_rings_cfg *cfg,
			  struct gve_rx_ring *rx,
			  int idx)
{
	struct device *hdev = &priv->pdev->dev;
	u32 slots = cfg->ring_size;
	int filled_pages;
	int qpl_page_cnt;
	u32 qpl_id = 0;
	size_t bytes;
	int err;

	netif_dbg(priv, drv, priv->dev, "allocating rx ring\n");
	/* Make sure everything is zeroed to start with */
	memset(rx, 0, sizeof(*rx));

	rx->gve = priv;
	rx->q_num = idx;
	rx->packet_buffer_size = cfg->packet_buffer_size;

	rx->mask = slots - 1;
	rx->data.raw_addressing = cfg->raw_addressing;

	/* alloc rx data ring */
	bytes = sizeof(*rx->data.data_ring) * slots;
	rx->data.data_ring = dma_alloc_coherent(hdev, bytes,
						&rx->data.data_bus,
						GFP_KERNEL);
	if (!rx->data.data_ring)
		return -ENOMEM;

	rx->qpl_copy_pool_mask = min_t(u32, U32_MAX, slots * 2) - 1;
	rx->qpl_copy_pool_head = 0;
	rx->qpl_copy_pool = kvcalloc(rx->qpl_copy_pool_mask + 1,
				     sizeof(rx->qpl_copy_pool[0]),
				     GFP_KERNEL);

	if (!rx->qpl_copy_pool) {
		err = -ENOMEM;
		goto abort_with_slots;
	}

	if (!rx->data.raw_addressing) {
		qpl_id = gve_get_rx_qpl_id(cfg->qcfg_tx, rx->q_num);
		qpl_page_cnt = cfg->ring_size;

		rx->data.qpl = gve_alloc_queue_page_list(priv, qpl_id,
							 qpl_page_cnt);
		if (!rx->data.qpl) {
			err = -ENOMEM;
			goto abort_with_copy_pool;
		}
	}

	filled_pages = gve_rx_prefill_pages(rx, cfg);
	if (filled_pages < 0) {
		err = -ENOMEM;
		goto abort_with_qpl;
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
	bytes = sizeof(struct gve_rx_desc) * cfg->ring_size;
	rx->desc.desc_ring = dma_alloc_coherent(hdev, bytes, &rx->desc.bus,
						GFP_KERNEL);
	if (!rx->desc.desc_ring) {
		err = -ENOMEM;
		goto abort_with_q_resources;
	}
	rx->db_threshold = slots / 2;
	gve_rx_init_ring_state_gqi(rx);

	gve_rx_ctx_clear(&rx->ctx);

	return 0;

abort_with_q_resources:
	dma_free_coherent(hdev, sizeof(*rx->q_resources),
			  rx->q_resources, rx->q_resources_bus);
	rx->q_resources = NULL;
abort_filled:
	gve_rx_unfill_pages(priv, rx, cfg);
abort_with_qpl:
	if (!rx->data.raw_addressing) {
		gve_free_queue_page_list(priv, rx->data.qpl, qpl_id);
		rx->data.qpl = NULL;
	}
abort_with_copy_pool:
	kvfree(rx->qpl_copy_pool);
	rx->qpl_copy_pool = NULL;
abort_with_slots:
	bytes = sizeof(*rx->data.data_ring) * slots;
	dma_free_coherent(hdev, bytes, rx->data.data_ring, rx->data.data_bus);
	rx->data.data_ring = NULL;

	return err;
}

int gve_rx_alloc_rings_gqi(struct gve_priv *priv,
			   struct gve_rx_alloc_rings_cfg *cfg)
{
	struct gve_rx_ring *rx;
	int err = 0;
	int i, j;

	rx = kvcalloc(cfg->qcfg_rx->max_queues, sizeof(struct gve_rx_ring),
		      GFP_KERNEL);
	if (!rx)
		return -ENOMEM;

	for (i = 0; i < cfg->qcfg_rx->num_queues; i++) {
		err = gve_rx_alloc_ring_gqi(priv, cfg, &rx[i], i);
		if (err) {
			netif_err(priv, drv, priv->dev,
				  "Failed to alloc rx ring=%d: err=%d\n",
				  i, err);
			goto cleanup;
		}
	}

	cfg->rx = rx;
	return 0;

cleanup:
	for (j = 0; j < i; j++)
		gve_rx_free_ring_gqi(priv, &rx[j], cfg);
	kvfree(rx);
	return err;
}

void gve_rx_free_rings_gqi(struct gve_priv *priv,
			   struct gve_rx_alloc_rings_cfg *cfg)
{
	struct gve_rx_ring *rx = cfg->rx;
	int i;

	if (!rx)
		return;

	for (i = 0; i < cfg->qcfg_rx->num_queues;  i++)
		gve_rx_free_ring_gqi(priv, &rx[i], cfg);

	kvfree(rx);
	cfg->rx = NULL;
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

static struct sk_buff *gve_rx_add_frags(struct napi_struct *napi,
					struct gve_rx_slot_page_info *page_info,
					unsigned int truesize, u16 len,
					struct gve_rx_ctx *ctx)
{
	u32 offset = page_info->page_offset + page_info->pad;
	struct sk_buff *skb = ctx->skb_tail;
	int num_frags = 0;

	if (!skb) {
		skb = napi_get_frags(napi);
		if (unlikely(!skb))
			return NULL;

		ctx->skb_head = skb;
		ctx->skb_tail = skb;
	} else {
		num_frags = skb_shinfo(ctx->skb_tail)->nr_frags;
		if (num_frags == MAX_SKB_FRAGS) {
			skb = napi_alloc_skb(napi, 0);
			if (!skb)
				return NULL;

			// We will never chain more than two SKBs: 2 * 16 * 2k > 64k
			// which is why we do not need to chain by using skb->next
			skb_shinfo(ctx->skb_tail)->frag_list = skb;

			ctx->skb_tail = skb;
			num_frags = 0;
		}
	}

	if (skb != ctx->skb_head) {
		ctx->skb_head->len += len;
		ctx->skb_head->data_len += len;
		ctx->skb_head->truesize += truesize;
	}
	skb_add_rx_frag(skb, num_frags, page_info->page,
			offset, len, truesize);

	return ctx->skb_head;
}

static void gve_rx_flip_buff(struct gve_rx_slot_page_info *page_info, __be64 *slot_addr)
{
	const __be64 offset = cpu_to_be64(GVE_DEFAULT_RX_BUFFER_OFFSET);

	/* "flip" to other packet buffer on this page */
	page_info->page_offset ^= GVE_DEFAULT_RX_BUFFER_OFFSET;
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

static struct sk_buff *gve_rx_copy_to_pool(struct gve_rx_ring *rx,
					   struct gve_rx_slot_page_info *page_info,
					   u16 len, struct napi_struct *napi)
{
	u32 pool_idx = rx->qpl_copy_pool_head & rx->qpl_copy_pool_mask;
	void *src = page_info->page_address + page_info->page_offset;
	struct gve_rx_slot_page_info *copy_page_info;
	struct gve_rx_ctx *ctx = &rx->ctx;
	bool alloc_page = false;
	struct sk_buff *skb;
	void *dst;

	copy_page_info = &rx->qpl_copy_pool[pool_idx];
	if (!copy_page_info->can_flip) {
		int recycle = gve_rx_can_recycle_buffer(copy_page_info);

		if (unlikely(recycle < 0)) {
			gve_schedule_reset(rx->gve);
			return NULL;
		}
		alloc_page = !recycle;
	}

	if (alloc_page) {
		struct gve_rx_slot_page_info alloc_page_info;
		struct page *page;

		/* The least recently used page turned out to be
		 * still in use by the kernel. Ignoring it and moving
		 * on alleviates head-of-line blocking.
		 */
		rx->qpl_copy_pool_head++;

		page = alloc_page(GFP_ATOMIC);
		if (!page)
			return NULL;

		alloc_page_info.page = page;
		alloc_page_info.page_offset = 0;
		alloc_page_info.page_address = page_address(page);
		alloc_page_info.pad = page_info->pad;

		memcpy(alloc_page_info.page_address, src, page_info->pad + len);
		skb = gve_rx_add_frags(napi, &alloc_page_info,
				       PAGE_SIZE,
				       len, ctx);

		u64_stats_update_begin(&rx->statss);
		rx->rx_frag_copy_cnt++;
		rx->rx_frag_alloc_cnt++;
		u64_stats_update_end(&rx->statss);

		return skb;
	}

	dst = copy_page_info->page_address + copy_page_info->page_offset;
	memcpy(dst, src, page_info->pad + len);
	copy_page_info->pad = page_info->pad;

	skb = gve_rx_add_frags(napi, copy_page_info,
			       copy_page_info->buf_size, len, ctx);
	if (unlikely(!skb))
		return NULL;

	gve_dec_pagecnt_bias(copy_page_info);
	copy_page_info->page_offset ^= GVE_DEFAULT_RX_BUFFER_OFFSET;

	if (copy_page_info->can_flip) {
		/* We have used both halves of this copy page, it
		 * is time for it to go to the back of the queue.
		 */
		copy_page_info->can_flip = false;
		rx->qpl_copy_pool_head++;
		prefetch(rx->qpl_copy_pool[rx->qpl_copy_pool_head & rx->qpl_copy_pool_mask].page);
	} else {
		copy_page_info->can_flip = true;
	}

	u64_stats_update_begin(&rx->statss);
	rx->rx_frag_copy_cnt++;
	u64_stats_update_end(&rx->statss);

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
	if (page_info->can_flip) {
		skb = gve_rx_add_frags(napi, page_info, page_info->buf_size,
				       len, ctx);
		/* No point in recycling if we didn't get the skb */
		if (skb) {
			/* Make sure that the page isn't freed. */
			gve_dec_pagecnt_bias(page_info);
			gve_rx_flip_buff(page_info, &data_slot->qpl_offset);
		}
	} else {
		skb = gve_rx_copy_to_pool(rx, page_info, len, napi);
	}
	return skb;
}

static struct sk_buff *gve_rx_skb(struct gve_priv *priv, struct gve_rx_ring *rx,
				  struct gve_rx_slot_page_info *page_info, struct napi_struct *napi,
				  u16 len, union gve_rx_data_slot *data_slot,
				  bool is_only_frag)
{
	struct net_device *netdev = priv->dev;
	struct gve_rx_ctx *ctx = &rx->ctx;
	struct sk_buff *skb = NULL;

	if (len <= priv->rx_copybreak && is_only_frag)  {
		/* Just copy small packets */
		skb = gve_rx_copy(netdev, napi, page_info, len);
		if (skb) {
			u64_stats_update_begin(&rx->statss);
			rx->rx_copied_pkt++;
			rx->rx_frag_copy_cnt++;
			rx->rx_copybreak_pkt++;
			u64_stats_update_end(&rx->statss);
		}
	} else {
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

		if (rx->data.raw_addressing) {
			skb = gve_rx_raw_addressing(&priv->pdev->dev, netdev,
						    page_info, len, napi,
						    data_slot,
						    page_info->buf_size, ctx);
		} else {
			skb = gve_rx_qpl(&priv->pdev->dev, netdev, rx,
					 page_info, len, napi, data_slot);
		}
	}
	return skb;
}

static int gve_xsk_pool_redirect(struct net_device *dev,
				 struct gve_rx_ring *rx,
				 void *data, int len,
				 struct bpf_prog *xdp_prog)
{
	struct xdp_buff *xdp;
	int err;

	if (rx->xsk_pool->frame_len < len)
		return -E2BIG;
	xdp = xsk_buff_alloc(rx->xsk_pool);
	if (!xdp) {
		u64_stats_update_begin(&rx->statss);
		rx->xdp_alloc_fails++;
		u64_stats_update_end(&rx->statss);
		return -ENOMEM;
	}
	xdp->data_end = xdp->data + len;
	memcpy(xdp->data, data, len);
	err = xdp_do_redirect(dev, xdp, xdp_prog);
	if (err)
		xsk_buff_free(xdp);
	return err;
}

static int gve_xdp_redirect(struct net_device *dev, struct gve_rx_ring *rx,
			    struct xdp_buff *orig, struct bpf_prog *xdp_prog)
{
	int total_len, len = orig->data_end - orig->data;
	int headroom = XDP_PACKET_HEADROOM;
	struct xdp_buff new;
	void *frame;
	int err;

	if (rx->xsk_pool)
		return gve_xsk_pool_redirect(dev, rx, orig->data,
					     len, xdp_prog);

	total_len = headroom + SKB_DATA_ALIGN(len) +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	frame = page_frag_alloc(&rx->page_cache, total_len, GFP_ATOMIC);
	if (!frame) {
		u64_stats_update_begin(&rx->statss);
		rx->xdp_alloc_fails++;
		u64_stats_update_end(&rx->statss);
		return -ENOMEM;
	}
	xdp_init_buff(&new, total_len, &rx->xdp_rxq);
	xdp_prepare_buff(&new, frame, headroom, len, false);
	memcpy(new.data, orig->data, len);

	err = xdp_do_redirect(dev, &new, xdp_prog);
	if (err)
		page_frag_free(frame);

	return err;
}

static void gve_xdp_done(struct gve_priv *priv, struct gve_rx_ring *rx,
			 struct xdp_buff *xdp, struct bpf_prog *xprog,
			 int xdp_act)
{
	struct gve_tx_ring *tx;
	int tx_qid;
	int err;

	switch (xdp_act) {
	case XDP_ABORTED:
	case XDP_DROP:
	default:
		break;
	case XDP_TX:
		tx_qid = gve_xdp_tx_queue_id(priv, rx->q_num);
		tx = &priv->tx[tx_qid];
		spin_lock(&tx->xdp_lock);
		err = gve_xdp_xmit_one(priv, tx, xdp->data,
				       xdp->data_end - xdp->data, NULL);
		spin_unlock(&tx->xdp_lock);

		if (unlikely(err)) {
			u64_stats_update_begin(&rx->statss);
			rx->xdp_tx_errors++;
			u64_stats_update_end(&rx->statss);
		}
		break;
	case XDP_REDIRECT:
		err = gve_xdp_redirect(priv->dev, rx, xdp, xprog);

		if (unlikely(err)) {
			u64_stats_update_begin(&rx->statss);
			rx->xdp_redirect_errors++;
			u64_stats_update_end(&rx->statss);
		}
		break;
	}
	u64_stats_update_begin(&rx->statss);
	if ((u32)xdp_act < GVE_XDP_ACTIONS)
		rx->xdp_actions[xdp_act]++;
	u64_stats_update_end(&rx->statss);
}

#define GVE_PKTCONT_BIT_IS_SET(x) (GVE_RXF_PKT_CONT & (x))
static void gve_rx(struct gve_rx_ring *rx, netdev_features_t feat,
		   struct gve_rx_desc *desc, u32 idx,
		   struct gve_rx_cnts *cnts)
{
	bool is_last_frag = !GVE_PKTCONT_BIT_IS_SET(desc->flags_seq);
	struct gve_rx_slot_page_info *page_info;
	u16 frag_size = be16_to_cpu(desc->len);
	struct gve_rx_ctx *ctx = &rx->ctx;
	union gve_rx_data_slot *data_slot;
	struct gve_priv *priv = rx->gve;
	struct sk_buff *skb = NULL;
	struct bpf_prog *xprog;
	struct xdp_buff xdp;
	dma_addr_t page_bus;
	void *va;

	u16 len = frag_size;
	struct napi_struct *napi = &priv->ntfy_blocks[rx->ntfy_id].napi;
	bool is_first_frag = ctx->frag_cnt == 0;

	bool is_only_frag = is_first_frag && is_last_frag;

	if (unlikely(ctx->drop_pkt))
		goto finish_frag;

	if (desc->flags_seq & GVE_RXF_ERR) {
		ctx->drop_pkt = true;
		cnts->desc_err_pkt_cnt++;
		napi_free_frags(napi);
		goto finish_frag;
	}

	if (unlikely(frag_size > rx->packet_buffer_size)) {
		netdev_warn(priv->dev, "Unexpected frag size %d, can't exceed %d, scheduling reset",
			    frag_size, rx->packet_buffer_size);
		ctx->drop_pkt = true;
		napi_free_frags(napi);
		gve_schedule_reset(rx->gve);
		goto finish_frag;
	}

	/* Prefetch two packet buffers ahead, we will need it soon. */
	page_info = &rx->data.page_info[(idx + 2) & rx->mask];
	va = page_info->page_address + page_info->page_offset;
	prefetch(page_info->page); /* Kernel page struct. */
	prefetch(va);              /* Packet header. */
	prefetch(va + 64);         /* Next cacheline too. */

	page_info = &rx->data.page_info[idx];
	data_slot = &rx->data.data_ring[idx];
	page_bus = (rx->data.raw_addressing) ?
		be64_to_cpu(data_slot->addr) - page_info->page_offset :
		rx->data.qpl->page_buses[idx];
	dma_sync_single_for_cpu(&priv->pdev->dev, page_bus,
				PAGE_SIZE, DMA_FROM_DEVICE);
	page_info->pad = is_first_frag ? GVE_RX_PAD : 0;
	len -= page_info->pad;
	frag_size -= page_info->pad;

	xprog = READ_ONCE(priv->xdp_prog);
	if (xprog && is_only_frag) {
		void *old_data;
		int xdp_act;

		xdp_init_buff(&xdp, page_info->buf_size, &rx->xdp_rxq);
		xdp_prepare_buff(&xdp, page_info->page_address +
				 page_info->page_offset, GVE_RX_PAD,
				 len, false);
		old_data = xdp.data;
		xdp_act = bpf_prog_run_xdp(xprog, &xdp);
		if (xdp_act != XDP_PASS) {
			gve_xdp_done(priv, rx, &xdp, xprog, xdp_act);
			ctx->total_size += frag_size;
			goto finish_ok_pkt;
		}

		page_info->pad += xdp.data - old_data;
		len = xdp.data_end - xdp.data;

		u64_stats_update_begin(&rx->statss);
		rx->xdp_actions[XDP_PASS]++;
		u64_stats_update_end(&rx->statss);
	}

	skb = gve_rx_skb(priv, rx, page_info, napi, len,
			 data_slot, is_only_frag);
	if (!skb) {
		u64_stats_update_begin(&rx->statss);
		rx->rx_skb_alloc_fail++;
		u64_stats_update_end(&rx->statss);

		napi_free_frags(napi);
		ctx->drop_pkt = true;
		goto finish_frag;
	}
	ctx->total_size += frag_size;

	if (is_first_frag) {
		if (likely(feat & NETIF_F_RXCSUM)) {
			/* NIC passes up the partial sum */
			if (desc->csum)
				skb->ip_summed = CHECKSUM_COMPLETE;
			else
				skb->ip_summed = CHECKSUM_NONE;
			skb->csum = csum_unfold(desc->csum);
		}

		/* parse flags & pass relevant info up */
		if (likely(feat & NETIF_F_RXHASH) &&
		    gve_needs_rss(desc->flags_seq))
			skb_set_hash(skb, be32_to_cpu(desc->rss_hash),
				     gve_rss_type(desc->flags_seq));
	}

	if (is_last_frag) {
		skb_record_rx_queue(skb, rx->q_num);
		if (skb_is_nonlinear(skb))
			napi_gro_frags(napi);
		else
			napi_gro_receive(napi, skb);
		goto finish_ok_pkt;
	}

	goto finish_frag;

finish_ok_pkt:
	cnts->ok_pkt_bytes += ctx->total_size;
	cnts->ok_pkt_cnt++;
finish_frag:
	ctx->frag_cnt++;
	if (is_last_frag) {
		cnts->total_pkt_cnt++;
		cnts->cont_pkt_cnt += (ctx->frag_cnt > 1);
		gve_rx_ctx_clear(ctx);
	}
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
							data_slot, rx)) {
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
	u64 xdp_redirects = rx->xdp_actions[XDP_REDIRECT];
	u64 xdp_txs = rx->xdp_actions[XDP_TX];
	struct gve_rx_ctx *ctx = &rx->ctx;
	struct gve_priv *priv = rx->gve;
	struct gve_rx_cnts cnts = {0};
	struct gve_rx_desc *next_desc;
	u32 idx = rx->cnt & rx->mask;
	u32 work_done = 0;

	struct gve_rx_desc *desc = &rx->desc.desc_ring[idx];

	// Exceed budget only if (and till) the inflight packet is consumed.
	while ((GVE_SEQNO(desc->flags_seq) == rx->desc.seqno) &&
	       (work_done < budget || ctx->frag_cnt)) {
		next_desc = &rx->desc.desc_ring[(idx + 1) & rx->mask];
		prefetch(next_desc);

		gve_rx(rx, feat, desc, idx, &cnts);

		rx->cnt++;
		idx = rx->cnt & rx->mask;
		desc = &rx->desc.desc_ring[idx];
		rx->desc.seqno = gve_next_seqno(rx->desc.seqno);
		work_done++;
	}

	// The device will only send whole packets.
	if (unlikely(ctx->frag_cnt)) {
		struct napi_struct *napi = &priv->ntfy_blocks[rx->ntfy_id].napi;

		napi_free_frags(napi);
		gve_rx_ctx_clear(&rx->ctx);
		netdev_warn(priv->dev, "Unexpected seq number %d with incomplete packet, expected %d, scheduling reset",
			    GVE_SEQNO(desc->flags_seq), rx->desc.seqno);
		gve_schedule_reset(rx->gve);
	}

	if (!work_done && rx->fill_cnt - rx->cnt > rx->db_threshold)
		return 0;

	if (work_done) {
		u64_stats_update_begin(&rx->statss);
		rx->rpackets += cnts.ok_pkt_cnt;
		rx->rbytes += cnts.ok_pkt_bytes;
		rx->rx_cont_packet_cnt += cnts.cont_pkt_cnt;
		rx->rx_desc_err_dropped_pkt += cnts.desc_err_pkt_cnt;
		u64_stats_update_end(&rx->statss);
	}

	if (xdp_txs != rx->xdp_actions[XDP_TX])
		gve_xdp_tx_flush(priv, rx->q_num);

	if (xdp_redirects != rx->xdp_actions[XDP_REDIRECT])
		xdp_do_flush();

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
	return cnts.total_pkt_cnt;
}

int gve_rx_poll(struct gve_notify_block *block, int budget)
{
	struct gve_rx_ring *rx = block->rx;
	netdev_features_t feat;
	int work_done = 0;

	feat = block->napi.dev->features;

	if (budget > 0)
		work_done = gve_clean_rx_done(rx, budget, feat);

	return work_done;
}
