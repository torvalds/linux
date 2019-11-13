// SPDX-License-Identifier: GPL-2.0-only
/*
 * Scan related functions.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <net/mac80211.h>

#include "scan.h"
#include "wfx.h"
#include "sta.h"
#include "hif_tx_mib.h"

static void __ieee80211_scan_completed_compat(struct ieee80211_hw *hw,
					      bool aborted)
{
	struct cfg80211_scan_info info = {
		.aborted = aborted ? 1 : 0,
	};

	ieee80211_scan_completed(hw, &info);
}

static void wfx_scan_restart_delayed(struct wfx_vif *wvif)
{
	if (wvif->delayed_unjoin) {
		wvif->delayed_unjoin = false;
		if (!schedule_work(&wvif->unjoin_work))
			wfx_tx_unlock(wvif->wdev);
	} else if (wvif->delayed_link_loss) {
		wvif->delayed_link_loss = 0;
		wfx_cqm_bssloss_sm(wvif, 1, 0, 0);
	}
}

static int wfx_scan_start(struct wfx_vif *wvif, struct wfx_scan_params *scan)
{
	int ret;
	int tmo = 500;

	if (wvif->state == WFX_STATE_PRE_STA)
		return -EBUSY;

	tmo += scan->scan_req.num_of_channels *
	       ((20 * (scan->scan_req.max_channel_time)) + 10);
	atomic_set(&wvif->scan.in_progress, 1);
	atomic_set(&wvif->wdev->scan_in_progress, 1);

	schedule_delayed_work(&wvif->scan.timeout, msecs_to_jiffies(tmo));
	ret = hif_scan(wvif, scan);
	if (ret) {
		wfx_scan_failed_cb(wvif);
		atomic_set(&wvif->scan.in_progress, 0);
		atomic_set(&wvif->wdev->scan_in_progress, 0);
		cancel_delayed_work_sync(&wvif->scan.timeout);
		wfx_scan_restart_delayed(wvif);
	}
	return ret;
}

int wfx_hw_scan(struct ieee80211_hw *hw,
		   struct ieee80211_vif *vif,
		   struct ieee80211_scan_request *hw_req)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	struct cfg80211_scan_request *req = &hw_req->req;
	struct sk_buff *skb;
	int i, ret;
	struct hif_mib_template_frame *p;

	if (!wvif)
		return -EINVAL;

	if (wvif->state == WFX_STATE_AP)
		return -EOPNOTSUPP;

	if (req->n_ssids == 1 && !req->ssids[0].ssid_len)
		req->n_ssids = 0;

	if (req->n_ssids > HIF_API_MAX_NB_SSIDS)
		return -EINVAL;

	skb = ieee80211_probereq_get(hw, wvif->vif->addr, NULL, 0, req->ie_len);
	if (!skb)
		return -ENOMEM;

	if (req->ie_len)
		memcpy(skb_put(skb, req->ie_len), req->ie, req->ie_len);

	mutex_lock(&wdev->conf_mutex);

	p = (struct hif_mib_template_frame *)skb_push(skb, 4);
	p->frame_type = HIF_TMPLT_PRBREQ;
	p->frame_length = cpu_to_le16(skb->len - 4);
	ret = hif_set_template_frame(wvif, p);
	skb_pull(skb, 4);

	if (!ret)
		/* Host want to be the probe responder. */
		ret = wfx_fwd_probe_req(wvif, true);
	if (ret) {
		mutex_unlock(&wdev->conf_mutex);
		dev_kfree_skb(skb);
		return ret;
	}

	wfx_tx_lock_flush(wdev);

	WARN(wvif->scan.req, "unexpected concurrent scan");
	wvif->scan.req = req;
	wvif->scan.n_ssids = 0;
	wvif->scan.status = 0;
	wvif->scan.begin = &req->channels[0];
	wvif->scan.curr = wvif->scan.begin;
	wvif->scan.end = &req->channels[req->n_channels];
	wvif->scan.output_power = wdev->output_power;

	for (i = 0; i < req->n_ssids; ++i) {
		struct hif_ssid_def *dst = &wvif->scan.ssids[wvif->scan.n_ssids];

		memcpy(&dst->ssid[0], req->ssids[i].ssid, sizeof(dst->ssid));
		dst->ssid_length = req->ssids[i].ssid_len;
		++wvif->scan.n_ssids;
	}

	mutex_unlock(&wdev->conf_mutex);

	if (skb)
		dev_kfree_skb(skb);
	schedule_work(&wvif->scan.work);
	return 0;
}

