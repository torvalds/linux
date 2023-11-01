// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <asm/set_memory.h>
#include <asm/smp.h>
#include <linux/types.h>
#include <linux/stop_machine.h>

#include <drm/drm_managed.h>
#include <drm/i915_drm.h>
#include <drm/intel-gtt.h>

#include "display/intel_display.h"
#include "gem/i915_gem_lmem.h"

#include "intel_context.h"
#include "intel_ggtt_gmch.h"
#include "intel_gpu_commands.h"
#include "intel_gt.h"
#include "intel_gt_regs.h"
#include "intel_pci_config.h"
#include "intel_ring.h"
#include "i915_drv.h"
#include "i915_pci.h"
#include "i915_request.h"
#include "i915_scatterlist.h"
#include "i915_utils.h"
#include "i915_vgpu.h"

#include "intel_gtt.h"
#include "gen8_ppgtt.h"
#include "intel_engine_pm.h"

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
	struct intel_gt *gt;

	i915_ggtt_suspend_vm(&ggtt->vm);
	ggtt->invalidate(ggtt);

	list_for_each_entry(gt, &ggtt->gt_list, ggtt_link)
		intel_gt_check_and_clear_faults(gt);
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

static void guc_ggtt_ct_invalidate(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;
	intel_wakeref_t wakeref;

	with_intel_runtime_pm_if_active(uncore->rpm, wakeref) {
		struct intel_guc *guc = &gt->uc.guc;

		intel_guc_invalidate_tlb_guc(guc);
	}
}

static void guc_ggtt_invalidate(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	struct intel_gt *gt;

	gen8_ggtt_invalidate(ggtt);

	list_for_each_entry(gt, &ggtt->gt_list, ggtt_link) {
		if (intel_guc_tlb_invalidation_is_available(&gt->uc.guc)) {
			guc_ggtt_ct_invalidate(gt);
		} else if (GRAPHICS_VER(i915) >= 12) {
			intel_uncore_write_fw(gt->uncore,
					      GEN12_GUC_TLB_INV_CR,
					      GEN12_GUC_TLB_INV_CR_INVALIDATE);
		} else {
			intel_uncore_write_fw(gt->uncore,
					      GEN8_GTCR, GEN8_GTCR_INVALIDATE);
		}
	}
}

static u64 mtl_ggtt_pte_encode(dma_addr_t addr,
			       unsigned int pat_index,
			       u32 flags)
{
	gen8_pte_t pte = addr | GEN8_PAGE_PRESENT;

	WARN_ON_ONCE(addr & ~GEN12_GGTT_PTE_ADDR_MASK);

	if (flags & PTE_LM)
		pte |= GEN12_GGTT_PTE_LM;

	if (pat_index & BIT(0))
		pte |= MTL_GGTT_PTE_PAT0;

	if (pat_index & BIT(1))
		pte |= MTL_GGTT_PTE_PAT1;

	return pte;
}

u64 gen8_ggtt_pte_encode(dma_addr_t addr,
			 unsigned int pat_index,
			 u32 flags)
{
	gen8_pte_t pte = addr | GEN8_PAGE_PRESENT;

	if (flags & PTE_LM)
		pte |= GEN12_GGTT_PTE_LM;

	return pte;
}

static bool should_update_ggtt_with_bind(struct i915_ggtt *ggtt)
{
	struct intel_gt *gt = ggtt->vm.gt;

	return intel_gt_is_bind_context_ready(gt);
}

static struct intel_context *gen8_ggtt_bind_get_ce(struct i915_ggtt *ggtt)
{
	struct intel_context *ce;
	struct intel_gt *gt = ggtt->vm.gt;

	if (intel_gt_is_wedged(gt))
		return NULL;

	ce = gt->engine[BCS0]->bind_context;
	GEM_BUG_ON(!ce);

	/*
	 * If the GT is not awake already at this stage then fallback
	 * to pci based GGTT update otherwise __intel_wakeref_get_first()
	 * would conflict with fs_reclaim trying to allocate memory while
	 * doing rpm_resume().
	 */
	if (!intel_gt_pm_get_if_awake(gt))
		return NULL;

