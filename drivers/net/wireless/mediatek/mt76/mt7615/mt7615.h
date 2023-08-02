/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2019 MediaTek Inc. */

#ifndef __MT7615_H
#define __MT7615_H

#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/regmap.h>
#include "../mt76_connac_mcu.h"
#include "regs.h"

#define MT7615_MAX_INTERFACES		16
#define MT7615_MAX_WMM_SETS		4
#define MT7663_WTBL_SIZE		32
#define MT7615_WTBL_SIZE		128
#define MT7615_WTBL_RESERVED		(mt7615_wtbl_size(dev) - 1)
#define MT7615_WTBL_STA			(MT7615_WTBL_RESERVED - \
					 MT7615_MAX_INTERFACES)

#define MT7615_PM_TIMEOUT		(HZ / 12)
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
#define MT7663_EEPROM_SIZE		1536
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

struct mt7615_wtbl_rate_desc {
	struct list_head node;

	struct mt7615_rate_desc rate;
	struct mt7615_sta *sta;
};

struct mt7663s_intr {
	u32 isr;
	struct {
		u32 wtqcr[8];
	} tx;
	struct {
		u16 num[2];
		u16 len[2][16];
	} rx;
	u32 rec_mb[2];
} __packed;

struct mt7615_sta {
	struct mt76_wcid wcid; /* must be first */

	struct mt7615_vif *vif;

	u32 airtime_ac[8];

	struct ieee80211_tx_rate rates[4];

	struct mt7615_rate_set rateset[2];
	u32 rate_set_tsf;

	u8 rate_count;
	u8 n_rates;

	u8 rate_probe;
};

struct mt7615_vif {
	struct mt76_vif mt76; /* must be first */
	struct mt7615_sta sta;
	bool sta_added;
};

struct mib_stats {
	u32 ack_fail_cnt;
	u32 fcs_err_cnt;
	u32 rts_cnt;
	u32 rts_retries_cnt;
	u32 ba_miss_cnt;
	unsigned long aggr_per;
};

struct mt7615_phy {
	struct mt76_phy *mt76;
	struct mt7615_dev *dev;

	struct ieee80211_vif *monitor_vif;

	u8 n_beacon_vif;

	u32 rxfilter;
	u64 omac_mask;

	u16 noise;

	bool scs_en;

	unsigned long last_cca_adj;
	int false_cca_ofdm, false_cca_cck;
	s8 ofdm_sensitivity;
	s8 cck_sensitivity;

	s16 coverage_class;
	u8 slottime;

	u8 chfreq;
	u8 rdd_state;

	u32 rx_ampdu_ts;
	u32 ampdu_ref;

	struct mib_stats mib;

	struct sk_buff_head scan_event_list;
	struct delayed_work scan_work;

	struct work_struct roc_work;
	struct timer_list roc_timer;
	wait_queue_head_t roc_wait;
	bool roc_grant;

#ifdef CONFIG_NL80211_TESTMODE
	struct {
		u32 *reg_backup;

		s16 last_freq_offset;
		u8 last_rcpi[4];
		s8 last_ib_rssi[4];
		s8 last_wb_rssi[4];
	} test;
#endif
};

#define mt7615_mcu_add_tx_ba(dev, ...)	(dev)->mcu_ops->add_tx_ba((dev), __VA_ARGS__)
#define mt7615_mcu_add_rx_ba(dev, ...)	(dev)->mcu_ops->add_rx_ba((dev), __VA_ARGS__)
#define mt7615_mcu_sta_add(phy, ...)	((phy)->dev)->mcu_ops->sta_add((phy),  __VA_ARGS__)
#define mt7615_mcu_add_dev_info(phy, ...) ((phy)->dev)->mcu_ops->add_dev_info((phy),  __VA_ARGS__)
#define mt7615_mcu_add_bss_info(phy, ...) ((phy)->dev)->mcu_ops->add_bss_info((phy),  __VA_ARGS__)
#define mt7615_mcu_add_beacon(dev, ...)	(dev)->mcu_ops->add_beacon_offload((dev),  __VA_ARGS__)
#define mt7615_mcu_set_pm(dev, ...)	(dev)->mcu_ops->set_pm_state((dev),  __VA_ARGS__)
#define mt7615_mcu_set_drv_ctrl(dev)	(dev)->mcu_ops->set_drv_ctrl((dev))
#define mt7615_mcu_set_fw_ctrl(dev)	(dev)->mcu_ops->set_fw_ctrl((dev))
#define mt7615_mcu_set_sta_decap_offload(dev, ...) (dev)->mcu_ops->set_sta_decap_offload((dev), __VA_ARGS__)
struct mt7615_mcu_ops {
	int (*add_tx_ba)(struct mt7615_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable);
	int (*add_rx_ba)(struct mt7615_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable);
	int (*sta_add)(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, bool enable);
	int (*add_dev_info)(struct mt7615_phy *phy, struct ieee80211_vif *vif,
			    bool enable);
	int (*add_bss_info)(struct mt7615_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, bool enable);
	int (*add_beacon_offload)(struct mt7615_dev *dev,
				  struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif, bool enable);
	int (*set_pm_state)(struct mt7615_dev *dev, int band, int state);
	int (*set_drv_ctrl)(struct mt7615_dev *dev);
	int (*set_fw_ctrl)(struct mt7615_dev *dev);
	int (*set_sta_decap_offload)(struct mt7615_dev *dev,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta);
};

