/*
 * vsp1_drm.c  --  R-Car VSP1 DRM API
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>
#include <media/vsp1.h>

#include "vsp1.h"
#include "vsp1_brx.h"
#include "vsp1_dl.h"
#include "vsp1_drm.h"
#include "vsp1_lif.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"

#define BRX_NAME(e)	(e)->type == VSP1_ENTITY_BRU ? "BRU" : "BRS"

/* -----------------------------------------------------------------------------
 * Interrupt Handling
 */

static void vsp1_du_pipeline_frame_end(struct vsp1_pipeline *pipe,
				       unsigned int completion)
{
	struct vsp1_drm_pipeline *drm_pipe = to_vsp1_drm_pipeline(pipe);
	bool complete = completion == VSP1_DL_FRAME_END_COMPLETED;

	if (drm_pipe->du_complete)
		drm_pipe->du_complete(drm_pipe->du_private, complete);

	if (completion & VSP1_DL_FRAME_END_INTERNAL) {
		drm_pipe->force_brx_release = false;
		wake_up(&drm_pipe->wait_queue);
	}
}

/* -----------------------------------------------------------------------------
 * Pipeline Configuration
 */

/* Setup one RPF and the connected BRx sink pad. */
static int vsp1_du_pipeline_setup_rpf(struct vsp1_device *vsp1,
				      struct vsp1_pipeline *pipe,
				      struct vsp1_rwpf *rpf,
				      unsigned int brx_input)
{
	struct v4l2_subdev_selection sel;
	struct v4l2_subdev_format format;
	const struct v4l2_rect *crop;
	int ret;

	/*
	 * Configure the format on the RPF sink pad and propagate it up to the
	 * BRx sink pad.
	 */
	crop = &vsp1->drm->inputs[rpf->entity.index].crop;

	memset(&format, 0, sizeof(format));
	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	format.pad = RWPF_PAD_SINK;
	format.format.width = crop->width + crop->left;
	format.format.height = crop->height + crop->top;
	format.format.code = rpf->fmtinfo->mbus;
	format.format.field = V4L2_FIELD_NONE;

	ret = v4l2_subdev_call(&rpf->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: set format %ux%u (%x) on RPF%u sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code, rpf->entity.index);

	memset(&sel, 0, sizeof(sel));
	sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sel.pad = RWPF_PAD_SINK;
	sel.target = V4L2_SEL_TGT_CROP;
	sel.r = *crop;

	ret = v4l2_subdev_call(&rpf->entity.subdev, pad, set_selection, NULL,
			       &sel);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: set selection (%u,%u)/%ux%u on RPF%u sink\n",
		__func__, sel.r.left, sel.r.top, sel.r.width, sel.r.height,
		rpf->entity.index);

	/*
	 * RPF source, hardcode the format to ARGB8888 to turn on format
	 * conversion if needed.
	 */
	format.pad = RWPF_PAD_SOURCE;

	ret = v4l2_subdev_call(&rpf->entity.subdev, pad, get_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: got format %ux%u (%x) on RPF%u source\n",
		__func__, format.format.width, format.format.height,
		format.format.code, rpf->entity.index);

	format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;

	ret = v4l2_subdev_call(&rpf->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	/* BRx sink, propagate the format from the RPF source. */
	format.pad = brx_input;

	ret = v4l2_subdev_call(&pipe->brx->subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on %s pad %u\n",
		__func__, format.format.width, format.format.height,
		format.format.code, BRX_NAME(pipe->brx), format.pad);

	sel.pad = brx_input;
	sel.target = V4L2_SEL_TGT_COMPOSE;
	sel.r = vsp1->drm->inputs[rpf->entity.index].compose;

	ret = v4l2_subdev_call(&pipe->brx->subdev, pad, set_selection, NULL,
			       &sel);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set selection (%u,%u)/%ux%u on %s pad %u\n",
		__func__, sel.r.left, sel.r.top, sel.r.width, sel.r.height,
		BRX_NAME(pipe->brx), sel.pad);

	return 0;
}

