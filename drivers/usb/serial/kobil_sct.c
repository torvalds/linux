/*
 *  KOBIL USB Smart Card Terminal Driver
 *
 *  Copyright (C) 2002  KOBIL Systems GmbH
 *  Author: Thomas Wahrenbruch
 *
 *  Contact: linuxusb@kobil.de
 *
 *  This program is largely derived from work by the linux-usb group
 *  and associated source files.  Please see the usb/serial files for
 *  individual credits and copyrights.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Thanks to Greg Kroah-Hartman (greg@kroah.com) for his help and
 *  patience.
 *
 * Supported readers: USB TWIN, KAAN Standard Plus and SecOVID Reader Plus
 * (Adapter K), B1 Professional and KAAN Professional (Adapter B)
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
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/ioctl.h>
#include "kobil_sct.h"

#define DRIVER_AUTHOR "KOBIL Systems GmbH - http://www.kobil.com"
#define DRIVER_DESC "KOBIL USB Smart Card Terminal Driver (experimental)"

#define KOBIL_VENDOR_ID			0x0D46
#define KOBIL_ADAPTER_B_PRODUCT_ID	0x2011
#define KOBIL_ADAPTER_K_PRODUCT_ID	0x2012
#define KOBIL_USBTWIN_PRODUCT_ID	0x0078
#define KOBIL_KAAN_SIM_PRODUCT_ID       0x0081

#define KOBIL_TIMEOUT		500
#define KOBIL_BUF_LENGTH	300


/* Function prototypes */
static int kobil_port_probe(struct usb_serial_port *probe);
static int kobil_port_remove(struct usb_serial_port *probe);
static int  kobil_open(struct tty_struct *tty, struct usb_serial_port *port);
static void kobil_close(struct usb_serial_port *port);
static int  kobil_write(struct tty_struct *tty, struct usb_serial_port *port,
			 const unsigned char *buf, int count);
static int  kobil_write_room(struct tty_struct *tty);
static int  kobil_ioctl(struct tty_struct *tty,
			unsigned int cmd, unsigned long arg);
static int  kobil_tiocmget(struct tty_struct *tty);
static int  kobil_tiocmset(struct tty_struct *tty,
			   unsigned int set, unsigned int clear);
static void kobil_read_int_callback(struct urb *urb);
static void kobil_write_int_callback(struct urb *urb);
static void kobil_set_termios(struct tty_struct *tty,
			struct usb_serial_port *port, struct ktermios *old);
static void kobil_init_termios(struct tty_struct *tty);

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(KOBIL_VENDOR_ID, KOBIL_ADAPTER_B_PRODUCT_ID) },
	{ USB_DEVICE(KOBIL_VENDOR_ID, KOBIL_ADAPTER_K_PRODUCT_ID) },
	{ USB_DEVICE(KOBIL_VENDOR_ID, KOBIL_USBTWIN_PRODUCT_ID) },
	{ USB_DEVICE(KOBIL_VENDOR_ID, KOBIL_KAAN_SIM_PRODUCT_ID) },
	{ }			/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_serial_driver kobil_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"kobil",
	},
	.description =		"KOBIL USB smart card terminal",
	.id_table =		id_table,
	.num_ports =		1,
	.port_probe =		kobil_port_probe,
	.port_remove =		kobil_port_remove,
	.ioctl =		kobil_ioctl,
	.set_termios =		kobil_set_termios,
	.init_termios =		kobil_init_termios,
	.tiocmget =		kobil_tiocmget,
	.tiocmset =		kobil_tiocmset,
	.open =			kobil_open,
	.close =		kobil_close,
	.write =		kobil_write,
	.write_room =		kobil_write_room,
	.read_int_callback =	kobil_read_int_callback,
	.write_int_callback =	kobil_write_int_callback,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&kobil_device, NULL
};

struct kobil_private {
	unsigned char buf[KOBIL_BUF_LENGTH]; /* buffer for the APDU to send */
	int filled;  /* index of the last char in buf */
	int cur_pos; /* index of the next char to send in buf */
	__u16 device_type;
};


