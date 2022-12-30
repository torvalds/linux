// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/firmware.h>
#include "mt7996.h"
#include "eeprom.h"

static int mt7996_check_eeprom(struct mt7996_dev *dev)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	u16 val = get_unaligned_le16(eeprom);

	switch (val) {
	case 0x7990:
		return 0;
	default:
		return -EINVAL;
	}
}

static char *mt7996_eeprom_name(struct mt7996_dev *dev)
{
	/* reserve for future variants */
	return MT7996_EEPROM_DEFAULT;
}

static int
mt7996_eeprom_load_default(struct mt7996_dev *dev)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	const struct firmware *fw = NULL;
	int ret;

	ret = request_firmware(&fw, mt7996_eeprom_name(dev), dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data) {
		dev_err(dev->mt76.dev, "Invalid default bin\n");
		ret = -EINVAL;
		goto out;
	}

	memcpy(eeprom, fw->data, MT7996_EEPROM_SIZE);
	dev->flash_mode = true;

out:
	release_firmware(fw);

	return ret;
}

static int mt7996_eeprom_load(struct mt7996_dev *dev)
{
	int ret;

	ret = mt76_eeprom_init(&dev->mt76, MT7996_EEPROM_SIZE);
	if (ret < 0)
		return ret;

	if (ret) {
		dev->flash_mode = true;
	} else {
		u8 free_block_num;
		u32 block_num, i;

		/* TODO: check free block event */
		mt7996_mcu_get_eeprom_free_block(dev, &free_block_num);
		/* efuse info not enough */
		if (free_block_num >= 59)
			return -EINVAL;

		/* read eeprom data from efuse */
		block_num = DIV_ROUND_UP(MT7996_EEPROM_SIZE, MT7996_EEPROM_BLOCK_SIZE);
		for (i = 0; i < block_num; i++)
			mt7996_mcu_get_eeprom(dev, i * MT7996_EEPROM_BLOCK_SIZE);
	}

	return mt7996_check_eeprom(dev);
}

static int mt7996_eeprom_parse_band_config(struct mt7996_phy *phy)
{
	u8 *eeprom = phy->dev->mt76.eeprom.data;
	u32 val = eeprom[MT_EE_WIFI_CONF];
	int ret = 0;

	switch (phy->mt76->band_idx) {
	case MT_BAND1:
		val = FIELD_GET(MT_EE_WIFI_CONF1_BAND_SEL, val);
		break;
	case MT_BAND2:
		val = eeprom[MT_EE_WIFI_CONF + 1];
		val = FIELD_GET(MT_EE_WIFI_CONF2_BAND_SEL, val);
		break;
	default:
		val = FIELD_GET(MT_EE_WIFI_CONF0_BAND_SEL, val);
		break;
	}

	switch (val) {
	case MT_EE_BAND_SEL_2GHZ:
		phy->mt76->cap.has_2ghz = true;
		break;
	case MT_EE_BAND_SEL_5GHZ:
		phy->mt76->cap.has_5ghz = true;
		break;
	case MT_EE_BAND_SEL_6GHZ:
		phy->mt76->cap.has_6ghz = true;
		break;
	case MT_EE_BAND_SEL_5GHZ_6GHZ:
		phy->mt76->cap.has_5ghz = true;
		phy->mt76->cap.has_6ghz = true;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int mt7996_eeprom_parse_hw_cap(struct mt7996_dev *dev, struct mt7996_phy *phy)
{
	u8 path, nss, band_idx = phy->mt76->band_idx;
	u8 *eeprom = dev->mt76.eeprom.data;
	struct mt76_phy *mphy = phy->mt76;

	switch (band_idx) {
	case MT_BAND1:
		path = FIELD_GET(MT_EE_WIFI_CONF2_TX_PATH_BAND1,
				 eeprom[MT_EE_WIFI_CONF + 2]);
		nss = FIELD_GET(MT_EE_WIFI_CONF5_STREAM_NUM_BAND1,
				eeprom[MT_EE_WIFI_CONF + 5]);
		break;
	case MT_BAND2:
		path = FIELD_GET(MT_EE_WIFI_CONF2_TX_PATH_BAND2,
				 eeprom[MT_EE_WIFI_CONF + 2]);
		nss = FIELD_GET(MT_EE_WIFI_CONF5_STREAM_NUM_BAND2,
				eeprom[MT_EE_WIFI_CONF + 5]);
		break;
	default:
		path = FIELD_GET(MT_EE_WIFI_CONF1_TX_PATH_BAND0,
				 eeprom[MT_EE_WIFI_CONF + 1]);
		nss = FIELD_GET(MT_EE_WIFI_CONF4_STREAM_NUM_BAND0,
				eeprom[MT_EE_WIFI_CONF + 4]);
		break;
	}

	if (!path || path > 4)
		path = 4;

	nss = min_t(u8, min_t(u8, 4, nss), path);

	mphy->antenna_mask = BIT(nss) - 1;
	mphy->chainmask = (BIT(path) - 1) << dev->chainshift[band_idx];
	dev->chainmask |= mphy->chainmask;
	if (band_idx < MT_BAND2)
		dev->chainshift[band_idx + 1] = dev->chainshift[band_idx] +
						hweight16(mphy->chainmask);

	return mt7996_eeprom_parse_band_config(phy);
}

int mt7996_eeprom_init(struct mt7996_dev *dev)
{
	int ret;

	ret = mt7996_eeprom_load(dev);
	if (ret < 0) {
		if (ret != -EINVAL)
			return ret;

		dev_warn(dev->mt76.dev, "eeprom load fail, use default bin\n");
		ret = mt7996_eeprom_load_default(dev);
		if (ret)
			return ret;
	}

	ret = mt7996_eeprom_parse_hw_cap(dev, &dev->phy);
	if (ret < 0)
		return ret;

	memcpy(dev->mphy.macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR, ETH_ALEN);
	mt76_eeprom_override(&dev->mphy);

	return 0;
}

int mt7996_eeprom_get_target_power(struct mt7996_dev *dev,
				   struct ieee80211_channel *chan)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	int target_power;

	if (chan->band == NL80211_BAND_5GHZ)
		target_power = eeprom[MT_EE_TX0_POWER_5G +
				      mt7996_get_channel_group_5g(chan->hw_value)];
	else if (chan->band == NL80211_BAND_6GHZ)
		target_power = eeprom[MT_EE_TX0_POWER_6G +
				      mt7996_get_channel_group_6g(chan->hw_value)];
	else
		target_power = eeprom[MT_EE_TX0_POWER_2G];

	return target_power;
}

s8 mt7996_eeprom_get_power_delta(struct mt7996_dev *dev, int band)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	u32 val;
	s8 delta;

	if (band == NL80211_BAND_5GHZ)
		val = eeprom[MT_EE_RATE_DELTA_5G];
	else if (band == NL80211_BAND_6GHZ)
		val = eeprom[MT_EE_RATE_DELTA_6G];
	else
		val = eeprom[MT_EE_RATE_DELTA_2G];

	if (!(val & MT_EE_RATE_DELTA_EN))
		return 0;

	delta = FIELD_GET(MT_EE_RATE_DELTA_MASK, val);

	return val & MT_EE_RATE_DELTA_SIGN ? delta : -delta;
}
