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

#ifndef MT7601U_H
#define MT7601U_H

#include <linux/bitfield.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/usb.h>
#include <linux/completion.h>
#include <net/mac80211.h>
#include <linux/debugfs.h>

#include "regs.h"

#define MT_CALIBRATE_INTERVAL		(4 * HZ)

#define MT_FREQ_CAL_INIT_DELAY		(30 * HZ)
#define MT_FREQ_CAL_CHECK_INTERVAL	(10 * HZ)
#define MT_FREQ_CAL_ADJ_INTERVAL	(HZ / 2)

#define MT_BBP_REG_VERSION		0x00

#define MT_USB_AGGR_SIZE_LIMIT		28 /* * 1024B */
#define MT_USB_AGGR_TIMEOUT		0x80 /* * 33ns */
#define MT_RX_ORDER			3
#define MT_RX_URB_SIZE			(PAGE_SIZE << MT_RX_ORDER)

struct mt7601u_dma_buf {
	struct urb *urb;
	void *buf;
	dma_addr_t dma;
	size_t len;
};

struct mt7601u_mcu {
	struct mutex mutex;

	u8 msg_seq;

	struct mt7601u_dma_buf resp;
	struct completion resp_cmpl;
};

struct mt7601u_freq_cal {
	struct delayed_work work;
	u8 freq;
	bool enabled;
	bool adjusting;
};

struct mac_stats {
	u64 rx_stat[6];
	u64 tx_stat[6];
	u64 aggr_stat[2];
	u64 aggr_n[32];
	u64 zero_len_del[2];
};

#define N_RX_ENTRIES	16
struct mt7601u_rx_queue {
	struct mt7601u_dev *dev;

	struct mt7601u_dma_buf_rx {
		struct urb *urb;
		struct page *p;
	} e[N_RX_ENTRIES];

	unsigned int start;
	unsigned int end;
	unsigned int entries;
	unsigned int pending;
};

#define N_TX_ENTRIES	64

struct mt7601u_tx_queue {
	struct mt7601u_dev *dev;

	struct mt7601u_dma_buf_tx {
		struct urb *urb;
		struct sk_buff *skb;
	} e[N_TX_ENTRIES];

	unsigned int start;
	unsigned int end;
	unsigned int entries;
	unsigned int used;
	unsigned int fifo_seq;
};

/* WCID allocation:
 *     0: mcast wcid
 *     1: bssid wcid
 *  1...: STAs
 * ...7e: group wcids
 *    7f: reserved
 */
#define N_WCIDS		128
#define GROUP_WCID(idx)	(N_WCIDS - 2 - idx)

struct mt7601u_eeprom_params;

#define MT_EE_TEMPERATURE_SLOPE		39
#define MT_FREQ_OFFSET_INVALID		-128

enum mt_temp_mode {
	MT_TEMP_MODE_NORMAL,
	MT_TEMP_MODE_HIGH,
	MT_TEMP_MODE_LOW,
};

enum mt_bw {
	MT_BW_20,
	MT_BW_40,
};

enum {
	MT7601U_STATE_INITIALIZED,
	MT7601U_STATE_REMOVED,
	MT7601U_STATE_WLAN_RUNNING,
	MT7601U_STATE_MCU_RUNNING,
	MT7601U_STATE_SCANNING,
	MT7601U_STATE_READING_STATS,
	MT7601U_STATE_MORE_STATS,
};

/**
 * struct mt7601u_dev - adapter structure
 * @lock:		protects @wcid->tx_rate.
 * @mac_lock:		locks out mac80211's tx status and rx paths.
 * @tx_lock:		protects @tx_q and changes of MT7601U_STATE_*_STATS
 *			flags in @state.
 * @rx_lock:		protects @rx_q.
 * @con_mon_lock:	protects @ap_bssid, @bcn_*, @avg_rssi.
 * @mutex:		ensures exclusive access from mac80211 callbacks.
 * @vendor_req_mutex:	protects @vend_buf, ensures atomicity of read/write
 *			accesses
 * @reg_atomic_mutex:	ensures atomicity of indirect register accesses
 *			(accesses to RF and BBP).
 * @hw_atomic_mutex:	ensures exclusive access to HW during critical
 *			operations (power management, channel switch).
 */
