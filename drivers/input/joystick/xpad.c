/*
 * X-Box gamepad - v0.0.6
 *
 * Copyright (c) 2002 Marko Friedemann <mfr@bmx-chemnitz.de>
 *               2004 Oliver Schwartz <Oliver.Schwartz@gmx.de>,
 *                    Steven Toth <steve@toth.demon.co.uk>,
 *                    Franz Lehner <franz@caos.at>,
 *                    Ivan Hawkes <blackhawk@ivanhawkes.com>
 *               2005 Dominic Cerquetti <binary1230@yahoo.com>
 *               2006 Adam Buchbinder <adam.buchbinder@gmail.com>
 *               2007 Jan Kratochvil <honza@jikos.cz>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
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
 *
 * This driver is based on:
 *  - information from     http://euc.jp/periphs/xbox-controller.ja.html
 *  - the iForce driver    drivers/char/joystick/iforce.c
 *  - the skeleton-driver  drivers/usb/usb-skeleton.c
 *  - Xbox 360 information http://www.free60.org/wiki/Gamepad
 *
 * Thanks to:
 *  - ITO Takayuki for providing essential xpad information on his website
 *  - Vojtech Pavlik     - iforce driver / input subsystem
 *  - Greg Kroah-Hartman - usb-skeleton driver
 *  - XBOX Linux project - extra USB id's
 *
 * TODO:
 *  - fine tune axes (especially trigger axes)
 *  - fix "analog" buttons (reported as digital now)
 *  - get rumble working
 *  - need USB IDs for other dance pads
 *
 * History:
 *
 * 2002-06-27 - 0.0.1 : first version, just said "XBOX HID controller"
 *
 * 2002-07-02 - 0.0.2 : basic working version
 *  - all axes and 9 of the 10 buttons work (german InterAct device)
 *  - the black button does not work
 *
 * 2002-07-14 - 0.0.3 : rework by Vojtech Pavlik
 *  - indentation fixes
 *  - usb + input init sequence fixes
 *
 * 2002-07-16 - 0.0.4 : minor changes, merge with Vojtech's v0.0.3
 *  - verified the lack of HID and report descriptors
 *  - verified that ALL buttons WORK
 *  - fixed d-pad to axes mapping
 *
 * 2002-07-17 - 0.0.5 : simplified d-pad handling
 *
 * 2004-10-02 - 0.0.6 : DDR pad support
 *  - borrowed from the XBOX linux kernel
 *  - USB id's for commonly used dance pads are present
 *  - dance pads will map D-PAD to buttons, not axes
 *  - pass the module paramater 'dpad_to_buttons' to force
 *    the D-PAD to map to buttons if your pad is not detected
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/usb/input.h>

#define DRIVER_VERSION "v0.0.6"
#define DRIVER_AUTHOR "Marko Friedemann <mfr@bmx-chemnitz.de>"
#define DRIVER_DESC "X-Box pad driver"

#define XPAD_PKT_LEN 32

/* xbox d-pads should map to buttons, as is required for DDR pads
   but we map them to axes when possible to simplify things */
#define MAP_DPAD_TO_BUTTONS    0
#define MAP_DPAD_TO_AXES       1
#define MAP_DPAD_UNKNOWN       -1

#define XTYPE_XBOX        0
#define XTYPE_XBOX360     1

static int dpad_to_buttons;
module_param(dpad_to_buttons, bool, S_IRUGO);
MODULE_PARM_DESC(dpad_to_buttons, "Map D-PAD to buttons rather than axes for unknown pads");

