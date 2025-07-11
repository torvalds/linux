/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#ifndef __iwl_mld_link_h__
#define __iwl_mld_link_h__

#include <net/mac80211.h>

#include "mld.h"
#include "sta.h"

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
 * struct iwl_mld_link - link configuration parameters
 *
 * @rcu_head: RCU head for freeing this data.
 * @fw_id: the fw id of the link.
 * @active: if the link is active or not.
 * @queue_params: QoS data from mac80211. This is updated with a call to
 *	drv_conf_tx per each AC, and then notified once with BSS_CHANGED_QOS.
 *	So we store it here and then send one link cmd for all the ACs.
 * @chan_ctx: pointer to the channel context assigned to the link. If a link
 *	has an assigned channel context it means that it is active.
 * @he_ru_2mhz_block: 26-tone RU OFDMA transmissions should be blocked.
 * @igtk: fw can only have one IGTK at a time, whereas mac80211 can have two.
 *	This tracks the one IGTK that currently exists in FW.
 * @vif: the vif this link belongs to
 * @bcast_sta: station used for broadcast packets. Used in AP, GO and IBSS.
 * @mcast_sta: station used for multicast packets. Used in AP, GO and IBSS.
 * @mon_sta: station used for TX injection in monitor interface.
 * @link_id: over the air link ID
 * @average_beacon_energy: average beacon energy for beacons received during
 *	client connections
 * @ap_early_keys: The firmware cannot install keys before bcast/mcast STAs,
 *	but higher layers work differently, so we store the keys here for
 *	later installation.
 * @silent_deactivation: next deactivation needs to be silent.
 * @probe_resp_data: data from FW notification to store NOA related data to be
 *	inserted into probe response.
 * @rx_omi: data for BW reduction with OMI
 * @rx_omi.bw_in_progress: update is in progress (indicates target BW)
 * @rx_omi.exit_ts: timestamp of last exit
 * @rx_omi.finished_work: work for the delayed reaction to the firmware saying
 *	the change was applied, and for then applying a new mode if it was
 *	updated while waiting for firmware/AP settle delay.
 * @rx_omi.desired_bw: desired bandwidth
 * @rx_omi.last_max_bw: last maximum BW used by firmware, for AP BW changes
 */
struct iwl_mld_link {
	struct rcu_head rcu_head;

	/* Add here fields that need clean up on restart */
	struct_group(zeroed_on_hw_restart,
		u8 fw_id;
		bool active;
		struct ieee80211_tx_queue_params queue_params[IEEE80211_NUM_ACS];
		struct ieee80211_chanctx_conf __rcu *chan_ctx;
		bool he_ru_2mhz_block;
		struct ieee80211_key_conf *igtk;
	);
	/* And here fields that survive a fw restart */
	struct ieee80211_vif *vif;
	struct iwl_mld_int_sta bcast_sta;
	struct iwl_mld_int_sta mcast_sta;
	struct iwl_mld_int_sta mon_sta;
	u8 link_id;

	struct {
		struct wiphy_delayed_work finished_work;
		unsigned long exit_ts;
		enum ieee80211_sta_rx_bandwidth bw_in_progress,
						desired_bw,
						last_max_bw;
	} rx_omi;

	/* we can only have 2 GTK + 2 IGTK + 2 BIGTK active at a time */
	struct ieee80211_key_conf *ap_early_keys[6];
	u32 average_beacon_energy;
	bool silent_deactivation;
	struct iwl_probe_resp_data __rcu *probe_resp_data;
};

/* Cleanup function for struct iwl_mld_link, will be called in restart */
static inline void
iwl_mld_cleanup_link(struct iwl_mld *mld, struct iwl_mld_link *link)
{
	struct iwl_probe_resp_data *probe_data;

	probe_data = wiphy_dereference(mld->wiphy, link->probe_resp_data);
	RCU_INIT_POINTER(link->probe_resp_data, NULL);
	if (probe_data)
		kfree_rcu(probe_data, rcu_head);

	CLEANUP_STRUCT(link);
	if (link->bcast_sta.sta_id != IWL_INVALID_STA)
		iwl_mld_free_internal_sta(mld, &link->bcast_sta);
	if (link->mcast_sta.sta_id != IWL_INVALID_STA)
		iwl_mld_free_internal_sta(mld, &link->mcast_sta);
	if (link->mon_sta.sta_id != IWL_INVALID_STA)
		iwl_mld_free_internal_sta(mld, &link->mon_sta);
}

/* Convert a percentage from [0,100] to [0,255] */
#define NORMALIZE_PERCENT_TO_255(percentage) ((percentage) * 256 / 100)

int iwl_mld_add_link(struct iwl_mld *mld,
		     struct ieee80211_bss_conf *bss_conf);
void iwl_mld_remove_link(struct iwl_mld *mld,
			 struct ieee80211_bss_conf *bss_conf);
int iwl_mld_activate_link(struct iwl_mld *mld,
			  struct ieee80211_bss_conf *link);
void iwl_mld_deactivate_link(struct iwl_mld *mld,
			     struct ieee80211_bss_conf *link);
int iwl_mld_change_link_omi_bw(struct iwl_mld *mld,
			       struct ieee80211_bss_conf *link,
			       enum ieee80211_sta_rx_bandwidth bw);
int iwl_mld_change_link_in_fw(struct iwl_mld *mld,
			      struct ieee80211_bss_conf *link, u32 changes);
void iwl_mld_handle_missed_beacon_notif(struct iwl_mld *mld,
					struct iwl_rx_packet *pkt);
bool iwl_mld_cancel_missed_beacon_notif(struct iwl_mld *mld,
					struct iwl_rx_packet *pkt,
					u32 removed_link_id);
int iwl_mld_link_set_associated(struct iwl_mld *mld, struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *link);

unsigned int iwl_mld_get_link_grade(struct iwl_mld *mld,
				    struct ieee80211_bss_conf *link_conf);

unsigned int iwl_mld_get_chan_load(struct iwl_mld *mld,
				   struct ieee80211_bss_conf *link_conf);

int iwl_mld_get_chan_load_by_others(struct iwl_mld *mld,
				    struct ieee80211_bss_conf *link_conf,
				    bool expect_active_link);
void iwl_mld_handle_omi_status_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt);
void iwl_mld_leave_omi_bw_reduction(struct iwl_mld *mld);
void iwl_mld_check_omi_bw_reduction(struct iwl_mld *mld);
void iwl_mld_omi_ap_changed_bw(struct iwl_mld *mld,
			       struct ieee80211_bss_conf *link_conf,
			       enum ieee80211_sta_rx_bandwidth bw);

void iwl_mld_handle_beacon_filter_notif(struct iwl_mld *mld,
					struct iwl_rx_packet *pkt);

#endif /* __iwl_mld_link_h__ */
