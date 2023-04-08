/*
 * Definitions for nl80211 vendor command/event access to host driver
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 * $Id: brcm_nl80211.h 601873 2015-11-24 11:04:28Z $
 *
 */

#ifndef _brcm_nl80211_h_
#define _brcm_nl80211_h_

#define OUI_BRCM  0x001018
#define OUI_GOOGLE  0x001A11

enum wl_vendor_subcmd {
	BRCM_VENDOR_SCMD_UNSPEC		= 0,
	BRCM_VENDOR_SCMD_PRIV_STR	= 1,
	BRCM_VENDOR_SCMD_BCM_STR	= 2,
	BRCM_VENDOR_SCMD_BCM_PSK	= 3,
	BRCM_VENDOR_SCMD_SET_PMK	= 4,
	BRCM_VENDOR_SCMD_GET_FEATURES	= 5,
	BRCM_VENDOR_SCMD_FRAMEBURST     = 6,
	BRCM_VENDOR_SCMD_MPC            = 7,
	BRCM_VENDOR_SCMD_BAND           = 8,
	BRCM_VENDOR_SCMD_ACS            = 9,
	BRCM_VENDOR_SCMD_MAX
};

struct bcm_nlmsg_hdr {
	uint cmd;	/* common ioctl definition */
	int len;	/* expected return buffer length */
	uint offset;	/* user buffer offset */
	uint set;	/* get or set request optional */
	uint magic;	/* magic number for verification */
};

enum bcmnl_attrs {
	BCM_NLATTR_UNSPEC,

	BCM_NLATTR_LEN,
	BCM_NLATTR_DATA,

	__BCM_NLATTR_AFTER_LAST,
	BCM_NLATTR_MAX = __BCM_NLATTR_AFTER_LAST - 1
};

struct nl_prv_data {
	int err;			/* return result */
	void *data;			/* ioctl return buffer pointer */
	uint len;			/* ioctl return buffer length */
	struct bcm_nlmsg_hdr *nlioc;	/* bcm_nlmsg_hdr header pointer */
};

#endif /* _brcm_nl80211_h_ */
