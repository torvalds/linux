/*
 * USB Debug cable driver
 *
 * Copyright (C) 2006 Greg Kroah-Hartman <greg@kroah.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define URB_DEBUG_MAX_IN_FLIGHT_URBS	4000
#define USB_DEBUG_MAX_PACKET_SIZE	8
#define USB_DEBUG_BRK_SIZE		8
static char USB_DEBUG_BRK[USB_DEBUG_BRK_SIZE] = {
	0x00,
	0xff,
	0x01,
	0xfe,
	0x00,
	0xfe,
	0x01,
	0xff,
};

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x0525, 0x127a) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver debug_driver = {
	.name =		"debug",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id = 	1,
};

static int usb_debug_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	port->bulk_out_size = USB_DEBUG_MAX_PACKET_SIZE;
	return usb_serial_generic_open(tty, port);
}

/* This HW really does not support a serial break, so one will be
 * emulated when ever the break state is set to true.
 */
static void usb_debug_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	if (!break_state)
		return;
	usb_serial_generic_write(tty, port, USB_DEBUG_BRK, USB_DEBUG_BRK_SIZE);
}

static void usb_debug_read_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;

	if (urb->actual_length == USB_DEBUG_BRK_SIZE &&
	    memcmp(urb->transfer_buffer, USB_DEBUG_BRK,
		   USB_DEBUG_BRK_SIZE) == 0) {
		usb_serial_handle_break(port);
		usb_serial_generic_resubmit_read_urb(port, GFP_ATOMIC);
		return;
	}

	usb_serial_generic_read_bulk_callback(urb);
}

static struct usb_serial_driver debug_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"debug",
	},
	.id_table =		id_table,
	.num_ports =		1,
	.open =			usb_debug_open,
	.max_in_flight_urbs =	URB_DEBUG_MAX_IN_FLIGHT_URBS,
	.break_ctl =		usb_debug_break_ctl,
	.read_bulk_callback =	usb_debug_read_bulk_callback,
};

static int __init debug_init(void)
{
	int retval;

	retval = usb_serial_register(&debug_device);
	if (retval)
		return retval;
	retval = usb_register(&debug_driver);
	if (retval)
		usb_serial_deregister(&debug_device);
	return retval;
}

static void __exit debug_exit(void)
{
	usb_deregister(&debug_driver);
	usb_serial_deregister(&debug_device);
}

module_init(debug_init);
module_exit(debug_exit);
MODULE_LICENSE("GPL");
