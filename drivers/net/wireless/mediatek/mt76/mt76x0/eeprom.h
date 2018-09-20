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

#include "../mt76x02_eeprom.h"

struct mt76x0_dev;

#define MT76X0U_EE_MAX_VER		0x0c
#define MT76X0_EEPROM_SIZE		512

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

	/* TX_PWR_CFG_* values from EEPROM for 20 and 40 Mhz bandwidths. */
	u32 tx_pwr_cfg_2g[5][2];
	u32 tx_pwr_cfg_5g[5][2];

	u8 tx_pwr_per_chan[58];

	struct reg_channel_bounds reg;
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
