// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/module.h>
#include "mt7915.h"
#include "mcu.h"

static bool mt7915_dev_running(struct mt7915_dev *dev)
{
	struct mt7915_phy *phy;

	if (test_bit(MT76_STATE_RUNNING, &dev->mphy.state))
		return true;

	phy = mt7915_ext_phy(dev);

	return phy && test_bit(MT76_STATE_RUNNING, &phy->mt76->state);
}

int mt7915_run(struct ieee80211_hw *hw)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	bool running;
	int ret;

	running = mt7915_dev_running(dev);

	if (!running) {
		ret = mt76_connac_mcu_set_pm(&dev->mt76,
					     dev->phy.mt76->band_idx, 0);
		if (ret)
			goto out;

		ret = mt7915_mcu_set_mac(dev, dev->phy.mt76->band_idx,
					 true, true);
		if (ret)
			goto out;

		mt7915_mac_enable_nf(dev, dev->phy.mt76->band_idx);
	}

	if (phy != &dev->phy) {
		ret = mt76_connac_mcu_set_pm(&dev->mt76,
					     phy->mt76->band_idx, 0);
		if (ret)
			goto out;

		ret = mt7915_mcu_set_mac(dev, phy->mt76->band_idx,
					 true, true);
		if (ret)
			goto out;

		mt7915_mac_enable_nf(dev, phy->mt76->band_idx);
	}

	ret = mt7915_mcu_set_thermal_throttling(phy,
						MT7915_THERMAL_THROTTLE_MAX);

	if (ret)
		goto out;

	ret = mt7915_mcu_set_thermal_protect(phy);

	if (ret)
		goto out;

	ret = mt76_connac_mcu_set_rts_thresh(&dev->mt76, 0x92b,
					     phy->mt76->band_idx);
	if (ret)
		goto out;

	ret = mt7915_mcu_set_sku_en(phy, true);
	if (ret)
		goto out;

	ret = mt7915_mcu_set_chan_info(phy, MCU_EXT_CMD(SET_RX_PATH));
	if (ret)
		goto out;

	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	if (!mt76_testmode_enabled(phy->mt76))
		ieee80211_queue_delayed_work(hw, &phy->mt76->mac_work,
					     MT7915_WATCHDOG_TIME);

	if (!running)
		mt7915_mac_reset_counters(phy);

out:
	return ret;
}

static int mt7915_start(struct ieee80211_hw *hw)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	int ret;

	flush_work(&dev->init_work);

	mutex_lock(&dev->mt76.mutex);
	ret = mt7915_run(hw);
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void mt7915_stop(struct ieee80211_hw *hw)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);

	cancel_delayed_work_sync(&phy->mt76->mac_work);

	mutex_lock(&dev->mt76.mutex);

	mt76_testmode_reset(phy->mt76, true);

	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	if (phy != &dev->phy) {
		mt76_connac_mcu_set_pm(&dev->mt76, phy->mt76->band_idx, 1);
		mt7915_mcu_set_mac(dev, phy->mt76->band_idx, false, false);
	}

	if (!mt7915_dev_running(dev)) {
		mt76_connac_mcu_set_pm(&dev->mt76, dev->phy.mt76->band_idx, 1);
		mt7915_mcu_set_mac(dev, dev->phy.mt76->band_idx, false, false);
	}

	mutex_unlock(&dev->mt76.mutex);
}

static inline int get_free_idx(u32 mask, u8 start, u8 end)
{
	return ffs(~mask & GENMASK(end, start));
}

static int get_omac_idx(enum nl80211_iftype type, u64 mask)
{
	int i;

	switch (type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_STATION:
		/* prefer hw bssid slot 1-3 */
		i = get_free_idx(mask, HW_BSSID_1, HW_BSSID_3);
		if (i)
			return i - 1;

		if (type != NL80211_IFTYPE_STATION)
			break;

		i = get_free_idx(mask, EXT_BSSID_1, EXT_BSSID_MAX);
		if (i)
			return i - 1;

		if (~mask & BIT(HW_BSSID_0))
			return HW_BSSID_0;

		break;
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

static void mt7915_init_bitrate_mask(struct ieee80211_vif *vif)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	int i;

	for (i = 0; i < ARRAY_SIZE(mvif->bitrate_mask.control); i++) {
		mvif->bitrate_mask.control[i].gi = NL80211_TXRATE_DEFAULT_GI;
		mvif->bitrate_mask.control[i].he_gi = 0xff;
		mvif->bitrate_mask.control[i].he_ltf = 0xff;
		mvif->bitrate_mask.control[i].legacy = GENMASK(31, 0);
		memset(mvif->bitrate_mask.control[i].ht_mcs, 0xff,
		       sizeof(mvif->bitrate_mask.control[i].ht_mcs));
		memset(mvif->bitrate_mask.control[i].vht_mcs, 0xff,
		       sizeof(mvif->bitrate_mask.control[i].vht_mcs));
		memset(mvif->bitrate_mask.control[i].he_mcs, 0xff,
		       sizeof(mvif->bitrate_mask.control[i].he_mcs));
	}
}

static int mt7915_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt76_txq *mtxq;
	bool ext_phy = phy != &dev->phy;
	int idx, ret = 0;

	mutex_lock(&dev->mt76.mutex);

	mt76_testmode_reset(phy->mt76, true);

	if (vif->type == NL80211_IFTYPE_MONITOR &&
	    is_zero_ether_addr(vif->addr))
		phy->monitor_vif = vif;

	mvif->mt76.idx = __ffs64(~dev->mt76.vif_mask);
	if (mvif->mt76.idx >= (MT7915_MAX_INTERFACES << dev->dbdc_support)) {
		ret = -ENOSPC;
		goto out;
	}

	idx = get_omac_idx(vif->type, phy->omac_mask);
	if (idx < 0) {
		ret = -ENOSPC;
		goto out;
	}
	mvif->mt76.omac_idx = idx;
	mvif->phy = phy;
	mvif->mt76.band_idx = phy->mt76->band_idx;

	mvif->mt76.wmm_idx = vif->type != NL80211_IFTYPE_AP;
	if (ext_phy)
		mvif->mt76.wmm_idx += 2;

	ret = mt7915_mcu_add_dev_info(phy, vif, true);
	if (ret)
		goto out;

	dev->mt76.vif_mask |= BIT_ULL(mvif->mt76.idx);
	phy->omac_mask |= BIT_ULL(mvif->mt76.omac_idx);

	idx = MT7915_WTBL_RESERVED - mvif->mt76.idx;

	INIT_LIST_HEAD(&mvif->sta.rc_list);
	INIT_LIST_HEAD(&mvif->sta.wcid.poll_list);
	mvif->sta.wcid.idx = idx;
	mvif->sta.wcid.phy_idx = ext_phy;
	mvif->sta.wcid.hw_key_idx = -1;
	mvif->sta.wcid.tx_info |= MT_WCID_TX_INFO_SET;
	mt76_wcid_init(&mvif->sta.wcid);

	mt7915_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	if (vif->txq) {
		mtxq = (struct mt76_txq *)vif->txq->drv_priv;
		mtxq->wcid = idx;
	}

	if (vif->type != NL80211_IFTYPE_AP &&
	    (!mvif->mt76.omac_idx || mvif->mt76.omac_idx > 3))
		vif->offload_flags = 0;
	vif->offload_flags |= IEEE80211_OFFLOAD_ENCAP_4ADDR;

	mt7915_init_bitrate_mask(vif);
	memset(&mvif->cap, -1, sizeof(mvif->cap));

	mt7915_mcu_add_bss_info(phy, vif, true);
	mt7915_mcu_add_sta(dev, vif, NULL, true);
	rcu_assign_pointer(dev->mt76.wcid[idx], &mvif->sta.wcid);

out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void mt7915_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta = &mvif->sta;
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	int idx = msta->wcid.idx;

	mt7915_mcu_add_bss_info(phy, vif, false);
	mt7915_mcu_add_sta(dev, vif, NULL, false);

	mutex_lock(&dev->mt76.mutex);
	mt76_testmode_reset(phy->mt76, true);
	mutex_unlock(&dev->mt76.mutex);

	if (vif == phy->monitor_vif)
		phy->monitor_vif = NULL;

	mt7915_mcu_add_dev_info(phy, vif, false);

	rcu_assign_pointer(dev->mt76.wcid[idx], NULL);

	mutex_lock(&dev->mt76.mutex);
	dev->mt76.vif_mask &= ~BIT_ULL(mvif->mt76.idx);
	phy->omac_mask &= ~BIT_ULL(mvif->mt76.omac_idx);
	mutex_unlock(&dev->mt76.mutex);

	spin_lock_bh(&dev->mt76.sta_poll_lock);
	if (!list_empty(&msta->wcid.poll_list))
		list_del_init(&msta->wcid.poll_list);
	spin_unlock_bh(&dev->mt76.sta_poll_lock);

	mt76_wcid_cleanup(&dev->mt76, &msta->wcid);
}

