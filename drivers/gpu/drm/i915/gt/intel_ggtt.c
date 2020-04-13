// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/stop_machine.h>

#include <asm/set_memory.h>
#include <asm/smp.h>

#include <drm/i915_drm.h>

#include "intel_gt.h"
#include "i915_drv.h"
#include "i915_scatterlist.h"
#include "i915_vgpu.h"

#include "intel_gtt.h"

static int
i915_get_ggtt_vma_pages(struct i915_vma *vma);

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

	i915_ggtt_init_fences(ggtt);

	return 0;
}

/**
 * i915_ggtt_init_hw - Initialize GGTT hardware
 * @i915: i915 device
 */
int i915_ggtt_init_hw(struct drm_i915_private *i915)
{
	int ret;

	stash_init(&i915->mm.wc_stash);

	/*
	 * Note that we use page colouring to enforce a guard page at the
	 * end of the address space. This is required as the CS may prefetch
	 * beyond the end of the batch buffer, across the page boundary,
	 * and beyond the end of the GTT if we do not provide a guard.
	 */
	ret = ggtt_init_hw(&i915->ggtt);
	if (ret)
		return ret;

	return 0;
}

/*
 * Certain Gen5 chipsets require require idling the GPU before
 * unmapping anything from the GTT when VT-d is enabled.
 */
static bool needs_idle_maps(struct drm_i915_private *i915)
{
	/*
	 * Query intel_iommu to see if we need the workaround. Presumably that
	 * was loaded first.
	 */
	return IS_GEN(i915, 5) && IS_MOBILE(i915) && intel_vtd_active();
}

void i915_ggtt_suspend(struct i915_ggtt *ggtt)
{
	struct i915_vma *vma;

	list_for_each_entry(vma, &ggtt->vm.bound_list, vm_link)
		i915_vma_wait_for_bind(vma);

	ggtt->vm.clear_range(&ggtt->vm, 0, ggtt->vm.total);
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

static void gen8_ggtt_invalidate(struct i915_ggtt *ggtt)
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

	if (INTEL_GEN(i915) >= 12)
		intel_uncore_write_fw(uncore, GEN12_GUC_TLB_INV_CR,
				      GEN12_GUC_TLB_INV_CR_INVALIDATE);
	else
		intel_uncore_write_fw(uncore, GEN8_GTCR, GEN8_GTCR_INVALIDATE);
}

static void gmch_ggtt_invalidate(struct i915_ggtt *ggtt)
{
	intel_gtt_chipset_flush();
}

static u64 gen8_ggtt_pte_encode(dma_addr_t addr,
				enum i915_cache_level level,
				u32 flags)
{
	return addr | _PAGE_PRESENT;
}

static void gen8_set_pte(void __iomem *addr, gen8_pte_t pte)
{
	writeq(pte, addr);
}

static void gen8_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  enum i915_cache_level level,
				  u32 unused)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen8_pte_t __iomem *pte =
		(gen8_pte_t __iomem *)ggtt->gsm + offset / I915_GTT_PAGE_SIZE;

	gen8_set_pte(pte, gen8_ggtt_pte_encode(addr, level, 0));

	ggtt->invalidate(ggtt);
}

static void gen8_ggtt_insert_entries(struct i915_address_space *vm,
				     struct i915_vma *vma,
				     enum i915_cache_level level,
				     u32 flags)
{
	const gen8_pte_t pte_encode = gen8_ggtt_pte_encode(0, level, 0);
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen8_pte_t __iomem *gte;
	gen8_pte_t __iomem *end;
	struct sgt_iter iter;
	dma_addr_t addr;

	/*
	 * Note that we ignore PTE_READ_ONLY here. The caller must be careful
	 * not to allow the user to override access to a read only page.
	 */

	gte = (gen8_pte_t __iomem *)ggtt->gsm;
	gte += vma->node.start / I915_GTT_PAGE_SIZE;
	end = gte + vma->node.size / I915_GTT_PAGE_SIZE;

	for_each_sgt_daddr(addr, iter, vma->pages)
		gen8_set_pte(gte++, pte_encode | addr);
	GEM_BUG_ON(gte > end);

	/* Fill the allocated but "unused" space beyond the end of the buffer */
	while (gte < end)
		gen8_set_pte(gte++, vm->scratch[0].encode);

	/*
	 * We want to flush the TLBs only after we're certain all the PTE
	 * updates have finished.
	 */
	ggtt->invalidate(ggtt);
}

static void gen6_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  enum i915_cache_level level,
				  u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen6_pte_t __iomem *pte =
		(gen6_pte_t __iomem *)ggtt->gsm + offset / I915_GTT_PAGE_SIZE;

	iowrite32(vm->pte_encode(addr, level, flags), pte);

	ggtt->invalidate(ggtt);
}

