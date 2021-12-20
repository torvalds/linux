// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/etherdevice.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/thermal.h>
#include "mt7915.h"
#include "mac.h"
#include "mcu.h"
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
		.max = MT7915_MAX_INTERFACES,
		.types = BIT(NL80211_IFTYPE_STATION)
	}
};

static const struct ieee80211_iface_combination if_comb[] = {
	{
		.limits = if_limits,
		.n_limits = ARRAY_SIZE(if_limits),
		.max_interfaces = MT7915_MAX_INTERFACES,
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

static ssize_t mt7915_thermal_temp_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct mt7915_phy *phy = dev_get_drvdata(dev);
	int i = to_sensor_dev_attr(attr)->index;
	int temperature;

	if (i)
		return sprintf(buf, "%u\n", phy->throttle_temp[i - 1] * 1000);

	temperature = mt7915_mcu_get_temperature(phy);
	if (temperature < 0)
		return temperature;

	/* display in millidegree celcius */
	return sprintf(buf, "%u\n", temperature * 1000);
}

static ssize_t mt7915_thermal_temp_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct mt7915_phy *phy = dev_get_drvdata(dev);
	int ret, i = to_sensor_dev_attr(attr)->index;
	long val;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	mutex_lock(&phy->dev->mt76.mutex);
	val = clamp_val(DIV_ROUND_CLOSEST(val, 1000), 60, 130);
	phy->throttle_temp[i - 1] = val;
	mutex_unlock(&phy->dev->mt76.mutex);

	return count;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, mt7915_thermal_temp, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_crit, mt7915_thermal_temp, 1);
static SENSOR_DEVICE_ATTR_RW(temp1_max, mt7915_thermal_temp, 2);

static struct attribute *mt7915_hwmon_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mt7915_hwmon);

static int
mt7915_thermal_get_max_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long *state)
{
	*state = MT7915_THERMAL_THROTTLE_MAX;

	return 0;
}

static int
mt7915_thermal_get_cur_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long *state)
{
	struct mt7915_phy *phy = cdev->devdata;

	*state = phy->throttle_state;

	return 0;
}

static int
mt7915_thermal_set_cur_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long state)
{
	struct mt7915_phy *phy = cdev->devdata;
	int ret;

	if (state > MT7915_THERMAL_THROTTLE_MAX)
		return -EINVAL;

	if (phy->throttle_temp[0] > phy->throttle_temp[1])
		return 0;

	if (state == phy->throttle_state)
		return 0;

	ret = mt7915_mcu_set_thermal_throttling(phy, state);
	if (ret)
		return ret;

	phy->throttle_state = state;

	return 0;
}

static const struct thermal_cooling_device_ops mt7915_thermal_ops = {
	.get_max_state = mt7915_thermal_get_max_throttle_state,
	.get_cur_state = mt7915_thermal_get_cur_throttle_state,
	.set_cur_state = mt7915_thermal_set_cur_throttle_state,
};

static void mt7915_unregister_thermal(struct mt7915_phy *phy)
{
	struct wiphy *wiphy = phy->mt76->hw->wiphy;

	if (!phy->cdev)
	    return;

	sysfs_remove_link(&wiphy->dev.kobj, "cooling_device");
	thermal_cooling_device_unregister(phy->cdev);
}

static int mt7915_thermal_init(struct mt7915_phy *phy)
{
	struct wiphy *wiphy = phy->mt76->hw->wiphy;
	struct thermal_cooling_device *cdev;
	struct device *hwmon;
	const char *name;

	name = devm_kasprintf(&wiphy->dev, GFP_KERNEL, "mt7915_%s",
			      wiphy_name(wiphy));

	cdev = thermal_cooling_device_register(name, phy, &mt7915_thermal_ops);
	if (!IS_ERR(cdev)) {
		if (sysfs_create_link(&wiphy->dev.kobj, &cdev->device.kobj,
				      "cooling_device") < 0)
			thermal_cooling_device_unregister(cdev);
		else
			phy->cdev = cdev;
	}

	if (!IS_REACHABLE(CONFIG_HWMON))
		return 0;

	hwmon = devm_hwmon_device_register_with_groups(&wiphy->dev, name, phy,
						       mt7915_hwmon_groups);
	if (IS_ERR(hwmon))
		return PTR_ERR(hwmon);

	/* initialize critical/maximum high temperature */
	phy->throttle_temp[0] = 110;
	phy->throttle_temp[1] = 120;

	return 0;
}

