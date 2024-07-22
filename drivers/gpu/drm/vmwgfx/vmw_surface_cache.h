/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**********************************************************
 *
 * Copyright (c) 2021-2024 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#ifndef VMW_SURFACE_CACHE_H
#define VMW_SURFACE_CACHE_H

#include "device_include/svga3d_surfacedefs.h"

#include <drm/vmwgfx_drm.h>

#define SVGA3D_FLAGS_UPPER_32(svga3d_flags) ((svga3d_flags) >> 32)
#define SVGA3D_FLAGS_LOWER_32(svga3d_flags) \
	((svga3d_flags) & ((uint64_t)U32_MAX))

static inline u32 clamped_umul32(u32 a, u32 b)
{
	uint64_t tmp = (uint64_t) a*b;
	return (tmp > (uint64_t) ((u32) -1)) ? (u32) -1 : tmp;
}

/**
 * vmw_surface_get_desc - Look up the appropriate SVGA3dSurfaceDesc for the
 * given format.
 */
static inline const SVGA3dSurfaceDesc *
vmw_surface_get_desc(SVGA3dSurfaceFormat format)
{
	if (format < ARRAY_SIZE(g_SVGA3dSurfaceDescs))
		return &g_SVGA3dSurfaceDescs[format];

	return &g_SVGA3dSurfaceDescs[SVGA3D_FORMAT_INVALID];
}

/**
 * vmw_surface_get_mip_size -  Given a base level size and the mip level,
 * compute the size of the mip level.
 */
static inline struct drm_vmw_size
vmw_surface_get_mip_size(struct drm_vmw_size base_level, u32 mip_level)
{
	struct drm_vmw_size size = {
		.width = max_t(u32, base_level.width >> mip_level, 1),
		.height = max_t(u32, base_level.height >> mip_level, 1),
		.depth = max_t(u32, base_level.depth >> mip_level, 1)
	};

	return size;
}

static inline void
vmw_surface_get_size_in_blocks(const SVGA3dSurfaceDesc *desc,
				 const struct drm_vmw_size *pixel_size,
				 SVGA3dSize *block_size)
{
	block_size->width = __KERNEL_DIV_ROUND_UP(pixel_size->width,
						  desc->blockSize.width);
	block_size->height = __KERNEL_DIV_ROUND_UP(pixel_size->height,
						   desc->blockSize.height);
	block_size->depth = __KERNEL_DIV_ROUND_UP(pixel_size->depth,
						  desc->blockSize.depth);
}

static inline bool
vmw_surface_is_planar_surface(const SVGA3dSurfaceDesc *desc)
{
	return (desc->blockDesc & SVGA3DBLOCKDESC_PLANAR_YUV) != 0;
}

static inline u32
vmw_surface_calculate_pitch(const SVGA3dSurfaceDesc *desc,
			      const struct drm_vmw_size *size)
{
	u32 pitch;
	SVGA3dSize blocks;

	vmw_surface_get_size_in_blocks(desc, size, &blocks);

	pitch = blocks.width * desc->pitchBytesPerBlock;

	return pitch;
}

/**
 * vmw_surface_get_image_buffer_size - Calculates image buffer size.
 *
 * Return the number of bytes of buffer space required to store one image of a
 * surface, optionally using the specified pitch.
 *
 * If pitch is zero, it is assumed that rows are tightly packed.
 *
 * This function is overflow-safe. If the result would have overflowed, instead
 * we return MAX_UINT32.
 */
static inline u32
vmw_surface_get_image_buffer_size(const SVGA3dSurfaceDesc *desc,
				    const struct drm_vmw_size *size,
				    u32 pitch)
{
	SVGA3dSize image_blocks;
	u32 slice_size, total_size;

	vmw_surface_get_size_in_blocks(desc, size, &image_blocks);

	if (vmw_surface_is_planar_surface(desc)) {
		total_size = clamped_umul32(image_blocks.width,
					    image_blocks.height);
		total_size = clamped_umul32(total_size, image_blocks.depth);
		total_size = clamped_umul32(total_size, desc->bytesPerBlock);
		return total_size;
	}

	if (pitch == 0)
		pitch = vmw_surface_calculate_pitch(desc, size);

	slice_size = clamped_umul32(image_blocks.height, pitch);
	total_size = clamped_umul32(slice_size, image_blocks.depth);

	return total_size;
}

/**
 * vmw_surface_get_serialized_size - Get the serialized size for the image.
 */
