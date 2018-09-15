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

/* Take mac80211 Q id from the skb and translate it to hardware Q id */
static u8 skb2q(struct sk_buff *skb)
{
	int qid = skb_get_queue_mapping(skb);

	if (WARN_ON(qid >= MT_TXQ_PSD)) {
		qid = MT_TXQ_BE;
		skb_set_queue_mapping(skb, qid);
	}

	return q2hwq(qid);
}

static void mt76x0_tx_skb_remove_dma_overhead(struct sk_buff *skb,
					       struct ieee80211_tx_info *info)
{
	int pkt_len = (unsigned long)info->status.status_driver_data[0];

	skb_pull(skb, sizeof(struct mt76_txwi) + 4);
	if (ieee80211_get_hdrlen_from_skb(skb) % 4)
		mt76x0_remove_hdr_pad(skb);

	skb_trim(skb, pkt_len);
}

void mt76x0_tx_status(struct mt76x0_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	mt76x0_tx_skb_remove_dma_overhead(skb, info);

	ieee80211_tx_info_clear_status(info);
	info->status.rates[0].idx = -1;
	info->flags |= IEEE80211_TX_STAT_ACK;

	spin_lock(&dev->mac_lock);
	ieee80211_tx_status(dev->mt76.hw, skb);
	spin_unlock(&dev->mac_lock);
}

static int mt76x0_skb_rooms(struct mt76x0_dev *dev, struct sk_buff *skb)
{
	int hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	u32 need_head;

	need_head = sizeof(struct mt76_txwi) + 4;
	if (hdr_len % 4)
		need_head += 2;

	return skb_cow(skb, need_head);
}

static struct mt76_txwi *
mt76x0_push_txwi(struct mt76x0_dev *dev, struct sk_buff *skb,
		  struct ieee80211_sta *sta, struct mt76_wcid *wcid,
		  int pkt_len)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	struct mt76_txwi *txwi;
	unsigned long flags;
	u16 txwi_flags = 0;
	u32 pkt_id;
	u16 rate_ctl;
	u8 nss;

	txwi = (struct mt76_txwi *)skb_push(skb, sizeof(struct mt76_txwi));
	memset(txwi, 0, sizeof(*txwi));

	if (!wcid->tx_rate_set)
		ieee80211_get_tx_rates(info->control.vif, sta, skb,
				       info->control.rates, 1);

	spin_lock_irqsave(&dev->mt76.lock, flags);
	if (rate->idx < 0 || !rate->count) {
		rate_ctl = wcid->tx_rate;
		nss = wcid->tx_rate_nss;
	} else {
		rate_ctl = mt76x0_mac_tx_rate_val(dev, rate, &nss);
	}
	spin_unlock_irqrestore(&dev->mt76.lock, flags);

	txwi->rate_ctl = cpu_to_le16(rate_ctl);

	if (info->flags & IEEE80211_TX_CTL_LDPC)
		txwi->rate_ctl |= cpu_to_le16(MT_RXWI_RATE_LDPC);
	if ((info->flags & IEEE80211_TX_CTL_STBC) && nss == 1)
		txwi->rate_ctl |= cpu_to_le16(MT_RXWI_RATE_STBC);
	if (nss > 1 && sta && sta->smps_mode == IEEE80211_SMPS_DYNAMIC)
		txwi_flags |= MT_TXWI_FLAGS_MMPS;

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
		txwi->ack_ctl |= MT_TXWI_ACK_CTL_REQ;
		pkt_id = 1;
	} else {
		pkt_id = 0;
	}

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
		pkt_id |= MT_TXWI_PKTID_PROBE;

	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ)
		txwi->ack_ctl |= MT_TXWI_ACK_CTL_NSEQ;

	if ((info->flags & IEEE80211_TX_CTL_AMPDU) && sta) {
		u8 ba_size = IEEE80211_MIN_AMPDU_BUF;

		ba_size <<= sta->ht_cap.ampdu_factor;
		ba_size = min_t(int, 7, ba_size - 1);
		if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE) {
			ba_size = 0;
		} else {
			txwi_flags |= MT_TXWI_FLAGS_AMPDU;
			txwi_flags |= FIELD_PREP(MT_TXWI_FLAGS_MPDU_DENSITY,
						 sta->ht_cap.ampdu_density);
		}
		txwi->ack_ctl |= FIELD_PREP(MT_TXWI_ACK_CTL_BA_WINDOW, ba_size);
	}

	txwi->wcid = wcid->idx;
	txwi->flags |= cpu_to_le16(txwi_flags);
	txwi->len_ctl = cpu_to_le16(pkt_len);
	txwi->pktid = pkt_id;

	return txwi;
}

