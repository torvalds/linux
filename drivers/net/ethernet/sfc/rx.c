/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2011 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/socket.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/ip.h>
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
#define EFX_RX_PREFERRED_BATCH 8U

/* Number of RX buffers to recycle pages for.  When creating the RX page recycle
 * ring, this number is divided by the number of buffers per page to calculate
 * the number of pages to store in the RX page recycle ring.
 */
#define EFX_RECYCLE_RING_SIZE_IOMMU 4096
#define EFX_RECYCLE_RING_SIZE_NOIOMMU (2 * EFX_RX_PREFERRED_BATCH)

/* Size of buffer allocated for skb header area. */
#define EFX_SKB_HEADERS  128u

/* This is the percentage fill level below which new RX descriptors
 * will be added to the RX descriptor ring.
 */
static unsigned int rx_refill_threshold;

/* Each packet can consume up to ceil(max_frame_len / buffer_size) buffers */
#define EFX_RX_MAX_FRAGS DIV_ROUND_UP(EFX_MAX_FRAME_LEN(EFX_MAX_MTU), \
				      EFX_RX_USR_BUF_SIZE)

/*
 * RX maximum head room required.
 *
 * This must be at least 1 to prevent overflow, plus one packet-worth
 * to allow pipelined receives.
 */
#define EFX_RXD_HEAD_ROOM (1 + EFX_RX_MAX_FRAGS)

static inline u8 *efx_rx_buf_va(struct efx_rx_buffer *buf)
{
	return page_address(buf->page) + buf->page_offset;
}

static inline u32 efx_rx_buf_hash(struct efx_nic *efx, const u8 *eh)
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

static inline struct efx_rx_buffer *
efx_rx_buf_next(struct efx_rx_queue *rx_queue, struct efx_rx_buffer *rx_buf)
{
	if (unlikely(rx_buf == efx_rx_buffer(rx_queue, rx_queue->ptr_mask)))
		return efx_rx_buffer(rx_queue, 0);
	else
		return rx_buf + 1;
}

static inline void efx_sync_rx_buffer(struct efx_nic *efx,
				      struct efx_rx_buffer *rx_buf,
				      unsigned int len)
{
	dma_sync_single_for_cpu(&efx->pci_dev->dev, rx_buf->dma_addr, len,
				DMA_FROM_DEVICE);
}

void efx_rx_config_page_split(struct efx_nic *efx)
{
	efx->rx_page_buf_step = ALIGN(efx->rx_dma_len + NET_IP_ALIGN,
				      EFX_RX_BUF_ALIGNMENT);
	efx->rx_bufs_per_page = efx->rx_buffer_order ? 1 :
		((PAGE_SIZE - sizeof(struct efx_rx_page_state)) /
		 efx->rx_page_buf_step);
	efx->rx_buffer_truesize = (PAGE_SIZE << efx->rx_buffer_order) /
		efx->rx_bufs_per_page;
	efx->rx_pages_per_batch = DIV_ROUND_UP(EFX_RX_PREFERRED_BATCH,
					       efx->rx_bufs_per_page);
}

