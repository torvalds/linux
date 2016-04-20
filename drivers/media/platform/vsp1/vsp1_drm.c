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
#include <linux/vsp1.h>

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_bru.h"
#include "vsp1_dl.h"
#include "vsp1_drm.h"
#include "vsp1_lif.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"

/* -----------------------------------------------------------------------------
 * Runtime Handling
 */

static void vsp1_drm_pipeline_frame_end(struct vsp1_pipeline *pipe)
{
	unsigned long flags;

	spin_lock_irqsave(&pipe->irqlock, flags);
	if (pipe->num_inputs)
		vsp1_pipeline_run(pipe);
	spin_unlock_irqrestore(&pipe->irqlock, flags);
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
			bru->inputs[i].rpf = NULL;
			pipe->inputs[i] = NULL;
		}

		pipe->num_inputs = 0;

		vsp1_device_put(vsp1);

		dev_dbg(vsp1->dev, "%s: pipeline disabled\n", __func__);

		return 0;
	}

	vsp1_dl_reset(vsp1->drm->dl);

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
	unsigned long flags;

	spin_lock_irqsave(&pipe->irqlock, flags);

	vsp1->drm->num_inputs = pipe->num_inputs;

	spin_unlock_irqrestore(&pipe->irqlock, flags);

	/* Prepare the display list. */
	vsp1_dl_begin(vsp1->drm->dl);
}
EXPORT_SYMBOL_GPL(vsp1_du_atomic_begin);

/**
 * vsp1_du_atomic_update - Setup one RPF input of the VSP pipeline
 * @dev: the VSP device
 * @rpf_index: index of the RPF to setup (0-based)
 * @pixelformat: V4L2 pixel format for the RPF memory input
 * @pitch: number of bytes per line in the image stored in memory
 * @mem: DMA addresses of the memory buffers (one per plane)
 * @src: the source crop rectangle for the RPF
 * @dst: the destination compose rectangle for the BRU input
 *
 * Configure the VSP to perform composition of the image referenced by @mem
 * through RPF @rpf_index, using the @src crop rectangle and the @dst
 * composition rectangle. The Z-order is fixed with RPF 0 at the bottom.
 *
 * Image format as stored in memory is expressed as a V4L2 @pixelformat value.
 * As a special case, setting the pixel format to 0 will disable the RPF. The
 * @pitch, @mem, @src and @dst parameters are ignored in that case. Calling the
 * function on a disabled RPF is allowed.
 *
 * The memory pitch is configurable to allow for padding at end of lines, or
 * simple for images that extend beyond the crop rectangle boundaries. The
 * @pitch value is expressed in bytes and applies to all planes for multiplanar
 * formats.
 *
 * The source memory buffer is referenced by the DMA address of its planes in
 * the @mem array. Up to two planes are supported. The second plane DMA address
 * is ignored for formats using a single plane.
 *
 * This function isn't reentrant, the caller needs to serialize calls.
 *
 * TODO: Implement Z-order control by decoupling the RPF index from the BRU
 * input index.
 *
 * Return 0 on success or a negative error code on failure.
 */
