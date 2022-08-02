// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/etherdevice.h>
#include "mt7921.h"
#include "mac.h"
#include "mcu.h"
#include "eeprom.h"

static const struct ieee80211_iface_limit if_limits[] = {
	{
		.max = MT7921_MAX_INTERFACES,
		.types = BIT(NL80211_IFTYPE_STATION)
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_AP)
	}
};

static const struct ieee80211_iface_combination if_comb[] = {
	{
		.limits = if_limits,
		.n_limits = ARRAY_SIZE(if_limits),
		.max_interfaces = MT7921_MAX_INTERFACES,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
	}
};

static void
mt7921_regd_notifier(struct wiphy *wiphy,
		     struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);

	memcpy(dev->mt76.alpha2, request->alpha2, sizeof(dev->mt76.alpha2));
	dev->mt76.region = request->dfs_region;

	mt7921_mutex_acquire(dev);
	mt76_connac_mcu_set_channel_domain(hw->priv);
	mt76_connac_mcu_set_rate_txpower(phy->mt76);
	mt7921_mutex_release(dev);
}

static int
mt7921_init_wiphy(struct ieee80211_hw *hw)
{
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	struct mt7921_dev *dev = phy->dev;
	struct wiphy *wiphy = hw->wiphy;

	hw->queues = 4;
	hw->max_rx_aggregation_subframes = 64;
	hw->max_tx_aggregation_subframes = 128;
	hw->netdev_features = NETIF_F_RXCSUM;

	hw->radiotap_timestamp.units_pos =
		IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US;

	phy->slottime = 9;

	hw->sta_data_size = sizeof(struct mt7921_sta);
	hw->vif_data_size = sizeof(struct mt7921_vif);

	wiphy->iface_combinations = if_comb;
	wiphy->flags &= ~(WIPHY_FLAG_IBSS_RSN | WIPHY_FLAG_4ADDR_AP |
			  WIPHY_FLAG_4ADDR_STATION);
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_AP);
	wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);
	wiphy->max_scan_ie_len = MT76_CONNAC_SCAN_IE_LEN;
	wiphy->max_scan_ssids = 4;
	wiphy->max_sched_scan_plan_interval =
		MT76_CONNAC_MAX_TIME_SCHED_SCAN_INTERVAL;
	wiphy->max_sched_scan_ie_len = IEEE80211_MAX_DATA_LEN;
	wiphy->max_sched_scan_ssids = MT76_CONNAC_MAX_SCHED_SCAN_SSID;
	wiphy->max_match_sets = MT76_CONNAC_MAX_SCAN_MATCH;
	wiphy->max_sched_scan_reqs = 1;
	wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;
	wiphy->reg_notifier = mt7921_regd_notifier;

	wiphy->features |= NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR |
			   NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR;
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SET_SCAN_DWELL);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_LEGACY);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_HT);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_VHT);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_HE);

	ieee80211_hw_set(hw, SINGLE_SCAN_ON_ALL_BANDS);
	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, SUPPORTS_TX_ENCAP_OFFLOAD);
	ieee80211_hw_set(hw, SUPPORTS_RX_DECAP_OFFLOAD);
	ieee80211_hw_set(hw, WANT_MONITOR_VIF);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);

	if (dev->pm.enable)
		ieee80211_hw_set(hw, CONNECTION_MONITOR);

	hw->max_tx_fragments = 4;

	return 0;
}

static void
mt7921_mac_init_band(struct mt7921_dev *dev, u8 band)
{
	mt76_rmw_field(dev, MT_TMAC_CTCR0(band),
		       MT_TMAC_CTCR0_INS_DDLMT_REFTIME, 0x3f);
	mt76_set(dev, MT_TMAC_CTCR0(band),
		 MT_TMAC_CTCR0_INS_DDLMT_VHT_SMPDU_EN |
		 MT_TMAC_CTCR0_INS_DDLMT_EN);

	mt76_set(dev, MT_WF_RMAC_MIB_TIME0(band), MT_WF_RMAC_MIB_RXTIME_EN);
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0(band), MT_WF_RMAC_MIB_RXTIME_EN);

	/* enable MIB tx-rx time reporting */
	mt76_set(dev, MT_MIB_SCR1(band), MT_MIB_TXDUR_EN);
	mt76_set(dev, MT_MIB_SCR1(band), MT_MIB_RXDUR_EN);

	mt76_rmw_field(dev, MT_DMA_DCR0(band), MT_DMA_DCR0_MAX_RX_LEN, 1536);
	/* disable rx rate report by default due to hw issues */
	mt76_clear(dev, MT_DMA_DCR0(band), MT_DMA_DCR0_RXD_G5_EN);
}