/* Setup the BRx source pad. */
static int vsp1_du_pipeline_setup_inputs(struct vsp1_device *vsp1,
					 struct vsp1_pipeline *pipe);
static void vsp1_du_pipeline_configure(struct vsp1_pipeline *pipe);

static int vsp1_du_pipeline_setup_brx(struct vsp1_device *vsp1,
				      struct vsp1_pipeline *pipe)
{
	struct vsp1_drm_pipeline *drm_pipe = to_vsp1_drm_pipeline(pipe);
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct vsp1_entity *brx;
	int ret;

	/*
	 * Pick a BRx:
	 * - If we need more than two inputs, use the BRU.
	 * - Otherwise, if we are not forced to release our BRx, keep it.
	 * - Else, use any free BRx (randomly starting with the BRU).
	 */
	if (pipe->num_inputs > 2)
		brx = &vsp1->bru->entity;
	else if (pipe->brx && !drm_pipe->force_brx_release)
		brx = pipe->brx;
	else if (!vsp1->bru->entity.pipe)
		brx = &vsp1->bru->entity;
	else
		brx = &vsp1->brs->entity;

	/* Switch BRx if needed. */
	if (brx != pipe->brx) {
		struct vsp1_entity *released_brx = NULL;

		/* Release our BRx if we have one. */
		if (pipe->brx) {
			dev_dbg(vsp1->dev, "%s: pipe %u: releasing %s\n",
				__func__, pipe->lif->index,
				BRX_NAME(pipe->brx));

			/*
			 * The BRx might be acquired by the other pipeline in
			 * the next step. We must thus remove it from the list
			 * of entities for this pipeline. The other pipeline's
			 * hardware configuration will reconfigure the BRx
			 * routing.
			 *
			 * However, if the other pipeline doesn't acquire our
			 * BRx, we need to keep it in the list, otherwise the
			 * hardware configuration step won't disconnect it from
			 * the pipeline. To solve this, store the released BRx
			 * pointer to add it back to the list of entities later
			 * if it isn't acquired by the other pipeline.
			 */
			released_brx = pipe->brx;

			list_del(&pipe->brx->list_pipe);
			pipe->brx->sink = NULL;
			pipe->brx->pipe = NULL;
			pipe->brx = NULL;
		}

		/*
		 * If the BRx we need is in use, force the owner pipeline to
		 * switch to the other BRx and wait until the switch completes.
		 */
		if (brx->pipe) {
			struct vsp1_drm_pipeline *owner_pipe;

			dev_dbg(vsp1->dev, "%s: pipe %u: waiting for %s\n",
				__func__, pipe->lif->index, BRX_NAME(brx));

			owner_pipe = to_vsp1_drm_pipeline(brx->pipe);
			owner_pipe->force_brx_release = true;

			vsp1_du_pipeline_setup_inputs(vsp1, &owner_pipe->pipe);
			vsp1_du_pipeline_configure(&owner_pipe->pipe);

			ret = wait_event_timeout(owner_pipe->wait_queue,
						 !owner_pipe->force_brx_release,
						 msecs_to_jiffies(500));
			if (ret == 0)
				dev_warn(vsp1->dev,
					 "DRM pipeline %u reconfiguration timeout\n",
					 owner_pipe->pipe.lif->index);
		}

		/*
		 * If the BRx we have released previously hasn't been acquired
		 * by the other pipeline, add it back to the entities list (with
		 * the pipe pointer NULL) to let vsp1_du_pipeline_configure()
		 * disconnect it from the hardware pipeline.
		 */
		if (released_brx && !released_brx->pipe)
			list_add_tail(&released_brx->list_pipe,
				      &pipe->entities);

		/* Add the BRx to the pipeline. */
		dev_dbg(vsp1->dev, "%s: pipe %u: acquired %s\n",
			__func__, pipe->lif->index, BRX_NAME(brx));

		pipe->brx = brx;
		pipe->brx->pipe = pipe;
		pipe->brx->sink = &pipe->output->entity;
		pipe->brx->sink_pad = 0;

		list_add_tail(&pipe->brx->list_pipe, &pipe->entities);
	}

