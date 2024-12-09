// SPDX-License-Identifier: GPL-2.0+
/*
 * Belkin USB Serial Adapter Driver
 *
 *  Copyright (C) 2000		William Greathouse (wgreathouse@smva.com)
 *  Copyright (C) 2000-2001	Greg Kroah-Hartman (greg@kroah.com)
 *  Copyright (C) 2010		Johan Hovold (jhovold@gmail.com)
 *
 *  This program is largely derived from work by the linux-usb group
 *  and associated source files.  Please see the usb/serial files for
 *  individual credits and copyrights.
 *
 * See Documentation/usb/usb-serial.rst for more information on using this
 * driver
 *
 * TODO:
 * -- Add true modem control line query capability.  Currently we track the
 *    states reported by the interrupt and the states we request.
 * -- Add support for flush commands
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include "belkin_sa.h"

#define DRIVER_AUTHOR "William Greathouse <wgreathouse@smva.com>"
#define DRIVER_DESC "USB Belkin Serial converter driver"

/* function prototypes for a Belkin USB Serial Adapter F5U103 */
static int belkin_sa_port_probe(struct usb_serial_port *port);
static void belkin_sa_port_remove(struct usb_serial_port *port);
static int  belkin_sa_open(struct tty_struct *tty,
			struct usb_serial_port *port);
static void belkin_sa_close(struct usb_serial_port *port);
static void belkin_sa_read_int_callback(struct urb *urb);
static void belkin_sa_process_read_urb(struct urb *urb);
static void belkin_sa_set_termios(struct tty_struct *tty,
				  struct usb_serial_port *port,
				  const struct ktermios *old_termios);
static int belkin_sa_break_ctl(struct tty_struct *tty, int break_state);
static int  belkin_sa_tiocmget(struct tty_struct *tty);
static int  belkin_sa_tiocmset(struct tty_struct *tty,
					unsigned int set, unsigned int clear);


static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(BELKIN_SA_VID, BELKIN_SA_PID) },
	{ USB_DEVICE(BELKIN_OLD_VID, BELKIN_OLD_PID) },
	{ USB_DEVICE(PERACOM_VID, PERACOM_PID) },
	{ USB_DEVICE(GOHUBS_VID, GOHUBS_PID) },
	{ USB_DEVICE(GOHUBS_VID, HANDYLINK_PID) },
	{ USB_DEVICE(BELKIN_DOCKSTATION_VID, BELKIN_DOCKSTATION_PID) },
	{ }	/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

/* All of the device info needed for the serial converters */
static struct usb_serial_driver belkin_device = {
	.driver = {
		.name =		"belkin",
	},
	.description =		"Belkin / Peracom / GoHubs USB Serial Adapter",
	.id_table =		id_table,
	.num_ports =		1,
	.open =			belkin_sa_open,
	.close =		belkin_sa_close,
	.read_int_callback =	belkin_sa_read_int_callback,
	.process_read_urb =	belkin_sa_process_read_urb,
	.set_termios =		belkin_sa_set_termios,
	.break_ctl =		belkin_sa_break_ctl,
	.tiocmget =		belkin_sa_tiocmget,
	.tiocmset =		belkin_sa_tiocmset,
	.port_probe =		belkin_sa_port_probe,
	.port_remove =		belkin_sa_port_remove,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&belkin_device, NULL
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
#define BSA_USB_CMD(c, v) usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0), \
					    (c), BELKIN_SA_SET_REQUEST_TYPE, \
					    (v), 0, NULL, 0, WDR_TIMEOUT)

