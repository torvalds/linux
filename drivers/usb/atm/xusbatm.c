/******************************************************************************
 *  xusbatm.c -	dumb usbatm-based driver for modems initialized in userspace
 *
 *  Copyright (C) 2005 Duncan Sands, Roman Kagan (rkagan % mail ! ru)
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 ******************************************************************************/

#include <linux/module.h>
#include <linux/netdevice.h>		/* FIXME: required by linux/etherdevice.h */
#include <linux/etherdevice.h>		/* for random_ether_addr() */

#include "usbatm.h"


#define XUSBATM_DRIVERS_MAX	8

#define XUSBATM_PARM(name, type, parmtype, desc) \
	static type name[XUSBATM_DRIVERS_MAX]; \
	static int num_##name; \
	module_param_array(name, parmtype, &num_##name, 0444); \
	MODULE_PARM_DESC(name, desc)

XUSBATM_PARM(vendor, unsigned short, ushort, "USB device vendor");
XUSBATM_PARM(product, unsigned short, ushort, "USB device product");

XUSBATM_PARM(rx_endpoint, unsigned char, byte, "rx endpoint number");
XUSBATM_PARM(tx_endpoint, unsigned char, byte, "tx endpoint number");
XUSBATM_PARM(rx_padding, unsigned char, byte, "rx padding (default 0)");
XUSBATM_PARM(tx_padding, unsigned char, byte, "tx padding (default 0)");

static const char xusbatm_driver_name[] = "xusbatm";

static struct usbatm_driver xusbatm_drivers[XUSBATM_DRIVERS_MAX];
static struct usb_device_id xusbatm_usb_ids[XUSBATM_DRIVERS_MAX + 1];
static struct usb_driver xusbatm_usb_driver;

static int usb_intf_has_ep(const struct usb_interface *intf, u8 ep)
{
	int i, j;

	for (i = 0; i < intf->num_altsetting; i++) {
		struct usb_host_interface *alt = intf->altsetting;
		for (j = 0; j < alt->desc.bNumEndpoints; j++)
			if ((alt->endpoint[i].desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK) == ep)
				return 1;
	}
	return 0;
}

static int xusbatm_bind(struct usbatm_data *usbatm_instance,
			struct usb_interface *intf, const struct usb_device_id *id,
			int *need_heavy_init)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	int drv_ix = id - xusbatm_usb_ids;
	int rx_ep_present = usb_intf_has_ep(intf, rx_endpoint[drv_ix]);
	int tx_ep_present = usb_intf_has_ep(intf, tx_endpoint[drv_ix]);
	u8 searched_ep = rx_ep_present ? tx_endpoint[drv_ix] : rx_endpoint[drv_ix];
	int i, ret;

	usb_dbg(usbatm_instance, "%s: binding driver %d: vendor %#x product %#x"
		" rx: ep %#x padd %d tx: ep %#x padd %d\n",
		__func__, drv_ix, vendor[drv_ix], product[drv_ix],
		rx_endpoint[drv_ix], rx_padding[drv_ix],
		tx_endpoint[drv_ix], tx_padding[drv_ix]);

	if (!rx_ep_present && !tx_ep_present) {
		usb_dbg(usbatm_instance, "%s: intf #%d has neither rx (%#x) nor tx (%#x) endpoint\n",
			__func__, intf->altsetting->desc.bInterfaceNumber,
			rx_endpoint[drv_ix], tx_endpoint[drv_ix]);
		return -ENODEV;
	}

	if (rx_ep_present && tx_ep_present)
		return 0;

	for(i = 0; i < usb_dev->actconfig->desc.bNumInterfaces; i++) {
		struct usb_interface *cur_if = usb_dev->actconfig->interface[i];

		if (cur_if != intf && usb_intf_has_ep(cur_if, searched_ep)) {
			ret = usb_driver_claim_interface(&xusbatm_usb_driver,
							 cur_if, usbatm_instance);
			if (!ret)
				usb_err(usbatm_instance, "%s: failed to claim interface #%d (%d)\n",
					__func__, cur_if->altsetting->desc.bInterfaceNumber, ret);
			return ret;
		}
	}

	usb_err(usbatm_instance, "%s: no interface has endpoint %#x\n",
		__func__, searched_ep);
	return -ENODEV;
}

static void xusbatm_unbind(struct usbatm_data *usbatm_instance,
			   struct usb_interface *intf)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	int i;
	usb_dbg(usbatm_instance, "%s entered\n", __func__);

	for(i = 0; i < usb_dev->actconfig->desc.bNumInterfaces; i++) {
		struct usb_interface *cur_if = usb_dev->actconfig->interface[i];
		usb_set_intfdata(cur_if, NULL);
		usb_driver_release_interface(&xusbatm_usb_driver, cur_if);
	}
}

static int xusbatm_atm_start(struct usbatm_data *usbatm_instance,
			     struct atm_dev *atm_dev)
{
	atm_dbg(usbatm_instance, "%s entered\n", __func__);

	/* use random MAC as we've no way to get it from the device */
	random_ether_addr(atm_dev->esi);

	return 0;
}


static int xusbatm_usb_probe(struct usb_interface *intf,
			     const struct usb_device_id *id)
{
	return usbatm_usb_probe(intf, id,
				xusbatm_drivers + (id - xusbatm_usb_ids));
}

static struct usb_driver xusbatm_usb_driver = {
	.owner		= THIS_MODULE,
	.name		= xusbatm_driver_name,
	.probe		= xusbatm_usb_probe,
	.disconnect	= usbatm_usb_disconnect,
	.id_table	= xusbatm_usb_ids
};

static int __init xusbatm_init(void)
{
	int i;

	dbg("xusbatm_init");

	if (!num_vendor ||
	    num_vendor != num_product ||
	    num_vendor != num_rx_endpoint ||
	    num_vendor != num_tx_endpoint) {
		warn("malformed module parameters");
		return -EINVAL;
	}

	for (i = 0; i < num_vendor; i++) {
		xusbatm_usb_ids[i].match_flags	= USB_DEVICE_ID_MATCH_DEVICE;
		xusbatm_usb_ids[i].idVendor	= vendor[i];
		xusbatm_usb_ids[i].idProduct	= product[i];


		xusbatm_drivers[i].owner	= THIS_MODULE;
		xusbatm_drivers[i].driver_name	= xusbatm_driver_name;
		xusbatm_drivers[i].bind		= xusbatm_bind;
		xusbatm_drivers[i].unbind	= xusbatm_unbind;
		xusbatm_drivers[i].atm_start	= xusbatm_atm_start;
		xusbatm_drivers[i].in		= rx_endpoint[i];
		xusbatm_drivers[i].out		= tx_endpoint[i];
		xusbatm_drivers[i].rx_padding	= rx_padding[i];
		xusbatm_drivers[i].tx_padding	= tx_padding[i];
	}

	return usb_register(&xusbatm_usb_driver);
}
module_init(xusbatm_init);

static void __exit xusbatm_exit(void)
{
	dbg("xusbatm_exit entered");

	usb_deregister(&xusbatm_usb_driver);
}
module_exit(xusbatm_exit);

MODULE_AUTHOR("Roman Kagan, Duncan Sands");
MODULE_DESCRIPTION("Driver for USB ADSL modems initialized in userspace");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
