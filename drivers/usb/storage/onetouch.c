// SPDX-License-Identifier: GPL-2.0+
/*
 * Support for the Maxtor OneTouch USB hard drive's button
 *
 * Current development and maintenance by:
 *	Copyright (c) 2005 Nick Sillik <n.sillik@temple.edu>
 *
 * Initial work by:
 *	Copyright (c) 2003 Erik Thyren <erth7411@student.uu.se>
 *
 * Based on usbmouse.c (Vojtech Pavlik) and xpad.c (Marko Friedemann)
 *
 */
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb/input.h>
#include "usb.h"
#include "debug.h"
#include "scsiglue.h"

#define DRV_NAME "ums-onetouch"

MODULE_DESCRIPTION("Maxtor USB OneTouch hard drive button driver");
MODULE_AUTHOR("Nick Sillik <n.sillik@temple.edu>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("USB_STORAGE");

#define ONETOUCH_PKT_LEN        0x02
#define ONETOUCH_BUTTON         KEY_PROG1

static int onetouch_connect_input(struct us_data *ss);
static void onetouch_release_input(void *onetouch_);

struct usb_onetouch {
	char name[128];
	char phys[64];
	struct input_dev *dev;	/* input device interface */
	struct usb_device *udev;	/* usb device */

	struct urb *irq;	/* urb for interrupt in report */
	unsigned char *data;	/* input data */
	dma_addr_t data_dma;
	unsigned int is_open:1;
};


/*
 * The table of devices
 */
#define UNUSUAL_DEV(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax, \
		    vendorName, productName, useProtocol, useTransport, \
		    initFunction, flags) \
{ USB_DEVICE_VER(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax), \
  .driver_info = (flags) }

static const struct usb_device_id onetouch_usb_ids[] = {
#	include "unusual_onetouch.h"
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, onetouch_usb_ids);

#undef UNUSUAL_DEV

/*
 * The flags table
 */
#define UNUSUAL_DEV(idVendor, idProduct, bcdDeviceMin, bcdDeviceMax, \
		    vendor_name, product_name, use_protocol, use_transport, \
		    init_function, Flags) \
{ \
	.vendorName = vendor_name,	\
	.productName = product_name,	\
	.useProtocol = use_protocol,	\
	.useTransport = use_transport,	\
	.initFunction = init_function,	\
}

static const struct us_unusual_dev onetouch_unusual_dev_list[] = {
#	include "unusual_onetouch.h"
	{ }		/* Terminating entry */
};

#undef UNUSUAL_DEV


static void usb_onetouch_irq(struct urb *urb)
{
	struct usb_onetouch *onetouch = urb->context;
	signed char *data = onetouch->data;
	struct input_dev *dev = onetouch->dev;
	int status = urb->status;
	int retval;

	switch (status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	/* -EPIPE:  should clear the halt */
	default:		/* error */
		goto resubmit;
	}

	input_report_key(dev, ONETOUCH_BUTTON, data[0] & 0x02);
	input_sync(dev);

resubmit:
	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		dev_err(&dev->dev, "can't resubmit intr, %s-%s/input0, "
			"retval %d\n", onetouch->udev->bus->bus_name,
			onetouch->udev->devpath, retval);
}

static int usb_onetouch_open(struct input_dev *dev)
{
	struct usb_onetouch *onetouch = input_get_drvdata(dev);

	onetouch->is_open = 1;
	onetouch->irq->dev = onetouch->udev;
	if (usb_submit_urb(onetouch->irq, GFP_KERNEL)) {
		dev_err(&dev->dev, "usb_submit_urb failed\n");
		return -EIO;
	}

	return 0;
}

static void usb_onetouch_close(struct input_dev *dev)
{
	struct usb_onetouch *onetouch = input_get_drvdata(dev);

	usb_kill_urb(onetouch->irq);
	onetouch->is_open = 0;
}

#ifdef CONFIG_PM
static void usb_onetouch_pm_hook(struct us_data *us, int action)
{
	struct usb_onetouch *onetouch = (struct usb_onetouch *) us->extra;

	if (onetouch->is_open) {
		switch (action) {
		case US_SUSPEND:
			usb_kill_urb(onetouch->irq);
			break;
		case US_RESUME:
			if (usb_submit_urb(onetouch->irq, GFP_NOIO) != 0)
				dev_err(&onetouch->irq->dev->dev,
					"usb_submit_urb failed\n");
			break;
		default:
			break;
		}
	}
}
#endif /* CONFIG_PM */

