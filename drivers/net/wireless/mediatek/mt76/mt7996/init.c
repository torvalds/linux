// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/etherdevice.h>
#include <linux/of.h>
#include <linux/thermal.h>
#include "mt7996.h"
#include "mac.h"
#include "mcu.h"
#include "coredump.h"
#include "eeprom.h"

static const struct ieee80211_iface_limit if_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_ADHOC)
	}, {
		.max = 16,
		.types = BIT(NL80211_IFTYPE_AP)
#ifdef CONFIG_MAC80211_MESH
			 | BIT(NL80211_IFTYPE_MESH_POINT)
#endif
	}, {
		.max = MT7996_MAX_INTERFACES,
		.types = BIT(NL80211_IFTYPE_STATION)
	}
};

static const struct ieee80211_iface_combination if_comb[] = {
	{
		.limits = if_limits,
		.n_limits = ARRAY_SIZE(if_limits),
		.max_interfaces = MT7996_MAX_INTERFACES,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
		.radar_detect_widths = BIT(NL80211_CHAN_WIDTH_20_NOHT) |
				       BIT(NL80211_CHAN_WIDTH_20) |
				       BIT(NL80211_CHAN_WIDTH_40) |
				       BIT(NL80211_CHAN_WIDTH_80) |
				       BIT(NL80211_CHAN_WIDTH_160),
	}
};

static void mt7996_led_set_config(struct led_classdev *led_cdev,
				  u8 delay_on, u8 delay_off)
{
	struct mt7996_dev *dev;
	struct mt76_phy *mphy;
	u32 val;

	mphy = container_of(led_cdev, struct mt76_phy, leds.cdev);
	dev = container_of(mphy->dev, struct mt7996_dev, mt76);

	/* select TX blink mode, 2: only data frames */
	mt76_rmw_field(dev, MT_TMAC_TCR0(0), MT_TMAC_TCR0_TX_BLINK, 2);

	/* enable LED */
	mt76_wr(dev, MT_LED_EN(0), 1);

	/* set LED Tx blink on/off time */
	val = FIELD_PREP(MT_LED_TX_BLINK_ON_MASK, delay_on) |
	      FIELD_PREP(MT_LED_TX_BLINK_OFF_MASK, delay_off);
	mt76_wr(dev, MT_LED_TX_BLINK(0), val);

	/* control LED */
	val = MT_LED_CTRL_BLINK_MODE | MT_LED_CTRL_KICK;
	if (mphy->leds.al)
		val |= MT_LED_CTRL_POLARITY;

	mt76_wr(dev, MT_LED_CTRL(0), val);
	mt76_clear(dev, MT_LED_CTRL(0), MT_LED_CTRL_KICK);
}

static int mt7996_led_set_blink(struct led_classdev *led_cdev,
				unsigned long *delay_on,
				unsigned long *delay_off)
{
	u16 delta_on = 0, delta_off = 0;

#define HW_TICK		10
#define TO_HW_TICK(_t)	(((_t) > HW_TICK) ? ((_t) / HW_TICK) : HW_TICK)

	if (*delay_on)
		delta_on = TO_HW_TICK(*delay_on);
	if (*delay_off)
		delta_off = TO_HW_TICK(*delay_off);

	mt7996_led_set_config(led_cdev, delta_on, delta_off);

	return 0;
}

static void mt7996_led_set_brightness(struct led_classdev *led_cdev,
				      enum led_brightness brightness)
{
	if (!brightness)
		mt7996_led_set_config(led_cdev, 0, 0xff);
	else
		mt7996_led_set_config(led_cdev, 0xff, 0);
}

void mt7996_init_txpower(struct mt7996_dev *dev,
			 struct ieee80211_supported_band *sband)
{
	int i, nss = hweight8(dev->mphy.antenna_mask);
	int nss_delta = mt76_tx_power_nss_delta(nss);
	int pwr_delta = mt7996_eeprom_get_power_delta(dev, sband->band);
	struct mt76_power_limits limits;

