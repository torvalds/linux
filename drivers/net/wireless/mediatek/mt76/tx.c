// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#include "mt76.h"

static int
mt76_txq_get_qid(struct ieee80211_txq *txq)
{
	if (!txq->sta)
		return MT_TXQ_BE;

	return txq->ac;
}

void
mt76_tx_check_agg_ssn(struct ieee80211_sta *sta, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_txq *txq;
	struct mt76_txq *mtxq;
	u8 tid;

	if (!sta || !ieee80211_is_data_qos(hdr->frame_control) ||
	    !ieee80211_is_data_present(hdr->frame_control))
		return;

	tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
	txq = sta->txq[tid];
	mtxq = (struct mt76_txq *)txq->drv_priv;
	if (!mtxq->aggr)
		return;

	mtxq->agg_ssn = le16_to_cpu(hdr->seq_ctrl) + 0x10;
}
EXPORT_SYMBOL_GPL(mt76_tx_check_agg_ssn);

void
mt76_tx_status_lock(struct mt76_dev *dev, struct sk_buff_head *list)
		   __acquires(&dev->status_lock)
{
	__skb_queue_head_init(list);
	spin_lock_bh(&dev->status_lock);
}
EXPORT_SYMBOL_GPL(mt76_tx_status_lock);

void
mt76_tx_status_unlock(struct mt76_dev *dev, struct sk_buff_head *list)
		      __releases(&dev->status_lock)
{
	struct ieee80211_hw *hw;
	struct sk_buff *skb;

	spin_unlock_bh(&dev->status_lock);

	rcu_read_lock();
	while ((skb = __skb_dequeue(list)) != NULL) {
		struct ieee80211_tx_status status = {
			.skb = skb,
			.info = IEEE80211_SKB_CB(skb),
		};
		struct mt76_tx_cb *cb = mt76_tx_skb_cb(skb);
		struct mt76_wcid *wcid;

		wcid = rcu_dereference(dev->wcid[cb->wcid]);
		if (wcid) {
			status.sta = wcid_to_sta(wcid);
			status.rates = NULL;
			status.n_rates = 0;
		}

		hw = mt76_tx_status_get_hw(dev, skb);
		ieee80211_tx_status_ext(hw, &status);
	}
	rcu_read_unlock();
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

	/* Tx status can be unreliable. if it fails, mark the frame as ACKed */
	if (flags & MT_TX_CB_TXS_FAILED) {
		info->status.rates[0].count = 0;
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

	memset(cb, 0, sizeof(*cb));

	if (!wcid || !rcu_access_pointer(dev->wcid[wcid->idx]))
		return MT_PACKET_ID_NO_ACK;

	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		return MT_PACKET_ID_NO_ACK;

	if (!(info->flags & (IEEE80211_TX_CTL_REQ_TX_STATUS |
			     IEEE80211_TX_CTL_RATE_CTRL_PROBE)))
		return MT_PACKET_ID_NO_SKB;

	spin_lock_bh(&dev->status_lock);

	pid = idr_alloc(&wcid->pktid, skb, MT_PACKET_ID_FIRST,
			MT_PACKET_ID_MASK, GFP_ATOMIC);
	if (pid < 0) {
		pid = MT_PACKET_ID_NO_SKB;
		goto out;
	}

	cb->wcid = wcid->idx;
	cb->pktid = pid;

	if (list_empty(&wcid->list))
		list_add_tail(&wcid->list, &dev->wcid_list);

out:
	spin_unlock_bh(&dev->status_lock);

	return pid;
}
EXPORT_SYMBOL_GPL(mt76_tx_status_skb_add);

struct sk_buff *
mt76_tx_status_skb_get(struct mt76_dev *dev, struct mt76_wcid *wcid, int pktid,
		       struct sk_buff_head *list)
{
	struct sk_buff *skb;
	int id;

	lockdep_assert_held(&dev->status_lock);

	skb = idr_remove(&wcid->pktid, pktid);
	if (skb)
		goto out;

	/* look for stale entries in the wcid idr queue */
	idr_for_each_entry(&wcid->pktid, skb, id) {
		struct mt76_tx_cb *cb = mt76_tx_skb_cb(skb);

		if (pktid >= 0) {
			if (!(cb->flags & MT_TX_CB_DMA_DONE))
				continue;

			if (time_is_after_jiffies(cb->jiffies +
						   MT_TX_STATUS_SKB_TIMEOUT))
				continue;
		}

		/* It has been too long since DMA_DONE, time out this packet
		 * and stop waiting for TXS callback.
		 */
		idr_remove(&wcid->pktid, cb->pktid);
		__mt76_tx_status_skb_done(dev, skb, MT_TX_CB_TXS_FAILED |
						    MT_TX_CB_TXS_DONE, list);
	}

out:
	if (idr_is_empty(&wcid->pktid))
		list_del_init(&wcid->list);

	return skb;
}
EXPORT_SYMBOL_GPL(mt76_tx_status_skb_get);

void
mt76_tx_status_check(struct mt76_dev *dev, bool flush)
{
	struct mt76_wcid *wcid, *tmp;
	struct sk_buff_head list;

	mt76_tx_status_lock(dev, &list);
	list_for_each_entry_safe(wcid, tmp, &dev->wcid_list, list)
		mt76_tx_status_skb_get(dev, wcid, flush ? -1 : 0, &list);
	mt76_tx_status_unlock(dev, &list);
}
EXPORT_SYMBOL_GPL(mt76_tx_status_check);

static void
mt76_tx_check_non_aql(struct mt76_dev *dev, struct mt76_wcid *wcid,
		      struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int pending;

	if (!wcid || info->tx_time_est)
		return;

	pending = atomic_dec_return(&wcid->non_aql_packets);
	if (pending < 0)
		atomic_cmpxchg(&wcid->non_aql_packets, pending, 0);
}

void __mt76_tx_complete_skb(struct mt76_dev *dev, u16 wcid_idx, struct sk_buff *skb,
			    struct list_head *free_list)
{
	struct mt76_tx_cb *cb = mt76_tx_skb_cb(skb);
	struct ieee80211_tx_status status = {
		.skb = skb,
		.free_list = free_list,
	};
	struct mt76_wcid *wcid = NULL;
	struct ieee80211_hw *hw;
	struct sk_buff_head list;

	rcu_read_lock();

	if (wcid_idx < ARRAY_SIZE(dev->wcid))
		wcid = rcu_dereference(dev->wcid[wcid_idx]);

	mt76_tx_check_non_aql(dev, wcid, skb);

#ifdef CONFIG_NL80211_TESTMODE
	if (mt76_is_testmode_skb(dev, skb, &hw)) {
		struct mt76_phy *phy = hw->priv;

		if (skb == phy->test.tx_skb)
			phy->test.tx_done++;
		if (phy->test.tx_queued == phy->test.tx_done)
			wake_up(&dev->tx_wait);

		dev_kfree_skb_any(skb);
		goto out;
	}
#endif

	if (cb->pktid < MT_PACKET_ID_FIRST) {
		hw = mt76_tx_status_get_hw(dev, skb);
		status.sta = wcid_to_sta(wcid);
		ieee80211_tx_status_ext(hw, &status);
		goto out;
	}

	mt76_tx_status_lock(dev, &list);
	cb->jiffies = jiffies;
	__mt76_tx_status_skb_done(dev, skb, MT_TX_CB_DMA_DONE, &list);
	mt76_tx_status_unlock(dev, &list);

out:
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(__mt76_tx_complete_skb);

static int
__mt76_tx_queue_skb(struct mt76_phy *phy, int qid, struct sk_buff *skb,
		    struct mt76_wcid *wcid, struct ieee80211_sta *sta,
		    bool *stop)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mt76_queue *q = phy->q_tx[qid];
	struct mt76_dev *dev = phy->dev;
	bool non_aql;
	int pending;
	int idx;

	non_aql = !info->tx_time_est;
	idx = dev->queue_ops->tx_queue_skb(dev, q, skb, wcid, sta);
	if (idx < 0 || !sta)
		return idx;

	wcid = (struct mt76_wcid *)sta->drv_priv;
	q->entry[idx].wcid = wcid->idx;

	if (!non_aql)
		return idx;

	pending = atomic_inc_return(&wcid->non_aql_packets);
	if (stop && pending >= MT_MAX_NON_AQL_PKT)
		*stop = true;

	return idx;
}

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

	if (mt76_testmode_enabled(phy)) {
		ieee80211_free_txskb(phy->hw, skb);
		return;
	}

	if (WARN_ON(qid >= MT_TXQ_PSD)) {
		qid = MT_TXQ_BE;
		skb_set_queue_mapping(skb, qid);
	}

	if ((dev->drv->drv_flags & MT_DRV_HW_MGMT_TXQ) &&
	    !(info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP) &&
	    !ieee80211_is_data(hdr->frame_control) &&
	    !ieee80211_is_bufferable_mmpdu(hdr->frame_control)) {
		qid = MT_TXQ_PSD;
		skb_set_queue_mapping(skb, qid);
	}

	if (wcid && !(wcid->tx_info & MT_WCID_TX_INFO_SET))
		ieee80211_get_tx_rates(info->control.vif, sta, skb,
				       info->control.rates, 1);

	if (ext_phy)
		info->hw_queue |= MT_TX_HW_QUEUE_EXT_PHY;

	q = phy->q_tx[qid];

	spin_lock_bh(&q->lock);
	__mt76_tx_queue_skb(phy, qid, skb, wcid, sta, NULL);
	dev->queue_ops->kick(dev, q);
	spin_unlock_bh(&q->lock);
}
EXPORT_SYMBOL_GPL(mt76_tx);

