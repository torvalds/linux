// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Roy Luo <royluo@google.com>
 *         Ryder Lee <ryder.lee@mediatek.com>
 *         Felix Fietkau <nbd@nbd.name>
 *         Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/etherdevice.h>
#include <linux/module.h>
#include "mt7615.h"
#include "mcu.h"

static bool mt7615_dev_running(struct mt7615_dev *dev)
{
	struct mt7615_phy *phy;

	if (test_bit(MT76_STATE_RUNNING, &dev->mphy.state))
		return true;

	phy = mt7615_ext_phy(dev);

	return phy && test_bit(MT76_STATE_RUNNING, &phy->mt76->state);
}

static int mt7615_start(struct ieee80211_hw *hw)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	bool running;

	if (!mt7615_wait_for_mcu_init(dev))
		return -EIO;

	mutex_lock(&dev->mt76.mutex);

	running = mt7615_dev_running(dev);

	if (!running) {
		mt7615_mcu_set_pm(dev, 0, 0);
		mt7615_mcu_set_mac_enable(dev, 0, true);
		mt7615_mac_enable_nf(dev, 0);
	}

	if (phy != &dev->phy) {
		mt7615_mcu_set_pm(dev, 1, 0);
		mt7615_mcu_set_mac_enable(dev, 1, true);
		mt7615_mac_enable_nf(dev, 1);
	}

	mt7615_mcu_set_channel_domain(phy);
	mt7615_mcu_set_chan_info(phy, MCU_EXT_CMD_SET_RX_PATH);

	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	ieee80211_queue_delayed_work(hw, &phy->mac_work,
				     MT7615_WATCHDOG_TIME);

	if (!running)
		mt7615_mac_reset_counters(dev);

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static void mt7615_stop(struct ieee80211_hw *hw)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);

	cancel_delayed_work_sync(&phy->mac_work);
	del_timer_sync(&phy->roc_timer);
	cancel_work_sync(&phy->roc_work);

	mutex_lock(&dev->mt76.mutex);

	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	cancel_delayed_work_sync(&phy->scan_work);

	if (phy != &dev->phy) {
		mt7615_mcu_set_pm(dev, 1, 1);
		mt7615_mcu_set_mac_enable(dev, 1, false);
	}

	if (!mt7615_dev_running(dev)) {
		mt7615_mcu_set_pm(dev, 0, 1);
		mt7615_mcu_set_mac_enable(dev, 0, false);
	}

	mutex_unlock(&dev->mt76.mutex);
}

static int get_omac_idx(enum nl80211_iftype type, u32 mask)
{
	int i;

	switch (type) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_ADHOC:
		/* ap use hw bssid 0 and ext bssid */
		if (~mask & BIT(HW_BSSID_0))
			return HW_BSSID_0;

		for (i = EXT_BSSID_1; i < EXT_BSSID_END; i++)
			if (~mask & BIT(i))
				return i;

		break;
	case NL80211_IFTYPE_STATION:
		/* sta use hw bssid other than 0 */
		for (i = HW_BSSID_1; i < HW_BSSID_MAX; i++)
			if (~mask & BIT(i))
				return i;

		break;
	default:
		WARN_ON(1);
		break;
	}

	return -1;
}

static int mt7615_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	struct mt76_txq *mtxq;
	bool ext_phy = phy != &dev->phy;
	int idx, ret = 0;

	mutex_lock(&dev->mt76.mutex);

	mvif->idx = ffs(~dev->vif_mask) - 1;
	if (mvif->idx >= MT7615_MAX_INTERFACES) {
		ret = -ENOSPC;
		goto out;
	}

	idx = get_omac_idx(vif->type, dev->omac_mask);
	if (idx < 0) {
		ret = -ENOSPC;
		goto out;
	}
	mvif->omac_idx = idx;

	mvif->band_idx = ext_phy;
	if (mt7615_ext_phy(dev))
		mvif->wmm_idx = ext_phy * (MT7615_MAX_WMM_SETS / 2) +
				mvif->idx % (MT7615_MAX_WMM_SETS / 2);
	else
		mvif->wmm_idx = mvif->idx % MT7615_MAX_WMM_SETS;

	dev->vif_mask |= BIT(mvif->idx);
	dev->omac_mask |= BIT(mvif->omac_idx);
	phy->omac_mask |= BIT(mvif->omac_idx);

	mt7615_mcu_set_dbdc(dev);

	idx = MT7615_WTBL_RESERVED - mvif->idx;

	INIT_LIST_HEAD(&mvif->sta.poll_list);
	mvif->sta.wcid.idx = idx;
	mvif->sta.wcid.ext_phy = mvif->band_idx;
	mvif->sta.wcid.hw_key_idx = -1;
	mt7615_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	rcu_assign_pointer(dev->mt76.wcid[idx], &mvif->sta.wcid);
	if (vif->txq) {
		mtxq = (struct mt76_txq *)vif->txq->drv_priv;
		mtxq->wcid = &mvif->sta.wcid;
		mt76_txq_init(&dev->mt76, vif->txq);
	}

	ret = mt7615_mcu_add_dev_info(dev, vif, true);
