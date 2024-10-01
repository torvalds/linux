/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Scan interface for ST-Ericsson CW1200 mac80211 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 */

#ifndef SCAN_H_INCLUDED
#define SCAN_H_INCLUDED

#include <linux/semaphore.h>
#include "wsm.h"

/* external */ struct sk_buff;
/* external */ struct cfg80211_scan_request;
/* external */ struct ieee80211_channel;
/* external */ struct ieee80211_hw;
/* external */ struct work_struct;

struct cw1200_scan {
	struct semaphore lock;
	struct work_struct work;
	struct delayed_work timeout;
	struct cfg80211_scan_request *req;
	struct ieee80211_channel **begin;
	struct ieee80211_channel **curr;
	struct ieee80211_channel **end;
	struct wsm_ssid ssids[WSM_SCAN_MAX_NUM_OF_SSIDS];
	int output_power;
	int n_ssids;
	int status;
	atomic_t in_progress;
	/* Direct probe requests workaround */
	struct delayed_work probe_work;
	int direct_probe;
};

int cw1200_hw_scan(struct ieee80211_hw *hw,
		   struct ieee80211_vif *vif,
		   struct ieee80211_scan_request *hw_req);
void cw1200_scan_work(struct work_struct *work);
void cw1200_scan_timeout(struct work_struct *work);
void cw1200_clear_recent_scan_work(struct work_struct *work);
void cw1200_scan_complete_cb(struct cw1200_common *priv,
			     struct wsm_scan_complete *arg);
void cw1200_scan_failed_cb(struct cw1200_common *priv);

/* ******************************************************************** */
/* Raw probe requests TX workaround					*/
void cw1200_probe_work(struct work_struct *work);

#endif
