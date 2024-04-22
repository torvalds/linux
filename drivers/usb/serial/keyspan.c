// SPDX-License-Identifier: GPL-2.0+
/*
  Keyspan USB to Serial Converter driver

  (C) Copyright (C) 2000-2001	Hugh Blemings <hugh@blemings.org>
  (C) Copyright (C) 2002	Greg Kroah-Hartman <greg@kroah.com>

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
*/


#include <linux/kernel.h>
#include <linux/jiffies.h>
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
#include <linux/usb/ezusb.h>

#define DRIVER_AUTHOR "Hugh Blemings <hugh@misc.nu"
#define DRIVER_DESC "Keyspan USB to Serial Converter Driver"

static void keyspan_send_setup(struct usb_serial_port *port, int reset_port);

static int keyspan_usa19_calc_baud(struct usb_serial_port *port,
				   u32 baud_rate, u32 baudclk,
				   u8 *rate_hi, u8 *rate_low,
				   u8 *prescaler, int portnum);
static int keyspan_usa19w_calc_baud(struct usb_serial_port *port,
				    u32 baud_rate, u32 baudclk,
				    u8 *rate_hi, u8 *rate_low,
				    u8 *prescaler, int portnum);
static int keyspan_usa28_calc_baud(struct usb_serial_port *port,
				   u32 baud_rate, u32 baudclk,
				   u8 *rate_hi, u8 *rate_low,
				   u8 *prescaler, int portnum);
static int keyspan_usa19hs_calc_baud(struct usb_serial_port *port,
				     u32 baud_rate, u32 baudclk,
				     u8 *rate_hi, u8 *rate_low,
				     u8 *prescaler, int portnum);

static int keyspan_usa28_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port);
static int keyspan_usa26_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port);
static int keyspan_usa49_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port);
static int keyspan_usa90_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port);
static int keyspan_usa67_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port);

/* Values used for baud rate calculation - device specific */
#define KEYSPAN_INVALID_BAUD_RATE		(-1)
#define KEYSPAN_BAUD_RATE_OK			(0)
#define KEYSPAN_USA18X_BAUDCLK			(12000000L)	/* a guess */
#define KEYSPAN_USA19_BAUDCLK			(12000000L)
#define KEYSPAN_USA19W_BAUDCLK			(24000000L)
#define KEYSPAN_USA19HS_BAUDCLK			(14769231L)
#define KEYSPAN_USA28_BAUDCLK			(1843200L)
#define KEYSPAN_USA28X_BAUDCLK			(12000000L)
#define KEYSPAN_USA49W_BAUDCLK			(48000000L)

/* Some constants used to characterise each device.  */
#define KEYSPAN_MAX_NUM_PORTS			(4)
#define KEYSPAN_MAX_FLIPS			(2)

/*
 * Device info for the Keyspan serial converter, used by the overall
 * usb-serial probe function.
 */
#define KEYSPAN_VENDOR_ID			(0x06cd)

/* Product IDs for the products supported, pre-renumeration */
#define keyspan_usa18x_pre_product_id		0x0105
#define keyspan_usa19_pre_product_id		0x0103
#define keyspan_usa19qi_pre_product_id		0x010b
#define keyspan_mpr_pre_product_id		0x011b
#define keyspan_usa19qw_pre_product_id		0x0118
#define keyspan_usa19w_pre_product_id		0x0106
#define keyspan_usa28_pre_product_id		0x0101
#define keyspan_usa28x_pre_product_id		0x0102
#define keyspan_usa28xa_pre_product_id		0x0114
#define keyspan_usa28xb_pre_product_id		0x0113
#define keyspan_usa49w_pre_product_id		0x0109
#define keyspan_usa49wlc_pre_product_id		0x011a

/*
 * Product IDs post-renumeration.  Note that the 28x and 28xb have the same
 * id's post-renumeration but behave identically so it's not an issue. As
 * such, the 28xb is not listed in any of the device tables.
 */
#define keyspan_usa18x_product_id		0x0112
#define keyspan_usa19_product_id		0x0107
#define keyspan_usa19qi_product_id		0x010c
#define keyspan_usa19hs_product_id		0x0121
#define keyspan_mpr_product_id			0x011c
#define keyspan_usa19qw_product_id		0x0119
#define keyspan_usa19w_product_id		0x0108
#define keyspan_usa28_product_id		0x010f
#define keyspan_usa28x_product_id		0x0110
#define keyspan_usa28xa_product_id		0x0115
#define keyspan_usa28xb_product_id		0x0110
#define keyspan_usa28xg_product_id		0x0135
#define keyspan_usa49w_product_id		0x010a
#define keyspan_usa49wlc_product_id		0x012a
#define keyspan_usa49wg_product_id		0x0131

struct keyspan_device_details {
	/* product ID value */
	int	product_id;

	enum	{msg_usa26, msg_usa28, msg_usa49, msg_usa90, msg_usa67} msg_format;

		/* Number of physical ports */
	int	num_ports;

		/* 1 if endpoint flipping used on input, 0 if not */
	int	indat_endp_flip;

		/* 1 if endpoint flipping used on output, 0 if not */
	int	outdat_endp_flip;

		/*
		 * Table mapping input data endpoint IDs to physical port
		 * number and flip if used
		 */
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

	int	(*calculate_baud_rate)(struct usb_serial_port *port,
				       u32 baud_rate, u32 baudclk,
				       u8 *rate_hi, u8 *rate_low, u8 *prescaler,
				       int portnum);
	u32	baudclk;
};

/*
 * Now for each device type we setup the device detail structure with the
 * appropriate information (provided in Keyspan's documentation)
 */

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
/*
 * We don't need a separate entry for the usa28xb as it appears as a 28x
 * anyway.
 */

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
	.indat_endpoints	= {-1, -1, -1, -1},	/* single 'global' data in EP */
	.outdat_endpoints	= {0x01, 0x02, 0x04, 0x06},
	.inack_endpoints	= {-1, -1, -1, -1},
	.outcont_endpoints	= {-1, -1, -1, -1},
	.instat_endpoint	= 0x81,
	.indat_endpoint		= 0x88,
	.glocont_endpoint	= 0x00,			/* uses control EP */
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

#define INSTAT_BUFLEN	32
#define GLOCONT_BUFLEN	64
#define INDAT49W_BUFLEN	512
#define IN_BUFLEN	64
#define OUT_BUFLEN	64
#define INACK_BUFLEN	1
#define OUTCONT_BUFLEN	64

	/* Per device and per port private data */
struct keyspan_serial_private {
	const struct keyspan_device_details	*device_details;

	struct urb	*instat_urb;
	char		*instat_buf;

	/* added to support 49wg, where data from all 4 ports comes in
	   on 1 EP and high-speed supported */
	struct urb	*indat_urb;
	char		*indat_buf;

	/* XXX this one probably will need a lock */
	struct urb	*glocont_urb;
	char		*glocont_buf;
	char		*ctrl_buf;	/* for EP0 control message */
};

struct keyspan_port_private {
	/* Keep track of which input & output endpoints to use */
	int		in_flip;
	int		out_flip;

	/* Keep duplicate of device details in each port
	   structure as well - simplifies some of the
	   callback functions etc. */
	const struct keyspan_device_details	*device_details;

	/* Input endpoints and buffer for this port */
	struct urb	*in_urbs[2];
	char		*in_buffer[2];
	/* Output endpoints and buffer for this port */
	struct urb	*out_urbs[2];
	char		*out_buffer[2];

	/* Input ack endpoint */
	struct urb	*inack_urb;
	char		*inack_buffer;

	/* Output control endpoint */
	struct urb	*outcont_urb;
	char		*outcont_buffer;

	/* Settings for the port */
	int		baud;
	int		old_baud;
	unsigned int	cflag;
	unsigned int	old_cflag;
	enum		{flow_none, flow_cts, flow_xon} flow_control;
	int		rts_state;	/* Handshaking pins (outputs) */
	int		dtr_state;
	int		cts_state;	/* Handshaking pins (inputs) */
	int		dsr_state;
	int		dcd_state;
	int		ri_state;
	int		break_on;

	unsigned long	tx_start_time[2];
	int		resend_cont;	/* need to resend control packet */
};

/* Include Keyspan message headers.  All current Keyspan Adapters
   make use of one of five message formats which are referred
   to as USA-26, USA-28, USA-49, USA-90, USA-67 by Keyspan and
   within this driver. */
#include "keyspan_usa26msg.h"
#include "keyspan_usa28msg.h"
#include "keyspan_usa49msg.h"
#include "keyspan_usa90msg.h"
#include "keyspan_usa67msg.h"


static int keyspan_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct keyspan_port_private 	*p_priv;

	p_priv = usb_get_serial_port_data(port);

	if (break_state == -1)
		p_priv->break_on = 1;
	else
		p_priv->break_on = 0;

	/* FIXME: return errors */
	keyspan_send_setup(port, 0);

	return 0;
}


static void keyspan_set_termios(struct tty_struct *tty,
				struct usb_serial_port *port,
				const struct ktermios *old_termios)
{
	int				baud_rate, device_port;
	struct keyspan_port_private 	*p_priv;
	const struct keyspan_device_details	*d_details;
	unsigned int 			cflag;

	p_priv = usb_get_serial_port_data(port);
	d_details = p_priv->device_details;
	cflag = tty->termios.c_cflag;
	device_port = port->port_number;

	/* Baud rate calculation takes baud rate as an integer
	   so other rates can be generated if desired. */
	baud_rate = tty_get_baud_rate(tty);
	/* If no match or invalid, don't change */
	if (d_details->calculate_baud_rate(port, baud_rate, d_details->baudclk,
				NULL, NULL, NULL, device_port) == KEYSPAN_BAUD_RATE_OK) {
		/* FIXME - more to do here to ensure rate changes cleanly */
		/* FIXME - calculate exact rate from divisor ? */
		p_priv->baud = baud_rate;
	} else
		baud_rate = tty_termios_baud_rate(old_termios);

	tty_encode_baud_rate(tty, baud_rate, baud_rate);
	/* set CTS/RTS handshake etc. */
	p_priv->cflag = cflag;
	p_priv->flow_control = (cflag & CRTSCTS) ? flow_cts : flow_none;

	/* Mark/Space not supported */
	tty->termios.c_cflag &= ~CMSPAR;

	keyspan_send_setup(port, 0);
}

