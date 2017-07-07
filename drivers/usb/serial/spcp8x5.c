/*
 * spcp8x5 USB to serial adaptor driver
 *
 * Copyright (C) 2010-2013 Johan Hovold (jhovold@gmail.com)
 * Copyright (C) 2006 Linxb (xubin.lin@worldplus.com.cn)
 * Copyright (C) 2006 S1 Corp.
 *
 * Original driver for 2.6.10 pl2303 driver by
 *   Greg Kroah-Hartman (greg@kroah.com)
 * Changes for 2.6.20 by Harald Klein <hari@vt100.at>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define DRIVER_DESC	"SPCP8x5 USB to serial adaptor driver"

#define SPCP825_QUIRK_NO_UART_STATUS	0x01
#define SPCP825_QUIRK_NO_WORK_MODE	0x02

#define SPCP8x5_007_VID		0x04FC
#define SPCP8x5_007_PID		0x0201
#define SPCP8x5_008_VID		0x04fc
#define SPCP8x5_008_PID		0x0235
#define SPCP8x5_PHILIPS_VID	0x0471
#define SPCP8x5_PHILIPS_PID	0x081e
#define SPCP8x5_INTERMATIC_VID	0x04FC
#define SPCP8x5_INTERMATIC_PID	0x0204
#define SPCP8x5_835_VID		0x04fc
#define SPCP8x5_835_PID		0x0231

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(SPCP8x5_PHILIPS_VID , SPCP8x5_PHILIPS_PID)},
	{ USB_DEVICE(SPCP8x5_INTERMATIC_VID, SPCP8x5_INTERMATIC_PID)},
	{ USB_DEVICE(SPCP8x5_835_VID, SPCP8x5_835_PID)},
	{ USB_DEVICE(SPCP8x5_008_VID, SPCP8x5_008_PID)},
	{ USB_DEVICE(SPCP8x5_007_VID, SPCP8x5_007_PID),
	  .driver_info = SPCP825_QUIRK_NO_UART_STATUS |
				SPCP825_QUIRK_NO_WORK_MODE },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

struct spcp8x5_usb_ctrl_arg {
	u8	type;
	u8	cmd;
	u8	cmd_type;
	u16	value;
	u16	index;
	u16	length;
};


/* spcp8x5 spec register define */
#define MCR_CONTROL_LINE_RTS		0x02
#define MCR_CONTROL_LINE_DTR		0x01
#define MCR_DTR				0x01
#define MCR_RTS				0x02

#define MSR_STATUS_LINE_DCD		0x80
#define MSR_STATUS_LINE_RI		0x40
#define MSR_STATUS_LINE_DSR		0x20
#define MSR_STATUS_LINE_CTS		0x10

/* verdor command here , we should define myself */
#define SET_DEFAULT			0x40
#define SET_DEFAULT_TYPE		0x20

#define SET_UART_FORMAT			0x40
#define SET_UART_FORMAT_TYPE		0x21
#define SET_UART_FORMAT_SIZE_5		0x00
#define SET_UART_FORMAT_SIZE_6		0x01
#define SET_UART_FORMAT_SIZE_7		0x02
#define SET_UART_FORMAT_SIZE_8		0x03
#define SET_UART_FORMAT_STOP_1		0x00
#define SET_UART_FORMAT_STOP_2		0x04
#define SET_UART_FORMAT_PAR_NONE	0x00
#define SET_UART_FORMAT_PAR_ODD		0x10
#define SET_UART_FORMAT_PAR_EVEN	0x30
#define SET_UART_FORMAT_PAR_MASK	0xD0
#define SET_UART_FORMAT_PAR_SPACE	0x90

#define GET_UART_STATUS_TYPE		0xc0
#define GET_UART_STATUS			0x22
#define GET_UART_STATUS_MSR		0x06

#define SET_UART_STATUS			0x40
#define SET_UART_STATUS_TYPE		0x23
#define SET_UART_STATUS_MCR		0x0004
#define SET_UART_STATUS_MCR_DTR		0x01
#define SET_UART_STATUS_MCR_RTS		0x02
#define SET_UART_STATUS_MCR_LOOP	0x10

