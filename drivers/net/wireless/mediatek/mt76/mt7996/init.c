// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/etherdevice.h>
#include <linux/of.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
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

static ssize_t mt7996_thermal_temp_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct mt7996_phy *phy = dev_get_drvdata(dev);
	int i = to_sensor_dev_attr(attr)->index;
	int temperature;

	switch (i) {
	case 0:
		temperature = mt7996_mcu_get_temperature(phy);
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

static ssize_t mt7996_thermal_temp_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct mt7996_phy *phy = dev_get_drvdata(dev);
	int ret, i = to_sensor_dev_attr(attr)->index;
	long val;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	mutex_lock(&phy->dev->mt76.mutex);
	val = clamp_val(DIV_ROUND_CLOSEST(val, 1000), 40, 130);

	/* add a safety margin ~10 */
	if ((i - 1 == MT7996_CRIT_TEMP_IDX &&
	     val > phy->throttle_temp[MT7996_MAX_TEMP_IDX] - 10) ||
	    (i - 1 == MT7996_MAX_TEMP_IDX &&
	     val - 10 < phy->throttle_temp[MT7996_CRIT_TEMP_IDX])) {
		dev_err(phy->dev->mt76.dev,
			"temp1_max shall be 10 degrees higher than temp1_crit.");
		mutex_unlock(&phy->dev->mt76.mutex);
		return -EINVAL;
	}

	phy->throttle_temp[i - 1] = val;
	mutex_unlock(&phy->dev->mt76.mutex);

	ret = mt7996_mcu_set_thermal_protect(phy, true);
	if (ret)
		return ret;

	return count;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, mt7996_thermal_temp, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_crit, mt7996_thermal_temp, 1);
static SENSOR_DEVICE_ATTR_RW(temp1_max, mt7996_thermal_temp, 2);
static SENSOR_DEVICE_ATTR_RO(throttle1, mt7996_thermal_temp, 3);

static struct attribute *mt7996_hwmon_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_throttle1.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mt7996_hwmon);

static int
mt7996_thermal_get_max_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long *state)
{
	*state = MT7996_CDEV_THROTTLE_MAX;

	return 0;
}

static int
mt7996_thermal_get_cur_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long *state)
{
	struct mt7996_phy *phy = cdev->devdata;

	*state = phy->cdev_state;

	return 0;
}

static int
mt7996_thermal_set_cur_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long state)
{
	struct mt7996_phy *phy = cdev->devdata;
	u8 throttling = MT7996_THERMAL_THROTTLE_MAX - state;
	int ret;

	if (state > MT7996_CDEV_THROTTLE_MAX) {
		dev_err(phy->dev->mt76.dev,
			"please specify a valid throttling state\n");
		return -EINVAL;
	}

	if (state == phy->cdev_state)
		return 0;

	/* cooling_device convention: 0 = no cooling, more = more cooling
	 * mcu convention: 1 = max cooling, more = less cooling
	 */
	ret = mt7996_mcu_set_thermal_throttling(phy, throttling);
	if (ret)
		return ret;

	phy->cdev_state = state;

	return 0;
}

static const struct thermal_cooling_device_ops mt7996_thermal_ops = {
	.get_max_state = mt7996_thermal_get_max_throttle_state,
	.get_cur_state = mt7996_thermal_get_cur_throttle_state,
	.set_cur_state = mt7996_thermal_set_cur_throttle_state,
};

static void mt7996_unregister_thermal(struct mt7996_phy *phy)
{
	struct wiphy *wiphy = phy->mt76->hw->wiphy;

	if (!phy->cdev)
		return;

	sysfs_remove_link(&wiphy->dev.kobj, "cooling_device");
	thermal_cooling_device_unregister(phy->cdev);
}

