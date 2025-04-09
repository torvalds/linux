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
	case MT7996_DEVICE_ID:
		return is_mt7996(&dev->mt76) ? 0 : -EINVAL;
	case MT7992_DEVICE_ID:
		return is_mt7992(&dev->mt76) ? 0 : -EINVAL;
	case MT7990_DEVICE_ID:
		return is_mt7990(&dev->mt76) ? 0 : -EINVAL;
	default:
		return -EINVAL;
	}
}

static char *mt7996_eeprom_name(struct mt7996_dev *dev)
{
	switch (mt76_chip(&dev->mt76)) {
	case MT7992_DEVICE_ID:
		switch (dev->var.type) {
		case MT7992_VAR_TYPE_23:
			if (dev->var.fem == MT7996_FEM_INT)
				return MT7992_EEPROM_DEFAULT_23_INT;
			return MT7992_EEPROM_DEFAULT_23;
		case MT7992_VAR_TYPE_44:
		default:
			if (dev->var.fem == MT7996_FEM_INT)
				return MT7992_EEPROM_DEFAULT_INT;
			if (dev->var.fem == MT7996_FEM_MIX)
				return MT7992_EEPROM_DEFAULT_MIX;
			return MT7992_EEPROM_DEFAULT;
		}
	case MT7990_DEVICE_ID:
		if (dev->var.fem == MT7996_FEM_INT)
			return MT7990_EEPROM_DEFAULT_INT;
		return MT7990_EEPROM_DEFAULT;
	case MT7996_DEVICE_ID:
	default:
		switch (dev->var.type) {
		case MT7996_VAR_TYPE_233:
			if (dev->var.fem == MT7996_FEM_INT)
				return MT7996_EEPROM_DEFAULT_233_INT;
			return MT7996_EEPROM_DEFAULT_233;
		case MT7996_VAR_TYPE_444:
		default:
			if (dev->var.fem == MT7996_FEM_INT)
				return MT7996_EEPROM_DEFAULT_INT;
			return MT7996_EEPROM_DEFAULT;
		}
	}
}

static void
mt7996_eeprom_parse_stream(const u8 *eeprom, u8 band_idx, u8 *path,
			   u8 *rx_path, u8 *nss)
{
	switch (band_idx) {
	case MT_BAND1:
		*path = FIELD_GET(MT_EE_WIFI_CONF2_TX_PATH_BAND1,
				  eeprom[MT_EE_WIFI_CONF + 2]);
		*rx_path = FIELD_GET(MT_EE_WIFI_CONF3_RX_PATH_BAND1,
				     eeprom[MT_EE_WIFI_CONF + 3]);
		*nss = FIELD_GET(MT_EE_WIFI_CONF5_STREAM_NUM_BAND1,
				 eeprom[MT_EE_WIFI_CONF + 5]);
		break;
	case MT_BAND2:
		*path = FIELD_GET(MT_EE_WIFI_CONF2_TX_PATH_BAND2,
				  eeprom[MT_EE_WIFI_CONF + 2]);
		*rx_path = FIELD_GET(MT_EE_WIFI_CONF4_RX_PATH_BAND2,
				     eeprom[MT_EE_WIFI_CONF + 4]);
		*nss = FIELD_GET(MT_EE_WIFI_CONF5_STREAM_NUM_BAND2,
				 eeprom[MT_EE_WIFI_CONF + 5]);
		break;
	default:
		*path = FIELD_GET(MT_EE_WIFI_CONF1_TX_PATH_BAND0,
				  eeprom[MT_EE_WIFI_CONF + 1]);
		*rx_path = FIELD_GET(MT_EE_WIFI_CONF3_RX_PATH_BAND0,
				     eeprom[MT_EE_WIFI_CONF + 3]);
		*nss = FIELD_GET(MT_EE_WIFI_CONF4_STREAM_NUM_BAND0,
				 eeprom[MT_EE_WIFI_CONF + 4]);
		break;
	}
}

static bool mt7996_eeprom_variant_valid(struct mt7996_dev *dev, const u8 *def)
{
#define FEM_INT	0
#define FEM_EXT	3
	u8 *eeprom = dev->mt76.eeprom.data, fem[2];
	int i;

	for (i = 0; i < 2; i++)
		fem[i] = u8_get_bits(eeprom[MT_EE_WIFI_CONF + 6 + i],
				     MT_EE_WIFI_PA_LNA_CONFIG);

	if (dev->var.fem == MT7996_FEM_EXT &&
	    !(fem[0] == FEM_EXT && fem[1] == FEM_EXT))
		return false;
	else if (dev->var.fem == MT7996_FEM_INT &&
		 !(fem[0] == FEM_INT && fem[1] == FEM_INT))
		return false;
	else if (dev->var.fem == MT7996_FEM_MIX &&
		 !(fem[0] == FEM_INT && fem[1] == FEM_EXT))
		return false;

	for (i = 0; i < __MT_MAX_BAND; i++) {
		u8 path, rx_path, nss;
		u8 def_path, def_rx_path, def_nss;

		if (!dev->mt76.phys[i])
			continue;

		mt7996_eeprom_parse_stream(eeprom, i, &path, &rx_path, &nss);
		mt7996_eeprom_parse_stream(def, i, &def_path, &def_rx_path,
					   &def_nss);
		if (path > def_path || rx_path > def_rx_path || nss > def_nss)
			return false;
	}

	return true;
}

