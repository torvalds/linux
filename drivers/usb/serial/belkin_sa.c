/*
 * Belkin USB Serial Adapter Driver
 *
 *  Copyright (C) 2000		William Greathouse (wgreathouse@smva.com)
 *  Copyright (C) 2000-2001 	Greg Kroah-Hartman (greg@kroah.com)
 *
 *  This program is largely derived from work by the linux-usb group
 *  and associated source files.  Please see the usb/serial files for
 *  individual credits and copyrights.
 *  
 * 	This program is free software; you can redistribute it and/or modify
 * 	it under the terms of the GNU General Public License as published by
 * 	the Free Software Foundation; either version 2 of the License, or
 * 	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 * TODO:
 * -- Add true modem contol line query capability.  Currently we track the
 *    states reported by the interrupt and the states we request.
 * -- Add error reporting back to application for UART error conditions.
 *    Just point me at how to implement this and I'll do it. I've put the
 *    framework in, but haven't analyzed the "tty_flip" interface yet.
 * -- Add support for flush commands
 * -- Add everything that is missing :)
 *
 * 27-Nov-2001 gkh
 * 	compressed all the differnent device entries into 1.
 *
 * 30-May-2001 gkh
 *	switched from using spinlock to a semaphore, which fixes lots of problems.
 *
 * 08-Apr-2001 gb
 *	- Identify version on module load.
 *
 * 12-Mar-2001 gkh
 *	- Added support for the GoHubs GO-COM232 device which is the same as the
 *	  Peracom device.
 *
 * 06-Nov-2000 gkh
 *	- Added support for the old Belkin and Peracom devices.
 *	- Made the port able to be opened multiple times.
 *	- Added some defaults incase the line settings are things these devices
 *	  can't support. 
 *
 * 18-Oct-2000 William Greathouse
 *    Released into the wild (linux-usb-devel)
 *
 * 17-Oct-2000 William Greathouse
 *    Add code to recognize firmware version and set hardware flow control
 *    appropriately.  Belkin states that firmware prior to 3.05 does not
 *    operate correctly in hardware handshake mode.  I have verified this
 *    on firmware 2.05 -- for both RTS and DTR input flow control, the control
 *    line is not reset.  The test performed by the Belkin Win* driver is
 *    to enable hardware flow control for firmware 2.06 or greater and
 *    for 1.00 or prior.  I am only enabling for 2.06 or greater.
 *
 * 12-Oct-2000 William Greathouse
 *    First cut at supporting Belkin USB Serial Adapter F5U103
 *    I did not have a copy of the original work to support this
 *    adapter, so pardon any stupid mistakes.  All of the information
 *    I am using to write this driver was acquired by using a modified
 *    UsbSnoop on Windows2000 and from examining the other USB drivers.
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
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include "belkin_sa.h"

static int debug;

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.2"
#define DRIVER_AUTHOR "William Greathouse <wgreathouse@smva.com>"
#define DRIVER_DESC "USB Belkin Serial converter driver"

/* function prototypes for a Belkin USB Serial Adapter F5U103 */
static int  belkin_sa_startup		(struct usb_serial *serial);
static void belkin_sa_shutdown		(struct usb_serial *serial);
static int  belkin_sa_open		(struct usb_serial_port *port, struct file *filp);
static void belkin_sa_close		(struct usb_serial_port *port, struct file *filp);
static void belkin_sa_read_int_callback (struct urb *urb);
static void belkin_sa_set_termios	(struct usb_serial_port *port, struct ktermios * old);
static int  belkin_sa_ioctl		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
static void belkin_sa_break_ctl		(struct usb_serial_port *port, int break_state );
static int  belkin_sa_tiocmget		(struct usb_serial_port *port, struct file *file);
static int  belkin_sa_tiocmset		(struct usb_serial_port *port, struct file *file, unsigned int set, unsigned int clear);


static struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(BELKIN_SA_VID, BELKIN_SA_PID) },
	{ USB_DEVICE(BELKIN_OLD_VID, BELKIN_OLD_PID) },
	{ USB_DEVICE(PERACOM_VID, PERACOM_PID) },
	{ USB_DEVICE(GOHUBS_VID, GOHUBS_PID) },
	{ USB_DEVICE(GOHUBS_VID, HANDYLINK_PID) },
	{ USB_DEVICE(BELKIN_DOCKSTATION_VID, BELKIN_DOCKSTATION_PID) },
	{ }							/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table_combined);

