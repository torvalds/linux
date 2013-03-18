/*
 * MCT (Magic Control Technology Corp.) USB RS232 Converter Driver
 *
 *   Copyright (C) 2000 Wolfgang Grandegger (wolfgang@ces.ch)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 * This program is largely derived from the Belkin USB Serial Adapter Driver
 * (see belkin_sa.[ch]). All of the information about the device was acquired
 * by using SniffUSB on Windows98. For technical details see mct_u232.h.
 *
 * William G. Greathouse and Greg Kroah-Hartman provided great help on how to
 * do the reverse engineering and how to write a USB serial device driver.
 *
 * TO BE DONE, TO BE CHECKED:
 *   DTR/RTS signal handling may be incomplete or incorrect. I have mainly
 *   implemented what I have seen with SniffUSB or found in belkin_sa.c.
 *   For further TODOs check also belkin_sa.c.
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
#include <asm/unaligned.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial.h>
#include <linux/ioctl.h>
#include "mct_u232.h"

#define DRIVER_AUTHOR "Wolfgang Grandegger <wolfgang@ces.ch>"
#define DRIVER_DESC "Magic Control Technology USB-RS232 converter driver"

/*
 * Function prototypes
 */
static int  mct_u232_startup(struct usb_serial *serial);
static int  mct_u232_port_probe(struct usb_serial_port *port);
static int  mct_u232_port_remove(struct usb_serial_port *remove);
static int  mct_u232_open(struct tty_struct *tty, struct usb_serial_port *port);
static void mct_u232_close(struct usb_serial_port *port);
static void mct_u232_dtr_rts(struct usb_serial_port *port, int on);
static void mct_u232_read_int_callback(struct urb *urb);
static void mct_u232_set_termios(struct tty_struct *tty,
			struct usb_serial_port *port, struct ktermios *old);
static void mct_u232_break_ctl(struct tty_struct *tty, int break_state);
static int  mct_u232_tiocmget(struct tty_struct *tty);
static int  mct_u232_tiocmset(struct tty_struct *tty,
			unsigned int set, unsigned int clear);
static int  mct_u232_ioctl(struct tty_struct *tty,
			unsigned int cmd, unsigned long arg);
static int  mct_u232_get_icount(struct tty_struct *tty,
			struct serial_icounter_struct *icount);
static void mct_u232_throttle(struct tty_struct *tty);
static void mct_u232_unthrottle(struct tty_struct *tty);


/*
 * All of the device info needed for the MCT USB-RS232 converter.
 */
static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(MCT_U232_VID, MCT_U232_PID) },
	{ USB_DEVICE(MCT_U232_VID, MCT_U232_SITECOM_PID) },
	{ USB_DEVICE(MCT_U232_VID, MCT_U232_DU_H3SP_PID) },
	{ USB_DEVICE(MCT_U232_BELKIN_F5U109_VID, MCT_U232_BELKIN_F5U109_PID) },
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_serial_driver mct_u232_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"mct_u232",
	},
	.description =	     "MCT U232",
	.id_table =	     id_table,
	.num_ports =	     1,
	.open =		     mct_u232_open,
	.close =	     mct_u232_close,
	.dtr_rts =	     mct_u232_dtr_rts,
	.throttle =	     mct_u232_throttle,
	.unthrottle =	     mct_u232_unthrottle,
	.read_int_callback = mct_u232_read_int_callback,
	.set_termios =	     mct_u232_set_termios,
	.break_ctl =	     mct_u232_break_ctl,
	.tiocmget =	     mct_u232_tiocmget,
	.tiocmset =	     mct_u232_tiocmset,
	.attach =	     mct_u232_startup,
	.port_probe =        mct_u232_port_probe,
	.port_remove =       mct_u232_port_remove,
	.ioctl =             mct_u232_ioctl,
	.get_icount =        mct_u232_get_icount,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&mct_u232_device, NULL
};

