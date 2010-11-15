/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/pci.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/ipv6.h>
#include <linux/slab.h>
#include <net/ipv6.h>
#include <linux/if_ether.h>
#include <linux/highmem.h>
#include "net_driver.h"
#include "efx.h"
#include "nic.h"
#include "workarounds.h"

/*
 * TX descriptor ring full threshold
 *
 * The tx_queue descriptor ring fill-level must fall below this value
 * before we restart the netif queue
 */
#define EFX_TXQ_THRESHOLD(_efx) ((_efx)->txq_entries / 2u)

/* We need to be able to nest calls to netif_tx_stop_queue(), partly
 * because of the 2 hardware queues associated with each core queue,
 * but also so that we can inhibit TX for reasons other than a full
 * hardware queue. */
void efx_stop_queue(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;
	struct efx_tx_queue *tx_queue = efx_channel_get_tx_queue(channel, 0);

	if (!tx_queue)
		return;

	spin_lock_bh(&channel->tx_stop_lock);
	netif_vdbg(efx, tx_queued, efx->net_dev, "stop TX queue\n");

	atomic_inc(&channel->tx_stop_count);
	netif_tx_stop_queue(
		netdev_get_tx_queue(efx->net_dev,
				    tx_queue->queue / EFX_TXQ_TYPES));

	spin_unlock_bh(&channel->tx_stop_lock);
}

/* Decrement core TX queue stop count and wake it if the count is 0 */
void efx_wake_queue(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;
	struct efx_tx_queue *tx_queue = efx_channel_get_tx_queue(channel, 0);

	if (!tx_queue)
		return;

	local_bh_disable();
	if (atomic_dec_and_lock(&channel->tx_stop_count,
				&channel->tx_stop_lock)) {
		netif_vdbg(efx, tx_queued, efx->net_dev, "waking TX queue\n");
		netif_tx_wake_queue(
			netdev_get_tx_queue(efx->net_dev,
					    tx_queue->queue / EFX_TXQ_TYPES));
		spin_unlock(&channel->tx_stop_lock);
	}
	local_bh_enable();
}

static void efx_dequeue_buffer(struct efx_tx_queue *tx_queue,
			       struct efx_tx_buffer *buffer)
{
	if (buffer->unmap_len) {
		struct pci_dev *pci_dev = tx_queue->efx->pci_dev;
		dma_addr_t unmap_addr = (buffer->dma_addr + buffer->len -
					 buffer->unmap_len);
		if (buffer->unmap_single)
			pci_unmap_single(pci_dev, unmap_addr, buffer->unmap_len,
					 PCI_DMA_TODEVICE);
		else
			pci_unmap_page(pci_dev, unmap_addr, buffer->unmap_len,
				       PCI_DMA_TODEVICE);
		buffer->unmap_len = 0;
		buffer->unmap_single = false;
	}

	if (buffer->skb) {
		dev_kfree_skb_any((struct sk_buff *) buffer->skb);
		buffer->skb = NULL;
		netif_vdbg(tx_queue->efx, tx_done, tx_queue->efx->net_dev,
			   "TX queue %d transmission id %x complete\n",
			   tx_queue->queue, tx_queue->read_count);
	}
}

/**
 * struct efx_tso_header - a DMA mapped buffer for packet headers
 * @next: Linked list of free ones.
 *	The list is protected by the TX queue lock.
 * @dma_unmap_len: Length to unmap for an oversize buffer, or 0.
 * @dma_addr: The DMA address of the header below.
 *
 * This controls the memory used for a TSO header.  Use TSOH_DATA()
 * to find the packet header data.  Use TSOH_SIZE() to calculate the
 * total size required for a given packet header length.  TSO headers
 * in the free list are exactly %TSOH_STD_SIZE bytes in size.
 */
struct efx_tso_header {
	union {
		struct efx_tso_header *next;
		size_t unmap_len;
	};
	dma_addr_t dma_addr;
};

static int efx_enqueue_skb_tso(struct efx_tx_queue *tx_queue,
			       struct sk_buff *skb);
static void efx_fini_tso(struct efx_tx_queue *tx_queue);
static void efx_tsoh_heap_free(struct efx_tx_queue *tx_queue,
			       struct efx_tso_header *tsoh);

static void efx_tsoh_free(struct efx_tx_queue *tx_queue,
			  struct efx_tx_buffer *buffer)
{
	if (buffer->tsoh) {
		if (likely(!buffer->tsoh->unmap_len)) {
			buffer->tsoh->next = tx_queue->tso_headers_free;
			tx_queue->tso_headers_free = buffer->tsoh;
		} else {
			efx_tsoh_heap_free(tx_queue, buffer->tsoh);
		}
		buffer->tsoh = NULL;
	}
}


static inline unsigned
efx_max_tx_len(struct efx_nic *efx, dma_addr_t dma_addr)
{
	/* Depending on the NIC revision, we can use descriptor
	 * lengths up to 8K or 8K-1.  However, since PCI Express
	 * devices must split read requests at 4K boundaries, there is
	 * little benefit from using descriptors that cross those
	 * boundaries and we keep things simple by not doing so.
	 */
	unsigned len = (~dma_addr & 0xfff) + 1;

	/* Work around hardware bug for unaligned buffers. */
	if (EFX_WORKAROUND_5391(efx) && (dma_addr & 0xf))
		len = min_t(unsigned, len, 512 - (dma_addr & 0xf));

	return len;
}

