// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_drm.c  --  R-Car VSP1 DRM/KMS Interface
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
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
#include "vsp1_uif.h"

#define BRX_NAME(e)	(e)->type == VSP1_ENTITY_BRU ? "BRU" : "BRS"

/* -----------------------------------------------------------------------------
 * Interrupt Handling
 */

static void vsp1_du_pipeline_frame_end(struct vsp1_pipeline *pipe,
				       unsigned int completion)
{
	struct vsp1_drm_pipeline *drm_pipe = to_vsp1_drm_pipeline(pipe);

	if (drm_pipe->du_complete) {
		struct vsp1_entity *uif = drm_pipe->uif;
		unsigned int status = completion
				    & (VSP1_DU_STATUS_COMPLETE |
				       VSP1_DU_STATUS_WRITEBACK);
		u32 crc;

		crc = uif ? vsp1_uif_get_crc(to_uif(&uif->subdev)) : 0;
		drm_pipe->du_complete(drm_pipe->du_private, status, crc);
	}

	if (completion & VSP1_DL_FRAME_END_INTERNAL) {
		drm_pipe->force_brx_release = false;
		wake_up(&drm_pipe->wait_queue);
	}
}

/* -----------------------------------------------------------------------------
 * Pipeline Configuration
 */

/*
 * Insert the UIF in the pipeline between the prev and next entities. If no UIF
 * is available connect the two entities directly.
 */
static int vsp1_du_insert_uif(struct vsp1_device *vsp1,
			      struct vsp1_pipeline *pipe,
			      struct vsp1_entity *uif,
			      struct vsp1_entity *prev, unsigned int prev_pad,
			      struct vsp1_entity *next, unsigned int next_pad)
{
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	if (!uif) {
		/*
		 * If there's no UIF to be inserted, connect the previous and
		 * next entities directly.
		 */
		prev->sink = next;
		prev->sink_pad = next_pad;
		return 0;
	}

	prev->sink = uif;
	prev->sink_pad = UIF_PAD_SINK;

	format.pad = prev_pad;

	ret = v4l2_subdev_call(&prev->subdev, pad, get_fmt, NULL, &format);
	if (ret < 0)
		return ret;

	format.pad = UIF_PAD_SINK;

	ret = v4l2_subdev_call(&uif->subdev, pad, set_fmt, NULL, &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on UIF sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code);

	/*
	 * The UIF doesn't mangle the format between its sink and source pads,
	 * so there is no need to retrieve the format on its source pad.
	 */

	uif->sink = next;
	uif->sink_pad = next_pad;

	return 0;
}

