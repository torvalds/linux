// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include <linux/module.h>
#include <linux/iommu.h>
#include "efx.h"
#include "nic.h"
#include "rx_common.h"

/* This is the percentage fill level below which new RX descriptors
 * will be added to the RX descriptor ring.
 */
static unsigned int rx_refill_threshold;
module_param(rx_refill_threshold, uint, 0444);
MODULE_PARM_DESC(rx_refill_threshold,
		 "RX descriptor ring refill threshold (%)");

/* Number of RX buffers to recycle pages for.  When creating the RX page recycle
 * ring, this number is divided by the number of buffers per page to calculate
 * the number of pages to store in the RX page recycle ring.
 */
#define EFX_RECYCLE_RING_SIZE_IOMMU 4096
#define EFX_RECYCLE_RING_SIZE_NOIOMMU (2 * EFX_RX_PREFERRED_BATCH)

/* RX maximum head room required.
 *
 * This must be at least 1 to prevent overflow, plus one packet-worth
 * to allow pipelined receives.
 */
#define EFX_RXD_HEAD_ROOM (1 + EFX_RX_MAX_FRAGS)

/* Check the RX page recycle ring for a page that can be reused. */
static struct page *efx_reuse_page(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	struct efx_rx_page_state *state;
	unsigned int index;
	struct page *page;

	index = rx_queue->page_remove & rx_queue->page_ptr_mask;
	page = rx_queue->page_ring[index];
	if (page == NULL)
		return NULL;

	rx_queue->page_ring[index] = NULL;
	/* page_remove cannot exceed page_add. */
	if (rx_queue->page_remove != rx_queue->page_add)
		++rx_queue->page_remove;

	/* If page_count is 1 then we hold the only reference to this page. */
	if (page_count(page) == 1) {
		++rx_queue->page_recycle_count;
		return page;
	} else {
		state = page_address(page);
		dma_unmap_page(&efx->pci_dev->dev, state->dma_addr,
			       PAGE_SIZE << efx->rx_buffer_order,
			       DMA_FROM_DEVICE);
		put_page(page);
		++rx_queue->page_recycle_failed;
	}

	return NULL;
}

/* Attempt to recycle the page if there is an RX recycle ring; the page can
 * only be added if this is the final RX buffer, to prevent pages being used in
 * the descriptor ring and appearing in the recycle ring simultaneously.
 */
static void efx_recycle_rx_page(struct efx_channel *channel,
				struct efx_rx_buffer *rx_buf)
{
	struct efx_rx_queue *rx_queue = efx_channel_get_rx_queue(channel);
	struct efx_nic *efx = rx_queue->efx;
	struct page *page = rx_buf->page;
	unsigned int index;

	/* Only recycle the page after processing the final buffer. */
	if (!(rx_buf->flags & EFX_RX_BUF_LAST_IN_PAGE))
		return;

	index = rx_queue->page_add & rx_queue->page_ptr_mask;
	if (rx_queue->page_ring[index] == NULL) {
		unsigned int read_index = rx_queue->page_remove &
			rx_queue->page_ptr_mask;

		/* The next slot in the recycle ring is available, but
		 * increment page_remove if the read pointer currently
		 * points here.
		 */
		if (read_index == index)
			++rx_queue->page_remove;
		rx_queue->page_ring[index] = page;
		++rx_queue->page_add;
		return;
	}
	++rx_queue->page_recycle_full;
	efx_unmap_rx_buffer(efx, rx_buf);
	put_page(rx_buf->page);
}

/* Recycle the pages that are used by buffers that have just been received. */
void efx_recycle_rx_pages(struct efx_channel *channel,
			  struct efx_rx_buffer *rx_buf,
			  unsigned int n_frags)
{
	struct efx_rx_queue *rx_queue = efx_channel_get_rx_queue(channel);

	do {
		efx_recycle_rx_page(channel, rx_buf);
		rx_buf = efx_rx_buf_next(rx_queue, rx_buf);
	} while (--n_frags);
}

void efx_discard_rx_packet(struct efx_channel *channel,
			   struct efx_rx_buffer *rx_buf,
			   unsigned int n_frags)
{
	struct efx_rx_queue *rx_queue = efx_channel_get_rx_queue(channel);

	efx_recycle_rx_pages(channel, rx_buf, n_frags);

	efx_free_rx_buffers(rx_queue, rx_buf, n_frags);
}

static void efx_init_rx_recycle_ring(struct efx_rx_queue *rx_queue)
{
	unsigned int bufs_in_recycle_ring, page_ring_size;
	struct efx_nic *efx = rx_queue->efx;

	/* Set the RX recycle ring size */
#ifdef CONFIG_PPC64
	bufs_in_recycle_ring = EFX_RECYCLE_RING_SIZE_IOMMU;
#else
	if (iommu_present(&pci_bus_type))
		bufs_in_recycle_ring = EFX_RECYCLE_RING_SIZE_IOMMU;
	else
		bufs_in_recycle_ring = EFX_RECYCLE_RING_SIZE_NOIOMMU;
#endif /* CONFIG_PPC64 */

	page_ring_size = roundup_pow_of_two(bufs_in_recycle_ring /
					    efx->rx_bufs_per_page);
	rx_queue->page_ring = kcalloc(page_ring_size,
				      sizeof(*rx_queue->page_ring), GFP_KERNEL);
	if (!rx_queue->page_ring)
		rx_queue->page_ptr_mask = 0;
	else
		rx_queue->page_ptr_mask = page_ring_size - 1;
}

