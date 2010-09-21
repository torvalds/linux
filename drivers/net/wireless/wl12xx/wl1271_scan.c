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

void wl1271_scan_complete_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1271 *wl;

	dwork = container_of(work, struct delayed_work, work);
	wl = container_of(dwork, struct wl1271, scan_complete_work);

	wl1271_debug(DEBUG_SCAN, "Scanning complete");

	mutex_lock(&wl->mutex);

	if (wl->scan.state == WL1271_SCAN_STATE_IDLE) {
		mutex_unlock(&wl->mutex);
		return;
	}

	wl->scan.state = WL1271_SCAN_STATE_IDLE;
	kfree(wl->scan.scanned_ch);
	wl->scan.scanned_ch = NULL;
	mutex_unlock(&wl->mutex);

	ieee80211_scan_completed(wl->hw, false);

	if (wl->scan.failed) {
		wl1271_info("Scan completed due to error.");
		ieee80211_queue_work(wl->hw, &wl->recovery_work);
	}
}


static int wl1271_get_scan_channels(struct wl1271 *wl,
				    struct cfg80211_scan_request *req,
				    struct basic_scan_channel_params *channels,
				    enum ieee80211_band band, bool passive)
{
	int i, j;
	u32 flags;

	for (i = 0, j = 0;
	     i < req->n_channels && j < WL1271_SCAN_MAX_CHANNELS;
	     i++) {

		flags = req->channels[i]->flags;

		if (!wl->scan.scanned_ch[i] &&
		    !(flags & IEEE80211_CHAN_DISABLED) &&
		    ((!!(flags & IEEE80211_CHAN_PASSIVE_SCAN)) == passive) &&
		    (req->channels[i]->band == band)) {

			wl1271_debug(DEBUG_SCAN, "band %d, center_freq %d ",
				     req->channels[i]->band,
				     req->channels[i]->center_freq);
			wl1271_debug(DEBUG_SCAN, "hw_value %d, flags %X",
				     req->channels[i]->hw_value,
				     req->channels[i]->flags);
			wl1271_debug(DEBUG_SCAN,
				     "max_antenna_gain %d, max_power %d",
				     req->channels[i]->max_antenna_gain,
				     req->channels[i]->max_power);
			wl1271_debug(DEBUG_SCAN, "beacon_found %d",
				     req->channels[i]->beacon_found);

			channels[j].min_duration =
				cpu_to_le32(WL1271_SCAN_CHAN_MIN_DURATION);
			channels[j].max_duration =
				cpu_to_le32(WL1271_SCAN_CHAN_MAX_DURATION);
			channels[j].early_termination = 0;
			channels[j].tx_power_att = req->channels[i]->max_power;
			channels[j].channel = req->channels[i]->hw_value;

			memset(&channels[j].bssid_lsb, 0xff, 4);
			memset(&channels[j].bssid_msb, 0xff, 2);

			/* Mark the channels we already used */
			wl->scan.scanned_ch[i] = true;

			j++;
		}
	}

	return j;
}

#define WL1271_NOTHING_TO_SCAN 1