struct mt7601u_dev {
	struct ieee80211_hw *hw;
	struct device *dev;

	unsigned long state;

	struct mutex mutex;

	unsigned long wcid_mask[N_WCIDS / BITS_PER_LONG];

	struct cfg80211_chan_def chandef;
	struct ieee80211_supported_band *sband_2g;

	struct mt7601u_mcu mcu;

	struct delayed_work cal_work;
	struct delayed_work mac_work;

	struct workqueue_struct *stat_wq;
	struct delayed_work stat_work;

	struct mt76_wcid *mon_wcid;
	struct mt76_wcid __rcu *wcid[N_WCIDS];

	spinlock_t lock;
	spinlock_t mac_lock;

	const u16 *beacon_offsets;

	u8 macaddr[ETH_ALEN];
	struct mt7601u_eeprom_params *ee;

	struct mutex vendor_req_mutex;
	void *vend_buf;

	struct mutex reg_atomic_mutex;
	struct mutex hw_atomic_mutex;

	u32 rxfilter;
	u32 debugfs_reg;

	u8 out_eps[8];
	u8 in_eps[8];
	u16 out_max_packet;
	u16 in_max_packet;

	/* TX */
	spinlock_t tx_lock;
	struct tasklet_struct tx_tasklet;
	struct mt7601u_tx_queue *tx_q;
	struct sk_buff_head tx_skb_done;

	atomic_t avg_ampdu_len;

	/* RX */
	spinlock_t rx_lock;
	struct tasklet_struct rx_tasklet;
	struct mt7601u_rx_queue rx_q;

	/* Connection monitoring things */
	spinlock_t con_mon_lock;
	u8 ap_bssid[ETH_ALEN];

	s8 bcn_freq_off;
	u8 bcn_phy_mode;

	int avg_rssi; /* starts at 0 and converges */

	u8 agc_save;

	struct mt7601u_freq_cal freq_cal;

	bool tssi_read_trig;

	s8 tssi_init;
	s8 tssi_init_hvga;
	s16 tssi_init_hvga_offset_db;

	int prev_pwr_diff;

	enum mt_temp_mode temp_mode;
	int curr_temp;
	int dpd_temp;
	s8 raw_temp;
	bool pll_lock_protect;

	u8 bw;
	bool chan_ext_below;

	/* PA mode */
	u32 rf_pa_mode[2];

	struct mac_stats stats;
};

struct mt7601u_tssi_params {
	char tssi0;
	int trgt_power;
};

struct mt76_wcid {
	u8 idx;
	u8 hw_key_idx;

	u16 tx_rate;
	bool tx_rate_set;
	u8 tx_rate_nss;
};

struct mt76_vif {
	u8 idx;

	struct mt76_wcid group_wcid;
};

struct mt76_sta {
	struct mt76_wcid wcid;
	u16 agg_ssn[IEEE80211_NUM_TIDS];
};

struct mt76_reg_pair {
	u32 reg;
	u32 value;
};

struct mt7601u_rxwi;

extern const struct ieee80211_ops mt7601u_ops;

void mt7601u_init_debugfs(struct mt7601u_dev *dev);

u32 mt7601u_rr(struct mt7601u_dev *dev, u32 offset);
void mt7601u_wr(struct mt7601u_dev *dev, u32 offset, u32 val);
u32 mt7601u_rmw(struct mt7601u_dev *dev, u32 offset, u32 mask, u32 val);
u32 mt7601u_rmc(struct mt7601u_dev *dev, u32 offset, u32 mask, u32 val);
void mt7601u_wr_copy(struct mt7601u_dev *dev, u32 offset,
		     const void *data, int len);

int mt7601u_wait_asic_ready(struct mt7601u_dev *dev);
bool mt76_poll(struct mt7601u_dev *dev, u32 offset, u32 mask, u32 val,
	       int timeout);
bool mt76_poll_msec(struct mt7601u_dev *dev, u32 offset, u32 mask, u32 val,
		    int timeout);

/* Compatibility with mt76 */
#define mt76_rmw_field(_dev, _reg, _field, _val)	\
	mt76_rmw(_dev, _reg, _field, FIELD_PREP(_field, _val))

static inline u32 mt76_rr(struct mt7601u_dev *dev, u32 offset)
{
	return mt7601u_rr(dev, offset);
}

