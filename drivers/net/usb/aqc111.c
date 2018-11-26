// SPDX-License-Identifier: GPL-2.0-or-later
/* Aquantia Corp. Aquantia AQtion USB to 5GbE Controller
 * Copyright (C) 2003-2005 David Hollis <dhollis@davehollis.com>
 * Copyright (C) 2005 Phil Chang <pchang23@sbcglobal.net>
 * Copyright (C) 2002-2003 TiVo Inc.
 * Copyright (C) 2017-2018 ASIX
 * Copyright (C) 2018 Aquantia Corp.
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>

static const struct net_device_ops aqc111_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
};

static int aqc111_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	int ret;

	/* Check if vendor configuration */
	if (udev->actconfig->desc.bConfigurationValue != 1) {
		usb_driver_set_configuration(udev, 1);
		return -ENODEV;
	}

	usb_reset_configuration(dev->udev);

	ret = usbnet_get_endpoints(dev, intf);
	if (ret < 0) {
		netdev_dbg(dev->net, "usbnet_get_endpoints failed");
		return ret;
	}

	dev->net->netdev_ops = &aqc111_netdev_ops;

	return 0;
}

static void aqc111_unbind(struct usbnet *dev, struct usb_interface *intf)
{
}

static const struct driver_info aqc111_info = {
	.description	= "Aquantia AQtion USB to 5GbE Controller",
	.bind		= aqc111_bind,
	.unbind		= aqc111_unbind,
};

#define AQC111_USB_ETH_DEV(vid, pid, table) \
	USB_DEVICE_INTERFACE_CLASS((vid), (pid), USB_CLASS_VENDOR_SPEC), \
	.driver_info = (unsigned long)&(table) \
}, \
{ \
	USB_DEVICE_AND_INTERFACE_INFO((vid), (pid), \
				      USB_CLASS_COMM, \
				      USB_CDC_SUBCLASS_ETHERNET, \
				      USB_CDC_PROTO_NONE), \
	.driver_info = (unsigned long)&(table),

static const struct usb_device_id products[] = {
	{AQC111_USB_ETH_DEV(0x2eca, 0xc101, aqc111_info)},
	{ },/* END */
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver aq_driver = {
	.name		= "aqc111",
	.id_table	= products,
	.probe		= usbnet_probe,
	.disconnect	= usbnet_disconnect,
};

module_usb_driver(aq_driver);

MODULE_DESCRIPTION("Aquantia AQtion USB to 5/2.5GbE Controllers");
MODULE_LICENSE("GPL");
