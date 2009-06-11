/*
 * Symbol USB barcode to serial driver
 *
 * Copyright (C) 2009 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (C) 2009 Novell Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/uaccess.h>

static int debug;

static struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x05e0, 0x0600) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

/* This structure holds all of the individual device information */
struct symbol_private {
	struct usb_device *udev;
	struct usb_serial *serial;
	struct usb_serial_port *port;
	unsigned char *int_buffer;
	struct urb *int_urb;
	int buffer_size;
	u8 bInterval;
	u8 int_address;
	spinlock_t lock;	/* protects the following flags */
	bool throttled;
	bool actually_throttled;
	bool rts;
};

static void symbol_int_callback(struct urb *urb)
{
	struct symbol_private *priv = urb->context;
	unsigned char *data = urb->transfer_buffer;
	struct usb_serial_port *port = priv->port;
	int status = urb->status;
	struct tty_struct *tty;
	int result;
	int available_room = 0;
	int data_length;

	dbg("%s - port %d", __func__, port->number);

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d",
		    __func__, status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d",
		    __func__, status);
		goto exit;
	}

	usb_serial_debug_data(debug, &port->dev, __func__, urb->actual_length,
			      data);

	if (urb->actual_length > 1) {
		data_length = urb->actual_length - 1;

		/*
		 * Data from the device comes with a 1 byte header:
		 *
		 * <size of data>data...
		 * 	This is real data to be sent to the tty layer
		 * we pretty much just ignore the size and send everything
		 * else to the tty layer.
		 */
		tty = tty_port_tty_get(&port->port);
		if (tty) {
			available_room = tty_buffer_request_room(tty,
							data_length);
			if (available_room) {
				tty_insert_flip_string(tty, &data[1],
						       available_room);
				tty_flip_buffer_push(tty);
			}
			tty_kref_put(tty);
		}
	} else {
		dev_dbg(&priv->udev->dev,
			"Improper ammount of data received from the device, "
			"%d bytes", urb->actual_length);
	}

exit:
	spin_lock(&priv->lock);

	/* Continue trying to always read if we should */
	if (!priv->throttled) {
		usb_fill_int_urb(priv->int_urb, priv->udev,
				 usb_rcvintpipe(priv->udev,
				 		priv->int_address),
				 priv->int_buffer, priv->buffer_size,
				 symbol_int_callback, priv, priv->bInterval);
		result = usb_submit_urb(priv->int_urb, GFP_ATOMIC);
		if (result)
			dev_err(&port->dev,
			    "%s - failed resubmitting read urb, error %d\n",
							__func__, result);
	} else
		priv->actually_throttled = true;
	spin_unlock(&priv->lock);
}

static int symbol_open(struct tty_struct *tty, struct usb_serial_port *port,
			struct file *filp)
{
	struct symbol_private *priv = usb_get_serial_data(port->serial);
	unsigned long flags;
	int result = 0;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	priv->throttled = false;
	priv->actually_throttled = false;
	priv->port = port;
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Start reading from the device */
	usb_fill_int_urb(priv->int_urb, priv->udev,
			 usb_rcvintpipe(priv->udev, priv->int_address),
			 priv->int_buffer, priv->buffer_size,
			 symbol_int_callback, priv, priv->bInterval);
	result = usb_submit_urb(priv->int_urb, GFP_KERNEL);
	if (result)
		dev_err(&port->dev,
			"%s - failed resubmitting read urb, error %d\n",
			__func__, result);
	return result;
}

static void symbol_close(struct usb_serial_port *port)
{
	struct symbol_private *priv = usb_get_serial_data(port->serial);

	dbg("%s - port %d", __func__, port->number);

	/* shutdown our urbs */
	usb_kill_urb(priv->int_urb);
}

