/*
 * soc-camera generic scaling-cropping manipulation functions
 *
 * Copyright (C) 2013 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef SOC_SCALE_CROP_H
#define SOC_SCALE_CROP_H

#include <linux/kernel.h>

struct soc_camera_device;

struct v4l2_crop;
struct v4l2_mbus_framefmt;
struct v4l2_pix_format;
struct v4l2_rect;
struct v4l2_subdev;

static inline unsigned int soc_camera_shift_scale(unsigned int size,
				unsigned int shift, unsigned int scale)
{
	return DIV_ROUND_CLOSEST(size << shift, scale);
}

#define soc_camera_calc_scale(in, shift, out) soc_camera_shift_scale(in, shift, out)

int soc_camera_client_g_rect(struct v4l2_subdev *sd, struct v4l2_rect *rect);
int soc_camera_client_s_crop(struct v4l2_subdev *sd,
			struct v4l2_crop *crop, struct v4l2_crop *cam_crop,
			struct v4l2_rect *target_rect, struct v4l2_rect *subrect);
int soc_camera_client_scale(struct soc_camera_device *icd,
			struct v4l2_rect *rect, struct v4l2_rect *subrect,
			struct v4l2_mbus_framefmt *mf,
			unsigned int *width, unsigned int *height,
			bool host_can_scale, unsigned int shift);
void soc_camera_calc_client_output(struct soc_camera_device *icd,
		struct v4l2_rect *rect, struct v4l2_rect *subrect,
		const struct v4l2_pix_format *pix, struct v4l2_mbus_framefmt *mf,
		unsigned int shift);

#endif