int vsp1_du_atomic_update(struct device *dev, unsigned int rpf_index,
			  u32 pixelformat, unsigned int pitch,
			  dma_addr_t mem[2], const struct v4l2_rect *src,
			  const struct v4l2_rect *dst)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe;
	const struct vsp1_format_info *fmtinfo;
	struct v4l2_subdev_selection sel;
	struct v4l2_subdev_format format;
	struct vsp1_rwpf_memory memory;
	struct vsp1_rwpf *rpf;
	unsigned long flags;
	int ret;

	if (rpf_index >= vsp1->info->rpf_count)
		return -EINVAL;

	rpf = vsp1->rpf[rpf_index];

	if (pixelformat == 0) {
		dev_dbg(vsp1->dev, "%s: RPF%u: disable requested\n", __func__,
			rpf_index);

		spin_lock_irqsave(&pipe->irqlock, flags);

		if (pipe->inputs[rpf_index]) {
			/* Remove the RPF from the pipeline if it was previously
			 * enabled.
			 */
			vsp1->bru->inputs[rpf_index].rpf = NULL;
			pipe->inputs[rpf_index] = NULL;

			pipe->num_inputs--;
		}

		spin_unlock_irqrestore(&pipe->irqlock, flags);

		return 0;
	}

	dev_dbg(vsp1->dev,
		"%s: RPF%u: (%u,%u)/%ux%u -> (%u,%u)/%ux%u (%08x), pitch %u dma { %pad, %pad }\n",
		__func__, rpf_index,
		src->left, src->top, src->width, src->height,
		dst->left, dst->top, dst->width, dst->height,
		pixelformat, pitch, &mem[0], &mem[1]);

	/* Set the stride at the RPF input. */
	fmtinfo = vsp1_get_format_info(pixelformat);
	if (!fmtinfo) {
		dev_dbg(vsp1->dev, "Unsupport pixel format %08x for RPF\n",
			pixelformat);
		return -EINVAL;
	}

	rpf->fmtinfo = fmtinfo;
	rpf->format.num_planes = fmtinfo->planes;
	rpf->format.plane_fmt[0].bytesperline = pitch;
	rpf->format.plane_fmt[1].bytesperline = pitch;

	/* Configure the format on the RPF sink pad and propagate it up to the
	 * BRU sink pad.
	 */
	memset(&format, 0, sizeof(format));
	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	format.pad = RWPF_PAD_SINK;
	format.format.width = src->width + src->left;
	format.format.height = src->height + src->top;
	format.format.code = fmtinfo->mbus;
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
	sel.r = *src;

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
	format.pad = rpf->entity.index;

	ret = v4l2_subdev_call(&vsp1->bru->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on BRU pad %u\n",
		__func__, format.format.width, format.format.height,
		format.format.code, format.pad);

	sel.pad = rpf->entity.index;
	sel.target = V4L2_SEL_TGT_COMPOSE;
	sel.r = *dst;

	ret = v4l2_subdev_call(&vsp1->bru->entity.subdev, pad, set_selection,
			       NULL, &sel);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: set selection (%u,%u)/%ux%u on BRU pad %u\n",
		__func__, sel.r.left, sel.r.top, sel.r.width, sel.r.height,
		sel.pad);

	/* Store the compose rectangle coordinates in the RPF. */
	rpf->location.left = dst->left;
	rpf->location.top = dst->top;

	/* Set the memory buffer address. */
	memory.num_planes = fmtinfo->planes;
	memory.addr[0] = mem[0];
	memory.addr[1] = mem[1];

	rpf->ops->set_memory(rpf, &memory);

	spin_lock_irqsave(&pipe->irqlock, flags);

	/* If the RPF was previously stopped set the BRU input to the RPF and
	 * store the RPF in the pipeline inputs array.
	 */
	if (!pipe->inputs[rpf->entity.index]) {
		vsp1->bru->inputs[rpf_index].rpf = rpf;
		pipe->inputs[rpf->entity.index] = rpf;
		pipe->num_inputs++;
	}

	spin_unlock_irqrestore(&pipe->irqlock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_atomic_update);

/**
 * vsp1_du_atomic_flush - Commit an atomic update
 * @dev: the VSP device
 */
void vsp1_du_atomic_flush(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe;
	struct vsp1_entity *entity;
	unsigned long flags;
	bool stop = false;
	int ret;

	list_for_each_entry(entity, &pipe->entities, list_pipe) {
		/* Disconnect unused RPFs from the pipeline. */
		if (entity->type == VSP1_ENTITY_RPF) {
			struct vsp1_rwpf *rpf = to_rwpf(&entity->subdev);

			if (!pipe->inputs[rpf->entity.index]) {
				vsp1_mod_write(entity, entity->route->reg,
					   VI6_DPR_NODE_UNUSED);
				continue;
			}
		}

		vsp1_entity_route_setup(entity);

		ret = v4l2_subdev_call(&entity->subdev, video,
				       s_stream, 1);
		if (ret < 0) {
			dev_err(vsp1->dev,
				"DRM pipeline start failure on entity %s\n",
				entity->subdev.name);
			return;
		}
	}

	vsp1_dl_commit(vsp1->drm->dl);

	spin_lock_irqsave(&pipe->irqlock, flags);

	/* Start or stop the pipeline if needed. */
	if (!vsp1->drm->num_inputs && pipe->num_inputs) {
		vsp1_write(vsp1, VI6_DISP_IRQ_STA, 0);
		vsp1_write(vsp1, VI6_DISP_IRQ_ENB, VI6_DISP_IRQ_ENB_DSTE);
		vsp1_pipeline_run(pipe);
	} else if (vsp1->drm->num_inputs && !pipe->num_inputs) {
		stop = true;
	}

	spin_unlock_irqrestore(&pipe->irqlock, flags);

	if (stop) {
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

	vsp1->drm->dl = vsp1_dl_create(vsp1);
	if (!vsp1->drm->dl)
		return -ENOMEM;

	pipe = &vsp1->drm->pipe;

	vsp1_pipeline_init(pipe);
	pipe->frame_end = vsp1_drm_pipeline_frame_end;

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

	pipe->dl = vsp1->drm->dl;

	return 0;
}

void vsp1_drm_cleanup(struct vsp1_device *vsp1)
{
	vsp1_dl_destroy(vsp1->drm->dl);
}
