/*
 * USB Empeg empeg-car player driver
 *
 *	Copyright (C) 2000, 2001
 *	    Gary Brubaker (xavyer@ix.netcom.com)
 *
 *	Copyright (C) 1999 - 2001
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License, as published by
 *	the Free Software Foundation, version 2.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this
 * driver
 *
 * (07/16/2001) gb
 *	remove unused code in empeg_close() (thanks to Oliver Neukum for
 *	pointing this out) and rewrote empeg_set_termios().
 *
 * (05/30/2001) gkh
 *	switched from using spinlock to a semaphore, which fixes lots of
 * problems.
 *
 * (04/08/2001) gb
 *      Identify version on module load.
 *
 * (01/22/2001) gb
 *	Added write_room() and chars_in_buffer() support.
 *
 * (12/21/2000) gb
 *	Moved termio stuff inside the port->active check.
 *	Moved MOD_DEC_USE_COUNT to end of empeg_close().
 *
 * (12/03/2000) gb
 *	Added tty->ldisc.set_termios(port, tty, NULL) to empeg_open().
 *	This notifies the tty driver that the termios have changed.
 *
 * (11/13/2000) gb
 *	Moved tty->low_latency = 1 from empeg_read_bulk_callback() to
 *	empeg_open() (It only needs to be set once - Doh!)
 *
 * (11/11/2000) gb
 *	Updated to work with id_table structure.
 *
 * (11/04/2000) gb
 *	Forked this from visor.c, and hacked it up to work with an
 *	Empeg ltd. empeg-car player.  Constructive criticism welcomed.
 *	I would like to say, 'Thank You' to Greg Kroah-Hartman for the
 *	use of his code, and for his guidance, advice and patience. :)
 *	A 'Thank You' is in order for John Ripley of Empeg ltd for his
 *	advice, and patience too.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

static int debug;

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.2"
#define DRIVER_AUTHOR "Greg Kroah-Hartman <greg@kroah.com>, Gary Brubaker <xavyer@ix.netcom.com>"
#define DRIVER_DESC "USB Empeg Mark I/II Driver"

#define EMPEG_VENDOR_ID			0x084f
#define EMPEG_PRODUCT_ID		0x0001

/* function prototypes for an empeg-car player */
static int  empeg_open(struct tty_struct *tty, struct usb_serial_port *port,
						struct file *filp);
static void empeg_close(struct tty_struct *tty, struct usb_serial_port *port,
						struct file *filp);
static int  empeg_write(struct tty_struct *tty, struct usb_serial_port *port,
						const unsigned char *buf,
						int count);
static int  empeg_write_room(struct tty_struct *tty);
static int  empeg_chars_in_buffer(struct tty_struct *tty);
static void empeg_throttle(struct tty_struct *tty);
static void empeg_unthrottle(struct tty_struct *tty);
static int  empeg_startup(struct usb_serial *serial);
static void empeg_shutdown(struct usb_serial *serial);
static void empeg_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios);
static void empeg_write_bulk_callback(struct urb *urb);
static void empeg_read_bulk_callback(struct urb *urb);

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(EMPEG_VENDOR_ID, EMPEG_PRODUCT_ID) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver empeg_driver = {
	.name =		"empeg",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id = 	1,
};

static struct usb_serial_driver empeg_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"empeg",
	},
	.id_table =		id_table,
	.usb_driver = 		&empeg_driver,
	.num_ports =		1,
	.open =			empeg_open,
	.close =		empeg_close,
	.throttle =		empeg_throttle,
	.unthrottle =		empeg_unthrottle,
	.attach =		empeg_startup,
	.shutdown =		empeg_shutdown,
	.set_termios =		empeg_set_termios,
	.write =		empeg_write,
	.write_room =		empeg_write_room,
	.chars_in_buffer =	empeg_chars_in_buffer,
	.write_bulk_callback =	empeg_write_bulk_callback,
	.read_bulk_callback =	empeg_read_bulk_callback,
};

#define NUM_URBS			16
#define URB_TRANSFER_BUFFER_SIZE	4096

static struct urb	*write_urb_pool[NUM_URBS];
static spinlock_t	write_urb_pool_lock;
static int		bytes_in;
static int		bytes_out;

/******************************************************************************
 * Empeg specific driver functions
 ******************************************************************************/
static int empeg_open(struct tty_struct *tty, struct usb_serial_port *port,
				struct file *filp)
{
	struct usb_serial *serial = port->serial;
	int result = 0;

	dbg("%s - port %d", __func__, port->number);

	/* Force default termio settings */
	empeg_set_termios(tty, port, NULL) ;

	bytes_in = 0;
	bytes_out = 0;

	/* Start reading from the device */
	usb_fill_bulk_urb(
		port->read_urb,
		serial->dev,
		usb_rcvbulkpipe(serial->dev,
			port->bulk_in_endpointAddress),
		port->read_urb->transfer_buffer,
		port->read_urb->transfer_buffer_length,
		empeg_read_bulk_callback,
		port);

	result = usb_submit_urb(port->read_urb, GFP_KERNEL);

