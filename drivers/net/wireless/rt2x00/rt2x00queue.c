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
#include <linux/dma-mapping.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

struct sk_buff *rt2x00queue_alloc_rxskb(struct rt2x00_dev *rt2x00dev,
					struct queue_entry *entry)
{
	struct sk_buff *skb;
	struct skb_frame_desc *skbdesc;
	unsigned int frame_size;
	unsigned int head_size = 0;
	unsigned int tail_size = 0;

	/*
	 * The frame size includes descriptor size, because the
	 * hardware directly receive the frame into the skbuffer.
	 */
	frame_size = entry->queue->data_size + entry->queue->desc_size;

	/*
	 * The payload should be aligned to a 4-byte boundary,
	 * this means we need at least 3 bytes for moving the frame
	 * into the correct offset.
	 */
	head_size = 4;

	/*
	 * For IV/EIV/ICV assembly we must make sure there is
	 * at least 8 bytes bytes available in headroom for IV/EIV
	 * and 8 bytes for ICV data as tailroon.
	 */
	if (test_bit(CONFIG_SUPPORT_HW_CRYPTO, &rt2x00dev->flags)) {
		head_size += 8;
		tail_size += 8;
	}

	/*
	 * Allocate skbuffer.
	 */
	skb = dev_alloc_skb(frame_size + head_size + tail_size);
	if (!skb)
		return NULL;

	/*
	 * Make sure we not have a frame with the requested bytes
	 * available in the head and tail.
	 */
	skb_reserve(skb, head_size);
	skb_put(skb, frame_size);

	/*
	 * Populate skbdesc.
	 */
	skbdesc = get_skb_frame_desc(skb);
	memset(skbdesc, 0, sizeof(*skbdesc));
	skbdesc->entry = entry;

	if (test_bit(DRIVER_REQUIRE_DMA, &rt2x00dev->flags)) {
		skbdesc->skb_dma = dma_map_single(rt2x00dev->dev,
						  skb->data,
						  skb->len,
						  DMA_FROM_DEVICE);
		skbdesc->flags |= SKBDESC_DMA_MAPPED_RX;
	}

	return skb;
}

void rt2x00queue_map_txskb(struct rt2x00_dev *rt2x00dev, struct sk_buff *skb)
{
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(skb);

	/*
	 * If device has requested headroom, we should make sure that
	 * is also mapped to the DMA so it can be used for transfering
	 * additional descriptor information to the hardware.
	 */
	skb_push(skb, rt2x00dev->hw->extra_tx_headroom);

	skbdesc->skb_dma =
	    dma_map_single(rt2x00dev->dev, skb->data, skb->len, DMA_TO_DEVICE);

	/*
	 * Restore data pointer to original location again.
	 */
	skb_pull(skb, rt2x00dev->hw->extra_tx_headroom);

	skbdesc->flags |= SKBDESC_DMA_MAPPED_TX;
}
EXPORT_SYMBOL_GPL(rt2x00queue_map_txskb);

void rt2x00queue_unmap_skb(struct rt2x00_dev *rt2x00dev, struct sk_buff *skb)
{
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(skb);

	if (skbdesc->flags & SKBDESC_DMA_MAPPED_RX) {
		dma_unmap_single(rt2x00dev->dev, skbdesc->skb_dma, skb->len,
				 DMA_FROM_DEVICE);
		skbdesc->flags &= ~SKBDESC_DMA_MAPPED_RX;
	}

	if (skbdesc->flags & SKBDESC_DMA_MAPPED_TX) {
		/*
		 * Add headroom to the skb length, it has been removed
		 * by the driver, but it was actually mapped to DMA.
		 */
		dma_unmap_single(rt2x00dev->dev, skbdesc->skb_dma,
				 skb->len + rt2x00dev->hw->extra_tx_headroom,
				 DMA_TO_DEVICE);
		skbdesc->flags &= ~SKBDESC_DMA_MAPPED_TX;
	}
}

void rt2x00queue_free_skb(struct rt2x00_dev *rt2x00dev, struct sk_buff *skb)
{
	if (!skb)
		return;

	rt2x00queue_unmap_skb(rt2x00dev, skb);
	dev_kfree_skb_any(skb);
}

