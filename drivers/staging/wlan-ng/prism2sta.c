/* src/prism2/driver/prism2sta.c
 *
 * Implements the station functionality for prism2
 *
 * Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
 * --------------------------------------------------------------------
 *
 * linux-wlan
 *
 *   The contents of this file are subject to the Mozilla Public
 *   License Version 1.1 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.mozilla.org/MPL/
 *
 *   Software distributed under the License is distributed on an "AS
 *   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 *   implied. See the License for the specific language governing
 *   rights and limitations under the License.
 *
 *   Alternatively, the contents of this file may be used under the
 *   terms of the GNU Public License version 2 (the "GPL"), in which
 *   case the provisions of the GPL are applicable instead of the
 *   above.  If you wish to allow the use of your version of this file
 *   only under the terms of the GPL and not to allow others to use
 *   your version of this file under the MPL, indicate your decision
 *   by deleting the provisions above and replace them with the notice
 *   and other provisions required by the GPL.  If you do not delete
 *   the provisions above, a recipient may use your version of this
 *   file under either the MPL or the GPL.
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
 * This file implements the module and linux pcmcia routines for the
 * prism2 driver.
 *
 * --------------------------------------------------------------------
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/byteorder/generic.h>
#include <linux/etherdevice.h>

#include <linux/io.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/bitops.h>

#include "p80211types.h"
#include "p80211hdr.h"
#include "p80211mgmt.h"
#include "p80211conv.h"
#include "p80211msg.h"
#include "p80211netdev.h"
#include "p80211req.h"
#include "p80211metadef.h"
#include "p80211metastruct.h"
#include "hfa384x.h"
#include "prism2mgmt.h"

static char *dev_info = "prism2_usb";
static struct wlandevice *create_wlan(void);

int prism2_reset_holdtime = 30;	/* Reset hold time in ms */
int prism2_reset_settletime = 100;	/* Reset settle time in ms */

static int prism2_doreset;	/* Do a reset at init? */

module_param(prism2_doreset, int, 0644);
MODULE_PARM_DESC(prism2_doreset, "Issue a reset on initialization");

module_param(prism2_reset_holdtime, int, 0644);
MODULE_PARM_DESC(prism2_reset_holdtime, "reset hold time in ms");
module_param(prism2_reset_settletime, int, 0644);
MODULE_PARM_DESC(prism2_reset_settletime, "reset settle time in ms");

MODULE_LICENSE("Dual MPL/GPL");

static int prism2sta_open(struct wlandevice *wlandev);
static int prism2sta_close(struct wlandevice *wlandev);
static void prism2sta_reset(struct wlandevice *wlandev);
static int prism2sta_txframe(struct wlandevice *wlandev, struct sk_buff *skb,
			     union p80211_hdr *p80211_hdr,
			     struct p80211_metawep *p80211_wep);
static int prism2sta_mlmerequest(struct wlandevice *wlandev, struct p80211msg *msg);
static int prism2sta_getcardinfo(struct wlandevice *wlandev);
static int prism2sta_globalsetup(struct wlandevice *wlandev);
static int prism2sta_setmulticast(struct wlandevice *wlandev,
				  struct net_device *dev);

static void prism2sta_inf_handover(struct wlandevice *wlandev,
				   hfa384x_InfFrame_t *inf);
static void prism2sta_inf_tallies(struct wlandevice *wlandev,
				  hfa384x_InfFrame_t *inf);
static void prism2sta_inf_hostscanresults(struct wlandevice *wlandev,
					  hfa384x_InfFrame_t *inf);
static void prism2sta_inf_scanresults(struct wlandevice *wlandev,
				      hfa384x_InfFrame_t *inf);
static void prism2sta_inf_chinforesults(struct wlandevice *wlandev,
					hfa384x_InfFrame_t *inf);
static void prism2sta_inf_linkstatus(struct wlandevice *wlandev,
				     hfa384x_InfFrame_t *inf);
static void prism2sta_inf_assocstatus(struct wlandevice *wlandev,
				      hfa384x_InfFrame_t *inf);
static void prism2sta_inf_authreq(struct wlandevice *wlandev,
				  hfa384x_InfFrame_t *inf);
static void prism2sta_inf_authreq_defer(struct wlandevice *wlandev,
					hfa384x_InfFrame_t *inf);
static void prism2sta_inf_psusercnt(struct wlandevice *wlandev,
				    hfa384x_InfFrame_t *inf);

/*
 * prism2sta_open
 *
 * WLAN device open method.  Called from p80211netdev when kernel
 * device open (start) method is called in response to the
 * SIOCSIIFFLAGS ioctl changing the flags bit IFF_UP
 * from clear to set.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *
 * Returns:
 *	0	success
 *	>0	f/w reported error
 *	<0	driver reported error
 *
 * Side effects:
 *
 * Call context:
 *	process thread
 */
static int prism2sta_open(struct wlandevice *wlandev)
{
	/* We don't currently have to do anything else.
	 * The setup of the MAC should be subsequently completed via
	 * the mlme commands.
	 * Higher layers know we're ready from dev->start==1 and
	 * dev->tbusy==0.  Our rx path knows to pass up received/
	 * frames because of dev->flags&IFF_UP is true.
	 */

	return 0;
}

/*
 * prism2sta_close
 *
 * WLAN device close method.  Called from p80211netdev when kernel
 * device close method is called in response to the
 * SIOCSIIFFLAGS ioctl changing the flags bit IFF_UP
 * from set to clear.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *
 * Returns:
 *	0	success
 *	>0	f/w reported error
 *	<0	driver reported error
 *
 * Side effects:
 *
 * Call context:
 *	process thread
 */
static int prism2sta_close(struct wlandevice *wlandev)
{
	/* We don't currently have to do anything else.
	 * Higher layers know we're not ready from dev->start==0 and
	 * dev->tbusy==1.  Our rx path knows to not pass up received
	 * frames because of dev->flags&IFF_UP is false.
	 */

	return 0;
}

/*
 * prism2sta_reset
 *
 * Currently not implemented.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	none
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	process thread
 */
static void prism2sta_reset(struct wlandevice *wlandev)
{
}

/*
 * prism2sta_txframe
 *
 * Takes a frame from p80211 and queues it for transmission.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	pb		packet buffer struct.  Contains an 802.11
 *			data frame.
 *       p80211_hdr      points to the 802.11 header for the packet.
 * Returns:
 *	0		Success and more buffs available
 *	1		Success but no more buffs
 *	2		Allocation failure
 *	4		Buffer full or queue busy
 *
 * Side effects:
 *
 * Call context:
 *	process thread
 */
static int prism2sta_txframe(struct wlandevice *wlandev, struct sk_buff *skb,
			     union p80211_hdr *p80211_hdr,
			     struct p80211_metawep *p80211_wep)
{
	hfa384x_t *hw = wlandev->priv;

	/* If necessary, set the 802.11 WEP bit */
	if ((wlandev->hostwep & (HOSTWEP_PRIVACYINVOKED | HOSTWEP_ENCRYPT)) ==
	    HOSTWEP_PRIVACYINVOKED) {
		p80211_hdr->a3.fc |= cpu_to_le16(WLAN_SET_FC_ISWEP(1));
	}

	return hfa384x_drvr_txframe(hw, skb, p80211_hdr, p80211_wep);
}

/*
 * prism2sta_mlmerequest
 *
 * wlan command message handler.  All we do here is pass the message
 * over to the prism2sta_mgmt_handler.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	msg		wlan command message
 * Returns:
 *	0		success
 *	<0		successful acceptance of message, but we're
 *			waiting for an async process to finish before
 *			we're done with the msg.  When the asynch
 *			process is done, we'll call the p80211
 *			function p80211req_confirm() .
 *	>0		An error occurred while we were handling
 *			the message.
 *
 * Side effects:
 *
 * Call context:
 *	process thread
 */
