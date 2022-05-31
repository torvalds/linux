/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
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
#include <linux/etherdevice.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <net/mac80211.h>

#include "common.h"
#include "htc_hst.h"
#include "hif_usb.h"
#include "wmi.h"

#define ATH_STA_SHORT_CALINTERVAL 1000    /* 1 second */
#define ATH_AP_SHORT_CALINTERVAL  100     /* 100 ms */
#define ATH_ANI_POLLINTERVAL      100     /* 100 ms */
#define ATH_LONG_CALINTERVAL      30000   /* 30 seconds */
#define ATH_RESTART_CALINTERVAL   1200000 /* 20 minutes */

#define ATH_DEFAULT_BMISS_LIMIT 10
#define TSF_TO_TU(_h, _l) \
	((((u32)(_h)) << 22) | (((u32)(_l)) >> 10))

extern struct ieee80211_ops ath9k_htc_ops;
extern int htc_modparam_nohwcrypt;
#ifdef CONFIG_MAC80211_LEDS
extern int ath9k_htc_led_blink;
#endif

enum htc_phymode {
	HTC_MODE_11NA		= 0,
	HTC_MODE_11NG		= 1
};

enum htc_opmode {
	HTC_M_STA	= 1,
	HTC_M_IBSS	= 0,
	HTC_M_AHDEMO	= 3,
	HTC_M_HOSTAP	= 6,
	HTC_M_MONITOR	= 8,
	HTC_M_WDS	= 2
};

#define ATH9K_HTC_AMPDU  1
#define ATH9K_HTC_NORMAL 2
#define ATH9K_HTC_BEACON 3
#define ATH9K_HTC_MGMT   4

#define ATH9K_HTC_TX_CTSONLY      0x1
#define ATH9K_HTC_TX_RTSCTS       0x2

struct tx_frame_hdr {
	u8 data_type;
	u8 node_idx;
	u8 vif_idx;
	u8 tidno;
	__be32 flags; /* ATH9K_HTC_TX_* */
	u8 key_type;
	u8 keyix;
	u8 cookie;
	u8 pad;
} __packed;

struct tx_mgmt_hdr {
	u8 node_idx;
	u8 vif_idx;
	u8 tidno;
	u8 flags;
	u8 key_type;
	u8 keyix;
	u8 cookie;
	u8 pad;
} __packed;

struct tx_beacon_header {
	u8 vif_index;
	u8 len_changed;
	u16 rev;
} __packed;

#define MAX_TX_AMPDU_SUBFRAMES_9271 17
#define MAX_TX_AMPDU_SUBFRAMES_7010 22

struct ath9k_htc_cap_target {
	__be32 ampdu_limit;
	u8 ampdu_subframes;
	u8 enable_coex;
	u8 tx_chainmask;
	u8 pad;
} __packed;

struct ath9k_htc_target_vif {
	u8 index;
	u8 opmode;
	u8 myaddr[ETH_ALEN];
	u8 ath_cap;
	__be16 rtsthreshold;
	u8 pad;
} __packed;

