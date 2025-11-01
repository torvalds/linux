// SPDX-License-Identifier: GPL-2.0+
/*
 *	uvc_v4l2.c  --  USB Video Class Gadget driver
 *
 *	Copyright (C) 2009-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/usb/g_uvc.h>
#include <linux/usb/uvc.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "f_uvc.h"
#include "uvc.h"
#include "uvc_queue.h"
#include "uvc_video.h"
#include "uvc_v4l2.h"
#include "uvc_configfs.h"

static const struct uvc_format_desc *to_uvc_format(struct uvcg_format *uformat)
{
	char guid[16] = UVC_GUID_FORMAT_MJPEG;
	const struct uvc_format_desc *format;

	if (uformat->type == UVCG_UNCOMPRESSED) {
		struct uvcg_uncompressed *unc;

		unc = to_uvcg_uncompressed(&uformat->group.cg_item);
		if (!unc)
			return ERR_PTR(-EINVAL);

		memcpy(guid, unc->desc.guidFormat, sizeof(guid));
	} else if (uformat->type == UVCG_FRAMEBASED) {
		struct uvcg_framebased *unc;

		unc = to_uvcg_framebased(&uformat->group.cg_item);
		if (!unc)
			return ERR_PTR(-EINVAL);

		memcpy(guid, unc->desc.guidFormat, sizeof(guid));
	}

	format = uvc_format_by_guid(guid);
	if (!format)
		return ERR_PTR(-EINVAL);

	return format;
}

static int uvc_v4l2_get_bytesperline(struct uvcg_format *uformat,
			      struct uvcg_frame *uframe)
{
	struct uvcg_uncompressed *u;

	if (uformat->type == UVCG_UNCOMPRESSED) {
		u = to_uvcg_uncompressed(&uformat->group.cg_item);
		if (!u)
			return 0;

		return u->desc.bBitsPerPixel * uframe->frame.w_width / 8;
	}

	return 0;
}

static int uvc_get_frame_size(struct uvcg_format *uformat,
		       struct uvcg_frame *uframe)
{
	unsigned int bpl = uvc_v4l2_get_bytesperline(uformat, uframe);

	return bpl ? bpl * uframe->frame.w_height :
		uframe->frame.dw_max_video_frame_buffer_size;
}

static struct uvcg_format *find_format_by_index(struct uvc_device *uvc, int index)
{
	struct uvcg_format_ptr *format;
	struct uvcg_format *uformat = NULL;
	int i = 1;

	list_for_each_entry(format, &uvc->header->formats, entry) {
		if (index == i) {
			uformat = format->fmt;
			break;
		}
		i++;
	}

	return uformat;
}

static struct uvcg_frame *find_frame_by_index(struct uvc_device *uvc,
				       struct uvcg_format *uformat,
				       int index)
{
	struct uvcg_format_ptr *format;
	struct uvcg_frame_ptr *frame;
	struct uvcg_frame *uframe = NULL;

	list_for_each_entry(format, &uvc->header->formats, entry) {
		if (format->fmt->type != uformat->type)
			continue;
		list_for_each_entry(frame, &format->fmt->frames, entry) {
			if (index == frame->frm->frame.b_frame_index) {
				uframe = frame->frm;
				break;
			}
		}
	}

	return uframe;
}

static struct uvcg_format *find_format_by_pix(struct uvc_device *uvc,
					      u32 pixelformat)
{
	struct uvcg_format_ptr *format;
	struct uvcg_format *uformat = NULL;

	list_for_each_entry(format, &uvc->header->formats, entry) {
		const struct uvc_format_desc *fmtdesc = to_uvc_format(format->fmt);

		if (IS_ERR(fmtdesc))
			continue;

		if (fmtdesc->fcc == pixelformat) {
			uformat = format->fmt;
			break;
		}
	}

	return uformat;
}

static struct uvcg_frame *find_closest_frame_by_size(struct uvc_device *uvc,
					   struct uvcg_format *uformat,
					   u16 rw, u16 rh)
{
	struct uvc_video *video = &uvc->video;
	struct uvcg_format_ptr *format;
	struct uvcg_frame_ptr *frame;
	struct uvcg_frame *uframe = NULL;
	unsigned int d, maxd;

	/* Find the closest image size. The distance between image sizes is
	 * the size in pixels of the non-overlapping regions between the
	 * requested size and the frame-specified size.
	 */
	maxd = (unsigned int)-1;

	list_for_each_entry(format, &uvc->header->formats, entry) {
		if (format->fmt->type != uformat->type)
			continue;

		list_for_each_entry(frame, &format->fmt->frames, entry) {
			u16 w, h;

			w = frame->frm->frame.w_width;
			h = frame->frm->frame.w_height;

			d = min(w, rw) * min(h, rh);
			d = w*h + rw*rh - 2*d;
			if (d < maxd) {
				maxd = d;
				uframe = frame->frm;
			}

			if (maxd == 0)
				break;
		}
	}

	if (!uframe)
		uvcg_dbg(&video->uvc->func, "Unsupported size %ux%u\n", rw, rh);

	return uframe;
}

