/*
 * Common function shared by Linux WEXT, cfg80211 and p2p drivers
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
 * $Id: wldev_common.c 540901 2015-03-13 11:50:58Z $
 */

#include <osl.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>

#include <wldev_common.h>
#include <bcmutils.h>

#define htod32(i) (i)
#define htod16(i) (i)
#define dtoh32(i) (i)
#define dtoh16(i) (i)
#define htodchanspec(i) (i)
#define dtohchanspec(i) (i)

#define	WLDEV_ERROR(args)						\
	do {										\
		printk(KERN_ERR "WLDEV-ERROR) %s : ", __func__);	\
		printk args;							\
	} while (0)

extern int dhd_ioctl_entry_local(struct net_device *net, wl_ioctl_t *ioc, int cmd);

#define MAX_NUM_OF_ASSOCLIST 64
s32 wldev_ioctl(
	struct net_device *dev, u32 cmd, void *arg, u32 len, u32 set)
{
	s32 ret = 0;
	struct wl_ioctl ioc;


	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = cmd;
	ioc.buf = arg;
	ioc.len = len;
	ioc.set = set;

	ret = dhd_ioctl_entry_local(dev, &ioc, cmd);

	return ret;
}

/* Format a iovar buffer, not bsscfg indexed. The bsscfg index will be
 * taken care of in dhd_ioctl_entry. Internal use only, not exposed to
 * wl_iw, wl_cfg80211 and wl_cfgp2p
 */
static s32 wldev_mkiovar(
	s8 *iovar_name, s8 *param, s32 paramlen,
	s8 *iovar_buf, u32 buflen)
{
	s32 iolen = 0;

	iolen = bcm_mkiovar(iovar_name, param, paramlen, iovar_buf, buflen);
	return iolen;
}

s32 wldev_iovar_getbuf(
	struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync)
{
	s32 ret = 0;
	if (buf_sync) {
		mutex_lock(buf_sync);
	}
	wldev_mkiovar(iovar_name, param, paramlen, buf, buflen);
	ret = wldev_ioctl(dev, WLC_GET_VAR, buf, buflen, FALSE);
	if (buf_sync)
		mutex_unlock(buf_sync);
	return ret;
}


s32 wldev_iovar_setbuf(
	struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync)
{
	s32 ret = 0;
	s32 iovar_len;
	if (buf_sync) {
		mutex_lock(buf_sync);
	}
	iovar_len = wldev_mkiovar(iovar_name, param, paramlen, buf, buflen);
	if (iovar_len > 0)
		ret = wldev_ioctl(dev, WLC_SET_VAR, buf, iovar_len, TRUE);
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
	memset(iovar_buf, 0, sizeof(iovar_buf));
	return wldev_iovar_setbuf(dev, iovar, &val, sizeof(val), iovar_buf,
		sizeof(iovar_buf), NULL);
}


s32 wldev_iovar_getint(
	struct net_device *dev, s8 *iovar, s32 *pval)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	s32 err;

	memset(iovar_buf, 0, sizeof(iovar_buf));
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
	const s8 *iovar_name, s8 *param, s32 paramlen,
	s8 *iovar_buf, s32 buflen, s32 bssidx)
{
	const s8 *prefix = "bsscfg:";
	s8 *p;
	u32 prefixlen;
	u32 namelen;
	u32 iolen;

	if (bssidx == 0) {
		return wldev_mkiovar((s8*)iovar_name, (s8 *)param, paramlen,
			(s8 *) iovar_buf, buflen);
	}

	prefixlen = (u32) strlen(prefix); /* lengh of bsscfg prefix */
	namelen = (u32) strlen(iovar_name) + 1; /* lengh of iovar  name + null */
	iolen = prefixlen + namelen + sizeof(u32) + paramlen;

	if (buflen < 0 || iolen > (u32)buflen)
	{
		WLDEV_ERROR(("%s: buffer is too short\n", __FUNCTION__));
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
	ret = wldev_ioctl(dev, WLC_GET_VAR, buf, buflen, FALSE);
	if (buf_sync) {
		mutex_unlock(buf_sync);
	}
	return ret;

}

s32 wldev_iovar_setbuf_bsscfg(
	struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, s32 bsscfg_idx, struct mutex* buf_sync)
{
	s32 ret = 0;
	s32 iovar_len;
	if (buf_sync) {
		mutex_lock(buf_sync);
	}
	iovar_len = wldev_mkiovar_bsscfg(iovar_name, param, paramlen, buf, buflen, bsscfg_idx);
	if (iovar_len > 0)
		ret = wldev_ioctl(dev, WLC_SET_VAR, buf, iovar_len, TRUE);
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
	memset(iovar_buf, 0, sizeof(iovar_buf));
	return wldev_iovar_setbuf_bsscfg(dev, iovar, &val, sizeof(val), iovar_buf,
		sizeof(iovar_buf), bssidx, NULL);
}


s32 wldev_iovar_getint_bsscfg(
	struct net_device *dev, s8 *iovar, s32 *pval, s32 bssidx)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	s32 err;

	memset(iovar_buf, 0, sizeof(iovar_buf));
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
	error = wldev_ioctl(dev, WLC_GET_RATE, plink_speed, sizeof(int), 0);
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

	error = wldev_ioctl(dev, WLC_GET_RSSI, scb_val, sizeof(scb_val_t), 0);
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
	error = wldev_ioctl(dev, WLC_GET_SSID, pssid, sizeof(wlc_ssid_t), 0);
	if (unlikely(error))
		return error;
	pssid->SSID_len = dtoh32(pssid->SSID_len);
	return error;
}

int wldev_get_band(
	struct net_device *dev, uint *pband)
{
	int error;

