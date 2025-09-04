/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_mld_key_h__
#define __iwl_mld_key_h__

#include "mld.h"
#include <net/mac80211.h>
#include "fw/api/sta.h"

void iwl_mld_remove_key(struct iwl_mld *mld,
			struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			struct ieee80211_key_conf *key);
int iwl_mld_add_key(struct iwl_mld *mld,
		    struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta,
		    struct ieee80211_key_conf *key);
void iwl_mld_remove_ap_keys(struct iwl_mld *mld,
			    struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta,
			    unsigned int link_id);

int iwl_mld_update_sta_keys(struct iwl_mld *mld,
			    struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta,
			    u32 old_sta_mask,
			    u32 new_sta_mask);

static inline void
iwl_mld_cleanup_keys_iter(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key, void *data)
{
	key->hw_key_idx = STA_KEY_IDX_INVALID;
}

void iwl_mld_track_bigtk(struct iwl_mld *mld,
			 struct ieee80211_vif *vif,
			 struct ieee80211_key_conf *key, bool add);

bool iwl_mld_beacon_protection_enabled(struct iwl_mld *mld,
				       struct ieee80211_bss_conf *link);

#endif /* __iwl_mld_key_h__ */
