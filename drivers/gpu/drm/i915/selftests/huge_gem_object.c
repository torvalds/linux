/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "huge_gem_object.h"

static void huge_free_pages(struct drm_i915_gem_object *obj,
			    struct sg_table *pages)
{
	unsigned long nreal = obj->scratch / PAGE_SIZE;
	struct scatterlist *sg;

	for (sg = pages->sgl; sg && nreal--; sg = __sg_next(sg))
		__free_page(sg_page(sg));

	sg_free_table(pages);
	kfree(pages);
}

static struct sg_table *
huge_get_pages(struct drm_i915_gem_object *obj)
{
#define GFP (GFP_KERNEL | __GFP_NOWARN | __GFP_NORETRY)
	const unsigned long nreal = obj->scratch / PAGE_SIZE;
	const unsigned long npages = obj->base.size / PAGE_SIZE;
	struct scatterlist *sg, *src, *end;
	struct sg_table *pages;
	unsigned long n;

	pages = kmalloc(sizeof(*pages), GFP);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	if (sg_alloc_table(pages, npages, GFP)) {
		kfree(pages);
		return ERR_PTR(-ENOMEM);
	}

	sg = pages->sgl;
	for (n = 0; n < nreal; n++) {
		struct page *page;

		page = alloc_page(GFP | __GFP_HIGHMEM);
		if (!page) {
			sg_mark_end(sg);
			goto err;
		}

		sg_set_page(sg, page, PAGE_SIZE, 0);
		sg = __sg_next(sg);
	}
	if (nreal < npages) {
		for (end = sg, src = pages->sgl; sg; sg = __sg_next(sg)) {
			sg_set_page(sg, sg_page(src), PAGE_SIZE, 0);
			src = __sg_next(src);
			if (src == end)
				src = pages->sgl;
		}
	}

	if (i915_gem_gtt_prepare_pages(obj, pages))
		goto err;

	return pages;

err:
	huge_free_pages(obj, pages);
	return ERR_PTR(-ENOMEM);
#undef GFP
}

static void huge_put_pages(struct drm_i915_gem_object *obj,
			   struct sg_table *pages)
{
	i915_gem_gtt_finish_pages(obj, pages);
	huge_free_pages(obj, pages);

	obj->mm.dirty = false;
}

static const struct drm_i915_gem_object_ops huge_ops = {
	.flags = I915_GEM_OBJECT_HAS_STRUCT_PAGE |
		 I915_GEM_OBJECT_IS_SHRINKABLE,
	.get_pages = huge_get_pages,
	.put_pages = huge_put_pages,
};

struct drm_i915_gem_object *
huge_gem_object(struct drm_i915_private *i915,
		phys_addr_t phys_size,
		dma_addr_t dma_size)
{
	struct drm_i915_gem_object *obj;

	GEM_BUG_ON(!phys_size || phys_size > dma_size);
	GEM_BUG_ON(!IS_ALIGNED(phys_size, PAGE_SIZE));
	GEM_BUG_ON(!IS_ALIGNED(dma_size, I915_GTT_PAGE_SIZE));

	if (overflows_type(dma_size, obj->base.size))
		return ERR_PTR(-E2BIG);

	obj = i915_gem_object_alloc(i915);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	drm_gem_private_object_init(&i915->drm, &obj->base, dma_size);
	i915_gem_object_init(obj, &huge_ops);

	obj->base.read_domains = I915_GEM_DOMAIN_CPU;
	obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	obj->cache_level = HAS_LLC(i915) ? I915_CACHE_LLC : I915_CACHE_NONE;
	obj->cache_coherent = i915_gem_object_is_coherent(obj);
	obj->cache_dirty = !obj->cache_coherent;
	obj->scratch = phys_size;

	return obj;
}
