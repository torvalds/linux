/*
 * This file is part of wlcore
 *
 * Copyright (C) 2014 Texas Instruments. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <net/mac80211.h>
#include <net/netlink.h>

#include "wlcore.h"
#include "debug.h"
#include "ps.h"
#include "hw_ops.h"
#include "vendor_cmd.h"

static const
struct nla_policy wlcore_vendor_attr_policy[NUM_WLCORE_VENDOR_ATTR] = {
	[WLCORE_VENDOR_ATTR_FREQ]		= { .type = NLA_U32 },
	[WLCORE_VENDOR_ATTR_GROUP_ID]		= { .type = NLA_U32 },
	[WLCORE_VENDOR_ATTR_GROUP_KEY]		= { .type = NLA_BINARY,
						    .len = WLAN_MAX_KEY_LEN },
};

static int
wlcore_vendor_cmd_smart_config_start(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     const void *data, int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct wl1271 *wl = hw->priv;
	struct nlattr *tb[NUM_WLCORE_VENDOR_ATTR];
	int ret;

	wl1271_debug(DEBUG_CMD, "vendor cmd smart config start");

	if (!data)
		return -EINVAL;

	ret = nla_parse(tb, MAX_WLCORE_VENDOR_ATTR, data, data_len,
			wlcore_vendor_attr_policy);
	if (ret)
		return ret;

	if (!tb[WLCORE_VENDOR_ATTR_GROUP_ID])
		return -EINVAL;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EINVAL;
		goto out;
	}

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wlcore_smart_config_start(wl,
			nla_get_u32(tb[WLCORE_VENDOR_ATTR_GROUP_ID]));

	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);

	return 0;
}

static int
wlcore_vendor_cmd_smart_config_stop(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    const void *data, int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct wl1271 *wl = hw->priv;
	int ret;

	wl1271_debug(DEBUG_CMD, "testmode cmd smart config stop");

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EINVAL;
		goto out;
	}

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wlcore_smart_config_stop(wl);

	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static int
wlcore_vendor_cmd_smart_config_set_group_key(struct wiphy *wiphy,
					     struct wireless_dev *wdev,
					     const void *data, int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct wl1271 *wl = hw->priv;
	struct nlattr *tb[NUM_WLCORE_VENDOR_ATTR];
	int ret;

	wl1271_debug(DEBUG_CMD, "testmode cmd smart config set group key");

	if (!data)
		return -EINVAL;

	ret = nla_parse(tb, MAX_WLCORE_VENDOR_ATTR, data, data_len,
			wlcore_vendor_attr_policy);
	if (ret)
		return ret;

	if (!tb[WLCORE_VENDOR_ATTR_GROUP_ID] ||
	    !tb[WLCORE_VENDOR_ATTR_GROUP_KEY])
		return -EINVAL;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EINVAL;
		goto out;
	}

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wlcore_smart_config_set_group_key(wl,
			nla_get_u32(tb[WLCORE_VENDOR_ATTR_GROUP_ID]),
			nla_len(tb[WLCORE_VENDOR_ATTR_GROUP_KEY]),
			nla_data(tb[WLCORE_VENDOR_ATTR_GROUP_KEY]));

	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static const struct wiphy_vendor_command wlcore_vendor_commands[] = {
	{
		.info = {
			.vendor_id = TI_OUI,
			.subcmd = WLCORE_VENDOR_CMD_SMART_CONFIG_START,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = wlcore_vendor_cmd_smart_config_start,
	},
	{
		.info = {
			.vendor_id = TI_OUI,
			.subcmd = WLCORE_VENDOR_CMD_SMART_CONFIG_STOP,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = wlcore_vendor_cmd_smart_config_stop,
	},
	{
		.info = {
			.vendor_id = TI_OUI,
			.subcmd = WLCORE_VENDOR_CMD_SMART_CONFIG_SET_GROUP_KEY,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = wlcore_vendor_cmd_smart_config_set_group_key,
	},
};

static const struct nl80211_vendor_cmd_info wlcore_vendor_events[] = {
	{
		.vendor_id = TI_OUI,
		.subcmd = WLCORE_VENDOR_EVENT_SC_SYNC,
	},
	{
		.vendor_id = TI_OUI,
		.subcmd = WLCORE_VENDOR_EVENT_SC_DECODE,
	},
};

void wlcore_set_vendor_commands(struct wiphy *wiphy)
{
	wiphy->vendor_commands = wlcore_vendor_commands;
	wiphy->n_vendor_commands = ARRAY_SIZE(wlcore_vendor_commands);
	wiphy->vendor_events = wlcore_vendor_events;
	wiphy->n_vendor_events = ARRAY_SIZE(wlcore_vendor_events);
}
