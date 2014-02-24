/*
 * USB Keyspan PDA / Xircom / Entregra Converter driver
 *
 * Copyright (C) 1999 - 2001 Greg Kroah-Hartman	<greg@kroah.com>
 * Copyright (C) 1999, 2000 Brian Warner	<warner@lothar.com>
 * Copyright (C) 2000 Al Borchers		<borchers@steinerpoint.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this
 * driver
 */


#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/usb/ezusb.h>

/* make a simple define to handle if we are compiling keyspan_pda or xircom support */
#if defined(CONFIG_USB_SERIAL_KEYSPAN_PDA) || defined(CONFIG_USB_SERIAL_KEYSPAN_PDA_MODULE)
	#define KEYSPAN
#else
	#undef KEYSPAN
#endif
#if defined(CONFIG_USB_SERIAL_XIRCOM) || defined(CONFIG_USB_SERIAL_XIRCOM_MODULE)
	#define XIRCOM
#else
	#undef XIRCOM
#endif

#define DRIVER_AUTHOR "Brian Warner <warner@lothar.com>"
#define DRIVER_DESC "USB Keyspan PDA Converter driver"

struct keyspan_pda_private {
	int			tx_room;
	int			tx_throttled;
	struct work_struct			wakeup_work;
	struct work_struct			unthrottle_work;
	struct usb_serial	*serial;
	struct usb_serial_port	*port;
};


#define KEYSPAN_VENDOR_ID		0x06cd
#define KEYSPAN_PDA_FAKE_ID		0x0103
#define KEYSPAN_PDA_ID			0x0104 /* no clue */

/* For Xircom PGSDB9 and older Entregra version of the same device */
#define XIRCOM_VENDOR_ID		0x085a
#define XIRCOM_FAKE_ID			0x8027
#define ENTREGRA_VENDOR_ID		0x1645
#define ENTREGRA_FAKE_ID		0x8093

static const struct usb_device_id id_table_combined[] = {
#ifdef KEYSPAN
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, KEYSPAN_PDA_FAKE_ID) },
#endif
#ifdef XIRCOM
	{ USB_DEVICE(XIRCOM_VENDOR_ID, XIRCOM_FAKE_ID) },
	{ USB_DEVICE(ENTREGRA_VENDOR_ID, ENTREGRA_FAKE_ID) },
#endif
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, KEYSPAN_PDA_ID) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table_combined);

static const struct usb_device_id id_table_std[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, KEYSPAN_PDA_ID) },
	{ }						/* Terminating entry */
};

#ifdef KEYSPAN
static const struct usb_device_id id_table_fake[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, KEYSPAN_PDA_FAKE_ID) },
	{ }						/* Terminating entry */
};
#endif

#ifdef XIRCOM
static const struct usb_device_id id_table_fake_xircom[] = {
	{ USB_DEVICE(XIRCOM_VENDOR_ID, XIRCOM_FAKE_ID) },
	{ USB_DEVICE(ENTREGRA_VENDOR_ID, ENTREGRA_FAKE_ID) },
	{ }
};
#endif

static void keyspan_pda_wakeup_write(struct work_struct *work)
{
	struct keyspan_pda_private *priv =
		container_of(work, struct keyspan_pda_private, wakeup_work);
	struct usb_serial_port *port = priv->port;

	tty_port_tty_wakeup(&port->port);
}

static void keyspan_pda_request_unthrottle(struct work_struct *work)
{
	struct keyspan_pda_private *priv =
		container_of(work, struct keyspan_pda_private, unthrottle_work);
	struct usb_serial *serial = priv->serial;
	int result;

	/* ask the device to tell us when the tx buffer becomes
	   sufficiently empty */
	result = usb_control_msg(serial->dev,
				 usb_sndctrlpipe(serial->dev, 0),
				 7, /* request_unthrottle */
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE
				 | USB_DIR_OUT,
				 16, /* value: threshold */
				 0, /* index */
				 NULL,
				 0,
				 2000);
	if (result < 0)
		dev_dbg(&serial->dev->dev, "%s - error %d from usb_control_msg\n",
			__func__, result);
}


