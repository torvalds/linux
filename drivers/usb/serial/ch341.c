/*
 * Copyright 2007, Frank A Kingswood <frank@kingswood-consulting.co.uk>
 *
 * ch341.c implements a serial port driver for the Winchiphead CH341.
 *
 * The CH341 device can be used to implement an RS232 asynchronous
 * serial port, an IEEE-1284 parallel printer port or a memory-like
 * interface. In all cases the CH341 supports an I2C interface as well.
 * This driver only supports the asynchronous serial interface.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial.h>

#define DEFAULT_BAUD_RATE 2400
#define DEFAULT_TIMEOUT   1000

static int debug;

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x4348, 0x5523) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

struct ch341_private {
	unsigned baud_rate;
	u8 dtr;
	u8 rts;
};

static int ch341_control_out(struct usb_device *dev, u8 request,
			     u16 value, u16 index)
{
	int r;
	dbg("ch341_control_out(%02x,%02x,%04x,%04x)", USB_DIR_OUT|0x40,
		(int)request, (int)value, (int)index);

	r = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), request,
			    USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			    value, index, NULL, 0, DEFAULT_TIMEOUT);

	return r;
}

static int ch341_control_in(struct usb_device *dev,
			    u8 request, u16 value, u16 index,
			    char *buf, unsigned bufsize)
{
	int r;
	dbg("ch341_control_in(%02x,%02x,%04x,%04x,%p,%u)", USB_DIR_IN|0x40,
		(int)request, (int)value, (int)index, buf, (int)bufsize);

	r = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), request,
			    USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			    value, index, buf, bufsize, DEFAULT_TIMEOUT);
	return r;
}

static int ch341_set_baudrate(struct usb_device *dev,
			      struct ch341_private *priv)
{
	short a, b;
	int r;

	dbg("ch341_set_baudrate(%d)", priv->baud_rate);
	switch (priv->baud_rate) {
	case 2400:
		a = 0xd901;
		b = 0x0038;
		break;
	case 4800:
		a = 0x6402;
		b = 0x001f;
		break;
	case 9600:
		a = 0xb202;
		b = 0x0013;
		break;
	case 19200:
		a = 0xd902;
		b = 0x000d;
		break;
	case 38400:
		a = 0x6403;
		b = 0x000a;
		break;
	case 115200:
		a = 0xcc03;
		b = 0x0008;
		break;
	default:
		return -EINVAL;
	}

	r = ch341_control_out(dev, 0x9a, 0x1312, a);
	if (!r)
		r = ch341_control_out(dev, 0x9a, 0x0f2c, b);

	return r;
}

static int ch341_set_handshake(struct usb_device *dev,
			       struct ch341_private *priv)
{
	dbg("ch341_set_handshake(%d,%d)", priv->dtr, priv->rts);
	return ch341_control_out(dev, 0xa4,
		~((priv->dtr?1<<5:0)|(priv->rts?1<<6:0)), 0);
}

static int ch341_get_status(struct usb_device *dev)
{
	char *buffer;
	int r;
	const unsigned size = 8;

	dbg("ch341_get_status()");

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	r = ch341_control_in(dev, 0x95, 0x0706, 0, buffer, size);
	if ( r < 0)
		goto out;

	/* Not having the datasheet for the CH341, we ignore the bytes returned
	 * from the device. Return error if the device did not respond in time.
	 */
	r = 0;

out:	kfree(buffer);
	return r;
}

/* -------------------------------------------------------------------------- */

static int ch341_configure(struct usb_device *dev, struct ch341_private *priv)
{
	char *buffer;
	int r;
	const unsigned size = 8;

	dbg("ch341_configure()");

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	/* expect two bytes 0x27 0x00 */
	r = ch341_control_in(dev, 0x5f, 0, 0, buffer, size);
	if (r < 0)
		goto out;

	r = ch341_control_out(dev, 0xa1, 0, 0);
	if (r < 0)
		goto out;

	r = ch341_set_baudrate(dev, priv);
	if (r < 0)
		goto out;

	/* expect two bytes 0x56 0x00 */
	r = ch341_control_in(dev, 0x95, 0x2518, 0, buffer, size);
	if (r < 0)
		goto out;

	r = ch341_control_out(dev, 0x9a, 0x2518, 0x0050);
	if (r < 0)
		goto out;

	/* expect 0xff 0xee */
	r = ch341_get_status(dev);
	if (r < 0)
		goto out;

	r = ch341_control_out(dev, 0xa1, 0x501f, 0xd90a);
	if (r < 0)
		goto out;

	r = ch341_set_baudrate(dev, priv);
	if (r < 0)
		goto out;

	r = ch341_set_handshake(dev, priv);
	if (r < 0)
		goto out;

	/* expect 0x9f 0xee */
	r = ch341_get_status(dev);

out:	kfree(buffer);
	return r;
}

