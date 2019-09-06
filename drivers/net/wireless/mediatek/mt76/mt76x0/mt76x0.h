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
#include "regs.h"

#define MT_CALIBRATE_INTERVAL		(4 * HZ)

#define MT_FREQ_CAL_INIT_DELAY		(30 * HZ)
#define MT_FREQ_CAL_CHECK_INTERVAL	(10 * HZ)
#define MT_FREQ_CAL_ADJ_INTERVAL	(HZ / 2)

#define MT_BBP_REG_VERSION		0x00

#define MT_USB_AGGR_SIZE_LIMIT		21 /* * 1024B */
#define MT_USB_AGGR_TIMEOUT		0x80 /* * 33ns */
#define MT_RX_ORDER			3
#define MT_RX_URB_SIZE			(PAGE_SIZE << MT_RX_ORDER)

struct mt76x0_dma_buf {
	struct urb *urb;
	void *buf;
	dma_addr_t dma;
	size_t len;
};

struct mt76x0_mcu {
	struct mutex mutex;

	u8 msg_seq;

	struct mt76x0_dma_buf resp;
	struct completion resp_cmpl;

	struct mt76_reg_pair *reg_pairs;
	unsigned int reg_pairs_len;
	u32 reg_base;
	bool burst_read;
};

struct mac_stats {
	u64 rx_stat[6];
	u64 tx_stat[6];
	u64 aggr_stat[2];
	u64 aggr_n[32];
	u64 zero_len_del[2];
};

#define N_RX_ENTRIES	16
struct mt76x0_rx_queue {
	struct mt76x0_dev *dev;

	struct mt76x0_dma_buf_rx {
		struct urb *urb;
		struct page *p;
	} e[N_RX_ENTRIES];

	unsigned int start;
	unsigned int end;
	unsigned int entries;
	unsigned int pending;
};

#define N_TX_ENTRIES	64

struct mt76x0_tx_queue {
	struct mt76x0_dev *dev;

	struct mt76x0_dma_buf_tx {
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
#define GROUP_WCID(idx)	(254 - idx)

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
 * @tx_lock:		protects @tx_q and changes of MT76_STATE_*_STATS
 *			flags in @state.
 * @rx_lock:		protects @rx_q.
 * @con_mon_lock:	protects @ap_bssid, @bcn_*, @avg_rssi.
 * @mutex:		ensures exclusive access from mac80211 callbacks.
 * @reg_atomic_mutex:	ensures atomicity of indirect register accesses
 *			(accesses to RF and BBP).
 * @hw_atomic_mutex:	ensures exclusive access to HW during critical
 *			operations (power management, channel switch).
 */
struct mt76x0_dev {
	struct mt76_dev mt76; /* must be first */

	struct mutex mutex;

	struct mutex usb_ctrl_mtx;
	u8 data[32];

	struct tasklet_struct rx_tasklet;
	struct tasklet_struct tx_tasklet;

	u8 out_ep[__MT_EP_OUT_MAX];
	u16 out_max_packet;
	u8 in_ep[__MT_EP_IN_MAX];
	u16 in_max_packet;

	unsigned long wcid_mask[DIV_ROUND_UP(N_WCIDS, BITS_PER_LONG)];
	unsigned long vif_mask;

	struct mt76x0_mcu mcu;

	struct delayed_work cal_work;
	struct delayed_work mac_work;

	struct workqueue_struct *stat_wq;
	struct delayed_work stat_work;

	struct mt76_wcid *mon_wcid;
	struct mt76_wcid __rcu *wcid[N_WCIDS];

	spinlock_t mac_lock;

	const u16 *beacon_offsets;

	u8 macaddr[ETH_ALEN];
	struct mt76x0_eeprom_params *ee;

	struct mutex reg_atomic_mutex;
	struct mutex hw_atomic_mutex;

	u32 rxfilter;
	u32 debugfs_reg;

	/* TX */
	spinlock_t tx_lock;
	struct mt76x0_tx_queue *tx_q;
	struct sk_buff_head tx_skb_done;

	atomic_t avg_ampdu_len;

	/* RX */
	spinlock_t rx_lock;
	struct mt76x0_rx_queue rx_q;

	/* Connection monitoring things */
	spinlock_t con_mon_lock;
	u8 ap_bssid[ETH_ALEN];

	s8 bcn_freq_off;
	u8 bcn_phy_mode;

	int avg_rssi; /* starts at 0 and converges */

	u8 agc_save;
	u16 chainmask;

