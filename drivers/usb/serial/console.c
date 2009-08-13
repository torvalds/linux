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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

static int debug;

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
	struct tty_struct *tty = NULL;
	struct ktermios *termios = NULL, dummy;

	dbg("%s", __func__);

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
	
	/* Sane default */
	if (baud == 0)
		baud = 9600;

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

	/*
	 * no need to check the index here: if the index is wrong, console
	 * code won't call us
	 */
	serial = usb_serial_get_by_index(co->index);
	if (serial == NULL) {
		/* no device is connected yet, sorry :( */
		err("No USB device connected to ttyUSB%i", co->index);
		return -ENODEV;
	}

	port = serial->port[0];
	tty_port_tty_set(&port->port, NULL);

	info->port = port;

	++port->port.count;
	if (port->port.count == 1) {
		if (serial->type->set_termios) {
			/*
			 * allocate a fake tty so the driver can initialize
			 * the termios structure, then later call set_termios to
			 * configure according to command line arguments
			 */
			tty = kzalloc(sizeof(*tty), GFP_KERNEL);
			if (!tty) {
				retval = -ENOMEM;
				err("no more memory");
				goto reset_open_count;
			}
			kref_init(&tty->kref);
			termios = kzalloc(sizeof(*termios), GFP_KERNEL);
			if (!termios) {
				retval = -ENOMEM;
				err("no more memory");
				goto free_tty;
			}
			memset(&dummy, 0, sizeof(struct ktermios));
			tty->termios = termios;
			tty_port_tty_set(&port->port, tty);
		}

		/* only call the device specific open if this
		 * is the first time the port is opened */
		if (serial->type->open)
			retval = serial->type->open(NULL, port, NULL);
		else
			retval = usb_serial_generic_open(NULL, port, NULL);

		if (retval) {
			err("could not open USB console port");
			goto free_termios;
		}

		if (serial->type->set_termios) {
			termios->c_cflag = cflag;
			tty_termios_encode_baud_rate(termios, baud, baud);
			serial->type->set_termios(tty, port, &dummy);

			tty_port_tty_set(&port->port, NULL);
			kfree(termios);
			kfree(tty);
		}
	}
	/* Now that any required fake tty operations are completed restore
	 * the tty port count */
	--port->port.count;
	/* The console is special in terms of closing the device so
	 * indicate this port is now acting as a system console. */
	port->console = 1;
	retval = 0;

out:
	return retval;
free_termios:
	kfree(termios);
	tty_port_tty_set(&port->port, NULL);
free_tty:
	kfree(tty);
reset_open_count:
	port->port.count = 0;
	goto out;
}

static void usb_console_write(struct console *co,
					const char *buf, unsigned count)
{
	static struct usbcons_info *info = &usbcons_info;
	struct usb_serial_port *port = info->port;
	struct usb_serial *serial;
	int retval = -ENODEV;

	if (!port || port->serial->dev->state == USB_STATE_NOTATTACHED)
		return;
	serial = port->serial;

	if (count == 0)
		return;

	dbg("%s - port %d, %d byte(s)", __func__, port->number, count);

	if (!port->console) {
		dbg("%s - port not opened", __func__);
		return;
	}

	while (count) {
		unsigned int i;
		unsigned int lf;
		/* search for LF so we can insert CR if necessary */
		for (i = 0, lf = 0 ; i < count ; i++) {
			if (*(buf + i) == 10) {
				lf = 1;
				i++;
				break;
			}
		}
		/* pass on to the driver specific version of this function if
		   it is available */
		if (serial->type->write)
			retval = serial->type->write(NULL, port, buf, i);
		else
			retval = usb_serial_generic_write(NULL, port, buf, i);
		dbg("%s - return value : %d", __func__, retval);
		if (lf) {
			/* append CR after LF */
			unsigned char cr = 13;
			if (serial->type->write)
				retval = serial->type->write(NULL,
								port, &cr, 1);
			else
				retval = usb_serial_generic_write(NULL,
								port, &cr, 1);
			dbg("%s - return value : %d", __func__, retval);
		}
		buf += i;
		count -= i;
	}
}

static struct tty_driver *usb_console_device(struct console *co, int *index)
{
	struct tty_driver **p = (struct tty_driver **)co->data;

	if (!*p)
		return NULL;

	*index = co->index;
	return *p;
}

static struct console usbcons = {
	.name =		"ttyUSB",
	.write =	usb_console_write,
	.device =	usb_console_device,
	.setup =	usb_console_setup,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
	.data = 	&usb_serial_tty_driver,
};

void usb_serial_console_disconnect(struct usb_serial *serial)
{
	if (serial && serial->port && serial->port[0]
				&& serial->port[0] == usbcons_info.port) {
		usb_serial_console_exit();
		usb_serial_put(serial);
	}
}

void usb_serial_console_init(int serial_debug, int minor)
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
		 * console_setup() is called (back) immediately (from
		 * register_console). console_write() is called immediately
		 * from register_console iff CON_PRINTBUFFER is set in flags.
		 */
		dbg("registering the USB serial console.");
		register_console(&usbcons);
	}
}

void usb_serial_console_exit(void)
{
	if (usbcons_info.port) {
		unregister_console(&usbcons);
		usbcons_info.port->console = 0;
		usbcons_info.port = NULL;
	}
}

