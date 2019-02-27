/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#ifndef __IWL_MVM_H__
#define __IWL_MVM_H__

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/leds.h>
#include <linux/in6.h>

#ifdef CONFIG_THERMAL
#include <linux/thermal.h>
#endif

#include "iwl-op-mode.h"
#include "iwl-trans.h"
#include "fw/notif-wait.h"
#include "iwl-eeprom-parse.h"
#include "fw/file.h"
#include "iwl-config.h"
#include "sta.h"
#include "fw-api.h"
#include "constants.h"
#include "fw/runtime.h"
#include "fw/dbg.h"
#include "fw/acpi.h"
#include "iwl-nvm-parse.h"

#include <linux/average.h>

#define IWL_MVM_MAX_ADDRESSES		5
/* RSSI offset for WkP */
#define IWL_RSSI_OFFSET 50
#define IWL_MVM_MISSED_BEACONS_THRESHOLD 8
#define IWL_MVM_MISSED_BEACONS_THRESHOLD_LONG 16

/* A TimeUnit is 1024 microsecond */
#define MSEC_TO_TU(_msec)	(_msec*1000/1024)

/* For GO, this value represents the number of TUs before CSA "beacon
 * 0" TBTT when the CSA time-event needs to be scheduled to start.  It
 * must be big enough to ensure that we switch in time.
 */
#define IWL_MVM_CHANNEL_SWITCH_TIME_GO		40

/* For client, this value represents the number of TUs before CSA
 * "beacon 1" TBTT, instead.  This is because we don't know when the
 * GO/AP will be in the new channel, so we switch early enough.
 */
#define IWL_MVM_CHANNEL_SWITCH_TIME_CLIENT	10

/*
 * This value (in TUs) is used to fine tune the CSA NoA end time which should
 * be just before "beacon 0" TBTT.
 */
#define IWL_MVM_CHANNEL_SWITCH_MARGIN 4

/*
 * Number of beacons to transmit on a new channel until we unblock tx to
 * the stations, even if we didn't identify them on a new channel
 */
#define IWL_MVM_CS_UNBLOCK_TX_TIMEOUT 3

/* offchannel queue towards mac80211 */
#define IWL_MVM_OFFCHANNEL_QUEUE 0

extern const struct ieee80211_ops iwl_mvm_hw_ops;

/**
 * struct iwl_mvm_mod_params - module parameters for iwlmvm
 * @init_dbg: if true, then the NIC won't be stopped if the INIT fw asserted.
 *	We will register to mac80211 to have testmode working. The NIC must not
 *	be up'ed after the INIT fw asserted. This is useful to be able to use
 *	proprietary tools over testmode to debug the INIT fw.
 * @tfd_q_hang_detect: enabled the detection of hung transmit queues
 * @power_scheme: one of enum iwl_power_scheme
 */
struct iwl_mvm_mod_params {
	bool init_dbg;
	bool tfd_q_hang_detect;
	int power_scheme;
};
extern struct iwl_mvm_mod_params iwlmvm_mod_params;

struct iwl_mvm_phy_ctxt {
	u16 id;
	u16 color;
	u32 ref;

	enum nl80211_chan_width width;

	/*
	 * TODO: This should probably be removed. Currently here only for rate
	 * scaling algorithm
	 */
	struct ieee80211_channel *channel;
};

struct iwl_mvm_time_event_data {
	struct ieee80211_vif *vif;
	struct list_head list;
	unsigned long end_jiffies;
	u32 duration;
	bool running;
	u32 uid;

	/*
	 * The access to the 'id' field must be done when the
	 * mvm->time_event_lock is held, as it value is used to indicate
	 * if the te is in the time event list or not (when id == TE_MAX)
	 */
	u32 id;
};

 /* Power management */

/**
 * enum iwl_power_scheme
 * @IWL_POWER_LEVEL_CAM - Continuously Active Mode
 * @IWL_POWER_LEVEL_BPS - Balanced Power Save (default)
 * @IWL_POWER_LEVEL_LP  - Low Power
 */
enum iwl_power_scheme {
	IWL_POWER_SCHEME_CAM = 1,
	IWL_POWER_SCHEME_BPS,
	IWL_POWER_SCHEME_LP
};

#define IWL_CONN_MAX_LISTEN_INTERVAL	10
#define IWL_UAPSD_MAX_SP		IEEE80211_WMM_IE_STA_QOSINFO_SP_ALL

#ifdef CONFIG_IWLWIFI_DEBUGFS
enum iwl_dbgfs_pm_mask {
	MVM_DEBUGFS_PM_KEEP_ALIVE = BIT(0),
	MVM_DEBUGFS_PM_SKIP_OVER_DTIM = BIT(1),
	MVM_DEBUGFS_PM_SKIP_DTIM_PERIODS = BIT(2),
	MVM_DEBUGFS_PM_RX_DATA_TIMEOUT = BIT(3),
	MVM_DEBUGFS_PM_TX_DATA_TIMEOUT = BIT(4),
	MVM_DEBUGFS_PM_LPRX_ENA = BIT(6),
	MVM_DEBUGFS_PM_LPRX_RSSI_THRESHOLD = BIT(7),
	MVM_DEBUGFS_PM_SNOOZE_ENABLE = BIT(8),
	MVM_DEBUGFS_PM_UAPSD_MISBEHAVING = BIT(9),
	MVM_DEBUGFS_PM_USE_PS_POLL = BIT(10),
};

struct iwl_dbgfs_pm {
	u16 keep_alive_seconds;
	u32 rx_data_timeout;
	u32 tx_data_timeout;
	bool skip_over_dtim;
	u8 skip_dtim_periods;
	bool lprx_ena;
	u32 lprx_rssi_threshold;
	bool snooze_ena;
	bool uapsd_misbehaving;
	bool use_ps_poll;
	int mask;
};

/* beacon filtering */

enum iwl_dbgfs_bf_mask {
	MVM_DEBUGFS_BF_ENERGY_DELTA = BIT(0),
	MVM_DEBUGFS_BF_ROAMING_ENERGY_DELTA = BIT(1),
	MVM_DEBUGFS_BF_ROAMING_STATE = BIT(2),
	MVM_DEBUGFS_BF_TEMP_THRESHOLD = BIT(3),
	MVM_DEBUGFS_BF_TEMP_FAST_FILTER = BIT(4),
	MVM_DEBUGFS_BF_TEMP_SLOW_FILTER = BIT(5),
	MVM_DEBUGFS_BF_ENABLE_BEACON_FILTER = BIT(6),
	MVM_DEBUGFS_BF_DEBUG_FLAG = BIT(7),
	MVM_DEBUGFS_BF_ESCAPE_TIMER = BIT(8),
	MVM_DEBUGFS_BA_ESCAPE_TIMER = BIT(9),
	MVM_DEBUGFS_BA_ENABLE_BEACON_ABORT = BIT(10),
};

struct iwl_dbgfs_bf {
	u32 bf_energy_delta;
	u32 bf_roaming_energy_delta;
	u32 bf_roaming_state;
	u32 bf_temp_threshold;
	u32 bf_temp_fast_filter;
	u32 bf_temp_slow_filter;
	u32 bf_enable_beacon_filter;
	u32 bf_debug_flag;
	u32 bf_escape_timer;
	u32 ba_escape_timer;
	u32 ba_enable_beacon_abort;
	int mask;
};
#endif

enum iwl_mvm_smps_type_request {
	IWL_MVM_SMPS_REQ_BT_COEX,
	IWL_MVM_SMPS_REQ_TT,
	IWL_MVM_SMPS_REQ_PROT,
	NUM_IWL_MVM_SMPS_REQ,
};

enum iwl_mvm_ref_type {
	IWL_MVM_REF_UCODE_DOWN,
	IWL_MVM_REF_SCAN,
	IWL_MVM_REF_ROC,
	IWL_MVM_REF_ROC_AUX,
	IWL_MVM_REF_P2P_CLIENT,
	IWL_MVM_REF_AP_IBSS,
	IWL_MVM_REF_USER,
	IWL_MVM_REF_TX,
	IWL_MVM_REF_TX_AGG,
	IWL_MVM_REF_ADD_IF,
	IWL_MVM_REF_START_AP,
	IWL_MVM_REF_BSS_CHANGED,
	IWL_MVM_REF_PREPARE_TX,
	IWL_MVM_REF_PROTECT_TDLS,
	IWL_MVM_REF_CHECK_CTKILL,
	IWL_MVM_REF_PRPH_READ,
	IWL_MVM_REF_PRPH_WRITE,
	IWL_MVM_REF_NMI,
	IWL_MVM_REF_TM_CMD,
	IWL_MVM_REF_EXIT_WORK,
	IWL_MVM_REF_PROTECT_CSA,
	IWL_MVM_REF_FW_DBG_COLLECT,
	IWL_MVM_REF_INIT_UCODE,
	IWL_MVM_REF_SENDING_CMD,
	IWL_MVM_REF_RX,

	/* update debugfs.c when changing this */

	IWL_MVM_REF_COUNT,
};

enum iwl_bt_force_ant_mode {
	BT_FORCE_ANT_DIS = 0,
	BT_FORCE_ANT_AUTO,
	BT_FORCE_ANT_BT,
	BT_FORCE_ANT_WIFI,

	BT_FORCE_ANT_MAX,
};

/**
 * struct iwl_mvm_low_latency_force - low latency force mode set by debugfs
 * @LOW_LATENCY_FORCE_UNSET: unset force mode
 * @LOW_LATENCY_FORCE_ON: for low latency on
 * @LOW_LATENCY_FORCE_OFF: for low latency off
 * @NUM_LOW_LATENCY_FORCE: max num of modes
 */
enum iwl_mvm_low_latency_force {
	LOW_LATENCY_FORCE_UNSET,
	LOW_LATENCY_FORCE_ON,
	LOW_LATENCY_FORCE_OFF,
	NUM_LOW_LATENCY_FORCE
};

/**
* struct iwl_mvm_low_latency_cause - low latency set causes
* @LOW_LATENCY_TRAFFIC: indicates low latency traffic was detected
* @LOW_LATENCY_DEBUGFS: low latency mode set from debugfs
* @LOW_LATENCY_VCMD: low latency mode set from vendor command
* @LOW_LATENCY_VIF_TYPE: low latency mode set because of vif type (ap)
* @LOW_LATENCY_DEBUGFS_FORCE_ENABLE: indicate that force mode is enabled
*	the actual set/unset is done with LOW_LATENCY_DEBUGFS_FORCE
* @LOW_LATENCY_DEBUGFS_FORCE: low latency force mode from debugfs
*	set this with LOW_LATENCY_DEBUGFS_FORCE_ENABLE flag
*	in low_latency.
*/
enum iwl_mvm_low_latency_cause {
	LOW_LATENCY_TRAFFIC = BIT(0),
	LOW_LATENCY_DEBUGFS = BIT(1),
	LOW_LATENCY_VCMD = BIT(2),
	LOW_LATENCY_VIF_TYPE = BIT(3),
	LOW_LATENCY_DEBUGFS_FORCE_ENABLE = BIT(4),
	LOW_LATENCY_DEBUGFS_FORCE = BIT(5),
};

