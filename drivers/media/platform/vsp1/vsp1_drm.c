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

void vsp1_drm_display_start(struct vsp1_device *vsp1)
{
	vsp1_dlm_irq_display_start(vsp1->drm->pipe.output->dlm);
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
 * @width: output frame width in pixels
 * @height: output frame height in pixels
 *
 * Configure the output part of VSP DRM pipeline for the given frame @width and
 * @height. This sets up formats on the BRU source pad, the WPF0 sink and source
 * pads, and the LIF sink pad.
 *
 * As the media bus code on the BRU source pad is conditioned by the
 * configuration of the BRU sink 0 pad, we also set up the formats on all BRU
 * sinks, even if the configuration will be overwritten later by
 * vsp1_du_setup_rpf(). This ensures that the BRU configuration is set to a well
 * defined state.
 *
 * Return 0 on success or a negative error code on failure.
 */
int vsp1_du_setup_lif(struct device *dev, unsigned int width,
		      unsigned int height)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe;
	struct vsp1_bru *bru = vsp1->bru;
	struct v4l2_subdev_format format;
	unsigned int i;
	int ret;

	dev_dbg(vsp1->dev, "%s: configuring LIF with format %ux%u\n",
		__func__, width, height);

	if (width == 0 || height == 0) {
		/* Zero width or height means the CRTC is being disabled, stop
		 * the pipeline and turn the light off.
		 */
		ret = vsp1_pipeline_stop(pipe);
		if (ret == -ETIMEDOUT)
			dev_err(vsp1->dev, "DRM pipeline stop timeout\n");

		media_entity_pipeline_stop(&pipe->output->entity.subdev.entity);

		for (i = 0; i < bru->entity.source_pad; ++i) {
			vsp1->drm->inputs[i].enabled = false;
			bru->inputs[i].rpf = NULL;
			pipe->inputs[i] = NULL;
		}

		pipe->num_inputs = 0;

		vsp1_dlm_reset(pipe->output->dlm);
		vsp1_device_put(vsp1);

		dev_dbg(vsp1->dev, "%s: pipeline disabled\n", __func__);

		return 0;
	}

	/* Configure the format at the BRU sinks and propagate it through the
	 * pipeline.
	 */
	memset(&format, 0, sizeof(format));
	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	for (i = 0; i < bru->entity.source_pad; ++i) {
		format.pad = i;

		format.format.width = width;
		format.format.height = height;
		format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;
		format.format.field = V4L2_FIELD_NONE;

		ret = v4l2_subdev_call(&bru->entity.subdev, pad,
				       set_fmt, NULL, &format);
		if (ret < 0)
			return ret;

		dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on BRU pad %u\n",
			__func__, format.format.width, format.format.height,
			format.format.code, i);
	}

	format.pad = bru->entity.source_pad;
	format.format.width = width;
	format.format.height = height;
	format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;
	format.format.field = V4L2_FIELD_NONE;

	ret = v4l2_subdev_call(&bru->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on BRU pad %u\n",
		__func__, format.format.width, format.format.height,
		format.format.code, i);

	format.pad = RWPF_PAD_SINK;
	ret = v4l2_subdev_call(&vsp1->wpf[0]->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on WPF0 sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code);

	format.pad = RWPF_PAD_SOURCE;
	ret = v4l2_subdev_call(&vsp1->wpf[0]->entity.subdev, pad, get_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: got format %ux%u (%x) on WPF0 source\n",
		__func__, format.format.width, format.format.height,
		format.format.code);

	format.pad = LIF_PAD_SINK;
	ret = v4l2_subdev_call(&vsp1->lif->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on LIF sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code);

	/* Verify that the format at the output of the pipeline matches the
	 * requested frame size and media bus code.
	 */
	if (format.format.width != width || format.format.height != height ||
	    format.format.code != MEDIA_BUS_FMT_ARGB8888_1X32) {
		dev_dbg(vsp1->dev, "%s: format mismatch\n", __func__);
		return -EPIPE;
	}

	/* Mark the pipeline as streaming and enable the VSP1. This will store
	 * the pipeline pointer in all entities, which the s_stream handlers
	 * will need. We don't start the entities themselves right at this point
	 * as there's no plane configured yet, so we can't start processing
	 * buffers.
	 */
	ret = vsp1_device_get(vsp1);
	if (ret < 0)
		return ret;

	ret = media_entity_pipeline_start(&pipe->output->entity.subdev.entity,
					  &pipe->pipe);
	if (ret < 0) {
		dev_dbg(vsp1->dev, "%s: pipeline start failed\n", __func__);
		vsp1_device_put(vsp1);
		return ret;
	}

	dev_dbg(vsp1->dev, "%s: pipeline enabled\n", __func__);

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_setup_lif);

