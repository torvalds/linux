/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014, 2018-2025 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __IWL_MVM_H__
#define __IWL_MVM_H__

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/cleanup.h>
#include <linux/leds.h>
#include <linux/in6.h>

#ifdef CONFIG_THERMAL
#include <linux/thermal.h>
#endif

#include <linux/ptp_clock_kernel.h>

#include <linux/ktime.h>

#include "iwl-op-mode.h"
#include "iwl-trans.h"
#include "fw/notif-wait.h"
#include "iwl-nvm-utils.h"
#include "fw/file.h"
#include "iwl-config.h"
#include "sta.h"
#include "fw-api.h"
#include "constants.h"
#include "fw/runtime.h"
#include "fw/dbg.h"
#include "fw/acpi.h"
#include "mei/iwl-mei.h"
#include "iwl-nvm-parse.h"

#include <linux/average.h>

#define IWL_MVM_MAX_ADDRESSES		5
/* RSSI offset for WkP */
#define IWL_RSSI_OFFSET 50
#define IWL_MVM_MISSED_BEACONS_SINCE_RX_THOLD 4
#define IWL_MVM_MISSED_BEACONS_THRESHOLD 8
#define IWL_MVM_MISSED_BEACONS_THRESHOLD_LONG 19

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

/* invalid value for FW link id */
#define IWL_MVM_FW_LINK_ID_INVALID 0xff

extern const struct ieee80211_ops iwl_mvm_hw_ops;
extern const struct ieee80211_ops iwl_mvm_mld_hw_ops;

/**
 * struct iwl_mvm_mod_params - module parameters for iwlmvm
 * @power_scheme: one of enum iwl_power_scheme
 */
struct iwl_mvm_mod_params {
	int power_scheme;
};
extern struct iwl_mvm_mod_params iwlmvm_mod_params;

struct iwl_mvm_phy_ctxt {
	u16 id;
	u16 color;
	u32 ref;

	enum nl80211_chan_width width;

	struct ieee80211_channel *channel;

	/* track for RLC config command */
	u32 center_freq1;
	bool rlc_disabled;
	u32 channel_load_by_us;
	u32 channel_load_not_by_us;
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
	s8 link_id;
};

 /* Power management */

/**
 * enum iwl_power_scheme - iwl power schemes
 * @IWL_POWER_SCHEME_CAM: Continuously Active Mode
 * @IWL_POWER_SCHEME_BPS: Balanced Power Save (default)
 * @IWL_POWER_SCHEME_LP: Low Power
 */
enum iwl_power_scheme {
	IWL_POWER_SCHEME_CAM = 1,
	IWL_POWER_SCHEME_BPS,
	IWL_POWER_SCHEME_LP
};

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
	IWL_MVM_SMPS_REQ_FW,
	NUM_IWL_MVM_SMPS_REQ,
};

enum iwl_bt_force_ant_mode {
	BT_FORCE_ANT_DIS = 0,
	BT_FORCE_ANT_AUTO,
	BT_FORCE_ANT_BT,
	BT_FORCE_ANT_WIFI,

	BT_FORCE_ANT_MAX,
};

/**
 * enum iwl_mvm_low_latency_force - low latency force mode set by debugfs
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
* enum iwl_mvm_low_latency_cause - low latency set causes
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
* struct iwl_mvm_link_bf_data - beacon filtering related data
* @ave_beacon_signal: average beacon signal
* @last_cqm_event: rssi of the last cqm event
* @bt_coex_min_thold: minimum threshold for BT coex
* @bt_coex_max_thold: maximum threshold for BT coex
* @last_bt_coex_event: rssi of the last BT coex event
*/
struct iwl_mvm_link_bf_data {
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
 * struct iwl_mvm_vif_link_info - per link data in Virtual Interface
 * @ap_sta_id: the sta_id of the AP - valid only if VIF type is STA
 * @fw_link_id: the id of the link according to the FW API
 * @bssid: BSSID for this (client) interface
 * @bcast_sta: station used for broadcast packets. Used by the following
 *	vifs: P2P_DEVICE, GO and AP.
 * @beacon_stats: beacon statistics, containing the # of received beacons,
 *	# of received beacons accumulated over FW restart, and the current
 *	average signal of beacons retrieved from the firmware
 * @smps_requests: the SMPS requests of different parts of the driver,
 *	combined on update to yield the overall request to mac80211.
 * @probe_resp_data: data from FW notification to store NOA and CSA related
 *	data to be inserted into probe response.
 * @he_ru_2mhz_block: 26-tone RU OFDMA transmissions should be blocked
 * @queue_params: QoS params for this MAC
 * @mgmt_queue: queue number for unbufferable management frames
 * @igtk: the current IGTK programmed into the firmware
 * @active: indicates the link is active in FW (for sanity checking)
 * @cab_queue: content-after-beacon (multicast) queue
 * @listen_lmac: indicates this link is allocated to the listen LMAC
 * @csa_block_tx: we got CSA with mode=1
 * @mcast_sta: multicast station
 * @phy_ctxt: phy context allocated to this link, if any
 * @bf_data: beacon filtering data
 */
struct iwl_mvm_vif_link_info {
	u8 bssid[ETH_ALEN];
	u8 ap_sta_id;
	u8 fw_link_id;

	struct iwl_mvm_int_sta bcast_sta;
	struct iwl_mvm_int_sta mcast_sta;

	struct {
		u32 num_beacons, accu_num_beacons;
		u8 avg_signal;
	} beacon_stats;

	enum ieee80211_smps_mode smps_requests[NUM_IWL_MVM_SMPS_REQ];
	struct iwl_probe_resp_data __rcu *probe_resp_data;

	struct ieee80211_key_conf *igtk;

	bool he_ru_2mhz_block;
	bool active;
	bool listen_lmac;
	bool csa_block_tx;

	u16 cab_queue;
	/* Assigned while mac80211 has the link in a channel context,
	 * or, for P2P Device, while it exists.
	 */
	struct iwl_mvm_phy_ctxt *phy_ctxt;
	/* QoS data from mac80211, need to store this here
	 * as mac80211 has a separate callback but we need
	 * to have the data for the MAC context
	 */
	struct ieee80211_tx_queue_params queue_params[IEEE80211_NUM_ACS];

	u16 mgmt_queue;

