// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_brx.c  --  R-Car VSP1 Blend ROP Unit (BRU and BRS)
 *
 * Copyright (C) 2013 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/device.h>
#include <linux/gfp.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_brx.h"
#include "vsp1_dl.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"
#include "vsp1_video.h"

#define BRX_MIN_SIZE				1U
#define BRX_MAX_SIZE				8190U

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_brx_write(struct vsp1_brx *brx,
				  struct vsp1_dl_body *dlb, u32 reg, u32 data)
{
	vsp1_dl_body_write(dlb, brx->base + reg, data);
}

/* -----------------------------------------------------------------------------
 * Controls
 */

static int brx_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vsp1_brx *brx =
		container_of(ctrl->handler, struct vsp1_brx, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_BG_COLOR:
		brx->bgcolor = ctrl->val;
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops brx_ctrl_ops = {
	.s_ctrl = brx_s_ctrl,
};

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

/*
 * The BRx can't perform format conversion, all sink and source formats must be
 * identical. We pick the format on the first sink pad (pad 0) and propagate it
 * to all other pads.
 */

static int brx_enum_mbus_code(struct v4l2_subdev *subdev,
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

static int brx_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
	    fse->code != MEDIA_BUS_FMT_AYUV8_1X32)
		return -EINVAL;

	fse->min_width = BRX_MIN_SIZE;
	fse->max_width = BRX_MAX_SIZE;
	fse->min_height = BRX_MIN_SIZE;
	fse->max_height = BRX_MAX_SIZE;

	return 0;
}

static struct v4l2_rect *brx_get_compose(struct vsp1_brx *brx,
					 struct v4l2_subdev_pad_config *cfg,
					 unsigned int pad)
{
	return v4l2_subdev_get_try_compose(&brx->entity.subdev, cfg, pad);
}

static void brx_try_format(struct vsp1_brx *brx,
			   struct v4l2_subdev_pad_config *config,
			   unsigned int pad, struct v4l2_mbus_framefmt *fmt)
{
	struct v4l2_mbus_framefmt *format;

	switch (pad) {
	case BRX_PAD_SINK(0):
		/* Default to YUV if the requested format is not supported. */
		if (fmt->code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
		    fmt->code != MEDIA_BUS_FMT_AYUV8_1X32)
			fmt->code = MEDIA_BUS_FMT_AYUV8_1X32;
		break;

	default:
		/* The BRx can't perform format conversion. */
		format = vsp1_entity_get_pad_format(&brx->entity, config,
						    BRX_PAD_SINK(0));
		fmt->code = format->code;
		break;
	}

	fmt->width = clamp(fmt->width, BRX_MIN_SIZE, BRX_MAX_SIZE);
	fmt->height = clamp(fmt->height, BRX_MIN_SIZE, BRX_MAX_SIZE);
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
}

static int brx_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_brx *brx = to_brx(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	int ret = 0;

	mutex_lock(&brx->entity.lock);

	config = vsp1_entity_get_pad_config(&brx->entity, cfg, fmt->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	brx_try_format(brx, config, fmt->pad, &fmt->format);

	format = vsp1_entity_get_pad_format(&brx->entity, config, fmt->pad);
	*format = fmt->format;

	/* Reset the compose rectangle. */
	if (fmt->pad != brx->entity.source_pad) {
		struct v4l2_rect *compose;

		compose = brx_get_compose(brx, config, fmt->pad);
		compose->left = 0;
		compose->top = 0;
		compose->width = format->width;
		compose->height = format->height;
	}

	/* Propagate the format code to all pads. */
	if (fmt->pad == BRX_PAD_SINK(0)) {
		unsigned int i;

		for (i = 0; i <= brx->entity.source_pad; ++i) {
			format = vsp1_entity_get_pad_format(&brx->entity,
							    config, i);
			format->code = fmt->format.code;
		}
	}

done:
	mutex_unlock(&brx->entity.lock);
	return ret;
}

static int brx_get_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_brx *brx = to_brx(subdev);
	struct v4l2_subdev_pad_config *config;

	if (sel->pad == brx->entity.source_pad)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = BRX_MAX_SIZE;
		sel->r.height = BRX_MAX_SIZE;
		return 0;

	case V4L2_SEL_TGT_COMPOSE:
		config = vsp1_entity_get_pad_config(&brx->entity, cfg,
						    sel->which);
		if (!config)
			return -EINVAL;

		mutex_lock(&brx->entity.lock);
		sel->r = *brx_get_compose(brx, config, sel->pad);
		mutex_unlock(&brx->entity.lock);
		return 0;

	default:
		return -EINVAL;
	}
}

