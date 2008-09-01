/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/pci.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/highmem.h>
#include "net_driver.h"
#include "tx.h"
#include "efx.h"
#include "falcon.h"
#include "workarounds.h"

/*
 * TX descriptor ring full threshold
 *
 * The tx_queue descriptor ring fill-level must fall below this value
 * before we restart the netif queue
 */
#define EFX_NETDEV_TX_THRESHOLD(_tx_queue)	\
	(_tx_queue->efx->type->txd_ring_mask / 2u)

/* We want to be able to nest calls to netif_stop_queue(), since each
 * channel can have an individual stop on the queue.
 */
void efx_stop_queue(struct efx_nic *efx)
{
	spin_lock_bh(&efx->netif_stop_lock);
	EFX_TRACE(efx, "stop TX queue\n");

	atomic_inc(&efx->netif_stop_count);
	netif_stop_queue(efx->net_dev);

	spin_unlock_bh(&efx->netif_stop_lock);
}

/* Wake netif's TX queue
 * We want to be able to nest calls to netif_stop_queue(), since each
 * channel can have an individual stop on the queue.
 */
inline void efx_wake_queue(struct efx_nic *efx)
{
	local_bh_disable();
	if (atomic_dec_and_lock(&efx->netif_stop_count,
				&efx->netif_stop_lock)) {
		EFX_TRACE(efx, "waking TX queue\n");
		netif_wake_queue(efx->net_dev);
		spin_unlock(&efx->netif_stop_lock);
	}
	local_bh_enable();
}

static inline void efx_dequeue_buffer(struct efx_tx_queue *tx_queue,
				      struct efx_tx_buffer *buffer)
{
	if (buffer->unmap_len) {
		struct pci_dev *pci_dev = tx_queue->efx->pci_dev;
		if (buffer->unmap_single)
			pci_unmap_single(pci_dev, buffer->unmap_addr,
					 buffer->unmap_len, PCI_DMA_TODEVICE);
		else
			pci_unmap_page(pci_dev, buffer->unmap_addr,
				       buffer->unmap_len, PCI_DMA_TODEVICE);
		buffer->unmap_len = 0;
		buffer->unmap_single = 0;
	}

	if (buffer->skb) {
		dev_kfree_skb_any((struct sk_buff *) buffer->skb);
		buffer->skb = NULL;
		EFX_TRACE(tx_queue->efx, "TX queue %d transmission id %x "
			  "complete\n", tx_queue->queue, read_ptr);
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
			       const struct sk_buff *skb);
static void efx_fini_tso(struct efx_tx_queue *tx_queue);
static void efx_tsoh_heap_free(struct efx_tx_queue *tx_queue,
			       struct efx_tso_header *tsoh);

static inline void efx_tsoh_free(struct efx_tx_queue *tx_queue,
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
 * Returns NETDEV_TX_OK or NETDEV_TX_BUSY
 * You must hold netif_tx_lock() to call this function.
 */
static inline int efx_enqueue_skb(struct efx_tx_queue *tx_queue,
				  const struct sk_buff *skb)
{
	struct efx_nic *efx = tx_queue->efx;
	struct pci_dev *pci_dev = efx->pci_dev;
	struct efx_tx_buffer *buffer;
	skb_frag_t *fragment;
	struct page *page;
	int page_offset;
	unsigned int len, unmap_len = 0, fill_level, insert_ptr, misalign;
	dma_addr_t dma_addr, unmap_addr = 0;
	unsigned int dma_len;
	unsigned unmap_single;
	int q_space, i = 0;
	int rc = NETDEV_TX_OK;

	EFX_BUG_ON_PARANOID(tx_queue->write_count != tx_queue->insert_count);

	if (skb_shinfo((struct sk_buff *)skb)->gso_size)
		return efx_enqueue_skb_tso(tx_queue, skb);

	/* Get size of the initial fragment */
	len = skb_headlen(skb);

	fill_level = tx_queue->insert_count - tx_queue->old_read_count;
	q_space = efx->type->txd_ring_mask - 1 - fill_level;

	/* Map for DMA.  Use pci_map_single rather than pci_map_page
	 * since this is more efficient on machines with sparse
	 * memory.
	 */
	unmap_single = 1;
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
					*(volatile unsigned *)
					&tx_queue->read_count;
				fill_level = (tx_queue->insert_count
					      - tx_queue->old_read_count);
				q_space = (efx->type->txd_ring_mask - 1 -
					   fill_level);
				if (unlikely(q_space-- <= 0))
					goto stop;
				smp_mb();
				--tx_queue->stopped;
			}

			insert_ptr = (tx_queue->insert_count &
				      efx->type->txd_ring_mask);
			buffer = &tx_queue->buffer[insert_ptr];
			efx_tsoh_free(tx_queue, buffer);
			EFX_BUG_ON_PARANOID(buffer->tsoh);
			EFX_BUG_ON_PARANOID(buffer->skb);
			EFX_BUG_ON_PARANOID(buffer->len);
			EFX_BUG_ON_PARANOID(buffer->continuation != 1);
			EFX_BUG_ON_PARANOID(buffer->unmap_len);

			dma_len = (((~dma_addr) & efx->type->tx_dma_mask) + 1);
			if (likely(dma_len > len))
				dma_len = len;

			misalign = (unsigned)dma_addr & efx->type->bug5391_mask;
			if (misalign && dma_len + misalign > 512)
				dma_len = 512 - misalign;

			/* Fill out per descriptor fields */
			buffer->len = dma_len;
			buffer->dma_addr = dma_addr;
			len -= dma_len;
			dma_addr += dma_len;
			++tx_queue->insert_count;
		} while (len);

