/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include <net/cfg80211.h>
#include <linux/etherdevice.h>

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
void iwl_mld_handle_nan_dw_end_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt);
bool iwl_mld_cancel_nan_cluster_notif(struct iwl_mld *mld,
				      struct iwl_rx_packet *pkt,
				      u32 obj_id);
bool iwl_mld_cancel_nan_dw_end_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt,
				     u32 obj_id);
