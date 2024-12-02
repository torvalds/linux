// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 */

#include "mt7601u.h"
#include "trace.h"

enum mt76_txq_id {
	MT_TXQ_VO = IEEE80211_AC_VO,
	MT_TXQ_VI = IEEE80211_AC_VI,
	MT_TXQ_BE = IEEE80211_AC_BE,
	MT_TXQ_BK = IEEE80211_AC_BK,
	MT_TXQ_PSD,
	MT_TXQ_MCU,
	__MT_TXQ_MAX
};

/* Hardware uses mirrored order of queues with Q0 having the highest priority */
static u8 q2hwq(u8 q)
{
	return q ^ 0x3;
}

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

/* Note: TX retry reporting is a bit broken.
 *	 Retries are reported only once per AMPDU and often come a frame early
 *	 i.e. they are reported in the last status preceding the AMPDU. Apart
 *	 from the fact that it's hard to know the length of the AMPDU (which is
 *	 required to know to how many consecutive frames retries should be
 *	 applied), if status comes early on full FIFO it gets lost and retries
 *	 of the whole AMPDU become invisible.
 *	 As a work-around encode the desired rate in PKT_ID of TX descriptor
 *	 and based on that guess the retries (every rate is tried once).
 *	 Only downside here is that for MCS0 we have to rely solely on
 *	 transmission failures as no retries can ever be reported.
 *	 Not having to read EXT_FIFO has a nice effect of doubling the number
 *	 of reports which can be fetched.
 *	 Also the vendor driver never uses the EXT_FIFO register so it may be
 *	 undertested.
 */
static u8 mt7601u_tx_pktid_enc(struct mt7601u_dev *dev, u8 rate, bool is_probe)
{
	u8 encoded = (rate + 1) + is_probe *  8;

	/* Because PKT_ID 0 disables status reporting only 15 values are
	 * available but 16 are needed (8 MCS * 2 for encoding is_probe)
	 * - we need to cram together two rates. MCS0 and MCS7 with is_probe
	 * share PKT_ID 9.
	 */
	if (is_probe && rate == 7)
		return encoded - 7;

	return encoded;
}

static void
mt7601u_tx_pktid_dec(struct mt7601u_dev *dev, struct mt76_tx_status *stat)
{
	u8 req_rate = stat->pktid;
	u8 eff_rate = stat->rate & 0x7;

	req_rate -= 1;

	if (req_rate > 7) {
		stat->is_probe = true;
		req_rate -= 8;

		/* Decide between MCS0 and MCS7 which share pktid 9 */
		if (!req_rate && eff_rate)
			req_rate = 7;
	}

	stat->retry = req_rate - eff_rate;
}

static void mt7601u_tx_skb_remove_dma_overhead(struct sk_buff *skb,
					       struct ieee80211_tx_info *info)
{
	int pkt_len = (unsigned long)info->status.status_driver_data[0];

	skb_pull(skb, sizeof(struct mt76_txwi) + 4);
	if (ieee80211_get_hdrlen_from_skb(skb) % 4)
		mt76_remove_hdr_pad(skb);

	skb_trim(skb, pkt_len);
}

void mt7601u_tx_status(struct mt7601u_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	mt7601u_tx_skb_remove_dma_overhead(skb, info);

	ieee80211_tx_info_clear_status(info);
	info->status.rates[0].idx = -1;
	info->flags |= IEEE80211_TX_STAT_ACK;

	spin_lock_bh(&dev->mac_lock);
	ieee80211_tx_status(dev->hw, skb);
	spin_unlock_bh(&dev->mac_lock);
}

static int mt7601u_skb_rooms(struct mt7601u_dev *dev, struct sk_buff *skb)
{
	int hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	u32 need_head;

	need_head = sizeof(struct mt76_txwi) + 4;
	if (hdr_len % 4)
		need_head += 2;

	return skb_cow(skb, need_head);
}

