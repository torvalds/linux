/*
  Keyspan USB to Serial Converter driver
 
  (C) Copyright (C) 2000-2001
      Hugh Blemings <hugh@blemings.org>
   
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  See http://blemings.org/hugh/keyspan.html for more information.
  
  Code in this driver inspired by and in a number of places taken
  from Brian Warner's original Keyspan-PDA driver.

  This driver has been put together with the support of Innosys, Inc.
  and Keyspan, Inc the manufacturers of the Keyspan USB-serial products.
  Thanks Guys :)
  
  Thanks to Paulus for miscellaneous tidy ups, some largish chunks
  of much nicer and/or completely new code and (perhaps most uniquely)
  having the patience to sit down and explain why and where he'd changed
  stuff.

  Tip 'o the hat to IBM (and previously Linuxcare :) for supporting 
  staff in their work on open source projects.
  
  See keyspan.c for update history.

*/

#ifndef __LINUX_USB_SERIAL_KEYSPAN_H
#define __LINUX_USB_SERIAL_KEYSPAN_H


/* Function prototypes for Keyspan serial converter */
static int  keyspan_open		(struct tty_struct *tty,
					 struct usb_serial_port *port);
static void keyspan_close		(struct usb_serial_port *port);
static void keyspan_dtr_rts		(struct usb_serial_port *port, int on);
static int  keyspan_startup		(struct usb_serial *serial);
static void keyspan_disconnect		(struct usb_serial *serial);
static void keyspan_release		(struct usb_serial *serial);
static int keyspan_port_probe(struct usb_serial_port *port);
static int keyspan_port_remove(struct usb_serial_port *port);
static int  keyspan_write_room		(struct tty_struct *tty);

static int  keyspan_write		(struct tty_struct *tty,
					 struct usb_serial_port *port,
					 const unsigned char *buf,
					 int count);

static void keyspan_send_setup		(struct usb_serial_port *port,
					 int reset_port);


static void keyspan_set_termios		(struct tty_struct *tty,
					 struct usb_serial_port *port,
					 struct ktermios *old);
static void keyspan_break_ctl		(struct tty_struct *tty,
					 int break_state);
static int  keyspan_tiocmget		(struct tty_struct *tty);
static int  keyspan_tiocmset		(struct tty_struct *tty,
					 unsigned int set,
					 unsigned int clear);
static int  keyspan_fake_startup	(struct usb_serial *serial);

static int  keyspan_usa19_calc_baud	(struct usb_serial_port *port,
					 u32 baud_rate, u32 baudclk,
					 u8 *rate_hi, u8 *rate_low,
					 u8 *prescaler, int portnum);

static int  keyspan_usa19w_calc_baud	(struct usb_serial_port *port,
					 u32 baud_rate, u32 baudclk,
					 u8 *rate_hi, u8 *rate_low,
					 u8 *prescaler, int portnum);

static int  keyspan_usa28_calc_baud	(struct usb_serial_port *port,
					 u32 baud_rate, u32 baudclk,
					 u8 *rate_hi, u8 *rate_low,
					 u8 *prescaler, int portnum);

static int  keyspan_usa19hs_calc_baud	(struct usb_serial_port *port,
					 u32 baud_rate, u32 baudclk,
					 u8 *rate_hi, u8 *rate_low,
					 u8 *prescaler, int portnum);

static int  keyspan_usa28_send_setup	(struct usb_serial *serial,
					 struct usb_serial_port *port,
					 int reset_port);
static int  keyspan_usa26_send_setup	(struct usb_serial *serial,
	       				 struct usb_serial_port *port,
					 int reset_port);
static int  keyspan_usa49_send_setup	(struct usb_serial *serial,
					 struct usb_serial_port *port,
					 int reset_port);

static int  keyspan_usa90_send_setup	(struct usb_serial *serial,
					 struct usb_serial_port *port,
					 int reset_port);

static int  keyspan_usa67_send_setup	(struct usb_serial *serial,
					 struct usb_serial_port *port,
					 int reset_port);

