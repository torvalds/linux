/* src/p80211/p80211knetdev.c
*
* Linux Kernel net device interface
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
* The functions required for a Linux network device are defined here.
*
* --------------------------------------------------------------------
*/


/*================================================================*/
/* System Includes */


#include <linux/version.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/kmod.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/sockios.h>
#include <linux/etherdevice.h>

#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

#ifdef SIOCETHTOOL
#include <linux/ethtool.h>
#endif

#if WIRELESS_EXT > 12
#include <net/iw_handler.h>
#endif
#include <net/net_namespace.h>

/*================================================================*/
/* Project Includes */

#include "version.h"
#include "wlan_compat.h"
#include "p80211types.h"
#include "p80211hdr.h"
#include "p80211conv.h"
#include "p80211mgmt.h"
#include "p80211msg.h"
#include "p80211netdev.h"
#include "p80211ioctl.h"
#include "p80211req.h"
#include "p80211metastruct.h"
#include "p80211metadef.h"

/*================================================================*/
/* Local Constants */

/*================================================================*/
/* Local Macros */


/*================================================================*/
/* Local Types */

/*================================================================*/
/* Local Static Definitions */

#define __NO_VERSION__		/* prevent the static definition */

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry	*proc_p80211;
#endif

/*================================================================*/
/* Local Function Declarations */

/* Support functions */
static void p80211netdev_rx_bh(unsigned long arg);

/* netdevice method functions */
static int p80211knetdev_init( netdevice_t *netdev);
static struct net_device_stats* p80211knetdev_get_stats(netdevice_t *netdev);
static int p80211knetdev_open( netdevice_t *netdev);
static int p80211knetdev_stop( netdevice_t *netdev );
static int p80211knetdev_hard_start_xmit( struct sk_buff *skb, netdevice_t *netdev);
static void p80211knetdev_set_multicast_list(netdevice_t *dev);
static int p80211knetdev_do_ioctl(netdevice_t *dev, struct ifreq *ifr, int cmd);
static int p80211knetdev_set_mac_address(netdevice_t *dev, void *addr);
static void p80211knetdev_tx_timeout(netdevice_t *netdev);
static int p80211_rx_typedrop( wlandevice_t *wlandev, UINT16 fc);

#ifdef CONFIG_PROC_FS
static int
p80211netdev_proc_read(
	char	*page,
	char	**start,
	off_t	offset,
	int	count,
	int	*eof,
	void	*data);
#endif

/*================================================================*/
/* Function Definitions */