	struct iwl_mvm_link_bf_data bf_data;
};

/**
 * enum iwl_mvm_esr_state - defines reasons for which the EMLSR is exited or
 * blocked.
 * The low 16 bits are used for blocking reasons, and the 16 higher bits
 * are used for exit reasons.
 * For the blocking reasons - use iwl_mvm_(un)block_esr(), and for the exit
 * reasons - use iwl_mvm_exit_esr().
 *
 * Note: new reasons shall be added to HANDLE_ESR_REASONS as well (for logs)
 *
 * @IWL_MVM_ESR_BLOCKED_PREVENTION: Prevent EMLSR to avoid entering and exiting
 *	in a loop.
 * @IWL_MVM_ESR_BLOCKED_WOWLAN: WOWLAN is preventing the enablement of EMLSR
 * @IWL_MVM_ESR_BLOCKED_TPT: block EMLSR when there is not enough traffic
 * @IWL_MVM_ESR_BLOCKED_FW: FW didn't recommended/forced exit from EMLSR
 * @IWL_MVM_ESR_BLOCKED_NON_BSS: An active non-BSS interface's link is
 *	preventing EMLSR
 * @IWL_MVM_ESR_BLOCKED_ROC: remain-on-channel is preventing EMLSR
 * @IWL_MVM_ESR_BLOCKED_TMP_NON_BSS: An expected active non-BSS interface's link
 *      is preventing EMLSR. This is a temporary blocking that is set when there
 *      is an indication that a non-BSS interface is to be added.
 * @IWL_MVM_ESR_EXIT_MISSED_BEACON: exited EMLSR due to missed beacons
 * @IWL_MVM_ESR_EXIT_LOW_RSSI: link is deactivated/not allowed for EMLSR
 *	due to low RSSI.
 * @IWL_MVM_ESR_EXIT_COEX: link is deactivated/not allowed for EMLSR
 *	due to BT Coex.
 * @IWL_MVM_ESR_EXIT_BANDWIDTH: Bandwidths of primary and secondry links
 *	preventing the enablement of EMLSR
 * @IWL_MVM_ESR_EXIT_CSA: CSA happened, so exit EMLSR
 * @IWL_MVM_ESR_EXIT_LINK_USAGE: Exit EMLSR due to low tpt on secondary link
 * @IWL_MVM_ESR_EXIT_FAIL_ENTRY: Exit EMLSR due to entry failure
 */
enum iwl_mvm_esr_state {
	IWL_MVM_ESR_BLOCKED_PREVENTION	= 0x1,
	IWL_MVM_ESR_BLOCKED_WOWLAN	= 0x2,
	IWL_MVM_ESR_BLOCKED_TPT		= 0x4,
	IWL_MVM_ESR_BLOCKED_FW		= 0x8,
	IWL_MVM_ESR_BLOCKED_NON_BSS	= 0x10,
	IWL_MVM_ESR_BLOCKED_ROC		= 0x20,
	IWL_MVM_ESR_BLOCKED_TMP_NON_BSS	= 0x40,
	IWL_MVM_ESR_EXIT_MISSED_BEACON	= 0x10000,
	IWL_MVM_ESR_EXIT_LOW_RSSI	= 0x20000,
	IWL_MVM_ESR_EXIT_COEX		= 0x40000,
	IWL_MVM_ESR_EXIT_BANDWIDTH	= 0x80000,
	IWL_MVM_ESR_EXIT_CSA		= 0x100000,
	IWL_MVM_ESR_EXIT_LINK_USAGE	= 0x200000,
	IWL_MVM_ESR_EXIT_FAIL_ENTRY	= 0x400000,
};

#define IWL_MVM_BLOCK_ESR_REASONS 0xffff

const char *iwl_get_esr_state_string(enum iwl_mvm_esr_state state);

/**
 * struct iwl_mvm_esr_exit - details of the last exit from EMLSR mode.
 * @reason: The reason for the last exit from EMLSR.
 *	&iwl_mvm_prevent_esr_reasons. Will be 0 before exiting EMLSR.
 * @ts: the time stamp of the last time we existed EMLSR.
 */
struct iwl_mvm_esr_exit {
	unsigned long ts;
	enum iwl_mvm_esr_state reason;
};

/**
 * struct iwl_mvm_vif - data per Virtual Interface, it is a MAC context
 * @mvm: pointer back to the mvm struct
 * @id: between 0 and 3
 * @color: to solve races upon MAC addition and removal
 * @associated: indicates that we're currently associated, used only for
 *	managing the firmware state in iwl_mvm_bss_info_changed_station()
 * @ap_assoc_sta_count: count of stations associated to us - valid only
 *	if VIF type is AP
 * @uploaded: indicates the MAC context has been added to the device
 * @ap_ibss_active: indicates that AP/IBSS is configured and that the interface
 *	should get quota etc.
 * @pm_enabled - indicate if MAC power management is allowed
 * @monitor_active: indicates that monitor context is configured, and that the
 *	interface should get quota etc.
 * @low_latency: bit flags for low latency
 *	see enum &iwl_mvm_low_latency_cause for causes.
 * @low_latency_actual: boolean, indicates low latency is set,
 *	as a result from low_latency bit flags and takes force into account.
 * @authorized: indicates the AP station was set to authorized
 * @ps_disabled: indicates that this interface requires PS to be disabled
 * @csa_countdown: indicates that CSA countdown may be started
 * @csa_failed: CSA failed to schedule time event, report an error later
 * @csa_bcn_pending: indicates that we are waiting for a beacon on a new channel
 * @csa_blocks_tx: CSA is blocking TX
 * @features: hw features active for this vif
 * @max_tx_op: max TXOP in usecs for all ACs, zero for no limit.
 * @ap_beacon_time: AP beacon time for synchronisation (on older FW)
 * @bf_enabled: indicates if beacon filtering is enabled
 * @ba_enabled: indicated if beacon abort is enabled
 * @bcn_prot: beacon protection data (keys; FIXME: needs to be per link)
 * @deflink: default link data for use in non-MLO
 * @link: link data for each link in MLO
 * @esr_active: indicates eSR mode is active
 * @esr_disable_reason: a bitmap of &enum iwl_mvm_esr_state
 * @pm_enabled: indicates powersave is enabled
 * @link_selection_res: bitmap of active links as it was decided in the last
 *	link selection. Valid only for a MLO vif after assoc. 0 if there wasn't
 *	any link selection yet.
 * @link_selection_primary: primary link selected by link selection
 * @primary_link: primary link in eSR. Valid only for an associated MLD vif,
 *	and in eSR mode. Valid only for a STA.
 * @last_esr_exit: Details of the last exit from EMLSR.
 * @exit_same_reason_count: The number of times we exited due to the specified
 *	@last_esr_exit::reason, only counting exits due to
 *	&IWL_MVM_ESR_PREVENT_REASONS.
 * @prevent_esr_done_wk: work that should be done when esr prevention ends.
 * @mlo_int_scan_wk: work for the internal MLO scan.
 * @unblock_esr_tpt_wk: work for unblocking EMLSR when tpt is high enough.
 * @unblock_esr_tmp_non_bss_wk: work for removing the
 *      IWL_MVM_ESR_BLOCKED_TMP_NON_BSS blocking for EMLSR.
 * @roc_activity: currently running ROC activity for this vif (or
 *	ROC_NUM_ACTIVITIES if no activity is running).
 * @session_prot_connection_loss: the connection was lost due to session
 *	protection ending without receiving a beacon, so we need to now
 *	protect the deauth separately
 * @ap_early_keys: The firmware cannot install keys before stations etc.,
 *	but higher layers work differently, so we store the keys here for
 *	later installation.
 * @ap_sta: pointer to the AP STA data structure
 * @csa_count: CSA counter (old CSA implementation w/o firmware)
 * @csa_misbehave: CSA AP misbehaviour flag (old implementation)
 * @csa_target_freq: CSA target channel frequency (old implementation)
 * @csa_work: CSA work (old implementation)
 * @dbgfs_bf: beamforming debugfs data
 * @dbgfs_dir: debugfs directory for this vif
 * @dbgfs_pm: power management debugfs data
 * @dbgfs_quota_min: debugfs value for minimal quota
 * @dbgfs_slink: debugfs symlink for this interface
 * @ftm_unprotected: unprotected FTM debugfs override
 * @hs_time_event_data: hotspot/AUX ROC time event data
 * @mac_pwr_cmd: debugfs override for MAC power command
 * @target_ipv6_addrs: IPv6 addresses on this interface for offload
 * @num_target_ipv6_addrs: number of @target_ipv6_addrs
 * @tentative_addrs: bitmap of tentative IPv6 addresses in @target_ipv6_addrs
 * @rekey_data: rekeying data for WoWLAN GTK rekey offload
 * @seqno: storage for seqno for older firmware D0/D3 transition
 * @seqno_valid: indicates @seqno is valid
 * @time_event_data: session protection time event data
 * @tsf_id: the TSF resource ID assigned in firmware (for firmware needing that)
 * @tx_key_idx: WEP transmit key index for D3
 * @uapsd_misbehaving_ap_addr: MLD address/BSSID of U-APSD misbehaving AP, to
 *	not use U-APSD on reconnection
 * @uapsd_nonagg_detected_wk: worker for handling detection of no aggregation
 *	in U-APSD
 */
struct iwl_mvm_vif {
	struct iwl_mvm *mvm;
	u16 id;
	u16 color;

	bool associated;
	u8 ap_assoc_sta_count;
	bool uploaded;
	bool ap_ibss_active;
	bool pm_enabled;
	bool monitor_active;
	bool esr_active;
	bool session_prot_connection_loss;

	u8 low_latency: 6;
	u8 low_latency_actual: 1;

	u8 authorized:1;
	bool ps_disabled;

	u32 esr_disable_reason;
	u32 ap_beacon_time;
	bool bf_enabled;
	bool ba_enabled;

#ifdef CONFIG_PM_SLEEP
	/* WoWLAN GTK rekey data */
	struct {
		u8 kck[NL80211_KCK_EXT_LEN];
		u8 kek[NL80211_KEK_EXT_LEN];
		size_t kek_len;
		size_t kck_len;
		u32 akm;
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
	bool ftm_unprotected;
#endif

	/* FW identified misbehaving AP */
	u8 uapsd_misbehaving_ap_addr[ETH_ALEN] __aligned(2);
	struct delayed_work uapsd_nonagg_detected_wk;

	bool csa_countdown;
	bool csa_failed;
	bool csa_bcn_pending;
	bool csa_blocks_tx;
	u16 csa_target_freq;
	u16 csa_count;
	u16 csa_misbehave;
	struct delayed_work csa_work;

	enum iwl_tsf_id tsf_id;

	struct iwl_mvm_time_event_data time_event_data;
	struct iwl_mvm_time_event_data hs_time_event_data;
	enum iwl_roc_activity roc_activity;

	/* TCP Checksum Offload */
	netdev_features_t features;

	struct ieee80211_sta *ap_sta;

	/* we can only have 2 GTK + 2 IGTK active at a time */
	struct ieee80211_key_conf *ap_early_keys[4];

	struct {
		struct ieee80211_key_conf __rcu *keys[2];
	} bcn_prot;

	u16 max_tx_op;

	u16 link_selection_res;
	u8 link_selection_primary;
	u8 primary_link;
	struct iwl_mvm_esr_exit last_esr_exit;
	u8 exit_same_reason_count;
	struct wiphy_delayed_work prevent_esr_done_wk;
	struct wiphy_delayed_work mlo_int_scan_wk;
	struct wiphy_work unblock_esr_tpt_wk;
	struct wiphy_delayed_work unblock_esr_tmp_non_bss_wk;

	struct iwl_mvm_vif_link_info deflink;
	struct iwl_mvm_vif_link_info *link[IEEE80211_MLD_MAX_NUM_LINKS];
};

#define for_each_mvm_vif_valid_link(mvm_vif, link_id)			\
	for (link_id = 0;						\
	     link_id < ARRAY_SIZE((mvm_vif)->link);			\
	     link_id++)							\
		if ((mvm_vif)->link[link_id])

static inline struct iwl_mvm_vif *
iwl_mvm_vif_from_mac80211(struct ieee80211_vif *vif)
{
	return (void *)vif->drv_priv;
}

extern const u8 tid_to_mac80211_ac[];

#define IWL_MVM_SCAN_STOPPING_SHIFT	8

enum iwl_scan_status {
	IWL_MVM_SCAN_REGULAR		= BIT(0),
	IWL_MVM_SCAN_SCHED		= BIT(1),
	IWL_MVM_SCAN_NETDETECT		= BIT(2),
	IWL_MVM_SCAN_INT_MLO		= BIT(3),

	IWL_MVM_SCAN_STOPPING_REGULAR	= BIT(8),
	IWL_MVM_SCAN_STOPPING_SCHED	= BIT(9),
	IWL_MVM_SCAN_STOPPING_NETDETECT	= BIT(10),
	IWL_MVM_SCAN_STOPPING_INT_MLO	= BIT(11),

	IWL_MVM_SCAN_REGULAR_MASK	= IWL_MVM_SCAN_REGULAR |
					  IWL_MVM_SCAN_STOPPING_REGULAR,
	IWL_MVM_SCAN_SCHED_MASK		= IWL_MVM_SCAN_SCHED |
					  IWL_MVM_SCAN_STOPPING_SCHED,
	IWL_MVM_SCAN_NETDETECT_MASK	= IWL_MVM_SCAN_NETDETECT |
					  IWL_MVM_SCAN_STOPPING_NETDETECT,
	IWL_MVM_SCAN_INT_MLO_MASK       = IWL_MVM_SCAN_INT_MLO |
					  IWL_MVM_SCAN_STOPPING_INT_MLO,

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
 * struct iwl_mvm_tt_mgmt - Thermal Throttling Management structure
 * @ct_kill_exit: worker to exit thermal kill
 * @dynamic_smps: Is thermal throttling enabled dynamic_smps?
 * @tx_backoff: The current thremal throttling tx backoff in uSec.
 * @min_backoff: The minimal tx backoff due to power restrictions
 * @params: Parameters to configure the thermal throttling algorithm.
 * @throttle: Is thermal throttling is active?
 * @power_budget_mw: maximum cTDP power budget as defined for this system and
 *	device
 */
struct iwl_mvm_tt_mgmt {
	struct delayed_work ct_kill_exit;
	bool dynamic_smps;
	u32 tx_backoff;
	u32 min_backoff;
	struct iwl_tt_params params;
	bool throttle;

