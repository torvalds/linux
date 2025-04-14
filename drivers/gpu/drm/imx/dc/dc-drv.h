/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 */

#ifndef __DC_DRV_H__
#define __DC_DRV_H__

#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <drm/drm_device.h>

#include "dc-de.h"

/**
 * struct dc_drm_device - DC specific drm_device
 */
struct dc_drm_device {
	/** @base: base drm_device structure */
	struct drm_device base;
	/** @de: display engine list */
	struct dc_de *de[DC_DISPLAYS];
	/** @fg: framegen list */
	struct dc_fg *fg[DC_DISPLAYS];
	/** @tc: tcon list */
	struct dc_tc *tc[DC_DISPLAYS];
};

struct dc_subdev_info {
	resource_size_t reg_start;
	int id;
};

extern struct platform_driver dc_de_driver;
extern struct platform_driver dc_fg_driver;
extern struct platform_driver dc_tc_driver;

static inline int dc_subdev_get_id(const struct dc_subdev_info *info,
				   int info_cnt, struct resource *res)
{
	int i;

	if (!res)
		return -EINVAL;

	for (i = 0; i < info_cnt; i++)
		if (info[i].reg_start == res->start)
			return info[i].id;

	return -EINVAL;
}

void dc_de_post_bind(struct dc_drm_device *dc_drm);

#endif /* __DC_DRV_H__ */