static struct sk_buff *
mt76_txq_dequeue(struct mt76_phy *phy, struct mt76_txq *mtxq)
{
	struct ieee80211_txq *txq = mtxq_to_txq(mtxq);
	struct ieee80211_tx_info *info;
	bool ext_phy = phy != &phy->dev->phy;
	struct sk_buff *skb;

	skb = ieee80211_tx_dequeue(phy->hw, txq);
	if (!skb)
		return NULL;

	info = IEEE80211_SKB_CB(skb);
	if (ext_phy)
		info->hw_queue |= MT_TX_HW_QUEUE_EXT_PHY;

	return skb;
}

static void
mt76_queue_ps_skb(struct mt76_phy *phy, struct ieee80211_sta *sta,
		  struct sk_buff *skb, bool last)
{
	struct mt76_wcid *wcid = (struct mt76_wcid *)sta->drv_priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	info->control.flags |= IEEE80211_TX_CTRL_PS_RESPONSE;
	if (last)
		info->flags |= IEEE80211_TX_STATUS_EOSP |
			       IEEE80211_TX_CTL_REQ_TX_STATUS;

	mt76_skb_set_moredata(skb, !last);
	__mt76_tx_queue_skb(phy, MT_TXQ_PSD, skb, wcid, sta, NULL);
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
	struct mt76_queue *hwq = phy->q_tx[MT_TXQ_PSD];
	int i;

