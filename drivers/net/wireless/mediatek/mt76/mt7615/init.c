// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Roy Luo <royluo@google.com>
 *         Ryder Lee <ryder.lee@mediatek.com>
 *         Felix Fietkau <nbd@nbd.name>
 *         Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/etherdevice.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include "mt7615.h"
#include "mac.h"
#include "mcu.h"
#include "eeprom.h"

static ssize_t mt7615_thermal_show_temp(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct mt7615_dev *mdev = dev_get_drvdata(dev);
	int temperature;

	if (!mt7615_wait_for_mcu_init(mdev))
		return 0;

	mt7615_mutex_acquire(mdev);
	temperature = mt7615_mcu_get_temperature(mdev);
	mt7615_mutex_release(mdev);

	if (temperature < 0)
		return temperature;

	/* display in millidegree celcius */
	return sprintf(buf, "%u\n", temperature * 1000);
}

static SENSOR_DEVICE_ATTR(temp1_input, 0444, mt7615_thermal_show_temp,
			  NULL, 0);

static struct attribute *mt7615_hwmon_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mt7615_hwmon);

int mt7615_thermal_init(struct mt7615_dev *dev)
{
	struct wiphy *wiphy = mt76_hw(dev)->wiphy;
	struct device *hwmon;
	const char *name;

	if (!IS_REACHABLE(CONFIG_HWMON))
		return 0;

	name = devm_kasprintf(&wiphy->dev, GFP_KERNEL, "mt7615_%s",
			      wiphy_name(wiphy));
	hwmon = devm_hwmon_device_register_with_groups(&wiphy->dev, name, dev,
						       mt7615_hwmon_groups);
	if (IS_ERR(hwmon))
		return PTR_ERR(hwmon);

	return 0;
}
EXPORT_SYMBOL_GPL(mt7615_thermal_init);

static void
mt7615_phy_init(struct mt7615_dev *dev)
{
	/* disable rf low power beacon mode */
	mt76_set(dev, MT_WF_PHY_WF2_RFCTRL0(0), MT_WF_PHY_WF2_RFCTRL0_LPBCN_EN);
	mt76_set(dev, MT_WF_PHY_WF2_RFCTRL0(1), MT_WF_PHY_WF2_RFCTRL0_LPBCN_EN);
}

static void
mt7615_init_mac_chain(struct mt7615_dev *dev, int chain)
{
	u32 val;

	if (!chain)
		val = MT_CFG_CCR_MAC_D0_1X_GC_EN | MT_CFG_CCR_MAC_D0_2X_GC_EN;
	else
		val = MT_CFG_CCR_MAC_D1_1X_GC_EN | MT_CFG_CCR_MAC_D1_2X_GC_EN;

	/* enable band 0/1 clk */
	mt76_set(dev, MT_CFG_CCR, val);

	mt76_rmw(dev, MT_TMAC_TRCR(chain),
		 MT_TMAC_TRCR_CCA_SEL | MT_TMAC_TRCR_SEC_CCA_SEL,
		 FIELD_PREP(MT_TMAC_TRCR_CCA_SEL, 2) |
		 FIELD_PREP(MT_TMAC_TRCR_SEC_CCA_SEL, 0));

	mt76_wr(dev, MT_AGG_ACR(chain),
		MT_AGG_ACR_PKT_TIME_EN | MT_AGG_ACR_NO_BA_AR_RULE |
		FIELD_PREP(MT_AGG_ACR_CFEND_RATE, MT7615_CFEND_RATE_DEFAULT) |
		FIELD_PREP(MT_AGG_ACR_BAR_RATE, MT7615_BAR_RATE_DEFAULT));

	mt76_wr(dev, MT_AGG_ARUCR(chain),
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(0), 7) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(1), 2) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(2), 2) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(3), 2) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(4), 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(5), 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(6), 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(7), 1));

	mt76_wr(dev, MT_AGG_ARDCR(chain),
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(0), MT7615_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(1), MT7615_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(2), MT7615_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(3), MT7615_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(4), MT7615_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(5), MT7615_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(6), MT7615_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(7), MT7615_RATE_RETRY - 1));

	mt76_clear(dev, MT_DMA_RCFR0(chain), MT_DMA_RCFR0_MCU_RX_TDLS);
	if (!mt7615_firmware_offload(dev)) {
		u32 mask, set;

		mask = MT_DMA_RCFR0_MCU_RX_MGMT |
		       MT_DMA_RCFR0_MCU_RX_CTL_NON_BAR |
		       MT_DMA_RCFR0_MCU_RX_CTL_BAR |
		       MT_DMA_RCFR0_MCU_RX_BYPASS |
		       MT_DMA_RCFR0_RX_DROPPED_UCAST |
		       MT_DMA_RCFR0_RX_DROPPED_MCAST;
		set = FIELD_PREP(MT_DMA_RCFR0_RX_DROPPED_UCAST, 2) |
		      FIELD_PREP(MT_DMA_RCFR0_RX_DROPPED_MCAST, 2);
		mt76_rmw(dev, MT_DMA_RCFR0(chain), mask, set);
	}
}

