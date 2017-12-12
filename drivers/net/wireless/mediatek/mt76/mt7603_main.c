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

#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/module.h>
#include "mt7603.h"
#include "mt7603_eeprom.h"

static int
mt7603_start(struct ieee80211_hw *hw)
{
	struct mt7603_dev *dev = hw->priv;

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mac_work,
				     MT7603_WATCHDOG_TIME);
	mt7603_mac_start(dev);
	set_bit(MT76_STATE_RUNNING, &dev->mt76.state);

	return 0;
}

static void
mt7603_stop(struct ieee80211_hw *hw)
{
	struct mt7603_dev *dev = hw->priv;

	clear_bit(MT76_STATE_RUNNING, &dev->mt76.state);
	cancel_delayed_work_sync(&dev->mac_work);
	mt7603_mac_watchdog_reset(dev);
	mt7603_mac_stop(dev);
}

static void
mt7603_txq_init(struct mt7603_dev *dev, struct ieee80211_txq *txq)
{
	struct mt76_txq *mtxq;

	if (!txq)
		return;

	mtxq = (struct mt76_txq *) txq->drv_priv;
	if (txq->sta) {
		struct mt7603_sta *sta = (struct mt7603_sta *) txq->sta->drv_priv;
		mtxq->wcid = &sta->wcid;
	} else {
		struct mt7603_vif *mvif = (struct mt7603_vif *) txq->vif->drv_priv;
		mtxq->wcid = &mvif->sta.wcid;
	}

	mt76_txq_init(&dev->mt76, txq);
}

static int
mt7603_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7603_vif *mvif = (struct mt7603_vif *) vif->drv_priv;
	struct mt7603_dev *dev = hw->priv;
	u8 bc_addr[ETH_ALEN];
	int idx;
	int ret = 0;

	mutex_lock(&dev->mutex);

	mvif->idx = ffs(~dev->vif_mask) - 1;
	if (mvif->idx >= MT7603_MAX_INTERFACES) {
		ret = -ENOSPC;
		goto out;
	}

	mt76_wr(dev, MT_MAC_ADDR0(mvif->idx),
		get_unaligned_le32(vif->addr));
	mt76_wr(dev, MT_MAC_ADDR1(mvif->idx),
		(get_unaligned_le16(vif->addr + 4) |
		 MT_MAC_ADDR1_VALID));

	idx = MT7603_WTBL_RESERVED - 1 - mvif->idx;
	dev->vif_mask |= BIT(mvif->idx);
	mvif->sta.wcid.idx = idx;
	mvif->sta.wcid.hw_key_idx = -1;

	eth_broadcast_addr(bc_addr);
	mt7603_wtbl_init(dev, idx, mvif->idx, bc_addr);

	rcu_assign_pointer(dev->wcid[idx], &mvif->sta.wcid);
	mt7603_txq_init(dev, vif->txq);

out:
	mutex_unlock(&dev->mutex);

	return ret;
}

static void
mt7603_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7603_vif *mvif = (struct mt7603_vif *) vif->drv_priv;
	struct mt7603_dev *dev = hw->priv;
	int idx = mvif->sta.wcid.idx;

	mt7603_beacon_set_timer(dev, mvif->idx, 0);

	rcu_assign_pointer(dev->wcid[idx], NULL);
	mt76_txq_remove(&dev->mt76, vif->txq);

	mutex_lock(&dev->mutex);
	dev->vif_mask &= ~BIT(mvif->idx);
	mutex_unlock(&dev->mutex);
}

static int
mt7603_set_channel(struct mt7603_dev *dev, struct cfg80211_chan_def *def)
{
	u8 *rssi_data = (u8 *) dev->mt76.eeprom.data;
	int idx, ret;

	u8 bw = MT_BW_20;

	mt7603_mac_stop(dev);

	dev->mt76.chandef = *def;
	mt76_rmw_field(dev, MT_AGG_BWCR, MT_AGG_BWCR_BW, bw);
	ret = mt7603_mcu_set_channel(dev);
	if (ret)
		return ret;

	if (def->chan->band == NL80211_BAND_5GHZ) {
		idx = 1;
		rssi_data += MT_EE_RSSI_OFFSET_5G;
	} else {
		idx = 0;
		rssi_data += MT_EE_RSSI_OFFSET_2G;
	}

	memcpy(dev->rssi_offset, rssi_data, sizeof(dev->rssi_offset));

	idx |= (def->chan - mt76_hw(dev)->wiphy->bands[def->chan->band]->channels) << 1;
	mt76_wr(dev, MT_WF_RMAC_CH_FREQ, idx);
	mt7603_mac_start(dev);

	return 0;
}

