// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#include "mt76.h"

static struct mt76_txwi_cache *
mt76_alloc_txwi(struct mt76_dev *dev)
{
	struct mt76_txwi_cache *t;
	dma_addr_t addr;
	u8 *txwi;
	int size;

	size = L1_CACHE_ALIGN(dev->drv->txwi_size + sizeof(*t));
	txwi = devm_kzalloc(dev->dev, size, GFP_ATOMIC);
	if (!txwi)
		return NULL;

	addr = dma_map_single(dev->dev, txwi, dev->drv->txwi_size,
			      DMA_TO_DEVICE);
	t = (struct mt76_txwi_cache *)(txwi + dev->drv->txwi_size);
	t->dma_addr = addr;

	return t;
}

static struct mt76_txwi_cache *
__mt76_get_txwi(struct mt76_dev *dev)
{
	struct mt76_txwi_cache *t = NULL;

	spin_lock_bh(&dev->lock);
	if (!list_empty(&dev->txwi_cache)) {
		t = list_first_entry(&dev->txwi_cache, struct mt76_txwi_cache,
				     list);
		list_del(&t->list);
	}
	spin_unlock_bh(&dev->lock);

	return t;
}

struct mt76_txwi_cache *
mt76_get_txwi(struct mt76_dev *dev)
{
	struct mt76_txwi_cache *t = __mt76_get_txwi(dev);

	if (t)
		return t;

	return mt76_alloc_txwi(dev);
}

void
mt76_put_txwi(struct mt76_dev *dev, struct mt76_txwi_cache *t)
{
	if (!t)
		return;

	spin_lock_bh(&dev->lock);
	list_add(&t->list, &dev->txwi_cache);
	spin_unlock_bh(&dev->lock);
}
EXPORT_SYMBOL_GPL(mt76_put_txwi);

void mt76_tx_free(struct mt76_dev *dev)
{
	struct mt76_txwi_cache *t;

	while ((t = __mt76_get_txwi(dev)) != NULL)
		dma_unmap_single(dev->dev, t->dma_addr, dev->drv->txwi_size,
				 DMA_TO_DEVICE);
}

static int
mt76_txq_get_qid(struct ieee80211_txq *txq)
{
	if (!txq->sta)
		return MT_TXQ_BE;

	return txq->ac;
}

static void
mt76_check_agg_ssn(struct mt76_txq *mtxq, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (!ieee80211_is_data_qos(hdr->frame_control) ||
	    !ieee80211_is_data_present(hdr->frame_control))
		return;

	mtxq->agg_ssn = le16_to_cpu(hdr->seq_ctrl) + 0x10;
}

void
mt76_tx_status_lock(struct mt76_dev *dev, struct sk_buff_head *list)
		   __acquires(&dev->status_list.lock)
{
	__skb_queue_head_init(list);
	spin_lock_bh(&dev->status_list.lock);
}
EXPORT_SYMBOL_GPL(mt76_tx_status_lock);

void
mt76_tx_status_unlock(struct mt76_dev *dev, struct sk_buff_head *list)
		      __releases(&dev->status_list.lock)
{
	struct ieee80211_hw *hw;
	struct sk_buff *skb;

	spin_unlock_bh(&dev->status_list.lock);

	while ((skb = __skb_dequeue(list)) != NULL) {
		hw = mt76_tx_status_get_hw(dev, skb);
		ieee80211_tx_status(hw, skb);
	}

}
EXPORT_SYMBOL_GPL(mt76_tx_status_unlock);

static void
__mt76_tx_status_skb_done(struct mt76_dev *dev, struct sk_buff *skb, u8 flags,
			  struct sk_buff_head *list)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mt76_tx_cb *cb = mt76_tx_skb_cb(skb);
	u8 done = MT_TX_CB_DMA_DONE | MT_TX_CB_TXS_DONE;

	flags |= cb->flags;
	cb->flags = flags;

	if ((flags & done) != done)
		return;

	__skb_unlink(skb, &dev->status_list);

	/* Tx status can be unreliable. if it fails, mark the frame as ACKed */
	if (flags & MT_TX_CB_TXS_FAILED) {
		ieee80211_tx_info_clear_status(info);
		info->status.rates[0].idx = -1;
		info->flags |= IEEE80211_TX_STAT_ACK;
	}

	__skb_queue_tail(list, skb);
}

