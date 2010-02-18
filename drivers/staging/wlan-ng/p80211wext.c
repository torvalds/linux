/* src/p80211/p80211wext.c
*
* Glue code to make linux-wlan-ng a happy wireless extension camper.
*
* original author:  Reyk Floeter <reyk@synack.de>
* Completely re-written by Solomon Peachy <solomon@linux-wlan.com>
*
* Copyright (C) 2002 AbsoluteValue Systems, Inc.  All Rights Reserved.
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
*/

/*================================================================*/
/* System Includes */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <linux/if_arp.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <asm/byteorder.h>
#include <linux/if_ether.h>
#include <linux/bitops.h>

#include "p80211types.h"
#include "p80211hdr.h"
#include "p80211conv.h"
#include "p80211mgmt.h"
#include "p80211msg.h"
#include "p80211metastruct.h"
#include "p80211metadef.h"
#include "p80211netdev.h"
#include "p80211ioctl.h"
#include "p80211req.h"

static int p80211wext_giwrate(netdevice_t *dev,
			      struct iw_request_info *info,
			      struct iw_param *rrq, char *extra);
static int p80211wext_giwessid(netdevice_t *dev,
			       struct iw_request_info *info,
			       struct iw_point *data, char *essid);

static u8 p80211_mhz_to_channel(u16 mhz)
{
	if (mhz >= 5000)
		return (mhz - 5000) / 5;

	if (mhz == 2482)
		return 14;

	if (mhz >= 2407)
		return (mhz - 2407) / 5;

	return 0;
}

static u16 p80211_channel_to_mhz(u8 ch, int dot11a)
{

	if (ch == 0)
		return 0;
	if (ch > 200)
		return 0;

	/* 5G */
	if (dot11a)
		return 5000 + (5 * ch);

	/* 2.4G */
	if (ch == 14)
		return 2484;

	if ((ch < 14) && (ch > 0))
		return 2407 + (5 * ch);

	return 0;
}

/* taken from orinoco.c ;-) */
static const long p80211wext_channel_freq[] = {
	2412, 2417, 2422, 2427, 2432, 2437, 2442,
	2447, 2452, 2457, 2462, 2467, 2472, 2484
};

#define NUM_CHANNELS ARRAY_SIZE(p80211wext_channel_freq)

/* steal a spare bit to store the shared/opensystems state.
   should default to open if not set */
#define HOSTWEP_SHAREDKEY BIT(3)

static int qual_as_percent(int snr)
{
	if (snr <= 0)
		return 0;
	if (snr <= 40)
		return snr * 5 / 2;
	return 100;
}

static int p80211wext_dorequest(wlandevice_t *wlandev, u32 did, u32 data)
{
	p80211msg_dot11req_mibset_t msg;
	p80211item_uint32_t mibitem;
	int result;

	msg.msgcode = DIDmsg_dot11req_mibset;
	mibitem.did = did;
	mibitem.data = data;
	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	return result;
}

static int p80211wext_autojoin(wlandevice_t *wlandev)
{
	p80211msg_lnxreq_autojoin_t msg;
	struct iw_point data;
	char ssid[IW_ESSID_MAX_SIZE];

	int result;
	int err = 0;

	/* Get ESSID */
	result = p80211wext_giwessid(wlandev->netdev, NULL, &data, ssid);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

	if (wlandev->hostwep & HOSTWEP_SHAREDKEY)
		msg.authtype.data = P80211ENUM_authalg_sharedkey;
	else
		msg.authtype.data = P80211ENUM_authalg_opensystem;

	msg.msgcode = DIDmsg_lnxreq_autojoin;

	/* Trim the last '\0' to fit the SSID format */

	if (data.length && ssid[data.length - 1] == '\0')
		data.length = data.length - 1;

	memcpy(msg.ssid.data.data, ssid, data.length);
	msg.ssid.data.len = data.length;

	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

exit:

	return err;

}

/* called by /proc/net/wireless */
struct iw_statistics *p80211wext_get_wireless_stats(netdevice_t *dev)
{
	p80211msg_lnxreq_commsquality_t quality;
	wlandevice_t *wlandev = dev->ml_priv;
	struct iw_statistics *wstats = &wlandev->wstats;
	int retval;

	/* Check */
	if ((wlandev == NULL) || (wlandev->msdstate != WLAN_MSD_RUNNING))
		return NULL;

	/* XXX Only valid in station mode */
	wstats->status = 0;

	/* build request message */
	quality.msgcode = DIDmsg_lnxreq_commsquality;
	quality.dbm.data = P80211ENUM_truth_true;
	quality.dbm.status = P80211ENUM_msgitem_status_data_ok;

	/* send message to nsd */
	if (wlandev->mlmerequest == NULL)
		return NULL;

	retval = wlandev->mlmerequest(wlandev, (p80211msg_t *) &quality);

	wstats->qual.qual = qual_as_percent(quality.link.data);	/* overall link quality */
	wstats->qual.level = quality.level.data;	/* instant signal level */
	wstats->qual.noise = quality.noise.data;	/* instant noise level */

	wstats->qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
	wstats->discard.code = wlandev->rx.decrypt_err;
	wstats->discard.nwid = 0;
	wstats->discard.misc = 0;

	wstats->discard.fragment = 0;	/* incomplete fragments */
	wstats->discard.retries = 0;	/* tx retries. */
	wstats->miss.beacon = 0;

	return wstats;
}

static int p80211wext_giwname(netdevice_t *dev,
			      struct iw_request_info *info,
			      char *name, char *extra)
{
	struct iw_param rate;
	int result;
	int err = 0;

	result = p80211wext_giwrate(dev, NULL, &rate, NULL);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

	switch (rate.value) {
	case 1000000:
	case 2000000:
		strcpy(name, "IEEE 802.11-DS");
		break;
	case 5500000:
	case 11000000:
		strcpy(name, "IEEE 802.11-b");
		break;
	}
exit:
	return err;
}

