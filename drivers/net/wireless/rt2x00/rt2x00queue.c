/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00lib
	Abstract: rt2x00 queue specific routines.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

void rt2x00queue_create_tx_descriptor(struct queue_entry *entry,
				      struct txentry_desc *txdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(entry->skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)entry->skb->data;
	struct ieee80211_rate *rate =
	    ieee80211_get_tx_rate(rt2x00dev->hw, tx_info);
	const struct rt2x00_rate *hwrate;
	unsigned int data_length;
	unsigned int duration;
	unsigned int residual;
	u16 frame_control;

	memset(txdesc, 0, sizeof(*txdesc));

	/*
	 * Initialize information from queue
	 */
	txdesc->queue = entry->queue->qid;
	txdesc->cw_min = entry->queue->cw_min;
	txdesc->cw_max = entry->queue->cw_max;
	txdesc->aifs = entry->queue->aifs;

	/* Data length should be extended with 4 bytes for CRC */
	data_length = entry->skb->len + 4;

	/*
	 * Read required fields from ieee80211 header.
	 */
	frame_control = le16_to_cpu(hdr->frame_control);

	/*
	 * Check whether this frame is to be acked.
	 */
	if (!(tx_info->flags & IEEE80211_TX_CTL_NO_ACK))
		__set_bit(ENTRY_TXD_ACK, &txdesc->flags);

	/*
	 * Check if this is a RTS/CTS frame
	 */
	if (is_rts_frame(frame_control) || is_cts_frame(frame_control)) {
		__set_bit(ENTRY_TXD_BURST, &txdesc->flags);
		if (is_rts_frame(frame_control))
			__set_bit(ENTRY_TXD_RTS_FRAME, &txdesc->flags);
		else
			__set_bit(ENTRY_TXD_CTS_FRAME, &txdesc->flags);
		if (tx_info->control.rts_cts_rate_idx >= 0)
			rate =
			    ieee80211_get_rts_cts_rate(rt2x00dev->hw, tx_info);
	}

	/*
	 * Determine retry information.
	 */
	txdesc->retry_limit = tx_info->control.retry_limit;
	if (tx_info->flags & IEEE80211_TX_CTL_LONG_RETRY_LIMIT)
		__set_bit(ENTRY_TXD_RETRY_MODE, &txdesc->flags);

	/*
	 * Check if more fragments are pending
	 */
	if (ieee80211_get_morefrag(hdr)) {
		__set_bit(ENTRY_TXD_BURST, &txdesc->flags);
		__set_bit(ENTRY_TXD_MORE_FRAG, &txdesc->flags);
	}

	/*
	 * Beacons and probe responses require the tsf timestamp
	 * to be inserted into the frame.
	 */
	if (txdesc->queue == QID_BEACON || is_probe_resp(frame_control))
		__set_bit(ENTRY_TXD_REQ_TIMESTAMP, &txdesc->flags);

	/*
	 * Determine with what IFS priority this frame should be send.
	 * Set ifs to IFS_SIFS when the this is not the first fragment,
	 * or this fragment came after RTS/CTS.
	 */
	if (test_bit(ENTRY_TXD_RTS_FRAME, &txdesc->flags)) {
		txdesc->ifs = IFS_SIFS;
	} else if (tx_info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT) {
		__set_bit(ENTRY_TXD_FIRST_FRAGMENT, &txdesc->flags);
		txdesc->ifs = IFS_BACKOFF;
	} else {
		txdesc->ifs = IFS_SIFS;
	}

	/*
	 * PLCP setup
	 * Length calculation depends on OFDM/CCK rate.
	 */
	hwrate = rt2x00_get_rate(rate->hw_value);
	txdesc->signal = hwrate->plcp;
	txdesc->service = 0x04;

	if (hwrate->flags & DEV_RATE_OFDM) {
		__set_bit(ENTRY_TXD_OFDM_RATE, &txdesc->flags);

		txdesc->length_high = (data_length >> 6) & 0x3f;
		txdesc->length_low = data_length & 0x3f;
	} else {
		/*
		 * Convert length to microseconds.
		 */
		residual = get_duration_res(data_length, hwrate->bitrate);
		duration = get_duration(data_length, hwrate->bitrate);

		if (residual != 0) {
			duration++;

			/*
			 * Check if we need to set the Length Extension
			 */
			if (hwrate->bitrate == 110 && residual <= 30)
				txdesc->service |= 0x80;
		}

		txdesc->length_high = (duration >> 8) & 0xff;
		txdesc->length_low = duration & 0xff;

		/*
		 * When preamble is enabled we should set the
		 * preamble bit for the signal.
		 */
		if (rt2x00_get_rate_preamble(rate->hw_value))
			txdesc->signal |= 0x08;
	}
}
EXPORT_SYMBOL_GPL(rt2x00queue_create_tx_descriptor);

