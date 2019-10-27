/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2019 MediaTek Inc. */

#ifndef __MT7615_H
#define __MT7615_H

#include <linux/interrupt.h>
#include <linux/ktime.h>
#include "../mt76.h"
#include "regs.h"

#define MT7615_MAX_INTERFACES		4
#define MT7615_MAX_WMM_SETS		4
#define MT7615_WTBL_SIZE		128
#define MT7615_WTBL_RESERVED		(MT7615_WTBL_SIZE - 1)
#define MT7615_WTBL_STA			(MT7615_WTBL_RESERVED - \
					 MT7615_MAX_INTERFACES)

#define MT7615_WATCHDOG_TIME		(HZ / 10)
#define MT7615_RATE_RETRY		2

#define MT7615_TX_RING_SIZE		1024
#define MT7615_TX_MCU_RING_SIZE		128
#define MT7615_TX_FWDL_RING_SIZE	128

#define MT7615_RX_RING_SIZE		1024
#define MT7615_RX_MCU_RING_SIZE		512

#define MT7615_FIRMWARE_CR4		"mediatek/mt7615_cr4.bin"
#define MT7615_FIRMWARE_N9		"mediatek/mt7615_n9.bin"
#define MT7615_ROM_PATCH		"mediatek/mt7615_rom_patch.bin"

#define MT7615_EEPROM_SIZE		1024
#define MT7615_TOKEN_SIZE		4096

#define MT_FRAC_SCALE		12
#define MT_FRAC(val, div)	(((val) << MT_FRAC_SCALE) / (div))

struct mt7615_vif;
struct mt7615_sta;

enum mt7615_hw_txq_id {
	MT7615_TXQ_MAIN,
	MT7615_TXQ_EXT,
	MT7615_TXQ_MCU,
	MT7615_TXQ_FWDL,
};

struct mt7615_rate_set {
	struct ieee80211_tx_rate probe_rate;
	struct ieee80211_tx_rate rates[4];
};

struct mt7615_sta {
	struct mt76_wcid wcid; /* must be first */

	struct mt7615_vif *vif;

	struct ieee80211_tx_rate rates[4];

	struct mt7615_rate_set rateset[2];
	u32 rate_set_tsf;

	u8 rate_count;
	u8 n_rates;

	u8 rate_probe;
};

struct mt7615_vif {
	u8 idx;
	u8 omac_idx;
	u8 band_idx;
	u8 wmm_idx;

	struct mt7615_sta sta;
};

struct mt7615_dev {
	struct mt76_dev mt76; /* must be first */
	u32 vif_mask;
	u32 omac_mask;

	struct {
		u8 n_pulses;
		u32 period;
		u16 width;
		s16 power;
	} radar_pattern;
	u32 hw_pattern;
	int dfs_state;

	int false_cca_ofdm, false_cca_cck;
	unsigned long last_cca_adj;
	u8 mac_work_count;
	s8 ofdm_sensitivity;
	s8 cck_sensitivity;
	bool scs_en;

	spinlock_t token_lock;
	struct idr token;
};

enum {
	HW_BSSID_0 = 0x0,
	HW_BSSID_1,
	HW_BSSID_2,
	HW_BSSID_3,
	HW_BSSID_MAX,
	EXT_BSSID_START = 0x10,
	EXT_BSSID_1,
	EXT_BSSID_2,
	EXT_BSSID_3,
	EXT_BSSID_4,
	EXT_BSSID_5,
	EXT_BSSID_6,
	EXT_BSSID_7,
	EXT_BSSID_8,
	EXT_BSSID_9,
	EXT_BSSID_10,
	EXT_BSSID_11,
	EXT_BSSID_12,
	EXT_BSSID_13,
	EXT_BSSID_14,
	EXT_BSSID_15,
	EXT_BSSID_END
};

enum {
	MT_HW_RDD0,
	MT_HW_RDD1,
};

enum {
	MT_RX_SEL0,
	MT_RX_SEL1,
};

enum mt7615_rdd_cmd {
	RDD_STOP,
	RDD_START,
	RDD_DET_MODE,
	RDD_DET_STOP,
	RDD_CAC_START,
	RDD_CAC_END,
	RDD_NORMAL_START,
	RDD_DISABLE_DFS_CAL,
	RDD_PULSE_DBG,
	RDD_READ_PULSE,
	RDD_RESUME_BF,
};

extern const struct ieee80211_ops mt7615_ops;
extern struct pci_driver mt7615_pci_driver;

u32 mt7615_reg_map(struct mt7615_dev *dev, u32 addr);

int mt7615_register_device(struct mt7615_dev *dev);
void mt7615_unregister_device(struct mt7615_dev *dev);
int mt7615_eeprom_init(struct mt7615_dev *dev);
int mt7615_eeprom_get_power_index(struct mt7615_dev *dev,
				  struct ieee80211_channel *chan,
				  u8 chain_idx);