/*
 * Binds an object into the global gtt with the specified cache level.
 * The object will be accessible to the GPU via commands whose operands
 * reference offsets within the global GTT as well as accessible by the GPU
 * through the GMADR mapped BAR (i915->mm.gtt->gtt).
 */
static void gen6_ggtt_insert_entries(struct i915_address_space *vm,
				     struct i915_vma *vma,
				     enum i915_cache_level level,
				     u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen6_pte_t __iomem *gte;
	gen6_pte_t __iomem *end;
	struct sgt_iter iter;
	dma_addr_t addr;

	gte = (gen6_pte_t __iomem *)ggtt->gsm;
	gte += vma->node.start / I915_GTT_PAGE_SIZE;
	end = gte + vma->node.size / I915_GTT_PAGE_SIZE;

	for_each_sgt_daddr(addr, iter, vma->pages)
		iowrite32(vm->pte_encode(addr, level, flags), gte++);
	GEM_BUG_ON(gte > end);

	/* Fill the allocated but "unused" space beyond the end of the buffer */
	while (gte < end)
		iowrite32(vm->scratch[0].encode, gte++);

	/*
	 * We want to flush the TLBs only after we're certain all the PTE
	 * updates have finished.
	 */
	ggtt->invalidate(ggtt);
}

static void nop_clear_range(struct i915_address_space *vm,
			    u64 start, u64 length)
{
}

static void gen8_ggtt_clear_range(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	unsigned int first_entry = start / I915_GTT_PAGE_SIZE;
	unsigned int num_entries = length / I915_GTT_PAGE_SIZE;
	const gen8_pte_t scratch_pte = vm->scratch[0].encode;
	gen8_pte_t __iomem *gtt_base =
		(gen8_pte_t __iomem *)ggtt->gsm + first_entry;
	const int max_entries = ggtt_total_entries(ggtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	for (i = 0; i < num_entries; i++)
		gen8_set_pte(&gtt_base[i], scratch_pte);
}

static void bxt_vtd_ggtt_wa(struct i915_address_space *vm)
{
	/*
	 * Make sure the internal GAM fifo has been cleared of all GTT
	 * writes before exiting stop_machine(). This guarantees that
	 * any aperture accesses waiting to start in another process
	 * cannot back up behind the GTT writes causing a hang.
	 * The register can be any arbitrary GAM register.
	 */
	intel_uncore_posting_read_fw(vm->gt->uncore, GFX_FLSH_CNTL_GEN6);
}

struct insert_page {
	struct i915_address_space *vm;
	dma_addr_t addr;
	u64 offset;
	enum i915_cache_level level;
};

static int bxt_vtd_ggtt_insert_page__cb(void *_arg)
{
	struct insert_page *arg = _arg;

	gen8_ggtt_insert_page(arg->vm, arg->addr, arg->offset, arg->level, 0);
	bxt_vtd_ggtt_wa(arg->vm);

	return 0;
}

static void bxt_vtd_ggtt_insert_page__BKL(struct i915_address_space *vm,
					  dma_addr_t addr,
					  u64 offset,
					  enum i915_cache_level level,
					  u32 unused)
{
	struct insert_page arg = { vm, addr, offset, level };

	stop_machine(bxt_vtd_ggtt_insert_page__cb, &arg, NULL);
}

struct insert_entries {
	struct i915_address_space *vm;
	struct i915_vma *vma;
	enum i915_cache_level level;
	u32 flags;
};

static int bxt_vtd_ggtt_insert_entries__cb(void *_arg)
{
	struct insert_entries *arg = _arg;

	gen8_ggtt_insert_entries(arg->vm, arg->vma, arg->level, arg->flags);
	bxt_vtd_ggtt_wa(arg->vm);

	return 0;
}

static void bxt_vtd_ggtt_insert_entries__BKL(struct i915_address_space *vm,
					     struct i915_vma *vma,
					     enum i915_cache_level level,
					     u32 flags)
{
	struct insert_entries arg = { vm, vma, level, flags };

	stop_machine(bxt_vtd_ggtt_insert_entries__cb, &arg, NULL);
}

static void gen6_ggtt_clear_range(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	unsigned int first_entry = start / I915_GTT_PAGE_SIZE;
	unsigned int num_entries = length / I915_GTT_PAGE_SIZE;
	gen6_pte_t scratch_pte, __iomem *gtt_base =
		(gen6_pte_t __iomem *)ggtt->gsm + first_entry;
	const int max_entries = ggtt_total_entries(ggtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = vm->scratch[0].encode;
	for (i = 0; i < num_entries; i++)
		iowrite32(scratch_pte, &gtt_base[i]);
}

static void i915_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  enum i915_cache_level cache_level,
				  u32 unused)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gtt_insert_page(addr, offset >> PAGE_SHIFT, flags);
}

static void i915_ggtt_insert_entries(struct i915_address_space *vm,
				     struct i915_vma *vma,
				     enum i915_cache_level cache_level,
				     u32 unused)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gtt_insert_sg_entries(vma->pages, vma->node.start >> PAGE_SHIFT,
				    flags);
}

