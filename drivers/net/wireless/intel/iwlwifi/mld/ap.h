/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_ap_h__
#define __iwl_ap_h__

#include "mld.h"
#include "iface.h"

#include "fw/api/tx.h"

int iwl_mld_update_beacon_template(struct iwl_mld *mld,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *link_conf);

int iwl_mld_start_ap_ibss(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link);

void iwl_mld_stop_ap_ibss(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link);

int iwl_mld_store_ap_early_key(struct iwl_mld *mld,
			       struct ieee80211_key_conf *key,
			       struct iwl_mld_vif *mld_vif);

void iwl_mld_free_ap_early_key(struct iwl_mld *mld,
			       struct ieee80211_key_conf *key,
			       struct iwl_mld_vif *mld_vif);

u8 iwl_mld_get_rate_flags(struct iwl_mld *mld,
			  struct ieee80211_tx_info *info,
			  struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link,
			  enum nl80211_band band);

void iwl_mld_set_tim_idx(struct iwl_mld *mld, __le32 *tim_index,
			 u8 *beacon, u32 frame_size);

int iwl_mld_send_beacon_template_cmd(struct iwl_mld *mld,
				     struct sk_buff *beacon,
				     struct iwl_mac_beacon_cmd *cmd);

#endif /* __iwl_ap_h__ */