struct mt7615_dev {
	union { /* must be first */
		struct mt76_dev mt76;
		struct mt76_phy mphy;
	};

	const struct mt76_bus_ops *bus_ops;
	struct mt7615_phy phy;
	u64 omac_mask;

	u16 chainmask;

	struct ieee80211_ops *ops;
	const struct mt7615_mcu_ops *mcu_ops;
	struct regmap *infracfg;
	const u32 *reg_map;

	struct work_struct mcu_work;

	struct work_struct reset_work;
	wait_queue_head_t reset_wait;
	u32 reset_state;

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

	u8 fw_ver;

	struct work_struct rate_work;
	struct list_head wrd_head;

	u32 debugfs_rf_wf;
	u32 debugfs_rf_reg;

	u32 muar_mask;

	struct mt76_connac_pm pm;
	struct mt76_connac_coredump coredump;
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
	struct mt76_phy *phy = dev->mt76.phys[MT_BAND1];

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

int mt7615_thermal_init(struct mt7615_dev *dev);
int mt7615_mmio_probe(struct device *pdev, void __iomem *mem_base,
		      int irq, const u32 *map);
u32 mt7615_reg_map(struct mt7615_dev *dev, u32 addr);

u32 mt7615_reg_map(struct mt7615_dev *dev, u32 addr);
int mt7615_led_set_blink(struct led_classdev *led_cdev,
			 unsigned long *delay_on,
			 unsigned long *delay_off);
void mt7615_led_set_brightness(struct led_classdev *led_cdev,
			       enum led_brightness brightness);
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
void mt7615_dma_start(struct mt7615_dev *dev);
void mt7615_dma_cleanup(struct mt7615_dev *dev);
int mt7615_mcu_init(struct mt7615_dev *dev);
bool mt7615_wait_for_mcu_init(struct mt7615_dev *dev);
void mt7615_mac_set_rates(struct mt7615_phy *phy, struct mt7615_sta *sta,
			  struct ieee80211_tx_rate *probe_rate,
			  struct ieee80211_tx_rate *rates);
void mt7615_pm_wake_work(struct work_struct *work);
void mt7615_pm_power_save_work(struct work_struct *work);
int mt7615_mcu_del_wtbl_all(struct mt7615_dev *dev);
int mt7615_mcu_set_chan_info(struct mt7615_phy *phy, int cmd);
int mt7615_mcu_set_wmm(struct mt7615_dev *dev, u8 queue,
		       const struct ieee80211_tx_queue_params *params);
void mt7615_mcu_rx_event(struct mt7615_dev *dev, struct sk_buff *skb);
int mt7615_mcu_rdd_send_pattern(struct mt7615_dev *dev);
int mt7615_mcu_fw_log_2_host(struct mt7615_dev *dev, u8 ctrl);

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

#define mt7615_mutex_acquire(dev)	\
	mt76_connac_mutex_acquire(&(dev)->mt76, &(dev)->pm)
#define mt7615_mutex_release(dev)	\
	mt76_connac_mutex_release(&(dev)->mt76, &(dev)->pm)

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

static inline u32 mt7615_tx_mcu_int_mask(struct mt7615_dev *dev)
{
	return MT_INT_TX_DONE(dev->mt76.q_mcu[MT_MCUQ_WM]->hw_idx);
}

static inline unsigned long
mt7615_get_macwork_timeout(struct mt7615_dev *dev)
{
	return dev->pm.enable ? HZ / 3 : HZ / 10;
}

void mt7615_dma_reset(struct mt7615_dev *dev);
void mt7615_scan_work(struct work_struct *work);
void mt7615_roc_work(struct work_struct *work);
void mt7615_roc_timer(struct timer_list *timer);
void mt7615_init_txpower(struct mt7615_dev *dev,
			 struct ieee80211_supported_band *sband);
int mt7615_set_channel(struct mt7615_phy *phy);
void mt7615_init_work(struct mt7615_dev *dev);

int mt7615_mcu_restart(struct mt76_dev *dev);
void mt7615_update_channel(struct mt76_phy *mphy);
bool mt7615_mac_wtbl_update(struct mt7615_dev *dev, int idx, u32 mask);
void mt7615_mac_reset_counters(struct mt7615_phy *phy);
void mt7615_mac_cca_stats_reset(struct mt7615_phy *phy);
void mt7615_mac_set_scs(struct mt7615_phy *phy, bool enable);
void mt7615_mac_enable_nf(struct mt7615_dev *dev, bool ext_phy);
void mt7615_mac_enable_rtscts(struct mt7615_dev *dev,
			      struct ieee80211_vif *vif, bool enable);
void mt7615_mac_sta_poll(struct mt7615_dev *dev);
int mt7615_mac_write_txwi(struct mt7615_dev *dev, __le32 *txwi,
			  struct sk_buff *skb, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta, int pid,
			  struct ieee80211_key_conf *key,
			  enum mt76_txq_id qid, bool beacon);
void mt7615_mac_set_timing(struct mt7615_phy *phy);
int __mt7615_mac_wtbl_set_key(struct mt7615_dev *dev,
			      struct mt76_wcid *wcid,
			      struct ieee80211_key_conf *key);
int mt7615_mac_wtbl_set_key(struct mt7615_dev *dev, struct mt76_wcid *wcid,
			    struct ieee80211_key_conf *key);
void mt7615_mac_reset_work(struct work_struct *work);
u32 mt7615_mac_get_sta_tid_sn(struct mt7615_dev *dev, int wcid, u8 tid);

int mt7615_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			      struct sk_buff *skb, int seq);
u32 mt7615_rf_rr(struct mt7615_dev *dev, u32 wf, u32 reg);
int mt7615_rf_wr(struct mt7615_dev *dev, u32 wf, u32 reg, u32 val);
int mt7615_mcu_set_dbdc(struct mt7615_dev *dev);
int mt7615_mcu_set_eeprom(struct mt7615_dev *dev);
int mt7615_mcu_get_temperature(struct mt7615_dev *dev);
int mt7615_mcu_set_tx_power(struct mt7615_phy *phy);
void mt7615_mcu_exit(struct mt7615_dev *dev);
void mt7615_mcu_fill_msg(struct mt7615_dev *dev, struct sk_buff *skb,
			 int cmd, int *wait_seq);

