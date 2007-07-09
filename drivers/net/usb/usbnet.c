/*
 * USB Network driver infrastructure
 * Copyright (C) 2000-2005 by David Brownell
 * Copyright (C) 2003-2005 David Hollis <dhollis@davehollis.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * This is a generic "USB networking" framework that works with several
 * kinds of full and high speed networking devices:  host-to-host cables,
 * smart usb peripherals, and actual Ethernet adapters.
 *
 * These devices usually differ in terms of control protocols (if they
 * even have one!) and sometimes they define new framing to wrap or batch
 * Ethernet packets.  Otherwise, they talk to USB pretty much the same,
 * so interface (un)binding, endpoint I/O queues, fault handling, and other
 * issues can usefully be addressed by this framework.
 */

// #define	DEBUG			// error path messages, extra info
// #define	VERBOSE			// more; success messages

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/usb.h>

#include "usbnet.h"

#define DRIVER_VERSION		"22-Aug-2005"


/*-------------------------------------------------------------------------*/

/*
 * Nineteen USB 1.1 max size bulk transactions per frame (ms), max.
 * Several dozen bytes of IPv4 data can fit in two such transactions.
 * One maximum size Ethernet packet takes twenty four of them.
 * For high speed, each frame comfortably fits almost 36 max size
 * Ethernet packets (so queues should be bigger).
 *
 * REVISIT qlens should be members of 'struct usbnet'; the goal is to
 * let the USB host controller be busy for 5msec or more before an irq
 * is required, under load.  Jumbograms change the equation.
 */
#define RX_MAX_QUEUE_MEMORY (60 * 1518)
#define	RX_QLEN(dev) (((dev)->udev->speed == USB_SPEED_HIGH) ? \
			(RX_MAX_QUEUE_MEMORY/(dev)->rx_urb_size) : 4)
#define	TX_QLEN(dev) (((dev)->udev->speed == USB_SPEED_HIGH) ? \
			(RX_MAX_QUEUE_MEMORY/(dev)->hard_mtu) : 4)

// reawaken network queue this soon after stopping; else watchdog barks
#define TX_TIMEOUT_JIFFIES	(5*HZ)

// throttle rx/tx briefly after some faults, so khubd might disconnect()
// us (it polls at HZ/4 usually) before we report too many false errors.
#define THROTTLE_JIFFIES	(HZ/8)

// between wakeups
#define UNLINK_TIMEOUT_MS	3

/*-------------------------------------------------------------------------*/

// randomly generated ethernet address
static u8	node_id [ETH_ALEN];

static const char driver_name [] = "usbnet";

/* use ethtool to change the level for any given device */
static int msg_level = -1;
module_param (msg_level, int, 0);
MODULE_PARM_DESC (msg_level, "Override default message level");

/*-------------------------------------------------------------------------*/

