/*
 * vsp1_lif.c  --  R-Car VSP1 LCD Controller Interface
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
#include <linux/gfp.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_lif.h"

#define LIF_MIN_SIZE				2U
#define LIF_MAX_SIZE				2048U

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_lif_write(struct vsp1_lif *lif, u32 reg, u32 data)
{
	vsp1_mod_write(&lif->entity, reg, data);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Core Operations
 */

static int lif_s_stream(struct v4l2_subdev *subdev, int enable)
{
	const struct v4l2_mbus_framefmt *format;
	struct vsp1_lif *lif = to_lif(subdev);
	unsigned int hbth = 1300;
	unsigned int obth = 400;
	unsigned int lbth = 200;

	if (!enable) {
		vsp1_write(lif->entity.vsp1, VI6_LIF_CTRL, 0);
		return 0;
	}

	format = &lif->entity.formats[LIF_PAD_SOURCE];

	obth = min(obth, (format->width + 1) / 2 * format->height - 4);

	vsp1_lif_write(lif, VI6_LIF_CSBTH,
			(hbth << VI6_LIF_CSBTH_HBTH_SHIFT) |
			(lbth << VI6_LIF_CSBTH_LBTH_SHIFT));

	vsp1_lif_write(lif, VI6_LIF_CTRL,
			(obth << VI6_LIF_CTRL_OBTH_SHIFT) |
			(format->code == 0 ? VI6_LIF_CTRL_CFMT : 0) |
			VI6_LIF_CTRL_REQSEL | VI6_LIF_CTRL_LIF_EN);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static int lif_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	static const unsigned int codes[] = {
		MEDIA_BUS_FMT_ARGB8888_1X32,
		MEDIA_BUS_FMT_AYUV8_1X32,
	};
	struct vsp1_lif *lif = to_lif(subdev);

	if (code->pad == LIF_PAD_SINK) {
		if (code->index >= ARRAY_SIZE(codes))
			return -EINVAL;

		code->code = codes[code->index];
	} else {
		struct v4l2_mbus_framefmt *format;

		/* The LIF can't perform format conversion, the sink format is
		 * always identical to the source format.
		 */
		if (code->index)
			return -EINVAL;

		format = vsp1_entity_get_pad_format(&lif->entity, cfg,
						    LIF_PAD_SINK, code->which);
		code->code = format->code;
	}

	return 0;
}

static int lif_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	struct vsp1_lif *lif = to_lif(subdev);
	struct v4l2_mbus_framefmt *format;

	format = vsp1_entity_get_pad_format(&lif->entity, cfg, LIF_PAD_SINK,
					    fse->which);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	if (fse->pad == LIF_PAD_SINK) {
		fse->min_width = LIF_MIN_SIZE;
		fse->max_width = LIF_MAX_SIZE;
		fse->min_height = LIF_MIN_SIZE;
		fse->max_height = LIF_MAX_SIZE;
	} else {
		fse->min_width = format->width;
		fse->max_width = format->width;
		fse->min_height = format->height;
		fse->max_height = format->height;
	}

	return 0;
}

static int lif_get_format(struct v4l2_subdev *subdev, struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_lif *lif = to_lif(subdev);

	fmt->format = *vsp1_entity_get_pad_format(&lif->entity, cfg, fmt->pad,
						  fmt->which);

	return 0;
}

static int lif_set_format(struct v4l2_subdev *subdev, struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_lif *lif = to_lif(subdev);
	struct v4l2_mbus_framefmt *format;

	/* Default to YUV if the requested format is not supported. */
	if (fmt->format.code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
	    fmt->format.code != MEDIA_BUS_FMT_AYUV8_1X32)
		fmt->format.code = MEDIA_BUS_FMT_AYUV8_1X32;

	format = vsp1_entity_get_pad_format(&lif->entity, cfg, fmt->pad,
					    fmt->which);

	if (fmt->pad == LIF_PAD_SOURCE) {
		/* The LIF source format is always identical to its sink
		 * format.
		 */
		fmt->format = *format;
		return 0;
	}

	format->code = fmt->format.code;
	format->width = clamp_t(unsigned int, fmt->format.width,
				LIF_MIN_SIZE, LIF_MAX_SIZE);
	format->height = clamp_t(unsigned int, fmt->format.height,
				 LIF_MIN_SIZE, LIF_MAX_SIZE);
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	fmt->format = *format;

	/* Propagate the format to the source pad. */
	format = vsp1_entity_get_pad_format(&lif->entity, cfg, LIF_PAD_SOURCE,
					    fmt->which);
	*format = fmt->format;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static struct v4l2_subdev_video_ops lif_video_ops = {
	.s_stream = lif_s_stream,
};

static struct v4l2_subdev_pad_ops lif_pad_ops = {
	.enum_mbus_code = lif_enum_mbus_code,
	.enum_frame_size = lif_enum_frame_size,
	.get_fmt = lif_get_format,
	.set_fmt = lif_set_format,
};

static struct v4l2_subdev_ops lif_ops = {
	.video	= &lif_video_ops,
	.pad    = &lif_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_lif *vsp1_lif_create(struct vsp1_device *vsp1)
{
	struct v4l2_subdev *subdev;
	struct vsp1_lif *lif;
	int ret;

	lif = devm_kzalloc(vsp1->dev, sizeof(*lif), GFP_KERNEL);
	if (lif == NULL)
		return ERR_PTR(-ENOMEM);

	lif->entity.type = VSP1_ENTITY_LIF;

	ret = vsp1_entity_init(vsp1, &lif->entity, 2);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the V4L2 subdev. */
	subdev = &lif->entity.subdev;
	v4l2_subdev_init(subdev, &lif_ops);

	subdev->entity.ops = &vsp1->media_ops;
	subdev->internal_ops = &vsp1_subdev_internal_ops;
	snprintf(subdev->name, sizeof(subdev->name), "%s lif",
		 dev_name(vsp1->dev));
	v4l2_set_subdevdata(subdev, lif);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	vsp1_entity_init_formats(subdev, NULL);

	return lif;
}
