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
 * 
 * (21/05/2004) tw
 *      Fix bug with P'n'P readers
 *
 * (28/05/2003) tw
 *      Add support for KAAN SIM
 *
 * (12/09/2002) tw
 *      Adapted to 2.5.
 *
 * (11/08/2002) tw
 *      Initial version.
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
#include <linux/ioctl.h>
#include "kobil_sct.h"

static int debug;

/* Version Information */
#define DRIVER_VERSION "21/05/2004"
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
static int  kobil_startup (struct usb_serial *serial);
static void kobil_shutdown (struct usb_serial *serial);
static int  kobil_open (struct usb_serial_port *port, struct file *filp);
static void kobil_close (struct usb_serial_port *port, struct file *filp);
static int  kobil_write (struct usb_serial_port *port, 
			 const unsigned char *buf, int count);
static int  kobil_write_room(struct usb_serial_port *port);
static int  kobil_ioctl(struct usb_serial_port *port, struct file *file,
			unsigned int cmd, unsigned long arg);
static int  kobil_tiocmget(struct usb_serial_port *port, struct file *file);
static int  kobil_tiocmset(struct usb_serial_port *port, struct file *file,
			   unsigned int set, unsigned int clear);
static void kobil_read_int_callback( struct urb *urb );
static void kobil_write_callback( struct urb *purb );


static struct usb_device_id id_table [] = {
	{ USB_DEVICE(KOBIL_VENDOR_ID, KOBIL_ADAPTER_B_PRODUCT_ID) },
	{ USB_DEVICE(KOBIL_VENDOR_ID, KOBIL_ADAPTER_K_PRODUCT_ID) },
	{ USB_DEVICE(KOBIL_VENDOR_ID, KOBIL_USBTWIN_PRODUCT_ID) },
	{ USB_DEVICE(KOBIL_VENDOR_ID, KOBIL_KAAN_SIM_PRODUCT_ID) },
	{ }			/* Terminating entry */
};


MODULE_DEVICE_TABLE (usb, id_table);

static struct usb_driver kobil_driver = {
	.name =		"kobil",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id = 	1,
};


static struct usb_serial_driver kobil_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"kobil",
	},
	.description =		"KOBIL USB smart card terminal",
	.id_table =		id_table,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		0,
	.num_bulk_out =		0,
	.num_ports =		1,
	.attach =		kobil_startup,
	.shutdown =		kobil_shutdown,
	.ioctl =		kobil_ioctl,
	.tiocmget =		kobil_tiocmget,
	.tiocmset =		kobil_tiocmset,
	.open =			kobil_open,
	.close =		kobil_close,
	.write =		kobil_write,
	.write_room =		kobil_write_room,
	.read_int_callback =	kobil_read_int_callback,
};


struct kobil_private {
	int write_int_endpoint_address;
	int read_int_endpoint_address;
	unsigned char buf[KOBIL_BUF_LENGTH]; // buffer for the APDU to send
	int filled;  // index of the last char in buf
	int cur_pos; // index of the next char to send in buf
	__u16 device_type;
	int line_state;
	struct termios internal_termios;
};


