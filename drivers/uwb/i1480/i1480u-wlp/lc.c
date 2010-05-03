/*
 * WUSB Wire Adapter: WLP interface
 * Driver for the Linux Network stack.
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: docs
 *
 * This implements a very simple network driver for the WLP USB
 * device that is associated to a UWB (Ultra Wide Band) host.
 *
 * This is seen as an interface of a composite device. Once the UWB
 * host has an association to another WLP capable device, the
 * networking interface (aka WLP) can start to send packets back and
 * forth.
 *
 * Limitations:
 *
 *  - Hand cranked; can't ifup the interface until there is an association
 *
 *  - BW allocation very simplistic [see i1480u_mas_set() and callees].
 *
 *
 * ROADMAP:
 *
 *   ENTRY POINTS (driver model):
 *
 *     i1480u_driver_{exit,init}(): initialization of the driver.
 *
 *     i1480u_probe(): called by the driver code when a device
 *                     matching 'i1480u_id_table' is connected.
 *
 *                     This allocs a netdev instance, inits with
 *                     i1480u_add(), then registers_netdev().
 *         i1480u_init()
 *         i1480u_add()
 *
 *     i1480u_disconnect(): device has been disconnected/module
 *                          is being removed.
 *         i1480u_rm()
 */
#include <linux/gfp.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>

#include "i1480u-wlp.h"



static inline
void i1480u_init(struct i1480u *i1480u)
{
	/* nothing so far... doesn't it suck? */
	spin_lock_init(&i1480u->lock);
	INIT_LIST_HEAD(&i1480u->tx_list);
	spin_lock_init(&i1480u->tx_list_lock);
	wlp_options_init(&i1480u->options);
	edc_init(&i1480u->tx_errors);
	edc_init(&i1480u->rx_errors);
#ifdef i1480u_FLOW_CONTROL
	edc_init(&i1480u->notif_edc);
#endif
	stats_init(&i1480u->lqe_stats);
	stats_init(&i1480u->rssi_stats);
	wlp_init(&i1480u->wlp);
}

/**
 * Fill WLP device information structure
 *
 * The structure will contain a few character arrays, each ending with a
 * null terminated string. Each string has to fit (excluding terminating
 * character) into a specified range obtained from the WLP substack.
 *
 * It is still not clear exactly how this device information should be
 * obtained. Until we find out we use the USB device descriptor as backup, some
 * information elements have intuitive mappings, other not.
 */
static
void i1480u_fill_device_info(struct wlp *wlp, struct wlp_device_info *dev_info)
{
	struct i1480u *i1480u = container_of(wlp, struct i1480u, wlp);
	struct usb_device *usb_dev = i1480u->usb_dev;
	/* Treat device name and model name the same */
	if (usb_dev->descriptor.iProduct) {
		usb_string(usb_dev, usb_dev->descriptor.iProduct,
			   dev_info->name, sizeof(dev_info->name));
		usb_string(usb_dev, usb_dev->descriptor.iProduct,
			   dev_info->model_name, sizeof(dev_info->model_name));
	}
	if (usb_dev->descriptor.iManufacturer)
		usb_string(usb_dev, usb_dev->descriptor.iManufacturer,
			   dev_info->manufacturer,
			   sizeof(dev_info->manufacturer));
	scnprintf(dev_info->model_nr, sizeof(dev_info->model_nr), "%04x",
		  __le16_to_cpu(usb_dev->descriptor.bcdDevice));
	if (usb_dev->descriptor.iSerialNumber)
		usb_string(usb_dev, usb_dev->descriptor.iSerialNumber,
			   dev_info->serial, sizeof(dev_info->serial));
	/* FIXME: where should we obtain category? */
	dev_info->prim_dev_type.category = cpu_to_le16(WLP_DEV_CAT_OTHER);
	/* FIXME: Complete OUI and OUIsubdiv attributes */
}

#ifdef i1480u_FLOW_CONTROL
/**
 * Callback for the notification endpoint
 *
 * This mostly controls the xon/xoff protocol. In case of hard error,
 * we stop the queue. If not, we always retry.
 */
static
void i1480u_notif_cb(struct urb *urb, struct pt_regs *regs)
{
	struct i1480u *i1480u = urb->context;
	struct usb_interface *usb_iface = i1480u->usb_iface;
	struct device *dev = &usb_iface->dev;
	int result;

	switch (urb->status) {
	case 0:				/* Got valid data, do xon/xoff */
		switch (i1480u->notif_buffer[0]) {
		case 'N':
			dev_err(dev, "XOFF STOPPING queue at %lu\n", jiffies);
			netif_stop_queue(i1480u->net_dev);
			break;
		case 'A':
			dev_err(dev, "XON STARTING queue at %lu\n", jiffies);
			netif_start_queue(i1480u->net_dev);
			break;
		default:
			dev_err(dev, "NEP: unknown data 0x%02hhx\n",
				i1480u->notif_buffer[0]);
		}
		break;
	case -ECONNRESET:		/* Controlled situation ... */
	case -ENOENT:			/* we killed the URB... */
		dev_err(dev, "NEP: URB reset/noent %d\n", urb->status);
		goto error;
	case -ESHUTDOWN:		/* going away! */
		dev_err(dev, "NEP: URB down %d\n", urb->status);
		goto error;
	default:			/* Retry unless it gets ugly */
		if (edc_inc(&i1480u->notif_edc, EDC_MAX_ERRORS,
			    EDC_ERROR_TIMEFRAME)) {
			dev_err(dev, "NEP: URB max acceptable errors "
				"exceeded; resetting device\n");
			goto error_reset;
		}
		dev_err(dev, "NEP: URB error %d\n", urb->status);
		break;
	}
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result < 0) {
		dev_err(dev, "NEP: Can't resubmit URB: %d; resetting device\n",
			result);
		goto error_reset;
	}
	return;

