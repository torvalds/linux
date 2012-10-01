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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/serial.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

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
	int retval;
	struct tty_struct *tty = NULL;
	struct ktermios dummy;

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
		pr_err("No USB device connected to ttyUSB%i\n", co->index);
		return -ENODEV;
	}

	retval = usb_autopm_get_interface(serial->interface);
	if (retval)
		goto error_get_interface;

	port = serial->port[co->index - serial->minor];
	tty_port_tty_set(&port->port, NULL);

	info->port = port;

	++port->port.count;
	if (!test_bit(ASYNCB_INITIALIZED, &port->port.flags)) {
		if (serial->type->set_termios) {
			/*
			 * allocate a fake tty so the driver can initialize
			 * the termios structure, then later call set_termios to
			 * configure according to command line arguments
			 */
			tty = kzalloc(sizeof(*tty), GFP_KERNEL);
			if (!tty) {
				retval = -ENOMEM;
				dev_err(&port->dev, "no more memory\n");
				goto reset_open_count;
			}
			kref_init(&tty->kref);
			tty_port_tty_set(&port->port, tty);
			tty->driver = usb_serial_tty_driver;
			tty->index = co->index;
			if (tty_init_termios(tty)) {
				retval = -ENOMEM;
				dev_err(&port->dev, "no more memory\n");
				goto free_tty;
			}
		}

		/* only call the device specific open if this
		 * is the first time the port is opened */
		if (serial->type->open)
			retval = serial->type->open(NULL, port);
		else
			retval = usb_serial_generic_open(NULL, port);

		if (retval) {
			dev_err(&port->dev, "could not open USB console port\n");
			goto fail;
		}

		if (serial->type->set_termios) {
			tty->termios.c_cflag = cflag;
			tty_termios_encode_baud_rate(&tty->termios, baud, baud);
			memset(&dummy, 0, sizeof(struct ktermios));
			serial->type->set_termios(tty, port, &dummy);

			tty_port_tty_set(&port->port, NULL);
			kfree(tty);
		}
		set_bit(ASYNCB_INITIALIZED, &port->port.flags);
	}
	/* Now that any required fake tty operations are completed restore
	 * the tty port count */
	--port->port.count;
	/* The console is special in terms of closing the device so
	 * indicate this port is now acting as a system console. */
	port->port.console = 1;

	mutex_unlock(&serial->disc_mutex);
	return retval;

 fail:
	tty_port_tty_set(&port->port, NULL);
 free_tty:
	kfree(tty);
 reset_open_count:
	port->port.count = 0;
	usb_autopm_put_interface(serial->interface);
 error_get_interface:
	usb_serial_put(serial);
	mutex_unlock(&serial->disc_mutex);
	return retval;
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

	pr_debug("%s - port %d, %d byte(s)\n", __func__, port->number, count);

	if (!port->port.console) {
		pr_debug("%s - port not opened\n", __func__);
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
		pr_debug("%s - return value : %d\n", __func__, retval);
		if (lf) {
			/* append CR after LF */
			unsigned char cr = 13;
			if (serial->type->write)
				retval = serial->type->write(NULL,
								port, &cr, 1);
			else
				retval = usb_serial_generic_write(NULL,
								port, &cr, 1);
			pr_debug("%s - return value : %d\n", __func__, retval);
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

void usb_serial_console_init(int minor)
{
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
		pr_debug("registering the USB serial console.\n");
		register_console(&usbcons);
	}
}

void usb_serial_console_exit(void)
{
	if (usbcons_info.port) {
		unregister_console(&usbcons);
		usbcons_info.port->port.console = 0;
		usbcons_info.port = NULL;
	}
}

