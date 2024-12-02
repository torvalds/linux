// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2013 Solarflare Communications Inc.
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
#include <linux/cache.h>
#include "net_driver.h"
#include "efx.h"
#include "io.h"
#include "nic.h"
#include "tx.h"
#include "workarounds.h"

static inline u8 *ef4_tx_get_copy_buffer(struct ef4_tx_queue *tx_queue,
					 struct ef4_tx_buffer *buffer)
{
	unsigned int index = ef4_tx_queue_get_insert_index(tx_queue);
	struct ef4_buffer *page_buf =
		&tx_queue->cb_page[index >> (PAGE_SHIFT - EF4_TX_CB_ORDER)];
	unsigned int offset =
		((index << EF4_TX_CB_ORDER) + NET_IP_ALIGN) & (PAGE_SIZE - 1);

	if (unlikely(!page_buf->addr) &&
	    ef4_nic_alloc_buffer(tx_queue->efx, page_buf, PAGE_SIZE,
				 GFP_ATOMIC))
		return NULL;
	buffer->dma_addr = page_buf->dma_addr + offset;
	buffer->unmap_len = 0;
	return (u8 *)page_buf->addr + offset;
}

u8 *ef4_tx_get_copy_buffer_limited(struct ef4_tx_queue *tx_queue,
				   struct ef4_tx_buffer *buffer, size_t len)
{
	if (len > EF4_TX_CB_SIZE)
		return NULL;
	return ef4_tx_get_copy_buffer(tx_queue, buffer);
}

static void ef4_dequeue_buffer(struct ef4_tx_queue *tx_queue,
			       struct ef4_tx_buffer *buffer,
			       unsigned int *pkts_compl,
			       unsigned int *bytes_compl)
{
	if (buffer->unmap_len) {
		struct device *dma_dev = &tx_queue->efx->pci_dev->dev;
		dma_addr_t unmap_addr = buffer->dma_addr - buffer->dma_offset;
		if (buffer->flags & EF4_TX_BUF_MAP_SINGLE)
			dma_unmap_single(dma_dev, unmap_addr, buffer->unmap_len,
					 DMA_TO_DEVICE);
		else
			dma_unmap_page(dma_dev, unmap_addr, buffer->unmap_len,
				       DMA_TO_DEVICE);
		buffer->unmap_len = 0;
	}

	if (buffer->flags & EF4_TX_BUF_SKB) {
		(*pkts_compl)++;
		(*bytes_compl) += buffer->skb->len;
		dev_consume_skb_any((struct sk_buff *)buffer->skb);
		netif_vdbg(tx_queue->efx, tx_done, tx_queue->efx->net_dev,
			   "TX queue %d transmission id %x complete\n",
			   tx_queue->queue, tx_queue->read_count);
	}

	buffer->len = 0;
	buffer->flags = 0;
}

unsigned int ef4_tx_max_skb_descs(struct ef4_nic *efx)
{
	/* This is probably too much since we don't have any TSO support;
	 * it's a left-over from when we had Software TSO.  But it's safer
	 * to leave it as-is than try to determine a new bound.
	 */
	/* Header and payload descriptor for each output segment, plus
	 * one for every input fragment boundary within a segment
	 */
	unsigned int max_descs = EF4_TSO_MAX_SEGS * 2 + MAX_SKB_FRAGS;

	/* Possibly one more per segment for the alignment workaround,
	 * or for option descriptors
	 */
	if (EF4_WORKAROUND_5391(efx))
		max_descs += EF4_TSO_MAX_SEGS;

	/* Possibly more for PCIe page boundaries within input fragments */
	if (PAGE_SIZE > EF4_PAGE_SIZE)
		max_descs += max_t(unsigned int, MAX_SKB_FRAGS,
				   DIV_ROUND_UP(GSO_LEGACY_MAX_SIZE,
						EF4_PAGE_SIZE));

	return max_descs;
}

