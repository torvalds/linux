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

#include "wlcore.h"
#include "debug.h"
#include "cmd.h"
#include "scan.h"
#include "acx.h"
#include "ps.h"
#include "tx.h"

void wl1271_scan_complete_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1271 *wl;
	struct wl12xx_vif *wlvif;
	int ret;

	dwork = to_delayed_work(work);
	wl = container_of(dwork, struct wl1271, scan_complete_work);

	wl1271_debug(DEBUG_SCAN, "Scanning complete");

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	if (wl->scan.state == WL1271_SCAN_STATE_IDLE)
		goto out;

	wlvif = wl->scan_wlvif;

	/*
	 * Rearm the tx watchdog just before idling scan. This
	 * prevents just-finished scans from triggering the watchdog
	 */
	wl12xx_rearm_tx_watchdog_locked(wl);

	wl->scan.state = WL1271_SCAN_STATE_IDLE;
	memset(wl->scan.scanned_ch, 0, sizeof(wl->scan.scanned_ch));
	wl->scan.req = NULL;
	wl->scan_wlvif = NULL;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	if (test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags)) {
		/* restore hardware connection monitoring template */
		wl1271_cmd_build_ap_probe_req(wl, wlvif, wlvif->probereq);
	}

	wl1271_ps_elp_sleep(wl);

	if (wl->scan.failed) {
		wl1271_info("Scan completed due to error.");
		wl12xx_queue_recovery_work(wl);
	}

	wlcore_cmd_regdomain_config_locked(wl);

	ieee80211_scan_completed(wl->hw, false);

out:
	mutex_unlock(&wl->mutex);

}