/*----------------------------------------------------------------
* p80211knetdev_startup
*
* Initialize the wlandevice/netdevice part of 802.11 services at
* load time.
*
* Arguments:
*	none
*
* Returns:
*	nothing
----------------------------------------------------------------*/
void p80211netdev_startup(void)
{
	DBFENTER;

#ifdef CONFIG_PROC_FS
	if (init_net.proc_net != NULL) {
		proc_p80211 = create_proc_entry(
				"p80211",
				(S_IFDIR|S_IRUGO|S_IXUGO),
				init_net.proc_net);
	}
#endif
	DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* p80211knetdev_shutdown
*
* Shutdown the wlandevice/netdevice part of 802.11 services at
* unload time.
*
* Arguments:
*	none
*
* Returns:
*	nothing
----------------------------------------------------------------*/
void
p80211netdev_shutdown(void)
{
	DBFENTER;
#ifdef CONFIG_PROC_FS
	if (proc_p80211 != NULL) {
		remove_proc_entry("p80211", init_net.proc_net);
	}
#endif
	DBFEXIT;
}

/*----------------------------------------------------------------
* p80211knetdev_init
*
* Init method for a Linux netdevice.  Called in response to
* register_netdev.
*
* Arguments:
*	none
*
* Returns:
*	nothing
----------------------------------------------------------------*/
static int p80211knetdev_init( netdevice_t *netdev)
{
	DBFENTER;
	/* Called in response to register_netdev */
	/* This is usually the probe function, but the probe has */
	/* already been done by the MSD and the create_kdev */
	/* function.  All we do here is return success */
	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* p80211knetdev_get_stats
*
* Statistics retrieval for linux netdevices.  Here we're reporting
* the Linux i/f level statistics.  Hence, for the primary numbers,
* we don't want to report the numbers from the MIB.  Eventually,
* it might be useful to collect some of the error counters though.
*
* Arguments:
*	netdev		Linux netdevice
*
* Returns:
*	the address of the statistics structure
----------------------------------------------------------------*/
static struct net_device_stats*
p80211knetdev_get_stats(netdevice_t *netdev)
{
	wlandevice_t	*wlandev = (wlandevice_t*)netdev->priv;
	DBFENTER;

	/* TODO: review the MIB stats for items that correspond to
		linux stats */

	DBFEXIT;
	return &(wlandev->linux_stats);
}


/*----------------------------------------------------------------
* p80211knetdev_open
*
* Linux netdevice open method.  Following a successful call here,
* the device is supposed to be ready for tx and rx.  In our
* situation that may not be entirely true due to the state of the
* MAC below.
*
* Arguments:
*	netdev		Linux network device structure
*
* Returns:
*	zero on success, non-zero otherwise
----------------------------------------------------------------*/
static int p80211knetdev_open( netdevice_t *netdev )
{
	int 		result = 0; /* success */
	wlandevice_t	*wlandev = (wlandevice_t*)(netdev->priv);

	DBFENTER;

	/* Check to make sure the MSD is running */
	if ( wlandev->msdstate != WLAN_MSD_RUNNING ) {
		return -ENODEV;
	}

	/* Tell the MSD to open */
	if ( wlandev->open != NULL) {
		result = wlandev->open(wlandev);
		if ( result == 0 ) {
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,43) )
			netdev->interrupt = 0;
#endif
			p80211netdev_start_queue(wlandev);
			wlandev->state = WLAN_DEVICE_OPEN;
		}
	} else {
		result = -EAGAIN;
	}

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* p80211knetdev_stop
*
* Linux netdevice stop (close) method.  Following this call,
* no frames should go up or down through this interface.
*
* Arguments:
*	netdev		Linux network device structure
*
* Returns:
*	zero on success, non-zero otherwise
----------------------------------------------------------------*/
static int p80211knetdev_stop( netdevice_t *netdev )
{
	int		result = 0;
	wlandevice_t	*wlandev = (wlandevice_t*)(netdev->priv);

	DBFENTER;

	if ( wlandev->close != NULL ) {
		result = wlandev->close(wlandev);
	}

	p80211netdev_stop_queue(wlandev);
	wlandev->state = WLAN_DEVICE_CLOSED;

	DBFEXIT;
	return result;
}

/*----------------------------------------------------------------
* p80211netdev_rx
*
* Frame receive function called by the mac specific driver.
*
* Arguments:
*	wlandev		WLAN network device structure
*	skb		skbuff containing a full 802.11 frame.
* Returns:
*	nothing
* Side effects:
*
----------------------------------------------------------------*/
void
p80211netdev_rx(wlandevice_t *wlandev, struct sk_buff *skb )
{
	DBFENTER;

	/* Enqueue for post-irq processing */
	skb_queue_tail(&wlandev->nsd_rxq, skb);

	tasklet_schedule(&wlandev->rx_bh);

        DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* p80211netdev_rx_bh
*
* Deferred processing of all received frames.
*
* Arguments:
*	wlandev		WLAN network device structure
*	skb		skbuff containing a full 802.11 frame.
* Returns:
*	nothing
* Side effects:
*
----------------------------------------------------------------*/
static void p80211netdev_rx_bh(unsigned long arg)
{
	wlandevice_t *wlandev = (wlandevice_t *) arg;
	struct sk_buff *skb = NULL;
	netdevice_t     *dev = wlandev->netdev;
	p80211_hdr_a3_t *hdr;
	UINT16 fc;

        DBFENTER;

	/* Let's empty our our queue */
	while ( (skb = skb_dequeue(&wlandev->nsd_rxq)) ) {
		if (wlandev->state == WLAN_DEVICE_OPEN) {

			if (dev->type != ARPHRD_ETHER) {
				/* RAW frame; we shouldn't convert it */
				// XXX Append the Prism Header here instead.

				/* set up various data fields */
				skb->dev = dev;
				skb_reset_mac_header(skb);
				skb->ip_summed = CHECKSUM_NONE;
				skb->pkt_type = PACKET_OTHERHOST;
				skb->protocol = htons(ETH_P_80211_RAW);
				dev->last_rx = jiffies;

				wlandev->linux_stats.rx_packets++;
				wlandev->linux_stats.rx_bytes += skb->len;
				netif_rx_ni(skb);
				continue;
			} else {
				hdr = (p80211_hdr_a3_t *)skb->data;
				fc = ieee2host16(hdr->fc);
				if (p80211_rx_typedrop(wlandev, fc)) {
					dev_kfree_skb(skb);
					continue;
				}

				/* perform mcast filtering */
				if (wlandev->netdev->flags & IFF_ALLMULTI) {
					/* allow my local address through */
					if (memcmp(hdr->a1, wlandev->netdev->dev_addr, WLAN_ADDR_LEN) != 0) {
						/* but reject anything else that isn't multicast */
						if (!(hdr->a1[0] & 0x01)) {
							dev_kfree_skb(skb);
							continue;
						}
					}
				}

				if ( skb_p80211_to_ether(wlandev, wlandev->ethconv, skb) == 0 ) {
					skb->dev->last_rx = jiffies;
					wlandev->linux_stats.rx_packets++;
					wlandev->linux_stats.rx_bytes += skb->len;
					netif_rx_ni(skb);
					continue;
				}
				WLAN_LOG_DEBUG(1, "p80211_to_ether failed.\n");
			}
		}
		dev_kfree_skb(skb);
	}

        DBFEXIT;
}


/*----------------------------------------------------------------
* p80211knetdev_hard_start_xmit
*
* Linux netdevice method for transmitting a frame.
*
* Arguments:
*	skb	Linux sk_buff containing the frame.
*	netdev	Linux netdevice.
*
* Side effects:
*	If the lower layers report that buffers are full. netdev->tbusy
*	will be set to prevent higher layers from sending more traffic.
*
*	Note: If this function returns non-zero, higher layers retain
*	      ownership of the skb.
*
* Returns:
*	zero on success, non-zero on failure.
----------------------------------------------------------------*/
static int p80211knetdev_hard_start_xmit( struct sk_buff *skb, netdevice_t *netdev)
{
	int		result = 0;
	int		txresult = -1;
	wlandevice_t	*wlandev = (wlandevice_t*)netdev->priv;
	p80211_hdr_t    p80211_hdr;
	p80211_metawep_t p80211_wep;

	DBFENTER;

	if (skb == NULL) {
		return 0;
	}

        if (wlandev->state != WLAN_DEVICE_OPEN) {
		result = 1;
		goto failed;
	}

	memset(&p80211_hdr, 0, sizeof(p80211_hdr_t));
	memset(&p80211_wep, 0, sizeof(p80211_metawep_t));

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,38) )
	if ( test_and_set_bit(0, (void*)&(netdev->tbusy)) != 0 ) {
		/* We've been called w/ tbusy set, has the tx */
		/* path stalled?   */
		WLAN_LOG_DEBUG(1, "called when tbusy set\n");
		result = 1;
		goto failed;
	}
#else
	if ( netif_queue_stopped(netdev) ) {
		WLAN_LOG_DEBUG(1, "called when queue stopped.\n");
		result = 1;
		goto failed;
	}

	netif_stop_queue(netdev);

	/* No timeout handling here, 2.3.38+ kernels call the
	 * timeout function directly.
	 * TODO: Add timeout handling.
	*/
#endif

	/* Check to see that a valid mode is set */
	switch( wlandev->macmode ) {
	case WLAN_MACMODE_IBSS_STA:
	case WLAN_MACMODE_ESS_STA:
	case WLAN_MACMODE_ESS_AP:
		break;
	default:
		/* Mode isn't set yet, just drop the frame
		 * and return success .
		 * TODO: we need a saner way to handle this
		 */
		if(skb->protocol != ETH_P_80211_RAW) {
			p80211netdev_start_queue(wlandev);
			WLAN_LOG_NOTICE(
				"Tx attempt prior to association, frame dropped.\n");
			wlandev->linux_stats.tx_dropped++;
			result = 0;
			goto failed;
		}
		break;
	}

	/* Check for raw transmits */
	if(skb->protocol == ETH_P_80211_RAW) {
		if (!capable(CAP_NET_ADMIN)) {
			result = 1;
			goto failed;
		}
		/* move the header over */
		memcpy(&p80211_hdr, skb->data, sizeof(p80211_hdr_t));
		skb_pull(skb, sizeof(p80211_hdr_t));
	} else {
		if ( skb_ether_to_p80211(wlandev, wlandev->ethconv, skb, &p80211_hdr, &p80211_wep) != 0 ) {
			/* convert failed */
			WLAN_LOG_DEBUG(1, "ether_to_80211(%d) failed.\n",
					wlandev->ethconv);
			result = 1;
			goto failed;
		}
	}
	if ( wlandev->txframe == NULL ) {
		result = 1;
		goto failed;
	}

	netdev->trans_start = jiffies;

	wlandev->linux_stats.tx_packets++;
	/* count only the packet payload */
	wlandev->linux_stats.tx_bytes += skb->len;

	txresult = wlandev->txframe(wlandev, skb, &p80211_hdr, &p80211_wep);

	if ( txresult == 0) {
		/* success and more buf */
		/* avail, re: hw_txdata */
		p80211netdev_wake_queue(wlandev);
		result = 0;
	} else if ( txresult == 1 ) {
		/* success, no more avail */
		WLAN_LOG_DEBUG(3, "txframe success, no more bufs\n");
		/* netdev->tbusy = 1;  don't set here, irqhdlr */
		/*   may have already cleared it */
		result = 0;
	} else if ( txresult == 2 ) {
		/* alloc failure, drop frame */
		WLAN_LOG_DEBUG(3, "txframe returned alloc_fail\n");
		result = 1;
	} else {
		/* buffer full or queue busy, drop frame. */
		WLAN_LOG_DEBUG(3, "txframe returned full or busy\n");
		result = 1;
	}

 failed:
	/* Free up the WEP buffer if it's not the same as the skb */
	if ((p80211_wep.data) && (p80211_wep.data != skb->data))
		kfree(p80211_wep.data);

	/* we always free the skb here, never in a lower level. */
	if (!result)
		dev_kfree_skb(skb);

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* p80211knetdev_set_multicast_list
*
* Called from higher lavers whenever there's a need to set/clear
* promiscuous mode or rewrite the multicast list.
*
* Arguments:
*	none
*
* Returns:
*	nothing
----------------------------------------------------------------*/
static void p80211knetdev_set_multicast_list(netdevice_t *dev)
{
	wlandevice_t	*wlandev = (wlandevice_t*)dev->priv;

	DBFENTER;

	/* TODO:  real multicast support as well */

	if (wlandev->set_multicast_list)
		wlandev->set_multicast_list(wlandev, dev);

	DBFEXIT;
}

#ifdef SIOCETHTOOL

static int p80211netdev_ethtool(wlandevice_t *wlandev, void __user *useraddr)
{
	UINT32 ethcmd;
	struct ethtool_drvinfo info;
	struct ethtool_value edata;

	memset(&info, 0, sizeof(info));
	memset(&edata, 0, sizeof(edata));

	if (copy_from_user(&ethcmd, useraddr, sizeof(ethcmd)))
		return -EFAULT;

	switch (ethcmd) {
	case ETHTOOL_GDRVINFO:
		info.cmd = ethcmd;
		snprintf(info.driver, sizeof(info.driver), "p80211_%s",
			 wlandev->nsdname);
		snprintf(info.version, sizeof(info.version), "%s",
			 WLAN_RELEASE);

		// info.fw_version
		// info.bus_info

		if (copy_to_user(useraddr, &info, sizeof(info)))
			return -EFAULT;
		return 0;
#ifdef ETHTOOL_GLINK
	case ETHTOOL_GLINK:
		edata.cmd = ethcmd;

		if (wlandev->linkstatus &&
		    (wlandev->macmode != WLAN_MACMODE_NONE)) {
			edata.data = 1;
		} else {
			edata.data = 0;
		}

		if (copy_to_user(useraddr, &edata, sizeof(edata)))
                        return -EFAULT;
		return 0;
	}
#endif

	return -EOPNOTSUPP;
}

#endif

/*----------------------------------------------------------------
* p80211knetdev_do_ioctl
*
* Handle an ioctl call on one of our devices.  Everything Linux
* ioctl specific is done here.  Then we pass the contents of the
* ifr->data to the request message handler.
*
* Arguments:
*	dev	Linux kernel netdevice
*	ifr	Our private ioctl request structure, typed for the
*		generic struct ifreq so we can use ptr to func
*		w/o cast.
*
* Returns:
*	zero on success, a negative errno on failure.  Possible values:
*		-ENETDOWN Device isn't up.
*		-EBUSY	cmd already in progress
*		-ETIME	p80211 cmd timed out (MSD may have its own timers)
*		-EFAULT memory fault copying msg from user buffer
*		-ENOMEM unable to allocate kernel msg buffer
*		-ENOSYS	bad magic, it the cmd really for us?
*		-EINTR	sleeping on cmd, awakened by signal, cmd cancelled.
*
* Call Context:
*	Process thread (ioctl caller).  TODO: SMP support may require
*	locks.
----------------------------------------------------------------*/
static int p80211knetdev_do_ioctl(netdevice_t *dev, struct ifreq *ifr, int cmd)
{
	int			result = 0;
	p80211ioctl_req_t	*req = (p80211ioctl_req_t*)ifr;
	wlandevice_t		*wlandev = (wlandevice_t*)dev->priv;
	UINT8			*msgbuf;
	DBFENTER;

	WLAN_LOG_DEBUG(2, "rx'd ioctl, cmd=%d, len=%d\n", cmd, req->len);

#if WIRELESS_EXT < 13
	/* Is this a wireless extensions ioctl? */
	if ((cmd >= SIOCIWFIRST) && (cmd <= SIOCIWLAST)) {
		if ((result = p80211wext_support_ioctl(dev, ifr, cmd))
		    != (-EOPNOTSUPP)) {
			goto bail;
		}
	}
#endif

#ifdef SIOCETHTOOL
	if (cmd == SIOCETHTOOL) {
		result = p80211netdev_ethtool(wlandev, (void __user *) ifr->ifr_data);
		goto bail;
	}
#endif

	/* Test the magic, assume ifr is good if it's there */
	if ( req->magic != P80211_IOCTL_MAGIC ) {
		result = -ENOSYS;
		goto bail;
	}

	if ( cmd == P80211_IFTEST ) {
		result = 0;
		goto bail;
	} else if ( cmd != P80211_IFREQ ) {
		result = -ENOSYS;
		goto bail;
	}

	/* Allocate a buf of size req->len */
	if ((msgbuf = kmalloc( req->len, GFP_KERNEL))) {
		if ( copy_from_user( msgbuf, (void __user *) req->data, req->len) ) {
			result = -EFAULT;
		} else {
			result = p80211req_dorequest( wlandev, msgbuf);
		}

		if ( result == 0 ) {
			if ( copy_to_user( (void __user *) req->data, msgbuf, req->len)) {
				result = -EFAULT;
			}
		}
		kfree(msgbuf);
	} else {
		result = -ENOMEM;
	}
bail:
	DBFEXIT;

	return result; /* If allocate,copyfrom or copyto fails, return errno */
}

/*----------------------------------------------------------------
* p80211knetdev_set_mac_address
*
* Handles the ioctl for changing the MACAddress of a netdevice
*
* references: linux/netdevice.h and drivers/net/net_init.c
*
* NOTE: [MSM] We only prevent address changes when the netdev is
* up.  We don't control anything based on dot11 state.  If the
* address is changed on a STA that's currently associated, you
* will probably lose the ability to send and receive data frames.
* Just be aware.  Therefore, this should usually only be done
* prior to scan/join/auth/assoc.
*
* Arguments:
*	dev	netdevice struct
*	addr	the new MACAddress (a struct)
*
* Returns:
*	zero on success, a negative errno on failure.  Possible values:
*		-EBUSY	device is bussy (cmd not possible)
*		-and errors returned by: p80211req_dorequest(..)
*
* by: Collin R. Mulliner <collin@mulliner.org>
----------------------------------------------------------------*/
static int p80211knetdev_set_mac_address(netdevice_t *dev, void *addr)
{
	struct sockaddr			*new_addr = addr;
	p80211msg_dot11req_mibset_t	dot11req;
	p80211item_unk392_t		*mibattr;
	p80211item_pstr6_t		*macaddr;
	p80211item_uint32_t		*resultcode;
	int result = 0;

	DBFENTER;
	/* If we're running, we don't allow MAC address changes */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,38) )
	if ( dev->start) {
		return -EBUSY;
	}