static inline u32
vmw_surface_get_serialized_size(SVGA3dSurfaceFormat format,
				  struct drm_vmw_size base_level_size,
				  u32 num_mip_levels,
				  u32 num_layers)
{
	const SVGA3dSurfaceDesc *desc = vmw_surface_get_desc(format);
	u32 total_size = 0;
	u32 mip;

	for (mip = 0; mip < num_mip_levels; mip++) {
		struct drm_vmw_size size =
			vmw_surface_get_mip_size(base_level_size, mip);
		total_size += vmw_surface_get_image_buffer_size(desc,
								  &size, 0);
	}

	return total_size * num_layers;
}

/**
 * vmw_surface_get_serialized_size_extended - Returns the number of bytes
 * required for a surface with given parameters. Support for sample count.
 */
static inline u32
vmw_surface_get_serialized_size_extended(SVGA3dSurfaceFormat format,
					   struct drm_vmw_size base_level_size,
					   u32 num_mip_levels,
					   u32 num_layers,
					   u32 num_samples)
{
	uint64_t total_size =
		vmw_surface_get_serialized_size(format,
						  base_level_size,
						  num_mip_levels,
						  num_layers);
	total_size *= max_t(u32, 1, num_samples);

	return min_t(uint64_t, total_size, (uint64_t)U32_MAX);
}

/**
 * vmw_surface_get_pixel_offset - Compute the offset (in bytes) to a pixel
 * in an image (or volume).
 *
 * @width: The image width in pixels.
 * @height: The image height in pixels
 */
static inline u32
vmw_surface_get_pixel_offset(SVGA3dSurfaceFormat format,
			       u32 width, u32 height,
			       u32 x, u32 y, u32 z)
{
	const SVGA3dSurfaceDesc *desc = vmw_surface_get_desc(format);
	const u32 bw = desc->blockSize.width, bh = desc->blockSize.height;
	const u32 bd = desc->blockSize.depth;
	const u32 rowstride = __KERNEL_DIV_ROUND_UP(width, bw) *
			      desc->bytesPerBlock;
	const u32 imgstride = __KERNEL_DIV_ROUND_UP(height, bh) * rowstride;
	const u32 offset = (z / bd * imgstride +
			    y / bh * rowstride +
			    x / bw * desc->bytesPerBlock);
	return offset;
}

static inline u32
vmw_surface_get_image_offset(SVGA3dSurfaceFormat format,
			       struct drm_vmw_size baseLevelSize,
			       u32 numMipLevels,
			       u32 face,
			       u32 mip)

{
	u32 offset;
	u32 mipChainBytes;
	u32 mipChainBytesToLevel;
	u32 i;
	const SVGA3dSurfaceDesc *desc;
	struct drm_vmw_size mipSize;
	u32 bytes;

	desc = vmw_surface_get_desc(format);

	mipChainBytes = 0;
	mipChainBytesToLevel = 0;
	for (i = 0; i < numMipLevels; i++) {
		mipSize = vmw_surface_get_mip_size(baseLevelSize, i);
		bytes = vmw_surface_get_image_buffer_size(desc, &mipSize, 0);
		mipChainBytes += bytes;
		if (i < mip)
			mipChainBytesToLevel += bytes;
	}

	offset = mipChainBytes * face + mipChainBytesToLevel;

	return offset;
}


/**
 * vmw_surface_is_gb_screen_target_format - Is the specified format usable as
 *                                            a ScreenTarget?
 *                                            (with just the GBObjects cap-bit
 *                                             set)
 * @format: format to queried
 *
 * RETURNS:
 * true if queried format is valid for screen targets
 */
static inline bool
vmw_surface_is_gb_screen_target_format(SVGA3dSurfaceFormat format)
{
	return (format == SVGA3D_X8R8G8B8 ||
		format == SVGA3D_A8R8G8B8 ||
		format == SVGA3D_R5G6B5   ||
		format == SVGA3D_X1R5G5B5 ||
		format == SVGA3D_A1R5G5B5 ||
		format == SVGA3D_P8);
}


/**
 * vmw_surface_is_dx_screen_target_format - Is the specified format usable as
 *                                            a ScreenTarget?
 *                                            (with DX10 enabled)
 *
 * @format: format to queried
 *
 * Results:
 * true if queried format is valid for screen targets
 */
