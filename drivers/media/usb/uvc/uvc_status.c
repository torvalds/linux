// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      uvc_status.c  --  USB Video Class driver - Status endpoint
 *
 *      Copyright (C) 2005-2009
 *          Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <asm/barrier.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/input.h>

#include "uvcvideo.h"

/* --------------------------------------------------------------------------
 * Input device
 */
#ifdef CONFIG_USB_VIDEO_CLASS_INPUT_EVDEV

static bool uvc_input_has_button(struct uvc_device *dev)
{
	struct uvc_streaming *stream;

	/*
	 * The device has button events if both bTriggerSupport and
	 * bTriggerUsage are one. Otherwise the camera button does not
	 * exist or is handled automatically by the camera without host
	 * driver or client application intervention.
	 */
	list_for_each_entry(stream, &dev->streams, list) {
		if (stream->header.bTriggerSupport == 1 &&
		    stream->header.bTriggerUsage == 1)
			return true;
	}

	return false;
}

static int uvc_input_init(struct uvc_device *dev)
{
	struct input_dev *input;
	int ret;

	if (!uvc_input_has_button(dev))
		return 0;

	input = input_allocate_device();
	if (input == NULL)
		return -ENOMEM;

	usb_make_path(dev->udev, dev->input_phys, sizeof(dev->input_phys));
	strlcat(dev->input_phys, "/button", sizeof(dev->input_phys));

	input->name = dev->name;
	input->phys = dev->input_phys;
	usb_to_input_id(dev->udev, &input->id);
	input->dev.parent = &dev->intf->dev;

	__set_bit(EV_KEY, input->evbit);
	__set_bit(KEY_CAMERA, input->keybit);

	if ((ret = input_register_device(input)) < 0)
		goto error;

	dev->input = input;
	return 0;

error:
	input_free_device(input);
	return ret;
}

static void uvc_input_unregister(struct uvc_device *dev)
{
	if (dev->input)
		input_unregister_device(dev->input);
}

static void uvc_input_report_key(struct uvc_device *dev, unsigned int code,
	int value)
{
	if (dev->input) {
		input_report_key(dev->input, code, value);
		input_sync(dev->input);
	}
}

#else
#define uvc_input_init(dev)
#define uvc_input_unregister(dev)
#define uvc_input_report_key(dev, code, value)
#endif /* CONFIG_USB_VIDEO_CLASS_INPUT_EVDEV */

/* --------------------------------------------------------------------------
 * Status interrupt endpoint
 */
static void uvc_event_streaming(struct uvc_device *dev,
				struct uvc_status *status, int len)
{
	if (len <= offsetof(struct uvc_status, bEvent)) {
		uvc_dbg(dev, STATUS,
			"Invalid streaming status event received\n");
		return;
	}

	if (status->bEvent == 0) {
		if (len <= offsetof(struct uvc_status, streaming))
			return;

		uvc_dbg(dev, STATUS, "Button (intf %u) %s len %d\n",
			status->bOriginator,
			status->streaming.button ? "pressed" : "released", len);
		uvc_input_report_key(dev, KEY_CAMERA, status->streaming.button);
	} else {
		uvc_dbg(dev, STATUS, "Stream %u error event %02x len %d\n",
			status->bOriginator, status->bEvent, len);
	}
}

#define UVC_CTRL_VALUE_CHANGE	0
#define UVC_CTRL_INFO_CHANGE	1
#define UVC_CTRL_FAILURE_CHANGE	2
#define UVC_CTRL_MIN_CHANGE	3
#define UVC_CTRL_MAX_CHANGE	4

static struct uvc_control *uvc_event_entity_find_ctrl(struct uvc_entity *entity,
						      u8 selector)
{
	struct uvc_control *ctrl;
	unsigned int i;

	for (i = 0, ctrl = entity->controls; i < entity->ncontrols; i++, ctrl++)
		if (ctrl->info.selector == selector)
			return ctrl;

	return NULL;
}

static struct uvc_control *uvc_event_find_ctrl(struct uvc_device *dev,
					const struct uvc_status *status,
					struct uvc_video_chain **chain)
{
	list_for_each_entry((*chain), &dev->chains, list) {
		struct uvc_entity *entity;
		struct uvc_control *ctrl;

		list_for_each_entry(entity, &(*chain)->entities, chain) {
			if (entity->id != status->bOriginator)
				continue;

			ctrl = uvc_event_entity_find_ctrl(entity,
						     status->control.bSelector);
			if (ctrl)
				return ctrl;
		}
	}

	return NULL;
}

static bool uvc_event_control(struct urb *urb,
			      const struct uvc_status *status, int len)
{
	static const char *attrs[] = { "value", "info", "failure", "min", "max" };
	struct uvc_device *dev = urb->context;
	struct uvc_video_chain *chain;
	struct uvc_control *ctrl;

	if (len < 6 || status->bEvent != 0 ||
	    status->control.bAttribute >= ARRAY_SIZE(attrs)) {
		uvc_dbg(dev, STATUS, "Invalid control status event received\n");
		return false;
	}

	uvc_dbg(dev, STATUS, "Control %u/%u %s change len %d\n",
		status->bOriginator, status->control.bSelector,
		attrs[status->control.bAttribute], len);

	/* Find the control. */
	ctrl = uvc_event_find_ctrl(dev, status, &chain);
	if (!ctrl)
		return false;

	switch (status->control.bAttribute) {
	case UVC_CTRL_VALUE_CHANGE:
		return uvc_ctrl_status_event_async(urb, chain, ctrl,
						   status->control.bValue);

	case UVC_CTRL_INFO_CHANGE:
	case UVC_CTRL_FAILURE_CHANGE:
	case UVC_CTRL_MIN_CHANGE:
	case UVC_CTRL_MAX_CHANGE:
		break;
	}

	return false;
}

