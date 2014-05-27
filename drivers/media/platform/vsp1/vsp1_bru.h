/*
 * vsp1_bru.h  --  R-Car VSP1 Blend ROP Unit
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
#ifndef __VSP1_BRU_H__
#define __VSP1_BRU_H__

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

#include "vsp1_entity.h"

struct vsp1_device;
struct vsp1_rwpf;

#define BRU_PAD_SINK(n)				(n)
#define BRU_PAD_SOURCE				4

struct vsp1_bru {
	struct vsp1_entity entity;

	struct {
		struct vsp1_rwpf *rpf;
		struct v4l2_rect compose;
	} inputs[4];
};

static inline struct vsp1_bru *to_bru(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_bru, entity.subdev);
}

struct vsp1_bru *vsp1_bru_create(struct vsp1_device *vsp1);

#endif /* __VSP1_BRU_H__ */