static int keyspan_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct keyspan_port_private *p_priv = usb_get_serial_port_data(port);
	unsigned int			value;

	value = ((p_priv->rts_state) ? TIOCM_RTS : 0) |
		((p_priv->dtr_state) ? TIOCM_DTR : 0) |
		((p_priv->cts_state) ? TIOCM_CTS : 0) |
		((p_priv->dsr_state) ? TIOCM_DSR : 0) |
		((p_priv->dcd_state) ? TIOCM_CAR : 0) |
		((p_priv->ri_state) ? TIOCM_RNG : 0);

	return value;
}

static int keyspan_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct keyspan_port_private *p_priv = usb_get_serial_port_data(port);

	if (set & TIOCM_RTS)
		p_priv->rts_state = 1;
	if (set & TIOCM_DTR)
		p_priv->dtr_state = 1;
	if (clear & TIOCM_RTS)
		p_priv->rts_state = 0;
	if (clear & TIOCM_DTR)
		p_priv->dtr_state = 0;
	keyspan_send_setup(port, 0);
	return 0;
}

/* Write function is similar for the four protocols used
   with only a minor change for usa90 (usa19hs) required */
static int keyspan_write(struct tty_struct *tty,
	struct usb_serial_port *port, const unsigned char *buf, int count)
{
	struct keyspan_port_private 	*p_priv;
	const struct keyspan_device_details	*d_details;
	int				flip;
	int 				left, todo;
	struct urb			*this_urb;
	int 				err, maxDataLen, dataOffset;

	p_priv = usb_get_serial_port_data(port);
	d_details = p_priv->device_details;

	if (d_details->msg_format == msg_usa90) {
		maxDataLen = 64;
		dataOffset = 0;
	} else {
		maxDataLen = 63;
		dataOffset = 1;
	}

	dev_dbg(&port->dev, "%s - %d chars, flip=%d\n", __func__, count,
		p_priv->out_flip);

	for (left = count; left > 0; left -= todo) {
		todo = left;
		if (todo > maxDataLen)
			todo = maxDataLen;

		flip = p_priv->out_flip;

		/* Check we have a valid urb/endpoint before we use it... */
		this_urb = p_priv->out_urbs[flip];
		if (this_urb == NULL) {
			/* no bulk out, so return 0 bytes written */
			dev_dbg(&port->dev, "%s - no output urb :(\n", __func__);
			return count;
		}

		dev_dbg(&port->dev, "%s - endpoint %x flip %d\n",
			__func__, usb_pipeendpoint(this_urb->pipe), flip);

		if (this_urb->status == -EINPROGRESS) {
			if (time_before(jiffies,
					p_priv->tx_start_time[flip] + 10 * HZ))
				break;
			usb_unlink_urb(this_urb);
			break;
		}

		/* First byte in buffer is "last flag" (except for usa19hx)
		   - unused so for now so set to zero */
		((char *)this_urb->transfer_buffer)[0] = 0;

		memcpy(this_urb->transfer_buffer + dataOffset, buf, todo);
		buf += todo;

		/* send the data out the bulk port */
		this_urb->transfer_buffer_length = todo + dataOffset;

		err = usb_submit_urb(this_urb, GFP_ATOMIC);
		if (err != 0)
			dev_dbg(&port->dev, "usb_submit_urb(write bulk) failed (%d)\n", err);
		p_priv->tx_start_time[flip] = jiffies;

		/* Flip for next time if usa26 or usa28 interface
		   (not used on usa49) */
		p_priv->out_flip = (flip + 1) & d_details->outdat_endp_flip;
	}

	return count - left;
}

static void	usa26_indat_callback(struct urb *urb)
{
	int			i, err;
	int			endpoint;
	struct usb_serial_port	*port;
	unsigned char 		*data = urb->transfer_buffer;
	int status = urb->status;

	endpoint = usb_pipeendpoint(urb->pipe);

	if (status) {
		dev_dbg(&urb->dev->dev, "%s - nonzero status %d on endpoint %x\n",
			__func__, status, endpoint);
		return;
	}

	port =  urb->context;
	if (urb->actual_length) {
		/* 0x80 bit is error flag */
		if ((data[0] & 0x80) == 0) {
			/* no errors on individual bytes, only
			   possible overrun err */
			if (data[0] & RXERROR_OVERRUN) {
				tty_insert_flip_char(&port->port, 0,
								TTY_OVERRUN);
			}
			for (i = 1; i < urb->actual_length ; ++i)
				tty_insert_flip_char(&port->port, data[i],
								TTY_NORMAL);
		} else {
			/* some bytes had errors, every byte has status */
			dev_dbg(&port->dev, "%s - RX error!!!!\n", __func__);
			for (i = 0; i + 1 < urb->actual_length; i += 2) {
				int stat = data[i];
				int flag = TTY_NORMAL;

				if (stat & RXERROR_OVERRUN) {
					tty_insert_flip_char(&port->port, 0,
								TTY_OVERRUN);
				}
				/* XXX should handle break (0x10) */
				if (stat & RXERROR_PARITY)
					flag = TTY_PARITY;
				else if (stat & RXERROR_FRAMING)
					flag = TTY_FRAME;

				tty_insert_flip_char(&port->port, data[i+1],
						flag);
			}
		}
		tty_flip_buffer_push(&port->port);
	}

	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - resubmit read urb failed. (%d)\n", __func__, err);
}

/* Outdat handling is common for all devices */
static void	usa2x_outdat_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;

	port =  urb->context;
	p_priv = usb_get_serial_port_data(port);
	dev_dbg(&port->dev, "%s - urb %d\n", __func__, urb == p_priv->out_urbs[1]);

	usb_serial_port_softint(port);
}

static void	usa26_inack_callback(struct urb *urb)
{
}

static void	usa26_outcont_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;

	port =  urb->context;
	p_priv = usb_get_serial_port_data(port);

	if (p_priv->resend_cont) {
		dev_dbg(&port->dev, "%s - sending setup\n", __func__);
		keyspan_usa26_send_setup(port->serial, port,
						p_priv->resend_cont - 1);
	}
}

static void	usa26_instat_callback(struct urb *urb)
{
	unsigned char 				*data = urb->transfer_buffer;
	struct keyspan_usa26_portStatusMessage	*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	int old_dcd_state, err;
	int status = urb->status;

	serial =  urb->context;

	if (status) {
		dev_dbg(&urb->dev->dev, "%s - nonzero status: %d\n",
				__func__, status);
		return;
	}
	if (urb->actual_length != 9) {
		dev_dbg(&urb->dev->dev, "%s - %d byte report??\n", __func__, urb->actual_length);
		goto exit;
	}

	msg = (struct keyspan_usa26_portStatusMessage *)data;

	/* Check port number from message and retrieve private data */
	if (msg->port >= serial->num_ports) {
		dev_dbg(&urb->dev->dev, "%s - Unexpected port number %d\n", __func__, msg->port);
		goto exit;
	}
	port = serial->port[msg->port];
	p_priv = usb_get_serial_port_data(port);
	if (!p_priv)
		goto resubmit;

	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->hskia_cts) ? 1 : 0);
	p_priv->dsr_state = ((msg->dsr) ? 1 : 0);
	p_priv->dcd_state = ((msg->gpia_dcd) ? 1 : 0);
	p_priv->ri_state = ((msg->ri) ? 1 : 0);

	if (old_dcd_state != p_priv->dcd_state)
		tty_port_tty_hangup(&port->port, true);
resubmit:
	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - resubmit read urb failed. (%d)\n", __func__, err);
exit: ;
}

static void	usa26_glocont_callback(struct urb *urb)
{
}


static void usa28_indat_callback(struct urb *urb)
{
	int                     err;
	struct usb_serial_port  *port;
	unsigned char           *data;
	struct keyspan_port_private             *p_priv;
	int status = urb->status;

	port =  urb->context;
	p_priv = usb_get_serial_port_data(port);

	if (urb != p_priv->in_urbs[p_priv->in_flip])
		return;

	do {
		if (status) {
			dev_dbg(&urb->dev->dev, "%s - nonzero status %d on endpoint %x\n",
				__func__, status, usb_pipeendpoint(urb->pipe));
			return;
		}

		port =  urb->context;
		p_priv = usb_get_serial_port_data(port);
		data = urb->transfer_buffer;

		if (urb->actual_length) {
			tty_insert_flip_string(&port->port, data,
					urb->actual_length);
			tty_flip_buffer_push(&port->port);
		}

		/* Resubmit urb so we continue receiving */
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err != 0)
			dev_dbg(&port->dev, "%s - resubmit read urb failed. (%d)\n",
							__func__, err);
		p_priv->in_flip ^= 1;

		urb = p_priv->in_urbs[p_priv->in_flip];
	} while (urb->status != -EINPROGRESS);
}

static void	usa28_inack_callback(struct urb *urb)
{
}

static void	usa28_outcont_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;

	port =  urb->context;
	p_priv = usb_get_serial_port_data(port);

	if (p_priv->resend_cont) {
		dev_dbg(&port->dev, "%s - sending setup\n", __func__);
		keyspan_usa28_send_setup(port->serial, port,
						p_priv->resend_cont - 1);
	}
}

static void	usa28_instat_callback(struct urb *urb)
{
	int					err;
	unsigned char 				*data = urb->transfer_buffer;
	struct keyspan_usa28_portStatusMessage	*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	int old_dcd_state;
	int status = urb->status;

	serial =  urb->context;

	if (status) {
		dev_dbg(&urb->dev->dev, "%s - nonzero status: %d\n",
				__func__, status);
		return;
	}

	if (urb->actual_length != sizeof(struct keyspan_usa28_portStatusMessage)) {
		dev_dbg(&urb->dev->dev, "%s - bad length %d\n", __func__, urb->actual_length);
		goto exit;
	}

	msg = (struct keyspan_usa28_portStatusMessage *)data;

	/* Check port number from message and retrieve private data */
	if (msg->port >= serial->num_ports) {
		dev_dbg(&urb->dev->dev, "%s - Unexpected port number %d\n", __func__, msg->port);
		goto exit;
	}
	port = serial->port[msg->port];
	p_priv = usb_get_serial_port_data(port);
	if (!p_priv)
		goto resubmit;

	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->cts) ? 1 : 0);
	p_priv->dsr_state = ((msg->dsr) ? 1 : 0);
	p_priv->dcd_state = ((msg->dcd) ? 1 : 0);
	p_priv->ri_state = ((msg->ri) ? 1 : 0);

	if (old_dcd_state != p_priv->dcd_state && old_dcd_state)
		tty_port_tty_hangup(&port->port, true);
resubmit:
		/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - resubmit read urb failed. (%d)\n", __func__, err);
