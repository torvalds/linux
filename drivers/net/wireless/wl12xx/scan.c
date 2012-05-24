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

#include "wl12xx.h"
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
	struct ieee80211_vif *vif;
	struct wl12xx_vif *wlvif;
	int ret;

	dwork = container_of(work, struct delayed_work, work);
	wl = container_of(dwork, struct wl1271, scan_complete_work);

	wl1271_debug(DEBUG_SCAN, "Scanning complete");

	mutex_lock(&wl->mutex);

	if (wl->state == WL1271_STATE_OFF)
		goto out;

	if (wl->scan.state == WL1271_SCAN_STATE_IDLE)
		goto out;

	vif = wl->scan_vif;
	wlvif = wl12xx_vif_to_data(vif);

	/*
	 * Rearm the tx watchdog just before idling scan. This
	 * prevents just-finished scans from triggering the watchdog
	 */
	wl12xx_rearm_tx_watchdog_locked(wl);

	wl->scan.state = WL1271_SCAN_STATE_IDLE;
	memset(wl->scan.scanned_ch, 0, sizeof(wl->scan.scanned_ch));
	wl->scan.req = NULL;
	wl->scan_vif = NULL;

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

	ieee80211_scan_completed(wl->hw, false);

out:
	mutex_unlock(&wl->mutex);

}


static int wl1271_get_scan_channels(struct wl1271 *wl,
				    struct cfg80211_scan_request *req,
				    struct basic_scan_channel_params *channels,
				    enum ieee80211_band band, bool passive)
{
	struct conf_scan_settings *c = &wl->conf.scan;
	int i, j;
	u32 flags;

	for (i = 0, j = 0;
	     i < req->n_channels && j < WL1271_SCAN_MAX_CHANNELS;
	     i++) {
		flags = req->channels[i]->flags;

		if (!test_bit(i, wl->scan.scanned_ch) &&
		    !(flags & IEEE80211_CHAN_DISABLED) &&
		    (req->channels[i]->band == band) &&
		    /*
		     * In passive scans, we scan all remaining
		     * channels, even if not marked as such.
		     * In active scans, we only scan channels not
		     * marked as passive.
		     */
		    (passive || !(flags & IEEE80211_CHAN_PASSIVE_SCAN))) {
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

			if (!passive) {
				channels[j].min_duration =
					cpu_to_le32(c->min_dwell_time_active);
				channels[j].max_duration =
					cpu_to_le32(c->max_dwell_time_active);
			} else {
				channels[j].min_duration =
					cpu_to_le32(c->min_dwell_time_passive);
				channels[j].max_duration =
					cpu_to_le32(c->max_dwell_time_passive);
			}
			channels[j].early_termination = 0;
			channels[j].tx_power_att = req->channels[i]->max_power;
			channels[j].channel = req->channels[i]->hw_value;

			memset(&channels[j].bssid_lsb, 0xff, 4);
			memset(&channels[j].bssid_msb, 0xff, 2);

			/* Mark the channels we already used */
			set_bit(i, wl->scan.scanned_ch);

			j++;
		}
	}

	return j;
}

#define WL1271_NOTHING_TO_SCAN 1

