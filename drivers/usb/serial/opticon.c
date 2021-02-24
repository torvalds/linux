// SPDX-License-Identifier: GPL-2.0
/*
 * Opticon USB barcode to serial driver
 *
 * Copyright (C) 2011 - 2012 Johan Hovold <jhovold@gmail.com>
 * Copyright (C) 2011 Martin Jansen <martin.jansen@opticon.com>
 * Copyright (C) 2008 - 2009 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (C) 2008 - 2009 Novell Inc.
 */

#include <linux/kernel.h>
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
	spinlock_t lock;	/* protects the following flags */
	bool rts;
	bool cts;
	int outstanding_urbs;
	int outstanding_bytes;

	struct usb_anchor anchor;
};


static void opticon_process_data_packet(struct usb_serial_port *port,
					const unsigned char *buf, size_t len)
{
	tty_insert_flip_string(&port->port, buf, len);
	tty_flip_buffer_push(&port->port);
}

static void opticon_process_status_packet(struct usb_serial_port *port,
					const unsigned char *buf, size_t len)
{
	struct opticon_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	if (buf[0] == 0x00)
		priv->cts = false;
	else
		priv->cts = true;
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void opticon_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	const unsigned char *hdr = urb->transfer_buffer;
	const unsigned char *data = hdr + 2;
	size_t data_len = urb->actual_length - 2;

	if (urb->actual_length <= 2) {
		dev_dbg(&port->dev, "malformed packet received: %d bytes\n",
							urb->actual_length);
		return;
	}
	/*
	 * Data from the device comes with a 2 byte header:
	 *
	 * <0x00><0x00>data...
	 *      This is real data to be sent to the tty layer
	 * <0x00><0x01>level
	 *      This is a CTS level change, the third byte is the CTS
	 *      value (0 for low, 1 for high).
	 */
	if ((hdr[0] == 0x00) && (hdr[1] == 0x00)) {
		opticon_process_data_packet(port, data, data_len);
	} else if ((hdr[0] == 0x00) && (hdr[1] == 0x01)) {
		opticon_process_status_packet(port, data, data_len);
	} else {
		dev_dbg(&port->dev, "unknown packet received: %02x %02x\n",
							hdr[0], hdr[1]);
	}
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
				0, 0, buffer, 1, USB_CTRL_SET_TIMEOUT);
	kfree(buffer);

	if (retval < 0)
		return retval;

	return 0;
}

static int opticon_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct opticon_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	int res;

	spin_lock_irqsave(&priv->lock, flags);
	priv->rts = false;
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Clear RTS line */
	send_control_msg(port, CONTROL_RTS, 0);

	/* clear the halt status of the endpoint */
	usb_clear_halt(port->serial->dev, port->read_urb->pipe);

	res = usb_serial_generic_open(tty, port);
	if (res)
		return res;

	/* Request CTS line state, sometimes during opening the current
	 * CTS state can be missed. */
	send_control_msg(port, RESEND_CTS_STATE, 1);

	return res;
}

static void opticon_close(struct usb_serial_port *port)
{
	struct opticon_private *priv = usb_get_serial_port_data(port);

	usb_kill_anchored_urbs(&priv->anchor);

	usb_serial_generic_close(port);
}

static void opticon_write_control_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct opticon_private *priv = usb_get_serial_port_data(port);
	int status = urb->status;
	unsigned long flags;

	/* free up the transfer buffer, as usb_free_urb() does not do this */
	kfree(urb->transfer_buffer);

	/* setup packet may be set if we're using it for writing */
	kfree(urb->setup_packet);

	if (status)
		dev_dbg(&port->dev,
			"%s - non-zero urb status received: %d\n",
			__func__, status);

	spin_lock_irqsave(&priv->lock, flags);
	--priv->outstanding_urbs;
	priv->outstanding_bytes -= urb->transfer_buffer_length;
	spin_unlock_irqrestore(&priv->lock, flags);

	usb_serial_port_softint(port);
}