exit: ;
}

static void	usa28_glocont_callback(struct urb *urb)
{
}


static void	usa49_glocont_callback(struct urb *urb)
{
	struct usb_serial *serial;
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;
	int i;

	serial =  urb->context;
	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		p_priv = usb_get_serial_port_data(port);
		if (!p_priv)
			continue;

		if (p_priv->resend_cont) {
			dev_dbg(&port->dev, "%s - sending setup\n", __func__);
			keyspan_usa49_send_setup(serial, port,
						p_priv->resend_cont - 1);
			break;
		}
	}
}

	/* This is actually called glostat in the Keyspan
	   doco */
static void	usa49_instat_callback(struct urb *urb)
{
	int					err;
	unsigned char 				*data = urb->transfer_buffer;
	struct keyspan_usa49_portStatusMessage	*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	int old_dcd_state;
	int status = urb->status;

	serial =  urb->context;

	if (status) {
		dev_dbg(&urb->dev->dev, "%s - nonzero status: %d\n",
				__func__, status);
		return;
	}

	if (urb->actual_length !=
			sizeof(struct keyspan_usa49_portStatusMessage)) {
		dev_dbg(&urb->dev->dev, "%s - bad length %d\n", __func__, urb->actual_length);
		goto exit;
	}

	msg = (struct keyspan_usa49_portStatusMessage *)data;

	/* Check port number from message and retrieve private data */
	if (msg->portNumber >= serial->num_ports) {
		dev_dbg(&urb->dev->dev, "%s - Unexpected port number %d\n",
			__func__, msg->portNumber);
		goto exit;
	}
	port = serial->port[msg->portNumber];
	p_priv = usb_get_serial_port_data(port);
	if (!p_priv)
		goto resubmit;

	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->cts) ? 1 : 0);
	p_priv->dsr_state = ((msg->dsr) ? 1 : 0);
	p_priv->dcd_state = ((msg->dcd) ? 1 : 0);
	p_priv->ri_state = ((msg->ri) ? 1 : 0);

	if (old_dcd_state != p_priv->dcd_state && old_dcd_state)
		tty_port_tty_hangup(&port->port, true);
resubmit:
	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - resubmit read urb failed. (%d)\n", __func__, err);
exit:	;
}

static void	usa49_inack_callback(struct urb *urb)
{
}

static void	usa49_indat_callback(struct urb *urb)
{
	int			i, err;
	int			endpoint;
	struct usb_serial_port	*port;
	unsigned char 		*data = urb->transfer_buffer;
	int status = urb->status;

	endpoint = usb_pipeendpoint(urb->pipe);

	if (status) {
		dev_dbg(&urb->dev->dev, "%s - nonzero status %d on endpoint %x\n",
			__func__, status, endpoint);
		return;
	}

	port =  urb->context;
	if (urb->actual_length) {
		/* 0x80 bit is error flag */
		if ((data[0] & 0x80) == 0) {
			/* no error on any byte */
			tty_insert_flip_string(&port->port, data + 1,
						urb->actual_length - 1);
		} else {
			/* some bytes had errors, every byte has status */
			for (i = 0; i + 1 < urb->actual_length; i += 2) {
				int stat = data[i];
				int flag = TTY_NORMAL;

				if (stat & RXERROR_OVERRUN) {
					tty_insert_flip_char(&port->port, 0,
								TTY_OVERRUN);
				}
				/* XXX should handle break (0x10) */
				if (stat & RXERROR_PARITY)
					flag = TTY_PARITY;
				else if (stat & RXERROR_FRAMING)
					flag = TTY_FRAME;

				tty_insert_flip_char(&port->port, data[i+1],
						flag);
			}
		}
		tty_flip_buffer_push(&port->port);
	}

	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - resubmit read urb failed. (%d)\n", __func__, err);
}

static void usa49wg_indat_callback(struct urb *urb)
{
	int			i, len, x, err;
	struct usb_serial	*serial;
	struct usb_serial_port	*port;
	unsigned char 		*data = urb->transfer_buffer;
	int status = urb->status;

	serial = urb->context;

	if (status) {
		dev_dbg(&urb->dev->dev, "%s - nonzero status: %d\n",
				__func__, status);
		return;
	}

	/* inbound data is in the form P#, len, status, data */
	i = 0;
	len = 0;

	while (i < urb->actual_length) {

		/* Check port number from message */
		if (data[i] >= serial->num_ports) {
			dev_dbg(&urb->dev->dev, "%s - Unexpected port number %d\n",
				__func__, data[i]);
			return;
		}
		port = serial->port[data[i++]];
		len = data[i++];

		/* 0x80 bit is error flag */
		if ((data[i] & 0x80) == 0) {
			/* no error on any byte */
			i++;
			for (x = 1; x < len && i < urb->actual_length; ++x)
				tty_insert_flip_char(&port->port,
						data[i++], 0);
		} else {
			/*
			 * some bytes had errors, every byte has status
			 */
			for (x = 0; x + 1 < len &&
				    i + 1 < urb->actual_length; x += 2) {
				int stat = data[i];
				int flag = TTY_NORMAL;

				if (stat & RXERROR_OVERRUN) {
					tty_insert_flip_char(&port->port, 0,
								TTY_OVERRUN);
				}
				/* XXX should handle break (0x10) */
				if (stat & RXERROR_PARITY)
					flag = TTY_PARITY;
				else if (stat & RXERROR_FRAMING)
					flag = TTY_FRAME;

				tty_insert_flip_char(&port->port, data[i+1],
						     flag);
				i += 2;
			}
		}
		tty_flip_buffer_push(&port->port);
	}

	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&urb->dev->dev, "%s - resubmit read urb failed. (%d)\n", __func__, err);
}

/* not used, usa-49 doesn't have per-port control endpoints */
static void usa49_outcont_callback(struct urb *urb)
{
}

static void usa90_indat_callback(struct urb *urb)
{
	int			i, err;
	int			endpoint;
	struct usb_serial_port	*port;
	struct keyspan_port_private	 	*p_priv;
	unsigned char 		*data = urb->transfer_buffer;
	int status = urb->status;

	endpoint = usb_pipeendpoint(urb->pipe);

	if (status) {
		dev_dbg(&urb->dev->dev, "%s - nonzero status %d on endpoint %x\n",
			__func__, status, endpoint);
		return;
	}

	port =  urb->context;
	p_priv = usb_get_serial_port_data(port);

	if (urb->actual_length) {
		/* if current mode is DMA, looks like usa28 format
		   otherwise looks like usa26 data format */

		if (p_priv->baud > 57600)
			tty_insert_flip_string(&port->port, data,
					urb->actual_length);
		else {
			/* 0x80 bit is error flag */
			if ((data[0] & 0x80) == 0) {
				/* no errors on individual bytes, only
				   possible overrun err*/
				if (data[0] & RXERROR_OVERRUN) {
					tty_insert_flip_char(&port->port, 0,
								TTY_OVERRUN);
				}
				for (i = 1; i < urb->actual_length ; ++i)
					tty_insert_flip_char(&port->port,
							data[i], TTY_NORMAL);
			}  else {
			/* some bytes had errors, every byte has status */
				dev_dbg(&port->dev, "%s - RX error!!!!\n", __func__);
				for (i = 0; i + 1 < urb->actual_length; i += 2) {
					int stat = data[i];
					int flag = TTY_NORMAL;

					if (stat & RXERROR_OVERRUN) {
						tty_insert_flip_char(
								&port->port, 0,
								TTY_OVERRUN);
					}
					/* XXX should handle break (0x10) */
					if (stat & RXERROR_PARITY)
						flag = TTY_PARITY;
					else if (stat & RXERROR_FRAMING)
						flag = TTY_FRAME;

					tty_insert_flip_char(&port->port,
							data[i+1], flag);
				}
			}
		}
		tty_flip_buffer_push(&port->port);
	}

	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - resubmit read urb failed. (%d)\n", __func__, err);
}


static void	usa90_instat_callback(struct urb *urb)
{
	unsigned char 				*data = urb->transfer_buffer;
	struct keyspan_usa90_portStatusMessage	*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	int old_dcd_state, err;
	int status = urb->status;

	serial =  urb->context;

	if (status) {
		dev_dbg(&urb->dev->dev, "%s - nonzero status: %d\n",
				__func__, status);
		return;
	}
	if (urb->actual_length < 14) {
		dev_dbg(&urb->dev->dev, "%s - %d byte report??\n", __func__, urb->actual_length);
		goto exit;
	}

	msg = (struct keyspan_usa90_portStatusMessage *)data;

	/* Now do something useful with the data */

	port = serial->port[0];
	p_priv = usb_get_serial_port_data(port);
	if (!p_priv)
		goto resubmit;

	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->cts) ? 1 : 0);
	p_priv->dsr_state = ((msg->dsr) ? 1 : 0);
	p_priv->dcd_state = ((msg->dcd) ? 1 : 0);
	p_priv->ri_state = ((msg->ri) ? 1 : 0);

	if (old_dcd_state != p_priv->dcd_state && old_dcd_state)
		tty_port_tty_hangup(&port->port, true);
resubmit:
	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - resubmit read urb failed. (%d)\n", __func__, err);
exit:
	;
}

static void	usa90_outcont_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;

	port =  urb->context;
	p_priv = usb_get_serial_port_data(port);

	if (p_priv->resend_cont) {
		dev_dbg(&urb->dev->dev, "%s - sending setup\n", __func__);
		keyspan_usa90_send_setup(port->serial, port,
						p_priv->resend_cont - 1);
	}
}

/* Status messages from the 28xg */
static void	usa67_instat_callback(struct urb *urb)
{
	int					err;
	unsigned char 				*data = urb->transfer_buffer;
	struct keyspan_usa67_portStatusMessage	*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	int old_dcd_state;
	int status = urb->status;

	serial = urb->context;

	if (status) {
		dev_dbg(&urb->dev->dev, "%s - nonzero status: %d\n",
				__func__, status);
		return;
	}

	if (urb->actual_length !=
			sizeof(struct keyspan_usa67_portStatusMessage)) {
		dev_dbg(&urb->dev->dev, "%s - bad length %d\n", __func__, urb->actual_length);
		return;
	}


	/* Now do something useful with the data */
	msg = (struct keyspan_usa67_portStatusMessage *)data;

	/* Check port number from message and retrieve private data */
	if (msg->port >= serial->num_ports) {
		dev_dbg(&urb->dev->dev, "%s - Unexpected port number %d\n", __func__, msg->port);
		return;
	}

	port = serial->port[msg->port];
	p_priv = usb_get_serial_port_data(port);
	if (!p_priv)
		goto resubmit;

	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->hskia_cts) ? 1 : 0);
	p_priv->dcd_state = ((msg->gpia_dcd) ? 1 : 0);

	if (old_dcd_state != p_priv->dcd_state && old_dcd_state)
		tty_port_tty_hangup(&port->port, true);
