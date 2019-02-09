/*
 *      uvc_v4l2.c  --  USB Video Class driver - V4L2 API
 *
 *      Copyright (C) 2005-2010
 *          Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 */

#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include <linux/atomic.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "uvcvideo.h"

/* ------------------------------------------------------------------------
 * UVC ioctls
 */
static int uvc_ioctl_ctrl_map(struct uvc_video_chain *chain,
	struct uvc_xu_control_mapping *xmap)
{
	struct uvc_control_mapping *map;
	unsigned int size;
	int ret;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (map == NULL)
		return -ENOMEM;

	map->id = xmap->id;
	memcpy(map->name, xmap->name, sizeof(map->name));
	memcpy(map->entity, xmap->entity, sizeof(map->entity));
	map->selector = xmap->selector;
	map->size = xmap->size;
	map->offset = xmap->offset;
	map->v4l2_type = xmap->v4l2_type;
	map->data_type = xmap->data_type;

	switch (xmap->v4l2_type) {
	case V4L2_CTRL_TYPE_INTEGER:
	case V4L2_CTRL_TYPE_BOOLEAN:
	case V4L2_CTRL_TYPE_BUTTON:
		break;

	case V4L2_CTRL_TYPE_MENU:
		/* Prevent excessive memory consumption, as well as integer
		 * overflows.
		 */
		if (xmap->menu_count == 0 ||
		    xmap->menu_count > UVC_MAX_CONTROL_MENU_ENTRIES) {
			ret = -EINVAL;
			goto free_map;
		}

		size = xmap->menu_count * sizeof(*map->menu_info);
		map->menu_info = memdup_user(xmap->menu_info, size);
		if (IS_ERR(map->menu_info)) {
			ret = PTR_ERR(map->menu_info);
			goto free_map;
		}

		map->menu_count = xmap->menu_count;
		break;

	default:
		uvc_trace(UVC_TRACE_CONTROL, "Unsupported V4L2 control type "
			  "%u.\n", xmap->v4l2_type);
		ret = -ENOTTY;
		goto free_map;
	}

	ret = uvc_ctrl_add_mapping(chain, map);

	kfree(map->menu_info);
free_map:
	kfree(map);

	return ret;
}

/* ------------------------------------------------------------------------
 * V4L2 interface
 */

/*
 * Find the frame interval closest to the requested frame interval for the
 * given frame format and size. This should be done by the device as part of
 * the Video Probe and Commit negotiation, but some hardware don't implement
 * that feature.
 */
static u32 uvc_try_frame_interval(struct uvc_frame *frame, u32 interval)
{
	unsigned int i;

	if (frame->bFrameIntervalType) {
		u32 best = -1, dist;

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
		const u32 min = frame->dwFrameInterval[0];
		const u32 max = frame->dwFrameInterval[1];
		const u32 step = frame->dwFrameInterval[2];

		interval = min + (interval - min + step/2) / step * step;
		if (interval > max)
			interval = max;
	}

	return interval;
}

static u32 uvc_v4l2_get_bytesperline(const struct uvc_format *format,
	const struct uvc_frame *frame)
{
	switch (format->fcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_M420:
		return frame->wWidth;

	default:
		return format->bpp * frame->wWidth / 8;
	}
}

static int uvc_v4l2_try_format(struct uvc_streaming *stream,
	struct v4l2_format *fmt, struct uvc_streaming_control *probe,
	struct uvc_format **uvc_format, struct uvc_frame **uvc_frame)
{
	struct uvc_format *format = NULL;
	struct uvc_frame *frame = NULL;
	u16 rw, rh;
	unsigned int d, maxd;
	unsigned int i;
	u32 interval;
	int ret = 0;
	u8 *fcc;

	if (fmt->type != stream->type)
		return -EINVAL;

	fcc = (u8 *)&fmt->fmt.pix.pixelformat;
	uvc_trace(UVC_TRACE_FORMAT, "Trying format 0x%08x (%c%c%c%c): %ux%u.\n",
			fmt->fmt.pix.pixelformat,
			fcc[0], fcc[1], fcc[2], fcc[3],
			fmt->fmt.pix.width, fmt->fmt.pix.height);

	/* Check if the hardware supports the requested format, use the default
	 * format otherwise.
	 */
	for (i = 0; i < stream->nformats; ++i) {
		format = &stream->format[i];
		if (format->fcc == fmt->fmt.pix.pixelformat)
			break;
	}

	if (i == stream->nformats) {
		format = stream->def_format;
		fmt->fmt.pix.pixelformat = format->fcc;
	}

	/* Find the closest image size. The distance between image sizes is
	 * the size in pixels of the non-overlapping regions between the
	 * requested size and the frame-specified size.
	 */
	rw = fmt->fmt.pix.width;
	rh = fmt->fmt.pix.height;
	maxd = (unsigned int)-1;

