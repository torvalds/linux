/*
 * Greybus "AP" USB driver
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/usb.h>
#include "greybus.h"

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0xffff, 0x0001) },		// FIXME
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

struct es1_ap_dev {
	struct usb_device *usb_dev;
	struct usb_interface *usb_intf;
	struct greybus_host_device *hd;

	__u8 ap_comm_endpoint;		/* endpoint to talk to the AP */
	__u8 ap_in_endpoint;		/* bulk in for CPort data */
	__u8 ap_out_endpoint;		/* bulk out for CPort data */
	u8 *ap_buffer;

};

static inline struct es1_ap_dev *hd_to_es1(struct greybus_host_device *hd)
{
	return (struct es1_ap_dev *)(hd->hd_priv);
}

/*
 * Allocate the actual buffer for this gbuf and device and cport
 *
 * We are responsible for setting the following fields in a struct gbuf:
 *	void *hcpriv;
 *	void *transfer_buffer;
 *	u32 transfer_buffer_length;
 */
static int alloc_gbuf(struct gbuf *gbuf, unsigned int size, gfp_t gfp_mask)
{
	struct es1_ap_dev *es1 = hd_to_es1(gbuf->gdev->hd);
	u8 *buffer;

	/* For ES2 we need to figure out what cport is going to what endpoint,
	 * but for ES1, it's so dirt simple, we don't have a choice...
	 *
	 * Also, do a "slow" allocation now, if we need speed, use a cache
	 */
	buffer = kmalloc(size + 1, gfp_mask);
	if (!buffer)
		return -ENOMEM;

	/*
	 * we will encode the cport number in the first byte of the buffer, so
	 * set the second byte to be the "transfer buffer"
	 */
	buffer[0] = gbuf->cport->number;
	gbuf->transfer_buffer = &buffer[1];
	gbuf->transfer_buffer_length = size;

	gbuf->hdpriv = es1;	/* really, we could do something else here... */

	return 0;
}

/* Free the memory we allocated with a gbuf */
static void free_gbuf(struct gbuf *gbuf)
{
	u8 *transfer_buffer;
	u8 *buffer;

	transfer_buffer = gbuf->transfer_buffer;
	buffer = &transfer_buffer[-1];	/* yes, we mean -1 */
	kfree(buffer);
}


static struct greybus_host_driver es1_driver = {
	.hd_priv_size = sizeof(struct es1_ap_dev),
	.alloc_gbuf = alloc_gbuf,
	.free_gbuf = free_gbuf,
};


void ap_in_callback(struct urb *urb)
{
	struct device *dev = &urb->dev->dev;
	int status = urb->status;
	int retval;

	switch (status) {
	case 0:
		break;
	case -EOVERFLOW:
		dev_err(dev, "%s: overflow actual length is %d\n",
			__func__, urb->actual_length);
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -EILSEQ:
		/* device is gone, stop sending */
		return;
	default:
		dev_err(dev, "%s: unknown status %d\n", __func__, status);
		goto exit;
	}

	/* We have a message, create a new message structure, add it to the
	 * list, and wake up our thread that will process the messages.
	 */
	gb_new_ap_msg(urb->transfer_buffer, urb->actual_length);

exit:
	/* resubmit the urb to get more messages */
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(dev, "Can not submit urb for AP data: %d\n", retval);
}

void ap_out_callback(struct urb *urb)
{
	struct device *dev = &urb->dev->dev;
	int status = urb->status;

	switch (status) {
	case 0:
		break;
	case -EOVERFLOW:
		dev_err(dev, "%s: overflow actual length is %d\n",
			__func__, urb->actual_length);
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -EILSEQ:
		/* device is gone, stop sending */
		return;
	default:
		dev_err(dev, "%s: unknown status %d\n", __func__, status);
		goto exit;
	}

	// FIXME - queue up next AP message to send???
exit:
	return;
}


static int ap_probe(struct usb_interface *interface,
		    const struct usb_device_id *id)
{
	struct es1_ap_dev *es1;
	struct greybus_host_device *hd;
	struct usb_device *udev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i;

	udev = usb_get_dev(interface_to_usbdev(interface));

	hd = greybus_create_hd(&es1_driver, &udev->dev);
	if (!hd)
		return -ENOMEM;

	es1 = hd_to_es1(hd);
	es1->hd = hd;

	/* Control endpoint is the pipe to talk to this AP, so save it off */
	endpoint = &udev->ep0.desc;
	es1->ap_comm_endpoint = endpoint->bEndpointAddress;

	// FIXME
	// figure out endpoint for talking to the AP.
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(endpoint)) {
			es1->ap_in_endpoint = endpoint->bEndpointAddress;
		}
		if (usb_endpoint_is_bulk_out(endpoint)) {
			es1->ap_out_endpoint = endpoint->bEndpointAddress;
		}
		// FIXME - properly exit once found the AP endpoint
		// FIXME - set up cport endpoints
	}

	// FIXME - allocate buffer
	// FIXME = start up talking, then create the gb "devices" based on what the AP tells us.

	es1->usb_intf = interface;
	es1->usb_dev = udev;
	usb_set_intfdata(interface, es1);
	return 0;
}

static void ap_disconnect(struct usb_interface *interface)
{
	struct es1_ap_dev *es1;

	es1 = usb_get_intfdata(interface);

	/* Tear down everything! */

	usb_put_dev(es1->usb_dev);
	kfree(es1->ap_buffer);

	// FIXME
	//greybus_destroy_hd(es1->hd);
}

static struct usb_driver es1_ap_driver = {
	.name =		"es1_ap_driver",
	.probe =	ap_probe,
	.disconnect =	ap_disconnect,
	.id_table =	id_table,
};

module_usb_driver(es1_ap_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