	for (i = 0; i < sband->n_channels; i++) {
		struct ieee80211_channel *chan = &sband->channels[i];
		int target_power = mt7996_eeprom_get_target_power(dev, chan);

		target_power += pwr_delta;
		target_power = mt76_get_rate_power_limits(&dev->mphy, chan,
							  &limits,
							  target_power);
		target_power += nss_delta;
		target_power = DIV_ROUND_UP(target_power, 2);
		chan->max_power = min_t(int, chan->max_reg_power,
					target_power);
		chan->orig_mpwr = target_power;
	}
}

static void
mt7996_regd_notifier(struct wiphy *wiphy,
		     struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy = mt7996_hw_phy(hw);

	memcpy(dev->mt76.alpha2, request->alpha2, sizeof(dev->mt76.alpha2));
	dev->mt76.region = request->dfs_region;

	if (dev->mt76.region == NL80211_DFS_UNSET)
		mt7996_mcu_rdd_background_enable(phy, NULL);

	mt7996_init_txpower(dev, &phy->mt76->sband_2g.sband);
	mt7996_init_txpower(dev, &phy->mt76->sband_5g.sband);
	mt7996_init_txpower(dev, &phy->mt76->sband_6g.sband);

	phy->mt76->dfs_state = MT_DFS_STATE_UNKNOWN;
	mt7996_dfs_init_radar_detector(phy);
}

static void
mt7996_init_wiphy(struct ieee80211_hw *hw)
{
	struct mt7996_phy *phy = mt7996_hw_phy(hw);
	struct mt76_dev *mdev = &phy->dev->mt76;
	struct wiphy *wiphy = hw->wiphy;
	u16 max_subframes = phy->dev->has_eht ? IEEE80211_MAX_AMPDU_BUF_EHT :
						IEEE80211_MAX_AMPDU_BUF_HE;

	hw->queues = 4;
	hw->max_rx_aggregation_subframes = max_subframes;
	hw->max_tx_aggregation_subframes = max_subframes;
	hw->netdev_features = NETIF_F_RXCSUM;

	hw->radiotap_timestamp.units_pos =
		IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US;

	phy->slottime = 9;

	hw->sta_data_size = sizeof(struct mt7996_sta);
	hw->vif_data_size = sizeof(struct mt7996_vif);

	wiphy->iface_combinations = if_comb;
	wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);
	wiphy->reg_notifier = mt7996_regd_notifier;
	wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BSS_COLOR);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_VHT_IBSS);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_LEGACY);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_HT);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_VHT);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_HE);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_UNSOL_BCAST_PROBE_RESP);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_FILS_DISCOVERY);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_ACK_SIGNAL_SUPPORT);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_CAN_REPLACE_PTK0);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_MU_MIMO_AIR_SNIFFER);

	if (!mdev->dev->of_node ||
	    !of_property_read_bool(mdev->dev->of_node,
				   "mediatek,disable-radar-background"))
		wiphy_ext_feature_set(wiphy,
				      NL80211_EXT_FEATURE_RADAR_BACKGROUND);

	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, SUPPORTS_TX_ENCAP_OFFLOAD);
	ieee80211_hw_set(hw, SUPPORTS_RX_DECAP_OFFLOAD);
	ieee80211_hw_set(hw, WANT_MONITOR_VIF);

	hw->max_tx_fragments = 4;

	if (phy->mt76->cap.has_2ghz) {
		phy->mt76->sband_2g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;
		phy->mt76->sband_2g.sband.ht_cap.ampdu_density =
			IEEE80211_HT_MPDU_DENSITY_2;
	}

	if (phy->mt76->cap.has_5ghz) {
		phy->mt76->sband_5g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;

		phy->mt76->sband_5g.sband.vht_cap.cap |=
			IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK |
			IEEE80211_VHT_CAP_SHORT_GI_160 |
			IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
		phy->mt76->sband_5g.sband.ht_cap.ampdu_density =
			IEEE80211_HT_MPDU_DENSITY_1;

		ieee80211_hw_set(hw, SUPPORTS_VHT_EXT_NSS_BW);
	}

	mt76_set_stream_caps(phy->mt76, true);
	mt7996_set_stream_vht_txbf_caps(phy);
	mt7996_set_stream_he_eht_caps(phy);

	wiphy->available_antennas_rx = phy->mt76->antenna_mask;
	wiphy->available_antennas_tx = phy->mt76->antenna_mask;
}

