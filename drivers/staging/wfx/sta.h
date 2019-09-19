/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of mac80211 API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_STA_H
#define WFX_STA_H

#include <net/mac80211.h>

struct wfx_sta_priv {
	int link_id;
	int vif_id;
};

// mac80211 interface
int wfx_start(struct ieee80211_hw *hw);
void wfx_stop(struct ieee80211_hw *hw);
int wfx_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void wfx_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif);

#endif /* WFX_STA_H */