		/* Transfer ownership of the unmapping to the final buffer */
		buffer->unmap_addr = unmap_addr;
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
		unmap_single = 0;
		dma_addr = pci_map_page(pci_dev, page, page_offset, len,
					PCI_DMA_TODEVICE);
	}

	/* Transfer ownership of the skb to the final buffer */
	buffer->skb = skb;
	buffer->continuation = 0;

	/* Pass off to hardware */
	falcon_push_buffers(tx_queue);

	return NETDEV_TX_OK;

 pci_err:
	EFX_ERR_RL(efx, " TX queue %d could not map skb with %d bytes %d "
		   "fragments for DMA\n", tx_queue->queue, skb->len,
		   skb_shinfo(skb)->nr_frags + 1);

	/* Mark the packet as transmitted, and free the SKB ourselves */
	dev_kfree_skb_any((struct sk_buff *)skb);
	goto unwind;

 stop:
	rc = NETDEV_TX_BUSY;

	if (tx_queue->stopped == 1)
		efx_stop_queue(efx);

 unwind:
	/* Work backwards until we hit the original insert pointer value */
	while (tx_queue->insert_count != tx_queue->write_count) {
		--tx_queue->insert_count;
		insert_ptr = tx_queue->insert_count & efx->type->txd_ring_mask;
		buffer = &tx_queue->buffer[insert_ptr];
		efx_dequeue_buffer(tx_queue, buffer);
		buffer->len = 0;
	}

	/* Free the fragment we were mid-way through pushing */
	if (unmap_len)
		pci_unmap_page(pci_dev, unmap_addr, unmap_len,
			       PCI_DMA_TODEVICE);

	return rc;
}

/* Remove packets from the TX queue
 *
 * This removes packets from the TX queue, up to and including the
 * specified index.
 */
static inline void efx_dequeue_buffers(struct efx_tx_queue *tx_queue,
				       unsigned int index)
{
	struct efx_nic *efx = tx_queue->efx;
	unsigned int stop_index, read_ptr;
	unsigned int mask = tx_queue->efx->type->txd_ring_mask;

	stop_index = (index + 1) & mask;
	read_ptr = tx_queue->read_count & mask;

	while (read_ptr != stop_index) {
		struct efx_tx_buffer *buffer = &tx_queue->buffer[read_ptr];
		if (unlikely(buffer->len == 0)) {
			EFX_ERR(tx_queue->efx, "TX queue %d spurious TX "
				"completion id %x\n", tx_queue->queue,
				read_ptr);
			efx_schedule_reset(efx, RESET_TYPE_TX_SKIP);
			return;
		}

		efx_dequeue_buffer(tx_queue, buffer);
		buffer->continuation = 1;
		buffer->len = 0;

		++tx_queue->read_count;
		read_ptr = tx_queue->read_count & mask;
	}
}

