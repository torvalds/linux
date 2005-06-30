/*
 * X-Box gamepad - v0.0.5
 *
 * Copyright (c) 2002 Marko Friedemann <mfr@bmx-chemnitz.de>
 *
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
 *
 * Thanks to:
 *  - ITO Takayuki for providing essential xpad information on his website
 *  - Vojtech Pavlik     - iforce driver / input subsystem
 *  - Greg Kroah-Hartman - usb-skeleton driver
 *
 * TODO:
 *  - fine tune axes
 *  - fix "analog" buttons (reported as digital now)
 *  - get rumble working
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
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/usb.h>
#include <linux/usb_input.h>

#define DRIVER_VERSION "v0.0.5"
#define DRIVER_AUTHOR "Marko Friedemann <mfr@bmx-chemnitz.de>"
#define DRIVER_DESC "X-Box pad driver"

#define XPAD_PKT_LEN 32

static struct xpad_device {
	u16 idVendor;
	u16 idProduct;
	char *name;
} xpad_device[] = {
	{ 0x045e, 0x0202, "Microsoft X-Box pad (US)" },
	{ 0x045e, 0x0285, "Microsoft X-Box pad (Japan)" },
	{ 0x05fd, 0x107a, "InterAct 'PowerPad Pro' X-Box pad (Germany)" },
	{ 0x0000, 0x0000, "X-Box pad" }
};

static signed short xpad_btn[] = {
	BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z,	/* "analog" buttons */
	BTN_START, BTN_BACK, BTN_THUMBL, BTN_THUMBR,	/* start/back/sticks */
	-1						/* terminating entry */
};

static signed short xpad_abs[] = {
	ABS_X, ABS_Y,		/* left stick */
	ABS_RX, ABS_RY,		/* right stick */
	ABS_Z, ABS_RZ,		/* triggers left/right */
	ABS_HAT0X, ABS_HAT0Y,	/* digital pad */
	-1			/* terminating entry */
};

static struct usb_device_id xpad_table [] = {
	{ USB_INTERFACE_INFO('X', 'B', 0) },	/* X-Box USB-IF not approved class */
	{ }
};

MODULE_DEVICE_TABLE (usb, xpad_table);

struct usb_xpad {
	struct input_dev dev;			/* input device interface */
	struct usb_device *udev;		/* usb device */

	struct urb *irq_in;			/* urb for interrupt in report */
	unsigned char *idata;			/* input data */
	dma_addr_t idata_dma;

