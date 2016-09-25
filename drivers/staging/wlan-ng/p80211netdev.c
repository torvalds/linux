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
#include <linux/if_ether.h>
#include <linux/byteorder/generic.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <asm/byteorder.h>

#ifdef SIOCETHTOOL
#include <linux/ethtool.h>
#endif

#include <net/iw_handler.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>

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

#include "cfg80211.c"

/* netdevice method functions */
static int p80211knetdev_init(netdevice_t *netdev);
static int p80211knetdev_open(netdevice_t *netdev);
static int p80211knetdev_stop(netdevice_t *netdev);
static int p80211knetdev_hard_start_xmit(struct sk_buff *skb,
					 netdevice_t *netdev);
static void p80211knetdev_set_multicast_list(netdevice_t *dev);
static int p80211knetdev_do_ioctl(netdevice_t *dev, struct ifreq *ifr,
				  int cmd);
static int p80211knetdev_set_mac_address(netdevice_t *dev, void *addr);
static void p80211knetdev_tx_timeout(netdevice_t *netdev);
static int p80211_rx_typedrop(struct wlandevice *wlandev, u16 fc);

int wlan_watchdog = 5000;
module_param(wlan_watchdog, int, 0644);
MODULE_PARM_DESC(wlan_watchdog, "transmit timeout in milliseconds");

