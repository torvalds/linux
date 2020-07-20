/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2019 MediaTek Inc. */

#ifndef __MT7615_H
#define __MT7615_H

#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/regmap.h>
#include "../mt76.h"
#include "regs.h"

#define MT7615_MAX_INTERFACES		4
#define MT7615_MAX_WMM_SETS		4
#define MT7663_WTBL_SIZE		32
#define MT7615_WTBL_SIZE		128
#define MT7615_WTBL_RESERVED		(mt7615_wtbl_size(dev) - 1)
#define MT7615_WTBL_STA			(MT7615_WTBL_RESERVED - \
					 MT7615_MAX_INTERFACES)

#define MT7615_PM_TIMEOUT		(HZ / 12)
#define MT7615_WATCHDOG_TIME		(HZ / 10)
#define MT7615_HW_SCAN_TIMEOUT		(HZ / 10)
#define MT7615_RESET_TIMEOUT		(30 * HZ)
#define MT7615_RATE_RETRY		2

#define MT7615_TX_RING_SIZE		1024
#define MT7615_TX_MGMT_RING_SIZE	128
#define MT7615_TX_MCU_RING_SIZE		128
#define MT7615_TX_FWDL_RING_SIZE	128

#define MT7615_RX_RING_SIZE		1024
#define MT7615_RX_MCU_RING_SIZE		512

#define MT7615_DRV_OWN_RETRY_COUNT	10

#define MT7615_FIRMWARE_CR4		"mediatek/mt7615_cr4.bin"
#define MT7615_FIRMWARE_N9		"mediatek/mt7615_n9.bin"
#define MT7615_ROM_PATCH		"mediatek/mt7615_rom_patch.bin"

#define MT7622_FIRMWARE_N9		"mediatek/mt7622_n9.bin"
#define MT7622_ROM_PATCH		"mediatek/mt7622_rom_patch.bin"

#define MT7615_FIRMWARE_V1		1
#define MT7615_FIRMWARE_V2		2
#define MT7615_FIRMWARE_V3		3

#define MT7663_OFFLOAD_ROM_PATCH	"mediatek/mt7663pr2h.bin"
#define MT7663_OFFLOAD_FIRMWARE_N9	"mediatek/mt7663_n9_v3.bin"
#define MT7663_ROM_PATCH		"mediatek/mt7663pr2h_rebb.bin"
#define MT7663_FIRMWARE_N9		"mediatek/mt7663_n9_rebb.bin"

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

#define MT7615_SCAN_IE_LEN		600
#define MT7615_MAX_SCHED_SCAN_INTERVAL	10
#define MT7615_MAX_SCHED_SCAN_SSID	10
#define MT7615_MAX_SCAN_MATCH		16

struct mt7615_vif;
struct mt7615_sta;
struct mt7615_dfs_pulse;
struct mt7615_dfs_pattern;
enum mt7615_cipher_type;

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

struct mt7615_rate_desc {
	bool rateset;
	u16 probe_val;
	u16 val[4];
	u8 bw_idx;
	u8 bw;
};

enum mt7615_wtbl_desc_type {
	MT7615_WTBL_RATE_DESC,
	MT7615_WTBL_KEY_DESC
};

struct mt7615_key_desc {
	enum set_key_cmd cmd;
	u32 cipher;
	s8 keyidx;
	u8 keylen;
	u8 *key;
};

struct mt7615_wtbl_desc {
	struct list_head node;

	enum mt7615_wtbl_desc_type type;
	struct mt7615_sta *sta;

	union {
		struct mt7615_rate_desc rate;
		struct mt7615_key_desc key;
	};
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
	u8 scan_seq_num;

	struct mt7615_sta sta;
};

struct mib_stats {
	u16 ack_fail_cnt;
	u16 fcs_err_cnt;
	u16 rts_cnt;
	u16 rts_retries_cnt;
	u16 ba_miss_cnt;
	unsigned long aggr_per;
};

struct mt7615_phy {
	struct mt76_phy *mt76;
	struct mt7615_dev *dev;

	struct ieee80211_vif *monitor_vif;

	u32 rxfilter;
	u32 omac_mask;

	u16 noise;

	bool scs_en;

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

	struct delayed_work mac_work;
	u8 mac_work_count;

	struct sk_buff_head scan_event_list;
	struct delayed_work scan_work;

	struct work_struct roc_work;
	struct timer_list roc_timer;
	wait_queue_head_t roc_wait;
	bool roc_grant;
};

