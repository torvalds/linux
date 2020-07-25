/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT7915_H
#define __MT7915_H

#include <linux/interrupt.h>
#include <linux/ktime.h>
#include "../mt76.h"
#include "regs.h"

#define MT7915_MAX_INTERFACES		4
#define MT7915_MAX_WMM_SETS		4
#define MT7915_WTBL_SIZE		288
#define MT7915_WTBL_RESERVED		(MT7915_WTBL_SIZE - 1)
#define MT7915_WTBL_STA			(MT7915_WTBL_RESERVED - \
					 MT7915_MAX_INTERFACES)

#define MT7915_WATCHDOG_TIME		(HZ / 10)
#define MT7915_RESET_TIMEOUT		(30 * HZ)

#define MT7915_TX_RING_SIZE		2048
#define MT7915_TX_MCU_RING_SIZE		256
#define MT7915_TX_FWDL_RING_SIZE	128

#define MT7915_RX_RING_SIZE		1536
#define MT7915_RX_MCU_RING_SIZE		512

#define MT7915_FIRMWARE_WA		"mediatek/mt7915_wa.bin"
#define MT7915_FIRMWARE_WM		"mediatek/mt7915_wm.bin"
#define MT7915_ROM_PATCH		"mediatek/mt7915_rom_patch.bin"

#define MT7915_EEPROM_SIZE		3584
#define MT7915_TOKEN_SIZE		8192

#define MT7915_CFEND_RATE_DEFAULT	0x49	/* OFDM 24M */
#define MT7915_CFEND_RATE_11B		0x03	/* 11B LP, 11M */
#define MT7915_5G_RATE_DEFAULT		0x4b	/* OFDM 6M */
#define MT7915_2G_RATE_DEFAULT		0x0	/* CCK 1M */

#define MT7915_SKU_RATE_NUM		161
#define MT7915_SKU_MAX_DELTA_IDX	MT7915_SKU_RATE_NUM
#define MT7915_SKU_TABLE_SIZE		(MT7915_SKU_RATE_NUM + 1)

struct mt7915_vif;
struct mt7915_sta;
struct mt7915_dfs_pulse;
struct mt7915_dfs_pattern;

enum mt7915_txq_id {
	MT7915_TXQ_FWDL = 16,
	MT7915_TXQ_MCU_WM,
	MT7915_TXQ_BAND0,
	MT7915_TXQ_BAND1,
	MT7915_TXQ_MCU_WA,
};

enum mt7915_rxq_id {
	MT7915_RXQ_BAND0 = 0,
	MT7915_RXQ_BAND1,
	MT7915_RXQ_MCU_WM = 0,
	MT7915_RXQ_MCU_WA,
};

struct mt7915_sta_stats {
	struct rate_info prob_rate;
	struct rate_info tx_rate;

	unsigned long per;
	unsigned long changed;
	unsigned long jiffies;
};

struct mt7915_sta {
	struct mt76_wcid wcid; /* must be first */

	struct mt7915_vif *vif;

	struct list_head stats_list;
	struct list_head poll_list;
	struct list_head rc_list;
	u32 airtime_ac[8];

	struct mt7915_sta_stats stats;

	unsigned long ampdu_state;
};

struct mt7915_vif {
	u16 idx;
	u8 omac_idx;
	u8 band_idx;
	u8 wmm_idx;

	struct mt7915_sta sta;
	struct mt7915_phy *phy;

	struct ieee80211_tx_queue_params queue_params[IEEE80211_NUM_ACS];
};

struct mib_stats {
	u16 ack_fail_cnt;
	u16 fcs_err_cnt;
	u16 rts_cnt;
	u16 rts_retries_cnt;
	u16 ba_miss_cnt;
};

struct mt7915_phy {
	struct mt76_phy *mt76;
	struct mt7915_dev *dev;

	struct ieee80211_sband_iftype_data iftype[2][NUM_NL80211_IFTYPES];

	u32 rxfilter;
	u32 omac_mask;

	u16 noise;
	u16 chainmask;

	s16 coverage_class;
	u8 slottime;

	u8 rdd_state;
	int dfs_state;

	__le32 rx_ampdu_ts;
	u32 ampdu_ref;

	struct mib_stats mib;
	struct list_head stats_list;

	struct delayed_work mac_work;
	u8 mac_work_count;
	u8 sta_work_count;
};

