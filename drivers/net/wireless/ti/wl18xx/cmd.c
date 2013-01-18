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

	wl1271_debug(DEBUG_ACX, "cmd channel switch");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->role_id = wlvif->role_id;
	cmd->channel = ch_switch->channel->hw_value;
	cmd->switch_time = ch_switch->count;
	cmd->stop_tx = ch_switch->block_tx;

	switch (ch_switch->channel->band) {
	case IEEE80211_BAND_2GHZ:
		cmd->band = WLCORE_BAND_2_4GHZ;
		break;
	case IEEE80211_BAND_5GHZ:
		cmd->band = WLCORE_BAND_5GHZ;
		break;
	default:
		wl1271_error("invalid channel switch band: %d",
			     ch_switch->channel->band);
		ret = -EINVAL;
		goto out_free;
	}

	supported_rates = CONF_TX_ENABLED_RATES | CONF_TX_MCS_RATES |
			  wlcore_hw_sta_get_ap_rate_mask(wl, wlvif);
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
