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
 *
 * TEST STATUS:
 *   Basic tests have been performed with minicom/zmodem transfers and
 *   modem dialing under Linux 2.4.0-test10 (for me it works fine).
 *
 * 04-Nov-2003 Bill Marr <marr at flex dot com>
 *   - Mimic Windows driver by sending 2 USB 'device request' messages
 *     following normal 'baud rate change' message.  This allows data to be
 *     transmitted to RS-232 devices which don't assert the 'CTS' signal.
 *
 * 10-Nov-2001 Wolfgang Grandegger
 *   - Fixed an endianess problem with the baudrate selection for PowerPC.
 *
 * 06-Dec-2001 Martin Hamilton <martinh@gnu.org>
 *	Added support for the Belkin F5U109 DB9 adaptor
 *
 * 30-May-2001 Greg Kroah-Hartman
 *	switched from using spinlock to a semaphore, which fixes lots of problems.
 *
 * 04-May-2001 Stelian Pop
 *   - Set the maximum bulk output size for Sitecom U232-P25 model to 16 bytes
 *     instead of the device reported 32 (using 32 bytes causes many data
 *     loss, Windows driver uses 16 too).
 *
 * 02-May-2001 Stelian Pop
 *   - Fixed the baud calculation for Sitecom U232-P25 model
 *
 * 08-Apr-2001 gb
 *   - Identify version on module load.
 *
 * 06-Jan-2001 Cornel Ciocirlan 
 *   - Added support for Sitecom U232-P25 model (Product Id 0x0230)
 *   - Added support for D-Link DU-H3SP USB BAY (Product Id 0x0200)
 *
 * 29-Nov-2000 Greg Kroah-Hartman
 *   - Added device id table to fit with 2.4.0-test11 structure.
 *   - took out DEAL_WITH_TWO_INT_IN_ENDPOINTS #define as it's not needed
 *     (lots of things will change if/when the usb-serial core changes to
 *     handle these issues.
 *
 * 27-Nov-2000 Wolfgang Grandegger
 *   A version for kernel 2.4.0-test10 released to the Linux community 
 *   (via linux-usb-devel).
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include "usb-serial.h"
#include "mct_u232.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "z2.0"		/* Linux in-kernel version */
#define DRIVER_AUTHOR "Wolfgang Grandegger <wolfgang@ces.ch>"
#define DRIVER_DESC "Magic Control Technology USB-RS232 converter driver"

static int debug;

/*
 * Function prototypes
 */
static int  mct_u232_startup	         (struct usb_serial *serial);
static void mct_u232_shutdown	         (struct usb_serial *serial);
static int  mct_u232_open	         (struct usb_serial_port *port,
					  struct file *filp);
static void mct_u232_close	         (struct usb_serial_port *port,
					  struct file *filp);
static void mct_u232_read_int_callback   (struct urb *urb, struct pt_regs *regs);
static void mct_u232_set_termios         (struct usb_serial_port *port,
					  struct termios * old);
static int  mct_u232_ioctl	         (struct usb_serial_port *port,
					  struct file * file,
					  unsigned int cmd,
					  unsigned long arg);
static void mct_u232_break_ctl	         (struct usb_serial_port *port,
					  int break_state );
static int  mct_u232_tiocmget		 (struct usb_serial_port *port,
					  struct file *file);
static int  mct_u232_tiocmset		 (struct usb_serial_port *port,
					  struct file *file, unsigned int set,
					  unsigned int clear);
/*
 * All of the device info needed for the MCT USB-RS232 converter.
 */
static struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(MCT_U232_VID, MCT_U232_PID) },
	{ USB_DEVICE(MCT_U232_VID, MCT_U232_SITECOM_PID) },
	{ USB_DEVICE(MCT_U232_VID, MCT_U232_DU_H3SP_PID) },
	{ USB_DEVICE(MCT_U232_BELKIN_F5U109_VID, MCT_U232_BELKIN_F5U109_PID) },
	{ }		/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table_combined);

static struct usb_driver mct_u232_driver = {
	.name =		"mct_u232",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table_combined,
	.no_dynamic_id = 	1,
};

