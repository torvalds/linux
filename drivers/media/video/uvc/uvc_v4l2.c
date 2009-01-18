/*
 *      uvc_v4l2.c  --  USB Video Class driver - V4L2 API
 *
 *      Copyright (C) 2005-2008
 *          Laurent Pinchart (laurent.pinchart@skynet.be)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include <asm/atomic.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

#include "uvcvideo.h"

/* ------------------------------------------------------------------------
 * V4L2 interface
 */

/*
 * Mapping V4L2 controls to UVC controls can be straighforward if done well.
 * Most of the UVC controls exist in V4L2, and can be mapped directly. Some
 * must be grouped (for instance the Red Balance, Blue Balance and Do White
 * Balance V4L2 controls use the White Balance Component UVC control) or
 * otherwise translated. The approach we take here is to use a translation
 * table for the controls which can be mapped directly, and handle the others
 * manually.
 */
static int uvc_v4l2_query_menu(struct uvc_video_device *video,
	struct v4l2_querymenu *query_menu)
{
	struct uvc_menu_info *menu_info;
	struct uvc_control_mapping *mapping;
	struct uvc_control *ctrl;

	ctrl = uvc_find_control(video, query_menu->id, &mapping);
	if (ctrl == NULL || mapping->v4l2_type != V4L2_CTRL_TYPE_MENU)
		return -EINVAL;

	if (query_menu->index >= mapping->menu_count)
		return -EINVAL;

	menu_info = &mapping->menu_info[query_menu->index];
	strncpy(query_menu->name, menu_info->name, 32);
	return 0;
}

/*
 * Find the frame interval closest to the requested frame interval for the
 * given frame format and size. This should be done by the device as part of
 * the Video Probe and Commit negotiation, but some hardware don't implement
 * that feature.
 */
static __u32 uvc_try_frame_interval(struct uvc_frame *frame, __u32 interval)
{
	unsigned int i;

	if (frame->bFrameIntervalType) {
		__u32 best = -1, dist;

		for (i = 0; i < frame->bFrameIntervalType; ++i) {
			dist = interval > frame->dwFrameInterval[i]
			     ? interval - frame->dwFrameInterval[i]
			     : frame->dwFrameInterval[i] - interval;

			if (dist > best)
				break;

			best = dist;
		}

		interval = frame->dwFrameInterval[i-1];
	} else {
		const __u32 min = frame->dwFrameInterval[0];
		const __u32 max = frame->dwFrameInterval[1];
		const __u32 step = frame->dwFrameInterval[2];

		interval = min + (interval - min + step/2) / step * step;
		if (interval > max)
			interval = max;
	}

	return interval;
}

static int uvc_v4l2_try_format(struct uvc_video_device *video,
	struct v4l2_format *fmt, struct uvc_streaming_control *probe,
	struct uvc_format **uvc_format, struct uvc_frame **uvc_frame)
{
	struct uvc_format *format = NULL;
	struct uvc_frame *frame = NULL;
	__u16 rw, rh;
	unsigned int d, maxd;
	unsigned int i;
	__u32 interval;
	int ret = 0;
	__u8 *fcc;

	if (fmt->type != video->streaming->type)
		return -EINVAL;

	fcc = (__u8 *)&fmt->fmt.pix.pixelformat;
	uvc_trace(UVC_TRACE_FORMAT, "Trying format 0x%08x (%c%c%c%c): %ux%u.\n",
			fmt->fmt.pix.pixelformat,
			fcc[0], fcc[1], fcc[2], fcc[3],
			fmt->fmt.pix.width, fmt->fmt.pix.height);

	/* Check if the hardware supports the requested format. */
	for (i = 0; i < video->streaming->nformats; ++i) {
		format = &video->streaming->format[i];
		if (format->fcc == fmt->fmt.pix.pixelformat)
			break;
	}

	if (format == NULL || format->fcc != fmt->fmt.pix.pixelformat) {
		uvc_trace(UVC_TRACE_FORMAT, "Unsupported format 0x%08x.\n",
				fmt->fmt.pix.pixelformat);
		return -EINVAL;
	}

	/* Find the closest image size. The distance between image sizes is
	 * the size in pixels of the non-overlapping regions between the
	 * requested size and the frame-specified size.
	 */
	rw = fmt->fmt.pix.width;
	rh = fmt->fmt.pix.height;
	maxd = (unsigned int)-1;

