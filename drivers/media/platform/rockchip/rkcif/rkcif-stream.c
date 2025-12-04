// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Camera Interface (CIF) Driver
 *
 * Copyright (C) 2025 Michael Riesch <michael.riesch@wolfvision.net>
 * Copyright (C) 2025 Collabora, Ltd.
 */

#include <linux/pm_runtime.h>

#include <media/v4l2-common.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include "rkcif-common.h"
#include "rkcif-stream.h"

#define CIF_REQ_BUFS_MIN 1
#define CIF_MIN_WIDTH	 64
#define CIF_MIN_HEIGHT	 64
#define CIF_MAX_WIDTH	 8192
#define CIF_MAX_HEIGHT	 8192

static inline struct rkcif_buffer *to_rkcif_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct rkcif_buffer, vb);
}

static inline struct rkcif_stream *to_rkcif_stream(struct video_device *vdev)
{
	return container_of(vdev, struct rkcif_stream, vdev);
}

static struct rkcif_buffer *rkcif_stream_pop_buffer(struct rkcif_stream *stream)
{
	struct rkcif_buffer *buffer;

	guard(spinlock_irqsave)(&stream->driver_queue_lock);

	if (list_empty(&stream->driver_queue))
		return NULL;

	buffer = list_first_entry(&stream->driver_queue, struct rkcif_buffer,
				  queue);
	list_del(&buffer->queue);

	return buffer;
}

static void rkcif_stream_push_buffer(struct rkcif_stream *stream,
				     struct rkcif_buffer *buffer)
{
	guard(spinlock_irqsave)(&stream->driver_queue_lock);

	list_add_tail(&buffer->queue, &stream->driver_queue);
}

static inline void rkcif_stream_return_buffer(struct rkcif_buffer *buffer,
					      enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *vb = &buffer->vb;

	vb2_buffer_done(&vb->vb2_buf, state);
}

static void rkcif_stream_complete_buffer(struct rkcif_stream *stream,
					 struct rkcif_buffer *buffer)
{
	struct vb2_v4l2_buffer *vb = &buffer->vb;

	vb->vb2_buf.timestamp = ktime_get_ns();
	vb->sequence = stream->frame_idx;
	vb2_buffer_done(&vb->vb2_buf, VB2_BUF_STATE_DONE);
	stream->frame_idx++;
}

void rkcif_stream_pingpong(struct rkcif_stream *stream)
{
	struct rkcif_buffer *buffer;

	buffer = stream->buffers[stream->frame_phase];
	if (!buffer->is_dummy)
		rkcif_stream_complete_buffer(stream, buffer);

	buffer = rkcif_stream_pop_buffer(stream);
	if (buffer) {
		stream->buffers[stream->frame_phase] = buffer;
		stream->buffers[stream->frame_phase]->is_dummy = false;
	} else {
		stream->buffers[stream->frame_phase] = &stream->dummy.buffer;
		stream->buffers[stream->frame_phase]->is_dummy = true;
		dev_dbg(stream->rkcif->dev,
			"no buffer available, frame will be dropped\n");
	}

	if (stream->queue_buffer)
		stream->queue_buffer(stream, stream->frame_phase);

	stream->frame_phase = 1 - stream->frame_phase;
}

static int rkcif_stream_init_buffers(struct rkcif_stream *stream)
{
	struct v4l2_pix_format_mplane *pix = &stream->pix;

	stream->buffers[0] = rkcif_stream_pop_buffer(stream);
	if (!stream->buffers[0])
		goto err_buff_0;

	stream->buffers[1] = rkcif_stream_pop_buffer(stream);
	if (!stream->buffers[1])
		goto err_buff_1;

	if (stream->queue_buffer) {
		stream->queue_buffer(stream, 0);
		stream->queue_buffer(stream, 1);
	}

	stream->dummy.size = pix->num_planes * pix->plane_fmt[0].sizeimage;
	stream->dummy.vaddr =
		dma_alloc_attrs(stream->rkcif->dev, stream->dummy.size,
				&stream->dummy.buffer.buff_addr[0], GFP_KERNEL,
				DMA_ATTR_NO_KERNEL_MAPPING);
	if (!stream->dummy.vaddr)
		goto err_dummy;

	for (unsigned int i = 1; i < pix->num_planes; i++)
		stream->dummy.buffer.buff_addr[i] =
			stream->dummy.buffer.buff_addr[i - 1] +
			pix->plane_fmt[i - 1].bytesperline * pix->height;

	return 0;

err_dummy:
	rkcif_stream_return_buffer(stream->buffers[1], VB2_BUF_STATE_QUEUED);
	stream->buffers[1] = NULL;

err_buff_1:
	rkcif_stream_return_buffer(stream->buffers[0], VB2_BUF_STATE_QUEUED);
	stream->buffers[0] = NULL;
err_buff_0:
	return -EINVAL;
}

