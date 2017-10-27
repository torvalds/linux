/*
 * vsp1_hgt.h  --  R-Car VSP1 Histogram Generator 2D
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * Contact: Niklas Söderlund (niklas.soderlund@ragnatech.se)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_HGT_H__
#define __VSP1_HGT_H__

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "vsp1_histo.h"

struct vsp1_device;

#define HGT_NUM_HUE_AREAS			6

struct vsp1_hgt {
	struct vsp1_histogram histo;

	struct v4l2_ctrl_handler ctrls;

	u8 hue_areas[HGT_NUM_HUE_AREAS * 2];
};

static inline struct vsp1_hgt *to_hgt(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_hgt, histo.entity.subdev);
}

struct vsp1_hgt *vsp1_hgt_create(struct vsp1_device *vsp1);
void vsp1_hgt_frame_end(struct vsp1_entity *hgt);

#endif /* __VSP1_HGT_H__ */