static struct usb_driver belkin_driver = {
	.name =		"belkin",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table_combined,
	.no_dynamic_id = 	1,
};

/* All of the device info needed for the serial converters */
static struct usb_serial_driver belkin_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"belkin",
	},
	.description =		"Belkin / Peracom / GoHubs USB Serial Adapter",
	.usb_driver =		&belkin_driver,
	.id_table =		id_table_combined,
	.num_interrupt_in =	1,
	.num_bulk_in =		1,
	.num_bulk_out =		1,
	.num_ports =		1,
	.open =			belkin_sa_open,
	.close =		belkin_sa_close,
	.read_int_callback =	belkin_sa_read_int_callback,	/* How we get the status info */
	.ioctl =		belkin_sa_ioctl,
	.set_termios =		belkin_sa_set_termios,
	.break_ctl =		belkin_sa_break_ctl,
	.tiocmget =		belkin_sa_tiocmget,
	.tiocmset =		belkin_sa_tiocmset,
	.attach =		belkin_sa_startup,
	.shutdown =		belkin_sa_shutdown,
};


struct belkin_sa_private {
	spinlock_t		lock;
	unsigned long		control_state;
	unsigned char		last_lsr;
	unsigned char		last_msr;
	int			bad_flow_control;
};


/*
 * ***************************************************************************
 * Belkin USB Serial Adapter F5U103 specific driver functions
 * ***************************************************************************
 */

#define WDR_TIMEOUT 5000 /* default urb timeout */

/* assumes that struct usb_serial *serial is available */
#define BSA_USB_CMD(c,v) usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0), \
					    (c), BELKIN_SA_SET_REQUEST_TYPE, \
					    (v), 0, NULL, 0, WDR_TIMEOUT)

/* do some startup allocations not currently performed by usb_serial_probe() */
static int belkin_sa_startup (struct usb_serial *serial)
{
	struct usb_device *dev = serial->dev;
	struct belkin_sa_private *priv;

	/* allocate the private data structure */
	priv = kmalloc(sizeof(struct belkin_sa_private), GFP_KERNEL);
	if (!priv)
		return (-1); /* error */
	/* set initial values for control structures */
	spin_lock_init(&priv->lock);
	priv->control_state = 0;
	priv->last_lsr = 0;
	priv->last_msr = 0;
	/* see comments at top of file */
	priv->bad_flow_control = (le16_to_cpu(dev->descriptor.bcdDevice) <= 0x0206) ? 1 : 0;
	info("bcdDevice: %04x, bfc: %d", le16_to_cpu(dev->descriptor.bcdDevice), priv->bad_flow_control);

	init_waitqueue_head(&serial->port[0]->write_wait);
	usb_set_serial_port_data(serial->port[0], priv);
	
	return (0);
}


static void belkin_sa_shutdown (struct usb_serial *serial)
{
	struct belkin_sa_private *priv;
	int i;
	
	dbg ("%s", __FUNCTION__);

	/* stop reads and writes on all ports */
	for (i=0; i < serial->num_ports; ++i) {
		/* My special items, the standard routines free my urbs */
		priv = usb_get_serial_port_data(serial->port[i]);
		kfree(priv);
	}
}


static int  belkin_sa_open (struct usb_serial_port *port, struct file *filp)
{
	int retval = 0;

	dbg("%s port %d", __FUNCTION__, port->number);

	/*Start reading from the device*/
	/* TODO: Look at possibility of submitting multiple URBs to device to
	 *       enhance buffering.  Win trace shows 16 initial read URBs.
	 */
	port->read_urb->dev = port->serial->dev;
	retval = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (retval) {
		err("usb_submit_urb(read bulk) failed");
		goto exit;
	}

	port->interrupt_in_urb->dev = port->serial->dev;
	retval = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (retval) {
		usb_kill_urb(port->read_urb);
		err(" usb_submit_urb(read int) failed");
	}

exit:
	return retval;
} /* belkin_sa_open */


