/*
 * Common function shared by Linux WEXT, cfg80211 and p2p drivers
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
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#include <osl.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>

#include <wldev_common.h>
#include <bcmutils.h>
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#include <wl_cfgscan.h>
#endif /* WL_CFG80211 */
#include <dhd_config.h>

#if defined(IL_BIGENDIAN)
#include <bcmendian.h>
#define htod32(i) (bcmswap32(i))
#define htod16(i) (bcmswap16(i))
#define dtoh32(i) (bcmswap32(i))
#define dtoh16(i) (bcmswap16(i))
#define htodchanspec(i) htod16(i)
#define dtohchanspec(i) dtoh16(i)
#else
#define htod32(i) (i)
#define htod16(i) (i)
#define dtoh32(i) (i)
#define dtoh16(i) (i)
#define htodchanspec(i) (i)
#define dtohchanspec(i) (i)
#endif

#if defined(CUSTOMER_DBG_PREFIX_ENABLE)
#define USER_PREFIX_WLDEV		"[wldev][wlan] "
#define WLDEV_ERROR_TEXT		USER_PREFIX_WLDEV
#define WLDEV_INFO_TEXT			USER_PREFIX_WLDEV
#else
#define WLDEV_ERROR_TEXT		DHD_LOG_PREFIXS "WLDEV-ERROR) "
#define WLDEV_INFO_TEXT			DHD_LOG_PREFIXS "WLDEV-INFO) "
#endif /* defined(CUSTOMER_DBG_PREFIX_ENABLE) */

#define	WLDEV_ERROR_MSG(x, args...)						\
	do {										\
		WL_DBG_PRINT_SYSTEM_TIME;		\
		pr_cont(WLDEV_ERROR_TEXT x, ## args);	\
	} while (0)
#define WLDEV_ERROR(x) WLDEV_ERROR_MSG x

#define	WLDEV_INFO_MSG(x, args...)						\
	do {										\
		WL_DBG_PRINT_SYSTEM_TIME;		\
		pr_cont(WLDEV_INFO_TEXT x, ## args);	\
	} while (0)
#define WLDEV_INFO(x) WLDEV_INFO_MSG x

extern int dhd_ioctl_entry_local(struct net_device *net, wl_ioctl_t *ioc, int cmd);

s32 wldev_ioctl(
	struct net_device *dev, u32 cmd, void *arg, u32 len, u32 set)
{
	s32 ret = 0;
	struct wl_ioctl  ioc;

#if defined(BCMDONGLEHOST)

	bzero(&ioc, sizeof(ioc));
	ioc.cmd = cmd;
	ioc.buf = arg;
	ioc.len = len;
	ioc.set = set;
	ret = dhd_ioctl_entry_local(dev, (wl_ioctl_t *)&ioc, cmd);
#else
	struct ifreq ifr;
	mm_segment_t fs;

	bzero(&ioc, sizeof(ioc));
	ioc.cmd = cmd;
	ioc.buf = arg;
	ioc.len = len;
	ioc.set = set;

	strlcpy(ifr.ifr_name, dev->name, sizeof(ifr.ifr_name));
	ifr.ifr_data = (caddr_t)&ioc;

	fs = get_fs();
	set_fs(get_ds());
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
	ret = dev->do_ioctl(dev, &ifr, SIOCDEVPRIVATE);
#else
	ret = dev->netdev_ops->ndo_do_ioctl(dev, &ifr, SIOCDEVPRIVATE);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31) */
	set_fs(fs);

	ret = 0;
#endif /* defined(BCMDONGLEHOST) */

	return ret;
}

/*
SET commands :
cast buffer to non-const  and call the GET function
*/

s32 wldev_ioctl_set(
	struct net_device *dev, u32 cmd, const void *arg, u32 len)
{

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	return wldev_ioctl(dev, cmd, (void *)arg, len, 1);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

}

s32 wldev_ioctl_get(
	struct net_device *dev, u32 cmd, void *arg, u32 len)
{
	return wldev_ioctl(dev, cmd, (void *)arg, len, 0);
}

/* Format a iovar buffer, not bsscfg indexed. The bsscfg index will be
 * taken care of in dhd_ioctl_entry. Internal use only, not exposed to
 * wl_iw, wl_cfg80211 and wl_cfgp2p
 */
static s32 wldev_mkiovar(
	const s8 *iovar_name, const s8 *param, u32 paramlen,
	s8 *iovar_buf, u32 buflen)
{
	s32 iolen = 0;

	iolen = bcm_mkiovar(iovar_name, param, paramlen, iovar_buf, buflen);
	return iolen;
}

s32 wldev_iovar_getbuf(
	struct net_device *dev, s8 *iovar_name,
	const void *param, u32 paramlen, void *buf, u32 buflen, struct mutex* buf_sync)
{
	s32 ret = 0;
	if (buf_sync) {
		mutex_lock(buf_sync);
	}

	if (buf && (buflen > 0)) {
		/* initialize the response buffer */
		bzero(buf, buflen);
	} else {
		ret = BCME_BADARG;
		goto exit;
	}

	ret = wldev_mkiovar(iovar_name, param, paramlen, buf, buflen);

	if (!ret) {
		ret = BCME_BUFTOOSHORT;
		goto exit;
	}
	ret = wldev_ioctl_get(dev, WLC_GET_VAR, buf, buflen);
exit:
	if (buf_sync)
		mutex_unlock(buf_sync);
	return ret;
}

s32 wldev_iovar_setbuf(
	struct net_device *dev, s8 *iovar_name,
	const void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync)
{
	s32 ret = 0;
	s32 iovar_len;
	if (buf_sync) {
		mutex_lock(buf_sync);
	}
	iovar_len = wldev_mkiovar(iovar_name, param, paramlen, buf, buflen);
	if (iovar_len > 0)
		ret = wldev_ioctl_set(dev, WLC_SET_VAR, buf, iovar_len);
	else
		ret = BCME_BUFTOOSHORT;

	if (buf_sync)
		mutex_unlock(buf_sync);
	return ret;
}

s32 wldev_iovar_setint(
	struct net_device *dev, s8 *iovar, s32 val)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];

	val = htod32(val);
	bzero(iovar_buf, sizeof(iovar_buf));
	return wldev_iovar_setbuf(dev, iovar, &val, sizeof(val), iovar_buf,
		sizeof(iovar_buf), NULL);
}

