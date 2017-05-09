/*
 * vsp1_bru.c  --  R-Car VSP1 Blend ROP Unit
 *
 * Copyright (C) 2013 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/gfp.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_bru.h"
#include "vsp1_dl.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"
#include "vsp1_video.h"

#define BRU_MIN_SIZE				1U
#define BRU_MAX_SIZE				8190U

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_bru_write(struct vsp1_bru *bru, struct vsp1_dl_list *dl,
				  u32 reg, u32 data)
{
	vsp1_dl_list_write(dl, reg, data);
}

/* -----------------------------------------------------------------------------
 * Controls
 */

static int bru_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vsp1_bru *bru =
		container_of(ctrl->handler, struct vsp1_bru, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_BG_COLOR:
		bru->bgcolor = ctrl->val;
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops bru_ctrl_ops = {
	.s_ctrl = bru_s_ctrl,
};

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

/*
 * The BRU can't perform format conversion, all sink and source formats must be
 * identical. We pick the format on the first sink pad (pad 0) and propagate it
 * to all other pads.
 */

static int bru_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	static const unsigned int codes[] = {
		MEDIA_BUS_FMT_ARGB8888_1X32,
		MEDIA_BUS_FMT_AYUV8_1X32,
	};

	return vsp1_subdev_enum_mbus_code(subdev, cfg, code, codes,
					  ARRAY_SIZE(codes));
}

static int bru_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
	    fse->code != MEDIA_BUS_FMT_AYUV8_1X32)
		return -EINVAL;

	fse->min_width = BRU_MIN_SIZE;
	fse->max_width = BRU_MAX_SIZE;
	fse->min_height = BRU_MIN_SIZE;
	fse->max_height = BRU_MAX_SIZE;

	return 0;
}

static struct v4l2_rect *bru_get_compose(struct vsp1_bru *bru,
					 struct v4l2_subdev_pad_config *cfg,
					 unsigned int pad)
{
	return v4l2_subdev_get_try_compose(&bru->entity.subdev, cfg, pad);
}

static void bru_try_format(struct vsp1_bru *bru,
			   struct v4l2_subdev_pad_config *config,
			   unsigned int pad, struct v4l2_mbus_framefmt *fmt)
{
	struct v4l2_mbus_framefmt *format;

	switch (pad) {
	case BRU_PAD_SINK(0):
		/* Default to YUV if the requested format is not supported. */
		if (fmt->code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
		    fmt->code != MEDIA_BUS_FMT_AYUV8_1X32)
			fmt->code = MEDIA_BUS_FMT_AYUV8_1X32;
		break;

	default:
		/* The BRU can't perform format conversion. */
		format = vsp1_entity_get_pad_format(&bru->entity, config,
						    BRU_PAD_SINK(0));
		fmt->code = format->code;
		break;
	}

	fmt->width = clamp(fmt->width, BRU_MIN_SIZE, BRU_MAX_SIZE);
	fmt->height = clamp(fmt->height, BRU_MIN_SIZE, BRU_MAX_SIZE);
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
}

static int bru_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_bru *bru = to_bru(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	int ret = 0;

	mutex_lock(&bru->entity.lock);

	config = vsp1_entity_get_pad_config(&bru->entity, cfg, fmt->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	bru_try_format(bru, config, fmt->pad, &fmt->format);

	format = vsp1_entity_get_pad_format(&bru->entity, config, fmt->pad);
	*format = fmt->format;

	/* Reset the compose rectangle */
	if (fmt->pad != bru->entity.source_pad) {
		struct v4l2_rect *compose;

		compose = bru_get_compose(bru, config, fmt->pad);
		compose->left = 0;
		compose->top = 0;
		compose->width = format->width;
		compose->height = format->height;
	}

	/* Propagate the format code to all pads */
	if (fmt->pad == BRU_PAD_SINK(0)) {
		unsigned int i;

		for (i = 0; i <= bru->entity.source_pad; ++i) {
			format = vsp1_entity_get_pad_format(&bru->entity,
							    config, i);
			format->code = fmt->format.code;
		}
	}

done:
	mutex_unlock(&bru->entity.lock);
	return ret;
}

static int bru_get_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_bru *bru = to_bru(subdev);
	struct v4l2_subdev_pad_config *config;

	if (sel->pad == bru->entity.source_pad)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = BRU_MAX_SIZE;
		sel->r.height = BRU_MAX_SIZE;
		return 0;

	case V4L2_SEL_TGT_COMPOSE:
		config = vsp1_entity_get_pad_config(&bru->entity, cfg,
						    sel->which);
		if (!config)
			return -EINVAL;

		mutex_lock(&bru->entity.lock);
		sel->r = *bru_get_compose(bru, config, sel->pad);
		mutex_unlock(&bru->entity.lock);
		return 0;

	default:
		return -EINVAL;
	}
}

