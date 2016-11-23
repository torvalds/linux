/*
 * Copyright (c) 2014-2016 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "wil6210.h"
#include "wmi.h"

#define P2P_WILDCARD_SSID "DIRECT-"
#define P2P_DMG_SOCIAL_CHANNEL 2
#define P2P_SEARCH_DURATION_MS 500
#define P2P_DEFAULT_BI 100

bool wil_p2p_is_social_scan(struct cfg80211_scan_request *request)
{
	return (request->n_channels == 1) &&
	       (request->channels[0]->hw_value == P2P_DMG_SOCIAL_CHANNEL);
}

void wil_p2p_discovery_timer_fn(ulong x)
{
	struct wil6210_priv *wil = (void *)x;

	wil_dbg_misc(wil, "%s\n", __func__);

	schedule_work(&wil->p2p.discovery_expired_work);
}

int wil_p2p_search(struct wil6210_priv *wil,
		   struct cfg80211_scan_request *request)
{
	int rc;
	struct wil_p2p_info *p2p = &wil->p2p;

	wil_dbg_misc(wil, "%s: channel %d\n",
		     __func__, P2P_DMG_SOCIAL_CHANNEL);

	mutex_lock(&wil->mutex);

	if (p2p->discovery_started) {
		wil_err(wil, "%s: search failed. discovery already ongoing\n",
			__func__);
		rc = -EBUSY;
		goto out;
	}

	rc = wmi_p2p_cfg(wil, P2P_DMG_SOCIAL_CHANNEL, P2P_DEFAULT_BI);
	if (rc) {
		wil_err(wil, "%s: wmi_p2p_cfg failed\n", __func__);
		goto out;
	}

	rc = wmi_set_ssid(wil, strlen(P2P_WILDCARD_SSID), P2P_WILDCARD_SSID);
	if (rc) {
		wil_err(wil, "%s: wmi_set_ssid failed\n", __func__);
		goto out_stop;
	}

	/* Set application IE to probe request and probe response */
	rc = wmi_set_ie(wil, WMI_FRAME_PROBE_REQ,
			request->ie_len, request->ie);
	if (rc) {
		wil_err(wil, "%s: wmi_set_ie(WMI_FRAME_PROBE_REQ) failed\n",
			__func__);
		goto out_stop;
	}

	/* supplicant doesn't provide Probe Response IEs. As a workaround -
	 * re-use Probe Request IEs
	 */
	rc = wmi_set_ie(wil, WMI_FRAME_PROBE_RESP,
			request->ie_len, request->ie);
	if (rc) {
		wil_err(wil, "%s: wmi_set_ie(WMI_FRAME_PROBE_RESP) failed\n",
			__func__);
		goto out_stop;
	}

	rc = wmi_start_search(wil);
	if (rc) {
		wil_err(wil, "%s: wmi_start_search failed\n", __func__);
		goto out_stop;
	}

	p2p->discovery_started = 1;
	INIT_WORK(&p2p->discovery_expired_work, wil_p2p_search_expired);
	mod_timer(&p2p->discovery_timer,
		  jiffies + msecs_to_jiffies(P2P_SEARCH_DURATION_MS));

out_stop:
	if (rc)
		wmi_stop_discovery(wil);

out:
	mutex_unlock(&wil->mutex);
	return rc;
}

int wil_p2p_listen(struct wil6210_priv *wil, unsigned int duration,
		   struct ieee80211_channel *chan, u64 *cookie)
{
	struct wil_p2p_info *p2p = &wil->p2p;
	u8 channel = P2P_DMG_SOCIAL_CHANNEL;
	int rc;

	if (!chan)
		return -EINVAL;

	channel = chan->hw_value;

	wil_dbg_misc(wil, "%s: duration %d\n", __func__, duration);

	mutex_lock(&wil->mutex);

	if (p2p->discovery_started) {
		wil_err(wil, "%s: discovery already ongoing\n", __func__);
		rc = -EBUSY;
		goto out;
	}

	rc = wmi_p2p_cfg(wil, channel, P2P_DEFAULT_BI);
	if (rc) {
		wil_err(wil, "%s: wmi_p2p_cfg failed\n", __func__);
		goto out;
	}

	rc = wmi_set_ssid(wil, strlen(P2P_WILDCARD_SSID), P2P_WILDCARD_SSID);
	if (rc) {
		wil_err(wil, "%s: wmi_set_ssid failed\n", __func__);
		goto out_stop;
	}

	rc = wmi_start_listen(wil);
	if (rc) {
		wil_err(wil, "%s: wmi_start_listen failed\n", __func__);
		goto out_stop;
	}

	memcpy(&p2p->listen_chan, chan, sizeof(*chan));
	*cookie = ++p2p->cookie;

