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

static struct svc_msg *svc_msg_alloc(enum svc_function_type type)
{
	struct svc_msg *svc_msg;

	svc_msg = kzalloc((sizeof *svc_msg), GFP_KERNEL);
	if (!svc_msg)
		return NULL;

	// FIXME - verify we are only sending message types we should be
	svc_msg->header.type = type;
	return svc_msg;
}

static void svc_msg_free(struct svc_msg *svc_msg)
{
	kfree(svc_msg);
}

static int svc_msg_send(struct svc_msg *svc_msg)
{
	// FIXME - Do something with this message!


	svc_msg_free(svc_msg);
	return 0;
}


static void svc_handshake(struct svc_function_handshake *handshake,
			  struct es1_ap_dev *es1)
{
	struct svc_msg *svc_msg;

	/* A new SVC communication channel, let's verify it was for us */
	if (handshake->handshake_type != SVC_HANDSHAKE_SVC_HELLO) {
		/* we don't know what to do with this, log it and return */
		dev_dbg(&es1->usb_intf->dev,
			"received invalid handshake type %d\n",
			handshake->handshake_type);
		return;
	}

	/* Send back a AP_HELLO message */
	svc_msg = svc_msg_alloc(SVC_FUNCTION_HANDSHAKE);
	if (!svc_msg)
		return;

	svc_msg->handshake.handshake_type = SVC_HANDSHAKE_AP_HELLO;
	svc_msg_send(svc_msg);
}

static void svc_management(struct svc_function_unipro_management *management,
			   struct es1_ap_dev *es1)
{
	/* What?  An AP should not get this message */
	dev_err(&es1->usb_intf->dev, "Got an svc management message???\n");
}

static void svc_hotplug(struct svc_function_hotplug *hotplug,
			struct es1_ap_dev *es1)
{
	u8 module_id = hotplug->module_id;

	switch (hotplug->hotplug_event) {
	case SVC_HOTPLUG_EVENT:
		dev_dbg(&es1->usb_intf->dev, "module id %d added\n",
			module_id);
		// FIXME - add the module to the system
		break;

	case SVC_HOTUNPLUG_EVENT:
		dev_dbg(&es1->usb_intf->dev, "module id %d removed\n",
			module_id);
		// FIXME - remove the module from the system
		break;

	default:
		dev_err(&es1->usb_intf->dev, "received invalid hotplug message type %d\n",
			hotplug->hotplug_event);
		break;
	}
}

static void svc_ddb(struct svc_function_ddb *ddb, struct es1_ap_dev *es1)
{
	/* What?  An AP should not get this message */
	dev_err(&es1->usb_intf->dev, "Got an svc DDB message???\n");
}

static void svc_power(struct svc_function_power *power, struct es1_ap_dev *es1)
{
	u8 module_id = power->module_id;

	if (power->power_type != SVC_POWER_BATTERY_STATUS) {
		dev_err(&es1->usb_intf->dev, "received invalid power type %d\n",
			power->power_type);
		return;
	}

	dev_dbg(&es1->usb_intf->dev, "power status for module id %d is %d\n",
		module_id, power->status.status);

	// FIXME - do something with the power information, like update our
	// battery information...
}

static void svc_epm(struct svc_function_epm *epm, struct es1_ap_dev *es1)
{
	/* What?  An AP should not get this message */
	dev_err(&es1->usb_intf->dev, "Got an EPM message???\n");
}

static void svc_suspend(struct svc_function_suspend *suspend,
			struct es1_ap_dev *es1)
{
	/* What?  An AP should not get this message */
	dev_err(&es1->usb_intf->dev, "Got an suspend message???\n");
}

/* Main message loop for ap messages */
/* Odds are, most of this logic can move to core.c someday, but as we only have
 * one host controller driver for now, let's leave it here */
static void ap_msg(struct svc_msg *svc_msg, struct greybus_host_device *hd)
{
	struct es1_ap_dev *es1 = hd_to_es1(hd);

	/* Look at the message to figure out what to do with it */
	switch (svc_msg->header.type) {
	case SVC_FUNCTION_HANDSHAKE:
		svc_handshake(&svc_msg->handshake, es1);
		break;
	case SVC_FUNCTION_UNIPRO_NETWORK_MANAGEMENT:
		svc_management(&svc_msg->management, es1);
		break;
	case SVC_FUNCTION_HOTPLUG:
		svc_hotplug(&svc_msg->hotplug, es1);
		break;
	case SVC_FUNCTION_DDB:
		svc_ddb(&svc_msg->ddb, es1);
		break;
	case SVC_FUNCTION_POWER:
		svc_power(&svc_msg->power, es1);
		break;
	case SVC_FUNCTION_EPM:
		svc_epm(&svc_msg->epm, es1);
		break;
	case SVC_FUNCTION_SUSPEND:
		svc_suspend(&svc_msg->suspend, es1);
		break;
	default:
		dev_err(&es1->usb_intf->dev, "received invalid SVC message type %d\n",
			svc_msg->header.type);
	}
}


static struct greybus_host_driver es1_driver = {
	.hd_priv_size = sizeof(struct es1_ap_dev),
	.alloc_gbuf = alloc_gbuf,
	.free_gbuf = free_gbuf,
	.ap_msg = ap_msg,
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

void cport_in_callback(struct urb *urb)
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

void cport_out_callback(struct urb *urb)
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
		goto error_urb;

	usb_fill_int_urb(es1->svc_urb, udev,
			 usb_rcvintpipe(udev, es1->svc_endpoint),
			 es1->svc_buffer, buffer_size, svc_callback,
			 es1, svc_interval);
	retval = usb_submit_urb(es1->svc_urb, GFP_KERNEL);
	if (retval)
		goto error_submit_urb;

	return 0;

error_submit_urb:
	usb_free_urb(es1->svc_urb);
error_urb:
	kfree(es1->svc_buffer);
error:
	greybus_remove_hd(es1->hd);
	return retval;
}

static void ap_disconnect(struct usb_interface *interface)
{
	struct es1_ap_dev *es1;

	es1 = usb_get_intfdata(interface);

	/* Tear down everything! */
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
