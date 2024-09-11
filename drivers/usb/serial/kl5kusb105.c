// SPDX-License-Identifier: GPL-2.0+
/*
 * KLSI KL5KUSB105 chip RS232 converter driver
 *
 *   Copyright (C) 2010 Johan Hovold <jhovold@gmail.com>
 *   Copyright (C) 2001 Utz-Uwe Haus <haus@uuhaus.de>
 *
 * All information about the device was acquired using SniffUSB ans snoopUSB
 * on Windows98.
 * It was written out of frustration with the PalmConnect USB Serial adapter
 * sold by Palm Inc.
 * Neither Palm, nor their contractor (MCCI) or their supplier (KLSI) provided
 * information that was not already available.
 *
 * It seems that KLSI bought some silicon-design information from ScanLogic,
 * whose SL11R processor is at the core of the KL5KUSB chipset from KLSI.
 * KLSI has firmware available for their devices; it is probable that the
 * firmware differs from that used by KLSI in their products. If you have an
 * original KLSI device and can provide some information on it, I would be
 * most interested in adding support for it here. If you have any information
 * on the protocol used (or find errors in my reverse-engineered stuff), please
 * let me know.
 *
 * The code was only tested with a PalmConnect USB adapter; if you
 * are adventurous, try it with any KLSI-based device and let me know how it
 * breaks so that I can fix it!
 */

/* TODO:
 *	check modem line signals
 *	implement handshaking or decide that we do not support it
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include "kl5kusb105.h"

#define DRIVER_AUTHOR "Utz-Uwe Haus <haus@uuhaus.de>, Johan Hovold <jhovold@gmail.com>"
#define DRIVER_DESC "KLSI KL5KUSB105 chipset USB->Serial Converter driver"


/*
 * Function prototypes
 */
static int klsi_105_port_probe(struct usb_serial_port *port);
static void klsi_105_port_remove(struct usb_serial_port *port);
static int  klsi_105_open(struct tty_struct *tty, struct usb_serial_port *port);
static void klsi_105_close(struct usb_serial_port *port);
static void klsi_105_set_termios(struct tty_struct *tty,
				 struct usb_serial_port *port,
				 const struct ktermios *old_termios);
static int  klsi_105_tiocmget(struct tty_struct *tty);
static void klsi_105_process_read_urb(struct urb *urb);
static int klsi_105_prepare_write_buffer(struct usb_serial_port *port,
						void *dest, size_t size);

/*
 * All of the device info needed for the KLSI converters.
 */
static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(PALMCONNECT_VID, PALMCONNECT_PID) },
	{ }		/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_serial_driver kl5kusb105d_device = {
	.driver = {
		.name =		"kl5kusb105d",
	},
	.description =		"KL5KUSB105D / PalmConnect",
	.id_table =		id_table,
	.num_ports =		1,
	.bulk_out_size =	64,
	.open =			klsi_105_open,
	.close =		klsi_105_close,
	.set_termios =		klsi_105_set_termios,
	.tiocmget =		klsi_105_tiocmget,
	.port_probe =		klsi_105_port_probe,
	.port_remove =		klsi_105_port_remove,
	.throttle =		usb_serial_generic_throttle,
	.unthrottle =		usb_serial_generic_unthrottle,
	.process_read_urb =	klsi_105_process_read_urb,
	.prepare_write_buffer =	klsi_105_prepare_write_buffer,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&kl5kusb105d_device, NULL
};

struct klsi_105_port_settings {
	u8	pktlen;		/* always 5, it seems */
	u8	baudrate;
	u8	databits;
	u8	unknown1;
	u8	unknown2;
};

struct klsi_105_private {
	struct klsi_105_port_settings	cfg;
	unsigned long			line_state; /* modem line settings */
	spinlock_t			lock;
};


/*
 * Handle vendor specific USB requests
 */


#define KLSI_TIMEOUT	 5000 /* default urb timeout */

static int klsi_105_chg_port_settings(struct usb_serial_port *port,
				      struct klsi_105_port_settings *settings)
{
	int rc;