static void keyspan_pda_rx_interrupt(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	int retval;
	int status = urb->status;
	struct keyspan_pda_private *priv;
	priv = usb_get_serial_port_data(port);

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&urb->dev->dev, "%s - urb shutting down with status: %d\n", __func__, status);
		return;
	default:
		dev_dbg(&urb->dev->dev, "%s - nonzero urb status received: %d\n", __func__, status);
		goto exit;
	}

	/* see if the message is data or a status interrupt */
	switch (data[0]) {
	case 0:
		 /* rest of message is rx data */
		if (urb->actual_length) {
			tty_insert_flip_string(&port->port, data + 1,
						urb->actual_length - 1);
			tty_flip_buffer_push(&port->port);
		}
		break;
	case 1:
		/* status interrupt */
		dev_dbg(&port->dev, "rx int, d1=%d, d2=%d\n", data[1], data[2]);
		switch (data[1]) {
		case 1: /* modemline change */
			break;
		case 2: /* tx unthrottle interrupt */
			priv->tx_throttled = 0;
			/* queue up a wakeup at scheduler time */
			schedule_work(&priv->wakeup_work);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&port->dev,
			"%s - usb_submit_urb failed with result %d",
			__func__, retval);
}


static void keyspan_pda_rx_throttle(struct tty_struct *tty)
{
	/* stop receiving characters. We just turn off the URB request, and
	   let chars pile up in the device. If we're doing hardware
	   flowcontrol, the device will signal the other end when its buffer
	   fills up. If we're doing XON/XOFF, this would be a good time to
	   send an XOFF, although it might make sense to foist that off
	   upon the device too. */
	struct usb_serial_port *port = tty->driver_data;

	usb_kill_urb(port->interrupt_in_urb);
}


static void keyspan_pda_rx_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	/* just restart the receive interrupt URB */

	if (usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL))
		dev_dbg(&port->dev, "usb_submit_urb(read urb) failed\n");
}


static speed_t keyspan_pda_setbaud(struct usb_serial *serial, speed_t baud)
{
	int rc;
	int bindex;

	switch (baud) {
	case 110:
		bindex = 0;
		break;
	case 300:
		bindex = 1;
		break;
	case 1200:
		bindex = 2;
		break;
	case 2400:
		bindex = 3;
		break;
	case 4800:
		bindex = 4;
		break;
	case 9600:
		bindex = 5;
		break;
	case 19200:
		bindex = 6;
		break;
	case 38400:
		bindex = 7;
		break;
	case 57600:
		bindex = 8;
		break;
	case 115200:
		bindex = 9;
		break;
	default:
		bindex = 5;	/* Default to 9600 */
		baud = 9600;
	}

	/* rather than figure out how to sleep while waiting for this
	   to complete, I just use the "legacy" API. */
	rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			     0, /* set baud */
			     USB_TYPE_VENDOR
			     | USB_RECIP_INTERFACE
			     | USB_DIR_OUT, /* type */
			     bindex, /* value */
			     0, /* index */
			     NULL, /* &data */
			     0, /* size */
			     2000); /* timeout */
	if (rc < 0)
		return 0;
	return baud;
}


static void keyspan_pda_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	int value;
	int result;

	if (break_state == -1)
		value = 1; /* start break */
	else
		value = 0; /* clear break */
	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			4, /* set break */
			USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
			value, 0, NULL, 0, 2000);
	if (result < 0)
		dev_dbg(&port->dev, "%s - error %d from usb_control_msg\n",
			__func__, result);
	/* there is something funky about this.. the TCSBRK that 'cu' performs
	   ought to translate into a break_ctl(-1),break_ctl(0) pair HZ/4
	   seconds apart, but it feels like the break sent isn't as long as it
	   is on /dev/ttyS0 */
}


static void keyspan_pda_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct usb_serial *serial = port->serial;
	speed_t speed;

	/* cflag specifies lots of stuff: number of stop bits, parity, number
	   of data bits, baud. What can the device actually handle?:
	   CSTOPB (1 stop bit or 2)
	   PARENB (parity)
	   CSIZE (5bit .. 8bit)
	   There is minimal hw support for parity (a PSW bit seems to hold the
	   parity of whatever is in the accumulator). The UART either deals
	   with 10 bits (start, 8 data, stop) or 11 bits (start, 8 data,
	   1 special, stop). So, with firmware changes, we could do:
	   8N1: 10 bit
	   8N2: 11 bit, extra bit always (mark?)
	   8[EOMS]1: 11 bit, extra bit is parity
	   7[EOMS]1: 10 bit, b0/b7 is parity
	   7[EOMS]2: 11 bit, b0/b7 is parity, extra bit always (mark?)

	   HW flow control is dictated by the tty->termios.c_cflags & CRTSCTS
	   bit.

	   For now, just do baud. */

	speed = tty_get_baud_rate(tty);
	speed = keyspan_pda_setbaud(serial, speed);

	if (speed == 0) {
		dev_dbg(&port->dev, "can't handle requested baud rate\n");
		/* It hasn't changed so.. */
		speed = tty_termios_baud_rate(old_termios);
	}
	/* Only speed can change so copy the old h/w parameters
	   then encode the new speed */
	tty_termios_copy_hw(&tty->termios, old_termios);
	tty_encode_baud_rate(tty, speed, speed);
}


