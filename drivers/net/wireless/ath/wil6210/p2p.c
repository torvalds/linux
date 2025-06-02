// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include "wil6210.h"
#include "wmi.h"

#define P2P_WILDCARD_SSID "DIRECT-"
#define P2P_DMG_SOCIAL_CHANNEL 2
#define P2P_SEARCH_DURATION_MS 500
#define P2P_DEFAULT_BI 100

static int wil_p2p_start_listen(struct wil6210_vif *vif)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wil_p2p_info *p2p = &vif->p2p;
	u8 channel = p2p->listen_chan.hw_value;
	int rc;

	lockdep_assert_held(&wil->mutex);

	rc = wmi_p2p_cfg(vif, channel, P2P_DEFAULT_BI);
	if (rc) {
		wil_err(wil, "wmi_p2p_cfg failed\n");
		goto out;
	}

	rc = wmi_set_ssid(vif, strlen(P2P_WILDCARD_SSID), P2P_WILDCARD_SSID);
	if (rc) {
		wil_err(wil, "wmi_set_ssid failed\n");
		goto out_stop;
	}

	rc = wmi_start_listen(vif);
	if (rc) {
		wil_err(wil, "wmi_start_listen failed\n");
		goto out_stop;
	}

	INIT_WORK(&p2p->discovery_expired_work, wil_p2p_listen_expired);
	mod_timer(&p2p->discovery_timer,
		  jiffies + msecs_to_jiffies(p2p->listen_duration));
out_stop:
	if (rc)
		wmi_stop_discovery(vif);

out:
	return rc;
}

bool wil_p2p_is_social_scan(struct cfg80211_scan_request *request)
{
	return (request->n_channels == 1) &&
	       (request->channels[0]->hw_value == P2P_DMG_SOCIAL_CHANNEL);
}

int wil_p2p_search(struct wil6210_vif *vif,
		   struct cfg80211_scan_request *request)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;
	struct wil_p2p_info *p2p = &vif->p2p;

	wil_dbg_misc(wil, "p2p_search: channel %d\n", P2P_DMG_SOCIAL_CHANNEL);

	lockdep_assert_held(&wil->mutex);

	if (p2p->discovery_started) {
		wil_err(wil, "search failed. discovery already ongoing\n");
		rc = -EBUSY;
		goto out;
	}

	rc = wmi_p2p_cfg(vif, P2P_DMG_SOCIAL_CHANNEL, P2P_DEFAULT_BI);
	if (rc) {
		wil_err(wil, "wmi_p2p_cfg failed\n");
		goto out;
	}

	rc = wmi_set_ssid(vif, strlen(P2P_WILDCARD_SSID), P2P_WILDCARD_SSID);
	if (rc) {
		wil_err(wil, "wmi_set_ssid failed\n");
		goto out_stop;
	}

	/* Set application IE to probe request and probe response */
	rc = wmi_set_ie(vif, WMI_FRAME_PROBE_REQ,
			request->ie_len, request->ie);
	if (rc) {
		wil_err(wil, "wmi_set_ie(WMI_FRAME_PROBE_REQ) failed\n");
		goto out_stop;
	}

	/* supplicant doesn't provide Probe Response IEs. As a workaround -
	 * re-use Probe Request IEs
	 */
	rc = wmi_set_ie(vif, WMI_FRAME_PROBE_RESP,
			request->ie_len, request->ie);
	if (rc) {
		wil_err(wil, "wmi_set_ie(WMI_FRAME_PROBE_RESP) failed\n");
		goto out_stop;
	}

	rc = wmi_start_search(vif);
	if (rc) {
		wil_err(wil, "wmi_start_search failed\n");
		goto out_stop;
	}

	p2p->discovery_started = 1;
	INIT_WORK(&p2p->discovery_expired_work, wil_p2p_search_expired);
	mod_timer(&p2p->discovery_timer,
		  jiffies + msecs_to_jiffies(P2P_SEARCH_DURATION_MS));

out_stop:
	if (rc)
		wmi_stop_discovery(vif);

out:
	return rc;
}