/* Initiate a packet transmission on the specified TX queue.
 * Note that returning anything other than NETDEV_TX_OK will cause the
 * OS to free the skb.
 *
 * This function is split out from efx_hard_start_xmit to allow the
 * loopback test to direct packets via specific TX queues.  It is
 * therefore a non-static inline, so as not to penalise performance
 * for non-loopback transmissions.
 *
 * Context: netif_tx_lock held
 */
inline int efx_xmit(struct efx_nic *efx,
		    struct efx_tx_queue *tx_queue, struct sk_buff *skb)
{
	int rc;

	/* Map fragments for DMA and add to TX queue */
	rc = efx_enqueue_skb(tx_queue, skb);
	if (unlikely(rc != NETDEV_TX_OK))
		goto out;

	/* Update last TX timer */
	efx->net_dev->trans_start = jiffies;

 out:
	return rc;
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
int efx_hard_start_xmit(struct sk_buff *skb, struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_tx_queue *tx_queue;

	if (likely(skb->ip_summed == CHECKSUM_PARTIAL))
		tx_queue = &efx->tx_queue[EFX_TX_QUEUE_OFFLOAD_CSUM];
	else
		tx_queue = &efx->tx_queue[EFX_TX_QUEUE_NO_CSUM];

	return efx_xmit(efx, tx_queue, skb);
}

void efx_xmit_done(struct efx_tx_queue *tx_queue, unsigned int index)
{
	unsigned fill_level;
	struct efx_nic *efx = tx_queue->efx;

	EFX_BUG_ON_PARANOID(index > efx->type->txd_ring_mask);

	efx_dequeue_buffers(tx_queue, index);

	/* See if we need to restart the netif queue.  This barrier
	 * separates the update of read_count from the test of
	 * stopped. */
	smp_mb();
	if (unlikely(tx_queue->stopped)) {
		fill_level = tx_queue->insert_count - tx_queue->read_count;
		if (fill_level < EFX_NETDEV_TX_THRESHOLD(tx_queue)) {
			EFX_BUG_ON_PARANOID(!efx_dev_registered(efx));

			/* Do this under netif_tx_lock(), to avoid racing
			 * with efx_xmit(). */
			netif_tx_lock(efx->net_dev);
			if (tx_queue->stopped) {
				tx_queue->stopped = 0;
				efx_wake_queue(efx);
			}
			netif_tx_unlock(efx->net_dev);
		}
	}
}

int efx_probe_tx_queue(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	unsigned int txq_size;
	int i, rc;

	EFX_LOG(efx, "creating TX queue %d\n", tx_queue->queue);

	/* Allocate software ring */
	txq_size = (efx->type->txd_ring_mask + 1) * sizeof(*tx_queue->buffer);
	tx_queue->buffer = kzalloc(txq_size, GFP_KERNEL);
	if (!tx_queue->buffer)
		return -ENOMEM;
	for (i = 0; i <= efx->type->txd_ring_mask; ++i)
		tx_queue->buffer[i].continuation = 1;

	/* Allocate hardware ring */
	rc = falcon_probe_tx(tx_queue);
	if (rc)
		goto fail;

	return 0;

 fail:
	kfree(tx_queue->buffer);
	tx_queue->buffer = NULL;
	return rc;
}

int efx_init_tx_queue(struct efx_tx_queue *tx_queue)
{
	EFX_LOG(tx_queue->efx, "initialising TX queue %d\n", tx_queue->queue);

	tx_queue->insert_count = 0;
	tx_queue->write_count = 0;
	tx_queue->read_count = 0;
	tx_queue->old_read_count = 0;
	BUG_ON(tx_queue->stopped);

	/* Set up TX descriptor ring */
	return falcon_init_tx(tx_queue);
}

void efx_release_tx_buffers(struct efx_tx_queue *tx_queue)
{
	struct efx_tx_buffer *buffer;

	if (!tx_queue->buffer)
		return;

	/* Free any buffers left in the ring */
	while (tx_queue->read_count != tx_queue->write_count) {
		buffer = &tx_queue->buffer[tx_queue->read_count &
					   tx_queue->efx->type->txd_ring_mask];
		efx_dequeue_buffer(tx_queue, buffer);
		buffer->continuation = 1;
		buffer->len = 0;

		++tx_queue->read_count;
	}
}

void efx_fini_tx_queue(struct efx_tx_queue *tx_queue)
{
	EFX_LOG(tx_queue->efx, "shutting down TX queue %d\n", tx_queue->queue);

	/* Flush TX queue, remove descriptor ring */
	falcon_fini_tx(tx_queue);

	efx_release_tx_buffers(tx_queue);

	/* Free up TSO header cache */
	efx_fini_tso(tx_queue);

	/* Release queue's stop on port, if any */
	if (tx_queue->stopped) {
		tx_queue->stopped = 0;
		efx_wake_queue(tx_queue->efx);
	}
}

void efx_remove_tx_queue(struct efx_tx_queue *tx_queue)
{
	EFX_LOG(tx_queue->efx, "destroying TX queue %d\n", tx_queue->queue);
	falcon_remove_tx(tx_queue);

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
#if defined(__i386__) || defined(__x86_64__)
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

/**
 * struct tso_state - TSO state for an SKB
 * @remaining_len: Bytes of data we've yet to segment
 * @seqnum: Current sequence number
 * @packet_space: Remaining space in current packet
 * @ifc: Input fragment cursor.
 *	Where we are in the current fragment of the incoming SKB.  These
 *	values get updated in place when we split a fragment over
 *	multiple packets.
 * @p: Parameters.
 *	These values are set once at the start of the TSO send and do
 *	not get changed as the routine progresses.
 *
 * The state used during segmentation.  It is put into this data structure
 * just to make it easy to pass into inline functions.
 */
struct tso_state {
	unsigned remaining_len;
	unsigned seqnum;
	unsigned packet_space;

	struct {
		/* DMA address of current position */
		dma_addr_t dma_addr;
		/* Remaining length */
		unsigned int len;
		/* DMA address and length of the whole fragment */
		unsigned int unmap_len;
		dma_addr_t unmap_addr;
		struct page *page;
		unsigned page_off;
	} ifc;

	struct {
		/* The number of bytes of header */
		unsigned int header_length;

		/* The number of bytes to put in each outgoing segment. */
		int full_packet_size;

		/* Current IPv4 ID, host endian. */
		unsigned ipv4_id;
	} p;
};


/*
 * Verify that our various assumptions about sk_buffs and the conditions
 * under which TSO will be attempted hold true.
 */
static inline void efx_tso_check_safe(const struct sk_buff *skb)
{
	EFX_BUG_ON_PARANOID(skb->protocol != htons(ETH_P_IP));
	EFX_BUG_ON_PARANOID(((struct ethhdr *)skb->data)->h_proto !=
			    skb->protocol);
	EFX_BUG_ON_PARANOID(ip_hdr(skb)->protocol != IPPROTO_TCP);
	EFX_BUG_ON_PARANOID((PTR_DIFF(tcp_hdr(skb), skb->data)
			     + (tcp_hdr(skb)->doff << 2u)) >
			    skb_headlen(skb));
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
		EFX_ERR(tx_queue->efx, "Unable to allocate page for TSO"
			" headers\n");
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
 * @skb:		Only non-null for end of last segment
 * @end_of_packet:	True if last fragment in a packet
 * @unmap_addr:		DMA address of fragment for unmapping
 * @unmap_len:		Only set this in last segment of a fragment
 *
 * Push descriptors onto the TX queue.  Return 0 on success or 1 if
 * @tx_queue full.
 */
static int efx_tx_queue_insert(struct efx_tx_queue *tx_queue,
			       dma_addr_t dma_addr, unsigned len,
			       const struct sk_buff *skb, int end_of_packet,
			       dma_addr_t unmap_addr, unsigned unmap_len)
{
	struct efx_tx_buffer *buffer;
	struct efx_nic *efx = tx_queue->efx;
	unsigned dma_len, fill_level, insert_ptr, misalign;
	int q_space;

	EFX_BUG_ON_PARANOID(len <= 0);

	fill_level = tx_queue->insert_count - tx_queue->old_read_count;
	/* -1 as there is no way to represent all descriptors used */
	q_space = efx->type->txd_ring_mask - 1 - fill_level;

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
				*(volatile unsigned *)&tx_queue->read_count;
			fill_level = (tx_queue->insert_count
				      - tx_queue->old_read_count);
			q_space = efx->type->txd_ring_mask - 1 - fill_level;
			if (unlikely(q_space-- <= 0))
				return 1;
			smp_mb();
			--tx_queue->stopped;
		}

		insert_ptr = tx_queue->insert_count & efx->type->txd_ring_mask;
		buffer = &tx_queue->buffer[insert_ptr];
		++tx_queue->insert_count;

		EFX_BUG_ON_PARANOID(tx_queue->insert_count -
				    tx_queue->read_count >
				    efx->type->txd_ring_mask);

		efx_tsoh_free(tx_queue, buffer);
		EFX_BUG_ON_PARANOID(buffer->len);
		EFX_BUG_ON_PARANOID(buffer->unmap_len);
		EFX_BUG_ON_PARANOID(buffer->skb);
		EFX_BUG_ON_PARANOID(buffer->continuation != 1);
		EFX_BUG_ON_PARANOID(buffer->tsoh);

		buffer->dma_addr = dma_addr;

		/* Ensure we do not cross a boundary unsupported by H/W */
		dma_len = (~dma_addr & efx->type->tx_dma_mask) + 1;

		misalign = (unsigned)dma_addr & efx->type->bug5391_mask;
		if (misalign && dma_len + misalign > 512)
			dma_len = 512 - misalign;

		/* If there is enough space to send then do so */
		if (dma_len >= len)
			break;

		buffer->len = dma_len; /* Don't set the other members */
		dma_addr += dma_len;
		len -= dma_len;
	}

	EFX_BUG_ON_PARANOID(!len);
	buffer->len = len;
	buffer->skb = skb;
	buffer->continuation = !end_of_packet;
	buffer->unmap_addr = unmap_addr;
	buffer->unmap_len = unmap_len;
	return 0;
}


/*
 * Put a TSO header into the TX queue.
 *
 * This is special-cased because we know that it is small enough to fit in
 * a single fragment, and we know it doesn't cross a page boundary.  It
 * also allows us to not worry about end-of-packet etc.
 */
static inline void efx_tso_put_header(struct efx_tx_queue *tx_queue,
				      struct efx_tso_header *tsoh, unsigned len)
{
	struct efx_tx_buffer *buffer;