#define mt7615_mcu_add_tx_ba(dev, ...)	(dev)->mcu_ops->add_tx_ba((dev), __VA_ARGS__)
#define mt7615_mcu_add_rx_ba(dev, ...)	(dev)->mcu_ops->add_rx_ba((dev), __VA_ARGS__)
#define mt7615_mcu_sta_add(dev, ...)	(dev)->mcu_ops->sta_add((dev),  __VA_ARGS__)
#define mt7615_mcu_add_dev_info(dev, ...) (dev)->mcu_ops->add_dev_info((dev),  __VA_ARGS__)
#define mt7615_mcu_add_bss_info(phy, ...) (phy->dev)->mcu_ops->add_bss_info((phy),  __VA_ARGS__)
#define mt7615_mcu_add_beacon(dev, ...)	(dev)->mcu_ops->add_beacon_offload((dev),  __VA_ARGS__)
#define mt7615_mcu_set_pm(dev, ...)	(dev)->mcu_ops->set_pm_state((dev),  __VA_ARGS__)
#define mt7615_mcu_set_drv_ctrl(dev)	(dev)->mcu_ops->set_drv_ctrl((dev))
#define mt7615_mcu_set_fw_ctrl(dev)	(dev)->mcu_ops->set_fw_ctrl((dev))
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
	int (*add_bss_info)(struct mt7615_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, bool enable);
	int (*add_beacon_offload)(struct mt7615_dev *dev,
				  struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif, bool enable);
	int (*set_pm_state)(struct mt7615_dev *dev, int band, int state);
	int (*set_drv_ctrl)(struct mt7615_dev *dev);
	int (*set_fw_ctrl)(struct mt7615_dev *dev);
};

struct mt7615_dev {
	union { /* must be first */
		struct mt76_dev mt76;
		struct mt76_phy mphy;
	};

	const struct mt76_bus_ops *bus_ops;
	struct tasklet_struct irq_tasklet;

	struct mt7615_phy phy;
	u32 omac_mask;

	u16 chainmask;

	struct ieee80211_ops *ops;
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

	bool fw_debug;
	bool flash_eeprom;
	bool dbdc_support;

	spinlock_t token_lock;
	struct idr token;

	u8 fw_ver;

	struct work_struct wtbl_work;
	struct list_head wd_head;

	u32 debugfs_rf_wf;
	u32 debugfs_rf_reg;

#ifdef CONFIG_NL80211_TESTMODE
	struct {
		u32 *reg_backup;

		s16 last_freq_offset;
		u8 last_rcpi[4];
		s8 last_ib_rssi;
		s8 last_wb_rssi;
	} test;
#endif

	struct {
		bool enable;

		spinlock_t txq_lock;
		struct {
			struct mt7615_sta *msta;
			struct sk_buff *skb;
		} tx_q[IEEE80211_NUM_ACS];

		struct work_struct wake_work;
		struct completion wake_cmpl;

		struct delayed_work ps_work;
		unsigned long last_activity;
		unsigned long idle_timeout;
	} pm;
};

enum tx_pkt_queue_idx {
	MT_LMAC_AC00,
	MT_LMAC_AC01,
	MT_LMAC_AC02,
	MT_LMAC_AC03,
	MT_LMAC_ALTX0 = 0x10,
	MT_LMAC_BMC0,
	MT_LMAC_BCN0,
	MT_LMAC_PSMP0,
	MT_LMAC_ALTX1,
	MT_LMAC_BMC1,
	MT_LMAC_BCN1,
	MT_LMAC_PSMP1,
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

extern struct ieee80211_rate mt7615_rates[12];
extern const struct ieee80211_ops mt7615_ops;
extern const u32 mt7615e_reg_map[__MT_BASE_MAX];
extern const u32 mt7663e_reg_map[__MT_BASE_MAX];
extern const u32 mt7663_usb_sdio_reg_map[__MT_BASE_MAX];
extern struct pci_driver mt7615_pci_driver;
extern struct platform_driver mt7622_wmac_driver;
extern const struct mt76_testmode_ops mt7615_testmode_ops;

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

void mt7615_check_offload_capability(struct mt7615_dev *dev);
void mt7615_init_device(struct mt7615_dev *dev);
int mt7615_register_device(struct mt7615_dev *dev);
void mt7615_unregister_device(struct mt7615_dev *dev);
int mt7615_register_ext_phy(struct mt7615_dev *dev);
void mt7615_unregister_ext_phy(struct mt7615_dev *dev);
int mt7615_eeprom_init(struct mt7615_dev *dev, u32 addr);
int mt7615_eeprom_get_target_power_index(struct mt7615_dev *dev,
					 struct ieee80211_channel *chan,
					 u8 chain_idx);
int mt7615_eeprom_get_power_delta_index(struct mt7615_dev *dev,
					enum nl80211_band band);
int mt7615_wait_pdma_busy(struct mt7615_dev *dev);
int mt7615_dma_init(struct mt7615_dev *dev);
void mt7615_dma_cleanup(struct mt7615_dev *dev);
int mt7615_mcu_init(struct mt7615_dev *dev);
bool mt7615_wait_for_mcu_init(struct mt7615_dev *dev);
void mt7615_mac_set_rates(struct mt7615_phy *phy, struct mt7615_sta *sta,
			  struct ieee80211_tx_rate *probe_rate,
			  struct ieee80211_tx_rate *rates);
int mt7615_pm_set_enable(struct mt7615_dev *dev, bool enable);
void mt7615_pm_wake_work(struct work_struct *work);
int mt7615_pm_wake(struct mt7615_dev *dev);
void mt7615_pm_power_save_sched(struct mt7615_dev *dev);
void mt7615_pm_power_save_work(struct work_struct *work);
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
	return mt76_chip(dev) == 0x7615 || mt76_chip(dev) == 0x7611;
}