void rt2x00queue_write_tx_descriptor(struct queue_entry *entry,
				     struct txentry_desc *txdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);

	rt2x00dev->ops->lib->write_tx_desc(rt2x00dev, entry->skb, txdesc);

	/*
	 * All processing on the frame has been completed, this means
	 * it is now ready to be dumped to userspace through debugfs.
	 */
	rt2x00debug_dump_frame(rt2x00dev, DUMP_FRAME_TX, entry->skb);

	/*
	 * We are done writing the frame to the queue entry,
	 * also kick the queue in case the correct flags are set,
	 * note that this will automatically filter beacons and
	 * RTS/CTS frames since those frames don't have this flag
	 * set.
	 */
	if (rt2x00dev->ops->lib->kick_tx_queue &&
	    !(skbdesc->flags & FRAME_DESC_DRIVER_GENERATED))
		rt2x00dev->ops->lib->kick_tx_queue(rt2x00dev,
						   entry->queue->qid);
}
EXPORT_SYMBOL_GPL(rt2x00queue_write_tx_descriptor);

struct data_queue *rt2x00queue_get_queue(struct rt2x00_dev *rt2x00dev,
					 const enum data_queue_qid queue)
{
	int atim = test_bit(DRIVER_REQUIRE_ATIM_QUEUE, &rt2x00dev->flags);

	if (queue < rt2x00dev->ops->tx_queues && rt2x00dev->tx)
		return &rt2x00dev->tx[queue];

	if (!rt2x00dev->bcn)
		return NULL;

	if (queue == QID_BEACON)
		return &rt2x00dev->bcn[0];
	else if (queue == QID_ATIM && atim)
		return &rt2x00dev->bcn[1];

	return NULL;
}
EXPORT_SYMBOL_GPL(rt2x00queue_get_queue);

struct queue_entry *rt2x00queue_get_entry(struct data_queue *queue,
					  enum queue_index index)
{
	struct queue_entry *entry;
	unsigned long irqflags;

	if (unlikely(index >= Q_INDEX_MAX)) {
		ERROR(queue->rt2x00dev,
		      "Entry requested from invalid index type (%d)\n", index);
		return NULL;
	}

	spin_lock_irqsave(&queue->lock, irqflags);

	entry = &queue->entries[queue->index[index]];

	spin_unlock_irqrestore(&queue->lock, irqflags);

	return entry;
}
EXPORT_SYMBOL_GPL(rt2x00queue_get_entry);

void rt2x00queue_index_inc(struct data_queue *queue, enum queue_index index)
{
	unsigned long irqflags;

	if (unlikely(index >= Q_INDEX_MAX)) {
		ERROR(queue->rt2x00dev,
		      "Index change on invalid index type (%d)\n", index);
		return;
	}

	spin_lock_irqsave(&queue->lock, irqflags);

	queue->index[index]++;
	if (queue->index[index] >= queue->limit)
		queue->index[index] = 0;

	if (index == Q_INDEX) {
		queue->length++;
	} else if (index == Q_INDEX_DONE) {
		queue->length--;
		queue->count ++;
	}

	spin_unlock_irqrestore(&queue->lock, irqflags);
}
EXPORT_SYMBOL_GPL(rt2x00queue_index_inc);

static void rt2x00queue_reset(struct data_queue *queue)
{
	unsigned long irqflags;

	spin_lock_irqsave(&queue->lock, irqflags);

	queue->count = 0;
	queue->length = 0;
	memset(queue->index, 0, sizeof(queue->index));

	spin_unlock_irqrestore(&queue->lock, irqflags);
}

void rt2x00queue_init_rx(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue = rt2x00dev->rx;
	unsigned int i;

	rt2x00queue_reset(queue);

	if (!rt2x00dev->ops->lib->init_rxentry)
		return;

	for (i = 0; i < queue->limit; i++)
		rt2x00dev->ops->lib->init_rxentry(rt2x00dev,
						  &queue->entries[i]);
}

void rt2x00queue_init_tx(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	unsigned int i;

	txall_queue_for_each(rt2x00dev, queue) {
		rt2x00queue_reset(queue);

		if (!rt2x00dev->ops->lib->init_txentry)
			continue;

		for (i = 0; i < queue->limit; i++)
			rt2x00dev->ops->lib->init_txentry(rt2x00dev,
							  &queue->entries[i]);
	}
}

