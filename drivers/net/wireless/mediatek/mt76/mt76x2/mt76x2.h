/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __MT76x2_H
#define __MT76x2_H

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/kfifo.h>

#define MT7662_FIRMWARE		"mt7662.bin"
#define MT7662_ROM_PATCH	"mt7662_rom_patch.bin"
#define MT7662_EEPROM_SIZE	512

#define MT7662U_FIRMWARE	"mediatek/mt7662u.bin"
#define MT7662U_ROM_PATCH	"mediatek/mt7662u_rom_patch.bin"

#define MT_MAX_CHAINS		2

#define MT_CALIBRATE_INTERVAL	HZ

#include "../mt76.h"
#include "../mt76x02_regs.h"
#include "mac.h"
#include "dfs.h"

struct mt76x2_rx_freq_cal {
	s8 high_gain[MT_MAX_CHAINS];
	s8 rssi_offset[MT_MAX_CHAINS];
	s8 lna_gain;
	u32 mcu_gain;
};

struct mt76x2_calibration {
	struct mt76x2_rx_freq_cal rx;

	u8 agc_gain_init[MT_MAX_CHAINS];
	u8 agc_gain_cur[MT_MAX_CHAINS];

	u16 false_cca;
	s8 avg_rssi_all;
	s8 agc_gain_adjust;
	s8 low_gain;

	u8 temp;

	bool init_cal_done;
	bool tssi_cal_done;
	bool tssi_comp_pending;
	bool dpd_cal_done;
	bool channel_cal_done;
};

struct mt76x2_dev {
	struct mt76_dev mt76; /* must be first */

	struct mac_address macaddr_list[8];

	struct mutex mutex;

	u8 txdone_seq;
	DECLARE_KFIFO_PTR(txstatus_fifo, struct mt76x02_tx_status);

	struct sk_buff *rx_head;

	struct tasklet_struct tx_tasklet;
	struct tasklet_struct pre_tbtt_tasklet;
	struct delayed_work cal_work;
	struct delayed_work mac_work;

	u32 aggr_stats[32];

	struct sk_buff *beacons[8];
	u8 beacon_mask;
	u8 beacon_data_mask;

	u8 tbtt_count;
	u16 beacon_int;

	struct mt76x2_calibration cal;

	s8 target_power;
	s8 target_power_delta[2];
	bool enable_tpc;

	u8 coverage_class;
	u8 slottime;

	struct mt76x2_dfs_pattern_detector dfs_pd;
};

static inline bool is_mt7612(struct mt76x2_dev *dev)
{
	return mt76_chip(&dev->mt76) == 0x7612;
}

static inline bool mt76x2_channel_silent(struct mt76x2_dev *dev)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;

	return ((chan->flags & IEEE80211_CHAN_RADAR) &&
		chan->dfs_state != NL80211_DFS_AVAILABLE);
}

extern const struct ieee80211_ops mt76x2_ops;

struct mt76x2_dev *mt76x2_alloc_device(struct device *pdev);
int mt76x2_register_device(struct mt76x2_dev *dev);
void mt76x2_init_debugfs(struct mt76x2_dev *dev);
void mt76x2_init_device(struct mt76x2_dev *dev);

irqreturn_t mt76x2_irq_handler(int irq, void *dev_instance);
void mt76x2_phy_power_on(struct mt76x2_dev *dev);
int mt76x2_init_hardware(struct mt76x2_dev *dev);
void mt76x2_stop_hardware(struct mt76x2_dev *dev);
int mt76x2_eeprom_init(struct mt76x2_dev *dev);
int mt76x2_apply_calibration_data(struct mt76x2_dev *dev, int channel);
void mt76x2_set_tx_ackto(struct mt76x2_dev *dev);

void mt76x2_phy_set_antenna(struct mt76x2_dev *dev);
int mt76x2_phy_start(struct mt76x2_dev *dev);
int mt76x2_phy_set_channel(struct mt76x2_dev *dev,
			 struct cfg80211_chan_def *chandef);
int mt76x2_mac_get_rssi(struct mt76x2_dev *dev, s8 rssi, int chain);
void mt76x2_phy_calibrate(struct work_struct *work);
void mt76x2_phy_set_txpower(struct mt76x2_dev *dev);

int mt76x2_mcu_init(struct mt76x2_dev *dev);
int mt76x2_mcu_set_channel(struct mt76x2_dev *dev, u8 channel, u8 bw,
			   u8 bw_index, bool scan);
int mt76x2_mcu_load_cr(struct mt76x2_dev *dev, u8 type, u8 temp_level,
		       u8 channel);

void mt76x2_tx_tasklet(unsigned long data);
void mt76x2_dma_cleanup(struct mt76x2_dev *dev);

void mt76x2_cleanup(struct mt76x2_dev *dev);

void mt76x2_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
	       struct sk_buff *skb);
int mt76x2_tx_prepare_skb(struct mt76_dev *mdev, void *txwi,
			  struct sk_buff *skb, struct mt76_queue *q,
			  struct mt76_wcid *wcid, struct ieee80211_sta *sta,
			  u32 *tx_info);
void mt76x2_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue *q,
			    struct mt76_queue_entry *e, bool flush);
void mt76x2_mac_set_tx_protection(struct mt76x2_dev *dev, u32 val);

void mt76x2_pre_tbtt_tasklet(unsigned long arg);

void mt76x2_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q);
void mt76x2_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb);

void mt76x2_sta_ps(struct mt76_dev *dev, struct ieee80211_sta *sta, bool ps);

void mt76x2_update_channel(struct mt76_dev *mdev);

s8 mt76x2_tx_get_txpwr_adj(struct mt76_dev *mdev, s8 txpwr, s8 max_txpwr_adj);
void mt76x2_tx_set_txpwr_auto(struct mt76x2_dev *dev, s8 txpwr);


void mt76x2_reset_wlan(struct mt76x2_dev *dev, bool enable);
void mt76x2_init_txpower(struct mt76x2_dev *dev,
			 struct ieee80211_supported_band *sband);
void mt76_write_mac_initvals(struct mt76x2_dev *dev);

int mt76x2_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta);
int mt76x2_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta);
void mt76x2_remove_interface(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif);
int mt76x2_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   u16 queue, const struct ieee80211_tx_queue_params *params);
void mt76x2_txq_init(struct mt76x2_dev *dev, struct ieee80211_txq *txq);

void mt76x2_phy_tssi_compensate(struct mt76x2_dev *dev, bool wait);
void mt76x2_phy_set_txpower_regs(struct mt76x2_dev *dev,
				 enum nl80211_band band);
void mt76x2_configure_tx_delay(struct mt76x2_dev *dev,
			       enum nl80211_band band, u8 bw);
void mt76x2_phy_set_bw(struct mt76x2_dev *dev, int width, u8 ctrl);
void mt76x2_phy_set_band(struct mt76x2_dev *dev, int band, bool primary_upper);
int mt76x2_phy_get_min_avg_rssi(struct mt76x2_dev *dev);
void mt76x2_apply_gain_adj(struct mt76x2_dev *dev);

#endif