error_reset:
	wlp_reset_all(&i1480-wlp);
error:
	netif_stop_queue(i1480u->net_dev);
	return;
}
#endif

static const struct net_device_ops i1480u_netdev_ops = {
	.ndo_open	= i1480u_open,
	.ndo_stop 	= i1480u_stop,
	.ndo_start_xmit = i1480u_hard_start_xmit,
	.ndo_tx_timeout = i1480u_tx_timeout,
	.ndo_set_config = i1480u_set_config,
	.ndo_change_mtu = i1480u_change_mtu,
};

static
int i1480u_add(struct i1480u *i1480u, struct usb_interface *iface)
{
	int result = -ENODEV;
	struct wlp *wlp = &i1480u->wlp;
	struct usb_device *usb_dev = interface_to_usbdev(iface);
	struct net_device *net_dev = i1480u->net_dev;
	struct uwb_rc *rc;
	struct uwb_dev *uwb_dev;
#ifdef i1480u_FLOW_CONTROL
	struct usb_endpoint_descriptor *epd;
#endif

	i1480u->usb_dev = usb_get_dev(usb_dev);
	i1480u->usb_iface = iface;
	rc = uwb_rc_get_by_grandpa(&i1480u->usb_dev->dev);
	if (rc == NULL) {
		dev_err(&iface->dev, "Cannot get associated UWB Radio "
			"Controller\n");
		goto out;
	}
	wlp->xmit_frame = i1480u_xmit_frame;
	wlp->fill_device_info = i1480u_fill_device_info;
	wlp->stop_queue = i1480u_stop_queue;
	wlp->start_queue = i1480u_start_queue;
	result = wlp_setup(wlp, rc, net_dev);
	if (result < 0) {
		dev_err(&iface->dev, "Cannot setup WLP\n");
		goto error_wlp_setup;
	}
	result = 0;
	ether_setup(net_dev);			/* make it an etherdevice */
	uwb_dev = &rc->uwb_dev;
	/* FIXME: hookup address change notifications? */

	memcpy(net_dev->dev_addr, uwb_dev->mac_addr.data,
	       sizeof(net_dev->dev_addr));

	net_dev->hard_header_len = sizeof(struct untd_hdr_cmp)
		+ sizeof(struct wlp_tx_hdr)
		+ WLP_DATA_HLEN
		+ ETH_HLEN;
	net_dev->mtu = 3500;
	net_dev->tx_queue_len = 20;		/* FIXME: maybe use 1000? */

/*	net_dev->flags &= ~IFF_BROADCAST;	FIXME: BUG in firmware */
	/* FIXME: multicast disabled */
	net_dev->flags &= ~IFF_MULTICAST;
	net_dev->features &= ~NETIF_F_SG;
	net_dev->features &= ~NETIF_F_FRAGLIST;
	/* All NETIF_F_*_CSUM disabled */
	net_dev->features |= NETIF_F_HIGHDMA;
	net_dev->watchdog_timeo = 5*HZ;		/* FIXME: a better default? */

	net_dev->netdev_ops = &i1480u_netdev_ops;

#ifdef i1480u_FLOW_CONTROL
	/* Notification endpoint setup (submitted when we open the device) */
	i1480u->notif_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (i1480u->notif_urb == NULL) {
		dev_err(&iface->dev, "Unable to allocate notification URB\n");
		result = -ENOMEM;
		goto error_urb_alloc;
	}
	epd = &iface->cur_altsetting->endpoint[0].desc;
	usb_fill_int_urb(i1480u->notif_urb, usb_dev,
			 usb_rcvintpipe(usb_dev, epd->bEndpointAddress),
			 i1480u->notif_buffer, sizeof(i1480u->notif_buffer),
			 i1480u_notif_cb, i1480u, epd->bInterval);

#endif

	i1480u->tx_inflight.max = i1480u_TX_INFLIGHT_MAX;
	i1480u->tx_inflight.threshold = i1480u_TX_INFLIGHT_THRESHOLD;
	i1480u->tx_inflight.restart_ts = jiffies;
	usb_set_intfdata(iface, i1480u);
	return result;

#ifdef i1480u_FLOW_CONTROL
error_urb_alloc:
#endif
	wlp_remove(wlp);
error_wlp_setup:
	uwb_rc_put(rc);
out:
	usb_put_dev(i1480u->usb_dev);
	return result;
}