static const struct xpad_device {
	u16 idVendor;
	u16 idProduct;
	char *name;
	u8 dpad_mapping;
	u8 xtype;
} xpad_device[] = {
	{ 0x045e, 0x0202, "Microsoft X-Box pad v1 (US)", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x045e, 0x0289, "Microsoft X-Box pad v2 (US)", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x045e, 0x0285, "Microsoft X-Box pad (Japan)", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x045e, 0x0287, "Microsoft Xbox Controller S", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0c12, 0x8809, "RedOctane Xbox Dance Pad", MAP_DPAD_TO_BUTTONS, XTYPE_XBOX },
	{ 0x044f, 0x0f07, "Thrustmaster, Inc. Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x046d, 0xca84, "Logitech Xbox Cordless Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x046d, 0xca88, "Logitech Compact Controller for Xbox", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x05fd, 0x1007, "Mad Catz Controller (unverified)", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x05fd, 0x107a, "InterAct 'PowerPad Pro' X-Box pad (Germany)", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0738, 0x4516, "Mad Catz Control Pad", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0738, 0x4522, "Mad Catz LumiCON", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0738, 0x4526, "Mad Catz Control Pad Pro", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0738, 0x4536, "Mad Catz MicroCON", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0738, 0x4540, "Mad Catz Beat Pad", MAP_DPAD_TO_BUTTONS, XTYPE_XBOX },
	{ 0x0738, 0x4556, "Mad Catz Lynx Wireless Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0738, 0x6040, "Mad Catz Beat Pad Pro", MAP_DPAD_TO_BUTTONS, XTYPE_XBOX },
	{ 0x0c12, 0x8802, "Zeroplus Xbox Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0c12, 0x8810, "Zeroplus Xbox Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0c12, 0x9902, "HAMA VibraX - *FAULTY HARDWARE*", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0e4c, 0x1097, "Radica Gamester Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0e4c, 0x2390, "Radica Games Jtech Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0e6f, 0x0003, "Logic3 Freebird wireless Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0e6f, 0x0005, "Eclipse wireless Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0e6f, 0x0006, "Edge wireless Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0e8f, 0x0201, "SmartJoy Frag Xpad/PS2 adaptor", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0f30, 0x0202, "Joytech Advanced Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0f30, 0x8888, "BigBen XBMiniPad Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x102c, 0xff0c, "Joytech Wireless Advanced Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x12ab, 0x8809, "Xbox DDR dancepad", MAP_DPAD_TO_BUTTONS, XTYPE_XBOX },
	{ 0x1430, 0x8888, "TX6500+ Dance Pad (first generation)", MAP_DPAD_TO_BUTTONS, XTYPE_XBOX },
	{ 0x045e, 0x028e, "Microsoft X-Box 360 pad", MAP_DPAD_TO_AXES, XTYPE_XBOX360 },
	{ 0xffff, 0xffff, "Chinese-made Xbox Controller", MAP_DPAD_TO_AXES, XTYPE_XBOX },
	{ 0x0000, 0x0000, "Generic X-Box pad", MAP_DPAD_UNKNOWN, XTYPE_XBOX }
};

static const signed short xpad_btn[] = {
	BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z,	/* "analog" buttons */
	BTN_START, BTN_BACK, BTN_THUMBL, BTN_THUMBR,	/* start/back/sticks */
	-1						/* terminating entry */
};

/* only used if MAP_DPAD_TO_BUTTONS */
static const signed short xpad_btn_pad[] = {
	BTN_LEFT, BTN_RIGHT,		/* d-pad left, right */
	BTN_0, BTN_1,			/* d-pad up, down (XXX names??) */
	-1				/* terminating entry */
};

static const signed short xpad360_btn[] = {  /* buttons for x360 controller */
	BTN_TL, BTN_TR,		/* Button LB/RB */
	BTN_MODE,		/* The big X button */
	-1
};

static const signed short xpad_abs[] = {
	ABS_X, ABS_Y,		/* left stick */
	ABS_RX, ABS_RY,		/* right stick */
	ABS_Z, ABS_RZ,		/* triggers left/right */
	-1			/* terminating entry */
};

/* only used if MAP_DPAD_TO_AXES */
static const signed short xpad_abs_pad[] = {
	ABS_HAT0X, ABS_HAT0Y,	/* d-pad axes */
	-1			/* terminating entry */
};

/* Xbox 360 has a vendor-specific (sub)class, so we cannot match it with only
 * USB_INTERFACE_INFO, more to that this device has 4 InterfaceProtocols,
 * but we need only one of them. */
static struct usb_device_id xpad_table [] = {
	{ USB_INTERFACE_INFO('X', 'B', 0) },	/* X-Box USB-IF not approved class */
	{ USB_DEVICE_INTERFACE_PROTOCOL(0x045e, 0x028e, 1) },	/* X-Box 360 controller */
	{ }
};

MODULE_DEVICE_TABLE (usb, xpad_table);

struct usb_xpad {
	struct input_dev *dev;		/* input device interface */
	struct usb_device *udev;	/* usb device */

	struct urb *irq_in;		/* urb for interrupt in report */
	unsigned char *idata;		/* input data */
	dma_addr_t idata_dma;

#ifdef CONFIG_JOYSTICK_XPAD_FF
	struct urb *irq_out;		/* urb for interrupt out report */
	unsigned char *odata;		/* output data */
	dma_addr_t odata_dma;
#endif

	char phys[65];			/* physical device path */

	int dpad_mapping;		/* map d-pad to buttons or to axes */
	int xtype;			/* type of xbox device */
};

/*
 *	xpad_process_packet
 *
 *	Completes a request by converting the data into events for the
 *	input subsystem.
 *
 *	The used report descriptor was taken from ITO Takayukis website:
 *	 http://euc.jp/periphs/xbox-controller.ja.html
 */

static void xpad_process_packet(struct usb_xpad *xpad, u16 cmd, unsigned char *data)
{
	struct input_dev *dev = xpad->dev;

	/* left stick */
	input_report_abs(dev, ABS_X, (__s16) (((__s16)data[13] << 8) | data[12]));
	input_report_abs(dev, ABS_Y, (__s16) (((__s16)data[15] << 8) | data[14]));

	/* right stick */
	input_report_abs(dev, ABS_RX, (__s16) (((__s16)data[17] << 8) | data[16]));
	input_report_abs(dev, ABS_RY, (__s16) (((__s16)data[19] << 8) | data[18]));

	/* triggers left/right */
	input_report_abs(dev, ABS_Z, data[10]);
	input_report_abs(dev, ABS_RZ, data[11]);

	/* digital pad */
	if (xpad->dpad_mapping == MAP_DPAD_TO_AXES) {
		input_report_abs(dev, ABS_HAT0X, !!(data[2] & 0x08) - !!(data[2] & 0x04));
		input_report_abs(dev, ABS_HAT0Y, !!(data[2] & 0x02) - !!(data[2] & 0x01));
	} else /* xpad->dpad_mapping == MAP_DPAD_TO_BUTTONS */ {
		input_report_key(dev, BTN_LEFT,  data[2] & 0x04);
		input_report_key(dev, BTN_RIGHT, data[2] & 0x08);
		input_report_key(dev, BTN_0,     data[2] & 0x01); // up
		input_report_key(dev, BTN_1,     data[2] & 0x02); // down
	}

	/* start/back buttons and stick press left/right */
	input_report_key(dev, BTN_START,  data[2] & 0x10);
	input_report_key(dev, BTN_BACK,   data[2] & 0x20);
	input_report_key(dev, BTN_THUMBL, data[2] & 0x40);
	input_report_key(dev, BTN_THUMBR, data[2] & 0x80);

	/* "analog" buttons A, B, X, Y */
	input_report_key(dev, BTN_A, data[4]);
	input_report_key(dev, BTN_B, data[5]);
	input_report_key(dev, BTN_X, data[6]);
	input_report_key(dev, BTN_Y, data[7]);

	/* "analog" buttons black, white */
	input_report_key(dev, BTN_C, data[8]);
	input_report_key(dev, BTN_Z, data[9]);

	input_sync(dev);
}

/*
 *	xpad360_process_packet
 *
 *	Completes a request by converting the data into events for the
 *	input subsystem. It is version for xbox 360 controller
 *
 *	The used report descriptor was taken from:
 *		http://www.free60.org/wiki/Gamepad
 */

static void xpad360_process_packet(struct usb_xpad *xpad, u16 cmd, unsigned char *data)
{
	struct input_dev *dev = xpad->dev;

	/* digital pad */
	if (xpad->dpad_mapping == MAP_DPAD_TO_AXES) {
		input_report_abs(dev, ABS_HAT0X, !!(data[2] & 0x01) - !!((data[2] & 0x08) >> 3));
		input_report_abs(dev, ABS_HAT0Y, !!((data[2] & 0x02) >> 1) - !!((data[2] & 0x04) >> 2));
	} else if ( xpad->dpad_mapping == MAP_DPAD_TO_BUTTONS ) {
		/* dpad as buttons (right, left, down, up) */
		input_report_key(dev, BTN_RIGHT, (data[2] & 0x01));
		input_report_key(dev, BTN_LEFT, (data[2] & 0x08) >> 3);
		input_report_key(dev, BTN_0, (data[2] & 0x02) >> 1);
		input_report_key(dev, BTN_1, (data[2] & 0x04) >> 2);
	}

	/* start/back buttons */
	input_report_key(dev, BTN_START,  (data[2] & 0x10) >> 4);
	input_report_key(dev, BTN_BACK,   (data[2] & 0x20) >> 5);

	/* stick press left/right */
	input_report_key(dev, BTN_THUMBL, data[2] & 0x40);
	input_report_key(dev, BTN_THUMBR, data[2] & 0x80);

	/* buttons A,B,X,Y,TL,TR and MODE */
	input_report_key(dev, BTN_A, (data[3] & 0x10) >> 4);
	input_report_key(dev, BTN_B, (data[3] & 0x20) >> 5);
	input_report_key(dev, BTN_X, (data[3] & 0x40) >> 6);
	input_report_key(dev, BTN_Y, (data[3] & 0x80) >> 7);
	input_report_key(dev, BTN_TL, data[3] & 0x01 );
	input_report_key(dev, BTN_TR, (data[3] & 0x02) >> 1);
	input_report_key(dev, BTN_MODE, (data[3] & 0x04) >> 2);

	/* left stick */
	input_report_abs(dev, ABS_X, (__s16) (((__s16)data[7] << 8) | (__s16)data[6]));
	input_report_abs(dev, ABS_Y, ~(__s16) (((__s16)data[9] << 8) | (__s16)data[8]));

	/* right stick */
	input_report_abs(dev, ABS_RY, ~(__s16) (((__s16)data[13] << 8) | (__s16)data[12]));
	input_report_abs(dev, ABS_RX, (__s16) (((__s16)data[11] << 8) | (__s16)data[10]));

	/* triggers left/right */
	input_report_abs(dev, ABS_Z, data[4]);
	input_report_abs(dev, ABS_RZ, data[5]);

	input_sync(dev);
}

static void xpad_irq_in(struct urb *urb)
{
	struct usb_xpad *xpad = urb->context;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __FUNCTION__, urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __FUNCTION__, urb->status);
		goto exit;
	}

	if (xpad->xtype == XTYPE_XBOX360)
		xpad360_process_packet(xpad, 0, xpad->idata);
	else
		xpad_process_packet(xpad, 0, xpad->idata);