int mt7915_set_channel(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	int ret;

	cancel_delayed_work_sync(&phy->mt76->mac_work);

	mutex_lock(&dev->mt76.mutex);
	set_bit(MT76_RESET, &phy->mt76->state);

	mt76_set_channel(phy->mt76);

	if (dev->flash_mode) {
		ret = mt7915_mcu_apply_tx_dpd(phy);
		if (ret)
			goto out;
	}

	ret = mt7915_mcu_set_chan_info(phy, MCU_EXT_CMD(CHANNEL_SWITCH));
	if (ret)
		goto out;

	mt7915_mac_set_timing(phy);
	ret = mt7915_dfs_init_radar_detector(phy);
	mt7915_mac_cca_stats_reset(phy);

	mt7915_mac_reset_counters(phy);
	phy->noise = 0;

out:
	clear_bit(MT76_RESET, &phy->mt76->state);
	mutex_unlock(&dev->mt76.mutex);

	mt76_txq_schedule_all(phy->mt76);

	if (!mt76_testmode_enabled(phy->mt76))
		ieee80211_queue_delayed_work(phy->mt76->hw,
					     &phy->mt76->mac_work,
					     MT7915_WATCHDOG_TIME);

	return ret;
}

static int mt7915_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta = sta ? (struct mt7915_sta *)sta->drv_priv :
				  &mvif->sta;
	struct mt76_wcid *wcid = &msta->wcid;
	u8 *wcid_keyidx = &wcid->hw_key_idx;
	int idx = key->keyidx;
	int err = 0;

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

	mutex_lock(&dev->mt76.mutex);

	if (cmd == SET_KEY && !sta && !mvif->mt76.cipher) {
		mvif->mt76.cipher = mt76_connac_mcu_get_cipher(key->cipher);
		mt7915_mcu_add_bss_info(phy, vif, true);
	}

	if (cmd == SET_KEY) {
		*wcid_keyidx = idx;
	} else {
		if (idx == *wcid_keyidx)
			*wcid_keyidx = -1;
		goto out;
	}

	mt76_wcid_key_setup(&dev->mt76, wcid, key);
	err = mt76_connac_mcu_add_key(&dev->mt76, vif, &msta->bip,
				      key, MCU_EXT_CMD(STA_REC_UPDATE),
				      &msta->wcid, cmd);
out:
	mutex_unlock(&dev->mt76.mutex);

	return err;
}

static int mt7915_set_sar_specs(struct ieee80211_hw *hw,
				const struct cfg80211_sar_specs *sar)
{
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	int err = -EINVAL;

	mutex_lock(&dev->mt76.mutex);
	if (!cfg80211_chandef_valid(&phy->mt76->chandef))
		goto out;

	err = mt76_init_sar_power(hw, sar);
	if (err)
		goto out;

	err = mt7915_mcu_set_txpower_sku(phy);
out:
	mutex_unlock(&dev->mt76.mutex);

	return err;
}

static int mt7915_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	int ret;

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
#ifdef CONFIG_NL80211_TESTMODE
		if (phy->mt76->test.state != MT76_TM_STATE_OFF) {
			mutex_lock(&dev->mt76.mutex);
			mt76_testmode_reset(phy->mt76, false);
			mutex_unlock(&dev->mt76.mutex);
		}
#endif
		ieee80211_stop_queues(hw);
		ret = mt7915_set_channel(phy);
		if (ret)
			return ret;
		ieee80211_wake_queues(hw);
	}

	if (changed & (IEEE80211_CONF_CHANGE_POWER |
		       IEEE80211_CONF_CHANGE_CHANNEL)) {
		ret = mt7915_mcu_set_txpower_sku(phy);
		if (ret)
			return ret;
	}

	mutex_lock(&dev->mt76.mutex);

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		bool enabled = !!(hw->conf.flags & IEEE80211_CONF_MONITOR);
		bool band = phy->mt76->band_idx;
		u32 rxfilter = phy->rxfilter;

		if (!enabled) {
			rxfilter |= MT_WF_RFCR_DROP_OTHER_UC;
			dev->monitor_mask &= ~BIT(band);
		} else {
			rxfilter &= ~MT_WF_RFCR_DROP_OTHER_UC;
			dev->monitor_mask |= BIT(band);
		}

		mt76_rmw_field(dev, MT_DMA_DCR0(band), MT_DMA_DCR0_RXD_G5_EN,
			       enabled);
		mt76_rmw_field(dev, MT_DMA_DCR0(band), MT_MDP_DCR0_RX_HDR_TRANS_EN,
			       !dev->monitor_mask);
		mt76_testmode_reset(phy->mt76, true);
		mt76_wr(dev, MT_WF_RFCR(band), rxfilter);
	}

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static int
mt7915_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       unsigned int link_id, u16 queue,
	       const struct ieee80211_tx_queue_params *params)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;

	/* no need to update right away, we'll get BSS_CHANGED_QOS */
	queue = mt76_connac_lmac_mapping(queue);
	mvif->queue_params[queue] = *params;

	return 0;
}