static void belkin_sa_close (struct usb_serial_port *port, struct file *filp)
{
	dbg("%s port %d", __FUNCTION__, port->number);

	/* shutdown our bulk reads and writes */
	usb_kill_urb(port->write_urb);
	usb_kill_urb(port->read_urb);
	usb_kill_urb(port->interrupt_in_urb);
} /* belkin_sa_close */


static void belkin_sa_read_int_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct belkin_sa_private *priv;
	unsigned char *data = urb->transfer_buffer;
	int retval;
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

	usb_serial_debug_data(debug, &port->dev, __FUNCTION__, urb->actual_length, data);

	/* Handle known interrupt data */
	/* ignore data[0] and data[1] */

	priv = usb_get_serial_port_data(port);
	spin_lock_irqsave(&priv->lock, flags);
	priv->last_msr = data[BELKIN_SA_MSR_INDEX];
	
	/* Record Control Line states */
	if (priv->last_msr & BELKIN_SA_MSR_DSR)
		priv->control_state |= TIOCM_DSR;
	else
		priv->control_state &= ~TIOCM_DSR;

	if (priv->last_msr & BELKIN_SA_MSR_CTS)
		priv->control_state |= TIOCM_CTS;
	else
		priv->control_state &= ~TIOCM_CTS;

	if (priv->last_msr & BELKIN_SA_MSR_RI)
		priv->control_state |= TIOCM_RI;
	else
		priv->control_state &= ~TIOCM_RI;

	if (priv->last_msr & BELKIN_SA_MSR_CD)
		priv->control_state |= TIOCM_CD;
	else
		priv->control_state &= ~TIOCM_CD;

	/* Now to report any errors */
	priv->last_lsr = data[BELKIN_SA_LSR_INDEX];
#if 0
	/*
	 * fill in the flip buffer here, but I do not know the relation
	 * to the current/next receive buffer or characters.  I need
	 * to look in to this before committing any code.
	 */
	if (priv->last_lsr & BELKIN_SA_LSR_ERR) {
		tty = port->tty;
		/* Overrun Error */
		if (priv->last_lsr & BELKIN_SA_LSR_OE) {
		}
		/* Parity Error */
		if (priv->last_lsr & BELKIN_SA_LSR_PE) {
		}
		/* Framing Error */
		if (priv->last_lsr & BELKIN_SA_LSR_FE) {
		}
		/* Break Indicator */
		if (priv->last_lsr & BELKIN_SA_LSR_BI) {
		}
	}
#endif
	spin_unlock_irqrestore(&priv->lock, flags);
exit:
	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		err ("%s - usb_submit_urb failed with result %d",
		     __FUNCTION__, retval);
}