static int prism2sta_mlmerequest(struct wlandevice *wlandev, struct p80211msg *msg)
{
	hfa384x_t *hw = wlandev->priv;

	int result = 0;

	switch (msg->msgcode) {
	case DIDmsg_dot11req_mibget:
		pr_debug("Received mibget request\n");
		result = prism2mgmt_mibset_mibget(wlandev, msg);
		break;
	case DIDmsg_dot11req_mibset:
		pr_debug("Received mibset request\n");
		result = prism2mgmt_mibset_mibget(wlandev, msg);
		break;
	case DIDmsg_dot11req_scan:
		pr_debug("Received scan request\n");
		result = prism2mgmt_scan(wlandev, msg);
		break;
	case DIDmsg_dot11req_scan_results:
		pr_debug("Received scan_results request\n");
		result = prism2mgmt_scan_results(wlandev, msg);
		break;
	case DIDmsg_dot11req_start:
		pr_debug("Received mlme start request\n");
		result = prism2mgmt_start(wlandev, msg);
		break;
		/*
		 * Prism2 specific messages
		 */
	case DIDmsg_p2req_readpda:
		pr_debug("Received mlme readpda request\n");
		result = prism2mgmt_readpda(wlandev, msg);
		break;
	case DIDmsg_p2req_ramdl_state:
		pr_debug("Received mlme ramdl_state request\n");
		result = prism2mgmt_ramdl_state(wlandev, msg);
		break;
	case DIDmsg_p2req_ramdl_write:
		pr_debug("Received mlme ramdl_write request\n");
		result = prism2mgmt_ramdl_write(wlandev, msg);
		break;
	case DIDmsg_p2req_flashdl_state:
		pr_debug("Received mlme flashdl_state request\n");
		result = prism2mgmt_flashdl_state(wlandev, msg);
		break;
	case DIDmsg_p2req_flashdl_write:
		pr_debug("Received mlme flashdl_write request\n");
		result = prism2mgmt_flashdl_write(wlandev, msg);
		break;
		/*
		 * Linux specific messages
		 */
	case DIDmsg_lnxreq_hostwep:
		break;		/* ignore me. */
	case DIDmsg_lnxreq_ifstate:
		{
			struct p80211msg_lnxreq_ifstate *ifstatemsg;

			pr_debug("Received mlme ifstate request\n");
			ifstatemsg = (struct p80211msg_lnxreq_ifstate *)msg;
			result =
			    prism2sta_ifstate(wlandev,
					      ifstatemsg->ifstate.data);
			ifstatemsg->resultcode.status =
			    P80211ENUM_msgitem_status_data_ok;
			ifstatemsg->resultcode.data = result;
			result = 0;
		}
		break;
	case DIDmsg_lnxreq_wlansniff:
		pr_debug("Received mlme wlansniff request\n");
		result = prism2mgmt_wlansniff(wlandev, msg);
		break;
	case DIDmsg_lnxreq_autojoin:
		pr_debug("Received mlme autojoin request\n");
		result = prism2mgmt_autojoin(wlandev, msg);
		break;
	case DIDmsg_lnxreq_commsquality:{
			struct p80211msg_lnxreq_commsquality *qualmsg;

			pr_debug("Received commsquality request\n");

			qualmsg = (struct p80211msg_lnxreq_commsquality *)msg;

			qualmsg->link.status =
			    P80211ENUM_msgitem_status_data_ok;
			qualmsg->level.status =
			    P80211ENUM_msgitem_status_data_ok;
			qualmsg->noise.status =
			    P80211ENUM_msgitem_status_data_ok;

			qualmsg->link.data = le16_to_cpu(hw->qual.CQ_currBSS);
			qualmsg->level.data = le16_to_cpu(hw->qual.ASL_currBSS);
			qualmsg->noise.data = le16_to_cpu(hw->qual.ANL_currFC);
			qualmsg->txrate.data = hw->txrate;

			break;
		}
	default:
		netdev_warn(wlandev->netdev,
			    "Unknown mgmt request message 0x%08x",
			    msg->msgcode);
		break;
	}

	return result;
}

/*
 * prism2sta_ifstate
 *
 * Interface state.  This is the primary WLAN interface enable/disable
 * handler.  Following the driver/load/deviceprobe sequence, this
 * function must be called with a state of "enable" before any other
 * commands will be accepted.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	msgp		ptr to msg buffer
 *
 * Returns:
 *	A p80211 message resultcode value.
 *
 * Side effects:
 *
 * Call context:
 *	process thread  (usually)
 *	interrupt
 */
u32 prism2sta_ifstate(struct wlandevice *wlandev, u32 ifstate)
{
	hfa384x_t *hw = wlandev->priv;
	u32 result;

	result = P80211ENUM_resultcode_implementation_failure;

	pr_debug("Current MSD state(%d), requesting(%d)\n",
		 wlandev->msdstate, ifstate);
	switch (ifstate) {
	case P80211ENUM_ifstate_fwload:
		switch (wlandev->msdstate) {
		case WLAN_MSD_HWPRESENT:
			wlandev->msdstate = WLAN_MSD_FWLOAD_PENDING;
			/*
			 * Initialize the device+driver sufficiently
			 * for firmware loading.
			 */
			result = hfa384x_drvr_start(hw);
			if (result) {
				netdev_err(wlandev->netdev,
					   "hfa384x_drvr_start() failed,result=%d\n",
					   (int)result);
				result =
				 P80211ENUM_resultcode_implementation_failure;
				wlandev->msdstate = WLAN_MSD_HWPRESENT;
				break;
			}
			wlandev->msdstate = WLAN_MSD_FWLOAD;
			result = P80211ENUM_resultcode_success;
			break;
		case WLAN_MSD_FWLOAD:
			hfa384x_cmd_initialize(hw);
			result = P80211ENUM_resultcode_success;
			break;
		case WLAN_MSD_RUNNING:
			netdev_warn(wlandev->netdev,
				    "Cannot enter fwload state from enable state, you must disable first.\n");
			result = P80211ENUM_resultcode_invalid_parameters;
			break;
		case WLAN_MSD_HWFAIL:
		default:
			/* probe() had a problem or the msdstate contains
			 * an unrecognized value, there's nothing we can do.
			 */
			result = P80211ENUM_resultcode_implementation_failure;
			break;
		}
		break;
	case P80211ENUM_ifstate_enable:
		switch (wlandev->msdstate) {
		case WLAN_MSD_HWPRESENT:
		case WLAN_MSD_FWLOAD:
			wlandev->msdstate = WLAN_MSD_RUNNING_PENDING;
			/* Initialize the device+driver for full
			 * operation. Note that this might me an FWLOAD to
			 * to RUNNING transition so we must not do a chip
			 * or board level reset.  Note that on failure,
			 * the MSD state is set to HWPRESENT because we
			 * can't make any assumptions about the state
			 * of the hardware or a previous firmware load.
			 */
			result = hfa384x_drvr_start(hw);
			if (result) {
				netdev_err(wlandev->netdev,
					   "hfa384x_drvr_start() failed,result=%d\n",
					   (int)result);
				result =
				  P80211ENUM_resultcode_implementation_failure;
				wlandev->msdstate = WLAN_MSD_HWPRESENT;
				break;
			}

			result = prism2sta_getcardinfo(wlandev);
			if (result) {
				netdev_err(wlandev->netdev,
					   "prism2sta_getcardinfo() failed,result=%d\n",
					   (int)result);
				result =
				  P80211ENUM_resultcode_implementation_failure;
				hfa384x_drvr_stop(hw);
				wlandev->msdstate = WLAN_MSD_HWPRESENT;
				break;
			}
			result = prism2sta_globalsetup(wlandev);
			if (result) {
				netdev_err(wlandev->netdev,
					   "prism2sta_globalsetup() failed,result=%d\n",
					   (int)result);
				result =
				  P80211ENUM_resultcode_implementation_failure;
				hfa384x_drvr_stop(hw);
				wlandev->msdstate = WLAN_MSD_HWPRESENT;
				break;
			}
			wlandev->msdstate = WLAN_MSD_RUNNING;
			hw->join_ap = 0;
			hw->join_retries = 60;
			result = P80211ENUM_resultcode_success;
			break;
		case WLAN_MSD_RUNNING:
			/* Do nothing, we're already in this state. */
			result = P80211ENUM_resultcode_success;
			break;
		case WLAN_MSD_HWFAIL:
		default:
			/* probe() had a problem or the msdstate contains
			 * an unrecognized value, there's nothing we can do.
			 */
			result = P80211ENUM_resultcode_implementation_failure;
			break;
		}
		break;
	case P80211ENUM_ifstate_disable:
		switch (wlandev->msdstate) {
		case WLAN_MSD_HWPRESENT:
			/* Do nothing, we're already in this state. */
			result = P80211ENUM_resultcode_success;
			break;
		case WLAN_MSD_FWLOAD:
		case WLAN_MSD_RUNNING:
			wlandev->msdstate = WLAN_MSD_HWPRESENT_PENDING;
			/*
			 * TODO: Shut down the MAC completely. Here a chip
			 * or board level reset is probably called for.
			 * After a "disable" _all_ results are lost, even
			 * those from a fwload.
			 */
			if (!wlandev->hwremoved)
				netif_carrier_off(wlandev->netdev);

			hfa384x_drvr_stop(hw);

			wlandev->macmode = WLAN_MACMODE_NONE;
			wlandev->msdstate = WLAN_MSD_HWPRESENT;
			result = P80211ENUM_resultcode_success;
			break;
		case WLAN_MSD_HWFAIL:
		default:
			/* probe() had a problem or the msdstate contains
			 * an unrecognized value, there's nothing we can do.
			 */
			result = P80211ENUM_resultcode_implementation_failure;
			break;
		}
		break;
	default:
		result = P80211ENUM_resultcode_invalid_parameters;
		break;
	}

	return result;
}

