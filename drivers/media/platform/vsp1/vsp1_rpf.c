/*
 * vsp1_rpf.c  --  R-Car VSP1 Read Pixel Formatter
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

#include <linux/device.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"
#include "vsp1_video.h"

#define RPF_MAX_WIDTH				8190
#define RPF_MAX_HEIGHT				8190

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_rpf_write(struct vsp1_rwpf *rpf,
				  struct vsp1_dl_list *dl, u32 reg, u32 data)
{
	vsp1_dl_list_write(dl, reg + rpf->entity.index * VI6_RPF_OFFSET, data);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static const struct v4l2_subdev_ops rpf_ops = {
	.pad    = &vsp1_rwpf_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void rpf_configure(struct vsp1_entity *entity,
			  struct vsp1_pipeline *pipe,
			  struct vsp1_dl_list *dl,
			  enum vsp1_entity_params params)
{
	struct vsp1_rwpf *rpf = to_rwpf(&entity->subdev);
	const struct vsp1_format_info *fmtinfo = rpf->fmtinfo;
	const struct v4l2_pix_format_mplane *format = &rpf->format;
	const struct v4l2_mbus_framefmt *source_format;
	const struct v4l2_mbus_framefmt *sink_format;
	unsigned int left = 0;
	unsigned int top = 0;
	u32 pstride;
	u32 infmt;

	if (params == VSP1_ENTITY_PARAMS_RUNTIME) {
		vsp1_rpf_write(rpf, dl, VI6_RPF_VRTCOL_SET,
			       rpf->alpha << VI6_RPF_VRTCOL_SET_LAYA_SHIFT);
		vsp1_rpf_write(rpf, dl, VI6_RPF_MULT_ALPHA, rpf->mult_alpha |
			       (rpf->alpha << VI6_RPF_MULT_ALPHA_RATIO_SHIFT));

		vsp1_pipeline_propagate_alpha(pipe, dl, rpf->alpha);
		return;
	}

	if (params == VSP1_ENTITY_PARAMS_PARTITION) {
		unsigned int offsets[2];
		struct v4l2_rect crop;

		/*
		 * Source size and crop offsets.
		 *
		 * The crop offsets correspond to the location of the crop
		 * rectangle top left corner in the plane buffer. Only two
		 * offsets are needed, as planes 2 and 3 always have identical
		 * strides.
		 */
		crop = *vsp1_rwpf_get_crop(rpf, rpf->entity.config);

		/*
		 * Partition Algorithm Control
		 *
		 * The partition algorithm can split this frame into multiple
		 * slices. We must scale our partition window based on the pipe
		 * configuration to match the destination partition window.
		 * To achieve this, we adjust our crop to provide a 'sub-crop'
		 * matching the expected partition window. Only 'left' and
		 * 'width' need to be adjusted.
		 */
		if (pipe->partitions > 1) {
			const struct v4l2_mbus_framefmt *output;
			struct vsp1_entity *wpf = &pipe->output->entity;
			unsigned int input_width = crop.width;

			/*
			 * Scale the partition window based on the configuration
			 * of the pipeline.
			 */
			output = vsp1_entity_get_pad_format(wpf, wpf->config,
							    RWPF_PAD_SOURCE);

			crop.width = pipe->partition.width * input_width
				   / output->width;
			crop.left += pipe->partition.left * input_width
				   / output->width;
		}

		vsp1_rpf_write(rpf, dl, VI6_RPF_SRC_BSIZE,
			       (crop.width << VI6_RPF_SRC_BSIZE_BHSIZE_SHIFT) |
			       (crop.height << VI6_RPF_SRC_BSIZE_BVSIZE_SHIFT));
		vsp1_rpf_write(rpf, dl, VI6_RPF_SRC_ESIZE,
			       (crop.width << VI6_RPF_SRC_ESIZE_EHSIZE_SHIFT) |
			       (crop.height << VI6_RPF_SRC_ESIZE_EVSIZE_SHIFT));

		offsets[0] = crop.top * format->plane_fmt[0].bytesperline
			   + crop.left * fmtinfo->bpp[0] / 8;

		if (format->num_planes > 1)
			offsets[1] = crop.top * format->plane_fmt[1].bytesperline
				   + crop.left / fmtinfo->hsub
				   * fmtinfo->bpp[1] / 8;
		else
			offsets[1] = 0;

		vsp1_rpf_write(rpf, dl, VI6_RPF_SRCM_ADDR_Y,
			       rpf->mem.addr[0] + offsets[0]);
		vsp1_rpf_write(rpf, dl, VI6_RPF_SRCM_ADDR_C0,
			       rpf->mem.addr[1] + offsets[1]);
		vsp1_rpf_write(rpf, dl, VI6_RPF_SRCM_ADDR_C1,
			       rpf->mem.addr[2] + offsets[1]);
		return;
	}

	/* Stride */
	pstride = format->plane_fmt[0].bytesperline
		<< VI6_RPF_SRCM_PSTRIDE_Y_SHIFT;
	if (format->num_planes > 1)
		pstride |= format->plane_fmt[1].bytesperline
			<< VI6_RPF_SRCM_PSTRIDE_C_SHIFT;

	vsp1_rpf_write(rpf, dl, VI6_RPF_SRCM_PSTRIDE, pstride);

	/* Format */
	sink_format = vsp1_entity_get_pad_format(&rpf->entity,
						 rpf->entity.config,
						 RWPF_PAD_SINK);
	source_format = vsp1_entity_get_pad_format(&rpf->entity,
						   rpf->entity.config,
						   RWPF_PAD_SOURCE);

	infmt = VI6_RPF_INFMT_CIPM
	      | (fmtinfo->hwfmt << VI6_RPF_INFMT_RDFMT_SHIFT);

	if (fmtinfo->swap_yc)
		infmt |= VI6_RPF_INFMT_SPYCS;
	if (fmtinfo->swap_uv)
		infmt |= VI6_RPF_INFMT_SPUVS;

	if (sink_format->code != source_format->code)
		infmt |= VI6_RPF_INFMT_CSC;

	vsp1_rpf_write(rpf, dl, VI6_RPF_INFMT, infmt);
	vsp1_rpf_write(rpf, dl, VI6_RPF_DSWAP, fmtinfo->swap);

	/* Output location */
	if (pipe->bru) {
		const struct v4l2_rect *compose;

		compose = vsp1_entity_get_pad_selection(pipe->bru,
							pipe->bru->config,
							rpf->bru_input,
							V4L2_SEL_TGT_COMPOSE);
		left = compose->left;
		top = compose->top;
	}

	vsp1_rpf_write(rpf, dl, VI6_RPF_LOC,
		       (left << VI6_RPF_LOC_HCOORD_SHIFT) |
		       (top << VI6_RPF_LOC_VCOORD_SHIFT));

	/* On Gen2 use the alpha channel (extended to 8 bits) when available or
	 * a fixed alpha value set through the V4L2_CID_ALPHA_COMPONENT control
	 * otherwise.
	 *
	 * The Gen3 RPF has extended alpha capability and can both multiply the
	 * alpha channel by a fixed global alpha value, and multiply the pixel
	 * components to convert the input to premultiplied alpha.
	 *
	 * As alpha premultiplication is available in the BRU for both Gen2 and
	 * Gen3 we handle it there and use the Gen3 alpha multiplier for global
	 * alpha multiplication only. This however prevents conversion to
	 * premultiplied alpha if no BRU is present in the pipeline. If that use
	 * case turns out to be useful we will revisit the implementation (for
	 * Gen3 only).
	 *
	 * We enable alpha multiplication on Gen3 using the fixed alpha value
	 * set through the V4L2_CID_ALPHA_COMPONENT control when the input
	 * contains an alpha channel. On Gen2 the global alpha is ignored in
	 * that case.
	 *
	 * In all cases, disable color keying.
	 */
	vsp1_rpf_write(rpf, dl, VI6_RPF_ALPH_SEL, VI6_RPF_ALPH_SEL_AEXT_EXT |
		       (fmtinfo->alpha ? VI6_RPF_ALPH_SEL_ASEL_PACKED
				       : VI6_RPF_ALPH_SEL_ASEL_FIXED));

	if (entity->vsp1->info->gen == 3) {
		u32 mult;

		if (fmtinfo->alpha) {
			/* When the input contains an alpha channel enable the
			 * alpha multiplier. If the input is premultiplied we
			 * need to multiply both the alpha channel and the pixel
			 * components by the global alpha value to keep them
			 * premultiplied. Otherwise multiply the alpha channel
			 * only.
			 */
			bool premultiplied = format->flags
					   & V4L2_PIX_FMT_FLAG_PREMUL_ALPHA;

			mult = VI6_RPF_MULT_ALPHA_A_MMD_RATIO
			     | (premultiplied ?
				VI6_RPF_MULT_ALPHA_P_MMD_RATIO :
				VI6_RPF_MULT_ALPHA_P_MMD_NONE);
		} else {
			/* When the input doesn't contain an alpha channel the
			 * global alpha value is applied in the unpacking unit,
			 * the alpha multiplier isn't needed and must be
			 * disabled.
			 */
			mult = VI6_RPF_MULT_ALPHA_A_MMD_NONE
			     | VI6_RPF_MULT_ALPHA_P_MMD_NONE;
		}

		rpf->mult_alpha = mult;
	}

	vsp1_rpf_write(rpf, dl, VI6_RPF_MSK_CTRL, 0);
	vsp1_rpf_write(rpf, dl, VI6_RPF_CKEY_CTRL, 0);

}