static int p80211wext_giwfreq(netdevice_t *dev,
			      struct iw_request_info *info,
			      struct iw_freq *freq, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211item_uint32_t mibitem;
	p80211msg_dot11req_mibset_t msg;
	int result;
	int err = 0;

	msg.msgcode = DIDmsg_dot11req_mibget;
	mibitem.did = DIDmib_dot11phy_dot11PhyDSSSTable_dot11CurrentChannel;
	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

	memcpy(&mibitem, &msg.mibattribute.data, sizeof(mibitem));

	if (mibitem.data > NUM_CHANNELS) {
		err = -EFAULT;
		goto exit;
	}

	/* convert into frequency instead of a channel */
	freq->e = 1;
	freq->m = p80211_channel_to_mhz(mibitem.data, 0) * 100000;

exit:
	return err;
}

static int p80211wext_siwfreq(netdevice_t *dev,
			      struct iw_request_info *info,
			      struct iw_freq *freq, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211item_uint32_t mibitem;
	p80211msg_dot11req_mibset_t msg;
	int result;
	int err = 0;

	if (!wlan_wext_write) {
		err = (-EOPNOTSUPP);
		goto exit;
	}

	msg.msgcode = DIDmsg_dot11req_mibset;
	mibitem.did = DIDmib_dot11phy_dot11PhyDSSSTable_dot11CurrentChannel;
	mibitem.status = P80211ENUM_msgitem_status_data_ok;

	if ((freq->e == 0) && (freq->m <= 1000))
		mibitem.data = freq->m;
	else
		mibitem.data = p80211_mhz_to_channel(freq->m);

	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

exit:
	return err;
}

static int p80211wext_giwmode(netdevice_t *dev,
			      struct iw_request_info *info,
			      __u32 *mode, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;

	switch (wlandev->macmode) {
	case WLAN_MACMODE_IBSS_STA:
		*mode = IW_MODE_ADHOC;
		break;
	case WLAN_MACMODE_ESS_STA:
		*mode = IW_MODE_INFRA;
		break;
	case WLAN_MACMODE_ESS_AP:
		*mode = IW_MODE_MASTER;
		break;
	default:
		/* Not set yet. */
		*mode = IW_MODE_AUTO;
	}

	return 0;
}

static int p80211wext_siwmode(netdevice_t *dev,
			      struct iw_request_info *info,
			      __u32 *mode, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211item_uint32_t mibitem;
	p80211msg_dot11req_mibset_t msg;
	int result;
	int err = 0;

	if (!wlan_wext_write) {
		err = (-EOPNOTSUPP);
		goto exit;
	}

	if (*mode != IW_MODE_ADHOC && *mode != IW_MODE_INFRA &&
	    *mode != IW_MODE_MASTER) {
		err = (-EOPNOTSUPP);
		goto exit;
	}

	/* Operation mode is the same with current mode */
	if (*mode == wlandev->macmode)
		goto exit;

	switch (*mode) {
	case IW_MODE_ADHOC:
		wlandev->macmode = WLAN_MACMODE_IBSS_STA;
		break;
	case IW_MODE_INFRA:
		wlandev->macmode = WLAN_MACMODE_ESS_STA;
		break;
	case IW_MODE_MASTER:
		wlandev->macmode = WLAN_MACMODE_ESS_AP;
		break;
	default:
		/* Not set yet. */
		printk(KERN_INFO "Operation mode: %d not support\n", *mode);
		return -EOPNOTSUPP;
	}

	/* Set Operation mode to the PORT TYPE RID */
	msg.msgcode = DIDmsg_dot11req_mibset;
	mibitem.did = DIDmib_p2_p2Static_p2CnfPortType;
	mibitem.data = (*mode == IW_MODE_ADHOC) ? 0 : 1;
	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result)
		err = -EFAULT;

exit:
	return err;
}

static int p80211wext_giwrange(netdevice_t *dev,
			       struct iw_request_info *info,
			       struct iw_point *data, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;
	int i, val;

	/* for backward compatability set size and zero everything we don't understand */
	data->length = sizeof(*range);
	memset(range, 0, sizeof(*range));

	range->txpower_capa = IW_TXPOW_DBM;
	/* XXX what about min/max_pmp, min/max_pmt, etc. */

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 13;

	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->min_retry = 0;
	range->max_retry = 255;

	range->event_capa[0] = (IW_EVENT_CAPA_K_0 |	/* mode/freq/ssid */
				IW_EVENT_CAPA_MASK(SIOCGIWAP) |
				IW_EVENT_CAPA_MASK(SIOCGIWSCAN));
	range->event_capa[1] = IW_EVENT_CAPA_K_1;	/* encode */
	range->event_capa[4] = (IW_EVENT_CAPA_MASK(IWEVQUAL) |
				IW_EVENT_CAPA_MASK(IWEVCUSTOM));

	range->num_channels = NUM_CHANNELS;

	/* XXX need to filter against the regulatory domain &| active set */
	val = 0;
	for (i = 0; i < NUM_CHANNELS; i++) {
		range->freq[val].i = i + 1;
		range->freq[val].m = p80211wext_channel_freq[i] * 100000;
		range->freq[val].e = 1;
		val++;
	}

	range->num_frequency = val;

	/* Max of /proc/net/wireless */
	range->max_qual.qual = 100;
	range->max_qual.level = 0;
	range->max_qual.noise = 0;
	range->sensitivity = 3;
	/* XXX these need to be nsd-specific! */

	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	range->max_encoding_tokens = NUM_WEPKEYS;
	range->num_encoding_sizes = 2;
	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;

	/* XXX what about num_bitrates/throughput? */
	range->num_bitrates = 0;

	/* estimated max throughput */
	/* XXX need to cap it if we're running at ~2Mbps.. */
	range->throughput = 5500000;

	return 0;
}

static int p80211wext_giwap(netdevice_t *dev,
			    struct iw_request_info *info,
			    struct sockaddr *ap_addr, char *extra)
{

	wlandevice_t *wlandev = dev->ml_priv;

	memcpy(ap_addr->sa_data, wlandev->bssid, WLAN_BSSID_LEN);
	ap_addr->sa_family = ARPHRD_ETHER;

	return 0;
}

