// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2009 by Bart Hartgers (bart.hartgers+ark3116@gmail.com)
 * Original version:
 * Copyright (C) 2006
 *   Simon Schulz (ark3116_driver <at> auctionant.de)
 *
 * ark3116
 * - implements a driver for the arkmicro ark3116 chipset (vendor=0x6547,
 *   productid=0x0232) (used in a datacable called KQ-U8A)
 *
 * Supports full modem status lines, break, hardware flow control. Does not
 * support software flow control, since I do not know how to enable it in hw.
 *
 * This driver is a essentially new implementation. I initially dug
 * into the old ark3116.c driver and suddenly realized the ark3116 is
 * a 16450 with a USB interface glued to it. See comments at the
 * bottom of this file.
 */

#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#define DRIVER_AUTHOR "Bart Hartgers <bart.hartgers+ark3116@gmail.com>"
#define DRIVER_DESC "USB ARK3116 serial/IrDA driver"
#define DRIVER_DEV_DESC "ARK3116 RS232/IrDA"
#define DRIVER_NAME "ark3116"

/* usb timeout of 1 second */
#define ARK_TIMEOUT 1000

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x6547, 0x0232) },
	{ USB_DEVICE(0x18ec, 0x3118) },		/* USB to IrDA adapter */
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

static int is_irda(struct usb_serial *serial)
{
	struct usb_device *dev = serial->dev;
	if (le16_to_cpu(dev->descriptor.idVendor) == 0x18ec &&
			le16_to_cpu(dev->descriptor.idProduct) == 0x3118)
		return 1;
	return 0;
}

struct ark3116_private {
	int			irda;	/* 1 for irda device */

	/* protects hw register updates */
	struct mutex		hw_lock;

	int			quot;	/* baudrate divisor */
	__u32			lcr;	/* line control register value */
	__u32			hcr;	/* handshake control register (0x8)
					 * value */
	__u32			mcr;	/* modem control register value */

	/* protects the status values below */
	spinlock_t		status_lock;
	__u32			msr;	/* modem status register value */
	__u32			lsr;	/* line status register value */
};

static int ark3116_write_reg(struct usb_serial *serial,
			     unsigned reg, __u8 val)
{
	int result;
	 /* 0xfe 0x40 are magic values taken from original driver */
	result = usb_control_msg(serial->dev,
				 usb_sndctrlpipe(serial->dev, 0),
				 0xfe, 0x40, val, reg,
				 NULL, 0, ARK_TIMEOUT);
	if (result)
		return result;

	return 0;
}

static int ark3116_read_reg(struct usb_serial *serial,
			    unsigned reg, unsigned char *buf)
{
	int result;
	/* 0xfe 0xc0 are magic values taken from original driver */
	result = usb_control_msg(serial->dev,
				 usb_rcvctrlpipe(serial->dev, 0),
				 0xfe, 0xc0, 0, reg,
				 buf, 1, ARK_TIMEOUT);
	if (result < 1) {
		dev_err(&serial->interface->dev,
				"failed to read register %u: %d\n",
				reg, result);
		if (result >= 0)
			result = -EIO;

		return result;
	}

	return 0;
}

static inline int calc_divisor(int bps)
{
	/* Original ark3116 made some exceptions in rounding here
	 * because windows did the same. Assume that is not really
	 * necessary.
	 * Crystal is 12MHz, probably because of USB, but we divide by 4?
	 */
	return (12000000 + 2*bps) / (4*bps);
}

static int ark3116_port_probe(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct ark3116_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->hw_lock);
	spin_lock_init(&priv->status_lock);

	priv->irda = is_irda(serial);

	usb_set_serial_port_data(port, priv);

	/* setup the hardware */
	ark3116_write_reg(serial, UART_IER, 0);
	/* disable DMA */
	ark3116_write_reg(serial, UART_FCR, 0);
	/* handshake control */
	priv->hcr = 0;
	ark3116_write_reg(serial, 0x8     , 0);
	/* modem control */
	priv->mcr = 0;
	ark3116_write_reg(serial, UART_MCR, 0);

	if (!(priv->irda)) {
		ark3116_write_reg(serial, 0xb , 0);
	} else {
		ark3116_write_reg(serial, 0xb , 1);
		ark3116_write_reg(serial, 0xc , 0);
		ark3116_write_reg(serial, 0xd , 0x41);
		ark3116_write_reg(serial, 0xa , 1);
	}

	/* setup baudrate */
	ark3116_write_reg(serial, UART_LCR, UART_LCR_DLAB);

	/* setup for 9600 8N1 */
	priv->quot = calc_divisor(9600);
	ark3116_write_reg(serial, UART_DLL, priv->quot & 0xff);
	ark3116_write_reg(serial, UART_DLM, (priv->quot>>8) & 0xff);

	priv->lcr = UART_LCR_WLEN8;
	ark3116_write_reg(serial, UART_LCR, UART_LCR_WLEN8);

	ark3116_write_reg(serial, 0xe, 0);

	if (priv->irda)
		ark3116_write_reg(serial, 0x9, 0);

	dev_info(&port->dev, "using %s mode\n", priv->irda ? "IrDA" : "RS232");

	return 0;
}

