// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/types.h>
#include <asm/set_memory.h>
#include <asm/smp.h>

#include <drm/i915_drm.h>

#include "gem/i915_gem_lmem.h"

#include "intel_gt.h"
#include "intel_gt_gmch.h"
#include "intel_gt_regs.h"
#include "i915_drv.h"
#include "i915_scatterlist.h"
#include "i915_utils.h"
#include "i915_vgpu.h"

#include "intel_gtt.h"
#include "gen8_ppgtt.h"

static void i915_ggtt_color_adjust(const struct drm_mm_node *node,
				   unsigned long color,
				   u64 *start,
				   u64 *end)
{
	if (i915_node_color_differs(node, color))
		*start += I915_GTT_PAGE_SIZE;

	/*
	 * Also leave a space between the unallocated reserved node after the
	 * GTT and any objects within the GTT, i.e. we use the color adjustment
	 * to insert a guard page to prevent prefetches crossing over the
	 * GTT boundary.
	 */
	node = list_next_entry(node, node_list);
	if (node->color != color)
		*end -= I915_GTT_PAGE_SIZE;
}

static int ggtt_init_hw(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;

	i915_address_space_init(&ggtt->vm, VM_CLASS_GGTT);

	ggtt->vm.is_ggtt = true;

	/* Only VLV supports read-only GGTT mappings */
	ggtt->vm.has_read_only = IS_VALLEYVIEW(i915);

	if (!HAS_LLC(i915) && !HAS_PPGTT(i915))
		ggtt->vm.mm.color_adjust = i915_ggtt_color_adjust;

	if (ggtt->mappable_end) {
		if (!io_mapping_init_wc(&ggtt->iomap,
					ggtt->gmadr.start,
					ggtt->mappable_end)) {
			ggtt->vm.cleanup(&ggtt->vm);
			return -EIO;
		}

		ggtt->mtrr = arch_phys_wc_add(ggtt->gmadr.start,
					      ggtt->mappable_end);
	}

	intel_ggtt_init_fences(ggtt);

	return 0;
}

/**
 * i915_ggtt_init_hw - Initialize GGTT hardware
 * @i915: i915 device
 */
int i915_ggtt_init_hw(struct drm_i915_private *i915)
{
	int ret;

	/*
	 * Note that we use page colouring to enforce a guard page at the
	 * end of the address space. This is required as the CS may prefetch
	 * beyond the end of the batch buffer, across the page boundary,
	 * and beyond the end of the GTT if we do not provide a guard.
	 */
	ret = ggtt_init_hw(to_gt(i915)->ggtt);
	if (ret)
		return ret;

	return 0;
}

/**
 * i915_ggtt_suspend_vm - Suspend the memory mappings for a GGTT or DPT VM
 * @vm: The VM to suspend the mappings for
 *
 * Suspend the memory mappings for all objects mapped to HW via the GGTT or a
 * DPT page table.
 */
void i915_ggtt_suspend_vm(struct i915_address_space *vm)
{
	struct i915_vma *vma, *vn;
	int save_skip_rewrite;

	drm_WARN_ON(&vm->i915->drm, !vm->is_ggtt && !vm->is_dpt);

retry:
	i915_gem_drain_freed_objects(vm->i915);

	mutex_lock(&vm->mutex);

	/*
	 * Skip rewriting PTE on VMA unbind.
	 * FIXME: Use an argument to i915_vma_unbind() instead?
	 */
	save_skip_rewrite = vm->skip_pte_rewrite;
	vm->skip_pte_rewrite = true;

	list_for_each_entry_safe(vma, vn, &vm->bound_list, vm_link) {
		struct drm_i915_gem_object *obj = vma->obj;

		GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));

		if (i915_vma_is_pinned(vma) || !i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND))
			continue;

		/* unlikely to race when GPU is idle, so no worry about slowpath.. */
		if (WARN_ON(!i915_gem_object_trylock(obj, NULL))) {
			/*
			 * No dead objects should appear here, GPU should be
			 * completely idle, and userspace suspended
			 */
			i915_gem_object_get(obj);

			mutex_unlock(&vm->mutex);

			i915_gem_object_lock(obj, NULL);
			GEM_WARN_ON(i915_vma_unbind(vma));
			i915_gem_object_unlock(obj);
			i915_gem_object_put(obj);

			vm->skip_pte_rewrite = save_skip_rewrite;
			goto retry;
		}

		if (!i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND)) {
			i915_vma_wait_for_bind(vma);

			__i915_vma_evict(vma, false);
			drm_mm_remove_node(&vma->node);
		}

		i915_gem_object_unlock(obj);
	}

	vm->clear_range(vm, 0, vm->total);

	vm->skip_pte_rewrite = save_skip_rewrite;

	mutex_unlock(&vm->mutex);
}

