/* p80211netdev.h
*
* WLAN net device structure and functions
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
* This file declares the structure type that represents each wlan
* interface.
*
* --------------------------------------------------------------------
*/

#ifndef _LINUX_P80211NETDEV_H
#define _LINUX_P80211NETDEV_H

#include <linux/interrupt.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>

#define WLAN_RELEASE	"0.3.0-staging"

#define WLAN_DEVICE_CLOSED	0
#define WLAN_DEVICE_OPEN	1

#define WLAN_MACMODE_NONE	0
#define WLAN_MACMODE_IBSS_STA	1
#define WLAN_MACMODE_ESS_STA	2
#define WLAN_MACMODE_ESS_AP	3

/* MSD States */
#define WLAN_MSD_HWPRESENT_PENDING	1
#define WLAN_MSD_HWFAIL			2
#define WLAN_MSD_HWPRESENT		3
#define WLAN_MSD_FWLOAD_PENDING		4
#define WLAN_MSD_FWLOAD			5
#define WLAN_MSD_RUNNING_PENDING	6
#define WLAN_MSD_RUNNING		7

#ifndef ETH_P_ECONET
#define ETH_P_ECONET   0x0018	/* needed for 2.2.x kernels */
#endif

#define ETH_P_80211_RAW        (ETH_P_ECONET + 1)

#ifndef ARPHRD_IEEE80211
#define ARPHRD_IEEE80211 801	/* kernel 2.4.6 */
#endif

#ifndef ARPHRD_IEEE80211_PRISM	/* kernel 2.4.18 */
#define ARPHRD_IEEE80211_PRISM 802
#endif

/*--- NSD Capabilities Flags ------------------------------*/
#define P80211_NSDCAP_HARDWAREWEP           0x01  /* hardware wep engine */
#define P80211_NSDCAP_SHORT_PREAMBLE        0x10  /* hardware supports */
#define P80211_NSDCAP_HWFRAGMENT            0x80  /* nsd handles frag/defrag */
#define P80211_NSDCAP_AUTOJOIN              0x100 /* nsd does autojoin */
#define P80211_NSDCAP_NOSCAN                0x200 /* nsd can scan */

/* Received frame statistics */
struct p80211_frmrx {
	u32 mgmt;
	u32 assocreq;
	u32 assocresp;
	u32 reassocreq;
	u32 reassocresp;
	u32 probereq;
	u32 proberesp;
	u32 beacon;
	u32 atim;
	u32 disassoc;
	u32 authen;
	u32 deauthen;
	u32 mgmt_unknown;
	u32 ctl;
	u32 pspoll;
	u32 rts;
	u32 cts;
	u32 ack;
	u32 cfend;
	u32 cfendcfack;
	u32 ctl_unknown;
	u32 data;
	u32 dataonly;
	u32 data_cfack;
	u32 data_cfpoll;
	u32 data__cfack_cfpoll;
	u32 null;
	u32 cfack;
	u32 cfpoll;
	u32 cfack_cfpoll;
	u32 data_unknown;
	u32 decrypt;
	u32 decrypt_err;
};

/* called by /proc/net/wireless */
struct iw_statistics *p80211wext_get_wireless_stats(struct net_device *dev);
/* wireless extensions' ioctls */
extern struct iw_handler_def p80211wext_handler_def;

/* WEP stuff */
#define NUM_WEPKEYS 4
#define MAX_KEYLEN 32

#define HOSTWEP_DEFAULTKEY_MASK (BIT(1)|BIT(0))
#define HOSTWEP_SHAREDKEY BIT(3)
#define HOSTWEP_DECRYPT  BIT(4)
#define HOSTWEP_ENCRYPT  BIT(5)
#define HOSTWEP_PRIVACYINVOKED BIT(6)
#define HOSTWEP_EXCLUDEUNENCRYPTED BIT(7)

extern int wlan_watchdog;
extern int wlan_wext_write;

/* WLAN device type */
struct wlandevice {
	void *priv;		/* private data for MSD */

	/* Subsystem State */
	char name[WLAN_DEVNAMELEN_MAX];	/* Dev name, from register_wlandev() */
	char *nsdname;

	u32 state;		/* Device I/F state (open/closed) */
	u32 msdstate;		/* state of underlying driver */
	u32 hwremoved;		/* Has the hw been yanked out? */

	/* Hardware config */
	unsigned int irq;
	unsigned int iobase;
	unsigned int membase;
	u32 nsdcaps;		/* NSD Capabilities flags */

	/* Config vars */
	unsigned int ethconv;

	/* device methods (init by MSD, used by p80211 */
	int (*open)(struct wlandevice *wlandev);
	int (*close)(struct wlandevice *wlandev);
	void (*reset)(struct wlandevice *wlandev);
	int (*txframe)(struct wlandevice *wlandev, struct sk_buff *skb,
			union p80211_hdr *p80211_hdr,
			struct p80211_metawep *p80211_wep);
	int (*mlmerequest)(struct wlandevice *wlandev, struct p80211msg *msg);
	int (*set_multicast_list)(struct wlandevice *wlandev,
				   struct net_device *dev);
	void (*tx_timeout)(struct wlandevice *wlandev);

	/* 802.11 State */
	u8 bssid[WLAN_BSSID_LEN];
	struct p80211pstr32 ssid;
	u32 macmode;
	int linkstatus;

	/* WEP State */
	u8 wep_keys[NUM_WEPKEYS][MAX_KEYLEN];
	u8 wep_keylens[NUM_WEPKEYS];
	int hostwep;

	/* Request/Confirm i/f state (used by p80211) */
	unsigned long request_pending;	/* flag, access atomically */

	/* netlink socket */
	/* queue for indications waiting for cmd completion */
	/* Linux netdevice and support */
	struct net_device *netdev;	/* ptr to linux netdevice */

	/* Rx bottom half */
	struct tasklet_struct rx_bh;

	struct sk_buff_head nsd_rxq;

	/* 802.11 device statistics */
	struct p80211_frmrx rx;

	struct iw_statistics wstats;

	/* jkriegl: iwspy fields */
	u8 spy_number;
	char spy_address[IW_MAX_SPY][ETH_ALEN];
	struct iw_quality spy_stat[IW_MAX_SPY];
};

/* WEP stuff */
int wep_change_key(struct wlandevice *wlandev, int keynum, u8 *key, int keylen);
int wep_decrypt(struct wlandevice *wlandev, u8 *buf, u32 len, int key_override,
		u8 *iv, u8 *icv);
int wep_encrypt(struct wlandevice *wlandev, u8 *buf, u8 *dst, u32 len,
		int keynum, u8 *iv, u8 *icv);

int wlan_setup(struct wlandevice *wlandev, struct device *physdev);
void wlan_unsetup(struct wlandevice *wlandev);
int register_wlandev(struct wlandevice *wlandev);
int unregister_wlandev(struct wlandevice *wlandev);
void p80211netdev_rx(struct wlandevice *wlandev, struct sk_buff *skb);
void p80211netdev_hwremoved(struct wlandevice *wlandev);
#endif
