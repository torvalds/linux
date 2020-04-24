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

#endif