/*
 * Add a socket buffer to a TX queue
 *
 * This maps all fragments of a socket buffer for DMA and adds them to
 * the TX queue.  The queue's insert pointer will be incremented by
 * the number of fragments in the socket buffer.
 *
 * If any DMA mapping fails, any mapped fragments will be unmapped,
 * the queue's insert pointer will be restored to its original value.
 *
 * This function is split out from efx_hard_start_xmit to allow the
 * loopback test to direct packets via specific TX queues.
 *
 * Returns NETDEV_TX_OK or NETDEV_TX_BUSY
 * You must hold netif_tx_lock() to call this function.
 */
netdev_tx_t efx_enqueue_skb(struct efx_tx_queue *tx_queue, struct sk_buff *skb)
{
	struct efx_nic *efx = tx_queue->efx;
	struct pci_dev *pci_dev = efx->pci_dev;
	struct efx_tx_buffer *buffer;
	skb_frag_t *fragment;
	struct page *page;
	int page_offset;
	unsigned int len, unmap_len = 0, fill_level, insert_ptr;
	dma_addr_t dma_addr, unmap_addr = 0;
	unsigned int dma_len;
	bool unmap_single;
	int q_space, i = 0;
	netdev_tx_t rc = NETDEV_TX_OK;

	EFX_BUG_ON_PARANOID(tx_queue->write_count != tx_queue->insert_count);

	if (skb_shinfo(skb)->gso_size)
		return efx_enqueue_skb_tso(tx_queue, skb);

	/* Get size of the initial fragment */
	len = skb_headlen(skb);

	/* Pad if necessary */
	if (EFX_WORKAROUND_15592(efx) && skb->len <= 32) {
		EFX_BUG_ON_PARANOID(skb->data_len);
		len = 32 + 1;
		if (skb_pad(skb, len - skb->len))
			return NETDEV_TX_OK;
	}

	fill_level = tx_queue->insert_count - tx_queue->old_read_count;
	q_space = efx->txq_entries - 1 - fill_level;

	/* Map for DMA.  Use pci_map_single rather than pci_map_page
	 * since this is more efficient on machines with sparse
	 * memory.
	 */
	unmap_single = true;
	dma_addr = pci_map_single(pci_dev, skb->data, len, PCI_DMA_TODEVICE);

	/* Process all fragments */
	while (1) {
		if (unlikely(pci_dma_mapping_error(pci_dev, dma_addr)))
			goto pci_err;

		/* Store fields for marking in the per-fragment final
		 * descriptor */
		unmap_len = len;
		unmap_addr = dma_addr;

		/* Add to TX queue, splitting across DMA boundaries */
		do {
			if (unlikely(q_space-- <= 0)) {
				/* It might be that completions have
				 * happened since the xmit path last
				 * checked.  Update the xmit path's
				 * copy of read_count.
				 */
				++tx_queue->stopped;
				/* This memory barrier protects the
				 * change of stopped from the access
				 * of read_count. */
				smp_mb();
				tx_queue->old_read_count =
					ACCESS_ONCE(tx_queue->read_count);
				fill_level = (tx_queue->insert_count
					      - tx_queue->old_read_count);
				q_space = efx->txq_entries - 1 - fill_level;
				if (unlikely(q_space-- <= 0))
					goto stop;
				smp_mb();
				--tx_queue->stopped;
			}

			insert_ptr = tx_queue->insert_count & tx_queue->ptr_mask;
			buffer = &tx_queue->buffer[insert_ptr];
			efx_tsoh_free(tx_queue, buffer);
			EFX_BUG_ON_PARANOID(buffer->tsoh);
			EFX_BUG_ON_PARANOID(buffer->skb);
			EFX_BUG_ON_PARANOID(buffer->len);
			EFX_BUG_ON_PARANOID(!buffer->continuation);
			EFX_BUG_ON_PARANOID(buffer->unmap_len);

			dma_len = efx_max_tx_len(efx, dma_addr);
			if (likely(dma_len >= len))
				dma_len = len;

			/* Fill out per descriptor fields */
			buffer->len = dma_len;
			buffer->dma_addr = dma_addr;
			len -= dma_len;
			dma_addr += dma_len;
			++tx_queue->insert_count;
		} while (len);

		/* Transfer ownership of the unmapping to the final buffer */
		buffer->unmap_single = unmap_single;
		buffer->unmap_len = unmap_len;
		unmap_len = 0;

		/* Get address and size of next fragment */
		if (i >= skb_shinfo(skb)->nr_frags)
			break;
		fragment = &skb_shinfo(skb)->frags[i];
		len = fragment->size;
		page = fragment->page;
		page_offset = fragment->page_offset;
		i++;
		/* Map for DMA */
		unmap_single = false;
		dma_addr = pci_map_page(pci_dev, page, page_offset, len,
					PCI_DMA_TODEVICE);
	}

	/* Transfer ownership of the skb to the final buffer */
	buffer->skb = skb;
	buffer->continuation = false;

	/* Pass off to hardware */
	efx_nic_push_buffers(tx_queue);

	return NETDEV_TX_OK;

 pci_err:
	netif_err(efx, tx_err, efx->net_dev,
		  " TX queue %d could not map skb with %d bytes %d "
		  "fragments for DMA\n", tx_queue->queue, skb->len,
		  skb_shinfo(skb)->nr_frags + 1);

	/* Mark the packet as transmitted, and free the SKB ourselves */
	dev_kfree_skb_any(skb);
	goto unwind;

 stop:
	rc = NETDEV_TX_BUSY;

	if (tx_queue->stopped == 1)
		efx_stop_queue(tx_queue->channel);

 unwind:
	/* Work backwards until we hit the original insert pointer value */
	while (tx_queue->insert_count != tx_queue->write_count) {
		--tx_queue->insert_count;
		insert_ptr = tx_queue->insert_count & tx_queue->ptr_mask;
		buffer = &tx_queue->buffer[insert_ptr];
		efx_dequeue_buffer(tx_queue, buffer);
		buffer->len = 0;
	}

	/* Free the fragment we were mid-way through pushing */
	if (unmap_len) {
		if (unmap_single)
			pci_unmap_single(pci_dev, unmap_addr, unmap_len,
					 PCI_DMA_TODEVICE);
		else
			pci_unmap_page(pci_dev, unmap_addr, unmap_len,
				       PCI_DMA_TODEVICE);
	}

	return rc;
}

