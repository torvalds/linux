/*
 * Opticon USB barcode to serial driver
 *
 * Copyright (C) 2011 Martin Jansen <martin.jansen@opticon.com>
 * Copyright (C) 2008 - 2009 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (C) 2008 - 2009 Novell Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/slab.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/uaccess.h>

#define CONTROL_RTS			0x02
#define RESEND_CTS_STATE	0x03

/* max number of write urbs in flight */
#define URB_UPPER_LIMIT	8

/* This driver works for the Opticon 1D barcode reader
 * an examples of 1D barcode types are EAN, UPC, Code39, IATA etc.. */
#define DRIVER_DESC	"Opticon USB barcode to serial driver (1D)"

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x065a, 0x0009) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

/* This structure holds all of the individual device information */
struct opticon_private {
	struct usb_device *udev;
	struct usb_serial *serial;
	struct usb_serial_port *port;
	unsigned char *bulk_in_buffer;
	struct urb *bulk_read_urb;
	int buffer_size;
	u8 bulk_address;
	spinlock_t lock;	/* protects the following flags */
	bool throttled;
	bool actually_throttled;
	bool rts;
	bool cts;
	int outstanding_urbs;
};



static void opticon_read_bulk_callback(struct urb *urb)
{
	struct opticon_private *priv = urb->context;
	unsigned char *data = urb->transfer_buffer;
	struct usb_serial_port *port = priv->port;
	int status = urb->status;
	struct tty_struct *tty;
	int result;
	int data_length;
	unsigned long flags;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&priv->udev->dev, "%s - urb shutting down with status: %d\n",
			__func__, status);
		return;
	default:
		dev_dbg(&priv->udev->dev, "%s - nonzero urb status received: %d\n",
			__func__, status);
		goto exit;
	}

	usb_serial_debug_data(&port->dev, __func__, urb->actual_length, data);

	if (urb->actual_length > 2) {
		data_length = urb->actual_length - 2;

		/*
		 * Data from the device comes with a 2 byte header:
		 *
		 * <0x00><0x00>data...
		 *	This is real data to be sent to the tty layer
		 * <0x00><0x01)level
		 *	This is a CTS level change, the third byte is the CTS
		 *	value (0 for low, 1 for high).
		 */
		if ((data[0] == 0x00) && (data[1] == 0x00)) {
			/* real data, send it to the tty layer */
			tty = tty_port_tty_get(&port->port);
			if (tty) {
				tty_insert_flip_string(tty, data + 2,
						       data_length);
				tty_flip_buffer_push(tty);
				tty_kref_put(tty);
			}
		} else {
			if ((data[0] == 0x00) && (data[1] == 0x01)) {
				spin_lock_irqsave(&priv->lock, flags);
				/* CTS status information package */
				if (data[2] == 0x00)
					priv->cts = false;
				else
					priv->cts = true;
				spin_unlock_irqrestore(&priv->lock, flags);
			} else {
				dev_dbg(&priv->udev->dev,
					"Unknown data packet received from the device:"
					" %2x %2x\n",
					data[0], data[1]);
			}
		}
	} else {
		dev_dbg(&priv->udev->dev,
			"Improper amount of data received from the device, "
			"%d bytes", urb->actual_length);
	}

exit:
	spin_lock(&priv->lock);

	/* Continue trying to always read if we should */
	if (!priv->throttled) {
		result = usb_submit_urb(priv->bulk_read_urb, GFP_ATOMIC);
		if (result)
			dev_err(&port->dev,
			    "%s - failed resubmitting read urb, error %d\n",
							__func__, result);
	} else
		priv->actually_throttled = true;
	spin_unlock(&priv->lock);
}

static int send_control_msg(struct usb_serial_port *port, u8 requesttype,
				u8 val)
{
	struct usb_serial *serial = port->serial;
	int retval;
	u8 *buffer;

	buffer = kzalloc(1, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	buffer[0] = val;
	/* Send the message to the vendor control endpoint
	 * of the connected device */
	retval = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				requesttype,
				USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
				0, 0, buffer, 1, 0);
	kfree(buffer);

	return retval;
}

