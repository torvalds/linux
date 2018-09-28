/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "mt76x0.h"
#include "trace.h"
#include "../mt76x02_util.h"
#include "../mt76x02_usb.h"

struct mt76x02_txwi *
mt76x0_push_txwi(struct mt76x0_dev *dev, struct sk_buff *skb,
		 struct ieee80211_sta *sta, struct mt76_wcid *wcid,
		 int pkt_len)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	struct mt76x02_txwi *txwi;
	unsigned long flags;
	u16 rate_ctl;
	u8 nss;

	txwi = (struct mt76x02_txwi *)skb_push(skb, sizeof(struct mt76x02_txwi));
	memset(txwi, 0, sizeof(*txwi));

	if (!wcid->tx_rate_set)
		ieee80211_get_tx_rates(info->control.vif, sta, skb,
				       info->control.rates, 1);

	spin_lock_irqsave(&dev->mt76.lock, flags);
	if (rate->idx < 0 || !rate->count) {
		rate_ctl = wcid->tx_rate;
		nss = wcid->tx_rate_nss;
	} else {
		rate_ctl = mt76x02_mac_tx_rate_val(&dev->mt76, rate, &nss);
	}
	spin_unlock_irqrestore(&dev->mt76.lock, flags);

	txwi->wcid = wcid->idx;
	txwi->rate = cpu_to_le16(rate_ctl);
	txwi->pktid = (!(info->flags & IEEE80211_TX_CTL_NO_ACK)) ? 1 : 0;

	mt76x02_mac_fill_txwi(txwi, skb, sta, pkt_len, nss);

	return txwi;
}
EXPORT_SYMBOL_GPL(mt76x0_push_txwi);

void mt76x0_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
	       struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mt76x0_dev *dev = hw->priv;
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
EXPORT_SYMBOL_GPL(mt76x0_tx);

void mt76x0_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb)
{
	struct mt76x0_dev *dev = container_of(mdev, struct mt76x0_dev, mt76);
	void *rxwi = skb->data;

	skb_pull(skb, sizeof(struct mt76x02_rxwi));
	if (!mt76x0_mac_process_rx(dev, skb, rxwi)) {
		dev_kfree_skb(skb);
		return;
	}

	mt76_rx(&dev->mt76, q, skb);
}
EXPORT_SYMBOL_GPL(mt76x0_queue_rx_skb);