static int p80211wext_giwencode(netdevice_t *dev,
				struct iw_request_info *info,
				struct iw_point *erq, char *key)
{
	wlandevice_t *wlandev = dev->ml_priv;
	int err = 0;
	int i;

	i = (erq->flags & IW_ENCODE_INDEX) - 1;
	erq->flags = 0;

	if (wlandev->hostwep & HOSTWEP_PRIVACYINVOKED)
		erq->flags |= IW_ENCODE_ENABLED;
	else
		erq->flags |= IW_ENCODE_DISABLED;

	if (wlandev->hostwep & HOSTWEP_EXCLUDEUNENCRYPTED)
		erq->flags |= IW_ENCODE_RESTRICTED;
	else
		erq->flags |= IW_ENCODE_OPEN;

	i = (erq->flags & IW_ENCODE_INDEX) - 1;

	if (i == -1)
		i = wlandev->hostwep & HOSTWEP_DEFAULTKEY_MASK;

	if ((i < 0) || (i >= NUM_WEPKEYS)) {
		err = -EINVAL;
		goto exit;
	}

	erq->flags |= i + 1;

	/* copy the key from the driver cache as the keys are read-only MIBs */
	erq->length = wlandev->wep_keylens[i];
	memcpy(key, wlandev->wep_keys[i], erq->length);

exit:
	return err;
}

static int p80211wext_siwencode(netdevice_t *dev,
				struct iw_request_info *info,
				struct iw_point *erq, char *key)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211msg_dot11req_mibset_t msg;
	p80211item_pstr32_t pstr;

	int err = 0;
	int result = 0;
	int i;

	if (!wlan_wext_write) {
		err = (-EOPNOTSUPP);
		goto exit;
	}

	/* Check the Key index first. */
	if ((i = (erq->flags & IW_ENCODE_INDEX))) {

		if ((i < 1) || (i > NUM_WEPKEYS)) {
			err = -EINVAL;
			goto exit;
		} else
			i--;

		/* Set current key number only if no keys are given */
		if (erq->flags & IW_ENCODE_NOKEY) {
			result =
			    p80211wext_dorequest(wlandev,
						 DIDmib_dot11smt_dot11PrivacyTable_dot11WEPDefaultKeyID,
						 i);

			if (result) {
				err = -EFAULT;
				goto exit;
			}
		}

	} else {
		/* Use defaultkey if no Key Index */
		i = wlandev->hostwep & HOSTWEP_DEFAULTKEY_MASK;
	}

	/* Check if there is no key information in the iwconfig request */
	if ((erq->flags & IW_ENCODE_NOKEY) == 0) {

		/*------------------------------------------------------------
		 * If there is WEP Key for setting, check the Key Information
		 * and then set it to the firmware.
		 -------------------------------------------------------------*/

		if (erq->length > 0) {

			/* copy the key from the driver cache as the keys are read-only MIBs */
			wlandev->wep_keylens[i] = erq->length;
			memcpy(wlandev->wep_keys[i], key, erq->length);

			/* Prepare data struture for p80211req_dorequest. */
			memcpy(pstr.data.data, key, erq->length);
			pstr.data.len = erq->length;

			switch (i) {
			case 0:
				pstr.did =
				    DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey0;
				break;

			case 1:
				pstr.did =
				    DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey1;
				break;

			case 2:
				pstr.did =
				    DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey2;
				break;

			case 3:
				pstr.did =
				    DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey3;
				break;

			default:
				err = -EINVAL;
				goto exit;
			}

			msg.msgcode = DIDmsg_dot11req_mibset;
			memcpy(&msg.mibattribute.data, &pstr, sizeof(pstr));
			result = p80211req_dorequest(wlandev, (u8 *) &msg);

			if (result) {
				err = -EFAULT;
				goto exit;
			}
		}

	}

	/* Check the PrivacyInvoked flag */
	if (erq->flags & IW_ENCODE_DISABLED) {
		result =
		    p80211wext_dorequest(wlandev,
					 DIDmib_dot11smt_dot11PrivacyTable_dot11PrivacyInvoked,
					 P80211ENUM_truth_false);
	} else {
		result =
		    p80211wext_dorequest(wlandev,
					 DIDmib_dot11smt_dot11PrivacyTable_dot11PrivacyInvoked,
					 P80211ENUM_truth_true);
	}

	if (result) {
		err = -EFAULT;
		goto exit;
	}

	/*  The  security  mode  may  be open or restricted, and its meaning
	   depends on the card used. With  most  cards,  in  open  mode  no
	   authentication  is  used  and  the  card  may  also  accept non-
	   encrypted sessions, whereas in restricted  mode  only  encrypted
	   sessions  are  accepted  and the card will use authentication if
	   available.
	 */
	if (erq->flags & IW_ENCODE_RESTRICTED) {
		result =
		    p80211wext_dorequest(wlandev,
					 DIDmib_dot11smt_dot11PrivacyTable_dot11ExcludeUnencrypted,
					 P80211ENUM_truth_true);
	} else if (erq->flags & IW_ENCODE_OPEN) {
		result =
		    p80211wext_dorequest(wlandev,
					 DIDmib_dot11smt_dot11PrivacyTable_dot11ExcludeUnencrypted,
					 P80211ENUM_truth_false);
	}

	if (result) {
		err = -EFAULT;
		goto exit;
	}

exit:

	return err;
}

static int p80211wext_giwessid(netdevice_t *dev,
			       struct iw_request_info *info,
			       struct iw_point *data, char *essid)
{
	wlandevice_t *wlandev = dev->ml_priv;

	if (wlandev->ssid.len) {
		data->length = wlandev->ssid.len;
		data->flags = 1;
		memcpy(essid, wlandev->ssid.data, data->length);
		essid[data->length] = 0;
	} else {
		memset(essid, 0, sizeof(wlandev->ssid.data));
		data->length = 0;
		data->flags = 0;
	}

	return 0;
}