#else
	if (netif_running(dev)) {
		return -EBUSY;
	}
#endif

	/* Set up some convenience pointers. */
	mibattr = &dot11req.mibattribute;
	macaddr = (p80211item_pstr6_t*)&mibattr->data;
	resultcode = &dot11req.resultcode;

	/* Set up a dot11req_mibset */
	memset(&dot11req, 0, sizeof(p80211msg_dot11req_mibset_t));
	dot11req.msgcode = DIDmsg_dot11req_mibset;
	dot11req.msglen = sizeof(p80211msg_dot11req_mibset_t);
	memcpy(dot11req.devname,
		((wlandevice_t*)(dev->priv))->name,
		WLAN_DEVNAMELEN_MAX - 1);

	/* Set up the mibattribute argument */
	mibattr->did = DIDmsg_dot11req_mibset_mibattribute;
	mibattr->status = P80211ENUM_msgitem_status_data_ok;
	mibattr->len = sizeof(mibattr->data);

	macaddr->did = DIDmib_dot11mac_dot11OperationTable_dot11MACAddress;
	macaddr->status = P80211ENUM_msgitem_status_data_ok;
	macaddr->len = sizeof(macaddr->data);
	macaddr->data.len = WLAN_ADDR_LEN;
	memcpy(&macaddr->data.data, new_addr->sa_data, WLAN_ADDR_LEN);

	/* Set up the resultcode argument */
	resultcode->did = DIDmsg_dot11req_mibset_resultcode;
	resultcode->status = P80211ENUM_msgitem_status_no_value;
	resultcode->len = sizeof(resultcode->data);
	resultcode->data = 0;

	/* now fire the request */
	result = p80211req_dorequest(dev->priv, (UINT8*)&dot11req);

	/* If the request wasn't successful, report an error and don't
	 * change the netdev address
	 */
	if ( result != 0 || resultcode->data != P80211ENUM_resultcode_success) {
		WLAN_LOG_ERROR(
		"Low-level driver failed dot11req_mibset(dot11MACAddress).\n");
		result = -EADDRNOTAVAIL;
	} else {
		/* everything's ok, change the addr in netdev */
		memcpy(dev->dev_addr, new_addr->sa_data, dev->addr_len);
	}

	DBFEXIT;
	return result;
}

