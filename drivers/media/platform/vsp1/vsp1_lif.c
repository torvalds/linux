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
#include "vsp1_dl.h"
#include "vsp1_lif.h"

#define LIF_MIN_SIZE				2U
#define LIF_MAX_SIZE				8190U

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_lif_write(struct vsp1_lif *lif, struct vsp1_dl_list *dl,
				  u32 reg, u32 data)
{
	vsp1_dl_list_write(dl, reg, data);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static int lif_enum_mbus_code(struct v4l2_subdev *subdev,
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

static int lif_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	return vsp1_subdev_enum_frame_size(subdev, cfg, fse, LIF_MIN_SIZE,
					   LIF_MIN_SIZE, LIF_MAX_SIZE,
					   LIF_MAX_SIZE);
}

static int lif_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_lif *lif = to_lif(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	int ret = 0;

	mutex_lock(&lif->entity.lock);

	config = vsp1_entity_get_pad_config(&lif->entity, cfg, fmt->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	/* Default to YUV if the requested format is not supported. */
	if (fmt->format.code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
	    fmt->format.code != MEDIA_BUS_FMT_AYUV8_1X32)
		fmt->format.code = MEDIA_BUS_FMT_AYUV8_1X32;

	format = vsp1_entity_get_pad_format(&lif->entity, config, fmt->pad);

	if (fmt->pad == LIF_PAD_SOURCE) {
		/* The LIF source format is always identical to its sink
		 * format.
		 */
		fmt->format = *format;
		goto done;
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
	format = vsp1_entity_get_pad_format(&lif->entity, config,
					    LIF_PAD_SOURCE);
	*format = fmt->format;

done:
	mutex_unlock(&lif->entity.lock);
	return ret;
}

static const struct v4l2_subdev_pad_ops lif_pad_ops = {
	.init_cfg = vsp1_entity_init_cfg,
	.enum_mbus_code = lif_enum_mbus_code,
	.enum_frame_size = lif_enum_frame_size,
	.get_fmt = vsp1_subdev_get_pad_format,
	.set_fmt = lif_set_format,
};

static const struct v4l2_subdev_ops lif_ops = {
	.pad    = &lif_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void lif_configure(struct vsp1_entity *entity,
			  struct vsp1_pipeline *pipe,
			  struct vsp1_dl_list *dl,
			  enum vsp1_entity_params params)
{
	const struct v4l2_mbus_framefmt *format;
	struct vsp1_lif *lif = to_lif(&entity->subdev);
	unsigned int hbth = 1300;
	unsigned int obth = 400;
	unsigned int lbth = 200;

	if (params != VSP1_ENTITY_PARAMS_INIT)
		return;

	format = vsp1_entity_get_pad_format(&lif->entity, lif->entity.config,
					    LIF_PAD_SOURCE);

	obth = min(obth, (format->width + 1) / 2 * format->height - 4);

	vsp1_lif_write(lif, dl, VI6_LIF_CSBTH,
			(hbth << VI6_LIF_CSBTH_HBTH_SHIFT) |
			(lbth << VI6_LIF_CSBTH_LBTH_SHIFT));

	vsp1_lif_write(lif, dl, VI6_LIF_CTRL,
			(obth << VI6_LIF_CTRL_OBTH_SHIFT) |
			(format->code == 0 ? VI6_LIF_CTRL_CFMT : 0) |
			VI6_LIF_CTRL_REQSEL | VI6_LIF_CTRL_LIF_EN);
}

static const struct vsp1_entity_operations lif_entity_ops = {
	.configure = lif_configure,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_lif *vsp1_lif_create(struct vsp1_device *vsp1)
{
	struct vsp1_lif *lif;
	int ret;

	lif = devm_kzalloc(vsp1->dev, sizeof(*lif), GFP_KERNEL);
	if (lif == NULL)
		return ERR_PTR(-ENOMEM);

	lif->entity.ops = &lif_entity_ops;
	lif->entity.type = VSP1_ENTITY_LIF;

	/* The LIF is never exposed to userspace, but media entity registration
	 * requires a function to be set. Use PROC_VIDEO_PIXEL_FORMATTER just to
	 * avoid triggering a WARN_ON(), the value won't be seen anywhere.
	 */
	ret = vsp1_entity_init(vsp1, &lif->entity, "lif", 2, &lif_ops,
			       MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER);
	if (ret < 0)
		return ERR_PTR(ret);

	return lif;
}
