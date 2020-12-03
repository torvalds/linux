// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2007, Frank A Kingswood <frank@kingswood-consulting.co.uk>
 * Copyright 2007, Werner Cornelius <werner@cornelius-consult.de>
 * Copyright 2009, Boris Hajduk <boris@hajduk.org>
 *
 * ch341.c implements a serial port driver for the Winchiphead CH341.
 *
 * The CH341 device can be used to implement an RS232 asynchronous
 * serial port, an IEEE-1284 parallel printer port or a memory-like
 * interface. In all cases the CH341 supports an I2C interface as well.
 * This driver only supports the asynchronous serial interface.
 */

#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial.h>
#include <asm/unaligned.h>

#define DEFAULT_BAUD_RATE 9600
#define DEFAULT_TIMEOUT   1000

/* flags for IO-Bits */
#define CH341_BIT_RTS (1 << 6)
#define CH341_BIT_DTR (1 << 5)

/******************************/
/* interrupt pipe definitions */
/******************************/
/* always 4 interrupt bytes */
/* first irq byte normally 0x08 */
/* second irq byte base 0x7d + below */
/* third irq byte base 0x94 + below */
/* fourth irq byte normally 0xee */

/* second interrupt byte */
#define CH341_MULT_STAT 0x04 /* multiple status since last interrupt event */

/* status returned in third interrupt answer byte, inverted in data
   from irq */
#define CH341_BIT_CTS 0x01
#define CH341_BIT_DSR 0x02
#define CH341_BIT_RI  0x04
#define CH341_BIT_DCD 0x08
#define CH341_BITS_MODEM_STAT 0x0f /* all bits */

/*******************************/
/* baudrate calculation factor */
/*******************************/
#define CH341_BAUDBASE_FACTOR 1532620800
#define CH341_BAUDBASE_DIVMAX 3

/* Break support - the information used to implement this was gleaned from
 * the Net/FreeBSD uchcom.c driver by Takanori Watanabe.  Domo arigato.
 */

#define CH341_REQ_READ_VERSION 0x5F
#define CH341_REQ_WRITE_REG    0x9A
#define CH341_REQ_READ_REG     0x95
#define CH341_REQ_SERIAL_INIT  0xA1
#define CH341_REQ_MODEM_CTRL   0xA4

#define CH341_REG_BREAK        0x05
#define CH341_REG_LCR          0x18
#define CH341_NBREAK_BITS      0x01

#define CH341_LCR_ENABLE_RX    0x80
#define CH341_LCR_ENABLE_TX    0x40
#define CH341_LCR_MARK_SPACE   0x20
#define CH341_LCR_PAR_EVEN     0x10
#define CH341_LCR_ENABLE_PAR   0x08
#define CH341_LCR_STOP_BITS_2  0x04
#define CH341_LCR_CS8          0x03
#define CH341_LCR_CS7          0x02
#define CH341_LCR_CS6          0x01
#define CH341_LCR_CS5          0x00

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x1a86, 0x5512) },
	{ USB_DEVICE(0x1a86, 0x5523) },
	{ USB_DEVICE(0x1a86, 0x7522) },
	{ USB_DEVICE(0x1a86, 0x7523) },
	{ USB_DEVICE(0x4348, 0x5523) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

struct ch341_private {
	spinlock_t lock; /* access lock */
	unsigned baud_rate; /* set baud rate */
	u8 mcr;
	u8 msr;
	u8 lcr;
};

static void ch341_set_termios(struct tty_struct *tty,
			      struct usb_serial_port *port,
			      struct ktermios *old_termios);

static int ch341_control_out(struct usb_device *dev, u8 request,
			     u16 value, u16 index)
{
	int r;

	dev_dbg(&dev->dev, "%s - (%02x,%04x,%04x)\n", __func__,
		request, value, index);

	r = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), request,
			    USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			    value, index, NULL, 0, DEFAULT_TIMEOUT);
	if (r < 0)
		dev_err(&dev->dev, "failed to send control message: %d\n", r);

	return r;
}