	for (i = 0; i < format->nframes; ++i) {
		__u16 w = format->frame[i].wWidth;
		__u16 h = format->frame[i].wHeight;

		d = min(w, rw) * min(h, rh);
		d = w*h + rw*rh - 2*d;
		if (d < maxd) {
			maxd = d;
			frame = &format->frame[i];
		}

		if (maxd == 0)
			break;
	}

	if (frame == NULL) {
		uvc_trace(UVC_TRACE_FORMAT, "Unsupported size %ux%u.\n",
				fmt->fmt.pix.width, fmt->fmt.pix.height);
		return -EINVAL;
	}

	/* Use the default frame interval. */
	interval = frame->dwDefaultFrameInterval;
	uvc_trace(UVC_TRACE_FORMAT, "Using default frame interval %u.%u us "
		"(%u.%u fps).\n", interval/10, interval%10, 10000000/interval,
		(100000000/interval)%10);

	/* Set the format index, frame index and frame interval. */
	memset(probe, 0, sizeof *probe);
	probe->bmHint = 1;	/* dwFrameInterval */
	probe->bFormatIndex = format->index;
	probe->bFrameIndex = frame->bFrameIndex;
	probe->dwFrameInterval = uvc_try_frame_interval(frame, interval);
	/* Some webcams stall the probe control set request when the
	 * dwMaxVideoFrameSize field is set to zero. The UVC specification
	 * clearly states that the field is read-only from the host, so this
	 * is a webcam bug. Set dwMaxVideoFrameSize to the value reported by
	 * the webcam to work around the problem.
	 *
	 * The workaround could probably be enabled for all webcams, so the
	 * quirk can be removed if needed. It's currently useful to detect
	 * webcam bugs and fix them before they hit the market (providing
	 * developers test their webcams with the Linux driver as well as with
	 * the Windows driver).
	 */
	if (video->dev->quirks & UVC_QUIRK_PROBE_EXTRAFIELDS)
		probe->dwMaxVideoFrameSize =
			video->streaming->ctrl.dwMaxVideoFrameSize;

	/* Probe the device */
	if ((ret = uvc_probe_video(video, probe)) < 0)
		goto done;

	fmt->fmt.pix.width = frame->wWidth;
	fmt->fmt.pix.height = frame->wHeight;
	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.bytesperline = format->bpp * frame->wWidth / 8;
	fmt->fmt.pix.sizeimage = probe->dwMaxVideoFrameSize;
	fmt->fmt.pix.colorspace = format->colorspace;
	fmt->fmt.pix.priv = 0;

	if (uvc_format != NULL)
		*uvc_format = format;
	if (uvc_frame != NULL)
		*uvc_frame = frame;

done:
	return ret;
}

static int uvc_v4l2_get_format(struct uvc_video_device *video,
	struct v4l2_format *fmt)
{
	struct uvc_format *format = video->streaming->cur_format;
	struct uvc_frame *frame = video->streaming->cur_frame;

	if (fmt->type != video->streaming->type)
		return -EINVAL;

	if (format == NULL || frame == NULL)
		return -EINVAL;

	fmt->fmt.pix.pixelformat = format->fcc;
	fmt->fmt.pix.width = frame->wWidth;
	fmt->fmt.pix.height = frame->wHeight;
	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.bytesperline = format->bpp * frame->wWidth / 8;
	fmt->fmt.pix.sizeimage = video->streaming->ctrl.dwMaxVideoFrameSize;
	fmt->fmt.pix.colorspace = format->colorspace;
	fmt->fmt.pix.priv = 0;

	return 0;
}

static int uvc_v4l2_set_format(struct uvc_video_device *video,
	struct v4l2_format *fmt)
{
	struct uvc_streaming_control probe;
	struct uvc_format *format;
	struct uvc_frame *frame;
	int ret;

	if (fmt->type != video->streaming->type)
		return -EINVAL;

	if (uvc_queue_streaming(&video->queue))
		return -EBUSY;

	ret = uvc_v4l2_try_format(video, fmt, &probe, &format, &frame);
	if (ret < 0)
		return ret;

	memcpy(&video->streaming->ctrl, &probe, sizeof probe);
	video->streaming->cur_format = format;
	video->streaming->cur_frame = frame;

	return 0;
}

