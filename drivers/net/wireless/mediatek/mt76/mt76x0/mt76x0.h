/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 */

#ifndef MT76X0U_H
#define MT76X0U_H

#include <linux/bitfield.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/usb.h>
#include <linux/completion.h>
#include <net/mac80211.h>
#include <linux/debugfs.h>

#include "../mt76x02.h"
#include "eeprom.h"

#define MT7610E_FIRMWARE		"mediatek/mt7610e.bin"
#define MT7650E_FIRMWARE		"mediatek/mt7650e.bin"

#define MT7610U_FIRMWARE		"mediatek/mt7610u.bin"

#define MT_USB_AGGR_SIZE_LIMIT		21 /* * 1024B */
#define MT_USB_AGGR_TIMEOUT		0x80 /* * 33ns */

static inline bool is_mt7610e(struct mt76x02_dev *dev)
{
	if (!mt76_is_mmio(dev))
		return false;

	return mt76_chip(&dev->mt76) == 0x7610;
}

static inline bool is_mt7630(struct mt76x02_dev *dev)
{
	return mt76_chip(&dev->mt76) == 0x7630;
}

/* Init */
int mt76x0_init_hardware(struct mt76x02_dev *dev);
int mt76x0_register_device(struct mt76x02_dev *dev);
void mt76x0_chip_onoff(struct mt76x02_dev *dev, bool enable, bool reset);

int mt76x0_mac_start(struct mt76x02_dev *dev);
void mt76x0_mac_stop(struct mt76x02_dev *dev);

int mt76x0_config(struct ieee80211_hw *hw, u32 changed);

/* PHY */
void mt76x0_phy_init(struct mt76x02_dev *dev);
int mt76x0_phy_wait_bbp_ready(struct mt76x02_dev *dev);
int mt76x0_phy_set_channel(struct mt76x02_dev *dev,
			    struct cfg80211_chan_def *chandef);
void mt76x0_phy_set_txpower(struct mt76x02_dev *dev);
void mt76x0_phy_calibrate(struct mt76x02_dev *dev, bool power_on);
#endif
