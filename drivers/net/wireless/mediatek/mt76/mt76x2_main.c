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

#include "mt76x2.h"

static int
mt76x2_start(struct ieee80211_hw *hw)
{
	struct mt76x2_dev *dev = hw->priv;
	int ret;

	mutex_lock(&dev->mutex);

	ret = mt76x2_mac_start(dev);
	if (ret)
		goto out;

	ret = mt76x2_phy_start(dev);
	if (ret)
		goto out;

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mac_work,
				     MT_CALIBRATE_INTERVAL);

	set_bit(MT76_STATE_RUNNING, &dev->mt76.state);

out:
	mutex_unlock(&dev->mutex);
	return ret;
}

static void
mt76x2_stop(struct ieee80211_hw *hw)
{
	struct mt76x2_dev *dev = hw->priv;

	mutex_lock(&dev->mutex);
	clear_bit(MT76_STATE_RUNNING, &dev->mt76.state);
	mt76x2_stop_hardware(dev);
	mutex_unlock(&dev->mutex);
}

static void
mt76x2_txq_init(struct mt76x2_dev *dev, struct ieee80211_txq *txq)
{
	struct mt76_txq *mtxq;

	if (!txq)
		return;

	mtxq = (struct mt76_txq *) txq->drv_priv;
	if (txq->sta) {
		struct mt76x2_sta *sta;

		sta = (struct mt76x2_sta *) txq->sta->drv_priv;
		mtxq->wcid = &sta->wcid;
	} else {
		struct mt76x2_vif *mvif;

		mvif = (struct mt76x2_vif *) txq->vif->drv_priv;
		mtxq->wcid = &mvif->group_wcid;
	}

	mt76_txq_init(&dev->mt76, txq);
}

static int
mt76x2_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt76x2_dev *dev = hw->priv;
	struct mt76x2_vif *mvif = (struct mt76x2_vif *) vif->drv_priv;
	unsigned int idx = 0;

	if (vif->addr[0] & BIT(1))
		idx = 1 + (((dev->mt76.macaddr[0] ^ vif->addr[0]) >> 2) & 7);

	/*
	 * Client mode typically only has one configurable BSSID register,
	 * which is used for bssidx=0. This is linked to the MAC address.
	 * Since mac80211 allows changing interface types, and we cannot
	 * force the use of the primary MAC address for a station mode
	 * interface, we need some other way of configuring a per-interface
	 * remote BSSID.
	 * The hardware provides an AP-Client feature, where bssidx 0-7 are
	 * used for AP mode and bssidx 8-15 for client mode.
	 * We shift the station interface bss index by 8 to force the
	 * hardware to recognize the BSSID.
	 * The resulting bssidx mismatch for unicast frames is ignored by hw.
	 */
	if (vif->type == NL80211_IFTYPE_STATION)
		idx += 8;

	mvif->idx = idx;
	mvif->group_wcid.idx = 254 - idx;
	mvif->group_wcid.hw_key_idx = -1;
	mt76x2_txq_init(dev, vif->txq);

	return 0;
}

static void
mt76x2_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt76x2_dev *dev = hw->priv;

	mt76_txq_remove(&dev->mt76, vif->txq);
}

static int
mt76x2_set_channel(struct mt76x2_dev *dev, struct cfg80211_chan_def *chandef)
{
	int ret;

	mt76_set_channel(&dev->mt76);

	tasklet_disable(&dev->pre_tbtt_tasklet);
	tasklet_disable(&dev->dfs_pd.dfs_tasklet);
	cancel_delayed_work_sync(&dev->cal_work);

	mt76x2_mac_stop(dev, true);
	ret = mt76x2_phy_set_channel(dev, chandef);

	/* channel cycle counters read-and-clear */
	mt76_rr(dev, MT_CH_IDLE);
	mt76_rr(dev, MT_CH_BUSY);

	mt76x2_dfs_init_params(dev);

	mt76x2_mac_resume(dev);
	tasklet_enable(&dev->dfs_pd.dfs_tasklet);
	tasklet_enable(&dev->pre_tbtt_tasklet);

	return ret;
}

