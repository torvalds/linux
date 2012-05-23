/*
 *  Copyright (c) 2001-2005 Edouard TISSERANT   <edouard.tisserant@wanadoo.fr>
 *  Copyright (c) 2004-2005 Stephane VOLTZ      <svoltz@numericable.fr>
 *
 *  USB Acecad "Acecad Flair" tablet support
 *
 *  Changelog:
 *      v3.2 - Added sysfs support
 */

/*
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
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v3.2"
#define DRIVER_DESC    "USB Acecad Flair tablet driver"
#define DRIVER_LICENSE "GPL"
#define DRIVER_AUTHOR  "Edouard TISSERANT <edouard.tisserant@wanadoo.fr>"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

#define USB_VENDOR_ID_ACECAD	0x0460
#define USB_DEVICE_ID_FLAIR	0x0004
#define USB_DEVICE_ID_302	0x0008

struct usb_acecad {
	char name[128];
	char phys[64];
	struct usb_device *usbdev;
	struct usb_interface *intf;
	struct input_dev *input;
	struct urb *irq;

	unsigned char *data;
	dma_addr_t data_dma;
};

static void usb_acecad_irq(struct urb *urb)
{
	struct usb_acecad *acecad = urb->context;
	unsigned char *data = acecad->data;
	struct input_dev *dev = acecad->input;
	struct usb_interface *intf = acecad->intf;
	int prox, status;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&intf->dev, "%s - urb shutting down with status: %d\n",
			__func__, urb->status);
		return;
	default:
		dev_dbg(&intf->dev, "%s - nonzero urb status received: %d\n",
			__func__, urb->status);
		goto resubmit;
	}

	prox = (data[0] & 0x04) >> 2;
	input_report_key(dev, BTN_TOOL_PEN, prox);

	if (prox) {
		int x = data[1] | (data[2] << 8);
		int y = data[3] | (data[4] << 8);
		/* Pressure should compute the same way for flair and 302 */
		int pressure = data[5] | (data[6] << 8);
		int touch = data[0] & 0x01;
		int stylus = (data[0] & 0x10) >> 4;
		int stylus2 = (data[0] & 0x20) >> 5;
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
		input_report_abs(dev, ABS_PRESSURE, pressure);
		input_report_key(dev, BTN_TOUCH, touch);
		input_report_key(dev, BTN_STYLUS, stylus);
		input_report_key(dev, BTN_STYLUS2, stylus2);
	}

	/* event termination */
	input_sync(dev);

resubmit:
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status)
		dev_err(&intf->dev,
			"can't resubmit intr, %s-%s/input0, status %d\n",
			acecad->usbdev->bus->bus_name,
			acecad->usbdev->devpath, status);
}

