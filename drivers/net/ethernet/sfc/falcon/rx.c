/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2013 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/socket.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/prefetch.h>
#include <linux/moduleparam.h>
#include <linux/iommu.h>
#include <net/ip.h>
#include <net/checksum.h>
#include "net_driver.h"
#include "efx.h"
#include "filter.h"
#include "nic.h"
#include "selftest.h"
#include "workarounds.h"

/* Preferred number of descriptors to fill at once */
#define EF4_RX_PREFERRED_BATCH 8U

/* Number of RX buffers to recycle pages for.  When creating the RX page recycle
 * ring, this number is divided by the number of buffers per page to calculate
 * the number of pages to store in the RX page recycle ring.
 */
#define EF4_RECYCLE_RING_SIZE_IOMMU 4096
#define EF4_RECYCLE_RING_SIZE_NOIOMMU (2 * EF4_RX_PREFERRED_BATCH)

/* Size of buffer allocated for skb header area. */
#define EF4_SKB_HEADERS  128u

/* This is the percentage fill level below which new RX descriptors
 * will be added to the RX descriptor ring.
 */
static unsigned int rx_refill_threshold;

/* Each packet can consume up to ceil(max_frame_len / buffer_size) buffers */
#define EF4_RX_MAX_FRAGS DIV_ROUND_UP(EF4_MAX_FRAME_LEN(EF4_MAX_MTU), \
				      EF4_RX_USR_BUF_SIZE)

/*
 * RX maximum head room required.
 *
 * This must be at least 1 to prevent overflow, plus one packet-worth
 * to allow pipelined receives.
 */
#define EF4_RXD_HEAD_ROOM (1 + EF4_RX_MAX_FRAGS)

static inline u8 *ef4_rx_buf_va(struct ef4_rx_buffer *buf)
{
	return page_address(buf->page) + buf->page_offset;
}

static inline u32 ef4_rx_buf_hash(struct ef4_nic *efx, const u8 *eh)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	return __le32_to_cpup((const __le32 *)(eh + efx->rx_packet_hash_offset));
#else
	const u8 *data = eh + efx->rx_packet_hash_offset;
	return (u32)data[0]	  |
	       (u32)data[1] << 8  |
	       (u32)data[2] << 16 |
	       (u32)data[3] << 24;
#endif
}

static inline struct ef4_rx_buffer *
ef4_rx_buf_next(struct ef4_rx_queue *rx_queue, struct ef4_rx_buffer *rx_buf)
{
	if (unlikely(rx_buf == ef4_rx_buffer(rx_queue, rx_queue->ptr_mask)))
		return ef4_rx_buffer(rx_queue, 0);
	else
		return rx_buf + 1;
}

static inline void ef4_sync_rx_buffer(struct ef4_nic *efx,
				      struct ef4_rx_buffer *rx_buf,
				      unsigned int len)
{
	dma_sync_single_for_cpu(&efx->pci_dev->dev, rx_buf->dma_addr, len,
				DMA_FROM_DEVICE);
}

void ef4_rx_config_page_split(struct ef4_nic *efx)
{
	efx->rx_page_buf_step = ALIGN(efx->rx_dma_len + efx->rx_ip_align,
				      EF4_RX_BUF_ALIGNMENT);
	efx->rx_bufs_per_page = efx->rx_buffer_order ? 1 :
		((PAGE_SIZE - sizeof(struct ef4_rx_page_state)) /
		 efx->rx_page_buf_step);
	efx->rx_buffer_truesize = (PAGE_SIZE << efx->rx_buffer_order) /
		efx->rx_bufs_per_page;
	efx->rx_pages_per_batch = DIV_ROUND_UP(EF4_RX_PREFERRED_BATCH,
					       efx->rx_bufs_per_page);
}

