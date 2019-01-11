// SPDX-License-Identifier: (GPL-2.0 OR MPL-1.1)
/* src/prism2/driver/prism2mib.c
 *
 * Management request for mibset/mibget
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
 * The functions in this file handle the mibset/mibget management
 * functions.
 *
 * --------------------------------------------------------------------
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
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

#define MIB_TMP_MAXLEN    200	/* Max length of RID record (in bytes). */

#define  F_STA        0x1	/* MIB is supported on stations. */
#define  F_READ       0x2	/* MIB may be read. */
#define  F_WRITE      0x4	/* MIB may be written. */

struct mibrec {
	u32 did;
	u16 flag;
	u16 parm1;
	u16 parm2;
	u16 parm3;
	int (*func)(struct mibrec *mib,
		    int isget,
		    struct wlandevice *wlandev,
		    struct hfa384x *hw,
		    struct p80211msg_dot11req_mibset *msg, void *data);
};

static int prism2mib_bytearea2pstr(struct mibrec *mib,
				   int isget,
				   struct wlandevice *wlandev,
				   struct hfa384x *hw,
				   struct p80211msg_dot11req_mibset *msg,
				   void *data);

static int prism2mib_uint32(struct mibrec *mib,
			    int isget,
			    struct wlandevice *wlandev,
			    struct hfa384x *hw,
			    struct p80211msg_dot11req_mibset *msg, void *data);

static int prism2mib_flag(struct mibrec *mib,
			  int isget,
			  struct wlandevice *wlandev,
			  struct hfa384x *hw,
			  struct p80211msg_dot11req_mibset *msg, void *data);

static int prism2mib_wepdefaultkey(struct mibrec *mib,
				   int isget,
				   struct wlandevice *wlandev,
				   struct hfa384x *hw,
				   struct p80211msg_dot11req_mibset *msg,
				   void *data);

static int prism2mib_privacyinvoked(struct mibrec *mib,
				    int isget,
				    struct wlandevice *wlandev,
				    struct hfa384x *hw,
				    struct p80211msg_dot11req_mibset *msg,
				    void *data);

static int prism2mib_excludeunencrypted(struct mibrec *mib,
					int isget,
					struct wlandevice *wlandev,
					struct hfa384x *hw,
					struct p80211msg_dot11req_mibset *msg,
					void *data);

static int
prism2mib_fragmentationthreshold(struct mibrec *mib,
				 int isget,
				 struct wlandevice *wlandev,
				 struct hfa384x *hw,
				 struct p80211msg_dot11req_mibset *msg,
				 void *data);

static int prism2mib_priv(struct mibrec *mib,
			  int isget,
			  struct wlandevice *wlandev,
			  struct hfa384x *hw,
			  struct p80211msg_dot11req_mibset *msg, void *data);

static struct mibrec mibtab[] = {
	/* dot11smt MIB's */
	{didmib_dot11smt_wepdefaultkeystable_key(1),
	 F_STA | F_WRITE,
	 HFA384x_RID_CNFWEPDEFAULTKEY0, 0, 0,
	 prism2mib_wepdefaultkey},
	{didmib_dot11smt_wepdefaultkeystable_key(2),
	 F_STA | F_WRITE,
	 HFA384x_RID_CNFWEPDEFAULTKEY1, 0, 0,
	 prism2mib_wepdefaultkey},
	{didmib_dot11smt_wepdefaultkeystable_key(3),
	 F_STA | F_WRITE,
	 HFA384x_RID_CNFWEPDEFAULTKEY2, 0, 0,
	 prism2mib_wepdefaultkey},
	{didmib_dot11smt_wepdefaultkeystable_key(4),
	 F_STA | F_WRITE,
	 HFA384x_RID_CNFWEPDEFAULTKEY3, 0, 0,
	 prism2mib_wepdefaultkey},
	{DIDMIB_DOT11SMT_PRIVACYTABLE_PRIVACYINVOKED,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_CNFWEPFLAGS, HFA384x_WEPFLAGS_PRIVINVOKED, 0,
	 prism2mib_privacyinvoked},
	{DIDMIB_DOT11SMT_PRIVACYTABLE_WEPDEFAULTKEYID,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_CNFWEPDEFAULTKEYID, 0, 0,
	 prism2mib_uint32},
	{DIDMIB_DOT11SMT_PRIVACYTABLE_EXCLUDEUNENCRYPTED,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_CNFWEPFLAGS, HFA384x_WEPFLAGS_EXCLUDE, 0,
	 prism2mib_excludeunencrypted},

	/* dot11mac MIB's */