/**
* struct iwl_mvm_vif_bf_data - beacon filtering related data
* @bf_enabled: indicates if beacon filtering is enabled
* @ba_enabled: indicated if beacon abort is enabled
* @ave_beacon_signal: average beacon signal
* @last_cqm_event: rssi of the last cqm event
* @bt_coex_min_thold: minimum threshold for BT coex
* @bt_coex_max_thold: maximum threshold for BT coex
* @last_bt_coex_event: rssi of the last BT coex event
*/
struct iwl_mvm_vif_bf_data {
	bool bf_enabled;
	bool ba_enabled;
	int ave_beacon_signal;
	int last_cqm_event;
	int bt_coex_min_thold;
	int bt_coex_max_thold;
	int last_bt_coex_event;
};

/**
 * struct iwl_probe_resp_data - data for NoA/CSA updates
 * @rcu_head: used for freeing the data on update
 * @notif: notification data
 * @noa_len: length of NoA attribute, calculated from the notification
 */
struct iwl_probe_resp_data {
	struct rcu_head rcu_head;
	struct iwl_probe_resp_data_notif notif;
	int noa_len;
};

/**
 * struct iwl_mvm_vif - data per Virtual Interface, it is a MAC context
 * @id: between 0 and 3
 * @color: to solve races upon MAC addition and removal
 * @ap_sta_id: the sta_id of the AP - valid only if VIF type is STA
 * @bssid: BSSID for this (client) interface
 * @associated: indicates that we're currently associated, used only for
 *	managing the firmware state in iwl_mvm_bss_info_changed_station()
 * @ap_assoc_sta_count: count of stations associated to us - valid only
 *	if VIF type is AP
 * @uploaded: indicates the MAC context has been added to the device
 * @ap_ibss_active: indicates that AP/IBSS is configured and that the interface
 *	should get quota etc.
 * @pm_enabled - Indicate if MAC power management is allowed
 * @monitor_active: indicates that monitor context is configured, and that the
 *	interface should get quota etc.
 * @low_latency: bit flags for low latency
 *	see enum &iwl_mvm_low_latency_cause for causes.
 * @low_latency_actual: boolean, indicates low latency is set,
 *	as a result from low_latency bit flags and takes force into account.
 * @ps_disabled: indicates that this interface requires PS to be disabled
 * @queue_params: QoS params for this MAC
 * @bcast_sta: station used for broadcast packets. Used by the following
 *  vifs: P2P_DEVICE, GO and AP.
 * @beacon_skb: the skb used to hold the AP/GO beacon template
 * @smps_requests: the SMPS requests of different parts of the driver,
 *	combined on update to yield the overall request to mac80211.
 * @beacon_stats: beacon statistics, containing the # of received beacons,
 *	# of received beacons accumulated over FW restart, and the current
 *	average signal of beacons retrieved from the firmware
 * @csa_failed: CSA failed to schedule time event, report an error later
 * @features: hw features active for this vif
 * @probe_resp_data: data from FW notification to store NOA and CSA related
 *	data to be inserted into probe response.
 */
struct iwl_mvm_vif {
	struct iwl_mvm *mvm;
	u16 id;
	u16 color;
	u8 ap_sta_id;

	u8 bssid[ETH_ALEN];
	bool associated;
	u8 ap_assoc_sta_count;

	u16 cab_queue;

	bool uploaded;
	bool ap_ibss_active;
	bool pm_enabled;
	bool monitor_active;
	u8 low_latency: 6;
	u8 low_latency_actual: 1;
	bool ps_disabled;
	struct iwl_mvm_vif_bf_data bf_data;

	struct {
		u32 num_beacons, accu_num_beacons;
		u8 avg_signal;
	} beacon_stats;

	u32 ap_beacon_time;

	enum iwl_tsf_id tsf_id;

	/*
	 * QoS data from mac80211, need to store this here
	 * as mac80211 has a separate callback but we need
	 * to have the data for the MAC context
	 */
	struct ieee80211_tx_queue_params queue_params[IEEE80211_NUM_ACS];
	struct iwl_mvm_time_event_data time_event_data;
	struct iwl_mvm_time_event_data hs_time_event_data;

	struct iwl_mvm_int_sta bcast_sta;
	struct iwl_mvm_int_sta mcast_sta;

	/*
	 * Assigned while mac80211 has the interface in a channel context,
	 * or, for P2P Device, while it exists.
	 */
	struct iwl_mvm_phy_ctxt *phy_ctxt;

#ifdef CONFIG_PM
	/* WoWLAN GTK rekey data */
	struct {
		u8 kck[NL80211_KCK_LEN], kek[NL80211_KEK_LEN];
		__le64 replay_ctr;
		bool valid;
	} rekey_data;

	int tx_key_idx;

	bool seqno_valid;
	u16 seqno;
#endif

#if IS_ENABLED(CONFIG_IPV6)
	/* IPv6 addresses for WoWLAN */
	struct in6_addr target_ipv6_addrs[IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_MAX];
	unsigned long tentative_addrs[BITS_TO_LONGS(IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_MAX)];
	int num_target_ipv6_addrs;
#endif

#ifdef CONFIG_IWLWIFI_DEBUGFS
	struct dentry *dbgfs_dir;
	struct dentry *dbgfs_slink;
	struct iwl_dbgfs_pm dbgfs_pm;
	struct iwl_dbgfs_bf dbgfs_bf;
	struct iwl_mac_power_cmd mac_pwr_cmd;
	int dbgfs_quota_min;
#endif

	enum ieee80211_smps_mode smps_requests[NUM_IWL_MVM_SMPS_REQ];

	/* FW identified misbehaving AP */
	u8 uapsd_misbehaving_bssid[ETH_ALEN];

	struct delayed_work uapsd_nonagg_detected_wk;

	/* Indicates that CSA countdown may be started */
	bool csa_countdown;
	bool csa_failed;
	u16 csa_target_freq;
	u16 csa_count;
	u16 csa_misbehave;
	struct delayed_work csa_work;

	/* Indicates that we are waiting for a beacon on a new channel */
	bool csa_bcn_pending;

	/* TCP Checksum Offload */
	netdev_features_t features;

	struct iwl_probe_resp_data __rcu *probe_resp_data;
	struct ieee80211_key_conf *ap_wep_key;
};

static inline struct iwl_mvm_vif *
iwl_mvm_vif_from_mac80211(struct ieee80211_vif *vif)
{
	if (!vif)
		return NULL;
	return (void *)vif->drv_priv;
}

extern const u8 tid_to_mac80211_ac[];

#define IWL_MVM_SCAN_STOPPING_SHIFT	8

enum iwl_scan_status {
	IWL_MVM_SCAN_REGULAR		= BIT(0),
	IWL_MVM_SCAN_SCHED		= BIT(1),
	IWL_MVM_SCAN_NETDETECT		= BIT(2),

	IWL_MVM_SCAN_STOPPING_REGULAR	= BIT(8),
	IWL_MVM_SCAN_STOPPING_SCHED	= BIT(9),
	IWL_MVM_SCAN_STOPPING_NETDETECT	= BIT(10),

	IWL_MVM_SCAN_REGULAR_MASK	= IWL_MVM_SCAN_REGULAR |
					  IWL_MVM_SCAN_STOPPING_REGULAR,
	IWL_MVM_SCAN_SCHED_MASK		= IWL_MVM_SCAN_SCHED |
					  IWL_MVM_SCAN_STOPPING_SCHED,
	IWL_MVM_SCAN_NETDETECT_MASK	= IWL_MVM_SCAN_NETDETECT |
					  IWL_MVM_SCAN_STOPPING_NETDETECT,

	IWL_MVM_SCAN_STOPPING_MASK	= 0xff << IWL_MVM_SCAN_STOPPING_SHIFT,
	IWL_MVM_SCAN_MASK		= 0xff,
};

enum iwl_mvm_scan_type {
	IWL_SCAN_TYPE_NOT_SET,
	IWL_SCAN_TYPE_UNASSOC,
	IWL_SCAN_TYPE_WILD,
	IWL_SCAN_TYPE_MILD,
	IWL_SCAN_TYPE_FRAGMENTED,
	IWL_SCAN_TYPE_FAST_BALANCE,
};

enum iwl_mvm_sched_scan_pass_all_states {
	SCHED_SCAN_PASS_ALL_DISABLED,
	SCHED_SCAN_PASS_ALL_ENABLED,
	SCHED_SCAN_PASS_ALL_FOUND,
};

/**
 * struct iwl_mvm_tt_mgnt - Thermal Throttling Management structure
 * @ct_kill_exit: worker to exit thermal kill
 * @dynamic_smps: Is thermal throttling enabled dynamic_smps?
 * @tx_backoff: The current thremal throttling tx backoff in uSec.
 * @min_backoff: The minimal tx backoff due to power restrictions
 * @params: Parameters to configure the thermal throttling algorithm.
 * @throttle: Is thermal throttling is active?
 */
struct iwl_mvm_tt_mgmt {
	struct delayed_work ct_kill_exit;
	bool dynamic_smps;
	u32 tx_backoff;
	u32 min_backoff;
	struct iwl_tt_params params;
	bool throttle;
};

#ifdef CONFIG_THERMAL
/**
 *struct iwl_mvm_thermal_device - thermal zone related data
 * @temp_trips: temperature thresholds for report
 * @fw_trips_index: keep indexes to original array - temp_trips
 * @tzone: thermal zone device data
*/
struct iwl_mvm_thermal_device {
	s16 temp_trips[IWL_MAX_DTS_TRIPS];
	u8 fw_trips_index[IWL_MAX_DTS_TRIPS];
	struct thermal_zone_device *tzone;
};

/*
 * struct iwl_mvm_cooling_device
 * @cur_state: current state
 * @cdev: struct thermal cooling device
 */
struct iwl_mvm_cooling_device {
	u32 cur_state;
	struct thermal_cooling_device *cdev;
};
#endif

#define IWL_MVM_NUM_LAST_FRAMES_UCODE_RATES 8

struct iwl_mvm_frame_stats {
	u32 legacy_frames;
	u32 ht_frames;
	u32 vht_frames;
	u32 bw_20_frames;
	u32 bw_40_frames;
	u32 bw_80_frames;
	u32 bw_160_frames;
	u32 sgi_frames;
	u32 ngi_frames;
	u32 siso_frames;
	u32 mimo2_frames;
	u32 agg_frames;
	u32 ampdu_count;
	u32 success_frames;
	u32 fail_frames;
	u32 last_rates[IWL_MVM_NUM_LAST_FRAMES_UCODE_RATES];
	int last_frame_idx;
};

enum {
	D0I3_DEFER_WAKEUP,
	D0I3_PENDING_WAKEUP,
};

