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
#include <linux/etherdevice.h>		/* for eth_random_addr() */

#include "usbatm.h"


#define XUSBATM_DRIVERS_MAX	8

#define XUSBATM_PARM(name, type, parmtype, desc) \
	static type name[XUSBATM_DRIVERS_MAX]; \
	static unsigned int num_##name; \
	module_param_array(name, parmtype, &num_##name, 0444); \
	MODULE_PARM_DESC(name, desc)

XUSBATM_PARM(vendor, unsigned short, ushort, "USB device vendor");
XUSBATM_PARM(product, unsigned short, ushort, "USB device product");

XUSBATM_PARM(rx_endpoint, unsigned char, byte, "rx endpoint number");
XUSBATM_PARM(tx_endpoint, unsigned char, byte, "tx endpoint number");
XUSBATM_PARM(rx_padding, unsigned char, byte, "rx padding (default 0)");
XUSBATM_PARM(tx_padding, unsigned char, byte, "tx padding (default 0)");
XUSBATM_PARM(rx_altsetting, unsigned char, byte, "rx altsetting (default 0)");
XUSBATM_PARM(tx_altsetting, unsigned char, byte, "rx altsetting (default 0)");

static const char xusbatm_driver_name[] = "xusbatm";

static struct usbatm_driver xusbatm_drivers[XUSBATM_DRIVERS_MAX];
static struct usb_device_id xusbatm_usb_ids[XUSBATM_DRIVERS_MAX + 1];
static struct usb_driver xusbatm_usb_driver;

static struct usb_interface *xusbatm_find_intf(struct usb_device *usb_dev, int altsetting, u8 ep)
{
	struct usb_host_interface *alt;
	struct usb_interface *intf;
	int i, j;

	for (i = 0; i < usb_dev->actconfig->desc.bNumInterfaces; i++)
		if ((intf = usb_dev->actconfig->interface[i]) && (alt = usb_altnum_to_altsetting(intf, altsetting)))
			for (j = 0; j < alt->desc.bNumEndpoints; j++)
				if (alt->endpoint[j].desc.bEndpointAddress == ep)
					return intf;
	return NULL;
}

static int xusbatm_capture_intf(struct usbatm_data *usbatm, struct usb_device *usb_dev,
		struct usb_interface *intf, int altsetting, int claim)
{
	int ifnum = intf->altsetting->desc.bInterfaceNumber;
	int ret;

	if (claim && (ret = usb_driver_claim_interface(&xusbatm_usb_driver, intf, usbatm))) {
		usb_err(usbatm, "%s: failed to claim interface %2d (%d)!\n", __func__, ifnum, ret);
		return ret;
	}
	if ((ret = usb_set_interface(usb_dev, ifnum, altsetting))) {
		usb_err(usbatm, "%s: altsetting %2d for interface %2d failed (%d)!\n", __func__, altsetting, ifnum, ret);
		return ret;
	}
	return 0;
}

static void xusbatm_release_intf(struct usb_device *usb_dev, struct usb_interface *intf, int claimed)
{
	if (claimed) {
		usb_set_intfdata(intf, NULL);
		usb_driver_release_interface(&xusbatm_usb_driver, intf);
	}
}

static int xusbatm_bind(struct usbatm_data *usbatm,
			struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	int drv_ix = id - xusbatm_usb_ids;
	int rx_alt = rx_altsetting[drv_ix];
	int tx_alt = tx_altsetting[drv_ix];
	struct usb_interface *rx_intf = xusbatm_find_intf(usb_dev, rx_alt, rx_endpoint[drv_ix]);
	struct usb_interface *tx_intf = xusbatm_find_intf(usb_dev, tx_alt, tx_endpoint[drv_ix]);
	int ret;

	usb_dbg(usbatm, "%s: binding driver %d: vendor %04x product %04x"
		" rx: ep %02x padd %d alt %2d tx: ep %02x padd %d alt %2d\n",
		__func__, drv_ix, vendor[drv_ix], product[drv_ix],
		rx_endpoint[drv_ix], rx_padding[drv_ix], rx_alt,
		tx_endpoint[drv_ix], tx_padding[drv_ix], tx_alt);

	if (!rx_intf || !tx_intf) {
		if (!rx_intf)
			usb_dbg(usbatm, "%s: no interface contains endpoint %02x in altsetting %2d\n",
				__func__, rx_endpoint[drv_ix], rx_alt);
		if (!tx_intf)
			usb_dbg(usbatm, "%s: no interface contains endpoint %02x in altsetting %2d\n",
				__func__, tx_endpoint[drv_ix], tx_alt);
		return -ENODEV;
	}

	if ((rx_intf != intf) && (tx_intf != intf))
		return -ENODEV;

	if ((rx_intf == tx_intf) && (rx_alt != tx_alt)) {
		usb_err(usbatm, "%s: altsettings clash on interface %2d (%2d vs %2d)!\n", __func__,
				rx_intf->altsetting->desc.bInterfaceNumber, rx_alt, tx_alt);
		return -EINVAL;
	}

	usb_dbg(usbatm, "%s: rx If#=%2d; tx If#=%2d\n", __func__,
			rx_intf->altsetting->desc.bInterfaceNumber,
			tx_intf->altsetting->desc.bInterfaceNumber);

	if ((ret = xusbatm_capture_intf(usbatm, usb_dev, rx_intf, rx_alt, rx_intf != intf)))
		return ret;

	if ((tx_intf != rx_intf) && (ret = xusbatm_capture_intf(usbatm, usb_dev, tx_intf, tx_alt, tx_intf != intf))) {
		xusbatm_release_intf(usb_dev, rx_intf, rx_intf != intf);
		return ret;
	}

	return 0;
}

static void xusbatm_unbind(struct usbatm_data *usbatm,
			   struct usb_interface *intf)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	int i;

	usb_dbg(usbatm, "%s entered\n", __func__);

	for (i = 0; i < usb_dev->actconfig->desc.bNumInterfaces; i++) {
		struct usb_interface *cur_intf = usb_dev->actconfig->interface[i];

		if (cur_intf && (usb_get_intfdata(cur_intf) == usbatm)) {
			usb_set_intfdata(cur_intf, NULL);
			usb_driver_release_interface(&xusbatm_usb_driver, cur_intf);
		}
	}
}

