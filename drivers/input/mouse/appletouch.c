/*
 * Apple USB Touchpad (for post-February 2005 PowerBooks and MacBooks) driver
 *
 * Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2005-2008 Johannes Berg (johannes@sipsolutions.net)
 * Copyright (C) 2005-2008 Stelian Pop (stelian@popies.net)
 * Copyright (C) 2005      Frank Arnold (frank@scirocco-5v-turbo.de)
 * Copyright (C) 2005      Peter Osterlund (petero2@telia.com)
 * Copyright (C) 2005      Michael Hanselmann (linux-kernel@hansmi.ch)
 * Copyright (C) 2006      Nicolas Boichat (nicolas@boichat.ch)
 * Copyright (C) 2007-2008 Sven Anders (anders@anduras.de)
 *
 * Thanks to Alex Harper <basilisk@foobox.net> for his inputs.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb/input.h>

/*
 * Note: We try to keep the touchpad aspect ratio while still doing only
 * simple arithmetics:
 *	0 <= x <= (xsensors - 1) * xfact
 *	0 <= y <= (ysensors - 1) * yfact
 */
struct atp_info {
	int xsensors;				/* number of X sensors */
	int xsensors_17;			/* 17" models have more sensors */
	int ysensors;				/* number of Y sensors */
	int xfact;				/* X multiplication factor */
	int yfact;				/* Y multiplication factor */
	int datalen;				/* size of USB transfers */
	void (*callback)(struct urb *);		/* callback function */
};

static void atp_complete_geyser_1_2(struct urb *urb);
static void atp_complete_geyser_3_4(struct urb *urb);

static const struct atp_info fountain_info = {
	.xsensors	= 16,
	.xsensors_17	= 26,
	.ysensors	= 16,
	.xfact		= 64,
	.yfact		= 43,
	.datalen	= 81,
	.callback	= atp_complete_geyser_1_2,
};

static const struct atp_info geyser1_info = {
	.xsensors	= 16,
	.xsensors_17	= 26,
	.ysensors	= 16,
	.xfact		= 64,
	.yfact		= 43,
	.datalen	= 81,
	.callback	= atp_complete_geyser_1_2,
};

static const struct atp_info geyser2_info = {
	.xsensors	= 15,
	.xsensors_17	= 20,
	.ysensors	= 9,
	.xfact		= 64,
	.yfact		= 43,
	.datalen	= 64,
	.callback	= atp_complete_geyser_1_2,
};

static const struct atp_info geyser3_info = {
	.xsensors	= 20,
	.ysensors	= 10,
	.xfact		= 64,
	.yfact		= 64,
	.datalen	= 64,
	.callback	= atp_complete_geyser_3_4,
};

static const struct atp_info geyser4_info = {
	.xsensors	= 20,
	.ysensors	= 10,
	.xfact		= 64,
	.yfact		= 64,
	.datalen	= 64,
	.callback	= atp_complete_geyser_3_4,
};

#define ATP_DEVICE(prod, info)					\
{								\
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE |		\
		       USB_DEVICE_ID_MATCH_INT_CLASS |		\
		       USB_DEVICE_ID_MATCH_INT_PROTOCOL,	\
	.idVendor = 0x05ac, /* Apple */				\
	.idProduct = (prod),					\
	.bInterfaceClass = 0x03,				\
	.bInterfaceProtocol = 0x02,				\
	.driver_info = (unsigned long) &info,			\
}

/*
 * Table of devices (Product IDs) that work with this driver.
 * (The names come from Info.plist in AppleUSBTrackpad.kext,
 *  According to Info.plist Geyser IV is the same as Geyser III.)
 */

static struct usb_device_id atp_table[] = {
	/* PowerBooks Feb 2005, iBooks G4 */
	ATP_DEVICE(0x020e, fountain_info),	/* FOUNTAIN ANSI */
	ATP_DEVICE(0x020f, fountain_info),	/* FOUNTAIN ISO */
	ATP_DEVICE(0x030a, fountain_info),	/* FOUNTAIN TP ONLY */
	ATP_DEVICE(0x030b, geyser1_info),	/* GEYSER 1 TP ONLY */