/* Setup one RPF and the connected BRx sink pad. */
static int vsp1_du_pipeline_setup_rpf(struct vsp1_device *vsp1,
				      struct vsp1_pipeline *pipe,
				      struct vsp1_rwpf *rpf,
				      struct vsp1_entity *uif,
				      unsigned int brx_input)
{
	const struct vsp1_drm_input *input = &vsp1->drm->inputs[rpf->entity.index];
	struct v4l2_subdev_selection sel = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	/*
	 * Configure the format on the RPF sink pad and propagate it up to the
	 * BRx sink pad.
	 */
	format.pad = RWPF_PAD_SINK;
	format.format.width = input->crop.width + input->crop.left;
	format.format.height = input->crop.height + input->crop.top;
	format.format.code = rpf->fmtinfo->mbus;
	format.format.field = V4L2_FIELD_NONE;
	format.format.ycbcr_enc = input->ycbcr_enc;
	format.format.quantization = input->quantization;

	ret = v4l2_subdev_call(&rpf->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: set format %ux%u (%x) on RPF%u sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code, rpf->entity.index);

	sel.pad = RWPF_PAD_SINK;
	sel.target = V4L2_SEL_TGT_CROP;
	sel.r = input->crop;

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

	/* Insert and configure the UIF if available. */
	ret = vsp1_du_insert_uif(vsp1, pipe, uif, &rpf->entity, RWPF_PAD_SOURCE,
				 pipe->brx, brx_input);
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
	else if (vsp1_feature(vsp1, VSP1_HAS_BRU) && !vsp1->bru->entity.pipe)
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

		/*
		 * Add the BRx to the pipeline, inserting it just before the
		 * WPF.
		 */
		dev_dbg(vsp1->dev, "%s: pipe %u: acquired %s\n",
			__func__, pipe->lif->index, BRX_NAME(brx));

		pipe->brx = brx;
		pipe->brx->pipe = pipe;
		pipe->brx->sink = &pipe->output->entity;
		pipe->brx->sink_pad = 0;

		list_add_tail(&pipe->brx->list_pipe,
			      &pipe->output->entity.list_pipe);
	}

	/*
	 * Configure the format on the BRx source and verify that it matches the
	 * requested format. We don't set the media bus code as it is configured
	 * on the BRx sink pad 0 and propagated inside the entity, not on the
	 * source pad.
	 */
	format.pad = brx->source_pad;
	format.format.width = drm_pipe->width;
	format.format.height = drm_pipe->height;
	format.format.field = V4L2_FIELD_NONE;

	ret = v4l2_subdev_call(&brx->subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on %s pad %u\n",
		__func__, format.format.width, format.format.height,
		format.format.code, BRX_NAME(brx), brx->source_pad);

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
	struct vsp1_drm_pipeline *drm_pipe = to_vsp1_drm_pipeline(pipe);
	struct vsp1_rwpf *inputs[VSP1_MAX_RPF] = { NULL, };
	struct vsp1_entity *uif;
	bool use_uif = false;
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
			list_add(&rpf->entity.list_pipe, &pipe->entities);
		}

		brx->inputs[i].rpf = rpf;
		rpf->brx_input = i;
		rpf->entity.sink = pipe->brx;
		rpf->entity.sink_pad = i;

		dev_dbg(vsp1->dev, "%s: connecting RPF.%u to %s:%u\n",
			__func__, rpf->entity.index, BRX_NAME(pipe->brx), i);

		uif = drm_pipe->crc.source == VSP1_DU_CRC_PLANE &&
		      drm_pipe->crc.index == i ? drm_pipe->uif : NULL;
		if (uif)
			use_uif = true;
		ret = vsp1_du_pipeline_setup_rpf(vsp1, pipe, rpf, uif, i);
		if (ret < 0) {
			dev_err(vsp1->dev,
				"%s: failed to setup RPF.%u\n",
				__func__, rpf->entity.index);
			return ret;
		}
	}

	/* Insert and configure the UIF at the BRx output if available. */
	uif = drm_pipe->crc.source == VSP1_DU_CRC_OUTPUT ? drm_pipe->uif : NULL;
	if (uif)
		use_uif = true;
	ret = vsp1_du_insert_uif(vsp1, pipe, uif,
				 pipe->brx, pipe->brx->source_pad,
				 &pipe->output->entity, 0);
	if (ret < 0)
		dev_err(vsp1->dev, "%s: failed to setup UIF after %s\n",
			__func__, BRX_NAME(pipe->brx));

	/* If the DRM pipe does not have a UIF there is nothing we can update. */
	if (!drm_pipe->uif)
		return 0;

	/*
	 * If the UIF is not in use schedule it for removal by setting its pipe
	 * pointer to NULL, vsp1_du_pipeline_configure() will remove it from the
	 * hardware pipeline and from the pipeline's list of entities. Otherwise
	 * make sure it is present in the pipeline's list of entities if it
	 * wasn't already.
	 */
	if (!use_uif) {
		drm_pipe->uif->pipe = NULL;
	} else if (!drm_pipe->uif->pipe) {
		drm_pipe->uif->pipe = pipe;
		list_add_tail(&drm_pipe->uif->list_pipe, &pipe->entities);
	}

	return 0;
}

