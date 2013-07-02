/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2012 Texas Instruments. All rights reserved.
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
#include "scan.h"
#include "../wlcore/debug.h"

static void wl18xx_adjust_channels(struct wl18xx_cmd_scan_params *cmd,
				   struct wlcore_scan_channels *cmd_channels)
{
	memcpy(cmd->passive, cmd_channels->passive, sizeof(cmd->passive));
	memcpy(cmd->active, cmd_channels->active, sizeof(cmd->active));
	cmd->dfs = cmd_channels->dfs;
	cmd->passive_active = cmd_channels->passive_active;

	memcpy(cmd->channels_2, cmd_channels->channels_2,
	       sizeof(cmd->channels_2));
	memcpy(cmd->channels_5, cmd_channels->channels_5,
	       sizeof(cmd->channels_5));
	/* channels_4 are not supported, so no need to copy them */
}

static int wl18xx_scan_send(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			    struct cfg80211_scan_request *req)
{
	struct wl18xx_cmd_scan_params *cmd;
	struct wlcore_scan_channels *cmd_channels = NULL;
	int ret;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->role_id = wlvif->role_id;

	if (WARN_ON(cmd->role_id == WL12XX_INVALID_ROLE_ID)) {
		ret = -EINVAL;
		goto out;
	}

	cmd->scan_type = SCAN_TYPE_SEARCH;
	cmd->rssi_threshold = -127;
	cmd->snr_threshold = 0;

	cmd->bss_type = SCAN_BSS_TYPE_ANY;

	cmd->ssid_from_list = 0;
	cmd->filter = 0;
	cmd->add_broadcast = 0;

	cmd->urgency = 0;
	cmd->protect = 0;

	cmd->n_probe_reqs = wl->conf.scan.num_probe_reqs;
	cmd->terminate_after = 0;

	/* configure channels */
	WARN_ON(req->n_ssids > 1);

	cmd_channels = kzalloc(sizeof(*cmd_channels), GFP_KERNEL);
	if (!cmd_channels) {
		ret = -ENOMEM;
		goto out;
	}

	wlcore_set_scan_chan_params(wl, cmd_channels, req->channels,
				    req->n_channels, req->n_ssids,
				    SCAN_TYPE_SEARCH);
	wl18xx_adjust_channels(cmd, cmd_channels);

	/*
	 * all the cycles params (except total cycles) should
	 * remain 0 for normal scan
	 */
	cmd->total_cycles = 1;

	if (req->no_cck)
		cmd->rate = WL18XX_SCAN_RATE_6;

	cmd->tag = WL1271_SCAN_DEFAULT_TAG;

	if (req->n_ssids) {
		cmd->ssid_len = req->ssids[0].ssid_len;
		memcpy(cmd->ssid, req->ssids[0].ssid, cmd->ssid_len);
	}

	/* TODO: per-band ies? */
	if (cmd->active[0]) {
		u8 band = IEEE80211_BAND_2GHZ;
		ret = wl12xx_cmd_build_probe_req(wl, wlvif,
				 cmd->role_id, band,
				 req->ssids ? req->ssids[0].ssid : NULL,
				 req->ssids ? req->ssids[0].ssid_len : 0,
				 req->ie,
				 req->ie_len,
				 false);
		if (ret < 0) {
			wl1271_error("2.4GHz PROBE request template failed");
			goto out;
		}
	}

	if (cmd->active[1] || cmd->dfs) {
		u8 band = IEEE80211_BAND_5GHZ;
		ret = wl12xx_cmd_build_probe_req(wl, wlvif,
				 cmd->role_id, band,
				 req->ssids ? req->ssids[0].ssid : NULL,
				 req->ssids ? req->ssids[0].ssid_len : 0,
				 req->ie,
				 req->ie_len,
				 false);
		if (ret < 0) {
			wl1271_error("5GHz PROBE request template failed");
			goto out;
		}
	}

	wl1271_dump(DEBUG_SCAN, "SCAN: ", cmd, sizeof(*cmd));

	ret = wl1271_cmd_send(wl, CMD_SCAN, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("SCAN failed");
		goto out;
	}

out:
	kfree(cmd_channels);
	kfree(cmd);
	return ret;
}

void wl18xx_scan_completed(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	wl->scan.failed = false;
	cancel_delayed_work(&wl->scan_complete_work);
	ieee80211_queue_delayed_work(wl->hw, &wl->scan_complete_work,
				     msecs_to_jiffies(0));
}

static
int wl18xx_scan_sched_scan_config(struct wl1271 *wl,
				  struct wl12xx_vif *wlvif,
				  struct cfg80211_sched_scan_request *req,
				  struct ieee80211_sched_scan_ies *ies)
{
	struct wl18xx_cmd_scan_params *cmd;
	struct wlcore_scan_channels *cmd_channels = NULL;
	struct conf_sched_scan_settings *c = &wl->conf.sched_scan;
	int ret;
	int filter_type;