	intel_engine_pm_get(ce->engine);

	return ce;
}

static void gen8_ggtt_bind_put_ce(struct intel_context *ce)
{
	intel_engine_pm_put(ce->engine);
	intel_gt_pm_put(ce->engine->gt);
}

static bool gen8_ggtt_bind_ptes(struct i915_ggtt *ggtt, u32 offset,
				struct sg_table *pages, u32 num_entries,
				const gen8_pte_t pte)
{
	struct i915_sched_attr attr = {};
	struct intel_gt *gt = ggtt->vm.gt;
	const gen8_pte_t scratch_pte = ggtt->vm.scratch[0]->encode;
	struct sgt_iter iter;
	struct i915_request *rq;
	struct intel_context *ce;
	u32 *cs;

	if (!num_entries)
		return true;

	ce = gen8_ggtt_bind_get_ce(ggtt);
	if (!ce)
		return false;

	if (pages)
		iter = __sgt_iter(pages->sgl, true);

	while (num_entries) {
		int count = 0;
		dma_addr_t addr;
		/*
		 * MI_UPDATE_GTT can update 512 entries in a single command but
		 * that end up with engine reset, 511 works.
		 */
		u32 n_ptes = min_t(u32, 511, num_entries);

		if (mutex_lock_interruptible(&ce->timeline->mutex))
			goto put_ce;

		intel_context_enter(ce);
		rq = __i915_request_create(ce, GFP_NOWAIT | GFP_ATOMIC);
		intel_context_exit(ce);
		if (IS_ERR(rq)) {
			GT_TRACE(gt, "Failed to get bind request\n");
			mutex_unlock(&ce->timeline->mutex);
			goto put_ce;
		}

		cs = intel_ring_begin(rq, 2 * n_ptes + 2);
		if (IS_ERR(cs)) {
			GT_TRACE(gt, "Failed to ring space for GGTT bind\n");
			i915_request_set_error_once(rq, PTR_ERR(cs));
			/* once a request is created, it must be queued */
			goto queue_err_rq;
		}

		*cs++ = MI_UPDATE_GTT | (2 * n_ptes);
		*cs++ = offset << 12;

		if (pages) {
			for_each_sgt_daddr_next(addr, iter) {
				if (count == n_ptes)
					break;
				*cs++ = lower_32_bits(pte | addr);
				*cs++ = upper_32_bits(pte | addr);
				count++;
			}
			/* fill remaining with scratch pte, if any */
			if (count < n_ptes) {
				memset64((u64 *)cs, scratch_pte,
					 n_ptes - count);
				cs += (n_ptes - count) * 2;
			}
		} else {
			memset64((u64 *)cs, pte, n_ptes);
			cs += n_ptes * 2;
		}

		intel_ring_advance(rq, cs);
queue_err_rq:
		i915_request_get(rq);
		__i915_request_commit(rq);
		__i915_request_queue(rq, &attr);

		mutex_unlock(&ce->timeline->mutex);
		/* This will break if the request is complete or after engine reset */
		i915_request_wait(rq, 0, MAX_SCHEDULE_TIMEOUT);
		if (rq->fence.error)
			goto err_rq;

		i915_request_put(rq);

		num_entries -= n_ptes;
		offset += n_ptes;
	}

	gen8_ggtt_bind_put_ce(ce);
	return true;

err_rq:
	i915_request_put(rq);
put_ce:
	gen8_ggtt_bind_put_ce(ce);
	return false;
}

static void gen8_set_pte(void __iomem *addr, gen8_pte_t pte)
{
	writeq(pte, addr);
}

static void gen8_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  unsigned int pat_index,
				  u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen8_pte_t __iomem *pte =
		(gen8_pte_t __iomem *)ggtt->gsm + offset / I915_GTT_PAGE_SIZE;

	gen8_set_pte(pte, ggtt->vm.pte_encode(addr, pat_index, flags));

	ggtt->invalidate(ggtt);
}

