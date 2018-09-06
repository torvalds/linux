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

/* Take mac80211 Q id from the skb and translate it to hardware Q id */
static u8 skb2q(struct sk_buff *skb)
{
	int qid = skb_get_queue_mapping(skb);

	if (WARN_ON(qid >= MT_TXQ_PSD)) {
		qid = MT_TXQ_BE;
		skb_set_queue_mapping(skb, qid);
	}

	return mt76_ac_to_hwq(qid);
}

void mt76x0_tx_status(struct mt76x0_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	mt76x02_remove_dma_hdr(skb);

	ieee80211_tx_info_clear_status(info);
	info->status.rates[0].idx = -1;
	info->flags |= IEEE80211_TX_STAT_ACK;

	spin_lock(&dev->mac_lock);
	ieee80211_tx_status(dev->mt76.hw, skb);
	spin_unlock(&dev->mac_lock);
}

static struct mt76x02_txwi *
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

void mt76x0_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mt76x0_dev *dev = hw->priv;
	struct ieee80211_vif *vif = info->control.vif;
	struct ieee80211_sta *sta = control->sta;
	struct mt76x02_sta *msta = NULL;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	struct mt76x02_txwi *txwi;
	int pkt_len = skb->len;
	int hw_q = skb2q(skb);

	BUILD_BUG_ON(ARRAY_SIZE(info->status.status_driver_data) < 1);
	info->status.status_driver_data[0] = (void *)(unsigned long)pkt_len;

	mt76x02_insert_hdr_pad(skb);

	if (sta) {
		msta = (struct mt76x02_sta *) sta->drv_priv;
		wcid = &msta->wcid;
	} else if (vif && (!info->control.hw_key && wcid->hw_key_idx != 0xff)) {
		struct mt76x02_vif *mvif = (struct mt76x02_vif *)vif->drv_priv;

		wcid = &mvif->group_wcid;
	}

	txwi = mt76x0_push_txwi(dev, skb, sta, wcid, pkt_len);

	if (mt76x0_dma_enqueue_tx(dev, skb, wcid, hw_q))
		return;

	trace_mt76x0_tx(&dev->mt76, skb, msta, txwi);
}

void mt76x0_tx_stat(struct work_struct *work)
{
	struct mt76x0_dev *dev = container_of(work, struct mt76x0_dev,
					       stat_work.work);
	struct mt76x02_tx_status stat;
	unsigned long flags;
	int cleaned = 0;
	u8 update = 1;

	while (!test_bit(MT76_REMOVED, &dev->mt76.state)) {
		if (!mt76x02_mac_load_tx_status(&dev->mt76, &stat))
			break;

		mt76x02_send_tx_status(&dev->mt76, &stat, &update);

		cleaned++;
	}
	trace_mt76x0_tx_status_cleaned(&dev->mt76, cleaned);

	spin_lock_irqsave(&dev->tx_lock, flags);
	if (cleaned)
		queue_delayed_work(dev->stat_wq, &dev->stat_work,
				   msecs_to_jiffies(10));
	else if (test_and_clear_bit(MT76_MORE_STATS, &dev->mt76.state))
		queue_delayed_work(dev->stat_wq, &dev->stat_work,
				   msecs_to_jiffies(20));
	else
		clear_bit(MT76_READING_STATS, &dev->mt76.state);
	spin_unlock_irqrestore(&dev->tx_lock, flags);
}