struct mct_u232_private {
	spinlock_t lock;
	unsigned int	     control_state; /* Modem Line Setting (TIOCM) */
	unsigned char        last_lcr;      /* Line Control Register */
	unsigned char	     last_lsr;      /* Line Status Register */
	unsigned char	     last_msr;      /* Modem Status Register */
	unsigned int	     rx_flags;      /* Throttling flags */
	struct async_icount  icount;
	wait_queue_head_t    msr_wait;	/* for handling sleeping while waiting
						for msr change to happen */
};

#define THROTTLED		0x01

/*
 * Handle vendor specific USB requests
 */

#define WDR_TIMEOUT 5000 /* default urb timeout */

/*
 * Later day 2.6.0-test kernels have new baud rates like B230400 which
 * we do not know how to support. We ignore them for the moment.
 */
static int mct_u232_calculate_baud_rate(struct usb_serial *serial,
					speed_t value, speed_t *result)
{
	*result = value;

	if (le16_to_cpu(serial->dev->descriptor.idProduct) == MCT_U232_SITECOM_PID
		|| le16_to_cpu(serial->dev->descriptor.idProduct) == MCT_U232_BELKIN_F5U109_PID) {
		switch (value) {
		case 300:
			return 0x01;
		case 600:
			return 0x02; /* this one not tested */
		case 1200:
			return 0x03;
		case 2400:
			return 0x04;
		case 4800:
			return 0x06;
		case 9600:
			return 0x08;
		case 19200:
			return 0x09;
		case 38400:
			return 0x0a;
		case 57600:
			return 0x0b;
		case 115200:
			return 0x0c;
		default:
			*result = 9600;
			return 0x08;
		}
	} else {
		/* FIXME: Can we use any divider - should we do
		   divider = 115200/value;
		   real baud = 115200/divider */
		switch (value) {
		case 300: break;
		case 600: break;
		case 1200: break;
		case 2400: break;
		case 4800: break;
		case 9600: break;
		case 19200: break;
		case 38400: break;
		case 57600: break;
		case 115200: break;
		default:
			value = 9600;
			*result = 9600;
		}
		return 115200/value;
	}
}

static int mct_u232_set_baud_rate(struct tty_struct *tty,
	struct usb_serial *serial, struct usb_serial_port *port, speed_t value)
{
	unsigned int divisor;
	int rc;
	unsigned char *buf;
	unsigned char cts_enable_byte = 0;
	speed_t speed;

	buf = kmalloc(MCT_U232_MAX_SIZE, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	divisor = mct_u232_calculate_baud_rate(serial, value, &speed);
	put_unaligned_le32(cpu_to_le32(divisor), buf);
	rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				MCT_U232_SET_BAUD_RATE_REQUEST,
				MCT_U232_SET_REQUEST_TYPE,
				0, 0, buf, MCT_U232_SET_BAUD_RATE_SIZE,
				WDR_TIMEOUT);
	if (rc < 0)	/*FIXME: What value speed results */
		dev_err(&port->dev, "Set BAUD RATE %d failed (error = %d)\n",
			value, rc);
	else
		tty_encode_baud_rate(tty, speed, speed);
	dev_dbg(&port->dev, "set_baud_rate: value: 0x%x, divisor: 0x%x\n", value, divisor);

	/* Mimic the MCT-supplied Windows driver (version 1.21P.0104), which
	   always sends two extra USB 'device request' messages after the
	   'baud rate change' message.  The actual functionality of the
	   request codes in these messages is not fully understood but these
	   particular codes are never seen in any operation besides a baud
	   rate change.  Both of these messages send a single byte of data.
	   In the first message, the value of this byte is always zero.

	   The second message has been determined experimentally to control
	   whether data will be transmitted to a device which is not asserting
	   the 'CTS' signal.  If the second message's data byte is zero, data
	   will be transmitted even if 'CTS' is not asserted (i.e. no hardware
	   flow control).  if the second message's data byte is nonzero (a
	   value of 1 is used by this driver), data will not be transmitted to
	   a device which is not asserting 'CTS'.
	*/