/* modem control pins: DTR and RTS are outputs and can be controlled.
   DCD, RI, DSR, CTS are inputs and can be read. All outputs can also be
   read. The byte passed is: DTR(b7) DCD RI DSR CTS RTS(b2) unused unused */

static int keyspan_pda_get_modem_info(struct usb_serial *serial,
				      unsigned char *value)
{
	int rc;
	u8 *data;

	data = kmalloc(1, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	rc = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			     3, /* get pins */
			     USB_TYPE_VENDOR|USB_RECIP_INTERFACE|USB_DIR_IN,
			     0, 0, data, 1, 2000);
	if (rc >= 0)
		*value = *data;

	kfree(data);
	return rc;
}


static int keyspan_pda_set_modem_info(struct usb_serial *serial,
				      unsigned char value)
{
	int rc;
	rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			     3, /* set pins */
			     USB_TYPE_VENDOR|USB_RECIP_INTERFACE|USB_DIR_OUT,
			     value, 0, NULL, 0, 2000);
	return rc;
}

static int keyspan_pda_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	int rc;
	unsigned char status;
	int value;

	rc = keyspan_pda_get_modem_info(serial, &status);
	if (rc < 0)
		return rc;
	value =
		((status & (1<<7)) ? TIOCM_DTR : 0) |
		((status & (1<<6)) ? TIOCM_CAR : 0) |
		((status & (1<<5)) ? TIOCM_RNG : 0) |
		((status & (1<<4)) ? TIOCM_DSR : 0) |
		((status & (1<<3)) ? TIOCM_CTS : 0) |
		((status & (1<<2)) ? TIOCM_RTS : 0);
	return value;
}

static int keyspan_pda_tiocmset(struct tty_struct *tty,
				unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	int rc;
	unsigned char status;

	rc = keyspan_pda_get_modem_info(serial, &status);
	if (rc < 0)
		return rc;

	if (set & TIOCM_RTS)
		status |= (1<<2);
	if (set & TIOCM_DTR)
		status |= (1<<7);

	if (clear & TIOCM_RTS)
		status &= ~(1<<2);
	if (clear & TIOCM_DTR)
		status &= ~(1<<7);
	rc = keyspan_pda_set_modem_info(serial, status);
	return rc;
}

static int keyspan_pda_write(struct tty_struct *tty,
	struct usb_serial_port *port, const unsigned char *buf, int count)
{
	struct usb_serial *serial = port->serial;
	int request_unthrottle = 0;
	int rc = 0;
	struct keyspan_pda_private *priv;

	priv = usb_get_serial_port_data(port);
	/* guess how much room is left in the device's ring buffer, and if we
	   want to send more than that, check first, updating our notion of
	   what is left. If our write will result in no room left, ask the
	   device to give us an interrupt when the room available rises above
	   a threshold, and hold off all writers (eventually, those using
	   select() or poll() too) until we receive that unthrottle interrupt.
	   Block if we can't write anything at all, otherwise write as much as
	   we can. */
	if (count == 0) {
		dev_dbg(&port->dev, "write request of 0 bytes\n");
		return 0;
	}

	/* we might block because of:
	   the TX urb is in-flight (wait until it completes)
	   the device is full (wait until it says there is room)
	*/
	spin_lock_bh(&port->lock);
	if (!test_bit(0, &port->write_urbs_free) || priv->tx_throttled) {
		spin_unlock_bh(&port->lock);
		return 0;
	}
	clear_bit(0, &port->write_urbs_free);
	spin_unlock_bh(&port->lock);

	/* At this point the URB is in our control, nobody else can submit it
	   again (the only sudden transition was the one from EINPROGRESS to
	   finished).  Also, the tx process is not throttled. So we are
	   ready to write. */

	count = (count > port->bulk_out_size) ? port->bulk_out_size : count;