static void efx_fini_rx_recycle_ring(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	int i;

	/* Unmap and release the pages in the recycle ring. Remove the ring. */
	for (i = 0; i <= rx_queue->page_ptr_mask; i++) {
		struct page *page = rx_queue->page_ring[i];
		struct efx_rx_page_state *state;

		if (page == NULL)
			continue;

		state = page_address(page);
		dma_unmap_page(&efx->pci_dev->dev, state->dma_addr,
			       PAGE_SIZE << efx->rx_buffer_order,
			       DMA_FROM_DEVICE);
		put_page(page);
	}
	kfree(rx_queue->page_ring);
	rx_queue->page_ring = NULL;
}

static void efx_fini_rx_buffer(struct efx_rx_queue *rx_queue,
			       struct efx_rx_buffer *rx_buf)
{
	/* Release the page reference we hold for the buffer. */
	if (rx_buf->page)
		put_page(rx_buf->page);

	/* If this is the last buffer in a page, unmap and free it. */
	if (rx_buf->flags & EFX_RX_BUF_LAST_IN_PAGE) {
		efx_unmap_rx_buffer(rx_queue->efx, rx_buf);
		efx_free_rx_buffers(rx_queue, rx_buf, 1);
	}
	rx_buf->page = NULL;
}

int efx_probe_rx_queue(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	unsigned int entries;
	int rc;

	/* Create the smallest power-of-two aligned ring */
	entries = max(roundup_pow_of_two(efx->rxq_entries), EFX_MIN_DMAQ_SIZE);
	EFX_WARN_ON_PARANOID(entries > EFX_MAX_DMAQ_SIZE);
	rx_queue->ptr_mask = entries - 1;

	netif_dbg(efx, probe, efx->net_dev,
		  "creating RX queue %d size %#x mask %#x\n",
		  efx_rx_queue_index(rx_queue), efx->rxq_entries,
		  rx_queue->ptr_mask);

	/* Allocate RX buffers */
	rx_queue->buffer = kcalloc(entries, sizeof(*rx_queue->buffer),
				   GFP_KERNEL);
	if (!rx_queue->buffer)
		return -ENOMEM;

	rc = efx_nic_probe_rx(rx_queue);
	if (rc) {
		kfree(rx_queue->buffer);
		rx_queue->buffer = NULL;
	}

	return rc;
}

void efx_init_rx_queue(struct efx_rx_queue *rx_queue)
{
	unsigned int max_fill, trigger, max_trigger;
	struct efx_nic *efx = rx_queue->efx;
	int rc = 0;

	netif_dbg(rx_queue->efx, drv, rx_queue->efx->net_dev,
		  "initialising RX queue %d\n", efx_rx_queue_index(rx_queue));

	/* Initialise ptr fields */
	rx_queue->added_count = 0;
	rx_queue->notified_count = 0;
	rx_queue->removed_count = 0;
	rx_queue->min_fill = -1U;
	efx_init_rx_recycle_ring(rx_queue);

	rx_queue->page_remove = 0;
	rx_queue->page_add = rx_queue->page_ptr_mask + 1;
	rx_queue->page_recycle_count = 0;
	rx_queue->page_recycle_failed = 0;
	rx_queue->page_recycle_full = 0;

	/* Initialise limit fields */
	max_fill = efx->rxq_entries - EFX_RXD_HEAD_ROOM;
	max_trigger =
		max_fill - efx->rx_pages_per_batch * efx->rx_bufs_per_page;
	if (rx_refill_threshold != 0) {
		trigger = max_fill * min(rx_refill_threshold, 100U) / 100U;
		if (trigger > max_trigger)
			trigger = max_trigger;
	} else {
		trigger = max_trigger;
	}

	rx_queue->max_fill = max_fill;
	rx_queue->fast_fill_trigger = trigger;
	rx_queue->refill_enabled = true;

	/* Initialise XDP queue information */
	rc = xdp_rxq_info_reg(&rx_queue->xdp_rxq_info, efx->net_dev,
			      rx_queue->core_index, 0);

	if (rc) {
		netif_err(efx, rx_err, efx->net_dev,
			  "Failure to initialise XDP queue information rc=%d\n",
			  rc);
		efx->xdp_rxq_info_failed = true;
	} else {
		rx_queue->xdp_rxq_info_valid = true;
	}

	/* Set up RX descriptor ring */
	efx_nic_init_rx(rx_queue);
}

