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
#include <linux/input.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb/input.h>
#include "usb.h"
#include "onetouch.h"
#include "debug.h"

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
			if (usb_submit_urb(onetouch->irq, GFP_KERNEL) != 0)
				dev_err(&onetouch->irq->dev->dev,
					"usb_submit_urb failed\n");
			break;
		default:
			break;
		}
	}
}
#endif /* CONFIG_PM */

int onetouch_connect_input(struct us_data *ss)
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
	maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));

	onetouch = kzalloc(sizeof(struct usb_onetouch), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!onetouch || !input_dev)
		goto fail1;

	onetouch->data = usb_buffer_alloc(udev, ONETOUCH_PKT_LEN,
					  GFP_ATOMIC, &onetouch->data_dma);
	if (!onetouch->data)
		goto fail1;

	onetouch->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!onetouch->irq)
		goto fail2;

	onetouch->udev = udev;
	onetouch->dev = input_dev;

	if (udev->manufacturer)
		strlcpy(onetouch->name, udev->manufacturer,
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

	usb_fill_int_urb(onetouch->irq, udev, pipe, onetouch->data,
			 (maxp > 8 ? 8 : maxp),
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
 fail2:	usb_buffer_free(udev, ONETOUCH_PKT_LEN,
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
		usb_buffer_free(onetouch->udev, ONETOUCH_PKT_LEN,
				onetouch->data, onetouch->data_dma);
	}
}