	char phys[65];				/* physical device path */
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

static void xpad_process_packet(struct usb_xpad *xpad, u16 cmd, unsigned char *data, struct pt_regs *regs)
{
	struct input_dev *dev = &xpad->dev;

	input_regs(dev, regs);

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
	input_report_abs(dev, ABS_HAT0X, !!(data[2] & 0x08) - !!(data[2] & 0x04));
	input_report_abs(dev, ABS_HAT0Y, !!(data[2] & 0x02) - !!(data[2] & 0x01));

	/* start/back buttons and stick press left/right */
	input_report_key(dev, BTN_START, (data[2] & 0x10) >> 4);
	input_report_key(dev, BTN_BACK, (data[2] & 0x20) >> 5);
	input_report_key(dev, BTN_THUMBL, (data[2] & 0x40) >> 6);
	input_report_key(dev, BTN_THUMBR, data[2] >> 7);

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

static void xpad_irq_in(struct urb *urb, struct pt_regs *regs)
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

	xpad_process_packet(xpad, 0, xpad->idata, regs);

exit:
	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		err ("%s - usb_submit_urb failed with result %d",
		     __FUNCTION__, retval);
}

static int xpad_open (struct input_dev *dev)
{
	struct usb_xpad *xpad = dev->private;

	xpad->irq_in->dev = xpad->udev;
	if (usb_submit_urb(xpad->irq_in, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void xpad_close (struct input_dev *dev)
{
	struct usb_xpad *xpad = dev->private;

	usb_kill_urb(xpad->irq_in);
}

static int xpad_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev (intf);
	struct usb_xpad *xpad = NULL;
	struct usb_endpoint_descriptor *ep_irq_in;
	char path[64];
	int i;

	for (i = 0; xpad_device[i].idVendor; i++) {
		if ((le16_to_cpu(udev->descriptor.idVendor) == xpad_device[i].idVendor) &&
		    (le16_to_cpu(udev->descriptor.idProduct) == xpad_device[i].idProduct))
			break;
	}

	if ((xpad = kmalloc (sizeof(struct usb_xpad), GFP_KERNEL)) == NULL) {
		err("cannot allocate memory for new pad");
		return -ENOMEM;
	}
	memset(xpad, 0, sizeof(struct usb_xpad));

	xpad->idata = usb_buffer_alloc(udev, XPAD_PKT_LEN,
				       SLAB_ATOMIC, &xpad->idata_dma);
	if (!xpad->idata) {
		kfree(xpad);
		return -ENOMEM;
	}

	xpad->irq_in = usb_alloc_urb(0, GFP_KERNEL);
        if (!xpad->irq_in) {
		err("cannot allocate memory for new pad irq urb");
		usb_buffer_free(udev, XPAD_PKT_LEN, xpad->idata, xpad->idata_dma);
                kfree(xpad);
		return -ENOMEM;
        }

	ep_irq_in = &intf->cur_altsetting->endpoint[0].desc;

	usb_fill_int_urb(xpad->irq_in, udev,
			 usb_rcvintpipe(udev, ep_irq_in->bEndpointAddress),
			 xpad->idata, XPAD_PKT_LEN, xpad_irq_in,
			 xpad, ep_irq_in->bInterval);
	xpad->irq_in->transfer_dma = xpad->idata_dma;
	xpad->irq_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	xpad->udev = udev;

	usb_to_input_id(udev, &xpad->dev.id);
	xpad->dev.dev = &intf->dev;
	xpad->dev.private = xpad;
	xpad->dev.name = xpad_device[i].name;
	xpad->dev.phys = xpad->phys;
	xpad->dev.open = xpad_open;
	xpad->dev.close = xpad_close;

	usb_make_path(udev, path, 64);
	snprintf(xpad->phys, 64,  "%s/input0", path);

	xpad->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

	for (i = 0; xpad_btn[i] >= 0; i++)
		set_bit(xpad_btn[i], xpad->dev.keybit);

	for (i = 0; xpad_abs[i] >= 0; i++) {

		signed short t = xpad_abs[i];

		set_bit(t, xpad->dev.absbit);

		switch (t) {
			case ABS_X:
			case ABS_Y:
			case ABS_RX:
			case ABS_RY:	/* the two sticks */
				xpad->dev.absmax[t] =  32767;
				xpad->dev.absmin[t] = -32768;
				xpad->dev.absflat[t] = 128;
				xpad->dev.absfuzz[t] = 16;
				break;
			case ABS_Z:
			case ABS_RZ:	/* the triggers */
				xpad->dev.absmax[t] = 255;
				xpad->dev.absmin[t] = 0;
				break;
			case ABS_HAT0X:
			case ABS_HAT0Y:	/* the d-pad */
				xpad->dev.absmax[t] =  1;
				xpad->dev.absmin[t] = -1;
				break;
		}
	}

	input_register_device(&xpad->dev);

	printk(KERN_INFO "input: %s on %s", xpad->dev.name, path);

	usb_set_intfdata(intf, xpad);
	return 0;
}

static void xpad_disconnect(struct usb_interface *intf)
{
	struct usb_xpad *xpad = usb_get_intfdata (intf);

	usb_set_intfdata(intf, NULL);
	if (xpad) {
		usb_kill_urb(xpad->irq_in);
		input_unregister_device(&xpad->dev);
		usb_free_urb(xpad->irq_in);
		usb_buffer_free(interface_to_usbdev(intf), XPAD_PKT_LEN, xpad->idata, xpad->idata_dma);
		kfree(xpad);
	}
}

static struct usb_driver xpad_driver = {
	.owner		= THIS_MODULE,
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
