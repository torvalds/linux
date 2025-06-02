/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_mld_stats_h__
#define __iwl_mld_stats_h__

int iwl_mld_request_periodic_fw_stats(struct iwl_mld *mld, bool enable);

void iwl_mld_mac80211_sta_statistics(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta,
				     struct station_info *sinfo);

void iwl_mld_handle_stats_oper_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt);
void iwl_mld_handle_stats_oper_part1_notif(struct iwl_mld *mld,
					   struct iwl_rx_packet *pkt);

int iwl_mld_clear_stats_in_fw(struct iwl_mld *mld);

#endif /* __iwl_mld_stats_h__ */