static int p80211wext_siwessid(netdevice_t *dev,
			       struct iw_request_info *info,
			       struct iw_point *data, char *essid)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211msg_lnxreq_autojoin_t msg;

	int result;
	int err = 0;
	int length = data->length;

	if (!wlan_wext_write) {
		err = (-EOPNOTSUPP);
		goto exit;
	}

	if (wlandev->hostwep & HOSTWEP_SHAREDKEY)
		msg.authtype.data = P80211ENUM_authalg_sharedkey;
	else
		msg.authtype.data = P80211ENUM_authalg_opensystem;

	msg.msgcode = DIDmsg_lnxreq_autojoin;

	/* Trim the last '\0' to fit the SSID format */
	if (length && essid[length - 1] == '\0')
		length--;

	memcpy(msg.ssid.data.data, essid, length);
	msg.ssid.data.len = length;

	pr_debug("autojoin_ssid for %s \n", essid);
	result = p80211req_dorequest(wlandev, (u8 *) &msg);
	pr_debug("autojoin_ssid %d\n", result);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

exit:
	return err;
}

static int p80211wext_siwcommit(netdevice_t *dev,
				struct iw_request_info *info,
				struct iw_point *data, char *essid)
{
	wlandevice_t *wlandev = dev->ml_priv;
	int err = 0;

	if (!wlan_wext_write) {
		err = (-EOPNOTSUPP);
		goto exit;
	}

	/* Auto Join */
	err = p80211wext_autojoin(wlandev);

exit:
	return err;
}

static int p80211wext_giwrate(netdevice_t *dev,
			      struct iw_request_info *info,
			      struct iw_param *rrq, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211item_uint32_t mibitem;
	p80211msg_dot11req_mibset_t msg;
	int result;
	int err = 0;

	msg.msgcode = DIDmsg_dot11req_mibget;
	mibitem.did = DIDmib_p2_p2MAC_p2CurrentTxRate;
	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

	memcpy(&mibitem, &msg.mibattribute.data, sizeof(mibitem));

	rrq->fixed = 0;		/* can it change? */
	rrq->disabled = 0;
	rrq->value = 0;

#define		HFA384x_RATEBIT_1			((u16)1)
#define		HFA384x_RATEBIT_2			((u16)2)
#define		HFA384x_RATEBIT_5dot5			((u16)4)
#define		HFA384x_RATEBIT_11			((u16)8)

	switch (mibitem.data) {
	case HFA384x_RATEBIT_1:
		rrq->value = 1000000;
		break;
	case HFA384x_RATEBIT_2:
		rrq->value = 2000000;
		break;
	case HFA384x_RATEBIT_5dot5:
		rrq->value = 5500000;
		break;
	case HFA384x_RATEBIT_11:
		rrq->value = 11000000;
		break;
	default:
		err = -EINVAL;
	}
exit:
	return err;
}

static int p80211wext_giwrts(netdevice_t *dev,
			     struct iw_request_info *info,
			     struct iw_param *rts, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211item_uint32_t mibitem;
	p80211msg_dot11req_mibset_t msg;
	int result;
	int err = 0;

	msg.msgcode = DIDmsg_dot11req_mibget;
	mibitem.did = DIDmib_dot11mac_dot11OperationTable_dot11RTSThreshold;
	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

	memcpy(&mibitem, &msg.mibattribute.data, sizeof(mibitem));

	rts->value = mibitem.data;
	rts->disabled = (rts->value == 2347);
	rts->fixed = 1;

exit:
	return err;
}

static int p80211wext_siwrts(netdevice_t *dev,
			     struct iw_request_info *info,
			     struct iw_param *rts, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211item_uint32_t mibitem;
	p80211msg_dot11req_mibset_t msg;
	int result;
	int err = 0;

	if (!wlan_wext_write) {
		err = (-EOPNOTSUPP);
		goto exit;
	}

	msg.msgcode = DIDmsg_dot11req_mibget;
	mibitem.did = DIDmib_dot11mac_dot11OperationTable_dot11RTSThreshold;
	if (rts->disabled)
		mibitem.data = 2347;
	else
		mibitem.data = rts->value;

	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

exit:
	return err;
}

static int p80211wext_giwfrag(netdevice_t *dev,
			      struct iw_request_info *info,
			      struct iw_param *frag, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211item_uint32_t mibitem;
	p80211msg_dot11req_mibset_t msg;
	int result;
	int err = 0;

	msg.msgcode = DIDmsg_dot11req_mibget;
	mibitem.did =
	    DIDmib_dot11mac_dot11OperationTable_dot11FragmentationThreshold;
	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

	memcpy(&mibitem, &msg.mibattribute.data, sizeof(mibitem));

	frag->value = mibitem.data;
	frag->disabled = (frag->value == 2346);
	frag->fixed = 1;

exit:
	return err;
}

static int p80211wext_siwfrag(netdevice_t *dev,
			      struct iw_request_info *info,
			      struct iw_param *frag, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211item_uint32_t mibitem;
	p80211msg_dot11req_mibset_t msg;
	int result;
	int err = 0;

	if (!wlan_wext_write) {
		err = (-EOPNOTSUPP);
		goto exit;
	}

	msg.msgcode = DIDmsg_dot11req_mibset;
	mibitem.did =
	    DIDmib_dot11mac_dot11OperationTable_dot11FragmentationThreshold;

	if (frag->disabled)
		mibitem.data = 2346;
	else
		mibitem.data = frag->value;

	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

exit:
	return err;
}

#ifndef IW_RETRY_LONG
#define IW_RETRY_LONG IW_RETRY_MAX
#endif

#ifndef IW_RETRY_SHORT
#define IW_RETRY_SHORT IW_RETRY_MIN
#endif

