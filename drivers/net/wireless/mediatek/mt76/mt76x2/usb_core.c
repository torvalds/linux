/*
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

#include "mt76x2u.h"
#include "../dma.h"
#include "../mt76x02_util.h"
#include "../mt76x02_usb.h"

static int
mt76x2u_check_skb_rooms(struct sk_buff *skb)
{
	int hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	u32 need_head;

	need_head = sizeof(struct mt76x02_txwi) + MT_DMA_HDR_LEN;
	if (hdr_len % 4)
		need_head += 2;
	return skb_cow(skb, need_head);
}

int mt76x2u_tx_prepare_skb(struct mt76_dev *mdev, void *data,
			   struct sk_buff *skb, struct mt76_queue *q,
			   struct mt76_wcid *wcid, struct ieee80211_sta *sta,
			   u32 *tx_info)
{
	struct mt76x2_dev *dev = container_of(mdev, struct mt76x2_dev, mt76);
	struct mt76x02_txwi *txwi;
	int err, len = skb->len;

	err = mt76x2u_check_skb_rooms(skb);
	if (err < 0)
		return -ENOMEM;

	mt76x02_insert_hdr_pad(skb);

	txwi = skb_push(skb, sizeof(struct mt76x02_txwi));
	mt76x2_mac_write_txwi(dev, txwi, skb, wcid, sta, len);

	return mt76x02u_set_txinfo(skb, wcid, q2ep(q->hw_idx));
}
