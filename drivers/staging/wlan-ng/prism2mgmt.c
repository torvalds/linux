/* src/prism2/driver/prism2mgmt.c
*
* Management request handler functions.
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
* The functions in this file handle management requests sent from
* user mode.
*
* Most of these functions have two separate blocks of code that are
* conditional on whether this is a station or an AP.  This is used
* to separate out the STA and AP responses to these management primitives.
* It's a choice (good, bad, indifferent?) to have the code in the same
* place so it's clear that the same primitive is implemented in both
* cases but has different behavior.
*
* --------------------------------------------------------------------
*/

/*================================================================*/
/* System Includes */
#define WLAN_DBVAR	prism2_debug

#include "version.h"


#include <linux/version.h>

#include <linux/if_arp.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/random.h>

#if (WLAN_HOSTIF == WLAN_USB)
#include <linux/usb.h>
#endif

#if (WLAN_HOSTIF == WLAN_PCMCIA)
#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>
#endif

#include "wlan_compat.h"

/*================================================================*/
/* Project Includes */

#include "p80211types.h"
#include "p80211hdr.h"
#include "p80211mgmt.h"
#include "p80211conv.h"
#include "p80211msg.h"
#include "p80211netdev.h"
#include "p80211metadef.h"
#include "p80211metastruct.h"
#include "hfa384x.h"
#include "prism2mgmt.h"

/*================================================================*/
/* Local Constants */


/*================================================================*/
/* Local Macros */

/* Converts 802.11 format rate specifications to prism2 */
#define p80211rate_to_p2bit(n)	((((n)&~BIT7) == 2) ? BIT0 : \
				 (((n)&~BIT7) == 4) ? BIT1 : \
				 (((n)&~BIT7) == 11) ? BIT2 : \
				 (((n)&~BIT7) == 22) ? BIT3 : 0)

/*================================================================*/
/* Local Types */


/*================================================================*/
/* Local Static Definitions */


/*================================================================*/
/* Local Function Declarations */


/*================================================================*/
/* Function Definitions */