	/* PowerBooks Oct 2005 */
	ATP_DEVICE(0x0214, geyser2_info),	/* GEYSER 2 ANSI */
	ATP_DEVICE(0x0215, geyser2_info),	/* GEYSER 2 ISO */
	ATP_DEVICE(0x0216, geyser2_info),	/* GEYSER 2 JIS */

	/* Core Duo MacBook & MacBook Pro */
	ATP_DEVICE(0x0217, geyser3_info),	/* GEYSER 3 ANSI */
	ATP_DEVICE(0x0218, geyser3_info),	/* GEYSER 3 ISO */
	ATP_DEVICE(0x0219, geyser3_info),	/* GEYSER 3 JIS */

	/* Core2 Duo MacBook & MacBook Pro */
	ATP_DEVICE(0x021a, geyser4_info),	/* GEYSER 4 ANSI */
	ATP_DEVICE(0x021b, geyser4_info),	/* GEYSER 4 ISO */
	ATP_DEVICE(0x021c, geyser4_info),	/* GEYSER 4 JIS */

	/* Core2 Duo MacBook3,1 */
	ATP_DEVICE(0x0229, geyser4_info),	/* GEYSER 4 HF ANSI */
	ATP_DEVICE(0x022a, geyser4_info),	/* GEYSER 4 HF ISO */
	ATP_DEVICE(0x022b, geyser4_info),	/* GEYSER 4 HF JIS */

	/* Terminating entry */
	{ }
};
MODULE_DEVICE_TABLE(usb, atp_table);

/* maximum number of sensors */
#define ATP_XSENSORS	26
#define ATP_YSENSORS	16

/* amount of fuzz this touchpad generates */
#define ATP_FUZZ	16

/* maximum pressure this driver will report */
#define ATP_PRESSURE	300

/*
 * Threshold for the touchpad sensors. Any change less than ATP_THRESHOLD is
 * ignored.
 */
#define ATP_THRESHOLD	 5

/* Geyser initialization constants */
#define ATP_GEYSER_MODE_READ_REQUEST_ID		1
#define ATP_GEYSER_MODE_WRITE_REQUEST_ID	9
#define ATP_GEYSER_MODE_REQUEST_VALUE		0x300
#define ATP_GEYSER_MODE_REQUEST_INDEX		0
#define ATP_GEYSER_MODE_VENDOR_VALUE		0x04

/**
 * enum atp_status_bits - status bit meanings
 *
 * These constants represent the meaning of the status bits.
 * (only Geyser 3/4)
 *
 * @ATP_STATUS_BUTTON: The button was pressed
 * @ATP_STATUS_BASE_UPDATE: Update of the base values (untouched pad)
 * @ATP_STATUS_FROM_RESET: Reset previously performed
 */
enum atp_status_bits {
	ATP_STATUS_BUTTON	= BIT(0),
	ATP_STATUS_BASE_UPDATE	= BIT(2),
	ATP_STATUS_FROM_RESET	= BIT(4),
};

/* Structure to hold all of our device specific stuff */
struct atp {
	char			phys[64];
	struct usb_device	*udev;		/* usb device */
	struct urb		*urb;		/* usb request block */
	u8			*data;		/* transferred data */
	struct input_dev	*input;		/* input dev */
	const struct atp_info	*info;		/* touchpad model */
	bool			open;
	bool			valid;		/* are the samples valid? */
	bool			size_detect_done;
	bool			overflow_warned;
	int			x_old;		/* last reported x/y, */
	int			y_old;		/* used for smoothing */
	signed char		xy_cur[ATP_XSENSORS + ATP_YSENSORS];
	signed char		xy_old[ATP_XSENSORS + ATP_YSENSORS];
	int			xy_acc[ATP_XSENSORS + ATP_YSENSORS];
	int			idlecount;	/* number of empty packets */
	struct work_struct	work;
};