static void ef4_tx_maybe_stop_queue(struct ef4_tx_queue *txq1)
{
	/* We need to consider both queues that the net core sees as one */
	struct ef4_tx_queue *txq2 = ef4_tx_queue_partner(txq1);
	struct ef4_nic *efx = txq1->efx;
	unsigned int fill_level;

	fill_level = max(txq1->insert_count - txq1->old_read_count,
			 txq2->insert_count - txq2->old_read_count);
	if (likely(fill_level < efx->txq_stop_thresh))
		return;

	/* We used the stale old_read_count above, which gives us a
	 * pessimistic estimate of the fill level (which may even
	 * validly be >= efx->txq_entries).  Now try again using
	 * read_count (more likely to be a cache miss).
	 *
	 * If we read read_count and then conditionally stop the
	 * queue, it is possible for the completion path to race with
	 * us and complete all outstanding descriptors in the middle,
	 * after which there will be no more completions to wake it.
	 * Therefore we stop the queue first, then read read_count
	 * (with a memory barrier to ensure the ordering), then
	 * restart the queue if the fill level turns out to be low
	 * enough.
	 */
	netif_tx_stop_queue(txq1->core_txq);
	smp_mb();
	txq1->old_read_count = READ_ONCE(txq1->read_count);
	txq2->old_read_count = READ_ONCE(txq2->read_count);

	fill_level = max(txq1->insert_count - txq1->old_read_count,
			 txq2->insert_count - txq2->old_read_count);
	EF4_BUG_ON_PARANOID(fill_level >= efx->txq_entries);
	if (likely(fill_level < efx->txq_stop_thresh)) {
		smp_mb();
		if (likely(!efx->loopback_selftest))
			netif_tx_start_queue(txq1->core_txq);
	}
}

static int ef4_enqueue_skb_copy(struct ef4_tx_queue *tx_queue,
				struct sk_buff *skb)
{
	unsigned int min_len = tx_queue->tx_min_size;
	unsigned int copy_len = skb->len;
	struct ef4_tx_buffer *buffer;
	u8 *copy_buffer;
	int rc;

	EF4_BUG_ON_PARANOID(copy_len > EF4_TX_CB_SIZE);

	buffer = ef4_tx_queue_get_insert_buffer(tx_queue);

	copy_buffer = ef4_tx_get_copy_buffer(tx_queue, buffer);
	if (unlikely(!copy_buffer))
		return -ENOMEM;

	rc = skb_copy_bits(skb, 0, copy_buffer, copy_len);
	EF4_WARN_ON_PARANOID(rc);
	if (unlikely(copy_len < min_len)) {
		memset(copy_buffer + copy_len, 0, min_len - copy_len);
		buffer->len = min_len;
	} else {
		buffer->len = copy_len;
	}

	buffer->skb = skb;
	buffer->flags = EF4_TX_BUF_SKB;

	++tx_queue->insert_count;
	return rc;
}

static struct ef4_tx_buffer *ef4_tx_map_chunk(struct ef4_tx_queue *tx_queue,
					      dma_addr_t dma_addr,
					      size_t len)
{
	const struct ef4_nic_type *nic_type = tx_queue->efx->type;
	struct ef4_tx_buffer *buffer;
	unsigned int dma_len;

	/* Map the fragment taking account of NIC-dependent DMA limits. */
	do {
		buffer = ef4_tx_queue_get_insert_buffer(tx_queue);
		dma_len = nic_type->tx_limit_len(tx_queue, dma_addr, len);

		buffer->len = dma_len;
		buffer->dma_addr = dma_addr;
		buffer->flags = EF4_TX_BUF_CONT;
		len -= dma_len;
		dma_addr += dma_len;
		++tx_queue->insert_count;
	} while (len);

	return buffer;
}

/* Map all data from an SKB for DMA and create descriptors on the queue.
 */
