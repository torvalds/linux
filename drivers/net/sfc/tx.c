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
		if (unlikely(pci_dma_mapping_error(dma_addr)))
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
	struct efx_nic *efx = net_dev->priv;
	return efx_xmit(efx, &efx->tx_queue[0], skb);
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
			EFX_BUG_ON_PARANOID(!NET_DEV_REGISTERED(efx));

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
	if (!tx_queue->buffer) {
		rc = -ENOMEM;
		goto fail1;
	}
	for (i = 0; i <= efx->type->txd_ring_mask; ++i)
		tx_queue->buffer[i].continuation = 1;

	/* Allocate hardware ring */
	rc = falcon_probe_tx(tx_queue);
	if (rc)
		goto fail2;

	return 0;

 fail2:
	kfree(tx_queue->buffer);
	tx_queue->buffer = NULL;
 fail1:
	tx_queue->used = 0;

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
	tx_queue->used = 0;
}


