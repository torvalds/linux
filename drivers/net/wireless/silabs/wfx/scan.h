/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Scan related functions.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_SCAN_H
#define WFX_SCAN_H

#include <net/mac80211.h>

struct wfx_dev;
struct wfx_vif;

void wfx_hw_scan_work(struct work_struct *work);
int wfx_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_scan_request *req);
void wfx_cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void wfx_scan_complete(struct wfx_vif *wvif, int nb_chan_done);

#endif