	for (i = 0; i < format->nframes; ++i) {
		u16 w = format->frame[i].wWidth;
		u16 h = format->frame[i].wHeight;

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
	memset(probe, 0, sizeof(*probe));
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
	mutex_lock(&stream->mutex);
	if (stream->dev->quirks & UVC_QUIRK_PROBE_EXTRAFIELDS)
		probe->dwMaxVideoFrameSize =
			stream->ctrl.dwMaxVideoFrameSize;

	/* Probe the device. */
	ret = uvc_probe_video(stream, probe);
	mutex_unlock(&stream->mutex);
	if (ret < 0)
		goto done;

	fmt->fmt.pix.width = frame->wWidth;
	fmt->fmt.pix.height = frame->wHeight;
	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.bytesperline = uvc_v4l2_get_bytesperline(format, frame);
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

static int uvc_v4l2_get_format(struct uvc_streaming *stream,
	struct v4l2_format *fmt)
{
	struct uvc_format *format;
	struct uvc_frame *frame;
	int ret = 0;

	if (fmt->type != stream->type)
		return -EINVAL;

	mutex_lock(&stream->mutex);
	format = stream->cur_format;
	frame = stream->cur_frame;

	if (format == NULL || frame == NULL) {
		ret = -EINVAL;
		goto done;
	}

	fmt->fmt.pix.pixelformat = format->fcc;
	fmt->fmt.pix.width = frame->wWidth;
	fmt->fmt.pix.height = frame->wHeight;
	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.bytesperline = uvc_v4l2_get_bytesperline(format, frame);
	fmt->fmt.pix.sizeimage = stream->ctrl.dwMaxVideoFrameSize;
	fmt->fmt.pix.colorspace = format->colorspace;
	fmt->fmt.pix.priv = 0;

done:
	mutex_unlock(&stream->mutex);
	return ret;
}

static int uvc_v4l2_set_format(struct uvc_streaming *stream,
	struct v4l2_format *fmt)
{
	struct uvc_streaming_control probe;
	struct uvc_format *format;
	struct uvc_frame *frame;
	int ret;

	if (fmt->type != stream->type)
		return -EINVAL;

	ret = uvc_v4l2_try_format(stream, fmt, &probe, &format, &frame);
	if (ret < 0)
		return ret;

	mutex_lock(&stream->mutex);

	if (uvc_queue_allocated(&stream->queue)) {
		ret = -EBUSY;
		goto done;
	}

	stream->ctrl = probe;
	stream->cur_format = format;
	stream->cur_frame = frame;

done:
	mutex_unlock(&stream->mutex);
	return ret;
}

static int uvc_v4l2_get_streamparm(struct uvc_streaming *stream,
		struct v4l2_streamparm *parm)
{
	u32 numerator, denominator;

	if (parm->type != stream->type)
		return -EINVAL;

	mutex_lock(&stream->mutex);
	numerator = stream->ctrl.dwFrameInterval;
	mutex_unlock(&stream->mutex);

	denominator = 10000000;
	uvc_simplify_fraction(&numerator, &denominator, 8, 333);

	memset(parm, 0, sizeof(*parm));
	parm->type = stream->type;

	if (stream->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
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

static int uvc_v4l2_set_streamparm(struct uvc_streaming *stream,
		struct v4l2_streamparm *parm)
{
	struct uvc_streaming_control probe;
	struct v4l2_fract timeperframe;
	struct uvc_format *format;
	struct uvc_frame *frame;
	u32 interval, maxd;
	unsigned int i;
	int ret;

	if (parm->type != stream->type)
		return -EINVAL;

	if (parm->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		timeperframe = parm->parm.capture.timeperframe;
	else
		timeperframe = parm->parm.output.timeperframe;

	interval = uvc_fraction_to_interval(timeperframe.numerator,
		timeperframe.denominator);
	uvc_trace(UVC_TRACE_FORMAT, "Setting frame interval to %u/%u (%u).\n",
		timeperframe.numerator, timeperframe.denominator, interval);

	mutex_lock(&stream->mutex);

	if (uvc_queue_streaming(&stream->queue)) {
		mutex_unlock(&stream->mutex);
		return -EBUSY;
	}

	format = stream->cur_format;
	frame = stream->cur_frame;
	probe = stream->ctrl;
	probe.dwFrameInterval = uvc_try_frame_interval(frame, interval);
	maxd = abs((s32)probe.dwFrameInterval - interval);

	/* Try frames with matching size to find the best frame interval. */
	for (i = 0; i < format->nframes && maxd != 0; i++) {
		u32 d, ival;

		if (&format->frame[i] == stream->cur_frame)
			continue;

		if (format->frame[i].wWidth != stream->cur_frame->wWidth ||
		    format->frame[i].wHeight != stream->cur_frame->wHeight)
			continue;

		ival = uvc_try_frame_interval(&format->frame[i], interval);
		d = abs((s32)ival - interval);
		if (d >= maxd)
			continue;

		frame = &format->frame[i];
		probe.bFrameIndex = frame->bFrameIndex;
		probe.dwFrameInterval = ival;
		maxd = d;
	}

	/* Probe the device with the new settings. */
	ret = uvc_probe_video(stream, &probe);
	if (ret < 0) {
		mutex_unlock(&stream->mutex);
		return ret;
	}

	stream->ctrl = probe;
	stream->cur_frame = frame;
	mutex_unlock(&stream->mutex);

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
 * a given time. Trying to perform an operation that requires privileges will
 * automatically acquire the required privileges if possible, or return -EBUSY
 * otherwise. Privileges are dismissed when closing the instance or when
 * freeing the video buffers using VIDIOC_REQBUFS.
 *
 * Operations that require privileges are:
 *
 * - VIDIOC_S_INPUT
 * - VIDIOC_S_PARM
 * - VIDIOC_S_FMT
 * - VIDIOC_REQBUFS
 */
static int uvc_acquire_privileges(struct uvc_fh *handle)
{
	/* Always succeed if the handle is already privileged. */
	if (handle->state == UVC_HANDLE_ACTIVE)
		return 0;

	/* Check if the device already has a privileged handle. */
	if (atomic_inc_return(&handle->stream->active) != 1) {
		atomic_dec(&handle->stream->active);
		return -EBUSY;
	}

	handle->state = UVC_HANDLE_ACTIVE;
	return 0;
}

static void uvc_dismiss_privileges(struct uvc_fh *handle)
{
	if (handle->state == UVC_HANDLE_ACTIVE)
		atomic_dec(&handle->stream->active);

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
	struct uvc_streaming *stream;
	struct uvc_fh *handle;
	int ret = 0;

	uvc_trace(UVC_TRACE_CALLS, "uvc_v4l2_open\n");
	stream = video_drvdata(file);

	ret = usb_autopm_get_interface(stream->dev->intf);
	if (ret < 0)
		return ret;

	/* Create the device handle. */
	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (handle == NULL) {
		usb_autopm_put_interface(stream->dev->intf);
		return -ENOMEM;
	}

	mutex_lock(&stream->dev->lock);
	if (stream->dev->users == 0) {
		ret = uvc_status_start(stream->dev, GFP_KERNEL);
		if (ret < 0) {
			mutex_unlock(&stream->dev->lock);
			usb_autopm_put_interface(stream->dev->intf);
			kfree(handle);
			return ret;
		}
	}

	stream->dev->users++;
	mutex_unlock(&stream->dev->lock);

	v4l2_fh_init(&handle->vfh, &stream->vdev);
	v4l2_fh_add(&handle->vfh);
	handle->chain = stream->chain;
	handle->stream = stream;
	handle->state = UVC_HANDLE_PASSIVE;
	file->private_data = handle;

	return 0;
}

static int uvc_v4l2_release(struct file *file)
{
	struct uvc_fh *handle = file->private_data;
	struct uvc_streaming *stream = handle->stream;

	uvc_trace(UVC_TRACE_CALLS, "uvc_v4l2_release\n");

	/* Only free resources if this is a privileged handle. */
	if (uvc_has_privileges(handle))
		uvc_queue_release(&stream->queue);

	/* Release the file handle. */
	uvc_dismiss_privileges(handle);
	v4l2_fh_del(&handle->vfh);
	v4l2_fh_exit(&handle->vfh);
	kfree(handle);
	file->private_data = NULL;

	mutex_lock(&stream->dev->lock);
	if (--stream->dev->users == 0)
		uvc_status_stop(stream->dev);
	mutex_unlock(&stream->dev->lock);

	usb_autopm_put_interface(stream->dev->intf);
	return 0;
}

static int uvc_ioctl_querycap(struct file *file, void *fh,
			      struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_fh *handle = file->private_data;
	struct uvc_video_chain *chain = handle->chain;
	struct uvc_streaming *stream = handle->stream;

	strlcpy(cap->driver, "uvcvideo", sizeof(cap->driver));
	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	usb_make_path(stream->dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->capabilities = V4L2_CAP_DEVICE_CAPS | V4L2_CAP_STREAMING
			  | chain->caps;

	return 0;
}

static int uvc_ioctl_enum_fmt(struct uvc_streaming *stream,
			      struct v4l2_fmtdesc *fmt)
{
	struct uvc_format *format;
	enum v4l2_buf_type type = fmt->type;
	u32 index = fmt->index;

	if (fmt->type != stream->type || fmt->index >= stream->nformats)
		return -EINVAL;

	memset(fmt, 0, sizeof(*fmt));
	fmt->index = index;
	fmt->type = type;

	format = &stream->format[fmt->index];
	fmt->flags = 0;
	if (format->flags & UVC_FMT_FLAG_COMPRESSED)
		fmt->flags |= V4L2_FMT_FLAG_COMPRESSED;
	strlcpy(fmt->description, format->name, sizeof(fmt->description));
	fmt->description[sizeof(fmt->description) - 1] = 0;
	fmt->pixelformat = format->fcc;
	return 0;
}

static int uvc_ioctl_enum_fmt_vid_cap(struct file *file, void *fh,
				      struct v4l2_fmtdesc *fmt)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;

	return uvc_ioctl_enum_fmt(stream, fmt);
}

static int uvc_ioctl_enum_fmt_vid_out(struct file *file, void *fh,
				      struct v4l2_fmtdesc *fmt)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;

	return uvc_ioctl_enum_fmt(stream, fmt);
}

static int uvc_ioctl_g_fmt_vid_cap(struct file *file, void *fh,
				   struct v4l2_format *fmt)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;

	return uvc_v4l2_get_format(stream, fmt);
}

static int uvc_ioctl_g_fmt_vid_out(struct file *file, void *fh,
				   struct v4l2_format *fmt)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;

	return uvc_v4l2_get_format(stream, fmt);
}

static int uvc_ioctl_s_fmt_vid_cap(struct file *file, void *fh,
				   struct v4l2_format *fmt)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;
	int ret;

	ret = uvc_acquire_privileges(handle);
	if (ret < 0)
		return ret;

	return uvc_v4l2_set_format(stream, fmt);
}

static int uvc_ioctl_s_fmt_vid_out(struct file *file, void *fh,
				   struct v4l2_format *fmt)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;
	int ret;

	ret = uvc_acquire_privileges(handle);
	if (ret < 0)
		return ret;

	return uvc_v4l2_set_format(stream, fmt);
}

static int uvc_ioctl_try_fmt_vid_cap(struct file *file, void *fh,
				     struct v4l2_format *fmt)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;
	struct uvc_streaming_control probe;

