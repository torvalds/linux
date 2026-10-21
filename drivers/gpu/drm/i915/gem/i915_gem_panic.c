// SPDX-License-Identifier: MIT
/* Copyright © 2026 Intel Corporation */

#include <drm/drm_cache.h>
#include <drm/drm_panic.h>
#include <drm/intel/display_parent_interface.h>

#include "i915_gem_object.h"
#include "i915_gem_panic.h"

struct intel_panic {
	struct page **pages;
	int page;
	void *vaddr;

	unsigned int (*tiling)(unsigned int x, unsigned int y, unsigned int width);
};

static void i915_panic_kunmap(struct intel_panic *panic)
{
	if (panic->vaddr) {
		drm_clflush_virt_range(panic->vaddr, PAGE_SIZE);
		kunmap_local(panic->vaddr);
		panic->vaddr = NULL;
	}
}

static struct page **i915_gem_object_panic_pages(struct drm_i915_gem_object *obj)
{
	unsigned long n_pages = obj->base.size >> PAGE_SHIFT, i;
	struct page *page;
	struct page **pages;
	struct sgt_iter iter;

	/* For a 3840x2160 32 bits Framebuffer, this should require ~64K */
	pages = kmalloc_objs(*pages, n_pages, GFP_ATOMIC);
	if (!pages)
		return NULL;

	i = 0;
	for_each_sgt_page(page, iter, obj->mm.pages)
		pages[i++] = page;
	return pages;
}

static void i915_gem_object_panic_map_set_pixel(struct drm_scanout_buffer *sb, unsigned int x,
						unsigned int y, u32 color)
{
	struct intel_panic *panic = sb->private;
	unsigned int offset = panic->tiling(sb->width, x, y);

	iosys_map_wr(&sb->map[0], offset, u32, color);
}

/*
 * The scanout buffer pages are not mapped, so for each pixel,
 * use kmap_local_page_try_from_panic() to map the page, and write the pixel.
 * Try to keep the map from the previous pixel, to avoid too much map/unmap.
 */
static void i915_gem_object_panic_page_set_pixel(struct drm_scanout_buffer *sb, unsigned int x,
						 unsigned int y, u32 color)
{
	struct intel_panic *panic = sb->private;
	unsigned int new_page;
	unsigned int offset;

	if (panic->tiling)
		offset = panic->tiling(sb->width, x, y);
	else
		offset = y * sb->pitch[0] + x * sb->format->cpp[0];

	new_page = offset >> PAGE_SHIFT;
	offset = offset % PAGE_SIZE;
	if (new_page != panic->page) {
		i915_panic_kunmap(panic);
		panic->page = new_page;
		panic->vaddr =
			kmap_local_page_try_from_panic(panic->pages[panic->page]);
	}
	if (panic->vaddr) {
		u32 *pix = panic->vaddr + offset;
		*pix = color;
	}
}

static struct intel_panic *i915_gem_object_alloc_panic(void)
{
	struct intel_panic *panic;

	panic = kzalloc_obj(*panic);

	return panic;
}

/*
 * Setup the gem framebuffer for drm_panic access.
 * Use current vaddr if it exists, or setup a list of pages.
 * pfn is not supported yet.
 */
static int i915_gem_object_panic_setup(struct intel_panic *panic, struct drm_scanout_buffer *sb,
				       struct drm_gem_object *_obj,
				       unsigned int (*tiling)(unsigned int x, unsigned int y, unsigned int width))
{
	enum i915_map_type has_type;
	struct drm_i915_gem_object *obj = to_intel_bo(_obj);
	void *ptr;

	sb->private = panic;

	ptr = page_unpack_bits(obj->mm.mapping, &has_type);
	if (ptr) {
		if (i915_gem_object_has_iomem(obj))
			iosys_map_set_vaddr_iomem(&sb->map[0], (void __iomem *)ptr);
		else
			iosys_map_set_vaddr(&sb->map[0], ptr);

		if (tiling) {
			panic->tiling = tiling;
			sb->set_pixel = i915_gem_object_panic_map_set_pixel;
		}
		return 0;
	}
	if (i915_gem_object_has_struct_page(obj)) {
		panic->pages = i915_gem_object_panic_pages(obj);
		if (!panic->pages)
			return -ENOMEM;
		panic->page = -1;
		panic->tiling = tiling;
		sb->set_pixel = i915_gem_object_panic_page_set_pixel;
		return 0;
	}
	return -EOPNOTSUPP;
}

static void i915_gem_object_panic_finish(struct intel_panic *panic)
{
	i915_panic_kunmap(panic);
	panic->page = -1;
	kfree(panic->pages);
	panic->pages = NULL;
}

const struct intel_display_panic_interface i915_display_panic_interface = {
	.alloc = i915_gem_object_alloc_panic,
	.setup = i915_gem_object_panic_setup,
	.finish = i915_gem_object_panic_finish,
};
