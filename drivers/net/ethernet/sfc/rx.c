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
#include <net/ip.h>
#include <net/checksum.h>
#include "net_driver.h"
#include "efx.h"
#include "nic.h"
#include "selftest.h"
#include "workarounds.h"

/* Number of RX descriptors pushed at once. */
#define EFX_RX_BATCH  8

/* Maximum length for an RX descriptor sharing a page */
#define EFX_RX_HALF_PAGE ((PAGE_SIZE >> 1) - sizeof(struct efx_rx_page_state) \
			  - EFX_PAGE_IP_ALIGN)

/* Size of buffer allocated for skb header area. */
#define EFX_SKB_HEADERS  64u

/* This is the percentage fill level below which new RX descriptors
 * will be added to the RX descriptor ring.
 */
static unsigned int rx_refill_threshold;

/*
 * RX maximum head room required.
 *
 * This must be at least 1 to prevent overflow and at least 2 to allow
 * pipelined receives.
 */
#define EFX_RXD_HEAD_ROOM 2

/* Offset of ethernet header within page */
static inline unsigned int efx_rx_buf_offset(struct efx_nic *efx,
					     struct efx_rx_buffer *buf)
{
	return buf->page_offset + efx->type->rx_buffer_hash_size;
}

static u8 *efx_rx_buf_eh(struct efx_nic *efx, struct efx_rx_buffer *buf)
{
	return page_address(buf->page) + efx_rx_buf_offset(efx, buf);
}

static inline u32 efx_rx_buf_hash(const u8 *eh)
{
	/* The ethernet header is always directly after any hash. */
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) || NET_IP_ALIGN % 4 == 0
	return __le32_to_cpup((const __le32 *)(eh - 4));
#else
	const u8 *data = eh - 4;
	return (u32)data[0]	  |
	       (u32)data[1] << 8  |
	       (u32)data[2] << 16 |
	       (u32)data[3] << 24;
#endif
}

/**
 * efx_init_rx_buffers - create EFX_RX_BATCH page-based RX buffers
 *
 * @rx_queue:		Efx RX queue
 *
 * This allocates memory for EFX_RX_BATCH receive buffers, maps them for DMA,
 * and populates struct efx_rx_buffers for each one. Return a negative error
 * code or 0 on success. If a single page can be split between two buffers,
 * then the page will either be inserted fully, or not at at all.
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

	/* We can split a page between two buffers */
	BUILD_BUG_ON(EFX_RX_BATCH & 1);

	for (count = 0; count < EFX_RX_BATCH; ++count) {
		page = alloc_pages(__GFP_COLD | __GFP_COMP | GFP_ATOMIC,
				   efx->rx_buffer_order);
		if (unlikely(page == NULL))
			return -ENOMEM;
		dma_addr = dma_map_page(&efx->pci_dev->dev, page, 0,
					PAGE_SIZE << efx->rx_buffer_order,
					DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(&efx->pci_dev->dev, dma_addr))) {
			__free_pages(page, efx->rx_buffer_order);
			return -EIO;
		}
		state = page_address(page);
		state->refcnt = 0;
		state->dma_addr = dma_addr;

		dma_addr += sizeof(struct efx_rx_page_state);
		page_offset = sizeof(struct efx_rx_page_state);

	split:
		index = rx_queue->added_count & rx_queue->ptr_mask;
		rx_buf = efx_rx_buffer(rx_queue, index);
		rx_buf->dma_addr = dma_addr + EFX_PAGE_IP_ALIGN;
		rx_buf->page = page;
		rx_buf->page_offset = page_offset + EFX_PAGE_IP_ALIGN;
		rx_buf->len = efx->rx_dma_len;
		rx_buf->flags = 0;
		++rx_queue->added_count;
		++state->refcnt;

		if ((~count & 1) && (efx->rx_dma_len <= EFX_RX_HALF_PAGE)) {
			/* Use the second half of the page */
			get_page(page);
			dma_addr += (PAGE_SIZE >> 1);
			page_offset += (PAGE_SIZE >> 1);
			++count;
			goto split;
		}
	}

	return 0;
}