static struct mt76_txwi *
mt7601u_push_txwi(struct mt7601u_dev *dev, struct sk_buff *skb,
		  struct ieee80211_sta *sta, struct mt76_wcid *wcid,
		  int pkt_len)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	struct mt76_txwi *txwi;
	unsigned long flags;
	bool is_probe;
	u32 pkt_id;
	u16 rate_ctl;
	u8 nss;

	txwi = skb_push(skb, sizeof(struct mt76_txwi));
	memset(txwi, 0, sizeof(*txwi));

	if (!wcid->tx_rate_set)
		ieee80211_get_tx_rates(info->control.vif, sta, skb,
				       info->control.rates, 1);

	spin_lock_irqsave(&dev->lock, flags);
	if (rate->idx < 0 || !rate->count)
		rate_ctl = wcid->tx_rate;
	else
		rate_ctl = mt76_mac_tx_rate_val(dev, rate, &nss);
	spin_unlock_irqrestore(&dev->lock, flags);
	txwi->rate_ctl = cpu_to_le16(rate_ctl);

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
		txwi->ack_ctl |= MT_TXWI_ACK_CTL_REQ;
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ)
		txwi->ack_ctl |= MT_TXWI_ACK_CTL_NSEQ;

	if ((info->flags & IEEE80211_TX_CTL_AMPDU) && sta) {
		u8 ba_size = IEEE80211_MIN_AMPDU_BUF;

		ba_size <<= sta->deflink.ht_cap.ampdu_factor;
		ba_size = min_t(int, 63, ba_size);
		if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
			ba_size = 0;
		txwi->ack_ctl |= FIELD_PREP(MT_TXWI_ACK_CTL_BA_WINDOW, ba_size);

		txwi->flags =
			cpu_to_le16(MT_TXWI_FLAGS_AMPDU |
				    FIELD_PREP(MT_TXWI_FLAGS_MPDU_DENSITY,
					       sta->deflink.ht_cap.ampdu_density));
		if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
			txwi->flags = 0;
	}

	txwi->wcid = wcid->idx;

	is_probe = !!(info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE);
	pkt_id = mt7601u_tx_pktid_enc(dev, rate_ctl & 0x7, is_probe);
	pkt_len |= FIELD_PREP(MT_TXWI_LEN_PKTID, pkt_id);
	txwi->len_ctl = cpu_to_le16(pkt_len);

	return txwi;
}

void mt7601u_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mt7601u_dev *dev = hw->priv;
	struct ieee80211_vif *vif = info->control.vif;
	struct ieee80211_sta *sta = control->sta;
	struct mt76_sta *msta = NULL;
	struct mt76_wcid *wcid = dev->mon_wcid;
	struct mt76_txwi *txwi;
	int pkt_len = skb->len;
	int hw_q = skb2q(skb);

	BUILD_BUG_ON(ARRAY_SIZE(info->status.status_driver_data) < 1);
	info->status.status_driver_data[0] = (void *)(unsigned long)pkt_len;

	if (mt7601u_skb_rooms(dev, skb) || mt76_insert_hdr_pad(skb)) {
		ieee80211_free_txskb(dev->hw, skb);
		return;
	}

	if (sta) {
		msta = (struct mt76_sta *) sta->drv_priv;
		wcid = &msta->wcid;
	} else if (vif) {
		struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;

		wcid = &mvif->group_wcid;
	}

	txwi = mt7601u_push_txwi(dev, skb, sta, wcid, pkt_len);

	if (mt7601u_dma_enqueue_tx(dev, skb, wcid, hw_q))
		return;

	trace_mt_tx(dev, skb, msta, txwi);
}

void mt7601u_tx_stat(struct work_struct *work)
{
	struct mt7601u_dev *dev = container_of(work, struct mt7601u_dev,
					       stat_work.work);
	struct mt76_tx_status stat;
	unsigned long flags;
	int cleaned = 0;

	while (!test_bit(MT7601U_STATE_REMOVED, &dev->state)) {
		stat = mt7601u_mac_fetch_tx_status(dev);
		if (!stat.valid)
			break;

		mt7601u_tx_pktid_dec(dev, &stat);
		mt76_send_tx_status(dev, &stat);

		cleaned++;
	}
	trace_mt_tx_status_cleaned(dev, cleaned);

	spin_lock_irqsave(&dev->tx_lock, flags);
	if (cleaned)
		queue_delayed_work(dev->stat_wq, &dev->stat_work,
				   msecs_to_jiffies(10));
	else if (test_and_clear_bit(MT7601U_STATE_MORE_STATS, &dev->state))
		queue_delayed_work(dev->stat_wq, &dev->stat_work,
				   msecs_to_jiffies(20));
	else
		clear_bit(MT7601U_STATE_READING_STATS, &dev->state);
	spin_unlock_irqrestore(&dev->tx_lock, flags);
}

int mt7601u_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    unsigned int link_id, u16 queue,
		    const struct ieee80211_tx_queue_params *params)
{
	struct mt7601u_dev *dev = hw->priv;
	u8 cw_min = 5, cw_max = 10, hw_q = q2hwq(queue);
	u32 val;

	/* TODO: should we do funny things with the parameters?
	 *	 See what mt7601u_set_default_edca() used to do in init.c.
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