static void
mt7996_mac_init_band(struct mt7996_dev *dev, u8 band)
{
	u32 mask, set;

	/* clear estimated value of EIFS for Rx duration & OBSS time */
	mt76_wr(dev, MT_WF_RMAC_RSVD0(band), MT_WF_RMAC_RSVD0_EIFS_CLR);

	/* clear backoff time for Rx duration  */
	mt76_clear(dev, MT_WF_RMAC_MIB_AIRTIME1(band),
		   MT_WF_RMAC_MIB_NONQOSD_BACKOFF);
	mt76_clear(dev, MT_WF_RMAC_MIB_AIRTIME3(band),
		   MT_WF_RMAC_MIB_QOS01_BACKOFF);
	mt76_clear(dev, MT_WF_RMAC_MIB_AIRTIME4(band),
		   MT_WF_RMAC_MIB_QOS23_BACKOFF);

	/* clear backoff time and set software compensation for OBSS time */
	mask = MT_WF_RMAC_MIB_OBSS_BACKOFF | MT_WF_RMAC_MIB_ED_OFFSET;
	set = FIELD_PREP(MT_WF_RMAC_MIB_OBSS_BACKOFF, 0) |
	      FIELD_PREP(MT_WF_RMAC_MIB_ED_OFFSET, 4);
	mt76_rmw(dev, MT_WF_RMAC_MIB_AIRTIME0(band), mask, set);

	/* filter out non-resp frames and get instanstaeous signal reporting */
	mask = MT_WTBLOFF_RSCR_RCPI_MODE | MT_WTBLOFF_RSCR_RCPI_PARAM;
	set = FIELD_PREP(MT_WTBLOFF_RSCR_RCPI_MODE, 0) |
	      FIELD_PREP(MT_WTBLOFF_RSCR_RCPI_PARAM, 0x3);
	mt76_rmw(dev, MT_WTBLOFF_RSCR(band), mask, set);
}

static void mt7996_mac_init_basic_rates(struct mt7996_dev *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt76_rates); i++) {
		u16 rate = mt76_rates[i].hw_value;
		u16 idx = MT7996_BASIC_RATES_TBL + i;

		rate = FIELD_PREP(MT_TX_RATE_MODE, rate >> 8) |
		       FIELD_PREP(MT_TX_RATE_IDX, rate & GENMASK(7, 0));
		mt7996_mac_set_fixed_rate_table(dev, idx, rate);
	}
}

