/*
 * This file is part of wl12xx
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
#include "../wlcore/tx.h"

static int wl1271_get_scan_channels(struct wl1271 *wl,
				    struct cfg80211_scan_request *req,
				    struct basic_scan_channel_params *channels,
				    enum nl80211_band band, bool passive)
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
		    (passive || !(flags & IEEE80211_CHAN_NO_IR))) {
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
					cpu_to_le32(c->dwell_time_passive);
				channels[j].max_duration =
					cpu_to_le32(c->dwell_time_passive);
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

static int wl1271_scan_send(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			    enum nl80211_band band,
			    bool passive, u32 basic_rate)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);
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

	/* scan on the dev role if the regular one is not started */
	if (wlcore_is_p2p_mgmt(wlvif))
		cmd->params.role_id = wlvif->dev_role_id;
	else
		cmd->params.role_id = wlvif->role_id;

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

	if (band == NL80211_BAND_2GHZ)
		cmd->params.band = WL1271_SCAN_BAND_2_4_GHZ;
	else
		cmd->params.band = WL1271_SCAN_BAND_5_GHZ;

	if (wl->scan.ssid_len) {
		cmd->params.ssid_len = wl->scan.ssid_len;
		memcpy(cmd->params.ssid, wl->scan.ssid, wl->scan.ssid_len);
	}

	memcpy(cmd->addr, vif->addr, ETH_ALEN);

	ret = wl12xx_cmd_build_probe_req(wl, wlvif,
					 cmd->params.role_id, band,
					 wl->scan.ssid, wl->scan.ssid_len,
					 wl->scan.req->ie,
					 wl->scan.req->ie_len, NULL, 0, false);
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

int wl12xx_scan_stop(struct wl1271 *wl, struct wl12xx_vif *wlvif)
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