static int wlan_change_mtu(netdevice_t *dev, int new_mtu)
{
	DBFENTER;
	// 2312 is max 802.11 payload, 20 is overhead, (ether + llc +snap)
	// and another 8 for wep.
        if ( (new_mtu < 68) || (new_mtu > (2312 - 20 - 8)))
                return -EINVAL;

        dev->mtu = new_mtu;

	DBFEXIT;

        return 0;
}



/*----------------------------------------------------------------
* wlan_setup
*
* Roughly matches the functionality of ether_setup.  Here
* we set up any members of the wlandevice structure that are common
* to all devices.  Additionally, we allocate a linux 'struct device'
* and perform the same setup as ether_setup.
*
* Note: It's important that the caller have setup the wlandev->name
*	ptr prior to calling this function.
*
* Arguments:
*	wlandev		ptr to the wlandev structure for the
*			interface.
* Returns:
*	zero on success, non-zero otherwise.
* Call Context:
*	Should be process thread.  We'll assume it might be
*	interrupt though.  When we add support for statically
*	compiled drivers, this function will be called in the
*	context of the kernel startup code.
----------------------------------------------------------------*/
int wlan_setup(wlandevice_t *wlandev)
{
	int		result = 0;
	netdevice_t	*dev;

	DBFENTER;

	/* Set up the wlandev */
	wlandev->state = WLAN_DEVICE_CLOSED;
	wlandev->ethconv = WLAN_ETHCONV_8021h;
	wlandev->macmode = WLAN_MACMODE_NONE;

	/* Set up the rx queue */
	skb_queue_head_init(&wlandev->nsd_rxq);
	tasklet_init(&wlandev->rx_bh,
		     p80211netdev_rx_bh,
		     (unsigned long)wlandev);

	/* Allocate and initialize the struct device */
	dev = kmalloc(sizeof(netdevice_t), GFP_ATOMIC);
	if ( dev == NULL ) {
		WLAN_LOG_ERROR("Failed to alloc netdev.\n");
		result = 1;
	} else {
		memset( dev, 0, sizeof(netdevice_t));
		ether_setup(dev);
		wlandev->netdev = dev;
		dev->priv = wlandev;
		dev->hard_start_xmit =	p80211knetdev_hard_start_xmit;
		dev->get_stats =	p80211knetdev_get_stats;
#ifdef HAVE_PRIVATE_IOCTL
		dev->do_ioctl = 	p80211knetdev_do_ioctl;
#endif
#ifdef HAVE_MULTICAST
		dev->set_multicast_list = p80211knetdev_set_multicast_list;
#endif
		dev->init =		p80211knetdev_init;
		dev->open =		p80211knetdev_open;
		dev->stop =		p80211knetdev_stop;

#ifdef CONFIG_NET_WIRELESS
#if ((WIRELESS_EXT < 17) && (WIRELESS_EXT < 21))
		dev->get_wireless_stats = p80211wext_get_wireless_stats;
#endif
#if WIRELESS_EXT > 12
		dev->wireless_handlers = &p80211wext_handler_def;
#endif
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,38) )
		dev->tbusy = 1;
		dev->start = 0;
