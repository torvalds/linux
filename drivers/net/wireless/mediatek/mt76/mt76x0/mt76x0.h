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
#include "eeprom.h"

#define MT_CALIBRATE_INTERVAL		(4 * HZ)

#define MT_FREQ_CAL_INIT_DELAY		(30 * HZ)
#define MT_FREQ_CAL_CHECK_INTERVAL	(10 * HZ)
#define MT_FREQ_CAL_ADJ_INTERVAL	(HZ / 2)

#define MT_BBP_REG_VERSION		0x00

#define MT_USB_AGGR_SIZE_LIMIT		21 /* * 1024B */
#define MT_USB_AGGR_TIMEOUT		0x80 /* * 33ns */

struct mac_stats {
	u64 rx_stat[6];
	u64 tx_stat[6];
	u64 aggr_stat[2];
	u64 aggr_n[32];
	u64 zero_len_del[2];
};

struct mt76x0_eeprom_params;

#define MT_EE_TEMPERATURE_SLOPE		39
#define MT_FREQ_OFFSET_INVALID		-128

/* addr req mask */
#define MT_VEND_TYPE_EEPROM	BIT(31)
#define MT_VEND_TYPE_CFG	BIT(30)
#define MT_VEND_TYPE_MASK	(MT_VEND_TYPE_EEPROM | MT_VEND_TYPE_CFG)

#define MT_VEND_ADDR(type, n)	(MT_VEND_TYPE_##type | (n))

enum mt_bw {
	MT_BW_20,
	MT_BW_40,
};

/**
 * struct mt76x0_dev - adapter structure
 * @lock:		protects @wcid->tx_rate.
 * @mac_lock:		locks out mac80211's tx status and rx paths.
 * @con_mon_lock:	protects @ap_bssid, @bcn_*, @avg_rssi.
 * @mutex:		ensures exclusive access from mac80211 callbacks.
 * @reg_atomic_mutex:	ensures atomicity of indirect register accesses
 *			(accesses to RF and BBP).
 * @hw_atomic_mutex:	ensures exclusive access to HW during critical
 *			operations (power management, channel switch).
 */
struct mt76x0_dev {
	struct mt76_dev mt76; /* must be first */

	u8 data[32];

	struct delayed_work cal_work;
	struct delayed_work mac_work;

	spinlock_t mac_lock;

	struct mt76x0_caldata caldata;

	struct mutex reg_atomic_mutex;
	struct mutex hw_atomic_mutex;

	atomic_t avg_ampdu_len;

	/* Connection monitoring things */
	spinlock_t con_mon_lock;
	u8 ap_bssid[ETH_ALEN];

	s8 bcn_freq_off;
	u8 bcn_phy_mode;

	int avg_rssi; /* starts at 0 and converges */

	u8 agc_save;

	bool no_2ghz;

	struct mac_stats stats;
};

static inline bool is_mt7610e(struct mt76x0_dev *dev)
{
	/* TODO */
	return false;
}

void mt76x0_init_debugfs(struct mt76x0_dev *dev);

/* Compatibility with mt76 */
#define mt76_rmw_field(_dev, _reg, _field, _val)	\
	mt76_rmw(_dev, _reg, _field, FIELD_PREP(_field, _val))

/* Init */
struct mt76x0_dev *
mt76x0_alloc_device(struct device *pdev,
		    const struct mt76_driver_ops *drv_ops,
		    const struct ieee80211_ops *ops);
int mt76x0_init_hardware(struct mt76x0_dev *dev);
int mt76x0_register_device(struct mt76x0_dev *dev);
void mt76x0_chip_onoff(struct mt76x0_dev *dev, bool enable, bool reset);

int mt76x0_mac_start(struct mt76x0_dev *dev);
void mt76x0_mac_stop(struct mt76x0_dev *dev);

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
void mt76x0_phy_init(struct mt76x0_dev *dev);
int mt76x0_wait_bbp_ready(struct mt76x0_dev *dev);
void mt76x0_agc_save(struct mt76x0_dev *dev);
void mt76x0_agc_restore(struct mt76x0_dev *dev);
int mt76x0_phy_set_channel(struct mt76x0_dev *dev,
			    struct cfg80211_chan_def *chandef);
void mt76x0_phy_recalibrate_after_assoc(struct mt76x0_dev *dev);
int mt76x0_phy_get_rssi(struct mt76x0_dev *dev, struct mt76x02_rxwi *rxwi);
void mt76x0_phy_con_cal_onoff(struct mt76x0_dev *dev,
			       struct ieee80211_bss_conf *info);
void mt76x0_phy_set_txpower(struct mt76x0_dev *dev);

/* MAC */
void mt76x0_mac_work(struct work_struct *work);
void mt76x0_mac_set_protection(struct mt76x0_dev *dev, bool legacy_prot,
				int ht_mode);
void mt76x0_mac_set_short_preamble(struct mt76x0_dev *dev, bool short_preamb);
void mt76x0_mac_config_tsf(struct mt76x0_dev *dev, bool enable, int interval);
void mt76x0_mac_set_ampdu_factor(struct mt76x0_dev *dev);

/* TX */
void mt76x0_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		struct sk_buff *skb);
void mt76x0_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb);

#endif