static void symbol_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct symbol_private *priv = usb_get_serial_data(port->serial);
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);
	spin_lock_irqsave(&priv->lock, flags);
	priv->throttled = true;
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void symbol_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct symbol_private *priv = usb_get_serial_data(port->serial);
	unsigned long flags;
	int result;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	priv->throttled = false;
	priv->actually_throttled = false;
	spin_unlock_irqrestore(&priv->lock, flags);

	priv->int_urb->dev = port->serial->dev;
	result = usb_submit_urb(priv->int_urb, GFP_ATOMIC);
	if (result)
		dev_err(&port->dev,
			"%s - failed submitting read urb, error %d\n",
							__func__, result);
}

static int symbol_startup(struct usb_serial *serial)
{
	struct symbol_private *priv;
	struct usb_host_interface *intf;
	int i;
	int retval = -ENOMEM;
	bool int_in_found = false;

	/* create our private serial structure */
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (priv == NULL) {
		dev_err(&serial->dev->dev, "%s - Out of memory\n", __func__);
		return -ENOMEM;
	}
	spin_lock_init(&priv->lock);
	priv->serial = serial;
	priv->port = serial->port[0];
	priv->udev = serial->dev;

	/* find our interrupt endpoint */
	intf = serial->interface->altsetting;
	for (i = 0; i < intf->desc.bNumEndpoints; ++i) {
		struct usb_endpoint_descriptor *endpoint;

		endpoint = &intf->endpoint[i].desc;
		if (!usb_endpoint_is_int_in(endpoint))
			continue;

		priv->int_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!priv->int_urb) {
			dev_err(&priv->udev->dev, "out of memory\n");
			goto error;
		}

		priv->buffer_size = le16_to_cpu(endpoint->wMaxPacketSize) * 2;
		priv->int_buffer = kmalloc(priv->buffer_size, GFP_KERNEL);
		if (!priv->int_buffer) {
			dev_err(&priv->udev->dev, "out of memory\n");
			goto error;
		}

		priv->int_address = endpoint->bEndpointAddress;
		priv->bInterval = endpoint->bInterval;

		/* set up our int urb */
		usb_fill_int_urb(priv->int_urb, priv->udev,
				 usb_rcvintpipe(priv->udev,
				 		endpoint->bEndpointAddress),
				 priv->int_buffer, priv->buffer_size,
				 symbol_int_callback, priv, priv->bInterval);

		int_in_found = true;
		break;
		}

	if (!int_in_found) {
		dev_err(&priv->udev->dev,
			"Error - the proper endpoints were not found!\n");
		goto error;
	}

	usb_set_serial_data(serial, priv);
	return 0;

error:
	usb_free_urb(priv->int_urb);
	kfree(priv->int_buffer);
	kfree(priv);
	return retval;
}

static void symbol_shutdown(struct usb_serial *serial)
{
	struct symbol_private *priv = usb_get_serial_data(serial);

	dbg("%s", __func__);

	usb_kill_urb(priv->int_urb);
	usb_free_urb(priv->int_urb);
	kfree(priv->int_buffer);
	kfree(priv);
	usb_set_serial_data(serial, NULL);
}

static struct usb_driver symbol_driver = {
	.name =			"symbol",
	.probe =		usb_serial_probe,
	.disconnect =		usb_serial_disconnect,
	.id_table =		id_table,
	.no_dynamic_id = 	1,
};

static struct usb_serial_driver symbol_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"symbol",
	},
	.id_table =		id_table,
	.usb_driver = 		&symbol_driver,
	.num_ports =		1,
	.attach =		symbol_startup,
	.open =			symbol_open,
	.close =		symbol_close,
	.shutdown =		symbol_shutdown,
	.throttle = 		symbol_throttle,
	.unthrottle =		symbol_unthrottle,
};

static int __init symbol_init(void)
{
	int retval;

	retval = usb_serial_register(&symbol_device);
	if (retval)
		return retval;
	retval = usb_register(&symbol_driver);
	if (retval)
		usb_serial_deregister(&symbol_device);
	return retval;
}

static void __exit symbol_exit(void)
{
	usb_deregister(&symbol_driver);
	usb_serial_deregister(&symbol_device);
}

module_init(symbol_init);
module_exit(symbol_exit);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
