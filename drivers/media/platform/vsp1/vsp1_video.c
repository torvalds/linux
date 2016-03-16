/*
 * vsp1_video.c  --  R-Car VSP1 Video Node
 *
 * Copyright (C) 2013-2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>
#include <linux/wait.h>

#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "vsp1.h"
#include "vsp1_bru.h"
#include "vsp1_entity.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"
#include "vsp1_uds.h"
#include "vsp1_video.h"

#define VSP1_VIDEO_DEF_FORMAT		V4L2_PIX_FMT_YUYV
#define VSP1_VIDEO_DEF_WIDTH		1024
#define VSP1_VIDEO_DEF_HEIGHT		768

#define VSP1_VIDEO_MIN_WIDTH		2U
#define VSP1_VIDEO_MAX_WIDTH		8190U
#define VSP1_VIDEO_MIN_HEIGHT		2U
#define VSP1_VIDEO_MAX_HEIGHT		8190U

/* -----------------------------------------------------------------------------
 * Helper functions
 */

static struct v4l2_subdev *
vsp1_video_remote_subdev(struct media_pad *local, u32 *pad)
{
	struct media_pad *remote;

	remote = media_entity_remote_pad(local);
	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

static int vsp1_video_verify_format(struct vsp1_video *video)
{
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *subdev;
	int ret;

	subdev = vsp1_video_remote_subdev(&video->pad, &fmt.pad);
	if (subdev == NULL)
		return -EINVAL;

	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret < 0)
		return ret == -ENOIOCTLCMD ? -EINVAL : ret;

	if (video->rwpf->fmtinfo->mbus != fmt.format.code ||
	    video->rwpf->format.height != fmt.format.height ||
	    video->rwpf->format.width != fmt.format.width)
		return -EINVAL;

	return 0;
}

