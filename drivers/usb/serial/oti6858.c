// SPDX-License-Identifier: GPL-2.0
/*
 * Ours Technology Inc. OTi-6858 USB to serial adapter driver.
 *
 * Copyleft  (C) 2007 Kees Lemmens (adapted for kernel 2.6.20)
 * Copyright (C) 2006 Tomasz Michal Lukaszewski (FIXME: add e-mail)
 * Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2003 IBM Corp.
 *
 * Many thanks to the authors of pl2303 driver: all functions in this file
 * are heavily based on pl2303 code, buffering code is a 1-to-1 copy.
 *
 * Warning! You use this driver on your own risk! The only official
 * description of this device I have is datasheet from manufacturer,
 * and it doesn't contain almost any information needed to write a driver.
 * Almost all knowlegde used while writing this driver was gathered by:
 *  - analyzing traffic between device and the M$ Windows 2000 driver,
 *  - trying different bit combinations and checking pin states
 *    with a voltmeter,
 *  - receiving malformed frames and producing buffer overflows
 *    to learn how errors are reported,
 * So, THIS CODE CAN DESTROY OTi-6858 AND ANY OTHER DEVICES, THAT ARE
 * CONNECTED TO IT!
 *
 * See Documentation/usb/usb-serial.txt for more information on using this
 * driver
 *
 * TODO:
 *  - implement correct flushing for ioctls and oti6858_close()
 *  - check how errors (rx overflow, parity error, framing error) are reported
 *  - implement oti6858_break_ctl()
 *  - implement more ioctls
 *  - test/implement flow control
 *  - allow setting custom baud rates
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
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/uaccess.h>
#include <linux/kfifo.h>
#include "oti6858.h"

#define OTI6858_DESCRIPTION \
	"Ours Technology Inc. OTi-6858 USB to serial adapter driver"
#define OTI6858_AUTHOR "Tomasz Michal Lukaszewski <FIXME@FIXME>"

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(OTI6858_VENDOR_ID, OTI6858_PRODUCT_ID) },
	{ }
};

MODULE_DEVICE_TABLE(usb, id_table);

/* requests */
#define	OTI6858_REQ_GET_STATUS		(USB_DIR_IN | USB_TYPE_VENDOR | 0x00)
#define	OTI6858_REQ_T_GET_STATUS	0x01

#define	OTI6858_REQ_SET_LINE		(USB_DIR_OUT | USB_TYPE_VENDOR | 0x00)
#define	OTI6858_REQ_T_SET_LINE		0x00

#define	OTI6858_REQ_CHECK_TXBUFF	(USB_DIR_IN | USB_TYPE_VENDOR | 0x01)
#define	OTI6858_REQ_T_CHECK_TXBUFF	0x00

/* format of the control packet */
struct oti6858_control_pkt {
	__le16	divisor;	/* baud rate = 96000000 / (16 * divisor), LE */
#define OTI6858_MAX_BAUD_RATE	3000000
	u8	frame_fmt;
#define FMT_STOP_BITS_MASK	0xc0
#define FMT_STOP_BITS_1		0x00
#define FMT_STOP_BITS_2		0x40	/* 1.5 stop bits if FMT_DATA_BITS_5 */
#define FMT_PARITY_MASK		0x38
#define FMT_PARITY_NONE		0x00
#define FMT_PARITY_ODD		0x08
#define FMT_PARITY_EVEN		0x18
#define FMT_PARITY_MARK		0x28
#define FMT_PARITY_SPACE	0x38
#define FMT_DATA_BITS_MASK	0x03
#define FMT_DATA_BITS_5		0x00
#define FMT_DATA_BITS_6		0x01
#define FMT_DATA_BITS_7		0x02
#define FMT_DATA_BITS_8		0x03
	u8	something;	/* always equals 0x43 */
	u8	control;	/* settings of flow control lines */
#define CONTROL_MASK		0x0c
#define CONTROL_DTR_HIGH	0x08
#define CONTROL_RTS_HIGH	0x04
	u8	tx_status;
#define	TX_BUFFER_EMPTIED	0x09
	u8	pin_state;
#define PIN_MASK		0x3f
#define PIN_MSR_MASK		0x1b
#define PIN_RTS			0x20	/* output pin */
#define PIN_CTS			0x10	/* input pin, active low */
#define PIN_DSR			0x08	/* input pin, active low */
#define PIN_DTR			0x04	/* output pin */
#define PIN_RI			0x02	/* input pin, active low */
#define PIN_DCD			0x01	/* input pin, active low */
	u8	rx_bytes_avail;		/* number of bytes in rx buffer */;
};