static void uvc_status_complete(struct urb *urb)
{
	struct uvc_device *dev = urb->context;
	int len, ret;

	switch (urb->status) {
	case 0:
		break;

	case -ENOENT:		/* usb_kill_urb() called. */
	case -ECONNRESET:	/* usb_unlink_urb() called. */
	case -ESHUTDOWN:	/* The endpoint is being disabled. */
	case -EPROTO:		/* Device is disconnected (reported by some host controllers). */
		return;

	default:
		dev_warn(&dev->udev->dev,
			 "Non-zero status (%d) in status completion handler.\n",
			 urb->status);
		return;
	}

	len = urb->actual_length;
	if (len > 0) {
		switch (dev->status->bStatusType & 0x0f) {
		case UVC_STATUS_TYPE_CONTROL: {
			if (uvc_event_control(urb, dev->status, len))
				/* The URB will be resubmitted in work context. */
				return;
			break;
		}

		case UVC_STATUS_TYPE_STREAMING: {
			uvc_event_streaming(dev, dev->status, len);
			break;
		}

		default:
			uvc_dbg(dev, STATUS, "Unknown status event type %u\n",
				dev->status->bStatusType);
			break;
		}
	}

	/* Resubmit the URB. */
	urb->interval = dev->int_ep->desc.bInterval;
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret < 0)
		dev_err(&dev->udev->dev,
			"Failed to resubmit status URB (%d).\n", ret);
}

int uvc_status_init(struct uvc_device *dev)
{
	struct usb_host_endpoint *ep = dev->int_ep;
	unsigned int pipe;
	int interval;

	mutex_init(&dev->status_lock);

	if (ep == NULL)
		return 0;

	uvc_input_init(dev);

	dev->status = kzalloc(sizeof(*dev->status), GFP_KERNEL);
	if (!dev->status)
		return -ENOMEM;

	dev->int_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->int_urb) {
		kfree(dev->status);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(dev->udev, ep->desc.bEndpointAddress);

	/*
	 * For high-speed interrupt endpoints, the bInterval value is used as
	 * an exponent of two. Some developers forgot about it.
	 */
	interval = ep->desc.bInterval;
	if (interval > 16 && dev->udev->speed == USB_SPEED_HIGH &&
	    (dev->quirks & UVC_QUIRK_STATUS_INTERVAL))
		interval = fls(interval) - 1;

	usb_fill_int_urb(dev->int_urb, dev->udev, pipe,
		dev->status, sizeof(*dev->status), uvc_status_complete,
		dev, interval);

	return 0;
}

void uvc_status_unregister(struct uvc_device *dev)
{
	uvc_status_suspend(dev);
	uvc_input_unregister(dev);
}

void uvc_status_cleanup(struct uvc_device *dev)
{
	usb_free_urb(dev->int_urb);
	kfree(dev->status);
}

static int uvc_status_start(struct uvc_device *dev, gfp_t flags)
{
	lockdep_assert_held(&dev->status_lock);

	if (!dev->int_urb)
		return 0;

	return usb_submit_urb(dev->int_urb, flags);
}

static void uvc_status_stop(struct uvc_device *dev)
{
	struct uvc_ctrl_work *w = &dev->async_ctrl;

	lockdep_assert_held(&dev->status_lock);

	if (!dev->int_urb)
		return;

	/*
	 * Prevent the asynchronous control handler from requeing the URB. The
	 * barrier is needed so the flush_status change is visible to other
	 * CPUs running the asynchronous handler before usb_kill_urb() is
	 * called below.
	 */
	smp_store_release(&dev->flush_status, true);

	/*
	 * Cancel any pending asynchronous work. If any status event was queued,
	 * process it synchronously.
	 */
	if (cancel_work_sync(&w->work))
		uvc_ctrl_status_event(w->chain, w->ctrl, w->data);

	/* Kill the urb. */
	usb_kill_urb(dev->int_urb);

	/*
	 * The URB completion handler may have queued asynchronous work. This
	 * won't resubmit the URB as flush_status is set, but it needs to be
	 * cancelled before returning or it could then race with a future
	 * uvc_status_start() call.
	 */
	if (cancel_work_sync(&w->work))
		uvc_ctrl_status_event(w->chain, w->ctrl, w->data);

	/*
	 * From this point, there are no events on the queue and the status URB
	 * is dead. No events will be queued until uvc_status_start() is called.
	 * The barrier is needed to make sure that flush_status is visible to
	 * uvc_ctrl_status_event_work() when uvc_status_start() will be called
	 * again.
	 */
	smp_store_release(&dev->flush_status, false);
}

int uvc_status_resume(struct uvc_device *dev)
{
	guard(mutex)(&dev->status_lock);

	if (dev->status_users)
		return uvc_status_start(dev, GFP_NOIO);

	return 0;
}

void uvc_status_suspend(struct uvc_device *dev)
{
	guard(mutex)(&dev->status_lock);

	if (dev->status_users)
		uvc_status_stop(dev);
}

int uvc_status_get(struct uvc_device *dev)
{
	int ret;

	guard(mutex)(&dev->status_lock);

	if (!dev->status_users) {
		ret = uvc_status_start(dev, GFP_KERNEL);
		if (ret)
			return ret;
	}

	dev->status_users++;

	return 0;
}

void uvc_status_put(struct uvc_device *dev)
{
	guard(mutex)(&dev->status_lock);

	if (dev->status_users == 1)
		uvc_status_stop(dev);
	WARN_ON(!dev->status_users);
	if (dev->status_users)
		dev->status_users--;
}
