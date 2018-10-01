/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
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

#include "mt76x2.h"
#include "../mt76x02_mac.h"

void mt76x2_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb)
{
	struct mt76x2_dev *dev = container_of(mdev, struct mt76x2_dev, mt76);
	void *rxwi = skb->data;

	if (q == MT_RXQ_MCU) {
		/* this is used just by mmio code */
		skb_queue_tail(&mdev->mmio.mcu.res_q, skb);
		wake_up(&mdev->mmio.mcu.wait);
		return;
	}

	skb_pull(skb, sizeof(struct mt76x02_rxwi));
	if (mt76x2_mac_process_rx(dev, skb, rxwi)) {
		dev_kfree_skb(skb);
		return;
	}

	mt76_rx(&dev->mt76, q, skb);
}
EXPORT_SYMBOL_GPL(mt76x2_queue_rx_skb);