static int
mt76x2_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt76x2_dev *dev = hw->priv;
	int ret = 0;

	mutex_lock(&dev->mutex);

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		if (!(hw->conf.flags & IEEE80211_CONF_MONITOR))
			dev->rxfilter |= MT_RX_FILTR_CFG_PROMISC;
		else
			dev->rxfilter &= ~MT_RX_FILTR_CFG_PROMISC;

		mt76_wr(dev, MT_RX_FILTR_CFG, dev->rxfilter);
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		dev->txpower_conf = hw->conf.power_level * 2;

		/* convert to per-chain power for 2x2 devices */
		dev->txpower_conf -= 6;

		if (test_bit(MT76_STATE_RUNNING, &dev->mt76.state)) {
			mt76x2_phy_set_txpower(dev);
			mt76x2_tx_set_txpwr_auto(dev, dev->txpower_conf);
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ieee80211_stop_queues(hw);
		ret = mt76x2_set_channel(dev, &hw->conf.chandef);
		ieee80211_wake_queues(hw);
	}

	mutex_unlock(&dev->mutex);

	return ret;
}

static void
mt76x2_configure_filter(struct ieee80211_hw *hw, unsigned int changed_flags,
			unsigned int *total_flags, u64 multicast)
{
	struct mt76x2_dev *dev = hw->priv;
	u32 flags = 0;

#define MT76_FILTER(_flag, _hw) do { \
		flags |= *total_flags & FIF_##_flag;			\
		dev->rxfilter &= ~(_hw);				\
		dev->rxfilter |= !(flags & FIF_##_flag) * (_hw);	\
	} while (0)

	mutex_lock(&dev->mutex);

	dev->rxfilter &= ~MT_RX_FILTR_CFG_OTHER_BSS;

	MT76_FILTER(FCSFAIL, MT_RX_FILTR_CFG_CRC_ERR);
	MT76_FILTER(PLCPFAIL, MT_RX_FILTR_CFG_PHY_ERR);
	MT76_FILTER(CONTROL, MT_RX_FILTR_CFG_ACK |
			     MT_RX_FILTR_CFG_CTS |
			     MT_RX_FILTR_CFG_CFEND |
			     MT_RX_FILTR_CFG_CFACK |
			     MT_RX_FILTR_CFG_BA |
			     MT_RX_FILTR_CFG_CTRL_RSV);
	MT76_FILTER(PSPOLL, MT_RX_FILTR_CFG_PSPOLL);

	*total_flags = flags;
	mt76_wr(dev, MT_RX_FILTR_CFG, dev->rxfilter);

	mutex_unlock(&dev->mutex);
}

static void
mt76x2_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *info, u32 changed)
{
	struct mt76x2_dev *dev = hw->priv;
	struct mt76x2_vif *mvif = (struct mt76x2_vif *) vif->drv_priv;

	mutex_lock(&dev->mutex);

	if (changed & BSS_CHANGED_BSSID)
		mt76x2_mac_set_bssid(dev, mvif->idx, info->bssid);

	if (changed & BSS_CHANGED_BEACON_INT)
		mt76_rmw_field(dev, MT_BEACON_TIME_CFG,
			       MT_BEACON_TIME_CFG_INTVAL,
			       info->beacon_int << 4);

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		tasklet_disable(&dev->pre_tbtt_tasklet);
		mt76x2_mac_set_beacon_enable(dev, mvif->idx,
					     info->enable_beacon);
		tasklet_enable(&dev->pre_tbtt_tasklet);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		int slottime = info->use_short_slot ? 9 : 20;

		dev->slottime = slottime;
		mt76_rmw_field(dev, MT_BKOFF_SLOT_CFG,
			       MT_BKOFF_SLOT_CFG_SLOTTIME, slottime);
	}

	mutex_unlock(&dev->mutex);
}

