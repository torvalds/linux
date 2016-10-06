/*
 *  REINER SCT cyberJack pinpad/e-com USB Chipcard Reader Driver
 *
 *  Copyright (C) 2001  REINER SCT
 *  Author: Matthias Bruestle
 *
 *  Contact: support@reiner-sct.com (see MAINTAINERS)
 *
 *  This program is largely derived from work by the linux-usb group
 *  and associated source files.  Please see the usb/serial files for
 *  individual credits and copyrights.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Thanks to Greg Kroah-Hartman (greg@kroah.com) for his help and
 *  patience.
 *
 *  In case of problems, please write to the contact e-mail address
 *  mentioned above.
 *
 *  Please note that later models of the cyberjack reader family are
 *  supported by a libusb-based userspace device driver.
 *
 *  Homepage: http://www.reiner-sct.de/support/treiber_cyberjack.php#linux
 */


#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define CYBERJACK_LOCAL_BUF_SIZE 32

#define DRIVER_AUTHOR "Matthias Bruestle"
#define DRIVER_DESC "REINER SCT cyberJack pinpad/e-com USB Chipcard Reader Driver"


#define CYBERJACK_VENDOR_ID	0x0C4B
#define CYBERJACK_PRODUCT_ID	0x0100

/* Function prototypes */
static int cyberjack_port_probe(struct usb_serial_port *port);
static int cyberjack_port_remove(struct usb_serial_port *port);
static int  cyberjack_open(struct tty_struct *tty,
	struct usb_serial_port *port);
static void cyberjack_close(struct usb_serial_port *port);
static int cyberjack_write(struct tty_struct *tty,
	struct usb_serial_port *port, const unsigned char *buf, int count);
static int cyberjack_write_room(struct tty_struct *tty);
static void cyberjack_read_int_callback(struct urb *urb);
static void cyberjack_read_bulk_callback(struct urb *urb);
static void cyberjack_write_bulk_callback(struct urb *urb);

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(CYBERJACK_VENDOR_ID, CYBERJACK_PRODUCT_ID) },
	{ }			/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_serial_driver cyberjack_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"cyberjack",
	},
	.description =		"Reiner SCT Cyberjack USB card reader",
	.id_table =		id_table,
	.num_ports =		1,
	.port_probe =		cyberjack_port_probe,
	.port_remove =		cyberjack_port_remove,
	.open =			cyberjack_open,
	.close =		cyberjack_close,
	.write =		cyberjack_write,
	.write_room =		cyberjack_write_room,
	.read_int_callback =	cyberjack_read_int_callback,
	.read_bulk_callback =	cyberjack_read_bulk_callback,
	.write_bulk_callback =	cyberjack_write_bulk_callback,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&cyberjack_device, NULL
};

struct cyberjack_private {
	spinlock_t	lock;		/* Lock for SMP */
	short		rdtodo;		/* Bytes still to read */
	unsigned char	wrbuf[5*64];	/* Buffer for collecting data to write */
	short		wrfilled;	/* Overall data size we already got */
	short		wrsent;		/* Data already sent */
};

