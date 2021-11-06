/*
 * Linux cfg80211 driver - Dongle Host Driver (DHD) related
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#include <linux/vmalloc.h>
#include <net/rtnetlink.h>

#include <bcmutils.h>
#include <wldev_common.h>
#include <wl_cfg80211.h>
#include <dhd_cfg80211.h>

#ifdef PKT_FILTER_SUPPORT
#include <dngl_stats.h>
#include <dhd.h>
#endif

#ifdef PKT_FILTER_SUPPORT
extern uint dhd_pkt_filter_enable;
extern uint dhd_master_mode;
extern void dhd_pktfilter_offload_enable(dhd_pub_t * dhd, char *arg, int enable, int master_mode);
#endif

static int dhd_dongle_up = FALSE;
#define PKT_FILTER_BUF_SIZE 64

#if defined(BCMDONGLEHOST)
#include <dngl_stats.h>
#include <dhd.h>
#include <dhdioctl.h>
#include <wlioctl.h>
#include <brcm_nl80211.h>
#include <dhd_cfg80211.h>
#endif /* defined(BCMDONGLEHOST) */

static s32 wl_dongle_up(struct net_device *ndev);
static s32 wl_dongle_down(struct net_device *ndev);
#ifndef OEM_ANDROID
#ifndef CUSTOMER_HW6
static s32 wl_dongle_power(struct net_device *ndev, u32 power_mode);
#ifdef BCMSDIO /* glomming is a sdio specific feature */
static s32 wl_dongle_glom(struct net_device *ndev, s32 glom, u32 dongle_align);
#endif
static s32 wl_dongle_scantime(struct net_device *ndev, s32 scan_assoc_time, s32 scan_unassoc_time);
static s32 wl_dongle_offload(struct net_device *ndev, s32 arpoe, s32 arp_ol);
static s32 wl_pattern_atoh(s8 *src, s8 *dst);
static s32 wl_dongle_filter(struct net_device *ndev, u32 filter_mode);
#endif /* !CUSTOMER_HW6 */
#endif /* !OEM_ANDROID */

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
	struct net_device *ndev;
	s32 err = 0;

	WL_TRACE(("In\n"));
	if (!dhd_dongle_up) {
		WL_INFORM_MEM(("Dongle is already down\n"));
		err = 0;
		goto done;
	}
	ndev = bcmcfg_to_prmry_ndev(cfg);
	wl_dongle_down(ndev);
done:
	return err;
}

s32 dhd_cfg80211_set_p2p_info(struct bcm_cfg80211 *cfg, int val)
{
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
	dhd->op_mode |= val;
	WL_ERR(("Set : op_mode=0x%04x\n", dhd->op_mode));

	return 0;
}

s32 dhd_cfg80211_clean_p2p_info(struct bcm_cfg80211 *cfg)
{
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
	dhd->op_mode &= ~(DHD_FLAG_P2P_GC_MODE | DHD_FLAG_P2P_GO_MODE);
	WL_ERR(("Clean : op_mode=0x%04x\n", dhd->op_mode));

	return 0;
}
#ifdef WL_STATIC_IF
int32
wl_cfg80211_update_iflist_info(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	int ifidx, uint8 *addr, int bssidx, char *name, int if_state)
{
		return dhd_update_iflist_info(cfg->pub, ndev, ifidx, addr, bssidx, name, if_state);
}
#endif /* WL_STATIC_IF */
struct net_device* wl_cfg80211_allocate_if(struct bcm_cfg80211 *cfg, int ifidx, const char *name,
	uint8 *mac, uint8 bssidx, const char *dngl_name)
{
	return dhd_allocate_if(cfg->pub, ifidx, name, mac, bssidx, FALSE, dngl_name);
}

int wl_cfg80211_register_if(struct bcm_cfg80211 *cfg,
	int ifidx, struct net_device* ndev, bool rtnl_lock_reqd)
{
	return dhd_register_if(cfg->pub, ifidx, rtnl_lock_reqd);
}