static int
mt7603_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt7603_dev *dev = hw->priv;
	int ret = 0;

	mutex_lock(&dev->mutex);

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ret = mt7603_set_channel(dev, &hw->conf.chandef);
		mt7603_mac_set_timing(dev);
	}

	mutex_unlock(&dev->mutex);

	return ret;
}

static void
mt7603_configure_filter(struct ieee80211_hw *hw, unsigned int changed_flags,
		      unsigned int *total_flags, u64 multicast)
{
	struct mt7603_dev *dev = hw->priv;
	u32 flags = 0;

#define MT76_FILTER(_flag, _hw) do { \
		flags |= *total_flags & FIF_##_flag;			\
		dev->rxfilter &= ~(_hw);				\
		dev->rxfilter |= !(flags & FIF_##_flag) * (_hw);	\
	} while (0)

	dev->rxfilter &= ~(MT_WF_RFCR_DROP_OTHER_BSS |
			   MT_WF_RFCR_DROP_OTHER_BEACON |
			   MT_WF_RFCR_DROP_FRAME_REPORT |
			   MT_WF_RFCR_DROP_PROBEREQ |
			   MT_WF_RFCR_DROP_MCAST_FILTERED |
			   MT_WF_RFCR_DROP_MCAST |
			   MT_WF_RFCR_DROP_BCAST |
			   MT_WF_RFCR_DROP_DUPLICATE |
			   MT_WF_RFCR_DROP_A2_BSSID |
			   MT_WF_RFCR_DROP_UNWANTED_CTL |
			   MT_WF_RFCR_DROP_STBC_MULTI);

	MT76_FILTER(OTHER_BSS, MT_WF_RFCR_DROP_OTHER_UC |
			       MT_WF_RFCR_DROP_OTHER_TIM |
			       MT_WF_RFCR_DROP_A3_MAC |
			       MT_WF_RFCR_DROP_A3_BSSID);

	MT76_FILTER(FCSFAIL, MT_WF_RFCR_DROP_FCSFAIL);

	MT76_FILTER(CONTROL, MT_WF_RFCR_DROP_CTS |
			     MT_WF_RFCR_DROP_RTS |
			     MT_WF_RFCR_DROP_CTL_RSV |
			     MT_WF_RFCR_DROP_NDPA);

	*total_flags = flags;
	mt76_wr(dev, MT_WF_RFCR, dev->rxfilter);
}

static void
mt7603_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *info, u32 changed)
{
	struct mt7603_dev *dev = hw->priv;
	struct mt7603_vif *mvif = (struct mt7603_vif *) vif->drv_priv;

	mutex_lock(&dev->mutex);

	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_BSSID)) {
		if (info->assoc || info->ibss_joined) {
			mt76_wr(dev, MT_BSSID0(mvif->idx),
				get_unaligned_le32(info->bssid));
			mt76_wr(dev, MT_BSSID1(mvif->idx),
				(get_unaligned_le16(info->bssid + 4) |
				 MT_BSSID1_VALID));
		} else {
			mt76_wr(dev, MT_BSSID0(mvif->idx), 0);
			mt76_wr(dev, MT_BSSID1(mvif->idx), 0);
		}
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		dev->slottime = info->use_short_slot ? 9 : 20;
		mt7603_mac_set_timing(dev);
	}

	if (changed & (BSS_CHANGED_BEACON_ENABLED | BSS_CHANGED_BEACON_INT)) {
		int beacon_int = !!info->enable_beacon * info->beacon_int;
		tasklet_disable(&dev->pre_tbtt_tasklet);
		mt7603_beacon_set_timer(dev, mvif->idx, beacon_int);
		tasklet_enable(&dev->pre_tbtt_tasklet);
	}

	mutex_unlock(&dev->mutex);
}

static int
mt7603_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	     struct ieee80211_sta *sta)
{
	struct mt7603_dev *dev = hw->priv;
	struct mt7603_sta *msta = (struct mt7603_sta *) sta->drv_priv;
	struct mt7603_vif *mvif = (struct mt7603_vif *) vif->drv_priv;
	int i, idx;
	int ret = 0;

	mutex_lock(&dev->mutex);

	idx = mt76_wcid_alloc(dev->wcid_mask, MT7603_WTBL_STA - 1);
	if (idx < 0) {
		ret = -ENOSPC;
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		mt7603_txq_init(dev, sta->txq[i]);

	msta->wcid.idx = idx;
	mt7603_wtbl_init(dev, idx, mvif->idx, sta->addr);
	mt7603_wtbl_update_cap(dev, sta);

	rcu_assign_pointer(dev->wcid[idx], &msta->wcid);

out:
	mutex_unlock(&dev->mutex);

	return ret;
}

static int
mt7603_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_sta *sta)
{
	struct mt7603_dev *dev = hw->priv;
	struct mt7603_sta *msta = (struct mt7603_sta *) sta->drv_priv;
	int idx = msta->wcid.idx;
	int i;

	mutex_lock(&dev->mutex);
	rcu_assign_pointer(dev->wcid[idx], NULL);
	mt7603_wtbl_clear(dev, idx);

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		mt76_txq_remove(&dev->mt76, sta->txq[i]);

	mt76_wcid_free(dev->wcid_mask, idx);

	mutex_unlock(&dev->mutex);

	return 0;
}

