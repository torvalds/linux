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
#include "vsp1_bru.h"
#include "vsp1_dl.h"
#include "vsp1_drm.h"
#include "vsp1_lif.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"


/* -----------------------------------------------------------------------------
 * Interrupt Handling
 */

static void vsp1_du_pipeline_frame_end(struct vsp1_pipeline *pipe,
				       bool completed)
{
	struct vsp1_drm_pipeline *drm_pipe = to_vsp1_drm_pipeline(pipe);

	if (drm_pipe->du_complete)
		drm_pipe->du_complete(drm_pipe->du_private, completed);
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
 * and @cfg.height. This sets up formats on the blend unit (BRU or BRS) source
 * pad, the WPF sink and source pads, and the LIF sink pad.
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
	struct vsp1_bru *bru;
	struct v4l2_subdev_format format;
	const char *bru_name;
	unsigned int i;
	int ret;

	if (pipe_index >= vsp1->info->lif_count)
		return -EINVAL;

	drm_pipe = &vsp1->drm->pipe[pipe_index];
	pipe = &drm_pipe->pipe;
	bru = to_bru(&pipe->bru->subdev);
	bru_name = pipe->bru->type == VSP1_ENTITY_BRU ? "BRU" : "BRS";

	if (!cfg) {
		/*
		 * NULL configuration means the CRTC is being disabled, stop
		 * the pipeline and turn the light off.
		 */
		ret = vsp1_pipeline_stop(pipe);
		if (ret == -ETIMEDOUT)
			dev_err(vsp1->dev, "DRM pipeline stop timeout\n");

		media_pipeline_stop(&pipe->output->entity.subdev.entity);

		for (i = 0; i < ARRAY_SIZE(pipe->inputs); ++i) {
			struct vsp1_rwpf *rpf = pipe->inputs[i];

			if (!rpf)
				continue;

			/*
			 * Remove the RPF from the pipe and the list of BRU
			 * inputs.
			 */
			WARN_ON(list_empty(&rpf->entity.list_pipe));
			list_del_init(&rpf->entity.list_pipe);
			pipe->inputs[i] = NULL;

			bru->inputs[rpf->bru_input].rpf = NULL;
		}

		drm_pipe->du_complete = NULL;
		pipe->num_inputs = 0;

		vsp1_dlm_reset(pipe->output->dlm);
		vsp1_device_put(vsp1);

		dev_dbg(vsp1->dev, "%s: pipeline disabled\n", __func__);

		return 0;
	}

	dev_dbg(vsp1->dev, "%s: configuring LIF%u with format %ux%u\n",
		__func__, pipe_index, cfg->width, cfg->height);

	/*
	 * Configure the format at the BRU sinks and propagate it through the
	 * pipeline.
	 */
	memset(&format, 0, sizeof(format));
	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	for (i = 0; i < pipe->bru->source_pad; ++i) {
		format.pad = i;

		format.format.width = cfg->width;
		format.format.height = cfg->height;
		format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;
		format.format.field = V4L2_FIELD_NONE;

		ret = v4l2_subdev_call(&pipe->bru->subdev, pad,
				       set_fmt, NULL, &format);
		if (ret < 0)
			return ret;

		dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on %s pad %u\n",
			__func__, format.format.width, format.format.height,
			format.format.code, bru_name, i);
	}

	format.pad = pipe->bru->source_pad;
	format.format.width = cfg->width;
	format.format.height = cfg->height;
	format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;
	format.format.field = V4L2_FIELD_NONE;

	ret = v4l2_subdev_call(&pipe->bru->subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on %s pad %u\n",
		__func__, format.format.width, format.format.height,
		format.format.code, bru_name, i);

	format.pad = RWPF_PAD_SINK;
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
		format.format.code, pipe_index);

	/*
	 * Verify that the format at the output of the pipeline matches the
	 * requested frame size and media bus code.
	 */
	if (format.format.width != cfg->width ||
	    format.format.height != cfg->height ||
	    format.format.code != MEDIA_BUS_FMT_ARGB8888_1X32) {
		dev_dbg(vsp1->dev, "%s: format mismatch\n", __func__);
		return -EPIPE;
	}

	/*
	 * Mark the pipeline as streaming and enable the VSP1. This will store
	 * the pipeline pointer in all entities, which the s_stream handlers
	 * will need. We don't start the entities themselves right at this point
	 * as there's no plane configured yet, so we can't start processing
	 * buffers.
	 */
	ret = vsp1_device_get(vsp1);
	if (ret < 0)
		return ret;

	/*
	 * Register a callback to allow us to notify the DRM driver of frame
	 * completion events.
	 */
	drm_pipe->du_complete = cfg->callback;
	drm_pipe->du_private = cfg->callback_data;

	ret = media_pipeline_start(&pipe->output->entity.subdev.entity,
					  &pipe->pipe);
	if (ret < 0) {
		dev_dbg(vsp1->dev, "%s: pipeline start failed\n", __func__);
		vsp1_device_put(vsp1);
		return ret;
	}

	/* Disable the display interrupts. */
	vsp1_write(vsp1, VI6_DISP_IRQ_STA, 0);
	vsp1_write(vsp1, VI6_DISP_IRQ_ENB, 0);

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
	struct vsp1_drm_pipeline *drm_pipe = &vsp1->drm->pipe[pipe_index];

	drm_pipe->enabled = drm_pipe->pipe.num_inputs != 0;
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
		 * Remove the RPF from the pipe's inputs. The atomic flush
		 * handler will disable the input and remove the entity from the
		 * pipe's entities list.
		 */
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