#else
		netif_stop_queue(dev);
#endif
#ifdef HAVE_CHANGE_MTU
		dev->change_mtu = wlan_change_mtu;
#endif
#ifdef HAVE_SET_MAC_ADDR
		dev->set_mac_address =	p80211knetdev_set_mac_address;
#endif
#ifdef HAVE_TX_TIMEOUT
		dev->tx_timeout      =  &p80211knetdev_tx_timeout;
		dev->watchdog_timeo  =  (wlan_watchdog * HZ) / 1000;
#endif
		netif_carrier_off(dev);
	}

	DBFEXIT;
	return result;
}

/*----------------------------------------------------------------
* wlan_unsetup
*
* This function is paired with the wlan_setup routine.  It should
* be called after unregister_wlandev.  Basically, all it does is
* free the 'struct device' that's associated with the wlandev.
* We do it here because the 'struct device' isn't allocated
* explicitly in the driver code, it's done in wlan_setup.  To
* do the free in the driver might seem like 'magic'.
*
* Arguments:
*	wlandev		ptr to the wlandev structure for the
*			interface.
* Returns:
*	zero on success, non-zero otherwise.
* Call Context:
*	Should be process thread.  We'll assume it might be
*	interrupt though.  When we add support for statically
*	compiled drivers, this function will be called in the
*	context of the kernel startup code.
----------------------------------------------------------------*/
int wlan_unsetup(wlandevice_t *wlandev)
{
	int		result = 0;

	DBFENTER;

	tasklet_kill(&wlandev->rx_bh);

	if (wlandev->netdev == NULL ) {
		WLAN_LOG_ERROR("called without wlandev->netdev set.\n");
		result = 1;
	} else {
		free_netdev(wlandev->netdev);
		wlandev->netdev = NULL;
	}

	DBFEXIT;
	return 0;
}



