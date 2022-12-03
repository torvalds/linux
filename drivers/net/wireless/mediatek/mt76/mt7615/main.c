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
	unsigned long timeout;
	bool running;
	int ret;

	if (!mt7615_wait_for_mcu_init(dev))
		return -EIO;

	mt7615_mutex_acquire(dev);

	running = mt7615_dev_running(dev);

	if (!running) {
		ret = mt7615_mcu_set_pm(dev, 0, 0);
		if (ret)
			goto out;

		ret = mt76_connac_mcu_set_mac_enable(&dev->mt76, 0, true, false);
		if (ret)
			goto out;

		mt7615_mac_enable_nf(dev, 0);
	}

	if (phy != &dev->phy) {
		ret = mt7615_mcu_set_pm(dev, 1, 0);
		if (ret)
			goto out;

		ret = mt76_connac_mcu_set_mac_enable(&dev->mt76, 1, true, false);
		if (ret)
			goto out;

		mt7615_mac_enable_nf(dev, 1);
	}

	if (mt7615_firmware_offload(dev)) {
		ret = mt76_connac_mcu_set_channel_domain(phy->mt76);
		if (ret)
			goto out;

		ret = mt76_connac_mcu_set_rate_txpower(phy->mt76);
		if (ret)
			goto out;
	}

	ret = mt7615_mcu_set_chan_info(phy, MCU_EXT_CMD(SET_RX_PATH));
	if (ret)
		goto out;

	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	timeout = mt7615_get_macwork_timeout(dev);
	ieee80211_queue_delayed_work(hw, &phy->mt76->mac_work, timeout);

	if (!running)
		mt7615_mac_reset_counters(phy);

out:
	mt7615_mutex_release(dev);

	return ret;
}

static void mt7615_stop(struct ieee80211_hw *hw)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);

	cancel_delayed_work_sync(&phy->mt76->mac_work);
	del_timer_sync(&phy->roc_timer);
	cancel_work_sync(&phy->roc_work);

	cancel_delayed_work_sync(&dev->pm.ps_work);
	cancel_work_sync(&dev->pm.wake_work);

	mt76_connac_free_pending_tx_skbs(&dev->pm, NULL);

	mt7615_mutex_acquire(dev);

	mt76_testmode_reset(phy->mt76, true);

	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	cancel_delayed_work_sync(&phy->scan_work);

	if (phy != &dev->phy) {
		mt7615_mcu_set_pm(dev, 1, 1);
		mt76_connac_mcu_set_mac_enable(&dev->mt76, 1, false, false);
	}

	if (!mt7615_dev_running(dev)) {
		mt7615_mcu_set_pm(dev, 0, 1);
		mt76_connac_mcu_set_mac_enable(&dev->mt76, 0, false, false);
	}

	mt7615_mutex_release(dev);
}

static inline int get_free_idx(u32 mask, u8 start, u8 end)
{
	return ffs(~mask & GENMASK(end, start));
}