static int kobil_startup (struct usb_serial *serial)
{
	int i;
	struct kobil_private *priv;
	struct usb_device *pdev;
	struct usb_host_config *actconfig;
	struct usb_interface *interface;
	struct usb_host_interface *altsetting;
	struct usb_host_endpoint *endpoint;

	priv = kmalloc(sizeof(struct kobil_private), GFP_KERNEL);
	if (!priv){
		return -ENOMEM;
	}

	priv->filled = 0;
	priv->cur_pos = 0;
	priv->device_type = le16_to_cpu(serial->dev->descriptor.idProduct);
	priv->line_state = 0;

	switch (priv->device_type){
	case KOBIL_ADAPTER_B_PRODUCT_ID:
		printk(KERN_DEBUG "KOBIL B1 PRO / KAAN PRO detected\n");
		break;
	case KOBIL_ADAPTER_K_PRODUCT_ID:
		printk(KERN_DEBUG "KOBIL KAAN Standard Plus / SecOVID Reader Plus detected\n");
		break;
	case KOBIL_USBTWIN_PRODUCT_ID:
		printk(KERN_DEBUG "KOBIL USBTWIN detected\n");
		break;
	case KOBIL_KAAN_SIM_PRODUCT_ID:
		printk(KERN_DEBUG "KOBIL KAAN SIM detected\n");
		break;
	}
	usb_set_serial_port_data(serial->port[0], priv);

	// search for the necessary endpoints
	pdev = serial->dev;
 	actconfig = pdev->actconfig;
 	interface = actconfig->interface[0];
	altsetting = interface->cur_altsetting;
 	endpoint = altsetting->endpoint;
  
 	for (i = 0; i < altsetting->desc.bNumEndpoints; i++) {
		endpoint = &altsetting->endpoint[i];
		if (usb_endpoint_is_int_out(&endpoint->desc)) {
		 	dbg("%s Found interrupt out endpoint. Address: %d", __FUNCTION__, endpoint->desc.bEndpointAddress);
		 	priv->write_int_endpoint_address = endpoint->desc.bEndpointAddress;
 		}
		if (usb_endpoint_is_int_in(&endpoint->desc)) {
		 	dbg("%s Found interrupt in  endpoint. Address: %d", __FUNCTION__, endpoint->desc.bEndpointAddress);
		 	priv->read_int_endpoint_address = endpoint->desc.bEndpointAddress;
	 	}
	}
	return 0;
}


static void kobil_shutdown (struct usb_serial *serial)
{
	int i;
	dbg("%s - port %d", __FUNCTION__, serial->port[0]->number);

	for (i=0; i < serial->num_ports; ++i) {
		while (serial->port[i]->open_count > 0) {
			kobil_close (serial->port[i], NULL);
		}
		kfree(usb_get_serial_port_data(serial->port[i]));
		usb_set_serial_port_data(serial->port[i], NULL);
	}
}