	/*
	 * Configure the format on the BRx source and verify that it matches the
	 * requested format. We don't set the media bus code as it is configured
	 * on the BRx sink pad 0 and propagated inside the entity, not on the
	 * source pad.
	 */
	format.pad = pipe->brx->source_pad;
	format.format.width = drm_pipe->width;
	format.format.height = drm_pipe->height;
	format.format.field = V4L2_FIELD_NONE;

	ret = v4l2_subdev_call(&pipe->brx->subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on %s pad %u\n",
		__func__, format.format.width, format.format.height,
		format.format.code, BRX_NAME(pipe->brx), pipe->brx->source_pad);

	if (format.format.width != drm_pipe->width ||
	    format.format.height != drm_pipe->height) {
		dev_dbg(vsp1->dev, "%s: format mismatch\n", __func__);
		return -EPIPE;
	}

	return 0;
}

static unsigned int rpf_zpos(struct vsp1_device *vsp1, struct vsp1_rwpf *rpf)
{
	return vsp1->drm->inputs[rpf->entity.index].zpos;
}

/* Setup the input side of the pipeline (RPFs and BRx). */
static int vsp1_du_pipeline_setup_inputs(struct vsp1_device *vsp1,
					struct vsp1_pipeline *pipe)
{
	struct vsp1_rwpf *inputs[VSP1_MAX_RPF] = { NULL, };
	struct vsp1_brx *brx;
	unsigned int i;
	int ret;

	/* Count the number of enabled inputs and sort them by Z-order. */
	pipe->num_inputs = 0;

	for (i = 0; i < vsp1->info->rpf_count; ++i) {
		struct vsp1_rwpf *rpf = vsp1->rpf[i];
		unsigned int j;

		if (!pipe->inputs[i])
			continue;

		/* Insert the RPF in the sorted RPFs array. */
		for (j = pipe->num_inputs++; j > 0; --j) {
			if (rpf_zpos(vsp1, inputs[j-1]) <= rpf_zpos(vsp1, rpf))
				break;
			inputs[j] = inputs[j-1];
		}

		inputs[j] = rpf;
	}

	/*
	 * Setup the BRx. This must be done before setting up the RPF input
	 * pipelines as the BRx sink compose rectangles depend on the BRx source
	 * format.
	 */
	ret = vsp1_du_pipeline_setup_brx(vsp1, pipe);
	if (ret < 0) {
		dev_err(vsp1->dev, "%s: failed to setup %s source\n", __func__,
			BRX_NAME(pipe->brx));
		return ret;
	}

	brx = to_brx(&pipe->brx->subdev);

	/* Setup the RPF input pipeline for every enabled input. */
	for (i = 0; i < pipe->brx->source_pad; ++i) {
		struct vsp1_rwpf *rpf = inputs[i];

		if (!rpf) {
			brx->inputs[i].rpf = NULL;
			continue;
		}

		if (!rpf->entity.pipe) {
			rpf->entity.pipe = pipe;
			list_add_tail(&rpf->entity.list_pipe, &pipe->entities);
		}

		brx->inputs[i].rpf = rpf;
		rpf->brx_input = i;
		rpf->entity.sink = pipe->brx;
		rpf->entity.sink_pad = i;

		dev_dbg(vsp1->dev, "%s: connecting RPF.%u to %s:%u\n",
			__func__, rpf->entity.index, BRX_NAME(pipe->brx), i);

		ret = vsp1_du_pipeline_setup_rpf(vsp1, pipe, rpf, i);
		if (ret < 0) {
			dev_err(vsp1->dev,
				"%s: failed to setup RPF.%u\n",
				__func__, rpf->entity.index);
			return ret;
		}
	}

	return 0;
}

