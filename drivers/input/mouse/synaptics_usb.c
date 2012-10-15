/*
 * USB Synaptics device driver
 *
 *  Copyright (c) 2002 Rob Miller (rob@inpharmatica . co . uk)
 *  Copyright (c) 2003 Ron Lee (ron@debian.org)
 *	cPad driver for kernel 2.4
 *
 *  Copyright (c) 2004 Jan Steinhoff (cpad@jan-steinhoff . de)
 *  Copyright (c) 2004 Ron Lee (ron@debian.org)
 *	rewritten for kernel 2.6
 *
 *  cPad display character device part is not included. It can be found at
 *  http://jan-steinhoff.de/linux/synaptics-usb.html
 *
 * Bases on:	usb_skeleton.c v2.2 by Greg Kroah-Hartman
 *		drivers/hid/usbhid/usbmouse.c by Vojtech Pavlik
 *		drivers/input/mouse/synaptics.c by Peter Osterlund
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Trademarks are the property of their respective owners.
 */

/*
 * There are three different types of Synaptics USB devices: Touchpads,
 * touchsticks (or trackpoints), and touchscreens. Touchpads are well supported
 * by this driver, touchstick support has not been tested much yet, and
 * touchscreens have not been tested at all.
 *
 * Up to three alternate settings are possible:
 *	setting 0: one int endpoint for relative movement (used by usbhid.ko)
 *	setting 1: one int endpoint for absolute finger position
 *	setting 2 (cPad only): one int endpoint for absolute finger position and
 *		   two bulk endpoints for the display (in/out)
 * This driver uses setting 1.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/usb.h>
#include <linux/input.h>
#include <linux/usb/input.h>

#define USB_VENDOR_ID_SYNAPTICS	0x06cb
#define USB_DEVICE_ID_SYNAPTICS_TP	0x0001	/* Synaptics USB TouchPad */
#define USB_DEVICE_ID_SYNAPTICS_INT_TP	0x0002	/* Integrated USB TouchPad */
#define USB_DEVICE_ID_SYNAPTICS_CPAD	0x0003	/* Synaptics cPad */
#define USB_DEVICE_ID_SYNAPTICS_TS	0x0006	/* Synaptics TouchScreen */
#define USB_DEVICE_ID_SYNAPTICS_STICK	0x0007	/* Synaptics USB Styk */
#define USB_DEVICE_ID_SYNAPTICS_WP	0x0008	/* Synaptics USB WheelPad */
#define USB_DEVICE_ID_SYNAPTICS_COMP_TP	0x0009	/* Composite USB TouchPad */
#define USB_DEVICE_ID_SYNAPTICS_WTP	0x0010	/* Wireless TouchPad */
#define USB_DEVICE_ID_SYNAPTICS_DPAD	0x0013	/* DisplayPad */

#define SYNUSB_TOUCHPAD			(1 << 0)
#define SYNUSB_STICK			(1 << 1)
#define SYNUSB_TOUCHSCREEN		(1 << 2)
#define SYNUSB_AUXDISPLAY		(1 << 3) /* For cPad */
#define SYNUSB_COMBO			(1 << 4) /* Composite device (TP + stick) */
#define SYNUSB_IO_ALWAYS		(1 << 5)

