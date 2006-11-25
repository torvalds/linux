/*
 * AirPrime CDMA Wireless Serial USB driver
 *
 * Copyright (C) 2005-2006 Greg Kroah-Hartman <gregkh@suse.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x0c88, 0x17da) }, /* Kyocera Wireless KPC650/Passport */
	{ USB_DEVICE(0x1410, 0x1110) }, /* Novatel Wireless Merlin CDMA */
	{ USB_DEVICE(0x1410, 0x1430) },	/* Novatel Merlin XU870 HSDPA/3G */
	{ USB_DEVICE(0x1410, 0x1100) }, /* ExpressCard34 Qualcomm 3G CDMA */
	{ USB_DEVICE(0x413c, 0x8115) }, /* Dell Wireless HSDPA 5500 */
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

#define URB_TRANSFER_BUFFER_SIZE	4096
#define NUM_READ_URBS			4
#define NUM_WRITE_URBS			4
#define NUM_BULK_EPS			3
#define MAX_BULK_EPS			6

/* if overridden by the user, then use their value for the size of the
 * read and write urbs, and the number of endpoints */
static int buffer_size = URB_TRANSFER_BUFFER_SIZE;
static int endpoints = NUM_BULK_EPS;
static int debug;
struct airprime_private {
	spinlock_t lock;
	int outstanding_urbs;
	int throttled;
	struct urb *read_urbp[NUM_READ_URBS];
};

static void airprime_read_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	struct tty_struct *tty;
	int result;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (urb->status) {
		dbg("%s - nonzero read bulk status received: %d",
		    __FUNCTION__, urb->status);
		/* something happened, so free up the memory for this urb */
		if (urb->transfer_buffer) {
			kfree (urb->transfer_buffer);
			urb->transfer_buffer = NULL;
		}
		return;
	}
	usb_serial_debug_data(debug, &port->dev, __FUNCTION__, urb->actual_length, data);

	tty = port->tty;
	if (tty && urb->actual_length) {
		tty_insert_flip_string (tty, data, urb->actual_length);
		tty_flip_buffer_push (tty);
	}

	result = usb_submit_urb (urb, GFP_ATOMIC);
	if (result)
		dev_err(&port->dev, "%s - failed resubmitting read urb, error %d\n",
			__FUNCTION__, result);
	return;
}

static void airprime_write_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct airprime_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	/* free up the transfer buffer, as usb_free_urb() does not do this */
	kfree (urb->transfer_buffer);

	if (urb->status)
		dbg("%s - nonzero write bulk status received: %d",
		    __FUNCTION__, urb->status);
	spin_lock_irqsave(&priv->lock, flags);
	--priv->outstanding_urbs;
	spin_unlock_irqrestore(&priv->lock, flags);

	usb_serial_port_softint(port);
}

static int airprime_open(struct usb_serial_port *port, struct file *filp)
{
	struct airprime_private *priv = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	struct urb *urb;
	char *buffer = NULL;
	int i;
	int result = 0;

	dbg("%s - port %d", __FUNCTION__, port->number);

	/* initialize our private data structure if it isn't already created */
	if (!priv) {
		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (!priv) {
			result = -ENOMEM;
			goto out;
		}
		spin_lock_init(&priv->lock);
		usb_set_serial_port_data(port, priv);
	}

	for (i = 0; i < NUM_READ_URBS; ++i) {
		buffer = kmalloc(buffer_size, GFP_KERNEL);
		if (!buffer) {
			dev_err(&port->dev, "%s - out of memory.\n",
				__FUNCTION__);
			result = -ENOMEM;
			goto errout;
		}
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			kfree(buffer);
			dev_err(&port->dev, "%s - no more urbs?\n",
				__FUNCTION__);
			result = -ENOMEM;
			goto errout;
		}
		usb_fill_bulk_urb(urb, serial->dev,
				  usb_rcvbulkpipe(serial->dev,
						  port->bulk_out_endpointAddress),
				  buffer, buffer_size,
				  airprime_read_bulk_callback, port);
		result = usb_submit_urb(urb, GFP_KERNEL);
		if (result) {
			dev_err(&port->dev,
				"%s - failed submitting read urb %d for port %d, error %d\n",
				__FUNCTION__, i, port->number, result);
			goto errout;
		}
		/* remember this urb so we can kill it when the port is closed */
		priv->read_urbp[i] = urb;
	}
	goto out;

 errout:
	/* some error happened, cancel any submitted urbs and clean up anything that
	   got allocated successfully */

	for ( ; i >= 0; --i) {
		urb = priv->read_urbp[i];
		if (urb) {
			/* This urb was submitted successfully. So we have to
			   cancel it.
			   Unlinking the urb will invoke read_bulk_callback()
			   with an error status, so its transfer buffer will
			   be freed there */
			if (usb_unlink_urb (urb) != -EINPROGRESS) {
				/* comments in drivers/usb/core/urb.c say this
				   can only happen if the urb was never submitted,
				   or has completed already.
				   Either way we may have to free the transfer
				   buffer here. */
				if (urb->transfer_buffer) {
					kfree (urb->transfer_buffer);
					urb->transfer_buffer = NULL;
				}
			}
			usb_free_urb (urb);
		}
	}

 out:
	return result;
}