/* handles CDC Ethernet and many other network "bulk data" interfaces */
int usbnet_get_endpoints(struct usbnet *dev, struct usb_interface *intf)
{
	int				tmp;
	struct usb_host_interface	*alt = NULL;
	struct usb_host_endpoint	*in = NULL, *out = NULL;
	struct usb_host_endpoint	*status = NULL;

	for (tmp = 0; tmp < intf->num_altsetting; tmp++) {
		unsigned	ep;

		in = out = status = NULL;
		alt = intf->altsetting + tmp;

		/* take the first altsetting with in-bulk + out-bulk;
		 * remember any status endpoint, just in case;
		 * ignore other endpoints and altsetttings.
		 */
		for (ep = 0; ep < alt->desc.bNumEndpoints; ep++) {
			struct usb_host_endpoint	*e;
			int				intr = 0;

			e = alt->endpoint + ep;
			switch (e->desc.bmAttributes) {
			case USB_ENDPOINT_XFER_INT:
				if (!usb_endpoint_dir_in(&e->desc))
					continue;
				intr = 1;
				/* FALLTHROUGH */
			case USB_ENDPOINT_XFER_BULK:
				break;
			default:
				continue;
			}
			if (usb_endpoint_dir_in(&e->desc)) {
				if (!intr && !in)
					in = e;
				else if (intr && !status)
					status = e;
			} else {
				if (!out)
					out = e;
			}
		}
		if (in && out)
			break;
	}
	if (!alt || !in || !out)
		return -EINVAL;

	if (alt->desc.bAlternateSetting != 0
			|| !(dev->driver_info->flags & FLAG_NO_SETINT)) {
		tmp = usb_set_interface (dev->udev, alt->desc.bInterfaceNumber,
				alt->desc.bAlternateSetting);
		if (tmp < 0)
			return tmp;
	}

	dev->in = usb_rcvbulkpipe (dev->udev,
			in->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	dev->out = usb_sndbulkpipe (dev->udev,
			out->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	dev->status = status;
	return 0;
}
EXPORT_SYMBOL_GPL(usbnet_get_endpoints);

static void intr_complete (struct urb *urb);

static int init_status (struct usbnet *dev, struct usb_interface *intf)
{
	char		*buf = NULL;
	unsigned	pipe = 0;
	unsigned	maxp;
	unsigned	period;

	if (!dev->driver_info->status)
		return 0;

	pipe = usb_rcvintpipe (dev->udev,
			dev->status->desc.bEndpointAddress
				& USB_ENDPOINT_NUMBER_MASK);
	maxp = usb_maxpacket (dev->udev, pipe, 0);

	/* avoid 1 msec chatter:  min 8 msec poll rate */
	period = max ((int) dev->status->desc.bInterval,
		(dev->udev->speed == USB_SPEED_HIGH) ? 7 : 3);

	buf = kmalloc (maxp, GFP_KERNEL);
	if (buf) {
		dev->interrupt = usb_alloc_urb (0, GFP_KERNEL);
		if (!dev->interrupt) {
			kfree (buf);
			return -ENOMEM;
		} else {
			usb_fill_int_urb(dev->interrupt, dev->udev, pipe,
				buf, maxp, intr_complete, dev, period);
			dev_dbg(&intf->dev,
				"status ep%din, %d bytes period %d\n",
				usb_pipeendpoint(pipe), maxp, period);
		}
	}
	return 0;
}

/* Passes this packet up the stack, updating its accounting.
 * Some link protocols batch packets, so their rx_fixup paths
 * can return clones as well as just modify the original skb.
 */
void usbnet_skb_return (struct usbnet *dev, struct sk_buff *skb)
{
	int	status;

	skb->protocol = eth_type_trans (skb, dev->net);
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += skb->len;

	if (netif_msg_rx_status (dev))
		devdbg (dev, "< rx, len %zu, type 0x%x",
			skb->len + sizeof (struct ethhdr), skb->protocol);
	memset (skb->cb, 0, sizeof (struct skb_data));
	status = netif_rx (skb);
	if (status != NET_RX_SUCCESS && netif_msg_rx_err (dev))
		devdbg (dev, "netif_rx status %d", status);
}
EXPORT_SYMBOL_GPL(usbnet_skb_return);


/*-------------------------------------------------------------------------
 *
 * Network Device Driver (peer link to "Host Device", from USB host)
 *
 *-------------------------------------------------------------------------*/

static int usbnet_change_mtu (struct net_device *net, int new_mtu)
{
	struct usbnet	*dev = netdev_priv(net);
	int		ll_mtu = new_mtu + net->hard_header_len;
	int		old_hard_mtu = dev->hard_mtu;
	int		old_rx_urb_size = dev->rx_urb_size;

	if (new_mtu <= 0)
		return -EINVAL;
	// no second zero-length packet read wanted after mtu-sized packets
	if ((ll_mtu % dev->maxpacket) == 0)
		return -EDOM;
	net->mtu = new_mtu;

	dev->hard_mtu = net->mtu + net->hard_header_len;
	if (dev->rx_urb_size == old_hard_mtu) {
		dev->rx_urb_size = dev->hard_mtu;
		if (dev->rx_urb_size > old_rx_urb_size)
			usbnet_unlink_rx_urbs(dev);
	}

	return 0;
}

/*-------------------------------------------------------------------------*/

static struct net_device_stats *usbnet_get_stats (struct net_device *net)
{
	struct usbnet	*dev = netdev_priv(net);
	return &dev->stats;
}

/*-------------------------------------------------------------------------*/

/* some LK 2.4 HCDs oopsed if we freed or resubmitted urbs from
 * completion callbacks.  2.5 should have fixed those bugs...
 */

static void defer_bh(struct usbnet *dev, struct sk_buff *skb, struct sk_buff_head *list)
{
	unsigned long		flags;

	spin_lock_irqsave(&list->lock, flags);
	__skb_unlink(skb, list);
	spin_unlock(&list->lock);
	spin_lock(&dev->done.lock);
	__skb_queue_tail(&dev->done, skb);
	if (dev->done.qlen == 1)
		tasklet_schedule(&dev->bh);
	spin_unlock_irqrestore(&dev->done.lock, flags);
}

/* some work can't be done in tasklets, so we use keventd
 *
 * NOTE:  annoying asymmetry:  if it's active, schedule_work() fails,
 * but tasklet_schedule() doesn't.  hope the failure is rare.
 */
void usbnet_defer_kevent (struct usbnet *dev, int work)
{
	set_bit (work, &dev->flags);
	if (!schedule_work (&dev->kevent))
		deverr (dev, "kevent %d may have been dropped", work);
	else
		devdbg (dev, "kevent %d scheduled", work);
}
EXPORT_SYMBOL_GPL(usbnet_defer_kevent);

/*-------------------------------------------------------------------------*/

static void rx_complete (struct urb *urb);

static void rx_submit (struct usbnet *dev, struct urb *urb, gfp_t flags)
{
	struct sk_buff		*skb;
	struct skb_data		*entry;
	int			retval = 0;
	unsigned long		lockflags;
	size_t			size = dev->rx_urb_size;

	if ((skb = alloc_skb (size + NET_IP_ALIGN, flags)) == NULL) {
		if (netif_msg_rx_err (dev))
			devdbg (dev, "no rx skb");
		usbnet_defer_kevent (dev, EVENT_RX_MEMORY);
		usb_free_urb (urb);
		return;
	}
	skb_reserve (skb, NET_IP_ALIGN);

	entry = (struct skb_data *) skb->cb;
	entry->urb = urb;
	entry->dev = dev;
	entry->state = rx_start;
	entry->length = 0;

	usb_fill_bulk_urb (urb, dev->udev, dev->in,
		skb->data, size, rx_complete, skb);

	spin_lock_irqsave (&dev->rxq.lock, lockflags);

	if (netif_running (dev->net)
			&& netif_device_present (dev->net)
			&& !test_bit (EVENT_RX_HALT, &dev->flags)) {
		switch (retval = usb_submit_urb (urb, GFP_ATOMIC)) {
		case -EPIPE:
			usbnet_defer_kevent (dev, EVENT_RX_HALT);
			break;
		case -ENOMEM:
			usbnet_defer_kevent (dev, EVENT_RX_MEMORY);
			break;
		case -ENODEV:
			if (netif_msg_ifdown (dev))
				devdbg (dev, "device gone");
			netif_device_detach (dev->net);
			break;
		default:
			if (netif_msg_rx_err (dev))
				devdbg (dev, "rx submit, %d", retval);
			tasklet_schedule (&dev->bh);
			break;
		case 0:
			__skb_queue_tail (&dev->rxq, skb);
		}
	} else {
		if (netif_msg_ifdown (dev))
			devdbg (dev, "rx: stopped");
		retval = -ENOLINK;
	}
	spin_unlock_irqrestore (&dev->rxq.lock, lockflags);
	if (retval) {
		dev_kfree_skb_any (skb);
		usb_free_urb (urb);
	}
}


/*-------------------------------------------------------------------------*/

static inline void rx_process (struct usbnet *dev, struct sk_buff *skb)
{
	if (dev->driver_info->rx_fixup
			&& !dev->driver_info->rx_fixup (dev, skb))
		goto error;
	// else network stack removes extra byte if we forced a short packet

	if (skb->len)
		usbnet_skb_return (dev, skb);
	else {
		if (netif_msg_rx_err (dev))
			devdbg (dev, "drop");
error:
		dev->stats.rx_errors++;
		skb_queue_tail (&dev->done, skb);
	}
}

/*-------------------------------------------------------------------------*/

static void rx_complete (struct urb *urb)
{
	struct sk_buff		*skb = (struct sk_buff *) urb->context;
	struct skb_data		*entry = (struct skb_data *) skb->cb;
	struct usbnet		*dev = entry->dev;
	int			urb_status = urb->status;

	skb_put (skb, urb->actual_length);
	entry->state = rx_done;
	entry->urb = NULL;

	switch (urb_status) {
	/* success */
	case 0:
		if (skb->len < dev->net->hard_header_len) {
			entry->state = rx_cleanup;
			dev->stats.rx_errors++;
			dev->stats.rx_length_errors++;
			if (netif_msg_rx_err (dev))
				devdbg (dev, "rx length %d", skb->len);
		}
		break;

	/* stalls need manual reset. this is rare ... except that
	 * when going through USB 2.0 TTs, unplug appears this way.
	 * we avoid the highspeed version of the ETIMEOUT/EILSEQ
	 * storm, recovering as needed.
	 */
	case -EPIPE:
		dev->stats.rx_errors++;
		usbnet_defer_kevent (dev, EVENT_RX_HALT);
		// FALLTHROUGH

	/* software-driven interface shutdown */
	case -ECONNRESET:		/* async unlink */
	case -ESHUTDOWN:		/* hardware gone */
		if (netif_msg_ifdown (dev))
			devdbg (dev, "rx shutdown, code %d", urb_status);
		goto block;

	/* we get controller i/o faults during khubd disconnect() delays.
	 * throttle down resubmits, to avoid log floods; just temporarily,
	 * so we still recover when the fault isn't a khubd delay.
	 */
	case -EPROTO:
	case -ETIME:
	case -EILSEQ:
		dev->stats.rx_errors++;
		if (!timer_pending (&dev->delay)) {
			mod_timer (&dev->delay, jiffies + THROTTLE_JIFFIES);
			if (netif_msg_link (dev))
				devdbg (dev, "rx throttle %d", urb_status);
		}
block:
		entry->state = rx_cleanup;
		entry->urb = urb;
		urb = NULL;
		break;

	/* data overrun ... flush fifo? */
	case -EOVERFLOW:
		dev->stats.rx_over_errors++;
		// FALLTHROUGH

	default:
		entry->state = rx_cleanup;
		dev->stats.rx_errors++;
		if (netif_msg_rx_err (dev))
			devdbg (dev, "rx status %d", urb_status);
		break;
	}

	defer_bh(dev, skb, &dev->rxq);

	if (urb) {
		if (netif_running (dev->net)
				&& !test_bit (EVENT_RX_HALT, &dev->flags)) {
			rx_submit (dev, urb, GFP_ATOMIC);
			return;
		}
		usb_free_urb (urb);
	}
	if (netif_msg_rx_err (dev))
		devdbg (dev, "no read resubmitted");
}

static void intr_complete (struct urb *urb)
{
	struct usbnet	*dev = urb->context;
	int		status = urb->status;

	switch (status) {
	/* success */
	case 0:
		dev->driver_info->status(dev, urb);
		break;

	/* software-driven interface shutdown */
	case -ENOENT:		/* urb killed */
	case -ESHUTDOWN:	/* hardware gone */
		if (netif_msg_ifdown (dev))
			devdbg (dev, "intr shutdown, code %d", status);
		return;

	/* NOTE:  not throttling like RX/TX, since this endpoint
	 * already polls infrequently
	 */
	default:
		devdbg (dev, "intr status %d", status);
		break;
	}

	if (!netif_running (dev->net))
		return;

	memset(urb->transfer_buffer, 0, urb->transfer_buffer_length);
	status = usb_submit_urb (urb, GFP_ATOMIC);
	if (status != 0 && netif_msg_timer (dev))
		deverr(dev, "intr resubmit --> %d", status);
}

/*-------------------------------------------------------------------------*/

// unlink pending rx/tx; completion handlers do all other cleanup

static int unlink_urbs (struct usbnet *dev, struct sk_buff_head *q)
{
	unsigned long		flags;
	struct sk_buff		*skb, *skbnext;
	int			count = 0;

	spin_lock_irqsave (&q->lock, flags);
	for (skb = q->next; skb != (struct sk_buff *) q; skb = skbnext) {
		struct skb_data		*entry;
		struct urb		*urb;
		int			retval;

		entry = (struct skb_data *) skb->cb;
		urb = entry->urb;
		skbnext = skb->next;

		// during some PM-driven resume scenarios,
		// these (async) unlinks complete immediately
		retval = usb_unlink_urb (urb);
		if (retval != -EINPROGRESS && retval != 0)
			devdbg (dev, "unlink urb err, %d", retval);
		else
			count++;
	}
	spin_unlock_irqrestore (&q->lock, flags);
	return count;
}

// Flush all pending rx urbs
// minidrivers may need to do this when the MTU changes

void usbnet_unlink_rx_urbs(struct usbnet *dev)
{
	if (netif_running(dev->net)) {
		(void) unlink_urbs (dev, &dev->rxq);
		tasklet_schedule(&dev->bh);
	}
}
EXPORT_SYMBOL_GPL(usbnet_unlink_rx_urbs);

/*-------------------------------------------------------------------------*/

// precondition: never called in_interrupt

static int usbnet_stop (struct net_device *net)
{
	struct usbnet		*dev = netdev_priv(net);
	int			temp;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK (unlink_wakeup);
	DECLARE_WAITQUEUE (wait, current);

	netif_stop_queue (net);

	if (netif_msg_ifdown (dev))
		devinfo (dev, "stop stats: rx/tx %ld/%ld, errs %ld/%ld",
			dev->stats.rx_packets, dev->stats.tx_packets,
			dev->stats.rx_errors, dev->stats.tx_errors
			);

	// ensure there are no more active urbs
	add_wait_queue (&unlink_wakeup, &wait);
	dev->wait = &unlink_wakeup;
	temp = unlink_urbs (dev, &dev->txq) + unlink_urbs (dev, &dev->rxq);

	// maybe wait for deletions to finish.
	while (!skb_queue_empty(&dev->rxq)
			&& !skb_queue_empty(&dev->txq)
			&& !skb_queue_empty(&dev->done)) {
		msleep(UNLINK_TIMEOUT_MS);
		if (netif_msg_ifdown (dev))
			devdbg (dev, "waited for %d urb completions", temp);
	}
	dev->wait = NULL;
	remove_wait_queue (&unlink_wakeup, &wait);

	usb_kill_urb(dev->interrupt);

	/* deferred work (task, timer, softirq) must also stop.
	 * can't flush_scheduled_work() until we drop rtnl (later),
	 * else workers could deadlock; so make workers a NOP.
	 */
	dev->flags = 0;
	del_timer_sync (&dev->delay);
	tasklet_kill (&dev->bh);

	return 0;
}

/*-------------------------------------------------------------------------*/

// posts reads, and enables write queuing

// precondition: never called in_interrupt

static int usbnet_open (struct net_device *net)
{
	struct usbnet		*dev = netdev_priv(net);
	int			retval = 0;
	struct driver_info	*info = dev->driver_info;

	// put into "known safe" state
	if (info->reset && (retval = info->reset (dev)) < 0) {
		if (netif_msg_ifup (dev))
			devinfo (dev,
				"open reset fail (%d) usbnet usb-%s-%s, %s",
				retval,
				dev->udev->bus->bus_name, dev->udev->devpath,
			info->description);
		goto done;
	}

	// insist peer be connected
	if (info->check_connect && (retval = info->check_connect (dev)) < 0) {
		if (netif_msg_ifup (dev))
			devdbg (dev, "can't open; %d", retval);
		goto done;
	}

	/* start any status interrupt transfer */
	if (dev->interrupt) {
		retval = usb_submit_urb (dev->interrupt, GFP_KERNEL);
		if (retval < 0) {
			if (netif_msg_ifup (dev))
				deverr (dev, "intr submit %d", retval);
			goto done;
		}
	}

	netif_start_queue (net);
	if (netif_msg_ifup (dev)) {
		char	*framing;

		if (dev->driver_info->flags & FLAG_FRAMING_NC)
			framing = "NetChip";
		else if (dev->driver_info->flags & FLAG_FRAMING_GL)
			framing = "GeneSys";
		else if (dev->driver_info->flags & FLAG_FRAMING_Z)
			framing = "Zaurus";
		else if (dev->driver_info->flags & FLAG_FRAMING_RN)
			framing = "RNDIS";
		else if (dev->driver_info->flags & FLAG_FRAMING_AX)
			framing = "ASIX";
		else
			framing = "simple";

		devinfo (dev, "open: enable queueing "
				"(rx %d, tx %d) mtu %d %s framing",
			(int)RX_QLEN (dev), (int)TX_QLEN (dev), dev->net->mtu,
			framing);
	}

	// delay posting reads until we're fully open
	tasklet_schedule (&dev->bh);
done:
	return retval;
}

/*-------------------------------------------------------------------------*/

/* ethtool methods; minidrivers may need to add some more, but
 * they'll probably want to use this base set.
 */

#if defined(CONFIG_MII) || defined(CONFIG_MII_MODULE)
#define HAVE_MII

int usbnet_get_settings (struct net_device *net, struct ethtool_cmd *cmd)
{
	struct usbnet *dev = netdev_priv(net);

	if (!dev->mii.mdio_read)
		return -EOPNOTSUPP;

	return mii_ethtool_gset(&dev->mii, cmd);
}
EXPORT_SYMBOL_GPL(usbnet_get_settings);

int usbnet_set_settings (struct net_device *net, struct ethtool_cmd *cmd)
{
	struct usbnet *dev = netdev_priv(net);
	int retval;

	if (!dev->mii.mdio_write)
		return -EOPNOTSUPP;

	retval = mii_ethtool_sset(&dev->mii, cmd);

	/* link speed/duplex might have changed */
	if (dev->driver_info->link_reset)
		dev->driver_info->link_reset(dev);

	return retval;

}
EXPORT_SYMBOL_GPL(usbnet_set_settings);

u32 usbnet_get_link (struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);

	/* If a check_connect is defined, return its result */
	if (dev->driver_info->check_connect)
		return dev->driver_info->check_connect (dev) == 0;

	/* if the device has mii operations, use those */
	if (dev->mii.mdio_read)
		return mii_link_ok(&dev->mii);

	/* Otherwise, say we're up (to avoid breaking scripts) */
	return 1;
}
EXPORT_SYMBOL_GPL(usbnet_get_link);

int usbnet_nway_reset(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);

	if (!dev->mii.mdio_write)
		return -EOPNOTSUPP;

	return mii_nway_restart(&dev->mii);
}
EXPORT_SYMBOL_GPL(usbnet_nway_reset);

