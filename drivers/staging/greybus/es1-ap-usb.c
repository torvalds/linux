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
#include "svc_msg.h"

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0xffff, 0x0001) },		// FIXME
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

/*
 * Number of CPort IN urbs in flight at any point in time.
 * Adjust if we are having stalls in the USB buffer due to not enough urbs in
 * flight.
 */
#define NUM_CPORT_IN_URB	4

/* Number of CPort OUT urbs in flight at any point in time.
 * Adjust if we get messages saying we are out of urbs in the system log.
 */
#define NUM_CPORT_OUT_URB	8


struct es1_ap_dev {
	struct usb_device *usb_dev;
	struct usb_interface *usb_intf;
	struct greybus_host_device *hd;

	__u8 control_endpoint;		/* endpoint to send data to SVC */
	__u8 svc_endpoint;		/* endpoint for SVC data */
	__u8 cport_in_endpoint;		/* bulk in for CPort data */
	__u8 cport_out_endpoint;	/* bulk out for CPort data */
	u8 *svc_buffer;			/* buffer for SVC messages coming in */
	struct urb *svc_urb;		/* urb for SVC messages coming in */
	struct urb *cport_in_urb[NUM_CPORT_IN_URB];	/* CPort IN urbs */
	u8 *cport_in_buffer[NUM_CPORT_IN_URB];		/* CPort IN buffers */
	struct urb *cport_out_urb[NUM_CPORT_OUT_URB];	/* CPort OUT urbs */
	u8 cport_out_urb_busy[NUM_CPORT_OUT_URB];	/* CPort OUT urb busy marker */
};

static inline struct es1_ap_dev *hd_to_es1(struct greybus_host_device *hd)
{
	return (struct es1_ap_dev *)(hd->hd_priv);
}

static void cport_out_callback(struct urb *urb);

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

	/* When we send the gbuf, we need this pointer to be here */
	gbuf->hdpriv = es1;

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

#define ES1_TIMEOUT	500	/* 500 ms for the SVC to do something */
static int send_svc_msg(struct svc_msg *svc_msg, struct greybus_host_device *hd)
{
	struct es1_ap_dev *es1 = hd_to_es1(hd);
	int retval;

	/* SVC messages go down our control pipe */
	retval = usb_control_msg(es1->usb_dev,
				 usb_sndctrlpipe(es1->usb_dev,
						 es1->control_endpoint),
				 0x01,	/* vendor request AP message */
				 USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_OTHER,
				 0x00, 0x00,
				 (char *)svc_msg,
				 sizeof(*svc_msg),
				 ES1_TIMEOUT);
	if (retval != sizeof(*svc_msg))
		return retval;

	return 0;
}

static struct urb *next_free_urb(struct es1_ap_dev *es1)
{
	// FIXME
	return NULL;
}

static int send_gbuf(struct gbuf *gbuf, struct greybus_host_device *hd,
		     gfp_t gfp_mask)
{
	struct es1_ap_dev *es1 = hd_to_es1(hd);
	struct usb_device *udev = es1->usb_dev;
	int retval;
	u8 *transfer_buffer;
	u8 *buffer;
	struct urb *urb;

	transfer_buffer = gbuf->transfer_buffer;
	buffer = &transfer_buffer[-1];	/* yes, we mean -1 */

	/* Find a free urb */
	urb = next_free_urb(es1);
	if (!urb)
		return -ENOMEM;

	usb_fill_bulk_urb(urb, udev,
			  usb_sndbulkpipe(udev, es1->cport_out_endpoint),
			  buffer, gbuf->transfer_buffer_length + 1,
			  cport_out_callback, gbuf);
	retval = usb_submit_urb(urb, gfp_mask);
	return retval;
}

static struct greybus_host_driver es1_driver = {
	.hd_priv_size = sizeof(struct es1_ap_dev),
	.alloc_gbuf = alloc_gbuf,
	.free_gbuf = free_gbuf,
	.send_svc_msg = send_svc_msg,
	.send_gbuf = send_gbuf,
};

/* Callback for when we get a SVC message */
static void svc_callback(struct urb *urb)
{
	struct es1_ap_dev *es1 = urb->context;
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
	gb_new_ap_msg(urb->transfer_buffer, urb->actual_length, es1->hd);

exit:
	/* resubmit the urb to get more messages */
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(dev, "Can not submit urb for AP data: %d\n", retval);
}

static void cport_in_callback(struct urb *urb)
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

	// FIXME - handle the CPort in data
exit:
	return;
}

static void cport_out_callback(struct urb *urb)
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

	// FIXME - handle the CPort out data callback
exit:
	return;
}

/*
 * The ES1 USB Bridge device contains 4 endpoints
 * 1 Control - usual USB stuff + AP -> SVC messages
 * 1 Interrupt IN - SVC -> AP messages
 * 1 Bulk IN - CPort data in
 * 1 Bulk OUT - CPorta data out
 */