static void i915_ggtt_clear_range(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	intel_gtt_clear_range(start >> PAGE_SHIFT, length >> PAGE_SHIFT);
}

static int ggtt_bind_vma(struct i915_vma *vma,
			 enum i915_cache_level cache_level,
			 u32 flags)
{
	struct drm_i915_gem_object *obj = vma->obj;
	u32 pte_flags;

	/* Applicable to VLV (gen8+ do not support RO in the GGTT) */
	pte_flags = 0;
	if (i915_gem_object_is_readonly(obj))
		pte_flags |= PTE_READ_ONLY;

	vma->vm->insert_entries(vma->vm, vma, cache_level, pte_flags);

	vma->page_sizes.gtt = I915_GTT_PAGE_SIZE;

	/*
	 * Without aliasing PPGTT there's no difference between
	 * GLOBAL/LOCAL_BIND, it's all the same ptes. Hence unconditionally
	 * upgrade to both bound if we bind either to avoid double-binding.
	 */
	atomic_or(I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND, &vma->flags);

	return 0;
}

static void ggtt_unbind_vma(struct i915_vma *vma)
{
	vma->vm->clear_range(vma->vm, vma->node.start, vma->size);
}

static int ggtt_reserve_guc_top(struct i915_ggtt *ggtt)
{
	u64 size;
	int ret;

	if (!intel_uc_uses_guc(&ggtt->vm.gt->uc))
		return 0;

	GEM_BUG_ON(ggtt->vm.total <= GUC_GGTT_TOP);
	size = ggtt->vm.total - GUC_GGTT_TOP;

	ret = i915_gem_gtt_reserve(&ggtt->vm, &ggtt->uc_fw, size,
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
		/* Reserve a mappable slot for our lockless error capture */
		ret = drm_mm_insert_node_in_range(&ggtt->vm.mm,
						  &ggtt->error_capture,
						  PAGE_SIZE, 0,
						  I915_COLOR_UNEVICTABLE,
						  0, ggtt->mappable_end,
						  DRM_MM_INSERT_LOW);
		if (ret)
			return ret;
	}

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
		drm_dbg_kms(&ggtt->vm.i915->drm,
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

static int aliasing_gtt_bind_vma(struct i915_vma *vma,
				 enum i915_cache_level cache_level,
				 u32 flags)
{
	u32 pte_flags;
	int ret;

	/* Currently applicable only to VLV */
	pte_flags = 0;
	if (i915_gem_object_is_readonly(vma->obj))
		pte_flags |= PTE_READ_ONLY;

	if (flags & I915_VMA_LOCAL_BIND) {
		struct i915_ppgtt *alias = i915_vm_to_ggtt(vma->vm)->alias;

		if (flags & I915_VMA_ALLOC) {
			ret = alias->vm.allocate_va_range(&alias->vm,
							  vma->node.start,
							  vma->size);
			if (ret)
				return ret;

			set_bit(I915_VMA_ALLOC_BIT, __i915_vma_flags(vma));
		}

		GEM_BUG_ON(!test_bit(I915_VMA_ALLOC_BIT,
				     __i915_vma_flags(vma)));
		alias->vm.insert_entries(&alias->vm, vma,
					 cache_level, pte_flags);
	}

	if (flags & I915_VMA_GLOBAL_BIND)
		vma->vm->insert_entries(vma->vm, vma, cache_level, pte_flags);

	return 0;
}

static void aliasing_gtt_unbind_vma(struct i915_vma *vma)
{
	if (i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND)) {
		struct i915_address_space *vm = vma->vm;

		vm->clear_range(vm, vma->node.start, vma->size);
	}

	if (test_and_clear_bit(I915_VMA_ALLOC_BIT, __i915_vma_flags(vma))) {
		struct i915_address_space *vm =
			&i915_vm_to_ggtt(vma->vm)->alias->vm;

		vm->clear_range(vm, vma->node.start, vma->size);
	}
}

static int init_aliasing_ppgtt(struct i915_ggtt *ggtt)
{
	struct i915_ppgtt *ppgtt;
	int err;

	ppgtt = i915_ppgtt_create(ggtt->vm.gt);
	if (IS_ERR(ppgtt))
		return PTR_ERR(ppgtt);

	if (GEM_WARN_ON(ppgtt->vm.total < ggtt->vm.total)) {
		err = -ENODEV;
		goto err_ppgtt;
	}

	/*
	 * Note we only pre-allocate as far as the end of the global
	 * GTT. On 48b / 4-level page-tables, the difference is very,
	 * very significant! We have to preallocate as GVT/vgpu does
	 * not like the page directory disappearing.
	 */
	err = ppgtt->vm.allocate_va_range(&ppgtt->vm, 0, ggtt->vm.total);
	if (err)
		goto err_ppgtt;

	ggtt->alias = ppgtt;
	ggtt->vm.bind_async_flags |= ppgtt->vm.bind_async_flags;

	GEM_BUG_ON(ggtt->vm.vma_ops.bind_vma != ggtt_bind_vma);
	ggtt->vm.vma_ops.bind_vma = aliasing_gtt_bind_vma;

	GEM_BUG_ON(ggtt->vm.vma_ops.unbind_vma != ggtt_unbind_vma);
	ggtt->vm.vma_ops.unbind_vma = aliasing_gtt_unbind_vma;

	return 0;

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

	ggtt->vm.vma_ops.bind_vma   = ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma = ggtt_unbind_vma;
}

int i915_init_ggtt(struct drm_i915_private *i915)
{
	int ret;

	ret = init_ggtt(&i915->ggtt);
	if (ret)
		return ret;

	if (INTEL_PPGTT(i915) == INTEL_PPGTT_ALIASING) {
		ret = init_aliasing_ppgtt(&i915->ggtt);
		if (ret)
			cleanup_init_ggtt(&i915->ggtt);
	}

	return 0;
}

static void ggtt_cleanup_hw(struct i915_ggtt *ggtt)
{
	struct i915_vma *vma, *vn;

	atomic_set(&ggtt->vm.open, 0);

	rcu_barrier(); /* flush the RCU'ed__i915_vm_release */
	flush_workqueue(ggtt->vm.i915->wq);

	mutex_lock(&ggtt->vm.mutex);

	list_for_each_entry_safe(vma, vn, &ggtt->vm.bound_list, vm_link)
		WARN_ON(__i915_vma_unbind(vma));

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
	struct pagevec *pvec;

	fini_aliasing_ppgtt(&i915->ggtt);

	ggtt_cleanup_hw(&i915->ggtt);

	pvec = &i915->mm.wc_stash.pvec;
	if (pvec->nr) {
		set_pages_array_wb(pvec->pages, pvec->nr);
		__pagevec_release(pvec);
	}
}

static unsigned int gen6_get_total_gtt_size(u16 snb_gmch_ctl)
{
	snb_gmch_ctl >>= SNB_GMCH_GGMS_SHIFT;
	snb_gmch_ctl &= SNB_GMCH_GGMS_MASK;
	return snb_gmch_ctl << 20;
}

static unsigned int gen8_get_total_gtt_size(u16 bdw_gmch_ctl)
{
	bdw_gmch_ctl >>= BDW_GMCH_GGMS_SHIFT;
	bdw_gmch_ctl &= BDW_GMCH_GGMS_MASK;
	if (bdw_gmch_ctl)
		bdw_gmch_ctl = 1 << bdw_gmch_ctl;

#ifdef CONFIG_X86_32
	/* Limit 32b platforms to a 2GB GGTT: 4 << 20 / pte size * I915_GTT_PAGE_SIZE */
	if (bdw_gmch_ctl > 4)
		bdw_gmch_ctl = 4;
#endif

	return bdw_gmch_ctl << 20;
}

static unsigned int chv_get_total_gtt_size(u16 gmch_ctrl)
{
	gmch_ctrl >>= SNB_GMCH_GGMS_SHIFT;
	gmch_ctrl &= SNB_GMCH_GGMS_MASK;

	if (gmch_ctrl)
		return 1 << (20 + gmch_ctrl);

	return 0;
}

static int ggtt_probe_common(struct i915_ggtt *ggtt, u64 size)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	struct pci_dev *pdev = i915->drm.pdev;
	phys_addr_t phys_addr;
	int ret;

	/* For Modern GENs the PTEs and register space are split in the BAR */
	phys_addr = pci_resource_start(pdev, 0) + pci_resource_len(pdev, 0) / 2;

	/*
	 * On BXT+/CNL+ writes larger than 64 bit to the GTT pagetable range
	 * will be dropped. For WC mappings in general we have 64 byte burst
	 * writes when the WC buffer is flushed, so we can't use it, but have to
	 * resort to an uncached mapping. The WC issue is easily caught by the
	 * readback check when writing GTT PTE entries.
	 */
	if (IS_GEN9_LP(i915) || INTEL_GEN(i915) >= 10)
		ggtt->gsm = ioremap(phys_addr, size);
	else
		ggtt->gsm = ioremap_wc(phys_addr, size);
	if (!ggtt->gsm) {
		DRM_ERROR("Failed to map the ggtt page table\n");
		return -ENOMEM;
	}

	ret = setup_scratch_page(&ggtt->vm, GFP_DMA32);
	if (ret) {
		DRM_ERROR("Scratch setup failed\n");
		/* iounmap will also get called at remove, but meh */
		iounmap(ggtt->gsm);
		return ret;
	}

	ggtt->vm.scratch[0].encode =
		ggtt->vm.pte_encode(px_dma(&ggtt->vm.scratch[0]),
				    I915_CACHE_NONE, 0);

	return 0;
}