static inline bool is_mt7663(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7663;
}

static inline bool is_mt7611(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7611;
}

static inline void mt7615_irq_enable(struct mt7615_dev *dev, u32 mask)
{
	mt76_set_irq_mask(&dev->mt76, 0, 0, mask);

	tasklet_schedule(&dev->irq_tasklet);
}

static inline bool mt7615_firmware_offload(struct mt7615_dev *dev)
{
	return dev->fw_ver > MT7615_FIRMWARE_V2;
}

static inline u16 mt7615_wtbl_size(struct mt7615_dev *dev)
{
	if (is_mt7663(&dev->mt76) && mt7615_firmware_offload(dev))
		return MT7663_WTBL_SIZE;
	else
		return MT7615_WTBL_SIZE;
}

static inline void mt7615_mutex_acquire(struct mt7615_dev *dev)
	 __acquires(&dev->mt76.mutex)
{
	mutex_lock(&dev->mt76.mutex);
	mt7615_pm_wake(dev);
}

static inline void mt7615_mutex_release(struct mt7615_dev *dev)
	__releases(&dev->mt76.mutex)
{
	mt7615_pm_power_save_sched(dev);
	mutex_unlock(&dev->mt76.mutex);
}

static inline u8 mt7615_lmac_mapping(struct mt7615_dev *dev, u8 ac)
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

void mt7615_dma_reset(struct mt7615_dev *dev);
void mt7615_scan_work(struct work_struct *work);
void mt7615_roc_work(struct work_struct *work);
void mt7615_roc_timer(struct timer_list *timer);
void mt7615_init_txpower(struct mt7615_dev *dev,
			 struct ieee80211_supported_band *sband);
void mt7615_phy_init(struct mt7615_dev *dev);
void mt7615_mac_init(struct mt7615_dev *dev);
int mt7615_set_channel(struct mt7615_phy *phy);

int mt7615_mcu_restart(struct mt76_dev *dev);
void mt7615_update_channel(struct mt76_dev *mdev);
bool mt7615_mac_wtbl_update(struct mt7615_dev *dev, int idx, u32 mask);
void mt7615_mac_reset_counters(struct mt7615_dev *dev);
void mt7615_mac_cca_stats_reset(struct mt7615_phy *phy);
void mt7615_mac_set_scs(struct mt7615_phy *phy, bool enable);
void mt7615_mac_enable_nf(struct mt7615_dev *dev, bool ext_phy);
void mt7615_mac_sta_poll(struct mt7615_dev *dev);
int mt7615_mac_write_txwi(struct mt7615_dev *dev, __le32 *txwi,
			  struct sk_buff *skb, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta, int pid,
			  struct ieee80211_key_conf *key, bool beacon);
void mt7615_mac_set_timing(struct mt7615_phy *phy);
int mt7615_mac_wtbl_set_key(struct mt7615_dev *dev, struct mt76_wcid *wcid,
			    struct ieee80211_key_conf *key,
			    enum set_key_cmd cmd);
int mt7615_mac_wtbl_update_pk(struct mt7615_dev *dev,
			      struct mt76_wcid *wcid,
			      enum mt7615_cipher_type cipher,
			      int keyidx, enum set_key_cmd cmd);
void mt7615_mac_wtbl_update_cipher(struct mt7615_dev *dev,
				   struct mt76_wcid *wcid,
				   enum mt7615_cipher_type cipher,
				   enum set_key_cmd cmd);
int mt7615_mac_wtbl_update_key(struct mt7615_dev *dev,
			       struct mt76_wcid *wcid,
			       u8 *key, u8 keylen,
			       enum mt7615_cipher_type cipher,
			       enum set_key_cmd cmd);
