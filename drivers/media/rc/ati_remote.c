/*
 *  USB ATI Remote support
 *
 *                Copyright (c) 2011, 2012 Anssi Hannula <anssi.hannula@iki.fi>
 *  Version 2.2.0 Copyright (c) 2004 Torrey Hoffman <thoffman@arnor.net>
 *  Version 2.1.1 Copyright (c) 2002 Vladimir Dergachev
 *
 *  This 2.2.0 version is a rewrite / cleanup of the 2.1.1 driver, including
 *  porting to the 2.6 kernel interfaces, along with other modification
 *  to better match the style of the existing usb/input drivers.  However, the
 *  protocol and hardware handling is essentially unchanged from 2.1.1.
 *
 *  The 2.1.1 driver was derived from the usbati_remote and usbkbd drivers by
 *  Vojtech Pavlik.
 *
 *  Changes:
 *
 *  Feb 2004: Torrey Hoffman <thoffman@arnor.net>
 *            Version 2.2.0
 *  Jun 2004: Torrey Hoffman <thoffman@arnor.net>
 *            Version 2.2.1
 *            Added key repeat support contributed by:
 *                Vincent Vanackere <vanackere@lif.univ-mrs.fr>
 *            Added support for the "Lola" remote contributed by:
 *                Seth Cohn <sethcohn@yahoo.com>
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 * Hardware & software notes
 *
 * These remote controls are distributed by ATI as part of their
 * "All-In-Wonder" video card packages.  The receiver self-identifies as a
 * "USB Receiver" with manufacturer "X10 Wireless Technology Inc".
 *
 * The "Lola" remote is available from X10.  See:
 *    http://www.x10.com/products/lola_sg1.htm
 * The Lola is similar to the ATI remote but has no mouse support, and slightly
 * different keys.
 *
 * It is possible to use multiple receivers and remotes on multiple computers
 * simultaneously by configuring them to use specific channels.
 *
 * The RF protocol used by the remote supports 16 distinct channels, 1 to 16.
 * Actually, it may even support more, at least in some revisions of the
 * hardware.
 *
 * Each remote can be configured to transmit on one channel as follows:
 *   - Press and hold the "hand icon" button.
 *   - When the red LED starts to blink, let go of the "hand icon" button.
 *   - When it stops blinking, input the channel code as two digits, from 01
 *     to 16, and press the hand icon again.
 *
 * The timing can be a little tricky.  Try loading the module with debug=1
 * to have the kernel print out messages about the remote control number
 * and mask.  Note: debugging prints remote numbers as zero-based hexadecimal.
 *
 * The driver has a "channel_mask" parameter. This bitmask specifies which
 * channels will be ignored by the module.  To mask out channels, just add
 * all the 2^channel_number values together.
 *
 * For instance, set channel_mask = 2^4 = 16 (binary 10000) to make ati_remote
 * ignore signals coming from remote controls transmitting on channel 4, but
 * accept all other channels.
 *
 * Or, set channel_mask = 65533, (0xFFFD), and all channels except 1 will be
 * ignored.
 *
 * The default is 0 (respond to all channels). Bit 0 and bits 17-32 of this
 * parameter are unused.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/usb/input.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <media/rc-core.h>

/*
 * Module and Version Information, Module Parameters
 */

#define ATI_REMOTE_VENDOR_ID		0x0bc7
#define LOLA_REMOTE_PRODUCT_ID		0x0002
#define LOLA2_REMOTE_PRODUCT_ID		0x0003
#define ATI_REMOTE_PRODUCT_ID		0x0004
#define NVIDIA_REMOTE_PRODUCT_ID	0x0005
#define MEDION_REMOTE_PRODUCT_ID	0x0006
#define FIREFLY_REMOTE_PRODUCT_ID	0x0008

#define DRIVER_VERSION		"2.2.1"
#define DRIVER_AUTHOR           "Torrey Hoffman <thoffman@arnor.net>"
#define DRIVER_DESC             "ATI/X10 RF USB Remote Control"

#define NAME_BUFSIZE      80    /* size of product name, path buffers */
#define DATA_BUFSIZE      63    /* size of URB data buffers */

/*
 * Duplicate event filtering time.
 * Sequential, identical KIND_FILTERED inputs with less than
 * FILTER_TIME milliseconds between them are considered as repeat
 * events. The hardware generates 5 events for the first keypress
 * and we have to take this into account for an accurate repeat
 * behaviour.
 */