static int __vsp1_video_try_format(struct vsp1_video *video,
				   struct v4l2_pix_format_mplane *pix,
				   const struct vsp1_format_info **fmtinfo)
{
	static const u32 xrgb_formats[][2] = {
		{ V4L2_PIX_FMT_RGB444, V4L2_PIX_FMT_XRGB444 },
		{ V4L2_PIX_FMT_RGB555, V4L2_PIX_FMT_XRGB555 },
		{ V4L2_PIX_FMT_BGR32, V4L2_PIX_FMT_XBGR32 },
		{ V4L2_PIX_FMT_RGB32, V4L2_PIX_FMT_XRGB32 },
	};

	const struct vsp1_format_info *info;
	unsigned int width = pix->width;
	unsigned int height = pix->height;
	unsigned int i;

	/* Backward compatibility: replace deprecated RGB formats by their XRGB
	 * equivalent. This selects the format older userspace applications want
	 * while still exposing the new format.
	 */
	for (i = 0; i < ARRAY_SIZE(xrgb_formats); ++i) {
		if (xrgb_formats[i][0] == pix->pixelformat) {
			pix->pixelformat = xrgb_formats[i][1];
			break;
		}
	}

	/* Retrieve format information and select the default format if the
	 * requested format isn't supported.
	 */
	info = vsp1_get_format_info(pix->pixelformat);
	if (info == NULL)
		info = vsp1_get_format_info(VSP1_VIDEO_DEF_FORMAT);

	pix->pixelformat = info->fourcc;
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	pix->field = V4L2_FIELD_NONE;
	memset(pix->reserved, 0, sizeof(pix->reserved));

	/* Align the width and height for YUV 4:2:2 and 4:2:0 formats. */
	width = round_down(width, info->hsub);
	height = round_down(height, info->vsub);

	/* Clamp the width and height. */
	pix->width = clamp(width, VSP1_VIDEO_MIN_WIDTH, VSP1_VIDEO_MAX_WIDTH);
	pix->height = clamp(height, VSP1_VIDEO_MIN_HEIGHT,
			    VSP1_VIDEO_MAX_HEIGHT);

	/* Compute and clamp the stride and image size. While not documented in
	 * the datasheet, strides not aligned to a multiple of 128 bytes result
	 * in image corruption.
	 */
	for (i = 0; i < min(info->planes, 2U); ++i) {
		unsigned int hsub = i > 0 ? info->hsub : 1;
		unsigned int vsub = i > 0 ? info->vsub : 1;
		unsigned int align = 128;
		unsigned int bpl;

		bpl = clamp_t(unsigned int, pix->plane_fmt[i].bytesperline,
			      pix->width / hsub * info->bpp[i] / 8,
			      round_down(65535U, align));

		pix->plane_fmt[i].bytesperline = round_up(bpl, align);
		pix->plane_fmt[i].sizeimage = pix->plane_fmt[i].bytesperline
					    * pix->height / vsub;
	}

	if (info->planes == 3) {
		/* The second and third planes must have the same stride. */
		pix->plane_fmt[2].bytesperline = pix->plane_fmt[1].bytesperline;
		pix->plane_fmt[2].sizeimage = pix->plane_fmt[1].sizeimage;
	}

	pix->num_planes = info->planes;

	if (fmtinfo)
		*fmtinfo = info;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Pipeline Management
 */

static int vsp1_video_pipeline_validate_branch(struct vsp1_pipeline *pipe,
					       struct vsp1_rwpf *input,
					       struct vsp1_rwpf *output)
{
	struct vsp1_entity *entity;
	struct media_entity_enum ent_enum;
	struct media_pad *pad;
	int rval;
	bool bru_found = false;

	input->location.left = 0;
	input->location.top = 0;

	rval = media_entity_enum_init(
		&ent_enum, input->entity.pads[RWPF_PAD_SOURCE].graph_obj.mdev);
	if (rval)
		return rval;

	pad = media_entity_remote_pad(&input->entity.pads[RWPF_PAD_SOURCE]);

	while (1) {
		if (pad == NULL) {
			rval = -EPIPE;
			goto out;
		}

		/* We've reached a video node, that shouldn't have happened. */
		if (!is_media_entity_v4l2_subdev(pad->entity)) {
			rval = -EPIPE;
			goto out;
		}

		entity = to_vsp1_entity(
			media_entity_to_v4l2_subdev(pad->entity));

		/* A BRU is present in the pipeline, store the compose rectangle
		 * location in the input RPF for use when configuring the RPF.
		 */
		if (entity->type == VSP1_ENTITY_BRU) {
			struct vsp1_bru *bru = to_bru(&entity->subdev);
			struct v4l2_rect *rect =
				&bru->inputs[pad->index].compose;

			bru->inputs[pad->index].rpf = input;

			input->location.left = rect->left;
			input->location.top = rect->top;

			bru_found = true;
		}

		/* We've reached the WPF, we're done. */
		if (entity->type == VSP1_ENTITY_WPF)
			break;

		/* Ensure the branch has no loop. */
		if (media_entity_enum_test_and_set(&ent_enum,
						   &entity->subdev.entity)) {
			rval = -EPIPE;
			goto out;
		}

		/* UDS can't be chained. */
		if (entity->type == VSP1_ENTITY_UDS) {
			if (pipe->uds) {
				rval = -EPIPE;
				goto out;
			}

			pipe->uds = entity;
			pipe->uds_input = bru_found ? pipe->bru
					: &input->entity;
		}

		/* Follow the source link. The link setup operations ensure
		 * that the output fan-out can't be more than one, there is thus
		 * no need to verify here that only a single source link is
		 * activated.
		 */
		pad = &entity->pads[entity->source_pad];
		pad = media_entity_remote_pad(pad);
	}

	/* The last entity must be the output WPF. */
	if (entity != &output->entity)
		rval = -EPIPE;

out:
	media_entity_enum_cleanup(&ent_enum);

	return rval;
}

static int vsp1_video_pipeline_validate(struct vsp1_pipeline *pipe,
					struct vsp1_video *video)
{
	struct media_entity_graph graph;
	struct media_entity *entity = &video->video.entity;
	struct media_device *mdev = entity->graph_obj.mdev;
	unsigned int i;
	int ret;

	mutex_lock(&mdev->graph_mutex);

	/* Walk the graph to locate the entities and video nodes. */
	ret = media_entity_graph_walk_init(&graph, mdev);
	if (ret) {
		mutex_unlock(&mdev->graph_mutex);
		return ret;
	}

	media_entity_graph_walk_start(&graph, entity);

	while ((entity = media_entity_graph_walk_next(&graph))) {
		struct v4l2_subdev *subdev;
		struct vsp1_rwpf *rwpf;
		struct vsp1_entity *e;

		if (is_media_entity_v4l2_io(entity))
			continue;

		subdev = media_entity_to_v4l2_subdev(entity);
		e = to_vsp1_entity(subdev);
		list_add_tail(&e->list_pipe, &pipe->entities);

		if (e->type == VSP1_ENTITY_RPF) {
			rwpf = to_rwpf(subdev);
			pipe->inputs[rwpf->entity.index] = rwpf;
			rwpf->video->pipe_index = ++pipe->num_inputs;
		} else if (e->type == VSP1_ENTITY_WPF) {
			rwpf = to_rwpf(subdev);
			pipe->output = rwpf;
			rwpf->video->pipe_index = 0;
		} else if (e->type == VSP1_ENTITY_LIF) {
			pipe->lif = e;
		} else if (e->type == VSP1_ENTITY_BRU) {
			pipe->bru = e;
		}
	}

	mutex_unlock(&mdev->graph_mutex);

	media_entity_graph_walk_cleanup(&graph);

	/* We need one output and at least one input. */
	if (pipe->num_inputs == 0 || !pipe->output) {
		ret = -EPIPE;
		goto error;
	}

	/* Follow links downstream for each input and make sure the graph
	 * contains no loop and that all branches end at the output WPF.
	 */
	for (i = 0; i < video->vsp1->info->rpf_count; ++i) {
		if (!pipe->inputs[i])
			continue;

		ret = vsp1_video_pipeline_validate_branch(pipe, pipe->inputs[i],
							  pipe->output);
		if (ret < 0)
			goto error;
	}

	return 0;

error:
	vsp1_pipeline_reset(pipe);
	return ret;
}

static int vsp1_video_pipeline_init(struct vsp1_pipeline *pipe,
				    struct vsp1_video *video)
{
	int ret;

	mutex_lock(&pipe->lock);

	/* If we're the first user validate and initialize the pipeline. */
	if (pipe->use_count == 0) {
		ret = vsp1_video_pipeline_validate(pipe, video);
		if (ret < 0)
			goto done;
	}

	pipe->use_count++;
	ret = 0;

done:
	mutex_unlock(&pipe->lock);
	return ret;
}

static void vsp1_video_pipeline_cleanup(struct vsp1_pipeline *pipe)
{
	mutex_lock(&pipe->lock);

	/* If we're the last user clean up the pipeline. */
	if (--pipe->use_count == 0)
		vsp1_pipeline_reset(pipe);

	mutex_unlock(&pipe->lock);
}

/*
 * vsp1_video_complete_buffer - Complete the current buffer
 * @video: the video node
 *
 * This function completes the current buffer by filling its sequence number,
 * time stamp and payload size, and hands it back to the videobuf core.
 *
 * When operating in DU output mode (deep pipeline to the DU through the LIF),
 * the VSP1 needs to constantly supply frames to the display. In that case, if
 * no other buffer is queued, reuse the one that has just been processed instead
 * of handing it back to the videobuf core.
 *
 * Return the next queued buffer or NULL if the queue is empty.
 */
static struct vsp1_vb2_buffer *
vsp1_video_complete_buffer(struct vsp1_video *video)
{
	struct vsp1_pipeline *pipe = to_vsp1_pipeline(&video->video.entity);
	struct vsp1_vb2_buffer *next = NULL;
	struct vsp1_vb2_buffer *done;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&video->irqlock, flags);

	if (list_empty(&video->irqqueue)) {
		spin_unlock_irqrestore(&video->irqlock, flags);
		return NULL;
	}

	done = list_first_entry(&video->irqqueue,
				struct vsp1_vb2_buffer, queue);

	/* In DU output mode reuse the buffer if the list is singular. */
	if (pipe->lif && list_is_singular(&video->irqqueue)) {
		spin_unlock_irqrestore(&video->irqlock, flags);
		return done;
	}

	list_del(&done->queue);

	if (!list_empty(&video->irqqueue))
		next = list_first_entry(&video->irqqueue,
					struct vsp1_vb2_buffer, queue);

	spin_unlock_irqrestore(&video->irqlock, flags);

	done->buf.sequence = video->sequence++;
	done->buf.vb2_buf.timestamp = ktime_get_ns();
	for (i = 0; i < done->buf.vb2_buf.num_planes; ++i)
		vb2_set_plane_payload(&done->buf.vb2_buf, i,
				      done->mem.length[i]);
	vb2_buffer_done(&done->buf.vb2_buf, VB2_BUF_STATE_DONE);

	return next;
}

