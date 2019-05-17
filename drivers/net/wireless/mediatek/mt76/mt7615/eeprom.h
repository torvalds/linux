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
	MT_EE_WIFI_CONF =			0x03e,

	__MT_EE_MAX =				0x3bf
};

#define MT_EE_NIC_WIFI_CONF_BAND_SEL		GENMASK(5, 4)
enum mt7615_eeprom_band {
	MT_EE_DUAL_BAND,
	MT_EE_5GHZ,
	MT_EE_2GHZ,
	MT_EE_DBDC,
};

#endif
