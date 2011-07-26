/*
 *	uvc_gadget.c  --  USB Video Class Gadget driver
 *
 *	Copyright (C) 2009-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/video.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>

#include "uvc.h"

unsigned int uvc_gadget_trace_param;

/* --------------------------------------------------------------------------
 * Function descriptors
 */

/* string IDs are assigned dynamically */

#define UVC_STRING_ASSOCIATION_IDX		0
#define UVC_STRING_CONTROL_IDX			1
#define UVC_STRING_STREAMING_IDX		2

static struct usb_string uvc_en_us_strings[] = {
	[UVC_STRING_ASSOCIATION_IDX].s = "UVC Camera",
	[UVC_STRING_CONTROL_IDX].s = "Video Control",
	[UVC_STRING_STREAMING_IDX].s = "Video Streaming",
	{  }
};

static struct usb_gadget_strings uvc_stringtab = {
	.language = 0x0409,	/* en-us */
	.strings = uvc_en_us_strings,
};

static struct usb_gadget_strings *uvc_function_strings[] = {
	&uvc_stringtab,
	NULL,
};

#define UVC_INTF_VIDEO_CONTROL			0
#define UVC_INTF_VIDEO_STREAMING		1

static struct usb_interface_assoc_descriptor uvc_iad __initdata = {
	.bLength		= sizeof(uvc_iad),
	.bDescriptorType	= USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface	= 0,
	.bInterfaceCount	= 2,
	.bFunctionClass		= USB_CLASS_VIDEO,
	.bFunctionSubClass	= UVC_SC_VIDEO_INTERFACE_COLLECTION,
	.bFunctionProtocol	= 0x00,
	.iFunction		= 0,
};

static struct usb_interface_descriptor uvc_control_intf __initdata = {
	.bLength		= USB_DT_INTERFACE_SIZE,
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= UVC_INTF_VIDEO_CONTROL,
	.bAlternateSetting	= 0,
	.bNumEndpoints		= 1,
	.bInterfaceClass	= USB_CLASS_VIDEO,
	.bInterfaceSubClass	= UVC_SC_VIDEOCONTROL,
	.bInterfaceProtocol	= 0x00,
	.iInterface		= 0,
};

static struct usb_endpoint_descriptor uvc_control_ep __initdata = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize		= cpu_to_le16(16),
	.bInterval		= 8,
};

static struct uvc_control_endpoint_descriptor uvc_control_cs_ep __initdata = {
	.bLength		= UVC_DT_CONTROL_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_CS_ENDPOINT,
	.bDescriptorSubType	= UVC_EP_INTERRUPT,
	.wMaxTransferSize	= cpu_to_le16(16),
};

static struct usb_interface_descriptor uvc_streaming_intf_alt0 __initdata = {
	.bLength		= USB_DT_INTERFACE_SIZE,
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= UVC_INTF_VIDEO_STREAMING,
	.bAlternateSetting	= 0,
	.bNumEndpoints		= 0,
	.bInterfaceClass	= USB_CLASS_VIDEO,
	.bInterfaceSubClass	= UVC_SC_VIDEOSTREAMING,
	.bInterfaceProtocol	= 0x00,
	.iInterface		= 0,
};

static struct usb_interface_descriptor uvc_streaming_intf_alt1 __initdata = {
	.bLength		= USB_DT_INTERFACE_SIZE,
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= UVC_INTF_VIDEO_STREAMING,
	.bAlternateSetting	= 1,
	.bNumEndpoints		= 1,
	.bInterfaceClass	= USB_CLASS_VIDEO,
	.bInterfaceSubClass	= UVC_SC_VIDEOSTREAMING,
	.bInterfaceProtocol	= 0x00,
	.iInterface		= 0,
};

static struct usb_endpoint_descriptor uvc_streaming_ep = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize		= cpu_to_le16(512),
	.bInterval		= 1,
};

static const struct usb_descriptor_header * const uvc_fs_streaming[] = {
	(struct usb_descriptor_header *) &uvc_streaming_intf_alt1,
	(struct usb_descriptor_header *) &uvc_streaming_ep,
	NULL,
};

static const struct usb_descriptor_header * const uvc_hs_streaming[] = {
	(struct usb_descriptor_header *) &uvc_streaming_intf_alt1,
	(struct usb_descriptor_header *) &uvc_streaming_ep,
	NULL,
};

