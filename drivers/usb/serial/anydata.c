/*
 * AnyData CDMA Serial USB driver
 *
 * Copyright (C) 2005 Greg Kroah-Hartman <gregkh@suse.de>
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
#include "usb-serial.h"

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x16d5, 0x6501) },	/* AirData CDMA device */
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

/* if overridden by the user, then use their value for the size of the
 * read and write urbs */
static int buffer_size;
static int debug;

static struct usb_driver anydata_driver = {
	.name =		"anydata",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id = 	1,
};

static int anydata_open(struct usb_serial_port *port, struct file *filp)
{
	char *buffer;
	int result = 0;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (buffer_size) {
		/* override the default buffer sizes */
		buffer = kmalloc(buffer_size, GFP_KERNEL);
		if (!buffer) {
			dev_err(&port->dev, "%s - out of memory.\n",
				__FUNCTION__);
			return -ENOMEM;
		}
		kfree (port->read_urb->transfer_buffer);
		port->read_urb->transfer_buffer = buffer;
		port->read_urb->transfer_buffer_length = buffer_size;

		buffer = kmalloc(buffer_size, GFP_KERNEL);
		if (!buffer) {
			dev_err(&port->dev, "%s - out of memory.\n",
				__FUNCTION__);
			return -ENOMEM;
		}
		kfree (port->write_urb->transfer_buffer);
		port->write_urb->transfer_buffer = buffer;
		port->write_urb->transfer_buffer_length = buffer_size;
		port->bulk_out_size = buffer_size;
	}

	/* Start reading from the device */
	usb_fill_bulk_urb(port->read_urb, port->serial->dev,
			  usb_rcvbulkpipe(port->serial->dev,
				  	  port->bulk_in_endpointAddress),
			  port->read_urb->transfer_buffer,
			  port->read_urb->transfer_buffer_length,
			  usb_serial_generic_write_bulk_callback, port);
	result = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (result)
		dev_err(&port->dev,
			"%s - failed submitting read urb, error %d\n",
			__FUNCTION__, result);

	return result;
}

static struct usb_serial_driver anydata_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"anydata",
	},
	.id_table =		id_table,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		NUM_DONT_CARE,
	.num_bulk_out =		NUM_DONT_CARE,
	.num_ports =		1,
	.open =			anydata_open,
};

static int __init anydata_init(void)
{
	int retval;

	retval = usb_serial_register(&anydata_device);
	if (retval)
		return retval;
	retval = usb_register(&anydata_driver);
	if (retval)
		usb_serial_deregister(&anydata_device);
	return retval;
}

static void __exit anydata_exit(void)
{
	usb_deregister(&anydata_driver);
	usb_serial_deregister(&anydata_device);
}

module_init(anydata_init);
module_exit(anydata_exit);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
module_param(buffer_size, int, 0);
MODULE_PARM_DESC(buffer_size, "Size of the transfer buffers");
