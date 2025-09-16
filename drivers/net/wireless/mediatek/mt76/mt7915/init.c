// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/etherdevice.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/of.h>
#include <linux/thermal.h>
#include "mt7915.h"
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
				       BIT(NL80211_CHAN_WIDTH_160),
	}
};

static ssize_t mt7915_thermal_temp_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct mt7915_phy *phy = dev_get_drvdata(dev);
	int i = to_sensor_dev_attr(attr)->index;
	int temperature;

	switch (i) {
	case 0:
		mutex_lock(&phy->dev->mt76.mutex);
		temperature = mt7915_mcu_get_temperature(phy);
		mutex_unlock(&phy->dev->mt76.mutex);
		if (temperature < 0)
			return temperature;
		/* display in millidegree celcius */
		return sprintf(buf, "%u\n", temperature * 1000);
	case 1:
	case 2:
		return sprintf(buf, "%u\n",
			       phy->throttle_temp[i - 1] * 1000);
	case 3:
		return sprintf(buf, "%hhu\n", phy->throttle_state);
	default:
		return -EINVAL;
	}
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
	val = DIV_ROUND_CLOSEST(clamp_val(val, 60 * 1000, 130 * 1000), 1000);

	if ((i - 1 == MT7915_CRIT_TEMP_IDX &&
	     val > phy->throttle_temp[MT7915_MAX_TEMP_IDX]) ||
	    (i - 1 == MT7915_MAX_TEMP_IDX &&
	     val < phy->throttle_temp[MT7915_CRIT_TEMP_IDX])) {
		dev_err(phy->dev->mt76.dev,
			"temp1_max shall be greater than temp1_crit.");
		mutex_unlock(&phy->dev->mt76.mutex);
		return -EINVAL;
	}

	phy->throttle_temp[i - 1] = val;
	ret = mt7915_mcu_set_thermal_protect(phy);
	mutex_unlock(&phy->dev->mt76.mutex);
	if (ret)
		return ret;

	return count;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, mt7915_thermal_temp, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_crit, mt7915_thermal_temp, 1);
static SENSOR_DEVICE_ATTR_RW(temp1_max, mt7915_thermal_temp, 2);
static SENSOR_DEVICE_ATTR_RO(throttle1, mt7915_thermal_temp, 3);

static struct attribute *mt7915_hwmon_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_throttle1.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mt7915_hwmon);

static int
mt7915_thermal_get_max_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long *state)
{
	*state = MT7915_CDEV_THROTTLE_MAX;

	return 0;
}

static int
mt7915_thermal_get_cur_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long *state)
{
	struct mt7915_phy *phy = cdev->devdata;

	*state = phy->cdev_state;

	return 0;
}

static int
mt7915_thermal_set_cur_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long state)
{
	struct mt7915_phy *phy = cdev->devdata;
	u8 throttling = MT7915_THERMAL_THROTTLE_MAX - state;
	int ret;

	if (state > MT7915_CDEV_THROTTLE_MAX) {
		dev_err(phy->dev->mt76.dev,
			"please specify a valid throttling state\n");
		return -EINVAL;
	}

	if (state == phy->cdev_state)
		return 0;

	/*
	 * cooling_device convention: 0 = no cooling, more = more cooling
	 * mcu convention: 1 = max cooling, more = less cooling
	 */
	mutex_lock(&phy->dev->mt76.mutex);
	ret = mt7915_mcu_set_thermal_throttling(phy, throttling);
	mutex_unlock(&phy->dev->mt76.mutex);
	if (ret)
		return ret;

	phy->cdev_state = state;

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
	if (!name)
		return -ENOMEM;

	cdev = thermal_cooling_device_register(name, phy, &mt7915_thermal_ops);
	if (!IS_ERR(cdev)) {
		if (sysfs_create_link(&wiphy->dev.kobj, &cdev->device.kobj,
				      "cooling_device") < 0)
			thermal_cooling_device_unregister(cdev);
		else
			phy->cdev = cdev;
	}

	/* initialize critical/maximum high temperature */
	phy->throttle_temp[MT7915_CRIT_TEMP_IDX] = MT7915_CRIT_TEMP;
	phy->throttle_temp[MT7915_MAX_TEMP_IDX] = MT7915_MAX_TEMP;

	if (!IS_REACHABLE(CONFIG_HWMON))
		return 0;

	hwmon = devm_hwmon_device_register_with_groups(&wiphy->dev, name, phy,
						       mt7915_hwmon_groups);
	return PTR_ERR_OR_ZERO(hwmon);
}