static void gen8_ggtt_insert_page_bind(struct i915_address_space *vm,
				       dma_addr_t addr, u64 offset,
				       unsigned int pat_index, u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen8_pte_t pte;

	pte = ggtt->vm.pte_encode(addr, pat_index, flags);
	if (should_update_ggtt_with_bind(i915_vm_to_ggtt(vm)) &&
	    gen8_ggtt_bind_ptes(ggtt, offset, NULL, 1, pte))
		return ggtt->invalidate(ggtt);

	gen8_ggtt_insert_page(vm, addr, offset, pat_index, flags);
}

static void gen8_ggtt_insert_entries(struct i915_address_space *vm,
				     struct i915_vma_resource *vma_res,
				     unsigned int pat_index,
				     u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	const gen8_pte_t pte_encode = ggtt->vm.pte_encode(0, pat_index, flags);
	gen8_pte_t __iomem *gte;
	gen8_pte_t __iomem *end;
	struct sgt_iter iter;
	dma_addr_t addr;

	/*
	 * Note that we ignore PTE_READ_ONLY here. The caller must be careful
	 * not to allow the user to override access to a read only page.
	 */

	gte = (gen8_pte_t __iomem *)ggtt->gsm;
	gte += (vma_res->start - vma_res->guard) / I915_GTT_PAGE_SIZE;
	end = gte + vma_res->guard / I915_GTT_PAGE_SIZE;
	while (gte < end)
		gen8_set_pte(gte++, vm->scratch[0]->encode);
	end += (vma_res->node_size + vma_res->guard) / I915_GTT_PAGE_SIZE;

	for_each_sgt_daddr(addr, iter, vma_res->bi.pages)
		gen8_set_pte(gte++, pte_encode | addr);
	GEM_BUG_ON(gte > end);

	/* Fill the allocated but "unused" space beyond the end of the buffer */
	while (gte < end)
		gen8_set_pte(gte++, vm->scratch[0]->encode);

	/*
	 * We want to flush the TLBs only after we're certain all the PTE
	 * updates have finished.
	 */
	ggtt->invalidate(ggtt);
}

static bool __gen8_ggtt_insert_entries_bind(struct i915_address_space *vm,
					    struct i915_vma_resource *vma_res,
					    unsigned int pat_index, u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen8_pte_t scratch_pte = vm->scratch[0]->encode;
	gen8_pte_t pte_encode;
	u64 start, end;

	pte_encode = ggtt->vm.pte_encode(0, pat_index, flags);
	start = (vma_res->start - vma_res->guard) / I915_GTT_PAGE_SIZE;
	end = start + vma_res->guard / I915_GTT_PAGE_SIZE;
	if (!gen8_ggtt_bind_ptes(ggtt, start, NULL, end - start, scratch_pte))
		goto err;

	start = end;
	end += (vma_res->node_size + vma_res->guard) / I915_GTT_PAGE_SIZE;
	if (!gen8_ggtt_bind_ptes(ggtt, start, vma_res->bi.pages,
	      vma_res->node_size / I915_GTT_PAGE_SIZE, pte_encode))
		goto err;

	start += vma_res->node_size / I915_GTT_PAGE_SIZE;
	if (!gen8_ggtt_bind_ptes(ggtt, start, NULL, end - start, scratch_pte))
		goto err;

	return true;

err:
	return false;
}

static void gen8_ggtt_insert_entries_bind(struct i915_address_space *vm,
					  struct i915_vma_resource *vma_res,
					  unsigned int pat_index, u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);

	if (should_update_ggtt_with_bind(i915_vm_to_ggtt(vm)) &&
	    __gen8_ggtt_insert_entries_bind(vm, vma_res, pat_index, flags))
		return ggtt->invalidate(ggtt);

	gen8_ggtt_insert_entries(vm, vma_res, pat_index, flags);
}

