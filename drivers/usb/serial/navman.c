/*
 * Navman Serial USB driver
 *
 * Copyright (C) 2006 Greg Kroah-Hartman <gregkh@suse.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include "usb-serial.h"

static int debug;

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x0a99, 0x0001) },	/* Talon Technology device */
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver navman_driver = {
	.name =		"navman",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id = 	1,
};

static void navman_read_int_callback(struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	struct tty_struct *tty;
	int result;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d",
		    __FUNCTION__, urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d",
		    __FUNCTION__, urb->status);
		goto exit;
	}

	usb_serial_debug_data(debug, &port->dev, __FUNCTION__,
			      urb->actual_length, data);

	tty = port->tty;
	if (tty && urb->actual_length) {
		tty_buffer_request_room(tty, urb->actual_length);
		tty_insert_flip_string(tty, data, urb->actual_length);
		tty_flip_buffer_push(tty);
	}

exit:
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result)
		dev_err(&urb->dev->dev,
			"%s - Error %d submitting interrupt urb\n",
			__FUNCTION__, result);
}

static int navman_open(struct usb_serial_port *port, struct file *filp)
{
	int result = 0;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (port->interrupt_in_urb) {
		dbg("%s - adding interrupt input for treo", __FUNCTION__);
		result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
		if (result)
			dev_err(&port->dev,
				"%s - failed submitting interrupt urb, error %d\n",
				__FUNCTION__, result);
	}
	return result;
}

static void navman_close(struct usb_serial_port *port, struct file *filp)
{
	dbg("%s - port %d", __FUNCTION__, port->number);

	if (port->interrupt_in_urb)
		usb_kill_urb(port->interrupt_in_urb);
}

static int navman_write(struct usb_serial_port *port,
			const unsigned char *buf, int count)
{
	dbg("%s - port %d", __FUNCTION__, port->number);

	/*
	 * This device can't write any data, only read from the device
	 * so we just silently eat all data sent to us and say it was
	 * successfully sent.
	 * Evil, I know, but do you have a better idea?
	 */

	return count;
}

static struct usb_serial_driver navman_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"navman",
	},
	.id_table =		id_table,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		NUM_DONT_CARE,
	.num_bulk_out =		NUM_DONT_CARE,
	.num_ports =		1,
	.open =			navman_open,
	.close = 		navman_close,
	.write = 		navman_write,
	.read_int_callback =	navman_read_int_callback,
};

static int __init navman_init(void)
{
	int retval;

	retval = usb_serial_register(&navman_device);
	if (retval)
		return retval;
	retval = usb_register(&navman_driver);
	if (retval)
		usb_serial_deregister(&navman_device);
	return retval;
}

static void __exit navman_exit(void)
{
	usb_deregister(&navman_driver);
	usb_serial_deregister(&navman_device);
}

module_init(navman_init);
module_exit(navman_exit);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
