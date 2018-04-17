/*
 * TI OMAP4 ISS V4L2 Driver - Generic video node
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Author: Sergio Aguirre <sergio.a.aguirre@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <asm/cacheflush.h>
#include <linux/clk.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>

#include "iss_video.h"
#include "iss.h"

/* -----------------------------------------------------------------------------
 * Helper functions
 */

static struct iss_format_info formats[] = {
	{ MEDIA_BUS_FMT_Y8_1X8, MEDIA_BUS_FMT_Y8_1X8,
	  MEDIA_BUS_FMT_Y8_1X8, MEDIA_BUS_FMT_Y8_1X8,
	  V4L2_PIX_FMT_GREY, 8, "Greyscale 8 bpp", },
	{ MEDIA_BUS_FMT_Y10_1X10, MEDIA_BUS_FMT_Y10_1X10,
	  MEDIA_BUS_FMT_Y10_1X10, MEDIA_BUS_FMT_Y8_1X8,
	  V4L2_PIX_FMT_Y10, 10, "Greyscale 10 bpp", },
	{ MEDIA_BUS_FMT_Y12_1X12, MEDIA_BUS_FMT_Y10_1X10,
	  MEDIA_BUS_FMT_Y12_1X12, MEDIA_BUS_FMT_Y8_1X8,
	  V4L2_PIX_FMT_Y12, 12, "Greyscale 12 bpp", },
	{ MEDIA_BUS_FMT_SBGGR8_1X8, MEDIA_BUS_FMT_SBGGR8_1X8,
	  MEDIA_BUS_FMT_SBGGR8_1X8, MEDIA_BUS_FMT_SBGGR8_1X8,
	  V4L2_PIX_FMT_SBGGR8, 8, "BGGR Bayer 8 bpp", },
	{ MEDIA_BUS_FMT_SGBRG8_1X8, MEDIA_BUS_FMT_SGBRG8_1X8,
	  MEDIA_BUS_FMT_SGBRG8_1X8, MEDIA_BUS_FMT_SGBRG8_1X8,
	  V4L2_PIX_FMT_SGBRG8, 8, "GBRG Bayer 8 bpp", },
	{ MEDIA_BUS_FMT_SGRBG8_1X8, MEDIA_BUS_FMT_SGRBG8_1X8,
	  MEDIA_BUS_FMT_SGRBG8_1X8, MEDIA_BUS_FMT_SGRBG8_1X8,
	  V4L2_PIX_FMT_SGRBG8, 8, "GRBG Bayer 8 bpp", },
	{ MEDIA_BUS_FMT_SRGGB8_1X8, MEDIA_BUS_FMT_SRGGB8_1X8,
	  MEDIA_BUS_FMT_SRGGB8_1X8, MEDIA_BUS_FMT_SRGGB8_1X8,
	  V4L2_PIX_FMT_SRGGB8, 8, "RGGB Bayer 8 bpp", },
	{ MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8, MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8,
	  MEDIA_BUS_FMT_SGRBG10_1X10, 0,
	  V4L2_PIX_FMT_SGRBG10DPCM8, 8, "GRBG Bayer 10 bpp DPCM8",  },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, MEDIA_BUS_FMT_SBGGR10_1X10,
	  MEDIA_BUS_FMT_SBGGR10_1X10, MEDIA_BUS_FMT_SBGGR8_1X8,
	  V4L2_PIX_FMT_SBGGR10, 10, "BGGR Bayer 10 bpp", },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, MEDIA_BUS_FMT_SGBRG10_1X10,
	  MEDIA_BUS_FMT_SGBRG10_1X10, MEDIA_BUS_FMT_SGBRG8_1X8,
	  V4L2_PIX_FMT_SGBRG10, 10, "GBRG Bayer 10 bpp", },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, MEDIA_BUS_FMT_SGRBG10_1X10,
	  MEDIA_BUS_FMT_SGRBG10_1X10, MEDIA_BUS_FMT_SGRBG8_1X8,
	  V4L2_PIX_FMT_SGRBG10, 10, "GRBG Bayer 10 bpp", },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, MEDIA_BUS_FMT_SRGGB10_1X10,
	  MEDIA_BUS_FMT_SRGGB10_1X10, MEDIA_BUS_FMT_SRGGB8_1X8,
	  V4L2_PIX_FMT_SRGGB10, 10, "RGGB Bayer 10 bpp", },
	{ MEDIA_BUS_FMT_SBGGR12_1X12, MEDIA_BUS_FMT_SBGGR10_1X10,
	  MEDIA_BUS_FMT_SBGGR12_1X12, MEDIA_BUS_FMT_SBGGR8_1X8,
	  V4L2_PIX_FMT_SBGGR12, 12, "BGGR Bayer 12 bpp", },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, MEDIA_BUS_FMT_SGBRG10_1X10,
	  MEDIA_BUS_FMT_SGBRG12_1X12, MEDIA_BUS_FMT_SGBRG8_1X8,
	  V4L2_PIX_FMT_SGBRG12, 12, "GBRG Bayer 12 bpp", },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, MEDIA_BUS_FMT_SGRBG10_1X10,
	  MEDIA_BUS_FMT_SGRBG12_1X12, MEDIA_BUS_FMT_SGRBG8_1X8,
	  V4L2_PIX_FMT_SGRBG12, 12, "GRBG Bayer 12 bpp", },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, MEDIA_BUS_FMT_SRGGB10_1X10,
	  MEDIA_BUS_FMT_SRGGB12_1X12, MEDIA_BUS_FMT_SRGGB8_1X8,
	  V4L2_PIX_FMT_SRGGB12, 12, "RGGB Bayer 12 bpp", },
	{ MEDIA_BUS_FMT_UYVY8_1X16, MEDIA_BUS_FMT_UYVY8_1X16,
	  MEDIA_BUS_FMT_UYVY8_1X16, 0,
	  V4L2_PIX_FMT_UYVY, 16, "YUV 4:2:2 (UYVY)", },
	{ MEDIA_BUS_FMT_YUYV8_1X16, MEDIA_BUS_FMT_YUYV8_1X16,
	  MEDIA_BUS_FMT_YUYV8_1X16, 0,
	  V4L2_PIX_FMT_YUYV, 16, "YUV 4:2:2 (YUYV)", },
	{ MEDIA_BUS_FMT_YUYV8_1_5X8, MEDIA_BUS_FMT_YUYV8_1_5X8,
	  MEDIA_BUS_FMT_YUYV8_1_5X8, 0,
	  V4L2_PIX_FMT_NV12, 8, "YUV 4:2:0 (NV12)", },
};