#define OTI6858_CTRL_PKT_SIZE	sizeof(struct oti6858_control_pkt)
#define OTI6858_CTRL_EQUALS_PENDING(a, priv) \
	(((a)->divisor == (priv)->pending_setup.divisor) \
	  && ((a)->control == (priv)->pending_setup.control) \
	  && ((a)->frame_fmt == (priv)->pending_setup.frame_fmt))

/* function prototypes */
static int oti6858_open(struct tty_struct *tty, struct usb_serial_port *port);
static void oti6858_close(struct usb_serial_port *port);
static void oti6858_set_termios(struct tty_struct *tty,
			struct usb_serial_port *port, struct ktermios *old);
static void oti6858_init_termios(struct tty_struct *tty);
static void oti6858_read_int_callback(struct urb *urb);
static void oti6858_read_bulk_callback(struct urb *urb);
static void oti6858_write_bulk_callback(struct urb *urb);
static int oti6858_write(struct tty_struct *tty, struct usb_serial_port *port,
			const unsigned char *buf, int count);
static int oti6858_write_room(struct tty_struct *tty);
static int oti6858_chars_in_buffer(struct tty_struct *tty);
static int oti6858_tiocmget(struct tty_struct *tty);
static int oti6858_tiocmset(struct tty_struct *tty,
				unsigned int set, unsigned int clear);
static int oti6858_port_probe(struct usb_serial_port *port);
static int oti6858_port_remove(struct usb_serial_port *port);

/* device info */
static struct usb_serial_driver oti6858_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"oti6858",
	},
	.id_table =		id_table,
	.num_ports =		1,
	.num_bulk_in =		1,
	.num_bulk_out =		1,
	.num_interrupt_in =	1,
	.open =			oti6858_open,
	.close =		oti6858_close,
	.write =		oti6858_write,
	.set_termios =		oti6858_set_termios,
	.init_termios = 	oti6858_init_termios,
	.tiocmget =		oti6858_tiocmget,
	.tiocmset =		oti6858_tiocmset,
	.tiocmiwait =		usb_serial_generic_tiocmiwait,
	.read_bulk_callback =	oti6858_read_bulk_callback,
	.read_int_callback =	oti6858_read_int_callback,
	.write_bulk_callback =	oti6858_write_bulk_callback,
	.write_room =		oti6858_write_room,
	.chars_in_buffer =	oti6858_chars_in_buffer,
	.port_probe =		oti6858_port_probe,
	.port_remove =		oti6858_port_remove,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&oti6858_device, NULL
};

struct oti6858_private {
	spinlock_t lock;

	struct oti6858_control_pkt status;

	struct {
		u8 read_urb_in_use;
		u8 write_urb_in_use;
	} flags;
	struct delayed_work delayed_write_work;

	struct {
		__le16 divisor;
		u8 frame_fmt;
		u8 control;
	} pending_setup;
	u8 transient;
	u8 setup_done;
	struct delayed_work delayed_setup_work;

	struct usb_serial_port *port;   /* USB port with which associated */
};

