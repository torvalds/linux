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
				struct i915_vma *vma,
				enum i915_cache_level level, u32 flags)
{
}

static void mock_bind_ppgtt(struct i915_address_space *vm,
			    struct i915_vm_pt_stash *stash,
			    struct i915_vma *vma,
			    enum i915_cache_level cache_level,
			    u32 flags)
{
	GEM_BUG_ON(flags & I915_VMA_GLOBAL_BIND);
	set_bit(I915_VMA_LOCAL_BIND_BIT, __i915_vma_flags(vma));
}

static void mock_unbind_ppgtt(struct i915_address_space *vm,
			      struct i915_vma *vma)
{
}

static void mock_cleanup(struct i915_address_space *vm)
{
}

static void mock_clear_range(struct i915_address_space *vm,
			     u64 start, u64 length)
{
}

struct i915_ppgtt *mock_ppgtt(struct drm_i915_private *i915, const char *name)
{
	struct i915_ppgtt *ppgtt;

	ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
	if (!ppgtt)
		return NULL;

	ppgtt->vm.gt = to_gt(i915);
	ppgtt->vm.i915 = i915;
	ppgtt->vm.total = round_down(U64_MAX, PAGE_SIZE);
	ppgtt->vm.dma = i915->drm.dev;

	i915_address_space_init(&ppgtt->vm, VM_CLASS_PPGTT);

	ppgtt->vm.alloc_pt_dma = alloc_pt_dma;
	ppgtt->vm.alloc_scratch_dma = alloc_pt_dma;

	ppgtt->vm.clear_range = mock_clear_range;
	ppgtt->vm.insert_page = mock_insert_page;
	ppgtt->vm.insert_entries = mock_insert_entries;
	ppgtt->vm.cleanup = mock_cleanup;

	ppgtt->vm.vma_ops.bind_vma    = mock_bind_ppgtt;
	ppgtt->vm.vma_ops.unbind_vma  = mock_unbind_ppgtt;

	return ppgtt;
}

static void mock_bind_ggtt(struct i915_address_space *vm,
			   struct i915_vm_pt_stash *stash,
			   struct i915_vma *vma,
			   enum i915_cache_level cache_level,
			   u32 flags)
{
}

static void mock_unbind_ggtt(struct i915_address_space *vm,
			     struct i915_vma *vma)
{
}

void mock_init_ggtt(struct drm_i915_private *i915, struct i915_ggtt *ggtt)
{
	memset(ggtt, 0, sizeof(*ggtt));

	ggtt->vm.gt = to_gt(i915);
	ggtt->vm.i915 = i915;
	ggtt->vm.is_ggtt = true;

	ggtt->gmadr = (struct resource) DEFINE_RES_MEM(0, 2048 * PAGE_SIZE);
	ggtt->mappable_end = resource_size(&ggtt->gmadr);
	ggtt->vm.total = 4096 * PAGE_SIZE;

	ggtt->vm.alloc_pt_dma = alloc_pt_dma;
	ggtt->vm.alloc_scratch_dma = alloc_pt_dma;

	ggtt->vm.clear_range = mock_clear_range;
	ggtt->vm.insert_page = mock_insert_page;
	ggtt->vm.insert_entries = mock_insert_entries;
	ggtt->vm.cleanup = mock_cleanup;

	ggtt->vm.vma_ops.bind_vma    = mock_bind_ggtt;
	ggtt->vm.vma_ops.unbind_vma  = mock_unbind_ggtt;

	i915_address_space_init(&ggtt->vm, VM_CLASS_GGTT);
	to_gt(i915)->ggtt = ggtt;
}

void mock_fini_ggtt(struct i915_ggtt *ggtt)
{
	i915_address_space_fini(&ggtt->vm);
}
