/*
 * vsp1_video.c  --  R-Car VSP1 Video Node
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
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
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "vsp1.h"
#include "vsp1_bru.h"
#include "vsp1_entity.h"
#include "vsp1_rwpf.h"
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

static const struct vsp1_format_info vsp1_video_formats[] = {
	{ V4L2_PIX_FMT_RGB332, V4L2_MBUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGB_332, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 8, 0, 0 }, false, false, 1, 1 },
	{ V4L2_PIX_FMT_RGB444, V4L2_MBUS_FMT_ARGB8888_1X32,
	  VI6_FMT_XRGB_4444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1 },
	{ V4L2_PIX_FMT_RGB555, V4L2_MBUS_FMT_ARGB8888_1X32,
	  VI6_FMT_XRGB_1555, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1 },
	{ V4L2_PIX_FMT_RGB565, V4L2_MBUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGB_565, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1 },
	{ V4L2_PIX_FMT_BGR24, V4L2_MBUS_FMT_ARGB8888_1X32,
	  VI6_FMT_BGR_888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 24, 0, 0 }, false, false, 1, 1 },
	{ V4L2_PIX_FMT_RGB24, V4L2_MBUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGB_888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 24, 0, 0 }, false, false, 1, 1 },
	{ V4L2_PIX_FMT_BGR32, V4L2_MBUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS,
	  1, { 32, 0, 0 }, false, false, 1, 1 },
	{ V4L2_PIX_FMT_RGB32, V4L2_MBUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 32, 0, 0 }, false, false, 1, 1 },
	{ V4L2_PIX_FMT_UYVY, V4L2_MBUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 16, 0, 0 }, false, false, 2, 1 },
	{ V4L2_PIX_FMT_VYUY, V4L2_MBUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 16, 0, 0 }, false, true, 2, 1 },
	{ V4L2_PIX_FMT_YUYV, V4L2_MBUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 16, 0, 0 }, true, false, 2, 1 },
	{ V4L2_PIX_FMT_YVYU, V4L2_MBUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 16, 0, 0 }, true, true, 2, 1 },
	{ V4L2_PIX_FMT_NV12M, V4L2_MBUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_UV_420, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  2, { 8, 16, 0 }, false, false, 2, 2 },
	{ V4L2_PIX_FMT_NV21M, V4L2_MBUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_UV_420, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  2, { 8, 16, 0 }, false, true, 2, 2 },
	{ V4L2_PIX_FMT_NV16M, V4L2_MBUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_UV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  2, { 8, 16, 0 }, false, false, 2, 1 },
	{ V4L2_PIX_FMT_NV61M, V4L2_MBUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_UV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  2, { 8, 16, 0 }, false, true, 2, 1 },
	{ V4L2_PIX_FMT_YUV420M, V4L2_MBUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_420, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, false, 2, 2 },
};

/*
 * vsp1_get_format_info - Retrieve format information for a 4CC
 * @fourcc: the format 4CC
 *
 * Return a pointer to the format information structure corresponding to the
 * given V4L2 format 4CC, or NULL if no corresponding format can be found.
 */
static const struct vsp1_format_info *vsp1_get_format_info(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vsp1_video_formats); ++i) {
		const struct vsp1_format_info *info = &vsp1_video_formats[i];

		if (info->fourcc == fourcc)
			return info;
	}

	return NULL;
}


