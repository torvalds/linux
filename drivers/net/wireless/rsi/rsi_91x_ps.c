/*
 * Copyright (c) 2014 Redpine Signals Inc.
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

#include <linux/etherdevice.h>
#include <linux/if.h>
#include <linux/version.h>
#include "rsi_debugfs.h"
#include "rsi_mgmt.h"
#include "rsi_common.h"
#include "rsi_ps.h"

char *str_psstate(enum ps_state state)
{
	switch (state) {
	case PS_NONE:
		return "PS_NONE";
	case PS_DISABLE_REQ_SENT:
		return "PS_DISABLE_REQ_SENT";
	case PS_ENABLE_REQ_SENT:
		return "PS_ENABLE_REQ_SENT";
	case PS_ENABLED:
		return "PS_ENABLED";
	default:
		return "INVALID_STATE";
	}
}

static inline void rsi_modify_ps_state(struct rsi_hw *adapter,
				       enum ps_state nstate)
{
	rsi_dbg(INFO_ZONE, "PS state changed %s => %s\n",
		str_psstate(adapter->ps_state),
		str_psstate(nstate));

	adapter->ps_state = nstate;
}

void rsi_default_ps_params(struct rsi_hw *adapter)
{
	struct rsi_ps_info *ps_info = &adapter->ps_info;

	ps_info->enabled = true;
	ps_info->sleep_type = RSI_SLEEP_TYPE_LP;
	ps_info->tx_threshold = 0;
	ps_info->rx_threshold = 0;
	ps_info->tx_hysterisis = 0;
	ps_info->rx_hysterisis = 0;
	ps_info->monitor_interval = 0;
	ps_info->listen_interval = RSI_DEF_LISTEN_INTERVAL;
	ps_info->num_bcns_per_lis_int = 0;
	ps_info->dtim_interval_duration = 0;
	ps_info->num_dtims_per_sleep = 0;
	ps_info->deep_sleep_wakeup_period = RSI_DEF_DS_WAKEUP_PERIOD;
}

void rsi_enable_ps(struct rsi_hw *adapter, struct ieee80211_vif *vif)
{
	if (adapter->ps_state != PS_NONE) {
		rsi_dbg(ERR_ZONE,
			"%s: Cannot accept enable PS in %s state\n",
			__func__, str_psstate(adapter->ps_state));
		return;
	}

	if (rsi_send_ps_request(adapter, true, vif)) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to send PS request to device\n",
			__func__);
		return;
	}

	rsi_modify_ps_state(adapter, PS_ENABLE_REQ_SENT);
}

/* This function is used to disable power save */
void rsi_disable_ps(struct rsi_hw *adapter, struct ieee80211_vif *vif)
{
	if (adapter->ps_state != PS_ENABLED) {
		rsi_dbg(ERR_ZONE,
			"%s: Cannot accept disable PS in %s state\n",
			__func__, str_psstate(adapter->ps_state));
		return;
	}

	if (rsi_send_ps_request(adapter, false, vif)) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to send PS request to device\n",
			__func__);
		return;
	}

	rsi_modify_ps_state(adapter, PS_DISABLE_REQ_SENT);
}

void rsi_conf_uapsd(struct rsi_hw *adapter, struct ieee80211_vif *vif)
{
	int ret;

	if (adapter->ps_state != PS_ENABLED)
		return;

	ret = rsi_send_ps_request(adapter, false, vif);
	if (!ret)
		ret = rsi_send_ps_request(adapter, true, vif);
	if (ret)
		rsi_dbg(ERR_ZONE,
			"%s: Failed to send PS request to device\n",
			__func__);
}

int rsi_handle_ps_confirm(struct rsi_hw *adapter, u8 *msg)
{
	u16 cfm_type = get_unaligned_le16(msg + PS_CONFIRM_INDEX);

	switch (cfm_type) {
	case RSI_SLEEP_REQUEST:
		if (adapter->ps_state == PS_ENABLE_REQ_SENT)
			rsi_modify_ps_state(adapter, PS_ENABLED);
		break;
	case RSI_WAKEUP_REQUEST:
		if (adapter->ps_state == PS_DISABLE_REQ_SENT)
			rsi_modify_ps_state(adapter, PS_NONE);
		break;
	default:
		rsi_dbg(ERR_ZONE,
			"Invalid PS confirm type %x in state %s\n",
			cfm_type, str_psstate(adapter->ps_state));
		return -1;
	}

	return 0;
}

