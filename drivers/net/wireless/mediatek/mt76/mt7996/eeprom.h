/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#ifndef __MT7996_EEPROM_H
#define __MT7996_EEPROM_H

#include "mt7996.h"

enum mt7996_eeprom_field {
	MT_EE_CHIP_ID =		0x000,
	MT_EE_VERSION =		0x002,
	MT_EE_MAC_ADDR =	0x004,
	MT_EE_MAC_ADDR2 =	0x00a,
	MT_EE_WIFI_CONF =	0x190,
	MT_EE_MAC_ADDR3 =	0x2c0,
	MT_EE_RATE_DELTA_2G =	0x1400,
	MT_EE_RATE_DELTA_5G =	0x147d,
	MT_EE_RATE_DELTA_6G =	0x154a,
	MT_EE_TX0_POWER_2G =	0x1300,
	MT_EE_TX0_POWER_5G =	0x1301,
	MT_EE_TX0_POWER_6G =	0x1310,

	__MT_EE_MAX =	0x1dff,
};

#define MT_EE_WIFI_CONF0_TX_PATH		GENMASK(2, 0)
#define MT_EE_WIFI_CONF0_BAND_SEL		GENMASK(2, 0)
#define MT_EE_WIFI_CONF1_BAND_SEL		GENMASK(5, 3)
#define MT_EE_WIFI_CONF2_BAND_SEL		GENMASK(2, 0)

#define MT_EE_WIFI_CONF1_TX_PATH_BAND0		GENMASK(5, 3)
#define MT_EE_WIFI_CONF2_TX_PATH_BAND1		GENMASK(2, 0)
#define MT_EE_WIFI_CONF2_TX_PATH_BAND2		GENMASK(5, 3)
#define MT_EE_WIFI_CONF4_STREAM_NUM_BAND0	GENMASK(5, 3)
#define MT_EE_WIFI_CONF5_STREAM_NUM_BAND1	GENMASK(2, 0)
#define MT_EE_WIFI_CONF5_STREAM_NUM_BAND2	GENMASK(5, 3)

#define MT_EE_RATE_DELTA_MASK			GENMASK(5, 0)
#define MT_EE_RATE_DELTA_SIGN			BIT(6)
#define MT_EE_RATE_DELTA_EN			BIT(7)

enum mt7996_eeprom_band {
	MT_EE_BAND_SEL_DEFAULT,
	MT_EE_BAND_SEL_2GHZ,
	MT_EE_BAND_SEL_5GHZ,
	MT_EE_BAND_SEL_6GHZ,
};

static inline int
mt7996_get_channel_group_5g(int channel)
{
	if (channel <= 64)
		return 0;
	if (channel <= 96)
		return 1;
	if (channel <= 128)
		return 2;
	if (channel <= 144)
		return 3;
	return 4;
}

static inline int
mt7996_get_channel_group_6g(int channel)
{
	if (channel <= 29)
		return 0;

	return DIV_ROUND_UP(channel - 29, 32);
}

#endif
