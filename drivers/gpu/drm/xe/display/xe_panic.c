// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/drm_cache.h>
#include <drm/drm_panic.h>

#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_panic.h"
#include "xe_bo.h"

struct intel_panic {
	struct page **pages;
	int page;
	void *vaddr;
};

struct xe_framebuffer {
	struct intel_framebuffer base;
	struct intel_panic panic;
};

static void xe_panic_kunmap(struct intel_panic *panic)
{
	if (panic->vaddr) {
		drm_clflush_virt_range(panic->vaddr, PAGE_SIZE);
		kunmap_local(panic->vaddr);
		panic->vaddr = NULL;
	}
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
		xe_panic_kunmap(panic);
		panic->page = new_page;
		panic->vaddr = ttm_bo_kmap_try_from_panic(&bo->ttm,
							  panic->page);
	}
	if (panic->vaddr) {
		u32 *pix = panic->vaddr + offset;
		*pix = color;
	}
}

struct intel_framebuffer *intel_bo_alloc_framebuffer(void)
{
	struct xe_framebuffer *xe_fb;

	xe_fb = kzalloc(sizeof(*xe_fb), GFP_KERNEL);
	if (!xe_fb)
		return NULL;

	xe_fb->base.panic = &xe_fb->panic;

	return &xe_fb->base;
}

int intel_panic_setup(struct drm_scanout_buffer *sb)
{
	struct intel_framebuffer *fb = (struct intel_framebuffer *)sb->private;
	struct intel_panic *panic = fb->panic;

	panic->page = -1;
	sb->set_pixel = xe_panic_page_set_pixel;
	return 0;
}

void intel_panic_finish(struct intel_framebuffer *fb)
{
	struct intel_panic *panic = fb->panic;

	xe_panic_kunmap(panic);
	panic->page = -1;
}