static void mt7915_led_set_config(struct led_classdev *led_cdev,
				  u8 delay_on, u8 delay_off)
{
	struct mt7915_dev *dev;
	struct mt76_phy *mphy;
	u32 val;

	mphy = container_of(led_cdev, struct mt76_phy, leds.cdev);
	dev = container_of(mphy->dev, struct mt7915_dev, mt76);

	/* set PWM mode */
	val = FIELD_PREP(MT_LED_STATUS_DURATION, 0xffff) |
	      FIELD_PREP(MT_LED_STATUS_OFF, delay_off) |
	      FIELD_PREP(MT_LED_STATUS_ON, delay_on);
	mt76_wr(dev, MT_LED_STATUS_0(mphy->band_idx), val);
	mt76_wr(dev, MT_LED_STATUS_1(mphy->band_idx), val);

	/* enable LED */
	mt76_wr(dev, MT_LED_EN(mphy->band_idx), 1);

	/* control LED */
	val = MT_LED_CTRL_KICK;
	if (dev->mphy.leds.al)
		val |= MT_LED_CTRL_POLARITY;
	if (mphy->band_idx)
		val |= MT_LED_CTRL_BAND;

	mt76_wr(dev, MT_LED_CTRL(mphy->band_idx), val);
	mt76_clear(dev, MT_LED_CTRL(mphy->band_idx), MT_LED_CTRL_KICK);
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

static void __mt7915_init_txpower(struct mt7915_phy *phy,
				  struct ieee80211_supported_band *sband)
{
	struct mt7915_dev *dev = phy->dev;
	int i, n_chains = hweight16(phy->mt76->chainmask);
	int path_delta = mt76_tx_power_path_delta(n_chains);
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
		target_power = mt76_get_rate_power_limits(phy->mt76, chan,
							  &limits,
							  target_power);
		target_power += path_delta;
		target_power = DIV_ROUND_UP(target_power, 2);
		chan->max_power = min_t(int, chan->max_reg_power,
					target_power);
		chan->orig_mpwr = target_power;
	}
}

void mt7915_init_txpower(struct mt7915_phy *phy)
{
	if (!phy)
		return;

	if (phy->mt76->cap.has_2ghz)
		__mt7915_init_txpower(phy, &phy->mt76->sband_2g.sband);
	if (phy->mt76->cap.has_5ghz)
		__mt7915_init_txpower(phy, &phy->mt76->sband_5g.sband);
	if (phy->mt76->cap.has_6ghz)
		__mt7915_init_txpower(phy, &phy->mt76->sband_6g.sband);
}

static void
mt7915_regd_notifier(struct wiphy *wiphy,
		     struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	struct mt7915_phy *phy = mphy->priv;

	memcpy(dev->mt76.alpha2, request->alpha2, sizeof(dev->mt76.alpha2));
	dev->mt76.region = request->dfs_region;

	if (dev->mt76.region == NL80211_DFS_UNSET)
		mt7915_mcu_rdd_background_enable(phy, NULL);

	mt7915_init_txpower(phy);

	mphy->dfs_state = MT_DFS_STATE_UNKNOWN;
	mt7915_dfs_init_radar_detector(phy);
}

