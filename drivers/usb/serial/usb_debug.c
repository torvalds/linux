// SPDX-License-Identifier: GPL-2.0
/*
 * USB De cable driver
 *
 * Copyright (C) 2006 Greg Kroah-Hartman <greg@kroah.com>
 */

#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define USB_DE_MAX_PACKET_SIZE	8
#define USB_DE_BRK_SIZE		8
static const char USB_DE_BRK[USB_DE_BRK_SIZE] = {
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

static const struct usb_device_id dbc_id_table[] = {
	{ USB_DEVICE(0x1d6b, 0x0010) },
	{ USB_DEVICE(0x1d6b, 0x0011) },
	{ },
};

static const struct usb_device_id id_table_combined[] = {
	{ USB_DEVICE(0x0525, 0x127a) },
	{ USB_DEVICE(0x1d6b, 0x0010) },
	{ USB_DEVICE(0x1d6b, 0x0011) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table_combined);

/* This HW really does not support a serial break, so one will be
 * emulated when ever the break state is set to true.
 */
static void usb_de_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	if (!break_state)
		return;
	usb_serial_generic_write(tty, port, USB_DE_BRK, USB_DE_BRK_SIZE);
}

static void usb_de_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;

	if (urb->actual_length == USB_DE_BRK_SIZE &&
		memcmp(urb->transfer_buffer, USB_DE_BRK,
						USB_DE_BRK_SIZE) == 0) {
		usb_serial_handle_break(port);
		return;
	}

	usb_serial_generic_process_read_urb(urb);
}

static struct usb_serial_driver de_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"de",
	},
	.id_table =		id_table,
	.num_ports =		1,
	.bulk_out_size =	USB_DE_MAX_PACKET_SIZE,
	.break_ctl =		usb_de_break_ctl,
	.process_read_urb =	usb_de_process_read_urb,
};

static struct usb_serial_driver dbc_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"xhci_dbc",
	},
	.id_table =		dbc_id_table,
	.num_ports =		1,
	.break_ctl =		usb_de_break_ctl,
	.process_read_urb =	usb_de_process_read_urb,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&de_device, &dbc_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table_combined);
MODULE_LICENSE("GPL v2");