/* Remove packets from the TX queue
 *
 * This removes packets from the TX queue, up to and including the
 * specified index.
 */
static void efx_dequeue_buffers(struct efx_tx_queue *tx_queue,
				unsigned int index)
{
	struct efx_nic *efx = tx_queue->efx;
	unsigned int stop_index, read_ptr;

	stop_index = (index + 1) & tx_queue->ptr_mask;
	read_ptr = tx_queue->read_count & tx_queue->ptr_mask;

	while (read_ptr != stop_index) {
		struct efx_tx_buffer *buffer = &tx_queue->buffer[read_ptr];
		if (unlikely(buffer->len == 0)) {
			netif_err(efx, tx_err, efx->net_dev,
				  "TX queue %d spurious TX completion id %x\n",
				  tx_queue->queue, read_ptr);
			efx_schedule_reset(efx, RESET_TYPE_TX_SKIP);
			return;
		}

		efx_dequeue_buffer(tx_queue, buffer);
		buffer->continuation = true;
		buffer->len = 0;

		++tx_queue->read_count;
		read_ptr = tx_queue->read_count & tx_queue->ptr_mask;
	}
}

/* Initiate a packet transmission.  We use one channel per CPU
 * (sharing when we have more CPUs than channels).  On Falcon, the TX
 * completion events will be directed back to the CPU that transmitted
 * the packet, which should be cache-efficient.
 *
 * Context: non-blocking.
 * Note that returning anything other than NETDEV_TX_OK will cause the
 * OS to free the skb.
 */
netdev_tx_t efx_hard_start_xmit(struct sk_buff *skb,
				      struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_tx_queue *tx_queue;

	if (unlikely(efx->port_inhibited))
		return NETDEV_TX_BUSY;

	tx_queue = efx_get_tx_queue(efx, skb_get_queue_mapping(skb),
				    skb->ip_summed == CHECKSUM_PARTIAL ?
				    EFX_TXQ_TYPE_OFFLOAD : 0);

	return efx_enqueue_skb(tx_queue, skb);
}

void efx_xmit_done(struct efx_tx_queue *tx_queue, unsigned int index)
{
	unsigned fill_level;
	struct efx_nic *efx = tx_queue->efx;
	struct netdev_queue *queue;

	EFX_BUG_ON_PARANOID(index > tx_queue->ptr_mask);

	efx_dequeue_buffers(tx_queue, index);

	/* See if we need to restart the netif queue.  This barrier
	 * separates the update of read_count from the test of
	 * stopped. */
	smp_mb();
	if (unlikely(tx_queue->stopped) && likely(efx->port_enabled)) {
		fill_level = tx_queue->insert_count - tx_queue->read_count;
		if (fill_level < EFX_TXQ_THRESHOLD(efx)) {
			EFX_BUG_ON_PARANOID(!efx_dev_registered(efx));

			/* Do this under netif_tx_lock(), to avoid racing
			 * with efx_xmit(). */
			queue = netdev_get_tx_queue(
				efx->net_dev,
				tx_queue->queue / EFX_TXQ_TYPES);
			__netif_tx_lock(queue, smp_processor_id());
			if (tx_queue->stopped) {
				tx_queue->stopped = 0;
				efx_wake_queue(tx_queue->channel);
			}
			__netif_tx_unlock(queue);
		}
	}

	/* Check whether the hardware queue is now empty */
	if ((int)(tx_queue->read_count - tx_queue->old_write_count) >= 0) {
		tx_queue->old_write_count = ACCESS_ONCE(tx_queue->write_count);
		if (tx_queue->read_count == tx_queue->old_write_count) {
			smp_mb();
			tx_queue->empty_read_count =
				tx_queue->read_count | EFX_EMPTY_COUNT_VALID;
		}
	}
}