/* Check the RX page recycle ring for a page that can be reused. */
static struct page *ef4_reuse_page(struct ef4_rx_queue *rx_queue)
{
	struct ef4_nic *efx = rx_queue->efx;
	struct page *page;
	struct ef4_rx_page_state *state;
	unsigned index;

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

/**
 * ef4_init_rx_buffers - create EF4_RX_BATCH page-based RX buffers
 *
 * @rx_queue:		Efx RX queue
 *
 * This allocates a batch of pages, maps them for DMA, and populates
 * struct ef4_rx_buffers for each one. Return a negative error code or
 * 0 on success. If a single page can be used for multiple buffers,
 * then the page will either be inserted fully, or not at all.
 */
static int ef4_init_rx_buffers(struct ef4_rx_queue *rx_queue, bool atomic)
{
	struct ef4_nic *efx = rx_queue->efx;
	struct ef4_rx_buffer *rx_buf;
	struct page *page;
	unsigned int page_offset;
	struct ef4_rx_page_state *state;
	dma_addr_t dma_addr;
	unsigned index, count;

	count = 0;
	do {
		page = ef4_reuse_page(rx_queue);
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

		dma_addr += sizeof(struct ef4_rx_page_state);
		page_offset = sizeof(struct ef4_rx_page_state);

		do {
			index = rx_queue->added_count & rx_queue->ptr_mask;
			rx_buf = ef4_rx_buffer(rx_queue, index);
			rx_buf->dma_addr = dma_addr + efx->rx_ip_align;
			rx_buf->page = page;
			rx_buf->page_offset = page_offset + efx->rx_ip_align;
			rx_buf->len = efx->rx_dma_len;
			rx_buf->flags = 0;
			++rx_queue->added_count;
			get_page(page);
			dma_addr += efx->rx_page_buf_step;
			page_offset += efx->rx_page_buf_step;
		} while (page_offset + efx->rx_page_buf_step <= PAGE_SIZE);

		rx_buf->flags = EF4_RX_BUF_LAST_IN_PAGE;
	} while (++count < efx->rx_pages_per_batch);

	return 0;
}

/* Unmap a DMA-mapped page.  This function is only called for the final RX
 * buffer in a page.
 */
static void ef4_unmap_rx_buffer(struct ef4_nic *efx,
				struct ef4_rx_buffer *rx_buf)
{
	struct page *page = rx_buf->page;

	if (page) {
		struct ef4_rx_page_state *state = page_address(page);
		dma_unmap_page(&efx->pci_dev->dev,
			       state->dma_addr,
			       PAGE_SIZE << efx->rx_buffer_order,
			       DMA_FROM_DEVICE);
	}
}

static void ef4_free_rx_buffers(struct ef4_rx_queue *rx_queue,
				struct ef4_rx_buffer *rx_buf,
				unsigned int num_bufs)
{
	do {
		if (rx_buf->page) {
			put_page(rx_buf->page);
			rx_buf->page = NULL;
		}
		rx_buf = ef4_rx_buf_next(rx_queue, rx_buf);
	} while (--num_bufs);
}

/* Attempt to recycle the page if there is an RX recycle ring; the page can
 * only be added if this is the final RX buffer, to prevent pages being used in
 * the descriptor ring and appearing in the recycle ring simultaneously.
 */
static void ef4_recycle_rx_page(struct ef4_channel *channel,
				struct ef4_rx_buffer *rx_buf)
{
	struct page *page = rx_buf->page;
	struct ef4_rx_queue *rx_queue = ef4_channel_get_rx_queue(channel);
	struct ef4_nic *efx = rx_queue->efx;
	unsigned index;

	/* Only recycle the page after processing the final buffer. */
	if (!(rx_buf->flags & EF4_RX_BUF_LAST_IN_PAGE))
		return;

	index = rx_queue->page_add & rx_queue->page_ptr_mask;
	if (rx_queue->page_ring[index] == NULL) {
		unsigned read_index = rx_queue->page_remove &
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
	ef4_unmap_rx_buffer(efx, rx_buf);
	put_page(rx_buf->page);
}

static void ef4_fini_rx_buffer(struct ef4_rx_queue *rx_queue,
			       struct ef4_rx_buffer *rx_buf)
{
	/* Release the page reference we hold for the buffer. */
	if (rx_buf->page)
		put_page(rx_buf->page);

