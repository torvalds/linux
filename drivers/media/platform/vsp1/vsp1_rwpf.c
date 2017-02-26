/*
 * vsp1_rwpf.c  --  R-Car VSP1 Read and Write Pixel Formatters
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

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_rwpf.h"
#include "vsp1_video.h"

#define RWPF_MIN_WIDTH				1
#define RWPF_MIN_HEIGHT				1

struct v4l2_rect *vsp1_rwpf_get_crop(struct vsp1_rwpf *rwpf,
				     struct v4l2_subdev_pad_config *config)
{
	return v4l2_subdev_get_try_crop(&rwpf->entity.subdev, config,
					RWPF_PAD_SINK);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static int vsp1_rwpf_enum_mbus_code(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_mbus_code_enum *code)
{
	static const unsigned int codes[] = {
		MEDIA_BUS_FMT_ARGB8888_1X32,
		MEDIA_BUS_FMT_AHSV8888_1X32,
		MEDIA_BUS_FMT_AYUV8_1X32,
	};

	if (code->index >= ARRAY_SIZE(codes))
		return -EINVAL;

	code->code = codes[code->index];

	return 0;
}

static int vsp1_rwpf_enum_frame_size(struct v4l2_subdev *subdev,
				     struct v4l2_subdev_pad_config *cfg,
				     struct v4l2_subdev_frame_size_enum *fse)
{
	struct vsp1_rwpf *rwpf = to_rwpf(subdev);

	return vsp1_subdev_enum_frame_size(subdev, cfg, fse, RWPF_MIN_WIDTH,
					   RWPF_MIN_HEIGHT, rwpf->max_width,
					   rwpf->max_height);
}

static int vsp1_rwpf_set_format(struct v4l2_subdev *subdev,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_format *fmt)
{
	struct vsp1_rwpf *rwpf = to_rwpf(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	int ret = 0;

	mutex_lock(&rwpf->entity.lock);

	config = vsp1_entity_get_pad_config(&rwpf->entity, cfg, fmt->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	/* Default to YUV if the requested format is not supported. */
	if (fmt->format.code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
	    fmt->format.code != MEDIA_BUS_FMT_AHSV8888_1X32 &&
	    fmt->format.code != MEDIA_BUS_FMT_AYUV8_1X32)
		fmt->format.code = MEDIA_BUS_FMT_AYUV8_1X32;

	format = vsp1_entity_get_pad_format(&rwpf->entity, config, fmt->pad);

	if (fmt->pad == RWPF_PAD_SOURCE) {
		/*
		 * The RWPF performs format conversion but can't scale, only the
		 * format code can be changed on the source pad.
		 */
		format->code = fmt->format.code;
		fmt->format = *format;
		goto done;
	}

	format->code = fmt->format.code;
	format->width = clamp_t(unsigned int, fmt->format.width,
				RWPF_MIN_WIDTH, rwpf->max_width);
	format->height = clamp_t(unsigned int, fmt->format.height,
				 RWPF_MIN_HEIGHT, rwpf->max_height);
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	fmt->format = *format;

	if (rwpf->entity.type == VSP1_ENTITY_RPF) {
		struct v4l2_rect *crop;

		/* Update the sink crop rectangle. */
		crop = vsp1_rwpf_get_crop(rwpf, config);
		crop->left = 0;
		crop->top = 0;
		crop->width = fmt->format.width;
		crop->height = fmt->format.height;
	}

	/* Propagate the format to the source pad. */
	format = vsp1_entity_get_pad_format(&rwpf->entity, config,
					    RWPF_PAD_SOURCE);
	*format = fmt->format;

done:
	mutex_unlock(&rwpf->entity.lock);
	return ret;
}