	buffer = &tx_queue->buffer[tx_queue->insert_count &
				   tx_queue->efx->type->txd_ring_mask];
	efx_tsoh_free(tx_queue, buffer);
	EFX_BUG_ON_PARANOID(buffer->len);
	EFX_BUG_ON_PARANOID(buffer->unmap_len);
	EFX_BUG_ON_PARANOID(buffer->skb);
	EFX_BUG_ON_PARANOID(buffer->continuation != 1);
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

	/* Work backwards until we hit the original insert pointer value */
	while (tx_queue->insert_count != tx_queue->write_count) {
		--tx_queue->insert_count;
		buffer = &tx_queue->buffer[tx_queue->insert_count &
					   tx_queue->efx->type->txd_ring_mask];
		efx_tsoh_free(tx_queue, buffer);
		EFX_BUG_ON_PARANOID(buffer->skb);
		buffer->len = 0;
		buffer->continuation = 1;
		if (buffer->unmap_len) {
			pci_unmap_page(tx_queue->efx->pci_dev,
				       buffer->unmap_addr,
				       buffer->unmap_len, PCI_DMA_TODEVICE);
			buffer->unmap_len = 0;
		}
	}
}


/* Parse the SKB header and initialise state. */
static inline void tso_start(struct tso_state *st, const struct sk_buff *skb)
{
	/* All ethernet/IP/TCP headers combined size is TCP header size
	 * plus offset of TCP header relative to start of packet.
	 */
	st->p.header_length = ((tcp_hdr(skb)->doff << 2u)
			       + PTR_DIFF(tcp_hdr(skb), skb->data));
	st->p.full_packet_size = (st->p.header_length
				  + skb_shinfo(skb)->gso_size);

	st->p.ipv4_id = ntohs(ip_hdr(skb)->id);
	st->seqnum = ntohl(tcp_hdr(skb)->seq);

	EFX_BUG_ON_PARANOID(tcp_hdr(skb)->urg);
	EFX_BUG_ON_PARANOID(tcp_hdr(skb)->syn);
	EFX_BUG_ON_PARANOID(tcp_hdr(skb)->rst);

	st->packet_space = st->p.full_packet_size;
	st->remaining_len = skb->len - st->p.header_length;
}


/**
 * tso_get_fragment - record fragment details and map for DMA
 * @st:			TSO state
 * @efx:		Efx NIC
 * @data:		Pointer to fragment data
 * @len:		Length of fragment
 *
 * Record fragment details and map for DMA.  Return 0 on success, or
 * -%ENOMEM if DMA mapping fails.
 */
static inline int tso_get_fragment(struct tso_state *st, struct efx_nic *efx,
				   int len, struct page *page, int page_off)
{

	st->ifc.unmap_addr = pci_map_page(efx->pci_dev, page, page_off,
					  len, PCI_DMA_TODEVICE);
	if (likely(!pci_dma_mapping_error(efx->pci_dev, st->ifc.unmap_addr))) {
		st->ifc.unmap_len = len;
		st->ifc.len = len;
		st->ifc.dma_addr = st->ifc.unmap_addr;
		st->ifc.page = page;
		st->ifc.page_off = page_off;
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
static inline int tso_fill_packet_with_fragment(struct efx_tx_queue *tx_queue,
						const struct sk_buff *skb,
						struct tso_state *st)
{

	int n, end_of_packet, rc;

	if (st->ifc.len == 0)
		return 0;
	if (st->packet_space == 0)
		return 0;

	EFX_BUG_ON_PARANOID(st->ifc.len <= 0);
	EFX_BUG_ON_PARANOID(st->packet_space <= 0);

	n = min(st->ifc.len, st->packet_space);

	st->packet_space -= n;
	st->remaining_len -= n;
	st->ifc.len -= n;
	st->ifc.page_off += n;
	end_of_packet = st->remaining_len == 0 || st->packet_space == 0;

	rc = efx_tx_queue_insert(tx_queue, st->ifc.dma_addr, n,
				 st->remaining_len ? NULL : skb,
				 end_of_packet, st->ifc.unmap_addr,
				 st->ifc.len ? 0 : st->ifc.unmap_len);

	st->ifc.dma_addr += n;

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
static inline int tso_start_new_packet(struct efx_tx_queue *tx_queue,
				       const struct sk_buff *skb,
				       struct tso_state *st)
{
	struct efx_tso_header *tsoh;
	struct iphdr *tsoh_iph;
	struct tcphdr *tsoh_th;
	unsigned ip_length;
	u8 *header;

	/* Allocate a DMA-mapped header buffer. */
	if (likely(TSOH_SIZE(st->p.header_length) <= TSOH_STD_SIZE)) {
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
		tsoh = efx_tsoh_heap_alloc(tx_queue, st->p.header_length);
		if (unlikely(!tsoh))
			return -1;
	}

	header = TSOH_BUFFER(tsoh);
	tsoh_th = (struct tcphdr *)(header + SKB_TCP_OFF(skb));
	tsoh_iph = (struct iphdr *)(header + SKB_IPV4_OFF(skb));

	/* Copy and update the headers. */
	memcpy(header, skb->data, st->p.header_length);

	tsoh_th->seq = htonl(st->seqnum);
	st->seqnum += skb_shinfo(skb)->gso_size;
	if (st->remaining_len > skb_shinfo(skb)->gso_size) {
		/* This packet will not finish the TSO burst. */
		ip_length = st->p.full_packet_size - ETH_HDR_LEN(skb);
		tsoh_th->fin = 0;
		tsoh_th->psh = 0;
	} else {
		/* This packet will be the last in the TSO burst. */
		ip_length = (st->p.header_length - ETH_HDR_LEN(skb)
			     + st->remaining_len);
		tsoh_th->fin = tcp_hdr(skb)->fin;
		tsoh_th->psh = tcp_hdr(skb)->psh;
	}
	tsoh_iph->tot_len = htons(ip_length);

	/* Linux leaves suitable gaps in the IP ID space for us to fill. */
	tsoh_iph->id = htons(st->p.ipv4_id);
	st->p.ipv4_id++;

	st->packet_space = skb_shinfo(skb)->gso_size;
	++tx_queue->tso_packets;

	/* Form a descriptor for this header. */
	efx_tso_put_header(tx_queue, tsoh, st->p.header_length);

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
			       const struct sk_buff *skb)
{
	int frag_i, rc, rc2 = NETDEV_TX_OK;
	struct tso_state state;
	skb_frag_t *f;

	/* Verify TSO is safe - these checks should never fail. */
	efx_tso_check_safe(skb);

	EFX_BUG_ON_PARANOID(tx_queue->write_count != tx_queue->insert_count);

	tso_start(&state, skb);

	/* Assume that skb header area contains exactly the headers, and
	 * all payload is in the frag list.
	 */
	if (skb_headlen(skb) == state.p.header_length) {
		/* Grab the first payload fragment. */
		EFX_BUG_ON_PARANOID(skb_shinfo(skb)->nr_frags < 1);
		frag_i = 0;
		f = &skb_shinfo(skb)->frags[frag_i];
		rc = tso_get_fragment(&state, tx_queue->efx,
				      f->size, f->page, f->page_offset);
		if (rc)
			goto mem_err;
	} else {
		/* It may look like this code fragment assumes that the
		 * skb->data portion does not cross a page boundary, but
		 * that is not the case.  It is guaranteed to be direct
		 * mapped memory, and therefore is physically contiguous,
		 * and so DMA will work fine.  kmap_atomic() on this region
		 * will just return the direct mapping, so that will work
		 * too.
		 */
		int page_off = (unsigned long)skb->data & (PAGE_SIZE - 1);
		int hl = state.p.header_length;
		rc = tso_get_fragment(&state, tx_queue->efx,
				      skb_headlen(skb) - hl,
				      virt_to_page(skb->data), page_off + hl);
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
		if (state.ifc.len == 0) {
			if (++frag_i >= skb_shinfo(skb)->nr_frags)
				/* End of payload reached. */
				break;
			f = &skb_shinfo(skb)->frags[frag_i];
			rc = tso_get_fragment(&state, tx_queue->efx,
					      f->size, f->page, f->page_offset);
			if (rc)
				goto mem_err;
		}

		/* Start at new packet? */
		if (state.packet_space == 0 &&
		    tso_start_new_packet(tx_queue, skb, &state) < 0)
			goto mem_err;
	}

	/* Pass off to hardware */
	falcon_push_buffers(tx_queue);

	tx_queue->tso_bursts++;
	return NETDEV_TX_OK;

 mem_err:
	EFX_ERR(tx_queue->efx, "Out of memory for TSO headers, or PCI mapping"
		" error\n");
	dev_kfree_skb_any((struct sk_buff *)skb);
	goto unwind;

 stop:
	rc2 = NETDEV_TX_BUSY;

	/* Stop the queue if it wasn't stopped before. */
	if (tx_queue->stopped == 1)
		efx_stop_queue(tx_queue->efx);

 unwind:
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
		for (i = 0; i <= tx_queue->efx->type->txd_ring_mask; ++i)
			efx_tsoh_free(tx_queue, &tx_queue->buffer[i]);
	}

	while (tx_queue->tso_headers_free != NULL)
		efx_tsoh_block_free(tx_queue, tx_queue->tso_headers_free,
				    tx_queue->efx->pci_dev);
}