	/* If this is the last buffer in a page, unmap and free it. */
	if (rx_buf->flags & EF4_RX_BUF_LAST_IN_PAGE) {
		ef4_unmap_rx_buffer(rx_queue->efx, rx_buf);
		ef4_free_rx_buffers(rx_queue, rx_buf, 1);
	}
	rx_buf->page = NULL;
}

/* Recycle the pages that are used by buffers that have just been received. */
static void ef4_recycle_rx_pages(struct ef4_channel *channel,
				 struct ef4_rx_buffer *rx_buf,
				 unsigned int n_frags)
{
	struct ef4_rx_queue *rx_queue = ef4_channel_get_rx_queue(channel);

	do {
		ef4_recycle_rx_page(channel, rx_buf);
		rx_buf = ef4_rx_buf_next(rx_queue, rx_buf);
	} while (--n_frags);
}

static void ef4_discard_rx_packet(struct ef4_channel *channel,
				  struct ef4_rx_buffer *rx_buf,
				  unsigned int n_frags)
{
	struct ef4_rx_queue *rx_queue = ef4_channel_get_rx_queue(channel);

	ef4_recycle_rx_pages(channel, rx_buf, n_frags);

	ef4_free_rx_buffers(rx_queue, rx_buf, n_frags);
}

/**
 * ef4_fast_push_rx_descriptors - push new RX descriptors quickly
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
void ef4_fast_push_rx_descriptors(struct ef4_rx_queue *rx_queue, bool atomic)
{
	struct ef4_nic *efx = rx_queue->efx;
	unsigned int fill_level, batch_size;
	int space, rc = 0;

	if (!rx_queue->refill_enabled)
		return;

	/* Calculate current fill level, and exit if we don't need to fill */
	fill_level = (rx_queue->added_count - rx_queue->removed_count);
	EF4_BUG_ON_PARANOID(fill_level > rx_queue->efx->rxq_entries);
	if (fill_level >= rx_queue->fast_fill_trigger)
		goto out;

	/* Record minimum fill level */
	if (unlikely(fill_level < rx_queue->min_fill)) {
		if (fill_level)
			rx_queue->min_fill = fill_level;
	}

	batch_size = efx->rx_pages_per_batch * efx->rx_bufs_per_page;
	space = rx_queue->max_fill - fill_level;
	EF4_BUG_ON_PARANOID(space < batch_size);

	netif_vdbg(rx_queue->efx, rx_status, rx_queue->efx->net_dev,
		   "RX queue %d fast-filling descriptor ring from"
		   " level %d to level %d\n",
		   ef4_rx_queue_index(rx_queue), fill_level,
		   rx_queue->max_fill);


	do {
		rc = ef4_init_rx_buffers(rx_queue, atomic);
		if (unlikely(rc)) {
			/* Ensure that we don't leave the rx queue empty */
			if (rx_queue->added_count == rx_queue->removed_count)
				ef4_schedule_slow_fill(rx_queue);
			goto out;
		}
	} while ((space -= batch_size) >= batch_size);

	netif_vdbg(rx_queue->efx, rx_status, rx_queue->efx->net_dev,
		   "RX queue %d fast-filled descriptor ring "
		   "to level %d\n", ef4_rx_queue_index(rx_queue),
		   rx_queue->added_count - rx_queue->removed_count);

 out:
	if (rx_queue->notified_count != rx_queue->added_count)
		ef4_nic_notify_rx_desc(rx_queue);
}

void ef4_rx_slow_fill(struct timer_list *t)
{
	struct ef4_rx_queue *rx_queue = from_timer(rx_queue, t, slow_fill);

	/* Post an event to cause NAPI to run and refill the queue */
	ef4_nic_generate_fill_event(rx_queue);
	++rx_queue->slow_fill_count;
}

static void ef4_rx_packet__check_len(struct ef4_rx_queue *rx_queue,
				     struct ef4_rx_buffer *rx_buf,
				     int len)
{
	struct ef4_nic *efx = rx_queue->efx;
	unsigned max_len = rx_buf->len - efx->type->rx_buffer_padding;

	if (likely(len <= max_len))
		return;

	/* The packet must be discarded, but this is only a fatal error
	 * if the caller indicated it was
	 */
	rx_buf->flags |= EF4_RX_PKT_DISCARD;

	if ((len > rx_buf->len) && EF4_WORKAROUND_8071(efx)) {
		if (net_ratelimit())
			netif_err(efx, rx_err, efx->net_dev,
				  " RX queue %d seriously overlength "
				  "RX event (0x%x > 0x%x+0x%x). Leaking\n",
				  ef4_rx_queue_index(rx_queue), len, max_len,
				  efx->type->rx_buffer_padding);
		ef4_schedule_reset(efx, RESET_TYPE_RX_RECOVERY);
	} else {
		if (net_ratelimit())
			netif_err(efx, rx_err, efx->net_dev,
				  " RX queue %d overlength RX event "
				  "(0x%x > 0x%x)\n",
				  ef4_rx_queue_index(rx_queue), len, max_len);
	}

	ef4_rx_queue_channel(rx_queue)->n_rx_overlength++;
}

/* Pass a received packet up through GRO.  GRO can handle pages
 * regardless of checksum state and skbs with a good checksum.
 */
static void
ef4_rx_packet_gro(struct ef4_channel *channel, struct ef4_rx_buffer *rx_buf,
		  unsigned int n_frags, u8 *eh)
{
	struct napi_struct *napi = &channel->napi_str;
	gro_result_t gro_result;
	struct ef4_nic *efx = channel->efx;
	struct sk_buff *skb;

	skb = napi_get_frags(napi);
	if (unlikely(!skb)) {
		struct ef4_rx_queue *rx_queue;

		rx_queue = ef4_channel_get_rx_queue(channel);
		ef4_free_rx_buffers(rx_queue, rx_buf, n_frags);
		return;
	}

	if (efx->net_dev->features & NETIF_F_RXHASH)
		skb_set_hash(skb, ef4_rx_buf_hash(efx, eh),
			     PKT_HASH_TYPE_L3);
	skb->ip_summed = ((rx_buf->flags & EF4_RX_PKT_CSUMMED) ?
			  CHECKSUM_UNNECESSARY : CHECKSUM_NONE);

	for (;;) {
		skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags,
				   rx_buf->page, rx_buf->page_offset,
				   rx_buf->len);
		rx_buf->page = NULL;
		skb->len += rx_buf->len;
		if (skb_shinfo(skb)->nr_frags == n_frags)
			break;

		rx_buf = ef4_rx_buf_next(&channel->rx_queue, rx_buf);
	}