static int kobil_port_probe(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct kobil_private *priv;

	priv = kmalloc(sizeof(struct kobil_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->filled = 0;
	priv->cur_pos = 0;
	priv->device_type = le16_to_cpu(serial->dev->descriptor.idProduct);

	switch (priv->device_type) {
	case KOBIL_ADAPTER_B_PRODUCT_ID:
		dev_dbg(&serial->dev->dev, "KOBIL B1 PRO / KAAN PRO detected\n");
		break;
	case KOBIL_ADAPTER_K_PRODUCT_ID:
		dev_dbg(&serial->dev->dev, "KOBIL KAAN Standard Plus / SecOVID Reader Plus detected\n");
		break;
	case KOBIL_USBTWIN_PRODUCT_ID:
		dev_dbg(&serial->dev->dev, "KOBIL USBTWIN detected\n");
		break;
	case KOBIL_KAAN_SIM_PRODUCT_ID:
		dev_dbg(&serial->dev->dev, "KOBIL KAAN SIM detected\n");
		break;
	}
	usb_set_serial_port_data(port, priv);

	return 0;
}


static int kobil_port_remove(struct usb_serial_port *port)
{
	struct kobil_private *priv;

	priv = usb_get_serial_port_data(port);
	kfree(priv);

	return 0;
}

static void kobil_init_termios(struct tty_struct *tty)
{
	/* Default to echo off and other sane device settings */
	tty->termios.c_lflag = 0;
	tty->termios.c_iflag &= ~(ISIG | ICANON | ECHO | IEXTEN | XCASE);
	tty->termios.c_iflag |= IGNBRK | IGNPAR | IXOFF;
	/* do NOT translate CR to CR-NL (0x0A -> 0x0A 0x0D) */
	tty->termios.c_oflag &= ~ONLCR;
}

static int kobil_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct device *dev = &port->dev;
	int result = 0;
	struct kobil_private *priv;
	unsigned char *transfer_buffer;
	int transfer_buffer_length = 8;

	priv = usb_get_serial_port_data(port);

	/* allocate memory for transfer buffer */
	transfer_buffer = kzalloc(transfer_buffer_length, GFP_KERNEL);
	if (!transfer_buffer)
		return -ENOMEM;

	/* get hardware version */
	result = usb_control_msg(port->serial->dev,
			  usb_rcvctrlpipe(port->serial->dev, 0),
			  SUSBCRequest_GetMisc,
			  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_IN,
			  SUSBCR_MSC_GetHWVersion,
			  0,
			  transfer_buffer,
			  transfer_buffer_length,
			  KOBIL_TIMEOUT
	);
	dev_dbg(dev, "%s - Send get_HW_version URB returns: %i\n", __func__, result);
	dev_dbg(dev, "Harware version: %i.%i.%i\n", transfer_buffer[0],
		transfer_buffer[1], transfer_buffer[2]);

	/* get firmware version */
	result = usb_control_msg(port->serial->dev,
			  usb_rcvctrlpipe(port->serial->dev, 0),
			  SUSBCRequest_GetMisc,
			  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_IN,
			  SUSBCR_MSC_GetFWVersion,
			  0,
			  transfer_buffer,
			  transfer_buffer_length,
			  KOBIL_TIMEOUT
	);
	dev_dbg(dev, "%s - Send get_FW_version URB returns: %i\n", __func__, result);
	dev_dbg(dev, "Firmware version: %i.%i.%i\n", transfer_buffer[0],
		transfer_buffer[1], transfer_buffer[2]);

	if (priv->device_type == KOBIL_ADAPTER_B_PRODUCT_ID ||
			priv->device_type == KOBIL_ADAPTER_K_PRODUCT_ID) {
		/* Setting Baudrate, Parity and Stopbits */
		result = usb_control_msg(port->serial->dev,
			  usb_rcvctrlpipe(port->serial->dev, 0),
			  SUSBCRequest_SetBaudRateParityAndStopBits,
			  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_OUT,
			  SUSBCR_SBR_9600 | SUSBCR_SPASB_EvenParity |
							SUSBCR_SPASB_1StopBit,
			  0,
			  transfer_buffer,
			  0,
			  KOBIL_TIMEOUT
		);
		dev_dbg(dev, "%s - Send set_baudrate URB returns: %i\n", __func__, result);

		/* reset all queues */
		result = usb_control_msg(port->serial->dev,
			  usb_rcvctrlpipe(port->serial->dev, 0),
			  SUSBCRequest_Misc,
			  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_OUT,
			  SUSBCR_MSC_ResetAllQueues,
			  0,
			  transfer_buffer,
			  0,
			  KOBIL_TIMEOUT
		);
		dev_dbg(dev, "%s - Send reset_all_queues URB returns: %i\n", __func__, result);
	}
	if (priv->device_type == KOBIL_USBTWIN_PRODUCT_ID ||
	    priv->device_type == KOBIL_ADAPTER_B_PRODUCT_ID ||
	    priv->device_type == KOBIL_KAAN_SIM_PRODUCT_ID) {
		/* start reading (Adapter B 'cause PNP string) */
		result = usb_submit_urb(port->interrupt_in_urb, GFP_ATOMIC);
		dev_dbg(dev, "%s - Send read URB returns: %i\n", __func__, result);
	}

	kfree(transfer_buffer);
	return 0;
}


static void kobil_close(struct usb_serial_port *port)
{
	/* FIXME: Add rts/dtr methods */
	usb_kill_urb(port->interrupt_out_urb);
	usb_kill_urb(port->interrupt_in_urb);
}


static void kobil_read_int_callback(struct urb *urb)
{
	int result;
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	int status = urb->status;

	if (status) {
		dev_dbg(&port->dev, "%s - Read int status not zero: %d\n", __func__, status);
		return;
	}

	if (urb->actual_length) {
		usb_serial_debug_data(&port->dev, __func__, urb->actual_length,
									data);
		tty_insert_flip_string(&port->port, data, urb->actual_length);
		tty_flip_buffer_push(&port->port);
	}

	result = usb_submit_urb(port->interrupt_in_urb, GFP_ATOMIC);
	dev_dbg(&port->dev, "%s - Send read URB returns: %i\n", __func__, result);
}


static void kobil_write_int_callback(struct urb *urb)
{
}


static int kobil_write(struct tty_struct *tty, struct usb_serial_port *port,
			const unsigned char *buf, int count)
{
	int length = 0;
	int result = 0;
	int todo = 0;
	struct kobil_private *priv;

	if (count == 0) {
		dev_dbg(&port->dev, "%s - write request of 0 bytes\n", __func__);
		return 0;
	}

	priv = usb_get_serial_port_data(port);

	if (count > (KOBIL_BUF_LENGTH - priv->filled)) {
		dev_dbg(&port->dev, "%s - Error: write request bigger than buffer size\n", __func__);
		return -ENOMEM;
	}

	/* Copy data to buffer */
	memcpy(priv->buf + priv->filled, buf, count);
	usb_serial_debug_data(&port->dev, __func__, count, priv->buf + priv->filled);
	priv->filled = priv->filled + count;

	/* only send complete block. TWIN, KAAN SIM and adapter K
	   use the same protocol. */
	if (((priv->device_type != KOBIL_ADAPTER_B_PRODUCT_ID) && (priv->filled > 2) && (priv->filled >= (priv->buf[1] + 3))) ||
	     ((priv->device_type == KOBIL_ADAPTER_B_PRODUCT_ID) && (priv->filled > 3) && (priv->filled >= (priv->buf[2] + 4)))) {
		/* stop reading (except TWIN and KAAN SIM) */
		if ((priv->device_type == KOBIL_ADAPTER_B_PRODUCT_ID)
			|| (priv->device_type == KOBIL_ADAPTER_K_PRODUCT_ID))
			usb_kill_urb(port->interrupt_in_urb);

		todo = priv->filled - priv->cur_pos;

		while (todo > 0) {
			/* max 8 byte in one urb (endpoint size) */
			length = min(todo, port->interrupt_out_size);
			/* copy data to transfer buffer */
			memcpy(port->interrupt_out_buffer,
					priv->buf + priv->cur_pos, length);
			port->interrupt_out_urb->transfer_buffer_length = length;

			priv->cur_pos = priv->cur_pos + length;
			result = usb_submit_urb(port->interrupt_out_urb, GFP_NOIO);
			dev_dbg(&port->dev, "%s - Send write URB returns: %i\n", __func__, result);
			todo = priv->filled - priv->cur_pos;

			if (todo > 0)
				msleep(24);
		}

		priv->filled = 0;
		priv->cur_pos = 0;

		/* start reading (except TWIN and KAAN SIM) */
		if (priv->device_type == KOBIL_ADAPTER_B_PRODUCT_ID ||
			priv->device_type == KOBIL_ADAPTER_K_PRODUCT_ID) {
			result = usb_submit_urb(port->interrupt_in_urb,
								GFP_NOIO);
			dev_dbg(&port->dev, "%s - Send read URB returns: %i\n", __func__, result);
		}
	}
	return count;
}


static int kobil_write_room(struct tty_struct *tty)
{
	/* FIXME */
	return 8;
}


static int kobil_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct kobil_private *priv;
	int result;
	unsigned char *transfer_buffer;
	int transfer_buffer_length = 8;

	priv = usb_get_serial_port_data(port);
	if (priv->device_type == KOBIL_USBTWIN_PRODUCT_ID
			|| priv->device_type == KOBIL_KAAN_SIM_PRODUCT_ID) {
		/* This device doesn't support ioctl calls */
		return -EINVAL;
	}

	/* allocate memory for transfer buffer */
	transfer_buffer = kzalloc(transfer_buffer_length, GFP_KERNEL);
	if (!transfer_buffer)
		return -ENOMEM;

	result = usb_control_msg(port->serial->dev,
			  usb_rcvctrlpipe(port->serial->dev, 0),
			  SUSBCRequest_GetStatusLineState,
			  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_IN,
			  0,
			  0,
			  transfer_buffer,
			  transfer_buffer_length,
			  KOBIL_TIMEOUT);

	dev_dbg(&port->dev, "%s - Send get_status_line_state URB returns: %i. Statusline: %02x\n",
		__func__, result, transfer_buffer[0]);

	result = 0;
	if ((transfer_buffer[0] & SUSBCR_GSL_DSR) != 0)
		result = TIOCM_DSR;
	kfree(transfer_buffer);
	return result;
}

