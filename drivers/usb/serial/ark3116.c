/*
 * Copyright (C) 2006
 *   Simon Schulz (ark3116_driver <at> auctionant.de)
 *
 * ark3116
 * - implements a driver for the arkmicro ark3116 chipset (vendor=0x6547,
 *   productid=0x0232) (used in a datacable called KQ-U8A)
 *
 * - based on code by krisfx -> thanks !!
 *   (see http://www.linuxquestions.org/questions/showthread.php?p=2184457#post2184457)
 *
 *  - based on logs created by usbsnoopy
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial.h>
#include <linux/uaccess.h>


static int debug;

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x6547, 0x0232) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

struct ark3116_private {
	spinlock_t lock;
	u8 termios_initialized;
};

static inline void ARK3116_SND(struct usb_serial *serial, int seq,
			       __u8 request, __u8 requesttype,
			       __u16 value, __u16 index)
{
	int result;
	result = usb_control_msg(serial->dev,
				 usb_sndctrlpipe(serial->dev, 0),
				 request, requesttype, value, index,
				 NULL, 0x00, 1000);
	dbg("%03d > ok", seq);
}

static inline void ARK3116_RCV(struct usb_serial *serial, int seq,
			       __u8 request, __u8 requesttype,
			       __u16 value, __u16 index, __u8 expected,
			       char *buf)
{
	int result;
	result = usb_control_msg(serial->dev,
				 usb_rcvctrlpipe(serial->dev, 0),
				 request, requesttype, value, index,
				 buf, 0x0000001, 1000);
	if (result)
		dbg("%03d < %d bytes [0x%02X]", seq, result,
		    ((unsigned char *)buf)[0]);
	else
		dbg("%03d < 0 bytes", seq);
}

static inline void ARK3116_RCV_QUIET(struct usb_serial *serial,
				     __u8 request, __u8 requesttype,
				     __u16 value, __u16 index, char *buf)
{
	usb_control_msg(serial->dev,
			usb_rcvctrlpipe(serial->dev, 0),
			request, requesttype, value, index,
			buf, 0x0000001, 1000);
}

static int ark3116_attach(struct usb_serial *serial)
{
	char *buf;
	struct ark3116_private *priv;
	int i;

	for (i = 0; i < serial->num_ports; ++i) {
		priv = kzalloc(sizeof(struct ark3116_private), GFP_KERNEL);
		if (!priv)
			goto cleanup;
		spin_lock_init(&priv->lock);

		usb_set_serial_port_data(serial->port[i], priv);
	}

	buf = kmalloc(1, GFP_KERNEL);
	if (!buf) {
		dbg("error kmalloc -> out of mem?");
		goto cleanup;
	}

	/* 3 */
	ARK3116_SND(serial, 3, 0xFE, 0x40, 0x0008, 0x0002);
	ARK3116_SND(serial, 4, 0xFE, 0x40, 0x0008, 0x0001);
	ARK3116_SND(serial, 5, 0xFE, 0x40, 0x0000, 0x0008);
	ARK3116_SND(serial, 6, 0xFE, 0x40, 0x0000, 0x000B);

	/* <-- seq7 */
	ARK3116_RCV(serial,  7, 0xFE, 0xC0, 0x0000, 0x0003, 0x00, buf);
	ARK3116_SND(serial,  8, 0xFE, 0x40, 0x0080, 0x0003);
	ARK3116_SND(serial,  9, 0xFE, 0x40, 0x001A, 0x0000);
	ARK3116_SND(serial, 10, 0xFE, 0x40, 0x0000, 0x0001);
	ARK3116_SND(serial, 11, 0xFE, 0x40, 0x0000, 0x0003);

	/* <-- seq12 */
	ARK3116_RCV(serial, 12, 0xFE, 0xC0, 0x0000, 0x0004, 0x00, buf);
	ARK3116_SND(serial, 13, 0xFE, 0x40, 0x0000, 0x0004);

	/* 14 */
	ARK3116_RCV(serial, 14, 0xFE, 0xC0, 0x0000, 0x0004, 0x00, buf);
	ARK3116_SND(serial, 15, 0xFE, 0x40, 0x0000, 0x0004);

	/* 16 */
	ARK3116_RCV(serial, 16, 0xFE, 0xC0, 0x0000, 0x0004, 0x00, buf);
	/* --> seq17 */
	ARK3116_SND(serial, 17, 0xFE, 0x40, 0x0001, 0x0004);

	/* <-- seq18 */
	ARK3116_RCV(serial, 18, 0xFE, 0xC0, 0x0000, 0x0004, 0x01, buf);

	/* --> seq19 */
	ARK3116_SND(serial, 19, 0xFE, 0x40, 0x0003, 0x0004);

	/* <-- seq20 */
	/* seems like serial port status info (RTS, CTS, ...) */
	/* returns modem control line status?! */
	ARK3116_RCV(serial, 20, 0xFE, 0xC0, 0x0000, 0x0006, 0xFF, buf);

	/* set 9600 baud & do some init?! */
	ARK3116_SND(serial, 147, 0xFE, 0x40, 0x0083, 0x0003);
	ARK3116_SND(serial, 148, 0xFE, 0x40, 0x0038, 0x0000);
	ARK3116_SND(serial, 149, 0xFE, 0x40, 0x0001, 0x0001);
	ARK3116_SND(serial, 150, 0xFE, 0x40, 0x0003, 0x0003);
	ARK3116_RCV(serial, 151, 0xFE, 0xC0, 0x0000, 0x0004, 0x03, buf);
	ARK3116_SND(serial, 152, 0xFE, 0x40, 0x0000, 0x0003);
	ARK3116_RCV(serial, 153, 0xFE, 0xC0, 0x0000, 0x0003, 0x00, buf);
	ARK3116_SND(serial, 154, 0xFE, 0x40, 0x0003, 0x0003);

	kfree(buf);
	return 0;