static int p80211wext_giwretry(netdevice_t *dev,
			       struct iw_request_info *info,
			       struct iw_param *rrq, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211item_uint32_t mibitem;
	p80211msg_dot11req_mibset_t msg;
	int result;
	int err = 0;
	u16 shortretry, longretry, lifetime;

	msg.msgcode = DIDmsg_dot11req_mibget;
	mibitem.did = DIDmib_dot11mac_dot11OperationTable_dot11ShortRetryLimit;

	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

	memcpy(&mibitem, &msg.mibattribute.data, sizeof(mibitem));

	shortretry = mibitem.data;

	mibitem.did = DIDmib_dot11mac_dot11OperationTable_dot11LongRetryLimit;

	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

	memcpy(&mibitem, &msg.mibattribute.data, sizeof(mibitem));

	longretry = mibitem.data;

	mibitem.did =
	    DIDmib_dot11mac_dot11OperationTable_dot11MaxTransmitMSDULifetime;

	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

	memcpy(&mibitem, &msg.mibattribute.data, sizeof(mibitem));

	lifetime = mibitem.data;

	rrq->disabled = 0;

	if ((rrq->flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME) {
		rrq->flags = IW_RETRY_LIFETIME;
		rrq->value = lifetime * 1024;
	} else {
		if (rrq->flags & IW_RETRY_LONG) {
			rrq->flags = IW_RETRY_LIMIT | IW_RETRY_LONG;
			rrq->value = longretry;
		} else {
			rrq->flags = IW_RETRY_LIMIT;
			rrq->value = shortretry;
			if (shortretry != longretry)
				rrq->flags |= IW_RETRY_SHORT;
		}
	}

exit:
	return err;

}

static int p80211wext_siwretry(netdevice_t *dev,
			       struct iw_request_info *info,
			       struct iw_param *rrq, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211item_uint32_t mibitem;
	p80211msg_dot11req_mibset_t msg;
	int result;
	int err = 0;

	if (!wlan_wext_write) {
		err = (-EOPNOTSUPP);
		goto exit;
	}

	if (rrq->disabled) {
		err = -EINVAL;
		goto exit;
	}

	msg.msgcode = DIDmsg_dot11req_mibset;

	if ((rrq->flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME) {
		mibitem.did =
		    DIDmib_dot11mac_dot11OperationTable_dot11MaxTransmitMSDULifetime;
		mibitem.data = rrq->value /= 1024;

		memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
		result = p80211req_dorequest(wlandev, (u8 *) &msg);

		if (result) {
			err = -EFAULT;
			goto exit;
		}
	} else {
		if (rrq->flags & IW_RETRY_LONG) {
			mibitem.did =
			    DIDmib_dot11mac_dot11OperationTable_dot11LongRetryLimit;
			mibitem.data = rrq->value;

			memcpy(&msg.mibattribute.data, &mibitem,
			       sizeof(mibitem));
			result = p80211req_dorequest(wlandev, (u8 *) &msg);

			if (result) {
				err = -EFAULT;
				goto exit;
			}
		}

		if (rrq->flags & IW_RETRY_SHORT) {
			mibitem.did =
			    DIDmib_dot11mac_dot11OperationTable_dot11ShortRetryLimit;
			mibitem.data = rrq->value;

			memcpy(&msg.mibattribute.data, &mibitem,
			       sizeof(mibitem));
			result = p80211req_dorequest(wlandev, (u8 *) &msg);

			if (result) {
				err = -EFAULT;
				goto exit;
			}
		}
	}

exit:
	return err;

}

static int p80211wext_siwtxpow(netdevice_t *dev,
			       struct iw_request_info *info,
			       struct iw_param *rrq, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211item_uint32_t mibitem;
	p80211msg_dot11req_mibset_t msg;
	int result;
	int err = 0;

	if (!wlan_wext_write) {
		err = (-EOPNOTSUPP);
		goto exit;
	}

	msg.msgcode = DIDmsg_dot11req_mibset;
	mibitem.did =
	    DIDmib_dot11phy_dot11PhyTxPowerTable_dot11CurrentTxPowerLevel;
	if (rrq->fixed == 0)
		mibitem.data = 30;
	else
		mibitem.data = rrq->value;
	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

exit:
	return err;
}

static int p80211wext_giwtxpow(netdevice_t *dev,
			       struct iw_request_info *info,
			       struct iw_param *rrq, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211item_uint32_t mibitem;
	p80211msg_dot11req_mibset_t msg;
	int result;
	int err = 0;

	msg.msgcode = DIDmsg_dot11req_mibget;
	mibitem.did =
	    DIDmib_dot11phy_dot11PhyTxPowerTable_dot11CurrentTxPowerLevel;

	memcpy(&msg.mibattribute.data, &mibitem, sizeof(mibitem));
	result = p80211req_dorequest(wlandev, (u8 *) &msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

	memcpy(&mibitem, &msg.mibattribute.data, sizeof(mibitem));

	/* XXX handle OFF by setting disabled = 1; */

	rrq->flags = 0;		/* IW_TXPOW_DBM; */
	rrq->disabled = 0;
	rrq->fixed = 0;
	rrq->value = mibitem.data;

exit:
	return err;
}

static int p80211wext_siwspy(netdevice_t *dev,
			     struct iw_request_info *info,
			     struct iw_point *srq, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	struct sockaddr address[IW_MAX_SPY];
	int number = srq->length;
	int i;

	/* Copy the data from the input buffer */
	memcpy(address, extra, sizeof(struct sockaddr) * number);

	wlandev->spy_number = 0;

	if (number > 0) {

		/* extract the addresses */
		for (i = 0; i < number; i++) {

			memcpy(wlandev->spy_address[i], address[i].sa_data,
			       ETH_ALEN);
		}

		/* reset stats */
		memset(wlandev->spy_stat, 0,
		       sizeof(struct iw_quality) * IW_MAX_SPY);

		/* set number of addresses */
		wlandev->spy_number = number;
	}

	return 0;
}

/* jkriegl: from orinoco, modified */
static int p80211wext_giwspy(netdevice_t *dev,
			     struct iw_request_info *info,
			     struct iw_point *srq, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;

	struct sockaddr address[IW_MAX_SPY];
	struct iw_quality spy_stat[IW_MAX_SPY];
	int number;
	int i;

	number = wlandev->spy_number;

	if (number > 0) {

		/* populate address and spy struct's */
		for (i = 0; i < number; i++) {
			memcpy(address[i].sa_data, wlandev->spy_address[i],
			       ETH_ALEN);
			address[i].sa_family = AF_UNIX;
			memcpy(&spy_stat[i], &wlandev->spy_stat[i],
			       sizeof(struct iw_quality));
		}

		/* reset update flag */
		for (i = 0; i < number; i++)
			wlandev->spy_stat[i].updated = 0;
	}

	/* push stuff to user space */
	srq->length = number;
	memcpy(extra, address, sizeof(struct sockaddr) * number);
	memcpy(extra + sizeof(struct sockaddr) * number, spy_stat,
	       sizeof(struct iw_quality) * number);

	return 0;
}

static int prism2_result2err(int prism2_result)
{
	int err = 0;

	switch (prism2_result) {
	case P80211ENUM_resultcode_invalid_parameters:
		err = -EINVAL;
		break;
	case P80211ENUM_resultcode_implementation_failure:
		err = -EIO;
		break;
	case P80211ENUM_resultcode_not_supported:
		err = -EOPNOTSUPP;
		break;
	default:
		err = 0;
		break;
	}

	return err;
}

static int p80211wext_siwscan(netdevice_t *dev,
			      struct iw_request_info *info,
			      struct iw_point *srq, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211msg_dot11req_scan_t msg;
	int result;
	int err = 0;
	int i = 0;

	if (wlandev->macmode == WLAN_MACMODE_ESS_AP) {
		printk(KERN_ERR "Can't scan in AP mode\n");
		err = (-EOPNOTSUPP);
		goto exit;
	}

	memset(&msg, 0x00, sizeof(p80211msg_dot11req_scan_t));
	msg.msgcode = DIDmsg_dot11req_scan;
	msg.bsstype.data = P80211ENUM_bsstype_any;

	memset(&(msg.bssid.data), 0xFF, sizeof(p80211item_pstr6_t));
	msg.bssid.data.len = 6;

	msg.scantype.data = P80211ENUM_scantype_active;
	msg.probedelay.data = 0;

	for (i = 1; i <= 14; i++)
		msg.channellist.data.data[i - 1] = i;
	msg.channellist.data.len = 14;

	msg.maxchanneltime.data = 250;
	msg.minchanneltime.data = 200;

	result = p80211req_dorequest(wlandev, (u8 *) &msg);
	if (result)
		err = prism2_result2err(msg.resultcode.data);

exit:
	return err;
}

/* Helper to translate scan into Wireless Extensions scan results.
 * Inspired by the prism54 code, which was in turn inspired by the
 * airo driver code.
 */
static char *wext_translate_bss(struct iw_request_info *info, char *current_ev,
				char *end_buf,
				p80211msg_dot11req_scan_results_t *bss)
{
	struct iw_event iwe;	/* Temporary buffer */

	/* The first entry must be the MAC address */
	memcpy(iwe.u.ap_addr.sa_data, bss->bssid.data.data, WLAN_BSSID_LEN);
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	iwe.cmd = SIOCGIWAP;
	current_ev =
	    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
				 IW_EV_ADDR_LEN);

	/* The following entries will be displayed in the same order we give them */

	/* The ESSID. */
	if (bss->ssid.data.len > 0) {
		char essid[IW_ESSID_MAX_SIZE + 1];
		int size;

		size =
		    min_t(unsigned short, IW_ESSID_MAX_SIZE,
			  bss->ssid.data.len);
		memset(&essid, 0, sizeof(essid));
		memcpy(&essid, bss->ssid.data.data, size);
		pr_debug(" essid size = %d\n", size);
		iwe.u.data.length = size;
		iwe.u.data.flags = 1;
		iwe.cmd = SIOCGIWESSID;
		current_ev =
		    iwe_stream_add_point(info, current_ev, end_buf, &iwe,
					 &essid[0]);
		pr_debug(" essid size OK.\n");
	}

	switch (bss->bsstype.data) {
	case P80211ENUM_bsstype_infrastructure:
		iwe.u.mode = IW_MODE_MASTER;
		break;

	case P80211ENUM_bsstype_independent:
		iwe.u.mode = IW_MODE_ADHOC;
		break;

	default:
		iwe.u.mode = 0;
		break;
	}
	iwe.cmd = SIOCGIWMODE;
	if (iwe.u.mode)
		current_ev =
		    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					 IW_EV_UINT_LEN);

	/* Encryption capability */
	if (bss->privacy.data == P80211ENUM_truth_true)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	iwe.cmd = SIOCGIWENCODE;
	current_ev =
	    iwe_stream_add_point(info, current_ev, end_buf, &iwe, NULL);

	/* Add frequency. (short) bss->channel is the frequency in MHz */
	iwe.u.freq.m = bss->dschannel.data;
	iwe.u.freq.e = 0;
	iwe.cmd = SIOCGIWFREQ;
	current_ev =
	    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
				 IW_EV_FREQ_LEN);

	/* Add quality statistics */
	iwe.u.qual.level = bss->signal.data;
	iwe.u.qual.noise = bss->noise.data;
	/* do a simple SNR for quality */
	iwe.u.qual.qual = qual_as_percent(bss->signal.data - bss->noise.data);
	iwe.cmd = IWEVQUAL;
	current_ev =
	    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
				 IW_EV_QUAL_LEN);

	return current_ev;
}