out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void mt7615_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_sta *msta = &mvif->sta;
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	int idx = msta->wcid.idx;

	/* TODO: disable beacon for the bss */

	mt7615_mcu_add_dev_info(dev, vif, false);

	rcu_assign_pointer(dev->mt76.wcid[idx], NULL);
	if (vif->txq)
		mt76_txq_remove(&dev->mt76, vif->txq);

	mutex_lock(&dev->mt76.mutex);
	dev->vif_mask &= ~BIT(mvif->idx);
	dev->omac_mask &= ~BIT(mvif->omac_idx);
	phy->omac_mask &= ~BIT(mvif->omac_idx);
	mutex_unlock(&dev->mt76.mutex);

	spin_lock_bh(&dev->sta_poll_lock);
	if (!list_empty(&msta->poll_list))
		list_del_init(&msta->poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);
}

static void mt7615_init_dfs_state(struct mt7615_phy *phy)
{
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_hw *hw = mphy->hw;
	struct cfg80211_chan_def *chandef = &hw->conf.chandef;

	if (hw->conf.flags & IEEE80211_CONF_OFFCHANNEL)
		return;

	if (!(chandef->chan->flags & IEEE80211_CHAN_RADAR))
		return;

	if (mphy->chandef.chan->center_freq == chandef->chan->center_freq &&
	    mphy->chandef.width == chandef->width)
		return;

	phy->dfs_state = -1;
}

static int mt7615_set_channel(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	int ret;

	cancel_delayed_work_sync(&phy->mac_work);

	mutex_lock(&dev->mt76.mutex);
	set_bit(MT76_RESET, &phy->mt76->state);

	mt7615_init_dfs_state(phy);
	mt76_set_channel(phy->mt76);

	if (is_mt7615(&dev->mt76) && dev->flash_eeprom) {
		mt7615_mcu_apply_rx_dcoc(phy);
		mt7615_mcu_apply_tx_dpd(phy);
	}

	ret = mt7615_mcu_set_chan_info(phy, MCU_EXT_CMD_CHANNEL_SWITCH);
	if (ret)
		goto out;

	mt7615_mac_set_timing(phy);
	ret = mt7615_dfs_init_radar_detector(phy);
	mt7615_mac_cca_stats_reset(phy);
	mt7615_mcu_set_sku_en(phy, true);

	mt7615_mac_reset_counters(dev);
	phy->noise = 0;
	phy->chfreq = mt76_rr(dev, MT_CHFREQ(ext_phy));

out:
	clear_bit(MT76_RESET, &phy->mt76->state);
	mutex_unlock(&dev->mt76.mutex);

	mt76_txq_schedule_all(phy->mt76);
	ieee80211_queue_delayed_work(phy->mt76->hw, &phy->mac_work,
				     MT7615_WATCHDOG_TIME);
	return ret;
}

static int
mt7615_queue_key_update(struct mt7615_dev *dev, enum set_key_cmd cmd,
			struct mt7615_sta *msta,
			struct ieee80211_key_conf *key)
{
	struct mt7615_wtbl_desc *wd;

	wd = kzalloc(sizeof(*wd), GFP_KERNEL);
	if (!wd)
		return -ENOMEM;