static int ch341_control_in(struct usb_device *dev,
			    u8 request, u16 value, u16 index,
			    char *buf, unsigned bufsize)
{
	int r;

	dev_dbg(&dev->dev, "%s - (%02x,%04x,%04x,%u)\n", __func__,
		request, value, index, bufsize);

	r = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), request,
			    USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			    value, index, buf, bufsize, DEFAULT_TIMEOUT);
	if (r < (int)bufsize) {
		if (r >= 0) {
			dev_err(&dev->dev,
				"short control message received (%d < %u)\n",
				r, bufsize);
			r = -EIO;
		}

		dev_err(&dev->dev, "failed to receive control message: %d\n",
			r);
		return r;
	}

	return 0;
}

static int ch341_set_baudrate_lcr(struct usb_device *dev,
				  struct ch341_private *priv, u8 lcr)
{
	short a;
	int r;
	unsigned long factor;
	short divisor;

	if (!priv->baud_rate)
		return -EINVAL;
	factor = (CH341_BAUDBASE_FACTOR / priv->baud_rate);
	divisor = CH341_BAUDBASE_DIVMAX;

	while ((factor > 0xfff0) && divisor) {
		factor >>= 3;
		divisor--;
	}

	if (factor > 0xfff0)
		return -EINVAL;

	factor = 0x10000 - factor;
	a = (factor & 0xff00) | divisor;

	/*
	 * CH341A buffers data until a full endpoint-size packet (32 bytes)
	 * has been received unless bit 7 is set.
	 */
	a |= BIT(7);

	r = ch341_control_out(dev, CH341_REQ_WRITE_REG, 0x1312, a);
	if (r)
		return r;

	r = ch341_control_out(dev, CH341_REQ_WRITE_REG, 0x2518, lcr);
	if (r)
		return r;

	return r;
}

static int ch341_set_handshake(struct usb_device *dev, u8 control)
{
	return ch341_control_out(dev, CH341_REQ_MODEM_CTRL, ~control, 0);
}

static int ch341_get_status(struct usb_device *dev, struct ch341_private *priv)
{
	const unsigned int size = 2;
	char *buffer;
	int r;
	unsigned long flags;

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	r = ch341_control_in(dev, CH341_REQ_READ_REG, 0x0706, 0, buffer, size);
	if (r < 0)
		goto out;

	spin_lock_irqsave(&priv->lock, flags);
	priv->msr = (~(*buffer)) & CH341_BITS_MODEM_STAT;
	spin_unlock_irqrestore(&priv->lock, flags);

out:	kfree(buffer);
	return r;
}

/* -------------------------------------------------------------------------- */

static int ch341_configure(struct usb_device *dev, struct ch341_private *priv)
{
	const unsigned int size = 2;
	char *buffer;
	int r;

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	/* expect two bytes 0x27 0x00 */
	r = ch341_control_in(dev, CH341_REQ_READ_VERSION, 0, 0, buffer, size);
	if (r < 0)
		goto out;
	dev_dbg(&dev->dev, "Chip version: 0x%02x\n", buffer[0]);

	r = ch341_control_out(dev, CH341_REQ_SERIAL_INIT, 0, 0);
	if (r < 0)
		goto out;

	r = ch341_set_baudrate_lcr(dev, priv, priv->lcr);
	if (r < 0)
		goto out;

	r = ch341_set_handshake(dev, priv->mcr);

out:	kfree(buffer);
	return r;
}