	return uvc_v4l2_try_format(stream, fmt, &probe, NULL, NULL);
}

static int uvc_ioctl_try_fmt_vid_out(struct file *file, void *fh,
				     struct v4l2_format *fmt)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;
	struct uvc_streaming_control probe;

	return uvc_v4l2_try_format(stream, fmt, &probe, NULL, NULL);
}

static int uvc_ioctl_reqbufs(struct file *file, void *fh,
			     struct v4l2_requestbuffers *rb)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;
	int ret;

	ret = uvc_acquire_privileges(handle);
	if (ret < 0)
		return ret;

	mutex_lock(&stream->mutex);
	ret = uvc_request_buffers(&stream->queue, rb);
	mutex_unlock(&stream->mutex);
	if (ret < 0)
		return ret;

	if (ret == 0)
		uvc_dismiss_privileges(handle);

	return 0;
}

static int uvc_ioctl_querybuf(struct file *file, void *fh,
			      struct v4l2_buffer *buf)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;

	if (!uvc_has_privileges(handle))
		return -EBUSY;

	return uvc_query_buffer(&stream->queue, buf);
}

static int uvc_ioctl_qbuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;

	if (!uvc_has_privileges(handle))
		return -EBUSY;

	return uvc_queue_buffer(&stream->queue, buf);
}