static int opticon_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct opticon_private *priv = usb_get_serial_data(port->serial);
	unsigned long flags;
	int result = 0;

	spin_lock_irqsave(&priv->lock, flags);
	priv->throttled = false;
	priv->actually_throttled = false;
	priv->port = port;
	priv->rts = false;
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Clear RTS line */
	send_control_msg(port, CONTROL_RTS, 0);

	/* clear the halt status of the enpoint */
	usb_clear_halt(priv->udev, priv->bulk_read_urb->pipe);

	result = usb_submit_urb(priv->bulk_read_urb, GFP_KERNEL);
	if (result)
		dev_err(&port->dev,
			"%s - failed resubmitting read urb, error %d\n",
			__func__, result);
	/* Request CTS line state, sometimes during opening the current
	 * CTS state can be missed. */
	send_control_msg(port, RESEND_CTS_STATE, 1);
	return result;
}

static void opticon_close(struct usb_serial_port *port)
{
	struct opticon_private *priv = usb_get_serial_data(port->serial);

	/* shutdown our urbs */
	usb_kill_urb(priv->bulk_read_urb);
}

static void opticon_write_control_callback(struct urb *urb)
{
	struct opticon_private *priv = urb->context;
	int status = urb->status;
	unsigned long flags;

	/* free up the transfer buffer, as usb_free_urb() does not do this */
	kfree(urb->transfer_buffer);

	/* setup packet may be set if we're using it for writing */
	kfree(urb->setup_packet);

	if (status)
		dev_dbg(&priv->udev->dev, "%s - nonzero write bulk status received: %d\n",
			__func__, status);

	spin_lock_irqsave(&priv->lock, flags);
	--priv->outstanding_urbs;
	spin_unlock_irqrestore(&priv->lock, flags);

	usb_serial_port_softint(priv->port);
}

static int opticon_write(struct tty_struct *tty, struct usb_serial_port *port,
			 const unsigned char *buf, int count)
{
	struct opticon_private *priv = usb_get_serial_data(port->serial);
	struct usb_serial *serial = port->serial;
	struct urb *urb;
	unsigned char *buffer;
	unsigned long flags;
	int status;
	struct usb_ctrlrequest *dr;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->outstanding_urbs > URB_UPPER_LIMIT) {
		spin_unlock_irqrestore(&priv->lock, flags);
		dev_dbg(&port->dev, "%s - write limit hit\n", __func__);
		return 0;
	}
	priv->outstanding_urbs++;
	spin_unlock_irqrestore(&priv->lock, flags);

	buffer = kmalloc(count, GFP_ATOMIC);
	if (!buffer) {
		dev_err(&port->dev, "out of memory\n");
		count = -ENOMEM;

		goto error_no_buffer;
	}

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		dev_err(&port->dev, "no more free urbs\n");
		count = -ENOMEM;
		goto error_no_urb;
	}

	memcpy(buffer, buf, count);

	usb_serial_debug_data(&port->dev, __func__, count, buffer);

	/* The conncected devices do not have a bulk write endpoint,
	 * to transmit data to de barcode device the control endpoint is used */
	dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_NOIO);
	if (!dr) {
		dev_err(&port->dev, "out of memory\n");
		count = -ENOMEM;
		goto error_no_dr;
	}

	dr->bRequestType = USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT;
	dr->bRequest = 0x01;
	dr->wValue = 0;
	dr->wIndex = 0;
	dr->wLength = cpu_to_le16(count);

	usb_fill_control_urb(urb, serial->dev,
		usb_sndctrlpipe(serial->dev, 0),
		(unsigned char *)dr, buffer, count,
		opticon_write_control_callback, priv);

	/* send it down the pipe */
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		dev_err(&port->dev,
		"%s - usb_submit_urb(write endpoint) failed status = %d\n",
							__func__, status);
		count = status;
		goto error;
	}

	/* we are done with this urb, so let the host driver
	 * really free it when it is finished with it */
	usb_free_urb(urb);

	return count;