	buf[0] = 0;
	rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				MCT_U232_SET_UNKNOWN1_REQUEST,
				MCT_U232_SET_REQUEST_TYPE,
				0, 0, buf, MCT_U232_SET_UNKNOWN1_SIZE,
				WDR_TIMEOUT);
	if (rc < 0)
		dev_err(&port->dev, "Sending USB device request code %d "
			"failed (error = %d)\n", MCT_U232_SET_UNKNOWN1_REQUEST,
			rc);

	if (port && C_CRTSCTS(tty))
	   cts_enable_byte = 1;

	dev_dbg(&port->dev, "set_baud_rate: send second control message, data = %02X\n",
		cts_enable_byte);
	buf[0] = cts_enable_byte;
	rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			MCT_U232_SET_CTS_REQUEST,
			MCT_U232_SET_REQUEST_TYPE,
			0, 0, buf, MCT_U232_SET_CTS_SIZE,
			WDR_TIMEOUT);
	if (rc < 0)
		dev_err(&port->dev, "Sending USB device request code %d "
			"failed (error = %d)\n", MCT_U232_SET_CTS_REQUEST, rc);

	kfree(buf);
	return rc;
} /* mct_u232_set_baud_rate */

static int mct_u232_set_line_ctrl(struct usb_serial_port *port,
				  unsigned char lcr)
{
	int rc;
	unsigned char *buf;

	buf = kmalloc(MCT_U232_MAX_SIZE, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = lcr;
	rc = usb_control_msg(port->serial->dev, usb_sndctrlpipe(port->serial->dev, 0),
			MCT_U232_SET_LINE_CTRL_REQUEST,
			MCT_U232_SET_REQUEST_TYPE,
			0, 0, buf, MCT_U232_SET_LINE_CTRL_SIZE,
			WDR_TIMEOUT);
	if (rc < 0)
		dev_err(&port->dev, "Set LINE CTRL 0x%x failed (error = %d)\n", lcr, rc);
	dev_dbg(&port->dev, "set_line_ctrl: 0x%x\n", lcr);
	kfree(buf);
	return rc;
} /* mct_u232_set_line_ctrl */

static int mct_u232_set_modem_ctrl(struct usb_serial_port *port,
				   unsigned int control_state)
{
	int rc;
	unsigned char mcr;
	unsigned char *buf;

	buf = kmalloc(MCT_U232_MAX_SIZE, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	mcr = MCT_U232_MCR_NONE;
	if (control_state & TIOCM_DTR)
		mcr |= MCT_U232_MCR_DTR;
	if (control_state & TIOCM_RTS)
		mcr |= MCT_U232_MCR_RTS;

	buf[0] = mcr;
	rc = usb_control_msg(port->serial->dev, usb_sndctrlpipe(port->serial->dev, 0),
			MCT_U232_SET_MODEM_CTRL_REQUEST,
			MCT_U232_SET_REQUEST_TYPE,
			0, 0, buf, MCT_U232_SET_MODEM_CTRL_SIZE,
			WDR_TIMEOUT);
	kfree(buf);

	dev_dbg(&port->dev, "set_modem_ctrl: state=0x%x ==> mcr=0x%x\n", control_state, mcr);

	if (rc < 0) {
		dev_err(&port->dev, "Set MODEM CTRL 0x%x failed (error = %d)\n", mcr, rc);
		return rc;
	}
	return 0;
} /* mct_u232_set_modem_ctrl */

static int mct_u232_get_modem_stat(struct usb_serial_port *port,
				   unsigned char *msr)
{
	int rc;
	unsigned char *buf;

	buf = kmalloc(MCT_U232_MAX_SIZE, GFP_KERNEL);
	if (buf == NULL) {
		*msr = 0;
		return -ENOMEM;
	}
	rc = usb_control_msg(port->serial->dev, usb_rcvctrlpipe(port->serial->dev, 0),
			MCT_U232_GET_MODEM_STAT_REQUEST,
			MCT_U232_GET_REQUEST_TYPE,
			0, 0, buf, MCT_U232_GET_MODEM_STAT_SIZE,
			WDR_TIMEOUT);
	if (rc < 0) {
		dev_err(&port->dev, "Get MODEM STATus failed (error = %d)\n", rc);
		*msr = 0;
	} else {
		*msr = buf[0];
	}
	dev_dbg(&port->dev, "get_modem_stat: 0x%x\n", *msr);
	kfree(buf);
	return rc;
} /* mct_u232_get_modem_stat */

static void mct_u232_msr_to_icount(struct async_icount *icount,
						unsigned char msr)
{
	/* Translate Control Line states */
	if (msr & MCT_U232_MSR_DDSR)
		icount->dsr++;
	if (msr & MCT_U232_MSR_DCTS)
		icount->cts++;
	if (msr & MCT_U232_MSR_DRI)
		icount->rng++;
	if (msr & MCT_U232_MSR_DCD)
		icount->dcd++;
} /* mct_u232_msr_to_icount */

static void mct_u232_msr_to_state(struct usb_serial_port *port,
				  unsigned int *control_state, unsigned char msr)
{
	/* Translate Control Line states */
	if (msr & MCT_U232_MSR_DSR)
		*control_state |=  TIOCM_DSR;
	else
		*control_state &= ~TIOCM_DSR;
	if (msr & MCT_U232_MSR_CTS)
		*control_state |=  TIOCM_CTS;
	else
		*control_state &= ~TIOCM_CTS;
	if (msr & MCT_U232_MSR_RI)
		*control_state |=  TIOCM_RI;
	else
		*control_state &= ~TIOCM_RI;
	if (msr & MCT_U232_MSR_CD)
		*control_state |=  TIOCM_CD;
	else
		*control_state &= ~TIOCM_CD;
	dev_dbg(&port->dev, "msr_to_state: msr=0x%x ==> state=0x%x\n", msr, *control_state);
} /* mct_u232_msr_to_state */

/*
 * Driver's tty interface functions
 */

static int mct_u232_startup(struct usb_serial *serial)
{
	struct usb_serial_port *port, *rport;

	/* Puh, that's dirty */
	port = serial->port[0];
	rport = serial->port[1];
	/* No unlinking, it wasn't submitted yet. */
	usb_free_urb(port->read_urb);
	port->read_urb = rport->interrupt_in_urb;
	rport->interrupt_in_urb = NULL;
	port->read_urb->context = port;

	return 0;
} /* mct_u232_startup */

static int mct_u232_port_probe(struct usb_serial_port *port)
{
	struct mct_u232_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	init_waitqueue_head(&priv->msr_wait);

	usb_set_serial_port_data(port, priv);

	return 0;
}

static int mct_u232_port_remove(struct usb_serial_port *port)
{
	struct mct_u232_private *priv;

	priv = usb_get_serial_port_data(port);
	kfree(priv);

	return 0;
}

static int  mct_u232_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	int retval = 0;
	unsigned int control_state;
	unsigned long flags;
	unsigned char last_lcr;
	unsigned char last_msr;

	/* Compensate for a hardware bug: although the Sitecom U232-P25
	 * device reports a maximum output packet size of 32 bytes,
	 * it seems to be able to accept only 16 bytes (and that's what
	 * SniffUSB says too...)
	 */
	if (le16_to_cpu(serial->dev->descriptor.idProduct)
						== MCT_U232_SITECOM_PID)
		port->bulk_out_size = 16;

	/* Do a defined restart: the normal serial device seems to
	 * always turn on DTR and RTS here, so do the same. I'm not
	 * sure if this is really necessary. But it should not harm
	 * either.
	 */
	spin_lock_irqsave(&priv->lock, flags);
	if (tty && (tty->termios.c_cflag & CBAUD))
		priv->control_state = TIOCM_DTR | TIOCM_RTS;
	else
		priv->control_state = 0;

	priv->last_lcr = (MCT_U232_DATA_BITS_8 |
			  MCT_U232_PARITY_NONE |
			  MCT_U232_STOP_BITS_1);
	control_state = priv->control_state;
	last_lcr = priv->last_lcr;
	spin_unlock_irqrestore(&priv->lock, flags);
	mct_u232_set_modem_ctrl(port, control_state);
	mct_u232_set_line_ctrl(port, last_lcr);

	/* Read modem status and update control state */
	mct_u232_get_modem_stat(port, &last_msr);
	spin_lock_irqsave(&priv->lock, flags);
	priv->last_msr = last_msr;
	mct_u232_msr_to_state(port, &priv->control_state, priv->last_msr);
	spin_unlock_irqrestore(&priv->lock, flags);

	retval = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (retval) {
		dev_err(&port->dev,
			"usb_submit_urb(read bulk) failed pipe 0x%x err %d\n",
			port->read_urb->pipe, retval);
		goto error;
	}

	retval = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (retval) {
		usb_kill_urb(port->read_urb);
		dev_err(&port->dev,
			"usb_submit_urb(read int) failed pipe 0x%x err %d",
			port->interrupt_in_urb->pipe, retval);
		goto error;
	}
	return 0;

error:
	return retval;
} /* mct_u232_open */

static void mct_u232_dtr_rts(struct usb_serial_port *port, int on)
{
	unsigned int control_state;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);

	spin_lock_irq(&priv->lock);
	if (on)
		priv->control_state |= TIOCM_DTR | TIOCM_RTS;
	else
		priv->control_state &= ~(TIOCM_DTR | TIOCM_RTS);
	control_state = priv->control_state;
	spin_unlock_irq(&priv->lock);

	mct_u232_set_modem_ctrl(port, control_state);
}