static void vsp1_video_frame_end(struct vsp1_pipeline *pipe,
				 struct vsp1_rwpf *rwpf)
{
	struct vsp1_video *video = rwpf->video;
	struct vsp1_vb2_buffer *buf;
	unsigned long flags;

	buf = vsp1_video_complete_buffer(video);
	if (buf == NULL)
		return;

	spin_lock_irqsave(&pipe->irqlock, flags);

	video->rwpf->ops->set_memory(video->rwpf, &buf->mem);
	pipe->buffers_ready |= 1 << video->pipe_index;

	spin_unlock_irqrestore(&pipe->irqlock, flags);
}

static void vsp1_video_pipeline_frame_end(struct vsp1_pipeline *pipe)
{
	struct vsp1_device *vsp1 = pipe->output->entity.vsp1;
	unsigned int i;

	/* Complete buffers on all video nodes. */
	for (i = 0; i < vsp1->info->rpf_count; ++i) {
		if (!pipe->inputs[i])
			continue;

		vsp1_video_frame_end(pipe, pipe->inputs[i]);
	}

	if (!pipe->lif)
		vsp1_video_frame_end(pipe, pipe->output);
}

/* -----------------------------------------------------------------------------
 * videobuf2 Queue Operations
 */

static int
vsp1_video_queue_setup(struct vb2_queue *vq,
		     unsigned int *nbuffers, unsigned int *nplanes,
		     unsigned int sizes[], void *alloc_ctxs[])
{
	struct vsp1_video *video = vb2_get_drv_priv(vq);
	const struct v4l2_pix_format_mplane *format = &video->rwpf->format;
	unsigned int i;

	if (*nplanes) {
		if (*nplanes != format->num_planes)
			return -EINVAL;

		for (i = 0; i < *nplanes; i++) {
			if (sizes[i] < format->plane_fmt[i].sizeimage)
				return -EINVAL;
			alloc_ctxs[i] = video->alloc_ctx;
		}
		return 0;
	}

	*nplanes = format->num_planes;

	for (i = 0; i < format->num_planes; ++i) {
		sizes[i] = format->plane_fmt[i].sizeimage;
		alloc_ctxs[i] = video->alloc_ctx;
	}

	return 0;
}

