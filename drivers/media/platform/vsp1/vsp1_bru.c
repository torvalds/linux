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
#include "vsp1_rwpf.h"

#define BRU_MIN_SIZE				4U
#define BRU_MAX_SIZE				8190U

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline u32 vsp1_bru_read(struct vsp1_bru *bru, u32 reg)
{
	return vsp1_read(bru->entity.vsp1, reg);
}

static inline void vsp1_bru_write(struct vsp1_bru *bru, u32 reg, u32 data)
{
	vsp1_write(bru->entity.vsp1, reg, data);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Core Operations
 */

static int bru_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct vsp1_pipeline *pipe = to_vsp1_pipeline(&subdev->entity);
	struct vsp1_bru *bru = to_bru(subdev);
	struct v4l2_mbus_framefmt *format;
	unsigned int flags;
	unsigned int i;

	if (!enable)
		return 0;

	format = &bru->entity.formats[BRU_PAD_SOURCE];

	/* The hardware is extremely flexible but we have no userspace API to
	 * expose all the parameters, nor is it clear whether we would have use
	 * cases for all the supported modes. Let's just harcode the parameters
	 * to sane default values for now.
	 */

	/* Disable dithering and enable color data normalization unless the
	 * format at the pipeline output is premultiplied.
	 */
	flags = pipe->output ? pipe->output->video.format.flags : 0;
	vsp1_bru_write(bru, VI6_BRU_INCTRL,
		       flags & V4L2_PIX_FMT_FLAG_PREMUL_ALPHA ?
		       0 : VI6_BRU_INCTRL_NRM);

	/* Set the background position to cover the whole output image and
	 * set its color to opaque black.
	 */
	vsp1_bru_write(bru, VI6_BRU_VIRRPF_SIZE,
		       (format->width << VI6_BRU_VIRRPF_SIZE_HSIZE_SHIFT) |
		       (format->height << VI6_BRU_VIRRPF_SIZE_VSIZE_SHIFT));
	vsp1_bru_write(bru, VI6_BRU_VIRRPF_LOC, 0);
	vsp1_bru_write(bru, VI6_BRU_VIRRPF_COL,
		       0xff << VI6_BRU_VIRRPF_COL_A_SHIFT);

	/* Route BRU input 1 as SRC input to the ROP unit and configure the ROP
	 * unit with a NOP operation to make BRU input 1 available as the
	 * Blend/ROP unit B SRC input.
	 */
	vsp1_bru_write(bru, VI6_BRU_ROP, VI6_BRU_ROP_DSTSEL_BRUIN(1) |
		       VI6_BRU_ROP_CROP(VI6_ROP_NOP) |
		       VI6_BRU_ROP_AROP(VI6_ROP_NOP));

	for (i = 0; i < 4; ++i) {
		bool premultiplied = false;
		u32 ctrl = 0;

		/* Configure all Blend/ROP units corresponding to an enabled BRU
		 * input for alpha blending. Blend/ROP units corresponding to
		 * disabled BRU inputs are used in ROP NOP mode to ignore the
		 * SRC input.
		 */
		if (bru->inputs[i].rpf) {
			ctrl |= VI6_BRU_CTRL_RBC;

			premultiplied = bru->inputs[i].rpf->video.format.flags
				      & V4L2_PIX_FMT_FLAG_PREMUL_ALPHA;
		} else {
			ctrl |= VI6_BRU_CTRL_CROP(VI6_ROP_NOP)
			     |  VI6_BRU_CTRL_AROP(VI6_ROP_NOP);
		}

		/* Select the virtual RPF as the Blend/ROP unit A DST input to
		 * serve as a background color.
		 */
		if (i == 0)
			ctrl |= VI6_BRU_CTRL_DSTSEL_VRPF;

		/* Route BRU inputs 0 to 3 as SRC inputs to Blend/ROP units A to
		 * D in that order. The Blend/ROP unit B SRC is hardwired to the
		 * ROP unit output, the corresponding register bits must be set
		 * to 0.
		 */
		if (i != 1)
			ctrl |= VI6_BRU_CTRL_SRCSEL_BRUIN(i);

		vsp1_bru_write(bru, VI6_BRU_CTRL(i), ctrl);

		/* Harcode the blending formula to
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
		vsp1_bru_write(bru, VI6_BRU_BLD(i),
			       VI6_BRU_BLD_CCMDX_255_SRC_A |
			       (premultiplied ? VI6_BRU_BLD_CCMDY_COEFY :
						VI6_BRU_BLD_CCMDY_SRC_A) |
			       VI6_BRU_BLD_ACMDX_255_SRC_A |
			       VI6_BRU_BLD_ACMDY_COEFY |
			       (0xff << VI6_BRU_BLD_COEFY_SHIFT));
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

/*
 * The BRU can't perform format conversion, all sink and source formats must be
 * identical. We pick the format on the first sink pad (pad 0) and propagate it
 * to all other pads.
 */

static int bru_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	static const unsigned int codes[] = {
		V4L2_MBUS_FMT_ARGB8888_1X32,
		V4L2_MBUS_FMT_AYUV8_1X32,
	};
	struct v4l2_mbus_framefmt *format;

	if (code->pad == BRU_PAD_SINK(0)) {
		if (code->index >= ARRAY_SIZE(codes))
			return -EINVAL;

		code->code = codes[code->index];
	} else {
		if (code->index)
			return -EINVAL;

		format = v4l2_subdev_get_try_format(fh, BRU_PAD_SINK(0));
		code->code = format->code;
	}

	return 0;
}

static int bru_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index)
		return -EINVAL;

	if (fse->code != V4L2_MBUS_FMT_ARGB8888_1X32 &&
	    fse->code != V4L2_MBUS_FMT_AYUV8_1X32)
		return -EINVAL;

	fse->min_width = BRU_MIN_SIZE;
	fse->max_width = BRU_MAX_SIZE;
	fse->min_height = BRU_MIN_SIZE;
	fse->max_height = BRU_MAX_SIZE;

	return 0;
}