static int ap_probe(struct usb_interface *interface,
		    const struct usb_device_id *id)
{
	struct es1_ap_dev *es1;
	struct greybus_host_device *hd;
	struct usb_device *udev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	bool int_in_found = false;
	bool bulk_in_found = false;
	bool bulk_out_found = false;
	int retval = -ENOMEM;
	int i;
	int buffer_size = 0;
	u8 svc_interval = 0;

	udev = usb_get_dev(interface_to_usbdev(interface));

	hd = greybus_create_hd(&es1_driver, &udev->dev);
	if (!hd)
		return -ENOMEM;

	es1 = hd_to_es1(hd);
	es1->hd = hd;
	es1->usb_intf = interface;
	es1->usb_dev = udev;
	usb_set_intfdata(interface, es1);

	/* Control endpoint is the pipe to talk to this AP, so save it off */
	endpoint = &udev->ep0.desc;
	es1->control_endpoint = endpoint->bEndpointAddress;

	/* find all 3 of our endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_int_in(endpoint)) {
			es1->svc_endpoint = endpoint->bEndpointAddress;
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			svc_interval = endpoint->bInterval;
			int_in_found = true;
		} else if (usb_endpoint_is_bulk_in(endpoint)) {
			es1->cport_in_endpoint = endpoint->bEndpointAddress;
			bulk_in_found = true;
		} else if (usb_endpoint_is_bulk_out(endpoint)) {
			es1->cport_out_endpoint = endpoint->bEndpointAddress;
			bulk_out_found = true;
		} else {
			dev_err(&udev->dev,
				"Unknown endpoint type found, address %x\n",
				endpoint->bEndpointAddress);
		}
	}
	if ((int_in_found == false) ||
	    (bulk_in_found == false) ||
	    (bulk_out_found == false)) {
		dev_err(&udev->dev, "Not enough endpoints found in device, aborting!\n");
		goto error;
	}

	/* Create our buffer and URB to get SVC messages, and start it up */
	es1->svc_buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!es1->svc_buffer)
		goto error;

	es1->svc_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!es1->svc_urb)
		goto error_int_urb;

	usb_fill_int_urb(es1->svc_urb, udev,
			 usb_rcvintpipe(udev, es1->svc_endpoint),
			 es1->svc_buffer, buffer_size, svc_callback,
			 es1, svc_interval);
	retval = usb_submit_urb(es1->svc_urb, GFP_KERNEL);
	if (retval)
		goto error_submit_urb;

	/* Allocate buffers for our cport in messages and start them up */
	for (i = 0; i < NUM_CPORT_IN_URB; ++i) {
		struct urb *urb;
		u8 *buffer;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb)
			goto error_bulk_in_urb;
		buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!buffer)
			goto error_bulk_in_urb;

		usb_fill_bulk_urb(urb, udev,
				  usb_rcvbulkpipe(udev, es1->cport_in_endpoint),
				  buffer, PAGE_SIZE, cport_in_callback, es1);
		es1->cport_in_urb[i] = urb;
		es1->cport_in_buffer[i] = buffer;
		retval = usb_submit_urb(urb, GFP_KERNEL);
		if (retval)
			goto error_bulk_in_urb;
	}

	/* Allocate urbs for our CPort OUT messages */
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		struct urb *urb;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb)
			goto error_bulk_out_urb;

		es1->cport_out_urb[i] = urb;
		es1->cport_out_urb_busy[i] = false;	/* just to be anal */
	}

	return 0;

error_bulk_out_urb:
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i)
		usb_free_urb(es1->cport_out_urb[i]);

error_bulk_in_urb:
	for (i = 0; i < NUM_CPORT_IN_URB; ++i) {
		usb_kill_urb(es1->cport_in_urb[i]);
		usb_free_urb(es1->cport_in_urb[i]);
		kfree(es1->cport_in_buffer[i]);
	}

error_submit_urb:
	usb_free_urb(es1->svc_urb);
error_int_urb:
	kfree(es1->svc_buffer);
error:
	greybus_remove_hd(es1->hd);
	return retval;
}

static void ap_disconnect(struct usb_interface *interface)
{
	struct es1_ap_dev *es1;
	int i;

	es1 = usb_get_intfdata(interface);
	if (!es1)
		return;

	/* Tear down everything! */
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		usb_kill_urb(es1->cport_out_urb[i]);
		usb_free_urb(es1->cport_out_urb[i]);
	}

	for (i = 0; i < NUM_CPORT_IN_URB; ++i) {
		usb_kill_urb(es1->cport_in_urb[i]);
		usb_free_urb(es1->cport_in_urb[i]);
		kfree(es1->cport_in_buffer[i]);
	}

	usb_kill_urb(es1->svc_urb);
	usb_free_urb(es1->svc_urb);
	usb_put_dev(es1->usb_dev);
	kfree(es1->svc_buffer);
	greybus_remove_hd(es1->hd);
	usb_set_intfdata(interface, NULL);
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