exit:
	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		err ("%s - usb_submit_urb failed with result %d",
		     __FUNCTION__, retval);
}

#ifdef CONFIG_JOYSTICK_XPAD_FF
static void xpad_irq_out(struct urb *urb)
{
	int retval;

	switch (urb->status) {
		case 0:
		/* success */
		break;
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
			/* this urb is terminated, clean up */
			dbg("%s - urb shutting down with status: %d",  __FUNCTION__, urb->status);
			return;
		default:
			dbg("%s - nonzero urb status received: %d",  __FUNCTION__, urb->status);
			goto exit;
	}

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		err("%s - usb_submit_urb failed with result %d",
		   __FUNCTION__, retval);
}

static int xpad_play_effect(struct input_dev *dev, void *data,
			    struct ff_effect *effect)
{
	struct usb_xpad *xpad = input_get_drvdata(dev);

	if (effect->type == FF_RUMBLE) {
		__u16 strong = effect->u.rumble.strong_magnitude;
		__u16 weak = effect->u.rumble.weak_magnitude;
		xpad->odata[0] = 0x00;
		xpad->odata[1] = 0x08;
		xpad->odata[2] = 0x00;
		xpad->odata[3] = strong / 256;
		xpad->odata[4] = weak / 256;
		xpad->odata[5] = 0x00;
		xpad->odata[6] = 0x00;
		xpad->odata[7] = 0x00;
		usb_submit_urb(xpad->irq_out, GFP_KERNEL);
	}

