/*
 * Common private data for ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * Based on the mac80211 Prism54 code, which is
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 *
 * Based on the islsm (softmac prism54) driver, which is:
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CW1200_H
#define CW1200_H

#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <net/mac80211.h>

#include "queue.h"
#include "wsm.h"
#include "scan.h"
#include "txrx.h"
#include "pm.h"

/* Forward declarations */
struct hwbus_ops;
struct task_struct;
struct cw1200_debug_priv;
struct firmware;

#define CW1200_MAX_CTRL_FRAME_LEN	(0x1000)

#define CW1200_MAX_STA_IN_AP_MODE	(5)
#define CW1200_LINK_ID_AFTER_DTIM	(CW1200_MAX_STA_IN_AP_MODE + 1)
#define CW1200_LINK_ID_UAPSD		(CW1200_MAX_STA_IN_AP_MODE + 2)
#define CW1200_LINK_ID_MAX		(CW1200_MAX_STA_IN_AP_MODE + 3)
#define CW1200_MAX_REQUEUE_ATTEMPTS	(5)

#define CW1200_MAX_TID			(8)

#define CW1200_BLOCK_ACK_CNT		(30)
#define CW1200_BLOCK_ACK_THLD		(800)
#define CW1200_BLOCK_ACK_HIST		(3)
#define CW1200_BLOCK_ACK_INTERVAL	(1 * HZ / CW1200_BLOCK_ACK_HIST)

#define CW1200_JOIN_TIMEOUT		(1 * HZ)
#define CW1200_AUTH_TIMEOUT		(5 * HZ)

struct cw1200_ht_info {
	struct ieee80211_sta_ht_cap     ht_cap;
	enum nl80211_channel_type       channel_type;
	u16                             operation_mode;
};

/* Please keep order */
enum cw1200_join_status {
	CW1200_JOIN_STATUS_PASSIVE = 0,
	CW1200_JOIN_STATUS_MONITOR,
	CW1200_JOIN_STATUS_JOINING,
	CW1200_JOIN_STATUS_PRE_STA,
	CW1200_JOIN_STATUS_STA,
	CW1200_JOIN_STATUS_IBSS,
	CW1200_JOIN_STATUS_AP,
};

enum cw1200_link_status {
	CW1200_LINK_OFF,
	CW1200_LINK_RESERVE,
	CW1200_LINK_SOFT,
	CW1200_LINK_HARD,
	CW1200_LINK_RESET,
	CW1200_LINK_RESET_REMAP,
};

extern int cw1200_power_mode;
extern const char * const cw1200_fw_types[];

struct cw1200_link_entry {
	unsigned long			timestamp;
	enum cw1200_link_status		status;
	enum cw1200_link_status		prev_status;
	u8				mac[ETH_ALEN];
	u8				buffered[CW1200_MAX_TID];
	struct sk_buff_head		rx_queue;
};

struct cw1200_common {
	/* interfaces to the rest of the stack */
	struct ieee80211_hw		*hw;
	struct ieee80211_vif		*vif;
	struct device			*pdev;

	/* Statistics */
	struct ieee80211_low_level_stats stats;

	/* Our macaddr */
	u8 mac_addr[ETH_ALEN];

	/* Hardware interface */
	const struct hwbus_ops		*hwbus_ops;
	struct hwbus_priv		*hwbus_priv;

	/* Hardware information */
	enum {
		HIF_9000_SILICON_VERSATILE = 0,
		HIF_8601_VERSATILE,
		HIF_8601_SILICON,
	} hw_type;
	enum {
		CW1200_HW_REV_CUT10 = 10,
		CW1200_HW_REV_CUT11 = 11,
		CW1200_HW_REV_CUT20 = 20,
		CW1200_HW_REV_CUT22 = 22,
		CW1X60_HW_REV       = 40,
	} hw_revision;
	int                             hw_refclk;
	bool				hw_have_5ghz;
	const struct firmware		*sdd;
	char                            *sdd_path;

	struct cw1200_debug_priv	*debug;

	struct workqueue_struct		*workqueue;
	struct mutex			conf_mutex;

	struct cw1200_queue		tx_queue[4];
	struct cw1200_queue_stats	tx_queue_stats;
	int				tx_burst_idx;

	/* firmware/hardware info */
	unsigned int tx_hdr_len;

	/* Radio data */
	int output_power;

	/* BBP/MAC state */
	struct ieee80211_rate		*rates;
	struct ieee80211_rate		*mcs_rates;
	struct ieee80211_channel	*channel;
	struct wsm_edca_params		edca;
	struct wsm_tx_queue_params	tx_queue_params;
	struct wsm_mib_association_mode	association_mode;
	struct wsm_set_bss_params	bss_params;
	struct cw1200_ht_info		ht_info;
	struct wsm_set_pm		powersave_mode;
	struct wsm_set_pm		firmware_ps_mode;
	int				cqm_rssi_thold;
	unsigned			cqm_rssi_hyst;
	bool				cqm_use_rssi;
	int				cqm_beacon_loss_count;
	int				channel_switch_in_progress;
	wait_queue_head_t		channel_switch_done;
	u8				long_frame_max_tx_count;
	u8				short_frame_max_tx_count;
	int				mode;
	bool				enable_beacon;
	int				beacon_int;
	bool				listening;
	struct wsm_rx_filter		rx_filter;
	struct wsm_mib_multicast_filter multicast_filter;
	bool				has_multicast_subscription;
	bool				disable_beacon_filter;
	struct work_struct		update_filtering_work;
	struct work_struct		set_beacon_wakeup_period_work;

