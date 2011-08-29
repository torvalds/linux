/*
 * Shared defines for all mac80211 Prism54 code
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 *
 * Based on the islsm (softmac prism54) driver, which is:
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef P54_H
#define P54_H

#ifdef CONFIG_P54_LEDS
#include <linux/leds.h>
#endif /* CONFIG_P54_LEDS */

#define ISL38XX_DEV_FIRMWARE_ADDR 0x20000

#define BR_CODE_MIN			0x80000000
#define BR_CODE_COMPONENT_ID		0x80000001
#define BR_CODE_COMPONENT_VERSION	0x80000002
#define BR_CODE_DEPENDENT_IF		0x80000003
#define BR_CODE_EXPOSED_IF		0x80000004
#define BR_CODE_DESCR			0x80000101
#define BR_CODE_MAX			0x8FFFFFFF
#define BR_CODE_END_OF_BRA		0xFF0000FF
#define LEGACY_BR_CODE_END_OF_BRA	0xFFFFFFFF

struct bootrec {
	__le32 code;
	__le32 len;
	u32 data[10];
} __packed;

/* Interface role definitions */
#define BR_INTERFACE_ROLE_SERVER	0x0000
#define BR_INTERFACE_ROLE_CLIENT	0x8000

#define BR_DESC_PRIV_CAP_WEP		BIT(0)
#define BR_DESC_PRIV_CAP_TKIP		BIT(1)
#define BR_DESC_PRIV_CAP_MICHAEL	BIT(2)
#define BR_DESC_PRIV_CAP_CCX_CP		BIT(3)
#define BR_DESC_PRIV_CAP_CCX_MIC	BIT(4)
#define BR_DESC_PRIV_CAP_AESCCMP	BIT(5)

struct bootrec_desc {
	__le16 modes;
	__le16 flags;
	__le32 rx_start;
	__le32 rx_end;
	u8 headroom;
	u8 tailroom;
	u8 tx_queues;
	u8 tx_depth;
	u8 privacy_caps;
	u8 rx_keycache_size;
	u8 time_size;
	u8 padding;
	u8 rates[16];
	u8 padding2[4];
	__le16 rx_mtu;
} __packed;

#define FW_FMAC 0x464d4143
#define FW_LM86 0x4c4d3836
#define FW_LM87 0x4c4d3837
#define FW_LM20 0x4c4d3230

struct bootrec_comp_id {
	__le32 fw_variant;
} __packed;

struct bootrec_comp_ver {
	char fw_version[24];
} __packed;

struct bootrec_end {
	__le16 crc;
	u8 padding[2];
	u8 md5[16];
} __packed;

/* provide 16 bytes for the transport back-end */
#define P54_TX_INFO_DATA_SIZE		16

/* stored in ieee80211_tx_info's rate_driver_data */
struct p54_tx_info {
	u32 start_addr;
	u32 end_addr;
	union {
		void *data[P54_TX_INFO_DATA_SIZE / sizeof(void *)];
		struct {
			u32 extra_len;
		};
	};
};

#define P54_MAX_CTRL_FRAME_LEN		0x1000

#define P54_SET_QUEUE(queue, ai_fs, cw_min, cw_max, _txop)	\
do {								\
	queue.aifs = cpu_to_le16(ai_fs);			\
	queue.cwmin = cpu_to_le16(cw_min);			\
	queue.cwmax = cpu_to_le16(cw_max);			\
	queue.txop = cpu_to_le16(_txop);			\
} while (0)

struct p54_edcf_queue_param {
	__le16 aifs;
	__le16 cwmin;
	__le16 cwmax;
	__le16 txop;
} __packed;

struct p54_rssi_db_entry {
	u16 freq;
	s16 mul;
	s16 add;
	s16 longbow_unkn;
	s16 longbow_unk2;
};

struct p54_cal_database {
	size_t entries;
	size_t entry_size;
	size_t offset;
	size_t len;
	u8 data[0];
};

#define EEPROM_READBACK_LEN 0x3fc

enum fw_state {
	FW_STATE_OFF,
	FW_STATE_BOOTING,
	FW_STATE_READY,
	FW_STATE_RESET,
	FW_STATE_RESETTING,
};

#ifdef CONFIG_P54_LEDS

#define P54_LED_MAX_NAME_LEN 31