static int uvc_v4l2_get_streamparm(struct uvc_video_device *video,
		struct v4l2_streamparm *parm)
{
	uint32_t numerator, denominator;

	if (parm->type != video->streaming->type)
		return -EINVAL;

	numerator = video->streaming->ctrl.dwFrameInterval;
	denominator = 10000000;
	uvc_simplify_fraction(&numerator, &denominator, 8, 333);

	memset(parm, 0, sizeof *parm);
	parm->type = video->streaming->type;

	if (video->streaming->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
		parm->parm.capture.capturemode = 0;
		parm->parm.capture.timeperframe.numerator = numerator;
		parm->parm.capture.timeperframe.denominator = denominator;
		parm->parm.capture.extendedmode = 0;
		parm->parm.capture.readbuffers = 0;
	} else {
		parm->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
		parm->parm.output.outputmode = 0;
		parm->parm.output.timeperframe.numerator = numerator;
		parm->parm.output.timeperframe.denominator = denominator;
	}

	return 0;
}

static int uvc_v4l2_set_streamparm(struct uvc_video_device *video,
		struct v4l2_streamparm *parm)
{
	struct uvc_frame *frame = video->streaming->cur_frame;
	struct uvc_streaming_control probe;
	struct v4l2_fract timeperframe;
	uint32_t interval;
	int ret;

	if (parm->type != video->streaming->type)
		return -EINVAL;

	if (uvc_queue_streaming(&video->queue))
		return -EBUSY;