#define USB_DEVICE_SYNAPTICS(prod, kind)		\
	USB_DEVICE(USB_VENDOR_ID_SYNAPTICS,		\
		   USB_DEVICE_ID_SYNAPTICS_##prod),	\
	.driver_info = (kind),

#define SYNUSB_RECV_SIZE	8

#define XMIN_NOMINAL		1472
#define XMAX_NOMINAL		5472
#define YMIN_NOMINAL		1408
#define YMAX_NOMINAL		4448

struct synusb {
	struct usb_device *udev;
	struct usb_interface *intf;
	struct urb *urb;
	unsigned char *data;

	/* input device related data structures */
	struct input_dev *input;
	char name[128];
	char phys[64];

	/* characteristics of the device */
	unsigned long flags;
};

static void synusb_report_buttons(struct synusb *synusb)
{
	struct input_dev *input_dev = synusb->input;

	input_report_key(input_dev, BTN_LEFT, synusb->data[1] & 0x04);
	input_report_key(input_dev, BTN_RIGHT, synusb->data[1] & 0x01);
	input_report_key(input_dev, BTN_MIDDLE, synusb->data[1] & 0x02);
}

static void synusb_report_stick(struct synusb *synusb)
{
	struct input_dev *input_dev = synusb->input;
	int x, y;
	unsigned int pressure;

	pressure = synusb->data[6];
	x = (s16)(be16_to_cpup((__be16 *)&synusb->data[2]) << 3) >> 7;
	y = (s16)(be16_to_cpup((__be16 *)&synusb->data[4]) << 3) >> 7;

	if (pressure > 0) {
		input_report_rel(input_dev, REL_X, x);
		input_report_rel(input_dev, REL_Y, -y);
	}

	input_report_abs(input_dev, ABS_PRESSURE, pressure);

	synusb_report_buttons(synusb);

	input_sync(input_dev);
}

static void synusb_report_touchpad(struct synusb *synusb)
{
	struct input_dev *input_dev = synusb->input;
	unsigned int num_fingers, tool_width;
	unsigned int x, y;
	unsigned int pressure, w;

	pressure = synusb->data[6];
	x = be16_to_cpup((__be16 *)&synusb->data[2]);
	y = be16_to_cpup((__be16 *)&synusb->data[4]);
	w = synusb->data[0] & 0x0f;

	if (pressure > 0) {
		num_fingers = 1;
		tool_width = 5;
		switch (w) {
		case 0 ... 1:
			num_fingers = 2 + w;
			break;

		case 2:	                /* pen, pretend its a finger */
			break;

		case 4 ... 15:
			tool_width = w;
			break;
		}
	} else {
		num_fingers = 0;
		tool_width = 0;
	}

	/*
	 * Post events
	 * BTN_TOUCH has to be first as mousedev relies on it when doing
	 * absolute -> relative conversion
	 */

	if (pressure > 30)
		input_report_key(input_dev, BTN_TOUCH, 1);
	if (pressure < 25)
		input_report_key(input_dev, BTN_TOUCH, 0);

	if (num_fingers > 0) {
		input_report_abs(input_dev, ABS_X, x);
		input_report_abs(input_dev, ABS_Y,
				 YMAX_NOMINAL + YMIN_NOMINAL - y);
	}

	input_report_abs(input_dev, ABS_PRESSURE, pressure);
	input_report_abs(input_dev, ABS_TOOL_WIDTH, tool_width);

	input_report_key(input_dev, BTN_TOOL_FINGER, num_fingers == 1);
	input_report_key(input_dev, BTN_TOOL_DOUBLETAP, num_fingers == 2);
	input_report_key(input_dev, BTN_TOOL_TRIPLETAP, num_fingers == 3);

	synusb_report_buttons(synusb);
	if (synusb->flags & SYNUSB_AUXDISPLAY)
		input_report_key(input_dev, BTN_MIDDLE, synusb->data[1] & 0x08);

	input_sync(input_dev);
}

static void synusb_irq(struct urb *urb)
{
	struct synusb *synusb = urb->context;
	int error;

	/* Check our status in case we need to bail out early. */
	switch (urb->status) {
	case 0:
		usb_mark_last_busy(synusb->udev);
		break;

	/* Device went away so don't keep trying to read from it. */
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		return;

	default:
		goto resubmit;
		break;
	}

	if (synusb->flags & SYNUSB_STICK)
		synusb_report_stick(synusb);
	else
		synusb_report_touchpad(synusb);

resubmit:
	error = usb_submit_urb(urb, GFP_ATOMIC);
	if (error && error != -EPERM)
		dev_err(&synusb->intf->dev,
			"%s - usb_submit_urb failed with result: %d",
			__func__, error);
}

static struct usb_endpoint_descriptor *
synusb_get_in_endpoint(struct usb_host_interface *iface)
{

	struct usb_endpoint_descriptor *endpoint;
	int i;

	for (i = 0; i < iface->desc.bNumEndpoints; ++i) {
		endpoint = &iface->endpoint[i].desc;

		if (usb_endpoint_is_int_in(endpoint)) {
			/* we found our interrupt in endpoint */
			return endpoint;
		}
	}

	return NULL;
}

static int synusb_open(struct input_dev *dev)
{
	struct synusb *synusb = input_get_drvdata(dev);
	int retval;

	retval = usb_autopm_get_interface(synusb->intf);
	if (retval) {
		dev_err(&synusb->intf->dev,
			"%s - usb_autopm_get_interface failed, error: %d\n",
			__func__, retval);
		return retval;
	}

	retval = usb_submit_urb(synusb->urb, GFP_KERNEL);
	if (retval) {
		dev_err(&synusb->intf->dev,
			"%s - usb_submit_urb failed, error: %d\n",
			__func__, retval);
		retval = -EIO;
		goto out;
	}

	synusb->intf->needs_remote_wakeup = 1;

out:
	usb_autopm_put_interface(synusb->intf);
	return retval;
}

static void synusb_close(struct input_dev *dev)
{
	struct synusb *synusb = input_get_drvdata(dev);
	int autopm_error;

	autopm_error = usb_autopm_get_interface(synusb->intf);

	usb_kill_urb(synusb->urb);
	synusb->intf->needs_remote_wakeup = 0;

	if (!autopm_error)
		usb_autopm_put_interface(synusb->intf);
}

static int synusb_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *ep;
	struct synusb *synusb;
	struct input_dev *input_dev;
	unsigned int intf_num = intf->cur_altsetting->desc.bInterfaceNumber;
	unsigned int altsetting = min(intf->num_altsetting, 1U);
	int error;

	error = usb_set_interface(udev, intf_num, altsetting);
	if (error) {
		dev_err(&udev->dev,
			"Can not set alternate setting to %i, error: %i",
			altsetting, error);
		return error;
	}

	ep = synusb_get_in_endpoint(intf->cur_altsetting);
	if (!ep)
		return -ENODEV;

	synusb = kzalloc(sizeof(*synusb), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!synusb || !input_dev) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	synusb->udev = udev;
	synusb->intf = intf;
	synusb->input = input_dev;

	synusb->flags = id->driver_info;
	if (synusb->flags & SYNUSB_COMBO) {
		/*
		 * This is a combo device, we need to set proper
		 * capability, depending on the interface.
		 */
		synusb->flags |= intf_num == 1 ?
					SYNUSB_STICK : SYNUSB_TOUCHPAD;
	}

	synusb->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!synusb->urb) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	synusb->data = usb_alloc_coherent(udev, SYNUSB_RECV_SIZE, GFP_KERNEL,
					  &synusb->urb->transfer_dma);
	if (!synusb->data) {
		error = -ENOMEM;
		goto err_free_urb;
	}

	usb_fill_int_urb(synusb->urb, udev,
			 usb_rcvintpipe(udev, ep->bEndpointAddress),
			 synusb->data, SYNUSB_RECV_SIZE,
			 synusb_irq, synusb,
			 ep->bInterval);
	synusb->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	if (udev->manufacturer)
		strlcpy(synusb->name, udev->manufacturer,
			sizeof(synusb->name));

	if (udev->product) {
		if (udev->manufacturer)
			strlcat(synusb->name, " ", sizeof(synusb->name));
		strlcat(synusb->name, udev->product, sizeof(synusb->name));
	}

	if (!strlen(synusb->name))
		snprintf(synusb->name, sizeof(synusb->name),
			 "USB Synaptics Device %04x:%04x",
			 le16_to_cpu(udev->descriptor.idVendor),
			 le16_to_cpu(udev->descriptor.idProduct));

	if (synusb->flags & SYNUSB_STICK)
		strlcat(synusb->name, " (Stick)", sizeof(synusb->name));

	usb_make_path(udev, synusb->phys, sizeof(synusb->phys));
	strlcat(synusb->phys, "/input0", sizeof(synusb->phys));

	input_dev->name = synusb->name;
	input_dev->phys = synusb->phys;
	usb_to_input_id(udev, &input_dev->id);
	input_dev->dev.parent = &synusb->intf->dev;

	if (!(synusb->flags & SYNUSB_IO_ALWAYS)) {
		input_dev->open = synusb_open;
		input_dev->close = synusb_close;
	}

	input_set_drvdata(input_dev, synusb);

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);

	if (synusb->flags & SYNUSB_STICK) {
		__set_bit(EV_REL, input_dev->evbit);
		__set_bit(REL_X, input_dev->relbit);
		__set_bit(REL_Y, input_dev->relbit);
		input_set_abs_params(input_dev, ABS_PRESSURE, 0, 127, 0, 0);
	} else {
		input_set_abs_params(input_dev, ABS_X,
				     XMIN_NOMINAL, XMAX_NOMINAL, 0, 0);
		input_set_abs_params(input_dev, ABS_Y,
				     YMIN_NOMINAL, YMAX_NOMINAL, 0, 0);
		input_set_abs_params(input_dev, ABS_PRESSURE, 0, 255, 0, 0);
		input_set_abs_params(input_dev, ABS_TOOL_WIDTH, 0, 15, 0, 0);
		__set_bit(BTN_TOUCH, input_dev->keybit);
		__set_bit(BTN_TOOL_FINGER, input_dev->keybit);
		__set_bit(BTN_TOOL_DOUBLETAP, input_dev->keybit);
		__set_bit(BTN_TOOL_TRIPLETAP, input_dev->keybit);
	}

	__set_bit(BTN_LEFT, input_dev->keybit);
	__set_bit(BTN_RIGHT, input_dev->keybit);
	__set_bit(BTN_MIDDLE, input_dev->keybit);

	usb_set_intfdata(intf, synusb);

	if (synusb->flags & SYNUSB_IO_ALWAYS) {
		error = synusb_open(input_dev);
		if (error)
			goto err_free_dma;
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(&udev->dev,
			"Failed to register input device, error %d\n",
			error);
		goto err_stop_io;
	}

	return 0;

