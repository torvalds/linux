/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#ifndef __iwl_mld_h__
#define __iwl_mld_h__

#include <linux/leds.h>
#include <net/mac80211.h>

#include "iwl-trans.h"
#include "iwl-op-mode.h"
#include "fw/runtime.h"
#include "fw/notif-wait.h"
#include "fw/api/commands.h"
#include "fw/api/scan.h"
#include "fw/api/mac-cfg.h"
#include "fw/api/mac.h"
#include "fw/api/phy-ctxt.h"
#include "fw/api/datapath.h"
#include "fw/api/rx.h"
#include "fw/api/rs.h"
#include "fw/api/context.h"
#include "fw/api/coex.h"
#include "fw/api/location.h"

#include "fw/dbg.h"

#include "notif.h"
#include "scan.h"
#include "rx.h"
#include "thermal.h"
#include "low_latency.h"
#include "constants.h"
#include "ptp.h"
#include "time_sync.h"
#include "ftm-initiator.h"

/**
 * DOC: Introduction
 *
 * iwlmld is an operation mode (a.k.a. op_mode) for Intel wireless devices.
 * It is used for devices that ship after 2024 which typically support
 * the WiFi-7 features. MLD stands for multi-link device. Note that there are
 * devices that do not support WiFi-7 or even WiFi 6E and yet use iwlmld, but
 * the firmware APIs used in this driver are WiFi-7 compatible.
 *
 * In the architecture of iwlwifi, an op_mode is a layer that translates
 * mac80211's APIs into commands for the firmware and, of course, notifications
 * from the firmware to mac80211's APIs. An op_mode must implement the
 * interface defined in iwl-op-mode.h to interact with the transport layer
 * which allows to send and receive data to the device, start the hardware,
 * etc...
 */

/**
 * DOC: Locking policy
 *
 * iwlmld has a very simple locking policy: it doesn't have any mutexes. It
 * relies on cfg80211's wiphy->mtx and takes the lock when needed. All the
 * control flows originating from mac80211 already acquired the lock, so that
 * part is trivial, but also notifications that are received from the firmware
 * and handled asynchronously are handled only after having taken the lock.
 * This is described in notif.c.
 * There are spin_locks needed to synchronize with the data path, around the
 * allocation of the queues, for example.
 */

/**
 * DOC: Debugfs
 *
 * iwlmld adds its share of debugfs hooks and its handlers are synchronized
 * with the wiphy_lock using wiphy_locked_debugfs. This avoids races against
 * resources deletion while the debugfs hook is being used.
 */

/**
 * DOC: Main resources
 *
 * iwlmld is designed with the life cycle of the resource in mind. The
 * resources are:
 *
 *  - struct iwl_mld (matches mac80211's struct ieee80211_hw)
 *
 *  - struct iwl_mld_vif (matches macu80211's struct ieee80211_vif)
 *    iwl_mld_vif contains an array of pointers to struct iwl_mld_link
 *    which describe the links for this vif.
 *
 *  - struct iwl_mld_sta (matches mac80211's struct ieee80211_sta)
 *    iwl_mld_sta contains an array of points to struct iwl_mld_link_sta
 *    which describes the link stations for this station
 *
 * Each object has properties that can survive a firmware reset or not.
 * Asynchronous firmware notifications can declare themselves as dependent on a
 * certain instance of those resources and that means that the notifications
 * will be cancelled once the instance is destroyed.
 */

#define IWL_MLD_MAX_ADDRESSES		5

