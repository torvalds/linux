/*
 * USB Serial Converter Generic functions
 *
 * Copyright (C) 2010 - 2013 Johan Hovold (jhovold@gmail.com)
 * Copyright (C) 1999 - 2002 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
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

#ifdef CONFIG_USB_SERIAL_GENERIC

static __u16 vendor  = 0x05f9;
static __u16 product = 0xffff;

module_param(vendor, ushort, 0);
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

module_param(product, ushort, 0);
MODULE_PARM_DESC(product, "User specified USB idProduct");

static struct usb_device_id generic_device_ids[2]; /* Initially all zeroes. */

struct usb_serial_driver usb_serial_generic_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"generic",
	},
	.id_table =		generic_device_ids,
	.num_ports =		1,
	.throttle =		usb_serial_generic_throttle,
	.unthrottle =		usb_serial_generic_unthrottle,
	.resume =		usb_serial_generic_resume,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&usb_serial_generic_device, NULL
};

#endif

int usb_serial_generic_register(void)
{
	int retval = 0;

#ifdef CONFIG_USB_SERIAL_GENERIC
	generic_device_ids[0].idVendor = vendor;
	generic_device_ids[0].idProduct = product;
	generic_device_ids[0].match_flags =
		USB_DEVICE_ID_MATCH_VENDOR | USB_DEVICE_ID_MATCH_PRODUCT;

	retval = usb_serial_register_drivers(serial_drivers,
			"usbserial_generic", generic_device_ids);
#endif
	return retval;
}

void usb_serial_generic_deregister(void)
{
#ifdef CONFIG_USB_SERIAL_GENERIC
	usb_serial_deregister_drivers(serial_drivers);
#endif
}

int usb_serial_generic_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	int result = 0;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	port->throttled = 0;
	port->throttle_req = 0;
	spin_unlock_irqrestore(&port->lock, flags);

	if (port->bulk_in_size)
		result = usb_serial_generic_submit_read_urbs(port, GFP_KERNEL);

	return result;
}
EXPORT_SYMBOL_GPL(usb_serial_generic_open);

void usb_serial_generic_close(struct usb_serial_port *port)
{
	unsigned long flags;
	int i;

	if (port->bulk_out_size) {
		for (i = 0; i < ARRAY_SIZE(port->write_urbs); ++i)
			usb_kill_urb(port->write_urbs[i]);

		spin_lock_irqsave(&port->lock, flags);
		kfifo_reset_out(&port->write_fifo);
		spin_unlock_irqrestore(&port->lock, flags);
	}
	if (port->bulk_in_size) {
		for (i = 0; i < ARRAY_SIZE(port->read_urbs); ++i)
			usb_kill_urb(port->read_urbs[i]);
	}
}
EXPORT_SYMBOL_GPL(usb_serial_generic_close);

int usb_serial_generic_prepare_write_buffer(struct usb_serial_port *port,
						void *dest, size_t size)
{
	return kfifo_out_locked(&port->write_fifo, dest, size, &port->lock);
}

/**
 * usb_serial_generic_write_start - start writing buffered data
 * @port: usb-serial port
 * @mem_flags: flags to use for memory allocations
 *
 * Serialised using USB_SERIAL_WRITE_BUSY flag.
 *
 * Return: Zero on success or if busy, otherwise a negative errno value.
 */
int usb_serial_generic_write_start(struct usb_serial_port *port,
							gfp_t mem_flags)
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
	usb_serial_debug_data(&port->dev, __func__, count, urb->transfer_buffer);
	spin_lock_irqsave(&port->lock, flags);
	port->tx_bytes += count;
	spin_unlock_irqrestore(&port->lock, flags);

	clear_bit(i, &port->write_urbs_free);
	result = usb_submit_urb(urb, mem_flags);
	if (result) {
		dev_err_console(port, "%s - error submitting urb: %d\n",
						__func__, result);
		set_bit(i, &port->write_urbs_free);
		spin_lock_irqsave(&port->lock, flags);
		port->tx_bytes -= count;
		spin_unlock_irqrestore(&port->lock, flags);

		clear_bit_unlock(USB_SERIAL_WRITE_BUSY, &port->flags);
		return result;
	}

	goto retry;	/* try sending off another urb */
}
EXPORT_SYMBOL_GPL(usb_serial_generic_write_start);