#define dbg_dump(msg, tab) \
	if (debug > 1) {						\
		int __i;						\
		printk(KERN_DEBUG "appletouch: %s", msg);		\
		for (__i = 0; __i < ATP_XSENSORS + ATP_YSENSORS; __i++)	\
			printk(" %02x", tab[__i]);			\
		printk("\n");						\
	}

#define dprintk(format, a...)						\
	do {								\
		if (debug)						\
			printk(KERN_DEBUG format, ##a);			\
	} while (0)

MODULE_AUTHOR("Johannes Berg");
MODULE_AUTHOR("Stelian Pop");
MODULE_AUTHOR("Frank Arnold");
MODULE_AUTHOR("Michael Hanselmann");
MODULE_AUTHOR("Sven Anders");
MODULE_DESCRIPTION("Apple PowerBook and MacBook USB touchpad driver");
MODULE_LICENSE("GPL");

/*
 * Make the threshold a module parameter
 */
static int threshold = ATP_THRESHOLD;
module_param(threshold, int, 0644);
MODULE_PARM_DESC(threshold, "Discard any change in data from a sensor"
			    " (the trackpad has many of these sensors)"
			    " less than this value.");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Activate debugging output");

/*
 * By default newer Geyser devices send standard USB HID mouse
 * packets (Report ID 2). This code changes device mode, so it
 * sends raw sensor reports (Report ID 5).
 */
static int atp_geyser_init(struct usb_device *udev)
{
	char *data;
	int size;
	int i;
	int ret;

	data = kmalloc(8, GFP_KERNEL);
	if (!data) {
		err("Out of memory");
		return -ENOMEM;
	}

	size = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			ATP_GEYSER_MODE_READ_REQUEST_ID,
			USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			ATP_GEYSER_MODE_REQUEST_VALUE,
			ATP_GEYSER_MODE_REQUEST_INDEX, data, 8, 5000);

	if (size != 8) {
		dprintk("atp_geyser_init: read error\n");
		for (i = 0; i < 8; i++)
			dprintk("appletouch[%d]: %d\n", i, data[i]);

		err("Failed to read mode from device.");
		ret = -EIO;
		goto out_free;
	}

	/* Apply the mode switch */
	data[0] = ATP_GEYSER_MODE_VENDOR_VALUE;

	size = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			ATP_GEYSER_MODE_WRITE_REQUEST_ID,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			ATP_GEYSER_MODE_REQUEST_VALUE,
			ATP_GEYSER_MODE_REQUEST_INDEX, data, 8, 5000);

	if (size != 8) {
		dprintk("atp_geyser_init: write error\n");
		for (i = 0; i < 8; i++)
			dprintk("appletouch[%d]: %d\n", i, data[i]);

		err("Failed to request geyser raw mode");
		ret = -EIO;
		goto out_free;
	}
	ret = 0;
out_free:
	kfree(data);
	return ret;
}

/*
 * Reinitialise the device. This usually stops stream of empty packets
 * coming from it.
 */
static void atp_reinit(struct work_struct *work)
{
	struct atp *dev = container_of(work, struct atp, work);
	struct usb_device *udev = dev->udev;
	int retval;

	dprintk("appletouch: putting appletouch to sleep (reinit)\n");
	atp_geyser_init(udev);

	retval = usb_submit_urb(dev->urb, GFP_ATOMIC);
	if (retval)
		err("atp_reinit: usb_submit_urb failed with error %d",
		    retval);
}

