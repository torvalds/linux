/*
 * Copyright (c) 2010 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef HTC_H
#define HTC_H

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <net/mac80211.h>

#include "common.h"
#include "htc_hst.h"
#include "hif_usb.h"
#include "wmi.h"

#define ATH_STA_SHORT_CALINTERVAL 1000    /* 1 second */
#define ATH_ANI_POLLINTERVAL      100     /* 100 ms */
#define ATH_LONG_CALINTERVAL      30000   /* 30 seconds */
#define ATH_RESTART_CALINTERVAL   1200000 /* 20 minutes */

#define ATH_DEFAULT_BMISS_LIMIT 10
#define IEEE80211_MS_TO_TU(x)   (((x) * 1000) / 1024)
#define TSF_TO_TU(_h, _l) \
	((((u32)(_h)) << 22) | (((u32)(_l)) >> 10))

extern struct ieee80211_ops ath9k_htc_ops;
extern int htc_modparam_nohwcrypt;

enum htc_phymode {
	HTC_MODE_AUTO		= 0,
	HTC_MODE_11A		= 1,
	HTC_MODE_11B		= 2,
	HTC_MODE_11G		= 3,
	HTC_MODE_FH		= 4,
	HTC_MODE_TURBO_A	= 5,
	HTC_MODE_TURBO_G	= 6,
	HTC_MODE_11NA		= 7,
	HTC_MODE_11NG		= 8
};

enum htc_opmode {
	HTC_M_STA	= 1,
	HTC_M_IBSS	= 0,
	HTC_M_AHDEMO	= 3,
	HTC_M_HOSTAP	= 6,
	HTC_M_MONITOR	= 8,
	HTC_M_WDS	= 2
};

#define ATH9K_HTC_HDRSPACE sizeof(struct htc_frame_hdr)
#define ATH9K_HTC_AMPDU	1
#define ATH9K_HTC_NORMAL 2

#define ATH9K_HTC_TX_CTSONLY      0x1
#define ATH9K_HTC_TX_RTSCTS       0x2
#define ATH9K_HTC_TX_USE_MIN_RATE 0x100

struct tx_frame_hdr {
	u8 data_type;
	u8 node_idx;
	u8 vif_idx;
	u8 tidno;
	u32 flags; /* ATH9K_HTC_TX_* */
	u8 key_type;
	u8 keyix;
	u8 reserved[26];
} __packed;

struct tx_mgmt_hdr {
	u8 node_idx;
	u8 vif_idx;
	u8 tidno;
	u8 flags;
	u8 key_type;
	u8 keyix;
	u16 reserved;
} __packed;

struct tx_beacon_header {
	u8 len_changed;
	u8 vif_index;
	u16 rev;
} __packed;

struct ath9k_htc_target_hw {
	u32 flags;
	u32 flags_ext;
	u32 ampdu_limit;
	u8 ampdu_subframes;
	u8 tx_chainmask;
	u8 tx_chainmask_legacy;
	u8 rtscts_ratecode;
	u8 protmode;
} __packed;

struct ath9k_htc_cap_target {
	u32 flags;
	u32 flags_ext;
	u32 ampdu_limit;
	u8 ampdu_subframes;
	u8 tx_chainmask;
	u8 tx_chainmask_legacy;
	u8 rtscts_ratecode;
	u8 protmode;
} __packed;