	if (parm->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		timeperframe = parm->parm.capture.timeperframe;
	else
		timeperframe = parm->parm.output.timeperframe;

	memcpy(&probe, &video->streaming->ctrl, sizeof probe);
	interval = uvc_fraction_to_interval(timeperframe.numerator,
		timeperframe.denominator);

	uvc_trace(UVC_TRACE_FORMAT, "Setting frame interval to %u/%u (%u).\n",
		timeperframe.numerator, timeperframe.denominator, interval);
	probe.dwFrameInterval = uvc_try_frame_interval(frame, interval);

	/* Probe the device with the new settings. */
	if ((ret = uvc_probe_video(video, &probe)) < 0)
		return ret;

	memcpy(&video->streaming->ctrl, &probe, sizeof probe);

	/* Return the actual frame period. */
	timeperframe.numerator = probe.dwFrameInterval;
	timeperframe.denominator = 10000000;
	uvc_simplify_fraction(&timeperframe.numerator,
		&timeperframe.denominator, 8, 333);

	if (parm->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		parm->parm.capture.timeperframe = timeperframe;
	else
		parm->parm.output.timeperframe = timeperframe;

	return 0;
}

/* ------------------------------------------------------------------------
 * Privilege management
 */

/*
 * Privilege management is the multiple-open implementation basis. The current
 * implementation is completely transparent for the end-user and doesn't
 * require explicit use of the VIDIOC_G_PRIORITY and VIDIOC_S_PRIORITY ioctls.
 * Those ioctls enable finer control on the device (by making possible for a
 * user to request exclusive access to a device), but are not mature yet.
 * Switching to the V4L2 priority mechanism might be considered in the future
 * if this situation changes.
 *
 * Each open instance of a UVC device can either be in a privileged or
 * unprivileged state. Only a single instance can be in a privileged state at
 * a given time. Trying to perform an operation which requires privileges will
 * automatically acquire the required privileges if possible, or return -EBUSY
 * otherwise. Privileges are dismissed when closing the instance.
 *
 * Operations which require privileges are:
 *
 * - VIDIOC_S_INPUT
 * - VIDIOC_S_PARM
 * - VIDIOC_S_FMT
 * - VIDIOC_TRY_FMT
 * - VIDIOC_REQBUFS
 */
static int uvc_acquire_privileges(struct uvc_fh *handle)
{
	int ret = 0;

	/* Always succeed if the handle is already privileged. */
	if (handle->state == UVC_HANDLE_ACTIVE)
		return 0;

	/* Check if the device already has a privileged handle. */
	mutex_lock(&uvc_driver.open_mutex);
	if (atomic_inc_return(&handle->device->active) != 1) {
		atomic_dec(&handle->device->active);
		ret = -EBUSY;
		goto done;
	}

	handle->state = UVC_HANDLE_ACTIVE;

done:
	mutex_unlock(&uvc_driver.open_mutex);
	return ret;
}

static void uvc_dismiss_privileges(struct uvc_fh *handle)
{
	if (handle->state == UVC_HANDLE_ACTIVE)
		atomic_dec(&handle->device->active);

	handle->state = UVC_HANDLE_PASSIVE;
}

static int uvc_has_privileges(struct uvc_fh *handle)
{
	return handle->state == UVC_HANDLE_ACTIVE;
}

/* ------------------------------------------------------------------------
 * V4L2 file operations
 */

static int uvc_v4l2_open(struct file *file)
{
	struct uvc_video_device *video;
	struct uvc_fh *handle;
	int ret = 0;

	uvc_trace(UVC_TRACE_CALLS, "uvc_v4l2_open\n");
	mutex_lock(&uvc_driver.open_mutex);
	video = video_drvdata(file);

	if (video->dev->state & UVC_DEV_DISCONNECTED) {
		ret = -ENODEV;
		goto done;
	}

	ret = usb_autopm_get_interface(video->dev->intf);
	if (ret < 0)
		goto done;

	/* Create the device handle. */
	handle = kzalloc(sizeof *handle, GFP_KERNEL);
	if (handle == NULL) {
		usb_autopm_put_interface(video->dev->intf);
		ret = -ENOMEM;
		goto done;
	}

	handle->device = video;
	handle->state = UVC_HANDLE_PASSIVE;
	file->private_data = handle;

	kref_get(&video->dev->kref);

done:
	mutex_unlock(&uvc_driver.open_mutex);
	return ret;
}

static int uvc_v4l2_release(struct file *file)
{
	struct uvc_video_device *video = video_drvdata(file);
	struct uvc_fh *handle = (struct uvc_fh *)file->private_data;

	uvc_trace(UVC_TRACE_CALLS, "uvc_v4l2_release\n");

	/* Only free resources if this is a privileged handle. */
	if (uvc_has_privileges(handle)) {
		uvc_video_enable(video, 0);

		mutex_lock(&video->queue.mutex);
		if (uvc_free_buffers(&video->queue) < 0)
			uvc_printk(KERN_ERR, "uvc_v4l2_release: Unable to "
					"free buffers.\n");
		mutex_unlock(&video->queue.mutex);
	}

	/* Release the file handle. */
	uvc_dismiss_privileges(handle);
	kfree(handle);
	file->private_data = NULL;

	usb_autopm_put_interface(video->dev->intf);
	kref_put(&video->dev->kref, uvc_delete);
	return 0;
}

static long uvc_v4l2_do_ioctl(struct file *file, unsigned int cmd, void *arg)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_video_device *video = video_get_drvdata(vdev);
	struct uvc_fh *handle = (struct uvc_fh *)file->private_data;
	long ret = 0;

	switch (cmd) {
	/* Query capabilities */
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;

		memset(cap, 0, sizeof *cap);
		strncpy(cap->driver, "uvcvideo", sizeof cap->driver);
		strncpy(cap->card, vdev->name, 32);
		strncpy(cap->bus_info, video->dev->udev->bus->bus_name,
			sizeof cap->bus_info);
		cap->version = DRIVER_VERSION_NUMBER;
		if (video->streaming->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			cap->capabilities = V4L2_CAP_VIDEO_CAPTURE
					  | V4L2_CAP_STREAMING;
		else
			cap->capabilities = V4L2_CAP_VIDEO_OUTPUT
					  | V4L2_CAP_STREAMING;
		break;
	}

	/* Get, Set & Query control */
	case VIDIOC_QUERYCTRL:
		return uvc_query_v4l2_ctrl(video, arg);

	case VIDIOC_G_CTRL:
	{
		struct v4l2_control *ctrl = arg;
		struct v4l2_ext_control xctrl;

		memset(&xctrl, 0, sizeof xctrl);
		xctrl.id = ctrl->id;

		uvc_ctrl_begin(video);
		ret = uvc_ctrl_get(video, &xctrl);
		uvc_ctrl_rollback(video);
		if (ret >= 0)
			ctrl->value = xctrl.value;
		break;
	}

	case VIDIOC_S_CTRL:
	{
		struct v4l2_control *ctrl = arg;
		struct v4l2_ext_control xctrl;

		memset(&xctrl, 0, sizeof xctrl);
		xctrl.id = ctrl->id;
		xctrl.value = ctrl->value;

		uvc_ctrl_begin(video);
		ret = uvc_ctrl_set(video, &xctrl);
		if (ret < 0) {
			uvc_ctrl_rollback(video);
			return ret;
		}
		ret = uvc_ctrl_commit(video);
		break;
	}

	case VIDIOC_QUERYMENU:
		return uvc_v4l2_query_menu(video, arg);

	case VIDIOC_G_EXT_CTRLS:
	{
		struct v4l2_ext_controls *ctrls = arg;
		struct v4l2_ext_control *ctrl = ctrls->controls;
		unsigned int i;

		uvc_ctrl_begin(video);
		for (i = 0; i < ctrls->count; ++ctrl, ++i) {
			ret = uvc_ctrl_get(video, ctrl);
			if (ret < 0) {
				uvc_ctrl_rollback(video);
				ctrls->error_idx = i;
				return ret;
			}
		}
		ctrls->error_idx = 0;
		ret = uvc_ctrl_rollback(video);
		break;
	}

	case VIDIOC_S_EXT_CTRLS:
	case VIDIOC_TRY_EXT_CTRLS:
	{
		struct v4l2_ext_controls *ctrls = arg;
		struct v4l2_ext_control *ctrl = ctrls->controls;
		unsigned int i;

		ret = uvc_ctrl_begin(video);
		if (ret < 0)
			return ret;

		for (i = 0; i < ctrls->count; ++ctrl, ++i) {
			ret = uvc_ctrl_set(video, ctrl);
			if (ret < 0) {
				uvc_ctrl_rollback(video);
				ctrls->error_idx = i;
				return ret;
			}
		}

		ctrls->error_idx = 0;

		if (cmd == VIDIOC_S_EXT_CTRLS)
			ret = uvc_ctrl_commit(video);
		else
			ret = uvc_ctrl_rollback(video);
		break;
	}

	/* Get, Set & Enum input */
	case VIDIOC_ENUMINPUT:
	{
		const struct uvc_entity *selector = video->selector;
		struct v4l2_input *input = arg;
		struct uvc_entity *iterm = NULL;
		u32 index = input->index;
		int pin = 0;

		if (selector == NULL ||
		    (video->dev->quirks & UVC_QUIRK_IGNORE_SELECTOR_UNIT)) {
			if (index != 0)
				return -EINVAL;
			iterm = list_first_entry(&video->iterms,
					struct uvc_entity, chain);
			pin = iterm->id;
		} else if (pin < selector->selector.bNrInPins) {
			pin = selector->selector.baSourceID[index];
			list_for_each_entry(iterm, video->iterms.next, chain) {
				if (iterm->id == pin)
					break;
			}
		}

		if (iterm == NULL || iterm->id != pin)
			return -EINVAL;

		memset(input, 0, sizeof *input);
		input->index = index;
		strncpy(input->name, iterm->name, sizeof input->name);
		if (UVC_ENTITY_TYPE(iterm) == ITT_CAMERA)
			input->type = V4L2_INPUT_TYPE_CAMERA;
		break;
	}

	case VIDIOC_G_INPUT:
	{
		u8 input;

		if (video->selector == NULL ||
		    (video->dev->quirks & UVC_QUIRK_IGNORE_SELECTOR_UNIT)) {
			*(int *)arg = 0;
			break;
		}

		ret = uvc_query_ctrl(video->dev, GET_CUR, video->selector->id,
			video->dev->intfnum, SU_INPUT_SELECT_CONTROL,
			&input, 1);
		if (ret < 0)
			return ret;

		*(int *)arg = input - 1;
		break;
	}

	case VIDIOC_S_INPUT:
	{
		u8 input = *(u32 *)arg + 1;

		if ((ret = uvc_acquire_privileges(handle)) < 0)
			return ret;

		if (video->selector == NULL ||
		    (video->dev->quirks & UVC_QUIRK_IGNORE_SELECTOR_UNIT)) {
			if (input != 1)
				return -EINVAL;
			break;
		}

		if (input > video->selector->selector.bNrInPins)
			return -EINVAL;

		return uvc_query_ctrl(video->dev, SET_CUR, video->selector->id,
			video->dev->intfnum, SU_INPUT_SELECT_CONTROL,
			&input, 1);
	}

	/* Try, Get, Set & Enum format */
	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *fmt = arg;
		struct uvc_format *format;

		if (fmt->type != video->streaming->type ||
		    fmt->index >= video->streaming->nformats)
			return -EINVAL;

		format = &video->streaming->format[fmt->index];
		fmt->flags = 0;
		if (format->flags & UVC_FMT_FLAG_COMPRESSED)
			fmt->flags |= V4L2_FMT_FLAG_COMPRESSED;
		strncpy(fmt->description, format->name,
			sizeof fmt->description);
		fmt->description[sizeof fmt->description - 1] = 0;
		fmt->pixelformat = format->fcc;
		break;
	}

	case VIDIOC_TRY_FMT:
	{
		struct uvc_streaming_control probe;

		if ((ret = uvc_acquire_privileges(handle)) < 0)
			return ret;

		return uvc_v4l2_try_format(video, arg, &probe, NULL, NULL);
	}

	case VIDIOC_S_FMT:
		if ((ret = uvc_acquire_privileges(handle)) < 0)
			return ret;

		return uvc_v4l2_set_format(video, arg);

	case VIDIOC_G_FMT:
		return uvc_v4l2_get_format(video, arg);

	/* Frame size enumeration */
	case VIDIOC_ENUM_FRAMESIZES:
	{
		struct v4l2_frmsizeenum *fsize = arg;
		struct uvc_format *format = NULL;
		struct uvc_frame *frame;
		int i;

		/* Look for the given pixel format */
		for (i = 0; i < video->streaming->nformats; i++) {
			if (video->streaming->format[i].fcc ==
					fsize->pixel_format) {
				format = &video->streaming->format[i];
				break;
			}
		}
		if (format == NULL)
			return -EINVAL;

		if (fsize->index >= format->nframes)
			return -EINVAL;

		frame = &format->frame[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frame->wWidth;
		fsize->discrete.height = frame->wHeight;
		break;
	}

	/* Frame interval enumeration */
	case VIDIOC_ENUM_FRAMEINTERVALS:
	{
		struct v4l2_frmivalenum *fival = arg;
		struct uvc_format *format = NULL;
		struct uvc_frame *frame = NULL;
		int i;

		/* Look for the given pixel format and frame size */
		for (i = 0; i < video->streaming->nformats; i++) {
			if (video->streaming->format[i].fcc ==
					fival->pixel_format) {
				format = &video->streaming->format[i];
				break;
			}
		}
		if (format == NULL)
			return -EINVAL;

		for (i = 0; i < format->nframes; i++) {
			if (format->frame[i].wWidth == fival->width &&
			    format->frame[i].wHeight == fival->height) {
				frame = &format->frame[i];
				break;
			}
		}
		if (frame == NULL)
			return -EINVAL;

		if (frame->bFrameIntervalType) {
			if (fival->index >= frame->bFrameIntervalType)
				return -EINVAL;

			fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
			fival->discrete.numerator =
				frame->dwFrameInterval[fival->index];
			fival->discrete.denominator = 10000000;
			uvc_simplify_fraction(&fival->discrete.numerator,
				&fival->discrete.denominator, 8, 333);
		} else {
			fival->type = V4L2_FRMIVAL_TYPE_STEPWISE;
			fival->stepwise.min.numerator =
				frame->dwFrameInterval[0];
			fival->stepwise.min.denominator = 10000000;
			fival->stepwise.max.numerator =
				frame->dwFrameInterval[1];
			fival->stepwise.max.denominator = 10000000;
			fival->stepwise.step.numerator =
				frame->dwFrameInterval[2];
			fival->stepwise.step.denominator = 10000000;
			uvc_simplify_fraction(&fival->stepwise.min.numerator,
				&fival->stepwise.min.denominator, 8, 333);
			uvc_simplify_fraction(&fival->stepwise.max.numerator,
				&fival->stepwise.max.denominator, 8, 333);
			uvc_simplify_fraction(&fival->stepwise.step.numerator,
				&fival->stepwise.step.denominator, 8, 333);
		}
		break;
	}

	/* Get & Set streaming parameters */
	case VIDIOC_G_PARM:
		return uvc_v4l2_get_streamparm(video, arg);

	case VIDIOC_S_PARM:
		if ((ret = uvc_acquire_privileges(handle)) < 0)
			return ret;

		return uvc_v4l2_set_streamparm(video, arg);

	/* Cropping and scaling */
	case VIDIOC_CROPCAP:
	{
		struct v4l2_cropcap *ccap = arg;
		struct uvc_frame *frame = video->streaming->cur_frame;

		if (ccap->type != video->streaming->type)
			return -EINVAL;

		ccap->bounds.left = 0;
		ccap->bounds.top = 0;
		ccap->bounds.width = frame->wWidth;
		ccap->bounds.height = frame->wHeight;

		ccap->defrect = ccap->bounds;

		ccap->pixelaspect.numerator = 1;
		ccap->pixelaspect.denominator = 1;
		break;
	}

	case VIDIOC_G_CROP:
	case VIDIOC_S_CROP:
		return -EINVAL;

	/* Buffers & streaming */
	case VIDIOC_REQBUFS:
	{
		struct v4l2_requestbuffers *rb = arg;
		unsigned int bufsize =
			video->streaming->ctrl.dwMaxVideoFrameSize;

		if (rb->type != video->streaming->type ||
		    rb->memory != V4L2_MEMORY_MMAP)
			return -EINVAL;

		if ((ret = uvc_acquire_privileges(handle)) < 0)
			return ret;

		ret = uvc_alloc_buffers(&video->queue, rb->count, bufsize);
		if (ret < 0)
			return ret;

		rb->count = ret;
		ret = 0;
		break;
	}

	case VIDIOC_QUERYBUF:
	{
		struct v4l2_buffer *buf = arg;

		if (buf->type != video->streaming->type)
			return -EINVAL;

		if (!uvc_has_privileges(handle))
			return -EBUSY;

		return uvc_query_buffer(&video->queue, buf);
	}

	case VIDIOC_QBUF:
		if (!uvc_has_privileges(handle))
			return -EBUSY;

		return uvc_queue_buffer(&video->queue, arg);

	case VIDIOC_DQBUF:
		if (!uvc_has_privileges(handle))
			return -EBUSY;

		return uvc_dequeue_buffer(&video->queue, arg,
			file->f_flags & O_NONBLOCK);

	case VIDIOC_STREAMON:
	{
		int *type = arg;

		if (*type != video->streaming->type)
			return -EINVAL;

		if (!uvc_has_privileges(handle))
			return -EBUSY;

		if ((ret = uvc_video_enable(video, 1)) < 0)
			return ret;
		break;
	}

	case VIDIOC_STREAMOFF:
	{
		int *type = arg;

		if (*type != video->streaming->type)
			return -EINVAL;

		if (!uvc_has_privileges(handle))
			return -EBUSY;

		return uvc_video_enable(video, 0);
	}

	/* Analog video standards make no sense for digital cameras. */
	case VIDIOC_ENUMSTD:
	case VIDIOC_QUERYSTD:
	case VIDIOC_G_STD:
	case VIDIOC_S_STD:

	case VIDIOC_OVERLAY:

	case VIDIOC_ENUMAUDIO:
	case VIDIOC_ENUMAUDOUT:

	case VIDIOC_ENUMOUTPUT:
		uvc_trace(UVC_TRACE_IOCTL, "Unsupported ioctl 0x%08x\n", cmd);
		return -EINVAL;

	/* Dynamic controls. */
	case UVCIOC_CTRL_ADD:
	{
		struct uvc_xu_control_info *xinfo = arg;
		struct uvc_control_info *info;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		info = kzalloc(sizeof *info, GFP_KERNEL);
		if (info == NULL)
			return -ENOMEM;

		memcpy(info->entity, xinfo->entity, sizeof info->entity);
		info->index = xinfo->index;
		info->selector = xinfo->selector;
		info->size = xinfo->size;
		info->flags = xinfo->flags;

		info->flags |= UVC_CONTROL_GET_MIN | UVC_CONTROL_GET_MAX |
				UVC_CONTROL_GET_RES | UVC_CONTROL_GET_DEF;

		ret = uvc_ctrl_add_info(info);
		if (ret < 0)
			kfree(info);
		break;
	}

	case UVCIOC_CTRL_MAP:
	{
		struct uvc_xu_control_mapping *xmap = arg;
		struct uvc_control_mapping *map;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		map = kzalloc(sizeof *map, GFP_KERNEL);
		if (map == NULL)
			return -ENOMEM;

		map->id = xmap->id;
		memcpy(map->name, xmap->name, sizeof map->name);
		memcpy(map->entity, xmap->entity, sizeof map->entity);
		map->selector = xmap->selector;
		map->size = xmap->size;
		map->offset = xmap->offset;
		map->v4l2_type = xmap->v4l2_type;
		map->data_type = xmap->data_type;

		ret = uvc_ctrl_add_mapping(map);
		if (ret < 0)
			kfree(map);
		break;
	}

	case UVCIOC_CTRL_GET:
		return uvc_xu_ctrl_query(video, arg, 0);

	case UVCIOC_CTRL_SET:
		return uvc_xu_ctrl_query(video, arg, 1);

	default:
		if ((ret = v4l_compat_translate_ioctl(file, cmd, arg,
			uvc_v4l2_do_ioctl)) == -ENOIOCTLCMD)
			uvc_trace(UVC_TRACE_IOCTL, "Unknown ioctl 0x%08x\n",
				  cmd);
		return ret;
	}

	return ret;
}

static long uvc_v4l2_ioctl(struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	if (uvc_trace_param & UVC_TRACE_IOCTL) {
		uvc_printk(KERN_DEBUG, "uvc_v4l2_ioctl(");
		v4l_printk_ioctl(cmd);
		printk(")\n");
	}

	return video_usercopy(file, cmd, arg, uvc_v4l2_do_ioctl);
}

static ssize_t uvc_v4l2_read(struct file *file, char __user *data,
		    size_t count, loff_t *ppos)
{
	uvc_trace(UVC_TRACE_CALLS, "uvc_v4l2_read: not implemented.\n");
	return -ENODEV;
}

/*
 * VMA operations.
 */
static void uvc_vm_open(struct vm_area_struct *vma)
{
	struct uvc_buffer *buffer = vma->vm_private_data;
	buffer->vma_use_count++;
}

static void uvc_vm_close(struct vm_area_struct *vma)
{
	struct uvc_buffer *buffer = vma->vm_private_data;
	buffer->vma_use_count--;
}

static struct vm_operations_struct uvc_vm_ops = {
	.open		= uvc_vm_open,
	.close		= uvc_vm_close,
};

static int uvc_v4l2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct uvc_video_device *video = video_drvdata(file);
	struct uvc_buffer *uninitialized_var(buffer);
	struct page *page;
	unsigned long addr, start, size;
	unsigned int i;
	int ret = 0;

