// SPDX-License-Identifier: GPL-2.0
/*
 * Fintek F81232 USB to serial adaptor driver
 * Fintek F81532A/534A/535/536 USB to 2/4/8/12 serial adaptor driver
 *
 * Copyright (C) 2012 Greg Kroah-Hartman (gregkh@linuxfoundation.org)
 * Copyright (C) 2012 Linux Foundation
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

#define F81232_ID		\
	{ USB_DEVICE(0x1934, 0x0706) }	/* 1 port UART device */

#define F81534A_SERIES_ID	\
	{ USB_DEVICE(0x2c42, 0x1602) },	/* In-Box 2 port UART device */	\
	{ USB_DEVICE(0x2c42, 0x1604) },	/* In-Box 4 port UART device */	\
	{ USB_DEVICE(0x2c42, 0x1605) },	/* In-Box 8 port UART device */	\
	{ USB_DEVICE(0x2c42, 0x1606) },	/* In-Box 12 port UART device */ \
	{ USB_DEVICE(0x2c42, 0x1608) },	/* Non-Flash type */ \
	{ USB_DEVICE(0x2c42, 0x1632) },	/* 2 port UART device */ \
	{ USB_DEVICE(0x2c42, 0x1634) },	/* 4 port UART device */ \
	{ USB_DEVICE(0x2c42, 0x1635) },	/* 8 port UART device */ \
	{ USB_DEVICE(0x2c42, 0x1636) }	/* 12 port UART device */

#define F81534A_CTRL_ID		\
	{ USB_DEVICE(0x2c42, 0x16f8) }	/* Global control device */

static const struct usb_device_id f81232_id_table[] = {
	F81232_ID,
	{ }					/* Terminating entry */
};

static const struct usb_device_id f81534a_id_table[] = {
	F81534A_SERIES_ID,
	{ }					/* Terminating entry */
};

static const struct usb_device_id f81534a_ctrl_id_table[] = {
	F81534A_CTRL_ID,
	{ }					/* Terminating entry */
};

static const struct usb_device_id combined_id_table[] = {
	F81232_ID,
	F81534A_SERIES_ID,
	F81534A_CTRL_ID,
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, combined_id_table);

/* Maximum baudrate for F81232 */
#define F81232_MAX_BAUDRATE		1500000
#define F81232_DEF_BAUDRATE		9600

/* USB Control EP parameter */
#define F81232_REGISTER_REQUEST		0xa0
#define F81232_GET_REGISTER		0xc0
#define F81232_SET_REGISTER		0x40
#define F81534A_ACCESS_REG_RETRY	2

#define SERIAL_BASE_ADDRESS		0x0120
#define RECEIVE_BUFFER_REGISTER		(0x00 + SERIAL_BASE_ADDRESS)
#define INTERRUPT_ENABLE_REGISTER	(0x01 + SERIAL_BASE_ADDRESS)
#define FIFO_CONTROL_REGISTER		(0x02 + SERIAL_BASE_ADDRESS)
#define LINE_CONTROL_REGISTER		(0x03 + SERIAL_BASE_ADDRESS)
#define MODEM_CONTROL_REGISTER		(0x04 + SERIAL_BASE_ADDRESS)
#define LINE_STATUS_REGISTER		(0x05 + SERIAL_BASE_ADDRESS)
#define MODEM_STATUS_REGISTER		(0x06 + SERIAL_BASE_ADDRESS)

/*
 * F81232 Clock registers (106h)
 *
 * Bit1-0:	Clock source selector
 *			00: 1.846MHz.
 *			01: 18.46MHz.
 *			10: 24MHz.
 *			11: 14.77MHz.
 */
#define F81232_CLK_REGISTER		0x106
#define F81232_CLK_1_846_MHZ		0
#define F81232_CLK_18_46_MHZ		BIT(0)
#define F81232_CLK_24_MHZ		BIT(1)
#define F81232_CLK_14_77_MHZ		(BIT(1) | BIT(0))
#define F81232_CLK_MASK			GENMASK(1, 0)

#define F81534A_MODE_REG		0x107
#define F81534A_TRIGGER_MASK		GENMASK(3, 2)
#define F81534A_TRIGGER_MULTIPLE_4X	BIT(3)
#define F81534A_FIFO_128BYTE		(BIT(1) | BIT(0))