	struct mac_stats stats;
};

struct mt76x0_wcid {
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

struct mt76_tx_status {
	u8 valid:1;
	u8 success:1;
	u8 aggr:1;
	u8 ack_req:1;
	u8 is_probe:1;
	u8 wcid;
	u8 pktid;
	u8 retry;
	u16 rate;
} __packed __aligned(2);

struct mt76_sta {
	struct mt76_wcid wcid;
	struct mt76_tx_status status;
	int n_frames;
	u16 agg_ssn[IEEE80211_NUM_TIDS];
};

struct mt76_reg_pair {
	u32 reg;
	u32 value;
};

struct mt76x0_rxwi;

extern const struct ieee80211_ops mt76x0_ops;

static inline bool is_mt7610e(struct mt76x0_dev *dev)
{
	/* TODO */
	return false;
}

void mt76x0_init_debugfs(struct mt76x0_dev *dev);

int mt76x0_wait_asic_ready(struct mt76x0_dev *dev);

/* Compatibility with mt76 */
#define mt76_rmw_field(_dev, _reg, _field, _val)	\
	mt76_rmw(_dev, _reg, _field, FIELD_PREP(_field, _val))

int mt76x0_write_reg_pairs(struct mt76x0_dev *dev, u32 base,
			    const struct mt76_reg_pair *data, int len);
int mt76x0_read_reg_pairs(struct mt76x0_dev *dev, u32 base,
			  struct mt76_reg_pair *data, int len);
int mt76x0_burst_write_regs(struct mt76x0_dev *dev, u32 offset,
			     const u32 *data, int n);
void mt76x0_addr_wr(struct mt76x0_dev *dev, const u32 offset, const u8 *addr);

/* Init */
struct mt76x0_dev *mt76x0_alloc_device(struct device *dev);
int mt76x0_init_hardware(struct mt76x0_dev *dev, bool reset);
int mt76x0_register_device(struct mt76x0_dev *dev);
void mt76x0_cleanup(struct mt76x0_dev *dev);
void mt76x0_chip_onoff(struct mt76x0_dev *dev, bool enable, bool reset);

int mt76x0_mac_start(struct mt76x0_dev *dev);
void mt76x0_mac_stop(struct mt76x0_dev *dev);

/* PHY */
void mt76x0_phy_init(struct mt76x0_dev *dev);
int mt76x0_wait_bbp_ready(struct mt76x0_dev *dev);
void mt76x0_agc_save(struct mt76x0_dev *dev);
void mt76x0_agc_restore(struct mt76x0_dev *dev);
int mt76x0_phy_set_channel(struct mt76x0_dev *dev,
			    struct cfg80211_chan_def *chandef);
void mt76x0_phy_recalibrate_after_assoc(struct mt76x0_dev *dev);
int mt76x0_phy_get_rssi(struct mt76x0_dev *dev, struct mt76x0_rxwi *rxwi);
void mt76x0_phy_con_cal_onoff(struct mt76x0_dev *dev,
			       struct ieee80211_bss_conf *info);

/* MAC */
void mt76x0_mac_work(struct work_struct *work);
void mt76x0_mac_set_protection(struct mt76x0_dev *dev, bool legacy_prot,
				int ht_mode);
void mt76x0_mac_set_short_preamble(struct mt76x0_dev *dev, bool short_preamb);
void mt76x0_mac_config_tsf(struct mt76x0_dev *dev, bool enable, int interval);
void
mt76x0_mac_wcid_setup(struct mt76x0_dev *dev, u8 idx, u8 vif_idx, u8 *mac);
void mt76x0_mac_set_ampdu_factor(struct mt76x0_dev *dev);

/* TX */
void mt76x0_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		struct sk_buff *skb);
int mt76x0_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    u16 queue, const struct ieee80211_tx_queue_params *params);
void mt76x0_tx_status(struct mt76x0_dev *dev, struct sk_buff *skb);
void mt76x0_tx_stat(struct work_struct *work);

/* util */
void mt76x0_remove_hdr_pad(struct sk_buff *skb);
int mt76x0_insert_hdr_pad(struct sk_buff *skb);

int mt76x0_dma_init(struct mt76x0_dev *dev);
void mt76x0_dma_cleanup(struct mt76x0_dev *dev);

int mt76x0_dma_enqueue_tx(struct mt76x0_dev *dev, struct sk_buff *skb,
			   struct mt76_wcid *wcid, int hw_q);

#endif