static int uvc_ioctl_expbuf(struct file *file, void *fh,
			    struct v4l2_exportbuffer *exp)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;

	if (!uvc_has_privileges(handle))
		return -EBUSY;

	return uvc_export_buffer(&stream->queue, exp);
}

static int uvc_ioctl_dqbuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;

	if (!uvc_has_privileges(handle))
		return -EBUSY;

	return uvc_dequeue_buffer(&stream->queue, buf,
				  file->f_flags & O_NONBLOCK);
}

static int uvc_ioctl_create_bufs(struct file *file, void *fh,
				  struct v4l2_create_buffers *cb)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;
	int ret;

	ret = uvc_acquire_privileges(handle);
	if (ret < 0)
		return ret;

	return uvc_create_buffers(&stream->queue, cb);
}

static int uvc_ioctl_streamon(struct file *file, void *fh,
			      enum v4l2_buf_type type)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;
	int ret;

	if (!uvc_has_privileges(handle))
		return -EBUSY;

	mutex_lock(&stream->mutex);
	ret = uvc_queue_streamon(&stream->queue, type);
	mutex_unlock(&stream->mutex);

	return ret;
}

static int uvc_ioctl_streamoff(struct file *file, void *fh,
			       enum v4l2_buf_type type)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;

	if (!uvc_has_privileges(handle))
		return -EBUSY;

	mutex_lock(&stream->mutex);
	uvc_queue_streamoff(&stream->queue, type);
	mutex_unlock(&stream->mutex);

	return 0;
}

static int uvc_ioctl_enum_input(struct file *file, void *fh,
				struct v4l2_input *input)
{
	struct uvc_fh *handle = fh;
	struct uvc_video_chain *chain = handle->chain;
	const struct uvc_entity *selector = chain->selector;
	struct uvc_entity *iterm = NULL;
	u32 index = input->index;
	int pin = 0;

	if (selector == NULL ||
	    (chain->dev->quirks & UVC_QUIRK_IGNORE_SELECTOR_UNIT)) {
		if (index != 0)
			return -EINVAL;
		list_for_each_entry(iterm, &chain->entities, chain) {
			if (UVC_ENTITY_IS_ITERM(iterm))
				break;
		}
		pin = iterm->id;
	} else if (index < selector->bNrInPins) {
		pin = selector->baSourceID[index];
		list_for_each_entry(iterm, &chain->entities, chain) {
			if (!UVC_ENTITY_IS_ITERM(iterm))
				continue;
			if (iterm->id == pin)
				break;
		}
	}

	if (iterm == NULL || iterm->id != pin)
		return -EINVAL;

	memset(input, 0, sizeof(*input));
	input->index = index;
	strlcpy(input->name, iterm->name, sizeof(input->name));
	if (UVC_ENTITY_TYPE(iterm) == UVC_ITT_CAMERA)
		input->type = V4L2_INPUT_TYPE_CAMERA;

	return 0;
}