#define IWL_MVM_DEBUG_SET_TEMPERATURE_DISABLE 0xff
#define IWL_MVM_DEBUG_SET_TEMPERATURE_MIN -100
#define IWL_MVM_DEBUG_SET_TEMPERATURE_MAX 200

enum iwl_mvm_tdls_cs_state {
	IWL_MVM_TDLS_SW_IDLE = 0,
	IWL_MVM_TDLS_SW_REQ_SENT,
	IWL_MVM_TDLS_SW_RESP_RCVD,
	IWL_MVM_TDLS_SW_REQ_RCVD,
	IWL_MVM_TDLS_SW_ACTIVE,
};

enum iwl_mvm_traffic_load {
	IWL_MVM_TRAFFIC_LOW,
	IWL_MVM_TRAFFIC_MEDIUM,
	IWL_MVM_TRAFFIC_HIGH,
};

DECLARE_EWMA(rate, 16, 16)

struct iwl_mvm_tcm_mac {
	struct {
		u32 pkts[IEEE80211_NUM_ACS];
		u32 airtime;
	} tx;
	struct {
		u32 pkts[IEEE80211_NUM_ACS];
		u32 airtime;
		u32 last_ampdu_ref;
	} rx;
	struct {
		/* track AP's transfer in client mode */
		u64 rx_bytes;
		struct ewma_rate rate;
		bool detected;
	} uapsd_nonagg_detect;
	bool opened_rx_ba_sessions;
};

struct iwl_mvm_tcm {
	struct delayed_work work;
	spinlock_t lock; /* used when time elapsed */
	unsigned long ts; /* timestamp when period ends */
	unsigned long ll_ts;
	unsigned long uapsd_nonagg_ts;
	bool paused;
	struct iwl_mvm_tcm_mac data[NUM_MAC_INDEX_DRIVER];
	struct {
		u32 elapsed; /* milliseconds for this TCM period */
		u32 airtime[NUM_MAC_INDEX_DRIVER];
		enum iwl_mvm_traffic_load load[NUM_MAC_INDEX_DRIVER];
		enum iwl_mvm_traffic_load band_load[NUM_NL80211_BANDS];
		enum iwl_mvm_traffic_load global_load;
		bool low_latency[NUM_MAC_INDEX_DRIVER];
		bool change[NUM_MAC_INDEX_DRIVER];
		bool global_change;
	} result;
};

/**
 * struct iwl_mvm_reorder_buffer - per ra/tid/queue reorder buffer
 * @head_sn: reorder window head sn
 * @num_stored: number of mpdus stored in the buffer
 * @buf_size: the reorder buffer size as set by the last addba request
 * @queue: queue of this reorder buffer
 * @last_amsdu: track last ASMDU SN for duplication detection
 * @last_sub_index: track ASMDU sub frame index for duplication detection
 * @reorder_timer: timer for frames are in the reorder buffer. For AMSDU
 *	it is the time of last received sub-frame
 * @removed: prevent timer re-arming
 * @valid: reordering is valid for this queue
 * @lock: protect reorder buffer internal state
 * @mvm: mvm pointer, needed for frame timer context
 */
struct iwl_mvm_reorder_buffer {
	u16 head_sn;
	u16 num_stored;
	u16 buf_size;
	int queue;
	u16 last_amsdu;
	u8 last_sub_index;
	struct timer_list reorder_timer;
	bool removed;
	bool valid;
	spinlock_t lock;
	struct iwl_mvm *mvm;
} ____cacheline_aligned_in_smp;

/**
 * struct _iwl_mvm_reorder_buf_entry - reorder buffer entry per-queue/per-seqno
 * @frames: list of skbs stored
 * @reorder_time: time the packet was stored in the reorder buffer
 */
struct _iwl_mvm_reorder_buf_entry {
	struct sk_buff_head frames;
	unsigned long reorder_time;
};

/* make this indirection to get the aligned thing */
struct iwl_mvm_reorder_buf_entry {
	struct _iwl_mvm_reorder_buf_entry e;
}
#ifndef __CHECKER__
/* sparse doesn't like this construct: "bad integer constant expression" */
__aligned(roundup_pow_of_two(sizeof(struct _iwl_mvm_reorder_buf_entry)))
#endif
;

/**
 * struct iwl_mvm_baid_data - BA session data
 * @sta_id: station id
 * @tid: tid of the session
 * @baid baid of the session
 * @timeout: the timeout set in the addba request
 * @entries_per_queue: # of buffers per queue, this actually gets
 *	aligned up to avoid cache line sharing between queues
 * @last_rx: last rx jiffies, updated only if timeout passed from last update
 * @session_timer: timer to check if BA session expired, runs at 2 * timeout
 * @mvm: mvm pointer, needed for timer context
 * @reorder_buf: reorder buffer, allocated per queue
 * @reorder_buf_data: data
 */
struct iwl_mvm_baid_data {
	struct rcu_head rcu_head;
	u8 sta_id;
	u8 tid;
	u8 baid;
	u16 timeout;
	u16 entries_per_queue;
	unsigned long last_rx;
	struct timer_list session_timer;
	struct iwl_mvm_baid_data __rcu **rcu_ptr;
	struct iwl_mvm *mvm;
	struct iwl_mvm_reorder_buffer reorder_buf[IWL_MAX_RX_HW_QUEUES];
	struct iwl_mvm_reorder_buf_entry entries[];
};

static inline struct iwl_mvm_baid_data *
iwl_mvm_baid_data_from_reorder_buf(struct iwl_mvm_reorder_buffer *buf)
{
	return (void *)((u8 *)buf -
			offsetof(struct iwl_mvm_baid_data, reorder_buf) -
			sizeof(*buf) * buf->queue);
}

/*
 * enum iwl_mvm_queue_status - queue status
 * @IWL_MVM_QUEUE_FREE: the queue is not allocated nor reserved
 *	Basically, this means that this queue can be used for any purpose
 * @IWL_MVM_QUEUE_RESERVED: queue is reserved but not yet in use
 *	This is the state of a queue that has been dedicated for some RATID
 *	(agg'd or not), but that hasn't yet gone through the actual enablement
 *	of iwl_mvm_enable_txq(), and therefore no traffic can go through it yet.
 *	Note that in this state there is no requirement to already know what TID
 *	should be used with this queue, it is just marked as a queue that will
 *	be used, and shouldn't be allocated to anyone else.
 * @IWL_MVM_QUEUE_READY: queue is ready to be used
 *	This is the state of a queue that has been fully configured (including
 *	SCD pointers, etc), has a specific RA/TID assigned to it, and can be
 *	used to send traffic.
 * @IWL_MVM_QUEUE_SHARED: queue is shared, or in a process of becoming shared
 *	This is a state in which a single queue serves more than one TID, all of
 *	which are not aggregated. Note that the queue is only associated to one
 *	RA.
 */
enum iwl_mvm_queue_status {
	IWL_MVM_QUEUE_FREE,
	IWL_MVM_QUEUE_RESERVED,
	IWL_MVM_QUEUE_READY,
	IWL_MVM_QUEUE_SHARED,
};

#define IWL_MVM_DQA_QUEUE_TIMEOUT	(5 * HZ)
#define IWL_MVM_INVALID_QUEUE		0xFFFF

#define IWL_MVM_NUM_CIPHERS             10

struct iwl_mvm_sar_profile {
	bool enabled;
	u8 table[ACPI_SAR_TABLE_SIZE];
};

struct iwl_mvm_geo_profile {
	u8 values[ACPI_GEO_TABLE_SIZE];
};

struct iwl_mvm_txq {
	struct list_head list;
	u16 txq_id;
	atomic_t tx_request;
	bool stopped;
};

static inline struct iwl_mvm_txq *
iwl_mvm_txq_from_mac80211(struct ieee80211_txq *txq)
{
	return (void *)txq->drv_priv;
}

static inline struct iwl_mvm_txq *
iwl_mvm_txq_from_tid(struct ieee80211_sta *sta, u8 tid)
{
	if (tid == IWL_MAX_TID_COUNT)
		tid = IEEE80211_NUM_TIDS;

	return (void *)sta->txq[tid]->drv_priv;
}

/**
 * struct iwl_mvm_tvqm_txq_info - maps TVQM hw queue to tid
 *
 * @sta_id: sta id
 * @txq_tid: txq tid
 */
struct iwl_mvm_tvqm_txq_info {
	u8 sta_id;
	u8 txq_tid;
};

struct iwl_mvm_dqa_txq_info {
	u8 ra_sta_id; /* The RA this queue is mapped to, if exists */
	bool reserved; /* Is this the TXQ reserved for a STA */
	u8 mac80211_ac; /* The mac80211 AC this queue is mapped to */
	u8 txq_tid; /* The TID "owner" of this queue*/
	u16 tid_bitmap; /* Bitmap of the TIDs mapped to this queue */
	/* Timestamp for inactivation per TID of this queue */
	unsigned long last_frame_time[IWL_MAX_TID_COUNT + 1];
	enum iwl_mvm_queue_status status;
};

struct iwl_mvm {
	/* for logger access */
	struct device *dev;

	struct iwl_trans *trans;
	const struct iwl_fw *fw;
	const struct iwl_cfg *cfg;
	struct iwl_phy_db *phy_db;
	struct ieee80211_hw *hw;

	/* for protecting access to iwl_mvm */
	struct mutex mutex;
	struct list_head async_handlers_list;
	spinlock_t async_handlers_lock;
	struct work_struct async_handlers_wk;

	struct work_struct roc_done_wk;

	unsigned long init_status;

	unsigned long status;

	u32 queue_sync_cookie;
	atomic_t queue_sync_counter;
	/*
	 * for beacon filtering -
	 * currently only one interface can be supported
	 */
	struct iwl_mvm_vif *bf_allowed_vif;

	bool hw_registered;
	bool calibrating;
	bool support_umac_log;

	u32 ampdu_ref;
	bool ampdu_toggle;

	struct iwl_notif_wait_data notif_wait;

	union {
		struct mvm_statistics_rx_v3 rx_stats_v3;
		struct mvm_statistics_rx rx_stats;
	};

	struct {
		u64 rx_time;
		u64 tx_time;
		u64 on_time_rf;
		u64 on_time_scan;
	} radio_stats, accu_radio_stats;

	struct list_head add_stream_txqs;
	union {
		struct iwl_mvm_dqa_txq_info queue_info[IWL_MAX_HW_QUEUES];
		struct iwl_mvm_tvqm_txq_info tvqm_info[IWL_MAX_TVQM_QUEUES];
	};
	struct work_struct add_stream_wk; /* To add streams to queues */

	const char *nvm_file_name;
	struct iwl_nvm_data *nvm_data;
	/* NVM sections */
	struct iwl_nvm_section nvm_sections[NVM_MAX_NUM_SECTIONS];

	struct iwl_fw_runtime fwrt;

	/* EEPROM MAC addresses */
	struct mac_address addresses[IWL_MVM_MAX_ADDRESSES];

	/* data related to data path */
	struct iwl_rx_phy_info last_phy_info;
	struct ieee80211_sta __rcu *fw_id_to_mac_id[IWL_MVM_STATION_COUNT];
	u8 rx_ba_sessions;

