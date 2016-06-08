/*
 * vsp1_entity.c  --  R-Car VSP1 Base Entity
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

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_entity.h"

void vsp1_entity_route_setup(struct vsp1_entity *source,
			     struct vsp1_dl_list *dl)
{
	struct vsp1_entity *sink;

	if (source->route->reg == 0)
		return;

	sink = container_of(source->sink, struct vsp1_entity, subdev.entity);
	vsp1_dl_list_write(dl, source->route->reg,
			   sink->route->inputs[source->sink_pad]);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

/**
 * vsp1_entity_get_pad_config - Get the pad configuration for an entity
 * @entity: the entity
 * @cfg: the TRY pad configuration
 * @which: configuration selector (ACTIVE or TRY)
 *
 * Return the pad configuration requested by the which argument. The TRY
 * configuration is passed explicitly to the function through the cfg argument
 * and simply returned when requested. The ACTIVE configuration comes from the
 * entity structure.
 */
struct v4l2_subdev_pad_config *
vsp1_entity_get_pad_config(struct vsp1_entity *entity,
			   struct v4l2_subdev_pad_config *cfg,
			   enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return entity->config;
	case V4L2_SUBDEV_FORMAT_TRY:
	default:
		return cfg;
	}
}

/**
 * vsp1_entity_get_pad_format - Get a pad format from storage for an entity
 * @entity: the entity
 * @cfg: the configuration storage
 * @pad: the pad number
 *
 * Return the format stored in the given configuration for an entity's pad. The
 * configuration can be an ACTIVE or TRY configuration.
 */
struct v4l2_mbus_framefmt *
vsp1_entity_get_pad_format(struct vsp1_entity *entity,
			   struct v4l2_subdev_pad_config *cfg,
			   unsigned int pad)
{
	return v4l2_subdev_get_try_format(&entity->subdev, cfg, pad);
}

struct v4l2_rect *
vsp1_entity_get_pad_compose(struct vsp1_entity *entity,
			    struct v4l2_subdev_pad_config *cfg,
			    unsigned int pad)
{
	return v4l2_subdev_get_try_compose(&entity->subdev, cfg, pad);
}

/*
 * vsp1_entity_init_cfg - Initialize formats on all pads
 * @subdev: V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 *
 * Initialize all pad formats with default values in the given pad config. This
 * function can be used as a handler for the subdev pad::init_cfg operation.
 */
int vsp1_entity_init_cfg(struct v4l2_subdev *subdev,
			 struct v4l2_subdev_pad_config *cfg)
{
	struct v4l2_subdev_format format;
	unsigned int pad;

	for (pad = 0; pad < subdev->entity.num_pads - 1; ++pad) {
		memset(&format, 0, sizeof(format));

		format.pad = pad;
		format.which = cfg ? V4L2_SUBDEV_FORMAT_TRY
			     : V4L2_SUBDEV_FORMAT_ACTIVE;

		v4l2_subdev_call(subdev, pad, set_fmt, cfg, &format);
	}

	return 0;
}

/*
 * vsp1_subdev_get_pad_format - Subdev pad get_fmt handler
 * @subdev: V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @fmt: V4L2 subdev format
 *
 * This function implements the subdev get_fmt pad operation. It can be used as
 * a direct drop-in for the operation handler.
 */
int vsp1_subdev_get_pad_format(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_format *fmt)
{
	struct vsp1_entity *entity = to_vsp1_entity(subdev);
	struct v4l2_subdev_pad_config *config;

	config = vsp1_entity_get_pad_config(entity, cfg, fmt->which);
	if (!config)
		return -EINVAL;

	fmt->format = *vsp1_entity_get_pad_format(entity, config, fmt->pad);

	return 0;
}

