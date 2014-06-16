/*
 * Samsung S5P/EXYNOS4 SoC Camera Subsystem driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <media/exynos-fimc.h>
#include "common.h"

/* Called with the media graph mutex held or entity->stream_count > 0. */
struct v4l2_subdev *fimc_find_remote_sensor(struct media_entity *entity)
{
	struct media_pad *pad = &entity->pads[0];
	struct v4l2_subdev *sd;

	while (pad->flags & MEDIA_PAD_FL_SINK) {
		/* source pad */
		pad = media_entity_remote_pad(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		sd = media_entity_to_v4l2_subdev(pad->entity);

		if (sd->grp_id == GRP_ID_FIMC_IS_SENSOR ||
		    sd->grp_id == GRP_ID_SENSOR)
			return sd;
		/* sink pad */
		pad = &sd->entity.pads[0];
	}
	return NULL;
}
EXPORT_SYMBOL(fimc_find_remote_sensor);

void __fimc_vidioc_querycap(struct device *dev, struct v4l2_capability *cap,
						unsigned int caps)
{
	strlcpy(cap->driver, dev->driver->name, sizeof(cap->driver));
	strlcpy(cap->card, dev->driver->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
				"platform:%s", dev_name(dev));
	cap->device_caps = caps;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
}
EXPORT_SYMBOL(__fimc_vidioc_querycap);

MODULE_LICENSE("GPL");