static int bru_set_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_bru *bru = to_bru(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *compose;
	int ret = 0;

	if (sel->pad == bru->entity.source_pad)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	mutex_lock(&bru->entity.lock);

	config = vsp1_entity_get_pad_config(&bru->entity, cfg, sel->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	/*
	 * The compose rectangle top left corner must be inside the output
	 * frame.
	 */
	format = vsp1_entity_get_pad_format(&bru->entity, config,
					    bru->entity.source_pad);
	sel->r.left = clamp_t(unsigned int, sel->r.left, 0, format->width - 1);
	sel->r.top = clamp_t(unsigned int, sel->r.top, 0, format->height - 1);

	/*
	 * Scaling isn't supported, the compose rectangle size must be identical
	 * to the sink format size.
	 */
	format = vsp1_entity_get_pad_format(&bru->entity, config, sel->pad);
	sel->r.width = format->width;
	sel->r.height = format->height;

	compose = bru_get_compose(bru, config, sel->pad);
	*compose = sel->r;

done:
	mutex_unlock(&bru->entity.lock);
	return ret;
}

static const struct v4l2_subdev_pad_ops bru_pad_ops = {
	.init_cfg = vsp1_entity_init_cfg,
	.enum_mbus_code = bru_enum_mbus_code,
	.enum_frame_size = bru_enum_frame_size,
	.get_fmt = vsp1_subdev_get_pad_format,
	.set_fmt = bru_set_format,
	.get_selection = bru_get_selection,
	.set_selection = bru_set_selection,
};

static const struct v4l2_subdev_ops bru_ops = {
	.pad    = &bru_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void bru_configure(struct vsp1_entity *entity,
			  struct vsp1_pipeline *pipe,
			  struct vsp1_dl_list *dl,
			  enum vsp1_entity_params params)
{
	struct vsp1_bru *bru = to_bru(&entity->subdev);
	struct v4l2_mbus_framefmt *format;
	unsigned int flags;
	unsigned int i;

	if (params != VSP1_ENTITY_PARAMS_INIT)
		return;

	format = vsp1_entity_get_pad_format(&bru->entity, bru->entity.config,
					    bru->entity.source_pad);

	/*
	 * The hardware is extremely flexible but we have no userspace API to
	 * expose all the parameters, nor is it clear whether we would have use
	 * cases for all the supported modes. Let's just harcode the parameters
	 * to sane default values for now.
	 */

	/*
	 * Disable dithering and enable color data normalization unless the
	 * format at the pipeline output is premultiplied.
	 */
	flags = pipe->output ? pipe->output->format.flags : 0;
	vsp1_bru_write(bru, dl, VI6_BRU_INCTRL,
		       flags & V4L2_PIX_FMT_FLAG_PREMUL_ALPHA ?
		       0 : VI6_BRU_INCTRL_NRM);

	/*
	 * Set the background position to cover the whole output image and
	 * configure its color.
	 */
	vsp1_bru_write(bru, dl, VI6_BRU_VIRRPF_SIZE,
		       (format->width << VI6_BRU_VIRRPF_SIZE_HSIZE_SHIFT) |
		       (format->height << VI6_BRU_VIRRPF_SIZE_VSIZE_SHIFT));
	vsp1_bru_write(bru, dl, VI6_BRU_VIRRPF_LOC, 0);

	vsp1_bru_write(bru, dl, VI6_BRU_VIRRPF_COL, bru->bgcolor |
		       (0xff << VI6_BRU_VIRRPF_COL_A_SHIFT));

	/*
	 * Route BRU input 1 as SRC input to the ROP unit and configure the ROP
	 * unit with a NOP operation to make BRU input 1 available as the
	 * Blend/ROP unit B SRC input.
	 */
	vsp1_bru_write(bru, dl, VI6_BRU_ROP, VI6_BRU_ROP_DSTSEL_BRUIN(1) |
		       VI6_BRU_ROP_CROP(VI6_ROP_NOP) |
		       VI6_BRU_ROP_AROP(VI6_ROP_NOP));

	for (i = 0; i < bru->entity.source_pad; ++i) {
		bool premultiplied = false;
		u32 ctrl = 0;

		/*
		 * Configure all Blend/ROP units corresponding to an enabled BRU
		 * input for alpha blending. Blend/ROP units corresponding to
		 * disabled BRU inputs are used in ROP NOP mode to ignore the
		 * SRC input.
		 */
		if (bru->inputs[i].rpf) {
			ctrl |= VI6_BRU_CTRL_RBC;

			premultiplied = bru->inputs[i].rpf->format.flags
				      & V4L2_PIX_FMT_FLAG_PREMUL_ALPHA;
		} else {
			ctrl |= VI6_BRU_CTRL_CROP(VI6_ROP_NOP)
			     |  VI6_BRU_CTRL_AROP(VI6_ROP_NOP);
		}

		/*
		 * Select the virtual RPF as the Blend/ROP unit A DST input to
		 * serve as a background color.
		 */
		if (i == 0)
			ctrl |= VI6_BRU_CTRL_DSTSEL_VRPF;

		/*
		 * Route BRU inputs 0 to 3 as SRC inputs to Blend/ROP units A to
		 * D in that order. The Blend/ROP unit B SRC is hardwired to the
		 * ROP unit output, the corresponding register bits must be set
		 * to 0.
		 */
		if (i != 1)
			ctrl |= VI6_BRU_CTRL_SRCSEL_BRUIN(i);

		vsp1_bru_write(bru, dl, VI6_BRU_CTRL(i), ctrl);

		/*
		 * Harcode the blending formula to
		 *
		 *	DSTc = DSTc * (1 - SRCa) + SRCc * SRCa
		 *	DSTa = DSTa * (1 - SRCa) + SRCa
		 *
		 * when the SRC input isn't premultiplied, and to
		 *
		 *	DSTc = DSTc * (1 - SRCa) + SRCc
		 *	DSTa = DSTa * (1 - SRCa) + SRCa
		 *
		 * otherwise.
		 */
		vsp1_bru_write(bru, dl, VI6_BRU_BLD(i),
			       VI6_BRU_BLD_CCMDX_255_SRC_A |
			       (premultiplied ? VI6_BRU_BLD_CCMDY_COEFY :
						VI6_BRU_BLD_CCMDY_SRC_A) |
			       VI6_BRU_BLD_ACMDX_255_SRC_A |
			       VI6_BRU_BLD_ACMDY_COEFY |
			       (0xff << VI6_BRU_BLD_COEFY_SHIFT));
	}
}

static const struct vsp1_entity_operations bru_entity_ops = {
	.configure = bru_configure,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_bru *vsp1_bru_create(struct vsp1_device *vsp1)
{
	struct vsp1_bru *bru;
	int ret;

	bru = devm_kzalloc(vsp1->dev, sizeof(*bru), GFP_KERNEL);
	if (bru == NULL)
		return ERR_PTR(-ENOMEM);

	bru->entity.ops = &bru_entity_ops;
	bru->entity.type = VSP1_ENTITY_BRU;

	ret = vsp1_entity_init(vsp1, &bru->entity, "bru",
			       vsp1->info->num_bru_inputs + 1, &bru_ops,
			       MEDIA_ENT_F_PROC_VIDEO_COMPOSER);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(&bru->ctrls, 1);
	v4l2_ctrl_new_std(&bru->ctrls, &bru_ctrl_ops, V4L2_CID_BG_COLOR,
			  0, 0xffffff, 1, 0);

	bru->bgcolor = 0;

	bru->entity.subdev.ctrl_handler = &bru->ctrls;

	if (bru->ctrls.error) {
		dev_err(vsp1->dev, "bru: failed to initialize controls\n");
		ret = bru->ctrls.error;
		vsp1_entity_destroy(&bru->entity);
		return ERR_PTR(ret);
	}

	return bru;
}