/* Values used for baud rate calculation - device specific */
#define	KEYSPAN_INVALID_BAUD_RATE		(-1)
#define	KEYSPAN_BAUD_RATE_OK			(0)
#define	KEYSPAN_USA18X_BAUDCLK			(12000000L)	/* a guess */
#define	KEYSPAN_USA19_BAUDCLK			(12000000L)
#define	KEYSPAN_USA19W_BAUDCLK			(24000000L)
#define	KEYSPAN_USA19HS_BAUDCLK			(14769231L)
#define	KEYSPAN_USA28_BAUDCLK			(1843200L)
#define	KEYSPAN_USA28X_BAUDCLK			(12000000L)
#define	KEYSPAN_USA49W_BAUDCLK			(48000000L)

/* Some constants used to characterise each device.  */
#define		KEYSPAN_MAX_NUM_PORTS		(4)
#define		KEYSPAN_MAX_FLIPS		(2)

/* Device info for the Keyspan serial converter, used
   by the overall usb-serial probe function */
#define KEYSPAN_VENDOR_ID			(0x06cd)

/* Product IDs for the products supported, pre-renumeration */
#define	keyspan_usa18x_pre_product_id		0x0105
#define	keyspan_usa19_pre_product_id		0x0103
#define	keyspan_usa19qi_pre_product_id		0x010b
#define	keyspan_mpr_pre_product_id		0x011b
#define	keyspan_usa19qw_pre_product_id		0x0118
#define	keyspan_usa19w_pre_product_id		0x0106
#define	keyspan_usa28_pre_product_id		0x0101
#define	keyspan_usa28x_pre_product_id		0x0102
#define	keyspan_usa28xa_pre_product_id		0x0114
#define	keyspan_usa28xb_pre_product_id		0x0113
#define	keyspan_usa49w_pre_product_id		0x0109
#define	keyspan_usa49wlc_pre_product_id		0x011a

/* Product IDs post-renumeration.  Note that the 28x and 28xb
   have the same id's post-renumeration but behave identically
   so it's not an issue. As such, the 28xb is not listed in any
   of the device tables. */
#define	keyspan_usa18x_product_id		0x0112
#define	keyspan_usa19_product_id		0x0107
#define	keyspan_usa19qi_product_id		0x010c
#define	keyspan_usa19hs_product_id		0x0121
#define	keyspan_mpr_product_id			0x011c
#define	keyspan_usa19qw_product_id		0x0119
#define	keyspan_usa19w_product_id		0x0108
#define	keyspan_usa28_product_id		0x010f
#define	keyspan_usa28x_product_id		0x0110
#define	keyspan_usa28xa_product_id		0x0115
#define	keyspan_usa28xb_product_id		0x0110
#define	keyspan_usa28xg_product_id		0x0135
#define	keyspan_usa49w_product_id		0x010a
#define	keyspan_usa49wlc_product_id		0x012a
#define	keyspan_usa49wg_product_id		0x0131

struct keyspan_device_details {
	/* product ID value */
	int	product_id;

	enum	{msg_usa26, msg_usa28, msg_usa49, msg_usa90, msg_usa67} msg_format;

		/* Number of physical ports */
	int	num_ports;

		/* 1 if endpoint flipping used on input, 0 if not */
	int	indat_endp_flip;

		/* 1 if endpoint flipping used on output, 0 if not */
	int 	outdat_endp_flip;

		/* Table mapping input data endpoint IDs to physical
		   port number and flip if used */
	int	indat_endpoints[KEYSPAN_MAX_NUM_PORTS];

		/* Same for output endpoints */
	int	outdat_endpoints[KEYSPAN_MAX_NUM_PORTS];

		/* Input acknowledge endpoints */
	int	inack_endpoints[KEYSPAN_MAX_NUM_PORTS];

		/* Output control endpoints */
	int	outcont_endpoints[KEYSPAN_MAX_NUM_PORTS];

		/* Endpoint used for input status */
	int	instat_endpoint;

