/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Scan related functions.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_SCAN_H
#define WFX_SCAN_H

#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <net/mac80211.h>

#include "hif_api_cmd.h"

struct wfx_dev;
struct wfx_vif;

struct wfx_scan {
	struct semaphore lock;
	struct work_struct work;
	struct delayed_work timeout;
	struct cfg80211_scan_request *req;
	struct ieee80211_channel **begin;
	struct ieee80211_channel **curr;
	struct ieee80211_channel **end;
	struct hif_ssid_def ssids[HIF_API_MAX_NB_SSIDS];
	int output_power;
	int n_ssids;
	int status;
	atomic_t in_progress;
};

int wfx_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_scan_request *req);
void wfx_scan_work(struct work_struct *work);
void wfx_scan_timeout(struct work_struct *work);
void wfx_scan_complete_cb(struct wfx_vif *wvif, struct hif_ind_scan_cmpl *arg);
void wfx_scan_failed_cb(struct wfx_vif *wvif);

#endif /* WFX_SCAN_H */