static void
mt7615_mac_init(struct mt7615_dev *dev)
{
	int i;

	mt7615_init_mac_chain(dev, 0);

	mt76_rmw_field(dev, MT_TMAC_CTCR0,
		       MT_TMAC_CTCR0_INS_DDLMT_REFTIME, 0x3f);
	mt76_rmw_field(dev, MT_TMAC_CTCR0,
		       MT_TMAC_CTCR0_INS_DDLMT_DENSITY, 0x3);
	mt76_rmw(dev, MT_TMAC_CTCR0,
		 MT_TMAC_CTCR0_INS_DDLMT_VHT_SMPDU_EN |
		 MT_TMAC_CTCR0_INS_DDLMT_EN,
		 MT_TMAC_CTCR0_INS_DDLMT_VHT_SMPDU_EN |
		 MT_TMAC_CTCR0_INS_DDLMT_EN);

	mt76_connac_mcu_set_rts_thresh(&dev->mt76, 0x92b, 0);
	mt7615_mac_set_scs(&dev->phy, true);

	mt76_rmw(dev, MT_AGG_SCR, MT_AGG_SCR_NLNAV_MID_PTEC_DIS,
		 MT_AGG_SCR_NLNAV_MID_PTEC_DIS);

	mt76_wr(dev, MT_AGG_ARCR,
		FIELD_PREP(MT_AGG_ARCR_RTS_RATE_THR, 2) |
		MT_AGG_ARCR_RATE_DOWN_RATIO_EN |
		FIELD_PREP(MT_AGG_ARCR_RATE_DOWN_RATIO, 1) |
		FIELD_PREP(MT_AGG_ARCR_RATE_UP_EXTRA_TH, 4));

	for (i = 0; i < MT7615_WTBL_SIZE; i++)
		mt7615_mac_wtbl_update(dev, i,
				       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	mt76_set(dev, MT_WF_RMAC_MIB_TIME0, MT_WF_RMAC_MIB_RXTIME_EN);
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0, MT_WF_RMAC_MIB_RXTIME_EN);

	mt76_wr(dev, MT_DMA_DCR0,
		FIELD_PREP(MT_DMA_DCR0_MAX_RX_LEN, 3072) |
		MT_DMA_DCR0_RX_VEC_DROP | MT_DMA_DCR0_DAMSDU_EN |
		MT_DMA_DCR0_RX_HDR_TRANS_EN);
	/* disable TDLS filtering */
	mt76_clear(dev, MT_WF_PFCR, MT_WF_PFCR_TDLS_EN);
	mt76_set(dev, MT_WF_MIB_SCR0, MT_MIB_SCR0_AGG_CNT_RANGE_EN);
	if (is_mt7663(&dev->mt76)) {
		mt76_wr(dev, MT_WF_AGG(0x160), 0x5c341c02);
		mt76_wr(dev, MT_WF_AGG(0x164), 0x70708040);
	} else {
		mt7615_init_mac_chain(dev, 1);
	}
	mt7615_mcu_set_rx_hdr_trans_blacklist(dev);
}

static void
mt7615_check_offload_capability(struct mt7615_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct wiphy *wiphy = hw->wiphy;

	if (mt7615_firmware_offload(dev)) {
		ieee80211_hw_set(hw, SUPPORTS_PS);
		ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);

		wiphy->flags &= ~WIPHY_FLAG_4ADDR_STATION;
		wiphy->max_remain_on_channel_duration = 5000;
		wiphy->features |= NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR |
				   NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR |
				   WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
				   NL80211_FEATURE_P2P_GO_CTWIN |
				   NL80211_FEATURE_P2P_GO_OPPPS;
	} else {
		dev->ops->hw_scan = NULL;
		dev->ops->cancel_hw_scan = NULL;
		dev->ops->sched_scan_start = NULL;
		dev->ops->sched_scan_stop = NULL;
		dev->ops->set_rekey_data = NULL;
		dev->ops->remain_on_channel = NULL;
		dev->ops->cancel_remain_on_channel = NULL;

		wiphy->max_sched_scan_plan_interval = 0;
		wiphy->max_sched_scan_ie_len = 0;
		wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;
		wiphy->max_sched_scan_ssids = 0;
		wiphy->max_match_sets = 0;
		wiphy->max_sched_scan_reqs = 0;
	}
}

