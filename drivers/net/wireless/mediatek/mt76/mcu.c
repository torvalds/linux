// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2019 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#include "mt76.h"

struct sk_buff *
mt76_mcu_msg_alloc(struct mt76_dev *dev, const void *data,
		   int data_len)
{
	const struct mt76_mcu_ops *ops = dev->mcu_ops;
	int length = ops->headroom + data_len + ops->tailroom;
	struct sk_buff *skb;

	skb = alloc_skb(length, GFP_KERNEL);
	if (!skb)
		return NULL;

	memset(skb->head, 0, length);
	skb_reserve(skb, ops->headroom);

	if (data && data_len)
		skb_put_data(skb, data, data_len);

	return skb;
}
EXPORT_SYMBOL_GPL(mt76_mcu_msg_alloc);

struct sk_buff *mt76_mcu_get_response(struct mt76_dev *dev,
				      unsigned long expires)
{
	unsigned long timeout;

	if (!time_is_after_jiffies(expires))
		return NULL;

	timeout = expires - jiffies;
	wait_event_timeout(dev->mcu.wait,
			   (!skb_queue_empty(&dev->mcu.res_q) ||
			    test_bit(MT76_MCU_RESET, &dev->phy.state)),
			   timeout);
	return skb_dequeue(&dev->mcu.res_q);
}
EXPORT_SYMBOL_GPL(mt76_mcu_get_response);

void mt76_mcu_rx_event(struct mt76_dev *dev, struct sk_buff *skb)
{
	skb_queue_tail(&dev->mcu.res_q, skb);
	wake_up(&dev->mcu.wait);
}
EXPORT_SYMBOL_GPL(mt76_mcu_rx_event);