static void
mt7915_init_wiphy(struct mt7915_phy *phy)
{
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_hw *hw = mphy->hw;
	struct mt76_dev *mdev = &phy->dev->mt76;
	struct wiphy *wiphy = hw->wiphy;
	struct mt7915_dev *dev = phy->dev;

	hw->queues = 4;
	hw->max_rx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF_HE;
	hw->max_tx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF_HE;
	hw->netdev_features = NETIF_F_RXCSUM;

	if (mtk_wed_device_active(&mdev->mmio.wed))
		hw->netdev_features |= NETIF_F_HW_TC;

	hw->radiotap_timestamp.units_pos =
		IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US;

	phy->slottime = 9;

	hw->sta_data_size = sizeof(struct mt7915_sta);
	hw->vif_data_size = sizeof(struct mt7915_vif);

	wiphy->iface_combinations = if_comb;
	wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);
	wiphy->reg_notifier = mt7915_regd_notifier;
	wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;
	wiphy->mbssid_max_interfaces = 16;

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

	if (!is_mt7915(&dev->mt76))
		wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_STA_TX_PWR);

	if (mt7915_eeprom_has_background_radar(phy->dev) &&
	    (!mdev->dev->of_node ||
	     !of_property_read_bool(mdev->dev->of_node,
				    "mediatek,disable-radar-background")))
		wiphy_ext_feature_set(wiphy,
				      NL80211_EXT_FEATURE_RADAR_BACKGROUND);

	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, SUPPORTS_TX_ENCAP_OFFLOAD);
	ieee80211_hw_set(hw, SUPPORTS_RX_DECAP_OFFLOAD);
	ieee80211_hw_set(hw, SUPPORTS_MULTI_BSSID);
	ieee80211_hw_set(hw, WANT_MONITOR_VIF);
	ieee80211_hw_set(hw, SUPPORTS_TX_FRAG);

	hw->max_tx_fragments = 4;

	if (phy->mt76->cap.has_2ghz) {
		phy->mt76->sband_2g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;
		if (is_mt7915(&dev->mt76))
			phy->mt76->sband_2g.sband.ht_cap.ampdu_density =
				IEEE80211_HT_MPDU_DENSITY_4;
		else
			phy->mt76->sband_2g.sband.ht_cap.ampdu_density =
				IEEE80211_HT_MPDU_DENSITY_2;
	}

	if (phy->mt76->cap.has_5ghz) {
		struct ieee80211_sta_vht_cap *vht_cap;

		vht_cap = &phy->mt76->sband_5g.sband.vht_cap;
		phy->mt76->sband_5g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;

		if (is_mt7915(&dev->mt76)) {
			phy->mt76->sband_5g.sband.ht_cap.ampdu_density =
				IEEE80211_HT_MPDU_DENSITY_4;

			vht_cap->cap |=
				IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991 |
				IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;

			if (!dev->dbdc_support)
				vht_cap->cap |=
					IEEE80211_VHT_CAP_SHORT_GI_160 |
					FIELD_PREP(IEEE80211_VHT_CAP_EXT_NSS_BW_MASK, 1);
		} else {
			phy->mt76->sband_5g.sband.ht_cap.ampdu_density =
				IEEE80211_HT_MPDU_DENSITY_2;

			vht_cap->cap |=
				IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
				IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;

			/* mt7916 dbdc with 2g 2x2 bw40 and 5g 2x2 bw160c */
			vht_cap->cap |=
				IEEE80211_VHT_CAP_SHORT_GI_160 |
				IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
		}

		if (!is_mt7915(&dev->mt76) || !dev->dbdc_support)
			ieee80211_hw_set(hw, SUPPORTS_VHT_EXT_NSS_BW);
	}

	mt76_set_stream_caps(phy->mt76, true);
	mt7915_set_stream_vht_txbf_caps(phy);
	mt7915_set_stream_he_caps(phy);
	mt7915_init_txpower(phy);

	wiphy->available_antennas_rx = phy->mt76->antenna_mask;
	wiphy->available_antennas_tx = phy->mt76->antenna_mask;

	/* init led callbacks */
	if (IS_ENABLED(CONFIG_MT76_LEDS)) {
		mphy->leds.cdev.brightness_set = mt7915_led_set_brightness;
		mphy->leds.cdev.blink_set = mt7915_led_set_blink;
	}
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

	/* mt7915: disable rx rate report by default due to hw issues */
	mt76_clear(dev, MT_DMA_DCR0(band), MT_DMA_DCR0_RXD_G5_EN);

	/* clear estimated value of EIFS for Rx duration & OBSS time */
	mt76_wr(dev, MT_WF_RMAC_RSVD0(band), MT_WF_RMAC_RSVD0_EIFS_CLR);

	/* clear backoff time for Rx duration  */
	mt76_clear(dev, MT_WF_RMAC_MIB_AIRTIME1(band),
		   MT_WF_RMAC_MIB_NONQOSD_BACKOFF);
	mt76_clear(dev, MT_WF_RMAC_MIB_AIRTIME3(band),
		   MT_WF_RMAC_MIB_QOS01_BACKOFF);
	mt76_clear(dev, MT_WF_RMAC_MIB_AIRTIME4(band),
		   MT_WF_RMAC_MIB_QOS23_BACKOFF);

	/* clear backoff time for Tx duration */
	mt76_clear(dev, MT_WTBLOFF_TOP_ACR(band),
		   MT_WTBLOFF_TOP_ADM_BACKOFFTIME);

	/* exclude estimated backoff time for Tx duration on MT7915 */
	if (is_mt7915(&dev->mt76))
		mt76_set(dev, MT_AGG_ATCR0(band),
			   MT_AGG_ATCR_MAC_BFF_TIME_EN);

	/* clear backoff time and set software compensation for OBSS time */
	mask = MT_WF_RMAC_MIB_OBSS_BACKOFF | MT_WF_RMAC_MIB_ED_OFFSET;
	set = FIELD_PREP(MT_WF_RMAC_MIB_OBSS_BACKOFF, 0) |
	      FIELD_PREP(MT_WF_RMAC_MIB_ED_OFFSET, 4);
	mt76_rmw(dev, MT_WF_RMAC_MIB_AIRTIME0(band), mask, set);

	/* filter out non-resp frames and get instanstaeous signal reporting */
	mask = MT_WTBLOFF_TOP_RSCR_RCPI_MODE | MT_WTBLOFF_TOP_RSCR_RCPI_PARAM;
	set = FIELD_PREP(MT_WTBLOFF_TOP_RSCR_RCPI_MODE, 0) |
	      FIELD_PREP(MT_WTBLOFF_TOP_RSCR_RCPI_PARAM, 0x3);
	mt76_rmw(dev, MT_WTBLOFF_TOP_RSCR(band), mask, set);

	/* MT_TXD5_TX_STATUS_HOST (MPDU format) has higher priority than
	 * MT_AGG_ACR_PPDU_TXS2H (PPDU format) even though ACR bit is set.
	 */
	if (mtk_wed_device_active(&dev->mt76.mmio.wed))
		mt76_set(dev, MT_AGG_ACR4(band), MT_AGG_ACR_PPDU_TXS2H);
}

