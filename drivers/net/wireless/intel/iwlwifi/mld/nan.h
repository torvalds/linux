/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2025-2026 Intel Corporation
 */
#ifndef __iwl_mld_nan_h__
#define __iwl_mld_nan_h__
#include <net/cfg80211.h>
#include <linux/etherdevice.h>

/**
 * struct iwl_mld_nan_link - struct representing a NAN link
 * @chanctx: the channel context
 * @active: indicates the NAN link is currently active
 * @fw_id: FW link ID
 */
struct iwl_mld_nan_link {
	struct ieee80211_chanctx_conf *chanctx;
	bool active;
	u8 fw_id;
};

/* Cleanup function for struct iwl_mld_nan_link, will be called in restart */
static inline void iwl_mld_cleanup_nan_link(struct iwl_mld_nan_link *nan_link)
{
	memset(nan_link, 0, sizeof(*nan_link));
	nan_link->fw_id = FW_CTXT_ID_INVALID;
}

bool iwl_mld_nan_supported(struct iwl_mld *mld);
int iwl_mld_start_nan(struct ieee80211_hw *hw,
		      struct ieee80211_vif *vif,
		      struct cfg80211_nan_conf *conf);
int iwl_mld_nan_change_config(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct cfg80211_nan_conf *conf,
			      u32 changes);
int iwl_mld_stop_nan(struct ieee80211_hw *hw,
		     struct ieee80211_vif *vif);
void iwl_mld_handle_nan_cluster_notif(struct iwl_mld *mld,
				      struct iwl_rx_packet *pkt);
void iwl_mld_handle_nan_ulw_attr_notif(struct iwl_mld *mld,
				       struct iwl_rx_packet *pkt);
void iwl_mld_handle_nan_dw_end_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt);
void iwl_mld_handle_nan_sched_update_completed_notif(struct iwl_mld *mld,
						     struct iwl_rx_packet *pkt);
bool iwl_mld_cancel_nan_cluster_notif(struct iwl_mld *mld,
				      struct iwl_rx_packet *pkt,
				      u32 obj_id);
bool iwl_mld_cancel_nan_ulw_attr_notif(struct iwl_mld *mld,
				       struct iwl_rx_packet *pkt,
				       u32 obj_id);
bool iwl_mld_cancel_nan_dw_end_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt,
				     u32 obj_id);
bool iwl_mld_cancel_nan_sched_update_completed_notif(struct iwl_mld *mld,
						     struct iwl_rx_packet *pkt,
						     u32 obj_id);
void iwl_mld_nan_vif_cfg_changed(struct iwl_mld *mld,
				 struct ieee80211_vif *vif,
				 u64 changes);

int iwl_mld_mac802111_nan_peer_sched_changed(struct ieee80211_hw *hw,
					     struct ieee80211_sta *sta);

int iwl_mld_nan_get_mgmt_queue(struct iwl_mld *mld, struct ieee80211_vif *vif);

bool iwl_mld_nan_use_nan_stations(struct iwl_mld *mld);

#endif /* __iwl_mld_nan_h__ */
