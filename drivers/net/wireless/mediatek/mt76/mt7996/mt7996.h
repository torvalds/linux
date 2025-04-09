/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#ifndef __MT7996_H
#define __MT7996_H

#include <linux/interrupt.h>
#include <linux/ktime.h>
#include "../mt76_connac.h"
#include "regs.h"

#define MT7996_MAX_RADIOS		3
#define MT7996_MAX_INTERFACES		19	/* per-band */
#define MT7996_MAX_WMM_SETS		4
#define MT7996_WTBL_BMC_SIZE		(is_mt7996(&dev->mt76) ? 64 : 32)
#define MT7996_WTBL_RESERVED		(mt7996_wtbl_size(dev) - 1)
#define MT7996_WTBL_STA			(MT7996_WTBL_RESERVED - \
					 mt7996_max_interface_num(dev))

#define MT7996_WATCHDOG_TIME		(HZ / 10)
#define MT7996_RESET_TIMEOUT		(30 * HZ)

#define MT7996_TX_RING_SIZE		2048
#define MT7996_TX_MCU_RING_SIZE		256
#define MT7996_TX_FWDL_RING_SIZE	128

#define MT7996_RX_RING_SIZE		1536
#define MT7996_RX_MCU_RING_SIZE		512
#define MT7996_RX_MCU_RING_SIZE_WA	1024

#define MT7996_DEVICE_ID		0x7990
#define MT7996_DEVICE_ID_2		0x7991
#define MT7992_DEVICE_ID		0x7992
#define MT7992_DEVICE_ID_2		0x799a
#define MT7990_DEVICE_ID		0x7993
#define MT7990_DEVICE_ID_2		0x799b

#define MT7996_FIRMWARE_WA		"mediatek/mt7996/mt7996_wa.bin"
#define MT7996_FIRMWARE_WM		"mediatek/mt7996/mt7996_wm.bin"
#define MT7996_FIRMWARE_DSP		"mediatek/mt7996/mt7996_dsp.bin"
#define MT7996_ROM_PATCH		"mediatek/mt7996/mt7996_rom_patch.bin"

#define MT7996_FIRMWARE_WA_233		"mediatek/mt7996/mt7996_wa_233.bin"
#define MT7996_FIRMWARE_WM_233		"mediatek/mt7996/mt7996_wm_233.bin"
#define MT7996_FIRMWARE_DSP_233		MT7996_FIRMWARE_DSP
#define MT7996_ROM_PATCH_233		"mediatek/mt7996/mt7996_rom_patch_233.bin"

#define MT7992_FIRMWARE_WA		"mediatek/mt7996/mt7992_wa.bin"
#define MT7992_FIRMWARE_WM		"mediatek/mt7996/mt7992_wm.bin"
#define MT7992_FIRMWARE_DSP		"mediatek/mt7996/mt7992_dsp.bin"
#define MT7992_ROM_PATCH		"mediatek/mt7996/mt7992_rom_patch.bin"

#define MT7992_FIRMWARE_WA_23		"mediatek/mt7996/mt7992_wa_23.bin"
#define MT7992_FIRMWARE_WM_23		"mediatek/mt7996/mt7992_wm_23.bin"
#define MT7992_FIRMWARE_DSP_23		"mediatek/mt7996/mt7992_dsp_23.bin"
#define MT7992_ROM_PATCH_23		"mediatek/mt7996/mt7992_rom_patch_23.bin"

#define MT7990_FIRMWARE_WA		""
#define MT7990_FIRMWARE_WM		"mediatek/mt7996/mt7990_wm.bin"
#define MT7990_FIRMWARE_DSP		""
#define MT7990_ROM_PATCH		"mediatek/mt7996/mt7990_rom_patch.bin"

#define MT7996_EEPROM_DEFAULT		"mediatek/mt7996/mt7996_eeprom.bin"
#define MT7996_EEPROM_DEFAULT_INT	"mediatek/mt7996/mt7996_eeprom_2i5i6i.bin"
#define MT7996_EEPROM_DEFAULT_233	"mediatek/mt7996/mt7996_eeprom_233.bin"
#define MT7996_EEPROM_DEFAULT_233_INT	"mediatek/mt7996/mt7996_eeprom_233_2i5i6i.bin"