int efx_probe_tx_queue(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	unsigned int entries;
	int i, rc;

	/* Create the smallest power-of-two aligned ring */
	entries = max(roundup_pow_of_two(efx->txq_entries), EFX_MIN_DMAQ_SIZE);
	EFX_BUG_ON_PARANOID(entries > EFX_MAX_DMAQ_SIZE);
	tx_queue->ptr_mask = entries - 1;

	netif_dbg(efx, probe, efx->net_dev,
		  "creating TX queue %d size %#x mask %#x\n",
		  tx_queue->queue, efx->txq_entries, tx_queue->ptr_mask);

	/* Allocate software ring */
	tx_queue->buffer = kzalloc(entries * sizeof(*tx_queue->buffer),
				   GFP_KERNEL);
	if (!tx_queue->buffer)
		return -ENOMEM;
	for (i = 0; i <= tx_queue->ptr_mask; ++i)
		tx_queue->buffer[i].continuation = true;

	/* Allocate hardware ring */
	rc = efx_nic_probe_tx(tx_queue);
	if (rc)
		goto fail;

	return 0;

 fail:
	kfree(tx_queue->buffer);
	tx_queue->buffer = NULL;
	return rc;
}

void efx_init_tx_queue(struct efx_tx_queue *tx_queue)
{
	netif_dbg(tx_queue->efx, drv, tx_queue->efx->net_dev,
		  "initialising TX queue %d\n", tx_queue->queue);

	tx_queue->insert_count = 0;
	tx_queue->write_count = 0;
	tx_queue->old_write_count = 0;
	tx_queue->read_count = 0;
	tx_queue->old_read_count = 0;
	tx_queue->empty_read_count = 0 | EFX_EMPTY_COUNT_VALID;
	BUG_ON(tx_queue->stopped);

	/* Set up TX descriptor ring */
	efx_nic_init_tx(tx_queue);
}

void efx_release_tx_buffers(struct efx_tx_queue *tx_queue)
{
	struct efx_tx_buffer *buffer;

	if (!tx_queue->buffer)
		return;

	/* Free any buffers left in the ring */
	while (tx_queue->read_count != tx_queue->write_count) {
		buffer = &tx_queue->buffer[tx_queue->read_count & tx_queue->ptr_mask];
		efx_dequeue_buffer(tx_queue, buffer);
		buffer->continuation = true;
		buffer->len = 0;

		++tx_queue->read_count;
	}
}

void efx_fini_tx_queue(struct efx_tx_queue *tx_queue)
{
	netif_dbg(tx_queue->efx, drv, tx_queue->efx->net_dev,
		  "shutting down TX queue %d\n", tx_queue->queue);

	/* Flush TX queue, remove descriptor ring */
	efx_nic_fini_tx(tx_queue);

	efx_release_tx_buffers(tx_queue);

	/* Free up TSO header cache */
	efx_fini_tso(tx_queue);

	/* Release queue's stop on port, if any */
	if (tx_queue->stopped) {
		tx_queue->stopped = 0;
		efx_wake_queue(tx_queue->channel);
	}
}

void efx_remove_tx_queue(struct efx_tx_queue *tx_queue)
{
	netif_dbg(tx_queue->efx, drv, tx_queue->efx->net_dev,
		  "destroying TX queue %d\n", tx_queue->queue);
	efx_nic_remove_tx(tx_queue);

	kfree(tx_queue->buffer);
	tx_queue->buffer = NULL;
}


/* Efx TCP segmentation acceleration.
 *
 * Why?  Because by doing it here in the driver we can go significantly
 * faster than the GSO.
 *
 * Requires TX checksum offload support.
 */

/* Number of bytes inserted at the start of a TSO header buffer,
 * similar to NET_IP_ALIGN.
 */
#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
#define TSOH_OFFSET	0
#else
#define TSOH_OFFSET	NET_IP_ALIGN
#endif

#define TSOH_BUFFER(tsoh)	((u8 *)(tsoh + 1) + TSOH_OFFSET)

/* Total size of struct efx_tso_header, buffer and padding */
#define TSOH_SIZE(hdr_len)					\
	(sizeof(struct efx_tso_header) + TSOH_OFFSET + hdr_len)

/* Size of blocks on free list.  Larger blocks must be allocated from
 * the heap.
 */
#define TSOH_STD_SIZE		128

#define PTR_DIFF(p1, p2)  ((u8 *)(p1) - (u8 *)(p2))
#define ETH_HDR_LEN(skb)  (skb_network_header(skb) - (skb)->data)
#define SKB_TCP_OFF(skb)  PTR_DIFF(tcp_hdr(skb), (skb)->data)
#define SKB_IPV4_OFF(skb) PTR_DIFF(ip_hdr(skb), (skb)->data)
#define SKB_IPV6_OFF(skb) PTR_DIFF(ipv6_hdr(skb), (skb)->data)

/**
 * struct tso_state - TSO state for an SKB
 * @out_len: Remaining length in current segment
 * @seqnum: Current sequence number
 * @ipv4_id: Current IPv4 ID, host endian
 * @packet_space: Remaining space in current packet
 * @dma_addr: DMA address of current position
 * @in_len: Remaining length in current SKB fragment
 * @unmap_len: Length of SKB fragment
 * @unmap_addr: DMA address of SKB fragment
 * @unmap_single: DMA single vs page mapping flag
 * @protocol: Network protocol (after any VLAN header)
 * @header_len: Number of bytes of header
 * @full_packet_size: Number of bytes to put in each outgoing segment
 *
 * The state used during segmentation.  It is put into this data structure
 * just to make it easy to pass into inline functions.
 */
struct tso_state {
	/* Output position */
	unsigned out_len;
	unsigned seqnum;
	unsigned ipv4_id;
	unsigned packet_space;

	/* Input position */
	dma_addr_t dma_addr;
	unsigned in_len;
	unsigned unmap_len;
	dma_addr_t unmap_addr;
	bool unmap_single;

