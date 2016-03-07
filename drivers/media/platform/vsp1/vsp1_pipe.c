/*
 * vsp1_pipe.c  --  R-Car VSP1 Pipeline
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

#include <linux/delay.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_bru.h"
#include "vsp1_dl.h"
#include "vsp1_entity.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"
#include "vsp1_uds.h"

/* -----------------------------------------------------------------------------
 * Helper Functions
 */

static const struct vsp1_format_info vsp1_video_formats[] = {
	{ V4L2_PIX_FMT_RGB332, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGB_332, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 8, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_ARGB444, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_4444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_XRGB444, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_XRGB_4444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_ARGB555, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_1555, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_XRGB555, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_XRGB_1555, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_RGB565, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGB_565, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_BGR24, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_BGR_888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 24, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_RGB24, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGB_888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 24, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_ABGR32, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS,
	  1, { 32, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_XBGR32, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS,
	  1, { 32, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_ARGB32, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 32, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_XRGB32, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 32, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_UYVY, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 16, 0, 0 }, false, false, 2, 1, false },
	{ V4L2_PIX_FMT_VYUY, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 16, 0, 0 }, false, true, 2, 1, false },
	{ V4L2_PIX_FMT_YUYV, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 16, 0, 0 }, true, false, 2, 1, false },
	{ V4L2_PIX_FMT_YVYU, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 16, 0, 0 }, true, true, 2, 1, false },
	{ V4L2_PIX_FMT_NV12M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_UV_420, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  2, { 8, 16, 0 }, false, false, 2, 2, false },
	{ V4L2_PIX_FMT_NV21M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_UV_420, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  2, { 8, 16, 0 }, false, true, 2, 2, false },
	{ V4L2_PIX_FMT_NV16M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_UV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  2, { 8, 16, 0 }, false, false, 2, 1, false },
	{ V4L2_PIX_FMT_NV61M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_UV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  2, { 8, 16, 0 }, false, true, 2, 1, false },
	{ V4L2_PIX_FMT_YUV420M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_420, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, false, 2, 2, false },
	{ V4L2_PIX_FMT_YVU420M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_420, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, true, 2, 2, false },
	{ V4L2_PIX_FMT_YUV422M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, false, 2, 1, false },
	{ V4L2_PIX_FMT_YVU422M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, true, 2, 1, false },
	{ V4L2_PIX_FMT_YUV444M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_YVU444M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, true, 1, 1, false },
};

/*
 * vsp1_get_format_info - Retrieve format information for a 4CC
 * @fourcc: the format 4CC
 *
 * Return a pointer to the format information structure corresponding to the
 * given V4L2 format 4CC, or NULL if no corresponding format can be found.
 */
const struct vsp1_format_info *vsp1_get_format_info(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vsp1_video_formats); ++i) {
		const struct vsp1_format_info *info = &vsp1_video_formats[i];

		if (info->fourcc == fourcc)
			return info;
	}

	return NULL;
}

/* -----------------------------------------------------------------------------
 * Pipeline Management
 */

void vsp1_pipeline_reset(struct vsp1_pipeline *pipe)
{
	unsigned int i;

	if (pipe->bru) {
		struct vsp1_bru *bru = to_bru(&pipe->bru->subdev);

		for (i = 0; i < ARRAY_SIZE(bru->inputs); ++i)
			bru->inputs[i].rpf = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(pipe->inputs); ++i)
		pipe->inputs[i] = NULL;

	INIT_LIST_HEAD(&pipe->entities);
	pipe->state = VSP1_PIPELINE_STOPPED;
	pipe->buffers_ready = 0;
	pipe->num_inputs = 0;
	pipe->output = NULL;
	pipe->bru = NULL;
	pipe->lif = NULL;
	pipe->uds = NULL;
}

void vsp1_pipeline_init(struct vsp1_pipeline *pipe)
{
	mutex_init(&pipe->lock);
	spin_lock_init(&pipe->irqlock);
	init_waitqueue_head(&pipe->wq);

	INIT_LIST_HEAD(&pipe->entities);
	pipe->state = VSP1_PIPELINE_STOPPED;
}

void vsp1_pipeline_run(struct vsp1_pipeline *pipe)
{
	struct vsp1_device *vsp1 = pipe->output->entity.vsp1;

	if (pipe->state == VSP1_PIPELINE_STOPPED) {
		vsp1_write(vsp1, VI6_CMD(pipe->output->entity.index),
			   VI6_CMD_STRCMD);
		pipe->state = VSP1_PIPELINE_RUNNING;
	}

	pipe->buffers_ready = 0;
}

bool vsp1_pipeline_stopped(struct vsp1_pipeline *pipe)
{
	unsigned long flags;
	bool stopped;

	spin_lock_irqsave(&pipe->irqlock, flags);
	stopped = pipe->state == VSP1_PIPELINE_STOPPED;
	spin_unlock_irqrestore(&pipe->irqlock, flags);

	return stopped;
}

