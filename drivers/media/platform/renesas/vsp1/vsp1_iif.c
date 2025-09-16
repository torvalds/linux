// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_iif.c  --  R-Car VSP1 IIF (ISP Interface)
 *
 * Copyright (C) 2025 Ideas On Board Oy
 * Copyright (C) 2025 Renesas Corporation
 */

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_iif.h"

#define IIF_MIN_WIDTH				128U
#define IIF_MIN_HEIGHT				32U
#define IIF_MAX_WIDTH				5120U
#define IIF_MAX_HEIGHT				4096U

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_iif_write(struct vsp1_dl_body *dlb, u32 reg, u32 data)
{
	vsp1_dl_body_write(dlb, reg, data);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static const unsigned int iif_codes[] = {
	MEDIA_BUS_FMT_Y8_1X8,
	MEDIA_BUS_FMT_Y10_1X10,
	MEDIA_BUS_FMT_Y12_1X12,
	MEDIA_BUS_FMT_Y16_1X16,
	MEDIA_BUS_FMT_METADATA_FIXED
};

static int iif_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	return vsp1_subdev_enum_mbus_code(subdev, sd_state, code, iif_codes,
					  ARRAY_SIZE(iif_codes));
}

static int iif_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	return vsp1_subdev_enum_frame_size(subdev, sd_state, fse,
					   IIF_MIN_WIDTH, IIF_MIN_HEIGHT,
					   IIF_MAX_WIDTH, IIF_MAX_HEIGHT);
}

static int iif_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	return vsp1_subdev_set_pad_format(subdev, sd_state, fmt, iif_codes,
					  ARRAY_SIZE(iif_codes),
					  IIF_MIN_WIDTH, IIF_MIN_HEIGHT,
					  IIF_MAX_WIDTH, IIF_MAX_HEIGHT);
}

static const struct v4l2_subdev_pad_ops iif_pad_ops = {
	.enum_mbus_code = iif_enum_mbus_code,
	.enum_frame_size = iif_enum_frame_size,
	.get_fmt = vsp1_subdev_get_pad_format,
	.set_fmt = iif_set_format,
};

static const struct v4l2_subdev_ops iif_ops = {
	.pad    = &iif_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void iif_configure_stream(struct vsp1_entity *entity,
				 struct v4l2_subdev_state *state,
				 struct vsp1_pipeline *pipe,
				 struct vsp1_dl_list *dl,
				 struct vsp1_dl_body *dlb)
{
	vsp1_iif_write(dlb, VI6_IIF_CTRL, VI6_IIF_CTRL_CTRL);
}

static const struct vsp1_entity_operations iif_entity_ops = {
	.configure_stream = iif_configure_stream,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_iif *vsp1_iif_create(struct vsp1_device *vsp1)
{
	struct vsp1_iif *iif;
	int ret;

	iif = devm_kzalloc(vsp1->dev, sizeof(*iif), GFP_KERNEL);
	if (!iif)
		return ERR_PTR(-ENOMEM);

	iif->entity.ops = &iif_entity_ops;
	iif->entity.type = VSP1_ENTITY_IIF;

	/*
	 * The IIF is never exposed to userspace, but media entity registration
	 * requires a function to be set. Use PROC_VIDEO_PIXEL_FORMATTER just to
	 * avoid triggering a WARN_ON(), the value won't be seen anywhere.
	 */
	ret = vsp1_entity_init(vsp1, &iif->entity, "iif", 3, &iif_ops,
			       MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER);
	if (ret < 0)
		return ERR_PTR(ret);

	return iif;
}