#define MT7992_EEPROM_DEFAULT		"mediatek/mt7996/mt7992_eeprom.bin"
#define MT7992_EEPROM_DEFAULT_INT	"mediatek/mt7996/mt7992_eeprom_2i5i.bin"
#define MT7992_EEPROM_DEFAULT_MIX	"mediatek/mt7996/mt7992_eeprom_2i5e.bin"
#define MT7992_EEPROM_DEFAULT_23	"mediatek/mt7996/mt7992_eeprom_23.bin"
#define MT7992_EEPROM_DEFAULT_23_INT	"mediatek/mt7996/mt7992_eeprom_23_2i5i.bin"

#define MT7990_EEPROM_DEFAULT		"mediatek/mt7996/mt7990_eeprom.bin"
#define MT7990_EEPROM_DEFAULT_INT	"mediatek/mt7996/mt7990_eeprom_2i5i.bin"

#define MT7996_EEPROM_SIZE		7680
#define MT7996_EEPROM_BLOCK_SIZE	16
#define MT7996_TOKEN_SIZE		16384
#define MT7996_HW_TOKEN_SIZE		8192

#define MT7996_CFEND_RATE_DEFAULT	0x49	/* OFDM 24M */
#define MT7996_CFEND_RATE_11B		0x03	/* 11B LP, 11M */
#define MT7996_IBF_MAX_NC		2
#define MT7996_IBF_TIMEOUT		0x18
#define MT7996_IBF_TIMEOUT_LEGACY	0x48

#define MT7992_CFEND_RATE_DEFAULT	0x4b	/* OFDM 6M */
#define MT7992_IBF_TIMEOUT		0xff

#define MT7996_SKU_RATE_NUM		417
#define MT7996_SKU_PATH_NUM		494

#define MT7996_MAX_TWT_AGRT		16
#define MT7996_MAX_STA_TWT_AGRT		8
#define MT7996_MIN_TWT_DUR		64
#define MT7996_MAX_QUEUE		(__MT_RXQ_MAX +	__MT_MCUQ_MAX + 3)

/* NOTE: used to map mt76_rates. idx may change if firmware expands table */
#define MT7996_BASIC_RATES_TBL		31
#define MT7996_BEACON_RATES_TBL		25

#define MT7996_THERMAL_THROTTLE_MAX	100
#define MT7996_CDEV_THROTTLE_MAX	99
#define MT7996_CRIT_TEMP_IDX		0
#define MT7996_MAX_TEMP_IDX		1
#define MT7996_CRIT_TEMP		110
#define MT7996_MAX_TEMP			120

#define MT7996_RRO_MAX_SESSION		1024
#define MT7996_RRO_WINDOW_MAX_LEN	1024
#define MT7996_RRO_ADDR_ELEM_LEN	128
#define MT7996_RRO_BA_BITMAP_LEN	2
#define MT7996_RRO_BA_BITMAP_CR_SIZE	((MT7996_RRO_MAX_SESSION * 128) /	\
					 MT7996_RRO_BA_BITMAP_LEN)
#define MT7996_RRO_BA_BITMAP_SESSION_SIZE	(MT7996_RRO_MAX_SESSION /	\
						 MT7996_RRO_ADDR_ELEM_LEN)
#define MT7996_RRO_WINDOW_MAX_SIZE	(MT7996_RRO_WINDOW_MAX_LEN *		\
					 MT7996_RRO_BA_BITMAP_SESSION_SIZE)

#define MT7996_RX_BUF_SIZE		(1800 + \
					 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
#define MT7996_RX_MSDU_PAGE_SIZE	(128 + \
					 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

struct mt7996_vif;
struct mt7996_sta;
struct mt7996_dfs_pulse;
struct mt7996_dfs_pattern;

enum mt7996_ram_type {
	MT7996_RAM_TYPE_WM,
	MT7996_RAM_TYPE_WA,
	MT7996_RAM_TYPE_DSP,
};

enum mt7996_var_type {
	MT7996_VAR_TYPE_444,
	MT7996_VAR_TYPE_233,
};