static int usb_acecad_open(struct input_dev *dev)
{
	struct usb_acecad *acecad = input_get_drvdata(dev);

	acecad->irq->dev = acecad->usbdev;
	if (usb_submit_urb(acecad->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void usb_acecad_close(struct input_dev *dev)
{
	struct usb_acecad *acecad = input_get_drvdata(dev);

	usb_kill_urb(acecad->irq);
}

static int usb_acecad_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface = intf->cur_altsetting;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_acecad *acecad;
	struct input_dev *input_dev;
	int pipe, maxp;
	int err;

	if (interface->desc.bNumEndpoints != 1)
		return -ENODEV;

	endpoint = &interface->endpoint[0].desc;

	if (!usb_endpoint_is_int_in(endpoint))
		return -ENODEV;

	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	acecad = kzalloc(sizeof(struct usb_acecad), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!acecad || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	acecad->data = usb_alloc_coherent(dev, 8, GFP_KERNEL, &acecad->data_dma);
	if (!acecad->data) {
		err= -ENOMEM;
		goto fail1;
	}

	acecad->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!acecad->irq) {
		err = -ENOMEM;
		goto fail2;
	}

	acecad->usbdev = dev;
	acecad->intf = intf;
	acecad->input = input_dev;

	if (dev->manufacturer)
		strlcpy(acecad->name, dev->manufacturer, sizeof(acecad->name));

	if (dev->product) {
		if (dev->manufacturer)
			strlcat(acecad->name, " ", sizeof(acecad->name));
		strlcat(acecad->name, dev->product, sizeof(acecad->name));
	}

	usb_make_path(dev, acecad->phys, sizeof(acecad->phys));
	strlcat(acecad->phys, "/input0", sizeof(acecad->phys));

	input_dev->name = acecad->name;
	input_dev->phys = acecad->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, acecad);

	input_dev->open = usb_acecad_open;
	input_dev->close = usb_acecad_close;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_DIGI)] = BIT_MASK(BTN_TOOL_PEN) |
		BIT_MASK(BTN_TOUCH) | BIT_MASK(BTN_STYLUS) |
		BIT_MASK(BTN_STYLUS2);

	switch (id->driver_info) {
	case 0:
		input_set_abs_params(input_dev, ABS_X, 0, 5000, 4, 0);
		input_set_abs_params(input_dev, ABS_Y, 0, 3750, 4, 0);
		input_set_abs_params(input_dev, ABS_PRESSURE, 0, 512, 0, 0);
		if (!strlen(acecad->name))
			snprintf(acecad->name, sizeof(acecad->name),
				"USB Acecad Flair Tablet %04x:%04x",
				le16_to_cpu(dev->descriptor.idVendor),
				le16_to_cpu(dev->descriptor.idProduct));
		break;

	case 1:
		input_set_abs_params(input_dev, ABS_X, 0, 53000, 4, 0);
		input_set_abs_params(input_dev, ABS_Y, 0, 2250, 4, 0);
		input_set_abs_params(input_dev, ABS_PRESSURE, 0, 1024, 0, 0);
		if (!strlen(acecad->name))
			snprintf(acecad->name, sizeof(acecad->name),
				"USB Acecad 302 Tablet %04x:%04x",
				le16_to_cpu(dev->descriptor.idVendor),
				le16_to_cpu(dev->descriptor.idProduct));
		break;
	}

	usb_fill_int_urb(acecad->irq, dev, pipe,
			acecad->data, maxp > 8 ? 8 : maxp,
			usb_acecad_irq, acecad, endpoint->bInterval);
	acecad->irq->transfer_dma = acecad->data_dma;
	acecad->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	err = input_register_device(acecad->input);
	if (err)
		goto fail3;

	usb_set_intfdata(intf, acecad);

	return 0;

 fail3:	usb_free_urb(acecad->irq);
 fail2:	usb_free_coherent(dev, 8, acecad->data, acecad->data_dma);
 fail1: input_free_device(input_dev);
	kfree(acecad);
	return err;
}

static void usb_acecad_disconnect(struct usb_interface *intf)
{
	struct usb_acecad *acecad = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	input_unregister_device(acecad->input);
	usb_free_urb(acecad->irq);
	usb_free_coherent(acecad->usbdev, 8, acecad->data, acecad->data_dma);
	kfree(acecad);
}

static struct usb_device_id usb_acecad_id_table [] = {
	{ USB_DEVICE(USB_VENDOR_ID_ACECAD, USB_DEVICE_ID_FLAIR), .driver_info = 0 },
	{ USB_DEVICE(USB_VENDOR_ID_ACECAD, USB_DEVICE_ID_302),	 .driver_info = 1 },
	{ }
};

MODULE_DEVICE_TABLE(usb, usb_acecad_id_table);

static struct usb_driver usb_acecad_driver = {
	.name =		"usb_acecad",
	.probe =	usb_acecad_probe,
	.disconnect =	usb_acecad_disconnect,
	.id_table =	usb_acecad_id_table,
};

module_usb_driver(usb_acecad_driver);