	/* configured by mac80211 */
	u32 rts_threshold;

	/* Scan status, cmd (pre-allocated) and auxiliary station */
	unsigned int scan_status;
	void *scan_cmd;
	struct iwl_mcast_filter_cmd *mcast_filter_cmd;
	/* For CDB this is low band scan type, for non-CDB - type. */
	enum iwl_mvm_scan_type scan_type;
	enum iwl_mvm_scan_type hb_scan_type;

	enum iwl_mvm_sched_scan_pass_all_states sched_scan_pass_all;
	struct delayed_work scan_timeout_dwork;

	/* max number of simultaneous scans the FW supports */
	unsigned int max_scans;

	/* UMAC scan tracking */
	u32 scan_uid_status[IWL_MVM_MAX_UMAC_SCANS];

	/* start time of last scan in TSF of the mac that requested the scan */
	u64 scan_start;

	/* the vif that requested the current scan */
	struct iwl_mvm_vif *scan_vif;

	/* rx chain antennas set through debugfs for the scan command */
	u8 scan_rx_ant;

#ifdef CONFIG_IWLWIFI_BCAST_FILTERING
	/* broadcast filters to configure for each associated station */
	const struct iwl_fw_bcast_filter *bcast_filters;
#ifdef CONFIG_IWLWIFI_DEBUGFS
	struct {
		bool override;
		struct iwl_bcast_filter_cmd cmd;
	} dbgfs_bcast_filtering;
#endif
#endif

	/* Internal station */
	struct iwl_mvm_int_sta aux_sta;
	struct iwl_mvm_int_sta snif_sta;

	bool last_ebs_successful;

	u8 scan_last_antenna_idx; /* to toggle TX between antennas */
	u8 mgmt_last_antenna_idx;

	/* last smart fifo state that was successfully sent to firmware */
	enum iwl_sf_state sf_state;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	struct dentry *debugfs_dir;
	u32 dbgfs_sram_offset, dbgfs_sram_len;
	u32 dbgfs_prph_reg_addr;
	bool disable_power_off;
	bool disable_power_off_d3;
	bool beacon_inject_active;

	bool scan_iter_notif_enabled;

	struct debugfs_blob_wrapper nvm_hw_blob;
	struct debugfs_blob_wrapper nvm_sw_blob;
	struct debugfs_blob_wrapper nvm_calib_blob;
	struct debugfs_blob_wrapper nvm_prod_blob;
	struct debugfs_blob_wrapper nvm_phy_sku_blob;
	struct debugfs_blob_wrapper nvm_reg_blob;

	struct iwl_mvm_frame_stats drv_rx_stats;
	spinlock_t drv_stats_lock;
	u16 dbgfs_rx_phyinfo;
#endif

	struct iwl_mvm_phy_ctxt phy_ctxts[NUM_PHY_CTX];

	struct list_head time_event_list;
	spinlock_t time_event_lock;

	/*
	 * A bitmap indicating the index of the key in use. The firmware
	 * can hold 16 keys at most. Reflect this fact.
	 */
	unsigned long fw_key_table[BITS_TO_LONGS(STA_KEY_MAX_NUM)];
	u8 fw_key_deleted[STA_KEY_MAX_NUM];

	/* references taken by the driver and spinlock protecting them */
	spinlock_t refs_lock;
	u8 refs[IWL_MVM_REF_COUNT];

	u8 vif_count;
	struct ieee80211_vif __rcu *vif_id_to_mac[NUM_MAC_INDEX_DRIVER];

	/* -1 for always, 0 for never, >0 for that many times */
	s8 fw_restart;
	u8 *error_recovery_buf;

#ifdef CONFIG_IWLWIFI_LEDS
	struct led_classdev led;
#endif

	struct ieee80211_vif *p2p_device_vif;

#ifdef CONFIG_PM
	struct wiphy_wowlan_support wowlan;
	int gtk_ivlen, gtk_icvlen, ptk_ivlen, ptk_icvlen;

	/* sched scan settings for net detect */
	struct ieee80211_scan_ies nd_ies;
	struct cfg80211_match_set *nd_match_sets;
	int n_nd_match_sets;
	struct ieee80211_channel **nd_channels;
	int n_nd_channels;
	bool net_detect;
#ifdef CONFIG_IWLWIFI_DEBUGFS
	bool d3_wake_sysassert;
	bool d3_test_active;
	bool store_d3_resume_sram;
	void *d3_resume_sram;
	u32 d3_test_pme_ptr;
	struct ieee80211_vif *keep_vif;
	u32 last_netdetect_scans; /* no. of scans in the last net-detect wake */
#endif
#endif

	/* d0i3 */
	u8 d0i3_ap_sta_id;
	bool d0i3_offloading;
	struct work_struct d0i3_exit_work;
	struct sk_buff_head d0i3_tx;
	/* protect d0i3_suspend_flags */
	struct mutex d0i3_suspend_mutex;
	unsigned long d0i3_suspend_flags;
	/* sync d0i3_tx queue and IWL_MVM_STATUS_IN_D0I3 status flag */
	spinlock_t d0i3_tx_lock;
	wait_queue_head_t d0i3_exit_waitq;
	wait_queue_head_t rx_sync_waitq;

	/* BT-Coex */
	struct iwl_bt_coex_profile_notif last_bt_notif;
	struct iwl_bt_coex_ci_cmd last_bt_ci_cmd;

	u8 bt_tx_prio;
	enum iwl_bt_force_ant_mode bt_force_ant_mode;

	/* Aux ROC */
	struct list_head aux_roc_te_list;

	/* Thermal Throttling and CTkill */
	struct iwl_mvm_tt_mgmt thermal_throttle;
#ifdef CONFIG_THERMAL
	struct iwl_mvm_thermal_device tz_device;
	struct iwl_mvm_cooling_device cooling_dev;
#endif

	s32 temperature;	/* Celsius */
	/*
	 * Debug option to set the NIC temperature. This option makes the
	 * driver think this is the actual NIC temperature, and ignore the
	 * real temperature that is received from the fw
	 */
	bool temperature_test;  /* Debug test temperature is enabled */

	unsigned long bt_coex_last_tcm_ts;
	struct iwl_mvm_tcm tcm;

	u8 uapsd_noagg_bssid_write_idx;
	struct mac_address uapsd_noagg_bssids[IWL_MVM_UAPSD_NOAGG_BSSIDS_NUM]
		__aligned(2);

	struct iwl_time_quota_cmd last_quota_cmd;

#ifdef CONFIG_NL80211_TESTMODE
	u32 noa_duration;
	struct ieee80211_vif *noa_vif;
#endif

	/* Tx queues */
	u16 aux_queue;
	u16 snif_queue;
	u16 probe_queue;
	u16 p2p_dev_queue;

	/* Indicate if device power save is allowed */
	u8 ps_disabled; /* u8 instead of bool to ease debugfs_create_* usage */
	/* Indicate if 32Khz external clock is valid */
	u32 ext_clock_valid;
	unsigned int max_amsdu_len; /* used for debugfs only */

	struct ieee80211_vif __rcu *csa_vif;
	struct ieee80211_vif __rcu *csa_tx_blocked_vif;
	u8 csa_tx_block_bcn_timeout;

	/* system time of last beacon (for AP/GO interface) */
	u32 ap_last_beacon_gp2;

	/* indicates that we transmitted the last beacon */
	bool ibss_manager;

	bool lar_regdom_set;
	enum iwl_mcc_source mcc_src;

	/* TDLS channel switch data */
	struct {
		struct delayed_work dwork;
		enum iwl_mvm_tdls_cs_state state;

		/*
		 * Current cs sta - might be different from periodic cs peer
		 * station. Value is meaningless when the cs-state is idle.
		 */
		u8 cur_sta_id;

		/* TDLS periodic channel-switch peer */
		struct {
			u8 sta_id;
			u8 op_class;
			bool initiator; /* are we the link initiator */
			struct cfg80211_chan_def chandef;
			struct sk_buff *skb; /* ch sw template */
			u32 ch_sw_tm_ie;

			/* timestamp of last ch-sw request sent (GP2 time) */
			u32 sent_timestamp;
		} peer;
	} tdls_cs;


	u32 ciphers[IWL_MVM_NUM_CIPHERS];
	struct ieee80211_cipher_scheme cs[IWL_UCODE_MAX_CS];

	struct cfg80211_ftm_responder_stats ftm_resp_stats;
	struct {
		struct cfg80211_pmsr_request *req;
		struct wireless_dev *req_wdev;
		struct list_head loc_list;
		int responses[IWL_MVM_TOF_MAX_APS];
	} ftm_initiator;

	struct ieee80211_vif *nan_vif;
#define IWL_MAX_BAID	32
	struct iwl_mvm_baid_data __rcu *baid_map[IWL_MAX_BAID];

	/*
	 * Drop beacons from other APs in AP mode when there are no connected
	 * clients.
	 */
	bool drop_bcn_ap_mode;

	struct delayed_work cs_tx_unblock_dwork;

	/* does a monitor vif exist (only one can exist hence bool) */
	bool monitor_on;

	/* sniffer data to include in radiotap */
	__le16 cur_aid;
	u8 cur_bssid[ETH_ALEN];

#ifdef CONFIG_ACPI
	struct iwl_mvm_sar_profile sar_profiles[ACPI_SAR_PROFILE_NUM];
	struct iwl_mvm_geo_profile geo_profiles[ACPI_NUM_GEO_PROFILES];
#endif
};

/* Extract MVM priv from op_mode and _hw */
#define IWL_OP_MODE_GET_MVM(_iwl_op_mode)		\
	((struct iwl_mvm *)(_iwl_op_mode)->op_mode_specific)

#define IWL_MAC80211_GET_MVM(_hw)			\
	IWL_OP_MODE_GET_MVM((struct iwl_op_mode *)((_hw)->priv))

/**
 * enum iwl_mvm_status - MVM status bits
 * @IWL_MVM_STATUS_HW_RFKILL: HW RF-kill is asserted
 * @IWL_MVM_STATUS_HW_CTKILL: CT-kill is active
 * @IWL_MVM_STATUS_ROC_RUNNING: remain-on-channel is running
 * @IWL_MVM_STATUS_HW_RESTART_REQUESTED: HW restart was requested
 * @IWL_MVM_STATUS_IN_HW_RESTART: HW restart is active
 * @IWL_MVM_STATUS_IN_D0I3: NIC is in D0i3
 * @IWL_MVM_STATUS_ROC_AUX_RUNNING: AUX remain-on-channel is running
 * @IWL_MVM_STATUS_FIRMWARE_RUNNING: firmware is running
 * @IWL_MVM_STATUS_NEED_FLUSH_P2P: need to flush P2P bcast STA
 */