static const struct vsp1_entity_operations rpf_entity_ops = {
	.configure = rpf_configure,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_rwpf *vsp1_rpf_create(struct vsp1_device *vsp1, unsigned int index)
{
	struct vsp1_rwpf *rpf;
	char name[6];
	int ret;

	rpf = devm_kzalloc(vsp1->dev, sizeof(*rpf), GFP_KERNEL);
	if (rpf == NULL)
		return ERR_PTR(-ENOMEM);

	rpf->max_width = RPF_MAX_WIDTH;
	rpf->max_height = RPF_MAX_HEIGHT;

	rpf->entity.ops = &rpf_entity_ops;
	rpf->entity.type = VSP1_ENTITY_RPF;
	rpf->entity.index = index;

	sprintf(name, "rpf.%u", index);
	ret = vsp1_entity_init(vsp1, &rpf->entity, name, 2, &rpf_ops,
			       MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the control handler. */
	ret = vsp1_rwpf_init_ctrls(rpf, 0);
	if (ret < 0) {
		dev_err(vsp1->dev, "rpf%u: failed to initialize controls\n",
			index);
		goto error;
	}

	v4l2_ctrl_handler_setup(&rpf->ctrls);

	return rpf;

error:
	vsp1_entity_destroy(&rpf->entity);
	return ERR_PTR(ret);
}