/*
 * prism2sta_getcardinfo
 *
 * Collect the NICID, firmware version and any other identifiers
 * we'd like to have in host-side data structures.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *
 * Returns:
 *	0	success
 *	>0	f/w reported error
 *	<0	driver reported error
 *
 * Side effects:
 *
 * Call context:
 *	Either.
 */
static int prism2sta_getcardinfo(struct wlandevice *wlandev)
{
	int result = 0;
	hfa384x_t *hw = wlandev->priv;
	u16 temp;
	u8 snum[HFA384x_RID_NICSERIALNUMBER_LEN];

	/* Collect version and compatibility info */
	/*  Some are critical, some are not */
	/* NIC identity */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_NICIDENTITY,
					&hw->ident_nic,
					sizeof(struct hfa384x_compident));
	if (result) {
		netdev_err(wlandev->netdev, "Failed to retrieve NICIDENTITY\n");
		goto failed;
	}

	/* get all the nic id fields in host byte order */
	hw->ident_nic.id = le16_to_cpu(hw->ident_nic.id);
	hw->ident_nic.variant = le16_to_cpu(hw->ident_nic.variant);
	hw->ident_nic.major = le16_to_cpu(hw->ident_nic.major);
	hw->ident_nic.minor = le16_to_cpu(hw->ident_nic.minor);

	netdev_info(wlandev->netdev, "ident: nic h/w: id=0x%02x %d.%d.%d\n",
	       hw->ident_nic.id, hw->ident_nic.major,
	       hw->ident_nic.minor, hw->ident_nic.variant);

	/* Primary f/w identity */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_PRIIDENTITY,
					&hw->ident_pri_fw,
					sizeof(struct hfa384x_compident));
	if (result) {
		netdev_err(wlandev->netdev, "Failed to retrieve PRIIDENTITY\n");
		goto failed;
	}

	/* get all the private fw id fields in host byte order */
	hw->ident_pri_fw.id = le16_to_cpu(hw->ident_pri_fw.id);
	hw->ident_pri_fw.variant = le16_to_cpu(hw->ident_pri_fw.variant);
	hw->ident_pri_fw.major = le16_to_cpu(hw->ident_pri_fw.major);
	hw->ident_pri_fw.minor = le16_to_cpu(hw->ident_pri_fw.minor);

	netdev_info(wlandev->netdev, "ident: pri f/w: id=0x%02x %d.%d.%d\n",
	       hw->ident_pri_fw.id, hw->ident_pri_fw.major,
	       hw->ident_pri_fw.minor, hw->ident_pri_fw.variant);

	/* Station (Secondary?) f/w identity */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_STAIDENTITY,
					&hw->ident_sta_fw,
					sizeof(struct hfa384x_compident));
	if (result) {
		netdev_err(wlandev->netdev, "Failed to retrieve STAIDENTITY\n");
		goto failed;
	}

	if (hw->ident_nic.id < 0x8000) {
		netdev_err(wlandev->netdev,
		       "FATAL: Card is not an Intersil Prism2/2.5/3\n");
		result = -1;
		goto failed;
	}

	/* get all the station fw id fields in host byte order */
	hw->ident_sta_fw.id = le16_to_cpu(hw->ident_sta_fw.id);
	hw->ident_sta_fw.variant = le16_to_cpu(hw->ident_sta_fw.variant);
	hw->ident_sta_fw.major = le16_to_cpu(hw->ident_sta_fw.major);
	hw->ident_sta_fw.minor = le16_to_cpu(hw->ident_sta_fw.minor);

	/* strip out the 'special' variant bits */
	hw->mm_mods = hw->ident_sta_fw.variant & (BIT(14) | BIT(15));
	hw->ident_sta_fw.variant &= ~((u16)(BIT(14) | BIT(15)));

	if (hw->ident_sta_fw.id == 0x1f) {
		netdev_info(wlandev->netdev,
		       "ident: sta f/w: id=0x%02x %d.%d.%d\n",
		       hw->ident_sta_fw.id, hw->ident_sta_fw.major,
		       hw->ident_sta_fw.minor, hw->ident_sta_fw.variant);
	} else {
		netdev_info(wlandev->netdev,
		       "ident:  ap f/w: id=0x%02x %d.%d.%d\n",
		       hw->ident_sta_fw.id, hw->ident_sta_fw.major,
		       hw->ident_sta_fw.minor, hw->ident_sta_fw.variant);
		netdev_err(wlandev->netdev, "Unsupported Tertiary AP firmware loaded!\n");
		goto failed;
	}

	/* Compatibility range, Modem supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_MFISUPRANGE,
					&hw->cap_sup_mfi,
					sizeof(struct hfa384x_caplevel));
	if (result) {
		netdev_err(wlandev->netdev, "Failed to retrieve MFISUPRANGE\n");
		goto failed;
	}

	/* get all the Compatibility range, modem interface supplier
	 * fields in byte order
	 */
	hw->cap_sup_mfi.role = le16_to_cpu(hw->cap_sup_mfi.role);
	hw->cap_sup_mfi.id = le16_to_cpu(hw->cap_sup_mfi.id);
	hw->cap_sup_mfi.variant = le16_to_cpu(hw->cap_sup_mfi.variant);
	hw->cap_sup_mfi.bottom = le16_to_cpu(hw->cap_sup_mfi.bottom);
	hw->cap_sup_mfi.top = le16_to_cpu(hw->cap_sup_mfi.top);

	netdev_info(wlandev->netdev,
	       "MFI:SUP:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
	       hw->cap_sup_mfi.role, hw->cap_sup_mfi.id,
	       hw->cap_sup_mfi.variant, hw->cap_sup_mfi.bottom,
	       hw->cap_sup_mfi.top);

	/* Compatibility range, Controller supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_CFISUPRANGE,
					&hw->cap_sup_cfi,
					sizeof(struct hfa384x_caplevel));
	if (result) {
		netdev_err(wlandev->netdev, "Failed to retrieve CFISUPRANGE\n");
		goto failed;
	}

	/* get all the Compatibility range, controller interface supplier
	 * fields in byte order
	 */
	hw->cap_sup_cfi.role = le16_to_cpu(hw->cap_sup_cfi.role);
	hw->cap_sup_cfi.id = le16_to_cpu(hw->cap_sup_cfi.id);
	hw->cap_sup_cfi.variant = le16_to_cpu(hw->cap_sup_cfi.variant);
	hw->cap_sup_cfi.bottom = le16_to_cpu(hw->cap_sup_cfi.bottom);
	hw->cap_sup_cfi.top = le16_to_cpu(hw->cap_sup_cfi.top);

	netdev_info(wlandev->netdev,
	       "CFI:SUP:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
	       hw->cap_sup_cfi.role, hw->cap_sup_cfi.id,
	       hw->cap_sup_cfi.variant, hw->cap_sup_cfi.bottom,
	       hw->cap_sup_cfi.top);

	/* Compatibility range, Primary f/w supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_PRISUPRANGE,
					&hw->cap_sup_pri,
					sizeof(struct hfa384x_caplevel));
	if (result) {
		netdev_err(wlandev->netdev, "Failed to retrieve PRISUPRANGE\n");
		goto failed;
	}

	/* get all the Compatibility range, primary firmware supplier
	 * fields in byte order
	 */
	hw->cap_sup_pri.role = le16_to_cpu(hw->cap_sup_pri.role);
	hw->cap_sup_pri.id = le16_to_cpu(hw->cap_sup_pri.id);
	hw->cap_sup_pri.variant = le16_to_cpu(hw->cap_sup_pri.variant);
	hw->cap_sup_pri.bottom = le16_to_cpu(hw->cap_sup_pri.bottom);
	hw->cap_sup_pri.top = le16_to_cpu(hw->cap_sup_pri.top);

	netdev_info(wlandev->netdev,
	       "PRI:SUP:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
	       hw->cap_sup_pri.role, hw->cap_sup_pri.id,
	       hw->cap_sup_pri.variant, hw->cap_sup_pri.bottom,
	       hw->cap_sup_pri.top);

	/* Compatibility range, Station f/w supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_STASUPRANGE,
					&hw->cap_sup_sta,
					sizeof(struct hfa384x_caplevel));
	if (result) {
		netdev_err(wlandev->netdev, "Failed to retrieve STASUPRANGE\n");
		goto failed;
	}

	/* get all the Compatibility range, station firmware supplier
	 * fields in byte order
	 */
	hw->cap_sup_sta.role = le16_to_cpu(hw->cap_sup_sta.role);
	hw->cap_sup_sta.id = le16_to_cpu(hw->cap_sup_sta.id);
	hw->cap_sup_sta.variant = le16_to_cpu(hw->cap_sup_sta.variant);
	hw->cap_sup_sta.bottom = le16_to_cpu(hw->cap_sup_sta.bottom);
	hw->cap_sup_sta.top = le16_to_cpu(hw->cap_sup_sta.top);

	if (hw->cap_sup_sta.id == 0x04) {
		netdev_info(wlandev->netdev,
		       "STA:SUP:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
		       hw->cap_sup_sta.role, hw->cap_sup_sta.id,
		       hw->cap_sup_sta.variant, hw->cap_sup_sta.bottom,
		       hw->cap_sup_sta.top);
	} else {
		netdev_info(wlandev->netdev,
		       "AP:SUP:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
		       hw->cap_sup_sta.role, hw->cap_sup_sta.id,
		       hw->cap_sup_sta.variant, hw->cap_sup_sta.bottom,
		       hw->cap_sup_sta.top);
	}

	/* Compatibility range, primary f/w actor, CFI supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_PRI_CFIACTRANGES,
					&hw->cap_act_pri_cfi,
					sizeof(struct hfa384x_caplevel));
	if (result) {
		netdev_err(wlandev->netdev, "Failed to retrieve PRI_CFIACTRANGES\n");
		goto failed;
	}

	/* get all the Compatibility range, primary f/w actor, CFI supplier
	 * fields in byte order
	 */
	hw->cap_act_pri_cfi.role = le16_to_cpu(hw->cap_act_pri_cfi.role);
	hw->cap_act_pri_cfi.id = le16_to_cpu(hw->cap_act_pri_cfi.id);
	hw->cap_act_pri_cfi.variant = le16_to_cpu(hw->cap_act_pri_cfi.variant);
	hw->cap_act_pri_cfi.bottom = le16_to_cpu(hw->cap_act_pri_cfi.bottom);
	hw->cap_act_pri_cfi.top = le16_to_cpu(hw->cap_act_pri_cfi.top);

	netdev_info(wlandev->netdev,
	       "PRI-CFI:ACT:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
	       hw->cap_act_pri_cfi.role, hw->cap_act_pri_cfi.id,
	       hw->cap_act_pri_cfi.variant, hw->cap_act_pri_cfi.bottom,
	       hw->cap_act_pri_cfi.top);

	/* Compatibility range, sta f/w actor, CFI supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_STA_CFIACTRANGES,
					&hw->cap_act_sta_cfi,
					sizeof(struct hfa384x_caplevel));
	if (result) {
		netdev_err(wlandev->netdev, "Failed to retrieve STA_CFIACTRANGES\n");
		goto failed;
	}

	/* get all the Compatibility range, station f/w actor, CFI supplier
	 * fields in byte order
	 */
	hw->cap_act_sta_cfi.role = le16_to_cpu(hw->cap_act_sta_cfi.role);
	hw->cap_act_sta_cfi.id = le16_to_cpu(hw->cap_act_sta_cfi.id);
	hw->cap_act_sta_cfi.variant = le16_to_cpu(hw->cap_act_sta_cfi.variant);
	hw->cap_act_sta_cfi.bottom = le16_to_cpu(hw->cap_act_sta_cfi.bottom);
	hw->cap_act_sta_cfi.top = le16_to_cpu(hw->cap_act_sta_cfi.top);

	netdev_info(wlandev->netdev,
	       "STA-CFI:ACT:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
	       hw->cap_act_sta_cfi.role, hw->cap_act_sta_cfi.id,
	       hw->cap_act_sta_cfi.variant, hw->cap_act_sta_cfi.bottom,
	       hw->cap_act_sta_cfi.top);

	/* Compatibility range, sta f/w actor, MFI supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_STA_MFIACTRANGES,
					&hw->cap_act_sta_mfi,
					sizeof(struct hfa384x_caplevel));
	if (result) {
		netdev_err(wlandev->netdev, "Failed to retrieve STA_MFIACTRANGES\n");
		goto failed;
	}

	/* get all the Compatibility range, station f/w actor, MFI supplier
	 * fields in byte order
	 */
	hw->cap_act_sta_mfi.role = le16_to_cpu(hw->cap_act_sta_mfi.role);
	hw->cap_act_sta_mfi.id = le16_to_cpu(hw->cap_act_sta_mfi.id);
	hw->cap_act_sta_mfi.variant = le16_to_cpu(hw->cap_act_sta_mfi.variant);
	hw->cap_act_sta_mfi.bottom = le16_to_cpu(hw->cap_act_sta_mfi.bottom);
	hw->cap_act_sta_mfi.top = le16_to_cpu(hw->cap_act_sta_mfi.top);

	netdev_info(wlandev->netdev,
	       "STA-MFI:ACT:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
	       hw->cap_act_sta_mfi.role, hw->cap_act_sta_mfi.id,
	       hw->cap_act_sta_mfi.variant, hw->cap_act_sta_mfi.bottom,
	       hw->cap_act_sta_mfi.top);

	/* Serial Number */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_NICSERIALNUMBER,
					snum, HFA384x_RID_NICSERIALNUMBER_LEN);
	if (!result) {
		netdev_info(wlandev->netdev, "Prism2 card SN: %*pEhp\n",
			    HFA384x_RID_NICSERIALNUMBER_LEN, snum);
	} else {
		netdev_err(wlandev->netdev, "Failed to retrieve Prism2 Card SN\n");
		goto failed;
	}

	/* Collect the MAC address */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_CNFOWNMACADDR,
					wlandev->netdev->dev_addr, ETH_ALEN);
	if (result != 0) {
		netdev_err(wlandev->netdev, "Failed to retrieve mac address\n");
		goto failed;
	}

	/* short preamble is always implemented */
	wlandev->nsdcaps |= P80211_NSDCAP_SHORT_PREAMBLE;

	/* find out if hardware wep is implemented */
	hfa384x_drvr_getconfig16(hw, HFA384x_RID_PRIVACYOPTIMP, &temp);
	if (temp)
		wlandev->nsdcaps |= P80211_NSDCAP_HARDWAREWEP;

	/* get the dBm Scaling constant */
	hfa384x_drvr_getconfig16(hw, HFA384x_RID_CNFDBMADJUST, &temp);
	hw->dbmadjust = temp;

	/* Only enable scan by default on newer firmware */
	if (HFA384x_FIRMWARE_VERSION(hw->ident_sta_fw.major,
				     hw->ident_sta_fw.minor,
				     hw->ident_sta_fw.variant) <
	    HFA384x_FIRMWARE_VERSION(1, 5, 5)) {
		wlandev->nsdcaps |= P80211_NSDCAP_NOSCAN;
	}

	/* TODO: Set any internally managed config items */

	goto done;