static int vsp1_video_buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vsp1_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct vsp1_vb2_buffer *buf = to_vsp1_vb2_buffer(vbuf);
	const struct v4l2_pix_format_mplane *format = &video->rwpf->format;
	unsigned int i;

	if (vb->num_planes < format->num_planes)
		return -EINVAL;

	buf->mem.num_planes = vb->num_planes;

	for (i = 0; i < vb->num_planes; ++i) {
		buf->mem.addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
		buf->mem.length[i] = vb2_plane_size(vb, i);

		if (buf->mem.length[i] < format->plane_fmt[i].sizeimage)
			return -EINVAL;
	}

	return 0;
}

static void vsp1_video_buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vsp1_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct vsp1_pipeline *pipe = to_vsp1_pipeline(&video->video.entity);
	struct vsp1_vb2_buffer *buf = to_vsp1_vb2_buffer(vbuf);
	unsigned long flags;
	bool empty;

	spin_lock_irqsave(&video->irqlock, flags);
	empty = list_empty(&video->irqqueue);
	list_add_tail(&buf->queue, &video->irqqueue);
	spin_unlock_irqrestore(&video->irqlock, flags);

	if (!empty)
		return;

	spin_lock_irqsave(&pipe->irqlock, flags);

	video->rwpf->ops->set_memory(video->rwpf, &buf->mem);
	pipe->buffers_ready |= 1 << video->pipe_index;

	if (vb2_is_streaming(&video->queue) &&
	    vsp1_pipeline_ready(pipe))
		vsp1_pipeline_run(pipe);

	spin_unlock_irqrestore(&pipe->irqlock, flags);
}