static struct usb_serial_driver mct_u232_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"mct_u232",
	},
	.description =	     "MCT U232",
	.id_table =	     id_table_combined,
	.num_interrupt_in =  2,
	.num_bulk_in =	     0,
	.num_bulk_out =	     1,
	.num_ports =	     1,
	.open =		     mct_u232_open,
	.close =	     mct_u232_close,
	.read_int_callback = mct_u232_read_int_callback,
	.ioctl =	     mct_u232_ioctl,
	.set_termios =	     mct_u232_set_termios,
	.break_ctl =	     mct_u232_break_ctl,
	.tiocmget =	     mct_u232_tiocmget,
	.tiocmset =	     mct_u232_tiocmset,
	.attach =	     mct_u232_startup,
	.shutdown =	     mct_u232_shutdown,
};


struct mct_u232_private {
	spinlock_t lock;
	unsigned int	     control_state; /* Modem Line Setting (TIOCM) */
	unsigned char        last_lcr;      /* Line Control Register */
	unsigned char	     last_lsr;      /* Line Status Register */
	unsigned char	     last_msr;      /* Modem Status Register */
};

/*
 * Handle vendor specific USB requests
 */

#define WDR_TIMEOUT 5000 /* default urb timeout */

/*
 * Later day 2.6.0-test kernels have new baud rates like B230400 which
 * we do not know how to support. We ignore them for the moment.
 * XXX Rate-limit the error message, it's user triggerable.
 */
static int mct_u232_calculate_baud_rate(struct usb_serial *serial, int value)
{
	if (le16_to_cpu(serial->dev->descriptor.idProduct) == MCT_U232_SITECOM_PID
	  || le16_to_cpu(serial->dev->descriptor.idProduct) == MCT_U232_BELKIN_F5U109_PID) {
		switch (value) {
		case    B300: return 0x01;
		case    B600: return 0x02; /* this one not tested */
		case   B1200: return 0x03;
		case   B2400: return 0x04;
		case   B4800: return 0x06;
		case   B9600: return 0x08;
		case  B19200: return 0x09;
		case  B38400: return 0x0a;
		case  B57600: return 0x0b;
		case B115200: return 0x0c;
		default:
			err("MCT USB-RS232: unsupported baudrate request 0x%x,"
			    " using default of B9600", value);
			return 0x08;
		}
	} else {
		switch (value) {
		case    B300: value =     300; break;
		case    B600: value =     600; break;
		case   B1200: value =    1200; break;
		case   B2400: value =    2400; break;
		case   B4800: value =    4800; break;
		case   B9600: value =    9600; break;
		case  B19200: value =   19200; break;
		case  B38400: value =   38400; break;
		case  B57600: value =   57600; break;
		case B115200: value =  115200; break;
		default:
			err("MCT USB-RS232: unsupported baudrate request 0x%x,"
			    " using default of B9600", value);
			value = 9600;
		}
		return 115200/value;
	}
}

static int mct_u232_set_baud_rate(struct usb_serial *serial, int value)
{
	__le32 divisor;
        int rc;
        unsigned char zero_byte = 0;

	divisor = cpu_to_le32(mct_u232_calculate_baud_rate(serial, value));

        rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
                             MCT_U232_SET_BAUD_RATE_REQUEST,
			     MCT_U232_SET_REQUEST_TYPE,
                             0, 0, &divisor, MCT_U232_SET_BAUD_RATE_SIZE,
			     WDR_TIMEOUT);
	if (rc < 0)
		err("Set BAUD RATE %d failed (error = %d)", value, rc);
	dbg("set_baud_rate: value: 0x%x, divisor: 0x%x", value, divisor);

	/* Mimic the MCT-supplied Windows driver (version 1.21P.0104), which
	   always sends two extra USB 'device request' messages after the
	   'baud rate change' message.  The actual functionality of the
	   request codes in these messages is not fully understood but these
	   particular codes are never seen in any operation besides a baud
	   rate change.  Both of these messages send a single byte of data
	   whose value is always zero.  The second of these two extra messages
	   is required in order for data to be properly written to an RS-232
	   device which does not assert the 'CTS' signal. */

	rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			     MCT_U232_SET_UNKNOWN1_REQUEST, 
			     MCT_U232_SET_REQUEST_TYPE,
			     0, 0, &zero_byte, MCT_U232_SET_UNKNOWN1_SIZE, 
			     WDR_TIMEOUT);
	if (rc < 0)
		err("Sending USB device request code %d failed (error = %d)", 
		    MCT_U232_SET_UNKNOWN1_REQUEST, rc);

	rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			     MCT_U232_SET_UNKNOWN2_REQUEST, 
			     MCT_U232_SET_REQUEST_TYPE,
			     0, 0, &zero_byte, MCT_U232_SET_UNKNOWN2_SIZE, 
			     WDR_TIMEOUT);
	if (rc < 0)
		err("Sending USB device request code %d failed (error = %d)", 
		    MCT_U232_SET_UNKNOWN2_REQUEST, rc);

        return rc;
} /* mct_u232_set_baud_rate */