static int mt7996_thermal_init(struct mt7996_phy *phy)
{
	struct wiphy *wiphy = phy->mt76->hw->wiphy;
	struct thermal_cooling_device *cdev;
	struct device *hwmon;
	const char *name;

	name = devm_kasprintf(&wiphy->dev, GFP_KERNEL, "mt7996_%s",
			      wiphy_name(wiphy));

	cdev = thermal_cooling_device_register(name, phy, &mt7996_thermal_ops);
	if (!IS_ERR(cdev)) {
		if (sysfs_create_link(&wiphy->dev.kobj, &cdev->device.kobj,
				      "cooling_device") < 0)
			thermal_cooling_device_unregister(cdev);
		else
			phy->cdev = cdev;
	}

	/* initialize critical/maximum high temperature */
	phy->throttle_temp[MT7996_CRIT_TEMP_IDX] = MT7996_CRIT_TEMP;
	phy->throttle_temp[MT7996_MAX_TEMP_IDX] = MT7996_MAX_TEMP;

	if (!IS_REACHABLE(CONFIG_HWMON))
		return 0;

	hwmon = devm_hwmon_device_register_with_groups(&wiphy->dev, name, phy,
						       mt7996_hwmon_groups);

	if (IS_ERR(hwmon))
		return PTR_ERR(hwmon);

	return 0;
}

static void mt7996_led_set_config(struct led_classdev *led_cdev,
				  u8 delay_on, u8 delay_off)
{
	struct mt7996_dev *dev;
	struct mt76_phy *mphy;
	u32 val;

	mphy = container_of(led_cdev, struct mt76_phy, leds.cdev);
	dev = container_of(mphy->dev, struct mt7996_dev, mt76);

	/* select TX blink mode, 2: only data frames */
	mt76_rmw_field(dev, MT_TMAC_TCR0(mphy->band_idx), MT_TMAC_TCR0_TX_BLINK, 2);

	/* enable LED */
	mt76_wr(dev, MT_LED_EN(mphy->band_idx), 1);

	/* set LED Tx blink on/off time */
	val = FIELD_PREP(MT_LED_TX_BLINK_ON_MASK, delay_on) |
	      FIELD_PREP(MT_LED_TX_BLINK_OFF_MASK, delay_off);
	mt76_wr(dev, MT_LED_TX_BLINK(mphy->band_idx), val);

	/* turn LED off */
	if (delay_off == 0xff && delay_on == 0x0) {
		val = MT_LED_CTRL_POLARITY | MT_LED_CTRL_KICK;
	} else {
		/* control LED */
		val = MT_LED_CTRL_BLINK_MODE | MT_LED_CTRL_KICK;
		if (mphy->band_idx == MT_BAND1)
			val |= MT_LED_CTRL_BLINK_BAND_SEL;
	}

	if (mphy->leds.al)
		val |= MT_LED_CTRL_POLARITY;

	mt76_wr(dev, MT_LED_CTRL(mphy->band_idx), val);
	mt76_clear(dev, MT_LED_CTRL(mphy->band_idx), MT_LED_CTRL_KICK);
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

static void __mt7996_init_txpower(struct mt7996_phy *phy,
				  struct ieee80211_supported_band *sband)
{
	struct mt7996_dev *dev = phy->dev;
	int i, nss = hweight16(phy->mt76->chainmask);
	int nss_delta = mt76_tx_power_nss_delta(nss);
	int pwr_delta = mt7996_eeprom_get_power_delta(dev, sband->band);
	struct mt76_power_limits limits;

	for (i = 0; i < sband->n_channels; i++) {
		struct ieee80211_channel *chan = &sband->channels[i];
		int target_power = mt7996_eeprom_get_target_power(dev, chan);

		target_power += pwr_delta;
		target_power = mt76_get_rate_power_limits(phy->mt76, chan,
							  &limits,
							  target_power);
		target_power += nss_delta;
		target_power = DIV_ROUND_UP(target_power, 2);
		chan->max_power = min_t(int, chan->max_reg_power,
					target_power);
		chan->orig_mpwr = target_power;
	}
}

void mt7996_init_txpower(struct mt7996_phy *phy)
{
	if (!phy)
		return;

	if (phy->mt76->cap.has_2ghz)
		__mt7996_init_txpower(phy, &phy->mt76->sband_2g.sband);
	if (phy->mt76->cap.has_5ghz)
		__mt7996_init_txpower(phy, &phy->mt76->sband_5g.sband);
	if (phy->mt76->cap.has_6ghz)
		__mt7996_init_txpower(phy, &phy->mt76->sband_6g.sband);
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

	mt7996_init_txpower(phy);

	phy->mt76->dfs_state = MT_DFS_STATE_UNKNOWN;
	mt7996_dfs_init_radar_detector(phy);
}

static void
mt7996_init_wiphy(struct ieee80211_hw *hw, struct mtk_wed_device *wed)
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
	if (mtk_wed_device_active(wed))
		hw->netdev_features |= NETIF_F_HW_TC;

	hw->radiotap_timestamp.units_pos =
		IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US;

	phy->slottime = 9;
	phy->beacon_rate = -1;

	hw->sta_data_size = sizeof(struct mt7996_sta);
	hw->vif_data_size = sizeof(struct mt7996_vif);

	wiphy->iface_combinations = if_comb;
	wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);
	wiphy->reg_notifier = mt7996_regd_notifier;
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
	ieee80211_hw_set(hw, SUPPORTS_MULTI_BSSID);

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

	/* init led callbacks */
	if (IS_ENABLED(CONFIG_MT76_LEDS)) {
		phy->mt76->leds.cdev.brightness_set = mt7996_led_set_brightness;
		phy->mt76->leds.cdev.blink_set = mt7996_led_set_blink;
	}

	mt76_set_stream_caps(phy->mt76, true);
	mt7996_set_stream_vht_txbf_caps(phy);
	mt7996_set_stream_he_eht_caps(phy);
	mt7996_init_txpower(phy);

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

	/* MT_TXD5_TX_STATUS_HOST (MPDU format) has higher priority than
	 * MT_AGG_ACR_PPDU_TXS2H (PPDU format) even though ACR bit is set.
	 */
	mt76_set(dev, MT_AGG_ACR4(band), MT_AGG_ACR_PPDU_TXS2H);
}