enum iwl_mvm_status {
	IWL_MVM_STATUS_HW_RFKILL,
	IWL_MVM_STATUS_HW_CTKILL,
	IWL_MVM_STATUS_ROC_RUNNING,
	IWL_MVM_STATUS_HW_RESTART_REQUESTED,
	IWL_MVM_STATUS_IN_HW_RESTART,
	IWL_MVM_STATUS_IN_D0I3,
	IWL_MVM_STATUS_ROC_AUX_RUNNING,
	IWL_MVM_STATUS_FIRMWARE_RUNNING,
	IWL_MVM_STATUS_NEED_FLUSH_P2P,
};

/* Keep track of completed init configuration */
enum iwl_mvm_init_status {
	IWL_MVM_INIT_STATUS_THERMAL_INIT_COMPLETE = BIT(0),
	IWL_MVM_INIT_STATUS_LEDS_INIT_COMPLETE = BIT(1),
};

static inline bool iwl_mvm_is_radio_killed(struct iwl_mvm *mvm)
{
	return test_bit(IWL_MVM_STATUS_HW_RFKILL, &mvm->status) ||
	       test_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status);
}

static inline bool iwl_mvm_is_radio_hw_killed(struct iwl_mvm *mvm)
{
	return test_bit(IWL_MVM_STATUS_HW_RFKILL, &mvm->status);
}

static inline bool iwl_mvm_firmware_running(struct iwl_mvm *mvm)
{
	return test_bit(IWL_MVM_STATUS_FIRMWARE_RUNNING, &mvm->status);
}

/* Must be called with rcu_read_lock() held and it can only be
 * released when mvmsta is not needed anymore.
 */
static inline struct iwl_mvm_sta *
iwl_mvm_sta_from_staid_rcu(struct iwl_mvm *mvm, u8 sta_id)
{
	struct ieee80211_sta *sta;

	if (sta_id >= ARRAY_SIZE(mvm->fw_id_to_mac_id))
		return NULL;

	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);

	/* This can happen if the station has been removed right now */
	if (IS_ERR_OR_NULL(sta))
		return NULL;

	return iwl_mvm_sta_from_mac80211(sta);
}

static inline struct iwl_mvm_sta *
iwl_mvm_sta_from_staid_protected(struct iwl_mvm *mvm, u8 sta_id)
{
	struct ieee80211_sta *sta;

	if (sta_id >= ARRAY_SIZE(mvm->fw_id_to_mac_id))
		return NULL;

	sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[sta_id],
					lockdep_is_held(&mvm->mutex));

	/* This can happen if the station has been removed right now */
	if (IS_ERR_OR_NULL(sta))
		return NULL;

	return iwl_mvm_sta_from_mac80211(sta);
}

static inline struct ieee80211_vif *
iwl_mvm_rcu_dereference_vif_id(struct iwl_mvm *mvm, u8 vif_id, bool rcu)
{
	if (WARN_ON(vif_id >= ARRAY_SIZE(mvm->vif_id_to_mac)))
		return NULL;

	if (rcu)
		return rcu_dereference(mvm->vif_id_to_mac[vif_id]);

	return rcu_dereference_protected(mvm->vif_id_to_mac[vif_id],
					 lockdep_is_held(&mvm->mutex));
}

static inline bool iwl_mvm_is_d0i3_supported(struct iwl_mvm *mvm)
{
	return !iwlwifi_mod_params.d0i3_disable &&
		fw_has_capa(&mvm->fw->ucode_capa,
			    IWL_UCODE_TLV_CAPA_D0I3_SUPPORT);
}

static inline bool iwl_mvm_is_adaptive_dwell_supported(struct iwl_mvm *mvm)
{
	return fw_has_api(&mvm->fw->ucode_capa,
			  IWL_UCODE_TLV_API_ADAPTIVE_DWELL);
}

static inline bool iwl_mvm_is_adaptive_dwell_v2_supported(struct iwl_mvm *mvm)
{
	return fw_has_api(&mvm->fw->ucode_capa,
			  IWL_UCODE_TLV_API_ADAPTIVE_DWELL_V2);
}

static inline bool iwl_mvm_is_oce_supported(struct iwl_mvm *mvm)
{
	/* OCE should never be enabled for LMAC scan FWs */
	return fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_OCE);
}

static inline bool iwl_mvm_is_frag_ebs_supported(struct iwl_mvm *mvm)
{
	return fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_FRAG_EBS);
}

static inline bool iwl_mvm_is_short_beacon_notif_supported(struct iwl_mvm *mvm)
{
	return fw_has_api(&mvm->fw->ucode_capa,
			  IWL_UCODE_TLV_API_SHORT_BEACON_NOTIF);
}

static inline bool iwl_mvm_enter_d0i3_on_suspend(struct iwl_mvm *mvm)
{
	/* For now we only use this mode to differentiate between
	 * slave transports, which handle D0i3 entry in suspend by
	 * themselves in conjunction with runtime PM D0i3.  So, this
	 * function is used to check whether we need to do anything
	 * when entering suspend or if the transport layer has already
	 * done it.
	 */
	return (mvm->trans->system_pm_mode == IWL_PLAT_PM_MODE_D0I3) &&
		(mvm->trans->runtime_pm_mode != IWL_PLAT_PM_MODE_D0I3);
}

static inline bool iwl_mvm_is_dqa_data_queue(struct iwl_mvm *mvm, u8 queue)
{
	return (queue >= IWL_MVM_DQA_MIN_DATA_QUEUE) &&
	       (queue <= IWL_MVM_DQA_MAX_DATA_QUEUE);
}

static inline bool iwl_mvm_is_dqa_mgmt_queue(struct iwl_mvm *mvm, u8 queue)
{
	return (queue >= IWL_MVM_DQA_MIN_MGMT_QUEUE) &&
	       (queue <= IWL_MVM_DQA_MAX_MGMT_QUEUE);
}

static inline bool iwl_mvm_is_lar_supported(struct iwl_mvm *mvm)
{
	bool nvm_lar = mvm->nvm_data->lar_enabled;
	bool tlv_lar = fw_has_capa(&mvm->fw->ucode_capa,
				   IWL_UCODE_TLV_CAPA_LAR_SUPPORT);

	if (iwlwifi_mod_params.lar_disable)
		return false;

	/*
	 * Enable LAR only if it is supported by the FW (TLV) &&
	 * enabled in the NVM
	 */
	if (mvm->cfg->nvm_type == IWL_NVM_EXT)
		return nvm_lar && tlv_lar;
	else
		return tlv_lar;
}

static inline bool iwl_mvm_is_wifi_mcc_supported(struct iwl_mvm *mvm)
{
	return fw_has_api(&mvm->fw->ucode_capa,
			  IWL_UCODE_TLV_API_WIFI_MCC_UPDATE) ||
	       fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_LAR_MULTI_MCC);
}

static inline bool iwl_mvm_bt_is_rrc_supported(struct iwl_mvm *mvm)
{
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_BT_COEX_RRC) &&
		IWL_MVM_BT_COEX_RRC;
}

static inline bool iwl_mvm_is_csum_supported(struct iwl_mvm *mvm)
{
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_CSUM_SUPPORT) &&
               !IWL_MVM_HW_CSUM_DISABLE;
}

static inline bool iwl_mvm_is_mplut_supported(struct iwl_mvm *mvm)
{
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_BT_MPLUT_SUPPORT) &&
		IWL_MVM_BT_COEX_MPLUT;
}

static inline
bool iwl_mvm_is_p2p_scm_uapsd_supported(struct iwl_mvm *mvm)
{
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_P2P_SCM_UAPSD) &&
		!(iwlwifi_mod_params.uapsd_disable &
		  IWL_DISABLE_UAPSD_P2P_CLIENT);
}

static inline bool iwl_mvm_has_new_rx_api(struct iwl_mvm *mvm)
{
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_MULTI_QUEUE_RX_SUPPORT);
}

static inline bool iwl_mvm_has_new_tx_api(struct iwl_mvm *mvm)
{
	/* TODO - replace with TLV once defined */
	return mvm->trans->cfg->use_tfh;
}

static inline bool iwl_mvm_has_unified_ucode(struct iwl_mvm *mvm)
{
	/* TODO - better define this */
	return mvm->trans->cfg->device_family >= IWL_DEVICE_FAMILY_22000;
}

static inline bool iwl_mvm_is_cdb_supported(struct iwl_mvm *mvm)
{
	/*
	 * TODO:
	 * The issue of how to determine CDB APIs and usage is still not fully
	 * defined.
	 * There is a compilation for CDB and non-CDB FW, but there may
	 * be also runtime check.
	 * For now there is a TLV for checking compilation mode, but a
	 * runtime check will also have to be here - once defined.
	 */
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_CDB_SUPPORT);
}

static inline bool iwl_mvm_cdb_scan_api(struct iwl_mvm *mvm)
{
	/*
	 * TODO: should this be the same as iwl_mvm_is_cdb_supported()?
	 * but then there's a little bit of code in scan that won't make
	 * any sense...
	 */
	return mvm->trans->cfg->device_family >= IWL_DEVICE_FAMILY_22000;
}

static inline bool iwl_mvm_has_new_rx_stats_api(struct iwl_mvm *mvm)
{
	return fw_has_api(&mvm->fw->ucode_capa,
			  IWL_UCODE_TLV_API_NEW_RX_STATS);
}

static inline bool iwl_mvm_has_quota_low_latency(struct iwl_mvm *mvm)
{
	return fw_has_api(&mvm->fw->ucode_capa,
			  IWL_UCODE_TLV_API_QUOTA_LOW_LATENCY);
}

static inline bool iwl_mvm_has_tlc_offload(const struct iwl_mvm *mvm)
{
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_TLC_OFFLOAD);
}

static inline struct agg_tx_status *
iwl_mvm_get_agg_status(struct iwl_mvm *mvm, void *tx_resp)
{
	if (iwl_mvm_has_new_tx_api(mvm))
		return &((struct iwl_mvm_tx_resp *)tx_resp)->status;
	else
		return ((struct iwl_mvm_tx_resp_v3 *)tx_resp)->status;
}

static inline bool iwl_mvm_is_tt_in_fw(struct iwl_mvm *mvm)
{
#ifdef CONFIG_THERMAL
	/* these two TLV are redundant since the responsibility to CT-kill by
	 * FW happens only after we send at least one command of
	 * temperature THs report.
	 */
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_CT_KILL_BY_FW) &&
	       fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_TEMP_THS_REPORT_SUPPORT);
#else /* CONFIG_THERMAL */
	return false;
#endif /* CONFIG_THERMAL */
}

static inline bool iwl_mvm_is_ctdp_supported(struct iwl_mvm *mvm)
{
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_CTDP_SUPPORT);
}

extern const u8 iwl_mvm_ac_to_tx_fifo[];
extern const u8 iwl_mvm_ac_to_gen2_tx_fifo[];

static inline u8 iwl_mvm_mac_ac_to_tx_fifo(struct iwl_mvm *mvm,
					   enum ieee80211_ac_numbers ac)
{
	return iwl_mvm_has_new_tx_api(mvm) ?
		iwl_mvm_ac_to_gen2_tx_fifo[ac] : iwl_mvm_ac_to_tx_fifo[ac];
}

