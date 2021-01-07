/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT7915_EEPROM_H
#define __MT7915_EEPROM_H

#include "mt7915.h"

struct cal_data {
	u8 count;
	u16 offset[60];
};

enum mt7915_eeprom_field {
	MT_EE_CHIP_ID =		0x000,
	MT_EE_VERSION =		0x002,
	MT_EE_MAC_ADDR =	0x004,
	MT_EE_MAC_ADDR2 =	0x00a,
	MT_EE_DDIE_FT_VERSION =	0x050,
	MT_EE_WIFI_CONF =	0x190,
	MT_EE_TX0_POWER_2G =	0x2fc,
	MT_EE_TX0_POWER_5G =	0x34b,
	MT_EE_ADIE_FT_VERSION =	0x9a0,

	__MT_EE_MAX =		0xe00
};

#define MT_EE_WIFI_CONF_TX_MASK			GENMASK(2, 0)
#define MT_EE_WIFI_CONF_BAND_SEL		GENMASK(7, 6)
#define MT_EE_WIFI_CONF_TSSI0_2G		BIT(0)
#define MT_EE_WIFI_CONF_TSSI0_5G		BIT(2)
#define MT_EE_WIFI_CONF_TSSI1_5G		BIT(4)

enum mt7915_eeprom_band {
	MT_EE_DUAL_BAND,
	MT_EE_5GHZ,
	MT_EE_2GHZ,
	MT_EE_DBDC,
};

#define SKU_DELTA_VAL		GENMASK(5, 0)
#define SKU_DELTA_ADD		BIT(6)
#define SKU_DELTA_EN		BIT(7)

enum mt7915_sku_delta_group {
	SKU_CCK_GROUP0,
	SKU_CCK_GROUP1,

	SKU_OFDM_GROUP0 = 0,
	SKU_OFDM_GROUP1,
	SKU_OFDM_GROUP2,
	SKU_OFDM_GROUP3,
	SKU_OFDM_GROUP4,

	SKU_MCS_GROUP0 = 0,
	SKU_MCS_GROUP1,
	SKU_MCS_GROUP2,
	SKU_MCS_GROUP3,
	SKU_MCS_GROUP4,
	SKU_MCS_GROUP5,
	SKU_MCS_GROUP6,
	SKU_MCS_GROUP7,
	SKU_MCS_GROUP8,
	SKU_MCS_GROUP9,
};

enum mt7915_sku_rate_group {
	SKU_CCK,
	SKU_OFDM,
	SKU_HT_BW20,
	SKU_HT_BW40,
	SKU_VHT_BW20,
	SKU_VHT_BW40,
	SKU_VHT_BW80,
	SKU_VHT_BW160,
	SKU_HE_RU26,
	SKU_HE_RU52,
	SKU_HE_RU106,
	SKU_HE_RU242,
	SKU_HE_RU484,
	SKU_HE_RU996,
	SKU_HE_RU2x996,
	MAX_SKU_RATE_GROUP_NUM,
};

struct sku_group {
	u8 len;
	u16 offset[2];
	const u8 *delta_map;
};

static inline int
mt7915_get_channel_group(int channel)
{
	if (channel >= 184 && channel <= 196)
		return 0;
	if (channel <= 48)
		return 1;
	if (channel <= 64)
		return 2;
	if (channel <= 96)
		return 3;
	if (channel <= 112)
		return 4;
	if (channel <= 128)
		return 5;
	if (channel <= 144)
		return 6;
	return 7;
}

static inline bool
mt7915_tssi_enabled(struct mt7915_dev *dev, enum nl80211_band band)
{
	u8 *eep = dev->mt76.eeprom.data;

	/* TODO: DBDC */
	if (band == NL80211_BAND_5GHZ)
		return eep[MT_EE_WIFI_CONF + 7] & MT_EE_WIFI_CONF_TSSI0_5G;
	else
		return eep[MT_EE_WIFI_CONF + 7] & MT_EE_WIFI_CONF_TSSI0_2G;
}

extern const struct sku_group mt7915_sku_groups[];

#endif