static int vsp1_video_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vsp1_video *video = vb2_get_drv_priv(vq);
	struct vsp1_pipeline *pipe = to_vsp1_pipeline(&video->video.entity);
	struct vsp1_entity *entity;
	unsigned long flags;
	int ret;

	mutex_lock(&pipe->lock);
	if (pipe->stream_count == pipe->num_inputs) {
		if (pipe->uds) {
			struct vsp1_uds *uds = to_uds(&pipe->uds->subdev);

			/* If a BRU is present in the pipeline before the UDS,
			 * the alpha component doesn't need to be scaled as the
			 * BRU output alpha value is fixed to 255. Otherwise we
			 * need to scale the alpha component only when available
			 * at the input RPF.
			 */
			if (pipe->uds_input->type == VSP1_ENTITY_BRU) {
				uds->scale_alpha = false;
			} else {
				struct vsp1_rwpf *rpf =
					to_rwpf(&pipe->uds_input->subdev);

				uds->scale_alpha = rpf->fmtinfo->alpha;
			}
		}

		list_for_each_entry(entity, &pipe->entities, list_pipe) {
			vsp1_entity_route_setup(entity);

			ret = v4l2_subdev_call(&entity->subdev, video,
					       s_stream, 1);
			if (ret < 0) {
				mutex_unlock(&pipe->lock);
				return ret;
			}
		}
	}

	pipe->stream_count++;
	mutex_unlock(&pipe->lock);

	spin_lock_irqsave(&pipe->irqlock, flags);
	if (vsp1_pipeline_ready(pipe))
		vsp1_pipeline_run(pipe);
	spin_unlock_irqrestore(&pipe->irqlock, flags);

	return 0;
}

static void vsp1_video_stop_streaming(struct vb2_queue *vq)
{
	struct vsp1_video *video = vb2_get_drv_priv(vq);
	struct vsp1_pipeline *pipe = to_vsp1_pipeline(&video->video.entity);
	struct vsp1_vb2_buffer *buffer;
	unsigned long flags;
	int ret;

	mutex_lock(&pipe->lock);
	if (--pipe->stream_count == 0) {
		/* Stop the pipeline. */
		ret = vsp1_pipeline_stop(pipe);
		if (ret == -ETIMEDOUT)
			dev_err(video->vsp1->dev, "pipeline stop timeout\n");
	}
	mutex_unlock(&pipe->lock);

	vsp1_video_pipeline_cleanup(pipe);
	media_entity_pipeline_stop(&video->video.entity);

	/* Remove all buffers from the IRQ queue. */
	spin_lock_irqsave(&video->irqlock, flags);
	list_for_each_entry(buffer, &video->irqqueue, queue)
		vb2_buffer_done(&buffer->buf.vb2_buf, VB2_BUF_STATE_ERROR);
	INIT_LIST_HEAD(&video->irqqueue);
	spin_unlock_irqrestore(&video->irqlock, flags);
}

static struct vb2_ops vsp1_video_queue_qops = {
	.queue_setup = vsp1_video_queue_setup,
	.buf_prepare = vsp1_video_buffer_prepare,
	.buf_queue = vsp1_video_buffer_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = vsp1_video_start_streaming,
	.stop_streaming = vsp1_video_stop_streaming,
};

