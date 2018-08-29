/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
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

#ifndef __MT76X02_MAC_H
#define __MT76X02_MAC_H

#include <linux/average.h>

struct mt76x02_tx_status {
	u8 valid:1;
	u8 success:1;
	u8 aggr:1;
	u8 ack_req:1;
	u8 wcid;
	u8 pktid;
	u8 retry;
	u16 rate;
} __packed __aligned(2);

struct mt76x02_vif {
	u8 idx;

	struct mt76_wcid group_wcid;
};

DECLARE_EWMA(signal, 10, 8);

struct mt76x02_sta {
	struct mt76_wcid wcid; /* must be first */

	struct mt76x02_vif *vif;
	struct mt76x02_tx_status status;
	int n_frames;

	struct ewma_signal rssi;
	int inactive_count;
};

static inline bool mt76x02_wait_for_mac(struct mt76_dev *dev)
{
	const u32 MAC_CSR0 = 0x1000;
	int i;

	for (i = 0; i < 500; i++) {
		if (test_bit(MT76_REMOVED, &dev->state))
			return -EIO;

		switch (dev->bus->rr(dev, MAC_CSR0)) {
		case 0:
		case ~0:
			break;
		default:
			return true;
		}
		usleep_range(5000, 10000);
	}
	return false;
}

void mt76x02_txq_init(struct mt76_dev *dev, struct ieee80211_txq *txq);

enum mt76x02_cipher_type
mt76x02_mac_get_key_info(struct ieee80211_key_conf *key, u8 *key_data);

int mt76x02_mac_shared_key_setup(struct mt76_dev *dev, u8 vif_idx, u8 key_idx,
				struct ieee80211_key_conf *key);
int mt76x02_mac_wcid_set_key(struct mt76_dev *dev, u8 idx,
			    struct ieee80211_key_conf *key);
void mt76x02_mac_wcid_setup(struct mt76_dev *dev, u8 idx, u8 vif_idx, u8 *mac);
void mt76x02_mac_wcid_set_drop(struct mt76_dev *dev, u8 idx, bool drop);
#endif
