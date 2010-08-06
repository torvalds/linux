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

#include <linux/if_arp.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <asm/byteorder.h>
#include <linux/random.h>
#include <linux/usb.h>
#include <linux/bitops.h>

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

/* Converts 802.11 format rate specifications to prism2 */
#define p80211rate_to_p2bit(n)	((((n)&~BIT(7)) == 2) ? BIT(0) :  \
				 (((n)&~BIT(7)) == 4) ? BIT(1) : \
				 (((n)&~BIT(7)) == 11) ? BIT(2) : \
				 (((n)&~BIT(7)) == 22) ? BIT(3) : 0)

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
	int result = 0;
	hfa384x_t *hw = wlandev->priv;
	p80211msg_dot11req_scan_t *msg = msgp;
	u16 roamingmode, word;
	int i, timeout;
	int istmpenable = 0;

	hfa384x_HostScanRequest_data_t scanreq;

	/* gatekeeper check */
	if (HFA384x_FIRMWARE_VERSION(hw->ident_sta_fw.major,
				     hw->ident_sta_fw.minor,
				     hw->ident_sta_fw.variant) <
	    HFA384x_FIRMWARE_VERSION(1, 3, 2)) {
		printk(KERN_ERR
		       "HostScan not supported with current firmware (<1.3.2).\n");
		result = 1;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
		goto exit;
	}

	memset(&scanreq, 0, sizeof(scanreq));

	/* save current roaming mode */
	result = hfa384x_drvr_getconfig16(hw,
					  HFA384x_RID_CNFROAMINGMODE,
					  &roamingmode);
	if (result) {
		printk(KERN_ERR "getconfig(ROAMMODE) failed. result=%d\n",
		       result);
		msg->resultcode.data =
		    P80211ENUM_resultcode_implementation_failure;
		goto exit;
	}

	/* drop into mode 3 for the scan */
	result = hfa384x_drvr_setconfig16(hw,
					  HFA384x_RID_CNFROAMINGMODE,
					  HFA384x_ROAMMODE_HOSTSCAN_HOSTROAM);
	if (result) {
		printk(KERN_ERR "setconfig(ROAMINGMODE) failed. result=%d\n",
		       result);
		msg->resultcode.data =
		    P80211ENUM_resultcode_implementation_failure;
		goto exit;
	}

	/* active or passive? */
	if (HFA384x_FIRMWARE_VERSION(hw->ident_sta_fw.major,
				     hw->ident_sta_fw.minor,
				     hw->ident_sta_fw.variant) >
	    HFA384x_FIRMWARE_VERSION(1, 5, 0)) {
		if (msg->scantype.data != P80211ENUM_scantype_active)
			word = cpu_to_le16(msg->maxchanneltime.data);
		else
			word = 0;

		result =
		    hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFPASSIVESCANCTRL,
					     word);
		if (result) {
			printk(KERN_WARNING "Passive scan not supported with "
			       "current firmware.  (<1.5.1)\n");
		}
	}

	/* set up the txrate to be 2MBPS. Should be fastest basicrate... */
	word = HFA384x_RATEBIT_2;
	scanreq.txRate = cpu_to_le16(word);

	/* set up the channel list */
	word = 0;
	for (i = 0; i < msg->channellist.data.len; i++) {
		u8 channel = msg->channellist.data.data[i];
		if (channel > 14)
			continue;
		/* channel 1 is BIT 0 ... channel 14 is BIT 13 */
		word |= (1 << (channel - 1));
	}
	scanreq.channelList = cpu_to_le16(word);

	/* set up the ssid, if present. */
	scanreq.ssid.len = cpu_to_le16(msg->ssid.data.len);
	memcpy(scanreq.ssid.data, msg->ssid.data.data, msg->ssid.data.len);

	/* Enable the MAC port if it's not already enabled  */
	result = hfa384x_drvr_getconfig16(hw, HFA384x_RID_PORTSTATUS, &word);
	if (result) {
		printk(KERN_ERR "getconfig(PORTSTATUS) failed. "
		       "result=%d\n", result);
		msg->resultcode.data =
		    P80211ENUM_resultcode_implementation_failure;
		goto exit;
	}
	if (word == HFA384x_PORTSTATUS_DISABLED) {
		u16 wordbuf[17];

		result = hfa384x_drvr_setconfig16(hw,
						  HFA384x_RID_CNFROAMINGMODE,
						  HFA384x_ROAMMODE_HOSTSCAN_HOSTROAM);
		if (result) {
			printk(KERN_ERR
			       "setconfig(ROAMINGMODE) failed. result=%d\n",
			       result);
			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
		/* Construct a bogus SSID and assign it to OwnSSID and
		 * DesiredSSID
		 */
		wordbuf[0] = cpu_to_le16(WLAN_SSID_MAXLEN);
		get_random_bytes(&wordbuf[1], WLAN_SSID_MAXLEN);
		result = hfa384x_drvr_setconfig(hw, HFA384x_RID_CNFOWNSSID,
						wordbuf,
						HFA384x_RID_CNFOWNSSID_LEN);
		if (result) {
			printk(KERN_ERR "Failed to set OwnSSID.\n");
			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
		result = hfa384x_drvr_setconfig(hw, HFA384x_RID_CNFDESIREDSSID,
						wordbuf,
						HFA384x_RID_CNFDESIREDSSID_LEN);
		if (result) {
			printk(KERN_ERR "Failed to set DesiredSSID.\n");
			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
		/* bsstype */
		result = hfa384x_drvr_setconfig16(hw,
						  HFA384x_RID_CNFPORTTYPE,
						  HFA384x_PORTTYPE_IBSS);
		if (result) {
			printk(KERN_ERR "Failed to set CNFPORTTYPE.\n");
			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
		/* ibss options */
		result = hfa384x_drvr_setconfig16(hw,
						  HFA384x_RID_CREATEIBSS,
						  HFA384x_CREATEIBSS_JOINCREATEIBSS);
		if (result) {
			printk(KERN_ERR "Failed to set CREATEIBSS.\n");
			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
		result = hfa384x_drvr_enable(hw, 0);
		if (result) {
			printk(KERN_ERR "drvr_enable(0) failed. "
			       "result=%d\n", result);
			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
		istmpenable = 1;
	}

	/* Figure out our timeout first Kus, then HZ */
	timeout = msg->channellist.data.len * msg->maxchanneltime.data;
	timeout = (timeout * HZ) / 1000;

	/* Issue the scan request */
	hw->scanflag = 0;

	result = hfa384x_drvr_setconfig(hw,
					HFA384x_RID_HOSTSCAN, &scanreq,
					sizeof(hfa384x_HostScanRequest_data_t));
	if (result) {
		printk(KERN_ERR "setconfig(SCANREQUEST) failed. result=%d\n",
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
		if (result) {
			printk(KERN_ERR "drvr_disable(0) failed. "
			       "result=%d\n", result);
			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
			goto exit;
		}
	}

	/* restore original roaming mode */
	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFROAMINGMODE,
					  roamingmode);
	if (result) {
		printk(KERN_ERR "setconfig(ROAMMODE) failed. result=%d\n",
		       result);
		msg->resultcode.data =
		    P80211ENUM_resultcode_implementation_failure;
		goto exit;
	}

	result = 0;
	msg->resultcode.data = P80211ENUM_resultcode_success;

exit:
	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;

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
	int result = 0;
	p80211msg_dot11req_scan_results_t *req;
	hfa384x_t *hw = wlandev->priv;
	hfa384x_HScanResultSub_t *item = NULL;

	int count;

	req = (p80211msg_dot11req_scan_results_t *) msgp;

	req->resultcode.status = P80211ENUM_msgitem_status_data_ok;

	if (!hw->scanresults) {
		printk(KERN_ERR
		       "dot11req_scan_results can only be used after a successful dot11req_scan.\n");
		result = 2;
		req->resultcode.data = P80211ENUM_resultcode_invalid_parameters;
		goto exit;
	}

	count = (hw->scanresults->framelen - 3) / 32;
	if (count > 32)
		count = 32;

	if (req->bssindex.data >= count) {
		pr_debug("requested index (%d) out of range (%d)\n",
			 req->bssindex.data, count);
		result = 2;
		req->resultcode.data = P80211ENUM_resultcode_invalid_parameters;
		goto exit;
	}

	item = &(hw->scanresults->info.hscanresult.result[req->bssindex.data]);
	/* signal and noise */
	req->signal.status = P80211ENUM_msgitem_status_data_ok;
	req->noise.status = P80211ENUM_msgitem_status_data_ok;
	req->signal.data = le16_to_cpu(item->sl);
	req->noise.data = le16_to_cpu(item->anl);

	/* BSSID */
	req->bssid.status = P80211ENUM_msgitem_status_data_ok;
	req->bssid.data.len = WLAN_BSSID_LEN;
	memcpy(req->bssid.data.data, item->bssid, WLAN_BSSID_LEN);

	/* SSID */
	req->ssid.status = P80211ENUM_msgitem_status_data_ok;
	req->ssid.data.len = le16_to_cpu(item->ssid.len);
	memcpy(req->ssid.data.data, item->ssid.data, req->ssid.data.len);

	/* supported rates */
	for (count = 0; count < 10; count++)
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
	req->beaconperiod.data = le16_to_cpu(item->bcnint);

	/* timestamps */
	req->timestamp.status = P80211ENUM_msgitem_status_data_ok;
	req->timestamp.data = jiffies;
	req->localtime.status = P80211ENUM_msgitem_status_data_ok;
	req->localtime.data = jiffies;

	/* atim window */
	req->ibssatimwindow.status = P80211ENUM_msgitem_status_data_ok;
	req->ibssatimwindow.data = le16_to_cpu(item->atim);

	/* Channel */
	req->dschannel.status = P80211ENUM_msgitem_status_data_ok;
	req->dschannel.data = le16_to_cpu(item->chid);

	/* capinfo bits */
	count = le16_to_cpu(item->capinfo);

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
	req->bsstype.status = P80211ENUM_msgitem_status_data_ok;
	req->bsstype.data = (WLAN_GET_MGMT_CAP_INFO_ESS(count)) ?
	    P80211ENUM_bsstype_infrastructure : P80211ENUM_bsstype_independent;

	result = 0;
	req->resultcode.data = P80211ENUM_resultcode_success;

exit:
	return result;
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
	int result = 0;
	hfa384x_t *hw = wlandev->priv;
	p80211msg_dot11req_start_t *msg = msgp;

	p80211pstrd_t *pstr;
	u8 bytebuf[80];
	hfa384x_bytestr_t *p2bytestr = (hfa384x_bytestr_t *) bytebuf;
	u16 word;

	wlandev->macmode = WLAN_MACMODE_NONE;

	/* Set the SSID */
	memcpy(&wlandev->ssid, &msg->ssid.data, sizeof(msg->ssid.data));

	/*** ADHOC IBSS ***/
	/* see if current f/w is less than 8c3 */
	if (HFA384x_FIRMWARE_VERSION(hw->ident_sta_fw.major,
				     hw->ident_sta_fw.minor,
				     hw->ident_sta_fw.variant) <
	    HFA384x_FIRMWARE_VERSION(0, 8, 3)) {
		/* Ad-Hoc not quite supported on Prism2 */
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
		goto done;
	}

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;

	/*** STATION ***/
	/* Set the REQUIRED config items */
	/* SSID */
	pstr = (p80211pstrd_t *) &(msg->ssid.data);
	prism2mgmt_pstr2bytestr(p2bytestr, pstr);
	result = hfa384x_drvr_setconfig(hw, HFA384x_RID_CNFOWNSSID,
					bytebuf, HFA384x_RID_CNFOWNSSID_LEN);
	if (result) {
		printk(KERN_ERR "Failed to set CnfOwnSSID\n");
		goto failed;
	}
	result = hfa384x_drvr_setconfig(hw, HFA384x_RID_CNFDESIREDSSID,
					bytebuf,
					HFA384x_RID_CNFDESIREDSSID_LEN);
	if (result) {
		printk(KERN_ERR "Failed to set CnfDesiredSSID\n");
		goto failed;
	}

	/* bsstype - we use the default in the ap firmware */
	/* IBSS port */
	hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFPORTTYPE, 0);

	/* beacon period */
	word = msg->beaconperiod.data;
	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFAPBCNint, word);
	if (result) {
		printk(KERN_ERR "Failed to set beacon period=%d.\n", word);
		goto failed;
	}

	/* dschannel */
	word = msg->dschannel.data;
	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFOWNCHANNEL, word);
	if (result) {
		printk(KERN_ERR "Failed to set channel=%d.\n", word);
		goto failed;
	}
	/* Basic rates */
	word = p80211rate_to_p2bit(msg->basicrate1.data);
	if (msg->basicrate2.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->basicrate2.data);

	if (msg->basicrate3.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->basicrate3.data);

	if (msg->basicrate4.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->basicrate4.data);

	if (msg->basicrate5.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->basicrate5.data);

	if (msg->basicrate6.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->basicrate6.data);

	if (msg->basicrate7.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->basicrate7.data);

	if (msg->basicrate8.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->basicrate8.data);

	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFBASICRATES, word);
	if (result) {
		printk(KERN_ERR "Failed to set basicrates=%d.\n", word);
		goto failed;
	}

	/* Operational rates (supprates and txratecontrol) */
	word = p80211rate_to_p2bit(msg->operationalrate1.data);
	if (msg->operationalrate2.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->operationalrate2.data);

	if (msg->operationalrate3.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->operationalrate3.data);

	if (msg->operationalrate4.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->operationalrate4.data);

	if (msg->operationalrate5.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->operationalrate5.data);

	if (msg->operationalrate6.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->operationalrate6.data);

	if (msg->operationalrate7.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->operationalrate7.data);

	if (msg->operationalrate8.status == P80211ENUM_msgitem_status_data_ok)
		word |= p80211rate_to_p2bit(msg->operationalrate8.data);

	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFSUPPRATES, word);
	if (result) {
		printk(KERN_ERR "Failed to set supprates=%d.\n", word);
		goto failed;
	}

	result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_TXRATECNTL, word);
	if (result) {
		printk(KERN_ERR "Failed to set txrates=%d.\n", word);
		goto failed;
	}

	/* Set the macmode so the frame setup code knows what to do */
	if (msg->bsstype.data == P80211ENUM_bsstype_independent) {
		wlandev->macmode = WLAN_MACMODE_IBSS_STA;
		/* lets extend the data length a bit */
		hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFMAXDATALEN, 2304);
	}

	/* Enable the Port */
	result = hfa384x_drvr_enable(hw, 0);
	if (result) {
		printk(KERN_ERR "Enable macport failed, result=%d.\n", result);
		goto failed;
	}

	msg->resultcode.data = P80211ENUM_resultcode_success;

	goto done;