static int mct_u232_set_line_ctrl(struct usb_serial *serial, unsigned char lcr)
{
        int rc;
        rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
                             MCT_U232_SET_LINE_CTRL_REQUEST,
			     MCT_U232_SET_REQUEST_TYPE,
                             0, 0, &lcr, MCT_U232_SET_LINE_CTRL_SIZE,
			     WDR_TIMEOUT);
	if (rc < 0)
		err("Set LINE CTRL 0x%x failed (error = %d)", lcr, rc);
	dbg("set_line_ctrl: 0x%x", lcr);
        return rc;
} /* mct_u232_set_line_ctrl */

static int mct_u232_set_modem_ctrl(struct usb_serial *serial,
				   unsigned int control_state)
{
        int rc;
	unsigned char mcr = MCT_U232_MCR_NONE;

	if (control_state & TIOCM_DTR)
		mcr |= MCT_U232_MCR_DTR;
	if (control_state & TIOCM_RTS)
		mcr |= MCT_U232_MCR_RTS;

        rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
                             MCT_U232_SET_MODEM_CTRL_REQUEST,
			     MCT_U232_SET_REQUEST_TYPE,
                             0, 0, &mcr, MCT_U232_SET_MODEM_CTRL_SIZE,
			     WDR_TIMEOUT);
	if (rc < 0)
		err("Set MODEM CTRL 0x%x failed (error = %d)", mcr, rc);
	dbg("set_modem_ctrl: state=0x%x ==> mcr=0x%x", control_state, mcr);

        return rc;
} /* mct_u232_set_modem_ctrl */

static int mct_u232_get_modem_stat(struct usb_serial *serial, unsigned char *msr)
{
        int rc;
        rc = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
                             MCT_U232_GET_MODEM_STAT_REQUEST,
			     MCT_U232_GET_REQUEST_TYPE,
                             0, 0, msr, MCT_U232_GET_MODEM_STAT_SIZE,
			     WDR_TIMEOUT);
	if (rc < 0) {
		err("Get MODEM STATus failed (error = %d)", rc);
		*msr = 0;
	}
	dbg("get_modem_stat: 0x%x", *msr);
        return rc;
} /* mct_u232_get_modem_stat */

static void mct_u232_msr_to_state(unsigned int *control_state, unsigned char msr)
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
 	dbg("msr_to_state: msr=0x%x ==> state=0x%x", msr, *control_state);
} /* mct_u232_msr_to_state */

/*
 * Driver's tty interface functions
 */

