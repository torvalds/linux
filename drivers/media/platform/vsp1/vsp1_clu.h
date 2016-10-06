/*
 * vsp1_clu.h  --  R-Car VSP1 Cubic Look-Up Table
 *
 * Copyright (C) 2015 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_CLU_H__
#define __VSP1_CLU_H__

#include <linux/spinlock.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "vsp1_entity.h"

struct vsp1_device;
struct vsp1_dl_body;

#define CLU_PAD_SINK				0
#define CLU_PAD_SOURCE				1

struct vsp1_clu {
	struct vsp1_entity entity;

	struct v4l2_ctrl_handler ctrls;

	bool yuv_mode;
	spinlock_t lock;
	unsigned int mode;
	struct vsp1_dl_body *clu;
};

static inline struct vsp1_clu *to_clu(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_clu, entity.subdev);
}

struct vsp1_clu *vsp1_clu_create(struct vsp1_device *vsp1);

#endif /* __VSP1_CLU_H__ */