bool mt7615_wait_for_mcu_init(struct mt7615_dev *dev)
{
	flush_work(&dev->mcu_work);

	return test_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
}
EXPORT_SYMBOL_GPL(mt7615_wait_for_mcu_init);

static const struct ieee80211_iface_limit if_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_ADHOC)
	}, {
		.max = MT7615_MAX_INTERFACES,
		.types = BIT(NL80211_IFTYPE_AP) |
#ifdef CONFIG_MAC80211_MESH
			 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
			 BIT(NL80211_IFTYPE_P2P_CLIENT) |
			 BIT(NL80211_IFTYPE_P2P_GO) |
			 BIT(NL80211_IFTYPE_STATION)
	}
};

static const struct ieee80211_iface_combination if_comb_radar[] = {
	{
		.limits = if_limits,
		.n_limits = ARRAY_SIZE(if_limits),
		.max_interfaces = MT7615_MAX_INTERFACES,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
		.radar_detect_widths = BIT(NL80211_CHAN_WIDTH_20_NOHT) |
				       BIT(NL80211_CHAN_WIDTH_20) |
				       BIT(NL80211_CHAN_WIDTH_40) |
				       BIT(NL80211_CHAN_WIDTH_80) |
				       BIT(NL80211_CHAN_WIDTH_160) |
				       BIT(NL80211_CHAN_WIDTH_80P80),
	}
};

static const struct ieee80211_iface_combination if_comb[] = {
	{
		.limits = if_limits,
		.n_limits = ARRAY_SIZE(if_limits),
		.max_interfaces = MT7615_MAX_INTERFACES,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
	}
};

void mt7615_init_txpower(struct mt7615_dev *dev,
			 struct ieee80211_supported_band *sband)
{
	int i, n_chains = hweight8(dev->mphy.antenna_mask), target_chains;
	int delta_idx, delta = mt76_tx_power_nss_delta(n_chains);
	u8 *eep = (u8 *)dev->mt76.eeprom.data;
	enum nl80211_band band = sband->band;
	struct mt76_power_limits limits;
	u8 rate_val;

	delta_idx = mt7615_eeprom_get_power_delta_index(dev, band);
	rate_val = eep[delta_idx];
	if ((rate_val & ~MT_EE_RATE_POWER_MASK) ==
	    (MT_EE_RATE_POWER_EN | MT_EE_RATE_POWER_SIGN))
		delta += rate_val & MT_EE_RATE_POWER_MASK;

	if (!is_mt7663(&dev->mt76) && mt7615_ext_pa_enabled(dev, band))
		target_chains = 1;
	else
		target_chains = n_chains;

	for (i = 0; i < sband->n_channels; i++) {
		struct ieee80211_channel *chan = &sband->channels[i];
		u8 target_power = 0;
		int j;

		for (j = 0; j < target_chains; j++) {
			int index;

			index = mt7615_eeprom_get_target_power_index(dev, chan, j);
			if (index < 0)
				continue;

			target_power = max(target_power, eep[index]);
		}

		target_power = mt76_get_rate_power_limits(&dev->mphy, chan,
							  &limits,
							  target_power);
		target_power += delta;
		target_power = DIV_ROUND_UP(target_power, 2);
		chan->max_power = min_t(int, chan->max_reg_power,
					target_power);
		chan->orig_mpwr = target_power;
	}
}
EXPORT_SYMBOL_GPL(mt7615_init_txpower);

void mt7615_init_work(struct mt7615_dev *dev)
{
	mt7615_mcu_set_eeprom(dev);
	mt7615_mac_init(dev);
	mt7615_phy_init(dev);
	mt7615_mcu_del_wtbl_all(dev);
	mt7615_check_offload_capability(dev);
}
EXPORT_SYMBOL_GPL(mt7615_init_work);

static void
mt7615_regd_notifier(struct wiphy *wiphy,
		     struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	struct mt7615_phy *phy = mphy->priv;
	struct cfg80211_chan_def *chandef = &mphy->chandef;

	memcpy(dev->mt76.alpha2, request->alpha2, sizeof(dev->mt76.alpha2));
	dev->mt76.region = request->dfs_region;

	mt7615_init_txpower(dev, &mphy->sband_2g.sband);
	mt7615_init_txpower(dev, &mphy->sband_5g.sband);

	mt7615_mutex_acquire(dev);

	if (chandef->chan->flags & IEEE80211_CHAN_RADAR)
		mt7615_dfs_init_radar_detector(phy);

	if (mt7615_firmware_offload(phy->dev)) {
		mt76_connac_mcu_set_channel_domain(mphy);
		mt76_connac_mcu_set_rate_txpower(mphy);
	}

	mt7615_mutex_release(dev);
}