static void wlcore_started_vifs_iter(void *data, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	bool active = false;
	int *count = (int *)data;

	/*
	 * count active interfaces according to interface type.
	 * checking only bss_conf.idle is bad for some cases, e.g.
	 * we don't want to count sta in p2p_find as active interface.
	 */
	switch (wlvif->bss_type) {
	case BSS_TYPE_STA_BSS:
		if (test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
			active = true;
		break;

	case BSS_TYPE_AP_BSS:
		if (wlvif->wl->active_sta_count > 0)
			active = true;
		break;

	default:
		break;
	}

	if (active)
		(*count)++;
}

static int wlcore_count_started_vifs(struct wl1271 *wl)
{
	int count = 0;

	ieee80211_iterate_active_interfaces_atomic(wl->hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					wlcore_started_vifs_iter, &count);
	return count;
}

static int
wlcore_scan_get_channels(struct wl1271 *wl,
			 struct ieee80211_channel *req_channels[],
			 u32 n_channels,
			 u32 n_ssids,
			 struct conn_scan_ch_params *channels,
			 u32 band, bool radar, bool passive,
			 int start, int max_channels,
			 u8 *n_pactive_ch,
			 int scan_type)
{
	int i, j;
	u32 flags;
	bool force_passive = !n_ssids;
	u32 min_dwell_time_active, max_dwell_time_active;
	u32 dwell_time_passive, dwell_time_dfs;

	/* configure dwell times according to scan type */
	if (scan_type == SCAN_TYPE_SEARCH) {
		struct conf_scan_settings *c = &wl->conf.scan;
		bool active_vif_exists = !!wlcore_count_started_vifs(wl);

		min_dwell_time_active = active_vif_exists ?
			c->min_dwell_time_active :
			c->min_dwell_time_active_long;
		max_dwell_time_active = active_vif_exists ?
			c->max_dwell_time_active :
			c->max_dwell_time_active_long;
		dwell_time_passive = c->dwell_time_passive;
		dwell_time_dfs = c->dwell_time_dfs;
	} else {
		struct conf_sched_scan_settings *c = &wl->conf.sched_scan;
		u32 delta_per_probe;

		if (band == NL80211_BAND_5GHZ)
			delta_per_probe = c->dwell_time_delta_per_probe_5;
		else
			delta_per_probe = c->dwell_time_delta_per_probe;

		min_dwell_time_active = c->base_dwell_time +
			 n_ssids * c->num_probe_reqs * delta_per_probe;

		max_dwell_time_active = min_dwell_time_active +
					c->max_dwell_time_delta;
		dwell_time_passive = c->dwell_time_passive;
		dwell_time_dfs = c->dwell_time_dfs;
	}
	min_dwell_time_active = DIV_ROUND_UP(min_dwell_time_active, 1000);
	max_dwell_time_active = DIV_ROUND_UP(max_dwell_time_active, 1000);
	dwell_time_passive = DIV_ROUND_UP(dwell_time_passive, 1000);
	dwell_time_dfs = DIV_ROUND_UP(dwell_time_dfs, 1000);

	for (i = 0, j = start;
	     i < n_channels && j < max_channels;
	     i++) {
		flags = req_channels[i]->flags;

		if (force_passive)
			flags |= IEEE80211_CHAN_NO_IR;

		if ((req_channels[i]->band == band) &&
		    !(flags & IEEE80211_CHAN_DISABLED) &&
		    (!!(flags & IEEE80211_CHAN_RADAR) == radar) &&
		    /* if radar is set, we ignore the passive flag */
		    (radar ||
		     !!(flags & IEEE80211_CHAN_NO_IR) == passive)) {
			if (flags & IEEE80211_CHAN_RADAR) {
				channels[j].flags |= SCAN_CHANNEL_FLAGS_DFS;

				channels[j].passive_duration =
					cpu_to_le16(dwell_time_dfs);
			} else {
				channels[j].passive_duration =
					cpu_to_le16(dwell_time_passive);
			}

			channels[j].min_duration =
				cpu_to_le16(min_dwell_time_active);
			channels[j].max_duration =
				cpu_to_le16(max_dwell_time_active);

			channels[j].tx_power_att = req_channels[i]->max_power;
			channels[j].channel = req_channels[i]->hw_value;

			if (n_pactive_ch &&
			    (band == NL80211_BAND_2GHZ) &&
			    (channels[j].channel >= 12) &&
			    (channels[j].channel <= 14) &&
			    (flags & IEEE80211_CHAN_NO_IR) &&
			    !force_passive) {
				/* pactive channels treated as DFS */
				channels[j].flags = SCAN_CHANNEL_FLAGS_DFS;

				/*
				 * n_pactive_ch is counted down from the end of
				 * the passive channel list
				 */
				(*n_pactive_ch)++;
				wl1271_debug(DEBUG_SCAN, "n_pactive_ch = %d",
					     *n_pactive_ch);
			}

			wl1271_debug(DEBUG_SCAN, "freq %d, ch. %d, flags 0x%x, power %d, min/max_dwell %d/%d%s%s",
				     req_channels[i]->center_freq,
				     req_channels[i]->hw_value,
				     req_channels[i]->flags,
				     req_channels[i]->max_power,
				     min_dwell_time_active,
				     max_dwell_time_active,
				     flags & IEEE80211_CHAN_RADAR ?
					", DFS" : "",
				     flags & IEEE80211_CHAN_NO_IR ?
					", NO-IR" : "");
			j++;
		}
	}

	return j - start;
}

bool
wlcore_set_scan_chan_params(struct wl1271 *wl,
			    struct wlcore_scan_channels *cfg,
			    struct ieee80211_channel *channels[],
			    u32 n_channels,
			    u32 n_ssids,
			    int scan_type)
{
	u8 n_pactive_ch = 0;

	cfg->passive[0] =
		wlcore_scan_get_channels(wl,
					 channels,
					 n_channels,
					 n_ssids,
					 cfg->channels_2,
					 NL80211_BAND_2GHZ,
					 false, true, 0,
					 MAX_CHANNELS_2GHZ,
					 &n_pactive_ch,
					 scan_type);
	cfg->active[0] =
		wlcore_scan_get_channels(wl,
					 channels,
					 n_channels,
					 n_ssids,
					 cfg->channels_2,
					 NL80211_BAND_2GHZ,
					 false, false,
					 cfg->passive[0],
					 MAX_CHANNELS_2GHZ,
					 &n_pactive_ch,
					 scan_type);
	cfg->passive[1] =
		wlcore_scan_get_channels(wl,
					 channels,
					 n_channels,
					 n_ssids,
					 cfg->channels_5,
					 NL80211_BAND_5GHZ,
					 false, true, 0,
					 wl->max_channels_5,
					 &n_pactive_ch,
					 scan_type);
	cfg->dfs =
		wlcore_scan_get_channels(wl,
					 channels,
					 n_channels,
					 n_ssids,
					 cfg->channels_5,
					 NL80211_BAND_5GHZ,
					 true, true,
					 cfg->passive[1],
					 wl->max_channels_5,
					 &n_pactive_ch,
					 scan_type);
	cfg->active[1] =
		wlcore_scan_get_channels(wl,
					 channels,
					 n_channels,
					 n_ssids,
					 cfg->channels_5,
					 NL80211_BAND_5GHZ,
					 false, false,
					 cfg->passive[1] + cfg->dfs,
					 wl->max_channels_5,
					 &n_pactive_ch,
					 scan_type);

	/* 802.11j channels are not supported yet */
	cfg->passive[2] = 0;
	cfg->active[2] = 0;

	cfg->passive_active = n_pactive_ch;

	wl1271_debug(DEBUG_SCAN, "    2.4GHz: active %d passive %d",
		     cfg->active[0], cfg->passive[0]);
	wl1271_debug(DEBUG_SCAN, "    5GHz: active %d passive %d",
		     cfg->active[1], cfg->passive[1]);
	wl1271_debug(DEBUG_SCAN, "    DFS: %d", cfg->dfs);

	return  cfg->passive[0] || cfg->active[0] ||
		cfg->passive[1] || cfg->active[1] || cfg->dfs ||
		cfg->passive[2] || cfg->active[2];
}
EXPORT_SYMBOL_GPL(wlcore_set_scan_chan_params);

int wlcore_scan(struct wl1271 *wl, struct ieee80211_vif *vif,
		const u8 *ssid, size_t ssid_len,
		struct cfg80211_scan_request *req)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);

	/*
	 * cfg80211 should guarantee that we don't get more channels
	 * than what we have registered.
	 */
	BUG_ON(req->n_channels > WL1271_MAX_CHANNELS);

	if (wl->scan.state != WL1271_SCAN_STATE_IDLE)
		return -EBUSY;

	wl->scan.state = WL1271_SCAN_STATE_2GHZ_ACTIVE;

	if (ssid_len && ssid) {
		wl->scan.ssid_len = ssid_len;
		memcpy(wl->scan.ssid, ssid, ssid_len);
	} else {
		wl->scan.ssid_len = 0;
	}

	wl->scan_wlvif = wlvif;
	wl->scan.req = req;
	memset(wl->scan.scanned_ch, 0, sizeof(wl->scan.scanned_ch));

	/* we assume failure so that timeout scenarios are handled correctly */
	wl->scan.failed = true;
	ieee80211_queue_delayed_work(wl->hw, &wl->scan_complete_work,
				     msecs_to_jiffies(WL1271_SCAN_TIMEOUT));

	wl->ops->scan_start(wl, wlvif, req);

	return 0;
}
/* Returns the scan type to be used or a negative value on error */
int
wlcore_scan_sched_scan_ssid_list(struct wl1271 *wl,
				 struct wl12xx_vif *wlvif,
				 struct cfg80211_sched_scan_request *req)
{
	struct wl1271_cmd_sched_scan_ssid_list *cmd = NULL;
	struct cfg80211_match_set *sets = req->match_sets;
	struct cfg80211_ssid *ssids = req->ssids;
	int ret = 0, type, i, j, n_match_ssids = 0;