static int uvc_ioctl_g_input(struct file *file, void *fh, unsigned int *input)
{
	struct uvc_fh *handle = fh;
	struct uvc_video_chain *chain = handle->chain;
	int ret;
	u8 i;

	if (chain->selector == NULL ||
	    (chain->dev->quirks & UVC_QUIRK_IGNORE_SELECTOR_UNIT)) {
		*input = 0;
		return 0;
	}

	ret = uvc_query_ctrl(chain->dev, UVC_GET_CUR, chain->selector->id,
			     chain->dev->intfnum,  UVC_SU_INPUT_SELECT_CONTROL,
			     &i, 1);
	if (ret < 0)
		return ret;

	*input = i - 1;
	return 0;
}

static int uvc_ioctl_s_input(struct file *file, void *fh, unsigned int input)
{
	struct uvc_fh *handle = fh;
	struct uvc_video_chain *chain = handle->chain;
	int ret;
	u32 i;

	ret = uvc_acquire_privileges(handle);
	if (ret < 0)
		return ret;

	if (chain->selector == NULL ||
	    (chain->dev->quirks & UVC_QUIRK_IGNORE_SELECTOR_UNIT)) {
		if (input)
			return -EINVAL;
		return 0;
	}

	if (input >= chain->selector->bNrInPins)
		return -EINVAL;

	i = input + 1;
	return uvc_query_ctrl(chain->dev, UVC_SET_CUR, chain->selector->id,
			      chain->dev->intfnum, UVC_SU_INPUT_SELECT_CONTROL,
			      &i, 1);
}

static int uvc_ioctl_queryctrl(struct file *file, void *fh,
			       struct v4l2_queryctrl *qc)
{
	struct uvc_fh *handle = fh;
	struct uvc_video_chain *chain = handle->chain;

	return uvc_query_v4l2_ctrl(chain, qc);
}

static int uvc_ioctl_query_ext_ctrl(struct file *file, void *fh,
				    struct v4l2_query_ext_ctrl *qec)
{
	struct uvc_fh *handle = fh;
	struct uvc_video_chain *chain = handle->chain;
	struct v4l2_queryctrl qc = { qec->id };
	int ret;

	ret = uvc_query_v4l2_ctrl(chain, &qc);
	if (ret)
		return ret;

	qec->id = qc.id;
	qec->type = qc.type;
	strlcpy(qec->name, qc.name, sizeof(qec->name));
	qec->minimum = qc.minimum;
	qec->maximum = qc.maximum;
	qec->step = qc.step;
	qec->default_value = qc.default_value;
	qec->flags = qc.flags;
	qec->elem_size = 4;
	qec->elems = 1;
	qec->nr_of_dims = 0;
	memset(qec->dims, 0, sizeof(qec->dims));
	memset(qec->reserved, 0, sizeof(qec->reserved));

	return 0;
}

static int uvc_ioctl_g_ctrl(struct file *file, void *fh,
			    struct v4l2_control *ctrl)
{
	struct uvc_fh *handle = fh;
	struct uvc_video_chain *chain = handle->chain;
	struct v4l2_ext_control xctrl;
	int ret;

	memset(&xctrl, 0, sizeof(xctrl));
	xctrl.id = ctrl->id;

	ret = uvc_ctrl_begin(chain);
	if (ret < 0)
		return ret;

	ret = uvc_ctrl_get(chain, &xctrl);
	uvc_ctrl_rollback(handle);
	if (ret < 0)
		return ret;

	ctrl->value = xctrl.value;
	return 0;
}

static int uvc_ioctl_s_ctrl(struct file *file, void *fh,
			    struct v4l2_control *ctrl)
{
	struct uvc_fh *handle = fh;
	struct uvc_video_chain *chain = handle->chain;
	struct v4l2_ext_control xctrl;
	int ret;

	memset(&xctrl, 0, sizeof(xctrl));
	xctrl.id = ctrl->id;
	xctrl.value = ctrl->value;

	ret = uvc_ctrl_begin(chain);
	if (ret < 0)
		return ret;

	ret = uvc_ctrl_set(handle, &xctrl);
	if (ret < 0) {
		uvc_ctrl_rollback(handle);
		return ret;
	}

	ret = uvc_ctrl_commit(handle, &xctrl, 1);
	if (ret < 0)
		return ret;

	ctrl->value = xctrl.value;
	return 0;
}

static int uvc_ioctl_g_ext_ctrls(struct file *file, void *fh,
				 struct v4l2_ext_controls *ctrls)
{
	struct uvc_fh *handle = fh;
	struct uvc_video_chain *chain = handle->chain;
	struct v4l2_ext_control *ctrl = ctrls->controls;
	unsigned int i;
	int ret;

	if (ctrls->which == V4L2_CTRL_WHICH_DEF_VAL) {
		for (i = 0; i < ctrls->count; ++ctrl, ++i) {
			struct v4l2_queryctrl qc = { .id = ctrl->id };

			ret = uvc_query_v4l2_ctrl(chain, &qc);
			if (ret < 0) {
				ctrls->error_idx = i;
				return ret;
			}

			ctrl->value = qc.default_value;
		}

		return 0;
	}

	ret = uvc_ctrl_begin(chain);
	if (ret < 0)
		return ret;

