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
	vsp1_write(wpf->entity.vsp1,
		   reg + wpf->entity.index * VI6_WPF_OFFSET, data);
}

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

	if (!enable) {
		vsp1_write(vsp1, VI6_WPF_IRQ_ENB(wpf->entity.index), 0);
		return 0;
	}

	/* Sources. If the pipeline has a single input configure it as the
	 * master layer. Otherwise configure all inputs as sub-layers and
	 * select the virtual RPF as the master layer.
	 */
	for (i = 0; i < pipe->num_inputs; ++i) {
		struct vsp1_rwpf *input = pipe->inputs[i];

		srcrpf |= pipe->num_inputs == 1
			? VI6_WPF_SRCRPF_RPF_ACT_MST(input->entity.index)
			: VI6_WPF_SRCRPF_RPF_ACT_SUB(input->entity.index);
	}

	if (pipe->num_inputs > 1)
		srcrpf |= VI6_WPF_SRCRPF_VIRACT_MST;

	vsp1_wpf_write(wpf, VI6_WPF_SRCRPF, srcrpf);

	/* Destination stride. */
	if (!pipe->lif) {
		struct v4l2_pix_format_mplane *format = &wpf->video.format;

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
		const struct vsp1_format_info *fmtinfo = wpf->video.fmtinfo;

		outfmt = fmtinfo->hwfmt << VI6_WPF_OUTFMT_WRFMT_SHIFT;

		if (fmtinfo->swap_yc)
			outfmt |= VI6_WPF_OUTFMT_SPYCS;
		if (fmtinfo->swap_uv)
			outfmt |= VI6_WPF_OUTFMT_SPUVS;

		vsp1_wpf_write(wpf, VI6_WPF_DSWAP, fmtinfo->swap);
	}

	if (wpf->entity.formats[RWPF_PAD_SINK].code !=
	    wpf->entity.formats[RWPF_PAD_SOURCE].code)
		outfmt |= VI6_WPF_OUTFMT_CSC;

	vsp1_wpf_write(wpf, VI6_WPF_OUTFMT, outfmt);

	vsp1_write(vsp1, VI6_DPR_WPF_FPORCH(wpf->entity.index),
		   VI6_DPR_WPF_FPORCH_FP_WPFN);

	vsp1_write(vsp1, VI6_WPF_WRBCK_CTRL, 0);

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

static void wpf_vdev_queue(struct vsp1_video *video,
			   struct vsp1_video_buffer *buf)
{
	struct vsp1_rwpf *wpf = container_of(video, struct vsp1_rwpf, video);

	vsp1_wpf_write(wpf, VI6_WPF_DSTM_ADDR_Y, buf->addr[0]);
	if (buf->buf.num_planes > 1)
		vsp1_wpf_write(wpf, VI6_WPF_DSTM_ADDR_C0, buf->addr[1]);
	if (buf->buf.num_planes > 2)
		vsp1_wpf_write(wpf, VI6_WPF_DSTM_ADDR_C1, buf->addr[2]);
}

static const struct vsp1_video_operations wpf_vdev_ops = {
	.queue = wpf_vdev_queue,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_rwpf *vsp1_wpf_create(struct vsp1_device *vsp1, unsigned int index)
{
	struct v4l2_subdev *subdev;
	struct vsp1_video *video;
	struct vsp1_rwpf *wpf;
	unsigned int flags;
	int ret;

	wpf = devm_kzalloc(vsp1->dev, sizeof(*wpf), GFP_KERNEL);
	if (wpf == NULL)
		return ERR_PTR(-ENOMEM);

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

	subdev->entity.ops = &vsp1_media_ops;
	subdev->internal_ops = &vsp1_subdev_internal_ops;
	snprintf(subdev->name, sizeof(subdev->name), "%s wpf.%u",
		 dev_name(vsp1->dev), index);
	v4l2_set_subdevdata(subdev, wpf);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	vsp1_entity_init_formats(subdev, NULL);

	/* Initialize the video device. */
	video = &wpf->video;

	video->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	video->vsp1 = vsp1;
	video->ops = &wpf_vdev_ops;

	ret = vsp1_video_init(video, &wpf->entity);
	if (ret < 0)
		goto error;

	wpf->entity.video = video;

	/* Connect the video device to the WPF. All connections are immutable
	 * except for the WPF0 source link if a LIF is present.
	 */
	flags = MEDIA_LNK_FL_ENABLED;
	if (!(vsp1->pdata->features & VSP1_HAS_LIF) || index != 0)
		flags |= MEDIA_LNK_FL_IMMUTABLE;

	ret = media_entity_create_link(&wpf->entity.subdev.entity,
				       RWPF_PAD_SOURCE,
				       &wpf->video.video.entity, 0, flags);
	if (ret < 0)
		goto error;

	wpf->entity.sink = &wpf->video.video.entity;

	return wpf;

error:
	vsp1_entity_destroy(&wpf->entity);
	return ERR_PTR(ret);
}