static int atp_calculate_abs(int *xy_sensors, int nb_sensors, int fact,
			     int *z, int *fingers)
{
	int i;
	/* values to calculate mean */
	int pcum = 0, psum = 0;
	int is_increasing = 0;

	*fingers = 0;

	for (i = 0; i < nb_sensors; i++) {
		if (xy_sensors[i] < threshold) {
			if (is_increasing)
				is_increasing = 0;

			continue;
		}

		/*
		 * Makes the finger detection more versatile.  For example,
		 * two fingers with no gap will be detected.  Also, my
		 * tests show it less likely to have intermittent loss
		 * of multiple finger readings while moving around (scrolling).
		 *
		 * Changes the multiple finger detection to counting humps on
		 * sensors (transitions from nonincreasing to increasing)
		 * instead of counting transitions from low sensors (no
		 * finger reading) to high sensors (finger above
		 * sensor)
		 *
		 * - Jason Parekh <jasonparekh@gmail.com>
		 */
		if (i < 1 ||
		    (!is_increasing && xy_sensors[i - 1] < xy_sensors[i])) {
			(*fingers)++;
			is_increasing = 1;
		} else if (i > 0 && (xy_sensors[i - 1] - xy_sensors[i] > threshold)) {
			is_increasing = 0;
		}

		/*
		 * Subtracts threshold so a high sensor that just passes the
		 * threshold won't skew the calculated absolute coordinate.
		 * Fixes an issue where slowly moving the mouse would
		 * occasionally jump a number of pixels (slowly moving the
		 * finger makes this issue most apparent.)
		 */
		pcum += (xy_sensors[i] - threshold) * i;
		psum += (xy_sensors[i] - threshold);
	}

	if (psum > 0) {
		*z = psum;
		return pcum * fact / psum;
	}

	return 0;
}

static inline void atp_report_fingers(struct input_dev *input, int fingers)
{
	input_report_key(input, BTN_TOOL_FINGER, fingers == 1);
	input_report_key(input, BTN_TOOL_DOUBLETAP, fingers == 2);
	input_report_key(input, BTN_TOOL_TRIPLETAP, fingers > 2);
}

/* Check URB status and for correct length of data package */

#define ATP_URB_STATUS_SUCCESS		0
#define ATP_URB_STATUS_ERROR		1
#define ATP_URB_STATUS_ERROR_FATAL	2

static int atp_status_check(struct urb *urb)
{
	struct atp *dev = urb->context;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -EOVERFLOW:
		if (!dev->overflow_warned) {
			printk(KERN_WARNING "appletouch: OVERFLOW with data "
				"length %d, actual length is %d\n",
				dev->info->datalen, dev->urb->actual_length);
			dev->overflow_warned = true;
		}
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* This urb is terminated, clean up */
		dbg("atp_complete: urb shutting down with status: %d",
		    urb->status);
		return ATP_URB_STATUS_ERROR_FATAL;

	default:
		dbg("atp_complete: nonzero urb status received: %d",
		    urb->status);
		return ATP_URB_STATUS_ERROR;
	}

	/* drop incomplete datasets */
	if (dev->urb->actual_length != dev->info->datalen) {
		dprintk("appletouch: incomplete data package"
			" (first byte: %d, length: %d).\n",
			dev->data[0], dev->urb->actual_length);
		return ATP_URB_STATUS_ERROR;
	}

	return ATP_URB_STATUS_SUCCESS;
}

static void atp_detect_size(struct atp *dev)
{
	int i;

	/* 17" Powerbooks have extra X sensors */
	for (i = dev->info->xsensors; i < ATP_XSENSORS; i++) {
		if (dev->xy_cur[i]) {

			printk(KERN_INFO "appletouch: 17\" model detected.\n");

			input_set_abs_params(dev->input, ABS_X, 0,
					     (dev->info->xsensors_17 - 1) *
							dev->info->xfact - 1,
					     ATP_FUZZ, 0);
			break;
		}
	}
}

/*
 * USB interrupt callback functions
 */

/* Interrupt function for older touchpads: FOUNTAIN/GEYSER1/GEYSER2 */