/*
 * vsp1_subdev_enum_mbus_code - Subdev pad enum_mbus_code handler
 * @subdev: V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @code: Media bus code enumeration
 * @codes: Array of supported media bus codes
 * @ncodes: Number of supported media bus codes
 *
 * This function implements the subdev enum_mbus_code pad operation for entities
 * that do not support format conversion. It enumerates the given supported
 * media bus codes on the sink pad and reports a source pad format identical to
 * the sink pad.
 */
int vsp1_subdev_enum_mbus_code(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_mbus_code_enum *code,
			       const unsigned int *codes, unsigned int ncodes)
{
	struct vsp1_entity *entity = to_vsp1_entity(subdev);

	if (code->pad == 0) {
		if (code->index >= ncodes)
			return -EINVAL;

		code->code = codes[code->index];
	} else {
		struct v4l2_subdev_pad_config *config;
		struct v4l2_mbus_framefmt *format;

		/* The entity can't perform format conversion, the sink format
		 * is always identical to the source format.
		 */
		if (code->index)
			return -EINVAL;

		config = vsp1_entity_get_pad_config(entity, cfg, code->which);
		if (!config)
			return -EINVAL;

		format = vsp1_entity_get_pad_format(entity, config, 0);
		code->code = format->code;
	}

	return 0;
}

/*
 * vsp1_subdev_enum_frame_size - Subdev pad enum_frame_size handler
 * @subdev: V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @fse: Frame size enumeration
 * @min_width: Minimum image width
 * @min_height: Minimum image height
 * @max_width: Maximum image width
 * @max_height: Maximum image height
 *
 * This function implements the subdev enum_frame_size pad operation for
 * entities that do not support scaling or cropping. It reports the given
 * minimum and maximum frame width and height on the sink pad, and a fixed
 * source pad size identical to the sink pad.
 */