/*----------------------------------------------------------------
* prism2mgmt_powermgmt
*
* Set the power management state of this station's MAC.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_powermgmt(wlandevice_t *wlandev, void *msgp)
{
	int 			result = 0;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_dot11req_powermgmt_t	*msg = msgp;

	DBFENTER;

	if (!hw->ap) {

		/*** STATION ***/

		/*
		 * Set CNFPMENABLED (on or off)
		 * Set CNFMULTICASTRX (if PM on, otherwise clear)
		 * Spout a notice stating that SleepDuration and
		 * HoldoverDuration and PMEPS also have an impact.
		 */
		/* Powermgmt is currently unsupported for STA */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	} else {

		/*** ACCESS POINT ***/

		/* Powermgmt is never supported for AP */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	}

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_scan
*
* Initiate a scan for BSSs.
*
* This function corresponds to MLME-scan.request and part of
* MLME-scan.confirm.  As far as I can tell in the standard, there
* are no restrictions on when a scan.request may be issued.  We have
* to handle in whatever state the driver/MAC happen to be.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_scan(wlandevice_t *wlandev, void *msgp)
{
	int 			result = 0;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_dot11req_scan_t	*msg = msgp;
        UINT16                  roamingmode, word;
	int                     i, timeout;
	int                     istmpenable = 0;

        hfa384x_HostScanRequest_data_t  scanreq;

	DBFENTER;

        if (hw->ap) {
                WLAN_LOG_ERROR("Prism2 in AP mode cannot perform scans.\n");
                result = 1;
                msg->resultcode.data = P80211ENUM_resultcode_not_supported;
                goto exit;
        }

        /* gatekeeper check */
        if (HFA384x_FIRMWARE_VERSION(hw->ident_sta_fw.major,
                                     hw->ident_sta_fw.minor,
                                     hw->ident_sta_fw.variant) <
            HFA384x_FIRMWARE_VERSION(1,3,2)) {
		WLAN_LOG_ERROR("HostScan not supported with current firmware (<1.3.2).\n");
                result = 1;
                msg->resultcode.data = P80211ENUM_resultcode_not_supported;
		goto exit;
	}

        memset(&scanreq, 0, sizeof(scanreq));

        /* save current roaming mode */
        result = hfa384x_drvr_getconfig16(hw,
                        HFA384x_RID_CNFROAMINGMODE, &roamingmode);
        if ( result ) {
                WLAN_LOG_ERROR("getconfig(ROAMMODE) failed. result=%d\n",
                                result);
                msg->resultcode.data =
                        P80211ENUM_resultcode_implementation_failure;
                goto exit;
        }

        /* drop into mode 3 for the scan */
        result = hfa384x_drvr_setconfig16(hw,
                        HFA384x_RID_CNFROAMINGMODE,
			HFA384x_ROAMMODE_HOSTSCAN_HOSTROAM);
        if ( result ) {
                WLAN_LOG_ERROR("setconfig(ROAMINGMODE) failed. result=%d\n",
                                result);
                msg->resultcode.data =
                        P80211ENUM_resultcode_implementation_failure;
                goto exit;
        }

        /* active or passive? */
        if (HFA384x_FIRMWARE_VERSION(hw->ident_sta_fw.major,
                                     hw->ident_sta_fw.minor,
                                     hw->ident_sta_fw.variant) >
            HFA384x_FIRMWARE_VERSION(1,5,0)) {
                if (msg->scantype.data != P80211ENUM_scantype_active) {
                        word = host2hfa384x_16(msg->maxchanneltime.data);
                } else {
                        word = 0;
                }
                result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFPASSIVESCANCTRL, word);
                if ( result ) {
                        WLAN_LOG_WARNING("Passive scan not supported with "
					  "current firmware.  (<1.5.1)\n");
                }
        }

	/* set up the txrate to be 2MBPS. Should be fastest basicrate... */
	word = HFA384x_RATEBIT_2;
	scanreq.txRate = host2hfa384x_16(word);

        /* set up the channel list */
        word = 0;
        for (i = 0; i < msg->channellist.data.len; i++) {
                UINT8 channel = msg->channellist.data.data[i];
                if (channel > 14) continue;
                /* channel 1 is BIT0 ... channel 14 is BIT13 */
                word |= (1 << (channel-1));
        }
        scanreq.channelList = host2hfa384x_16(word);

        /* set up the ssid, if present. */
        scanreq.ssid.len = host2hfa384x_16(msg->ssid.data.len);
        memcpy(scanreq.ssid.data, msg->ssid.data.data, msg->ssid.data.len);

	/* Enable the MAC port if it's not already enabled  */
	result = hfa384x_drvr_getconfig16(hw, HFA384x_RID_PORTSTATUS, &word);
	if ( result ) {
		WLAN_LOG_ERROR("getconfig(PORTSTATUS) failed. "
				"result=%d\n", result);
		msg->resultcode.data =
			P80211ENUM_resultcode_implementation_failure;
		goto exit;
	}
	if (word == HFA384x_PORTSTATUS_DISABLED) {
		UINT16 wordbuf[17];

		result = hfa384x_drvr_setconfig16(hw,
			HFA384x_RID_CNFROAMINGMODE,
			HFA384x_ROAMMODE_HOSTSCAN_HOSTROAM);
		if ( result ) {
			WLAN_LOG_ERROR("setconfig(ROAMINGMODE) failed. result=%d\n", result);
			msg->resultcode.data =
				P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
		/* Construct a bogus SSID and assign it to OwnSSID and
		 * DesiredSSID
		 */
		wordbuf[0] = host2hfa384x_16(WLAN_SSID_MAXLEN);
		get_random_bytes(&wordbuf[1], WLAN_SSID_MAXLEN);
		result = hfa384x_drvr_setconfig( hw, HFA384x_RID_CNFOWNSSID,
				wordbuf, HFA384x_RID_CNFOWNSSID_LEN);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set OwnSSID.\n");
			msg->resultcode.data =
				P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
		result = hfa384x_drvr_setconfig( hw, HFA384x_RID_CNFDESIREDSSID,
				wordbuf, HFA384x_RID_CNFDESIREDSSID_LEN);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set DesiredSSID.\n");
			msg->resultcode.data =
				P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
		/* bsstype */
		result = hfa384x_drvr_setconfig16(hw,
				HFA384x_RID_CNFPORTTYPE,
				HFA384x_PORTTYPE_IBSS);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set CNFPORTTYPE.\n");
			msg->resultcode.data =
				P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
		/* ibss options */
		result = hfa384x_drvr_setconfig16(hw,
				HFA384x_RID_CREATEIBSS,
				HFA384x_CREATEIBSS_JOINCREATEIBSS);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set CREATEIBSS.\n");
			msg->resultcode.data =
				P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
		result = hfa384x_drvr_enable(hw, 0);
		if ( result ) {
			WLAN_LOG_ERROR("drvr_enable(0) failed. "
					"result=%d\n", result);
			msg->resultcode.data =
			P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
		istmpenable = 1;
	}

        /* Figure out our timeout first Kus, then HZ */
        timeout = msg->channellist.data.len * msg->maxchanneltime.data;
	timeout = (timeout * HZ)/1000;

        /* Issue the scan request */
        hw->scanflag = 0;

	WLAN_HEX_DUMP(5,"hscanreq", &scanreq, sizeof(scanreq));

        result = hfa384x_drvr_setconfig( hw,
                        HFA384x_RID_HOSTSCAN, &scanreq,
                        sizeof(hfa384x_HostScanRequest_data_t));
        if ( result ) {
                WLAN_LOG_ERROR("setconfig(SCANREQUEST) failed. result=%d\n",
                                result);
                msg->resultcode.data =
                        P80211ENUM_resultcode_implementation_failure;
                goto exit;
        }

        /* sleep until info frame arrives */
        wait_event_interruptible_timeout(hw->cmdq, hw->scanflag, timeout);

	msg->numbss.status = P80211ENUM_msgitem_status_data_ok;
	if (hw->scanflag == -1)
		hw->scanflag = 0;

	msg->numbss.data = hw->scanflag;

        hw->scanflag = 0;

	/* Disable port if we temporarily enabled it. */
	if (istmpenable) {
		result = hfa384x_drvr_disable(hw, 0);
		if ( result ) {
			WLAN_LOG_ERROR("drvr_disable(0) failed. "
					"result=%d\n", result);
			msg->resultcode.data =
			P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
	}

	/* restore original roaming mode */
	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFROAMINGMODE,
					  roamingmode);
        if ( result ) {
                WLAN_LOG_ERROR("setconfig(ROAMMODE) failed. result=%d\n",
                                result);
                msg->resultcode.data =
                        P80211ENUM_resultcode_implementation_failure;
                goto exit;
        }

        result = 0;
        msg->resultcode.data = P80211ENUM_resultcode_success;

 exit:
	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_scan_results
*
* Retrieve the BSS description for one of the BSSs identified in
* a scan.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_scan_results(wlandevice_t *wlandev, void *msgp)
{
	int 			result = 0;
        p80211msg_dot11req_scan_results_t       *req;
	hfa384x_t		*hw = wlandev->priv;
	hfa384x_HScanResultSub_t *item = NULL;

	int count;

	DBFENTER;

        req = (p80211msg_dot11req_scan_results_t *) msgp;

	req->resultcode.status = P80211ENUM_msgitem_status_data_ok;

	if (hw->ap) {
		result = 1;
		req->resultcode.data = P80211ENUM_resultcode_not_supported;
		goto exit;
	}

	if (! hw->scanresults) {
		WLAN_LOG_ERROR("dot11req_scan_results can only be used after a successful dot11req_scan.\n");
		result = 2;
		req->resultcode.data = P80211ENUM_resultcode_invalid_parameters;
		goto exit;
	}

        count = (hw->scanresults->framelen - 3) / 32;
	if (count > 32)  count = 32;

	if (req->bssindex.data >= count) {
		WLAN_LOG_DEBUG(0, "requested index (%d) out of range (%d)\n",
				req->bssindex.data, count);
		result = 2;
		req->resultcode.data = P80211ENUM_resultcode_invalid_parameters;
		goto exit;
	}

	item = &(hw->scanresults->info.hscanresult.result[req->bssindex.data]);
	/* signal and noise */
	req->signal.status = P80211ENUM_msgitem_status_data_ok;
	req->noise.status = P80211ENUM_msgitem_status_data_ok;
	req->signal.data = hfa384x2host_16(item->sl);
	req->noise.data = hfa384x2host_16(item->anl);

	/* BSSID */
	req->bssid.status = P80211ENUM_msgitem_status_data_ok;
	req->bssid.data.len = WLAN_BSSID_LEN;
	memcpy(req->bssid.data.data, item->bssid, WLAN_BSSID_LEN);

	/* SSID */
	req->ssid.status = P80211ENUM_msgitem_status_data_ok;
	req->ssid.data.len = hfa384x2host_16(item->ssid.len);
	memcpy(req->ssid.data.data, item->ssid.data, req->ssid.data.len);

	/* supported rates */
        for (count = 0; count < 10 ; count++)
                if (item->supprates[count] == 0)
                        break;

#define REQBASICRATE(N) \
	if ((count >= N) && DOT11_RATE5_ISBASIC_GET(item->supprates[(N)-1])) { \
		req->basicrate ## N .data = item->supprates[(N)-1]; \
		req->basicrate ## N .status = P80211ENUM_msgitem_status_data_ok; \
	}

	REQBASICRATE(1);
	REQBASICRATE(2);
	REQBASICRATE(3);
	REQBASICRATE(4);
	REQBASICRATE(5);
	REQBASICRATE(6);
	REQBASICRATE(7);
	REQBASICRATE(8);

#define REQSUPPRATE(N) \
	if (count >= N) { \
		req->supprate ## N .data = item->supprates[(N)-1]; \
		req->supprate ## N .status = P80211ENUM_msgitem_status_data_ok; \
	}

	REQSUPPRATE(1);
	REQSUPPRATE(2);
	REQSUPPRATE(3);
	REQSUPPRATE(4);
	REQSUPPRATE(5);
	REQSUPPRATE(6);
	REQSUPPRATE(7);
	REQSUPPRATE(8);

	/* beacon period */
	req->beaconperiod.status = P80211ENUM_msgitem_status_data_ok;
	req->beaconperiod.data = hfa384x2host_16(item->bcnint);

	/* timestamps */
	req->timestamp.status = P80211ENUM_msgitem_status_data_ok;
	req->timestamp.data = jiffies;
	req->localtime.status = P80211ENUM_msgitem_status_data_ok;
	req->localtime.data = jiffies;

	/* atim window */
	req->ibssatimwindow.status = P80211ENUM_msgitem_status_data_ok;
	req->ibssatimwindow.data = hfa384x2host_16(item->atim);

	/* Channel */
	req->dschannel.status = P80211ENUM_msgitem_status_data_ok;
	req->dschannel.data = hfa384x2host_16(item->chid);

	/* capinfo bits */
	count = hfa384x2host_16(item->capinfo);

	/* privacy flag */
	req->privacy.status = P80211ENUM_msgitem_status_data_ok;
	req->privacy.data = WLAN_GET_MGMT_CAP_INFO_PRIVACY(count);

	/* cfpollable */
	req->cfpollable.status = P80211ENUM_msgitem_status_data_ok;
	req->cfpollable.data = WLAN_GET_MGMT_CAP_INFO_CFPOLLABLE(count);

	/* cfpollreq */
	req->cfpollreq.status = P80211ENUM_msgitem_status_data_ok;
	req->cfpollreq.data = WLAN_GET_MGMT_CAP_INFO_CFPOLLREQ(count);

	/* bsstype */
	req->bsstype.status =  P80211ENUM_msgitem_status_data_ok;
	req->bsstype.data = (WLAN_GET_MGMT_CAP_INFO_ESS(count)) ?
		P80211ENUM_bsstype_infrastructure :
		P80211ENUM_bsstype_independent;

	// item->proberesp_rate
/*
	req->fhdwelltime
	req->fhhopset
	req->fhhoppattern
	req->fhhopindex
        req->cfpdurremaining
*/

	result = 0;
	req->resultcode.data = P80211ENUM_resultcode_success;

 exit:
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_join
*
* Join a BSS whose BSS description was previously obtained with
* a scan.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_join(wlandevice_t *wlandev, void *msgp)
{
	int 			result = 0;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_dot11req_join_t	*msg = msgp;
	DBFENTER;

	if (!hw->ap) {

		/*** STATION ***/

		/* TODO: Implement after scan */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	} else {

		/*** ACCESS POINT ***/

		/* Never supported by APs */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	}

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_p2_join
*
* Join a specific BSS
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_p2_join(wlandevice_t *wlandev, void *msgp)
{
	int 			result = 0;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_join_t	*msg = msgp;
	UINT16			reg;
	p80211pstrd_t		*pstr;
	UINT8			bytebuf[256];
	hfa384x_bytestr_t	*p2bytestr = (hfa384x_bytestr_t*)bytebuf;
        hfa384x_JoinRequest_data_t	joinreq;
	DBFENTER;

	if (!hw->ap) {

		wlandev->macmode = WLAN_MACMODE_NONE;

		/*** STATION ***/
		/* Set the PortType */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_success;

		/* ess port */
		result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFPORTTYPE, 1);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set Port Type\n");
			goto failed;
		}

		/* Set the auth type */
		if ( msg->authtype.data == P80211ENUM_authalg_sharedkey ) {
			reg = HFA384x_CNFAUTHENTICATION_SHAREDKEY;
		} else {
			reg = HFA384x_CNFAUTHENTICATION_OPENSYSTEM;
		}
		result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFAUTHENTICATION, reg);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set Authentication\n");
			goto failed;
		}

		/* Turn off all roaming */
		hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFROAMINGMODE, 3);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to Turn off Roaming\n");
			goto failed;
		}

		/* Basic rates */
                reg = 0;
		if ( msg->basicrate1.status == P80211ENUM_msgitem_status_data_ok ) {
			reg = p80211rate_to_p2bit(msg->basicrate1.data);
		}
		if ( msg->basicrate2.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->basicrate2.data);
		}
		if ( msg->basicrate3.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->basicrate3.data);
		}
		if ( msg->basicrate4.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->basicrate4.data);
		}
		if ( msg->basicrate5.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->basicrate5.data);
		}
		if ( msg->basicrate6.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->basicrate6.data);
		}
		if ( msg->basicrate7.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->basicrate7.data);
		}
		if ( msg->basicrate8.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->basicrate8.data);
		}
		if( reg == 0)
			 reg = 0x03;
		result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFBASICRATES, reg);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set basicrates=%d.\n", reg);
			goto failed;
		}

		/* Operational rates (supprates and txratecontrol) */
		reg = 0;
		if ( msg->operationalrate1.status == P80211ENUM_msgitem_status_data_ok ) {
			reg = p80211rate_to_p2bit(msg->operationalrate1.data);
		}
		if ( msg->operationalrate2.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->operationalrate2.data);
		}
		if ( msg->operationalrate3.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->operationalrate3.data);
		}
		if ( msg->operationalrate4.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->operationalrate4.data);
		}
		if ( msg->operationalrate5.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->operationalrate5.data);
		}
		if ( msg->operationalrate6.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->operationalrate6.data);
		}
		if ( msg->operationalrate7.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->operationalrate7.data);
		}
		if ( msg->operationalrate8.status == P80211ENUM_msgitem_status_data_ok ) {
			reg |= p80211rate_to_p2bit(msg->operationalrate8.data);
		}
		if( reg == 0)
			 reg = 0x0f;
		result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFSUPPRATES, reg);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set supprates=%d.\n", reg);
			goto failed;
		}

 		result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_TXRATECNTL, reg);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set txrates=%d.\n", reg);
			goto failed;
		}

		/* Set the ssid */
		memset(bytebuf, 0, 256);
		pstr = (p80211pstrd_t*)&(msg->ssid.data);
		prism2mgmt_pstr2bytestr(p2bytestr, pstr);
		result = hfa384x_drvr_setconfig(
			hw, HFA384x_RID_CNFDESIREDSSID,
			bytebuf, HFA384x_RID_CNFDESIREDSSID_LEN);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set SSID\n");
			goto failed;
		}

		/* Enable the Port */
		result = hfa384x_cmd_enable(hw, 0);
		if ( result ) {
			WLAN_LOG_ERROR("Enable macport failed, result=%d.\n", result);
			goto failed;
		}

		/* Fill in the join request */
		joinreq.channel = msg->channel.data;
		memcpy( joinreq.bssid, ((unsigned char *) &msg->bssid.data) + 1, WLAN_BSSID_LEN);
		hw->joinreq = joinreq;
		hw->join_ap = 1;

		/* Send the join request */
		result = hfa384x_drvr_setconfig( hw,
			HFA384x_RID_JOINREQUEST,
			&joinreq, HFA384x_RID_JOINREQUEST_LEN);
                if(result != 0) {
			WLAN_LOG_ERROR("Join request failed, result=%d.\n", result);
			goto failed;
		}

	} else {

		/*** ACCESS POINT ***/

		/* Never supported by APs */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	}

        goto done;