/*----------------------------------------------------------------
* register_wlandev
*
* Roughly matches the functionality of register_netdev.  This function
* is called after the driver has successfully probed and set up the
* resources for the device.  It's now ready to become a named device
* in the Linux system.
*
* First we allocate a name for the device (if not already set), then
* we call the Linux function register_netdevice.
*
* Arguments:
*	wlandev		ptr to the wlandev structure for the
*			interface.
* Returns:
*	zero on success, non-zero otherwise.
* Call Context:
*	Can be either interrupt or not.
----------------------------------------------------------------*/
int register_wlandev(wlandevice_t *wlandev)
{
	int		i = 0;
	netdevice_t	*dev = wlandev->netdev;

	DBFENTER;

	i = dev_alloc_name(wlandev->netdev, "wlan%d");
	if (i >= 0) {
		i = register_netdev(wlandev->netdev);
	}
	if (i != 0) {
		return -EIO;
	}

#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0) )
	dev->name = wlandev->name;
#else
	strcpy(wlandev->name, dev->name);
#endif

#ifdef CONFIG_PROC_FS
	if (proc_p80211) {
		wlandev->procdir = proc_mkdir(wlandev->name, proc_p80211);
		if ( wlandev->procdir )
			wlandev->procwlandev =
				create_proc_read_entry("wlandev", 0,
						       wlandev->procdir,
						       p80211netdev_proc_read,
						       wlandev);
		if (wlandev->nsd_proc_read)
			create_proc_read_entry("nsd", 0,
					       wlandev->procdir,
					       wlandev->nsd_proc_read,
					       wlandev);
	}
#endif

#ifdef CONFIG_HOTPLUG
	p80211_run_sbin_hotplug(wlandev, WLAN_HOTPLUG_REGISTER);
#endif

	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* unregister_wlandev
*
* Roughly matches the functionality of unregister_netdev.  This
* function is called to remove a named device from the system.
*
* First we tell linux that the device should no longer exist.
* Then we remove it from the list of known wlan devices.
*
* Arguments:
*	wlandev		ptr to the wlandev structure for the
*			interface.
* Returns:
*	zero on success, non-zero otherwise.
* Call Context:
*	Can be either interrupt or not.
----------------------------------------------------------------*/
int unregister_wlandev(wlandevice_t *wlandev)
{
	struct sk_buff *skb;

	DBFENTER;

#ifdef CONFIG_HOTPLUG
	p80211_run_sbin_hotplug(wlandev, WLAN_HOTPLUG_REMOVE);
#endif

#ifdef CONFIG_PROC_FS
	if ( wlandev->procwlandev ) {
		remove_proc_entry("wlandev", wlandev->procdir);
	}
	if ( wlandev->nsd_proc_read ) {
		remove_proc_entry("nsd", wlandev->procdir);
	}
	if (wlandev->procdir) {
		remove_proc_entry(wlandev->name, proc_p80211);
	}
#endif

	unregister_netdev(wlandev->netdev);

	/* Now to clean out the rx queue */
	while ( (skb = skb_dequeue(&wlandev->nsd_rxq)) ) {
		dev_kfree_skb(skb);
	}

	DBFEXIT;
	return 0;
}

#ifdef CONFIG_PROC_FS
/*----------------------------------------------------------------
* proc_read
*
* Read function for /proc/net/p80211/<device>/wlandev
*
* Arguments:
*	buf
*	start
*	offset
*	count
*	eof
*	data
* Returns:
*	zero on success, non-zero otherwise.
* Call Context:
*	Can be either interrupt or not.
----------------------------------------------------------------*/
static int
p80211netdev_proc_read(
	char	*page,
	char	**start,
	off_t	offset,
	int	count,
	int	*eof,
	void	*data)
{
	char	 *p = page;
	wlandevice_t *wlandev = (wlandevice_t *) data;

	DBFENTER;
	if (offset != 0) {
		*eof = 1;
		goto exit;
	}

	p += sprintf(p, "p80211 version: %s (%s)\n\n",
		     WLAN_RELEASE, WLAN_BUILD_DATE);
	p += sprintf(p, "name       : %s\n", wlandev->name);
	p += sprintf(p, "nsd name   : %s\n", wlandev->nsdname);
	p += sprintf(p, "address    : %02x:%02x:%02x:%02x:%02x:%02x\n",
		     wlandev->netdev->dev_addr[0], wlandev->netdev->dev_addr[1], wlandev->netdev->dev_addr[2],
		     wlandev->netdev->dev_addr[3], wlandev->netdev->dev_addr[4], wlandev->netdev->dev_addr[5]);
	p += sprintf(p, "nsd caps   : %s%s%s%s%s%s%s%s%s%s\n",
		     (wlandev->nsdcaps & P80211_NSDCAP_HARDWAREWEP) ? "wep_hw " : "",
		     (wlandev->nsdcaps & P80211_NSDCAP_TIEDWEP) ? "wep_tied " : "",
		     (wlandev->nsdcaps & P80211_NSDCAP_NOHOSTWEP) ? "wep_hw_only " : "",
		     (wlandev->nsdcaps & P80211_NSDCAP_PBCC) ? "pbcc " : "",
		     (wlandev->nsdcaps & P80211_NSDCAP_SHORT_PREAMBLE) ? "short_preamble " : "",
		     (wlandev->nsdcaps & P80211_NSDCAP_AGILITY) ? "agility " : "",
		     (wlandev->nsdcaps & P80211_NSDCAP_AP_RETRANSMIT) ? "ap_retransmit " : "",
		     (wlandev->nsdcaps & P80211_NSDCAP_HWFRAGMENT) ? "hw_frag " : "",
		     (wlandev->nsdcaps & P80211_NSDCAP_AUTOJOIN) ? "autojoin " : "",
		     (wlandev->nsdcaps & P80211_NSDCAP_NOSCAN) ? "" : "scan ");


	p += sprintf(p, "bssid      : %02x:%02x:%02x:%02x:%02x:%02x\n",
		     wlandev->bssid[0], wlandev->bssid[1], wlandev->bssid[2],
		     wlandev->bssid[3], wlandev->bssid[4], wlandev->bssid[5]);

	p += sprintf(p, "Enabled    : %s%s\n",
		     (wlandev->shortpreamble) ? "short_preamble " : "",
		     (wlandev->hostwep & HOSTWEP_PRIVACYINVOKED) ? "privacy" : "");


 exit:
	DBFEXIT;
	return (p - page);
}
#endif

/*----------------------------------------------------------------
* p80211netdev_hwremoved
*
* Hardware removed notification. This function should be called
* immediately after an MSD has detected that the underlying hardware
* has been yanked out from under us.  The primary things we need
* to do are:
*   - Mark the wlandev
*   - Prevent any further traffic from the knetdev i/f
*   - Prevent any further requests from mgmt i/f
*   - If there are any waitq'd mgmt requests or mgmt-frame exchanges,
*     shut them down.
*   - Call the MSD hwremoved function.
*
* The remainder of the cleanup will be handled by unregister().
* Our primary goal here is to prevent as much tickling of the MSD
* as possible since the MSD is already in a 'wounded' state.
*
* TODO: As new features are added, this function should be
*       updated.
*
* Arguments:
*	wlandev		WLAN network device structure
* Returns:
*	nothing
* Side effects:
*
* Call context:
*	Usually interrupt.
----------------------------------------------------------------*/
void p80211netdev_hwremoved(wlandevice_t *wlandev)
{
	DBFENTER;
	wlandev->hwremoved = 1;
	if ( wlandev->state == WLAN_DEVICE_OPEN) {
		p80211netdev_stop_queue(wlandev);
	}

	netif_device_detach(wlandev->netdev);

	DBFEXIT;
}


/*----------------------------------------------------------------
* p80211_rx_typedrop
*
* Classifies the frame, increments the appropriate counter, and
* returns 0|1|2 indicating whether the driver should handle, ignore, or
* drop the frame
*
* Arguments:
*	wlandev		wlan device structure
*	fc		frame control field
*
* Returns:
*	zero if the frame should be handled by the driver,
*       one if the frame should be ignored
*       anything else means we drop it.
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
static int p80211_rx_typedrop( wlandevice_t *wlandev, UINT16 fc)
{
	UINT16	ftype;
	UINT16	fstype;
	int	drop = 0;
	/* Classify frame, increment counter */
	ftype = WLAN_GET_FC_FTYPE(fc);
	fstype = WLAN_GET_FC_FSTYPE(fc);
#if 0
	WLAN_LOG_DEBUG(4,
		"rx_typedrop : ftype=%d fstype=%d.\n", ftype, fstype);
#endif
	switch ( ftype ) {
	case WLAN_FTYPE_MGMT:
		if ((wlandev->netdev->flags & IFF_PROMISC) ||
			(wlandev->netdev->flags & IFF_ALLMULTI)) {
			drop = 1;
			break;
		}
		WLAN_LOG_DEBUG(3, "rx'd mgmt:\n");
		wlandev->rx.mgmt++;
		switch( fstype ) {
		case WLAN_FSTYPE_ASSOCREQ:
			/* printk("assocreq"); */
			wlandev->rx.assocreq++;
			break;
		case WLAN_FSTYPE_ASSOCRESP:
			/* printk("assocresp"); */
			wlandev->rx.assocresp++;
			break;
		case WLAN_FSTYPE_REASSOCREQ:
			/* printk("reassocreq"); */
			wlandev->rx.reassocreq++;
			break;
		case WLAN_FSTYPE_REASSOCRESP:
			/* printk("reassocresp"); */
			wlandev->rx.reassocresp++;
			break;
		case WLAN_FSTYPE_PROBEREQ:
			/* printk("probereq"); */
			wlandev->rx.probereq++;
			break;
		case WLAN_FSTYPE_PROBERESP:
			/* printk("proberesp"); */
			wlandev->rx.proberesp++;
			break;
		case WLAN_FSTYPE_BEACON:
			/* printk("beacon"); */
			wlandev->rx.beacon++;
			break;
		case WLAN_FSTYPE_ATIM:
			/* printk("atim"); */
			wlandev->rx.atim++;
			break;
		case WLAN_FSTYPE_DISASSOC:
			/* printk("disassoc"); */
			wlandev->rx.disassoc++;
			break;
		case WLAN_FSTYPE_AUTHEN:
			/* printk("authen"); */
			wlandev->rx.authen++;
			break;
		case WLAN_FSTYPE_DEAUTHEN:
			/* printk("deauthen"); */
			wlandev->rx.deauthen++;
			break;
		default:
			/* printk("unknown"); */
			wlandev->rx.mgmt_unknown++;
			break;
		}
		/* printk("\n"); */
		drop = 2;
		break;

	case WLAN_FTYPE_CTL:
		if ((wlandev->netdev->flags & IFF_PROMISC) ||
			(wlandev->netdev->flags & IFF_ALLMULTI)) {
			drop = 1;
			break;
		}
		WLAN_LOG_DEBUG(3, "rx'd ctl:\n");
		wlandev->rx.ctl++;
		switch( fstype ) {
		case WLAN_FSTYPE_PSPOLL:
			/* printk("pspoll"); */
			wlandev->rx.pspoll++;
			break;
		case WLAN_FSTYPE_RTS:
			/* printk("rts"); */
			wlandev->rx.rts++;
			break;
		case WLAN_FSTYPE_CTS:
			/* printk("cts"); */
			wlandev->rx.cts++;
			break;
		case WLAN_FSTYPE_ACK:
			/* printk("ack"); */
			wlandev->rx.ack++;
			break;
		case WLAN_FSTYPE_CFEND:
			/* printk("cfend"); */
			wlandev->rx.cfend++;
			break;
		case WLAN_FSTYPE_CFENDCFACK:
			/* printk("cfendcfack"); */
			wlandev->rx.cfendcfack++;
			break;
		default:
			/* printk("unknown"); */
			wlandev->rx.ctl_unknown++;
			break;
		}
		/* printk("\n"); */
		drop = 2;
		break;

	case WLAN_FTYPE_DATA:
		wlandev->rx.data++;
		switch( fstype ) {
		case WLAN_FSTYPE_DATAONLY:
			wlandev->rx.dataonly++;
			break;
		case WLAN_FSTYPE_DATA_CFACK:
			wlandev->rx.data_cfack++;
			break;
		case WLAN_FSTYPE_DATA_CFPOLL:
			wlandev->rx.data_cfpoll++;
			break;
		case WLAN_FSTYPE_DATA_CFACK_CFPOLL:
			wlandev->rx.data__cfack_cfpoll++;
			break;
		case WLAN_FSTYPE_NULL:
			WLAN_LOG_DEBUG(3, "rx'd data:null\n");
			wlandev->rx.null++;
			break;
		case WLAN_FSTYPE_CFACK:
			WLAN_LOG_DEBUG(3, "rx'd data:cfack\n");
			wlandev->rx.cfack++;
			break;
		case WLAN_FSTYPE_CFPOLL:
			WLAN_LOG_DEBUG(3, "rx'd data:cfpoll\n");
			wlandev->rx.cfpoll++;
			break;
		case WLAN_FSTYPE_CFACK_CFPOLL:
			WLAN_LOG_DEBUG(3, "rx'd data:cfack_cfpoll\n");
			wlandev->rx.cfack_cfpoll++;
			break;
		default:
			/* printk("unknown"); */
			wlandev->rx.data_unknown++;
			break;
		}

		break;
	}
	return drop;
}