	if (result)
		dev_err(&port->dev,
			"%s - failed submitting read urb, error %d\n",
							__func__, result);

	return result;
}


static void empeg_close(struct tty_struct *tty, struct usb_serial_port *port,
				struct file *filp)
{
	dbg("%s - port %d", __func__, port->number);

	/* shutdown our bulk read */
	usb_kill_urb(port->read_urb);
	/* Uncomment the following line if you want to see some statistics in your syslog */
	/* dev_info (&port->dev, "Bytes In = %d  Bytes Out = %d\n", bytes_in, bytes_out); */
}


static int empeg_write(struct tty_struct *tty, struct usb_serial_port *port,
					const unsigned char *buf, int count)
{
	struct usb_serial *serial = port->serial;
	struct urb *urb;
	const unsigned char *current_position = buf;
	unsigned long flags;
	int status;
	int i;
	int bytes_sent = 0;
	int transfer_size;

	dbg("%s - port %d", __func__, port->number);

	while (count > 0) {
		/* try to find a free urb in our list of them */
		urb = NULL;

		spin_lock_irqsave(&write_urb_pool_lock, flags);

		for (i = 0; i < NUM_URBS; ++i) {
			if (write_urb_pool[i]->status != -EINPROGRESS) {
				urb = write_urb_pool[i];
				break;
			}
		}

		spin_unlock_irqrestore(&write_urb_pool_lock, flags);

		if (urb == NULL) {
			dbg("%s - no more free urbs", __func__);
			goto exit;
		}

		if (urb->transfer_buffer == NULL) {
			urb->transfer_buffer = kmalloc(URB_TRANSFER_BUFFER_SIZE, GFP_ATOMIC);
			if (urb->transfer_buffer == NULL) {
				dev_err(&port->dev,
					"%s no more kernel memory...\n",
								__func__);
				goto exit;
			}
		}

		transfer_size = min(count, URB_TRANSFER_BUFFER_SIZE);

		memcpy(urb->transfer_buffer, current_position, transfer_size);

		usb_serial_debug_data(debug, &port->dev, __func__, transfer_size, urb->transfer_buffer);

		/* build up our urb */
		usb_fill_bulk_urb(
			urb,
			serial->dev,
			usb_sndbulkpipe(serial->dev,
					port->bulk_out_endpointAddress),
			urb->transfer_buffer,
			transfer_size,
			empeg_write_bulk_callback,
			port);

		/* send it down the pipe */
		status = usb_submit_urb(urb, GFP_ATOMIC);
		if (status) {
			dev_err(&port->dev, "%s - usb_submit_urb(write bulk) failed with status = %d\n", __func__, status);
			bytes_sent = status;
			break;
		}

		current_position += transfer_size;
		bytes_sent += transfer_size;
		count -= transfer_size;
		bytes_out += transfer_size;

	}
exit:
	return bytes_sent;
}


static int empeg_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned long flags;
	int i;
	int room = 0;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&write_urb_pool_lock, flags);
	/* tally up the number of bytes available */
	for (i = 0; i < NUM_URBS; ++i) {
		if (write_urb_pool[i]->status != -EINPROGRESS)
			room += URB_TRANSFER_BUFFER_SIZE;
	}
	spin_unlock_irqrestore(&write_urb_pool_lock, flags);
	dbg("%s - returns %d", __func__, room);
	return room;

}


static int empeg_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned long flags;
	int i;
	int chars = 0;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&write_urb_pool_lock, flags);

	/* tally up the number of bytes waiting */
	for (i = 0; i < NUM_URBS; ++i) {
		if (write_urb_pool[i]->status == -EINPROGRESS)
			chars += URB_TRANSFER_BUFFER_SIZE;
	}

	spin_unlock_irqrestore(&write_urb_pool_lock, flags);
	dbg("%s - returns %d", __func__, chars);
	return chars;
}


static void empeg_write_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int status = urb->status;

	dbg("%s - port %d", __func__, port->number);

	if (status) {
		dbg("%s - nonzero write bulk status received: %d",
		    __func__, status);
		return;
	}

	usb_serial_port_softint(port);
}


static void empeg_read_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;
	int result;
	int status = urb->status;

	dbg("%s - port %d", __func__, port->number);

	if (status) {
		dbg("%s - nonzero read bulk status received: %d",
		    __func__, status);
		return;
	}

	usb_serial_debug_data(debug, &port->dev, __func__,
						urb->actual_length, data);
	tty = tty_port_tty_get(&port->port);

	if (urb->actual_length) {
		tty_buffer_request_room(tty, urb->actual_length);
		tty_insert_flip_string(tty, data, urb->actual_length);
		tty_flip_buffer_push(tty);
		bytes_in += urb->actual_length;
	}
	tty_kref_put(tty);

	/* Continue trying to always read  */
	usb_fill_bulk_urb(
		port->read_urb,
		port->serial->dev,
		usb_rcvbulkpipe(port->serial->dev,
			port->bulk_in_endpointAddress),
		port->read_urb->transfer_buffer,
		port->read_urb->transfer_buffer_length,
		empeg_read_bulk_callback,
		port);

	result = usb_submit_urb(port->read_urb, GFP_ATOMIC);

	if (result)
		dev_err(&urb->dev->dev,
			"%s - failed resubmitting read urb, error %d\n",
							__func__, result);

	return;

}