static void
mt7615_init_wiphy(struct ieee80211_hw *hw)
{
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	struct wiphy *wiphy = hw->wiphy;

	hw->queues = 4;
	hw->max_rates = 3;
	hw->max_report_rates = 7;
	hw->max_rate_tries = 11;
	hw->netdev_features = NETIF_F_RXCSUM;

	hw->radiotap_timestamp.units_pos =
		IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US;

	phy->slottime = 9;

	hw->sta_data_size = sizeof(struct mt7615_sta);
	hw->vif_data_size = sizeof(struct mt7615_vif);

	if (is_mt7663(&phy->dev->mt76)) {
		wiphy->iface_combinations = if_comb;
		wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);
	} else {
		wiphy->iface_combinations = if_comb_radar;
		wiphy->n_iface_combinations = ARRAY_SIZE(if_comb_radar);
	}
	wiphy->reg_notifier = mt7615_regd_notifier;

	wiphy->max_sched_scan_plan_interval =
		MT76_CONNAC_MAX_TIME_SCHED_SCAN_INTERVAL;
	wiphy->max_sched_scan_ie_len = IEEE80211_MAX_DATA_LEN;
	wiphy->max_scan_ie_len = MT76_CONNAC_SCAN_IE_LEN;
	wiphy->max_sched_scan_ssids = MT76_CONNAC_MAX_SCHED_SCAN_SSID;
	wiphy->max_match_sets = MT76_CONNAC_MAX_SCAN_MATCH;
	wiphy->max_sched_scan_reqs = 1;
	wiphy->max_scan_ssids = 4;

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SET_SCAN_DWELL);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_VHT_IBSS);

	ieee80211_hw_set(hw, SINGLE_SCAN_ON_ALL_BANDS);
	ieee80211_hw_set(hw, TX_STATUS_NO_AMPDU_LEN);
	ieee80211_hw_set(hw, WANT_MONITOR_VIF);
	ieee80211_hw_set(hw, SUPPORTS_RX_DECAP_OFFLOAD);
	ieee80211_hw_set(hw, SUPPORTS_VHT_EXT_NSS_BW);

	if (is_mt7615(&phy->dev->mt76))
		hw->max_tx_fragments = MT_TXP_MAX_BUF_NUM;
	else
		hw->max_tx_fragments = MT_HW_TXP_MAX_BUF_NUM;

	phy->mt76->sband_2g.sband.ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;
	phy->mt76->sband_5g.sband.ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;
	phy->mt76->sband_5g.sband.vht_cap.cap |=
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;
}

static void
mt7615_cap_dbdc_enable(struct mt7615_dev *dev)
{
	dev->mphy.sband_5g.sband.vht_cap.cap &=
			~(IEEE80211_VHT_CAP_SHORT_GI_160 |
			  IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ);
	if (dev->chainmask == 0xf)
		dev->mphy.antenna_mask = dev->chainmask >> 2;
	else
		dev->mphy.antenna_mask = dev->chainmask >> 1;
	dev->mphy.chainmask = dev->mphy.antenna_mask;
	dev->mphy.hw->wiphy->available_antennas_rx = dev->mphy.chainmask;
	dev->mphy.hw->wiphy->available_antennas_tx = dev->mphy.chainmask;
	mt76_set_stream_caps(&dev->mphy, true);
}

static void
mt7615_cap_dbdc_disable(struct mt7615_dev *dev)
{
	dev->mphy.sband_5g.sband.vht_cap.cap |=
			IEEE80211_VHT_CAP_SHORT_GI_160 |
			IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ;
	dev->mphy.antenna_mask = dev->chainmask;
	dev->mphy.chainmask = dev->chainmask;
	dev->mphy.hw->wiphy->available_antennas_rx = dev->chainmask;
	dev->mphy.hw->wiphy->available_antennas_tx = dev->chainmask;
	mt76_set_stream_caps(&dev->mphy, true);
}