struct iwl_rate_info {
	u8 plcp;	/* uCode API:  IWL_RATE_6M_PLCP, etc. */
	u8 plcp_siso;	/* uCode API:  IWL_RATE_SISO_6M_PLCP, etc. */
	u8 plcp_mimo2;	/* uCode API:  IWL_RATE_MIMO2_6M_PLCP, etc. */
	u8 plcp_mimo3;  /* uCode API:  IWL_RATE_MIMO3_6M_PLCP, etc. */
	u8 ieee;	/* MAC header:  IWL_RATE_6M_IEEE, etc. */
};

void __iwl_mvm_mac_stop(struct iwl_mvm *mvm);
int __iwl_mvm_mac_start(struct iwl_mvm *mvm);

/******************
 * MVM Methods
 ******************/
/* uCode */
int iwl_run_init_mvm_ucode(struct iwl_mvm *mvm, bool read_nvm);

/* Utils */
int iwl_mvm_legacy_rate_to_mac80211_idx(u32 rate_n_flags,
					enum nl80211_band band);
void iwl_mvm_hwrate_to_tx_rate(u32 rate_n_flags,
			       enum nl80211_band band,
			       struct ieee80211_tx_rate *r);
u8 iwl_mvm_mac80211_idx_to_hwrate(int rate_idx);
void iwl_mvm_dump_nic_error_log(struct iwl_mvm *mvm);
u8 first_antenna(u8 mask);
u8 iwl_mvm_next_antenna(struct iwl_mvm *mvm, u8 valid, u8 last_idx);
void iwl_mvm_get_sync_time(struct iwl_mvm *mvm, u32 *gp2, u64 *boottime);
u32 iwl_mvm_get_systime(struct iwl_mvm *mvm);

/* Tx / Host Commands */
int __must_check iwl_mvm_send_cmd(struct iwl_mvm *mvm,
				  struct iwl_host_cmd *cmd);
int __must_check iwl_mvm_send_cmd_pdu(struct iwl_mvm *mvm, u32 id,
				      u32 flags, u16 len, const void *data);
int __must_check iwl_mvm_send_cmd_status(struct iwl_mvm *mvm,
					 struct iwl_host_cmd *cmd,
					 u32 *status);
int __must_check iwl_mvm_send_cmd_pdu_status(struct iwl_mvm *mvm, u32 id,
					     u16 len, const void *data,
					     u32 *status);
int iwl_mvm_tx_skb(struct iwl_mvm *mvm, struct sk_buff *skb,
		   struct ieee80211_sta *sta);
int iwl_mvm_tx_skb_non_sta(struct iwl_mvm *mvm, struct sk_buff *skb);
void iwl_mvm_set_tx_cmd(struct iwl_mvm *mvm, struct sk_buff *skb,
			struct iwl_tx_cmd *tx_cmd,
			struct ieee80211_tx_info *info, u8 sta_id);
void iwl_mvm_set_tx_cmd_rate(struct iwl_mvm *mvm, struct iwl_tx_cmd *tx_cmd,
			    struct ieee80211_tx_info *info,
			    struct ieee80211_sta *sta, __le16 fc);
void iwl_mvm_mac_itxq_xmit(struct ieee80211_hw *hw, struct ieee80211_txq *txq);
unsigned int iwl_mvm_max_amsdu_size(struct iwl_mvm *mvm,
				    struct ieee80211_sta *sta,
				    unsigned int tid);

#ifdef CONFIG_IWLWIFI_DEBUG
const char *iwl_mvm_get_tx_fail_reason(u32 status);
#else
static inline const char *iwl_mvm_get_tx_fail_reason(u32 status) { return ""; }
#endif
int iwl_mvm_flush_tx_path(struct iwl_mvm *mvm, u32 tfd_msk, u32 flags);
int iwl_mvm_flush_sta(struct iwl_mvm *mvm, void *sta, bool internal, u32 flags);
int iwl_mvm_flush_sta_tids(struct iwl_mvm *mvm, u32 sta_id,
			   u16 tids, u32 flags);

void iwl_mvm_async_handlers_purge(struct iwl_mvm *mvm);

static inline void iwl_mvm_set_tx_cmd_ccmp(struct ieee80211_tx_info *info,
					   struct iwl_tx_cmd *tx_cmd)
{
	struct ieee80211_key_conf *keyconf = info->control.hw_key;

	tx_cmd->sec_ctl = TX_CMD_SEC_CCM;
	memcpy(tx_cmd->key, keyconf->key, keyconf->keylen);
}

static inline void iwl_mvm_wait_for_async_handlers(struct iwl_mvm *mvm)
{
	flush_work(&mvm->async_handlers_wk);
}

/* Statistics */
void iwl_mvm_handle_rx_statistics(struct iwl_mvm *mvm,
				  struct iwl_rx_packet *pkt);
void iwl_mvm_rx_statistics(struct iwl_mvm *mvm,
			   struct iwl_rx_cmd_buffer *rxb);
int iwl_mvm_request_statistics(struct iwl_mvm *mvm, bool clear);
void iwl_mvm_accu_radio_stats(struct iwl_mvm *mvm);

/* NVM */
int iwl_nvm_init(struct iwl_mvm *mvm);
int iwl_mvm_load_nvm_to_nic(struct iwl_mvm *mvm);

static inline u8 iwl_mvm_get_valid_tx_ant(struct iwl_mvm *mvm)
{
	return mvm->nvm_data && mvm->nvm_data->valid_tx_ant ?
	       mvm->fw->valid_tx_ant & mvm->nvm_data->valid_tx_ant :
	       mvm->fw->valid_tx_ant;
}

static inline u8 iwl_mvm_get_valid_rx_ant(struct iwl_mvm *mvm)
{
	return mvm->nvm_data && mvm->nvm_data->valid_rx_ant ?
	       mvm->fw->valid_rx_ant & mvm->nvm_data->valid_rx_ant :
	       mvm->fw->valid_rx_ant;
}

static inline void iwl_mvm_toggle_tx_ant(struct iwl_mvm *mvm, u8 *ant)
{
	*ant = iwl_mvm_next_antenna(mvm, iwl_mvm_get_valid_tx_ant(mvm), *ant);
}

static inline u32 iwl_mvm_get_phy_config(struct iwl_mvm *mvm)
{
	u32 phy_config = ~(FW_PHY_CFG_TX_CHAIN |
			   FW_PHY_CFG_RX_CHAIN);
	u32 valid_rx_ant = iwl_mvm_get_valid_rx_ant(mvm);
	u32 valid_tx_ant = iwl_mvm_get_valid_tx_ant(mvm);

	phy_config |= valid_tx_ant << FW_PHY_CFG_TX_CHAIN_POS |
		      valid_rx_ant << FW_PHY_CFG_RX_CHAIN_POS;

	return mvm->fw->phy_config & phy_config;
}

int iwl_mvm_up(struct iwl_mvm *mvm);
int iwl_mvm_load_d3_fw(struct iwl_mvm *mvm);

int iwl_mvm_mac_setup_register(struct iwl_mvm *mvm);
bool iwl_mvm_bcast_filter_build_cmd(struct iwl_mvm *mvm,
				    struct iwl_bcast_filter_cmd *cmd);

/*
 * FW notifications / CMD responses handlers
 * Convention: iwl_mvm_rx_<NAME OF THE CMD>
 */