#endif	/* HAVE_MII */

void usbnet_get_drvinfo (struct net_device *net, struct ethtool_drvinfo *info)
{
	struct usbnet *dev = netdev_priv(net);

	strncpy (info->driver, dev->driver_name, sizeof info->driver);
	strncpy (info->version, DRIVER_VERSION, sizeof info->version);
	strncpy (info->fw_version, dev->driver_info->description,
		sizeof info->fw_version);
	usb_make_path (dev->udev, info->bus_info, sizeof info->bus_info);
}
EXPORT_SYMBOL_GPL(usbnet_get_drvinfo);

u32 usbnet_get_msglevel (struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);

	return dev->msg_enable;
}
EXPORT_SYMBOL_GPL(usbnet_get_msglevel);

void usbnet_set_msglevel (struct net_device *net, u32 level)
{
	struct usbnet *dev = netdev_priv(net);

	dev->msg_enable = level;
}
EXPORT_SYMBOL_GPL(usbnet_set_msglevel);

/* drivers may override default ethtool_ops in their bind() routine */
static struct ethtool_ops usbnet_ethtool_ops = {
#ifdef	HAVE_MII
	.get_settings		= usbnet_get_settings,
	.set_settings		= usbnet_set_settings,
	.get_link		= usbnet_get_link,
	.nway_reset		= usbnet_nway_reset,
#endif
	.get_drvinfo		= usbnet_get_drvinfo,
	.get_msglevel		= usbnet_get_msglevel,
	.set_msglevel		= usbnet_set_msglevel,
};