static int kobil_open (struct usb_serial_port *port, struct file *filp)
{
	int i, result = 0;
	struct kobil_private *priv;
	unsigned char *transfer_buffer;
	int transfer_buffer_length = 8;
	int write_urb_transfer_buffer_length = 8;

	dbg("%s - port %d", __FUNCTION__, port->number);
	priv = usb_get_serial_port_data(port);
	priv->line_state = 0;

	// someone sets the dev to 0 if the close method has been called
	port->interrupt_in_urb->dev = port->serial->dev;


	/* force low_latency on so that our tty_push actually forces
	 * the data through, otherwise it is scheduled, and with high
	 * data rates (like with OHCI) data can get lost.
	 */
	port->tty->low_latency = 1;

	// without this, every push_tty_char is echoed :-(  
	port->tty->termios->c_lflag = 0;
	port->tty->termios->c_lflag &= ~(ISIG | ICANON | ECHO | IEXTEN | XCASE);
	port->tty->termios->c_iflag = IGNBRK | IGNPAR | IXOFF;
	port->tty->termios->c_oflag &= ~ONLCR; // do NOT translate CR to CR-NL (0x0A -> 0x0A 0x0D)
	
	// set up internal termios structure 
	priv->internal_termios.c_iflag = port->tty->termios->c_iflag;
	priv->internal_termios.c_oflag = port->tty->termios->c_oflag;
	priv->internal_termios.c_cflag = port->tty->termios->c_cflag;
	priv->internal_termios.c_lflag = port->tty->termios->c_lflag;

	for (i=0; i<NCCS; i++) {
		priv->internal_termios.c_cc[i] = port->tty->termios->c_cc[i];
	}
	
	// allocate memory for transfer buffer
	transfer_buffer = kzalloc(transfer_buffer_length, GFP_KERNEL);
	if (! transfer_buffer) {
		return -ENOMEM;
	}
	
	// allocate write_urb
	if (!port->write_urb) { 
		dbg("%s - port %d  Allocating port->write_urb", __FUNCTION__, port->number);
		port->write_urb = usb_alloc_urb(0, GFP_KERNEL);  
		if (!port->write_urb) {
			dbg("%s - port %d usb_alloc_urb failed", __FUNCTION__, port->number);
			kfree(transfer_buffer);
			return -ENOMEM;
		}
	}

	// allocate memory for write_urb transfer buffer
	port->write_urb->transfer_buffer = (unsigned char *) kmalloc(write_urb_transfer_buffer_length, GFP_KERNEL);
	if (! port->write_urb->transfer_buffer) {
		kfree(transfer_buffer);
		usb_free_urb(port->write_urb);
		port->write_urb = NULL;
		return -ENOMEM;
	} 

	// get hardware version
	result = usb_control_msg( port->serial->dev, 
				  usb_rcvctrlpipe(port->serial->dev, 0 ), 
				  SUSBCRequest_GetMisc,
				  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_IN,
				  SUSBCR_MSC_GetHWVersion,
				  0,
				  transfer_buffer,
				  transfer_buffer_length,
				  KOBIL_TIMEOUT
		);
	dbg("%s - port %d Send get_HW_version URB returns: %i", __FUNCTION__, port->number, result);
	dbg("Harware version: %i.%i.%i", transfer_buffer[0], transfer_buffer[1], transfer_buffer[2] );
	
	// get firmware version
	result = usb_control_msg( port->serial->dev, 
				  usb_rcvctrlpipe(port->serial->dev, 0 ), 
				  SUSBCRequest_GetMisc,
				  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_IN,
				  SUSBCR_MSC_GetFWVersion,
				  0,
				  transfer_buffer,
				  transfer_buffer_length,
				  KOBIL_TIMEOUT
		);
	dbg("%s - port %d Send get_FW_version URB returns: %i", __FUNCTION__, port->number, result);
	dbg("Firmware version: %i.%i.%i", transfer_buffer[0], transfer_buffer[1], transfer_buffer[2] );

	if (priv->device_type == KOBIL_ADAPTER_B_PRODUCT_ID || priv->device_type == KOBIL_ADAPTER_K_PRODUCT_ID) {
		// Setting Baudrate, Parity and Stopbits
		result = usb_control_msg( port->serial->dev, 
					  usb_rcvctrlpipe(port->serial->dev, 0 ), 
					  SUSBCRequest_SetBaudRateParityAndStopBits,
					  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_OUT,
					  SUSBCR_SBR_9600 | SUSBCR_SPASB_EvenParity | SUSBCR_SPASB_1StopBit,
					  0,
					  transfer_buffer,
					  0,
					  KOBIL_TIMEOUT
			);
		dbg("%s - port %d Send set_baudrate URB returns: %i", __FUNCTION__, port->number, result);
		
		// reset all queues
		result = usb_control_msg( port->serial->dev, 
					  usb_rcvctrlpipe(port->serial->dev, 0 ), 
					  SUSBCRequest_Misc,
					  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_OUT,
					  SUSBCR_MSC_ResetAllQueues,
					  0,
					  transfer_buffer,
					  0,
					  KOBIL_TIMEOUT
			);
		dbg("%s - port %d Send reset_all_queues URB returns: %i", __FUNCTION__, port->number, result);
	}
	if (priv->device_type == KOBIL_USBTWIN_PRODUCT_ID || priv->device_type == KOBIL_ADAPTER_B_PRODUCT_ID ||
	    priv->device_type == KOBIL_KAAN_SIM_PRODUCT_ID) {
		// start reading (Adapter B 'cause PNP string)
		result = usb_submit_urb( port->interrupt_in_urb, GFP_ATOMIC  ); 
		dbg("%s - port %d Send read URB returns: %i", __FUNCTION__, port->number, result);
	}

	kfree(transfer_buffer);
	return 0;
}


static void kobil_close (struct usb_serial_port *port, struct file *filp)
{
	dbg("%s - port %d", __FUNCTION__, port->number);

	if (port->write_urb) {
		usb_kill_urb(port->write_urb);
		usb_free_urb( port->write_urb );
		port->write_urb = NULL;
	}
	usb_kill_urb(port->interrupt_in_urb);
}


static void kobil_read_int_callback( struct urb *purb)
{
	int result;
	struct usb_serial_port *port = (struct usb_serial_port *) purb->context;
	struct tty_struct *tty;
	unsigned char *data = purb->transfer_buffer;
//	char *dbg_data;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (purb->status) {
		dbg("%s - port %d Read int status not zero: %d", __FUNCTION__, port->number, purb->status);
		return;
	}
	
	tty = port->tty; 
	if (purb->actual_length) {
		
		// BEGIN DEBUG
		/*
		  dbg_data = kzalloc((3 *  purb->actual_length + 10) * sizeof(char), GFP_KERNEL);
		  if (! dbg_data) {
		  return;
		  }
		  for (i = 0; i < purb->actual_length; i++) { 
		  sprintf(dbg_data +3*i, "%02X ", data[i]); 
		  }
		  dbg(" <-- %s", dbg_data );
		  kfree(dbg_data);
		*/
		// END DEBUG

		tty_buffer_request_room(tty, purb->actual_length);
		tty_insert_flip_string(tty, data, purb->actual_length);
		tty_flip_buffer_push(tty);
	}

	// someone sets the dev to 0 if the close method has been called
	port->interrupt_in_urb->dev = port->serial->dev;

	result = usb_submit_urb( port->interrupt_in_urb, GFP_ATOMIC ); 
	dbg("%s - port %d Send read URB returns: %i", __FUNCTION__, port->number, result);
}