static int mct_u232_startup (struct usb_serial *serial)
{
	struct mct_u232_private *priv;
	struct usb_serial_port *port, *rport;

	priv = kzalloc(sizeof(struct mct_u232_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	spin_lock_init(&priv->lock);
	usb_set_serial_port_data(serial->port[0], priv);

	init_waitqueue_head(&serial->port[0]->write_wait);

	/* Puh, that's dirty */
	port = serial->port[0];
	rport = serial->port[1];
	if (port->read_urb) {
		/* No unlinking, it wasn't submitted yet. */
		usb_free_urb(port->read_urb);
	}
	port->read_urb = rport->interrupt_in_urb;
	rport->interrupt_in_urb = NULL;
	port->read_urb->context = port;

	return (0);
} /* mct_u232_startup */


static void mct_u232_shutdown (struct usb_serial *serial)
{
	struct mct_u232_private *priv;
	int i;
	
	dbg("%s", __FUNCTION__);

	for (i=0; i < serial->num_ports; ++i) {
		/* My special items, the standard routines free my urbs */
		priv = usb_get_serial_port_data(serial->port[i]);
		if (priv) {
			usb_set_serial_port_data(serial->port[i], NULL);
			kfree(priv);
		}
	}
} /* mct_u232_shutdown */

static int  mct_u232_open (struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial = port->serial;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	int retval = 0;
	unsigned int control_state;
	unsigned long flags;
	unsigned char last_lcr;
	unsigned char last_msr;

	dbg("%s port %d", __FUNCTION__, port->number);

	/* Compensate for a hardware bug: although the Sitecom U232-P25
	 * device reports a maximum output packet size of 32 bytes,
	 * it seems to be able to accept only 16 bytes (and that's what
	 * SniffUSB says too...)
	 */
	if (le16_to_cpu(serial->dev->descriptor.idProduct) == MCT_U232_SITECOM_PID)
		port->bulk_out_size = 16;

	/* Do a defined restart: the normal serial device seems to 
	 * always turn on DTR and RTS here, so do the same. I'm not
	 * sure if this is really necessary. But it should not harm
	 * either.
	 */
	spin_lock_irqsave(&priv->lock, flags);
	if (port->tty->termios->c_cflag & CBAUD)
		priv->control_state = TIOCM_DTR | TIOCM_RTS;
	else
		priv->control_state = 0;
	
	priv->last_lcr = (MCT_U232_DATA_BITS_8 | 
			  MCT_U232_PARITY_NONE |
			  MCT_U232_STOP_BITS_1);
	control_state = priv->control_state;
	last_lcr = priv->last_lcr;
	spin_unlock_irqrestore(&priv->lock, flags);
	mct_u232_set_modem_ctrl(serial, control_state);
	mct_u232_set_line_ctrl(serial, last_lcr);

	/* Read modem status and update control state */
	mct_u232_get_modem_stat(serial, &last_msr);
	spin_lock_irqsave(&priv->lock, flags);
	priv->last_msr = last_msr;
	mct_u232_msr_to_state(&priv->control_state, priv->last_msr);
	spin_unlock_irqrestore(&priv->lock, flags);

	port->read_urb->dev = port->serial->dev;
	retval = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (retval) {
		err("usb_submit_urb(read bulk) failed pipe 0x%x err %d",
		    port->read_urb->pipe, retval);
		goto exit;
	}

	port->interrupt_in_urb->dev = port->serial->dev;
	retval = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (retval)
		err(" usb_submit_urb(read int) failed pipe 0x%x err %d",
		    port->interrupt_in_urb->pipe, retval);

exit:
	return 0;
} /* mct_u232_open */


static void mct_u232_close (struct usb_serial_port *port, struct file *filp)
{
	dbg("%s port %d", __FUNCTION__, port->number);

	if (port->serial->dev) {
		/* shutdown our urbs */
		usb_kill_urb(port->write_urb);
		usb_kill_urb(port->read_urb);
		usb_kill_urb(port->interrupt_in_urb);
	}
} /* mct_u232_close */


static void mct_u232_read_int_callback (struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;
	int status;
	unsigned long flags;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __FUNCTION__, urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __FUNCTION__, urb->status);
		goto exit;
	}

	if (!serial) {
		dbg("%s - bad serial pointer, exiting", __FUNCTION__);
		return;
	}

        dbg("%s - port %d", __FUNCTION__, port->number);
	usb_serial_debug_data(debug, &port->dev, __FUNCTION__, urb->actual_length, data);

	/*
	 * Work-a-round: handle the 'usual' bulk-in pipe here
	 */
	if (urb->transfer_buffer_length > 2) {
		int i;
		tty = port->tty;
		if (urb->actual_length) {
			for (i = 0; i < urb->actual_length ; ++i) {
				tty_insert_flip_char(tty, data[i], 0);
			}
			tty_flip_buffer_push(tty);
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
	mct_u232_msr_to_state(&priv->control_state, priv->last_msr);

#if 0
	/* Not yet handled. See belin_sa.c for further information */
	/* Now to report any errors */
	priv->last_lsr = data[MCT_U232_LSR_INDEX];
	/*
	 * fill in the flip buffer here, but I do not know the relation
	 * to the current/next receive buffer or characters.  I need
	 * to look in to this before committing any code.
	 */
	if (priv->last_lsr & MCT_U232_LSR_ERR) {
		tty = port->tty;
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
	}
#endif
	spin_unlock_irqrestore(&priv->lock, flags);
exit:
	status = usb_submit_urb (urb, GFP_ATOMIC);
	if (status)
		err ("%s - usb_submit_urb failed with result %d",
		     __FUNCTION__, status);
} /* mct_u232_read_int_callback */

static void mct_u232_set_termios (struct usb_serial_port *port,
				  struct termios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	unsigned int iflag = port->tty->termios->c_iflag;
	unsigned int cflag = port->tty->termios->c_cflag;
	unsigned int old_cflag = old_termios->c_cflag;
	unsigned long flags;
	unsigned int control_state, new_state;
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

        /* reassert DTR and (maybe) RTS on transition from B0 */
	if ((old_cflag & CBAUD) == B0) {
		dbg("%s: baud was B0", __FUNCTION__);
		control_state |= TIOCM_DTR;
		/* don't set RTS if using hardware flow control */
		if (!(old_cflag & CRTSCTS)) {
			control_state |= TIOCM_RTS;
		}
		mct_u232_set_modem_ctrl(serial, control_state);
	}

	mct_u232_set_baud_rate(serial, cflag & CBAUD);

	if ((cflag & CBAUD) == B0 ) {
		dbg("%s: baud is B0", __FUNCTION__);
		/* Drop RTS and DTR */
		control_state &= ~(TIOCM_DTR | TIOCM_RTS);
       		mct_u232_set_modem_ctrl(serial, control_state);
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
		err("CSIZE was not CS5-CS8, using default of 8");
		last_lcr |= MCT_U232_DATA_BITS_8;
		break;
	}

	/* set the number of stop bits */
	last_lcr |= (cflag & CSTOPB) ?
		MCT_U232_STOP_BITS_2 : MCT_U232_STOP_BITS_1;

	mct_u232_set_line_ctrl(serial, last_lcr);

	/*
	 * Set flow control: well, I do not really now how to handle DTR/RTS.
	 * Just do what we have seen with SniffUSB on Win98.
	 */
	/* Drop DTR/RTS if no flow control otherwise assert */
	new_state = control_state;
	if ((iflag & IXOFF) || (iflag & IXON) || (cflag & CRTSCTS))
		new_state |= TIOCM_DTR | TIOCM_RTS;
	else
		new_state &= ~(TIOCM_DTR | TIOCM_RTS);
	if (new_state != control_state) {
		mct_u232_set_modem_ctrl(serial, new_state);
		control_state = new_state;
	}

	/* save off the modified port settings */
	spin_lock_irqsave(&priv->lock, flags);
	priv->control_state = control_state;
	priv->last_lcr = last_lcr;
	spin_unlock_irqrestore(&priv->lock, flags);
} /* mct_u232_set_termios */

static void mct_u232_break_ctl( struct usb_serial_port *port, int break_state )
{
	struct usb_serial *serial = port->serial;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	unsigned char lcr;
	unsigned long flags;

	dbg("%sstate=%d", __FUNCTION__, break_state);

	spin_lock_irqsave(&priv->lock, flags);
	lcr = priv->last_lcr;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (break_state)
		lcr |= MCT_U232_SET_BREAK;

	mct_u232_set_line_ctrl(serial, lcr);
} /* mct_u232_break_ctl */


static int mct_u232_tiocmget (struct usb_serial_port *port, struct file *file)
{
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	unsigned int control_state;
	unsigned long flags;
	
	dbg("%s", __FUNCTION__);

	spin_lock_irqsave(&priv->lock, flags);
	control_state = priv->control_state;
	spin_unlock_irqrestore(&priv->lock, flags);

	return control_state;
}

static int mct_u232_tiocmset (struct usb_serial_port *port, struct file *file,
			      unsigned int set, unsigned int clear)
{
	struct usb_serial *serial = port->serial;
	struct mct_u232_private *priv = usb_get_serial_port_data(port);
	unsigned int control_state;
	unsigned long flags;
	
	dbg("%s", __FUNCTION__);

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
	return mct_u232_set_modem_ctrl(serial, control_state);
}

static int mct_u232_ioctl (struct usb_serial_port *port, struct file * file,
			   unsigned int cmd, unsigned long arg)
{
	dbg("%scmd=0x%x", __FUNCTION__, cmd);

	/* Based on code from acm.c and others */
	switch (cmd) {
	case TIOCMIWAIT:
		/* wait for any of the 4 modem inputs (DCD,RI,DSR,CTS)*/
		/* TODO */
		return( 0 );

	case TIOCGICOUNT:
		/* return count of modemline transitions */
		/* TODO */
		return 0;

	default:
		dbg("%s: arg not supported - 0x%04x", __FUNCTION__,cmd);
		return(-ENOIOCTLCMD);
		break;
	}
	return 0;
} /* mct_u232_ioctl */


static int __init mct_u232_init (void)
{
	int retval;
	retval = usb_serial_register(&mct_u232_device);
	if (retval)
		goto failed_usb_serial_register;
	retval = usb_register(&mct_u232_driver);
	if (retval)
		goto failed_usb_register;
	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
failed_usb_register:
	usb_serial_deregister(&mct_u232_device);
failed_usb_serial_register:
	return retval;
}


static void __exit mct_u232_exit (void)
{
	usb_deregister (&mct_u232_driver);
	usb_serial_deregister (&mct_u232_device);
}


module_init (mct_u232_init);
module_exit(mct_u232_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
