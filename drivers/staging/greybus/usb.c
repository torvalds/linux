/*
 * USB host driver for the Greybus "generic" USB module.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "greybus.h"

/* Version of the Greybus USB protocol we support */
#define GB_USB_VERSION_MAJOR		0x00
#define GB_USB_VERSION_MINOR		0x01

/* Greybus USB request types */
#define GB_USB_TYPE_INVALID		0x00
#define GB_USB_TYPE_PROTOCOL_VERSION	0x01
#define GB_USB_TYPE_HCD_STOP		0x02
#define GB_USB_TYPE_HCD_START		0x03
#define GB_USB_TYPE_URB_ENQUEUE		0x04
#define GB_USB_TYPE_URB_DEQUEUE		0x05
#define GB_USB_TYPE_ENDPOINT_DISABLE	0x06
#define GB_USB_TYPE_HUB_CONTROL		0x07
#define GB_USB_TYPE_GET_FRAME_NUMBER	0x08
#define GB_USB_TYPE_HUB_STATUS_DATA	0x09

struct gb_usb_urb_enqueue_request {
	__le32 pipe;
	__le32 transfer_flags;
	__le32 transfer_buffer_length;
	__le32 maxpacket;
	__le32 interval;
	__le64 hcpriv_ep;
	__le32 number_of_packets;
	u8 setup_packet[8];
	u8 payload[0];
};

struct gb_usb_urb_dequeue_request {
	__le64 hcpriv_ep;
};

struct gb_usb_endpoint_disable_request {
	__le64	hcpriv;
};

struct gb_usb_hub_control_request {
	__le16 typeReq;
	__le16 wValue;
	__le16 wIndex;
	__le16 wLength;
};

struct gb_usb_hub_control_response {
	u8 buf[0];
};

struct gb_usb_header {
	__le16	size;
	__le16	id;
	__u8	type;
};

struct gb_usb_hub_status {
	__le32 status;
	__le16 buf_size;
	u8 buf[0];
};

static struct gb_usb_hub_status *hub_status;	// FIXME!!!
static DEFINE_SPINLOCK(hub_status_lock);
static atomic_t frame_number;			// FIXME!!!

struct gb_usb_device {
	struct gb_connection *connection;

	struct usb_hcd *hcd;
	u8 version_major;
	u8 version_minor;
};

#define to_gb_usb_device(d) ((struct gb_usb_device*) d->hcd_priv)

/* Define get_version() routine */
define_get_version(gb_usb_device, USB);

static void hcd_stop(struct usb_hcd *hcd)
{
	struct gb_usb_device *dev = to_gb_usb_device(hcd);
	int ret;

	ret = gb_operation_sync(dev->connection, GB_USB_TYPE_HCD_STOP,
				NULL, 0, NULL, 0);
	if (ret)
		dev_err(&dev->connection->dev, "HCD stop failed '%d'\n", ret);
}

static int hcd_start(struct usb_hcd *hcd)
{
	struct usb_bus *bus = hcd_to_bus(hcd);
	struct gb_usb_device *dev = to_gb_usb_device(hcd);
	int ret;

	ret = gb_operation_sync(dev->connection, GB_USB_TYPE_HCD_START,
				NULL, 0, NULL, 0);
	if (ret) {
		dev_err(&dev->connection->dev, "HCD start failed '%d'\n", ret);
		return ret;
	}

	hcd->state = HC_STATE_RUNNING;
	if (bus->root_hub)
		usb_hcd_resume_root_hub(hcd);
	return 0;
}