	return 0;
}

static int xpad_init_ff(struct usb_interface *intf, struct usb_xpad *xpad)
{
	struct usb_endpoint_descriptor *ep_irq_out;
	int error = -ENOMEM;

	if (xpad->xtype != XTYPE_XBOX360)
		return 0;

	xpad->odata = usb_buffer_alloc(xpad->udev, XPAD_PKT_LEN,
				       GFP_ATOMIC, &xpad->odata_dma );
	if (!xpad->odata)
		goto fail1;

	xpad->irq_out = usb_alloc_urb(0, GFP_KERNEL);
	if (!xpad->irq_out)
		goto fail2;

	ep_irq_out = &intf->cur_altsetting->endpoint[1].desc;
	usb_fill_int_urb(xpad->irq_out, xpad->udev,
			 usb_sndintpipe(xpad->udev, ep_irq_out->bEndpointAddress),
			 xpad->odata, XPAD_PKT_LEN,
			 xpad_irq_out, xpad, ep_irq_out->bInterval);
	xpad->irq_out->transfer_dma = xpad->odata_dma;
	xpad->irq_out->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	input_set_capability(xpad->dev, EV_FF, FF_RUMBLE);

	error = input_ff_create_memless(xpad->dev, NULL, xpad_play_effect);
	if (error)
		goto fail2;

	return 0;

 fail2:	usb_buffer_free(xpad->udev, XPAD_PKT_LEN, xpad->odata, xpad->odata_dma);
 fail1:	return error;
}