static int onetouch_connect_input(struct us_data *ss)
{
	struct usb_device *udev = ss->pusb_dev;
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_onetouch *onetouch;
	struct input_dev *input_dev;
	int pipe, maxp;
	int error = -ENOMEM;

	interface = ss->pusb_intf->cur_altsetting;

	if (interface->desc.bNumEndpoints != 3)
		return -ENODEV;

	endpoint = &interface->endpoint[2].desc;
	if (!usb_endpoint_is_int_in(endpoint))
		return -ENODEV;

	pipe = usb_rcvintpipe(udev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(udev, pipe);
	maxp = min(maxp, ONETOUCH_PKT_LEN);

	onetouch = kzalloc(sizeof(struct usb_onetouch), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!onetouch || !input_dev)
		goto fail1;

	onetouch->data = usb_alloc_coherent(udev, ONETOUCH_PKT_LEN,
					    GFP_KERNEL, &onetouch->data_dma);
	if (!onetouch->data)
		goto fail1;

	onetouch->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!onetouch->irq)
		goto fail2;

	onetouch->udev = udev;
	onetouch->dev = input_dev;

	if (udev->manufacturer)
		strscpy(onetouch->name, udev->manufacturer,
			sizeof(onetouch->name));
	if (udev->product) {
		if (udev->manufacturer)
			strlcat(onetouch->name, " ", sizeof(onetouch->name));
		strlcat(onetouch->name, udev->product, sizeof(onetouch->name));
	}

	if (!strlen(onetouch->name))
		snprintf(onetouch->name, sizeof(onetouch->name),
			 "Maxtor Onetouch %04x:%04x",
			 le16_to_cpu(udev->descriptor.idVendor),
			 le16_to_cpu(udev->descriptor.idProduct));

	usb_make_path(udev, onetouch->phys, sizeof(onetouch->phys));
	strlcat(onetouch->phys, "/input0", sizeof(onetouch->phys));

	input_dev->name = onetouch->name;
	input_dev->phys = onetouch->phys;
	usb_to_input_id(udev, &input_dev->id);
	input_dev->dev.parent = &udev->dev;

	set_bit(EV_KEY, input_dev->evbit);
	set_bit(ONETOUCH_BUTTON, input_dev->keybit);
	clear_bit(0, input_dev->keybit);

	input_set_drvdata(input_dev, onetouch);

	input_dev->open = usb_onetouch_open;
	input_dev->close = usb_onetouch_close;

	usb_fill_int_urb(onetouch->irq, udev, pipe, onetouch->data, maxp,
			 usb_onetouch_irq, onetouch, endpoint->bInterval);
	onetouch->irq->transfer_dma = onetouch->data_dma;
	onetouch->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	ss->extra_destructor = onetouch_release_input;
	ss->extra = onetouch;
#ifdef CONFIG_PM
	ss->suspend_resume_hook = usb_onetouch_pm_hook;
#endif

	error = input_register_device(onetouch->dev);
	if (error)
		goto fail3;

	return 0;

 fail3:	usb_free_urb(onetouch->irq);
 fail2:	usb_free_coherent(udev, ONETOUCH_PKT_LEN,
			  onetouch->data, onetouch->data_dma);
 fail1:	kfree(onetouch);
	input_free_device(input_dev);
	return error;
}

static void onetouch_release_input(void *onetouch_)
{
	struct usb_onetouch *onetouch = (struct usb_onetouch *) onetouch_;

	if (onetouch) {
		usb_kill_urb(onetouch->irq);
		input_unregister_device(onetouch->dev);
		usb_free_urb(onetouch->irq);
		usb_free_coherent(onetouch->udev, ONETOUCH_PKT_LEN,
				  onetouch->data, onetouch->data_dma);
	}
}

static struct scsi_host_template onetouch_host_template;

static int onetouch_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct us_data *us;
	int result;

	result = usb_stor_probe1(&us, intf, id,
			(id - onetouch_usb_ids) + onetouch_unusual_dev_list,
			&onetouch_host_template);
	if (result)
		return result;

	/* Use default transport and protocol */

	result = usb_stor_probe2(us);
	return result;
}

static struct usb_driver onetouch_driver = {
	.name =		DRV_NAME,
	.probe =	onetouch_probe,
	.disconnect =	usb_stor_disconnect,
	.suspend =	usb_stor_suspend,
	.resume =	usb_stor_resume,
	.reset_resume =	usb_stor_reset_resume,
	.pre_reset =	usb_stor_pre_reset,
	.post_reset =	usb_stor_post_reset,
	.id_table =	onetouch_usb_ids,
	.soft_unbind =	1,
	.no_dynamic_id = 1,
};

module_usb_stor_driver(onetouch_driver, onetouch_host_template, DRV_NAME);
