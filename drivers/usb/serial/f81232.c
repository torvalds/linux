// SPDX-License-Identifier: GPL-2.0
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
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial_reg.h>

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x1934, 0x0706) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

/* Maximum baudrate for F81232 */
#define F81232_MAX_BAUDRATE		115200

/* USB Control EP parameter */
#define F81232_REGISTER_REQUEST		0xa0
#define F81232_GET_REGISTER		0xc0
#define F81232_SET_REGISTER		0x40

#define SERIAL_BASE_ADDRESS		0x0120
#define RECEIVE_BUFFER_REGISTER		(0x00 + SERIAL_BASE_ADDRESS)
#define INTERRUPT_ENABLE_REGISTER	(0x01 + SERIAL_BASE_ADDRESS)
#define FIFO_CONTROL_REGISTER		(0x02 + SERIAL_BASE_ADDRESS)
#define LINE_CONTROL_REGISTER		(0x03 + SERIAL_BASE_ADDRESS)
#define MODEM_CONTROL_REGISTER		(0x04 + SERIAL_BASE_ADDRESS)
#define MODEM_STATUS_REGISTER		(0x06 + SERIAL_BASE_ADDRESS)

struct f81232_private {
	struct mutex lock;
	u8 modem_control;
	u8 modem_status;
	struct work_struct interrupt_work;
	struct usb_serial_port *port;
};

static int calc_baud_divisor(speed_t baudrate)
{
	return DIV_ROUND_CLOSEST(F81232_MAX_BAUDRATE, baudrate);
}

