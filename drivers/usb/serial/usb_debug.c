/*
 * USB Debug cable driver
 *
 * Copyright (C) 2006 Greg Kroah-Hartman <greg@kroah.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

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

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x0525, 0x127a) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

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

static void usb_debug_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;

	if (urb->actual_length == USB_DEBUG_BRK_SIZE &&
		memcmp(urb->transfer_buffer, USB_DEBUG_BRK,
						USB_DEBUG_BRK_SIZE) == 0) {
		usb_serial_handle_break(port);
		return;
	}

	usb_serial_generic_process_read_urb(urb);
}

static struct usb_serial_driver debug_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"debug",
	},
	.id_table =		id_table,
	.num_ports =		1,
	.bulk_out_size =	USB_DEBUG_MAX_PACKET_SIZE,
	.break_ctl =		usb_debug_break_ctl,
	.process_read_urb =	usb_debug_process_read_urb,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&debug_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);
MODULE_LICENSE("GPL");