/*-------------------------------------------------------------------------*/

/* work that cannot be done in interrupt context uses keventd.
 *
 * NOTE:  with 2.5 we could do more of this using completion callbacks,
 * especially now that control transfers can be queued.
 */
static void
kevent (struct work_struct *work)
{
	struct usbnet		*dev =
		container_of(work, struct usbnet, kevent);
	int			status;

	/* usb_clear_halt() needs a thread context */
	if (test_bit (EVENT_TX_HALT, &dev->flags)) {
		unlink_urbs (dev, &dev->txq);
		status = usb_clear_halt (dev->udev, dev->out);
		if (status < 0
				&& status != -EPIPE
				&& status != -ESHUTDOWN) {
			if (netif_msg_tx_err (dev))
				deverr (dev, "can't clear tx halt, status %d",
					status);
		} else {
			clear_bit (EVENT_TX_HALT, &dev->flags);
			if (status != -ESHUTDOWN)
				netif_wake_queue (dev->net);
		}
	}
	if (test_bit (EVENT_RX_HALT, &dev->flags)) {
		unlink_urbs (dev, &dev->rxq);
		status = usb_clear_halt (dev->udev, dev->in);
		if (status < 0
				&& status != -EPIPE
				&& status != -ESHUTDOWN) {
			if (netif_msg_rx_err (dev))
				deverr (dev, "can't clear rx halt, status %d",
					status);
		} else {
			clear_bit (EVENT_RX_HALT, &dev->flags);
			tasklet_schedule (&dev->bh);
		}
	}

	/* tasklet could resubmit itself forever if memory is tight */
	if (test_bit (EVENT_RX_MEMORY, &dev->flags)) {
		struct urb	*urb = NULL;

		if (netif_running (dev->net))
			urb = usb_alloc_urb (0, GFP_KERNEL);
		else
			clear_bit (EVENT_RX_MEMORY, &dev->flags);
		if (urb != NULL) {
			clear_bit (EVENT_RX_MEMORY, &dev->flags);
			rx_submit (dev, urb, GFP_KERNEL);
			tasklet_schedule (&dev->bh);
		}
	}

	if (test_bit (EVENT_LINK_RESET, &dev->flags)) {
		struct driver_info	*info = dev->driver_info;
		int			retval = 0;

		clear_bit (EVENT_LINK_RESET, &dev->flags);
		if(info->link_reset && (retval = info->link_reset(dev)) < 0) {
			devinfo(dev, "link reset failed (%d) usbnet usb-%s-%s, %s",
				retval,
				dev->udev->bus->bus_name, dev->udev->devpath,
				info->description);
		}
	}

	if (dev->flags)
		devdbg (dev, "kevent done, flags = 0x%lx",
			dev->flags);
}