int vsp1_subdev_enum_frame_size(struct v4l2_subdev *subdev,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_size_enum *fse,
				unsigned int min_width, unsigned int min_height,
				unsigned int max_width, unsigned int max_height)
{
	struct vsp1_entity *entity = to_vsp1_entity(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;

	config = vsp1_entity_get_pad_config(entity, cfg, fse->which);
	if (!config)
		return -EINVAL;

	format = vsp1_entity_get_pad_format(entity, config, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	if (fse->pad == 0) {
		fse->min_width = min_width;
		fse->max_width = max_width;
		fse->min_height = min_height;
		fse->max_height = max_height;
	} else {
		/* The size on the source pad are fixed and always identical to
		 * the size on the sink pad.
		 */
		fse->min_width = format->width;
		fse->max_width = format->width;
		fse->min_height = format->height;
		fse->max_height = format->height;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Media Operations
 */

int vsp1_entity_link_setup(struct media_entity *entity,
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
		source->sink_pad = remote->index;
	} else {
		source->sink = NULL;
		source->sink_pad = 0;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Initialization
 */

static const struct vsp1_route vsp1_routes[] = {
	{ VSP1_ENTITY_BRU, 0, VI6_DPR_BRU_ROUTE,
	  { VI6_DPR_NODE_BRU_IN(0), VI6_DPR_NODE_BRU_IN(1),
	    VI6_DPR_NODE_BRU_IN(2), VI6_DPR_NODE_BRU_IN(3),
	    VI6_DPR_NODE_BRU_IN(4) } },
	{ VSP1_ENTITY_HSI, 0, VI6_DPR_HSI_ROUTE, { VI6_DPR_NODE_HSI, } },
	{ VSP1_ENTITY_HST, 0, VI6_DPR_HST_ROUTE, { VI6_DPR_NODE_HST, } },
	{ VSP1_ENTITY_LIF, 0, 0, { VI6_DPR_NODE_LIF, } },
	{ VSP1_ENTITY_LUT, 0, VI6_DPR_LUT_ROUTE, { VI6_DPR_NODE_LUT, } },
	{ VSP1_ENTITY_RPF, 0, VI6_DPR_RPF_ROUTE(0), { 0, } },
	{ VSP1_ENTITY_RPF, 1, VI6_DPR_RPF_ROUTE(1), { 0, } },
	{ VSP1_ENTITY_RPF, 2, VI6_DPR_RPF_ROUTE(2), { 0, } },
	{ VSP1_ENTITY_RPF, 3, VI6_DPR_RPF_ROUTE(3), { 0, } },
	{ VSP1_ENTITY_RPF, 4, VI6_DPR_RPF_ROUTE(4), { 0, } },
	{ VSP1_ENTITY_SRU, 0, VI6_DPR_SRU_ROUTE, { VI6_DPR_NODE_SRU, } },
	{ VSP1_ENTITY_UDS, 0, VI6_DPR_UDS_ROUTE(0), { VI6_DPR_NODE_UDS(0), } },
	{ VSP1_ENTITY_UDS, 1, VI6_DPR_UDS_ROUTE(1), { VI6_DPR_NODE_UDS(1), } },
	{ VSP1_ENTITY_UDS, 2, VI6_DPR_UDS_ROUTE(2), { VI6_DPR_NODE_UDS(2), } },
	{ VSP1_ENTITY_WPF, 0, 0, { VI6_DPR_NODE_WPF(0), } },
	{ VSP1_ENTITY_WPF, 1, 0, { VI6_DPR_NODE_WPF(1), } },
	{ VSP1_ENTITY_WPF, 2, 0, { VI6_DPR_NODE_WPF(2), } },
	{ VSP1_ENTITY_WPF, 3, 0, { VI6_DPR_NODE_WPF(3), } },
};

int vsp1_entity_init(struct vsp1_device *vsp1, struct vsp1_entity *entity,
		     const char *name, unsigned int num_pads,
		     const struct v4l2_subdev_ops *ops)
{
	struct v4l2_subdev *subdev;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(vsp1_routes); ++i) {
		if (vsp1_routes[i].type == entity->type &&
		    vsp1_routes[i].index == entity->index) {
			entity->route = &vsp1_routes[i];
			break;
		}
	}

	if (i == ARRAY_SIZE(vsp1_routes))
		return -EINVAL;

	entity->vsp1 = vsp1;
	entity->source_pad = num_pads - 1;

	/* Allocate and initialize pads. */
	entity->pads = devm_kzalloc(vsp1->dev, num_pads * sizeof(*entity->pads),
				    GFP_KERNEL);
	if (entity->pads == NULL)
		return -ENOMEM;

	for (i = 0; i < num_pads - 1; ++i)
		entity->pads[i].flags = MEDIA_PAD_FL_SINK;

	entity->pads[num_pads - 1].flags = MEDIA_PAD_FL_SOURCE;

	/* Initialize the media entity. */
	ret = media_entity_pads_init(&entity->subdev.entity, num_pads,
				     entity->pads);
	if (ret < 0)
		return ret;

	/* Initialize the V4L2 subdev. */
	subdev = &entity->subdev;
	v4l2_subdev_init(subdev, ops);

	subdev->entity.ops = &vsp1->media_ops;
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	snprintf(subdev->name, sizeof(subdev->name), "%s %s",
		 dev_name(vsp1->dev), name);

	vsp1_entity_init_cfg(subdev, NULL);

	/* Allocate the pad configuration to store formats and selection
	 * rectangles.
	 */
	entity->config = v4l2_subdev_alloc_pad_config(&entity->subdev);
	if (entity->config == NULL) {
		media_entity_cleanup(&entity->subdev.entity);
		return -ENOMEM;
	}

	return 0;
}

void vsp1_entity_destroy(struct vsp1_entity *entity)
{
	if (entity->ops && entity->ops->destroy)
		entity->ops->destroy(entity);
	if (entity->subdev.ctrl_handler)
		v4l2_ctrl_handler_free(entity->subdev.ctrl_handler);
	v4l2_subdev_free_pad_config(entity->config);
	media_entity_cleanup(&entity->subdev.entity);
}