/**
 * usb_serial_generic_write - generic write function
 * @tty: tty for the port
 * @port: usb-serial port
 * @buf: data to write
 * @count: number of bytes to write
 *
 * Return: The number of characters buffered, which may be anything from
 * zero to @count, or a negative errno value.
 */
int usb_serial_generic_write(struct tty_struct *tty,
	struct usb_serial_port *port, const unsigned char *buf, int count)
{
	int result;

	if (!port->bulk_out_size)
		return -ENODEV;

	if (!count)
		return 0;

	count = kfifo_in_locked(&port->write_fifo, buf, count, &port->lock);
	result = usb_serial_generic_write_start(port, GFP_ATOMIC);
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

	if (!port->bulk_out_size)
		return 0;

	spin_lock_irqsave(&port->lock, flags);
	room = kfifo_avail(&port->write_fifo);
	spin_unlock_irqrestore(&port->lock, flags);

	dev_dbg(&port->dev, "%s - returns %d\n", __func__, room);
	return room;
}

int usb_serial_generic_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned long flags;
	int chars;

	if (!port->bulk_out_size)
		return 0;

	spin_lock_irqsave(&port->lock, flags);
	chars = kfifo_len(&port->write_fifo) + port->tx_bytes;
	spin_unlock_irqrestore(&port->lock, flags);

	dev_dbg(&port->dev, "%s - returns %d\n", __func__, chars);
	return chars;
}
EXPORT_SYMBOL_GPL(usb_serial_generic_chars_in_buffer);

void usb_serial_generic_wait_until_sent(struct tty_struct *tty, long timeout)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned int bps;
	unsigned long period;
	unsigned long expire;

	bps = tty_get_baud_rate(tty);
	if (!bps)
		bps = 9600;	/* B0 */
	/*
	 * Use a poll-period of roughly the time it takes to send one
	 * character or at least one jiffy.
	 */
	period = max_t(unsigned long, (10 * HZ / bps), 1);
	if (timeout)
		period = min_t(unsigned long, period, timeout);

	dev_dbg(&port->dev, "%s - timeout = %u ms, period = %u ms\n",
					__func__, jiffies_to_msecs(timeout),
					jiffies_to_msecs(period));
	expire = jiffies + timeout;
	while (!port->serial->type->tx_empty(port)) {
		schedule_timeout_interruptible(period);
		if (signal_pending(current))
			break;
		if (timeout && time_after(jiffies, expire))
			break;
	}
}
EXPORT_SYMBOL_GPL(usb_serial_generic_wait_until_sent);

static int usb_serial_generic_submit_read_urb(struct usb_serial_port *port,
						int index, gfp_t mem_flags)
{
	int res;

	if (!test_and_clear_bit(index, &port->read_urbs_free))
		return 0;

	dev_dbg(&port->dev, "%s - urb %d\n", __func__, index);

	res = usb_submit_urb(port->read_urbs[index], mem_flags);
	if (res) {
		if (res != -EPERM && res != -ENODEV) {
			dev_err(&port->dev,
					"%s - usb_submit_urb failed: %d\n",
					__func__, res);
		}
		set_bit(index, &port->read_urbs_free);
		return res;
	}

	return 0;
}

int usb_serial_generic_submit_read_urbs(struct usb_serial_port *port,
					gfp_t mem_flags)
{
	int res;
	int i;

	for (i = 0; i < ARRAY_SIZE(port->read_urbs); ++i) {
		res = usb_serial_generic_submit_read_urb(port, i, mem_flags);
		if (res)
			goto err;
	}

	return 0;
err:
	for (; i >= 0; --i)
		usb_kill_urb(port->read_urbs[i]);

	return res;
}
EXPORT_SYMBOL_GPL(usb_serial_generic_submit_read_urbs);