s32 wldev_iovar_getint(
	struct net_device *dev, s8 *iovar, s32 *pval)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	s32 err;

	bzero(iovar_buf, sizeof(iovar_buf));
	err = wldev_iovar_getbuf(dev, iovar, pval, sizeof(*pval), iovar_buf,
		sizeof(iovar_buf), NULL);
	if (err == 0)
	{
		memcpy(pval, iovar_buf, sizeof(*pval));
		*pval = dtoh32(*pval);
	}
	return err;
}

/** Format a bsscfg indexed iovar buffer. The bsscfg index will be
 *  taken care of in dhd_ioctl_entry. Internal use only, not exposed to
 *  wl_iw, wl_cfg80211 and wl_cfgp2p
 */
s32 wldev_mkiovar_bsscfg(
	const s8 *iovar_name, const s8 *param, s32 paramlen,
	s8 *iovar_buf, s32 buflen, s32 bssidx)
{
	const s8 *prefix = "bsscfg:";
	s8 *p;
	u32 prefixlen;
	u32 namelen;
	u32 iolen;

	/* initialize buffer */
	if (!iovar_buf || buflen <= 0)
		return BCME_BADARG;
	bzero(iovar_buf, buflen);

	if (bssidx == 0) {
		return wldev_mkiovar(iovar_name, param, paramlen,
			iovar_buf, buflen);
	}

	prefixlen = (u32) strlen(prefix); /* lengh of bsscfg prefix */
	namelen = (u32) strlen(iovar_name) + 1; /* lengh of iovar  name + null */
	iolen = prefixlen + namelen + sizeof(u32) + paramlen;

	if (iolen > (u32)buflen) {
		WLDEV_ERROR(("wldev_mkiovar_bsscfg: buffer is too short\n"));
		return BCME_BUFTOOSHORT;
	}

	p = (s8 *)iovar_buf;

	/* copy prefix, no null */
	memcpy(p, prefix, prefixlen);
	p += prefixlen;

	/* copy iovar name including null */
	memcpy(p, iovar_name, namelen);
	p += namelen;

	/* bss config index as first param */
	bssidx = htod32(bssidx);
	memcpy(p, &bssidx, sizeof(u32));
	p += sizeof(u32);

	/* parameter buffer follows */
	if (paramlen)
		memcpy(p, param, paramlen);

	return iolen;

}