static void mt7915_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	bool band = phy->mt76->band_idx;
	u32 ctl_flags = MT_WF_RFCR1_DROP_ACK |
			MT_WF_RFCR1_DROP_BF_POLL |
			MT_WF_RFCR1_DROP_BA |
			MT_WF_RFCR1_DROP_CFEND |
			MT_WF_RFCR1_DROP_CFACK;
	u32 rxfilter;
	u32 flags = 0;

#define MT76_FILTER(_flag, _hw) do {					\
		flags |= *total_flags & FIF_##_flag;			\
		phy->rxfilter &= ~(_hw);				\
		phy->rxfilter |= !(flags & FIF_##_flag) * (_hw);	\
	} while (0)

	mutex_lock(&dev->mt76.mutex);

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
	rxfilter = phy->rxfilter;
	if (hw->conf.flags & IEEE80211_CONF_MONITOR)
		rxfilter &= ~MT_WF_RFCR_DROP_OTHER_UC;
	else
		rxfilter |= MT_WF_RFCR_DROP_OTHER_UC;
	mt76_wr(dev, MT_WF_RFCR(band), rxfilter);

	if (*total_flags & FIF_CONTROL)
		mt76_clear(dev, MT_WF_RFCR1(band), ctl_flags);
	else
		mt76_set(dev, MT_WF_RFCR1(band), ctl_flags);

	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7915_update_bss_color(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			struct cfg80211_he_bss_color *bss_color)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);

	switch (vif->type) {
	case NL80211_IFTYPE_AP: {
		struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;

		if (mvif->mt76.omac_idx > HW_BSSID_MAX)
			return;
		fallthrough;
	}
	case NL80211_IFTYPE_STATION:
		mt7915_mcu_update_bss_color(dev, vif, bss_color);
		break;
	default:
		break;
	}
}

static void mt7915_bss_info_changed(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *info,
				    u64 changed)
{
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	int set_bss_info = -1, set_sta = -1;

	mutex_lock(&dev->mt76.mutex);

	/*
	 * station mode uses BSSID to map the wlan entry to a peer,
	 * and then peer references bss_info_rfch to set bandwidth cap.
	 */
	if (changed & BSS_CHANGED_BSSID &&
	    vif->type == NL80211_IFTYPE_STATION)
		set_bss_info = set_sta = !is_zero_ether_addr(info->bssid);
	if (changed & BSS_CHANGED_ASSOC)
		set_bss_info = vif->cfg.assoc;
	if (changed & BSS_CHANGED_BEACON_ENABLED &&
	    vif->type != NL80211_IFTYPE_AP)
		set_bss_info = set_sta = info->enable_beacon;

	if (set_bss_info == 1)
		mt7915_mcu_add_bss_info(phy, vif, true);
	if (set_sta == 1)
		mt7915_mcu_add_sta(dev, vif, NULL, true);

	if (changed & BSS_CHANGED_ERP_CTS_PROT)
		mt7915_mac_enable_rtscts(dev, vif, info->use_cts_prot);

	if (changed & BSS_CHANGED_ERP_SLOT) {
		int slottime = info->use_short_slot ? 9 : 20;

		if (slottime != phy->slottime) {
			phy->slottime = slottime;
			mt7915_mac_set_timing(phy);
		}
	}

	/* ensure that enable txcmd_mode after bss_info */
	if (changed & (BSS_CHANGED_QOS | BSS_CHANGED_BEACON_ENABLED))
		mt7915_mcu_set_tx(dev, vif);

	if (changed & BSS_CHANGED_HE_OBSS_PD)
		mt7915_mcu_add_obss_spr(phy, vif, &info->he_obss_pd);

	if (changed & BSS_CHANGED_HE_BSS_COLOR)
		mt7915_update_bss_color(hw, vif, &info->he_bss_color);

	if (changed & (BSS_CHANGED_BEACON |
		       BSS_CHANGED_BEACON_ENABLED))
		mt7915_mcu_add_beacon(hw, vif, info->enable_beacon, changed);

	if (changed & (BSS_CHANGED_UNSOL_BCAST_PROBE_RESP |
		       BSS_CHANGED_FILS_DISCOVERY))
		mt7915_mcu_add_inband_discov(dev, vif, changed);

	if (set_bss_info == 0)
		mt7915_mcu_add_bss_info(phy, vif, false);
	if (set_sta == 0)
		mt7915_mcu_add_sta(dev, vif, NULL, false);

	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7915_vif_check_caps(struct mt7915_phy *phy, struct ieee80211_vif *vif)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_vif_cap *vc = &mvif->cap;

	vc->ht_ldpc = vif->bss_conf.ht_ldpc;
	vc->vht_ldpc = vif->bss_conf.vht_ldpc;
	vc->vht_su_ebfer = vif->bss_conf.vht_su_beamformer;
	vc->vht_su_ebfee = vif->bss_conf.vht_su_beamformee;
	vc->vht_mu_ebfer = vif->bss_conf.vht_mu_beamformer;
	vc->vht_mu_ebfee = vif->bss_conf.vht_mu_beamformee;
	vc->he_ldpc = vif->bss_conf.he_ldpc;
	vc->he_su_ebfer = vif->bss_conf.he_su_beamformer;
	vc->he_su_ebfee = vif->bss_conf.he_su_beamformee;
	vc->he_mu_ebfer = vif->bss_conf.he_mu_beamformer;
}

static int
mt7915_start_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_bss_conf *link_conf)
{
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	int err;

	mutex_lock(&dev->mt76.mutex);

	mt7915_vif_check_caps(phy, vif);

	err = mt7915_mcu_add_bss_info(phy, vif, true);
	if (err)
		goto out;
	err = mt7915_mcu_add_sta(dev, vif, NULL, true);
out:
	mutex_unlock(&dev->mt76.mutex);

	return err;
}