static int get_omac_idx(enum nl80211_iftype type, u64 mask)
{
	int i;

	switch (type) {
	case NL80211_IFTYPE_STATION:
		/* prefer hw bssid slot 1-3 */
		i = get_free_idx(mask, HW_BSSID_1, HW_BSSID_3);
		if (i)
			return i - 1;

		/* next, try to find a free repeater entry for the sta */
		i = get_free_idx(mask >> REPEATER_BSSID_START, 0,
				 REPEATER_BSSID_MAX - REPEATER_BSSID_START);
		if (i)
			return i + 32 - 1;

		i = get_free_idx(mask, EXT_BSSID_1, EXT_BSSID_MAX);
		if (i)
			return i - 1;

		if (~mask & BIT(HW_BSSID_0))
			return HW_BSSID_0;

		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_AP:
		/* ap uses hw bssid 0 and ext bssid */
		if (~mask & BIT(HW_BSSID_0))
			return HW_BSSID_0;

		i = get_free_idx(mask, EXT_BSSID_1, EXT_BSSID_MAX);
		if (i)
			return i - 1;

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

	mt7615_mutex_acquire(dev);

	mt76_testmode_reset(phy->mt76, true);

	if (vif->type == NL80211_IFTYPE_MONITOR &&
	    is_zero_ether_addr(vif->addr))
		phy->monitor_vif = vif;

	mvif->mt76.idx = __ffs64(~dev->mt76.vif_mask);
	if (mvif->mt76.idx >= MT7615_MAX_INTERFACES) {
		ret = -ENOSPC;
		goto out;
	}

	idx = get_omac_idx(vif->type, dev->omac_mask);
	if (idx < 0) {
		ret = -ENOSPC;
		goto out;
	}
	mvif->mt76.omac_idx = idx;

	mvif->mt76.band_idx = ext_phy;
	mvif->mt76.wmm_idx = vif->type != NL80211_IFTYPE_AP;
	if (ext_phy)
		mvif->mt76.wmm_idx += 2;

	dev->mt76.vif_mask |= BIT_ULL(mvif->mt76.idx);
	dev->omac_mask |= BIT_ULL(mvif->mt76.omac_idx);
	phy->omac_mask |= BIT_ULL(mvif->mt76.omac_idx);

	ret = mt7615_mcu_set_dbdc(dev);
	if (ret)
		goto out;

	idx = MT7615_WTBL_RESERVED - mvif->mt76.idx;

	INIT_LIST_HEAD(&mvif->sta.poll_list);
	mvif->sta.wcid.idx = idx;
	mvif->sta.wcid.phy_idx = mvif->mt76.band_idx;
	mvif->sta.wcid.hw_key_idx = -1;
	mt76_packet_id_init(&mvif->sta.wcid);

	mt7615_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	rcu_assign_pointer(dev->mt76.wcid[idx], &mvif->sta.wcid);
	if (vif->txq) {
		mtxq = (struct mt76_txq *)vif->txq->drv_priv;
		mtxq->wcid = idx;
	}

	ret = mt7615_mcu_add_dev_info(phy, vif, true);
out:
	mt7615_mutex_release(dev);

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

	mt7615_mutex_acquire(dev);

	mt7615_mcu_add_bss_info(phy, vif, NULL, false);
	mt7615_mcu_sta_add(phy, vif, NULL, false);

	mt76_testmode_reset(phy->mt76, true);
	if (vif == phy->monitor_vif)
	    phy->monitor_vif = NULL;

	mt76_connac_free_pending_tx_skbs(&dev->pm, &msta->wcid);

	mt7615_mcu_add_dev_info(phy, vif, false);

	rcu_assign_pointer(dev->mt76.wcid[idx], NULL);

	dev->mt76.vif_mask &= ~BIT_ULL(mvif->mt76.idx);
	dev->omac_mask &= ~BIT_ULL(mvif->mt76.omac_idx);
	phy->omac_mask &= ~BIT_ULL(mvif->mt76.omac_idx);

	mt7615_mutex_release(dev);

	spin_lock_bh(&dev->sta_poll_lock);
	if (!list_empty(&msta->poll_list))
		list_del_init(&msta->poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	mt76_packet_id_flush(&dev->mt76, &mvif->sta.wcid);
}

int mt7615_set_channel(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	int ret;

	cancel_delayed_work_sync(&phy->mt76->mac_work);

	mt7615_mutex_acquire(dev);

	set_bit(MT76_RESET, &phy->mt76->state);

	mt76_set_channel(phy->mt76);

	if (is_mt7615(&dev->mt76) && dev->flash_eeprom) {
		ret = mt7615_mcu_apply_rx_dcoc(phy);
		if (ret)
			goto out;

		ret = mt7615_mcu_apply_tx_dpd(phy);
		if (ret)
			goto out;
	}

	ret = mt7615_mcu_set_chan_info(phy, MCU_EXT_CMD(CHANNEL_SWITCH));
	if (ret)
		goto out;

	mt7615_mac_set_timing(phy);
	ret = mt7615_dfs_init_radar_detector(phy);
	if (ret)
		goto out;

	mt7615_mac_cca_stats_reset(phy);
	ret = mt7615_mcu_set_sku_en(phy, true);
	if (ret)
		goto out;

	mt7615_mac_reset_counters(phy);
	phy->noise = 0;
	phy->chfreq = mt76_rr(dev, MT_CHFREQ(ext_phy));

out:
	clear_bit(MT76_RESET, &phy->mt76->state);

	mt7615_mutex_release(dev);

	mt76_worker_schedule(&dev->mt76.tx_worker);
	if (!mt76_testmode_enabled(phy->mt76)) {
		unsigned long timeout = mt7615_get_macwork_timeout(dev);

		ieee80211_queue_delayed_work(phy->mt76->hw,
					     &phy->mt76->mac_work, timeout);
	}

	return ret;
}

static int mt7615_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_sta *msta = sta ? (struct mt7615_sta *)sta->drv_priv :
				  &mvif->sta;
	struct mt76_wcid *wcid = &msta->wcid;
	int idx = key->keyidx, err = 0;
	u8 *wcid_keyidx = &wcid->hw_key_idx;

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
		wcid_keyidx = &wcid->hw_key_idx2;
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIE;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
	case WLAN_CIPHER_SUITE_SMS4:
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
	default:
		return -EOPNOTSUPP;
	}

	mt7615_mutex_acquire(dev);

	if (cmd == SET_KEY && !sta && !mvif->mt76.cipher) {
		mvif->mt76.cipher = mt76_connac_mcu_get_cipher(key->cipher);
		mt7615_mcu_add_bss_info(phy, vif, NULL, true);
	}

	if (cmd == SET_KEY)
		*wcid_keyidx = idx;
	else if (idx == *wcid_keyidx)
		*wcid_keyidx = -1;
	else
		goto out;

	mt76_wcid_key_setup(&dev->mt76, wcid,
			    cmd == SET_KEY ? key : NULL);

	if (mt76_is_mmio(&dev->mt76))
		err = mt7615_mac_wtbl_set_key(dev, wcid, key, cmd);
	else
		err = __mt7615_mac_wtbl_set_key(dev, wcid, key, cmd);

out:
	mt7615_mutex_release(dev);

	return err;
}