static void rt2x00queue_create_tx_descriptor(struct queue_entry *entry,
					     struct txentry_desc *txdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(entry->skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)entry->skb->data;
	struct ieee80211_tx_rate *txrate = &tx_info->control.rates[0];
	struct ieee80211_rate *rate =
	    ieee80211_get_tx_rate(rt2x00dev->hw, tx_info);
	const struct rt2x00_rate *hwrate;
	unsigned int data_length;
	unsigned int duration;
	unsigned int residual;
	unsigned long irqflags;

	memset(txdesc, 0, sizeof(*txdesc));

	/*
	 * Initialize information from queue
	 */
	txdesc->queue = entry->queue->qid;
	txdesc->cw_min = entry->queue->cw_min;
	txdesc->cw_max = entry->queue->cw_max;
	txdesc->aifs = entry->queue->aifs;

	/* Data length + CRC */
	data_length = entry->skb->len + 4;

	/*
	 * Check whether this frame is to be acked.
	 */
	if (!(tx_info->flags & IEEE80211_TX_CTL_NO_ACK))
		__set_bit(ENTRY_TXD_ACK, &txdesc->flags);

	if (test_bit(CONFIG_SUPPORT_HW_CRYPTO, &rt2x00dev->flags) &&
	    !entry->skb->do_not_encrypt) {
		/* Apply crypto specific descriptor information */
		rt2x00crypto_create_tx_descriptor(entry, txdesc);

		/*
		 * Extend frame length to include all encryption overhead
		 * that will be added by the hardware.
		 */
		data_length += rt2x00crypto_tx_overhead(tx_info);
	}

	/*
	 * Check if this is a RTS/CTS frame
	 */
	if (ieee80211_is_rts(hdr->frame_control) ||
	    ieee80211_is_cts(hdr->frame_control)) {
		__set_bit(ENTRY_TXD_BURST, &txdesc->flags);
		if (ieee80211_is_rts(hdr->frame_control))
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
	txdesc->retry_limit = tx_info->control.rates[0].count - 1;
	if (txdesc->retry_limit >= rt2x00dev->long_retry)
		__set_bit(ENTRY_TXD_RETRY_MODE, &txdesc->flags);

	/*
	 * Check if more fragments are pending
	 */
	if (ieee80211_has_morefrags(hdr->frame_control)) {
		__set_bit(ENTRY_TXD_BURST, &txdesc->flags);
		__set_bit(ENTRY_TXD_MORE_FRAG, &txdesc->flags);
	}

	/*
	 * Beacons and probe responses require the tsf timestamp
	 * to be inserted into the frame.
	 */
	if (ieee80211_is_beacon(hdr->frame_control) ||
	    ieee80211_is_probe_resp(hdr->frame_control))
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
	 * Hardware should insert sequence counter.
	 * FIXME: We insert a software sequence counter first for
	 * hardware that doesn't support hardware sequence counting.
	 *
	 * This is wrong because beacons are not getting sequence
	 * numbers assigned properly.
	 *
	 * A secondary problem exists for drivers that cannot toggle
	 * sequence counting per-frame, since those will override the
	 * sequence counter given by mac80211.
	 */
	if (tx_info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		if (likely(tx_info->control.vif)) {
			struct rt2x00_intf *intf;

			intf = vif_to_intf(tx_info->control.vif);

			spin_lock_irqsave(&intf->seqlock, irqflags);

			if (test_bit(ENTRY_TXD_FIRST_FRAGMENT, &txdesc->flags))
				intf->seqno += 0x10;
			hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
			hdr->seq_ctrl |= cpu_to_le16(intf->seqno);

			spin_unlock_irqrestore(&intf->seqlock, irqflags);

			__set_bit(ENTRY_TXD_GENERATE_SEQ, &txdesc->flags);
		}
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
		residual = GET_DURATION_RES(data_length, hwrate->bitrate);
		duration = GET_DURATION(data_length, hwrate->bitrate);

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
		if (txrate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			txdesc->signal |= 0x08;
	}
}

static void rt2x00queue_write_tx_descriptor(struct queue_entry *entry,
					    struct txentry_desc *txdesc)
{
	struct data_queue *queue = entry->queue;
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;

	rt2x00dev->ops->lib->write_tx_desc(rt2x00dev, entry->skb, txdesc);

	/*
	 * All processing on the frame has been completed, this means
	 * it is now ready to be dumped to userspace through debugfs.
	 */
	rt2x00debug_dump_frame(rt2x00dev, DUMP_FRAME_TX, entry->skb);

	/*
	 * Check if we need to kick the queue, there are however a few rules
	 *	1) Don't kick beacon queue
	 *	2) Don't kick unless this is the last in frame in a burst.
	 *	   When the burst flag is set, this frame is always followed
	 *	   by another frame which in some way are related to eachother.
	 *	   This is true for fragments, RTS or CTS-to-self frames.
	 *	3) Rule 2 can be broken when the available entries
	 *	   in the queue are less then a certain threshold.
	 */
	if (entry->queue->qid == QID_BEACON)
		return;

	if (rt2x00queue_threshold(queue) ||
	    !test_bit(ENTRY_TXD_BURST, &txdesc->flags))
		rt2x00dev->ops->lib->kick_tx_queue(rt2x00dev, queue->qid);
}

int rt2x00queue_write_tx_frame(struct data_queue *queue, struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info;
	struct queue_entry *entry = rt2x00queue_get_entry(queue, Q_INDEX);
	struct txentry_desc txdesc;
	struct skb_frame_desc *skbdesc;
	unsigned int iv_len = 0;
	u8 rate_idx, rate_flags;

	if (unlikely(rt2x00queue_full(queue)))
		return -ENOBUFS;

	if (test_and_set_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags)) {
		ERROR(queue->rt2x00dev,
		      "Arrived at non-free entry in the non-full queue %d.\n"
		      "Please file bug report to %s.\n",
		      queue->qid, DRV_PROJECT);
		return -EINVAL;
	}

	/*
	 * Copy all TX descriptor information into txdesc,
	 * after that we are free to use the skb->cb array
	 * for our information.
	 */
	entry->skb = skb;
	rt2x00queue_create_tx_descriptor(entry, &txdesc);

	if (IEEE80211_SKB_CB(skb)->control.hw_key != NULL)
		iv_len = IEEE80211_SKB_CB(skb)->control.hw_key->iv_len;

	/*
	 * All information is retrieved from the skb->cb array,
	 * now we should claim ownership of the driver part of that
	 * array, preserving the bitrate index and flags.
	 */
	tx_info = IEEE80211_SKB_CB(skb);
	rate_idx = tx_info->control.rates[0].idx;
	rate_flags = tx_info->control.rates[0].flags;
	skbdesc = get_skb_frame_desc(skb);
	memset(skbdesc, 0, sizeof(*skbdesc));
	skbdesc->entry = entry;
	skbdesc->tx_rate_idx = rate_idx;
	skbdesc->tx_rate_flags = rate_flags;

	/*
	 * When hardware encryption is supported, and this frame
	 * is to be encrypted, we should strip the IV/EIV data from
	 * the frame so we can provide it to the driver seperately.
	 */
	if (test_bit(ENTRY_TXD_ENCRYPT, &txdesc.flags) &&
	    !test_bit(ENTRY_TXD_ENCRYPT_IV, &txdesc.flags)) {
		if (test_bit(CONFIG_CRYPTO_COPY_IV, &queue->rt2x00dev->flags))
			rt2x00crypto_tx_copy_iv(skb, iv_len);
		else
			rt2x00crypto_tx_remove_iv(skb, iv_len);
	}

	/*
	 * It could be possible that the queue was corrupted and this
	 * call failed. Since we always return NETDEV_TX_OK to mac80211,
	 * this frame will simply be dropped.
	 */
	if (unlikely(queue->rt2x00dev->ops->lib->write_tx_data(entry))) {
		clear_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags);
		entry->skb = NULL;
		return -EIO;
	}

	if (test_bit(DRIVER_REQUIRE_DMA, &queue->rt2x00dev->flags))
		rt2x00queue_map_txskb(queue->rt2x00dev, skb);

	set_bit(ENTRY_DATA_PENDING, &entry->flags);

	rt2x00queue_index_inc(queue, Q_INDEX);
	rt2x00queue_write_tx_descriptor(entry, &txdesc);

	return 0;
}