static int kobil_tiocmset(struct tty_struct *tty,
			   unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct device *dev = &port->dev;
	struct kobil_private *priv;
	int result;
	int dtr = 0;
	int rts = 0;
	unsigned char *transfer_buffer;
	int transfer_buffer_length = 8;

	/* FIXME: locking ? */
	priv = usb_get_serial_port_data(port);
	if (priv->device_type == KOBIL_USBTWIN_PRODUCT_ID
		|| priv->device_type == KOBIL_KAAN_SIM_PRODUCT_ID) {
		/* This device doesn't support ioctl calls */
		return -EINVAL;
	}

	/* allocate memory for transfer buffer */
	transfer_buffer = kzalloc(transfer_buffer_length, GFP_KERNEL);
	if (!transfer_buffer)
		return -ENOMEM;

	if (set & TIOCM_RTS)
		rts = 1;
	if (set & TIOCM_DTR)
		dtr = 1;
	if (clear & TIOCM_RTS)
		rts = 0;
	if (clear & TIOCM_DTR)
		dtr = 0;

	if (priv->device_type == KOBIL_ADAPTER_B_PRODUCT_ID) {
		if (dtr != 0)
			dev_dbg(dev, "%s - Setting DTR\n", __func__);
		else
			dev_dbg(dev, "%s - Clearing DTR\n", __func__);
		result = usb_control_msg(port->serial->dev,
			  usb_rcvctrlpipe(port->serial->dev, 0),
			  SUSBCRequest_SetStatusLinesOrQueues,
			  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_OUT,
			  ((dtr != 0) ? SUSBCR_SSL_SETDTR : SUSBCR_SSL_CLRDTR),
			  0,
			  transfer_buffer,
			  0,
			  KOBIL_TIMEOUT);
	} else {
		if (rts != 0)
			dev_dbg(dev, "%s - Setting RTS\n", __func__);
		else
			dev_dbg(dev, "%s - Clearing RTS\n", __func__);
		result = usb_control_msg(port->serial->dev,
			usb_rcvctrlpipe(port->serial->dev, 0),
			SUSBCRequest_SetStatusLinesOrQueues,
			USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_OUT,
			((rts != 0) ? SUSBCR_SSL_SETRTS : SUSBCR_SSL_CLRRTS),
			0,
			transfer_buffer,
			0,
			KOBIL_TIMEOUT);
	}
	dev_dbg(dev, "%s - Send set_status_line URB returns: %i\n", __func__, result);
	kfree(transfer_buffer);
	return (result < 0) ? result : 0;
}