static int xusbatm_atm_start(struct usbatm_data *usbatm,
			     struct atm_dev *atm_dev)
{
	atm_dbg(usbatm, "%s entered\n", __func__);

	/* use random MAC as we've no way to get it from the device */
	eth_random_addr(atm_dev->esi);

	return 0;
}


static int xusbatm_usb_probe(struct usb_interface *intf,
			     const struct usb_device_id *id)
{
	return usbatm_usb_probe(intf, id,
				xusbatm_drivers + (id - xusbatm_usb_ids));
}

static struct usb_driver xusbatm_usb_driver = {
	.name		= xusbatm_driver_name,
	.probe		= xusbatm_usb_probe,
	.disconnect	= usbatm_usb_disconnect,
	.id_table	= xusbatm_usb_ids
};

static int __init xusbatm_init(void)
{
	int i;

	if (!num_vendor ||
	    num_vendor != num_product ||
	    num_vendor != num_rx_endpoint ||
	    num_vendor != num_tx_endpoint) {
		printk(KERN_WARNING "xusbatm: malformed module parameters\n");
		return -EINVAL;
	}

	for (i = 0; i < num_vendor; i++) {
		rx_endpoint[i] |= USB_DIR_IN;
		tx_endpoint[i] &= USB_ENDPOINT_NUMBER_MASK;

		xusbatm_usb_ids[i].match_flags	= USB_DEVICE_ID_MATCH_DEVICE;
		xusbatm_usb_ids[i].idVendor	= vendor[i];
		xusbatm_usb_ids[i].idProduct	= product[i];

		xusbatm_drivers[i].driver_name	= xusbatm_driver_name;
		xusbatm_drivers[i].bind		= xusbatm_bind;
		xusbatm_drivers[i].unbind	= xusbatm_unbind;
		xusbatm_drivers[i].atm_start	= xusbatm_atm_start;
		xusbatm_drivers[i].bulk_in	= rx_endpoint[i];
		xusbatm_drivers[i].bulk_out	= tx_endpoint[i];
		xusbatm_drivers[i].rx_padding	= rx_padding[i];
		xusbatm_drivers[i].tx_padding	= tx_padding[i];
	}

	return usb_register(&xusbatm_usb_driver);
}
module_init(xusbatm_init);

static void __exit xusbatm_exit(void)
{
	usb_deregister(&xusbatm_usb_driver);
}
module_exit(xusbatm_exit);

MODULE_AUTHOR("Roman Kagan, Duncan Sands");
MODULE_DESCRIPTION("Driver for USB ADSL modems initialized in userspace");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