	for (i = 0; i < ctrls->count; ++ctrl, ++i) {
		ret = uvc_ctrl_get(chain, ctrl);
		if (ret < 0) {
			uvc_ctrl_rollback(handle);
			ctrls->error_idx = i;
			return ret;
		}
	}

	ctrls->error_idx = 0;

	return uvc_ctrl_rollback(handle);
}

static int uvc_ioctl_s_try_ext_ctrls(struct uvc_fh *handle,
				     struct v4l2_ext_controls *ctrls,
				     bool commit)
{
	struct v4l2_ext_control *ctrl = ctrls->controls;
	struct uvc_video_chain *chain = handle->chain;
	unsigned int i;
	int ret;

	/* Default value cannot be changed */
	if (ctrls->which == V4L2_CTRL_WHICH_DEF_VAL)
		return -EINVAL;

	ret = uvc_ctrl_begin(chain);
	if (ret < 0)
		return ret;

	for (i = 0; i < ctrls->count; ++ctrl, ++i) {
		ret = uvc_ctrl_set(handle, ctrl);
		if (ret < 0) {
			uvc_ctrl_rollback(handle);
			ctrls->error_idx = commit ? ctrls->count : i;
			return ret;
		}
	}

	ctrls->error_idx = 0;

	if (commit)
		return uvc_ctrl_commit(handle, ctrls->controls, ctrls->count);
	else
		return uvc_ctrl_rollback(handle);
}

static int uvc_ioctl_s_ext_ctrls(struct file *file, void *fh,
				 struct v4l2_ext_controls *ctrls)
{
	struct uvc_fh *handle = fh;

	return uvc_ioctl_s_try_ext_ctrls(handle, ctrls, true);
}

static int uvc_ioctl_try_ext_ctrls(struct file *file, void *fh,
				   struct v4l2_ext_controls *ctrls)
{
	struct uvc_fh *handle = fh;

	return uvc_ioctl_s_try_ext_ctrls(handle, ctrls, false);
}

static int uvc_ioctl_querymenu(struct file *file, void *fh,
			       struct v4l2_querymenu *qm)
{
	struct uvc_fh *handle = fh;
	struct uvc_video_chain *chain = handle->chain;

	return uvc_query_v4l2_menu(chain, qm);
}

static int uvc_ioctl_g_selection(struct file *file, void *fh,
				 struct v4l2_selection *sel)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;

	if (sel->type != stream->type)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (stream->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		if (stream->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	sel->r.left = 0;
	sel->r.top = 0;
	mutex_lock(&stream->mutex);
	sel->r.width = stream->cur_frame->wWidth;
	sel->r.height = stream->cur_frame->wHeight;
	mutex_unlock(&stream->mutex);

	return 0;
}

static int uvc_ioctl_g_parm(struct file *file, void *fh,
			    struct v4l2_streamparm *parm)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;

	return uvc_v4l2_get_streamparm(stream, parm);
}

static int uvc_ioctl_s_parm(struct file *file, void *fh,
			    struct v4l2_streamparm *parm)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;
	int ret;

	ret = uvc_acquire_privileges(handle);
	if (ret < 0)
		return ret;

	return uvc_v4l2_set_streamparm(stream, parm);
}

static int uvc_ioctl_enum_framesizes(struct file *file, void *fh,
				     struct v4l2_frmsizeenum *fsize)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;
	struct uvc_format *format = NULL;
	struct uvc_frame *frame = NULL;
	unsigned int index;
	unsigned int i;

	/* Look for the given pixel format */
	for (i = 0; i < stream->nformats; i++) {
		if (stream->format[i].fcc == fsize->pixel_format) {
			format = &stream->format[i];
			break;
		}
	}
	if (format == NULL)
		return -EINVAL;

	/* Skip duplicate frame sizes */
	for (i = 0, index = 0; i < format->nframes; i++) {
		if (frame && frame->wWidth == format->frame[i].wWidth &&
		    frame->wHeight == format->frame[i].wHeight)
			continue;
		frame = &format->frame[i];
		if (index == fsize->index)
			break;
		index++;
	}

	if (i == format->nframes)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = frame->wWidth;
	fsize->discrete.height = frame->wHeight;
	return 0;
}

static int uvc_ioctl_enum_frameintervals(struct file *file, void *fh,
					 struct v4l2_frmivalenum *fival)
{
	struct uvc_fh *handle = fh;
	struct uvc_streaming *stream = handle->stream;
	struct uvc_format *format = NULL;
	struct uvc_frame *frame = NULL;
	unsigned int nintervals;
	unsigned int index;
	unsigned int i;

	/* Look for the given pixel format and frame size */
	for (i = 0; i < stream->nformats; i++) {
		if (stream->format[i].fcc == fival->pixel_format) {
			format = &stream->format[i];
			break;
		}
	}
	if (format == NULL)
		return -EINVAL;

	index = fival->index;
	for (i = 0; i < format->nframes; i++) {
		if (format->frame[i].wWidth == fival->width &&
		    format->frame[i].wHeight == fival->height) {
			frame = &format->frame[i];
			nintervals = frame->bFrameIntervalType ?: 1;
			if (index < nintervals)
				break;
			index -= nintervals;
		}
	}
	if (i == format->nframes)
		return -EINVAL;