resubmit:
	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - resubmit read urb failed. (%d)\n", __func__, err);
}

static void usa67_glocont_callback(struct urb *urb)
{
	struct usb_serial *serial;
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;
	int i;

	serial = urb->context;
	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		p_priv = usb_get_serial_port_data(port);
		if (!p_priv)
			continue;

		if (p_priv->resend_cont) {
			dev_dbg(&port->dev, "%s - sending setup\n", __func__);
			keyspan_usa67_send_setup(serial, port,
						p_priv->resend_cont - 1);
			break;
		}
	}
}

static unsigned int keyspan_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct keyspan_port_private	*p_priv;
	const struct keyspan_device_details	*d_details;
	int				flip;
	unsigned int			data_len;
	struct urb			*this_urb;

	p_priv = usb_get_serial_port_data(port);
	d_details = p_priv->device_details;

	/* FIXME: locking */
	if (d_details->msg_format == msg_usa90)
		data_len = 64;
	else
		data_len = 63;

	flip = p_priv->out_flip;

	/* Check both endpoints to see if any are available. */
	this_urb = p_priv->out_urbs[flip];
	if (this_urb != NULL) {
		if (this_urb->status != -EINPROGRESS)
			return data_len;
		flip = (flip + 1) & d_details->outdat_endp_flip;
		this_urb = p_priv->out_urbs[flip];
		if (this_urb != NULL) {
			if (this_urb->status != -EINPROGRESS)
				return data_len;
		}
	}
	return 0;
}


static int keyspan_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct keyspan_port_private 	*p_priv;
	const struct keyspan_device_details	*d_details;
	int				i, err;
	int				baud_rate, device_port;
	struct urb			*urb;
	unsigned int			cflag = 0;

	p_priv = usb_get_serial_port_data(port);
	d_details = p_priv->device_details;

	/* Set some sane defaults */
	p_priv->rts_state = 1;
	p_priv->dtr_state = 1;
	p_priv->baud = 9600;

	/* force baud and lcr to be set on open */
	p_priv->old_baud = 0;
	p_priv->old_cflag = 0;

	p_priv->out_flip = 0;
	p_priv->in_flip = 0;

	/* Reset low level data toggle and start reading from endpoints */
	for (i = 0; i < 2; i++) {
		urb = p_priv->in_urbs[i];
		if (urb == NULL)
			continue;

		/* make sure endpoint data toggle is synchronized
		   with the device */
		usb_clear_halt(urb->dev, urb->pipe);
		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err != 0)
			dev_dbg(&port->dev, "%s - submit urb %d failed (%d)\n", __func__, i, err);
	}

	/* Reset low level data toggle on out endpoints */
	for (i = 0; i < 2; i++) {
		urb = p_priv->out_urbs[i];
		if (urb == NULL)
			continue;
		/* usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe),
						usb_pipeout(urb->pipe), 0); */
	}

	/* get the terminal config for the setup message now so we don't
	 * need to send 2 of them */

	device_port = port->port_number;
	if (tty) {
		cflag = tty->termios.c_cflag;
		/* Baud rate calculation takes baud rate as an integer
		   so other rates can be generated if desired. */
		baud_rate = tty_get_baud_rate(tty);
		/* If no match or invalid, leave as default */
		if (baud_rate >= 0
		    && d_details->calculate_baud_rate(port, baud_rate, d_details->baudclk,
					NULL, NULL, NULL, device_port) == KEYSPAN_BAUD_RATE_OK) {
			p_priv->baud = baud_rate;
		}
	}
	/* set CTS/RTS handshake etc. */
	p_priv->cflag = cflag;
	p_priv->flow_control = (cflag & CRTSCTS) ? flow_cts : flow_none;

	keyspan_send_setup(port, 1);
	/* mdelay(100); */
	/* keyspan_set_termios(port, NULL); */

	return 0;
}

static void keyspan_dtr_rts(struct usb_serial_port *port, int on)
{
	struct keyspan_port_private *p_priv = usb_get_serial_port_data(port);

	p_priv->rts_state = on;
	p_priv->dtr_state = on;
	keyspan_send_setup(port, 0);
}

static void keyspan_close(struct usb_serial_port *port)
{
	int			i;
	struct keyspan_port_private 	*p_priv;

	p_priv = usb_get_serial_port_data(port);

	p_priv->rts_state = 0;
	p_priv->dtr_state = 0;

	keyspan_send_setup(port, 2);
	/* pilot-xfer seems to work best with this delay */
	mdelay(100);

	p_priv->out_flip = 0;
	p_priv->in_flip = 0;

	usb_kill_urb(p_priv->inack_urb);
	for (i = 0; i < 2; i++) {
		usb_kill_urb(p_priv->in_urbs[i]);
		usb_kill_urb(p_priv->out_urbs[i]);
	}
}

/* download the firmware to a pre-renumeration device */
static int keyspan_fake_startup(struct usb_serial *serial)
{
	char	*fw_name;

	dev_dbg(&serial->dev->dev, "Keyspan startup version %04x product %04x\n",
		le16_to_cpu(serial->dev->descriptor.bcdDevice),
		le16_to_cpu(serial->dev->descriptor.idProduct));

	if ((le16_to_cpu(serial->dev->descriptor.bcdDevice) & 0x8000)
								!= 0x8000) {
		dev_dbg(&serial->dev->dev, "Firmware already loaded.  Quitting.\n");
		return 1;
	}

		/* Select firmware image on the basis of idProduct */
	switch (le16_to_cpu(serial->dev->descriptor.idProduct)) {
	case keyspan_usa28_pre_product_id:
		fw_name = "keyspan/usa28.fw";
		break;

	case keyspan_usa28x_pre_product_id:
		fw_name = "keyspan/usa28x.fw";
		break;

	case keyspan_usa28xa_pre_product_id:
		fw_name = "keyspan/usa28xa.fw";
		break;

	case keyspan_usa28xb_pre_product_id:
		fw_name = "keyspan/usa28xb.fw";
		break;

	case keyspan_usa19_pre_product_id:
		fw_name = "keyspan/usa19.fw";
		break;

	case keyspan_usa19qi_pre_product_id:
		fw_name = "keyspan/usa19qi.fw";
		break;

	case keyspan_mpr_pre_product_id:
		fw_name = "keyspan/mpr.fw";
		break;

	case keyspan_usa19qw_pre_product_id:
		fw_name = "keyspan/usa19qw.fw";
		break;

	case keyspan_usa18x_pre_product_id:
		fw_name = "keyspan/usa18x.fw";
		break;

	case keyspan_usa19w_pre_product_id:
		fw_name = "keyspan/usa19w.fw";
		break;

	case keyspan_usa49w_pre_product_id:
		fw_name = "keyspan/usa49w.fw";
		break;

	case keyspan_usa49wlc_pre_product_id:
		fw_name = "keyspan/usa49wlc.fw";
		break;

	default:
		dev_err(&serial->dev->dev, "Unknown product ID (%04x)\n",
			le16_to_cpu(serial->dev->descriptor.idProduct));
		return 1;
	}

	dev_dbg(&serial->dev->dev, "Uploading Keyspan %s firmware.\n", fw_name);

	if (ezusb_fx1_ihex_firmware_download(serial->dev, fw_name) < 0) {
		dev_err(&serial->dev->dev, "failed to load firmware \"%s\"\n",
			fw_name);
		return -ENOENT;
	}

	/* after downloading firmware Renumeration will occur in a
	  moment and the new device will bind to the real driver */

	/* we don't want this device to have a driver assigned to it. */
	return 1;
}

/* Helper functions used by keyspan_setup_urbs */
static struct usb_endpoint_descriptor const *find_ep(struct usb_serial const *serial,
						     int endpoint)
{
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *ep;
	int i;

	iface_desc = serial->interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		ep = &iface_desc->endpoint[i].desc;
		if (ep->bEndpointAddress == endpoint)
			return ep;
	}
	dev_warn(&serial->interface->dev, "found no endpoint descriptor for endpoint %x\n",
			endpoint);
	return NULL;
}

static struct urb *keyspan_setup_urb(struct usb_serial *serial, int endpoint,
				      int dir, void *ctx, char *buf, int len,
				      void (*callback)(struct urb *))
{
	struct urb *urb;
	struct usb_endpoint_descriptor const *ep_desc;
	char const *ep_type_name;

	if (endpoint == -1)
		return NULL;		/* endpoint not needed */

	dev_dbg(&serial->interface->dev, "%s - alloc for endpoint %x\n",
			__func__, endpoint);
	urb = usb_alloc_urb(0, GFP_KERNEL);		/* No ISO */
	if (!urb)
		return NULL;

	if (endpoint == 0) {
		/* control EP filled in when used */
		return urb;
	}

	ep_desc = find_ep(serial, endpoint);
	if (!ep_desc) {
		usb_free_urb(urb);
		return NULL;
	}
	if (usb_endpoint_xfer_int(ep_desc)) {
		ep_type_name = "INT";
		usb_fill_int_urb(urb, serial->dev,
				 usb_sndintpipe(serial->dev, endpoint) | dir,
				 buf, len, callback, ctx,
				 ep_desc->bInterval);
	} else if (usb_endpoint_xfer_bulk(ep_desc)) {
		ep_type_name = "BULK";
		usb_fill_bulk_urb(urb, serial->dev,
				  usb_sndbulkpipe(serial->dev, endpoint) | dir,
				  buf, len, callback, ctx);
	} else {
		dev_warn(&serial->interface->dev,
			 "unsupported endpoint type %x\n",
			 usb_endpoint_type(ep_desc));
		usb_free_urb(urb);
		return NULL;
	}

	dev_dbg(&serial->interface->dev, "%s - using urb %p for %s endpoint %x\n",
	    __func__, urb, ep_type_name, endpoint);
	return urb;
}