int mt7921_mac_init(struct mt7921_dev *dev)
{
	int i;

	mt76_rmw_field(dev, MT_MDP_DCR1, MT_MDP_DCR1_MAX_RX_LEN, 1536);
	/* enable hardware de-agg */
	mt76_set(dev, MT_MDP_DCR0, MT_MDP_DCR0_DAMSDU_EN);
	/* enable hardware rx header translation */
	mt76_set(dev, MT_MDP_DCR0, MT_MDP_DCR0_RX_HDR_TRANS_EN);

	for (i = 0; i < MT7921_WTBL_SIZE; i++)
		mt7921_mac_wtbl_update(dev, i,
				       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
	for (i = 0; i < 2; i++)
		mt7921_mac_init_band(dev, i);

	dev->mt76.rxfilter = mt76_rr(dev, MT_WF_RFCR(0));

	return mt76_connac_mcu_set_rts_thresh(&dev->mt76, 0x92b, 0);
}
EXPORT_SYMBOL_GPL(mt7921_mac_init);

static int __mt7921_init_hardware(struct mt7921_dev *dev)
{
	int ret;

	/* force firmware operation mode into normal state,
	 * which should be set before firmware download stage.
	 */
	mt76_wr(dev, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);
	ret = mt7921_mcu_init(dev);
	if (ret)
		goto out;

	mt76_eeprom_override(&dev->mphy);

	ret = mt7921_mcu_set_eeprom(dev);
	if (ret)
		goto out;

	ret = mt7921_mac_init(dev);
out:
	return ret;
}

static int mt7921_init_hardware(struct mt7921_dev *dev)
{
	int ret, i;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	for (i = 0; i < MT7921_MCU_INIT_RETRY_COUNT; i++) {
		ret = __mt7921_init_hardware(dev);
		if (!ret)
			break;

		mt7921_init_reset(dev);
	}

	if (i == MT7921_MCU_INIT_RETRY_COUNT) {
		dev_err(dev->mt76.dev, "hardware init failed\n");
		return ret;
	}

	return 0;
}

static int mt7921_init_wcid(struct mt7921_dev *dev)
{
	int idx;

	/* Beacon and mgmt frames should occupy wcid 0 */
	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7921_WTBL_STA - 1);
	if (idx)
		return -ENOSPC;

	dev->mt76.global_wcid.idx = idx;
	dev->mt76.global_wcid.hw_key_idx = -1;
	dev->mt76.global_wcid.tx_info |= MT_WCID_TX_INFO_SET;
	rcu_assign_pointer(dev->mt76.wcid[idx], &dev->mt76.global_wcid);

	return 0;
}

static void mt7921_init_work(struct work_struct *work)
{
	struct mt7921_dev *dev = container_of(work, struct mt7921_dev,
					      init_work);
	int ret;

	ret = mt7921_init_hardware(dev);
	if (ret)
		return;

	mt76_set_stream_caps(&dev->mphy, true);
	mt7921_set_stream_he_caps(&dev->phy);

	ret = mt76_register_device(&dev->mt76, true, mt76_rates,
				   ARRAY_SIZE(mt76_rates));
	if (ret) {
		dev_err(dev->mt76.dev, "register device failed\n");
		return;
	}

	ret = mt7921_init_debugfs(dev);
	if (ret) {
		dev_err(dev->mt76.dev, "register debugfs failed\n");
		return;
	}

	/* we support chip reset now */
	dev->hw_init_done = true;

	mt76_connac_mcu_set_deep_sleep(&dev->mt76, dev->pm.ds_enable);
}

int mt7921_register_device(struct mt7921_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	int ret;

	dev->phy.dev = dev;
	dev->phy.mt76 = &dev->mt76.phy;
	dev->mt76.phy.priv = &dev->phy;
	dev->mt76.tx_worker.fn = mt7921_tx_worker;

	INIT_DELAYED_WORK(&dev->pm.ps_work, mt7921_pm_power_save_work);
	INIT_WORK(&dev->pm.wake_work, mt7921_pm_wake_work);
	spin_lock_init(&dev->pm.wake.lock);
	mutex_init(&dev->pm.mutex);
	init_waitqueue_head(&dev->pm.wait);
	if (mt76_is_sdio(&dev->mt76))
		init_waitqueue_head(&dev->mt76.sdio.wait);
	spin_lock_init(&dev->pm.txq_lock);
	INIT_DELAYED_WORK(&dev->mphy.mac_work, mt7921_mac_work);
	INIT_DELAYED_WORK(&dev->phy.scan_work, mt7921_scan_work);
	INIT_DELAYED_WORK(&dev->coredump.work, mt7921_coredump_work);
#if IS_ENABLED(CONFIG_IPV6)
	INIT_WORK(&dev->ipv6_ns_work, mt7921_set_ipv6_ns_work);
	skb_queue_head_init(&dev->ipv6_ns_list);
#endif
	skb_queue_head_init(&dev->phy.scan_event_list);
	skb_queue_head_init(&dev->coredump.msg_list);
	INIT_LIST_HEAD(&dev->sta_poll_list);
	spin_lock_init(&dev->sta_poll_lock);

	INIT_WORK(&dev->reset_work, mt7921_mac_reset_work);
	INIT_WORK(&dev->init_work, mt7921_init_work);

	dev->pm.idle_timeout = MT7921_PM_TIMEOUT;
	dev->pm.stats.last_wake_event = jiffies;
	dev->pm.stats.last_doze_event = jiffies;
	if (!mt76_is_usb(&dev->mt76)) {
		dev->pm.enable_user = true;
		dev->pm.enable = true;
		dev->pm.ds_enable_user = true;
		dev->pm.ds_enable = true;
	}

	if (!mt76_is_mmio(&dev->mt76))
		hw->extra_tx_headroom += MT_SDIO_TXD_SIZE + MT_SDIO_HDR_SIZE;

	ret = mt7921_init_wcid(dev);
	if (ret)
		return ret;

	ret = mt7921_init_wiphy(hw);
	if (ret)
		return ret;

	dev->mphy.sband_2g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;
	dev->mphy.sband_5g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;
	dev->mphy.sband_5g.sband.vht_cap.cap |=
			IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991 |
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK |
			IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
			IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE |
			(3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT);
	if (is_mt7922(&dev->mt76))
		dev->mphy.sband_5g.sband.vht_cap.cap |=
			IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ |
			IEEE80211_VHT_CAP_SHORT_GI_160;

	dev->mphy.hw->wiphy->available_antennas_rx = dev->mphy.chainmask;
	dev->mphy.hw->wiphy->available_antennas_tx = dev->mphy.chainmask;

	queue_work(system_wq, &dev->init_work);

	return 0;
}
EXPORT_SYMBOL_GPL(mt7921_register_device);
