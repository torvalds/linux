/*
 * Copyright (c) 2014 David Jander, Protonic Holland
 * Copyright (C) 2014-2017 Pengutronix, Marc Kleine-Budde <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/can/dev.h>
#include <linux/can/rx-offload.h>

struct can_rx_offload_cb {
	u32 timestamp;
};

static inline struct can_rx_offload_cb *can_rx_offload_get_cb(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct can_rx_offload_cb) > sizeof(skb->cb));

	return (struct can_rx_offload_cb *)skb->cb;
}

static inline bool can_rx_offload_le(struct can_rx_offload *offload, unsigned int a, unsigned int b)
{
	if (offload->inc)
		return a <= b;
	else
		return a >= b;
}

static inline unsigned int can_rx_offload_inc(struct can_rx_offload *offload, unsigned int *val)
{
	if (offload->inc)
		return (*val)++;
	else
		return (*val)--;
}

static int can_rx_offload_napi_poll(struct napi_struct *napi, int quota)
{
	struct can_rx_offload *offload = container_of(napi, struct can_rx_offload, napi);
	struct net_device *dev = offload->dev;
	struct net_device_stats *stats = &dev->stats;
	struct sk_buff *skb;
	int work_done = 0;

	while ((work_done < quota) &&
	       (skb = skb_dequeue(&offload->skb_queue))) {
		struct can_frame *cf = (struct can_frame *)skb->data;

		work_done++;
		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_receive_skb(skb);
	}

	if (work_done < quota) {
		napi_complete_done(napi, work_done);

		/* Check if there was another interrupt */
		if (!skb_queue_empty(&offload->skb_queue))
			napi_reschedule(&offload->napi);
	}

	can_led_event(offload->dev, CAN_LED_EVENT_RX);

	return work_done;
}

static inline void __skb_queue_add_sort(struct sk_buff_head *head, struct sk_buff *new,
					int (*compare)(struct sk_buff *a, struct sk_buff *b))
{
	struct sk_buff *pos, *insert = (struct sk_buff *)head;

	skb_queue_reverse_walk(head, pos) {
		const struct can_rx_offload_cb *cb_pos, *cb_new;

		cb_pos = can_rx_offload_get_cb(pos);
		cb_new = can_rx_offload_get_cb(new);

		netdev_dbg(new->dev,
			   "%s: pos=0x%08x, new=0x%08x, diff=%10d, queue_len=%d\n",
			   __func__,
			   cb_pos->timestamp, cb_new->timestamp,
			   cb_new->timestamp - cb_pos->timestamp,
			   skb_queue_len(head));

		if (compare(pos, new) < 0)
			continue;
		insert = pos;
		break;
	}

	__skb_queue_after(head, insert, new);
}

static int can_rx_offload_compare(struct sk_buff *a, struct sk_buff *b)
{
	const struct can_rx_offload_cb *cb_a, *cb_b;

	cb_a = can_rx_offload_get_cb(a);
	cb_b = can_rx_offload_get_cb(b);

	/* Substract two u32 and return result as int, to keep
	 * difference steady around the u32 overflow.
	 */
	return cb_b->timestamp - cb_a->timestamp;
}

static struct sk_buff *can_rx_offload_offload_one(struct can_rx_offload *offload, unsigned int n)
{
	struct sk_buff *skb = NULL;
	struct can_rx_offload_cb *cb;
	struct can_frame *cf;
	int ret;

	/* If queue is full or skb not available, read to discard mailbox */
	if (likely(skb_queue_len(&offload->skb_queue) <
		   offload->skb_queue_len_max))
		skb = alloc_can_skb(offload->dev, &cf);

	if (!skb) {
		struct can_frame cf_overflow;
		u32 timestamp;

		ret = offload->mailbox_read(offload, &cf_overflow,
					    &timestamp, n);
		if (ret) {
			offload->dev->stats.rx_dropped++;
			offload->dev->stats.rx_fifo_errors++;
		}

		return NULL;
	}

	cb = can_rx_offload_get_cb(skb);
	ret = offload->mailbox_read(offload, cf, &cb->timestamp, n);
	if (!ret) {
		kfree_skb(skb);
		return NULL;
	}

	return skb;
}

int can_rx_offload_irq_offload_timestamp(struct can_rx_offload *offload, u64 pending)
{
	struct sk_buff_head skb_queue;
	unsigned int i;

	__skb_queue_head_init(&skb_queue);

	for (i = offload->mb_first;
	     can_rx_offload_le(offload, i, offload->mb_last);
	     can_rx_offload_inc(offload, &i)) {
		struct sk_buff *skb;

		if (!(pending & BIT_ULL(i)))
			continue;

		skb = can_rx_offload_offload_one(offload, i);
		if (!skb)
			break;

		__skb_queue_add_sort(&skb_queue, skb, can_rx_offload_compare);
	}

	if (!skb_queue_empty(&skb_queue)) {
		unsigned long flags;
		u32 queue_len;

		spin_lock_irqsave(&offload->skb_queue.lock, flags);
		skb_queue_splice_tail(&skb_queue, &offload->skb_queue);
		spin_unlock_irqrestore(&offload->skb_queue.lock, flags);

		if ((queue_len = skb_queue_len(&offload->skb_queue)) >
		    (offload->skb_queue_len_max / 8))
			netdev_dbg(offload->dev, "%s: queue_len=%d\n",
				   __func__, queue_len);

		can_rx_offload_schedule(offload);
	}

	return skb_queue_len(&skb_queue);
}
EXPORT_SYMBOL_GPL(can_rx_offload_irq_offload_timestamp);

