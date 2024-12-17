// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Video DMA
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/dma/xilinx_dma.h>
#include <linux/lcm.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "xilinx-dma.h"
#include "xilinx-vip.h"
#include "xilinx-vipp.h"

#define XVIP_DMA_DEF_WIDTH		1920
#define XVIP_DMA_DEF_HEIGHT		1080

/* Minimum and maximum widths are expressed in bytes */
#define XVIP_DMA_MIN_WIDTH		1U
#define XVIP_DMA_MAX_WIDTH		65535U
#define XVIP_DMA_MIN_HEIGHT		1U
#define XVIP_DMA_MAX_HEIGHT		8191U

/* -----------------------------------------------------------------------------
 * Helper functions
 */

static struct v4l2_subdev *
xvip_dma_remote_subdev(struct media_pad *local, u32 *pad)
{
	struct media_pad *remote;

	remote = media_pad_remote_pad_first(local);
	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

static int xvip_dma_verify_format(struct xvip_dma *dma)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_subdev *subdev;
	int ret;

	subdev = xvip_dma_remote_subdev(&dma->pad, &fmt.pad);
	if (subdev == NULL)
		return -EPIPE;

	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret < 0)
		return ret == -ENOIOCTLCMD ? -EINVAL : ret;

	if (dma->fmtinfo->code != fmt.format.code ||
	    dma->format.height != fmt.format.height ||
	    dma->format.width != fmt.format.width ||
	    dma->format.colorspace != fmt.format.colorspace)
		return -EINVAL;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Pipeline Stream Management
 */

/**
 * xvip_pipeline_start_stop - Start ot stop streaming on a pipeline
 * @pipe: The pipeline
 * @start: Start (when true) or stop (when false) the pipeline
 *
 * Walk the entities chain starting at the pipeline output video node and start
 * or stop all of them.
 *
 * Return: 0 if successful, or the return value of the failed video::s_stream
 * operation otherwise.
 */
static int xvip_pipeline_start_stop(struct xvip_pipeline *pipe, bool start)
{
	struct xvip_dma *dma = pipe->output;
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	int ret;

	entity = &dma->video.entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_pad_remote_pad_first(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, video, s_stream, start);
		if (start && ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
	}

	return 0;
}

/**
 * xvip_pipeline_set_stream - Enable/disable streaming on a pipeline
 * @pipe: The pipeline
 * @on: Turn the stream on when true or off when false
 *
 * The pipeline is shared between all DMA engines connect at its input and
 * output. While the stream state of DMA engines can be controlled
 * independently, pipelines have a shared stream state that enable or disable
 * all entities in the pipeline. For this reason the pipeline uses a streaming
 * counter that tracks the number of DMA engines that have requested the stream
 * to be enabled.
 *
 * When called with the @on argument set to true, this function will increment
 * the pipeline streaming count. If the streaming count reaches the number of
 * DMA engines in the pipeline it will enable all entities that belong to the
 * pipeline.
 *
 * Similarly, when called with the @on argument set to false, this function will
 * decrement the pipeline streaming count and disable all entities in the
 * pipeline when the streaming count reaches zero.
 *
 * Return: 0 if successful, or the return value of the failed video::s_stream
 * operation otherwise. Stopping the pipeline never fails. The pipeline state is
 * not updated when the operation fails.
 */
static int xvip_pipeline_set_stream(struct xvip_pipeline *pipe, bool on)
{
	int ret = 0;

	mutex_lock(&pipe->lock);

	if (on) {
		if (pipe->stream_count == pipe->num_dmas - 1) {
			ret = xvip_pipeline_start_stop(pipe, true);
			if (ret < 0)
				goto done;
		}
		pipe->stream_count++;
	} else {
		if (--pipe->stream_count == 0)
			xvip_pipeline_start_stop(pipe, false);
	}

done:
	mutex_unlock(&pipe->lock);
	return ret;
}

static int xvip_pipeline_validate(struct xvip_pipeline *pipe,
				  struct xvip_dma *start)
{
	struct media_pipeline_pad_iter iter;
	unsigned int num_inputs = 0;
	unsigned int num_outputs = 0;
	struct media_pad *pad;