/**
 * struct iwl_mld - MLD op mode
 *
 * @fw_id_to_bss_conf: maps a fw id of a link to the corresponding
 *	ieee80211_bss_conf.
 * @fw_id_to_vif: maps a fw id of a MAC context to the corresponding
 *	ieee80211_vif. Mapping is valid only when the MAC exists in the fw.
 * @fw_id_to_txq: maps a fw id of a txq to the corresponding
 *	ieee80211_txq.
 * @used_phy_ids: a bitmap of the phy IDs used. If a bit is set, it means
 *	that the index of this bit is already used as a PHY id.
 * @num_igtks: the number if iGTKs that were sent to the FW.
 * @monitor: monitor related data
 * @monitor.on: does a monitor vif exist (singleton hence bool)
 * @monitor.ampdu_ref: the id of the A-MPDU for sniffer
 * @monitor.ampdu_toggle: the state of the previous packet to track A-MPDU
 * @monitor.cur_aid: current association id tracked by the sniffer
 * @monitor.cur_bssid: current bssid tracked by the sniffer
 * @monitor.p80: primary channel position relative to he whole bandwidth, in
 * steps of 80 MHz
 * @fw_id_to_link_sta: maps a fw id of a sta to the corresponding
 *	ieee80211_link_sta. This is not cleaned up on restart since we want to
 *	preserve the fw sta ids during a restart (for SN/PN restoring).
 *	FW ids of internal stations will be mapped to ERR_PTR, and will be
 *	re-allocated during a restart, so make sure to free it in restart
 *	cleanup using iwl_mld_free_internal_sta
 * @netdetect: indicates the FW is in suspend mode with netdetect configured
 * @p2p_device_vif: points to the p2p device vif if exists
 * @bt_is_active: indicates that BT is active
 * @dev: pointer to device struct. For printing purposes
 * @trans: pointer to the transport layer
 * @cfg: pointer to the device configuration
 * @fw: a pointer to the fw object
 * @hw: pointer to the hw object.
 * @wiphy: a pointer to the wiphy struct, for easier access to it.
 * @nvm_data: pointer to the nvm_data that includes all our capabilities
 * @fwrt: fw runtime data
 * @debugfs_dir: debugfs directory
 * @notif_wait: notification wait related data.
 * @async_handlers_list: a list of all async RX handlers. When a notifciation
 *	with an async handler is received, it is added to this list.
 *	When &async_handlers_wk runs - it runs these handlers one by one.
 * @async_handlers_lock: a lock for &async_handlers_list. Sync
 *	&async_handlers_wk and RX notifcation path.
 * @async_handlers_wk: A work to run all async RX handlers from
 *	&async_handlers_list.
 * @ct_kill_exit_wk: worker to exit thermal kill
 * @fw_status: bitmap of fw status bits
 * @running: true if the firmware is running
 * @do_not_dump_once: true if firmware dump must be prevented once
 * @in_d3: indicates FW is in suspend mode and should be resumed
 * @in_hw_restart: indicates that we are currently in restart flow.
 *	rather than restarted. Should be unset upon restart.
 * @radio_kill: bitmap of radio kill status
 * @radio_kill.hw: radio is killed by hw switch
 * @radio_kill.ct: radio is killed because the device it too hot
 * @addresses: device MAC addresses.
 * @scan: instance of the scan object
 * @wowlan: WoWLAN support data.
 * @led: the led device
 * @mcc_src: the source id of the MCC, comes from the firmware
 * @bios_enable_puncturing: is puncturing enabled by bios
 * @fw_id_to_ba: maps a fw (BA) id to a corresponding Block Ack session data.
 * @num_rx_ba_sessions: tracks the number of active Rx Block Ack (BA) sessions.
 *	the driver ensures that new BA sessions are blocked once the maximum
 *	supported by the firmware is reached, preventing firmware asserts.
 * @rxq_sync: manages RX queue sync state
 * @txqs_to_add: a list of &ieee80211_txq's to allocate in &add_txqs_wk
 * @add_txqs_wk: a worker to allocate txqs.
 * @add_txqs_lock: to lock the &txqs_to_add list.
 * @error_recovery_buf: pointer to the recovery buffer that will be read
 *	from firmware upon fw/hw error and sent back to the firmware in
 *	reconfig flow (after NIC reset).
 * @mcast_filter_cmd: pointer to the multicast filter command.
 * @mgmt_tx_ant: stores the last TX antenna index; used for setting
 *	TX rate_n_flags for non-STA mgmt frames (toggles on every TX failure).
 * @low_latency: low-latency manager.
 * @tzone: thermal zone device's data
 * @cooling_dev: cooling device's related data
 * @ibss_manager: in IBSS mode (only one vif can be active), indicates what
 *	firmware indicated about having transmitted the last beacon, i.e.
 *	being IBSS manager for that time and needing to respond to probe
 *	requests
 * @ptp_data: data of the PTP clock
 * @time_sync: time sync data.
 * @ftm_initiator: FTM initiator data
 */