	skb->data_len = skb->len;
	skb->truesize += n_frags * efx->rx_buffer_truesize;

	skb_record_rx_queue(skb, channel->rx_queue.core_index);

	gro_result = napi_gro_frags(napi);
	if (gro_result != GRO_DROP)
		channel->irq_mod_score += 2;
}

/* Allocate and construct an SKB around page fragments */
static struct sk_buff *ef4_rx_mk_skb(struct ef4_channel *channel,
				     struct ef4_rx_buffer *rx_buf,
				     unsigned int n_frags,
				     u8 *eh, int hdr_len)
{
	struct ef4_nic *efx = channel->efx;
	struct sk_buff *skb;

	/* Allocate an SKB to store the headers */
	skb = netdev_alloc_skb(efx->net_dev,
			       efx->rx_ip_align + efx->rx_prefix_size +
			       hdr_len);
	if (unlikely(skb == NULL)) {
		atomic_inc(&efx->n_rx_noskb_drops);
		return NULL;
	}

	EF4_BUG_ON_PARANOID(rx_buf->len < hdr_len);

	memcpy(skb->data + efx->rx_ip_align, eh - efx->rx_prefix_size,
	       efx->rx_prefix_size + hdr_len);
	skb_reserve(skb, efx->rx_ip_align + efx->rx_prefix_size);
	__skb_put(skb, hdr_len);

	/* Append the remaining page(s) onto the frag list */
	if (rx_buf->len > hdr_len) {
		rx_buf->page_offset += hdr_len;
		rx_buf->len -= hdr_len;

		for (;;) {
			skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags,
					   rx_buf->page, rx_buf->page_offset,
					   rx_buf->len);
			rx_buf->page = NULL;
			skb->len += rx_buf->len;
			skb->data_len += rx_buf->len;
			if (skb_shinfo(skb)->nr_frags == n_frags)
				break;

			rx_buf = ef4_rx_buf_next(&channel->rx_queue, rx_buf);
		}
	} else {
		__free_pages(rx_buf->page, efx->rx_buffer_order);
		rx_buf->page = NULL;
		n_frags = 0;
	}

	skb->truesize += n_frags * efx->rx_buffer_truesize;

	/* Move past the ethernet header */
	skb->protocol = eth_type_trans(skb, efx->net_dev);

	skb_mark_napi_id(skb, &channel->napi_str);

	return skb;
}