void efx_fini_rx_queue(struct efx_rx_queue *rx_queue)
{
	struct efx_rx_buffer *rx_buf;
	int i;

	netif_dbg(rx_queue->efx, drv, rx_queue->efx->net_dev,
		  "shutting down RX queue %d\n", efx_rx_queue_index(rx_queue));

	del_timer_sync(&rx_queue->slow_fill);

	/* Release RX buffers from the current read ptr to the write ptr */
	if (rx_queue->buffer) {
		for (i = rx_queue->removed_count; i < rx_queue->added_count;
		     i++) {
			unsigned int index = i & rx_queue->ptr_mask;

			rx_buf = efx_rx_buffer(rx_queue, index);
			efx_fini_rx_buffer(rx_queue, rx_buf);
		}
	}

	efx_fini_rx_recycle_ring(rx_queue);

	if (rx_queue->xdp_rxq_info_valid)
		xdp_rxq_info_unreg(&rx_queue->xdp_rxq_info);

	rx_queue->xdp_rxq_info_valid = false;
}

void efx_remove_rx_queue(struct efx_rx_queue *rx_queue)
{
	netif_dbg(rx_queue->efx, drv, rx_queue->efx->net_dev,
		  "destroying RX queue %d\n", efx_rx_queue_index(rx_queue));

	efx_nic_remove_rx(rx_queue);

	kfree(rx_queue->buffer);
	rx_queue->buffer = NULL;
}

/* Unmap a DMA-mapped page.  This function is only called for the final RX
 * buffer in a page.
 */
void efx_unmap_rx_buffer(struct efx_nic *efx,
			 struct efx_rx_buffer *rx_buf)
{
	struct page *page = rx_buf->page;

	if (page) {
		struct efx_rx_page_state *state = page_address(page);

		dma_unmap_page(&efx->pci_dev->dev,
			       state->dma_addr,
			       PAGE_SIZE << efx->rx_buffer_order,
			       DMA_FROM_DEVICE);
	}
}

void efx_free_rx_buffers(struct efx_rx_queue *rx_queue,
			 struct efx_rx_buffer *rx_buf,
			 unsigned int num_bufs)
{
	do {
		if (rx_buf->page) {
			put_page(rx_buf->page);
			rx_buf->page = NULL;
		}
		rx_buf = efx_rx_buf_next(rx_queue, rx_buf);
	} while (--num_bufs);
}

void efx_rx_slow_fill(struct timer_list *t)
{
	struct efx_rx_queue *rx_queue = from_timer(rx_queue, t, slow_fill);

	/* Post an event to cause NAPI to run and refill the queue */
	efx_nic_generate_fill_event(rx_queue);
	++rx_queue->slow_fill_count;
}

void efx_schedule_slow_fill(struct efx_rx_queue *rx_queue)
{
	mod_timer(&rx_queue->slow_fill, jiffies + msecs_to_jiffies(10));
}

/* efx_init_rx_buffers - create EFX_RX_BATCH page-based RX buffers
 *
 * @rx_queue:		Efx RX queue
 *
 * This allocates a batch of pages, maps them for DMA, and populates
 * struct efx_rx_buffers for each one. Return a negative error code or
 * 0 on success. If a single page can be used for multiple buffers,
 * then the page will either be inserted fully, or not at all.
 */
static int efx_init_rx_buffers(struct efx_rx_queue *rx_queue, bool atomic)
{
	unsigned int page_offset, index, count;
	struct efx_nic *efx = rx_queue->efx;
	struct efx_rx_page_state *state;
	struct efx_rx_buffer *rx_buf;
	dma_addr_t dma_addr;
	struct page *page;

	count = 0;
	do {
		page = efx_reuse_page(rx_queue);
		if (page == NULL) {
			page = alloc_pages(__GFP_COMP |
					   (atomic ? GFP_ATOMIC : GFP_KERNEL),
					   efx->rx_buffer_order);
			if (unlikely(page == NULL))
				return -ENOMEM;
			dma_addr =
				dma_map_page(&efx->pci_dev->dev, page, 0,
					     PAGE_SIZE << efx->rx_buffer_order,
					     DMA_FROM_DEVICE);
			if (unlikely(dma_mapping_error(&efx->pci_dev->dev,
						       dma_addr))) {
				__free_pages(page, efx->rx_buffer_order);
				return -EIO;
			}
			state = page_address(page);
			state->dma_addr = dma_addr;
		} else {
			state = page_address(page);
			dma_addr = state->dma_addr;
		}

		dma_addr += sizeof(struct efx_rx_page_state);
		page_offset = sizeof(struct efx_rx_page_state);

		do {
			index = rx_queue->added_count & rx_queue->ptr_mask;
			rx_buf = efx_rx_buffer(rx_queue, index);
			rx_buf->dma_addr = dma_addr + efx->rx_ip_align +
					   EFX_XDP_HEADROOM;
			rx_buf->page = page;
			rx_buf->page_offset = page_offset + efx->rx_ip_align +
					      EFX_XDP_HEADROOM;
			rx_buf->len = efx->rx_dma_len;
			rx_buf->flags = 0;
			++rx_queue->added_count;
			get_page(page);
			dma_addr += efx->rx_page_buf_step;
			page_offset += efx->rx_page_buf_step;
		} while (page_offset + efx->rx_page_buf_step <= PAGE_SIZE);

		rx_buf->flags = EFX_RX_BUF_LAST_IN_PAGE;
	} while (++count < efx->rx_pages_per_batch);

	return 0;
}

