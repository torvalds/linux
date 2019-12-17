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
		.aborted = aborted,
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

static int wfx_scan_start(struct wfx_vif *wvif,
			  int chan_start_idx, int chan_num)
{
	int tmo;

	if (wvif->state == WFX_STATE_PRE_STA)
		return -EBUSY;

	atomic_set(&wvif->scan.in_progress, 1);

	tmo = hif_scan(wvif, wvif->scan.req, chan_start_idx, chan_num);
	schedule_delayed_work(&wvif->scan.timeout, tmo);
	return 0;
}

static int update_probe_tmpl(struct wfx_vif *wvif,
			     struct cfg80211_scan_request *req)
{
	struct sk_buff *skb;

	skb = ieee80211_probereq_get(wvif->wdev->hw, wvif->vif->addr,
				     NULL, 0, req->ie_len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, req->ie, req->ie_len);
	hif_set_template_frame(wvif, skb, HIF_TMPLT_PRBREQ, 0);
	dev_kfree_skb(skb);
	return 0;
}

int wfx_hw_scan(struct ieee80211_hw *hw,
		   struct ieee80211_vif *vif,
		   struct ieee80211_scan_request *hw_req)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	struct cfg80211_scan_request *req = &hw_req->req;
	int i, ret;

	if (!wvif)
		return -EINVAL;

	if (wvif->state == WFX_STATE_AP)
		return -EOPNOTSUPP;

	if (req->n_ssids == 1 && !req->ssids[0].ssid_len)
		req->n_ssids = 0;

	if (req->n_ssids > HIF_API_MAX_NB_SSIDS)
		return -EINVAL;

	mutex_lock(&wdev->conf_mutex);

	ret = update_probe_tmpl(wvif, req);
	if (ret)
		goto failed;

	ret = wfx_fwd_probe_req(wvif, true);
	if (ret)
		goto failed;

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
	schedule_work(&wvif->scan.work);

failed:
	mutex_unlock(&wdev->conf_mutex);
	return ret;
}

void wfx_scan_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif, scan.work);
	struct ieee80211_channel **it;
	struct ieee80211_channel *first;
	int i;

	down(&wvif->scan.lock);
	mutex_lock(&wvif->wdev->conf_mutex);


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
	if (!(first->flags & IEEE80211_CHAN_NO_IR) &&
	    wvif->scan.output_power != first->max_power) {
		wvif->scan.output_power = first->max_power;
		hif_set_output_power(wvif, wvif->scan.output_power * 10);
	}
	wvif->scan.status = wfx_scan_start(wvif,
					   wvif->scan.curr - wvif->scan.begin,
					   it - wvif->scan.curr);
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

void wfx_scan_complete_cb(struct wfx_vif *wvif,
			  const struct hif_ind_scan_cmpl *arg)
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
		up(&wvif->scan.lock);
		wfx_scan_work(&wvif->scan.work);
	}
}