#define FILTER_TIME	60 /* msec */
#define REPEAT_DELAY	500 /* msec */

static unsigned long channel_mask;
module_param(channel_mask, ulong, 0644);
MODULE_PARM_DESC(channel_mask, "Bitmask of remote control channels to ignore");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable extra debug messages and information");

static int repeat_filter = FILTER_TIME;
module_param(repeat_filter, int, 0644);
MODULE_PARM_DESC(repeat_filter, "Repeat filter time, default = 60 msec");

static int repeat_delay = REPEAT_DELAY;
module_param(repeat_delay, int, 0644);
MODULE_PARM_DESC(repeat_delay, "Delay before sending repeats, default = 500 msec");

static bool mouse = true;
module_param(mouse, bool, 0444);
MODULE_PARM_DESC(mouse, "Enable mouse device, default = yes");

#define dbginfo(dev, format, arg...) \
	do { if (debug) dev_info(dev , format , ## arg); } while (0)
#undef err
#define err(format, arg...) printk(KERN_ERR format , ## arg)

struct ati_receiver_type {
	/* either default_keymap or get_default_keymap should be set */
	const char *default_keymap;
	const char *(*get_default_keymap)(struct usb_interface *interface);
};

static const char *get_medion_keymap(struct usb_interface *interface)
{
	struct usb_device *udev = interface_to_usbdev(interface);

	/*
	 * There are many different Medion remotes shipped with a receiver
	 * with the same usb id, but the receivers have subtle differences
	 * in the USB descriptors allowing us to detect them.
	 */

	if (udev->manufacturer && udev->product) {
		if (udev->actconfig->desc.bmAttributes & USB_CONFIG_ATT_WAKEUP) {

			if (!strcmp(udev->manufacturer, "X10 Wireless Technology Inc")
			    && !strcmp(udev->product, "USB Receiver"))
				return RC_MAP_MEDION_X10_DIGITAINER;

			if (!strcmp(udev->manufacturer, "X10 WTI")
			    && !strcmp(udev->product, "RF receiver"))
				return RC_MAP_MEDION_X10_OR2X;
		} else {

			 if (!strcmp(udev->manufacturer, "X10 Wireless Technology Inc")
			    && !strcmp(udev->product, "USB Receiver"))
				return RC_MAP_MEDION_X10;
		}
	}

	dev_info(&interface->dev,
		 "Unknown Medion X10 receiver, using default ati_remote Medion keymap\n");

	return RC_MAP_MEDION_X10;
}

static const struct ati_receiver_type type_ati		= {
	.default_keymap = RC_MAP_ATI_X10
};
static const struct ati_receiver_type type_medion	= {
	.get_default_keymap = get_medion_keymap
};
static const struct ati_receiver_type type_firefly	= {
	.default_keymap = RC_MAP_SNAPSTREAM_FIREFLY
};

static struct usb_device_id ati_remote_table[] = {
	{
		USB_DEVICE(ATI_REMOTE_VENDOR_ID, LOLA_REMOTE_PRODUCT_ID),
		.driver_info = (unsigned long)&type_ati
	},
	{
		USB_DEVICE(ATI_REMOTE_VENDOR_ID, LOLA2_REMOTE_PRODUCT_ID),
		.driver_info = (unsigned long)&type_ati
	},
	{
		USB_DEVICE(ATI_REMOTE_VENDOR_ID, ATI_REMOTE_PRODUCT_ID),
		.driver_info = (unsigned long)&type_ati
	},
	{
		USB_DEVICE(ATI_REMOTE_VENDOR_ID, NVIDIA_REMOTE_PRODUCT_ID),
		.driver_info = (unsigned long)&type_ati
	},
	{
		USB_DEVICE(ATI_REMOTE_VENDOR_ID, MEDION_REMOTE_PRODUCT_ID),
		.driver_info = (unsigned long)&type_medion
	},
	{
		USB_DEVICE(ATI_REMOTE_VENDOR_ID, FIREFLY_REMOTE_PRODUCT_ID),
		.driver_info = (unsigned long)&type_firefly
	},
	{}	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, ati_remote_table);

/* Get hi and low bytes of a 16-bits int */
#define HI(a)	((unsigned char)((a) >> 8))
#define LO(a)	((unsigned char)((a) & 0xff))

#define SEND_FLAG_IN_PROGRESS	1
#define SEND_FLAG_COMPLETE	2

/* Device initialization strings */
static char init1[] = { 0x01, 0x00, 0x20, 0x14 };
static char init2[] = { 0x01, 0x00, 0x20, 0x14, 0x20, 0x20, 0x20 };

struct ati_remote {
	struct input_dev *idev;
	struct rc_dev *rdev;
	struct usb_device *udev;
	struct usb_interface *interface;

	struct urb *irq_urb;
	struct urb *out_urb;
	struct usb_endpoint_descriptor *endpoint_in;
	struct usb_endpoint_descriptor *endpoint_out;
	unsigned char *inbuf;
	unsigned char *outbuf;
	dma_addr_t inbuf_dma;
	dma_addr_t outbuf_dma;

	unsigned char old_data;     /* Detect duplicate events */
	unsigned long old_jiffies;
	unsigned long acc_jiffies;  /* handle acceleration */
	unsigned long first_jiffies;

	unsigned int repeat_count;

	char rc_name[NAME_BUFSIZE];
	char rc_phys[NAME_BUFSIZE];
	char mouse_name[NAME_BUFSIZE];
	char mouse_phys[NAME_BUFSIZE];

	wait_queue_head_t wait;
	int send_flags;

	int users; /* 0-2, users are rc and input */
	struct mutex open_mutex;
};

/* "Kinds" of messages sent from the hardware to the driver. */
#define KIND_END        0
#define KIND_LITERAL    1   /* Simply pass to input system */
#define KIND_FILTERED   2   /* Add artificial key-up events, drop keyrepeats */
#define KIND_LU         3   /* Directional keypad diagonals - left up, */
#define KIND_RU         4   /*   right up,  */
#define KIND_LD         5   /*   left down, */
#define KIND_RD         6   /*   right down */
#define KIND_ACCEL      7   /* Directional keypad - left, right, up, down.*/

/* Translation table from hardware messages to input events. */
static const struct {
	short kind;
	unsigned char data;
	int type;
	unsigned int code;
	int value;
}  ati_remote_tbl[] = {
	/* Directional control pad axes */
	{KIND_ACCEL,   0x70, EV_REL, REL_X, -1},   /* left */
	{KIND_ACCEL,   0x71, EV_REL, REL_X, 1},    /* right */
	{KIND_ACCEL,   0x72, EV_REL, REL_Y, -1},   /* up */
	{KIND_ACCEL,   0x73, EV_REL, REL_Y, 1},    /* down */
	/* Directional control pad diagonals */
	{KIND_LU,      0x74, EV_REL, 0, 0},        /* left up */
	{KIND_RU,      0x75, EV_REL, 0, 0},        /* right up */
	{KIND_LD,      0x77, EV_REL, 0, 0},        /* left down */
	{KIND_RD,      0x76, EV_REL, 0, 0},        /* right down */

	/* "Mouse button" buttons */
	{KIND_LITERAL, 0x78, EV_KEY, BTN_LEFT, 1}, /* left btn down */
	{KIND_LITERAL, 0x79, EV_KEY, BTN_LEFT, 0}, /* left btn up */
	{KIND_LITERAL, 0x7c, EV_KEY, BTN_RIGHT, 1},/* right btn down */
	{KIND_LITERAL, 0x7d, EV_KEY, BTN_RIGHT, 0},/* right btn up */

	/* Artificial "doubleclick" events are generated by the hardware.
	 * They are mapped to the "side" and "extra" mouse buttons here. */
	{KIND_FILTERED, 0x7a, EV_KEY, BTN_SIDE, 1}, /* left dblclick */
	{KIND_FILTERED, 0x7e, EV_KEY, BTN_EXTRA, 1},/* right dblclick */

	/* Non-mouse events are handled by rc-core */
	{KIND_END, 0x00, EV_MAX + 1, 0, 0}
};

/*
 * ati_remote_dump_input
 */
static void ati_remote_dump(struct device *dev, unsigned char *data,
			    unsigned int len)
{
	if (len == 1) {
		if (data[0] != (unsigned char)0xff && data[0] != 0x00)
			dev_warn(dev, "Weird byte 0x%02x\n", data[0]);
	} else if (len == 4)
		dev_warn(dev, "Weird key %*ph\n", 4, data);
	else
		dev_warn(dev, "Weird data, len=%d %*ph ...\n", len, 6, data);
}

/*
 * ati_remote_open
 */
static int ati_remote_open(struct ati_remote *ati_remote)
{
	int err = 0;

	mutex_lock(&ati_remote->open_mutex);

	if (ati_remote->users++ != 0)
		goto out; /* one was already active */

	/* On first open, submit the read urb which was set up previously. */
	ati_remote->irq_urb->dev = ati_remote->udev;
	if (usb_submit_urb(ati_remote->irq_urb, GFP_KERNEL)) {
		dev_err(&ati_remote->interface->dev,
			"%s: usb_submit_urb failed!\n", __func__);
		err = -EIO;
	}

out:	mutex_unlock(&ati_remote->open_mutex);
	return err;
}

/*
 * ati_remote_close
 */
static void ati_remote_close(struct ati_remote *ati_remote)
{
	mutex_lock(&ati_remote->open_mutex);
	if (--ati_remote->users == 0)
		usb_kill_urb(ati_remote->irq_urb);
	mutex_unlock(&ati_remote->open_mutex);
}

static int ati_remote_input_open(struct input_dev *inputdev)
{
	struct ati_remote *ati_remote = input_get_drvdata(inputdev);
	return ati_remote_open(ati_remote);
}

static void ati_remote_input_close(struct input_dev *inputdev)
{
	struct ati_remote *ati_remote = input_get_drvdata(inputdev);
	ati_remote_close(ati_remote);
}

static int ati_remote_rc_open(struct rc_dev *rdev)
{
	struct ati_remote *ati_remote = rdev->priv;
	return ati_remote_open(ati_remote);
}

static void ati_remote_rc_close(struct rc_dev *rdev)
{
	struct ati_remote *ati_remote = rdev->priv;
	ati_remote_close(ati_remote);
}

/*
 * ati_remote_irq_out
 */
static void ati_remote_irq_out(struct urb *urb)
{
	struct ati_remote *ati_remote = urb->context;

	if (urb->status) {
		dev_dbg(&ati_remote->interface->dev, "%s: status %d\n",
			__func__, urb->status);
		return;
	}

	ati_remote->send_flags |= SEND_FLAG_COMPLETE;
	wmb();
	wake_up(&ati_remote->wait);
}

/*
 * ati_remote_sendpacket
 *
 * Used to send device initialization strings
 */
static int ati_remote_sendpacket(struct ati_remote *ati_remote, u16 cmd,
	unsigned char *data)
{
	int retval = 0;

	/* Set up out_urb */
	memcpy(ati_remote->out_urb->transfer_buffer + 1, data, LO(cmd));
	((char *) ati_remote->out_urb->transfer_buffer)[0] = HI(cmd);

	ati_remote->out_urb->transfer_buffer_length = LO(cmd) + 1;
	ati_remote->out_urb->dev = ati_remote->udev;
	ati_remote->send_flags = SEND_FLAG_IN_PROGRESS;

	retval = usb_submit_urb(ati_remote->out_urb, GFP_ATOMIC);
	if (retval) {
		dev_dbg(&ati_remote->interface->dev,
			 "sendpacket: usb_submit_urb failed: %d\n", retval);
		return retval;
	}

	wait_event_timeout(ati_remote->wait,
		((ati_remote->out_urb->status != -EINPROGRESS) ||
			(ati_remote->send_flags & SEND_FLAG_COMPLETE)),
		HZ);
	usb_kill_urb(ati_remote->out_urb);

	return retval;
}

/*
 * ati_remote_compute_accel
 *
 * Implements acceleration curve for directional control pad
 * If elapsed time since last event is > 1/4 second, user "stopped",
 * so reset acceleration. Otherwise, user is probably holding the control
 * pad down, so we increase acceleration, ramping up over two seconds to
 * a maximum speed.
 */
static int ati_remote_compute_accel(struct ati_remote *ati_remote)
{
	static const char accel[] = { 1, 2, 4, 6, 9, 13, 20 };
	unsigned long now = jiffies;
	int acc;

	if (time_after(now, ati_remote->old_jiffies + msecs_to_jiffies(250))) {
		acc = 1;
		ati_remote->acc_jiffies = now;
	}
	else if (time_before(now, ati_remote->acc_jiffies + msecs_to_jiffies(125)))
		acc = accel[0];
	else if (time_before(now, ati_remote->acc_jiffies + msecs_to_jiffies(250)))
		acc = accel[1];
	else if (time_before(now, ati_remote->acc_jiffies + msecs_to_jiffies(500)))
		acc = accel[2];
	else if (time_before(now, ati_remote->acc_jiffies + msecs_to_jiffies(1000)))
		acc = accel[3];
	else if (time_before(now, ati_remote->acc_jiffies + msecs_to_jiffies(1500)))
		acc = accel[4];
	else if (time_before(now, ati_remote->acc_jiffies + msecs_to_jiffies(2000)))
		acc = accel[5];
	else
		acc = accel[6];

	return acc;
}

/*
 * ati_remote_report_input
 */
static void ati_remote_input_report(struct urb *urb)
{
	struct ati_remote *ati_remote = urb->context;
	unsigned char *data= ati_remote->inbuf;
	struct input_dev *dev = ati_remote->idev;
	int index = -1;
	int acc;
	int remote_num;
	unsigned char scancode;
	u32 wheel_keycode = KEY_RESERVED;
	int i;

	/*
	 * data[0] = 0x14
	 * data[1] = data[2] + data[3] + 0xd5 (a checksum byte)
	 * data[2] = the key code (with toggle bit in MSB with some models)
	 * data[3] = channel << 4 (the low 4 bits must be zero)
	 */

	/* Deal with strange looking inputs */
	if ( (urb->actual_length != 4) || (data[0] != 0x14) ||
		((data[3] & 0x0f) != 0x00) ) {
		ati_remote_dump(&urb->dev->dev, data, urb->actual_length);
		return;
	}

	if (data[1] != ((data[2] + data[3] + 0xd5) & 0xff)) {
		dbginfo(&ati_remote->interface->dev,
			"wrong checksum in input: %*ph\n", 4, data);
		return;
	}

	/* Mask unwanted remote channels.  */
	/* note: remote_num is 0-based, channel 1 on remote == 0 here */
	remote_num = (data[3] >> 4) & 0x0f;
	if (channel_mask & (1 << (remote_num + 1))) {
		dbginfo(&ati_remote->interface->dev,
			"Masked input from channel 0x%02x: data %02x,%02x, "
			"mask= 0x%02lx\n",
			remote_num, data[1], data[2], channel_mask);
		return;
	}

	/*
	 * MSB is a toggle code, though only used by some devices
	 * (e.g. SnapStream Firefly)
	 */
	scancode = data[2] & 0x7f;

	dbginfo(&ati_remote->interface->dev,
		"channel 0x%02x; key data %02x, scancode %02x\n",
		remote_num, data[2], scancode);

	if (scancode >= 0x70) {
		/*
		 * This is either a mouse or scrollwheel event, depending on
		 * the remote/keymap.
		 * Get the keycode assigned to scancode 0x78/0x70. If it is
		 * set, assume this is a scrollwheel up/down event.
		 */
		wheel_keycode = rc_g_keycode_from_table(ati_remote->rdev,
							scancode & 0x78);

		if (wheel_keycode == KEY_RESERVED) {
			/* scrollwheel was not mapped, assume mouse */

			/* Look up event code index in the mouse translation
			 * table.
			 */
			for (i = 0; ati_remote_tbl[i].kind != KIND_END; i++) {
				if (scancode == ati_remote_tbl[i].data) {
					index = i;
					break;
				}
			}
		}
	}

	if (index >= 0 && ati_remote_tbl[index].kind == KIND_LITERAL) {
		input_event(dev, ati_remote_tbl[index].type,
			ati_remote_tbl[index].code,
			ati_remote_tbl[index].value);
		input_sync(dev);

		ati_remote->old_jiffies = jiffies;
		return;
	}

	if (index < 0 || ati_remote_tbl[index].kind == KIND_FILTERED) {
		unsigned long now = jiffies;

		/* Filter duplicate events which happen "too close" together. */
		if (ati_remote->old_data == data[2] &&
		    time_before(now, ati_remote->old_jiffies +
				     msecs_to_jiffies(repeat_filter))) {
			ati_remote->repeat_count++;
		} else {
			ati_remote->repeat_count = 0;
			ati_remote->first_jiffies = now;
		}

		ati_remote->old_data = data[2];
		ati_remote->old_jiffies = now;

		/* Ensure we skip at least the 4 first duplicate events (generated
		 * by a single keypress), and continue skipping until repeat_delay
		 * msecs have passed
		 */
		if (ati_remote->repeat_count > 0 &&
		    (ati_remote->repeat_count < 5 ||
		     time_before(now, ati_remote->first_jiffies +
				      msecs_to_jiffies(repeat_delay))))
			return;

		if (index < 0) {
			/* Not a mouse event, hand it to rc-core. */
			int count = 1;

			if (wheel_keycode != KEY_RESERVED) {
				/*
				 * This is a scrollwheel event, send the
				 * scroll up (0x78) / down (0x70) scancode
				 * repeatedly as many times as indicated by
				 * rest of the scancode.
				 */
				count = (scancode & 0x07) + 1;
				scancode &= 0x78;
			}

			while (count--) {
				/*
				* We don't use the rc-core repeat handling yet as
				* it would cause ghost repeats which would be a
				* regression for this driver.
				*/
				rc_keydown_notimeout(ati_remote->rdev, RC_TYPE_OTHER,
						     scancode, data[2]);
				rc_keyup(ati_remote->rdev);
			}
			return;
		}

		input_event(dev, ati_remote_tbl[index].type,
			ati_remote_tbl[index].code, 1);
		input_sync(dev);
		input_event(dev, ati_remote_tbl[index].type,
			ati_remote_tbl[index].code, 0);
		input_sync(dev);

	} else {

		/*
		 * Other event kinds are from the directional control pad, and
		 * have an acceleration factor applied to them.  Without this
		 * acceleration, the control pad is mostly unusable.
		 */
		acc = ati_remote_compute_accel(ati_remote);

		switch (ati_remote_tbl[index].kind) {
		case KIND_ACCEL:
			input_event(dev, ati_remote_tbl[index].type,
				ati_remote_tbl[index].code,
				ati_remote_tbl[index].value * acc);
			break;
		case KIND_LU:
			input_report_rel(dev, REL_X, -acc);
			input_report_rel(dev, REL_Y, -acc);
			break;
		case KIND_RU:
			input_report_rel(dev, REL_X, acc);
			input_report_rel(dev, REL_Y, -acc);
			break;
		case KIND_LD:
			input_report_rel(dev, REL_X, -acc);
			input_report_rel(dev, REL_Y, acc);
			break;
		case KIND_RD:
			input_report_rel(dev, REL_X, acc);
			input_report_rel(dev, REL_Y, acc);
			break;
		default:
			dev_dbg(&ati_remote->interface->dev,
				"ati_remote kind=%d\n",
				ati_remote_tbl[index].kind);
		}
		input_sync(dev);

		ati_remote->old_jiffies = jiffies;
		ati_remote->old_data = data[2];
	}
}

/*
 * ati_remote_irq_in
 */
static void ati_remote_irq_in(struct urb *urb)
{
	struct ati_remote *ati_remote = urb->context;
	int retval;

	switch (urb->status) {
	case 0:			/* success */
		ati_remote_input_report(urb);
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		dev_dbg(&ati_remote->interface->dev,
			"%s: urb error status, unlink?\n",
			__func__);
		return;
	default:		/* error */
		dev_dbg(&ati_remote->interface->dev,
			"%s: Nonzero urb status %d\n",
			__func__, urb->status);
	}

	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&ati_remote->interface->dev,
			"%s: usb_submit_urb()=%d\n",
			__func__, retval);
}

/*
 * ati_remote_alloc_buffers
 */
static int ati_remote_alloc_buffers(struct usb_device *udev,
				    struct ati_remote *ati_remote)
{
	ati_remote->inbuf = usb_alloc_coherent(udev, DATA_BUFSIZE, GFP_ATOMIC,
					       &ati_remote->inbuf_dma);
	if (!ati_remote->inbuf)
		return -1;

	ati_remote->outbuf = usb_alloc_coherent(udev, DATA_BUFSIZE, GFP_ATOMIC,
						&ati_remote->outbuf_dma);
	if (!ati_remote->outbuf)
		return -1;

	ati_remote->irq_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ati_remote->irq_urb)
		return -1;

	ati_remote->out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ati_remote->out_urb)
		return -1;

	return 0;
}