	__be16 protocol;
	unsigned header_len;
	int full_packet_size;
};


/*
 * Verify that our various assumptions about sk_buffs and the conditions
 * under which TSO will be attempted hold true.  Return the protocol number.
 */
static __be16 efx_tso_check_protocol(struct sk_buff *skb)
{
	__be16 protocol = skb->protocol;

	EFX_BUG_ON_PARANOID(((struct ethhdr *)skb->data)->h_proto !=
			    protocol);
	if (protocol == htons(ETH_P_8021Q)) {
		/* Find the encapsulated protocol; reset network header
		 * and transport header based on that. */
		struct vlan_ethhdr *veh = (struct vlan_ethhdr *)skb->data;
		protocol = veh->h_vlan_encapsulated_proto;
		skb_set_network_header(skb, sizeof(*veh));
		if (protocol == htons(ETH_P_IP))
			skb_set_transport_header(skb, sizeof(*veh) +
						 4 * ip_hdr(skb)->ihl);
		else if (protocol == htons(ETH_P_IPV6))
			skb_set_transport_header(skb, sizeof(*veh) +
						 sizeof(struct ipv6hdr));
	}

	if (protocol == htons(ETH_P_IP)) {
		EFX_BUG_ON_PARANOID(ip_hdr(skb)->protocol != IPPROTO_TCP);
	} else {
		EFX_BUG_ON_PARANOID(protocol != htons(ETH_P_IPV6));
		EFX_BUG_ON_PARANOID(ipv6_hdr(skb)->nexthdr != NEXTHDR_TCP);
	}
	EFX_BUG_ON_PARANOID((PTR_DIFF(tcp_hdr(skb), skb->data)
			     + (tcp_hdr(skb)->doff << 2u)) >
			    skb_headlen(skb));

	return protocol;
}


/*
 * Allocate a page worth of efx_tso_header structures, and string them
 * into the tx_queue->tso_headers_free linked list. Return 0 or -ENOMEM.
 */
static int efx_tsoh_block_alloc(struct efx_tx_queue *tx_queue)
{

	struct pci_dev *pci_dev = tx_queue->efx->pci_dev;
	struct efx_tso_header *tsoh;
	dma_addr_t dma_addr;
	u8 *base_kva, *kva;

	base_kva = pci_alloc_consistent(pci_dev, PAGE_SIZE, &dma_addr);
	if (base_kva == NULL) {
		netif_err(tx_queue->efx, tx_err, tx_queue->efx->net_dev,
			  "Unable to allocate page for TSO headers\n");
		return -ENOMEM;
	}

	/* pci_alloc_consistent() allocates pages. */
	EFX_BUG_ON_PARANOID(dma_addr & (PAGE_SIZE - 1u));

	for (kva = base_kva; kva < base_kva + PAGE_SIZE; kva += TSOH_STD_SIZE) {
		tsoh = (struct efx_tso_header *)kva;
		tsoh->dma_addr = dma_addr + (TSOH_BUFFER(tsoh) - base_kva);
		tsoh->next = tx_queue->tso_headers_free;
		tx_queue->tso_headers_free = tsoh;
	}

	return 0;
}


/* Free up a TSO header, and all others in the same page. */
static void efx_tsoh_block_free(struct efx_tx_queue *tx_queue,
				struct efx_tso_header *tsoh,
				struct pci_dev *pci_dev)
{
	struct efx_tso_header **p;
	unsigned long base_kva;
	dma_addr_t base_dma;

	base_kva = (unsigned long)tsoh & PAGE_MASK;
	base_dma = tsoh->dma_addr & PAGE_MASK;

	p = &tx_queue->tso_headers_free;
	while (*p != NULL) {
		if (((unsigned long)*p & PAGE_MASK) == base_kva)
			*p = (*p)->next;
		else
			p = &(*p)->next;
	}

	pci_free_consistent(pci_dev, PAGE_SIZE, (void *)base_kva, base_dma);
}

static struct efx_tso_header *
efx_tsoh_heap_alloc(struct efx_tx_queue *tx_queue, size_t header_len)
{
	struct efx_tso_header *tsoh;

	tsoh = kmalloc(TSOH_SIZE(header_len), GFP_ATOMIC | GFP_DMA);
	if (unlikely(!tsoh))
		return NULL;

	tsoh->dma_addr = pci_map_single(tx_queue->efx->pci_dev,
					TSOH_BUFFER(tsoh), header_len,
					PCI_DMA_TODEVICE);
	if (unlikely(pci_dma_mapping_error(tx_queue->efx->pci_dev,
					   tsoh->dma_addr))) {
		kfree(tsoh);
		return NULL;
	}

	tsoh->unmap_len = header_len;
	return tsoh;
}

static void
efx_tsoh_heap_free(struct efx_tx_queue *tx_queue, struct efx_tso_header *tsoh)
{
	pci_unmap_single(tx_queue->efx->pci_dev,
			 tsoh->dma_addr, tsoh->unmap_len,
			 PCI_DMA_TODEVICE);
	kfree(tsoh);
}

/**
 * efx_tx_queue_insert - push descriptors onto the TX queue
 * @tx_queue:		Efx TX queue
 * @dma_addr:		DMA address of fragment
 * @len:		Length of fragment
 * @final_buffer:	The final buffer inserted into the queue
 *
 * Push descriptors onto the TX queue.  Return 0 on success or 1 if
 * @tx_queue full.
 */
