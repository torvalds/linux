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
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <asm/uaccess.h>
#include "usb-serial.h"

/*
 * Version Information
 */
#define DRIVER_VERSION	"v0.3"
#define DRIVER_AUTHOR	"Roelf Diedericks"
#define DRIVER_DESC	"IPWireless tty driver"

#define IPW_TTY_MAJOR	240	/* real device node major id, experimental range */
#define IPW_TTY_MINORS	256	/* we support 256 devices, dunno why, it'd be insane :) */

#define USB_IPW_MAGIC	0x6d02	/* magic number for ipw struct */


/* Message sizes */
#define EVENT_BUFFER_SIZE	0xFF
#define CHAR2INT16(c1,c0)	(((u32)((c1) & 0xff) << 8) + (u32)((c0) & 0xff))
#define NUM_BULK_URBS		24
#define NUM_CONTROL_URBS	16

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
#define ipw_dtb_8		0x810	// ok so the define is misleading, I know, but forces 8,n,1
					// I mean, is there a point to any other setting these days? :)	

/* usb control request types : */
#define IPW_SIO_RXCTL		0x00	// control bulk rx channel transmissions, value=1/0 (on/off)
#define IPW_SIO_SET_BAUD	0x01	// set baud, value=requested ipw_sio_bxxxx
#define IPW_SIO_SET_LINE	0x03	// set databits, parity. value=ipw_dtb_x
#define IPW_SIO_SET_PIN		0x03	// set/clear dtr/rts value=ipw_pin_xxx
#define IPW_SIO_POLL		0x08	// get serial port status byte, call with value=0
#define IPW_SIO_INIT		0x11	// initializes ? value=0 (appears as first thing todo on open)
#define IPW_SIO_PURGE		0x12	// purge all transmissions?, call with value=numchar_to_purge
#define IPW_SIO_HANDFLOW	0x13	// set xon/xoff limits value=0, and a buffer of 0x10 bytes
#define IPW_SIO_SETCHARS	0x13	// set the flowcontrol special chars, value=0, buf=6 bytes, 
					// last 2 bytes contain flowcontrol chars e.g. 00 00 00 00 11 13

/* values used for request IPW_SIO_SET_PIN */
#define IPW_PIN_SETDTR		0x101
#define IPW_PIN_SETRTS		0x202
#define IPW_PIN_CLRDTR		0x100
#define IPW_PIN_CLRRTS		0x200 // unconfirmed

/* values used for request IPW_SIO_RXCTL */
#define IPW_RXBULK_ON		1
#define IPW_RXBULK_OFF		0

/* various 16 byte hardcoded transferbuffers used by flow control */
#define IPW_BYTES_FLOWINIT	{ 0x01, 0, 0, 0, 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }

/* Interpretation of modem status lines */
/* These need sorting out by individually connecting pins and checking
 * results. FIXME!
 * When data is being sent we see 0x30 in the lower byte; this must
 * contain DSR and CTS ...
 */
#define IPW_DSR			((1<<4) | (1<<5))
#define IPW_CTS			((1<<5) | (1<<4))

#define IPW_WANTS_TO_SEND	0x30
//#define IPW_DTR			/* Data Terminal Ready */
//#define IPW_CTS			/* Clear To Send */
//#define IPW_CD			/* Carrier Detect */
//#define IPW_DSR			/* Data Set Ready */
//#define IPW_RxD			/* Receive pin */

//#define IPW_LE
//#define IPW_RTS		
//#define IPW_ST		
//#define IPW_SR		
//#define IPW_RI			/* Ring Indicator */

static struct usb_device_id usb_ipw_ids[] = {
	{ USB_DEVICE(IPW_VID, IPW_PID) },
	{ },
};

MODULE_DEVICE_TABLE(usb, usb_ipw_ids);

static struct usb_driver usb_ipw_driver = {
	.name =		"ipwtty",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	usb_ipw_ids,
	.no_dynamic_id = 	1,
};

static int debug;