static void empeg_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	dbg("%s - port %d", __func__, port->number);
	usb_kill_urb(port->read_urb);
}


static void empeg_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int result;
	dbg("%s - port %d", __func__, port->number);

	port->read_urb->dev = port->serial->dev;
	result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
	if (result)
		dev_err(&port->dev,
			"%s - failed submitting read urb, error %d\n",
							__func__, result);
}


static int  empeg_startup(struct usb_serial *serial)
{
	int r;

	dbg("%s", __func__);

	if (serial->dev->actconfig->desc.bConfigurationValue != 1) {
		dev_err(&serial->dev->dev, "active config #%d != 1 ??\n",
			serial->dev->actconfig->desc.bConfigurationValue);
		return -ENODEV;
	}
	dbg("%s - reset config", __func__);
	r = usb_reset_configuration(serial->dev);

	/* continue on with initialization */
	return r;

}


static void empeg_shutdown(struct usb_serial *serial)
{
	dbg("%s", __func__);
}


static void empeg_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct ktermios *termios = tty->termios;
	dbg("%s - port %d", __func__, port->number);

	/*
	 * The empeg-car player wants these particular tty settings.
	 * You could, for example, change the baud rate, however the
	 * player only supports 115200 (currently), so there is really
	 * no point in support for changes to the tty settings.
	 * (at least for now)
	 *
	 * The default requirements for this device are:
	 */
	termios->c_iflag
		&= ~(IGNBRK	/* disable ignore break */
		| BRKINT	/* disable break causes interrupt */
		| PARMRK	/* disable mark parity errors */
		| ISTRIP	/* disable clear high bit of input characters */
		| INLCR		/* disable translate NL to CR */
		| IGNCR		/* disable ignore CR */
		| ICRNL		/* disable translate CR to NL */
		| IXON);	/* disable enable XON/XOFF flow control */

	termios->c_oflag
		&= ~OPOST;	/* disable postprocess output characters */

	termios->c_lflag
		&= ~(ECHO	/* disable echo input characters */
		| ECHONL	/* disable echo new line */
		| ICANON	/* disable erase, kill, werase, and rprnt special characters */
		| ISIG		/* disable interrupt, quit, and suspend special characters */
		| IEXTEN);	/* disable non-POSIX special characters */

	termios->c_cflag
		&= ~(CSIZE	/* no size */
		| PARENB	/* disable parity bit */
		| CBAUD);	/* clear current baud rate */

	termios->c_cflag
		|= CS8;		/* character size 8 bits */

	tty_encode_baud_rate(tty, 115200, 115200);
}


static int __init empeg_init(void)
{
	struct urb *urb;
	int i, retval;

	/* create our write urb pool and transfer buffers */
	spin_lock_init(&write_urb_pool_lock);
	for (i = 0; i < NUM_URBS; ++i) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		write_urb_pool[i] = urb;
		if (urb == NULL) {
			printk(KERN_ERR "empeg: No more urbs???\n");
			continue;
		}

		urb->transfer_buffer = kmalloc(URB_TRANSFER_BUFFER_SIZE,
								GFP_KERNEL);
		if (!urb->transfer_buffer) {
			printk(KERN_ERR "empeg: %s - out of memory for urb "
			       "buffers.", __func__);
			continue;
		}
	}

	retval = usb_serial_register(&empeg_device);
	if (retval)
		goto failed_usb_serial_register;
	retval = usb_register(&empeg_driver);
	if (retval)
		goto failed_usb_register;

	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
	       DRIVER_DESC "\n");

	return 0;
failed_usb_register:
	usb_serial_deregister(&empeg_device);
failed_usb_serial_register:
	for (i = 0; i < NUM_URBS; ++i) {
		if (write_urb_pool[i]) {
			kfree(write_urb_pool[i]->transfer_buffer);
			usb_free_urb(write_urb_pool[i]);
		}
	}
	return retval;
}


static void __exit empeg_exit(void)
{
	int i;
	unsigned long flags;

	usb_deregister(&empeg_driver);
	usb_serial_deregister(&empeg_device);

	spin_lock_irqsave(&write_urb_pool_lock, flags);

	for (i = 0; i < NUM_URBS; ++i) {
		if (write_urb_pool[i]) {
			/* FIXME - uncomment the following usb_kill_urb call
			 * when the host controllers get fixed to set urb->dev
			 * = NULL after the urb is finished.  Otherwise this
			 * call oopses. */
			/* usb_kill_urb(write_urb_pool[i]); */
			kfree(write_urb_pool[i]->transfer_buffer);
			usb_free_urb(write_urb_pool[i]);
		}
	}
	spin_unlock_irqrestore(&write_urb_pool_lock, flags);
}


module_init(empeg_init);
module_exit(empeg_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