static int ef4_tx_map_data(struct ef4_tx_queue *tx_queue, struct sk_buff *skb)
{
	struct ef4_nic *efx = tx_queue->efx;
	struct device *dma_dev = &efx->pci_dev->dev;
	unsigned int frag_index, nr_frags;
	dma_addr_t dma_addr, unmap_addr;
	unsigned short dma_flags;
	size_t len, unmap_len;

	nr_frags = skb_shinfo(skb)->nr_frags;
	frag_index = 0;

	/* Map header data. */
	len = skb_headlen(skb);
	dma_addr = dma_map_single(dma_dev, skb->data, len, DMA_TO_DEVICE);
	dma_flags = EF4_TX_BUF_MAP_SINGLE;
	unmap_len = len;
	unmap_addr = dma_addr;

	if (unlikely(dma_mapping_error(dma_dev, dma_addr)))
		return -EIO;

	/* Add descriptors for each fragment. */
	do {
		struct ef4_tx_buffer *buffer;
		skb_frag_t *fragment;

		buffer = ef4_tx_map_chunk(tx_queue, dma_addr, len);

		/* The final descriptor for a fragment is responsible for
		 * unmapping the whole fragment.
		 */
		buffer->flags = EF4_TX_BUF_CONT | dma_flags;
		buffer->unmap_len = unmap_len;
		buffer->dma_offset = buffer->dma_addr - unmap_addr;

		if (frag_index >= nr_frags) {
			/* Store SKB details with the final buffer for
			 * the completion.
			 */
			buffer->skb = skb;
			buffer->flags = EF4_TX_BUF_SKB | dma_flags;
			return 0;
		}

		/* Move on to the next fragment. */
		fragment = &skb_shinfo(skb)->frags[frag_index++];
		len = skb_frag_size(fragment);
		dma_addr = skb_frag_dma_map(dma_dev, fragment,
				0, len, DMA_TO_DEVICE);
		dma_flags = 0;
		unmap_len = len;
		unmap_addr = dma_addr;

		if (unlikely(dma_mapping_error(dma_dev, dma_addr)))
			return -EIO;
	} while (1);
}

/* Remove buffers put into a tx_queue.  None of the buffers must have
 * an skb attached.
 */
