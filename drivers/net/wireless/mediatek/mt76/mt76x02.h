/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 */

#ifndef __MT76x02_H
#define __MT76x02_H

#include <linux/kfifo.h>

#include "mt76.h"
#include "mt76x02_regs.h"
#include "mt76x02_mac.h"
#include "mt76x02_dfs.h"
#include "mt76x02_dma.h"

#define MT76x02_TX_RING_SIZE	512
#define MT76x02_PSD_RING_SIZE	128
#define MT76x02_N_WCIDS 128
#define MT_CALIBRATE_INTERVAL	HZ
#define MT_MAC_WORK_INTERVAL	(HZ / 10)

#define MT_WATCHDOG_TIME	(HZ / 10)
#define MT_TX_HANG_TH		10

#define MT_MAX_CHAINS		2
struct mt76x02_rx_freq_cal {
	s8 high_gain[MT_MAX_CHAINS];
	s8 rssi_offset[MT_MAX_CHAINS];
	s8 lna_gain;
	u32 mcu_gain;
	s16 temp_offset;
	u8 freq_offset;
};

struct mt76x02_calibration {
	struct mt76x02_rx_freq_cal rx;

	u8 agc_gain_init[MT_MAX_CHAINS];
	u8 agc_gain_cur[MT_MAX_CHAINS];

	u16 false_cca;
	s8 avg_rssi_all;
	s8 agc_gain_adjust;
	s8 agc_lowest_gain;
	s8 low_gain;

	s8 temp_vco;
	s8 temp;

	bool init_cal_done;
	bool tssi_cal_done;
	bool tssi_comp_pending;
	bool dpd_cal_done;
	bool channel_cal_done;
	bool gain_init_done;

	int tssi_target;
	s8 tssi_dc;
};

struct mt76x02_beacon_ops {
	unsigned int nslots;
	unsigned int slot_size;
	void (*pre_tbtt_enable)(struct mt76x02_dev *dev, bool en);
	void (*beacon_enable)(struct mt76x02_dev *dev, bool en);
};

#define mt76x02_beacon_enable(dev, enable)	\
	(dev)->beacon_ops->beacon_enable(dev, enable)
#define mt76x02_pre_tbtt_enable(dev, enable)	\
	(dev)->beacon_ops->pre_tbtt_enable(dev, enable)

struct mt76x02_dev {
	union { /* must be first */
		struct mt76_dev mt76;
		struct mt76_phy mphy;
	};

	struct mac_address macaddr_list[8];

	struct mutex phy_mutex;

	u8 txdone_seq;
	DECLARE_KFIFO_PTR(txstatus_fifo, struct mt76x02_tx_status);
	spinlock_t txstatus_fifo_lock;
	u32 tx_airtime;
	u32 ampdu_ref;

	struct sk_buff *rx_head;

	struct delayed_work cal_work;
	struct delayed_work wdt_work;

	struct hrtimer pre_tbtt_timer;
	struct work_struct pre_tbtt_work;

	const struct mt76x02_beacon_ops *beacon_ops;

	u8 beacon_data_count;

	u8 tbtt_count;

	u32 tx_hang_reset;
	u8 tx_hang_check[4];
	u8 beacon_hang_check;
	u8 mcu_timeout;

	struct mt76x02_calibration cal;

	int txpower_conf;
	s8 target_power;
	s8 target_power_delta[2];
	bool enable_tpc;

	bool no_2ghz;

	s16 coverage_class;
	u8 slottime;

	struct mt76x02_dfs_pattern_detector dfs_pd;

	/* edcca monitor */
	unsigned long ed_trigger_timeout;
	bool ed_tx_blocked;
	bool ed_monitor;
	u8 ed_monitor_enabled;
	u8 ed_monitor_learning;
	u8 ed_trigger;
	u8 ed_silent;
	ktime_t ed_time;
};

extern struct ieee80211_rate mt76x02_rates[12];

int mt76x02_init_device(struct mt76x02_dev *dev);
void mt76x02_configure_filter(struct ieee80211_hw *hw,
			      unsigned int changed_flags,
			      unsigned int *total_flags, u64 multicast);
int mt76x02_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta);
void mt76x02_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta);

void mt76x02_config_mac_addr_list(struct mt76x02_dev *dev);

int mt76x02_add_interface(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif);
void mt76x02_remove_interface(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif);