static void kobil_write_callback( struct urb *purb )
{
}


static int kobil_write (struct usb_serial_port *port, 
			const unsigned char *buf, int count)
{
	int length = 0;
	int result = 0;
	int todo = 0;
	struct kobil_private * priv;

	if (count == 0) {
		dbg("%s - port %d write request of 0 bytes", __FUNCTION__, port->number);
		return 0;
	}

	priv = usb_get_serial_port_data(port);

	if (count > (KOBIL_BUF_LENGTH - priv->filled)) {
		dbg("%s - port %d Error: write request bigger than buffer size", __FUNCTION__, port->number);
		return -ENOMEM;
	}

	// Copy data to buffer
	memcpy (priv->buf + priv->filled, buf, count);

	usb_serial_debug_data(debug, &port->dev, __FUNCTION__, count, priv->buf + priv->filled);

	priv->filled = priv->filled + count;


	// only send complete block. TWIN, KAAN SIM and adapter K use the same protocol.
	if ( ((priv->device_type != KOBIL_ADAPTER_B_PRODUCT_ID) && (priv->filled > 2) && (priv->filled >= (priv->buf[1] + 3))) || 
	     ((priv->device_type == KOBIL_ADAPTER_B_PRODUCT_ID) && (priv->filled > 3) && (priv->filled >= (priv->buf[2] + 4))) ) {
		
		// stop reading (except TWIN and KAAN SIM)
		if ( (priv->device_type == KOBIL_ADAPTER_B_PRODUCT_ID) || (priv->device_type == KOBIL_ADAPTER_K_PRODUCT_ID) )
			usb_kill_urb(port->interrupt_in_urb);

		todo = priv->filled - priv->cur_pos;

		while(todo > 0) {
			// max 8 byte in one urb (endpoint size)
			length = (todo < 8) ? todo : 8;
			// copy data to transfer buffer
			memcpy(port->write_urb->transfer_buffer, priv->buf + priv->cur_pos, length );
			usb_fill_int_urb( port->write_urb,
					  port->serial->dev,
					  usb_sndintpipe(port->serial->dev, priv->write_int_endpoint_address),
					  port->write_urb->transfer_buffer,
					  length,
					  kobil_write_callback,
					  port,
					  8
				);

			priv->cur_pos = priv->cur_pos + length;
			result = usb_submit_urb( port->write_urb, GFP_NOIO );
			dbg("%s - port %d Send write URB returns: %i", __FUNCTION__, port->number, result);
			todo = priv->filled - priv->cur_pos;

			if (todo > 0) {
				msleep(24);
			}

		} // end while
		
		priv->filled = 0;
		priv->cur_pos = 0;

		// someone sets the dev to 0 if the close method has been called
		port->interrupt_in_urb->dev = port->serial->dev;
		
		// start reading (except TWIN and KAAN SIM)
		if ( (priv->device_type == KOBIL_ADAPTER_B_PRODUCT_ID) || (priv->device_type == KOBIL_ADAPTER_K_PRODUCT_ID) ) {
			// someone sets the dev to 0 if the close method has been called
			port->interrupt_in_urb->dev = port->serial->dev;
			
			result = usb_submit_urb( port->interrupt_in_urb, GFP_NOIO ); 
			dbg("%s - port %d Send read URB returns: %i", __FUNCTION__, port->number, result);
		}
	}
	return count;
}


static int kobil_write_room (struct usb_serial_port *port)
{
	//dbg("%s - port %d", __FUNCTION__, port->number);
	return 8;
}