static inline bool
vmw_surface_is_dx_screen_target_format(SVGA3dSurfaceFormat format)
{
	return (format == SVGA3D_R8G8B8A8_UNORM ||
		format == SVGA3D_B8G8R8A8_UNORM ||
		format == SVGA3D_B8G8R8X8_UNORM);
}


/**
 * vmw_surface_is_screen_target_format - Is the specified format usable as a
 *                                         ScreenTarget?
 *                                         (for some combination of caps)
 *
 * @format: format to queried
 *
 * Results:
 * true if queried format is valid for screen targets
 */
static inline bool
vmw_surface_is_screen_target_format(SVGA3dSurfaceFormat format)
{
	if (vmw_surface_is_gb_screen_target_format(format)) {
		return true;
	}
	return vmw_surface_is_dx_screen_target_format(format);
}

/**
 * struct vmw_surface_mip - Mimpmap level information
 * @bytes: Bytes required in the backing store of this mipmap level.
 * @img_stride: Byte stride per image.
 * @row_stride: Byte stride per block row.
 * @size: The size of the mipmap.
 */
struct vmw_surface_mip {
	size_t bytes;
	size_t img_stride;
	size_t row_stride;
	struct drm_vmw_size size;

};

/**
 * struct vmw_surface_cache - Cached surface information
 * @desc: Pointer to the surface descriptor
 * @mip: Array of mipmap level information. Valid size is @num_mip_levels.
 * @mip_chain_bytes: Bytes required in the backing store for the whole chain
 * of mip levels.
 * @sheet_bytes: Bytes required in the backing store for a sheet
 * representing a single sample.
 * @num_mip_levels: Valid size of the @mip array. Number of mipmap levels in
 * a chain.
 * @num_layers: Number of slices in an array texture or number of faces in
 * a cubemap texture.
 */
struct vmw_surface_cache {
	const SVGA3dSurfaceDesc *desc;
	struct vmw_surface_mip mip[DRM_VMW_MAX_MIP_LEVELS];
	size_t mip_chain_bytes;
	size_t sheet_bytes;
	u32 num_mip_levels;
	u32 num_layers;
};

/**
 * struct vmw_surface_loc - Surface location
 * @sheet: The multisample sheet.
 * @sub_resource: Surface subresource. Defined as layer * num_mip_levels +
 * mip_level.
 * @x: X coordinate.
 * @y: Y coordinate.
 * @z: Z coordinate.
 */
struct vmw_surface_loc {
	u32 sheet;
	u32 sub_resource;
	u32 x, y, z;
};

/**
 * vmw_surface_subres - Compute the subresource from layer and mipmap.
 * @cache: Surface layout data.
 * @mip_level: The mipmap level.
 * @layer: The surface layer (face or array slice).
 *
 * Return: The subresource.
 */
static inline u32 vmw_surface_subres(const struct vmw_surface_cache *cache,
				       u32 mip_level, u32 layer)
{
	return cache->num_mip_levels * layer + mip_level;
}

/**
 * vmw_surface_setup_cache - Build a surface cache entry
 * @size: The surface base level dimensions.
 * @format: The surface format.
 * @num_mip_levels: Number of mipmap levels.
 * @num_layers: Number of layers.
 * @cache: Pointer to a struct vmw_surface_cach object to be filled in.
 *
 * Return: Zero on success, -EINVAL on invalid surface layout.
 */
static inline int vmw_surface_setup_cache(const struct drm_vmw_size *size,
					    SVGA3dSurfaceFormat format,
					    u32 num_mip_levels,
					    u32 num_layers,
					    u32 num_samples,
					    struct vmw_surface_cache *cache)
{
	const SVGA3dSurfaceDesc *desc;
	u32 i;

	memset(cache, 0, sizeof(*cache));
	cache->desc = desc = vmw_surface_get_desc(format);
	cache->num_mip_levels = num_mip_levels;
	cache->num_layers = num_layers;
	for (i = 0; i < cache->num_mip_levels; i++) {
		struct vmw_surface_mip *mip = &cache->mip[i];

		mip->size = vmw_surface_get_mip_size(*size, i);
		mip->bytes = vmw_surface_get_image_buffer_size
			(desc, &mip->size, 0);
		mip->row_stride =
			__KERNEL_DIV_ROUND_UP(mip->size.width,
					      desc->blockSize.width) *
			desc->bytesPerBlock * num_samples;
		if (!mip->row_stride)
			goto invalid_dim;

		mip->img_stride =
			__KERNEL_DIV_ROUND_UP(mip->size.height,
					      desc->blockSize.height) *
			mip->row_stride;
		if (!mip->img_stride)
			goto invalid_dim;

		cache->mip_chain_bytes += mip->bytes;
	}
	cache->sheet_bytes = cache->mip_chain_bytes * num_layers;
	if (!cache->sheet_bytes)
		goto invalid_dim;

