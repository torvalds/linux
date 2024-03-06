/* SPDX-License-Identifier: (GPL-2.0 OR MPL-1.1) */
/*
 *
 * Declares constants and types for the p80211 ioctls
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
 *
 *  While this file is called 'ioctl' is purpose goes a little beyond
 *  that.  This file defines the types and contants used to implement
 *  the p80211 request/confirm/indicate interfaces on Linux.  The
 *  request/confirm interface is, in fact, normally implemented as an
 *  ioctl.  The indicate interface on the other hand, is implemented
 *  using the Linux 'netlink' interface.
 *
 *  The reason I say that request/confirm is 'normally' implemented
 *  via ioctl is that we're reserving the right to be able to send
 *  request commands via the netlink interface.  This will be necessary
 *  if we ever need to send request messages when there aren't any
 *  wlan network devices present (i.e. sending a message that only p80211
 *  cares about.
 * --------------------------------------------------------------------
 */

#ifndef _P80211IOCTL_H
#define _P80211IOCTL_H

/* p80211 ioctl "request" codes.  See argument 2 of ioctl(2). */

#define P80211_IFTEST		(SIOCDEVPRIVATE + 0)
#define P80211_IFREQ		(SIOCDEVPRIVATE + 1)

/*----------------------------------------------------------------*/
/* Magic number, a quick test to see we're getting the desired struct */

#define P80211_IOCTL_MAGIC	(0x4a2d464dUL)

/*----------------------------------------------------------------*/
/* A ptr to the following structure type is passed as the third */
/*  argument to the ioctl system call when issuing a request to */
/*  the p80211 module. */

struct p80211ioctl_req {
	char name[WLAN_DEVNAMELEN_MAX];
	char __user *data;
	u32 magic;
	u16 len;
	u32 result;
} __packed;

#endif /* _P80211IOCTL_H */