		/* Endpoint used for input data 49WG only */
	int	indat_endpoint;

		/* Endpoint used for global control functions */
	int	glocont_endpoint;

	int	(*calculate_baud_rate) (struct usb_serial_port *port,
					u32 baud_rate, u32 baudclk,
					u8 *rate_hi, u8 *rate_low, u8 *prescaler, int portnum);
	u32	baudclk;
}; 

/* Now for each device type we setup the device detail
   structure with the appropriate information (provided
   in Keyspan's documentation) */

static const struct keyspan_device_details usa18x_device_details = {
	.product_id		= keyspan_usa18x_product_id,
	.msg_format		= msg_usa26,
	.num_ports		= 1,
	.indat_endp_flip	= 0,
	.outdat_endp_flip	= 1,
	.indat_endpoints	= {0x81},
	.outdat_endpoints	= {0x01},
	.inack_endpoints	= {0x85},
	.outcont_endpoints	= {0x05},
	.instat_endpoint	= 0x87,
	.indat_endpoint		= -1,
	.glocont_endpoint	= 0x07,
	.calculate_baud_rate	= keyspan_usa19w_calc_baud,
	.baudclk		= KEYSPAN_USA18X_BAUDCLK,
};

static const struct keyspan_device_details usa19_device_details = {
	.product_id		= keyspan_usa19_product_id,
	.msg_format		= msg_usa28,
	.num_ports		= 1,
	.indat_endp_flip	= 1,
	.outdat_endp_flip	= 1,
	.indat_endpoints	= {0x81},
	.outdat_endpoints	= {0x01},
	.inack_endpoints	= {0x83},
	.outcont_endpoints	= {0x03},
	.instat_endpoint	= 0x84,
	.indat_endpoint		= -1,
	.glocont_endpoint	= -1,
	.calculate_baud_rate	= keyspan_usa19_calc_baud,
	.baudclk		= KEYSPAN_USA19_BAUDCLK,
};

static const struct keyspan_device_details usa19qi_device_details = {
	.product_id		= keyspan_usa19qi_product_id,
	.msg_format		= msg_usa28,
	.num_ports		= 1,
	.indat_endp_flip	= 1,
	.outdat_endp_flip	= 1,
	.indat_endpoints	= {0x81},
	.outdat_endpoints	= {0x01},
	.inack_endpoints	= {0x83},
	.outcont_endpoints	= {0x03},
	.instat_endpoint	= 0x84,
	.indat_endpoint		= -1,
	.glocont_endpoint	= -1,
	.calculate_baud_rate	= keyspan_usa28_calc_baud,
	.baudclk		= KEYSPAN_USA19_BAUDCLK,
};

static const struct keyspan_device_details mpr_device_details = {
	.product_id		= keyspan_mpr_product_id,
	.msg_format		= msg_usa28,
	.num_ports		= 1,
	.indat_endp_flip	= 1,
	.outdat_endp_flip	= 1,
	.indat_endpoints	= {0x81},
	.outdat_endpoints	= {0x01},
	.inack_endpoints	= {0x83},
	.outcont_endpoints	= {0x03},
	.instat_endpoint	= 0x84,
	.indat_endpoint		= -1,
	.glocont_endpoint	= -1,
	.calculate_baud_rate	= keyspan_usa28_calc_baud,
	.baudclk		= KEYSPAN_USA19_BAUDCLK,
};

static const struct keyspan_device_details usa19qw_device_details = {
	.product_id		= keyspan_usa19qw_product_id,
	.msg_format		= msg_usa26,
	.num_ports		= 1,
	.indat_endp_flip	= 0,
	.outdat_endp_flip	= 1,
	.indat_endpoints	= {0x81},
	.outdat_endpoints	= {0x01},
	.inack_endpoints	= {0x85},
	.outcont_endpoints	= {0x05},
	.instat_endpoint	= 0x87,
	.indat_endpoint		= -1,
	.glocont_endpoint	= 0x07,
	.calculate_baud_rate	= keyspan_usa19w_calc_baud,
	.baudclk		= KEYSPAN_USA19W_BAUDCLK,
};