/* Setup the output side of the pipeline (WPF and LIF). */
static int vsp1_du_pipeline_setup_output(struct vsp1_device *vsp1,
					 struct vsp1_pipeline *pipe)
{
	struct vsp1_drm_pipeline *drm_pipe = to_vsp1_drm_pipeline(pipe);
	struct v4l2_subdev_format format = { 0, };
	int ret;

	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	format.pad = RWPF_PAD_SINK;
	format.format.width = drm_pipe->width;
	format.format.height = drm_pipe->height;
	format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;
	format.format.field = V4L2_FIELD_NONE;

	ret = v4l2_subdev_call(&pipe->output->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on WPF%u sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code, pipe->output->entity.index);

	format.pad = RWPF_PAD_SOURCE;
	ret = v4l2_subdev_call(&pipe->output->entity.subdev, pad, get_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: got format %ux%u (%x) on WPF%u source\n",
		__func__, format.format.width, format.format.height,
		format.format.code, pipe->output->entity.index);

	format.pad = LIF_PAD_SINK;
	ret = v4l2_subdev_call(&pipe->lif->subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on LIF%u sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code, pipe->lif->index);

	/*
	 * Verify that the format at the output of the pipeline matches the
	 * requested frame size and media bus code.
	 */
	if (format.format.width != drm_pipe->width ||
	    format.format.height != drm_pipe->height ||
	    format.format.code != MEDIA_BUS_FMT_ARGB8888_1X32) {
		dev_dbg(vsp1->dev, "%s: format mismatch on LIF%u\n", __func__,
			pipe->lif->index);
		return -EPIPE;
	}

	return 0;
}

/* Configure all entities in the pipeline. */
static void vsp1_du_pipeline_configure(struct vsp1_pipeline *pipe)
{
	struct vsp1_drm_pipeline *drm_pipe = to_vsp1_drm_pipeline(pipe);
	struct vsp1_entity *entity;
	struct vsp1_entity *next;
	struct vsp1_dl_list *dl;

	dl = vsp1_dl_list_get(pipe->output->dlm);

	list_for_each_entry_safe(entity, next, &pipe->entities, list_pipe) {
		/* Disconnect unused entities from the pipeline. */
		if (!entity->pipe) {
			vsp1_dl_list_write(dl, entity->route->reg,
					   VI6_DPR_NODE_UNUSED);

			entity->sink = NULL;
			list_del(&entity->list_pipe);

			continue;
		}

		vsp1_entity_route_setup(entity, pipe, dl);

		if (entity->ops->configure) {
			entity->ops->configure(entity, pipe, dl,
					       VSP1_ENTITY_PARAMS_INIT);
			entity->ops->configure(entity, pipe, dl,
					       VSP1_ENTITY_PARAMS_RUNTIME);
			entity->ops->configure(entity, pipe, dl,
					       VSP1_ENTITY_PARAMS_PARTITION);
		}
	}

	vsp1_dl_list_commit(dl, drm_pipe->force_brx_release);
}

/* -----------------------------------------------------------------------------
 * DU Driver API
 */

int vsp1_du_init(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	if (!vsp1)
		return -EPROBE_DEFER;

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_init);

/**
 * vsp1_du_setup_lif - Setup the output part of the VSP pipeline
 * @dev: the VSP device
 * @pipe_index: the DRM pipeline index
 * @cfg: the LIF configuration
 *
 * Configure the output part of VSP DRM pipeline for the given frame @cfg.width
 * and @cfg.height. This sets up formats on the BRx source pad, the WPF sink and
 * source pads, and the LIF sink pad.
 *
 * The @pipe_index argument selects which DRM pipeline to setup. The number of
 * available pipelines depend on the VSP instance.
 *
 * As the media bus code on the blend unit source pad is conditioned by the
 * configuration of its sink 0 pad, we also set up the formats on all blend unit
 * sinks, even if the configuration will be overwritten later by
 * vsp1_du_setup_rpf(). This ensures that the blend unit configuration is set to
 * a well defined state.
 *
 * Return 0 on success or a negative error code on failure.
 */