	spin_lock_bh(&hwq->lock);
	for (i = 0; tids && nframes; i++, tids >>= 1) {
		struct ieee80211_txq *txq = sta->txq[i];
		struct mt76_txq *mtxq = (struct mt76_txq *)txq->drv_priv;
		struct sk_buff *skb;

		if (!(tids & 1))
			continue;

		do {
			skb = mt76_txq_dequeue(phy, mtxq);
			if (!skb)
				break;

			nframes--;
			if (last_skb)
				mt76_queue_ps_skb(phy, sta, last_skb, false);

			last_skb = skb;
		} while (nframes);
	}

	if (last_skb) {
		mt76_queue_ps_skb(phy, sta, last_skb, true);
		dev->queue_ops->kick(dev, hwq);
	} else {
		ieee80211_sta_eosp(sta);
	}

	spin_unlock_bh(&hwq->lock);
}
EXPORT_SYMBOL_GPL(mt76_release_buffered_frames);

static bool
mt76_txq_stopped(struct mt76_queue *q)
{
	return q->stopped || q->blocked ||
	       q->queued + MT_TXQ_FREE_THR >= q->ndesc;
}

static int
mt76_txq_send_burst(struct mt76_phy *phy, struct mt76_queue *q,
		    struct mt76_txq *mtxq, struct mt76_wcid *wcid)
{
	struct mt76_dev *dev = phy->dev;
	struct ieee80211_txq *txq = mtxq_to_txq(mtxq);
	enum mt76_txq_id qid = mt76_txq_get_qid(txq);
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	int n_frames = 1;
	bool stop = false;
	int idx;

	if (test_bit(MT_WCID_FLAG_PS, &wcid->flags))
		return 0;

	if (atomic_read(&wcid->non_aql_packets) >= MT_MAX_NON_AQL_PKT)
		return 0;

	skb = mt76_txq_dequeue(phy, mtxq);
	if (!skb)
		return 0;

