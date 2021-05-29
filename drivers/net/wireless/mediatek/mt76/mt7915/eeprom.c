// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7915.h"
#include "eeprom.h"

static int mt7915_eeprom_load_precal(struct mt7915_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	u8 *eeprom = mdev->eeprom.data;
	u32 val = eeprom[MT_EE_DO_PRE_CAL];

	if (val != (MT_EE_WIFI_CAL_DPD | MT_EE_WIFI_CAL_GROUP))
		return 0;

	val = MT_EE_CAL_GROUP_SIZE + MT_EE_CAL_DPD_SIZE;
	dev->cal = devm_kzalloc(mdev->dev, val, GFP_KERNEL);
	if (!dev->cal)
		return -ENOMEM;

	return mt76_get_of_eeprom(mdev, dev->cal, MT_EE_PRECAL, val);
}

static int mt7915_eeprom_load(struct mt7915_dev *dev)
{
	int ret;

	ret = mt76_eeprom_init(&dev->mt76, MT7915_EEPROM_SIZE);
	if (ret < 0)
		return ret;

	if (ret) {
		dev->flash_mode = true;
		ret = mt7915_eeprom_load_precal(dev);
	} else {
		u32 block_num, i;

		block_num = DIV_ROUND_UP(MT7915_EEPROM_SIZE,
					 MT7915_EEPROM_BLOCK_SIZE);
		for (i = 0; i < block_num; i++)
			mt7915_mcu_get_eeprom(dev,
					      i * MT7915_EEPROM_BLOCK_SIZE);
	}

	return ret;
}

static int mt7915_check_eeprom(struct mt7915_dev *dev)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	u16 val = get_unaligned_le16(eeprom);

	switch (val) {
	case 0x7915:
		return 0;
	default:
		return -EINVAL;
	}
}

void mt7915_eeprom_parse_band_config(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	u8 *eeprom = dev->mt76.eeprom.data;
	u32 val;

	val = eeprom[MT_EE_WIFI_CONF + ext_phy];
	val = FIELD_GET(MT_EE_WIFI_CONF0_BAND_SEL, val);
	if (val == MT_EE_BAND_SEL_DEFAULT && dev->dbdc_support)
		val = ext_phy ? MT_EE_BAND_SEL_5GHZ : MT_EE_BAND_SEL_2GHZ;

	switch (val) {
	case MT_EE_BAND_SEL_5GHZ:
		phy->mt76->cap.has_5ghz = true;
		break;
	case MT_EE_BAND_SEL_2GHZ:
		phy->mt76->cap.has_2ghz = true;
		break;
	default:
		phy->mt76->cap.has_2ghz = true;
		phy->mt76->cap.has_5ghz = true;
		break;
	}
}

static void mt7915_eeprom_parse_hw_cap(struct mt7915_dev *dev)
{
	u8 nss, nss_band, *eeprom = dev->mt76.eeprom.data;

	mt7915_eeprom_parse_band_config(&dev->phy);

	/* read tx mask from eeprom */
	nss = FIELD_GET(MT_EE_WIFI_CONF0_TX_PATH, eeprom[MT_EE_WIFI_CONF]);
	if (!nss || nss > 4)
		nss = 4;

	nss_band = nss;

	if (dev->dbdc_support) {
		nss_band = FIELD_GET(MT_EE_WIFI_CONF3_TX_PATH_B0,
				     eeprom[MT_EE_WIFI_CONF + 3]);
		if (!nss_band || nss_band > 2)
			nss_band = 2;

		if (nss_band >= nss)
			nss = 4;
	}

	dev->chainmask = BIT(nss) - 1;
	dev->mphy.antenna_mask = BIT(nss_band) - 1;
	dev->mphy.chainmask = dev->mphy.antenna_mask;
}

int mt7915_eeprom_init(struct mt7915_dev *dev)
{
	int ret;

	ret = mt7915_eeprom_load(dev);
	if (ret < 0)
		return ret;

	ret = mt7915_check_eeprom(dev);
	if (ret)
		return ret;

	mt7915_eeprom_parse_hw_cap(dev);
	memcpy(dev->mphy.macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR,
	       ETH_ALEN);

	mt76_eeprom_override(&dev->mphy);

	return 0;
}

int mt7915_eeprom_get_target_power(struct mt7915_dev *dev,
				   struct ieee80211_channel *chan,
				   u8 chain_idx)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	int index, target_power;
	bool tssi_on;

	if (chain_idx > 3)
		return -EINVAL;

	tssi_on = mt7915_tssi_enabled(dev, chan->band);

	if (chan->band == NL80211_BAND_2GHZ) {
		index = MT_EE_TX0_POWER_2G + chain_idx * 3;
		target_power = eeprom[index];

		if (!tssi_on)
			target_power += eeprom[index + 1];
	} else {
		int group = mt7915_get_channel_group(chan->hw_value);

		index = MT_EE_TX0_POWER_5G + chain_idx * 12;
		target_power = eeprom[index + group];

		if (!tssi_on)
			target_power += eeprom[index + 8];
	}

	return target_power;
}

s8 mt7915_eeprom_get_power_delta(struct mt7915_dev *dev, int band)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	u32 val;
	s8 delta;

	if (band == NL80211_BAND_2GHZ)
		val = eeprom[MT_EE_RATE_DELTA_2G];
	else
		val = eeprom[MT_EE_RATE_DELTA_5G];

	if (!(val & MT_EE_RATE_DELTA_EN))
		return 0;

	delta = FIELD_GET(MT_EE_RATE_DELTA_MASK, val);

	return val & MT_EE_RATE_DELTA_SIGN ? delta : -delta;
}

const u8 mt7915_sku_group_len[] = {
	[SKU_CCK] = 4,
	[SKU_OFDM] = 8,
	[SKU_HT_BW20] = 8,
	[SKU_HT_BW40] = 9,
	[SKU_VHT_BW20] = 12,
	[SKU_VHT_BW40] = 12,
	[SKU_VHT_BW80] = 12,
	[SKU_VHT_BW160] = 12,
	[SKU_HE_RU26] = 12,
	[SKU_HE_RU52] = 12,
	[SKU_HE_RU106] = 12,
	[SKU_HE_RU242] = 12,
	[SKU_HE_RU484] = 12,
	[SKU_HE_RU996] = 12,
	[SKU_HE_RU2x996] = 12
};
