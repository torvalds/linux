/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <net/netlink.h>
#include <linux/firmware.h>
#include <net/cfg80211.h>
#include "wcn36xx.h"

#include "testmode.h"
#include "testmode_i.h"
#include "hal.h"
#include "smd.h"

static const struct nla_policy wcn36xx_tm_policy[WCN36XX_TM_ATTR_MAX + 1] = {
	[WCN36XX_TM_ATTR_CMD] = { .type = NLA_U16 },
	[WCN36XX_TM_ATTR_DATA] = { .type = NLA_BINARY,
	.len = WCN36XX_TM_DATA_MAX_LEN },
};

struct build_release_number {
	u16 drv_major;
	u16 drv_minor;
	u16 drv_patch;
	u16 drv_build;
	u16 ptt_max;
	u16 ptt_min;
	u16 fw_ver;
} __packed;

static int wcn36xx_tm_cmd_ptt(struct wcn36xx *wcn, struct ieee80211_vif *vif,
			      struct nlattr *tb[])
{
	int ret = 0, buf_len;
	void *buf;
	struct ftm_rsp_msg *msg, *rsp = NULL;
	struct sk_buff *skb;

	if (!tb[WCN36XX_TM_ATTR_DATA])
		return -EINVAL;

	buf = nla_data(tb[WCN36XX_TM_ATTR_DATA]);
	buf_len = nla_len(tb[WCN36XX_TM_ATTR_DATA]);
	msg = (struct ftm_rsp_msg *)buf;

	wcn36xx_dbg(WCN36XX_DBG_TESTMODE,
		    "testmode cmd wmi msg_id 0x%04X msg_len %d buf %pK buf_len %d\n",
		   msg->msg_id, msg->msg_body_length,
		   buf, buf_len);

	wcn36xx_dbg_dump(WCN36XX_DBG_TESTMODE_DUMP, "REQ ", buf, buf_len);

	if (msg->msg_id == MSG_GET_BUILD_RELEASE_NUMBER) {
		struct build_release_number *body =
				(struct build_release_number *)
				msg->msg_response;

		body->drv_major = wcn->fw_major;
		body->drv_minor = wcn->fw_minor;
		body->drv_patch = wcn->fw_version;
		body->drv_build = wcn->fw_revision;
		body->ptt_max = 10;
		body->ptt_min = 0;

		rsp = msg;
		rsp->resp_status = 0;
	} else {
		wcn36xx_dbg(WCN36XX_DBG_TESTMODE,
			    "PPT Request >> HAL size %d\n",
				msg->msg_body_length);

		msg->resp_status = wcn36xx_smd_process_ptt_msg(wcn, vif, msg,
							       msg->msg_body_length, (void *)(&rsp));

		wcn36xx_dbg(WCN36XX_DBG_TESTMODE,
			    "Response status = %d\n",
				msg->resp_status);
		if (rsp)
			wcn36xx_dbg(WCN36XX_DBG_TESTMODE,
				    "PPT Response << HAL size %d\n",
					rsp->msg_body_length);
	}

	if (!rsp) {
		rsp = msg;
		wcn36xx_warn("No response! Echoing request with response status %d\n",
			     rsp->resp_status);
	}
	wcn36xx_dbg_dump(WCN36XX_DBG_TESTMODE_DUMP, "RSP ",
			 rsp, rsp->msg_body_length);

	skb = cfg80211_testmode_alloc_reply_skb(wcn->hw->wiphy,
						nla_total_size(msg->msg_body_length));
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}

	ret = nla_put(skb, WCN36XX_TM_ATTR_DATA, rsp->msg_body_length, rsp);
	if (ret) {
		kfree_skb(skb);
		goto out;
	}

	ret = cfg80211_testmode_reply(skb);

out:
	if (rsp != msg)
		kfree(rsp);

	return ret;
}

int wcn36xx_tm_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   void *data, int len)
{
	struct wcn36xx *wcn = hw->priv;
	struct nlattr *tb[WCN36XX_TM_ATTR_MAX + 1];
	int ret = 0;
	unsigned short attr;

	wcn36xx_dbg_dump(WCN36XX_DBG_TESTMODE_DUMP, "Data:", data, len);
	ret = nla_parse(tb, WCN36XX_TM_ATTR_MAX, data, len,
			wcn36xx_tm_policy, NULL);
	if (ret)
		return ret;

	if (!tb[WCN36XX_TM_ATTR_CMD])
		return -EINVAL;

	attr = nla_get_u16(tb[WCN36XX_TM_ATTR_CMD]);

	if (attr != WCN36XX_TM_CMD_PTT)
		return -EOPNOTSUPP;

	return wcn36xx_tm_cmd_ptt(wcn, vif, tb);
}