void
mt76_tx_status_skb_done(struct mt76_dev *dev, struct sk_buff *skb,
			struct sk_buff_head *list)
{
	__mt76_tx_status_skb_done(dev, skb, MT_TX_CB_TXS_DONE, list);
}
EXPORT_SYMBOL_GPL(mt76_tx_status_skb_done);

int
mt76_tx_status_skb_add(struct mt76_dev *dev, struct mt76_wcid *wcid,
		       struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mt76_tx_cb *cb = mt76_tx_skb_cb(skb);
	int pid;

	if (!wcid)
		return MT_PACKET_ID_NO_ACK;

	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		return MT_PACKET_ID_NO_ACK;

	if (!(info->flags & (IEEE80211_TX_CTL_REQ_TX_STATUS |
			     IEEE80211_TX_CTL_RATE_CTRL_PROBE)))
		return MT_PACKET_ID_NO_SKB;

	spin_lock_bh(&dev->status_list.lock);

	memset(cb, 0, sizeof(*cb));
	wcid->packet_id = (wcid->packet_id + 1) & MT_PACKET_ID_MASK;
	if (wcid->packet_id == MT_PACKET_ID_NO_ACK ||
	    wcid->packet_id == MT_PACKET_ID_NO_SKB)
		wcid->packet_id = MT_PACKET_ID_FIRST;

	pid = wcid->packet_id;
	cb->wcid = wcid->idx;
	cb->pktid = pid;
	cb->jiffies = jiffies;

	__skb_queue_tail(&dev->status_list, skb);
	spin_unlock_bh(&dev->status_list.lock);

	return pid;
}
EXPORT_SYMBOL_GPL(mt76_tx_status_skb_add);