static void mct_u232_close(struct usb_serial_port *port)
{
	/*
	 * Must kill the read urb as it is actually an interrupt urb, which
	 * generic close thus fails to kill.
	 */
	usb_kill_urb(port->read_urb);
	usb_kill_urb(port->interrupt_in_urb);

	usb_serial_generic_close(port);
} /* mct_u232_close */


static void mct_u232_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	int retval;
	int status = urb->status;
	unsigned long flags;

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

	usb_serial_debug_data(&port->dev, __func__, urb->actual_length, data);

	/*
	 * Work-a-round: handle the 'usual' bulk-in pipe here
	 */
	if (urb->transfer_buffer_length > 2) {
		if (urb->actual_length) {
			tty_insert_flip_string(&port->port, data,
					urb->actual_length);
			tty_flip_buffer_push(&port->port);
		}
		goto exit;
	}

	/*
	 * The interrupt-in pipe signals exceptional conditions (modem line
	 * signal changes and errors). data[0] holds MSR, data[1] holds LSR.
	 */
	spin_lock_irqsave(&priv->lock, flags);
	priv->last_msr = data[MCT_U232_MSR_INDEX];

	/* Record Control Line states */
	mct_u232_msr_to_state(port, &priv->control_state, priv->last_msr);

	mct_u232_msr_to_icount(&priv->icount, priv->last_msr);