static void
mt7915_stop_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       struct ieee80211_bss_conf *link_conf)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);
	mt7915_mcu_add_sta(dev, vif, NULL, false);
	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7915_channel_switch_beacon(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct cfg80211_chan_def *chandef)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);
	mt7915_mcu_add_beacon(hw, vif, true, BSS_CHANGED_BEACON);
	mutex_unlock(&dev->mt76.mutex);
}

int mt7915_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	bool ext_phy = mvif->phy != &dev->phy;
	int ret, idx;

	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7915_WTBL_STA);
	if (idx < 0)
		return -ENOSPC;

	INIT_LIST_HEAD(&msta->rc_list);
	INIT_LIST_HEAD(&msta->wcid.poll_list);
	msta->vif = mvif;
	msta->wcid.sta = 1;
	msta->wcid.idx = idx;
	msta->wcid.phy_idx = ext_phy;
	msta->wcid.tx_info |= MT_WCID_TX_INFO_SET;
	msta->jiffies = jiffies;

	ewma_avg_signal_init(&msta->avg_ack_signal);

	mt7915_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	ret = mt7915_mcu_add_sta(dev, vif, sta, true);
	if (ret)
		return ret;

	return mt7915_mcu_add_rate_ctrl(dev, vif, sta, false);
}

void mt7915_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	int i;

	mt7915_mcu_add_sta(dev, vif, sta, false);

	mt7915_mac_wtbl_update(dev, msta->wcid.idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	for (i = 0; i < ARRAY_SIZE(msta->twt.flow); i++)
		mt7915_mac_twt_teardown_flow(dev, msta, i);

	spin_lock_bh(&mdev->sta_poll_lock);
	if (!list_empty(&msta->wcid.poll_list))
		list_del_init(&msta->wcid.poll_list);
	if (!list_empty(&msta->rc_list))
		list_del_init(&msta->rc_list);
	spin_unlock_bh(&mdev->sta_poll_lock);
}

static void mt7915_tx(struct ieee80211_hw *hw,
		      struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;

	if (control->sta) {
		struct mt7915_sta *sta;

		sta = (struct mt7915_sta *)control->sta->drv_priv;
		wcid = &sta->wcid;
	}

	if (vif && !control->sta) {
		struct mt7915_vif *mvif;

		mvif = (struct mt7915_vif *)vif->drv_priv;
		wcid = &mvif->sta.wcid;
	}

	mt76_tx(mphy, control->sta, wcid, skb);
}

static int mt7915_set_rts_threshold(struct ieee80211_hw *hw, u32 val)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	int ret;

	mutex_lock(&dev->mt76.mutex);
	ret = mt76_connac_mcu_set_rts_thresh(&dev->mt76, val,
					     phy->mt76->band_idx);
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7915_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params)
{
	enum ieee80211_ampdu_mlme_action action = params->action;
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct ieee80211_sta *sta = params->sta;
	struct ieee80211_txq *txq = sta->txq[params->tid];
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
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
		ret = mt7915_mcu_add_rx_ba(dev, params, true);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		mt76_rx_aggr_stop(&dev->mt76, &msta->wcid, tid);
		ret = mt7915_mcu_add_rx_ba(dev, params, false);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		mtxq->aggr = true;
		mtxq->send_bar = false;
		ret = mt7915_mcu_add_tx_ba(dev, params, true);
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		mtxq->aggr = false;
		clear_bit(tid, &msta->wcid.ampdu_state);
		ret = mt7915_mcu_add_tx_ba(dev, params, false);
		break;
	case IEEE80211_AMPDU_TX_START:
		set_bit(tid, &msta->wcid.ampdu_state);
		ret = IEEE80211_AMPDU_TX_START_IMMEDIATE;
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		mtxq->aggr = false;
		clear_bit(tid, &msta->wcid.ampdu_state);
		ret = mt7915_mcu_add_tx_ba(dev, params, false);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	}
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7915_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       struct ieee80211_sta *sta)
{
	return mt76_sta_state(hw, vif, sta, IEEE80211_STA_NOTEXIST,
			      IEEE80211_STA_NONE);
}

static int
mt7915_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  struct ieee80211_sta *sta)
{
	return mt76_sta_state(hw, vif, sta, IEEE80211_STA_NONE,
			      IEEE80211_STA_NOTEXIST);
}

static int
mt7915_get_stats(struct ieee80211_hw *hw,
		 struct ieee80211_low_level_stats *stats)
{
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt76_mib_stats *mib = &phy->mib;

	mutex_lock(&dev->mt76.mutex);

	stats->dot11RTSSuccessCount = mib->rts_cnt;
	stats->dot11RTSFailureCount = mib->rts_retries_cnt;
	stats->dot11FCSErrorCount = mib->fcs_err_cnt;
	stats->dot11ACKFailureCount = mib->ack_fail_cnt;

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

u64 __mt7915_get_tsf(struct ieee80211_hw *hw, struct mt7915_vif *mvif)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	bool band = phy->mt76->band_idx;
	union {
		u64 t64;
		u32 t32[2];
	} tsf;
	u16 n;

	lockdep_assert_held(&dev->mt76.mutex);

	n = mvif->mt76.omac_idx > HW_BSSID_MAX ? HW_BSSID_0
					       : mvif->mt76.omac_idx;
	/* TSF software read */
	if (is_mt7915(&dev->mt76))
		mt76_rmw(dev, MT_LPON_TCR(band, n), MT_LPON_TCR_SW_MODE,
			 MT_LPON_TCR_SW_READ);
	else
		mt76_rmw(dev, MT_LPON_TCR_MT7916(band, n), MT_LPON_TCR_SW_MODE,
			 MT_LPON_TCR_SW_READ);
	tsf.t32[0] = mt76_rr(dev, MT_LPON_UTTR0(band));
	tsf.t32[1] = mt76_rr(dev, MT_LPON_UTTR1(band));

	return tsf.t64;
}