	u32 power_budget_mw;
};

#ifdef CONFIG_THERMAL
/**
 * struct iwl_mvm_thermal_device - thermal zone related data
 * @trips: temperature thresholds for report
 * @tzone: thermal zone device data
*/
struct iwl_mvm_thermal_device {
	struct thermal_trip trips[IWL_MAX_DTS_TRIPS];
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
	} result;
};

/**
 * struct iwl_mvm_reorder_buffer - per ra/tid/queue reorder buffer
 * @head_sn: reorder window head sn
 * @num_stored: number of mpdus stored in the buffer
 * @queue: queue of this reorder buffer
 * @valid: reordering is valid for this queue
 * @lock: protect reorder buffer internal state
 */
struct iwl_mvm_reorder_buffer {
	u16 head_sn;
	u16 num_stored;
	int queue;
	bool valid;
	spinlock_t lock;
} ____cacheline_aligned_in_smp;

/**
 * struct iwl_mvm_reorder_buf_entry - reorder buffer entry per-queue/per-seqno
 * @frames: list of skbs stored
 */
struct iwl_mvm_reorder_buf_entry {
	struct sk_buff_head frames;
}
#ifndef __CHECKER__
/* sparse doesn't like this construct: "bad integer constant expression" */
__aligned(roundup_pow_of_two(sizeof(struct sk_buff_head)))
#endif
;

/**
 * struct iwl_mvm_baid_data - BA session data
 * @sta_mask: current station mask for the BAID
 * @tid: tid of the session
 * @baid: baid of the session
 * @timeout: the timeout set in the addba request
 * @buf_size: the reorder buffer size as set by the last addba request
 * @entries_per_queue: # of buffers per queue, this actually gets
 *	aligned up to avoid cache line sharing between queues
 * @last_rx: last rx jiffies, updated only if timeout passed from last update
 * @session_timer: timer to check if BA session expired, runs at 2 * timeout
 * @rcu_ptr: BA data RCU protected access
 * @rcu_head: RCU head for freeing this data
 * @mvm: mvm pointer, needed for timer context
 * @reorder_buf: reorder buffer, allocated per queue
 * @entries: data
 */
struct iwl_mvm_baid_data {
	struct rcu_head rcu_head;
	u32 sta_mask;
	u8 tid;
	u8 baid;
	u16 timeout;
	u16 buf_size;
	u16 entries_per_queue;
	unsigned long last_rx;
	struct timer_list session_timer;
	struct iwl_mvm_baid_data __rcu **rcu_ptr;
	struct iwl_mvm *mvm;
	struct iwl_mvm_reorder_buffer reorder_buf[IWL_MAX_RX_HW_QUEUES];
	struct iwl_mvm_reorder_buf_entry entries[] ____cacheline_aligned_in_smp;
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


struct iwl_mvm_txq {
	struct list_head list;
	u16 txq_id;
	atomic_t tx_request;
#define IWL_MVM_TXQ_STATE_READY		0
#define IWL_MVM_TXQ_STATE_STOP_FULL	1
#define IWL_MVM_TXQ_STATE_STOP_REDIRECT	2
#define IWL_MVM_TXQ_STATE_STOP_AP_CSA	3
	unsigned long state;
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

struct ptp_data {
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_info;

	struct delayed_work dwork;

	/* The last GP2 reading from the hw */
	u32 last_gp2;

	/* number of wraparounds since scale_update_adj_time_ns */
	u32 wrap_counter;

	/* GP2 time when the scale was last updated */
	u32 scale_update_gp2;

	/* Adjusted time when the scale was last updated in nanoseconds */
	u64 scale_update_adj_time_ns;

	/* clock frequency offset, scaled to 65536000000 */
	u64 scaled_freq;

	/* Delta between hardware clock and ptp clock in nanoseconds */
	s64 delta;
};

struct iwl_time_sync_data {
	struct sk_buff_head frame_list;
	u8 peer_addr[ETH_ALEN];
	bool active;
};

struct iwl_mei_scan_filter {
	bool is_mei_limited_scan;
	struct sk_buff_head scan_res;
	struct work_struct scan_work;
};

/**
 * struct iwl_mvm_acs_survey_channel - per-channel survey information
 *
 * Stripped down version of &struct survey_info.
 *
 * @time: time in ms the radio was on the channel
 * @time_busy: time in ms the channel was sensed busy
 * @time_tx: time in ms spent transmitting data
 * @time_rx: time in ms spent receiving data
 * @noise: channel noise in dBm
 */
struct iwl_mvm_acs_survey_channel {
	u32 time;
	u32 time_busy;
	u32 time_tx;
	u32 time_rx;
	s8 noise;
};

struct iwl_mvm_acs_survey {
	struct iwl_mvm_acs_survey_channel *bands[NUM_NL80211_BANDS];

	/* Overall number of channels */
	int n_channels;

	/* Storage space for per-channel information follows */
	struct iwl_mvm_acs_survey_channel channels[] __counted_by(n_channels);
};

struct iwl_mvm {
	/* for logger access */
	struct device *dev;

	struct iwl_trans *trans;
	const struct iwl_fw *fw;
	const struct iwl_rf_cfg *cfg;
	struct iwl_phy_db *phy_db;
	struct ieee80211_hw *hw;

	/* for protecting access to iwl_mvm */
	struct mutex mutex;
	struct list_head async_handlers_list;
	spinlock_t async_handlers_lock;
	struct work_struct async_handlers_wk;

	/* For async rx handlers that require the wiphy lock */
	struct wiphy_work async_handlers_wiphy_wk;

	struct wiphy_work trig_link_selection_wk;

	struct work_struct roc_done_wk;

	unsigned long init_status;

	unsigned long status;

	u32 queue_sync_cookie;
	unsigned long queue_sync_state;
	/*
	 * for beacon filtering -
	 * currently only one interface can be supported
	 */
	struct iwl_mvm_vif *bf_allowed_vif;

	bool hw_registered;
	bool rfkill_safe_init_done;

	u8 cca_40mhz_workaround;

	u8 fw_rates_ver;

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
	spinlock_t add_stream_lock;

	const char *nvm_file_name;
	struct iwl_nvm_data *nvm_data;
	struct iwl_mei_nvm *mei_nvm_data;
	struct iwl_mvm_csme_conn_info __rcu *csme_conn_info;
	bool mei_rfkill_blocked;
	bool mei_registered;
	struct work_struct sap_connected_wk;

	/*
	 * NVM built based on the SAP data but that we can't free even after
	 * we get ownership because it contains the cfg80211's channel.
	 */
	struct iwl_nvm_data *temp_nvm_data;

	/* NVM sections */
	struct iwl_nvm_section nvm_sections[NVM_MAX_NUM_SECTIONS];

	struct iwl_fw_runtime fwrt;

	/* EEPROM MAC addresses */
	struct mac_address addresses[IWL_MVM_MAX_ADDRESSES];

	/* data related to data path */
	struct iwl_rx_phy_info last_phy_info;
	struct ieee80211_sta __rcu *fw_id_to_mac_id[IWL_STATION_COUNT_MAX];
	/* note: fw_id_to_link_sta must be protected by wiphy and mvm mutexes */
	struct ieee80211_link_sta __rcu *fw_id_to_link_sta[IWL_STATION_COUNT_MAX];
	u8 rx_ba_sessions;

	/* configured by mac80211 */
	u32 rts_threshold;

	/* Scan status, cmd (pre-allocated) and auxiliary station */
	unsigned int scan_status;
	size_t scan_cmd_size;
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
	u32 scan_uid_status[IWL_MAX_UMAC_SCANS];

	/* start time of last scan in TSF of the mac that requested the scan */
	u64 scan_start;

	/* the vif that requested the current scan */
	struct iwl_mvm_vif *scan_vif;
	u8 scan_link_id;

	/* rx chain antennas set through debugfs for the scan command */
	u8 scan_rx_ant;

	/* Internal station */
	struct iwl_mvm_int_sta aux_sta;
	struct iwl_mvm_int_sta snif_sta;

	bool last_ebs_successful;

	u8 scan_last_antenna_idx; /* to toggle TX between antennas */
	u8 mgmt_last_antenna_idx;

	u8 set_tx_ant;
	u8 set_rx_ant;

	/* last smart fifo state that was successfully sent to firmware */
	enum iwl_sf_state sf_state;

	/*
	 * Leave this pointer outside the ifdef below so that it can be
	 * assigned without ifdef in the source code.
	 */
	struct dentry *debugfs_dir;
#ifdef CONFIG_IWLWIFI_DEBUGFS
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

	struct ieee80211_vif __rcu *vif_id_to_mac[NUM_MAC_INDEX_DRIVER];

	struct ieee80211_bss_conf __rcu *link_id_to_link_conf[IWL_FW_MAX_LINK_ID + 1];

	u8 *error_recovery_buf;

#ifdef CONFIG_IWLWIFI_LEDS
	struct led_classdev led;
#endif

	struct ieee80211_vif *p2p_device_vif;

#ifdef CONFIG_PM_SLEEP
	struct wiphy_wowlan_support wowlan;
	int gtk_ivlen, gtk_icvlen, ptk_ivlen, ptk_icvlen;

	/* sched scan settings for net detect */
	struct ieee80211_scan_ies nd_ies;
	struct cfg80211_match_set *nd_match_sets;
	int n_nd_match_sets;
	struct ieee80211_channel **nd_channels;
	int n_nd_channels;
	bool net_detect;
	bool fast_resume;
	u8 offload_tid;
#ifdef CONFIG_IWLWIFI_DEBUGFS
	bool d3_wake_sysassert;
	bool d3_test_active;
	u32 d3_test_pme_ptr;
	struct ieee80211_vif *keep_vif;
	u32 last_netdetect_scans; /* no. of scans in the last net-detect wake */
#endif
#endif

