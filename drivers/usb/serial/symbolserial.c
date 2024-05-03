// SPDX-License-Identifier: GPL-2.0
/*
 * Symbol USB barcode to serial driver
 *
 * Copyright (C) 2013 Johan Hovold <jhovold@gmail.com>
 * Copyright (C) 2009 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (C) 2009 Novell Inc.
 */

#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/uaccess.h>

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x05e0, 0x0600) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

struct symbol_private {
	spinlock_t lock;	/* protects the following flags */
	bool throttled;
	bool actually_throttled;
};

static void symbol_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct symbol_private *priv = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	int status = urb->status;
	unsigned long flags;
	int result;
	int data_length;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&port->dev, "%s - urb shutting down with status: %d\n",
			__func__, status);
		return;
	default:
		dev_dbg(&port->dev, "%s - nonzero urb status received: %d\n",
			__func__, status);
		goto exit;
	}

	usb_serial_debug_data(&port->dev, __func__, urb->actual_length, data);

	/*
	 * Data from the device comes with a 1 byte header:
	 *
	 * <size of data> <data>...
	 */
	if (urb->actual_length > 1) {
		data_length = data[0];
		if (data_length > (urb->actual_length - 1))
			data_length = urb->actual_length - 1;
		tty_insert_flip_string(&port->port, &data[1], data_length);
		tty_flip_buffer_push(&port->port);
	} else {
		dev_dbg(&port->dev, "%s - short packet\n", __func__);
	}

exit:
	spin_lock_irqsave(&priv->lock, flags);

	/* Continue trying to always read if we should */
	if (!priv->throttled) {
		result = usb_submit_urb(port->interrupt_in_urb, GFP_ATOMIC);
		if (result)
			dev_err(&port->dev,
			    "%s - failed resubmitting read urb, error %d\n",
							__func__, result);
	} else
		priv->actually_throttled = true;
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int symbol_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct symbol_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	int result = 0;

	spin_lock_irqsave(&priv->lock, flags);
	priv->throttled = false;
	priv->actually_throttled = false;
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Start reading from the device */
	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (result)
		dev_err(&port->dev,
			"%s - failed resubmitting read urb, error %d\n",
			__func__, result);
	return result;
}

static void symbol_close(struct usb_serial_port *port)
{
	usb_kill_urb(port->interrupt_in_urb);
}

static void symbol_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct symbol_private *priv = usb_get_serial_port_data(port);

	spin_lock_irq(&priv->lock);
	priv->throttled = true;
	spin_unlock_irq(&priv->lock);
}

static void symbol_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct symbol_private *priv = usb_get_serial_port_data(port);
	int result;
	bool was_throttled;

	spin_lock_irq(&priv->lock);
	priv->throttled = false;
	was_throttled = priv->actually_throttled;
	priv->actually_throttled = false;
	spin_unlock_irq(&priv->lock);

	if (was_throttled) {
		result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
		if (result)
			dev_err(&port->dev,
				"%s - failed submitting read urb, error %d\n",
							__func__, result);
	}
}

static int symbol_port_probe(struct usb_serial_port *port)
{
	struct symbol_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);

	usb_set_serial_port_data(port, priv);

	return 0;
}

static void symbol_port_remove(struct usb_serial_port *port)
{
	struct symbol_private *priv = usb_get_serial_port_data(port);

	kfree(priv);
}

static struct usb_serial_driver symbol_device = {
	.driver = {
		.name =		"symbol",
	},
	.id_table =		id_table,
	.num_ports =		1,
	.num_interrupt_in =	1,
	.port_probe =		symbol_port_probe,
	.port_remove =		symbol_port_remove,
	.open =			symbol_open,
	.close =		symbol_close,
	.throttle = 		symbol_throttle,
	.unthrottle =		symbol_unthrottle,
	.read_int_callback =	symbol_int_callback,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&symbol_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_DESCRIPTION("Symbol USB barcode to serial driver");
MODULE_LICENSE("GPL v2");