static void
mt7915_init_led_mux(struct mt7915_dev *dev)
{
	if (!IS_ENABLED(CONFIG_MT76_LEDS))
		return;

	if (dev->dbdc_support) {
		switch (mt76_chip(&dev->mt76)) {
		case 0x7915:
			mt76_rmw_field(dev, MT_LED_GPIO_MUX2,
				       GENMASK(11, 8), 4);
			mt76_rmw_field(dev, MT_LED_GPIO_MUX3,
				       GENMASK(11, 8), 4);
			break;
		case 0x7986:
			mt76_rmw_field(dev, MT_LED_GPIO_MUX0,
				       GENMASK(7, 4), 1);
			mt76_rmw_field(dev, MT_LED_GPIO_MUX0,
				       GENMASK(11, 8), 1);
			break;
		case 0x7916:
			mt76_rmw_field(dev, MT_LED_GPIO_MUX1,
				       GENMASK(27, 24), 3);
			mt76_rmw_field(dev, MT_LED_GPIO_MUX1,
				       GENMASK(31, 28), 3);
			break;
		default:
			break;
		}
	} else if (dev->mphy.leds.pin) {
		switch (mt76_chip(&dev->mt76)) {
		case 0x7915:
			mt76_rmw_field(dev, MT_LED_GPIO_MUX3,
				       GENMASK(11, 8), 4);
			break;
		case 0x7986:
			mt76_rmw_field(dev, MT_LED_GPIO_MUX0,
				       GENMASK(11, 8), 1);
			break;
		case 0x7916:
			mt76_rmw_field(dev, MT_LED_GPIO_MUX1,
				       GENMASK(31, 28), 3);
			break;
		default:
			break;
		}
	} else {
		switch (mt76_chip(&dev->mt76)) {
		case 0x7915:
			mt76_rmw_field(dev, MT_LED_GPIO_MUX2,
				       GENMASK(11, 8), 4);
			break;
		case 0x7986:
			mt76_rmw_field(dev, MT_LED_GPIO_MUX0,
				       GENMASK(7, 4), 1);
			break;
		case 0x7916:
			mt76_rmw_field(dev, MT_LED_GPIO_MUX1,
				       GENMASK(27, 24), 3);
			break;
		default:
			break;
		}
	}
}

