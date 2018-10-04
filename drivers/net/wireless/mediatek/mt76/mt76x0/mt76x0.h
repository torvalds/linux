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

#include "../mt76.h"
#include "../mt76x02_regs.h"
#include "../mt76x02_mac.h"
#include "../mt76x02_util.h"
#include "eeprom.h"

#define MT_CALIBRATE_INTERVAL		(4 * HZ)

#define MT_USB_AGGR_SIZE_LIMIT		21 /* * 1024B */
#define MT_USB_AGGR_TIMEOUT		0x80 /* * 33ns */

static inline bool is_mt7610e(struct mt76x02_dev *dev)
{
	/* TODO */
	return false;
}

void mt76x0_init_debugfs(struct mt76x02_dev *dev);

/* Init */
struct mt76x02_dev *
mt76x0_alloc_device(struct device *pdev,
		    const struct mt76_driver_ops *drv_ops,
		    const struct ieee80211_ops *ops);
int mt76x0_init_hardware(struct mt76x02_dev *dev);
int mt76x0_register_device(struct mt76x02_dev *dev);
void mt76x0_chip_onoff(struct mt76x02_dev *dev, bool enable, bool reset);

int mt76x0_mac_start(struct mt76x02_dev *dev);
void mt76x0_mac_stop(struct mt76x02_dev *dev);

int mt76x0_config(struct ieee80211_hw *hw, u32 changed);
void mt76x0_bss_info_changed(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *info, u32 changed);
void mt76x0_sw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    const u8 *mac_addr);
void mt76x0_sw_scan_complete(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif);
int mt76x0_set_rts_threshold(struct ieee80211_hw *hw, u32 value);

/* PHY */
void mt76x0_phy_init(struct mt76x02_dev *dev);
int mt76x0_wait_bbp_ready(struct mt76x02_dev *dev);
void mt76x0_agc_save(struct mt76x02_dev *dev);
void mt76x0_agc_restore(struct mt76x02_dev *dev);
int mt76x0_phy_set_channel(struct mt76x02_dev *dev,
			    struct cfg80211_chan_def *chandef);
void mt76x0_phy_recalibrate_after_assoc(struct mt76x02_dev *dev);
int mt76x0_phy_get_rssi(struct mt76x02_dev *dev, struct mt76x02_rxwi *rxwi);
void mt76x0_phy_set_txpower(struct mt76x02_dev *dev);

/* MAC */
void mt76x0_mac_work(struct work_struct *work);
void mt76x0_mac_set_protection(struct mt76x02_dev *dev, bool legacy_prot,
				int ht_mode);
void mt76x0_mac_set_short_preamble(struct mt76x02_dev *dev, bool short_preamb);
void mt76x0_mac_config_tsf(struct mt76x02_dev *dev, bool enable, int interval);
void mt76x0_mac_set_ampdu_factor(struct mt76x02_dev *dev);

/* TX */
void mt76x0_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		struct sk_buff *skb);
void mt76x0_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb);

#endif