static inline void mt76_wr(struct mt7601u_dev *dev, u32 offset, u32 val)
{
	return mt7601u_wr(dev, offset, val);
}

static inline u32
mt76_rmw(struct mt7601u_dev *dev, u32 offset, u32 mask, u32 val)
{
	return mt7601u_rmw(dev, offset, mask, val);
}

static inline u32 mt76_set(struct mt7601u_dev *dev, u32 offset, u32 val)
{
	return mt76_rmw(dev, offset, 0, val);
}

static inline u32 mt76_clear(struct mt7601u_dev *dev, u32 offset, u32 val)
{
	return mt76_rmw(dev, offset, val, 0);
}

int mt7601u_write_reg_pairs(struct mt7601u_dev *dev, u32 base,
			    const struct mt76_reg_pair *data, int len);
int mt7601u_burst_write_regs(struct mt7601u_dev *dev, u32 offset,
			     const u32 *data, int n);
void mt7601u_addr_wr(struct mt7601u_dev *dev, const u32 offset, const u8 *addr);

/* Init */
struct mt7601u_dev *mt7601u_alloc_device(struct device *dev);
int mt7601u_init_hardware(struct mt7601u_dev *dev);
int mt7601u_register_device(struct mt7601u_dev *dev);
void mt7601u_cleanup(struct mt7601u_dev *dev);

int mt7601u_mac_start(struct mt7601u_dev *dev);
void mt7601u_mac_stop(struct mt7601u_dev *dev);

/* PHY */
int mt7601u_phy_init(struct mt7601u_dev *dev);
int mt7601u_wait_bbp_ready(struct mt7601u_dev *dev);
void mt7601u_set_rx_path(struct mt7601u_dev *dev, u8 path);
void mt7601u_set_tx_dac(struct mt7601u_dev *dev, u8 path);
int mt7601u_bbp_set_bw(struct mt7601u_dev *dev, int bw);
void mt7601u_agc_save(struct mt7601u_dev *dev);
void mt7601u_agc_restore(struct mt7601u_dev *dev);
int mt7601u_phy_set_channel(struct mt7601u_dev *dev,
			    struct cfg80211_chan_def *chandef);
void mt7601u_phy_recalibrate_after_assoc(struct mt7601u_dev *dev);
int mt7601u_phy_get_rssi(struct mt7601u_dev *dev,
			 struct mt7601u_rxwi *rxwi, u16 rate);
void mt7601u_phy_con_cal_onoff(struct mt7601u_dev *dev,
			       struct ieee80211_bss_conf *info);

/* MAC */
void mt7601u_mac_work(struct work_struct *work);
void mt7601u_mac_set_protection(struct mt7601u_dev *dev, bool legacy_prot,
				int ht_mode);
void mt7601u_mac_set_short_preamble(struct mt7601u_dev *dev, bool short_preamb);
void mt7601u_mac_config_tsf(struct mt7601u_dev *dev, bool enable, int interval);
void
mt7601u_mac_wcid_setup(struct mt7601u_dev *dev, u8 idx, u8 vif_idx, u8 *mac);
void mt7601u_mac_set_ampdu_factor(struct mt7601u_dev *dev);

/* TX */
void mt7601u_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		struct sk_buff *skb);
int mt7601u_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    u16 queue, const struct ieee80211_tx_queue_params *params);
void mt7601u_tx_status(struct mt7601u_dev *dev, struct sk_buff *skb);
void mt7601u_tx_stat(struct work_struct *work);

/* util */
void mt76_remove_hdr_pad(struct sk_buff *skb);
int mt76_insert_hdr_pad(struct sk_buff *skb);

u32 mt7601u_bbp_set_ctrlch(struct mt7601u_dev *dev, bool below);

static inline u32 mt7601u_mac_set_ctrlch(struct mt7601u_dev *dev, bool below)
{
	return mt7601u_rmc(dev, MT_TX_BAND_CFG, 1, below);
}

int mt7601u_dma_init(struct mt7601u_dev *dev);
void mt7601u_dma_cleanup(struct mt7601u_dev *dev);

int mt7601u_dma_enqueue_tx(struct mt7601u_dev *dev, struct sk_buff *skb,
			   struct mt76_wcid *wcid, int hw_q);

#endif