failed:
	WLAN_LOG_DEBUG(1, "Failed to set a config option, result=%d\n", result);
	msg->resultcode.data = P80211ENUM_resultcode_invalid_parameters;

done:
        result = 0;

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_authenticate
*
* Station should be begin an authentication exchange.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_authenticate(wlandevice_t *wlandev, void *msgp)
{
	int 			result = 0;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_dot11req_authenticate_t	*msg = msgp;
	DBFENTER;

	if (!hw->ap) {

		/*** STATION ***/

		/* TODO: Decide how we're going to handle this one w/ Prism2 */
		/*       It could be entertaining since Prism2 doesn't have  */
		/*       an explicit way to control this */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	} else {

		/*** ACCESS POINT ***/

		/* Never supported by APs */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	}

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_deauthenticate
*
* Send a deauthenticate notification.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_deauthenticate(wlandevice_t *wlandev, void *msgp)
{
	int 			result = 0;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_dot11req_deauthenticate_t	*msg = msgp;
	DBFENTER;

	if (!hw->ap) {

		/*** STATION ***/

		/* TODO: Decide how we're going to handle this one w/ Prism2 */
		/*       It could be entertaining since Prism2 doesn't have  */
		/*       an explicit way to control this */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	} else {

		/*** ACCESS POINT ***/
		hfa384x_drvr_handover(hw, msg->peerstaaddress.data.data);
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_success;
	}

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_associate
*
* Associate with an ESS.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_associate(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t		*hw = wlandev->priv;
	int 			result = 0;
	p80211msg_dot11req_associate_t	*msg = msgp;
	DBFENTER;

	if (!hw->ap) {

		/*** STATION ***/

#if 0
		/* Set the TxRates */
		reg = 0x000f;
		hfa384x_drvr_setconfig16(hw, HFA384x_RID_TXRATECNTL, reg);
#endif

		/* Set the PortType */
		/* ess port */
		hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFPORTTYPE, 1);

		/* Enable the Port */
		hfa384x_drvr_enable(hw, 0);

		/* Set the resultcode */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_success;

	} else {

		/*** ACCESS POINT ***/

		/* Never supported on AP */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	}

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_reassociate
*
* Renew association because of a BSS change.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_reassociate(wlandevice_t *wlandev, void *msgp)
{
	int 			result = 0;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_dot11req_reassociate_t	*msg = msgp;
	DBFENTER;

	if (!hw->ap) {

		/*** STATION ***/

		/* TODO: Not supported yet...not sure how we're going to do it */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	} else {

		/*** ACCESS POINT ***/

		/* Never supported on AP */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	}

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_disassociate
*
* Send a disassociation notification.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_disassociate(wlandevice_t *wlandev, void *msgp)
{
	int 			result = 0;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_dot11req_disassociate_t	*msg = msgp;
	DBFENTER;

	if (!hw->ap) {

		/*** STATION ***/

		/* TODO: Not supported yet...not sure how to do it */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	} else {

		/*** ACCESS POINT ***/
		hfa384x_drvr_handover(hw, msg->peerstaaddress.data.data);
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_success;
	}

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_reset
*
* Reset the MAC and MSD.  The p80211 layer has it's own handling
* that should be done before and after this function.
* Procedure:
*   - disable system interrupts ??
*   - disable MAC interrupts
*   - restore system interrupts
*   - issue the MAC initialize command
*   - clear any MSD level state (including timers, queued events,
*     etc.).  Note that if we're removing timer'd/queue events, we may
*     need to have remained in the system interrupt disabled state.
*     We should be left in the same state that we're in following
*     driver initialization.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer, MAY BE NULL! for a driver local
*			call.
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread, commonly wlanctl, but might be rmmod/pci_close.
----------------------------------------------------------------*/
int prism2mgmt_reset(wlandevice_t *wlandev, void *msgp)
{
	int 			result = 0;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_dot11req_reset_t	*msg = msgp;
	DBFENTER;

	/*
	 * This is supported on both AP and STA and it's not allowed
	 * to fail.
	 */
	if ( msgp ) {
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_success;
		WLAN_LOG_INFO("dot11req_reset: the macaddress and "
			"setdefaultmib arguments are currently unsupported.\n");
	}

	/*
	 * If we got this far, the MSD must be in the MSDRUNNING state
	 * therefore, we must stop and then restart the hw/MAC combo.
	 */
	hfa384x_drvr_stop(hw);
	result = hfa384x_drvr_start(hw);
	if (result != 0) {
		WLAN_LOG_ERROR("dot11req_reset: Initialize command failed,"
				" bad things will happen from here.\n");
		return 0;
	}

	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* prism2mgmt_start
*
* Start a BSS.  Any station can do this for IBSS, only AP for ESS.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_start(wlandevice_t *wlandev, void *msgp)
{
	int 			result = 0;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_dot11req_start_t	*msg = msgp;

	p80211pstrd_t		*pstr;
	UINT8			bytebuf[80];
	hfa384x_bytestr_t	*p2bytestr = (hfa384x_bytestr_t*)bytebuf;
	hfa384x_PCFInfo_data_t	*pcfinfo = (hfa384x_PCFInfo_data_t*)bytebuf;
	UINT16			word;
	DBFENTER;

	wlandev->macmode = WLAN_MACMODE_NONE;

	/* Set the SSID */
	memcpy(&wlandev->ssid, &msg->ssid.data, sizeof(msg->ssid.data));

	if (!hw->ap) {
		/*** ADHOC IBSS ***/
		/* see if current f/w is less than 8c3 */
		if (HFA384x_FIRMWARE_VERSION(hw->ident_sta_fw.major,
					     hw->ident_sta_fw.minor,
					     hw->ident_sta_fw.variant) <
		    HFA384x_FIRMWARE_VERSION(0,8,3)) {
			/* Ad-Hoc not quite supported on Prism2 */
			msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
			msg->resultcode.data = P80211ENUM_resultcode_not_supported;
			goto done;
		}

  		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;

		/*** STATION ***/
		/* Set the REQUIRED config items */
		/* SSID */
		pstr = (p80211pstrd_t*)&(msg->ssid.data);
		prism2mgmt_pstr2bytestr(p2bytestr, pstr);
		result = hfa384x_drvr_setconfig( hw, HFA384x_RID_CNFOWNSSID,
				bytebuf, HFA384x_RID_CNFOWNSSID_LEN);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set CnfOwnSSID\n");
			goto failed;
		}
		result = hfa384x_drvr_setconfig( hw, HFA384x_RID_CNFDESIREDSSID,
				bytebuf, HFA384x_RID_CNFDESIREDSSID_LEN);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set CnfDesiredSSID\n");
			goto failed;
		}

		/* bsstype - we use the default in the ap firmware */
		/* IBSS port */
		hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFPORTTYPE, 0);

		/* beacon period */
		word = msg->beaconperiod.data;
		result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFAPBCNINT, word);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set beacon period=%d.\n", word);
			goto failed;
		}

		/* dschannel */
		word = msg->dschannel.data;
		result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFOWNCHANNEL, word);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set channel=%d.\n", word);
			goto failed;
		}
		/* Basic rates */
		word = p80211rate_to_p2bit(msg->basicrate1.data);
		if ( msg->basicrate2.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->basicrate2.data);
		}
		if ( msg->basicrate3.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->basicrate3.data);
		}
		if ( msg->basicrate4.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->basicrate4.data);
		}
		if ( msg->basicrate5.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->basicrate5.data);
		}
		if ( msg->basicrate6.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->basicrate6.data);
		}
		if ( msg->basicrate7.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->basicrate7.data);
		}
		if ( msg->basicrate8.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->basicrate8.data);
		}
		result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFBASICRATES, word);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set basicrates=%d.\n", word);
			goto failed;
		}

		/* Operational rates (supprates and txratecontrol) */
		word = p80211rate_to_p2bit(msg->operationalrate1.data);
		if ( msg->operationalrate2.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->operationalrate2.data);
		}
		if ( msg->operationalrate3.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->operationalrate3.data);
		}
		if ( msg->operationalrate4.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->operationalrate4.data);
		}
		if ( msg->operationalrate5.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->operationalrate5.data);
		}
		if ( msg->operationalrate6.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->operationalrate6.data);
		}
		if ( msg->operationalrate7.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->operationalrate7.data);
		}
		if ( msg->operationalrate8.status == P80211ENUM_msgitem_status_data_ok ) {
			word |= p80211rate_to_p2bit(msg->operationalrate8.data);
		}
		result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFSUPPRATES, word);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set supprates=%d.\n", word);
			goto failed;
		}

 		result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_TXRATECNTL, word);
		if ( result ) {
			WLAN_LOG_ERROR("Failed to set txrates=%d.\n", word);
			goto failed;
		}

		/* Set the macmode so the frame setup code knows what to do */
		if ( msg->bsstype.data == P80211ENUM_bsstype_independent ) {
			wlandev->macmode = WLAN_MACMODE_IBSS_STA;
			/* lets extend the data length a bit */
			hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFMAXDATALEN, 2304);
		}

		/* Enable the Port */
		result = hfa384x_drvr_enable(hw, 0);
		if ( result ) {
			WLAN_LOG_ERROR("Enable macport failed, result=%d.\n", result);
			goto failed;
		}

		msg->resultcode.data = P80211ENUM_resultcode_success;

		goto done;
	}

	/*** ACCESS POINT ***/

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;

	/* Validate the command, if BSStype=infra is the tertiary loaded? */
	if ( msg->bsstype.data == P80211ENUM_bsstype_independent ) {
		WLAN_LOG_ERROR("AP driver cannot create IBSS.\n");
		goto failed;
	} else if ( hw->cap_sup_sta.id != 5) {
		WLAN_LOG_ERROR("AP driver failed to detect AP firmware.\n");
		goto failed;
	}

	/* Set the REQUIRED config items */
	/* SSID */
	pstr = (p80211pstrd_t*)&(msg->ssid.data);
	prism2mgmt_pstr2bytestr(p2bytestr, pstr);
	result = hfa384x_drvr_setconfig( hw, HFA384x_RID_CNFOWNSSID,
				bytebuf, HFA384x_RID_CNFOWNSSID_LEN);
	if ( result ) {
		WLAN_LOG_ERROR("Failed to set SSID, result=0x%04x\n", result);
		goto failed;
	}

	/* bsstype - we use the default in the ap firmware */

	/* beacon period */
	word = msg->beaconperiod.data;
	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFAPBCNINT, word);
	if ( result ) {
		WLAN_LOG_ERROR("Failed to set beacon period=%d.\n", word);
		goto failed;
	}

	/* dschannel */
	word = msg->dschannel.data;
	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFOWNCHANNEL, word);
	if ( result ) {
		WLAN_LOG_ERROR("Failed to set channel=%d.\n", word);
		goto failed;
	}
	/* Basic rates */
	word = p80211rate_to_p2bit(msg->basicrate1.data);
	if ( msg->basicrate2.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->basicrate2.data);
	}
	if ( msg->basicrate3.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->basicrate3.data);
	}
	if ( msg->basicrate4.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->basicrate4.data);
	}
	if ( msg->basicrate5.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->basicrate5.data);
	}
	if ( msg->basicrate6.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->basicrate6.data);
	}
	if ( msg->basicrate7.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->basicrate7.data);
	}
	if ( msg->basicrate8.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->basicrate8.data);
	}
	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFBASICRATES, word);
	if ( result ) {
		WLAN_LOG_ERROR("Failed to set basicrates=%d.\n", word);
		goto failed;
	}

	/* Operational rates (supprates and txratecontrol) */
	word = p80211rate_to_p2bit(msg->operationalrate1.data);
	if ( msg->operationalrate2.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->operationalrate2.data);
	}
	if ( msg->operationalrate3.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->operationalrate3.data);
	}
	if ( msg->operationalrate4.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->operationalrate4.data);
	}
	if ( msg->operationalrate5.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->operationalrate5.data);
	}
	if ( msg->operationalrate6.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->operationalrate6.data);
	}
	if ( msg->operationalrate7.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->operationalrate7.data);
	}
	if ( msg->operationalrate8.status == P80211ENUM_msgitem_status_data_ok ) {
		word |= p80211rate_to_p2bit(msg->operationalrate8.data);
	}
	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFSUPPRATES, word);
	if ( result ) {
		WLAN_LOG_ERROR("Failed to set supprates=%d.\n", word);
		goto failed;
	}
	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_TXRATECNTL0, word);
	if ( result ) {
		WLAN_LOG_ERROR("Failed to set txrates=%d.\n", word);
		goto failed;
	}

	/* ibssatimwindow */
	if (msg->ibssatimwindow.status == P80211ENUM_msgitem_status_data_ok) {
		WLAN_LOG_INFO("prism2mgmt_start: atimwindow not used in "
			       "Infrastructure mode, ignored.\n");
	}

	/* DTIM period */
	word = msg->dtimperiod.data;
	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFOWNDTIMPER, word);
	if ( result ) {
		WLAN_LOG_ERROR("Failed to set dtim period=%d.\n", word);
		goto failed;
	}

	/* probedelay */
	if (msg->probedelay.status == P80211ENUM_msgitem_status_data_ok) {
		WLAN_LOG_INFO("prism2mgmt_start: probedelay not "
			       "supported in prism2, ignored.\n");
	}

	/* cfpollable, cfpollreq, cfpperiod, cfpmaxduration */
	if (msg->cfpollable.data == P80211ENUM_truth_true &&
	    msg->cfpollreq.data == P80211ENUM_truth_true ) {
		WLAN_LOG_ERROR("cfpollable=cfpollreq=true is illegal.\n");
		result = -1;
		goto failed;
	}

	/* read the PCFInfo and update */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_CNFAPPCFINFO,
					pcfinfo, HFA384x_RID_CNFAPPCFINFO_LEN);
	if ( result ) {
		WLAN_LOG_INFO("prism2mgmt_start: read(pcfinfo) failed, "
				"assume it's "
				"not supported, pcf settings ignored.\n");
		goto pcf_skip;
	}
	if ((msg->cfpollable.data == P80211ENUM_truth_false &&
	     msg->cfpollreq.data == P80211ENUM_truth_false) ) {
	    	pcfinfo->MediumOccupancyLimit = 0;
		pcfinfo->CFPPeriod = 0;
		pcfinfo->CFPMaxDuration = 0;
		pcfinfo->CFPFlags &= host2hfa384x_16((UINT16)~BIT0);

		if ( msg->cfpperiod.status == P80211ENUM_msgitem_status_data_ok ||
		     msg->cfpmaxduration.status == P80211ENUM_msgitem_status_data_ok ) {
			WLAN_LOG_WARNING(
				"Setting cfpperiod or cfpmaxduration when "
				"cfpollable and cfreq are false is pointless.\n");
		}
	}
	if ((msg->cfpollable.data == P80211ENUM_truth_true ||
	     msg->cfpollreq.data == P80211ENUM_truth_true) ) {
		if ( msg->cfpollable.data == P80211ENUM_truth_true) {
			pcfinfo->CFPFlags |= host2hfa384x_16((UINT16)BIT0);
		}

		if ( msg->cfpperiod.status == P80211ENUM_msgitem_status_data_ok) {
			pcfinfo->CFPPeriod = msg->cfpperiod.data;
			pcfinfo->CFPPeriod = host2hfa384x_16(pcfinfo->CFPPeriod);
		}

		if ( msg->cfpmaxduration.status == P80211ENUM_msgitem_status_data_ok) {
			pcfinfo->CFPMaxDuration = msg->cfpmaxduration.data;
			pcfinfo->CFPMaxDuration = host2hfa384x_16(pcfinfo->CFPMaxDuration);
			pcfinfo->MediumOccupancyLimit = pcfinfo->CFPMaxDuration;
		}
	}
	result = hfa384x_drvr_setconfig(hw, HFA384x_RID_CNFAPPCFINFO,
					pcfinfo, HFA384x_RID_CNFAPPCFINFO_LEN);
	if ( result ) {
		WLAN_LOG_ERROR("write(pcfinfo) failed.\n");
		goto failed;
	}

