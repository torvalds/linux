/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vsp1_uif.h  --  R-Car VSP1 User Logic Interface
 *
 * Copyright (C) 2017-2018 Laurent Pinchart
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */
#ifndef __VSP1_UIF_H__
#define __VSP1_UIF_H__

#include "vsp1_entity.h"

struct vsp1_device;

#define UIF_PAD_SINK				0
#define UIF_PAD_SOURCE				1

struct vsp1_uif {
	struct vsp1_entity entity;
	bool m3w_quirk;
};

static inline struct vsp1_uif *to_uif(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_uif, entity.subdev);
}

struct vsp1_uif *vsp1_uif_create(struct vsp1_device *vsp1, unsigned int index);
u32 vsp1_uif_get_crc(struct vsp1_uif *uif);

#endif /* __VSP1_UIF_H__ */