void ef4_rx_packet(struct ef4_rx_queue *rx_queue, unsigned int index,
		   unsigned int n_frags, unsigned int len, u16 flags)
{
	struct ef4_nic *efx = rx_queue->efx;
	struct ef4_channel *channel = ef4_rx_queue_channel(rx_queue);
	struct ef4_rx_buffer *rx_buf;

	rx_queue->rx_packets++;

	rx_buf = ef4_rx_buffer(rx_queue, index);
	rx_buf->flags |= flags;

	/* Validate the number of fragments and completed length */
	if (n_frags == 1) {
		if (!(flags & EF4_RX_PKT_PREFIX_LEN))
			ef4_rx_packet__check_len(rx_queue, rx_buf, len);
	} else if (unlikely(n_frags > EF4_RX_MAX_FRAGS) ||
		   unlikely(len <= (n_frags - 1) * efx->rx_dma_len) ||
		   unlikely(len > n_frags * efx->rx_dma_len) ||
		   unlikely(!efx->rx_scatter)) {
		/* If this isn't an explicit discard request, either
		 * the hardware or the driver is broken.
		 */
		WARN_ON(!(len == 0 && rx_buf->flags & EF4_RX_PKT_DISCARD));
		rx_buf->flags |= EF4_RX_PKT_DISCARD;
	}

	netif_vdbg(efx, rx_status, efx->net_dev,
		   "RX queue %d received ids %x-%x len %d %s%s\n",
		   ef4_rx_queue_index(rx_queue), index,
		   (index + n_frags - 1) & rx_queue->ptr_mask, len,
		   (rx_buf->flags & EF4_RX_PKT_CSUMMED) ? " [SUMMED]" : "",
		   (rx_buf->flags & EF4_RX_PKT_DISCARD) ? " [DISCARD]" : "");

	/* Discard packet, if instructed to do so.  Process the
	 * previous receive first.
	 */
	if (unlikely(rx_buf->flags & EF4_RX_PKT_DISCARD)) {
		ef4_rx_flush_packet(channel);
		ef4_discard_rx_packet(channel, rx_buf, n_frags);
		return;
	}

	if (n_frags == 1 && !(flags & EF4_RX_PKT_PREFIX_LEN))
		rx_buf->len = len;

	/* Release and/or sync the DMA mapping - assumes all RX buffers
	 * consumed in-order per RX queue.
	 */
	ef4_sync_rx_buffer(efx, rx_buf, rx_buf->len);

	/* Prefetch nice and early so data will (hopefully) be in cache by
	 * the time we look at it.
	 */
	prefetch(ef4_rx_buf_va(rx_buf));

	rx_buf->page_offset += efx->rx_prefix_size;
	rx_buf->len -= efx->rx_prefix_size;

	if (n_frags > 1) {
		/* Release/sync DMA mapping for additional fragments.
		 * Fix length for last fragment.
		 */
		unsigned int tail_frags = n_frags - 1;

		for (;;) {
			rx_buf = ef4_rx_buf_next(rx_queue, rx_buf);
			if (--tail_frags == 0)
				break;
			ef4_sync_rx_buffer(efx, rx_buf, efx->rx_dma_len);
		}
		rx_buf->len = len - (n_frags - 1) * efx->rx_dma_len;
		ef4_sync_rx_buffer(efx, rx_buf, rx_buf->len);
	}

	/* All fragments have been DMA-synced, so recycle pages. */
	rx_buf = ef4_rx_buffer(rx_queue, index);
	ef4_recycle_rx_pages(channel, rx_buf, n_frags);

	/* Pipeline receives so that we give time for packet headers to be
	 * prefetched into cache.
	 */
	ef4_rx_flush_packet(channel);
	channel->rx_pkt_n_frags = n_frags;
	channel->rx_pkt_index = index;
}

static void ef4_rx_deliver(struct ef4_channel *channel, u8 *eh,
			   struct ef4_rx_buffer *rx_buf,
			   unsigned int n_frags)
{
	struct sk_buff *skb;
	u16 hdr_len = min_t(u16, rx_buf->len, EF4_SKB_HEADERS);

	skb = ef4_rx_mk_skb(channel, rx_buf, n_frags, eh, hdr_len);
	if (unlikely(skb == NULL)) {
		struct ef4_rx_queue *rx_queue;

		rx_queue = ef4_channel_get_rx_queue(channel);
		ef4_free_rx_buffers(rx_queue, rx_buf, n_frags);
		return;
	}
	skb_record_rx_queue(skb, channel->rx_queue.core_index);

	/* Set the SKB flags */
	skb_checksum_none_assert(skb);
	if (likely(rx_buf->flags & EF4_RX_PKT_CSUMMED))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (channel->type->receive_skb)
		if (channel->type->receive_skb(channel, skb))
			return;

	/* Pass the packet up */
	netif_receive_skb(skb);
}