#define SET_WORKING_MODE		0x40
#define SET_WORKING_MODE_TYPE		0x24
#define SET_WORKING_MODE_U2C		0x00
#define SET_WORKING_MODE_RS485		0x01
#define SET_WORKING_MODE_PDMA		0x02
#define SET_WORKING_MODE_SPP		0x03

#define SET_FLOWCTL_CHAR		0x40
#define SET_FLOWCTL_CHAR_TYPE		0x25

#define GET_VERSION			0xc0
#define GET_VERSION_TYPE		0x26

#define SET_REGISTER			0x40
#define SET_REGISTER_TYPE		0x27

#define	GET_REGISTER			0xc0
#define GET_REGISTER_TYPE		0x28

#define SET_RAM				0x40
#define SET_RAM_TYPE			0x31

#define GET_RAM				0xc0
#define GET_RAM_TYPE			0x32

/* how come ??? */
#define UART_STATE			0x08
#define UART_STATE_TRANSIENT_MASK	0x75
#define UART_DCD			0x01
#define UART_DSR			0x02
#define UART_BREAK_ERROR		0x04
#define UART_RING			0x08
#define UART_FRAME_ERROR		0x10
#define UART_PARITY_ERROR		0x20
#define UART_OVERRUN_ERROR		0x40
#define UART_CTS			0x80

struct spcp8x5_private {
	unsigned		quirks;
	spinlock_t		lock;
	u8			line_control;
};

static int spcp8x5_probe(struct usb_serial *serial,
						const struct usb_device_id *id)
{
	usb_set_serial_data(serial, (void *)id);

	return 0;
}

static int spcp8x5_port_probe(struct usb_serial_port *port)
{
	const struct usb_device_id *id = usb_get_serial_data(port->serial);
	struct spcp8x5_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	priv->quirks = id->driver_info;

	usb_set_serial_port_data(port, priv);

	port->port.drain_delay = 256;

	return 0;
}

static int spcp8x5_port_remove(struct usb_serial_port *port)
{
	struct spcp8x5_private *priv;

	priv = usb_get_serial_port_data(port);
	kfree(priv);

	return 0;
}

static int spcp8x5_set_ctrl_line(struct usb_serial_port *port, u8 mcr)
{
	struct spcp8x5_private *priv = usb_get_serial_port_data(port);
	struct usb_device *dev = port->serial->dev;
	int retval;

	if (priv->quirks & SPCP825_QUIRK_NO_UART_STATUS)
		return -EPERM;

	retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 SET_UART_STATUS_TYPE, SET_UART_STATUS,
				 mcr, 0x04, NULL, 0, 100);
	if (retval != 0) {
		dev_err(&port->dev, "failed to set control lines: %d\n",
								retval);
	}
	return retval;
}

static int spcp8x5_get_msr(struct usb_serial_port *port, u8 *status)
{
	struct spcp8x5_private *priv = usb_get_serial_port_data(port);
	struct usb_device *dev = port->serial->dev;
	u8 *buf;
	int ret;

	if (priv->quirks & SPCP825_QUIRK_NO_UART_STATUS)
		return -EPERM;

	buf = kzalloc(1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			      GET_UART_STATUS, GET_UART_STATUS_TYPE,
			      0, GET_UART_STATUS_MSR, buf, 1, 100);
	if (ret < 1) {
		dev_err(&port->dev, "failed to get modem status: %d\n", ret);
		if (ret >= 0)
			ret = -EIO;
		goto out;
	}

	dev_dbg(&port->dev, "0xc0:0x22:0:6  %d - 0x02%x\n", ret, *buf);
	*status = *buf;
	ret = 0;
out:
	kfree(buf);

	return ret;
}

static void spcp8x5_set_work_mode(struct usb_serial_port *port, u16 value,
								 u16 index)
{
	struct spcp8x5_private *priv = usb_get_serial_port_data(port);
	struct usb_device *dev = port->serial->dev;
	int ret;

	if (priv->quirks & SPCP825_QUIRK_NO_WORK_MODE)
		return;

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			      SET_WORKING_MODE_TYPE, SET_WORKING_MODE,
			      value, index, NULL, 0, 100);
	dev_dbg(&port->dev, "value = %#x , index = %#x\n", value, index);
	if (ret < 0)
		dev_err(&port->dev, "failed to set work mode: %d\n", ret);
}