static void kobil_set_termios(struct tty_struct *tty,
			struct usb_serial_port *port, struct ktermios *old)
{
	struct kobil_private *priv;
	int result;
	unsigned short urb_val = 0;
	int c_cflag = tty->termios.c_cflag;
	speed_t speed;

	priv = usb_get_serial_port_data(port);
	if (priv->device_type == KOBIL_USBTWIN_PRODUCT_ID ||
			priv->device_type == KOBIL_KAAN_SIM_PRODUCT_ID) {
		/* This device doesn't support ioctl calls */
		tty_termios_copy_hw(&tty->termios, old);
		return;
	}

	speed = tty_get_baud_rate(tty);
	switch (speed) {
	case 1200:
		urb_val = SUSBCR_SBR_1200;
		break;
	default:
		speed = 9600;
	case 9600:
		urb_val = SUSBCR_SBR_9600;
		break;
	}
	urb_val |= (c_cflag & CSTOPB) ? SUSBCR_SPASB_2StopBits :
							SUSBCR_SPASB_1StopBit;
	if (c_cflag & PARENB) {
		if  (c_cflag & PARODD)
			urb_val |= SUSBCR_SPASB_OddParity;
		else
			urb_val |= SUSBCR_SPASB_EvenParity;
	} else
		urb_val |= SUSBCR_SPASB_NoParity;
	tty->termios.c_cflag &= ~CMSPAR;
	tty_encode_baud_rate(tty, speed, speed);

