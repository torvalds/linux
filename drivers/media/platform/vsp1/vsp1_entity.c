/*
 * vsp1_entity.c  --  R-Car VSP1 Base Entity
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

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_entity.h"

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

struct v4l2_mbus_framefmt *
vsp1_entity_get_pad_format(struct vsp1_entity *entity,
			   struct v4l2_subdev_fh *fh,
			   unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &entity->formats[pad];
	default:
		return NULL;
	}
}

/*
 * vsp1_entity_init_formats - Initialize formats on all pads
 * @subdev: V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
void vsp1_entity_init_formats(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;
	unsigned int pad;

	for (pad = 0; pad < subdev->entity.num_pads - 1; ++pad) {
		memset(&format, 0, sizeof(format));

		format.pad = pad;
		format.which = fh ? V4L2_SUBDEV_FORMAT_TRY
			     : V4L2_SUBDEV_FORMAT_ACTIVE;

		v4l2_subdev_call(subdev, pad, set_fmt, fh, &format);
	}
}

static int vsp1_entity_open(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_fh *fh)
{
	vsp1_entity_init_formats(subdev, fh);

	return 0;
}

const struct v4l2_subdev_internal_ops vsp1_subdev_internal_ops = {
	.open = vsp1_entity_open,
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static int vsp1_entity_link_setup(struct media_entity *entity,
				  const struct media_pad *local,
				  const struct media_pad *remote, u32 flags)
{
	struct vsp1_entity *source;

	if (!(local->flags & MEDIA_PAD_FL_SOURCE))
		return 0;

	source = container_of(local->entity, struct vsp1_entity, subdev.entity);

	if (!source->route)
		return 0;

	if (flags & MEDIA_LNK_FL_ENABLED) {
		if (source->sink)
			return -EBUSY;
		source->sink = remote->entity;
	} else {
		source->sink = NULL;
	}

	return 0;
}

const struct media_entity_operations vsp1_media_ops = {
	.link_setup = vsp1_entity_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * Initialization
 */

int vsp1_entity_init(struct vsp1_device *vsp1, struct vsp1_entity *entity,
		     unsigned int num_pads)
{
	static const struct {
		unsigned int id;
		unsigned int reg;
	} routes[] = {
		{ VI6_DPR_NODE_LIF, 0 },
		{ VI6_DPR_NODE_RPF(0), VI6_DPR_RPF_ROUTE(0) },
		{ VI6_DPR_NODE_RPF(1), VI6_DPR_RPF_ROUTE(1) },
		{ VI6_DPR_NODE_RPF(2), VI6_DPR_RPF_ROUTE(2) },
		{ VI6_DPR_NODE_RPF(3), VI6_DPR_RPF_ROUTE(3) },
		{ VI6_DPR_NODE_RPF(4), VI6_DPR_RPF_ROUTE(4) },
		{ VI6_DPR_NODE_UDS(0), VI6_DPR_UDS_ROUTE(0) },
		{ VI6_DPR_NODE_UDS(1), VI6_DPR_UDS_ROUTE(1) },
		{ VI6_DPR_NODE_UDS(2), VI6_DPR_UDS_ROUTE(2) },
		{ VI6_DPR_NODE_WPF(0), 0 },
		{ VI6_DPR_NODE_WPF(1), 0 },
		{ VI6_DPR_NODE_WPF(2), 0 },
		{ VI6_DPR_NODE_WPF(3), 0 },
	};

	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(routes); ++i) {
		if (routes[i].id == entity->id) {
			entity->route = routes[i].reg;
			break;
		}
	}

	if (i == ARRAY_SIZE(routes))
		return -EINVAL;

	entity->vsp1 = vsp1;
	entity->source_pad = num_pads - 1;

	/* Allocate formats and pads. */
	entity->formats = devm_kzalloc(vsp1->dev,
				       num_pads * sizeof(*entity->formats),
				       GFP_KERNEL);
	if (entity->formats == NULL)
		return -ENOMEM;

	entity->pads = devm_kzalloc(vsp1->dev, num_pads * sizeof(*entity->pads),
				    GFP_KERNEL);
	if (entity->pads == NULL)
		return -ENOMEM;

	/* Initialize pads. */
	for (i = 0; i < num_pads - 1; ++i)
		entity->pads[i].flags = MEDIA_PAD_FL_SINK;

	entity->pads[num_pads - 1].flags = MEDIA_PAD_FL_SOURCE;

	/* Initialize the media entity. */
	return media_entity_init(&entity->subdev.entity, num_pads,
				 entity->pads, 0);
}

void vsp1_entity_destroy(struct vsp1_entity *entity)
{
	media_entity_cleanup(&entity->subdev.entity);
}