enum mt7992_var_type {
	MT7992_VAR_TYPE_44,
	MT7992_VAR_TYPE_23,
};

enum mt7990_var_type {
	MT7990_VAR_TYPE_23,
};

enum mt7996_fem_type {
	MT7996_FEM_EXT,
	MT7996_FEM_INT,
	MT7996_FEM_MIX,
};

enum mt7996_txq_id {
	MT7996_TXQ_FWDL = 16,
	MT7996_TXQ_MCU_WM,
	MT7996_TXQ_BAND0,
	MT7996_TXQ_BAND1,
	MT7996_TXQ_MCU_WA,
	MT7996_TXQ_BAND2,
};

enum mt7996_rxq_id {
	MT7996_RXQ_MCU_WM = 0,
	MT7996_RXQ_MCU_WA,
	MT7996_RXQ_MCU_WA_MAIN = 2,
	MT7996_RXQ_MCU_WA_EXT = 3, /* for mt7992 */
	MT7996_RXQ_MCU_WA_TRI = 3,
	MT7996_RXQ_BAND0 = 4,
	MT7996_RXQ_BAND1 = 5, /* for mt7992 */
	MT7996_RXQ_BAND2 = 5,
	MT7996_RXQ_RRO_BAND0 = 8,
	MT7996_RXQ_RRO_BAND1 = 8,/* unused */
	MT7996_RXQ_RRO_BAND2 = 6,
	MT7996_RXQ_MSDU_PG_BAND0 = 10,
	MT7996_RXQ_MSDU_PG_BAND1 = 11,
	MT7996_RXQ_MSDU_PG_BAND2 = 12,
	MT7996_RXQ_TXFREE0 = 9,
	MT7996_RXQ_TXFREE1 = 9,
	MT7996_RXQ_TXFREE2 = 7,
	MT7996_RXQ_RRO_IND = 0,
	MT7990_RXQ_TXFREE0 = 6,
	MT7990_RXQ_TXFREE1 = 7,
};

struct mt7996_twt_flow {
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

DECLARE_EWMA(avg_signal, 10, 8)

struct mt7996_sta_link {
	struct mt76_wcid wcid; /* must be first */

	struct mt7996_sta *sta;

	struct list_head rc_list;
	u32 airtime_ac[8];

	int ack_signal;
	struct ewma_avg_signal avg_ack_signal;

	unsigned long changed;

	struct mt76_connac_sta_key_conf bip;

	struct {
		u8 flowid_mask;
		struct mt7996_twt_flow flow[MT7996_MAX_STA_TWT_AGRT];
	} twt;

	struct rcu_head rcu_head;
};

struct mt7996_sta {
	struct mt7996_sta_link deflink; /* must be first */
	struct mt7996_sta_link __rcu *link[IEEE80211_MLD_MAX_NUM_LINKS];
	u8 deflink_id;

	struct mt7996_vif *vif;
};

struct mt7996_vif_link {
	struct mt76_vif_link mt76; /* must be first */

	struct mt7996_sta_link msta_link;
	struct mt7996_phy *phy;

	struct ieee80211_tx_queue_params queue_params[IEEE80211_NUM_ACS];
	struct cfg80211_bitrate_mask bitrate_mask;
};

struct mt7996_vif {
	struct mt7996_vif_link deflink; /* must be first */
	struct mt76_vif_data mt76;
};

/* crash-dump */
struct mt7996_crash_data {
	guid_t guid;
	struct timespec64 timestamp;

	u8 *memdump_buf;
	size_t memdump_buf_len;
};

struct mt7996_hif {
	struct list_head list;

	struct device *dev;
	void __iomem *regs;
	int irq;
};

struct mt7996_wed_rro_addr {
	u32 head_low;
	u32 head_high : 4;
	u32 count: 11;
	u32 oor: 1;
	u32 rsv : 8;
	u32 signature : 8;
};

struct mt7996_wed_rro_session_id {
	struct list_head list;
	u16 id;
};

struct mt7996_phy {
	struct mt76_phy *mt76;
	struct mt7996_dev *dev;