static int mt7615_set_sar_specs(struct ieee80211_hw *hw,
				const struct cfg80211_sar_specs *sar)
{
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	int err;

	if (!cfg80211_chandef_valid(&phy->mt76->chandef))
		return -EINVAL;

	err = mt76_init_sar_power(hw, sar);
	if (err)
		return err;

	if (mt7615_firmware_offload(phy->dev))
		return mt76_connac_mcu_set_rate_txpower(phy->mt76);

	ieee80211_stop_queues(hw);
	err = mt7615_set_channel(phy);
	ieee80211_wake_queues(hw);

	return err;
}

static int mt7615_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	bool band = phy != &dev->phy;
	int ret = 0;

	if (changed & (IEEE80211_CONF_CHANGE_CHANNEL |
		       IEEE80211_CONF_CHANGE_POWER)) {
#ifdef CONFIG_NL80211_TESTMODE
		if (phy->mt76->test.state != MT76_TM_STATE_OFF) {
			mt7615_mutex_acquire(dev);
			mt76_testmode_reset(phy->mt76, false);
			mt7615_mutex_release(dev);
		}
#endif
		ieee80211_stop_queues(hw);
		ret = mt7615_set_channel(phy);
		ieee80211_wake_queues(hw);
	}

	mt7615_mutex_acquire(dev);

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		mt76_testmode_reset(phy->mt76, true);

		if (!(hw->conf.flags & IEEE80211_CONF_MONITOR))
			phy->rxfilter |= MT_WF_RFCR_DROP_OTHER_UC;
		else
			phy->rxfilter &= ~MT_WF_RFCR_DROP_OTHER_UC;

		mt76_wr(dev, MT_WF_RFCR(band), phy->rxfilter);
	}

	mt7615_mutex_release(dev);

	return ret;
}

static int
mt7615_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       unsigned int link_id, u16 queue,
	       const struct ieee80211_tx_queue_params *params)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	int err;

	mt7615_mutex_acquire(dev);

	queue = mt7615_lmac_mapping(dev, queue);
	queue += mvif->wmm_idx * MT7615_MAX_WMM_SETS;
	err = mt7615_mcu_set_wmm(dev, queue, params);

	mt7615_mutex_release(dev);

	return err;
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

	mt7615_mutex_acquire(dev);

#define MT76_FILTER(_flag, _hw) do { \
		flags |= *total_flags & FIF_##_flag;			\
		phy->rxfilter &= ~(_hw);				\
		if (!mt76_testmode_enabled(phy->mt76))			\
			phy->rxfilter |= !(flags & FIF_##_flag) * (_hw);\
	} while (0)

	phy->rxfilter &= ~(MT_WF_RFCR_DROP_OTHER_BSS |
			   MT_WF_RFCR_DROP_FRAME_REPORT |
			   MT_WF_RFCR_DROP_PROBEREQ |
			   MT_WF_RFCR_DROP_MCAST_FILTERED |
			   MT_WF_RFCR_DROP_MCAST |
			   MT_WF_RFCR_DROP_BCAST |
			   MT_WF_RFCR_DROP_DUPLICATE |
			   MT_WF_RFCR_DROP_A2_BSSID |
			   MT_WF_RFCR_DROP_UNWANTED_CTL |
			   MT_WF_RFCR_DROP_STBC_MULTI);

	if (phy->n_beacon_vif || !mt7615_firmware_offload(dev))
		phy->rxfilter &= ~MT_WF_RFCR_DROP_OTHER_BEACON;

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

	mt7615_mutex_release(dev);
}