static void setup_line(struct work_struct *work)
{
	struct oti6858_private *priv = container_of(work,
			struct oti6858_private, delayed_setup_work.work);
	struct usb_serial_port *port = priv->port;
	struct oti6858_control_pkt *new_setup;
	unsigned long flags;
	int result;

	new_setup = kmalloc(OTI6858_CTRL_PKT_SIZE, GFP_KERNEL);
	if (!new_setup) {
		/* we will try again */
		schedule_delayed_work(&priv->delayed_setup_work,
						msecs_to_jiffies(2));
		return;
	}

	result = usb_control_msg(port->serial->dev,
				usb_rcvctrlpipe(port->serial->dev, 0),
				OTI6858_REQ_T_GET_STATUS,
				OTI6858_REQ_GET_STATUS,
				0, 0,
				new_setup, OTI6858_CTRL_PKT_SIZE,
				100);

	if (result != OTI6858_CTRL_PKT_SIZE) {
		dev_err(&port->dev, "%s(): error reading status\n", __func__);
		kfree(new_setup);
		/* we will try again */
		schedule_delayed_work(&priv->delayed_setup_work,
							msecs_to_jiffies(2));
		return;
	}

	spin_lock_irqsave(&priv->lock, flags);
	if (!OTI6858_CTRL_EQUALS_PENDING(new_setup, priv)) {
		new_setup->divisor = priv->pending_setup.divisor;
		new_setup->control = priv->pending_setup.control;
		new_setup->frame_fmt = priv->pending_setup.frame_fmt;

		spin_unlock_irqrestore(&priv->lock, flags);
		result = usb_control_msg(port->serial->dev,
					usb_sndctrlpipe(port->serial->dev, 0),
					OTI6858_REQ_T_SET_LINE,
					OTI6858_REQ_SET_LINE,
					0, 0,
					new_setup, OTI6858_CTRL_PKT_SIZE,
					100);
	} else {
		spin_unlock_irqrestore(&priv->lock, flags);
		result = 0;
	}
	kfree(new_setup);

	spin_lock_irqsave(&priv->lock, flags);
	if (result != OTI6858_CTRL_PKT_SIZE)
		priv->transient = 0;
	priv->setup_done = 1;
	spin_unlock_irqrestore(&priv->lock, flags);

	dev_dbg(&port->dev, "%s(): submitting interrupt urb\n", __func__);
	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (result != 0) {
		dev_err(&port->dev, "%s(): usb_submit_urb() failed with error %d\n",
			__func__, result);
	}
}

static void send_data(struct work_struct *work)
{
	struct oti6858_private *priv = container_of(work,
			struct oti6858_private, delayed_write_work.work);
	struct usb_serial_port *port = priv->port;
	int count = 0, result;
	unsigned long flags;
	u8 *allow;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->flags.write_urb_in_use) {
		spin_unlock_irqrestore(&priv->lock, flags);
		schedule_delayed_work(&priv->delayed_write_work,
						msecs_to_jiffies(2));
		return;
	}
	priv->flags.write_urb_in_use = 1;
	spin_unlock_irqrestore(&priv->lock, flags);

	spin_lock_irqsave(&port->lock, flags);
	count = kfifo_len(&port->write_fifo);
	spin_unlock_irqrestore(&port->lock, flags);

	if (count > port->bulk_out_size)
		count = port->bulk_out_size;

	if (count != 0) {
		allow = kmalloc(1, GFP_KERNEL);
		if (!allow)
			return;

		result = usb_control_msg(port->serial->dev,
				usb_rcvctrlpipe(port->serial->dev, 0),
				OTI6858_REQ_T_CHECK_TXBUFF,
				OTI6858_REQ_CHECK_TXBUFF,
				count, 0, allow, 1, 100);
		if (result != 1 || *allow != 0)
			count = 0;
		kfree(allow);
	}

	if (count == 0) {
		priv->flags.write_urb_in_use = 0;

		dev_dbg(&port->dev, "%s(): submitting interrupt urb\n", __func__);
		result = usb_submit_urb(port->interrupt_in_urb, GFP_NOIO);
		if (result != 0) {
			dev_err(&port->dev, "%s(): usb_submit_urb() failed with error %d\n",
				__func__, result);
		}
		return;
	}

	count = kfifo_out_locked(&port->write_fifo,
					port->write_urb->transfer_buffer,
					count, &port->lock);
	port->write_urb->transfer_buffer_length = count;
	result = usb_submit_urb(port->write_urb, GFP_NOIO);
	if (result != 0) {
		dev_err_console(port, "%s(): usb_submit_urb() failed with error %d\n",
				__func__, result);
		priv->flags.write_urb_in_use = 0;
	}

	usb_serial_port_softint(port);
}