pcf_skip:
	/* Set the macmode so the frame setup code knows what to do */
	if ( msg->bsstype.data == P80211ENUM_bsstype_infrastructure ) {
		wlandev->macmode = WLAN_MACMODE_ESS_AP;
		/* lets extend the data length a bit */
		hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFMAXDATALEN, 2304);
	}

	/* Set the BSSID to the same as our MAC */
	memcpy( wlandev->bssid, wlandev->netdev->dev_addr, WLAN_BSSID_LEN);

	/* Enable the Port */
	result = hfa384x_drvr_enable(hw, 0);
	if ( result ) {
		WLAN_LOG_ERROR("Enable macport failed, result=%d.\n", result);
		goto failed;
	}

	msg->resultcode.data = P80211ENUM_resultcode_success;

	goto done;
failed:
	WLAN_LOG_DEBUG(1, "Failed to set a config option, result=%d\n", result);
	msg->resultcode.data = P80211ENUM_resultcode_invalid_parameters;

done:
	result = 0;

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_enable
*
* Start a BSS.  Any station can do this for IBSS, only AP for ESS.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_enable(wlandevice_t *wlandev, void *msgp)
{
	int			result = 0;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_enable_t	*msg = msgp;
	DBFENTER;

	if (!hw->ap) {

		/*** STATION ***/

		/* Ad-Hoc not quite supported on Prism2 */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
		goto done;
	}

	/*** ACCESS POINT ***/

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;

	/* Is the tertiary loaded? */
	if ( hw->cap_sup_sta.id != 5) {
		WLAN_LOG_ERROR("AP driver failed to detect AP firmware.\n");
		goto failed;
	}

	/* Set the macmode so the frame setup code knows what to do */
	wlandev->macmode = WLAN_MACMODE_ESS_AP;

	/* Set the BSSID to the same as our MAC */
	memcpy( wlandev->bssid, wlandev->netdev->dev_addr, WLAN_BSSID_LEN);

	/* Enable the Port */
	result = hfa384x_drvr_enable(hw, 0);
	if ( result ) {
		WLAN_LOG_ERROR("Enable macport failed, result=%d.\n", result);
		goto failed;
	}

	msg->resultcode.data = P80211ENUM_resultcode_success;

	goto done;
failed:
	msg->resultcode.data = P80211ENUM_resultcode_invalid_parameters;

done:
	result = 0;

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_readpda
*
* Collect the PDA data and put it in the message.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_readpda(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_readpda_t	*msg = msgp;
	int				result;
	DBFENTER;

	/* We only support collecting the PDA when in the FWLOAD
	 * state.
	 */
	if (wlandev->msdstate != WLAN_MSD_FWLOAD) {
		WLAN_LOG_ERROR(
			"PDA may only be read "
			"in the fwload state.\n");
		msg->resultcode.data =
			P80211ENUM_resultcode_implementation_failure;
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	} else {
		/*  Call drvr_readpda(), it handles the auxport enable
		 *  and validating the returned PDA.
		 */
		result = hfa384x_drvr_readpda(
			hw,
			msg->pda.data,
			HFA384x_PDA_LEN_MAX);
		if (result) {
			WLAN_LOG_ERROR(
				"hfa384x_drvr_readpda() failed, "
				"result=%d\n",
				result);

			msg->resultcode.data =
				P80211ENUM_resultcode_implementation_failure;
			msg->resultcode.status =
				P80211ENUM_msgitem_status_data_ok;
			DBFEXIT;
			return 0;
		}
		msg->pda.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_success;
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	}

	DBFEXIT;
	return 0;
}

/*----------------------------------------------------------------
* prism2mgmt_readcis
*
* Collect the CIS data and put it in the message.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_readcis(wlandevice_t *wlandev, void *msgp)
{
	int			result;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_readcis_t	*msg = msgp;

	DBFENTER;

        memset(msg->cis.data, 0, sizeof(msg->cis.data));

	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_CIS,
					msg->cis.data, HFA384x_RID_CIS_LEN);
	if ( result ) {
		WLAN_LOG_INFO("prism2mgmt_readcis: read(cis) failed.\n");
		msg->cis.status = P80211ENUM_msgitem_status_no_value;
		msg->resultcode.data = P80211ENUM_resultcode_implementation_failure;

		}
	else {
		msg->cis.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_success;
		}

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;

	DBFEXIT;
	return 0;
}

/*----------------------------------------------------------------
* prism2mgmt_auxport_state
*
* Enables/Disables the card's auxiliary port.  Should be called
* before and after a sequence of auxport_read()/auxport_write()
* calls.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_auxport_state(wlandevice_t *wlandev, void *msgp)
{
	p80211msg_p2req_auxport_state_t	*msg = msgp;

#if (WLAN_HOSTIF != WLAN_USB)
	hfa384x_t		*hw = wlandev->priv;
	DBFENTER;

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	if (msg->enable.data == P80211ENUM_truth_true) {
		if ( hfa384x_cmd_aux_enable(hw, 0) ) {
			msg->resultcode.data = P80211ENUM_resultcode_implementation_failure;
		} else {
			msg->resultcode.data = P80211ENUM_resultcode_success;
		}
	} else {
		hfa384x_cmd_aux_disable(hw);
		msg->resultcode.data = P80211ENUM_resultcode_success;
	}

#else /* !USB */
	DBFENTER;

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	msg->resultcode.data = P80211ENUM_resultcode_not_supported;

#endif /* WLAN_HOSTIF != WLAN_USB */

	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* prism2mgmt_auxport_read
*
* Copies data from the card using the auxport.  The auxport must
* have previously been enabled.  Note: this is not the way to
* do downloads, see the [ram|flash]dl functions.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_auxport_read(wlandevice_t *wlandev, void *msgp)
{
#if (WLAN_HOSTIF != WLAN_USB)
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_auxport_read_t	*msg = msgp;
	UINT32			addr;
	UINT32			len;
	UINT8*			buf;
	UINT32			maxlen = sizeof(msg->data.data);
	DBFENTER;

	if ( hw->auxen ) {
		addr = msg->addr.data;
		len = msg->len.data;
		buf = msg->data.data;
		if ( len <= maxlen ) {  /* max read/write size */
			hfa384x_copy_from_aux(hw, addr, HFA384x_AUX_CTL_EXTDS, buf, len);
			msg->resultcode.data = P80211ENUM_resultcode_success;
		} else {
			WLAN_LOG_DEBUG(1,"Attempt to read > maxlen from auxport.\n");
			msg->resultcode.data = P80211ENUM_resultcode_refused;
		}

	} else {
		msg->resultcode.data = P80211ENUM_resultcode_refused;
	}
	msg->data.status = P80211ENUM_msgitem_status_data_ok;
	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;

	DBFEXIT;
	return 0;
#else
	DBFENTER;

	WLAN_LOG_ERROR("prism2mgmt_auxport_read: Not supported on USB.\n");

	DBFEXIT;
	return 0;
#endif
}


/*----------------------------------------------------------------
* prism2mgmt_auxport_write
*
* Copies data to the card using the auxport.  The auxport must
* have previously been enabled.  Note: this is not the way to
* do downloads, see the [ram|flash]dl functions.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_auxport_write(wlandevice_t *wlandev, void *msgp)
{
#if (WLAN_HOSTIF != WLAN_USB)
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_auxport_write_t	*msg = msgp;
	UINT32			addr;
	UINT32			len;
	UINT8*			buf;
	UINT32			maxlen = sizeof(msg->data.data);
	DBFENTER;

	if ( hw->auxen ) {
		addr = msg->addr.data;
		len = msg->len.data;
		buf = msg->data.data;
		if ( len <= maxlen ) {  /* max read/write size */
			hfa384x_copy_to_aux(hw, addr, HFA384x_AUX_CTL_EXTDS, buf, len);
		} else {
			WLAN_LOG_DEBUG(1,"Attempt to write > maxlen from auxport.\n");
			msg->resultcode.data = P80211ENUM_resultcode_refused;
		}

	} else {
		msg->resultcode.data = P80211ENUM_resultcode_refused;
	}
	msg->data.status = P80211ENUM_msgitem_status_data_ok;
	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;

	DBFEXIT;
	return 0;