static int spcp8x5_carrier_raised(struct usb_serial_port *port)
{
	u8 msr;
	int ret;

	ret = spcp8x5_get_msr(port, &msr);
	if (ret || msr & MSR_STATUS_LINE_DCD)
		return 1;

	return 0;
}

static void spcp8x5_dtr_rts(struct usb_serial_port *port, int on)
{
	struct spcp8x5_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	u8 control;

	spin_lock_irqsave(&priv->lock, flags);
	if (on)
		priv->line_control = MCR_CONTROL_LINE_DTR
						| MCR_CONTROL_LINE_RTS;
	else
		priv->line_control &= ~ (MCR_CONTROL_LINE_DTR
						| MCR_CONTROL_LINE_RTS);
	control = priv->line_control;
	spin_unlock_irqrestore(&priv->lock, flags);
	spcp8x5_set_ctrl_line(port, control);
}

static void spcp8x5_init_termios(struct tty_struct *tty)
{
	tty->termios = tty_std_termios;
	tty->termios.c_cflag = B115200 | CS8 | CREAD | HUPCL | CLOCAL;
	tty->termios.c_ispeed = 115200;
	tty->termios.c_ospeed = 115200;
}

static void spcp8x5_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct spcp8x5_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	unsigned int cflag = tty->termios.c_cflag;
	unsigned short uartdata;
	unsigned char buf[2] = {0, 0};
	int baud;
	int i;
	u8 control;

	/* check that they really want us to change something */
	if (old_termios && !tty_termios_hw_change(&tty->termios, old_termios))
		return;

	/* set DTR/RTS active */
	spin_lock_irqsave(&priv->lock, flags);
	control = priv->line_control;
	if (old_termios && (old_termios->c_cflag & CBAUD) == B0) {
		priv->line_control |= MCR_DTR;
		if (!(old_termios->c_cflag & CRTSCTS))
			priv->line_control |= MCR_RTS;
	}
	if (control != priv->line_control) {
		control = priv->line_control;
		spin_unlock_irqrestore(&priv->lock, flags);
		spcp8x5_set_ctrl_line(port, control);
	} else {
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	/* Set Baud Rate */
	baud = tty_get_baud_rate(tty);
	switch (baud) {
	case 300:	buf[0] = 0x00;	break;
	case 600:	buf[0] = 0x01;	break;
	case 1200:	buf[0] = 0x02;	break;
	case 2400:	buf[0] = 0x03;	break;
	case 4800:	buf[0] = 0x04;	break;
	case 9600:	buf[0] = 0x05;	break;
	case 19200:	buf[0] = 0x07;	break;
	case 38400:	buf[0] = 0x09;	break;
	case 57600:	buf[0] = 0x0a;	break;
	case 115200:	buf[0] = 0x0b;	break;
	case 230400:	buf[0] = 0x0c;	break;
	case 460800:	buf[0] = 0x0d;	break;
	case 921600:	buf[0] = 0x0e;	break;
/*	case 1200000:	buf[0] = 0x0f;	break; */
/*	case 2400000:	buf[0] = 0x10;	break; */
	case 3000000:	buf[0] = 0x11;	break;
/*	case 6000000:	buf[0] = 0x12;	break; */
	case 0:
	case 1000000:
			buf[0] = 0x0b;	break;
	default:
		dev_err(&port->dev, "unsupported baudrate, using 9600\n");
	}

	/* Set Data Length : 00:5bit, 01:6bit, 10:7bit, 11:8bit */
	switch (cflag & CSIZE) {
	case CS5:
		buf[1] |= SET_UART_FORMAT_SIZE_5;
		break;
	case CS6:
		buf[1] |= SET_UART_FORMAT_SIZE_6;
		break;
	case CS7:
		buf[1] |= SET_UART_FORMAT_SIZE_7;
		break;
	default:
	case CS8:
		buf[1] |= SET_UART_FORMAT_SIZE_8;
		break;
	}

	/* Set Stop bit2 : 0:1bit 1:2bit */
	buf[1] |= (cflag & CSTOPB) ? SET_UART_FORMAT_STOP_2 :
				     SET_UART_FORMAT_STOP_1;

	/* Set Parity bit3-4 01:Odd 11:Even */
	if (cflag & PARENB) {
		buf[1] |= (cflag & PARODD) ?
		SET_UART_FORMAT_PAR_ODD : SET_UART_FORMAT_PAR_EVEN ;
	} else {
		buf[1] |= SET_UART_FORMAT_PAR_NONE;
	}
	uartdata = buf[0] | buf[1]<<8;

	i = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    SET_UART_FORMAT_TYPE, SET_UART_FORMAT,
			    uartdata, 0, NULL, 0, 100);
	if (i < 0)
		dev_err(&port->dev, "Set UART format %#x failed (error = %d)\n",
			uartdata, i);
	dev_dbg(&port->dev, "0x21:0x40:0:0  %d\n", i);

	if (cflag & CRTSCTS) {
		/* enable hardware flow control */
		spcp8x5_set_work_mode(port, 0x000a, SET_WORKING_MODE_U2C);
	}
}

