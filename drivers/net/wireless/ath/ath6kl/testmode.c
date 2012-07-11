/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
 * Copyright (c) 2011 Qualcomm Atheros, Inc.
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

#include "testmode.h"
#include "debug.h"

#include <net/netlink.h>

enum ath6kl_tm_attr {
	__ATH6KL_TM_ATTR_INVALID	= 0,
	ATH6KL_TM_ATTR_CMD		= 1,
	ATH6KL_TM_ATTR_DATA		= 2,

	/* keep last */
	__ATH6KL_TM_ATTR_AFTER_LAST,
	ATH6KL_TM_ATTR_MAX		= __ATH6KL_TM_ATTR_AFTER_LAST - 1,
};

enum ath6kl_tm_cmd {
	ATH6KL_TM_CMD_TCMD		= 0,
	ATH6KL_TM_CMD_RX_REPORT		= 1,	/* not used anymore */
};

#define ATH6KL_TM_DATA_MAX_LEN		5000

static const struct nla_policy ath6kl_tm_policy[ATH6KL_TM_ATTR_MAX + 1] = {
	[ATH6KL_TM_ATTR_CMD]		= { .type = NLA_U32 },
	[ATH6KL_TM_ATTR_DATA]		= { .type = NLA_BINARY,
					    .len = ATH6KL_TM_DATA_MAX_LEN },
};

void ath6kl_tm_rx_event(struct ath6kl *ar, void *buf, size_t buf_len)
{
	struct sk_buff *skb;

	if (!buf || buf_len == 0)
		return;

	skb = cfg80211_testmode_alloc_event_skb(ar->wiphy, buf_len, GFP_KERNEL);
	if (!skb) {
		ath6kl_warn("failed to allocate testmode rx skb!\n");
		return;
	}
	if (nla_put_u32(skb, ATH6KL_TM_ATTR_CMD, ATH6KL_TM_CMD_TCMD) ||
	    nla_put(skb, ATH6KL_TM_ATTR_DATA, buf_len, buf))
		goto nla_put_failure;
	cfg80211_testmode_event(skb, GFP_KERNEL);
	return;

nla_put_failure:
	kfree_skb(skb);
	ath6kl_warn("nla_put failed on testmode rx skb!\n");
}

int ath6kl_tm_cmd(struct wiphy *wiphy, void *data, int len)
{
	struct ath6kl *ar = wiphy_priv(wiphy);
	struct nlattr *tb[ATH6KL_TM_ATTR_MAX + 1];
	int err, buf_len;
	void *buf;

	err = nla_parse(tb, ATH6KL_TM_ATTR_MAX, data, len,
			ath6kl_tm_policy);
	if (err)
		return err;

	if (!tb[ATH6KL_TM_ATTR_CMD])
		return -EINVAL;

	switch (nla_get_u32(tb[ATH6KL_TM_ATTR_CMD])) {
	case ATH6KL_TM_CMD_TCMD:
		if (!tb[ATH6KL_TM_ATTR_DATA])
			return -EINVAL;

		buf = nla_data(tb[ATH6KL_TM_ATTR_DATA]);
		buf_len = nla_len(tb[ATH6KL_TM_ATTR_DATA]);

		ath6kl_wmi_test_cmd(ar->wmi, buf, buf_len);

		return 0;

		break;
	case ATH6KL_TM_CMD_RX_REPORT:
	default:
		return -EOPNOTSUPP;
	}
}