int ggtt_set_pages(struct i915_vma *vma)
{
	int ret;

	GEM_BUG_ON(vma->pages);

	ret = i915_get_ggtt_vma_pages(vma);
	if (ret)
		return ret;

	vma->page_sizes = vma->obj->mm.page_sizes;

	return 0;
}

static void gen6_gmch_remove(struct i915_address_space *vm)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);

	iounmap(ggtt->gsm);
	cleanup_scratch_page(vm);
}

static struct resource pci_resource(struct pci_dev *pdev, int bar)
{
	return (struct resource)DEFINE_RES_MEM(pci_resource_start(pdev, bar),
					       pci_resource_len(pdev, bar));
}

static int gen8_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	struct pci_dev *pdev = i915->drm.pdev;
	unsigned int size;
	u16 snb_gmch_ctl;
	int err;

	/* TODO: We're not aware of mappable constraints on gen8 yet */
	if (!IS_DGFX(i915)) {
		ggtt->gmadr = pci_resource(pdev, 2);
		ggtt->mappable_end = resource_size(&ggtt->gmadr);
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(39));
	if (!err)
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(39));
	if (err)
		DRM_ERROR("Can't set DMA mask/consistent mask (%d)\n", err);

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);
	if (IS_CHERRYVIEW(i915))
		size = chv_get_total_gtt_size(snb_gmch_ctl);
	else
		size = gen8_get_total_gtt_size(snb_gmch_ctl);

	ggtt->vm.total = (size / sizeof(gen8_pte_t)) * I915_GTT_PAGE_SIZE;
	ggtt->vm.cleanup = gen6_gmch_remove;
	ggtt->vm.insert_page = gen8_ggtt_insert_page;
	ggtt->vm.clear_range = nop_clear_range;
	if (intel_scanout_needs_vtd_wa(i915))
		ggtt->vm.clear_range = gen8_ggtt_clear_range;

	ggtt->vm.insert_entries = gen8_ggtt_insert_entries;

	/* Serialize GTT updates with aperture access on BXT if VT-d is on. */
	if (intel_ggtt_update_needs_vtd_wa(i915) ||
	    IS_CHERRYVIEW(i915) /* fails with concurrent use/update */) {
		ggtt->vm.insert_entries = bxt_vtd_ggtt_insert_entries__BKL;
		ggtt->vm.insert_page    = bxt_vtd_ggtt_insert_page__BKL;
		ggtt->vm.bind_async_flags =
			I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND;
	}

	ggtt->invalidate = gen8_ggtt_invalidate;

	ggtt->vm.vma_ops.bind_vma    = ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma  = ggtt_unbind_vma;
	ggtt->vm.vma_ops.set_pages   = ggtt_set_pages;
	ggtt->vm.vma_ops.clear_pages = clear_pages;

	ggtt->vm.pte_encode = gen8_ggtt_pte_encode;

	setup_private_pat(ggtt->vm.gt->uncore);

	return ggtt_probe_common(ggtt, size);
}