static struct v4l2_rect *bru_get_compose(struct vsp1_bru *bru,
					 struct v4l2_subdev_fh *fh,
					 unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &bru->inputs[pad].compose;
	default:
		return NULL;
	}
}

static int bru_get_format(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_bru *bru = to_bru(subdev);

	fmt->format = *vsp1_entity_get_pad_format(&bru->entity, fh, fmt->pad,
						  fmt->which);

	return 0;
}

static void bru_try_format(struct vsp1_bru *bru, struct v4l2_subdev_fh *fh,
			   unsigned int pad, struct v4l2_mbus_framefmt *fmt,
			   enum v4l2_subdev_format_whence which)
{
	struct v4l2_mbus_framefmt *format;

	switch (pad) {
	case BRU_PAD_SINK(0):
		/* Default to YUV if the requested format is not supported. */
		if (fmt->code != V4L2_MBUS_FMT_ARGB8888_1X32 &&
		    fmt->code != V4L2_MBUS_FMT_AYUV8_1X32)
			fmt->code = V4L2_MBUS_FMT_AYUV8_1X32;
		break;

	default:
		/* The BRU can't perform format conversion. */
		format = vsp1_entity_get_pad_format(&bru->entity, fh,
						    BRU_PAD_SINK(0), which);
		fmt->code = format->code;
		break;
	}

	fmt->width = clamp(fmt->width, BRU_MIN_SIZE, BRU_MAX_SIZE);
	fmt->height = clamp(fmt->height, BRU_MIN_SIZE, BRU_MAX_SIZE);
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
}