static void efx_unmap_rx_buffer(struct efx_nic *efx,
				struct efx_rx_buffer *rx_buf,
				unsigned int used_len)
{
	if (rx_buf->page) {
		struct efx_rx_page_state *state;

		state = page_address(rx_buf->page);
		if (--state->refcnt == 0) {
			dma_unmap_page(&efx->pci_dev->dev,
				       state->dma_addr,
				       PAGE_SIZE << efx->rx_buffer_order,
				       DMA_FROM_DEVICE);
		} else if (used_len) {
			dma_sync_single_for_cpu(&efx->pci_dev->dev,
						rx_buf->dma_addr, used_len,
						DMA_FROM_DEVICE);
		}
	}
}

static void efx_free_rx_buffer(struct efx_nic *efx,
			       struct efx_rx_buffer *rx_buf)
{
	if (rx_buf->page) {
		__free_pages(rx_buf->page, efx->rx_buffer_order);
		rx_buf->page = NULL;
	}
}

static void efx_fini_rx_buffer(struct efx_rx_queue *rx_queue,
			       struct efx_rx_buffer *rx_buf)
{
	efx_unmap_rx_buffer(rx_queue->efx, rx_buf, 0);
	efx_free_rx_buffer(rx_queue->efx, rx_buf);
}

/* Attempt to resurrect the other receive buffer that used to share this page,
 * which had previously been passed up to the kernel and freed. */
static void efx_resurrect_rx_buffer(struct efx_rx_queue *rx_queue,
				    struct efx_rx_buffer *rx_buf)
{
	struct efx_rx_page_state *state = page_address(rx_buf->page);
	struct efx_rx_buffer *new_buf;
	unsigned fill_level, index;

	/* +1 because efx_rx_packet() incremented removed_count. +1 because
	 * we'd like to insert an additional descriptor whilst leaving
	 * EFX_RXD_HEAD_ROOM for the non-recycle path */
	fill_level = (rx_queue->added_count - rx_queue->removed_count + 2);
	if (unlikely(fill_level > rx_queue->max_fill)) {
		/* We could place "state" on a list, and drain the list in
		 * efx_fast_push_rx_descriptors(). For now, this will do. */
		return;
	}

	++state->refcnt;
	get_page(rx_buf->page);

	index = rx_queue->added_count & rx_queue->ptr_mask;
	new_buf = efx_rx_buffer(rx_queue, index);
	new_buf->dma_addr = rx_buf->dma_addr ^ (PAGE_SIZE >> 1);
	new_buf->page = rx_buf->page;
	new_buf->len = rx_buf->len;
	++rx_queue->added_count;
}

/* Recycle the given rx buffer directly back into the rx_queue. There is
 * always room to add this buffer, because we've just popped a buffer. */
static void efx_recycle_rx_buffer(struct efx_channel *channel,
				  struct efx_rx_buffer *rx_buf)
{
	struct efx_nic *efx = channel->efx;
	struct efx_rx_queue *rx_queue = efx_channel_get_rx_queue(channel);
	struct efx_rx_buffer *new_buf;
	unsigned index;

	rx_buf->flags = 0;

	if (efx->rx_dma_len <= EFX_RX_HALF_PAGE &&
	    page_count(rx_buf->page) == 1)
		efx_resurrect_rx_buffer(rx_queue, rx_buf);

	index = rx_queue->added_count & rx_queue->ptr_mask;
	new_buf = efx_rx_buffer(rx_queue, index);

	memcpy(new_buf, rx_buf, sizeof(*new_buf));
	rx_buf->page = NULL;
	++rx_queue->added_count;
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
	unsigned fill_level;
	int space, rc = 0;

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