	rc = usb_control_msg_send(port->serial->dev,
				  0,
				  KL5KUSB105A_SIO_SET_DATA,
				  USB_TYPE_VENDOR | USB_DIR_OUT |
				  USB_RECIP_INTERFACE,
				  0, /* value */
				  0, /* index */
				  settings,
				  sizeof(struct klsi_105_port_settings),
				  KLSI_TIMEOUT,
				  GFP_KERNEL);
	if (rc)
		dev_err(&port->dev,
			"Change port settings failed (error = %d)\n", rc);

	dev_dbg(&port->dev,
		"pktlen %u, baudrate 0x%02x, databits %u, u1 %u, u2 %u\n",
		settings->pktlen, settings->baudrate, settings->databits,
		settings->unknown1, settings->unknown2);

	return rc;
}

/*
 * Read line control via vendor command and return result through
 * the state pointer.
 */
static int klsi_105_get_line_state(struct usb_serial_port *port,
				   unsigned long *state)
{
	u16 status;
	int rc;

	rc = usb_control_msg_recv(port->serial->dev, 0,
				  KL5KUSB105A_SIO_POLL,
				  USB_TYPE_VENDOR | USB_DIR_IN,
				  0, /* value */
				  0, /* index */
				  &status, sizeof(status),
				  10000,
				  GFP_KERNEL);
	if (rc) {
		dev_err(&port->dev, "reading line status failed: %d\n", rc);
		return rc;
	}

	le16_to_cpus(&status);

	dev_dbg(&port->dev, "read status %04x\n", status);

	*state = ((status & KL5KUSB105A_DSR) ? TIOCM_DSR : 0) |
		 ((status & KL5KUSB105A_CTS) ? TIOCM_CTS : 0);

	return 0;
}


/*
 * Driver's tty interface functions
 */

static int klsi_105_port_probe(struct usb_serial_port *port)
{
	struct klsi_105_private *priv;

	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* set initial values for control structures */
	priv->cfg.pktlen    = 5;
	priv->cfg.baudrate  = kl5kusb105a_sio_b9600;
	priv->cfg.databits  = kl5kusb105a_dtb_8;
	priv->cfg.unknown1  = 0;
	priv->cfg.unknown2  = 1;

	priv->line_state    = 0;

	spin_lock_init(&priv->lock);

	usb_set_serial_port_data(port, priv);

	return 0;
}

static void klsi_105_port_remove(struct usb_serial_port *port)
{
	struct klsi_105_private *priv;

	priv = usb_get_serial_port_data(port);
	kfree(priv);
}

static int  klsi_105_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct klsi_105_private *priv = usb_get_serial_port_data(port);
	int retval = 0;
	int rc;
	unsigned long line_state;
	struct klsi_105_port_settings cfg;
	unsigned long flags;

	/* Do a defined restart:
	 * Set up sane default baud rate and send the 'READ_ON'
	 * vendor command.
	 * FIXME: set modem line control (how?)
	 * Then read the modem line control and store values in
	 * priv->line_state.
	 */

	cfg.pktlen   = 5;
	cfg.baudrate = kl5kusb105a_sio_b9600;
	cfg.databits = kl5kusb105a_dtb_8;
	cfg.unknown1 = 0;
	cfg.unknown2 = 1;
	klsi_105_chg_port_settings(port, &cfg);

	spin_lock_irqsave(&priv->lock, flags);
	priv->cfg.pktlen   = cfg.pktlen;
	priv->cfg.baudrate = cfg.baudrate;
	priv->cfg.databits = cfg.databits;
	priv->cfg.unknown1 = cfg.unknown1;
	priv->cfg.unknown2 = cfg.unknown2;
	spin_unlock_irqrestore(&priv->lock, flags);

	/* READ_ON and urb submission */
	rc = usb_serial_generic_open(tty, port);
	if (rc)
		return rc;

	rc = usb_control_msg(port->serial->dev,
			     usb_sndctrlpipe(port->serial->dev, 0),
			     KL5KUSB105A_SIO_CONFIGURE,
			     USB_TYPE_VENDOR|USB_DIR_OUT|USB_RECIP_INTERFACE,
			     KL5KUSB105A_SIO_CONFIGURE_READ_ON,
			     0, /* index */
			     NULL,
			     0,
			     KLSI_TIMEOUT);
	if (rc < 0) {
		dev_err(&port->dev, "Enabling read failed (error = %d)\n", rc);
		retval = rc;
		goto err_generic_close;
	} else
		dev_dbg(&port->dev, "%s - enabled reading\n", __func__);

	rc = klsi_105_get_line_state(port, &line_state);
	if (rc < 0) {
		retval = rc;
		goto err_disable_read;
	}

	spin_lock_irqsave(&priv->lock, flags);
	priv->line_state = line_state;
	spin_unlock_irqrestore(&priv->lock, flags);
	dev_dbg(&port->dev, "%s - read line state 0x%lx\n", __func__,
			line_state);

	return 0;

