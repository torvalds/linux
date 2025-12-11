// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/drm_cache.h>
#include <drm/drm_panic.h>
#include <drm/intel/display_parent_interface.h>

#include "intel_display_types.h"
#include "intel_fb.h"
#include "xe_bo.h"
#include "xe_panic.h"
#include "xe_res_cursor.h"

struct intel_panic {
	struct xe_res_cursor res;
	struct iosys_map vmap;

	int page;
};

static void xe_panic_kunmap(struct intel_panic *panic)
{
	if (!panic->vmap.is_iomem && iosys_map_is_set(&panic->vmap)) {
		drm_clflush_virt_range(panic->vmap.vaddr, PAGE_SIZE);
		kunmap_local(panic->vmap.vaddr);
	}
	iosys_map_clear(&panic->vmap);
	panic->page = -1;
}

/*
 * The scanout buffer pages are not mapped, so for each pixel,
 * use kmap_local_page_try_from_panic() to map the page, and write the pixel.
 * Try to keep the map from the previous pixel, to avoid too much map/unmap.
 */
static void xe_panic_page_set_pixel(struct drm_scanout_buffer *sb, unsigned int x,
				    unsigned int y, u32 color)
{
	struct intel_framebuffer *fb = (struct intel_framebuffer *)sb->private;
	struct intel_panic *panic = fb->panic;
	struct xe_bo *bo = gem_to_xe_bo(intel_fb_bo(&fb->base));
	unsigned int new_page;
	unsigned int offset;

	if (fb->panic_tiling)
		offset = fb->panic_tiling(sb->width, x, y);
	else
		offset = y * sb->pitch[0] + x * sb->format->cpp[0];

	new_page = offset >> PAGE_SHIFT;
	offset = offset % PAGE_SIZE;
	if (new_page != panic->page) {
		if (xe_bo_is_vram(bo)) {
			/* Display is always mapped on root tile */
			struct xe_vram_region *vram = xe_bo_device(bo)->mem.vram;

			if (panic->page < 0 || new_page < panic->page) {
				xe_res_first(bo->ttm.resource, new_page * PAGE_SIZE,
					     bo->ttm.base.size - new_page * PAGE_SIZE, &panic->res);
			} else {
				xe_res_next(&panic->res, PAGE_SIZE * (new_page - panic->page));
			}
			iosys_map_set_vaddr_iomem(&panic->vmap,
						  vram->mapping + panic->res.start);
		} else {
			xe_panic_kunmap(panic);
			iosys_map_set_vaddr(&panic->vmap,
					    ttm_bo_kmap_try_from_panic(&bo->ttm,
								       new_page));
		}
		panic->page = new_page;
	}

	if (iosys_map_is_set(&panic->vmap))
		iosys_map_wr(&panic->vmap, offset, u32, color);
}

static struct intel_panic *xe_panic_alloc(void)
{
	struct intel_panic *panic;

	panic = kzalloc(sizeof(*panic), GFP_KERNEL);

	return panic;
}

static int xe_panic_setup(struct intel_panic *panic, struct drm_scanout_buffer *sb)
{
	struct intel_framebuffer *fb = (struct intel_framebuffer *)sb->private;
	struct xe_bo *bo = gem_to_xe_bo(intel_fb_bo(&fb->base));

	if (xe_bo_is_vram(bo) && !xe_bo_is_visible_vram(bo))
		return -ENODEV;

	panic->page = -1;
	sb->set_pixel = xe_panic_page_set_pixel;
	return 0;
}

const struct intel_display_panic_interface xe_display_panic_interface = {
	.alloc = xe_panic_alloc,
	.setup = xe_panic_setup,
	.finish = xe_panic_kunmap,
};
