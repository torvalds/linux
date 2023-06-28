/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2023 MediaTek Inc. */

#ifndef __MT792X_H
#define __MT792X_H

#include <linux/interrupt.h>
#include <linux/ktime.h>

#include "mt76_connac_mcu.h"

struct mt792x_vif;
struct mt792x_sta;

enum {
	MT792x_CLC_POWER,
	MT792x_CLC_CHAN,
	MT792x_CLC_MAX_NUM,
};

DECLARE_EWMA(avg_signal, 10, 8)

struct mt792x_sta {
	struct mt76_wcid wcid; /* must be first */

	struct mt792x_vif *vif;

	u32 airtime_ac[8];

	int ack_signal;
	struct ewma_avg_signal avg_ack_signal;

	unsigned long last_txs;

	struct mt76_connac_sta_key_conf bip;
};

DECLARE_EWMA(rssi, 10, 8);

struct mt792x_vif {
	struct mt76_vif mt76; /* must be first */

	struct mt792x_sta sta;
	struct mt792x_sta *wep_sta;

	struct mt792x_phy *phy;

	struct ewma_rssi rssi;

	struct ieee80211_tx_queue_params queue_params[IEEE80211_NUM_ACS];
	struct ieee80211_chanctx_conf *ctx;
};

struct mt792x_phy {
	struct mt76_phy *mt76;
	struct mt792x_dev *dev;

	struct ieee80211_sband_iftype_data iftype[NUM_NL80211_BANDS][NUM_NL80211_IFTYPES];

	u64 omac_mask;

	u16 noise;

	s16 coverage_class;
	u8 slottime;

	u32 rx_ampdu_ts;
	u32 ampdu_ref;

	struct mt76_mib_stats mib;

	u8 sta_work_count;

	struct sk_buff_head scan_event_list;
	struct delayed_work scan_work;
#ifdef CONFIG_ACPI
	void *acpisar;
#endif
	void *clc[MT792x_CLC_MAX_NUM];

	struct work_struct roc_work;
	struct timer_list roc_timer;
	wait_queue_head_t roc_wait;
	u8 roc_token_id;
	bool roc_grant;
};

struct mt792x_hif_ops {
	int (*init_reset)(struct mt792x_dev *dev);
	int (*reset)(struct mt792x_dev *dev);
	int (*mcu_init)(struct mt792x_dev *dev);
	int (*drv_own)(struct mt792x_dev *dev);
	int (*fw_own)(struct mt792x_dev *dev);
};

struct mt792x_dev {
	union { /* must be first */
		struct mt76_dev mt76;
		struct mt76_phy mphy;
	};

	const struct mt76_bus_ops *bus_ops;
	struct mt792x_phy phy;

	struct work_struct reset_work;
	bool hw_full_reset:1;
	bool hw_init_done:1;
	bool fw_assert:1;
	bool has_eht:1;

	struct work_struct init_work;

	u8 fw_debug;
	u8 fw_features;

	struct mt76_connac_pm pm;
	struct mt76_connac_coredump coredump;
	const struct mt792x_hif_ops *hif_ops;

	struct work_struct ipv6_ns_work;
	/* IPv6 addresses for WoWLAN */
	struct sk_buff_head ipv6_ns_list;

	enum environment_cap country_ie_env;
	u32 backup_l1;
	u32 backup_l2;
};

#endif /* __MT7925_H */
