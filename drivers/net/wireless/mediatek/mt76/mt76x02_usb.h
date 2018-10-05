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

#ifndef __MT76x02_USB_H
#define __MT76x02_USB_H

#include "mt76.h"

void mt76x02u_init_mcu(struct mt76_dev *dev);
void mt76x02u_mcu_fw_reset(struct mt76_dev *dev);
int mt76x02u_mcu_fw_send_data(struct mt76_dev *dev, const void *data,
			      int data_len, u32 max_payload, u32 offset);

int mt76x02u_skb_dma_info(struct sk_buff *skb, int port, u32 flags);
int mt76x02u_tx_prepare_skb(struct mt76_dev *dev, void *data,
			    struct sk_buff *skb, struct mt76_queue *q,
			    struct mt76_wcid *wcid, struct ieee80211_sta *sta,
			    u32 *tx_info);
void mt76x02u_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue *q,
			      struct mt76_queue_entry *e, bool flush);
#endif /* __MT76x02_USB_H */
