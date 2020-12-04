/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux cfg80211 vendor command/event handlers of DHD
 *
 * Copyright (C) 1999-2019, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_cfg_vendor.c 525516 2015-01-09 23:12:53Z $
 */

#include <linux/vmalloc.h>
#include <linuxver.h>
#include <net/cfg80211.h>
#include <net/netlink.h>

#include <bcmutils.h>
#include <wl_cfg80211.h>
#include <wl_cfgvendor.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhdioctl.h>
#include <brcm_nl80211.h>

#ifdef VENDOR_EXT_SUPPORT
static int dhd_cfgvendor_priv_string_handler(struct wiphy *wiphy,
	struct wireless_dev *wdev, const void  *data, int len)
{
	const struct bcm_nlmsg_hdr *nlioc = data;
	struct net_device *ndev = NULL;
	struct bcm_cfg80211 *cfg;
	struct sk_buff *reply;
	void *buf = NULL, *cur;
	dhd_pub_t *dhd;
	dhd_ioctl_t ioc = { 0 };
	int ret = 0, ret_len, payload, msglen;
	int maxmsglen = PAGE_SIZE - 0x100;
	int8 index;

	WL_TRACE(("entry: cmd = %d\n", nlioc->cmd));
	DHD_ERROR(("entry: cmd = %d\n", nlioc->cmd));

	cfg = wiphy_priv(wiphy);
	dhd = cfg->pub;

	DHD_OS_WAKE_LOCK(dhd);

	/* send to dongle only if we are not waiting for reload already */
	if (dhd->hang_was_sent) {
		WL_ERR(("HANG was sent up earlier\n"));
		DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE(dhd, DHD_EVENT_TIMEOUT_MS);
		DHD_OS_WAKE_UNLOCK(dhd);
		return OSL_ERROR(BCME_DONGLE_DOWN);
	}

	len -= sizeof(struct bcm_nlmsg_hdr);
	ret_len = nlioc->len;
	if (ret_len > 0 || len > 0) {
		if (len > DHD_IOCTL_MAXLEN) {
			WL_ERR(("oversize input buffer %d\n", len));
			len = DHD_IOCTL_MAXLEN;
		}
		if (ret_len > DHD_IOCTL_MAXLEN) {
			WL_ERR(("oversize return buffer %d\n", ret_len));
			ret_len = DHD_IOCTL_MAXLEN;
		}
		payload = max(ret_len, len) + 1;
		buf = vzalloc(payload);
		if (!buf) {
			DHD_OS_WAKE_UNLOCK(dhd);
			return -ENOMEM;
		}
		memcpy(buf, (void *)nlioc + nlioc->offset, len);
		*(char *)(buf + len) = '\0';
	}

	ndev = wdev_to_wlc_ndev(wdev, cfg);
	index = dhd_net2idx(dhd->info, ndev);
	if (index == DHD_BAD_IF) {
		WL_ERR(("Bad ifidx from wdev:%p\n", wdev));
		ret = BCME_ERROR;
		goto done;
	}

	ioc.cmd = nlioc->cmd;
	ioc.len = nlioc->len;
	ioc.set = nlioc->set;
	ioc.driver = nlioc->magic;
	ret = dhd_ioctl_process(dhd, index, &ioc, buf);
	if (ret) {
		WL_TRACE(("dhd_ioctl_process return err %d\n", ret));
		ret = OSL_ERROR(ret);
		goto done;
	}

	cur = buf;
	while (ret_len > 0) {
		msglen = nlioc->len > maxmsglen ? maxmsglen : ret_len;
		ret_len -= msglen;
		payload = msglen + sizeof(msglen);
		reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, payload);
		if (!reply) {
			WL_ERR(("Failed to allocate reply msg\n"));
			ret = -ENOMEM;
			break;
		}

		if (nla_put(reply, BCM_NLATTR_DATA, msglen, cur) ||
			nla_put_u16(reply, BCM_NLATTR_LEN, msglen)) {
			kfree_skb(reply);
			ret = -ENOBUFS;
			break;
		}

		ret = cfg80211_vendor_cmd_reply(reply);
		if (ret) {
			WL_ERR(("testmode reply failed:%d\n", ret));
			break;
		}
		cur += msglen;
	}

done:
	vfree(buf);
	DHD_OS_WAKE_UNLOCK(dhd);
	return ret;
}

const struct wiphy_vendor_command dhd_cfgvendor_cmds [] = {
	{
		{
			.vendor_id = OUI_BRCM,
			.subcmd = BRCM_VENDOR_SCMD_PRIV_STR
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = dhd_cfgvendor_priv_string_handler
	},
};

int cfgvendor_attach(struct wiphy *wiphy)
{
	wiphy->vendor_commands	= dhd_cfgvendor_cmds;
	wiphy->n_vendor_commands = ARRAY_SIZE(dhd_cfgvendor_cmds);

	return 0;
}

int cfgvendor_detach(struct wiphy *wiphy)
{
	wiphy->vendor_commands  = NULL;
	wiphy->n_vendor_commands = 0;

	return 0;
}
#endif /* VENDOR_EXT_SUPPORT */