	{DIDMIB_DOT11MAC_OPERATIONTABLE_MACADDRESS,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_CNFOWNMACADDR, HFA384x_RID_CNFOWNMACADDR_LEN, 0,
	 prism2mib_bytearea2pstr},
	{DIDMIB_DOT11MAC_OPERATIONTABLE_RTSTHRESHOLD,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_RTSTHRESH, 0, 0,
	 prism2mib_uint32},
	{DIDMIB_DOT11MAC_OPERATIONTABLE_SHORTRETRYLIMIT,
	 F_STA | F_READ,
	 HFA384x_RID_SHORTRETRYLIMIT, 0, 0,
	 prism2mib_uint32},
	{DIDMIB_DOT11MAC_OPERATIONTABLE_LONGRETRYLIMIT,
	 F_STA | F_READ,
	 HFA384x_RID_LONGRETRYLIMIT, 0, 0,
	 prism2mib_uint32},
	{DIDMIB_DOT11MAC_OPERATIONTABLE_FRAGMENTATIONTHRESHOLD,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_FRAGTHRESH, 0, 0,
	 prism2mib_fragmentationthreshold},
	{DIDMIB_DOT11MAC_OPERATIONTABLE_MAXTRANSMITMSDULIFETIME,
	 F_STA | F_READ,
	 HFA384x_RID_MAXTXLIFETIME, 0, 0,
	 prism2mib_uint32},

	/* dot11phy MIB's */

	{DIDMIB_DOT11PHY_DSSSTABLE_CURRENTCHANNEL,
	 F_STA | F_READ,
	 HFA384x_RID_CURRENTCHANNEL, 0, 0,
	 prism2mib_uint32},
	{DIDMIB_DOT11PHY_TXPOWERTABLE_CURRENTTXPOWERLEVEL,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_TXPOWERMAX, 0, 0,
	 prism2mib_uint32},

	/* p2Static MIB's */

	{DIDMIB_P2_STATIC_CNFPORTTYPE,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_CNFPORTTYPE, 0, 0,
	 prism2mib_uint32},

	/* p2MAC MIB's */

	{DIDMIB_P2_MAC_CURRENTTXRATE,
	 F_STA | F_READ,
	 HFA384x_RID_CURRENTTXRATE, 0, 0,
	 prism2mib_uint32},

	/* And finally, lnx mibs */
	{DIDMIB_LNX_CONFIGTABLE_RSNAIE,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_CNFWPADATA, 0, 0,
	 prism2mib_priv},
	{0, 0, 0, 0, 0, NULL}
};

/*
 * prism2mgmt_mibset_mibget
 *
 * Set the value of a mib item.
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
 */

int prism2mgmt_mibset_mibget(struct wlandevice *wlandev, void *msgp)
{
	struct hfa384x *hw = wlandev->priv;
	int result, isget;
	struct mibrec *mib;

	u16 which;

	struct p80211msg_dot11req_mibset *msg = msgp;
	struct p80211itemd *mibitem;

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	msg->resultcode.data = P80211ENUM_resultcode_success;

	/*
	 ** Determine if this is an Access Point or a station.
	 */

	which = F_STA;

	/*
	 ** Find the MIB in the MIB table.  Note that a MIB may be in the
	 ** table twice...once for an AP and once for a station.  Make sure
	 ** to get the correct one.  Note that DID=0 marks the end of the
	 ** MIB table.
	 */

	mibitem = (struct p80211itemd *)msg->mibattribute.data;

	for (mib = mibtab; mib->did != 0; mib++)
		if (mib->did == mibitem->did && (mib->flag & which))
			break;

	if (mib->did == 0) {
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
		goto done;
	}

	/*
	 ** Determine if this is a "mibget" or a "mibset".  If this is a
	 ** "mibget", then make sure that the MIB may be read.  Otherwise,
	 ** this is a "mibset" so make make sure that the MIB may be written.
	 */

	isget = (msg->msgcode == DIDMSG_DOT11REQ_MIBGET);

	if (isget) {
		if (!(mib->flag & F_READ)) {
			msg->resultcode.data =
			    P80211ENUM_resultcode_cant_get_writeonly_mib;
			goto done;
		}
	} else {
		if (!(mib->flag & F_WRITE)) {
			msg->resultcode.data =
			    P80211ENUM_resultcode_cant_set_readonly_mib;
			goto done;
		}
	}

	/*
	 ** Execute the MIB function.  If things worked okay, then make
	 ** sure that the MIB function also worked okay.  If so, and this
	 ** is a "mibget", then the status value must be set for both the
	 ** "mibattribute" parameter and the mib item within the data
	 ** portion of the "mibattribute".
	 */

	result = mib->func(mib, isget, wlandev, hw, msg, (void *)mibitem->data);

	if (msg->resultcode.data == P80211ENUM_resultcode_success) {
		if (result != 0) {
			pr_debug("get/set failure, result=%d\n", result);
			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
		} else {
			if (isget) {
				msg->mibattribute.status =
				    P80211ENUM_msgitem_status_data_ok;
				mibitem->status =
				    P80211ENUM_msgitem_status_data_ok;
			}
		}
	}

done:
	return 0;
}

