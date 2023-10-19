// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "gem/i915_gem_domain.h"
#include "gem/i915_gem_internal.h"
#include "gt/gen8_ppgtt.h"

#include "i915_drv.h"
#include "intel_display_types.h"
#include "intel_dpt.h"
#include "intel_fb.h"

struct i915_dpt {
	struct i915_address_space vm;

	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	void __iomem *iomem;
};

#define i915_is_dpt(vm) ((vm)->is_dpt)

static inline struct i915_dpt *
i915_vm_to_dpt(struct i915_address_space *vm)
{
	BUILD_BUG_ON(offsetof(struct i915_dpt, vm));
	GEM_BUG_ON(!i915_is_dpt(vm));
	return container_of(vm, struct i915_dpt, vm);
}

#define dpt_total_entries(dpt) ((dpt)->vm.total >> PAGE_SHIFT)

static void gen8_set_pte(void __iomem *addr, gen8_pte_t pte)
{
	writeq(pte, addr);
}

static void dpt_insert_page(struct i915_address_space *vm,
			    dma_addr_t addr,
			    u64 offset,
			    enum i915_cache_level level,
			    u32 flags)
{
	struct i915_dpt *dpt = i915_vm_to_dpt(vm);
	gen8_pte_t __iomem *base = dpt->iomem;

	gen8_set_pte(base + offset / I915_GTT_PAGE_SIZE,
		     vm->pte_encode(addr, level, flags));
}

static void dpt_insert_entries(struct i915_address_space *vm,
			       struct i915_vma_resource *vma_res,
			       enum i915_cache_level level,
			       u32 flags)
{
	struct i915_dpt *dpt = i915_vm_to_dpt(vm);
	gen8_pte_t __iomem *base = dpt->iomem;
	const gen8_pte_t pte_encode = vm->pte_encode(0, level, flags);
	struct sgt_iter sgt_iter;
	dma_addr_t addr;
	int i;

	/*
	 * Note that we ignore PTE_READ_ONLY here. The caller must be careful
	 * not to allow the user to override access to a read only page.
	 */

	i = vma_res->start / I915_GTT_PAGE_SIZE;
	for_each_sgt_daddr(addr, sgt_iter, vma_res->bi.pages)
		gen8_set_pte(&base[i++], pte_encode | addr);
}

static void dpt_clear_range(struct i915_address_space *vm,
			    u64 start, u64 length)
{
}

static void dpt_bind_vma(struct i915_address_space *vm,
			 struct i915_vm_pt_stash *stash,
			 struct i915_vma_resource *vma_res,
			 enum i915_cache_level cache_level,
			 u32 flags)
{
	u32 pte_flags;

	if (vma_res->bound_flags)
		return;

	/* Applicable to VLV (gen8+ do not support RO in the GGTT) */
	pte_flags = 0;
	if (vm->has_read_only && vma_res->bi.readonly)
		pte_flags |= PTE_READ_ONLY;
	if (vma_res->bi.lmem)
		pte_flags |= PTE_LM;

	vm->insert_entries(vm, vma_res, cache_level, pte_flags);

	vma_res->page_sizes_gtt = I915_GTT_PAGE_SIZE;

	/*
	 * Without aliasing PPGTT there's no difference between
	 * GLOBAL/LOCAL_BIND, it's all the same ptes. Hence unconditionally
	 * upgrade to both bound if we bind either to avoid double-binding.
	 */
	vma_res->bound_flags = I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND;
}

static void dpt_unbind_vma(struct i915_address_space *vm,
			   struct i915_vma_resource *vma_res)
{
	vm->clear_range(vm, vma_res->start, vma_res->vma_size);
}

static void dpt_cleanup(struct i915_address_space *vm)
{
	struct i915_dpt *dpt = i915_vm_to_dpt(vm);

	i915_gem_object_put(dpt->obj);
}

struct i915_vma *intel_dpt_pin(struct i915_address_space *vm)
{
	struct drm_i915_private *i915 = vm->i915;
	struct i915_dpt *dpt = i915_vm_to_dpt(vm);
	intel_wakeref_t wakeref;
	struct i915_vma *vma;
	void __iomem *iomem;
	struct i915_gem_ww_ctx ww;
	u64 pin_flags = 0;
	int err;

	if (i915_gem_object_is_stolen(dpt->obj))
		pin_flags |= PIN_MAPPABLE;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);
	atomic_inc(&i915->gpu_error.pending_fb_pin);

	for_i915_gem_ww(&ww, err, true) {
		err = i915_gem_object_lock(dpt->obj, &ww);
		if (err)
			continue;

		vma = i915_gem_object_ggtt_pin_ww(dpt->obj, &ww, NULL, 0, 4096,
						  pin_flags);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			continue;
		}

		iomem = i915_vma_pin_iomap(vma);
		i915_vma_unpin(vma);

		if (IS_ERR(iomem)) {
			err = PTR_ERR(iomem);
			continue;
		}

		dpt->vma = vma;
		dpt->iomem = iomem;

		i915_vma_get(vma);
	}

	atomic_dec(&i915->gpu_error.pending_fb_pin);
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return err ? ERR_PTR(err) : vma;
}