static int efx_tx_queue_insert(struct efx_tx_queue *tx_queue,
			       dma_addr_t dma_addr, unsigned len,
			       struct efx_tx_buffer **final_buffer)
{
	struct efx_tx_buffer *buffer;
	struct efx_nic *efx = tx_queue->efx;
	unsigned dma_len, fill_level, insert_ptr;
	int q_space;

	EFX_BUG_ON_PARANOID(len <= 0);

	fill_level = tx_queue->insert_count - tx_queue->old_read_count;
	/* -1 as there is no way to represent all descriptors used */
	q_space = efx->txq_entries - 1 - fill_level;

	while (1) {
		if (unlikely(q_space-- <= 0)) {
			/* It might be that completions have happened
			 * since the xmit path last checked.  Update
			 * the xmit path's copy of read_count.
			 */
			++tx_queue->stopped;
			/* This memory barrier protects the change of
			 * stopped from the access of read_count. */
			smp_mb();
			tx_queue->old_read_count =
				ACCESS_ONCE(tx_queue->read_count);
			fill_level = (tx_queue->insert_count
				      - tx_queue->old_read_count);
			q_space = efx->txq_entries - 1 - fill_level;
			if (unlikely(q_space-- <= 0)) {
				*final_buffer = NULL;
				return 1;
			}
			smp_mb();
			--tx_queue->stopped;
		}

		insert_ptr = tx_queue->insert_count & tx_queue->ptr_mask;
		buffer = &tx_queue->buffer[insert_ptr];
		++tx_queue->insert_count;

		EFX_BUG_ON_PARANOID(tx_queue->insert_count -
				    tx_queue->read_count >=
				    efx->txq_entries);

		efx_tsoh_free(tx_queue, buffer);
		EFX_BUG_ON_PARANOID(buffer->len);
		EFX_BUG_ON_PARANOID(buffer->unmap_len);
		EFX_BUG_ON_PARANOID(buffer->skb);
		EFX_BUG_ON_PARANOID(!buffer->continuation);
		EFX_BUG_ON_PARANOID(buffer->tsoh);

		buffer->dma_addr = dma_addr;

		dma_len = efx_max_tx_len(efx, dma_addr);

		/* If there is enough space to send then do so */
		if (dma_len >= len)
			break;

		buffer->len = dma_len; /* Don't set the other members */
		dma_addr += dma_len;
		len -= dma_len;
	}

	EFX_BUG_ON_PARANOID(!len);
	buffer->len = len;
	*final_buffer = buffer;
	return 0;
}


/*
 * Put a TSO header into the TX queue.
 *
 * This is special-cased because we know that it is small enough to fit in
 * a single fragment, and we know it doesn't cross a page boundary.  It
 * also allows us to not worry about end-of-packet etc.
 */
static void efx_tso_put_header(struct efx_tx_queue *tx_queue,
			       struct efx_tso_header *tsoh, unsigned len)
{
	struct efx_tx_buffer *buffer;

	buffer = &tx_queue->buffer[tx_queue->insert_count & tx_queue->ptr_mask];
	efx_tsoh_free(tx_queue, buffer);
	EFX_BUG_ON_PARANOID(buffer->len);
	EFX_BUG_ON_PARANOID(buffer->unmap_len);
	EFX_BUG_ON_PARANOID(buffer->skb);
	EFX_BUG_ON_PARANOID(!buffer->continuation);
	EFX_BUG_ON_PARANOID(buffer->tsoh);
	buffer->len = len;
	buffer->dma_addr = tsoh->dma_addr;
	buffer->tsoh = tsoh;

	++tx_queue->insert_count;
}


/* Remove descriptors put into a tx_queue. */
static void efx_enqueue_unwind(struct efx_tx_queue *tx_queue)
{
	struct efx_tx_buffer *buffer;
	dma_addr_t unmap_addr;

	/* Work backwards until we hit the original insert pointer value */
	while (tx_queue->insert_count != tx_queue->write_count) {
		--tx_queue->insert_count;
		buffer = &tx_queue->buffer[tx_queue->insert_count &
					   tx_queue->ptr_mask];
		efx_tsoh_free(tx_queue, buffer);
		EFX_BUG_ON_PARANOID(buffer->skb);
		if (buffer->unmap_len) {
			unmap_addr = (buffer->dma_addr + buffer->len -
				      buffer->unmap_len);
			if (buffer->unmap_single)
				pci_unmap_single(tx_queue->efx->pci_dev,
						 unmap_addr, buffer->unmap_len,
						 PCI_DMA_TODEVICE);
			else
				pci_unmap_page(tx_queue->efx->pci_dev,
					       unmap_addr, buffer->unmap_len,
					       PCI_DMA_TODEVICE);
			buffer->unmap_len = 0;
		}
		buffer->len = 0;
		buffer->continuation = true;
	}
}


