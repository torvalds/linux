/*
 * USB Serial Console driver
 *
 * Copyright (C) 2001 - 2002 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 * 
 * Thanks to Randy Dunlap for the original version of this code.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/usb.h>

static int debug;

#include "usb-serial.h"

struct usbcons_info {
	int			magic;
	int			break_flag;
	struct usb_serial_port	*port;
};

static struct usbcons_info usbcons_info;
static struct console usbcons;

/*
 * ------------------------------------------------------------
 * USB Serial console driver
 *
 * Much of the code here is copied from drivers/char/serial.c
 * and implements a phony serial console in the same way that
 * serial.c does so that in case some software queries it,
 * it will get the same results.
 *
 * Things that are different from the way the serial port code
 * does things, is that we call the lower level usb-serial
 * driver code to initialize the device, and we set the initial
 * console speeds based on the command line arguments.
 * ------------------------------------------------------------
 */


/*
 * The parsing of the command line works exactly like the
 * serial.c code, except that the specifier is "ttyUSB" instead
 * of "ttyS".
 */
static int usb_console_setup(struct console *co, char *options)
{
	struct usbcons_info *info = &usbcons_info;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int doflow = 0;
	int cflag = CREAD | HUPCL | CLOCAL;
	char *s;
	struct usb_serial *serial;
	struct usb_serial_port *port;
	int retval = 0;
	struct tty_struct *tty;
	struct termios *termios;

	dbg ("%s", __FUNCTION__);

	if (options) {
		baud = simple_strtoul(options, NULL, 10);
		s = options;
		while (*s >= '0' && *s <= '9')
			s++;
		if (*s)
			parity = *s++;
		if (*s)
			bits   = *s++ - '0';
		if (*s)
			doflow = (*s++ == 'r');
	}

	/* build a cflag setting */
	switch (baud) {
		case 1200:
			cflag |= B1200;
			break;
		case 2400:
			cflag |= B2400;
			break;
		case 4800:
			cflag |= B4800;
			break;
		case 19200:
			cflag |= B19200;
			break;
		case 38400:
			cflag |= B38400;
			break;
		case 57600:
			cflag |= B57600;
			break;
		case 115200:
			cflag |= B115200;
			break;
		case 9600:
		default:
			cflag |= B9600;
			/*
			 * Set this to a sane value to prevent a divide error
			 */
			baud  = 9600;
			break;
	}
	switch (bits) {
		case 7:
			cflag |= CS7;
			break;
		default:
		case 8:
			cflag |= CS8;
			break;
	}
	switch (parity) {
		case 'o': case 'O':
			cflag |= PARODD;
			break;
		case 'e': case 'E':
			cflag |= PARENB;
			break;
	}
	co->cflag = cflag;

	/* grab the first serial port that happens to be connected */
	serial = usb_serial_get_by_index(0);
	if (serial == NULL) {
		/* no device is connected yet, sorry :( */
		err ("No USB device connected to ttyUSB0");
		return -ENODEV;
	}

	port = serial->port[0];
	port->tty = NULL;

	info->port = port;
	 
	++port->open_count;
	if (port->open_count == 1) {
		/* only call the device specific open if this 
		 * is the first time the port is opened */
		if (serial->type->open)
			retval = serial->type->open(port, NULL);
		else
			retval = usb_serial_generic_open(port, NULL);
		if (retval)
			port->open_count = 0;
	}

	if (retval) {
		err ("could not open USB console port");
		return retval;
	}

	if (serial->type->set_termios) {
		/* build up a fake tty structure so that the open call has something
		 * to look at to get the cflag value */
		tty = kmalloc (sizeof (*tty), GFP_KERNEL);
		if (!tty) {
			err ("no more memory");
			return -ENOMEM;
		}
		termios = kmalloc (sizeof (*termios), GFP_KERNEL);
		if (!termios) {
			err ("no more memory");
			kfree (tty);
			return -ENOMEM;
		}
		memset (tty, 0x00, sizeof(*tty));
		memset (termios, 0x00, sizeof(*termios));
		termios->c_cflag = cflag;
		tty->termios = termios;
		port->tty = tty;

		/* set up the initial termios settings */
		serial->type->set_termios(port, NULL);
		port->tty = NULL;
		kfree (termios);
		kfree (tty);
	}

	return retval;
}

static void usb_console_write(struct console *co, const char *buf, unsigned count)
{
	static struct usbcons_info *info = &usbcons_info;
	struct usb_serial_port *port = info->port;
	struct usb_serial *serial;
	int retval = -ENODEV;

	if (!port)
		return;
	serial = port->serial;

	if (count == 0)
		return;

	dbg("%s - port %d, %d byte(s)", __FUNCTION__, port->number, count);

	if (!port->open_count) {
		dbg ("%s - port not opened", __FUNCTION__);
		return;
	}

	while (count) {
		unsigned int i;
		unsigned int lf;
		/* search for LF so we can insert CR if necessary */
		for (i=0, lf=0 ; i < count ; i++) {
			if (*(buf + i) == 10) {
				lf = 1;
				i++;
				break;
			}
		}
		/* pass on to the driver specific version of this function if it is available */
		if (serial->type->write)
			retval = serial->type->write(port, buf, i);
		else
			retval = usb_serial_generic_write(port, buf, i);
		dbg("%s - return value : %d", __FUNCTION__, retval);
		if (lf) {
			/* append CR after LF */
			unsigned char cr = 13;
			if (serial->type->write)
				retval = serial->type->write(port, &cr, 1);
			else
				retval = usb_serial_generic_write(port, &cr, 1);
			dbg("%s - return value : %d", __FUNCTION__, retval);
		}
		buf += i;
		count -= i;
	}
}

static struct console usbcons = {
	.name =		"ttyUSB",
	.write =	usb_console_write,
	.setup =	usb_console_setup,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};

void usb_serial_console_init (int serial_debug, int minor)
{
	debug = serial_debug;

	if (minor == 0) {
		/* 
		 * Call register_console() if this is the first device plugged
		 * in.  If we call it earlier, then the callback to
		 * console_setup() will fail, as there is not a device seen by
		 * the USB subsystem yet.
		 */
		/*
		 * Register console.
		 * NOTES:
		 * console_setup() is called (back) immediately (from register_console).
		 * console_write() is called immediately from register_console iff
		 * CON_PRINTBUFFER is set in flags.
		 */
		dbg ("registering the USB serial console.");
		register_console(&usbcons);
	}
}

void usb_serial_console_exit (void)
{
	unregister_console(&usbcons);
}