#else
	DBFENTER;
	WLAN_LOG_ERROR("prism2mgmt_auxport_read: Not supported on USB.\n");
	DBFEXIT;
	return 0;
#endif
}

/*----------------------------------------------------------------
* prism2mgmt_low_level
*
* Puts the card into the desired test mode.
*
* Arguments:
*       wlandev         wlan device structure
*       msgp            ptr to msg buffer
*
* Returns:
*       0       success and done
*       <0      success, but we're waiting for something to finish.
*       >0      an error occurred while handling the message.
* Side effects:
*
* Call context:
*       process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_low_level(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t		*hw = wlandev->priv;
        p80211msg_p2req_low_level_t     *msg = msgp;
	hfa384x_metacmd_t cmd;
        DBFENTER;

        msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;

        /* call some routine to execute the test command */
	cmd.cmd = (UINT16) msg->command.data;
	cmd.parm0 = (UINT16) msg->param0.data;
	cmd.parm1 = (UINT16) msg->param1.data;
	cmd.parm2 = (UINT16) msg->param2.data;

        hfa384x_drvr_low_level(hw,&cmd);

        msg->resp0.data = (UINT32) cmd.result.resp0;
        msg->resp1.data = (UINT32) cmd.result.resp1;
        msg->resp2.data = (UINT32) cmd.result.resp2;

        msg->resultcode.data = P80211ENUM_resultcode_success;

        DBFEXIT;
        return 0;
}

