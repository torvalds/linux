/*
 * Common function shared by Linux WEXT, cfg80211 and p2p drivers
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: wldev_common.h,v 1.1.4.1.2.14 2011-02-09 01:40:07 $
 */
#ifndef __WLDEV_COMMON_H__
#define __WLDEV_COMMON_H__

#include <wlioctl.h>

/* wl_dev_ioctl - get/set IOCTLs, will call net_device's do_ioctl (or
 *  netdev_ops->ndo_do_ioctl in new kernels)
 *  @dev: the net_device handle
 */
s32 wldev_ioctl(
	struct net_device *dev, u32 cmd, void *arg, u32 len, u32 set);

/** Retrieve named IOVARs, this function calls wl_dev_ioctl with
 *  WLC_GET_VAR IOCTL code
 */
s32 wldev_iovar_getbuf(
	struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync);

/** Set named IOVARs, this function calls wl_dev_ioctl with
 *  WLC_SET_VAR IOCTL code
 */
s32 wldev_iovar_setbuf(
	struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync);

s32 wldev_iovar_setint(
	struct net_device *dev, s8 *iovar, s32 val);

s32 wldev_iovar_getint(
	struct net_device *dev, s8 *iovar, s32 *pval);

/** The following function can be implemented if there is a need for bsscfg
 *  indexed IOVARs
 */

s32 wldev_mkiovar_bsscfg(
	const s8 *iovar_name, s8 *param, s32 paramlen,
	s8 *iovar_buf, s32 buflen, s32 bssidx);

/** Retrieve named and bsscfg indexed IOVARs, this function calls wl_dev_ioctl with
 *  WLC_GET_VAR IOCTL code
 */
s32 wldev_iovar_getbuf_bsscfg(
	struct net_device *dev, s8 *iovar_name, void *param, s32 paramlen,
	void *buf, s32 buflen, s32 bsscfg_idx, struct mutex* buf_sync);

/** Set named and bsscfg indexed IOVARs, this function calls wl_dev_ioctl with
 *  WLC_SET_VAR IOCTL code
 */
s32 wldev_iovar_setbuf_bsscfg(
	struct net_device *dev, s8 *iovar_name, void *param, s32 paramlen,
	void *buf, s32 buflen, s32 bsscfg_idx, struct mutex* buf_sync);

s32 wldev_iovar_getint_bsscfg(
	struct net_device *dev, s8 *iovar, s32 *pval, s32 bssidx);

s32 wldev_iovar_setint_bsscfg(
	struct net_device *dev, s8 *iovar, s32 val, s32 bssidx);

extern void get_customized_country_code(char *country_iso_code, wl_country_t *cspec);
extern void dhd_bus_country_set(struct net_device *dev, wl_country_t *cspec, bool notify);
extern void dhd_bus_band_set(struct net_device *dev, uint band);
extern int wldev_set_country(struct net_device *dev, char *country_code, bool notify,
	bool user_enforced);
extern int net_os_wake_lock(struct net_device *dev);
extern int net_os_wake_unlock(struct net_device *dev);
extern int net_os_wake_lock_timeout(struct net_device *dev);
extern int net_os_wake_lock_timeout_enable(struct net_device *dev, int val);
extern int net_os_set_dtim_skip(struct net_device *dev, int val);
extern int net_os_set_suspend_disable(struct net_device *dev, int val);
extern int net_os_set_suspend(struct net_device *dev, int val, int force);
extern int wl_iw_parse_ssid_list_tlv(char** list_str, wlc_ssid_t* ssid,
	int max, int *bytes_left);

/* Get the link speed from dongle, speed is in kpbs */
int wldev_get_link_speed(struct net_device *dev, int *plink_speed);

int wldev_get_rssi(struct net_device *dev, int *prssi);

int wldev_get_ssid(struct net_device *dev, wlc_ssid_t *pssid);

int wldev_get_band(struct net_device *dev, uint *pband);

int wldev_set_band(struct net_device *dev, uint band);

#endif /* __WLDEV_COMMON_H__ */