static void ipw_read_bulk_callback(struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	struct tty_struct *tty;
	int result;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (urb->status) {
		dbg("%s - nonzero read bulk status received: %d", __FUNCTION__, urb->status);
		return;
	}

	usb_serial_debug_data(debug, &port->dev, __FUNCTION__, urb->actual_length, data);

	tty = port->tty;
	if (tty && urb->actual_length) {
		tty_buffer_request_room(tty, urb->actual_length);
		tty_insert_flip_string(tty, data, urb->actual_length);
		tty_flip_buffer_push(tty);
	}

	/* Continue trying to always read  */
	usb_fill_bulk_urb (port->read_urb, port->serial->dev,
			   usb_rcvbulkpipe(port->serial->dev,
					   port->bulk_in_endpointAddress),
			   port->read_urb->transfer_buffer,
			   port->read_urb->transfer_buffer_length,
			   ipw_read_bulk_callback, port);
	result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
	if (result)
		dev_err(&port->dev, "%s - failed resubmitting read urb, error %d\n", __FUNCTION__, result);
	return;
}

static int ipw_open(struct usb_serial_port *port, struct file *filp)
{
	struct usb_device *dev = port->serial->dev;
	u8 buf_flow_static[16] = IPW_BYTES_FLOWINIT;
	u8 *buf_flow_init;
	int result;

	dbg("%s", __FUNCTION__);

	buf_flow_init = kmalloc(16, GFP_KERNEL);
	if (!buf_flow_init)
		return -ENOMEM;
	memcpy(buf_flow_init, buf_flow_static, 16);

	if (port->tty)
		port->tty->low_latency = 1;

	/* --1: Tell the modem to initialize (we think) From sniffs this is always the
	 * first thing that gets sent to the modem during opening of the device */
	dbg("%s: Sending SIO_INIT (we guess)",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev,0),
				 IPW_SIO_INIT,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 0,
				 0, /* index */
				 NULL,
				 0,
				 100000);
	if (result < 0)
		dev_err(&port->dev, "Init of modem failed (error = %d)", result);

	/* reset the bulk pipes */
	usb_clear_halt(dev, usb_rcvbulkpipe(dev, port->bulk_in_endpointAddress));
	usb_clear_halt(dev, usb_sndbulkpipe(dev, port->bulk_out_endpointAddress));

	/*--2: Start reading from the device */	
	dbg("%s: setting up bulk read callback",__FUNCTION__);
	usb_fill_bulk_urb(port->read_urb, dev,
			  usb_rcvbulkpipe(dev, port->bulk_in_endpointAddress),
			  port->bulk_in_buffer,
			  port->bulk_in_size,
			  ipw_read_bulk_callback, port);
	result = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (result < 0)
		dbg("%s - usb_submit_urb(read bulk) failed with status %d", __FUNCTION__, result);

	/*--3: Tell the modem to open the floodgates on the rx bulk channel */
	dbg("%s:asking modem for RxRead (RXBULK_ON)",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_RXCTL,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 IPW_RXBULK_ON,
				 0, /* index */
				 NULL,
				 0,
				 100000);
	if (result < 0) 
		dev_err(&port->dev, "Enabling bulk RxRead failed (error = %d)", result);

	/*--4: setup the initial flowcontrol */
	dbg("%s:setting init flowcontrol (%s)",__FUNCTION__,buf_flow_init);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_HANDFLOW,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 0,
				 0,
				 buf_flow_init,
				 0x10,
				 200000);
	if (result < 0)
		dev_err(&port->dev, "initial flowcontrol failed (error = %d)", result);


	/*--5: raise the dtr */
	dbg("%s:raising dtr",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_SET_PIN,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 IPW_PIN_SETDTR,
				 0,
				 NULL,
				 0,
				 200000);
	if (result < 0)
		dev_err(&port->dev, "setting dtr failed (error = %d)", result);

	/*--6: raise the rts */
	dbg("%s:raising rts",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_SET_PIN,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 IPW_PIN_SETRTS,
				 0,
				 NULL,
				 0,
				 200000);
	if (result < 0)
		dev_err(&port->dev, "setting dtr failed (error = %d)", result);
	
	kfree(buf_flow_init);
	return 0;
}