err_stop_io:
	if (synusb->flags & SYNUSB_IO_ALWAYS)
		synusb_close(synusb->input);
err_free_dma:
	usb_free_coherent(udev, SYNUSB_RECV_SIZE, synusb->data,
			  synusb->urb->transfer_dma);
err_free_urb:
	usb_free_urb(synusb->urb);
err_free_mem:
	input_free_device(input_dev);
	kfree(synusb);
	usb_set_intfdata(intf, NULL);

	return error;
}

static void synusb_disconnect(struct usb_interface *intf)
{
	struct synusb *synusb = usb_get_intfdata(intf);
	struct usb_device *udev = interface_to_usbdev(intf);

	if (synusb->flags & SYNUSB_IO_ALWAYS)
		synusb_close(synusb->input);

	input_unregister_device(synusb->input);

	usb_free_coherent(udev, SYNUSB_RECV_SIZE, synusb->data,
			  synusb->urb->transfer_dma);
	usb_free_urb(synusb->urb);
	kfree(synusb);

	usb_set_intfdata(intf, NULL);
}

static int synusb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct synusb *synusb = usb_get_intfdata(intf);
	struct input_dev *input_dev = synusb->input;

	mutex_lock(&input_dev->mutex);
	usb_kill_urb(synusb->urb);
	mutex_unlock(&input_dev->mutex);

	return 0;
}