struct ath9k_htc_target_vif {
	u8 index;
	u8 des_bssid[ETH_ALEN];
	__be32 opmode;
	u8 myaddr[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	u32 flags;
	u32 flags_ext;
	u16 ps_sta;
	__be16 rtsthreshold;
	u8 ath_cap;
	u8 node;
	s8 mcast_rate;
} __packed;

#define ATH_HTC_STA_AUTH  0x0001
#define ATH_HTC_STA_QOS   0x0002
#define ATH_HTC_STA_ERP   0x0004
#define ATH_HTC_STA_HT    0x0008

/* FIXME: UAPSD variables */
struct ath9k_htc_target_sta {
	u16 associd;
	u16 txpower;
	u32 ucastkey;
	u8 macaddr[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	u8 sta_index;
	u8 vif_index;
	u8 vif_sta;
	__be16 flags; /* ATH_HTC_STA_* */
	u16 htcap;
	u8 valid;
	u16 capinfo;
	struct ath9k_htc_target_hw *hw;
	struct ath9k_htc_target_vif *vif;
	u16 txseqmgmt;
	u8 is_vif_sta;
	u16 maxampdu;
	u16 iv16;
	u32 iv32;
} __packed;

struct ath9k_htc_target_aggr {
	u8 sta_index;
	u8 tidno;
	u8 aggr_enable;
	u8 padding;
} __packed;

#define ATH_HTC_RATE_MAX 30

#define WLAN_RC_DS_FLAG  0x01
#define WLAN_RC_40_FLAG  0x02
#define WLAN_RC_SGI_FLAG 0x04
#define WLAN_RC_HT_FLAG  0x08

struct ath9k_htc_rateset {
	u8 rs_nrates;
	u8 rs_rates[ATH_HTC_RATE_MAX];
};

struct ath9k_htc_rate {
	struct ath9k_htc_rateset legacy_rates;
	struct ath9k_htc_rateset ht_rates;
} __packed;

struct ath9k_htc_target_rate {
	u8 sta_index;
	u8 isnew;
	__be32 capflags;
	struct ath9k_htc_rate rates;
};

struct ath9k_htc_target_stats {
	__be32 tx_shortretry;
	__be32 tx_longretry;
	__be32 tx_xretries;
	__be32 ht_txunaggr_xretry;
	__be32 ht_tx_xretries;
} __packed;

struct ath9k_htc_vif {
	u8 index;
};

#define ATH9K_HTC_MAX_STA 8
#define ATH9K_HTC_MAX_TID 8

enum tid_aggr_state {
	AGGR_STOP = 0,
	AGGR_PROGRESS,
	AGGR_START,
	AGGR_OPERATIONAL
};

struct ath9k_htc_sta {
	u8 index;
	enum tid_aggr_state tid_state[ATH9K_HTC_MAX_TID];
};

#define ATH9K_HTC_RXBUF 256
#define HTC_RX_FRAME_HEADER_SIZE 40

struct ath9k_htc_rxbuf {
	bool in_process;
	struct sk_buff *skb;
	struct ath_htc_rx_status rxstatus;
	struct list_head list;
};

struct ath9k_htc_rx {
	int last_rssi; /* FIXME: per-STA */
	struct list_head rxbuf;
	spinlock_t rxbuflock;
};

struct ath9k_htc_tx_ctl {
	u8 type; /* ATH9K_HTC_* */
};

#ifdef CONFIG_ATH9K_HTC_DEBUGFS

#define TX_STAT_INC(c) (hif_dev->htc_handle->drv_priv->debug.tx_stats.c++)
#define RX_STAT_INC(c) (hif_dev->htc_handle->drv_priv->debug.rx_stats.c++)

#define TX_QSTAT_INC(q) (priv->debug.tx_stats.queue_stats[q]++)

struct ath_tx_stats {
	u32 buf_queued;
	u32 buf_completed;
	u32 skb_queued;
	u32 skb_completed;
	u32 skb_dropped;
	u32 queue_stats[WME_NUM_AC];
};

struct ath_rx_stats {
	u32 skb_allocated;
	u32 skb_completed;
	u32 skb_dropped;
};

struct ath9k_debug {
	struct dentry *debugfs_phy;
	struct dentry *debugfs_tgt_stats;
	struct dentry *debugfs_xmit;
	struct dentry *debugfs_recv;
	struct ath_tx_stats tx_stats;
	struct ath_rx_stats rx_stats;
	u32 txrate;
};

#else

#define TX_STAT_INC(c) do { } while (0)
#define RX_STAT_INC(c) do { } while (0)

#define TX_QSTAT_INC(c) do { } while (0)

#endif /* CONFIG_ATH9K_HTC_DEBUGFS */

#define ATH_LED_PIN_DEF             1
#define ATH_LED_PIN_9287            8
#define ATH_LED_PIN_9271            15
#define ATH_LED_PIN_7010            12
#define ATH_LED_ON_DURATION_IDLE    350	/* in msecs */
#define ATH_LED_OFF_DURATION_IDLE   250	/* in msecs */

enum ath_led_type {
	ATH_LED_RADIO,
	ATH_LED_ASSOC,
	ATH_LED_TX,
	ATH_LED_RX
};

struct ath_led {
	struct ath9k_htc_priv *priv;
	struct led_classdev led_cdev;
	enum ath_led_type led_type;
	struct delayed_work brightness_work;
	char name[32];
	bool registered;
	int brightness;
};

struct htc_beacon_config {
	u16 beacon_interval;
	u16 listen_interval;
	u16 dtim_period;
	u16 bmiss_timeout;
	u8 dtim_count;
};

struct ath_btcoex {
	u32 bt_priority_cnt;
	unsigned long bt_priority_time;
	int bt_stomp_type; /* Types of BT stomping */
	u32 btcoex_no_stomp;
	u32 btcoex_period;
	u32 btscan_no_stomp;
};

void ath_htc_init_btcoex_work(struct ath9k_htc_priv *priv);
void ath_htc_resume_btcoex_work(struct ath9k_htc_priv *priv);
void ath_htc_cancel_btcoex_work(struct ath9k_htc_priv *priv);

#define OP_INVALID		   BIT(0)
#define OP_SCANNING		   BIT(1)
#define OP_FULL_RESET		   BIT(2)
#define OP_LED_ASSOCIATED	   BIT(3)
#define OP_LED_ON		   BIT(4)
#define OP_PREAMBLE_SHORT	   BIT(5)
#define OP_PROTECT_ENABLE	   BIT(6)
#define OP_ASSOCIATED		   BIT(7)
#define OP_ENABLE_BEACON	   BIT(8)
#define OP_LED_DEINIT		   BIT(9)
#define OP_UNPLUGGED		   BIT(10)
#define OP_BT_PRIORITY_DETECTED	   BIT(11)
#define OP_BT_SCAN		   BIT(12)

struct ath9k_htc_priv {
	struct device *dev;
	struct ieee80211_hw *hw;
	struct ath_hw *ah;
	struct htc_target *htc;
	struct wmi *wmi;