	wait_queue_head_t rx_sync_waitq;

	/* BT-Coex - only one of those will be used */
	union {
		struct iwl_bt_coex_prof_old_notif last_bt_notif;
		struct iwl_bt_coex_profile_notif last_bt_wifi_loss;
	};
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

	bool fw_static_smps_request;

	unsigned long bt_coex_last_tcm_ts;
	struct iwl_mvm_tcm tcm;

	u8 uapsd_noagg_bssid_write_idx;
	struct mac_address uapsd_noagg_bssids[IWL_MVM_UAPSD_NOAGG_BSSIDS_NUM]
		__aligned(2);

	struct iwl_time_quota_cmd last_quota_cmd;

	/* Tx queues */
	u16 aux_queue;
	u16 snif_queue;
	u16 probe_queue;
	u16 p2p_dev_queue;

	/* Indicate if device power save is allowed */
	u8 ps_disabled; /* u8 instead of bool to ease debugfs_create_* usage */
	/* Indicate if 32Khz external clock is valid */
	u32 ext_clock_valid;

	/* This vif used by CSME to send / receive traffic */
	struct ieee80211_vif *csme_vif;
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

	struct cfg80211_ftm_responder_stats ftm_resp_stats;
	struct {
		struct cfg80211_pmsr_request *req;
		struct wireless_dev *req_wdev;
		struct list_head loc_list;
		int responses[IWL_TOF_MAX_APS];
		struct {
			struct list_head resp;
		} smooth;
		struct list_head pasn_list;
	} ftm_initiator;

	struct list_head resp_pasn_list;

	struct ptp_data ptp_data;

	struct {
		u8 range_resp;
	} cmd_ver;

	struct ieee80211_vif *nan_vif;
	struct iwl_mvm_baid_data __rcu *baid_map[IWL_MAX_BAID];

	/*
	 * Drop beacons from other APs in AP mode when there are no connected
	 * clients.
	 */
	bool drop_bcn_ap_mode;

	struct delayed_work cs_tx_unblock_dwork;

	/* does a monitor vif exist (only one can exist hence bool) */
	bool monitor_on;
	/*
	 * primary channel position relative to he whole bandwidth,
	 * in steps of 80 MHz
	 */
	u8 monitor_p80;

	/* sniffer data to include in radiotap */
	__le16 cur_aid;
	u8 cur_bssid[ETH_ALEN];

	/* report rx timestamp in ptp clock time */
	bool rx_ts_ptp;

	unsigned long last_6ghz_passive_scan_jiffies;
	unsigned long last_reset_or_resume_time_jiffies;

	bool sta_remove_requires_queue_remove;
	bool mld_api_is_used;

	/*
	 * Indicates that firmware will do a product reset (and then
	 * therefore fail to load) when we start it (due to OTP burn),
	 * if so don't dump errors etc. since this is expected.
	 */
	bool fw_product_reset;

	struct iwl_time_sync_data time_sync;

	struct iwl_mei_scan_filter mei_scan_filter;

	struct iwl_mvm_acs_survey *acs_survey;

	bool statistics_clear;
};

/* Extract MVM priv from op_mode and _hw */
#define IWL_OP_MODE_GET_MVM(_iwl_op_mode)		\
	((struct iwl_mvm *)(_iwl_op_mode)->op_mode_specific)

#define IWL_MAC80211_GET_MVM(_hw)			\
	IWL_OP_MODE_GET_MVM((struct iwl_op_mode *)((_hw)->priv))

DEFINE_GUARD(mvm, struct iwl_mvm *, mutex_lock(&_T->mutex), mutex_unlock(&_T->mutex))

/**
 * enum iwl_mvm_status - MVM status bits
 * @IWL_MVM_STATUS_HW_RFKILL: HW RF-kill is asserted
 * @IWL_MVM_STATUS_HW_CTKILL: CT-kill is active
 * @IWL_MVM_STATUS_ROC_P2P_RUNNING: remain-on-channel on P2P is running (when
 *	P2P is not over AUX)
 * @IWL_MVM_STATUS_HW_RESTART_REQUESTED: HW restart was requested
 * @IWL_MVM_STATUS_IN_HW_RESTART: HW restart is active
 * @IWL_MVM_STATUS_ROC_AUX_RUNNING: AUX remain-on-channel is running
 * @IWL_MVM_STATUS_FIRMWARE_RUNNING: firmware is running
 * @IWL_MVM_STATUS_IN_D3: in D3 (or at least about to go into it)
 * @IWL_MVM_STATUS_SUPPRESS_ERROR_LOG_ONCE: suppress one error log
 *	if this is set, when intentionally triggered
 */
enum iwl_mvm_status {
	IWL_MVM_STATUS_HW_RFKILL,
	IWL_MVM_STATUS_HW_CTKILL,
	IWL_MVM_STATUS_ROC_P2P_RUNNING,
	IWL_MVM_STATUS_HW_RESTART_REQUESTED,
	IWL_MVM_STATUS_IN_HW_RESTART,
	IWL_MVM_STATUS_ROC_AUX_RUNNING,
	IWL_MVM_STATUS_FIRMWARE_RUNNING,
	IWL_MVM_STATUS_IN_D3,
	IWL_MVM_STATUS_SUPPRESS_ERROR_LOG_ONCE,
};

struct iwl_mvm_csme_conn_info {
	struct rcu_head rcu_head;
	struct iwl_mei_conn_info conn_info;
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

	if (sta_id >= mvm->fw->ucode_capa.num_stations)
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

	if (sta_id >= mvm->fw->ucode_capa.num_stations)
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

static inline struct ieee80211_bss_conf *
iwl_mvm_rcu_fw_link_id_to_link_conf(struct iwl_mvm *mvm, u8 link_id, bool rcu)
{
	if (IWL_FW_CHECK(mvm, link_id >= ARRAY_SIZE(mvm->link_id_to_link_conf),
			 "erroneous FW link ID: %d\n", link_id))
		return NULL;

	if (rcu)
		return rcu_dereference(mvm->link_id_to_link_conf[link_id]);

	return rcu_dereference_protected(mvm->link_id_to_link_conf[link_id],
					 lockdep_is_held(&mvm->mutex));
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

static inline bool iwl_mvm_is_adwell_hb_ap_num_supported(struct iwl_mvm *mvm)
{
	return fw_has_api(&mvm->fw->ucode_capa,
			  IWL_UCODE_TLV_API_ADWELL_HB_DEF_N_AP);
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

	/*
	 * Enable LAR only if it is supported by the FW (TLV) &&
	 * enabled in the NVM
	 */
	if (mvm->trans->cfg->nvm_type == IWL_NVM_EXT)
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
			   IWL_UCODE_TLV_CAPA_CSUM_SUPPORT);
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

static inline bool iwl_mvm_has_mld_api(const struct iwl_fw *fw)
{
	return fw_has_capa(&fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_MLD_API_SUPPORT);
}

static inline bool iwl_mvm_has_new_station_api(const struct iwl_fw *fw)
{
	return iwl_mvm_has_mld_api(fw) ||
	       iwl_fw_lookup_cmd_ver(fw, ADD_STA, 0) >= 12;
}

static inline bool iwl_mvm_has_new_tx_api(struct iwl_mvm *mvm)
{
	/* TODO - replace with TLV once defined */
	return mvm->trans->mac_cfg->gen2;
}

static inline bool iwl_mvm_has_unified_ucode(struct iwl_mvm *mvm)
{
	/* TODO - better define this */
	return mvm->trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_22000;
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
	return mvm->trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_22000;
}

static inline bool iwl_mvm_is_scan_ext_chan_supported(struct iwl_mvm *mvm)
{
	return fw_has_api(&mvm->fw->ucode_capa,
			  IWL_UCODE_TLV_API_SCAN_EXT_CHAN_VER);
}


static inline bool iwl_mvm_is_reduced_config_scan_supported(struct iwl_mvm *mvm)
{
	return fw_has_api(&mvm->fw->ucode_capa,
			  IWL_UCODE_TLV_API_REDUCED_SCAN_CONFIG);
}

static inline bool iwl_mvm_is_band_in_rx_supported(struct iwl_mvm *mvm)
{
	return fw_has_api(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_API_BAND_IN_RX_DATA);
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

static inline bool iwl_mvm_has_no_host_disable_tx(struct iwl_mvm *mvm)
{
	return fw_has_api(&mvm->fw->ucode_capa,
			  IWL_UCODE_TLV_API_NO_HOST_DISABLE_TX);
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
		return &((struct iwl_tx_resp *)tx_resp)->status;
	else
		return ((struct iwl_tx_resp_v3 *)tx_resp)->status;
}

static inline bool iwl_mvm_is_tt_in_fw(struct iwl_mvm *mvm)
{
	/* these two TLV are redundant since the responsibility to CT-kill by
	 * FW happens only after we send at least one command of
	 * temperature THs report.
	 */
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_CT_KILL_BY_FW) &&
	       fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_TEMP_THS_REPORT_SUPPORT);
}

static inline bool iwl_mvm_is_ctdp_supported(struct iwl_mvm *mvm)
{
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_CTDP_SUPPORT);
}

static inline bool iwl_mvm_is_esr_supported(struct iwl_trans *trans)
{
	if (CSR_HW_RFID_IS_CDB(trans->info.hw_rf_id))
		return false;

	switch (CSR_HW_RFID_TYPE(trans->info.hw_rf_id)) {
	case IWL_CFG_RF_TYPE_FM:
		/* Step A doesn't support eSR */
		return CSR_HW_RFID_STEP(trans->info.hw_rf_id);
	case IWL_CFG_RF_TYPE_WH:
	case IWL_CFG_RF_TYPE_PE:
		return true;
	default:
		return false;
	}
}

static inline int iwl_mvm_max_active_links(struct iwl_mvm *mvm,
					   struct ieee80211_vif *vif)
{
	struct iwl_trans *trans = mvm->fwrt.trans;

