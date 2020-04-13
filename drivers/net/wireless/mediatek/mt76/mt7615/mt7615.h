/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2019 MediaTek Inc. */

#ifndef __MT7615_H
#define __MT7615_H

#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/regmap.h>
#include "../mt76.h"
#include "regs.h"

#define MT7615_MAX_INTERFACES		4
#define MT7615_MAX_WMM_SETS		4
#define MT7615_WTBL_SIZE		128
#define MT7615_WTBL_RESERVED		(MT7615_WTBL_SIZE - 1)
#define MT7615_WTBL_STA			(MT7615_WTBL_RESERVED - \
					 MT7615_MAX_INTERFACES)

#define MT7615_WATCHDOG_TIME		(HZ / 10)
#define MT7615_RESET_TIMEOUT		(30 * HZ)
#define MT7615_RATE_RETRY		2

#define MT7615_TX_RING_SIZE		1024
#define MT7615_TX_MGMT_RING_SIZE	128
#define MT7615_TX_MCU_RING_SIZE		128
#define MT7615_TX_FWDL_RING_SIZE	128

#define MT7615_RX_RING_SIZE		1024
#define MT7615_RX_MCU_RING_SIZE		512

#define MT7615_FIRMWARE_CR4		"mediatek/mt7615_cr4.bin"
#define MT7615_FIRMWARE_N9		"mediatek/mt7615_n9.bin"
#define MT7615_ROM_PATCH		"mediatek/mt7615_rom_patch.bin"

#define MT7622_FIRMWARE_N9		"mediatek/mt7622_n9.bin"
#define MT7622_ROM_PATCH		"mediatek/mt7622_rom_patch.bin"

#define MT7615_FIRMWARE_V1		1
#define MT7615_FIRMWARE_V2		2
#define MT7615_FIRMWARE_V3		3

#define MT7663_ROM_PATCH		"mediatek/mt7663pr2h_v3.bin"
#define MT7663_FIRMWARE_N9              "mediatek/mt7663_n9_v3.bin"

#define MT7615_EEPROM_SIZE		1024
#define MT7615_TOKEN_SIZE		4096

#define MT_FRAC_SCALE		12
#define MT_FRAC(val, div)	(((val) << MT_FRAC_SCALE) / (div))

#define MT_CHFREQ_VALID		BIT(7)
#define MT_CHFREQ_DBDC_IDX	BIT(6)
#define MT_CHFREQ_SEQ		GENMASK(5, 0)

#define MT7615_BAR_RATE_DEFAULT		0x4b /* OFDM 6M */
#define MT7615_CFEND_RATE_DEFAULT	0x49 /* OFDM 24M */
#define MT7615_CFEND_RATE_11B		0x03 /* 11B LP, 11M */

struct mt7615_vif;
struct mt7615_sta;
struct mt7615_dfs_pulse;
struct mt7615_dfs_pattern;

enum mt7615_hw_txq_id {
	MT7615_TXQ_MAIN,
	MT7615_TXQ_EXT,
	MT7615_TXQ_MCU,
	MT7615_TXQ_FWDL,
};

enum mt7622_hw_txq_id {
	MT7622_TXQ_AC0,
	MT7622_TXQ_AC1,
	MT7622_TXQ_AC2,
	MT7622_TXQ_FWDL = MT7615_TXQ_FWDL,
	MT7622_TXQ_AC3,
	MT7622_TXQ_MGMT,
	MT7622_TXQ_MCU = 15,
};

struct mt7615_rate_set {
	struct ieee80211_tx_rate probe_rate;
	struct ieee80211_tx_rate rates[4];
};

struct mt7615_sta {
	struct mt76_wcid wcid; /* must be first */

	struct mt7615_vif *vif;

	struct list_head poll_list;
	u32 airtime_ac[8];

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

struct mib_stats {
	u32 ack_fail_cnt;
	u32 fcs_err_cnt;
	u32 rts_cnt;
	u32 rts_retries_cnt;
};

struct mt7615_phy {
	struct mt76_phy *mt76;
	struct mt7615_dev *dev;

	u32 rxfilter;
	u32 omac_mask;

	u16 noise;

	unsigned long last_cca_adj;
	int false_cca_ofdm, false_cca_cck;
	s8 ofdm_sensitivity;
	s8 cck_sensitivity;

	u16 chainmask;

	s16 coverage_class;
	u8 slottime;

	u8 chfreq;
	u8 rdd_state;
	int dfs_state;

	__le32 rx_ampdu_ts;
	u32 ampdu_ref;