const struct iss_format_info *
omap4iss_video_format_info(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); ++i) {
		if (formats[i].code == code)
			return &formats[i];
	}

	return NULL;
}

/*
 * iss_video_mbus_to_pix - Convert v4l2_mbus_framefmt to v4l2_pix_format
 * @video: ISS video instance
 * @mbus: v4l2_mbus_framefmt format (input)
 * @pix: v4l2_pix_format format (output)
 *
 * Fill the output pix structure with information from the input mbus format.
 * The bytesperline and sizeimage fields are computed from the requested bytes
 * per line value in the pix format and information from the video instance.
 *
 * Return the number of padding bytes at end of line.
 */
static unsigned int iss_video_mbus_to_pix(const struct iss_video *video,
					  const struct v4l2_mbus_framefmt *mbus,
					  struct v4l2_pix_format *pix)
{
	unsigned int bpl = pix->bytesperline;
	unsigned int min_bpl;
	unsigned int i;

	memset(pix, 0, sizeof(*pix));
	pix->width = mbus->width;
	pix->height = mbus->height;

	/*
	 * Skip the last format in the loop so that it will be selected if no
	 * match is found.
	 */
	for (i = 0; i < ARRAY_SIZE(formats) - 1; ++i) {
		if (formats[i].code == mbus->code)
			break;
	}

	min_bpl = pix->width * ALIGN(formats[i].bpp, 8) / 8;

	/*
	 * Clamp the requested bytes per line value. If the maximum bytes per
	 * line value is zero, the module doesn't support user configurable line
	 * sizes. Override the requested value with the minimum in that case.
	 */
	if (video->bpl_max)
		bpl = clamp(bpl, min_bpl, video->bpl_max);
	else
		bpl = min_bpl;

	if (!video->bpl_zero_padding || bpl != min_bpl)
		bpl = ALIGN(bpl, video->bpl_alignment);

	pix->pixelformat = formats[i].pixelformat;
	pix->bytesperline = bpl;
	pix->sizeimage = pix->bytesperline * pix->height;
	pix->colorspace = mbus->colorspace;
	pix->field = mbus->field;

	/* FIXME: Special case for NV12! We should make this nicer... */
	if (pix->pixelformat == V4L2_PIX_FMT_NV12)
		pix->sizeimage += (pix->bytesperline * pix->height) / 2;

	return bpl - min_bpl;
}

static void iss_video_pix_to_mbus(const struct v4l2_pix_format *pix,
				  struct v4l2_mbus_framefmt *mbus)
{
	unsigned int i;

	memset(mbus, 0, sizeof(*mbus));
	mbus->width = pix->width;
	mbus->height = pix->height;

	/*
	 * Skip the last format in the loop so that it will be selected if no
	 * match is found.
	 */
	for (i = 0; i < ARRAY_SIZE(formats) - 1; ++i) {
		if (formats[i].pixelformat == pix->pixelformat)
			break;
	}

	mbus->code = formats[i].code;
	mbus->colorspace = pix->colorspace;
	mbus->field = pix->field;
}