failed:
	netdev_err(wlandev->netdev, "Failed, result=%d\n", result);
done:
	return result;
}

/*
 * prism2sta_globalsetup
 *
 * Set any global RIDs that we want to set at device activation.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *
 * Returns:
 *	0	success
 *	>0	f/w reported error
 *	<0	driver reported error
 *
 * Side effects:
 *
 * Call context:
 *	process thread
 */
static int prism2sta_globalsetup(struct wlandevice *wlandev)
{
	hfa384x_t *hw = wlandev->priv;

	/* Set the maximum frame size */
	return hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFMAXDATALEN,
					WLAN_DATA_MAXLEN);
}

static int prism2sta_setmulticast(struct wlandevice *wlandev,
					struct net_device *dev)
{
	int result = 0;
	hfa384x_t *hw = wlandev->priv;

	u16 promisc;

	/* If we're not ready, what's the point? */
	if (hw->state != HFA384x_STATE_RUNNING)
		goto exit;

	if ((dev->flags & (IFF_PROMISC | IFF_ALLMULTI)) != 0)
		promisc = P80211ENUM_truth_true;
	else
		promisc = P80211ENUM_truth_false;

	result =
	    hfa384x_drvr_setconfig16_async(hw, HFA384x_RID_PROMISCMODE,
					   promisc);
exit:
	return result;
}