static u64
mt7915_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	u64 ret;

	mutex_lock(&dev->mt76.mutex);
	ret = __mt7915_get_tsf(hw, mvif);
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void
mt7915_set_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       u64 timestamp)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	bool band = phy->mt76->band_idx;
	union {
		u64 t64;
		u32 t32[2];
	} tsf = { .t64 = timestamp, };
	u16 n;

	mutex_lock(&dev->mt76.mutex);

	n = mvif->mt76.omac_idx > HW_BSSID_MAX ? HW_BSSID_0
					       : mvif->mt76.omac_idx;
	mt76_wr(dev, MT_LPON_UTTR0(band), tsf.t32[0]);
	mt76_wr(dev, MT_LPON_UTTR1(band), tsf.t32[1]);
	/* TSF software overwrite */
	if (is_mt7915(&dev->mt76))
		mt76_rmw(dev, MT_LPON_TCR(band, n), MT_LPON_TCR_SW_MODE,
			 MT_LPON_TCR_SW_WRITE);
	else
		mt76_rmw(dev, MT_LPON_TCR_MT7916(band, n), MT_LPON_TCR_SW_MODE,
			 MT_LPON_TCR_SW_WRITE);

	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7915_offset_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  s64 timestamp)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	bool band = phy->mt76->band_idx;
	union {
		u64 t64;
		u32 t32[2];
	} tsf = { .t64 = timestamp, };
	u16 n;

	mutex_lock(&dev->mt76.mutex);

	n = mvif->mt76.omac_idx > HW_BSSID_MAX ? HW_BSSID_0
					       : mvif->mt76.omac_idx;
	mt76_wr(dev, MT_LPON_UTTR0(band), tsf.t32[0]);
	mt76_wr(dev, MT_LPON_UTTR1(band), tsf.t32[1]);
	/* TSF software adjust*/
	if (is_mt7915(&dev->mt76))
		mt76_rmw(dev, MT_LPON_TCR(band, n), MT_LPON_TCR_SW_MODE,
			 MT_LPON_TCR_SW_ADJUST);
	else
		mt76_rmw(dev, MT_LPON_TCR_MT7916(band, n), MT_LPON_TCR_SW_MODE,
			 MT_LPON_TCR_SW_ADJUST);

	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7915_set_coverage_class(struct ieee80211_hw *hw, s16 coverage_class)
{
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_dev *dev = phy->dev;

	mutex_lock(&dev->mt76.mutex);
	phy->coverage_class = max_t(s16, coverage_class, 0);
	mt7915_mac_set_timing(phy);
	mutex_unlock(&dev->mt76.mutex);
}

static int
mt7915_set_antenna(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	int max_nss = hweight8(hw->wiphy->available_antennas_tx);
	u8 chainshift = dev->chainshift;
	u8 band = phy->mt76->band_idx;

	if (!tx_ant || tx_ant != rx_ant || ffs(tx_ant) > max_nss)
		return -EINVAL;

	mutex_lock(&dev->mt76.mutex);

	phy->mt76->antenna_mask = tx_ant;

	/* handle a variant of mt7916/mt7981 which has 3T3R but nss2 on 5 GHz band */
	if ((is_mt7916(&dev->mt76) || is_mt7981(&dev->mt76)) &&
	    band && hweight8(tx_ant) == max_nss)
		phy->mt76->chainmask = (dev->chainmask >> chainshift) << chainshift;
	else
		phy->mt76->chainmask = tx_ant << (chainshift * band);

	mt76_set_stream_caps(phy->mt76, true);
	mt7915_set_stream_vht_txbf_caps(phy);
	mt7915_set_stream_he_caps(phy);

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static void mt7915_sta_statistics(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  struct station_info *sinfo)
{
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct rate_info *txrate = &msta->wcid.rate;
	struct rate_info rxrate = {};

	if (is_mt7915(&phy->dev->mt76) &&
	    !mt7915_mcu_get_rx_rate(phy, vif, sta, &rxrate)) {
		sinfo->rxrate = rxrate;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BITRATE);
	}

	if (txrate->legacy || txrate->flags) {
		if (txrate->legacy) {
			sinfo->txrate.legacy = txrate->legacy;
		} else {
			sinfo->txrate.mcs = txrate->mcs;
			sinfo->txrate.nss = txrate->nss;
			sinfo->txrate.bw = txrate->bw;
			sinfo->txrate.he_gi = txrate->he_gi;
			sinfo->txrate.he_dcm = txrate->he_dcm;
			sinfo->txrate.he_ru_alloc = txrate->he_ru_alloc;
		}
		sinfo->txrate.flags = txrate->flags;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
	}

	/* offloading flows bypass networking stack, so driver counts and
	 * reports sta statistics via NL80211_STA_INFO when WED is active.
	 */
	if (mtk_wed_device_active(&phy->dev->mt76.mmio.wed)) {
		sinfo->tx_bytes = msta->wcid.stats.tx_bytes;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BYTES64);

		if (!mt7915_mcu_wed_wa_tx_stats(phy->dev, msta->wcid.idx)) {
			sinfo->tx_packets = msta->wcid.stats.tx_packets;
			sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_PACKETS);
		}

		if (mtk_wed_get_rx_capa(&phy->dev->mt76.mmio.wed)) {
			sinfo->rx_bytes = msta->wcid.stats.rx_bytes;
			sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BYTES64);

			sinfo->rx_packets = msta->wcid.stats.rx_packets;
			sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_PACKETS);
		}
	}

	sinfo->tx_failed = msta->wcid.stats.tx_failed;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_FAILED);

	sinfo->tx_retries = msta->wcid.stats.tx_retries;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_RETRIES);

	sinfo->ack_signal = (s8)msta->ack_signal;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL);

	sinfo->avg_ack_signal = -(s8)ewma_avg_signal_read(&msta->avg_ack_signal);
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL_AVG);
}

static void mt7915_sta_rc_work(void *data, struct ieee80211_sta *sta)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_dev *dev = msta->vif->phy->dev;
	u32 *changed = data;

	spin_lock_bh(&dev->mt76.sta_poll_lock);
	msta->changed |= *changed;
	if (list_empty(&msta->rc_list))
		list_add_tail(&msta->rc_list, &dev->sta_rc_list);
	spin_unlock_bh(&dev->mt76.sta_poll_lock);
}

static void mt7915_sta_rc_update(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 u32 changed)
{
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_dev *dev = phy->dev;

	mt7915_sta_rc_work(&changed, sta);
	ieee80211_queue_work(hw, &dev->rc_work);
}

static int
mt7915_set_bitrate_mask(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			const struct cfg80211_bitrate_mask *mask)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_dev *dev = phy->dev;
	u32 changed = IEEE80211_RC_SUPP_RATES_CHANGED;

	mvif->bitrate_mask = *mask;

	/* if multiple rates across different preambles are given we can
	 * reconfigure this info with all peers using sta_rec command with
	 * the below exception cases.
	 * - single rate : if a rate is passed along with different preambles,
	 * we select the highest one as fixed rate. i.e VHT MCS for VHT peers.
	 * - multiple rates: if it's not in range format i.e 0-{7,8,9} for VHT
	 * then multiple MCS setting (MCS 4,5,6) is not supported.
	 */
	ieee80211_iterate_stations_atomic(hw, mt7915_sta_rc_work, &changed);
	ieee80211_queue_work(hw, &dev->rc_work);

	return 0;
}