	info = IEEE80211_SKB_CB(skb);
	if (!(wcid->tx_info & MT_WCID_TX_INFO_SET))
		ieee80211_get_tx_rates(txq->vif, txq->sta, skb,
				       info->control.rates, 1);

	spin_lock(&q->lock);
	idx = __mt76_tx_queue_skb(phy, qid, skb, wcid, txq->sta, &stop);
	spin_unlock(&q->lock);
	if (idx < 0)
		return idx;

	do {
		if (test_bit(MT76_RESET, &phy->state))
			return -EBUSY;

		if (stop || mt76_txq_stopped(q))
			break;

		skb = mt76_txq_dequeue(phy, mtxq);
		if (!skb)
			break;

		info = IEEE80211_SKB_CB(skb);
		if (!(wcid->tx_info & MT_WCID_TX_INFO_SET))
			ieee80211_get_tx_rates(txq->vif, txq->sta, skb,
					       info->control.rates, 1);

		spin_lock(&q->lock);
		idx = __mt76_tx_queue_skb(phy, qid, skb, wcid, txq->sta, &stop);
		spin_unlock(&q->lock);
		if (idx < 0)
			break;

		n_frames++;
	} while (1);

	spin_lock(&q->lock);
	dev->queue_ops->kick(dev, q);
	spin_unlock(&q->lock);

	return n_frames;
}

static int
mt76_txq_schedule_list(struct mt76_phy *phy, enum mt76_txq_id qid)
{
	struct mt76_queue *q = phy->q_tx[qid];
	struct mt76_dev *dev = phy->dev;
	struct ieee80211_txq *txq;
	struct mt76_txq *mtxq;
	struct mt76_wcid *wcid;
	int ret = 0;

	while (1) {
		int n_frames = 0;

		if (test_bit(MT76_RESET, &phy->state))
			return -EBUSY;

		if (dev->queue_ops->tx_cleanup &&
		    q->queued + 2 * MT_TXQ_FREE_THR >= q->ndesc) {
			dev->queue_ops->tx_cleanup(dev, q, false);
		}

		txq = ieee80211_next_txq(phy->hw, qid);
		if (!txq)
			break;

		mtxq = (struct mt76_txq *)txq->drv_priv;
		wcid = rcu_dereference(dev->wcid[mtxq->wcid]);
		if (!wcid || test_bit(MT_WCID_FLAG_PS, &wcid->flags))
			continue;

		if (mtxq->send_bar && mtxq->aggr) {
			struct ieee80211_txq *txq = mtxq_to_txq(mtxq);
			struct ieee80211_sta *sta = txq->sta;
			struct ieee80211_vif *vif = txq->vif;
			u16 agg_ssn = mtxq->agg_ssn;
			u8 tid = txq->tid;

			mtxq->send_bar = false;
			ieee80211_send_bar(vif, sta->addr, tid, agg_ssn);
		}

		if (!mt76_txq_stopped(q))
			n_frames = mt76_txq_send_burst(phy, q, mtxq, wcid);

		ieee80211_return_txq(phy->hw, txq, false);

		if (unlikely(n_frames < 0))
			return n_frames;

		ret += n_frames;
	}

	return ret;
}

void mt76_txq_schedule(struct mt76_phy *phy, enum mt76_txq_id qid)
{
	int len;

	if (qid >= 4)
		return;

	local_bh_disable();
	rcu_read_lock();

	do {
		ieee80211_txq_schedule_start(phy->hw, qid);
		len = mt76_txq_schedule_list(phy, qid);
		ieee80211_txq_schedule_end(phy->hw, qid);
	} while (len > 0);

	rcu_read_unlock();
	local_bh_enable();
}
EXPORT_SYMBOL_GPL(mt76_txq_schedule);

void mt76_txq_schedule_all(struct mt76_phy *phy)
{
	int i;

	for (i = 0; i <= MT_TXQ_BK; i++)
		mt76_txq_schedule(phy, i);
}
EXPORT_SYMBOL_GPL(mt76_txq_schedule_all);

void mt76_tx_worker_run(struct mt76_dev *dev)
{
	mt76_txq_schedule_all(&dev->phy);
	if (dev->phy2)
		mt76_txq_schedule_all(dev->phy2);

#ifdef CONFIG_NL80211_TESTMODE
	if (dev->phy.test.tx_pending)
		mt76_testmode_tx_pending(&dev->phy);
	if (dev->phy2 && dev->phy2->test.tx_pending)
		mt76_testmode_tx_pending(dev->phy2);
#endif
}
EXPORT_SYMBOL_GPL(mt76_tx_worker_run);