/*
 * prism2mib_bytearea2pstr
 *
 * Get/set pstr data to/from a byte area.
 *
 * MIB record parameters:
 *       parm1    Prism2 RID value.
 *       parm2    Number of bytes of RID data.
 *       parm3    Not used.
 *
 * Arguments:
 *       mib      MIB record.
 *       isget    MIBGET/MIBSET flag.
 *       wlandev  wlan device structure.
 *       priv     "priv" structure.
 *       hw       "hw" structure.
 *       msg      Message structure.
 *       data     Data buffer.
 *
 * Returns:
 *       0   - Success.
 *       ~0  - Error.
 *
 */

static int prism2mib_bytearea2pstr(struct mibrec *mib,
				   int isget,
				   struct wlandevice *wlandev,
				   struct hfa384x *hw,
				   struct p80211msg_dot11req_mibset *msg,
				   void *data)
{
	int result;
	struct p80211pstrd *pstr = data;
	u8 bytebuf[MIB_TMP_MAXLEN];

	if (isget) {
		result =
		    hfa384x_drvr_getconfig(hw, mib->parm1, bytebuf, mib->parm2);
		prism2mgmt_bytearea2pstr(bytebuf, pstr, mib->parm2);
	} else {
		memset(bytebuf, 0, mib->parm2);
		memcpy(bytebuf, pstr->data, pstr->len);
		result =
		    hfa384x_drvr_setconfig(hw, mib->parm1, bytebuf, mib->parm2);
	}

	return result;
}

/*
 * prism2mib_uint32
 *
 * Get/set uint32 data.
 *
 * MIB record parameters:
 *       parm1    Prism2 RID value.
 *       parm2    Not used.
 *       parm3    Not used.
 *
 * Arguments:
 *       mib      MIB record.
 *       isget    MIBGET/MIBSET flag.
 *       wlandev  wlan device structure.
 *       priv     "priv" structure.
 *       hw       "hw" structure.
 *       msg      Message structure.
 *       data     Data buffer.
 *
 * Returns:
 *       0   - Success.
 *       ~0  - Error.
 *
 */

static int prism2mib_uint32(struct mibrec *mib,
			    int isget,
			    struct wlandevice *wlandev,
			    struct hfa384x *hw,
			    struct p80211msg_dot11req_mibset *msg, void *data)
{
	int result;
	u32 *uint32 = data;
	u8 bytebuf[MIB_TMP_MAXLEN];
	u16 *wordbuf = (u16 *)bytebuf;

	if (isget) {
		result = hfa384x_drvr_getconfig16(hw, mib->parm1, wordbuf);
		*uint32 = *wordbuf;
	} else {
		*wordbuf = *uint32;
		result = hfa384x_drvr_setconfig16(hw, mib->parm1, *wordbuf);
	}

	return result;
}

/*
 * prism2mib_flag
 *
 * Get/set a flag.
 *
 * MIB record parameters:
 *       parm1    Prism2 RID value.
 *       parm2    Bit to get/set.
 *       parm3    Not used.
 *
 * Arguments:
 *       mib      MIB record.
 *       isget    MIBGET/MIBSET flag.
 *       wlandev  wlan device structure.
 *       priv     "priv" structure.
 *       hw       "hw" structure.
 *       msg      Message structure.
 *       data     Data buffer.
 *
 * Returns:
 *       0   - Success.
 *       ~0  - Error.
 *
 */

static int prism2mib_flag(struct mibrec *mib,
			  int isget,
			  struct wlandevice *wlandev,
			  struct hfa384x *hw,
			  struct p80211msg_dot11req_mibset *msg, void *data)
{
	int result;
	u32 *uint32 = data;
	u8 bytebuf[MIB_TMP_MAXLEN];
	u16 *wordbuf = (u16 *)bytebuf;
	u32 flags;