/* Serial port self GPIO control, 2bytes [control&output data][input data] */
#define F81534A_GPIO_REG		0x10e
#define F81534A_GPIO_MODE2_DIR		BIT(6) /* 1: input, 0: output */
#define F81534A_GPIO_MODE1_DIR		BIT(5)
#define F81534A_GPIO_MODE0_DIR		BIT(4)
#define F81534A_GPIO_MODE2_OUTPUT	BIT(2)
#define F81534A_GPIO_MODE1_OUTPUT	BIT(1)
#define F81534A_GPIO_MODE0_OUTPUT	BIT(0)

#define F81534A_CTRL_CMD_ENABLE_PORT	0x116

struct f81232_private {
	struct mutex lock;
	u8 modem_control;
	u8 modem_status;
	u8 shadow_lcr;
	speed_t baud_base;
	struct work_struct lsr_work;
	struct work_struct interrupt_work;
	struct usb_serial_port *port;
};

static u32 const baudrate_table[] = { 115200, 921600, 1152000, 1500000 };
static u8 const clock_table[] = { F81232_CLK_1_846_MHZ, F81232_CLK_14_77_MHZ,
				F81232_CLK_18_46_MHZ, F81232_CLK_24_MHZ };

static int calc_baud_divisor(speed_t baudrate, speed_t clockrate)
{
	if (!baudrate)
		return 0;

	return DIV_ROUND_CLOSEST(clockrate, baudrate);
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
	if (status < 0) {
		dev_err(&port->dev, "%s failed status: %d\n", __func__, status);
		status = usb_translate_errors(status);
	} else {
		status = 0;
	}

	kfree(tmp);
	return status;
}

static int f81232_set_mask_register(struct usb_serial_port *port, u16 reg,
					u8 mask, u8 val)
{
	int status;
	u8 tmp;

	status = f81232_get_register(port, reg, &tmp);
	if (status)
		return status;

	tmp = (tmp & ~mask) | (val & mask);

	return f81232_set_register(port, reg, tmp);
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

static char f81232_handle_lsr(struct usb_serial_port *port, u8 lsr)
{
	struct f81232_private *priv = usb_get_serial_port_data(port);
	char tty_flag = TTY_NORMAL;

	if (!(lsr & UART_LSR_BRK_ERROR_BITS))
		return tty_flag;

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
		schedule_work(&priv->lsr_work);
		tty_insert_flip_char(&port->port, 0, TTY_OVERRUN);
	}

	return tty_flag;
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
		lsr = data[i];
		tty_flag = f81232_handle_lsr(port, lsr);

		if (port->sysrq) {
			if (usb_serial_handle_sysrq_char(port, data[i + 1]))
				continue;
		}

		tty_insert_flip_char(&port->port, data[i + 1], tty_flag);
	}

	tty_flip_buffer_push(&port->port);
}

static void f81534a_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	char tty_flag;
	unsigned int i;
	u8 lsr;
	u8 len;

	if (urb->actual_length < 3) {
		dev_err(&port->dev, "short message received: %d\n",
				urb->actual_length);
		return;
	}

	len = data[0];
	if (len != urb->actual_length) {
		dev_err(&port->dev, "malformed message received: %d (%d)\n",
				urb->actual_length, len);
		return;
	}

	/* bulk-in data: [LEN][Data.....][LSR] */
	lsr = data[len - 1];
	tty_flag = f81232_handle_lsr(port, lsr);

	if (port->sysrq) {
		for (i = 1; i < len - 1; ++i) {
			if (!usb_serial_handle_sysrq_char(port, data[i])) {
				tty_insert_flip_char(&port->port, data[i],
						tty_flag);
			}
		}
	} else {
		tty_insert_flip_string_fixed_flag(&port->port, &data[1],
							tty_flag, len - 2);
	}

	tty_flip_buffer_push(&port->port);
}