int wl_cfg80211_remove_if(struct bcm_cfg80211 *cfg,
	int ifidx, struct net_device* ndev, bool rtnl_lock_reqd)
{
#ifdef DHD_PCIE_RUNTIMEPM
	dhdpcie_runtime_bus_wake(cfg->pub, CAN_SLEEP(), __builtin_return_address(0));
#endif /* DHD_PCIE_RUNTIMEPM */
	return dhd_remove_if(cfg->pub, ifidx, rtnl_lock_reqd);
}

void wl_cfg80211_cleanup_if(struct net_device *net)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);
#ifdef DHD_PCIE_RUNTIMEPM
	dhdpcie_runtime_bus_wake(cfg->pub, CAN_SLEEP(), __builtin_return_address(0));
#else
	BCM_REFERENCE(cfg);
#endif /* DHD_PCIE_RUNTIMEPM */
	dhd_cleanup_if(net);
}

struct net_device * dhd_cfg80211_netdev_free(struct net_device *ndev)
{
	struct bcm_cfg80211 *cfg;

	if (ndev) {
		cfg = wl_get_cfg(ndev);
		if (ndev->ieee80211_ptr) {
			MFREE(cfg->osh, ndev->ieee80211_ptr, sizeof(struct wireless_dev));
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

static s32
wl_dongle_up(struct net_device *ndev)
{
	s32 err = 0;
	u32 local_up = 0;
#ifdef WLAN_ACCEL_BOOT
	u32 bus_host_access = 1;
	err = wldev_iovar_setint(ndev, "bus:host_access", bus_host_access);
	if (unlikely(err)) {
		WL_ERR(("bus:host_access(%d) error (%d)\n", bus_host_access, err));
	}
#endif /* WLAN_ACCEL_BOOT */
	err = wldev_ioctl_set(ndev, WLC_UP, &local_up, sizeof(local_up));
	if (unlikely(err)) {
		WL_ERR(("WLC_UP error (%d)\n", err));
	} else {
		WL_INFORM_MEM(("wl up\n"));
		dhd_dongle_up = TRUE;
	}
	return err;
}

static s32
wl_dongle_down(struct net_device *ndev)
{
	s32 err = 0;
	u32 local_down = 0;
#ifdef WLAN_ACCEL_BOOT
	u32 bus_host_access = 0;
#endif /* WLAN_ACCEL_BOOT */

	err = wldev_ioctl_set(ndev, WLC_DOWN, &local_down, sizeof(local_down));
	if (unlikely(err)) {
		WL_ERR(("WLC_DOWN error (%d)\n", err));
	}
#ifdef WLAN_ACCEL_BOOT
	err = wldev_iovar_setint(ndev, "bus:host_access", bus_host_access);
	if (unlikely(err)) {
		WL_ERR(("bus:host_access(%d) error (%d)\n", bus_host_access, err));
	}
#endif /* WLAN_ACCEL_BOOT */
	WL_INFORM_MEM(("wl down\n"));
	dhd_dongle_up = FALSE;

	return err;
}

#ifndef OEM_ANDROID
#ifndef CUSTOMER_HW6
static s32 wl_dongle_power(struct net_device *ndev, u32 power_mode)
{
	s32 err = 0;

	WL_TRACE(("In\n"));
	err = wldev_ioctl_set(ndev, WLC_SET_PM, &power_mode, sizeof(power_mode));
	if (unlikely(err)) {
		WL_ERR(("WLC_SET_PM error (%d)\n", err));
	}
	return err;
}

#ifdef BCMSDIO
static s32
wl_dongle_glom(struct net_device *ndev, s32 glom, u32 dongle_align)
{
	s32 err = 0;

	/* Match Host and Dongle rx alignment */
	err = wldev_iovar_setint(ndev, "bus:txglomalign", dongle_align);
	if (unlikely(err)) {
		WL_ERR(("txglomalign error (%d)\n", err));
		goto dongle_glom_out;
	}
	/* disable glom option per default */
	if (glom != DEFAULT_GLOM_VALUE) {
		err = wldev_iovar_setint(ndev, "bus:txglom", glom);
		if (unlikely(err)) {
			WL_ERR(("txglom error (%d)\n", err));
			goto dongle_glom_out;
		}
	}
dongle_glom_out:
	return err;
}

#endif /* BCMSDIO */
#endif /* !CUSTOMER_HW6 */
#endif /* !OEM_ANDROID */

s32
wl_dongle_roam(struct net_device *ndev, u32 roamvar, u32 bcn_timeout)
{
	s32 err = 0;

	/* Setup timeout if Beacons are lost and roam is off to report link down */
	if (roamvar) {
		err = wldev_iovar_setint(ndev, "bcn_timeout", bcn_timeout);
		if (unlikely(err)) {
			WL_ERR(("bcn_timeout error (%d)\n", err));
			goto dongle_rom_out;
		}
	}
	/* Enable/Disable built-in roaming to allow supplicant to take care of roaming */
	err = wldev_iovar_setint(ndev, "roam_off", roamvar);
	if (unlikely(err)) {
		WL_ERR(("roam_off error (%d)\n", err));
		goto dongle_rom_out;
	}
dongle_rom_out:
	return err;
}

#ifndef OEM_ANDROID
#ifndef CUSTOMER_HW6
static s32
wl_dongle_scantime(struct net_device *ndev, s32 scan_assoc_time,
	s32 scan_unassoc_time)
{
	s32 err = 0;

	err = wldev_ioctl_set(ndev, WLC_SET_SCAN_CHANNEL_TIME, &scan_assoc_time,
		sizeof(scan_assoc_time));
	if (err) {
		if (err == -EOPNOTSUPP) {
			WL_INFORM(("Scan assoc time is not supported\n"));
		} else {
			WL_ERR(("Scan assoc time error (%d)\n", err));
		}
		goto dongle_scantime_out;
	}
	err = wldev_ioctl_set(ndev, WLC_SET_SCAN_UNASSOC_TIME, &scan_unassoc_time,
		sizeof(scan_unassoc_time));
	if (err) {
		if (err == -EOPNOTSUPP) {
			WL_INFORM(("Scan unassoc time is not supported\n"));
		} else {
			WL_ERR(("Scan unassoc time error (%d)\n", err));
		}
		goto dongle_scantime_out;
	}

dongle_scantime_out:
	return err;
}

static s32
wl_dongle_offload(struct net_device *ndev, s32 arpoe, s32 arp_ol)
{
	s8 iovbuf[WLC_IOCTL_SMLEN];
	s32 err = 0;
	s32 len;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

	/* Set ARP offload */
	len = bcm_mkiovar("arpoe", (char *)&arpoe, sizeof(arpoe), iovbuf, sizeof(iovbuf));
	if (!len) {
		WL_ERR(("%s: bcm_mkiovar failed:%d\n", __FUNCTION__, len));
		return BCME_BADARG;
	}
	err = wldev_ioctl_set(ndev, WLC_SET_VAR, iovbuf, len);
	if (err) {
		if (err == -EOPNOTSUPP)
			WL_INFORM(("arpoe is not supported\n"));
		else
			WL_ERR(("arpoe error (%d)\n", err));

		goto dongle_offload_out;
	}
	len = bcm_mkiovar("arp_ol", (char *)&arp_ol, sizeof(arp_ol), iovbuf, sizeof(iovbuf));
	if (!len) {
		WL_ERR(("%s: bcm_mkiovar failed:%d\n", __FUNCTION__, len));
		return BCME_BADARG;
	}
	err = wldev_ioctl_set(ndev, WLC_SET_VAR, iovbuf, len);
	if (err) {
		if (err == -EOPNOTSUPP)
			WL_INFORM(("arp_ol is not supported\n"));
		else
			WL_ERR(("arp_ol error (%d)\n", err));

		goto dongle_offload_out;
	}

	dhd->arpoe_enable = TRUE;
	dhd->arpol_configured = TRUE;
	WL_ERR(("arpoe:%d arpol:%d\n",
		dhd->arpoe_enable, dhd->arpol_configured));

dongle_offload_out:
	return err;
}

static s32 wl_pattern_atoh(s8 *src, s8 *dst)
{
	int i;
	if (strncmp(src, "0x", 2) != 0 && strncmp(src, "0X", 2) != 0) {
		WL_ERR(("Mask invalid format. Needs to start with 0x\n"));
		return -1;
	}
	src = src + 2;		/* Skip past 0x */
	if (strlen(src) % 2 != 0) {
		WL_ERR(("Mask invalid format. Needs to be of even length\n"));
		return -1;
	}

	for (i = 0; *src != '\0'; i++) {
		char num[3];
		if ((num[0] = src[0]) != '\0') {
			num[1] = src[1];
		}
		num[2] = '\0';
		dst[i] = (u8) simple_strtoul(num, NULL, 16);
		src += 2;
	}

	return i;
}

static s32 wl_dongle_filter(struct net_device *ndev, u32 filter_mode)
{
	const s8 *str;
	struct wl_pkt_filter pkt_filter;
	struct wl_pkt_filter *pkt_filterp;
	s32 buf_len;
	s32 str_len;
	u32 mask_size;
	u32 pattern_size;
	s8 buf[PKT_FILTER_BUF_SIZE] = {0};
	s32 err = 0;

	/* add a default packet filter pattern */
	str = "pkt_filter_add";
	str_len = strlen(str);
	strlcpy(buf, str, sizeof(buf));
	buf_len = str_len + 1;

	pkt_filterp = (struct wl_pkt_filter *)(buf + str_len + 1);

	/* Parse packet filter id. */
	pkt_filter.id = htod32(100);

	/* Parse filter polarity. */
	pkt_filter.negate_match = htod32(0);

	/* Parse filter type. */
	pkt_filter.type = htod32(0);

	/* Parse pattern filter offset. */
	pkt_filter.u.pattern.offset = htod32(0);

	/* Parse pattern filter mask. */
	mask_size = htod32(wl_pattern_atoh("0xff",
		(char *)pkt_filterp->u.pattern.
		    mask_and_pattern));

	if (mask_size == (typeof(mask_size))-1 ||
		(mask_size > (PKT_FILTER_BUF_SIZE - (buf_len) +
		WL_PKT_FILTER_FIXED_LEN + WL_PKT_FILTER_PATTERN_FIXED_LEN))) {
		/* mask_size has to be equal to pattern_size */
		err = -EINVAL;
		goto dongle_filter_out;
	}
	/* Parse pattern filter pattern. */
	pattern_size = htod32(wl_pattern_atoh("0x00",
		(char *)&pkt_filterp->u.pattern.mask_and_pattern[mask_size]));

	if (mask_size != pattern_size) {
		WL_ERR(("Mask and pattern not the same size\n"));
		err = -EINVAL;
		goto dongle_filter_out;
	}

	pkt_filter.u.pattern.size_bytes = mask_size;
	buf_len += WL_PKT_FILTER_FIXED_LEN;
	buf_len += (WL_PKT_FILTER_PATTERN_FIXED_LEN + 2 * mask_size);

	/* Keep-alive attributes are set in local
	 * variable (keep_alive_pkt), and
	 * then memcpy'ed into buffer (keep_alive_pktp) since there is no
	 * guarantee that the buffer is properly aligned.
	 */
	memcpy((char *)pkt_filterp, &pkt_filter,
		WL_PKT_FILTER_FIXED_LEN + WL_PKT_FILTER_PATTERN_FIXED_LEN);

	err = wldev_ioctl_set(ndev, WLC_SET_VAR, buf, buf_len);
	if (err) {
		if (err == -EOPNOTSUPP) {
			WL_INFORM(("filter not supported\n"));
		} else {
			WL_ERR(("filter (%d)\n", err));
		}
		goto dongle_filter_out;
	}

	/* set mode to allow pattern */
	err = wldev_iovar_setint(ndev, "pkt_filter_mode", filter_mode);
	if (err) {
		if (err == -EOPNOTSUPP) {
			WL_INFORM(("filter_mode not supported\n"));
		} else {
			WL_ERR(("filter_mode (%d)\n", err));
		}
		goto dongle_filter_out;
	}

dongle_filter_out:
	return err;
}
#endif /* !CUSTOMER_HW6 */
#endif /* !OEM_ANDROID */

s32 dhd_config_dongle(struct bcm_cfg80211 *cfg)
{
#ifndef DHD_SDALIGN
#define DHD_SDALIGN	32
#endif
	struct net_device *ndev;
	s32 err = 0;
	dhd_pub_t *dhd = NULL;
#if !defined(OEM_ANDROID) && defined(BCMSDIO)
	s32 glom = CUSTOM_GLOM_SETTING;
	BCM_REFERENCE(glom);
#endif

	WL_TRACE(("In\n"));

	ndev = bcmcfg_to_prmry_ndev(cfg);
	dhd = (dhd_pub_t *)(cfg->pub);

	err = wl_dongle_up(ndev);
	if (unlikely(err)) {
		WL_ERR(("wl_dongle_up failed\n"));
		goto default_conf_out;
	}

	if (dhd && dhd->fw_preinit) {
		/* Init config will be done by fw preinit context */
		return BCME_OK;
	}

#ifndef OEM_ANDROID
#ifndef CUSTOMER_HW6
	err = wl_dongle_power(ndev, PM_FAST);
	if (unlikely(err)) {
		WL_ERR(("wl_dongle_power failed\n"));
		goto default_conf_out;
	}
#ifdef BCMSDIO
	err = wl_dongle_glom(ndev, glom, DHD_SDALIGN);
	if (unlikely(err)) {
		WL_ERR(("wl_dongle_glom failed\n"));
		goto default_conf_out;
	}
#endif /* BCMSDIO */
	err = wl_dongle_roam(ndev, (cfg->roam_on ? 0 : 1), 3);
	if (unlikely(err)) {
		WL_ERR(("wl_dongle_roam failed\n"));
		goto default_conf_out;
	}
	wl_dongle_scantime(ndev, 40, 80);
	wl_dongle_offload(ndev, 1, 0xf);
	wl_dongle_filter(ndev, 1);
#endif /* !CUSTOMER_HW6 */
#endif /* OEM_ANDROID */

default_conf_out:

	return err;

}

int dhd_cfgvendor_priv_string_handler(struct bcm_cfg80211 *cfg, struct wireless_dev *wdev,
	const struct bcm_nlmsg_hdr *nlioc, void *buf)
{
	struct net_device *ndev = NULL;
	dhd_pub_t *dhd;
	dhd_ioctl_t ioc = { 0, NULL, 0, 0, 0, 0, 0};
	int ret = 0;
	int8 index;

	WL_TRACE(("entry: cmd = %d\n", nlioc->cmd));

	dhd = cfg->pub;
	DHD_OS_WAKE_LOCK(dhd);

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
	ioc.buf = buf;
	ret = dhd_ioctl_process(dhd, index, &ioc, buf);
	if (ret) {
		WL_TRACE(("dhd_ioctl_process return err %d\n", ret));
		ret = OSL_ERROR(ret);
		goto done;
	}

done:
	DHD_OS_WAKE_UNLOCK(dhd);
	return ret;
}