static int oti6858_port_probe(struct usb_serial_port *port)
{
	struct oti6858_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	priv->port = port;
	INIT_DELAYED_WORK(&priv->delayed_setup_work, setup_line);
	INIT_DELAYED_WORK(&priv->delayed_write_work, send_data);

	usb_set_serial_port_data(port, priv);

	port->port.drain_delay = 256;	/* FIXME: check the FIFO length */

	return 0;
}

static int oti6858_port_remove(struct usb_serial_port *port)
{
	struct oti6858_private *priv;

	priv = usb_get_serial_port_data(port);
	kfree(priv);

	return 0;
}

static int oti6858_write(struct tty_struct *tty, struct usb_serial_port *port,
			const unsigned char *buf, int count)
{
	if (!count)
		return count;

	count = kfifo_in_locked(&port->write_fifo, buf, count, &port->lock);

	return count;
}

static int oti6858_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int room = 0;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	room = kfifo_avail(&port->write_fifo);
	spin_unlock_irqrestore(&port->lock, flags);

	return room;
}

static int oti6858_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int chars = 0;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	chars = kfifo_len(&port->write_fifo);
	spin_unlock_irqrestore(&port->lock, flags);

	return chars;
}

static void oti6858_init_termios(struct tty_struct *tty)
{
	tty_encode_baud_rate(tty, 38400, 38400);
}