/*----------------------------------------------------------------
* prism2mgmt_test_command
*
* Puts the card into the desired test mode.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_test_command(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_test_command_t	*msg = msgp;
	hfa384x_metacmd_t cmd;

        DBFENTER;

	cmd.cmd = ((UINT16) msg->testcode.data) << 8 | 0x38;
	cmd.parm0 = (UINT16) msg->testparam.data;
	cmd.parm1 = 0;
	cmd.parm2 = 0;

        /* call some routine to execute the test command */

        hfa384x_drvr_low_level(hw,&cmd);

        msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
        msg->resultcode.data = P80211ENUM_resultcode_success;

        msg->status.status = P80211ENUM_msgitem_status_data_ok;
        msg->status.data = cmd.result.status;
        msg->resp0.status = P80211ENUM_msgitem_status_data_ok;
        msg->resp0.data = cmd.result.resp0;
        msg->resp1.status = P80211ENUM_msgitem_status_data_ok;
        msg->resp1.data = cmd.result.resp1;
        msg->resp2.status = P80211ENUM_msgitem_status_data_ok;
        msg->resp2.data = cmd.result.resp2;

	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* prism2mgmt_mmi_read
*
* Read from one of the MMI registers.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_mmi_read(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_mmi_read_t	*msg = msgp;
	UINT32 resp = 0;

	DBFENTER;

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;

	/* call some routine to execute the test command */

	hfa384x_drvr_mmi_read(hw, msg->addr.data, &resp);

	/* I'm not sure if this is "architecturally" correct, but it
           is expedient. */

	msg->value.status = P80211ENUM_msgitem_status_data_ok;
	msg->value.data = resp;
	msg->resultcode.data = P80211ENUM_resultcode_success;

	DBFEXIT;
	return 0;
}

/*----------------------------------------------------------------
* prism2mgmt_mmi_write
*
* Write a data value to one of the MMI registers.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_mmi_write(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_mmi_write_t	*msg = msgp;
	DBFENTER;

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;

	/* call some routine to execute the test command */

	hfa384x_drvr_mmi_write(hw, msg->addr.data, msg->data.data);

	msg->resultcode.data = P80211ENUM_resultcode_success;

	DBFEXIT;
	return 0;
}

/*----------------------------------------------------------------
* prism2mgmt_ramdl_state
*
* Establishes the beginning/end of a card RAM download session.
*
* It is expected that the ramdl_write() function will be called
* one or more times between the 'enable' and 'disable' calls to
* this function.
*
* Note: This function should not be called when a mac comm port
*       is active.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_ramdl_state(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_ramdl_state_t	*msg = msgp;
	DBFENTER;

	if (wlandev->msdstate != WLAN_MSD_FWLOAD) {
		WLAN_LOG_ERROR(
			"ramdl_state(): may only be called "
			"in the fwload state.\n");
		msg->resultcode.data =
			P80211ENUM_resultcode_implementation_failure;
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		DBFEXIT;
		return 0;
	}

	/*
	** Note: Interrupts are locked out if this is an AP and are NOT
	** locked out if this is a station.
	*/

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	if  ( msg->enable.data == P80211ENUM_truth_true ) {
		if ( hfa384x_drvr_ramdl_enable(hw, msg->exeaddr.data) ) {
			msg->resultcode.data = P80211ENUM_resultcode_implementation_failure;
		} else {
			msg->resultcode.data = P80211ENUM_resultcode_success;
		}
	} else {
		hfa384x_drvr_ramdl_disable(hw);
		msg->resultcode.data = P80211ENUM_resultcode_success;
	}

	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* prism2mgmt_ramdl_write