static struct callbacks {
	void	(*instat_callback)(struct urb *);
	void	(*glocont_callback)(struct urb *);
	void	(*indat_callback)(struct urb *);
	void	(*outdat_callback)(struct urb *);
	void	(*inack_callback)(struct urb *);
	void	(*outcont_callback)(struct urb *);
} keyspan_callbacks[] = {
	{
		/* msg_usa26 callbacks */
		.instat_callback =	usa26_instat_callback,
		.glocont_callback =	usa26_glocont_callback,
		.indat_callback =	usa26_indat_callback,
		.outdat_callback =	usa2x_outdat_callback,
		.inack_callback =	usa26_inack_callback,
		.outcont_callback =	usa26_outcont_callback,
	}, {
		/* msg_usa28 callbacks */
		.instat_callback =	usa28_instat_callback,
		.glocont_callback =	usa28_glocont_callback,
		.indat_callback =	usa28_indat_callback,
		.outdat_callback =	usa2x_outdat_callback,
		.inack_callback =	usa28_inack_callback,
		.outcont_callback =	usa28_outcont_callback,
	}, {
		/* msg_usa49 callbacks */
		.instat_callback =	usa49_instat_callback,
		.glocont_callback =	usa49_glocont_callback,
		.indat_callback =	usa49_indat_callback,
		.outdat_callback =	usa2x_outdat_callback,
		.inack_callback =	usa49_inack_callback,
		.outcont_callback =	usa49_outcont_callback,
	}, {
		/* msg_usa90 callbacks */
		.instat_callback =	usa90_instat_callback,
		.glocont_callback =	usa28_glocont_callback,
		.indat_callback =	usa90_indat_callback,
		.outdat_callback =	usa2x_outdat_callback,
		.inack_callback =	usa28_inack_callback,
		.outcont_callback =	usa90_outcont_callback,
	}, {
		/* msg_usa67 callbacks */
		.instat_callback =	usa67_instat_callback,
		.glocont_callback =	usa67_glocont_callback,
		.indat_callback =	usa26_indat_callback,
		.outdat_callback =	usa2x_outdat_callback,
		.inack_callback =	usa26_inack_callback,
		.outcont_callback =	usa26_outcont_callback,
	}
};

	/* Generic setup urbs function that uses
	   data in device_details */
static void keyspan_setup_urbs(struct usb_serial *serial)
{
	struct keyspan_serial_private 	*s_priv;
	const struct keyspan_device_details	*d_details;
	struct callbacks		*cback;

	s_priv = usb_get_serial_data(serial);
	d_details = s_priv->device_details;

	/* Setup values for the various callback routines */
	cback = &keyspan_callbacks[d_details->msg_format];

	/* Allocate and set up urbs for each one that is in use,
	   starting with instat endpoints */
	s_priv->instat_urb = keyspan_setup_urb
		(serial, d_details->instat_endpoint, USB_DIR_IN,
		 serial, s_priv->instat_buf, INSTAT_BUFLEN,
		 cback->instat_callback);

	s_priv->indat_urb = keyspan_setup_urb
		(serial, d_details->indat_endpoint, USB_DIR_IN,
		 serial, s_priv->indat_buf, INDAT49W_BUFLEN,
		 usa49wg_indat_callback);

	s_priv->glocont_urb = keyspan_setup_urb
		(serial, d_details->glocont_endpoint, USB_DIR_OUT,
		 serial, s_priv->glocont_buf, GLOCONT_BUFLEN,
		 cback->glocont_callback);
}

/* usa19 function doesn't require prescaler */
static int keyspan_usa19_calc_baud(struct usb_serial_port *port,
				   u32 baud_rate, u32 baudclk, u8 *rate_hi,
				   u8 *rate_low, u8 *prescaler, int portnum)
{
	u32 	b16,	/* baud rate times 16 (actual rate used internally) */
		div,	/* divisor */
		cnt;	/* inverse of divisor (programmed into 8051) */

	dev_dbg(&port->dev, "%s - %d.\n", __func__, baud_rate);

	/* prevent divide by zero...  */
	b16 = baud_rate * 16L;
	if (b16 == 0)
		return KEYSPAN_INVALID_BAUD_RATE;
	/* Any "standard" rate over 57k6 is marginal on the USA-19
	   as we run out of divisor resolution. */
	if (baud_rate > 57600)
		return KEYSPAN_INVALID_BAUD_RATE;

	/* calculate the divisor and the counter (its inverse) */
	div = baudclk / b16;
	if (div == 0)
		return KEYSPAN_INVALID_BAUD_RATE;
	else
		cnt = 0 - div;

	if (div > 0xffff)
		return KEYSPAN_INVALID_BAUD_RATE;

	/* return the counter values if non-null */
	if (rate_low)
		*rate_low = (u8) (cnt & 0xff);
	if (rate_hi)
		*rate_hi = (u8) ((cnt >> 8) & 0xff);
	if (rate_low && rate_hi)
		dev_dbg(&port->dev, "%s - %d %02x %02x.\n",
				__func__, baud_rate, *rate_hi, *rate_low);
	return KEYSPAN_BAUD_RATE_OK;
}

/* usa19hs function doesn't require prescaler */
static int keyspan_usa19hs_calc_baud(struct usb_serial_port *port,
				     u32 baud_rate, u32 baudclk, u8 *rate_hi,
				     u8 *rate_low, u8 *prescaler, int portnum)
{
	u32 	b16,	/* baud rate times 16 (actual rate used internally) */
			div;	/* divisor */

	dev_dbg(&port->dev, "%s - %d.\n", __func__, baud_rate);

	/* prevent divide by zero...  */
	b16 = baud_rate * 16L;
	if (b16 == 0)
		return KEYSPAN_INVALID_BAUD_RATE;

	/* calculate the divisor */
	div = baudclk / b16;
	if (div == 0)
		return KEYSPAN_INVALID_BAUD_RATE;

	if (div > 0xffff)
		return KEYSPAN_INVALID_BAUD_RATE;

	/* return the counter values if non-null */
	if (rate_low)
		*rate_low = (u8) (div & 0xff);

	if (rate_hi)
		*rate_hi = (u8) ((div >> 8) & 0xff);

	if (rate_low && rate_hi)
		dev_dbg(&port->dev, "%s - %d %02x %02x.\n",
			__func__, baud_rate, *rate_hi, *rate_low);

	return KEYSPAN_BAUD_RATE_OK;
}

static int keyspan_usa19w_calc_baud(struct usb_serial_port *port,
				    u32 baud_rate, u32 baudclk, u8 *rate_hi,
				    u8 *rate_low, u8 *prescaler, int portnum)
{
	u32 	b16,	/* baud rate times 16 (actual rate used internally) */
		clk,	/* clock with 13/8 prescaler */
		div,	/* divisor using 13/8 prescaler */
		res,	/* resulting baud rate using 13/8 prescaler */
		diff,	/* error using 13/8 prescaler */
		smallest_diff;
	u8	best_prescaler;
	int	i;

	dev_dbg(&port->dev, "%s - %d.\n", __func__, baud_rate);

	/* prevent divide by zero */
	b16 = baud_rate * 16L;
	if (b16 == 0)
		return KEYSPAN_INVALID_BAUD_RATE;

	/* Calculate prescaler by trying them all and looking
	   for best fit */

	/* start with largest possible difference */
	smallest_diff = 0xffffffff;

		/* 0 is an invalid prescaler, used as a flag */
	best_prescaler = 0;

	for (i = 8; i <= 0xff; ++i) {
		clk = (baudclk * 8) / (u32) i;

		div = clk / b16;
		if (div == 0)
			continue;

		res = clk / div;
		diff = (res > b16) ? (res-b16) : (b16-res);

		if (diff < smallest_diff) {
			best_prescaler = i;
			smallest_diff = diff;
		}
	}

	if (best_prescaler == 0)
		return KEYSPAN_INVALID_BAUD_RATE;

	clk = (baudclk * 8) / (u32) best_prescaler;
	div = clk / b16;

	/* return the divisor and prescaler if non-null */
	if (rate_low)
		*rate_low = (u8) (div & 0xff);
	if (rate_hi)
		*rate_hi = (u8) ((div >> 8) & 0xff);
	if (prescaler) {
		*prescaler = best_prescaler;
		/*  dev_dbg(&port->dev, "%s - %d %d\n", __func__, *prescaler, div); */
	}
	return KEYSPAN_BAUD_RATE_OK;
}

	/* USA-28 supports different maximum baud rates on each port */
static int keyspan_usa28_calc_baud(struct usb_serial_port *port,
				   u32 baud_rate, u32 baudclk, u8 *rate_hi,
				   u8 *rate_low, u8 *prescaler, int portnum)
{
	u32 	b16,	/* baud rate times 16 (actual rate used internally) */
		div,	/* divisor */
		cnt;	/* inverse of divisor (programmed into 8051) */

	dev_dbg(&port->dev, "%s - %d.\n", __func__, baud_rate);

		/* prevent divide by zero */
	b16 = baud_rate * 16L;
	if (b16 == 0)
		return KEYSPAN_INVALID_BAUD_RATE;

	/* calculate the divisor and the counter (its inverse) */
	div = KEYSPAN_USA28_BAUDCLK / b16;
	if (div == 0)
		return KEYSPAN_INVALID_BAUD_RATE;
	else
		cnt = 0 - div;

	/* check for out of range, based on portnum,
	   and return result */
	if (portnum == 0) {
		if (div > 0xffff)
			return KEYSPAN_INVALID_BAUD_RATE;
	} else {
		if (portnum == 1) {
			if (div > 0xff)
				return KEYSPAN_INVALID_BAUD_RATE;
		} else
			return KEYSPAN_INVALID_BAUD_RATE;
	}

		/* return the counter values if not NULL
		   (port 1 will ignore retHi) */
	if (rate_low)
		*rate_low = (u8) (cnt & 0xff);
	if (rate_hi)
		*rate_hi = (u8) ((cnt >> 8) & 0xff);
	dev_dbg(&port->dev, "%s - %d OK.\n", __func__, baud_rate);
	return KEYSPAN_BAUD_RATE_OK;
}

