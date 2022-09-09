/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_FORMATS_H
#define _DPU_FORMATS_H

#include <drm/drm_fourcc.h>
#include "msm_gem.h"
#include "dpu_hw_mdss.h"

/**
 * dpu_get_dpu_format_ext() - Returns dpu format structure pointer.
 * @format:          DRM FourCC Code
 * @modifiers:       format modifier array from client, one per plane
 */
const struct dpu_format *dpu_get_dpu_format_ext(
		const uint32_t format,
		const uint64_t modifier);

#define dpu_get_dpu_format(f) dpu_get_dpu_format_ext(f, 0)

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

/**
 * dpu_get_msm_format - get an dpu_format by its msm_format base
 *                     callback function registers with the msm_kms layer
 * @kms:             kms driver
 * @format:          DRM FourCC Code
 * @modifiers:       data layout modifier
 */
const struct msm_format *dpu_get_msm_format(
		struct msm_kms *kms,
		const uint32_t format,
		const uint64_t modifiers);

/**
 * dpu_format_check_modified_format - validate format and buffers for
 *                   dpu non-standard, i.e. modified format
 * @kms:             kms driver
 * @msm_fmt:         pointer to the msm_fmt base pointer of an dpu_format
 * @cmd:             fb_cmd2 structure user request
 * @bos:             gem buffer object list
 *
 * Return: error code on failure, 0 on success
 */
int dpu_format_check_modified_format(
		const struct msm_kms *kms,
		const struct msm_format *msm_fmt,
		const struct drm_mode_fb_cmd2 *cmd,
		struct drm_gem_object **bos);

/**
 * dpu_format_populate_layout - populate the given format layout based on
 *                     mmu, fb, and format found in the fb
 * @aspace:            address space pointer
 * @fb:                framebuffer pointer
 * @fmtl:              format layout structure to populate
 *
 * Return: error code on failure, -EAGAIN if success but the addresses
 *         are the same as before or 0 if new addresses were populated
 */
int dpu_format_populate_layout(
		struct msm_gem_address_space *aspace,
		struct drm_framebuffer *fb,
		struct dpu_hw_fmt_layout *fmtl);

#endif /*_DPU_FORMATS_H */
