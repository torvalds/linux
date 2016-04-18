/*
 * vsp1_wpf.c  --  R-Car VSP1 Write Pixel Formatter
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

#define WPF_MAX_WIDTH				2048
#define WPF_MAX_HEIGHT				2048

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline u32 vsp1_wpf_read(struct vsp1_rwpf *wpf, u32 reg)
{
	return vsp1_read(wpf->entity.vsp1,
			 reg + wpf->entity.index * VI6_WPF_OFFSET);
}

static inline void vsp1_wpf_write(struct vsp1_rwpf *wpf, u32 reg, u32 data)
{
	vsp1_mod_write(&wpf->entity,
		       reg + wpf->entity.index * VI6_WPF_OFFSET, data);
}

/* -----------------------------------------------------------------------------
 * Controls
 */

static int wpf_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vsp1_rwpf *wpf =
		container_of(ctrl->handler, struct vsp1_rwpf, ctrls);
	u32 value;

	if (!vsp1_entity_is_streaming(&wpf->entity))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ALPHA_COMPONENT:
		value = vsp1_wpf_read(wpf, VI6_WPF_OUTFMT);
		value &= ~VI6_WPF_OUTFMT_PDV_MASK;
		value |= ctrl->val << VI6_WPF_OUTFMT_PDV_SHIFT;
		vsp1_wpf_write(wpf, VI6_WPF_OUTFMT, value);
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops wpf_ctrl_ops = {
	.s_ctrl = wpf_s_ctrl,
};

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Core Operations
 */

static int wpf_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct vsp1_pipeline *pipe = to_vsp1_pipeline(&subdev->entity);
	struct vsp1_rwpf *wpf = to_rwpf(subdev);
	struct vsp1_device *vsp1 = wpf->entity.vsp1;
	const struct v4l2_rect *crop = &wpf->crop;
	unsigned int i;
	u32 srcrpf = 0;
	u32 outfmt = 0;
	int ret;

	ret = vsp1_entity_set_streaming(&wpf->entity, enable);
	if (ret < 0)
		return ret;

	if (!enable) {
		vsp1_write(vsp1, VI6_WPF_IRQ_ENB(wpf->entity.index), 0);
		vsp1_write(vsp1, wpf->entity.index * VI6_WPF_OFFSET +
			   VI6_WPF_SRCRPF, 0);
		return 0;
	}

	/* Sources. If the pipeline has a single input and BRU is not used,
	 * configure it as the master layer. Otherwise configure all
	 * inputs as sub-layers and select the virtual RPF as the master
	 * layer.
	 */
	for (i = 0; i < vsp1->info->rpf_count; ++i) {
		struct vsp1_rwpf *input = pipe->inputs[i];

		if (!input)
			continue;

		srcrpf |= (!pipe->bru && pipe->num_inputs == 1)
			? VI6_WPF_SRCRPF_RPF_ACT_MST(input->entity.index)
			: VI6_WPF_SRCRPF_RPF_ACT_SUB(input->entity.index);
	}

	if (pipe->bru || pipe->num_inputs > 1)
		srcrpf |= VI6_WPF_SRCRPF_VIRACT_MST;

	vsp1_wpf_write(wpf, VI6_WPF_SRCRPF, srcrpf);

	/* Destination stride. */
	if (!pipe->lif) {
		struct v4l2_pix_format_mplane *format = &wpf->format;

		vsp1_wpf_write(wpf, VI6_WPF_DSTM_STRIDE_Y,
			       format->plane_fmt[0].bytesperline);
		if (format->num_planes > 1)
			vsp1_wpf_write(wpf, VI6_WPF_DSTM_STRIDE_C,
				       format->plane_fmt[1].bytesperline);
	}

	vsp1_wpf_write(wpf, VI6_WPF_HSZCLIP, VI6_WPF_SZCLIP_EN |
		       (crop->left << VI6_WPF_SZCLIP_OFST_SHIFT) |
		       (crop->width << VI6_WPF_SZCLIP_SIZE_SHIFT));
	vsp1_wpf_write(wpf, VI6_WPF_VSZCLIP, VI6_WPF_SZCLIP_EN |
		       (crop->top << VI6_WPF_SZCLIP_OFST_SHIFT) |
		       (crop->height << VI6_WPF_SZCLIP_SIZE_SHIFT));

	/* Format */
	if (!pipe->lif) {
		const struct vsp1_format_info *fmtinfo = wpf->fmtinfo;

		outfmt = fmtinfo->hwfmt << VI6_WPF_OUTFMT_WRFMT_SHIFT;

		if (fmtinfo->alpha)
			outfmt |= VI6_WPF_OUTFMT_PXA;
		if (fmtinfo->swap_yc)
			outfmt |= VI6_WPF_OUTFMT_SPYCS;
		if (fmtinfo->swap_uv)
			outfmt |= VI6_WPF_OUTFMT_SPUVS;

		vsp1_wpf_write(wpf, VI6_WPF_DSWAP, fmtinfo->swap);
	}

	if (wpf->entity.formats[RWPF_PAD_SINK].code !=
	    wpf->entity.formats[RWPF_PAD_SOURCE].code)
		outfmt |= VI6_WPF_OUTFMT_CSC;

	/* Take the control handler lock to ensure that the PDV value won't be
	 * changed behind our back by a set control operation.
	 */
	if (vsp1->info->uapi)
		mutex_lock(wpf->ctrls.lock);
	outfmt |= wpf->alpha->cur.val << VI6_WPF_OUTFMT_PDV_SHIFT;
	vsp1_wpf_write(wpf, VI6_WPF_OUTFMT, outfmt);
	if (vsp1->info->uapi)
		mutex_unlock(wpf->ctrls.lock);

	vsp1_mod_write(&wpf->entity, VI6_DPR_WPF_FPORCH(wpf->entity.index),
		       VI6_DPR_WPF_FPORCH_FP_WPFN);

	vsp1_mod_write(&wpf->entity, VI6_WPF_WRBCK_CTRL, 0);

	/* Enable interrupts */
	vsp1_write(vsp1, VI6_WPF_IRQ_STA(wpf->entity.index), 0);
	vsp1_write(vsp1, VI6_WPF_IRQ_ENB(wpf->entity.index),
		   VI6_WFP_IRQ_ENB_FREE);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static struct v4l2_subdev_video_ops wpf_video_ops = {
	.s_stream = wpf_s_stream,
};