int vsp1_pipeline_stop(struct vsp1_pipeline *pipe)
{
	struct vsp1_entity *entity;
	unsigned long flags;
	int ret;

	if (pipe->dl) {
		/* When using display lists in continuous frame mode the only
		 * way to stop the pipeline is to reset the hardware.
		 */
		ret = vsp1_reset_wpf(pipe->output->entity.vsp1,
				     pipe->output->entity.index);
		if (ret == 0) {
			spin_lock_irqsave(&pipe->irqlock, flags);
			pipe->state = VSP1_PIPELINE_STOPPED;
			spin_unlock_irqrestore(&pipe->irqlock, flags);
		}
	} else {
		/* Otherwise just request a stop and wait. */
		spin_lock_irqsave(&pipe->irqlock, flags);
		if (pipe->state == VSP1_PIPELINE_RUNNING)
			pipe->state = VSP1_PIPELINE_STOPPING;
		spin_unlock_irqrestore(&pipe->irqlock, flags);

		ret = wait_event_timeout(pipe->wq, vsp1_pipeline_stopped(pipe),
					 msecs_to_jiffies(500));
		ret = ret == 0 ? -ETIMEDOUT : 0;
	}

	list_for_each_entry(entity, &pipe->entities, list_pipe) {
		if (entity->route && entity->route->reg)
			vsp1_write(entity->vsp1, entity->route->reg,
				   VI6_DPR_NODE_UNUSED);

		v4l2_subdev_call(&entity->subdev, video, s_stream, 0);
	}

	return ret;
}

bool vsp1_pipeline_ready(struct vsp1_pipeline *pipe)
{
	unsigned int mask;

	mask = ((1 << pipe->num_inputs) - 1) << 1;
	if (!pipe->lif)
		mask |= 1 << 0;

	return pipe->buffers_ready == mask;
}

void vsp1_pipeline_display_start(struct vsp1_pipeline *pipe)
{
	if (pipe->dl)
		vsp1_dl_irq_display_start(pipe->dl);
}

void vsp1_pipeline_frame_end(struct vsp1_pipeline *pipe)
{
	enum vsp1_pipeline_state state;
	unsigned long flags;

	if (pipe == NULL)
		return;

	if (pipe->dl)
		vsp1_dl_irq_frame_end(pipe->dl);

	/* Signal frame end to the pipeline handler. */
	pipe->frame_end(pipe);

	spin_lock_irqsave(&pipe->irqlock, flags);

	state = pipe->state;

	/* When using display lists in continuous frame mode the pipeline is
	 * automatically restarted by the hardware.
	 */
	if (!pipe->dl)
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

/*
 * Propagate the alpha value through the pipeline.
 *
 * As the UDS has restricted scaling capabilities when the alpha component needs
 * to be scaled, we disable alpha scaling when the UDS input has a fixed alpha
 * value. The UDS then outputs a fixed alpha value which needs to be programmed
 * from the input RPF alpha.
 */
void vsp1_pipeline_propagate_alpha(struct vsp1_pipeline *pipe,
				   struct vsp1_entity *input,
				   unsigned int alpha)
{
	struct vsp1_entity *entity;
	struct media_pad *pad;

	pad = media_entity_remote_pad(&input->pads[RWPF_PAD_SOURCE]);

	while (pad) {
		if (!is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = to_vsp1_entity(media_entity_to_v4l2_subdev(pad->entity));

		/* The BRU background color has a fixed alpha value set to 255,
		 * the output alpha value is thus always equal to 255.
		 */
		if (entity->type == VSP1_ENTITY_BRU)
			alpha = 255;

		if (entity->type == VSP1_ENTITY_UDS) {
			struct vsp1_uds *uds = to_uds(&entity->subdev);

			vsp1_uds_set_alpha(uds, alpha);
			break;
		}

		pad = &entity->pads[entity->source_pad];
		pad = media_entity_remote_pad(pad);
	}
}

void vsp1_pipelines_suspend(struct vsp1_device *vsp1)
{
	unsigned long flags;
	unsigned int i;
	int ret;

	/* To avoid increasing the system suspend time needlessly, loop over the
	 * pipelines twice, first to set them all to the stopping state, and
	 * then to wait for the stop to complete.
	 */
	for (i = 0; i < vsp1->info->wpf_count; ++i) {
		struct vsp1_rwpf *wpf = vsp1->wpf[i];
		struct vsp1_pipeline *pipe;

		if (wpf == NULL)
			continue;

		pipe = to_vsp1_pipeline(&wpf->entity.subdev.entity);
		if (pipe == NULL)
			continue;

		spin_lock_irqsave(&pipe->irqlock, flags);
		if (pipe->state == VSP1_PIPELINE_RUNNING)
			pipe->state = VSP1_PIPELINE_STOPPING;
		spin_unlock_irqrestore(&pipe->irqlock, flags);
	}

	for (i = 0; i < vsp1->info->wpf_count; ++i) {
		struct vsp1_rwpf *wpf = vsp1->wpf[i];
		struct vsp1_pipeline *pipe;

		if (wpf == NULL)
			continue;

		pipe = to_vsp1_pipeline(&wpf->entity.subdev.entity);
		if (pipe == NULL)
			continue;

		ret = wait_event_timeout(pipe->wq, vsp1_pipeline_stopped(pipe),
					 msecs_to_jiffies(500));
		if (ret == 0)
			dev_warn(vsp1->dev, "pipeline %u stop timeout\n",
				 wpf->entity.index);
	}
}

void vsp1_pipelines_resume(struct vsp1_device *vsp1)
{
	unsigned int i;

	/* Resume pipeline all running pipelines. */
	for (i = 0; i < vsp1->info->wpf_count; ++i) {
		struct vsp1_rwpf *wpf = vsp1->wpf[i];
		struct vsp1_pipeline *pipe;

		if (wpf == NULL)
			continue;

		pipe = to_vsp1_pipeline(&wpf->entity.subdev.entity);
		if (pipe == NULL)
			continue;

		if (vsp1_pipeline_ready(pipe))
			vsp1_pipeline_run(pipe);
	}
}