int can_rx_offload_irq_offload_fifo(struct can_rx_offload *offload)
{
	struct sk_buff *skb;
	int received = 0;

	while ((skb = can_rx_offload_offload_one(offload, 0))) {
		skb_queue_tail(&offload->skb_queue, skb);
		received++;
	}

	if (received)
		can_rx_offload_schedule(offload);

	return received;
}
EXPORT_SYMBOL_GPL(can_rx_offload_irq_offload_fifo);

int can_rx_offload_queue_sorted(struct can_rx_offload *offload,
				struct sk_buff *skb, u32 timestamp)
{
	struct can_rx_offload_cb *cb;
	unsigned long flags;

	if (skb_queue_len(&offload->skb_queue) >
	    offload->skb_queue_len_max) {
		kfree_skb(skb);
		return -ENOBUFS;
	}

	cb = can_rx_offload_get_cb(skb);
	cb->timestamp = timestamp;

	spin_lock_irqsave(&offload->skb_queue.lock, flags);
	__skb_queue_add_sort(&offload->skb_queue, skb, can_rx_offload_compare);
	spin_unlock_irqrestore(&offload->skb_queue.lock, flags);

	can_rx_offload_schedule(offload);

	return 0;
}
EXPORT_SYMBOL_GPL(can_rx_offload_queue_sorted);

unsigned int can_rx_offload_get_echo_skb(struct can_rx_offload *offload,
					 unsigned int idx, u32 timestamp)
{
	struct net_device *dev = offload->dev;
	struct net_device_stats *stats = &dev->stats;
	struct sk_buff *skb;
	u8 len;
	int err;

	skb = __can_get_echo_skb(dev, idx, &len);
	if (!skb)
		return 0;

	err = can_rx_offload_queue_sorted(offload, skb, timestamp);
	if (err) {
		stats->rx_errors++;
		stats->tx_fifo_errors++;
	}

	return len;
}
EXPORT_SYMBOL_GPL(can_rx_offload_get_echo_skb);

int can_rx_offload_queue_tail(struct can_rx_offload *offload,
			      struct sk_buff *skb)
{
	if (skb_queue_len(&offload->skb_queue) >
	    offload->skb_queue_len_max) {
		kfree_skb(skb);
		return -ENOBUFS;
	}

	skb_queue_tail(&offload->skb_queue, skb);
	can_rx_offload_schedule(offload);

	return 0;
}
EXPORT_SYMBOL_GPL(can_rx_offload_queue_tail);

static int can_rx_offload_init_queue(struct net_device *dev, struct can_rx_offload *offload, unsigned int weight)
{
	offload->dev = dev;

	/* Limit queue len to 4x the weight (rounted to next power of two) */
	offload->skb_queue_len_max = 2 << fls(weight);
	offload->skb_queue_len_max *= 4;
	skb_queue_head_init(&offload->skb_queue);

	can_rx_offload_reset(offload);
	netif_napi_add(dev, &offload->napi, can_rx_offload_napi_poll, weight);

	dev_dbg(dev->dev.parent, "%s: skb_queue_len_max=%d\n",
		__func__, offload->skb_queue_len_max);

	return 0;
}

int can_rx_offload_add_timestamp(struct net_device *dev, struct can_rx_offload *offload)
{
	unsigned int weight;

	if (offload->mb_first > BITS_PER_LONG_LONG ||
	    offload->mb_last > BITS_PER_LONG_LONG || !offload->mailbox_read)
		return -EINVAL;

	if (offload->mb_first < offload->mb_last) {
		offload->inc = true;
		weight = offload->mb_last - offload->mb_first;
	} else {
		offload->inc = false;
		weight = offload->mb_first - offload->mb_last;
	}

	return can_rx_offload_init_queue(dev, offload, weight);
}
EXPORT_SYMBOL_GPL(can_rx_offload_add_timestamp);

int can_rx_offload_add_fifo(struct net_device *dev, struct can_rx_offload *offload, unsigned int weight)
{
	if (!offload->mailbox_read)
		return -EINVAL;

	return can_rx_offload_init_queue(dev, offload, weight);
}
EXPORT_SYMBOL_GPL(can_rx_offload_add_fifo);

void can_rx_offload_enable(struct can_rx_offload *offload)
{
	can_rx_offload_reset(offload);
	napi_enable(&offload->napi);
}
EXPORT_SYMBOL_GPL(can_rx_offload_enable);

void can_rx_offload_del(struct can_rx_offload *offload)
{
	netif_napi_del(&offload->napi);
	skb_queue_purge(&offload->skb_queue);
}
EXPORT_SYMBOL_GPL(can_rx_offload_del);

void can_rx_offload_reset(struct can_rx_offload *offload)
{
}
EXPORT_SYMBOL_GPL(can_rx_offload_reset);
