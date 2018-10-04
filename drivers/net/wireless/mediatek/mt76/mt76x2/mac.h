/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
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

#ifndef __MT76x2_MAC_H
#define __MT76x2_MAC_H

#include "../mt76.h"
#include "../mt76x02_mac.h"

struct mt76x02_dev;
struct mt76x2_sta;
struct mt76x02_vif;

struct mt76x2_tx_info {
	unsigned long jiffies;
	u8 tries;

	u8 wcid;
	u8 pktid;
	u8 retry;
};

static inline struct mt76x2_tx_info *
mt76x2_skb_tx_info(struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	return (void *) info->status.status_driver_data;
}

int mt76x2_mac_start(struct mt76x02_dev *dev);
void mt76x2_mac_stop(struct mt76x02_dev *dev, bool force);
void mt76x2_mac_resume(struct mt76x02_dev *dev);
void mt76x2_mac_set_bssid(struct mt76x02_dev *dev, u8 idx, const u8 *addr);

int mt76x2_mac_process_rx(struct mt76x02_dev *dev, struct sk_buff *skb,
			  void *rxi);

int mt76x2_mac_set_beacon(struct mt76x02_dev *dev, u8 vif_idx,
			  struct sk_buff *skb);
void mt76x2_mac_set_beacon_enable(struct mt76x02_dev *dev, u8 vif_idx, bool val);

void mt76x2_mac_poll_tx_status(struct mt76x02_dev *dev, bool irq);
void mt76x2_mac_process_tx_status_fifo(struct mt76x02_dev *dev);

void mt76x2_mac_work(struct work_struct *work);

#endif
