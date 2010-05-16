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
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
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

static int debug;
/*
 * Version information
 */

#define DRIVER_VERSION "v0.5"
#define DRIVER_AUTHOR "Bart Hartgers <bart.hartgers+ark3116@gmail.com>"
#define DRIVER_DESC "USB ARK3116 serial/IrDA driver"
#define DRIVER_DEV_DESC "ARK3116 RS232/IrDA"
#define DRIVER_NAME "ark3116"

/* usb timeout of 1 second */
#define ARK_TIMEOUT (1*HZ)

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
	wait_queue_head_t       delta_msr_wait;
	struct async_icount	icount;
	int			irda;	/* 1 for irda device */

	/* protects hw register updates */
	struct mutex		hw_lock;

	int			quot;	/* baudrate divisor */
	__u32			lcr;	/* line control register value */
	__u32			hcr;	/* handshake control register (0x8)
					 * value */
	__u32			mcr;	/* modem contol register value */

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
	return result;
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
	if (result < 0)
		return result;
	else
		return buf[0];
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

static int ark3116_attach(struct usb_serial *serial)
{
	struct usb_serial_port *port = serial->port[0];
	struct ark3116_private *priv;

	/* make sure we have our end-points */
	if ((serial->num_bulk_in == 0) ||
	    (serial->num_bulk_out == 0) ||
	    (serial->num_interrupt_in == 0)) {
		dev_err(&serial->dev->dev,
			"%s - missing endpoint - "
			"bulk in: %d, bulk out: %d, int in %d\n",
			KBUILD_MODNAME,
			serial->num_bulk_in,
			serial->num_bulk_out,
			serial->num_interrupt_in);
		return -EINVAL;
	}

	priv = kzalloc(sizeof(struct ark3116_private),
		       GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	init_waitqueue_head(&priv->delta_msr_wait);
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

	dev_info(&serial->dev->dev,
		"%s using %s mode\n",
		KBUILD_MODNAME,
		priv->irda ? "IrDA" : "RS232");
	return 0;
}

static void ark3116_release(struct usb_serial *serial)
{
	struct usb_serial_port *port = serial->port[0];
	struct ark3116_private *priv = usb_get_serial_port_data(port);

	/* device is closed, so URBs and DMA should be down */

	usb_set_serial_port_data(port, NULL);

	mutex_destroy(&priv->hw_lock);

	kfree(priv);
}

static void ark3116_init_termios(struct tty_struct *tty)
{
	struct ktermios *termios = tty->termios;
	*termios = tty_std_termios;
	termios->c_cflag = B9600 | CS8
				      | CREAD | HUPCL | CLOCAL;
	termios->c_ispeed = 9600;
	termios->c_ospeed = 9600;
}

static void ark3116_set_termios(struct tty_struct *tty,
				struct usb_serial_port *port,
				struct ktermios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	struct ktermios *termios = tty->termios;
	unsigned int cflag = termios->c_cflag;
	int bps = tty_get_baud_rate(tty);
	int quot;
	__u8 lcr, hcr, eval;

	/* set data bit count */
	switch (cflag & CSIZE) {
	case CS5:
		lcr = UART_LCR_WLEN5;
		break;
	case CS6:
		lcr = UART_LCR_WLEN6;
		break;
	case CS7:
		lcr = UART_LCR_WLEN7;
		break;
	default:
	case CS8:
		lcr = UART_LCR_WLEN8;
		break;
	}
	if (cflag & CSTOPB)
		lcr |= UART_LCR_STOP;
	if (cflag & PARENB)
		lcr |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		lcr |= UART_LCR_EPAR;
#ifdef CMSPAR
	if (cflag & CMSPAR)
		lcr |= UART_LCR_SPAR;
#endif
	/* handshake control */
	hcr = (cflag & CRTSCTS) ? 0x03 : 0x00;

	/* calc baudrate */
	dbg("%s - setting bps to %d", __func__, bps);
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

	dbg("%s - setting hcr:0x%02x,lcr:0x%02x,quot:%d",
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
		dev_warn(&serial->dev->dev,
			 "%s: don't know how to do software flow control\n",
			 KBUILD_MODNAME);
	}

	/* Don't rewrite B0 */
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, bps, bps);
}