int mt7615_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info);

void mt7615_tx_worker(struct mt76_worker *w);
void mt7615_tx_token_put(struct mt7615_dev *dev);
bool mt7615_rx_check(struct mt76_dev *mdev, void *data, int len);
void mt7615_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb, u32 *info);
int mt7615_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
void mt7615_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);
void mt7615_mac_work(struct work_struct *work);
int mt7615_mcu_set_rx_hdr_trans_blacklist(struct mt7615_dev *dev);
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
int mt7615_dfs_init_radar_detector(struct mt7615_phy *phy);

int mt7615_mcu_set_roc(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_channel *chan, int duration);

int mt7615_init_debugfs(struct mt7615_dev *dev);
int mt7615_mcu_wait_response(struct mt7615_dev *dev, int cmd, int seq);

int mt7615_mac_set_beacon_filter(struct mt7615_phy *phy,
				 struct ieee80211_vif *vif,
				 bool enable);
int mt7615_mcu_set_bss_pm(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			  bool enable);
int __mt7663_load_firmware(struct mt7615_dev *dev);
void mt7615_coredump_work(struct work_struct *work);

void mt7622_trigger_hif_int(struct mt7615_dev *dev, bool en);

/* usb */
int mt7663_usb_sdio_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
				   enum mt76_txq_id qid, struct mt76_wcid *wcid,
				   struct ieee80211_sta *sta,
				   struct mt76_tx_info *tx_info);
bool mt7663_usb_sdio_tx_status_data(struct mt76_dev *mdev, u8 *update);
void mt7663_usb_sdio_tx_complete_skb(struct mt76_dev *mdev,
				     struct mt76_queue_entry *e);
int mt7663_usb_sdio_register_device(struct mt7615_dev *dev);
int mt7663u_mcu_init(struct mt7615_dev *dev);
int mt7663u_mcu_power_on(struct mt7615_dev *dev);

/* sdio */
int mt7663s_mcu_init(struct mt7615_dev *dev);

#endif