void mt7615_mac_reset_work(struct work_struct *work);
u32 mt7615_mac_get_sta_tid_sn(struct mt7615_dev *dev, int wcid, u8 tid);

int mt7615_mcu_wait_response(struct mt7615_dev *dev, int cmd, int seq);
int mt7615_mcu_msg_send(struct mt76_dev *mdev, int cmd, const void *data,
			int len, bool wait_resp);
u32 mt7615_rf_rr(struct mt7615_dev *dev, u32 wf, u32 reg);
int mt7615_rf_wr(struct mt7615_dev *dev, u32 wf, u32 reg, u32 val);
int mt7615_mcu_set_dbdc(struct mt7615_dev *dev);
int mt7615_mcu_set_eeprom(struct mt7615_dev *dev);
int mt7615_mcu_set_mac_enable(struct mt7615_dev *dev, int band, bool enable);
int mt7615_mcu_set_rts_thresh(struct mt7615_phy *phy, u32 val);
int mt7615_mcu_get_temperature(struct mt7615_dev *dev, int index);
int mt7615_mcu_set_tx_power(struct mt7615_phy *phy);
void mt7615_mcu_exit(struct mt7615_dev *dev);
void mt7615_mcu_fill_msg(struct mt7615_dev *dev, struct sk_buff *skb,
			 int cmd, int *wait_seq);
int mt7615_mcu_set_channel_domain(struct mt7615_phy *phy);
int mt7615_mcu_hw_scan(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_scan_request *scan_req);
int mt7615_mcu_cancel_hw_scan(struct mt7615_phy *phy,
			      struct ieee80211_vif *vif);
int mt7615_mcu_sched_scan_req(struct mt7615_phy *phy,
			      struct ieee80211_vif *vif,
			      struct cfg80211_sched_scan_request *sreq);
int mt7615_mcu_sched_scan_enable(struct mt7615_phy *phy,
				 struct ieee80211_vif *vif,
				 bool enable);

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
int mt7615_mcu_set_test_param(struct mt7615_dev *dev, u8 param, bool test_mode,
			      u32 val);
int mt7615_mcu_set_sku_en(struct mt7615_phy *phy, bool enable);
int mt7615_mcu_apply_rx_dcoc(struct mt7615_phy *phy);
int mt7615_mcu_apply_tx_dpd(struct mt7615_phy *phy);
int mt7615_mcu_set_vif_ps(struct mt7615_dev *dev, struct ieee80211_vif *vif);
int mt7615_dfs_init_radar_detector(struct mt7615_phy *phy);

int mt7615_mcu_set_p2p_oppps(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif);
int mt7615_mcu_set_roc(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_channel *chan, int duration);

int mt7615_init_debugfs(struct mt7615_dev *dev);
int mt7615_mcu_wait_response(struct mt7615_dev *dev, int cmd, int seq);

int mt7615_mcu_set_bss_pm(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			  bool enable);
int mt7615_mcu_set_hif_suspend(struct mt7615_dev *dev, bool suspend);
void mt7615_mcu_set_suspend_iter(void *priv, u8 *mac,
				 struct ieee80211_vif *vif);
int mt7615_mcu_update_gtk_rekey(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct cfg80211_gtk_rekey_data *key);
int mt7615_mcu_update_arp_filter(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *info);
int __mt7663_load_firmware(struct mt7615_dev *dev);
u32 mt7615_mcu_reg_rr(struct mt76_dev *dev, u32 offset);
void mt7615_mcu_reg_wr(struct mt76_dev *dev, u32 offset, u32 val);

/* usb */
int mt7663_usb_sdio_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
				   enum mt76_txq_id qid, struct mt76_wcid *wcid,
				   struct ieee80211_sta *sta,
				   struct mt76_tx_info *tx_info);
bool mt7663_usb_sdio_tx_status_data(struct mt76_dev *mdev, u8 *update);
void mt7663_usb_sdio_tx_complete_skb(struct mt76_dev *mdev,
				     enum mt76_txq_id qid,
				     struct mt76_queue_entry *e);
void mt7663_usb_sdio_wtbl_work(struct work_struct *work);
int mt7663_usb_sdio_register_device(struct mt7615_dev *dev);
int mt7663u_mcu_init(struct mt7615_dev *dev);

/* sdio */
u32 mt7663s_read_pcr(struct mt7615_dev *dev);
int mt7663s_mcu_init(struct mt7615_dev *dev);
int mt7663s_kthread_run(void *data);
void mt7663s_sdio_irq(struct sdio_func *func);

#endif