struct sk_buff *
mt76_tx_status_skb_get(struct mt76_dev *dev, struct mt76_wcid *wcid, int pktid,
		       struct sk_buff_head *list)
{
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&dev->status_list, skb, tmp) {
		struct mt76_tx_cb *cb = mt76_tx_skb_cb(skb);

		if (wcid && cb->wcid != wcid->idx)
			continue;

		if (cb->pktid == pktid)
			return skb;

		if (pktid >= 0 && !time_after(jiffies, cb->jiffies +
					      MT_TX_STATUS_SKB_TIMEOUT))
			continue;

		__mt76_tx_status_skb_done(dev, skb, MT_TX_CB_TXS_FAILED |
						    MT_TX_CB_TXS_DONE, list);
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(mt76_tx_status_skb_get);

void
mt76_tx_status_check(struct mt76_dev *dev, struct mt76_wcid *wcid, bool flush)
{
	struct sk_buff_head list;

	mt76_tx_status_lock(dev, &list);
	mt76_tx_status_skb_get(dev, wcid, flush ? -1 : 0, &list);
	mt76_tx_status_unlock(dev, &list);
}
EXPORT_SYMBOL_GPL(mt76_tx_status_check);

void mt76_tx_complete_skb(struct mt76_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_hw *hw;
	struct sk_buff_head list;

	if (!skb->prev) {
		hw = mt76_tx_status_get_hw(dev, skb);
		ieee80211_free_txskb(hw, skb);
		return;
	}

	mt76_tx_status_lock(dev, &list);
	__mt76_tx_status_skb_done(dev, skb, MT_TX_CB_DMA_DONE, &list);
	mt76_tx_status_unlock(dev, &list);
}
EXPORT_SYMBOL_GPL(mt76_tx_complete_skb);

void
mt76_tx(struct mt76_phy *phy, struct ieee80211_sta *sta,
	struct mt76_wcid *wcid, struct sk_buff *skb)
{
	struct mt76_dev *dev = phy->dev;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct mt76_queue *q;
	int qid = skb_get_queue_mapping(skb);
	bool ext_phy = phy != &dev->phy;

	if (WARN_ON(qid >= MT_TXQ_PSD)) {
		qid = MT_TXQ_BE;
		skb_set_queue_mapping(skb, qid);
	}

	if (!(wcid->tx_info & MT_WCID_TX_INFO_SET))
		ieee80211_get_tx_rates(info->control.vif, sta, skb,
				       info->control.rates, 1);

	if (sta && ieee80211_is_data_qos(hdr->frame_control)) {
		struct ieee80211_txq *txq;
		struct mt76_txq *mtxq;
		u8 tid;

		tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;
		txq = sta->txq[tid];
		mtxq = (struct mt76_txq *)txq->drv_priv;

		if (mtxq->aggr)
			mt76_check_agg_ssn(mtxq, skb);
	}

	if (ext_phy)
		info->hw_queue |= MT_TX_HW_QUEUE_EXT_PHY;

	q = dev->q_tx[qid].q;

	spin_lock_bh(&q->lock);
	dev->queue_ops->tx_queue_skb(dev, qid, skb, wcid, sta);
	dev->queue_ops->kick(dev, q);

	if (q->queued > q->ndesc - 8 && !q->stopped) {
		ieee80211_stop_queue(phy->hw, skb_get_queue_mapping(skb));
		q->stopped = true;
	}

	spin_unlock_bh(&q->lock);
}
EXPORT_SYMBOL_GPL(mt76_tx);

static struct sk_buff *
mt76_txq_dequeue(struct mt76_phy *phy, struct mt76_txq *mtxq, bool ps)
{
	struct ieee80211_txq *txq = mtxq_to_txq(mtxq);
	struct ieee80211_tx_info *info;
	bool ext_phy = phy != &phy->dev->phy;
	struct sk_buff *skb;

	skb = skb_dequeue(&mtxq->retry_q);
	if (skb) {
		u8 tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;

		if (ps && skb_queue_empty(&mtxq->retry_q))
			ieee80211_sta_set_buffered(txq->sta, tid, false);

		return skb;
	}

	skb = ieee80211_tx_dequeue(phy->hw, txq);
	if (!skb)
		return NULL;

	info = IEEE80211_SKB_CB(skb);
	if (ext_phy)
		info->hw_queue |= MT_TX_HW_QUEUE_EXT_PHY;

	return skb;
}

static void
mt76_queue_ps_skb(struct mt76_dev *dev, struct ieee80211_sta *sta,
		  struct sk_buff *skb, bool last)
{
	struct mt76_wcid *wcid = (struct mt76_wcid *)sta->drv_priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	info->control.flags |= IEEE80211_TX_CTRL_PS_RESPONSE;
	if (last)
		info->flags |= IEEE80211_TX_STATUS_EOSP |
			       IEEE80211_TX_CTL_REQ_TX_STATUS;

	mt76_skb_set_moredata(skb, !last);
	dev->queue_ops->tx_queue_skb(dev, MT_TXQ_PSD, skb, wcid, sta);
}

void
mt76_release_buffered_frames(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
			     u16 tids, int nframes,
			     enum ieee80211_frame_release_type reason,
			     bool more_data)
{
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;
	struct sk_buff *last_skb = NULL;
	struct mt76_queue *hwq = dev->q_tx[MT_TXQ_PSD].q;
	int i;

	spin_lock_bh(&hwq->lock);
	for (i = 0; tids && nframes; i++, tids >>= 1) {
		struct ieee80211_txq *txq = sta->txq[i];
		struct mt76_txq *mtxq = (struct mt76_txq *)txq->drv_priv;
		struct sk_buff *skb;

		if (!(tids & 1))
			continue;

		do {
			skb = mt76_txq_dequeue(phy, mtxq, true);
			if (!skb)
				break;

			if (mtxq->aggr)
				mt76_check_agg_ssn(mtxq, skb);

			nframes--;
			if (last_skb)
				mt76_queue_ps_skb(dev, sta, last_skb, false);

			last_skb = skb;
		} while (nframes);
	}

	if (last_skb) {
		mt76_queue_ps_skb(dev, sta, last_skb, true);
		dev->queue_ops->kick(dev, hwq);
	} else {
		ieee80211_sta_eosp(sta);
	}

	spin_unlock_bh(&hwq->lock);
}
EXPORT_SYMBOL_GPL(mt76_release_buffered_frames);

static int
mt76_txq_send_burst(struct mt76_phy *phy, struct mt76_sw_queue *sq,
		    struct mt76_txq *mtxq)
{
	struct mt76_dev *dev = phy->dev;
	struct ieee80211_txq *txq = mtxq_to_txq(mtxq);
	enum mt76_txq_id qid = mt76_txq_get_qid(txq);
	struct mt76_wcid *wcid = mtxq->wcid;
	struct mt76_queue *hwq = sq->q;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	int n_frames = 1, limit;
	struct ieee80211_tx_rate tx_rate;
	bool ampdu;
	bool probe;
	int idx;

	if (test_bit(MT_WCID_FLAG_PS, &wcid->flags))
		return 0;

	skb = mt76_txq_dequeue(phy, mtxq, false);
	if (!skb)
		return 0;

	info = IEEE80211_SKB_CB(skb);
	if (!(wcid->tx_info & MT_WCID_TX_INFO_SET))
		ieee80211_get_tx_rates(txq->vif, txq->sta, skb,
				       info->control.rates, 1);
	tx_rate = info->control.rates[0];

	probe = (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE);
	ampdu = IEEE80211_SKB_CB(skb)->flags & IEEE80211_TX_CTL_AMPDU;
	limit = ampdu ? 16 : 3;

	if (ampdu)
		mt76_check_agg_ssn(mtxq, skb);

	idx = dev->queue_ops->tx_queue_skb(dev, qid, skb, wcid, txq->sta);

	if (idx < 0)
		return idx;

	do {
		bool cur_ampdu;

		if (probe)
			break;

		if (test_bit(MT76_RESET, &phy->state))
			return -EBUSY;

		skb = mt76_txq_dequeue(phy, mtxq, false);
		if (!skb)
			break;

		info = IEEE80211_SKB_CB(skb);
		cur_ampdu = info->flags & IEEE80211_TX_CTL_AMPDU;

		if (ampdu != cur_ampdu ||
		    (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)) {
			skb_queue_tail(&mtxq->retry_q, skb);
			break;
		}

		info->control.rates[0] = tx_rate;

		if (cur_ampdu)
			mt76_check_agg_ssn(mtxq, skb);

		idx = dev->queue_ops->tx_queue_skb(dev, qid, skb, wcid,
						   txq->sta);
		if (idx < 0)
			return idx;

		n_frames++;
	} while (n_frames < limit);

	if (!probe) {
		hwq->entry[idx].qid = sq - dev->q_tx;
		hwq->entry[idx].schedule = true;
		sq->swq_queued++;
	}

	dev->queue_ops->kick(dev, hwq);

	return n_frames;
}

static int
mt76_txq_schedule_list(struct mt76_phy *phy, enum mt76_txq_id qid)
{
	struct mt76_dev *dev = phy->dev;
	struct mt76_sw_queue *sq = &dev->q_tx[qid];
	struct mt76_queue *hwq = sq->q;
	struct ieee80211_txq *txq;
	struct mt76_txq *mtxq;
	struct mt76_wcid *wcid;
	int ret = 0;

	spin_lock_bh(&hwq->lock);
	while (1) {
		if (sq->swq_queued >= 4)
			break;

		if (test_bit(MT76_RESET, &phy->state)) {
			ret = -EBUSY;
			break;
		}

		txq = ieee80211_next_txq(phy->hw, qid);
		if (!txq)
			break;

		mtxq = (struct mt76_txq *)txq->drv_priv;
		wcid = mtxq->wcid;
		if (wcid && test_bit(MT_WCID_FLAG_PS, &wcid->flags))
			continue;

		if (mtxq->send_bar && mtxq->aggr) {
			struct ieee80211_txq *txq = mtxq_to_txq(mtxq);
			struct ieee80211_sta *sta = txq->sta;
			struct ieee80211_vif *vif = txq->vif;
			u16 agg_ssn = mtxq->agg_ssn;
			u8 tid = txq->tid;

			mtxq->send_bar = false;
			spin_unlock_bh(&hwq->lock);
			ieee80211_send_bar(vif, sta->addr, tid, agg_ssn);
			spin_lock_bh(&hwq->lock);
		}

		ret += mt76_txq_send_burst(phy, sq, mtxq);
		ieee80211_return_txq(phy->hw, txq,
				     !skb_queue_empty(&mtxq->retry_q));
	}
	spin_unlock_bh(&hwq->lock);

	return ret;
}

void mt76_txq_schedule(struct mt76_phy *phy, enum mt76_txq_id qid)
{
	struct mt76_dev *dev = phy->dev;
	struct mt76_sw_queue *sq = &dev->q_tx[qid];
	int len;

	if (qid >= 4)
		return;

	if (sq->swq_queued >= 4)
		return;

	rcu_read_lock();

	do {
		ieee80211_txq_schedule_start(phy->hw, qid);
		len = mt76_txq_schedule_list(phy, qid);
		ieee80211_txq_schedule_end(phy->hw, qid);
	} while (len > 0);

	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(mt76_txq_schedule);

void mt76_txq_schedule_all(struct mt76_phy *phy)
{
	int i;

	for (i = 0; i <= MT_TXQ_BK; i++)
		mt76_txq_schedule(phy, i);
}
EXPORT_SYMBOL_GPL(mt76_txq_schedule_all);

void mt76_tx_tasklet(unsigned long data)
{
	struct mt76_dev *dev = (struct mt76_dev *)data;

	mt76_txq_schedule_all(&dev->phy);
	if (dev->phy2)
		mt76_txq_schedule_all(dev->phy2);
}

void mt76_stop_tx_queues(struct mt76_dev *dev, struct ieee80211_sta *sta,
			 bool send_bar)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
		struct ieee80211_txq *txq = sta->txq[i];
		struct mt76_queue *hwq;
		struct mt76_txq *mtxq;

		if (!txq)
			continue;

		mtxq = (struct mt76_txq *)txq->drv_priv;
		hwq = mtxq->swq->q;

		spin_lock_bh(&hwq->lock);
		mtxq->send_bar = mtxq->aggr && send_bar;
		spin_unlock_bh(&hwq->lock);
	}
}
EXPORT_SYMBOL_GPL(mt76_stop_tx_queues);