static void oti6858_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct oti6858_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	unsigned int cflag;
	u8 frame_fmt, control;
	__le16 divisor;
	int br;

	cflag = tty->termios.c_cflag;

	spin_lock_irqsave(&priv->lock, flags);
	divisor = priv->pending_setup.divisor;
	frame_fmt = priv->pending_setup.frame_fmt;
	control = priv->pending_setup.control;
	spin_unlock_irqrestore(&priv->lock, flags);

	frame_fmt &= ~FMT_DATA_BITS_MASK;
	switch (cflag & CSIZE) {
	case CS5:
		frame_fmt |= FMT_DATA_BITS_5;
		break;
	case CS6:
		frame_fmt |= FMT_DATA_BITS_6;
		break;
	case CS7:
		frame_fmt |= FMT_DATA_BITS_7;
		break;
	default:
	case CS8:
		frame_fmt |= FMT_DATA_BITS_8;
		break;
	}

	/* manufacturer claims that this device can work with baud rates
	 * up to 3 Mbps; I've tested it only on 115200 bps, so I can't
	 * guarantee that any other baud rate will work (especially
	 * the higher ones)
	 */
	br = tty_get_baud_rate(tty);
	if (br == 0) {
		divisor = 0;
	} else {
		int real_br;
		int new_divisor;
		br = min(br, OTI6858_MAX_BAUD_RATE);

		new_divisor = (96000000 + 8 * br) / (16 * br);
		real_br = 96000000 / (16 * new_divisor);
		divisor = cpu_to_le16(new_divisor);
		tty_encode_baud_rate(tty, real_br, real_br);
	}

	frame_fmt &= ~FMT_STOP_BITS_MASK;
	if ((cflag & CSTOPB) != 0)
		frame_fmt |= FMT_STOP_BITS_2;
	else
		frame_fmt |= FMT_STOP_BITS_1;

	frame_fmt &= ~FMT_PARITY_MASK;
	if ((cflag & PARENB) != 0) {
		if ((cflag & PARODD) != 0)
			frame_fmt |= FMT_PARITY_ODD;
		else
			frame_fmt |= FMT_PARITY_EVEN;
	} else {
		frame_fmt |= FMT_PARITY_NONE;
	}

	control &= ~CONTROL_MASK;
	if ((cflag & CRTSCTS) != 0)
		control |= (CONTROL_DTR_HIGH | CONTROL_RTS_HIGH);

	/* change control lines if we are switching to or from B0 */
	/* FIXME:
	spin_lock_irqsave(&priv->lock, flags);
	control = priv->line_control;
	if ((cflag & CBAUD) == B0)
		priv->line_control &= ~(CONTROL_DTR | CONTROL_RTS);
	else
		priv->line_control |= (CONTROL_DTR | CONTROL_RTS);
	if (control != priv->line_control) {
		control = priv->line_control;
		spin_unlock_irqrestore(&priv->lock, flags);
		set_control_lines(serial->dev, control);
	} else {
		spin_unlock_irqrestore(&priv->lock, flags);
	}
	*/

	spin_lock_irqsave(&priv->lock, flags);
	if (divisor != priv->pending_setup.divisor
			|| control != priv->pending_setup.control
			|| frame_fmt != priv->pending_setup.frame_fmt) {
		priv->pending_setup.divisor = divisor;
		priv->pending_setup.control = control;
		priv->pending_setup.frame_fmt = frame_fmt;
	}
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int oti6858_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct oti6858_private *priv = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	struct oti6858_control_pkt *buf;
	unsigned long flags;
	int result;

	usb_clear_halt(serial->dev, port->write_urb->pipe);
	usb_clear_halt(serial->dev, port->read_urb->pipe);

	buf = kmalloc(OTI6858_CTRL_PKT_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				OTI6858_REQ_T_GET_STATUS,
				OTI6858_REQ_GET_STATUS,
				0, 0,
				buf, OTI6858_CTRL_PKT_SIZE,
				100);
	if (result != OTI6858_CTRL_PKT_SIZE) {
		/* assume default (after power-on reset) values */
		buf->divisor = cpu_to_le16(0x009c);	/* 38400 bps */
		buf->frame_fmt = 0x03;	/* 8N1 */
		buf->something = 0x43;
		buf->control = 0x4c;	/* DTR, RTS */
		buf->tx_status = 0x00;
		buf->pin_state = 0x5b;	/* RTS, CTS, DSR, DTR, RI, DCD */
		buf->rx_bytes_avail = 0x00;
	}

	spin_lock_irqsave(&priv->lock, flags);
	memcpy(&priv->status, buf, OTI6858_CTRL_PKT_SIZE);
	priv->pending_setup.divisor = buf->divisor;
	priv->pending_setup.frame_fmt = buf->frame_fmt;
	priv->pending_setup.control = buf->control;
	spin_unlock_irqrestore(&priv->lock, flags);
	kfree(buf);

	dev_dbg(&port->dev, "%s(): submitting interrupt urb\n", __func__);
	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (result != 0) {
		dev_err(&port->dev, "%s(): usb_submit_urb() failed with error %d\n",
			__func__, result);
		oti6858_close(port);
		return result;
	}

	/* setup termios */
	if (tty)
		oti6858_set_termios(tty, port, NULL);

	return 0;
}

static void oti6858_close(struct usb_serial_port *port)
{
	struct oti6858_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	/* clear out any remaining data in the buffer */
	kfifo_reset_out(&port->write_fifo);
	spin_unlock_irqrestore(&port->lock, flags);

	dev_dbg(&port->dev, "%s(): after buf_clear()\n", __func__);

	/* cancel scheduled setup */
	cancel_delayed_work_sync(&priv->delayed_setup_work);
	cancel_delayed_work_sync(&priv->delayed_write_work);

	/* shutdown our urbs */
	dev_dbg(&port->dev, "%s(): shutting down urbs\n", __func__);
	usb_kill_urb(port->write_urb);
	usb_kill_urb(port->read_urb);
	usb_kill_urb(port->interrupt_in_urb);
}