void efx_rx_config_page_split(struct efx_nic *efx)
{
	efx->rx_page_buf_step = ALIGN(efx->rx_dma_len + efx->rx_ip_align +
				      EFX_XDP_HEADROOM + EFX_XDP_TAILROOM,
				      EFX_RX_BUF_ALIGNMENT);
	efx->rx_bufs_per_page = efx->rx_buffer_order ? 1 :
		((PAGE_SIZE - sizeof(struct efx_rx_page_state)) /
		efx->rx_page_buf_step);
	efx->rx_buffer_truesize = (PAGE_SIZE << efx->rx_buffer_order) /
		efx->rx_bufs_per_page;
	efx->rx_pages_per_batch = DIV_ROUND_UP(EFX_RX_PREFERRED_BATCH,
					       efx->rx_bufs_per_page);
}

/* efx_fast_push_rx_descriptors - push new RX descriptors quickly
 * @rx_queue:		RX descriptor queue
 *
 * This will aim to fill the RX descriptor queue up to
 * @rx_queue->@max_fill. If there is insufficient atomic
 * memory to do so, a slow fill will be scheduled.
 *
 * The caller must provide serialisation (none is used here). In practise,
 * this means this function must run from the NAPI handler, or be called
 * when NAPI is disabled.
 */
void efx_fast_push_rx_descriptors(struct efx_rx_queue *rx_queue, bool atomic)
{
	struct efx_nic *efx = rx_queue->efx;
	unsigned int fill_level, batch_size;
	int space, rc = 0;

	if (!rx_queue->refill_enabled)
		return;

	/* Calculate current fill level, and exit if we don't need to fill */
	fill_level = (rx_queue->added_count - rx_queue->removed_count);
	EFX_WARN_ON_ONCE_PARANOID(fill_level > rx_queue->efx->rxq_entries);
	if (fill_level >= rx_queue->fast_fill_trigger)
		goto out;

	/* Record minimum fill level */
	if (unlikely(fill_level < rx_queue->min_fill)) {
		if (fill_level)
			rx_queue->min_fill = fill_level;
	}

	batch_size = efx->rx_pages_per_batch * efx->rx_bufs_per_page;
	space = rx_queue->max_fill - fill_level;
	EFX_WARN_ON_ONCE_PARANOID(space < batch_size);

	netif_vdbg(rx_queue->efx, rx_status, rx_queue->efx->net_dev,
		   "RX queue %d fast-filling descriptor ring from"
		   " level %d to level %d\n",
		   efx_rx_queue_index(rx_queue), fill_level,
		   rx_queue->max_fill);

	do {
		rc = efx_init_rx_buffers(rx_queue, atomic);
		if (unlikely(rc)) {
			/* Ensure that we don't leave the rx queue empty */
			efx_schedule_slow_fill(rx_queue);
			goto out;
		}
	} while ((space -= batch_size) >= batch_size);

	netif_vdbg(rx_queue->efx, rx_status, rx_queue->efx->net_dev,
		   "RX queue %d fast-filled descriptor ring "
		   "to level %d\n", efx_rx_queue_index(rx_queue),
		   rx_queue->added_count - rx_queue->removed_count);

 out:
	if (rx_queue->notified_count != rx_queue->added_count)
		efx_nic_notify_rx_desc(rx_queue);
}

/* Pass a received packet up through GRO.  GRO can handle pages
 * regardless of checksum state and skbs with a good checksum.
 */
void
efx_rx_packet_gro(struct efx_channel *channel, struct efx_rx_buffer *rx_buf,
		  unsigned int n_frags, u8 *eh, __wsum csum)
{
	struct napi_struct *napi = &channel->napi_str;
	struct efx_nic *efx = channel->efx;
	struct sk_buff *skb;

	skb = napi_get_frags(napi);
	if (unlikely(!skb)) {
		struct efx_rx_queue *rx_queue;

		rx_queue = efx_channel_get_rx_queue(channel);
		efx_free_rx_buffers(rx_queue, rx_buf, n_frags);
		return;
	}

	if (efx->net_dev->features & NETIF_F_RXHASH &&
	    efx_rx_buf_hash_valid(efx, eh))
		skb_set_hash(skb, efx_rx_buf_hash(efx, eh),
			     PKT_HASH_TYPE_L3);
	if (csum) {
		skb->csum = csum;
		skb->ip_summed = CHECKSUM_COMPLETE;
	} else {
		skb->ip_summed = ((rx_buf->flags & EFX_RX_PKT_CSUMMED) ?
				  CHECKSUM_UNNECESSARY : CHECKSUM_NONE);
	}
	skb->csum_level = !!(rx_buf->flags & EFX_RX_PKT_CSUM_LEVEL);

	for (;;) {
		skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags,
				   rx_buf->page, rx_buf->page_offset,
				   rx_buf->len);
		rx_buf->page = NULL;
		skb->len += rx_buf->len;
		if (skb_shinfo(skb)->nr_frags == n_frags)
			break;

		rx_buf = efx_rx_buf_next(&channel->rx_queue, rx_buf);
	}

	skb->data_len = skb->len;
	skb->truesize += n_frags * efx->rx_buffer_truesize;

	skb_record_rx_queue(skb, channel->rx_queue.core_index);

	napi_gro_frags(napi);
}