static int
mt76x2_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       struct ieee80211_sta *sta)
{
	struct mt76x2_dev *dev = hw->priv;
	struct mt76x2_sta *msta = (struct mt76x2_sta *) sta->drv_priv;
	struct mt76x2_vif *mvif = (struct mt76x2_vif *) vif->drv_priv;
	int ret = 0;
	int idx = 0;
	int i;

	mutex_lock(&dev->mutex);

	idx = mt76_wcid_alloc(dev->wcid_mask, ARRAY_SIZE(dev->wcid));
	if (idx < 0) {
		ret = -ENOSPC;
		goto out;
	}

	msta->vif = mvif;
	msta->wcid.sta = 1;
	msta->wcid.idx = idx;
	msta->wcid.hw_key_idx = -1;
	mt76x2_mac_wcid_setup(dev, idx, mvif->idx, sta->addr);
	mt76x2_mac_wcid_set_drop(dev, idx, false);
	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		mt76x2_txq_init(dev, sta->txq[i]);

	if (vif->type == NL80211_IFTYPE_AP)
		set_bit(MT_WCID_FLAG_CHECK_PS, &msta->wcid.flags);

	rcu_assign_pointer(dev->wcid[idx], &msta->wcid);

out:
	mutex_unlock(&dev->mutex);

	return ret;
}

static int
mt76x2_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  struct ieee80211_sta *sta)
{
	struct mt76x2_dev *dev = hw->priv;
	struct mt76x2_sta *msta = (struct mt76x2_sta *) sta->drv_priv;
	int idx = msta->wcid.idx;
	int i;

	mutex_lock(&dev->mutex);
	rcu_assign_pointer(dev->wcid[idx], NULL);
	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		mt76_txq_remove(&dev->mt76, sta->txq[i]);
	mt76x2_mac_wcid_set_drop(dev, idx, true);
	mt76_wcid_free(dev->wcid_mask, idx);
	mt76x2_mac_wcid_setup(dev, idx, 0, NULL);
	mutex_unlock(&dev->mutex);

	return 0;
}

void
mt76x2_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps)
{
	struct mt76x2_sta *msta = (struct mt76x2_sta *) sta->drv_priv;
	struct mt76x2_dev *dev = container_of(mdev, struct mt76x2_dev, mt76);
	int idx = msta->wcid.idx;

	mt76_stop_tx_queues(&dev->mt76, sta, true);
	mt76x2_mac_wcid_set_drop(dev, idx, ps);
}

static int
mt76x2_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
	       struct ieee80211_vif *vif, struct ieee80211_sta *sta,
	       struct ieee80211_key_conf *key)
{
	struct mt76x2_dev *dev = hw->priv;
	struct mt76x2_vif *mvif = (struct mt76x2_vif *) vif->drv_priv;
	struct mt76x2_sta *msta;
	struct mt76_wcid *wcid;
	int idx = key->keyidx;
	int ret;

	/* fall back to sw encryption for unsupported ciphers */
	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
		break;
	default:
		return -EOPNOTSUPP;
	}

	/*
	 * The hardware does not support per-STA RX GTK, fall back
	 * to software mode for these.
	 */
	if ((vif->type == NL80211_IFTYPE_ADHOC ||
	     vif->type == NL80211_IFTYPE_MESH_POINT) &&
	    (key->cipher == WLAN_CIPHER_SUITE_TKIP ||
	     key->cipher == WLAN_CIPHER_SUITE_CCMP) &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return -EOPNOTSUPP;

	msta = sta ? (struct mt76x2_sta *) sta->drv_priv : NULL;
	wcid = msta ? &msta->wcid : &mvif->group_wcid;

	if (cmd == SET_KEY) {
		key->hw_key_idx = wcid->idx;
		wcid->hw_key_idx = idx;
		if (key->flags & IEEE80211_KEY_FLAG_RX_MGMT) {
			key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
			wcid->sw_iv = true;
		}
	} else {
		if (idx == wcid->hw_key_idx) {
			wcid->hw_key_idx = -1;
			wcid->sw_iv = true;
		}

		key = NULL;
	}
	mt76_wcid_key_setup(&dev->mt76, wcid, key);

	if (!msta) {
		if (key || wcid->hw_key_idx == idx) {
			ret = mt76x2_mac_wcid_set_key(dev, wcid->idx, key);
			if (ret)
				return ret;
		}

		return mt76x2_mac_shared_key_setup(dev, mvif->idx, idx, key);
	}

	return mt76x2_mac_wcid_set_key(dev, msta->wcid.idx, key);
}