static void ark3116_port_remove(struct usb_serial_port *port)
{
	struct ark3116_private *priv = usb_get_serial_port_data(port);

	/* device is closed, so URBs and DMA should be down */
	mutex_destroy(&priv->hw_lock);
	kfree(priv);
}

static void ark3116_set_termios(struct tty_struct *tty,
				struct usb_serial_port *port,
				const struct ktermios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	struct ktermios *termios = &tty->termios;
	unsigned int cflag = termios->c_cflag;
	int bps = tty_get_baud_rate(tty);
	int quot;
	__u8 lcr, hcr, eval;

	/* set data bit count */
	lcr = UART_LCR_WLEN(tty_get_char_size(cflag));

	if (cflag & CSTOPB)
		lcr |= UART_LCR_STOP;
	if (cflag & PARENB)
		lcr |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		lcr |= UART_LCR_EPAR;
	if (cflag & CMSPAR)
		lcr |= UART_LCR_SPAR;

	/* handshake control */
	hcr = (cflag & CRTSCTS) ? 0x03 : 0x00;

	/* calc baudrate */
	dev_dbg(&port->dev, "%s - setting bps to %d\n", __func__, bps);
	eval = 0;
	switch (bps) {
	case 0:
		quot = calc_divisor(9600);
		break;
	default:
		if ((bps < 75) || (bps > 3000000))
			bps = 9600;
		quot = calc_divisor(bps);
		break;
	case 460800:
		eval = 1;
		quot = calc_divisor(bps);
		break;
	case 921600:
		eval = 2;
		quot = calc_divisor(bps);
		break;
	}

	/* Update state: synchronize */
	mutex_lock(&priv->hw_lock);

	/* keep old LCR_SBC bit */
	lcr |= (priv->lcr & UART_LCR_SBC);

	dev_dbg(&port->dev, "%s - setting hcr:0x%02x,lcr:0x%02x,quot:%d\n",
		__func__, hcr, lcr, quot);

	/* handshake control */
	if (priv->hcr != hcr) {
		priv->hcr = hcr;
		ark3116_write_reg(serial, 0x8, hcr);
	}

	/* baudrate */
	if (priv->quot != quot) {
		priv->quot = quot;
		priv->lcr = lcr; /* need to write lcr anyway */

		/* disable DMA since transmit/receive is
		 * shadowed by UART_DLL
		 */
		ark3116_write_reg(serial, UART_FCR, 0);

		ark3116_write_reg(serial, UART_LCR,
				  lcr|UART_LCR_DLAB);
		ark3116_write_reg(serial, UART_DLL, quot & 0xff);
		ark3116_write_reg(serial, UART_DLM, (quot>>8) & 0xff);

		/* restore lcr */
		ark3116_write_reg(serial, UART_LCR, lcr);
		/* magic baudrate thingy: not sure what it does,
		 * but windows does this as well.
		 */
		ark3116_write_reg(serial, 0xe, eval);

		/* enable DMA */
		ark3116_write_reg(serial, UART_FCR, UART_FCR_DMA_SELECT);
	} else if (priv->lcr != lcr) {
		priv->lcr = lcr;
		ark3116_write_reg(serial, UART_LCR, lcr);
	}

	mutex_unlock(&priv->hw_lock);

	/* check for software flow control */
	if (I_IXOFF(tty) || I_IXON(tty)) {
		dev_warn(&port->dev,
				"software flow control not implemented\n");
	}

	/* Don't rewrite B0 */
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, bps, bps);
}

static void ark3116_close(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;

	/* disable DMA */
	ark3116_write_reg(serial, UART_FCR, 0);

	/* deactivate interrupts */
	ark3116_write_reg(serial, UART_IER, 0);

	usb_serial_generic_close(port);

	usb_kill_urb(port->interrupt_in_urb);
}

