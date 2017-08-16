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
#include "testmode.h"

#include <linux/slab.h>
#include <net/genetlink.h>

#include "wlcore.h"
#include "debug.h"
#include "acx.h"
#include "ps.h"
#include "io.h"

#define WL1271_TM_MAX_DATA_LENGTH 1024

enum wl1271_tm_commands {
	WL1271_TM_CMD_UNSPEC,
	WL1271_TM_CMD_TEST,
	WL1271_TM_CMD_INTERROGATE,
	WL1271_TM_CMD_CONFIGURE,
	WL1271_TM_CMD_NVS_PUSH,		/* Not in use. Keep to not break ABI */
	WL1271_TM_CMD_SET_PLT_MODE,
	WL1271_TM_CMD_RECOVER,		/* Not in use. Keep to not break ABI */
	WL1271_TM_CMD_GET_MAC,

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

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EINVAL;
		goto out;
	}

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wl1271_cmd_test(wl, buf, buf_len, answer);
	if (ret < 0) {
		wl1271_warning("testmode cmd test failed: %d", ret);
		goto out_sleep;
	}

	if (answer) {
		/* If we got bip calibration answer print radio status */
		struct wl1271_cmd_cal_p2g *params =
			(struct wl1271_cmd_cal_p2g *) buf;

		s16 radio_status = (s16) le16_to_cpu(params->radio_status);

		if (params->test.id == TEST_CMD_P2G_CAL &&
		    radio_status < 0)
			wl1271_warning("testmode cmd: radio status=%d",
					radio_status);
		else
			wl1271_info("testmode cmd: radio status=%d",
					radio_status);

		len = nla_total_size(buf_len);
		skb = cfg80211_testmode_alloc_reply_skb(wl->hw->wiphy, len);
		if (!skb) {
			ret = -ENOMEM;
			goto out_sleep;
		}

		if (nla_put(skb, WL1271_TM_ATTR_DATA, buf_len, buf)) {
			kfree_skb(skb);
			ret = -EMSGSIZE;
			goto out_sleep;
		}

		ret = cfg80211_testmode_reply(skb);
		if (ret < 0)
			goto out_sleep;
	}

out_sleep:
	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);

	return ret;
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

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON)) {
		ret = -EINVAL;
		goto out;
	}

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out_sleep;
	}

	ret = wl1271_cmd_interrogate(wl, ie_id, cmd,
				     sizeof(struct acx_header), sizeof(*cmd));
	if (ret < 0) {
		wl1271_warning("testmode cmd interrogate failed: %d", ret);
		goto out_free;
	}

	skb = cfg80211_testmode_alloc_reply_skb(wl->hw->wiphy, sizeof(*cmd));
	if (!skb) {
		ret = -ENOMEM;
		goto out_free;
	}

	if (nla_put(skb, WL1271_TM_ATTR_DATA, sizeof(*cmd), cmd)) {
		kfree_skb(skb);
		ret = -EMSGSIZE;
		goto out_free;
	}

	ret = cfg80211_testmode_reply(skb);
	if (ret < 0)
		goto out_free;

out_free:
	kfree(cmd);
out_sleep:
	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);

	return ret;
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

static int wl1271_tm_detect_fem(struct wl1271 *wl, struct nlattr *tb[])
{
	/* return FEM type */
	int ret, len;
	struct sk_buff *skb;

	ret = wl1271_plt_start(wl, PLT_FEM_DETECT);
	if (ret < 0)
		goto out;

	mutex_lock(&wl->mutex);

	len = nla_total_size(sizeof(wl->fem_manuf));
	skb = cfg80211_testmode_alloc_reply_skb(wl->hw->wiphy, len);
	if (!skb) {
		ret = -ENOMEM;
		goto out_mutex;
	}

	if (nla_put(skb, WL1271_TM_ATTR_DATA, sizeof(wl->fem_manuf),
					      &wl->fem_manuf)) {
		kfree_skb(skb);
		ret = -EMSGSIZE;
		goto out_mutex;
	}

	ret = cfg80211_testmode_reply(skb);

out_mutex:
	mutex_unlock(&wl->mutex);

	/* We always stop plt after DETECT mode */
	wl1271_plt_stop(wl);
out:
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
	case PLT_OFF:
		ret = wl1271_plt_stop(wl);
		break;
	case PLT_ON:
	case PLT_CHIP_AWAKE:
		ret = wl1271_plt_start(wl, val);
		break;
	case PLT_FEM_DETECT:
		ret = wl1271_tm_detect_fem(wl, tb);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int wl12xx_tm_cmd_get_mac(struct wl1271 *wl, struct nlattr *tb[])
{
	struct sk_buff *skb;
	u8 mac_addr[ETH_ALEN];
	int ret = 0;

	mutex_lock(&wl->mutex);

	if (!wl->plt) {
		ret = -EINVAL;
		goto out;
	}

	if (wl->fuse_oui_addr == 0 && wl->fuse_nic_addr == 0) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	mac_addr[0] = (u8)(wl->fuse_oui_addr >> 16);
	mac_addr[1] = (u8)(wl->fuse_oui_addr >> 8);
	mac_addr[2] = (u8) wl->fuse_oui_addr;
	mac_addr[3] = (u8)(wl->fuse_nic_addr >> 16);
	mac_addr[4] = (u8)(wl->fuse_nic_addr >> 8);
	mac_addr[5] = (u8) wl->fuse_nic_addr;

	skb = cfg80211_testmode_alloc_reply_skb(wl->hw->wiphy, ETH_ALEN);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}

	if (nla_put(skb, WL1271_TM_ATTR_DATA, ETH_ALEN, mac_addr)) {
		kfree_skb(skb);
		ret = -EMSGSIZE;
		goto out;
	}

	ret = cfg80211_testmode_reply(skb);
	if (ret < 0)
		goto out;

out:
	mutex_unlock(&wl->mutex);
	return ret;
}

int wl1271_tm_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  void *data, int len)
{
	struct wl1271 *wl = hw->priv;
	struct nlattr *tb[WL1271_TM_ATTR_MAX + 1];
	u32 nla_cmd;
	int err;

	err = nla_parse(tb, WL1271_TM_ATTR_MAX, data, len, wl1271_tm_policy,
			NULL);
	if (err)
		return err;

	if (!tb[WL1271_TM_ATTR_CMD_ID])
		return -EINVAL;

	nla_cmd = nla_get_u32(tb[WL1271_TM_ATTR_CMD_ID]);

	/* Only SET_PLT_MODE is allowed in case of mode PLT_CHIP_AWAKE */
	if (wl->plt_mode == PLT_CHIP_AWAKE &&
	    nla_cmd != WL1271_TM_CMD_SET_PLT_MODE)
		return -EOPNOTSUPP;

	switch (nla_cmd) {
	case WL1271_TM_CMD_TEST:
		return wl1271_tm_cmd_test(wl, tb);
	case WL1271_TM_CMD_INTERROGATE:
		return wl1271_tm_cmd_interrogate(wl, tb);
	case WL1271_TM_CMD_CONFIGURE:
		return wl1271_tm_cmd_configure(wl, tb);
	case WL1271_TM_CMD_SET_PLT_MODE:
		return wl1271_tm_cmd_set_plt_mode(wl, tb);
	case WL1271_TM_CMD_GET_MAC:
		return wl12xx_tm_cmd_get_mac(wl, tb);
	default:
		return -EOPNOTSUPP;
	}
}