/* Parse the SKB header and initialise state. */
static void tso_start(struct tso_state *st, const struct sk_buff *skb)
{
	/* All ethernet/IP/TCP headers combined size is TCP header size
	 * plus offset of TCP header relative to start of packet.
	 */
	st->header_len = ((tcp_hdr(skb)->doff << 2u)
			  + PTR_DIFF(tcp_hdr(skb), skb->data));
	st->full_packet_size = st->header_len + skb_shinfo(skb)->gso_size;

	if (st->protocol == htons(ETH_P_IP))
		st->ipv4_id = ntohs(ip_hdr(skb)->id);
	else
		st->ipv4_id = 0;
	st->seqnum = ntohl(tcp_hdr(skb)->seq);

	EFX_BUG_ON_PARANOID(tcp_hdr(skb)->urg);
	EFX_BUG_ON_PARANOID(tcp_hdr(skb)->syn);
	EFX_BUG_ON_PARANOID(tcp_hdr(skb)->rst);

	st->packet_space = st->full_packet_size;
	st->out_len = skb->len - st->header_len;
	st->unmap_len = 0;
	st->unmap_single = false;
}

static int tso_get_fragment(struct tso_state *st, struct efx_nic *efx,
			    skb_frag_t *frag)
{
	st->unmap_addr = pci_map_page(efx->pci_dev, frag->page,
				      frag->page_offset, frag->size,
				      PCI_DMA_TODEVICE);
	if (likely(!pci_dma_mapping_error(efx->pci_dev, st->unmap_addr))) {
		st->unmap_single = false;
		st->unmap_len = frag->size;
		st->in_len = frag->size;
		st->dma_addr = st->unmap_addr;
		return 0;
	}
	return -ENOMEM;
}

static int tso_get_head_fragment(struct tso_state *st, struct efx_nic *efx,
				 const struct sk_buff *skb)
{
	int hl = st->header_len;
	int len = skb_headlen(skb) - hl;

	st->unmap_addr = pci_map_single(efx->pci_dev, skb->data + hl,
					len, PCI_DMA_TODEVICE);
	if (likely(!pci_dma_mapping_error(efx->pci_dev, st->unmap_addr))) {
		st->unmap_single = true;
		st->unmap_len = len;
		st->in_len = len;
		st->dma_addr = st->unmap_addr;
		return 0;
	}
	return -ENOMEM;
}


/**
 * tso_fill_packet_with_fragment - form descriptors for the current fragment
 * @tx_queue:		Efx TX queue
 * @skb:		Socket buffer
 * @st:			TSO state
 *
 * Form descriptors for the current fragment, until we reach the end
 * of fragment or end-of-packet.  Return 0 on success, 1 if not enough
 * space in @tx_queue.
 */
static int tso_fill_packet_with_fragment(struct efx_tx_queue *tx_queue,
					 const struct sk_buff *skb,
					 struct tso_state *st)
{
	struct efx_tx_buffer *buffer;
	int n, end_of_packet, rc;

	if (st->in_len == 0)
		return 0;
	if (st->packet_space == 0)
		return 0;

	EFX_BUG_ON_PARANOID(st->in_len <= 0);
	EFX_BUG_ON_PARANOID(st->packet_space <= 0);

	n = min(st->in_len, st->packet_space);

	st->packet_space -= n;
	st->out_len -= n;
	st->in_len -= n;

	rc = efx_tx_queue_insert(tx_queue, st->dma_addr, n, &buffer);
	if (likely(rc == 0)) {
		if (st->out_len == 0)
			/* Transfer ownership of the skb */
			buffer->skb = skb;

		end_of_packet = st->out_len == 0 || st->packet_space == 0;
		buffer->continuation = !end_of_packet;

		if (st->in_len == 0) {
			/* Transfer ownership of the pci mapping */
			buffer->unmap_len = st->unmap_len;
			buffer->unmap_single = st->unmap_single;
			st->unmap_len = 0;
		}
	}

	st->dma_addr += n;
	return rc;
}


/**
 * tso_start_new_packet - generate a new header and prepare for the new packet
 * @tx_queue:		Efx TX queue
 * @skb:		Socket buffer
 * @st:			TSO state
 *
 * Generate a new header and prepare for the new packet.  Return 0 on
 * success, or -1 if failed to alloc header.
 */
static int tso_start_new_packet(struct efx_tx_queue *tx_queue,
				const struct sk_buff *skb,
				struct tso_state *st)
{
	struct efx_tso_header *tsoh;
	struct tcphdr *tsoh_th;
	unsigned ip_length;
	u8 *header;

	/* Allocate a DMA-mapped header buffer. */
	if (likely(TSOH_SIZE(st->header_len) <= TSOH_STD_SIZE)) {
		if (tx_queue->tso_headers_free == NULL) {
			if (efx_tsoh_block_alloc(tx_queue))
				return -1;
		}
		EFX_BUG_ON_PARANOID(!tx_queue->tso_headers_free);
		tsoh = tx_queue->tso_headers_free;
		tx_queue->tso_headers_free = tsoh->next;
		tsoh->unmap_len = 0;
	} else {
		tx_queue->tso_long_headers++;
		tsoh = efx_tsoh_heap_alloc(tx_queue, st->header_len);
		if (unlikely(!tsoh))
			return -1;
	}

	header = TSOH_BUFFER(tsoh);
	tsoh_th = (struct tcphdr *)(header + SKB_TCP_OFF(skb));

	/* Copy and update the headers. */
	memcpy(header, skb->data, st->header_len);