static struct v4l2_subdev_pad_ops wpf_pad_ops = {
	.enum_mbus_code = vsp1_rwpf_enum_mbus_code,
	.enum_frame_size = vsp1_rwpf_enum_frame_size,
	.get_fmt = vsp1_rwpf_get_format,
	.set_fmt = vsp1_rwpf_set_format,
	.get_selection = vsp1_rwpf_get_selection,
	.set_selection = vsp1_rwpf_set_selection,
};

static struct v4l2_subdev_ops wpf_ops = {
	.video	= &wpf_video_ops,
	.pad    = &wpf_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Video Device Operations
 */

static void wpf_set_memory(struct vsp1_rwpf *wpf, struct vsp1_rwpf_memory *mem)
{
	vsp1_wpf_write(wpf, VI6_WPF_DSTM_ADDR_Y, mem->addr[0]);
	if (mem->num_planes > 1)
		vsp1_wpf_write(wpf, VI6_WPF_DSTM_ADDR_C0, mem->addr[1]);
	if (mem->num_planes > 2)
		vsp1_wpf_write(wpf, VI6_WPF_DSTM_ADDR_C1, mem->addr[2]);
}

static const struct vsp1_rwpf_operations wpf_vdev_ops = {
	.set_memory = wpf_set_memory,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_rwpf *vsp1_wpf_create(struct vsp1_device *vsp1, unsigned int index)
{
	struct v4l2_subdev *subdev;
	struct vsp1_rwpf *wpf;
	int ret;

	wpf = devm_kzalloc(vsp1->dev, sizeof(*wpf), GFP_KERNEL);
	if (wpf == NULL)
		return ERR_PTR(-ENOMEM);

	wpf->ops = &wpf_vdev_ops;

	wpf->max_width = WPF_MAX_WIDTH;
	wpf->max_height = WPF_MAX_HEIGHT;

	wpf->entity.type = VSP1_ENTITY_WPF;
	wpf->entity.index = index;

	ret = vsp1_entity_init(vsp1, &wpf->entity, 2);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the V4L2 subdev. */
	subdev = &wpf->entity.subdev;
	v4l2_subdev_init(subdev, &wpf_ops);

	subdev->entity.ops = &vsp1->media_ops;
	subdev->internal_ops = &vsp1_subdev_internal_ops;
	snprintf(subdev->name, sizeof(subdev->name), "%s wpf.%u",
		 dev_name(vsp1->dev), index);
	v4l2_set_subdevdata(subdev, wpf);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	vsp1_entity_init_formats(subdev, NULL);

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(&wpf->ctrls, 1);
	wpf->alpha = v4l2_ctrl_new_std(&wpf->ctrls, &wpf_ctrl_ops,
				       V4L2_CID_ALPHA_COMPONENT,
				       0, 255, 1, 255);

	wpf->entity.subdev.ctrl_handler = &wpf->ctrls;

	if (wpf->ctrls.error) {
		dev_err(vsp1->dev, "wpf%u: failed to initialize controls\n",
			index);
		ret = wpf->ctrls.error;
		goto error;
	}

	return wpf;

error:
	vsp1_entity_destroy(&wpf->entity);
	return ERR_PTR(ret);
}
