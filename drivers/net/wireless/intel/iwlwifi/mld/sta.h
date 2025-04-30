/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */

#ifndef __iwl_mld_sta_h__
#define __iwl_mld_sta_h__

#include <net/mac80211.h>

#include "mld.h"
#include "tx.h"

/**
 * struct iwl_mld_rxq_dup_data - Duplication detection data, per STA & Rx queue
 * @last_seq: last sequence per tid.
 * @last_sub_frame_idx: the index of the last subframe in an A-MSDU. This value
 *	will be zero if the packet is not part of an A-MSDU.
 */
struct iwl_mld_rxq_dup_data {
	__le16 last_seq[IWL_MAX_TID_COUNT + 1];
	u8 last_sub_frame_idx[IWL_MAX_TID_COUNT + 1];
} ____cacheline_aligned_in_smp;

/**
 * struct iwl_mld_link_sta - link-level station
 *
 * This represents the link-level sta - the driver level equivalent to the
 * ieee80211_link_sta
 *
 * @last_rate_n_flags: rate_n_flags from the last &iwl_tlc_update_notif
 * @signal_avg: the signal average coming from the firmware
 * @in_fw: whether the link STA is uploaded to the FW (false during restart)
 * @rcu_head: RCU head for freeing this object
 * @fw_id: the FW id of this link sta.
 */
struct iwl_mld_link_sta {
	/* Add here fields that need clean up on restart */
	struct_group(zeroed_on_hw_restart,
		u32 last_rate_n_flags;
		bool in_fw;
		s8 signal_avg;
	);
	/* And here fields that survive a fw restart */
	struct rcu_head rcu_head;
	u32 fw_id;
};

#define iwl_mld_link_sta_dereference_check(mld_sta, link_id)		\
	rcu_dereference_check((mld_sta)->link[link_id],			\
			      lockdep_is_held(&mld_sta->mld->wiphy->mtx))

#define for_each_mld_link_sta(mld_sta, link_sta, link_id)		\
	for (link_id = 0; link_id < ARRAY_SIZE((mld_sta)->link);	\
	     link_id++)							\
		if ((link_sta =						\
			iwl_mld_link_sta_dereference_check(mld_sta, link_id)))

#define IWL_NUM_DEFAULT_KEYS 4

/* struct iwl_mld_ptk_pn - Holds Packet Number (PN) per TID.
 * @rcu_head: RCU head for freeing this data.
 * @pn: Array storing PN for each TID.
 */
struct iwl_mld_ptk_pn {
	struct rcu_head rcu_head;
	struct {
		u8 pn[IWL_MAX_TID_COUNT][IEEE80211_CCMP_PN_LEN];
	} ____cacheline_aligned_in_smp q[];
};

/**
 * struct iwl_mld_per_link_mpdu_counter - per-link TX/RX MPDU counters
 *
 * @tx: Number of TX MPDUs.
 * @rx: Number of RX MPDUs.
 */
struct iwl_mld_per_link_mpdu_counter {
	u32 tx;
	u32 rx;
};

/**
 * struct iwl_mld_per_q_mpdu_counter - per-queue MPDU counter
 *
 * @lock: Needed to protect the counters when modified from statistics.
 * @per_link: per-link counters.
 * @window_start_time: timestamp of the counting-window start
 */
struct iwl_mld_per_q_mpdu_counter {
	spinlock_t lock;
	struct iwl_mld_per_link_mpdu_counter per_link[IWL_FW_MAX_LINK_ID + 1];
	unsigned long window_start_time;
} ____cacheline_aligned_in_smp;

/**
 * struct iwl_mld_sta - representation of a station in the driver.
 *
 * This represent the MLD-level sta, and will not be added to the FW.
 * Embedded in ieee80211_sta.
 *
 * @vif: pointer the vif object.
 * @sta_state: station state according to enum %ieee80211_sta_state
 * @sta_type: type of this station. See &enum iwl_fw_sta_type
 * @mld: a pointer to the iwl_mld object
 * @dup_data: per queue duplicate packet detection data
 * @data_tx_ant: stores the last TX antenna index; used for setting
 *	TX rate_n_flags for injected data frames (toggles on every TX failure).
 * @tid_to_baid: a simple map of TID to Block-Ack fw id
 * @deflink: This holds the default link STA information, for non MLO STA all
 *	link specific STA information is accessed through @deflink or through
 *	link[0] which points to address of @deflink. For MLO Link STA
 *	the first added link STA will point to deflink.
 * @link: reference to Link Sta entries. For Non MLO STA, except 1st link,
 *	i.e link[0] all links would be assigned to NULL by default and
 *	would access link information via @deflink or link[0]. For MLO
 *	STA, first link STA being added will point its link pointer to
 *	@deflink address and remaining would be allocated and the address
 *	would be assigned to link[link_id] where link_id is the id assigned
 *	by the AP.
 * @ptk_pn: Array of pointers to PTK PN data, used to track the Packet Number
 *	per key index and per queue (TID).
 * @mpdu_counters: RX/TX MPDUs counters for each queue.
 */
struct iwl_mld_sta {
	/* Add here fields that need clean up on restart */
	struct_group(zeroed_on_hw_restart,
		enum ieee80211_sta_state sta_state;
		enum iwl_fw_sta_type sta_type;
	);
	/* And here fields that survive a fw restart */
	struct iwl_mld *mld;
	struct ieee80211_vif *vif;
	struct iwl_mld_rxq_dup_data *dup_data;
	u8 tid_to_baid[IWL_MAX_TID_COUNT];
	u8 data_tx_ant;