int mt76x02_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_ampdu_params *params);
int mt76x02_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		    struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		    struct ieee80211_key_conf *key);
int mt76x02_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    u16 queue, const struct ieee80211_tx_queue_params *params);
void mt76x02_sta_rate_tbl_update(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta);
s8 mt76x02_tx_get_max_txpwr_adj(struct mt76x02_dev *dev,
				const struct ieee80211_tx_rate *rate);
s8 mt76x02_tx_get_txpwr_adj(struct mt76x02_dev *dev, s8 txpwr,
			    s8 max_txpwr_adj);
void mt76x02_wdt_work(struct work_struct *work);
void mt76x02_tx_set_txpwr_auto(struct mt76x02_dev *dev, s8 txpwr);
void mt76x02_set_tx_ackto(struct mt76x02_dev *dev);
void mt76x02_set_coverage_class(struct ieee80211_hw *hw,
				s16 coverage_class);
int mt76x02_set_rts_threshold(struct ieee80211_hw *hw, u32 val);
void mt76x02_remove_hdr_pad(struct sk_buff *skb, int len);
bool mt76x02_tx_status_data(struct mt76_dev *mdev, u8 *update);
void mt76x02_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			  struct sk_buff *skb);
void mt76x02_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q);
irqreturn_t mt76x02_irq_handler(int irq, void *dev_instance);
void mt76x02_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		struct sk_buff *skb);
int mt76x02_tx_prepare_skb(struct mt76_dev *mdev, void *txwi,
			   enum mt76_txq_id qid, struct mt76_wcid *wcid,
			   struct ieee80211_sta *sta,
			   struct mt76_tx_info *tx_info);
void mt76x02_sw_scan_complete(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif);
void mt76x02_sta_ps(struct mt76_dev *dev, struct ieee80211_sta *sta, bool ps);
void mt76x02_bss_info_changed(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *info, u32 changed);
void mt76x02_reconfig_complete(struct ieee80211_hw *hw,
			       enum ieee80211_reconfig_type reconfig_type);

struct beacon_bc_data {
	struct mt76x02_dev *dev;
	struct sk_buff_head q;
	struct sk_buff *tail[8];
};

void mt76x02_init_beacon_config(struct mt76x02_dev *dev);
void mt76x02e_init_beacon_config(struct mt76x02_dev *dev);
void mt76x02_resync_beacon_timer(struct mt76x02_dev *dev);
void mt76x02_update_beacon_iter(void *priv, u8 *mac, struct ieee80211_vif *vif);
void mt76x02_enqueue_buffered_bc(struct mt76x02_dev *dev,
				 struct beacon_bc_data *data,
				 int max_nframes);

void mt76x02_mac_start(struct mt76x02_dev *dev);

void mt76x02_init_debugfs(struct mt76x02_dev *dev);

static inline bool is_mt76x0(struct mt76x02_dev *dev)
{
	return mt76_chip(&dev->mt76) == 0x7610 ||
	       mt76_chip(&dev->mt76) == 0x7630 ||
	       mt76_chip(&dev->mt76) == 0x7650;
}

static inline bool is_mt76x2(struct mt76x02_dev *dev)
{
	return mt76_chip(&dev->mt76) == 0x7612 ||
	       mt76_chip(&dev->mt76) == 0x7632 ||
	       mt76_chip(&dev->mt76) == 0x7662 ||
	       mt76_chip(&dev->mt76) == 0x7602;
}

static inline void mt76x02_irq_enable(struct mt76x02_dev *dev, u32 mask)
{
	mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, 0, mask);
}

static inline void mt76x02_irq_disable(struct mt76x02_dev *dev, u32 mask)
{
	mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, mask, 0);
}

static inline bool
mt76x02_wait_for_txrx_idle(struct mt76_dev *dev)
{
	return __mt76_poll_msec(dev, MT_MAC_STATUS,
				MT_MAC_STATUS_TX | MT_MAC_STATUS_RX,
				0, 100);
}

static inline struct mt76x02_sta *
mt76x02_rx_get_sta(struct mt76_dev *dev, u8 idx)
{
	struct mt76_wcid *wcid;

	if (idx >= MT76x02_N_WCIDS)
		return NULL;

	wcid = rcu_dereference(dev->wcid[idx]);
	if (!wcid)
		return NULL;

	return container_of(wcid, struct mt76x02_sta, wcid);
}

static inline struct mt76_wcid *
mt76x02_rx_get_sta_wcid(struct mt76x02_sta *sta, bool unicast)
{
	if (!sta)
		return NULL;

	if (unicast)
		return &sta->wcid;
	else
		return &sta->vif->group_wcid;
}

#endif /* __MT76x02_H */