void mt7996_mac_init(struct mt7996_dev *dev)
{
#define HIF_TXD_V2_1	4
	int i;

	mt76_clear(dev, MT_MDP_DCR2, MT_MDP_DCR2_RX_TRANS_SHORT);

	for (i = 0; i < mt7996_wtbl_size(dev); i++)
		mt7996_mac_wtbl_update(dev, i,
				       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	if (IS_ENABLED(CONFIG_MT76_LEDS)) {
		i = dev->mphy.leds.pin ? MT_LED_GPIO_MUX3 : MT_LED_GPIO_MUX2;
		mt76_rmw_field(dev, i, MT_LED_GPIO_SEL_MASK, 4);
	}

	/* txs report queue */
	mt76_rmw_field(dev, MT_DMA_TCRF1(0), MT_DMA_TCRF1_QIDX, 0);
	mt76_rmw_field(dev, MT_DMA_TCRF1(1), MT_DMA_TCRF1_QIDX, 6);
	mt76_rmw_field(dev, MT_DMA_TCRF1(2), MT_DMA_TCRF1_QIDX, 0);

	/* rro module init */
	mt7996_mcu_set_rro(dev, UNI_RRO_SET_PLATFORM_TYPE, 2);
	mt7996_mcu_set_rro(dev, UNI_RRO_SET_BYPASS_MODE, 3);
	mt7996_mcu_set_rro(dev, UNI_RRO_SET_TXFREE_PATH, 1);

	mt7996_mcu_wa_cmd(dev, MCU_WA_PARAM_CMD(SET),
			  MCU_WA_PARAM_HW_PATH_HIF_VER,
			  HIF_TXD_V2_1, 0);

	for (i = MT_BAND0; i <= MT_BAND2; i++)
		mt7996_mac_init_band(dev, i);

	mt7996_mac_init_basic_rates(dev);
}

int mt7996_txbf_init(struct mt7996_dev *dev)
{
	int ret;

	if (dev->dbdc_support) {
		ret = mt7996_mcu_set_txbf(dev, BF_MOD_EN_CTRL);
		if (ret)
			return ret;
	}

	/* trigger sounding packets */
	ret = mt7996_mcu_set_txbf(dev, BF_SOUNDING_ON);
	if (ret)
		return ret;

	/* enable eBF */
	return mt7996_mcu_set_txbf(dev, BF_HW_EN_UPDATE);
}

static int mt7996_register_phy(struct mt7996_dev *dev, struct mt7996_phy *phy,
			       enum mt76_band_id band)
{
	struct mt76_phy *mphy;
	u32 mac_ofs, hif1_ofs = 0;
	int ret;

	if (band != MT_BAND1 && band != MT_BAND2)
		return 0;

	if ((band == MT_BAND1 && !dev->dbdc_support) ||
	    (band == MT_BAND2 && !dev->tbtc_support))
		return 0;

	if (phy)
		return 0;

	if (band == MT_BAND2 && dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	mphy = mt76_alloc_phy(&dev->mt76, sizeof(*phy), &mt7996_ops, band);
	if (!mphy)
		return -ENOMEM;

	phy = mphy->priv;
	phy->dev = dev;
	phy->mt76 = mphy;
	mphy->dev->phys[band] = mphy;

	INIT_DELAYED_WORK(&mphy->mac_work, mt7996_mac_work);

	ret = mt7996_eeprom_parse_hw_cap(dev, phy);
	if (ret)
		goto error;

	mac_ofs = band == MT_BAND2 ? MT_EE_MAC_ADDR3 : MT_EE_MAC_ADDR2;
	memcpy(mphy->macaddr, dev->mt76.eeprom.data + mac_ofs, ETH_ALEN);
	/* Make the extra PHY MAC address local without overlapping with
	 * the usual MAC address allocation scheme on multiple virtual interfaces
	 */
	if (!is_valid_ether_addr(mphy->macaddr)) {
		memcpy(mphy->macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR,
		       ETH_ALEN);
		mphy->macaddr[0] |= 2;
		mphy->macaddr[0] ^= BIT(7);
		if (band == MT_BAND2)
			mphy->macaddr[0] ^= BIT(6);
	}
	mt76_eeprom_override(mphy);

	/* init wiphy according to mphy and phy */
	mt7996_init_wiphy(mphy->hw);
	ret = mt76_connac_init_tx_queues(phy->mt76,
					 MT_TXQ_ID(band),
					 MT7996_TX_RING_SIZE,
					 MT_TXQ_RING_BASE(band) + hif1_ofs, 0);
	if (ret)
		goto error;

	ret = mt76_register_phy(mphy, true, mt76_rates,
				ARRAY_SIZE(mt76_rates));
	if (ret)
		goto error;

	ret = mt7996_init_debugfs(phy);
	if (ret)
		goto error;

	return 0;

error:
	mphy->dev->phys[band] = NULL;
	ieee80211_free_hw(mphy->hw);
	return ret;
}

static void
mt7996_unregister_phy(struct mt7996_phy *phy, enum mt76_band_id band)
{
	struct mt76_phy *mphy;

	if (!phy)
		return;

	mphy = phy->dev->mt76.phys[band];
	mt76_unregister_phy(mphy);
	ieee80211_free_hw(mphy->hw);
	phy->dev->mt76.phys[band] = NULL;
}

static void mt7996_init_work(struct work_struct *work)
{
	struct mt7996_dev *dev = container_of(work, struct mt7996_dev,
				 init_work);

	mt7996_mcu_set_eeprom(dev);
	mt7996_mac_init(dev);
	mt7996_init_txpower(dev, &dev->mphy.sband_2g.sband);
	mt7996_init_txpower(dev, &dev->mphy.sband_5g.sband);
	mt7996_init_txpower(dev, &dev->mphy.sband_6g.sband);
	mt7996_txbf_init(dev);
}

void mt7996_wfsys_reset(struct mt7996_dev *dev)
{
	mt76_set(dev, MT_WF_SUBSYS_RST, 0x1);
	msleep(20);

	mt76_clear(dev, MT_WF_SUBSYS_RST, 0x1);
	msleep(20);
}

static int mt7996_init_hardware(struct mt7996_dev *dev)
{
	int ret, idx;

	mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

	INIT_WORK(&dev->init_work, mt7996_init_work);

	dev->dbdc_support = true;
	dev->tbtc_support = true;

	ret = mt7996_dma_init(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	ret = mt7996_mcu_init(dev);
	if (ret)
		return ret;

	ret = mt7996_eeprom_init(dev);
	if (ret < 0)
		return ret;

	/* Beacon and mgmt frames should occupy wcid 0 */
	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7996_WTBL_STA);
	if (idx)
		return -ENOSPC;

	dev->mt76.global_wcid.idx = idx;
	dev->mt76.global_wcid.hw_key_idx = -1;
	dev->mt76.global_wcid.tx_info |= MT_WCID_TX_INFO_SET;
	rcu_assign_pointer(dev->mt76.wcid[idx], &dev->mt76.global_wcid);

	return 0;
}

void mt7996_set_stream_vht_txbf_caps(struct mt7996_phy *phy)
{
	int sts;
	u32 *cap;

	if (!phy->mt76->cap.has_5ghz)
		return;

	sts = hweight16(phy->mt76->chainmask);
	cap = &phy->mt76->sband_5g.sband.vht_cap.cap;

	*cap |= IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
		IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE |
		FIELD_PREP(IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK, sts - 1);

	*cap &= ~(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK |
		  IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
		  IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE);

	if (sts < 2)
		return;

	*cap |= IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
		IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE |
		FIELD_PREP(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK, sts - 1);
}

static void
mt7996_set_stream_he_txbf_caps(struct mt7996_phy *phy,
			       struct ieee80211_sta_he_cap *he_cap, int vif)
{
	struct ieee80211_he_cap_elem *elem = &he_cap->he_cap_elem;
	int sts = hweight16(phy->mt76->chainmask);
	u8 c;

#ifdef CONFIG_MAC80211_MESH
	if (vif == NL80211_IFTYPE_MESH_POINT)
		return;
#endif

	elem->phy_cap_info[3] &= ~IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER;
	elem->phy_cap_info[4] &= ~IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER;

	c = IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK |
	    IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK;
	elem->phy_cap_info[5] &= ~c;

	c = IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
	    IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB;
	elem->phy_cap_info[6] &= ~c;

	elem->phy_cap_info[7] &= ~IEEE80211_HE_PHY_CAP7_MAX_NC_MASK;

	c = IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
	    IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
	    IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO;
	elem->phy_cap_info[2] |= c;

	c = IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
	    IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4 |
	    IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4;
	elem->phy_cap_info[4] |= c;

	/* do not support NG16 due to spec D4.0 changes subcarrier idx */
	c = IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
	    IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU;

	if (vif == NL80211_IFTYPE_STATION)
		c |= IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO;

	elem->phy_cap_info[6] |= c;

	if (sts < 2)
		return;

	/* the maximum cap is 4 x 3, (Nr, Nc) = (3, 2) */
	elem->phy_cap_info[7] |= min_t(int, sts - 1, 2) << 3;

	if (vif != NL80211_IFTYPE_AP)
		return;

	elem->phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER;
	elem->phy_cap_info[4] |= IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER;

	c = FIELD_PREP(IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
		       sts - 1) |
	    FIELD_PREP(IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK,
		       sts - 1);
	elem->phy_cap_info[5] |= c;

	c = IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
	    IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB;
	elem->phy_cap_info[6] |= c;

	c = IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ |
	    IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ;
	elem->phy_cap_info[7] |= c;
}

static void
mt7996_init_he_caps(struct mt7996_phy *phy, enum nl80211_band band,
		    struct ieee80211_sband_iftype_data *data,
		    enum nl80211_iftype iftype)
{
	struct ieee80211_sta_he_cap *he_cap = &data->he_cap;
	struct ieee80211_he_cap_elem *he_cap_elem = &he_cap->he_cap_elem;
	struct ieee80211_he_mcs_nss_supp *he_mcs = &he_cap->he_mcs_nss_supp;
	int i, nss = hweight8(phy->mt76->antenna_mask);
	u16 mcs_map = 0;

	for (i = 0; i < 8; i++) {
		if (i < nss)
			mcs_map |= (IEEE80211_HE_MCS_SUPPORT_0_11 << (i * 2));
		else
			mcs_map |= (IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
	}

	he_cap->has_he = true;

	he_cap_elem->mac_cap_info[0] = IEEE80211_HE_MAC_CAP0_HTC_HE;
	he_cap_elem->mac_cap_info[3] = IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
				       IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3;
	he_cap_elem->mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU;

	if (band == NL80211_BAND_2GHZ)
		he_cap_elem->phy_cap_info[0] =
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
	else
		he_cap_elem->phy_cap_info[0] =
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;

	he_cap_elem->phy_cap_info[1] = IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
	he_cap_elem->phy_cap_info[2] = IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
				       IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ;

	switch (iftype) {
	case NL80211_IFTYPE_AP:
		he_cap_elem->mac_cap_info[0] |= IEEE80211_HE_MAC_CAP0_TWT_RES;
		he_cap_elem->mac_cap_info[2] |= IEEE80211_HE_MAC_CAP2_BSR;
		he_cap_elem->mac_cap_info[4] |= IEEE80211_HE_MAC_CAP4_BQR;
		he_cap_elem->mac_cap_info[5] |=
			IEEE80211_HE_MAC_CAP5_OM_CTRL_UL_MU_DATA_DIS_RX;
		he_cap_elem->phy_cap_info[3] |=
			IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK |
			IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_QPSK;
		he_cap_elem->phy_cap_info[6] |=
			IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE |
			IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT;
		he_cap_elem->phy_cap_info[9] |=
			IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU |
			IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU;
		break;
	case NL80211_IFTYPE_STATION:
		he_cap_elem->mac_cap_info[1] |=
			IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US;

		if (band == NL80211_BAND_2GHZ)
			he_cap_elem->phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G;
		else
			he_cap_elem->phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G;

		he_cap_elem->phy_cap_info[1] |=
			IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
			IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US;
		he_cap_elem->phy_cap_info[3] |=
			IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK |
			IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_QPSK;
		he_cap_elem->phy_cap_info[6] |=
			IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB |
			IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE |
			IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT;
		he_cap_elem->phy_cap_info[7] |=
			IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP |
			IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI;
		he_cap_elem->phy_cap_info[8] |=
			IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G |
			IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU |
			IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU |
			IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484;
		he_cap_elem->phy_cap_info[9] |=
			IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM |
			IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK |
			IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU |
			IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU |
			IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
			IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB;
		break;
	default:
		break;
	}

	he_mcs->rx_mcs_80 = cpu_to_le16(mcs_map);
	he_mcs->tx_mcs_80 = cpu_to_le16(mcs_map);
	he_mcs->rx_mcs_160 = cpu_to_le16(mcs_map);
	he_mcs->tx_mcs_160 = cpu_to_le16(mcs_map);

	mt7996_set_stream_he_txbf_caps(phy, he_cap, iftype);

	memset(he_cap->ppe_thres, 0, sizeof(he_cap->ppe_thres));
	if (he_cap_elem->phy_cap_info[6] &
	    IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
		mt76_connac_gen_ppe_thresh(he_cap->ppe_thres, nss);
	} else {
		he_cap_elem->phy_cap_info[9] |=
			u8_encode_bits(IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US,
				       IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK);
	}

	if (band == NL80211_BAND_6GHZ) {
		u16 cap = IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS |
			  IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS;

		cap |= u16_encode_bits(IEEE80211_HT_MPDU_DENSITY_0_5,
				       IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START) |
		       u16_encode_bits(IEEE80211_VHT_MAX_AMPDU_1024K,
				       IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP) |
		       u16_encode_bits(IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454,
				       IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN);

		data->he_6ghz_capa.capa = cpu_to_le16(cap);
	}
}

static void
mt7996_init_eht_caps(struct mt7996_phy *phy, enum nl80211_band band,
		     struct ieee80211_sband_iftype_data *data,
		     enum nl80211_iftype iftype)
{
	struct ieee80211_sta_eht_cap *eht_cap = &data->eht_cap;
	struct ieee80211_eht_cap_elem_fixed *eht_cap_elem = &eht_cap->eht_cap_elem;
	struct ieee80211_eht_mcs_nss_supp *eht_nss = &eht_cap->eht_mcs_nss_supp;
	enum nl80211_chan_width width = phy->mt76->chandef.width;
	int nss = hweight8(phy->mt76->antenna_mask);
	int sts = hweight16(phy->mt76->chainmask);
	u8 val;

	if (!phy->dev->has_eht)
		return;

	eht_cap->has_eht = true;

	eht_cap_elem->mac_cap_info[0] =
		IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS |
		IEEE80211_EHT_MAC_CAP0_OM_CONTROL;

	eht_cap_elem->phy_cap_info[0] =
		IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ |
		IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI |
		IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER |
		IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE;

	eht_cap_elem->phy_cap_info[0] |=
		u8_encode_bits(u8_get_bits(sts - 1, BIT(0)),
			       IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK);

	eht_cap_elem->phy_cap_info[1] =
		u8_encode_bits(u8_get_bits(sts - 1, GENMASK(2, 1)),
			       IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK) |
		u8_encode_bits(sts - 1,
			       IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK) |
		u8_encode_bits(sts - 1,
			       IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_320MHZ_MASK);

	eht_cap_elem->phy_cap_info[2] =
		u8_encode_bits(sts - 1, IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_80MHZ_MASK) |
		u8_encode_bits(sts - 1, IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_160MHZ_MASK) |
		u8_encode_bits(sts - 1, IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_320MHZ_MASK);

	eht_cap_elem->phy_cap_info[3] =
		IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK |
		IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK |
		IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK |
		IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
		IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK |
		IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK |
		IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK;

	eht_cap_elem->phy_cap_info[4] =
		u8_encode_bits(min_t(int, sts - 1, 2),
			       IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK);

	eht_cap_elem->phy_cap_info[5] =
		IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK |
		u8_encode_bits(IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_16US,
			       IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK) |
		u8_encode_bits(u8_get_bits(0x11, GENMASK(1, 0)),
			       IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK);

	val = width == NL80211_CHAN_WIDTH_320 ? 0xf :
	      width == NL80211_CHAN_WIDTH_160 ? 0x7 :
	      width == NL80211_CHAN_WIDTH_80 ? 0x3 : 0x1;
	eht_cap_elem->phy_cap_info[6] =
		u8_encode_bits(u8_get_bits(0x11, GENMASK(4, 2)),
			       IEEE80211_EHT_PHY_CAP6_MAX_NUM_SUPP_EHT_LTF_MASK) |
		u8_encode_bits(val, IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK);

	eht_cap_elem->phy_cap_info[7] =
		IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ |
		IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ |
		IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_320MHZ |
		IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ |
		IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ |
		IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_320MHZ;

	val = u8_encode_bits(nss, IEEE80211_EHT_MCS_NSS_RX) |
	      u8_encode_bits(nss, IEEE80211_EHT_MCS_NSS_TX);
#define SET_EHT_MAX_NSS(_bw, _val) do {				\
		eht_nss->bw._##_bw.rx_tx_mcs9_max_nss = _val;	\
		eht_nss->bw._##_bw.rx_tx_mcs11_max_nss = _val;	\
		eht_nss->bw._##_bw.rx_tx_mcs13_max_nss = _val;	\
	} while (0)

	SET_EHT_MAX_NSS(80, val);
	SET_EHT_MAX_NSS(160, val);
	SET_EHT_MAX_NSS(320, val);
#undef SET_EHT_MAX_NSS
}

static void
__mt7996_set_stream_he_eht_caps(struct mt7996_phy *phy,
				struct ieee80211_supported_band *sband,
				enum nl80211_band band)
{
	struct ieee80211_sband_iftype_data *data = phy->iftype[band];
	int i, n = 0;

	for (i = 0; i < NUM_NL80211_IFTYPES; i++) {
		switch (i) {
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_AP:
#ifdef CONFIG_MAC80211_MESH
		case NL80211_IFTYPE_MESH_POINT:
#endif
			break;
		default:
			continue;
		}

		data[n].types_mask = BIT(i);
		mt7996_init_he_caps(phy, band, &data[n], i);
		mt7996_init_eht_caps(phy, band, &data[n], i);

		n++;
	}

	sband->iftype_data = data;
	sband->n_iftype_data = n;
}

void mt7996_set_stream_he_eht_caps(struct mt7996_phy *phy)
{
	if (phy->mt76->cap.has_2ghz)
		__mt7996_set_stream_he_eht_caps(phy, &phy->mt76->sband_2g.sband,
						NL80211_BAND_2GHZ);

	if (phy->mt76->cap.has_5ghz)
		__mt7996_set_stream_he_eht_caps(phy, &phy->mt76->sband_5g.sband,
						NL80211_BAND_5GHZ);

	if (phy->mt76->cap.has_6ghz)
		__mt7996_set_stream_he_eht_caps(phy, &phy->mt76->sband_6g.sband,
						NL80211_BAND_6GHZ);
}

int mt7996_register_device(struct mt7996_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	int ret;

	dev->phy.dev = dev;
	dev->phy.mt76 = &dev->mt76.phy;
	dev->mt76.phy.priv = &dev->phy;
	INIT_WORK(&dev->rc_work, mt7996_mac_sta_rc_work);
	INIT_DELAYED_WORK(&dev->mphy.mac_work, mt7996_mac_work);
	INIT_LIST_HEAD(&dev->sta_rc_list);
	INIT_LIST_HEAD(&dev->twt_list);

	init_waitqueue_head(&dev->reset_wait);
	INIT_WORK(&dev->reset_work, mt7996_mac_reset_work);
	INIT_WORK(&dev->dump_work, mt7996_mac_dump_work);
	mutex_init(&dev->dump_mutex);

	ret = mt7996_init_hardware(dev);
	if (ret)
		return ret;

	mt7996_init_wiphy(hw);

	/* init led callbacks */
	if (IS_ENABLED(CONFIG_MT76_LEDS)) {
		dev->mphy.leds.cdev.brightness_set = mt7996_led_set_brightness;
		dev->mphy.leds.cdev.blink_set = mt7996_led_set_blink;
	}

	ret = mt76_register_device(&dev->mt76, true, mt76_rates,
				   ARRAY_SIZE(mt76_rates));
	if (ret)
		return ret;

	ieee80211_queue_work(mt76_hw(dev), &dev->init_work);

	ret = mt7996_register_phy(dev, mt7996_phy2(dev), MT_BAND1);
	if (ret)
		return ret;

	ret = mt7996_register_phy(dev, mt7996_phy3(dev), MT_BAND2);
	if (ret)
		return ret;

	dev->recovery.hw_init_done = true;

	ret = mt7996_init_debugfs(&dev->phy);
	if (ret)
		return ret;

	return mt7996_coredump_register(dev);
}

void mt7996_unregister_device(struct mt7996_dev *dev)
{
	mt7996_unregister_phy(mt7996_phy3(dev), MT_BAND2);
	mt7996_unregister_phy(mt7996_phy2(dev), MT_BAND1);
	mt7996_coredump_unregister(dev);
	mt76_unregister_device(&dev->mt76);
	mt7996_mcu_exit(dev);
	mt7996_tx_token_put(dev);
	mt7996_dma_cleanup(dev);
	tasklet_disable(&dev->mt76.irq_tasklet);

	mt76_free_device(&dev->mt76);
}
