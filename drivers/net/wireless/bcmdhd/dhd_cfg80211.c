/*
 * Linux cfg80211 driver - Dongle Host Driver (DHD) related
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 * $Id: wl_cfg80211.c,v 1.1.4.1.2.14 2011/02/09 01:40:07 Exp $
 */

#include <linux/vmalloc.h>
#include <net/rtnetlink.h>

#include <bcmutils.h>
#include <wldev_common.h>
#include <wl_cfg80211.h>
#include <brcm_nl80211.h>
#include <dhd_cfg80211.h>

#ifdef PKT_FILTER_SUPPORT
#include <dngl_stats.h>
#include <dhd.h>
#endif
extern struct bcm_cfg80211 *g_bcm_cfg;

#ifdef PKT_FILTER_SUPPORT
extern uint dhd_pkt_filter_enable;
extern uint dhd_master_mode;
extern void dhd_pktfilter_offload_enable(dhd_pub_t * dhd, char *arg, int enable, int master_mode);
#endif

static int dhd_dongle_up = FALSE;

#include <dngl_stats.h>
#include <dhd.h>
#include <dhdioctl.h>
#include <wlioctl.h>
#include <dhd_cfg80211.h>

static s32 wl_dongle_up(struct net_device *ndev, u32 up);

/**
 * Function implementations
 */

s32 dhd_cfg80211_init(struct bcm_cfg80211 *cfg)
{
	dhd_dongle_up = FALSE;
	return 0;
}

s32 dhd_cfg80211_deinit(struct bcm_cfg80211 *cfg)
{
	dhd_dongle_up = FALSE;
	return 0;
}

s32 dhd_cfg80211_down(struct bcm_cfg80211 *cfg)
{
	dhd_dongle_up = FALSE;
	return 0;
}

s32 dhd_cfg80211_set_p2p_info(struct bcm_cfg80211 *cfg, int val)
{
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
	dhd->op_mode |= val;
	WL_ERR(("Set : op_mode=0x%04x\n", dhd->op_mode));
#ifdef ARP_OFFLOAD_SUPPORT
	if (dhd->arp_version == 1) {
		/* IF P2P is enabled, disable arpoe */
		dhd_arp_offload_set(dhd, 0);
		dhd_arp_offload_enable(dhd, false);
	}
#endif /* ARP_OFFLOAD_SUPPORT */

	return 0;
}

s32 dhd_cfg80211_clean_p2p_info(struct bcm_cfg80211 *cfg)
{
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
	dhd->op_mode &= ~(DHD_FLAG_P2P_GC_MODE | DHD_FLAG_P2P_GO_MODE);
	WL_ERR(("Clean : op_mode=0x%04x\n", dhd->op_mode));

#ifdef ARP_OFFLOAD_SUPPORT
	if (dhd->arp_version == 1) {
		/* IF P2P is disabled, enable arpoe back for STA mode. */
		dhd_arp_offload_set(dhd, dhd_arp_mode);
		dhd_arp_offload_enable(dhd, true);
	}
#endif /* ARP_OFFLOAD_SUPPORT */

	return 0;
}

struct net_device* wl_cfg80211_allocate_if(struct bcm_cfg80211 *cfg, int ifidx, char *name,
	uint8 *mac, uint8 bssidx)
{
	return dhd_allocate_if(cfg->pub, ifidx, name, mac, bssidx, FALSE);
}

int wl_cfg80211_register_if(struct bcm_cfg80211 *cfg, int ifidx, struct net_device* ndev)
{
	return dhd_register_if(cfg->pub, ifidx, FALSE);
}

int wl_cfg80211_remove_if(struct bcm_cfg80211 *cfg, int ifidx, struct net_device* ndev)
{
	return dhd_remove_if(cfg->pub, ifidx, FALSE);
}

struct net_device * dhd_cfg80211_netdev_free(struct net_device *ndev)
{
	if (ndev) {
		if (ndev->ieee80211_ptr) {
			kfree(ndev->ieee80211_ptr);
			ndev->ieee80211_ptr = NULL;
		}
		free_netdev(ndev);
		return NULL;
	}

	return ndev;
}

void dhd_netdev_free(struct net_device *ndev)
{
#ifdef WL_CFG80211
	ndev = dhd_cfg80211_netdev_free(ndev);
#endif
	if (ndev)
		free_netdev(ndev);
}

