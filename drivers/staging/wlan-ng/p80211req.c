// SPDX-License-Identifier: (GPL-2.0 OR MPL-1.1)
/*
 *
 * Request/Indication/MacMgmt interface handling functions
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
 * This file contains the functions, types, and macros to support the
 * MLME request interface that's implemented via the device ioctls.
 *
 * --------------------------------------------------------------------
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/sock.h>
#include <linux/netlink.h>

#include "p80211types.h"
#include "p80211hdr.h"
#include "p80211mgmt.h"
#include "p80211conv.h"
#include "p80211msg.h"
#include "p80211netdev.h"
#include "p80211ioctl.h"
#include "p80211metadef.h"
#include "p80211metastruct.h"
#include "p80211req.h"

static void p80211req_handlemsg(struct wlandevice *wlandev,
				struct p80211msg *msg);
static void p80211req_mibset_mibget(struct wlandevice *wlandev,
				    struct p80211msg_dot11req_mibget *mib_msg,
				    int isget);

static void p80211req_handle_action(struct wlandevice *wlandev, u32 *data,
				    int isget, u32 flag)
{
	if (isget) {
		if (wlandev->hostwep & flag)
			*data = P80211ENUM_truth_true;
		else
			*data = P80211ENUM_truth_false;
	} else {
		wlandev->hostwep &= ~flag;
		if (*data == P80211ENUM_truth_true)
			wlandev->hostwep |= flag;
	}
}

/*----------------------------------------------------------------
 * p80211req_dorequest
 *
 * Handles an MLME request/confirm message.
 *
 * Arguments:
 *	wlandev		WLAN device struct
 *	msgbuf		Buffer containing a request message
 *
 * Returns:
 *	0 on success, an errno otherwise
 *
 * Call context:
 *	Potentially blocks the caller, so it's a good idea to
 *	not call this function from an interrupt context.
 *----------------------------------------------------------------
 */
int p80211req_dorequest(struct wlandevice *wlandev, u8 *msgbuf)
{
	struct p80211msg *msg = (struct p80211msg *)msgbuf;

	/* Check to make sure the MSD is running */
	if (!((wlandev->msdstate == WLAN_MSD_HWPRESENT &&
	       msg->msgcode == DIDMSG_LNXREQ_IFSTATE) ||
	      wlandev->msdstate == WLAN_MSD_RUNNING ||
	      wlandev->msdstate == WLAN_MSD_FWLOAD)) {
		return -ENODEV;
	}

	/* Check Permissions */
	if (!capable(CAP_NET_ADMIN) &&
	    (msg->msgcode != DIDMSG_DOT11REQ_MIBGET)) {
		netdev_err(wlandev->netdev,
			   "%s: only dot11req_mibget allowed for non-root.\n",
			   wlandev->name);
		return -EPERM;
	}

	/* Check for busy status */
	if (test_and_set_bit(1, &wlandev->request_pending))
		return -EBUSY;

	/* Allow p80211 to look at msg and handle if desired. */
	/* So far, all p80211 msgs are immediate, no waitq/timer necessary */
	/* This may change. */
	p80211req_handlemsg(wlandev, msg);

	/* Pass it down to wlandev via wlandev->mlmerequest */
	if (wlandev->mlmerequest)
		wlandev->mlmerequest(wlandev, msg);

	clear_bit(1, &wlandev->request_pending);
	return 0;	/* if result==0, msg->status still may contain an err */
}

/*----------------------------------------------------------------
 * p80211req_handlemsg
 *
 * p80211 message handler.  Primarily looks for messages that
 * belong to p80211 and then dispatches the appropriate response.
 * TODO: we don't do anything yet.  Once the linuxMIB is better
 *	defined we'll need a get/set handler.
 *
 * Arguments:
 *	wlandev		WLAN device struct
 *	msg		message structure
 *
 * Returns:
 *	nothing (any results are set in the status field of the msg)
 *
 * Call context:
 *	Process thread
 *----------------------------------------------------------------
 */
static void p80211req_handlemsg(struct wlandevice *wlandev,
				struct p80211msg *msg)
{
	switch (msg->msgcode) {
	case DIDMSG_LNXREQ_HOSTWEP: {
		struct p80211msg_lnxreq_hostwep *req =
			(struct p80211msg_lnxreq_hostwep *)msg;
		wlandev->hostwep &=
				~(HOSTWEP_DECRYPT | HOSTWEP_ENCRYPT);
		if (req->decrypt.data == P80211ENUM_truth_true)
			wlandev->hostwep |= HOSTWEP_DECRYPT;
		if (req->encrypt.data == P80211ENUM_truth_true)
			wlandev->hostwep |= HOSTWEP_ENCRYPT;

		break;
	}
	case DIDMSG_DOT11REQ_MIBGET:
	case DIDMSG_DOT11REQ_MIBSET: {
		int isget = (msg->msgcode == DIDMSG_DOT11REQ_MIBGET);
		struct p80211msg_dot11req_mibget *mib_msg =
			(struct p80211msg_dot11req_mibget *)msg;
		p80211req_mibset_mibget(wlandev, mib_msg, isget);
		break;
	}
	}			/* switch msg->msgcode */
}

static void p80211req_mibset_mibget(struct wlandevice *wlandev,
				    struct p80211msg_dot11req_mibget *mib_msg,
				    int isget)
{
	struct p80211itemd *mibitem =
		(struct p80211itemd *)mib_msg->mibattribute.data;
	struct p80211pstrd *pstr = (struct p80211pstrd *)mibitem->data;
	u8 *key = mibitem->data + sizeof(struct p80211pstrd);

	switch (mibitem->did) {
	case didmib_dot11smt_wepdefaultkeystable_key(1):
	case didmib_dot11smt_wepdefaultkeystable_key(2):
	case didmib_dot11smt_wepdefaultkeystable_key(3):
	case didmib_dot11smt_wepdefaultkeystable_key(4):
		if (!isget)
			wep_change_key(wlandev,
				       P80211DID_ITEM(mibitem->did) - 1,
				       key, pstr->len);
		break;

	case DIDMIB_DOT11SMT_PRIVACYTABLE_WEPDEFAULTKEYID: {
		u32 *data = (u32 *)mibitem->data;

		if (isget) {
			*data = wlandev->hostwep & HOSTWEP_DEFAULTKEY_MASK;
		} else {
			wlandev->hostwep &= ~(HOSTWEP_DEFAULTKEY_MASK);
			wlandev->hostwep |= (*data & HOSTWEP_DEFAULTKEY_MASK);
		}
		break;
	}
	case DIDMIB_DOT11SMT_PRIVACYTABLE_PRIVACYINVOKED: {
		u32 *data = (u32 *)mibitem->data;

		p80211req_handle_action(wlandev, data, isget,
					HOSTWEP_PRIVACYINVOKED);
		break;
	}
	case DIDMIB_DOT11SMT_PRIVACYTABLE_EXCLUDEUNENCRYPTED: {
		u32 *data = (u32 *)mibitem->data;

		p80211req_handle_action(wlandev, data, isget,
					HOSTWEP_EXCLUDEUNENCRYPTED);
		break;
	}
	}
}
