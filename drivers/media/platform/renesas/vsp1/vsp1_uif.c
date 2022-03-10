// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_uif.c  --  R-Car VSP1 User Logic Interface
 *
 * Copyright (C) 2017-2018 Laurent Pinchart
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/sys_soc.h>

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_entity.h"
#include "vsp1_uif.h"

#define UIF_MIN_SIZE				4U
#define UIF_MAX_SIZE				8190U

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline u32 vsp1_uif_read(struct vsp1_uif *uif, u32 reg)
{
	return vsp1_read(uif->entity.vsp1,
			 uif->entity.index * VI6_UIF_OFFSET + reg);
}

static inline void vsp1_uif_write(struct vsp1_uif *uif,
				  struct vsp1_dl_body *dlb, u32 reg, u32 data)
{
	vsp1_dl_body_write(dlb, reg + uif->entity.index * VI6_UIF_OFFSET, data);
}

u32 vsp1_uif_get_crc(struct vsp1_uif *uif)
{
	return vsp1_uif_read(uif, VI6_UIF_DISCOM_DOCMCCRCR);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static const unsigned int uif_codes[] = {
	MEDIA_BUS_FMT_ARGB8888_1X32,
	MEDIA_BUS_FMT_AHSV8888_1X32,
	MEDIA_BUS_FMT_AYUV8_1X32,
};

static int uif_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	return vsp1_subdev_enum_mbus_code(subdev, sd_state, code, uif_codes,
					  ARRAY_SIZE(uif_codes));
}

static int uif_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	return vsp1_subdev_enum_frame_size(subdev, sd_state, fse,
					   UIF_MIN_SIZE,
					   UIF_MIN_SIZE, UIF_MAX_SIZE,
					   UIF_MAX_SIZE);
}

static int uif_set_format(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *fmt)
{
	return vsp1_subdev_set_pad_format(subdev, sd_state, fmt, uif_codes,
					  ARRAY_SIZE(uif_codes),
					  UIF_MIN_SIZE, UIF_MIN_SIZE,
					  UIF_MAX_SIZE, UIF_MAX_SIZE);
}

static int uif_get_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_uif *uif = to_uif(subdev);
	struct v4l2_subdev_state *config;
	struct v4l2_mbus_framefmt *format;
	int ret = 0;

	if (sel->pad != UIF_PAD_SINK)
		return -EINVAL;

	mutex_lock(&uif->entity.lock);

	config = vsp1_entity_get_pad_config(&uif->entity, sd_state,
					    sel->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		format = vsp1_entity_get_pad_format(&uif->entity, config,
						    UIF_PAD_SINK);
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = format->width;
		sel->r.height = format->height;
		break;

	case V4L2_SEL_TGT_CROP:
		sel->r = *vsp1_entity_get_pad_selection(&uif->entity, config,
							sel->pad, sel->target);
		break;

	default:
		ret = -EINVAL;
		break;
	}

done:
	mutex_unlock(&uif->entity.lock);
	return ret;
}

static int uif_set_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_uif *uif = to_uif(subdev);
	struct v4l2_subdev_state *config;
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *selection;
	int ret = 0;

	if (sel->pad != UIF_PAD_SINK ||
	    sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	mutex_lock(&uif->entity.lock);

	config = vsp1_entity_get_pad_config(&uif->entity, sd_state,
					    sel->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	/* The crop rectangle must be inside the input frame. */
	format = vsp1_entity_get_pad_format(&uif->entity, config, UIF_PAD_SINK);

	sel->r.left = clamp_t(unsigned int, sel->r.left, 0, format->width - 1);
	sel->r.top = clamp_t(unsigned int, sel->r.top, 0, format->height - 1);
	sel->r.width = clamp_t(unsigned int, sel->r.width, UIF_MIN_SIZE,
			       format->width - sel->r.left);
	sel->r.height = clamp_t(unsigned int, sel->r.height, UIF_MIN_SIZE,
				format->height - sel->r.top);

	/* Store the crop rectangle. */
	selection = vsp1_entity_get_pad_selection(&uif->entity, config,
						  sel->pad, V4L2_SEL_TGT_CROP);
	*selection = sel->r;

done:
	mutex_unlock(&uif->entity.lock);
	return ret;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static const struct v4l2_subdev_pad_ops uif_pad_ops = {
	.init_cfg = vsp1_entity_init_cfg,
	.enum_mbus_code = uif_enum_mbus_code,
	.enum_frame_size = uif_enum_frame_size,
	.get_fmt = vsp1_subdev_get_pad_format,
	.set_fmt = uif_set_format,
	.get_selection = uif_get_selection,
	.set_selection = uif_set_selection,
};

static const struct v4l2_subdev_ops uif_ops = {
	.pad    = &uif_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void uif_configure_stream(struct vsp1_entity *entity,
				 struct vsp1_pipeline *pipe,
				 struct vsp1_dl_list *dl,
				 struct vsp1_dl_body *dlb)
{
	struct vsp1_uif *uif = to_uif(&entity->subdev);
	const struct v4l2_rect *crop;
	unsigned int left;
	unsigned int width;

	vsp1_uif_write(uif, dlb, VI6_UIF_DISCOM_DOCMPMR,
		       VI6_UIF_DISCOM_DOCMPMR_SEL(9));

	crop = vsp1_entity_get_pad_selection(entity, entity->config,
					     UIF_PAD_SINK, V4L2_SEL_TGT_CROP);

	left = crop->left;
	width = crop->width;

	/* On M3-W the horizontal coordinates are twice the register value. */
	if (uif->m3w_quirk) {
		left /= 2;
		width /= 2;
	}

	vsp1_uif_write(uif, dlb, VI6_UIF_DISCOM_DOCMSPXR, left);
	vsp1_uif_write(uif, dlb, VI6_UIF_DISCOM_DOCMSPYR, crop->top);
	vsp1_uif_write(uif, dlb, VI6_UIF_DISCOM_DOCMSZXR, width);
	vsp1_uif_write(uif, dlb, VI6_UIF_DISCOM_DOCMSZYR, crop->height);

	vsp1_uif_write(uif, dlb, VI6_UIF_DISCOM_DOCMCR,
		       VI6_UIF_DISCOM_DOCMCR_CMPR);
}

static const struct vsp1_entity_operations uif_entity_ops = {
	.configure_stream = uif_configure_stream,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

static const struct soc_device_attribute vsp1_r8a7796[] = {
	{ .soc_id = "r8a7796" },
	{ /* sentinel */ }
};

struct vsp1_uif *vsp1_uif_create(struct vsp1_device *vsp1, unsigned int index)
{
	struct vsp1_uif *uif;
	char name[6];
	int ret;

	uif = devm_kzalloc(vsp1->dev, sizeof(*uif), GFP_KERNEL);
	if (!uif)
		return ERR_PTR(-ENOMEM);

	if (soc_device_match(vsp1_r8a7796))
		uif->m3w_quirk = true;

	uif->entity.ops = &uif_entity_ops;
	uif->entity.type = VSP1_ENTITY_UIF;
	uif->entity.index = index;

	/* The datasheet names the two UIF instances UIF4 and UIF5. */
	sprintf(name, "uif.%u", index + 4);
	ret = vsp1_entity_init(vsp1, &uif->entity, name, 2, &uif_ops,
			       MEDIA_ENT_F_PROC_VIDEO_STATISTICS);
	if (ret < 0)
		return ERR_PTR(ret);

	return uif;
}