	uvc_trace(UVC_TRACE_CALLS, "uvc_v4l2_mmap\n");

	start = vma->vm_start;
	size = vma->vm_end - vma->vm_start;

	mutex_lock(&video->queue.mutex);

	for (i = 0; i < video->queue.count; ++i) {
		buffer = &video->queue.buffer[i];
		if ((buffer->buf.m.offset >> PAGE_SHIFT) == vma->vm_pgoff)
			break;
	}

	if (i == video->queue.count || size != video->queue.buf_size) {
		ret = -EINVAL;
		goto done;
	}

	/*
	 * VM_IO marks the area as being an mmaped region for I/O to a
	 * device. It also prevents the region from being core dumped.
	 */
	vma->vm_flags |= VM_IO;

	addr = (unsigned long)video->queue.mem + buffer->buf.m.offset;
	while (size > 0) {
		page = vmalloc_to_page((void *)addr);
		if ((ret = vm_insert_page(vma, start, page)) < 0)
			goto done;

		start += PAGE_SIZE;
		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	vma->vm_ops = &uvc_vm_ops;
	vma->vm_private_data = buffer;
	uvc_vm_open(vma);

done:
	mutex_unlock(&video->queue.mutex);
	return ret;
}

static unsigned int uvc_v4l2_poll(struct file *file, poll_table *wait)
{
	struct uvc_video_device *video = video_drvdata(file);

	uvc_trace(UVC_TRACE_CALLS, "uvc_v4l2_poll\n");

	return uvc_queue_poll(&video->queue, file, wait);
}

const struct v4l2_file_operations uvc_fops = {
	.owner		= THIS_MODULE,
	.open		= uvc_v4l2_open,
	.release	= uvc_v4l2_release,
	.ioctl		= uvc_v4l2_ioctl,
	.read		= uvc_v4l2_read,
	.mmap		= uvc_v4l2_mmap,
	.poll		= uvc_v4l2_poll,
};

