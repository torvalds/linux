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

#include <linux/kernel.h>

#include "mt76.h"
#include "mt76x02_phy.h"
#include "mt76x02_mac.h"

void mt76x02_phy_set_rxpath(struct mt76_dev *dev)
{
	u32 val;

	val = __mt76_rr(dev, MT_BBP(AGC, 0));
	val &= ~BIT(4);

	switch (dev->chainmask & 0xf) {
	case 2:
		val |= BIT(3);
		break;
	default:
		val &= ~BIT(3);
		break;
	}

	__mt76_wr(dev, MT_BBP(AGC, 0), val);
	mb();
	val = __mt76_rr(dev, MT_BBP(AGC, 0));
}
EXPORT_SYMBOL_GPL(mt76x02_phy_set_rxpath);

void mt76x02_phy_set_txdac(struct mt76_dev *dev)
{
	int txpath;

	txpath = (dev->chainmask >> 8) & 0xf;
	switch (txpath) {
	case 2:
		__mt76_set(dev, MT_BBP(TXBE, 5), 0x3);
		break;
	default:
		__mt76_clear(dev, MT_BBP(TXBE, 5), 0x3);
		break;
	}
}
EXPORT_SYMBOL_GPL(mt76x02_phy_set_txdac);

static u32
mt76x02_tx_power_mask(u8 v1, u8 v2, u8 v3, u8 v4)
{
	u32 val = 0;

	val |= (v1 & (BIT(6) - 1)) << 0;
	val |= (v2 & (BIT(6) - 1)) << 8;
	val |= (v3 & (BIT(6) - 1)) << 16;
	val |= (v4 & (BIT(6) - 1)) << 24;
	return val;
}

int mt76x02_get_max_rate_power(struct mt76_rate_power *r)
{
	s8 ret = 0;
	int i;

	for (i = 0; i < sizeof(r->all); i++)
		ret = max(ret, r->all[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76x02_get_max_rate_power);

void mt76x02_limit_rate_power(struct mt76_rate_power *r, int limit)
{
	int i;

	for (i = 0; i < sizeof(r->all); i++)
		if (r->all[i] > limit)
			r->all[i] = limit;
}
EXPORT_SYMBOL_GPL(mt76x02_limit_rate_power);

void mt76x02_add_rate_power_offset(struct mt76_rate_power *r, int offset)
{
	int i;

	for (i = 0; i < sizeof(r->all); i++)
		r->all[i] += offset;
}
EXPORT_SYMBOL_GPL(mt76x02_add_rate_power_offset);

void mt76x02_phy_set_txpower(struct mt76_dev *dev, int txp_0, int txp_1)
{
	struct mt76_rate_power *t = &dev->rate_power;

	__mt76_rmw_field(dev, MT_TX_ALC_CFG_0, MT_TX_ALC_CFG_0_CH_INIT_0,
			 txp_0);
	__mt76_rmw_field(dev, MT_TX_ALC_CFG_0, MT_TX_ALC_CFG_0_CH_INIT_1,
			 txp_1);

	__mt76_wr(dev, MT_TX_PWR_CFG_0,
		  mt76x02_tx_power_mask(t->cck[0], t->cck[2], t->ofdm[0],
					t->ofdm[2]));
	__mt76_wr(dev, MT_TX_PWR_CFG_1,
		  mt76x02_tx_power_mask(t->ofdm[4], t->ofdm[6], t->ht[0],
					t->ht[2]));
	__mt76_wr(dev, MT_TX_PWR_CFG_2,
		  mt76x02_tx_power_mask(t->ht[4], t->ht[6], t->ht[8],
					t->ht[10]));
	__mt76_wr(dev, MT_TX_PWR_CFG_3,
		  mt76x02_tx_power_mask(t->ht[12], t->ht[14], t->stbc[0],
					t->stbc[2]));
	__mt76_wr(dev, MT_TX_PWR_CFG_4,
		  mt76x02_tx_power_mask(t->stbc[4], t->stbc[6], 0, 0));
	__mt76_wr(dev, MT_TX_PWR_CFG_7,
		  mt76x02_tx_power_mask(t->ofdm[7], t->vht[8], t->ht[7],
					t->vht[9]));
	__mt76_wr(dev, MT_TX_PWR_CFG_8,
		  mt76x02_tx_power_mask(t->ht[14], 0, t->vht[8], t->vht[9]));
	__mt76_wr(dev, MT_TX_PWR_CFG_9,
		  mt76x02_tx_power_mask(t->ht[7], 0, t->stbc[8], t->stbc[9]));
}
EXPORT_SYMBOL_GPL(mt76x02_phy_set_txpower);

int mt76x02_phy_get_min_avg_rssi(struct mt76_dev *dev)
{
	struct mt76x02_sta *sta;
	struct mt76_wcid *wcid;
	int i, j, min_rssi = 0;
	s8 cur_rssi;

	local_bh_disable();
	rcu_read_lock();

	for (i = 0; i < ARRAY_SIZE(dev->wcid_mask); i++) {
		unsigned long mask = dev->wcid_mask[i];

		if (!mask)
			continue;

		for (j = i * BITS_PER_LONG; mask; j++, mask >>= 1) {
			if (!(mask & 1))
				continue;

			wcid = rcu_dereference(dev->wcid[j]);
			if (!wcid)
				continue;

			sta = container_of(wcid, struct mt76x02_sta, wcid);
			spin_lock(&dev->rx_lock);
			if (sta->inactive_count++ < 5)
				cur_rssi = ewma_signal_read(&sta->rssi);
			else
				cur_rssi = 0;
			spin_unlock(&dev->rx_lock);

			if (cur_rssi < min_rssi)
				min_rssi = cur_rssi;
		}
	}

	rcu_read_unlock();
	local_bh_enable();

	if (!min_rssi)
		return -75;

	return min_rssi;
}
EXPORT_SYMBOL_GPL(mt76x02_phy_get_min_avg_rssi);
