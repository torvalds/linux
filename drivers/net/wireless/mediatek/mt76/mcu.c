/*
 * Copyright (C) 2019 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "mt76.h"

struct sk_buff *
mt76_mcu_msg_alloc(const void *data, int head_len,
		   int data_len, int tail_len)
{
	struct sk_buff *skb;

	skb = alloc_skb(head_len + data_len + tail_len,
			GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_reserve(skb, head_len);
	if (data && data_len)
		skb_put_data(skb, data, data_len);

	return skb;
}
EXPORT_SYMBOL_GPL(mt76_mcu_msg_alloc);

/* mmio */
struct sk_buff *mt76_mcu_get_response(struct mt76_dev *dev,
				      unsigned long expires)
{
	unsigned long timeout;

	if (!time_is_after_jiffies(expires))
		return NULL;

	timeout = expires - jiffies;
	wait_event_timeout(dev->mmio.mcu.wait,
			   !skb_queue_empty(&dev->mmio.mcu.res_q),
			   timeout);
	return skb_dequeue(&dev->mmio.mcu.res_q);
}
EXPORT_SYMBOL_GPL(mt76_mcu_get_response);

void mt76_mcu_rx_event(struct mt76_dev *dev, struct sk_buff *skb)
{
	skb_queue_tail(&dev->mmio.mcu.res_q, skb);
	wake_up(&dev->mmio.mcu.wait);
}
EXPORT_SYMBOL_GPL(mt76_mcu_rx_event);