void wl1271_scan_stm(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	int ret = 0;
	enum nl80211_band band;
	u32 rate, mask;

	switch (wl->scan.state) {
	case WL1271_SCAN_STATE_IDLE:
		break;

	case WL1271_SCAN_STATE_2GHZ_ACTIVE:
		band = NL80211_BAND_2GHZ;
		mask = wlvif->bitrate_masks[band];
		if (wl->scan.req->no_cck) {
			mask &= ~CONF_TX_CCK_RATES;
			if (!mask)
				mask = CONF_TX_RATE_MASK_BASIC_P2P;
		}
		rate = wl1271_tx_min_rate_get(wl, mask);
		ret = wl1271_scan_send(wl, wlvif, band, false, rate);
		if (ret == WL1271_NOTHING_TO_SCAN) {
			wl->scan.state = WL1271_SCAN_STATE_2GHZ_PASSIVE;
			wl1271_scan_stm(wl, wlvif);
		}

		break;

	case WL1271_SCAN_STATE_2GHZ_PASSIVE:
		band = NL80211_BAND_2GHZ;
		mask = wlvif->bitrate_masks[band];
		if (wl->scan.req->no_cck) {
			mask &= ~CONF_TX_CCK_RATES;
			if (!mask)
				mask = CONF_TX_RATE_MASK_BASIC_P2P;
		}
		rate = wl1271_tx_min_rate_get(wl, mask);
		ret = wl1271_scan_send(wl, wlvif, band, true, rate);
		if (ret == WL1271_NOTHING_TO_SCAN) {
			if (wl->enable_11a)
				wl->scan.state = WL1271_SCAN_STATE_5GHZ_ACTIVE;
			else
				wl->scan.state = WL1271_SCAN_STATE_DONE;
			wl1271_scan_stm(wl, wlvif);
		}

		break;

	case WL1271_SCAN_STATE_5GHZ_ACTIVE:
		band = NL80211_BAND_5GHZ;
		rate = wl1271_tx_min_rate_get(wl, wlvif->bitrate_masks[band]);
		ret = wl1271_scan_send(wl, wlvif, band, false, rate);
		if (ret == WL1271_NOTHING_TO_SCAN) {
			wl->scan.state = WL1271_SCAN_STATE_5GHZ_PASSIVE;
			wl1271_scan_stm(wl, wlvif);
		}

		break;

	case WL1271_SCAN_STATE_5GHZ_PASSIVE:
		band = NL80211_BAND_5GHZ;
		rate = wl1271_tx_min_rate_get(wl, wlvif->bitrate_masks[band]);
		ret = wl1271_scan_send(wl, wlvif, band, true, rate);
		if (ret == WL1271_NOTHING_TO_SCAN) {
			wl->scan.state = WL1271_SCAN_STATE_DONE;
			wl1271_scan_stm(wl, wlvif);
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

static void wl12xx_adjust_channels(struct wl1271_cmd_sched_scan_config *cmd,
				   struct wlcore_scan_channels *cmd_channels)
{
	memcpy(cmd->passive, cmd_channels->passive, sizeof(cmd->passive));
	memcpy(cmd->active, cmd_channels->active, sizeof(cmd->active));
	cmd->dfs = cmd_channels->dfs;
	cmd->n_pactive_ch = cmd_channels->passive_active;

	memcpy(cmd->channels_2, cmd_channels->channels_2,
	       sizeof(cmd->channels_2));
	memcpy(cmd->channels_5, cmd_channels->channels_5,
	       sizeof(cmd->channels_5));
	/* channels_4 are not supported, so no need to copy them */
}

int wl1271_scan_sched_scan_config(struct wl1271 *wl,
				  struct wl12xx_vif *wlvif,
				  struct cfg80211_sched_scan_request *req,
				  struct ieee80211_scan_ies *ies)
{
	struct wl1271_cmd_sched_scan_config *cfg = NULL;
	struct wlcore_scan_channels *cfg_channels = NULL;
	struct conf_sched_scan_settings *c = &wl->conf.sched_scan;
	int i, ret;
	bool force_passive = !req->n_ssids;

	wl1271_debug(DEBUG_CMD, "cmd sched_scan scan config");

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	cfg->role_id = wlvif->role_id;
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
		cfg->intervals[i] = cpu_to_le32(req->scan_plans[0].interval *
						MSEC_PER_SEC);

	cfg->ssid_len = 0;
	ret = wlcore_scan_sched_scan_ssid_list(wl, wlvif, req);
	if (ret < 0)
		goto out;

	cfg->filter_type = ret;

	wl1271_debug(DEBUG_SCAN, "filter_type = %d", cfg->filter_type);

	cfg_channels = kzalloc(sizeof(*cfg_channels), GFP_KERNEL);
	if (!cfg_channels) {
		ret = -ENOMEM;
		goto out;
	}

	if (!wlcore_set_scan_chan_params(wl, cfg_channels, req->channels,
					 req->n_channels, req->n_ssids,
					 SCAN_TYPE_PERIODIC)) {
		wl1271_error("scan channel list is empty");
		ret = -EINVAL;
		goto out;
	}
	wl12xx_adjust_channels(cfg, cfg_channels);

	if (!force_passive && cfg->active[0]) {
		u8 band = NL80211_BAND_2GHZ;
		ret = wl12xx_cmd_build_probe_req(wl, wlvif,
						 wlvif->role_id, band,
						 req->ssids[0].ssid,
						 req->ssids[0].ssid_len,
						 ies->ies[band],
						 ies->len[band],
						 ies->common_ies,
						 ies->common_ie_len,
						 true);
		if (ret < 0) {
			wl1271_error("2.4GHz PROBE request template failed");
			goto out;
		}
	}

	if (!force_passive && cfg->active[1]) {
		u8 band = NL80211_BAND_5GHZ;
		ret = wl12xx_cmd_build_probe_req(wl, wlvif,
						 wlvif->role_id, band,
						 req->ssids[0].ssid,
						 req->ssids[0].ssid_len,
						 ies->ies[band],
						 ies->len[band],
						 ies->common_ies,
						 ies->common_ie_len,
						 true);
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
	kfree(cfg_channels);
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

	if ((wl->quirks & WLCORE_QUIRK_NO_SCHED_SCAN_WHILE_CONN) &&
	    test_bit(WLVIF_FLAG_IN_USE, &wlvif->flags))
		return -EBUSY;

	start = kzalloc(sizeof(*start), GFP_KERNEL);
	if (!start)
		return -ENOMEM;

	start->role_id = wlvif->role_id;
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

int wl12xx_sched_scan_start(struct wl1271 *wl, struct wl12xx_vif  *wlvif,
			    struct cfg80211_sched_scan_request *req,
			    struct ieee80211_scan_ies *ies)
{
	int ret;

	ret = wl1271_scan_sched_scan_config(wl, wlvif, req, ies);
	if (ret < 0)
		return ret;

	return wl1271_scan_sched_scan_start(wl, wlvif);
}

void wl12xx_scan_sched_scan_stop(struct wl1271 *wl,  struct wl12xx_vif *wlvif)
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

	stop->role_id = wlvif->role_id;
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

int wl12xx_scan_start(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		      struct cfg80211_scan_request *req)
{
	wl1271_scan_stm(wl, wlvif);
	return 0;
}

void wl12xx_scan_completed(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	wl1271_scan_stm(wl, wlvif);
}