static void mt7615_bss_info_changed(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *info,
				    u64 changed)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);

	mt7615_mutex_acquire(dev);

	if (changed & BSS_CHANGED_ERP_SLOT) {
		int slottime = info->use_short_slot ? 9 : 20;

		if (slottime != phy->slottime) {
			phy->slottime = slottime;
			mt7615_mac_set_timing(phy);
		}
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT)
		mt7615_mac_enable_rtscts(dev, vif, info->use_cts_prot);

	if (changed & BSS_CHANGED_BEACON_ENABLED && info->enable_beacon) {
		mt7615_mcu_add_bss_info(phy, vif, NULL, true);
		mt7615_mcu_sta_add(phy, vif, NULL, true);

		if (mt7615_firmware_offload(dev) && vif->p2p)
			mt76_connac_mcu_set_p2p_oppps(hw, vif);
	}

	if (changed & (BSS_CHANGED_BEACON |
		       BSS_CHANGED_BEACON_ENABLED))
		mt7615_mcu_add_beacon(dev, hw, vif, info->enable_beacon);

	if (changed & BSS_CHANGED_PS)
		mt76_connac_mcu_set_vif_ps(&dev->mt76, vif);

	if ((changed & BSS_CHANGED_ARP_FILTER) &&
	    mt7615_firmware_offload(dev)) {
		struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;

		mt76_connac_mcu_update_arp_filter(&dev->mt76, &mvif->mt76,
						  info);
	}

	if (changed & BSS_CHANGED_ASSOC)
		mt7615_mac_set_beacon_filter(phy, vif, vif->cfg.assoc);

	mt7615_mutex_release(dev);
}

static void
mt7615_channel_switch_beacon(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct cfg80211_chan_def *chandef)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);

	mt7615_mutex_acquire(dev);
	mt7615_mcu_add_beacon(dev, hw, vif, true);
	mt7615_mutex_release(dev);
}

int mt7615_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_phy *phy;
	int idx, err;

	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7615_WTBL_STA - 1);
	if (idx < 0)
		return -ENOSPC;

	INIT_LIST_HEAD(&msta->poll_list);
	msta->vif = mvif;
	msta->wcid.sta = 1;
	msta->wcid.idx = idx;
	msta->wcid.phy_idx = mvif->mt76.band_idx;

	phy = mvif->mt76.band_idx ? mt7615_ext_phy(dev) : &dev->phy;
	err = mt76_connac_pm_wake(phy->mt76, &dev->pm);
	if (err)
		return err;

	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls) {
		err = mt7615_mcu_add_bss_info(phy, vif, sta, true);
		if (err)
			return err;
	}

	mt7615_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
	err = mt7615_mcu_sta_add(&dev->phy, vif, sta, true);
	if (err)
		return err;

	mt76_connac_power_save_sched(phy->mt76, &dev->pm);

	return err;
}
EXPORT_SYMBOL_GPL(mt7615_mac_sta_add);

void mt7615_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_phy *phy;

	mt76_connac_free_pending_tx_skbs(&dev->pm, &msta->wcid);

	phy = mvif->mt76.band_idx ? mt7615_ext_phy(dev) : &dev->phy;
	mt76_connac_pm_wake(phy->mt76, &dev->pm);

	mt7615_mcu_sta_add(&dev->phy, vif, sta, false);
	mt7615_mac_wtbl_update(dev, msta->wcid.idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls)
		mt7615_mcu_add_bss_info(phy, vif, sta, false);

	spin_lock_bh(&dev->sta_poll_lock);
	if (!list_empty(&msta->poll_list))
		list_del_init(&msta->poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	mt76_connac_power_save_sched(phy->mt76, &dev->pm);
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

	if (!sta_rates)
		return;

	spin_lock_bh(&dev->mt76.lock);
	for (i = 0; i < ARRAY_SIZE(msta->rates); i++) {
		msta->rates[i].idx = sta_rates->rate[i].idx;
		msta->rates[i].count = sta_rates->rate[i].count;
		msta->rates[i].flags = sta_rates->rate[i].flags;

		if (msta->rates[i].idx < 0 || !msta->rates[i].count)
			break;
	}
	msta->n_rates = i;
	if (mt76_connac_pm_ref(phy->mt76, &dev->pm)) {
		mt7615_mac_set_rates(phy, msta, NULL, msta->rates);
		mt76_connac_pm_unref(phy->mt76, &dev->pm);
	}
	spin_unlock_bh(&dev->mt76.lock);
}

