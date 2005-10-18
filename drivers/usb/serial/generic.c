/*
 * USB Serial Converter Generic functions
 *
 * Copyright (C) 1999 - 2002 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/usb.h>
#include <asm/uaccess.h>
#include "usb-serial.h"

static int debug;

#ifdef CONFIG_USB_SERIAL_GENERIC
static __u16 vendor  = 0x05f9;
static __u16 product = 0xffff;

module_param(vendor, ushort, 0);
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

module_param(product, ushort, 0);
MODULE_PARM_DESC(product, "User specified USB idProduct");

static struct usb_device_id generic_device_ids[2]; /* Initially all zeroes. */

/* All of the device info needed for the Generic Serial Converter */
struct usb_serial_device_type usb_serial_generic_device = {
	.owner =		THIS_MODULE,
	.name =			"Generic",
	.short_name =		"generic",
	.id_table =		generic_device_ids,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		NUM_DONT_CARE,
	.num_bulk_out =		NUM_DONT_CARE,
	.num_ports =		1,
	.shutdown =		usb_serial_generic_shutdown,
};

/* we want to look at all devices, as the vendor/product id can change
 * depending on the command line argument */
static struct usb_device_id generic_serial_ids[] = {
	{.driver_info = 42},
	{}
};

static int generic_probe(struct usb_interface *interface,
			       const struct usb_device_id *id)
{
	const struct usb_device_id *id_pattern;

	id_pattern = usb_match_id(interface, generic_device_ids);
	if (id_pattern != NULL)
		return usb_serial_probe(interface, id);
	return -ENODEV;
}

static struct usb_driver generic_driver = {
	.owner =	THIS_MODULE,
	.name =		"usbserial_generic",
	.probe =	generic_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	generic_serial_ids,
};
#endif

int usb_serial_generic_register (int _debug)
{
	int retval = 0;

	debug = _debug;
#ifdef CONFIG_USB_SERIAL_GENERIC
	generic_device_ids[0].idVendor = vendor;
	generic_device_ids[0].idProduct = product;
	generic_device_ids[0].match_flags = USB_DEVICE_ID_MATCH_VENDOR | USB_DEVICE_ID_MATCH_PRODUCT;

	/* register our generic driver with ourselves */
	retval = usb_serial_register (&usb_serial_generic_device);
	if (retval)
		goto exit;
	retval = usb_register(&generic_driver);
	if (retval)
		usb_serial_deregister(&usb_serial_generic_device);
exit:
#endif
	return retval;
}

void usb_serial_generic_deregister (void)
{
#ifdef CONFIG_USB_SERIAL_GENERIC
	/* remove our generic driver */
	usb_deregister(&generic_driver);
	usb_serial_deregister (&usb_serial_generic_device);
#endif
}

int usb_serial_generic_open (struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial = port->serial;
	int result = 0;

	dbg("%s - port %d", __FUNCTION__, port->number);

	/* force low_latency on so that our tty_push actually forces the data through, 
	   otherwise it is scheduled, and with high data rates (like with OHCI) data
	   can get lost. */
	if (port->tty)
		port->tty->low_latency = 1;

	/* if we have a bulk interrupt, start reading from it */
	if (serial->num_bulk_in) {
		/* Start reading from the device */
		usb_fill_bulk_urb (port->read_urb, serial->dev,
				   usb_rcvbulkpipe(serial->dev, port->bulk_in_endpointAddress),
				   port->read_urb->transfer_buffer,
				   port->read_urb->transfer_buffer_length,
				   ((serial->type->read_bulk_callback) ?
				     serial->type->read_bulk_callback :
				     usb_serial_generic_read_bulk_callback),
				   port);
		result = usb_submit_urb(port->read_urb, GFP_KERNEL);
		if (result)
			dev_err(&port->dev, "%s - failed resubmitting read urb, error %d\n", __FUNCTION__, result);
	}

	return result;
}

static void generic_cleanup (struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (serial->dev) {
		/* shutdown any bulk reads that might be going on */
		if (serial->num_bulk_out)
			usb_kill_urb(port->write_urb);
		if (serial->num_bulk_in)
			usb_kill_urb(port->read_urb);
	}
}

void usb_serial_generic_close (struct usb_serial_port *port, struct file * filp)
{
	dbg("%s - port %d", __FUNCTION__, port->number);
	generic_cleanup (port);
}