static void gen8_ggtt_clear_range(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	unsigned int first_entry = start / I915_GTT_PAGE_SIZE;
	unsigned int num_entries = length / I915_GTT_PAGE_SIZE;
	const gen8_pte_t scratch_pte = vm->scratch[0]->encode;
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

static void gen8_ggtt_scratch_range_bind(struct i915_address_space *vm,
					 u64 start, u64 length)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	unsigned int first_entry = start / I915_GTT_PAGE_SIZE;
	unsigned int num_entries = length / I915_GTT_PAGE_SIZE;
	const gen8_pte_t scratch_pte = vm->scratch[0]->encode;
	const int max_entries = ggtt_total_entries(ggtt) - first_entry;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	if (should_update_ggtt_with_bind(ggtt) && gen8_ggtt_bind_ptes(ggtt, first_entry,
	     NULL, num_entries, scratch_pte))
		return ggtt->invalidate(ggtt);

	gen8_ggtt_clear_range(vm, start, length);
}

static void gen6_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  unsigned int pat_index,
				  u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen6_pte_t __iomem *pte =
		(gen6_pte_t __iomem *)ggtt->gsm + offset / I915_GTT_PAGE_SIZE;

	iowrite32(vm->pte_encode(addr, pat_index, flags), pte);

	ggtt->invalidate(ggtt);
}

/*
 * Binds an object into the global gtt with the specified cache level.
 * The object will be accessible to the GPU via commands whose operands
 * reference offsets within the global GTT as well as accessible by the GPU
 * through the GMADR mapped BAR (i915->mm.gtt->gtt).
 */
static void gen6_ggtt_insert_entries(struct i915_address_space *vm,
				     struct i915_vma_resource *vma_res,
				     unsigned int pat_index,
				     u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen6_pte_t __iomem *gte;
	gen6_pte_t __iomem *end;
	struct sgt_iter iter;
	dma_addr_t addr;

	gte = (gen6_pte_t __iomem *)ggtt->gsm;
	gte += (vma_res->start - vma_res->guard) / I915_GTT_PAGE_SIZE;

	end = gte + vma_res->guard / I915_GTT_PAGE_SIZE;
	while (gte < end)
		iowrite32(vm->scratch[0]->encode, gte++);
	end += (vma_res->node_size + vma_res->guard) / I915_GTT_PAGE_SIZE;
	for_each_sgt_daddr(addr, iter, vma_res->bi.pages)
		iowrite32(vm->pte_encode(addr, pat_index, flags), gte++);
	GEM_BUG_ON(gte > end);

	/* Fill the allocated but "unused" space beyond the end of the buffer */
	while (gte < end)
		iowrite32(vm->scratch[0]->encode, gte++);

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
	unsigned int pat_index;
};

static int bxt_vtd_ggtt_insert_page__cb(void *_arg)
{
	struct insert_page *arg = _arg;

	gen8_ggtt_insert_page(arg->vm, arg->addr, arg->offset,
			      arg->pat_index, 0);
	bxt_vtd_ggtt_wa(arg->vm);

	return 0;
}

static void bxt_vtd_ggtt_insert_page__BKL(struct i915_address_space *vm,
					  dma_addr_t addr,
					  u64 offset,
					  unsigned int pat_index,
					  u32 unused)
{
	struct insert_page arg = { vm, addr, offset, pat_index };

	stop_machine(bxt_vtd_ggtt_insert_page__cb, &arg, NULL);
}

struct insert_entries {
	struct i915_address_space *vm;
	struct i915_vma_resource *vma_res;
	unsigned int pat_index;
	u32 flags;
};

static int bxt_vtd_ggtt_insert_entries__cb(void *_arg)
{
	struct insert_entries *arg = _arg;

	gen8_ggtt_insert_entries(arg->vm, arg->vma_res,
				 arg->pat_index, arg->flags);
	bxt_vtd_ggtt_wa(arg->vm);

	return 0;
}

static void bxt_vtd_ggtt_insert_entries__BKL(struct i915_address_space *vm,
					     struct i915_vma_resource *vma_res,
					     unsigned int pat_index,
					     u32 flags)
{
	struct insert_entries arg = { vm, vma_res, pat_index, flags };

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

	scratch_pte = vm->scratch[0]->encode;
	for (i = 0; i < num_entries; i++)
		iowrite32(scratch_pte, &gtt_base[i]);
}

