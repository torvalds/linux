// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/etherdevice.h>
#include "mt7915.h"
#include "mac.h"
#include "eeprom.h"

static void
mt7915_mac_init_band(struct mt7915_dev *dev, u8 band)
{
	u32 mask, set;

	mt76_rmw_field(dev, MT_TMAC_CTCR0(band),
		       MT_TMAC_CTCR0_INS_DDLMT_REFTIME, 0x3f);
	mt76_set(dev, MT_TMAC_CTCR0(band),
		 MT_TMAC_CTCR0_INS_DDLMT_VHT_SMPDU_EN |
		 MT_TMAC_CTCR0_INS_DDLMT_EN);

	mask = MT_MDP_RCFR0_MCU_RX_MGMT |
	       MT_MDP_RCFR0_MCU_RX_CTL_NON_BAR |
	       MT_MDP_RCFR0_MCU_RX_CTL_BAR;
	set = FIELD_PREP(MT_MDP_RCFR0_MCU_RX_MGMT, MT_MDP_TO_HIF) |
	      FIELD_PREP(MT_MDP_RCFR0_MCU_RX_CTL_NON_BAR, MT_MDP_TO_HIF) |
	      FIELD_PREP(MT_MDP_RCFR0_MCU_RX_CTL_BAR, MT_MDP_TO_HIF);
	mt76_rmw(dev, MT_MDP_BNRCFR0(band), mask, set);

	mask = MT_MDP_RCFR1_MCU_RX_BYPASS |
	       MT_MDP_RCFR1_RX_DROPPED_UCAST |
	       MT_MDP_RCFR1_RX_DROPPED_MCAST;
	set = FIELD_PREP(MT_MDP_RCFR1_MCU_RX_BYPASS, MT_MDP_TO_HIF) |
	      FIELD_PREP(MT_MDP_RCFR1_RX_DROPPED_UCAST, MT_MDP_TO_HIF) |
	      FIELD_PREP(MT_MDP_RCFR1_RX_DROPPED_MCAST, MT_MDP_TO_HIF);
	mt76_rmw(dev, MT_MDP_BNRCFR1(band), mask, set);

	mt76_set(dev, MT_WF_RMAC_MIB_TIME0(band), MT_WF_RMAC_MIB_RXTIME_EN);
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0(band), MT_WF_RMAC_MIB_RXTIME_EN);
}

static void mt7915_mac_init(struct mt7915_dev *dev)
{
	int i;

	mt76_rmw_field(dev, MT_DMA_DCR0, MT_DMA_DCR0_MAX_RX_LEN, 1536);
	mt76_rmw_field(dev, MT_MDP_DCR1, MT_MDP_DCR1_MAX_RX_LEN, 1536);
	/* disable hardware de-agg */
	mt76_clear(dev, MT_MDP_DCR0, MT_MDP_DCR0_DAMSDU_EN);

	for (i = 0; i < MT7915_WTBL_SIZE; i++)
		mt7915_mac_wtbl_update(dev, i,
				       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	mt7915_mac_init_band(dev, 0);
	mt7915_mac_init_band(dev, 1);
	mt7915_mcu_set_rts_thresh(&dev->phy, 0x92b);
}

static void
mt7915_init_txpower_band(struct mt7915_dev *dev,
			 struct ieee80211_supported_band *sband)
{
	int i, n_chains = hweight8(dev->mphy.antenna_mask);

	for (i = 0; i < sband->n_channels; i++) {
		struct ieee80211_channel *chan = &sband->channels[i];
		u32 target_power = 0;
		int j;

		for (j = 0; j < n_chains; j++) {
			u32 val;

			val = mt7915_eeprom_get_target_power(dev, chan, j);
			target_power = max(target_power, val);
		}

		chan->max_power = min_t(int, chan->max_reg_power,
					target_power / 2);
		chan->orig_mpwr = target_power / 2;
	}
}

static void mt7915_init_txpower(struct mt7915_dev *dev)
{
	mt7915_init_txpower_band(dev, &dev->mphy.sband_2g.sband);
	mt7915_init_txpower_band(dev, &dev->mphy.sband_5g.sband);
}

static void mt7915_init_work(struct work_struct *work)
{
	struct mt7915_dev *dev = container_of(work, struct mt7915_dev,
				 init_work);

	mt7915_mcu_set_eeprom(dev);
	mt7915_mac_init(dev);
	mt7915_init_txpower(dev);
}

static int mt7915_init_hardware(struct mt7915_dev *dev)
{
	int ret, idx;

	mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

	INIT_WORK(&dev->init_work, mt7915_init_work);
	spin_lock_init(&dev->token_lock);
	idr_init(&dev->token);

	ret = mt7915_dma_init(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	ret = mt7915_mcu_init(dev);
	if (ret)
		return ret;

	ret = mt7915_eeprom_init(dev);
	if (ret < 0)
		return ret;

	/* Beacon and mgmt frames should occupy wcid 0 */
	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7915_WTBL_STA - 1);
	if (idx)
		return -ENOSPC;

	dev->mt76.global_wcid.idx = idx;
	dev->mt76.global_wcid.hw_key_idx = -1;
	dev->mt76.global_wcid.tx_info |= MT_WCID_TX_INFO_SET;
	rcu_assign_pointer(dev->mt76.wcid[idx], &dev->mt76.global_wcid);

	return 0;
}

#define CCK_RATE(_idx, _rate) {						\
	.bitrate = _rate,						\
	.flags = IEEE80211_RATE_SHORT_PREAMBLE,				\
	.hw_value = (MT_PHY_TYPE_CCK << 8) | (_idx),			\
	.hw_value_short = (MT_PHY_TYPE_CCK << 8) | (4 + (_idx)),	\
}