struct p54_led_dev {
	struct ieee80211_hw *hw_dev;
	struct led_classdev led_dev;
	char name[P54_LED_MAX_NAME_LEN + 1];

	unsigned int toggled;
	unsigned int index;
	unsigned int registered;
};

#endif /* CONFIG_P54_LEDS */

struct p54_tx_queue_stats {
	unsigned int len;
	unsigned int limit;
	unsigned int count;
};

struct p54_common {
	struct ieee80211_hw *hw;
	struct ieee80211_vif *vif;
	void (*tx)(struct ieee80211_hw *dev, struct sk_buff *skb);
	int (*open)(struct ieee80211_hw *dev);
	void (*stop)(struct ieee80211_hw *dev);
	struct sk_buff_head tx_pending;
	struct sk_buff_head tx_queue;
	struct mutex conf_mutex;

	/* memory management (as seen by the firmware) */
	u32 rx_start;
	u32 rx_end;
	u16 rx_mtu;
	u8 headroom;
	u8 tailroom;

	/* firmware/hardware info */
	unsigned int tx_hdr_len;
	unsigned int fw_var;
	unsigned int fw_interface;
	u8 version;

	/* (e)DCF / QOS state */
	bool use_short_slot;
	spinlock_t tx_stats_lock;
	struct p54_tx_queue_stats tx_stats[8];
	struct p54_edcf_queue_param qos_params[8];

	/* Radio data */
	u16 rxhw;
	u8 rx_diversity_mask;
	u8 tx_diversity_mask;
	unsigned int output_power;
	struct p54_rssi_db_entry *cur_rssi;
	struct ieee80211_channel *curchan;
	struct survey_info *survey;
	unsigned int chan_num;
	struct completion stat_comp;
	bool update_stats;
	struct {
		unsigned int timestamp;
		unsigned int cached_cca;
		unsigned int cached_tx;
		unsigned int cached_rssi;
		u64 active;
		u64 cca;
		u64 tx;
		u64 rssi;
	} survey_raw;

	int noise;
	/* calibration, output power limit and rssi<->dBm conversation data */
	struct pda_iq_autocal_entry *iq_autocal;
	unsigned int iq_autocal_len;
	struct p54_cal_database *curve_data;
	struct p54_cal_database *output_limit;
	struct p54_cal_database *rssi_db;
	struct ieee80211_supported_band *band_table[IEEE80211_NUM_BANDS];

	/* BBP/MAC state */
	u8 mac_addr[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	u8 mc_maclist[4][ETH_ALEN];
	u16 wakeup_timer;
	unsigned int filter_flags;
	int mc_maclist_num;
	int mode;
	u32 tsf_low32, tsf_high32;
	u32 basic_rate_mask;
	u16 aid;
	u8 coverage_class;
	bool phy_idle;
	bool phy_ps;
	bool powersave_override;
	__le32 beacon_req_id;
	struct completion beacon_comp;

	/* cryptographic engine information */
	u8 privacy_caps;
	u8 rx_keycache_size;
	unsigned long *used_rxkeys;

	/* LED management */
#ifdef CONFIG_P54_LEDS
	struct p54_led_dev leds[4];
	struct delayed_work led_work;
#endif /* CONFIG_P54_LEDS */
	u16 softled_state;		/* bit field of glowing LEDs */

	/* statistics */
	struct ieee80211_low_level_stats stats;
	struct delayed_work work;

	/* eeprom handling */
	void *eeprom;
	struct completion eeprom_comp;
	struct mutex eeprom_mutex;
};

/* interfaces for the drivers */
int p54_rx(struct ieee80211_hw *dev, struct sk_buff *skb);
void p54_free_skb(struct ieee80211_hw *dev, struct sk_buff *skb);
int p54_parse_firmware(struct ieee80211_hw *dev, const struct firmware *fw);
int p54_parse_eeprom(struct ieee80211_hw *dev, void *eeprom, int len);
int p54_read_eeprom(struct ieee80211_hw *dev);

struct ieee80211_hw *p54_init_common(size_t priv_data_len);
int p54_register_common(struct ieee80211_hw *dev, struct device *pdev);
void p54_free_common(struct ieee80211_hw *dev);

void p54_unregister_common(struct ieee80211_hw *dev);

#endif /* P54_H */