void i915_ggtt_suspend(struct i915_ggtt *ggtt)
{
	i915_ggtt_suspend_vm(&ggtt->vm);
	ggtt->invalidate(ggtt);

	intel_gt_check_and_clear_faults(ggtt->vm.gt);
}

void gen6_ggtt_invalidate(struct i915_ggtt *ggtt)
{
	struct intel_uncore *uncore = ggtt->vm.gt->uncore;

	spin_lock_irq(&uncore->lock);
	intel_uncore_write_fw(uncore, GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	intel_uncore_read_fw(uncore, GFX_FLSH_CNTL_GEN6);
	spin_unlock_irq(&uncore->lock);
}

void gen8_ggtt_invalidate(struct i915_ggtt *ggtt)
{
	struct intel_uncore *uncore = ggtt->vm.gt->uncore;

	/*
	 * Note that as an uncached mmio write, this will flush the
	 * WCB of the writes into the GGTT before it triggers the invalidate.
	 */
	intel_uncore_write_fw(uncore, GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
}

static void guc_ggtt_invalidate(struct i915_ggtt *ggtt)
{
	struct intel_uncore *uncore = ggtt->vm.gt->uncore;
	struct drm_i915_private *i915 = ggtt->vm.i915;

	gen8_ggtt_invalidate(ggtt);

	if (GRAPHICS_VER(i915) >= 12)
		intel_uncore_write_fw(uncore, GEN12_GUC_TLB_INV_CR,
				      GEN12_GUC_TLB_INV_CR_INVALIDATE);
	else
		intel_uncore_write_fw(uncore, GEN8_GTCR, GEN8_GTCR_INVALIDATE);
}

u64 gen8_ggtt_pte_encode(dma_addr_t addr,
			 enum i915_cache_level level,
			 u32 flags)
{
	gen8_pte_t pte = addr | GEN8_PAGE_PRESENT;

	if (flags & PTE_LM)
		pte |= GEN12_GGTT_PTE_LM;

	return pte;
}

void intel_ggtt_bind_vma(struct i915_address_space *vm,
			  struct i915_vm_pt_stash *stash,
			  struct i915_vma_resource *vma_res,
			  enum i915_cache_level cache_level,
			  u32 flags)
{
	u32 pte_flags;

	if (vma_res->bound_flags & (~flags & I915_VMA_BIND_MASK))
		return;

	vma_res->bound_flags |= flags;

	/* Applicable to VLV (gen8+ do not support RO in the GGTT) */
	pte_flags = 0;
	if (vma_res->bi.readonly)
		pte_flags |= PTE_READ_ONLY;
	if (vma_res->bi.lmem)
		pte_flags |= PTE_LM;

	vm->insert_entries(vm, vma_res, cache_level, pte_flags);
	vma_res->page_sizes_gtt = I915_GTT_PAGE_SIZE;
}

void intel_ggtt_unbind_vma(struct i915_address_space *vm,
			    struct i915_vma_resource *vma_res)
{
	vm->clear_range(vm, vma_res->start, vma_res->vma_size);
}

static int ggtt_reserve_guc_top(struct i915_ggtt *ggtt)
{
	u64 size;
	int ret;

	if (!intel_uc_uses_guc(&ggtt->vm.gt->uc))
		return 0;

	GEM_BUG_ON(ggtt->vm.total <= GUC_GGTT_TOP);
	size = ggtt->vm.total - GUC_GGTT_TOP;

	ret = i915_gem_gtt_reserve(&ggtt->vm, NULL, &ggtt->uc_fw, size,
				   GUC_GGTT_TOP, I915_COLOR_UNEVICTABLE,
				   PIN_NOEVICT);
	if (ret)
		drm_dbg(&ggtt->vm.i915->drm,
			"Failed to reserve top of GGTT for GuC\n");

	return ret;
}

static void ggtt_release_guc_top(struct i915_ggtt *ggtt)
{
	if (drm_mm_node_allocated(&ggtt->uc_fw))
		drm_mm_remove_node(&ggtt->uc_fw);
}

static void cleanup_init_ggtt(struct i915_ggtt *ggtt)
{
	ggtt_release_guc_top(ggtt);
	if (drm_mm_node_allocated(&ggtt->error_capture))
		drm_mm_remove_node(&ggtt->error_capture);
	mutex_destroy(&ggtt->error_mutex);
}

static int init_ggtt(struct i915_ggtt *ggtt)
{
	/*
	 * Let GEM Manage all of the aperture.
	 *
	 * However, leave one page at the end still bound to the scratch page.
	 * There are a number of places where the hardware apparently prefetches
	 * past the end of the object, and we've seen multiple hangs with the
	 * GPU head pointer stuck in a batchbuffer bound at the last page of the
	 * aperture.  One page should be enough to keep any prefetching inside
	 * of the aperture.
	 */
	unsigned long hole_start, hole_end;
	struct drm_mm_node *entry;
	int ret;

	/*
	 * GuC requires all resources that we're sharing with it to be placed in
	 * non-WOPCM memory. If GuC is not present or not in use we still need a
	 * small bias as ring wraparound at offset 0 sometimes hangs. No idea
	 * why.
	 */
	ggtt->pin_bias = max_t(u32, I915_GTT_PAGE_SIZE,
			       intel_wopcm_guc_size(&ggtt->vm.i915->wopcm));

	ret = intel_vgt_balloon(ggtt);
	if (ret)
		return ret;

	mutex_init(&ggtt->error_mutex);
	if (ggtt->mappable_end) {
		/*
		 * Reserve a mappable slot for our lockless error capture.
		 *
		 * We strongly prefer taking address 0x0 in order to protect
		 * other critical buffers against accidental overwrites,
		 * as writing to address 0 is a very common mistake.
		 *
		 * Since 0 may already be in use by the system (e.g. the BIOS
		 * framebuffer), we let the reservation fail quietly and hope
		 * 0 remains reserved always.
		 *
		 * If we fail to reserve 0, and then fail to find any space
		 * for an error-capture, remain silent. We can afford not
		 * to reserve an error_capture node as we have fallback
		 * paths, and we trust that 0 will remain reserved. However,
		 * the only likely reason for failure to insert is a driver
		 * bug, which we expect to cause other failures...
		 */
		ggtt->error_capture.size = I915_GTT_PAGE_SIZE;
		ggtt->error_capture.color = I915_COLOR_UNEVICTABLE;
		if (drm_mm_reserve_node(&ggtt->vm.mm, &ggtt->error_capture))
			drm_mm_insert_node_in_range(&ggtt->vm.mm,
						    &ggtt->error_capture,
						    ggtt->error_capture.size, 0,
						    ggtt->error_capture.color,
						    0, ggtt->mappable_end,
						    DRM_MM_INSERT_LOW);
	}
	if (drm_mm_node_allocated(&ggtt->error_capture))
		drm_dbg(&ggtt->vm.i915->drm,
			"Reserved GGTT:[%llx, %llx] for use by error capture\n",
			ggtt->error_capture.start,
			ggtt->error_capture.start + ggtt->error_capture.size);

	/*
	 * The upper portion of the GuC address space has a sizeable hole
	 * (several MB) that is inaccessible by GuC. Reserve this range within
	 * GGTT as it can comfortably hold GuC/HuC firmware images.
	 */
	ret = ggtt_reserve_guc_top(ggtt);
	if (ret)
		goto err;

	/* Clear any non-preallocated blocks */
	drm_mm_for_each_hole(entry, &ggtt->vm.mm, hole_start, hole_end) {
		drm_dbg(&ggtt->vm.i915->drm,
			"clearing unused GTT space: [%lx, %lx]\n",
			hole_start, hole_end);
		ggtt->vm.clear_range(&ggtt->vm, hole_start,
				     hole_end - hole_start);
	}

	/* And finally clear the reserved guard page */
	ggtt->vm.clear_range(&ggtt->vm, ggtt->vm.total - PAGE_SIZE, PAGE_SIZE);

	return 0;

err:
	cleanup_init_ggtt(ggtt);
	return ret;
}

static void aliasing_gtt_bind_vma(struct i915_address_space *vm,
				  struct i915_vm_pt_stash *stash,
				  struct i915_vma_resource *vma_res,
				  enum i915_cache_level cache_level,
				  u32 flags)
{
	u32 pte_flags;

	/* Currently applicable only to VLV */
	pte_flags = 0;
	if (vma_res->bi.readonly)
		pte_flags |= PTE_READ_ONLY;

	if (flags & I915_VMA_LOCAL_BIND)
		ppgtt_bind_vma(&i915_vm_to_ggtt(vm)->alias->vm,
			       stash, vma_res, cache_level, flags);

	if (flags & I915_VMA_GLOBAL_BIND)
		vm->insert_entries(vm, vma_res, cache_level, pte_flags);

	vma_res->bound_flags |= flags;
}

static void aliasing_gtt_unbind_vma(struct i915_address_space *vm,
				    struct i915_vma_resource *vma_res)
{
	if (vma_res->bound_flags & I915_VMA_GLOBAL_BIND)
		vm->clear_range(vm, vma_res->start, vma_res->vma_size);

	if (vma_res->bound_flags & I915_VMA_LOCAL_BIND)
		ppgtt_unbind_vma(&i915_vm_to_ggtt(vm)->alias->vm, vma_res);
}

static int init_aliasing_ppgtt(struct i915_ggtt *ggtt)
{
	struct i915_vm_pt_stash stash = {};
	struct i915_ppgtt *ppgtt;
	int err;

	ppgtt = i915_ppgtt_create(ggtt->vm.gt, 0);
	if (IS_ERR(ppgtt))
		return PTR_ERR(ppgtt);

	if (GEM_WARN_ON(ppgtt->vm.total < ggtt->vm.total)) {
		err = -ENODEV;
		goto err_ppgtt;
	}

	err = i915_vm_alloc_pt_stash(&ppgtt->vm, &stash, ggtt->vm.total);
	if (err)
		goto err_ppgtt;

	i915_gem_object_lock(ppgtt->vm.scratch[0], NULL);
	err = i915_vm_map_pt_stash(&ppgtt->vm, &stash);
	i915_gem_object_unlock(ppgtt->vm.scratch[0]);
	if (err)
		goto err_stash;

	/*
	 * Note we only pre-allocate as far as the end of the global
	 * GTT. On 48b / 4-level page-tables, the difference is very,
	 * very significant! We have to preallocate as GVT/vgpu does
	 * not like the page directory disappearing.
	 */
	ppgtt->vm.allocate_va_range(&ppgtt->vm, &stash, 0, ggtt->vm.total);

	ggtt->alias = ppgtt;
	ggtt->vm.bind_async_flags |= ppgtt->vm.bind_async_flags;

	GEM_BUG_ON(ggtt->vm.vma_ops.bind_vma != intel_ggtt_bind_vma);
	ggtt->vm.vma_ops.bind_vma = aliasing_gtt_bind_vma;

	GEM_BUG_ON(ggtt->vm.vma_ops.unbind_vma != intel_ggtt_unbind_vma);
	ggtt->vm.vma_ops.unbind_vma = aliasing_gtt_unbind_vma;

	i915_vm_free_pt_stash(&ppgtt->vm, &stash);
	return 0;

err_stash:
	i915_vm_free_pt_stash(&ppgtt->vm, &stash);
err_ppgtt:
	i915_vm_put(&ppgtt->vm);
	return err;
}

static void fini_aliasing_ppgtt(struct i915_ggtt *ggtt)
{
	struct i915_ppgtt *ppgtt;

	ppgtt = fetch_and_zero(&ggtt->alias);
	if (!ppgtt)
		return;

	i915_vm_put(&ppgtt->vm);

	ggtt->vm.vma_ops.bind_vma   = intel_ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma = intel_ggtt_unbind_vma;
}

int i915_init_ggtt(struct drm_i915_private *i915)
{
	int ret;

	ret = init_ggtt(to_gt(i915)->ggtt);
	if (ret)
		return ret;

	if (INTEL_PPGTT(i915) == INTEL_PPGTT_ALIASING) {
		ret = init_aliasing_ppgtt(to_gt(i915)->ggtt);
		if (ret)
			cleanup_init_ggtt(to_gt(i915)->ggtt);
	}

	return 0;
}

static void ggtt_cleanup_hw(struct i915_ggtt *ggtt)
{
	struct i915_vma *vma, *vn;

	flush_workqueue(ggtt->vm.i915->wq);
	i915_gem_drain_freed_objects(ggtt->vm.i915);

	mutex_lock(&ggtt->vm.mutex);

	ggtt->vm.skip_pte_rewrite = true;

	list_for_each_entry_safe(vma, vn, &ggtt->vm.bound_list, vm_link) {
		struct drm_i915_gem_object *obj = vma->obj;
		bool trylock;

		trylock = i915_gem_object_trylock(obj, NULL);
		WARN_ON(!trylock);

		WARN_ON(__i915_vma_unbind(vma));
		if (trylock)
			i915_gem_object_unlock(obj);
	}

	if (drm_mm_node_allocated(&ggtt->error_capture))
		drm_mm_remove_node(&ggtt->error_capture);
	mutex_destroy(&ggtt->error_mutex);

	ggtt_release_guc_top(ggtt);
	intel_vgt_deballoon(ggtt);

	ggtt->vm.cleanup(&ggtt->vm);

	mutex_unlock(&ggtt->vm.mutex);
	i915_address_space_fini(&ggtt->vm);

	arch_phys_wc_del(ggtt->mtrr);

	if (ggtt->iomap.size)
		io_mapping_fini(&ggtt->iomap);
}

/**
 * i915_ggtt_driver_release - Clean up GGTT hardware initialization
 * @i915: i915 device
 */
void i915_ggtt_driver_release(struct drm_i915_private *i915)
{
	struct i915_ggtt *ggtt = to_gt(i915)->ggtt;

	fini_aliasing_ppgtt(ggtt);

	intel_ggtt_fini_fences(ggtt);
	ggtt_cleanup_hw(ggtt);
}

/**
 * i915_ggtt_driver_late_release - Cleanup of GGTT that needs to be done after
 * all free objects have been drained.
 * @i915: i915 device
 */
void i915_ggtt_driver_late_release(struct drm_i915_private *i915)
{
	struct i915_ggtt *ggtt = to_gt(i915)->ggtt;

	GEM_WARN_ON(kref_read(&ggtt->vm.resv_ref) != 1);
	dma_resv_fini(&ggtt->vm._resv);
}

struct resource intel_pci_resource(struct pci_dev *pdev, int bar)
{
	return (struct resource)DEFINE_RES_MEM(pci_resource_start(pdev, bar),
					       pci_resource_len(pdev, bar));
}

static int ggtt_probe_hw(struct i915_ggtt *ggtt, struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	int ret;

	ggtt->vm.gt = gt;
	ggtt->vm.i915 = i915;
	ggtt->vm.dma = i915->drm.dev;
	dma_resv_init(&ggtt->vm._resv);

	if (GRAPHICS_VER(i915) <= 5)
		ret = intel_gt_gmch_gen5_probe(ggtt);
	else if (GRAPHICS_VER(i915) < 8)
		ret = intel_gt_gmch_gen6_probe(ggtt);
	else
		ret = intel_gt_gmch_gen8_probe(ggtt);
	if (ret) {
		dma_resv_fini(&ggtt->vm._resv);
		return ret;
	}

	if ((ggtt->vm.total - 1) >> 32) {
		drm_err(&i915->drm,
			"We never expected a Global GTT with more than 32bits"
			" of address space! Found %lldM!\n",
			ggtt->vm.total >> 20);
		ggtt->vm.total = 1ULL << 32;
		ggtt->mappable_end =
			min_t(u64, ggtt->mappable_end, ggtt->vm.total);
	}

	if (ggtt->mappable_end > ggtt->vm.total) {
		drm_err(&i915->drm,
			"mappable aperture extends past end of GGTT,"
			" aperture=%pa, total=%llx\n",
			&ggtt->mappable_end, ggtt->vm.total);
		ggtt->mappable_end = ggtt->vm.total;
	}

	/* GMADR is the PCI mmio aperture into the global GTT. */
	drm_dbg(&i915->drm, "GGTT size = %lluM\n", ggtt->vm.total >> 20);
	drm_dbg(&i915->drm, "GMADR size = %lluM\n",
		(u64)ggtt->mappable_end >> 20);
	drm_dbg(&i915->drm, "DSM size = %lluM\n",
		(u64)resource_size(&intel_graphics_stolen_res) >> 20);

	return 0;
}

/**
 * i915_ggtt_probe_hw - Probe GGTT hardware location
 * @i915: i915 device
 */
int i915_ggtt_probe_hw(struct drm_i915_private *i915)
{
	int ret;

	ret = ggtt_probe_hw(to_gt(i915)->ggtt, to_gt(i915));
	if (ret)
		return ret;

	if (i915_vtd_active(i915))
		drm_info(&i915->drm, "VT-d active for gfx access\n");

	return 0;
}

int i915_ggtt_enable_hw(struct drm_i915_private *i915)
{
	return intel_gt_gmch_gen5_enable_hw(i915);
}

void i915_ggtt_enable_guc(struct i915_ggtt *ggtt)
{
	GEM_BUG_ON(ggtt->invalidate != gen8_ggtt_invalidate);

	ggtt->invalidate = guc_ggtt_invalidate;

	ggtt->invalidate(ggtt);
}

void i915_ggtt_disable_guc(struct i915_ggtt *ggtt)
{
	/* XXX Temporary pardon for error unload */
	if (ggtt->invalidate == gen8_ggtt_invalidate)
		return;

	/* We should only be called after i915_ggtt_enable_guc() */
	GEM_BUG_ON(ggtt->invalidate != guc_ggtt_invalidate);

	ggtt->invalidate = gen8_ggtt_invalidate;

	ggtt->invalidate(ggtt);
}

/**
 * i915_ggtt_resume_vm - Restore the memory mappings for a GGTT or DPT VM
 * @vm: The VM to restore the mappings for
 *
 * Restore the memory mappings for all objects mapped to HW via the GGTT or a
 * DPT page table.
 *
 * Returns %true if restoring the mapping for any object that was in a write
 * domain before suspend.
 */
bool i915_ggtt_resume_vm(struct i915_address_space *vm)
{
	struct i915_vma *vma;
	bool write_domain_objs = false;

	drm_WARN_ON(&vm->i915->drm, !vm->is_ggtt && !vm->is_dpt);

	/* First fill our portion of the GTT with scratch pages */
	vm->clear_range(vm, 0, vm->total);

	/* clflush objects bound into the GGTT and rebind them. */
	list_for_each_entry(vma, &vm->bound_list, vm_link) {
		struct drm_i915_gem_object *obj = vma->obj;
		unsigned int was_bound =
			atomic_read(&vma->flags) & I915_VMA_BIND_MASK;

		GEM_BUG_ON(!was_bound);
		vma->ops->bind_vma(vm, NULL, vma->resource,
				   obj ? obj->cache_level : 0,
				   was_bound);
		if (obj) { /* only used during resume => exclusive access */
			write_domain_objs |= fetch_and_zero(&obj->write_domain);
			obj->read_domains |= I915_GEM_DOMAIN_GTT;
		}
	}

	return write_domain_objs;
}

void i915_ggtt_resume(struct i915_ggtt *ggtt)
{
	bool flush;

	intel_gt_check_and_clear_faults(ggtt->vm.gt);

	flush = i915_ggtt_resume_vm(&ggtt->vm);

	ggtt->invalidate(ggtt);

	if (flush)
		wbinvd_on_all_cpus();

	if (GRAPHICS_VER(ggtt->vm.i915) >= 8)
		setup_private_pat(ggtt->vm.gt->uncore);

	intel_ggtt_restore_fences(ggtt);
}
