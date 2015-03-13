/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 *   wlan device header file
 */

#ifndef _ESP_PUB_H_
#define _ESP_PUB_H_

#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/firmware.h>
#include <linux/sched.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <linux/version.h>
#include "sip2_common.h"

// to support kernel < 2.6.28 there's no ieee80211_sta
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
#include <net/wireless.h>
#endif

enum esp_sdio_state{
	ESP_SDIO_STATE_FIRST_INIT,
	ESP_SDIO_STATE_FIRST_NORMAL_EXIT,
	ESP_SDIO_STATE_FIRST_ERROR_EXIT,
	ESP_SDIO_STATE_SECOND_INIT,
	ESP_SDIO_STATE_SECOND_ERROR_EXIT,
};

enum esp_tid_state {
	ESP_TID_STATE_INIT,
	ESP_TID_STATE_TRIGGER,
	ESP_TID_STATE_PROGRESS,
	ESP_TID_STATE_OPERATIONAL,
	ESP_TID_STATE_WAIT_STOP,
	ESP_TID_STATE_STOP,
};

struct esp_tx_tid {
	u8 state;
	u8 cnt;
	u16 ssn;
};

enum sta_state {
	ESP_STA_STATE_NORM,
	ESP_STA_STATE_WAIT,
	ESP_STA_STATE_LOST,
};
	

#define ESP_LOSS_COUNT_MAX	5
#define ESP_ND_TIME_REMAIN_MAX	11   /* 12*500ms 6000ms (12-1)*/
#define ESP_ND_TIME_REMAIN_MIN	0    /* immediate */
#define ESP_ND_TIMER_INTERVAL	500  /* 500ms */
#define WME_NUM_TID 16
struct esp_node {
        struct esp_tx_tid tid[WME_NUM_TID];
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
        struct ieee80211_sta *sta;
#else
	u8 addr[ETH_ALEN];
	u16 aid;
	u64 supp_rates[IEEE80211_NUM_BANDS];
	struct ieee80211_ht_info ht_info;
#endif
	u8 ifidx;
	u8 index;
	atomic_t loss_count;
	atomic_t time_remain;
	atomic_t sta_state;
};

#define WME_AC_BE 2
#define WME_AC_BK 3
#define WME_AC_VI 1
#define WME_AC_VO 0

struct llc_snap_hdr {
        u8 dsap;
        u8 ssap;
        u8 cntl;
        u8 org_code[3];
        __be16 eth_type;
} __packed;

struct esp_vif {
	struct esp_pub *epub;
	u8 index;
	u32 beacon_interval;
	bool ap_up;
	struct timer_list beacon_timer;
	struct timer_list nulldata_timer; /* gc use this, too */
};

/* WLAN related, mostly... */
/*struct hw_scan_timeout {
        struct delayed_work w;
        struct ieee80211_hw *hw;
};*/

typedef struct esp_wl {
        u8 bssid[ETH_ALEN];
        u8 req_bssid[ETH_ALEN];

        //struct hw_scan_timeout *hsd;
        struct cfg80211_scan_request *scan_req;
	atomic_t ptk_cnt;
	atomic_t gtk_cnt;
	atomic_t tkip_key_set;

        /* so far only 2G band */
        struct ieee80211_supported_band sbands[IEEE80211_NUM_BANDS];

        unsigned long flags;
        atomic_t off;
} esp_wl_t;

typedef struct esp_hw_idx_map {
	u8 mac[ETH_ALEN];
	u8 flag;
} esp_hw_idx_map_t;

#define ESP_WL_FLAG_RFKILL                	BIT(0)
#define ESP_WL_FLAG_HW_REGISTERED   		BIT(1)
#define ESP_WL_FLAG_CONNECT              		BIT(2)
#define ESP_WL_FLAG_STOP_TXQ          		BIT(3)

#define ESP_PUB_MAX_VIF		2
#define ESP_PUB_MAX_STA		4 //for one interface
#define ESP_PUB_MAX_RXAMPDU	8 //for all interfaces

enum {
        ESP_PM_OFF = 0,
        ESP_PM_TURNING_ON,
        ESP_PM_ON,
        ESP_PM_TURNING_OFF,  /* Do NOT change the order */
};

