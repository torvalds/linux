// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - Common definitions
 *
 * Copyright (C) 2019 Collabora, Ltd.
 */

#include <media/v4l2-rect.h>

#include "rkisp1-common.h"

static const struct v4l2_rect rkisp1_sd_min_crop = {
	.width = RKISP1_ISP_MIN_WIDTH,
	.height = RKISP1_ISP_MIN_HEIGHT,
	.top = 0,
	.left = 0,
};

void rkisp1_sd_adjust_crop_rect(struct v4l2_rect *crop,
				const struct v4l2_rect *bounds)
{
	v4l2_rect_set_min_size(crop, &rkisp1_sd_min_crop);
	v4l2_rect_map_inside(crop, bounds);
}

void rkisp1_sd_adjust_crop(struct v4l2_rect *crop,
			   const struct v4l2_mbus_framefmt *bounds)
{
	struct v4l2_rect crop_bounds = {
		.left = 0,
		.top = 0,
		.width = bounds->width,
		.height = bounds->height,
	};

	rkisp1_sd_adjust_crop_rect(crop, &crop_bounds);
}
