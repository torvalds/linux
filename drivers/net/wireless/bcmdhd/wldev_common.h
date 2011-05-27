/*
 * Common function shared by Linux WEXT, cfg80211 and p2p drivers
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
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
 * $Id: wldev_common.h,v 1.1.4.1.2.14 2011-02-09 01:40:07 Exp $
 */
#ifndef __WLDEV_COMMON_H__
#define __WLDEV_COMMON_H__

/** wl_dev_ioctl - get/set IOCTLs, will call net_device's do_ioctl (or 
 *  netdev_ops->ndo_do_ioctl in new kernels)
 *  @dev: the net_device handle
 */

s32 wldev_ioctl(
	struct net_device *dev, u32 cmd, void *arg, u32 len, u32 set);

/** Retrieve named IOVARs, this function calls wl_dev_ioctl with 
    WLC_GET_VAR IOCTL code
 */
s32 wldev_iovar_getbuf(
	struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen);

/** Set named IOVARs, this function calls wl_dev_ioctl with
    WLC_SET_VAR IOCTL code
 */
s32 wldev_iovar_setbuf(
	struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen);

s32 wldev_iovar_setint(
	struct net_device *dev, s8 *iovar, s32 val);

s32 wldev_iovar_getint(
	struct net_device *dev, s8 *iovar, s32 *pval);


s32 wldev_mkiovar(
	s8 *iovar_name, s8 *param, s32 paramlen,
	s8 *iovar_buf, u32 buflen);


/** The following function can be implemented if there is a need for bsscfg
    indexed IOVARs
 */

s32 wldev_mkiovar_bsscfg(
	const s8 *iovar_name, s8 *param, s32 paramlen,
	s8 *iovar_buf, s32 buflen, s32 bssidx);

/** Retrieve named and bsscfg indexed IOVARs, this function calls wl_dev_ioctl with
    WLC_GET_VAR IOCTL code
 */
s32 wldev_iovar_getbuf_bsscfg(
	struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, s32 bsscfg_idx);

/** Set named and bsscfg indexed IOVARs, this function calls wl_dev_ioctl with
    WLC_SET_VAR IOCTL code
 */
s32 wldev_iovar_setbuf_bsscfg(
	struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, s32 bsscfg_idx);

s32 wldev_iovar_getint_bsscfg(
	struct net_device *dev, s8 *iovar, s32 *pval, s32 bssidx);

s32 wldev_iovar_setint_bsscfg(
	struct net_device *dev, s8 *iovar, s32 val, s32 bssidx);

#endif /* __WLDEV_COMMON_H__ */
