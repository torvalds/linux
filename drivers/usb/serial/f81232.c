/*
 * Fintek F81232 USB to serial adaptor driver
 *
 * Copyright (C) 2012 Greg Kroah-Hartman (gregkh@linuxfoundation.org)
 * Copyright (C) 2012 Linux Foundation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x1934, 0x0706) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

#define CONTROL_DTR			0x01
#define CONTROL_RTS			0x02

#define UART_STATE			0x08
#define UART_STATE_TRANSIENT_MASK	0x74
#define UART_DCD			0x01
#define UART_DSR			0x02
#define UART_BREAK_ERROR		0x04
#define UART_RING			0x08
#define UART_FRAME_ERROR		0x10
#define UART_PARITY_ERROR		0x20
#define UART_OVERRUN_ERROR		0x40
#define UART_CTS			0x80

struct f81232_private {
	spinlock_t lock;
	wait_queue_head_t delta_msr_wait;
	u8 line_control;
	u8 line_status;
};

static void f81232_update_line_status(struct usb_serial_port *port,
				      unsigned char *data,
				      unsigned int actual_length)
{
}

static void f81232_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port =  urb->context;
	unsigned char *data = urb->transfer_buffer;
	unsigned int actual_length = urb->actual_length;
	int status = urb->status;
	int retval;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&port->dev, "%s - urb shutting down with status: %d\n",
			__func__, status);
		return;
	default:
		dev_dbg(&port->dev, "%s - nonzero urb status received: %d\n",
			__func__, status);
		goto exit;
	}

	usb_serial_debug_data(&port->dev, __func__,
			      urb->actual_length, urb->transfer_buffer);

	f81232_update_line_status(port, data, actual_length);

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&urb->dev->dev,
			"%s - usb_submit_urb failed with result %d\n",
			__func__, retval);
}

static void f81232_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct f81232_private *priv = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	char tty_flag = TTY_NORMAL;
	unsigned long flags;
	u8 line_status;
	int i;

	/* update line status */
	spin_lock_irqsave(&priv->lock, flags);
	line_status = priv->line_status;
	priv->line_status &= ~UART_STATE_TRANSIENT_MASK;
	spin_unlock_irqrestore(&priv->lock, flags);
	wake_up_interruptible(&priv->delta_msr_wait);

	if (!urb->actual_length)
		return;

	/* break takes precedence over parity, */
	/* which takes precedence over framing errors */
	if (line_status & UART_BREAK_ERROR)
		tty_flag = TTY_BREAK;
	else if (line_status & UART_PARITY_ERROR)
		tty_flag = TTY_PARITY;
	else if (line_status & UART_FRAME_ERROR)
		tty_flag = TTY_FRAME;
	dev_dbg(&port->dev, "%s - tty_flag = %d\n", __func__, tty_flag);

	/* overrun is special, not associated with a char */
	if (line_status & UART_OVERRUN_ERROR)
		tty_insert_flip_char(&port->port, 0, TTY_OVERRUN);

	if (port->port.console && port->sysrq) {
		for (i = 0; i < urb->actual_length; ++i)
			if (!usb_serial_handle_sysrq_char(port, data[i]))
				tty_insert_flip_char(&port->port, data[i],
						tty_flag);
	} else {
		tty_insert_flip_string_fixed_flag(&port->port, data, tty_flag,
							urb->actual_length);
	}

	tty_flip_buffer_push(&port->port);
}

static int set_control_lines(struct usb_device *dev, u8 value)
{
	/* FIXME - Stubbed out for now */
	return 0;
}

static void f81232_break_ctl(struct tty_struct *tty, int break_state)
{
	/* FIXME - Stubbed out for now */

	/*
	 * break_state = -1 to turn on break, and 0 to turn off break
	 * see drivers/char/tty_io.c to see it used.
	 * last_set_data_urb_value NEVER has the break bit set in it.
	 */
}

static void f81232_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	/* FIXME - Stubbed out for now */

	/* Don't change anything if nothing has changed */
	if (!tty_termios_hw_change(&tty->termios, old_termios))
		return;

	/* Do the real work here... */
	tty_termios_copy_hw(&tty->termios, old_termios);
}

static int f81232_tiocmget(struct tty_struct *tty)
{
	/* FIXME - Stubbed out for now */
	return 0;
}

static int f81232_tiocmset(struct tty_struct *tty,
			unsigned int set, unsigned int clear)
{
	/* FIXME - Stubbed out for now */
	return 0;
}

static int f81232_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct ktermios tmp_termios;
	int result;

	/* Setup termios */
	if (tty)
		f81232_set_termios(tty, port, &tmp_termios);

	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (result) {
		dev_err(&port->dev, "%s - failed submitting interrupt urb,"
			" error %d\n", __func__, result);
		return result;
	}

	result = usb_serial_generic_open(tty, port);
	if (result) {
		usb_kill_urb(port->interrupt_in_urb);
		return result;
	}

	port->port.drain_delay = 256;
	return 0;
}