static void
mt7603_sta_notify(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		enum sta_notify_cmd cmd, struct ieee80211_sta *sta)
{
	struct mt7603_dev *dev = hw->priv;
	struct mt7603_sta *msta = (struct mt7603_sta *) sta->drv_priv;
	int idx = msta->wcid.idx;

	switch (cmd) {
	case STA_NOTIFY_SLEEP:
		mt7603_wtbl_set_ps(dev, idx, true);
		mt76_stop_tx_queues(&dev->mt76, sta, false);
		break;
	case STA_NOTIFY_AWAKE:
		mt7603_wtbl_set_ps(dev, idx, false);
		break;
	}
}

static int
mt7603_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
	     struct ieee80211_vif *vif, struct ieee80211_sta *sta,
	     struct ieee80211_key_conf *key)
{
	struct mt7603_dev *dev = hw->priv;
	struct mt7603_vif *mvif = (struct mt7603_vif *) vif->drv_priv;
	struct mt7603_sta *msta = sta ? (struct mt7603_sta *) sta->drv_priv : NULL;
	struct mt76_wcid *wcid = msta ? &msta->wcid : &mvif->sta.wcid;
	int idx = key->keyidx;

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

	if (cmd == SET_KEY) {
		key->hw_key_idx = wcid->idx;
		wcid->hw_key_idx = idx;
	} else {
		if (idx == wcid->hw_key_idx)
			wcid->hw_key_idx = -1;

		key = NULL;
	}

	return mt7603_wtbl_set_key(dev, wcid->idx, key);
}

static int
mt7603_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u16 queue,
	     const struct ieee80211_tx_queue_params *params)
{
	struct mt7603_dev *dev = hw->priv;
	u16 cw_min = (1 << 5) - 1;
	u16 cw_max = (1 << 10) - 1;
	u32 val;

	queue = dev->mt76.q_tx[queue].hw_idx;

	if (params->cw_min)
		cw_min = params->cw_min;
	if (params->cw_max)
		cw_max = params->cw_max;

	mutex_lock(&dev->mutex);
	mt7603_mac_stop(dev);

	val = mt76_rr(dev, MT_WMM_TXOP(queue));
	val &= ~(MT_WMM_TXOP_MASK << MT_WMM_TXOP_SHIFT(queue));
	val |= params->txop << MT_WMM_TXOP_SHIFT(queue);
	mt76_wr(dev, MT_WMM_TXOP(queue), val);

	val = mt76_rr(dev, MT_WMM_AIFSN);
	val &= ~(MT_WMM_AIFSN_MASK << MT_WMM_AIFSN_SHIFT(queue));
	val |= params->aifs << MT_WMM_AIFSN_SHIFT(queue);
	mt76_wr(dev, MT_WMM_AIFSN, val);

	val = mt76_rr(dev, MT_WMM_CWMIN);
	val &= ~(MT_WMM_CWMIN_MASK << MT_WMM_CWMIN_SHIFT(queue));
	val |= cw_min << MT_WMM_CWMIN_SHIFT(queue);
	mt76_wr(dev, MT_WMM_CWMIN, val);

	val = mt76_rr(dev, MT_WMM_CWMAX(queue));
	val &= ~(MT_WMM_CWMAX_MASK << MT_WMM_CWMAX_SHIFT(queue));
	val |= cw_max << MT_WMM_CWMAX_SHIFT(queue);
	mt76_wr(dev, MT_WMM_CWMAX(queue), val);

	mt7603_mac_start(dev);
	mutex_unlock(&dev->mutex);

	return 0;
}

static void
mt7603_sw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif, const u8 *mac)
{
	struct mt7603_dev *dev = hw->priv;

	set_bit(MT76_SCANNING, &dev->mt76.state);
	mt7603_beacon_set_timer(dev, -1, 0);
}

static void
mt7603_sw_scan_complete(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7603_dev *dev = hw->priv;

	clear_bit(MT76_SCANNING, &dev->mt76.state);
	mt7603_beacon_set_timer(dev, -1, dev->beacon_int);
	mt76_txq_schedule_all(&dev->mt76);
}

static void
mt7603_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	   u32 queues, bool drop)
{
}

static int
mt7603_get_txpower(struct ieee80211_hw *hw, struct ieee80211_vif *vif, int *dbm)
{
	return -EINVAL;
}

