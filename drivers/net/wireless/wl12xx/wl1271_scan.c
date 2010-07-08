/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/ieee80211.h>

#include "wl1271.h"
#include "wl1271_cmd.h"
#include "wl1271_scan.h"
#include "wl1271_acx.h"

int wl1271_scan(struct wl1271 *wl, const u8 *ssid, size_t ssid_len,
		struct cfg80211_scan_request *req, u8 active_scan,
		u8 high_prio, u8 band, u8 probe_requests)
{

	struct wl1271_cmd_trigger_scan_to *trigger = NULL;
	struct wl1271_cmd_scan *params = NULL;
	struct ieee80211_channel *channels;
	u32 rate;
	int i, j, n_ch, ret;
	u16 scan_options = 0;
	u8 ieee_band;

	if (band == WL1271_SCAN_BAND_2_4_GHZ) {
		ieee_band = IEEE80211_BAND_2GHZ;
		rate = wl->conf.tx.basic_rate;
	} else if (band == WL1271_SCAN_BAND_DUAL && wl1271_11a_enabled()) {
		ieee_band = IEEE80211_BAND_2GHZ;
		rate = wl->conf.tx.basic_rate;
	} else if (band == WL1271_SCAN_BAND_5_GHZ && wl1271_11a_enabled()) {
		ieee_band = IEEE80211_BAND_5GHZ;
		rate = wl->conf.tx.basic_rate_5;
	} else
		return -EINVAL;

	if (wl->hw->wiphy->bands[ieee_band]->channels == NULL)
		return -EINVAL;

	channels = wl->hw->wiphy->bands[ieee_band]->channels;
	n_ch = wl->hw->wiphy->bands[ieee_band]->n_channels;

	if (test_bit(WL1271_FLAG_SCANNING, &wl->flags))
		return -EINVAL;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	params->params.rx_config_options = cpu_to_le32(CFG_RX_ALL_GOOD);
	params->params.rx_filter_options =
		cpu_to_le32(CFG_RX_PRSP_EN | CFG_RX_MGMT_EN | CFG_RX_BCN_EN);

	if (!active_scan)
		scan_options |= WL1271_SCAN_OPT_PASSIVE;
	if (high_prio)
		scan_options |= WL1271_SCAN_OPT_PRIORITY_HIGH;
	params->params.scan_options = cpu_to_le16(scan_options);

	params->params.num_probe_requests = probe_requests;
	params->params.tx_rate = cpu_to_le32(rate);
	params->params.tid_trigger = 0;
	params->params.scan_tag = WL1271_SCAN_DEFAULT_TAG;

	if (band == WL1271_SCAN_BAND_DUAL)
		params->params.band = WL1271_SCAN_BAND_2_4_GHZ;
	else
		params->params.band = band;

	for (i = 0, j = 0; i < n_ch && i < WL1271_SCAN_MAX_CHANNELS; i++) {
		if (!(channels[i].flags & IEEE80211_CHAN_DISABLED)) {
			params->channels[j].min_duration =
				cpu_to_le32(WL1271_SCAN_CHAN_MIN_DURATION);
			params->channels[j].max_duration =
				cpu_to_le32(WL1271_SCAN_CHAN_MAX_DURATION);
			memset(&params->channels[j].bssid_lsb, 0xff, 4);
			memset(&params->channels[j].bssid_msb, 0xff, 2);
			params->channels[j].early_termination = 0;
			params->channels[j].tx_power_att =
				WL1271_SCAN_CURRENT_TX_PWR;
			params->channels[j].channel = channels[i].hw_value;
			j++;
		}
	}

	params->params.num_channels = j;

	if (ssid_len && ssid) {
		params->params.ssid_len = ssid_len;
		memcpy(params->params.ssid, ssid, ssid_len);
	}

	ret = wl1271_cmd_build_probe_req(wl, ssid, ssid_len,
					 req->ie, req->ie_len, ieee_band);
	if (ret < 0) {
		wl1271_error("PROBE request template failed");
		goto out;
	}

	trigger = kzalloc(sizeof(*trigger), GFP_KERNEL);
	if (!trigger) {
		ret = -ENOMEM;
		goto out;
	}

	/* disable the timeout */
	trigger->timeout = 0;

	ret = wl1271_cmd_send(wl, CMD_TRIGGER_SCAN_TO, trigger,
			      sizeof(*trigger), 0);
	if (ret < 0) {
		wl1271_error("trigger scan to failed for hw scan");
		goto out;
	}

	wl1271_dump(DEBUG_SCAN, "SCAN: ", params, sizeof(*params));

	set_bit(WL1271_FLAG_SCANNING, &wl->flags);
	if (wl1271_11a_enabled()) {
		wl->scan.state = band;
		if (band == WL1271_SCAN_BAND_DUAL) {
			wl->scan.active = active_scan;
			wl->scan.high_prio = high_prio;
			wl->scan.probe_requests = probe_requests;
			if (ssid_len && ssid) {
				wl->scan.ssid_len = ssid_len;
				memcpy(wl->scan.ssid, ssid, ssid_len);
			} else
				wl->scan.ssid_len = 0;
			wl->scan.req = req;
		} else
			wl->scan.req = NULL;
	}

	ret = wl1271_cmd_send(wl, CMD_SCAN, params, sizeof(*params), 0);
	if (ret < 0) {
		wl1271_error("SCAN failed");
		clear_bit(WL1271_FLAG_SCANNING, &wl->flags);
		goto out;
	}

out:
	kfree(params);
	kfree(trigger);
	return ret;
}

int wl1271_scan_complete(struct wl1271 *wl)
{
	if (test_bit(WL1271_FLAG_SCANNING, &wl->flags)) {
		if (wl->scan.state == WL1271_SCAN_BAND_DUAL) {
			/* 2.4 GHz band scanned, scan 5 GHz band, pretend to
			 * the wl1271_scan function that we are not scanning
			 * as it checks that.
			 */
			clear_bit(WL1271_FLAG_SCANNING, &wl->flags);
			/* FIXME: ie missing! */
			wl1271_scan(wl, wl->scan.ssid, wl->scan.ssid_len,
				    wl->scan.req,
				    wl->scan.active,
				    wl->scan.high_prio,
				    WL1271_SCAN_BAND_5_GHZ,
				    wl->scan.probe_requests);
		} else {
			mutex_unlock(&wl->mutex);
			ieee80211_scan_completed(wl->hw, false);
			mutex_lock(&wl->mutex);
			clear_bit(WL1271_FLAG_SCANNING, &wl->flags);
		}
	}
	return 0;
}