static int urb_enqueue(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags)
{
	struct gb_usb_device *dev = to_gb_usb_device(hcd);
	struct gb_usb_urb_enqueue_request *request;
	struct gb_operation *operation;
	int ret;

	operation = gb_operation_create(dev->connection,
					GB_USB_TYPE_URB_ENQUEUE,
					sizeof(*request) +
					urb->transfer_buffer_length, 0,
					GFP_KERNEL);
	if (!operation)
		return -ENODEV;

	request = operation->request->payload;
	request->pipe = cpu_to_le32(urb->pipe);
	request->transfer_flags = cpu_to_le32(urb->transfer_flags);
	request->transfer_buffer_length = cpu_to_le32(urb->transfer_buffer_length);
	request->interval = cpu_to_le32(urb->interval);
	request->hcpriv_ep = cpu_to_le64((unsigned long)urb->ep->hcpriv);
	request->number_of_packets = cpu_to_le32(urb->number_of_packets);

	memcpy(request->setup_packet, urb->setup_packet, 8);
	memcpy(&request->payload, urb->transfer_buffer,
	       urb->transfer_buffer_length);

	ret = gb_operation_request_send_sync(operation);
	gb_operation_destroy(operation);

	return ret;
}

static int urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct gb_usb_device *dev = to_gb_usb_device(hcd);
	struct gb_usb_urb_dequeue_request request;
	int ret;

	urb->ep->hcpriv = NULL;
	request.hcpriv_ep = cpu_to_le64((unsigned long)urb->hcpriv);
	ret = gb_operation_sync(dev->connection, GB_USB_TYPE_URB_DEQUEUE,
				&request, sizeof(request), NULL, 0);
	urb->hcpriv = NULL;
	return ret;
}

static void endpoint_disable(struct usb_hcd *hcd, struct usb_host_endpoint *ep)
{
	struct gb_usb_device *dev = to_gb_usb_device(hcd);
	struct gb_usb_endpoint_disable_request request;
	int ret;

	request.hcpriv = cpu_to_le64((unsigned long)ep->hcpriv);
	ret = gb_operation_sync(dev->connection, GB_USB_TYPE_ENDPOINT_DISABLE,
				&request, sizeof(request), NULL, 0);
	ep->hcpriv = NULL;
}

static void endpoint_reset(struct usb_hcd *hcd, struct usb_host_endpoint *ep)
{
}

static int get_frame_number(struct usb_hcd *hcd)
{
	return atomic_read(&frame_number);
}

static int hub_status_data(struct usb_hcd *hcd, char *buf)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&hub_status_lock, flags);
	memcpy(buf, hub_status->buf, le16_to_cpu(hub_status->buf_size));
	retval = le32_to_cpu(hub_status->status);
	spin_unlock_irqrestore(&hub_status_lock, flags);

	return retval;
}

static int hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue, u16 wIndex,
		       char *buf, u16 wLength)
{
	struct gb_usb_hub_control_request request;
	struct gb_usb_device *dev = to_gb_usb_device(hcd);
	int ret;

	request.typeReq = cpu_to_le16(typeReq);
	request.wValue = cpu_to_le16(wValue);
	request.wIndex = cpu_to_le16(wIndex);
	request.wLength = cpu_to_le16(wLength);

	// FIXME - buf needs to come back in struct gb_usb_hub_control_response
	// for some types of requests, depending on typeReq.  Do we do this in a
	// "generic" way, or only ask for a response for the ones we "know" need
	// a response (a small subset of all valid typeReq, thankfully.)
	ret = gb_operation_sync(dev->connection, GB_USB_TYPE_HUB_CONTROL,
				&request, sizeof(request), NULL, 0);

	return ret;
}

static struct hc_driver usb_gb_hc_driver = {
	.description = "greybus_usb",
	.product_desc = "GB-Bridge USB Controller", /* TODO: Get this from GPB ?*/
	.flags = HCD_MEMORY | HCD_USB2, /* FIXME: Get this from GPB */
	.hcd_priv_size = sizeof(struct gb_usb_device),

	.start = hcd_start,
	.stop = hcd_stop,
	.urb_enqueue = urb_enqueue,
	.urb_dequeue = urb_dequeue,
	.endpoint_disable = endpoint_disable,
	.endpoint_reset = endpoint_reset,
	.get_frame_number = get_frame_number,
	.hub_status_data = hub_status_data,
	.hub_control = hub_control,
};