/* RSS contexts.  We're using linked lists and crappy O(n) algorithms, because
 * (a) this is an infrequent control-plane operation and (b) n is small (max 64)
 */
struct efx_rss_context *efx_alloc_rss_context_entry(struct efx_nic *efx)
{
	struct list_head *head = &efx->rss_context.list;
	struct efx_rss_context *ctx, *new;
	u32 id = 1; /* Don't use zero, that refers to the master RSS context */

	WARN_ON(!mutex_is_locked(&efx->rss_lock));

	/* Search for first gap in the numbering */
	list_for_each_entry(ctx, head, list) {
		if (ctx->user_id != id)
			break;
		id++;
		/* Check for wrap.  If this happens, we have nearly 2^32
		 * allocated RSS contexts, which seems unlikely.
		 */
		if (WARN_ON_ONCE(!id))
			return NULL;
	}

	/* Create the new entry */
	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;
	new->context_id = EFX_MCDI_RSS_CONTEXT_INVALID;
	new->rx_hash_udp_4tuple = false;

	/* Insert the new entry into the gap */
	new->user_id = id;
	list_add_tail(&new->list, &ctx->list);
	return new;
}

struct efx_rss_context *efx_find_rss_context_entry(struct efx_nic *efx, u32 id)
{
	struct list_head *head = &efx->rss_context.list;
	struct efx_rss_context *ctx;

	WARN_ON(!mutex_is_locked(&efx->rss_lock));

	list_for_each_entry(ctx, head, list)
		if (ctx->user_id == id)
			return ctx;
	return NULL;
}

void efx_free_rss_context_entry(struct efx_rss_context *ctx)
{
	list_del(&ctx->list);
	kfree(ctx);
}

void efx_set_default_rx_indir_table(struct efx_nic *efx,
				    struct efx_rss_context *ctx)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(ctx->rx_indir_table); i++)
		ctx->rx_indir_table[i] =
			ethtool_rxfh_indir_default(i, efx->rss_spread);
}

/**
 * efx_filter_is_mc_recipient - test whether spec is a multicast recipient
 * @spec: Specification to test
 *
 * Return: %true if the specification is a non-drop RX filter that
 * matches a local MAC address I/G bit value of 1 or matches a local
 * IPv4 or IPv6 address value in the respective multicast address
 * range.  Otherwise %false.
 */
bool efx_filter_is_mc_recipient(const struct efx_filter_spec *spec)
{
	if (!(spec->flags & EFX_FILTER_FLAG_RX) ||
	    spec->dmaq_id == EFX_FILTER_RX_DMAQ_ID_DROP)
		return false;

	if (spec->match_flags &
	    (EFX_FILTER_MATCH_LOC_MAC | EFX_FILTER_MATCH_LOC_MAC_IG) &&
	    is_multicast_ether_addr(spec->loc_mac))
		return true;

	if ((spec->match_flags &
	     (EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_LOC_HOST)) ==
	    (EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_LOC_HOST)) {
		if (spec->ether_type == htons(ETH_P_IP) &&
		    ipv4_is_multicast(spec->loc_host[0]))
			return true;
		if (spec->ether_type == htons(ETH_P_IPV6) &&
		    ((const u8 *)spec->loc_host)[0] == 0xff)
			return true;
	}

	return false;
}

bool efx_filter_spec_equal(const struct efx_filter_spec *left,
			   const struct efx_filter_spec *right)
{
	if ((left->match_flags ^ right->match_flags) |
	    ((left->flags ^ right->flags) &
	     (EFX_FILTER_FLAG_RX | EFX_FILTER_FLAG_TX)))
		return false;

	return memcmp(&left->outer_vid, &right->outer_vid,
		      sizeof(struct efx_filter_spec) -
		      offsetof(struct efx_filter_spec, outer_vid)) == 0;
}

u32 efx_filter_spec_hash(const struct efx_filter_spec *spec)
{
	BUILD_BUG_ON(offsetof(struct efx_filter_spec, outer_vid) & 3);
	return jhash2((const u32 *)&spec->outer_vid,
		      (sizeof(struct efx_filter_spec) -
		       offsetof(struct efx_filter_spec, outer_vid)) / 4,
		      0);
}

