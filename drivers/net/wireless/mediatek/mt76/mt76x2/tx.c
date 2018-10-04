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
#include "../dma.h"

void mt76x2_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
	       struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mt76x02_dev *dev = hw->priv;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;

	if (control->sta) {
		struct mt76x02_sta *msta;

		msta = (struct mt76x02_sta *)control->sta->drv_priv;
		wcid = &msta->wcid;
		/* sw encrypted frames */
		if (!info->control.hw_key && wcid->hw_key_idx != 0xff)
			control->sta = NULL;
	}

	if (vif && !control->sta) {
		struct mt76x02_vif *mvif;

		mvif = (struct mt76x02_vif *)vif->drv_priv;
		wcid = &mvif->group_wcid;
	}

	mt76_tx(&dev->mt76, control->sta, wcid, skb);
}
EXPORT_SYMBOL_GPL(mt76x2_tx);

s8 mt76x2_tx_get_txpwr_adj(struct mt76_dev *mdev, s8 txpwr, s8 max_txpwr_adj)
{
	struct mt76x02_dev  *dev = container_of(mdev, struct mt76x02_dev, mt76);

	txpwr = min_t(s8, txpwr, dev->mt76.txpower_conf);
	txpwr -= (dev->target_power + dev->target_power_delta[0]);
	txpwr = min_t(s8, txpwr, max_txpwr_adj);

	if (!dev->enable_tpc)
		return 0;
	else if (txpwr >= 0)
		return min_t(s8, txpwr, 7);
	else
		return (txpwr < -16) ? 8 : (txpwr + 32) / 2;
}
EXPORT_SYMBOL_GPL(mt76x2_tx_get_txpwr_adj);

void mt76x2_tx_set_txpwr_auto(struct mt76x02_dev *dev, s8 txpwr)
{
	s8 txpwr_adj;

	txpwr_adj = mt76x2_tx_get_txpwr_adj(&dev->mt76, txpwr,
					    dev->mt76.rate_power.ofdm[4]);
	mt76_rmw_field(dev, MT_PROT_AUTO_TX_CFG,
		       MT_PROT_AUTO_TX_CFG_PROT_PADJ, txpwr_adj);
	mt76_rmw_field(dev, MT_PROT_AUTO_TX_CFG,
		       MT_PROT_AUTO_TX_CFG_AUTO_PADJ, txpwr_adj);
}
EXPORT_SYMBOL_GPL(mt76x2_tx_set_txpwr_auto);