	result = hfa384x_drvr_getconfig16(hw, mib->parm1, wordbuf);
	if (result == 0) {
		flags = *wordbuf;
		if (isget) {
			*uint32 = (flags & mib->parm2) ?
			    P80211ENUM_truth_true : P80211ENUM_truth_false;
		} else {
			if ((*uint32) == P80211ENUM_truth_true)
				flags |= mib->parm2;
			else
				flags &= ~mib->parm2;
			*wordbuf = flags;
			result =
			    hfa384x_drvr_setconfig16(hw, mib->parm1, *wordbuf);
		}
	}

	return result;
}

/*
 * prism2mib_wepdefaultkey
 *
 * Get/set WEP default keys.
 *
 * MIB record parameters:
 *       parm1    Prism2 RID value.
 *       parm2    Number of bytes of RID data.
 *       parm3    Not used.
 *
 * Arguments:
 *       mib      MIB record.
 *       isget    MIBGET/MIBSET flag.
 *       wlandev  wlan device structure.
 *       priv     "priv" structure.
 *       hw       "hw" structure.
 *       msg      Message structure.
 *       data     Data buffer.
 *
 * Returns:
 *       0   - Success.
 *       ~0  - Error.
 *
 */

static int prism2mib_wepdefaultkey(struct mibrec *mib,
				   int isget,
				   struct wlandevice *wlandev,
				   struct hfa384x *hw,
				   struct p80211msg_dot11req_mibset *msg,
				   void *data)
{
	int result;
	struct p80211pstrd *pstr = data;
	u8 bytebuf[MIB_TMP_MAXLEN];
	u16 len;

	if (isget) {
		result = 0;	/* Should never happen. */
	} else {
		len = (pstr->len > 5) ? HFA384x_RID_CNFWEP128DEFAULTKEY_LEN :
		    HFA384x_RID_CNFWEPDEFAULTKEY_LEN;
		memset(bytebuf, 0, len);
		memcpy(bytebuf, pstr->data, pstr->len);
		result = hfa384x_drvr_setconfig(hw, mib->parm1, bytebuf, len);
	}

	return result;
}

/*
 * prism2mib_privacyinvoked
 *
 * Get/set the dot11PrivacyInvoked value.
 *
 * MIB record parameters:
 *       parm1    Prism2 RID value.
 *       parm2    Bit value for PrivacyInvoked flag.
 *       parm3    Not used.
 *
 * Arguments:
 *       mib      MIB record.
 *       isget    MIBGET/MIBSET flag.
 *       wlandev  wlan device structure.
 *       priv     "priv" structure.
 *       hw       "hw" structure.
 *       msg      Message structure.
 *       data     Data buffer.
 *
 * Returns:
 *       0   - Success.
 *       ~0  - Error.
 *
 */

static int prism2mib_privacyinvoked(struct mibrec *mib,
				    int isget,
				    struct wlandevice *wlandev,
				    struct hfa384x *hw,
				    struct p80211msg_dot11req_mibset *msg,
				    void *data)
{
	if (wlandev->hostwep & HOSTWEP_DECRYPT) {
		if (wlandev->hostwep & HOSTWEP_DECRYPT)
			mib->parm2 |= HFA384x_WEPFLAGS_DISABLE_RXCRYPT;
		if (wlandev->hostwep & HOSTWEP_ENCRYPT)
			mib->parm2 |= HFA384x_WEPFLAGS_DISABLE_TXCRYPT;
	}

	return prism2mib_flag(mib, isget, wlandev, hw, msg, data);
}

/*
 * prism2mib_excludeunencrypted
 *
 * Get/set the dot11ExcludeUnencrypted value.
 *
 * MIB record parameters:
 *       parm1    Prism2 RID value.
 *       parm2    Bit value for ExcludeUnencrypted flag.
 *       parm3    Not used.
 *
 * Arguments:
 *       mib      MIB record.
 *       isget    MIBGET/MIBSET flag.
 *       wlandev  wlan device structure.
 *       priv     "priv" structure.
 *       hw       "hw" structure.
 *       msg      Message structure.
 *       data     Data buffer.
 *
 * Returns:
 *       0   - Success.
 *       ~0  - Error.
 *
 */

static int prism2mib_excludeunencrypted(struct mibrec *mib,
					int isget,
					struct wlandevice *wlandev,
					struct hfa384x *hw,
					struct p80211msg_dot11req_mibset *msg,
					void *data)
{
	return prism2mib_flag(mib, isget, wlandev, hw, msg, data);
}