static int
mt76x2_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u16 queue,
	       const struct ieee80211_tx_queue_params *params)
{
	struct mt76x2_dev *dev = hw->priv;
	u8 cw_min = 5, cw_max = 10, qid;
	u32 val;

	qid = dev->mt76.q_tx[queue].hw_idx;

	if (params->cw_min)
		cw_min = fls(params->cw_min);
	if (params->cw_max)
		cw_max = fls(params->cw_max);

	val = FIELD_PREP(MT_EDCA_CFG_TXOP, params->txop) |
	      FIELD_PREP(MT_EDCA_CFG_AIFSN, params->aifs) |
	      FIELD_PREP(MT_EDCA_CFG_CWMIN, cw_min) |
	      FIELD_PREP(MT_EDCA_CFG_CWMAX, cw_max);
	mt76_wr(dev, MT_EDCA_CFG_AC(qid), val);

	val = mt76_rr(dev, MT_WMM_TXOP(qid));
	val &= ~(MT_WMM_TXOP_MASK << MT_WMM_TXOP_SHIFT(qid));
	val |= params->txop << MT_WMM_TXOP_SHIFT(qid);
	mt76_wr(dev, MT_WMM_TXOP(qid), val);

	val = mt76_rr(dev, MT_WMM_AIFSN);
	val &= ~(MT_WMM_AIFSN_MASK << MT_WMM_AIFSN_SHIFT(qid));
	val |= params->aifs << MT_WMM_AIFSN_SHIFT(qid);
	mt76_wr(dev, MT_WMM_AIFSN, val);

	val = mt76_rr(dev, MT_WMM_CWMIN);
	val &= ~(MT_WMM_CWMIN_MASK << MT_WMM_CWMIN_SHIFT(qid));
	val |= cw_min << MT_WMM_CWMIN_SHIFT(qid);
	mt76_wr(dev, MT_WMM_CWMIN, val);

	val = mt76_rr(dev, MT_WMM_CWMAX);
	val &= ~(MT_WMM_CWMAX_MASK << MT_WMM_CWMAX_SHIFT(qid));
	val |= cw_max << MT_WMM_CWMAX_SHIFT(qid);
	mt76_wr(dev, MT_WMM_CWMAX, val);

	return 0;
}

static void
mt76x2_sw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       const u8 *mac)
{
	struct mt76x2_dev *dev = hw->priv;

	tasklet_disable(&dev->pre_tbtt_tasklet);
	set_bit(MT76_SCANNING, &dev->mt76.state);
}

static void
mt76x2_sw_scan_complete(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt76x2_dev *dev = hw->priv;

	clear_bit(MT76_SCANNING, &dev->mt76.state);
	tasklet_enable(&dev->pre_tbtt_tasklet);
	mt76_txq_schedule_all(&dev->mt76);
}

static void
mt76x2_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	     u32 queues, bool drop)
{
}

static int
mt76x2_get_txpower(struct ieee80211_hw *hw, struct ieee80211_vif *vif, int *dbm)
{
	struct mt76x2_dev *dev = hw->priv;

	*dbm = dev->txpower_cur / 2;

	/* convert from per-chain power to combined output on 2x2 devices */
	*dbm += 3;

	return 0;
}

static int
mt76x2_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params)
{
	enum ieee80211_ampdu_mlme_action action = params->action;
	struct ieee80211_sta *sta = params->sta;
	struct mt76x2_dev *dev = hw->priv;
	struct mt76x2_sta *msta = (struct mt76x2_sta *) sta->drv_priv;
	struct ieee80211_txq *txq = sta->txq[params->tid];
	u16 tid = params->tid;
	u16 *ssn = &params->ssn;
	struct mt76_txq *mtxq;

	if (!txq)
		return -EINVAL;

	mtxq = (struct mt76_txq *)txq->drv_priv;

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		mt76_rx_aggr_start(&dev->mt76, &msta->wcid, tid, *ssn, params->buf_size);
		mt76_set(dev, MT_WCID_ADDR(msta->wcid.idx) + 4, BIT(16 + tid));
		break;
	case IEEE80211_AMPDU_RX_STOP:
		mt76_rx_aggr_stop(&dev->mt76, &msta->wcid, tid);
		mt76_clear(dev, MT_WCID_ADDR(msta->wcid.idx) + 4,
			   BIT(16 + tid));
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		mtxq->aggr = true;
		mtxq->send_bar = false;
		ieee80211_send_bar(vif, sta->addr, tid, mtxq->agg_ssn);
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		mtxq->aggr = false;
		ieee80211_send_bar(vif, sta->addr, tid, mtxq->agg_ssn);
		break;
	case IEEE80211_AMPDU_TX_START:
		mtxq->agg_ssn = *ssn << 4;
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		mtxq->aggr = false;
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	}

	return 0;
}

