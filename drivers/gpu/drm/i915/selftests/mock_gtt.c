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

#include "mock_gtt.h"

static void mock_insert_page(struct i915_address_space *vm,
			     dma_addr_t addr,
			     u64 offset,
			     enum i915_cache_level level,
			     u32 flags)
{
}

static void mock_insert_entries(struct i915_address_space *vm,
				struct sg_table *st,
				u64 start,
				enum i915_cache_level level, u32 flags)
{
}

static int mock_bind_ppgtt(struct i915_vma *vma,
			   enum i915_cache_level cache_level,
			   u32 flags)
{
	GEM_BUG_ON(flags & I915_VMA_GLOBAL_BIND);
	vma->pages = vma->obj->mm.pages;
	vma->flags |= I915_VMA_LOCAL_BIND;
	return 0;
}

static void mock_unbind_ppgtt(struct i915_vma *vma)
{
}

static void mock_cleanup(struct i915_address_space *vm)
{
}

struct i915_hw_ppgtt *
mock_ppgtt(struct drm_i915_private *i915,
	   const char *name)
{
	struct i915_hw_ppgtt *ppgtt;

	ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
	if (!ppgtt)
		return NULL;

	kref_init(&ppgtt->ref);
	ppgtt->base.i915 = i915;
	ppgtt->base.total = round_down(U64_MAX, PAGE_SIZE);
	ppgtt->base.file = ERR_PTR(-ENODEV);

	INIT_LIST_HEAD(&ppgtt->base.active_list);
	INIT_LIST_HEAD(&ppgtt->base.inactive_list);
	INIT_LIST_HEAD(&ppgtt->base.unbound_list);

	INIT_LIST_HEAD(&ppgtt->base.global_link);
	drm_mm_init(&ppgtt->base.mm, 0, ppgtt->base.total);
	i915_gem_timeline_init(i915, &ppgtt->base.timeline, name);

	ppgtt->base.clear_range = nop_clear_range;
	ppgtt->base.insert_page = mock_insert_page;
	ppgtt->base.insert_entries = mock_insert_entries;
	ppgtt->base.bind_vma = mock_bind_ppgtt;
	ppgtt->base.unbind_vma = mock_unbind_ppgtt;
	ppgtt->base.cleanup = mock_cleanup;

	return ppgtt;
}

static int mock_bind_ggtt(struct i915_vma *vma,
			  enum i915_cache_level cache_level,
			  u32 flags)
{
	int err;

	err = i915_get_ggtt_vma_pages(vma);
	if (err)
		return err;

	vma->flags |= I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND;
	return 0;
}

static void mock_unbind_ggtt(struct i915_vma *vma)
{
}

void mock_init_ggtt(struct drm_i915_private *i915)
{
	struct i915_ggtt *ggtt = &i915->ggtt;

	INIT_LIST_HEAD(&i915->vm_list);

	ggtt->base.i915 = i915;

	ggtt->mappable_base = 0;
	ggtt->mappable_end = 2048 * PAGE_SIZE;
	ggtt->base.total = 4096 * PAGE_SIZE;

	ggtt->base.clear_range = nop_clear_range;
	ggtt->base.insert_page = mock_insert_page;
	ggtt->base.insert_entries = mock_insert_entries;
	ggtt->base.bind_vma = mock_bind_ggtt;
	ggtt->base.unbind_vma = mock_unbind_ggtt;
	ggtt->base.cleanup = mock_cleanup;

	i915_address_space_init(&ggtt->base, i915, "global");
}

void mock_fini_ggtt(struct drm_i915_private *i915)
{
	struct i915_ggtt *ggtt = &i915->ggtt;

	i915_address_space_fini(&ggtt->base);
}