void mt7915_mac_init(struct mt7915_dev *dev)
{
	int i;
	u32 rx_len = is_mt7915(&dev->mt76) ? 0x400 : 0x680;

	/* config pse qid6 wfdma port selection */
	if (!is_mt7915(&dev->mt76) && dev->hif2)
		mt76_rmw(dev, MT_WF_PP_TOP_RXQ_WFDMA_CF_5, 0,
			 MT_WF_PP_TOP_RXQ_QID6_WFDMA_HIF_SEL_MASK);

	mt76_rmw_field(dev, MT_MDP_DCR1, MT_MDP_DCR1_MAX_RX_LEN, rx_len);

	if (!is_mt7915(&dev->mt76))
		mt76_clear(dev, MT_MDP_DCR2, MT_MDP_DCR2_RX_TRANS_SHORT);
	else
		mt76_clear(dev, MT_PLE_HOST_RPT0, MT_PLE_HOST_RPT0_TX_LATENCY);

	/* enable hardware de-agg */
	mt76_set(dev, MT_MDP_DCR0, MT_MDP_DCR0_DAMSDU_EN);

	for (i = 0; i < mt7915_wtbl_size(dev); i++)
		mt7915_mac_wtbl_update(dev, i,
				       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
	for (i = 0; i < 2; i++)
		mt7915_mac_init_band(dev, i);

	mt7915_init_led_mux(dev);
}

int mt7915_txbf_init(struct mt7915_dev *dev)
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

static struct mt7915_phy *
mt7915_alloc_ext_phy(struct mt7915_dev *dev)
{
	struct mt7915_phy *phy;
	struct mt76_phy *mphy;

	if (!dev->dbdc_support)
		return NULL;

	mphy = mt76_alloc_phy(&dev->mt76, sizeof(*phy), &mt7915_ops, MT_BAND1);
	if (!mphy)
		return ERR_PTR(-ENOMEM);

	phy = mphy->priv;
	phy->dev = dev;
	phy->mt76 = mphy;

	/* Bind main phy to band0 and ext_phy to band1 for dbdc case */
	phy->mt76->band_idx = 1;

	return phy;
}

static int
mt7915_register_ext_phy(struct mt7915_dev *dev, struct mt7915_phy *phy)
{
	struct mt76_phy *mphy = phy->mt76;
	int ret;

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
	mt7915_init_wiphy(phy);

	ret = mt76_register_phy(mphy, true, mt76_rates,
				ARRAY_SIZE(mt76_rates));
	if (ret)
		return ret;

	ret = mt7915_thermal_init(phy);
	if (ret)
		goto unreg;

	mt7915_init_debugfs(phy);

	return 0;

unreg:
	mt76_unregister_phy(mphy);
	return ret;
}

static void mt7915_init_work(struct work_struct *work)
{
	struct mt7915_dev *dev = container_of(work, struct mt7915_dev,
				 init_work);

	mt7915_mcu_set_eeprom(dev);
	mt7915_mac_init(dev);
	mt7915_txbf_init(dev);
}

void mt7915_wfsys_reset(struct mt7915_dev *dev)
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

		/* release wfsys then mcu re-executes romcode */
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
	} else if (is_mt798x(&dev->mt76)) {
		mt7986_wmac_disable(dev);
		msleep(20);

		mt7986_wmac_enable(dev);
		msleep(20);
	} else {
		mt76_set(dev, MT_WF_SUBSYS_RST, 0x1);
		msleep(20);

		mt76_clear(dev, MT_WF_SUBSYS_RST, 0x1);
		msleep(20);
	}
}