static int synusb_resume(struct usb_interface *intf)
{
	struct synusb *synusb = usb_get_intfdata(intf);
	struct input_dev *input_dev = synusb->input;
	int retval = 0;

	mutex_lock(&input_dev->mutex);

	if ((input_dev->users || (synusb->flags & SYNUSB_IO_ALWAYS)) &&
	    usb_submit_urb(synusb->urb, GFP_NOIO) < 0) {
		retval = -EIO;
	}

	mutex_unlock(&input_dev->mutex);

	return retval;
}

static int synusb_pre_reset(struct usb_interface *intf)
{
	struct synusb *synusb = usb_get_intfdata(intf);
	struct input_dev *input_dev = synusb->input;

	mutex_lock(&input_dev->mutex);
	usb_kill_urb(synusb->urb);

	return 0;
}

static int synusb_post_reset(struct usb_interface *intf)
{
	struct synusb *synusb = usb_get_intfdata(intf);
	struct input_dev *input_dev = synusb->input;
	int retval = 0;

	if ((input_dev->users || (synusb->flags & SYNUSB_IO_ALWAYS)) &&
	    usb_submit_urb(synusb->urb, GFP_NOIO) < 0) {
		retval = -EIO;
	}

	mutex_unlock(&input_dev->mutex);

	return retval;
}

static int synusb_reset_resume(struct usb_interface *intf)
{
	return synusb_resume(intf);
}

static struct usb_device_id synusb_idtable[] = {
	{ USB_DEVICE_SYNAPTICS(TP, SYNUSB_TOUCHPAD) },
	{ USB_DEVICE_SYNAPTICS(INT_TP, SYNUSB_TOUCHPAD) },
	{ USB_DEVICE_SYNAPTICS(CPAD,
		SYNUSB_TOUCHPAD | SYNUSB_AUXDISPLAY | SYNUSB_IO_ALWAYS) },
	{ USB_DEVICE_SYNAPTICS(TS, SYNUSB_TOUCHSCREEN) },
	{ USB_DEVICE_SYNAPTICS(STICK, SYNUSB_STICK) },
	{ USB_DEVICE_SYNAPTICS(WP, SYNUSB_TOUCHPAD) },
	{ USB_DEVICE_SYNAPTICS(COMP_TP, SYNUSB_COMBO) },
	{ USB_DEVICE_SYNAPTICS(WTP, SYNUSB_TOUCHPAD) },
	{ USB_DEVICE_SYNAPTICS(DPAD, SYNUSB_TOUCHPAD) },
	{ }
};
MODULE_DEVICE_TABLE(usb, synusb_idtable);

static struct usb_driver synusb_driver = {
	.name		= "synaptics_usb",
	.probe		= synusb_probe,
	.disconnect	= synusb_disconnect,
	.id_table	= synusb_idtable,
	.suspend	= synusb_suspend,
	.resume		= synusb_resume,
	.pre_reset	= synusb_pre_reset,
	.post_reset	= synusb_post_reset,
	.reset_resume	= synusb_reset_resume,
	.supports_autosuspend = 1,
};

module_usb_driver(synusb_driver);

MODULE_AUTHOR("Rob Miller <rob@inpharmatica.co.uk>, "
              "Ron Lee <ron@debian.org>, "
              "Jan Steinhoff <cpad@jan-steinhoff.de>");
MODULE_DESCRIPTION("Synaptics USB device driver");
MODULE_LICENSE("GPL");