static int ark3116_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	unsigned char *buf;
	int result;

	buf = kmalloc(1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	result = usb_serial_generic_open(tty, port);
	if (result) {
		dev_dbg(&port->dev,
			"%s - usb_serial_generic_open failed: %d\n",
			__func__, result);
		goto err_free;
	}

	/* remove any data still left: also clears error state */
	ark3116_read_reg(serial, UART_RX, buf);

	/* read modem status */
	result = ark3116_read_reg(serial, UART_MSR, buf);
	if (result)
		goto err_close;
	priv->msr = *buf;

	/* read line status */
	result = ark3116_read_reg(serial, UART_LSR, buf);
	if (result)
		goto err_close;
	priv->lsr = *buf;

	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (result) {
		dev_err(&port->dev, "submit irq_in urb failed %d\n",
			result);
		goto err_close;
	}

	/* activate interrupts */
	ark3116_write_reg(port->serial, UART_IER, UART_IER_MSI|UART_IER_RLSI);

	/* enable DMA */
	ark3116_write_reg(port->serial, UART_FCR, UART_FCR_DMA_SELECT);

	/* setup termios */
	if (tty)
		ark3116_set_termios(tty, port, NULL);

	kfree(buf);

	return 0;

err_close:
	usb_serial_generic_close(port);
err_free:
	kfree(buf);

	return result;
}

static int ark3116_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	__u32 status;
	__u32 ctrl;
	unsigned long flags;

	mutex_lock(&priv->hw_lock);
	ctrl = priv->mcr;
	mutex_unlock(&priv->hw_lock);

	spin_lock_irqsave(&priv->status_lock, flags);
	status = priv->msr;
	spin_unlock_irqrestore(&priv->status_lock, flags);

	return  (status & UART_MSR_DSR  ? TIOCM_DSR  : 0) |
		(status & UART_MSR_CTS  ? TIOCM_CTS  : 0) |
		(status & UART_MSR_RI   ? TIOCM_RI   : 0) |
		(status & UART_MSR_DCD  ? TIOCM_CD   : 0) |
		(ctrl   & UART_MCR_DTR  ? TIOCM_DTR  : 0) |
		(ctrl   & UART_MCR_RTS  ? TIOCM_RTS  : 0) |
		(ctrl   & UART_MCR_OUT1 ? TIOCM_OUT1 : 0) |
		(ctrl   & UART_MCR_OUT2 ? TIOCM_OUT2 : 0);
}

static int ark3116_tiocmset(struct tty_struct *tty,
			unsigned set, unsigned clr)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ark3116_private *priv = usb_get_serial_port_data(port);

	/* we need to take the mutex here, to make sure that the value
	 * in priv->mcr is actually the one that is in the hardware
	 */

	mutex_lock(&priv->hw_lock);

	if (set & TIOCM_RTS)
		priv->mcr |= UART_MCR_RTS;
	if (set & TIOCM_DTR)
		priv->mcr |= UART_MCR_DTR;
	if (set & TIOCM_OUT1)
		priv->mcr |= UART_MCR_OUT1;
	if (set & TIOCM_OUT2)
		priv->mcr |= UART_MCR_OUT2;
	if (clr & TIOCM_RTS)
		priv->mcr &= ~UART_MCR_RTS;
	if (clr & TIOCM_DTR)
		priv->mcr &= ~UART_MCR_DTR;
	if (clr & TIOCM_OUT1)
		priv->mcr &= ~UART_MCR_OUT1;
	if (clr & TIOCM_OUT2)
		priv->mcr &= ~UART_MCR_OUT2;

	ark3116_write_reg(port->serial, UART_MCR, priv->mcr);

	mutex_unlock(&priv->hw_lock);

	return 0;
}

static void ark3116_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ark3116_private *priv = usb_get_serial_port_data(port);

	/* LCR is also used for other things: protect access */
	mutex_lock(&priv->hw_lock);

	if (break_state)
		priv->lcr |= UART_LCR_SBC;
	else
		priv->lcr &= ~UART_LCR_SBC;

	ark3116_write_reg(port->serial, UART_LCR, priv->lcr);

	mutex_unlock(&priv->hw_lock);
}