	/* Check if we might overrun the Tx buffer.   If so, ask the
	   device how much room it really has.  This is done only on
	   scheduler time, since usb_control_msg() sleeps. */
	if (count > priv->tx_room && !in_interrupt()) {
		u8 *room;

		room = kmalloc(1, GFP_KERNEL);
		if (!room) {
			rc = -ENOMEM;
			goto exit;
		}

		rc = usb_control_msg(serial->dev,
				     usb_rcvctrlpipe(serial->dev, 0),
				     6, /* write_room */
				     USB_TYPE_VENDOR | USB_RECIP_INTERFACE
				     | USB_DIR_IN,
				     0, /* value: 0 means "remaining room" */
				     0, /* index */
				     room,
				     1,
				     2000);
		if (rc > 0) {
			dev_dbg(&port->dev, "roomquery says %d\n", *room);
			priv->tx_room = *room;
		}
		kfree(room);
		if (rc < 0) {
			dev_dbg(&port->dev, "roomquery failed\n");
			goto exit;
		}
		if (rc == 0) {
			dev_dbg(&port->dev, "roomquery returned 0 bytes\n");
			rc = -EIO; /* device didn't return any data */
			goto exit;
		}
	}
	if (count > priv->tx_room) {
		/* we're about to completely fill the Tx buffer, so
		   we'll be throttled afterwards. */
		count = priv->tx_room;
		request_unthrottle = 1;
	}

	if (count) {
		/* now transfer data */
		memcpy(port->write_urb->transfer_buffer, buf, count);
		/* send the data out the bulk port */
		port->write_urb->transfer_buffer_length = count;

		priv->tx_room -= count;

		rc = usb_submit_urb(port->write_urb, GFP_ATOMIC);
		if (rc) {
			dev_dbg(&port->dev, "usb_submit_urb(write bulk) failed\n");
			goto exit;
		}
	} else {
		/* There wasn't any room left, so we are throttled until
		   the buffer empties a bit */
		request_unthrottle = 1;
	}

	if (request_unthrottle) {
		priv->tx_throttled = 1; /* block writers */
		schedule_work(&priv->unthrottle_work);
	}

	rc = count;
exit:
	if (rc < 0)
		set_bit(0, &port->write_urbs_free);
	return rc;
}


static void keyspan_pda_write_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct keyspan_pda_private *priv;

	set_bit(0, &port->write_urbs_free);
	priv = usb_get_serial_port_data(port);

	/* queue up a wakeup at scheduler time */
	schedule_work(&priv->wakeup_work);
}


static int keyspan_pda_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct keyspan_pda_private *priv;
	priv = usb_get_serial_port_data(port);
	/* used by n_tty.c for processing of tabs and such. Giving it our
	   conservative guess is probably good enough, but needs testing by
	   running a console through the device. */
	return priv->tx_room;
}


static int keyspan_pda_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct keyspan_pda_private *priv;
	unsigned long flags;
	int ret = 0;

	priv = usb_get_serial_port_data(port);

	/* when throttled, return at least WAKEUP_CHARS to tell select() (via
	   n_tty.c:normal_poll() ) that we're not writeable. */

	spin_lock_irqsave(&port->lock, flags);
	if (!test_bit(0, &port->write_urbs_free) || priv->tx_throttled)
		ret = 256;
	spin_unlock_irqrestore(&port->lock, flags);
	return ret;
}


static void keyspan_pda_dtr_rts(struct usb_serial_port *port, int on)
{
	struct usb_serial *serial = port->serial;

	if (on)
		keyspan_pda_set_modem_info(serial, (1 << 7) | (1 << 2));
	else
		keyspan_pda_set_modem_info(serial, 0);
}


static int keyspan_pda_open(struct tty_struct *tty,
					struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	u8 *room;
	int rc = 0;
	struct keyspan_pda_private *priv;

	/* find out how much room is in the Tx ring */
	room = kmalloc(1, GFP_KERNEL);
	if (!room)
		return -ENOMEM;

	rc = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			     6, /* write_room */
			     USB_TYPE_VENDOR | USB_RECIP_INTERFACE
			     | USB_DIR_IN,
			     0, /* value */
			     0, /* index */
			     room,
			     1,
			     2000);
	if (rc < 0) {
		dev_dbg(&port->dev, "%s - roomquery failed\n", __func__);
		goto error;
	}
	if (rc == 0) {
		dev_dbg(&port->dev, "%s - roomquery returned 0 bytes\n", __func__);
		rc = -EIO;
		goto error;
	}
	priv = usb_get_serial_port_data(port);
	priv->tx_room = *room;
	priv->tx_throttled = *room ? 0 : 1;

	/*Start reading from the device*/
	rc = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (rc) {
		dev_dbg(&port->dev, "%s - usb_submit_urb(read int) failed\n", __func__);
		goto error;
	}
error:
	kfree(room);
	return rc;
}
static void keyspan_pda_close(struct usb_serial_port *port)
{
	usb_kill_urb(port->write_urb);
	usb_kill_urb(port->interrupt_in_urb);
}