/*
 * ati_remote_free_buffers
 */
static void ati_remote_free_buffers(struct ati_remote *ati_remote)
{
	usb_free_urb(ati_remote->irq_urb);
	usb_free_urb(ati_remote->out_urb);

	usb_free_coherent(ati_remote->udev, DATA_BUFSIZE,
		ati_remote->inbuf, ati_remote->inbuf_dma);

	usb_free_coherent(ati_remote->udev, DATA_BUFSIZE,
		ati_remote->outbuf, ati_remote->outbuf_dma);
}

static void ati_remote_input_init(struct ati_remote *ati_remote)
{
	struct input_dev *idev = ati_remote->idev;
	int i;

	idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	idev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_SIDE) | BIT_MASK(BTN_EXTRA);
	idev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
	for (i = 0; ati_remote_tbl[i].kind != KIND_END; i++)
		if (ati_remote_tbl[i].type == EV_KEY)
			set_bit(ati_remote_tbl[i].code, idev->keybit);

	input_set_drvdata(idev, ati_remote);

	idev->open = ati_remote_input_open;
	idev->close = ati_remote_input_close;

	idev->name = ati_remote->mouse_name;
	idev->phys = ati_remote->mouse_phys;

	usb_to_input_id(ati_remote->udev, &idev->id);
	idev->dev.parent = &ati_remote->interface->dev;
}