int wil_p2p_listen(struct wil6210_priv *wil, struct wireless_dev *wdev,
		   unsigned int duration, struct ieee80211_channel *chan,
		   u64 *cookie)
{
	struct wil6210_vif *vif = wdev_to_vif(wil, wdev);
	struct wil_p2p_info *p2p = &vif->p2p;
	int rc;

	if (!chan)
		return -EINVAL;

	wil_dbg_misc(wil, "p2p_listen: duration %d\n", duration);

	mutex_lock(&wil->mutex);

	if (p2p->discovery_started) {
		wil_err(wil, "discovery already ongoing\n");
		rc = -EBUSY;
		goto out;
	}

	memcpy(&p2p->listen_chan, chan, sizeof(*chan));
	*cookie = ++p2p->cookie;
	p2p->listen_duration = duration;

	mutex_lock(&wil->vif_mutex);
	if (vif->scan_request) {
		wil_dbg_misc(wil, "Delaying p2p listen until scan done\n");
		p2p->pending_listen_wdev = wdev;
		p2p->discovery_started = 1;
		rc = 0;
		mutex_unlock(&wil->vif_mutex);
		goto out;
	}
	mutex_unlock(&wil->vif_mutex);

	rc = wil_p2p_start_listen(vif);
	if (rc)
		goto out;

	p2p->discovery_started = 1;
	if (vif->mid == 0)
		wil->radio_wdev = wdev;

	cfg80211_ready_on_channel(wdev, *cookie, chan, duration,
				  GFP_KERNEL);

out:
	mutex_unlock(&wil->mutex);
	return rc;
}

u8 wil_p2p_stop_discovery(struct wil6210_vif *vif)
{
	struct wil_p2p_info *p2p = &vif->p2p;
	u8 started = p2p->discovery_started;

	if (p2p->discovery_started) {
		if (p2p->pending_listen_wdev) {
			/* discovery not really started, only pending */
			p2p->pending_listen_wdev = NULL;
		} else {
			timer_delete_sync(&p2p->discovery_timer);
			wmi_stop_discovery(vif);
		}
		p2p->discovery_started = 0;
	}

	return started;
}

int wil_p2p_cancel_listen(struct wil6210_vif *vif, u64 cookie)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wil_p2p_info *p2p = &vif->p2p;
	u8 started;

	mutex_lock(&wil->mutex);

	if (cookie != p2p->cookie) {
		wil_info(wil, "Cookie mismatch: 0x%016llx vs. 0x%016llx\n",
			 p2p->cookie, cookie);
		mutex_unlock(&wil->mutex);
		return -ENOENT;
	}

	started = wil_p2p_stop_discovery(vif);

	mutex_unlock(&wil->mutex);

	if (!started) {
		wil_err(wil, "listen not started\n");
		return -ENOENT;
	}

	mutex_lock(&wil->vif_mutex);
	cfg80211_remain_on_channel_expired(vif_to_radio_wdev(wil, vif),
					   p2p->cookie,
					   &p2p->listen_chan,
					   GFP_KERNEL);
	if (vif->mid == 0)
		wil->radio_wdev = wil->main_ndev->ieee80211_ptr;
	mutex_unlock(&wil->vif_mutex);
	return 0;
}

void wil_p2p_listen_expired(struct work_struct *work)
{
	struct wil_p2p_info *p2p = container_of(work,
			struct wil_p2p_info, discovery_expired_work);
	struct wil6210_vif *vif = container_of(p2p,
			struct wil6210_vif, p2p);
	struct wil6210_priv *wil = vif_to_wil(vif);
	u8 started;

	wil_dbg_misc(wil, "p2p_listen_expired\n");

	mutex_lock(&wil->mutex);
	started = wil_p2p_stop_discovery(vif);
	mutex_unlock(&wil->mutex);

	if (!started)
		return;

	mutex_lock(&wil->vif_mutex);
	cfg80211_remain_on_channel_expired(vif_to_radio_wdev(wil, vif),
					   p2p->cookie,
					   &p2p->listen_chan,
					   GFP_KERNEL);
	if (vif->mid == 0)
		wil->radio_wdev = wil->main_ndev->ieee80211_ptr;
	mutex_unlock(&wil->vif_mutex);
}