	wd->type = MT7615_WTBL_KEY_DESC;
	wd->sta = msta;

	wd->key.key = kmemdup(key->key, key->keylen, GFP_KERNEL);
	if (!wd->key.key) {
		kfree(wd);
		return -ENOMEM;
	}
	wd->key.cipher = key->cipher;
	wd->key.keyidx = key->keyidx;
	wd->key.keylen = key->keylen;
	wd->key.cmd = cmd;

	list_add_tail(&wd->node, &dev->wd_head);
	queue_work(dev->mt76.usb.wq, &dev->wtbl_work);

	return 0;
}

static int mt7615_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_sta *msta = sta ? (struct mt7615_sta *)sta->drv_priv :
				  &mvif->sta;
	struct mt76_wcid *wcid = &msta->wcid;
	int idx = key->keyidx;

	/* The hardware does not support per-STA RX GTK, fallback
	 * to software mode for these.
	 */
	if ((vif->type == NL80211_IFTYPE_ADHOC ||
	     vif->type == NL80211_IFTYPE_MESH_POINT) &&
	    (key->cipher == WLAN_CIPHER_SUITE_TKIP ||
	     key->cipher == WLAN_CIPHER_SUITE_CCMP) &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return -EOPNOTSUPP;

	/* fall back to sw encryption for unsupported ciphers */
	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_AES_CMAC:
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIE;
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
	case WLAN_CIPHER_SUITE_SMS4:
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (cmd == SET_KEY) {
		key->hw_key_idx = wcid->idx;
		wcid->hw_key_idx = idx;
	} else if (idx == wcid->hw_key_idx) {
		wcid->hw_key_idx = -1;
	}
	mt76_wcid_key_setup(&dev->mt76, wcid,
			    cmd == SET_KEY ? key : NULL);

	if (mt76_is_usb(&dev->mt76))
		return mt7615_queue_key_update(dev, cmd, msta, key);

	return mt7615_mac_wtbl_set_key(dev, wcid, key, cmd);
}

static int mt7615_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	bool band = phy != &dev->phy;
	int ret = 0;

	if (changed & (IEEE80211_CONF_CHANGE_CHANNEL |
		       IEEE80211_CONF_CHANGE_POWER)) {
		ieee80211_stop_queues(hw);
		ret = mt7615_set_channel(phy);
		ieee80211_wake_queues(hw);
	}

	mutex_lock(&dev->mt76.mutex);

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		if (!(hw->conf.flags & IEEE80211_CONF_MONITOR))
			phy->rxfilter |= MT_WF_RFCR_DROP_OTHER_UC;
		else
			phy->rxfilter &= ~MT_WF_RFCR_DROP_OTHER_UC;

		mt76_wr(dev, MT_WF_RFCR(band), phy->rxfilter);
	}

	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7615_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u16 queue,
	       const struct ieee80211_tx_queue_params *params)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = mt7615_hw_dev(hw);

	queue = mt7615_lmac_mapping(dev, queue);
	queue += mvif->wmm_idx * MT7615_MAX_WMM_SETS;

	return mt7615_mcu_set_wmm(dev, queue, params);
}

static void mt7615_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	bool band = phy != &dev->phy;

	u32 ctl_flags = MT_WF_RFCR1_DROP_ACK |
			MT_WF_RFCR1_DROP_BF_POLL |
			MT_WF_RFCR1_DROP_BA |
			MT_WF_RFCR1_DROP_CFEND |
			MT_WF_RFCR1_DROP_CFACK;
	u32 flags = 0;

#define MT76_FILTER(_flag, _hw) do { \
		flags |= *total_flags & FIF_##_flag;			\
		phy->rxfilter &= ~(_hw);				\
		phy->rxfilter |= !(flags & FIF_##_flag) * (_hw);	\
	} while (0)

	phy->rxfilter &= ~(MT_WF_RFCR_DROP_OTHER_BSS |
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

	MT76_FILTER(OTHER_BSS, MT_WF_RFCR_DROP_OTHER_TIM |
			       MT_WF_RFCR_DROP_A3_MAC |
			       MT_WF_RFCR_DROP_A3_BSSID);

	MT76_FILTER(FCSFAIL, MT_WF_RFCR_DROP_FCSFAIL);

	MT76_FILTER(CONTROL, MT_WF_RFCR_DROP_CTS |
			     MT_WF_RFCR_DROP_RTS |
			     MT_WF_RFCR_DROP_CTL_RSV |
			     MT_WF_RFCR_DROP_NDPA);

	*total_flags = flags;
	mt76_wr(dev, MT_WF_RFCR(band), phy->rxfilter);

	if (*total_flags & FIF_CONTROL)
		mt76_clear(dev, MT_WF_RFCR1(band), ctl_flags);
	else
		mt76_set(dev, MT_WF_RFCR1(band), ctl_flags);
}