static void ark3116_close(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;

	if (serial->dev) {
		/* disable DMA */
		ark3116_write_reg(serial, UART_FCR, 0);

		/* deactivate interrupts */
		ark3116_write_reg(serial, UART_IER, 0);

		/* shutdown any bulk reads that might be going on */
		if (serial->num_bulk_out)
			usb_kill_urb(port->write_urb);
		if (serial->num_bulk_in)
			usb_kill_urb(port->read_urb);
		if (serial->num_interrupt_in)
			usb_kill_urb(port->interrupt_in_urb);
	}
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
		dbg("%s - usb_serial_generic_open failed: %d",
		    __func__, result);
		goto err_out;
	}

	/* setup termios */
	if (tty)
		ark3116_set_termios(tty, port, NULL);

	/* remove any data still left: also clears error state */
	ark3116_read_reg(serial, UART_RX, buf);

	/* read modem status */
	priv->msr = ark3116_read_reg(serial, UART_MSR, buf);
	/* read line status */
	priv->lsr = ark3116_read_reg(serial, UART_LSR, buf);

	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (result) {
		dev_err(&port->dev, "submit irq_in urb failed %d\n",
			result);
		ark3116_close(port);
		goto err_out;
	}

	/* activate interrupts */
	ark3116_write_reg(port->serial, UART_IER, UART_IER_MSI|UART_IER_RLSI);

	/* enable DMA */
	ark3116_write_reg(port->serial, UART_FCR, UART_FCR_DMA_SELECT);

err_out:
	kfree(buf);
	return result;
}

static int ark3116_ioctl(struct tty_struct *tty, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	struct serial_struct serstruct;
	void __user *user_arg = (void __user *)arg;

	switch (cmd) {
	case TIOCGSERIAL:
		/* XXX: Some of these values are probably wrong. */
		memset(&serstruct, 0, sizeof(serstruct));
		serstruct.type = PORT_16654;
		serstruct.line = port->serial->minor;
		serstruct.port = port->number;
		serstruct.custom_divisor = 0;
		serstruct.baud_base = 460800;

		if (copy_to_user(user_arg, &serstruct, sizeof(serstruct)))
			return -EFAULT;

		return 0;
	case TIOCSSERIAL:
		if (copy_from_user(&serstruct, user_arg, sizeof(serstruct)))
			return -EFAULT;
		return 0;
	case TIOCMIWAIT:
		for (;;) {
			struct async_icount prev = priv->icount;
			interruptible_sleep_on(&priv->delta_msr_wait);
			/* see if a signal did it */
			if (signal_pending(current))
				return -ERESTARTSYS;
			if ((prev.rng == priv->icount.rng) &&
			    (prev.dsr == priv->icount.dsr) &&
			    (prev.dcd == priv->icount.dcd) &&
			    (prev.cts == priv->icount.cts))
				return -EIO;
			if ((arg & TIOCM_RNG &&
			     (prev.rng != priv->icount.rng)) ||
			    (arg & TIOCM_DSR &&
			     (prev.dsr != priv->icount.dsr)) ||
			    (arg & TIOCM_CD  &&
			     (prev.dcd != priv->icount.dcd)) ||
			    (arg & TIOCM_CTS &&
			     (prev.cts != priv->icount.cts)))
				return 0;
		}
		break;
	case TIOCGICOUNT: {
		struct serial_icounter_struct icount;
		struct async_icount cnow = priv->icount;
		memset(&icount, 0, sizeof(icount));
		icount.cts = cnow.cts;
		icount.dsr = cnow.dsr;
		icount.rng = cnow.rng;
		icount.dcd = cnow.dcd;
		icount.rx = cnow.rx;
		icount.tx = cnow.tx;
		icount.frame = cnow.frame;
		icount.overrun = cnow.overrun;
		icount.parity = cnow.parity;
		icount.brk = cnow.brk;
		icount.buf_overrun = cnow.buf_overrun;
		if (copy_to_user(user_arg, &icount, sizeof(icount)))
			return -EFAULT;
		return 0;
	}
	}

	return -ENOIOCTLCMD;
}

static int ark3116_tiocmget(struct tty_struct *tty, struct file *file)
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

static int ark3116_tiocmset(struct tty_struct *tty, struct file *file,
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
			priv->icount.cts++;
		if (msr & UART_MSR_DDSR)
			priv->icount.dsr++;
		if (msr & UART_MSR_DDCD)
			priv->icount.dcd++;
		if (msr & UART_MSR_TERI)
			priv->icount.rng++;
		wake_up_interruptible(&priv->delta_msr_wait);
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
			priv->icount.brk++;
		if (lsr & UART_LSR_FE)
			priv->icount.frame++;
		if (lsr & UART_LSR_PE)
			priv->icount.parity++;
		if (lsr & UART_LSR_OE)
			priv->icount.overrun++;
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
		dbg("%s - urb shutting down with status: %d",
		    __func__, status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d",
		    __func__, status);
		break;
	case 0: /* success */
		/* discovered this by trail and error... */
		if ((urb->actual_length == 4) && (data[0] == 0xe8)) {
			const __u8 id = data[1]&UART_IIR_ID;
			dbg("%s: iir=%02x", __func__, data[1]);
			if (id == UART_IIR_MSI) {
				dbg("%s: msr=%02x", __func__, data[3]);
				ark3116_update_msr(port, data[3]);
				break;
			} else if (id == UART_IIR_RLSI) {
				dbg("%s: lsr=%02x", __func__, data[2]);
				ark3116_update_lsr(port, data[2]);
				break;
			}
		}
		/*
		 * Not sure what this data meant...
		 */
		usb_serial_debug_data(debug, &port->dev,
				      __func__,
				      urb->actual_length,
				      urb->transfer_buffer);
		break;
	}

	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result)
		dev_err(&urb->dev->dev,
			"%s - Error %d submitting interrupt urb\n",
			__func__, result);
}