struct iwl_mld {
	/* Add here fields that need clean up on restart */
	struct_group(zeroed_on_hw_restart,
		struct ieee80211_bss_conf __rcu *fw_id_to_bss_conf[IWL_FW_MAX_LINK_ID + 1];
		struct ieee80211_vif __rcu *fw_id_to_vif[NUM_MAC_INDEX_DRIVER];
		struct ieee80211_txq __rcu *fw_id_to_txq[IWL_MAX_TVQM_QUEUES];
		u8 used_phy_ids: NUM_PHY_CTX;
		u8 num_igtks;
		struct {
			bool on;
			u32 ampdu_ref;
			bool ampdu_toggle;
			u8 p80;
#ifdef CONFIG_IWLWIFI_DEBUGFS
			__le16 cur_aid;
			u8 cur_bssid[ETH_ALEN];
#endif
		} monitor;
#ifdef CONFIG_PM_SLEEP
		bool netdetect;
#endif /* CONFIG_PM_SLEEP */
		struct ieee80211_vif *p2p_device_vif;
		bool bt_is_active;
	);
	struct ieee80211_link_sta __rcu *fw_id_to_link_sta[IWL_STATION_COUNT_MAX];
	/* And here fields that survive a fw restart */
	struct device *dev;
	struct iwl_trans *trans;
	const struct iwl_cfg *cfg;
	const struct iwl_fw *fw;
	struct ieee80211_hw *hw;
	struct wiphy *wiphy;
	struct iwl_nvm_data *nvm_data;
	struct iwl_fw_runtime fwrt;
	struct dentry *debugfs_dir;
	struct iwl_notif_wait_data notif_wait;
	struct list_head async_handlers_list;
	spinlock_t async_handlers_lock;
	struct wiphy_work async_handlers_wk;
	struct wiphy_delayed_work ct_kill_exit_wk;

	struct {
		u32 running:1,
		    do_not_dump_once:1,
#ifdef CONFIG_PM_SLEEP
		    in_d3:1,
#endif
		    in_hw_restart:1;

	} fw_status;

	struct {
		u32 hw:1,
		    ct:1;
	} radio_kill;

	struct mac_address addresses[IWL_MLD_MAX_ADDRESSES];
	struct iwl_mld_scan scan;
#ifdef CONFIG_PM_SLEEP
	struct wiphy_wowlan_support wowlan;
#endif /* CONFIG_PM_SLEEP */
#ifdef CONFIG_IWLWIFI_LEDS
	struct led_classdev led;
#endif
	enum iwl_mcc_source mcc_src;
	bool bios_enable_puncturing;

	struct iwl_mld_baid_data __rcu *fw_id_to_ba[IWL_MAX_BAID];
	u8 num_rx_ba_sessions;

	struct iwl_mld_rx_queues_sync rxq_sync;

	struct list_head txqs_to_add;
	struct wiphy_work add_txqs_wk;
	spinlock_t add_txqs_lock;

	u8 *error_recovery_buf;
	struct iwl_mcast_filter_cmd *mcast_filter_cmd;

	u8 mgmt_tx_ant;

	struct iwl_mld_low_latency low_latency;

	bool ibss_manager;
#ifdef CONFIG_THERMAL
	struct thermal_zone_device *tzone;
	struct iwl_mld_cooling_device cooling_dev;
#endif

	struct ptp_data ptp_data;

	struct iwl_mld_time_sync_data __rcu *time_sync;

	struct ftm_initiator_data ftm_initiator;
};

/* memset the part of the struct that requires cleanup on restart */
#define CLEANUP_STRUCT(_ptr)                             \
	memset((void *)&(_ptr)->zeroed_on_hw_restart, 0, \
	       sizeof((_ptr)->zeroed_on_hw_restart))

/* Cleanup function for struct iwl_mld_vif, will be called in restart */
static inline void
iwl_cleanup_mld(struct iwl_mld *mld)
{
	CLEANUP_STRUCT(mld);
	CLEANUP_STRUCT(&mld->scan);

#ifdef CONFIG_PM_SLEEP
	mld->fw_status.in_d3 = false;
#endif

	iwl_mld_low_latency_restart_cleanup(mld);

	/* Empty the list of async notification handlers so we won't process
	 * notifications from the dead fw after the reconfig flow.
	 */
	iwl_mld_purge_async_handlers_list(mld);
}

enum iwl_power_scheme {
	IWL_POWER_SCHEME_CAM = 1,
	IWL_POWER_SCHEME_BPS,
};

/**
 * struct iwl_mld_mod_params - module parameters for iwlmld
 * @power_scheme: one of enum iwl_power_scheme
 */
struct iwl_mld_mod_params {
	int power_scheme;
};

extern struct iwl_mld_mod_params iwlmld_mod_params;

/* Extract MLD priv from op_mode */
#define IWL_OP_MODE_GET_MLD(_iwl_op_mode)		\
	((struct iwl_mld *)(_iwl_op_mode)->op_mode_specific)

#define IWL_MAC80211_GET_MLD(_hw)			\
	IWL_OP_MODE_GET_MLD((struct iwl_op_mode *)((_hw)->priv))

#ifdef CONFIG_IWLWIFI_DEBUGFS
void
iwl_mld_add_debugfs_files(struct iwl_mld *mld, struct dentry *debugfs_dir);
#else
static inline void
iwl_mld_add_debugfs_files(struct iwl_mld *mld, struct dentry *debugfs_dir)
{}
#endif