	struct ieee80211_sband_iftype_data iftype[NUM_NL80211_BANDS][NUM_NL80211_IFTYPES];

	struct thermal_cooling_device *cdev;
	u8 cdev_state;
	u8 throttle_state;
	u32 throttle_temp[2]; /* 0: critical high, 1: maximum */

	u32 rxfilter;
	u64 omac_mask;

	u16 noise;

	s16 coverage_class;
	u8 slottime;

	u8 rdd_state;

	u16 beacon_rate;

	u32 rx_ampdu_ts;
	u32 ampdu_ref;
	int txpower;

	struct mt76_mib_stats mib;
	struct mt76_channel_state state_ts;

	u16 orig_chainmask;

	bool has_aux_rx;
	bool counter_reset;
};

struct mt7996_dev {
	union { /* must be first */
		struct mt76_dev mt76;
		struct mt76_phy mphy;
	};

	struct mt7996_phy *radio_phy[MT7996_MAX_RADIOS];
	struct wiphy_radio radios[MT7996_MAX_RADIOS];
	struct wiphy_radio_freq_range radio_freqs[MT7996_MAX_RADIOS];

	struct mt7996_hif *hif2;
	struct mt7996_reg_desc reg;
	u8 q_id[MT7996_MAX_QUEUE];
	u32 q_int_mask[MT7996_MAX_QUEUE];
	u32 q_wfdma_mask;

	const struct mt76_bus_ops *bus_ops;
	struct mt7996_phy phy;

	/* monitor rx chain configured channel */
	struct cfg80211_chan_def rdd2_chandef;
	struct mt7996_phy *rdd2_phy;

	u16 chainmask;
	u8 chainshift[__MT_MAX_BAND];
	u32 hif_idx;

	struct work_struct init_work;
	struct work_struct rc_work;
	struct work_struct dump_work;
	struct work_struct reset_work;
	wait_queue_head_t reset_wait;
	struct {
		u32 state;
		u32 wa_reset_count;
		u32 wm_reset_count;
		bool hw_full_reset:1;
		bool hw_init_done:1;
		bool restart:1;
	} recovery;

	/* protects coredump data */
	struct mutex dump_mutex;
#ifdef CONFIG_DEV_COREDUMP
	struct {
		struct mt7996_crash_data *crash_data;
	} coredump;
#endif

	struct list_head sta_rc_list;
	struct list_head twt_list;

	u32 hw_pattern;

	bool flash_mode:1;
	bool has_eht:1;
	bool has_rro:1;

	struct {
		struct {
			void *ptr;
			dma_addr_t phy_addr;
		} ba_bitmap[MT7996_RRO_BA_BITMAP_LEN];
		struct {
			void *ptr;
			dma_addr_t phy_addr;
		} addr_elem[MT7996_RRO_ADDR_ELEM_LEN];
		struct {
			void *ptr;
			dma_addr_t phy_addr;
		} session;

		struct work_struct work;
		struct list_head poll_list;
		spinlock_t lock;
	} wed_rro;

	bool ibf;
	u8 fw_debug_wm;
	u8 fw_debug_wa;
	u8 fw_debug_bin;
	u16 fw_debug_seq;

	struct dentry *debugfs_dir;
	struct rchan *relay_fwlog;

	struct {
		u16 table_mask;
		u8 n_agrt;
	} twt;

	spinlock_t reg_lock;

