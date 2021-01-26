/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT76_CONNAC_H
#define __MT76_CONNAC_H

#include "mt76.h"

#define MT76_CONNAC_SCAN_IE_LEN			600
#define MT76_CONNAC_MAX_SCHED_SCAN_INTERVAL	10
#define MT76_CONNAC_MAX_SCHED_SCAN_SSID		10
#define MT76_CONNAC_MAX_SCAN_MATCH		16

enum {
	CMD_CBW_20MHZ = IEEE80211_STA_RX_BW_20,
	CMD_CBW_40MHZ = IEEE80211_STA_RX_BW_40,
	CMD_CBW_80MHZ = IEEE80211_STA_RX_BW_80,
	CMD_CBW_160MHZ = IEEE80211_STA_RX_BW_160,
	CMD_CBW_10MHZ,
	CMD_CBW_5MHZ,
	CMD_CBW_8080MHZ,

	CMD_HE_MCS_BW80 = 0,
	CMD_HE_MCS_BW160,
	CMD_HE_MCS_BW8080,
	CMD_HE_MCS_BW_NUM
};

enum {
	HW_BSSID_0 = 0x0,
	HW_BSSID_1,
	HW_BSSID_2,
	HW_BSSID_3,
	HW_BSSID_MAX = HW_BSSID_3,
	EXT_BSSID_START = 0x10,
	EXT_BSSID_1,
	EXT_BSSID_15 = 0x1f,
	EXT_BSSID_MAX = EXT_BSSID_15,
	REPEATER_BSSID_START = 0x20,
	REPEATER_BSSID_MAX = 0x3f,
};

struct mt76_connac_pm {
	bool enable;

	spinlock_t txq_lock;
	struct {
		struct mt76_wcid *wcid;
		struct sk_buff *skb;
	} tx_q[IEEE80211_NUM_ACS];

	struct work_struct wake_work;
	struct completion wake_cmpl;

	struct delayed_work ps_work;
	unsigned long last_activity;
	unsigned long idle_timeout;
};

extern const struct wiphy_wowlan_support mt76_connac_wowlan_support;

#endif /* __MT76_CONNAC_H */