void usb_serial_generic_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	char *ch = (char *)urb->transfer_buffer;
	int i;

	if (!urb->actual_length)
		return;
	/*
	 * The per character mucking around with sysrq path it too slow for
	 * stuff like 3G modems, so shortcircuit it in the 99.9999999% of
	 * cases where the USB serial is not a console anyway.
	 */
	if (!port->port.console || !port->sysrq) {
		tty_insert_flip_string(&port->port, ch, urb->actual_length);
	} else {
		for (i = 0; i < urb->actual_length; i++, ch++) {
			if (!usb_serial_handle_sysrq_char(port, *ch))
				tty_insert_flip_char(&port->port, *ch, TTY_NORMAL);
		}
	}
	tty_flip_buffer_push(&port->port);
}
EXPORT_SYMBOL_GPL(usb_serial_generic_process_read_urb);

void usb_serial_generic_read_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	unsigned long flags;
	int i;

	for (i = 0; i < ARRAY_SIZE(port->read_urbs); ++i) {
		if (urb == port->read_urbs[i])
			break;
	}
	set_bit(i, &port->read_urbs_free);

	dev_dbg(&port->dev, "%s - urb %d, len %d\n", __func__, i,
							urb->actual_length);
	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		dev_dbg(&port->dev, "%s - urb stopped: %d\n",
							__func__, urb->status);
		return;
	case -EPIPE:
		dev_err(&port->dev, "%s - urb stopped: %d\n",
							__func__, urb->status);
		return;
	default:
		dev_dbg(&port->dev, "%s - nonzero urb status: %d\n",
							__func__, urb->status);
		goto resubmit;
	}

	usb_serial_debug_data(&port->dev, __func__, urb->actual_length, data);
	port->serial->type->process_read_urb(urb);

resubmit:
	/* Throttle the device if requested by tty */
	spin_lock_irqsave(&port->lock, flags);
	port->throttled = port->throttle_req;
	if (!port->throttled) {
		spin_unlock_irqrestore(&port->lock, flags);
		usb_serial_generic_submit_read_urb(port, i, GFP_ATOMIC);
	} else {
		spin_unlock_irqrestore(&port->lock, flags);
	}
}
EXPORT_SYMBOL_GPL(usb_serial_generic_read_bulk_callback);

void usb_serial_generic_write_bulk_callback(struct urb *urb)
{
	unsigned long flags;
	struct usb_serial_port *port = urb->context;
	int i;

	for (i = 0; i < ARRAY_SIZE(port->write_urbs); ++i) {
		if (port->write_urbs[i] == urb)
			break;
	}
	spin_lock_irqsave(&port->lock, flags);
	port->tx_bytes -= urb->transfer_buffer_length;
	set_bit(i, &port->write_urbs_free);
	spin_unlock_irqrestore(&port->lock, flags);

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		dev_dbg(&port->dev, "%s - urb stopped: %d\n",
							__func__, urb->status);
		return;
	case -EPIPE:
		dev_err_console(port, "%s - urb stopped: %d\n",
							__func__, urb->status);
		return;
	default:
		dev_err_console(port, "%s - nonzero urb status: %d\n",
							__func__, urb->status);
		goto resubmit;
	}

resubmit:
	usb_serial_generic_write_start(port, GFP_ATOMIC);
	usb_serial_port_softint(port);
}
EXPORT_SYMBOL_GPL(usb_serial_generic_write_bulk_callback);

void usb_serial_generic_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	port->throttle_req = 1;
	spin_unlock_irqrestore(&port->lock, flags);
}
EXPORT_SYMBOL_GPL(usb_serial_generic_throttle);