	/* Locate the video nodes in the pipeline. */
	media_pipeline_for_each_pad(&pipe->pipe, &iter, pad) {
		struct xvip_dma *dma;

		if (pad->entity->function != MEDIA_ENT_F_IO_V4L)
			continue;

		dma = to_xvip_dma(media_entity_to_video_device(pad->entity));

		if (dma->pad.flags & MEDIA_PAD_FL_SINK) {
			pipe->output = dma;
			num_outputs++;
		} else {
			num_inputs++;
		}
	}

	/* We need exactly one output and zero or one input. */
	if (num_outputs != 1 || num_inputs > 1)
		return -EPIPE;

	pipe->num_dmas = num_inputs + num_outputs;

	return 0;
}

static void __xvip_pipeline_cleanup(struct xvip_pipeline *pipe)
{
	pipe->num_dmas = 0;
	pipe->output = NULL;
}

/**
 * xvip_pipeline_cleanup - Cleanup the pipeline after streaming
 * @pipe: the pipeline
 *
 * Decrease the pipeline use count and clean it up if we were the last user.
 */
static void xvip_pipeline_cleanup(struct xvip_pipeline *pipe)
{
	mutex_lock(&pipe->lock);

	/* If we're the last user clean up the pipeline. */
	if (--pipe->use_count == 0)
		__xvip_pipeline_cleanup(pipe);

	mutex_unlock(&pipe->lock);
}

/**
 * xvip_pipeline_prepare - Prepare the pipeline for streaming
 * @pipe: the pipeline
 * @dma: DMA engine at one end of the pipeline
 *
 * Validate the pipeline if no user exists yet, otherwise just increase the use
 * count.
 *
 * Return: 0 if successful or -EPIPE if the pipeline is not valid.
 */
static int xvip_pipeline_prepare(struct xvip_pipeline *pipe,
				 struct xvip_dma *dma)
{
	int ret;

	mutex_lock(&pipe->lock);

	/* If we're the first user validate and initialize the pipeline. */
	if (pipe->use_count == 0) {
		ret = xvip_pipeline_validate(pipe, dma);
		if (ret < 0) {
			__xvip_pipeline_cleanup(pipe);
			goto done;
		}
	}

	pipe->use_count++;
	ret = 0;

done:
	mutex_unlock(&pipe->lock);
	return ret;
}

/* -----------------------------------------------------------------------------
 * videobuf2 queue operations
 */

/**
 * struct xvip_dma_buffer - Video DMA buffer
 * @buf: vb2 buffer base object
 * @queue: buffer list entry in the DMA engine queued buffers list
 * @dma: DMA channel that uses the buffer
 */
struct xvip_dma_buffer {
	struct vb2_v4l2_buffer buf;
	struct list_head queue;
	struct xvip_dma *dma;
};

#define to_xvip_dma_buffer(vb)	container_of(vb, struct xvip_dma_buffer, buf)

static void xvip_dma_complete(void *param)
{
	struct xvip_dma_buffer *buf = param;
	struct xvip_dma *dma = buf->dma;

	spin_lock(&dma->queued_lock);
	list_del(&buf->queue);
	spin_unlock(&dma->queued_lock);

	buf->buf.field = V4L2_FIELD_NONE;
	buf->buf.sequence = dma->sequence++;
	buf->buf.vb2_buf.timestamp = ktime_get_ns();
	vb2_set_plane_payload(&buf->buf.vb2_buf, 0, dma->format.sizeimage);
	vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_DONE);
}

static int
xvip_dma_queue_setup(struct vb2_queue *vq,
		     unsigned int *nbuffers, unsigned int *nplanes,
		     unsigned int sizes[], struct device *alloc_devs[])
{
	struct xvip_dma *dma = vb2_get_drv_priv(vq);

	/* Make sure the image size is large enough. */
	if (*nplanes)
		return sizes[0] < dma->format.sizeimage ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = dma->format.sizeimage;

	return 0;
}

static int xvip_dma_buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct xvip_dma *dma = vb2_get_drv_priv(vb->vb2_queue);
	struct xvip_dma_buffer *buf = to_xvip_dma_buffer(vbuf);

	buf->dma = dma;

	return 0;
}