static void f81232_close(struct usb_serial_port *port)
{
	usb_serial_generic_close(port);
	usb_kill_urb(port->interrupt_in_urb);
}

static void f81232_dtr_rts(struct usb_serial_port *port, int on)
{
	struct f81232_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	u8 control;

	spin_lock_irqsave(&priv->lock, flags);
	/* Change DTR and RTS */
	if (on)
		priv->line_control |= (CONTROL_DTR | CONTROL_RTS);
	else
		priv->line_control &= ~(CONTROL_DTR | CONTROL_RTS);
	control = priv->line_control;
	spin_unlock_irqrestore(&priv->lock, flags);
	set_control_lines(port->serial->dev, control);
}

static int f81232_carrier_raised(struct usb_serial_port *port)
{
	struct f81232_private *priv = usb_get_serial_port_data(port);
	if (priv->line_status & UART_DCD)
		return 1;
	return 0;
}

static int wait_modem_info(struct usb_serial_port *port, unsigned int arg)
{
	struct f81232_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	unsigned int prevstatus;
	unsigned int status;
	unsigned int changed;

	spin_lock_irqsave(&priv->lock, flags);
	prevstatus = priv->line_status;
	spin_unlock_irqrestore(&priv->lock, flags);

	while (1) {
		interruptible_sleep_on(&priv->delta_msr_wait);
		/* see if a signal did it */
		if (signal_pending(current))
			return -ERESTARTSYS;

		spin_lock_irqsave(&priv->lock, flags);
		status = priv->line_status;
		spin_unlock_irqrestore(&priv->lock, flags);

		changed = prevstatus ^ status;

		if (((arg & TIOCM_RNG) && (changed & UART_RING)) ||
		    ((arg & TIOCM_DSR) && (changed & UART_DSR)) ||
		    ((arg & TIOCM_CD)  && (changed & UART_DCD)) ||
		    ((arg & TIOCM_CTS) && (changed & UART_CTS))) {
			return 0;
		}
		prevstatus = status;
	}
	/* NOTREACHED */
	return 0;
}

static int f81232_ioctl(struct tty_struct *tty,
			unsigned int cmd, unsigned long arg)
{
	struct serial_struct ser;
	struct usb_serial_port *port = tty->driver_data;

	dev_dbg(&port->dev, "%s (%d) cmd = 0x%04x\n", __func__,
		port->number, cmd);

	switch (cmd) {
	case TIOCGSERIAL:
		memset(&ser, 0, sizeof ser);
		ser.type = PORT_16654;
		ser.line = port->serial->minor;
		ser.port = port->number;
		ser.baud_base = 460800;

		if (copy_to_user((void __user *)arg, &ser, sizeof ser))
			return -EFAULT;

		return 0;

	case TIOCMIWAIT:
		dev_dbg(&port->dev, "%s (%d) TIOCMIWAIT\n", __func__,
			port->number);
		return wait_modem_info(port, arg);
	default:
		dev_dbg(&port->dev, "%s not supported = 0x%04x\n",
			__func__, cmd);
		break;
	}
	return -ENOIOCTLCMD;
}

static int f81232_port_probe(struct usb_serial_port *port)
{
	struct f81232_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	init_waitqueue_head(&priv->delta_msr_wait);

	usb_set_serial_port_data(port, priv);

	return 0;
}

static int f81232_port_remove(struct usb_serial_port *port)
{
	struct f81232_private *priv;

	priv = usb_get_serial_port_data(port);
	kfree(priv);

	return 0;
}

static struct usb_serial_driver f81232_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"f81232",
	},
	.id_table =		id_table,
	.num_ports =		1,
	.bulk_in_size =		256,
	.bulk_out_size =	256,
	.open =			f81232_open,
	.close =		f81232_close,
	.dtr_rts = 		f81232_dtr_rts,
	.carrier_raised =	f81232_carrier_raised,
	.ioctl =		f81232_ioctl,
	.break_ctl =		f81232_break_ctl,
	.set_termios =		f81232_set_termios,
	.tiocmget =		f81232_tiocmget,
	.tiocmset =		f81232_tiocmset,
	.process_read_urb =	f81232_process_read_urb,
	.read_int_callback =	f81232_read_int_callback,
	.port_probe =		f81232_port_probe,
	.port_remove =		f81232_port_remove,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&f81232_device,
	NULL,
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_DESCRIPTION("Fintek F81232 USB to serial adaptor driver");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org");
MODULE_LICENSE("GPL v2");