static int belkin_sa_port_probe(struct usb_serial_port *port)
{
	struct usb_device *dev = port->serial->dev;
	struct belkin_sa_private *priv;

	priv = kmalloc(sizeof(struct belkin_sa_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	priv->control_state = 0;
	priv->last_lsr = 0;
	priv->last_msr = 0;
	/* see comments at top of file */
	priv->bad_flow_control =
		(le16_to_cpu(dev->descriptor.bcdDevice) <= 0x0206) ? 1 : 0;
	dev_info(&dev->dev, "bcdDevice: %04x, bfc: %d\n",
					le16_to_cpu(dev->descriptor.bcdDevice),
					priv->bad_flow_control);

	usb_set_serial_port_data(port, priv);

	return 0;
}

static void belkin_sa_port_remove(struct usb_serial_port *port)
{
	struct belkin_sa_private *priv;

	priv = usb_get_serial_port_data(port);
	kfree(priv);
}

static int belkin_sa_open(struct tty_struct *tty,
					struct usb_serial_port *port)
{
	int retval;

	retval = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (retval) {
		dev_err(&port->dev, "usb_submit_urb(read int) failed\n");
		return retval;
	}

	retval = usb_serial_generic_open(tty, port);
	if (retval)
		usb_kill_urb(port->interrupt_in_urb);

	return retval;
}

static void belkin_sa_close(struct usb_serial_port *port)
{
	usb_serial_generic_close(port);
	usb_kill_urb(port->interrupt_in_urb);
}

static void belkin_sa_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct belkin_sa_private *priv;
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

	priv->last_lsr = data[BELKIN_SA_LSR_INDEX];
	spin_unlock_irqrestore(&priv->lock, flags);
exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&port->dev, "%s - usb_submit_urb failed with "
			"result %d\n", __func__, retval);
}

static void belkin_sa_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct belkin_sa_private *priv = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	unsigned long flags;
	unsigned char status;
	char tty_flag;

	/* Update line status */
	tty_flag = TTY_NORMAL;

	spin_lock_irqsave(&priv->lock, flags);
	status = priv->last_lsr;
	priv->last_lsr &= ~BELKIN_SA_LSR_ERR;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (!urb->actual_length)
		return;

	if (status & BELKIN_SA_LSR_ERR) {
		/* Break takes precedence over parity, which takes precedence
		 * over framing errors. */
		if (status & BELKIN_SA_LSR_BI)
			tty_flag = TTY_BREAK;
		else if (status & BELKIN_SA_LSR_PE)
			tty_flag = TTY_PARITY;
		else if (status & BELKIN_SA_LSR_FE)
			tty_flag = TTY_FRAME;
		dev_dbg(&port->dev, "tty_flag = %d\n", tty_flag);

		/* Overrun is special, not associated with a char. */
		if (status & BELKIN_SA_LSR_OE)
			tty_insert_flip_char(&port->port, 0, TTY_OVERRUN);
	}

	tty_insert_flip_string_fixed_flag(&port->port, data, tty_flag,
							urb->actual_length);
	tty_flip_buffer_push(&port->port);
}