void iwl_mvm_rx_rx_phy_cmd(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_rx_mpdu(struct iwl_mvm *mvm, struct napi_struct *napi,
			struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_mpdu_mq(struct iwl_mvm *mvm, struct napi_struct *napi,
			struct iwl_rx_cmd_buffer *rxb, int queue);
void iwl_mvm_rx_monitor_no_data(struct iwl_mvm *mvm, struct napi_struct *napi,
				struct iwl_rx_cmd_buffer *rxb, int queue);
void iwl_mvm_rx_frame_release(struct iwl_mvm *mvm, struct napi_struct *napi,
			      struct iwl_rx_cmd_buffer *rxb, int queue);
int iwl_mvm_notify_rx_queue(struct iwl_mvm *mvm, u32 rxq_mask,
			    const u8 *data, u32 count);
void iwl_mvm_rx_queue_notif(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb,
			    int queue);
void iwl_mvm_rx_tx_cmd(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_mfu_assert_dump_notif(struct iwl_mvm *mvm,
				   struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_send_recovery_cmd(struct iwl_mvm *mvm, u32 flags);
void iwl_mvm_rx_ba_notif(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_ant_coupling_notif(struct iwl_mvm *mvm,
				   struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_fw_error(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_card_state_notif(struct iwl_mvm *mvm,
				 struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_mfuart_notif(struct iwl_mvm *mvm,
			     struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_shared_mem_cfg_notif(struct iwl_mvm *mvm,
				     struct iwl_rx_cmd_buffer *rxb);

/* MVM PHY */
int iwl_mvm_phy_ctxt_add(struct iwl_mvm *mvm, struct iwl_mvm_phy_ctxt *ctxt,
			 struct cfg80211_chan_def *chandef,
			 u8 chains_static, u8 chains_dynamic);
int iwl_mvm_phy_ctxt_changed(struct iwl_mvm *mvm, struct iwl_mvm_phy_ctxt *ctxt,
			     struct cfg80211_chan_def *chandef,
			     u8 chains_static, u8 chains_dynamic);
void iwl_mvm_phy_ctxt_ref(struct iwl_mvm *mvm,
			  struct iwl_mvm_phy_ctxt *ctxt);
void iwl_mvm_phy_ctxt_unref(struct iwl_mvm *mvm,
			    struct iwl_mvm_phy_ctxt *ctxt);
int iwl_mvm_phy_ctx_count(struct iwl_mvm *mvm);
u8 iwl_mvm_get_channel_width(struct cfg80211_chan_def *chandef);
u8 iwl_mvm_get_ctrl_pos(struct cfg80211_chan_def *chandef);

/* MAC (virtual interface) programming */
int iwl_mvm_mac_ctxt_init(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_mac_ctxt_add(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_mac_ctxt_changed(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     bool force_assoc_off, const u8 *bssid_override);
int iwl_mvm_mac_ctxt_remove(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_mac_ctxt_beacon_changed(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif);
int iwl_mvm_mac_ctxt_send_beacon(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 struct sk_buff *beacon);
int iwl_mvm_mac_ctxt_send_beacon_cmd(struct iwl_mvm *mvm,
				     struct sk_buff *beacon,
				     void *data, int len);
u8 iwl_mvm_mac_ctxt_get_lowest_rate(struct ieee80211_tx_info *info,
				    struct ieee80211_vif *vif);
void iwl_mvm_mac_ctxt_set_tim(struct iwl_mvm *mvm,
			      __le32 *tim_index, __le32 *tim_size,
			      u8 *beacon, u32 frame_size);
void iwl_mvm_rx_beacon_notif(struct iwl_mvm *mvm,
			     struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_missed_beacons_notif(struct iwl_mvm *mvm,
				     struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_stored_beacon_notif(struct iwl_mvm *mvm,
				    struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_mu_mimo_grp_notif(struct iwl_mvm *mvm,
			       struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_sta_pm_notif(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_window_status_notif(struct iwl_mvm *mvm,
				 struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_mac_ctxt_recalc_tsf_id(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif);
void iwl_mvm_probe_resp_data_notif(struct iwl_mvm *mvm,
				   struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_channel_switch_noa_notif(struct iwl_mvm *mvm,
				      struct iwl_rx_cmd_buffer *rxb);
/* Bindings */
int iwl_mvm_binding_add_vif(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_binding_remove_vif(struct iwl_mvm *mvm, struct ieee80211_vif *vif);

/* Quota management */
static inline size_t iwl_mvm_quota_cmd_size(struct iwl_mvm *mvm)
{
	return iwl_mvm_has_quota_low_latency(mvm) ?
		sizeof(struct iwl_time_quota_cmd) :
		sizeof(struct iwl_time_quota_cmd_v1);
}

static inline struct iwl_time_quota_data
*iwl_mvm_quota_cmd_get_quota(struct iwl_mvm *mvm,
			     struct iwl_time_quota_cmd *cmd,
			     int i)
{
	struct iwl_time_quota_data_v1 *quotas;

	if (iwl_mvm_has_quota_low_latency(mvm))
		return &cmd->quotas[i];

	quotas = (struct iwl_time_quota_data_v1 *)cmd->quotas;
	return (struct iwl_time_quota_data *)&quotas[i];
}

int iwl_mvm_update_quotas(struct iwl_mvm *mvm, bool force_upload,
			  struct ieee80211_vif *disabled_vif);

/* Scanning */
int iwl_mvm_reg_scan_start(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			   struct cfg80211_scan_request *req,
			   struct ieee80211_scan_ies *ies);
int iwl_mvm_scan_size(struct iwl_mvm *mvm);
int iwl_mvm_scan_stop(struct iwl_mvm *mvm, int type, bool notify);
int iwl_mvm_max_scan_ie_len(struct iwl_mvm *mvm);
void iwl_mvm_report_scan_aborted(struct iwl_mvm *mvm);
void iwl_mvm_scan_timeout_wk(struct work_struct *work);

/* Scheduled scan */
void iwl_mvm_rx_lmac_scan_complete_notif(struct iwl_mvm *mvm,
					 struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_lmac_scan_iter_complete_notif(struct iwl_mvm *mvm,
					      struct iwl_rx_cmd_buffer *rxb);
int iwl_mvm_sched_scan_start(struct iwl_mvm *mvm,
			     struct ieee80211_vif *vif,
			     struct cfg80211_sched_scan_request *req,
			     struct ieee80211_scan_ies *ies,
			     int type);
void iwl_mvm_rx_scan_match_found(struct iwl_mvm *mvm,
				 struct iwl_rx_cmd_buffer *rxb);

/* UMAC scan */
int iwl_mvm_config_scan(struct iwl_mvm *mvm);
void iwl_mvm_rx_umac_scan_complete_notif(struct iwl_mvm *mvm,
					 struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_umac_scan_iter_complete_notif(struct iwl_mvm *mvm,
					      struct iwl_rx_cmd_buffer *rxb);

/* MVM debugfs */
#ifdef CONFIG_IWLWIFI_DEBUGFS
void iwl_mvm_dbgfs_register(struct iwl_mvm *mvm, struct dentry *dbgfs_dir);
void iwl_mvm_vif_dbgfs_register(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
void iwl_mvm_vif_dbgfs_clean(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
#else
static inline void iwl_mvm_dbgfs_register(struct iwl_mvm *mvm,
					  struct dentry *dbgfs_dir)
{
}
static inline void
iwl_mvm_vif_dbgfs_register(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
}
static inline void
iwl_mvm_vif_dbgfs_clean(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
}
#endif /* CONFIG_IWLWIFI_DEBUGFS */

/* rate scaling */
int iwl_mvm_send_lq_cmd(struct iwl_mvm *mvm, struct iwl_lq_cmd *lq, bool sync);
void iwl_mvm_update_frame_stats(struct iwl_mvm *mvm, u32 rate, bool agg);
int rs_pretty_print_rate(char *buf, int bufsz, const u32 rate);
void rs_update_last_rssi(struct iwl_mvm *mvm,
			 struct iwl_mvm_sta *mvmsta,
			 struct ieee80211_rx_status *rx_status);

/* power management */
int iwl_mvm_power_update_device(struct iwl_mvm *mvm);
int iwl_mvm_power_update_mac(struct iwl_mvm *mvm);
int iwl_mvm_power_update_ps(struct iwl_mvm *mvm);
int iwl_mvm_power_mac_dbgfs_read(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				 char *buf, int bufsz);

void iwl_mvm_power_vif_assoc(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
void iwl_mvm_power_uapsd_misbehaving_ap_notif(struct iwl_mvm *mvm,
					      struct iwl_rx_cmd_buffer *rxb);

#ifdef CONFIG_IWLWIFI_LEDS
int iwl_mvm_leds_init(struct iwl_mvm *mvm);
void iwl_mvm_leds_exit(struct iwl_mvm *mvm);
void iwl_mvm_leds_sync(struct iwl_mvm *mvm);
#else
static inline int iwl_mvm_leds_init(struct iwl_mvm *mvm)
{
	return 0;
}
static inline void iwl_mvm_leds_exit(struct iwl_mvm *mvm)
{
}
static inline void iwl_mvm_leds_sync(struct iwl_mvm *mvm)
{
}
#endif

/* D3 (WoWLAN, NetDetect) */
int iwl_mvm_suspend(struct ieee80211_hw *hw, struct cfg80211_wowlan *wowlan);
int iwl_mvm_resume(struct ieee80211_hw *hw);
void iwl_mvm_set_wakeup(struct ieee80211_hw *hw, bool enabled);
void iwl_mvm_set_rekey_data(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct cfg80211_gtk_rekey_data *data);
void iwl_mvm_ipv6_addr_change(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct inet6_dev *idev);
void iwl_mvm_set_default_unicast_key(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif, int idx);
extern const struct file_operations iwl_dbgfs_d3_test_ops;
struct iwl_wowlan_status *iwl_mvm_send_wowlan_get_status(struct iwl_mvm *mvm);
#ifdef CONFIG_PM
int iwl_mvm_wowlan_config_key_params(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif,
				     bool host_awake,
				     u32 cmd_flags);
void iwl_mvm_d0i3_update_keys(struct iwl_mvm *mvm,
			      struct ieee80211_vif *vif,
			      struct iwl_wowlan_status *status);
void iwl_mvm_set_last_nonqos_seq(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif);
#else
static inline int iwl_mvm_wowlan_config_key_params(struct iwl_mvm *mvm,
						   struct ieee80211_vif *vif,
						   bool host_awake,
						   u32 cmd_flags)
{
	return 0;
}

static inline void iwl_mvm_d0i3_update_keys(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif,
					    struct iwl_wowlan_status *status)
{
}

static inline void
iwl_mvm_set_last_nonqos_seq(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
}
#endif
void iwl_mvm_set_wowlan_qos_seq(struct iwl_mvm_sta *mvm_ap_sta,
				struct iwl_wowlan_config_cmd *cmd);
int iwl_mvm_send_proto_offload(struct iwl_mvm *mvm,
			       struct ieee80211_vif *vif,
			       bool disable_offloading,
			       bool offload_ns,
			       u32 cmd_flags);

/* D0i3 */
void iwl_mvm_ref(struct iwl_mvm *mvm, enum iwl_mvm_ref_type ref_type);
void iwl_mvm_unref(struct iwl_mvm *mvm, enum iwl_mvm_ref_type ref_type);
int iwl_mvm_ref_sync(struct iwl_mvm *mvm, enum iwl_mvm_ref_type ref_type);
bool iwl_mvm_ref_taken(struct iwl_mvm *mvm);

#ifdef CONFIG_PM
void iwl_mvm_d0i3_enable_tx(struct iwl_mvm *mvm, __le16 *qos_seq);
int iwl_mvm_enter_d0i3(struct iwl_op_mode *op_mode);
int iwl_mvm_exit_d0i3(struct iwl_op_mode *op_mode);
int _iwl_mvm_exit_d0i3(struct iwl_mvm *mvm);
#endif

/* BT Coex */
int iwl_mvm_send_bt_init_conf(struct iwl_mvm *mvm);
void iwl_mvm_rx_bt_coex_notif(struct iwl_mvm *mvm,
			      struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_bt_rssi_event(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			   enum ieee80211_rssi_event_data);
void iwl_mvm_bt_coex_vif_change(struct iwl_mvm *mvm);
u16 iwl_mvm_coex_agg_time_limit(struct iwl_mvm *mvm,
				struct ieee80211_sta *sta);
bool iwl_mvm_bt_coex_is_mimo_allowed(struct iwl_mvm *mvm,
				     struct ieee80211_sta *sta);
bool iwl_mvm_bt_coex_is_ant_avail(struct iwl_mvm *mvm, u8 ant);
bool iwl_mvm_bt_coex_is_shared_ant_avail(struct iwl_mvm *mvm);
bool iwl_mvm_bt_coex_is_tpc_allowed(struct iwl_mvm *mvm,
				    enum nl80211_band band);
u8 iwl_mvm_bt_coex_get_single_ant_msk(struct iwl_mvm *mvm, u8 enabled_ants);
u8 iwl_mvm_bt_coex_tx_prio(struct iwl_mvm *mvm, struct ieee80211_hdr *hdr,
			   struct ieee80211_tx_info *info, u8 ac);

/* beacon filtering */
#ifdef CONFIG_IWLWIFI_DEBUGFS
void
iwl_mvm_beacon_filter_debugfs_parameters(struct ieee80211_vif *vif,
					 struct iwl_beacon_filter_cmd *cmd);
#else
static inline void
iwl_mvm_beacon_filter_debugfs_parameters(struct ieee80211_vif *vif,
					 struct iwl_beacon_filter_cmd *cmd)
{}
#endif
int iwl_mvm_update_d0i3_power_mode(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif,
				   bool enable, u32 flags);
int iwl_mvm_enable_beacon_filter(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 u32 flags);
int iwl_mvm_disable_beacon_filter(struct iwl_mvm *mvm,
				  struct ieee80211_vif *vif,
				  u32 flags);
/* SMPS */
void iwl_mvm_update_smps(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				enum iwl_mvm_smps_type_request req_type,
				enum ieee80211_smps_mode smps_request);
bool iwl_mvm_rx_diversity_allowed(struct iwl_mvm *mvm);

/* Low latency */
int iwl_mvm_update_low_latency(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			      bool low_latency,
			      enum iwl_mvm_low_latency_cause cause);
/* get SystemLowLatencyMode - only needed for beacon threshold? */
bool iwl_mvm_low_latency(struct iwl_mvm *mvm);
bool iwl_mvm_low_latency_band(struct iwl_mvm *mvm, enum nl80211_band band);
void iwl_mvm_send_low_latency_cmd(struct iwl_mvm *mvm, bool low_latency,
				  u16 mac_id);

/* get VMACLowLatencyMode */
static inline bool iwl_mvm_vif_low_latency(struct iwl_mvm_vif *mvmvif)
{
	/*
	 * should this consider associated/active/... state?
	 *
	 * Normally low-latency should only be active on interfaces
	 * that are active, but at least with debugfs it can also be
	 * enabled on interfaces that aren't active. However, when
	 * interface aren't active then they aren't added into the
	 * binding, so this has no real impact. For now, just return
	 * the current desired low-latency state.
	 */
	return mvmvif->low_latency_actual;
}

static inline
void iwl_mvm_vif_set_low_latency(struct iwl_mvm_vif *mvmvif, bool set,
				 enum iwl_mvm_low_latency_cause cause)
{
	u8 new_state;

	if (set)
		mvmvif->low_latency |= cause;
	else
		mvmvif->low_latency &= ~cause;

	/*
	 * if LOW_LATENCY_DEBUGFS_FORCE_ENABLE is enabled no changes are
	 * allowed to actual mode.
	 */
	if (mvmvif->low_latency & LOW_LATENCY_DEBUGFS_FORCE_ENABLE &&
	    cause != LOW_LATENCY_DEBUGFS_FORCE_ENABLE)
		return;

	if (cause == LOW_LATENCY_DEBUGFS_FORCE_ENABLE && set)
		/*
		 * We enter force state
		 */
		new_state = !!(mvmvif->low_latency &
			       LOW_LATENCY_DEBUGFS_FORCE);
	else
		/*
		 * Check if any other one set low latency
		 */
		new_state = !!(mvmvif->low_latency &
				  ~(LOW_LATENCY_DEBUGFS_FORCE_ENABLE |
				    LOW_LATENCY_DEBUGFS_FORCE));

	mvmvif->low_latency_actual = new_state;
}

/* Return a bitmask with all the hw supported queues, except for the
 * command queue, which can't be flushed.
 */
static inline u32 iwl_mvm_flushable_queues(struct iwl_mvm *mvm)
{
	return ((BIT(mvm->cfg->base_params->num_of_queues) - 1) &
		~BIT(IWL_MVM_DQA_CMD_QUEUE));
}

static inline void iwl_mvm_stop_device(struct iwl_mvm *mvm)
{
	lockdep_assert_held(&mvm->mutex);
	iwl_fw_cancel_timestamp(&mvm->fwrt);
	clear_bit(IWL_MVM_STATUS_FIRMWARE_RUNNING, &mvm->status);
	iwl_fwrt_stop_device(&mvm->fwrt);
	iwl_free_fw_paging(&mvm->fwrt);
	iwl_fw_dump_conf_clear(&mvm->fwrt);
}

/* Re-configure the SCD for a queue that has already been configured */
int iwl_mvm_reconfig_scd(struct iwl_mvm *mvm, int queue, int fifo, int sta_id,
			 int tid, int frame_limit, u16 ssn);

/* Thermal management and CT-kill */
void iwl_mvm_tt_tx_backoff(struct iwl_mvm *mvm, u32 backoff);
void iwl_mvm_tt_temp_changed(struct iwl_mvm *mvm, u32 temp);
void iwl_mvm_temp_notif(struct iwl_mvm *mvm,
			struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_tt_handler(struct iwl_mvm *mvm);
void iwl_mvm_thermal_initialize(struct iwl_mvm *mvm, u32 min_backoff);
void iwl_mvm_thermal_exit(struct iwl_mvm *mvm);
void iwl_mvm_set_hw_ctkill_state(struct iwl_mvm *mvm, bool state);
int iwl_mvm_get_temp(struct iwl_mvm *mvm, s32 *temp);
void iwl_mvm_ct_kill_notif(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_enter_ctkill(struct iwl_mvm *mvm);
int iwl_mvm_send_temp_report_ths_cmd(struct iwl_mvm *mvm);
int iwl_mvm_ctdp_command(struct iwl_mvm *mvm, u32 op, u32 budget);

/* Location Aware Regulatory */
struct iwl_mcc_update_resp *
iwl_mvm_update_mcc(struct iwl_mvm *mvm, const char *alpha2,
		   enum iwl_mcc_source src_id);
int iwl_mvm_init_mcc(struct iwl_mvm *mvm);
void iwl_mvm_rx_chub_update_mcc(struct iwl_mvm *mvm,
				struct iwl_rx_cmd_buffer *rxb);
struct ieee80211_regdomain *iwl_mvm_get_regdomain(struct wiphy *wiphy,
						  const char *alpha2,
						  enum iwl_mcc_source src_id,
						  bool *changed);
struct ieee80211_regdomain *iwl_mvm_get_current_regdomain(struct iwl_mvm *mvm,
							  bool *changed);
int iwl_mvm_init_fw_regd(struct iwl_mvm *mvm);
void iwl_mvm_update_changed_regdom(struct iwl_mvm *mvm);

/* smart fifo */
int iwl_mvm_sf_update(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		      bool added_vif);

/* FTM responder */
int iwl_mvm_ftm_start_responder(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
void iwl_mvm_ftm_restart_responder(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif);
void iwl_mvm_ftm_responder_stats(struct iwl_mvm *mvm,
				 struct iwl_rx_cmd_buffer *rxb);

/* FTM initiator */
void iwl_mvm_ftm_restart(struct iwl_mvm *mvm);
void iwl_mvm_ftm_range_resp(struct iwl_mvm *mvm,
			    struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_ftm_lc_notif(struct iwl_mvm *mvm,
			  struct iwl_rx_cmd_buffer *rxb);
int iwl_mvm_ftm_start(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		      struct cfg80211_pmsr_request *request);
void iwl_mvm_ftm_abort(struct iwl_mvm *mvm, struct cfg80211_pmsr_request *req);

/* TDLS */

/*
 * We use TID 4 (VI) as a FW-used-only TID when TDLS connections are present.
 * This TID is marked as used vs the AP and all connected TDLS peers.
 */
#define IWL_MVM_TDLS_FW_TID 4

int iwl_mvm_tdls_sta_count(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
void iwl_mvm_teardown_tdls_peers(struct iwl_mvm *mvm);
void iwl_mvm_recalc_tdls_state(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			       bool sta_added);
void iwl_mvm_mac_mgd_protect_tdls_discover(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif);
int iwl_mvm_tdls_channel_switch(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta, u8 oper_class,
				struct cfg80211_chan_def *chandef,
				struct sk_buff *tmpl_skb, u32 ch_sw_tm_ie);
void iwl_mvm_tdls_recv_channel_switch(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_tdls_ch_sw_params *params);
void iwl_mvm_tdls_cancel_channel_switch(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_sta *sta);
void iwl_mvm_rx_tdls_notif(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_tdls_ch_switch_work(struct work_struct *work);

void iwl_mvm_sync_rx_queues_internal(struct iwl_mvm *mvm,
				     struct iwl_mvm_internal_rxq_notif *notif,
				     u32 size);
void iwl_mvm_reorder_timer_expired(struct timer_list *t);
struct ieee80211_vif *iwl_mvm_get_bss_vif(struct iwl_mvm *mvm);
bool iwl_mvm_is_vif_assoc(struct iwl_mvm *mvm);

#define MVM_TCM_PERIOD_MSEC 500
#define MVM_TCM_PERIOD (HZ * MVM_TCM_PERIOD_MSEC / 1000)
#define MVM_LL_PERIOD (10 * HZ)
void iwl_mvm_tcm_work(struct work_struct *work);
void iwl_mvm_recalc_tcm(struct iwl_mvm *mvm);
void iwl_mvm_pause_tcm(struct iwl_mvm *mvm, bool with_cancel);
void iwl_mvm_resume_tcm(struct iwl_mvm *mvm);
void iwl_mvm_tcm_add_vif(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
void iwl_mvm_tcm_rm_vif(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
u8 iwl_mvm_tcm_load_percentage(u32 airtime, u32 elapsed);

void iwl_mvm_nic_restart(struct iwl_mvm *mvm, bool fw_error);
unsigned int iwl_mvm_get_wd_timeout(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    bool tdls, bool cmd_q);
void iwl_mvm_connection_loss(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     const char *errmsg);
void iwl_mvm_event_frame_timeout_callback(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  const struct ieee80211_sta *sta,
					  u16 tid);

int iwl_mvm_sar_select_profile(struct iwl_mvm *mvm, int prof_a, int prof_b);
int iwl_mvm_get_sar_geo_profile(struct iwl_mvm *mvm);
#ifdef CONFIG_IWLWIFI_DEBUGFS
void iwl_mvm_sta_add_debugfs(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct dentry *dir);
#endif

/* Channel info utils */
static inline bool iwl_mvm_has_ultra_hb_channel(struct iwl_mvm *mvm)
{
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS);
}

static inline void *iwl_mvm_chan_info_cmd_tail(struct iwl_mvm *mvm,
					       struct iwl_fw_channel_info *ci)
{
	return (u8 *)ci + (iwl_mvm_has_ultra_hb_channel(mvm) ?
			   sizeof(struct iwl_fw_channel_info) :
			   sizeof(struct iwl_fw_channel_info_v1));
}

static inline size_t iwl_mvm_chan_info_padding(struct iwl_mvm *mvm)
{
	return iwl_mvm_has_ultra_hb_channel(mvm) ? 0 :
		sizeof(struct iwl_fw_channel_info) -
		sizeof(struct iwl_fw_channel_info_v1);
}

static inline void iwl_mvm_set_chan_info(struct iwl_mvm *mvm,
					 struct iwl_fw_channel_info *ci,
					 u32 chan, u8 band, u8 width,
					 u8 ctrl_pos)
{
	if (iwl_mvm_has_ultra_hb_channel(mvm)) {
		ci->channel = cpu_to_le32(chan);
		ci->band = band;
		ci->width = width;
		ci->ctrl_pos = ctrl_pos;
	} else {
		struct iwl_fw_channel_info_v1 *ci_v1 =
					(struct iwl_fw_channel_info_v1 *)ci;

		ci_v1->channel = chan;
		ci_v1->band = band;
		ci_v1->width = width;
		ci_v1->ctrl_pos = ctrl_pos;
	}
}

static inline void
iwl_mvm_set_chan_info_chandef(struct iwl_mvm *mvm,
			      struct iwl_fw_channel_info *ci,
			      struct cfg80211_chan_def *chandef)
{
	iwl_mvm_set_chan_info(mvm, ci, chandef->chan->hw_value,
			      (chandef->chan->band == NL80211_BAND_2GHZ ?
			       PHY_BAND_24 : PHY_BAND_5),
			       iwl_mvm_get_channel_width(chandef),
			       iwl_mvm_get_ctrl_pos(chandef));
}

#endif /* __IWL_MVM_H__ */