static int ch341_port_probe(struct usb_serial_port *port)
{
	struct ch341_private *priv;
	int r;

	priv = kzalloc(sizeof(struct ch341_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	priv->baud_rate = DEFAULT_BAUD_RATE;
	/*
	 * Some CH340 devices appear unable to change the initial LCR
	 * settings, so set a sane 8N1 default.
	 */
	priv->lcr = CH341_LCR_ENABLE_RX | CH341_LCR_ENABLE_TX | CH341_LCR_CS8;

	r = ch341_configure(port->serial->dev, priv);
	if (r < 0)
		goto error;

	usb_set_serial_port_data(port, priv);
	return 0;

error:	kfree(priv);
	return r;
}

static int ch341_port_remove(struct usb_serial_port *port)
{
	struct ch341_private *priv;

	priv = usb_get_serial_port_data(port);
	kfree(priv);

	return 0;
}

static int ch341_carrier_raised(struct usb_serial_port *port)
{
	struct ch341_private *priv = usb_get_serial_port_data(port);
	if (priv->msr & CH341_BIT_DCD)
		return 1;
	return 0;
}

static void ch341_dtr_rts(struct usb_serial_port *port, int on)
{
	struct ch341_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	/* drop DTR and RTS */
	spin_lock_irqsave(&priv->lock, flags);
	if (on)
		priv->mcr |= CH341_BIT_RTS | CH341_BIT_DTR;
	else
		priv->mcr &= ~(CH341_BIT_RTS | CH341_BIT_DTR);
	spin_unlock_irqrestore(&priv->lock, flags);
	ch341_set_handshake(port->serial->dev, priv->mcr);
}

static void ch341_close(struct usb_serial_port *port)
{
	usb_serial_generic_close(port);
	usb_kill_urb(port->interrupt_in_urb);
}


/* open this device, set default parameters */
static int ch341_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct ch341_private *priv = usb_get_serial_port_data(port);
	int r;

	if (tty)
		ch341_set_termios(tty, port, NULL);

	dev_dbg(&port->dev, "%s - submitting interrupt urb\n", __func__);
	r = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (r) {
		dev_err(&port->dev, "%s - failed to submit interrupt urb: %d\n",
			__func__, r);
		return r;
	}

	r = ch341_get_status(port->serial->dev, priv);
	if (r < 0) {
		dev_err(&port->dev, "failed to read modem status: %d\n", r);
		goto err_kill_interrupt_urb;
	}

	r = usb_serial_generic_open(tty, port);
	if (r)
		goto err_kill_interrupt_urb;

	return 0;

err_kill_interrupt_urb:
	usb_kill_urb(port->interrupt_in_urb);

	return r;
}

/* Old_termios contains the original termios settings and
 * tty->termios contains the new setting to be used.
 */
static void ch341_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct ch341_private *priv = usb_get_serial_port_data(port);
	unsigned baud_rate;
	unsigned long flags;
	u8 lcr;
	int r;

	/* redundant changes may cause the chip to lose bytes */
	if (old_termios && !tty_termios_hw_change(&tty->termios, old_termios))
		return;

	baud_rate = tty_get_baud_rate(tty);

	lcr = CH341_LCR_ENABLE_RX | CH341_LCR_ENABLE_TX;

	switch (C_CSIZE(tty)) {
	case CS5:
		lcr |= CH341_LCR_CS5;
		break;
	case CS6:
		lcr |= CH341_LCR_CS6;
		break;
	case CS7:
		lcr |= CH341_LCR_CS7;
		break;
	case CS8:
		lcr |= CH341_LCR_CS8;
		break;
	}

	if (C_PARENB(tty)) {
		lcr |= CH341_LCR_ENABLE_PAR;
		if (C_PARODD(tty) == 0)
			lcr |= CH341_LCR_PAR_EVEN;
		if (C_CMSPAR(tty))
			lcr |= CH341_LCR_MARK_SPACE;
	}

	if (C_CSTOPB(tty))
		lcr |= CH341_LCR_STOP_BITS_2;

	if (baud_rate) {
		priv->baud_rate = baud_rate;

		r = ch341_set_baudrate_lcr(port->serial->dev, priv, lcr);
		if (r < 0 && old_termios) {
			priv->baud_rate = tty_termios_baud_rate(old_termios);
			tty_termios_copy_hw(&tty->termios, old_termios);
		} else if (r == 0) {
			priv->lcr = lcr;
		}
	}

	spin_lock_irqsave(&priv->lock, flags);
	if (C_BAUD(tty) == B0)
		priv->mcr &= ~(CH341_BIT_DTR | CH341_BIT_RTS);
	else if (old_termios && (old_termios->c_cflag & CBAUD) == B0)
		priv->mcr |= (CH341_BIT_DTR | CH341_BIT_RTS);
	spin_unlock_irqrestore(&priv->lock, flags);

	ch341_set_handshake(port->serial->dev, priv->mcr);
}