static int f81232_get_register(struct usb_serial_port *port, u16 reg, u8 *val)
{
	int status;
	u8 *tmp;
	struct usb_device *dev = port->serial->dev;

	tmp = kmalloc(sizeof(*val), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	status = usb_control_msg(dev,
				usb_rcvctrlpipe(dev, 0),
				F81232_REGISTER_REQUEST,
				F81232_GET_REGISTER,
				reg,
				0,
				tmp,
				sizeof(*val),
				USB_CTRL_GET_TIMEOUT);
	if (status != sizeof(*val)) {
		dev_err(&port->dev, "%s failed status: %d\n", __func__, status);

		if (status < 0)
			status = usb_translate_errors(status);
		else
			status = -EIO;
	} else {
		status = 0;
		*val = *tmp;
	}

	kfree(tmp);
	return status;
}

static int f81232_set_register(struct usb_serial_port *port, u16 reg, u8 val)
{
	int status;
	u8 *tmp;
	struct usb_device *dev = port->serial->dev;

	tmp = kmalloc(sizeof(val), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	*tmp = val;

	status = usb_control_msg(dev,
				usb_sndctrlpipe(dev, 0),
				F81232_REGISTER_REQUEST,
				F81232_SET_REGISTER,
				reg,
				0,
				tmp,
				sizeof(val),
				USB_CTRL_SET_TIMEOUT);
	if (status != sizeof(val)) {
		dev_err(&port->dev, "%s failed status: %d\n", __func__, status);

		if (status < 0)
			status = usb_translate_errors(status);
		else
			status = -EIO;
	} else {
		status = 0;
	}

	kfree(tmp);
	return status;
}

static void f81232_read_msr(struct usb_serial_port *port)
{
	int status;
	u8 current_msr;
	struct tty_struct *tty;
	struct f81232_private *priv = usb_get_serial_port_data(port);

	mutex_lock(&priv->lock);
	status = f81232_get_register(port, MODEM_STATUS_REGISTER,
			&current_msr);
	if (status) {
		dev_err(&port->dev, "%s fail, status: %d\n", __func__, status);
		mutex_unlock(&priv->lock);
		return;
	}

	if (!(current_msr & UART_MSR_ANY_DELTA)) {
		mutex_unlock(&priv->lock);
		return;
	}

	priv->modem_status = current_msr;

	if (current_msr & UART_MSR_DCTS)
		port->icount.cts++;
	if (current_msr & UART_MSR_DDSR)
		port->icount.dsr++;
	if (current_msr & UART_MSR_TERI)
		port->icount.rng++;
	if (current_msr & UART_MSR_DDCD) {
		port->icount.dcd++;
		tty = tty_port_tty_get(&port->port);
		if (tty) {
			usb_serial_handle_dcd_change(port, tty,
					current_msr & UART_MSR_DCD);

			tty_kref_put(tty);
		}
	}

	wake_up_interruptible(&port->port.delta_msr_wait);
	mutex_unlock(&priv->lock);
}

static int f81232_set_mctrl(struct usb_serial_port *port,
					   unsigned int set, unsigned int clear)
{
	u8 val;
	int status;
	struct f81232_private *priv = usb_get_serial_port_data(port);

	if (((set | clear) & (TIOCM_DTR | TIOCM_RTS)) == 0)
		return 0;	/* no change */

	/* 'set' takes precedence over 'clear' */
	clear &= ~set;

	/* force enable interrupt with OUT2 */
	mutex_lock(&priv->lock);
	val = UART_MCR_OUT2 | priv->modem_control;

	if (clear & TIOCM_DTR)
		val &= ~UART_MCR_DTR;

	if (clear & TIOCM_RTS)
		val &= ~UART_MCR_RTS;

	if (set & TIOCM_DTR)
		val |= UART_MCR_DTR;

	if (set & TIOCM_RTS)
		val |= UART_MCR_RTS;

	dev_dbg(&port->dev, "%s new:%02x old:%02x\n", __func__,
			val, priv->modem_control);

	status = f81232_set_register(port, MODEM_CONTROL_REGISTER, val);
	if (status) {
		dev_err(&port->dev, "%s set MCR status < 0\n", __func__);
		mutex_unlock(&priv->lock);
		return status;
	}

	priv->modem_control = val;
	mutex_unlock(&priv->lock);

	return 0;
}

static void f81232_update_line_status(struct usb_serial_port *port,
				      unsigned char *data,
				      size_t actual_length)
{
	struct f81232_private *priv = usb_get_serial_port_data(port);

	if (!actual_length)
		return;

	switch (data[0] & 0x07) {
	case 0x00: /* msr change */
		dev_dbg(&port->dev, "IIR: MSR Change: %02x\n", data[0]);
		schedule_work(&priv->interrupt_work);
		break;
	case 0x02: /* tx-empty */
		break;
	case 0x04: /* rx data available */
		break;
	case 0x06: /* lsr change */
		/* we can forget it. the LSR will read from bulk-in */
		dev_dbg(&port->dev, "IIR: LSR Change: %02x\n", data[0]);
		break;
	}
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
	unsigned char *data = urb->transfer_buffer;
	char tty_flag;
	unsigned int i;
	u8 lsr;

	/*
	 * When opening the port we get a 1-byte packet with the current LSR,
	 * which we discard.
	 */
	if ((urb->actual_length < 2) || (urb->actual_length % 2))
		return;

	/* bulk-in data: [LSR(1Byte)+DATA(1Byte)][LSR(1Byte)+DATA(1Byte)]... */

	for (i = 0; i < urb->actual_length; i += 2) {
		tty_flag = TTY_NORMAL;
		lsr = data[i];

		if (lsr & UART_LSR_BRK_ERROR_BITS) {
			if (lsr & UART_LSR_BI) {
				tty_flag = TTY_BREAK;
				port->icount.brk++;
				usb_serial_handle_break(port);
			} else if (lsr & UART_LSR_PE) {
				tty_flag = TTY_PARITY;
				port->icount.parity++;
			} else if (lsr & UART_LSR_FE) {
				tty_flag = TTY_FRAME;
				port->icount.frame++;
			}

			if (lsr & UART_LSR_OE) {
				port->icount.overrun++;
				tty_insert_flip_char(&port->port, 0,
						TTY_OVERRUN);
			}
		}

		if (port->port.console && port->sysrq) {
			if (usb_serial_handle_sysrq_char(port, data[i + 1]))
				continue;
		}

		tty_insert_flip_char(&port->port, data[i + 1], tty_flag);
	}

	tty_flip_buffer_push(&port->port);
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

static void f81232_set_baudrate(struct usb_serial_port *port, speed_t baudrate)
{
	u8 lcr;
	int divisor;
	int status = 0;

	divisor = calc_baud_divisor(baudrate);

	status = f81232_get_register(port, LINE_CONTROL_REGISTER,
			 &lcr); /* get LCR */
	if (status) {
		dev_err(&port->dev, "%s failed to get LCR: %d\n",
			__func__, status);
		return;
	}

	status = f81232_set_register(port, LINE_CONTROL_REGISTER,
			 lcr | UART_LCR_DLAB); /* Enable DLAB */
	if (status) {
		dev_err(&port->dev, "%s failed to set DLAB: %d\n",
			__func__, status);
		return;
	}

	status = f81232_set_register(port, RECEIVE_BUFFER_REGISTER,
			 divisor & 0x00ff); /* low */
	if (status) {
		dev_err(&port->dev, "%s failed to set baudrate MSB: %d\n",
			__func__, status);
		goto reapply_lcr;
	}

	status = f81232_set_register(port, INTERRUPT_ENABLE_REGISTER,
			 (divisor & 0xff00) >> 8); /* high */
	if (status) {
		dev_err(&port->dev, "%s failed to set baudrate LSB: %d\n",
			__func__, status);
	}

reapply_lcr:
	status = f81232_set_register(port, LINE_CONTROL_REGISTER,
			lcr & ~UART_LCR_DLAB);
	if (status) {
		dev_err(&port->dev, "%s failed to set DLAB: %d\n",
			__func__, status);
	}
}

static int f81232_port_enable(struct usb_serial_port *port)
{
	u8 val;
	int status;

	/* fifo on, trigger8, clear TX/RX*/
	val = UART_FCR_TRIGGER_8 | UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR |
			UART_FCR_CLEAR_XMIT;

	status = f81232_set_register(port, FIFO_CONTROL_REGISTER, val);
	if (status) {
		dev_err(&port->dev, "%s failed to set FCR: %d\n",
			__func__, status);
		return status;
	}

	/* MSR Interrupt only, LSR will read from Bulk-in odd byte */
	status = f81232_set_register(port, INTERRUPT_ENABLE_REGISTER,
			UART_IER_MSI);
	if (status) {
		dev_err(&port->dev, "%s failed to set IER: %d\n",
			__func__, status);
		return status;
	}

	return 0;
}

static int f81232_port_disable(struct usb_serial_port *port)
{
	int status;

	status = f81232_set_register(port, INTERRUPT_ENABLE_REGISTER, 0);
	if (status) {
		dev_err(&port->dev, "%s failed to set IER: %d\n",
			__func__, status);
		return status;
	}

	return 0;
}

static void f81232_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	u8 new_lcr = 0;
	int status = 0;
	speed_t baudrate;

	/* Don't change anything if nothing has changed */
	if (old_termios && !tty_termios_hw_change(&tty->termios, old_termios))
		return;

	if (C_BAUD(tty) == B0)
		f81232_set_mctrl(port, 0, TIOCM_DTR | TIOCM_RTS);
	else if (old_termios && (old_termios->c_cflag & CBAUD) == B0)
		f81232_set_mctrl(port, TIOCM_DTR | TIOCM_RTS, 0);

	baudrate = tty_get_baud_rate(tty);
	if (baudrate > 0) {
		if (baudrate > F81232_MAX_BAUDRATE) {
			baudrate = F81232_MAX_BAUDRATE;
			tty_encode_baud_rate(tty, baudrate, baudrate);
		}
		f81232_set_baudrate(port, baudrate);
	}

	if (C_PARENB(tty)) {
		new_lcr |= UART_LCR_PARITY;

		if (!C_PARODD(tty))
			new_lcr |= UART_LCR_EPAR;

		if (C_CMSPAR(tty))
			new_lcr |= UART_LCR_SPAR;
	}

	if (C_CSTOPB(tty))
		new_lcr |= UART_LCR_STOP;

	switch (C_CSIZE(tty)) {
	case CS5:
		new_lcr |= UART_LCR_WLEN5;
		break;
	case CS6:
		new_lcr |= UART_LCR_WLEN6;
		break;
	case CS7:
		new_lcr |= UART_LCR_WLEN7;
		break;
	default:
	case CS8:
		new_lcr |= UART_LCR_WLEN8;
		break;
	}

	status = f81232_set_register(port, LINE_CONTROL_REGISTER, new_lcr);
	if (status) {
		dev_err(&port->dev, "%s failed to set LCR: %d\n",
			__func__, status);
	}
}

static int f81232_tiocmget(struct tty_struct *tty)
{
	int r;
	struct usb_serial_port *port = tty->driver_data;
	struct f81232_private *port_priv = usb_get_serial_port_data(port);
	u8 mcr, msr;

	/* force get current MSR changed state */
	f81232_read_msr(port);

	mutex_lock(&port_priv->lock);
	mcr = port_priv->modem_control;
	msr = port_priv->modem_status;
	mutex_unlock(&port_priv->lock);

	r = (mcr & UART_MCR_DTR ? TIOCM_DTR : 0) |
		(mcr & UART_MCR_RTS ? TIOCM_RTS : 0) |
		(msr & UART_MSR_CTS ? TIOCM_CTS : 0) |
		(msr & UART_MSR_DCD ? TIOCM_CAR : 0) |
		(msr & UART_MSR_RI ? TIOCM_RI : 0) |
		(msr & UART_MSR_DSR ? TIOCM_DSR : 0);

	return r;
}

static int f81232_tiocmset(struct tty_struct *tty,
			unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;

	return f81232_set_mctrl(port, set, clear);
}

static int f81232_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	int result;

	result = f81232_port_enable(port);
	if (result)
		return result;

	/* Setup termios */
	if (tty)
		f81232_set_termios(tty, port, NULL);

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

	return 0;
}

static void f81232_close(struct usb_serial_port *port)
{
	f81232_port_disable(port);
	usb_serial_generic_close(port);
	usb_kill_urb(port->interrupt_in_urb);
}

static void f81232_dtr_rts(struct usb_serial_port *port, int on)
{
	if (on)
		f81232_set_mctrl(port, TIOCM_DTR | TIOCM_RTS, 0);
	else
		f81232_set_mctrl(port, 0, TIOCM_DTR | TIOCM_RTS);
}

static int f81232_carrier_raised(struct usb_serial_port *port)
{
	u8 msr;
	struct f81232_private *priv = usb_get_serial_port_data(port);

	mutex_lock(&priv->lock);
	msr = priv->modem_status;
	mutex_unlock(&priv->lock);

	if (msr & UART_MSR_DCD)
		return 1;
	return 0;
}

static int f81232_get_serial_info(struct usb_serial_port *port,
		unsigned long arg)
{
	struct serial_struct ser;

	memset(&ser, 0, sizeof(ser));

	ser.type = PORT_16550A;
	ser.line = port->minor;
	ser.port = port->port_number;
	ser.baud_base = F81232_MAX_BAUDRATE;

	if (copy_to_user((void __user *)arg, &ser, sizeof(ser)))
		return -EFAULT;

	return 0;
}

static int f81232_ioctl(struct tty_struct *tty,
			unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;

	switch (cmd) {
	case TIOCGSERIAL:
		return f81232_get_serial_info(port, arg);
	default:
		break;
	}
	return -ENOIOCTLCMD;
}

static void  f81232_interrupt_work(struct work_struct *work)
{
	struct f81232_private *priv =
		container_of(work, struct f81232_private, interrupt_work);

	f81232_read_msr(priv->port);
}

static int f81232_port_probe(struct usb_serial_port *port)
{
	struct f81232_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->lock);
	INIT_WORK(&priv->interrupt_work,  f81232_interrupt_work);

	usb_set_serial_port_data(port, priv);

	port->port.drain_delay = 256;
	priv->port = port;

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
	.dtr_rts =		f81232_dtr_rts,
	.carrier_raised =	f81232_carrier_raised,
	.ioctl =		f81232_ioctl,
	.break_ctl =		f81232_break_ctl,
	.set_termios =		f81232_set_termios,
	.tiocmget =		f81232_tiocmget,
	.tiocmset =		f81232_tiocmset,
	.tiocmiwait =		usb_serial_generic_tiocmiwait,
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
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
MODULE_AUTHOR("Peter Hong <peter_hong@fintek.com.tw>");
MODULE_LICENSE("GPL v2");