static int p80211wext_giwscan(netdevice_t *dev,
			      struct iw_request_info *info,
			      struct iw_point *srq, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	p80211msg_dot11req_scan_results_t msg;
	int result = 0;
	int err = 0;
	int i = 0;
	int scan_good = 0;
	char *current_ev = extra;

	/* Since wireless tools doesn't really have a way of passing how
	 * many scan results results there were back here, keep grabbing them
	 * until we fail.
	 */
	do {
		memset(&msg, 0, sizeof(msg));
		msg.msgcode = DIDmsg_dot11req_scan_results;
		msg.bssindex.data = i;

		result = p80211req_dorequest(wlandev, (u8 *) &msg);
		if ((result != 0) ||
		    (msg.resultcode.data != P80211ENUM_resultcode_success)) {
			break;
		}

		current_ev =
		    wext_translate_bss(info, current_ev,
				       extra + IW_SCAN_MAX_DATA, &msg);
		scan_good = 1;
		i++;
	} while (i < IW_MAX_AP);

	srq->length = (current_ev - extra);
	srq->flags = 0;		/* todo */

	if (result && !scan_good)
		err = prism2_result2err(msg.resultcode.data);

	return err;
}

/* extra wireless extensions stuff to support NetworkManager (I hope) */

/* SIOCSIWENCODEEXT */
static int p80211wext_set_encodeext(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	p80211msg_dot11req_mibset_t msg;
	p80211item_pstr32_t *pstr;

	int result = 0;
	struct iw_point *encoding = &wrqu->encoding;
	int idx = encoding->flags & IW_ENCODE_INDEX;

	pr_debug("set_encode_ext flags[%d] alg[%d] keylen[%d]\n",
		 ext->ext_flags, (int)ext->alg, (int)ext->key_len);

	if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
		/* set default key ? I'm not sure if this the the correct thing to do here */

		if (idx) {
			if (idx < 1 || idx > NUM_WEPKEYS)
				return -EINVAL;
			else
				idx--;
		}
		pr_debug("setting default key (%d)\n", idx);
		result =
		    p80211wext_dorequest(wlandev,
					 DIDmib_dot11smt_dot11PrivacyTable_dot11WEPDefaultKeyID,
					 idx);
		if (result)
			return -EFAULT;
	}

	if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
		if (ext->alg != IW_ENCODE_ALG_WEP) {
			pr_debug("asked to set a non wep key :(\n");
			return -EINVAL;
		}
		if (idx) {
			if (idx < 1 || idx > NUM_WEPKEYS)
				return -EINVAL;
			else
				idx--;
		}
		pr_debug("Set WEP key (%d)\n", idx);
		wlandev->wep_keylens[idx] = ext->key_len;
		memcpy(wlandev->wep_keys[idx], ext->key, ext->key_len);

		memset(&msg, 0, sizeof(msg));
		pstr = (p80211item_pstr32_t *) &msg.mibattribute.data;
		memcpy(pstr->data.data, ext->key, ext->key_len);
		pstr->data.len = ext->key_len;
		switch (idx) {
		case 0:
			pstr->did =
			    DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey0;
			break;
		case 1:
			pstr->did =
			    DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey1;
			break;
		case 2:
			pstr->did =
			    DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey2;
			break;
		case 3:
			pstr->did =
			    DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey3;
			break;
		default:
			break;
		}
		msg.msgcode = DIDmsg_dot11req_mibset;
		result = p80211req_dorequest(wlandev, (u8 *) &msg);
		pr_debug("result (%d)\n", result);
	}
	return result;
}

