// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7915.h"
#include "eeprom.h"

static inline bool mt7915_efuse_valid(u8 val)
{
	return !(val == 0xff);
}

u32 mt7915_eeprom_read(struct mt7915_dev *dev, u32 offset)
{
	u8 *data = dev->mt76.eeprom.data;

	if (!mt7915_efuse_valid(data[offset]))
		mt7915_mcu_get_eeprom(dev, offset);

	return data[offset];
}

static int mt7915_eeprom_load(struct mt7915_dev *dev)
{
	int ret;

	ret = mt76_eeprom_init(&dev->mt76, MT7915_EEPROM_SIZE);
	if (ret < 0)
		return ret;

	memset(dev->mt76.eeprom.data, -1, MT7915_EEPROM_SIZE);

	return 0;
}

static int mt7915_check_eeprom(struct mt7915_dev *dev)
{
	u16 val;
	u8 *eeprom = dev->mt76.eeprom.data;

	mt7915_eeprom_read(dev, 0);
	val = get_unaligned_le16(eeprom);

	switch (val) {
	case 0x7915:
		return 0;
	default:
		return -EINVAL;
	}
}

static void mt7915_eeprom_parse_hw_cap(struct mt7915_dev *dev)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	u8 tx_mask, max_nss = 4;
	u32 val = mt7915_eeprom_read(dev, MT_EE_WIFI_CONF);

	val = FIELD_GET(MT_EE_WIFI_CONF_BAND_SEL, val);
	switch (val) {
	case MT_EE_5GHZ:
		dev->mt76.cap.has_5ghz = true;
		break;
	case MT_EE_2GHZ:
		dev->mt76.cap.has_2ghz = true;
		break;
	default:
		dev->mt76.cap.has_2ghz = true;
		dev->mt76.cap.has_5ghz = true;
		break;
	}

	/* read tx mask from eeprom */
	tx_mask =  FIELD_GET(MT_EE_WIFI_CONF_TX_MASK,
			     eeprom[MT_EE_WIFI_CONF]);
	if (!tx_mask || tx_mask > max_nss)
		tx_mask = max_nss;

	dev->chainmask = BIT(tx_mask) - 1;
	dev->mphy.antenna_mask = dev->chainmask;
	dev->phy.chainmask = dev->chainmask;
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
	memcpy(dev->mt76.macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR,
	       ETH_ALEN);

	mt76_eeprom_override(&dev->mt76);

	return 0;
}

int mt7915_eeprom_get_target_power(struct mt7915_dev *dev,
				   struct ieee80211_channel *chan,
				   u8 chain_idx)
{
	int index, target_power;
	bool tssi_on;

	if (chain_idx > 3)
		return -EINVAL;

	tssi_on = mt7915_tssi_enabled(dev, chan->band);

	if (chan->band == NL80211_BAND_2GHZ) {
		index = MT_EE_TX0_POWER_2G + chain_idx * 3;
		target_power = mt7915_eeprom_read(dev, index);

		if (!tssi_on)
			target_power += mt7915_eeprom_read(dev, index + 1);
	} else {
		int group = mt7915_get_channel_group(chan->hw_value);

		index = MT_EE_TX0_POWER_5G + chain_idx * 12;
		target_power = mt7915_eeprom_read(dev, index + group);

		if (!tssi_on)
			target_power += mt7915_eeprom_read(dev, index + 8);
	}

	return target_power;
}

static const u8 sku_cck_delta_map[] = {
	SKU_CCK_GROUP0,
	SKU_CCK_GROUP0,
	SKU_CCK_GROUP1,
	SKU_CCK_GROUP1,
};

static const u8 sku_ofdm_delta_map[] = {
	SKU_OFDM_GROUP0,
	SKU_OFDM_GROUP0,
	SKU_OFDM_GROUP1,
	SKU_OFDM_GROUP1,
	SKU_OFDM_GROUP2,
	SKU_OFDM_GROUP2,
	SKU_OFDM_GROUP3,
	SKU_OFDM_GROUP4,
};