	wl1271_debug(DEBUG_CMD, "cmd sched_scan scan config");

	filter_type = wlcore_scan_sched_scan_ssid_list(wl, wlvif, req);
	if (filter_type < 0)
		return filter_type;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->role_id = wlvif->role_id;

	if (WARN_ON(cmd->role_id == WL12XX_INVALID_ROLE_ID)) {
		ret = -EINVAL;
		goto out;
	}

	cmd->scan_type = SCAN_TYPE_PERIODIC;
	cmd->rssi_threshold = c->rssi_threshold;
	cmd->snr_threshold = c->snr_threshold;

	/* don't filter on BSS type */
	cmd->bss_type = SCAN_BSS_TYPE_ANY;

	cmd->ssid_from_list = 1;
	if (filter_type == SCAN_SSID_FILTER_LIST)
		cmd->filter = 1;
	cmd->add_broadcast = 0;

	cmd->urgency = 0;
	cmd->protect = 0;

	cmd->n_probe_reqs = c->num_probe_reqs;
	/* don't stop scanning automatically when something is found */
	cmd->terminate_after = 0;

	cmd_channels = kzalloc(sizeof(*cmd_channels), GFP_KERNEL);
	if (!cmd_channels) {
		ret = -ENOMEM;
		goto out;
	}

	/* configure channels */
	wlcore_set_scan_chan_params(wl, cmd_channels, req->channels,
				    req->n_channels, req->n_ssids,
				    SCAN_TYPE_PERIODIC);
	wl18xx_adjust_channels(cmd, cmd_channels);

	cmd->short_cycles_sec = 0;
	cmd->long_cycles_sec = cpu_to_le16(req->interval);
	cmd->short_cycles_count = 0;

	cmd->total_cycles = 0;

	cmd->tag = WL1271_SCAN_DEFAULT_TAG;

	/* create a PERIODIC_SCAN_REPORT_EVENT whenever we've got a match */
	cmd->report_threshold = 1;
	cmd->terminate_on_report = 0;

	if (cmd->active[0]) {
		u8 band = IEEE80211_BAND_2GHZ;
		ret = wl12xx_cmd_build_probe_req(wl, wlvif,
				 cmd->role_id, band,
				 req->ssids ? req->ssids[0].ssid : NULL,
				 req->ssids ? req->ssids[0].ssid_len : 0,
				 ies->ie[band],
				 ies->len[band],
				 true);
		if (ret < 0) {
			wl1271_error("2.4GHz PROBE request template failed");
			goto out;
		}
	}

	if (cmd->active[1] || cmd->dfs) {
		u8 band = IEEE80211_BAND_5GHZ;
		ret = wl12xx_cmd_build_probe_req(wl, wlvif,
				 cmd->role_id, band,
				 req->ssids ? req->ssids[0].ssid : NULL,
				 req->ssids ? req->ssids[0].ssid_len : 0,
				 ies->ie[band],
				 ies->len[band],
				 true);
		if (ret < 0) {
			wl1271_error("5GHz PROBE request template failed");
			goto out;
		}
	}

	wl1271_dump(DEBUG_SCAN, "SCAN: ", cmd, sizeof(*cmd));

	ret = wl1271_cmd_send(wl, CMD_SCAN, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("SCAN failed");
		goto out;
	}

out:
	kfree(cmd_channels);
	kfree(cmd);
	return ret;
}

int wl18xx_sched_scan_start(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			    struct cfg80211_sched_scan_request *req,
			    struct ieee80211_sched_scan_ies *ies)
{
	return wl18xx_scan_sched_scan_config(wl, wlvif, req, ies);
}

static int __wl18xx_scan_stop(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			       u8 scan_type)
{
	struct wl18xx_cmd_scan_stop *stop;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd periodic scan stop");

	stop = kzalloc(sizeof(*stop), GFP_KERNEL);
	if (!stop) {
		wl1271_error("failed to alloc memory to send sched scan stop");
		return -ENOMEM;
	}

	stop->role_id = wlvif->role_id;
	stop->scan_type = scan_type;

	ret = wl1271_cmd_send(wl, CMD_STOP_SCAN, stop, sizeof(*stop), 0);
	if (ret < 0) {
		wl1271_error("failed to send sched scan stop command");
		goto out_free;
	}

out_free:
	kfree(stop);
	return ret;
}

void wl18xx_scan_sched_scan_stop(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	__wl18xx_scan_stop(wl, wlvif, SCAN_TYPE_PERIODIC);
}
int wl18xx_scan_start(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		      struct cfg80211_scan_request *req)
{
	return wl18xx_scan_send(wl, wlvif, req);
}

int wl18xx_scan_stop(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	return __wl18xx_scan_stop(wl, wlvif, SCAN_TYPE_SEARCH);
}