#ifdef CONFIG_RFS_ACCEL
bool efx_rps_check_rule(struct efx_arfs_rule *rule, unsigned int filter_idx,
			bool *force)
{
	if (rule->filter_id == EFX_ARFS_FILTER_ID_PENDING) {
		/* ARFS is currently updating this entry, leave it */
		return false;
	}
	if (rule->filter_id == EFX_ARFS_FILTER_ID_ERROR) {
		/* ARFS tried and failed to update this, so it's probably out
		 * of date.  Remove the filter and the ARFS rule entry.
		 */
		rule->filter_id = EFX_ARFS_FILTER_ID_REMOVING;
		*force = true;
		return true;
	} else if (WARN_ON(rule->filter_id != filter_idx)) { /* can't happen */
		/* ARFS has moved on, so old filter is not needed.  Since we did
		 * not mark the rule with EFX_ARFS_FILTER_ID_REMOVING, it will
		 * not be removed by efx_rps_hash_del() subsequently.
		 */
		*force = true;
		return true;
	}
	/* Remove it iff ARFS wants to. */
	return true;
}

static
struct hlist_head *efx_rps_hash_bucket(struct efx_nic *efx,
				       const struct efx_filter_spec *spec)
{
	u32 hash = efx_filter_spec_hash(spec);

	lockdep_assert_held(&efx->rps_hash_lock);
	if (!efx->rps_hash_table)
		return NULL;
	return &efx->rps_hash_table[hash % EFX_ARFS_HASH_TABLE_SIZE];
}

struct efx_arfs_rule *efx_rps_hash_find(struct efx_nic *efx,
					const struct efx_filter_spec *spec)
{
	struct efx_arfs_rule *rule;
	struct hlist_head *head;
	struct hlist_node *node;

	head = efx_rps_hash_bucket(efx, spec);
	if (!head)
		return NULL;
	hlist_for_each(node, head) {
		rule = container_of(node, struct efx_arfs_rule, node);
		if (efx_filter_spec_equal(spec, &rule->spec))
			return rule;
	}
	return NULL;
}

struct efx_arfs_rule *efx_rps_hash_add(struct efx_nic *efx,
				       const struct efx_filter_spec *spec,
				       bool *new)
{
	struct efx_arfs_rule *rule;
	struct hlist_head *head;
	struct hlist_node *node;

	head = efx_rps_hash_bucket(efx, spec);
	if (!head)
		return NULL;
	hlist_for_each(node, head) {
		rule = container_of(node, struct efx_arfs_rule, node);
		if (efx_filter_spec_equal(spec, &rule->spec)) {
			*new = false;
			return rule;
		}
	}
	rule = kmalloc(sizeof(*rule), GFP_ATOMIC);
	*new = true;
	if (rule) {
		memcpy(&rule->spec, spec, sizeof(rule->spec));
		hlist_add_head(&rule->node, head);
	}
	return rule;
}

void efx_rps_hash_del(struct efx_nic *efx, const struct efx_filter_spec *spec)
{
	struct efx_arfs_rule *rule;
	struct hlist_head *head;
	struct hlist_node *node;

	head = efx_rps_hash_bucket(efx, spec);
	if (WARN_ON(!head))
		return;
	hlist_for_each(node, head) {
		rule = container_of(node, struct efx_arfs_rule, node);
		if (efx_filter_spec_equal(spec, &rule->spec)) {
			/* Someone already reused the entry.  We know that if
			 * this check doesn't fire (i.e. filter_id == REMOVING)
			 * then the REMOVING mark was put there by our caller,
			 * because caller is holding a lock on filter table and
			 * only holders of that lock set REMOVING.
			 */
			if (rule->filter_id != EFX_ARFS_FILTER_ID_REMOVING)
				return;
			hlist_del(node);
			kfree(rule);
			return;
		}
	}
	/* We didn't find it. */
	WARN_ON(1);
}
#endif

int efx_probe_filters(struct efx_nic *efx)
{
	int rc;

	mutex_lock(&efx->mac_lock);
	down_write(&efx->filter_sem);
	rc = efx->type->filter_table_probe(efx);
	if (rc)
		goto out_unlock;

#ifdef CONFIG_RFS_ACCEL
	if (efx->type->offload_features & NETIF_F_NTUPLE) {
		struct efx_channel *channel;
		int i, success = 1;

		efx_for_each_channel(channel, efx) {
			channel->rps_flow_id =
				kcalloc(efx->type->max_rx_ip_filters,
					sizeof(*channel->rps_flow_id),
					GFP_KERNEL);
			if (!channel->rps_flow_id)
				success = 0;
			else
				for (i = 0;
				     i < efx->type->max_rx_ip_filters;
				     ++i)
					channel->rps_flow_id[i] =
						RPS_FLOW_ID_INVALID;
			channel->rfs_expire_index = 0;
			channel->rfs_filter_count = 0;
		}

		if (!success) {
			efx_for_each_channel(channel, efx)
				kfree(channel->rps_flow_id);
			efx->type->filter_table_remove(efx);
			rc = -ENOMEM;
			goto out_unlock;
		}
	}
#endif
out_unlock:
	up_write(&efx->filter_sem);
	mutex_unlock(&efx->mac_lock);
	return rc;
}

void efx_remove_filters(struct efx_nic *efx)
{
#ifdef CONFIG_RFS_ACCEL
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx) {
		cancel_delayed_work_sync(&channel->filter_work);
		kfree(channel->rps_flow_id);
		channel->rps_flow_id = NULL;
	}