/* -----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int
vsp1_video_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_video *video = to_vsp1_video(vfh->vdev);

	cap->capabilities = V4L2_CAP_DEVICE_CAPS | V4L2_CAP_STREAMING
			  | V4L2_CAP_VIDEO_CAPTURE_MPLANE
			  | V4L2_CAP_VIDEO_OUTPUT_MPLANE;

	if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		cap->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE
				 | V4L2_CAP_STREAMING;
	else
		cap->device_caps = V4L2_CAP_VIDEO_OUTPUT_MPLANE
				 | V4L2_CAP_STREAMING;

	strlcpy(cap->driver, "vsp1", sizeof(cap->driver));
	strlcpy(cap->card, video->video.name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(video->vsp1->dev));

	return 0;
}

static int
vsp1_video_get_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_video *video = to_vsp1_video(vfh->vdev);

	if (format->type != video->queue.type)
		return -EINVAL;

	mutex_lock(&video->lock);
	format->fmt.pix_mp = video->rwpf->format;
	mutex_unlock(&video->lock);

	return 0;
}

static int
vsp1_video_try_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_video *video = to_vsp1_video(vfh->vdev);

	if (format->type != video->queue.type)
		return -EINVAL;

	return __vsp1_video_try_format(video, &format->fmt.pix_mp, NULL);
}

static int
vsp1_video_set_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_video *video = to_vsp1_video(vfh->vdev);
	const struct vsp1_format_info *info;
	int ret;

	if (format->type != video->queue.type)
		return -EINVAL;

	ret = __vsp1_video_try_format(video, &format->fmt.pix_mp, &info);
	if (ret < 0)
		return ret;

	mutex_lock(&video->lock);

	if (vb2_is_busy(&video->queue)) {
		ret = -EBUSY;
		goto done;
	}

	video->rwpf->format = format->fmt.pix_mp;
	video->rwpf->fmtinfo = info;

done:
	mutex_unlock(&video->lock);
	return ret;
}

static int
vsp1_video_streamon(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_video *video = to_vsp1_video(vfh->vdev);
	struct vsp1_pipeline *pipe;
	int ret;

	if (video->queue.owner && video->queue.owner != file->private_data)
		return -EBUSY;

	video->sequence = 0;

	/* Start streaming on the pipeline. No link touching an entity in the
	 * pipeline can be activated or deactivated once streaming is started.
	 *
	 * Use the VSP1 pipeline object embedded in the first video object that
	 * starts streaming.
	 */
	pipe = video->video.entity.pipe
	     ? to_vsp1_pipeline(&video->video.entity) : &video->pipe;

	ret = media_entity_pipeline_start(&video->video.entity, &pipe->pipe);
	if (ret < 0)
		return ret;

	/* Verify that the configured format matches the output of the connected
	 * subdev.
	 */
	ret = vsp1_video_verify_format(video);
	if (ret < 0)
		goto err_stop;

	ret = vsp1_video_pipeline_init(pipe, video);
	if (ret < 0)
		goto err_stop;

	/* Start the queue. */
	ret = vb2_streamon(&video->queue, type);
	if (ret < 0)
		goto err_cleanup;

	return 0;

err_cleanup:
	vsp1_video_pipeline_cleanup(pipe);
err_stop:
	media_entity_pipeline_stop(&video->video.entity);
	return ret;
}