static u64 snb_pte_encode(dma_addr_t addr,
			  enum i915_cache_level level,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	switch (level) {
	case I915_CACHE_L3_LLC:
	case I915_CACHE_LLC:
		pte |= GEN6_PTE_CACHE_LLC;
		break;
	case I915_CACHE_NONE:
		pte |= GEN6_PTE_UNCACHED;
		break;
	default:
		MISSING_CASE(level);
	}

	return pte;
}

static u64 ivb_pte_encode(dma_addr_t addr,
			  enum i915_cache_level level,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	switch (level) {
	case I915_CACHE_L3_LLC:
		pte |= GEN7_PTE_CACHE_L3_LLC;
		break;
	case I915_CACHE_LLC:
		pte |= GEN6_PTE_CACHE_LLC;
		break;
	case I915_CACHE_NONE:
		pte |= GEN6_PTE_UNCACHED;
		break;
	default:
		MISSING_CASE(level);
	}

	return pte;
}

static u64 byt_pte_encode(dma_addr_t addr,
			  enum i915_cache_level level,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	if (!(flags & PTE_READ_ONLY))
		pte |= BYT_PTE_WRITEABLE;

	if (level != I915_CACHE_NONE)
		pte |= BYT_PTE_SNOOPED_BY_CPU_CACHES;

	return pte;
}

static u64 hsw_pte_encode(dma_addr_t addr,
			  enum i915_cache_level level,
			  u32 flags)
{
	gen6_pte_t pte = HSW_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	if (level != I915_CACHE_NONE)
		pte |= HSW_WB_LLC_AGE3;

	return pte;
}