static void rkcif_stream_return_all_buffers(struct rkcif_stream *stream,
					    enum vb2_buffer_state state)
{
	struct rkcif_buffer *buffer;

	if (stream->buffers[0] && !stream->buffers[0]->is_dummy) {
		rkcif_stream_return_buffer(stream->buffers[0], state);
		stream->buffers[0] = NULL;
	}

	if (stream->buffers[1] && !stream->buffers[1]->is_dummy) {
		rkcif_stream_return_buffer(stream->buffers[1], state);
		stream->buffers[1] = NULL;
	}

	while ((buffer = rkcif_stream_pop_buffer(stream)))
		rkcif_stream_return_buffer(buffer, state);

	if (stream->dummy.vaddr) {
		dma_free_attrs(stream->rkcif->dev, stream->dummy.size,
			       stream->dummy.vaddr,
			       stream->dummy.buffer.buff_addr[0],
			       DMA_ATTR_NO_KERNEL_MAPPING);
		stream->dummy.vaddr = NULL;
	}
}

static int rkcif_stream_setup_queue(struct vb2_queue *queue,
				    unsigned int *num_buffers,
				    unsigned int *num_planes,
				    unsigned int sizes[],
				    struct device *alloc_devs[])
{
	struct rkcif_stream *stream = queue->drv_priv;
	struct v4l2_pix_format_mplane *pix = &stream->pix;

	if (*num_planes) {
		if (*num_planes != pix->num_planes)
			return -EINVAL;

		for (unsigned int i = 0; i < pix->num_planes; i++)
			if (sizes[i] < pix->plane_fmt[i].sizeimage)
				return -EINVAL;
	} else {
		*num_planes = pix->num_planes;
		for (unsigned int i = 0; i < pix->num_planes; i++)
			sizes[i] = pix->plane_fmt[i].sizeimage;
	}

	return 0;
}

static int rkcif_stream_prepare_buffer(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkcif_buffer *buffer = to_rkcif_buffer(vbuf);
	struct rkcif_stream *stream = vb->vb2_queue->drv_priv;
	const struct rkcif_output_fmt *fmt;
	struct v4l2_pix_format_mplane *pix = &stream->pix;
	unsigned int i;

	memset(buffer->buff_addr, 0, sizeof(buffer->buff_addr));
	for (i = 0; i < pix->num_planes; i++)
		buffer->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);

	/* apply fallback for non-mplane formats, if required */
	if (pix->num_planes == 1) {
		fmt = rkcif_stream_find_output_fmt(stream, true,
						   pix->pixelformat);
		for (i = 1; i < fmt->cplanes; i++)
			buffer->buff_addr[i] =
				buffer->buff_addr[i - 1] +
				pix->plane_fmt[i - 1].bytesperline *
					pix->height;
	}

	for (i = 0; i < pix->num_planes; i++) {
		unsigned long size = pix->plane_fmt[i].sizeimage;

		if (vb2_plane_size(vb, i) < size) {
			dev_err(stream->rkcif->dev,
				"user buffer too small (%ld < %ld)\n",
				vb2_plane_size(vb, i), size);
			return -EINVAL;
		}

		vb2_set_plane_payload(vb, i, size);
	}

	vbuf->field = V4L2_FIELD_NONE;

	return 0;
}

