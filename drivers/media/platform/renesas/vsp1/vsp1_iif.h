/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vsp1_iif.h  --  R-Car VSP1 IIF (ISP Interface)
 *
 * Copyright (C) 2025 Ideas On Board Oy
 * Copyright (C) 2025 Renesas Corporation
 */
#ifndef __VSP1_IIF_H__
#define __VSP1_IIF_H__

#include <media/v4l2-subdev.h>

#include "vsp1_entity.h"

#define VSPX_IIF_SINK_PAD_IMG		0
#define VSPX_IIF_SINK_PAD_CONFIG	2

struct vsp1_iif {
	struct vsp1_entity entity;
};

static inline struct vsp1_iif *to_iif(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_iif, entity.subdev);
}

struct vsp1_iif *vsp1_iif_create(struct vsp1_device *vsp1);

#endif /* __VSP1_IIF_H__ */