struct mt7915_dev {
	union { /* must be first */
		struct mt76_dev mt76;
		struct mt76_phy mphy;
	};

	const struct mt76_bus_ops *bus_ops;
	struct mt7915_phy phy;

	u16 chainmask;

	struct work_struct init_work;
	struct work_struct rc_work;
	struct work_struct reset_work;
	wait_queue_head_t reset_wait;
	u32 reset_state;

	struct list_head sta_rc_list;
	struct list_head sta_poll_list;
	spinlock_t sta_poll_lock;

	u32 hw_pattern;

	spinlock_t token_lock;
	struct idr token;

	s8 **rate_power; /* TODO: use mt76_rate_power */

	bool fw_debug;
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
	MT_LMAC_AC00,
	MT_LMAC_AC01,
	MT_LMAC_AC02,
	MT_LMAC_AC03,
	MT_LMAC_ALTX0 = 0x10,
	MT_LMAC_BMC0,
	MT_LMAC_BCN0,
};

enum {
	MT_RX_SEL0,
	MT_RX_SEL1,
};

enum mt7915_rdd_cmd {
	RDD_STOP,
	RDD_START,
	RDD_DET_MODE,
	RDD_RADAR_EMULATE,
	RDD_START_TXQ = 20,
	RDD_CAC_START = 50,
	RDD_CAC_END,
	RDD_NORMAL_START,
	RDD_DISABLE_DFS_CAL,
	RDD_PULSE_DBG,
	RDD_READ_PULSE,
	RDD_RESUME_BF,
	RDD_IRQ_OFF,
};

enum {
	RATE_CTRL_RU_INFO,
	RATE_CTRL_FIXED_RATE_INFO,
	RATE_CTRL_DUMP_INFO,
	RATE_CTRL_MU_INFO,
};

static inline struct mt7915_phy *
mt7915_hw_phy(struct ieee80211_hw *hw)
{
	struct mt76_phy *phy = hw->priv;

	return phy->priv;
}

static inline struct mt7915_dev *
mt7915_hw_dev(struct ieee80211_hw *hw)
{
	struct mt76_phy *phy = hw->priv;

	return container_of(phy->dev, struct mt7915_dev, mt76);
}

static inline struct mt7915_phy *
mt7915_ext_phy(struct mt7915_dev *dev)
{
	struct mt76_phy *phy = dev->mt76.phy2;

	if (!phy)
		return NULL;

	return phy->priv;
}

static inline u8 mt7915_lmac_mapping(struct mt7915_dev *dev, u8 ac)
{
	static const u8 lmac_queue_map[] = {
		[IEEE80211_AC_BK] = MT_LMAC_AC00,
		[IEEE80211_AC_BE] = MT_LMAC_AC01,
		[IEEE80211_AC_VI] = MT_LMAC_AC02,
		[IEEE80211_AC_VO] = MT_LMAC_AC03,
	};

	if (WARN_ON_ONCE(ac >= ARRAY_SIZE(lmac_queue_map)))
		return MT_LMAC_AC01; /* BE */

	return lmac_queue_map[ac];
}

extern const struct ieee80211_ops mt7915_ops;
extern struct pci_driver mt7915_pci_driver;

u32 mt7915_reg_map(struct mt7915_dev *dev, u32 addr);

int mt7915_register_device(struct mt7915_dev *dev);
void mt7915_unregister_device(struct mt7915_dev *dev);
int mt7915_register_ext_phy(struct mt7915_dev *dev);
void mt7915_unregister_ext_phy(struct mt7915_dev *dev);
int mt7915_eeprom_init(struct mt7915_dev *dev);
u32 mt7915_eeprom_read(struct mt7915_dev *dev, u32 offset);
int mt7915_eeprom_get_target_power(struct mt7915_dev *dev,
				   struct ieee80211_channel *chan,
				   u8 chain_idx);
void mt7915_eeprom_init_sku(struct mt7915_dev *dev);
int mt7915_dma_init(struct mt7915_dev *dev);
void mt7915_dma_prefetch(struct mt7915_dev *dev);
void mt7915_dma_cleanup(struct mt7915_dev *dev);
int mt7915_mcu_init(struct mt7915_dev *dev);
int mt7915_mcu_add_dev_info(struct mt7915_dev *dev,
			    struct ieee80211_vif *vif, bool enable);