/* Check the RX page recycle ring for a page that can be reused. */
static struct page *efx_reuse_page(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	struct page *page;
	struct efx_rx_page_state *state;
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
 * efx_init_rx_buffers - create EFX_RX_BATCH page-based RX buffers
 *
 * @rx_queue:		Efx RX queue
 *
 * This allocates a batch of pages, maps them for DMA, and populates
 * struct efx_rx_buffers for each one. Return a negative error code or
 * 0 on success. If a single page can be used for multiple buffers,
 * then the page will either be inserted fully, or not at all.
 */
static int efx_init_rx_buffers(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	struct efx_rx_buffer *rx_buf;
	struct page *page;
	unsigned int page_offset;
	struct efx_rx_page_state *state;
	dma_addr_t dma_addr;
	unsigned index, count;

	count = 0;
	do {
		page = efx_reuse_page(rx_queue);
		if (page == NULL) {
			page = alloc_pages(__GFP_COLD | __GFP_COMP | GFP_ATOMIC,
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
			rx_buf->dma_addr = dma_addr + NET_IP_ALIGN;
			rx_buf->page = page;
			rx_buf->page_offset = page_offset + NET_IP_ALIGN;
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

/* Unmap a DMA-mapped page.  This function is only called for the final RX
 * buffer in a page.
 */
static void efx_unmap_rx_buffer(struct efx_nic *efx,
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

static void efx_free_rx_buffer(struct efx_rx_buffer *rx_buf)
{
	if (rx_buf->page) {
		put_page(rx_buf->page);
		rx_buf->page = NULL;
	}
}

/* Attempt to recycle the page if there is an RX recycle ring; the page can
 * only be added if this is the final RX buffer, to prevent pages being used in
 * the descriptor ring and appearing in the recycle ring simultaneously.
 */
static void efx_recycle_rx_page(struct efx_channel *channel,
				struct efx_rx_buffer *rx_buf)
{
	struct page *page = rx_buf->page;
	struct efx_rx_queue *rx_queue = efx_channel_get_rx_queue(channel);
	struct efx_nic *efx = rx_queue->efx;
	unsigned index;

	/* Only recycle the page after processing the final buffer. */
	if (!(rx_buf->flags & EFX_RX_BUF_LAST_IN_PAGE))
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
	efx_unmap_rx_buffer(efx, rx_buf);
	put_page(rx_buf->page);
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
		efx_free_rx_buffer(rx_buf);
	}
	rx_buf->page = NULL;
}

/* Recycle the pages that are used by buffers that have just been received. */
static void efx_recycle_rx_pages(struct efx_channel *channel,
				 struct efx_rx_buffer *rx_buf,
				 unsigned int n_frags)
{
	struct efx_rx_queue *rx_queue = efx_channel_get_rx_queue(channel);

	do {
		efx_recycle_rx_page(channel, rx_buf);
		rx_buf = efx_rx_buf_next(rx_queue, rx_buf);
	} while (--n_frags);
}

static void efx_discard_rx_packet(struct efx_channel *channel,
				  struct efx_rx_buffer *rx_buf,
				  unsigned int n_frags)
{
	struct efx_rx_queue *rx_queue = efx_channel_get_rx_queue(channel);

	efx_recycle_rx_pages(channel, rx_buf, n_frags);

	do {
		efx_free_rx_buffer(rx_buf);
		rx_buf = efx_rx_buf_next(rx_queue, rx_buf);
	} while (--n_frags);
}

/**
 * efx_fast_push_rx_descriptors - push new RX descriptors quickly
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
void efx_fast_push_rx_descriptors(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	unsigned int fill_level, batch_size;
	int space, rc = 0;

	if (!rx_queue->refill_enabled)
		return;

	/* Calculate current fill level, and exit if we don't need to fill */
	fill_level = (rx_queue->added_count - rx_queue->removed_count);
	EFX_BUG_ON_PARANOID(fill_level > rx_queue->efx->rxq_entries);
	if (fill_level >= rx_queue->fast_fill_trigger)
		goto out;

	/* Record minimum fill level */
	if (unlikely(fill_level < rx_queue->min_fill)) {
		if (fill_level)
			rx_queue->min_fill = fill_level;
	}

	batch_size = efx->rx_pages_per_batch * efx->rx_bufs_per_page;
	space = rx_queue->max_fill - fill_level;
	EFX_BUG_ON_PARANOID(space < batch_size);

	netif_vdbg(rx_queue->efx, rx_status, rx_queue->efx->net_dev,
		   "RX queue %d fast-filling descriptor ring from"
		   " level %d to level %d\n",
		   efx_rx_queue_index(rx_queue), fill_level,
		   rx_queue->max_fill);


	do {
		rc = efx_init_rx_buffers(rx_queue);
		if (unlikely(rc)) {
			/* Ensure that we don't leave the rx queue empty */
			if (rx_queue->added_count == rx_queue->removed_count)
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

void efx_rx_slow_fill(unsigned long context)
{
	struct efx_rx_queue *rx_queue = (struct efx_rx_queue *)context;

	/* Post an event to cause NAPI to run and refill the queue */
	efx_nic_generate_fill_event(rx_queue);
	++rx_queue->slow_fill_count;
}

static void efx_rx_packet__check_len(struct efx_rx_queue *rx_queue,
				     struct efx_rx_buffer *rx_buf,
				     int len)
{
	struct efx_nic *efx = rx_queue->efx;
	unsigned max_len = rx_buf->len - efx->type->rx_buffer_padding;

	if (likely(len <= max_len))
		return;

	/* The packet must be discarded, but this is only a fatal error
	 * if the caller indicated it was
	 */
	rx_buf->flags |= EFX_RX_PKT_DISCARD;

	if ((len > rx_buf->len) && EFX_WORKAROUND_8071(efx)) {
		if (net_ratelimit())
			netif_err(efx, rx_err, efx->net_dev,
				  " RX queue %d seriously overlength "
				  "RX event (0x%x > 0x%x+0x%x). Leaking\n",
				  efx_rx_queue_index(rx_queue), len, max_len,
				  efx->type->rx_buffer_padding);
		efx_schedule_reset(efx, RESET_TYPE_RX_RECOVERY);
	} else {
		if (net_ratelimit())
			netif_err(efx, rx_err, efx->net_dev,
				  " RX queue %d overlength RX event "
				  "(0x%x > 0x%x)\n",
				  efx_rx_queue_index(rx_queue), len, max_len);
	}

	efx_rx_queue_channel(rx_queue)->n_rx_overlength++;
}

/* Pass a received packet up through GRO.  GRO can handle pages
 * regardless of checksum state and skbs with a good checksum.
 */
static void
efx_rx_packet_gro(struct efx_channel *channel, struct efx_rx_buffer *rx_buf,
		  unsigned int n_frags, u8 *eh)
{
	struct napi_struct *napi = &channel->napi_str;
	gro_result_t gro_result;
	struct efx_nic *efx = channel->efx;
	struct sk_buff *skb;

	skb = napi_get_frags(napi);
	if (unlikely(!skb)) {
		while (n_frags--) {
			put_page(rx_buf->page);
			rx_buf->page = NULL;
			rx_buf = efx_rx_buf_next(&channel->rx_queue, rx_buf);
		}
		return;
	}

	if (efx->net_dev->features & NETIF_F_RXHASH)
		skb->rxhash = efx_rx_buf_hash(efx, eh);
	skb->ip_summed = ((rx_buf->flags & EFX_RX_PKT_CSUMMED) ?
			  CHECKSUM_UNNECESSARY : CHECKSUM_NONE);

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

	gro_result = napi_gro_frags(napi);
	if (gro_result != GRO_DROP)
		channel->irq_mod_score += 2;
}

/* Allocate and construct an SKB around page fragments */
static struct sk_buff *efx_rx_mk_skb(struct efx_channel *channel,
				     struct efx_rx_buffer *rx_buf,
				     unsigned int n_frags,
				     u8 *eh, int hdr_len)
{
	struct efx_nic *efx = channel->efx;
	struct sk_buff *skb;

	/* Allocate an SKB to store the headers */
	skb = netdev_alloc_skb(efx->net_dev, hdr_len + EFX_PAGE_SKB_ALIGN);
	if (unlikely(skb == NULL))
		return NULL;

	EFX_BUG_ON_PARANOID(rx_buf->len < hdr_len);

	skb_reserve(skb, EFX_PAGE_SKB_ALIGN);
	memcpy(__skb_put(skb, hdr_len), eh, hdr_len);

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

			rx_buf = efx_rx_buf_next(&channel->rx_queue, rx_buf);
		}
	} else {
		__free_pages(rx_buf->page, efx->rx_buffer_order);
		rx_buf->page = NULL;
		n_frags = 0;
	}

	skb->truesize += n_frags * efx->rx_buffer_truesize;

	/* Move past the ethernet header */
	skb->protocol = eth_type_trans(skb, efx->net_dev);

	return skb;
}

void efx_rx_packet(struct efx_rx_queue *rx_queue, unsigned int index,
		   unsigned int n_frags, unsigned int len, u16 flags)
{
	struct efx_nic *efx = rx_queue->efx;
	struct efx_channel *channel = efx_rx_queue_channel(rx_queue);
	struct efx_rx_buffer *rx_buf;

	rx_buf = efx_rx_buffer(rx_queue, index);
	rx_buf->flags |= flags;

	/* Validate the number of fragments and completed length */
	if (n_frags == 1) {
		if (!(flags & EFX_RX_PKT_PREFIX_LEN))
			efx_rx_packet__check_len(rx_queue, rx_buf, len);
	} else if (unlikely(n_frags > EFX_RX_MAX_FRAGS) ||
		   unlikely(len <= (n_frags - 1) * EFX_RX_USR_BUF_SIZE) ||
		   unlikely(len > n_frags * EFX_RX_USR_BUF_SIZE) ||
		   unlikely(!efx->rx_scatter)) {
		/* If this isn't an explicit discard request, either
		 * the hardware or the driver is broken.
		 */
		WARN_ON(!(len == 0 && rx_buf->flags & EFX_RX_PKT_DISCARD));
		rx_buf->flags |= EFX_RX_PKT_DISCARD;
	}

	netif_vdbg(efx, rx_status, efx->net_dev,
		   "RX queue %d received ids %x-%x len %d %s%s\n",
		   efx_rx_queue_index(rx_queue), index,
		   (index + n_frags - 1) & rx_queue->ptr_mask, len,
		   (rx_buf->flags & EFX_RX_PKT_CSUMMED) ? " [SUMMED]" : "",
		   (rx_buf->flags & EFX_RX_PKT_DISCARD) ? " [DISCARD]" : "");

	/* Discard packet, if instructed to do so.  Process the
	 * previous receive first.
	 */
	if (unlikely(rx_buf->flags & EFX_RX_PKT_DISCARD)) {
		efx_rx_flush_packet(channel);
		efx_discard_rx_packet(channel, rx_buf, n_frags);
		return;
	}

	if (n_frags == 1 && !(flags & EFX_RX_PKT_PREFIX_LEN))
		rx_buf->len = len;

	/* Release and/or sync the DMA mapping - assumes all RX buffers
	 * consumed in-order per RX queue.
	 */
	efx_sync_rx_buffer(efx, rx_buf, rx_buf->len);

	/* Prefetch nice and early so data will (hopefully) be in cache by
	 * the time we look at it.
	 */
	prefetch(efx_rx_buf_va(rx_buf));

	rx_buf->page_offset += efx->rx_prefix_size;
	rx_buf->len -= efx->rx_prefix_size;

	if (n_frags > 1) {
		/* Release/sync DMA mapping for additional fragments.
		 * Fix length for last fragment.
		 */
		unsigned int tail_frags = n_frags - 1;

		for (;;) {
			rx_buf = efx_rx_buf_next(rx_queue, rx_buf);
			if (--tail_frags == 0)
				break;
			efx_sync_rx_buffer(efx, rx_buf, EFX_RX_USR_BUF_SIZE);
		}
		rx_buf->len = len - (n_frags - 1) * EFX_RX_USR_BUF_SIZE;
		efx_sync_rx_buffer(efx, rx_buf, rx_buf->len);
	}

	/* All fragments have been DMA-synced, so recycle pages. */
	rx_buf = efx_rx_buffer(rx_queue, index);
	efx_recycle_rx_pages(channel, rx_buf, n_frags);

	/* Pipeline receives so that we give time for packet headers to be
	 * prefetched into cache.
	 */
	efx_rx_flush_packet(channel);
	channel->rx_pkt_n_frags = n_frags;
	channel->rx_pkt_index = index;
}

static void efx_rx_deliver(struct efx_channel *channel, u8 *eh,
			   struct efx_rx_buffer *rx_buf,
			   unsigned int n_frags)
{
	struct sk_buff *skb;
	u16 hdr_len = min_t(u16, rx_buf->len, EFX_SKB_HEADERS);

	skb = efx_rx_mk_skb(channel, rx_buf, n_frags, eh, hdr_len);
	if (unlikely(skb == NULL)) {
		efx_free_rx_buffer(rx_buf);
		return;
	}
	skb_record_rx_queue(skb, channel->rx_queue.core_index);

	/* Set the SKB flags */
	skb_checksum_none_assert(skb);
	if (likely(rx_buf->flags & EFX_RX_PKT_CSUMMED))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (channel->type->receive_skb)
		if (channel->type->receive_skb(channel, skb))
			return;

	/* Pass the packet up */
	netif_receive_skb(skb);
}

/* Handle a received packet.  Second half: Touches packet payload. */
void __efx_rx_packet(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;
	struct efx_rx_buffer *rx_buf =
		efx_rx_buffer(&channel->rx_queue, channel->rx_pkt_index);
	u8 *eh = efx_rx_buf_va(rx_buf);

	/* Read length from the prefix if necessary.  This already
	 * excludes the length of the prefix itself.
	 */
	if (rx_buf->flags & EFX_RX_PKT_PREFIX_LEN)
		rx_buf->len = le16_to_cpup((__le16 *)
					   (eh + efx->rx_packet_len_offset));

	/* If we're in loopback test, then pass the packet directly to the
	 * loopback layer, and free the rx_buf here
	 */
	if (unlikely(efx->loopback_selftest)) {
		efx_loopback_rx_packet(efx, eh, rx_buf->len);
		efx_free_rx_buffer(rx_buf);
		goto out;
	}

	if (unlikely(!(efx->net_dev->features & NETIF_F_RXCSUM)))
		rx_buf->flags &= ~EFX_RX_PKT_CSUMMED;

	if ((rx_buf->flags & EFX_RX_PKT_TCP) && !channel->type->receive_skb)
		efx_rx_packet_gro(channel, rx_buf, channel->rx_pkt_n_frags, eh);
	else
		efx_rx_deliver(channel, eh, rx_buf, channel->rx_pkt_n_frags);
out:
	channel->rx_pkt_n_frags = 0;
}

int efx_probe_rx_queue(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	unsigned int entries;
	int rc;

	/* Create the smallest power-of-two aligned ring */
	entries = max(roundup_pow_of_two(efx->rxq_entries), EFX_MIN_DMAQ_SIZE);
	EFX_BUG_ON_PARANOID(entries > EFX_MAX_DMAQ_SIZE);
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

static void efx_init_rx_recycle_ring(struct efx_nic *efx,
				     struct efx_rx_queue *rx_queue)
{
	unsigned int bufs_in_recycle_ring, page_ring_size;

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
	rx_queue->page_ptr_mask = page_ring_size - 1;
}

void efx_init_rx_queue(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	unsigned int max_fill, trigger, max_trigger;

	netif_dbg(rx_queue->efx, drv, rx_queue->efx->net_dev,
		  "initialising RX queue %d\n", efx_rx_queue_index(rx_queue));

	/* Initialise ptr fields */
	rx_queue->added_count = 0;
	rx_queue->notified_count = 0;
	rx_queue->removed_count = 0;
	rx_queue->min_fill = -1U;
	efx_init_rx_recycle_ring(efx, rx_queue);

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

	/* Set up RX descriptor ring */
	efx_nic_init_rx(rx_queue);
}

void efx_fini_rx_queue(struct efx_rx_queue *rx_queue)
{
	int i;
	struct efx_nic *efx = rx_queue->efx;
	struct efx_rx_buffer *rx_buf;

	netif_dbg(rx_queue->efx, drv, rx_queue->efx->net_dev,
		  "shutting down RX queue %d\n", efx_rx_queue_index(rx_queue));

	del_timer_sync(&rx_queue->slow_fill);

	/* Release RX buffers from the current read ptr to the write ptr */
	if (rx_queue->buffer) {
		for (i = rx_queue->removed_count; i < rx_queue->added_count;
		     i++) {
			unsigned index = i & rx_queue->ptr_mask;
			rx_buf = efx_rx_buffer(rx_queue, index);
			efx_fini_rx_buffer(rx_queue, rx_buf);
		}
	}

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

void efx_remove_rx_queue(struct efx_rx_queue *rx_queue)
{
	netif_dbg(rx_queue->efx, drv, rx_queue->efx->net_dev,
		  "destroying RX queue %d\n", efx_rx_queue_index(rx_queue));

	efx_nic_remove_rx(rx_queue);

	kfree(rx_queue->buffer);
	rx_queue->buffer = NULL;
}


module_param(rx_refill_threshold, uint, 0444);
MODULE_PARM_DESC(rx_refill_threshold,
		 "RX descriptor ring refill threshold (%)");

#ifdef CONFIG_RFS_ACCEL

int efx_filter_rfs(struct net_device *net_dev, const struct sk_buff *skb,
		   u16 rxq_index, u32 flow_id)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_channel *channel;
	struct efx_filter_spec spec;
	const struct iphdr *ip;
	const __be16 *ports;
	int nhoff;
	int rc;

	nhoff = skb_network_offset(skb);

	if (skb->protocol == htons(ETH_P_8021Q)) {
		EFX_BUG_ON_PARANOID(skb_headlen(skb) <
				    nhoff + sizeof(struct vlan_hdr));
		if (((const struct vlan_hdr *)skb->data + nhoff)->
		    h_vlan_encapsulated_proto != htons(ETH_P_IP))
			return -EPROTONOSUPPORT;

		/* This is IP over 802.1q VLAN.  We can't filter on the
		 * IP 5-tuple and the vlan together, so just strip the
		 * vlan header and filter on the IP part.
		 */
		nhoff += sizeof(struct vlan_hdr);
	} else if (skb->protocol != htons(ETH_P_IP)) {
		return -EPROTONOSUPPORT;
	}

	/* RFS must validate the IP header length before calling us */
	EFX_BUG_ON_PARANOID(skb_headlen(skb) < nhoff + sizeof(*ip));
	ip = (const struct iphdr *)(skb->data + nhoff);
	if (ip_is_fragment(ip))
		return -EPROTONOSUPPORT;
	EFX_BUG_ON_PARANOID(skb_headlen(skb) < nhoff + 4 * ip->ihl + 4);
	ports = (const __be16 *)(skb->data + nhoff + 4 * ip->ihl);

	efx_filter_init_rx(&spec, EFX_FILTER_PRI_HINT,
			   efx->rx_scatter ? EFX_FILTER_FLAG_RX_SCATTER : 0,
			   rxq_index);
	rc = efx_filter_set_ipv4_full(&spec, ip->protocol,
				      ip->daddr, ports[1], ip->saddr, ports[0]);
	if (rc)
		return rc;

	rc = efx->type->filter_rfs_insert(efx, &spec);
	if (rc < 0)
		return rc;

	/* Remember this so we can check whether to expire the filter later */
	efx->rps_flow_id[rc] = flow_id;
	channel = efx_get_channel(efx, skb_get_rx_queue(skb));
	++channel->rfs_filters_added;

	netif_info(efx, rx_status, efx->net_dev,
		   "steering %s %pI4:%u:%pI4:%u to queue %u [flow %u filter %d]\n",
		   (ip->protocol == IPPROTO_TCP) ? "TCP" : "UDP",
		   &ip->saddr, ntohs(ports[0]), &ip->daddr, ntohs(ports[1]),
		   rxq_index, flow_id, rc);

	return rc;
}

bool __efx_filter_rfs_expire(struct efx_nic *efx, unsigned int quota)
{
	bool (*expire_one)(struct efx_nic *efx, u32 flow_id, unsigned int index);
	unsigned int index, size;
	u32 flow_id;

	if (!spin_trylock_bh(&efx->filter_lock))
		return false;

	expire_one = efx->type->filter_rfs_expire_one;
	index = efx->rps_expire_index;
	size = efx->type->max_rx_ip_filters;
	while (quota--) {
		flow_id = efx->rps_flow_id[index];
		if (expire_one(efx, flow_id, index))
			netif_info(efx, rx_status, efx->net_dev,
				   "expired filter %d [flow %u]\n",
				   index, flow_id);
		if (++index == size)
			index = 0;
	}
	efx->rps_expire_index = index;

	spin_unlock_bh(&efx->filter_lock);
	return true;
}

#endif /* CONFIG_RFS_ACCEL */
