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
	reinit_completion(&wvif->scan_complete);
	ret = hif_scan(wvif, req, start_idx, i - start_idx);
	if (ret < 0)
		return ret;
	timeout = ret;
	ret = wait_for_completion_timeout(&wvif->scan_complete, timeout);
	if (req->channels[start_idx]->max_power != wvif->wdev->output_power)
		hif_set_output_power(wvif, wvif->wdev->output_power * 10);
	wfx_tx_unlock(wvif->wdev);
	if (!ret) {
		dev_notice(wvif->wdev->dev, "scan timeout\n");
		hif_stop_scan(wvif);
		return -ETIMEDOUT;
	}
	return i - start_idx;
}

int wfx_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_scan_request *hw_req)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	int chan_cur, ret;

	WARN_ON(hw_req->req.n_channels > HIF_API_MAX_NB_CHANNELS);

	if (vif->type == NL80211_IFTYPE_AP)
		return -EOPNOTSUPP;

	if (wvif->state == WFX_STATE_PRE_STA)
		return -EBUSY;

	mutex_lock(&wvif->scan_lock);
	mutex_lock(&wdev->conf_mutex);
	update_probe_tmpl(wvif, &hw_req->req);
	wfx_fwd_probe_req(wvif, true);
	chan_cur = 0;
	do {
		ret = send_scan_req(wvif, &hw_req->req, chan_cur);
		if (ret > 0)
			chan_cur += ret;
	} while (ret > 0 && chan_cur < hw_req->req.n_channels);
	__ieee80211_scan_completed_compat(hw, ret < 0);
	mutex_unlock(&wdev->conf_mutex);
	mutex_unlock(&wvif->scan_lock);
	if (wvif->delayed_unjoin) {
		wvif->delayed_unjoin = false;
		wfx_tx_lock(wdev);
		if (!schedule_work(&wvif->unjoin_work))
			wfx_tx_unlock(wdev);
	} else if (wvif->delayed_link_loss) {
		wvif->delayed_link_loss = false;
		wfx_cqm_bssloss_sm(wvif, 1, 0, 0);
	}
	return 0;
}

void wfx_scan_complete(struct wfx_vif *wvif,
		       const struct hif_ind_scan_cmpl *arg)
{
	complete(&wvif->scan_complete);
}