static void ch341_break_ctl(struct tty_struct *tty, int break_state)
{
	const uint16_t ch341_break_reg =
			((uint16_t) CH341_REG_LCR << 8) | CH341_REG_BREAK;
	struct usb_serial_port *port = tty->driver_data;
	int r;
	uint16_t reg_contents;
	uint8_t *break_reg;

	break_reg = kmalloc(2, GFP_KERNEL);
	if (!break_reg)
		return;

	r = ch341_control_in(port->serial->dev, CH341_REQ_READ_REG,
			ch341_break_reg, 0, break_reg, 2);
	if (r < 0) {
		dev_err(&port->dev, "%s - USB control read error (%d)\n",
				__func__, r);
		goto out;
	}
	dev_dbg(&port->dev, "%s - initial ch341 break register contents - reg1: %x, reg2: %x\n",
		__func__, break_reg[0], break_reg[1]);
	if (break_state != 0) {
		dev_dbg(&port->dev, "%s - Enter break state requested\n", __func__);
		break_reg[0] &= ~CH341_NBREAK_BITS;
		break_reg[1] &= ~CH341_LCR_ENABLE_TX;
	} else {
		dev_dbg(&port->dev, "%s - Leave break state requested\n", __func__);
		break_reg[0] |= CH341_NBREAK_BITS;
		break_reg[1] |= CH341_LCR_ENABLE_TX;
	}
	dev_dbg(&port->dev, "%s - New ch341 break register contents - reg1: %x, reg2: %x\n",
		__func__, break_reg[0], break_reg[1]);
	reg_contents = get_unaligned_le16(break_reg);
	r = ch341_control_out(port->serial->dev, CH341_REQ_WRITE_REG,
			ch341_break_reg, reg_contents);
	if (r < 0)
		dev_err(&port->dev, "%s - USB control write error (%d)\n",
				__func__, r);
out:
	kfree(break_reg);
}

static int ch341_tiocmset(struct tty_struct *tty,
			  unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ch341_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	u8 control;

	spin_lock_irqsave(&priv->lock, flags);
	if (set & TIOCM_RTS)
		priv->mcr |= CH341_BIT_RTS;
	if (set & TIOCM_DTR)
		priv->mcr |= CH341_BIT_DTR;
	if (clear & TIOCM_RTS)
		priv->mcr &= ~CH341_BIT_RTS;
	if (clear & TIOCM_DTR)
		priv->mcr &= ~CH341_BIT_DTR;
	control = priv->mcr;
	spin_unlock_irqrestore(&priv->lock, flags);

	return ch341_set_handshake(port->serial->dev, control);
}

