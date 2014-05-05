/*
 * vsp1_entity.h  --  R-Car VSP1 Base Entity
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
#ifndef __VSP1_ENTITY_H__
#define __VSP1_ENTITY_H__

#include <linux/list.h>

#include <media/v4l2-subdev.h>

struct vsp1_device;

enum vsp1_entity_type {
	VSP1_ENTITY_HSI,
	VSP1_ENTITY_HST,
	VSP1_ENTITY_LIF,
	VSP1_ENTITY_LUT,
	VSP1_ENTITY_RPF,
	VSP1_ENTITY_SRU,
	VSP1_ENTITY_UDS,
	VSP1_ENTITY_WPF,
};

struct vsp1_entity {
	struct vsp1_device *vsp1;

	enum vsp1_entity_type type;
	unsigned int index;
	unsigned int id;
	unsigned int route;

	struct list_head list_dev;
	struct list_head list_pipe;

	struct media_pad *pads;
	unsigned int source_pad;

	struct media_entity *sink;

	struct v4l2_subdev subdev;
	struct v4l2_mbus_framefmt *formats;
};

static inline struct vsp1_entity *to_vsp1_entity(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_entity, subdev);
}

int vsp1_entity_init(struct vsp1_device *vsp1, struct vsp1_entity *entity,
		     unsigned int num_pads);
void vsp1_entity_destroy(struct vsp1_entity *entity);

extern const struct v4l2_subdev_internal_ops vsp1_subdev_internal_ops;
extern const struct media_entity_operations vsp1_media_ops;

struct v4l2_mbus_framefmt *
vsp1_entity_get_pad_format(struct vsp1_entity *entity,
			   struct v4l2_subdev_fh *fh,
			   unsigned int pad, u32 which);
void vsp1_entity_init_formats(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh);

#endif /* __VSP1_ENTITY_H__ */
