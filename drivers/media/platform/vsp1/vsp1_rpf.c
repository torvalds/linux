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
#include "vsp1_rwpf.h"
#include "vsp1_video.h"

#define RPF_MAX_WIDTH				8190
#define RPF_MAX_HEIGHT				8190

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_rpf_write(struct vsp1_rwpf *rpf, u32 reg, u32 data)
{
	vsp1_mod_write(&rpf->entity, reg + rpf->entity.index * VI6_RPF_OFFSET,
		       data);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Core Operations
 */

static int rpf_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct vsp1_pipeline *pipe = to_vsp1_pipeline(&subdev->entity);
	struct vsp1_rwpf *rpf = to_rwpf(subdev);
	const struct vsp1_format_info *fmtinfo = rpf->fmtinfo;
	const struct v4l2_pix_format_mplane *format = &rpf->format;
	const struct v4l2_rect *crop = &rpf->crop;
	u32 pstride;
	u32 infmt;

	if (!enable)
		return 0;

	/* Source size, stride and crop offsets.
	 *
	 * The crop offsets correspond to the location of the crop rectangle top
	 * left corner in the plane buffer. Only two offsets are needed, as
	 * planes 2 and 3 always have identical strides.
	 */
	vsp1_rpf_write(rpf, VI6_RPF_SRC_BSIZE,
		       (crop->width << VI6_RPF_SRC_BSIZE_BHSIZE_SHIFT) |
		       (crop->height << VI6_RPF_SRC_BSIZE_BVSIZE_SHIFT));
	vsp1_rpf_write(rpf, VI6_RPF_SRC_ESIZE,
		       (crop->width << VI6_RPF_SRC_ESIZE_EHSIZE_SHIFT) |
		       (crop->height << VI6_RPF_SRC_ESIZE_EVSIZE_SHIFT));

	rpf->offsets[0] = crop->top * format->plane_fmt[0].bytesperline
			+ crop->left * fmtinfo->bpp[0] / 8;
	pstride = format->plane_fmt[0].bytesperline
		<< VI6_RPF_SRCM_PSTRIDE_Y_SHIFT;

	if (format->num_planes > 1) {
		rpf->offsets[1] = crop->top * format->plane_fmt[1].bytesperline
				+ crop->left * fmtinfo->bpp[1] / 8;
		pstride |= format->plane_fmt[1].bytesperline
			<< VI6_RPF_SRCM_PSTRIDE_C_SHIFT;
	} else {
		rpf->offsets[1] = 0;
	}

	vsp1_rpf_write(rpf, VI6_RPF_SRCM_PSTRIDE, pstride);

	/* Format */
	infmt = VI6_RPF_INFMT_CIPM
	      | (fmtinfo->hwfmt << VI6_RPF_INFMT_RDFMT_SHIFT);

	if (fmtinfo->swap_yc)
		infmt |= VI6_RPF_INFMT_SPYCS;
	if (fmtinfo->swap_uv)
		infmt |= VI6_RPF_INFMT_SPUVS;

	if (rpf->entity.formats[RWPF_PAD_SINK].code !=
	    rpf->entity.formats[RWPF_PAD_SOURCE].code)
		infmt |= VI6_RPF_INFMT_CSC;

	vsp1_rpf_write(rpf, VI6_RPF_INFMT, infmt);
	vsp1_rpf_write(rpf, VI6_RPF_DSWAP, fmtinfo->swap);

	/* Output location */
	vsp1_rpf_write(rpf, VI6_RPF_LOC,
		       (rpf->location.left << VI6_RPF_LOC_HCOORD_SHIFT) |
		       (rpf->location.top << VI6_RPF_LOC_VCOORD_SHIFT));

	/* Use the alpha channel (extended to 8 bits) when available or an
	 * alpha value set through the V4L2_CID_ALPHA_COMPONENT control
	 * otherwise. Disable color keying.
	 */
	vsp1_rpf_write(rpf, VI6_RPF_ALPH_SEL, VI6_RPF_ALPH_SEL_AEXT_EXT |
		       (fmtinfo->alpha ? VI6_RPF_ALPH_SEL_ASEL_PACKED
				       : VI6_RPF_ALPH_SEL_ASEL_FIXED));

	vsp1_rpf_write(rpf, VI6_RPF_VRTCOL_SET,
		       rpf->alpha << VI6_RPF_VRTCOL_SET_LAYA_SHIFT);

	vsp1_pipeline_propagate_alpha(pipe, &rpf->entity, rpf->alpha);

	vsp1_rpf_write(rpf, VI6_RPF_MSK_CTRL, 0);
	vsp1_rpf_write(rpf, VI6_RPF_CKEY_CTRL, 0);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static struct v4l2_subdev_video_ops rpf_video_ops = {
	.s_stream = rpf_s_stream,
};

static struct v4l2_subdev_pad_ops rpf_pad_ops = {
	.enum_mbus_code = vsp1_rwpf_enum_mbus_code,
	.enum_frame_size = vsp1_rwpf_enum_frame_size,
	.get_fmt = vsp1_rwpf_get_format,
	.set_fmt = vsp1_rwpf_set_format,
	.get_selection = vsp1_rwpf_get_selection,
	.set_selection = vsp1_rwpf_set_selection,
};

static struct v4l2_subdev_ops rpf_ops = {
	.video	= &rpf_video_ops,
	.pad    = &rpf_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Video Device Operations
 */

static void rpf_set_memory(struct vsp1_rwpf *rpf)
{
	vsp1_rpf_write(rpf, VI6_RPF_SRCM_ADDR_Y,
		       rpf->mem.addr[0] + rpf->offsets[0]);
	vsp1_rpf_write(rpf, VI6_RPF_SRCM_ADDR_C0,
		       rpf->mem.addr[1] + rpf->offsets[1]);
	vsp1_rpf_write(rpf, VI6_RPF_SRCM_ADDR_C1,
		       rpf->mem.addr[2] + rpf->offsets[1]);
}

static const struct vsp1_rwpf_operations rpf_vdev_ops = {
	.set_memory = rpf_set_memory,
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

	rpf->ops = &rpf_vdev_ops;

	rpf->max_width = RPF_MAX_WIDTH;
	rpf->max_height = RPF_MAX_HEIGHT;

	rpf->entity.type = VSP1_ENTITY_RPF;
	rpf->entity.index = index;

	sprintf(name, "rpf.%u", index);
	ret = vsp1_entity_init(vsp1, &rpf->entity, name, 2, &rpf_ops);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the control handler. */
	ret = vsp1_rwpf_init_ctrls(rpf);
	if (ret < 0) {
		dev_err(vsp1->dev, "rpf%u: failed to initialize controls\n",
			index);
		goto error;
	}

	return rpf;

error:
	vsp1_entity_destroy(&rpf->entity);
	return ERR_PTR(ret);
}