s32 wldev_iovar_getbuf_bsscfg(
	struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, s32 bsscfg_idx, struct mutex* buf_sync)
{
	s32 ret = 0;
	if (buf_sync) {
		mutex_lock(buf_sync);
	}

	wldev_mkiovar_bsscfg(iovar_name, param, paramlen, buf, buflen, bsscfg_idx);
	ret = wldev_ioctl_get(dev, WLC_GET_VAR, buf, buflen);
	if (buf_sync) {
		mutex_unlock(buf_sync);
	}
	return ret;

}

s32 wldev_iovar_setbuf_bsscfg(
	struct net_device *dev, const s8 *iovar_name,
	const void *param, s32 paramlen,
	void *buf, s32 buflen, s32 bsscfg_idx, struct mutex* buf_sync)
{
	s32 ret = 0;
	s32 iovar_len;
	if (buf_sync) {
		mutex_lock(buf_sync);
	}
	iovar_len = wldev_mkiovar_bsscfg(iovar_name, param, paramlen, buf, buflen, bsscfg_idx);
	if (iovar_len > 0)
		ret = wldev_ioctl_set(dev, WLC_SET_VAR, buf, iovar_len);
	else {
		ret = BCME_BUFTOOSHORT;
	}

	if (buf_sync) {
		mutex_unlock(buf_sync);
	}
	return ret;
}

s32 wldev_iovar_setint_bsscfg(
	struct net_device *dev, s8 *iovar, s32 val, s32 bssidx)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];

	val = htod32(val);
	bzero(iovar_buf, sizeof(iovar_buf));
	return wldev_iovar_setbuf_bsscfg(dev, iovar, &val, sizeof(val), iovar_buf,
		sizeof(iovar_buf), bssidx, NULL);
}

s32 wldev_iovar_getint_bsscfg(
	struct net_device *dev, s8 *iovar, s32 *pval, s32 bssidx)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	s32 err;

	bzero(iovar_buf, sizeof(iovar_buf));
	err = wldev_iovar_getbuf_bsscfg(dev, iovar, pval, sizeof(*pval), iovar_buf,
		sizeof(iovar_buf), bssidx, NULL);
	if (err == 0)
	{
		memcpy(pval, iovar_buf, sizeof(*pval));
		*pval = dtoh32(*pval);
	}
	return err;
}

int wldev_get_link_speed(
	struct net_device *dev, int *plink_speed)
{
	int error;

	if (!plink_speed)
		return -ENOMEM;
	*plink_speed = 0;
	error = wldev_ioctl_get(dev, WLC_GET_RATE, plink_speed, sizeof(int));
	if (unlikely(error))
		return error;

	/* Convert internal 500Kbps to Kbps */
	*plink_speed *= 500;
	return error;
}

int wldev_get_rssi(
	struct net_device *dev, scb_val_t *scb_val)
{
	int error;

	if (!scb_val)
		return -ENOMEM;
	bzero(scb_val, sizeof(scb_val_t));
	error = wldev_ioctl_get(dev, WLC_GET_RSSI, scb_val, sizeof(scb_val_t));
	if (unlikely(error))
		return error;

	return error;
}

int wldev_get_ssid(
	struct net_device *dev, wlc_ssid_t *pssid)
{
	int error;

	if (!pssid)
		return -ENOMEM;
	bzero(pssid, sizeof(wlc_ssid_t));
	error = wldev_ioctl_get(dev, WLC_GET_SSID, pssid, sizeof(wlc_ssid_t));
	if (unlikely(error))
		return error;
	pssid->SSID_len = dtoh32(pssid->SSID_len);
	return error;
}

int wldev_get_band(
	struct net_device *dev, uint *pband)
{
	int error;

	*pband = 0;
	error = wldev_ioctl_get(dev, WLC_GET_BAND, pband, sizeof(uint));
	return error;
}

int wldev_set_band(
	struct net_device *dev, uint band)
{
	int error = -1;