failed:
	pr_debug("Failed to set a config option, result=%d\n", result);
	msg->resultcode.data = P80211ENUM_resultcode_invalid_parameters;

done:
	result = 0;

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
	hfa384x_t *hw = wlandev->priv;
	p80211msg_p2req_readpda_t *msg = msgp;
	int result;

	/* We only support collecting the PDA when in the FWLOAD
	 * state.
	 */
	if (wlandev->msdstate != WLAN_MSD_FWLOAD) {
		printk(KERN_ERR
		       "PDA may only be read " "in the fwload state.\n");
		msg->resultcode.data =
		    P80211ENUM_resultcode_implementation_failure;
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	} else {
		/*  Call drvr_readpda(), it handles the auxport enable
		 *  and validating the returned PDA.
		 */
		result = hfa384x_drvr_readpda(hw,
					      msg->pda.data,
					      HFA384x_PDA_LEN_MAX);
		if (result) {
			printk(KERN_ERR
			       "hfa384x_drvr_readpda() failed, "
			       "result=%d\n", result);

			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
			msg->resultcode.status =
			    P80211ENUM_msgitem_status_data_ok;
			return 0;
		}
		msg->pda.status = P80211ENUM_msgitem_status_data_ok;
		msg->resultcode.data = P80211ENUM_resultcode_success;
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	}

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
	hfa384x_t *hw = wlandev->priv;
	p80211msg_p2req_ramdl_state_t *msg = msgp;

	if (wlandev->msdstate != WLAN_MSD_FWLOAD) {
		printk(KERN_ERR
		       "ramdl_state(): may only be called "
		       "in the fwload state.\n");
		msg->resultcode.data =
		    P80211ENUM_resultcode_implementation_failure;
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		return 0;
	}

	/*
	 ** Note: Interrupts are locked out if this is an AP and are NOT
	 ** locked out if this is a station.
	 */

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	if (msg->enable.data == P80211ENUM_truth_true) {
		if (hfa384x_drvr_ramdl_enable(hw, msg->exeaddr.data)) {
			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
		} else {
			msg->resultcode.data = P80211ENUM_resultcode_success;
		}
	} else {
		hfa384x_drvr_ramdl_disable(hw);
		msg->resultcode.data = P80211ENUM_resultcode_success;
	}

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
	hfa384x_t *hw = wlandev->priv;
	p80211msg_p2req_ramdl_write_t *msg = msgp;
	u32 addr;
	u32 len;
	u8 *buf;

	if (wlandev->msdstate != WLAN_MSD_FWLOAD) {
		printk(KERN_ERR
		       "ramdl_write(): may only be called "
		       "in the fwload state.\n");
		msg->resultcode.data =
		    P80211ENUM_resultcode_implementation_failure;
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		return 0;
	}

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	/* first validate the length */
	if (msg->len.data > sizeof(msg->data.data)) {
		msg->resultcode.status =
		    P80211ENUM_resultcode_invalid_parameters;
		return 0;
	}
	/* call the hfa384x function to do the write */
	addr = msg->addr.data;
	len = msg->len.data;
	buf = msg->data.data;
	if (hfa384x_drvr_ramdl_write(hw, addr, buf, len))
		msg->resultcode.data = P80211ENUM_resultcode_refused;

	msg->resultcode.data = P80211ENUM_resultcode_success;

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
	int result = 0;
	hfa384x_t *hw = wlandev->priv;
	p80211msg_p2req_flashdl_state_t *msg = msgp;

	if (wlandev->msdstate != WLAN_MSD_FWLOAD) {
		printk(KERN_ERR
		       "flashdl_state(): may only be called "
		       "in the fwload state.\n");
		msg->resultcode.data =
		    P80211ENUM_resultcode_implementation_failure;
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		return 0;
	}

	/*
	 ** Note: Interrupts are locked out if this is an AP and are NOT
	 ** locked out if this is a station.
	 */

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	if (msg->enable.data == P80211ENUM_truth_true) {
		if (hfa384x_drvr_flashdl_enable(hw)) {
			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
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
			printk(KERN_ERR "prism2sta_ifstate(fwload) failed,"
			       "P80211ENUM_resultcode=%d\n", result);
			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
			result = -1;
		}
	}

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
	hfa384x_t *hw = wlandev->priv;
	p80211msg_p2req_flashdl_write_t *msg = msgp;
	u32 addr;
	u32 len;
	u8 *buf;

	if (wlandev->msdstate != WLAN_MSD_FWLOAD) {
		printk(KERN_ERR
		       "flashdl_write(): may only be called "
		       "in the fwload state.\n");
		msg->resultcode.data =
		    P80211ENUM_resultcode_implementation_failure;
		msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
		return 0;
	}

	/*
	 ** Note: Interrupts are locked out if this is an AP and are NOT
	 ** locked out if this is a station.
	 */

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	/* first validate the length */
	if (msg->len.data > sizeof(msg->data.data)) {
		msg->resultcode.status =
		    P80211ENUM_resultcode_invalid_parameters;
		return 0;
	}
	/* call the hfa384x function to do the write */
	addr = msg->addr.data;
	len = msg->len.data;
	buf = msg->data.data;
	if (hfa384x_drvr_flashdl_write(hw, addr, buf, len))
		msg->resultcode.data = P80211ENUM_resultcode_refused;

	msg->resultcode.data = P80211ENUM_resultcode_success;

	return 0;
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
	hfa384x_t *hw = wlandev->priv;
	int result = 0;
	u16 reg;
	u16 port_type;
	p80211msg_lnxreq_autojoin_t *msg = msgp;
	p80211pstrd_t *pstr;
	u8 bytebuf[256];
	hfa384x_bytestr_t *p2bytestr = (hfa384x_bytestr_t *) bytebuf;

	wlandev->macmode = WLAN_MACMODE_NONE;

	/* Set the SSID */
	memcpy(&wlandev->ssid, &msg->ssid.data, sizeof(msg->ssid.data));

	/* Disable the Port */
	hfa384x_drvr_disable(hw, 0);

	/*** STATION ***/
	/* Set the TxRates */
	hfa384x_drvr_setconfig16(hw, HFA384x_RID_TXRATECNTL, 0x000f);

	/* Set the auth type */
	if (msg->authtype.data == P80211ENUM_authalg_sharedkey)
		reg = HFA384x_CNFAUTHENTICATION_SHAREDKEY;
	else
		reg = HFA384x_CNFAUTHENTICATION_OPENSYSTEM;

	hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFAUTHENTICATION, reg);

	/* Set the ssid */
	memset(bytebuf, 0, 256);
	pstr = (p80211pstrd_t *) &(msg->ssid.data);
	prism2mgmt_pstr2bytestr(p2bytestr, pstr);
	result = hfa384x_drvr_setconfig(hw, HFA384x_RID_CNFDESIREDSSID,
					bytebuf,
					HFA384x_RID_CNFDESIREDSSID_LEN);
	port_type = HFA384x_PORTTYPE_BSS;
	/* Set the PortType */
	hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFPORTTYPE, port_type);

	/* Enable the Port */
	hfa384x_drvr_enable(hw, 0);

	/* Set the resultcode */
	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	msg->resultcode.data = P80211ENUM_resultcode_success;

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
	int result = 0;
	p80211msg_lnxreq_wlansniff_t *msg = msgp;

	hfa384x_t *hw = wlandev->priv;
	u16 word;

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	switch (msg->enable.data) {
	case P80211ENUM_truth_false:
		/* Confirm that we're in monitor mode */
		if (wlandev->netdev->type == ARPHRD_ETHER) {
			msg->resultcode.data =
			    P80211ENUM_resultcode_invalid_parameters;
			result = 0;
			goto exit;
		}
		/* Disable monitor mode */
		result = hfa384x_cmd_monitor(hw, HFA384x_MONITOR_DISABLE);
		if (result) {
			pr_debug("failed to disable monitor mode, result=%d\n",
				 result);
			goto failed;
		}
		/* Disable port 0 */
		result = hfa384x_drvr_disable(hw, 0);
		if (result) {
			pr_debug
			    ("failed to disable port 0 after sniffing, result=%d\n",
			     result);
			goto failed;
		}
		/* Clear the driver state */
		wlandev->netdev->type = ARPHRD_ETHER;

		/* Restore the wepflags */
		result = hfa384x_drvr_setconfig16(hw,
						  HFA384x_RID_CNFWEPFLAGS,
						  hw->presniff_wepflags);
		if (result) {
			pr_debug
			    ("failed to restore wepflags=0x%04x, result=%d\n",
			     hw->presniff_wepflags, result);
			goto failed;
		}

		/* Set the port to its prior type and enable (if necessary) */
		if (hw->presniff_port_type != 0) {
			word = hw->presniff_port_type;
			result = hfa384x_drvr_setconfig16(hw,
						  HFA384x_RID_CNFPORTTYPE,
						  word);
			if (result) {
				pr_debug
				    ("failed to restore porttype, result=%d\n",
				     result);
				goto failed;
			}

			/* Enable the port */
			result = hfa384x_drvr_enable(hw, 0);
			if (result) {
				pr_debug
				    ("failed to enable port to presniff setting, result=%d\n",
				     result);
				goto failed;
			}
		} else {
			result = hfa384x_drvr_disable(hw, 0);

		}

		printk(KERN_INFO "monitor mode disabled\n");
		msg->resultcode.data = P80211ENUM_resultcode_success;
		result = 0;
		goto exit;
		break;
	case P80211ENUM_truth_true:
		/* Disable the port (if enabled), only check Port 0 */
		if (hw->port_enabled[0]) {
			if (wlandev->netdev->type == ARPHRD_ETHER) {
				/* Save macport 0 state */
				result = hfa384x_drvr_getconfig16(hw,
						  HFA384x_RID_CNFPORTTYPE,
						  &(hw->presniff_port_type));
				if (result) {
					pr_debug
					    ("failed to read porttype, result=%d\n",
					     result);
					goto failed;
				}
				/* Save the wepflags state */
				result = hfa384x_drvr_getconfig16(hw,
						  HFA384x_RID_CNFWEPFLAGS,
						  &(hw->presniff_wepflags));
				if (result) {
					pr_debug
					    ("failed to read wepflags, result=%d\n",
					     result);
					goto failed;
				}
				hfa384x_drvr_stop(hw);
				result = hfa384x_drvr_start(hw);
				if (result) {
					pr_debug
					    ("failed to restart the card for sniffing, result=%d\n",
					     result);
					goto failed;
				}
			} else {
				/* Disable the port */
				result = hfa384x_drvr_disable(hw, 0);
				if (result) {
					pr_debug
					    ("failed to enable port for sniffing, result=%d\n",
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
						  HFA384x_RID_CNFOWNCHANNEL,
						  word);
		hw->sniff_channel = word;

		if (result) {
			pr_debug("failed to set channel %d, result=%d\n",
				 word, result);
			goto failed;
		}

		/* Now if we're already sniffing, we can skip the rest */
		if (wlandev->netdev->type != ARPHRD_ETHER) {
			/* Set the port type to pIbss */
			word = HFA384x_PORTTYPE_PSUEDOIBSS;
			result = hfa384x_drvr_setconfig16(hw,
						  HFA384x_RID_CNFPORTTYPE,
						  word);
			if (result) {
				pr_debug
				    ("failed to set porttype %d, result=%d\n",
				     word, result);
				goto failed;
			}
			if ((msg->keepwepflags.status ==
			     P80211ENUM_msgitem_status_data_ok)
			    && (msg->keepwepflags.data !=
				P80211ENUM_truth_true)) {
				/* Set the wepflags for no decryption */
				word = HFA384x_WEPFLAGS_DISABLE_TXCRYPT |
				    HFA384x_WEPFLAGS_DISABLE_RXCRYPT;
				result =
				    hfa384x_drvr_setconfig16(hw,
						     HFA384x_RID_CNFWEPFLAGS,
						     word);
			}

			if (result) {
				pr_debug
				    ("failed to set wepflags=0x%04x, result=%d\n",
				     word, result);
				goto failed;
			}
		}

		/* Do we want to strip the FCS in monitor mode? */
		if ((msg->stripfcs.status == P80211ENUM_msgitem_status_data_ok)
		    && (msg->stripfcs.data == P80211ENUM_truth_true)) {
			hw->sniff_fcs = 0;
		} else {
			hw->sniff_fcs = 1;
		}

		/* Do we want to truncate the packets? */
		if (msg->packet_trunc.status ==
		    P80211ENUM_msgitem_status_data_ok) {
			hw->sniff_truncate = msg->packet_trunc.data;
		} else {
			hw->sniff_truncate = 0;
		}

		/* Enable the port */
		result = hfa384x_drvr_enable(hw, 0);
		if (result) {
			pr_debug
			    ("failed to enable port for sniffing, result=%d\n",
			     result);
			goto failed;
		}
		/* Enable monitor mode */
		result = hfa384x_cmd_monitor(hw, HFA384x_MONITOR_ENABLE);
		if (result) {
			pr_debug("failed to enable monitor mode, result=%d\n",
				 result);
			goto failed;
		}

		if (wlandev->netdev->type == ARPHRD_ETHER)
			printk(KERN_INFO "monitor mode enabled\n");

		/* Set the driver state */
		/* Do we want the prism2 header? */
		if ((msg->prismheader.status ==
		     P80211ENUM_msgitem_status_data_ok)
		    && (msg->prismheader.data == P80211ENUM_truth_true)) {
			hw->sniffhdr = 0;
			wlandev->netdev->type = ARPHRD_IEEE80211_PRISM;
		} else
		    if ((msg->wlanheader.status ==
			 P80211ENUM_msgitem_status_data_ok)
			&& (msg->wlanheader.data == P80211ENUM_truth_true)) {
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
	return result;
}
