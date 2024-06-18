/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 */

#include <linux/device.h>
#include <linux/videodev2.h>
#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

struct v4l2_subdev *fimc_find_remote_sensor(struct media_entity *entity);
void __fimc_vidioc_querycap(struct device *dev, struct v4l2_capability *cap);