static bool mt7915_band_config(struct mt7915_dev *dev)
{
	bool ret = true;

	dev->phy.mt76->band_idx = 0;

	if (is_mt798x(&dev->mt76)) {
		u32 sku = mt7915_check_adie(dev, true);

		/*
		 * for mt7986, dbdc support is determined by the number
		 * of adie chips and the main phy is bound to band1 when
		 * dbdc is disabled.
		 */
		if (sku == MT7975_ONE_ADIE || sku == MT7976_ONE_ADIE) {
			dev->phy.mt76->band_idx = 1;
			ret = false;
		}
	} else {
		ret = is_mt7915(&dev->mt76) ?
		      !!(mt76_rr(dev, MT_HW_BOUND) & BIT(5)) : true;
	}

	return ret;
}

static int
mt7915_init_hardware(struct mt7915_dev *dev, struct mt7915_phy *phy2)
{
	int ret, idx;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);
	mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

	INIT_WORK(&dev->init_work, mt7915_init_work);

	ret = mt7915_dma_init(dev, phy2);
	if (ret)
		return ret;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	ret = mt7915_mcu_init(dev);
	if (ret)
		return ret;

	ret = mt7915_eeprom_init(dev);
	if (ret < 0)
		return ret;

	if (dev->cal) {
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
	int sts;
	u32 *cap;

	if (!phy->mt76->cap.has_5ghz)
		return;

	sts = hweight8(phy->mt76->chainmask);
	cap = &phy->mt76->sband_5g.sband.vht_cap.cap;

	*cap |= IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
		IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE |
		FIELD_PREP(IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK,
			   sts - 1);

	*cap &= ~(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK |
		  IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
		  IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE);

	if (sts < 2)
		return;

	*cap |= IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
		IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE |
		FIELD_PREP(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
			   sts - 1);
}

static void
mt7915_set_stream_he_txbf_caps(struct mt7915_phy *phy,
			       struct ieee80211_sta_he_cap *he_cap, int vif)
{
	struct mt7915_dev *dev = phy->dev;
	struct ieee80211_he_cap_elem *elem = &he_cap->he_cap_elem;
	int sts = hweight8(phy->mt76->chainmask);
	u8 c, sts_160 = sts;

	/* Can do 1/2 of STS in 160Mhz mode for mt7915 */
	if (is_mt7915(&dev->mt76)) {
		if (!dev->dbdc_support)
			sts_160 /= 2;
		else
			sts_160 = 0;
	}

#ifdef CONFIG_MAC80211_MESH
	if (vif == NL80211_IFTYPE_MESH_POINT)
		return;
#endif

	elem->phy_cap_info[3] &= ~IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER;
	elem->phy_cap_info[4] &= ~IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER;

	c = IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK;
	if (sts_160)
		c |= IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK;
	elem->phy_cap_info[5] &= ~c;

	c = IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
	    IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB;
	elem->phy_cap_info[6] &= ~c;

	elem->phy_cap_info[7] &= ~IEEE80211_HE_PHY_CAP7_MAX_NC_MASK;