	return 0;

invalid_dim:
	VMW_DEBUG_USER("Invalid surface layout for dirty tracking.\n");
	return -EINVAL;
}

/**
 * vmw_surface_get_loc - Get a surface location from an offset into the
 * backing store
 * @cache: Surface layout data.
 * @loc: Pointer to a struct vmw_surface_loc to be filled in.
 * @offset: Offset into the surface backing store.
 */
static inline void
vmw_surface_get_loc(const struct vmw_surface_cache *cache,
		      struct vmw_surface_loc *loc,
		      size_t offset)
{
	const struct vmw_surface_mip *mip = &cache->mip[0];
	const SVGA3dSurfaceDesc *desc = cache->desc;
	u32 layer;
	int i;

	loc->sheet = offset / cache->sheet_bytes;
	offset -= loc->sheet * cache->sheet_bytes;

	layer = offset / cache->mip_chain_bytes;
	offset -= layer * cache->mip_chain_bytes;
	for (i = 0; i < cache->num_mip_levels; ++i, ++mip) {
		if (mip->bytes > offset)
			break;
		offset -= mip->bytes;
	}

	loc->sub_resource = vmw_surface_subres(cache, i, layer);
	loc->z = offset / mip->img_stride;
	offset -= loc->z * mip->img_stride;
	loc->z *= desc->blockSize.depth;
	loc->y = offset / mip->row_stride;
	offset -= loc->y * mip->row_stride;
	loc->y *= desc->blockSize.height;
	loc->x = offset / desc->bytesPerBlock;
	loc->x *= desc->blockSize.width;
}

/**
 * vmw_surface_inc_loc - Clamp increment a surface location with one block
 * size
 * in each dimension.
 * @loc: Pointer to a struct vmw_surface_loc to be incremented.
 *
 * When computing the size of a range as size = end - start, the range does not
 * include the end element. However a location representing the last byte
 * of a touched region in the backing store *is* included in the range.
 * This function modifies such a location to match the end definition
 * given as start + size which is the one used in a SVGA3dBox.
 */
static inline void
vmw_surface_inc_loc(const struct vmw_surface_cache *cache,
		      struct vmw_surface_loc *loc)
{
	const SVGA3dSurfaceDesc *desc = cache->desc;
	u32 mip = loc->sub_resource % cache->num_mip_levels;
	const struct drm_vmw_size *size = &cache->mip[mip].size;

	loc->sub_resource++;
	loc->x += desc->blockSize.width;
	if (loc->x > size->width)
		loc->x = size->width;
	loc->y += desc->blockSize.height;
	if (loc->y > size->height)
		loc->y = size->height;
	loc->z += desc->blockSize.depth;
	if (loc->z > size->depth)
		loc->z = size->depth;
}

/**
 * vmw_surface_min_loc - The start location in a subresource
 * @cache: Surface layout data.
 * @sub_resource: The subresource.
 * @loc: Pointer to a struct vmw_surface_loc to be filled in.
 */
static inline void
vmw_surface_min_loc(const struct vmw_surface_cache *cache,
		      u32 sub_resource,
		      struct vmw_surface_loc *loc)
{
	loc->sheet = 0;
	loc->sub_resource = sub_resource;
	loc->x = loc->y = loc->z = 0;
}

/**
 * vmw_surface_min_loc - The end location in a subresource
 * @cache: Surface layout data.
 * @sub_resource: The subresource.
 * @loc: Pointer to a struct vmw_surface_loc to be filled in.
 *
 * Following the end definition given in vmw_surface_inc_loc(),
 * Compute the end location of a surface subresource.
 */
static inline void
vmw_surface_max_loc(const struct vmw_surface_cache *cache,
		      u32 sub_resource,
		      struct vmw_surface_loc *loc)
{
	const struct drm_vmw_size *size;
	u32 mip;

	loc->sheet = 0;
	loc->sub_resource = sub_resource + 1;
	mip = sub_resource % cache->num_mip_levels;
	size = &cache->mip[mip].size;
	loc->x = size->width;
	loc->y = size->height;
	loc->z = size->depth;
}


#endif /* VMW_SURFACE_CACHE_H */