static int wl1271_scan_send(struct wl1271 *wl, enum ieee80211_band band,
			     bool passive, u32 basic_rate)
{
	struct wl1271_cmd_scan *cmd;
	struct wl1271_cmd_trigger_scan_to *trigger;
	int ret;
	u16 scan_options = 0;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	trigger = kzalloc(sizeof(*trigger), GFP_KERNEL);
	if (!cmd || !trigger) {
		ret = -ENOMEM;
		goto out;
	}

	/* We always use high priority scans */
	scan_options = WL1271_SCAN_OPT_PRIORITY_HIGH;

	/* No SSIDs means that we have a forced passive scan */
	if (passive || wl->scan.req->n_ssids == 0)
		scan_options |= WL1271_SCAN_OPT_PASSIVE;

	cmd->params.scan_options = cpu_to_le16(scan_options);

	cmd->params.n_ch = wl1271_get_scan_channels(wl, wl->scan.req,
						    cmd->channels,
						    band, passive);
	if (cmd->params.n_ch == 0) {
		ret = WL1271_NOTHING_TO_SCAN;
		goto out;
	}

	cmd->params.tx_rate = cpu_to_le32(basic_rate);
	cmd->params.rx_config_options = cpu_to_le32(CFG_RX_ALL_GOOD);
	cmd->params.rx_filter_options =
		cpu_to_le32(CFG_RX_PRSP_EN | CFG_RX_MGMT_EN | CFG_RX_BCN_EN);

	cmd->params.n_probe_reqs = WL1271_SCAN_PROBE_REQS;
	cmd->params.tx_rate = cpu_to_le32(basic_rate);
	cmd->params.tid_trigger = 0;
	cmd->params.scan_tag = WL1271_SCAN_DEFAULT_TAG;

	if (band == IEEE80211_BAND_2GHZ)
		cmd->params.band = WL1271_SCAN_BAND_2_4_GHZ;
	else
		cmd->params.band = WL1271_SCAN_BAND_5_GHZ;

	if (wl->scan.ssid_len && wl->scan.ssid) {
		cmd->params.ssid_len = wl->scan.ssid_len;
		memcpy(cmd->params.ssid, wl->scan.ssid, wl->scan.ssid_len);
	}

	ret = wl1271_cmd_build_probe_req(wl, wl->scan.ssid, wl->scan.ssid_len,
					 wl->scan.req->ie, wl->scan.req->ie_len,
					 band);
	if (ret < 0) {
		wl1271_error("PROBE request template failed");
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

	wl1271_dump(DEBUG_SCAN, "SCAN: ", cmd, sizeof(*cmd));

	ret = wl1271_cmd_send(wl, CMD_SCAN, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("SCAN failed");
		goto out;
	}

out:
	kfree(cmd);
	kfree(trigger);
	return ret;
}

void wl1271_scan_stm(struct wl1271 *wl)
{
	int ret = 0;

	switch (wl->scan.state) {
	case WL1271_SCAN_STATE_IDLE:
		break;

	case WL1271_SCAN_STATE_2GHZ_ACTIVE:
		ret = wl1271_scan_send(wl, IEEE80211_BAND_2GHZ, false,
				       wl->conf.tx.basic_rate);
		if (ret == WL1271_NOTHING_TO_SCAN) {
			wl->scan.state = WL1271_SCAN_STATE_2GHZ_PASSIVE;
			wl1271_scan_stm(wl);
		}

		break;

	case WL1271_SCAN_STATE_2GHZ_PASSIVE:
		ret = wl1271_scan_send(wl, IEEE80211_BAND_2GHZ, true,
				       wl->conf.tx.basic_rate);
		if (ret == WL1271_NOTHING_TO_SCAN) {
			if (wl->enable_11a)
				wl->scan.state = WL1271_SCAN_STATE_5GHZ_ACTIVE;
			else
				wl->scan.state = WL1271_SCAN_STATE_DONE;
			wl1271_scan_stm(wl);
		}

		break;

	case WL1271_SCAN_STATE_5GHZ_ACTIVE:
		ret = wl1271_scan_send(wl, IEEE80211_BAND_5GHZ, false,
				       wl->conf.tx.basic_rate_5);
		if (ret == WL1271_NOTHING_TO_SCAN) {
			wl->scan.state = WL1271_SCAN_STATE_5GHZ_PASSIVE;
			wl1271_scan_stm(wl);
		}

		break;

	case WL1271_SCAN_STATE_5GHZ_PASSIVE:
		ret = wl1271_scan_send(wl, IEEE80211_BAND_5GHZ, true,
				       wl->conf.tx.basic_rate_5);
		if (ret == WL1271_NOTHING_TO_SCAN) {
			wl->scan.state = WL1271_SCAN_STATE_DONE;
			wl1271_scan_stm(wl);
		}

		break;

	case WL1271_SCAN_STATE_DONE:
		wl->scan.failed = false;
		cancel_delayed_work(&wl->scan_complete_work);
		ieee80211_queue_delayed_work(wl->hw, &wl->scan_complete_work,
					     msecs_to_jiffies(0));
		break;

	default:
		wl1271_error("invalid scan state");
		break;
	}

	if (ret < 0) {
		cancel_delayed_work(&wl->scan_complete_work);
		ieee80211_queue_delayed_work(wl->hw, &wl->scan_complete_work,
					     msecs_to_jiffies(0));
	}
}

int wl1271_scan(struct wl1271 *wl, const u8 *ssid, size_t ssid_len,
		struct cfg80211_scan_request *req)
{
	if (wl->scan.state != WL1271_SCAN_STATE_IDLE)
		return -EBUSY;

	wl->scan.state = WL1271_SCAN_STATE_2GHZ_ACTIVE;

	if (ssid_len && ssid) {
		wl->scan.ssid_len = ssid_len;
		memcpy(wl->scan.ssid, ssid, ssid_len);
	} else {
		wl->scan.ssid_len = 0;
	}

	wl->scan.req = req;

	wl->scan.scanned_ch = kzalloc(req->n_channels *
				      sizeof(*wl->scan.scanned_ch),
				      GFP_KERNEL);
	/* we assume failure so that timeout scenarios are handled correctly */
	wl->scan.failed = true;
	ieee80211_queue_delayed_work(wl->hw, &wl->scan_complete_work,
				     msecs_to_jiffies(WL1271_SCAN_TIMEOUT));

	wl1271_scan_stm(wl);

	return 0;
}