static void f81232_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct f81232_private *priv = usb_get_serial_port_data(port);
	int status;

	mutex_lock(&priv->lock);

	if (break_state)
		priv->shadow_lcr |= UART_LCR_SBC;
	else
		priv->shadow_lcr &= ~UART_LCR_SBC;

	status = f81232_set_register(port, LINE_CONTROL_REGISTER,
					priv->shadow_lcr);
	if (status)
		dev_err(&port->dev, "set break failed: %d\n", status);

	mutex_unlock(&priv->lock);
}

static int f81232_find_clk(speed_t baudrate)
{
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(baudrate_table); ++idx) {
		if (baudrate <= baudrate_table[idx] &&
				baudrate_table[idx] % baudrate == 0)
			return idx;
	}

	return -EINVAL;
}

static void f81232_set_baudrate(struct tty_struct *tty,
				struct usb_serial_port *port, speed_t baudrate,
				speed_t old_baudrate)
{
	struct f81232_private *priv = usb_get_serial_port_data(port);
	u8 lcr;
	int divisor;
	int status = 0;
	int i;
	int idx;
	speed_t baud_list[] = { baudrate, old_baudrate, F81232_DEF_BAUDRATE };

	for (i = 0; i < ARRAY_SIZE(baud_list); ++i) {
		idx = f81232_find_clk(baud_list[i]);
		if (idx >= 0) {
			baudrate = baud_list[i];
			tty_encode_baud_rate(tty, baudrate, baudrate);
			break;
		}
	}

	if (idx < 0)
		return;

	priv->baud_base = baudrate_table[idx];
	divisor = calc_baud_divisor(baudrate, priv->baud_base);

	status = f81232_set_mask_register(port, F81232_CLK_REGISTER,
			F81232_CLK_MASK, clock_table[idx]);
	if (status) {
		dev_err(&port->dev, "%s failed to set CLK_REG: %d\n",
			__func__, status);
		return;
	}

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
	struct f81232_private *priv = usb_get_serial_port_data(port);
	u8 new_lcr = 0;
	int status = 0;
	speed_t baudrate;
	speed_t old_baud;

	/* Don't change anything if nothing has changed */
	if (old_termios && !tty_termios_hw_change(&tty->termios, old_termios))
		return;

	if (C_BAUD(tty) == B0)
		f81232_set_mctrl(port, 0, TIOCM_DTR | TIOCM_RTS);
	else if (old_termios && (old_termios->c_cflag & CBAUD) == B0)
		f81232_set_mctrl(port, TIOCM_DTR | TIOCM_RTS, 0);

	baudrate = tty_get_baud_rate(tty);
	if (baudrate > 0) {
		if (old_termios)
			old_baud = tty_termios_baud_rate(old_termios);
		else
			old_baud = F81232_DEF_BAUDRATE;

		f81232_set_baudrate(tty, port, baudrate, old_baud);
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

	mutex_lock(&priv->lock);

	new_lcr |= (priv->shadow_lcr & UART_LCR_SBC);
	status = f81232_set_register(port, LINE_CONTROL_REGISTER, new_lcr);
	if (status) {
		dev_err(&port->dev, "%s failed to set LCR: %d\n",
			__func__, status);
	}

	priv->shadow_lcr = new_lcr;

	mutex_unlock(&priv->lock);
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

static int f81534a_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	int status;
	u8 mask;
	u8 val;

	val = F81534A_TRIGGER_MULTIPLE_4X | F81534A_FIFO_128BYTE;
	mask = F81534A_TRIGGER_MASK | F81534A_FIFO_128BYTE;

	status = f81232_set_mask_register(port, F81534A_MODE_REG, mask, val);
	if (status) {
		dev_err(&port->dev, "failed to set MODE_REG: %d\n", status);
		return status;
	}

	return f81232_open(tty, port);
}

static void f81232_close(struct usb_serial_port *port)
{
	struct f81232_private *port_priv = usb_get_serial_port_data(port);

	f81232_port_disable(port);
	usb_serial_generic_close(port);
	usb_kill_urb(port->interrupt_in_urb);
	flush_work(&port_priv->interrupt_work);
	flush_work(&port_priv->lsr_work);
}

static void f81232_dtr_rts(struct usb_serial_port *port, int on)
{
	if (on)
		f81232_set_mctrl(port, TIOCM_DTR | TIOCM_RTS, 0);
	else
		f81232_set_mctrl(port, 0, TIOCM_DTR | TIOCM_RTS);
}

static bool f81232_tx_empty(struct usb_serial_port *port)
{
	int status;
	u8 tmp;

	status = f81232_get_register(port, LINE_STATUS_REGISTER, &tmp);
	if (!status) {
		if ((tmp & UART_LSR_TEMT) != UART_LSR_TEMT)
			return false;
	}

	return true;
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

static int f81232_get_serial_info(struct tty_struct *tty,
		struct serial_struct *ss)
{
	struct usb_serial_port *port = tty->driver_data;
	struct f81232_private *priv = usb_get_serial_port_data(port);

	ss->type = PORT_16550A;
	ss->line = port->minor;
	ss->port = port->port_number;
	ss->baud_base = priv->baud_base;
	return 0;
}

static void  f81232_interrupt_work(struct work_struct *work)
{
	struct f81232_private *priv =
		container_of(work, struct f81232_private, interrupt_work);

	f81232_read_msr(priv->port);
}

static void f81232_lsr_worker(struct work_struct *work)
{
	struct f81232_private *priv;
	struct usb_serial_port *port;
	int status;
	u8 tmp;

	priv = container_of(work, struct f81232_private, lsr_work);
	port = priv->port;

	status = f81232_get_register(port, LINE_STATUS_REGISTER, &tmp);
	if (status)
		dev_warn(&port->dev, "read LSR failed: %d\n", status);
}

static int f81534a_ctrl_set_register(struct usb_interface *intf, u16 reg,
					u16 size, void *val)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	int retry = F81534A_ACCESS_REG_RETRY;
	int status;
	u8 *tmp;

	tmp = kmemdup(val, size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	while (retry--) {
		status = usb_control_msg(dev,
					usb_sndctrlpipe(dev, 0),
					F81232_REGISTER_REQUEST,
					F81232_SET_REGISTER,
					reg,
					0,
					tmp,
					size,
					USB_CTRL_SET_TIMEOUT);
		if (status < 0) {
			status = usb_translate_errors(status);
			if (status == -EIO)
				continue;
		} else {
			status = 0;
		}

		break;
	}

	if (status) {
		dev_err(&intf->dev, "failed to set register 0x%x: %d\n",
				reg, status);
	}

	kfree(tmp);
	return status;
}

static int f81534a_ctrl_enable_all_ports(struct usb_interface *intf, bool en)
{
	unsigned char enable[2] = {0};
	int status;

	/*
	 * Enable all available serial ports, define as following:
	 * bit 15	: Reset behavior (when HUB got soft reset)
	 *			0: maintain all serial port enabled state.
	 *			1: disable all serial port.
	 * bit 0~11	: Serial port enable bit.
	 */
	if (en) {
		enable[0] = 0xff;
		enable[1] = 0x8f;
	}

	status = f81534a_ctrl_set_register(intf, F81534A_CTRL_CMD_ENABLE_PORT,
			sizeof(enable), enable);
	if (status)
		dev_err(&intf->dev, "failed to enable ports: %d\n", status);

	return status;
}

static int f81534a_ctrl_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	return f81534a_ctrl_enable_all_ports(intf, true);
}

static void f81534a_ctrl_disconnect(struct usb_interface *intf)
{
	f81534a_ctrl_enable_all_ports(intf, false);
}

static int f81534a_ctrl_resume(struct usb_interface *intf)
{
	return f81534a_ctrl_enable_all_ports(intf, true);
}

static int f81232_port_probe(struct usb_serial_port *port)
{
	struct f81232_private *priv;

	priv = devm_kzalloc(&port->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->lock);
	INIT_WORK(&priv->interrupt_work,  f81232_interrupt_work);
	INIT_WORK(&priv->lsr_work, f81232_lsr_worker);

	usb_set_serial_port_data(port, priv);

	port->port.drain_delay = 256;
	priv->port = port;

	return 0;
}

static int f81534a_port_probe(struct usb_serial_port *port)
{
	int status;

	/* tri-state with pull-high, default RS232 Mode */
	status = f81232_set_register(port, F81534A_GPIO_REG,
					F81534A_GPIO_MODE2_DIR);
	if (status)
		return status;

	return f81232_port_probe(port);
}

static int f81232_suspend(struct usb_serial *serial, pm_message_t message)
{
	struct usb_serial_port *port = serial->port[0];
	struct f81232_private *port_priv = usb_get_serial_port_data(port);
	int i;

	for (i = 0; i < ARRAY_SIZE(port->read_urbs); ++i)
		usb_kill_urb(port->read_urbs[i]);

	usb_kill_urb(port->interrupt_in_urb);

	if (port_priv) {
		flush_work(&port_priv->interrupt_work);
		flush_work(&port_priv->lsr_work);
	}

	return 0;
}

static int f81232_resume(struct usb_serial *serial)
{
	struct usb_serial_port *port = serial->port[0];
	int result;

	if (tty_port_initialized(&port->port)) {
		result = usb_submit_urb(port->interrupt_in_urb, GFP_NOIO);
		if (result) {
			dev_err(&port->dev, "submit interrupt urb failed: %d\n",
					result);
			return result;
		}
	}

	return usb_serial_generic_resume(serial);
}

static struct usb_serial_driver f81232_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"f81232",
	},
	.id_table =		f81232_id_table,
	.num_ports =		1,
	.bulk_in_size =		256,
	.bulk_out_size =	256,
	.open =			f81232_open,
	.close =		f81232_close,
	.dtr_rts =		f81232_dtr_rts,
	.carrier_raised =	f81232_carrier_raised,
	.get_serial =		f81232_get_serial_info,
	.break_ctl =		f81232_break_ctl,
	.set_termios =		f81232_set_termios,
	.tiocmget =		f81232_tiocmget,
	.tiocmset =		f81232_tiocmset,
	.tiocmiwait =		usb_serial_generic_tiocmiwait,
	.tx_empty =		f81232_tx_empty,
	.process_read_urb =	f81232_process_read_urb,
	.read_int_callback =	f81232_read_int_callback,
	.port_probe =		f81232_port_probe,
	.suspend =		f81232_suspend,
	.resume =		f81232_resume,
};

