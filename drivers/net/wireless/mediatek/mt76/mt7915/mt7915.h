/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT7915_H
#define __MT7915_H

#include <linux/interrupt.h>
#include <linux/ktime.h>
#include "../mt76.h"
#include "regs.h"

#define MT7915_MAX_INTERFACES		19
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

#define MT7915_EEPROM_DEFAULT		"mediatek/mt7915_eeprom.bin"
#define MT7915_EEPROM_DEFAULT_DBDC	"mediatek/mt7915_eeprom_dbdc.bin"

#define MT7915_EEPROM_SIZE		3584
#define MT7915_EEPROM_BLOCK_SIZE	16
#define MT7915_TOKEN_SIZE		8192

#define MT7915_CFEND_RATE_DEFAULT	0x49	/* OFDM 24M */
#define MT7915_CFEND_RATE_11B		0x03	/* 11B LP, 11M */

#define MT7915_THERMAL_THROTTLE_MAX	100

#define MT7915_SKU_RATE_NUM		161

#define MT7915_MAX_TWT_AGRT		16
#define MT7915_MAX_STA_TWT_AGRT		8

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
	MT7915_RXQ_MCU_WA_EXT,
};

struct mt7915_sta_key_conf {
	s8 keyidx;
	u8 key[16];
};

struct mt7915_twt_flow {
	struct list_head list;
	u64 start_tsf;
	u64 tsf;
	u32 duration;
	u16 wcid;
	__le16 mantissa;
	u8 exp;
	u8 table_id;
	u8 id;
	u8 protection:1;
	u8 flowtype:1;
	u8 trigger:1;
	u8 sched:1;
};

struct mt7915_sta {
	struct mt76_wcid wcid; /* must be first */

	struct mt7915_vif *vif;

	struct list_head poll_list;
	struct list_head rc_list;
	u32 airtime_ac[8];

	unsigned long changed;
	unsigned long jiffies;
	unsigned long ampdu_state;

	struct mt76_sta_stats stats;

	struct mt7915_sta_key_conf bip;

	struct {
		u8 flowid_mask;
		struct mt7915_twt_flow flow[MT7915_MAX_STA_TWT_AGRT];
	} twt;
};

struct mt7915_vif_cap {
	bool ldpc:1;
	bool vht_su_ebfer:1;
	bool vht_su_ebfee:1;
	bool vht_mu_ebfer:1;
	bool vht_mu_ebfee:1;
	bool he_su_ebfer:1;
	bool he_su_ebfee:1;
	bool he_mu_ebfer:1;
};

struct mt7915_vif {
	struct mt76_vif mt76; /* must be first */

	struct mt7915_vif_cap cap;
	struct mt7915_sta sta;
	struct mt7915_phy *phy;

	struct ieee80211_tx_queue_params queue_params[IEEE80211_NUM_ACS];
	struct cfg80211_bitrate_mask bitrate_mask;
};

/* per-phy stats.  */
struct mib_stats {
	u32 ack_fail_cnt;
	u32 fcs_err_cnt;
	u32 rts_cnt;
	u32 rts_retries_cnt;
	u32 ba_miss_cnt;
	u32 tx_bf_cnt;
	u32 tx_mu_mpdu_cnt;
	u32 tx_mu_acked_mpdu_cnt;
	u32 tx_su_acked_mpdu_cnt;
	u32 tx_bf_ibf_ppdu_cnt;
	u32 tx_bf_ebf_ppdu_cnt;

	u32 tx_bf_rx_fb_all_cnt;
	u32 tx_bf_rx_fb_he_cnt;
	u32 tx_bf_rx_fb_vht_cnt;
	u32 tx_bf_rx_fb_ht_cnt;

	u32 tx_bf_rx_fb_bw; /* value of last sample, not cumulative */
	u32 tx_bf_rx_fb_nc_cnt;
	u32 tx_bf_rx_fb_nr_cnt;
	u32 tx_bf_fb_cpl_cnt;
	u32 tx_bf_fb_trig_cnt;

	u32 tx_ampdu_cnt;
	u32 tx_stop_q_empty_cnt;
	u32 tx_mpdu_attempts_cnt;
	u32 tx_mpdu_success_cnt;
	u32 tx_pkt_ebf_cnt;
	u32 tx_pkt_ibf_cnt;

	u32 tx_rwp_fail_cnt;
	u32 tx_rwp_need_cnt;