err_disable_read:
	usb_control_msg(port->serial->dev,
			     usb_sndctrlpipe(port->serial->dev, 0),
			     KL5KUSB105A_SIO_CONFIGURE,
			     USB_TYPE_VENDOR | USB_DIR_OUT,
			     KL5KUSB105A_SIO_CONFIGURE_READ_OFF,
			     0, /* index */
			     NULL, 0,
			     KLSI_TIMEOUT);
err_generic_close:
	usb_serial_generic_close(port);

	return retval;
}

static void klsi_105_close(struct usb_serial_port *port)
{
	int rc;

	/* send READ_OFF */
	rc = usb_control_msg(port->serial->dev,
			     usb_sndctrlpipe(port->serial->dev, 0),
			     KL5KUSB105A_SIO_CONFIGURE,
			     USB_TYPE_VENDOR | USB_DIR_OUT,
			     KL5KUSB105A_SIO_CONFIGURE_READ_OFF,
			     0, /* index */
			     NULL, 0,
			     KLSI_TIMEOUT);
	if (rc < 0)
		dev_err(&port->dev, "failed to disable read: %d\n", rc);

	/* shutdown our bulk reads and writes */
	usb_serial_generic_close(port);
}

/* We need to write a complete 64-byte data block and encode the
 * number actually sent in the first double-byte, LSB-order. That
 * leaves at most 62 bytes of payload.
 */
#define KLSI_HDR_LEN		2
static int klsi_105_prepare_write_buffer(struct usb_serial_port *port,
						void *dest, size_t size)
{
	unsigned char *buf = dest;
	int count;

	count = kfifo_out_locked(&port->write_fifo, buf + KLSI_HDR_LEN, size,
								&port->lock);
	put_unaligned_le16(count, buf);

	return count + KLSI_HDR_LEN;
}

/* The data received is preceded by a length double-byte in LSB-first order.
 */
static void klsi_105_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	unsigned len;

	/* empty urbs seem to happen, we ignore them */
	if (!urb->actual_length)
		return;

	if (urb->actual_length <= KLSI_HDR_LEN) {
		dev_dbg(&port->dev, "%s - malformed packet\n", __func__);
		return;
	}

	len = get_unaligned_le16(data);
	if (len > urb->actual_length - KLSI_HDR_LEN) {
		dev_dbg(&port->dev, "%s - packet length mismatch\n", __func__);
		len = urb->actual_length - KLSI_HDR_LEN;
	}

	tty_insert_flip_string(&port->port, data + KLSI_HDR_LEN, len);
	tty_flip_buffer_push(&port->port);
}