int usb_serial_generic_write(struct usb_serial_port *port, const unsigned char *buf, int count)
{
	struct usb_serial *serial = port->serial;
	int result;
	unsigned char *data;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (count == 0) {
		dbg("%s - write request of 0 bytes", __FUNCTION__);
		return (0);
	}

	/* only do something if we have a bulk out endpoint */
	if (serial->num_bulk_out) {
		spin_lock(&port->lock);
		if (port->write_urb_busy) {
			spin_unlock(&port->lock);
			dbg("%s - already writing", __FUNCTION__);
			return 0;
		}
		port->write_urb_busy = 1;
		spin_unlock(&port->lock);

		count = (count > port->bulk_out_size) ? port->bulk_out_size : count;

		memcpy (port->write_urb->transfer_buffer, buf, count);
		data = port->write_urb->transfer_buffer;
		usb_serial_debug_data(debug, &port->dev, __FUNCTION__, count, data);

		/* set up our urb */
		usb_fill_bulk_urb (port->write_urb, serial->dev,
				   usb_sndbulkpipe (serial->dev,
						    port->bulk_out_endpointAddress),
				   port->write_urb->transfer_buffer, count,
				   ((serial->type->write_bulk_callback) ? 
				     serial->type->write_bulk_callback :
				     usb_serial_generic_write_bulk_callback), port);

		/* send the data out the bulk port */
		port->write_urb_busy = 1;
		result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
		if (result) {
			dev_err(&port->dev, "%s - failed submitting write urb, error %d\n", __FUNCTION__, result);
			/* don't have to grab the lock here, as we will retry if != 0 */
			port->write_urb_busy = 0;
		} else
			result = count;

		return result;
	}

	/* no bulk out, so return 0 bytes written */
	return 0;
}

int usb_serial_generic_write_room (struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	int room = 0;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (serial->num_bulk_out) {
		if (!(port->write_urb_busy))
			room = port->bulk_out_size;
	}

	dbg("%s - returns %d", __FUNCTION__, room);
	return (room);
}

int usb_serial_generic_chars_in_buffer (struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	int chars = 0;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (serial->num_bulk_out) {
		if (port->write_urb_busy)
			chars = port->write_urb->transfer_buffer_length;
	}

	dbg("%s - returns %d", __FUNCTION__, chars);
	return (chars);
}

void usb_serial_generic_read_bulk_callback (struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = port->serial;
	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;
	int i;
	int result;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (urb->status) {
		dbg("%s - nonzero read bulk status received: %d", __FUNCTION__, urb->status);
		return;
	}

	usb_serial_debug_data(debug, &port->dev, __FUNCTION__, urb->actual_length, data);

	tty = port->tty;
	if (tty && urb->actual_length) {
		for (i = 0; i < urb->actual_length ; ++i) {
			/* if we insert more than TTY_FLIPBUF_SIZE characters, we drop them. */
			if(tty->flip.count >= TTY_FLIPBUF_SIZE) {
				tty_flip_buffer_push(tty);
			}
			/* this doesn't actually push the data through unless tty->low_latency is set */
			tty_insert_flip_char(tty, data[i], 0);
		}
	  	tty_flip_buffer_push(tty);
	}

	/* Continue trying to always read  */
	usb_fill_bulk_urb (port->read_urb, serial->dev,
			   usb_rcvbulkpipe (serial->dev,
				   	    port->bulk_in_endpointAddress),
			   port->read_urb->transfer_buffer,
			   port->read_urb->transfer_buffer_length,
			   ((serial->type->read_bulk_callback) ? 
			     serial->type->read_bulk_callback : 
			     usb_serial_generic_read_bulk_callback), port);
	result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
	if (result)
		dev_err(&port->dev, "%s - failed resubmitting read urb, error %d\n", __FUNCTION__, result);
}

void usb_serial_generic_write_bulk_callback (struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;

	dbg("%s - port %d", __FUNCTION__, port->number);

	port->write_urb_busy = 0;
	if (urb->status) {
		dbg("%s - nonzero write bulk status received: %d", __FUNCTION__, urb->status);
		return;
	}

	usb_serial_port_softint((void *)port);

	schedule_work(&port->work);
}

void usb_serial_generic_shutdown (struct usb_serial *serial)
{
	int i;

	dbg("%s", __FUNCTION__);

	/* stop reads and writes on all ports */
	for (i=0; i < serial->num_ports; ++i) {
		generic_cleanup(serial->port[i]);
	}
}