	wl1271_debug((DEBUG_CMD | DEBUG_SCAN), "cmd sched scan ssid list");

	/* count the match sets that contain SSIDs */
	for (i = 0; i < req->n_match_sets; i++)
		if (sets[i].ssid.ssid_len > 0)
			n_match_ssids++;

	/* No filter, no ssids or only bcast ssid */
	if (!n_match_ssids &&
	    (!req->n_ssids ||
	     (req->n_ssids == 1 && req->ssids[0].ssid_len == 0))) {
		type = SCAN_SSID_FILTER_ANY;
		goto out;
	}

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->role_id = wlvif->role_id;
	if (!n_match_ssids) {
		/* No filter, with ssids */
		type = SCAN_SSID_FILTER_DISABLED;

		for (i = 0; i < req->n_ssids; i++) {
			cmd->ssids[cmd->n_ssids].type = (ssids[i].ssid_len) ?
				SCAN_SSID_TYPE_HIDDEN : SCAN_SSID_TYPE_PUBLIC;
			cmd->ssids[cmd->n_ssids].len = ssids[i].ssid_len;
			memcpy(cmd->ssids[cmd->n_ssids].ssid, ssids[i].ssid,
			       ssids[i].ssid_len);
			cmd->n_ssids++;
		}
	} else {
		type = SCAN_SSID_FILTER_LIST;

		/* Add all SSIDs from the filters */
		for (i = 0; i < req->n_match_sets; i++) {
			/* ignore sets without SSIDs */
			if (!sets[i].ssid.ssid_len)
				continue;

			cmd->ssids[cmd->n_ssids].type = SCAN_SSID_TYPE_PUBLIC;
			cmd->ssids[cmd->n_ssids].len = sets[i].ssid.ssid_len;
			memcpy(cmd->ssids[cmd->n_ssids].ssid,
			       sets[i].ssid.ssid, sets[i].ssid.ssid_len);
			cmd->n_ssids++;
		}
		if ((req->n_ssids > 1) ||
		    (req->n_ssids == 1 && req->ssids[0].ssid_len > 0)) {
			/*
			 * Mark all the SSIDs passed in the SSID list as HIDDEN,
			 * so they're used in probe requests.
			 */
			for (i = 0; i < req->n_ssids; i++) {
				if (!req->ssids[i].ssid_len)
					continue;

				for (j = 0; j < cmd->n_ssids; j++)
					if ((req->ssids[i].ssid_len ==
					     cmd->ssids[j].len) &&
					    !memcmp(req->ssids[i].ssid,
						   cmd->ssids[j].ssid,
						   req->ssids[i].ssid_len)) {
						cmd->ssids[j].type =
							SCAN_SSID_TYPE_HIDDEN;
						break;
					}
				/* Fail if SSID isn't present in the filters */
				if (j == cmd->n_ssids) {
					ret = -EINVAL;
					goto out_free;
				}
			}
		}
	}

	ret = wl1271_cmd_send(wl, CMD_CONNECTION_SCAN_SSID_CFG, cmd,
			      sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("cmd sched scan ssid list failed");
		goto out_free;
	}

out_free:
	kfree(cmd);
out:
	if (ret < 0)
		return ret;
	return type;
}
EXPORT_SYMBOL_GPL(wlcore_scan_sched_scan_ssid_list);

void wlcore_scan_sched_scan_results(struct wl1271 *wl)
{
	wl1271_debug(DEBUG_SCAN, "got periodic scan results");

	ieee80211_sched_scan_results(wl->hw);
}
EXPORT_SYMBOL_GPL(wlcore_scan_sched_scan_results);