void wfx_scan_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif, scan.work);
	struct ieee80211_channel **it;
	struct wfx_scan_params scan = {
		.scan_req.scan_type.type = 0,    /* Foreground */
	};
	struct ieee80211_channel *first;
	bool first_run = (wvif->scan.begin == wvif->scan.curr &&
			  wvif->scan.begin != wvif->scan.end);
	int i;

	down(&wvif->scan.lock);
	mutex_lock(&wvif->wdev->conf_mutex);

	if (first_run) {
		if (wvif->state == WFX_STATE_STA &&
		    !(wvif->powersave_mode.pm_mode.enter_psm)) {
			struct hif_req_set_pm_mode pm = wvif->powersave_mode;

			pm.pm_mode.enter_psm = 1;
			wfx_set_pm(wvif, &pm);
		}
	}

	if (!wvif->scan.req || wvif->scan.curr == wvif->scan.end) {
		if (wvif->scan.output_power != wvif->wdev->output_power)
			hif_set_output_power(wvif,
					     wvif->wdev->output_power * 10);

		if (wvif->scan.status < 0)
			dev_warn(wvif->wdev->dev, "scan failed\n");
		else if (wvif->scan.req)
			dev_dbg(wvif->wdev->dev, "scan completed\n");
		else
			dev_dbg(wvif->wdev->dev, "scan canceled\n");

		wvif->scan.req = NULL;
		wfx_scan_restart_delayed(wvif);
		wfx_tx_unlock(wvif->wdev);
		mutex_unlock(&wvif->wdev->conf_mutex);
		__ieee80211_scan_completed_compat(wvif->wdev->hw,
						  wvif->scan.status ? 1 : 0);
		up(&wvif->scan.lock);
		if (wvif->state == WFX_STATE_STA &&
		    !(wvif->powersave_mode.pm_mode.enter_psm))
			wfx_set_pm(wvif, &wvif->powersave_mode);
		return;
	}
	first = *wvif->scan.curr;

	for (it = wvif->scan.curr + 1, i = 1;
	     it != wvif->scan.end && i < HIF_API_MAX_NB_CHANNELS;
	     ++it, ++i) {
		if ((*it)->band != first->band)
			break;
		if (((*it)->flags ^ first->flags) &
				IEEE80211_CHAN_NO_IR)
			break;
		if (!(first->flags & IEEE80211_CHAN_NO_IR) &&
		    (*it)->max_power != first->max_power)
			break;
	}
	scan.scan_req.band = first->band;

	if (wvif->scan.req->no_cck)
		scan.scan_req.max_transmit_rate = API_RATE_INDEX_G_6MBPS;
	else
		scan.scan_req.max_transmit_rate = API_RATE_INDEX_B_1MBPS;
	scan.scan_req.num_of_probe_requests =
		(first->flags & IEEE80211_CHAN_NO_IR) ? 0 : 2;
	scan.scan_req.num_of_ssi_ds = wvif->scan.n_ssids;
	scan.ssids = &wvif->scan.ssids[0];
	scan.scan_req.num_of_channels = it - wvif->scan.curr;
	scan.scan_req.probe_delay = 100;
	// FIXME: Check if FW can do active scan while joined.
	if (wvif->state == WFX_STATE_STA) {
		scan.scan_req.scan_type.type = 1;
		scan.scan_req.scan_flags.fbg = 1;
	}

	scan.ch = kcalloc(scan.scan_req.num_of_channels,
			  sizeof(u8), GFP_KERNEL);

	if (!scan.ch) {
		wvif->scan.status = -ENOMEM;
		goto fail;
	}
	for (i = 0; i < scan.scan_req.num_of_channels; ++i)
		scan.ch[i] = wvif->scan.curr[i]->hw_value;

	if (wvif->scan.curr[0]->flags & IEEE80211_CHAN_NO_IR) {
		scan.scan_req.min_channel_time = 50;
		scan.scan_req.max_channel_time = 150;
	} else {
		scan.scan_req.min_channel_time = 10;
		scan.scan_req.max_channel_time = 50;
	}
	if (!(first->flags & IEEE80211_CHAN_NO_IR) &&
	    wvif->scan.output_power != first->max_power) {
		wvif->scan.output_power = first->max_power;
		hif_set_output_power(wvif, wvif->scan.output_power * 10);
	}
	wvif->scan.status = wfx_scan_start(wvif, &scan);
	kfree(scan.ch);
	if (wvif->scan.status)
		goto fail;
	wvif->scan.curr = it;
	mutex_unlock(&wvif->wdev->conf_mutex);
	return;

fail:
	wvif->scan.curr = wvif->scan.end;
	mutex_unlock(&wvif->wdev->conf_mutex);
	up(&wvif->scan.lock);
	schedule_work(&wvif->scan.work);
}

static void wfx_scan_complete(struct wfx_vif *wvif)
{
	up(&wvif->scan.lock);
	atomic_set(&wvif->wdev->scan_in_progress, 0);

	wfx_scan_work(&wvif->scan.work);
}

void wfx_scan_failed_cb(struct wfx_vif *wvif)
{
	if (cancel_delayed_work_sync(&wvif->scan.timeout) > 0) {
		wvif->scan.status = -EIO;
		schedule_work(&wvif->scan.timeout.work);
	}
}

void wfx_scan_complete_cb(struct wfx_vif *wvif, struct hif_ind_scan_cmpl *arg)
{
	if (cancel_delayed_work_sync(&wvif->scan.timeout) > 0) {
		wvif->scan.status = 1;
		schedule_work(&wvif->scan.timeout.work);
	}
}

void wfx_scan_timeout(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif,
					    scan.timeout.work);

	if (atomic_xchg(&wvif->scan.in_progress, 0)) {
		if (wvif->scan.status > 0) {
			wvif->scan.status = 0;
		} else if (!wvif->scan.status) {
			dev_warn(wvif->wdev->dev, "timeout waiting for scan complete notification\n");
			wvif->scan.status = -ETIMEDOUT;
			wvif->scan.curr = wvif->scan.end;
			hif_stop_scan(wvif);
		}
		wfx_scan_complete(wvif);
	}
}