void mt7615_tx_worker(struct mt76_worker *w)
{
	struct mt7615_dev *dev = container_of(w, struct mt7615_dev,
					      mt76.tx_worker);

	if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
		queue_work(dev->mt76.wq, &dev->pm.wake_work);
		return;
	}

	mt76_tx_worker_run(&dev->mt76);
	mt76_connac_pm_unref(&dev->mphy, &dev->pm);
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
	struct mt7615_sta *msta = NULL;
	int qid;

	if (control->sta) {
		msta = (struct mt7615_sta *)control->sta->drv_priv;
		wcid = &msta->wcid;
	}

	if (vif && !control->sta) {
		struct mt7615_vif *mvif;

		mvif = (struct mt7615_vif *)vif->drv_priv;
		msta = &mvif->sta;
		wcid = &msta->wcid;
	}

	if (mt76_connac_pm_ref(mphy, &dev->pm)) {
		mt76_tx(mphy, control->sta, wcid, skb);
		mt76_connac_pm_unref(mphy, &dev->pm);
		return;
	}

	qid = skb_get_queue_mapping(skb);
	if (qid >= MT_TXQ_PSD) {
		qid = IEEE80211_AC_BE;
		skb_set_queue_mapping(skb, qid);
	}

	mt76_connac_pm_queue_skb(hw, &dev->pm, wcid, skb);
}

static int mt7615_set_rts_threshold(struct ieee80211_hw *hw, u32 val)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	int err, band = phy != &dev->phy;

	mt7615_mutex_acquire(dev);
	err = mt76_connac_mcu_set_rts_thresh(&dev->mt76, val, band);
	mt7615_mutex_release(dev);

	return err;
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

	mt7615_mutex_acquire(dev);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		mt76_rx_aggr_start(&dev->mt76, &msta->wcid, tid, ssn,
				   params->buf_size);
		ret = mt7615_mcu_add_rx_ba(dev, params, true);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		mt76_rx_aggr_stop(&dev->mt76, &msta->wcid, tid);
		ret = mt7615_mcu_add_rx_ba(dev, params, false);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		mtxq->aggr = true;
		mtxq->send_bar = false;
		ret = mt7615_mcu_add_tx_ba(dev, params, true);
		ssn = mt7615_mac_get_sta_tid_sn(dev, msta->wcid.idx, tid);
		ieee80211_send_bar(vif, sta->addr, tid,
				   IEEE80211_SN_TO_SEQ(ssn));
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		mtxq->aggr = false;
		ret = mt7615_mcu_add_tx_ba(dev, params, false);
		break;
	case IEEE80211_AMPDU_TX_START:
		ssn = mt7615_mac_get_sta_tid_sn(dev, msta->wcid.idx, tid);
		params->ssn = ssn;
		ret = IEEE80211_AMPDU_TX_START_IMMEDIATE;
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		mtxq->aggr = false;
		ret = mt7615_mcu_add_tx_ba(dev, params, false);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	}
	mt7615_mutex_release(dev);

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

	mt7615_mutex_acquire(phy->dev);

	stats->dot11RTSSuccessCount = mib->rts_cnt;
	stats->dot11RTSFailureCount = mib->rts_retries_cnt;
	stats->dot11FCSErrorCount = mib->fcs_err_cnt;
	stats->dot11ACKFailureCount = mib->ack_fail_cnt;

	mt7615_mutex_release(phy->dev);

	return 0;
}