	if ((band == WLC_BAND_AUTO) || (band == WLC_BAND_5G) || (band == WLC_BAND_2G)) {
		error = wldev_ioctl_set(dev, WLC_SET_BAND, &band, sizeof(band));
		if (!error)
			dhd_bus_band_set(dev, band);
	}
	return error;
}
int wldev_get_datarate(struct net_device *dev, int *datarate)
{
	int error = 0;

	error = wldev_ioctl_get(dev, WLC_GET_RATE, datarate, sizeof(int));
	if (error) {
		return -1;
	} else {
		*datarate = dtoh32(*datarate);
	}

	return error;
}

#ifdef WL_CFG80211
extern chanspec_t
wl_chspec_driver_to_host(chanspec_t chanspec);
#define WL_EXTRA_BUF_MAX 2048
int wldev_get_mode(
	struct net_device *dev, uint8 *cap, uint8 caplen)
{
	int error = 0;
	int chanspec = 0;
	uint16 band = 0;
	uint16 bandwidth = 0;
	wl_bss_info_t *bss = NULL;
	char* buf = NULL;

	buf = kzalloc(WL_EXTRA_BUF_MAX, GFP_KERNEL);
	if (!buf) {
		WLDEV_ERROR(("%s:ENOMEM\n", __FUNCTION__));
		return -ENOMEM;
	}

	*(u32*) buf = htod32(WL_EXTRA_BUF_MAX);
	error = wldev_ioctl_get(dev, WLC_GET_BSS_INFO, (void*)buf, WL_EXTRA_BUF_MAX);
	if (error) {
		WLDEV_ERROR(("%s:failed:%d\n", __FUNCTION__, error));
		kfree(buf);
		buf = NULL;
		return error;
	}
	bss = (wl_bss_info_t*)(buf + 4);
	chanspec = wl_chspec_driver_to_host(bss->chanspec);

	band = chanspec & WL_CHANSPEC_BAND_MASK;
	bandwidth = chanspec & WL_CHANSPEC_BW_MASK;

	if (band == WL_CHANSPEC_BAND_2G) {
		if (bss->n_cap)
			strlcpy(cap, "n", caplen);
		else
			strlcpy(cap, "bg", caplen);
	} else if (band == WL_CHANSPEC_BAND_5G) {
		if (bandwidth == WL_CHANSPEC_BW_80)
			strlcpy(cap, "ac", caplen);
		else if ((bandwidth == WL_CHANSPEC_BW_40) || (bandwidth == WL_CHANSPEC_BW_20)) {
			if ((bss->nbss_cap & 0xf00) && (bss->n_cap))
				strlcpy(cap, "n|ac", caplen);
			else if (bss->n_cap)
				strlcpy(cap, "n", caplen);
			else if (bss->vht_cap)
				strlcpy(cap, "ac", caplen);
			else
				strlcpy(cap, "a", caplen);
		} else {
			WLDEV_ERROR(("wldev_get_mode: Mode get failed\n"));
			error = BCME_ERROR;
		}

	}
	kfree(buf);
	buf = NULL;
	return error;
}
#endif

int wldev_set_country(
	struct net_device *dev, char *country_code, bool notify, int revinfo)
{
#if defined(BCMDONGLEHOST)
	int error = -1;
	wl_country_t cspec = {{0}, 0, {0}};

	if (!country_code)
		return error;

	cspec.rev = revinfo;
	strlcpy(cspec.country_abbrev, country_code, WL_CCODE_LEN + 1);
	strlcpy(cspec.ccode, country_code, WL_CCODE_LEN + 1);
	error = dhd_conf_map_country_list(dhd_get_pub(dev), &cspec);
	if (error)
		dhd_get_customized_country_code(dev, (char *)&cspec.country_abbrev, &cspec);
	error = dhd_conf_set_country(dhd_get_pub(dev), &cspec);
	if (error < 0) {
		WLDEV_ERROR(("%s: set country for %s as %s rev %d failed\n",
			__FUNCTION__, country_code, cspec.ccode, cspec.rev));
		return error;
	}
	dhd_conf_fix_country(dhd_get_pub(dev));
	dhd_conf_get_country(dhd_get_pub(dev), &cspec);
	dhd_bus_country_set(dev, &cspec, notify);
	printf("%s: set country for %s as %s rev %d\n",
		__FUNCTION__, country_code, cspec.ccode, cspec.rev);
#endif /* defined(BCMDONGLEHOST) */
	return 0;
}