static void rkcif_stream_queue_buffer(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkcif_buffer *buffer = to_rkcif_buffer(vbuf);
	struct rkcif_stream *stream = vb->vb2_queue->drv_priv;

	rkcif_stream_push_buffer(stream, buffer);
}

static int rkcif_stream_start_streaming(struct vb2_queue *queue,
					unsigned int count)
{
	struct rkcif_stream *stream = queue->drv_priv;
	struct rkcif_device *rkcif = stream->rkcif;
	u64 mask;
	int ret;

	stream->frame_idx = 0;
	stream->frame_phase = 0;

	ret = video_device_pipeline_start(&stream->vdev, &stream->pipeline);
	if (ret) {
		dev_err(rkcif->dev, "failed to start pipeline %d\n", ret);
		goto err_out;
	}

	ret = pm_runtime_resume_and_get(rkcif->dev);
	if (ret < 0) {
		dev_err(rkcif->dev, "failed to get runtime pm, %d\n", ret);
		goto err_pipeline_stop;
	}

	ret = rkcif_stream_init_buffers(stream);
	if (ret)
		goto err_runtime_put;

	if (stream->start_streaming) {
		ret = stream->start_streaming(stream);
		if (ret < 0)
			goto err_runtime_put;
	}

	mask = BIT_ULL(stream->id);
	ret = v4l2_subdev_enable_streams(&stream->interface->sd,
					 RKCIF_IF_PAD_SRC, mask);
	if (ret < 0)
		goto err_stop_stream;

	return 0;

err_stop_stream:
	if (stream->stop_streaming)
		stream->stop_streaming(stream);
err_runtime_put:
	pm_runtime_put(rkcif->dev);
err_pipeline_stop:
	video_device_pipeline_stop(&stream->vdev);
err_out:
	rkcif_stream_return_all_buffers(stream, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void rkcif_stream_stop_streaming(struct vb2_queue *queue)
{
	struct rkcif_stream *stream = queue->drv_priv;
	struct rkcif_device *rkcif = stream->rkcif;
	u64 mask;
	int ret;

	mask = BIT_ULL(stream->id);
	v4l2_subdev_disable_streams(&stream->interface->sd, RKCIF_IF_PAD_SRC,
				    mask);

	stream->stopping = true;
	ret = wait_event_timeout(stream->wq_stopped, !stream->stopping,
				 msecs_to_jiffies(1000));

	if (!ret && stream->stop_streaming)
		stream->stop_streaming(stream);

	pm_runtime_put(rkcif->dev);

	rkcif_stream_return_all_buffers(stream, VB2_BUF_STATE_ERROR);

	video_device_pipeline_stop(&stream->vdev);
}

static const struct vb2_ops rkcif_stream_vb2_ops = {
	.queue_setup = rkcif_stream_setup_queue,
	.buf_prepare = rkcif_stream_prepare_buffer,
	.buf_queue = rkcif_stream_queue_buffer,
	.start_streaming = rkcif_stream_start_streaming,
	.stop_streaming = rkcif_stream_stop_streaming,
};

static int rkcif_stream_fill_format(struct rkcif_stream *stream,
				    struct v4l2_pix_format_mplane *pix)
{
	const struct rkcif_output_fmt *fmt;
	u32 height, width;
	int ret;

	fmt = rkcif_stream_find_output_fmt(stream, true, pix->pixelformat);
	height = clamp_t(u32, pix->height, CIF_MIN_HEIGHT, CIF_MAX_HEIGHT);
	width = clamp_t(u32, pix->width, CIF_MIN_WIDTH, CIF_MAX_WIDTH);
	ret = v4l2_fill_pixfmt_mp(pix, fmt->fourcc, width, height);
	if (ret)
		return ret;

	pix->field = V4L2_FIELD_NONE;

	return 0;
}

static int rkcif_stream_try_format(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct rkcif_stream *stream = video_drvdata(file);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;

	return rkcif_stream_fill_format(stream, pix);
}

static int rkcif_stream_set_format(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct rkcif_stream *stream = video_drvdata(file);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	int ret;

	if (vb2_is_busy(&stream->buf_queue))
		return -EBUSY;

	ret = rkcif_stream_try_format(file, priv, f);
	if (ret)
		return ret;

	stream->pix = *pix;

	return 0;
}

static int rkcif_stream_get_format(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct rkcif_stream *stream = video_drvdata(file);

	f->fmt.pix_mp = stream->pix;

	return 0;
}

static int rkcif_stream_enum_formats(struct file *file, void *priv,
				     struct v4l2_fmtdesc *f)
{
	struct rkcif_stream *stream = video_drvdata(file);

	if (f->index >= stream->out_fmts_num)
		return -EINVAL;

	f->pixelformat = stream->out_fmts[f->index].fourcc;

	return 0;
}

static int rkcif_stream_enum_framesizes(struct file *file, void *fh,
					struct v4l2_frmsizeenum *fsize)
{
	struct rkcif_stream *stream = video_drvdata(file);

	if (fsize->index > 0)
		return -EINVAL;

	if (!rkcif_stream_find_output_fmt(stream, false, fsize->pixel_format))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = CIF_MIN_WIDTH;
	fsize->stepwise.max_width = CIF_MAX_WIDTH;
	fsize->stepwise.step_width = 8;
	fsize->stepwise.min_height = CIF_MIN_HEIGHT;
	fsize->stepwise.max_height = CIF_MAX_HEIGHT;
	fsize->stepwise.step_height = 8;

	return 0;
}

static int rkcif_stream_querycap(struct file *file, void *priv,
				 struct v4l2_capability *cap)
{
	struct rkcif_stream *stream = video_drvdata(file);
	struct device *dev = stream->rkcif->dev;

	strscpy(cap->driver, dev->driver->name, sizeof(cap->driver));
	strscpy(cap->card, dev->driver->name, sizeof(cap->card));

	return 0;
}

static const struct v4l2_ioctl_ops rkcif_stream_ioctl_ops = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_try_fmt_vid_cap_mplane = rkcif_stream_try_format,
	.vidioc_s_fmt_vid_cap_mplane = rkcif_stream_set_format,
	.vidioc_g_fmt_vid_cap_mplane = rkcif_stream_get_format,
	.vidioc_enum_fmt_vid_cap = rkcif_stream_enum_formats,
	.vidioc_enum_framesizes = rkcif_stream_enum_framesizes,
	.vidioc_querycap = rkcif_stream_querycap,
};

static int rkcif_stream_link_validate(struct media_link *link)
{
	struct video_device *vdev =
		media_entity_to_video_device(link->sink->entity);
	struct v4l2_mbus_framefmt *source_fmt;
	struct v4l2_subdev *sd;
	struct v4l2_subdev_state *state;
	struct rkcif_stream *stream = to_rkcif_stream(vdev);
	int ret = -EINVAL;

	if (!media_entity_remote_source_pad_unique(link->sink->entity))
		return -ENOTCONN;

	sd = media_entity_to_v4l2_subdev(link->source->entity);

	state = v4l2_subdev_lock_and_get_active_state(sd);

	source_fmt = v4l2_subdev_state_get_format(state, link->source->index,
						  stream->id);
	if (!source_fmt)
		goto out;

	if (source_fmt->height != stream->pix.height ||
	    source_fmt->width != stream->pix.width) {
		dev_dbg(stream->rkcif->dev,
			"link '%s':%u -> '%s':%u not valid: %ux%u != %ux%u\n",
			link->source->entity->name, link->source->index,
			link->sink->entity->name, link->sink->index,
			source_fmt->width, source_fmt->height,
			stream->pix.width, stream->pix.height);
		goto out;
	}

	ret = 0;

out:
	v4l2_subdev_unlock_state(state);
	return ret;
}

static const struct media_entity_operations rkcif_stream_media_ops = {
	.link_validate = rkcif_stream_link_validate,
};

static const struct v4l2_file_operations rkcif_stream_file_ops = {
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

static int rkcif_stream_init_vb2_queue(struct vb2_queue *q,
				       struct rkcif_stream *stream)
{
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = stream;
	q->ops = &rkcif_stream_vb2_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct rkcif_buffer);
	q->min_queued_buffers = CIF_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stream->vlock;
	q->dev = stream->rkcif->dev;

	return vb2_queue_init(q);
}

int rkcif_stream_register(struct rkcif_device *rkcif,
			  struct rkcif_stream *stream)
{
	struct rkcif_interface *interface = stream->interface;
	struct v4l2_device *v4l2_dev = &rkcif->v4l2_dev;
	struct video_device *vdev = &stream->vdev;
	u32 link_flags = 0;
	int ret;

	stream->rkcif = rkcif;

	INIT_LIST_HEAD(&stream->driver_queue);
	spin_lock_init(&stream->driver_queue_lock);

	init_waitqueue_head(&stream->wq_stopped);

	mutex_init(&stream->vlock);

	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_STREAMING |
			    V4L2_CAP_IO_MC;
	vdev->entity.ops = &rkcif_stream_media_ops;
	vdev->fops = &rkcif_stream_file_ops;
	vdev->ioctl_ops = &rkcif_stream_ioctl_ops;
	vdev->lock = &stream->vlock;
	vdev->minor = -1;
	vdev->release = video_device_release_empty;
	vdev->v4l2_dev = v4l2_dev;
	vdev->vfl_dir = VFL_DIR_RX;
	video_set_drvdata(vdev, stream);

	stream->pad.flags = MEDIA_PAD_FL_SINK;

	stream->pix.height = CIF_MIN_HEIGHT;
	stream->pix.width = CIF_MIN_WIDTH;
	rkcif_stream_fill_format(stream, &stream->pix);

	rkcif_stream_init_vb2_queue(&stream->buf_queue, stream);

	vdev->queue = &stream->buf_queue;
	if (interface->type == RKCIF_IF_DVP)
		snprintf(vdev->name, sizeof(vdev->name), "rkcif-dvp0-id%d",
			 stream->id);
	else if (interface->type == RKCIF_IF_MIPI)
		snprintf(vdev->name, sizeof(vdev->name), "rkcif-mipi%d-id%d",
			 interface->index - RKCIF_MIPI_BASE, stream->id);

	ret = media_entity_pads_init(&vdev->entity, 1, &stream->pad);
	if (ret < 0) {
		dev_err(rkcif->dev,
			"failed to initialize stream media pad: %d\n", ret);
		return ret;
	}

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		dev_err(rkcif->dev, "failed to register video device: %d\n",
			ret);
		goto err_media_entity_cleanup;
	}

	/* enable only stream ID0 by default */
	if (stream->id == RKCIF_ID0)
		link_flags |= MEDIA_LNK_FL_ENABLED;

	ret = media_create_pad_link(&interface->sd.entity, RKCIF_IF_PAD_SRC,
				    &stream->vdev.entity, 0, link_flags);
	if (ret) {
		dev_err(rkcif->dev, "failed to link stream media pad: %d\n",
			ret);
		goto err_video_unregister;
	}

	v4l2_info(v4l2_dev, "registered %s as /dev/video%d\n", vdev->name,
		  vdev->num);

	return 0;

err_video_unregister:
	video_unregister_device(&stream->vdev);
err_media_entity_cleanup:
	media_entity_cleanup(&stream->vdev.entity);
	return ret;
}

void rkcif_stream_unregister(struct rkcif_stream *stream)
{
	video_unregister_device(&stream->vdev);
	media_entity_cleanup(&stream->vdev.entity);
}

const struct rkcif_output_fmt *
rkcif_stream_find_output_fmt(struct rkcif_stream *stream, bool ret_def,
			     u32 pixelfmt)
{
	const struct rkcif_output_fmt *fmt;

	WARN_ON(stream->out_fmts_num == 0);

	for (unsigned int i = 0; i < stream->out_fmts_num; i++) {
		fmt = &stream->out_fmts[i];
		if (fmt->fourcc == pixelfmt)
			return fmt;
	}

	if (ret_def)
		return &stream->out_fmts[0];
	else
		return NULL;
}