int wlan_wext_write = 1;
module_param(wlan_wext_write, int, 0644);
MODULE_PARM_DESC(wlan_wext_write, "enable write wireless extensions");

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
static int p80211knetdev_init(netdevice_t *netdev)
{
	/* Called in response to register_netdev */
	/* This is usually the probe function, but the probe has */
	/* already been done by the MSD and the create_kdev */
	/* function.  All we do here is return success */
	return 0;
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
static int p80211knetdev_open(netdevice_t *netdev)
{
	int result = 0;		/* success */
	struct wlandevice *wlandev = netdev->ml_priv;

	/* Check to make sure the MSD is running */
	if (wlandev->msdstate != WLAN_MSD_RUNNING)
		return -ENODEV;

	/* Tell the MSD to open */
	if (wlandev->open) {
		result = wlandev->open(wlandev);
		if (result == 0) {
			netif_start_queue(wlandev->netdev);
			wlandev->state = WLAN_DEVICE_OPEN;
		}
	} else {
		result = -EAGAIN;
	}

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
static int p80211knetdev_stop(netdevice_t *netdev)
{
	int result = 0;
	struct wlandevice *wlandev = netdev->ml_priv;

	if (wlandev->close)
		result = wlandev->close(wlandev);

	netif_stop_queue(wlandev->netdev);
	wlandev->state = WLAN_DEVICE_CLOSED;

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
void p80211netdev_rx(struct wlandevice *wlandev, struct sk_buff *skb)
{
	/* Enqueue for post-irq processing */
	skb_queue_tail(&wlandev->nsd_rxq, skb);
	tasklet_schedule(&wlandev->rx_bh);
}

#define CONV_TO_ETHER_SKIPPED	0x01
#define CONV_TO_ETHER_FAILED	0x02

/**
 * p80211_convert_to_ether - conversion from 802.11 frame to ethernet frame
 * @wlandev: pointer to WLAN device
 * @skb: pointer to socket buffer
 *
 * Returns: 0 if conversion succeeded
 *	    CONV_TO_ETHER_FAILED if conversion failed
 *	    CONV_TO_ETHER_SKIPPED if frame is ignored
 */
static int p80211_convert_to_ether(struct wlandevice *wlandev, struct sk_buff *skb)
{
	struct p80211_hdr_a3 *hdr;

	hdr = (struct p80211_hdr_a3 *)skb->data;
	if (p80211_rx_typedrop(wlandev, hdr->fc))
		return CONV_TO_ETHER_SKIPPED;

	/* perform mcast filtering: allow my local address through but reject
	 * anything else that isn't multicast
	 */
	if (wlandev->netdev->flags & IFF_ALLMULTI) {
		if (!ether_addr_equal_unaligned(wlandev->netdev->dev_addr,
						hdr->a1)) {
			if (!is_multicast_ether_addr(hdr->a1))
				return CONV_TO_ETHER_SKIPPED;
		}
	}

	if (skb_p80211_to_ether(wlandev, wlandev->ethconv, skb) == 0) {
		skb->dev->last_rx = jiffies;
		wlandev->netdev->stats.rx_packets++;
		wlandev->netdev->stats.rx_bytes += skb->len;
		netif_rx_ni(skb);
		return 0;
	}

	netdev_dbg(wlandev->netdev, "p80211_convert_to_ether failed.\n");
	return CONV_TO_ETHER_FAILED;
}

/**
 * p80211netdev_rx_bh - deferred processing of all received frames
 *
 * @arg: pointer to WLAN network device structure (cast to unsigned long)
 */
static void p80211netdev_rx_bh(unsigned long arg)
{
	struct wlandevice *wlandev = (struct wlandevice *)arg;
	struct sk_buff *skb = NULL;
	netdevice_t *dev = wlandev->netdev;

	/* Let's empty our our queue */
	while ((skb = skb_dequeue(&wlandev->nsd_rxq))) {
		if (wlandev->state == WLAN_DEVICE_OPEN) {

			if (dev->type != ARPHRD_ETHER) {
				/* RAW frame; we shouldn't convert it */
				/* XXX Append the Prism Header here instead. */

				/* set up various data fields */
				skb->dev = dev;
				skb_reset_mac_header(skb);
				skb->ip_summed = CHECKSUM_NONE;
				skb->pkt_type = PACKET_OTHERHOST;
				skb->protocol = htons(ETH_P_80211_RAW);
				dev->last_rx = jiffies;

				dev->stats.rx_packets++;
				dev->stats.rx_bytes += skb->len;
				netif_rx_ni(skb);
				continue;
			} else {
				if (!p80211_convert_to_ether(wlandev, skb))
					continue;
			}
		}
		dev_kfree_skb(skb);
	}
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
static int p80211knetdev_hard_start_xmit(struct sk_buff *skb,
					 netdevice_t *netdev)
{
	int result = 0;
	int txresult = -1;
	struct wlandevice *wlandev = netdev->ml_priv;
	union p80211_hdr p80211_hdr;
	struct p80211_metawep p80211_wep;

	p80211_wep.data = NULL;

	if (!skb)
		return NETDEV_TX_OK;

	if (wlandev->state != WLAN_DEVICE_OPEN) {
		result = 1;
		goto failed;
	}

	memset(&p80211_hdr, 0, sizeof(union p80211_hdr));
	memset(&p80211_wep, 0, sizeof(struct p80211_metawep));

	if (netif_queue_stopped(netdev)) {
		netdev_dbg(netdev, "called when queue stopped.\n");
		result = 1;
		goto failed;
	}

	netif_stop_queue(netdev);

	/* Check to see that a valid mode is set */
	switch (wlandev->macmode) {
	case WLAN_MACMODE_IBSS_STA:
	case WLAN_MACMODE_ESS_STA:
	case WLAN_MACMODE_ESS_AP:
		break;
	default:
		/* Mode isn't set yet, just drop the frame
		 * and return success .
		 * TODO: we need a saner way to handle this
		 */
		if (be16_to_cpu(skb->protocol) != ETH_P_80211_RAW) {
			netif_start_queue(wlandev->netdev);
			netdev_notice(netdev, "Tx attempt prior to association, frame dropped.\n");
			netdev->stats.tx_dropped++;
			result = 0;
			goto failed;
		}
		break;
	}

	/* Check for raw transmits */
	if (be16_to_cpu(skb->protocol) == ETH_P_80211_RAW) {
		if (!capable(CAP_NET_ADMIN)) {
			result = 1;
			goto failed;
		}
		/* move the header over */
		memcpy(&p80211_hdr, skb->data, sizeof(union p80211_hdr));
		skb_pull(skb, sizeof(union p80211_hdr));
	} else {
		if (skb_ether_to_p80211
		    (wlandev, wlandev->ethconv, skb, &p80211_hdr,
		     &p80211_wep) != 0) {
			/* convert failed */
			netdev_dbg(netdev, "ether_to_80211(%d) failed.\n",
				   wlandev->ethconv);
			result = 1;
			goto failed;
		}
	}
	if (!wlandev->txframe) {
		result = 1;
		goto failed;
	}

	netif_trans_update(netdev);

	netdev->stats.tx_packets++;
	/* count only the packet payload */
	netdev->stats.tx_bytes += skb->len;

	txresult = wlandev->txframe(wlandev, skb, &p80211_hdr, &p80211_wep);

	if (txresult == 0) {
		/* success and more buf */
		/* avail, re: hw_txdata */
		netif_wake_queue(wlandev->netdev);
		result = NETDEV_TX_OK;
	} else if (txresult == 1) {
		/* success, no more avail */
		netdev_dbg(netdev, "txframe success, no more bufs\n");
		/* netdev->tbusy = 1;  don't set here, irqhdlr */
		/*   may have already cleared it */
		result = NETDEV_TX_OK;
	} else if (txresult == 2) {
		/* alloc failure, drop frame */
		netdev_dbg(netdev, "txframe returned alloc_fail\n");
		result = NETDEV_TX_BUSY;
	} else {
		/* buffer full or queue busy, drop frame. */
		netdev_dbg(netdev, "txframe returned full or busy\n");
		result = NETDEV_TX_BUSY;
	}

failed:
	/* Free up the WEP buffer if it's not the same as the skb */
	if ((p80211_wep.data) && (p80211_wep.data != skb->data))
		kzfree(p80211_wep.data);

	/* we always free the skb here, never in a lower level. */
	if (!result)
		dev_kfree_skb(skb);

	return result;
}

/*----------------------------------------------------------------
* p80211knetdev_set_multicast_list
*
* Called from higher layers whenever there's a need to set/clear
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
	struct wlandevice *wlandev = dev->ml_priv;

	/* TODO:  real multicast support as well */

	if (wlandev->set_multicast_list)
		wlandev->set_multicast_list(wlandev, dev);

}

#ifdef SIOCETHTOOL

static int p80211netdev_ethtool(struct wlandevice *wlandev, void __user *useraddr)
{
	u32 ethcmd;
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
#endif
	}

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
*		-EintR	sleeping on cmd, awakened by signal, cmd cancelled.
*
* Call Context:
*	Process thread (ioctl caller).  TODO: SMP support may require
*	locks.
----------------------------------------------------------------*/
static int p80211knetdev_do_ioctl(netdevice_t *dev, struct ifreq *ifr, int cmd)
{
	int result = 0;
	struct p80211ioctl_req *req = (struct p80211ioctl_req *)ifr;
	struct wlandevice *wlandev = dev->ml_priv;
	u8 *msgbuf;

	netdev_dbg(dev, "rx'd ioctl, cmd=%d, len=%d\n", cmd, req->len);

#ifdef SIOCETHTOOL
	if (cmd == SIOCETHTOOL) {
		result =
		    p80211netdev_ethtool(wlandev, (void __user *)ifr->ifr_data);
		goto bail;
	}
#endif

	/* Test the magic, assume ifr is good if it's there */
	if (req->magic != P80211_IOCTL_MAGIC) {
		result = -ENOSYS;
		goto bail;
	}

	if (cmd == P80211_IFTEST) {
		result = 0;
		goto bail;
	} else if (cmd != P80211_IFREQ) {
		result = -ENOSYS;
		goto bail;
	}

	/* Allocate a buf of size req->len */
	msgbuf = kmalloc(req->len, GFP_KERNEL);
	if (msgbuf) {
		if (copy_from_user(msgbuf, (void __user *)req->data, req->len))
			result = -EFAULT;
		else
			result = p80211req_dorequest(wlandev, msgbuf);

		if (result == 0) {
			if (copy_to_user
			    ((void __user *)req->data, msgbuf, req->len)) {
				result = -EFAULT;
			}
		}
		kfree(msgbuf);
	} else {
		result = -ENOMEM;
	}
bail:
	/* If allocate,copyfrom or copyto fails, return errno */
	return result;
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
	struct sockaddr *new_addr = addr;
	struct p80211msg_dot11req_mibset dot11req;
	p80211item_unk392_t *mibattr;
	struct p80211item_pstr6 *macaddr;
	struct p80211item_uint32 *resultcode;
	int result;

	/* If we're running, we don't allow MAC address changes */
	if (netif_running(dev))
		return -EBUSY;

	/* Set up some convenience pointers. */
	mibattr = &dot11req.mibattribute;
	macaddr = (struct p80211item_pstr6 *)&mibattr->data;
	resultcode = &dot11req.resultcode;

	/* Set up a dot11req_mibset */
	memset(&dot11req, 0, sizeof(struct p80211msg_dot11req_mibset));
	dot11req.msgcode = DIDmsg_dot11req_mibset;
	dot11req.msglen = sizeof(struct p80211msg_dot11req_mibset);
	memcpy(dot11req.devname,
	       ((struct wlandevice *)dev->ml_priv)->name, WLAN_DEVNAMELEN_MAX - 1);

	/* Set up the mibattribute argument */
	mibattr->did = DIDmsg_dot11req_mibset_mibattribute;
	mibattr->status = P80211ENUM_msgitem_status_data_ok;
	mibattr->len = sizeof(mibattr->data);

	macaddr->did = DIDmib_dot11mac_dot11OperationTable_dot11MACAddress;
	macaddr->status = P80211ENUM_msgitem_status_data_ok;
	macaddr->len = sizeof(macaddr->data);
	macaddr->data.len = ETH_ALEN;
	memcpy(&macaddr->data.data, new_addr->sa_data, ETH_ALEN);

	/* Set up the resultcode argument */
	resultcode->did = DIDmsg_dot11req_mibset_resultcode;
	resultcode->status = P80211ENUM_msgitem_status_no_value;
	resultcode->len = sizeof(resultcode->data);
	resultcode->data = 0;

	/* now fire the request */
	result = p80211req_dorequest(dev->ml_priv, (u8 *)&dot11req);

	/* If the request wasn't successful, report an error and don't
	 * change the netdev address
	 */
	if (result != 0 || resultcode->data != P80211ENUM_resultcode_success) {
		netdev_err(dev, "Low-level driver failed dot11req_mibset(dot11MACAddress).\n");
		result = -EADDRNOTAVAIL;
	} else {
		/* everything's ok, change the addr in netdev */
		memcpy(dev->dev_addr, new_addr->sa_data, dev->addr_len);
	}

	return result;
}

static int wlan_change_mtu(netdevice_t *dev, int new_mtu)
{
	/* 2312 is max 802.11 payload, 20 is overhead, (ether + llc +snap)
	   and another 8 for wep. */
	if ((new_mtu < 68) || (new_mtu > (2312 - 20 - 8)))
		return -EINVAL;

	dev->mtu = new_mtu;

	return 0;
}

static const struct net_device_ops p80211_netdev_ops = {
	.ndo_init = p80211knetdev_init,
	.ndo_open = p80211knetdev_open,
	.ndo_stop = p80211knetdev_stop,
	.ndo_start_xmit = p80211knetdev_hard_start_xmit,
	.ndo_set_rx_mode = p80211knetdev_set_multicast_list,
	.ndo_do_ioctl = p80211knetdev_do_ioctl,
	.ndo_set_mac_address = p80211knetdev_set_mac_address,
	.ndo_tx_timeout = p80211knetdev_tx_timeout,
	.ndo_change_mtu = wlan_change_mtu,
	.ndo_validate_addr = eth_validate_addr,
};

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
*	physdev		ptr to usb device
* Returns:
*	zero on success, non-zero otherwise.
* Call Context:
*	Should be process thread.  We'll assume it might be
*	interrupt though.  When we add support for statically
*	compiled drivers, this function will be called in the
*	context of the kernel startup code.
----------------------------------------------------------------*/
int wlan_setup(struct wlandevice *wlandev, struct device *physdev)
{
	int result = 0;
	netdevice_t *netdev;
	struct wiphy *wiphy;
	struct wireless_dev *wdev;

	/* Set up the wlandev */
	wlandev->state = WLAN_DEVICE_CLOSED;
	wlandev->ethconv = WLAN_ETHCONV_8021h;
	wlandev->macmode = WLAN_MACMODE_NONE;

	/* Set up the rx queue */
	skb_queue_head_init(&wlandev->nsd_rxq);
	tasklet_init(&wlandev->rx_bh,
		     p80211netdev_rx_bh, (unsigned long)wlandev);

	/* Allocate and initialize the wiphy struct */
	wiphy = wlan_create_wiphy(physdev, wlandev);
	if (!wiphy) {
		dev_err(physdev, "Failed to alloc wiphy.\n");
		return 1;
	}

	/* Allocate and initialize the struct device */
	netdev = alloc_netdev(sizeof(struct wireless_dev), "wlan%d",
			      NET_NAME_UNKNOWN, ether_setup);
	if (!netdev) {
		dev_err(physdev, "Failed to alloc netdev.\n");
		wlan_free_wiphy(wiphy);
		result = 1;
	} else {
		wlandev->netdev = netdev;
		netdev->ml_priv = wlandev;
		netdev->netdev_ops = &p80211_netdev_ops;
		wdev = netdev_priv(netdev);
		wdev->wiphy = wiphy;
		wdev->iftype = NL80211_IFTYPE_STATION;
		netdev->ieee80211_ptr = wdev;

		netif_stop_queue(netdev);
		netif_carrier_off(netdev);
	}

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
* Call Context:
*	Should be process thread.  We'll assume it might be
*	interrupt though.  When we add support for statically
*	compiled drivers, this function will be called in the
*	context of the kernel startup code.
----------------------------------------------------------------*/
void wlan_unsetup(struct wlandevice *wlandev)
{
	struct wireless_dev *wdev;

	tasklet_kill(&wlandev->rx_bh);

	if (wlandev->netdev) {
		wdev = netdev_priv(wlandev->netdev);
		if (wdev->wiphy)
			wlan_free_wiphy(wdev->wiphy);
		free_netdev(wlandev->netdev);
		wlandev->netdev = NULL;
	}
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
int register_wlandev(struct wlandevice *wlandev)
{
	return register_netdev(wlandev->netdev);
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
int unregister_wlandev(struct wlandevice *wlandev)
{
	struct sk_buff *skb;

	unregister_netdev(wlandev->netdev);

	/* Now to clean out the rx queue */
	while ((skb = skb_dequeue(&wlandev->nsd_rxq)))
		dev_kfree_skb(skb);

	return 0;
}

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
void p80211netdev_hwremoved(struct wlandevice *wlandev)
{
	wlandev->hwremoved = 1;
	if (wlandev->state == WLAN_DEVICE_OPEN)
		netif_stop_queue(wlandev->netdev);

	netif_device_detach(wlandev->netdev);
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
static int p80211_rx_typedrop(struct wlandevice *wlandev, u16 fc)
{
	u16 ftype;
	u16 fstype;
	int drop = 0;
	/* Classify frame, increment counter */
	ftype = WLAN_GET_FC_FTYPE(fc);
	fstype = WLAN_GET_FC_FSTYPE(fc);
#if 0
	netdev_dbg(wlandev->netdev, "rx_typedrop : ftype=%d fstype=%d.\n",
		   ftype, fstype);
#endif
	switch (ftype) {
	case WLAN_FTYPE_MGMT:
		if ((wlandev->netdev->flags & IFF_PROMISC) ||
		    (wlandev->netdev->flags & IFF_ALLMULTI)) {
			drop = 1;
			break;
		}
		netdev_dbg(wlandev->netdev, "rx'd mgmt:\n");
		wlandev->rx.mgmt++;
		switch (fstype) {
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
		netdev_dbg(wlandev->netdev, "rx'd ctl:\n");
		wlandev->rx.ctl++;
		switch (fstype) {
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
		switch (fstype) {
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
			netdev_dbg(wlandev->netdev, "rx'd data:null\n");
			wlandev->rx.null++;
			break;
		case WLAN_FSTYPE_CFACK:
			netdev_dbg(wlandev->netdev, "rx'd data:cfack\n");
			wlandev->rx.cfack++;
			break;
		case WLAN_FSTYPE_CFPOLL:
			netdev_dbg(wlandev->netdev, "rx'd data:cfpoll\n");
			wlandev->rx.cfpoll++;
			break;
		case WLAN_FSTYPE_CFACK_CFPOLL:
			netdev_dbg(wlandev->netdev, "rx'd data:cfack_cfpoll\n");
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

static void p80211knetdev_tx_timeout(netdevice_t *netdev)
{
	struct wlandevice *wlandev = netdev->ml_priv;

	if (wlandev->tx_timeout) {
		wlandev->tx_timeout(wlandev);
	} else {
		netdev_warn(netdev, "Implement tx_timeout for %s\n",
			    wlandev->nsdname);
		netif_wake_queue(wlandev->netdev);
	}
}