	space = rx_queue->max_fill - fill_level;
	EFX_BUG_ON_PARANOID(space < EFX_RX_BATCH);

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
	} while ((space -= EFX_RX_BATCH) >= EFX_RX_BATCH);

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
static void efx_rx_packet_gro(struct efx_channel *channel,
			      struct efx_rx_buffer *rx_buf,
			      const u8 *eh)
{
	struct napi_struct *napi = &channel->napi_str;
	gro_result_t gro_result;
	struct efx_nic *efx = channel->efx;
	struct page *page = rx_buf->page;
	struct sk_buff *skb;

	rx_buf->page = NULL;

	skb = napi_get_frags(napi);
	if (!skb) {
		put_page(page);
		return;
	}

	if (efx->net_dev->features & NETIF_F_RXHASH)
		skb->rxhash = efx_rx_buf_hash(eh);

	skb_fill_page_desc(skb, 0, page,
			   efx_rx_buf_offset(efx, rx_buf), rx_buf->len);

	skb->len = rx_buf->len;
	skb->data_len = rx_buf->len;
	skb->truesize += rx_buf->len;
	skb->ip_summed = ((rx_buf->flags & EFX_RX_PKT_CSUMMED) ?
			  CHECKSUM_UNNECESSARY : CHECKSUM_NONE);

	skb_record_rx_queue(skb, channel->rx_queue.core_index);

		gro_result = napi_gro_frags(napi);

	if (gro_result != GRO_DROP)
		channel->irq_mod_score += 2;
}

/* Allocate and construct an SKB around a struct page.*/
static struct sk_buff *efx_rx_mk_skb(struct efx_channel *channel,
				     struct efx_rx_buffer *rx_buf,
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

	skb->len = rx_buf->len;
	skb->truesize = rx_buf->len + sizeof(struct sk_buff);
	memcpy(skb->data, eh, hdr_len);
	skb->tail += hdr_len;

	/* Append the remaining page onto the frag list */
	if (rx_buf->len > hdr_len) {
		skb->data_len = skb->len - hdr_len;
		skb_fill_page_desc(skb, 0, rx_buf->page,
				   efx_rx_buf_offset(efx, rx_buf) + hdr_len,
				   skb->data_len);
	} else {
		__free_pages(rx_buf->page, efx->rx_buffer_order);
		skb->data_len = 0;
	}

	/* Ownership has transferred from the rx_buf to skb */
	rx_buf->page = NULL;

	/* Move past the ethernet header */
	skb->protocol = eth_type_trans(skb, efx->net_dev);

	return skb;
}

void efx_rx_packet(struct efx_rx_queue *rx_queue, unsigned int index,
		   unsigned int len, u16 flags)
{
	struct efx_nic *efx = rx_queue->efx;
	struct efx_channel *channel = efx_rx_queue_channel(rx_queue);
	struct efx_rx_buffer *rx_buf;

	rx_buf = efx_rx_buffer(rx_queue, index);
	rx_buf->flags |= flags;

	/* This allows the refill path to post another buffer.
	 * EFX_RXD_HEAD_ROOM ensures that the slot we are using
	 * isn't overwritten yet.
	 */
	rx_queue->removed_count++;

	/* Validate the length encoded in the event vs the descriptor pushed */
	efx_rx_packet__check_len(rx_queue, rx_buf, len);

	netif_vdbg(efx, rx_status, efx->net_dev,
		   "RX queue %d received id %x at %llx+%x %s%s\n",
		   efx_rx_queue_index(rx_queue), index,
		   (unsigned long long)rx_buf->dma_addr, len,
		   (rx_buf->flags & EFX_RX_PKT_CSUMMED) ? " [SUMMED]" : "",
		   (rx_buf->flags & EFX_RX_PKT_DISCARD) ? " [DISCARD]" : "");

	/* Discard packet, if instructed to do so */
	if (unlikely(rx_buf->flags & EFX_RX_PKT_DISCARD)) {
		efx_recycle_rx_buffer(channel, rx_buf);

		/* Don't hold off the previous receive */
		rx_buf = NULL;
		goto out;
	}

	/* Release and/or sync DMA mapping - assumes all RX buffers
	 * consumed in-order per RX queue
	 */
	efx_unmap_rx_buffer(efx, rx_buf, len);

	/* Prefetch nice and early so data will (hopefully) be in cache by
	 * the time we look at it.
	 */
	prefetch(efx_rx_buf_eh(efx, rx_buf));

	/* Pipeline receives so that we give time for packet headers to be
	 * prefetched into cache.
	 */
	rx_buf->len = len - efx->type->rx_buffer_hash_size;
out:
	efx_rx_flush_packet(channel);
	channel->rx_pkt = rx_buf;
}