static int keyspan_usa26_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port)
{
	struct keyspan_usa26_portControlMessage	msg;
	struct keyspan_serial_private 		*s_priv;
	struct keyspan_port_private 		*p_priv;
	const struct keyspan_device_details	*d_details;
	struct urb				*this_urb;
	int 					device_port, err;

	dev_dbg(&port->dev, "%s reset=%d\n", __func__, reset_port);

	s_priv = usb_get_serial_data(serial);
	p_priv = usb_get_serial_port_data(port);
	d_details = s_priv->device_details;
	device_port = port->port_number;

	this_urb = p_priv->outcont_urb;

		/* Make sure we have an urb then send the message */
	if (this_urb == NULL) {
		dev_dbg(&port->dev, "%s - oops no urb.\n", __func__);
		return -1;
	}

	dev_dbg(&port->dev, "%s - endpoint %x\n",
			__func__, usb_pipeendpoint(this_urb->pipe));

	/* Save reset port val for resend.
	   Don't overwrite resend for open/close condition. */
	if ((reset_port + 1) > p_priv->resend_cont)
		p_priv->resend_cont = reset_port + 1;
	if (this_urb->status == -EINPROGRESS) {
		/*  dev_dbg(&port->dev, "%s - already writing\n", __func__); */
		mdelay(5);
		return -1;
	}

	memset(&msg, 0, sizeof(struct keyspan_usa26_portControlMessage));

	/* Only set baud rate if it's changed */
	if (p_priv->old_baud != p_priv->baud) {
		p_priv->old_baud = p_priv->baud;
		msg.setClocking = 0xff;
		if (d_details->calculate_baud_rate(port, p_priv->baud, d_details->baudclk,
						   &msg.baudHi, &msg.baudLo, &msg.prescaler,
						   device_port) == KEYSPAN_INVALID_BAUD_RATE) {
			dev_dbg(&port->dev, "%s - Invalid baud rate %d requested, using 9600.\n",
				__func__, p_priv->baud);
			msg.baudLo = 0;
			msg.baudHi = 125;	/* Values for 9600 baud */
			msg.prescaler = 10;
		}
		msg.setPrescaler = 0xff;
	}

	msg.lcr = (p_priv->cflag & CSTOPB) ? STOPBITS_678_2 : STOPBITS_5678_1;
	switch (p_priv->cflag & CSIZE) {
	case CS5:
		msg.lcr |= USA_DATABITS_5;
		break;
	case CS6:
		msg.lcr |= USA_DATABITS_6;
		break;
	case CS7:
		msg.lcr |= USA_DATABITS_7;
		break;
	case CS8:
		msg.lcr |= USA_DATABITS_8;
		break;
	}
	if (p_priv->cflag & PARENB) {
		/* note USA_PARITY_NONE == 0 */
		msg.lcr |= (p_priv->cflag & PARODD) ?
			USA_PARITY_ODD : USA_PARITY_EVEN;
	}
	msg.setLcr = 0xff;

	msg.ctsFlowControl = (p_priv->flow_control == flow_cts);
	msg.xonFlowControl = 0;
	msg.setFlowControl = 0xff;
	msg.forwardingLength = 16;
	msg.xonChar = 17;
	msg.xoffChar = 19;

	/* Opening port */
	if (reset_port == 1) {
		msg._txOn = 1;
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txBreak = 0;
		msg.rxOn = 1;
		msg.rxOff = 0;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0xff;
	}

	/* Closing port */
	else if (reset_port == 2) {
		msg._txOn = 0;
		msg._txOff = 1;
		msg.txFlush = 0;
		msg.txBreak = 0;
		msg.rxOn = 0;
		msg.rxOff = 1;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0;
	}

	/* Sending intermediate configs */
	else {
		msg._txOn = (!p_priv->break_on);
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txBreak = (p_priv->break_on);
		msg.rxOn = 0;
		msg.rxOff = 0;
		msg.rxFlush = 0;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0x0;
	}

	/* Do handshaking outputs */
	msg.setTxTriState_setRts = 0xff;
	msg.txTriState_rts = p_priv->rts_state;

	msg.setHskoa_setDtr = 0xff;
	msg.hskoa_dtr = p_priv->dtr_state;

	p_priv->resend_cont = 0;
	memcpy(this_urb->transfer_buffer, &msg, sizeof(msg));

	/* send the data out the device on control endpoint */
	this_urb->transfer_buffer_length = sizeof(msg);

	err = usb_submit_urb(this_urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - usb_submit_urb(setup) failed (%d)\n", __func__, err);
	return 0;
}

static int keyspan_usa28_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port)
{
	struct keyspan_usa28_portControlMessage	msg;
	struct keyspan_serial_private	 	*s_priv;
	struct keyspan_port_private 		*p_priv;
	const struct keyspan_device_details	*d_details;
	struct urb				*this_urb;
	int 					device_port, err;

	s_priv = usb_get_serial_data(serial);
	p_priv = usb_get_serial_port_data(port);
	d_details = s_priv->device_details;
	device_port = port->port_number;

	/* only do something if we have a bulk out endpoint */
	this_urb = p_priv->outcont_urb;
	if (this_urb == NULL) {
		dev_dbg(&port->dev, "%s - oops no urb.\n", __func__);
		return -1;
	}

	/* Save reset port val for resend.
	   Don't overwrite resend for open/close condition. */
	if ((reset_port + 1) > p_priv->resend_cont)
		p_priv->resend_cont = reset_port + 1;
	if (this_urb->status == -EINPROGRESS) {
		dev_dbg(&port->dev, "%s already writing\n", __func__);
		mdelay(5);
		return -1;
	}

	memset(&msg, 0, sizeof(struct keyspan_usa28_portControlMessage));

	msg.setBaudRate = 1;
	if (d_details->calculate_baud_rate(port, p_priv->baud, d_details->baudclk,
					   &msg.baudHi, &msg.baudLo, NULL,
					   device_port) == KEYSPAN_INVALID_BAUD_RATE) {
		dev_dbg(&port->dev, "%s - Invalid baud rate requested %d.\n",
						__func__, p_priv->baud);
		msg.baudLo = 0xff;
		msg.baudHi = 0xb2;	/* Values for 9600 baud */
	}

	/* If parity is enabled, we must calculate it ourselves. */
	msg.parity = 0;		/* XXX for now */

	msg.ctsFlowControl = (p_priv->flow_control == flow_cts);
	msg.xonFlowControl = 0;

	/* Do handshaking outputs, DTR is inverted relative to RTS */
	msg.rts = p_priv->rts_state;
	msg.dtr = p_priv->dtr_state;

	msg.forwardingLength = 16;
	msg.forwardMs = 10;
	msg.breakThreshold = 45;
	msg.xonChar = 17;
	msg.xoffChar = 19;

	/*msg.returnStatus = 1;
	msg.resetDataToggle = 0xff;*/
	/* Opening port */
	if (reset_port == 1) {
		msg._txOn = 1;
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txForceXoff = 0;
		msg.txBreak = 0;
		msg.rxOn = 1;
		msg.rxOff = 0;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0xff;
	}
	/* Closing port */
	else if (reset_port == 2) {
		msg._txOn = 0;
		msg._txOff = 1;
		msg.txFlush = 0;
		msg.txForceXoff = 0;
		msg.txBreak = 0;
		msg.rxOn = 0;
		msg.rxOff = 1;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0;
	}
	/* Sending intermediate configs */
	else {
		msg._txOn = (!p_priv->break_on);
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txForceXoff = 0;
		msg.txBreak = (p_priv->break_on);
		msg.rxOn = 0;
		msg.rxOff = 0;
		msg.rxFlush = 0;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0x0;
	}

	p_priv->resend_cont = 0;
	memcpy(this_urb->transfer_buffer, &msg, sizeof(msg));

	/* send the data out the device on control endpoint */
	this_urb->transfer_buffer_length = sizeof(msg);

	err = usb_submit_urb(this_urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - usb_submit_urb(setup) failed\n", __func__);

	return 0;
}

static int keyspan_usa49_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port)
{
	struct keyspan_usa49_portControlMessage	msg;
	struct usb_ctrlrequest 			*dr = NULL;
	struct keyspan_serial_private 		*s_priv;
	struct keyspan_port_private 		*p_priv;
	const struct keyspan_device_details	*d_details;
	struct urb				*this_urb;
	int 					err, device_port;

	s_priv = usb_get_serial_data(serial);
	p_priv = usb_get_serial_port_data(port);
	d_details = s_priv->device_details;

	this_urb = s_priv->glocont_urb;

	/* Work out which port within the device is being setup */
	device_port = port->port_number;

	/* Make sure we have an urb then send the message */
	if (this_urb == NULL) {
		dev_dbg(&port->dev, "%s - oops no urb for port.\n", __func__);
		return -1;
	}

	dev_dbg(&port->dev, "%s - endpoint %x (%d)\n",
		__func__, usb_pipeendpoint(this_urb->pipe), device_port);

	/* Save reset port val for resend.
	   Don't overwrite resend for open/close condition. */
	if ((reset_port + 1) > p_priv->resend_cont)
		p_priv->resend_cont = reset_port + 1;

	if (this_urb->status == -EINPROGRESS) {
		/*  dev_dbg(&port->dev, "%s - already writing\n", __func__); */
		mdelay(5);
		return -1;
	}

	memset(&msg, 0, sizeof(struct keyspan_usa49_portControlMessage));

	msg.portNumber = device_port;

	/* Only set baud rate if it's changed */
	if (p_priv->old_baud != p_priv->baud) {
		p_priv->old_baud = p_priv->baud;
		msg.setClocking = 0xff;
		if (d_details->calculate_baud_rate(port, p_priv->baud, d_details->baudclk,
						   &msg.baudHi, &msg.baudLo, &msg.prescaler,
						   device_port) == KEYSPAN_INVALID_BAUD_RATE) {
			dev_dbg(&port->dev, "%s - Invalid baud rate %d requested, using 9600.\n",
				__func__, p_priv->baud);
			msg.baudLo = 0;
			msg.baudHi = 125;	/* Values for 9600 baud */
			msg.prescaler = 10;
		}
		/* msg.setPrescaler = 0xff; */
	}

	msg.lcr = (p_priv->cflag & CSTOPB) ? STOPBITS_678_2 : STOPBITS_5678_1;
	switch (p_priv->cflag & CSIZE) {
	case CS5:
		msg.lcr |= USA_DATABITS_5;
		break;
	case CS6:
		msg.lcr |= USA_DATABITS_6;
		break;
	case CS7:
		msg.lcr |= USA_DATABITS_7;
		break;
	case CS8:
		msg.lcr |= USA_DATABITS_8;
		break;
	}
	if (p_priv->cflag & PARENB) {
		/* note USA_PARITY_NONE == 0 */
		msg.lcr |= (p_priv->cflag & PARODD) ?
			USA_PARITY_ODD : USA_PARITY_EVEN;
	}
	msg.setLcr = 0xff;

	msg.ctsFlowControl = (p_priv->flow_control == flow_cts);
	msg.xonFlowControl = 0;
	msg.setFlowControl = 0xff;

	msg.forwardingLength = 16;
	msg.xonChar = 17;
	msg.xoffChar = 19;

	/* Opening port */
	if (reset_port == 1) {
		msg._txOn = 1;
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txBreak = 0;
		msg.rxOn = 1;
		msg.rxOff = 0;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0xff;
		msg.enablePort = 1;
		msg.disablePort = 0;
	}
	/* Closing port */
	else if (reset_port == 2) {
		msg._txOn = 0;
		msg._txOff = 1;
		msg.txFlush = 0;
		msg.txBreak = 0;
		msg.rxOn = 0;
		msg.rxOff = 1;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0;
		msg.enablePort = 0;
		msg.disablePort = 1;
	}
	/* Sending intermediate configs */
	else {
		msg._txOn = (!p_priv->break_on);
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txBreak = (p_priv->break_on);
		msg.rxOn = 0;
		msg.rxOff = 0;
		msg.rxFlush = 0;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0x0;
		msg.enablePort = 0;
		msg.disablePort = 0;
	}

	/* Do handshaking outputs */
	msg.setRts = 0xff;
	msg.rts = p_priv->rts_state;

	msg.setDtr = 0xff;
	msg.dtr = p_priv->dtr_state;

	p_priv->resend_cont = 0;

	/* if the device is a 49wg, we send control message on usb
	   control EP 0 */

	if (d_details->product_id == keyspan_usa49wg_product_id) {
		dr = (void *)(s_priv->ctrl_buf);
		dr->bRequestType = USB_TYPE_VENDOR | USB_DIR_OUT;
		dr->bRequest = 0xB0;	/* 49wg control message */
		dr->wValue = 0;
		dr->wIndex = 0;
		dr->wLength = cpu_to_le16(sizeof(msg));

		memcpy(s_priv->glocont_buf, &msg, sizeof(msg));

		usb_fill_control_urb(this_urb, serial->dev,
				usb_sndctrlpipe(serial->dev, 0),
				(unsigned char *)dr, s_priv->glocont_buf,
				sizeof(msg), usa49_glocont_callback, serial);

	} else {
		memcpy(this_urb->transfer_buffer, &msg, sizeof(msg));

		/* send the data out the device on control endpoint */
		this_urb->transfer_buffer_length = sizeof(msg);
	}
	err = usb_submit_urb(this_urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - usb_submit_urb(setup) failed (%d)\n", __func__, err);

	return 0;
}

static int keyspan_usa90_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port)
{
	struct keyspan_usa90_portControlMessage	msg;
	struct keyspan_serial_private 		*s_priv;
	struct keyspan_port_private 		*p_priv;
	const struct keyspan_device_details	*d_details;
	struct urb				*this_urb;
	int 					err;
	u8						prescaler;

	s_priv = usb_get_serial_data(serial);
	p_priv = usb_get_serial_port_data(port);
	d_details = s_priv->device_details;

	/* only do something if we have a bulk out endpoint */
	this_urb = p_priv->outcont_urb;
	if (this_urb == NULL) {
		dev_dbg(&port->dev, "%s - oops no urb.\n", __func__);
		return -1;
	}

	/* Save reset port val for resend.
	   Don't overwrite resend for open/close condition. */
	if ((reset_port + 1) > p_priv->resend_cont)
		p_priv->resend_cont = reset_port + 1;
	if (this_urb->status == -EINPROGRESS) {
		dev_dbg(&port->dev, "%s already writing\n", __func__);
		mdelay(5);
		return -1;
	}

	memset(&msg, 0, sizeof(struct keyspan_usa90_portControlMessage));

	/* Only set baud rate if it's changed */
	if (p_priv->old_baud != p_priv->baud) {
		p_priv->old_baud = p_priv->baud;
		msg.setClocking = 0x01;
		if (d_details->calculate_baud_rate(port, p_priv->baud, d_details->baudclk,
						   &msg.baudHi, &msg.baudLo, &prescaler, 0) == KEYSPAN_INVALID_BAUD_RATE) {
			dev_dbg(&port->dev, "%s - Invalid baud rate %d requested, using 9600.\n",
				__func__, p_priv->baud);
			p_priv->baud = 9600;
			d_details->calculate_baud_rate(port, p_priv->baud, d_details->baudclk,
				&msg.baudHi, &msg.baudLo, &prescaler, 0);
		}
		msg.setRxMode = 1;
		msg.setTxMode = 1;
	}

	/* modes must always be correctly specified */
	if (p_priv->baud > 57600) {
		msg.rxMode = RXMODE_DMA;
		msg.txMode = TXMODE_DMA;
	} else {
		msg.rxMode = RXMODE_BYHAND;
		msg.txMode = TXMODE_BYHAND;
	}

	msg.lcr = (p_priv->cflag & CSTOPB) ? STOPBITS_678_2 : STOPBITS_5678_1;
	switch (p_priv->cflag & CSIZE) {
	case CS5:
		msg.lcr |= USA_DATABITS_5;
		break;
	case CS6:
		msg.lcr |= USA_DATABITS_6;
		break;
	case CS7:
		msg.lcr |= USA_DATABITS_7;
		break;
	case CS8:
		msg.lcr |= USA_DATABITS_8;
		break;
	}
	if (p_priv->cflag & PARENB) {
		/* note USA_PARITY_NONE == 0 */
		msg.lcr |= (p_priv->cflag & PARODD) ?
			USA_PARITY_ODD : USA_PARITY_EVEN;
	}
	if (p_priv->old_cflag != p_priv->cflag) {
		p_priv->old_cflag = p_priv->cflag;
		msg.setLcr = 0x01;
	}

	if (p_priv->flow_control == flow_cts)
		msg.txFlowControl = TXFLOW_CTS;
	msg.setTxFlowControl = 0x01;
	msg.setRxFlowControl = 0x01;

	msg.rxForwardingLength = 16;
	msg.rxForwardingTimeout = 16;
	msg.txAckSetting = 0;
	msg.xonChar = 17;
	msg.xoffChar = 19;

	/* Opening port */
	if (reset_port == 1) {
		msg.portEnabled = 1;
		msg.rxFlush = 1;
		msg.txBreak = (p_priv->break_on);
	}
	/* Closing port */
	else if (reset_port == 2)
		msg.portEnabled = 0;
	/* Sending intermediate configs */
	else {
		msg.portEnabled = 1;
		msg.txBreak = (p_priv->break_on);
	}

	/* Do handshaking outputs */
	msg.setRts = 0x01;
	msg.rts = p_priv->rts_state;

	msg.setDtr = 0x01;
	msg.dtr = p_priv->dtr_state;

	p_priv->resend_cont = 0;
	memcpy(this_urb->transfer_buffer, &msg, sizeof(msg));

	/* send the data out the device on control endpoint */
	this_urb->transfer_buffer_length = sizeof(msg);

	err = usb_submit_urb(this_urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - usb_submit_urb(setup) failed (%d)\n", __func__, err);
	return 0;
}

static int keyspan_usa67_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port)
{
	struct keyspan_usa67_portControlMessage	msg;
	struct keyspan_serial_private 		*s_priv;
	struct keyspan_port_private 		*p_priv;
	const struct keyspan_device_details	*d_details;
	struct urb				*this_urb;
	int 					err, device_port;

	s_priv = usb_get_serial_data(serial);
	p_priv = usb_get_serial_port_data(port);
	d_details = s_priv->device_details;

	this_urb = s_priv->glocont_urb;

	/* Work out which port within the device is being setup */
	device_port = port->port_number;

	/* Make sure we have an urb then send the message */
	if (this_urb == NULL) {
		dev_dbg(&port->dev, "%s - oops no urb for port.\n", __func__);
		return -1;
	}

	/* Save reset port val for resend.
	   Don't overwrite resend for open/close condition. */
	if ((reset_port + 1) > p_priv->resend_cont)
		p_priv->resend_cont = reset_port + 1;
	if (this_urb->status == -EINPROGRESS) {
		/*  dev_dbg(&port->dev, "%s - already writing\n", __func__); */
		mdelay(5);
		return -1;
	}

	memset(&msg, 0, sizeof(struct keyspan_usa67_portControlMessage));

	msg.port = device_port;

	/* Only set baud rate if it's changed */
	if (p_priv->old_baud != p_priv->baud) {
		p_priv->old_baud = p_priv->baud;
		msg.setClocking = 0xff;
		if (d_details->calculate_baud_rate(port, p_priv->baud, d_details->baudclk,
						   &msg.baudHi, &msg.baudLo, &msg.prescaler,
						   device_port) == KEYSPAN_INVALID_BAUD_RATE) {
			dev_dbg(&port->dev, "%s - Invalid baud rate %d requested, using 9600.\n",
				__func__, p_priv->baud);
			msg.baudLo = 0;
			msg.baudHi = 125;	/* Values for 9600 baud */
			msg.prescaler = 10;
		}
		msg.setPrescaler = 0xff;
	}

	msg.lcr = (p_priv->cflag & CSTOPB) ? STOPBITS_678_2 : STOPBITS_5678_1;
	switch (p_priv->cflag & CSIZE) {
	case CS5:
		msg.lcr |= USA_DATABITS_5;
		break;
	case CS6:
		msg.lcr |= USA_DATABITS_6;
		break;
	case CS7:
		msg.lcr |= USA_DATABITS_7;
		break;
	case CS8:
		msg.lcr |= USA_DATABITS_8;
		break;
	}
	if (p_priv->cflag & PARENB) {
		/* note USA_PARITY_NONE == 0 */
		msg.lcr |= (p_priv->cflag & PARODD) ?
					USA_PARITY_ODD : USA_PARITY_EVEN;
	}
	msg.setLcr = 0xff;

	msg.ctsFlowControl = (p_priv->flow_control == flow_cts);
	msg.xonFlowControl = 0;
	msg.setFlowControl = 0xff;
	msg.forwardingLength = 16;
	msg.xonChar = 17;
	msg.xoffChar = 19;

	if (reset_port == 1) {
		/* Opening port */
		msg._txOn = 1;
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txBreak = 0;
		msg.rxOn = 1;
		msg.rxOff = 0;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0xff;
	} else if (reset_port == 2) {
		/* Closing port */
		msg._txOn = 0;
		msg._txOff = 1;
		msg.txFlush = 0;
		msg.txBreak = 0;
		msg.rxOn = 0;
		msg.rxOff = 1;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0;
	} else {
		/* Sending intermediate configs */
		msg._txOn = (!p_priv->break_on);
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txBreak = (p_priv->break_on);
		msg.rxOn = 0;
		msg.rxOff = 0;
		msg.rxFlush = 0;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0x0;
	}

	/* Do handshaking outputs */
	msg.setTxTriState_setRts = 0xff;
	msg.txTriState_rts = p_priv->rts_state;

	msg.setHskoa_setDtr = 0xff;
	msg.hskoa_dtr = p_priv->dtr_state;

	p_priv->resend_cont = 0;

	memcpy(this_urb->transfer_buffer, &msg, sizeof(msg));

	/* send the data out the device on control endpoint */
	this_urb->transfer_buffer_length = sizeof(msg);

	err = usb_submit_urb(this_urb, GFP_ATOMIC);
	if (err != 0)
		dev_dbg(&port->dev, "%s - usb_submit_urb(setup) failed (%d)\n", __func__, err);
	return 0;
}

