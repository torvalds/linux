/* SPDX-License-Identifier: (GPL-2.0 OR MPL-1.1) */
/*
 *
 * Macros, constants, types, and funcs for req and ind messages
 *
 * Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
 * --------------------------------------------------------------------
 *
 * linux-wlan
 *
 * --------------------------------------------------------------------
 *
 * Inquiries regarding the linux-wlan Open Source project can be
 * made directly to:
 *
 * AbsoluteValue Systems Inc.
 * info@linux-wlan.com
 * http://www.linux-wlan.com
 *
 * --------------------------------------------------------------------
 *
 * Portions of the development of this software were funded by
 * Intersil Corporation as part of PRISM(R) chipset product development.
 *
 * --------------------------------------------------------------------
 */

#ifndef _P80211MSG_H
#define _P80211MSG_H

#define WLAN_DEVNAMELEN_MAX	16

struct p80211msg {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
} __packed;

#endif /* _P80211MSG_H */