	u8 wtbl_size_group;
	struct {
		u8 type:4;
		u8 fem:4;
	} var;
};

enum {
	WFDMA0 = 0x0,
	WFDMA1,
	WFDMA_EXT,
	__MT_WFDMA_MAX,
};

enum {
	MT_RX_SEL0,
	MT_RX_SEL1,
	MT_RX_SEL2, /* monitor chain */
};

enum mt7996_rdd_cmd {
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

static inline struct mt7996_dev *
mt7996_hw_dev(struct ieee80211_hw *hw)
{
	struct mt76_phy *phy = hw->priv;

	return container_of(phy->dev, struct mt7996_dev, mt76);
}

static inline struct mt7996_phy *
__mt7996_phy(struct mt7996_dev *dev, enum mt76_band_id band)
{
	struct mt76_phy *phy = dev->mt76.phys[band];

	if (!phy)
		return NULL;

	return phy->priv;
}

static inline struct mt7996_phy *
mt7996_phy2(struct mt7996_dev *dev)
{
	return __mt7996_phy(dev, MT_BAND1);
}

static inline struct mt7996_phy *
mt7996_phy3(struct mt7996_dev *dev)
{
	return __mt7996_phy(dev, MT_BAND2);
}

static inline bool
mt7996_band_valid(struct mt7996_dev *dev, u8 band)
{
	if (!is_mt7996(&dev->mt76))
		return band <= MT_BAND1;

	return band <= MT_BAND2;
}

static inline bool
mt7996_has_background_radar(struct mt7996_dev *dev)
{
	switch (mt76_chip(&dev->mt76)) {
	case MT7996_DEVICE_ID:
		if (dev->var.type == MT7996_VAR_TYPE_233)
			return false;
		break;
	case MT7992_DEVICE_ID:
		if (dev->var.type == MT7992_VAR_TYPE_23)
			return false;
		break;
	default:
		return false;
	}

	return true;
}

static inline struct mt7996_phy *
mt7996_band_phy(struct mt7996_dev *dev, enum nl80211_band band)
{
	struct mt76_phy *mphy;

	mphy = dev->mt76.band_phys[band];
	if (!mphy)
		return NULL;

	return mphy->priv;
}

static inline struct mt7996_vif_link *
mt7996_vif_link(struct mt7996_dev *dev, struct ieee80211_vif *vif, int link_id)
{
	return (struct mt7996_vif_link *)mt76_vif_link(&dev->mt76, vif, link_id);
}

static inline struct mt7996_phy *
mt7996_vif_link_phy(struct mt7996_vif_link *link)
{
	struct mt76_phy *mphy = mt76_vif_link_phy(&link->mt76);

	if (!mphy)
		return NULL;

	return mphy->priv;
}

static inline struct mt7996_vif_link *
mt7996_vif_conf_link(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		     struct ieee80211_bss_conf *link_conf)
{
	return (struct mt7996_vif_link *)mt76_vif_conf_link(&dev->mt76, vif,
							    link_conf);
}

#define mt7996_for_each_phy(dev, phy)					\
	for (int __i = 0; __i < ARRAY_SIZE((dev)->radio_phy); __i++)	\
		if (((phy) = (dev)->radio_phy[__i]) != NULL)

extern const struct ieee80211_ops mt7996_ops;
extern struct pci_driver mt7996_pci_driver;
extern struct pci_driver mt7996_hif_driver;

struct mt7996_dev *mt7996_mmio_probe(struct device *pdev,
				     void __iomem *mem_base, u32 device_id);
void mt7996_wfsys_reset(struct mt7996_dev *dev);
irqreturn_t mt7996_irq_handler(int irq, void *dev_instance);
u64 __mt7996_get_tsf(struct ieee80211_hw *hw, struct mt7996_vif_link *link);
int mt7996_register_device(struct mt7996_dev *dev);
void mt7996_unregister_device(struct mt7996_dev *dev);
int mt7996_vif_link_add(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *link_conf,
			struct mt76_vif_link *mlink);
void mt7996_vif_link_remove(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink);
int mt7996_eeprom_init(struct mt7996_dev *dev);
int mt7996_eeprom_parse_hw_cap(struct mt7996_dev *dev, struct mt7996_phy *phy);
int mt7996_eeprom_get_target_power(struct mt7996_dev *dev,
				   struct ieee80211_channel *chan);
s8 mt7996_eeprom_get_power_delta(struct mt7996_dev *dev, int band);
int mt7996_dma_init(struct mt7996_dev *dev);
void mt7996_dma_reset(struct mt7996_dev *dev, bool force);
void mt7996_dma_prefetch(struct mt7996_dev *dev);
void mt7996_dma_cleanup(struct mt7996_dev *dev);
void mt7996_dma_start(struct mt7996_dev *dev, bool reset, bool wed_reset);
int mt7996_init_tx_queues(struct mt7996_phy *phy, int idx,
			  int n_desc, int ring_base, struct mtk_wed_device *wed);
void mt7996_init_txpower(struct mt7996_phy *phy);
int mt7996_txbf_init(struct mt7996_dev *dev);
void mt7996_reset(struct mt7996_dev *dev);
int mt7996_run(struct mt7996_phy *phy);
int mt7996_mcu_init(struct mt7996_dev *dev);
int mt7996_mcu_init_firmware(struct mt7996_dev *dev);
int mt7996_mcu_twt_agrt_update(struct mt7996_dev *dev,
			       struct mt7996_vif_link *link,
			       struct mt7996_twt_flow *flow,
			       int cmd);
int mt7996_mcu_add_dev_info(struct mt7996_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink, bool enable);
int mt7996_mcu_add_bss_info(struct mt7996_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink,
			    struct mt7996_sta_link *msta_link, int enable);
int mt7996_mcu_add_sta(struct mt7996_dev *dev,
		       struct ieee80211_bss_conf *link_conf,
		       struct ieee80211_link_sta *link_sta,
		       struct mt7996_vif_link *link,
		       struct mt7996_sta_link *msta_link,
		       int conn_state, bool newly);
int mt7996_mcu_teardown_mld_sta(struct mt7996_dev *dev,
				struct mt7996_vif_link *link,
				struct mt7996_sta_link *msta_link);
int mt7996_mcu_add_tx_ba(struct mt7996_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 struct mt7996_vif_link *link,
			 struct mt7996_sta_link *msta_link, bool enable);
int mt7996_mcu_add_rx_ba(struct mt7996_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 struct mt7996_vif_link *link, bool enable);
int mt7996_mcu_update_bss_color(struct mt7996_dev *dev,
				struct mt76_vif_link *mlink,
				struct cfg80211_he_bss_color *he_bss_color);
int mt7996_mcu_add_beacon(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link_conf);
int mt7996_mcu_beacon_inband_discov(struct mt7996_dev *dev,
				    struct ieee80211_bss_conf *link_conf,
				    struct mt7996_vif_link *link, u32 changed);
int mt7996_mcu_add_obss_spr(struct mt7996_phy *phy,
			    struct mt7996_vif_link *link,
			    struct ieee80211_he_obss_pd *he_obss_pd);
int mt7996_mcu_add_rate_ctrl(struct mt7996_dev *dev,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *link_conf,
			     struct ieee80211_link_sta *link_sta,
			     struct mt7996_vif_link *link,
			     struct mt7996_sta_link *msta_link, bool changed);
int mt7996_set_channel(struct mt76_phy *mphy);
int mt7996_mcu_set_chan_info(struct mt7996_phy *phy, u16 tag);
int mt7996_mcu_set_tx(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		      struct ieee80211_bss_conf *link_conf);
int mt7996_mcu_set_fixed_rate_ctrl(struct mt7996_dev *dev,
				   void *data, u16 version);
int mt7996_mcu_set_fixed_field(struct mt7996_dev *dev,
			       struct ieee80211_link_sta *link_sta,
			       struct mt7996_vif_link *link,
			       struct mt7996_sta_link *msta_link,
			       void *data, u32 field);
int mt7996_mcu_set_eeprom(struct mt7996_dev *dev);
int mt7996_mcu_get_eeprom(struct mt7996_dev *dev, u32 offset, u8 *buf, u32 buf_len);
int mt7996_mcu_get_eeprom_free_block(struct mt7996_dev *dev, u8 *block_num);
int mt7996_mcu_get_chip_config(struct mt7996_dev *dev, u32 *cap);
int mt7996_mcu_set_ser(struct mt7996_dev *dev, u8 action, u8 set, u8 band);
int mt7996_mcu_set_txbf(struct mt7996_dev *dev, u8 action);
int mt7996_mcu_set_fcc5_lpn(struct mt7996_dev *dev, int val);
int mt7996_mcu_set_pulse_th(struct mt7996_dev *dev,
			    const struct mt7996_dfs_pulse *pulse);
int mt7996_mcu_set_radar_th(struct mt7996_dev *dev, int index,
			    const struct mt7996_dfs_pattern *pattern);
int mt7996_mcu_set_radio_en(struct mt7996_phy *phy, bool enable);
int mt7996_mcu_set_rts_thresh(struct mt7996_phy *phy, u32 val);
int mt7996_mcu_set_timing(struct mt7996_phy *phy, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link_conf);
int mt7996_mcu_get_chan_mib_info(struct mt7996_phy *phy, bool chan_switch);
int mt7996_mcu_get_temperature(struct mt7996_phy *phy);
int mt7996_mcu_set_thermal_throttling(struct mt7996_phy *phy, u8 state);
int mt7996_mcu_set_thermal_protect(struct mt7996_phy *phy, bool enable);
int mt7996_mcu_set_txpower_sku(struct mt7996_phy *phy);
int mt7996_mcu_rdd_cmd(struct mt7996_dev *dev, int cmd, u8 index,
		       u8 rx_sel, u8 val);
int mt7996_mcu_rdd_background_enable(struct mt7996_phy *phy,
				     struct cfg80211_chan_def *chandef);
int mt7996_mcu_set_fixed_rate_table(struct mt7996_phy *phy, u8 table_idx,
				    u16 rate_idx, bool beacon);
int mt7996_mcu_rf_regval(struct mt7996_dev *dev, u32 regidx, u32 *val, bool set);
int mt7996_mcu_set_hdr_trans(struct mt7996_dev *dev, bool hdr_trans);
int mt7996_mcu_set_rro(struct mt7996_dev *dev, u16 tag, u16 val);
int mt7996_mcu_wa_cmd(struct mt7996_dev *dev, int cmd, u32 a1, u32 a2, u32 a3);
int mt7996_mcu_fw_log_2_host(struct mt7996_dev *dev, u8 type, u8 ctrl);
int mt7996_mcu_fw_dbg_ctrl(struct mt7996_dev *dev, u32 module, u8 level);
int mt7996_mcu_trigger_assert(struct mt7996_dev *dev);
void mt7996_mcu_rx_event(struct mt7996_dev *dev, struct sk_buff *skb);
void mt7996_mcu_exit(struct mt7996_dev *dev);
int mt7996_mcu_get_all_sta_info(struct mt7996_phy *phy, u16 tag);
int mt7996_mcu_wed_rro_reset_sessions(struct mt7996_dev *dev, u16 id);
int mt7996_mcu_set_sniffer_mode(struct mt7996_phy *phy, bool enabled);

static inline u8 mt7996_max_interface_num(struct mt7996_dev *dev)
{
	return min(MT7996_MAX_INTERFACES * (1 + mt7996_band_valid(dev, MT_BAND1) +
					    mt7996_band_valid(dev, MT_BAND2)),
		   MT7996_WTBL_BMC_SIZE);
}

static inline u16 mt7996_wtbl_size(struct mt7996_dev *dev)
{
	return (dev->wtbl_size_group << 8) + MT7996_WTBL_BMC_SIZE;
}

void mt7996_dual_hif_set_irq_mask(struct mt7996_dev *dev, bool write_reg,
				  u32 clear, u32 set);

static inline void mt7996_irq_enable(struct mt7996_dev *dev, u32 mask)
{
	if (dev->hif2)
		mt7996_dual_hif_set_irq_mask(dev, false, 0, mask);
	else
		mt76_set_irq_mask(&dev->mt76, 0, 0, mask);

	tasklet_schedule(&dev->mt76.irq_tasklet);
}

static inline void mt7996_irq_disable(struct mt7996_dev *dev, u32 mask)
{
	if (dev->hif2)
		mt7996_dual_hif_set_irq_mask(dev, true, mask, 0);
	else
		mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, mask, 0);
}

void mt7996_memcpy_fromio(struct mt7996_dev *dev, void *buf, u32 offset,
			  size_t len);

static inline u16 mt7996_rx_chainmask(struct mt7996_phy *phy)
{
	int max_nss = hweight8(phy->mt76->hw->wiphy->available_antennas_tx);
	int cur_nss = hweight8(phy->mt76->antenna_mask);
	u16 tx_chainmask = phy->mt76->chainmask;

	if (cur_nss != max_nss)
		return tx_chainmask;

	return tx_chainmask | (BIT(fls(tx_chainmask)) * phy->has_aux_rx);
}

static inline bool mt7996_has_wa(struct mt7996_dev *dev)
{
	return !is_mt7990(&dev->mt76);
}

void mt7996_mac_init(struct mt7996_dev *dev);
u32 mt7996_mac_wtbl_lmac_addr(struct mt7996_dev *dev, u16 wcid, u8 dw);
bool mt7996_mac_wtbl_update(struct mt7996_dev *dev, int idx, u32 mask);
void mt7996_mac_reset_counters(struct mt7996_phy *phy);
void mt7996_mac_cca_stats_reset(struct mt7996_phy *phy);
void mt7996_mac_enable_nf(struct mt7996_dev *dev, u8 band);
void mt7996_mac_write_txwi(struct mt7996_dev *dev, __le32 *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid,
			   struct ieee80211_key_conf *key, int pid,
			   enum mt76_txq_id qid, u32 changed);
void mt7996_mac_set_coverage_class(struct mt7996_phy *phy);
void mt7996_mac_work(struct work_struct *work);
void mt7996_mac_reset_work(struct work_struct *work);
void mt7996_mac_dump_work(struct work_struct *work);
void mt7996_mac_sta_rc_work(struct work_struct *work);
void mt7996_mac_update_stats(struct mt7996_phy *phy);
void mt7996_mac_twt_teardown_flow(struct mt7996_dev *dev,
				  struct mt7996_vif_link *link,
				  struct mt7996_sta_link *msta_link,
				  u8 flowid);
void mt7996_mac_add_twt_setup(struct ieee80211_hw *hw,
			      struct ieee80211_sta *sta,
			      struct ieee80211_twt_setup *twt);
int mt7996_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info);
void mt7996_tx_token_put(struct mt7996_dev *dev);
void mt7996_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb, u32 *info);
bool mt7996_rx_check(struct mt76_dev *mdev, void *data, int len);
void mt7996_stats_work(struct work_struct *work);
int mt76_dfs_start_rdd(struct mt7996_dev *dev, bool force);
int mt7996_dfs_init_radar_detector(struct mt7996_phy *phy);
void mt7996_set_stream_he_eht_caps(struct mt7996_phy *phy);
void mt7996_set_stream_vht_txbf_caps(struct mt7996_phy *phy);
void mt7996_update_channel(struct mt76_phy *mphy);
int mt7996_init_debugfs(struct mt7996_dev *dev);
void mt7996_debugfs_rx_fw_monitor(struct mt7996_dev *dev, const void *data, int len);
bool mt7996_debugfs_rx_log(struct mt7996_dev *dev, const void *data, int len);
int mt7996_mcu_add_key(struct mt76_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_key_conf *key, int mcu_cmd,
		       struct mt76_wcid *wcid, enum set_key_cmd cmd);
int mt7996_mcu_bcn_prot_enable(struct mt7996_dev *dev,
			       struct mt7996_vif_link *link,
			       struct mt7996_sta_link *msta_link,
			       struct ieee80211_key_conf *key);
int mt7996_mcu_wtbl_update_hdr_trans(struct mt7996_dev *dev,
				     struct ieee80211_vif *vif,
				     struct mt7996_vif_link *link,
				     struct mt7996_sta_link *msta_link);
int mt7996_mcu_cp_support(struct mt7996_dev *dev, u8 mode);
#ifdef CONFIG_MAC80211_DEBUGFS
void mt7996_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir);
#endif
int mt7996_mmio_wed_init(struct mt7996_dev *dev, void *pdev_ptr,
			 bool hif2, int *irq);
u32 mt7996_wed_init_buf(void *ptr, dma_addr_t phys, int token_id);

#ifdef CONFIG_MTK_DEBUG
int mt7996_mtk_init_debugfs(struct mt7996_phy *phy, struct dentry *dir);
#endif

#ifdef CONFIG_NET_MEDIATEK_SOC_WED
int mt7996_dma_rro_init(struct mt7996_dev *dev);
#endif /* CONFIG_NET_MEDIATEK_SOC_WED */

#endif
