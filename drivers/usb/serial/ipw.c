/*
 * IPWireless 3G UMTS TDD Modem driver (USB connected)
 *
 *   Copyright (C) 2004 Roelf Diedericks <roelfd@inet.co.za>
 *   Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 * All information about the device was acquired using SnoopyPro
 * on MSFT's O/S, and examing the MSFT drivers' debug output
 * (insanely left _on_ in the enduser version)
 *
 * It was written out of frustration with the IPWireless USB modem
 * supplied by Axity3G/Sentech South Africa not supporting
 * Linux whatsoever.
 *
 * Nobody provided any proprietary information that was not already
 * available for this device.
 *
 * The modem adheres to the "3GPP TS  27.007 AT command set for 3G
 * User Equipment (UE)" standard, available from
 * http://www.3gpp.org/ftp/Specs/html-info/27007.htm
 *
 * The code was only tested the IPWireless handheld modem distributed
 * in South Africa by Sentech.
 *
 * It may work for Woosh Inc in .nz too, as it appears they use the
 * same kit.
 *
 * There is still some work to be done in terms of handling
 * DCD, DTR, RTS, CTS which are currently faked.
 * It's good enough for PPP at this point. It's based off all kinds of
 * code found in usb/serial and usb/class
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/uaccess.h>
#include "usb-wwan.h"

#define DRIVER_AUTHOR	"Roelf Diedericks"
#define DRIVER_DESC	"IPWireless tty driver"

#define IPW_TTY_MAJOR	240	/* real device node major id, experimental range */
#define IPW_TTY_MINORS	256	/* we support 256 devices, dunno why, it'd be insane :) */

#define USB_IPW_MAGIC	0x6d02	/* magic number for ipw struct */


/* Message sizes */
#define EVENT_BUFFER_SIZE	0xFF
#define CHAR2INT16(c1, c0)	(((u32)((c1) & 0xff) << 8) + (u32)((c0) & 0xff))

/* vendor/product pairs that are known work with this driver*/
#define IPW_VID		0x0bc3
#define IPW_PID		0x0001


/* Vendor commands: */

/* baud rates */
enum {
	ipw_sio_b256000 = 0x000e,
	ipw_sio_b128000 = 0x001d,
	ipw_sio_b115200 = 0x0020,
	ipw_sio_b57600  = 0x0040,
	ipw_sio_b56000  = 0x0042,
	ipw_sio_b38400  = 0x0060,
	ipw_sio_b19200  = 0x00c0,
	ipw_sio_b14400  = 0x0100,
	ipw_sio_b9600   = 0x0180,
	ipw_sio_b4800   = 0x0300,
	ipw_sio_b2400   = 0x0600,
	ipw_sio_b1200   = 0x0c00,
	ipw_sio_b600    = 0x1800
};

/* data bits */
#define ipw_dtb_7		0x700
#define ipw_dtb_8		0x810	/* ok so the define is misleading, I know, but forces 8,n,1 */
					/* I mean, is there a point to any other setting these days? :) */

/* usb control request types : */
#define IPW_SIO_RXCTL		0x00	/* control bulk rx channel transmissions, value=1/0 (on/off) */
#define IPW_SIO_SET_BAUD	0x01	/* set baud, value=requested ipw_sio_bxxxx */
#define IPW_SIO_SET_LINE	0x03	/* set databits, parity. value=ipw_dtb_x */
#define IPW_SIO_SET_PIN		0x03	/* set/clear dtr/rts value=ipw_pin_xxx */
#define IPW_SIO_POLL		0x08	/* get serial port status byte, call with value=0 */
#define IPW_SIO_INIT		0x11	/* initializes ? value=0 (appears as first thing todo on open) */
#define IPW_SIO_PURGE		0x12	/* purge all transmissions?, call with value=numchar_to_purge */
#define IPW_SIO_HANDFLOW	0x13	/* set xon/xoff limits value=0, and a buffer of 0x10 bytes */
#define IPW_SIO_SETCHARS	0x13	/* set the flowcontrol special chars, value=0, buf=6 bytes, */
					/* last 2 bytes contain flowcontrol chars e.g. 00 00 00 00 11 13 */

/* values used for request IPW_SIO_SET_PIN */
#define IPW_PIN_SETDTR		0x101
#define IPW_PIN_SETRTS		0x202
#define IPW_PIN_CLRDTR		0x100
#define IPW_PIN_CLRRTS		0x200 /* unconfirmed */

/* values used for request IPW_SIO_RXCTL */
#define IPW_RXBULK_ON		1
#define IPW_RXBULK_OFF		0

/* various 16 byte hardcoded transferbuffers used by flow control */
#define IPW_BYTES_FLOWINIT	{ 0x01, 0, 0, 0, 0x40, 0, 0, 0, \
					0, 0, 0, 0, 0, 0, 0, 0 }

/* Interpretation of modem status lines */
/* These need sorting out by individually connecting pins and checking
 * results. FIXME!
 * When data is being sent we see 0x30 in the lower byte; this must
 * contain DSR and CTS ...
 */
#define IPW_DSR			((1<<4) | (1<<5))
#define IPW_CTS			((1<<5) | (1<<4))