cleanup:
	for (--i; i >= 0; --i) {
		kfree(usb_get_serial_port_data(serial->port[i]));
		usb_set_serial_port_data(serial->port[i], NULL);
	}
	return -ENOMEM;
}

static void ark3116_set_termios(struct tty_struct *tty,
				struct usb_serial_port *port,
				struct ktermios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	struct ktermios *termios = tty->termios;
	unsigned int cflag = termios->c_cflag;
	unsigned long flags;
	int baud;
	int ark3116_baud;
	char *buf;
	char config;

	config = 0;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	if (!priv->termios_initialized) {
		*termios = tty_std_termios;
		termios->c_cflag = B9600 | CS8
					      | CREAD | HUPCL | CLOCAL;
		termios->c_ispeed = 9600;
		termios->c_ospeed = 9600;
		priv->termios_initialized = 1;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	cflag = termios->c_cflag;
	termios->c_cflag &= ~(CMSPAR|CRTSCTS);

	buf = kmalloc(1, GFP_KERNEL);
	if (!buf) {
		dbg("error kmalloc");
		*termios = *old_termios;
		return;
	}

	/* set data bit count (8/7/6/5) */
	if (cflag & CSIZE) {
		switch (cflag & CSIZE) {
		case CS5:
			config |= 0x00;
			dbg("setting CS5");
			break;
		case CS6:
			config |= 0x01;
			dbg("setting CS6");
			break;
		case CS7:
			config |= 0x02;
			dbg("setting CS7");
			break;
		default:
			dbg("CSIZE was set but not CS5-CS8, using CS8!");
			/* fall through */
		case CS8:
			config |= 0x03;
			dbg("setting CS8");
			break;
		}
	}

	/* set parity (NONE/EVEN/ODD) */
	if (cflag & PARENB) {
		if (cflag & PARODD) {
			config |= 0x08;
			dbg("setting parity to ODD");
		} else {
			config |= 0x18;
			dbg("setting parity to EVEN");
		}
	} else {
		dbg("setting parity to NONE");
	}

	/* set stop bit (1/2) */
	if (cflag & CSTOPB) {
		config |= 0x04;
		dbg("setting 2 stop bits");
	} else {
		dbg("setting 1 stop bit");
	}

	/* set baudrate */
	baud = tty_get_baud_rate(tty);

	switch (baud) {
	case 75:
	case 150:
	case 300:
	case 600:
	case 1200:
	case 1800:
	case 2400:
	case 4800:
	case 9600:
	case 19200:
	case 38400:
	case 57600:
	case 115200:
	case 230400:
	case 460800:
		/* Report the resulting rate back to the caller */
		tty_encode_baud_rate(tty, baud, baud);
		break;
	/* set 9600 as default (if given baudrate is invalid for example) */
	default:
		tty_encode_baud_rate(tty, 9600, 9600);
	case 0:
		baud = 9600;
	}

	/*
	 * found by try'n'error, be careful, maybe there are other options
	 * for multiplicator etc! (3.5 for example)
	 */
	if (baud == 460800)
		/* strange, for 460800 the formula is wrong
		 * if using round() then 9600baud is wrong) */
		ark3116_baud = 7;
	else
		ark3116_baud = 3000000 / baud;

	/* ? */
	ARK3116_RCV(serial, 0, 0xFE, 0xC0, 0x0000, 0x0003, 0x03, buf);

	/* offset = buf[0]; */
	/* offset = 0x03; */
	/* dbg("using 0x%04X as target for 0x0003:", 0x0080 + offset); */

	/* set baudrate */
	dbg("setting baudrate to %d (->reg=%d)", baud, ark3116_baud);
	ARK3116_SND(serial, 147, 0xFE, 0x40, 0x0083, 0x0003);
	ARK3116_SND(serial, 148, 0xFE, 0x40,
			    (ark3116_baud & 0x00FF), 0x0000);
	ARK3116_SND(serial, 149, 0xFE, 0x40,
			    (ark3116_baud & 0xFF00) >> 8, 0x0001);
	ARK3116_SND(serial, 150, 0xFE, 0x40, 0x0003, 0x0003);

	/* ? */
	ARK3116_RCV(serial, 151, 0xFE, 0xC0, 0x0000, 0x0004, 0x03, buf);
	ARK3116_SND(serial, 152, 0xFE, 0x40, 0x0000, 0x0003);

	/* set data bit count, stop bit count & parity: */
	dbg("updating bit count, stop bit or parity (cfg=0x%02X)", config);
	ARK3116_RCV(serial, 153, 0xFE, 0xC0, 0x0000, 0x0003, 0x00, buf);
	ARK3116_SND(serial, 154, 0xFE, 0x40, config, 0x0003);

	if (cflag & CRTSCTS)
		dbg("CRTSCTS not supported by chipset?!");

	/* TEST ARK3116_SND(154, 0xFE, 0x40, 0xFFFF, 0x0006); */

	kfree(buf);

	return;
}

static int ark3116_open(struct tty_struct *tty, struct usb_serial_port *port,
					struct file *filp)
{
	struct ktermios tmp_termios;
	struct usb_serial *serial = port->serial;
	char *buf;
	int result = 0;

	dbg("%s - port %d", __func__, port->number);

	buf = kmalloc(1, GFP_KERNEL);
	if (!buf) {
		dbg("error kmalloc -> out of mem?");
		return -ENOMEM;
	}

	result = usb_serial_generic_open(tty, port, filp);
	if (result)
		goto err_out;

	/* open */
	ARK3116_RCV(serial, 111, 0xFE, 0xC0, 0x0000, 0x0003, 0x02, buf);

	ARK3116_SND(serial, 112, 0xFE, 0x40, 0x0082, 0x0003);
	ARK3116_SND(serial, 113, 0xFE, 0x40, 0x001A, 0x0000);
	ARK3116_SND(serial, 114, 0xFE, 0x40, 0x0000, 0x0001);
	ARK3116_SND(serial, 115, 0xFE, 0x40, 0x0002, 0x0003);

	ARK3116_RCV(serial, 116, 0xFE, 0xC0, 0x0000, 0x0004, 0x03, buf);
	ARK3116_SND(serial, 117, 0xFE, 0x40, 0x0002, 0x0004);

	ARK3116_RCV(serial, 118, 0xFE, 0xC0, 0x0000, 0x0004, 0x02, buf);
	ARK3116_SND(serial, 119, 0xFE, 0x40, 0x0000, 0x0004);

	ARK3116_RCV(serial, 120, 0xFE, 0xC0, 0x0000, 0x0004, 0x00, buf);

	ARK3116_SND(serial, 121, 0xFE, 0x40, 0x0001, 0x0004);

	ARK3116_RCV(serial, 122, 0xFE, 0xC0, 0x0000, 0x0004, 0x01, buf);

	ARK3116_SND(serial, 123, 0xFE, 0x40, 0x0003, 0x0004);

	/* returns different values (control lines?!) */
	ARK3116_RCV(serial, 124, 0xFE, 0xC0, 0x0000, 0x0006, 0xFF, buf);

	/* initialise termios */
	if (tty)
		ark3116_set_termios(tty, port, &tmp_termios);

err_out:
	kfree(buf);

	return result;
}

static int ark3116_ioctl(struct tty_struct *tty, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct serial_struct serstruct;
	void __user *user_arg = (void __user *)arg;

	switch (cmd) {
	case TIOCGSERIAL:
		/* XXX: Some of these values are probably wrong. */
		memset(&serstruct, 0, sizeof(serstruct));
		serstruct.type = PORT_16654;
		serstruct.line = port->serial->minor;
		serstruct.port = port->number;
		serstruct.custom_divisor = 0;
		serstruct.baud_base = 460800;

		if (copy_to_user(user_arg, &serstruct, sizeof(serstruct)))
			return -EFAULT;

		return 0;
	case TIOCSSERIAL:
		if (copy_from_user(&serstruct, user_arg, sizeof(serstruct)))
			return -EFAULT;
		return 0;
	default:
		dbg("%s cmd 0x%04x not supported", __func__, cmd);
		break;
	}

	return -ENOIOCTLCMD;
}

static int ark3116_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	char *buf;
	char temp;

	/* seems like serial port status info (RTS, CTS, ...) is stored
	 * in reg(?) 0x0006
	 * pcb connection point 11 = GND -> sets bit4 of response
	 * pcb connection point  7 = GND -> sets bit6 of response
	 */

	buf = kmalloc(1, GFP_KERNEL);
	if (!buf) {
		dbg("error kmalloc");
		return -ENOMEM;
	}

	/* read register */
	ARK3116_RCV_QUIET(serial, 0xFE, 0xC0, 0x0000, 0x0006, buf);
	temp = buf[0];
	kfree(buf);

	/* i do not really know if bit4=CTS and bit6=DSR... just a
	 * quick guess!
	 */
	return (temp & (1<<4) ? TIOCM_CTS : 0)
	       | (temp & (1<<6) ? TIOCM_DSR : 0);
}

static struct usb_driver ark3116_driver = {
	.name =		"ark3116",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id =	1,
};

static struct usb_serial_driver ark3116_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"ark3116",
	},
	.id_table =		id_table,
	.usb_driver =		&ark3116_driver,
	.num_ports =		1,
	.attach =		ark3116_attach,
	.set_termios =		ark3116_set_termios,
	.ioctl =		ark3116_ioctl,
	.tiocmget =		ark3116_tiocmget,
	.open =			ark3116_open,
};

static int __init ark3116_init(void)
{
	int retval;

	retval = usb_serial_register(&ark3116_device);
	if (retval)
		return retval;
	retval = usb_register(&ark3116_driver);
	if (retval)
		usb_serial_deregister(&ark3116_device);
	return retval;
}

static void __exit ark3116_exit(void)
{
	usb_deregister(&ark3116_driver);
	usb_serial_deregister(&ark3116_device);
}

module_init(ark3116_init);
module_exit(ark3116_exit);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");