/* allocate private data */
static int ch341_attach(struct usb_serial *serial)
{
	struct ch341_private *priv;
	int r;

	dbg("ch341_attach()");

	/* private data */
	priv = kzalloc(sizeof(struct ch341_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->baud_rate = DEFAULT_BAUD_RATE;
	priv->dtr = 1;
	priv->rts = 1;

	r = ch341_configure(serial->dev, priv);
	if (r < 0)
		goto error;

	usb_set_serial_port_data(serial->port[0], priv);
	return 0;

error:	kfree(priv);
	return r;
}

/* open this device, set default parameters */
static int ch341_open(struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial = port->serial;
	struct ch341_private *priv = usb_get_serial_port_data(serial->port[0]);
	int r;

	dbg("ch341_open()");

	priv->baud_rate = DEFAULT_BAUD_RATE;
	priv->dtr = 1;
	priv->rts = 1;

	r = ch341_configure(serial->dev, priv);
	if (r)
		goto out;

	r = ch341_set_handshake(serial->dev, priv);
	if (r)
		goto out;

	r = ch341_set_baudrate(serial->dev, priv);
	if (r)
		goto out;

	r = usb_serial_generic_open(port, filp);

out:	return r;
}

/* Old_termios contains the original termios settings and
 * tty->termios contains the new setting to be used.
 */
static void ch341_set_termios(struct usb_serial_port *port,
			      struct ktermios *old_termios)
{
	struct ch341_private *priv = usb_get_serial_port_data(port);
	struct tty_struct *tty = port->tty;
	unsigned baud_rate;

	dbg("ch341_set_termios()");

	baud_rate = tty_get_baud_rate(tty);

	switch (baud_rate) {
	case 2400:
	case 4800:
	case 9600:
	case 19200:
	case 38400:
	case 115200:
		priv->baud_rate = baud_rate;
		break;
	default:
		dbg("Rate %d not supported, using %d",
			baud_rate, DEFAULT_BAUD_RATE);
		priv->baud_rate = DEFAULT_BAUD_RATE;
	}

	ch341_set_baudrate(port->serial->dev, priv);

	/* Unimplemented:
	 * (cflag & CSIZE) : data bits [5, 8]
	 * (cflag & PARENB) : parity {NONE, EVEN, ODD}
	 * (cflag & CSTOPB) : stop bits [1, 2]
	 */

	 /* Copy back the old hardware settings */
	 tty_termios_copy_hw(tty->termios, old_termios);
	 /* And re-encode with the new baud */
	 tty_encode_baud_rate(tty, baud_rate, baud_rate);
}

static struct usb_driver ch341_driver = {
	.name		= "ch341",
	.probe		= usb_serial_probe,
	.disconnect	= usb_serial_disconnect,
	.id_table	= id_table,
	.no_dynamic_id	= 1,
};

static struct usb_serial_driver ch341_device = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "ch341-uart",
	},
	.id_table         = id_table,
	.usb_driver       = &ch341_driver,
	.num_interrupt_in = NUM_DONT_CARE,
	.num_bulk_in      = 1,
	.num_bulk_out     = 1,
	.num_ports        = 1,
	.open             = ch341_open,
	.set_termios      = ch341_set_termios,
	.attach           = ch341_attach,
};

static int __init ch341_init(void)
{
	int retval;

	retval = usb_serial_register(&ch341_device);
	if (retval)
		return retval;
	retval = usb_register(&ch341_driver);
	if (retval)
		usb_serial_deregister(&ch341_device);
	return retval;
}

static void __exit ch341_exit(void)
{
	usb_deregister(&ch341_driver);
	usb_serial_deregister(&ch341_device);
}

module_init(ch341_init);
module_exit(ch341_exit);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");

/* EOF ch341.c */