static void mt7915_sta_set_4addr(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 bool enabled)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;

	if (enabled)
		set_bit(MT_WCID_FLAG_4ADDR, &msta->wcid.flags);
	else
		clear_bit(MT_WCID_FLAG_4ADDR, &msta->wcid.flags);

	mt76_connac_mcu_wtbl_update_hdr_trans(&dev->mt76, vif, sta);
}

static void mt7915_sta_set_decap_offload(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 bool enabled)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;

	if (enabled)
		set_bit(MT_WCID_FLAG_HDR_TRANS, &msta->wcid.flags);
	else
		clear_bit(MT_WCID_FLAG_HDR_TRANS, &msta->wcid.flags);

	mt76_connac_mcu_wtbl_update_hdr_trans(&dev->mt76, vif, sta);
}

static int mt7915_sta_set_txpwr(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta)
{
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	s16 txpower = sta->deflink.txpwr.power;
	int ret;

	if (sta->deflink.txpwr.type == NL80211_TX_POWER_AUTOMATIC)
		txpower = 0;

	mutex_lock(&dev->mt76.mutex);

	/* NOTE: temporarily use 0 as minimum limit, which is a
	 * global setting and will be applied to all stations.
	 */
	ret = mt7915_mcu_set_txpower_frame_min(phy, 0);
	if (ret)
		goto out;

	/* This only applies to data frames while pushing traffic,
	 * whereas the management frames or other packets that are
	 * using fixed rate can be configured via TxD.
	 */
	ret = mt7915_mcu_set_txpower_frame(phy, vif, sta, txpower);

out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static const char mt7915_gstrings_stats[][ETH_GSTRING_LEN] = {
	"tx_ampdu_cnt",
	"tx_stop_q_empty_cnt",
	"tx_mpdu_attempts",
	"tx_mpdu_success",
	"tx_rwp_fail_cnt",
	"tx_rwp_need_cnt",
	"tx_pkt_ebf_cnt",
	"tx_pkt_ibf_cnt",
	"tx_ampdu_len:0-1",
	"tx_ampdu_len:2-10",
	"tx_ampdu_len:11-19",
	"tx_ampdu_len:20-28",
	"tx_ampdu_len:29-37",
	"tx_ampdu_len:38-46",
	"tx_ampdu_len:47-55",
	"tx_ampdu_len:56-79",
	"tx_ampdu_len:80-103",
	"tx_ampdu_len:104-127",
	"tx_ampdu_len:128-151",
	"tx_ampdu_len:152-175",
	"tx_ampdu_len:176-199",
	"tx_ampdu_len:200-223",
	"tx_ampdu_len:224-247",
	"ba_miss_count",
	"tx_beamformer_ppdu_iBF",
	"tx_beamformer_ppdu_eBF",
	"tx_beamformer_rx_feedback_all",
	"tx_beamformer_rx_feedback_he",
	"tx_beamformer_rx_feedback_vht",
	"tx_beamformer_rx_feedback_ht",
	"tx_beamformer_rx_feedback_bw", /* zero based idx: 20, 40, 80, 160 */
	"tx_beamformer_rx_feedback_nc",
	"tx_beamformer_rx_feedback_nr",
	"tx_beamformee_ok_feedback_pkts",
	"tx_beamformee_feedback_trig",
	"tx_mu_beamforming",
	"tx_mu_mpdu",
	"tx_mu_successful_mpdu",
	"tx_su_successful_mpdu",
	"tx_msdu_pack_1",
	"tx_msdu_pack_2",
	"tx_msdu_pack_3",
	"tx_msdu_pack_4",
	"tx_msdu_pack_5",
	"tx_msdu_pack_6",
	"tx_msdu_pack_7",
	"tx_msdu_pack_8",

	/* rx counters */
	"rx_fifo_full_cnt",
	"rx_mpdu_cnt",
	"channel_idle_cnt",
	"primary_cca_busy_time",
	"secondary_cca_busy_time",
	"primary_energy_detect_time",
	"cck_mdrdy_time",
	"ofdm_mdrdy_time",
	"green_mdrdy_time",
	"rx_vector_mismatch_cnt",
	"rx_delimiter_fail_cnt",
	"rx_mrdy_cnt",
	"rx_len_mismatch_cnt",
	"rx_ampdu_cnt",
	"rx_ampdu_bytes_cnt",
	"rx_ampdu_valid_subframe_cnt",
	"rx_ampdu_valid_subframe_b_cnt",
	"rx_pfdrop_cnt",
	"rx_vec_queue_overflow_drop_cnt",
	"rx_ba_cnt",

	/* muru mu-mimo and ofdma related stats */
	"dl_cck_cnt",
	"dl_ofdm_cnt",
	"dl_htmix_cnt",
	"dl_htgf_cnt",
	"dl_vht_su_cnt",
	"dl_vht_2mu_cnt",
	"dl_vht_3mu_cnt",
	"dl_vht_4mu_cnt",
	"dl_he_su_cnt",
	"dl_he_ext_su_cnt",
	"dl_he_2ru_cnt",
	"dl_he_2mu_cnt",
	"dl_he_3ru_cnt",
	"dl_he_3mu_cnt",
	"dl_he_4ru_cnt",
	"dl_he_4mu_cnt",
	"dl_he_5to8ru_cnt",
	"dl_he_9to16ru_cnt",
	"dl_he_gtr16ru_cnt",

	"ul_hetrig_su_cnt",
	"ul_hetrig_2ru_cnt",
	"ul_hetrig_3ru_cnt",
	"ul_hetrig_4ru_cnt",
	"ul_hetrig_5to8ru_cnt",
	"ul_hetrig_9to16ru_cnt",
	"ul_hetrig_gtr16ru_cnt",
	"ul_hetrig_2mu_cnt",
	"ul_hetrig_3mu_cnt",
	"ul_hetrig_4mu_cnt",

	/* per vif counters */
	"v_tx_mode_cck",
	"v_tx_mode_ofdm",
	"v_tx_mode_ht",
	"v_tx_mode_ht_gf",
	"v_tx_mode_vht",
	"v_tx_mode_he_su",
	"v_tx_mode_he_ext_su",
	"v_tx_mode_he_tb",
	"v_tx_mode_he_mu",
	"v_tx_bw_20",
	"v_tx_bw_40",
	"v_tx_bw_80",
	"v_tx_bw_160",
	"v_tx_mcs_0",
	"v_tx_mcs_1",
	"v_tx_mcs_2",
	"v_tx_mcs_3",
	"v_tx_mcs_4",
	"v_tx_mcs_5",
	"v_tx_mcs_6",
	"v_tx_mcs_7",
	"v_tx_mcs_8",
	"v_tx_mcs_9",
	"v_tx_mcs_10",
	"v_tx_mcs_11",
	"v_tx_nss_1",
	"v_tx_nss_2",
	"v_tx_nss_3",
	"v_tx_nss_4",
};

#define MT7915_SSTATS_LEN ARRAY_SIZE(mt7915_gstrings_stats)

/* Ethtool related API */
static
void mt7915_get_et_strings(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   u32 sset, u8 *data)
{
	if (sset != ETH_SS_STATS)
		return;

	memcpy(data, mt7915_gstrings_stats, sizeof(mt7915_gstrings_stats));
	data += sizeof(mt7915_gstrings_stats);
	page_pool_ethtool_stats_get_strings(data);
}

static
int mt7915_get_et_sset_count(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif, int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	return MT7915_SSTATS_LEN + page_pool_ethtool_stats_get_count();
}

static void mt7915_ethtool_worker(void *wi_data, struct ieee80211_sta *sta)
{
	struct mt76_ethtool_worker_info *wi = wi_data;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;

	if (msta->vif->mt76.idx != wi->idx)
		return;

	mt76_ethtool_worker(wi, &msta->wcid.stats, false);
}

static
void mt7915_get_et_stats(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 struct ethtool_stats *stats, u64 *data)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt76_mib_stats *mib = &phy->mib;
	struct mt76_ethtool_worker_info wi = {
		.data = data,
		.idx = mvif->mt76.idx,
	};
	/* See mt7915_ampdu_stat_read_phy, etc */
	int i, ei = 0, stats_size;

	mutex_lock(&dev->mt76.mutex);

	mt7915_mac_update_stats(phy);

	data[ei++] = mib->tx_ampdu_cnt;
	data[ei++] = mib->tx_stop_q_empty_cnt;
	data[ei++] = mib->tx_mpdu_attempts_cnt;
	data[ei++] = mib->tx_mpdu_success_cnt;
	data[ei++] = mib->tx_rwp_fail_cnt;
	data[ei++] = mib->tx_rwp_need_cnt;
	data[ei++] = mib->tx_pkt_ebf_cnt;
	data[ei++] = mib->tx_pkt_ibf_cnt;

	/* Tx ampdu stat */
	for (i = 0; i < 15 /*ARRAY_SIZE(bound)*/; i++)
		data[ei++] = phy->mt76->aggr_stats[i];

	data[ei++] = phy->mib.ba_miss_cnt;

	/* Tx Beamformer monitor */
	data[ei++] = mib->tx_bf_ibf_ppdu_cnt;
	data[ei++] = mib->tx_bf_ebf_ppdu_cnt;

	/* Tx Beamformer Rx feedback monitor */
	data[ei++] = mib->tx_bf_rx_fb_all_cnt;
	data[ei++] = mib->tx_bf_rx_fb_he_cnt;
	data[ei++] = mib->tx_bf_rx_fb_vht_cnt;
	data[ei++] = mib->tx_bf_rx_fb_ht_cnt;

	data[ei++] = mib->tx_bf_rx_fb_bw;
	data[ei++] = mib->tx_bf_rx_fb_nc_cnt;
	data[ei++] = mib->tx_bf_rx_fb_nr_cnt;

	/* Tx Beamformee Rx NDPA & Tx feedback report */
	data[ei++] = mib->tx_bf_fb_cpl_cnt;
	data[ei++] = mib->tx_bf_fb_trig_cnt;

	/* Tx SU & MU counters */
	data[ei++] = mib->tx_bf_cnt;
	data[ei++] = mib->tx_mu_mpdu_cnt;
	data[ei++] = mib->tx_mu_acked_mpdu_cnt;
	data[ei++] = mib->tx_su_acked_mpdu_cnt;

	/* Tx amsdu info (pack-count histogram) */
	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++)
		data[ei++] = mib->tx_amsdu[i];

	/* rx counters */
	data[ei++] = mib->rx_fifo_full_cnt;
	data[ei++] = mib->rx_mpdu_cnt;
	data[ei++] = mib->channel_idle_cnt;
	data[ei++] = mib->primary_cca_busy_time;
	data[ei++] = mib->secondary_cca_busy_time;
	data[ei++] = mib->primary_energy_detect_time;
	data[ei++] = mib->cck_mdrdy_time;
	data[ei++] = mib->ofdm_mdrdy_time;
	data[ei++] = mib->green_mdrdy_time;
	data[ei++] = mib->rx_vector_mismatch_cnt;
	data[ei++] = mib->rx_delimiter_fail_cnt;
	data[ei++] = mib->rx_mrdy_cnt;
	data[ei++] = mib->rx_len_mismatch_cnt;
	data[ei++] = mib->rx_ampdu_cnt;
	data[ei++] = mib->rx_ampdu_bytes_cnt;
	data[ei++] = mib->rx_ampdu_valid_subframe_cnt;
	data[ei++] = mib->rx_ampdu_valid_subframe_bytes_cnt;
	data[ei++] = mib->rx_pfdrop_cnt;
	data[ei++] = mib->rx_vec_queue_overflow_drop_cnt;
	data[ei++] = mib->rx_ba_cnt;

	data[ei++] = mib->dl_cck_cnt;
	data[ei++] = mib->dl_ofdm_cnt;
	data[ei++] = mib->dl_htmix_cnt;
	data[ei++] = mib->dl_htgf_cnt;
	data[ei++] = mib->dl_vht_su_cnt;
	data[ei++] = mib->dl_vht_2mu_cnt;
	data[ei++] = mib->dl_vht_3mu_cnt;
	data[ei++] = mib->dl_vht_4mu_cnt;
	data[ei++] = mib->dl_he_su_cnt;
	data[ei++] = mib->dl_he_ext_su_cnt;
	data[ei++] = mib->dl_he_2ru_cnt;
	data[ei++] = mib->dl_he_2mu_cnt;
	data[ei++] = mib->dl_he_3ru_cnt;
	data[ei++] = mib->dl_he_3mu_cnt;
	data[ei++] = mib->dl_he_4ru_cnt;
	data[ei++] = mib->dl_he_4mu_cnt;
	data[ei++] = mib->dl_he_5to8ru_cnt;
	data[ei++] = mib->dl_he_9to16ru_cnt;
	data[ei++] = mib->dl_he_gtr16ru_cnt;

	data[ei++] = mib->ul_hetrig_su_cnt;
	data[ei++] = mib->ul_hetrig_2ru_cnt;
	data[ei++] = mib->ul_hetrig_3ru_cnt;
	data[ei++] = mib->ul_hetrig_4ru_cnt;
	data[ei++] = mib->ul_hetrig_5to8ru_cnt;
	data[ei++] = mib->ul_hetrig_9to16ru_cnt;
	data[ei++] = mib->ul_hetrig_gtr16ru_cnt;
	data[ei++] = mib->ul_hetrig_2mu_cnt;
	data[ei++] = mib->ul_hetrig_3mu_cnt;
	data[ei++] = mib->ul_hetrig_4mu_cnt;

	/* Add values for all stations owned by this vif */
	wi.initial_stat_idx = ei;
	ieee80211_iterate_stations_atomic(hw, mt7915_ethtool_worker, &wi);

	mutex_unlock(&dev->mt76.mutex);

	if (wi.sta_count == 0)
		return;

	ei += wi.worker_stat_count;

	mt76_ethtool_page_pool_stats(&dev->mt76, &data[ei], &ei);

	stats_size = MT7915_SSTATS_LEN + page_pool_ethtool_stats_get_count();
	if (ei != stats_size)
		dev_err(dev->mt76.dev, "ei: %d size: %d", ei, stats_size);
}