static void keyspan_send_setup(struct usb_serial_port *port, int reset_port)
{
	struct usb_serial *serial = port->serial;
	struct keyspan_serial_private *s_priv;
	const struct keyspan_device_details *d_details;

	s_priv = usb_get_serial_data(serial);
	d_details = s_priv->device_details;

	switch (d_details->msg_format) {
	case msg_usa26:
		keyspan_usa26_send_setup(serial, port, reset_port);
		break;
	case msg_usa28:
		keyspan_usa28_send_setup(serial, port, reset_port);
		break;
	case msg_usa49:
		keyspan_usa49_send_setup(serial, port, reset_port);
		break;
	case msg_usa90:
		keyspan_usa90_send_setup(serial, port, reset_port);
		break;
	case msg_usa67:
		keyspan_usa67_send_setup(serial, port, reset_port);
		break;
	}
}


/* Gets called by the "real" driver (ie once firmware is loaded
   and renumeration has taken place. */
static int keyspan_startup(struct usb_serial *serial)
{
	int				i, err;
	struct keyspan_serial_private 	*s_priv;
	const struct keyspan_device_details	*d_details;

	for (i = 0; (d_details = keyspan_devices[i]) != NULL; ++i)
		if (d_details->product_id ==
				le16_to_cpu(serial->dev->descriptor.idProduct))
			break;
	if (d_details == NULL) {
		dev_err(&serial->dev->dev, "%s - unknown product id %x\n",
		    __func__, le16_to_cpu(serial->dev->descriptor.idProduct));
		return -ENODEV;
	}

	/* Setup private data for serial driver */
	s_priv = kzalloc(sizeof(struct keyspan_serial_private), GFP_KERNEL);
	if (!s_priv)
		return -ENOMEM;

	s_priv->instat_buf = kzalloc(INSTAT_BUFLEN, GFP_KERNEL);
	if (!s_priv->instat_buf)
		goto err_instat_buf;

	s_priv->indat_buf = kzalloc(INDAT49W_BUFLEN, GFP_KERNEL);
	if (!s_priv->indat_buf)
		goto err_indat_buf;

	s_priv->glocont_buf = kzalloc(GLOCONT_BUFLEN, GFP_KERNEL);
	if (!s_priv->glocont_buf)
		goto err_glocont_buf;

	s_priv->ctrl_buf = kzalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!s_priv->ctrl_buf)
		goto err_ctrl_buf;

	s_priv->device_details = d_details;
	usb_set_serial_data(serial, s_priv);

	keyspan_setup_urbs(serial);

	if (s_priv->instat_urb != NULL) {
		err = usb_submit_urb(s_priv->instat_urb, GFP_KERNEL);
		if (err != 0)
			dev_dbg(&serial->dev->dev, "%s - submit instat urb failed %d\n", __func__, err);
	}
	if (s_priv->indat_urb != NULL) {
		err = usb_submit_urb(s_priv->indat_urb, GFP_KERNEL);
		if (err != 0)
			dev_dbg(&serial->dev->dev, "%s - submit indat urb failed %d\n", __func__, err);
	}

	return 0;