static void belkin_sa_set_termios(struct tty_struct *tty,
				  struct usb_serial_port *port,
				  const struct ktermios *old_termios)
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
	speed_t baud;
	struct ktermios *termios = &tty->termios;

	iflag = termios->c_iflag;
	cflag = termios->c_cflag;

	termios->c_cflag &= ~CMSPAR;

	/* get a local copy of the current port settings */
	spin_lock_irqsave(&priv->lock, flags);
	control_state = priv->control_state;
	bad_flow_control = priv->bad_flow_control;
	spin_unlock_irqrestore(&priv->lock, flags);

	old_iflag = old_termios->c_iflag;
	old_cflag = old_termios->c_cflag;

	/* Set the baud rate */
	if ((cflag & CBAUD) != (old_cflag & CBAUD)) {
		/* reassert DTR and (maybe) RTS on transition from B0 */
		if ((old_cflag & CBAUD) == B0) {
			control_state |= (TIOCM_DTR|TIOCM_RTS);
			if (BSA_USB_CMD(BELKIN_SA_SET_DTR_REQUEST, 1) < 0)
				dev_err(&port->dev, "Set DTR error\n");
			/* don't set RTS if using hardware flow control */
			if (!(old_cflag & CRTSCTS))
				if (BSA_USB_CMD(BELKIN_SA_SET_RTS_REQUEST
								, 1) < 0)
					dev_err(&port->dev, "Set RTS error\n");
		}
	}

	baud = tty_get_baud_rate(tty);
	if (baud) {
		urb_value = BELKIN_SA_BAUD(baud);
		/* Clip to maximum speed */
		if (urb_value == 0)
			urb_value = 1;
		/* Turn it back into a resulting real baud rate */
		baud = BELKIN_SA_BAUD(urb_value);

		/* Report the actual baud rate back to the caller */
		tty_encode_baud_rate(tty, baud, baud);
		if (BSA_USB_CMD(BELKIN_SA_SET_BAUDRATE_REQUEST, urb_value) < 0)
			dev_err(&port->dev, "Set baudrate error\n");
	} else {
		/* Disable flow control */
		if (BSA_USB_CMD(BELKIN_SA_SET_FLOW_CTRL_REQUEST,
						BELKIN_SA_FLOW_NONE) < 0)
			dev_err(&port->dev, "Disable flowcontrol error\n");
		/* Drop RTS and DTR */
		control_state &= ~(TIOCM_DTR | TIOCM_RTS);
		if (BSA_USB_CMD(BELKIN_SA_SET_DTR_REQUEST, 0) < 0)
			dev_err(&port->dev, "DTR LOW error\n");
		if (BSA_USB_CMD(BELKIN_SA_SET_RTS_REQUEST, 0) < 0)
			dev_err(&port->dev, "RTS LOW error\n");
	}

	/* set the parity */
	if ((cflag ^ old_cflag) & (PARENB | PARODD)) {
		if (cflag & PARENB)
			urb_value = (cflag & PARODD) ?  BELKIN_SA_PARITY_ODD
						: BELKIN_SA_PARITY_EVEN;
		else
			urb_value = BELKIN_SA_PARITY_NONE;
		if (BSA_USB_CMD(BELKIN_SA_SET_PARITY_REQUEST, urb_value) < 0)
			dev_err(&port->dev, "Set parity error\n");
	}

	/* set the number of data bits */
	if ((cflag & CSIZE) != (old_cflag & CSIZE)) {
		urb_value = BELKIN_SA_DATA_BITS(tty_get_char_size(cflag));
		if (BSA_USB_CMD(BELKIN_SA_SET_DATA_BITS_REQUEST, urb_value) < 0)
			dev_err(&port->dev, "Set data bits error\n");
	}

	/* set the number of stop bits */
	if ((cflag & CSTOPB) != (old_cflag & CSTOPB)) {
		urb_value = (cflag & CSTOPB) ? BELKIN_SA_STOP_BITS(2)
						: BELKIN_SA_STOP_BITS(1);
		if (BSA_USB_CMD(BELKIN_SA_SET_STOP_BITS_REQUEST,
							urb_value) < 0)
			dev_err(&port->dev, "Set stop bits error\n");
	}

	/* Set flow control */
	if (((iflag ^ old_iflag) & (IXOFF | IXON)) ||
		((cflag ^ old_cflag) & CRTSCTS)) {
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
			dev_err(&port->dev, "Set flow control error\n");
	}

	/* save off the modified port settings */
	spin_lock_irqsave(&priv->lock, flags);
	priv->control_state = control_state;
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int belkin_sa_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	int ret;

	ret = BSA_USB_CMD(BELKIN_SA_SET_BREAK_REQUEST, break_state ? 1 : 0);
	if (ret < 0) {
		dev_err(&port->dev, "Set break_ctl %d\n", break_state);
		return ret;
	}

	return 0;
}

static int belkin_sa_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct belkin_sa_private *priv = usb_get_serial_port_data(port);
	unsigned long control_state;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	control_state = priv->control_state;
	spin_unlock_irqrestore(&priv->lock, flags);

	return control_state;
}

static int belkin_sa_tiocmset(struct tty_struct *tty,
			       unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	struct belkin_sa_private *priv = usb_get_serial_port_data(port);
	unsigned long control_state;
	unsigned long flags;
	int retval;
	int rts = 0;
	int dtr = 0;

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
		dev_err(&port->dev, "Set RTS error %d\n", retval);
		goto exit;
	}

	retval = BSA_USB_CMD(BELKIN_SA_SET_DTR_REQUEST, dtr);
	if (retval < 0) {
		dev_err(&port->dev, "Set DTR error %d\n", retval);
		goto exit;
	}
exit:
	return retval;
}

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