static void
mt7915_twt_teardown_request(struct ieee80211_hw *hw,
			    struct ieee80211_sta *sta,
			    u8 flowid)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_dev *dev = mt7915_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);
	mt7915_mac_twt_teardown_flow(dev, msta, flowid);
	mutex_unlock(&dev->mt76.mutex);
}

static int
mt7915_set_radar_background(struct ieee80211_hw *hw,
			    struct cfg80211_chan_def *chandef)
{
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_dev *dev = phy->dev;
	int ret = -EINVAL;
	bool running;

	mutex_lock(&dev->mt76.mutex);

	if (dev->mt76.region == NL80211_DFS_UNSET)
		goto out;

	if (dev->rdd2_phy && dev->rdd2_phy != phy) {
		/* rdd2 is already locked */
		ret = -EBUSY;
		goto out;
	}

	/* rdd2 already configured on a radar channel */
	running = dev->rdd2_phy &&
		  cfg80211_chandef_valid(&dev->rdd2_chandef) &&
		  !!(dev->rdd2_chandef.chan->flags & IEEE80211_CHAN_RADAR);

	if (!chandef || running ||
	    !(chandef->chan->flags & IEEE80211_CHAN_RADAR)) {
		ret = mt7915_mcu_rdd_background_enable(phy, NULL);
		if (ret)
			goto out;

		if (!running)
			goto update_phy;
	}