void intel_ggtt_bind_vma(struct i915_address_space *vm,
			 struct i915_vm_pt_stash *stash,
			 struct i915_vma_resource *vma_res,
			 unsigned int pat_index,
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

	vm->insert_entries(vm, vma_res, pat_index, pte_flags);
	vma_res->page_sizes_gtt = I915_GTT_PAGE_SIZE;
}

void intel_ggtt_unbind_vma(struct i915_address_space *vm,
			   struct i915_vma_resource *vma_res)
{
	vm->clear_range(vm, vma_res->start, vma_res->vma_size);
}

/*
 * Reserve the top of the GuC address space for firmware images. Addresses
 * beyond GUC_GGTT_TOP in the GuC address space are inaccessible by GuC,
 * which makes for a suitable range to hold GuC/HuC firmware images if the
 * size of the GGTT is 4G. However, on a 32-bit platform the size of the GGTT
 * is limited to 2G, which is less than GUC_GGTT_TOP, but we reserve a chunk
 * of the same size anyway, which is far more than needed, to keep the logic
 * in uc_fw_ggtt_offset() simple.
 */
#define GUC_TOP_RESERVE_SIZE (SZ_4G - GUC_GGTT_TOP)

static int ggtt_reserve_guc_top(struct i915_ggtt *ggtt)
{
	u64 offset;
	int ret;

	if (!intel_uc_uses_guc(&ggtt->vm.gt->uc))
		return 0;

	GEM_BUG_ON(ggtt->vm.total <= GUC_TOP_RESERVE_SIZE);
	offset = ggtt->vm.total - GUC_TOP_RESERVE_SIZE;

	ret = i915_gem_gtt_reserve(&ggtt->vm, NULL, &ggtt->uc_fw,
				   GUC_TOP_RESERVE_SIZE, offset,
				   I915_COLOR_UNEVICTABLE, PIN_NOEVICT);
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
			       intel_wopcm_guc_size(&ggtt->vm.gt->wopcm));

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
		 *
		 * Since CPU can perform speculative reads on error capture
		 * (write-combining allows it) add scratch page after error
		 * capture to avoid DMAR errors.
		 */
		ggtt->error_capture.size = 2 * I915_GTT_PAGE_SIZE;
		ggtt->error_capture.color = I915_COLOR_UNEVICTABLE;
		if (drm_mm_reserve_node(&ggtt->vm.mm, &ggtt->error_capture))
			drm_mm_insert_node_in_range(&ggtt->vm.mm,
						    &ggtt->error_capture,
						    ggtt->error_capture.size, 0,
						    ggtt->error_capture.color,
						    0, ggtt->mappable_end,
						    DRM_MM_INSERT_LOW);
	}
	if (drm_mm_node_allocated(&ggtt->error_capture)) {
		u64 start = ggtt->error_capture.start;
		u64 size = ggtt->error_capture.size;

		ggtt->vm.scratch_range(&ggtt->vm, start, size);
		drm_dbg(&ggtt->vm.i915->drm,
			"Reserved GGTT:[%llx, %llx] for use by error capture\n",
			start, start + size);
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
				  unsigned int pat_index,
				  u32 flags)
{
	u32 pte_flags;

	/* Currently applicable only to VLV */
	pte_flags = 0;
	if (vma_res->bi.readonly)
		pte_flags |= PTE_READ_ONLY;

	if (flags & I915_VMA_LOCAL_BIND)
		ppgtt_bind_vma(&i915_vm_to_ggtt(vm)->alias->vm,
			       stash, vma_res, pat_index, flags);

	if (flags & I915_VMA_GLOBAL_BIND)
		vm->insert_entries(vm, vma_res, pat_index, pte_flags);

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

static unsigned int gen6_gttmmadr_size(struct drm_i915_private *i915)
{
	/*
	 * GEN6: GTTMMADR size is 4MB and GTTADR starts at 2MB offset
	 * GEN8: GTTMMADR size is 16MB and GTTADR starts at 8MB offset
	 */
	GEM_BUG_ON(GRAPHICS_VER(i915) < 6);
	return (GRAPHICS_VER(i915) < 8) ? SZ_4M : SZ_16M;
}

static unsigned int gen6_gttadr_offset(struct drm_i915_private *i915)
{
	return gen6_gttmmadr_size(i915) / 2;
}

static int ggtt_probe_common(struct i915_ggtt *ggtt, u64 size)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	phys_addr_t phys_addr;
	u32 pte_flags;
	int ret;

	GEM_WARN_ON(pci_resource_len(pdev, GEN4_GTTMMADR_BAR) != gen6_gttmmadr_size(i915));
	phys_addr = pci_resource_start(pdev, GEN4_GTTMMADR_BAR) + gen6_gttadr_offset(i915);

	/*
	 * On BXT+/ICL+ writes larger than 64 bit to the GTT pagetable range
	 * will be dropped. For WC mappings in general we have 64 byte burst
	 * writes when the WC buffer is flushed, so we can't use it, but have to
	 * resort to an uncached mapping. The WC issue is easily caught by the
	 * readback check when writing GTT PTE entries.
	 */
	if (IS_GEN9_LP(i915) || GRAPHICS_VER(i915) >= 11)
		ggtt->gsm = ioremap(phys_addr, size);
	else
		ggtt->gsm = ioremap_wc(phys_addr, size);
	if (!ggtt->gsm) {
		drm_err(&i915->drm, "Failed to map the ggtt page table\n");
		return -ENOMEM;
	}

	kref_init(&ggtt->vm.resv_ref);
	ret = setup_scratch_page(&ggtt->vm);
	if (ret) {
		drm_err(&i915->drm, "Scratch setup failed\n");
		/* iounmap will also get called at remove, but meh */
		iounmap(ggtt->gsm);
		return ret;
	}

	pte_flags = 0;
	if (i915_gem_object_is_lmem(ggtt->vm.scratch[0]))
		pte_flags |= PTE_LM;

	ggtt->vm.scratch[0]->encode =
		ggtt->vm.pte_encode(px_dma(ggtt->vm.scratch[0]),
				    i915_gem_get_pat_index(i915,
							   I915_CACHE_NONE),
				    pte_flags);

	return 0;
}