static void mt7915_led_set_config(struct led_classdev *led_cdev,
				  u8 delay_on, u8 delay_off)
{
	struct mt7915_dev *dev;
	struct mt76_dev *mt76;
	u32 val;

	mt76 = container_of(led_cdev, struct mt76_dev, led_cdev);
	dev = container_of(mt76, struct mt7915_dev, mt76);

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
	if (dev->mt76.led_al)
		val |= MT_LED_CTRL_POLARITY;

	mt76_wr(dev, MT_LED_CTRL(0), val);
	mt76_clear(dev, MT_LED_CTRL(0), MT_LED_CTRL_KICK);
}

static int mt7915_led_set_blink(struct led_classdev *led_cdev,
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

	mt7915_led_set_config(led_cdev, delta_on, delta_off);

	return 0;
}

static void mt7915_led_set_brightness(struct led_classdev *led_cdev,
				      enum led_brightness brightness)
{
	if (!brightness)
		mt7915_led_set_config(led_cdev, 0, 0xff);
	else
		mt7915_led_set_config(led_cdev, 0xff, 0);
}

static void
mt7915_init_txpower(struct mt7915_dev *dev,
		    struct ieee80211_supported_band *sband)
{
	int i, n_chains = hweight8(dev->mphy.antenna_mask);
	int nss_delta = mt76_tx_power_nss_delta(n_chains);
	int pwr_delta = mt7915_eeprom_get_power_delta(dev, sband->band);
	struct mt76_power_limits limits;

	for (i = 0; i < sband->n_channels; i++) {
		struct ieee80211_channel *chan = &sband->channels[i];
		u32 target_power = 0;
		int j;

		for (j = 0; j < n_chains; j++) {
			u32 val;

			val = mt7915_eeprom_get_target_power(dev, chan, j);
			target_power = max(target_power, val);
		}

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
mt7915_regd_notifier(struct wiphy *wiphy,
		     struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	struct mt7915_phy *phy = mphy->priv;
	struct cfg80211_chan_def *chandef = &mphy->chandef;

	memcpy(dev->mt76.alpha2, request->alpha2, sizeof(dev->mt76.alpha2));
	dev->mt76.region = request->dfs_region;

	mt7915_init_txpower(dev, &mphy->sband_2g.sband);
	mt7915_init_txpower(dev, &mphy->sband_5g.sband);

	if (!(chandef->chan->flags & IEEE80211_CHAN_RADAR))
		return;

	mt7915_dfs_init_radar_detector(phy);
}

static void
mt7915_init_wiphy(struct ieee80211_hw *hw)
{
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct wiphy *wiphy = hw->wiphy;
	struct mt7915_dev *dev = phy->dev;

	hw->queues = 4;
	hw->max_rx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF;
	hw->max_tx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF;
	hw->netdev_features = NETIF_F_RXCSUM;

	hw->radiotap_timestamp.units_pos =
		IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US;

	phy->slottime = 9;

	hw->sta_data_size = sizeof(struct mt7915_sta);
	hw->vif_data_size = sizeof(struct mt7915_vif);

	wiphy->iface_combinations = if_comb;
	wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);
	wiphy->reg_notifier = mt7915_regd_notifier;
	wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BSS_COLOR);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_VHT_IBSS);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_LEGACY);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_HT);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_VHT);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_HE);

	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, SUPPORTS_TX_ENCAP_OFFLOAD);
	ieee80211_hw_set(hw, SUPPORTS_RX_DECAP_OFFLOAD);
	ieee80211_hw_set(hw, WANT_MONITOR_VIF);

	hw->max_tx_fragments = 4;

	if (phy->mt76->cap.has_2ghz)
		phy->mt76->sband_2g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;

	if (phy->mt76->cap.has_5ghz) {
		phy->mt76->sband_5g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;

		if (is_mt7915(&dev->mt76)) {
			phy->mt76->sband_5g.sband.vht_cap.cap |=
				IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991 |
				IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;

			if (!dev->dbdc_support)
				phy->mt76->sband_5g.sband.vht_cap.cap |=
					IEEE80211_VHT_CAP_SHORT_GI_160 |
					IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ;
		} else {
			phy->mt76->sband_5g.sband.vht_cap.cap |=
				IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
				IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;

			/* mt7916 dbdc with 2g 2x2 bw40 and 5g 2x2 bw160c */
			phy->mt76->sband_5g.sband.vht_cap.cap |=
				IEEE80211_VHT_CAP_SHORT_GI_160 |
				IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
		}
	}

	mt76_set_stream_caps(phy->mt76, true);
	mt7915_set_stream_vht_txbf_caps(phy);
	mt7915_set_stream_he_caps(phy);

	wiphy->available_antennas_rx = phy->mt76->antenna_mask;
	wiphy->available_antennas_tx = phy->mt76->antenna_mask;
}

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

	mt76_rmw_field(dev, MT_DMA_DCR0(band), MT_DMA_DCR0_MAX_RX_LEN, 0x680);
	/* disable rx rate report by default due to hw issues */
	mt76_clear(dev, MT_DMA_DCR0(band), MT_DMA_DCR0_RXD_G5_EN);
}