static void ati_remote_rc_init(struct ati_remote *ati_remote)
{
	struct rc_dev *rdev = ati_remote->rdev;

	rdev->priv = ati_remote;
	rdev->driver_type = RC_DRIVER_SCANCODE;
	rdev->allowed_protocols = RC_BIT_OTHER;
	rdev->driver_name = "ati_remote";

	rdev->open = ati_remote_rc_open;
	rdev->close = ati_remote_rc_close;

	rdev->input_name = ati_remote->rc_name;
	rdev->input_phys = ati_remote->rc_phys;

	usb_to_input_id(ati_remote->udev, &rdev->input_id);
	rdev->dev.parent = &ati_remote->interface->dev;
}

static int ati_remote_initialize(struct ati_remote *ati_remote)
{
	struct usb_device *udev = ati_remote->udev;
	int pipe, maxp;

	init_waitqueue_head(&ati_remote->wait);

	/* Set up irq_urb */
	pipe = usb_rcvintpipe(udev, ati_remote->endpoint_in->bEndpointAddress);
	maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));
	maxp = (maxp > DATA_BUFSIZE) ? DATA_BUFSIZE : maxp;

	usb_fill_int_urb(ati_remote->irq_urb, udev, pipe, ati_remote->inbuf,
			 maxp, ati_remote_irq_in, ati_remote,
			 ati_remote->endpoint_in->bInterval);
	ati_remote->irq_urb->transfer_dma = ati_remote->inbuf_dma;
	ati_remote->irq_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* Set up out_urb */
	pipe = usb_sndintpipe(udev, ati_remote->endpoint_out->bEndpointAddress);
	maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));
	maxp = (maxp > DATA_BUFSIZE) ? DATA_BUFSIZE : maxp;

	usb_fill_int_urb(ati_remote->out_urb, udev, pipe, ati_remote->outbuf,
			 maxp, ati_remote_irq_out, ati_remote,
			 ati_remote->endpoint_out->bInterval);
	ati_remote->out_urb->transfer_dma = ati_remote->outbuf_dma;
	ati_remote->out_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* send initialization strings */
	if ((ati_remote_sendpacket(ati_remote, 0x8004, init1)) ||
	    (ati_remote_sendpacket(ati_remote, 0x8007, init2))) {
		dev_err(&ati_remote->interface->dev,
			 "Initializing ati_remote hardware failed.\n");
		return -EIO;
	}

	return 0;
}