static void ark3116_update_msr(struct usb_serial_port *port, __u8 msr)
{
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	spin_lock_irqsave(&priv->status_lock, flags);
	priv->msr = msr;
	spin_unlock_irqrestore(&priv->status_lock, flags);

	if (msr & UART_MSR_ANY_DELTA) {
		/* update input line counters */
		if (msr & UART_MSR_DCTS)
			port->icount.cts++;
		if (msr & UART_MSR_DDSR)
			port->icount.dsr++;
		if (msr & UART_MSR_DDCD)
			port->icount.dcd++;
		if (msr & UART_MSR_TERI)
			port->icount.rng++;
		wake_up_interruptible(&port->port.delta_msr_wait);
	}
}

static void ark3116_update_lsr(struct usb_serial_port *port, __u8 lsr)
{
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	spin_lock_irqsave(&priv->status_lock, flags);
	/* combine bits */
	priv->lsr |= lsr;
	spin_unlock_irqrestore(&priv->status_lock, flags);

	if (lsr&UART_LSR_BRK_ERROR_BITS) {
		if (lsr & UART_LSR_BI)
			port->icount.brk++;
		if (lsr & UART_LSR_FE)
			port->icount.frame++;
		if (lsr & UART_LSR_PE)
			port->icount.parity++;
		if (lsr & UART_LSR_OE)
			port->icount.overrun++;
	}
}

static void ark3116_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int status = urb->status;
	const __u8 *data = urb->transfer_buffer;
	int result;

	switch (status) {
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
		break;
	case 0: /* success */
		/* discovered this by trail and error... */
		if ((urb->actual_length == 4) && (data[0] == 0xe8)) {
			const __u8 id = data[1]&UART_IIR_ID;
			dev_dbg(&port->dev, "%s: iir=%02x\n", __func__, data[1]);
			if (id == UART_IIR_MSI) {
				dev_dbg(&port->dev, "%s: msr=%02x\n",
					__func__, data[3]);
				ark3116_update_msr(port, data[3]);
				break;
			} else if (id == UART_IIR_RLSI) {
				dev_dbg(&port->dev, "%s: lsr=%02x\n",
					__func__, data[2]);
				ark3116_update_lsr(port, data[2]);
				break;
			}
		}
		/*
		 * Not sure what this data meant...
		 */
		usb_serial_debug_data(&port->dev, __func__,
				      urb->actual_length,
				      urb->transfer_buffer);
		break;
	}

	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result)
		dev_err(&port->dev, "failed to resubmit interrupt urb: %d\n",
			result);
}


/* Data comes in via the bulk (data) URB, errors/interrupts via the int URB.
 * This means that we cannot be sure which data byte has an associated error
 * condition, so we report an error for all data in the next bulk read.
 *
 * Actually, there might even be a window between the bulk data leaving the
 * ark and reading/resetting the lsr in the read_bulk_callback where an
 * interrupt for the next data block could come in.
 * Without somekind of ordering on the ark, we would have to report the
 * error for the next block of data as well...
 * For now, let's pretend this can't happen.
 */
static void ark3116_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	char tty_flag = TTY_NORMAL;
	unsigned long flags;
	__u32 lsr;

	/* update line status */
	spin_lock_irqsave(&priv->status_lock, flags);
	lsr = priv->lsr;
	priv->lsr &= ~UART_LSR_BRK_ERROR_BITS;
	spin_unlock_irqrestore(&priv->status_lock, flags);

	if (!urb->actual_length)
		return;

	if (lsr & UART_LSR_BRK_ERROR_BITS) {
		if (lsr & UART_LSR_BI)
			tty_flag = TTY_BREAK;
		else if (lsr & UART_LSR_PE)
			tty_flag = TTY_PARITY;
		else if (lsr & UART_LSR_FE)
			tty_flag = TTY_FRAME;

		/* overrun is special, not associated with a char */
		if (lsr & UART_LSR_OE)
			tty_insert_flip_char(&port->port, 0, TTY_OVERRUN);
	}
	tty_insert_flip_string_fixed_flag(&port->port, data, tty_flag,
							urb->actual_length);
	tty_flip_buffer_push(&port->port);
}

static struct usb_serial_driver ark3116_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"ark3116",
	},
	.id_table =		id_table,
	.num_ports =		1,
	.num_bulk_in =		1,
	.num_bulk_out =		1,
	.num_interrupt_in =	1,
	.port_probe =		ark3116_port_probe,
	.port_remove =		ark3116_port_remove,
	.set_termios =		ark3116_set_termios,
	.tiocmget =		ark3116_tiocmget,
	.tiocmset =		ark3116_tiocmset,
	.tiocmiwait =		usb_serial_generic_tiocmiwait,
	.get_icount =		usb_serial_generic_get_icount,
	.open =			ark3116_open,
	.close =		ark3116_close,
	.break_ctl = 		ark3116_break_ctl,
	.read_int_callback = 	ark3116_read_int_callback,
	.process_read_urb =	ark3116_process_read_urb,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&ark3116_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