/* --------------------------------------------------------------------------
 * Control requests
 */

static void
uvc_function_ep0_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct uvc_device *uvc = req->context;
	struct v4l2_event v4l2_event;
	struct uvc_event *uvc_event = (void *)&v4l2_event.u.data;

	if (uvc->event_setup_out) {
		uvc->event_setup_out = 0;

		memset(&v4l2_event, 0, sizeof(v4l2_event));
		v4l2_event.type = UVC_EVENT_DATA;
		uvc_event->data.length = req->actual;
		memcpy(&uvc_event->data.data, req->buf, req->actual);
		v4l2_event_queue(uvc->vdev, &v4l2_event);
	}
}

static int
uvc_function_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct uvc_device *uvc = to_uvc(f);
	struct v4l2_event v4l2_event;
	struct uvc_event *uvc_event = (void *)&v4l2_event.u.data;

	/* printk(KERN_INFO "setup request %02x %02x value %04x index %04x %04x\n",
	 *	ctrl->bRequestType, ctrl->bRequest, le16_to_cpu(ctrl->wValue),
	 *	le16_to_cpu(ctrl->wIndex), le16_to_cpu(ctrl->wLength));
	 */

	if ((ctrl->bRequestType & USB_TYPE_MASK) != USB_TYPE_CLASS) {
		INFO(f->config->cdev, "invalid request type\n");
		return -EINVAL;
	}

	/* Stall too big requests. */
	if (le16_to_cpu(ctrl->wLength) > UVC_MAX_REQUEST_SIZE)
		return -EINVAL;

	memset(&v4l2_event, 0, sizeof(v4l2_event));
	v4l2_event.type = UVC_EVENT_SETUP;
	memcpy(&uvc_event->req, ctrl, sizeof(uvc_event->req));
	v4l2_event_queue(uvc->vdev, &v4l2_event);

	return 0;
}

static int
uvc_function_get_alt(struct usb_function *f, unsigned interface)
{
	struct uvc_device *uvc = to_uvc(f);

	INFO(f->config->cdev, "uvc_function_get_alt(%u)\n", interface);

	if (interface == uvc->control_intf)
		return 0;
	else if (interface != uvc->streaming_intf)
		return -EINVAL;
	else
		return uvc->state == UVC_STATE_STREAMING ? 1 : 0;
}