struct esp_ps {
	u32 dtim_period;
	u32 max_sleep_period;
	unsigned long last_config_time;
        atomic_t state;
        bool nulldata_pm_on;
};

struct esp_mac_prefix {  
	u8 mac_index;
	u8 mac_addr_prefix[3];
};

struct esp_pub {
        struct device *dev;
#ifdef ESP_NO_MAC80211
        struct net_device *net_dev;
        struct wireless_dev *wdev;
        struct net_device_stats *net_stats;
#else
        struct ieee80211_hw *hw;
        struct ieee80211_vif *vif;
        u8 vif_slot;
#endif /* ESP_MAC80211 */

        void *sif; /* serial interface control block, e.g. sdio */
        enum esp_sdio_state sdio_state;
        struct esp_sip *sip;
        struct esp_wl wl;
        struct esp_hw_idx_map hi_map[19];
        struct esp_hw_idx_map low_map[ESP_PUB_MAX_VIF][2];
        //u32 flags; //flags to represent rfkill switch,start
        u8 roc_flags;   //0: not in remain on channel state, 1: in roc state

        struct work_struct tx_work; /* attach to ieee80211 workqueue */
        /* latest mac80211 has multiple tx queue, but we stick with single queue now */
        spinlock_t rx_lock;
        spinlock_t tx_ampdu_lock;
        spinlock_t rx_ampdu_lock;
	spinlock_t tx_lock;
        struct mutex tx_mtx;
        struct sk_buff_head txq;
        atomic_t txq_stopped;

        struct work_struct sendup_work; /* attach to ieee80211 workqueue */
        struct sk_buff_head txdoneq;
        struct sk_buff_head rxq;

        struct workqueue_struct *esp_wkq;

        //u8 bssid[ETH_ALEN];
        u8 mac_addr[ETH_ALEN];
	u8 master_addr[ETH_ALEN];
	u8 master_ifidx;

        u32 rx_filter;
        unsigned long scan_permit;
        bool scan_permit_valid;
        struct delayed_work scan_timeout_work;
	u32 enodes_map;
	u8 rxampdu_map;
	u32 enodes_maps[ESP_PUB_MAX_VIF];
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
        struct esp_node nodes[ESP_PUB_MAX_STA + 1];
#endif
        struct esp_node * enodes[ESP_PUB_MAX_STA + 1];
	struct esp_node * rxampdu_node[ESP_PUB_MAX_RXAMPDU];
	u8 rxampdu_tid[ESP_PUB_MAX_RXAMPDU];
	struct esp_ps ps;
	int enable_int;
	int wait_reset;
};

typedef struct esp_pub esp_pub_t;

struct esp_pub *esp_pub_alloc_mac80211(struct device *dev);
int esp_pub_dealloc_mac80211(struct esp_pub  *epub);
int esp_register_mac80211(struct esp_pub *epub);

int esp_pub_init_all(struct esp_pub *epub);

char *mod_eagle_path_get(void);

void esp_dsr(struct esp_pub *epub);
void hw_scan_done(struct esp_pub *epub, bool aborted);
void esp_rocdone_process(struct ieee80211_hw *hw, struct sip_evt_roc *report);

void esp_ps_config(struct esp_pub *epub, struct esp_ps *ps, bool on);


void esp_register_early_suspend(void);
void esp_unregister_early_suspend(void);
void esp_wakelock_init(void);
void esp_wakelock_destroy(void);
void esp_wake_lock(void);
void esp_wake_unlock(void);
struct esp_node * esp_get_node_by_addr(struct esp_pub * epub, const u8 *addr);
struct esp_node * esp_get_node_by_index(struct esp_pub * epub, u8 index);
int esp_get_empty_rxampdu(struct esp_pub * epub, const u8 *addr, u8 tid);
int esp_get_exist_rxampdu(struct esp_pub * epub, const u8 *addr, u8 tid);

void esp_sendup_deauth(struct esp_pub *epub, u8 *sta_addr);

#ifdef TEST_MODE
int test_init_netlink(struct esp_sip *sip);
void test_exit_netlink(void);
void esp_test_cmd_event(u32 cmd_type, char *reply_info);
void esp_test_init(struct esp_pub *epub);
#endif
#endif /* _ESP_PUB_H_ */
