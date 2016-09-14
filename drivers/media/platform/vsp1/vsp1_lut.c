/*
 * vsp1_lut.c  --  R-Car VSP1 Look-Up Table
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
#include <linux/vsp1.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_lut.h"

#define LUT_MIN_SIZE				4U
#define LUT_MAX_SIZE				8190U

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_lut_write(struct vsp1_lut *lut, struct vsp1_dl_list *dl,
				  u32 reg, u32 data)
{
	vsp1_dl_list_write(dl, reg, data);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Core Operations
 */

static int lut_set_table(struct vsp1_lut *lut, struct vsp1_lut_config *config)
{
	struct vsp1_dl_body *dlb;
	unsigned int i;

	dlb = vsp1_dl_fragment_alloc(lut->entity.vsp1, ARRAY_SIZE(config->lut));
	if (!dlb)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(config->lut); ++i)
		vsp1_dl_fragment_write(dlb, VI6_LUT_TABLE + 4 * i,
				       config->lut[i]);

	mutex_lock(&lut->lock);
	swap(lut->lut, dlb);
	mutex_unlock(&lut->lock);

	vsp1_dl_fragment_free(dlb);
	return 0;
}

static long lut_ioctl(struct v4l2_subdev *subdev, unsigned int cmd, void *arg)
{
	struct vsp1_lut *lut = to_lut(subdev);

	switch (cmd) {
	case VIDIOC_VSP1_LUT_CONFIG:
		return lut_set_table(lut, arg);

	default:
		return -ENOIOCTLCMD;
	}
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static int lut_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	static const unsigned int codes[] = {
		MEDIA_BUS_FMT_ARGB8888_1X32,
		MEDIA_BUS_FMT_AHSV8888_1X32,
		MEDIA_BUS_FMT_AYUV8_1X32,
	};

	return vsp1_subdev_enum_mbus_code(subdev, cfg, code, codes,
					  ARRAY_SIZE(codes));
}

static int lut_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	return vsp1_subdev_enum_frame_size(subdev, cfg, fse, LUT_MIN_SIZE,
					   LUT_MIN_SIZE, LUT_MAX_SIZE,
					   LUT_MAX_SIZE);
}

static int lut_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_lut *lut = to_lut(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;

	config = vsp1_entity_get_pad_config(&lut->entity, cfg, fmt->which);
	if (!config)
		return -EINVAL;

	/* Default to YUV if the requested format is not supported. */
	if (fmt->format.code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
	    fmt->format.code != MEDIA_BUS_FMT_AHSV8888_1X32 &&
	    fmt->format.code != MEDIA_BUS_FMT_AYUV8_1X32)
		fmt->format.code = MEDIA_BUS_FMT_AYUV8_1X32;

	format = vsp1_entity_get_pad_format(&lut->entity, config, fmt->pad);

	if (fmt->pad == LUT_PAD_SOURCE) {
		/* The LUT output format can't be modified. */
		fmt->format = *format;
		return 0;
	}

	format->code = fmt->format.code;
	format->width = clamp_t(unsigned int, fmt->format.width,
				LUT_MIN_SIZE, LUT_MAX_SIZE);
	format->height = clamp_t(unsigned int, fmt->format.height,
				 LUT_MIN_SIZE, LUT_MAX_SIZE);
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	fmt->format = *format;

	/* Propagate the format to the source pad. */
	format = vsp1_entity_get_pad_format(&lut->entity, config,
					    LUT_PAD_SOURCE);
	*format = fmt->format;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static struct v4l2_subdev_core_ops lut_core_ops = {
	.ioctl = lut_ioctl,
};

static struct v4l2_subdev_pad_ops lut_pad_ops = {
	.init_cfg = vsp1_entity_init_cfg,
	.enum_mbus_code = lut_enum_mbus_code,
	.enum_frame_size = lut_enum_frame_size,
	.get_fmt = vsp1_subdev_get_pad_format,
	.set_fmt = lut_set_format,
};

static struct v4l2_subdev_ops lut_ops = {
	.core	= &lut_core_ops,
	.pad    = &lut_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void lut_configure(struct vsp1_entity *entity,
			  struct vsp1_pipeline *pipe,
			  struct vsp1_dl_list *dl)
{
	struct vsp1_lut *lut = to_lut(&entity->subdev);

	vsp1_lut_write(lut, dl, VI6_LUT_CTRL, VI6_LUT_CTRL_EN);

	mutex_lock(&lut->lock);
	if (lut->lut) {
		vsp1_dl_list_add_fragment(dl, lut->lut);
		lut->lut = NULL;
	}
	mutex_unlock(&lut->lock);
}

static const struct vsp1_entity_operations lut_entity_ops = {
	.configure = lut_configure,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_lut *vsp1_lut_create(struct vsp1_device *vsp1)
{
	struct vsp1_lut *lut;
	int ret;

	lut = devm_kzalloc(vsp1->dev, sizeof(*lut), GFP_KERNEL);
	if (lut == NULL)
		return ERR_PTR(-ENOMEM);

	lut->entity.ops = &lut_entity_ops;
	lut->entity.type = VSP1_ENTITY_LUT;

	ret = vsp1_entity_init(vsp1, &lut->entity, "lut", 2, &lut_ops);
	if (ret < 0)
		return ERR_PTR(ret);

	return lut;
}