/*
 * prism2sta_inf_handover
 *
 * Handles the receipt of a Handover info frame. Should only be present
 * in APs only.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	inf		ptr to info frame (contents in hfa384x order)
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 */
static void prism2sta_inf_handover(struct wlandevice *wlandev,
				   hfa384x_InfFrame_t *inf)
{
	pr_debug("received infoframe:HANDOVER (unhandled)\n");
}

/*
 * prism2sta_inf_tallies
 *
 * Handles the receipt of a CommTallies info frame.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	inf		ptr to info frame (contents in hfa384x order)
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 */
static void prism2sta_inf_tallies(struct wlandevice *wlandev,
				  hfa384x_InfFrame_t *inf)
{
	hfa384x_t *hw = wlandev->priv;
	u16 *src16;
	u32 *dst;
	u32 *src32;
	int i;
	int cnt;

	/*
	 * Determine if these are 16-bit or 32-bit tallies, based on the
	 * record length of the info record.
	 */

	cnt = sizeof(hfa384x_CommTallies32_t) / sizeof(u32);
	if (inf->framelen > 22) {
		dst = (u32 *)&hw->tallies;
		src32 = (u32 *)&inf->info.commtallies32;
		for (i = 0; i < cnt; i++, dst++, src32++)
			*dst += le32_to_cpu(*src32);
	} else {
		dst = (u32 *)&hw->tallies;
		src16 = (u16 *)&inf->info.commtallies16;
		for (i = 0; i < cnt; i++, dst++, src16++)
			*dst += le16_to_cpu(*src16);
	}
}

/*
 * prism2sta_inf_scanresults
 *
 * Handles the receipt of a Scan Results info frame.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	inf		ptr to info frame (contents in hfa384x order)
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 */
static void prism2sta_inf_scanresults(struct wlandevice *wlandev,
				      hfa384x_InfFrame_t *inf)
{
	hfa384x_t *hw = wlandev->priv;
	int nbss;
	hfa384x_ScanResult_t *sr = &(inf->info.scanresult);
	int i;
	hfa384x_JoinRequest_data_t joinreq;
	int result;

	/* Get the number of results, first in bytes, then in results */
	nbss = (inf->framelen * sizeof(u16)) -
	    sizeof(inf->infotype) - sizeof(inf->info.scanresult.scanreason);
	nbss /= sizeof(hfa384x_ScanResultSub_t);

	/* Print em */
	pr_debug("rx scanresults, reason=%d, nbss=%d:\n",
		 inf->info.scanresult.scanreason, nbss);
	for (i = 0; i < nbss; i++) {
		pr_debug("chid=%d anl=%d sl=%d bcnint=%d\n",
			 sr->result[i].chid,
			 sr->result[i].anl,
			 sr->result[i].sl, sr->result[i].bcnint);
		pr_debug("  capinfo=0x%04x proberesp_rate=%d\n",
			 sr->result[i].capinfo, sr->result[i].proberesp_rate);
	}
	/* issue a join request */
	joinreq.channel = sr->result[0].chid;
	memcpy(joinreq.bssid, sr->result[0].bssid, WLAN_BSSID_LEN);
	result = hfa384x_drvr_setconfig(hw,
					HFA384x_RID_JOINREQUEST,
					&joinreq, HFA384x_RID_JOINREQUEST_LEN);
	if (result) {
		netdev_err(wlandev->netdev, "setconfig(joinreq) failed, result=%d\n",
		       result);
	}
}

/*
 * prism2sta_inf_hostscanresults
 *
 * Handles the receipt of a Scan Results info frame.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	inf		ptr to info frame (contents in hfa384x order)
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 */
static void prism2sta_inf_hostscanresults(struct wlandevice *wlandev,
					  hfa384x_InfFrame_t *inf)
{
	hfa384x_t *hw = wlandev->priv;
	int nbss;

	nbss = (inf->framelen - 3) / 32;
	pr_debug("Received %d hostscan results\n", nbss);

	if (nbss > 32)
		nbss = 32;

	kfree(hw->scanresults);

	hw->scanresults = kmemdup(inf, sizeof(hfa384x_InfFrame_t), GFP_ATOMIC);

	if (nbss == 0)
		nbss = -1;

	/* Notify/wake the sleeping caller. */
	hw->scanflag = nbss;
	wake_up_interruptible(&hw->cmdq);
};

/*
 * prism2sta_inf_chinforesults
 *
 * Handles the receipt of a Channel Info Results info frame.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	inf		ptr to info frame (contents in hfa384x order)
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 */
static void prism2sta_inf_chinforesults(struct wlandevice *wlandev,
					hfa384x_InfFrame_t *inf)
{
	hfa384x_t *hw = wlandev->priv;
	unsigned int i, n;

	hw->channel_info.results.scanchannels =
	    le16_to_cpu(inf->info.chinforesult.scanchannels);

	for (i = 0, n = 0; i < HFA384x_CHINFORESULT_MAX; i++) {
		hfa384x_ChInfoResultSub_t *result;
		hfa384x_ChInfoResultSub_t *chinforesult;
		int chan;

		if (!(hw->channel_info.results.scanchannels & (1 << i)))
			continue;

		result = &inf->info.chinforesult.result[n];
		chan = le16_to_cpu(result->chid) - 1;

		if (chan < 0 || chan >= HFA384x_CHINFORESULT_MAX)
			continue;

		chinforesult = &hw->channel_info.results.result[chan];
		chinforesult->chid = chan;
		chinforesult->anl = le16_to_cpu(result->anl);
		chinforesult->pnl = le16_to_cpu(result->pnl);
		chinforesult->active = le16_to_cpu(result->active);

		pr_debug("chinfo: channel %d, %s level (avg/peak)=%d/%d dB, pcf %d\n",
			 chan + 1,
			 (chinforesult->active & HFA384x_CHINFORESULT_BSSACTIVE)
				? "signal" : "noise",
			 chinforesult->anl, chinforesult->pnl,
			 (chinforesult->active & HFA384x_CHINFORESULT_PCFACTIVE)
				? 1 : 0);
		n++;
	}
	atomic_set(&hw->channel_info.done, 2);

	hw->channel_info.count = n;
}