int rt2x00queue_update_beacon(struct rt2x00_dev *rt2x00dev,
			      struct ieee80211_vif *vif)
{
	struct rt2x00_intf *intf = vif_to_intf(vif);
	struct skb_frame_desc *skbdesc;
	struct txentry_desc txdesc;
	__le32 desc[16];

	if (unlikely(!intf->beacon))
		return -ENOBUFS;

	intf->beacon->skb = ieee80211_beacon_get(rt2x00dev->hw, vif);
	if (!intf->beacon->skb)
		return -ENOMEM;

	/*
	 * Copy all TX descriptor information into txdesc,
	 * after that we are free to use the skb->cb array
	 * for our information.
	 */
	rt2x00queue_create_tx_descriptor(intf->beacon, &txdesc);

	/*
	 * For the descriptor we use a local array from where the
	 * driver can move it to the correct location required for
	 * the hardware.
	 */
	memset(desc, 0, sizeof(desc));

	/*
	 * Fill in skb descriptor
	 */
	skbdesc = get_skb_frame_desc(intf->beacon->skb);
	memset(skbdesc, 0, sizeof(*skbdesc));
	skbdesc->desc = desc;
	skbdesc->desc_len = intf->beacon->queue->desc_size;
	skbdesc->entry = intf->beacon;

