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

/*================================================================*/
/* Constants */

#define WLAN_DEVICE_CLOSED	0
#define WLAN_DEVICE_OPEN	1

#define WLAN_MACMODE_NONE	0
#define WLAN_MACMODE_IBSS_STA	1
#define WLAN_MACMODE_ESS_STA	2
#define WLAN_MACMODE_ESS_AP	3

/* MSD States */
#define WLAN_MSD_START			-1
#define WLAN_MSD_DRIVERLOADED		0
#define WLAN_MSD_HWPRESENT_PENDING	1
#define WLAN_MSD_HWFAIL			2
#define WLAN_MSD_HWPRESENT		3
#define WLAN_MSD_FWLOAD_PENDING		4
#define WLAN_MSD_FWLOAD			5
#define WLAN_MSD_RUNNING_PENDING	6
#define WLAN_MSD_RUNNING		7

#ifndef ETH_P_ECONET
#define ETH_P_ECONET   0x0018    /* needed for 2.2.x kernels */
#endif

#define ETH_P_80211_RAW        (ETH_P_ECONET + 1)

#ifndef ARPHRD_IEEE80211
#define ARPHRD_IEEE80211 801     /* kernel 2.4.6 */
#endif

#ifndef ARPHRD_IEEE80211_PRISM  /* kernel 2.4.18 */
#define ARPHRD_IEEE80211_PRISM 802
#endif

/*--- NSD Capabilities Flags ------------------------------*/
#define P80211_NSDCAP_HARDWAREWEP           0x01  /* hardware wep engine */
#define P80211_NSDCAP_TIEDWEP               0x02  /* can't decouple en/de */
#define P80211_NSDCAP_NOHOSTWEP             0x04  /* must use hardware wep */
#define P80211_NSDCAP_PBCC                  0x08  /* hardware supports PBCC */
#define P80211_NSDCAP_SHORT_PREAMBLE        0x10  /* hardware supports */
#define P80211_NSDCAP_AGILITY               0x20  /* hardware supports */
#define P80211_NSDCAP_AP_RETRANSMIT         0x40  /* nsd handles retransmits */
#define P80211_NSDCAP_HWFRAGMENT            0x80  /* nsd handles frag/defrag */
#define P80211_NSDCAP_AUTOJOIN              0x100  /* nsd does autojoin */
#define P80211_NSDCAP_NOSCAN                0x200  /* nsd can scan */

/*================================================================*/
/* Macros */

/*================================================================*/
/* Types */

/* Received frame statistics */
typedef struct p80211_frmrx_t
{
	UINT32	mgmt;
	UINT32	assocreq;
	UINT32	assocresp;
	UINT32	reassocreq;
	UINT32	reassocresp;
	UINT32	probereq;
	UINT32	proberesp;
	UINT32	beacon;
	UINT32	atim;
	UINT32	disassoc;
	UINT32	authen;
	UINT32	deauthen;
	UINT32	mgmt_unknown;
	UINT32	ctl;
	UINT32	pspoll;
	UINT32	rts;
	UINT32	cts;
	UINT32	ack;
	UINT32	cfend;
	UINT32	cfendcfack;
	UINT32	ctl_unknown;
	UINT32	data;
	UINT32	dataonly;
	UINT32	data_cfack;
	UINT32	data_cfpoll;
	UINT32	data__cfack_cfpoll;
	UINT32	null;
	UINT32	cfack;
	UINT32	cfpoll;
	UINT32	cfack_cfpoll;
	UINT32	data_unknown;
	UINT32  decrypt;
	UINT32  decrypt_err;
} p80211_frmrx_t;

#ifdef WIRELESS_EXT
/* called by /proc/net/wireless */
struct iw_statistics* p80211wext_get_wireless_stats(netdevice_t *dev);
/* wireless extensions' ioctls */
int p80211wext_support_ioctl(netdevice_t *dev, struct ifreq *ifr, int cmd);
#if WIRELESS_EXT > 12
extern struct iw_handler_def p80211wext_handler_def;
#endif

int p80211wext_event_associated(struct wlandevice *wlandev, int assoc);

#endif /* wireless extensions */

/* WEP stuff */
#define NUM_WEPKEYS 4
#define MAX_KEYLEN 32

#define HOSTWEP_DEFAULTKEY_MASK (BIT1|BIT0)
#define HOSTWEP_DECRYPT  BIT4
#define HOSTWEP_ENCRYPT  BIT5
#define HOSTWEP_PRIVACYINVOKED BIT6
#define HOSTWEP_EXCLUDEUNENCRYPTED BIT7

extern int wlan_watchdog;
extern int wlan_wext_write;