/*-------------------------------------------------------------------------*/

static void tx_complete (struct urb *urb)
{
	struct sk_buff		*skb = (struct sk_buff *) urb->context;
	struct skb_data		*entry = (struct skb_data *) skb->cb;
	struct usbnet		*dev = entry->dev;

	if (urb->status == 0) {
		dev->stats.tx_packets++;
		dev->stats.tx_bytes += entry->length;
	} else {
		dev->stats.tx_errors++;

		switch (urb->status) {
		case -EPIPE:
			usbnet_defer_kevent (dev, EVENT_TX_HALT);
			break;

		/* software-driven interface shutdown */
		case -ECONNRESET:		// async unlink
		case -ESHUTDOWN:		// hardware gone
			break;

		// like rx, tx gets controller i/o faults during khubd delays
		// and so it uses the same throttling mechanism.
		case -EPROTO:
		case -ETIME:
		case -EILSEQ:
			if (!timer_pending (&dev->delay)) {
				mod_timer (&dev->delay,
					jiffies + THROTTLE_JIFFIES);
				if (netif_msg_link (dev))
					devdbg (dev, "tx throttle %d",
							urb->status);
			}
			netif_stop_queue (dev->net);
			break;
		default:
			if (netif_msg_tx_err (dev))
				devdbg (dev, "tx err %d", entry->urb->status);
			break;
		}
	}

	urb->dev = NULL;
	entry->state = tx_done;
	defer_bh(dev, skb, &dev->txq);
}