static void mt7615_bss_info_changed(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *info,
				    u32 changed)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);

	mutex_lock(&dev->mt76.mutex);

	if (changed & BSS_CHANGED_ERP_SLOT) {
		int slottime = info->use_short_slot ? 9 : 20;

		if (slottime != phy->slottime) {
			phy->slottime = slottime;
			mt7615_mac_set_timing(phy);
		}
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		mt7615_mcu_add_bss_info(phy, vif, NULL, info->enable_beacon);
		mt7615_mcu_sta_add(dev, vif, NULL, info->enable_beacon);

		if (vif->p2p && info->enable_beacon)
			mt7615_mcu_set_p2p_oppps(hw, vif);
	}

	if (changed & (BSS_CHANGED_BEACON |
		       BSS_CHANGED_BEACON_ENABLED))
		mt7615_mcu_add_beacon(dev, hw, vif, info->enable_beacon);

	if (changed & BSS_CHANGED_PS)
		mt7615_mcu_set_vif_ps(dev, vif);

	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7615_channel_switch_beacon(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct cfg80211_chan_def *chandef)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);
	mt7615_mcu_add_beacon(dev, hw, vif, true);
	mutex_unlock(&dev->mt76.mutex);
}

int mt7615_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	int idx;

	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7615_WTBL_STA - 1);
	if (idx < 0)
		return -ENOSPC;

	INIT_LIST_HEAD(&msta->poll_list);
	msta->vif = mvif;
	msta->wcid.sta = 1;
	msta->wcid.idx = idx;
	msta->wcid.ext_phy = mvif->band_idx;

	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls) {
		struct mt7615_phy *phy;

		phy = mvif->band_idx ? mt7615_ext_phy(dev) : &dev->phy;
		mt7615_mcu_add_bss_info(phy, vif, sta, true);
	}
	mt7615_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
	mt7615_mcu_sta_add(dev, vif, sta, true);

	return 0;
}
EXPORT_SYMBOL_GPL(mt7615_mac_sta_add);

void mt7615_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;

	mt7615_mcu_sta_add(dev, vif, sta, false);
	mt7615_mac_wtbl_update(dev, msta->wcid.idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls) {
		struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
		struct mt7615_phy *phy;

		phy = mvif->band_idx ? mt7615_ext_phy(dev) : &dev->phy;
		mt7615_mcu_add_bss_info(phy, vif, sta, false);
	}

	spin_lock_bh(&dev->sta_poll_lock);
	if (!list_empty(&msta->poll_list))
		list_del_init(&msta->poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);
}
EXPORT_SYMBOL_GPL(mt7615_mac_sta_remove);

static void mt7615_sta_rate_tbl_update(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
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
	mt7615_mac_set_rates(phy, msta, NULL, msta->rates);
	msta->rate_probe = false;
	spin_unlock_bh(&dev->mt76.lock);
}

static void mt7615_tx(struct ieee80211_hw *hw,
		      struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;

	if (control->sta) {
		struct mt7615_sta *sta;

		sta = (struct mt7615_sta *)control->sta->drv_priv;
		wcid = &sta->wcid;
	}

	if (vif && !control->sta) {
		struct mt7615_vif *mvif;

		mvif = (struct mt7615_vif *)vif->drv_priv;
		wcid = &mvif->sta.wcid;
	}

	mt76_tx(mphy, control->sta, wcid, skb);
}

static int mt7615_set_rts_threshold(struct ieee80211_hw *hw, u32 val)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);

	mutex_lock(&dev->mt76.mutex);
	mt7615_mcu_set_rts_thresh(phy, val);
	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static int