static void atp_complete_geyser_1_2(struct urb *urb)
{
	int x, y, x_z, y_z, x_f, y_f;
	int retval, i, j;
	int key;
	struct atp *dev = urb->context;
	int status = atp_status_check(urb);

	if (status == ATP_URB_STATUS_ERROR_FATAL)
		return;
	else if (status == ATP_URB_STATUS_ERROR)
		goto exit;

	/* reorder the sensors values */
	if (dev->info == &geyser2_info) {
		memset(dev->xy_cur, 0, sizeof(dev->xy_cur));

		/*
		 * The values are laid out like this:
		 * Y1, Y2, -, Y3, Y4, -, ..., X1, X2, -, X3, X4, -, ...
		 * '-' is an unused value.
		 */

		/* read X values */
		for (i = 0, j = 19; i < 20; i += 2, j += 3) {
			dev->xy_cur[i] = dev->data[j];
			dev->xy_cur[i + 1] = dev->data[j + 1];
		}

		/* read Y values */
		for (i = 0, j = 1; i < 9; i += 2, j += 3) {
			dev->xy_cur[ATP_XSENSORS + i] = dev->data[j];
			dev->xy_cur[ATP_XSENSORS + i + 1] = dev->data[j + 1];
		}
	} else {
		for (i = 0; i < 8; i++) {
			/* X values */
			dev->xy_cur[i +  0] = dev->data[5 * i +  2];
			dev->xy_cur[i +  8] = dev->data[5 * i +  4];
			dev->xy_cur[i + 16] = dev->data[5 * i + 42];
			if (i < 2)
				dev->xy_cur[i + 24] = dev->data[5 * i + 44];

			/* Y values */
			dev->xy_cur[ATP_XSENSORS + i] = dev->data[5 * i +  1];
			dev->xy_cur[ATP_XSENSORS + i + 8] = dev->data[5 * i + 3];
		}
	}

	dbg_dump("sample", dev->xy_cur);

	if (!dev->valid) {
		/* first sample */
		dev->valid = true;
		dev->x_old = dev->y_old = -1;

		/* Store first sample */
		memcpy(dev->xy_old, dev->xy_cur, sizeof(dev->xy_old));

		/* Perform size detection, if not done already */
		if (unlikely(!dev->size_detect_done)) {
			atp_detect_size(dev);
			dev->size_detect_done = 1;
			goto exit;
		}
	}

	for (i = 0; i < ATP_XSENSORS + ATP_YSENSORS; i++) {
		/* accumulate the change */
		signed char change = dev->xy_old[i] - dev->xy_cur[i];
		dev->xy_acc[i] -= change;

		/* prevent down drifting */
		if (dev->xy_acc[i] < 0)
			dev->xy_acc[i] = 0;
	}

	memcpy(dev->xy_old, dev->xy_cur, sizeof(dev->xy_old));

	dbg_dump("accumulator", dev->xy_acc);

	x = atp_calculate_abs(dev->xy_acc, ATP_XSENSORS,
			      dev->info->xfact, &x_z, &x_f);
	y = atp_calculate_abs(dev->xy_acc + ATP_XSENSORS, ATP_YSENSORS,
			      dev->info->yfact, &y_z, &y_f);
	key = dev->data[dev->info->datalen - 1] & ATP_STATUS_BUTTON;

	if (x && y) {
		if (dev->x_old != -1) {
			x = (dev->x_old * 3 + x) >> 2;
			y = (dev->y_old * 3 + y) >> 2;
			dev->x_old = x;
			dev->y_old = y;

			if (debug > 1)
				printk(KERN_DEBUG "appletouch: "
					"X: %3d Y: %3d Xz: %3d Yz: %3d\n",
					x, y, x_z, y_z);

			input_report_key(dev->input, BTN_TOUCH, 1);
			input_report_abs(dev->input, ABS_X, x);
			input_report_abs(dev->input, ABS_Y, y);
			input_report_abs(dev->input, ABS_PRESSURE,
					 min(ATP_PRESSURE, x_z + y_z));
			atp_report_fingers(dev->input, max(x_f, y_f));
		}
		dev->x_old = x;
		dev->y_old = y;

	} else if (!x && !y) {

		dev->x_old = dev->y_old = -1;
		input_report_key(dev->input, BTN_TOUCH, 0);
		input_report_abs(dev->input, ABS_PRESSURE, 0);
		atp_report_fingers(dev->input, 0);

		/* reset the accumulator on release */
		memset(dev->xy_acc, 0, sizeof(dev->xy_acc));
	}

	input_report_key(dev->input, BTN_LEFT, key);
	input_sync(dev->input);

 exit:
	retval = usb_submit_urb(dev->urb, GFP_ATOMIC);
	if (retval)
		err("atp_complete: usb_submit_urb failed with result %d",
		    retval);
}