int iwl_mld_load_fw(struct iwl_mld *mld);
void iwl_mld_stop_fw(struct iwl_mld *mld);
int iwl_mld_start_fw(struct iwl_mld *mld);
void iwl_mld_send_recovery_cmd(struct iwl_mld *mld, u32 flags);

static inline void iwl_mld_set_ctkill(struct iwl_mld *mld, bool state)
{
	mld->radio_kill.ct = state;

	wiphy_rfkill_set_hw_state(mld->wiphy,
				  mld->radio_kill.hw || mld->radio_kill.ct);
}

static inline void iwl_mld_set_hwkill(struct iwl_mld *mld, bool state)
{
	mld->radio_kill.hw = state;

	wiphy_rfkill_set_hw_state(mld->wiphy,
				  mld->radio_kill.hw || mld->radio_kill.ct);
}

static inline u8 iwl_mld_get_valid_tx_ant(const struct iwl_mld *mld)
{
	u8 tx_ant = mld->fw->valid_tx_ant;

	if (mld->nvm_data && mld->nvm_data->valid_tx_ant)
		tx_ant &= mld->nvm_data->valid_tx_ant;

	return tx_ant;
}

static inline u8 iwl_mld_get_valid_rx_ant(const struct iwl_mld *mld)
{
	u8 rx_ant = mld->fw->valid_rx_ant;

	if (mld->nvm_data && mld->nvm_data->valid_rx_ant)
		rx_ant &= mld->nvm_data->valid_rx_ant;

	return rx_ant;
}

static inline u8 iwl_mld_nl80211_band_to_fw(enum nl80211_band band)
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

static inline u8 iwl_mld_phy_band_to_nl80211(u8 phy_band)
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

static inline int
iwl_mld_legacy_hw_idx_to_mac80211_idx(u32 rate_n_flags,
				      enum nl80211_band band)
{
	int format = rate_n_flags & RATE_MCS_MOD_TYPE_MSK;
	int rate = rate_n_flags & RATE_LEGACY_RATE_MSK;
	bool is_lb = band == NL80211_BAND_2GHZ;

	if (format == RATE_MCS_LEGACY_OFDM_MSK)
		return is_lb ? rate + IWL_FIRST_OFDM_RATE : rate;

	/* CCK is not allowed in 5 GHz */
	return is_lb ? rate : -1;
}

extern const struct ieee80211_ops iwl_mld_hw_ops;

/**
 * enum iwl_rx_handler_context: context for Rx handler
 * @RX_HANDLER_SYNC: this means that it will be called in the Rx path
 *	which can't acquire the wiphy->mutex.
 * @RX_HANDLER_ASYNC: If the handler needs to hold wiphy->mutex
 *	(and only in this case!), it should be set as ASYNC. In that case,
 *	it will be called from a worker with wiphy->mutex held.
 */
enum iwl_rx_handler_context {
	RX_HANDLER_SYNC,
	RX_HANDLER_ASYNC,
};

/**
 * struct iwl_rx_handler: handler for FW notification
 * @val_fn: input validation function.
 * @sizes: an array that mapps a version to the expected size.
 * @fn: the function is called when notification is handled
 * @cmd_id: command id
 * @n_sizes: number of elements in &sizes.
 * @context: see &iwl_rx_handler_context
 * @obj_type: the type of the object that this handler is related to.
 *	See &iwl_mld_object_type. Use IWL_MLD_OBJECT_TYPE_NONE if not related.
 * @cancel: function to cancel the notification. valid only if obj_type is not
 *	IWL_MLD_OBJECT_TYPE_NONE.
 */
struct iwl_rx_handler {
	union {
		bool (*val_fn)(struct iwl_mld *mld, struct iwl_rx_packet *pkt);
		const struct iwl_notif_struct_size *sizes;
	};
	void (*fn)(struct iwl_mld *mld, struct iwl_rx_packet *pkt);
	u16 cmd_id;
	u8 n_sizes;
	u8 context;
	enum iwl_mld_object_type obj_type;
	bool (*cancel)(struct iwl_mld *mld, struct iwl_rx_packet *pkt,
		       u32 obj_id);
};

/**
 * struct iwl_notif_struct_size: map a notif ver to the expected size
 *
 * @size: the size to expect
 * @ver: the version of the notification
 */
struct iwl_notif_struct_size {
	u32 size:24, ver:8;
};

#if IS_ENABLED(CONFIG_IWLWIFI_KUNIT_TESTS)
extern const struct iwl_hcmd_arr iwl_mld_groups[];
extern const unsigned int global_iwl_mld_goups_size;
extern const struct iwl_rx_handler iwl_mld_rx_handlers[];
extern const unsigned int iwl_mld_rx_handlers_num;

