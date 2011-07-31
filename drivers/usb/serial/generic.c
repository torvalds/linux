/*
 * USB Serial Converter Generic functions
 *
 * Copyright (C) 2010 Johan Hovold (jhovold@gmail.com)
 * Copyright (C) 1999 - 2002 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/uaccess.h>
#include <linux/kfifo.h>
#include <linux/serial.h>

static int debug;

#ifdef CONFIG_USB_SERIAL_GENERIC

static int generic_probe(struct usb_interface *interface,
			 const struct usb_device_id *id);

static __u16 vendor  = 0x05f9;
static __u16 product = 0xffff;

module_param(vendor, ushort, 0);
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

module_param(product, ushort, 0);
MODULE_PARM_DESC(product, "User specified USB idProduct");

static struct usb_device_id generic_device_ids[2]; /* Initially all zeroes. */

/* we want to look at all devices, as the vendor/product id can change
 * depending on the command line argument */
static const struct usb_device_id generic_serial_ids[] = {
	{.driver_info = 42},
	{}
};

static struct usb_driver generic_driver = {
	.name =		"usbserial_generic",
	.probe =	generic_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	generic_serial_ids,
	.no_dynamic_id =	1,
};

/* All of the device info needed for the Generic Serial Converter */
struct usb_serial_driver usb_serial_generic_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"generic",
	},
	.id_table =		generic_device_ids,
	.usb_driver = 		&generic_driver,
	.num_ports =		1,
	.disconnect =		usb_serial_generic_disconnect,
	.release =		usb_serial_generic_release,
	.throttle =		usb_serial_generic_throttle,
	.unthrottle =		usb_serial_generic_unthrottle,
	.resume =		usb_serial_generic_resume,
};

static int generic_probe(struct usb_interface *interface,
			       const struct usb_device_id *id)
{
	const struct usb_device_id *id_pattern;

	id_pattern = usb_match_id(interface, generic_device_ids);
	if (id_pattern != NULL)
		return usb_serial_probe(interface, id);
	return -ENODEV;
}
#endif

int usb_serial_generic_register(int _debug)
{
	int retval = 0;

	debug = _debug;
#ifdef CONFIG_USB_SERIAL_GENERIC
	generic_device_ids[0].idVendor = vendor;
	generic_device_ids[0].idProduct = product;
	generic_device_ids[0].match_flags =
		USB_DEVICE_ID_MATCH_VENDOR | USB_DEVICE_ID_MATCH_PRODUCT;

	/* register our generic driver with ourselves */
	retval = usb_serial_register(&usb_serial_generic_device);
	if (retval)
		goto exit;
	retval = usb_register(&generic_driver);
	if (retval)
		usb_serial_deregister(&usb_serial_generic_device);
exit:
#endif
	return retval;
}

void usb_serial_generic_deregister(void)
{
#ifdef CONFIG_USB_SERIAL_GENERIC
	/* remove our generic driver */
	usb_deregister(&generic_driver);
	usb_serial_deregister(&usb_serial_generic_device);
#endif
}

int usb_serial_generic_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	int result = 0;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	/* clear the throttle flags */
	spin_lock_irqsave(&port->lock, flags);
	port->throttled = 0;
	port->throttle_req = 0;
	spin_unlock_irqrestore(&port->lock, flags);

	/* if we have a bulk endpoint, start reading from it */
	if (port->bulk_in_size)
		result = usb_serial_generic_submit_read_urb(port, GFP_KERNEL);

	return result;
}
EXPORT_SYMBOL_GPL(usb_serial_generic_open);

static void generic_cleanup(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	unsigned long flags;
	int i;

	dbg("%s - port %d", __func__, port->number);

	if (serial->dev) {
		/* shutdown any bulk transfers that might be going on */
		if (port->bulk_out_size) {
			usb_kill_urb(port->write_urb);
			for (i = 0; i < ARRAY_SIZE(port->write_urbs); ++i)
				usb_kill_urb(port->write_urbs[i]);

			spin_lock_irqsave(&port->lock, flags);
			kfifo_reset_out(&port->write_fifo);
			spin_unlock_irqrestore(&port->lock, flags);
		}
		if (port->bulk_in_size)
			usb_kill_urb(port->read_urb);
	}
}

void usb_serial_generic_close(struct usb_serial_port *port)
{
	dbg("%s - port %d", __func__, port->number);
	generic_cleanup(port);
}
EXPORT_SYMBOL_GPL(usb_serial_generic_close);

int usb_serial_generic_prepare_write_buffer(struct usb_serial_port *port,
						void *dest, size_t size)
{
	return kfifo_out_locked(&port->write_fifo, dest, size, &port->lock);
}

/**
 * usb_serial_generic_write_start - kick off an URB write
 * @port:	Pointer to the &struct usb_serial_port data
 *
 * Returns zero on success, or a negative errno value
 */