static u64
mt7615_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	union {
		u64 t64;
		u32 t32[2];
	} tsf;
	u16 idx = mvif->mt76.omac_idx;
	u32 reg;

	idx = idx > HW_BSSID_MAX ? HW_BSSID_0 : idx;
	reg = idx > 1 ? MT_LPON_TCR2(idx): MT_LPON_TCR0(idx);

	mt7615_mutex_acquire(dev);

	/* TSF read */
	mt76_rmw(dev, reg, MT_LPON_TCR_MODE, MT_LPON_TCR_READ);
	tsf.t32[0] = mt76_rr(dev, MT_LPON_UTTR0);
	tsf.t32[1] = mt76_rr(dev, MT_LPON_UTTR1);

	mt7615_mutex_release(dev);

	return tsf.t64;
}

static void
mt7615_set_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       u64 timestamp)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	union {
		u64 t64;
		u32 t32[2];
	} tsf = { .t64 = timestamp, };
	u16 idx = mvif->mt76.omac_idx;
	u32 reg;

	idx = idx > HW_BSSID_MAX ? HW_BSSID_0 : idx;
	reg = idx > 1 ? MT_LPON_TCR2(idx): MT_LPON_TCR0(idx);

	mt7615_mutex_acquire(dev);

	mt76_wr(dev, MT_LPON_UTTR0, tsf.t32[0]);
	mt76_wr(dev, MT_LPON_UTTR1, tsf.t32[1]);
	/* TSF software overwrite */
	mt76_rmw(dev, reg, MT_LPON_TCR_MODE, MT_LPON_TCR_WRITE);

	mt7615_mutex_release(dev);
}

static void
mt7615_offset_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  s64 timestamp)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	union {
		u64 t64;
		u32 t32[2];
	} tsf = { .t64 = timestamp, };
	u16 idx = mvif->mt76.omac_idx;
	u32 reg;

	idx = idx > HW_BSSID_MAX ? HW_BSSID_0 : idx;
	reg = idx > 1 ? MT_LPON_TCR2(idx): MT_LPON_TCR0(idx);

	mt7615_mutex_acquire(dev);

	mt76_wr(dev, MT_LPON_UTTR0, tsf.t32[0]);
	mt76_wr(dev, MT_LPON_UTTR1, tsf.t32[1]);
	/* TSF software adjust*/
	mt76_rmw(dev, reg, MT_LPON_TCR_MODE, MT_LPON_TCR_ADJUST);

	mt7615_mutex_release(dev);
}

static void
mt7615_set_coverage_class(struct ieee80211_hw *hw, s16 coverage_class)
{
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	struct mt7615_dev *dev = phy->dev;

	mt7615_mutex_acquire(dev);
	phy->coverage_class = max_t(s16, coverage_class, 0);
	mt7615_mac_set_timing(phy);
	mt7615_mutex_release(dev);
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

	mt7615_mutex_acquire(dev);

	phy->mt76->antenna_mask = tx_ant;
	if (ext_phy) {
		if (dev->chainmask == 0xf)
			tx_ant <<= 2;
		else
			tx_ant <<= 1;
	}
	phy->mt76->chainmask = tx_ant;

	mt76_set_stream_caps(phy->mt76, true);

	mt7615_mutex_release(dev);

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

	mt7615_mutex_acquire(phy->dev);
	ieee80211_iterate_active_interfaces(phy->mt76->hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7615_roc_iter, phy);
	mt7615_mutex_release(phy->dev);
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
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	int err;

	/* fall-back to sw-scan */
	if (!mt7615_firmware_offload(dev))
		return 1;

	mt7615_mutex_acquire(dev);
	err = mt76_connac_mcu_hw_scan(mphy, vif, req);
	mt7615_mutex_release(dev);

	return err;
}

static void
mt7615_cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;

	mt7615_mutex_acquire(dev);
	mt76_connac_mcu_cancel_hw_scan(mphy, vif);
	mt7615_mutex_release(dev);
}

static int
mt7615_start_sched_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct cfg80211_sched_scan_request *req,
			struct ieee80211_scan_ies *ies)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	int err;

	if (!mt7615_firmware_offload(dev))
		return -EOPNOTSUPP;

	mt7615_mutex_acquire(dev);

	err = mt76_connac_mcu_sched_scan_req(mphy, vif, req);
	if (err < 0)
		goto out;

	err = mt76_connac_mcu_sched_scan_enable(mphy, vif, true);
out:
	mt7615_mutex_release(dev);

	return err;
}