static int oti6858_tiocmset(struct tty_struct *tty,
				unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct oti6858_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	u8 control;

	dev_dbg(&port->dev, "%s(set = 0x%08x, clear = 0x%08x)\n",
		__func__, set, clear);

	/* FIXME: check if this is correct (active high/low) */
	spin_lock_irqsave(&priv->lock, flags);
	control = priv->pending_setup.control;
	if ((set & TIOCM_RTS) != 0)
		control |= CONTROL_RTS_HIGH;
	if ((set & TIOCM_DTR) != 0)
		control |= CONTROL_DTR_HIGH;
	if ((clear & TIOCM_RTS) != 0)
		control &= ~CONTROL_RTS_HIGH;
	if ((clear & TIOCM_DTR) != 0)
		control &= ~CONTROL_DTR_HIGH;

	if (control != priv->pending_setup.control)
		priv->pending_setup.control = control;

	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
}

static int oti6858_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct oti6858_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	unsigned pin_state;
	unsigned result = 0;

	spin_lock_irqsave(&priv->lock, flags);
	pin_state = priv->status.pin_state & PIN_MASK;
	spin_unlock_irqrestore(&priv->lock, flags);

	/* FIXME: check if this is correct (active high/low) */
	if ((pin_state & PIN_RTS) != 0)
		result |= TIOCM_RTS;
	if ((pin_state & PIN_CTS) != 0)
		result |= TIOCM_CTS;
	if ((pin_state & PIN_DSR) != 0)
		result |= TIOCM_DSR;
	if ((pin_state & PIN_DTR) != 0)
		result |= TIOCM_DTR;
	if ((pin_state & PIN_RI) != 0)
		result |= TIOCM_RI;
	if ((pin_state & PIN_DCD) != 0)
		result |= TIOCM_CD;

	dev_dbg(&port->dev, "%s() = 0x%08x\n", __func__, result);

	return result;
}