static int usb_serial_generic_write_start(struct usb_serial_port *port)
{
	struct urb *urb;
	int count, result;
	unsigned long flags;
	int i;

	if (test_and_set_bit_lock(USB_SERIAL_WRITE_BUSY, &port->flags))
		return 0;
retry:
	spin_lock_irqsave(&port->lock, flags);
	if (!port->write_urbs_free || !kfifo_len(&port->write_fifo)) {
		clear_bit_unlock(USB_SERIAL_WRITE_BUSY, &port->flags);
		spin_unlock_irqrestore(&port->lock, flags);
		return 0;
	}
	i = (int)find_first_bit(&port->write_urbs_free,
						ARRAY_SIZE(port->write_urbs));
	spin_unlock_irqrestore(&port->lock, flags);

	urb = port->write_urbs[i];
	count = port->serial->type->prepare_write_buffer(port,
						urb->transfer_buffer,
						port->bulk_out_size);
	urb->transfer_buffer_length = count;
	usb_serial_debug_data(debug, &port->dev, __func__, count,
						urb->transfer_buffer);
	spin_lock_irqsave(&port->lock, flags);
	port->tx_bytes += count;
	spin_unlock_irqrestore(&port->lock, flags);

	clear_bit(i, &port->write_urbs_free);
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result) {
		dev_err(&port->dev, "%s - error submitting urb: %d\n",
						__func__, result);
		set_bit(i, &port->write_urbs_free);
		spin_lock_irqsave(&port->lock, flags);
		port->tx_bytes -= count;
		spin_unlock_irqrestore(&port->lock, flags);

		clear_bit_unlock(USB_SERIAL_WRITE_BUSY, &port->flags);
		return result;
	}

	/* Try sending off another urb, unless in irq context (in which case
	 * there will be no free urb). */
	if (!in_irq())
		goto retry;

	clear_bit_unlock(USB_SERIAL_WRITE_BUSY, &port->flags);

	return 0;
}

/**
 * usb_serial_generic_write - generic write function for serial USB devices
 * @tty:	Pointer to &struct tty_struct for the device
 * @port:	Pointer to the &usb_serial_port structure for the device
 * @buf:	Pointer to the data to write
 * @count:	Number of bytes to write
 *
 * Returns the number of characters actually written, which may be anything
 * from zero to @count. If an error occurs, it returns the negative errno
 * value.
 */
int usb_serial_generic_write(struct tty_struct *tty,
	struct usb_serial_port *port, const unsigned char *buf, int count)
{
	int result;

	dbg("%s - port %d", __func__, port->number);

	/* only do something if we have a bulk out endpoint */
	if (!port->bulk_out_size)
		return -ENODEV;

	if (!count)
		return 0;

	count = kfifo_in_locked(&port->write_fifo, buf, count, &port->lock);
	result = usb_serial_generic_write_start(port);
	if (result)
		return result;

	return count;
}
EXPORT_SYMBOL_GPL(usb_serial_generic_write);

int usb_serial_generic_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned long flags;
	int room;

	dbg("%s - port %d", __func__, port->number);

	if (!port->bulk_out_size)
		return 0;

	spin_lock_irqsave(&port->lock, flags);
	room = kfifo_avail(&port->write_fifo);
	spin_unlock_irqrestore(&port->lock, flags);

	dbg("%s - returns %d", __func__, room);
	return room;
}

int usb_serial_generic_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned long flags;
	int chars;

	dbg("%s - port %d", __func__, port->number);

	if (!port->bulk_out_size)
		return 0;

	spin_lock_irqsave(&port->lock, flags);
	chars = kfifo_len(&port->write_fifo) + port->tx_bytes;
	spin_unlock_irqrestore(&port->lock, flags);

	dbg("%s - returns %d", __func__, chars);
	return chars;
}

int usb_serial_generic_submit_read_urb(struct usb_serial_port *port,
					gfp_t mem_flags)
{
	int result;

	result = usb_submit_urb(port->read_urb, mem_flags);
	if (result && result != -EPERM) {
		dev_err(&port->dev, "%s - error submitting urb: %d\n",
							__func__, result);
	}
	return result;
}
EXPORT_SYMBOL_GPL(usb_serial_generic_submit_read_urb);

void usb_serial_generic_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct tty_struct *tty;
	char *ch = (char *)urb->transfer_buffer;
	int i;

	if (!urb->actual_length)
		return;

	tty = tty_port_tty_get(&port->port);
	if (!tty)
		return;

	/* The per character mucking around with sysrq path it too slow for
	   stuff like 3G modems, so shortcircuit it in the 99.9999999% of cases
	   where the USB serial is not a console anyway */
	if (!port->port.console || !port->sysrq)
		tty_insert_flip_string(tty, ch, urb->actual_length);
	else {
		for (i = 0; i < urb->actual_length; i++, ch++) {
			if (!usb_serial_handle_sysrq_char(port, *ch))
				tty_insert_flip_char(tty, *ch, TTY_NORMAL);
		}
	}
	tty_flip_buffer_push(tty);
	tty_kref_put(tty);
}
EXPORT_SYMBOL_GPL(usb_serial_generic_process_read_urb);

