/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_mld_roc_h__
#define __iwl_mld_roc_h__

#include <net/mac80211.h>

int iwl_mld_start_roc(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_channel *channel, int duration,
		      enum ieee80211_roc_type type);

int iwl_mld_cancel_roc(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif);

void iwl_mld_handle_roc_notif(struct iwl_mld *mld,
			      struct iwl_rx_packet *pkt);

#endif /* __iwl_mld_roc_h__ */
