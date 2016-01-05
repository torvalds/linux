/*
 * Definitions for nl80211 testmode access to host driver
 *
 * Copyright (C) 1999-2015, Broadcom Corporation
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
 * $Id: brcm_nl80211.h 454792 2014-02-11 20:40:19Z $
 *
 */

#ifndef _brcm_nl80211_h_
#define _brcm_nl80211_h_

struct bcm_nlmsg_hdr {
	uint cmd;	/* common ioctl definition */
	uint len;	/* attached buffer length */
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