static struct v4l2_subdev *
iss_video_remote_subdev(struct iss_video *video, u32 *pad)
{
	struct media_pad *remote;

	remote = media_entity_remote_pad(&video->pad);

	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

/* Return a pointer to the ISS video instance at the far end of the pipeline. */
static struct iss_video *
iss_video_far_end(struct iss_video *video)
{
	struct media_graph graph;
	struct media_entity *entity = &video->video.entity;
	struct media_device *mdev = entity->graph_obj.mdev;
	struct iss_video *far_end = NULL;

	mutex_lock(&mdev->graph_mutex);

	if (media_graph_walk_init(&graph, mdev)) {
		mutex_unlock(&mdev->graph_mutex);
		return NULL;
	}

	media_graph_walk_start(&graph, entity);

	while ((entity = media_graph_walk_next(&graph))) {
		if (entity == &video->video.entity)
			continue;

		if (!is_media_entity_v4l2_video_device(entity))
			continue;

		far_end = to_iss_video(media_entity_to_video_device(entity));
		if (far_end->type != video->type)
			break;

		far_end = NULL;
	}

	mutex_unlock(&mdev->graph_mutex);

	media_graph_walk_cleanup(&graph);

	return far_end;
}

static int
__iss_video_get_format(struct iss_video *video,
		       struct v4l2_mbus_framefmt *format)
{
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *subdev;
	u32 pad;
	int ret;

	subdev = iss_video_remote_subdev(video, &pad);
	if (!subdev)
		return -EINVAL;

	memset(&fmt, 0, sizeof(fmt));
	fmt.pad = pad;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	mutex_lock(&video->mutex);
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	mutex_unlock(&video->mutex);

	if (ret)
		return ret;

	*format = fmt.format;
	return 0;
}

static int
iss_video_check_format(struct iss_video *video, struct iss_video_fh *vfh)
{
	struct v4l2_mbus_framefmt format;
	struct v4l2_pix_format pixfmt;
	int ret;

	ret = __iss_video_get_format(video, &format);
	if (ret < 0)
		return ret;

	pixfmt.bytesperline = 0;
	ret = iss_video_mbus_to_pix(video, &format, &pixfmt);

	if (vfh->format.fmt.pix.pixelformat != pixfmt.pixelformat ||
	    vfh->format.fmt.pix.height != pixfmt.height ||
	    vfh->format.fmt.pix.width != pixfmt.width ||
	    vfh->format.fmt.pix.bytesperline != pixfmt.bytesperline ||
	    vfh->format.fmt.pix.sizeimage != pixfmt.sizeimage)
		return -EINVAL;

	return ret;
}

/* -----------------------------------------------------------------------------
 * Video queue operations
 */

static int iss_video_queue_setup(struct vb2_queue *vq,
				 unsigned int *count, unsigned int *num_planes,
				 unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct iss_video_fh *vfh = vb2_get_drv_priv(vq);
	struct iss_video *video = vfh->video;

	/* Revisit multi-planar support for NV12 */
	*num_planes = 1;

	sizes[0] = vfh->format.fmt.pix.sizeimage;
	if (sizes[0] == 0)
		return -EINVAL;

	*count = min(*count, video->capture_mem / PAGE_ALIGN(sizes[0]));

	return 0;
}

static void iss_video_buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct iss_buffer *buffer = container_of(vbuf, struct iss_buffer, vb);

	if (buffer->iss_addr)
		buffer->iss_addr = 0;
}

static int iss_video_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct iss_video_fh *vfh = vb2_get_drv_priv(vb->vb2_queue);
	struct iss_buffer *buffer = container_of(vbuf, struct iss_buffer, vb);
	struct iss_video *video = vfh->video;
	unsigned long size = vfh->format.fmt.pix.sizeimage;
	dma_addr_t addr;

	if (vb2_plane_size(vb, 0) < size)
		return -ENOBUFS;

	addr = vb2_dma_contig_plane_dma_addr(vb, 0);
	if (!IS_ALIGNED(addr, 32)) {
		dev_dbg(video->iss->dev,
			"Buffer address must be aligned to 32 bytes boundary.\n");
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);
	buffer->iss_addr = addr;
	return 0;
}

static void iss_video_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct iss_video_fh *vfh = vb2_get_drv_priv(vb->vb2_queue);
	struct iss_video *video = vfh->video;
	struct iss_buffer *buffer = container_of(vbuf, struct iss_buffer, vb);
	struct iss_pipeline *pipe = to_iss_pipeline(&video->video.entity);
	unsigned long flags;
	bool empty;

	spin_lock_irqsave(&video->qlock, flags);

	/*
	 * Mark the buffer is faulty and give it back to the queue immediately
	 * if the video node has registered an error. vb2 will perform the same
	 * check when preparing the buffer, but that is inherently racy, so we
	 * need to handle the race condition with an authoritative check here.
	 */
	if (unlikely(video->error)) {
		vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
		spin_unlock_irqrestore(&video->qlock, flags);
		return;
	}

	empty = list_empty(&video->dmaqueue);
	list_add_tail(&buffer->list, &video->dmaqueue);

	spin_unlock_irqrestore(&video->qlock, flags);

	if (empty) {
		enum iss_pipeline_state state;
		unsigned int start;

		if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			state = ISS_PIPELINE_QUEUE_OUTPUT;
		else
			state = ISS_PIPELINE_QUEUE_INPUT;

		spin_lock_irqsave(&pipe->lock, flags);
		pipe->state |= state;
		video->ops->queue(video, buffer);
		video->dmaqueue_flags |= ISS_VIDEO_DMAQUEUE_QUEUED;

		start = iss_pipeline_ready(pipe);
		if (start)
			pipe->state |= ISS_PIPELINE_STREAM;
		spin_unlock_irqrestore(&pipe->lock, flags);

		if (start)
			omap4iss_pipeline_set_stream(pipe,
						ISS_PIPELINE_STREAM_SINGLESHOT);
	}
}