int vsp1_du_setup_lif(struct device *dev, unsigned int pipe_index,
		      const struct vsp1_du_lif_config *cfg)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_drm_pipeline *drm_pipe;
	struct vsp1_pipeline *pipe;
	unsigned long flags;
	unsigned int i;
	int ret;

	if (pipe_index >= vsp1->info->lif_count)
		return -EINVAL;

	drm_pipe = &vsp1->drm->pipe[pipe_index];
	pipe = &drm_pipe->pipe;

	if (!cfg) {
		struct vsp1_brx *brx;

		mutex_lock(&vsp1->drm->lock);

		brx = to_brx(&pipe->brx->subdev);

		/*
		 * NULL configuration means the CRTC is being disabled, stop
		 * the pipeline and turn the light off.
		 */
		ret = vsp1_pipeline_stop(pipe);
		if (ret == -ETIMEDOUT)
			dev_err(vsp1->dev, "DRM pipeline stop timeout\n");

		for (i = 0; i < ARRAY_SIZE(pipe->inputs); ++i) {
			struct vsp1_rwpf *rpf = pipe->inputs[i];

			if (!rpf)
				continue;

			/*
			 * Remove the RPF from the pipe and the list of BRx
			 * inputs.
			 */
			WARN_ON(!rpf->entity.pipe);
			rpf->entity.pipe = NULL;
			list_del(&rpf->entity.list_pipe);
			pipe->inputs[i] = NULL;

			brx->inputs[rpf->brx_input].rpf = NULL;
		}

		drm_pipe->du_complete = NULL;
		pipe->num_inputs = 0;

		dev_dbg(vsp1->dev, "%s: pipe %u: releasing %s\n",
			__func__, pipe->lif->index,
			BRX_NAME(pipe->brx));

		list_del(&pipe->brx->list_pipe);
		pipe->brx->pipe = NULL;
		pipe->brx = NULL;

		mutex_unlock(&vsp1->drm->lock);

		vsp1_dlm_reset(pipe->output->dlm);
		vsp1_device_put(vsp1);

		dev_dbg(vsp1->dev, "%s: pipeline disabled\n", __func__);

		return 0;
	}

	drm_pipe->width = cfg->width;
	drm_pipe->height = cfg->height;

	dev_dbg(vsp1->dev, "%s: configuring LIF%u with format %ux%u\n",
		__func__, pipe_index, cfg->width, cfg->height);

	mutex_lock(&vsp1->drm->lock);

	/* Setup formats through the pipeline. */
	ret = vsp1_du_pipeline_setup_inputs(vsp1, pipe);
	if (ret < 0)
		goto unlock;

	ret = vsp1_du_pipeline_setup_output(vsp1, pipe);
	if (ret < 0)
		goto unlock;

	/* Enable the VSP1. */
	ret = vsp1_device_get(vsp1);
	if (ret < 0)
		goto unlock;

	/*
	 * Register a callback to allow us to notify the DRM driver of frame
	 * completion events.
	 */
	drm_pipe->du_complete = cfg->callback;
	drm_pipe->du_private = cfg->callback_data;

	/* Disable the display interrupts. */
	vsp1_write(vsp1, VI6_DISP_IRQ_STA, 0);
	vsp1_write(vsp1, VI6_DISP_IRQ_ENB, 0);

	/* Configure all entities in the pipeline. */
	vsp1_du_pipeline_configure(pipe);

unlock:
	mutex_unlock(&vsp1->drm->lock);

	if (ret < 0)
		return ret;

	/* Start the pipeline. */
	spin_lock_irqsave(&pipe->irqlock, flags);
	vsp1_pipeline_run(pipe);
	spin_unlock_irqrestore(&pipe->irqlock, flags);

	dev_dbg(vsp1->dev, "%s: pipeline enabled\n", __func__);

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_setup_lif);

