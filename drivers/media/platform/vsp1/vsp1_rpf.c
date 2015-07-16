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

static inline u32 vsp1_rpf_read(struct vsp1_rwpf *rpf, u32 reg)
{
	return vsp1_read(rpf->entity.vsp1,
			 reg + rpf->entity.index * VI6_RPF_OFFSET);
}

static inline void vsp1_rpf_write(struct vsp1_rwpf *rpf, u32 reg, u32 data)
{
	vsp1_write(rpf->entity.vsp1,
		   reg + rpf->entity.index * VI6_RPF_OFFSET, data);
}

/* -----------------------------------------------------------------------------
 * Controls
 */

static int rpf_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vsp1_rwpf *rpf =
		container_of(ctrl->handler, struct vsp1_rwpf, ctrls);
	struct vsp1_pipeline *pipe;

	if (!vsp1_entity_is_streaming(&rpf->entity))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ALPHA_COMPONENT:
		vsp1_rpf_write(rpf, VI6_RPF_VRTCOL_SET,
			       ctrl->val << VI6_RPF_VRTCOL_SET_LAYA_SHIFT);

		pipe = to_vsp1_pipeline(&rpf->entity.subdev.entity);
		vsp1_pipeline_propagate_alpha(pipe, &rpf->entity, ctrl->val);
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops rpf_ctrl_ops = {
	.s_ctrl = rpf_s_ctrl,
};

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Core Operations
 */

static int rpf_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct vsp1_rwpf *rpf = to_rwpf(subdev);
	const struct vsp1_format_info *fmtinfo = rpf->video.fmtinfo;
	const struct v4l2_pix_format_mplane *format = &rpf->video.format;
	const struct v4l2_rect *crop = &rpf->crop;
	u32 pstride;
	u32 infmt;
	int ret;

	ret = vsp1_entity_set_streaming(&rpf->entity, enable);
	if (ret < 0)
		return ret;

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

	vsp1_rpf_write(rpf, VI6_RPF_SRCM_ADDR_Y,
		       rpf->buf_addr[0] + rpf->offsets[0]);

	if (format->num_planes > 1) {
		rpf->offsets[1] = crop->top * format->plane_fmt[1].bytesperline
				+ crop->left * fmtinfo->bpp[1] / 8;
		pstride |= format->plane_fmt[1].bytesperline
			<< VI6_RPF_SRCM_PSTRIDE_C_SHIFT;

		vsp1_rpf_write(rpf, VI6_RPF_SRCM_ADDR_C0,
			       rpf->buf_addr[1] + rpf->offsets[1]);

		if (format->num_planes > 2)
			vsp1_rpf_write(rpf, VI6_RPF_SRCM_ADDR_C1,
				       rpf->buf_addr[2] + rpf->offsets[1]);
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

static void rpf_vdev_queue(struct vsp1_video *video,
			   struct vsp1_video_buffer *buf)
{
	struct vsp1_rwpf *rpf = container_of(video, struct vsp1_rwpf, video);
	unsigned int i;

	for (i = 0; i < 3; ++i)
		rpf->buf_addr[i] = buf->addr[i];

	if (!vsp1_entity_is_streaming(&rpf->entity))
		return;

	vsp1_rpf_write(rpf, VI6_RPF_SRCM_ADDR_Y,
		       buf->addr[0] + rpf->offsets[0]);
	if (buf->buf.num_planes > 1)
		vsp1_rpf_write(rpf, VI6_RPF_SRCM_ADDR_C0,
			       buf->addr[1] + rpf->offsets[1]);
	if (buf->buf.num_planes > 2)
		vsp1_rpf_write(rpf, VI6_RPF_SRCM_ADDR_C1,
			       buf->addr[2] + rpf->offsets[1]);
}

static const struct vsp1_video_operations rpf_vdev_ops = {
	.queue = rpf_vdev_queue,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_rwpf *vsp1_rpf_create(struct vsp1_device *vsp1, unsigned int index)
{
	struct v4l2_subdev *subdev;
	struct vsp1_video *video;
	struct vsp1_rwpf *rpf;
	int ret;

	rpf = devm_kzalloc(vsp1->dev, sizeof(*rpf), GFP_KERNEL);
	if (rpf == NULL)
		return ERR_PTR(-ENOMEM);

	rpf->max_width = RPF_MAX_WIDTH;
	rpf->max_height = RPF_MAX_HEIGHT;

	rpf->entity.type = VSP1_ENTITY_RPF;
	rpf->entity.index = index;

	ret = vsp1_entity_init(vsp1, &rpf->entity, 2);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the V4L2 subdev. */
	subdev = &rpf->entity.subdev;
	v4l2_subdev_init(subdev, &rpf_ops);

	subdev->entity.ops = &vsp1_media_ops;
	subdev->internal_ops = &vsp1_subdev_internal_ops;
	snprintf(subdev->name, sizeof(subdev->name), "%s rpf.%u",
		 dev_name(vsp1->dev), index);
	v4l2_set_subdevdata(subdev, rpf);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	vsp1_entity_init_formats(subdev, NULL);

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(&rpf->ctrls, 1);
	v4l2_ctrl_new_std(&rpf->ctrls, &rpf_ctrl_ops, V4L2_CID_ALPHA_COMPONENT,
			  0, 255, 1, 255);

	rpf->entity.subdev.ctrl_handler = &rpf->ctrls;

	if (rpf->ctrls.error) {
		dev_err(vsp1->dev, "rpf%u: failed to initialize controls\n",
			index);
		ret = rpf->ctrls.error;
		goto error;
	}

	/* Initialize the video device. */
	video = &rpf->video;

	video->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	video->vsp1 = vsp1;
	video->ops = &rpf_vdev_ops;

	ret = vsp1_video_init(video, &rpf->entity);
	if (ret < 0)
		goto error;

	rpf->entity.video = video;

	/* Connect the video device to the RPF. */
	ret = media_entity_create_link(&rpf->video.video.entity, 0,
				       &rpf->entity.subdev.entity,
				       RWPF_PAD_SINK,
				       MEDIA_LNK_FL_ENABLED |
				       MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0)
		goto error;

	return rpf;

error:
	vsp1_entity_destroy(&rpf->entity);
	return ERR_PTR(ret);
}