static void oti6858_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port =  urb->context;
	struct oti6858_private *priv = usb_get_serial_port_data(port);
	int transient = 0, can_recv = 0, resubmit = 1;
	int status = urb->status;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&urb->dev->dev, "%s(): urb shutting down with status: %d\n",
			__func__, status);
		return;
	default:
		dev_dbg(&urb->dev->dev, "%s(): nonzero urb status received: %d\n",
			__func__, status);
		break;
	}

	if (status == 0 && urb->actual_length == OTI6858_CTRL_PKT_SIZE) {
		struct oti6858_control_pkt *xs = urb->transfer_buffer;
		unsigned long flags;

		spin_lock_irqsave(&priv->lock, flags);

		if (!priv->transient) {
			if (!OTI6858_CTRL_EQUALS_PENDING(xs, priv)) {
				if (xs->rx_bytes_avail == 0) {
					priv->transient = 4;
					priv->setup_done = 0;
					resubmit = 0;
					dev_dbg(&port->dev, "%s(): scheduling setup_line()\n", __func__);
					schedule_delayed_work(&priv->delayed_setup_work, 0);
				}
			}
		} else {
			if (OTI6858_CTRL_EQUALS_PENDING(xs, priv)) {
				priv->transient = 0;
			} else if (!priv->setup_done) {
				resubmit = 0;
			} else if (--priv->transient == 0) {
				if (xs->rx_bytes_avail == 0) {
					priv->transient = 4;
					priv->setup_done = 0;
					resubmit = 0;
					dev_dbg(&port->dev, "%s(): scheduling setup_line()\n", __func__);
					schedule_delayed_work(&priv->delayed_setup_work, 0);
				}
			}
		}

		if (!priv->transient) {
			u8 delta = xs->pin_state ^ priv->status.pin_state;

			if (delta & PIN_MSR_MASK) {
				if (delta & PIN_CTS)
					port->icount.cts++;
				if (delta & PIN_DSR)
					port->icount.dsr++;
				if (delta & PIN_RI)
					port->icount.rng++;
				if (delta & PIN_DCD)
					port->icount.dcd++;

				wake_up_interruptible(&port->port.delta_msr_wait);
			}

			memcpy(&priv->status, xs, OTI6858_CTRL_PKT_SIZE);
		}

		if (!priv->transient && xs->rx_bytes_avail != 0) {
			can_recv = xs->rx_bytes_avail;
			priv->flags.read_urb_in_use = 1;
		}

		transient = priv->transient;
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	if (can_recv) {
		int result;

		result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
		if (result != 0) {
			priv->flags.read_urb_in_use = 0;
			dev_err(&port->dev, "%s(): usb_submit_urb() failed,"
					" error %d\n", __func__, result);
		} else {
			resubmit = 0;
		}
	} else if (!transient) {
		unsigned long flags;
		int count;

		spin_lock_irqsave(&port->lock, flags);
		count = kfifo_len(&port->write_fifo);
		spin_unlock_irqrestore(&port->lock, flags);

		spin_lock_irqsave(&priv->lock, flags);
		if (priv->flags.write_urb_in_use == 0 && count != 0) {
			schedule_delayed_work(&priv->delayed_write_work, 0);
			resubmit = 0;
		}
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	if (resubmit) {
		int result;

/*		dev_dbg(&urb->dev->dev, "%s(): submitting interrupt urb\n", __func__); */
		result = usb_submit_urb(urb, GFP_ATOMIC);
		if (result != 0) {
			dev_err(&urb->dev->dev,
					"%s(): usb_submit_urb() failed with"
					" error %d\n", __func__, result);
		}
	}
}

static void oti6858_read_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port =  urb->context;
	struct oti6858_private *priv = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	unsigned long flags;
	int status = urb->status;
	int result;

	spin_lock_irqsave(&priv->lock, flags);
	priv->flags.read_urb_in_use = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (status != 0) {
		dev_dbg(&urb->dev->dev, "%s(): unable to handle the error, exiting\n", __func__);
		return;
	}

	if (urb->actual_length > 0) {
		tty_insert_flip_string(&port->port, data, urb->actual_length);
		tty_flip_buffer_push(&port->port);
	}

	/* schedule the interrupt urb */
	result = usb_submit_urb(port->interrupt_in_urb, GFP_ATOMIC);
	if (result != 0 && result != -EPERM) {
		dev_err(&port->dev, "%s(): usb_submit_urb() failed,"
				" error %d\n", __func__, result);
	}
}

static void oti6858_write_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port =  urb->context;
	struct oti6858_private *priv = usb_get_serial_port_data(port);
	int status = urb->status;
	int result;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&urb->dev->dev, "%s(): urb shutting down with status: %d\n", __func__, status);
		priv->flags.write_urb_in_use = 0;
		return;
	default:
		/* error in the urb, so we have to resubmit it */
		dev_dbg(&urb->dev->dev, "%s(): nonzero write bulk status received: %d\n", __func__, status);
		dev_dbg(&urb->dev->dev, "%s(): overflow in write\n", __func__);

		port->write_urb->transfer_buffer_length = 1;
		result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
		if (result) {
			dev_err_console(port, "%s(): usb_submit_urb() failed,"
					" error %d\n", __func__, result);
		} else {
			return;
		}
	}

	priv->flags.write_urb_in_use = 0;

	/* schedule the interrupt urb if we are still open */
	dev_dbg(&port->dev, "%s(): submitting interrupt urb\n", __func__);
	result = usb_submit_urb(port->interrupt_in_urb, GFP_ATOMIC);
	if (result != 0) {
		dev_err(&port->dev, "%s(): failed submitting int urb,"
					" error %d\n", __func__, result);
	}
}

module_usb_serial_driver(serial_drivers, id_table);

MODULE_DESCRIPTION(OTI6858_DESCRIPTION);
MODULE_AUTHOR(OTI6858_AUTHOR);
MODULE_LICENSE("GPL v2");