#define OFDM_RATE(_idx, _rate) {					\
	.bitrate = _rate,						\
	.hw_value = (MT_PHY_TYPE_OFDM << 8) | (_idx),			\
	.hw_value_short = (MT_PHY_TYPE_OFDM << 8) | (_idx),		\
}

static struct ieee80211_rate mt7915_rates[] = {
	CCK_RATE(0, 10),
	CCK_RATE(1, 20),
	CCK_RATE(2, 55),
	CCK_RATE(3, 110),
	OFDM_RATE(11, 60),
	OFDM_RATE(15, 90),
	OFDM_RATE(10, 120),
	OFDM_RATE(14, 180),
	OFDM_RATE(9,  240),
	OFDM_RATE(13, 360),
	OFDM_RATE(8,  480),
	OFDM_RATE(12, 540),
};

static const struct ieee80211_iface_limit if_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_ADHOC)
	}, {
		.max = MT7915_MAX_INTERFACES,
		.types = BIT(NL80211_IFTYPE_AP) |
#ifdef CONFIG_MAC80211_MESH
			 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
			 BIT(NL80211_IFTYPE_STATION)
	}
};

static const struct ieee80211_iface_combination if_comb[] = {
	{
		.limits = if_limits,
		.n_limits = ARRAY_SIZE(if_limits),
		.max_interfaces = 4,
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

static void
mt7915_regd_notifier(struct wiphy *wiphy,
		     struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	struct mt7915_phy *phy = mphy->priv;
	struct cfg80211_chan_def *chandef = &mphy->chandef;

	dev->mt76.region = request->dfs_region;

	if (!(chandef->chan->flags & IEEE80211_CHAN_RADAR))
		return;

	mt7915_dfs_init_radar_detector(phy);
}

static void
mt7915_init_wiphy(struct ieee80211_hw *hw)
{
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct wiphy *wiphy = hw->wiphy;

	hw->queues = 4;
	hw->max_rx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF;
	hw->max_tx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF;

	phy->slottime = 9;

	hw->sta_data_size = sizeof(struct mt7915_sta);
	hw->vif_data_size = sizeof(struct mt7915_vif);

	wiphy->iface_combinations = if_comb;
	wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);
	wiphy->reg_notifier = mt7915_regd_notifier;
	wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_VHT_IBSS);

	ieee80211_hw_set(hw, HAS_RATE_CONTROL);

	hw->max_tx_fragments = 4;
}

static void
mt7915_cap_dbdc_enable(struct mt7915_dev *dev)
{
	dev->mphy.sband_5g.sband.vht_cap.cap &=
			~(IEEE80211_VHT_CAP_SHORT_GI_160 |
			  IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ);

	if (dev->chainmask == 0xf)
		dev->mphy.antenna_mask = dev->chainmask >> 2;
	else
		dev->mphy.antenna_mask = dev->chainmask >> 1;

	dev->phy.chainmask = dev->mphy.antenna_mask;
	dev->mphy.hw->wiphy->available_antennas_rx = dev->phy.chainmask;
	dev->mphy.hw->wiphy->available_antennas_tx = dev->phy.chainmask;

	mt76_set_stream_caps(&dev->mt76, true);
}