static int brx_set_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_brx *brx = to_brx(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *compose;
	int ret = 0;

	if (sel->pad == brx->entity.source_pad)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	mutex_lock(&brx->entity.lock);

	config = vsp1_entity_get_pad_config(&brx->entity, cfg, sel->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	/*
	 * The compose rectangle top left corner must be inside the output
	 * frame.
	 */
	format = vsp1_entity_get_pad_format(&brx->entity, config,
					    brx->entity.source_pad);
	sel->r.left = clamp_t(unsigned int, sel->r.left, 0, format->width - 1);
	sel->r.top = clamp_t(unsigned int, sel->r.top, 0, format->height - 1);

	/*
	 * Scaling isn't supported, the compose rectangle size must be identical
	 * to the sink format size.
	 */
	format = vsp1_entity_get_pad_format(&brx->entity, config, sel->pad);
	sel->r.width = format->width;
	sel->r.height = format->height;

	compose = brx_get_compose(brx, config, sel->pad);
	*compose = sel->r;

done:
	mutex_unlock(&brx->entity.lock);
	return ret;
}

static const struct v4l2_subdev_pad_ops brx_pad_ops = {
	.init_cfg = vsp1_entity_init_cfg,
	.enum_mbus_code = brx_enum_mbus_code,
	.enum_frame_size = brx_enum_frame_size,
	.get_fmt = vsp1_subdev_get_pad_format,
	.set_fmt = brx_set_format,
	.get_selection = brx_get_selection,
	.set_selection = brx_set_selection,
};

static const struct v4l2_subdev_ops brx_ops = {
	.pad    = &brx_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void brx_configure_stream(struct vsp1_entity *entity,
				 struct vsp1_pipeline *pipe,
				 struct vsp1_dl_body *dlb)
{
	struct vsp1_brx *brx = to_brx(&entity->subdev);
	struct v4l2_mbus_framefmt *format;
	unsigned int flags;
	unsigned int i;

	format = vsp1_entity_get_pad_format(&brx->entity, brx->entity.config,
					    brx->entity.source_pad);

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
	vsp1_brx_write(brx, dlb, VI6_BRU_INCTRL,
		       flags & V4L2_PIX_FMT_FLAG_PREMUL_ALPHA ?
		       0 : VI6_BRU_INCTRL_NRM);

	/*
	 * Set the background position to cover the whole output image and
	 * configure its color.
	 */
	vsp1_brx_write(brx, dlb, VI6_BRU_VIRRPF_SIZE,
		       (format->width << VI6_BRU_VIRRPF_SIZE_HSIZE_SHIFT) |
		       (format->height << VI6_BRU_VIRRPF_SIZE_VSIZE_SHIFT));
	vsp1_brx_write(brx, dlb, VI6_BRU_VIRRPF_LOC, 0);

	vsp1_brx_write(brx, dlb, VI6_BRU_VIRRPF_COL, brx->bgcolor |
		       (0xff << VI6_BRU_VIRRPF_COL_A_SHIFT));

	/*
	 * Route BRU input 1 as SRC input to the ROP unit and configure the ROP
	 * unit with a NOP operation to make BRU input 1 available as the
	 * Blend/ROP unit B SRC input. Only needed for BRU, the BRS has no ROP
	 * unit.
	 */
	if (entity->type == VSP1_ENTITY_BRU)
		vsp1_brx_write(brx, dlb, VI6_BRU_ROP,
			       VI6_BRU_ROP_DSTSEL_BRUIN(1) |
			       VI6_BRU_ROP_CROP(VI6_ROP_NOP) |
			       VI6_BRU_ROP_AROP(VI6_ROP_NOP));

	for (i = 0; i < brx->entity.source_pad; ++i) {
		bool premultiplied = false;
		u32 ctrl = 0;

		/*
		 * Configure all Blend/ROP units corresponding to an enabled BRx
		 * input for alpha blending. Blend/ROP units corresponding to
		 * disabled BRx inputs are used in ROP NOP mode to ignore the
		 * SRC input.
		 */
		if (brx->inputs[i].rpf) {
			ctrl |= VI6_BRU_CTRL_RBC;

			premultiplied = brx->inputs[i].rpf->format.flags
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
		 * Route inputs 0 to 3 as SRC inputs to Blend/ROP units A to D
		 * in that order. In the BRU the Blend/ROP unit B SRC is
		 * hardwired to the ROP unit output, the corresponding register
		 * bits must be set to 0. The BRS has no ROP unit and doesn't
		 * need any special processing.
		 */
		if (!(entity->type == VSP1_ENTITY_BRU && i == 1))
			ctrl |= VI6_BRU_CTRL_SRCSEL_BRUIN(i);

		vsp1_brx_write(brx, dlb, VI6_BRU_CTRL(i), ctrl);

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
		vsp1_brx_write(brx, dlb, VI6_BRU_BLD(i),
			       VI6_BRU_BLD_CCMDX_255_SRC_A |
			       (premultiplied ? VI6_BRU_BLD_CCMDY_COEFY :
						VI6_BRU_BLD_CCMDY_SRC_A) |
			       VI6_BRU_BLD_ACMDX_255_SRC_A |
			       VI6_BRU_BLD_ACMDY_COEFY |
			       (0xff << VI6_BRU_BLD_COEFY_SHIFT));
	}
}

static const struct vsp1_entity_operations brx_entity_ops = {
	.configure_stream = brx_configure_stream,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_brx *vsp1_brx_create(struct vsp1_device *vsp1,
				 enum vsp1_entity_type type)
{
	struct vsp1_brx *brx;
	unsigned int num_pads;
	const char *name;
	int ret;

	brx = devm_kzalloc(vsp1->dev, sizeof(*brx), GFP_KERNEL);
	if (brx == NULL)
		return ERR_PTR(-ENOMEM);

	brx->base = type == VSP1_ENTITY_BRU ? VI6_BRU_BASE : VI6_BRS_BASE;
	brx->entity.ops = &brx_entity_ops;
	brx->entity.type = type;

	if (type == VSP1_ENTITY_BRU) {
		num_pads = vsp1->info->num_bru_inputs + 1;
		name = "bru";
	} else {
		num_pads = 3;
		name = "brs";
	}

	ret = vsp1_entity_init(vsp1, &brx->entity, name, num_pads, &brx_ops,
			       MEDIA_ENT_F_PROC_VIDEO_COMPOSER);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(&brx->ctrls, 1);
	v4l2_ctrl_new_std(&brx->ctrls, &brx_ctrl_ops, V4L2_CID_BG_COLOR,
			  0, 0xffffff, 1, 0);

	brx->bgcolor = 0;

	brx->entity.subdev.ctrl_handler = &brx->ctrls;

	if (brx->ctrls.error) {
		dev_err(vsp1->dev, "%s: failed to initialize controls\n", name);
		ret = brx->ctrls.error;
		vsp1_entity_destroy(&brx->entity);
		return ERR_PTR(ret);
	}

	return brx;
}