static int vsp1_du_setup_rpf_pipe(struct vsp1_device *vsp1,
				  struct vsp1_pipeline *pipe,
				  struct vsp1_rwpf *rpf, unsigned int bru_input)
{
	struct v4l2_subdev_selection sel;
	struct v4l2_subdev_format format;
	const struct v4l2_rect *crop;
	int ret;

	/*
	 * Configure the format on the RPF sink pad and propagate it up to the
	 * BRU sink pad.
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

	/* BRU sink, propagate the format from the RPF source. */
	format.pad = bru_input;

	ret = v4l2_subdev_call(&pipe->bru->subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on BRU pad %u\n",
		__func__, format.format.width, format.format.height,
		format.format.code, format.pad);

	sel.pad = bru_input;
	sel.target = V4L2_SEL_TGT_COMPOSE;
	sel.r = vsp1->drm->inputs[rpf->entity.index].compose;

	ret = v4l2_subdev_call(&pipe->bru->subdev, pad, set_selection, NULL,
			       &sel);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: set selection (%u,%u)/%ux%u on BRU pad %u\n",
		__func__, sel.r.left, sel.r.top, sel.r.width, sel.r.height,
		sel.pad);

	return 0;
}

static unsigned int rpf_zpos(struct vsp1_device *vsp1, struct vsp1_rwpf *rpf)
{
	return vsp1->drm->inputs[rpf->entity.index].zpos;
}

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
	struct vsp1_rwpf *inputs[VSP1_MAX_RPF] = { NULL, };
	struct vsp1_bru *bru = to_bru(&pipe->bru->subdev);
	struct vsp1_entity *entity;
	struct vsp1_entity *next;
	struct vsp1_dl_list *dl;
	const char *bru_name;
	unsigned long flags;
	unsigned int i;
	int ret;

	bru_name = pipe->bru->type == VSP1_ENTITY_BRU ? "BRU" : "BRS";

	/* Prepare the display list. */
	dl = vsp1_dl_list_get(pipe->output->dlm);

	/* Count the number of enabled inputs and sort them by Z-order. */
	pipe->num_inputs = 0;

	for (i = 0; i < vsp1->info->rpf_count; ++i) {
		struct vsp1_rwpf *rpf = vsp1->rpf[i];
		unsigned int j;

		/*
		 * Make sure we don't accept more inputs than the hardware can
		 * handle. This is a temporary fix to avoid display stall, we
		 * need to instead allocate the BRU or BRS to display pipelines
		 * dynamically based on the number of planes they each use.
		 */
		if (pipe->num_inputs >= pipe->bru->source_pad)
			pipe->inputs[i] = NULL;

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

	/* Setup the RPF input pipeline for every enabled input. */
	for (i = 0; i < pipe->bru->source_pad; ++i) {
		struct vsp1_rwpf *rpf = inputs[i];

		if (!rpf) {
			bru->inputs[i].rpf = NULL;
			continue;
		}

		if (list_empty(&rpf->entity.list_pipe))
			list_add_tail(&rpf->entity.list_pipe, &pipe->entities);

		bru->inputs[i].rpf = rpf;
		rpf->bru_input = i;
		rpf->entity.sink = pipe->bru;
		rpf->entity.sink_pad = i;

		dev_dbg(vsp1->dev, "%s: connecting RPF.%u to %s:%u\n",
			__func__, rpf->entity.index, bru_name, i);

		ret = vsp1_du_setup_rpf_pipe(vsp1, pipe, rpf, i);
		if (ret < 0)
			dev_err(vsp1->dev,
				"%s: failed to setup RPF.%u\n",
				__func__, rpf->entity.index);
	}

	/* Configure all entities in the pipeline. */
	list_for_each_entry_safe(entity, next, &pipe->entities, list_pipe) {
		/* Disconnect unused RPFs from the pipeline. */
		if (entity->type == VSP1_ENTITY_RPF &&
		    !pipe->inputs[entity->index]) {
			vsp1_dl_list_write(dl, entity->route->reg,
					   VI6_DPR_NODE_UNUSED);

			list_del_init(&entity->list_pipe);

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

	vsp1_dl_list_commit(dl);

	/* Start or stop the pipeline if needed. */
	if (!drm_pipe->enabled && pipe->num_inputs) {
		spin_lock_irqsave(&pipe->irqlock, flags);
		vsp1_pipeline_run(pipe);
		spin_unlock_irqrestore(&pipe->irqlock, flags);
	} else if (drm_pipe->enabled && !pipe->num_inputs) {
		vsp1_pipeline_stop(pipe);
	}
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

	/* Create one DRM pipeline per LIF. */
	for (i = 0; i < vsp1->info->lif_count; ++i) {
		struct vsp1_drm_pipeline *drm_pipe = &vsp1->drm->pipe[i];
		struct vsp1_pipeline *pipe = &drm_pipe->pipe;

		vsp1_pipeline_init(pipe);

		/*
		 * The DRM pipeline is static, add entities manually. The first
		 * pipeline uses the BRU and the second pipeline the BRS.
		 */
		pipe->bru = i == 0 ? &vsp1->bru->entity : &vsp1->brs->entity;
		pipe->lif = &vsp1->lif[i]->entity;
		pipe->output = vsp1->wpf[i];
		pipe->output->pipe = pipe;
		pipe->frame_end = vsp1_du_pipeline_frame_end;

		pipe->bru->sink = &pipe->output->entity;
		pipe->bru->sink_pad = 0;
		pipe->output->entity.sink = pipe->lif;
		pipe->output->entity.sink_pad = 0;

		list_add_tail(&pipe->bru->list_pipe, &pipe->entities);
		list_add_tail(&pipe->lif->list_pipe, &pipe->entities);
		list_add_tail(&pipe->output->entity.list_pipe, &pipe->entities);
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
}