/**
 * vsp1_du_atomic_begin - Prepare for an atomic update
 * @dev: the VSP device
 */
void vsp1_du_atomic_begin(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe;

	vsp1->drm->num_inputs = pipe->num_inputs;

	/* Prepare the display list. */
	pipe->dl = vsp1_dl_list_get(pipe->output->dlm);
}
EXPORT_SYMBOL_GPL(vsp1_du_atomic_begin);

/**
 * vsp1_du_atomic_update - Setup one RPF input of the VSP pipeline
 * @dev: the VSP device
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
int vsp1_du_atomic_update(struct device *dev, unsigned int rpf_index,
			  const struct vsp1_du_atomic_config *cfg)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	const struct vsp1_format_info *fmtinfo;
	struct vsp1_rwpf *rpf;

	if (rpf_index >= vsp1->info->rpf_count)
		return -EINVAL;

	rpf = vsp1->rpf[rpf_index];

	if (!cfg) {
		dev_dbg(vsp1->dev, "%s: RPF%u: disable requested\n", __func__,
			rpf_index);

		vsp1->drm->inputs[rpf_index].enabled = false;
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
	vsp1->drm->inputs[rpf_index].enabled = true;

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_atomic_update);

static int vsp1_du_setup_rpf_pipe(struct vsp1_device *vsp1,
				  struct vsp1_rwpf *rpf, unsigned int bru_input)
{
	struct v4l2_subdev_selection sel;
	struct v4l2_subdev_format format;
	const struct v4l2_rect *crop;
	int ret;

	/* Configure the format on the RPF sink pad and propagate it up to the
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

	/* RPF source, hardcode the format to ARGB8888 to turn on format
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

	ret = v4l2_subdev_call(&vsp1->bru->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on BRU pad %u\n",
		__func__, format.format.width, format.format.height,
		format.format.code, format.pad);

	sel.pad = bru_input;
	sel.target = V4L2_SEL_TGT_COMPOSE;
	sel.r = vsp1->drm->inputs[rpf->entity.index].compose;

	ret = v4l2_subdev_call(&vsp1->bru->entity.subdev, pad, set_selection,
			       NULL, &sel);
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
 */
void vsp1_du_atomic_flush(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe;
	struct vsp1_rwpf *inputs[VSP1_MAX_RPF] = { NULL, };
	struct vsp1_entity *entity;
	unsigned long flags;
	unsigned int i;
	int ret;

	/* Count the number of enabled inputs and sort them by Z-order. */
	pipe->num_inputs = 0;

	for (i = 0; i < vsp1->info->rpf_count; ++i) {
		struct vsp1_rwpf *rpf = vsp1->rpf[i];
		unsigned int j;

		if (!vsp1->drm->inputs[i].enabled) {
			pipe->inputs[i] = NULL;
			continue;
		}

		pipe->inputs[i] = rpf;

		/* Insert the RPF in the sorted RPFs array. */
		for (j = pipe->num_inputs++; j > 0; --j) {
			if (rpf_zpos(vsp1, inputs[j-1]) <= rpf_zpos(vsp1, rpf))
				break;
			inputs[j] = inputs[j-1];
		}

		inputs[j] = rpf;
	}

	/* Setup the RPF input pipeline for every enabled input. */
	for (i = 0; i < vsp1->info->num_bru_inputs; ++i) {
		struct vsp1_rwpf *rpf = inputs[i];

		if (!rpf) {
			vsp1->bru->inputs[i].rpf = NULL;
			continue;
		}

		vsp1->bru->inputs[i].rpf = rpf;
		rpf->bru_input = i;
		rpf->entity.sink_pad = i;

		dev_dbg(vsp1->dev, "%s: connecting RPF.%u to BRU:%u\n",
			__func__, rpf->entity.index, i);

		ret = vsp1_du_setup_rpf_pipe(vsp1, rpf, i);
		if (ret < 0)
			dev_err(vsp1->dev,
				"%s: failed to setup RPF.%u\n",
				__func__, rpf->entity.index);
	}

	/* Configure all entities in the pipeline. */
	list_for_each_entry(entity, &pipe->entities, list_pipe) {
		/* Disconnect unused RPFs from the pipeline. */
		if (entity->type == VSP1_ENTITY_RPF) {
			struct vsp1_rwpf *rpf = to_rwpf(&entity->subdev);

			if (!pipe->inputs[rpf->entity.index]) {
				vsp1_dl_list_write(pipe->dl, entity->route->reg,
						   VI6_DPR_NODE_UNUSED);
				continue;
			}
		}

		vsp1_entity_route_setup(entity, pipe->dl);

		if (entity->ops->configure) {
			entity->ops->configure(entity, pipe, pipe->dl,
					       VSP1_ENTITY_PARAMS_INIT);
			entity->ops->configure(entity, pipe, pipe->dl,
					       VSP1_ENTITY_PARAMS_RUNTIME);
			entity->ops->configure(entity, pipe, pipe->dl,
					       VSP1_ENTITY_PARAMS_PARTITION);
		}
	}

	vsp1_dl_list_commit(pipe->dl);
	pipe->dl = NULL;

	/* Start or stop the pipeline if needed. */
	if (!vsp1->drm->num_inputs && pipe->num_inputs) {
		vsp1_write(vsp1, VI6_DISP_IRQ_STA, 0);
		vsp1_write(vsp1, VI6_DISP_IRQ_ENB, VI6_DISP_IRQ_ENB_DSTE);
		spin_lock_irqsave(&pipe->irqlock, flags);
		vsp1_pipeline_run(pipe);
		spin_unlock_irqrestore(&pipe->irqlock, flags);
	} else if (vsp1->drm->num_inputs && !pipe->num_inputs) {
		vsp1_write(vsp1, VI6_DISP_IRQ_ENB, 0);
		vsp1_pipeline_stop(pipe);
	}
}
EXPORT_SYMBOL_GPL(vsp1_du_atomic_flush);

