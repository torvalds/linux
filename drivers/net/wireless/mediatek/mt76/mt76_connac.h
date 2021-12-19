/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT76_CONNAC_H
#define __MT76_CONNAC_H

#include "mt76.h"

#define MT76_CONNAC_SCAN_IE_LEN			600
#define MT76_CONNAC_MAX_NUM_SCHED_SCAN_INTERVAL	 10
#define MT76_CONNAC_MAX_TIME_SCHED_SCAN_INTERVAL U16_MAX
#define MT76_CONNAC_MAX_SCHED_SCAN_SSID		10
#define MT76_CONNAC_MAX_SCAN_MATCH		16

#define MT76_CONNAC_COREDUMP_TIMEOUT		(HZ / 20)
#define MT76_CONNAC_COREDUMP_SZ			(1300 * 1024)

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
	bool ds_enable;
	bool suspended;

	spinlock_t txq_lock;
	struct {
		struct mt76_wcid *wcid;
		struct sk_buff *skb;
	} tx_q[IEEE80211_NUM_ACS];

	struct work_struct wake_work;
	wait_queue_head_t wait;

	struct {
		spinlock_t lock;
		u32 count;
	} wake;
	struct mutex mutex;

	struct delayed_work ps_work;
	unsigned long last_activity;
	unsigned long idle_timeout;

	struct {
		unsigned long last_wake_event;
		unsigned long awake_time;
		unsigned long last_doze_event;
		unsigned long doze_time;
		unsigned int lp_wake;
	} stats;
};

struct mt76_connac_coredump {
	struct sk_buff_head msg_list;
	struct delayed_work work;
	unsigned long last_activity;
};

extern const struct wiphy_wowlan_support mt76_connac_wowlan_support;

static inline bool is_mt7922(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7922;
}

static inline bool is_mt7921(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7961 || is_mt7922(dev);
}

static inline bool is_mt7663(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7663;
}

static inline bool is_mt7915(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7915;
}

int mt76_connac_pm_wake(struct mt76_phy *phy, struct mt76_connac_pm *pm);
void mt76_connac_power_save_sched(struct mt76_phy *phy,
				  struct mt76_connac_pm *pm);
void mt76_connac_free_pending_tx_skbs(struct mt76_connac_pm *pm,
				      struct mt76_wcid *wcid);

static inline bool
mt76_connac_pm_ref(struct mt76_phy *phy, struct mt76_connac_pm *pm)
{
	bool ret = false;

	spin_lock_bh(&pm->wake.lock);
	if (test_bit(MT76_STATE_PM, &phy->state))
		goto out;

	pm->wake.count++;
	ret = true;
out:
	spin_unlock_bh(&pm->wake.lock);

	return ret;
}

static inline void
mt76_connac_pm_unref(struct mt76_phy *phy, struct mt76_connac_pm *pm)
{
	spin_lock_bh(&pm->wake.lock);

	pm->last_activity = jiffies;
	if (--pm->wake.count == 0 &&
	    test_bit(MT76_STATE_MCU_RUNNING, &phy->state))
		mt76_connac_power_save_sched(phy, pm);

	spin_unlock_bh(&pm->wake.lock);
}

static inline bool
mt76_connac_skip_fw_pmctrl(struct mt76_phy *phy, struct mt76_connac_pm *pm)
{
	struct mt76_dev *dev = phy->dev;
	bool ret;

	if (dev->token_count)
		return true;

	spin_lock_bh(&pm->wake.lock);
	ret = pm->wake.count || test_and_set_bit(MT76_STATE_PM, &phy->state);
	spin_unlock_bh(&pm->wake.lock);

	return ret;
}

static inline void
mt76_connac_mutex_acquire(struct mt76_dev *dev, struct mt76_connac_pm *pm)
	__acquires(&dev->mutex)
{
	mutex_lock(&dev->mutex);
	mt76_connac_pm_wake(&dev->phy, pm);
}

static inline void
mt76_connac_mutex_release(struct mt76_dev *dev, struct mt76_connac_pm *pm)
	__releases(&dev->mutex)
{
	mt76_connac_power_save_sched(&dev->phy, pm);
	mutex_unlock(&dev->mutex);
}

void mt76_connac_pm_queue_skb(struct ieee80211_hw *hw,
			      struct mt76_connac_pm *pm,
			      struct mt76_wcid *wcid,
			      struct sk_buff *skb);
void mt76_connac_pm_dequeue_skbs(struct mt76_phy *phy,
				 struct mt76_connac_pm *pm);

#endif /* __MT76_CONNAC_H */
