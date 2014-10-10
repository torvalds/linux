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

static struct sk_buff *can_rx_offload_offload_one(struct can_rx_offload *offload, unsigned int n)
{
	struct sk_buff *skb = NULL;
	struct can_frame *cf;
	int ret;

	/* If queue is full or skb not available, read to discard mailbox */
	if (likely(skb_queue_len(&offload->skb_queue) <=
		   offload->skb_queue_len_max))
		skb = alloc_can_skb(offload->dev, &cf);

	if (!skb) {
		struct can_frame cf_overflow;

		ret = offload->mailbox_read(offload, &cf_overflow, n);
		if (ret)
			offload->dev->stats.rx_dropped++;

		return NULL;
	}

	ret = offload->mailbox_read(offload, cf, n);
	if (!ret) {
		kfree_skb(skb);
		return NULL;
	}

	return skb;
}

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

int can_rx_offload_irq_queue_err_skb(struct can_rx_offload *offload, struct sk_buff *skb)
{
	if (skb_queue_len(&offload->skb_queue) >
	    offload->skb_queue_len_max)
		return -ENOMEM;

	skb_queue_tail(&offload->skb_queue, skb);
	can_rx_offload_schedule(offload);

	return 0;
}
EXPORT_SYMBOL_GPL(can_rx_offload_irq_queue_err_skb);

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
