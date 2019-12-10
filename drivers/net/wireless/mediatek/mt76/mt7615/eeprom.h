/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2019 MediaTek Inc. */

#ifndef __MT7615_EEPROM_H
#define __MT7615_EEPROM_H

#include "mt7615.h"

enum mt7615_eeprom_field {
	MT_EE_CHIP_ID =				0x000,
	MT_EE_VERSION =				0x002,
	MT_EE_MAC_ADDR =			0x004,
	MT_EE_NIC_CONF_0 =			0x034,
	MT_EE_NIC_CONF_1 =			0x036,
	MT_EE_WIFI_CONF =			0x03e,
	MT_EE_TX0_2G_TARGET_POWER =		0x058,
	MT_EE_TX0_5G_G0_TARGET_POWER =		0x070,
	MT_EE_TX1_5G_G0_TARGET_POWER =		0x098,
	MT_EE_EXT_PA_2G_TARGET_POWER =		0x0f2,
	MT_EE_EXT_PA_5G_TARGET_POWER =		0x0f3,
	MT_EE_TX2_5G_G0_TARGET_POWER =		0x142,
	MT_EE_TX3_5G_G0_TARGET_POWER =		0x16a,

	__MT_EE_MAX =				0x3bf
};

#define MT_EE_NIC_CONF_TX_MASK			GENMASK(7, 4)
#define MT_EE_NIC_CONF_RX_MASK			GENMASK(3, 0)

#define MT_EE_NIC_CONF_TSSI_2G			BIT(5)
#define MT_EE_NIC_CONF_TSSI_5G			BIT(6)

#define MT_EE_NIC_WIFI_CONF_BAND_SEL		GENMASK(5, 4)
enum mt7615_eeprom_band {
	MT_EE_DUAL_BAND,
	MT_EE_5GHZ,
	MT_EE_2GHZ,
	MT_EE_DBDC,
};

enum mt7615_channel_group {
	MT_CH_5G_JAPAN,
	MT_CH_5G_UNII_1,
	MT_CH_5G_UNII_2A,
	MT_CH_5G_UNII_2B,
	MT_CH_5G_UNII_2E_1,
	MT_CH_5G_UNII_2E_2,
	MT_CH_5G_UNII_2E_3,
	MT_CH_5G_UNII_3,
	__MT_CH_MAX
};

static inline enum mt7615_channel_group
mt7615_get_channel_group(int channel)
{
	if (channel >= 184 && channel <= 196)
		return MT_CH_5G_JAPAN;
	if (channel <= 48)
		return MT_CH_5G_UNII_1;
	if (channel <= 64)
		return MT_CH_5G_UNII_2A;
	if (channel <= 114)
		return MT_CH_5G_UNII_2E_1;
	if (channel <= 144)
		return MT_CH_5G_UNII_2E_2;
	if (channel <= 161)
		return MT_CH_5G_UNII_2E_3;
	return MT_CH_5G_UNII_3;
}

static inline bool
mt7615_ext_pa_enabled(struct mt7615_dev *dev, enum nl80211_band band)
{
	u8 *eep = dev->mt76.eeprom.data;

	if (band == NL80211_BAND_5GHZ)
		return !(eep[MT_EE_NIC_CONF_1 + 1] & MT_EE_NIC_CONF_TSSI_5G);
	else
		return !(eep[MT_EE_NIC_CONF_1 + 1] & MT_EE_NIC_CONF_TSSI_2G);
}

#endif