void prism2sta_processing_defer(struct work_struct *data)
{
	hfa384x_t *hw = container_of(data, struct hfa384x, link_bh);
	struct wlandevice *wlandev = hw->wlandev;
	struct hfa384x_bytestr32 ssid;
	int result;

	/* First let's process the auth frames */
	{
		struct sk_buff *skb;
		hfa384x_InfFrame_t *inf;

		while ((skb = skb_dequeue(&hw->authq))) {
			inf = (hfa384x_InfFrame_t *)skb->data;
			prism2sta_inf_authreq_defer(wlandev, inf);
		}

	}

	/* Now let's handle the linkstatus stuff */
	if (hw->link_status == hw->link_status_new)
		return;

	hw->link_status = hw->link_status_new;

	switch (hw->link_status) {
	case HFA384x_LINK_NOTCONNECTED:
		/* I'm currently assuming that this is the initial link
		 * state.  It should only be possible immediately
		 * following an Enable command.
		 * Response:
		 * Block Transmits, Ignore receives of data frames
		 */
		netif_carrier_off(wlandev->netdev);

		netdev_info(wlandev->netdev, "linkstatus=NOTCONNECTED (unhandled)\n");
		break;

	case HFA384x_LINK_CONNECTED:
		/* This one indicates a successful scan/join/auth/assoc.
		 * When we have the full MLME complement, this event will
		 * signify successful completion of both mlme_authenticate
		 * and mlme_associate.  State management will get a little
		 * ugly here.
		 * Response:
		 * Indicate authentication and/or association
		 * Enable Transmits, Receives and pass up data frames
		 */

		netif_carrier_on(wlandev->netdev);

		/* If we are joining a specific AP, set our
		 * state and reset retries
		 */
		if (hw->join_ap == 1)
			hw->join_ap = 2;
		hw->join_retries = 60;

		/* Don't call this in monitor mode */
		if (wlandev->netdev->type == ARPHRD_ETHER) {
			u16 portstatus;

			netdev_info(wlandev->netdev, "linkstatus=CONNECTED\n");

			/* For non-usb devices, we can use the sync versions */
			/* Collect the BSSID, and set state to allow tx */

			result = hfa384x_drvr_getconfig(hw,
						HFA384x_RID_CURRENTBSSID,
						wlandev->bssid,
						WLAN_BSSID_LEN);
			if (result) {
				pr_debug
				    ("getconfig(0x%02x) failed, result = %d\n",
				     HFA384x_RID_CURRENTBSSID, result);
				return;
			}

			result = hfa384x_drvr_getconfig(hw,
							HFA384x_RID_CURRENTSSID,
							&ssid, sizeof(ssid));
			if (result) {
				pr_debug
				    ("getconfig(0x%02x) failed, result = %d\n",
				     HFA384x_RID_CURRENTSSID, result);
				return;
			}
			prism2mgmt_bytestr2pstr(
					(struct hfa384x_bytestr *)&ssid,
					(struct p80211pstrd *)&wlandev->ssid);

			/* Collect the port status */
			result = hfa384x_drvr_getconfig16(hw,
							HFA384x_RID_PORTSTATUS,
							&portstatus);
			if (result) {
				pr_debug
				    ("getconfig(0x%02x) failed, result = %d\n",
				     HFA384x_RID_PORTSTATUS, result);
				return;
			}
			wlandev->macmode =
			    (portstatus == HFA384x_PSTATUS_CONN_IBSS) ?
			    WLAN_MACMODE_IBSS_STA : WLAN_MACMODE_ESS_STA;

			/* signal back up to cfg80211 layer */
			prism2_connect_result(wlandev, P80211ENUM_truth_false);

			/* Get the ball rolling on the comms quality stuff */
			prism2sta_commsqual_defer(&hw->commsqual_bh);
		}
		break;

	case HFA384x_LINK_DISCONNECTED:
		/* This one indicates that our association is gone.  We've
		 * lost connection with the AP and/or been disassociated.
		 * This indicates that the MAC has completely cleared it's
		 * associated state.  We * should send a deauth indication
		 * (implying disassoc) up * to the MLME.
		 * Response:
		 * Indicate Deauthentication
		 * Block Transmits, Ignore receives of data frames
		 */
		if (wlandev->netdev->type == ARPHRD_ETHER)
			netdev_info(wlandev->netdev,
			       "linkstatus=DISCONNECTED (unhandled)\n");
		wlandev->macmode = WLAN_MACMODE_NONE;

		netif_carrier_off(wlandev->netdev);

		/* signal back up to cfg80211 layer */
		prism2_disconnected(wlandev);

		break;

	case HFA384x_LINK_AP_CHANGE:
		/* This one indicates that the MAC has decided to and
		 * successfully completed a change to another AP.  We
		 * should probably implement a reassociation indication
		 * in response to this one.  I'm thinking that the the
		 * p80211 layer needs to be notified in case of
		 * buffering/queueing issues.  User mode also needs to be
		 * notified so that any BSS dependent elements can be
		 * updated.
		 * associated state.  We * should send a deauth indication
		 * (implying disassoc) up * to the MLME.
		 * Response:
		 * Indicate Reassociation
		 * Enable Transmits, Receives and pass up data frames
		 */
		netdev_info(wlandev->netdev, "linkstatus=AP_CHANGE\n");

		result = hfa384x_drvr_getconfig(hw,
						HFA384x_RID_CURRENTBSSID,
						wlandev->bssid, WLAN_BSSID_LEN);
		if (result) {
			pr_debug("getconfig(0x%02x) failed, result = %d\n",
				 HFA384x_RID_CURRENTBSSID, result);
			return;
		}

		result = hfa384x_drvr_getconfig(hw,
						HFA384x_RID_CURRENTSSID,
						&ssid, sizeof(ssid));
		if (result) {
			pr_debug("getconfig(0x%02x) failed, result = %d\n",
				 HFA384x_RID_CURRENTSSID, result);
			return;
		}
		prism2mgmt_bytestr2pstr((struct hfa384x_bytestr *)&ssid,
					(struct p80211pstrd *)&wlandev->ssid);

		hw->link_status = HFA384x_LINK_CONNECTED;
		netif_carrier_on(wlandev->netdev);

		/* signal back up to cfg80211 layer */
		prism2_roamed(wlandev);

		break;

	case HFA384x_LINK_AP_OUTOFRANGE:
		/* This one indicates that the MAC has decided that the
		 * AP is out of range, but hasn't found a better candidate
		 * so the MAC maintains its "associated" state in case
		 * we get back in range.  We should block transmits and
		 * receives in this state.  Do we need an indication here?
		 * Probably not since a polling user-mode element would
		 * get this status from from p2PortStatus(FD40). What about
		 * p80211?
		 * Response:
		 * Block Transmits, Ignore receives of data frames
		 */
		netdev_info(wlandev->netdev, "linkstatus=AP_OUTOFRANGE (unhandled)\n");

		netif_carrier_off(wlandev->netdev);

		break;

	case HFA384x_LINK_AP_INRANGE:
		/* This one indicates that the MAC has decided that the
		 * AP is back in range.  We continue working with our
		 * existing association.
		 * Response:
		 * Enable Transmits, Receives and pass up data frames
		 */
		netdev_info(wlandev->netdev, "linkstatus=AP_INRANGE\n");

		hw->link_status = HFA384x_LINK_CONNECTED;
		netif_carrier_on(wlandev->netdev);

		break;

	case HFA384x_LINK_ASSOCFAIL:
		/* This one is actually a peer to CONNECTED.  We've
		 * requested a join for a given SSID and optionally BSSID.
		 * We can use this one to indicate authentication and
		 * association failures.  The trick is going to be
		 * 1) identifying the failure, and 2) state management.
		 * Response:
		 * Disable Transmits, Ignore receives of data frames
		 */
		if (hw->join_ap && --hw->join_retries > 0) {
			hfa384x_JoinRequest_data_t joinreq;

			joinreq = hw->joinreq;
			/* Send the join request */
			hfa384x_drvr_setconfig(hw,
					       HFA384x_RID_JOINREQUEST,
					       &joinreq,
					       HFA384x_RID_JOINREQUEST_LEN);
			netdev_info(wlandev->netdev,
			       "linkstatus=ASSOCFAIL (re-submitting join)\n");
		} else {
			netdev_info(wlandev->netdev, "linkstatus=ASSOCFAIL (unhandled)\n");
		}

		netif_carrier_off(wlandev->netdev);

		/* signal back up to cfg80211 layer */
		prism2_connect_result(wlandev, P80211ENUM_truth_true);

		break;

	default:
		/* This is bad, IO port problems? */
		netdev_warn(wlandev->netdev,
		       "unknown linkstatus=0x%02x\n", hw->link_status);
		return;
	}

	wlandev->linkstatus = (hw->link_status == HFA384x_LINK_CONNECTED);
}