#define IPW_WANTS_TO_SEND	0x30

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(IPW_VID, IPW_PID) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

static int ipw_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_device *udev = port->serial->dev;
	struct device *dev = &port->dev;
	u8 buf_flow_static[16] = IPW_BYTES_FLOWINIT;
	u8 *buf_flow_init;
	int result;

	buf_flow_init = kmemdup(buf_flow_static, 16, GFP_KERNEL);
	if (!buf_flow_init)
		return -ENOMEM;

	/* --1: Tell the modem to initialize (we think) From sniffs this is
	 *	always the first thing that gets sent to the modem during
	 *	opening of the device */
	dev_dbg(dev, "%s: Sending SIO_INIT (we guess)\n", __func__);
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			 IPW_SIO_INIT,
			 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
			 0,
			 0, /* index */
			 NULL,
			 0,
			 100000);
	if (result < 0)
		dev_err(dev, "Init of modem failed (error = %d)\n", result);

	/* reset the bulk pipes */
	usb_clear_halt(udev, usb_rcvbulkpipe(udev, port->bulk_in_endpointAddress));
	usb_clear_halt(udev, usb_sndbulkpipe(udev, port->bulk_out_endpointAddress));

	/*--2: Start reading from the device */
	dev_dbg(dev, "%s: setting up bulk read callback\n", __func__);
	usb_wwan_open(tty, port);

	/*--3: Tell the modem to open the floodgates on the rx bulk channel */
	dev_dbg(dev, "%s:asking modem for RxRead (RXBULK_ON)\n", __func__);
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			 IPW_SIO_RXCTL,
			 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
			 IPW_RXBULK_ON,
			 0, /* index */
			 NULL,
			 0,
			 100000);
	if (result < 0)
		dev_err(dev, "Enabling bulk RxRead failed (error = %d)\n", result);

	/*--4: setup the initial flowcontrol */
	dev_dbg(dev, "%s:setting init flowcontrol (%s)\n", __func__, buf_flow_init);
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			 IPW_SIO_HANDFLOW,
			 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
			 0,
			 0,
			 buf_flow_init,
			 0x10,
			 200000);
	if (result < 0)
		dev_err(dev, "initial flowcontrol failed (error = %d)\n", result);

	kfree(buf_flow_init);
	return 0;
}

static int ipw_attach(struct usb_serial *serial)
{
	struct usb_wwan_intf_private *data;

	data = kzalloc(sizeof(struct usb_wwan_intf_private), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	spin_lock_init(&data->susp_lock);
	usb_set_serial_data(serial, data);
	return 0;
}

static void ipw_release(struct usb_serial *serial)
{
	struct usb_wwan_intf_private *data = usb_get_serial_data(serial);

	usb_set_serial_data(serial, NULL);
	kfree(data);
}

static void ipw_dtr_rts(struct usb_serial_port *port, int on)
{
	struct usb_device *udev = port->serial->dev;
	struct device *dev = &port->dev;
	int result;

	dev_dbg(dev, "%s: on = %d\n", __func__, on);

	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			 IPW_SIO_SET_PIN,
			 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
			 on ? IPW_PIN_SETDTR : IPW_PIN_CLRDTR,
			 0,
			 NULL,
			 0,
			 200000);
	if (result < 0)
		dev_err(dev, "setting dtr failed (error = %d)\n", result);

	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			 IPW_SIO_SET_PIN, USB_TYPE_VENDOR |
					USB_RECIP_INTERFACE | USB_DIR_OUT,
			 on ? IPW_PIN_SETRTS : IPW_PIN_CLRRTS,
			 0,
			 NULL,
			 0,
			 200000);
	if (result < 0)
		dev_err(dev, "setting rts failed (error = %d)\n", result);
}

static void ipw_close(struct usb_serial_port *port)
{
	struct usb_device *udev = port->serial->dev;
	struct device *dev = &port->dev;
	int result;

	/*--3: purge */
	dev_dbg(dev, "%s:sending purge\n", __func__);
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			 IPW_SIO_PURGE, USB_TYPE_VENDOR |
			 		USB_RECIP_INTERFACE | USB_DIR_OUT,
			 0x03,
			 0,
			 NULL,
			 0,
			 200000);
	if (result < 0)
		dev_err(dev, "purge failed (error = %d)\n", result);


	/* send RXBULK_off (tell modem to stop transmitting bulk data on
	   rx chan) */
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			 IPW_SIO_RXCTL,
			 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
			 IPW_RXBULK_OFF,
			 0, /* index */
			 NULL,
			 0,
			 100000);

	if (result < 0)
		dev_err(dev, "Disabling bulk RxRead failed (error = %d)\n", result);

	usb_wwan_close(port);
}

static struct usb_serial_driver ipw_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"ipw",
	},
	.description =		"IPWireless converter",
	.id_table =		id_table,
	.num_ports =		1,
	.open =			ipw_open,
	.close =		ipw_close,
	.attach =		ipw_attach,
	.release =		ipw_release,
	.port_probe =		usb_wwan_port_probe,
	.port_remove =		usb_wwan_port_remove,
	.dtr_rts =		ipw_dtr_rts,
	.write =		usb_wwan_write,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&ipw_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

/* Module information */
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