/* download the firmware to a "fake" device (pre-renumeration) */
static int keyspan_pda_fake_startup(struct usb_serial *serial)
{
	int response;
	const char *fw_name;

	/* download the firmware here ... */
	response = ezusb_fx1_set_reset(serial->dev, 1);

	if (0) { ; }
#ifdef KEYSPAN
	else if (le16_to_cpu(serial->dev->descriptor.idVendor) == KEYSPAN_VENDOR_ID)
		fw_name = "keyspan_pda/keyspan_pda.fw";
#endif
#ifdef XIRCOM
	else if ((le16_to_cpu(serial->dev->descriptor.idVendor) == XIRCOM_VENDOR_ID) ||
		 (le16_to_cpu(serial->dev->descriptor.idVendor) == ENTREGRA_VENDOR_ID))
		fw_name = "keyspan_pda/xircom_pgs.fw";
#endif
	else {
		dev_err(&serial->dev->dev, "%s: unknown vendor, aborting.\n",
			__func__);
		return -ENODEV;
	}

	if (ezusb_fx1_ihex_firmware_download(serial->dev, fw_name) < 0) {
		dev_err(&serial->dev->dev, "failed to load firmware \"%s\"\n",
			fw_name);
		return -ENOENT;
	}

	/* after downloading firmware Renumeration will occur in a
	  moment and the new device will bind to the real driver */

	/* we want this device to fail to have a driver assigned to it. */
	return 1;
}

#ifdef KEYSPAN
MODULE_FIRMWARE("keyspan_pda/keyspan_pda.fw");
#endif
#ifdef XIRCOM
MODULE_FIRMWARE("keyspan_pda/xircom_pgs.fw");
#endif

static int keyspan_pda_port_probe(struct usb_serial_port *port)
{

	struct keyspan_pda_private *priv;

	priv = kmalloc(sizeof(struct keyspan_pda_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	INIT_WORK(&priv->wakeup_work, keyspan_pda_wakeup_write);
	INIT_WORK(&priv->unthrottle_work, keyspan_pda_request_unthrottle);
	priv->serial = port->serial;
	priv->port = port;

	usb_set_serial_port_data(port, priv);

	return 0;
}

static int keyspan_pda_port_remove(struct usb_serial_port *port)
{
	struct keyspan_pda_private *priv;

	priv = usb_get_serial_port_data(port);
	kfree(priv);

	return 0;
}

#ifdef KEYSPAN
static struct usb_serial_driver keyspan_pda_fake_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"keyspan_pda_pre",
	},
	.description =		"Keyspan PDA - (prerenumeration)",
	.id_table =		id_table_fake,
	.num_ports =		1,
	.attach =		keyspan_pda_fake_startup,
};
#endif

#ifdef XIRCOM
static struct usb_serial_driver xircom_pgs_fake_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"xircom_no_firm",
	},
	.description =		"Xircom / Entregra PGS - (prerenumeration)",
	.id_table =		id_table_fake_xircom,
	.num_ports =		1,
	.attach =		keyspan_pda_fake_startup,
};
#endif

static struct usb_serial_driver keyspan_pda_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"keyspan_pda",
	},
	.description =		"Keyspan PDA",
	.id_table =		id_table_std,
	.num_ports =		1,
	.dtr_rts =		keyspan_pda_dtr_rts,
	.open =			keyspan_pda_open,
	.close =		keyspan_pda_close,
	.write =		keyspan_pda_write,
	.write_room =		keyspan_pda_write_room,
	.write_bulk_callback = 	keyspan_pda_write_bulk_callback,
	.read_int_callback =	keyspan_pda_rx_interrupt,
	.chars_in_buffer =	keyspan_pda_chars_in_buffer,
	.throttle =		keyspan_pda_rx_throttle,
	.unthrottle =		keyspan_pda_rx_unthrottle,
	.set_termios =		keyspan_pda_set_termios,
	.break_ctl =		keyspan_pda_break_ctl,
	.tiocmget =		keyspan_pda_tiocmget,
	.tiocmset =		keyspan_pda_tiocmset,
	.port_probe =		keyspan_pda_port_probe,
	.port_remove =		keyspan_pda_port_remove,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&keyspan_pda_device,
#ifdef KEYSPAN
	&keyspan_pda_fake_device,
#endif
#ifdef XIRCOM
	&xircom_pgs_fake_device,
#endif
	NULL
};

module_usb_serial_driver(serial_drivers, id_table_combined);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