static u64 iris_pte_encode(dma_addr_t addr,
			   enum i915_cache_level level,
			   u32 flags)
{
	gen6_pte_t pte = HSW_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	switch (level) {
	case I915_CACHE_NONE:
		break;
	case I915_CACHE_WT:
		pte |= HSW_WT_ELLC_LLC_AGE3;
		break;
	default:
		pte |= HSW_WB_ELLC_LLC_AGE3;
		break;
	}

	return pte;
}

static int gen6_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	struct pci_dev *pdev = i915->drm.pdev;
	unsigned int size;
	u16 snb_gmch_ctl;
	int err;

	ggtt->gmadr = pci_resource(pdev, 2);
	ggtt->mappable_end = resource_size(&ggtt->gmadr);

	/*
	 * 64/512MB is the current min/max we actually know of, but this is
	 * just a coarse sanity check.
	 */
	if (ggtt->mappable_end < (64<<20) || ggtt->mappable_end > (512<<20)) {
		DRM_ERROR("Unknown GMADR size (%pa)\n", &ggtt->mappable_end);
		return -ENXIO;
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(40));
	if (!err)
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(40));
	if (err)
		DRM_ERROR("Can't set DMA mask/consistent mask (%d)\n", err);
	pci_read_config_word(pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);

	size = gen6_get_total_gtt_size(snb_gmch_ctl);
	ggtt->vm.total = (size / sizeof(gen6_pte_t)) * I915_GTT_PAGE_SIZE;

	ggtt->vm.clear_range = nop_clear_range;
	if (!HAS_FULL_PPGTT(i915) || intel_scanout_needs_vtd_wa(i915))
		ggtt->vm.clear_range = gen6_ggtt_clear_range;
	ggtt->vm.insert_page = gen6_ggtt_insert_page;
	ggtt->vm.insert_entries = gen6_ggtt_insert_entries;
	ggtt->vm.cleanup = gen6_gmch_remove;

	ggtt->invalidate = gen6_ggtt_invalidate;

	if (HAS_EDRAM(i915))
		ggtt->vm.pte_encode = iris_pte_encode;
	else if (IS_HASWELL(i915))
		ggtt->vm.pte_encode = hsw_pte_encode;
	else if (IS_VALLEYVIEW(i915))
		ggtt->vm.pte_encode = byt_pte_encode;
	else if (INTEL_GEN(i915) >= 7)
		ggtt->vm.pte_encode = ivb_pte_encode;
	else
		ggtt->vm.pte_encode = snb_pte_encode;

	ggtt->vm.vma_ops.bind_vma    = ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma  = ggtt_unbind_vma;
	ggtt->vm.vma_ops.set_pages   = ggtt_set_pages;
	ggtt->vm.vma_ops.clear_pages = clear_pages;

	return ggtt_probe_common(ggtt, size);
}

static void i915_gmch_remove(struct i915_address_space *vm)
{
	intel_gmch_remove();
}

static int i915_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	phys_addr_t gmadr_base;
	int ret;

	ret = intel_gmch_probe(i915->bridge_dev, i915->drm.pdev, NULL);
	if (!ret) {
		DRM_ERROR("failed to set up gmch\n");
		return -EIO;
	}

	intel_gtt_get(&ggtt->vm.total, &gmadr_base, &ggtt->mappable_end);

	ggtt->gmadr =
		(struct resource)DEFINE_RES_MEM(gmadr_base, ggtt->mappable_end);

	ggtt->do_idle_maps = needs_idle_maps(i915);
	ggtt->vm.insert_page = i915_ggtt_insert_page;
	ggtt->vm.insert_entries = i915_ggtt_insert_entries;
	ggtt->vm.clear_range = i915_ggtt_clear_range;
	ggtt->vm.cleanup = i915_gmch_remove;

	ggtt->invalidate = gmch_ggtt_invalidate;

	ggtt->vm.vma_ops.bind_vma    = ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma  = ggtt_unbind_vma;
	ggtt->vm.vma_ops.set_pages   = ggtt_set_pages;
	ggtt->vm.vma_ops.clear_pages = clear_pages;

	if (unlikely(ggtt->do_idle_maps))
		dev_notice(i915->drm.dev,
			   "Applying Ironlake quirks for intel_iommu\n");

	return 0;
}