/* Handle a received packet.  Second half: Touches packet payload. */
void __ef4_rx_packet(struct ef4_channel *channel)
{
	struct ef4_nic *efx = channel->efx;
	struct ef4_rx_buffer *rx_buf =
		ef4_rx_buffer(&channel->rx_queue, channel->rx_pkt_index);
	u8 *eh = ef4_rx_buf_va(rx_buf);

	/* Read length from the prefix if necessary.  This already
	 * excludes the length of the prefix itself.
	 */
	if (rx_buf->flags & EF4_RX_PKT_PREFIX_LEN)
		rx_buf->len = le16_to_cpup((__le16 *)
					   (eh + efx->rx_packet_len_offset));

	/* If we're in loopback test, then pass the packet directly to the
	 * loopback layer, and free the rx_buf here
	 */
	if (unlikely(efx->loopback_selftest)) {
		struct ef4_rx_queue *rx_queue;

		ef4_loopback_rx_packet(efx, eh, rx_buf->len);
		rx_queue = ef4_channel_get_rx_queue(channel);
		ef4_free_rx_buffers(rx_queue, rx_buf,
				    channel->rx_pkt_n_frags);
		goto out;
	}

	if (unlikely(!(efx->net_dev->features & NETIF_F_RXCSUM)))
		rx_buf->flags &= ~EF4_RX_PKT_CSUMMED;

	if ((rx_buf->flags & EF4_RX_PKT_TCP) && !channel->type->receive_skb)
		ef4_rx_packet_gro(channel, rx_buf, channel->rx_pkt_n_frags, eh);
	else
		ef4_rx_deliver(channel, eh, rx_buf, channel->rx_pkt_n_frags);
out:
	channel->rx_pkt_n_frags = 0;
}

int ef4_probe_rx_queue(struct ef4_rx_queue *rx_queue)
{
	struct ef4_nic *efx = rx_queue->efx;
	unsigned int entries;
	int rc;

	/* Create the smallest power-of-two aligned ring */
	entries = max(roundup_pow_of_two(efx->rxq_entries), EF4_MIN_DMAQ_SIZE);
	EF4_BUG_ON_PARANOID(entries > EF4_MAX_DMAQ_SIZE);
	rx_queue->ptr_mask = entries - 1;

	netif_dbg(efx, probe, efx->net_dev,
		  "creating RX queue %d size %#x mask %#x\n",
		  ef4_rx_queue_index(rx_queue), efx->rxq_entries,
		  rx_queue->ptr_mask);

	/* Allocate RX buffers */
	rx_queue->buffer = kcalloc(entries, sizeof(*rx_queue->buffer),
				   GFP_KERNEL);
	if (!rx_queue->buffer)
		return -ENOMEM;

	rc = ef4_nic_probe_rx(rx_queue);
	if (rc) {
		kfree(rx_queue->buffer);
		rx_queue->buffer = NULL;
	}

	return rc;
}

static void ef4_init_rx_recycle_ring(struct ef4_nic *efx,
				     struct ef4_rx_queue *rx_queue)
{
	unsigned int bufs_in_recycle_ring, page_ring_size;

	/* Set the RX recycle ring size */
#ifdef CONFIG_PPC64
	bufs_in_recycle_ring = EF4_RECYCLE_RING_SIZE_IOMMU;
#else
	if (iommu_present(&pci_bus_type))
		bufs_in_recycle_ring = EF4_RECYCLE_RING_SIZE_IOMMU;
	else
		bufs_in_recycle_ring = EF4_RECYCLE_RING_SIZE_NOIOMMU;
#endif /* CONFIG_PPC64 */

	page_ring_size = roundup_pow_of_two(bufs_in_recycle_ring /
					    efx->rx_bufs_per_page);
	rx_queue->page_ring = kcalloc(page_ring_size,
				      sizeof(*rx_queue->page_ring), GFP_KERNEL);
	rx_queue->page_ptr_mask = page_ring_size - 1;
}