/* --------------------------------------------------------------------------
 * Requests handling
 */

static int
uvc_send_response(struct uvc_device *uvc, struct uvc_request_data *data)
{
	struct usb_composite_dev *cdev = uvc->func.config->cdev;
	struct usb_request *req = uvc->control_req;

	if (data->length < 0)
		return usb_ep_set_halt(cdev->gadget->ep0);

	req->length = min_t(unsigned int, uvc->event_length, data->length);
	req->zero = data->length < uvc->event_length;

	memcpy(req->buf, data->data, req->length);

	return usb_ep_queue(cdev->gadget->ep0, req, GFP_KERNEL);
}

/* --------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int
uvc_v4l2_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct usb_composite_dev *cdev = uvc->func.config->cdev;

	strscpy(cap->driver, "g_uvc", sizeof(cap->driver));
	strscpy(cap->card, cdev->gadget->name, sizeof(cap->card));
	strscpy(cap->bus_info, dev_name(&cdev->gadget->dev),
		sizeof(cap->bus_info));
	return 0;
}

static int
uvc_v4l2_get_format(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;

	fmt->fmt.pix.pixelformat = video->fcc;
	fmt->fmt.pix.width = video->width;
	fmt->fmt.pix.height = video->height;
	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.bytesperline = video->bpp * video->width / 8;
	fmt->fmt.pix.sizeimage = video->imagesize;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	fmt->fmt.pix.priv = 0;

	return 0;
}

static int
uvc_v4l2_try_format(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;
	struct uvcg_format *uformat;
	struct uvcg_frame *uframe;
	const struct uvc_format_desc *fmtdesc;
	u8 *fcc;

	if (fmt->type != video->queue.queue.type)
		return -EINVAL;

	fcc = (u8 *)&fmt->fmt.pix.pixelformat;
	uvcg_dbg(&uvc->func, "Trying format 0x%08x (%c%c%c%c): %ux%u\n",
		fmt->fmt.pix.pixelformat,
		fcc[0], fcc[1], fcc[2], fcc[3],
		fmt->fmt.pix.width, fmt->fmt.pix.height);

	uformat = find_format_by_pix(uvc, fmt->fmt.pix.pixelformat);
	if (!uformat)
		return -EINVAL;

	uframe = find_closest_frame_by_size(uvc, uformat,
				fmt->fmt.pix.width, fmt->fmt.pix.height);
	if (!uframe)
		return -EINVAL;

	if (uformat->type == UVCG_UNCOMPRESSED) {
		struct uvcg_uncompressed *u =
			to_uvcg_uncompressed(&uformat->group.cg_item);
		if (!u)
			return 0;

		v4l2_fill_pixfmt(&fmt->fmt.pix, fmt->fmt.pix.pixelformat,
				 uframe->frame.w_width, uframe->frame.w_height);

		if (fmt->fmt.pix.sizeimage != (uvc_v4l2_get_bytesperline(uformat, uframe) *
						uframe->frame.w_height))
			return -EINVAL;
	} else {
		fmt->fmt.pix.width = uframe->frame.w_width;
		fmt->fmt.pix.height = uframe->frame.w_height;
		fmt->fmt.pix.bytesperline = uvc_v4l2_get_bytesperline(uformat, uframe);
		fmt->fmt.pix.sizeimage = uvc_get_frame_size(uformat, uframe);
		fmtdesc = to_uvc_format(uformat);
		if (IS_ERR(fmtdesc))
			return PTR_ERR(fmtdesc);
		fmt->fmt.pix.pixelformat = fmtdesc->fcc;
	}
	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	fmt->fmt.pix.priv = 0;

	return 0;
}

static int
uvc_v4l2_set_format(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;
	int ret;

	ret = uvc_v4l2_try_format(file, fh, fmt);
	if (ret)
		return ret;

	video->fcc = fmt->fmt.pix.pixelformat;
	video->bpp = fmt->fmt.pix.bytesperline * 8 / video->width;
	video->width = fmt->fmt.pix.width;
	video->height = fmt->fmt.pix.height;
	video->imagesize = fmt->fmt.pix.sizeimage;

	return ret;
}

static int uvc_v4l2_g_parm(struct file *file, void *fh,
			   struct v4l2_streamparm *parm)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;
	struct v4l2_fract timeperframe;

	if (!V4L2_TYPE_IS_OUTPUT(parm->type))
		return -EINVAL;

	/* Return the actual frame period. */
	timeperframe.numerator = video->interval;
	timeperframe.denominator = 10000000;
	v4l2_simplify_fraction(&timeperframe.numerator,
			       &timeperframe.denominator, 8, 333);

	uvcg_dbg(&uvc->func, "Getting frame interval of %u/%u (%u)\n",
		 timeperframe.numerator, timeperframe.denominator,
		 video->interval);

	parm->parm.output.timeperframe = timeperframe;
	parm->parm.output.capability = V4L2_CAP_TIMEPERFRAME;

	return 0;
}