static void xpad_stop_ff(struct usb_xpad *xpad)
{
	if (xpad->xtype == XTYPE_XBOX360)
		usb_kill_urb(xpad->irq_out);
}

static void xpad_deinit_ff(struct usb_xpad *xpad)
{
	if (xpad->xtype == XTYPE_XBOX360) {
		usb_free_urb(xpad->irq_out);
		usb_buffer_free(xpad->udev, XPAD_PKT_LEN,
				xpad->odata, xpad->odata_dma);
	}
}

#else
static int xpad_init_ff(struct usb_interface *intf, struct usb_xpad *xpad) { return 0; }
static void xpad_stop_ff(struct usb_xpad *xpad) { }
static void xpad_deinit_ff(struct usb_xpad *xpad) { }
#endif

static int xpad_open(struct input_dev *dev)
{
	struct usb_xpad *xpad = input_get_drvdata(dev);

	xpad->irq_in->dev = xpad->udev;
	if (usb_submit_urb(xpad->irq_in, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void xpad_close(struct input_dev *dev)
{
	struct usb_xpad *xpad = input_get_drvdata(dev);

	usb_kill_urb(xpad->irq_in);
	xpad_stop_ff(xpad);
}

static void xpad_set_up_abs(struct input_dev *input_dev, signed short abs)
{
	set_bit(abs, input_dev->absbit);

	switch (abs) {
	case ABS_X:
	case ABS_Y:
	case ABS_RX:
	case ABS_RY:	/* the two sticks */
		input_set_abs_params(input_dev, abs, -32768, 32767, 16, 128);
		break;
	case ABS_Z:
	case ABS_RZ:	/* the triggers */
		input_set_abs_params(input_dev, abs, 0, 255, 0, 0);
		break;
	case ABS_HAT0X:
	case ABS_HAT0Y:	/* the d-pad (only if MAP_DPAD_TO_AXES) */
		input_set_abs_params(input_dev, abs, -1, 1, 0, 0);
		break;
	}
}

static int xpad_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev (intf);
	struct usb_xpad *xpad;
	struct input_dev *input_dev;
	struct usb_endpoint_descriptor *ep_irq_in;
	int i;
	int error = -ENOMEM;

	for (i = 0; xpad_device[i].idVendor; i++) {
		if ((le16_to_cpu(udev->descriptor.idVendor) == xpad_device[i].idVendor) &&
		    (le16_to_cpu(udev->descriptor.idProduct) == xpad_device[i].idProduct))
			break;
	}

	xpad = kzalloc(sizeof(struct usb_xpad), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!xpad || !input_dev)
		goto fail1;

	xpad->idata = usb_buffer_alloc(udev, XPAD_PKT_LEN,
				       GFP_ATOMIC, &xpad->idata_dma);
	if (!xpad->idata)
		goto fail1;

	xpad->irq_in = usb_alloc_urb(0, GFP_KERNEL);
	if (!xpad->irq_in)
		goto fail2;

	xpad->udev = udev;
	xpad->dpad_mapping = xpad_device[i].dpad_mapping;
	xpad->xtype = xpad_device[i].xtype;
	if (xpad->dpad_mapping == MAP_DPAD_UNKNOWN)
		xpad->dpad_mapping = dpad_to_buttons;
	xpad->dev = input_dev;
	usb_make_path(udev, xpad->phys, sizeof(xpad->phys));
	strlcat(xpad->phys, "/input0", sizeof(xpad->phys));

	input_dev->name = xpad_device[i].name;
	input_dev->phys = xpad->phys;
	usb_to_input_id(udev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, xpad);

	input_dev->open = xpad_open;
	input_dev->close = xpad_close;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

	/* set up buttons */
	for (i = 0; xpad_btn[i] >= 0; i++)
		set_bit(xpad_btn[i], input_dev->keybit);
	if (xpad->xtype == XTYPE_XBOX360)
		for (i = 0; xpad360_btn[i] >= 0; i++)
			set_bit(xpad360_btn[i], input_dev->keybit);
	if (xpad->dpad_mapping == MAP_DPAD_TO_BUTTONS)
		for (i = 0; xpad_btn_pad[i] >= 0; i++)
			set_bit(xpad_btn_pad[i], input_dev->keybit);

	/* set up axes */
	for (i = 0; xpad_abs[i] >= 0; i++)
		xpad_set_up_abs(input_dev, xpad_abs[i]);
	if (xpad->dpad_mapping == MAP_DPAD_TO_AXES)
		for (i = 0; xpad_abs_pad[i] >= 0; i++)
		    xpad_set_up_abs(input_dev, xpad_abs_pad[i]);

	error = xpad_init_ff(intf, xpad);
	if (error)
		goto fail2;

	ep_irq_in = &intf->cur_altsetting->endpoint[0].desc;
	usb_fill_int_urb(xpad->irq_in, udev,
			 usb_rcvintpipe(udev, ep_irq_in->bEndpointAddress),
			 xpad->idata, XPAD_PKT_LEN, xpad_irq_in,
			 xpad, ep_irq_in->bInterval);
	xpad->irq_in->transfer_dma = xpad->idata_dma;
	xpad->irq_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(xpad->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, xpad);
	return 0;

 fail3:	usb_free_urb(xpad->irq_in);
 fail2:	usb_buffer_free(udev, XPAD_PKT_LEN, xpad->idata, xpad->idata_dma);
 fail1:	input_free_device(input_dev);
	kfree(xpad);
	return error;

}

static void xpad_disconnect(struct usb_interface *intf)
{
	struct usb_xpad *xpad = usb_get_intfdata (intf);

	usb_set_intfdata(intf, NULL);
	if (xpad) {
		input_unregister_device(xpad->dev);
		xpad_deinit_ff(xpad);
		usb_free_urb(xpad->irq_in);
		usb_buffer_free(xpad->udev, XPAD_PKT_LEN,
				xpad->idata, xpad->idata_dma);
		kfree(xpad);
	}
}

static struct usb_driver xpad_driver = {
	.name		= "xpad",
	.probe		= xpad_probe,
	.disconnect	= xpad_disconnect,
	.id_table	= xpad_table,
};

static int __init usb_xpad_init(void)
{
	int result = usb_register(&xpad_driver);
	if (result == 0)
		info(DRIVER_DESC ":" DRIVER_VERSION);
	return result;
}

static void __exit usb_xpad_exit(void)
{
	usb_deregister(&xpad_driver);
}

module_init(usb_xpad_init);
module_exit(usb_xpad_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
