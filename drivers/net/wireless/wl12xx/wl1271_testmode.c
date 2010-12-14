/*
 * This file is part of wl1271
 *
 * Copyright (C) 2010 Nokia Corporation
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
#include "wl1271_testmode.h"

#include <linux/slab.h>
#include <net/genetlink.h>

#include "wl1271.h"
#include "wl1271_acx.h"

#define WL1271_TM_MAX_DATA_LENGTH 1024

enum wl1271_tm_commands {
	WL1271_TM_CMD_UNSPEC,
	WL1271_TM_CMD_TEST,
	WL1271_TM_CMD_INTERROGATE,
	WL1271_TM_CMD_CONFIGURE,
	WL1271_TM_CMD_NVS_PUSH,
	WL1271_TM_CMD_SET_PLT_MODE,

	__WL1271_TM_CMD_AFTER_LAST
};
#define WL1271_TM_CMD_MAX (__WL1271_TM_CMD_AFTER_LAST - 1)

enum wl1271_tm_attrs {
	WL1271_TM_ATTR_UNSPEC,
	WL1271_TM_ATTR_CMD_ID,
	WL1271_TM_ATTR_ANSWER,
	WL1271_TM_ATTR_DATA,
	WL1271_TM_ATTR_IE_ID,
	WL1271_TM_ATTR_PLT_MODE,

	__WL1271_TM_ATTR_AFTER_LAST
};
#define WL1271_TM_ATTR_MAX (__WL1271_TM_ATTR_AFTER_LAST - 1)

static struct nla_policy wl1271_tm_policy[WL1271_TM_ATTR_MAX + 1] = {
	[WL1271_TM_ATTR_CMD_ID] =	{ .type = NLA_U32 },
	[WL1271_TM_ATTR_ANSWER] =	{ .type = NLA_U8 },
	[WL1271_TM_ATTR_DATA] =		{ .type = NLA_BINARY,
					  .len = WL1271_TM_MAX_DATA_LENGTH },
	[WL1271_TM_ATTR_IE_ID] =	{ .type = NLA_U32 },
	[WL1271_TM_ATTR_PLT_MODE] =	{ .type = NLA_U32 },
};


static int wl1271_tm_cmd_test(struct wl1271 *wl, struct nlattr *tb[])
{
	int buf_len, ret, len;
	struct sk_buff *skb;
	void *buf;
	u8 answer = 0;

	wl1271_debug(DEBUG_TESTMODE, "testmode cmd test");

	if (!tb[WL1271_TM_ATTR_DATA])
		return -EINVAL;

	buf = nla_data(tb[WL1271_TM_ATTR_DATA]);
	buf_len = nla_len(tb[WL1271_TM_ATTR_DATA]);

	if (tb[WL1271_TM_ATTR_ANSWER])
		answer = nla_get_u8(tb[WL1271_TM_ATTR_ANSWER]);

	if (buf_len > sizeof(struct wl1271_command))
		return -EMSGSIZE;

	mutex_lock(&wl->mutex);
	ret = wl1271_cmd_test(wl, buf, buf_len, answer);
	mutex_unlock(&wl->mutex);

	if (ret < 0) {
		wl1271_warning("testmode cmd test failed: %d", ret);
		return ret;
	}

	if (answer) {
		len = nla_total_size(buf_len);
		skb = cfg80211_testmode_alloc_reply_skb(wl->hw->wiphy, len);
		if (!skb)
			return -ENOMEM;

		NLA_PUT(skb, WL1271_TM_ATTR_DATA, buf_len, buf);
		ret = cfg80211_testmode_reply(skb);
		if (ret < 0)
			return ret;
	}

	return 0;

nla_put_failure:
	kfree_skb(skb);
	return -EMSGSIZE;
}

static int wl1271_tm_cmd_interrogate(struct wl1271 *wl, struct nlattr *tb[])
{
	int ret;
	struct wl1271_command *cmd;
	struct sk_buff *skb;
	u8 ie_id;

	wl1271_debug(DEBUG_TESTMODE, "testmode cmd interrogate");

	if (!tb[WL1271_TM_ATTR_IE_ID])
		return -EINVAL;

	ie_id = nla_get_u8(tb[WL1271_TM_ATTR_IE_ID]);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	mutex_lock(&wl->mutex);
	ret = wl1271_cmd_interrogate(wl, ie_id, cmd, sizeof(*cmd));
	mutex_unlock(&wl->mutex);

	if (ret < 0) {
		wl1271_warning("testmode cmd interrogate failed: %d", ret);
		return ret;
	}

	skb = cfg80211_testmode_alloc_reply_skb(wl->hw->wiphy, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	NLA_PUT(skb, WL1271_TM_ATTR_DATA, sizeof(*cmd), cmd);

	return 0;

nla_put_failure:
	kfree_skb(skb);
	return -EMSGSIZE;
}

static int wl1271_tm_cmd_configure(struct wl1271 *wl, struct nlattr *tb[])
{
	int buf_len, ret;
	void *buf;
	u8 ie_id;

	wl1271_debug(DEBUG_TESTMODE, "testmode cmd configure");

	if (!tb[WL1271_TM_ATTR_DATA])
		return -EINVAL;
	if (!tb[WL1271_TM_ATTR_IE_ID])
		return -EINVAL;

	ie_id = nla_get_u8(tb[WL1271_TM_ATTR_IE_ID]);
	buf = nla_data(tb[WL1271_TM_ATTR_DATA]);
	buf_len = nla_len(tb[WL1271_TM_ATTR_DATA]);

	if (buf_len > sizeof(struct wl1271_command))
		return -EMSGSIZE;

	mutex_lock(&wl->mutex);
	ret = wl1271_cmd_configure(wl, ie_id, buf, buf_len);
	mutex_unlock(&wl->mutex);

	if (ret < 0) {
		wl1271_warning("testmode cmd configure failed: %d", ret);
		return ret;
	}

	return 0;
}

static int wl1271_tm_cmd_nvs_push(struct wl1271 *wl, struct nlattr *tb[])
{
	int ret = 0;
	size_t len;
	void *buf;

	wl1271_debug(DEBUG_TESTMODE, "testmode cmd nvs push");

	if (!tb[WL1271_TM_ATTR_DATA])
		return -EINVAL;

	buf = nla_data(tb[WL1271_TM_ATTR_DATA]);
	len = nla_len(tb[WL1271_TM_ATTR_DATA]);

	mutex_lock(&wl->mutex);

	kfree(wl->nvs);

	wl->nvs = kzalloc(sizeof(struct wl1271_nvs_file), GFP_KERNEL);
	if (!wl->nvs) {
		wl1271_error("could not allocate memory for the nvs file");
		ret = -ENOMEM;
		goto out;
	}

	memcpy(wl->nvs, buf, len);
	wl->nvs_len = len;

	wl1271_debug(DEBUG_TESTMODE, "testmode pushed nvs");

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static int wl1271_tm_cmd_set_plt_mode(struct wl1271 *wl, struct nlattr *tb[])
{
	u32 val;
	int ret;

	wl1271_debug(DEBUG_TESTMODE, "testmode cmd set plt mode");

	if (!tb[WL1271_TM_ATTR_PLT_MODE])
		return -EINVAL;

	val = nla_get_u32(tb[WL1271_TM_ATTR_PLT_MODE]);

	switch (val) {
	case 0:
		ret = wl1271_plt_stop(wl);
		break;
	case 1:
		ret = wl1271_plt_start(wl);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int wl1271_tm_cmd(struct ieee80211_hw *hw, void *data, int len)
{
	struct wl1271 *wl = hw->priv;
	struct nlattr *tb[WL1271_TM_ATTR_MAX + 1];
	int err;

	err = nla_parse(tb, WL1271_TM_ATTR_MAX, data, len, wl1271_tm_policy);
	if (err)
		return err;

	if (!tb[WL1271_TM_ATTR_CMD_ID])
		return -EINVAL;

	switch (nla_get_u32(tb[WL1271_TM_ATTR_CMD_ID])) {
	case WL1271_TM_CMD_TEST:
		return wl1271_tm_cmd_test(wl, tb);
	case WL1271_TM_CMD_INTERROGATE:
		return wl1271_tm_cmd_interrogate(wl, tb);
	case WL1271_TM_CMD_CONFIGURE:
		return wl1271_tm_cmd_configure(wl, tb);
	case WL1271_TM_CMD_NVS_PUSH:
		return wl1271_tm_cmd_nvs_push(wl, tb);
	case WL1271_TM_CMD_SET_PLT_MODE:
		return wl1271_tm_cmd_set_plt_mode(wl, tb);
	default:
		return -EOPNOTSUPP;
	}
}