void ef4_init_rx_queue(struct ef4_rx_queue *rx_queue)
{
	struct ef4_nic *efx = rx_queue->efx;
	unsigned int max_fill, trigger, max_trigger;

	netif_dbg(rx_queue->efx, drv, rx_queue->efx->net_dev,
		  "initialising RX queue %d\n", ef4_rx_queue_index(rx_queue));

	/* Initialise ptr fields */
	rx_queue->added_count = 0;
	rx_queue->notified_count = 0;
	rx_queue->removed_count = 0;
	rx_queue->min_fill = -1U;
	ef4_init_rx_recycle_ring(efx, rx_queue);

	rx_queue->page_remove = 0;
	rx_queue->page_add = rx_queue->page_ptr_mask + 1;
	rx_queue->page_recycle_count = 0;
	rx_queue->page_recycle_failed = 0;
	rx_queue->page_recycle_full = 0;

	/* Initialise limit fields */
	max_fill = efx->rxq_entries - EF4_RXD_HEAD_ROOM;
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

	/* Set up RX descriptor ring */
	ef4_nic_init_rx(rx_queue);
}

void ef4_fini_rx_queue(struct ef4_rx_queue *rx_queue)
{
	int i;
	struct ef4_nic *efx = rx_queue->efx;
	struct ef4_rx_buffer *rx_buf;

	netif_dbg(rx_queue->efx, drv, rx_queue->efx->net_dev,
		  "shutting down RX queue %d\n", ef4_rx_queue_index(rx_queue));

	del_timer_sync(&rx_queue->slow_fill);

	/* Release RX buffers from the current read ptr to the write ptr */
	if (rx_queue->buffer) {
		for (i = rx_queue->removed_count; i < rx_queue->added_count;
		     i++) {
			unsigned index = i & rx_queue->ptr_mask;
			rx_buf = ef4_rx_buffer(rx_queue, index);
			ef4_fini_rx_buffer(rx_queue, rx_buf);
		}
	}

	/* Unmap and release the pages in the recycle ring. Remove the ring. */
	for (i = 0; i <= rx_queue->page_ptr_mask; i++) {
		struct page *page = rx_queue->page_ring[i];
		struct ef4_rx_page_state *state;

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

void ef4_remove_rx_queue(struct ef4_rx_queue *rx_queue)
{
	netif_dbg(rx_queue->efx, drv, rx_queue->efx->net_dev,
		  "destroying RX queue %d\n", ef4_rx_queue_index(rx_queue));

	ef4_nic_remove_rx(rx_queue);

	kfree(rx_queue->buffer);
	rx_queue->buffer = NULL;
}


module_param(rx_refill_threshold, uint, 0444);
MODULE_PARM_DESC(rx_refill_threshold,
		 "RX descriptor ring refill threshold (%)");

#ifdef CONFIG_RFS_ACCEL

int ef4_filter_rfs(struct net_device *net_dev, const struct sk_buff *skb,
		   u16 rxq_index, u32 flow_id)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	struct ef4_channel *channel;
	struct ef4_filter_spec spec;
	struct flow_keys fk;
	int rc;

	if (flow_id == RPS_FLOW_ID_INVALID)
		return -EINVAL;

	if (!skb_flow_dissect_flow_keys(skb, &fk, 0))
		return -EPROTONOSUPPORT;

	if (fk.basic.n_proto != htons(ETH_P_IP) && fk.basic.n_proto != htons(ETH_P_IPV6))
		return -EPROTONOSUPPORT;
	if (fk.control.flags & FLOW_DIS_IS_FRAGMENT)
		return -EPROTONOSUPPORT;

	ef4_filter_init_rx(&spec, EF4_FILTER_PRI_HINT,
			   efx->rx_scatter ? EF4_FILTER_FLAG_RX_SCATTER : 0,
			   rxq_index);
	spec.match_flags =
		EF4_FILTER_MATCH_ETHER_TYPE | EF4_FILTER_MATCH_IP_PROTO |
		EF4_FILTER_MATCH_LOC_HOST | EF4_FILTER_MATCH_LOC_PORT |
		EF4_FILTER_MATCH_REM_HOST | EF4_FILTER_MATCH_REM_PORT;
	spec.ether_type = fk.basic.n_proto;
	spec.ip_proto = fk.basic.ip_proto;

	if (fk.basic.n_proto == htons(ETH_P_IP)) {
		spec.rem_host[0] = fk.addrs.v4addrs.src;
		spec.loc_host[0] = fk.addrs.v4addrs.dst;
	} else {
		memcpy(spec.rem_host, &fk.addrs.v6addrs.src, sizeof(struct in6_addr));
		memcpy(spec.loc_host, &fk.addrs.v6addrs.dst, sizeof(struct in6_addr));
	}

	spec.rem_port = fk.ports.src;
	spec.loc_port = fk.ports.dst;

	rc = efx->type->filter_rfs_insert(efx, &spec);
	if (rc < 0)
		return rc;

	/* Remember this so we can check whether to expire the filter later */
	channel = ef4_get_channel(efx, rxq_index);
	channel->rps_flow_id[rc] = flow_id;
	++channel->rfs_filters_added;

	if (spec.ether_type == htons(ETH_P_IP))
		netif_info(efx, rx_status, efx->net_dev,
			   "steering %s %pI4:%u:%pI4:%u to queue %u [flow %u filter %d]\n",
			   (spec.ip_proto == IPPROTO_TCP) ? "TCP" : "UDP",
			   spec.rem_host, ntohs(spec.rem_port), spec.loc_host,
			   ntohs(spec.loc_port), rxq_index, flow_id, rc);
	else
		netif_info(efx, rx_status, efx->net_dev,
			   "steering %s [%pI6]:%u:[%pI6]:%u to queue %u [flow %u filter %d]\n",
			   (spec.ip_proto == IPPROTO_TCP) ? "TCP" : "UDP",
			   spec.rem_host, ntohs(spec.rem_port), spec.loc_host,
			   ntohs(spec.loc_port), rxq_index, flow_id, rc);

	return rc;
}

bool __ef4_filter_rfs_expire(struct ef4_nic *efx, unsigned int quota)
{
	bool (*expire_one)(struct ef4_nic *efx, u32 flow_id, unsigned int index);
	unsigned int channel_idx, index, size;
	u32 flow_id;

	if (!spin_trylock_bh(&efx->filter_lock))
		return false;

	expire_one = efx->type->filter_rfs_expire_one;
	channel_idx = efx->rps_expire_channel;
	index = efx->rps_expire_index;
	size = efx->type->max_rx_ip_filters;
	while (quota--) {
		struct ef4_channel *channel = ef4_get_channel(efx, channel_idx);
		flow_id = channel->rps_flow_id[index];

		if (flow_id != RPS_FLOW_ID_INVALID &&
		    expire_one(efx, flow_id, index)) {
			netif_info(efx, rx_status, efx->net_dev,
				   "expired filter %d [queue %u flow %u]\n",
				   index, channel_idx, flow_id);
			channel->rps_flow_id[index] = RPS_FLOW_ID_INVALID;
		}
		if (++index == size) {
			if (++channel_idx == efx->n_channels)
				channel_idx = 0;
			index = 0;
		}
	}
	efx->rps_expire_channel = channel_idx;
	efx->rps_expire_index = index;

	spin_unlock_bh(&efx->filter_lock);
	return true;
}

#endif /* CONFIG_RFS_ACCEL */

/**
 * ef4_filter_is_mc_recipient - test whether spec is a multicast recipient
 * @spec: Specification to test
 *
 * Return: %true if the specification is a non-drop RX filter that
 * matches a local MAC address I/G bit value of 1 or matches a local
 * IPv4 or IPv6 address value in the respective multicast address
 * range.  Otherwise %false.
 */
bool ef4_filter_is_mc_recipient(const struct ef4_filter_spec *spec)
{
	if (!(spec->flags & EF4_FILTER_FLAG_RX) ||
	    spec->dmaq_id == EF4_FILTER_RX_DMAQ_ID_DROP)
		return false;

	if (spec->match_flags &
	    (EF4_FILTER_MATCH_LOC_MAC | EF4_FILTER_MATCH_LOC_MAC_IG) &&
	    is_multicast_ether_addr(spec->loc_mac))
		return true;

	if ((spec->match_flags &
	     (EF4_FILTER_MATCH_ETHER_TYPE | EF4_FILTER_MATCH_LOC_HOST)) ==
	    (EF4_FILTER_MATCH_ETHER_TYPE | EF4_FILTER_MATCH_LOC_HOST)) {
		if (spec->ether_type == htons(ETH_P_IP) &&
		    ipv4_is_multicast(spec->loc_host[0]))
			return true;
		if (spec->ether_type == htons(ETH_P_IPV6) &&
		    ((const u8 *)spec->loc_host)[0] == 0xff)
			return true;
	}

	return false;
}