#if 0
	/* Not yet handled. See belkin_sa.c for further information */
	/* Now to report any errors */
	priv->last_lsr = data[MCT_U232_LSR_INDEX];
	/*
	 * fill in the flip buffer here, but I do not know the relation
	 * to the current/next receive buffer or characters.  I need
	 * to look in to this before committing any code.
	 */
	if (priv->last_lsr & MCT_U232_LSR_ERR) {
		tty = tty_port_tty_get(&port->port);
		/* Overrun Error */
		if (priv->last_lsr & MCT_U232_LSR_OE) {
		}
		/* Parity Error */
		if (priv->last_lsr & MCT_U232_LSR_PE) {
		}
		/* Framing Error */
		if (priv->last_lsr & MCT_U232_LSR_FE) {
		}
		/* Break Indicator */
		if (priv->last_lsr & MCT_U232_LSR_BI) {
		}
		tty_kref_put(tty);
	}
#endif
	wake_up_interruptible(&priv->msr_wait);
	spin_unlock_irqrestore(&priv->lock, flags);
exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&port->dev,
			"%s - usb_submit_urb failed with result %d\n",
			__func__, retval);
} /* mct_u232_read_int_callback */

static void mct_u232_set_termios(struct tty_struct *tty,
				 struct usb_serial_port *port,
				 struct ktermios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	struct ktermios *termios = &tty->termios;
	unsigned int cflag = termios->c_cflag;
	unsigned int old_cflag = old_termios->c_cflag;
	unsigned long flags;
	unsigned int control_state;
	unsigned char last_lcr;

	/* get a local copy of the current port settings */
	spin_lock_irqsave(&priv->lock, flags);
	control_state = priv->control_state;
	spin_unlock_irqrestore(&priv->lock, flags);
	last_lcr = 0;

	/*
	 * Update baud rate.
	 * Do not attempt to cache old rates and skip settings,
	 * disconnects screw such tricks up completely.
	 * Premature optimization is the root of all evil.
	 */

	/* reassert DTR and RTS on transition from B0 */
	if ((old_cflag & CBAUD) == B0) {
		dev_dbg(&port->dev, "%s: baud was B0\n", __func__);
		control_state |= TIOCM_DTR | TIOCM_RTS;
		mct_u232_set_modem_ctrl(port, control_state);
	}

	mct_u232_set_baud_rate(tty, serial, port, tty_get_baud_rate(tty));

	if ((cflag & CBAUD) == B0) {
		dev_dbg(&port->dev, "%s: baud is B0\n", __func__);
		/* Drop RTS and DTR */
		control_state &= ~(TIOCM_DTR | TIOCM_RTS);
		mct_u232_set_modem_ctrl(port, control_state);
	}

	/*
	 * Update line control register (LCR)
	 */

	/* set the parity */
	if (cflag & PARENB)
		last_lcr |= (cflag & PARODD) ?
			MCT_U232_PARITY_ODD : MCT_U232_PARITY_EVEN;
	else
		last_lcr |= MCT_U232_PARITY_NONE;

	/* set the number of data bits */
	switch (cflag & CSIZE) {
	case CS5:
		last_lcr |= MCT_U232_DATA_BITS_5; break;
	case CS6:
		last_lcr |= MCT_U232_DATA_BITS_6; break;
	case CS7:
		last_lcr |= MCT_U232_DATA_BITS_7; break;
	case CS8:
		last_lcr |= MCT_U232_DATA_BITS_8; break;
	default:
		dev_err(&port->dev,
			"CSIZE was not CS5-CS8, using default of 8\n");
		last_lcr |= MCT_U232_DATA_BITS_8;
		break;
	}

	termios->c_cflag &= ~CMSPAR;

	/* set the number of stop bits */
	last_lcr |= (cflag & CSTOPB) ?
		MCT_U232_STOP_BITS_2 : MCT_U232_STOP_BITS_1;

	mct_u232_set_line_ctrl(port, last_lcr);

	/* save off the modified port settings */
	spin_lock_irqsave(&priv->lock, flags);
	priv->control_state = control_state;
	priv->last_lcr = last_lcr;
	spin_unlock_irqrestore(&priv->lock, flags);
} /* mct_u232_set_termios */