static int kobil_tiocmget(struct usb_serial_port *port, struct file *file)
{
	struct kobil_private * priv;
	int result;
	unsigned char *transfer_buffer;
	int transfer_buffer_length = 8;

	priv = usb_get_serial_port_data(port);
	if ((priv->device_type == KOBIL_USBTWIN_PRODUCT_ID) || (priv->device_type == KOBIL_KAAN_SIM_PRODUCT_ID)) {
		// This device doesn't support ioctl calls
		return -EINVAL;
	}

	// allocate memory for transfer buffer
	transfer_buffer = kzalloc(transfer_buffer_length, GFP_KERNEL);
	if (!transfer_buffer) {
		return -ENOMEM;
	}

	result = usb_control_msg( port->serial->dev, 
				  usb_rcvctrlpipe(port->serial->dev, 0 ), 
				  SUSBCRequest_GetStatusLineState,
				  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_IN,
				  0,
				  0,
				  transfer_buffer,
				  transfer_buffer_length,
				  KOBIL_TIMEOUT);

	dbg("%s - port %d Send get_status_line_state URB returns: %i. Statusline: %02x", 
	    __FUNCTION__, port->number, result, transfer_buffer[0]);

	if ((transfer_buffer[0] & SUSBCR_GSL_DSR) != 0) {
		priv->line_state |= TIOCM_DSR;
	} else {
		priv->line_state &= ~TIOCM_DSR; 
	}

	kfree(transfer_buffer);
	return priv->line_state;
}

static int  kobil_tiocmset(struct usb_serial_port *port, struct file *file,
			   unsigned int set, unsigned int clear)
{
	struct kobil_private * priv;
	int result;
	int dtr = 0;
	int rts = 0;
	unsigned char *transfer_buffer;
	int transfer_buffer_length = 8;

	priv = usb_get_serial_port_data(port);
	if ((priv->device_type == KOBIL_USBTWIN_PRODUCT_ID) || (priv->device_type == KOBIL_KAAN_SIM_PRODUCT_ID)) {
		// This device doesn't support ioctl calls
		return -EINVAL;
	}

	// allocate memory for transfer buffer
	transfer_buffer = kzalloc(transfer_buffer_length, GFP_KERNEL);
	if (! transfer_buffer) {
		return -ENOMEM;
	}

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
			dbg("%s - port %d Setting DTR", __FUNCTION__, port->number);
		else
			dbg("%s - port %d Clearing DTR", __FUNCTION__, port->number);
		result = usb_control_msg( port->serial->dev, 
					  usb_rcvctrlpipe(port->serial->dev, 0 ), 
					  SUSBCRequest_SetStatusLinesOrQueues,
					  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_OUT,
					  ((dtr != 0) ? SUSBCR_SSL_SETDTR : SUSBCR_SSL_CLRDTR),
					  0,
					  transfer_buffer,
					  0,
					  KOBIL_TIMEOUT);
	} else {
		if (rts != 0)
			dbg("%s - port %d Setting RTS", __FUNCTION__, port->number);
		else
			dbg("%s - port %d Clearing RTS", __FUNCTION__, port->number);
		result = usb_control_msg( port->serial->dev, 
					  usb_rcvctrlpipe(port->serial->dev, 0 ), 
					  SUSBCRequest_SetStatusLinesOrQueues,
					  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_OUT,
					  ((rts != 0) ? SUSBCR_SSL_SETRTS : SUSBCR_SSL_CLRRTS),
					  0,
					  transfer_buffer,
					  0,
					  KOBIL_TIMEOUT);
	}
	dbg("%s - port %d Send set_status_line URB returns: %i", __FUNCTION__, port->number, result);
	kfree(transfer_buffer);
	return (result < 0) ? result : 0;
}