static const struct keyspan_device_details usa19w_device_details = {
	.product_id		= keyspan_usa19w_product_id,
	.msg_format		= msg_usa26,
	.num_ports		= 1,
	.indat_endp_flip	= 0,
	.outdat_endp_flip	= 1,
	.indat_endpoints	= {0x81},
	.outdat_endpoints	= {0x01},
	.inack_endpoints	= {0x85},
	.outcont_endpoints	= {0x05},
	.instat_endpoint	= 0x87,
	.indat_endpoint		= -1,
	.glocont_endpoint	= 0x07,
	.calculate_baud_rate	= keyspan_usa19w_calc_baud,
	.baudclk		= KEYSPAN_USA19W_BAUDCLK,
};

static const struct keyspan_device_details usa19hs_device_details = {
	.product_id		= keyspan_usa19hs_product_id,
	.msg_format		= msg_usa90,
	.num_ports		= 1,
	.indat_endp_flip	= 0,
	.outdat_endp_flip	= 0,
	.indat_endpoints	= {0x81},
	.outdat_endpoints	= {0x01},
	.inack_endpoints	= {-1},
	.outcont_endpoints	= {0x02},
	.instat_endpoint	= 0x82,
	.indat_endpoint		= -1,
	.glocont_endpoint	= -1,
	.calculate_baud_rate	= keyspan_usa19hs_calc_baud,
	.baudclk		= KEYSPAN_USA19HS_BAUDCLK,
};

static const struct keyspan_device_details usa28_device_details = {
	.product_id		= keyspan_usa28_product_id,
	.msg_format		= msg_usa28,
	.num_ports		= 2,
	.indat_endp_flip	= 1,
	.outdat_endp_flip	= 1,
	.indat_endpoints	= {0x81, 0x83},
	.outdat_endpoints	= {0x01, 0x03},
	.inack_endpoints	= {0x85, 0x86},
	.outcont_endpoints	= {0x05, 0x06},
	.instat_endpoint	= 0x87,
	.indat_endpoint		= -1,
	.glocont_endpoint	= 0x07,
	.calculate_baud_rate	= keyspan_usa28_calc_baud,
	.baudclk		= KEYSPAN_USA28_BAUDCLK,		
};

static const struct keyspan_device_details usa28x_device_details = {
	.product_id		= keyspan_usa28x_product_id,
	.msg_format		= msg_usa26,
	.num_ports		= 2,
	.indat_endp_flip	= 0,
	.outdat_endp_flip	= 1,
	.indat_endpoints	= {0x81, 0x83},
	.outdat_endpoints	= {0x01, 0x03},
	.inack_endpoints	= {0x85, 0x86},
	.outcont_endpoints	= {0x05, 0x06},
	.instat_endpoint	= 0x87,
	.indat_endpoint		= -1,
	.glocont_endpoint	= 0x07,
	.calculate_baud_rate	= keyspan_usa19w_calc_baud,
	.baudclk		= KEYSPAN_USA28X_BAUDCLK,
};

static const struct keyspan_device_details usa28xa_device_details = {
	.product_id		= keyspan_usa28xa_product_id,
	.msg_format		= msg_usa26,
	.num_ports		= 2,
	.indat_endp_flip	= 0,
	.outdat_endp_flip	= 1,
	.indat_endpoints	= {0x81, 0x83},
	.outdat_endpoints	= {0x01, 0x03},
	.inack_endpoints	= {0x85, 0x86},
	.outcont_endpoints	= {0x05, 0x06},
	.instat_endpoint	= 0x87,
	.indat_endpoint		= -1,
	.glocont_endpoint	= 0x07,
	.calculate_baud_rate	= keyspan_usa19w_calc_baud,
	.baudclk		= KEYSPAN_USA28X_BAUDCLK,
};