/* Interrupt function for older touchpads: GEYSER3/GEYSER4 */

static void atp_complete_geyser_3_4(struct urb *urb)
{
	int x, y, x_z, y_z, x_f, y_f;
	int retval, i, j;
	int key;
	struct atp *dev = urb->context;
	int status = atp_status_check(urb);

	if (status == ATP_URB_STATUS_ERROR_FATAL)
		return;
	else if (status == ATP_URB_STATUS_ERROR)
		goto exit;

	/* Reorder the sensors values:
	 *
	 * The values are laid out like this:
	 * -, Y1, Y2, -, Y3, Y4, -, ..., -, X1, X2, -, X3, X4, ...
	 * '-' is an unused value.
	 */

	/* read X values */
	for (i = 0, j = 19; i < 20; i += 2, j += 3) {
		dev->xy_cur[i] = dev->data[j + 1];
		dev->xy_cur[i + 1] = dev->data[j + 2];
	}
	/* read Y values */
	for (i = 0, j = 1; i < 9; i += 2, j += 3) {
		dev->xy_cur[ATP_XSENSORS + i] = dev->data[j + 1];
		dev->xy_cur[ATP_XSENSORS + i + 1] = dev->data[j + 2];
	}

	dbg_dump("sample", dev->xy_cur);

	/* Just update the base values (i.e. touchpad in untouched state) */
	if (dev->data[dev->info->datalen - 1] & ATP_STATUS_BASE_UPDATE) {

		dprintk("appletouch: updated base values\n");

		memcpy(dev->xy_old, dev->xy_cur, sizeof(dev->xy_old));
		goto exit;
	}

	for (i = 0; i < ATP_XSENSORS + ATP_YSENSORS; i++) {
		/* calculate the change */
		dev->xy_acc[i] = dev->xy_cur[i] - dev->xy_old[i];

		/* this is a round-robin value, so couple with that */
		if (dev->xy_acc[i] > 127)
			dev->xy_acc[i] -= 256;

		if (dev->xy_acc[i] < -127)
			dev->xy_acc[i] += 256;

		/* prevent down drifting */
		if (dev->xy_acc[i] < 0)
			dev->xy_acc[i] = 0;
	}

	dbg_dump("accumulator", dev->xy_acc);

	x = atp_calculate_abs(dev->xy_acc, ATP_XSENSORS,
			      dev->info->xfact, &x_z, &x_f);
	y = atp_calculate_abs(dev->xy_acc + ATP_XSENSORS, ATP_YSENSORS,
			      dev->info->yfact, &y_z, &y_f);
	key = dev->data[dev->info->datalen - 1] & ATP_STATUS_BUTTON;

	if (x && y) {
		if (dev->x_old != -1) {
			x = (dev->x_old * 3 + x) >> 2;
			y = (dev->y_old * 3 + y) >> 2;
			dev->x_old = x;
			dev->y_old = y;

			if (debug > 1)
				printk(KERN_DEBUG "appletouch: X: %3d Y: %3d "
				       "Xz: %3d Yz: %3d\n",
				       x, y, x_z, y_z);

			input_report_key(dev->input, BTN_TOUCH, 1);
			input_report_abs(dev->input, ABS_X, x);
			input_report_abs(dev->input, ABS_Y, y);
			input_report_abs(dev->input, ABS_PRESSURE,
					 min(ATP_PRESSURE, x_z + y_z));
			atp_report_fingers(dev->input, max(x_f, y_f));
		}
		dev->x_old = x;
		dev->y_old = y;

	} else if (!x && !y) {

		dev->x_old = dev->y_old = -1;
		input_report_key(dev->input, BTN_TOUCH, 0);
		input_report_abs(dev->input, ABS_PRESSURE, 0);
		atp_report_fingers(dev->input, 0);

		/* reset the accumulator on release */
		memset(dev->xy_acc, 0, sizeof(dev->xy_acc));
	}

	input_report_key(dev->input, BTN_LEFT, key);
	input_sync(dev->input);

	/*
	 * Geysers 3/4 will continue to send packets continually after
	 * the first touch unless reinitialised. Do so if it's been
	 * idle for a while in order to avoid waking the kernel up
	 * several hundred times a second.
	 */

	/*
	 * Button must not be pressed when entering suspend,
	 * otherwise we will never release the button.
	 */
	if (!x && !y && !key) {
		dev->idlecount++;
		if (dev->idlecount == 10) {
			dev->x_old = dev->y_old = -1;
			dev->idlecount = 0;
			schedule_work(&dev->work);
			/* Don't resubmit urb here, wait for reinit */
			return;
		}
	} else
		dev->idlecount = 0;

 exit:
	retval = usb_submit_urb(dev->urb, GFP_ATOMIC);
	if (retval)
		err("atp_complete: usb_submit_urb failed with result %d",
		    retval);
}