static void gen6_gmch_remove(struct i915_address_space *vm)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);

	iounmap(ggtt->gsm);
	free_scratch(vm);
}

static struct resource pci_resource(struct pci_dev *pdev, int bar)
{
	return DEFINE_RES_MEM(pci_resource_start(pdev, bar),
			      pci_resource_len(pdev, bar));
}

static int gen8_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	unsigned int size;
	u16 snb_gmch_ctl;

	if (!HAS_LMEM(i915) && !HAS_LMEMBAR_SMEM_STOLEN(i915)) {
		if (!i915_pci_resource_valid(pdev, GEN4_GMADR_BAR))
			return -ENXIO;

		ggtt->gmadr = pci_resource(pdev, GEN4_GMADR_BAR);
		ggtt->mappable_end = resource_size(&ggtt->gmadr);
	}

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);
	if (IS_CHERRYVIEW(i915))
		size = chv_get_total_gtt_size(snb_gmch_ctl);
	else
		size = gen8_get_total_gtt_size(snb_gmch_ctl);

	ggtt->vm.alloc_pt_dma = alloc_pt_dma;
	ggtt->vm.alloc_scratch_dma = alloc_pt_dma;
	ggtt->vm.lmem_pt_obj_flags = I915_BO_ALLOC_PM_EARLY;

	ggtt->vm.total = (size / sizeof(gen8_pte_t)) * I915_GTT_PAGE_SIZE;
	ggtt->vm.cleanup = gen6_gmch_remove;
	ggtt->vm.insert_page = gen8_ggtt_insert_page;
	ggtt->vm.clear_range = nop_clear_range;
	ggtt->vm.scratch_range = gen8_ggtt_clear_range;

	ggtt->vm.insert_entries = gen8_ggtt_insert_entries;

	/*
	 * Serialize GTT updates with aperture access on BXT if VT-d is on,
	 * and always on CHV.
	 */
	if (intel_vm_no_concurrent_access_wa(i915)) {
		ggtt->vm.insert_entries = bxt_vtd_ggtt_insert_entries__BKL;
		ggtt->vm.insert_page    = bxt_vtd_ggtt_insert_page__BKL;

		/*
		 * Calling stop_machine() version of GGTT update function
		 * at error capture/reset path will raise lockdep warning.
		 * Allow calling gen8_ggtt_insert_* directly at reset path
		 * which is safe from parallel GGTT updates.
		 */
		ggtt->vm.raw_insert_page = gen8_ggtt_insert_page;
		ggtt->vm.raw_insert_entries = gen8_ggtt_insert_entries;

		ggtt->vm.bind_async_flags =
			I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND;
	}

	if (i915_ggtt_require_binder(i915)) {
		ggtt->vm.scratch_range = gen8_ggtt_scratch_range_bind;
		ggtt->vm.insert_page = gen8_ggtt_insert_page_bind;
		ggtt->vm.insert_entries = gen8_ggtt_insert_entries_bind;
		/*
		 * On GPU is hung, we might bind VMAs for error capture.
		 * Fallback to CPU GGTT updates in that case.
		 */
		ggtt->vm.raw_insert_page = gen8_ggtt_insert_page;
	}

	if (intel_uc_wants_guc_submission(&ggtt->vm.gt->uc))
		ggtt->invalidate = guc_ggtt_invalidate;
	else
		ggtt->invalidate = gen8_ggtt_invalidate;

	ggtt->vm.vma_ops.bind_vma    = intel_ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma  = intel_ggtt_unbind_vma;

	if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 70))
		ggtt->vm.pte_encode = mtl_ggtt_pte_encode;
	else
		ggtt->vm.pte_encode = gen8_ggtt_pte_encode;

	return ggtt_probe_common(ggtt, size);
}