/*
 * The following describes what I learned from studying the old
 * ark3116.c driver, disassembling the windows driver, and some lucky
 * guesses. Since I do not have any datasheet or other
 * documentation, inaccuracies are almost guaranteed.
 *
 * Some specs for the ARK3116 can be found here:
 * http://web.archive.org/web/20060318000438/
 *   www.arkmicro.com/en/products/view.php?id=10
 * On that page, 2 GPIO pins are mentioned: I assume these are the
 * OUT1 and OUT2 pins of the UART, so I added support for those
 * through the MCR. Since the pins are not available on my hardware,
 * I could not verify this.
 * Also, it states there is "on-chip hardware flow control". I have
 * discovered how to enable that. Unfortunately, I do not know how to
 * enable XON/XOFF (software) flow control, which would need support
 * from the chip as well to work. Because of the wording on the web
 * page there is a real possibility the chip simply does not support
 * software flow control.
 *
 * I got my ark3116 as part of a mobile phone adapter cable. On the
 * PCB, the following numbered contacts are present:
 *
 *  1:- +5V
 *  2:o DTR
 *  3:i RX
 *  4:i DCD
 *  5:o RTS
 *  6:o TX
 *  7:i RI
 *  8:i DSR
 * 10:- 0V
 * 11:i CTS
 *
 * On my chip, all signals seem to be 3.3V, but 5V tolerant. But that
 * may be different for the one you have ;-).
 *
 * The windows driver limits the registers to 0-F, so I assume there
 * are actually 16 present on the device.
 *
 * On an UART interrupt, 4 bytes of data come in on the interrupt
 * endpoint. The bytes are 0xe8 IIR LSR MSR.
 *
 * The baudrate seems to be generated from the 12MHz crystal, using
 * 4-times subsampling. So quot=12e6/(4*baud). Also see description
 * of register E.
 *
 * Registers 0-7:
 * These seem to be the same as for a regular 16450. The FCR is set
 * to UART_FCR_DMA_SELECT (0x8), I guess to enable transfers between
 * the UART and the USB bridge/DMA engine.
 *
 * Register 8:
 * By trial and error, I found out that bit 0 enables hardware CTS,
 * stopping TX when CTS is +5V. Bit 1 does the same for RTS, making
 * RTS +5V when the 3116 cannot transfer the data to the USB bus
 * (verified by disabling the reading URB). Note that as far as I can
 * tell, the windows driver does NOT use this, so there might be some
 * hardware bug or something.
 *
 * According to a patch provided here
 * https://lore.kernel.org/lkml/200907261419.50702.linux@rainbow-software.org
 * the ARK3116 can also be used as an IrDA dongle. Since I do not have
 * such a thing, I could not investigate that aspect. However, I can
 * speculate ;-).
 *
 * - IrDA encodes data differently than RS232. Most likely, one of
 *   the bits in registers 9..E enables the IR ENDEC (encoder/decoder).
 * - Depending on the IR transceiver, the input and output need to be
 *   inverted, so there are probably bits for that as well.
 * - IrDA is half-duplex, so there should be a bit for selecting that.
 *
 * This still leaves at least two registers unaccounted for. Perhaps
 * The chip can do XON/XOFF or CRC in HW?
 *
 * Register 9:
 * Set to 0x00 for IrDA, when the baudrate is initialised.
 *
 * Register A:
 * Set to 0x01 for IrDA, at init.
 *
 * Register B:
 * Set to 0x01 for IrDA, 0x00 for RS232, at init.
 *
 * Register C:
 * Set to 00 for IrDA, at init.
 *
 * Register D:
 * Set to 0x41 for IrDA, at init.
 *
 * Register E:
 * Somekind of baudrate override. The windows driver seems to set
 * this to 0x00 for normal baudrates, 0x01 for 460800, 0x02 for 921600.
 * Since 460800 and 921600 cannot be obtained by dividing 3MHz by an integer,
 * it could be somekind of subdivisor thingy.
 * However,it does not seem to do anything: selecting 921600 (divisor 3,
 * reg E=2), still gets 1 MHz. I also checked if registers 9, C or F would
 * work, but they don't.
 *
 * Register F: unknown
 */