mt7615_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params)
{
	enum ieee80211_ampdu_mlme_action action = params->action;
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct ieee80211_sta *sta = params->sta;
	struct ieee80211_txq *txq = sta->txq[params->tid];
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
	u16 tid = params->tid;
	u16 ssn = params->ssn;
	struct mt76_txq *mtxq;
	int ret = 0;

	if (!txq)
		return -EINVAL;

	mtxq = (struct mt76_txq *)txq->drv_priv;

	mutex_lock(&dev->mt76.mutex);
	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		mt76_rx_aggr_start(&dev->mt76, &msta->wcid, tid, ssn,
				   params->buf_size);
		mt7615_mcu_add_rx_ba(dev, params, true);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		mt76_rx_aggr_stop(&dev->mt76, &msta->wcid, tid);
		mt7615_mcu_add_rx_ba(dev, params, false);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		mtxq->aggr = true;
		mtxq->send_bar = false;
		mt7615_mcu_add_tx_ba(dev, params, true);
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		mtxq->aggr = false;
		mt7615_mcu_add_tx_ba(dev, params, false);
		break;
	case IEEE80211_AMPDU_TX_START:
		mtxq->agg_ssn = IEEE80211_SN_TO_SEQ(ssn);
		ret = IEEE80211_AMPDU_TX_START_IMMEDIATE;
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		mtxq->aggr = false;
		mt7615_mcu_add_tx_ba(dev, params, false);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	}
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7615_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       struct ieee80211_sta *sta)
{
    return mt76_sta_state(hw, vif, sta, IEEE80211_STA_NOTEXIST,
			  IEEE80211_STA_NONE);
}

static int
mt7615_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  struct ieee80211_sta *sta)
{
    return mt76_sta_state(hw, vif, sta, IEEE80211_STA_NONE,
			  IEEE80211_STA_NOTEXIST);
}

static int
mt7615_get_stats(struct ieee80211_hw *hw,
		 struct ieee80211_low_level_stats *stats)
{
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	struct mib_stats *mib = &phy->mib;

	stats->dot11RTSSuccessCount = mib->rts_cnt;
	stats->dot11RTSFailureCount = mib->rts_retries_cnt;
	stats->dot11FCSErrorCount = mib->fcs_err_cnt;
	stats->dot11ACKFailureCount = mib->ack_fail_cnt;

	return 0;
}

static u64
mt7615_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	union {
		u64 t64;
		u32 t32[2];
	} tsf;

	mutex_lock(&dev->mt76.mutex);

	mt76_set(dev, MT_LPON_T0CR, MT_LPON_T0CR_MODE); /* TSF read */
	tsf.t32[0] = mt76_rr(dev, MT_LPON_UTTR0);
	tsf.t32[1] = mt76_rr(dev, MT_LPON_UTTR1);

	mutex_unlock(&dev->mt76.mutex);

	return tsf.t64;
}

static void
mt7615_set_coverage_class(struct ieee80211_hw *hw, s16 coverage_class)
{
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	struct mt7615_dev *dev = phy->dev;

	mutex_lock(&dev->mt76.mutex);
	phy->coverage_class = max_t(s16, coverage_class, 0);
	mt7615_mac_set_timing(phy);
	mutex_unlock(&dev->mt76.mutex);
}