/*-------------------------------------------------------------------------*/

static void usbnet_tx_timeout (struct net_device *net)
{
	struct usbnet		*dev = netdev_priv(net);

	unlink_urbs (dev, &dev->txq);
	tasklet_schedule (&dev->bh);

	// FIXME: device recovery -- reset?
}

/*-------------------------------------------------------------------------*/

static int usbnet_start_xmit (struct sk_buff *skb, struct net_device *net)
{
	struct usbnet		*dev = netdev_priv(net);
	int			length;
	int			retval = NET_XMIT_SUCCESS;
	struct urb		*urb = NULL;
	struct skb_data		*entry;
	struct driver_info	*info = dev->driver_info;
	unsigned long		flags;

	// some devices want funky USB-level framing, for
	// win32 driver (usually) and/or hardware quirks
	if (info->tx_fixup) {
		skb = info->tx_fixup (dev, skb, GFP_ATOMIC);
		if (!skb) {
			if (netif_msg_tx_err (dev))
				devdbg (dev, "can't tx_fixup skb");
			goto drop;
		}
	}
	length = skb->len;

	if (!(urb = usb_alloc_urb (0, GFP_ATOMIC))) {
		if (netif_msg_tx_err (dev))
			devdbg (dev, "no urb");
		goto drop;
	}

	entry = (struct skb_data *) skb->cb;
	entry->urb = urb;
	entry->dev = dev;
	entry->state = tx_start;
	entry->length = length;

	usb_fill_bulk_urb (urb, dev->udev, dev->out,
			skb->data, skb->len, tx_complete, skb);

	/* don't assume the hardware handles USB_ZERO_PACKET
	 * NOTE:  strictly conforming cdc-ether devices should expect
	 * the ZLP here, but ignore the one-byte packet.
	 */
	if ((length % dev->maxpacket) == 0) {
		urb->transfer_buffer_length++;
		if (skb_tailroom(skb)) {
			skb->data[skb->len] = 0;
			__skb_put(skb, 1);
		}
	}

	spin_lock_irqsave (&dev->txq.lock, flags);

	switch ((retval = usb_submit_urb (urb, GFP_ATOMIC))) {
	case -EPIPE:
		netif_stop_queue (net);
		usbnet_defer_kevent (dev, EVENT_TX_HALT);
		break;
	default:
		if (netif_msg_tx_err (dev))
			devdbg (dev, "tx: submit urb err %d", retval);
		break;
	case 0:
		net->trans_start = jiffies;
		__skb_queue_tail (&dev->txq, skb);
		if (dev->txq.qlen >= TX_QLEN (dev))
			netif_stop_queue (net);
	}
	spin_unlock_irqrestore (&dev->txq.lock, flags);

	if (retval) {
		if (netif_msg_tx_err (dev))
			devdbg (dev, "drop, code %d", retval);
drop:
		retval = NET_XMIT_SUCCESS;
		dev->stats.tx_dropped++;
		if (skb)
			dev_kfree_skb_any (skb);
		usb_free_urb (urb);
	} else if (netif_msg_tx_queued (dev)) {
		devdbg (dev, "> tx, len %d, type 0x%x",
			length, skb->protocol);
	}
	return retval;
}