static int uvc_v4l2_s_parm(struct file *file, void *fh,
			   struct v4l2_streamparm *parm)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;
	struct v4l2_fract timeperframe;

	if (!V4L2_TYPE_IS_OUTPUT(parm->type))
		return -EINVAL;

	timeperframe = parm->parm.output.timeperframe;

	video->interval = v4l2_fraction_to_interval(timeperframe.numerator,
						    timeperframe.denominator);

	uvcg_dbg(&uvc->func, "Setting frame interval to %u/%u (%u)\n",
		 timeperframe.numerator, timeperframe.denominator,
		 video->interval);

	return 0;
}

static int
uvc_v4l2_enum_frameintervals(struct file *file, void *fh,
		struct v4l2_frmivalenum *fival)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvcg_format *uformat = NULL;
	struct uvcg_frame *uframe = NULL;
	struct uvcg_frame_ptr *frame;

	uformat = find_format_by_pix(uvc, fival->pixel_format);
	if (!uformat)
		return -EINVAL;

	list_for_each_entry(frame, &uformat->frames, entry) {
		if (frame->frm->frame.w_width == fival->width &&
		    frame->frm->frame.w_height == fival->height) {
			uframe = frame->frm;
			break;
		}
	}
	if (!uframe)
		return -EINVAL;

	if (fival->index >= uframe->frame.b_frame_interval_type)
		return -EINVAL;

	fival->discrete.numerator =
		uframe->dw_frame_interval[fival->index];

	/* TODO: handle V4L2_FRMIVAL_TYPE_STEPWISE */
	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.denominator = 10000000;
	v4l2_simplify_fraction(&fival->discrete.numerator,
		&fival->discrete.denominator, 8, 333);

	return 0;
}