static void ipw_close(struct usb_serial_port *port, struct file * filp)
{
	struct usb_device *dev = port->serial->dev;
	int result;

	if (tty_hung_up_p(filp)) {
		dbg("%s: tty_hung_up_p ...", __FUNCTION__);
		return;
	}

	/*--1: drop the dtr */
	dbg("%s:dropping dtr",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_SET_PIN,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 IPW_PIN_CLRDTR,
				 0,
				 NULL,
				 0,
				 200000);
	if (result < 0)
		dev_err(&port->dev, "dropping dtr failed (error = %d)", result);

	/*--2: drop the rts */
	dbg("%s:dropping rts",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_SET_PIN, USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 IPW_PIN_CLRRTS,
				 0,
				 NULL,
				 0,
				 200000);
	if (result < 0)
		dev_err(&port->dev, "dropping rts failed (error = %d)", result);


	/*--3: purge */
	dbg("%s:sending purge",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_PURGE, USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 0x03,
				 0,
				 NULL,
				 0,
				 200000);
	if (result < 0)
		dev_err(&port->dev, "purge failed (error = %d)", result);


	/* send RXBULK_off (tell modem to stop transmitting bulk data on rx chan) */
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_RXCTL,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 IPW_RXBULK_OFF,
				 0, /* index */
				 NULL,
				 0,
				 100000);

	if (result < 0)
		dev_err(&port->dev, "Disabling bulk RxRead failed (error = %d)", result);

	/* shutdown any in-flight urbs that we know about */
	usb_kill_urb(port->read_urb);
	usb_kill_urb(port->write_urb);
}

static void ipw_write_bulk_callback(struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = urb->context;

	dbg("%s", __FUNCTION__);

	if (urb->status)
		dbg("%s - nonzero write bulk status received: %d", __FUNCTION__, urb->status);

	usb_serial_port_softint(port);
}

static int ipw_write(struct usb_serial_port *port, const unsigned char *buf, int count)
{
	struct usb_device *dev = port->serial->dev;
	int ret;

	dbg("%s: TOP: count=%d, in_interrupt=%ld", __FUNCTION__,
		count, in_interrupt() );

	if (count == 0) {
		dbg("%s - write request of 0 bytes", __FUNCTION__);
		return 0;
	}

	spin_lock(&port->lock);
	if (port->write_urb_busy) {
		spin_unlock(&port->lock);
		dbg("%s - already writing", __FUNCTION__);
		return 0;
	}
	port->write_urb_busy = 1;
	spin_unlock(&port->lock);

	count = min(count, port->bulk_out_size);
	memcpy(port->bulk_out_buffer, buf, count);

	dbg("%s count now:%d", __FUNCTION__, count);

	usb_fill_bulk_urb(port->write_urb, dev,
			  usb_sndbulkpipe(dev, port->bulk_out_endpointAddress),
			  port->write_urb->transfer_buffer,
			  count,
			  ipw_write_bulk_callback,
			  port);

	ret = usb_submit_urb(port->write_urb, GFP_ATOMIC);
	if (ret != 0) {
		port->write_urb_busy = 0;
		dbg("%s - usb_submit_urb(write bulk) failed with error = %d", __FUNCTION__, ret);
		return ret;
	}

	dbg("%s returning %d", __FUNCTION__, count);
	return count;
} 

static int ipw_probe(struct usb_serial_port *port)
{
	return 0;
}

static int ipw_disconnect(struct usb_serial_port *port)
{
	usb_set_serial_port_data(port, NULL);
	return 0;
}

static struct usb_serial_driver ipw_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"ipw",
	},
	.description =		"IPWireless converter",
	.id_table =		usb_ipw_ids,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		1,
	.num_bulk_out =		1,
	.num_ports =		1,
	.open =			ipw_open,
	.close =		ipw_close,
	.port_probe = 		ipw_probe,
	.port_remove =		ipw_disconnect,
	.write =		ipw_write,
	.write_bulk_callback =	ipw_write_bulk_callback,
	.read_bulk_callback =	ipw_read_bulk_callback,
};



static int usb_ipw_init(void)
{
	int retval;

	retval = usb_serial_register(&ipw_device);
	if (retval)
		return retval;
	retval = usb_register(&usb_ipw_driver);
	if (retval) {
		usb_serial_deregister(&ipw_device);
		return retval;
	}
	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
}

static void usb_ipw_exit(void)
{
	usb_deregister(&usb_ipw_driver);
	usb_serial_deregister(&ipw_device);
}

module_init(usb_ipw_init);
module_exit(usb_ipw_exit);

/* Module information */
MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