/* WLAN device type */
typedef struct wlandevice
{
	struct wlandevice	*next;		/* link for list of devices */
	void			*priv;		/* private data for MSD */

	/* Subsystem State */
	char		name[WLAN_DEVNAMELEN_MAX]; /* Dev name, from register_wlandev()*/
	char		*nsdname;

	UINT32          state;          /* Device I/F state (open/closed) */
	UINT32		msdstate;	/* state of underlying driver */
	UINT32		hwremoved;	/* Has the hw been yanked out? */

	/* Hardware config */
	UINT		irq;
	UINT		iobase;
	UINT		membase;
	UINT32          nsdcaps;  /* NSD Capabilities flags */

	/* Config vars */
	UINT		ethconv;

	/* device methods (init by MSD, used by p80211 */
	int		(*open)(struct wlandevice *wlandev);
	int		(*close)(struct wlandevice *wlandev);
	void		(*reset)(struct wlandevice *wlandev );
	int		(*txframe)(struct wlandevice *wlandev, struct sk_buff *skb, p80211_hdr_t *p80211_hdr, p80211_metawep_t *p80211_wep);
	int		(*mlmerequest)(struct wlandevice *wlandev, p80211msg_t *msg);
	int             (*set_multicast_list)(struct wlandevice *wlandev,
					      netdevice_t *dev);
	void		(*tx_timeout)(struct wlandevice *wlandev);

#ifdef CONFIG_PROC_FS
	int             (*nsd_proc_read)(char *page, char **start, off_t offset, int count, int	*eof, void *data);
#endif

	/* 802.11 State */
	UINT8		bssid[WLAN_BSSID_LEN];
	p80211pstr32_t	ssid;
	UINT32		macmode;
	int             linkstatus;
	int             shortpreamble;  /* C bool */

	/* WEP State */
	UINT8 wep_keys[NUM_WEPKEYS][MAX_KEYLEN];
	UINT8 wep_keylens[NUM_WEPKEYS];
	int   hostwep;

	/* Request/Confirm i/f state (used by p80211) */
	unsigned long		request_pending; /* flag, access atomically */

	/* netlink socket */
	/* queue for indications waiting for cmd completion */
	/* Linux netdevice and support */
	netdevice_t		*netdev;	/* ptr to linux netdevice */
	struct net_device_stats linux_stats;

#ifdef CONFIG_PROC_FS
	/* Procfs support */
	struct proc_dir_entry	*procdir;
	struct proc_dir_entry	*procwlandev;
#endif

	/* Rx bottom half */
	struct tasklet_struct	rx_bh;

	struct sk_buff_head	nsd_rxq;

	/* 802.11 device statistics */
	struct p80211_frmrx_t	rx;

/* compatibility to wireless extensions */
#ifdef WIRELESS_EXT
	struct iw_statistics	wstats;

	/* jkriegl: iwspy fields */
        UINT8			spy_number;
        char			spy_address[IW_MAX_SPY][ETH_ALEN];
        struct iw_quality       spy_stat[IW_MAX_SPY];

#endif

} wlandevice_t;

/* WEP stuff */
int wep_change_key(wlandevice_t *wlandev, int keynum, UINT8* key, int keylen);
int wep_decrypt(wlandevice_t *wlandev, UINT8 *buf, UINT32 len, int key_override, UINT8 *iv, UINT8 *icv);
int wep_encrypt(wlandevice_t *wlandev, UINT8 *buf, UINT8 *dst, UINT32 len, int keynum, UINT8 *iv, UINT8 *icv);

/*================================================================*/
/* Externs */

/*================================================================*/
/* Function Declarations */

void	p80211netdev_startup(void);
void	p80211netdev_shutdown(void);
int	wlan_setup(wlandevice_t *wlandev);
int	wlan_unsetup(wlandevice_t *wlandev);
int	register_wlandev(wlandevice_t *wlandev);
int	unregister_wlandev(wlandevice_t *wlandev);
void	p80211netdev_rx(wlandevice_t *wlandev, struct sk_buff *skb);
void	p80211netdev_hwremoved(wlandevice_t *wlandev);
void    p80211_suspend(wlandevice_t *wlandev);
void    p80211_resume(wlandevice_t *wlandev);

/*================================================================*/
/* Function Definitions */

static inline void
p80211netdev_stop_queue(wlandevice_t *wlandev)
{
	if ( !wlandev ) return;
	if ( !wlandev->netdev ) return;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,38) )
	wlandev->netdev->tbusy = 1;
	wlandev->netdev->start = 0;
#else
	netif_stop_queue(wlandev->netdev);
#endif
}

static inline void
p80211netdev_start_queue(wlandevice_t *wlandev)
{
	if ( !wlandev ) return;
	if ( !wlandev->netdev ) return;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,38) )
	wlandev->netdev->tbusy = 0;
	wlandev->netdev->start = 1;
#else
	netif_start_queue(wlandev->netdev);
#endif
}

static inline void
p80211netdev_wake_queue(wlandevice_t *wlandev)
{
	if ( !wlandev ) return;
	if ( !wlandev->netdev ) return;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,38) )
	wlandev->netdev->tbusy = 0;
	mark_bh(NET_BH);
#else
	netif_wake_queue(wlandev->netdev);
#endif
}

#ifdef CONFIG_HOTPLUG
#define WLAN_HOTPLUG_REGISTER "register"
#define WLAN_HOTPLUG_REMOVE   "remove"
#define WLAN_HOTPLUG_STARTUP  "startup"
#define WLAN_HOTPLUG_SHUTDOWN "shutdown"
#define WLAN_HOTPLUG_SUSPEND "suspend"
#define WLAN_HOTPLUG_RESUME "resume"
int p80211_run_sbin_hotplug(wlandevice_t *wlandev, char *action);
#endif

#endif
