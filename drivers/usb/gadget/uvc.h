/*
 *	uvc_gadget.h  --  USB Video Class Gadget driver
 *
 *	Copyright (C) 2009-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#ifndef _UVC_GADGET_H_
#define _UVC_GADGET_H_

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/usb/ch9.h>

#define UVC_EVENT_FIRST			(V4L2_EVENT_PRIVATE_START + 0)
#define UVC_EVENT_CONNECT		(V4L2_EVENT_PRIVATE_START + 0)
#define UVC_EVENT_DISCONNECT		(V4L2_EVENT_PRIVATE_START + 1)
#define UVC_EVENT_STREAMON		(V4L2_EVENT_PRIVATE_START + 2)
#define UVC_EVENT_STREAMOFF		(V4L2_EVENT_PRIVATE_START + 3)
#define UVC_EVENT_SETUP			(V4L2_EVENT_PRIVATE_START + 4)
#define UVC_EVENT_DATA			(V4L2_EVENT_PRIVATE_START + 5)
#define UVC_EVENT_LAST			(V4L2_EVENT_PRIVATE_START + 5)

struct uvc_request_data
{
	unsigned int length;
	__u8 data[60];
};

struct uvc_event
{
	union {
		enum usb_device_speed speed;
		struct usb_ctrlrequest req;
		struct uvc_request_data data;
	};
};

#define UVCIOC_SEND_RESPONSE		_IOW('U', 1, struct uvc_request_data)

#define UVC_INTF_CONTROL		0
#define UVC_INTF_STREAMING		1

/* ------------------------------------------------------------------------
 * Debugging, printing and logging
 */

#ifdef __KERNEL__

#include <linux/usb.h>	/* For usb_endpoint_* */
#include <linux/usb/gadget.h>
#include <linux/videodev2.h>
#include <linux/version.h>
#include <media/v4l2-fh.h>

#include "uvc_queue.h"

#define UVC_TRACE_PROBE				(1 << 0)
#define UVC_TRACE_DESCR				(1 << 1)
#define UVC_TRACE_CONTROL			(1 << 2)
#define UVC_TRACE_FORMAT			(1 << 3)
#define UVC_TRACE_CAPTURE			(1 << 4)
#define UVC_TRACE_CALLS				(1 << 5)
#define UVC_TRACE_IOCTL				(1 << 6)
#define UVC_TRACE_FRAME				(1 << 7)
#define UVC_TRACE_SUSPEND			(1 << 8)
#define UVC_TRACE_STATUS			(1 << 9)

#define UVC_WARN_MINMAX				0
#define UVC_WARN_PROBE_DEF			1

extern unsigned int uvc_gadget_trace_param;

#define uvc_trace(flag, msg...) \
	do { \
		if (uvc_gadget_trace_param & flag) \
			printk(KERN_DEBUG "uvcvideo: " msg); \
	} while (0)

#define uvc_warn_once(dev, warn, msg...) \
	do { \
		if (!test_and_set_bit(warn, &dev->warnings)) \
			printk(KERN_INFO "uvcvideo: " msg); \
	} while (0)

#define uvc_printk(level, msg...) \
	printk(level "uvcvideo: " msg)

/* ------------------------------------------------------------------------
 * Driver specific constants
 */

#define DRIVER_VERSION				"0.1.0"
#define DRIVER_VERSION_NUMBER			KERNEL_VERSION(0, 1, 0)

#define DMA_ADDR_INVALID			(~(dma_addr_t)0)

#define UVC_NUM_REQUESTS			4
#define UVC_MAX_REQUEST_SIZE			64
#define UVC_MAX_EVENTS				4

/* ------------------------------------------------------------------------
 * Structures
 */

struct uvc_video
{
	struct usb_ep *ep;

	/* Frame parameters */
	u8 bpp;
	u32 fcc;
	unsigned int width;
	unsigned int height;
	unsigned int imagesize;

	/* Requests */
	unsigned int req_size;
	struct usb_request *req[UVC_NUM_REQUESTS];
	__u8 *req_buffer[UVC_NUM_REQUESTS];
	struct list_head req_free;
	spinlock_t req_lock;

	void (*encode) (struct usb_request *req, struct uvc_video *video,
			struct uvc_buffer *buf);

	/* Context data used by the completion handler */
	__u32 payload_size;
	__u32 max_payload_size;

	struct uvc_video_queue queue;
	unsigned int fid;
};

enum uvc_state
{
	UVC_STATE_DISCONNECTED,
	UVC_STATE_CONNECTED,
	UVC_STATE_STREAMING,
};

struct uvc_device
{
	struct video_device *vdev;
	enum uvc_state state;
	struct usb_function func;
	struct uvc_video video;

	/* Descriptors */
	struct {
		const struct uvc_descriptor_header * const *control;
		const struct uvc_descriptor_header * const *fs_streaming;
		const struct uvc_descriptor_header * const *hs_streaming;
	} desc;

	unsigned int control_intf;
	struct usb_ep *control_ep;
	struct usb_request *control_req;
	void *control_buf;

	unsigned int streaming_intf;

	/* Events */
	unsigned int event_length;
	unsigned int event_setup_out : 1;
};

static inline struct uvc_device *to_uvc(struct usb_function *f)
{
	return container_of(f, struct uvc_device, func);
}

struct uvc_file_handle
{
	struct v4l2_fh vfh;
	struct uvc_video *device;
};

#define to_uvc_file_handle(handle) \
	container_of(handle, struct uvc_file_handle, vfh)

/* ------------------------------------------------------------------------
 * Functions
 */

extern void uvc_endpoint_stream(struct uvc_device *dev);

extern void uvc_function_connect(struct uvc_device *uvc);
extern void uvc_function_disconnect(struct uvc_device *uvc);

#endif /* __KERNEL__ */

#endif /* _UVC_GADGET_H_ */

