/*
 * vsp1_uds.h  --  R-Car VSP1 Up and Down Scaler
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
#ifndef __VSP1_UDS_H__
#define __VSP1_UDS_H__

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

#include "vsp1_entity.h"

struct vsp1_device;

#define UDS_PAD_SINK				0
#define UDS_PAD_SOURCE				1

struct vsp1_uds {
	struct vsp1_entity entity;

	unsigned int hscale;
	unsigned int vscale;
};

static inline struct vsp1_uds *to_uds(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_uds, entity.subdev);
}

struct vsp1_uds *vsp1_uds_create(struct vsp1_device *vsp1, unsigned int index);

#endif /* __VSP1_UDS_H__ */