	if (frame->bFrameIntervalType) {
		fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
		fival->discrete.numerator =
			frame->dwFrameInterval[index];
		fival->discrete.denominator = 10000000;
		uvc_simplify_fraction(&fival->discrete.numerator,
			&fival->discrete.denominator, 8, 333);
	} else {
		fival->type = V4L2_FRMIVAL_TYPE_STEPWISE;
		fival->stepwise.min.numerator = frame->dwFrameInterval[0];
		fival->stepwise.min.denominator = 10000000;
		fival->stepwise.max.numerator = frame->dwFrameInterval[1];
		fival->stepwise.max.denominator = 10000000;
		fival->stepwise.step.numerator = frame->dwFrameInterval[2];
		fival->stepwise.step.denominator = 10000000;
		uvc_simplify_fraction(&fival->stepwise.min.numerator,
			&fival->stepwise.min.denominator, 8, 333);
		uvc_simplify_fraction(&fival->stepwise.max.numerator,
			&fival->stepwise.max.denominator, 8, 333);
		uvc_simplify_fraction(&fival->stepwise.step.numerator,
			&fival->stepwise.step.denominator, 8, 333);
	}

	return 0;
}

static int uvc_ioctl_subscribe_event(struct v4l2_fh *fh,
				     const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_CTRL:
		return v4l2_event_subscribe(fh, sub, 0, &uvc_ctrl_sub_ev_ops);
	default:
		return -EINVAL;
	}
}