err_ctrl_buf:
	kfree(s_priv->glocont_buf);
err_glocont_buf:
	kfree(s_priv->indat_buf);
err_indat_buf:
	kfree(s_priv->instat_buf);
err_instat_buf:
	kfree(s_priv);

	return -ENOMEM;
}

static void keyspan_disconnect(struct usb_serial *serial)
{
	struct keyspan_serial_private *s_priv;

	s_priv = usb_get_serial_data(serial);

	usb_kill_urb(s_priv->instat_urb);
	usb_kill_urb(s_priv->glocont_urb);
	usb_kill_urb(s_priv->indat_urb);
}

static void keyspan_release(struct usb_serial *serial)
{
	struct keyspan_serial_private *s_priv;

	s_priv = usb_get_serial_data(serial);

	/* Make sure to unlink the URBs submitted in attach. */
	usb_kill_urb(s_priv->instat_urb);
	usb_kill_urb(s_priv->indat_urb);

	usb_free_urb(s_priv->instat_urb);
	usb_free_urb(s_priv->indat_urb);
	usb_free_urb(s_priv->glocont_urb);

	kfree(s_priv->ctrl_buf);
	kfree(s_priv->glocont_buf);
	kfree(s_priv->indat_buf);
	kfree(s_priv->instat_buf);

	kfree(s_priv);
}

static int keyspan_port_probe(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct keyspan_serial_private *s_priv;
	struct keyspan_port_private *p_priv;
	const struct keyspan_device_details *d_details;
	struct callbacks *cback;
	int endp;
	int port_num;
	int i;

	s_priv = usb_get_serial_data(serial);
	d_details = s_priv->device_details;

	p_priv = kzalloc(sizeof(*p_priv), GFP_KERNEL);
	if (!p_priv)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(p_priv->in_buffer); ++i) {
		p_priv->in_buffer[i] = kzalloc(IN_BUFLEN, GFP_KERNEL);
		if (!p_priv->in_buffer[i])
			goto err_free_in_buffer;
	}

	for (i = 0; i < ARRAY_SIZE(p_priv->out_buffer); ++i) {
		p_priv->out_buffer[i] = kzalloc(OUT_BUFLEN, GFP_KERNEL);
		if (!p_priv->out_buffer[i])
			goto err_free_out_buffer;
	}

	p_priv->inack_buffer = kzalloc(INACK_BUFLEN, GFP_KERNEL);
	if (!p_priv->inack_buffer)
		goto err_free_out_buffer;

	p_priv->outcont_buffer = kzalloc(OUTCONT_BUFLEN, GFP_KERNEL);
	if (!p_priv->outcont_buffer)
		goto err_free_inack_buffer;

	p_priv->device_details = d_details;

	/* Setup values for the various callback routines */
	cback = &keyspan_callbacks[d_details->msg_format];

	port_num = port->port_number;

	/* Do indat endpoints first, once for each flip */
	endp = d_details->indat_endpoints[port_num];
	for (i = 0; i <= d_details->indat_endp_flip; ++i, ++endp) {
		p_priv->in_urbs[i] = keyspan_setup_urb(serial, endp,
						USB_DIR_IN, port,
						p_priv->in_buffer[i],
						IN_BUFLEN,
						cback->indat_callback);
	}
	/* outdat endpoints also have flip */
	endp = d_details->outdat_endpoints[port_num];
	for (i = 0; i <= d_details->outdat_endp_flip; ++i, ++endp) {
		p_priv->out_urbs[i] = keyspan_setup_urb(serial, endp,
						USB_DIR_OUT, port,
						p_priv->out_buffer[i],
						OUT_BUFLEN,
						cback->outdat_callback);
	}
	/* inack endpoint */
	p_priv->inack_urb = keyspan_setup_urb(serial,
					d_details->inack_endpoints[port_num],
					USB_DIR_IN, port,
					p_priv->inack_buffer,
					INACK_BUFLEN,
					cback->inack_callback);
	/* outcont endpoint */
	p_priv->outcont_urb = keyspan_setup_urb(serial,
					d_details->outcont_endpoints[port_num],
					USB_DIR_OUT, port,
					p_priv->outcont_buffer,
					OUTCONT_BUFLEN,
					 cback->outcont_callback);

	usb_set_serial_port_data(port, p_priv);

	return 0;

err_free_inack_buffer:
	kfree(p_priv->inack_buffer);
err_free_out_buffer:
	for (i = 0; i < ARRAY_SIZE(p_priv->out_buffer); ++i)
		kfree(p_priv->out_buffer[i]);
err_free_in_buffer:
	for (i = 0; i < ARRAY_SIZE(p_priv->in_buffer); ++i)
		kfree(p_priv->in_buffer[i]);
	kfree(p_priv);

	return -ENOMEM;
}

static void keyspan_port_remove(struct usb_serial_port *port)
{
	struct keyspan_port_private *p_priv;
	int i;

	p_priv = usb_get_serial_port_data(port);

	usb_kill_urb(p_priv->inack_urb);
	usb_kill_urb(p_priv->outcont_urb);
	for (i = 0; i < 2; i++) {
		usb_kill_urb(p_priv->in_urbs[i]);
		usb_kill_urb(p_priv->out_urbs[i]);
	}

	usb_free_urb(p_priv->inack_urb);
	usb_free_urb(p_priv->outcont_urb);
	for (i = 0; i < 2; i++) {
		usb_free_urb(p_priv->in_urbs[i]);
		usb_free_urb(p_priv->out_urbs[i]);
	}

	kfree(p_priv->outcont_buffer);
	kfree(p_priv->inack_buffer);
	for (i = 0; i < ARRAY_SIZE(p_priv->out_buffer); ++i)
		kfree(p_priv->out_buffer[i]);
	for (i = 0; i < ARRAY_SIZE(p_priv->in_buffer); ++i)
		kfree(p_priv->in_buffer[i]);

	kfree(p_priv);
}

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

module_usb_serial_driver(serial_drivers, keyspan_ids_combined);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

MODULE_FIRMWARE("keyspan/usa28.fw");
MODULE_FIRMWARE("keyspan/usa28x.fw");
MODULE_FIRMWARE("keyspan/usa28xa.fw");
MODULE_FIRMWARE("keyspan/usa28xb.fw");
MODULE_FIRMWARE("keyspan/usa19.fw");
MODULE_FIRMWARE("keyspan/usa19qi.fw");
MODULE_FIRMWARE("keyspan/mpr.fw");
MODULE_FIRMWARE("keyspan/usa19qw.fw");
MODULE_FIRMWARE("keyspan/usa18x.fw");
MODULE_FIRMWARE("keyspan/usa19w.fw");
MODULE_FIRMWARE("keyspan/usa49w.fw");
MODULE_FIRMWARE("keyspan/usa49wlc.fw");