	struct iwl_mld_link_sta deflink;
	struct iwl_mld_link_sta __rcu *link[IEEE80211_MLD_MAX_NUM_LINKS];
	struct iwl_mld_ptk_pn __rcu *ptk_pn[IWL_NUM_DEFAULT_KEYS];
	struct iwl_mld_per_q_mpdu_counter *mpdu_counters;
};

static inline struct iwl_mld_sta *
iwl_mld_sta_from_mac80211(struct ieee80211_sta *sta)
{
	return (void *)sta->drv_priv;
}

static inline void
iwl_mld_cleanup_sta(void *data, struct ieee80211_sta *sta)
{
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(sta);
	struct iwl_mld_link_sta *mld_link_sta;
	u8 link_id;

	for (int i = 0; i < ARRAY_SIZE(sta->txq); i++)
		CLEANUP_STRUCT(iwl_mld_txq_from_mac80211(sta->txq[i]));

	for_each_mld_link_sta(mld_sta, mld_link_sta, link_id) {
		CLEANUP_STRUCT(mld_link_sta);

		if (!ieee80211_vif_is_mld(mld_sta->vif)) {
			/* not an MLD STA; only has the deflink with ID zero */
			WARN_ON(link_id);
			continue;
		}

		if (mld_sta->vif->active_links & BIT(link_id))
			continue;

		/* Should not happen as link removal should always succeed */
		WARN_ON(1);
		RCU_INIT_POINTER(mld_sta->link[link_id], NULL);
		RCU_INIT_POINTER(mld_sta->mld->fw_id_to_link_sta[mld_link_sta->fw_id],
				 NULL);
		if (mld_link_sta != &mld_sta->deflink)
			kfree_rcu(mld_link_sta, rcu_head);
	}

	CLEANUP_STRUCT(mld_sta);
}

static inline struct iwl_mld_link_sta *
iwl_mld_link_sta_from_mac80211(struct ieee80211_link_sta *link_sta)
{
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(link_sta->sta);

	return iwl_mld_link_sta_dereference_check(mld_sta, link_sta->link_id);
}

int iwl_mld_add_sta(struct iwl_mld *mld, struct ieee80211_sta *sta,
		    struct ieee80211_vif *vif, enum iwl_fw_sta_type type);
void iwl_mld_remove_sta(struct iwl_mld *mld, struct ieee80211_sta *sta);
int iwl_mld_fw_sta_id_from_link_sta(struct iwl_mld *mld,
				    struct ieee80211_link_sta *link_sta);
u32 iwl_mld_fw_sta_id_mask(struct iwl_mld *mld, struct ieee80211_sta *sta);
int iwl_mld_update_all_link_stations(struct iwl_mld *mld,
				     struct ieee80211_sta *sta);
void iwl_mld_flush_sta_txqs(struct iwl_mld *mld, struct ieee80211_sta *sta);
void iwl_mld_wait_sta_txqs_empty(struct iwl_mld *mld,
				 struct ieee80211_sta *sta);
void iwl_mld_count_mpdu_rx(struct ieee80211_link_sta *link_sta, int queue,
			   u32 count);
void iwl_mld_count_mpdu_tx(struct ieee80211_link_sta *link_sta, u32 count);

/**
 * struct iwl_mld_int_sta - representation of an internal station
 * (a station that exist in FW and in driver, but not in mac80211)
 *
 * @sta_id: the index of the station in the fw
 * @queue_id: the if of the queue used by the station
 * @sta_type: station type. One of &iwl_fw_sta_type
 */
struct iwl_mld_int_sta {
	u8 sta_id;
	u32 queue_id;
	enum iwl_fw_sta_type sta_type;
};

static inline void
iwl_mld_init_internal_sta(struct iwl_mld_int_sta *internal_sta)
{
	internal_sta->sta_id = IWL_INVALID_STA;
	internal_sta->queue_id = IWL_MLD_INVALID_QUEUE;
}

static inline void
iwl_mld_free_internal_sta(struct iwl_mld *mld,
			  struct iwl_mld_int_sta *internal_sta)
{
	if (WARN_ON(internal_sta->sta_id == IWL_INVALID_STA))
		return;

	RCU_INIT_POINTER(mld->fw_id_to_link_sta[internal_sta->sta_id], NULL);
	iwl_mld_init_internal_sta(internal_sta);
}

int iwl_mld_add_bcast_sta(struct iwl_mld *mld,
			  struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link);

int iwl_mld_add_mcast_sta(struct iwl_mld *mld,
			  struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link);

int iwl_mld_add_aux_sta(struct iwl_mld *mld,
			struct iwl_mld_int_sta *internal_sta);

int iwl_mld_add_mon_sta(struct iwl_mld *mld,
			struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *link);

void iwl_mld_remove_bcast_sta(struct iwl_mld *mld,
			      struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *link);

void iwl_mld_remove_mcast_sta(struct iwl_mld *mld,
			      struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *link);

void iwl_mld_remove_aux_sta(struct iwl_mld *mld,
			    struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link);

void iwl_mld_remove_mon_sta(struct iwl_mld *mld,
			    struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link);

int iwl_mld_update_link_stas(struct iwl_mld *mld,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     u16 old_links, u16 new_links);
#endif /* __iwl_mld_sta_h__ */