#endif
	down_write(&efx->filter_sem);
	efx->type->filter_table_remove(efx);
	up_write(&efx->filter_sem);
}

#ifdef CONFIG_RFS_ACCEL

static void efx_filter_rfs_work(struct work_struct *data)
{
	struct efx_async_filter_insertion *req = container_of(data, struct efx_async_filter_insertion,
							      work);
	struct efx_nic *efx = netdev_priv(req->net_dev);
	struct efx_channel *channel = efx_get_channel(efx, req->rxq_index);
	int slot_idx = req - efx->rps_slot;
	struct efx_arfs_rule *rule;
	u16 arfs_id = 0;
	int rc;

	rc = efx->type->filter_insert(efx, &req->spec, true);
	if (rc >= 0)
		/* Discard 'priority' part of EF10+ filter ID (mcdi_filters) */
		rc %= efx->type->max_rx_ip_filters;
	if (efx->rps_hash_table) {
		spin_lock_bh(&efx->rps_hash_lock);
		rule = efx_rps_hash_find(efx, &req->spec);
		/* The rule might have already gone, if someone else's request
		 * for the same spec was already worked and then expired before
		 * we got around to our work.  In that case we have nothing
		 * tying us to an arfs_id, meaning that as soon as the filter
		 * is considered for expiry it will be removed.
		 */
		if (rule) {
			if (rc < 0)
				rule->filter_id = EFX_ARFS_FILTER_ID_ERROR;
			else
				rule->filter_id = rc;
			arfs_id = rule->arfs_id;
		}
		spin_unlock_bh(&efx->rps_hash_lock);
	}
	if (rc >= 0) {
		/* Remember this so we can check whether to expire the filter
		 * later.
		 */
		mutex_lock(&efx->rps_mutex);
		if (channel->rps_flow_id[rc] == RPS_FLOW_ID_INVALID)
			channel->rfs_filter_count++;
		channel->rps_flow_id[rc] = req->flow_id;
		mutex_unlock(&efx->rps_mutex);

		if (req->spec.ether_type == htons(ETH_P_IP))
			netif_info(efx, rx_status, efx->net_dev,
				   "steering %s %pI4:%u:%pI4:%u to queue %u [flow %u filter %d id %u]\n",
				   (req->spec.ip_proto == IPPROTO_TCP) ? "TCP" : "UDP",
				   req->spec.rem_host, ntohs(req->spec.rem_port),
				   req->spec.loc_host, ntohs(req->spec.loc_port),
				   req->rxq_index, req->flow_id, rc, arfs_id);
		else
			netif_info(efx, rx_status, efx->net_dev,
				   "steering %s [%pI6]:%u:[%pI6]:%u to queue %u [flow %u filter %d id %u]\n",
				   (req->spec.ip_proto == IPPROTO_TCP) ? "TCP" : "UDP",
				   req->spec.rem_host, ntohs(req->spec.rem_port),
				   req->spec.loc_host, ntohs(req->spec.loc_port),
				   req->rxq_index, req->flow_id, rc, arfs_id);
		channel->n_rfs_succeeded++;
	} else {
		if (req->spec.ether_type == htons(ETH_P_IP))
			netif_dbg(efx, rx_status, efx->net_dev,
				  "failed to steer %s %pI4:%u:%pI4:%u to queue %u [flow %u rc %d id %u]\n",
				  (req->spec.ip_proto == IPPROTO_TCP) ? "TCP" : "UDP",
				  req->spec.rem_host, ntohs(req->spec.rem_port),
				  req->spec.loc_host, ntohs(req->spec.loc_port),
				  req->rxq_index, req->flow_id, rc, arfs_id);
		else
			netif_dbg(efx, rx_status, efx->net_dev,
				  "failed to steer %s [%pI6]:%u:[%pI6]:%u to queue %u [flow %u rc %d id %u]\n",
				  (req->spec.ip_proto == IPPROTO_TCP) ? "TCP" : "UDP",
				  req->spec.rem_host, ntohs(req->spec.rem_port),
				  req->spec.loc_host, ntohs(req->spec.loc_port),
				  req->rxq_index, req->flow_id, rc, arfs_id);
		channel->n_rfs_failed++;
		/* We're overloading the NIC's filter tables, so let's do a
		 * chunk of extra expiry work.
		 */
		__efx_filter_rfs_expire(channel, min(channel->rfs_filter_count,
						     100u));
	}

	/* Release references */
	clear_bit(slot_idx, &efx->rps_slot_map);
	dev_put(req->net_dev);
}

