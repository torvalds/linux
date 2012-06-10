/*
 * Motorola USB Phone driver
 *
 * Copyright (C) 2008 Greg Kroah-Hartman <greg@kroah.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 * {sigh}
 * Motorola should be using the CDC ACM USB spec, but instead
 * they try to just "do their own thing"...  This driver should handle a
 * few phones in which a basic "dumb serial connection" is needed to be
 * able to get a connection through to them.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x05c6, 0x3197) },	/* unknown Motorola phone */
	{ USB_DEVICE(0x0c44, 0x0022) },	/* unknown Mororola phone */
	{ USB_DEVICE(0x22b8, 0x2a64) },	/* Motorola KRZR K1m */
	{ USB_DEVICE(0x22b8, 0x2c84) }, /* Motorola VE240 phone */
	{ USB_DEVICE(0x22b8, 0x2c64) }, /* Motorola V950 phone */
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_serial_driver moto_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"moto-modem",
	},
	.id_table =		id_table,
	.num_ports =		1,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&moto_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);
MODULE_LICENSE("GPL");