struct ath9k_htc_target_sta {
	u8 macaddr[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	u8 sta_index;
	u8 vif_index;
	u8 is_vif_sta;
	__be16 flags;
	__be16 htcap;
	__be16 maxampdu;
	u8 pad;
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
#define ATH_RC_TX_STBC_FLAG 0x20

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

struct ath9k_htc_target_rate_mask {
	u8 vif_index;
	u8 band;
	__be32 mask;
	u16 pad;
} __packed;

struct ath9k_htc_target_int_stats {
	__be32 rx;
	__be32 rxorn;
	__be32 rxeol;
	__be32 txurn;
	__be32 txto;
	__be32 cst;
} __packed;

struct ath9k_htc_target_tx_stats {
	__be32 xretries;
	__be32 fifoerr;
	__be32 filtered;
	__be32 timer_exp;
	__be32 shortretries;
	__be32 longretries;
	__be32 qnull;
	__be32 encap_fail;
	__be32 nobuf;
} __packed;

struct ath9k_htc_target_rx_stats {
	__be32 nobuf;
	__be32 host_send;
	__be32 host_done;
} __packed;

#define ATH9K_HTC_MAX_VIF 2
#define ATH9K_HTC_MAX_BCN_VIF 2

#define INC_VIF(_priv, _type) do {		\
		switch (_type) {		\
		case NL80211_IFTYPE_STATION:	\
			_priv->num_sta_vif++;	\
			break;			\
		case NL80211_IFTYPE_ADHOC:	\
			_priv->num_ibss_vif++;	\
			break;			\
		case NL80211_IFTYPE_AP:		\
			_priv->num_ap_vif++;	\
			break;			\
		case NL80211_IFTYPE_MESH_POINT:	\
			_priv->num_mbss_vif++;	\
			break;			\
		default:			\
			break;			\
		}				\
	} while (0)

#define DEC_VIF(_priv, _type) do {		\
		switch (_type) {		\
		case NL80211_IFTYPE_STATION:	\
			_priv->num_sta_vif--;	\
			break;			\
		case NL80211_IFTYPE_ADHOC:	\
			_priv->num_ibss_vif--;	\
			break;			\
		case NL80211_IFTYPE_AP:		\
			_priv->num_ap_vif--;	\
			break;			\
		case NL80211_IFTYPE_MESH_POINT:	\
			_priv->num_mbss_vif--;	\
			break;			\
		default:			\
			break;			\
		}				\
	} while (0)

struct ath9k_htc_vif {
	u8 index;
	u16 seq_no;
	bool beacon_configured;
	int bslot;
	__le64 tsfadjust;
};

struct ath9k_vif_iter_data {
	const u8 *hw_macaddr;
	u8 mask[ETH_ALEN];
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
	struct work_struct rc_update_work;
	struct ath9k_htc_priv *htc_priv;
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
	struct list_head rxbuf;
	spinlock_t rxbuflock;
	bool initialized;
};

#define ATH9K_HTC_TX_CLEANUP_INTERVAL 50 /* ms */
#define ATH9K_HTC_TX_TIMEOUT_INTERVAL 3000 /* ms */
#define ATH9K_HTC_TX_RESERVE 10
#define ATH9K_HTC_TX_TIMEOUT_COUNT 40
#define ATH9K_HTC_TX_THRESHOLD (MAX_TX_BUF_NUM - ATH9K_HTC_TX_RESERVE)

#define ATH9K_HTC_OP_TX_QUEUES_STOP BIT(0)
#define ATH9K_HTC_OP_TX_DRAIN       BIT(1)

struct ath9k_htc_tx {
	u8 flags;
	int queued_cnt;
	struct sk_buff_head mgmt_ep_queue;
	struct sk_buff_head cab_ep_queue;
	struct sk_buff_head data_be_queue;
	struct sk_buff_head data_bk_queue;
	struct sk_buff_head data_vi_queue;
	struct sk_buff_head data_vo_queue;
	struct sk_buff_head tx_failed;
	DECLARE_BITMAP(tx_slot, MAX_TX_BUF_NUM);
	struct timer_list cleanup_timer;
	spinlock_t tx_lock;
	bool initialized;
};

struct ath9k_htc_tx_ctl {
	u8 type; /* ATH9K_HTC_* */
	u8 epid;
	u8 txok;
	u8 sta_idx;
	unsigned long timestamp;
};

static inline struct ath9k_htc_tx_ctl *HTC_SKB_CB(struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	BUILD_BUG_ON(sizeof(struct ath9k_htc_tx_ctl) >
		     IEEE80211_TX_INFO_DRIVER_DATA_SIZE);
	return (struct ath9k_htc_tx_ctl *) &tx_info->driver_data;
}

#ifdef CONFIG_ATH9K_HTC_DEBUGFS

#define TX_STAT_INC(c) (hif_dev->htc_handle->drv_priv->debug.tx_stats.c++)
#define TX_STAT_ADD(c, a) (hif_dev->htc_handle->drv_priv->debug.tx_stats.c += a)
#define RX_STAT_INC(c) (hif_dev->htc_handle->drv_priv->debug.skbrx_stats.c++)
#define RX_STAT_ADD(c, a) (hif_dev->htc_handle->drv_priv->debug.skbrx_stats.c += a)
#define CAB_STAT_INC   priv->debug.tx_stats.cab_queued++