static int rt2x00queue_alloc_entries(struct data_queue *queue,
				     const struct data_queue_desc *qdesc)
{
	struct queue_entry *entries;
	unsigned int entry_size;
	unsigned int i;

	rt2x00queue_reset(queue);

	queue->limit = qdesc->entry_num;
	queue->data_size = qdesc->data_size;
	queue->desc_size = qdesc->desc_size;

	/*
	 * Allocate all queue entries.
	 */
	entry_size = sizeof(*entries) + qdesc->priv_size;
	entries = kzalloc(queue->limit * entry_size, GFP_KERNEL);
	if (!entries)
		return -ENOMEM;

#define QUEUE_ENTRY_PRIV_OFFSET(__base, __index, __limit, __esize, __psize) \
	( ((char *)(__base)) + ((__limit) * (__esize)) + \
	    ((__index) * (__psize)) )

	for (i = 0; i < queue->limit; i++) {
		entries[i].flags = 0;
		entries[i].queue = queue;
		entries[i].skb = NULL;
		entries[i].entry_idx = i;
		entries[i].priv_data =
		    QUEUE_ENTRY_PRIV_OFFSET(entries, i, queue->limit,
					    sizeof(*entries), qdesc->priv_size);
	}

#undef QUEUE_ENTRY_PRIV_OFFSET

	queue->entries = entries;

	return 0;
}

int rt2x00queue_initialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	int status;


	status = rt2x00queue_alloc_entries(rt2x00dev->rx, rt2x00dev->ops->rx);
	if (status)
		goto exit;

	tx_queue_for_each(rt2x00dev, queue) {
		status = rt2x00queue_alloc_entries(queue, rt2x00dev->ops->tx);
		if (status)
			goto exit;
	}

	status = rt2x00queue_alloc_entries(rt2x00dev->bcn, rt2x00dev->ops->bcn);
	if (status)
		goto exit;

	if (!test_bit(DRIVER_REQUIRE_ATIM_QUEUE, &rt2x00dev->flags))
		return 0;

	status = rt2x00queue_alloc_entries(&rt2x00dev->bcn[1],
					   rt2x00dev->ops->atim);
	if (status)
		goto exit;

	return 0;

exit:
	ERROR(rt2x00dev, "Queue entries allocation failed.\n");

	rt2x00queue_uninitialize(rt2x00dev);

	return status;
}

void rt2x00queue_uninitialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;

	queue_for_each(rt2x00dev, queue) {
		kfree(queue->entries);
		queue->entries = NULL;
	}
}

static void rt2x00queue_init(struct rt2x00_dev *rt2x00dev,
			     struct data_queue *queue, enum data_queue_qid qid)
{
	spin_lock_init(&queue->lock);

	queue->rt2x00dev = rt2x00dev;
	queue->qid = qid;
	queue->aifs = 2;
	queue->cw_min = 5;
	queue->cw_max = 10;
}

int rt2x00queue_allocate(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	enum data_queue_qid qid;
	unsigned int req_atim =
	    !!test_bit(DRIVER_REQUIRE_ATIM_QUEUE, &rt2x00dev->flags);

	/*
	 * We need the following queues:
	 * RX: 1
	 * TX: ops->tx_queues
	 * Beacon: 1
	 * Atim: 1 (if required)
	 */
	rt2x00dev->data_queues = 2 + rt2x00dev->ops->tx_queues + req_atim;

	queue = kzalloc(rt2x00dev->data_queues * sizeof(*queue), GFP_KERNEL);
	if (!queue) {
		ERROR(rt2x00dev, "Queue allocation failed.\n");
		return -ENOMEM;
	}

	/*
	 * Initialize pointers
	 */
	rt2x00dev->rx = queue;
	rt2x00dev->tx = &queue[1];
	rt2x00dev->bcn = &queue[1 + rt2x00dev->ops->tx_queues];

	/*
	 * Initialize queue parameters.
	 * RX: qid = QID_RX
	 * TX: qid = QID_AC_BE + index
	 * TX: cw_min: 2^5 = 32.
	 * TX: cw_max: 2^10 = 1024.
	 * BCN & Atim: qid = QID_MGMT
	 */
	rt2x00queue_init(rt2x00dev, rt2x00dev->rx, QID_RX);

	qid = QID_AC_BE;
	tx_queue_for_each(rt2x00dev, queue)
		rt2x00queue_init(rt2x00dev, queue, qid++);

	rt2x00queue_init(rt2x00dev, &rt2x00dev->bcn[0], QID_MGMT);
	if (req_atim)
		rt2x00queue_init(rt2x00dev, &rt2x00dev->bcn[1], QID_MGMT);

	return 0;
}

void rt2x00queue_free(struct rt2x00_dev *rt2x00dev)
{
	kfree(rt2x00dev->rx);
	rt2x00dev->rx = NULL;
	rt2x00dev->tx = NULL;
	rt2x00dev->bcn = NULL;
}