static const struct vb2_ops iss_video_vb2ops = {
	.queue_setup	= iss_video_queue_setup,
	.buf_prepare	= iss_video_buf_prepare,
	.buf_queue	= iss_video_buf_queue,
	.buf_cleanup	= iss_video_buf_cleanup,
};

/*
 * omap4iss_video_buffer_next - Complete the current buffer and return the next
 * @video: ISS video object
 *
 * Remove the current video buffer from the DMA queue and fill its timestamp,
 * field count and state fields before waking up its completion handler.
 *
 * For capture video nodes, the buffer state is set to VB2_BUF_STATE_DONE if no
 * error has been flagged in the pipeline, or to VB2_BUF_STATE_ERROR otherwise.
 *
 * The DMA queue is expected to contain at least one buffer.
 *
 * Return a pointer to the next buffer in the DMA queue, or NULL if the queue is
 * empty.
 */
struct iss_buffer *omap4iss_video_buffer_next(struct iss_video *video)
{
	struct iss_pipeline *pipe = to_iss_pipeline(&video->video.entity);
	enum iss_pipeline_state state;
	struct iss_buffer *buf;
	unsigned long flags;

	spin_lock_irqsave(&video->qlock, flags);
	if (WARN_ON(list_empty(&video->dmaqueue))) {
		spin_unlock_irqrestore(&video->qlock, flags);
		return NULL;
	}

	buf = list_first_entry(&video->dmaqueue, struct iss_buffer,
			       list);
	list_del(&buf->list);
	spin_unlock_irqrestore(&video->qlock, flags);

	buf->vb.vb2_buf.timestamp = ktime_get_ns();

	/*
	 * Do frame number propagation only if this is the output video node.
	 * Frame number either comes from the CSI receivers or it gets
	 * incremented here if H3A is not active.
	 * Note: There is no guarantee that the output buffer will finish
	 * first, so the input number might lag behind by 1 in some cases.
	 */
	if (video == pipe->output && !pipe->do_propagation)
		buf->vb.sequence =
			atomic_inc_return(&pipe->frame_number);
	else
		buf->vb.sequence = atomic_read(&pipe->frame_number);

	vb2_buffer_done(&buf->vb.vb2_buf, pipe->error ?
			VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
	pipe->error = false;

	spin_lock_irqsave(&video->qlock, flags);
	if (list_empty(&video->dmaqueue)) {
		spin_unlock_irqrestore(&video->qlock, flags);
		if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			state = ISS_PIPELINE_QUEUE_OUTPUT
			      | ISS_PIPELINE_STREAM;
		else
			state = ISS_PIPELINE_QUEUE_INPUT
			      | ISS_PIPELINE_STREAM;

		spin_lock_irqsave(&pipe->lock, flags);
		pipe->state &= ~state;
		if (video->pipe.stream_state == ISS_PIPELINE_STREAM_CONTINUOUS)
			video->dmaqueue_flags |= ISS_VIDEO_DMAQUEUE_UNDERRUN;
		spin_unlock_irqrestore(&pipe->lock, flags);
		return NULL;
	}

	if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE && pipe->input) {
		spin_lock(&pipe->lock);
		pipe->state &= ~ISS_PIPELINE_STREAM;
		spin_unlock(&pipe->lock);
	}

	buf = list_first_entry(&video->dmaqueue, struct iss_buffer,
			       list);
	spin_unlock_irqrestore(&video->qlock, flags);
	buf->vb.vb2_buf.state = VB2_BUF_STATE_ACTIVE;
	return buf;
}

/*
 * omap4iss_video_cancel_stream - Cancel stream on a video node
 * @video: ISS video object
 *
 * Cancelling a stream mark all buffers on the video node as erroneous and makes
 * sure no new buffer can be queued.
 */
