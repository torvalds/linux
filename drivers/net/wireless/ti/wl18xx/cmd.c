/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2011 Texas Instruments Inc.
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

#include "../wlcore/cmd.h"
#include "../wlcore/debug.h"
#include "../wlcore/hw_ops.h"

#include "cmd.h"

int wl18xx_cmd_channel_switch(struct wl1271 *wl,
			      struct wl12xx_vif *wlvif,
			      struct ieee80211_channel_switch *ch_switch)
{
	struct wl18xx_cmd_channel_switch *cmd;
	u32 supported_rates;
	int ret;

	wl1271_debug(DEBUG_ACX, "cmd channel switch (count=%d)",
		     ch_switch->count);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->role_id = wlvif->role_id;
	cmd->channel = ch_switch->chandef.chan->hw_value;
	cmd->switch_time = ch_switch->count;
	cmd->stop_tx = ch_switch->block_tx;

	switch (ch_switch->chandef.chan->band) {
	case NL80211_BAND_2GHZ:
		cmd->band = WLCORE_BAND_2_4GHZ;
		break;
	case NL80211_BAND_5GHZ:
		cmd->band = WLCORE_BAND_5GHZ;
		break;
	default:
		wl1271_error("invalid channel switch band: %d",
			     ch_switch->chandef.chan->band);
		ret = -EINVAL;
		goto out_free;
	}

	supported_rates = CONF_TX_ENABLED_RATES | CONF_TX_MCS_RATES;
	if (wlvif->bss_type == BSS_TYPE_STA_BSS)
		supported_rates |= wlcore_hw_sta_get_ap_rate_mask(wl, wlvif);
	else
		supported_rates |=
			wlcore_hw_ap_get_mimo_wide_rate_mask(wl, wlvif);
	if (wlvif->p2p)
		supported_rates &= ~CONF_TX_CCK_RATES;
	cmd->local_supported_rates = cpu_to_le32(supported_rates);
	cmd->channel_type = wlvif->channel_type;

	ret = wl1271_cmd_send(wl, CMD_CHANNEL_SWITCH, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send channel switch command");
		goto out_free;
	}

out_free:
	kfree(cmd);
out:
	return ret;
}

int wl18xx_cmd_smart_config_start(struct wl1271 *wl, u32 group_bitmap)
{
	struct wl18xx_cmd_smart_config_start *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd smart config start group_bitmap=0x%x",
		     group_bitmap);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->group_id_bitmask = cpu_to_le32(group_bitmap);

	ret = wl1271_cmd_send(wl, CMD_SMART_CONFIG_START, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send smart config start command");
		goto out_free;
	}

out_free:
	kfree(cmd);
out:
	return ret;
}

int wl18xx_cmd_smart_config_stop(struct wl1271 *wl)
{
	struct wl1271_cmd_header *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd smart config stop");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl1271_cmd_send(wl, CMD_SMART_CONFIG_STOP, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send smart config stop command");
		goto out_free;
	}

out_free:
	kfree(cmd);
out:
	return ret;
}

int wl18xx_cmd_smart_config_set_group_key(struct wl1271 *wl, u16 group_id,
					  u8 key_len, u8 *key)
{
	struct wl18xx_cmd_smart_config_set_group_key *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd smart config set group key id=0x%x",
		     group_id);

	if (key_len != sizeof(cmd->key)) {
		wl1271_error("invalid group key size: %d", key_len);
		return -E2BIG;
	}

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->group_id = cpu_to_le32(group_id);
	memcpy(cmd->key, key, key_len);

	ret = wl1271_cmd_send(wl, CMD_SMART_CONFIG_SET_GROUP_KEY, cmd,
			      sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send smart config set group key cmd");
		goto out_free;
	}

out_free:
	kfree(cmd);
out:
	return ret;
}

int wl18xx_cmd_set_cac(struct wl1271 *wl, struct wl12xx_vif *wlvif, bool start)
{
	struct wlcore_cmd_cac_start *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd cac (channel %d) %s",
		     wlvif->channel, start ? "start" : "stop");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->role_id = wlvif->role_id;
	cmd->channel = wlvif->channel;
	if (wlvif->band == NL80211_BAND_5GHZ)
		cmd->band = WLCORE_BAND_5GHZ;
	cmd->bandwidth = wlcore_get_native_channel_type(wlvif->channel_type);

	ret = wl1271_cmd_send(wl,
			      start ? CMD_CAC_START : CMD_CAC_STOP,
			      cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send cac command");
		goto out_free;
	}

out_free:
	kfree(cmd);
	return ret;
}

int wl18xx_cmd_radar_detection_debug(struct wl1271 *wl, u8 channel)
{
	struct wl18xx_cmd_dfs_radar_debug *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd radar detection debug (chan %d)",
		     channel);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->channel = channel;

	ret = wl1271_cmd_send(wl, CMD_DFS_RADAR_DETECTION_DEBUG,
			      cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send radar detection debug command");
		goto out_free;
	}

out_free:
	kfree(cmd);
	return ret;
}

int wl18xx_cmd_dfs_master_restart(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct wl18xx_cmd_dfs_master_restart *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd dfs master restart (role %d)",
		     wlvif->role_id);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->role_id = wlvif->role_id;

	ret = wl1271_cmd_send(wl, CMD_DFS_MASTER_RESTART,
			      cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send dfs master restart command");
		goto out_free;
	}
out_free:
	kfree(cmd);
	return ret;
}