static void
mt7915_cap_dbdc_disable(struct mt7915_dev *dev)
{
	dev->mphy.sband_5g.sband.vht_cap.cap |=
			IEEE80211_VHT_CAP_SHORT_GI_160 |
			IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ;

	dev->mphy.antenna_mask = dev->chainmask;
	dev->phy.chainmask = dev->chainmask;
	dev->mphy.hw->wiphy->available_antennas_rx = dev->chainmask;
	dev->mphy.hw->wiphy->available_antennas_tx = dev->chainmask;

	mt76_set_stream_caps(&dev->mt76, true);
}

int mt7915_register_ext_phy(struct mt7915_dev *dev)
{
	struct mt7915_phy *phy = mt7915_ext_phy(dev);
	struct mt76_phy *mphy;
	int ret;
	bool bound;

	/* TODO: enble DBDC */
	bound = mt7915_l1_rr(dev, MT_HW_BOUND) & BIT(5);
	if (!bound)
		return -EINVAL;

	if (test_bit(MT76_STATE_RUNNING, &dev->mphy.state))
		return -EINVAL;

	if (phy)
		return 0;

	mt7915_cap_dbdc_enable(dev);
	mphy = mt76_alloc_phy(&dev->mt76, sizeof(*phy), &mt7915_ops);
	if (!mphy)
		return -ENOMEM;

	phy = mphy->priv;
	phy->dev = dev;
	phy->mt76 = mphy;
	phy->chainmask = dev->chainmask & ~dev->phy.chainmask;
	mphy->antenna_mask = BIT(hweight8(phy->chainmask)) - 1;
	mt7915_init_wiphy(mphy->hw);

	/*
	 * Make the secondary PHY MAC address local without overlapping with
	 * the usual MAC address allocation scheme on multiple virtual interfaces
	 */
	mphy->hw->wiphy->perm_addr[0] |= 2;
	mphy->hw->wiphy->perm_addr[0] ^= BIT(7);

	/* The second interface does not get any packets unless it has a vif */
	ieee80211_hw_set(mphy->hw, WANT_MONITOR_VIF);

	ret = mt76_register_phy(mphy);
	if (ret)
		ieee80211_free_hw(mphy->hw);

	return ret;
}

void mt7915_unregister_ext_phy(struct mt7915_dev *dev)
{
	struct mt7915_phy *phy = mt7915_ext_phy(dev);
	struct mt76_phy *mphy = dev->mt76.phy2;

	if (!phy)
		return;

	mt7915_cap_dbdc_disable(dev);
	mt76_unregister_phy(mphy);
	ieee80211_free_hw(mphy->hw);
}

int mt7915_register_device(struct mt7915_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	int ret;

	dev->phy.dev = dev;
	dev->phy.mt76 = &dev->mt76.phy;
	dev->mt76.phy.priv = &dev->phy;
	INIT_DELAYED_WORK(&dev->mt76.mac_work, mt7915_mac_work);
	INIT_LIST_HEAD(&dev->sta_poll_list);
	spin_lock_init(&dev->sta_poll_lock);

	init_waitqueue_head(&dev->reset_wait);
	INIT_WORK(&dev->reset_work, mt7915_mac_reset_work);

	ret = mt7915_init_hardware(dev);
	if (ret)
		return ret;

	mt7915_init_wiphy(hw);
	dev->mphy.sband_2g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;
	dev->mphy.sband_5g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;
	dev->mphy.sband_5g.sband.vht_cap.cap |=
			IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991 |
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;
	mt7915_cap_dbdc_disable(dev);
	dev->phy.dfs_state = -1;

	ret = mt76_register_device(&dev->mt76, true, mt7915_rates,
				   ARRAY_SIZE(mt7915_rates));
	if (ret)
		return ret;

	ieee80211_queue_work(mt76_hw(dev), &dev->init_work);

	return mt7915_init_debugfs(dev);
}

void mt7915_unregister_device(struct mt7915_dev *dev)
{
	struct mt76_txwi_cache *txwi;
	int id;

	mt7915_unregister_ext_phy(dev);
	mt76_unregister_device(&dev->mt76);
	mt7915_mcu_exit(dev);
	mt7915_dma_cleanup(dev);

	spin_lock_bh(&dev->token_lock);
	idr_for_each_entry(&dev->token, txwi, id) {
		mt7915_txp_skb_unmap(&dev->mt76, txwi);
		if (txwi->skb)
			dev_kfree_skb_any(txwi->skb);
		mt76_put_txwi(&dev->mt76, txwi);
	}
	spin_unlock_bh(&dev->token_lock);
	idr_destroy(&dev->token);

	mt76_free_device(&dev->mt76);
}