static struct usb_serial_driver f81534a_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"f81534a",
	},
	.id_table =		f81534a_id_table,
	.num_ports =		1,
	.open =			f81534a_open,
	.close =		f81232_close,
	.dtr_rts =		f81232_dtr_rts,
	.carrier_raised =	f81232_carrier_raised,
	.get_serial =		f81232_get_serial_info,
	.break_ctl =		f81232_break_ctl,
	.set_termios =		f81232_set_termios,
	.tiocmget =		f81232_tiocmget,
	.tiocmset =		f81232_tiocmset,
	.tiocmiwait =		usb_serial_generic_tiocmiwait,
	.tx_empty =		f81232_tx_empty,
	.process_read_urb =	f81534a_process_read_urb,
	.read_int_callback =	f81232_read_int_callback,
	.port_probe =		f81534a_port_probe,
	.suspend =		f81232_suspend,
	.resume =		f81232_resume,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&f81232_device,
	&f81534a_device,
	NULL,
};

static struct usb_driver f81534a_ctrl_driver = {
	.name =		"f81534a_ctrl",
	.id_table =	f81534a_ctrl_id_table,
	.probe =	f81534a_ctrl_probe,
	.disconnect =	f81534a_ctrl_disconnect,
	.resume =	f81534a_ctrl_resume,
};

static int __init f81232_init(void)
{
	int status;

	status = usb_register_driver(&f81534a_ctrl_driver, THIS_MODULE,
			KBUILD_MODNAME);
	if (status)
		return status;

	status = usb_serial_register_drivers(serial_drivers, KBUILD_MODNAME,
			combined_id_table);
	if (status) {
		usb_deregister(&f81534a_ctrl_driver);
		return status;
	}

	return 0;
}

static void __exit f81232_exit(void)
{
	usb_serial_deregister_drivers(serial_drivers);
	usb_deregister(&f81534a_ctrl_driver);
}

module_init(f81232_init);
module_exit(f81232_exit);

MODULE_DESCRIPTION("Fintek F81232/532A/534A/535/536 USB to serial driver");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
MODULE_AUTHOR("Peter Hong <peter_hong@fintek.com.tw>");
MODULE_LICENSE("GPL v2");