/**
 * vsp1_du_atomic_begin - Prepare for an atomic update
 * @dev: the VSP device
 * @pipe_index: the DRM pipeline index
 */
void vsp1_du_atomic_begin(struct device *dev, unsigned int pipe_index)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	mutex_lock(&vsp1->drm->lock);
}
EXPORT_SYMBOL_GPL(vsp1_du_atomic_begin);

/**
 * vsp1_du_atomic_update - Setup one RPF input of the VSP pipeline
 * @dev: the VSP device
 * @pipe_index: the DRM pipeline index
 * @rpf_index: index of the RPF to setup (0-based)
 * @cfg: the RPF configuration
 *
 * Configure the VSP to perform image composition through RPF @rpf_index as
 * described by the @cfg configuration. The image to compose is referenced by
 * @cfg.mem and composed using the @cfg.src crop rectangle and the @cfg.dst
 * composition rectangle. The Z-order is configurable with higher @zpos values
 * displayed on top.
 *
 * If the @cfg configuration is NULL, the RPF will be disabled. Calling the
 * function on a disabled RPF is allowed.
 *
 * Image format as stored in memory is expressed as a V4L2 @cfg.pixelformat
 * value. The memory pitch is configurable to allow for padding at end of lines,
 * or simply for images that extend beyond the crop rectangle boundaries. The
 * @cfg.pitch value is expressed in bytes and applies to all planes for
 * multiplanar formats.
 *
 * The source memory buffer is referenced by the DMA address of its planes in
 * the @cfg.mem array. Up to two planes are supported. The second plane DMA
 * address is ignored for formats using a single plane.
 *
 * This function isn't reentrant, the caller needs to serialize calls.
 *
 * Return 0 on success or a negative error code on failure.
 */
int vsp1_du_atomic_update(struct device *dev, unsigned int pipe_index,
			  unsigned int rpf_index,
			  const struct vsp1_du_atomic_config *cfg)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_drm_pipeline *drm_pipe = &vsp1->drm->pipe[pipe_index];
	const struct vsp1_format_info *fmtinfo;
	struct vsp1_rwpf *rpf;

	if (rpf_index >= vsp1->info->rpf_count)
		return -EINVAL;

	rpf = vsp1->rpf[rpf_index];

	if (!cfg) {
		dev_dbg(vsp1->dev, "%s: RPF%u: disable requested\n", __func__,
			rpf_index);

		/*
		 * Remove the RPF from the pipeline's inputs. Keep it in the
		 * pipeline's entity list to let vsp1_du_pipeline_configure()
		 * remove it from the hardware pipeline.
		 */
		rpf->entity.pipe = NULL;
		drm_pipe->pipe.inputs[rpf_index] = NULL;
		return 0;
	}

	dev_dbg(vsp1->dev,
		"%s: RPF%u: (%u,%u)/%ux%u -> (%u,%u)/%ux%u (%08x), pitch %u dma { %pad, %pad, %pad } zpos %u\n",
		__func__, rpf_index,
		cfg->src.left, cfg->src.top, cfg->src.width, cfg->src.height,
		cfg->dst.left, cfg->dst.top, cfg->dst.width, cfg->dst.height,
		cfg->pixelformat, cfg->pitch, &cfg->mem[0], &cfg->mem[1],
		&cfg->mem[2], cfg->zpos);

	/*
	 * Store the format, stride, memory buffer address, crop and compose
	 * rectangles and Z-order position and for the input.
	 */
	fmtinfo = vsp1_get_format_info(vsp1, cfg->pixelformat);
	if (!fmtinfo) {
		dev_dbg(vsp1->dev, "Unsupport pixel format %08x for RPF\n",
			cfg->pixelformat);
		return -EINVAL;
	}

	rpf->fmtinfo = fmtinfo;
	rpf->format.num_planes = fmtinfo->planes;
	rpf->format.plane_fmt[0].bytesperline = cfg->pitch;
	rpf->format.plane_fmt[1].bytesperline = cfg->pitch;
	rpf->alpha = cfg->alpha;

	rpf->mem.addr[0] = cfg->mem[0];
	rpf->mem.addr[1] = cfg->mem[1];
	rpf->mem.addr[2] = cfg->mem[2];

	vsp1->drm->inputs[rpf_index].crop = cfg->src;
	vsp1->drm->inputs[rpf_index].compose = cfg->dst;
	vsp1->drm->inputs[rpf_index].zpos = cfg->zpos;

	drm_pipe->pipe.inputs[rpf_index] = rpf;

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_atomic_update);