static const struct v4l2_ioctl_ops vsp1_video_ioctl_ops = {
	.vidioc_querycap		= vsp1_video_querycap,
	.vidioc_g_fmt_vid_cap_mplane	= vsp1_video_get_format,
	.vidioc_s_fmt_vid_cap_mplane	= vsp1_video_set_format,
	.vidioc_try_fmt_vid_cap_mplane	= vsp1_video_try_format,
	.vidioc_g_fmt_vid_out_mplane	= vsp1_video_get_format,
	.vidioc_s_fmt_vid_out_mplane	= vsp1_video_set_format,
	.vidioc_try_fmt_vid_out_mplane	= vsp1_video_try_format,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vsp1_video_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------------
 * V4L2 File Operations
 */

static int vsp1_video_open(struct file *file)
{
	struct vsp1_video *video = video_drvdata(file);
	struct v4l2_fh *vfh;
	int ret = 0;

	vfh = kzalloc(sizeof(*vfh), GFP_KERNEL);
	if (vfh == NULL)
		return -ENOMEM;

	v4l2_fh_init(vfh, &video->video);
	v4l2_fh_add(vfh);

	file->private_data = vfh;

	ret = vsp1_device_get(video->vsp1);
	if (ret < 0) {
		v4l2_fh_del(vfh);
		kfree(vfh);
	}

	return ret;
}

static int vsp1_video_release(struct file *file)
{
	struct vsp1_video *video = video_drvdata(file);
	struct v4l2_fh *vfh = file->private_data;

	mutex_lock(&video->lock);
	if (video->queue.owner == vfh) {
		vb2_queue_release(&video->queue);
		video->queue.owner = NULL;
	}
	mutex_unlock(&video->lock);

	vsp1_device_put(video->vsp1);

	v4l2_fh_release(file);

	file->private_data = NULL;

	return 0;
}

static struct v4l2_file_operations vsp1_video_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = vsp1_video_open,
	.release = vsp1_video_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_video *vsp1_video_create(struct vsp1_device *vsp1,
				     struct vsp1_rwpf *rwpf)
{
	struct vsp1_video *video;
	const char *direction;
	int ret;

	video = devm_kzalloc(vsp1->dev, sizeof(*video), GFP_KERNEL);
	if (!video)
		return ERR_PTR(-ENOMEM);

	rwpf->video = video;

	video->vsp1 = vsp1;
	video->rwpf = rwpf;

	if (rwpf->entity.type == VSP1_ENTITY_RPF) {
		direction = "input";
		video->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		video->pad.flags = MEDIA_PAD_FL_SOURCE;
		video->video.vfl_dir = VFL_DIR_TX;
	} else {
		direction = "output";
		video->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		video->pad.flags = MEDIA_PAD_FL_SINK;
		video->video.vfl_dir = VFL_DIR_RX;
	}

	mutex_init(&video->lock);
	spin_lock_init(&video->irqlock);
	INIT_LIST_HEAD(&video->irqqueue);

	vsp1_pipeline_init(&video->pipe);
	video->pipe.frame_end = vsp1_video_pipeline_frame_end;

	/* Initialize the media entity... */
	ret = media_entity_pads_init(&video->video.entity, 1, &video->pad);
	if (ret < 0)
		return ERR_PTR(ret);

	/* ... and the format ... */
	rwpf->fmtinfo = vsp1_get_format_info(VSP1_VIDEO_DEF_FORMAT);
	rwpf->format.pixelformat = rwpf->fmtinfo->fourcc;
	rwpf->format.colorspace = V4L2_COLORSPACE_SRGB;
	rwpf->format.field = V4L2_FIELD_NONE;
	rwpf->format.width = VSP1_VIDEO_DEF_WIDTH;
	rwpf->format.height = VSP1_VIDEO_DEF_HEIGHT;
	rwpf->format.num_planes = 1;
	rwpf->format.plane_fmt[0].bytesperline =
		rwpf->format.width * rwpf->fmtinfo->bpp[0] / 8;
	rwpf->format.plane_fmt[0].sizeimage =
		rwpf->format.plane_fmt[0].bytesperline * rwpf->format.height;

	/* ... and the video node... */
	video->video.v4l2_dev = &video->vsp1->v4l2_dev;
	video->video.fops = &vsp1_video_fops;
	snprintf(video->video.name, sizeof(video->video.name), "%s %s",
		 rwpf->entity.subdev.name, direction);
	video->video.vfl_type = VFL_TYPE_GRABBER;
	video->video.release = video_device_release_empty;
	video->video.ioctl_ops = &vsp1_video_ioctl_ops;

	video_set_drvdata(&video->video, video);

	/* ... and the buffers queue... */
	video->alloc_ctx = vb2_dma_contig_init_ctx(video->vsp1->dev);
	if (IS_ERR(video->alloc_ctx)) {
		ret = PTR_ERR(video->alloc_ctx);
		goto error;
	}

	video->queue.type = video->type;
	video->queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	video->queue.lock = &video->lock;
	video->queue.drv_priv = video;
	video->queue.buf_struct_size = sizeof(struct vsp1_vb2_buffer);
	video->queue.ops = &vsp1_video_queue_qops;
	video->queue.mem_ops = &vb2_dma_contig_memops;
	video->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	ret = vb2_queue_init(&video->queue);
	if (ret < 0) {
		dev_err(video->vsp1->dev, "failed to initialize vb2 queue\n");
		goto error;
	}

	/* ... and register the video device. */
	video->video.queue = &video->queue;
	ret = video_register_device(&video->video, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_err(video->vsp1->dev, "failed to register video device\n");
		goto error;
	}

	return video;

error:
	vb2_dma_contig_cleanup_ctx(video->alloc_ctx);
	vsp1_video_cleanup(video);
	return ERR_PTR(ret);
}

void vsp1_video_cleanup(struct vsp1_video *video)
{
	if (video_is_registered(&video->video))
		video_unregister_device(&video->video);

	vb2_dma_contig_cleanup_ctx(video->alloc_ctx);
	media_entity_cleanup(&video->video.entity);
}