static void i1480u_rm(struct i1480u *i1480u)
{
	struct uwb_rc *rc = i1480u->wlp.rc;
	usb_set_intfdata(i1480u->usb_iface, NULL);
#ifdef i1480u_FLOW_CONTROL
	usb_kill_urb(i1480u->notif_urb);
	usb_free_urb(i1480u->notif_urb);
#endif
	wlp_remove(&i1480u->wlp);
	uwb_rc_put(rc);
	usb_put_dev(i1480u->usb_dev);
}

/** Just setup @net_dev's i1480u private data */
static void i1480u_netdev_setup(struct net_device *net_dev)
{
	struct i1480u *i1480u = netdev_priv(net_dev);
	/* Initialize @i1480u */
	memset(i1480u, 0, sizeof(*i1480u));
	i1480u_init(i1480u);
}

/**
 * Probe a i1480u interface and register it
 *
 * @iface:   USB interface to link to
 * @id:      USB class/subclass/protocol id
 * @returns: 0 if ok, < 0 errno code on error.
 *
 * Does basic housekeeping stuff and then allocs a netdev with space
 * for the i1480u  data. Initializes, registers in i1480u, registers in
 * netdev, ready to go.
 */
static int i1480u_probe(struct usb_interface *iface,
			const struct usb_device_id *id)
{
	int result;
	struct net_device *net_dev;
	struct device *dev = &iface->dev;
	struct i1480u *i1480u;

	/* Allocate instance [calls i1480u_netdev_setup() on it] */
	result = -ENOMEM;
	net_dev = alloc_netdev(sizeof(*i1480u), "wlp%d", i1480u_netdev_setup);
	if (net_dev == NULL) {
		dev_err(dev, "no memory for network device instance\n");
		goto error_alloc_netdev;
	}
	SET_NETDEV_DEV(net_dev, dev);
	i1480u = netdev_priv(net_dev);
	i1480u->net_dev = net_dev;
	result = i1480u_add(i1480u, iface);	/* Now setup all the wlp stuff */
	if (result < 0) {
		dev_err(dev, "cannot add i1480u device: %d\n", result);
		goto error_i1480u_add;
	}
	result = register_netdev(net_dev);	/* Okey dokey, bring it up */
	if (result < 0) {
		dev_err(dev, "cannot register network device: %d\n", result);
		goto error_register_netdev;
	}
	i1480u_sysfs_setup(i1480u);
	if (result < 0)
		goto error_sysfs_init;
	return 0;

error_sysfs_init:
	unregister_netdev(net_dev);
error_register_netdev:
	i1480u_rm(i1480u);
error_i1480u_add:
	free_netdev(net_dev);
error_alloc_netdev:
	return result;
}


/**
 * Disconect a i1480u from the system.
 *
 * i1480u_stop() has been called before, so al the rx and tx contexts
 * have been taken down already. Make sure the queue is stopped,
 * unregister netdev and i1480u, free and kill.
 */
static void i1480u_disconnect(struct usb_interface *iface)
{
	struct i1480u *i1480u;
	struct net_device *net_dev;

	i1480u = usb_get_intfdata(iface);
	net_dev = i1480u->net_dev;
	netif_stop_queue(net_dev);
#ifdef i1480u_FLOW_CONTROL
	usb_kill_urb(i1480u->notif_urb);
#endif
	i1480u_sysfs_release(i1480u);
	unregister_netdev(net_dev);
	i1480u_rm(i1480u);
	free_netdev(net_dev);
}

static struct usb_device_id i1480u_id_table[] = {
	{
		.match_flags = USB_DEVICE_ID_MATCH_DEVICE \
				|  USB_DEVICE_ID_MATCH_DEV_INFO \
				|  USB_DEVICE_ID_MATCH_INT_INFO,
		.idVendor = 0x8086,
		.idProduct = 0x0c3b,
		.bDeviceClass = 0xef,
		.bDeviceSubClass = 0x02,
		.bDeviceProtocol = 0x02,
		.bInterfaceClass = 0xff,
		.bInterfaceSubClass = 0xff,
		.bInterfaceProtocol = 0xff,
	},
	{},
};
MODULE_DEVICE_TABLE(usb, i1480u_id_table);

static struct usb_driver i1480u_driver = {
	.name =		KBUILD_MODNAME,
	.probe =	i1480u_probe,
	.disconnect =	i1480u_disconnect,
	.id_table =	i1480u_id_table,
};

static int __init i1480u_driver_init(void)
{
	return usb_register(&i1480u_driver);
}
module_init(i1480u_driver_init);


static void __exit i1480u_driver_exit(void)
{
	usb_deregister(&i1480u_driver);
}
module_exit(i1480u_driver_exit);

MODULE_AUTHOR("Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>");
MODULE_DESCRIPTION("i1480 Wireless UWB Link WLP networking for USB");
MODULE_LICENSE("GPL");
