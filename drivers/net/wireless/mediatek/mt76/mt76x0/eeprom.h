/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
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

#ifndef __MT76X0U_EEPROM_H
#define __MT76X0U_EEPROM_H

struct mt76x0_dev;

#define MT76X0U_EE_MAX_VER			0x0c
#define MT76X0_EEPROM_SIZE			512

#define MT76X0U_DEFAULT_TX_POWER		6

enum mt76_eeprom_field {
	MT_EE_CHIP_ID =				0x00,
	MT_EE_VERSION_FAE =			0x02,
	MT_EE_VERSION_EE =			0x03,
	MT_EE_MAC_ADDR =			0x04,
	MT_EE_NIC_CONF_0 =			0x34,
	MT_EE_NIC_CONF_1 =			0x36,
	MT_EE_COUNTRY_REGION_5GHZ =		0x38,
	MT_EE_COUNTRY_REGION_2GHZ =		0x39,
	MT_EE_FREQ_OFFSET =			0x3a,
	MT_EE_NIC_CONF_2 =			0x42,

	MT_EE_LNA_GAIN_2GHZ =			0x44,
	MT_EE_LNA_GAIN_5GHZ_0 =			0x45,
	MT_EE_RSSI_OFFSET =			0x46,
	MT_EE_RSSI_OFFSET_5GHZ =		0x4a,
	MT_EE_LNA_GAIN_5GHZ_1 =			0x49,
	MT_EE_LNA_GAIN_5GHZ_2 =			0x4d,

	MT_EE_TX_POWER_DELTA_BW40 =		0x50,

	MT_EE_TX_POWER_OFFSET_2GHZ =		0x52,

	MT_EE_TX_TSSI_SLOPE =			0x6e,
	MT_EE_TX_TSSI_OFFSET_GROUP =		0x6f,
	MT_EE_TX_TSSI_OFFSET =			0x76,

	MT_EE_TX_POWER_OFFSET_5GHZ =		0x78,

	MT_EE_TEMP_OFFSET =			0xd1,
	MT_EE_FREQ_OFFSET_COMPENSATION =	0xdb,
	MT_EE_TX_POWER_BYRATE_BASE =		0xde,

	MT_EE_TX_POWER_BYRATE_BASE_5GHZ =	0x120,

	MT_EE_USAGE_MAP_START =			0x1e0,
	MT_EE_USAGE_MAP_END =			0x1fc,
};

#define MT_EE_NIC_CONF_0_RX_PATH		GENMASK(3, 0)
#define MT_EE_NIC_CONF_0_TX_PATH		GENMASK(7, 4)
#define MT_EE_NIC_CONF_0_PA_TYPE		GENMASK(9, 8)
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

enum mt76x0_eeprom_access_modes {
	MT_EE_READ = 0,
	MT_EE_PHYSICAL_READ = 1,
};

struct reg_channel_bounds {
	u8 start;
	u8 num;
};

struct mt76x0_eeprom_params {
	u8 rf_freq_off;
	s16 temp_off;
	s8 rssi_offset_2ghz[2];
	s8 rssi_offset_5ghz[3];
	s8 lna_gain_2ghz;
	s8 lna_gain_5ghz[3];
	u8 pa_type;

	/* TX_PWR_CFG_* values from EEPROM for 20 and 40 Mhz bandwidths. */
	u32 tx_pwr_cfg_2g[5][2];
	u32 tx_pwr_cfg_5g[5][2];

	u8 tx_pwr_per_chan[58];

	struct reg_channel_bounds reg;

	bool has_2ghz;
	bool has_5ghz;
};

int mt76x0_eeprom_init(struct mt76x0_dev *dev);

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