static void ef4_enqueue_unwind(struct ef4_tx_queue *tx_queue)
{
	struct ef4_tx_buffer *buffer;

	/* Work backwards until we hit the original insert pointer value */
	while (tx_queue->insert_count != tx_queue->write_count) {
		--tx_queue->insert_count;
		buffer = __ef4_tx_queue_get_insert_buffer(tx_queue);
		ef4_dequeue_buffer(tx_queue, buffer, NULL, NULL);
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
 * This function is split out from ef4_hard_start_xmit to allow the
 * loopback test to direct packets via specific TX queues.
 *
 * Returns NETDEV_TX_OK.
 * You must hold netif_tx_lock() to call this function.
 */
netdev_tx_t ef4_enqueue_skb(struct ef4_tx_queue *tx_queue, struct sk_buff *skb)
{
	bool data_mapped = false;
	unsigned int skb_len;

	skb_len = skb->len;
	EF4_WARN_ON_PARANOID(skb_is_gso(skb));

	if (skb_len < tx_queue->tx_min_size ||
			(skb->data_len && skb_len <= EF4_TX_CB_SIZE)) {
		/* Pad short packets or coalesce short fragmented packets. */
		if (ef4_enqueue_skb_copy(tx_queue, skb))
			goto err;
		tx_queue->cb_packets++;
		data_mapped = true;
	}

	/* Map for DMA and create descriptors if we haven't done so already. */
	if (!data_mapped && (ef4_tx_map_data(tx_queue, skb)))
		goto err;

	/* Update BQL */
	netdev_tx_sent_queue(tx_queue->core_txq, skb_len);

	/* Pass off to hardware */
	if (!netdev_xmit_more() || netif_xmit_stopped(tx_queue->core_txq)) {
		struct ef4_tx_queue *txq2 = ef4_tx_queue_partner(tx_queue);

		/* There could be packets left on the partner queue if those
		 * SKBs had skb->xmit_more set. If we do not push those they
		 * could be left for a long time and cause a netdev watchdog.
		 */
		if (txq2->xmit_more_available)
			ef4_nic_push_buffers(txq2);

		ef4_nic_push_buffers(tx_queue);
	} else {
		tx_queue->xmit_more_available = netdev_xmit_more();
	}

	tx_queue->tx_packets++;

	ef4_tx_maybe_stop_queue(tx_queue);

	return NETDEV_TX_OK;


err:
	ef4_enqueue_unwind(tx_queue);
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/* Remove packets from the TX queue
 *
 * This removes packets from the TX queue, up to and including the
 * specified index.
 */
static void ef4_dequeue_buffers(struct ef4_tx_queue *tx_queue,
				unsigned int index,
				unsigned int *pkts_compl,
				unsigned int *bytes_compl)
{
	struct ef4_nic *efx = tx_queue->efx;
	unsigned int stop_index, read_ptr;

	stop_index = (index + 1) & tx_queue->ptr_mask;
	read_ptr = tx_queue->read_count & tx_queue->ptr_mask;

	while (read_ptr != stop_index) {
		struct ef4_tx_buffer *buffer = &tx_queue->buffer[read_ptr];

		if (!(buffer->flags & EF4_TX_BUF_OPTION) &&
		    unlikely(buffer->len == 0)) {
			netif_err(efx, tx_err, efx->net_dev,
				  "TX queue %d spurious TX completion id %x\n",
				  tx_queue->queue, read_ptr);
			ef4_schedule_reset(efx, RESET_TYPE_TX_SKIP);
			return;
		}

		ef4_dequeue_buffer(tx_queue, buffer, pkts_compl, bytes_compl);

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
netdev_tx_t ef4_hard_start_xmit(struct sk_buff *skb,
				struct net_device *net_dev)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	struct ef4_tx_queue *tx_queue;
	unsigned index, type;

	EF4_WARN_ON_PARANOID(!netif_device_present(net_dev));

	index = skb_get_queue_mapping(skb);
	type = skb->ip_summed == CHECKSUM_PARTIAL ? EF4_TXQ_TYPE_OFFLOAD : 0;
	if (index >= efx->n_tx_channels) {
		index -= efx->n_tx_channels;
		type |= EF4_TXQ_TYPE_HIGHPRI;
	}
	tx_queue = ef4_get_tx_queue(efx, index, type);

	return ef4_enqueue_skb(tx_queue, skb);
}

void ef4_init_tx_queue_core_txq(struct ef4_tx_queue *tx_queue)
{
	struct ef4_nic *efx = tx_queue->efx;

	/* Must be inverse of queue lookup in ef4_hard_start_xmit() */
	tx_queue->core_txq =
		netdev_get_tx_queue(efx->net_dev,
				    tx_queue->queue / EF4_TXQ_TYPES +
				    ((tx_queue->queue & EF4_TXQ_TYPE_HIGHPRI) ?
				     efx->n_tx_channels : 0));
}

int ef4_setup_tc(struct net_device *net_dev, enum tc_setup_type type,
		 void *type_data)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	struct tc_mqprio_qopt *mqprio = type_data;
	struct ef4_channel *channel;
	struct ef4_tx_queue *tx_queue;
	unsigned tc, num_tc;
	int rc;

	if (type != TC_SETUP_QDISC_MQPRIO)
		return -EOPNOTSUPP;

	num_tc = mqprio->num_tc;

	if (ef4_nic_rev(efx) < EF4_REV_FALCON_B0 || num_tc > EF4_MAX_TX_TC)
		return -EINVAL;

	mqprio->hw = TC_MQPRIO_HW_OFFLOAD_TCS;

	if (num_tc == net_dev->num_tc)
		return 0;

	for (tc = 0; tc < num_tc; tc++) {
		net_dev->tc_to_txq[tc].offset = tc * efx->n_tx_channels;
		net_dev->tc_to_txq[tc].count = efx->n_tx_channels;
	}

	if (num_tc > net_dev->num_tc) {
		/* Initialise high-priority queues as necessary */
		ef4_for_each_channel(channel, efx) {
			ef4_for_each_possible_channel_tx_queue(tx_queue,
							       channel) {
				if (!(tx_queue->queue & EF4_TXQ_TYPE_HIGHPRI))
					continue;
				if (!tx_queue->buffer) {
					rc = ef4_probe_tx_queue(tx_queue);
					if (rc)
						return rc;
				}
				if (!tx_queue->initialised)
					ef4_init_tx_queue(tx_queue);
				ef4_init_tx_queue_core_txq(tx_queue);
			}
		}
	} else {
		/* Reduce number of classes before number of queues */
		net_dev->num_tc = num_tc;
	}

	rc = netif_set_real_num_tx_queues(net_dev,
					  max_t(int, num_tc, 1) *
					  efx->n_tx_channels);
	if (rc)
		return rc;

	/* Do not destroy high-priority queues when they become
	 * unused.  We would have to flush them first, and it is
	 * fairly difficult to flush a subset of TX queues.  Leave
	 * it to ef4_fini_channels().
	 */

	net_dev->num_tc = num_tc;
	return 0;
}

void ef4_xmit_done(struct ef4_tx_queue *tx_queue, unsigned int index)
{
	unsigned fill_level;
	struct ef4_nic *efx = tx_queue->efx;
	struct ef4_tx_queue *txq2;
	unsigned int pkts_compl = 0, bytes_compl = 0;

	EF4_BUG_ON_PARANOID(index > tx_queue->ptr_mask);

	ef4_dequeue_buffers(tx_queue, index, &pkts_compl, &bytes_compl);
	tx_queue->pkts_compl += pkts_compl;
	tx_queue->bytes_compl += bytes_compl;

	if (pkts_compl > 1)
		++tx_queue->merge_events;

	/* See if we need to restart the netif queue.  This memory
	 * barrier ensures that we write read_count (inside
	 * ef4_dequeue_buffers()) before reading the queue status.
	 */
	smp_mb();
	if (unlikely(netif_tx_queue_stopped(tx_queue->core_txq)) &&
	    likely(efx->port_enabled) &&
	    likely(netif_device_present(efx->net_dev))) {
		txq2 = ef4_tx_queue_partner(tx_queue);
		fill_level = max(tx_queue->insert_count - tx_queue->read_count,
				 txq2->insert_count - txq2->read_count);
		if (fill_level <= efx->txq_wake_thresh)
			netif_tx_wake_queue(tx_queue->core_txq);
	}

	/* Check whether the hardware queue is now empty */
	if ((int)(tx_queue->read_count - tx_queue->old_write_count) >= 0) {
		tx_queue->old_write_count = READ_ONCE(tx_queue->write_count);
		if (tx_queue->read_count == tx_queue->old_write_count) {
			smp_mb();
			tx_queue->empty_read_count =
				tx_queue->read_count | EF4_EMPTY_COUNT_VALID;
		}
	}
}

static unsigned int ef4_tx_cb_page_count(struct ef4_tx_queue *tx_queue)
{
	return DIV_ROUND_UP(tx_queue->ptr_mask + 1, PAGE_SIZE >> EF4_TX_CB_ORDER);
}

int ef4_probe_tx_queue(struct ef4_tx_queue *tx_queue)
{
	struct ef4_nic *efx = tx_queue->efx;
	unsigned int entries;
	int rc;

	/* Create the smallest power-of-two aligned ring */
	entries = max(roundup_pow_of_two(efx->txq_entries), EF4_MIN_DMAQ_SIZE);
	EF4_BUG_ON_PARANOID(entries > EF4_MAX_DMAQ_SIZE);
	tx_queue->ptr_mask = entries - 1;

	netif_dbg(efx, probe, efx->net_dev,
		  "creating TX queue %d size %#x mask %#x\n",
		  tx_queue->queue, efx->txq_entries, tx_queue->ptr_mask);

	/* Allocate software ring */
	tx_queue->buffer = kcalloc(entries, sizeof(*tx_queue->buffer),
				   GFP_KERNEL);
	if (!tx_queue->buffer)
		return -ENOMEM;

	tx_queue->cb_page = kcalloc(ef4_tx_cb_page_count(tx_queue),
				    sizeof(tx_queue->cb_page[0]), GFP_KERNEL);
	if (!tx_queue->cb_page) {
		rc = -ENOMEM;
		goto fail1;
	}

	/* Allocate hardware ring */
	rc = ef4_nic_probe_tx(tx_queue);
	if (rc)
		goto fail2;

	return 0;

fail2:
	kfree(tx_queue->cb_page);
	tx_queue->cb_page = NULL;
fail1:
	kfree(tx_queue->buffer);
	tx_queue->buffer = NULL;
	return rc;
}

void ef4_init_tx_queue(struct ef4_tx_queue *tx_queue)
{
	struct ef4_nic *efx = tx_queue->efx;

	netif_dbg(efx, drv, efx->net_dev,
		  "initialising TX queue %d\n", tx_queue->queue);

	tx_queue->insert_count = 0;
	tx_queue->write_count = 0;
	tx_queue->old_write_count = 0;
	tx_queue->read_count = 0;
	tx_queue->old_read_count = 0;
	tx_queue->empty_read_count = 0 | EF4_EMPTY_COUNT_VALID;
	tx_queue->xmit_more_available = false;

	/* Some older hardware requires Tx writes larger than 32. */
	tx_queue->tx_min_size = EF4_WORKAROUND_15592(efx) ? 33 : 0;

	/* Set up TX descriptor ring */
	ef4_nic_init_tx(tx_queue);

	tx_queue->initialised = true;
}

void ef4_fini_tx_queue(struct ef4_tx_queue *tx_queue)
{
	struct ef4_tx_buffer *buffer;

	netif_dbg(tx_queue->efx, drv, tx_queue->efx->net_dev,
		  "shutting down TX queue %d\n", tx_queue->queue);

	if (!tx_queue->buffer)
		return;

	/* Free any buffers left in the ring */
	while (tx_queue->read_count != tx_queue->write_count) {
		unsigned int pkts_compl = 0, bytes_compl = 0;
		buffer = &tx_queue->buffer[tx_queue->read_count & tx_queue->ptr_mask];
		ef4_dequeue_buffer(tx_queue, buffer, &pkts_compl, &bytes_compl);

		++tx_queue->read_count;
	}
	tx_queue->xmit_more_available = false;
	netdev_tx_reset_queue(tx_queue->core_txq);
}

void ef4_remove_tx_queue(struct ef4_tx_queue *tx_queue)
{
	int i;

	if (!tx_queue->buffer)
		return;

	netif_dbg(tx_queue->efx, drv, tx_queue->efx->net_dev,
		  "destroying TX queue %d\n", tx_queue->queue);
	ef4_nic_remove_tx(tx_queue);

	if (tx_queue->cb_page) {
		for (i = 0; i < ef4_tx_cb_page_count(tx_queue); i++)
			ef4_nic_free_buffer(tx_queue->efx,
					    &tx_queue->cb_page[i]);
		kfree(tx_queue->cb_page);
		tx_queue->cb_page = NULL;
	}

	kfree(tx_queue->buffer);
	tx_queue->buffer = NULL;
}