	/* rx stats */
	u32 rx_fifo_full_cnt;
	u32 channel_idle_cnt;
	u32 rx_vector_mismatch_cnt;
	u32 rx_delimiter_fail_cnt;
	u32 rx_len_mismatch_cnt;
	u32 rx_mpdu_cnt;
	u32 rx_ampdu_cnt;
	u32 rx_ampdu_bytes_cnt;
	u32 rx_ampdu_valid_subframe_cnt;
	u32 rx_ampdu_valid_subframe_bytes_cnt;
	u32 rx_pfdrop_cnt;
	u32 rx_vec_queue_overflow_drop_cnt;
	u32 rx_ba_cnt;

	u32 tx_amsdu[8];
	u32 tx_amsdu_cnt;
};

struct mt7915_hif {
	struct list_head list;

	struct device *dev;
	void __iomem *regs;
	int irq;
};

struct mt7915_phy {
	struct mt76_phy *mt76;
	struct mt7915_dev *dev;

	struct ieee80211_sband_iftype_data iftype[2][NUM_NL80211_IFTYPES];

	struct ieee80211_vif *monitor_vif;

	struct thermal_cooling_device *cdev;
	u8 throttle_state;
	u32 throttle_temp[2]; /* 0: critical high, 1: maximum */

	u32 rxfilter;
	u64 omac_mask;

	u16 noise;

	s16 coverage_class;
	u8 slottime;

	u8 rdd_state;
	int dfs_state;

	u32 rx_ampdu_ts;
	u32 ampdu_ref;

	struct mib_stats mib;
	struct mt76_channel_state state_ts;

#ifdef CONFIG_NL80211_TESTMODE
	struct {
		u32 *reg_backup;

		s32 last_freq_offset;
		u8 last_rcpi[4];
		s8 last_ib_rssi[4];
		s8 last_wb_rssi[4];
		u8 last_snr;

		u8 spe_idx;
	} test;
#endif
};

struct mt7915_dev {
	union { /* must be first */
		struct mt76_dev mt76;
		struct mt76_phy mphy;
	};

	struct mt7915_hif *hif2;

	const struct mt76_bus_ops *bus_ops;
	struct tasklet_struct irq_tasklet;
	struct mt7915_phy phy;

	u16 chainmask;
	u32 hif_idx;

	struct work_struct init_work;
	struct work_struct rc_work;
	struct work_struct reset_work;
	wait_queue_head_t reset_wait;
	u32 reset_state;

	struct list_head sta_rc_list;
	struct list_head sta_poll_list;
	struct list_head twt_list;
	spinlock_t sta_poll_lock;

	u32 hw_pattern;

	bool dbdc_support;
	bool flash_mode;
	bool muru_debug;
	bool ibf;
	u8 fw_debug_wm;
	u8 fw_debug_wa;

	void *cal;

	struct {
		u8 table_mask;
		u8 n_agrt;
	} twt;
};

enum {
	MT_CTX0,
	MT_HIF0 = 0x0,

	MT_LMAC_AC00 = 0x0,
	MT_LMAC_AC01,
	MT_LMAC_AC02,
	MT_LMAC_AC03,
	MT_LMAC_ALTX0 = 0x10,
	MT_LMAC_BMC0,
	MT_LMAC_BCN0,
	MT_LMAC_PSMP0,
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
	/* LMAC uses the reverse order of mac80211 AC indexes */
	return 3 - ac;
}

extern const struct ieee80211_ops mt7915_ops;
extern const struct mt76_testmode_ops mt7915_testmode_ops;
extern struct pci_driver mt7915_pci_driver;
extern struct pci_driver mt7915_hif_driver;

int mt7915_mmio_probe(struct device *pdev,
		      void __iomem *mem_base,
		      u32 device_id,
		      int irq, struct mt7915_hif *hif2);
u32 mt7915_reg_map(struct mt7915_dev *dev, u32 addr);
u64 __mt7915_get_tsf(struct ieee80211_hw *hw, struct mt7915_vif *mvif);
int mt7915_register_device(struct mt7915_dev *dev);
void mt7915_unregister_device(struct mt7915_dev *dev);
int mt7915_eeprom_init(struct mt7915_dev *dev);
void mt7915_eeprom_parse_band_config(struct mt7915_phy *phy);
int mt7915_eeprom_get_target_power(struct mt7915_dev *dev,
				   struct ieee80211_channel *chan,
				   u8 chain_idx);