/* -----------------------------------------------------------------------------
 * Initialization
 */

int vsp1_drm_create_links(struct vsp1_device *vsp1)
{
	const u32 flags = MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE;
	unsigned int i;
	int ret;

	/* VSPD instances require a BRU to perform composition and a LIF to
	 * output to the DU.
	 */
	if (!vsp1->bru || !vsp1->lif)
		return -ENXIO;

	for (i = 0; i < vsp1->info->rpf_count; ++i) {
		struct vsp1_rwpf *rpf = vsp1->rpf[i];

		ret = media_create_pad_link(&rpf->entity.subdev.entity,
					    RWPF_PAD_SOURCE,
					    &vsp1->bru->entity.subdev.entity,
					    i, flags);
		if (ret < 0)
			return ret;

		rpf->entity.sink = &vsp1->bru->entity.subdev.entity;
		rpf->entity.sink_pad = i;
	}

	ret = media_create_pad_link(&vsp1->bru->entity.subdev.entity,
				    vsp1->bru->entity.source_pad,
				    &vsp1->wpf[0]->entity.subdev.entity,
				    RWPF_PAD_SINK, flags);
	if (ret < 0)
		return ret;

	vsp1->bru->entity.sink = &vsp1->wpf[0]->entity.subdev.entity;
	vsp1->bru->entity.sink_pad = RWPF_PAD_SINK;

	ret = media_create_pad_link(&vsp1->wpf[0]->entity.subdev.entity,
				    RWPF_PAD_SOURCE,
				    &vsp1->lif->entity.subdev.entity,
				    LIF_PAD_SINK, flags);
	if (ret < 0)
		return ret;

	return 0;
}

int vsp1_drm_init(struct vsp1_device *vsp1)
{
	struct vsp1_pipeline *pipe;
	unsigned int i;

	vsp1->drm = devm_kzalloc(vsp1->dev, sizeof(*vsp1->drm), GFP_KERNEL);
	if (!vsp1->drm)
		return -ENOMEM;

	pipe = &vsp1->drm->pipe;

	vsp1_pipeline_init(pipe);

	/* The DRM pipeline is static, add entities manually. */
	for (i = 0; i < vsp1->info->rpf_count; ++i) {
		struct vsp1_rwpf *input = vsp1->rpf[i];

		list_add_tail(&input->entity.list_pipe, &pipe->entities);
	}

	list_add_tail(&vsp1->bru->entity.list_pipe, &pipe->entities);
	list_add_tail(&vsp1->wpf[0]->entity.list_pipe, &pipe->entities);
	list_add_tail(&vsp1->lif->entity.list_pipe, &pipe->entities);

	pipe->bru = &vsp1->bru->entity;
	pipe->lif = &vsp1->lif->entity;
	pipe->output = vsp1->wpf[0];

	return 0;
}

void vsp1_drm_cleanup(struct vsp1_device *vsp1)
{
}