static void belkin_sa_set_termios (struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct belkin_sa_private *priv = usb_get_serial_port_data(port);
	unsigned int iflag;
	unsigned int cflag;
	unsigned int old_iflag = 0;
	unsigned int old_cflag = 0;
	__u16 urb_value = 0; /* Will hold the new flags */
	unsigned long flags;
	unsigned long control_state;
	int bad_flow_control;
	
	if ((!port->tty) || (!port->tty->termios)) {
		dbg ("%s - no tty or termios structure", __FUNCTION__);
		return;
	}

	iflag = port->tty->termios->c_iflag;
	cflag = port->tty->termios->c_cflag;

	/* get a local copy of the current port settings */
	spin_lock_irqsave(&priv->lock, flags);
	control_state = priv->control_state;
	bad_flow_control = priv->bad_flow_control;
	spin_unlock_irqrestore(&priv->lock, flags);
	
	/* check that they really want us to change something */
	if (old_termios) {
		if ((cflag == old_termios->c_cflag) &&
		    (RELEVANT_IFLAG(port->tty->termios->c_iflag) == RELEVANT_IFLAG(old_termios->c_iflag))) {
			dbg("%s - nothing to change...", __FUNCTION__);
			return;
		}
		old_iflag = old_termios->c_iflag;
		old_cflag = old_termios->c_cflag;
	}

	/* Set the baud rate */
	if( (cflag&CBAUD) != (old_cflag&CBAUD) ) {
		/* reassert DTR and (maybe) RTS on transition from B0 */
		if( (old_cflag&CBAUD) == B0 ) {
			control_state |= (TIOCM_DTR|TIOCM_RTS);
			if (BSA_USB_CMD(BELKIN_SA_SET_DTR_REQUEST, 1) < 0)
				err("Set DTR error");
			/* don't set RTS if using hardware flow control */
			if (!(old_cflag&CRTSCTS) )
				if (BSA_USB_CMD(BELKIN_SA_SET_RTS_REQUEST, 1) < 0)
					err("Set RTS error");
		}

		switch(cflag & CBAUD) {
			case B0: /* handled below */ break;
			case B300: urb_value = BELKIN_SA_BAUD(300); break;
			case B600: urb_value = BELKIN_SA_BAUD(600); break;
			case B1200: urb_value = BELKIN_SA_BAUD(1200); break;
			case B2400: urb_value = BELKIN_SA_BAUD(2400); break;
			case B4800: urb_value = BELKIN_SA_BAUD(4800); break;
			case B9600: urb_value = BELKIN_SA_BAUD(9600); break;
			case B19200: urb_value = BELKIN_SA_BAUD(19200); break;
			case B38400: urb_value = BELKIN_SA_BAUD(38400); break;
			case B57600: urb_value = BELKIN_SA_BAUD(57600); break;
			case B115200: urb_value = BELKIN_SA_BAUD(115200); break;
			case B230400: urb_value = BELKIN_SA_BAUD(230400); break;
			default: err("BELKIN USB Serial Adapter: unsupported baudrate request, using default of 9600");
				urb_value = BELKIN_SA_BAUD(9600); break;
		}
		if ((cflag & CBAUD) != B0 ) {
			if (BSA_USB_CMD(BELKIN_SA_SET_BAUDRATE_REQUEST, urb_value) < 0)
				err("Set baudrate error");
		} else {
			/* Disable flow control */
			if (BSA_USB_CMD(BELKIN_SA_SET_FLOW_CTRL_REQUEST, BELKIN_SA_FLOW_NONE) < 0)
				err("Disable flowcontrol error");

			/* Drop RTS and DTR */
			control_state &= ~(TIOCM_DTR | TIOCM_RTS);
			if (BSA_USB_CMD(BELKIN_SA_SET_DTR_REQUEST, 0) < 0)
				err("DTR LOW error");
			if (BSA_USB_CMD(BELKIN_SA_SET_RTS_REQUEST, 0) < 0)
				err("RTS LOW error");
		}
	}

	/* set the parity */
	if( (cflag&(PARENB|PARODD)) != (old_cflag&(PARENB|PARODD)) ) {
		if (cflag & PARENB)
			urb_value = (cflag & PARODD) ?  BELKIN_SA_PARITY_ODD : BELKIN_SA_PARITY_EVEN;
		else
			urb_value = BELKIN_SA_PARITY_NONE;
		if (BSA_USB_CMD(BELKIN_SA_SET_PARITY_REQUEST, urb_value) < 0)
			err("Set parity error");
	}

	/* set the number of data bits */
	if( (cflag&CSIZE) != (old_cflag&CSIZE) ) {
		switch (cflag & CSIZE) {
			case CS5: urb_value = BELKIN_SA_DATA_BITS(5); break;
			case CS6: urb_value = BELKIN_SA_DATA_BITS(6); break;
			case CS7: urb_value = BELKIN_SA_DATA_BITS(7); break;
			case CS8: urb_value = BELKIN_SA_DATA_BITS(8); break;
			default: err("CSIZE was not CS5-CS8, using default of 8");
				urb_value = BELKIN_SA_DATA_BITS(8);
				break;
		}
		if (BSA_USB_CMD(BELKIN_SA_SET_DATA_BITS_REQUEST, urb_value) < 0)
			err("Set data bits error");
	}

	/* set the number of stop bits */
	if( (cflag&CSTOPB) != (old_cflag&CSTOPB) ) {
		urb_value = (cflag & CSTOPB) ? BELKIN_SA_STOP_BITS(2) : BELKIN_SA_STOP_BITS(1);
		if (BSA_USB_CMD(BELKIN_SA_SET_STOP_BITS_REQUEST, urb_value) < 0)
			err("Set stop bits error");
	}

	/* Set flow control */
	if( (iflag&IXOFF)   != (old_iflag&IXOFF)
	||	(iflag&IXON)    != (old_iflag&IXON)
	||  (cflag&CRTSCTS) != (old_cflag&CRTSCTS) ) {
		urb_value = 0;
		if ((iflag & IXOFF) || (iflag & IXON))
			urb_value |= (BELKIN_SA_FLOW_OXON | BELKIN_SA_FLOW_IXON);
		else
			urb_value &= ~(BELKIN_SA_FLOW_OXON | BELKIN_SA_FLOW_IXON);

		if (cflag & CRTSCTS)
			urb_value |=  (BELKIN_SA_FLOW_OCTS | BELKIN_SA_FLOW_IRTS);
		else
			urb_value &= ~(BELKIN_SA_FLOW_OCTS | BELKIN_SA_FLOW_IRTS);

		if (bad_flow_control)
			urb_value &= ~(BELKIN_SA_FLOW_IRTS);

		if (BSA_USB_CMD(BELKIN_SA_SET_FLOW_CTRL_REQUEST, urb_value) < 0)
			err("Set flow control error");
	}

	/* save off the modified port settings */
	spin_lock_irqsave(&priv->lock, flags);
	priv->control_state = control_state;
	spin_unlock_irqrestore(&priv->lock, flags);
} /* belkin_sa_set_termios */