	tsoh_th->seq = htonl(st->seqnum);
	st->seqnum += skb_shinfo(skb)->gso_size;
	if (st->out_len > skb_shinfo(skb)->gso_size) {
		/* This packet will not finish the TSO burst. */
		ip_length = st->full_packet_size - ETH_HDR_LEN(skb);
		tsoh_th->fin = 0;
		tsoh_th->psh = 0;
	} else {
		/* This packet will be the last in the TSO burst. */
		ip_length = st->header_len - ETH_HDR_LEN(skb) + st->out_len;
		tsoh_th->fin = tcp_hdr(skb)->fin;
		tsoh_th->psh = tcp_hdr(skb)->psh;
	}

	if (st->protocol == htons(ETH_P_IP)) {
		struct iphdr *tsoh_iph =
			(struct iphdr *)(header + SKB_IPV4_OFF(skb));

		tsoh_iph->tot_len = htons(ip_length);

		/* Linux leaves suitable gaps in the IP ID space for us to fill. */
		tsoh_iph->id = htons(st->ipv4_id);
		st->ipv4_id++;
	} else {
		struct ipv6hdr *tsoh_iph =
			(struct ipv6hdr *)(header + SKB_IPV6_OFF(skb));

		tsoh_iph->payload_len = htons(ip_length - sizeof(*tsoh_iph));
	}

	st->packet_space = skb_shinfo(skb)->gso_size;
	++tx_queue->tso_packets;

	/* Form a descriptor for this header. */
	efx_tso_put_header(tx_queue, tsoh, st->header_len);

	return 0;
}


/**
 * efx_enqueue_skb_tso - segment and transmit a TSO socket buffer
 * @tx_queue:		Efx TX queue
 * @skb:		Socket buffer
 *
 * Context: You must hold netif_tx_lock() to call this function.
 *
 * Add socket buffer @skb to @tx_queue, doing TSO or return != 0 if
 * @skb was not enqueued.  In all cases @skb is consumed.  Return
 * %NETDEV_TX_OK or %NETDEV_TX_BUSY.
 */
static int efx_enqueue_skb_tso(struct efx_tx_queue *tx_queue,
			       struct sk_buff *skb)
{
	struct efx_nic *efx = tx_queue->efx;
	int frag_i, rc, rc2 = NETDEV_TX_OK;
	struct tso_state state;

	/* Find the packet protocol and sanity-check it */
	state.protocol = efx_tso_check_protocol(skb);

	EFX_BUG_ON_PARANOID(tx_queue->write_count != tx_queue->insert_count);

	tso_start(&state, skb);

	/* Assume that skb header area contains exactly the headers, and
	 * all payload is in the frag list.
	 */
	if (skb_headlen(skb) == state.header_len) {
		/* Grab the first payload fragment. */
		EFX_BUG_ON_PARANOID(skb_shinfo(skb)->nr_frags < 1);
		frag_i = 0;
		rc = tso_get_fragment(&state, efx,
				      skb_shinfo(skb)->frags + frag_i);
		if (rc)
			goto mem_err;
	} else {
		rc = tso_get_head_fragment(&state, efx, skb);
		if (rc)
			goto mem_err;
		frag_i = -1;
	}

	if (tso_start_new_packet(tx_queue, skb, &state) < 0)
		goto mem_err;

	while (1) {
		rc = tso_fill_packet_with_fragment(tx_queue, skb, &state);
		if (unlikely(rc))
			goto stop;

		/* Move onto the next fragment? */
		if (state.in_len == 0) {
			if (++frag_i >= skb_shinfo(skb)->nr_frags)
				/* End of payload reached. */
				break;
			rc = tso_get_fragment(&state, efx,
					      skb_shinfo(skb)->frags + frag_i);
			if (rc)
				goto mem_err;
		}

		/* Start at new packet? */
		if (state.packet_space == 0 &&
		    tso_start_new_packet(tx_queue, skb, &state) < 0)
			goto mem_err;
	}

	/* Pass off to hardware */
	efx_nic_push_buffers(tx_queue);

	tx_queue->tso_bursts++;
	return NETDEV_TX_OK;

 mem_err:
	netif_err(efx, tx_err, efx->net_dev,
		  "Out of memory for TSO headers, or PCI mapping error\n");
	dev_kfree_skb_any(skb);
	goto unwind;

 stop:
	rc2 = NETDEV_TX_BUSY;

	/* Stop the queue if it wasn't stopped before. */
	if (tx_queue->stopped == 1)
		efx_stop_queue(tx_queue->channel);

 unwind:
	/* Free the DMA mapping we were in the process of writing out */
	if (state.unmap_len) {
		if (state.unmap_single)
			pci_unmap_single(efx->pci_dev, state.unmap_addr,
					 state.unmap_len, PCI_DMA_TODEVICE);
		else
			pci_unmap_page(efx->pci_dev, state.unmap_addr,
				       state.unmap_len, PCI_DMA_TODEVICE);
	}

	efx_enqueue_unwind(tx_queue);
	return rc2;
}


/*
 * Free up all TSO datastructures associated with tx_queue. This
 * routine should be called only once the tx_queue is both empty and
 * will no longer be used.
 */
static void efx_fini_tso(struct efx_tx_queue *tx_queue)
{
	unsigned i;

	if (tx_queue->buffer) {
		for (i = 0; i <= tx_queue->ptr_mask; ++i)
			efx_tsoh_free(tx_queue, &tx_queue->buffer[i]);
	}

	while (tx_queue->tso_headers_free != NULL)
		efx_tsoh_block_free(tx_queue, tx_queue->tso_headers_free,
				    tx_queue->efx->pci_dev);
}