static const struct keyspan_device_details usa28xg_device_details = {
	.product_id		= keyspan_usa28xg_product_id,
	.msg_format		= msg_usa67,
	.num_ports		= 2,
	.indat_endp_flip	= 0,
	.outdat_endp_flip	= 0,
	.indat_endpoints	= {0x84, 0x88},
	.outdat_endpoints	= {0x02, 0x06},
	.inack_endpoints	= {-1, -1},
	.outcont_endpoints	= {-1, -1},
	.instat_endpoint	= 0x81,
	.indat_endpoint		= -1,
	.glocont_endpoint	= 0x01,
	.calculate_baud_rate	= keyspan_usa19w_calc_baud,
	.baudclk		= KEYSPAN_USA28X_BAUDCLK,
};
/* We don't need a separate entry for the usa28xb as it appears as a 28x anyway */

static const struct keyspan_device_details usa49w_device_details = {
	.product_id		= keyspan_usa49w_product_id,
	.msg_format		= msg_usa49,
	.num_ports		= 4,
	.indat_endp_flip	= 0,
	.outdat_endp_flip	= 0,
	.indat_endpoints	= {0x81, 0x82, 0x83, 0x84},
	.outdat_endpoints	= {0x01, 0x02, 0x03, 0x04},
	.inack_endpoints	= {-1, -1, -1, -1},
	.outcont_endpoints	= {-1, -1, -1, -1},
	.instat_endpoint	= 0x87,
	.indat_endpoint		= -1,
	.glocont_endpoint	= 0x07,
	.calculate_baud_rate	= keyspan_usa19w_calc_baud,
	.baudclk		= KEYSPAN_USA49W_BAUDCLK,
};

static const struct keyspan_device_details usa49wlc_device_details = {
	.product_id		= keyspan_usa49wlc_product_id,
	.msg_format		= msg_usa49,
	.num_ports		= 4,
	.indat_endp_flip	= 0,
	.outdat_endp_flip	= 0,
	.indat_endpoints	= {0x81, 0x82, 0x83, 0x84},
	.outdat_endpoints	= {0x01, 0x02, 0x03, 0x04},
	.inack_endpoints	= {-1, -1, -1, -1},
	.outcont_endpoints	= {-1, -1, -1, -1},
	.instat_endpoint	= 0x87,
	.indat_endpoint		= -1,
	.glocont_endpoint	= 0x07,
	.calculate_baud_rate	= keyspan_usa19w_calc_baud,
	.baudclk		= KEYSPAN_USA19W_BAUDCLK,
};

static const struct keyspan_device_details usa49wg_device_details = {
	.product_id		= keyspan_usa49wg_product_id,
	.msg_format		= msg_usa49,
	.num_ports		= 4,
	.indat_endp_flip	= 0,
	.outdat_endp_flip	= 0,
	.indat_endpoints	= {-1, -1, -1, -1},		/* single 'global' data in EP */
	.outdat_endpoints	= {0x01, 0x02, 0x04, 0x06},
	.inack_endpoints	= {-1, -1, -1, -1},
	.outcont_endpoints	= {-1, -1, -1, -1},
	.instat_endpoint	= 0x81,
	.indat_endpoint		= 0x88,
	.glocont_endpoint	= 0x00,				/* uses control EP */
	.calculate_baud_rate	= keyspan_usa19w_calc_baud,
	.baudclk		= KEYSPAN_USA19W_BAUDCLK,
};

static const struct keyspan_device_details *keyspan_devices[] = {
	&usa18x_device_details,
	&usa19_device_details,
	&usa19qi_device_details,
	&mpr_device_details,
	&usa19qw_device_details,
	&usa19w_device_details,
	&usa19hs_device_details,
	&usa28_device_details,
	&usa28x_device_details,
	&usa28xa_device_details,
	&usa28xg_device_details,
	/* 28xb not required as it renumerates as a 28x */
	&usa49w_device_details,
	&usa49wlc_device_details,
	&usa49wg_device_details,
	NULL,
};