static int  kobil_ioctl(struct usb_serial_port *port, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct kobil_private * priv;
	int result;
	unsigned short urb_val = 0;
	unsigned char *transfer_buffer;
	int transfer_buffer_length = 8;
	char *settings;
	void __user *user_arg = (void __user *)arg;

	priv = usb_get_serial_port_data(port);
	if ((priv->device_type == KOBIL_USBTWIN_PRODUCT_ID) || (priv->device_type == KOBIL_KAAN_SIM_PRODUCT_ID)) {
		// This device doesn't support ioctl calls
		return 0;
	}

	switch (cmd) {
	case TCGETS:   // 0x5401
		if (!access_ok(VERIFY_WRITE, user_arg, sizeof(struct termios))) {
			dbg("%s - port %d Error in access_ok", __FUNCTION__, port->number);
			return -EFAULT;
		}
		if (kernel_termios_to_user_termios((struct termios __user *)arg,
						   &priv->internal_termios))
			return -EFAULT;
		return 0;

	case TCSETS:   // 0x5402
		if (!(port->tty->termios)) {
			dbg("%s - port %d Error: port->tty->termios is NULL", __FUNCTION__, port->number);
			return -ENOTTY;
		}
		if (!access_ok(VERIFY_READ, user_arg, sizeof(struct termios))) {
			dbg("%s - port %d Error in access_ok", __FUNCTION__, port->number);
			return -EFAULT;
		}
		if (user_termios_to_kernel_termios(&priv->internal_termios,
						   (struct termios __user *)arg))
			return -EFAULT;
		
		settings = kzalloc(50, GFP_KERNEL);
		if (! settings) {
			return -ENOBUFS;
		}

		switch (priv->internal_termios.c_cflag & CBAUD) {
		case B1200:
			urb_val = SUSBCR_SBR_1200;
			strcat(settings, "1200 ");
			break;
		case B9600:
		default:
			urb_val = SUSBCR_SBR_9600;
			strcat(settings, "9600 ");
			break;
		}

		urb_val |= (priv->internal_termios.c_cflag & CSTOPB) ? SUSBCR_SPASB_2StopBits : SUSBCR_SPASB_1StopBit;
		strcat(settings, (priv->internal_termios.c_cflag & CSTOPB) ? "2 StopBits " : "1 StopBit ");

		if (priv->internal_termios.c_cflag & PARENB) {
			if  (priv->internal_termios.c_cflag & PARODD) {
				urb_val |= SUSBCR_SPASB_OddParity;
				strcat(settings, "Odd Parity");
			} else {
				urb_val |= SUSBCR_SPASB_EvenParity;
				strcat(settings, "Even Parity");
			}
		} else {
			urb_val |= SUSBCR_SPASB_NoParity;
			strcat(settings, "No Parity");
		}
		dbg("%s - port %d setting port to: %s", __FUNCTION__, port->number, settings );

		result = usb_control_msg( port->serial->dev, 
					  usb_rcvctrlpipe(port->serial->dev, 0 ), 
					  SUSBCRequest_SetBaudRateParityAndStopBits,
					  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_OUT,
					  urb_val,
					  0,
					  settings,
					  0,
					  KOBIL_TIMEOUT
			);

		dbg("%s - port %d Send set_baudrate URB returns: %i", __FUNCTION__, port->number, result);
		kfree(settings);
		return 0;

	case TCFLSH:   // 0x540B
		transfer_buffer = (unsigned char *) kmalloc(transfer_buffer_length, GFP_KERNEL);
		if (! transfer_buffer) {
		 	return -ENOBUFS;
		}

		result = usb_control_msg( port->serial->dev, 
		 			  usb_rcvctrlpipe(port->serial->dev, 0 ), 
					  SUSBCRequest_Misc,
					  USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | USB_DIR_OUT,
					  SUSBCR_MSC_ResetAllQueues,
					  0,
					  NULL,//transfer_buffer,
					  0,
					  KOBIL_TIMEOUT
			);
		
		dbg("%s - port %d Send reset_all_queues (FLUSH) URB returns: %i", __FUNCTION__, port->number, result);

		kfree(transfer_buffer);
		return ((result < 0) ? -EFAULT : 0);

	}
	return -ENOIOCTLCMD;
}


static int __init kobil_init (void)
{
	int retval;
	retval = usb_serial_register(&kobil_device);
	if (retval)
		goto failed_usb_serial_register;
	retval = usb_register(&kobil_driver);
	if (retval) 
		goto failed_usb_register;

	info(DRIVER_VERSION " " DRIVER_AUTHOR);
	info(DRIVER_DESC);

	return 0;
failed_usb_register:
	usb_serial_deregister(&kobil_device);
failed_usb_serial_register:
	return retval;
}


static void __exit kobil_exit (void)
{
	usb_deregister (&kobil_driver);
	usb_serial_deregister (&kobil_device);
}

module_init(kobil_init);
module_exit(kobil_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE( "GPL" );

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