#define TX_QSTAT_INC(q) (priv->debug.tx_stats.queue_stats[q]++)

void ath9k_htc_err_stat_rx(struct ath9k_htc_priv *priv,
			   struct ath_rx_status *rs);

struct ath_tx_stats {
	u32 buf_queued;
	u32 buf_completed;
	u32 skb_queued;
	u32 skb_success;
	u32 skb_success_bytes;
	u32 skb_failed;
	u32 cab_queued;
	u32 queue_stats[IEEE80211_NUM_ACS];
};

struct ath_skbrx_stats {
	u32 skb_allocated;
	u32 skb_completed;
	u32 skb_completed_bytes;
	u32 skb_dropped;
};

struct ath9k_debug {
	struct dentry *debugfs_phy;
	struct ath_tx_stats tx_stats;
	struct ath_rx_stats rx_stats;
	struct ath_skbrx_stats skbrx_stats;
};

void ath9k_htc_get_et_strings(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      u32 sset, u8 *data);
int ath9k_htc_get_et_sset_count(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif, int sset);
void ath9k_htc_get_et_stats(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ethtool_stats *stats, u64 *data);
#else

#define TX_STAT_INC(c) do { } while (0)
#define TX_STAT_ADD(c, a) do { } while (0)
#define RX_STAT_INC(c) do { } while (0)
#define RX_STAT_ADD(c, a) do { } while (0)
#define CAB_STAT_INC   do { } while (0)

#define TX_QSTAT_INC(c) do { } while (0)

static inline void ath9k_htc_err_stat_rx(struct ath9k_htc_priv *priv,
					 struct ath_rx_status *rs)
{
}

#endif /* CONFIG_ATH9K_HTC_DEBUGFS */

#define ATH_LED_PIN_DEF             1
#define ATH_LED_PIN_9287            10
#define ATH_LED_PIN_9271            15
#define ATH_LED_PIN_7010            12

#define BSTUCK_THRESHOLD 10

/*
 * Adjust these when the max. no of beaconing interfaces is
 * increased.
 */
#define DEFAULT_SWBA_RESPONSE 40 /* in TUs */
#define MIN_SWBA_RESPONSE     10 /* in TUs */

struct htc_beacon {
	enum {
		OK,		/* no change needed */
		UPDATE,		/* update pending */
		COMMIT		/* beacon sent, commit change */
	} updateslot;		/* slot time update fsm */

	struct ieee80211_vif *bslot[ATH9K_HTC_MAX_BCN_VIF];
	u32 bmisscnt;
	u32 beaconq;
	int slottime;
	int slotupdate;
};

struct ath_btcoex {
	u32 bt_priority_cnt;
	unsigned long bt_priority_time;
	int bt_stomp_type; /* Types of BT stomping */
	u32 btcoex_no_stomp;
	u32 btcoex_period;
	u32 btscan_no_stomp;
};

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT
void ath9k_htc_init_btcoex(struct ath9k_htc_priv *priv, char *product);
void ath9k_htc_start_btcoex(struct ath9k_htc_priv *priv);
void ath9k_htc_stop_btcoex(struct ath9k_htc_priv *priv);
#else
static inline void ath9k_htc_init_btcoex(struct ath9k_htc_priv *priv, char *product)
{
}
static inline void ath9k_htc_start_btcoex(struct ath9k_htc_priv *priv)
{
}
static inline void ath9k_htc_stop_btcoex(struct ath9k_htc_priv *priv)
{
}
#endif /* CONFIG_ATH9K_BTCOEX_SUPPORT */

#define OP_BT_PRIORITY_DETECTED    3
#define OP_BT_SCAN                 4
#define OP_TSF_RESET               6

enum htc_op_flags {
	HTC_FWFLAG_NO_RMW,
};

struct ath9k_htc_priv {
	struct device *dev;
	struct ieee80211_hw *hw;
	struct ath_hw *ah;
	struct htc_target *htc;
	struct wmi *wmi;

	u16 fw_version_major;
	u16 fw_version_minor;