/*
 * prism2mib_fragmentationthreshold
 *
 * Get/set the fragmentation threshold.
 *
 * MIB record parameters:
 *       parm1    Prism2 RID value.
 *       parm2    Not used.
 *       parm3    Not used.
 *
 * Arguments:
 *       mib      MIB record.
 *       isget    MIBGET/MIBSET flag.
 *       wlandev  wlan device structure.
 *       priv     "priv" structure.
 *       hw       "hw" structure.
 *       msg      Message structure.
 *       data     Data buffer.
 *
 * Returns:
 *       0   - Success.
 *       ~0  - Error.
 *
 */

static int
prism2mib_fragmentationthreshold(struct mibrec *mib,
				 int isget,
				 struct wlandevice *wlandev,
				 struct hfa384x *hw,
				 struct p80211msg_dot11req_mibset *msg,
				 void *data)
{
	u32 *uint32 = data;

	if (!isget)
		if ((*uint32) % 2) {
			netdev_warn(wlandev->netdev,
				    "Attempt to set odd number FragmentationThreshold\n");
			msg->resultcode.data =
			    P80211ENUM_resultcode_not_supported;
			return 0;
		}

	return prism2mib_uint32(mib, isget, wlandev, hw, msg, data);
}

/*
 * prism2mib_priv
 *
 * Get/set values in the "priv" data structure.
 *
 * MIB record parameters:
 *       parm1    Not used.
 *       parm2    Not used.
 *       parm3    Not used.
 *
 * Arguments:
 *       mib      MIB record.
 *       isget    MIBGET/MIBSET flag.
 *       wlandev  wlan device structure.
 *       priv     "priv" structure.
 *       hw       "hw" structure.
 *       msg      Message structure.
 *       data     Data buffer.
 *
 * Returns:
 *       0   - Success.
 *       ~0  - Error.
 *
 */

static int prism2mib_priv(struct mibrec *mib,
			  int isget,
			  struct wlandevice *wlandev,
			  struct hfa384x *hw,
			  struct p80211msg_dot11req_mibset *msg, void *data)
{
	struct p80211pstrd *pstr = data;

	switch (mib->did) {
	case DIDMIB_LNX_CONFIGTABLE_RSNAIE: {
		struct hfa384x_wpa_data wpa;

		if (isget) {
			hfa384x_drvr_getconfig(hw,
					       HFA384x_RID_CNFWPADATA,
					       (u8 *)&wpa,
					       sizeof(wpa));
			pstr->len = le16_to_cpu(wpa.datalen);
			memcpy(pstr->data, wpa.data, pstr->len);
		} else {
			wpa.datalen = cpu_to_le16(pstr->len);
			memcpy(wpa.data, pstr->data, pstr->len);

			hfa384x_drvr_setconfig(hw,
					       HFA384x_RID_CNFWPADATA,
					       (u8 *)&wpa,
					       sizeof(wpa));
		}
		break;
	}
	default:
		netdev_err(wlandev->netdev, "Unhandled DID 0x%08x\n", mib->did);
	}

	return 0;
}

/*
 * prism2mgmt_pstr2bytestr
 *
 * Convert the pstr data in the WLAN message structure into an hfa384x
 * byte string format.
 *
 * Arguments:
 *	bytestr		hfa384x byte string data type
 *	pstr		wlan message data
 *
 * Returns:
 *	Nothing
 *
 */

void prism2mgmt_pstr2bytestr(struct hfa384x_bytestr *bytestr,
			     struct p80211pstrd *pstr)
{
	bytestr->len = cpu_to_le16((u16)(pstr->len));
	memcpy(bytestr->data, pstr->data, pstr->len);
}

/*
 * prism2mgmt_bytestr2pstr
 *
 * Convert the data in an hfa384x byte string format into a
 * pstr in the WLAN message.
 *
 * Arguments:
 *	bytestr		hfa384x byte string data type
 *	msg		wlan message
 *
 * Returns:
 *	Nothing
 *
 */

void prism2mgmt_bytestr2pstr(struct hfa384x_bytestr *bytestr,
			     struct p80211pstrd *pstr)
{
	pstr->len = (u8)(le16_to_cpu(bytestr->len));
	memcpy(pstr->data, bytestr->data, pstr->len);
}

/*
 * prism2mgmt_bytearea2pstr
 *
 * Convert the data in an hfa384x byte area format into a pstr
 * in the WLAN message.
 *
 * Arguments:
 *	bytearea	hfa384x byte area data type
 *	msg		wlan message
 *
 * Returns:
 *	Nothing
 *
 */

void prism2mgmt_bytearea2pstr(u8 *bytearea, struct p80211pstrd *pstr, int len)
{
	pstr->len = (u8)len;
	memcpy(pstr->data, bytearea, len);
}