void intel_dpt_unpin(struct i915_address_space *vm)
{
	struct i915_dpt *dpt = i915_vm_to_dpt(vm);

	i915_vma_unpin_iomap(dpt->vma);
	i915_vma_put(dpt->vma);
}

/**
 * intel_dpt_resume - restore the memory mapping for all DPT FBs during system resume
 * @i915: device instance
 *
 * Restore the memory mapping during system resume for all framebuffers which
 * are mapped to HW via a GGTT->DPT page table. The content of these page
 * tables are not stored in the hibernation image during S4 and S3RST->S4
 * transitions, so here we reprogram the PTE entries in those tables.
 *
 * This function must be called after the mappings in GGTT have been restored calling
 * i915_ggtt_resume().
 */
void intel_dpt_resume(struct drm_i915_private *i915)
{
	struct drm_framebuffer *drm_fb;

	if (!HAS_DISPLAY(i915))
		return;

	mutex_lock(&i915->drm.mode_config.fb_lock);
	drm_for_each_fb(drm_fb, &i915->drm) {
		struct intel_framebuffer *fb = to_intel_framebuffer(drm_fb);

		if (fb->dpt_vm)
			i915_ggtt_resume_vm(fb->dpt_vm);
	}
	mutex_unlock(&i915->drm.mode_config.fb_lock);
}

/**
 * intel_dpt_suspend - suspend the memory mapping for all DPT FBs during system suspend
 * @i915: device instance
 *
 * Suspend the memory mapping during system suspend for all framebuffers which
 * are mapped to HW via a GGTT->DPT page table.
 *
 * This function must be called before the mappings in GGTT are suspended calling
 * i915_ggtt_suspend().
 */
void intel_dpt_suspend(struct drm_i915_private *i915)
{
	struct drm_framebuffer *drm_fb;

	if (!HAS_DISPLAY(i915))
		return;

	mutex_lock(&i915->drm.mode_config.fb_lock);

	drm_for_each_fb(drm_fb, &i915->drm) {
		struct intel_framebuffer *fb = to_intel_framebuffer(drm_fb);

		if (fb->dpt_vm)
			i915_ggtt_suspend_vm(fb->dpt_vm);
	}

	mutex_unlock(&i915->drm.mode_config.fb_lock);
}

struct i915_address_space *
intel_dpt_create(struct intel_framebuffer *fb)
{
	struct drm_gem_object *obj = &intel_fb_obj(&fb->base)->base;
	struct drm_i915_private *i915 = to_i915(obj->dev);
	struct drm_i915_gem_object *dpt_obj;
	struct i915_address_space *vm;
	struct i915_dpt *dpt;
	size_t size;
	int ret;

	if (intel_fb_needs_pot_stride_remap(fb))
		size = intel_remapped_info_size(&fb->remapped_view.gtt.remapped);
	else
		size = DIV_ROUND_UP_ULL(obj->size, I915_GTT_PAGE_SIZE);

	size = round_up(size * sizeof(gen8_pte_t), I915_GTT_PAGE_SIZE);

	dpt_obj = i915_gem_object_create_lmem(i915, size, I915_BO_ALLOC_CONTIGUOUS);
	if (IS_ERR(dpt_obj) && i915_ggtt_has_aperture(to_gt(i915)->ggtt))
		dpt_obj = i915_gem_object_create_stolen(i915, size);
	if (IS_ERR(dpt_obj) && !HAS_LMEM(i915)) {
		drm_dbg_kms(&i915->drm, "Allocating dpt from smem\n");
		dpt_obj = i915_gem_object_create_internal(i915, size);
	}
	if (IS_ERR(dpt_obj))
		return ERR_CAST(dpt_obj);

	ret = i915_gem_object_lock_interruptible(dpt_obj, NULL);
	if (!ret) {
		ret = i915_gem_object_set_cache_level(dpt_obj, I915_CACHE_NONE);
		i915_gem_object_unlock(dpt_obj);
	}
	if (ret) {
		i915_gem_object_put(dpt_obj);
		return ERR_PTR(ret);
	}

	dpt = kzalloc(sizeof(*dpt), GFP_KERNEL);
	if (!dpt) {
		i915_gem_object_put(dpt_obj);
		return ERR_PTR(-ENOMEM);
	}

	vm = &dpt->vm;

	vm->gt = to_gt(i915);
	vm->i915 = i915;
	vm->dma = i915->drm.dev;
	vm->total = (size / sizeof(gen8_pte_t)) * I915_GTT_PAGE_SIZE;
	vm->is_dpt = true;

	i915_address_space_init(vm, VM_CLASS_DPT);

	vm->insert_page = dpt_insert_page;
	vm->clear_range = dpt_clear_range;
	vm->insert_entries = dpt_insert_entries;
	vm->cleanup = dpt_cleanup;

	vm->vma_ops.bind_vma    = dpt_bind_vma;
	vm->vma_ops.unbind_vma  = dpt_unbind_vma;

	vm->pte_encode = gen8_ggtt_pte_encode;

	dpt->obj = dpt_obj;
	dpt->obj->is_dpt = true;

	return &dpt->vm;
}

void intel_dpt_destroy(struct i915_address_space *vm)
{
	struct i915_dpt *dpt = i915_vm_to_dpt(vm);

	dpt->obj->is_dpt = false;
	i915_vm_put(&dpt->vm);
}
