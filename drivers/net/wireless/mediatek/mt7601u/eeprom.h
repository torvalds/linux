/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MT7601U_EEPROM_H
#define __MT7601U_EEPROM_H

struct mt7601u_dev;

#define MT7601U_EE_MAX_VER			0x0c
#define MT7601U_EEPROM_SIZE			256

#define MT7601U_DEFAULT_TX_POWER		6

enum mt76_eeprom_field {
	MT_EE_CHIP_ID =				0x00,
	MT_EE_VERSION_FAE =			0x02,
	MT_EE_VERSION_EE =			0x03,
	MT_EE_MAC_ADDR =			0x04,
	MT_EE_NIC_CONF_0 =			0x34,
	MT_EE_NIC_CONF_1 =			0x36,
	MT_EE_COUNTRY_REGION =			0x39,
	MT_EE_FREQ_OFFSET =			0x3a,
	MT_EE_NIC_CONF_2 =			0x42,

	MT_EE_LNA_GAIN =			0x44,
	MT_EE_RSSI_OFFSET =			0x46,

	MT_EE_TX_POWER_DELTA_BW40 =		0x50,
	MT_EE_TX_POWER_OFFSET =			0x52,

	MT_EE_TX_TSSI_SLOPE =			0x6e,
	MT_EE_TX_TSSI_OFFSET_GROUP =		0x6f,
	MT_EE_TX_TSSI_OFFSET =			0x76,

	MT_EE_TX_TSSI_TARGET_POWER =		0xd0,
	MT_EE_REF_TEMP =			0xd1,
	MT_EE_FREQ_OFFSET_COMPENSATION =	0xdb,
	MT_EE_TX_POWER_BYRATE_BASE =		0xde,

	MT_EE_USAGE_MAP_START =			0x1e0,
	MT_EE_USAGE_MAP_END =			0x1fc,
};

#define MT_EE_NIC_CONF_0_RX_PATH		GENMASK(3, 0)
#define MT_EE_NIC_CONF_0_TX_PATH		GENMASK(7, 4)
#define MT_EE_NIC_CONF_0_BOARD_TYPE		GENMASK(13, 12)

#define MT_EE_NIC_CONF_1_HW_RF_CTRL		BIT(0)
#define MT_EE_NIC_CONF_1_TEMP_TX_ALC		BIT(1)
#define MT_EE_NIC_CONF_1_LNA_EXT_2G		BIT(2)
#define MT_EE_NIC_CONF_1_LNA_EXT_5G		BIT(3)
#define MT_EE_NIC_CONF_1_TX_ALC_EN		BIT(13)

#define MT_EE_NIC_CONF_2_RX_STREAM		GENMASK(3, 0)
#define MT_EE_NIC_CONF_2_TX_STREAM		GENMASK(7, 4)
#define MT_EE_NIC_CONF_2_HW_ANTDIV		BIT(8)
#define MT_EE_NIC_CONF_2_XTAL_OPTION		GENMASK(10, 9)
#define MT_EE_NIC_CONF_2_TEMP_DISABLE		BIT(11)
#define MT_EE_NIC_CONF_2_COEX_METHOD		GENMASK(15, 13)

#define MT_EE_TX_POWER_BYRATE(i)		(MT_EE_TX_POWER_BYRATE_BASE + \
						 (i) * 4)

#define MT_EFUSE_USAGE_MAP_SIZE			(MT_EE_USAGE_MAP_END -	\
						 MT_EE_USAGE_MAP_START + 1)

enum mt7601u_eeprom_access_modes {
	MT_EE_READ = 0,
	MT_EE_PHYSICAL_READ = 1,
};

struct power_per_rate  {
	u8 raw;  /* validated s6 value */
	s8 bw20; /* sign-extended int */
	s8 bw40; /* sign-extended int */
};

/* Power per rate - one value per two rates */
struct mt7601u_rate_power {
	struct power_per_rate cck[2];
	struct power_per_rate ofdm[4];
	struct power_per_rate ht[4];
};

struct reg_channel_bounds {
	u8 start;
	u8 num;
};

struct mt7601u_eeprom_params {
	bool tssi_enabled;
	u8 rf_freq_off;
	s8 rssi_offset[2];
	s8 ref_temp;
	s8 lna_gain;

	u8 chan_pwr[14];
	struct mt7601u_rate_power power_rate_table;
	s8 real_cck_bw20[2];

	/* TSSI stuff - only with internal TX ALC */
	struct tssi_data {
		int tx0_delta_offset;
		u8 slope;
		u8 offset[3];
	} tssi_data;

	struct reg_channel_bounds reg;
};

int mt7601u_eeprom_init(struct mt7601u_dev *dev);

static inline u32 s6_validate(u32 reg)
{
	WARN_ON(reg & ~GENMASK(5, 0));
	return reg & GENMASK(5, 0);
}

static inline int s6_to_int(u32 reg)
{
	int s6;

	s6 = s6_validate(reg);
	if (s6 & BIT(5))
		s6 -= BIT(6);

	return s6;
}

static inline u32 int_to_s6(int val)
{
	if (val < -0x20)
		return 0x20;
	if (val > 0x1f)
		return 0x1f;

	return val & 0x3f;
}

#endif