void usb_serial_generic_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int was_throttled;

	spin_lock_irq(&port->lock);
	was_throttled = port->throttled;
	port->throttled = port->throttle_req = 0;
	spin_unlock_irq(&port->lock);

	if (was_throttled)
		usb_serial_generic_submit_read_urbs(port, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(usb_serial_generic_unthrottle);

static bool usb_serial_generic_msr_changed(struct tty_struct *tty,
				unsigned long arg, struct async_icount *cprev)
{
	struct usb_serial_port *port = tty->driver_data;
	struct async_icount cnow;
	unsigned long flags;
	bool ret;

	/*
	 * Use tty-port initialised flag to detect all hangups including the
	 * one generated at USB-device disconnect.
	 */
	if (!tty_port_initialized(&port->port))
		return true;

	spin_lock_irqsave(&port->lock, flags);
	cnow = port->icount;				/* atomic copy*/
	spin_unlock_irqrestore(&port->lock, flags);

	ret =	((arg & TIOCM_RNG) && (cnow.rng != cprev->rng)) ||
		((arg & TIOCM_DSR) && (cnow.dsr != cprev->dsr)) ||
		((arg & TIOCM_CD)  && (cnow.dcd != cprev->dcd)) ||
		((arg & TIOCM_CTS) && (cnow.cts != cprev->cts));

	*cprev = cnow;

	return ret;
}

int usb_serial_generic_tiocmiwait(struct tty_struct *tty, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct async_icount cnow;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&port->lock, flags);
	cnow = port->icount;				/* atomic copy */
	spin_unlock_irqrestore(&port->lock, flags);

	ret = wait_event_interruptible(port->port.delta_msr_wait,
			usb_serial_generic_msr_changed(tty, arg, &cnow));
	if (!ret && !tty_port_initialized(&port->port))
		ret = -EIO;

	return ret;
}
EXPORT_SYMBOL_GPL(usb_serial_generic_tiocmiwait);

int usb_serial_generic_get_icount(struct tty_struct *tty,
					struct serial_icounter_struct *icount)
{
	struct usb_serial_port *port = tty->driver_data;
	struct async_icount cnow;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	cnow = port->icount;				/* atomic copy */
	spin_unlock_irqrestore(&port->lock, flags);

	icount->cts = cnow.cts;
	icount->dsr = cnow.dsr;
	icount->rng = cnow.rng;
	icount->dcd = cnow.dcd;
	icount->tx = cnow.tx;
	icount->rx = cnow.rx;
	icount->frame = cnow.frame;
	icount->parity = cnow.parity;
	icount->overrun = cnow.overrun;
	icount->brk = cnow.brk;
	icount->buf_overrun = cnow.buf_overrun;

	return 0;
}
EXPORT_SYMBOL_GPL(usb_serial_generic_get_icount);

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

/**
 * usb_serial_handle_dcd_change - handle a change of carrier detect state
 * @port: usb-serial port
 * @tty: tty for the port
 * @status: new carrier detect status, nonzero if active
 */
void usb_serial_handle_dcd_change(struct usb_serial_port *usb_port,
				struct tty_struct *tty, unsigned int status)
{
	struct tty_port *port = &usb_port->port;

	dev_dbg(&usb_port->dev, "%s - status %d\n", __func__, status);

	if (tty) {
		struct tty_ldisc *ld = tty_ldisc_ref(tty);

		if (ld) {
			if (ld->ops->dcd_change)
				ld->ops->dcd_change(tty, status);
			tty_ldisc_deref(ld);
		}
	}

	if (status)
		wake_up_interruptible(&port->open_wait);
	else if (tty && !C_CLOCAL(tty))
		tty_hangup(tty);
}
EXPORT_SYMBOL_GPL(usb_serial_handle_dcd_change);

int usb_serial_generic_resume(struct usb_serial *serial)
{
	struct usb_serial_port *port;
	int i, c = 0, r;

	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		if (!tty_port_initialized(&port->port))
			continue;

		if (port->bulk_in_size) {
			r = usb_serial_generic_submit_read_urbs(port,
								GFP_NOIO);
			if (r < 0)
				c++;
		}

		if (port->bulk_out_size) {
			r = usb_serial_generic_write_start(port, GFP_NOIO);
			if (r < 0)
				c++;
		}
	}

	return c ? -EIO : 0;
}
EXPORT_SYMBOL_GPL(usb_serial_generic_resume);