static int wl1271_scan_send(struct wl1271 *wl, struct ieee80211_vif *vif,
			    enum ieee80211_band band,
			    bool passive, u32 basic_rate)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	struct wl1271_cmd_scan *cmd;
	struct wl1271_cmd_trigger_scan_to *trigger;
	int ret;
	u16 scan_options = 0;

	/* skip active scans if we don't have SSIDs */
	if (!passive && wl->scan.req->n_ssids == 0)
		return WL1271_NOTHING_TO_SCAN;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	trigger = kzalloc(sizeof(*trigger), GFP_KERNEL);
	if (!cmd || !trigger) {
		ret = -ENOMEM;
		goto out;
	}

	if (wl->conf.scan.split_scan_timeout)
		scan_options |= WL1271_SCAN_OPT_SPLIT_SCAN;

	if (passive)
		scan_options |= WL1271_SCAN_OPT_PASSIVE;

	if (wlvif->bss_type == BSS_TYPE_AP_BSS ||
	    test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
		cmd->params.role_id = wlvif->role_id;
	else
		cmd->params.role_id = wlvif->dev_role_id;

	if (WARN_ON(cmd->params.role_id == WL12XX_INVALID_ROLE_ID)) {
		ret = -EINVAL;
		goto out;
	}

	cmd->params.scan_options = cpu_to_le16(scan_options);

	cmd->params.n_ch = wl1271_get_scan_channels(wl, wl->scan.req,
						    cmd->channels,
						    band, passive);
	if (cmd->params.n_ch == 0) {
		ret = WL1271_NOTHING_TO_SCAN;
		goto out;
	}

	cmd->params.tx_rate = cpu_to_le32(basic_rate);
	cmd->params.n_probe_reqs = wl->conf.scan.num_probe_reqs;
	cmd->params.tid_trigger = CONF_TX_AC_ANY_TID;
	cmd->params.scan_tag = WL1271_SCAN_DEFAULT_TAG;

	if (band == IEEE80211_BAND_2GHZ)
		cmd->params.band = WL1271_SCAN_BAND_2_4_GHZ;
	else
		cmd->params.band = WL1271_SCAN_BAND_5_GHZ;

	if (wl->scan.ssid_len && wl->scan.ssid) {
		cmd->params.ssid_len = wl->scan.ssid_len;
		memcpy(cmd->params.ssid, wl->scan.ssid, wl->scan.ssid_len);
	}

	memcpy(cmd->addr, vif->addr, ETH_ALEN);

	ret = wl12xx_cmd_build_probe_req(wl, wlvif,
					 cmd->params.role_id, band,
					 wl->scan.ssid, wl->scan.ssid_len,
					 wl->scan.req->ie,
					 wl->scan.req->ie_len);
	if (ret < 0) {
		wl1271_error("PROBE request template failed");
		goto out;
	}

	trigger->timeout = cpu_to_le32(wl->conf.scan.split_scan_timeout);
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

void wl1271_scan_stm(struct wl1271 *wl, struct ieee80211_vif *vif)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	int ret = 0;
	enum ieee80211_band band;
	u32 rate, mask;

	switch (wl->scan.state) {
	case WL1271_SCAN_STATE_IDLE:
		break;

	case WL1271_SCAN_STATE_2GHZ_ACTIVE:
		band = IEEE80211_BAND_2GHZ;
		mask = wlvif->bitrate_masks[band];
		if (wl->scan.req->no_cck) {
			mask &= ~CONF_TX_CCK_RATES;
			if (!mask)
				mask = CONF_TX_RATE_MASK_BASIC_P2P;
		}
		rate = wl1271_tx_min_rate_get(wl, mask);
		ret = wl1271_scan_send(wl, vif, band, false, rate);
		if (ret == WL1271_NOTHING_TO_SCAN) {
			wl->scan.state = WL1271_SCAN_STATE_2GHZ_PASSIVE;
			wl1271_scan_stm(wl, vif);
		}

		break;

	case WL1271_SCAN_STATE_2GHZ_PASSIVE:
		band = IEEE80211_BAND_2GHZ;
		mask = wlvif->bitrate_masks[band];
		if (wl->scan.req->no_cck) {
			mask &= ~CONF_TX_CCK_RATES;
			if (!mask)
				mask = CONF_TX_RATE_MASK_BASIC_P2P;
		}
		rate = wl1271_tx_min_rate_get(wl, mask);
		ret = wl1271_scan_send(wl, vif, band, true, rate);
		if (ret == WL1271_NOTHING_TO_SCAN) {
			if (wl->enable_11a)
				wl->scan.state = WL1271_SCAN_STATE_5GHZ_ACTIVE;
			else
				wl->scan.state = WL1271_SCAN_STATE_DONE;
			wl1271_scan_stm(wl, vif);
		}

		break;

	case WL1271_SCAN_STATE_5GHZ_ACTIVE:
		band = IEEE80211_BAND_5GHZ;
		rate = wl1271_tx_min_rate_get(wl, wlvif->bitrate_masks[band]);
		ret = wl1271_scan_send(wl, vif, band, false, rate);
		if (ret == WL1271_NOTHING_TO_SCAN) {
			wl->scan.state = WL1271_SCAN_STATE_5GHZ_PASSIVE;
			wl1271_scan_stm(wl, vif);
		}

		break;

	case WL1271_SCAN_STATE_5GHZ_PASSIVE:
		band = IEEE80211_BAND_5GHZ;
		rate = wl1271_tx_min_rate_get(wl, wlvif->bitrate_masks[band]);
		ret = wl1271_scan_send(wl, vif, band, true, rate);
		if (ret == WL1271_NOTHING_TO_SCAN) {
			wl->scan.state = WL1271_SCAN_STATE_DONE;
			wl1271_scan_stm(wl, vif);
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

int wl1271_scan(struct wl1271 *wl, struct ieee80211_vif *vif,
		const u8 *ssid, size_t ssid_len,
		struct cfg80211_scan_request *req)
{
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

	wl->scan_vif = vif;
	wl->scan.req = req;
	memset(wl->scan.scanned_ch, 0, sizeof(wl->scan.scanned_ch));

	/* we assume failure so that timeout scenarios are handled correctly */
	wl->scan.failed = true;
	ieee80211_queue_delayed_work(wl->hw, &wl->scan_complete_work,
				     msecs_to_jiffies(WL1271_SCAN_TIMEOUT));

	wl1271_scan_stm(wl, vif);

	return 0;
}

int wl1271_scan_stop(struct wl1271 *wl)
{
	struct wl1271_cmd_header *cmd = NULL;
	int ret = 0;

	if (WARN_ON(wl->scan.state == WL1271_SCAN_STATE_IDLE))
		return -EINVAL;

	wl1271_debug(DEBUG_CMD, "cmd scan stop");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl1271_cmd_send(wl, CMD_STOP_SCAN, cmd,
			      sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("cmd stop_scan failed");
		goto out;
	}
out:
	kfree(cmd);
	return ret;
}

static int
wl1271_scan_get_sched_scan_channels(struct wl1271 *wl,
				    struct cfg80211_sched_scan_request *req,
				    struct conn_scan_ch_params *channels,
				    u32 band, bool radar, bool passive,
				    int start, int max_channels)
{
	struct conf_sched_scan_settings *c = &wl->conf.sched_scan;
	int i, j;
	u32 flags;
	bool force_passive = !req->n_ssids;

	for (i = 0, j = start;
	     i < req->n_channels && j < max_channels;
	     i++) {
		flags = req->channels[i]->flags;

		if (force_passive)
			flags |= IEEE80211_CHAN_PASSIVE_SCAN;

		if ((req->channels[i]->band == band) &&
		    !(flags & IEEE80211_CHAN_DISABLED) &&
		    (!!(flags & IEEE80211_CHAN_RADAR) == radar) &&
		    /* if radar is set, we ignore the passive flag */
		    (radar ||
		     !!(flags & IEEE80211_CHAN_PASSIVE_SCAN) == passive)) {
			wl1271_debug(DEBUG_SCAN, "band %d, center_freq %d ",
				     req->channels[i]->band,
				     req->channels[i]->center_freq);
			wl1271_debug(DEBUG_SCAN, "hw_value %d, flags %X",
				     req->channels[i]->hw_value,
				     req->channels[i]->flags);
			wl1271_debug(DEBUG_SCAN, "max_power %d",
				     req->channels[i]->max_power);

			if (flags & IEEE80211_CHAN_RADAR) {
				channels[j].flags |= SCAN_CHANNEL_FLAGS_DFS;

				channels[j].passive_duration =
					cpu_to_le16(c->dwell_time_dfs);
			} else {
				channels[j].passive_duration =
					cpu_to_le16(c->dwell_time_passive);
			}

			channels[j].min_duration =
				cpu_to_le16(c->min_dwell_time_active);
			channels[j].max_duration =
				cpu_to_le16(c->max_dwell_time_active);

			channels[j].tx_power_att = req->channels[i]->max_power;
			channels[j].channel = req->channels[i]->hw_value;

			j++;
		}
	}

	return j - start;
}

static bool
wl1271_scan_sched_scan_channels(struct wl1271 *wl,
				struct cfg80211_sched_scan_request *req,
				struct wl1271_cmd_sched_scan_config *cfg)
{
	cfg->passive[0] =
		wl1271_scan_get_sched_scan_channels(wl, req, cfg->channels_2,
						    IEEE80211_BAND_2GHZ,
						    false, true, 0,
						    MAX_CHANNELS_2GHZ);
	cfg->active[0] =
		wl1271_scan_get_sched_scan_channels(wl, req, cfg->channels_2,
						    IEEE80211_BAND_2GHZ,
						    false, false,
						    cfg->passive[0],
						    MAX_CHANNELS_2GHZ);
	cfg->passive[1] =
		wl1271_scan_get_sched_scan_channels(wl, req, cfg->channels_5,
						    IEEE80211_BAND_5GHZ,
						    false, true, 0,
						    MAX_CHANNELS_5GHZ);
	cfg->dfs =
		wl1271_scan_get_sched_scan_channels(wl, req, cfg->channels_5,
						    IEEE80211_BAND_5GHZ,
						    true, true,
						    cfg->passive[1],
						    MAX_CHANNELS_5GHZ);
	cfg->active[1] =
		wl1271_scan_get_sched_scan_channels(wl, req, cfg->channels_5,
						    IEEE80211_BAND_5GHZ,
						    false, false,
						    cfg->passive[1] + cfg->dfs,
						    MAX_CHANNELS_5GHZ);
	/* 802.11j channels are not supported yet */
	cfg->passive[2] = 0;
	cfg->active[2] = 0;

	wl1271_debug(DEBUG_SCAN, "    2.4GHz: active %d passive %d",
		     cfg->active[0], cfg->passive[0]);
	wl1271_debug(DEBUG_SCAN, "    5GHz: active %d passive %d",
		     cfg->active[1], cfg->passive[1]);
	wl1271_debug(DEBUG_SCAN, "    DFS: %d", cfg->dfs);

	return  cfg->passive[0] || cfg->active[0] ||
		cfg->passive[1] || cfg->active[1] || cfg->dfs ||
		cfg->passive[2] || cfg->active[2];
}

/* Returns the scan type to be used or a negative value on error */
static int
wl12xx_scan_sched_scan_ssid_list(struct wl1271 *wl,
				 struct cfg80211_sched_scan_request *req)
{
	struct wl1271_cmd_sched_scan_ssid_list *cmd = NULL;
	struct cfg80211_match_set *sets = req->match_sets;
	struct cfg80211_ssid *ssids = req->ssids;
	int ret = 0, type, i, j, n_match_ssids = 0;

	wl1271_debug(DEBUG_CMD, "cmd sched scan ssid list");

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
					if (!memcmp(req->ssids[i].ssid,
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

	wl1271_dump(DEBUG_SCAN, "SSID_LIST: ", cmd, sizeof(*cmd));

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

int wl1271_scan_sched_scan_config(struct wl1271 *wl,
				  struct wl12xx_vif *wlvif,
				  struct cfg80211_sched_scan_request *req,
				  struct ieee80211_sched_scan_ies *ies)
{
	struct wl1271_cmd_sched_scan_config *cfg = NULL;
	struct conf_sched_scan_settings *c = &wl->conf.sched_scan;
	int i, ret;
	bool force_passive = !req->n_ssids;

	wl1271_debug(DEBUG_CMD, "cmd sched_scan scan config");

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	cfg->rssi_threshold = c->rssi_threshold;
	cfg->snr_threshold  = c->snr_threshold;
	cfg->n_probe_reqs = c->num_probe_reqs;
	/* cycles set to 0 it means infinite (until manually stopped) */
	cfg->cycles = 0;
	/* report APs when at least 1 is found */
	cfg->report_after = 1;
	/* don't stop scanning automatically when something is found */
	cfg->terminate = 0;
	cfg->tag = WL1271_SCAN_DEFAULT_TAG;
	/* don't filter on BSS type */
	cfg->bss_type = SCAN_BSS_TYPE_ANY;
	/* currently NL80211 supports only a single interval */
	for (i = 0; i < SCAN_MAX_CYCLE_INTERVALS; i++)
		cfg->intervals[i] = cpu_to_le32(req->interval);

	cfg->ssid_len = 0;
	ret = wl12xx_scan_sched_scan_ssid_list(wl, req);
	if (ret < 0)
		goto out;

	cfg->filter_type = ret;

	wl1271_debug(DEBUG_SCAN, "filter_type = %d", cfg->filter_type);

	if (!wl1271_scan_sched_scan_channels(wl, req, cfg)) {
		wl1271_error("scan channel list is empty");
		ret = -EINVAL;
		goto out;
	}

	if (!force_passive && cfg->active[0]) {
		u8 band = IEEE80211_BAND_2GHZ;
		ret = wl12xx_cmd_build_probe_req(wl, wlvif,
						 wlvif->dev_role_id, band,
						 req->ssids[0].ssid,
						 req->ssids[0].ssid_len,
						 ies->ie[band],
						 ies->len[band]);
		if (ret < 0) {
			wl1271_error("2.4GHz PROBE request template failed");
			goto out;
		}
	}

	if (!force_passive && cfg->active[1]) {
		u8 band = IEEE80211_BAND_5GHZ;
		ret = wl12xx_cmd_build_probe_req(wl, wlvif,
						 wlvif->dev_role_id, band,
						 req->ssids[0].ssid,
						 req->ssids[0].ssid_len,
						 ies->ie[band],
						 ies->len[band]);
		if (ret < 0) {
			wl1271_error("5GHz PROBE request template failed");
			goto out;
		}
	}

	wl1271_dump(DEBUG_SCAN, "SCAN_CFG: ", cfg, sizeof(*cfg));

	ret = wl1271_cmd_send(wl, CMD_CONNECTION_SCAN_CFG, cfg,
			      sizeof(*cfg), 0);
	if (ret < 0) {
		wl1271_error("SCAN configuration failed");
		goto out;
	}
out:
	kfree(cfg);
	return ret;
}

int wl1271_scan_sched_scan_start(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct wl1271_cmd_sched_scan_start *start;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd periodic scan start");

	if (wlvif->bss_type != BSS_TYPE_STA_BSS)
		return -EOPNOTSUPP;

	if (test_bit(WLVIF_FLAG_IN_USE, &wlvif->flags))
		return -EBUSY;

	start = kzalloc(sizeof(*start), GFP_KERNEL);
	if (!start)
		return -ENOMEM;

	start->tag = WL1271_SCAN_DEFAULT_TAG;

	ret = wl1271_cmd_send(wl, CMD_START_PERIODIC_SCAN, start,
			      sizeof(*start), 0);
	if (ret < 0) {
		wl1271_error("failed to send scan start command");
		goto out_free;
	}

out_free:
	kfree(start);
	return ret;
}

void wl1271_scan_sched_scan_results(struct wl1271 *wl)
{
	wl1271_debug(DEBUG_SCAN, "got periodic scan results");

	ieee80211_sched_scan_results(wl->hw);
}

void wl1271_scan_sched_scan_stop(struct wl1271 *wl)
{
	struct wl1271_cmd_sched_scan_stop *stop;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd periodic scan stop");

	/* FIXME: what to do if alloc'ing to stop fails? */
	stop = kzalloc(sizeof(*stop), GFP_KERNEL);
	if (!stop) {
		wl1271_error("failed to alloc memory to send sched scan stop");
		return;
	}

	stop->tag = WL1271_SCAN_DEFAULT_TAG;

	ret = wl1271_cmd_send(wl, CMD_STOP_PERIODIC_SCAN, stop,
			      sizeof(*stop), 0);
	if (ret < 0) {
		wl1271_error("failed to send sched scan stop command");
		goto out_free;
	}

out_free:
	kfree(stop);
}
