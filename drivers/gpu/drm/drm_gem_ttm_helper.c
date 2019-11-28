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
	drm_print_bits(p, bo->mem.placement, plname, ARRAY_SIZE(plname));
	drm_printf(p, "\n");

	if (bo->mem.bus.is_iomem) {
		drm_printf_indent(p, indent, "bus.base=%lx\n",
				  (unsigned long)bo->mem.bus.base);
		drm_printf_indent(p, indent, "bus.offset=%lx\n",
				  (unsigned long)bo->mem.bus.offset);
	}
}
EXPORT_SYMBOL(drm_gem_ttm_print_info);

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
	drm_gem_object_put_unlocked(gem);

	return 0;
}
EXPORT_SYMBOL(drm_gem_ttm_mmap);

MODULE_DESCRIPTION("DRM gem ttm helpers");
MODULE_LICENSE("GPL");