	struct mib_stats mib;
};

#define mt7615_mcu_add_tx_ba(dev, ...)	(dev)->mcu_ops->add_tx_ba((dev), __VA_ARGS__)
#define mt7615_mcu_add_rx_ba(dev, ...)	(dev)->mcu_ops->add_rx_ba((dev), __VA_ARGS__)
#define mt7615_mcu_sta_add(dev, ...)	(dev)->mcu_ops->sta_add((dev),  __VA_ARGS__)
#define mt7615_mcu_add_dev_info(dev, ...) (dev)->mcu_ops->add_dev_info((dev),  __VA_ARGS__)
#define mt7615_mcu_add_bss_info(dev, ...) (dev)->mcu_ops->add_bss_info((dev),  __VA_ARGS__)
#define mt7615_mcu_add_beacon(dev, ...)	(dev)->mcu_ops->add_beacon_offload((dev),  __VA_ARGS__)
#define mt7615_mcu_set_pm(dev, ...)	(dev)->mcu_ops->set_pm_state((dev),  __VA_ARGS__)
struct mt7615_mcu_ops {
	int (*add_tx_ba)(struct mt7615_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable);
	int (*add_rx_ba)(struct mt7615_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable);
	int (*sta_add)(struct mt7615_dev *dev,
		       struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, bool enable);
	int (*add_dev_info)(struct mt7615_dev *dev,
			    struct ieee80211_vif *vif, bool enable);
	int (*add_bss_info)(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			    bool enable);
	int (*add_beacon_offload)(struct mt7615_dev *dev,
				  struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif, bool enable);
	int (*set_pm_state)(struct mt7615_dev *dev, int band, int state);
};

struct mt7615_dev {
	union { /* must be first */
		struct mt76_dev mt76;
		struct mt76_phy mphy;
	};

	struct mt7615_phy phy;
	u32 vif_mask;
	u32 omac_mask;

	u16 chainmask;

	const struct mt7615_mcu_ops *mcu_ops;
	struct regmap *infracfg;
	const u32 *reg_map;

	struct work_struct mcu_work;

	struct work_struct reset_work;
	wait_queue_head_t reset_wait;
	u32 reset_state;

	struct list_head sta_poll_list;
	spinlock_t sta_poll_lock;

	struct {
		u8 n_pulses;
		u32 period;
		u16 width;
		s16 power;
	} radar_pattern;
	u32 hw_pattern;

	u8 mac_work_count;
	bool scs_en;
	bool fw_debug;

	spinlock_t token_lock;
	struct idr token;