error:
	kfree(dr);
error_no_dr:
	usb_free_urb(urb);
error_no_urb:
	kfree(buffer);
error_no_buffer:
	spin_lock_irqsave(&priv->lock, flags);
	--priv->outstanding_urbs;
	spin_unlock_irqrestore(&priv->lock, flags);
	return count;
}

static int opticon_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct opticon_private *priv = usb_get_serial_data(port->serial);
	unsigned long flags;

	/*
	 * We really can take almost anything the user throws at us
	 * but let's pick a nice big number to tell the tty
	 * layer that we have lots of free space, unless we don't.
	 */
	spin_lock_irqsave(&priv->lock, flags);
	if (priv->outstanding_urbs > URB_UPPER_LIMIT * 2 / 3) {
		spin_unlock_irqrestore(&priv->lock, flags);
		dev_dbg(&port->dev, "%s - write limit hit\n", __func__);
		return 0;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	return 2048;
}

static void opticon_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct opticon_private *priv = usb_get_serial_data(port->serial);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	priv->throttled = true;
	spin_unlock_irqrestore(&priv->lock, flags);
}


static void opticon_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct opticon_private *priv = usb_get_serial_data(port->serial);
	unsigned long flags;
	int result, was_throttled;

	spin_lock_irqsave(&priv->lock, flags);
	priv->throttled = false;
	was_throttled = priv->actually_throttled;
	priv->actually_throttled = false;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (was_throttled) {
		result = usb_submit_urb(priv->bulk_read_urb, GFP_ATOMIC);
		if (result)
			dev_err(&port->dev,
				"%s - failed submitting read urb, error %d\n",
							__func__, result);
	}
}

static int opticon_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct opticon_private *priv = usb_get_serial_data(port->serial);
	unsigned long flags;
	int result = 0;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->rts)
		result |= TIOCM_RTS;
	if (priv->cts)
		result |= TIOCM_CTS;
	spin_unlock_irqrestore(&priv->lock, flags);

	dev_dbg(&port->dev, "%s - %x\n", __func__, result);
	return result;
}

static int opticon_tiocmset(struct tty_struct *tty,
			   unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	struct opticon_private *priv = usb_get_serial_data(port->serial);
	unsigned long flags;
	bool rts;
	bool changed = false;
	int ret;

	/* We only support RTS so we only handle that */
	spin_lock_irqsave(&priv->lock, flags);

	rts = priv->rts;
	if (set & TIOCM_RTS)
		priv->rts = true;
	if (clear & TIOCM_RTS)
		priv->rts = false;
	changed = rts ^ priv->rts;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (!changed)
		return 0;

	/* Send the new RTS state to the connected device */
	mutex_lock(&serial->disc_mutex);
	if (!serial->disconnected)
		ret = send_control_msg(port, CONTROL_RTS, !rts);
	else
		ret = -ENODEV;
	mutex_unlock(&serial->disc_mutex);

	return ret;
}

static int get_serial_info(struct opticon_private *priv,
			   struct serial_struct __user *serial)
{
	struct serial_struct tmp;

	if (!serial)
		return -EFAULT;

	memset(&tmp, 0x00, sizeof(tmp));

	/* fake emulate a 16550 uart to make userspace code happy */
	tmp.type		= PORT_16550A;
	tmp.line		= priv->serial->minor;
	tmp.port		= 0;
	tmp.irq			= 0;
	tmp.flags		= ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
	tmp.xmit_fifo_size	= 1024;
	tmp.baud_base		= 9600;
	tmp.close_delay		= 5*HZ;
	tmp.closing_wait	= 30*HZ;

	if (copy_to_user(serial, &tmp, sizeof(*serial)))
		return -EFAULT;
	return 0;
}