int mt7615_register_ext_phy(struct mt7615_dev *dev)
{
	struct mt7615_phy *phy = mt7615_ext_phy(dev);
	struct mt76_phy *mphy;
	int i, ret;

	if (!is_mt7615(&dev->mt76))
		return -EOPNOTSUPP;

	if (test_bit(MT76_STATE_RUNNING, &dev->mphy.state))
		return -EINVAL;

	if (phy)
		return 0;

	mt7615_cap_dbdc_enable(dev);
	mphy = mt76_alloc_phy(&dev->mt76, sizeof(*phy), &mt7615_ops, MT_BAND1);
	if (!mphy)
		return -ENOMEM;

	phy = mphy->priv;
	phy->dev = dev;
	phy->mt76 = mphy;
	mphy->chainmask = dev->chainmask & ~dev->mphy.chainmask;
	mphy->antenna_mask = BIT(hweight8(mphy->chainmask)) - 1;
	mt7615_init_wiphy(mphy->hw);

	INIT_DELAYED_WORK(&mphy->mac_work, mt7615_mac_work);
	INIT_DELAYED_WORK(&phy->scan_work, mt7615_scan_work);
	skb_queue_head_init(&phy->scan_event_list);

	INIT_WORK(&phy->roc_work, mt7615_roc_work);
	timer_setup(&phy->roc_timer, mt7615_roc_timer, 0);
	init_waitqueue_head(&phy->roc_wait);

	mt7615_mac_set_scs(phy, true);

	/*
	 * Make the secondary PHY MAC address local without overlapping with
	 * the usual MAC address allocation scheme on multiple virtual interfaces
	 */
	memcpy(mphy->macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR,
	       ETH_ALEN);
	mphy->macaddr[0] |= 2;
	mphy->macaddr[0] ^= BIT(7);
	mt76_eeprom_override(mphy);

	/* second phy can only handle 5 GHz */
	mphy->cap.has_5ghz = true;

	/* mt7615 second phy shares the same hw queues with the primary one */
	for (i = 0; i <= MT_TXQ_PSD ; i++)
		mphy->q_tx[i] = dev->mphy.q_tx[i];

	ret = mt76_register_phy(mphy, true, mt76_rates,
				ARRAY_SIZE(mt76_rates));
	if (ret)
		ieee80211_free_hw(mphy->hw);

	return ret;
}
EXPORT_SYMBOL_GPL(mt7615_register_ext_phy);

void mt7615_unregister_ext_phy(struct mt7615_dev *dev)
{
	struct mt7615_phy *phy = mt7615_ext_phy(dev);
	struct mt76_phy *mphy = dev->mt76.phys[MT_BAND1];

	if (!phy)
		return;

	mt7615_cap_dbdc_disable(dev);
	mt76_unregister_phy(mphy);
	ieee80211_free_hw(mphy->hw);
}
EXPORT_SYMBOL_GPL(mt7615_unregister_ext_phy);

void mt7615_init_device(struct mt7615_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);

	dev->phy.dev = dev;
	dev->phy.mt76 = &dev->mt76.phy;
	dev->mt76.phy.priv = &dev->phy;
	dev->mt76.tx_worker.fn = mt7615_tx_worker;

	INIT_DELAYED_WORK(&dev->pm.ps_work, mt7615_pm_power_save_work);
	INIT_WORK(&dev->pm.wake_work, mt7615_pm_wake_work);
	spin_lock_init(&dev->pm.wake.lock);
	mutex_init(&dev->pm.mutex);
	init_waitqueue_head(&dev->pm.wait);
	spin_lock_init(&dev->pm.txq_lock);
	INIT_DELAYED_WORK(&dev->mphy.mac_work, mt7615_mac_work);
	INIT_DELAYED_WORK(&dev->phy.scan_work, mt7615_scan_work);
	INIT_DELAYED_WORK(&dev->coredump.work, mt7615_coredump_work);
	skb_queue_head_init(&dev->phy.scan_event_list);
	skb_queue_head_init(&dev->coredump.msg_list);
	INIT_LIST_HEAD(&dev->sta_poll_list);
	spin_lock_init(&dev->sta_poll_lock);
	init_waitqueue_head(&dev->reset_wait);
	init_waitqueue_head(&dev->phy.roc_wait);

	INIT_WORK(&dev->phy.roc_work, mt7615_roc_work);
	timer_setup(&dev->phy.roc_timer, mt7615_roc_timer, 0);

	mt7615_init_wiphy(hw);
	dev->pm.idle_timeout = MT7615_PM_TIMEOUT;
	dev->pm.stats.last_wake_event = jiffies;
	dev->pm.stats.last_doze_event = jiffies;
	mt7615_cap_dbdc_disable(dev);

#ifdef CONFIG_NL80211_TESTMODE
	dev->mt76.test_ops = &mt7615_testmode_ops;
#endif
}
EXPORT_SYMBOL_GPL(mt7615_init_device);
