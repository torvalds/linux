// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>

#include <drm/drm_gem_ttm_helper.h>

/**
 * DOC: overview
 *
 * This library provides helper functions for gem objects backed by
 * ttm.
 */

/**
 * drm_gem_ttm_print_info() - Print &ttm_buffer_object info for debugfs
 * @p: DRM printer
 * @indent: Tab indentation level
 * @gem: GEM object
 *
 * This function can be used as &drm_gem_object_funcs.print_info
 * callback.
 */
void drm_gem_ttm_print_info(struct drm_printer *p, unsigned int indent,
			    const struct drm_gem_object *gem)
{
	static const char * const plname[] = {
		[ TTM_PL_SYSTEM ] = "system",
		[ TTM_PL_TT     ] = "tt",
		[ TTM_PL_VRAM   ] = "vram",
		[ TTM_PL_PRIV   ] = "priv",

		[ 16 ]            = "cached",
		[ 17 ]            = "uncached",
		[ 18 ]            = "wc",
		[ 19 ]            = "contig",

		[ 21 ]            = "pinned", /* NO_EVICT */
		[ 22 ]            = "topdown",
	};
	const struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);

	drm_printf_indent(p, indent, "placement=");
	drm_print_bits(p, bo->resource->placement, plname, ARRAY_SIZE(plname));
	drm_printf(p, "\n");

	if (bo->resource->bus.is_iomem)
		drm_printf_indent(p, indent, "bus.offset=%lx\n",
				  (unsigned long)bo->resource->bus.offset);
}
EXPORT_SYMBOL(drm_gem_ttm_print_info);

/**
 * drm_gem_ttm_vmap() - vmap &ttm_buffer_object
 * @gem: GEM object.
 * @map: [out] returns the dma-buf mapping.
 *
 * Maps a GEM object with ttm_bo_vmap(). This function can be used as
 * &drm_gem_object_funcs.vmap callback.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise.
 */
int drm_gem_ttm_vmap(struct drm_gem_object *gem,
		     struct iosys_map *map)
{
	struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);
	int ret;

	dma_resv_lock(gem->resv, NULL);
	ret = ttm_bo_vmap(bo, map);
	dma_resv_unlock(gem->resv);

	return ret;
}
EXPORT_SYMBOL(drm_gem_ttm_vmap);

/**
 * drm_gem_ttm_vunmap() - vunmap &ttm_buffer_object
 * @gem: GEM object.
 * @map: dma-buf mapping.
 *
 * Unmaps a GEM object with ttm_bo_vunmap(). This function can be used as
 * &drm_gem_object_funcs.vmap callback.
 */
void drm_gem_ttm_vunmap(struct drm_gem_object *gem,
			struct iosys_map *map)
{
	struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);

	dma_resv_lock(gem->resv, NULL);
	ttm_bo_vunmap(bo, map);
	dma_resv_unlock(gem->resv);
}
EXPORT_SYMBOL(drm_gem_ttm_vunmap);

/**
 * drm_gem_ttm_mmap() - mmap &ttm_buffer_object
 * @gem: GEM object.
 * @vma: vm area.
 *
 * This function can be used as &drm_gem_object_funcs.mmap
 * callback.
 */
int drm_gem_ttm_mmap(struct drm_gem_object *gem,
		     struct vm_area_struct *vma)
{
	struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);
	int ret;

	ret = ttm_bo_mmap_obj(vma, bo);
	if (ret < 0)
		return ret;

	/*
	 * ttm has its own object refcounting, so drop gem reference
	 * to avoid double accounting counting.
	 */
	drm_gem_object_put(gem);

	return 0;
}
EXPORT_SYMBOL(drm_gem_ttm_mmap);

/**
 * drm_gem_ttm_dumb_map_offset() - Implements struct &drm_driver.dumb_map_offset
 * @file:	DRM file pointer.
 * @dev:	DRM device.
 * @handle:	GEM handle
 * @offset:	Returns the mapping's memory offset on success
 *
 * Provides an implementation of struct &drm_driver.dumb_map_offset for
 * TTM-based GEM drivers. TTM allocates the offset internally and
 * drm_gem_ttm_dumb_map_offset() returns it for dumb-buffer implementations.
 *
 * See struct &drm_driver.dumb_map_offset.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise.
 */
int drm_gem_ttm_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
				uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *gem;

	gem = drm_gem_object_lookup(file, handle);
	if (!gem)
		return -ENOENT;

	*offset = drm_vma_node_offset_addr(&gem->vma_node);

	drm_gem_object_put(gem);

	return 0;
}
EXPORT_SYMBOL(drm_gem_ttm_dumb_map_offset);

MODULE_DESCRIPTION("DRM gem ttm helpers");
MODULE_LICENSE("GPL");