static int spcp8x5_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct spcp8x5_private *priv = usb_get_serial_port_data(port);
	int ret;

	usb_clear_halt(serial->dev, port->write_urb->pipe);
	usb_clear_halt(serial->dev, port->read_urb->pipe);

	ret = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			      0x09, 0x00,
			      0x01, 0x00, NULL, 0x00, 100);
	if (ret)
		return ret;

	spcp8x5_set_ctrl_line(port, priv->line_control);

	if (tty)
		spcp8x5_set_termios(tty, port, NULL);

	return usb_serial_generic_open(tty, port);
}

static int spcp8x5_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct spcp8x5_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	u8 control;

	spin_lock_irqsave(&priv->lock, flags);
	if (set & TIOCM_RTS)
		priv->line_control |= MCR_RTS;
	if (set & TIOCM_DTR)
		priv->line_control |= MCR_DTR;
	if (clear & TIOCM_RTS)
		priv->line_control &= ~MCR_RTS;
	if (clear & TIOCM_DTR)
		priv->line_control &= ~MCR_DTR;
	control = priv->line_control;
	spin_unlock_irqrestore(&priv->lock, flags);

	return spcp8x5_set_ctrl_line(port, control);
}

static int spcp8x5_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct spcp8x5_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	unsigned int mcr;
	u8 status;
	unsigned int result;

	result = spcp8x5_get_msr(port, &status);
	if (result)
		return result;

	spin_lock_irqsave(&priv->lock, flags);
	mcr = priv->line_control;
	spin_unlock_irqrestore(&priv->lock, flags);

	result = ((mcr & MCR_DTR)			? TIOCM_DTR : 0)
		  | ((mcr & MCR_RTS)			? TIOCM_RTS : 0)
		  | ((status & MSR_STATUS_LINE_CTS)	? TIOCM_CTS : 0)
		  | ((status & MSR_STATUS_LINE_DSR)	? TIOCM_DSR : 0)
		  | ((status & MSR_STATUS_LINE_RI)	? TIOCM_RI  : 0)
		  | ((status & MSR_STATUS_LINE_DCD)	? TIOCM_CD  : 0);

	return result;
}

static struct usb_serial_driver spcp8x5_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"SPCP8x5",
	},
	.id_table		= id_table,
	.num_ports		= 1,
	.num_bulk_in		= 1,
	.num_bulk_out		= 1,
	.open			= spcp8x5_open,
	.dtr_rts		= spcp8x5_dtr_rts,
	.carrier_raised		= spcp8x5_carrier_raised,
	.set_termios		= spcp8x5_set_termios,
	.init_termios		= spcp8x5_init_termios,
	.tiocmget		= spcp8x5_tiocmget,
	.tiocmset		= spcp8x5_tiocmset,
	.probe			= spcp8x5_probe,
	.port_probe		= spcp8x5_port_probe,
	.port_remove		= spcp8x5_port_remove,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&spcp8x5_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
