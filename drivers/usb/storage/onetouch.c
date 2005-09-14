/*
 * Support for the Maxtor OneTouch USB hard drive's button
 *
 * Current development and maintenance by:
 *	Copyright (c) 2005 Nick Sillik <n.sillik@temple.edu>
 *
 * Initial work by:
 * 	Copyright (c) 2003 Erik Thyren <erth7411@student.uu.se>
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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb_ch9.h>
#include <linux/usb_input.h>
#include "usb.h"
#include "onetouch.h"
#include "debug.h"

void onetouch_release_input(void *onetouch_);

struct usb_onetouch {
	char name[128];
	char phys[64];
	struct input_dev dev;	/* input device interface */
	struct usb_device *udev;	/* usb device */

	struct urb *irq;	/* urb for interrupt in report */
	unsigned char *data;	/* input data */
	dma_addr_t data_dma;
};

static void usb_onetouch_irq(struct urb *urb, struct pt_regs *regs)
{
	struct usb_onetouch *onetouch = urb->context;
	signed char *data = onetouch->data;
	struct input_dev *dev = &onetouch->dev;
	int status;

	switch (urb->status) {
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

	input_regs(dev, regs);

	input_report_key(&onetouch->dev, ONETOUCH_BUTTON,
			 data[0] & 0x02);

	input_sync(dev);
resubmit:
	status = usb_submit_urb (urb, SLAB_ATOMIC);
	if (status)
		err ("can't resubmit intr, %s-%s/input0, status %d",
			onetouch->udev->bus->bus_name,
			onetouch->udev->devpath, status);
}

static int usb_onetouch_open(struct input_dev *dev)
{
	struct usb_onetouch *onetouch = dev->private;

	onetouch->irq->dev = onetouch->udev;
	if (usb_submit_urb(onetouch->irq, GFP_KERNEL)) {
		err("usb_submit_urb failed");
		return -EIO;
	}

	return 0;
}

static void usb_onetouch_close(struct input_dev *dev)
{
	struct usb_onetouch *onetouch = dev->private;

	usb_kill_urb(onetouch->irq);
}

int onetouch_connect_input(struct us_data *ss)
{
	struct usb_device *udev = ss->pusb_dev;
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_onetouch *onetouch;
	int pipe, maxp;
	char path[64];

	interface = ss->pusb_intf->cur_altsetting;

	if (interface->desc.bNumEndpoints != 3)
		return -ENODEV;

	endpoint = &interface->endpoint[2].desc;
	if(!(endpoint->bEndpointAddress & USB_DIR_IN))
		return -ENODEV;
	if((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
			!= USB_ENDPOINT_XFER_INT)
		return -ENODEV;

	pipe = usb_rcvintpipe(udev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));

	if (!(onetouch = kcalloc(1, sizeof(struct usb_onetouch), GFP_KERNEL)))
		return -ENOMEM;

	onetouch->data = usb_buffer_alloc(udev, ONETOUCH_PKT_LEN,
					  SLAB_ATOMIC, &onetouch->data_dma);
	if (!onetouch->data){
		kfree(onetouch);
		return -ENOMEM;
	}

	onetouch->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!onetouch->irq){
		kfree(onetouch);
		usb_buffer_free(udev, ONETOUCH_PKT_LEN,
				onetouch->data, onetouch->data_dma);
		return -ENODEV;
	}


	onetouch->udev = udev;

	set_bit(EV_KEY, onetouch->dev.evbit);
	set_bit(ONETOUCH_BUTTON, onetouch->dev.keybit);
	clear_bit(0, onetouch->dev.keybit);

	onetouch->dev.private = onetouch;
	onetouch->dev.open = usb_onetouch_open;
	onetouch->dev.close = usb_onetouch_close;

	usb_make_path(udev, path, sizeof(path));
	sprintf(onetouch->phys, "%s/input0", path);

	onetouch->dev.name = onetouch->name;
	onetouch->dev.phys = onetouch->phys;

	usb_to_input_id(udev, &onetouch->dev.id);

	onetouch->dev.dev = &udev->dev;

	if (udev->manufacturer)
		strcat(onetouch->name, udev->manufacturer);
	if (udev->product)
		sprintf(onetouch->name, "%s %s", onetouch->name,
			udev->product);
	if (!strlen(onetouch->name))
		sprintf(onetouch->name, "Maxtor Onetouch %04x:%04x",
			onetouch->dev.id.vendor, onetouch->dev.id.product);

	usb_fill_int_urb(onetouch->irq, udev, pipe, onetouch->data,
			 (maxp > 8 ? 8 : maxp),
			 usb_onetouch_irq, onetouch, endpoint->bInterval);
	onetouch->irq->transfer_dma = onetouch->data_dma;
	onetouch->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	ss->extra_destructor = onetouch_release_input;
	ss->extra = onetouch;

	input_register_device(&onetouch->dev);
	printk(KERN_INFO "usb-input: %s on %s\n", onetouch->dev.name, path);

	return 0;
}

void onetouch_release_input(void *onetouch_)
{
	struct usb_onetouch *onetouch = (struct usb_onetouch *) onetouch_;

	if (onetouch) {
		usb_kill_urb(onetouch->irq);
		input_unregister_device(&onetouch->dev);
		usb_free_urb(onetouch->irq);
		usb_buffer_free(onetouch->udev, ONETOUCH_PKT_LEN,
				onetouch->data, onetouch->data_dma);
		printk(KERN_INFO "usb-input: deregistering %s\n",
				onetouch->dev.name);
	}
}
