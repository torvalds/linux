/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_FORMATS_H
#define _DPU_FORMATS_H

#include <drm/drm_fourcc.h>
#include "msm_gem.h"
#include "dpu_hw_mdss.h"

/**
 * dpu_find_format - validate if the pixel format is supported
 * @format:		dpu format
 * @supported_formats:	supported formats by dpu HW
 * @num_formatss:	total number of formats
 *
 * Return: false if not valid format, true on success
 */
static inline bool dpu_find_format(u32 format, const u32 *supported_formats,
					size_t num_formats)
{
	int i;

	for (i = 0; i < num_formats; i++) {
		/* check for valid formats supported */
		if (format == supported_formats[i])
			return true;
	}

	return false;
}

void dpu_format_populate_addrs(struct drm_framebuffer *fb,
			       struct dpu_hw_fmt_layout *layout);

int dpu_format_populate_plane_sizes(
		struct drm_framebuffer *fb,
		struct dpu_hw_fmt_layout *layout);

#endif /*_DPU_FORMATS_H */