static void
mt76x2_sta_rate_tbl_update(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct mt76x2_dev *dev = hw->priv;
	struct mt76x2_sta *msta = (struct mt76x2_sta *) sta->drv_priv;
	struct ieee80211_sta_rates *rates = rcu_dereference(sta->rates);
	struct ieee80211_tx_rate rate = {};

	if (!rates)
		return;

	rate.idx = rates->rate[0].idx;
	rate.flags = rates->rate[0].flags;
	mt76x2_mac_wcid_set_rate(dev, &msta->wcid, &rate);
	msta->wcid.max_txpwr_adj = mt76x2_tx_get_max_txpwr_adj(dev, &rate);
}

static void mt76x2_set_coverage_class(struct ieee80211_hw *hw,
				      s16 coverage_class)
{
	struct mt76x2_dev *dev = hw->priv;

	mutex_lock(&dev->mutex);
	dev->coverage_class = coverage_class;
	mt76x2_set_tx_ackto(dev);
	mutex_unlock(&dev->mutex);
}

static int
mt76x2_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set)
{
	return 0;
}

static int mt76x2_set_antenna(struct ieee80211_hw *hw, u32 tx_ant,
			      u32 rx_ant)
{
	struct mt76x2_dev *dev = hw->priv;

	if (!tx_ant || tx_ant > 3 || tx_ant != rx_ant)
		return -EINVAL;

	mutex_lock(&dev->mutex);

	dev->chainmask = (tx_ant == 3) ? 0x202 : 0x101;
	dev->mt76.antenna_mask = tx_ant;

	mt76_set_stream_caps(&dev->mt76, true);
	mt76x2_phy_set_antenna(dev);

	mutex_unlock(&dev->mutex);

	return 0;
}

static int mt76x2_get_antenna(struct ieee80211_hw *hw, u32 *tx_ant,
			      u32 *rx_ant)
{
	struct mt76x2_dev *dev = hw->priv;

	mutex_lock(&dev->mutex);
	*tx_ant = dev->mt76.antenna_mask;
	*rx_ant = dev->mt76.antenna_mask;
	mutex_unlock(&dev->mutex);

	return 0;
}

const struct ieee80211_ops mt76x2_ops = {
	.tx = mt76x2_tx,
	.start = mt76x2_start,
	.stop = mt76x2_stop,
	.add_interface = mt76x2_add_interface,
	.remove_interface = mt76x2_remove_interface,
	.config = mt76x2_config,
	.configure_filter = mt76x2_configure_filter,
	.bss_info_changed = mt76x2_bss_info_changed,
	.sta_add = mt76x2_sta_add,
	.sta_remove = mt76x2_sta_remove,
	.set_key = mt76x2_set_key,
	.conf_tx = mt76x2_conf_tx,
	.sw_scan_start = mt76x2_sw_scan,
	.sw_scan_complete = mt76x2_sw_scan_complete,
	.flush = mt76x2_flush,
	.ampdu_action = mt76x2_ampdu_action,
	.get_txpower = mt76x2_get_txpower,
	.wake_tx_queue = mt76_wake_tx_queue,
	.sta_rate_tbl_update = mt76x2_sta_rate_tbl_update,
	.release_buffered_frames = mt76_release_buffered_frames,
	.set_coverage_class = mt76x2_set_coverage_class,
	.get_survey = mt76_get_survey,
	.set_tim = mt76x2_set_tim,
	.set_antenna = mt76x2_set_antenna,
	.get_antenna = mt76x2_get_antenna,
};