/*-------------------------------------------------------------------------*/

// tasklet (work deferred from completions, in_irq) or timer

static void usbnet_bh (unsigned long param)
{
	struct usbnet		*dev = (struct usbnet *) param;
	struct sk_buff		*skb;
	struct skb_data		*entry;

	while ((skb = skb_dequeue (&dev->done))) {
		entry = (struct skb_data *) skb->cb;
		switch (entry->state) {
		case rx_done:
			entry->state = rx_cleanup;
			rx_process (dev, skb);
			continue;
		case tx_done:
		case rx_cleanup:
			usb_free_urb (entry->urb);
			dev_kfree_skb (skb);
			continue;
		default:
			devdbg (dev, "bogus skb state %d", entry->state);
		}
	}

	// waiting for all pending urbs to complete?
	if (dev->wait) {
		if ((dev->txq.qlen + dev->rxq.qlen + dev->done.qlen) == 0) {
			wake_up (dev->wait);
		}

	// or are we maybe short a few urbs?
	} else if (netif_running (dev->net)
			&& netif_device_present (dev->net)
			&& !timer_pending (&dev->delay)
			&& !test_bit (EVENT_RX_HALT, &dev->flags)) {
		int	temp = dev->rxq.qlen;
		int	qlen = RX_QLEN (dev);

		if (temp < qlen) {
			struct urb	*urb;
			int		i;

			// don't refill the queue all at once
			for (i = 0; i < 10 && dev->rxq.qlen < qlen; i++) {
				urb = usb_alloc_urb (0, GFP_ATOMIC);
				if (urb != NULL)
					rx_submit (dev, urb, GFP_ATOMIC);
			}
			if (temp != dev->rxq.qlen && netif_msg_link (dev))
				devdbg (dev, "rxqlen %d --> %d",
						temp, dev->rxq.qlen);
			if (dev->rxq.qlen < qlen)
				tasklet_schedule (&dev->bh);
		}
		if (dev->txq.qlen < TX_QLEN (dev))
			netif_wake_queue (dev->net);
	}
}



/*-------------------------------------------------------------------------
 *
 * USB Device Driver support
 *
 *-------------------------------------------------------------------------*/

// precondition: never called in_interrupt

void usbnet_disconnect (struct usb_interface *intf)
{
	struct usbnet		*dev;
	struct usb_device	*xdev;
	struct net_device	*net;

	dev = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);
	if (!dev)
		return;

	xdev = interface_to_usbdev (intf);

	if (netif_msg_probe (dev))
		devinfo (dev, "unregister '%s' usb-%s-%s, %s",
			intf->dev.driver->name,
			xdev->bus->bus_name, xdev->devpath,
			dev->driver_info->description);

	net = dev->net;
	unregister_netdev (net);

	/* we don't hold rtnl here ... */
	flush_scheduled_work ();

	if (dev->driver_info->unbind)
		dev->driver_info->unbind (dev, intf);

	free_netdev(net);
	usb_put_dev (xdev);
}
EXPORT_SYMBOL_GPL(usbnet_disconnect);


/*-------------------------------------------------------------------------*/

// precondition: never called in_interrupt