static int
mt7996_eeprom_check_or_use_default(struct mt7996_dev *dev, bool use_default)
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

	if (!use_default && mt7996_eeprom_variant_valid(dev, fw->data))
		goto out;

	dev_warn(dev->mt76.dev, "eeprom load fail, use default bin\n");
	memcpy(eeprom, fw->data, MT7996_EEPROM_SIZE);
	dev->flash_mode = true;

out:
	release_firmware(fw);

	return ret;
}

static int mt7996_eeprom_load(struct mt7996_dev *dev)
{
	bool use_default = false;
	int ret;

	ret = mt76_eeprom_init(&dev->mt76, MT7996_EEPROM_SIZE);
	if (ret < 0)
		return ret;

	if (ret && !mt7996_check_eeprom(dev)) {
		dev->flash_mode = true;
		goto out;
	}

	if (!dev->flash_mode) {
		u32 eeprom_blk_size = MT7996_EEPROM_BLOCK_SIZE;
		u32 block_num = DIV_ROUND_UP(MT7996_EEPROM_SIZE, eeprom_blk_size);
		u8 free_block_num;
		int i;

		memset(dev->mt76.eeprom.data, 0, MT7996_EEPROM_SIZE);
		ret = mt7996_mcu_get_eeprom_free_block(dev, &free_block_num);
		if (ret < 0)
			return ret;

		/* efuse info isn't enough */
		if (free_block_num >= 59) {
			use_default = true;
			goto out;
		}

		/* check if eeprom data from fw is valid */
		if (mt7996_mcu_get_eeprom(dev, 0, NULL, 0) ||
		    mt7996_check_eeprom(dev)) {
			use_default = true;
			goto out;
		}

		/* read eeprom data from fw */
		for (i = 1; i < block_num; i++) {
			u32 len = eeprom_blk_size;

			if (i == block_num - 1)
				len = MT7996_EEPROM_SIZE % eeprom_blk_size;
			ret = mt7996_mcu_get_eeprom(dev, i * eeprom_blk_size,
						    NULL, len);
			if (ret && ret != -EINVAL) {
				use_default = true;
				goto out;
			}
		}
	}

out:
	return mt7996_eeprom_check_or_use_default(dev, use_default);
}

static int mt7996_eeprom_parse_efuse_hw_cap(struct mt7996_phy *phy,
					    u8 *path, u8 *rx_path, u8 *nss)
{
#define MODE_HE_ONLY		BIT(0)
#define WTBL_SIZE_GROUP		GENMASK(31, 28)
#define STREAM_CAP(_offs)	((cap & (0x7 << (_offs))) >> (_offs))
	struct mt7996_dev *dev = phy->dev;
	u32 cap = 0;
	int ret;

	ret = mt7996_mcu_get_chip_config(dev, &cap);
	if (ret)
		return ret;

	if (cap) {
		u8 band_offs = phy->mt76->band_idx * 3;

		dev->has_eht = !(cap & MODE_HE_ONLY);
		dev->wtbl_size_group = u32_get_bits(cap, WTBL_SIZE_GROUP);
		*nss = min_t(u8, *nss, STREAM_CAP(1 + band_offs));
		*path = min_t(u8, *path, STREAM_CAP(10 + band_offs));
		*rx_path = min_t(u8, *rx_path, STREAM_CAP(19 + band_offs));
	}

	if (dev->wtbl_size_group < 2 || dev->wtbl_size_group > 4)
		dev->wtbl_size_group = is_mt7996(&dev->mt76) ? 4 : 2;

	return 0;
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
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int mt7996_eeprom_parse_hw_cap(struct mt7996_dev *dev, struct mt7996_phy *phy)
{
	u8 path, rx_path, nss, band_idx = phy->mt76->band_idx;
	u8 *eeprom = dev->mt76.eeprom.data;
	struct mt76_phy *mphy = phy->mt76;
	int max_path = 5, max_nss = 4;
	int ret;

	mt7996_eeprom_parse_stream(eeprom, band_idx, &path, &rx_path, &nss);
	ret = mt7996_eeprom_parse_efuse_hw_cap(phy, &path, &rx_path, &nss);
	if (ret)
		return ret;

	if (!path || path > max_path)
		path = max_path;

	if (!nss || nss > max_nss)
		nss = max_nss;

	nss = min_t(u8, nss, path);

	if (path != rx_path)
		phy->has_aux_rx = true;

	mphy->antenna_mask = BIT(nss) - 1;
	mphy->chainmask = (BIT(path) - 1) << dev->chainshift[band_idx];
	phy->orig_chainmask = mphy->chainmask;
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
	if (ret < 0)
		return ret;

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

bool mt7996_eeprom_has_background_radar(struct mt7996_dev *dev)
{
	switch (mt76_chip(&dev->mt76)) {
	case MT7996_DEVICE_ID:
		if (dev->var.type == MT7996_VAR_TYPE_233)
			return false;
		break;
	case MT7992_DEVICE_ID:
		if (dev->var.type == MT7992_VAR_TYPE_23)
			return false;
		break;
	case MT7990_DEVICE_ID: {
		u8 path, rx_path, nss, *eeprom = dev->mt76.eeprom.data;

		mt7996_eeprom_parse_stream(eeprom, MT_BAND1, &path, &rx_path, &nss);
		/* Disable background radar capability in 3T3R */
		if (path == 3 || rx_path == 3)
			return false;
		break;
		}
	default:
		return false;
	}

	return true;
}