static int
uvc_function_set_alt(struct usb_function *f, unsigned interface, unsigned alt)
{
	struct uvc_device *uvc = to_uvc(f);
	struct v4l2_event v4l2_event;
	struct uvc_event *uvc_event = (void *)&v4l2_event.u.data;

	INFO(f->config->cdev, "uvc_function_set_alt(%u, %u)\n", interface, alt);

	if (interface == uvc->control_intf) {
		if (alt)
			return -EINVAL;

		if (uvc->state == UVC_STATE_DISCONNECTED) {
			memset(&v4l2_event, 0, sizeof(v4l2_event));
			v4l2_event.type = UVC_EVENT_CONNECT;
			uvc_event->speed = f->config->cdev->gadget->speed;
			v4l2_event_queue(uvc->vdev, &v4l2_event);

			uvc->state = UVC_STATE_CONNECTED;
		}

		return 0;
	}

	if (interface != uvc->streaming_intf)
		return -EINVAL;

	/* TODO
	if (usb_endpoint_xfer_bulk(&uvc->desc.vs_ep))
		return alt ? -EINVAL : 0;
	*/

	switch (alt) {
	case 0:
		if (uvc->state != UVC_STATE_STREAMING)
			return 0;

		if (uvc->video.ep)
			usb_ep_disable(uvc->video.ep);

		memset(&v4l2_event, 0, sizeof(v4l2_event));
		v4l2_event.type = UVC_EVENT_STREAMOFF;
		v4l2_event_queue(uvc->vdev, &v4l2_event);

		uvc->state = UVC_STATE_CONNECTED;
		break;

	case 1:
		if (uvc->state != UVC_STATE_CONNECTED)
			return 0;

		if (uvc->video.ep) {
			uvc->video.ep->desc = &uvc_streaming_ep;
			usb_ep_enable(uvc->video.ep);
		}

		memset(&v4l2_event, 0, sizeof(v4l2_event));
		v4l2_event.type = UVC_EVENT_STREAMON;
		v4l2_event_queue(uvc->vdev, &v4l2_event);

		uvc->state = UVC_STATE_STREAMING;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static void
uvc_function_disable(struct usb_function *f)
{
	struct uvc_device *uvc = to_uvc(f);
	struct v4l2_event v4l2_event;

	INFO(f->config->cdev, "uvc_function_disable\n");

	memset(&v4l2_event, 0, sizeof(v4l2_event));
	v4l2_event.type = UVC_EVENT_DISCONNECT;
	v4l2_event_queue(uvc->vdev, &v4l2_event);

	uvc->state = UVC_STATE_DISCONNECTED;
}

/* --------------------------------------------------------------------------
 * Connection / disconnection
 */

void
uvc_function_connect(struct uvc_device *uvc)
{
	struct usb_composite_dev *cdev = uvc->func.config->cdev;
	int ret;

	if ((ret = usb_function_activate(&uvc->func)) < 0)
		INFO(cdev, "UVC connect failed with %d\n", ret);
}

void
uvc_function_disconnect(struct uvc_device *uvc)
{
	struct usb_composite_dev *cdev = uvc->func.config->cdev;
	int ret;

	if ((ret = usb_function_deactivate(&uvc->func)) < 0)
		INFO(cdev, "UVC disconnect failed with %d\n", ret);
}

/* --------------------------------------------------------------------------
 * USB probe and disconnect
 */

static int
uvc_register_video(struct uvc_device *uvc)
{
	struct usb_composite_dev *cdev = uvc->func.config->cdev;
	struct video_device *video;

	/* TODO reference counting. */
	video = video_device_alloc();
	if (video == NULL)
		return -ENOMEM;

	video->parent = &cdev->gadget->dev;
	video->minor = -1;
	video->fops = &uvc_v4l2_fops;
	video->release = video_device_release;
	strncpy(video->name, cdev->gadget->name, sizeof(video->name));

	uvc->vdev = video;
	video_set_drvdata(video, uvc);

	return video_register_device(video, VFL_TYPE_GRABBER, -1);
}

#define UVC_COPY_DESCRIPTOR(mem, dst, desc) \
	do { \
		memcpy(mem, desc, (desc)->bLength); \
		*(dst)++ = mem; \
		mem += (desc)->bLength; \
	} while (0);

#define UVC_COPY_DESCRIPTORS(mem, dst, src) \
	do { \
		const struct usb_descriptor_header * const *__src; \
		for (__src = src; *__src; ++__src) { \
			memcpy(mem, *__src, (*__src)->bLength); \
			*dst++ = mem; \
			mem += (*__src)->bLength; \
		} \
	} while (0)

static struct usb_descriptor_header ** __init
uvc_copy_descriptors(struct uvc_device *uvc, enum usb_device_speed speed)
{
	struct uvc_input_header_descriptor *uvc_streaming_header;
	struct uvc_header_descriptor *uvc_control_header;
	const struct uvc_descriptor_header * const *uvc_streaming_cls;
	const struct usb_descriptor_header * const *uvc_streaming_std;
	const struct usb_descriptor_header * const *src;
	struct usb_descriptor_header **dst;
	struct usb_descriptor_header **hdr;
	unsigned int control_size;
	unsigned int streaming_size;
	unsigned int n_desc;
	unsigned int bytes;
	void *mem;

	uvc_streaming_cls = (speed == USB_SPEED_FULL)
			  ? uvc->desc.fs_streaming : uvc->desc.hs_streaming;
	uvc_streaming_std = (speed == USB_SPEED_FULL)
			  ? uvc_fs_streaming : uvc_hs_streaming;

	/* Descriptors layout
	 *
	 * uvc_iad
	 * uvc_control_intf
	 * Class-specific UVC control descriptors
	 * uvc_control_ep
	 * uvc_control_cs_ep
	 * uvc_streaming_intf_alt0
	 * Class-specific UVC streaming descriptors
	 * uvc_{fs|hs}_streaming
	 */

	/* Count descriptors and compute their size. */
	control_size = 0;
	streaming_size = 0;
	bytes = uvc_iad.bLength + uvc_control_intf.bLength
	      + uvc_control_ep.bLength + uvc_control_cs_ep.bLength
	      + uvc_streaming_intf_alt0.bLength;
	n_desc = 5;

	for (src = (const struct usb_descriptor_header**)uvc->desc.control; *src; ++src) {
		control_size += (*src)->bLength;
		bytes += (*src)->bLength;
		n_desc++;
	}
	for (src = (const struct usb_descriptor_header**)uvc_streaming_cls; *src; ++src) {
		streaming_size += (*src)->bLength;
		bytes += (*src)->bLength;
		n_desc++;
	}
	for (src = uvc_streaming_std; *src; ++src) {
		bytes += (*src)->bLength;
		n_desc++;
	}

	mem = kmalloc((n_desc + 1) * sizeof(*src) + bytes, GFP_KERNEL);
	if (mem == NULL)
		return NULL;

	hdr = mem;
	dst = mem;
	mem += (n_desc + 1) * sizeof(*src);

	/* Copy the descriptors. */
	UVC_COPY_DESCRIPTOR(mem, dst, &uvc_iad);
	UVC_COPY_DESCRIPTOR(mem, dst, &uvc_control_intf);

	uvc_control_header = mem;
	UVC_COPY_DESCRIPTORS(mem, dst,
		(const struct usb_descriptor_header**)uvc->desc.control);
	uvc_control_header->wTotalLength = cpu_to_le16(control_size);
	uvc_control_header->bInCollection = 1;
	uvc_control_header->baInterfaceNr[0] = uvc->streaming_intf;

	UVC_COPY_DESCRIPTOR(mem, dst, &uvc_control_ep);
	UVC_COPY_DESCRIPTOR(mem, dst, &uvc_control_cs_ep);
	UVC_COPY_DESCRIPTOR(mem, dst, &uvc_streaming_intf_alt0);

	uvc_streaming_header = mem;
	UVC_COPY_DESCRIPTORS(mem, dst,
		(const struct usb_descriptor_header**)uvc_streaming_cls);
	uvc_streaming_header->wTotalLength = cpu_to_le16(streaming_size);
	uvc_streaming_header->bEndpointAddress = uvc_streaming_ep.bEndpointAddress;

	UVC_COPY_DESCRIPTORS(mem, dst, uvc_streaming_std);

	*dst = NULL;
	return hdr;
}

static void
uvc_function_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct uvc_device *uvc = to_uvc(f);

	INFO(cdev, "uvc_function_unbind\n");

	if (uvc->vdev) {
		if (uvc->vdev->minor == -1)
			video_device_release(uvc->vdev);
		else
			video_unregister_device(uvc->vdev);
		uvc->vdev = NULL;
	}

	if (uvc->control_ep)
		uvc->control_ep->driver_data = NULL;
	if (uvc->video.ep)
		uvc->video.ep->driver_data = NULL;

	if (uvc->control_req) {
		usb_ep_free_request(cdev->gadget->ep0, uvc->control_req);
		kfree(uvc->control_buf);
	}

	kfree(f->descriptors);
	kfree(f->hs_descriptors);

	kfree(uvc);
}

static int __init
uvc_function_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct uvc_device *uvc = to_uvc(f);
	struct usb_ep *ep;
	int ret = -EINVAL;

	INFO(cdev, "uvc_function_bind\n");

	/* Allocate endpoints. */
	ep = usb_ep_autoconfig(cdev->gadget, &uvc_control_ep);
	if (!ep) {
		INFO(cdev, "Unable to allocate control EP\n");
		goto error;
	}
	uvc->control_ep = ep;
	ep->driver_data = uvc;

	ep = usb_ep_autoconfig(cdev->gadget, &uvc_streaming_ep);
	if (!ep) {
		INFO(cdev, "Unable to allocate streaming EP\n");
		goto error;
	}
	uvc->video.ep = ep;
	ep->driver_data = uvc;

	/* Allocate interface IDs. */
	if ((ret = usb_interface_id(c, f)) < 0)
		goto error;
	uvc_iad.bFirstInterface = ret;
	uvc_control_intf.bInterfaceNumber = ret;
	uvc->control_intf = ret;

	if ((ret = usb_interface_id(c, f)) < 0)
		goto error;
	uvc_streaming_intf_alt0.bInterfaceNumber = ret;
	uvc_streaming_intf_alt1.bInterfaceNumber = ret;
	uvc->streaming_intf = ret;

	/* Copy descriptors. */
	f->descriptors = uvc_copy_descriptors(uvc, USB_SPEED_FULL);
	f->hs_descriptors = uvc_copy_descriptors(uvc, USB_SPEED_HIGH);

	/* Preallocate control endpoint request. */
	uvc->control_req = usb_ep_alloc_request(cdev->gadget->ep0, GFP_KERNEL);
	uvc->control_buf = kmalloc(UVC_MAX_REQUEST_SIZE, GFP_KERNEL);
	if (uvc->control_req == NULL || uvc->control_buf == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	uvc->control_req->buf = uvc->control_buf;
	uvc->control_req->complete = uvc_function_ep0_complete;
	uvc->control_req->context = uvc;

	/* Avoid letting this gadget enumerate until the userspace server is
	 * active.
	 */
	if ((ret = usb_function_deactivate(f)) < 0)
		goto error;

	/* Initialise video. */
	ret = uvc_video_init(&uvc->video);
	if (ret < 0)
		goto error;

	/* Register a V4L2 device. */
	ret = uvc_register_video(uvc);
	if (ret < 0) {
		printk(KERN_INFO "Unable to register video device\n");
		goto error;
	}

	return 0;

error:
	uvc_function_unbind(c, f);
	return ret;
}

/* --------------------------------------------------------------------------
 * USB gadget function
 */

/**
 * uvc_bind_config - add a UVC function to a configuration
 * @c: the configuration to support the UVC instance
 * Context: single threaded during gadget setup
 *
 * Returns zero on success, else negative errno.
 *
 * Caller must have called @uvc_setup(). Caller is also responsible for
 * calling @uvc_cleanup() before module unload.
 */
int __init
uvc_bind_config(struct usb_configuration *c,
		const struct uvc_descriptor_header * const *control,
		const struct uvc_descriptor_header * const *fs_streaming,
		const struct uvc_descriptor_header * const *hs_streaming)
{
	struct uvc_device *uvc;
	int ret = 0;

	/* TODO Check if the USB device controller supports the required
	 * features.
	 */
	if (!gadget_is_dualspeed(c->cdev->gadget))
		return -EINVAL;

	uvc = kzalloc(sizeof(*uvc), GFP_KERNEL);
	if (uvc == NULL)
		return -ENOMEM;

	uvc->state = UVC_STATE_DISCONNECTED;

	/* Validate the descriptors. */
	if (control == NULL || control[0] == NULL ||
	    control[0]->bDescriptorSubType != UVC_VC_HEADER)
		goto error;

	if (fs_streaming == NULL || fs_streaming[0] == NULL ||
	    fs_streaming[0]->bDescriptorSubType != UVC_VS_INPUT_HEADER)
		goto error;

	if (hs_streaming == NULL || hs_streaming[0] == NULL ||
	    hs_streaming[0]->bDescriptorSubType != UVC_VS_INPUT_HEADER)
		goto error;

	uvc->desc.control = control;
	uvc->desc.fs_streaming = fs_streaming;
	uvc->desc.hs_streaming = hs_streaming;

	/* Allocate string descriptor numbers. */
	if ((ret = usb_string_id(c->cdev)) < 0)
		goto error;
	uvc_en_us_strings[UVC_STRING_ASSOCIATION_IDX].id = ret;
	uvc_iad.iFunction = ret;

	if ((ret = usb_string_id(c->cdev)) < 0)
		goto error;
	uvc_en_us_strings[UVC_STRING_CONTROL_IDX].id = ret;
	uvc_control_intf.iInterface = ret;

	if ((ret = usb_string_id(c->cdev)) < 0)
		goto error;
	uvc_en_us_strings[UVC_STRING_STREAMING_IDX].id = ret;
	uvc_streaming_intf_alt0.iInterface = ret;
	uvc_streaming_intf_alt1.iInterface = ret;

	/* Register the function. */
	uvc->func.name = "uvc";
	uvc->func.strings = uvc_function_strings;
	uvc->func.bind = uvc_function_bind;
	uvc->func.unbind = uvc_function_unbind;
	uvc->func.get_alt = uvc_function_get_alt;
	uvc->func.set_alt = uvc_function_set_alt;
	uvc->func.disable = uvc_function_disable;
	uvc->func.setup = uvc_function_setup;

	ret = usb_add_function(c, &uvc->func);
	if (ret)
		kfree(uvc);

	return ret;

error:
	kfree(uvc);
	return ret;
}

module_param_named(trace, uvc_gadget_trace_param, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(trace, "Trace level bitmask");

