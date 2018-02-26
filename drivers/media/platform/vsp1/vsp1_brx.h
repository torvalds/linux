/*
 * vsp1_brx.h  --  R-Car VSP1 Blend ROP Unit (BRU and BRS)
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
#ifndef __VSP1_BRX_H__
#define __VSP1_BRX_H__

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "vsp1_entity.h"

struct vsp1_device;
struct vsp1_rwpf;

#define BRX_PAD_SINK(n)				(n)

struct vsp1_brx {
	struct vsp1_entity entity;
	unsigned int base;

	struct v4l2_ctrl_handler ctrls;

	struct {
		struct vsp1_rwpf *rpf;
	} inputs[VSP1_MAX_RPF];

	u32 bgcolor;
};

static inline struct vsp1_brx *to_brx(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_brx, entity.subdev);
}

struct vsp1_brx *vsp1_brx_create(struct vsp1_device *vsp1,
				 enum vsp1_entity_type type);

#endif /* __VSP1_BRX_H__ */