static int opticon_write(struct tty_struct *tty, struct usb_serial_port *port,
			 const unsigned char *buf, int count)
{
	struct opticon_private *priv = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	struct urb *urb;
	unsigned char *buffer;
	unsigned long flags;
	struct usb_ctrlrequest *dr;
	int ret = -ENOMEM;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->outstanding_urbs > URB_UPPER_LIMIT) {
		spin_unlock_irqrestore(&priv->lock, flags);
		dev_dbg(&port->dev, "%s - write limit hit\n", __func__);
		return 0;
	}
	priv->outstanding_urbs++;
	priv->outstanding_bytes += count;
	spin_unlock_irqrestore(&priv->lock, flags);

	buffer = kmalloc(count, GFP_ATOMIC);
	if (!buffer)
		goto error_no_buffer;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		goto error_no_urb;

	memcpy(buffer, buf, count);

	usb_serial_debug_data(&port->dev, __func__, count, buffer);

	/* The connected devices do not have a bulk write endpoint,
	 * to transmit data to de barcode device the control endpoint is used */
	dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_ATOMIC);
	if (!dr)
		goto error_no_dr;

	dr->bRequestType = USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT;
	dr->bRequest = 0x01;
	dr->wValue = 0;
	dr->wIndex = 0;
	dr->wLength = cpu_to_le16(count);

	usb_fill_control_urb(urb, serial->dev,
		usb_sndctrlpipe(serial->dev, 0),
		(unsigned char *)dr, buffer, count,
		opticon_write_control_callback, port);

	usb_anchor_urb(urb, &priv->anchor);

	/* send it down the pipe */
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		dev_err(&port->dev, "failed to submit write urb: %d\n", ret);
		usb_unanchor_urb(urb);
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
	priv->outstanding_bytes -= count;
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

static int opticon_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct opticon_private *priv = usb_get_serial_port_data(port);
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

static int opticon_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct opticon_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	int count;

	spin_lock_irqsave(&priv->lock, flags);
	count = priv->outstanding_bytes;
	spin_unlock_irqrestore(&priv->lock, flags);

	return count;
}

static int opticon_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct opticon_private *priv = usb_get_serial_port_data(port);
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
	struct opticon_private *priv = usb_get_serial_port_data(port);
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

	ret = send_control_msg(port, CONTROL_RTS, !rts);
	if (ret)
		return usb_translate_errors(ret);

	return 0;
}

static int get_serial_info(struct tty_struct *tty,
			   struct serial_struct *ss)
{
	struct usb_serial_port *port = tty->driver_data;

	/* fake emulate a 16550 uart to make userspace code happy */
	ss->type		= PORT_16550A;
	ss->line		= port->minor;
	ss->port		= 0;
	ss->irq			= 0;
	ss->xmit_fifo_size	= 1024;
	ss->baud_base		= 9600;
	ss->close_delay		= 5*HZ;
	ss->closing_wait	= 30*HZ;
	return 0;
}

static int opticon_port_probe(struct usb_serial_port *port)
{
	struct opticon_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	init_usb_anchor(&priv->anchor);

	usb_set_serial_port_data(port, priv);

	return 0;
}

static void opticon_port_remove(struct usb_serial_port *port)
{
	struct opticon_private *priv = usb_get_serial_port_data(port);

	kfree(priv);
}

static struct usb_serial_driver opticon_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"opticon",
	},
	.id_table =		id_table,
	.num_ports =		1,
	.num_bulk_in =		1,
	.bulk_in_size =		256,
	.port_probe =		opticon_port_probe,
	.port_remove =		opticon_port_remove,
	.open =			opticon_open,
	.close =		opticon_close,
	.write =		opticon_write,
	.write_room = 		opticon_write_room,
	.chars_in_buffer =	opticon_chars_in_buffer,
	.throttle =		usb_serial_generic_throttle,
	.unthrottle =		usb_serial_generic_unthrottle,
	.get_serial =		get_serial_info,
	.tiocmget =		opticon_tiocmget,
	.tiocmset =		opticon_tiocmset,
	.process_read_urb =	opticon_process_read_urb,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&opticon_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