static void belkin_sa_break_ctl( struct usb_serial_port *port, int break_state )
{
	struct usb_serial *serial = port->serial;

	if (BSA_USB_CMD(BELKIN_SA_SET_BREAK_REQUEST, break_state ? 1 : 0) < 0)
		err("Set break_ctl %d", break_state);
}


static int belkin_sa_tiocmget (struct usb_serial_port *port, struct file *file)
{
	struct belkin_sa_private *priv = usb_get_serial_port_data(port);
	unsigned long control_state;
	unsigned long flags;
	
	dbg("%s", __FUNCTION__);

	spin_lock_irqsave(&priv->lock, flags);
	control_state = priv->control_state;
	spin_unlock_irqrestore(&priv->lock, flags);

	return control_state;
}


static int belkin_sa_tiocmset (struct usb_serial_port *port, struct file *file,
			       unsigned int set, unsigned int clear)
{
	struct usb_serial *serial = port->serial;
	struct belkin_sa_private *priv = usb_get_serial_port_data(port);
	unsigned long control_state;
	unsigned long flags;
	int retval;
	int rts = 0;
	int dtr = 0;
	
	dbg("%s", __FUNCTION__);

	spin_lock_irqsave(&priv->lock, flags);
	control_state = priv->control_state;

	if (set & TIOCM_RTS) {
		control_state |= TIOCM_RTS;
		rts = 1;
	}
	if (set & TIOCM_DTR) {
		control_state |= TIOCM_DTR;
		dtr = 1;
	}
	if (clear & TIOCM_RTS) {
		control_state &= ~TIOCM_RTS;
		rts = 0;
	}
	if (clear & TIOCM_DTR) {
		control_state &= ~TIOCM_DTR;
		dtr = 0;
	}

	priv->control_state = control_state;
	spin_unlock_irqrestore(&priv->lock, flags);

	retval = BSA_USB_CMD(BELKIN_SA_SET_RTS_REQUEST, rts);
	if (retval < 0) {
		err("Set RTS error %d", retval);
		goto exit;
	}

	retval = BSA_USB_CMD(BELKIN_SA_SET_DTR_REQUEST, dtr);
	if (retval < 0) {
		err("Set DTR error %d", retval);
		goto exit;
	}
exit:
	return retval;
}


static int belkin_sa_ioctl (struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg)
{
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
		dbg("belkin_sa_ioctl arg not supported - 0x%04x",cmd);
		return(-ENOIOCTLCMD);
		break;
	}
	return 0;
} /* belkin_sa_ioctl */


static int __init belkin_sa_init (void)
{
	int retval;
	retval = usb_serial_register(&belkin_device);
	if (retval)
		goto failed_usb_serial_register;
	retval = usb_register(&belkin_driver);
	if (retval)
		goto failed_usb_register;
	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
failed_usb_register:
	usb_serial_deregister(&belkin_device);
failed_usb_serial_register:
	return retval;
}


static void __exit belkin_sa_exit (void)
{
	usb_deregister (&belkin_driver);
	usb_serial_deregister (&belkin_device);
}


module_init (belkin_sa_init);
module_exit (belkin_sa_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_VERSION( DRIVER_VERSION );
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