s8 mt7915_eeprom_get_power_delta(struct mt7915_dev *dev, int band);
int mt7915_dma_init(struct mt7915_dev *dev);
void mt7915_dma_prefetch(struct mt7915_dev *dev);
void mt7915_dma_cleanup(struct mt7915_dev *dev);
int mt7915_mcu_init(struct mt7915_dev *dev);
int mt7915_mcu_twt_agrt_update(struct mt7915_dev *dev,
			       struct mt7915_vif *mvif,
			       struct mt7915_twt_flow *flow,
			       int cmd);
int mt7915_mcu_add_dev_info(struct mt7915_phy *phy,
			    struct ieee80211_vif *vif, bool enable);
int mt7915_mcu_add_bss_info(struct mt7915_phy *phy,
			    struct ieee80211_vif *vif, int enable);
int mt7915_mcu_add_sta(struct mt7915_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, bool enable);
int mt7915_mcu_sta_update_hdr_trans(struct mt7915_dev *dev,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta);
int mt7915_mcu_add_tx_ba(struct mt7915_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add);
int mt7915_mcu_add_rx_ba(struct mt7915_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add);
int mt7915_mcu_add_key(struct mt7915_dev *dev, struct ieee80211_vif *vif,
		       struct mt7915_sta *msta, struct ieee80211_key_conf *key,
		       enum set_key_cmd cmd);
int mt7915_mcu_update_bss_color(struct mt7915_dev *dev, struct ieee80211_vif *vif,
				struct cfg80211_he_bss_color *he_bss_color);
int mt7915_mcu_add_beacon(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  int enable);
int mt7915_mcu_add_obss_spr(struct mt7915_dev *dev, struct ieee80211_vif *vif,
                            bool enable);
int mt7915_mcu_add_rate_ctrl(struct mt7915_dev *dev, struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta, bool changed);
int mt7915_mcu_add_smps(struct mt7915_dev *dev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta);
int mt7915_set_channel(struct mt7915_phy *phy);
int mt7915_mcu_set_chan_info(struct mt7915_phy *phy, int cmd);
int mt7915_mcu_set_tx(struct mt7915_dev *dev, struct ieee80211_vif *vif);
int mt7915_mcu_update_edca(struct mt7915_dev *dev, void *req);
int mt7915_mcu_set_fixed_rate_ctrl(struct mt7915_dev *dev,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   void *data, u32 field);
int mt7915_mcu_set_eeprom(struct mt7915_dev *dev);
int mt7915_mcu_get_eeprom(struct mt7915_dev *dev, u32 offset);
int mt7915_mcu_get_eeprom_free_block(struct mt7915_dev *dev, u8 *block_num);
int mt7915_mcu_set_mac(struct mt7915_dev *dev, int band, bool enable,
		       bool hdr_trans);
int mt7915_mcu_set_test_param(struct mt7915_dev *dev, u8 param, bool test_mode,
			      u8 en);
int mt7915_mcu_set_scs(struct mt7915_dev *dev, u8 band, bool enable);
int mt7915_mcu_set_ser(struct mt7915_dev *dev, u8 action, u8 set, u8 band);
int mt7915_mcu_set_rts_thresh(struct mt7915_phy *phy, u32 val);
int mt7915_mcu_set_pm(struct mt7915_dev *dev, int band, int enter);
int mt7915_mcu_set_sku_en(struct mt7915_phy *phy, bool enable);
int mt7915_mcu_set_txpower_sku(struct mt7915_phy *phy);
int mt7915_mcu_get_txpower_sku(struct mt7915_phy *phy, s8 *txpower, int len);
int mt7915_mcu_set_txbf(struct mt7915_dev *dev, u8 action);
int mt7915_mcu_set_fcc5_lpn(struct mt7915_dev *dev, int val);
int mt7915_mcu_set_pulse_th(struct mt7915_dev *dev,
			    const struct mt7915_dfs_pulse *pulse);
int mt7915_mcu_set_radar_th(struct mt7915_dev *dev, int index,
			    const struct mt7915_dfs_pattern *pattern);