static long uvc_ioctl_default(struct file *file, void *fh, bool valid_prio,
			      unsigned int cmd, void *arg)
{
	struct uvc_fh *handle = fh;
	struct uvc_video_chain *chain = handle->chain;

	switch (cmd) {
	/* Dynamic controls. */
	case UVCIOC_CTRL_MAP:
		return uvc_ioctl_ctrl_map(chain, arg);

	case UVCIOC_CTRL_QUERY:
		return uvc_xu_ctrl_query(chain, arg);

	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
struct uvc_xu_control_mapping32 {
	u32 id;
	u8 name[32];
	u8 entity[16];
	u8 selector;

	u8 size;
	u8 offset;
	u32 v4l2_type;
	u32 data_type;

	compat_caddr_t menu_info;
	u32 menu_count;

	u32 reserved[4];
};

static int uvc_v4l2_get_xu_mapping(struct uvc_xu_control_mapping *kp,
			const struct uvc_xu_control_mapping32 __user *up)
{
	struct uvc_xu_control_mapping32 *p = (void *)kp;
	compat_caddr_t info;
	u32 count;

	if (copy_from_user(p, up, sizeof(*p)))
		return -EFAULT;

	count = p->menu_count;
	info = p->menu_info;

	memset(kp->reserved, 0, sizeof(kp->reserved));
	kp->menu_info = count ? compat_ptr(info) : NULL;
	kp->menu_count = count;
	return 0;
}

static int uvc_v4l2_put_xu_mapping(const struct uvc_xu_control_mapping *kp,
			struct uvc_xu_control_mapping32 __user *up)
{
	if (copy_to_user(up, kp, offsetof(typeof(*up), menu_info)) ||
	    put_user(kp->menu_count, &up->menu_count))
		return -EFAULT;

	if (clear_user(up->reserved, sizeof(up->reserved)))
		return -EFAULT;

	return 0;
}

struct uvc_xu_control_query32 {
	u8 unit;
	u8 selector;
	u8 query;
	u16 size;
	compat_caddr_t data;
};

static int uvc_v4l2_get_xu_query(struct uvc_xu_control_query *kp,
			const struct uvc_xu_control_query32 __user *up)
{
	struct uvc_xu_control_query32 v;

	if (copy_from_user(&v, up, sizeof(v)))
		return -EFAULT;

	*kp = (struct uvc_xu_control_query){
		.unit = v.unit,
		.selector = v.selector,
		.query = v.query,
		.size = v.size,
		.data = v.size ? compat_ptr(v.data) : NULL
	};
	return 0;
}

static int uvc_v4l2_put_xu_query(const struct uvc_xu_control_query *kp,
			struct uvc_xu_control_query32 __user *up)
{
	if (copy_to_user(up, kp, offsetof(typeof(*up), data)))
		return -EFAULT;
	return 0;
}

#define UVCIOC_CTRL_MAP32	_IOWR('u', 0x20, struct uvc_xu_control_mapping32)
#define UVCIOC_CTRL_QUERY32	_IOWR('u', 0x21, struct uvc_xu_control_query32)

static long uvc_v4l2_compat_ioctl32(struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct uvc_fh *handle = file->private_data;
	union {
		struct uvc_xu_control_mapping xmap;
		struct uvc_xu_control_query xqry;
	} karg;
	void __user *up = compat_ptr(arg);
	long ret;

	switch (cmd) {
	case UVCIOC_CTRL_MAP32:
		ret = uvc_v4l2_get_xu_mapping(&karg.xmap, up);
		if (ret)
			return ret;
		ret = uvc_ioctl_ctrl_map(handle->chain, &karg.xmap);
		if (ret)
			return ret;
		ret = uvc_v4l2_put_xu_mapping(&karg.xmap, up);
		if (ret)
			return ret;

		break;

	case UVCIOC_CTRL_QUERY32:
		ret = uvc_v4l2_get_xu_query(&karg.xqry, up);
		if (ret)
			return ret;
		ret = uvc_xu_ctrl_query(handle->chain, &karg.xqry);
		if (ret)
			return ret;
		ret = uvc_v4l2_put_xu_query(&karg.xqry, up);
		if (ret)
			return ret;
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return ret;
}
#endif

static ssize_t uvc_v4l2_read(struct file *file, char __user *data,
		    size_t count, loff_t *ppos)
{
	uvc_trace(UVC_TRACE_CALLS, "uvc_v4l2_read: not implemented.\n");
	return -EINVAL;
}

static int uvc_v4l2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct uvc_fh *handle = file->private_data;
	struct uvc_streaming *stream = handle->stream;

	uvc_trace(UVC_TRACE_CALLS, "uvc_v4l2_mmap\n");

	return uvc_queue_mmap(&stream->queue, vma);
}

static __poll_t uvc_v4l2_poll(struct file *file, poll_table *wait)
{
	struct uvc_fh *handle = file->private_data;
	struct uvc_streaming *stream = handle->stream;

	uvc_trace(UVC_TRACE_CALLS, "uvc_v4l2_poll\n");

	return uvc_queue_poll(&stream->queue, file, wait);
}

#ifndef CONFIG_MMU
static unsigned long uvc_v4l2_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	struct uvc_fh *handle = file->private_data;
	struct uvc_streaming *stream = handle->stream;

	uvc_trace(UVC_TRACE_CALLS, "uvc_v4l2_get_unmapped_area\n");

	return uvc_queue_get_unmapped_area(&stream->queue, pgoff);
}
#endif

const struct v4l2_ioctl_ops uvc_ioctl_ops = {
	.vidioc_querycap = uvc_ioctl_querycap,
	.vidioc_enum_fmt_vid_cap = uvc_ioctl_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out = uvc_ioctl_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_cap = uvc_ioctl_g_fmt_vid_cap,
	.vidioc_g_fmt_vid_out = uvc_ioctl_g_fmt_vid_out,
	.vidioc_s_fmt_vid_cap = uvc_ioctl_s_fmt_vid_cap,
	.vidioc_s_fmt_vid_out = uvc_ioctl_s_fmt_vid_out,
	.vidioc_try_fmt_vid_cap = uvc_ioctl_try_fmt_vid_cap,
	.vidioc_try_fmt_vid_out = uvc_ioctl_try_fmt_vid_out,
	.vidioc_reqbufs = uvc_ioctl_reqbufs,
	.vidioc_querybuf = uvc_ioctl_querybuf,
	.vidioc_qbuf = uvc_ioctl_qbuf,
	.vidioc_expbuf = uvc_ioctl_expbuf,
	.vidioc_dqbuf = uvc_ioctl_dqbuf,
	.vidioc_create_bufs = uvc_ioctl_create_bufs,
	.vidioc_streamon = uvc_ioctl_streamon,
	.vidioc_streamoff = uvc_ioctl_streamoff,
	.vidioc_enum_input = uvc_ioctl_enum_input,
	.vidioc_g_input = uvc_ioctl_g_input,
	.vidioc_s_input = uvc_ioctl_s_input,
	.vidioc_queryctrl = uvc_ioctl_queryctrl,
	.vidioc_query_ext_ctrl = uvc_ioctl_query_ext_ctrl,
	.vidioc_g_ctrl = uvc_ioctl_g_ctrl,
	.vidioc_s_ctrl = uvc_ioctl_s_ctrl,
	.vidioc_g_ext_ctrls = uvc_ioctl_g_ext_ctrls,
	.vidioc_s_ext_ctrls = uvc_ioctl_s_ext_ctrls,
	.vidioc_try_ext_ctrls = uvc_ioctl_try_ext_ctrls,
	.vidioc_querymenu = uvc_ioctl_querymenu,
	.vidioc_g_selection = uvc_ioctl_g_selection,
	.vidioc_g_parm = uvc_ioctl_g_parm,
	.vidioc_s_parm = uvc_ioctl_s_parm,
	.vidioc_enum_framesizes = uvc_ioctl_enum_framesizes,
	.vidioc_enum_frameintervals = uvc_ioctl_enum_frameintervals,
	.vidioc_subscribe_event = uvc_ioctl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_default = uvc_ioctl_default,
};

const struct v4l2_file_operations uvc_fops = {
	.owner		= THIS_MODULE,
	.open		= uvc_v4l2_open,
	.release	= uvc_v4l2_release,
	.unlocked_ioctl	= video_ioctl2,
#ifdef CONFIG_COMPAT
	.compat_ioctl32	= uvc_v4l2_compat_ioctl32,
#endif
	.read		= uvc_v4l2_read,
	.mmap		= uvc_v4l2_mmap,
	.poll		= uvc_v4l2_poll,
#ifndef CONFIG_MMU
	.get_unmapped_area = uvc_v4l2_get_unmapped_area,
#endif
};