static void efx_rx_deliver(struct efx_channel *channel, u8 *eh,
			   struct efx_rx_buffer *rx_buf)
{
	struct sk_buff *skb;
	u16 hdr_len = min_t(u16, rx_buf->len, EFX_SKB_HEADERS);

	skb = efx_rx_mk_skb(channel, rx_buf, eh, hdr_len);
	if (unlikely(skb == NULL)) {
		efx_free_rx_buffer(channel->efx, rx_buf);
		return;
	}
	skb_record_rx_queue(skb, channel->rx_queue.core_index);

	/* Set the SKB flags */
	skb_checksum_none_assert(skb);

	if (channel->type->receive_skb)
		if (channel->type->receive_skb(channel, skb))
			return;

	/* Pass the packet up */
	netif_receive_skb(skb);
}

/* Handle a received packet.  Second half: Touches packet payload. */
void __efx_rx_packet(struct efx_channel *channel, struct efx_rx_buffer *rx_buf)
{
	struct efx_nic *efx = channel->efx;
	u8 *eh = efx_rx_buf_eh(efx, rx_buf);

	/* If we're in loopback test, then pass the packet directly to the
	 * loopback layer, and free the rx_buf here
	 */
	if (unlikely(efx->loopback_selftest)) {
		efx_loopback_rx_packet(efx, eh, rx_buf->len);
		efx_free_rx_buffer(efx, rx_buf);
		return;
	}

	if (unlikely(!(efx->net_dev->features & NETIF_F_RXCSUM)))
		rx_buf->flags &= ~EFX_RX_PKT_CSUMMED;

	if (!channel->type->receive_skb)
		efx_rx_packet_gro(channel, rx_buf, eh);
	else
		efx_rx_deliver(channel, eh, rx_buf);
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

	/* Initialise limit fields */
	max_fill = efx->rxq_entries - EFX_RXD_HEAD_ROOM;
	max_trigger = max_fill - EFX_RX_BATCH;
	if (rx_refill_threshold != 0) {
		trigger = max_fill * min(rx_refill_threshold, 100U) / 100U;
		if (trigger > max_trigger)
			trigger = max_trigger;
	} else {
		trigger = max_trigger;
	}

	rx_queue->max_fill = max_fill;
	rx_queue->fast_fill_trigger = trigger;

	/* Set up RX descriptor ring */
	rx_queue->enabled = true;
	efx_nic_init_rx(rx_queue);
}

void efx_fini_rx_queue(struct efx_rx_queue *rx_queue)
{
	int i;
	struct efx_rx_buffer *rx_buf;

	netif_dbg(rx_queue->efx, drv, rx_queue->efx->net_dev,
		  "shutting down RX queue %d\n", efx_rx_queue_index(rx_queue));

	/* A flush failure might have left rx_queue->enabled */
	rx_queue->enabled = false;

	del_timer_sync(&rx_queue->slow_fill);
	efx_nic_fini_rx(rx_queue);

	/* Release RX buffers NB start at index 0 not current HW ptr */
	if (rx_queue->buffer) {
		for (i = 0; i <= rx_queue->ptr_mask; i++) {
			rx_buf = efx_rx_buffer(rx_queue, i);
			efx_fini_rx_buffer(rx_queue, rx_buf);
		}
	}
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