	c = IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US;
	if (!is_mt7915(&dev->mt76))
		c |= IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO;
	elem->phy_cap_info[2] |= c;

	c = IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
	    IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
	if (sts_160)
		c |= IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4;
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

	if (vif != NL80211_IFTYPE_AP && vif != NL80211_IFTYPE_STATION)
		return;

	elem->phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER;

	c = FIELD_PREP(IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
		       sts - 1);
	if (sts_160)
		c |= FIELD_PREP(IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK,
				sts_160 - 1);
	elem->phy_cap_info[5] |= c;

	if (vif != NL80211_IFTYPE_AP)
		return;

	elem->phy_cap_info[4] |= IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER;

	c = IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
	    IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB;
	elem->phy_cap_info[6] |= c;

	if (!is_mt7915(&dev->mt76)) {
		c = IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ |
		    IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ;
		elem->phy_cap_info[7] |= c;
	}
}

static int
mt7915_init_he_caps(struct mt7915_phy *phy, enum nl80211_band band,
		    struct ieee80211_sband_iftype_data *data)
{
	struct mt7915_dev *dev = phy->dev;
	int i, idx = 0, nss = hweight8(phy->mt76->antenna_mask);
	u16 mcs_map = 0;
	u16 mcs_map_160 = 0;
	u8 nss_160;

	if (!is_mt7915(&dev->mt76))
		nss_160 = nss;
	else if (!dev->dbdc_support)
		/* Can do 1/2 of NSS streams in 160Mhz mode for mt7915 */
		nss_160 = nss / 2;
	else
		/* Can't do 160MHz with mt7915 dbdc */
		nss_160 = 0;

	for (i = 0; i < 8; i++) {
		if (i < nss)
			mcs_map |= (IEEE80211_HE_MCS_SUPPORT_0_11 << (i * 2));
		else
			mcs_map |= (IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));

		if (i < nss_160)
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
		else if (nss_160)
			he_cap_elem->phy_cap_info[0] =
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;
		else
			he_cap_elem->phy_cap_info[0] =
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;

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
				IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484;
			if (nss_160)
				he_cap_elem->phy_cap_info[8] |=
					IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU |
					IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU;
			he_cap_elem->phy_cap_info[9] |=
				IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM |
				IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK |
				IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU |
				IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU |
				IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
				IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB;
			break;
		}

		memset(he_mcs, 0, sizeof(*he_mcs));
		he_mcs->rx_mcs_80 = cpu_to_le16(mcs_map);
		he_mcs->tx_mcs_80 = cpu_to_le16(mcs_map);
		he_mcs->rx_mcs_160 = cpu_to_le16(mcs_map_160);
		he_mcs->tx_mcs_160 = cpu_to_le16(mcs_map_160);

		mt7915_set_stream_he_txbf_caps(phy, he_cap, i);

		memset(he_cap->ppe_thres, 0, sizeof(he_cap->ppe_thres));
		if (he_cap_elem->phy_cap_info[6] &
		    IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
			mt76_connac_gen_ppe_thresh(he_cap->ppe_thres, nss, band);
		} else {
			he_cap_elem->phy_cap_info[9] |=
				u8_encode_bits(IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US,
					       IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK);
		}

		if (band == NL80211_BAND_6GHZ) {
			u16 cap = IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS |
				  IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS;

			cap |= u16_encode_bits(IEEE80211_HT_MPDU_DENSITY_2,
					       IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START) |
			       u16_encode_bits(IEEE80211_VHT_MAX_AMPDU_1024K,
					       IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP) |
			       u16_encode_bits(IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454,
					       IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN);

			data[idx].he_6ghz_capa.capa = cpu_to_le16(cap);
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
		_ieee80211_set_sband_iftype_data(band, data, n);
	}

	if (phy->mt76->cap.has_5ghz) {
		data = phy->iftype[NL80211_BAND_5GHZ];
		n = mt7915_init_he_caps(phy, NL80211_BAND_5GHZ, data);

		band = &phy->mt76->sband_5g.sband;
		_ieee80211_set_sband_iftype_data(band, data, n);
	}

	if (phy->mt76->cap.has_6ghz) {
		data = phy->iftype[NL80211_BAND_6GHZ];
		n = mt7915_init_he_caps(phy, NL80211_BAND_6GHZ, data);

		band = &phy->mt76->sband_6g.sband;
		_ieee80211_set_sband_iftype_data(band, data, n);
	}
}