static int ggtt_probe_hw(struct i915_ggtt *ggtt, struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	int ret;

	ggtt->vm.gt = gt;
	ggtt->vm.i915 = i915;
	ggtt->vm.dma = &i915->drm.pdev->dev;

	if (INTEL_GEN(i915) <= 5)
		ret = i915_gmch_probe(ggtt);
	else if (INTEL_GEN(i915) < 8)
		ret = gen6_gmch_probe(ggtt);
	else
		ret = gen8_gmch_probe(ggtt);
	if (ret)
		return ret;

	if ((ggtt->vm.total - 1) >> 32) {
		DRM_ERROR("We never expected a Global GTT with more than 32bits"
			  " of address space! Found %lldM!\n",
			  ggtt->vm.total >> 20);
		ggtt->vm.total = 1ULL << 32;
		ggtt->mappable_end =
			min_t(u64, ggtt->mappable_end, ggtt->vm.total);
	}

	if (ggtt->mappable_end > ggtt->vm.total) {
		DRM_ERROR("mappable aperture extends past end of GGTT,"
			  " aperture=%pa, total=%llx\n",
			  &ggtt->mappable_end, ggtt->vm.total);
		ggtt->mappable_end = ggtt->vm.total;
	}

	/* GMADR is the PCI mmio aperture into the global GTT. */
	DRM_DEBUG_DRIVER("GGTT size = %lluM\n", ggtt->vm.total >> 20);
	DRM_DEBUG_DRIVER("GMADR size = %lluM\n", (u64)ggtt->mappable_end >> 20);
	DRM_DEBUG_DRIVER("DSM size = %lluM\n",
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

	ret = ggtt_probe_hw(&i915->ggtt, &i915->gt);
	if (ret)
		return ret;

	if (intel_vtd_active())
		dev_info(i915->drm.dev, "VT-d active for gfx access\n");

	return 0;
}

int i915_ggtt_enable_hw(struct drm_i915_private *i915)
{
	if (INTEL_GEN(i915) < 6 && !intel_enable_gtt())
		return -EIO;

	return 0;
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

void i915_ggtt_resume(struct i915_ggtt *ggtt)
{
	struct i915_vma *vma;
	bool flush = false;
	int open;

	intel_gt_check_and_clear_faults(ggtt->vm.gt);

	/* First fill our portion of the GTT with scratch pages */
	ggtt->vm.clear_range(&ggtt->vm, 0, ggtt->vm.total);

	/* Skip rewriting PTE on VMA unbind. */
	open = atomic_xchg(&ggtt->vm.open, 0);

	/* clflush objects bound into the GGTT and rebind them. */
	list_for_each_entry(vma, &ggtt->vm.bound_list, vm_link) {
		struct drm_i915_gem_object *obj = vma->obj;

		if (!i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND))
			continue;

		clear_bit(I915_VMA_GLOBAL_BIND_BIT, __i915_vma_flags(vma));
		WARN_ON(i915_vma_bind(vma,
				      obj ? obj->cache_level : 0,
				      PIN_GLOBAL, NULL));
		if (obj) { /* only used during resume => exclusive access */
			flush |= fetch_and_zero(&obj->write_domain);
			obj->read_domains |= I915_GEM_DOMAIN_GTT;
		}
	}

	atomic_set(&ggtt->vm.open, open);
	ggtt->invalidate(ggtt);

	if (flush)
		wbinvd_on_all_cpus();

	if (INTEL_GEN(ggtt->vm.i915) >= 8)
		setup_private_pat(ggtt->vm.gt->uncore);
}

static struct scatterlist *
rotate_pages(struct drm_i915_gem_object *obj, unsigned int offset,
	     unsigned int width, unsigned int height,
	     unsigned int stride,
	     struct sg_table *st, struct scatterlist *sg)
{
	unsigned int column, row;
	unsigned int src_idx;

	for (column = 0; column < width; column++) {
		src_idx = stride * (height - 1) + column + offset;
		for (row = 0; row < height; row++) {
			st->nents++;
			/*
			 * We don't need the pages, but need to initialize
			 * the entries so the sg list can be happily traversed.
			 * The only thing we need are DMA addresses.
			 */
			sg_set_page(sg, NULL, I915_GTT_PAGE_SIZE, 0);
			sg_dma_address(sg) =
				i915_gem_object_get_dma_address(obj, src_idx);
			sg_dma_len(sg) = I915_GTT_PAGE_SIZE;
			sg = sg_next(sg);
			src_idx -= stride;
		}
	}

	return sg;
}

static noinline struct sg_table *
intel_rotate_pages(struct intel_rotation_info *rot_info,
		   struct drm_i915_gem_object *obj)
{
	unsigned int size = intel_rotation_info_size(rot_info);
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct sg_table *st;
	struct scatterlist *sg;
	int ret = -ENOMEM;
	int i;

	/* Allocate target SG list. */
	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, size, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	st->nents = 0;
	sg = st->sgl;

	for (i = 0 ; i < ARRAY_SIZE(rot_info->plane); i++) {
		sg = rotate_pages(obj, rot_info->plane[i].offset,
				  rot_info->plane[i].width, rot_info->plane[i].height,
				  rot_info->plane[i].stride, st, sg);
	}

	return st;

err_sg_alloc:
	kfree(st);
err_st_alloc:

	drm_dbg(&i915->drm, "Failed to create rotated mapping for object size %zu! (%ux%u tiles, %u pages)\n",
		obj->base.size, rot_info->plane[0].width,
		rot_info->plane[0].height, size);

	return ERR_PTR(ret);
}

static struct scatterlist *
remap_pages(struct drm_i915_gem_object *obj, unsigned int offset,
	    unsigned int width, unsigned int height,
	    unsigned int stride,
	    struct sg_table *st, struct scatterlist *sg)
{
	unsigned int row;

	for (row = 0; row < height; row++) {
		unsigned int left = width * I915_GTT_PAGE_SIZE;

		while (left) {
			dma_addr_t addr;
			unsigned int length;

			/*
			 * We don't need the pages, but need to initialize
			 * the entries so the sg list can be happily traversed.
			 * The only thing we need are DMA addresses.
			 */

			addr = i915_gem_object_get_dma_address_len(obj, offset, &length);

			length = min(left, length);

			st->nents++;

			sg_set_page(sg, NULL, length, 0);
			sg_dma_address(sg) = addr;
			sg_dma_len(sg) = length;
			sg = sg_next(sg);

			offset += length / I915_GTT_PAGE_SIZE;
			left -= length;
		}

		offset += stride - width;
	}

	return sg;
}

static noinline struct sg_table *
intel_remap_pages(struct intel_remapped_info *rem_info,
		  struct drm_i915_gem_object *obj)
{
	unsigned int size = intel_remapped_info_size(rem_info);
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct sg_table *st;
	struct scatterlist *sg;
	int ret = -ENOMEM;
	int i;

	/* Allocate target SG list. */
	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, size, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	st->nents = 0;
	sg = st->sgl;

	for (i = 0 ; i < ARRAY_SIZE(rem_info->plane); i++) {
		sg = remap_pages(obj, rem_info->plane[i].offset,
				 rem_info->plane[i].width, rem_info->plane[i].height,
				 rem_info->plane[i].stride, st, sg);
	}

	i915_sg_trim(st);

	return st;

err_sg_alloc:
	kfree(st);
err_st_alloc:

	drm_dbg(&i915->drm, "Failed to create remapped mapping for object size %zu! (%ux%u tiles, %u pages)\n",
		obj->base.size, rem_info->plane[0].width,
		rem_info->plane[0].height, size);

	return ERR_PTR(ret);
}

static noinline struct sg_table *
intel_partial_pages(const struct i915_ggtt_view *view,
		    struct drm_i915_gem_object *obj)
{
	struct sg_table *st;
	struct scatterlist *sg, *iter;
	unsigned int count = view->partial.size;
	unsigned int offset;
	int ret = -ENOMEM;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, count, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	iter = i915_gem_object_get_sg(obj, view->partial.offset, &offset);
	GEM_BUG_ON(!iter);

	sg = st->sgl;
	st->nents = 0;
	do {
		unsigned int len;

		len = min(iter->length - (offset << PAGE_SHIFT),
			  count << PAGE_SHIFT);
		sg_set_page(sg, NULL, len, 0);
		sg_dma_address(sg) =
			sg_dma_address(iter) + (offset << PAGE_SHIFT);
		sg_dma_len(sg) = len;

		st->nents++;
		count -= len >> PAGE_SHIFT;
		if (count == 0) {
			sg_mark_end(sg);
			i915_sg_trim(st); /* Drop any unused tail entries. */

			return st;
		}

		sg = __sg_next(sg);
		iter = __sg_next(iter);
		offset = 0;
	} while (1);

err_sg_alloc:
	kfree(st);
err_st_alloc:
	return ERR_PTR(ret);
}

static int
i915_get_ggtt_vma_pages(struct i915_vma *vma)
{
	int ret;

	/*
	 * The vma->pages are only valid within the lifespan of the borrowed
	 * obj->mm.pages. When the obj->mm.pages sg_table is regenerated, so
	 * must be the vma->pages. A simple rule is that vma->pages must only
	 * be accessed when the obj->mm.pages are pinned.
	 */
	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(vma->obj));

	switch (vma->ggtt_view.type) {
	default:
		GEM_BUG_ON(vma->ggtt_view.type);
		/* fall through */
	case I915_GGTT_VIEW_NORMAL:
		vma->pages = vma->obj->mm.pages;
		return 0;

	case I915_GGTT_VIEW_ROTATED:
		vma->pages =
			intel_rotate_pages(&vma->ggtt_view.rotated, vma->obj);
		break;

	case I915_GGTT_VIEW_REMAPPED:
		vma->pages =
			intel_remap_pages(&vma->ggtt_view.remapped, vma->obj);
		break;

	case I915_GGTT_VIEW_PARTIAL:
		vma->pages = intel_partial_pages(&vma->ggtt_view, vma->obj);
		break;
	}

	ret = 0;
	if (IS_ERR(vma->pages)) {
		ret = PTR_ERR(vma->pages);
		vma->pages = NULL;
		drm_err(&vma->vm->i915->drm,
			"Failed to get pages for VMA view type %u (%d)!\n",
			vma->ggtt_view.type, ret);
	}
	return ret;
}