#if 0
static inline void gb_usb_handle_get_frame_number(struct gbuf *gbuf)
{
	__le32 frame_num;
	const size_t packet_size = sizeof(struct gb_usb_header) +
				   sizeof(frame_num);
	struct gb_usb_header* hdr = gbuf->transfer_buffer;

	if (le16_to_cpu(hdr->size) != packet_size) {
		pr_err("%s(): dropping packet too small\n", __func__);
		return;
	}

	frame_num = (__le32) ((char*) gbuf->transfer_buffer +
			      sizeof(struct gb_usb_header));
	atomic_set(&frame_number, le32_to_cpu(frame_num));
}

static inline void gb_usb_handle_hubs_status_data(struct gbuf *gbuf)
{
	struct gb_usb_hub_status *new_hubstatus, *hubstatus;
	struct gb_usb_header* hdr = gbuf->transfer_buffer;
	const size_t min_packet_size = sizeof(struct gb_usb_header) +
				       sizeof(struct gb_usb_hub_status);
	unsigned long flags;

	if (le16_to_cpu(hdr->size) < min_packet_size) {
		pr_err("%s(): dropping packet too small\n", __func__);
		return;
	}

	hubstatus = (struct gb_usb_hub_status*) ((char*) gbuf->transfer_buffer
						+ sizeof(struct gb_usb_header));

	if (le16_to_cpu(hdr->size) != min_packet_size + hubstatus->buf_size) {
		pr_err("%s(): invalid packet size, dropping packet\n",
		       __func__);
		return;
	}

	new_hubstatus = kmalloc(hubstatus->buf_size, GFP_KERNEL);
	memcpy(&new_hubstatus, hubstatus, hubstatus->buf_size);

	spin_lock_irqsave(&hub_status_lock, flags);
	hubstatus = hub_status;
	hub_status = new_hubstatus;
	spin_unlock_irqrestore(&hub_status_lock, flags);

	kfree(hubstatus);
}

static void gb_usb_in_handler(struct gbuf *gbuf)
{
	struct gb_usb_header* hdr = gbuf->transfer_buffer;

	switch (hdr->type) {
		case GB_USB_TYPE_GET_FRAME_NUMBER:
			gb_usb_handle_get_frame_number(gbuf);
			break;

		case GB_USB_TYPE_HUB_STATUS_DATA:
			gb_usb_handle_hubs_status_data(gbuf);
			break;
	}
}
#endif

static int gb_usb_connection_init(struct gb_connection *connection)
{
	struct device *dev = &connection->dev;
	struct gb_usb_device *gb_usb_dev;

	int retval;

	gb_usb_dev = kzalloc(sizeof(*gb_usb_dev), GFP_KERNEL);
	if (!gb_usb_dev)
		return -ENOMEM;

	gb_usb_dev->connection = connection;
	connection->private = gb_usb_dev;

	/* Check for compatible protocol version */
	retval = get_version(gb_usb_dev);
	if (retval)
		goto error_create_hcd;

	gb_usb_dev->hcd = usb_create_hcd(&usb_gb_hc_driver, dev, dev_name(dev));
	if (!gb_usb_dev->hcd) {
		retval = -ENODEV;
		goto error_create_hcd;
	}

	gb_usb_dev->hcd->has_tt = 1;
	gb_usb_dev->hcd->hcd_priv[0] = (unsigned long) gb_usb_dev;

	retval = usb_add_hcd(gb_usb_dev->hcd, 0, 0);
	if (retval)
		goto error_add_hcd;

	return 0;
error_add_hcd:
	usb_put_hcd(gb_usb_dev->hcd);
error_create_hcd:
	kfree(gb_usb_dev);
	return retval;
}

static void gb_usb_connection_exit(struct gb_connection *connection)
{
	// FIXME - tear everything down!
}

static struct gb_protocol usb_protocol = {
	.name			= "usb",
	.id			= GREYBUS_PROTOCOL_USB,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_usb_connection_init,
	.connection_exit	= gb_usb_connection_exit,
	.request_recv		= NULL,	/* FIXME we have requests!!! */
};

gb_builtin_protocol_driver(usb_protocol);