/*
 * ati_remote_probe
 */
static int ati_remote_probe(struct usb_interface *interface,
	const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_host_interface *iface_host = interface->cur_altsetting;
	struct usb_endpoint_descriptor *endpoint_in, *endpoint_out;
	struct ati_receiver_type *type = (struct ati_receiver_type *)id->driver_info;
	struct ati_remote *ati_remote;
	struct input_dev *input_dev;
	struct rc_dev *rc_dev;
	int err = -ENOMEM;

	if (iface_host->desc.bNumEndpoints != 2) {
		err("%s: Unexpected desc.bNumEndpoints\n", __func__);
		return -ENODEV;
	}

	endpoint_in = &iface_host->endpoint[0].desc;
	endpoint_out = &iface_host->endpoint[1].desc;

	if (!usb_endpoint_is_int_in(endpoint_in)) {
		err("%s: Unexpected endpoint_in\n", __func__);
		return -ENODEV;
	}
	if (le16_to_cpu(endpoint_in->wMaxPacketSize) == 0) {
		err("%s: endpoint_in message size==0? \n", __func__);
		return -ENODEV;
	}

	ati_remote = kzalloc(sizeof (struct ati_remote), GFP_KERNEL);
	rc_dev = rc_allocate_device();
	if (!ati_remote || !rc_dev)
		goto exit_free_dev_rdev;

	/* Allocate URB buffers, URBs */
	if (ati_remote_alloc_buffers(udev, ati_remote))
		goto exit_free_buffers;

	ati_remote->endpoint_in = endpoint_in;
	ati_remote->endpoint_out = endpoint_out;
	ati_remote->udev = udev;
	ati_remote->rdev = rc_dev;
	ati_remote->interface = interface;

	usb_make_path(udev, ati_remote->rc_phys, sizeof(ati_remote->rc_phys));
	strlcpy(ati_remote->mouse_phys, ati_remote->rc_phys,
		sizeof(ati_remote->mouse_phys));

	strlcat(ati_remote->rc_phys, "/input0", sizeof(ati_remote->rc_phys));
	strlcat(ati_remote->mouse_phys, "/input1", sizeof(ati_remote->mouse_phys));

	if (udev->manufacturer)
		strlcpy(ati_remote->rc_name, udev->manufacturer,
			sizeof(ati_remote->rc_name));

	if (udev->product)
		snprintf(ati_remote->rc_name, sizeof(ati_remote->rc_name),
			 "%s %s", ati_remote->rc_name, udev->product);

	if (!strlen(ati_remote->rc_name))
		snprintf(ati_remote->rc_name, sizeof(ati_remote->rc_name),
			DRIVER_DESC "(%04x,%04x)",
			le16_to_cpu(ati_remote->udev->descriptor.idVendor),
			le16_to_cpu(ati_remote->udev->descriptor.idProduct));

	snprintf(ati_remote->mouse_name, sizeof(ati_remote->mouse_name),
		 "%s mouse", ati_remote->rc_name);

	rc_dev->map_name = RC_MAP_ATI_X10; /* default map */

	/* set default keymap according to receiver model */
	if (type) {
		if (type->default_keymap)
			rc_dev->map_name = type->default_keymap;
		else if (type->get_default_keymap)
			rc_dev->map_name = type->get_default_keymap(interface);
	}

	ati_remote_rc_init(ati_remote);
	mutex_init(&ati_remote->open_mutex);

	/* Device Hardware Initialization - fills in ati_remote->idev from udev. */
	err = ati_remote_initialize(ati_remote);
	if (err)
		goto exit_kill_urbs;

	/* Set up and register rc device */
	err = rc_register_device(ati_remote->rdev);
	if (err)
		goto exit_kill_urbs;

	/* use our delay for rc_dev */
	ati_remote->rdev->input_dev->rep[REP_DELAY] = repeat_delay;

	/* Set up and register mouse input device */
	if (mouse) {
		input_dev = input_allocate_device();
		if (!input_dev) {
			err = -ENOMEM;
			goto exit_unregister_device;
		}

		ati_remote->idev = input_dev;
		ati_remote_input_init(ati_remote);
		err = input_register_device(input_dev);

		if (err)
			goto exit_free_input_device;
	}

	usb_set_intfdata(interface, ati_remote);
	return 0;

 exit_free_input_device:
	input_free_device(input_dev);
 exit_unregister_device:
	rc_unregister_device(rc_dev);
	rc_dev = NULL;
 exit_kill_urbs:
	usb_kill_urb(ati_remote->irq_urb);
	usb_kill_urb(ati_remote->out_urb);
 exit_free_buffers:
	ati_remote_free_buffers(ati_remote);
 exit_free_dev_rdev:
	 rc_free_device(rc_dev);
	kfree(ati_remote);
	return err;
}

/*
 * ati_remote_disconnect
 */
static void ati_remote_disconnect(struct usb_interface *interface)
{
	struct ati_remote *ati_remote;

	ati_remote = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);
	if (!ati_remote) {
		dev_warn(&interface->dev, "%s - null device?\n", __func__);
		return;
	}

	usb_kill_urb(ati_remote->irq_urb);
	usb_kill_urb(ati_remote->out_urb);
	if (ati_remote->idev)
		input_unregister_device(ati_remote->idev);
	rc_unregister_device(ati_remote->rdev);
	ati_remote_free_buffers(ati_remote);
	kfree(ati_remote);
}

/* usb specific object to register with the usb subsystem */
static struct usb_driver ati_remote_driver = {
	.name         = "ati_remote",
	.probe        = ati_remote_probe,
	.disconnect   = ati_remote_disconnect,
	.id_table     = ati_remote_table,
};

module_usb_driver(ati_remote_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