int mt7915_mcu_add_bss_info(struct mt7915_phy *phy,
			    struct ieee80211_vif *vif, int enable);
int mt7915_mcu_add_sta(struct mt7915_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, bool enable);
int mt7915_mcu_add_sta_adv(struct mt7915_dev *dev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta, bool enable);
int mt7915_mcu_add_tx_ba(struct mt7915_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add);
int mt7915_mcu_add_rx_ba(struct mt7915_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add);
int mt7915_mcu_add_key(struct mt7915_dev *dev, struct ieee80211_vif *vif,
		       struct mt7915_sta *msta, struct ieee80211_key_conf *key,
		       enum set_key_cmd cmd);
int mt7915_mcu_add_beacon(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  int enable);
int mt7915_mcu_add_obss_spr(struct mt7915_dev *dev, struct ieee80211_vif *vif,
                            bool enable);
int mt7915_mcu_add_rate_ctrl(struct mt7915_dev *dev, struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta);
int mt7915_mcu_add_smps(struct mt7915_dev *dev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta);
int mt7915_mcu_set_chan_info(struct mt7915_phy *phy, int cmd);
int mt7915_mcu_set_tx(struct mt7915_dev *dev, struct ieee80211_vif *vif);
int mt7915_mcu_set_fixed_rate(struct mt7915_dev *dev,
			      struct ieee80211_sta *sta, u32 rate);
int mt7915_mcu_set_eeprom(struct mt7915_dev *dev);
int mt7915_mcu_get_eeprom(struct mt7915_dev *dev, u32 offset);
int mt7915_mcu_set_mac(struct mt7915_dev *dev, int band, bool enable,
		       bool hdr_trans);
int mt7915_mcu_set_scs(struct mt7915_dev *dev, u8 band, bool enable);
int mt7915_mcu_set_ser(struct mt7915_dev *dev, u8 action, u8 set, u8 band);
int mt7915_mcu_set_rts_thresh(struct mt7915_phy *phy, u32 val);
int mt7915_mcu_set_pm(struct mt7915_dev *dev, int band, int enter);
int mt7915_mcu_set_sku_en(struct mt7915_phy *phy, bool enable);
int mt7915_mcu_set_sku(struct mt7915_phy *phy);
int mt7915_mcu_set_txbf_type(struct mt7915_dev *dev);
int mt7915_mcu_set_txbf_sounding(struct mt7915_dev *dev);
int mt7915_mcu_set_fcc5_lpn(struct mt7915_dev *dev, int val);
int mt7915_mcu_set_pulse_th(struct mt7915_dev *dev,
			    const struct mt7915_dfs_pulse *pulse);
int mt7915_mcu_set_radar_th(struct mt7915_dev *dev, int index,
			    const struct mt7915_dfs_pattern *pattern);
int mt7915_mcu_get_rate_info(struct mt7915_dev *dev, u32 cmd, u16 wlan_idx);
int mt7915_mcu_get_temperature(struct mt7915_dev *dev, int index);
int mt7915_mcu_rdd_cmd(struct mt7915_dev *dev, enum mt7915_rdd_cmd cmd,
		       u8 index, u8 rx_sel, u8 val);
int mt7915_mcu_fw_log_2_host(struct mt7915_dev *dev, u8 ctrl);
int mt7915_mcu_fw_dbg_ctrl(struct mt7915_dev *dev, u32 module, u8 level);
void mt7915_mcu_rx_event(struct mt7915_dev *dev, struct sk_buff *skb);
void mt7915_mcu_exit(struct mt7915_dev *dev);

static inline bool is_mt7915(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7915;
}

static inline void mt7915_irq_enable(struct mt7915_dev *dev, u32 mask)
{
	mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, 0, mask);
}

static inline void mt7915_irq_disable(struct mt7915_dev *dev, u32 mask)
{
	mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, mask, 0);
}

static inline u32
mt7915_reg_map_l1(struct mt7915_dev *dev, u32 addr)
{
	u32 offset = FIELD_GET(MT_HIF_REMAP_L1_OFFSET, addr);
	u32 base = FIELD_GET(MT_HIF_REMAP_L1_BASE, addr);

	mt76_rmw_field(dev, MT_HIF_REMAP_L1, MT_HIF_REMAP_L1_MASK, base);
	/* use read to push write */
	mt76_rr(dev, MT_HIF_REMAP_L1);

	return MT_HIF_REMAP_BASE_L1 + offset;
}