	u8				ba_rx_tid_mask;
	u8				ba_tx_tid_mask;

	struct cw1200_pm_state		pm_state;

	struct wsm_p2p_ps_modeinfo	p2p_ps_modeinfo;
	struct wsm_uapsd_info		uapsd_info;
	bool				setbssparams_done;
	bool				bt_present;
	u8				conf_listen_interval;
	u32				listen_interval;
	u32				erp_info;
	u32				rts_threshold;

	/* BH */
	atomic_t			bh_rx;
	atomic_t			bh_tx;
	atomic_t			bh_term;
	atomic_t			bh_suspend;

	struct workqueue_struct         *bh_workqueue;
	struct work_struct              bh_work;

	int				bh_error;
	wait_queue_head_t		bh_wq;
	wait_queue_head_t		bh_evt_wq;
	u8				buf_id_tx;
	u8				buf_id_rx;
	u8				wsm_rx_seq;
	u8				wsm_tx_seq;
	int				hw_bufs_used;
	bool				powersave_enabled;
	bool				device_can_sleep;

	/* Scan status */
	struct cw1200_scan scan;
	/* Keep cw1200 awake (WUP = 1) 1 second after each scan to avoid
	 * FW issue with sleeping/waking up.
	 */
	atomic_t			recent_scan;
	struct delayed_work		clear_recent_scan_work;

	/* WSM */
	struct wsm_startup_ind		wsm_caps;
	struct mutex			wsm_cmd_mux;
	struct wsm_buf			wsm_cmd_buf;
	struct wsm_cmd			wsm_cmd;
	wait_queue_head_t		wsm_cmd_wq;
	wait_queue_head_t		wsm_startup_done;
	int                             firmware_ready;
	atomic_t			tx_lock;

	/* WSM debug */
	int				wsm_enable_wsm_dumps;

	/* WSM Join */
	enum cw1200_join_status	join_status;
	u32			pending_frame_id;
	bool			join_pending;
	struct delayed_work	join_timeout;
	struct work_struct	unjoin_work;
	struct work_struct	join_complete_work;
	int			join_complete_status;
	int			join_dtim_period;
	bool			delayed_unjoin;

	/* TX/RX and security */
	s8			wep_default_key_id;
	struct work_struct	wep_key_work;
	u32			key_map;
	struct wsm_add_key	keys[WSM_KEY_MAX_INDEX + 1];

	/* AP powersave */
	u32			link_id_map;
	struct cw1200_link_entry link_id_db[CW1200_MAX_STA_IN_AP_MODE];
	struct work_struct	link_id_work;
	struct delayed_work	link_id_gc_work;
	u32			sta_asleep_mask;
	u32			pspoll_mask;
	bool			aid0_bit_set;
	spinlock_t		ps_state_lock; /* Protect power save state */
	bool			buffered_multicasts;
	bool			tx_multicast;
	struct work_struct	set_tim_work;
	struct work_struct	set_cts_work;
	struct work_struct	multicast_start_work;
	struct work_struct	multicast_stop_work;
	struct timer_list	mcast_timeout;

	/* WSM events and CQM implementation */
	spinlock_t		event_queue_lock; /* Protect event queue */
	struct list_head	event_queue;
	struct work_struct	event_handler;

	struct delayed_work	bss_loss_work;
	spinlock_t		bss_loss_lock; /* Protect BSS loss state */
	int                     bss_loss_state;
	int                     bss_loss_confirm_id;
	int			delayed_link_loss;
	struct work_struct	bss_params_work;

	/* TX rate policy cache */
	struct tx_policy_cache tx_policy_cache;
	struct work_struct tx_policy_upload_work;

	/* legacy PS mode switch in suspend */
	int			ps_mode_switch_in_progress;
	wait_queue_head_t	ps_mode_switch_done;

	/* Workaround for WFD testcase 6.1.10*/
	struct work_struct	linkid_reset_work;
	u8			action_frame_sa[ETH_ALEN];
	u8			action_linkid;
};

struct cw1200_sta_priv {
	int link_id;
};

/* interfaces for the drivers */
int cw1200_core_probe(const struct hwbus_ops *hwbus_ops,
		      struct hwbus_priv *hwbus,
		      struct device *pdev,
		      struct cw1200_common **pself,
		      int ref_clk, const u8 *macaddr,
		      const char *sdd_path, bool have_5ghz);
void cw1200_core_release(struct cw1200_common *self);

#define FWLOAD_BLOCK_SIZE (1024)

static inline int cw1200_is_ht(const struct cw1200_ht_info *ht_info)
{
	return ht_info->channel_type != NL80211_CHAN_NO_HT;
}

static inline int cw1200_ht_greenfield(const struct cw1200_ht_info *ht_info)
{
	return cw1200_is_ht(ht_info) &&
		(ht_info->ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD) &&
		!(ht_info->operation_mode &
		  IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
}

static inline int cw1200_ht_ampdu_density(const struct cw1200_ht_info *ht_info)
{
	if (!cw1200_is_ht(ht_info))
		return 0;
	return ht_info->ht_cap.ampdu_density;
}

#endif /* CW1200_H */