/* Data comes in via the bulk (data) URB, erors/interrupts via the int URB.
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

static void send_to_tty(struct tty_struct *tty,
			const unsigned char *chars,
			size_t size, char flag)
{
	if (size == 0)
		return;
	if (flag == TTY_NORMAL) {
		tty_insert_flip_string(tty, chars, size);
	} else {
		int i;
		for (i = 0; i < size; ++i)
			tty_insert_flip_char(tty, chars[i], flag);
	}
}

static void ark3116_read_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port =  urb->context;
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	const __u8 *data = urb->transfer_buffer;
	int status = urb->status;
	struct tty_struct *tty;
	unsigned long flags;
	int result;
	char flag;
	__u32 lsr;

	switch (status) {
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d",
		    __func__, status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d",
		    __func__, status);
		break;
	case 0: /* success */

		spin_lock_irqsave(&priv->status_lock, flags);
		lsr = priv->lsr;
		/* clear error bits */
		priv->lsr &= ~UART_LSR_BRK_ERROR_BITS;
		spin_unlock_irqrestore(&priv->status_lock, flags);

		if (unlikely(lsr & UART_LSR_BI))
			flag = TTY_BREAK;
		else if (unlikely(lsr & UART_LSR_PE))
			flag = TTY_PARITY;
		else if (unlikely(lsr & UART_LSR_FE))
			flag = TTY_FRAME;
		else
			flag = TTY_NORMAL;

		tty = tty_port_tty_get(&port->port);
		if (tty) {
			/* overrun is special, not associated with a char */
			if (unlikely(lsr & UART_LSR_OE))
				tty_insert_flip_char(tty, 0, TTY_OVERRUN);
			send_to_tty(tty, data, urb->actual_length, flag);
			tty_flip_buffer_push(tty);
			tty_kref_put(tty);
		}

		/* Throttle the device if requested by tty */
		spin_lock_irqsave(&port->lock, flags);
		port->throttled = port->throttle_req;
		if (port->throttled) {
			spin_unlock_irqrestore(&port->lock, flags);
			return;
		} else
			spin_unlock_irqrestore(&port->lock, flags);
	}
	/* Continue reading from device */
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result)
		dev_err(&urb->dev->dev, "%s - failed resubmitting"
			" read urb, error %d\n", __func__, result);
}

static struct usb_driver ark3116_driver = {
	.name =		"ark3116",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id =	1,
};

static struct usb_serial_driver ark3116_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"ark3116",
	},
	.id_table =		id_table,
	.usb_driver =		&ark3116_driver,
	.num_ports =		1,
	.attach =		ark3116_attach,
	.release =		ark3116_release,
	.set_termios =		ark3116_set_termios,
	.init_termios =		ark3116_init_termios,
	.ioctl =		ark3116_ioctl,
	.tiocmget =		ark3116_tiocmget,
	.tiocmset =		ark3116_tiocmset,
	.open =			ark3116_open,
	.close =		ark3116_close,
	.break_ctl = 		ark3116_break_ctl,
	.read_int_callback = 	ark3116_read_int_callback,
	.read_bulk_callback =	ark3116_read_bulk_callback,
};

static int __init ark3116_init(void)
{
	int retval;

	retval = usb_serial_register(&ark3116_device);
	if (retval)
		return retval;
	retval = usb_register(&ark3116_driver);
	if (retval == 0) {
		printk(KERN_INFO "%s:"
		       DRIVER_VERSION ":"
		       DRIVER_DESC "\n",
		       KBUILD_MODNAME);
	} else
		usb_serial_deregister(&ark3116_device);
	return retval;
}

static void __exit ark3116_exit(void)
{
	usb_deregister(&ark3116_driver);
	usb_serial_deregister(&ark3116_device);
}

module_init(ark3116_init);
module_exit(ark3116_exit);
MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debug");

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
 * (http://lkml.org/lkml/2009/7/26/56), the ARK3116 can also be used
 * as an IrDA dongle. Since I do not have such a thing, I could not
 * investigate that aspect. However, I can speculate ;-).
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