static void mt7915_unregister_ext_phy(struct mt7915_dev *dev)
{
	struct mt7915_phy *phy = mt7915_ext_phy(dev);
	struct mt76_phy *mphy = dev->mt76.phys[MT_BAND1];

	if (!phy)
		return;

	mt7915_unregister_thermal(phy);
	mt76_unregister_phy(mphy);
	ieee80211_free_hw(mphy->hw);
}

static void mt7915_stop_hardware(struct mt7915_dev *dev)
{
	mt7915_mcu_exit(dev);
	mt76_connac2_tx_token_put(&dev->mt76);
	mt7915_dma_cleanup(dev);
	tasklet_disable(&dev->mt76.irq_tasklet);

	if (is_mt798x(&dev->mt76))
		mt7986_wmac_disable(dev);
}

int mt7915_register_device(struct mt7915_dev *dev)
{
	struct mt7915_phy *phy2;
	int ret;

	dev->phy.dev = dev;
	dev->phy.mt76 = &dev->mt76.phy;
	dev->mt76.phy.priv = &dev->phy;
	INIT_WORK(&dev->rc_work, mt7915_mac_sta_rc_work);
	INIT_DELAYED_WORK(&dev->mphy.mac_work, mt7915_mac_work);
	INIT_LIST_HEAD(&dev->sta_rc_list);
	INIT_LIST_HEAD(&dev->twt_list);

	init_waitqueue_head(&dev->reset_wait);
	INIT_WORK(&dev->reset_work, mt7915_mac_reset_work);
	INIT_WORK(&dev->dump_work, mt7915_mac_dump_work);
	mutex_init(&dev->dump_mutex);

	dev->dbdc_support = mt7915_band_config(dev);

	phy2 = mt7915_alloc_ext_phy(dev);
	if (IS_ERR(phy2))
		return PTR_ERR(phy2);

	ret = mt7915_init_hardware(dev, phy2);
	if (ret)
		goto free_phy2;

	mt7915_init_wiphy(&dev->phy);

#ifdef CONFIG_NL80211_TESTMODE
	dev->mt76.test_ops = &mt7915_testmode_ops;
#endif

	ret = mt76_register_device(&dev->mt76, true, mt76_rates,
				   ARRAY_SIZE(mt76_rates));
	if (ret)
		goto stop_hw;

	ret = mt7915_thermal_init(&dev->phy);
	if (ret)
		goto unreg_dev;

	if (phy2) {
		ret = mt7915_register_ext_phy(dev, phy2);
		if (ret)
			goto unreg_thermal;
	}

	ieee80211_queue_work(mt76_hw(dev), &dev->init_work);

	dev->recovery.hw_init_done = true;

	ret = mt7915_init_debugfs(&dev->phy);
	if (ret)
		goto unreg_thermal;

	ret = mt7915_coredump_register(dev);
	if (ret)
		goto unreg_thermal;

	return 0;

unreg_thermal:
	mt7915_unregister_thermal(&dev->phy);
unreg_dev:
	mt76_unregister_device(&dev->mt76);
stop_hw:
	mt7915_stop_hardware(dev);
free_phy2:
	if (phy2)
		ieee80211_free_hw(phy2->mt76->hw);
	return ret;
}

void mt7915_unregister_device(struct mt7915_dev *dev)
{
	mt7915_unregister_ext_phy(dev);
	mt7915_coredump_unregister(dev);
	mt7915_unregister_thermal(&dev->phy);
	mt76_unregister_device(&dev->mt76);
	mt7915_stop_hardware(dev);

	mt76_free_device(&dev->mt76);
}