void omap4iss_video_cancel_stream(struct iss_video *video)
{
	unsigned long flags;

	spin_lock_irqsave(&video->qlock, flags);

	while (!list_empty(&video->dmaqueue)) {
		struct iss_buffer *buf;

		buf = list_first_entry(&video->dmaqueue, struct iss_buffer,
				       list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	vb2_queue_error(video->queue);
	video->error = true;

	spin_unlock_irqrestore(&video->qlock, flags);
}

/* -----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int
iss_video_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct iss_video *video = video_drvdata(file);

	strlcpy(cap->driver, ISS_VIDEO_DRIVER_NAME, sizeof(cap->driver));
	strlcpy(cap->card, video->video.name, sizeof(cap->card));
	strlcpy(cap->bus_info, "media", sizeof(cap->bus_info));

	if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	else
		cap->device_caps = V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_STREAMING;

	cap->capabilities = V4L2_CAP_DEVICE_CAPS | V4L2_CAP_STREAMING
			  | V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT;

	return 0;
}

static int
iss_video_enum_format(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct iss_video *video = video_drvdata(file);
	struct v4l2_mbus_framefmt format;
	unsigned int index = f->index;
	unsigned int i;
	int ret;

	if (f->type != video->type)
		return -EINVAL;

	ret = __iss_video_get_format(video, &format);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(formats); ++i) {
		const struct iss_format_info *info = &formats[i];

		if (format.code != info->code)
			continue;

		if (index == 0) {
			f->pixelformat = info->pixelformat;
			strlcpy(f->description, info->description,
				sizeof(f->description));
			return 0;
		}

		index--;
	}

	return -EINVAL;
}

static int
iss_video_get_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct iss_video_fh *vfh = to_iss_video_fh(fh);
	struct iss_video *video = video_drvdata(file);

	if (format->type != video->type)
		return -EINVAL;

	mutex_lock(&video->mutex);
	*format = vfh->format;
	mutex_unlock(&video->mutex);

	return 0;
}

static int
iss_video_set_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct iss_video_fh *vfh = to_iss_video_fh(fh);
	struct iss_video *video = video_drvdata(file);
	struct v4l2_mbus_framefmt fmt;

	if (format->type != video->type)
		return -EINVAL;

	mutex_lock(&video->mutex);

	/*
	 * Fill the bytesperline and sizeimage fields by converting to media bus
	 * format and back to pixel format.
	 */
	iss_video_pix_to_mbus(&format->fmt.pix, &fmt);
	iss_video_mbus_to_pix(video, &fmt, &format->fmt.pix);

	vfh->format = *format;

	mutex_unlock(&video->mutex);
	return 0;
}

static int
iss_video_try_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct iss_video *video = video_drvdata(file);
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *subdev;
	u32 pad;
	int ret;

	if (format->type != video->type)
		return -EINVAL;

	subdev = iss_video_remote_subdev(video, &pad);
	if (!subdev)
		return -EINVAL;

	iss_video_pix_to_mbus(&format->fmt.pix, &fmt.format);

	fmt.pad = pad;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret)
		return ret;

	iss_video_mbus_to_pix(video, &fmt.format, &format->fmt.pix);
	return 0;
}

static int
iss_video_get_selection(struct file *file, void *fh, struct v4l2_selection *sel)
{
	struct iss_video *video = video_drvdata(file);
	struct v4l2_subdev_format format;
	struct v4l2_subdev *subdev;
	struct v4l2_subdev_selection sdsel = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.target = sel->target,
	};
	u32 pad;
	int ret;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		if (video->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	subdev = iss_video_remote_subdev(video, &pad);
	if (subdev == NULL)
		return -EINVAL;

	/*
	 * Try the get selection operation first and fallback to get format if
	 * not implemented.
	 */
	sdsel.pad = pad;
	ret = v4l2_subdev_call(subdev, pad, get_selection, NULL, &sdsel);
	if (!ret)
		sel->r = sdsel.r;
	if (ret != -ENOIOCTLCMD)
		return ret;

	format.pad = pad;
	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &format);
	if (ret < 0)
		return ret == -ENOIOCTLCMD ? -ENOTTY : ret;

	sel->r.left = 0;
	sel->r.top = 0;
	sel->r.width = format.format.width;
	sel->r.height = format.format.height;

	return 0;
}

static int
iss_video_set_selection(struct file *file, void *fh, struct v4l2_selection *sel)
{
	struct iss_video *video = video_drvdata(file);
	struct v4l2_subdev *subdev;
	struct v4l2_subdev_selection sdsel = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.target = sel->target,
		.flags = sel->flags,
		.r = sel->r,
	};
	u32 pad;
	int ret;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		if (video->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	subdev = iss_video_remote_subdev(video, &pad);
	if (subdev == NULL)
		return -EINVAL;

	sdsel.pad = pad;
	mutex_lock(&video->mutex);
	ret = v4l2_subdev_call(subdev, pad, set_selection, NULL, &sdsel);
	mutex_unlock(&video->mutex);
	if (!ret)
		sel->r = sdsel.r;

	return ret == -ENOIOCTLCMD ? -ENOTTY : ret;
}

static int
iss_video_get_param(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct iss_video_fh *vfh = to_iss_video_fh(fh);
	struct iss_video *video = video_drvdata(file);

	if (video->type != V4L2_BUF_TYPE_VIDEO_OUTPUT ||
	    video->type != a->type)
		return -EINVAL;

	memset(a, 0, sizeof(*a));
	a->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.output.timeperframe = vfh->timeperframe;

	return 0;
}

static int
iss_video_set_param(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct iss_video_fh *vfh = to_iss_video_fh(fh);
	struct iss_video *video = video_drvdata(file);

	if (video->type != V4L2_BUF_TYPE_VIDEO_OUTPUT ||
	    video->type != a->type)
		return -EINVAL;

	if (a->parm.output.timeperframe.denominator == 0)
		a->parm.output.timeperframe.denominator = 1;

	vfh->timeperframe = a->parm.output.timeperframe;

	return 0;
}