	enum htc_endpoint_id wmi_cmd_ep;
	enum htc_endpoint_id beacon_ep;
	enum htc_endpoint_id cab_ep;
	enum htc_endpoint_id uapsd_ep;
	enum htc_endpoint_id mgmt_ep;
	enum htc_endpoint_id data_be_ep;
	enum htc_endpoint_id data_bk_ep;
	enum htc_endpoint_id data_vi_ep;
	enum htc_endpoint_id data_vo_ep;

	u16 op_flags;
	u16 curtxpow;
	u16 txpowlimit;
	u16 nvifs;
	u16 nstations;
	u16 seq_no;
	u32 bmiss_cnt;

	struct ath9k_hw_cal_data caldata[38];

	spinlock_t beacon_lock;

	bool tx_queues_stop;
	spinlock_t tx_lock;

	struct ieee80211_vif *vif;
	struct htc_beacon_config cur_beacon_conf;
	unsigned int rxfilter;
	struct tasklet_struct wmi_tasklet;
	struct tasklet_struct rx_tasklet;
	struct ieee80211_supported_band sbands[IEEE80211_NUM_BANDS];
	struct ath9k_htc_rx rx;
	struct tasklet_struct tx_tasklet;
	struct sk_buff_head tx_queue;
	struct delayed_work ath9k_ani_work;
	struct work_struct ps_work;

	struct mutex htc_pm_lock;
	unsigned long ps_usecount;
	bool ps_enabled;
	bool ps_idle;

	struct ath_led radio_led;
	struct ath_led assoc_led;
	struct ath_led tx_led;
	struct ath_led rx_led;
	struct delayed_work ath9k_led_blink_work;
	int led_on_duration;
	int led_off_duration;
	int led_on_cnt;
	int led_off_cnt;

	int beaconq;
	int cabq;
	int hwq_map[WME_NUM_AC];