void usb_serial_generic_read_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	int status = urb->status;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	if (unlikely(status != 0)) {
		dbg("%s - nonzero read bulk status received: %d",
		    __func__, status);
		return;
	}

	usb_serial_debug_data(debug, &port->dev, __func__,
						urb->actual_length, data);
	port->serial->type->process_read_urb(urb);

	/* Throttle the device if requested by tty */
	spin_lock_irqsave(&port->lock, flags);
	port->throttled = port->throttle_req;
	if (!port->throttled) {
		spin_unlock_irqrestore(&port->lock, flags);
		usb_serial_generic_submit_read_urb(port, GFP_ATOMIC);
	} else
		spin_unlock_irqrestore(&port->lock, flags);
}
EXPORT_SYMBOL_GPL(usb_serial_generic_read_bulk_callback);

void usb_serial_generic_write_bulk_callback(struct urb *urb)
{
	unsigned long flags;
	struct usb_serial_port *port = urb->context;
	int status = urb->status;
	int i;

	dbg("%s - port %d", __func__, port->number);

	for (i = 0; i < ARRAY_SIZE(port->write_urbs); ++i)
		if (port->write_urbs[i] == urb)
			break;

	spin_lock_irqsave(&port->lock, flags);
	port->tx_bytes -= urb->transfer_buffer_length;
	set_bit(i, &port->write_urbs_free);
	spin_unlock_irqrestore(&port->lock, flags);

	if (status) {
		dbg("%s - non-zero urb status: %d", __func__, status);

		spin_lock_irqsave(&port->lock, flags);
		kfifo_reset_out(&port->write_fifo);
		spin_unlock_irqrestore(&port->lock, flags);
	} else {
		usb_serial_generic_write_start(port);
	}

	usb_serial_port_softint(port);
}
EXPORT_SYMBOL_GPL(usb_serial_generic_write_bulk_callback);

void usb_serial_generic_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	/* Set the throttle request flag. It will be picked up
	 * by usb_serial_generic_read_bulk_callback(). */
	spin_lock_irqsave(&port->lock, flags);
	port->throttle_req = 1;
	spin_unlock_irqrestore(&port->lock, flags);
}
EXPORT_SYMBOL_GPL(usb_serial_generic_throttle);

void usb_serial_generic_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int was_throttled;

	dbg("%s - port %d", __func__, port->number);

	/* Clear the throttle flags */
	spin_lock_irq(&port->lock);
	was_throttled = port->throttled;
	port->throttled = port->throttle_req = 0;
	spin_unlock_irq(&port->lock);

	if (was_throttled)
		usb_serial_generic_submit_read_urb(port, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(usb_serial_generic_unthrottle);

#ifdef CONFIG_MAGIC_SYSRQ
int usb_serial_handle_sysrq_char(struct usb_serial_port *port, unsigned int ch)
{
	if (port->sysrq && port->port.console) {
		if (ch && time_before(jiffies, port->sysrq)) {
			handle_sysrq(ch);
			port->sysrq = 0;
			return 1;
		}
		port->sysrq = 0;
	}
	return 0;
}
#else
int usb_serial_handle_sysrq_char(struct usb_serial_port *port, unsigned int ch)
{
	return 0;
}
#endif
EXPORT_SYMBOL_GPL(usb_serial_handle_sysrq_char);

int usb_serial_handle_break(struct usb_serial_port *port)
{
	if (!port->sysrq) {
		port->sysrq = jiffies + HZ*5;
		return 1;
	}
	port->sysrq = 0;
	return 0;
}
EXPORT_SYMBOL_GPL(usb_serial_handle_break);

int usb_serial_generic_resume(struct usb_serial *serial)
{
	struct usb_serial_port *port;
	int i, c = 0, r;

	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		if (!test_bit(ASYNCB_INITIALIZED, &port->port.flags))
			continue;

		if (port->read_urb) {
			r = usb_submit_urb(port->read_urb, GFP_NOIO);
			if (r < 0)
				c++;
		}

		if (port->bulk_out_size) {
			r = usb_serial_generic_write_start(port);
			if (r < 0)
				c++;
		}
	}

	return c ? -EIO : 0;
}
EXPORT_SYMBOL_GPL(usb_serial_generic_resume);

void usb_serial_generic_disconnect(struct usb_serial *serial)
{
	int i;

	dbg("%s", __func__);

	/* stop reads and writes on all ports */
	for (i = 0; i < serial->num_ports; ++i)
		generic_cleanup(serial->port[i]);
}
EXPORT_SYMBOL_GPL(usb_serial_generic_disconnect);

void usb_serial_generic_release(struct usb_serial *serial)
{
	dbg("%s", __func__);
}