	enum htc_endpoint_id wmi_cmd_ep;
	enum htc_endpoint_id beacon_ep;
	enum htc_endpoint_id cab_ep;
	enum htc_endpoint_id uapsd_ep;
	enum htc_endpoint_id mgmt_ep;
	enum htc_endpoint_id data_be_ep;
	enum htc_endpoint_id data_bk_ep;
	enum htc_endpoint_id data_vi_ep;
	enum htc_endpoint_id data_vo_ep;

	u8 vif_slot;
	u8 mon_vif_idx;
	u8 sta_slot;
	u8 vif_sta_pos[ATH9K_HTC_MAX_VIF];
	u8 num_ibss_vif;
	u8 num_mbss_vif;
	u8 num_sta_vif;
	u8 num_sta_assoc_vif;
	u8 num_ap_vif;

	u16 curtxpow;
	u16 txpowlimit;
	u16 nvifs;
	u16 nstations;
	bool rearm_ani;
	bool reconfig_beacon;
	unsigned int rxfilter;
	unsigned long op_flags;
	unsigned long fw_flags;

	struct ath9k_hw_cal_data caldata;
	struct ath_spec_scan_priv spec_priv;

	spinlock_t beacon_lock;
	struct ath_beacon_config cur_beacon_conf;
	struct htc_beacon beacon;

	struct ath9k_htc_rx rx;
	struct ath9k_htc_tx tx;

	struct tasklet_struct swba_tasklet;
	struct tasklet_struct rx_tasklet;
	struct delayed_work ani_work;
	struct tasklet_struct tx_failed_tasklet;
	struct work_struct ps_work;
	struct work_struct fatal_work;

	struct mutex htc_pm_lock;
	unsigned long ps_usecount;
	bool ps_enabled;
	bool ps_idle;

#ifdef CONFIG_MAC80211_LEDS
	enum led_brightness brightness;
	bool led_registered;
	char led_name[32];
	struct led_classdev led_cdev;
	struct work_struct led_work;
#endif

	int cabq;
	int hwq_map[IEEE80211_NUM_ACS];

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT
	struct ath_btcoex btcoex;
#endif

	struct delayed_work coex_period_work;
	struct delayed_work duty_cycle_work;
#ifdef CONFIG_ATH9K_HTC_DEBUGFS
	struct ath9k_debug debug;
#endif
	struct mutex mutex;
	struct ieee80211_vif *csa_vif;
};

static inline void ath_read_cachesize(struct ath_common *common, int *csz)
{
	common->bus_ops->read_cachesize(common, csz);
}

void ath9k_htc_reset(struct ath9k_htc_priv *priv);

void ath9k_htc_assign_bslot(struct ath9k_htc_priv *priv,
			    struct ieee80211_vif *vif);
void ath9k_htc_remove_bslot(struct ath9k_htc_priv *priv,
			    struct ieee80211_vif *vif);
void ath9k_htc_set_tsfadjust(struct ath9k_htc_priv *priv,
			     struct ieee80211_vif *vif);
void ath9k_htc_beaconq_config(struct ath9k_htc_priv *priv);
void ath9k_htc_beacon_config(struct ath9k_htc_priv *priv,
			     struct ieee80211_vif *vif);
void ath9k_htc_beacon_reconfig(struct ath9k_htc_priv *priv);
void ath9k_htc_swba(struct ath9k_htc_priv *priv,
		    struct wmi_event_swba *swba);

void ath9k_htc_rxep(void *priv, struct sk_buff *skb,
		    enum htc_endpoint_id ep_id);
void ath9k_htc_txep(void *priv, struct sk_buff *skb, enum htc_endpoint_id ep_id,
		    bool txok);
void ath9k_htc_beaconep(void *drv_priv, struct sk_buff *skb,
			enum htc_endpoint_id ep_id, bool txok);

int ath9k_htc_update_cap_target(struct ath9k_htc_priv *priv,
				u8 enable_coex);
void ath9k_htc_ani_work(struct work_struct *work);
void ath9k_htc_start_ani(struct ath9k_htc_priv *priv);
void ath9k_htc_stop_ani(struct ath9k_htc_priv *priv);