static int atp_open(struct input_dev *input)
{
	struct atp *dev = input_get_drvdata(input);

	if (usb_submit_urb(dev->urb, GFP_ATOMIC))
		return -EIO;

	dev->open = 1;
	return 0;
}

static void atp_close(struct input_dev *input)
{
	struct atp *dev = input_get_drvdata(input);

	usb_kill_urb(dev->urb);
	cancel_work_sync(&dev->work);
	dev->open = 0;
}

static int atp_handle_geyser(struct atp *dev)
{
	struct usb_device *udev = dev->udev;

	if (dev->info != &fountain_info) {
		/* switch to raw sensor mode */
		if (atp_geyser_init(udev))
			return -EIO;

		printk(KERN_INFO "appletouch: Geyser mode initialized.\n");
	}

	return 0;
}

static int atp_probe(struct usb_interface *iface,
		     const struct usb_device_id *id)
{
	struct atp *dev;
	struct input_dev *input_dev;
	struct usb_device *udev = interface_to_usbdev(iface);
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int int_in_endpointAddr = 0;
	int i, error = -ENOMEM;
	const struct atp_info *info = (const struct atp_info *)id->driver_info;

	/* set up the endpoint information */
	/* use only the first interrupt-in endpoint */
	iface_desc = iface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (!int_in_endpointAddr && usb_endpoint_is_int_in(endpoint)) {
			/* we found an interrupt in endpoint */
			int_in_endpointAddr = endpoint->bEndpointAddress;
			break;
		}
	}
	if (!int_in_endpointAddr) {
		err("Could not find int-in endpoint");
		return -EIO;
	}

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(struct atp), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!dev || !input_dev) {
		err("Out of memory");
		goto err_free_devs;
	}

	dev->udev = udev;
	dev->input = input_dev;
	dev->info = info;
	dev->overflow_warned = false;

	dev->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->urb)
		goto err_free_devs;

	dev->data = usb_alloc_coherent(dev->udev, dev->info->datalen, GFP_KERNEL,
				       &dev->urb->transfer_dma);
	if (!dev->data)
		goto err_free_urb;

	usb_fill_int_urb(dev->urb, udev,
			 usb_rcvintpipe(udev, int_in_endpointAddr),
			 dev->data, dev->info->datalen,
			 dev->info->callback, dev, 1);

	error = atp_handle_geyser(dev);
	if (error)
		goto err_free_buffer;

	usb_make_path(udev, dev->phys, sizeof(dev->phys));
	strlcat(dev->phys, "/input0", sizeof(dev->phys));

	input_dev->name = "appletouch";
	input_dev->phys = dev->phys;
	usb_to_input_id(dev->udev, &input_dev->id);
	input_dev->dev.parent = &iface->dev;

	input_set_drvdata(input_dev, dev);

	input_dev->open = atp_open;
	input_dev->close = atp_close;

	set_bit(EV_ABS, input_dev->evbit);

	input_set_abs_params(input_dev, ABS_X, 0,
			     (dev->info->xsensors - 1) * dev->info->xfact - 1,
			     ATP_FUZZ, 0);
	input_set_abs_params(input_dev, ABS_Y, 0,
			     (dev->info->ysensors - 1) * dev->info->yfact - 1,
			     ATP_FUZZ, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, ATP_PRESSURE, 0, 0);

	set_bit(EV_KEY, input_dev->evbit);
	set_bit(BTN_TOUCH, input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, input_dev->keybit);
	set_bit(BTN_TOOL_DOUBLETAP, input_dev->keybit);
	set_bit(BTN_TOOL_TRIPLETAP, input_dev->keybit);
	set_bit(BTN_LEFT, input_dev->keybit);

	error = input_register_device(dev->input);
	if (error)
		goto err_free_buffer;

	/* save our data pointer in this interface device */
	usb_set_intfdata(iface, dev);

	INIT_WORK(&dev->work, atp_reinit);

	return 0;

 err_free_buffer:
	usb_free_coherent(dev->udev, dev->info->datalen,
			  dev->data, dev->urb->transfer_dma);
 err_free_urb:
	usb_free_urb(dev->urb);
 err_free_devs:
	usb_set_intfdata(iface, NULL);
	kfree(dev);
	input_free_device(input_dev);
	return error;
}