static const u8 sku_mcs_delta_map[] = {
	SKU_MCS_GROUP0,
	SKU_MCS_GROUP1,
	SKU_MCS_GROUP1,
	SKU_MCS_GROUP2,
	SKU_MCS_GROUP2,
	SKU_MCS_GROUP3,
	SKU_MCS_GROUP4,
	SKU_MCS_GROUP5,
	SKU_MCS_GROUP6,
	SKU_MCS_GROUP7,
	SKU_MCS_GROUP8,
	SKU_MCS_GROUP9,
};

#define SKU_GROUP(_mode, _len, _ofs_2g, _ofs_5g, _map)	\
	[_mode] = {					\
	.len = _len,					\
	.offset = {					\
		_ofs_2g,				\
		_ofs_5g,				\
	},						\
	.delta_map = _map				\
}

const struct sku_group mt7915_sku_groups[] = {
	SKU_GROUP(SKU_CCK, 4, 0x252, 0, sku_cck_delta_map),
	SKU_GROUP(SKU_OFDM, 8, 0x254, 0x29d, sku_ofdm_delta_map),

	SKU_GROUP(SKU_HT_BW20, 8, 0x259, 0x2a2, sku_mcs_delta_map),
	SKU_GROUP(SKU_HT_BW40, 9, 0x262, 0x2ab, sku_mcs_delta_map),
	SKU_GROUP(SKU_VHT_BW20, 12, 0x259, 0x2a2, sku_mcs_delta_map),
	SKU_GROUP(SKU_VHT_BW40, 12, 0x262, 0x2ab, sku_mcs_delta_map),
	SKU_GROUP(SKU_VHT_BW80, 12, 0, 0x2b4, sku_mcs_delta_map),
	SKU_GROUP(SKU_VHT_BW160, 12, 0, 0, sku_mcs_delta_map),

	SKU_GROUP(SKU_HE_RU26, 12, 0x27f, 0x2dd, sku_mcs_delta_map),
	SKU_GROUP(SKU_HE_RU52, 12, 0x289, 0x2e7, sku_mcs_delta_map),
	SKU_GROUP(SKU_HE_RU106, 12, 0x293, 0x2f1, sku_mcs_delta_map),
	SKU_GROUP(SKU_HE_RU242, 12, 0x26b, 0x2bf, sku_mcs_delta_map),
	SKU_GROUP(SKU_HE_RU484, 12, 0x275, 0x2c9, sku_mcs_delta_map),
	SKU_GROUP(SKU_HE_RU996, 12, 0, 0x2d3, sku_mcs_delta_map),
	SKU_GROUP(SKU_HE_RU2x996, 12, 0, 0, sku_mcs_delta_map),
};

static s8
mt7915_get_sku_delta(struct mt7915_dev *dev, u32 addr)
{
	u32 val = mt7915_eeprom_read(dev, addr);
	s8 delta = FIELD_GET(SKU_DELTA_VAL, val);

	if (!(val & SKU_DELTA_EN))
		return 0;

	return val & SKU_DELTA_ADD ? delta : -delta;
}

static void
mt7915_eeprom_init_sku_band(struct mt7915_dev *dev,
			    struct ieee80211_supported_band *sband)
{
	int i, band = sband->band;
	s8 *rate_power = dev->rate_power[band], max_delta = 0;
	u8 idx = 0;

	for (i = 0; i < ARRAY_SIZE(mt7915_sku_groups); i++) {
		const struct sku_group *sku = &mt7915_sku_groups[i];
		u32 offset = sku->offset[band];
		int j;

		if (!offset) {
			idx += sku->len;
			continue;
		}

		rate_power[idx++] = mt7915_get_sku_delta(dev, offset);
		if (rate_power[idx - 1] > max_delta)
			max_delta = rate_power[idx - 1];

		if (i == SKU_HT_BW20 || i == SKU_VHT_BW20)
			offset += 1;

		for (j = 1; j < sku->len; j++) {
			u32 addr = offset + sku->delta_map[j];

			rate_power[idx++] = mt7915_get_sku_delta(dev, addr);
			if (rate_power[idx - 1] > max_delta)
				max_delta = rate_power[idx - 1];
		}
	}

	rate_power[idx] = max_delta;
}

void mt7915_eeprom_init_sku(struct mt7915_dev *dev)
{
	mt7915_eeprom_init_sku_band(dev, &dev->mphy.sband_2g.sband);
	mt7915_eeprom_init_sku_band(dev, &dev->mphy.sband_5g.sband);
}