static void klsi_105_set_termios(struct tty_struct *tty,
				 struct usb_serial_port *port,
				 const struct ktermios *old_termios)
{
	struct klsi_105_private *priv = usb_get_serial_port_data(port);
	struct device *dev = &port->dev;
	unsigned int iflag = tty->termios.c_iflag;
	unsigned int old_iflag = old_termios->c_iflag;
	unsigned int cflag = tty->termios.c_cflag;
	unsigned int old_cflag = old_termios->c_cflag;
	struct klsi_105_port_settings *cfg;
	unsigned long flags;
	speed_t baud;

	cfg = kmalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return;

	/* lock while we are modifying the settings */
	spin_lock_irqsave(&priv->lock, flags);

	/*
	 * Update baud rate
	 */
	baud = tty_get_baud_rate(tty);

	switch (baud) {
	case 0: /* handled below */
		break;
	case 1200:
		priv->cfg.baudrate = kl5kusb105a_sio_b1200;
		break;
	case 2400:
		priv->cfg.baudrate = kl5kusb105a_sio_b2400;
		break;
	case 4800:
		priv->cfg.baudrate = kl5kusb105a_sio_b4800;
		break;
	case 9600:
		priv->cfg.baudrate = kl5kusb105a_sio_b9600;
		break;
	case 19200:
		priv->cfg.baudrate = kl5kusb105a_sio_b19200;
		break;
	case 38400:
		priv->cfg.baudrate = kl5kusb105a_sio_b38400;
		break;
	case 57600:
		priv->cfg.baudrate = kl5kusb105a_sio_b57600;
		break;
	case 115200:
		priv->cfg.baudrate = kl5kusb105a_sio_b115200;
		break;
	default:
		dev_dbg(dev, "unsupported baudrate, using 9600\n");
		priv->cfg.baudrate = kl5kusb105a_sio_b9600;
		baud = 9600;
		break;
	}

	/*
	 * FIXME: implement B0 handling
	 *
	 * Maybe this should be simulated by sending read disable and read
	 * enable messages?
	 */

	tty_encode_baud_rate(tty, baud, baud);

	if ((cflag & CSIZE) != (old_cflag & CSIZE)) {
		/* set the number of data bits */
		switch (cflag & CSIZE) {
		case CS5:
			dev_dbg(dev, "%s - 5 bits/byte not supported\n", __func__);
			spin_unlock_irqrestore(&priv->lock, flags);
			goto err;
		case CS6:
			dev_dbg(dev, "%s - 6 bits/byte not supported\n", __func__);
			spin_unlock_irqrestore(&priv->lock, flags);
			goto err;
		case CS7:
			priv->cfg.databits = kl5kusb105a_dtb_7;
			break;
		case CS8:
			priv->cfg.databits = kl5kusb105a_dtb_8;
			break;
		default:
			dev_err(dev, "CSIZE was not CS5-CS8, using default of 8\n");
			priv->cfg.databits = kl5kusb105a_dtb_8;
			break;
		}
	}

	/*
	 * Update line control register (LCR)
	 */
	if ((cflag & (PARENB|PARODD)) != (old_cflag & (PARENB|PARODD))
	    || (cflag & CSTOPB) != (old_cflag & CSTOPB)) {
		/* Not currently supported */
		tty->termios.c_cflag &= ~(PARENB|PARODD|CSTOPB);
	}
	/*
	 * Set flow control: well, I do not really now how to handle DTR/RTS.
	 * Just do what we have seen with SniffUSB on Win98.
	 */
	if ((iflag & IXOFF) != (old_iflag & IXOFF)
	    || (iflag & IXON) != (old_iflag & IXON)
	    ||  (cflag & CRTSCTS) != (old_cflag & CRTSCTS)) {
		/* Not currently supported */
		tty->termios.c_cflag &= ~CRTSCTS;
	}
	memcpy(cfg, &priv->cfg, sizeof(*cfg));
	spin_unlock_irqrestore(&priv->lock, flags);

	/* now commit changes to device */
	klsi_105_chg_port_settings(port, cfg);
err:
	kfree(cfg);
}

static int klsi_105_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct klsi_105_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	int rc;
	unsigned long line_state;

	rc = klsi_105_get_line_state(port, &line_state);
	if (rc < 0) {
		dev_err(&port->dev,
			"Reading line control failed (error = %d)\n", rc);
		/* better return value? EAGAIN? */
		return rc;
	}

	spin_lock_irqsave(&priv->lock, flags);
	priv->line_state = line_state;
	spin_unlock_irqrestore(&priv->lock, flags);
	dev_dbg(&port->dev, "%s - read line state 0x%lx\n", __func__, line_state);
	return (int)line_state;
}

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