static int
mt7615_set_antenna(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	int max_nss = hweight8(hw->wiphy->available_antennas_tx);
	bool ext_phy = phy != &dev->phy;

	if (!tx_ant || tx_ant != rx_ant || ffs(tx_ant) > max_nss)
		return -EINVAL;

	if ((BIT(hweight8(tx_ant)) - 1) != tx_ant)
		tx_ant = BIT(ffs(tx_ant) - 1) - 1;

	mutex_lock(&dev->mt76.mutex);

	phy->mt76->antenna_mask = tx_ant;
	if (ext_phy) {
		if (dev->chainmask == 0xf)
			tx_ant <<= 2;
		else
			tx_ant <<= 1;
	}
	phy->chainmask = tx_ant;

	mt76_set_stream_caps(phy->mt76, true);

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static void mt7615_roc_iter(void *priv, u8 *mac,
			    struct ieee80211_vif *vif)
{
	struct mt7615_phy *phy = priv;

	mt7615_mcu_set_roc(phy, vif, NULL, 0);
}

void mt7615_roc_work(struct work_struct *work)
{
	struct mt7615_phy *phy;

	phy = (struct mt7615_phy *)container_of(work, struct mt7615_phy,
						roc_work);

	if (!test_and_clear_bit(MT76_STATE_ROC, &phy->mt76->state))
		return;

	ieee80211_iterate_active_interfaces(phy->mt76->hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7615_roc_iter, phy);
	ieee80211_remain_on_channel_expired(phy->mt76->hw);
}

void mt7615_roc_timer(struct timer_list *timer)
{
	struct mt7615_phy *phy = from_timer(phy, timer, roc_timer);

	ieee80211_queue_work(phy->mt76->hw, &phy->roc_work);
}

void mt7615_scan_work(struct work_struct *work)
{
	struct mt7615_phy *phy;

	phy = (struct mt7615_phy *)container_of(work, struct mt7615_phy,
						scan_work.work);

	while (true) {
		struct mt7615_mcu_rxd *rxd;
		struct sk_buff *skb;

		spin_lock_bh(&phy->dev->mt76.lock);
		skb = __skb_dequeue(&phy->scan_event_list);
		spin_unlock_bh(&phy->dev->mt76.lock);

		if (!skb)
			break;

		rxd = (struct mt7615_mcu_rxd *)skb->data;
		if (rxd->eid == MCU_EVENT_SCHED_SCAN_DONE) {
			ieee80211_sched_scan_results(phy->mt76->hw);
		} else if (test_and_clear_bit(MT76_HW_SCANNING,
					      &phy->mt76->state)) {
			struct cfg80211_scan_info info = {
				.aborted = false,
			};

			ieee80211_scan_completed(phy->mt76->hw, &info);
		}
		dev_kfree_skb(skb);
	}
}

static int
mt7615_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       struct ieee80211_scan_request *req)
{
	struct mt76_phy *mphy = hw->priv;

	return mt7615_mcu_hw_scan(mphy->priv, vif, req);
}

static void
mt7615_cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt76_phy *mphy = hw->priv;

	mt7615_mcu_cancel_hw_scan(mphy->priv, vif);
}

static int
mt7615_start_sched_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct cfg80211_sched_scan_request *req,
			struct ieee80211_scan_ies *ies)
{
	struct mt76_phy *mphy = hw->priv;
	int err;

	err = mt7615_mcu_sched_scan_req(mphy->priv, vif, req);
	if (err < 0)
		return err;

	return mt7615_mcu_sched_scan_enable(mphy->priv, vif, true);
}

static int
mt7615_stop_sched_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt76_phy *mphy = hw->priv;

	return mt7615_mcu_sched_scan_enable(mphy->priv, vif, false);
}

static int mt7615_remain_on_channel(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_channel *chan,
				    int duration,
				    enum ieee80211_roc_type type)
{
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	int err;

	if (test_and_set_bit(MT76_STATE_ROC, &phy->mt76->state))
		return 0;

	err = mt7615_mcu_set_roc(phy, vif, chan, duration);
	if (err < 0) {
		clear_bit(MT76_STATE_ROC, &phy->mt76->state);
		return err;
	}

	if (!wait_event_timeout(phy->roc_wait, phy->roc_grant, HZ)) {
		mt7615_mcu_set_roc(phy, vif, NULL, 0);
		clear_bit(MT76_STATE_ROC, &phy->mt76->state);

		return -ETIMEDOUT;
	}

	return 0;
}

static int mt7615_cancel_remain_on_channel(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif)
{
	struct mt7615_phy *phy = mt7615_hw_phy(hw);

	if (!test_and_clear_bit(MT76_STATE_ROC, &phy->mt76->state))
		return 0;

	del_timer_sync(&phy->roc_timer);
	cancel_work_sync(&phy->roc_work);

	mt7615_mcu_set_roc(phy, vif, NULL, 0);

	return 0;
}