static int bru_set_format(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_bru *bru = to_bru(subdev);
	struct v4l2_mbus_framefmt *format;

	bru_try_format(bru, fh, fmt->pad, &fmt->format, fmt->which);

	format = vsp1_entity_get_pad_format(&bru->entity, fh, fmt->pad,
					    fmt->which);
	*format = fmt->format;

	/* Reset the compose rectangle */
	if (fmt->pad != BRU_PAD_SOURCE) {
		struct v4l2_rect *compose;

		compose = bru_get_compose(bru, fh, fmt->pad, fmt->which);
		compose->left = 0;
		compose->top = 0;
		compose->width = format->width;
		compose->height = format->height;
	}

	/* Propagate the format code to all pads */
	if (fmt->pad == BRU_PAD_SINK(0)) {
		unsigned int i;

		for (i = 0; i <= BRU_PAD_SOURCE; ++i) {
			format = vsp1_entity_get_pad_format(&bru->entity, fh,
							    i, fmt->which);
			format->code = fmt->format.code;
		}
	}

	return 0;
}

static int bru_get_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_bru *bru = to_bru(subdev);

	if (sel->pad == BRU_PAD_SOURCE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = BRU_MAX_SIZE;
		sel->r.height = BRU_MAX_SIZE;
		return 0;

	case V4L2_SEL_TGT_COMPOSE:
		sel->r = *bru_get_compose(bru, fh, sel->pad, sel->which);
		return 0;

	default:
		return -EINVAL;
	}
}

static int bru_set_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_bru *bru = to_bru(subdev);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *compose;

	if (sel->pad == BRU_PAD_SOURCE)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	/* The compose rectangle top left corner must be inside the output
	 * frame.
	 */
	format = vsp1_entity_get_pad_format(&bru->entity, fh, BRU_PAD_SOURCE,
					    sel->which);
	sel->r.left = clamp_t(unsigned int, sel->r.left, 0, format->width - 1);
	sel->r.top = clamp_t(unsigned int, sel->r.top, 0, format->height - 1);

	/* Scaling isn't supported, the compose rectangle size must be identical
	 * to the sink format size.
	 */
	format = vsp1_entity_get_pad_format(&bru->entity, fh, sel->pad,
					    sel->which);
	sel->r.width = format->width;
	sel->r.height = format->height;

	compose = bru_get_compose(bru, fh, sel->pad, sel->which);
	*compose = sel->r;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static struct v4l2_subdev_video_ops bru_video_ops = {
	.s_stream = bru_s_stream,
};

static struct v4l2_subdev_pad_ops bru_pad_ops = {
	.enum_mbus_code = bru_enum_mbus_code,
	.enum_frame_size = bru_enum_frame_size,
	.get_fmt = bru_get_format,
	.set_fmt = bru_set_format,
	.get_selection = bru_get_selection,
	.set_selection = bru_set_selection,
};

static struct v4l2_subdev_ops bru_ops = {
	.video	= &bru_video_ops,
	.pad    = &bru_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_bru *vsp1_bru_create(struct vsp1_device *vsp1)
{
	struct v4l2_subdev *subdev;
	struct vsp1_bru *bru;
	int ret;

	bru = devm_kzalloc(vsp1->dev, sizeof(*bru), GFP_KERNEL);
	if (bru == NULL)
		return ERR_PTR(-ENOMEM);

	bru->entity.type = VSP1_ENTITY_BRU;

	ret = vsp1_entity_init(vsp1, &bru->entity, 5);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the V4L2 subdev. */
	subdev = &bru->entity.subdev;
	v4l2_subdev_init(subdev, &bru_ops);

	subdev->entity.ops = &vsp1_media_ops;
	subdev->internal_ops = &vsp1_subdev_internal_ops;
	snprintf(subdev->name, sizeof(subdev->name), "%s bru",
		 dev_name(vsp1->dev));
	v4l2_set_subdevdata(subdev, bru);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	vsp1_entity_init_formats(subdev, NULL);

	return bru;
}