/* Setup the output side of the pipeline (WPF and LIF). */
static int vsp1_du_pipeline_setup_output(struct vsp1_device *vsp1,
					 struct vsp1_pipeline *pipe)
{
	struct vsp1_drm_pipeline *drm_pipe = to_vsp1_drm_pipeline(pipe);
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

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
	struct vsp1_dl_body *dlb;
	unsigned int dl_flags = 0;

	vsp1_pipeline_calculate_partition(pipe, &pipe->part_table[0],
					  drm_pipe->width, 0);

	if (drm_pipe->force_brx_release)
		dl_flags |= VSP1_DL_FRAME_END_INTERNAL;
	if (pipe->output->writeback)
		dl_flags |= VSP1_DL_FRAME_END_WRITEBACK;

	dl = vsp1_dl_list_get(pipe->output->dlm);
	dlb = vsp1_dl_list_get_body0(dl);

	list_for_each_entry_safe(entity, next, &pipe->entities, list_pipe) {
		/* Disconnect unused entities from the pipeline. */
		if (!entity->pipe) {
			vsp1_dl_body_write(dlb, entity->route->reg,
					   VI6_DPR_NODE_UNUSED);

			entity->sink = NULL;
			list_del(&entity->list_pipe);

			continue;
		}

		vsp1_entity_route_setup(entity, pipe, dlb);
		vsp1_entity_configure_stream(entity, entity->state, pipe,
					     dl, dlb);
		vsp1_entity_configure_frame(entity, pipe, dl, dlb);
		vsp1_entity_configure_partition(entity, pipe,
						&pipe->part_table[0], dl, dlb);
	}

	vsp1_dl_list_commit(dl, dl_flags);
}

static int vsp1_du_pipeline_set_rwpf_format(struct vsp1_device *vsp1,
					    struct vsp1_rwpf *rwpf,
					    u32 pixelformat, unsigned int pitch)
{
	const struct vsp1_format_info *fmtinfo;
	unsigned int chroma_hsub;

	fmtinfo = vsp1_get_format_info(vsp1, pixelformat);
	if (!fmtinfo) {
		dev_dbg(vsp1->dev, "Unsupported pixel format %p4cc\n",
			&pixelformat);
		return -EINVAL;
	}

	/*
	 * Only formats with three planes can affect the chroma planes pitch.
	 * All formats with two planes have a horizontal subsampling value of 2,
	 * but combine U and V in a single chroma plane, which thus results in
	 * the luma plane and chroma plane having the same pitch.
	 */
	chroma_hsub = (fmtinfo->planes == 3) ? fmtinfo->hsub : 1;

	rwpf->fmtinfo = fmtinfo;
	rwpf->format.num_planes = fmtinfo->planes;
	rwpf->format.plane_fmt[0].bytesperline = pitch;
	rwpf->format.plane_fmt[1].bytesperline = pitch / chroma_hsub;

	return 0;
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

	/* Reset the underrun counter */
	pipe->underrun_count = 0;

	drm_pipe->width = cfg->width;
	drm_pipe->height = cfg->height;
	pipe->interlaced = cfg->interlaced;

	dev_dbg(vsp1->dev, "%s: configuring LIF%u with format %ux%u%s\n",
		__func__, pipe_index, cfg->width, cfg->height,
		pipe->interlaced ? "i" : "");

	mutex_lock(&vsp1->drm->lock);

	/* Setup formats through the pipeline. */
	ret = vsp1_du_pipeline_setup_inputs(vsp1, pipe);
	if (ret < 0)
		goto unlock;

	ret = vsp1_du_pipeline_setup_output(vsp1, pipe);
	if (ret < 0)
		goto unlock;

	vsp1_pipeline_dump(pipe, "LIF setup");

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
	vsp1_write(vsp1, VI6_DISP_IRQ_STA(pipe_index), 0);
	vsp1_write(vsp1, VI6_DISP_IRQ_ENB(pipe_index), 0);

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
	struct vsp1_drm_input *input;
	struct vsp1_rwpf *rpf;
	int ret;

	if (rpf_index >= vsp1->info->rpf_count)
		return -EINVAL;

	input = &vsp1->drm->inputs[rpf_index];
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
		"%s: RPF%u: (%u,%u)/%ux%u -> (%u,%u)/%ux%u (%p4cc), pitch %u dma { %pad, %pad, %pad } zpos %u\n",
		__func__, rpf_index,
		cfg->src.left, cfg->src.top, cfg->src.width, cfg->src.height,
		cfg->dst.left, cfg->dst.top, cfg->dst.width, cfg->dst.height,
		&cfg->pixelformat, cfg->pitch, &cfg->mem[0], &cfg->mem[1],
		&cfg->mem[2], cfg->zpos);

	/*
	 * Store the format, stride, memory buffer address, crop and compose
	 * rectangles and Z-order position and for the input.
	 */
	ret = vsp1_du_pipeline_set_rwpf_format(vsp1, rpf, cfg->pixelformat,
					       cfg->pitch);
	if (ret < 0)
		return ret;

	rpf->alpha = cfg->alpha;

	rpf->mem.addr[0] = cfg->mem[0];
	rpf->mem.addr[1] = cfg->mem[1];
	rpf->mem.addr[2] = cfg->mem[2];

	rpf->format.flags = cfg->premult ? V4L2_PIX_FMT_FLAG_PREMUL_ALPHA : 0;

	input->crop = cfg->src;
	input->compose = cfg->dst;
	input->zpos = cfg->zpos;
	input->ycbcr_enc = cfg->color_encoding;
	input->quantization = cfg->color_range;

	drm_pipe->pipe.inputs[rpf_index] = rpf;

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_atomic_update);