int mt7915_mcu_set_muru_ctrl(struct mt7915_dev *dev, u32 cmd, u32 val);
int mt7915_mcu_apply_group_cal(struct mt7915_dev *dev);
int mt7915_mcu_apply_tx_dpd(struct mt7915_phy *phy);
int mt7915_mcu_get_chan_mib_info(struct mt7915_phy *phy, bool chan_switch);
int mt7915_mcu_get_temperature(struct mt7915_phy *phy);
int mt7915_mcu_set_thermal_throttling(struct mt7915_phy *phy, u8 state);
int mt7915_mcu_get_rx_rate(struct mt7915_phy *phy, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta, struct rate_info *rate);
int mt7915_mcu_rdd_cmd(struct mt7915_dev *dev, enum mt7915_rdd_cmd cmd,
		       u8 index, u8 rx_sel, u8 val);
int mt7915_mcu_wa_cmd(struct mt7915_dev *dev, int cmd, u32 a1, u32 a2, u32 a3);
int mt7915_mcu_fw_log_2_host(struct mt7915_dev *dev, u8 type, u8 ctrl);
int mt7915_mcu_fw_dbg_ctrl(struct mt7915_dev *dev, u32 module, u8 level);
void mt7915_mcu_rx_event(struct mt7915_dev *dev, struct sk_buff *skb);
void mt7915_mcu_exit(struct mt7915_dev *dev);

static inline bool is_mt7915(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7915;
}

void mt7915_dual_hif_set_irq_mask(struct mt7915_dev *dev, bool write_reg,
				  u32 clear, u32 set);

static inline void mt7915_irq_enable(struct mt7915_dev *dev, u32 mask)
{
	if (dev->hif2)
		mt7915_dual_hif_set_irq_mask(dev, false, 0, mask);
	else
		mt76_set_irq_mask(&dev->mt76, 0, 0, mask);

	tasklet_schedule(&dev->irq_tasklet);
}

static inline void mt7915_irq_disable(struct mt7915_dev *dev, u32 mask)
{
	if (dev->hif2)
		mt7915_dual_hif_set_irq_mask(dev, true, mask, 0);
	else
		mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, mask, 0);
}

u32 mt7915_mac_wtbl_lmac_addr(struct mt7915_dev *dev, u16 wcid, u8 dw);
bool mt7915_mac_wtbl_update(struct mt7915_dev *dev, int idx, u32 mask);
void mt7915_mac_reset_counters(struct mt7915_phy *phy);
void mt7915_mac_cca_stats_reset(struct mt7915_phy *phy);
void mt7915_mac_enable_nf(struct mt7915_dev *dev, bool ext_phy);
void mt7915_mac_write_txwi(struct mt7915_dev *dev, __le32 *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid, int pid,
			   struct ieee80211_key_conf *key, bool beacon);
void mt7915_mac_set_timing(struct mt7915_phy *phy);
int mt7915_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
void mt7915_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);
void mt7915_mac_work(struct work_struct *work);
void mt7915_mac_reset_work(struct work_struct *work);
void mt7915_mac_sta_rc_work(struct work_struct *work);
void mt7915_mac_update_stats(struct mt7915_phy *phy);
void mt7915_mac_twt_teardown_flow(struct mt7915_dev *dev,
				  struct mt7915_sta *msta,
				  u8 flowid);
void mt7915_mac_add_twt_setup(struct ieee80211_hw *hw,
			      struct ieee80211_sta *sta,
			      struct ieee80211_twt_setup *twt);
int mt7915_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info);
void mt7915_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue_entry *e);
void mt7915_tx_token_put(struct mt7915_dev *dev);
int mt7915_init_tx_queues(struct mt7915_phy *phy, int idx, int n_desc);
void mt7915_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb);
bool mt7915_rx_check(struct mt76_dev *mdev, void *data, int len);
void mt7915_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps);
void mt7915_stats_work(struct work_struct *work);
int mt76_dfs_start_rdd(struct mt7915_dev *dev, bool force);
int mt7915_dfs_init_radar_detector(struct mt7915_phy *phy);
void mt7915_set_stream_he_caps(struct mt7915_phy *phy);
void mt7915_set_stream_vht_txbf_caps(struct mt7915_phy *phy);
void mt7915_update_channel(struct mt76_phy *mphy);
int mt7915_mcu_muru_debug_set(struct mt7915_dev *dev, bool enable);
int mt7915_mcu_muru_debug_get(struct mt7915_phy *phy, void *ms);
int mt7915_init_debugfs(struct mt7915_phy *phy);
#ifdef CONFIG_MAC80211_DEBUGFS
void mt7915_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir);
#endif

#endif