void mt76x0_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mt76x0_dev *dev = hw->priv;
	struct ieee80211_vif *vif = info->control.vif;
	struct ieee80211_sta *sta = control->sta;
	struct mt76_sta *msta = NULL;
	struct mt76_wcid *wcid = dev->mon_wcid;
	struct mt76_txwi *txwi;
	int pkt_len = skb->len;
	int hw_q = skb2q(skb);

	BUILD_BUG_ON(ARRAY_SIZE(info->status.status_driver_data) < 1);
	info->status.status_driver_data[0] = (void *)(unsigned long)pkt_len;

	if (mt76x0_skb_rooms(dev, skb) || mt76x0_insert_hdr_pad(skb)) {
		ieee80211_free_txskb(dev->mt76.hw, skb);
		return;
	}

	if (sta) {
		msta = (struct mt76_sta *) sta->drv_priv;
		wcid = &msta->wcid;
	} else if (vif && (!info->control.hw_key && wcid->hw_key_idx != -1)) {
		struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;

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
	struct mt76_tx_status stat;
	unsigned long flags;
	int cleaned = 0;
	u8 update = 1;

	while (!test_bit(MT76_REMOVED, &dev->mt76.state)) {
		stat = mt76x0_mac_fetch_tx_status(dev);
		if (!stat.valid)
			break;

		mt76x0_send_tx_status(dev, &stat, &update);

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

int mt76x0_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    u16 queue, const struct ieee80211_tx_queue_params *params)
{
	struct mt76x0_dev *dev = hw->priv;
	u8 cw_min = 5, cw_max = 10, hw_q = q2hwq(queue);
	u32 val;

	/* TODO: should we do funny things with the parameters?
	 *	 See what mt76x0_set_default_edca() used to do in init.c.
	 */

	if (params->cw_min)
		cw_min = fls(params->cw_min);
	if (params->cw_max)
		cw_max = fls(params->cw_max);

	WARN_ON(params->txop > 0xff);
	WARN_ON(params->aifs > 0xf);
	WARN_ON(cw_min > 0xf);
	WARN_ON(cw_max > 0xf);

	val = FIELD_PREP(MT_EDCA_CFG_AIFSN, params->aifs) |
	      FIELD_PREP(MT_EDCA_CFG_CWMIN, cw_min) |
	      FIELD_PREP(MT_EDCA_CFG_CWMAX, cw_max);
	/* TODO: based on user-controlled EnableTxBurst var vendor drv sets
	 *	 a really long txop on AC0 (see connect.c:2009) but only on
	 *	 connect? When not connected should be 0.
	 */
	if (!hw_q)
		val |= 0x60;
	else
		val |= FIELD_PREP(MT_EDCA_CFG_TXOP, params->txop);
	mt76_wr(dev, MT_EDCA_CFG_AC(hw_q), val);

	val = mt76_rr(dev, MT_WMM_TXOP(hw_q));
	val &= ~(MT_WMM_TXOP_MASK << MT_WMM_TXOP_SHIFT(hw_q));
	val |= params->txop << MT_WMM_TXOP_SHIFT(hw_q);
	mt76_wr(dev, MT_WMM_TXOP(hw_q), val);

	val = mt76_rr(dev, MT_WMM_AIFSN);
	val &= ~(MT_WMM_AIFSN_MASK << MT_WMM_AIFSN_SHIFT(hw_q));
	val |= params->aifs << MT_WMM_AIFSN_SHIFT(hw_q);
	mt76_wr(dev, MT_WMM_AIFSN, val);

	val = mt76_rr(dev, MT_WMM_CWMIN);
	val &= ~(MT_WMM_CWMIN_MASK << MT_WMM_CWMIN_SHIFT(hw_q));
	val |= cw_min << MT_WMM_CWMIN_SHIFT(hw_q);
	mt76_wr(dev, MT_WMM_CWMIN, val);

	val = mt76_rr(dev, MT_WMM_CWMAX);
	val &= ~(MT_WMM_CWMAX_MASK << MT_WMM_CWMAX_SHIFT(hw_q));
	val |= cw_max << MT_WMM_CWMAX_SHIFT(hw_q);
	mt76_wr(dev, MT_WMM_CWMAX, val);

	return 0;
}
