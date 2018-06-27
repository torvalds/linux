/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
 * dpu_populate_formats - populate the given array with fourcc codes supported
 * @format_list:       pointer to list of possible formats
 * @pixel_formats:     array to populate with fourcc codes
 * @pixel_modifiers:   array to populate with drm modifiers, can be NULL
 * @pixel_formats_max: length of pixel formats array
 * Return: number of elements populated
 */
uint32_t dpu_populate_formats(
		const struct dpu_format_extended *format_list,
		uint32_t *pixel_formats,
		uint64_t *pixel_modifiers,
		uint32_t pixel_formats_max);

/**
 * dpu_format_get_plane_sizes - calculate size and layout of given buffer format
 * @fmt:             pointer to dpu_format
 * @w:               width of the buffer
 * @h:               height of the buffer
 * @layout:          layout of the buffer
 * @pitches:         array of size [DPU_MAX_PLANES] to populate
 *		     pitch for each plane
 *
 * Return: size of the buffer
 */
int dpu_format_get_plane_sizes(
		const struct dpu_format *fmt,
		const uint32_t w,
		const uint32_t h,
		struct dpu_hw_fmt_layout *layout,
		const uint32_t *pitches);

/**
 * dpu_format_get_block_size - get block size of given format when
 *	operating in block mode
 * @fmt:             pointer to dpu_format
 * @w:               pointer to width of the block
 * @h:               pointer to height of the block
 *
 * Return: 0 if success; error oode otherwise
 */
int dpu_format_get_block_size(const struct dpu_format *fmt,
		uint32_t *w, uint32_t *h);

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

/**
 * dpu_format_get_framebuffer_size - get framebuffer memory size
 * @format:            DRM pixel format
 * @width:             pixel width
 * @height:            pixel height
 * @pitches:           array of size [DPU_MAX_PLANES] to populate
 *		       pitch for each plane
 * @modifiers:         drm modifier
 *
 * Return: memory size required for frame buffer
 */
uint32_t dpu_format_get_framebuffer_size(
		const uint32_t format,
		const uint32_t width,
		const uint32_t height,
		const uint32_t *pitches,
		const uint64_t modifiers);

#endif /*_DPU_FORMATS_H */