static void mct_u232_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	unsigned char lcr;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	lcr = priv->last_lcr;

	if (break_state)
		lcr |= MCT_U232_SET_BREAK;
	spin_unlock_irqrestore(&priv->lock, flags);

	mct_u232_set_line_ctrl(port, lcr);
} /* mct_u232_break_ctl */


static int mct_u232_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	unsigned int control_state;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	control_state = priv->control_state;
	spin_unlock_irqrestore(&priv->lock, flags);

	return control_state;
}

static int mct_u232_tiocmset(struct tty_struct *tty,
			      unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	unsigned int control_state;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	control_state = priv->control_state;

	if (set & TIOCM_RTS)
		control_state |= TIOCM_RTS;
	if (set & TIOCM_DTR)
		control_state |= TIOCM_DTR;
	if (clear & TIOCM_RTS)
		control_state &= ~TIOCM_RTS;
	if (clear & TIOCM_DTR)
		control_state &= ~TIOCM_DTR;

	priv->control_state = control_state;
	spin_unlock_irqrestore(&priv->lock, flags);
	return mct_u232_set_modem_ctrl(port, control_state);
}

static void mct_u232_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	unsigned int control_state;

	spin_lock_irq(&priv->lock);
	priv->rx_flags |= THROTTLED;
	if (C_CRTSCTS(tty)) {
		priv->control_state &= ~TIOCM_RTS;
		control_state = priv->control_state;
		spin_unlock_irq(&priv->lock);
		mct_u232_set_modem_ctrl(port, control_state);
	} else {
		spin_unlock_irq(&priv->lock);
	}
}