void mt76_wake_tx_queue(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;

	if (!test_bit(MT76_STATE_RUNNING, &phy->state))
		return;

	tasklet_schedule(&dev->tx_tasklet);
}
EXPORT_SYMBOL_GPL(mt76_wake_tx_queue);

void mt76_txq_remove(struct mt76_dev *dev, struct ieee80211_txq *txq)
{
	struct ieee80211_hw *hw;
	struct mt76_txq *mtxq;
	struct sk_buff *skb;

	if (!txq)
		return;

	mtxq = (struct mt76_txq *)txq->drv_priv;

	while ((skb = skb_dequeue(&mtxq->retry_q)) != NULL) {
		hw = mt76_tx_status_get_hw(dev, skb);
		ieee80211_free_txskb(hw, skb);
	}
}
EXPORT_SYMBOL_GPL(mt76_txq_remove);

void mt76_txq_init(struct mt76_dev *dev, struct ieee80211_txq *txq)
{
	struct mt76_txq *mtxq = (struct mt76_txq *)txq->drv_priv;

	skb_queue_head_init(&mtxq->retry_q);

	mtxq->swq = &dev->q_tx[mt76_txq_get_qid(txq)];
}
EXPORT_SYMBOL_GPL(mt76_txq_init);

u8 mt76_ac_to_hwq(u8 ac)
{
	static const u8 wmm_queue_map[] = {
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
		[IEEE80211_AC_VI] = 2,
		[IEEE80211_AC_VO] = 3,
	};

	if (WARN_ON(ac >= IEEE80211_NUM_ACS))
		return 0;

	return wmm_queue_map[ac];
}
EXPORT_SYMBOL_GPL(mt76_ac_to_hwq);