/* SIOCGIWENCODEEXT */
static int p80211wext_get_encodeext(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;

	struct iw_point *encoding = &wrqu->encoding;
	int result = 0;
	int max_len;
	int idx;

	pr_debug("get_encode_ext flags[%d] alg[%d] keylen[%d]\n",
		 ext->ext_flags, (int)ext->alg, (int)ext->key_len);

	max_len = encoding->length - sizeof(*ext);
	if (max_len <= 0) {
		pr_debug("get_encodeext max_len [%d] invalid\n", max_len);
		result = -EINVAL;
		goto exit;
	}
	idx = encoding->flags & IW_ENCODE_INDEX;

	pr_debug("get_encode_ext index [%d]\n", idx);

	if (idx) {
		if (idx < 1 || idx > NUM_WEPKEYS) {
			pr_debug("get_encode_ext invalid key index [%d]\n",
				 idx);
			result = -EINVAL;
			goto exit;
		}
		idx--;
	} else {
		/* default key ? not sure what to do */
		/* will just use key[0] for now ! FIX ME */
	}

	encoding->flags = idx + 1;
	memset(ext, 0, sizeof(*ext));

	ext->alg = IW_ENCODE_ALG_WEP;
	ext->key_len = wlandev->wep_keylens[idx];
	memcpy(ext->key, wlandev->wep_keys[idx], ext->key_len);

	encoding->flags |= IW_ENCODE_ENABLED;
exit:
	return result;
}

/* SIOCSIWAUTH */
static int p80211_wext_set_iwauth(struct net_device *dev,
				  struct iw_request_info *info,
				  union iwreq_data *wrqu, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	struct iw_param *param = &wrqu->param;
	int result = 0;

	pr_debug("set_iwauth flags[%d]\n", (int)param->flags & IW_AUTH_INDEX);

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_DROP_UNENCRYPTED:
		pr_debug("drop_unencrypted %d\n", param->value);
		if (param->value)
			result =
			    p80211wext_dorequest(wlandev,
						 DIDmib_dot11smt_dot11PrivacyTable_dot11ExcludeUnencrypted,
						 P80211ENUM_truth_true);
		else
			result =
			    p80211wext_dorequest(wlandev,
						 DIDmib_dot11smt_dot11PrivacyTable_dot11ExcludeUnencrypted,
						 P80211ENUM_truth_false);
		break;

	case IW_AUTH_PRIVACY_INVOKED:
		pr_debug("privacy invoked %d\n", param->value);
		if (param->value)
			result =
			    p80211wext_dorequest(wlandev,
						 DIDmib_dot11smt_dot11PrivacyTable_dot11PrivacyInvoked,
						 P80211ENUM_truth_true);
		else
			result =
			    p80211wext_dorequest(wlandev,
						 DIDmib_dot11smt_dot11PrivacyTable_dot11PrivacyInvoked,
						 P80211ENUM_truth_false);

		break;

	case IW_AUTH_80211_AUTH_ALG:
		if (param->value & IW_AUTH_ALG_OPEN_SYSTEM) {
			pr_debug("set open_system\n");
			wlandev->hostwep &= ~HOSTWEP_SHAREDKEY;
		} else if (param->value & IW_AUTH_ALG_SHARED_KEY) {
			pr_debug("set shared key\n");
			wlandev->hostwep |= HOSTWEP_SHAREDKEY;
		} else {
			/* don't know what to do know  */
			pr_debug("unknown AUTH_ALG (%d)\n", param->value);
			result = -EINVAL;
		}
		break;

	default:
		break;
	}

	return result;
}