	/*
	 * Write TX descriptor into reserved room in front of the beacon.
	 */
	rt2x00queue_write_tx_descriptor(intf->beacon, &txdesc);

	/*
	 * Send beacon to hardware.
	 * Also enable beacon generation, which might have been disabled
	 * by the driver during the config_beacon() callback function.
	 */
	rt2x00dev->ops->lib->write_beacon(intf->beacon);
	rt2x00dev->ops->lib->kick_tx_queue(rt2x00dev, QID_BEACON);

	return 0;
}

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
		queue->count++;
	}

	spin_unlock_irqrestore(&queue->lock, irqflags);
}

static void rt2x00queue_reset(struct data_queue *queue)
{
	unsigned long irqflags;

	spin_lock_irqsave(&queue->lock, irqflags);

	queue->count = 0;
	queue->length = 0;
	memset(queue->index, 0, sizeof(queue->index));

	spin_unlock_irqrestore(&queue->lock, irqflags);
}

void rt2x00queue_init_queues(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	unsigned int i;

	queue_for_each(rt2x00dev, queue) {
		rt2x00queue_reset(queue);

		for (i = 0; i < queue->limit; i++) {
			queue->entries[i].flags = 0;

			rt2x00dev->ops->lib->clear_entry(&queue->entries[i]);
		}
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
	queue->threshold = DIV_ROUND_UP(qdesc->entry_num, 10);
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

static void rt2x00queue_free_skbs(struct rt2x00_dev *rt2x00dev,
				  struct data_queue *queue)
{
	unsigned int i;

	if (!queue->entries)
		return;

	for (i = 0; i < queue->limit; i++) {
		if (queue->entries[i].skb)
			rt2x00queue_free_skb(rt2x00dev, queue->entries[i].skb);
	}
}

static int rt2x00queue_alloc_rxskbs(struct rt2x00_dev *rt2x00dev,
				    struct data_queue *queue)
{
	unsigned int i;
	struct sk_buff *skb;

	for (i = 0; i < queue->limit; i++) {
		skb = rt2x00queue_alloc_rxskb(rt2x00dev, &queue->entries[i]);
		if (!skb)
			return -ENOMEM;
		queue->entries[i].skb = skb;
	}

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

	if (test_bit(DRIVER_REQUIRE_ATIM_QUEUE, &rt2x00dev->flags)) {
		status = rt2x00queue_alloc_entries(&rt2x00dev->bcn[1],
						   rt2x00dev->ops->atim);
		if (status)
			goto exit;
	}

	status = rt2x00queue_alloc_rxskbs(rt2x00dev, rt2x00dev->rx);
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

	rt2x00queue_free_skbs(rt2x00dev, rt2x00dev->rx);

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
	queue->txop = 0;
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
	 * BCN: qid = QID_BEACON
	 * ATIM: qid = QID_ATIM
	 */
	rt2x00queue_init(rt2x00dev, rt2x00dev->rx, QID_RX);

	qid = QID_AC_BE;
	tx_queue_for_each(rt2x00dev, queue)
		rt2x00queue_init(rt2x00dev, queue, qid++);

	rt2x00queue_init(rt2x00dev, &rt2x00dev->bcn[0], QID_BEACON);
	if (req_atim)
		rt2x00queue_init(rt2x00dev, &rt2x00dev->bcn[1], QID_ATIM);

	return 0;
}

void rt2x00queue_free(struct rt2x00_dev *rt2x00dev)
{
	kfree(rt2x00dev->rx);
	rt2x00dev->rx = NULL;
	rt2x00dev->tx = NULL;
	rt2x00dev->bcn = NULL;
}