	error = wldev_ioctl(dev, WLC_GET_BAND, pband, sizeof(uint), 0);
	return error;
}

int wldev_set_band(
	struct net_device *dev, uint band)
{
	int error = -1;

	if ((band == WLC_BAND_AUTO) || (band == WLC_BAND_5G) || (band == WLC_BAND_2G)) {
		error = wldev_ioctl(dev, WLC_SET_BAND, &band, sizeof(band), true);
		if (!error)
			dhd_bus_band_set(dev, band);
	}
	return error;
}

int wldev_get_datarate(struct net_device *dev, int *datarate)
{
	int error = 0;

	error = wldev_ioctl(dev, WLC_GET_RATE, datarate, sizeof(int), false);
	if (error) {
		return -1;
	} else {
		*datarate = dtoh32(*datarate);
	}

	return error;
}

extern chanspec_t
wl_chspec_driver_to_host(chanspec_t chanspec);
#define WL_EXTRA_BUF_MAX 2048
int wldev_get_mode(
	struct net_device *dev, uint8 *cap)
{
	int error = 0;
	int chanspec = 0;
	uint16 band = 0;
	uint16 bandwidth = 0;
	wl_bss_info_t *bss = NULL;
	char* buf = NULL;

	buf = kmalloc(WL_EXTRA_BUF_MAX, GFP_KERNEL);

	if (!buf) {
		WLDEV_ERROR(("%s:NOMEM\n", __FUNCTION__));
		return -ENOMEM;
	}

	*(u32*) buf = htod32(WL_EXTRA_BUF_MAX);
	error = wldev_ioctl(dev, WLC_GET_BSS_INFO, (void*)buf, WL_EXTRA_BUF_MAX, false);
	if (error) {
		WLDEV_ERROR(("%s:failed:%d\n", __FUNCTION__, error));
		kfree(buf);
		buf = NULL;
		return error;
	}
	bss = (struct  wl_bss_info *)(buf + 4);
	chanspec = wl_chspec_driver_to_host(bss->chanspec);

	band = chanspec & WL_CHANSPEC_BAND_MASK;
	bandwidth = chanspec & WL_CHANSPEC_BW_MASK;

	if (band == WL_CHANSPEC_BAND_2G) {
		if (bss->n_cap)
			strcpy(cap, "n");
		else
			strcpy(cap, "bg");
	} else if (band == WL_CHANSPEC_BAND_5G) {
		if (bandwidth == WL_CHANSPEC_BW_80)
			strcpy(cap, "ac");
		else if ((bandwidth == WL_CHANSPEC_BW_40) || (bandwidth == WL_CHANSPEC_BW_20)) {
			if ((bss->nbss_cap & 0xf00) && (bss->n_cap))
				strcpy(cap, "n|ac");
			else if (bss->n_cap)
				strcpy(cap, "n");
			else if (bss->vht_cap)
				strcpy(cap, "ac");
			else
				strcpy(cap, "a");
		} else {
			WLDEV_ERROR(("%s:Mode get failed\n", __FUNCTION__));
			error = BCME_ERROR;
		}

	}
	kfree(buf);
	buf = NULL;
	return error;
}
#define MAX_NUM_OF_ASSOCLIST 64
int wldev_get_assoclist(
		struct net_device *dev, struct maclist  *assoc_maclist, uint length)
{
	int ret = 0;

/* get the current list of associated STAs */
	assoc_maclist->count = MAX_NUM_OF_ASSOCLIST;
	if ((ret = wldev_ioctl(dev, WLC_GET_ASSOCLIST, assoc_maclist,
		length, false)) != 0) {
		return ret;
	}
	return 0;
}

int wldev_set_country(
	struct net_device *dev, char *country_code, bool notify, bool user_enforced)
{
	int error = -1;
	wl_country_t cspec = {{0}, 0, {0}};
	scb_val_t scbval;
	char smbuf[WLC_IOCTL_SMLEN];

	if (!country_code)
		return error;

	bzero(&scbval, sizeof(scb_val_t));
	error = wldev_iovar_getbuf(dev, "country", NULL, 0, &cspec, sizeof(cspec), NULL);
	if (error < 0) {
		WLDEV_ERROR(("%s: get country failed = %d\n", __FUNCTION__, error));
		return error;
	}

	if ((error < 0) ||
	    (strncmp(country_code, cspec.country_abbrev, WLC_CNTRY_BUF_SZ) != 0)) {

		if (user_enforced) {
			bzero(&scbval, sizeof(scb_val_t));
			error = wldev_ioctl(dev, WLC_DISASSOC, &scbval, sizeof(scb_val_t), true);
			if (error < 0) {
				WLDEV_ERROR(("%s: set country failed due to Disassoc error %d\n",
					__FUNCTION__, error));
				return error;
			}
		}

		cspec.rev = -1;
		memcpy(cspec.country_abbrev, country_code, WLC_CNTRY_BUF_SZ);
		memcpy(cspec.ccode, country_code, WLC_CNTRY_BUF_SZ);
		dhd_get_customized_country_code(dev, (char *)&cspec.country_abbrev, &cspec);
		error = wldev_iovar_setbuf(dev, "country", &cspec, sizeof(cspec),
			smbuf, sizeof(smbuf), NULL);
		if (error < 0) {
			WLDEV_ERROR(("%s: set country for %s as %s rev %d failed\n",
				__FUNCTION__, country_code, cspec.ccode, cspec.rev));
			return error;
		}
		dhd_bus_country_set(dev, &cspec, notify);
		WLDEV_ERROR(("%s: set country for %s as %s rev %d\n",
			__FUNCTION__, country_code, cspec.ccode, cspec.rev));
	}
	return 0;
}