static int
iss_video_reqbufs(struct file *file, void *fh, struct v4l2_requestbuffers *rb)
{
	struct iss_video_fh *vfh = to_iss_video_fh(fh);

	return vb2_reqbufs(&vfh->queue, rb);
}

static int
iss_video_querybuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct iss_video_fh *vfh = to_iss_video_fh(fh);

	return vb2_querybuf(&vfh->queue, b);
}

static int
iss_video_qbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct iss_video_fh *vfh = to_iss_video_fh(fh);

	return vb2_qbuf(&vfh->queue, b);
}

static int
iss_video_expbuf(struct file *file, void *fh, struct v4l2_exportbuffer *e)
{
	struct iss_video_fh *vfh = to_iss_video_fh(fh);

	return vb2_expbuf(&vfh->queue, e);
}

static int
iss_video_dqbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct iss_video_fh *vfh = to_iss_video_fh(fh);

	return vb2_dqbuf(&vfh->queue, b, file->f_flags & O_NONBLOCK);
}

/*
 * Stream management
 *
 * Every ISS pipeline has a single input and a single output. The input can be
 * either a sensor or a video node. The output is always a video node.
 *
 * As every pipeline has an output video node, the ISS video objects at the
 * pipeline output stores the pipeline state. It tracks the streaming state of
 * both the input and output, as well as the availability of buffers.
 *
 * In sensor-to-memory mode, frames are always available at the pipeline input.
 * Starting the sensor usually requires I2C transfers and must be done in
 * interruptible context. The pipeline is started and stopped synchronously
 * to the stream on/off commands. All modules in the pipeline will get their
 * subdev set stream handler called. The module at the end of the pipeline must
 * delay starting the hardware until buffers are available at its output.
 *
 * In memory-to-memory mode, starting/stopping the stream requires
 * synchronization between the input and output. ISS modules can't be stopped
 * in the middle of a frame, and at least some of the modules seem to become
 * busy as soon as they're started, even if they don't receive a frame start
 * event. For that reason frames need to be processed in single-shot mode. The
 * driver needs to wait until a frame is completely processed and written to
 * memory before restarting the pipeline for the next frame. Pipelined
 * processing might be possible but requires more testing.
 *
 * Stream start must be delayed until buffers are available at both the input
 * and output. The pipeline must be started in the videobuf queue callback with
 * the buffers queue spinlock held. The modules subdev set stream operation must
 * not sleep.
 */
static int
iss_video_streamon(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct iss_video_fh *vfh = to_iss_video_fh(fh);
	struct iss_video *video = video_drvdata(file);
	struct media_graph graph;
	struct media_entity *entity = &video->video.entity;
	enum iss_pipeline_state state;
	struct iss_pipeline *pipe;
	struct iss_video *far_end;
	unsigned long flags;
	int ret;

	if (type != video->type)
		return -EINVAL;

	mutex_lock(&video->stream_lock);

	/*
	 * Start streaming on the pipeline. No link touching an entity in the
	 * pipeline can be activated or deactivated once streaming is started.
	 */
	pipe = entity->pipe
	     ? to_iss_pipeline(entity) : &video->pipe;
	pipe->external = NULL;
	pipe->external_rate = 0;
	pipe->external_bpp = 0;

	ret = media_entity_enum_init(&pipe->ent_enum, entity->graph_obj.mdev);
	if (ret)
		goto err_graph_walk_init;

	ret = media_graph_walk_init(&graph, entity->graph_obj.mdev);
	if (ret)
		goto err_graph_walk_init;

	if (video->iss->pdata->set_constraints)
		video->iss->pdata->set_constraints(video->iss, true);

	ret = media_pipeline_start(entity, &pipe->pipe);
	if (ret < 0)
		goto err_media_pipeline_start;

	media_graph_walk_start(&graph, entity);
	while ((entity = media_graph_walk_next(&graph)))
		media_entity_enum_set(&pipe->ent_enum, entity);

	/*
	 * Verify that the currently configured format matches the output of
	 * the connected subdev.
	 */
	ret = iss_video_check_format(video, vfh);
	if (ret < 0)
		goto err_iss_video_check_format;

	video->bpl_padding = ret;
	video->bpl_value = vfh->format.fmt.pix.bytesperline;

	/*
	 * Find the ISS video node connected at the far end of the pipeline and
	 * update the pipeline.
	 */
	far_end = iss_video_far_end(video);

	if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		state = ISS_PIPELINE_STREAM_OUTPUT | ISS_PIPELINE_IDLE_OUTPUT;
		pipe->input = far_end;
		pipe->output = video;
	} else {
		if (!far_end) {
			ret = -EPIPE;
			goto err_iss_video_check_format;
		}

		state = ISS_PIPELINE_STREAM_INPUT | ISS_PIPELINE_IDLE_INPUT;
		pipe->input = video;
		pipe->output = far_end;
	}

	spin_lock_irqsave(&pipe->lock, flags);
	pipe->state &= ~ISS_PIPELINE_STREAM;
	pipe->state |= state;
	spin_unlock_irqrestore(&pipe->lock, flags);

	/*
	 * Set the maximum time per frame as the value requested by userspace.
	 * This is a soft limit that can be overridden if the hardware doesn't
	 * support the request limit.
	 */
	if (video->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		pipe->max_timeperframe = vfh->timeperframe;

	video->queue = &vfh->queue;
	INIT_LIST_HEAD(&video->dmaqueue);
	video->error = false;
	atomic_set(&pipe->frame_number, -1);

	ret = vb2_streamon(&vfh->queue, type);
	if (ret < 0)
		goto err_iss_video_check_format;

	/*
	 * In sensor-to-memory mode, the stream can be started synchronously
	 * to the stream on command. In memory-to-memory mode, it will be
	 * started when buffers are queued on both the input and output.
	 */
	if (!pipe->input) {
		unsigned long flags;

		ret = omap4iss_pipeline_set_stream(pipe,
					      ISS_PIPELINE_STREAM_CONTINUOUS);
		if (ret < 0)
			goto err_omap4iss_set_stream;
		spin_lock_irqsave(&video->qlock, flags);
		if (list_empty(&video->dmaqueue))
			video->dmaqueue_flags |= ISS_VIDEO_DMAQUEUE_UNDERRUN;
		spin_unlock_irqrestore(&video->qlock, flags);
	}

	media_graph_walk_cleanup(&graph);

	mutex_unlock(&video->stream_lock);

	return 0;

err_omap4iss_set_stream:
	vb2_streamoff(&vfh->queue, type);
err_iss_video_check_format:
	media_pipeline_stop(&video->video.entity);
err_media_pipeline_start:
	if (video->iss->pdata->set_constraints)
		video->iss->pdata->set_constraints(video->iss, false);
	video->queue = NULL;

	media_graph_walk_cleanup(&graph);

err_graph_walk_init:
	media_entity_enum_cleanup(&pipe->ent_enum);

	mutex_unlock(&video->stream_lock);

	return ret;
}