*
* Writes a buffer to the card RAM using the download state.  This
* is for writing code to card RAM.  To just read or write raw data
* use the aux functions.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_ramdl_write(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_ramdl_write_t	*msg = msgp;
	UINT32			addr;
	UINT32			len;
	UINT8			*buf;
	DBFENTER;

	if (wlandev->msdstate != WLAN_MSD_FWLOAD) {
		WLAN_LOG_ERROR(
			"ramdl_write(): may only be called "
			"in the fwload state.\n");
		msg->resultcode.data =
			P80211ENUM_resultcode_implementation_failure;
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		DBFEXIT;
		return 0;
	}

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	/* first validate the length */
	if  ( msg->len.data > sizeof(msg->data.data) ) {
		msg->resultcode.status = P80211ENUM_resultcode_invalid_parameters;
		return 0;
	}
	/* call the hfa384x function to do the write */
	addr = msg->addr.data;
	len = msg->len.data;
	buf = msg->data.data;
	if ( hfa384x_drvr_ramdl_write(hw, addr, buf, len) ) {
		msg->resultcode.data = P80211ENUM_resultcode_refused;

	}
	msg->resultcode.data = P80211ENUM_resultcode_success;

	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* prism2mgmt_flashdl_state
*
* Establishes the beginning/end of a card Flash download session.
*
* It is expected that the flashdl_write() function will be called
* one or more times between the 'enable' and 'disable' calls to
* this function.
*
* Note: This function should not be called when a mac comm port
*       is active.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_flashdl_state(wlandevice_t *wlandev, void *msgp)
{
	int			result = 0;
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_flashdl_state_t	*msg = msgp;
	DBFENTER;

	if (wlandev->msdstate != WLAN_MSD_FWLOAD) {
		WLAN_LOG_ERROR(
			"flashdl_state(): may only be called "
			"in the fwload state.\n");
		msg->resultcode.data =
			P80211ENUM_resultcode_implementation_failure;
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		DBFEXIT;
		return 0;
	}

	/*
	** Note: Interrupts are locked out if this is an AP and are NOT
	** locked out if this is a station.
	*/

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	if  ( msg->enable.data == P80211ENUM_truth_true ) {
		if ( hfa384x_drvr_flashdl_enable(hw) ) {
			msg->resultcode.data = P80211ENUM_resultcode_implementation_failure;
		} else {
			msg->resultcode.data = P80211ENUM_resultcode_success;
		}
	} else {
		hfa384x_drvr_flashdl_disable(hw);
		msg->resultcode.data = P80211ENUM_resultcode_success;
		/* NOTE: At this point, the MAC is in the post-reset
		 * state and the driver is in the fwload state.
		 * We need to get the MAC back into the fwload
		 * state.  To do this, we set the nsdstate to HWPRESENT
		 * and then call the ifstate function to redo everything
		 * that got us into the fwload state.
		 */
		wlandev->msdstate = WLAN_MSD_HWPRESENT;
		result = prism2sta_ifstate(wlandev, P80211ENUM_ifstate_fwload);
		if (result != P80211ENUM_resultcode_success) {
			WLAN_LOG_ERROR("prism2sta_ifstate(fwload) failed,"
				"P80211ENUM_resultcode=%d\n", result);
			msg->resultcode.data =
				P80211ENUM_resultcode_implementation_failure;
			result = -1;
		}
	}

	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* prism2mgmt_flashdl_write
*
*
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_flashdl_write(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t		*hw = wlandev->priv;
	p80211msg_p2req_flashdl_write_t	*msg = msgp;
	UINT32			addr;
	UINT32			len;
	UINT8			*buf;
	DBFENTER;

	if (wlandev->msdstate != WLAN_MSD_FWLOAD) {
		WLAN_LOG_ERROR(
			"flashdl_write(): may only be called "
			"in the fwload state.\n");
		msg->resultcode.data =
			P80211ENUM_resultcode_implementation_failure;
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		DBFEXIT;
		return 0;
	}

	/*
	** Note: Interrupts are locked out if this is an AP and are NOT
	** locked out if this is a station.
	*/

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	/* first validate the length */
	if  ( msg->len.data > sizeof(msg->data.data) ) {
		msg->resultcode.status =
			P80211ENUM_resultcode_invalid_parameters;
		return 0;
	}
	/* call the hfa384x function to do the write */
	addr = msg->addr.data;
	len = msg->len.data;
	buf = msg->data.data;
	if ( hfa384x_drvr_flashdl_write(hw, addr, buf, len) ) {
		msg->resultcode.data = P80211ENUM_resultcode_refused;

	}
	msg->resultcode.data = P80211ENUM_resultcode_success;

	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* prism2mgmt_dump_state
*
* Dumps the driver's and hardware's current state via the kernel
* log at KERN_NOTICE level.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_dump_state(wlandevice_t *wlandev, void *msgp)
{
	p80211msg_p2req_dump_state_t	*msg = msgp;
	int				result = 0;

#if (WLAN_HOSTIF != WLAN_USB)
	hfa384x_t		*hw = wlandev->priv;
	UINT16				auxbuf[15];
	DBFENTER;

	WLAN_LOG_NOTICE("prism2 driver and hardware state:\n");
	if  ( (result = hfa384x_cmd_aux_enable(hw, 0)) ) {
		WLAN_LOG_ERROR("aux_enable failed, result=%d\n", result);
		goto failed;
	}
	hfa384x_copy_from_aux(hw,
		0x01e2,
		HFA384x_AUX_CTL_EXTDS,
		auxbuf,
		sizeof(auxbuf));
	hfa384x_cmd_aux_disable(hw);
	WLAN_LOG_NOTICE("  cmac: FreeBlocks=%d\n", auxbuf[5]);
	WLAN_LOG_NOTICE("  cmac: IntEn=0x%02x EvStat=0x%02x\n",
		hfa384x_getreg(hw, HFA384x_INTEN),
		hfa384x_getreg(hw, HFA384x_EVSTAT));

	#ifdef USE_FID_STACK
	WLAN_LOG_NOTICE("  drvr: txfid_top=%d stacksize=%d\n",
		hw->txfid_top,HFA384x_DRVR_FIDSTACKLEN_MAX);
	#else
	WLAN_LOG_NOTICE("  drvr: txfid_head=%d txfid_tail=%d txfid_N=%d\n",
		hw->txfid_head, hw->txfid_tail, hw->txfid_N);
	#endif

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	msg->resultcode.data = P80211ENUM_resultcode_success;

#else /* (WLAN_HOSTIF == WLAN_USB) */

	DBFENTER;

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	msg->resultcode.data = P80211ENUM_resultcode_not_supported;
	goto failed;

#endif /* (WLAN_HOSTIF != WLAN_USB) */

failed:
	DBFEXIT;
	return result;
}

/*----------------------------------------------------------------
* prism2mgmt_channel_info
*
* Issues a ChannelInfoRequest.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_channel_info(wlandevice_t *wlandev, void *msgp)
{
	p80211msg_p2req_channel_info_t	*msg=msgp;
	hfa384x_t			*hw = wlandev->priv;
	int				result, i, n=0;
	UINT16				channel_mask=0;
	hfa384x_ChannelInfoRequest_data_t	chinforeq;
	// unsigned long 			now;

	DBFENTER;

	if (!hw->ap) {

		/*** STATION ***/

		/* Not supported in STA f/w */
		P80211_SET_INT(msg->resultcode, P80211ENUM_resultcode_not_supported);
		goto done;
	}

	/*** ACCESS POINT ***/

#define CHINFO_TIMEOUT 2

	P80211_SET_INT(msg->resultcode, P80211ENUM_resultcode_success);

	/* setting default value for channellist = all channels */
	if (!msg->channellist.data) {
		P80211_SET_INT(msg->channellist, 0x00007FFE);
	}
	/* setting default value for channeldwelltime = 100 ms */
	if (!msg->channeldwelltime.data) {
		P80211_SET_INT(msg->channeldwelltime, 100);
	}
	channel_mask = (UINT16) (msg->channellist.data >> 1);
	for (i=0, n=0; i < 14; i++) {
		if (channel_mask & (1<<i)) {
			n++;
		}
	}
	P80211_SET_INT(msg->numchinfo, n);
	chinforeq.channelList = host2hfa384x_16(channel_mask);
	chinforeq.channelDwellTime = host2hfa384x_16(msg->channeldwelltime.data);

	atomic_set(&hw->channel_info.done, 1);

	result = hfa384x_drvr_setconfig( hw, HFA384x_RID_CHANNELINFOREQUEST,
					 &chinforeq, HFA384x_RID_CHANNELINFOREQUEST_LEN);
	if ( result ) {
		WLAN_LOG_ERROR("setconfig(CHANNELINFOREQUEST) failed. result=%d\n",
				result);
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
		goto done;
	}
	/*
	now = jiffies;
	while (atomic_read(&hw->channel_info.done) != 1) {
		if ((jiffies - now) > CHINFO_TIMEOUT*HZ) {
			WLAN_LOG_NOTICE("ChannelInfo results not received in %d seconds, aborting.\n",
					CHINFO_TIMEOUT);
			msg->resultcode.data = P80211ENUM_resultcode_timeout;
			goto done;
		}
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/4);
		current->state = TASK_RUNNING;
	}
	*/

done:

	DBFEXIT;
	return 0;
}

/*----------------------------------------------------------------
* prism2mgmt_channel_info_results
*
* Returns required ChannelInfo result.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
----------------------------------------------------------------*/
int prism2mgmt_channel_info_results(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t			*hw = wlandev->priv;

	p80211msg_p2req_channel_info_results_t	*msg=msgp;
	int				result=0;
	int		channel;

	DBFENTER;

	if (!hw->ap) {

		/*** STATION ***/

		/* Not supported in STA f/w */
		P80211_SET_INT(msg->resultcode, P80211ENUM_resultcode_not_supported);
		goto done;
	}

	/*** ACCESS POINT ***/

	switch (atomic_read(&hw->channel_info.done)) {
	case 0: msg->resultcode.status = P80211ENUM_msgitem_status_no_value;
		goto done;
	case 1: msg->resultcode.status = P80211ENUM_msgitem_status_incomplete_itemdata;
		goto done;
	}

	P80211_SET_INT(msg->resultcode, P80211ENUM_resultcode_success);
	channel=msg->channel.data-1;

	if (channel < 0 || ! (hw->channel_info.results.scanchannels & 1<<channel) ) {
		msg->resultcode.data = P80211ENUM_resultcode_invalid_parameters;
		goto done;
	}
	WLAN_LOG_DEBUG(2, "chinfo_results: channel %d, avg/peak level=%d/%d dB, active=%d\n",
			channel+1,
			hw->channel_info.results.result[channel].anl,
			hw->channel_info.results.result[channel].pnl,
			hw->channel_info.results.result[channel].active
		);
	P80211_SET_INT(msg->avgnoiselevel, hw->channel_info.results.result[channel].anl);
	P80211_SET_INT(msg->peaknoiselevel, hw->channel_info.results.result[channel].pnl);
	P80211_SET_INT(msg->bssactive, hw->channel_info.results.result[channel].active &
		HFA384x_CHINFORESULT_BSSACTIVE
                ? P80211ENUM_truth_true
                : P80211ENUM_truth_false) ;
	P80211_SET_INT(msg->pcfactive, hw->channel_info.results.result[channel].active &
		HFA384x_CHINFORESULT_PCFACTIVE
                ? P80211ENUM_truth_true
                : P80211ENUM_truth_false) ;

done:
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_autojoin
*
* Associate with an ESS.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_autojoin(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t			*hw = wlandev->priv;
	int 			result = 0;
	UINT16			reg;
	UINT16			port_type;
	p80211msg_lnxreq_autojoin_t	*msg = msgp;
	p80211pstrd_t		*pstr;
	UINT8			bytebuf[256];
	hfa384x_bytestr_t	*p2bytestr = (hfa384x_bytestr_t*)bytebuf;
	DBFENTER;

	wlandev->macmode = WLAN_MACMODE_NONE;

	/* Set the SSID */
	memcpy(&wlandev->ssid, &msg->ssid.data, sizeof(msg->ssid.data));

	if (hw->ap) {

		/*** ACCESS POINT ***/

		/* Never supported on AP */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
		goto done;
	}

	/* Disable the Port */
	hfa384x_drvr_disable(hw, 0);

	/*** STATION ***/
	/* Set the TxRates */
	hfa384x_drvr_setconfig16(hw, HFA384x_RID_TXRATECNTL, 0x000f);

	/* Set the auth type */
	if ( msg->authtype.data == P80211ENUM_authalg_sharedkey ) {
		reg = HFA384x_CNFAUTHENTICATION_SHAREDKEY;
	} else {
		reg = HFA384x_CNFAUTHENTICATION_OPENSYSTEM;
	}
	hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFAUTHENTICATION, reg);

	/* Set the ssid */
	memset(bytebuf, 0, 256);
	pstr = (p80211pstrd_t*)&(msg->ssid.data);
	prism2mgmt_pstr2bytestr(p2bytestr, pstr);
        result = hfa384x_drvr_setconfig(
			hw, HFA384x_RID_CNFDESIREDSSID,
			bytebuf, HFA384x_RID_CNFDESIREDSSID_LEN);
#if 0
	/* we can use the new-fangled auto-unknown mode if the firmware
	   is 1.3.3 or newer */
	if (HFA384x_FIRMARE_VERSION(hw->ident_sta_fw.major,
				    hw->ident_sta_fw.minor,
				    hw->ident_sta_fw.variant) >=
	    HFA384x_FIRMWARE_VERSION(1,3,3)) {
		/* Set up the IBSS options */
		reg =  HFA384x_CREATEIBSS_JOINESS_JOINCREATEIBSS;
		hfa384x_drvr_setconfig16(hw, HFA384x_RID_CREATEIBSS, reg);

		/* Set the PortType */
		port_type = HFA384x_PORTTYPE_IBSS;
	} else {
		port_type = HFA384x_PORTTYPE_BSS;
	}
#else
	port_type = HFA384x_PORTTYPE_BSS;
#endif
	/* Set the PortType */
	hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFPORTTYPE, port_type);

	/* Enable the Port */
	hfa384x_drvr_enable(hw, 0);

	/* Set the resultcode */
	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	msg->resultcode.data = P80211ENUM_resultcode_success;

done:
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2mgmt_wlansniff
*
* Start or stop sniffing.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
int prism2mgmt_wlansniff(wlandevice_t *wlandev, void *msgp)
{
	int 			result = 0;
	p80211msg_lnxreq_wlansniff_t	*msg = msgp;

	hfa384x_t			*hw = wlandev->priv;
	UINT16			word;

	DBFENTER;

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	switch (msg->enable.data)
	{
	case P80211ENUM_truth_false:
		/* Confirm that we're in monitor mode */
		if ( wlandev->netdev->type == ARPHRD_ETHER ) {
			msg->resultcode.data = P80211ENUM_resultcode_invalid_parameters;
			result = 0;
			goto exit;
		}
		/* Disable monitor mode */
		result = hfa384x_cmd_monitor(hw, HFA384x_MONITOR_DISABLE);
		if ( result ) {
			WLAN_LOG_DEBUG(1,
				"failed to disable monitor mode, result=%d\n",
				result);
			goto failed;
		}
		/* Disable port 0 */
		result = hfa384x_drvr_disable(hw, 0);
		if ( result ) {
			WLAN_LOG_DEBUG(1,
			"failed to disable port 0 after sniffing, result=%d\n",
			result);
			goto failed;
		}
		/* Clear the driver state */
		wlandev->netdev->type = ARPHRD_ETHER;

		/* Restore the wepflags */
		result = hfa384x_drvr_setconfig16(hw,
				HFA384x_RID_CNFWEPFLAGS,
				hw->presniff_wepflags);
		if ( result ) {
			WLAN_LOG_DEBUG(1,
			"failed to restore wepflags=0x%04x, result=%d\n",
			hw->presniff_wepflags,
			result);
			goto failed;
		}

		/* Set the port to its prior type and enable (if necessary) */
		if (hw->presniff_port_type != 0 ) {
			word = hw->presniff_port_type;
			result = hfa384x_drvr_setconfig16(hw,
				HFA384x_RID_CNFPORTTYPE, word);
			if ( result ) {
				WLAN_LOG_DEBUG(1,
				"failed to restore porttype, result=%d\n",
				result);
				goto failed;
			}

			/* Enable the port */
			result = hfa384x_drvr_enable(hw, 0);
			if ( result ) {
				WLAN_LOG_DEBUG(1, "failed to enable port to presniff setting, result=%d\n", result);
				goto failed;
			}
		} else {
			result = hfa384x_drvr_disable(hw, 0);

		}

		WLAN_LOG_INFO("monitor mode disabled\n");
		msg->resultcode.data = P80211ENUM_resultcode_success;
		result = 0;
		goto exit;
		break;
	case P80211ENUM_truth_true:
		/* Disable the port (if enabled), only check Port 0 */
		if ( hw->port_enabled[0]) {
			if (wlandev->netdev->type == ARPHRD_ETHER) {
				/* Save macport 0 state */
				result = hfa384x_drvr_getconfig16(hw,
								  HFA384x_RID_CNFPORTTYPE,
								  &(hw->presniff_port_type));
				if ( result ) {
					WLAN_LOG_DEBUG(1,"failed to read porttype, result=%d\n", result);
					goto failed;
				}
				/* Save the wepflags state */
				result = hfa384x_drvr_getconfig16(hw,
								  HFA384x_RID_CNFWEPFLAGS,
								  &(hw->presniff_wepflags));
				if ( result ) {
					WLAN_LOG_DEBUG(1,"failed to read wepflags, result=%d\n", result);
					goto failed;
				}
				hfa384x_drvr_stop(hw);
				result = hfa384x_drvr_start(hw);
				if ( result ) {
					WLAN_LOG_DEBUG(1,
						       "failed to restart the card for sniffing, result=%d\n",
						       result);
					goto failed;
				}
			} else {
				/* Disable the port */
				result = hfa384x_drvr_disable(hw, 0);
				if ( result ) {
					WLAN_LOG_DEBUG(1,
						       "failed to enable port for sniffing, result=%d\n",
						       result);
					goto failed;
				}
			}
		} else {
			hw->presniff_port_type = 0;
		}

		/* Set the channel we wish to sniff  */
		word = msg->channel.data;
		result = hfa384x_drvr_setconfig16(hw,
						  HFA384x_RID_CNFOWNCHANNEL, word);
		hw->sniff_channel=word;

		if ( result ) {
			WLAN_LOG_DEBUG(1,
				       "failed to set channel %d, result=%d\n",
					       word,
				       result);
			goto failed;
		}

		/* Now if we're already sniffing, we can skip the rest */
		if (wlandev->netdev->type != ARPHRD_ETHER) {
			/* Set the port type to pIbss */
			word = HFA384x_PORTTYPE_PSUEDOIBSS;
			result = hfa384x_drvr_setconfig16(hw,
							  HFA384x_RID_CNFPORTTYPE, word);
			if ( result ) {
				WLAN_LOG_DEBUG(1,
					       "failed to set porttype %d, result=%d\n",
					       word,
					       result);
				goto failed;
			}
			if ((msg->keepwepflags.status == P80211ENUM_msgitem_status_data_ok) && (msg->keepwepflags.data != P80211ENUM_truth_true)) {
				/* Set the wepflags for no decryption */
				word = HFA384x_WEPFLAGS_DISABLE_TXCRYPT |
					HFA384x_WEPFLAGS_DISABLE_RXCRYPT;
				result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFWEPFLAGS, word);
			}

			if ( result ) {
				WLAN_LOG_DEBUG(1,
					       "failed to set wepflags=0x%04x, result=%d\n",
					       word,
					       result);
				goto failed;
			}
		}

		/* Do we want to strip the FCS in monitor mode? */
		if ((msg->stripfcs.status == P80211ENUM_msgitem_status_data_ok) && (msg->stripfcs.data == P80211ENUM_truth_true)) {
			hw->sniff_fcs = 0;
		} else {
			hw->sniff_fcs = 1;
		}

		/* Do we want to truncate the packets? */
		if (msg->packet_trunc.status == P80211ENUM_msgitem_status_data_ok) {
			hw->sniff_truncate = msg->packet_trunc.data;
		} else {
			hw->sniff_truncate = 0;
		}

		/* Enable the port */
		result = hfa384x_drvr_enable(hw, 0);
		if ( result ) {
			WLAN_LOG_DEBUG(1,
			"failed to enable port for sniffing, result=%d\n",
			result);
			goto failed;
		}
		/* Enable monitor mode */
		result = hfa384x_cmd_monitor(hw, HFA384x_MONITOR_ENABLE);
		if ( result ) {
			WLAN_LOG_DEBUG(1,
			"failed to enable monitor mode, result=%d\n",
			result);
			goto failed;
		}

		if (wlandev->netdev->type == ARPHRD_ETHER) {
			WLAN_LOG_INFO("monitor mode enabled\n");
		}

		/* Set the driver state */
		/* Do we want the prism2 header? */
		if ((msg->prismheader.status == P80211ENUM_msgitem_status_data_ok) && (msg->prismheader.data == P80211ENUM_truth_true)) {
			hw->sniffhdr = 0;
			wlandev->netdev->type = ARPHRD_IEEE80211_PRISM;
		} else if ((msg->wlanheader.status == P80211ENUM_msgitem_status_data_ok) && (msg->wlanheader.data == P80211ENUM_truth_true)) {
			hw->sniffhdr = 1;
			wlandev->netdev->type = ARPHRD_IEEE80211_PRISM;
		} else {
			wlandev->netdev->type = ARPHRD_IEEE80211;
		}

		msg->resultcode.data = P80211ENUM_resultcode_success;
		result = 0;
		goto exit;
		break;
	default:
		msg->resultcode.data = P80211ENUM_resultcode_invalid_parameters;
		result = 0;
		goto exit;
		break;
	}

failed:
	msg->resultcode.data = P80211ENUM_resultcode_refused;
	result = 0;
exit:

	DBFEXIT;
	return result;
}