static void mct_u232_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	unsigned int control_state;

	spin_lock_irq(&priv->lock);
	if ((priv->rx_flags & THROTTLED) && C_CRTSCTS(tty)) {
		priv->rx_flags &= ~THROTTLED;
		priv->control_state |= TIOCM_RTS;
		control_state = priv->control_state;
		spin_unlock_irq(&priv->lock);
		mct_u232_set_modem_ctrl(port, control_state);
	} else {
		spin_unlock_irq(&priv->lock);
	}
}

static int  mct_u232_ioctl(struct tty_struct *tty,
			unsigned int cmd, unsigned long arg)
{
	DEFINE_WAIT(wait);
	struct usb_serial_port *port = tty->driver_data;
	struct mct_u232_private *mct_u232_port = usb_get_serial_port_data(port);
	struct async_icount cnow, cprev;
	unsigned long flags;

	dev_dbg(&port->dev, "%s - cmd = 0x%x\n", __func__, cmd);

	switch (cmd) {

	case TIOCMIWAIT:

		dev_dbg(&port->dev, "%s TIOCMIWAIT", __func__);

		spin_lock_irqsave(&mct_u232_port->lock, flags);
		cprev = mct_u232_port->icount;
		spin_unlock_irqrestore(&mct_u232_port->lock, flags);
		for ( ; ; ) {
			prepare_to_wait(&mct_u232_port->msr_wait,
					&wait, TASK_INTERRUPTIBLE);
			schedule();
			finish_wait(&mct_u232_port->msr_wait, &wait);
			/* see if a signal did it */
			if (signal_pending(current))
				return -ERESTARTSYS;
			spin_lock_irqsave(&mct_u232_port->lock, flags);
			cnow = mct_u232_port->icount;
			spin_unlock_irqrestore(&mct_u232_port->lock, flags);
			if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
			    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
				return -EIO; /* no change => error */
			if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
			    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
			    ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
			    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
				return 0;
			}
			cprev = cnow;
		}

	}
	return -ENOIOCTLCMD;
}

static int  mct_u232_get_icount(struct tty_struct *tty,
			struct serial_icounter_struct *icount)
{
	struct usb_serial_port *port = tty->driver_data;
	struct mct_u232_private *mct_u232_port = usb_get_serial_port_data(port);
	struct async_icount *ic = &mct_u232_port->icount;
	unsigned long flags;

	spin_lock_irqsave(&mct_u232_port->lock, flags);

	icount->cts = ic->cts;
	icount->dsr = ic->dsr;
	icount->rng = ic->rng;
	icount->dcd = ic->dcd;
	icount->rx = ic->rx;
	icount->tx = ic->tx;
	icount->frame = ic->frame;
	icount->overrun = ic->overrun;
	icount->parity = ic->parity;
	icount->brk = ic->brk;
	icount->buf_overrun = ic->buf_overrun;

	spin_unlock_irqrestore(&mct_u232_port->lock, flags);

	dev_dbg(&port->dev, "%s TIOCGICOUNT RX=%d, TX=%d\n",
		__func__,  icount->rx, icount->tx);
	return 0;
}

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
