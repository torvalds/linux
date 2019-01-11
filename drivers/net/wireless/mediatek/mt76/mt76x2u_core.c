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
#include "dma.h"

static void mt76x2u_remove_dma_hdr(struct sk_buff *skb)
{
	int hdr_len;

	skb_pull(skb, sizeof(struct mt76x2_txwi) + MT_DMA_HDR_LEN);
	hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	if (hdr_len % 4) {
		memmove(skb->data + 2, skb->data, hdr_len);
		skb_pull(skb, 2);
	}
}

static int
mt76x2u_check_skb_rooms(struct sk_buff *skb)
{
	int hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	u32 need_head;

	need_head = sizeof(struct mt76x2_txwi) + MT_DMA_HDR_LEN;
	if (hdr_len % 4)
		need_head += 2;
	return skb_cow(skb, need_head);
}

static int
mt76x2u_set_txinfo(struct sk_buff *skb,
		   struct mt76_wcid *wcid, u8 ep)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	enum mt76x2_qsel qsel;
	u32 flags;

	if ((info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE) ||
	    ep == MT_EP_OUT_HCCA)
		qsel = MT_QSEL_MGMT;
	else
		qsel = MT_QSEL_EDCA;

	flags = FIELD_PREP(MT_TXD_INFO_QSEL, qsel) |
		MT_TXD_INFO_80211;
	if (!wcid || wcid->hw_key_idx == 0xff || wcid->sw_iv)
		flags |= MT_TXD_INFO_WIV;

	return mt76u_skb_dma_info(skb, WLAN_PORT, flags);
}

bool mt76x2u_tx_status_data(struct mt76_dev *mdev, u8 *update)
{
	struct mt76x2_dev *dev = container_of(mdev, struct mt76x2_dev, mt76);
	struct mt76x2_tx_status stat;

	if (!mt76x2_mac_load_tx_status(dev, &stat))
		return false;

	mt76x2_send_tx_status(dev, &stat, update);

	return true;
}

int mt76x2u_tx_prepare_skb(struct mt76_dev *mdev, void *data,
			   struct sk_buff *skb, struct mt76_queue *q,
			   struct mt76_wcid *wcid, struct ieee80211_sta *sta,
			   u32 *tx_info)
{
	struct mt76x2_dev *dev = container_of(mdev, struct mt76x2_dev, mt76);
	struct mt76x2_txwi *txwi;
	int err, len = skb->len;

	err = mt76x2u_check_skb_rooms(skb);
	if (err < 0)
		return -ENOMEM;

	mt76x2_insert_hdr_pad(skb);

	txwi = skb_push(skb, sizeof(struct mt76x2_txwi));
	mt76x2_mac_write_txwi(dev, txwi, skb, wcid, sta, len);

	return mt76x2u_set_txinfo(skb, wcid, q2ep(q->hw_idx));
}

void mt76x2u_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue *q,
			     struct mt76_queue_entry *e, bool flush)
{
	struct mt76x2_dev *dev = container_of(mdev, struct mt76x2_dev, mt76);

	mt76x2u_remove_dma_hdr(e->skb);
	mt76x2_tx_complete(dev, e->skb);
}