static int
uvc_v4l2_enum_framesizes(struct file *file, void *fh,
		struct v4l2_frmsizeenum *fsize)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvcg_format *uformat = NULL;
	struct uvcg_frame *uframe = NULL;

	uformat = find_format_by_pix(uvc, fsize->pixel_format);
	if (!uformat)
		return -EINVAL;

	if (fsize->index >= uformat->num_frames)
		return -EINVAL;

	uframe = find_frame_by_index(uvc, uformat, fsize->index + 1);
	if (!uframe)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = uframe->frame.w_width;
	fsize->discrete.height = uframe->frame.w_height;

	return 0;
}

static int
uvc_v4l2_enum_format(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	const struct uvc_format_desc *fmtdesc;
	struct uvcg_format *uformat;

	if (f->index >= uvc->header->num_fmt)
		return -EINVAL;

	uformat = find_format_by_index(uvc, f->index + 1);
	if (!uformat)
		return -EINVAL;

	fmtdesc = to_uvc_format(uformat);
	if (IS_ERR(fmtdesc))
		return PTR_ERR(fmtdesc);

	f->pixelformat = fmtdesc->fcc;

	return 0;
}

static int
uvc_v4l2_reqbufs(struct file *file, void *fh, struct v4l2_requestbuffers *b)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;

	if (b->type != video->queue.queue.type)
		return -EINVAL;

	return uvcg_alloc_buffers(&video->queue, b);
}

static int
uvc_v4l2_querybuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;

	return uvcg_query_buffer(&video->queue, b);
}

static int
uvc_v4l2_qbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;
	int ret;

	ret = uvcg_queue_buffer(&video->queue, b);
	if (ret < 0)
		return ret;

	if (uvc->state == UVC_STATE_STREAMING)
		queue_work(video->async_wq, &video->pump);

	return ret;
}

static int
uvc_v4l2_dqbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;

	return uvcg_dequeue_buffer(&video->queue, b, file->f_flags & O_NONBLOCK);
}

static int
uvc_v4l2_streamon(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;
	int ret;

	if (type != video->queue.queue.type)
		return -EINVAL;

	/* Enable UVC video. */
	ret = uvcg_video_enable(video);
	if (ret < 0)
		return ret;

	/*
	 * Complete the alternate setting selection setup phase now that
	 * userspace is ready to provide video frames.
	 */
	uvc_function_setup_continue(uvc, 0);
	uvc->state = UVC_STATE_STREAMING;

	return 0;
}

static int
uvc_v4l2_streamoff(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;
	int ret = 0;

	if (type != video->queue.queue.type)
		return -EINVAL;

	ret = uvcg_video_disable(video);
	if (ret < 0)
		return ret;

	if (uvc->state != UVC_STATE_STREAMING)
		return 0;

	uvc->state = UVC_STATE_CONNECTED;
	uvc_function_setup_continue(uvc, 1);
	return 0;
}

static int
uvc_v4l2_subscribe_event(struct v4l2_fh *fh,
			 const struct v4l2_event_subscription *sub)
{
	struct uvc_device *uvc = video_get_drvdata(fh->vdev);
	struct uvc_file_handle *handle = to_uvc_file_handle(fh);
	int ret;

	if (sub->type < UVC_EVENT_FIRST || sub->type > UVC_EVENT_LAST)
		return -EINVAL;

	if (sub->type == UVC_EVENT_SETUP && uvc->func_connected)
		return -EBUSY;

	ret = v4l2_event_subscribe(fh, sub, 2, NULL);
	if (ret < 0)
		return ret;

	if (sub->type == UVC_EVENT_SETUP) {
		uvc->func_connected = true;
		handle->is_uvc_app_handle = true;
		uvc_function_connect(uvc);
	}

	return 0;
}

static void uvc_v4l2_disable(struct uvc_device *uvc)
{
	uvc_function_disconnect(uvc);
	uvcg_video_disable(&uvc->video);
	uvcg_free_buffers(&uvc->video.queue);
	uvc->func_connected = false;
	wake_up_interruptible(&uvc->func_connected_queue);
}