static int opticon_ioctl(struct tty_struct *tty,
			 unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct opticon_private *priv = usb_get_serial_data(port->serial);

	dev_dbg(&port->dev, "%s - port %d, cmd = 0x%x\n", __func__, port->number, cmd);

	switch (cmd) {
	case TIOCGSERIAL:
		return get_serial_info(priv,
				       (struct serial_struct __user *)arg);
	}

	return -ENOIOCTLCMD;
}

static int opticon_startup(struct usb_serial *serial)
{
	struct opticon_private *priv;
	struct usb_host_interface *intf;
	int i;
	int retval = -ENOMEM;
	bool bulk_in_found = false;

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
	priv->outstanding_urbs = 0;	/* Init the outstanding urbs */

	/* find our bulk endpoint */
	intf = serial->interface->altsetting;
	for (i = 0; i < intf->desc.bNumEndpoints; ++i) {
		struct usb_endpoint_descriptor *endpoint;

		endpoint = &intf->endpoint[i].desc;
		if (!usb_endpoint_is_bulk_in(endpoint))
			continue;

		priv->bulk_read_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!priv->bulk_read_urb) {
			dev_err(&priv->udev->dev, "out of memory\n");
			goto error;
		}

		priv->buffer_size = usb_endpoint_maxp(endpoint) * 2;
		priv->bulk_in_buffer = kmalloc(priv->buffer_size, GFP_KERNEL);
		if (!priv->bulk_in_buffer) {
			dev_err(&priv->udev->dev, "out of memory\n");
			goto error;
		}

		priv->bulk_address = endpoint->bEndpointAddress;

		bulk_in_found = true;
		break;
		}

	if (!bulk_in_found) {
		dev_err(&priv->udev->dev,
			"Error - the proper endpoints were not found!\n");
		goto error;
	}

	usb_fill_bulk_urb(priv->bulk_read_urb, serial->dev,
				usb_rcvbulkpipe(serial->dev,
						priv->bulk_address),
				priv->bulk_in_buffer, priv->buffer_size,
				opticon_read_bulk_callback, priv);

	usb_set_serial_data(serial, priv);
	return 0;

error:
	usb_free_urb(priv->bulk_read_urb);
	kfree(priv->bulk_in_buffer);
	kfree(priv);
	return retval;
}

static void opticon_disconnect(struct usb_serial *serial)
{
	struct opticon_private *priv = usb_get_serial_data(serial);

	usb_kill_urb(priv->bulk_read_urb);
	usb_free_urb(priv->bulk_read_urb);
}

static void opticon_release(struct usb_serial *serial)
{
	struct opticon_private *priv = usb_get_serial_data(serial);

	kfree(priv->bulk_in_buffer);
	kfree(priv);
}

static int opticon_suspend(struct usb_serial *serial, pm_message_t message)
{
	struct opticon_private *priv = usb_get_serial_data(serial);

	usb_kill_urb(priv->bulk_read_urb);
	return 0;
}

static int opticon_resume(struct usb_serial *serial)
{
	struct opticon_private *priv = usb_get_serial_data(serial);
	struct usb_serial_port *port = serial->port[0];
	int result;

	mutex_lock(&port->port.mutex);
	/* This is protected by the port mutex against close/open */
	if (test_bit(ASYNCB_INITIALIZED, &port->port.flags))
		result = usb_submit_urb(priv->bulk_read_urb, GFP_NOIO);
	else
		result = 0;
	mutex_unlock(&port->port.mutex);
	return result;
}

static struct usb_serial_driver opticon_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"opticon",
	},
	.id_table =		id_table,
	.num_ports =		1,
	.attach =		opticon_startup,
	.open =			opticon_open,
	.close =		opticon_close,
	.write =		opticon_write,
	.write_room = 		opticon_write_room,
	.disconnect =		opticon_disconnect,
	.release =		opticon_release,
	.throttle = 		opticon_throttle,
	.unthrottle =		opticon_unthrottle,
	.ioctl =		opticon_ioctl,
	.tiocmget =		opticon_tiocmget,
	.tiocmset =		opticon_tiocmset,
	.suspend = 		opticon_suspend,
	.resume =		opticon_resume,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&opticon_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