static inline u32
mt7915_l1_rr(struct mt7915_dev *dev, u32 addr)
{
	return mt76_rr(dev, mt7915_reg_map_l1(dev, addr));
}

static inline void
mt7915_l1_wr(struct mt7915_dev *dev, u32 addr, u32 val)
{
	mt76_wr(dev, mt7915_reg_map_l1(dev, addr), val);
}

static inline u32
mt7915_l1_rmw(struct mt7915_dev *dev, u32 addr, u32 mask, u32 val)
{
	val |= mt7915_l1_rr(dev, addr) & ~mask;
	mt7915_l1_wr(dev, addr, val);

	return val;
}

#define mt7915_l1_set(dev, addr, val)	mt7915_l1_rmw(dev, addr, 0, val)
#define mt7915_l1_clear(dev, addr, val)	mt7915_l1_rmw(dev, addr, val, 0)

static inline u32
mt7915_reg_map_l2(struct mt7915_dev *dev, u32 addr)
{
	u32 offset = FIELD_GET(MT_HIF_REMAP_L2_OFFSET, addr);
	u32 base = FIELD_GET(MT_HIF_REMAP_L2_BASE, addr);

	mt76_rmw_field(dev, MT_HIF_REMAP_L2, MT_HIF_REMAP_L2_MASK, base);
	/* use read to push write */
	mt76_rr(dev, MT_HIF_REMAP_L2);

	return MT_HIF_REMAP_BASE_L2 + offset;
}

static inline u32
mt7915_l2_rr(struct mt7915_dev *dev, u32 addr)
{
	return mt76_rr(dev, mt7915_reg_map_l2(dev, addr));
}

static inline void
mt7915_l2_wr(struct mt7915_dev *dev, u32 addr, u32 val)
{
	mt76_wr(dev, mt7915_reg_map_l2(dev, addr), val);
}

static inline u32
mt7915_l2_rmw(struct mt7915_dev *dev, u32 addr, u32 mask, u32 val)
{
	val |= mt7915_l2_rr(dev, addr) & ~mask;
	mt7915_l2_wr(dev, addr, val);

	return val;
}

#define mt7915_l2_set(dev, addr, val)	mt7915_l2_rmw(dev, addr, 0, val)
#define mt7915_l2_clear(dev, addr, val)	mt7915_l2_rmw(dev, addr, val, 0)

bool mt7915_mac_wtbl_update(struct mt7915_dev *dev, int idx, u32 mask);
void mt7915_mac_reset_counters(struct mt7915_phy *phy);
void mt7915_mac_cca_stats_reset(struct mt7915_phy *phy);
void mt7915_mac_write_txwi(struct mt7915_dev *dev, __le32 *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid,
			   struct ieee80211_key_conf *key, bool beacon);
void mt7915_mac_set_timing(struct mt7915_phy *phy);
int mt7915_mac_fill_rx(struct mt7915_dev *dev, struct sk_buff *skb);
void mt7915_mac_tx_free(struct mt7915_dev *dev, struct sk_buff *skb);
int mt7915_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
void mt7915_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);
void mt7915_mac_work(struct work_struct *work);
void mt7915_mac_reset_work(struct work_struct *work);
void mt7915_mac_sta_rc_work(struct work_struct *work);
int mt7915_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info);
void mt7915_tx_complete_skb(struct mt76_dev *mdev, enum mt76_txq_id qid,
			    struct mt76_queue_entry *e);
void mt7915_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb);
void mt7915_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps);
void mt7915_stats_work(struct work_struct *work);
void mt7915_txp_skb_unmap(struct mt76_dev *dev,
			  struct mt76_txwi_cache *txwi);
int mt76_dfs_start_rdd(struct mt7915_dev *dev, bool force);
int mt7915_dfs_init_radar_detector(struct mt7915_phy *phy);
void mt7915_set_stream_he_caps(struct mt7915_phy *phy);
void mt7915_set_stream_vht_txbf_caps(struct mt7915_phy *phy);
void mt7915_update_channel(struct mt76_dev *mdev);
int mt7915_init_debugfs(struct mt7915_dev *dev);
#ifdef CONFIG_MAC80211_DEBUGFS
void mt7915_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir);
#endif

#endif