static int
iss_video_streamoff(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct iss_video_fh *vfh = to_iss_video_fh(fh);
	struct iss_video *video = video_drvdata(file);
	struct iss_pipeline *pipe = to_iss_pipeline(&video->video.entity);
	enum iss_pipeline_state state;
	unsigned long flags;

	if (type != video->type)
		return -EINVAL;

	mutex_lock(&video->stream_lock);

	if (!vb2_is_streaming(&vfh->queue))
		goto done;

	/* Update the pipeline state. */
	if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		state = ISS_PIPELINE_STREAM_OUTPUT
		      | ISS_PIPELINE_QUEUE_OUTPUT;
	else
		state = ISS_PIPELINE_STREAM_INPUT
		      | ISS_PIPELINE_QUEUE_INPUT;

	spin_lock_irqsave(&pipe->lock, flags);
	pipe->state &= ~state;
	spin_unlock_irqrestore(&pipe->lock, flags);

	/* Stop the stream. */
	omap4iss_pipeline_set_stream(pipe, ISS_PIPELINE_STREAM_STOPPED);
	vb2_streamoff(&vfh->queue, type);
	video->queue = NULL;

	media_entity_enum_cleanup(&pipe->ent_enum);

	if (video->iss->pdata->set_constraints)
		video->iss->pdata->set_constraints(video->iss, false);
	media_pipeline_stop(&video->video.entity);

done:
	mutex_unlock(&video->stream_lock);
	return 0;
}

static int
iss_video_enum_input(struct file *file, void *fh, struct v4l2_input *input)
{
	if (input->index > 0)
		return -EINVAL;

	strlcpy(input->name, "camera", sizeof(input->name));
	input->type = V4L2_INPUT_TYPE_CAMERA;

	return 0;
}

static int
iss_video_g_input(struct file *file, void *fh, unsigned int *input)
{
	*input = 0;

	return 0;
}

static int
iss_video_s_input(struct file *file, void *fh, unsigned int input)
{
	return input == 0 ? 0 : -EINVAL;
}

static const struct v4l2_ioctl_ops iss_video_ioctl_ops = {
	.vidioc_querycap		= iss_video_querycap,
	.vidioc_enum_fmt_vid_cap        = iss_video_enum_format,
	.vidioc_g_fmt_vid_cap		= iss_video_get_format,
	.vidioc_s_fmt_vid_cap		= iss_video_set_format,
	.vidioc_try_fmt_vid_cap		= iss_video_try_format,
	.vidioc_g_fmt_vid_out		= iss_video_get_format,
	.vidioc_s_fmt_vid_out		= iss_video_set_format,
	.vidioc_try_fmt_vid_out		= iss_video_try_format,
	.vidioc_g_selection		= iss_video_get_selection,
	.vidioc_s_selection		= iss_video_set_selection,
	.vidioc_g_parm			= iss_video_get_param,
	.vidioc_s_parm			= iss_video_set_param,
	.vidioc_reqbufs			= iss_video_reqbufs,
	.vidioc_querybuf		= iss_video_querybuf,
	.vidioc_qbuf			= iss_video_qbuf,
	.vidioc_expbuf			= iss_video_expbuf,
	.vidioc_dqbuf			= iss_video_dqbuf,
	.vidioc_streamon		= iss_video_streamon,
	.vidioc_streamoff		= iss_video_streamoff,
	.vidioc_enum_input		= iss_video_enum_input,
	.vidioc_g_input			= iss_video_g_input,
	.vidioc_s_input			= iss_video_s_input,
};