static void xvip_dma_buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct xvip_dma *dma = vb2_get_drv_priv(vb->vb2_queue);
	struct xvip_dma_buffer *buf = to_xvip_dma_buffer(vbuf);
	struct dma_async_tx_descriptor *desc;
	dma_addr_t addr = vb2_dma_contig_plane_dma_addr(vb, 0);
	u32 flags;

	if (dma->queue.type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;
		dma->xt.dir = DMA_DEV_TO_MEM;
		dma->xt.src_sgl = false;
		dma->xt.dst_sgl = true;
		dma->xt.dst_start = addr;
	} else {
		flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;
		dma->xt.dir = DMA_MEM_TO_DEV;
		dma->xt.src_sgl = true;
		dma->xt.dst_sgl = false;
		dma->xt.src_start = addr;
	}

	dma->xt.frame_size = 1;
	dma->sgl.size = dma->format.width * dma->fmtinfo->bpp;
	dma->sgl.icg = dma->format.bytesperline - dma->sgl.size;
	dma->xt.numf = dma->format.height;

	desc = dmaengine_prep_interleaved_dma(dma->dma, &dma->xt, flags);
	if (!desc) {
		dev_err(dma->xdev->dev, "Failed to prepare DMA transfer\n");
		vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_ERROR);
		return;
	}
	desc->callback = xvip_dma_complete;
	desc->callback_param = buf;

	spin_lock_irq(&dma->queued_lock);
	list_add_tail(&buf->queue, &dma->queued_bufs);
	spin_unlock_irq(&dma->queued_lock);

	dmaengine_submit(desc);

	if (vb2_is_streaming(&dma->queue))
		dma_async_issue_pending(dma->dma);
}

static int xvip_dma_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct xvip_dma *dma = vb2_get_drv_priv(vq);
	struct xvip_dma_buffer *buf, *nbuf;
	struct xvip_pipeline *pipe;
	int ret;

	dma->sequence = 0;

	/*
	 * Start streaming on the pipeline. No link touching an entity in the
	 * pipeline can be activated or deactivated once streaming is started.
	 *
	 * Use the pipeline object embedded in the first DMA object that starts
	 * streaming.
	 */
	pipe = to_xvip_pipeline(&dma->video) ? : &dma->pipe;

	ret = video_device_pipeline_start(&dma->video, &pipe->pipe);
	if (ret < 0)
		goto error;

	/* Verify that the configured format matches the output of the
	 * connected subdev.
	 */
	ret = xvip_dma_verify_format(dma);
	if (ret < 0)
		goto error_stop;

	ret = xvip_pipeline_prepare(pipe, dma);
	if (ret < 0)
		goto error_stop;

	/* Start the DMA engine. This must be done before starting the blocks
	 * in the pipeline to avoid DMA synchronization issues.
	 */
	dma_async_issue_pending(dma->dma);

	/* Start the pipeline. */
	xvip_pipeline_set_stream(pipe, true);

	return 0;

error_stop:
	video_device_pipeline_stop(&dma->video);

error:
	/* Give back all queued buffers to videobuf2. */
	spin_lock_irq(&dma->queued_lock);
	list_for_each_entry_safe(buf, nbuf, &dma->queued_bufs, queue) {
		vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_QUEUED);
		list_del(&buf->queue);
	}
	spin_unlock_irq(&dma->queued_lock);

	return ret;
}

static void xvip_dma_stop_streaming(struct vb2_queue *vq)
{
	struct xvip_dma *dma = vb2_get_drv_priv(vq);
	struct xvip_pipeline *pipe = to_xvip_pipeline(&dma->video);
	struct xvip_dma_buffer *buf, *nbuf;

	/* Stop the pipeline. */
	xvip_pipeline_set_stream(pipe, false);

	/* Stop and reset the DMA engine. */
	dmaengine_terminate_all(dma->dma);

	/* Cleanup the pipeline and mark it as being stopped. */
	xvip_pipeline_cleanup(pipe);
	video_device_pipeline_stop(&dma->video);

	/* Give back all queued buffers to videobuf2. */
	spin_lock_irq(&dma->queued_lock);
	list_for_each_entry_safe(buf, nbuf, &dma->queued_bufs, queue) {
		vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_ERROR);
		list_del(&buf->queue);
	}
	spin_unlock_irq(&dma->queued_lock);
}

static const struct vb2_ops xvip_dma_queue_qops = {
	.queue_setup = xvip_dma_queue_setup,
	.buf_prepare = xvip_dma_buffer_prepare,
	.buf_queue = xvip_dma_buffer_queue,
	.start_streaming = xvip_dma_start_streaming,
	.stop_streaming = xvip_dma_stop_streaming,
};