/*
 * prism2sta_inf_linkstatus
 *
 * Handles the receipt of a Link Status info frame.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	inf		ptr to info frame (contents in hfa384x order)
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 */
static void prism2sta_inf_linkstatus(struct wlandevice *wlandev,
				     hfa384x_InfFrame_t *inf)
{
	hfa384x_t *hw = wlandev->priv;

	hw->link_status_new = le16_to_cpu(inf->info.linkstatus.linkstatus);

	schedule_work(&hw->link_bh);
}

/*
 * prism2sta_inf_assocstatus
 *
 * Handles the receipt of an Association Status info frame. Should
 * be present in APs only.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	inf		ptr to info frame (contents in hfa384x order)
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 */
static void prism2sta_inf_assocstatus(struct wlandevice *wlandev,
				      hfa384x_InfFrame_t *inf)
{
	hfa384x_t *hw = wlandev->priv;
	hfa384x_AssocStatus_t rec;
	int i;

	memcpy(&rec, &inf->info.assocstatus, sizeof(rec));
	rec.assocstatus = le16_to_cpu(rec.assocstatus);
	rec.reason = le16_to_cpu(rec.reason);

	/*
	 * Find the address in the list of authenticated stations.
	 * If it wasn't found, then this address has not been previously
	 * authenticated and something weird has happened if this is
	 * anything other than an "authentication failed" message.
	 * If the address was found, then set the "associated" flag for
	 * that station, based on whether the station is associating or
	 * losing its association.  Something weird has also happened
	 * if we find the address in the list of authenticated stations
	 * but we are getting an "authentication failed" message.
	 */

	for (i = 0; i < hw->authlist.cnt; i++)
		if (ether_addr_equal(rec.sta_addr, hw->authlist.addr[i]))
			break;

	if (i >= hw->authlist.cnt) {
		if (rec.assocstatus != HFA384x_ASSOCSTATUS_AUTHFAIL)
			netdev_warn(wlandev->netdev,
	"assocstatus info frame received for non-authenticated station.\n");
	} else {
		hw->authlist.assoc[i] =
		    (rec.assocstatus == HFA384x_ASSOCSTATUS_STAASSOC ||
		     rec.assocstatus == HFA384x_ASSOCSTATUS_REASSOC);

		if (rec.assocstatus == HFA384x_ASSOCSTATUS_AUTHFAIL)
			netdev_warn(wlandev->netdev,
"authfail assocstatus info frame received for authenticated station.\n");
	}
}

/*
 * prism2sta_inf_authreq
 *
 * Handles the receipt of an Authentication Request info frame. Should
 * be present in APs only.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	inf		ptr to info frame (contents in hfa384x order)
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 *
 */
static void prism2sta_inf_authreq(struct wlandevice *wlandev,
				  hfa384x_InfFrame_t *inf)
{
	hfa384x_t *hw = wlandev->priv;
	struct sk_buff *skb;

	skb = dev_alloc_skb(sizeof(*inf));
	if (skb) {
		skb_put(skb, sizeof(*inf));
		memcpy(skb->data, inf, sizeof(*inf));
		skb_queue_tail(&hw->authq, skb);
		schedule_work(&hw->link_bh);
	}
}

static void prism2sta_inf_authreq_defer(struct wlandevice *wlandev,
					hfa384x_InfFrame_t *inf)
{
	hfa384x_t *hw = wlandev->priv;
	hfa384x_authenticateStation_data_t rec;

	int i, added, result, cnt;
	u8 *addr;

	/*
	 * Build the AuthenticateStation record.  Initialize it for denying
	 * authentication.
	 */

	ether_addr_copy(rec.address, inf->info.authreq.sta_addr);
	rec.status = P80211ENUM_status_unspec_failure;

	/*
	 * Authenticate based on the access mode.
	 */

	switch (hw->accessmode) {
	case WLAN_ACCESS_NONE:

		/*
		 * Deny all new authentications.  However, if a station
		 * is ALREADY authenticated, then accept it.
		 */

		for (i = 0; i < hw->authlist.cnt; i++)
			if (ether_addr_equal(rec.address,
					     hw->authlist.addr[i])) {
				rec.status = P80211ENUM_status_successful;
				break;
			}

		break;

	case WLAN_ACCESS_ALL:

		/*
		 * Allow all authentications.
		 */

		rec.status = P80211ENUM_status_successful;
		break;

	case WLAN_ACCESS_ALLOW:

		/*
		 * Only allow the authentication if the MAC address
		 * is in the list of allowed addresses.
		 *
		 * Since this is the interrupt handler, we may be here
		 * while the access list is in the middle of being
		 * updated.  Choose the list which is currently okay.
		 * See "prism2mib_priv_accessallow()" for details.
		 */

		if (hw->allow.modify == 0) {
			cnt = hw->allow.cnt;
			addr = hw->allow.addr[0];
		} else {
			cnt = hw->allow.cnt1;
			addr = hw->allow.addr1[0];
		}

		for (i = 0; i < cnt; i++, addr += ETH_ALEN)
			if (ether_addr_equal(rec.address, addr)) {
				rec.status = P80211ENUM_status_successful;
				break;
			}

		break;

	case WLAN_ACCESS_DENY:

		/*
		 * Allow the authentication UNLESS the MAC address is
		 * in the list of denied addresses.
		 *
		 * Since this is the interrupt handler, we may be here
		 * while the access list is in the middle of being
		 * updated.  Choose the list which is currently okay.
		 * See "prism2mib_priv_accessdeny()" for details.
		 */

		if (hw->deny.modify == 0) {
			cnt = hw->deny.cnt;
			addr = hw->deny.addr[0];
		} else {
			cnt = hw->deny.cnt1;
			addr = hw->deny.addr1[0];
		}

		rec.status = P80211ENUM_status_successful;

		for (i = 0; i < cnt; i++, addr += ETH_ALEN)
			if (ether_addr_equal(rec.address, addr)) {
				rec.status = P80211ENUM_status_unspec_failure;
				break;
			}

		break;
	}

	/*
	 * If the authentication is okay, then add the MAC address to the
	 * list of authenticated stations.  Don't add the address if it
	 * is already in the list. (802.11b does not seem to disallow
	 * a station from issuing an authentication request when the
	 * station is already authenticated. Does this sort of thing
	 * ever happen?  We might as well do the check just in case.)
	 */

	added = 0;

	if (rec.status == P80211ENUM_status_successful) {
		for (i = 0; i < hw->authlist.cnt; i++)
			if (ether_addr_equal(rec.address,
					     hw->authlist.addr[i]))
				break;

		if (i >= hw->authlist.cnt) {
			if (hw->authlist.cnt >= WLAN_AUTH_MAX) {
				rec.status = P80211ENUM_status_ap_full;
			} else {
				ether_addr_copy(
					hw->authlist.addr[hw->authlist.cnt],
					rec.address);
				hw->authlist.cnt++;
				added = 1;
			}
		}
	}

	/*
	 * Send back the results of the authentication.  If this doesn't work,
	 * then make sure to remove the address from the authenticated list if
	 * it was added.
	 */

	rec.status = cpu_to_le16(rec.status);
	rec.algorithm = inf->info.authreq.algorithm;

	result = hfa384x_drvr_setconfig(hw, HFA384x_RID_AUTHENTICATESTA,
					&rec, sizeof(rec));
	if (result) {
		if (added)
			hw->authlist.cnt--;
		netdev_err(wlandev->netdev,
		       "setconfig(authenticatestation) failed, result=%d\n",
		       result);
	}
}

/*
 * prism2sta_inf_psusercnt
 *
 * Handles the receipt of a PowerSaveUserCount info frame. Should
 * be present in APs only.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	inf		ptr to info frame (contents in hfa384x order)
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 */
static void prism2sta_inf_psusercnt(struct wlandevice *wlandev,
				    hfa384x_InfFrame_t *inf)
{
	hfa384x_t *hw = wlandev->priv;

	hw->psusercount = le16_to_cpu(inf->info.psusercnt.usercnt);
}