void wil_p2p_search_expired(struct work_struct *work)
{
	struct wil_p2p_info *p2p = container_of(work,
			struct wil_p2p_info, discovery_expired_work);
	struct wil6210_vif *vif = container_of(p2p,
			struct wil6210_vif, p2p);
	struct wil6210_priv *wil = vif_to_wil(vif);
	u8 started;

	wil_dbg_misc(wil, "p2p_search_expired\n");

	mutex_lock(&wil->mutex);
	started = wil_p2p_stop_discovery(vif);
	mutex_unlock(&wil->mutex);

	if (started) {
		struct cfg80211_scan_info info = {
			.aborted = false,
		};

		mutex_lock(&wil->vif_mutex);
		if (vif->scan_request) {
			cfg80211_scan_done(vif->scan_request, &info);
			vif->scan_request = NULL;
			if (vif->mid == 0)
				wil->radio_wdev =
					wil->main_ndev->ieee80211_ptr;
		}
		mutex_unlock(&wil->vif_mutex);
	}
}

void wil_p2p_delayed_listen_work(struct work_struct *work)
{
	struct wil_p2p_info *p2p = container_of(work,
			struct wil_p2p_info, delayed_listen_work);
	struct wil6210_vif *vif = container_of(p2p,
			struct wil6210_vif, p2p);
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;

	mutex_lock(&wil->mutex);

	wil_dbg_misc(wil, "Checking delayed p2p listen\n");
	if (!p2p->discovery_started || !p2p->pending_listen_wdev)
		goto out;

	mutex_lock(&wil->vif_mutex);
	if (vif->scan_request) {
		/* another scan started, wait again... */
		mutex_unlock(&wil->vif_mutex);
		goto out;
	}
	mutex_unlock(&wil->vif_mutex);

	rc = wil_p2p_start_listen(vif);

	mutex_lock(&wil->vif_mutex);
	if (rc) {
		cfg80211_remain_on_channel_expired(p2p->pending_listen_wdev,
						   p2p->cookie,
						   &p2p->listen_chan,
						   GFP_KERNEL);
		if (vif->mid == 0)
			wil->radio_wdev = wil->main_ndev->ieee80211_ptr;
	} else {
		cfg80211_ready_on_channel(p2p->pending_listen_wdev, p2p->cookie,
					  &p2p->listen_chan,
					  p2p->listen_duration, GFP_KERNEL);
		if (vif->mid == 0)
			wil->radio_wdev = p2p->pending_listen_wdev;
	}
	p2p->pending_listen_wdev = NULL;
	mutex_unlock(&wil->vif_mutex);

out:
	mutex_unlock(&wil->mutex);
}

void wil_p2p_stop_radio_operations(struct wil6210_priv *wil)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	struct wil_p2p_info *p2p = &vif->p2p;
	struct cfg80211_scan_info info = {
		.aborted = true,
	};

	lockdep_assert_held(&wil->mutex);
	lockdep_assert_held(&wil->vif_mutex);

	if (wil->radio_wdev != wil->p2p_wdev)
		goto out;

	if (!p2p->discovery_started) {
		/* Regular scan on the p2p device */
		if (vif->scan_request &&
		    vif->scan_request->wdev == wil->p2p_wdev)
			wil_abort_scan(vif, true);
		goto out;
	}

	/* Search or listen on p2p device */
	mutex_unlock(&wil->vif_mutex);
	wil_p2p_stop_discovery(vif);
	mutex_lock(&wil->vif_mutex);

	if (vif->scan_request) {
		/* search */
		cfg80211_scan_done(vif->scan_request, &info);
		vif->scan_request = NULL;
	} else {
		/* listen */
		cfg80211_remain_on_channel_expired(wil->radio_wdev,
						   p2p->cookie,
						   &p2p->listen_chan,
						   GFP_KERNEL);
	}

out:
	wil->radio_wdev = wil->main_ndev->ieee80211_ptr;
}