int efx_filter_rfs(struct net_device *net_dev, const struct sk_buff *skb,
		   u16 rxq_index, u32 flow_id)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_async_filter_insertion *req;
	struct efx_arfs_rule *rule;
	struct flow_keys fk;
	int slot_idx;
	bool new;
	int rc;

	/* find a free slot */
	for (slot_idx = 0; slot_idx < EFX_RPS_MAX_IN_FLIGHT; slot_idx++)
		if (!test_and_set_bit(slot_idx, &efx->rps_slot_map))
			break;
	if (slot_idx >= EFX_RPS_MAX_IN_FLIGHT)
		return -EBUSY;

	if (flow_id == RPS_FLOW_ID_INVALID) {
		rc = -EINVAL;
		goto out_clear;
	}

	if (!skb_flow_dissect_flow_keys(skb, &fk, 0)) {
		rc = -EPROTONOSUPPORT;
		goto out_clear;
	}

	if (fk.basic.n_proto != htons(ETH_P_IP) && fk.basic.n_proto != htons(ETH_P_IPV6)) {
		rc = -EPROTONOSUPPORT;
		goto out_clear;
	}
	if (fk.control.flags & FLOW_DIS_IS_FRAGMENT) {
		rc = -EPROTONOSUPPORT;
		goto out_clear;
	}

	req = efx->rps_slot + slot_idx;
	efx_filter_init_rx(&req->spec, EFX_FILTER_PRI_HINT,
			   efx->rx_scatter ? EFX_FILTER_FLAG_RX_SCATTER : 0,
			   rxq_index);
	req->spec.match_flags =
		EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_IP_PROTO |
		EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_LOC_PORT |
		EFX_FILTER_MATCH_REM_HOST | EFX_FILTER_MATCH_REM_PORT;
	req->spec.ether_type = fk.basic.n_proto;
	req->spec.ip_proto = fk.basic.ip_proto;

	if (fk.basic.n_proto == htons(ETH_P_IP)) {
		req->spec.rem_host[0] = fk.addrs.v4addrs.src;
		req->spec.loc_host[0] = fk.addrs.v4addrs.dst;
	} else {
		memcpy(req->spec.rem_host, &fk.addrs.v6addrs.src,
		       sizeof(struct in6_addr));
		memcpy(req->spec.loc_host, &fk.addrs.v6addrs.dst,
		       sizeof(struct in6_addr));
	}

	req->spec.rem_port = fk.ports.src;
	req->spec.loc_port = fk.ports.dst;

	if (efx->rps_hash_table) {
		/* Add it to ARFS hash table */
		spin_lock(&efx->rps_hash_lock);
		rule = efx_rps_hash_add(efx, &req->spec, &new);
		if (!rule) {
			rc = -ENOMEM;
			goto out_unlock;
		}
		if (new)
			rule->arfs_id = efx->rps_next_id++ % RPS_NO_FILTER;
		rc = rule->arfs_id;
		/* Skip if existing or pending filter already does the right thing */
		if (!new && rule->rxq_index == rxq_index &&
		    rule->filter_id >= EFX_ARFS_FILTER_ID_PENDING)
			goto out_unlock;
		rule->rxq_index = rxq_index;
		rule->filter_id = EFX_ARFS_FILTER_ID_PENDING;
		spin_unlock(&efx->rps_hash_lock);
	} else {
		/* Without an ARFS hash table, we just use arfs_id 0 for all
		 * filters.  This means if multiple flows hash to the same
		 * flow_id, all but the most recently touched will be eligible
		 * for expiry.
		 */
		rc = 0;
	}

	/* Queue the request */
	dev_hold(req->net_dev = net_dev);
	INIT_WORK(&req->work, efx_filter_rfs_work);
	req->rxq_index = rxq_index;
	req->flow_id = flow_id;
	schedule_work(&req->work);
	return rc;
out_unlock:
	spin_unlock(&efx->rps_hash_lock);
out_clear:
	clear_bit(slot_idx, &efx->rps_slot_map);
	return rc;
}

bool __efx_filter_rfs_expire(struct efx_channel *channel, unsigned int quota)
{
	bool (*expire_one)(struct efx_nic *efx, u32 flow_id, unsigned int index);
	struct efx_nic *efx = channel->efx;
	unsigned int index, size, start;
	u32 flow_id;

	if (!mutex_trylock(&efx->rps_mutex))
		return false;
	expire_one = efx->type->filter_rfs_expire_one;
	index = channel->rfs_expire_index;
	start = index;
	size = efx->type->max_rx_ip_filters;
	while (quota) {
		flow_id = channel->rps_flow_id[index];

		if (flow_id != RPS_FLOW_ID_INVALID) {
			quota--;
			if (expire_one(efx, flow_id, index)) {
				netif_info(efx, rx_status, efx->net_dev,
					   "expired filter %d [channel %u flow %u]\n",
					   index, channel->channel, flow_id);
				channel->rps_flow_id[index] = RPS_FLOW_ID_INVALID;
				channel->rfs_filter_count--;
			}
		}
		if (++index == size)
			index = 0;
		/* If we were called with a quota that exceeds the total number
		 * of filters in the table (which shouldn't happen, but could
		 * if two callers race), ensure that we don't loop forever -
		 * stop when we've examined every row of the table.
		 */
		if (index == start)
			break;
	}

	channel->rfs_expire_index = index;
	mutex_unlock(&efx->rps_mutex);
	return true;
}

#endif /* CONFIG_RFS_ACCEL */