#ifdef CONFIG_PM
static int mt7615_suspend(struct ieee80211_hw *hw,
			  struct cfg80211_wowlan *wowlan)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	bool ext_phy = phy != &dev->phy;
	int err = 0;

	mutex_lock(&dev->mt76.mutex);

	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	cancel_delayed_work_sync(&phy->scan_work);
	cancel_delayed_work_sync(&phy->mac_work);

	mt76_set(dev, MT_WF_RFCR(ext_phy), MT_WF_RFCR_DROP_OTHER_BEACON);

	set_bit(MT76_STATE_SUSPEND, &phy->mt76->state);
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7615_mcu_set_suspend_iter, phy);

	if (!mt7615_dev_running(dev))
		err = mt7615_mcu_set_hif_suspend(dev, true);

	mutex_unlock(&dev->mt76.mutex);

	return err;
}

static int mt7615_resume(struct ieee80211_hw *hw)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	bool running, ext_phy = phy != &dev->phy;

	mutex_lock(&dev->mt76.mutex);

	running = mt7615_dev_running(dev);
	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	if (!running) {
		int err;

		err = mt7615_mcu_set_hif_suspend(dev, false);
		if (err < 0) {
			mutex_unlock(&dev->mt76.mutex);
			return err;
		}
	}

	clear_bit(MT76_STATE_SUSPEND, &phy->mt76->state);
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7615_mcu_set_suspend_iter, phy);

	ieee80211_queue_delayed_work(hw, &phy->mac_work,
				     MT7615_WATCHDOG_TIME);
	mt76_clear(dev, MT_WF_RFCR(ext_phy), MT_WF_RFCR_DROP_OTHER_BEACON);

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static void mt7615_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt76_dev *mdev = &dev->mt76;

	device_set_wakeup_enable(mdev->dev, enabled);
}

static void mt7615_set_rekey_data(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct cfg80211_gtk_rekey_data *data)
{
	mt7615_mcu_update_gtk_rekey(hw, vif, data);
}
#endif /* CONFIG_PM */

const struct ieee80211_ops mt7615_ops = {
	.tx = mt7615_tx,
	.start = mt7615_start,
	.stop = mt7615_stop,
	.add_interface = mt7615_add_interface,
	.remove_interface = mt7615_remove_interface,
	.config = mt7615_config,
	.conf_tx = mt7615_conf_tx,
	.configure_filter = mt7615_configure_filter,
	.bss_info_changed = mt7615_bss_info_changed,
	.sta_add = mt7615_sta_add,
	.sta_remove = mt7615_sta_remove,
	.sta_pre_rcu_remove = mt76_sta_pre_rcu_remove,
	.set_key = mt7615_set_key,
	.ampdu_action = mt7615_ampdu_action,
	.set_rts_threshold = mt7615_set_rts_threshold,
	.wake_tx_queue = mt76_wake_tx_queue,
	.sta_rate_tbl_update = mt7615_sta_rate_tbl_update,
	.sw_scan_start = mt76_sw_scan,
	.sw_scan_complete = mt76_sw_scan_complete,
	.release_buffered_frames = mt76_release_buffered_frames,
	.get_txpower = mt76_get_txpower,
	.channel_switch_beacon = mt7615_channel_switch_beacon,
	.get_stats = mt7615_get_stats,
	.get_tsf = mt7615_get_tsf,
	.get_survey = mt76_get_survey,
	.get_antenna = mt76_get_antenna,
	.set_antenna = mt7615_set_antenna,
	.set_coverage_class = mt7615_set_coverage_class,
	.hw_scan = mt7615_hw_scan,
	.cancel_hw_scan = mt7615_cancel_hw_scan,
	.sched_scan_start = mt7615_start_sched_scan,
	.sched_scan_stop = mt7615_stop_sched_scan,
	.remain_on_channel = mt7615_remain_on_channel,
	.cancel_remain_on_channel = mt7615_cancel_remain_on_channel,
#ifdef CONFIG_PM
	.suspend = mt7615_suspend,
	.resume = mt7615_resume,
	.set_wakeup = mt7615_set_wakeup,
	.set_rekey_data = mt7615_set_rekey_data,
#endif /* CONFIG_PM */
};
EXPORT_SYMBOL_GPL(mt7615_ops);

MODULE_LICENSE("Dual BSD/GPL");