/* -----------------------------------------------------------------------------
 * V4L2 file operations
 */

static int iss_video_open(struct file *file)
{
	struct iss_video *video = video_drvdata(file);
	struct iss_video_fh *handle;
	struct vb2_queue *q;
	int ret = 0;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	v4l2_fh_init(&handle->vfh, &video->video);
	v4l2_fh_add(&handle->vfh);

	/* If this is the first user, initialise the pipeline. */
	if (!omap4iss_get(video->iss)) {
		ret = -EBUSY;
		goto done;
	}

	ret = v4l2_pipeline_pm_use(&video->video.entity, 1);
	if (ret < 0) {
		omap4iss_put(video->iss);
		goto done;
	}

	q = &handle->queue;

	q->type = video->type;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = handle;
	q->ops = &iss_video_vb2ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct iss_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->dev = video->iss->dev;

	ret = vb2_queue_init(q);
	if (ret) {
		omap4iss_put(video->iss);
		goto done;
	}

	memset(&handle->format, 0, sizeof(handle->format));
	handle->format.type = video->type;
	handle->timeperframe.denominator = 1;

	handle->video = video;
	file->private_data = &handle->vfh;

done:
	if (ret < 0) {
		v4l2_fh_del(&handle->vfh);
		v4l2_fh_exit(&handle->vfh);
		kfree(handle);
	}

	return ret;
}

static int iss_video_release(struct file *file)
{
	struct iss_video *video = video_drvdata(file);
	struct v4l2_fh *vfh = file->private_data;
	struct iss_video_fh *handle = to_iss_video_fh(vfh);

	/* Disable streaming and free the buffers queue resources. */
	iss_video_streamoff(file, vfh, video->type);

	v4l2_pipeline_pm_use(&video->video.entity, 0);

	/* Release the videobuf2 queue */
	vb2_queue_release(&handle->queue);

	v4l2_fh_del(vfh);
	v4l2_fh_exit(vfh);
	kfree(handle);
	file->private_data = NULL;

	omap4iss_put(video->iss);

	return 0;
}

static __poll_t iss_video_poll(struct file *file, poll_table *wait)
{
	struct iss_video_fh *vfh = to_iss_video_fh(file->private_data);

	return vb2_poll(&vfh->queue, file, wait);
}

static int iss_video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct iss_video_fh *vfh = to_iss_video_fh(file->private_data);

	return vb2_mmap(&vfh->queue, vma);
}

static const struct v4l2_file_operations iss_video_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = iss_video_open,
	.release = iss_video_release,
	.poll = iss_video_poll,
	.mmap = iss_video_mmap,
};

/* -----------------------------------------------------------------------------
 * ISS video core
 */

static const struct iss_video_operations iss_video_dummy_ops = {
};

int omap4iss_video_init(struct iss_video *video, const char *name)
{
	const char *direction;
	int ret;

	switch (video->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		direction = "output";
		video->pad.flags = MEDIA_PAD_FL_SINK;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		direction = "input";
		video->pad.flags = MEDIA_PAD_FL_SOURCE;
		break;

	default:
		return -EINVAL;
	}

	ret = media_entity_pads_init(&video->video.entity, 1, &video->pad);
	if (ret < 0)
		return ret;

	spin_lock_init(&video->qlock);
	mutex_init(&video->mutex);
	atomic_set(&video->active, 0);

	spin_lock_init(&video->pipe.lock);
	mutex_init(&video->stream_lock);

	/* Initialize the video device. */
	if (!video->ops)
		video->ops = &iss_video_dummy_ops;

	video->video.fops = &iss_video_fops;
	snprintf(video->video.name, sizeof(video->video.name),
		 "OMAP4 ISS %s %s", name, direction);
	video->video.vfl_type = VFL_TYPE_GRABBER;
	video->video.release = video_device_release_empty;
	video->video.ioctl_ops = &iss_video_ioctl_ops;
	video->pipe.stream_state = ISS_PIPELINE_STREAM_STOPPED;

	video_set_drvdata(&video->video, video);

	return 0;
}

void omap4iss_video_cleanup(struct iss_video *video)
{
	media_entity_cleanup(&video->video.entity);
	mutex_destroy(&video->stream_lock);
	mutex_destroy(&video->mutex);
}

int omap4iss_video_register(struct iss_video *video, struct v4l2_device *vdev)
{
	int ret;

	video->video.v4l2_dev = vdev;

	ret = video_register_device(&video->video, VFL_TYPE_GRABBER, -1);
	if (ret < 0)
		dev_err(video->iss->dev,
			"could not register video device (%d)\n", ret);

	return ret;
}

void omap4iss_video_unregister(struct iss_video *video)
{
	video_unregister_device(&video->video);
}