/* SIOCSIWAUTH */
static int p80211_wext_get_iwauth(struct net_device *dev,
				  struct iw_request_info *info,
				  union iwreq_data *wrqu, char *extra)
{
	wlandevice_t *wlandev = dev->ml_priv;
	struct iw_param *param = &wrqu->param;
	int result = 0;

	pr_debug("get_iwauth flags[%d]\n", (int)param->flags & IW_AUTH_INDEX);

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_DROP_UNENCRYPTED:
		param->value =
		    wlandev->hostwep & HOSTWEP_EXCLUDEUNENCRYPTED ? 1 : 0;
		break;

	case IW_AUTH_PRIVACY_INVOKED:
		param->value =
		    wlandev->hostwep & HOSTWEP_PRIVACYINVOKED ? 1 : 0;
		break;

	case IW_AUTH_80211_AUTH_ALG:
		param->value =
		    wlandev->hostwep & HOSTWEP_SHAREDKEY ?
		    IW_AUTH_ALG_SHARED_KEY : IW_AUTH_ALG_OPEN_SYSTEM;
		break;

	default:
		break;
	}

	return result;
}

static iw_handler p80211wext_handlers[] = {
	(iw_handler) p80211wext_siwcommit,	/* SIOCSIWCOMMIT */
	(iw_handler) p80211wext_giwname,	/* SIOCGIWNAME */
	(iw_handler) NULL,	/* SIOCSIWNWID */
	(iw_handler) NULL,	/* SIOCGIWNWID */
	(iw_handler) p80211wext_siwfreq,	/* SIOCSIWFREQ */
	(iw_handler) p80211wext_giwfreq,	/* SIOCGIWFREQ */
	(iw_handler) p80211wext_siwmode,	/* SIOCSIWMODE */
	(iw_handler) p80211wext_giwmode,	/* SIOCGIWMODE */
	(iw_handler) NULL,	/* SIOCSIWSENS */
	(iw_handler) NULL,	/* SIOCGIWSENS */
	(iw_handler) NULL,	/* not used *//* SIOCSIWRANGE */
	(iw_handler) p80211wext_giwrange,	/* SIOCGIWRANGE */
	(iw_handler) NULL,	/* not used *//* SIOCSIWPRIV */
	(iw_handler) NULL,	/* kernel code *//* SIOCGIWPRIV */
	(iw_handler) NULL,	/* not used *//* SIOCSIWSTATS */
	(iw_handler) NULL,	/* kernel code *//* SIOCGIWSTATS */
	(iw_handler) p80211wext_siwspy,	/* SIOCSIWSPY */
	(iw_handler) p80211wext_giwspy,	/* SIOCGIWSPY */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* SIOCSIWAP */
	(iw_handler) p80211wext_giwap,	/* SIOCGIWAP */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* SIOCGIWAPLIST */
	(iw_handler) p80211wext_siwscan,	/* SIOCSIWSCAN */
	(iw_handler) p80211wext_giwscan,	/* SIOCGIWSCAN */
	(iw_handler) p80211wext_siwessid,	/* SIOCSIWESSID */
	(iw_handler) p80211wext_giwessid,	/* SIOCGIWESSID */
	(iw_handler) NULL,	/* SIOCSIWNICKN */
	(iw_handler) p80211wext_giwessid,	/* SIOCGIWNICKN */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* SIOCSIWRATE */
	(iw_handler) p80211wext_giwrate,	/* SIOCGIWRATE */
	(iw_handler) p80211wext_siwrts,	/* SIOCSIWRTS */
	(iw_handler) p80211wext_giwrts,	/* SIOCGIWRTS */
	(iw_handler) p80211wext_siwfrag,	/* SIOCSIWFRAG */
	(iw_handler) p80211wext_giwfrag,	/* SIOCGIWFRAG */
	(iw_handler) p80211wext_siwtxpow,	/* SIOCSIWTXPOW */
	(iw_handler) p80211wext_giwtxpow,	/* SIOCGIWTXPOW */
	(iw_handler) p80211wext_siwretry,	/* SIOCSIWRETRY */
	(iw_handler) p80211wext_giwretry,	/* SIOCGIWRETRY */
	(iw_handler) p80211wext_siwencode,	/* SIOCSIWENCODE */
	(iw_handler) p80211wext_giwencode,	/* SIOCGIWENCODE */
	(iw_handler) NULL,	/* SIOCSIWPOWER */
	(iw_handler) NULL,	/* SIOCGIWPOWER */
/* WPA operations */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* SIOCSIWGENIE      set generic IE */
	(iw_handler) NULL,	/* SIOCGIWGENIE      get generic IE */
	(iw_handler) p80211_wext_set_iwauth,	/* SIOCSIWAUTH     set authentication mode params */
	(iw_handler) p80211_wext_get_iwauth,	/* SIOCGIWAUTH     get authentication mode params */

	(iw_handler) p80211wext_set_encodeext,	/* SIOCSIWENCODEEXT  set encoding token & mode */
	(iw_handler) p80211wext_get_encodeext,	/* SIOCGIWENCODEEXT  get encoding token & mode */
	(iw_handler) NULL,	/* SIOCSIWPMKSA      PMKSA cache operation */
};

struct iw_handler_def p80211wext_handler_def = {
	.num_standard = ARRAY_SIZE(p80211wext_handlers),
	.num_private = 0,
	.num_private_args = 0,
	.standard = p80211wext_handlers,
	.private = NULL,
	.private_args = NULL,
	.get_wireless_stats = p80211wext_get_wireless_stats
};

int p80211wext_event_associated(wlandevice_t *wlandev, int assoc)
{
	union iwreq_data data;

	/* Send the association state first */
	data.ap_addr.sa_family = ARPHRD_ETHER;
	if (assoc)
		memcpy(data.ap_addr.sa_data, wlandev->bssid, ETH_ALEN);
	else
		memset(data.ap_addr.sa_data, 0, ETH_ALEN);

	if (wlan_wext_write)
		wireless_send_event(wlandev->netdev, SIOCGIWAP, &data, NULL);

	if (!assoc)
		goto done;

	/* XXX send association data, like IEs, etc etc. */

done:
	return 0;
}