int mt7615_dma_init(struct mt7615_dev *dev);
void mt7615_dma_cleanup(struct mt7615_dev *dev);
int mt7615_mcu_init(struct mt7615_dev *dev);
int mt7615_mcu_set_dev_info(struct mt7615_dev *dev,
			    struct ieee80211_vif *vif, bool enable);
int mt7615_mcu_set_bss_info(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			    int en);
void mt7615_mac_set_rates(struct mt7615_dev *dev, struct mt7615_sta *sta,
			  struct ieee80211_tx_rate *probe_rate,
			  struct ieee80211_tx_rate *rates);
int mt7615_mcu_wtbl_bmc(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			bool enable);
int mt7615_mcu_add_wtbl(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta);
int mt7615_mcu_del_wtbl(struct mt7615_dev *dev, struct ieee80211_sta *sta);
int mt7615_mcu_del_wtbl_all(struct mt7615_dev *dev);
int mt7615_mcu_set_sta_rec_bmc(struct mt7615_dev *dev,
			       struct ieee80211_vif *vif, bool en);
int mt7615_mcu_set_sta_rec(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta, bool en);
int mt7615_mcu_set_bcn(struct mt7615_dev *dev, struct ieee80211_vif *vif,
		       int en);
int mt7615_mcu_set_channel(struct mt7615_dev *dev);
int mt7615_mcu_set_wmm(struct mt7615_dev *dev, u8 queue,
		       const struct ieee80211_tx_queue_params *params);
int mt7615_mcu_set_tx_ba(struct mt7615_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add);
int mt7615_mcu_set_rx_ba(struct mt7615_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add);
int mt7615_mcu_set_ht_cap(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta);
void mt7615_mcu_rx_event(struct mt7615_dev *dev, struct sk_buff *skb);
int mt7615_mcu_rdd_cmd(struct mt7615_dev *dev,
		       enum mt7615_rdd_cmd cmd, u8 index,
		       u8 rx_sel, u8 val);
int mt7615_dfs_start_radar_detector(struct mt7615_dev *dev);
int mt7615_dfs_stop_radar_detector(struct mt7615_dev *dev);
int mt7615_mcu_rdd_send_pattern(struct mt7615_dev *dev);

static inline bool is_mt7622(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7622;
}

static inline void mt7615_dfs_check_channel(struct mt7615_dev *dev)
{
	enum nl80211_chan_width width = dev->mt76.chandef.width;
	u32 freq = dev->mt76.chandef.chan->center_freq;
	struct ieee80211_hw *hw = mt76_hw(dev);

	if (hw->conf.chandef.chan->center_freq != freq ||
	    hw->conf.chandef.width != width)
		dev->dfs_state = -1;
}

static inline void mt7615_irq_enable(struct mt7615_dev *dev, u32 mask)
{
	mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, 0, mask);
}

static inline void mt7615_irq_disable(struct mt7615_dev *dev, u32 mask)
{
	mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, mask, 0);
}

void mt7615_update_channel(struct mt76_dev *mdev);
void mt7615_mac_cca_stats_reset(struct mt7615_dev *dev);
void mt7615_mac_set_scs(struct mt7615_dev *dev, bool enable);
int mt7615_mac_write_txwi(struct mt7615_dev *dev, __le32 *txwi,
			  struct sk_buff *skb, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta, int pid,
			  struct ieee80211_key_conf *key);
int mt7615_mac_fill_rx(struct mt7615_dev *dev, struct sk_buff *skb);
void mt7615_mac_add_txs(struct mt7615_dev *dev, void *data);
void mt7615_mac_tx_free(struct mt7615_dev *dev, struct sk_buff *skb);
int mt7615_mac_wtbl_set_key(struct mt7615_dev *dev, struct mt76_wcid *wcid,
			    struct ieee80211_key_conf *key,
			    enum set_key_cmd cmd);

int mt7615_mcu_set_eeprom(struct mt7615_dev *dev);
int mt7615_mcu_init_mac(struct mt7615_dev *dev);
int mt7615_mcu_set_rts_thresh(struct mt7615_dev *dev, u32 val);
int mt7615_mcu_ctrl_pm_state(struct mt7615_dev *dev, int enter);
int mt7615_mcu_get_temperature(struct mt7615_dev *dev, int index);
int mt7615_mcu_set_tx_power(struct mt7615_dev *dev);
void mt7615_mcu_exit(struct mt7615_dev *dev);

int mt7615_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info);

void mt7615_tx_complete_skb(struct mt76_dev *mdev, enum mt76_txq_id qid,
			    struct mt76_queue_entry *e);

void mt7615_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb);
void mt7615_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps);
int mt7615_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta);
void mt7615_sta_assoc(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta);
void mt7615_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
void mt7615_mac_work(struct work_struct *work);
void mt7615_txp_skb_unmap(struct mt76_dev *dev,
			  struct mt76_txwi_cache *txwi);
int mt76_dfs_start_rdd(struct mt7615_dev *dev, bool force);
int mt7615_dfs_init_radar_detector(struct mt7615_dev *dev);

int mt7615_init_debugfs(struct mt7615_dev *dev);

#endif