/*
 * prism2sta_ev_info
 *
 * Handles the Info event.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	inf		ptr to a generic info frame
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 */
void prism2sta_ev_info(struct wlandevice *wlandev, hfa384x_InfFrame_t *inf)
{
	inf->infotype = le16_to_cpu(inf->infotype);
	/* Dispatch */
	switch (inf->infotype) {
	case HFA384x_IT_HANDOVERADDR:
		prism2sta_inf_handover(wlandev, inf);
		break;
	case HFA384x_IT_COMMTALLIES:
		prism2sta_inf_tallies(wlandev, inf);
		break;
	case HFA384x_IT_HOSTSCANRESULTS:
		prism2sta_inf_hostscanresults(wlandev, inf);
		break;
	case HFA384x_IT_SCANRESULTS:
		prism2sta_inf_scanresults(wlandev, inf);
		break;
	case HFA384x_IT_CHINFORESULTS:
		prism2sta_inf_chinforesults(wlandev, inf);
		break;
	case HFA384x_IT_LINKSTATUS:
		prism2sta_inf_linkstatus(wlandev, inf);
		break;
	case HFA384x_IT_ASSOCSTATUS:
		prism2sta_inf_assocstatus(wlandev, inf);
		break;
	case HFA384x_IT_AUTHREQ:
		prism2sta_inf_authreq(wlandev, inf);
		break;
	case HFA384x_IT_PSUSERCNT:
		prism2sta_inf_psusercnt(wlandev, inf);
		break;
	case HFA384x_IT_KEYIDCHANGED:
		netdev_warn(wlandev->netdev, "Unhandled IT_KEYIDCHANGED\n");
		break;
	case HFA384x_IT_ASSOCREQ:
		netdev_warn(wlandev->netdev, "Unhandled IT_ASSOCREQ\n");
		break;
	case HFA384x_IT_MICFAILURE:
		netdev_warn(wlandev->netdev, "Unhandled IT_MICFAILURE\n");
		break;
	default:
		netdev_warn(wlandev->netdev,
		       "Unknown info type=0x%02x\n", inf->infotype);
		break;
	}
}

/*
 * prism2sta_ev_txexc
 *
 * Handles the TxExc event.  A Transmit Exception event indicates
 * that the MAC's TX process was unsuccessful - so the packet did
 * not get transmitted.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	status		tx frame status word
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 */
void prism2sta_ev_txexc(struct wlandevice *wlandev, u16 status)
{
	pr_debug("TxExc status=0x%x.\n", status);
}

/*
 * prism2sta_ev_tx
 *
 * Handles the Tx event.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *	status		tx frame status word
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 */
void prism2sta_ev_tx(struct wlandevice *wlandev, u16 status)
{
	pr_debug("Tx Complete, status=0x%04x\n", status);
	/* update linux network stats */
	wlandev->netdev->stats.tx_packets++;
}

/*
 * prism2sta_ev_alloc
 *
 * Handles the Alloc event.
 *
 * Arguments:
 *	wlandev		wlan device structure
 *
 * Returns:
 *	nothing
 *
 * Side effects:
 *
 * Call context:
 *	interrupt
 */
void prism2sta_ev_alloc(struct wlandevice *wlandev)
{
	netif_wake_queue(wlandev->netdev);
}

/*
* create_wlan
*
* Called at module init time.  This creates the struct wlandevice structure
* and initializes it with relevant bits.
*
* Arguments:
*	none
*
* Returns:
*	the created struct wlandevice structure.
*
* Side effects:
*	also allocates the priv/hw structures.
*
* Call context:
*	process thread
*
*/
static struct wlandevice *create_wlan(void)
{
	struct wlandevice *wlandev = NULL;
	hfa384x_t *hw = NULL;

	/* Alloc our structures */
	wlandev = kzalloc(sizeof(struct wlandevice), GFP_KERNEL);
	hw = kzalloc(sizeof(hfa384x_t), GFP_KERNEL);

	if (!wlandev || !hw) {
		kfree(wlandev);
		kfree(hw);
		return NULL;
	}

	/* Initialize the network device object. */
	wlandev->nsdname = dev_info;
	wlandev->msdstate = WLAN_MSD_HWPRESENT_PENDING;
	wlandev->priv = hw;
	wlandev->open = prism2sta_open;
	wlandev->close = prism2sta_close;
	wlandev->reset = prism2sta_reset;
	wlandev->txframe = prism2sta_txframe;
	wlandev->mlmerequest = prism2sta_mlmerequest;
	wlandev->set_multicast_list = prism2sta_setmulticast;
	wlandev->tx_timeout = hfa384x_tx_timeout;

	wlandev->nsdcaps = P80211_NSDCAP_HWFRAGMENT | P80211_NSDCAP_AUTOJOIN;

	/* Initialize the device private data structure. */
	hw->dot11_desired_bss_type = 1;

	return wlandev;
}

void prism2sta_commsqual_defer(struct work_struct *data)
{
	hfa384x_t *hw = container_of(data, struct hfa384x, commsqual_bh);
	struct wlandevice *wlandev = hw->wlandev;
	struct hfa384x_bytestr32 ssid;
	struct p80211msg_dot11req_mibget msg;
	struct p80211item_uint32 *mibitem = (struct p80211item_uint32 *)
						&msg.mibattribute.data;
	int result = 0;

	if (hw->wlandev->hwremoved)
		return;

	/* we don't care if we're in AP mode */
	if ((wlandev->macmode == WLAN_MACMODE_NONE) ||
	    (wlandev->macmode == WLAN_MACMODE_ESS_AP)) {
		return;
	}

	/* It only makes sense to poll these in non-IBSS */
	if (wlandev->macmode != WLAN_MACMODE_IBSS_STA) {
		result = hfa384x_drvr_getconfig(
				hw, HFA384x_RID_DBMCOMMSQUALITY,
				&hw->qual, HFA384x_RID_DBMCOMMSQUALITY_LEN);

		if (result) {
			netdev_err(wlandev->netdev, "error fetching commsqual\n");
			return;
		}

		pr_debug("commsqual %d %d %d\n",
			 le16_to_cpu(hw->qual.CQ_currBSS),
			 le16_to_cpu(hw->qual.ASL_currBSS),
			 le16_to_cpu(hw->qual.ANL_currFC));
	}

	/* Get the signal rate */
	msg.msgcode = DIDmsg_dot11req_mibget;
	mibitem->did = DIDmib_p2_p2MAC_p2CurrentTxRate;
	result = p80211req_dorequest(wlandev, (u8 *)&msg);

	if (result) {
		pr_debug("get signal rate failed, result = %d\n",
			 result);
		return;
	}

	switch (mibitem->data) {
	case HFA384x_RATEBIT_1:
		hw->txrate = 10;
		break;
	case HFA384x_RATEBIT_2:
		hw->txrate = 20;
		break;
	case HFA384x_RATEBIT_5dot5:
		hw->txrate = 55;
		break;
	case HFA384x_RATEBIT_11:
		hw->txrate = 110;
		break;
	default:
		pr_debug("Bad ratebit (%d)\n", mibitem->data);
	}

	/* Lastly, we need to make sure the BSSID didn't change on us */
	result = hfa384x_drvr_getconfig(hw,
					HFA384x_RID_CURRENTBSSID,
					wlandev->bssid, WLAN_BSSID_LEN);
	if (result) {
		pr_debug("getconfig(0x%02x) failed, result = %d\n",
			 HFA384x_RID_CURRENTBSSID, result);
		return;
	}

	result = hfa384x_drvr_getconfig(hw,
					HFA384x_RID_CURRENTSSID,
					&ssid, sizeof(ssid));
	if (result) {
		pr_debug("getconfig(0x%02x) failed, result = %d\n",
			 HFA384x_RID_CURRENTSSID, result);
		return;
	}
	prism2mgmt_bytestr2pstr((struct hfa384x_bytestr *)&ssid,
				(struct p80211pstrd *)&wlandev->ssid);

	/* Reschedule timer */
	mod_timer(&hw->commsqual_timer, jiffies + HZ);
}

void prism2sta_commsqual_timer(unsigned long data)
{
	hfa384x_t *hw = (hfa384x_t *)data;

	schedule_work(&hw->commsqual_bh);
}