static int
mt7603_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params)
{
	enum ieee80211_ampdu_mlme_action action = params->action;
	struct mt7603_dev *dev = hw->priv;
	struct ieee80211_sta *sta = params->sta;
	struct ieee80211_txq *txq = sta->txq[params->tid];
	struct mt7603_sta *msta = (struct mt7603_sta *) sta->drv_priv;
	struct mt76_txq *mtxq = (struct mt76_txq *) txq->drv_priv;
	u16 tid = params->tid;
	u16 *ssn = &params->ssn;
	u8 ba_size = params->buf_size;

	if (!txq)
		return -EINVAL;

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		mt7603_mac_rx_ba_reset(dev, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		mtxq->aggr = true;
		mtxq->send_bar = false;
		mt7603_mac_tx_ba_reset(dev, msta->wcid.idx, tid, *ssn, ba_size);
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		mtxq->aggr = false;
		ieee80211_send_bar(vif, sta->addr, tid, mtxq->agg_ssn);
		mt7603_mac_tx_ba_reset(dev, msta->wcid.idx, tid, *ssn, -1);
		break;
	case IEEE80211_AMPDU_TX_START:
		mtxq->agg_ssn = *ssn << 4;
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		mtxq->aggr = false;
		mt7603_mac_tx_ba_reset(dev, msta->wcid.idx, tid, *ssn, -1);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	}

	return 0;
}

static void
mt7603_sta_rate_tbl_update(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta)
{
	struct mt7603_dev *dev = hw->priv;
	struct mt7603_sta *msta = (struct mt7603_sta *) sta->drv_priv;
	struct ieee80211_sta_rates *sta_rates = rcu_dereference(sta->rates);
	int i;

	spin_lock_bh(&dev->mt76.lock);
	for (i = 0; i < ARRAY_SIZE(msta->rates); i++) {
		msta->rates[i].idx = sta_rates->rate[i].idx;
		msta->rates[i].count = sta_rates->rate[i].count;
		msta->rates[i].flags = sta_rates->rate[i].flags;

		if (msta->rates[i].idx < 0 || !msta->rates[i].count)
			break;
	}
	msta->n_rates = i;
	mt7603_wtbl_set_rates(dev, msta, NULL, msta->rates);
	msta->rate_probe = false;
	spin_unlock_bh(&dev->mt76.lock);
}

static void mt7603_set_coverage_class(struct ieee80211_hw *hw,
				    s16 coverage_class)
{
	struct mt7603_dev *dev = hw->priv;
	dev->coverage_class = coverage_class;
	mt7603_mac_set_timing(dev);
}

static void mt7603_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct mt7603_dev *dev = hw->priv;
	struct mt76_wcid *wcid = &dev->global_sta.wcid;

	mt76_tx(&dev->mt76, control->sta, wcid, skb);
}

const struct ieee80211_ops mt7603_ops = {
	.tx = mt7603_tx,
	.start = mt7603_start,
	.stop = mt7603_stop,
	.add_interface = mt7603_add_interface,
	.remove_interface = mt7603_remove_interface,
	.config = mt7603_config,
	.configure_filter = mt7603_configure_filter,
	.bss_info_changed = mt7603_bss_info_changed,
	.sta_add = mt7603_sta_add,
	.sta_remove = mt7603_sta_remove,
	.sta_notify = mt7603_sta_notify,
	.set_key = mt7603_set_key,
	.conf_tx = mt7603_conf_tx,
	.sw_scan_start = mt7603_sw_scan,
	.sw_scan_complete = mt7603_sw_scan_complete,
	.flush = mt7603_flush,
	.ampdu_action = mt7603_ampdu_action,
	.get_txpower = mt7603_get_txpower,
	.wake_tx_queue = mt76_wake_tx_queue,
	.sta_rate_tbl_update = mt7603_sta_rate_tbl_update,
	.release_buffered_frames = mt76_release_buffered_frames,
	.set_coverage_class = mt7603_set_coverage_class,
};

MODULE_LICENSE("Dual BSD/GPL");

static int __init mt7603_init(void)
{
	int ret;

	ret = platform_driver_register(&mt76_wmac_driver);
	if (ret)
		return ret;

#ifdef CONFIG_PCI
	ret = pci_register_driver(&mt7603_pci_driver);
	if (ret)
		platform_driver_unregister(&mt76_wmac_driver);
#endif
	return ret;
}

static void __exit mt7603_exit(void)
{
#ifdef CONFIG_PCI
	pci_unregister_driver(&mt7603_pci_driver);
#endif
	platform_driver_unregister(&mt76_wmac_driver);
}

module_init(mt7603_init);
module_exit(mt7603_exit);