int
usbnet_probe (struct usb_interface *udev, const struct usb_device_id *prod)
{
	struct usbnet			*dev;
	struct net_device		*net;
	struct usb_host_interface	*interface;
	struct driver_info		*info;
	struct usb_device		*xdev;
	int				status;
	const char			*name;

	name = udev->dev.driver->name;
	info = (struct driver_info *) prod->driver_info;
	if (!info) {
		dev_dbg (&udev->dev, "blacklisted by %s\n", name);
		return -ENODEV;
	}
	xdev = interface_to_usbdev (udev);
	interface = udev->cur_altsetting;

	usb_get_dev (xdev);

	status = -ENOMEM;

	// set up our own records
	net = alloc_etherdev(sizeof(*dev));
	if (!net) {
		dbg ("can't kmalloc dev");
		goto out;
	}

	dev = netdev_priv(net);
	dev->udev = xdev;
	dev->driver_info = info;
	dev->driver_name = name;
	dev->msg_enable = netif_msg_init (msg_level, NETIF_MSG_DRV
				| NETIF_MSG_PROBE | NETIF_MSG_LINK);
	skb_queue_head_init (&dev->rxq);
	skb_queue_head_init (&dev->txq);
	skb_queue_head_init (&dev->done);
	dev->bh.func = usbnet_bh;
	dev->bh.data = (unsigned long) dev;
	INIT_WORK (&dev->kevent, kevent);
	dev->delay.function = usbnet_bh;
	dev->delay.data = (unsigned long) dev;
	init_timer (&dev->delay);
	mutex_init (&dev->phy_mutex);

	SET_MODULE_OWNER (net);
	dev->net = net;
	strcpy (net->name, "usb%d");
	memcpy (net->dev_addr, node_id, sizeof node_id);

	/* rx and tx sides can use different message sizes;
	 * bind() should set rx_urb_size in that case.
	 */
	dev->hard_mtu = net->mtu + net->hard_header_len;
#if 0
// dma_supported() is deeply broken on almost all architectures
	// possible with some EHCI controllers
	if (dma_supported (&udev->dev, DMA_64BIT_MASK))
		net->features |= NETIF_F_HIGHDMA;
#endif

	net->change_mtu = usbnet_change_mtu;
	net->get_stats = usbnet_get_stats;
	net->hard_start_xmit = usbnet_start_xmit;
	net->open = usbnet_open;
	net->stop = usbnet_stop;
	net->watchdog_timeo = TX_TIMEOUT_JIFFIES;
	net->tx_timeout = usbnet_tx_timeout;
	net->ethtool_ops = &usbnet_ethtool_ops;

	// allow device-specific bind/init procedures
	// NOTE net->name still not usable ...
	if (info->bind) {
		status = info->bind (dev, udev);
		if (status < 0)
			goto out1;

		// heuristic:  "usb%d" for links we know are two-host,
		// else "eth%d" when there's reasonable doubt.  userspace
		// can rename the link if it knows better.
		if ((dev->driver_info->flags & FLAG_ETHER) != 0
				&& (net->dev_addr [0] & 0x02) == 0)
			strcpy (net->name, "eth%d");

		/* maybe the remote can't receive an Ethernet MTU */
		if (net->mtu > (dev->hard_mtu - net->hard_header_len))
			net->mtu = dev->hard_mtu - net->hard_header_len;
	} else if (!info->in || !info->out)
		status = usbnet_get_endpoints (dev, udev);
	else {
		dev->in = usb_rcvbulkpipe (xdev, info->in);
		dev->out = usb_sndbulkpipe (xdev, info->out);
		if (!(info->flags & FLAG_NO_SETINT))
			status = usb_set_interface (xdev,
				interface->desc.bInterfaceNumber,
				interface->desc.bAlternateSetting);
		else
			status = 0;

	}
	if (status == 0 && dev->status)
		status = init_status (dev, udev);
	if (status < 0)
		goto out3;

	if (!dev->rx_urb_size)
		dev->rx_urb_size = dev->hard_mtu;
	dev->maxpacket = usb_maxpacket (dev->udev, dev->out, 1);

	SET_NETDEV_DEV(net, &udev->dev);
	status = register_netdev (net);
	if (status)
		goto out3;
	if (netif_msg_probe (dev))
		devinfo (dev, "register '%s' at usb-%s-%s, %s, "
				"%02x:%02x:%02x:%02x:%02x:%02x",
			udev->dev.driver->name,
			xdev->bus->bus_name, xdev->devpath,
			dev->driver_info->description,
			net->dev_addr [0], net->dev_addr [1],
			net->dev_addr [2], net->dev_addr [3],
			net->dev_addr [4], net->dev_addr [5]);

	// ok, it's ready to go.
	usb_set_intfdata (udev, dev);

	// start as if the link is up
	netif_device_attach (net);

	return 0;

out3:
	if (info->unbind)
		info->unbind (dev, udev);
out1:
	free_netdev(net);
out:
	usb_put_dev(xdev);
	return status;
}
EXPORT_SYMBOL_GPL(usbnet_probe);

/*-------------------------------------------------------------------------*/

/*
 * suspend the whole driver as soon as the first interface is suspended
 * resume only when the last interface is resumed
 */

int usbnet_suspend (struct usb_interface *intf, pm_message_t message)
{
	struct usbnet		*dev = usb_get_intfdata(intf);

	if (!dev->suspend_count++) {
		/* accelerate emptying of the rx and queues, to avoid
		 * having everything error out.
		 */
		netif_device_detach (dev->net);
		(void) unlink_urbs (dev, &dev->rxq);
		(void) unlink_urbs (dev, &dev->txq);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(usbnet_suspend);

int usbnet_resume (struct usb_interface *intf)
{
	struct usbnet		*dev = usb_get_intfdata(intf);

	if (!--dev->suspend_count) {
		netif_device_attach (dev->net);
		tasklet_schedule (&dev->bh);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(usbnet_resume);


/*-------------------------------------------------------------------------*/

static int __init usbnet_init(void)
{
	/* compiler should optimize this out */
	BUILD_BUG_ON (sizeof (((struct sk_buff *)0)->cb)
			< sizeof (struct skb_data));

	random_ether_addr(node_id);
	return 0;
}
module_init(usbnet_init);

static void __exit usbnet_exit(void)
{
}
module_exit(usbnet_exit);

MODULE_AUTHOR("David Brownell");
MODULE_DESCRIPTION("USB network driver framework");
MODULE_LICENSE("GPL");
