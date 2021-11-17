// SPDX-License-Identifier: GPL-2.0+
/*
 * USB Wishbone-Serial adapter driver
 *
 * Copyright (C) 2013 Wesley W. Terpstra <w.terpstra@gsi.de>
 * Copyright (C) 2013 GSI Helmholtz Centre for Heavy Ion Research GmbH
 */

#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/uaccess.h>

#define GSI_VENDOR_OPENCLOSE 0xB0

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1D50, 0x6062, 0xFF, 0xFF, 0xFF) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

/*
 * Etherbone must be told that a new stream has begun before data arrives.
 * This is necessary to restart the negotiation of Wishbone bus parameters.
 * Similarly, when the stream ends, Etherbone must be told so that the cycle
 * line can be driven low in the case that userspace failed to do so.
 */
static int usb_gsi_openclose(struct usb_serial_port *port, int value)
{
	struct usb_device *dev = port->serial->dev;

	return usb_control_msg(
		dev,
		usb_sndctrlpipe(dev, 0), /* Send to EP0OUT */
		GSI_VENDOR_OPENCLOSE,
		USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
		value, /* wValue = device is open(1) or closed(0) */
		port->serial->interface->cur_altsetting->desc.bInterfaceNumber,
		NULL, 0,  /* There is no data stage */
		5000); /* Timeout till operation fails */
}

static int wishbone_serial_open(struct tty_struct *tty,
				struct usb_serial_port *port)
{
	int retval;

	retval = usb_gsi_openclose(port, 1);
	if (retval) {
		dev_err(&port->serial->dev->dev,
		       "Could not mark device as open (%d)\n",
		       retval);
		return retval;
	}

	retval = usb_serial_generic_open(tty, port);
	if (retval)
		usb_gsi_openclose(port, 0);

	return retval;
}

static void wishbone_serial_close(struct usb_serial_port *port)
{
	usb_serial_generic_close(port);
	usb_gsi_openclose(port, 0);
}

static struct usb_serial_driver wishbone_serial_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"wishbone_serial",
	},
	.id_table =		id_table,
	.num_ports =		1,
	.open =			&wishbone_serial_open,
	.close =		&wishbone_serial_close,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&wishbone_serial_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR("Wesley W. Terpstra <w.terpstra@gsi.de>");
MODULE_DESCRIPTION("USB Wishbone-Serial adapter");
MODULE_LICENSE("GPL");