/**
 * vsp1_du_atomic_flush - Commit an atomic update
 * @dev: the VSP device
 * @pipe_index: the DRM pipeline index
 * @cfg: atomic pipe configuration
 */
void vsp1_du_atomic_flush(struct device *dev, unsigned int pipe_index,
			  const struct vsp1_du_atomic_pipe_config *cfg)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_drm_pipeline *drm_pipe = &vsp1->drm->pipe[pipe_index];
	struct vsp1_pipeline *pipe = &drm_pipe->pipe;
	int ret;

	drm_pipe->crc = cfg->crc;

	mutex_lock(&vsp1->drm->lock);

	if (cfg->writeback.pixelformat) {
		const struct vsp1_du_writeback_config *wb_cfg = &cfg->writeback;

		ret = vsp1_du_pipeline_set_rwpf_format(vsp1, pipe->output,
						       wb_cfg->pixelformat,
						       wb_cfg->pitch);
		if (WARN_ON(ret < 0))
			goto done;

		pipe->output->mem.addr[0] = wb_cfg->mem[0];
		pipe->output->mem.addr[1] = wb_cfg->mem[1];
		pipe->output->mem.addr[2] = wb_cfg->mem[2];
		pipe->output->writeback = true;
	}

	vsp1_du_pipeline_setup_inputs(vsp1, pipe);

	vsp1_pipeline_dump(pipe, "atomic update");

	vsp1_du_pipeline_configure(pipe);

done:
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
	return dma_map_sgtable(vsp1->bus_master, sgt, DMA_TO_DEVICE,
			       DMA_ATTR_SKIP_CPU_SYNC);
}
EXPORT_SYMBOL_GPL(vsp1_du_map_sg);

void vsp1_du_unmap_sg(struct device *dev, struct sg_table *sgt)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	dma_unmap_sgtable(vsp1->bus_master, sgt, DMA_TO_DEVICE,
			  DMA_ATTR_SKIP_CPU_SYNC);
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

		pipe->partitions = 1;
		pipe->part_table = &drm_pipe->partition;

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

		/*
		 * CRC computation is initially disabled, don't add the UIF to
		 * the pipeline.
		 */
		if (i < vsp1->info->uif_count)
			drm_pipe->uif = &vsp1->uif[i]->entity;
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