/* -----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int
xvip_dma_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);

	cap->capabilities = dma->xdev->v4l2_caps | V4L2_CAP_STREAMING |
			    V4L2_CAP_DEVICE_CAPS;

	strscpy(cap->driver, "xilinx-vipp", sizeof(cap->driver));
	strscpy(cap->card, dma->video.name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%pOFn:%u",
		 dma->xdev->dev->of_node, dma->port);

	return 0;
}

/* FIXME: without this callback function, some applications are not configured
 * with correct formats, and it results in frames in wrong format. Whether this
 * callback needs to be required is not clearly defined, so it should be
 * clarified through the mailing list.
 */
static int
xvip_dma_enum_format(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);

	if (f->index > 0)
		return -EINVAL;

	f->pixelformat = dma->format.pixelformat;

	return 0;
}

static int
xvip_dma_get_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);

	format->fmt.pix = dma->format;

	return 0;
}

static void
__xvip_dma_try_format(struct xvip_dma *dma, struct v4l2_pix_format *pix,
		      const struct xvip_video_format **fmtinfo)
{
	const struct xvip_video_format *info;
	unsigned int min_width;
	unsigned int max_width;
	unsigned int min_bpl;
	unsigned int max_bpl;
	unsigned int width;
	unsigned int align;
	unsigned int bpl;

	/* Retrieve format information and select the default format if the
	 * requested format isn't supported.
	 */
	info = xvip_get_format_by_fourcc(pix->pixelformat);

	pix->pixelformat = info->fourcc;
	pix->field = V4L2_FIELD_NONE;

	/* The transfer alignment requirements are expressed in bytes. Compute
	 * the minimum and maximum values, clamp the requested width and convert
	 * it back to pixels.
	 */
	align = lcm(dma->align, info->bpp);
	min_width = roundup(XVIP_DMA_MIN_WIDTH, align);
	max_width = rounddown(XVIP_DMA_MAX_WIDTH, align);
	width = rounddown(pix->width * info->bpp, align);

	pix->width = clamp(width, min_width, max_width) / info->bpp;
	pix->height = clamp(pix->height, XVIP_DMA_MIN_HEIGHT,
			    XVIP_DMA_MAX_HEIGHT);

	/* Clamp the requested bytes per line value. If the maximum bytes per
	 * line value is zero, the module doesn't support user configurable line
	 * sizes. Override the requested value with the minimum in that case.
	 */
	min_bpl = pix->width * info->bpp;
	max_bpl = rounddown(XVIP_DMA_MAX_WIDTH, dma->align);
	bpl = rounddown(pix->bytesperline, dma->align);

	pix->bytesperline = clamp(bpl, min_bpl, max_bpl);
	pix->sizeimage = pix->bytesperline * pix->height;

	if (fmtinfo)
		*fmtinfo = info;
}

static int
xvip_dma_try_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);

	__xvip_dma_try_format(dma, &format->fmt.pix, NULL);
	return 0;
}

static int
xvip_dma_set_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	const struct xvip_video_format *info;

	__xvip_dma_try_format(dma, &format->fmt.pix, &info);

	if (vb2_is_busy(&dma->queue))
		return -EBUSY;

	dma->format = format->fmt.pix;
	dma->fmtinfo = info;

	return 0;
}

static const struct v4l2_ioctl_ops xvip_dma_ioctl_ops = {
	.vidioc_querycap		= xvip_dma_querycap,
	.vidioc_enum_fmt_vid_cap	= xvip_dma_enum_format,
	.vidioc_g_fmt_vid_cap		= xvip_dma_get_format,
	.vidioc_g_fmt_vid_out		= xvip_dma_get_format,
	.vidioc_s_fmt_vid_cap		= xvip_dma_set_format,
	.vidioc_s_fmt_vid_out		= xvip_dma_set_format,
	.vidioc_try_fmt_vid_cap		= xvip_dma_try_format,
	.vidioc_try_fmt_vid_out		= xvip_dma_try_format,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------------
 * V4L2 file operations
 */

static const struct v4l2_file_operations xvip_dma_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
};

/* -----------------------------------------------------------------------------
 * Xilinx Video DMA Core
 */

int xvip_dma_init(struct xvip_composite_device *xdev, struct xvip_dma *dma,
		  enum v4l2_buf_type type, unsigned int port)
{
	char name[16];
	int ret;