/*
 * For pre-gen8 platforms pat_index is the same as enum i915_cache_level,
 * so the switch-case statements in these PTE encode functions are still valid.
 * See translation table LEGACY_CACHELEVEL.
 */
static u64 snb_pte_encode(dma_addr_t addr,
			  unsigned int pat_index,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	switch (pat_index) {
	case I915_CACHE_L3_LLC:
	case I915_CACHE_LLC:
		pte |= GEN6_PTE_CACHE_LLC;
		break;
	case I915_CACHE_NONE:
		pte |= GEN6_PTE_UNCACHED;
		break;
	default:
		MISSING_CASE(pat_index);
	}

	return pte;
}

static u64 ivb_pte_encode(dma_addr_t addr,
			  unsigned int pat_index,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	switch (pat_index) {
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
		MISSING_CASE(pat_index);
	}

	return pte;
}

static u64 byt_pte_encode(dma_addr_t addr,
			  unsigned int pat_index,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	if (!(flags & PTE_READ_ONLY))
		pte |= BYT_PTE_WRITEABLE;

	if (pat_index != I915_CACHE_NONE)
		pte |= BYT_PTE_SNOOPED_BY_CPU_CACHES;

	return pte;
}

static u64 hsw_pte_encode(dma_addr_t addr,
			  unsigned int pat_index,
			  u32 flags)
{
	gen6_pte_t pte = HSW_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	if (pat_index != I915_CACHE_NONE)
		pte |= HSW_WB_LLC_AGE3;

	return pte;
}

