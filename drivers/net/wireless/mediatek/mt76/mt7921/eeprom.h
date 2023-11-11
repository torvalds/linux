/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT7921_EEPROM_H
#define __MT7921_EEPROM_H

#include "mt7921.h"

enum mt7921_eeprom_field {
	MT_EE_CHIP_ID =		0x000,
	MT_EE_VERSION =		0x002,
	MT_EE_MAC_ADDR =	0x004,
	MT_EE_WIFI_CONF =	0x07c,
	MT_EE_HW_TYPE =		0x55b,
	__MT_EE_MAX =		0x9ff
};

#define MT_EE_WIFI_CONF_TX_MASK			BIT(0)
#define MT_EE_WIFI_CONF_BAND_SEL		GENMASK(3, 2)

#define MT_EE_HW_TYPE_ENCAP			BIT(0)

enum mt7921_eeprom_band {
	MT_EE_NA,
	MT_EE_5GHZ,
	MT_EE_2GHZ,
	MT_EE_DUAL_BAND,
};

#endif