static int vsp1_rwpf_get_selection(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_selection *sel)
{
	struct vsp1_rwpf *rwpf = to_rwpf(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	int ret = 0;

	/*
	 * Cropping is only supported on the RPF and is implemented on the sink
	 * pad.
	 */
	if (rwpf->entity.type == VSP1_ENTITY_WPF || sel->pad != RWPF_PAD_SINK)
		return -EINVAL;

	mutex_lock(&rwpf->entity.lock);

	config = vsp1_entity_get_pad_config(&rwpf->entity, cfg, sel->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *vsp1_rwpf_get_crop(rwpf, config);
		break;

	case V4L2_SEL_TGT_CROP_BOUNDS:
		format = vsp1_entity_get_pad_format(&rwpf->entity, config,
						    RWPF_PAD_SINK);
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = format->width;
		sel->r.height = format->height;
		break;

	default:
		ret = -EINVAL;
		break;
	}

done:
	mutex_unlock(&rwpf->entity.lock);
	return ret;
}

static int vsp1_rwpf_set_selection(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_selection *sel)
{
	struct vsp1_rwpf *rwpf = to_rwpf(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;
	int ret = 0;

	/*
	 * Cropping is only supported on the RPF and is implemented on the sink
	 * pad.
	 */
	if (rwpf->entity.type == VSP1_ENTITY_WPF || sel->pad != RWPF_PAD_SINK)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	mutex_lock(&rwpf->entity.lock);

	config = vsp1_entity_get_pad_config(&rwpf->entity, cfg, sel->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	/* Make sure the crop rectangle is entirely contained in the image. */
	format = vsp1_entity_get_pad_format(&rwpf->entity, config,
					    RWPF_PAD_SINK);

	/*
	 * Restrict the crop rectangle coordinates to multiples of 2 to avoid
	 * shifting the color plane.
	 */
	if (format->code == MEDIA_BUS_FMT_AYUV8_1X32) {
		sel->r.left = ALIGN(sel->r.left, 2);
		sel->r.top = ALIGN(sel->r.top, 2);
		sel->r.width = round_down(sel->r.width, 2);
		sel->r.height = round_down(sel->r.height, 2);
	}

	sel->r.left = min_t(unsigned int, sel->r.left, format->width - 2);
	sel->r.top = min_t(unsigned int, sel->r.top, format->height - 2);
	sel->r.width = min_t(unsigned int, sel->r.width,
			     format->width - sel->r.left);
	sel->r.height = min_t(unsigned int, sel->r.height,
			      format->height - sel->r.top);

	crop = vsp1_rwpf_get_crop(rwpf, config);
	*crop = sel->r;

	/* Propagate the format to the source pad. */
	format = vsp1_entity_get_pad_format(&rwpf->entity, config,
					    RWPF_PAD_SOURCE);
	format->width = crop->width;
	format->height = crop->height;

done:
	mutex_unlock(&rwpf->entity.lock);
	return ret;
}

const struct v4l2_subdev_pad_ops vsp1_rwpf_pad_ops = {
	.init_cfg = vsp1_entity_init_cfg,
	.enum_mbus_code = vsp1_rwpf_enum_mbus_code,
	.enum_frame_size = vsp1_rwpf_enum_frame_size,
	.get_fmt = vsp1_subdev_get_pad_format,
	.set_fmt = vsp1_rwpf_set_format,
	.get_selection = vsp1_rwpf_get_selection,
	.set_selection = vsp1_rwpf_set_selection,
};

/* -----------------------------------------------------------------------------
 * Controls
 */

static int vsp1_rwpf_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vsp1_rwpf *rwpf =
		container_of(ctrl->handler, struct vsp1_rwpf, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_ALPHA_COMPONENT:
		rwpf->alpha = ctrl->val;
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops vsp1_rwpf_ctrl_ops = {
	.s_ctrl = vsp1_rwpf_s_ctrl,
};

int vsp1_rwpf_init_ctrls(struct vsp1_rwpf *rwpf, unsigned int ncontrols)
{
	v4l2_ctrl_handler_init(&rwpf->ctrls, ncontrols + 1);
	v4l2_ctrl_new_std(&rwpf->ctrls, &vsp1_rwpf_ctrl_ops,
			  V4L2_CID_ALPHA_COMPONENT, 0, 255, 1, 255);

	rwpf->entity.subdev.ctrl_handler = &rwpf->ctrls;

	return rwpf->ctrls.error;
}