static void mt7996_mac_init_basic_rates(struct mt7996_dev *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt76_rates); i++) {
		u16 rate = mt76_rates[i].hw_value;
		/* odd index for driver, even index for firmware */
		u16 idx = MT7996_BASIC_RATES_TBL + 2 * i;

		rate = FIELD_PREP(MT_TX_RATE_MODE, rate >> 8) |
		       FIELD_PREP(MT_TX_RATE_IDX, rate & GENMASK(7, 0));
		mt7996_mcu_set_fixed_rate_table(&dev->phy, idx, rate, false);
	}
}

void mt7996_mac_init(struct mt7996_dev *dev)
{
#define HIF_TXD_V2_1	0x21
	int i;

	mt76_clear(dev, MT_MDP_DCR2, MT_MDP_DCR2_RX_TRANS_SHORT);

	for (i = 0; i < mt7996_wtbl_size(dev); i++)
		mt7996_mac_wtbl_update(dev, i,
				       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	if (IS_ENABLED(CONFIG_MT76_LEDS)) {
		i = dev->mphy.leds.pin ? MT_LED_GPIO_MUX3 : MT_LED_GPIO_MUX2;
		mt76_rmw_field(dev, i, MT_LED_GPIO_SEL_MASK, 4);
	}

	/* rro module init */
	if (is_mt7996(&dev->mt76))
		mt7996_mcu_set_rro(dev, UNI_RRO_SET_PLATFORM_TYPE, 2);
	else
		mt7996_mcu_set_rro(dev, UNI_RRO_SET_PLATFORM_TYPE,
				   dev->hif2 ? 7 : 0);

	if (dev->has_rro) {
		u16 timeout;

		timeout = mt76_rr(dev, MT_HW_REV) == MT_HW_REV1 ? 512 : 128;
		mt7996_mcu_set_rro(dev, UNI_RRO_SET_FLUSH_TIMEOUT, timeout);
		mt7996_mcu_set_rro(dev, UNI_RRO_SET_BYPASS_MODE, 1);
		mt7996_mcu_set_rro(dev, UNI_RRO_SET_TXFREE_PATH, 0);
	} else {
		mt7996_mcu_set_rro(dev, UNI_RRO_SET_BYPASS_MODE, 3);
		mt7996_mcu_set_rro(dev, UNI_RRO_SET_TXFREE_PATH, 1);
	}

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

	if (mt7996_band_valid(dev, MT_BAND1) ||
	    mt7996_band_valid(dev, MT_BAND2)) {
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
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;

	if (!mt7996_band_valid(dev, band) || band == MT_BAND0)
		return 0;

	if (phy)
		return 0;

	if (is_mt7996(&dev->mt76) && band == MT_BAND2 && dev->hif2) {
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);
		wed = &dev->mt76.mmio.wed_hif2;
	}

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
	mt7996_init_wiphy(mphy->hw, wed);
	ret = mt7996_init_tx_queues(mphy->priv,
				    MT_TXQ_ID(band),
				    MT7996_TX_RING_SIZE,
				    MT_TXQ_RING_BASE(band) + hif1_ofs,
				    wed);
	if (ret)
		goto error;

	ret = mt76_register_phy(mphy, true, mt76_rates,
				ARRAY_SIZE(mt76_rates));
	if (ret)
		goto error;

	ret = mt7996_thermal_init(phy);
	if (ret)
		goto error;

	ret = mt7996_init_debugfs(phy);
	if (ret)
		goto error;

	if (wed == &dev->mt76.mmio.wed_hif2 && mtk_wed_device_active(wed)) {
		u32 irq_mask = dev->mt76.mmio.irqmask | MT_INT_TX_DONE_BAND2;

		mt76_wr(dev, MT_INT1_MASK_CSR, irq_mask);
		mtk_wed_device_start(&dev->mt76.mmio.wed_hif2, irq_mask);
	}

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

	mt7996_unregister_thermal(phy);

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
	mt7996_txbf_init(dev);
}

void mt7996_wfsys_reset(struct mt7996_dev *dev)
{
	mt76_set(dev, MT_WF_SUBSYS_RST, 0x1);
	msleep(20);

	mt76_clear(dev, MT_WF_SUBSYS_RST, 0x1);
	msleep(20);
}

static int mt7996_wed_rro_init(struct mt7996_dev *dev)
{
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	u32 reg = MT_RRO_ADDR_ELEM_SEG_ADDR0;
	struct mt7996_wed_rro_addr *addr;
	void *ptr;
	int i;

	if (!dev->has_rro)
		return 0;

	if (!mtk_wed_device_active(wed))
		return 0;

	for (i = 0; i < ARRAY_SIZE(dev->wed_rro.ba_bitmap); i++) {
		ptr = dmam_alloc_coherent(dev->mt76.dma_dev,
					  MT7996_RRO_BA_BITMAP_CR_SIZE,
					  &dev->wed_rro.ba_bitmap[i].phy_addr,
					  GFP_KERNEL);
		if (!ptr)
			return -ENOMEM;

		dev->wed_rro.ba_bitmap[i].ptr = ptr;
	}

	for (i = 0; i < ARRAY_SIZE(dev->wed_rro.addr_elem); i++) {
		int j;

		ptr = dmam_alloc_coherent(dev->mt76.dma_dev,
				MT7996_RRO_WINDOW_MAX_SIZE * sizeof(*addr),
				&dev->wed_rro.addr_elem[i].phy_addr,
				GFP_KERNEL);
		if (!ptr)
			return -ENOMEM;

		dev->wed_rro.addr_elem[i].ptr = ptr;
		memset(dev->wed_rro.addr_elem[i].ptr, 0,
		       MT7996_RRO_WINDOW_MAX_SIZE * sizeof(*addr));

		addr = dev->wed_rro.addr_elem[i].ptr;
		for (j = 0; j < MT7996_RRO_WINDOW_MAX_SIZE; j++) {
			addr->signature = 0xff;
			addr++;
		}

		wed->wlan.ind_cmd.addr_elem_phys[i] =
			dev->wed_rro.addr_elem[i].phy_addr;
	}

	ptr = dmam_alloc_coherent(dev->mt76.dma_dev,
				  MT7996_RRO_WINDOW_MAX_LEN * sizeof(*addr),
				  &dev->wed_rro.session.phy_addr,
				  GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	dev->wed_rro.session.ptr = ptr;
	addr = dev->wed_rro.session.ptr;
	for (i = 0; i < MT7996_RRO_WINDOW_MAX_LEN; i++) {
		addr->signature = 0xff;
		addr++;
	}

	/* rro hw init */
	/* TODO: remove line after WM has set */
	mt76_clear(dev, WF_RRO_AXI_MST_CFG, WF_RRO_AXI_MST_CFG_DIDX_OK);

	/* setup BA bitmap cache address */
	mt76_wr(dev, MT_RRO_BA_BITMAP_BASE0,
		dev->wed_rro.ba_bitmap[0].phy_addr);
	mt76_wr(dev, MT_RRO_BA_BITMAP_BASE1, 0);
	mt76_wr(dev, MT_RRO_BA_BITMAP_BASE_EXT0,
		dev->wed_rro.ba_bitmap[1].phy_addr);
	mt76_wr(dev, MT_RRO_BA_BITMAP_BASE_EXT1, 0);

	/* setup Address element address */
	for (i = 0; i < ARRAY_SIZE(dev->wed_rro.addr_elem); i++) {
		mt76_wr(dev, reg, dev->wed_rro.addr_elem[i].phy_addr >> 4);
		reg += 4;
	}

	/* setup Address element address - separate address segment mode */
	mt76_wr(dev, MT_RRO_ADDR_ARRAY_BASE1,
		MT_RRO_ADDR_ARRAY_ELEM_ADDR_SEG_MODE);

	wed->wlan.ind_cmd.win_size = ffs(MT7996_RRO_WINDOW_MAX_LEN) - 6;
	wed->wlan.ind_cmd.particular_sid = MT7996_RRO_MAX_SESSION;
	wed->wlan.ind_cmd.particular_se_phys = dev->wed_rro.session.phy_addr;
	wed->wlan.ind_cmd.se_group_nums = MT7996_RRO_ADDR_ELEM_LEN;
	wed->wlan.ind_cmd.ack_sn_addr = MT_RRO_ACK_SN_CTRL;

	mt76_wr(dev, MT_RRO_IND_CMD_SIGNATURE_BASE0, 0x15010e00);
	mt76_set(dev, MT_RRO_IND_CMD_SIGNATURE_BASE1,
		 MT_RRO_IND_CMD_SIGNATURE_BASE1_EN);

	/* particular session configure */
	/* use max session idx + 1 as particular session id */
	mt76_wr(dev, MT_RRO_PARTICULAR_CFG0, dev->wed_rro.session.phy_addr);
	mt76_wr(dev, MT_RRO_PARTICULAR_CFG1,
		MT_RRO_PARTICULAR_CONFG_EN |
		FIELD_PREP(MT_RRO_PARTICULAR_SID, MT7996_RRO_MAX_SESSION));

	/* interrupt enable */
	mt76_wr(dev, MT_RRO_HOST_INT_ENA,
		MT_RRO_HOST_INT_ENA_HOST_RRO_DONE_ENA);

	/* rro ind cmd queue init */
	return mt7996_dma_rro_init(dev);
#else
	return 0;
#endif
}

static void mt7996_wed_rro_free(struct mt7996_dev *dev)
{
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	int i;

	if (!dev->has_rro)
		return;

	if (!mtk_wed_device_active(&dev->mt76.mmio.wed))
		return;

	for (i = 0; i < ARRAY_SIZE(dev->wed_rro.ba_bitmap); i++) {
		if (!dev->wed_rro.ba_bitmap[i].ptr)
			continue;

		dmam_free_coherent(dev->mt76.dma_dev,
				   MT7996_RRO_BA_BITMAP_CR_SIZE,
				   dev->wed_rro.ba_bitmap[i].ptr,
				   dev->wed_rro.ba_bitmap[i].phy_addr);
	}

	for (i = 0; i < ARRAY_SIZE(dev->wed_rro.addr_elem); i++) {
		if (!dev->wed_rro.addr_elem[i].ptr)
			continue;

		dmam_free_coherent(dev->mt76.dma_dev,
				   MT7996_RRO_WINDOW_MAX_SIZE *
				   sizeof(struct mt7996_wed_rro_addr),
				   dev->wed_rro.addr_elem[i].ptr,
				   dev->wed_rro.addr_elem[i].phy_addr);
	}

	if (!dev->wed_rro.session.ptr)
		return;

	dmam_free_coherent(dev->mt76.dma_dev,
			   MT7996_RRO_WINDOW_MAX_LEN *
			   sizeof(struct mt7996_wed_rro_addr),
			   dev->wed_rro.session.ptr,
			   dev->wed_rro.session.phy_addr);
#endif
}

static void mt7996_wed_rro_work(struct work_struct *work)
{
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	struct mt7996_dev *dev;
	LIST_HEAD(list);

	dev = (struct mt7996_dev *)container_of(work, struct mt7996_dev,
						wed_rro.work);

	spin_lock_bh(&dev->wed_rro.lock);
	list_splice_init(&dev->wed_rro.poll_list, &list);
	spin_unlock_bh(&dev->wed_rro.lock);

	while (!list_empty(&list)) {
		struct mt7996_wed_rro_session_id *e;
		int i;

		e = list_first_entry(&list, struct mt7996_wed_rro_session_id,
				     list);
		list_del_init(&e->list);

		for (i = 0; i < MT7996_RRO_WINDOW_MAX_LEN; i++) {
			void *ptr = dev->wed_rro.session.ptr;
			struct mt7996_wed_rro_addr *elem;
			u32 idx, elem_id = i;

			if (e->id == MT7996_RRO_MAX_SESSION)
				goto reset;

			idx = e->id / MT7996_RRO_BA_BITMAP_SESSION_SIZE;
			if (idx >= ARRAY_SIZE(dev->wed_rro.addr_elem))
				goto out;

			ptr = dev->wed_rro.addr_elem[idx].ptr;
			elem_id +=
				(e->id % MT7996_RRO_BA_BITMAP_SESSION_SIZE) *
				MT7996_RRO_WINDOW_MAX_LEN;
reset:
			elem = ptr + elem_id * sizeof(*elem);
			elem->signature = 0xff;
		}
		mt7996_mcu_wed_rro_reset_sessions(dev, e->id);
out:
		kfree(e);
	}
#endif
}

static int mt7996_init_hardware(struct mt7996_dev *dev)
{
	int ret, idx;

	mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);
	if (is_mt7992(&dev->mt76)) {
		mt76_rmw(dev, MT_AFE_CTL_BAND_PLL_03(MT_BAND0), MT_AFE_CTL_BAND_PLL_03_MSB_EN, 0);
		mt76_rmw(dev, MT_AFE_CTL_BAND_PLL_03(MT_BAND1), MT_AFE_CTL_BAND_PLL_03_MSB_EN, 0);
	}

	INIT_WORK(&dev->init_work, mt7996_init_work);
	INIT_WORK(&dev->wed_rro.work, mt7996_wed_rro_work);
	INIT_LIST_HEAD(&dev->wed_rro.poll_list);
	spin_lock_init(&dev->wed_rro.lock);

	ret = mt7996_dma_init(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	ret = mt7996_mcu_init(dev);
	if (ret)
		return ret;

	ret = mt7996_wed_rro_init(dev);
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

	if (!(vif == NL80211_IFTYPE_AP || vif == NL80211_IFTYPE_STATION))
		return;

	elem->phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER;
	if (vif == NL80211_IFTYPE_AP)
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

	val = max_t(u8, sts - 1, 3);
	eht_cap_elem->phy_cap_info[0] |=
		u8_encode_bits(u8_get_bits(val, BIT(0)),
			       IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK);

	eht_cap_elem->phy_cap_info[1] =
		u8_encode_bits(u8_get_bits(val, GENMASK(2, 1)),
			       IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK) |
		u8_encode_bits(val,
			       IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK) |
		u8_encode_bits(val,
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

	_ieee80211_set_sband_iftype_data(sband, data, n);
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

	mt7996_init_wiphy(hw, &dev->mt76.mmio.wed);

	ret = mt76_register_device(&dev->mt76, true, mt76_rates,
				   ARRAY_SIZE(mt76_rates));
	if (ret)
		return ret;

	ret = mt7996_thermal_init(&dev->phy);
	if (ret)
		return ret;

	ret = mt7996_register_phy(dev, mt7996_phy2(dev), MT_BAND1);
	if (ret)
		return ret;

	ret = mt7996_register_phy(dev, mt7996_phy3(dev), MT_BAND2);
	if (ret)
		return ret;

	ieee80211_queue_work(mt76_hw(dev), &dev->init_work);

	dev->recovery.hw_init_done = true;

	ret = mt7996_init_debugfs(&dev->phy);
	if (ret)
		goto error;

	ret = mt7996_coredump_register(dev);
	if (ret)
		goto error;

	return 0;

error:
	cancel_work_sync(&dev->init_work);

	return ret;
}

void mt7996_unregister_device(struct mt7996_dev *dev)
{
	cancel_work_sync(&dev->wed_rro.work);
	mt7996_unregister_phy(mt7996_phy3(dev), MT_BAND2);
	mt7996_unregister_phy(mt7996_phy2(dev), MT_BAND1);
	mt7996_unregister_thermal(&dev->phy);
	mt7996_coredump_unregister(dev);
	mt76_unregister_device(&dev->mt76);
	mt7996_wed_rro_free(dev);
	mt7996_mcu_exit(dev);
	mt7996_tx_token_put(dev);
	mt7996_dma_cleanup(dev);
	tasklet_disable(&dev->mt76.irq_tasklet);

	mt76_free_device(&dev->mt76);
}