	p2p->discovery_started = 1;
	INIT_WORK(&p2p->discovery_expired_work, wil_p2p_listen_expired);
	mod_timer(&p2p->discovery_timer,
		  jiffies + msecs_to_jiffies(duration));

out_stop:
	if (rc)
		wmi_stop_discovery(wil);

out:
	mutex_unlock(&wil->mutex);
	return rc;
}

u8 wil_p2p_stop_discovery(struct wil6210_priv *wil)
{
	struct wil_p2p_info *p2p = &wil->p2p;
	u8 started = p2p->discovery_started;

	if (p2p->discovery_started) {
		del_timer_sync(&p2p->discovery_timer);
		p2p->discovery_started = 0;
		wmi_stop_discovery(wil);
	}

	return started;
}

int wil_p2p_cancel_listen(struct wil6210_priv *wil, u64 cookie)
{
	struct wil_p2p_info *p2p = &wil->p2p;
	u8 started;

	mutex_lock(&wil->mutex);

	if (cookie != p2p->cookie) {
		wil_info(wil, "%s: Cookie mismatch: 0x%016llx vs. 0x%016llx\n",
			 __func__, p2p->cookie, cookie);
		mutex_unlock(&wil->mutex);
		return -ENOENT;
	}

	started = wil_p2p_stop_discovery(wil);

	mutex_unlock(&wil->mutex);

	if (!started) {
		wil_err(wil, "%s: listen not started\n", __func__);
		return -ENOENT;
	}

	mutex_lock(&wil->p2p_wdev_mutex);
	cfg80211_remain_on_channel_expired(wil->radio_wdev,
					   p2p->cookie,
					   &p2p->listen_chan,
					   GFP_KERNEL);
	wil->radio_wdev = wil->wdev;
	mutex_unlock(&wil->p2p_wdev_mutex);
	return 0;
}

void wil_p2p_listen_expired(struct work_struct *work)
{
	struct wil_p2p_info *p2p = container_of(work,
			struct wil_p2p_info, discovery_expired_work);
	struct wil6210_priv *wil = container_of(p2p,
			struct wil6210_priv, p2p);
	u8 started;

	wil_dbg_misc(wil, "%s()\n", __func__);

	mutex_lock(&wil->mutex);
	started = wil_p2p_stop_discovery(wil);
	mutex_unlock(&wil->mutex);

	if (started) {
		mutex_lock(&wil->p2p_wdev_mutex);
		cfg80211_remain_on_channel_expired(wil->radio_wdev,
						   p2p->cookie,
						   &p2p->listen_chan,
						   GFP_KERNEL);
		wil->radio_wdev = wil->wdev;
		mutex_unlock(&wil->p2p_wdev_mutex);
	}

}

void wil_p2p_search_expired(struct work_struct *work)
{
	struct wil_p2p_info *p2p = container_of(work,
			struct wil_p2p_info, discovery_expired_work);
	struct wil6210_priv *wil = container_of(p2p,
			struct wil6210_priv, p2p);
	u8 started;

	wil_dbg_misc(wil, "%s()\n", __func__);

	mutex_lock(&wil->mutex);
	started = wil_p2p_stop_discovery(wil);
	mutex_unlock(&wil->mutex);

	if (started) {
		struct cfg80211_scan_info info = {
			.aborted = false,
		};

		mutex_lock(&wil->p2p_wdev_mutex);
		cfg80211_scan_done(wil->scan_request, &info);
		wil->scan_request = NULL;
		wil->radio_wdev = wil->wdev;
		mutex_unlock(&wil->p2p_wdev_mutex);
	}
}

void wil_p2p_stop_radio_operations(struct wil6210_priv *wil)
{
	struct wil_p2p_info *p2p = &wil->p2p;
	struct cfg80211_scan_info info = {
		.aborted = true,
	};

	lockdep_assert_held(&wil->mutex);
	lockdep_assert_held(&wil->p2p_wdev_mutex);

	if (wil->radio_wdev != wil->p2p_wdev)
		goto out;

	if (!p2p->discovery_started) {
		/* Regular scan on the p2p device */
		if (wil->scan_request &&
		    wil->scan_request->wdev == wil->p2p_wdev)
			wil_abort_scan(wil, true);
		goto out;
	}

	/* Search or listen on p2p device */
	mutex_unlock(&wil->p2p_wdev_mutex);
	wil_p2p_stop_discovery(wil);
	mutex_lock(&wil->p2p_wdev_mutex);

	if (wil->scan_request) {
		/* search */
		cfg80211_scan_done(wil->scan_request, &info);
		wil->scan_request = NULL;
	} else {
		/* listen */
		cfg80211_remain_on_channel_expired(wil->radio_wdev,
						   p2p->cookie,
						   &p2p->listen_chan,
						   GFP_KERNEL);
	}

out:
	wil->radio_wdev = wil->wdev;
}