	struct ath_btcoex btcoex;
	struct delayed_work coex_period_work;
	struct delayed_work duty_cycle_work;
#ifdef CONFIG_ATH9K_HTC_DEBUGFS
	struct ath9k_debug debug;
#endif
	struct mutex mutex;
};

static inline void ath_read_cachesize(struct ath_common *common, int *csz)
{
	common->bus_ops->read_cachesize(common, csz);
}

void ath9k_htc_beaconq_config(struct ath9k_htc_priv *priv);
void ath9k_htc_beacon_config(struct ath9k_htc_priv *priv,
			     struct ieee80211_vif *vif);
void ath9k_htc_swba(struct ath9k_htc_priv *priv, u8 beacon_pending);

void ath9k_htc_rxep(void *priv, struct sk_buff *skb,
		    enum htc_endpoint_id ep_id);
void ath9k_htc_txep(void *priv, struct sk_buff *skb, enum htc_endpoint_id ep_id,
		    bool txok);
void ath9k_htc_beaconep(void *drv_priv, struct sk_buff *skb,
			enum htc_endpoint_id ep_id, bool txok);

void ath9k_htc_station_work(struct work_struct *work);
void ath9k_htc_aggr_work(struct work_struct *work);
void ath9k_ani_work(struct work_struct *work);;

int ath9k_tx_init(struct ath9k_htc_priv *priv);
void ath9k_tx_tasklet(unsigned long data);
int ath9k_htc_tx_start(struct ath9k_htc_priv *priv, struct sk_buff *skb);
void ath9k_tx_cleanup(struct ath9k_htc_priv *priv);
bool ath9k_htc_txq_setup(struct ath9k_htc_priv *priv, int subtype);
int ath9k_htc_cabq_setup(struct ath9k_htc_priv *priv);
int get_hw_qnum(u16 queue, int *hwq_map);
int ath_htc_txq_update(struct ath9k_htc_priv *priv, int qnum,
		       struct ath9k_tx_queue_info *qinfo);

int ath9k_rx_init(struct ath9k_htc_priv *priv);
void ath9k_rx_cleanup(struct ath9k_htc_priv *priv);
void ath9k_host_rx_init(struct ath9k_htc_priv *priv);
void ath9k_rx_tasklet(unsigned long data);
u32 ath9k_htc_calcrxfilter(struct ath9k_htc_priv *priv);

void ath9k_htc_ps_wakeup(struct ath9k_htc_priv *priv);
void ath9k_htc_ps_restore(struct ath9k_htc_priv *priv);
void ath9k_ps_work(struct work_struct *work);
bool ath9k_htc_setpower(struct ath9k_htc_priv *priv,
			enum ath9k_power_mode mode);

void ath9k_start_rfkill_poll(struct ath9k_htc_priv *priv);
void ath9k_init_leds(struct ath9k_htc_priv *priv);
void ath9k_deinit_leds(struct ath9k_htc_priv *priv);

int ath9k_htc_probe_device(struct htc_target *htc_handle, struct device *dev,
			   u16 devid, char *product);
void ath9k_htc_disconnect_device(struct htc_target *htc_handle, bool hotunplug);
#ifdef CONFIG_PM
void ath9k_htc_suspend(struct htc_target *htc_handle);
int ath9k_htc_resume(struct htc_target *htc_handle);
#endif
#ifdef CONFIG_ATH9K_HTC_DEBUGFS
int ath9k_htc_debug_create_root(void);
void ath9k_htc_debug_remove_root(void);
int ath9k_htc_init_debug(struct ath_hw *ah);
void ath9k_htc_exit_debug(struct ath_hw *ah);
#else
static inline int ath9k_htc_debug_create_root(void) { return 0; };
static inline void ath9k_htc_debug_remove_root(void) {};
static inline int ath9k_htc_init_debug(struct ath_hw *ah) { return 0; };
static inline void ath9k_htc_exit_debug(struct ath_hw *ah) {};
#endif /* CONFIG_ATH9K_HTC_DEBUGFS */

#endif /* HTC_H */