	dma->xdev = xdev;
	dma->port = port;
	mutex_init(&dma->lock);
	mutex_init(&dma->pipe.lock);
	INIT_LIST_HEAD(&dma->queued_bufs);
	spin_lock_init(&dma->queued_lock);

	dma->fmtinfo = xvip_get_format_by_fourcc(V4L2_PIX_FMT_YUYV);
	dma->format.pixelformat = dma->fmtinfo->fourcc;
	dma->format.colorspace = V4L2_COLORSPACE_SRGB;
	dma->format.field = V4L2_FIELD_NONE;
	dma->format.width = XVIP_DMA_DEF_WIDTH;
	dma->format.height = XVIP_DMA_DEF_HEIGHT;
	dma->format.bytesperline = dma->format.width * dma->fmtinfo->bpp;
	dma->format.sizeimage = dma->format.bytesperline * dma->format.height;

	/* Initialize the media entity... */
	dma->pad.flags = type == V4L2_BUF_TYPE_VIDEO_CAPTURE
		       ? MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&dma->video.entity, 1, &dma->pad);
	if (ret < 0)
		goto error;

	/* ... and the video node... */
	dma->video.fops = &xvip_dma_fops;
	dma->video.v4l2_dev = &xdev->v4l2_dev;
	dma->video.queue = &dma->queue;
	snprintf(dma->video.name, sizeof(dma->video.name), "%pOFn %s %u",
		 xdev->dev->of_node,
		 type == V4L2_BUF_TYPE_VIDEO_CAPTURE ? "output" : "input",
		 port);
	dma->video.vfl_type = VFL_TYPE_VIDEO;
	dma->video.vfl_dir = type == V4L2_BUF_TYPE_VIDEO_CAPTURE
			   ? VFL_DIR_RX : VFL_DIR_TX;
	dma->video.release = video_device_release_empty;
	dma->video.ioctl_ops = &xvip_dma_ioctl_ops;
	dma->video.lock = &dma->lock;
	dma->video.device_caps = V4L2_CAP_STREAMING;
	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		dma->video.device_caps |= V4L2_CAP_VIDEO_CAPTURE;
	else
		dma->video.device_caps |= V4L2_CAP_VIDEO_OUTPUT;

	video_set_drvdata(&dma->video, dma);

	/* ... and the buffers queue... */
	/* Don't enable VB2_READ and VB2_WRITE, as using the read() and write()
	 * V4L2 APIs would be inefficient. Testing on the command line with a
	 * 'cat /dev/video?' thus won't be possible, but given that the driver
	 * anyway requires a test tool to setup the pipeline before any video
	 * stream can be started, requiring a specific V4L2 test tool as well
	 * instead of 'cat' isn't really a drawback.
	 */
	dma->queue.type = type;
	dma->queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dma->queue.lock = &dma->lock;
	dma->queue.drv_priv = dma;
	dma->queue.buf_struct_size = sizeof(struct xvip_dma_buffer);
	dma->queue.ops = &xvip_dma_queue_qops;
	dma->queue.mem_ops = &vb2_dma_contig_memops;
	dma->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC
				   | V4L2_BUF_FLAG_TSTAMP_SRC_EOF;
	dma->queue.dev = dma->xdev->dev;
	ret = vb2_queue_init(&dma->queue);
	if (ret < 0) {
		dev_err(dma->xdev->dev, "failed to initialize VB2 queue\n");
		goto error;
	}

	/* ... and the DMA channel. */
	snprintf(name, sizeof(name), "port%u", port);
	dma->dma = dma_request_chan(dma->xdev->dev, name);
	if (IS_ERR(dma->dma)) {
		ret = dev_err_probe(dma->xdev->dev, PTR_ERR(dma->dma),
				    "no VDMA channel found\n");
		goto error;
	}

	dma->align = 1 << dma->dma->device->copy_align;

	ret = video_register_device(&dma->video, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		dev_err(dma->xdev->dev, "failed to register video device\n");
		goto error;
	}

	return 0;

error:
	xvip_dma_cleanup(dma);
	return ret;
}

void xvip_dma_cleanup(struct xvip_dma *dma)
{
	if (video_is_registered(&dma->video))
		video_unregister_device(&dma->video);

	if (!IS_ERR_OR_NULL(dma->dma))
		dma_release_channel(dma->dma);

	media_entity_cleanup(&dma->video.entity);

	mutex_destroy(&dma->lock);
	mutex_destroy(&dma->pipe.lock);
}
