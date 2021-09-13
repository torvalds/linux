// SPDX-License-Identifier: GPL-2.0-only
/*
 * Scan related functions.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
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

static int send_scan_req(struct wfx_vif *wvif,
			 struct cfg80211_scan_request *req, int start_idx)
{
	int i, ret, timeout;
	struct ieee80211_channel *ch_start, *ch_cur;

	for (i = start_idx; i < req->n_channels; i++) {
		ch_start = req->channels[start_idx];
		ch_cur = req->channels[i];
		WARN(ch_cur->band != NL80211_BAND_2GHZ, "band not supported");
		if (ch_cur->max_power != ch_start->max_power)
			break;
		if ((ch_cur->flags ^ ch_start->flags) & IEEE80211_CHAN_NO_IR)
			break;
	}
	wfx_tx_lock_flush(wvif->wdev);
	wvif->scan_abort = false;
	reinit_completion(&wvif->scan_complete);
	ret = hif_scan(wvif, req, start_idx, i - start_idx, &timeout);
	if (ret) {
		ret = -EIO;
		goto err_scan_start;
	}
	ret = wait_for_completion_timeout(&wvif->scan_complete, timeout);
	if (!ret) {
		dev_notice(wvif->wdev->dev, "scan timeout\n");
		hif_stop_scan(wvif);
		ret = wait_for_completion_timeout(&wvif->scan_complete, 1 * HZ);
		if (!ret)
			dev_err(wvif->wdev->dev, "scan didn't stop\n");
		ret = -ETIMEDOUT;
		goto err_timeout;
	}
	if (wvif->scan_abort) {
		dev_notice(wvif->wdev->dev, "scan abort\n");
		ret = -ECONNABORTED;
		goto err_timeout;
	}
	ret = i - start_idx;
err_timeout:
	if (req->channels[start_idx]->max_power != wvif->vif->bss_conf.txpower)
		hif_set_output_power(wvif, wvif->vif->bss_conf.txpower);
err_scan_start:
	wfx_tx_unlock(wvif->wdev);
	return ret;
}

/*
 * It is not really necessary to run scan request asynchronously. However,
 * there is a bug in "iw scan" when ieee80211_scan_completed() is called before
 * wfx_hw_scan() return
 */
void wfx_hw_scan_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif, scan_work);
	struct ieee80211_scan_request *hw_req = wvif->scan_req;
	int chan_cur, ret;

	mutex_lock(&wvif->wdev->conf_mutex);
	mutex_lock(&wvif->scan_lock);
	if (wvif->join_in_progress) {
		dev_info(wvif->wdev->dev, "%s: abort in-progress REQ_JOIN",
			 __func__);
		wfx_reset(wvif);
	}
	update_probe_tmpl(wvif, &hw_req->req);
	chan_cur = 0;
	do {
		ret = send_scan_req(wvif, &hw_req->req, chan_cur);
		if (ret > 0)
			chan_cur += ret;
	} while (ret > 0 && chan_cur < hw_req->req.n_channels);
	mutex_unlock(&wvif->scan_lock);
	mutex_unlock(&wvif->wdev->conf_mutex);
	__ieee80211_scan_completed_compat(wvif->wdev->hw, ret < 0);
}

int wfx_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_scan_request *hw_req)
{
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;

	WARN_ON(hw_req->req.n_channels > HIF_API_MAX_NB_CHANNELS);
	wvif->scan_req = hw_req;
	schedule_work(&wvif->scan_work);
	return 0;
}

void wfx_cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;

	wvif->scan_abort = true;
	hif_stop_scan(wvif);
}

void wfx_scan_complete(struct wfx_vif *wvif)
{
	complete(&wvif->scan_complete);
}