int ath9k_tx_init(struct ath9k_htc_priv *priv);
int ath9k_htc_tx_start(struct ath9k_htc_priv *priv,
		       struct ieee80211_sta *sta,
		       struct sk_buff *skb, u8 slot, bool is_cab);
void ath9k_tx_cleanup(struct ath9k_htc_priv *priv);
bool ath9k_htc_txq_setup(struct ath9k_htc_priv *priv, int subtype);
int ath9k_htc_cabq_setup(struct ath9k_htc_priv *priv);
int get_hw_qnum(u16 queue, int *hwq_map);
int ath_htc_txq_update(struct ath9k_htc_priv *priv, int qnum,
		       struct ath9k_tx_queue_info *qinfo);
void ath9k_htc_check_stop_queues(struct ath9k_htc_priv *priv);
void ath9k_htc_check_wake_queues(struct ath9k_htc_priv *priv);
int ath9k_htc_tx_get_slot(struct ath9k_htc_priv *priv);
void ath9k_htc_tx_clear_slot(struct ath9k_htc_priv *priv, int slot);
void ath9k_htc_tx_drain(struct ath9k_htc_priv *priv);
void ath9k_htc_txstatus(struct ath9k_htc_priv *priv, void *wmi_event);
void ath9k_tx_failed_tasklet(struct tasklet_struct *t);
void ath9k_htc_tx_cleanup_timer(struct timer_list *t);
bool ath9k_htc_csa_is_finished(struct ath9k_htc_priv *priv);

int ath9k_rx_init(struct ath9k_htc_priv *priv);
void ath9k_rx_cleanup(struct ath9k_htc_priv *priv);
void ath9k_host_rx_init(struct ath9k_htc_priv *priv);
void ath9k_rx_tasklet(struct tasklet_struct *t);
u32 ath9k_htc_calcrxfilter(struct ath9k_htc_priv *priv);

void ath9k_htc_ps_wakeup(struct ath9k_htc_priv *priv);
void ath9k_htc_ps_restore(struct ath9k_htc_priv *priv);
void ath9k_ps_work(struct work_struct *work);
bool ath9k_htc_setpower(struct ath9k_htc_priv *priv,
			enum ath9k_power_mode mode);

void ath9k_start_rfkill_poll(struct ath9k_htc_priv *priv);
void ath9k_htc_rfkill_poll_state(struct ieee80211_hw *hw);

struct base_eep_header *ath9k_htc_get_eeprom_base(struct ath9k_htc_priv *priv);

#ifdef CONFIG_MAC80211_LEDS
void ath9k_configure_leds(struct ath9k_htc_priv *priv);
void ath9k_init_leds(struct ath9k_htc_priv *priv);
void ath9k_deinit_leds(struct ath9k_htc_priv *priv);
void ath9k_led_work(struct work_struct *work);
#else
static inline void ath9k_configure_leds(struct ath9k_htc_priv *priv)
{
}

static inline void ath9k_init_leds(struct ath9k_htc_priv *priv)
{
}

static inline void ath9k_deinit_leds(struct ath9k_htc_priv *priv)
{
}

static inline void ath9k_led_work(struct work_struct *work)
{
}
#endif

int ath9k_htc_probe_device(struct htc_target *htc_handle, struct device *dev,
			   u16 devid, char *product, u32 drv_info);
void ath9k_htc_disconnect_device(struct htc_target *htc_handle, bool hotunplug);
#ifdef CONFIG_PM
void ath9k_htc_suspend(struct htc_target *htc_handle);
int ath9k_htc_resume(struct htc_target *htc_handle);
#endif
#ifdef CONFIG_ATH9K_HTC_DEBUGFS
int ath9k_htc_init_debug(struct ath_hw *ah);
void ath9k_htc_deinit_debug(struct ath9k_htc_priv *priv);
#else
static inline int ath9k_htc_init_debug(struct ath_hw *ah) { return 0; };
static inline void ath9k_htc_deinit_debug(struct ath9k_htc_priv *priv)
{
}
#endif /* CONFIG_ATH9K_HTC_DEBUGFS */

#endif /* HTC_H */