	u8 fw_ver;
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

static inline struct mt7615_phy *
mt7615_hw_phy(struct ieee80211_hw *hw)
{
	struct mt76_phy *phy = hw->priv;

	return phy->priv;
}

static inline struct mt7615_dev *
mt7615_hw_dev(struct ieee80211_hw *hw)
{
	struct mt76_phy *phy = hw->priv;

	return container_of(phy->dev, struct mt7615_dev, mt76);
}

static inline struct mt7615_phy *
mt7615_ext_phy(struct mt7615_dev *dev)
{
	struct mt76_phy *phy = dev->mt76.phy2;

	if (!phy)
		return NULL;

	return phy->priv;
}

extern const struct ieee80211_ops mt7615_ops;
extern const u32 mt7615e_reg_map[__MT_BASE_MAX];
extern const u32 mt7663e_reg_map[__MT_BASE_MAX];
extern struct pci_driver mt7615_pci_driver;
extern struct platform_driver mt7622_wmac_driver;

#ifdef CONFIG_MT7622_WMAC
int mt7622_wmac_init(struct mt7615_dev *dev);
#else
static inline int mt7622_wmac_init(struct mt7615_dev *dev)
{
	return 0;
}
#endif

int mt7615_mmio_probe(struct device *pdev, void __iomem *mem_base,
		      int irq, const u32 *map);
u32 mt7615_reg_map(struct mt7615_dev *dev, u32 addr);

void mt7615_init_device(struct mt7615_dev *dev);
int mt7615_register_device(struct mt7615_dev *dev);
void mt7615_unregister_device(struct mt7615_dev *dev);
int mt7615_register_ext_phy(struct mt7615_dev *dev);
void mt7615_unregister_ext_phy(struct mt7615_dev *dev);
int mt7615_eeprom_init(struct mt7615_dev *dev);
int mt7615_eeprom_get_power_index(struct mt7615_dev *dev,
				  struct ieee80211_channel *chan,
				  u8 chain_idx);
int mt7615_dma_init(struct mt7615_dev *dev);
void mt7615_dma_cleanup(struct mt7615_dev *dev);
int mt7615_mcu_init(struct mt7615_dev *dev);
bool mt7615_wait_for_mcu_init(struct mt7615_dev *dev);
void mt7615_mac_set_rates(struct mt7615_phy *phy, struct mt7615_sta *sta,
			  struct ieee80211_tx_rate *probe_rate,
			  struct ieee80211_tx_rate *rates);
int mt7615_mcu_del_wtbl_all(struct mt7615_dev *dev);
int mt7615_mcu_set_chan_info(struct mt7615_phy *phy, int cmd);
int mt7615_mcu_set_wmm(struct mt7615_dev *dev, u8 queue,
		       const struct ieee80211_tx_queue_params *params);
void mt7615_mcu_rx_event(struct mt7615_dev *dev, struct sk_buff *skb);
int mt7615_mcu_rdd_cmd(struct mt7615_dev *dev,
		       enum mt7615_rdd_cmd cmd, u8 index,
		       u8 rx_sel, u8 val);
int mt7615_mcu_rdd_send_pattern(struct mt7615_dev *dev);
int mt7615_mcu_fw_log_2_host(struct mt7615_dev *dev, u8 ctrl);

static inline bool is_mt7622(struct mt76_dev *dev)
{
	if (!IS_ENABLED(CONFIG_MT7622_WMAC))
		return false;

	return mt76_chip(dev) == 0x7622;
}

static inline bool is_mt7615(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7615;
}

static inline bool is_mt7663(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7663;
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
bool mt7615_mac_wtbl_update(struct mt7615_dev *dev, int idx, u32 mask);
void mt7615_mac_reset_counters(struct mt7615_dev *dev);
void mt7615_mac_cca_stats_reset(struct mt7615_phy *phy);
void mt7615_mac_set_scs(struct mt7615_dev *dev, bool enable);
void mt7615_mac_enable_nf(struct mt7615_dev *dev, bool ext_phy);
void mt7615_mac_sta_poll(struct mt7615_dev *dev);
int mt7615_mac_write_txwi(struct mt7615_dev *dev, __le32 *txwi,
			  struct sk_buff *skb, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta, int pid,
			  struct ieee80211_key_conf *key, bool beacon);
void mt7615_mac_set_timing(struct mt7615_phy *phy);
int mt7615_mac_fill_rx(struct mt7615_dev *dev, struct sk_buff *skb);
void mt7615_mac_add_txs(struct mt7615_dev *dev, void *data);
void mt7615_mac_tx_free(struct mt7615_dev *dev, struct sk_buff *skb);
int mt7615_mac_wtbl_set_key(struct mt7615_dev *dev, struct mt76_wcid *wcid,
			    struct ieee80211_key_conf *key,
			    enum set_key_cmd cmd);
void mt7615_mac_reset_work(struct work_struct *work);

int mt7615_mcu_wait_response(struct mt7615_dev *dev, int cmd, int seq);
int mt7615_mcu_set_dbdc(struct mt7615_dev *dev);
int mt7615_mcu_set_eeprom(struct mt7615_dev *dev);
int mt7615_mcu_set_mac_enable(struct mt7615_dev *dev, int band, bool enable);
int mt7615_mcu_set_rts_thresh(struct mt7615_phy *phy, u32 val);
int mt7615_mcu_get_temperature(struct mt7615_dev *dev, int index);
void mt7615_mcu_exit(struct mt7615_dev *dev);
void mt7615_mcu_fill_msg(struct mt7615_dev *dev, struct sk_buff *skb,
			 int cmd, int *wait_seq);

int mt7615_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info);

void mt7615_tx_complete_skb(struct mt76_dev *mdev, enum mt76_txq_id qid,
			    struct mt76_queue_entry *e);

void mt7615_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb);
void mt7615_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps);
int mt7615_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
void mt7615_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);
void mt7615_mac_work(struct work_struct *work);
void mt7615_txp_skb_unmap(struct mt76_dev *dev,
			  struct mt76_txwi_cache *txwi);
int mt7615_mcu_set_fcc5_lpn(struct mt7615_dev *dev, int val);
int mt7615_mcu_set_pulse_th(struct mt7615_dev *dev,
			    const struct mt7615_dfs_pulse *pulse);
int mt7615_mcu_set_radar_th(struct mt7615_dev *dev, int index,
			    const struct mt7615_dfs_pattern *pattern);
int mt7615_mcu_set_sku_en(struct mt7615_phy *phy, bool enable);
int mt7615_dfs_init_radar_detector(struct mt7615_phy *phy);

int mt7615_init_debugfs(struct mt7615_dev *dev);

#endif