bool
iwl_mld_is_dup(struct iwl_mld *mld, struct ieee80211_sta *sta,
	       struct ieee80211_hdr *hdr,
	       const struct iwl_rx_mpdu_desc *mpdu_desc,
	       struct ieee80211_rx_status *rx_status, int queue);

void iwl_construct_mld(struct iwl_mld *mld, struct iwl_trans *trans,
		       const struct iwl_cfg *cfg, const struct iwl_fw *fw,
		       struct ieee80211_hw *hw, struct dentry *dbgfs_dir);
#endif

#define IWL_MLD_INVALID_FW_ID 0xff

#define IWL_MLD_ALLOC_FN(_type, _mac80211_type)						\
static int										\
iwl_mld_allocate_##_type##_fw_id(struct iwl_mld *mld,					\
				 u8 *fw_id,				\
				 struct ieee80211_##_mac80211_type *mac80211_ptr)	\
{											\
	u8 rand = IWL_MLD_DIS_RANDOM_FW_ID ? 0 : get_random_u8();			\
	u8 arr_sz = ARRAY_SIZE(mld->fw_id_to_##_mac80211_type);				\
	if (__builtin_types_compatible_p(typeof(*mac80211_ptr),				\
					 struct ieee80211_link_sta))			\
		arr_sz = mld->fw->ucode_capa.num_stations;				\
	if (__builtin_types_compatible_p(typeof(*mac80211_ptr),				\
					 struct ieee80211_bss_conf))			\
		arr_sz = mld->fw->ucode_capa.num_links;					\
	for (int i = 0; i < arr_sz; i++) {						\
		u8 idx = (i + rand) % arr_sz;						\
		if (rcu_access_pointer(mld->fw_id_to_##_mac80211_type[idx]))		\
			continue;							\
		IWL_DEBUG_INFO(mld, "Allocated at index %d / %d\n", idx, arr_sz);	\
		*fw_id = idx;								\
		rcu_assign_pointer(mld->fw_id_to_##_mac80211_type[idx], mac80211_ptr);	\
		return 0;								\
	}										\
	return -ENOSPC;									\
}

static inline struct ieee80211_bss_conf *
iwl_mld_fw_id_to_link_conf(struct iwl_mld *mld, u8 fw_link_id)
{
	if (IWL_FW_CHECK(mld, fw_link_id >= mld->fw->ucode_capa.num_links,
			 "Invalid fw_link_id: %d\n", fw_link_id))
		return NULL;

	return wiphy_dereference(mld->wiphy,
				 mld->fw_id_to_bss_conf[fw_link_id]);
}

#define MSEC_TO_TU(_msec)	((_msec) * 1000 / 1024)

void iwl_mld_add_vif_debugfs(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif);
void iwl_mld_add_link_debugfs(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *link_conf,
			      struct dentry *dir);
void iwl_mld_add_link_sta_debugfs(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_link_sta *link_sta,
				  struct dentry *dir);

/* Utilities */

static inline u8 iwl_mld_mac80211_ac_to_fw_tx_fifo(enum ieee80211_ac_numbers ac)
{
	static const u8 mac80211_ac_to_fw_tx_fifo[] = {
		IWL_BZ_EDCA_TX_FIFO_VO,
		IWL_BZ_EDCA_TX_FIFO_VI,
		IWL_BZ_EDCA_TX_FIFO_BE,
		IWL_BZ_EDCA_TX_FIFO_BK,
		IWL_BZ_TRIG_TX_FIFO_VO,
		IWL_BZ_TRIG_TX_FIFO_VI,
		IWL_BZ_TRIG_TX_FIFO_BE,
		IWL_BZ_TRIG_TX_FIFO_BK,
	};
	return mac80211_ac_to_fw_tx_fifo[ac];
}

static inline u32
iwl_mld_get_lmac_id(struct iwl_mld *mld, enum nl80211_band band)
{
	if (!fw_has_capa(&mld->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_CDB_SUPPORT) ||
	    band == NL80211_BAND_2GHZ)
		return IWL_LMAC_24G_INDEX;
	return IWL_LMAC_5G_INDEX;
}

/* Check if we had an error, but reconfig flow didn't start yet */
static inline bool iwl_mld_error_before_recovery(struct iwl_mld *mld)
{
	return mld->fw_status.in_hw_restart &&
		!iwl_trans_fw_running(mld->trans);
}

int iwl_mld_tdls_sta_count(struct iwl_mld *mld);

#endif /* __iwl_mld_h__ */
