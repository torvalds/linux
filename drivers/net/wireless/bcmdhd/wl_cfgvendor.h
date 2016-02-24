/*
 * Linux cfg80211 Vendor Extension Code
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
 * $Id: wl_cfgvendor.h 455257 2014-02-13 08:10:24Z $
 */

/*
 * New vendor interface additon to nl80211/cfg80211 to allow vendors
 * to implement proprietary features over the cfg80211 stack.
 */

#ifndef _wl_cfgvendor_h_
#define _wl_cfgvendor_h_

#define OUI_BRCM  0x001018
#define BRCM_VENDOR_SUBCMD_PRIV_STR	1

enum wl_vendor_subcmd {
	BRCM_VENDOR_SCMD_UNSPEC,
	BRCM_VENDOR_SCMD_PRIV_STR
};

enum wl_vendor_event {
	BRCM_VENDOR_EVENT_UNSPEC,
	BRCM_VENDOR_EVENT_PRIV_STR
};

/* Capture the BRCM_VENDOR_SUBCMD_PRIV_STRINGS* here */
#define BRCM_VENDOR_SCMD_CAPA	"cap"

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || defined(WL_VENDOR_EXT_SUPPORT)
extern int wl_cfgvendor_attach(struct wiphy *wiphy);
extern int wl_cfgvendor_detach(struct wiphy *wiphy);
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || defined(WL_VENDOR_EXT_SUPPORT) */

#endif /* _wl_cfgvendor_h_ */