	if (vif->type == NL80211_IFTYPE_AP)
		return mvm->fw->ucode_capa.num_beacons;

	/* Check if HW supports eSR or STR */
	if (iwl_mvm_is_esr_supported(trans) ||
	    (CSR_HW_RFID_TYPE(trans->info.hw_rf_id) == IWL_CFG_RF_TYPE_FM &&
	     CSR_HW_RFID_IS_CDB(trans->info.hw_rf_id)))
		return IWL_FW_MAX_ACTIVE_LINKS_NUM;

	return 1;
}

extern const u8 iwl_mvm_ac_to_tx_fifo[];
extern const u8 iwl_mvm_ac_to_gen2_tx_fifo[];
extern const u8 iwl_mvm_ac_to_bz_tx_fifo[];

static inline u8 iwl_mvm_mac_ac_to_tx_fifo(struct iwl_mvm *mvm,
					   enum ieee80211_ac_numbers ac)
{
	if (mvm->trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_BZ)
		return iwl_mvm_ac_to_bz_tx_fifo[ac];
	if (iwl_mvm_has_new_tx_api(mvm))
		return iwl_mvm_ac_to_gen2_tx_fifo[ac];
	return iwl_mvm_ac_to_tx_fifo[ac];
}

static inline bool iwl_mvm_has_rlc_offload(struct iwl_mvm *mvm)
{
	return iwl_fw_lookup_cmd_ver(mvm->fw,
				     WIDE_ID(DATA_PATH_GROUP, RLC_CONFIG_CMD),
				     0) >= 3;
}

struct iwl_rate_info {
	u8 plcp;	/* uCode API:  IWL_RATE_6M_PLCP, etc. */
	u8 plcp_siso;	/* uCode API:  IWL_RATE_SISO_6M_PLCP, etc. */
	u8 plcp_mimo2;	/* uCode API:  IWL_RATE_MIMO2_6M_PLCP, etc. */
	u8 plcp_mimo3;  /* uCode API:  IWL_RATE_MIMO3_6M_PLCP, etc. */
	u8 ieee;	/* MAC header:  IWL_RATE_6M_IEEE, etc. */
};

void __iwl_mvm_mac_stop(struct iwl_mvm *mvm, bool suspend);
int __iwl_mvm_mac_start(struct iwl_mvm *mvm);

/******************
 * MVM Methods
 ******************/
/* uCode */
int iwl_run_init_mvm_ucode(struct iwl_mvm *mvm);

/* Utils */
int iwl_mvm_legacy_hw_idx_to_mac80211_idx(u32 rate_n_flags,
					  enum nl80211_band band);
int iwl_mvm_legacy_rate_to_mac80211_idx(u32 rate_n_flags,
					enum nl80211_band band);
void iwl_mvm_hwrate_to_tx_rate(u32 rate_n_flags,
			       enum nl80211_band band,
			       struct ieee80211_tx_rate *r);
u8 iwl_mvm_mac80211_idx_to_hwrate(const struct iwl_fw *fw, int rate_idx);
u8 iwl_mvm_mac80211_ac_to_ucode_ac(enum ieee80211_ac_numbers ac);
bool iwl_mvm_is_nic_ack_enabled(struct iwl_mvm *mvm, struct ieee80211_vif *vif);

static inline void iwl_mvm_dump_nic_error_log(struct iwl_mvm *mvm)
{
	iwl_fwrt_dump_error_logs(&mvm->fwrt);
}

u8 first_antenna(u8 mask);
u8 iwl_mvm_next_antenna(struct iwl_mvm *mvm, u8 valid, u8 last_idx);
void iwl_mvm_get_sync_time(struct iwl_mvm *mvm, int clock_type, u32 *gp2,
			   u64 *boottime, ktime_t *realtime);
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
int iwl_mvm_tx_skb_sta(struct iwl_mvm *mvm, struct sk_buff *skb,
		       struct ieee80211_sta *sta);
int iwl_mvm_tx_skb_non_sta(struct iwl_mvm *mvm, struct sk_buff *skb);
void iwl_mvm_set_tx_cmd(struct iwl_mvm *mvm, struct sk_buff *skb,
			struct iwl_tx_cmd_v6 *tx_cmd,
			struct ieee80211_tx_info *info, u8 sta_id);
void iwl_mvm_set_tx_cmd_rate(struct iwl_mvm *mvm, struct iwl_tx_cmd_v6 *tx_cmd,
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
int iwl_mvm_flush_tx_path(struct iwl_mvm *mvm, u32 tfd_msk);
int iwl_mvm_flush_sta(struct iwl_mvm *mvm, u32 sta_id, u32 tfd_queue_mask);
int iwl_mvm_flush_sta_tids(struct iwl_mvm *mvm, u32 sta_id, u16 tids);

/* Utils to extract sta related data */
__le32 iwl_mvm_get_sta_htc_flags(struct ieee80211_sta *sta,
				 struct ieee80211_link_sta *link_sta);
u8 iwl_mvm_get_sta_uapsd_acs(struct ieee80211_sta *sta);
u32 iwl_mvm_get_sta_ampdu_dens(struct ieee80211_link_sta *link_sta,
			       struct ieee80211_bss_conf *link_conf,
			       u32 *_agg_size);
int iwl_mvm_set_sta_pkt_ext(struct iwl_mvm *mvm,
			    struct ieee80211_link_sta *link_sta,
			    struct iwl_he_pkt_ext_v2 *pkt_ext);

void iwl_mvm_async_handlers_purge(struct iwl_mvm *mvm);

static inline void iwl_mvm_set_tx_cmd_ccmp(struct ieee80211_tx_info *info,
					   struct iwl_tx_cmd_v6 *tx_cmd)
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
void iwl_mvm_handle_rx_system_oper_stats(struct iwl_mvm *mvm,
					 struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_handle_rx_system_oper_part1_stats(struct iwl_mvm *mvm,
					       struct iwl_rx_cmd_buffer *rxb);
static inline void
iwl_mvm_handle_rx_system_end_stats_notif(struct iwl_mvm *mvm,
					 struct iwl_rx_cmd_buffer *rxb)
{
}

void iwl_mvm_handle_rx_statistics(struct iwl_mvm *mvm,
				  struct iwl_rx_packet *pkt);
void iwl_mvm_rx_statistics(struct iwl_mvm *mvm,
			   struct iwl_rx_cmd_buffer *rxb);
int iwl_mvm_request_statistics(struct iwl_mvm *mvm, bool clear);
int iwl_mvm_request_periodic_system_statistics(struct iwl_mvm *mvm,
					       bool enable);
void iwl_mvm_accu_radio_stats(struct iwl_mvm *mvm);

/* NVM */
int iwl_nvm_init(struct iwl_mvm *mvm);
int iwl_mvm_load_nvm_to_nic(struct iwl_mvm *mvm);

static inline u8 iwl_mvm_get_valid_tx_ant(struct iwl_mvm *mvm)
{
	u8 tx_ant = mvm->fw->valid_tx_ant;

	if (mvm->nvm_data && mvm->nvm_data->valid_tx_ant)
		tx_ant &= mvm->nvm_data->valid_tx_ant;

	if (mvm->set_tx_ant)
		tx_ant &= mvm->set_tx_ant;

	return tx_ant;
}

static inline u8 iwl_mvm_get_valid_rx_ant(struct iwl_mvm *mvm)
{
	u8 rx_ant = mvm->fw->valid_rx_ant;

	if (mvm->nvm_data && mvm->nvm_data->valid_rx_ant)
		rx_ant &= mvm->nvm_data->valid_rx_ant;

	if (mvm->set_rx_ant)
		rx_ant &= mvm->set_rx_ant;

	return rx_ant;

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

void iwl_mvm_mac_init_mvmvif(struct iwl_mvm *mvm, struct iwl_mvm_vif *mvmvif);

/*
 * FW notifications / CMD responses handlers
 * Convention: iwl_mvm_rx_<NAME OF THE CMD>
 */
void iwl_mvm_rx_mq(struct iwl_op_mode *op_mode,
		   struct napi_struct *napi,
		   struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_rx_phy_cmd(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_rx_mpdu(struct iwl_mvm *mvm, struct napi_struct *napi,
			struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_mpdu_mq(struct iwl_mvm *mvm, struct napi_struct *napi,
			struct iwl_rx_cmd_buffer *rxb, int queue);
void iwl_mvm_rx_monitor_no_data(struct iwl_mvm *mvm, struct napi_struct *napi,
				struct iwl_rx_cmd_buffer *rxb, int queue);
void iwl_mvm_rx_frame_release(struct iwl_mvm *mvm, struct napi_struct *napi,
			      struct iwl_rx_cmd_buffer *rxb, int queue);
void iwl_mvm_rx_bar_frame_release(struct iwl_mvm *mvm, struct napi_struct *napi,
				  struct iwl_rx_cmd_buffer *rxb, int queue);
void iwl_mvm_rx_queue_notif(struct iwl_mvm *mvm, struct napi_struct *napi,
			    struct iwl_rx_cmd_buffer *rxb, int queue);
void iwl_mvm_rx_tx_cmd(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_mfu_assert_dump_notif(struct iwl_mvm *mvm,
				   struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_send_recovery_cmd(struct iwl_mvm *mvm, u32 flags);
void iwl_mvm_rx_ba_notif(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_ant_coupling_notif(struct iwl_mvm *mvm,
				   struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_fw_error(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_mfuart_notif(struct iwl_mvm *mvm,
			     struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_shared_mem_cfg_notif(struct iwl_mvm *mvm,
				     struct iwl_rx_cmd_buffer *rxb);

/* MVM PHY */
struct iwl_mvm_phy_ctxt *iwl_mvm_get_free_phy_ctxt(struct iwl_mvm *mvm);
int iwl_mvm_phy_ctxt_add(struct iwl_mvm *mvm, struct iwl_mvm_phy_ctxt *ctxt,
			 const struct cfg80211_chan_def *chandef,
			 const struct cfg80211_chan_def *ap,
			 u8 chains_static, u8 chains_dynamic);
int iwl_mvm_phy_ctxt_changed(struct iwl_mvm *mvm, struct iwl_mvm_phy_ctxt *ctxt,
			     const struct cfg80211_chan_def *chandef,
			     const struct cfg80211_chan_def *ap,
			     u8 chains_static, u8 chains_dynamic);
void iwl_mvm_phy_ctxt_ref(struct iwl_mvm *mvm,
			  struct iwl_mvm_phy_ctxt *ctxt);
void iwl_mvm_phy_ctxt_unref(struct iwl_mvm *mvm,
			    struct iwl_mvm_phy_ctxt *ctxt);
int iwl_mvm_phy_ctx_count(struct iwl_mvm *mvm);
u8 iwl_mvm_get_channel_width(const struct cfg80211_chan_def *chandef);
u8 iwl_mvm_get_ctrl_pos(const struct cfg80211_chan_def *chandef);
int iwl_mvm_phy_send_rlc(struct iwl_mvm *mvm, struct iwl_mvm_phy_ctxt *ctxt,
			 u8 chains_static, u8 chains_dynamic);

/* MAC (virtual interface) programming */

void iwl_mvm_prepare_mac_removal(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif);
void iwl_mvm_set_fw_basic_rates(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				struct iwl_mvm_vif_link_info *link_info,
				__le32 *cck_rates, __le32 *ofdm_rates);
void iwl_mvm_set_fw_protection_flags(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *link_conf,
				     __le32 *protection_flags, u32 ht_flag,
				     u32 tgg_flag);
void iwl_mvm_set_fw_qos_params(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			       struct ieee80211_bss_conf *link_conf,
			       struct iwl_ac_qos *ac, __le32 *qos_flags);
bool iwl_mvm_set_fw_mu_edca_params(struct iwl_mvm *mvm,
				   const struct iwl_mvm_vif_link_info *link_info,
				   struct iwl_he_backoff_conf *trig_based_txf);
void iwl_mvm_set_fw_dtim_tbtt(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *link_conf,
			      __le64 *dtim_tsf, __le32 *dtim_time,
			      __le32 *assoc_beacon_arrive_time);
__le32 iwl_mac_ctxt_p2p_dev_has_extended_disc(struct iwl_mvm *mvm,
					      struct ieee80211_vif *vif);
void iwl_mvm_mac_ctxt_cmd_ap_set_filter_flags(struct iwl_mvm *mvm,
					      struct iwl_mvm_vif *mvmvif,
					      __le32 *filter_flags,
					      int accept_probe_req_flag,
					      int accept_beacon_flag);
int iwl_mvm_get_mac_type(struct ieee80211_vif *vif);
__le32 iwl_mvm_mac_ctxt_cmd_p2p_sta_get_oppps_ctwin(struct iwl_mvm *mvm,
						    struct ieee80211_vif *vif);
u32 iwl_mvm_mac_ctxt_cmd_sta_get_twt_policy(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif);
int iwl_mvm_mld_mac_ctxt_add(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_mld_mac_ctxt_changed(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				 bool force_assoc_off);
int iwl_mvm_mld_mac_ctxt_remove(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_mac_ctxt_init(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_mac_ctxt_add(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_mac_ctxt_changed(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     bool force_assoc_off, const u8 *bssid_override);
int iwl_mvm_mac_ctxt_remove(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_mac_ctxt_beacon_changed(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *link_conf);
int iwl_mvm_mac_ctxt_send_beacon_cmd(struct iwl_mvm *mvm,
				     struct sk_buff *beacon,
				     void *data, int len);
u8 iwl_mvm_mac_ctxt_get_beacon_rate(struct iwl_mvm *mvm,
				    struct ieee80211_tx_info *info,
				    struct ieee80211_vif *vif);
u8 iwl_mvm_mac_ctxt_get_lowest_rate(struct iwl_mvm *mvm,
				    struct ieee80211_tx_info *info,
				    struct ieee80211_vif *vif);
u16 iwl_mvm_mac_ctxt_get_beacon_flags(const struct iwl_fw *fw,
				      u8 rate_idx);
void iwl_mvm_mac_ctxt_set_tim(struct iwl_mvm *mvm,
			      __le32 *tim_index, __le32 *tim_size,
			      u8 *beacon, u32 frame_size);
void iwl_mvm_rx_beacon_notif(struct iwl_mvm *mvm,
			     struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_missed_beacons_notif(struct iwl_mvm *mvm,
				     struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_rx_missed_beacons_notif_legacy(struct iwl_mvm *mvm,
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
void iwl_mvm_channel_switch_start_notif(struct iwl_mvm *mvm,
					struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_channel_switch_error_notif(struct iwl_mvm *mvm,
					struct iwl_rx_cmd_buffer *rxb);
/* Bindings */
int iwl_mvm_binding_add_vif(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
int iwl_mvm_binding_remove_vif(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
u32 iwl_mvm_get_lmac_id(struct iwl_mvm *mvm, enum nl80211_band band);

/* Links */
void iwl_mvm_init_link(struct iwl_mvm_vif_link_info *link);
int iwl_mvm_set_link_mapping(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *link_conf);
int iwl_mvm_add_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		     struct ieee80211_bss_conf *link_conf);
int iwl_mvm_link_changed(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			 struct ieee80211_bss_conf *link_conf,
			 u32 changes, bool active);
int iwl_mvm_unset_link_mapping(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			       struct ieee80211_bss_conf *link_conf);
int iwl_mvm_remove_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *link_conf);
int iwl_mvm_disable_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			 struct ieee80211_bss_conf *link_conf);

void iwl_mvm_select_links(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
u8 iwl_mvm_get_primary_link(struct ieee80211_vif *vif);
u8 iwl_mvm_get_other_link(struct ieee80211_vif *vif, u8 link_id);

struct iwl_mvm_link_sel_data {
	u8 link_id;
	const struct cfg80211_chan_def *chandef;
	s32 signal;
	u16 grade;
};

#if IS_ENABLED(CONFIG_IWLWIFI_KUNIT_TESTS)
unsigned int iwl_mvm_get_link_grade(struct ieee80211_bss_conf *link_conf);
bool iwl_mvm_mld_valid_link_pair(struct ieee80211_vif *vif,
				 const struct iwl_mvm_link_sel_data *a,
				 const struct iwl_mvm_link_sel_data *b);

s8 iwl_mvm_average_dbm_values(const struct iwl_umac_scan_channel_survey_notif *notif);

extern const struct iwl_hcmd_arr iwl_mvm_groups[];
extern const unsigned int iwl_mvm_groups_size;
#endif

/* AP and IBSS */
bool iwl_mvm_start_ap_ibss_common(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif, int *ret);
void iwl_mvm_stop_ap_ibss_common(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif);

/* BSS Info */
void iwl_mvm_bss_info_changed_station_common(struct iwl_mvm *mvm,
					     struct ieee80211_vif *vif,
					     struct ieee80211_bss_conf *link_conf,
					     u64 changes);
void iwl_mvm_bss_info_changed_station_assoc(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif,
					    u64 changes);

/* ROC */
/**
 * struct iwl_mvm_roc_ops - callbacks for the remain_on_channel()
 *
 * Since the only difference between both MLD and
 * non-MLD versions of remain_on_channel() is these function calls,
 * each version will send its specific function calls to
 * %iwl_mvm_roc_common().
 *
 * @add_aux_sta_for_hs20: pointer to the function that adds an aux sta
 *	for Hot Spot 2.0
 * @link: For a P2P Device interface, pointer to a function that links the
 *      MAC/Link to the PHY context
 */
struct iwl_mvm_roc_ops {
	int (*add_aux_sta_for_hs20)(struct iwl_mvm *mvm, u32 lmac_id);
	int (*link)(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
};

int iwl_mvm_roc_common(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       struct ieee80211_channel *channel, int duration,
		       enum ieee80211_roc_type type,
		       const struct iwl_mvm_roc_ops *ops);
int iwl_mvm_cancel_roc(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif);
/*Session Protection */
void iwl_mvm_protect_assoc(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			   u32 duration_override, unsigned int link_id);

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
size_t iwl_mvm_scan_size(struct iwl_mvm *mvm);
int iwl_mvm_scan_stop(struct iwl_mvm *mvm, int type, bool notify);

int iwl_mvm_max_scan_ie_len(struct iwl_mvm *mvm);
void iwl_mvm_report_scan_aborted(struct iwl_mvm *mvm);
void iwl_mvm_scan_timeout_wk(struct work_struct *work);
int iwl_mvm_int_mlo_scan(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
void iwl_mvm_rx_channel_survey_notif(struct iwl_mvm *mvm,
				     struct iwl_rx_cmd_buffer *rxb);

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
void iwl_mvm_dbgfs_register(struct iwl_mvm *mvm);
void iwl_mvm_vif_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void iwl_mvm_vif_dbgfs_add_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
void iwl_mvm_vif_dbgfs_rm_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
#else
static inline void iwl_mvm_dbgfs_register(struct iwl_mvm *mvm)
{
}
static inline void
iwl_mvm_vif_dbgfs_add_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
}
static inline void
iwl_mvm_vif_dbgfs_rm_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
}
#endif /* CONFIG_IWLWIFI_DEBUGFS */

/* rate scaling */
int iwl_mvm_send_lq_cmd(struct iwl_mvm *mvm, struct iwl_lq_cmd *lq);
void iwl_mvm_update_frame_stats(struct iwl_mvm *mvm, u32 rate, bool agg);
int rs_pretty_print_rate_v1(char *buf, int bufsz, const u32 rate);
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
#ifdef CONFIG_PM_SLEEP
void iwl_mvm_set_last_nonqos_seq(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif);
void iwl_mvm_fast_suspend(struct iwl_mvm *mvm);
int iwl_mvm_fast_resume(struct iwl_mvm *mvm);
#else
static inline void
iwl_mvm_set_last_nonqos_seq(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
}

static inline void iwl_mvm_fast_suspend(struct iwl_mvm *mvm)
{
}

static inline int iwl_mvm_fast_resume(struct iwl_mvm *mvm)
{
	return 0;
}
#endif
void iwl_mvm_set_wowlan_qos_seq(struct iwl_mvm_sta *mvm_ap_sta,
				struct iwl_wowlan_config_cmd_v6 *cmd);
int iwl_mvm_send_proto_offload(struct iwl_mvm *mvm,
			       struct ieee80211_vif *vif,
			       bool disable_offloading,
			       bool offload_ns,
			       u32 cmd_flags,
			       u8 sta_id);

/* BT Coex */
int iwl_mvm_send_bt_init_conf(struct iwl_mvm *mvm);
void iwl_mvm_rx_bt_coex_old_notif(struct iwl_mvm *mvm,
				  struct iwl_rx_cmd_buffer *rxb);
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
int iwl_mvm_enable_beacon_filter(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif);
int iwl_mvm_disable_beacon_filter(struct iwl_mvm *mvm,
				  struct ieee80211_vif *vif);
/* SMPS */
void iwl_mvm_update_smps(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				enum iwl_mvm_smps_type_request req_type,
				enum ieee80211_smps_mode smps_request,
				unsigned int link_id);
void
iwl_mvm_update_smps_on_active_links(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    enum iwl_mvm_smps_type_request req_type,
				    enum ieee80211_smps_mode smps_request);
bool iwl_mvm_rx_diversity_allowed(struct iwl_mvm *mvm,
				  struct iwl_mvm_phy_ctxt *ctxt);
void iwl_mvm_update_link_smps(struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *link_conf);

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
	return ((BIT(mvm->trans->mac_cfg->base->num_of_queues) - 1) &
		~BIT(IWL_MVM_DQA_CMD_QUEUE));
}

void iwl_mvm_stop_device(struct iwl_mvm *mvm);

/* Thermal management and CT-kill */
void iwl_mvm_tt_tx_backoff(struct iwl_mvm *mvm, u32 backoff);
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

#if IS_ENABLED(CONFIG_IWLMEI)

/* vendor commands */
void iwl_mvm_vendor_cmds_register(struct iwl_mvm *mvm);

#else

static inline void iwl_mvm_vendor_cmds_register(struct iwl_mvm *mvm) {}

#endif

/* Location Aware Regulatory */
struct iwl_mcc_update_resp_v8 *
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
int iwl_mvm_init_fw_regd(struct iwl_mvm *mvm, bool force_regd_sync);
void iwl_mvm_update_changed_regdom(struct iwl_mvm *mvm);

/* smart fifo */
int iwl_mvm_sf_update(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		      bool added_vif);

/* FTM responder */
int iwl_mvm_ftm_start_responder(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *bss_conf);
void iwl_mvm_ftm_restart_responder(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *bss_conf);
void iwl_mvm_ftm_responder_stats(struct iwl_mvm *mvm,
				 struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_ftm_responder_clear(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif);

/* FTM initiator */
void iwl_mvm_ftm_restart(struct iwl_mvm *mvm);
void iwl_mvm_ftm_range_resp(struct iwl_mvm *mvm,
			    struct iwl_rx_cmd_buffer *rxb);
void iwl_mvm_ftm_lc_notif(struct iwl_mvm *mvm,
			  struct iwl_rx_cmd_buffer *rxb);
int iwl_mvm_ftm_start(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		      struct cfg80211_pmsr_request *request);
void iwl_mvm_ftm_abort(struct iwl_mvm *mvm, struct cfg80211_pmsr_request *req);
void iwl_mvm_ftm_initiator_smooth_config(struct iwl_mvm *mvm);
void iwl_mvm_ftm_initiator_smooth_stop(struct iwl_mvm *mvm);

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
					   struct ieee80211_vif *vif,
					   unsigned int link_id);
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
				     enum iwl_mvm_rxq_notif_type type,
				     bool sync,
				     const void *data, u32 size);
struct ieee80211_vif *iwl_mvm_get_bss_vif(struct iwl_mvm *mvm);
struct ieee80211_vif *iwl_mvm_get_vif_by_macid(struct iwl_mvm *mvm, u32 macid);
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

unsigned int iwl_mvm_get_wd_timeout(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif);
void iwl_mvm_connection_loss(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     const char *errmsg);
void iwl_mvm_event_frame_timeout_callback(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  const struct ieee80211_sta *sta,
					  u16 tid);
void iwl_mvm_mei_scan_filter_init(struct iwl_mei_scan_filter *mei_scan_filter);

void iwl_mvm_ptp_init(struct iwl_mvm *mvm);
void iwl_mvm_ptp_remove(struct iwl_mvm *mvm);
u64 iwl_mvm_ptp_get_adj_time(struct iwl_mvm *mvm, u64 base_time);
int iwl_mvm_sar_select_profile(struct iwl_mvm *mvm, int prof_a, int prof_b);
int iwl_mvm_get_sar_geo_profile(struct iwl_mvm *mvm);
int iwl_mvm_ppag_send_cmd(struct iwl_mvm *mvm);
void iwl_mvm_get_bios_tables(struct iwl_mvm *mvm);
#ifdef CONFIG_IWLWIFI_DEBUGFS
void iwl_mvm_link_sta_add_debugfs(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_link_sta *link_sta,
				  struct dentry *dir);
void iwl_mvm_link_add_debugfs(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *link_conf,
			      struct dentry *dir);
#endif

/* new MLD related APIs */
int iwl_mvm_sec_key_add(struct iwl_mvm *mvm,
			struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			struct ieee80211_key_conf *keyconf);
int iwl_mvm_sec_key_del(struct iwl_mvm *mvm,
			struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			struct ieee80211_key_conf *keyconf);
int iwl_mvm_sec_key_del_pasn(struct iwl_mvm *mvm,
			     struct ieee80211_vif *vif,
			     u32 sta_mask,
			     struct ieee80211_key_conf *keyconf);
void iwl_mvm_sec_key_remove_ap(struct iwl_mvm *mvm,
			       struct ieee80211_vif *vif,
			       struct iwl_mvm_vif_link_info *link,
			       unsigned int link_id);
int iwl_mvm_mld_update_sta_keys(struct iwl_mvm *mvm,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta,
				u32 old_sta_mask,
				u32 new_sta_mask);
int iwl_mvm_mld_send_key(struct iwl_mvm *mvm, u32 sta_mask, u32 key_flags,
			 struct ieee80211_key_conf *keyconf);
u32 iwl_mvm_get_sec_flags(struct iwl_mvm *mvm,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *keyconf);

bool iwl_rfi_supported(struct iwl_mvm *mvm);
int iwl_rfi_send_config_cmd(struct iwl_mvm *mvm,
			    struct iwl_rfi_lut_entry *rfi_table);
struct iwl_rfi_freq_table_resp_cmd *iwl_rfi_get_freq_table(struct iwl_mvm *mvm);
void iwl_rfi_deactivate_notif_handler(struct iwl_mvm *mvm,
				      struct iwl_rx_cmd_buffer *rxb);

static inline u8 iwl_mvm_phy_band_from_nl80211(enum nl80211_band band)
{
	switch (band) {
	case NL80211_BAND_2GHZ:
		return PHY_BAND_24;
	case NL80211_BAND_5GHZ:
		return PHY_BAND_5;
	case NL80211_BAND_6GHZ:
		return PHY_BAND_6;
	default:
		WARN_ONCE(1, "Unsupported band (%u)\n", band);
		return PHY_BAND_5;
	}
}

static inline u8 iwl_mvm_nl80211_band_from_phy(u8 phy_band)
{
	switch (phy_band) {
	case PHY_BAND_24:
		return NL80211_BAND_2GHZ;
	case PHY_BAND_5:
		return NL80211_BAND_5GHZ;
	case PHY_BAND_6:
		return NL80211_BAND_6GHZ;
	default:
		WARN_ONCE(1, "Unsupported phy band (%u)\n", phy_band);
		return NL80211_BAND_5GHZ;
	}
}

/* Channel Switch */
void iwl_mvm_channel_switch_disconnect_wk(struct work_struct *wk);
int iwl_mvm_post_channel_switch(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *link);

/* Channel Context */
/**
 * struct iwl_mvm_switch_vif_chanctx_ops - callbacks for switch_vif_chanctx()
 *
 * Since the only difference between both MLD and
 * non-MLD versions of switch_vif_chanctx() is these function calls,
 * each version will send its specific function calls to
 * %iwl_mvm_switch_vif_chanctx_common().
 *
 * @__assign_vif_chanctx: pointer to the function that assigns a chanctx to
 *	a given vif
 * @__unassign_vif_chanctx: pointer to the function that unassigns a chanctx to
 *	a given vif
 */
struct iwl_mvm_switch_vif_chanctx_ops {
	int (*__assign_vif_chanctx)(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *link_conf,
				    struct ieee80211_chanctx_conf *ctx,
				    bool switching_chanctx);
	void (*__unassign_vif_chanctx)(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *link_conf,
				       struct ieee80211_chanctx_conf *ctx,
				       bool switching_chanctx);
};

int
iwl_mvm_switch_vif_chanctx_common(struct ieee80211_hw *hw,
				  struct ieee80211_vif_chanctx_switch *vifs,
				  int n_vifs,
				  enum ieee80211_chanctx_switch_mode mode,
				  const struct iwl_mvm_switch_vif_chanctx_ops *ops);

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
			      const struct cfg80211_chan_def *chandef)
{
	enum nl80211_band band = chandef->chan->band;

	iwl_mvm_set_chan_info(mvm, ci, chandef->chan->hw_value,
			      iwl_mvm_phy_band_from_nl80211(band),
			      iwl_mvm_get_channel_width(chandef),
			      iwl_mvm_get_ctrl_pos(chandef));
}

static inline int iwl_umac_scan_get_max_profiles(const struct iwl_fw *fw)
{
	u8 ver = iwl_fw_lookup_cmd_ver(fw, SCAN_OFFLOAD_UPDATE_PROFILES_CMD,
				       IWL_FW_CMD_VER_UNKNOWN);
	return (ver == IWL_FW_CMD_VER_UNKNOWN || ver < 3) ?
		IWL_SCAN_MAX_PROFILES : IWL_SCAN_MAX_PROFILES_V2;
}

static inline
enum iwl_location_cipher iwl_mvm_cipher_to_location_cipher(u32 cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		return IWL_LOCATION_CIPHER_CCMP_128;
	case WLAN_CIPHER_SUITE_GCMP:
		return IWL_LOCATION_CIPHER_GCMP_128;
	case WLAN_CIPHER_SUITE_GCMP_256:
		return IWL_LOCATION_CIPHER_GCMP_256;
	default:
		return IWL_LOCATION_CIPHER_INVALID;
	}
}

struct iwl_mvm_csme_conn_info *iwl_mvm_get_csme_conn_info(struct iwl_mvm *mvm);
static inline int iwl_mvm_mei_get_ownership(struct iwl_mvm *mvm)
{
	if (mvm->mei_registered)
		return iwl_mei_get_ownership();
	return 0;
}

static inline void iwl_mvm_mei_tx_copy_to_csme(struct iwl_mvm *mvm,
					       struct sk_buff *skb,
					       unsigned int ivlen)
{
	if (mvm->mei_registered)
		iwl_mei_tx_copy_to_csme(skb, ivlen);
}

static inline void iwl_mvm_mei_host_disassociated(struct iwl_mvm *mvm)
{
	if (mvm->mei_registered)
		iwl_mei_host_disassociated();
}

static inline void iwl_mvm_mei_device_state(struct iwl_mvm *mvm, bool up)
{
	if (mvm->mei_registered)
		iwl_mei_device_state(up);
}

static inline void iwl_mvm_mei_set_sw_rfkill_state(struct iwl_mvm *mvm)
{
	bool sw_rfkill =
		mvm->hw_registered ? rfkill_soft_blocked(mvm->hw->wiphy->rfkill) : false;

	if (mvm->mei_registered)
		iwl_mei_set_rfkill_state(iwl_mvm_is_radio_killed(mvm),
					 sw_rfkill);
}

static inline bool iwl_mvm_has_p2p_over_aux(struct iwl_mvm *mvm)
{
	u32 cmd_id = WIDE_ID(MAC_CONF_GROUP, ROC_CMD);

	return iwl_fw_lookup_cmd_ver(mvm->fw, cmd_id, 0) >= 4;
}

static inline bool iwl_mvm_mei_filter_scan(struct iwl_mvm *mvm,
					   struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;

	if (mvm->mei_scan_filter.is_mei_limited_scan &&
	    (ieee80211_is_probe_resp(mgmt->frame_control) ||
	     ieee80211_is_beacon(mgmt->frame_control))) {
		skb_queue_tail(&mvm->mei_scan_filter.scan_res, skb);
		schedule_work(&mvm->mei_scan_filter.scan_work);
		return true;
	}

	return false;
}

void iwl_mvm_send_roaming_forbidden_event(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  bool forbidden);

/* Callbacks for ieee80211_ops */
void iwl_mvm_mac_tx(struct ieee80211_hw *hw,
		    struct ieee80211_tx_control *control, struct sk_buff *skb);
void iwl_mvm_mac_wake_tx_queue(struct ieee80211_hw *hw,
			       struct ieee80211_txq *txq);

int iwl_mvm_mac_ampdu_action(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_ampdu_params *params);
int iwl_mvm_op_get_antenna(struct ieee80211_hw *hw, int radio_idx, u32 *tx_ant,
			   u32 *rx_ant);
int iwl_mvm_op_set_antenna(struct ieee80211_hw *hw, int radio_idx, u32 tx_ant,
			   u32 rx_ant);
int iwl_mvm_mac_start(struct ieee80211_hw *hw);
void iwl_mvm_mac_reconfig_complete(struct ieee80211_hw *hw,
				   enum ieee80211_reconfig_type reconfig_type);
void iwl_mvm_mac_stop(struct ieee80211_hw *hw, bool suspend);
static inline int iwl_mvm_mac_config(struct ieee80211_hw *hw, int radio_idx,
				     u32 changed)
{
	return 0;
}

u64 iwl_mvm_prepare_multicast(struct ieee80211_hw *hw,
			      struct netdev_hw_addr_list *mc_list);

void iwl_mvm_configure_filter(struct ieee80211_hw *hw,
			      unsigned int changed_flags,
			      unsigned int *total_flags, u64 multicast);
int iwl_mvm_mac_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_scan_request *hw_req);
void iwl_mvm_mac_cancel_hw_scan(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif);
void iwl_mvm_sta_pre_rcu_remove(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta);
void iwl_mvm_mac_sta_notify(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    enum sta_notify_cmd cmd,
			    struct ieee80211_sta *sta);
void
iwl_mvm_mac_allow_buffered_frames(struct ieee80211_hw *hw,
				  struct ieee80211_sta *sta, u16 tids,
				  int num_frames,
				  enum ieee80211_frame_release_type reason,
				  bool more_data);
void
iwl_mvm_mac_release_buffered_frames(struct ieee80211_hw *hw,
				    struct ieee80211_sta *sta, u16 tids,
				    int num_frames,
				    enum ieee80211_frame_release_type reason,
				    bool more_data);
int iwl_mvm_mac_set_rts_threshold(struct ieee80211_hw *hw, int radio_idx,
				  u32 value);
void iwl_mvm_sta_rc_update(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_link_sta *link_sta, u32 changed);
void iwl_mvm_mac_mgd_prepare_tx(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_prep_tx_info *info);
void iwl_mvm_mac_mgd_complete_tx(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_prep_tx_info *info);
void iwl_mvm_mac_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       u32 queues, bool drop);
void iwl_mvm_mac_flush_sta(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);
int iwl_mvm_mac_sched_scan_start(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct cfg80211_sched_scan_request *req,
				 struct ieee80211_scan_ies *ies);
int iwl_mvm_mac_sched_scan_stop(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif);
int iwl_mvm_mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			struct ieee80211_key_conf *key);
void iwl_mvm_mac_update_tkip_key(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_key_conf *keyconf,
				 struct ieee80211_sta *sta,
				 u32 iv32, u16 *phase1key);
int iwl_mvm_add_chanctx(struct ieee80211_hw *hw,
			struct ieee80211_chanctx_conf *ctx);
void iwl_mvm_remove_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_chanctx_conf *ctx);
void iwl_mvm_change_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_chanctx_conf *ctx, u32 changed);
int iwl_mvm_tx_last_beacon(struct ieee80211_hw *hw);
void iwl_mvm_channel_switch(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_channel_switch *chsw);
int iwl_mvm_pre_channel_switch(struct iwl_mvm *mvm,
			       struct ieee80211_vif *vif,
			       struct ieee80211_channel_switch *chsw);
void iwl_mvm_abort_channel_switch(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_bss_conf *link_conf);
void iwl_mvm_channel_switch_rx_beacon(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_channel_switch *chsw);
void iwl_mvm_mac_event_callback(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				const struct ieee80211_event *event);
void iwl_mvm_sync_rx_queues(struct ieee80211_hw *hw);
int iwl_mvm_mac_testmode_cmd(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     void *data, int len);
int iwl_mvm_mac_get_survey(struct ieee80211_hw *hw, int idx,
			   struct survey_info *survey);
void iwl_mvm_mac_sta_statistics(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta,
				struct station_info *sinfo);
int
iwl_mvm_mac_get_ftm_responder_stats(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct cfg80211_ftm_responder_stats *stats);
int iwl_mvm_start_pmsr(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       struct cfg80211_pmsr_request *request);
void iwl_mvm_abort_pmsr(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct cfg80211_pmsr_request *request);

bool iwl_mvm_have_links_same_channel(struct iwl_mvm_vif *vif1,
				     struct iwl_mvm_vif *vif2);
bool iwl_mvm_vif_is_active(struct iwl_mvm_vif *mvmvif);
int iwl_mvm_set_tx_power(struct iwl_mvm *mvm,
			 struct ieee80211_bss_conf *bss_conf,
			 s16 tx_power);
int iwl_mvm_set_hw_timestamp(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct cfg80211_set_hw_timestamp *hwts);
int iwl_mvm_update_mu_groups(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
bool iwl_mvm_enable_fils(struct iwl_mvm *mvm,
			 struct ieee80211_vif *vif,
			 struct ieee80211_chanctx_conf *ctx);

struct cfg80211_chan_def *
iwl_mvm_chanctx_def(struct iwl_mvm *mvm, struct ieee80211_chanctx_conf *ctx);

void iwl_mvm_roc_duration_and_delay(struct ieee80211_vif *vif,
				    u32 duration_ms,
				    u32 *duration_tu,
				    u32 *delay);
int iwl_mvm_roc_add_cmd(struct iwl_mvm *mvm,
			struct ieee80211_channel *channel,
			struct ieee80211_vif *vif,
			int duration, enum iwl_roc_activity activity);

/* EMLSR */
bool iwl_mvm_vif_has_esr_cap(struct iwl_mvm *mvm, struct ieee80211_vif *vif);
void iwl_mvm_block_esr(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		       enum iwl_mvm_esr_state reason,
		       u8 link_to_keep);
int iwl_mvm_block_esr_sync(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			   enum iwl_mvm_esr_state reason);
void iwl_mvm_unblock_esr(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			 enum iwl_mvm_esr_state reason);
void iwl_mvm_exit_esr(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		      enum iwl_mvm_esr_state reason,
		      u8 link_to_keep);
s8 iwl_mvm_get_esr_rssi_thresh(struct iwl_mvm *mvm,
			       const struct cfg80211_chan_def *chandef,
			       bool low);
void iwl_mvm_bt_coex_update_link_esr(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif,
				     int link_id);
bool
iwl_mvm_bt_coex_calculate_esr_mode(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif,
				   s32 link_rssi,
				   bool primary);
int iwl_mvm_esr_non_bss_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     unsigned int link_id, bool active);

void
iwl_mvm_send_ap_tx_power_constraint_cmd(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					struct ieee80211_bss_conf *bss_conf,
					bool is_ap);

void iwl_mvm_smps_workaround(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     bool update);
#endif /* __IWL_MVM_H__ */