static int cyberjack_port_probe(struct usb_serial_port *port)
{
	struct cyberjack_private *priv;
	int result;

	priv = kmalloc(sizeof(struct cyberjack_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	priv->rdtodo = 0;
	priv->wrfilled = 0;
	priv->wrsent = 0;

	usb_set_serial_port_data(port, priv);

	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (result)
		dev_err(&port->dev, "usb_submit_urb(read int) failed\n");

	return 0;
}

static int cyberjack_port_remove(struct usb_serial_port *port)
{
	struct cyberjack_private *priv;

	usb_kill_urb(port->interrupt_in_urb);

	priv = usb_get_serial_port_data(port);
	kfree(priv);

	return 0;
}

static int  cyberjack_open(struct tty_struct *tty,
					struct usb_serial_port *port)
{
	struct cyberjack_private *priv;
	unsigned long flags;

	dev_dbg(&port->dev, "%s - usb_clear_halt\n", __func__);
	usb_clear_halt(port->serial->dev, port->write_urb->pipe);

	priv = usb_get_serial_port_data(port);
	spin_lock_irqsave(&priv->lock, flags);
	priv->rdtodo = 0;
	priv->wrfilled = 0;
	priv->wrsent = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static void cyberjack_close(struct usb_serial_port *port)
{
	usb_kill_urb(port->write_urb);
	usb_kill_urb(port->read_urb);
}

static int cyberjack_write(struct tty_struct *tty,
	struct usb_serial_port *port, const unsigned char *buf, int count)
{
	struct device *dev = &port->dev;
	struct cyberjack_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	int result;
	int wrexpected;

	if (count == 0) {
		dev_dbg(dev, "%s - write request of 0 bytes\n", __func__);
		return 0;
	}

	if (!test_and_clear_bit(0, &port->write_urbs_free)) {
		dev_dbg(dev, "%s - already writing\n", __func__);
		return 0;
	}

	spin_lock_irqsave(&priv->lock, flags);

	if (count+priv->wrfilled > sizeof(priv->wrbuf)) {
		/* To much data for buffer. Reset buffer. */
		priv->wrfilled = 0;
		spin_unlock_irqrestore(&priv->lock, flags);
		set_bit(0, &port->write_urbs_free);
		return 0;
	}

	/* Copy data */
	memcpy(priv->wrbuf + priv->wrfilled, buf, count);

	usb_serial_debug_data(dev, __func__, count, priv->wrbuf + priv->wrfilled);
	priv->wrfilled += count;

	if (priv->wrfilled >= 3) {
		wrexpected = ((int)priv->wrbuf[2]<<8)+priv->wrbuf[1]+3;
		dev_dbg(dev, "%s - expected data: %d\n", __func__, wrexpected);
	} else
		wrexpected = sizeof(priv->wrbuf);

	if (priv->wrfilled >= wrexpected) {
		/* We have enough data to begin transmission */
		int length;

		dev_dbg(dev, "%s - transmitting data (frame 1)\n", __func__);
		length = (wrexpected > port->bulk_out_size) ?
					port->bulk_out_size : wrexpected;

		memcpy(port->write_urb->transfer_buffer, priv->wrbuf, length);
		priv->wrsent = length;

		/* set up our urb */
		port->write_urb->transfer_buffer_length = length;

		/* send the data out the bulk port */
		result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
		if (result) {
			dev_err(&port->dev,
				"%s - failed submitting write urb, error %d\n",
				__func__, result);
			/* Throw away data. No better idea what to do with it. */
			priv->wrfilled = 0;
			priv->wrsent = 0;
			spin_unlock_irqrestore(&priv->lock, flags);
			set_bit(0, &port->write_urbs_free);
			return 0;
		}

		dev_dbg(dev, "%s - priv->wrsent=%d\n", __func__, priv->wrsent);
		dev_dbg(dev, "%s - priv->wrfilled=%d\n", __func__, priv->wrfilled);

		if (priv->wrsent >= priv->wrfilled) {
			dev_dbg(dev, "%s - buffer cleaned\n", __func__);
			memset(priv->wrbuf, 0, sizeof(priv->wrbuf));
			priv->wrfilled = 0;
			priv->wrsent = 0;
		}
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	return count;
}

static int cyberjack_write_room(struct tty_struct *tty)
{
	/* FIXME: .... */
	return CYBERJACK_LOCAL_BUF_SIZE;
}

static void cyberjack_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct cyberjack_private *priv = usb_get_serial_port_data(port);
	struct device *dev = &port->dev;
	unsigned char *data = urb->transfer_buffer;
	int status = urb->status;
	int result;

	/* the urb might have been killed. */
	if (status)
		return;

	usb_serial_debug_data(dev, __func__, urb->actual_length, data);

	/* React only to interrupts signaling a bulk_in transfer */
	if (urb->actual_length == 4 && data[0] == 0x01) {
		short old_rdtodo;

		/* This is a announcement of coming bulk_ins. */
		unsigned short size = ((unsigned short)data[3]<<8)+data[2]+3;

		spin_lock(&priv->lock);

		old_rdtodo = priv->rdtodo;

		if (old_rdtodo > SHRT_MAX - size) {
			dev_dbg(dev, "To many bulk_in urbs to do.\n");
			spin_unlock(&priv->lock);
			goto resubmit;
		}

		/* "+=" is probably more fault tolerant than "=" */
		priv->rdtodo += size;

		dev_dbg(dev, "%s - rdtodo: %d\n", __func__, priv->rdtodo);

		spin_unlock(&priv->lock);

		if (!old_rdtodo) {
			result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
			if (result)
				dev_err(dev, "%s - failed resubmitting read urb, error %d\n",
					__func__, result);
			dev_dbg(dev, "%s - usb_submit_urb(read urb)\n", __func__);
		}
	}

resubmit:
	result = usb_submit_urb(port->interrupt_in_urb, GFP_ATOMIC);
	if (result)
		dev_err(&port->dev, "usb_submit_urb(read int) failed\n");
	dev_dbg(dev, "%s - usb_submit_urb(int urb)\n", __func__);
}

static void cyberjack_read_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct cyberjack_private *priv = usb_get_serial_port_data(port);
	struct device *dev = &port->dev;
	unsigned char *data = urb->transfer_buffer;
	short todo;
	int result;
	int status = urb->status;

	usb_serial_debug_data(dev, __func__, urb->actual_length, data);
	if (status) {
		dev_dbg(dev, "%s - nonzero read bulk status received: %d\n",
			__func__, status);
		return;
	}

	if (urb->actual_length) {
		tty_insert_flip_string(&port->port, data, urb->actual_length);
		tty_flip_buffer_push(&port->port);
	}

	spin_lock(&priv->lock);

	/* Reduce urbs to do by one. */
	priv->rdtodo -= urb->actual_length;
	/* Just to be sure */
	if (priv->rdtodo < 0)
		priv->rdtodo = 0;
	todo = priv->rdtodo;

	spin_unlock(&priv->lock);

	dev_dbg(dev, "%s - rdtodo: %d\n", __func__, todo);

	/* Continue to read if we have still urbs to do. */
	if (todo /* || (urb->actual_length==port->bulk_in_endpointAddress)*/) {
		result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
		if (result)
			dev_err(dev, "%s - failed resubmitting read urb, error %d\n",
				__func__, result);
		dev_dbg(dev, "%s - usb_submit_urb(read urb)\n", __func__);
	}
}

static void cyberjack_write_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct cyberjack_private *priv = usb_get_serial_port_data(port);
	struct device *dev = &port->dev;
	int status = urb->status;

	set_bit(0, &port->write_urbs_free);
	if (status) {
		dev_dbg(dev, "%s - nonzero write bulk status received: %d\n",
			__func__, status);
		return;
	}

	spin_lock(&priv->lock);

	/* only do something if we have more data to send */
	if (priv->wrfilled) {
		int length, blksize, result;

		dev_dbg(dev, "%s - transmitting data (frame n)\n", __func__);

		length = ((priv->wrfilled - priv->wrsent) > port->bulk_out_size) ?
			port->bulk_out_size : (priv->wrfilled - priv->wrsent);

		memcpy(port->write_urb->transfer_buffer,
					priv->wrbuf + priv->wrsent, length);
		priv->wrsent += length;

		/* set up our urb */
		port->write_urb->transfer_buffer_length = length;

		/* send the data out the bulk port */
		result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
		if (result) {
			dev_err(dev, "%s - failed submitting write urb, error %d\n",
				__func__, result);
			/* Throw away data. No better idea what to do with it. */
			priv->wrfilled = 0;
			priv->wrsent = 0;
			goto exit;
		}

		dev_dbg(dev, "%s - priv->wrsent=%d\n", __func__, priv->wrsent);
		dev_dbg(dev, "%s - priv->wrfilled=%d\n", __func__, priv->wrfilled);

		blksize = ((int)priv->wrbuf[2]<<8)+priv->wrbuf[1]+3;

		if (priv->wrsent >= priv->wrfilled ||
					priv->wrsent >= blksize) {
			dev_dbg(dev, "%s - buffer cleaned\n", __func__);
			memset(priv->wrbuf, 0, sizeof(priv->wrbuf));
			priv->wrfilled = 0;
			priv->wrsent = 0;
		}
	}

exit:
	spin_unlock(&priv->lock);
	usb_serial_port_softint(port);
}

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