static int
uvc_v4l2_unsubscribe_event(struct v4l2_fh *fh,
			   const struct v4l2_event_subscription *sub)
{
	struct uvc_device *uvc = video_get_drvdata(fh->vdev);
	struct uvc_file_handle *handle = to_uvc_file_handle(fh);
	int ret;

	ret = v4l2_event_unsubscribe(fh, sub);
	if (ret < 0)
		return ret;

	if (sub->type == UVC_EVENT_SETUP && handle->is_uvc_app_handle) {
		uvc_v4l2_disable(uvc);
		handle->is_uvc_app_handle = false;
	}

	return 0;
}

static long
uvc_v4l2_ioctl_default(struct file *file, void *fh, bool valid_prio,
		       unsigned int cmd, void *arg)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);

	switch (cmd) {
	case UVCIOC_SEND_RESPONSE:
		return uvc_send_response(uvc, arg);

	default:
		return -ENOIOCTLCMD;
	}
}

const struct v4l2_ioctl_ops uvc_v4l2_ioctl_ops = {
	.vidioc_querycap = uvc_v4l2_querycap,
	.vidioc_try_fmt_vid_out = uvc_v4l2_try_format,
	.vidioc_g_fmt_vid_out = uvc_v4l2_get_format,
	.vidioc_s_fmt_vid_out = uvc_v4l2_set_format,
	.vidioc_enum_frameintervals = uvc_v4l2_enum_frameintervals,
	.vidioc_enum_framesizes = uvc_v4l2_enum_framesizes,
	.vidioc_enum_fmt_vid_out = uvc_v4l2_enum_format,
	.vidioc_reqbufs = uvc_v4l2_reqbufs,
	.vidioc_querybuf = uvc_v4l2_querybuf,
	.vidioc_qbuf = uvc_v4l2_qbuf,
	.vidioc_dqbuf = uvc_v4l2_dqbuf,
	.vidioc_streamon = uvc_v4l2_streamon,
	.vidioc_streamoff = uvc_v4l2_streamoff,
	.vidioc_s_parm = uvc_v4l2_s_parm,
	.vidioc_g_parm = uvc_v4l2_g_parm,
	.vidioc_subscribe_event = uvc_v4l2_subscribe_event,
	.vidioc_unsubscribe_event = uvc_v4l2_unsubscribe_event,
	.vidioc_default = uvc_v4l2_ioctl_default,
};

/* --------------------------------------------------------------------------
 * V4L2
 */

static int
uvc_v4l2_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_file_handle *handle;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (handle == NULL)
		return -ENOMEM;

	v4l2_fh_init(&handle->vfh, vdev);
	v4l2_fh_add(&handle->vfh, file);

	handle->device = &uvc->video;

	return 0;
}

static int
uvc_v4l2_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_file_handle *handle = file_to_uvc_file_handle(file);
	struct uvc_video *video = handle->device;

	mutex_lock(&video->mutex);
	if (handle->is_uvc_app_handle)
		uvc_v4l2_disable(uvc);
	mutex_unlock(&video->mutex);

	v4l2_fh_del(&handle->vfh, file);
	v4l2_fh_exit(&handle->vfh);
	kfree(handle);

	return 0;
}

static int
uvc_v4l2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);

	return uvcg_queue_mmap(&uvc->video.queue, vma);
}

static __poll_t
uvc_v4l2_poll(struct file *file, poll_table *wait)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);

	return uvcg_queue_poll(&uvc->video.queue, file, wait);
}

#ifndef CONFIG_MMU
static unsigned long uvcg_v4l2_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);

	return uvcg_queue_get_unmapped_area(&uvc->video.queue, pgoff);
}
#endif

const struct v4l2_file_operations uvc_v4l2_fops = {
	.owner		= THIS_MODULE,
	.open		= uvc_v4l2_open,
	.release	= uvc_v4l2_release,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= uvc_v4l2_mmap,
	.poll		= uvc_v4l2_poll,
#ifndef CONFIG_MMU
	.get_unmapped_area = uvcg_v4l2_get_unmapped_area,
#endif
};