static void ch341_update_status(struct usb_serial_port *port,
					unsigned char *data, size_t len)
{
	struct ch341_private *priv = usb_get_serial_port_data(port);
	struct tty_struct *tty;
	unsigned long flags;
	u8 status;
	u8 delta;

	if (len < 4)
		return;

	status = ~data[2] & CH341_BITS_MODEM_STAT;

	spin_lock_irqsave(&priv->lock, flags);
	delta = status ^ priv->msr;
	priv->msr = status;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (data[1] & CH341_MULT_STAT)
		dev_dbg(&port->dev, "%s - multiple status change\n", __func__);

	if (!delta)
		return;

	if (delta & CH341_BIT_CTS)
		port->icount.cts++;
	if (delta & CH341_BIT_DSR)
		port->icount.dsr++;
	if (delta & CH341_BIT_RI)
		port->icount.rng++;
	if (delta & CH341_BIT_DCD) {
		port->icount.dcd++;
		tty = tty_port_tty_get(&port->port);
		if (tty) {
			usb_serial_handle_dcd_change(port, tty,
						status & CH341_BIT_DCD);
			tty_kref_put(tty);
		}
	}

	wake_up_interruptible(&port->port.delta_msr_wait);
}

static void ch341_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	unsigned int len = urb->actual_length;
	int status;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&urb->dev->dev, "%s - urb shutting down: %d\n",
			__func__, urb->status);
		return;
	default:
		dev_dbg(&urb->dev->dev, "%s - nonzero urb status: %d\n",
			__func__, urb->status);
		goto exit;
	}

	usb_serial_debug_data(&port->dev, __func__, len, data);
	ch341_update_status(port, data, len);
exit:
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		dev_err(&urb->dev->dev, "%s - usb_submit_urb failed: %d\n",
			__func__, status);
	}
}

static int ch341_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ch341_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	u8 mcr;
	u8 status;
	unsigned int result;

	spin_lock_irqsave(&priv->lock, flags);
	mcr = priv->mcr;
	status = priv->msr;
	spin_unlock_irqrestore(&priv->lock, flags);

	result = ((mcr & CH341_BIT_DTR)		? TIOCM_DTR : 0)
		  | ((mcr & CH341_BIT_RTS)	? TIOCM_RTS : 0)
		  | ((status & CH341_BIT_CTS)	? TIOCM_CTS : 0)
		  | ((status & CH341_BIT_DSR)	? TIOCM_DSR : 0)
		  | ((status & CH341_BIT_RI)	? TIOCM_RI  : 0)
		  | ((status & CH341_BIT_DCD)	? TIOCM_CD  : 0);

	dev_dbg(&port->dev, "%s - result = %x\n", __func__, result);

	return result;
}

static int ch341_reset_resume(struct usb_serial *serial)
{
	struct usb_serial_port *port = serial->port[0];
	struct ch341_private *priv;
	int ret;

	priv = usb_get_serial_port_data(port);
	if (!priv)
		return 0;

	/* reconfigure ch341 serial port after bus-reset */
	ch341_configure(serial->dev, priv);

	if (tty_port_initialized(&port->port)) {
		ret = usb_submit_urb(port->interrupt_in_urb, GFP_NOIO);
		if (ret) {
			dev_err(&port->dev, "failed to submit interrupt urb: %d\n",
				ret);
			return ret;
		}

		ret = ch341_get_status(port->serial->dev, priv);
		if (ret < 0) {
			dev_err(&port->dev, "failed to read modem status: %d\n",
				ret);
		}
	}

	return usb_serial_generic_resume(serial);
}

static struct usb_serial_driver ch341_device = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "ch341-uart",
	},
	.id_table          = id_table,
	.num_ports         = 1,
	.open              = ch341_open,
	.dtr_rts	   = ch341_dtr_rts,
	.carrier_raised	   = ch341_carrier_raised,
	.close             = ch341_close,
	.set_termios       = ch341_set_termios,
	.break_ctl         = ch341_break_ctl,
	.tiocmget          = ch341_tiocmget,
	.tiocmset          = ch341_tiocmset,
	.tiocmiwait        = usb_serial_generic_tiocmiwait,
	.read_int_callback = ch341_read_int_callback,
	.port_probe        = ch341_port_probe,
	.port_remove       = ch341_port_remove,
	.reset_resume      = ch341_reset_resume,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&ch341_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_LICENSE("GPL v2");