static void atp_disconnect(struct usb_interface *iface)
{
	struct atp *dev = usb_get_intfdata(iface);

	usb_set_intfdata(iface, NULL);
	if (dev) {
		usb_kill_urb(dev->urb);
		input_unregister_device(dev->input);
		usb_free_coherent(dev->udev, dev->info->datalen,
				  dev->data, dev->urb->transfer_dma);
		usb_free_urb(dev->urb);
		kfree(dev);
	}
	printk(KERN_INFO "input: appletouch disconnected\n");
}

static int atp_recover(struct atp *dev)
{
	int error;

	error = atp_handle_geyser(dev);
	if (error)
		return error;

	if (dev->open && usb_submit_urb(dev->urb, GFP_ATOMIC))
		return -EIO;

	return 0;
}

static int atp_suspend(struct usb_interface *iface, pm_message_t message)
{
	struct atp *dev = usb_get_intfdata(iface);

	usb_kill_urb(dev->urb);
	return 0;
}

static int atp_resume(struct usb_interface *iface)
{
	struct atp *dev = usb_get_intfdata(iface);

	if (dev->open && usb_submit_urb(dev->urb, GFP_ATOMIC))
		return -EIO;

	return 0;
}

static int atp_reset_resume(struct usb_interface *iface)
{
	struct atp *dev = usb_get_intfdata(iface);

	return atp_recover(dev);
}

static struct usb_driver atp_driver = {
	.name		= "appletouch",
	.probe		= atp_probe,
	.disconnect	= atp_disconnect,
	.suspend	= atp_suspend,
	.resume		= atp_resume,
	.reset_resume	= atp_reset_resume,
	.id_table	= atp_table,
};

static int __init atp_init(void)
{
	return usb_register(&atp_driver);
}

static void __exit atp_exit(void)
{
	usb_deregister(&atp_driver);
}

module_init(atp_init);
module_exit(atp_exit);