static struct v4l2_subdev *
vsp1_video_remote_subdev(struct media_pad *local, u32 *pad)
{
	struct media_pad *remote;

	remote = media_entity_remote_pad(local);
	if (remote == NULL ||
	    media_entity_type(remote->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
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

	if (video->fmtinfo->mbus != fmt.format.code ||
	    video->format.height != fmt.format.height ||
	    video->format.width != fmt.format.width)
		return -EINVAL;

	return 0;
}

static int __vsp1_video_try_format(struct vsp1_video *video,
				   struct v4l2_pix_format_mplane *pix,
				   const struct vsp1_format_info **fmtinfo)
{
	const struct vsp1_format_info *info;
	unsigned int width = pix->width;
	unsigned int height = pix->height;
	unsigned int i;

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
	for (i = 0; i < max(info->planes, 2U); ++i) {
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

static bool
vsp1_video_format_adjust(struct vsp1_video *video,
			 const struct v4l2_pix_format_mplane *format,
			 struct v4l2_pix_format_mplane *adjust)
{
	unsigned int i;

	*adjust = *format;
	__vsp1_video_try_format(video, adjust, NULL);

	if (format->width != adjust->width ||
	    format->height != adjust->height ||
	    format->pixelformat != adjust->pixelformat ||
	    format->num_planes != adjust->num_planes)
		return false;

	for (i = 0; i < format->num_planes; ++i) {
		if (format->plane_fmt[i].bytesperline !=
		    adjust->plane_fmt[i].bytesperline)
			return false;

		adjust->plane_fmt[i].sizeimage =
			max(adjust->plane_fmt[i].sizeimage,
			    format->plane_fmt[i].sizeimage);
	}

	return true;
}

/* -----------------------------------------------------------------------------
 * Pipeline Management
 */

static int vsp1_pipeline_validate_branch(struct vsp1_rwpf *input,
					 struct vsp1_rwpf *output)
{
	struct vsp1_entity *entity;
	unsigned int entities = 0;
	struct media_pad *pad;
	bool uds_found = false;

	input->location.left = 0;
	input->location.top = 0;

	pad = media_entity_remote_pad(&input->entity.pads[RWPF_PAD_SOURCE]);

	while (1) {
		if (pad == NULL)
			return -EPIPE;

		/* We've reached a video node, that shouldn't have happened. */
		if (media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			return -EPIPE;

		entity = to_vsp1_entity(media_entity_to_v4l2_subdev(pad->entity));

		/* A BRU is present in the pipeline, store the compose rectangle
		 * location in the input RPF for use when configuring the RPF.
		 */
		if (entity->type == VSP1_ENTITY_BRU) {
			struct vsp1_bru *bru = to_bru(&entity->subdev);
			struct v4l2_rect *rect = &bru->compose[pad->index];

			input->location.left = rect->left;
			input->location.top = rect->top;
		}

		/* We've reached the WPF, we're done. */
		if (entity->type == VSP1_ENTITY_WPF)
			break;

		/* Ensure the branch has no loop. */
		if (entities & (1 << entity->subdev.entity.id))
			return -EPIPE;

		entities |= 1 << entity->subdev.entity.id;

		/* UDS can't be chained. */
		if (entity->type == VSP1_ENTITY_UDS) {
			if (uds_found)
				return -EPIPE;
			uds_found = true;
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
		return -EPIPE;

	return 0;
}

static int vsp1_pipeline_validate(struct vsp1_pipeline *pipe,
				  struct vsp1_video *video)
{
	struct media_entity_graph graph;
	struct media_entity *entity = &video->video.entity;
	struct media_device *mdev = entity->parent;
	unsigned int i;
	int ret;

	mutex_lock(&mdev->graph_mutex);

	/* Walk the graph to locate the entities and video nodes. */
	media_entity_graph_walk_start(&graph, entity);

	while ((entity = media_entity_graph_walk_next(&graph))) {
		struct v4l2_subdev *subdev;
		struct vsp1_rwpf *rwpf;
		struct vsp1_entity *e;

		if (media_entity_type(entity) != MEDIA_ENT_T_V4L2_SUBDEV) {
			pipe->num_video++;
			continue;
		}

		subdev = media_entity_to_v4l2_subdev(entity);
		e = to_vsp1_entity(subdev);
		list_add_tail(&e->list_pipe, &pipe->entities);

		if (e->type == VSP1_ENTITY_RPF) {
			rwpf = to_rwpf(subdev);
			pipe->inputs[pipe->num_inputs++] = rwpf;
			rwpf->video.pipe_index = pipe->num_inputs;
		} else if (e->type == VSP1_ENTITY_WPF) {
			rwpf = to_rwpf(subdev);
			pipe->output = to_rwpf(subdev);
			rwpf->video.pipe_index = 0;
		} else if (e->type == VSP1_ENTITY_LIF) {
			pipe->lif = e;
		} else if (e->type == VSP1_ENTITY_BRU) {
			pipe->bru = e;
		}
	}

	mutex_unlock(&mdev->graph_mutex);

	/* We need one output and at least one input. */
	if (pipe->num_inputs == 0 || !pipe->output) {
		ret = -EPIPE;
		goto error;
	}

	/* Follow links downstream for each input and make sure the graph
	 * contains no loop and that all branches end at the output WPF.
	 */
	for (i = 0; i < pipe->num_inputs; ++i) {
		ret = vsp1_pipeline_validate_branch(pipe->inputs[i],
						    pipe->output);
		if (ret < 0)
			goto error;
	}

	return 0;

error:
	INIT_LIST_HEAD(&pipe->entities);
	pipe->buffers_ready = 0;
	pipe->num_video = 0;
	pipe->num_inputs = 0;
	pipe->output = NULL;
	pipe->bru = NULL;
	pipe->lif = NULL;
	return ret;
}

static int vsp1_pipeline_init(struct vsp1_pipeline *pipe,
			      struct vsp1_video *video)
{
	int ret;

	mutex_lock(&pipe->lock);

	/* If we're the first user validate and initialize the pipeline. */
	if (pipe->use_count == 0) {
		ret = vsp1_pipeline_validate(pipe, video);
		if (ret < 0)
			goto done;
	}

	pipe->use_count++;
	ret = 0;

done:
	mutex_unlock(&pipe->lock);
	return ret;
}

static void vsp1_pipeline_cleanup(struct vsp1_pipeline *pipe)
{
	mutex_lock(&pipe->lock);

	/* If we're the last user clean up the pipeline. */
	if (--pipe->use_count == 0) {
		INIT_LIST_HEAD(&pipe->entities);
		pipe->state = VSP1_PIPELINE_STOPPED;
		pipe->buffers_ready = 0;
		pipe->num_video = 0;
		pipe->num_inputs = 0;
		pipe->output = NULL;
		pipe->bru = NULL;
		pipe->lif = NULL;
	}

	mutex_unlock(&pipe->lock);
}

static void vsp1_pipeline_run(struct vsp1_pipeline *pipe)
{
	struct vsp1_device *vsp1 = pipe->output->entity.vsp1;

	vsp1_write(vsp1, VI6_CMD(pipe->output->entity.index), VI6_CMD_STRCMD);
	pipe->state = VSP1_PIPELINE_RUNNING;
	pipe->buffers_ready = 0;
}

static int vsp1_pipeline_stop(struct vsp1_pipeline *pipe)
{
	struct vsp1_entity *entity;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pipe->irqlock, flags);
	if (pipe->state == VSP1_PIPELINE_RUNNING)
		pipe->state = VSP1_PIPELINE_STOPPING;
	spin_unlock_irqrestore(&pipe->irqlock, flags);

	ret = wait_event_timeout(pipe->wq, pipe->state == VSP1_PIPELINE_STOPPED,
				 msecs_to_jiffies(500));
	ret = ret == 0 ? -ETIMEDOUT : 0;

	list_for_each_entry(entity, &pipe->entities, list_pipe) {
		if (entity->route && entity->route->reg)
			vsp1_write(entity->vsp1, entity->route->reg,
				   VI6_DPR_NODE_UNUSED);

		v4l2_subdev_call(&entity->subdev, video, s_stream, 0);
	}

	return ret;
}

static bool vsp1_pipeline_ready(struct vsp1_pipeline *pipe)
{
	unsigned int mask;

	mask = ((1 << pipe->num_inputs) - 1) << 1;
	if (!pipe->lif)
		mask |= 1 << 0;

	return pipe->buffers_ready == mask;
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
static struct vsp1_video_buffer *
vsp1_video_complete_buffer(struct vsp1_video *video)
{
	struct vsp1_pipeline *pipe = to_vsp1_pipeline(&video->video.entity);
	struct vsp1_video_buffer *next = NULL;
	struct vsp1_video_buffer *done;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&video->irqlock, flags);

	if (list_empty(&video->irqqueue)) {
		spin_unlock_irqrestore(&video->irqlock, flags);
		return NULL;
	}

	done = list_first_entry(&video->irqqueue,
				struct vsp1_video_buffer, queue);

	/* In DU output mode reuse the buffer if the list is singular. */
	if (pipe->lif && list_is_singular(&video->irqqueue)) {
		spin_unlock_irqrestore(&video->irqlock, flags);
		return done;
	}

	list_del(&done->queue);

	if (!list_empty(&video->irqqueue))
		next = list_first_entry(&video->irqqueue,
					struct vsp1_video_buffer, queue);

	spin_unlock_irqrestore(&video->irqlock, flags);

	done->buf.v4l2_buf.sequence = video->sequence++;
	v4l2_get_timestamp(&done->buf.v4l2_buf.timestamp);
	for (i = 0; i < done->buf.num_planes; ++i)
		vb2_set_plane_payload(&done->buf, i, done->length[i]);
	vb2_buffer_done(&done->buf, VB2_BUF_STATE_DONE);

	return next;
}

static void vsp1_video_frame_end(struct vsp1_pipeline *pipe,
				 struct vsp1_video *video)
{
	struct vsp1_video_buffer *buf;
	unsigned long flags;

	buf = vsp1_video_complete_buffer(video);
	if (buf == NULL)
		return;

	spin_lock_irqsave(&pipe->irqlock, flags);

	video->ops->queue(video, buf);
	pipe->buffers_ready |= 1 << video->pipe_index;

	spin_unlock_irqrestore(&pipe->irqlock, flags);
}

void vsp1_pipeline_frame_end(struct vsp1_pipeline *pipe)
{
	enum vsp1_pipeline_state state;
	unsigned long flags;
	unsigned int i;

	if (pipe == NULL)
		return;

	/* Complete buffers on all video nodes. */
	for (i = 0; i < pipe->num_inputs; ++i)
		vsp1_video_frame_end(pipe, &pipe->inputs[i]->video);

	if (!pipe->lif)
		vsp1_video_frame_end(pipe, &pipe->output->video);

	spin_lock_irqsave(&pipe->irqlock, flags);

	state = pipe->state;
	pipe->state = VSP1_PIPELINE_STOPPED;

	/* If a stop has been requested, mark the pipeline as stopped and
	 * return.
	 */
	if (state == VSP1_PIPELINE_STOPPING) {
		wake_up(&pipe->wq);
		goto done;
	}

	/* Restart the pipeline if ready. */
	if (vsp1_pipeline_ready(pipe))
		vsp1_pipeline_run(pipe);

done:
	spin_unlock_irqrestore(&pipe->irqlock, flags);
}

/* -----------------------------------------------------------------------------
 * videobuf2 Queue Operations
 */

static int
vsp1_video_queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
		     unsigned int *nbuffers, unsigned int *nplanes,
		     unsigned int sizes[], void *alloc_ctxs[])
{
	struct vsp1_video *video = vb2_get_drv_priv(vq);
	const struct v4l2_pix_format_mplane *format;
	struct v4l2_pix_format_mplane pix_mp;
	unsigned int i;

	if (fmt) {
		/* Make sure the format is valid and adjust the sizeimage field
		 * if needed.
		 */
		if (!vsp1_video_format_adjust(video, &fmt->fmt.pix_mp, &pix_mp))
			return -EINVAL;

		format = &pix_mp;
	} else {
		format = &video->format;
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
	struct vsp1_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct vsp1_video_buffer *buf = to_vsp1_video_buffer(vb);
	const struct v4l2_pix_format_mplane *format = &video->format;
	unsigned int i;

	if (vb->num_planes < format->num_planes)
		return -EINVAL;

	for (i = 0; i < vb->num_planes; ++i) {
		buf->addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
		buf->length[i] = vb2_plane_size(vb, i);

		if (buf->length[i] < format->plane_fmt[i].sizeimage)
			return -EINVAL;
	}

	return 0;
}

static void vsp1_video_buffer_queue(struct vb2_buffer *vb)
{
	struct vsp1_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct vsp1_pipeline *pipe = to_vsp1_pipeline(&video->video.entity);
	struct vsp1_video_buffer *buf = to_vsp1_video_buffer(vb);
	unsigned long flags;
	bool empty;

	spin_lock_irqsave(&video->irqlock, flags);
	empty = list_empty(&video->irqqueue);
	list_add_tail(&buf->queue, &video->irqqueue);
	spin_unlock_irqrestore(&video->irqlock, flags);

	if (!empty)
		return;

	spin_lock_irqsave(&pipe->irqlock, flags);

	video->ops->queue(video, buf);
	pipe->buffers_ready |= 1 << video->pipe_index;

	if (vb2_is_streaming(&video->queue) &&
	    vsp1_pipeline_ready(pipe))
		vsp1_pipeline_run(pipe);

	spin_unlock_irqrestore(&pipe->irqlock, flags);
}

static void vsp1_entity_route_setup(struct vsp1_entity *source)
{
	struct vsp1_entity *sink;

	if (source->route->reg == 0)
		return;

	sink = container_of(source->sink, struct vsp1_entity, subdev.entity);
	vsp1_write(source->vsp1, source->route->reg,
		   sink->route->inputs[source->sink_pad]);
}

static int vsp1_video_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vsp1_video *video = vb2_get_drv_priv(vq);
	struct vsp1_pipeline *pipe = to_vsp1_pipeline(&video->video.entity);
	struct vsp1_entity *entity;
	unsigned long flags;
	int ret;

	mutex_lock(&pipe->lock);
	if (pipe->stream_count == pipe->num_video - 1) {
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
	struct vsp1_video_buffer *buffer;
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

	vsp1_pipeline_cleanup(pipe);
	media_entity_pipeline_stop(&video->video.entity);

	/* Remove all buffers from the IRQ queue. */
	spin_lock_irqsave(&video->irqlock, flags);
	list_for_each_entry(buffer, &video->irqqueue, queue)
		vb2_buffer_done(&buffer->buf, VB2_BUF_STATE_ERROR);
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
	format->fmt.pix_mp = video->format;
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

	video->format = format->fmt.pix_mp;
	video->fmtinfo = info;

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

	ret = vsp1_pipeline_init(pipe, video);
	if (ret < 0)
		goto err_stop;

	/* Start the queue. */
	ret = vb2_streamon(&video->queue, type);
	if (ret < 0)
		goto err_cleanup;

	return 0;

err_cleanup:
	vsp1_pipeline_cleanup(pipe);
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

int vsp1_video_init(struct vsp1_video *video, struct vsp1_entity *rwpf)
{
	const char *direction;
	int ret;

	switch (video->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		direction = "output";
		video->pad.flags = MEDIA_PAD_FL_SINK;
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		direction = "input";
		video->pad.flags = MEDIA_PAD_FL_SOURCE;
		video->video.vfl_dir = VFL_DIR_TX;
		break;

	default:
		return -EINVAL;
	}

	video->rwpf = rwpf;

	mutex_init(&video->lock);
	spin_lock_init(&video->irqlock);
	INIT_LIST_HEAD(&video->irqqueue);

	mutex_init(&video->pipe.lock);
	spin_lock_init(&video->pipe.irqlock);
	INIT_LIST_HEAD(&video->pipe.entities);
	init_waitqueue_head(&video->pipe.wq);
	video->pipe.state = VSP1_PIPELINE_STOPPED;

	/* Initialize the media entity... */
	ret = media_entity_init(&video->video.entity, 1, &video->pad, 0);
	if (ret < 0)
		return ret;

	/* ... and the format ... */
	video->fmtinfo = vsp1_get_format_info(VSP1_VIDEO_DEF_FORMAT);
	video->format.pixelformat = video->fmtinfo->fourcc;
	video->format.colorspace = V4L2_COLORSPACE_SRGB;
	video->format.field = V4L2_FIELD_NONE;
	video->format.width = VSP1_VIDEO_DEF_WIDTH;
	video->format.height = VSP1_VIDEO_DEF_HEIGHT;
	video->format.num_planes = 1;
	video->format.plane_fmt[0].bytesperline =
		video->format.width * video->fmtinfo->bpp[0] / 8;
	video->format.plane_fmt[0].sizeimage =
		video->format.plane_fmt[0].bytesperline * video->format.height;

	/* ... and the video node... */
	video->video.v4l2_dev = &video->vsp1->v4l2_dev;
	video->video.fops = &vsp1_video_fops;
	snprintf(video->video.name, sizeof(video->video.name), "%s %s",
		 rwpf->subdev.name, direction);
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
	video->queue.buf_struct_size = sizeof(struct vsp1_video_buffer);
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

	return 0;

error:
	vb2_dma_contig_cleanup_ctx(video->alloc_ctx);
	vsp1_video_cleanup(video);
	return ret;
}

void vsp1_video_cleanup(struct vsp1_video *video)
{
	if (video_is_registered(&video->video))
		video_unregister_device(&video->video);

	vb2_dma_contig_cleanup_ctx(video->alloc_ctx);
	media_entity_cleanup(&video->video.entity);
}