static void mt7915_mac_init(struct mt7915_dev *dev)
{
	int i;
	u32 rx_len = is_mt7915(&dev->mt76) ? 0x400 : 0x680;

	/* config pse qid6 wfdma port selection */
	if (!is_mt7915(&dev->mt76) && dev->hif2)
		mt76_rmw(dev, MT_WF_PP_TOP_RXQ_WFDMA_CF_5, 0,
			 MT_WF_PP_TOP_RXQ_QID6_WFDMA_HIF_SEL_MASK);

	mt76_rmw_field(dev, MT_MDP_DCR1, MT_MDP_DCR1_MAX_RX_LEN, rx_len);

	/* enable hardware de-agg */
	mt76_set(dev, MT_MDP_DCR0, MT_MDP_DCR0_DAMSDU_EN);

	for (i = 0; i < mt7915_wtbl_size(dev); i++)
		mt7915_mac_wtbl_update(dev, i,
				       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
	for (i = 0; i < 2; i++)
		mt7915_mac_init_band(dev, i);

	if (IS_ENABLED(CONFIG_MT76_LEDS)) {
		i = dev->mt76.led_pin ? MT_LED_GPIO_MUX3 : MT_LED_GPIO_MUX2;
		mt76_rmw_field(dev, i, MT_LED_GPIO_SEL_MASK, 4);
	}
}

static int mt7915_txbf_init(struct mt7915_dev *dev)
{
	int ret;

	if (dev->dbdc_support) {
		ret = mt7915_mcu_set_txbf(dev, MT_BF_MODULE_UPDATE);
		if (ret)
			return ret;
	}

	/* trigger sounding packets */
	ret = mt7915_mcu_set_txbf(dev, MT_BF_SOUNDING_ON);
	if (ret)
		return ret;

	/* enable eBF */
	return mt7915_mcu_set_txbf(dev, MT_BF_TYPE_UPDATE);
}

static int mt7915_register_ext_phy(struct mt7915_dev *dev)
{
	struct mt7915_phy *phy = mt7915_ext_phy(dev);
	struct mt76_phy *mphy;
	int ret;

	if (!dev->dbdc_support)
		return 0;

	if (phy)
		return 0;

	mphy = mt76_alloc_phy(&dev->mt76, sizeof(*phy), &mt7915_ops);
	if (!mphy)
		return -ENOMEM;

	phy = mphy->priv;
	phy->dev = dev;
	phy->mt76 = mphy;

	INIT_DELAYED_WORK(&mphy->mac_work, mt7915_mac_work);

	mt7915_eeprom_parse_hw_cap(dev, phy);

	memcpy(mphy->macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR2,
	       ETH_ALEN);
	/* Make the secondary PHY MAC address local without overlapping with
	 * the usual MAC address allocation scheme on multiple virtual interfaces
	 */
	if (!is_valid_ether_addr(mphy->macaddr)) {
		memcpy(mphy->macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR,
		       ETH_ALEN);
		mphy->macaddr[0] |= 2;
		mphy->macaddr[0] ^= BIT(7);
	}
	mt76_eeprom_override(mphy);

	/* init wiphy according to mphy and phy */
	mt7915_init_wiphy(mphy->hw);
	ret = mt7915_init_tx_queues(phy, MT_TXQ_ID(1),
				    MT7915_TX_RING_SIZE,
				    MT_TXQ_RING_BASE(1));
	if (ret)
		goto error;

	ret = mt76_register_phy(mphy, true, mt76_rates,
				ARRAY_SIZE(mt76_rates));
	if (ret)
		goto error;

	ret = mt7915_thermal_init(phy);
	if (ret)
		goto error;

	ret = mt7915_init_debugfs(phy);
	if (ret)
		goto error;

	return 0;

error:
	ieee80211_free_hw(mphy->hw);
	return ret;
}

static void mt7915_init_work(struct work_struct *work)
{
	struct mt7915_dev *dev = container_of(work, struct mt7915_dev,
				 init_work);

	mt7915_mcu_set_eeprom(dev);
	mt7915_mac_init(dev);
	mt7915_init_txpower(dev, &dev->mphy.sband_2g.sband);
	mt7915_init_txpower(dev, &dev->mphy.sband_5g.sband);
	mt7915_txbf_init(dev);
}

static void mt7915_wfsys_reset(struct mt7915_dev *dev)
{
#define MT_MCU_DUMMY_RANDOM	GENMASK(15, 0)
#define MT_MCU_DUMMY_DEFAULT	GENMASK(31, 16)

	if (is_mt7915(&dev->mt76)) {
		u32 val = MT_TOP_PWR_KEY | MT_TOP_PWR_SW_PWR_ON | MT_TOP_PWR_PWR_ON;

		mt76_wr(dev, MT_MCU_WFDMA0_DUMMY_CR, MT_MCU_DUMMY_RANDOM);

		/* change to software control */
		val |= MT_TOP_PWR_SW_RST;
		mt76_wr(dev, MT_TOP_PWR_CTRL, val);

		/* reset wfsys */
		val &= ~MT_TOP_PWR_SW_RST;
		mt76_wr(dev, MT_TOP_PWR_CTRL, val);

		/* release wfsys then mcu re-excutes romcode */
		val |= MT_TOP_PWR_SW_RST;
		mt76_wr(dev, MT_TOP_PWR_CTRL, val);

		/* switch to hw control */
		val &= ~MT_TOP_PWR_SW_RST;
		val |= MT_TOP_PWR_HW_CTRL;
		mt76_wr(dev, MT_TOP_PWR_CTRL, val);

		/* check whether mcu resets to default */
		if (!mt76_poll_msec(dev, MT_MCU_WFDMA0_DUMMY_CR,
				    MT_MCU_DUMMY_DEFAULT, MT_MCU_DUMMY_DEFAULT,
				    1000)) {
			dev_err(dev->mt76.dev, "wifi subsystem reset failure\n");
			return;
		}

		/* wfsys reset won't clear host registers */
		mt76_clear(dev, MT_TOP_MISC, MT_TOP_MISC_FW_STATE);

		msleep(100);
	} else {
		mt76_set(dev, MT_WF_SUBSYS_RST, 0x1);
		msleep(20);

		mt76_clear(dev, MT_WF_SUBSYS_RST, 0x1);
		msleep(20);
	}
}

static int mt7915_init_hardware(struct mt7915_dev *dev)
{
	int ret, idx;

	mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

	INIT_WORK(&dev->init_work, mt7915_init_work);

	dev->dbdc_support = is_mt7915(&dev->mt76) ?
			    !!(mt76_rr(dev, MT_HW_BOUND) & BIT(5)) : true;

	/* If MCU was already running, it is likely in a bad state */
	if (mt76_get_field(dev, MT_TOP_MISC, MT_TOP_MISC_FW_STATE) >
	    FW_STATE_FW_DOWNLOAD)
		mt7915_wfsys_reset(dev);

	ret = mt7915_dma_init(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	ret = mt7915_mcu_init(dev);
	if (ret) {
		/* Reset and try again */
		mt7915_wfsys_reset(dev);

		ret = mt7915_mcu_init(dev);
		if (ret)
			return ret;
	}

	ret = mt7915_eeprom_init(dev);
	if (ret < 0)
		return ret;

	if (dev->flash_mode) {
		ret = mt7915_mcu_apply_group_cal(dev);
		if (ret)
			return ret;
	}

	/* Beacon and mgmt frames should occupy wcid 0 */
	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7915_WTBL_STA);
	if (idx)
		return -ENOSPC;

	dev->mt76.global_wcid.idx = idx;
	dev->mt76.global_wcid.hw_key_idx = -1;
	dev->mt76.global_wcid.tx_info |= MT_WCID_TX_INFO_SET;
	rcu_assign_pointer(dev->mt76.wcid[idx], &dev->mt76.global_wcid);

	return 0;
}

void mt7915_set_stream_vht_txbf_caps(struct mt7915_phy *phy)
{
	int nss;
	u32 *cap;

	if (!phy->mt76->cap.has_5ghz)
		return;

	nss = hweight8(phy->mt76->chainmask);
	cap = &phy->mt76->sband_5g.sband.vht_cap.cap;

	*cap |= IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
		IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE |
		(3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT);

	*cap &= ~(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK |
		  IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
		  IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE);

	if (nss < 2)
		return;

	*cap |= IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
		IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE |
		FIELD_PREP(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
			   nss - 1);
}

static void
mt7915_set_stream_he_txbf_caps(struct ieee80211_sta_he_cap *he_cap,
			       int vif, int nss)
{
	struct ieee80211_he_cap_elem *elem = &he_cap->he_cap_elem;
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

	if (nss < 2)
		return;

	/* the maximum cap is 4 x 3, (Nr, Nc) = (3, 2) */
	elem->phy_cap_info[7] |= min_t(int, nss - 1, 2) << 3;

	if (vif != NL80211_IFTYPE_AP)
		return;

	elem->phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER;
	elem->phy_cap_info[4] |= IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER;

	/* num_snd_dim
	 * for mt7915, max supported nss is 2 for bw > 80MHz
	 */
	c = (nss - 1) |
	    IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_2;
	elem->phy_cap_info[5] |= c;

	c = IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
	    IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB;
	elem->phy_cap_info[6] |= c;
}

static void
mt7915_gen_ppe_thresh(u8 *he_ppet, int nss)
{
	u8 i, ppet_bits, ppet_size, ru_bit_mask = 0x7; /* HE80 */
	u8 ppet16_ppet8_ru3_ru0[] = {0x1c, 0xc7, 0x71};

	he_ppet[0] = FIELD_PREP(IEEE80211_PPE_THRES_NSS_MASK, nss - 1) |
		     FIELD_PREP(IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK,
				ru_bit_mask);

	ppet_bits = IEEE80211_PPE_THRES_INFO_PPET_SIZE *
		    nss * hweight8(ru_bit_mask) * 2;
	ppet_size = DIV_ROUND_UP(ppet_bits, 8);

	for (i = 0; i < ppet_size - 1; i++)
		he_ppet[i + 1] = ppet16_ppet8_ru3_ru0[i % 3];

	he_ppet[i + 1] = ppet16_ppet8_ru3_ru0[i % 3] &
			 (0xff >> (8 - (ppet_bits - 1) % 8));
}

static int
mt7915_init_he_caps(struct mt7915_phy *phy, enum nl80211_band band,
		    struct ieee80211_sband_iftype_data *data)
{
	int i, idx = 0, nss = hweight8(phy->mt76->chainmask);
	u16 mcs_map = 0;
	u16 mcs_map_160 = 0;

	for (i = 0; i < 8; i++) {
		if (i < nss)
			mcs_map |= (IEEE80211_HE_MCS_SUPPORT_0_11 << (i * 2));
		else
			mcs_map |= (IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));

		/* Can do 1/2 of NSS streams in 160Mhz mode. */
		if (i < nss / 2)
			mcs_map_160 |= (IEEE80211_HE_MCS_SUPPORT_0_11 << (i * 2));
		else
			mcs_map_160 |= (IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
	}

	for (i = 0; i < NUM_NL80211_IFTYPES; i++) {
		struct ieee80211_sta_he_cap *he_cap = &data[idx].he_cap;
		struct ieee80211_he_cap_elem *he_cap_elem =
				&he_cap->he_cap_elem;
		struct ieee80211_he_mcs_nss_supp *he_mcs =
				&he_cap->he_mcs_nss_supp;

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

		data[idx].types_mask = BIT(i);
		he_cap->has_he = true;

		he_cap_elem->mac_cap_info[0] =
			IEEE80211_HE_MAC_CAP0_HTC_HE;
		he_cap_elem->mac_cap_info[3] =
			IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
			IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3;
		he_cap_elem->mac_cap_info[4] =
			IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU;

		if (band == NL80211_BAND_2GHZ)
			he_cap_elem->phy_cap_info[0] =
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
		else if (band == NL80211_BAND_5GHZ)
			he_cap_elem->phy_cap_info[0] =
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G;

		he_cap_elem->phy_cap_info[1] =
			IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
		he_cap_elem->phy_cap_info[2] =
			IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
			IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ;

		switch (i) {
		case NL80211_IFTYPE_AP:
			he_cap_elem->mac_cap_info[0] |=
				IEEE80211_HE_MAC_CAP0_TWT_RES;
			he_cap_elem->mac_cap_info[2] |=
				IEEE80211_HE_MAC_CAP2_BSR;
			he_cap_elem->mac_cap_info[4] |=
				IEEE80211_HE_MAC_CAP4_BQR;
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
			else if (band == NL80211_BAND_5GHZ)
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
		}

		he_mcs->rx_mcs_80 = cpu_to_le16(mcs_map);
		he_mcs->tx_mcs_80 = cpu_to_le16(mcs_map);
		he_mcs->rx_mcs_160 = cpu_to_le16(mcs_map_160);
		he_mcs->tx_mcs_160 = cpu_to_le16(mcs_map_160);
		he_mcs->rx_mcs_80p80 = cpu_to_le16(mcs_map_160);
		he_mcs->tx_mcs_80p80 = cpu_to_le16(mcs_map_160);

		mt7915_set_stream_he_txbf_caps(he_cap, i, nss);

		memset(he_cap->ppe_thres, 0, sizeof(he_cap->ppe_thres));
		if (he_cap_elem->phy_cap_info[6] &
		    IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
			mt7915_gen_ppe_thresh(he_cap->ppe_thres, nss);
		} else {
			he_cap_elem->phy_cap_info[9] |=
				u8_encode_bits(IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US,
					       IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK);
		}
		idx++;
	}

	return idx;
}

void mt7915_set_stream_he_caps(struct mt7915_phy *phy)
{
	struct ieee80211_sband_iftype_data *data;
	struct ieee80211_supported_band *band;
	int n;

	if (phy->mt76->cap.has_2ghz) {
		data = phy->iftype[NL80211_BAND_2GHZ];
		n = mt7915_init_he_caps(phy, NL80211_BAND_2GHZ, data);

		band = &phy->mt76->sband_2g.sband;
		band->iftype_data = data;
		band->n_iftype_data = n;
	}

	if (phy->mt76->cap.has_5ghz) {
		data = phy->iftype[NL80211_BAND_5GHZ];
		n = mt7915_init_he_caps(phy, NL80211_BAND_5GHZ, data);

		band = &phy->mt76->sband_5g.sband;
		band->iftype_data = data;
		band->n_iftype_data = n;
	}
}

static void mt7915_unregister_ext_phy(struct mt7915_dev *dev)
{
	struct mt7915_phy *phy = mt7915_ext_phy(dev);
	struct mt76_phy *mphy = dev->mt76.phy2;

	if (!phy)
		return;

	mt7915_unregister_thermal(phy);
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
	INIT_WORK(&dev->rc_work, mt7915_mac_sta_rc_work);
	INIT_DELAYED_WORK(&dev->mphy.mac_work, mt7915_mac_work);
	INIT_LIST_HEAD(&dev->sta_rc_list);
	INIT_LIST_HEAD(&dev->sta_poll_list);
	INIT_LIST_HEAD(&dev->twt_list);
	spin_lock_init(&dev->sta_poll_lock);

	init_waitqueue_head(&dev->reset_wait);
	INIT_WORK(&dev->reset_work, mt7915_mac_reset_work);

	ret = mt7915_init_hardware(dev);
	if (ret)
		return ret;

	mt7915_init_wiphy(hw);

	dev->phy.dfs_state = -1;

#ifdef CONFIG_NL80211_TESTMODE
	dev->mt76.test_ops = &mt7915_testmode_ops;
#endif

	/* init led callbacks */
	if (IS_ENABLED(CONFIG_MT76_LEDS)) {
		dev->mt76.led_cdev.brightness_set = mt7915_led_set_brightness;
		dev->mt76.led_cdev.blink_set = mt7915_led_set_blink;
	}

	ret = mt76_register_device(&dev->mt76, true, mt76_rates,
				   ARRAY_SIZE(mt76_rates));
	if (ret)
		return ret;

	ret = mt7915_thermal_init(&dev->phy);
	if (ret)
		return ret;

	ieee80211_queue_work(mt76_hw(dev), &dev->init_work);

	ret = mt7915_register_ext_phy(dev);
	if (ret)
		return ret;

	return mt7915_init_debugfs(&dev->phy);
}

void mt7915_unregister_device(struct mt7915_dev *dev)
{
	mt7915_unregister_ext_phy(dev);
	mt7915_unregister_thermal(&dev->phy);
	mt76_unregister_device(&dev->mt76);
	mt7915_mcu_exit(dev);
	mt7915_tx_token_put(dev);
	mt7915_dma_cleanup(dev);
	tasklet_disable(&dev->irq_tasklet);

	mt76_free_device(&dev->mt76);
}