	ret = mt7915_mcu_rdd_background_enable(phy, chandef);
	if (ret)
		goto out;

update_phy:
	dev->rdd2_phy = chandef ? phy : NULL;
	if (chandef)
		dev->rdd2_chandef = *chandef;
out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

#ifdef CONFIG_NET_MEDIATEK_SOC_WED
static int
mt7915_net_fill_forward_path(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct net_device_path_ctx *ctx,
			     struct net_device_path *path)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;

	if (!mtk_wed_device_active(wed))
		return -ENODEV;

	if (msta->wcid.idx > 0xff)
		return -EIO;

	path->type = DEV_PATH_MTK_WDMA;
	path->dev = ctx->dev;
	path->mtk_wdma.wdma_idx = wed->wdma_idx;
	path->mtk_wdma.bss = mvif->mt76.idx;
	path->mtk_wdma.wcid = is_mt7915(&dev->mt76) ? msta->wcid.idx : 0x3ff;
	path->mtk_wdma.queue = phy != &dev->phy;

	ctx->dev = NULL;

	return 0;
}
#endif

const struct ieee80211_ops mt7915_ops = {
	.tx = mt7915_tx,
	.start = mt7915_start,
	.stop = mt7915_stop,
	.add_interface = mt7915_add_interface,
	.remove_interface = mt7915_remove_interface,
	.config = mt7915_config,
	.conf_tx = mt7915_conf_tx,
	.configure_filter = mt7915_configure_filter,
	.bss_info_changed = mt7915_bss_info_changed,
	.start_ap = mt7915_start_ap,
	.stop_ap = mt7915_stop_ap,
	.sta_add = mt7915_sta_add,
	.sta_remove = mt7915_sta_remove,
	.sta_pre_rcu_remove = mt76_sta_pre_rcu_remove,
	.sta_rc_update = mt7915_sta_rc_update,
	.set_key = mt7915_set_key,
	.ampdu_action = mt7915_ampdu_action,
	.set_rts_threshold = mt7915_set_rts_threshold,
	.wake_tx_queue = mt76_wake_tx_queue,
	.sw_scan_start = mt76_sw_scan,
	.sw_scan_complete = mt76_sw_scan_complete,
	.release_buffered_frames = mt76_release_buffered_frames,
	.get_txpower = mt76_get_txpower,
	.set_sar_specs = mt7915_set_sar_specs,
	.channel_switch_beacon = mt7915_channel_switch_beacon,
	.get_stats = mt7915_get_stats,
	.get_et_sset_count = mt7915_get_et_sset_count,
	.get_et_stats = mt7915_get_et_stats,
	.get_et_strings = mt7915_get_et_strings,
	.get_tsf = mt7915_get_tsf,
	.set_tsf = mt7915_set_tsf,
	.offset_tsf = mt7915_offset_tsf,
	.get_survey = mt76_get_survey,
	.get_antenna = mt76_get_antenna,
	.set_antenna = mt7915_set_antenna,
	.set_bitrate_mask = mt7915_set_bitrate_mask,
	.set_coverage_class = mt7915_set_coverage_class,
	.sta_statistics = mt7915_sta_statistics,
	.sta_set_txpwr = mt7915_sta_set_txpwr,
	.sta_set_4addr = mt7915_sta_set_4addr,
	.sta_set_decap_offload = mt7915_sta_set_decap_offload,
	.add_twt_setup = mt7915_mac_add_twt_setup,
	.twt_teardown_request = mt7915_twt_teardown_request,
	CFG80211_TESTMODE_CMD(mt76_testmode_cmd)
	CFG80211_TESTMODE_DUMP(mt76_testmode_dump)
#ifdef CONFIG_MAC80211_DEBUGFS
	.sta_add_debugfs = mt7915_sta_add_debugfs,
#endif
	.set_radar_background = mt7915_set_radar_background,
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	.net_fill_forward_path = mt7915_net_fill_forward_path,
	.net_setup_tc = mt76_net_setup_tc,
#endif
};