static void airprime_close(struct usb_serial_port *port, struct file * filp)
{
	struct airprime_private *priv = usb_get_serial_port_data(port);
	int i;

	dbg("%s - port %d", __FUNCTION__, port->number);

	/* killing the urb will invoke read_bulk_callback() with an error status,
	   so the transfer buffer will be freed there */
	for (i = 0; i < NUM_READ_URBS; ++i) {
		usb_kill_urb (priv->read_urbp[i]);
		usb_free_urb (priv->read_urbp[i]);
	}

	/* free up private structure */
	kfree (priv);
	usb_set_serial_port_data(port, NULL);
}

static int airprime_write(struct usb_serial_port *port,
			  const unsigned char *buf, int count)
{
	struct airprime_private *priv = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	struct urb *urb;
	unsigned char *buffer;
	unsigned long flags;
	int status;
	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->outstanding_urbs > NUM_WRITE_URBS) {
		spin_unlock_irqrestore(&priv->lock, flags);
		dbg("%s - write limit hit\n", __FUNCTION__);
		return 0;
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	buffer = kmalloc(count, GFP_ATOMIC);
	if (!buffer) {
		dev_err(&port->dev, "out of memory\n");
		return -ENOMEM;
	}
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		dev_err(&port->dev, "no more free urbs\n");
		kfree (buffer);
		return -ENOMEM;
	}
	memcpy (buffer, buf, count);

	usb_serial_debug_data(debug, &port->dev, __FUNCTION__, count, buffer);

	usb_fill_bulk_urb(urb, serial->dev,
			  usb_sndbulkpipe(serial->dev,
					  port->bulk_out_endpointAddress),
			  buffer, count,
			  airprime_write_bulk_callback, port);

	/* send it down the pipe */
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		dev_err(&port->dev,
			"%s - usb_submit_urb(write bulk) failed with status = %d\n",
			__FUNCTION__, status);
		count = status;
		kfree (buffer);
	} else {
		spin_lock_irqsave(&priv->lock, flags);
		++priv->outstanding_urbs;
		spin_unlock_irqrestore(&priv->lock, flags);
	}
	/* we are done with this urb, so let the host driver
	 * really free it when it is finished with it */
	usb_free_urb (urb);
	return count;
}

static struct usb_driver airprime_driver = {
	.name =		"airprime",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id =	1,
};

static struct usb_serial_driver airprime_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"airprime",
	},
	.id_table =		id_table,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		NUM_DONT_CARE,
	.num_bulk_out =		NUM_DONT_CARE,
	.open =			airprime_open,
	.close =		airprime_close,
	.write =		airprime_write,
};

static int __init airprime_init(void)
{
	int retval;

	airprime_device.num_ports =
		(endpoints > 0 && endpoints <= MAX_BULK_EPS) ? endpoints : NUM_BULK_EPS;
	retval = usb_serial_register(&airprime_device);
	if (retval)
		return retval;
	retval = usb_register(&airprime_driver);
	if (retval)
		usb_serial_deregister(&airprime_device);
	return retval;
}

static void __exit airprime_exit(void)
{
	dbg("%s", __FUNCTION__);

	usb_deregister(&airprime_driver);
	usb_serial_deregister(&airprime_device);
}

module_init(airprime_init);
module_exit(airprime_exit);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled");
module_param(buffer_size, int, 0);
MODULE_PARM_DESC(buffer_size, "Size of the transfer buffers in bytes (default 4096)");
module_param(endpoints, int, 0);
MODULE_PARM_DESC(endpoints, "Number of bulk EPs to configure (default 3)");