/**
 * vsp1_du_atomic_flush - Commit an atomic update
 * @dev: the VSP device
 * @pipe_index: the DRM pipeline index
 */
void vsp1_du_atomic_flush(struct device *dev, unsigned int pipe_index)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_drm_pipeline *drm_pipe = &vsp1->drm->pipe[pipe_index];
	struct vsp1_pipeline *pipe = &drm_pipe->pipe;

	vsp1_du_pipeline_setup_inputs(vsp1, pipe);
	vsp1_du_pipeline_configure(pipe);
	mutex_unlock(&vsp1->drm->lock);
}
EXPORT_SYMBOL_GPL(vsp1_du_atomic_flush);

int vsp1_du_map_sg(struct device *dev, struct sg_table *sgt)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	/*
	 * As all the buffers allocated by the DU driver are coherent, we can
	 * skip cache sync. This will need to be revisited when support for
	 * non-coherent buffers will be added to the DU driver.
	 */
	return dma_map_sg_attrs(vsp1->bus_master, sgt->sgl, sgt->nents,
				DMA_TO_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
}
EXPORT_SYMBOL_GPL(vsp1_du_map_sg);

void vsp1_du_unmap_sg(struct device *dev, struct sg_table *sgt)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	dma_unmap_sg_attrs(vsp1->bus_master, sgt->sgl, sgt->nents,
			   DMA_TO_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
}
EXPORT_SYMBOL_GPL(vsp1_du_unmap_sg);

/* -----------------------------------------------------------------------------
 * Initialization
 */

int vsp1_drm_init(struct vsp1_device *vsp1)
{
	unsigned int i;

	vsp1->drm = devm_kzalloc(vsp1->dev, sizeof(*vsp1->drm), GFP_KERNEL);
	if (!vsp1->drm)
		return -ENOMEM;

	mutex_init(&vsp1->drm->lock);

	/* Create one DRM pipeline per LIF. */
	for (i = 0; i < vsp1->info->lif_count; ++i) {
		struct vsp1_drm_pipeline *drm_pipe = &vsp1->drm->pipe[i];
		struct vsp1_pipeline *pipe = &drm_pipe->pipe;

		init_waitqueue_head(&drm_pipe->wait_queue);

		vsp1_pipeline_init(pipe);

		pipe->frame_end = vsp1_du_pipeline_frame_end;

		/*
		 * The output side of the DRM pipeline is static, add the
		 * corresponding entities manually.
		 */
		pipe->output = vsp1->wpf[i];
		pipe->lif = &vsp1->lif[i]->entity;

		pipe->output->entity.pipe = pipe;
		pipe->output->entity.sink = pipe->lif;
		pipe->output->entity.sink_pad = 0;
		list_add_tail(&pipe->output->entity.list_pipe, &pipe->entities);

		pipe->lif->pipe = pipe;
		list_add_tail(&pipe->lif->list_pipe, &pipe->entities);
	}

	/* Disable all RPFs initially. */
	for (i = 0; i < vsp1->info->rpf_count; ++i) {
		struct vsp1_rwpf *input = vsp1->rpf[i];

		INIT_LIST_HEAD(&input->entity.list_pipe);
	}

	return 0;
}

void vsp1_drm_cleanup(struct vsp1_device *vsp1)
{
	mutex_destroy(&vsp1->drm->lock);
}