static int
mt7615_stop_sched_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	int err;

	if (!mt7615_firmware_offload(dev))
		return -EOPNOTSUPP;

	mt7615_mutex_acquire(dev);
	err = mt76_connac_mcu_sched_scan_enable(mphy, vif, false);
	mt7615_mutex_release(dev);

	return err;
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

	mt7615_mutex_acquire(phy->dev);

	err = mt7615_mcu_set_roc(phy, vif, chan, duration);
	if (err < 0) {
		clear_bit(MT76_STATE_ROC, &phy->mt76->state);
		goto out;
	}

	if (!wait_event_timeout(phy->roc_wait, phy->roc_grant, HZ)) {
		mt7615_mcu_set_roc(phy, vif, NULL, 0);
		clear_bit(MT76_STATE_ROC, &phy->mt76->state);
		err = -ETIMEDOUT;
	}

out:
	mt7615_mutex_release(phy->dev);

	return err;
}

static int mt7615_cancel_remain_on_channel(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif)
{
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	int err;

	if (!test_and_clear_bit(MT76_STATE_ROC, &phy->mt76->state))
		return 0;

	del_timer_sync(&phy->roc_timer);
	cancel_work_sync(&phy->roc_work);

	mt7615_mutex_acquire(phy->dev);
	err = mt7615_mcu_set_roc(phy, vif, NULL, 0);
	mt7615_mutex_release(phy->dev);

	return err;
}

static void mt7615_sta_set_decap_offload(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 bool enabled)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;

	mt7615_mutex_acquire(dev);

	if (enabled)
		set_bit(MT_WCID_FLAG_HDR_TRANS, &msta->wcid.flags);
	else
		clear_bit(MT_WCID_FLAG_HDR_TRANS, &msta->wcid.flags);

	mt7615_mcu_set_sta_decap_offload(dev, vif, sta);

	mt7615_mutex_release(dev);
}

#ifdef CONFIG_PM
static int mt7615_suspend(struct ieee80211_hw *hw,
			  struct cfg80211_wowlan *wowlan)
{
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	int err = 0;

	cancel_delayed_work_sync(&dev->pm.ps_work);
	mt76_connac_free_pending_tx_skbs(&dev->pm, NULL);

	mt7615_mutex_acquire(dev);

	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	cancel_delayed_work_sync(&phy->scan_work);
	cancel_delayed_work_sync(&phy->mt76->mac_work);

	set_bit(MT76_STATE_SUSPEND, &phy->mt76->state);
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt76_connac_mcu_set_suspend_iter,
					    phy->mt76);

	if (!mt7615_dev_running(dev))
		err = mt76_connac_mcu_set_hif_suspend(&dev->mt76, true);

	mt7615_mutex_release(dev);

	return err;
}

static int mt7615_resume(struct ieee80211_hw *hw)
{
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	unsigned long timeout;
	bool running;

	mt7615_mutex_acquire(dev);

	running = mt7615_dev_running(dev);
	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	if (!running) {
		int err;

		err = mt76_connac_mcu_set_hif_suspend(&dev->mt76, false);
		if (err < 0) {
			mt7615_mutex_release(dev);
			return err;
		}
	}

	clear_bit(MT76_STATE_SUSPEND, &phy->mt76->state);
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt76_connac_mcu_set_suspend_iter,
					    phy->mt76);

	timeout = mt7615_get_macwork_timeout(dev);
	ieee80211_queue_delayed_work(hw, &phy->mt76->mac_work, timeout);

	mt7615_mutex_release(dev);

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
	struct mt7615_dev *dev = mt7615_hw_dev(hw);

	mt7615_mutex_acquire(dev);
	mt76_connac_mcu_update_gtk_rekey(hw, vif, data);
	mt7615_mutex_release(dev);
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
	.sta_set_decap_offload = mt7615_sta_set_decap_offload,
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
	.set_tsf = mt7615_set_tsf,
	.offset_tsf = mt7615_offset_tsf,
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
	CFG80211_TESTMODE_CMD(mt76_testmode_cmd)
	CFG80211_TESTMODE_DUMP(mt76_testmode_dump)
#ifdef CONFIG_PM
	.suspend = mt7615_suspend,
	.resume = mt7615_resume,
	.set_wakeup = mt7615_set_wakeup,
	.set_rekey_data = mt7615_set_rekey_data,
#endif /* CONFIG_PM */
	.set_sar_specs = mt7615_set_sar_specs,
};
EXPORT_SYMBOL_GPL(mt7615_ops);

MODULE_LICENSE("Dual BSD/GPL");