#ifdef CONFIG_HOTPLUG
/* Notify userspace when a netdevice event occurs,
 * by running '/sbin/hotplug net' with certain
 * environment variables set.
 */
int p80211_run_sbin_hotplug(wlandevice_t *wlandev, char *action)
{
        char *argv[3], *envp[7], ifname[12 + IFNAMSIZ], action_str[32];
	char nsdname[32], wlan_wext[32];
        int i;

	if (wlandev) {
		sprintf(ifname, "INTERFACE=%s", wlandev->name);
		sprintf(nsdname, "NSDNAME=%s", wlandev->nsdname);
	} else {
		sprintf(ifname, "INTERFACE=null");
		sprintf(nsdname, "NSDNAME=null");
	}

	sprintf(wlan_wext, "WLAN_WEXT=%s", wlan_wext_write ? "y" : "");
        sprintf(action_str, "ACTION=%s", action);

        i = 0;
        argv[i++] = hotplug_path;
        argv[i++] = "wlan";
        argv[i] = NULL;

        i = 0;
        /* minimal command environment */
        envp [i++] = "HOME=/";
        envp [i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
        envp [i++] = ifname;
        envp [i++] = action_str;
        envp [i++] = nsdname;
        envp [i++] = wlan_wext;
        envp [i] = NULL;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,62))
        return call_usermodehelper(argv [0], argv, envp);
#else
        return call_usermodehelper(argv [0], argv, envp, 0);
#endif
}

#endif


void    p80211_suspend(wlandevice_t *wlandev)
{
	DBFENTER;

#ifdef CONFIG_HOTPLUG
	p80211_run_sbin_hotplug(wlandev, WLAN_HOTPLUG_SUSPEND);
#endif

	DBFEXIT;
}

void    p80211_resume(wlandevice_t *wlandev)
{
	DBFENTER;

#ifdef CONFIG_HOTPLUG
	p80211_run_sbin_hotplug(wlandev, WLAN_HOTPLUG_RESUME);
#endif

	DBFEXIT;
}

static void p80211knetdev_tx_timeout( netdevice_t *netdev)
{
	wlandevice_t	*wlandev = (wlandevice_t*)netdev->priv;
	DBFENTER;

	if (wlandev->tx_timeout) {
		wlandev->tx_timeout(wlandev);
	} else {
		WLAN_LOG_WARNING("Implement tx_timeout for %s\n",
				 wlandev->nsdname);
		p80211netdev_wake_queue(wlandev);
	}

	DBFEXIT;
}