static s32 wl_dongle_up(struct net_device *ndev, u32 up)
{
	s32 err = 0;

	err = wldev_ioctl(ndev, WLC_UP, &up, sizeof(up), true);
	if (unlikely(err)) {
		WL_ERR(("WLC_UP error (%d)\n", err));
	}
	return err;
}

s32 dhd_config_dongle(struct bcm_cfg80211 *cfg)
{
#ifndef DHD_SDALIGN
#define DHD_SDALIGN	32
#endif
	struct net_device *ndev;
	s32 err = 0;

	WL_TRACE(("In\n"));
	if (dhd_dongle_up) {
		WL_ERR(("Dongle is already up\n"));
		return err;
	}

	ndev = bcmcfg_to_prmry_ndev(cfg);

	err = wl_dongle_up(ndev, 0);
	if (unlikely(err)) {
		WL_ERR(("wl_dongle_up failed\n"));
		goto default_conf_out;
	}
	dhd_dongle_up = true;

default_conf_out:

	return err;

}

#ifdef CONFIG_NL80211_TESTMODE
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
int dhd_cfg80211_testmode_cmd(struct wiphy *wiphy, struct wireless_dev *wdev, void *data, int len)
#else
int dhd_cfg80211_testmode_cmd(struct wiphy *wiphy, void *data, int len)
#endif  /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */
{
	struct sk_buff *reply;
	struct bcm_cfg80211 *cfg;
	dhd_pub_t *dhd;
	struct bcm_nlmsg_hdr *nlioc = data;
	dhd_ioctl_t ioc = { 0 };
	int err = 0;
	void *buf = NULL, *cur;
	u16 buflen;
	u16 maxmsglen = PAGE_SIZE - 0x100;
	bool newbuf = false;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	int8 index = 0;
	struct net_device *ndev = NULL;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */

	WL_TRACE(("entry: cmd = %d\n", nlioc->cmd));
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

	if (nlioc->len > 0) {
		if (nlioc->len <= len) {
			buf = (void *)nlioc + nlioc->offset;
			*(char *)(buf + nlioc->len) = '\0';
		} else {
			if (nlioc->len > DHD_IOCTL_MAXLEN)
				nlioc->len = DHD_IOCTL_MAXLEN;
			buf = vzalloc(nlioc->len);
			if (!buf)
				return -ENOMEM;
			newbuf = true;
			memcpy(buf, (void *)nlioc + nlioc->offset, len);
			*(char *)(buf + len) = '\0';
		}
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	ndev = wdev_to_wlc_ndev(wdev, cfg);
	index = dhd_net2idx(dhd->info, ndev);
	if (index == DHD_BAD_IF) {
	WL_ERR(("Bad ifidx from wdev:%p\n", wdev));
		return BCME_ERROR;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */

	ioc.cmd = nlioc->cmd;
	ioc.len = nlioc->len;
	ioc.set = nlioc->set;
	ioc.driver = nlioc->magic;
	err = dhd_ioctl_process(dhd, 0, &ioc, buf);
	if (err) {
		WL_TRACE(("dhd_ioctl_process return err %d\n", err));
		err = OSL_ERROR(err);
		goto done;
	}

	cur = buf;
	while (nlioc->len > 0) {
		buflen = nlioc->len > maxmsglen ? maxmsglen : nlioc->len;
		nlioc->len -= buflen;
		reply = cfg80211_testmode_alloc_reply_skb(wiphy, buflen+4);
		if (!reply) {
			WL_ERR(("Failed to allocate reply msg\n"));
			err = -ENOMEM;
			break;
		}

		if (nla_put(reply, BCM_NLATTR_DATA, buflen, cur) ||
			nla_put_u16(reply, BCM_NLATTR_LEN, buflen)) {
			kfree_skb(reply);
			err = -ENOBUFS;
			break;
		}

		do {
			err = cfg80211_testmode_reply(reply);
		} while (err == -EAGAIN);
		if (err) {
			WL_ERR(("testmode reply failed:%d\n", err));
			break;
		}
		cur += buflen;
	}

done:
	if (newbuf)
		vfree(buf);
	DHD_OS_WAKE_UNLOCK(dhd);
	return err;
}
#endif /* CONFIG_NL80211_TESTMODE */