	result = usb_control_msg(port->serial->dev,
		  usb_rcvctrlpipe(port->serial->dev, 0),
		  SUSBCRequest_SetBaudRateParityAndStopBits,
		  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_OUT,
		  urb_val,
		  0,
		  NULL,
		  0,
		  KOBIL_TIMEOUT
		);
}

static int kobil_ioctl(struct tty_struct *tty,
					unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct kobil_private *priv = usb_get_serial_port_data(port);
	unsigned char *transfer_buffer;
	int transfer_buffer_length = 8;
	int result;

	if (priv->device_type == KOBIL_USBTWIN_PRODUCT_ID ||
			priv->device_type == KOBIL_KAAN_SIM_PRODUCT_ID)
		/* This device doesn't support ioctl calls */
		return -ENOIOCTLCMD;

	switch (cmd) {
	case TCFLSH:
		transfer_buffer = kmalloc(transfer_buffer_length, GFP_KERNEL);
		if (!transfer_buffer)
			return -ENOBUFS;

		result = usb_control_msg(port->serial->dev,
			  usb_rcvctrlpipe(port->serial->dev, 0),
			  SUSBCRequest_Misc,
			  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_OUT,
			  SUSBCR_MSC_ResetAllQueues,
			  0,
			  NULL, /* transfer_buffer, */
			  0,
			  KOBIL_TIMEOUT
			);

		dev_dbg(&port->dev,
			"%s - Send reset_all_queues (FLUSH) URB returns: %i", __func__, result);
		kfree(transfer_buffer);
		return (result < 0) ? -EIO: 0;
	default:
		return -ENOIOCTLCMD;
	}
}

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