static const struct usb_device_id keyspan_ids_combined[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa18x_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19w_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19qi_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19qw_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_mpr_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28x_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xa_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xb_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49w_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49wlc_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa18x_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19w_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19qi_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19qw_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19hs_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_mpr_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28x_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xa_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xg_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49w_product_id)},
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49wlc_product_id)},
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49wg_product_id)},
	{ } /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, keyspan_ids_combined);

/* usb_device_id table for the pre-firmware download keyspan devices */
static const struct usb_device_id keyspan_pre_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa18x_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19qi_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19qw_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19w_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_mpr_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28x_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xa_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xb_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49w_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49wlc_pre_product_id) },
	{ } /* Terminating entry */
};

static const struct usb_device_id keyspan_1port_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa18x_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19qi_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19qw_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19w_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19hs_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_mpr_product_id) },
	{ } /* Terminating entry */
};

static const struct usb_device_id keyspan_2port_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28x_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xa_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xg_product_id) },
	{ } /* Terminating entry */
};

static const struct usb_device_id keyspan_4port_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49w_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49wlc_product_id)},
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49wg_product_id)},
	{ } /* Terminating entry */
};

/* Structs for the devices, pre and post renumeration. */
static struct usb_serial_driver keyspan_pre_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "keyspan_no_firm",
	},
	.description		= "Keyspan - (without firmware)",
	.id_table		= keyspan_pre_ids,
	.num_ports		= 1,
	.attach			= keyspan_fake_startup,
};

static struct usb_serial_driver keyspan_1port_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "keyspan_1",
	},
	.description		= "Keyspan 1 port adapter",
	.id_table		= keyspan_1port_ids,
	.num_ports		= 1,
	.open			= keyspan_open,
	.close			= keyspan_close,
	.dtr_rts		= keyspan_dtr_rts,
	.write			= keyspan_write,
	.write_room		= keyspan_write_room,
	.set_termios		= keyspan_set_termios,
	.break_ctl		= keyspan_break_ctl,
	.tiocmget		= keyspan_tiocmget,
	.tiocmset		= keyspan_tiocmset,
	.attach			= keyspan_startup,
	.disconnect		= keyspan_disconnect,
	.release		= keyspan_release,
	.port_probe		= keyspan_port_probe,
	.port_remove		= keyspan_port_remove,
};

static struct usb_serial_driver keyspan_2port_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "keyspan_2",
	},
	.description		= "Keyspan 2 port adapter",
	.id_table		= keyspan_2port_ids,
	.num_ports		= 2,
	.open			= keyspan_open,
	.close			= keyspan_close,
	.dtr_rts		= keyspan_dtr_rts,
	.write			= keyspan_write,
	.write_room		= keyspan_write_room,
	.set_termios		= keyspan_set_termios,
	.break_ctl		= keyspan_break_ctl,
	.tiocmget		= keyspan_tiocmget,
	.tiocmset		= keyspan_tiocmset,
	.attach			= keyspan_startup,
	.disconnect		= keyspan_disconnect,
	.release		= keyspan_release,
	.port_probe		= keyspan_port_probe,
	.port_remove		= keyspan_port_remove,
};

static struct usb_serial_driver keyspan_4port_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "keyspan_4",
	},
	.description		= "Keyspan 4 port adapter",
	.id_table		= keyspan_4port_ids,
	.num_ports		= 4,
	.open			= keyspan_open,
	.close			= keyspan_close,
	.dtr_rts		= keyspan_dtr_rts,
	.write			= keyspan_write,
	.write_room		= keyspan_write_room,
	.set_termios		= keyspan_set_termios,
	.break_ctl		= keyspan_break_ctl,
	.tiocmget		= keyspan_tiocmget,
	.tiocmset		= keyspan_tiocmset,
	.attach			= keyspan_startup,
	.disconnect		= keyspan_disconnect,
	.release		= keyspan_release,
	.port_probe		= keyspan_port_probe,
	.port_remove		= keyspan_port_remove,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&keyspan_pre_device, &keyspan_1port_device,
	&keyspan_2port_device, &keyspan_4port_device, NULL
};

#endif