static u64 iris_pte_encode(dma_addr_t addr,
			   unsigned int pat_index,
			   u32 flags)
{
	gen6_pte_t pte = HSW_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	switch (pat_index) {
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
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	unsigned int size;
	u16 snb_gmch_ctl;

	if (!i915_pci_resource_valid(pdev, GEN4_GMADR_BAR))
		return -ENXIO;

	ggtt->gmadr = pci_resource(pdev, GEN4_GMADR_BAR);
	ggtt->mappable_end = resource_size(&ggtt->gmadr);

	/*
	 * 64/512MB is the current min/max we actually know of, but this is
	 * just a coarse sanity check.
	 */
	if (ggtt->mappable_end < (64 << 20) ||
	    ggtt->mappable_end > (512 << 20)) {
		drm_err(&i915->drm, "Unknown GMADR size (%pa)\n",
			&ggtt->mappable_end);
		return -ENXIO;
	}

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);

	size = gen6_get_total_gtt_size(snb_gmch_ctl);
	ggtt->vm.total = (size / sizeof(gen6_pte_t)) * I915_GTT_PAGE_SIZE;

	ggtt->vm.alloc_pt_dma = alloc_pt_dma;
	ggtt->vm.alloc_scratch_dma = alloc_pt_dma;

	ggtt->vm.clear_range = nop_clear_range;
	if (!HAS_FULL_PPGTT(i915))
		ggtt->vm.clear_range = gen6_ggtt_clear_range;
	ggtt->vm.scratch_range = gen6_ggtt_clear_range;
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
	else if (GRAPHICS_VER(i915) >= 7)
		ggtt->vm.pte_encode = ivb_pte_encode;
	else
		ggtt->vm.pte_encode = snb_pte_encode;

	ggtt->vm.vma_ops.bind_vma    = intel_ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma  = intel_ggtt_unbind_vma;

	return ggtt_probe_common(ggtt, size);
}

static int ggtt_probe_hw(struct i915_ggtt *ggtt, struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	int ret;

	ggtt->vm.gt = gt;
	ggtt->vm.i915 = i915;
	ggtt->vm.dma = i915->drm.dev;
	dma_resv_init(&ggtt->vm._resv);

	if (GRAPHICS_VER(i915) >= 8)
		ret = gen8_gmch_probe(ggtt);
	else if (GRAPHICS_VER(i915) >= 6)
		ret = gen6_gmch_probe(ggtt);
	else
		ret = intel_ggtt_gmch_probe(ggtt);

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
	struct intel_gt *gt;
	int ret, i;

	for_each_gt(gt, i915, i) {
		ret = intel_gt_assign_ggtt(gt);
		if (ret)
			return ret;
	}

	ret = ggtt_probe_hw(to_gt(i915)->ggtt, to_gt(i915));
	if (ret)
		return ret;

	if (i915_vtd_active(i915))
		drm_info(&i915->drm, "VT-d active for gfx access\n");

	return 0;
}

struct i915_ggtt *i915_ggtt_create(struct drm_i915_private *i915)
{
	struct i915_ggtt *ggtt;

	ggtt = drmm_kzalloc(&i915->drm, sizeof(*ggtt), GFP_KERNEL);
	if (!ggtt)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&ggtt->gt_list);

	return ggtt;
}

int i915_ggtt_enable_hw(struct drm_i915_private *i915)
{
	if (GRAPHICS_VER(i915) < 6)
		return intel_ggtt_gmch_enable_hw(i915);

	return 0;
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

		/*
		 * Clear the bound flags of the vma resource to allow
		 * ptes to be repopulated.
		 */
		vma->resource->bound_flags = 0;
		vma->ops->bind_vma(vm, NULL, vma->resource,
				   obj ? obj->pat_index :
					 i915_gem_get_pat_index(vm->i915,
								I915_CACHE_NONE),
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
	struct intel_gt *gt;
	bool flush;

	list_for_each_entry(gt, &ggtt->gt_list, ggtt_link)
		intel_gt_check_and_clear_faults(gt);

	flush = i915_ggtt_resume_vm(&ggtt->vm);

	if (drm_mm_node_allocated(&ggtt->error_capture))
		ggtt->vm.scratch_range(&ggtt->vm, ggtt->error_capture.start,
				       ggtt->error_capture.size);

	list_for_each_entry(gt, &ggtt->gt_list, ggtt_link)
		intel_uc_resume_mappings(&gt->uc);

	ggtt->invalidate(ggtt);

	if (flush)
		wbinvd_on_all_cpus();

	intel_ggtt_restore_fences(ggtt);
}