void mt76_tx_worker(struct mt76_worker *w)
{
	struct mt76_dev *dev = container_of(w, struct mt76_dev, tx_worker);

	mt76_tx_worker_run(dev);
}

void mt76_stop_tx_queues(struct mt76_phy *phy, struct ieee80211_sta *sta,
			 bool send_bar)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
		struct ieee80211_txq *txq = sta->txq[i];
		struct mt76_queue *hwq;
		struct mt76_txq *mtxq;

		if (!txq)
			continue;

		hwq = phy->q_tx[mt76_txq_get_qid(txq)];
		mtxq = (struct mt76_txq *)txq->drv_priv;

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

	mt76_worker_schedule(&dev->tx_worker);
}
EXPORT_SYMBOL_GPL(mt76_wake_tx_queue);

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

int mt76_skb_adjust_pad(struct sk_buff *skb, int pad)
{
	struct sk_buff *iter, *last = skb;

	/* First packet of a A-MSDU burst keeps track of the whole burst
	 * length, need to update length of it and the last packet.
	 */
	skb_walk_frags(skb, iter) {
		last = iter;
		if (!iter->next) {
			skb->data_len += pad;
			skb->len += pad;
			break;
		}
	}

	if (skb_pad(last, pad))
		return -ENOMEM;

	__skb_put(last, pad);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_skb_adjust_pad);

void mt76_queue_tx_complete(struct mt76_dev *dev, struct mt76_queue *q,
			    struct mt76_queue_entry *e)
{
	if (e->skb)
		dev->drv->tx_complete_skb(dev, e);

	spin_lock_bh(&q->lock);
	q->tail = (q->tail + 1) % q->ndesc;
	q->queued--;
	spin_unlock_bh(&q->lock);
}
EXPORT_SYMBOL_GPL(mt76_queue_tx_complete);

void __mt76_set_tx_blocked(struct mt76_dev *dev, bool blocked)
{
	struct mt76_phy *phy = &dev->phy, *phy2 = dev->phy2;
	struct mt76_queue *q, *q2 = NULL;

	q = phy->q_tx[0];
	if (blocked == q->blocked)
		return;

	q->blocked = blocked;
	if (phy2) {
		q2 = phy2->q_tx[0];
		q2->blocked = blocked;
	}

	if (!blocked)
		mt76_worker_schedule(&dev->tx_worker);
}
EXPORT_SYMBOL_GPL(__mt76_set_tx_blocked);

int mt76_token_consume(struct mt76_dev *dev, struct mt76_txwi_cache **ptxwi)
{
	int token;

	spin_lock_bh(&dev->token_lock);

	token = idr_alloc(&dev->token, *ptxwi, 0, dev->token_size, GFP_ATOMIC);
	if (token >= 0)
		dev->token_count++;

#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	if (mtk_wed_device_active(&dev->mmio.wed) &&
	    token >= dev->mmio.wed.wlan.token_start)
		dev->wed_token_count++;
#endif

	if (dev->token_count >= dev->token_size - MT76_TOKEN_FREE_THR)
		__mt76_set_tx_blocked(dev, true);

	spin_unlock_bh(&dev->token_lock);

	return token;
}
EXPORT_SYMBOL_GPL(mt76_token_consume);

struct mt76_txwi_cache *
mt76_token_release(struct mt76_dev *dev, int token, bool *wake)
{
	struct mt76_txwi_cache *txwi;

	spin_lock_bh(&dev->token_lock);

	txwi = idr_remove(&dev->token, token);
	if (txwi) {
		dev->token_count--;

#ifdef CONFIG_NET_MEDIATEK_SOC_WED
		if (mtk_wed_device_active(&dev->mmio.wed) &&
		    token >= dev->mmio.wed.wlan.token_start &&
		    --dev->wed_token_count == 0)
			wake_up(&dev->tx_wait);
#endif
	}

	if (dev->token_count < dev->token_size - MT76_TOKEN_FREE_THR &&
	    dev->phy.q_tx[0]->blocked)
		*wake = true;

	spin_unlock_bh(&dev->token_lock);

	return txwi;
}
EXPORT_SYMBOL_GPL(mt76_token_release);
