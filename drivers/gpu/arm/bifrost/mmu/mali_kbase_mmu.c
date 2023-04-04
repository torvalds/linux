// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2010-2023 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/**
 * DOC: Base kernel MMU management.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/migrate.h>
#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <gpu/mali_kbase_gpu_regmap.h>
#include <tl/mali_kbase_tracepoints.h>
#include <backend/gpu/mali_kbase_instr_defs.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_debug.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_hw.h>
#include <mmu/mali_kbase_mmu_hw.h>
#include <mali_kbase_mem.h>
#include <mali_kbase_reset_gpu.h>
#include <mmu/mali_kbase_mmu.h>
#include <mmu/mali_kbase_mmu_internal.h>
#include <mali_kbase_cs_experimental.h>
#include <device/mali_kbase_device.h>
#include <uapi/gpu/arm/bifrost/gpu/mali_kbase_gpu_id.h>
#if !MALI_USE_CSF
#include <mali_kbase_hwaccess_jm.h>
#endif

#include <mali_kbase_trace_gpu_mem.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

/* Threshold used to decide whether to flush full caches or just a physical range */
#define KBASE_PA_RANGE_THRESHOLD_NR_PAGES 20
#define MGM_DEFAULT_PTE_GROUP (0)

/* Macro to convert updated PDGs to flags indicating levels skip in flush */
#define pgd_level_to_skip_flush(dirty_pgds) (~(dirty_pgds) & 0xF)

/* Small wrapper function to factor out GPU-dependent context releasing */
static void release_ctx(struct kbase_device *kbdev,
		struct kbase_context *kctx)
{
#if MALI_USE_CSF
	CSTD_UNUSED(kbdev);
	kbase_ctx_sched_release_ctx_lock(kctx);
#else /* MALI_USE_CSF */
	kbasep_js_runpool_release_ctx(kbdev, kctx);
#endif /* MALI_USE_CSF */
}

static void mmu_hw_operation_begin(struct kbase_device *kbdev)
{
#if !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
#if MALI_USE_CSF
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_GPU2019_3878)) {
		unsigned long flags;

		lockdep_assert_held(&kbdev->mmu_hw_mutex);

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		WARN_ON_ONCE(kbdev->mmu_hw_operation_in_progress);
		kbdev->mmu_hw_operation_in_progress = true;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}
#endif /* MALI_USE_CSF */
#endif /* !CONFIG_MALI_BIFROST_NO_MALI */
}

static void mmu_hw_operation_end(struct kbase_device *kbdev)
{
#if !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
#if MALI_USE_CSF
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_GPU2019_3878)) {
		unsigned long flags;

		lockdep_assert_held(&kbdev->mmu_hw_mutex);

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		WARN_ON_ONCE(!kbdev->mmu_hw_operation_in_progress);
		kbdev->mmu_hw_operation_in_progress = false;
		/* Invoke the PM state machine, the L2 power off may have been
		 * skipped due to the MMU command.
		 */
		kbase_pm_update_state(kbdev);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}
#endif /* MALI_USE_CSF */
#endif /* !CONFIG_MALI_BIFROST_NO_MALI */
}

/**
 * mmu_flush_cache_on_gpu_ctrl() - Check if cache flush needs to be done
 * through GPU_CONTROL interface.
 *
 * @kbdev:         kbase device to check GPU model ID on.
 *
 * This function returns whether a cache flush for page table update should
 * run through GPU_CONTROL interface or MMU_AS_CONTROL interface.
 *
 * Return: True if cache flush should be done on GPU command.
 */
static bool mmu_flush_cache_on_gpu_ctrl(struct kbase_device *kbdev)
{
	uint32_t const arch_maj_cur = (kbdev->gpu_props.props.raw_props.gpu_id &
				       GPU_ID2_ARCH_MAJOR) >>
				      GPU_ID2_ARCH_MAJOR_SHIFT;

	return arch_maj_cur > 11;
}

/**
 * mmu_flush_pa_range() - Flush physical address range
 *
 * @kbdev:    kbase device to issue the MMU operation on.
 * @phys:     Starting address of the physical range to start the operation on.
 * @nr_bytes: Number of bytes to work on.
 * @op:       Type of cache flush operation to perform.
 *
 * Issue a cache flush physical range command.
 */
#if MALI_USE_CSF
static void mmu_flush_pa_range(struct kbase_device *kbdev, phys_addr_t phys, size_t nr_bytes,
			       enum kbase_mmu_op_type op)
{
	u32 flush_op;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* Translate operation to command */
	if (op == KBASE_MMU_OP_FLUSH_PT)
		flush_op = GPU_COMMAND_FLUSH_PA_RANGE_CLN_INV_L2;
	else if (op == KBASE_MMU_OP_FLUSH_MEM)
		flush_op = GPU_COMMAND_FLUSH_PA_RANGE_CLN_INV_L2_LSC;
	else {
		dev_warn(kbdev->dev, "Invalid flush request (op = %d)", op);
		return;
	}

	if (kbase_gpu_cache_flush_pa_range_and_busy_wait(kbdev, phys, nr_bytes, flush_op))
		dev_err(kbdev->dev, "Flush for physical address range did not complete");
}
#endif

/**
 * mmu_invalidate() - Perform an invalidate operation on MMU caches.
 * @kbdev:      The Kbase device.
 * @kctx:       The Kbase context.
 * @as_nr:      GPU address space number for which invalidate is required.
 * @op_param: Non-NULL pointer to struct containing information about the MMU
 *            operation to perform.
 *
 * Perform an MMU invalidate operation on a particual address space
 * by issuing a UNLOCK command.
 */
static void mmu_invalidate(struct kbase_device *kbdev, struct kbase_context *kctx, int as_nr,
			   const struct kbase_mmu_hw_op_param *op_param)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->pm.backend.gpu_powered && (!kctx || kctx->as_nr >= 0)) {
		as_nr = kctx ? kctx->as_nr : as_nr;
		if (kbase_mmu_hw_do_unlock(kbdev, &kbdev->as[as_nr], op_param))
			dev_err(kbdev->dev,
				"Invalidate after GPU page table update did not complete");
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

/* Perform a flush/invalidate on a particular address space
 */
static void mmu_flush_invalidate_as(struct kbase_device *kbdev, struct kbase_as *as,
				    const struct kbase_mmu_hw_op_param *op_param)
{
	unsigned long flags;

	/* AS transaction begin */
	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->pm.backend.gpu_powered && (kbase_mmu_hw_do_flush_locked(kbdev, as, op_param)))
		dev_err(kbdev->dev, "Flush for GPU page table update did not complete");

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);
	/* AS transaction end */
}

/**
 * mmu_flush_invalidate() - Perform a flush operation on GPU caches.
 * @kbdev:      The Kbase device.
 * @kctx:       The Kbase context.
 * @as_nr:      GPU address space number for which flush + invalidate is required.
 * @op_param: Non-NULL pointer to struct containing information about the MMU
 *            operation to perform.
 *
 * This function performs the cache flush operation described by @op_param.
 * The function retains a reference to the given @kctx and releases it
 * after performing the flush operation.
 *
 * If operation is set to KBASE_MMU_OP_FLUSH_PT then this function will issue
 * a cache flush + invalidate to the L2 caches and invalidate the TLBs.
 *
 * If operation is set to KBASE_MMU_OP_FLUSH_MEM then this function will issue
 * a cache flush + invalidate to the L2 and GPU Load/Store caches as well as
 * invalidating the TLBs.
 */
static void mmu_flush_invalidate(struct kbase_device *kbdev, struct kbase_context *kctx, int as_nr,
				 const struct kbase_mmu_hw_op_param *op_param)
{
	bool ctx_is_in_runpool;

	/* Early out if there is nothing to do */
	if (op_param->nr == 0)
		return;

	/* If no context is provided then MMU operation is performed on address
	 * space which does not belong to user space context. Otherwise, retain
	 * refcount to context provided and release after flush operation.
	 */
	if (!kctx) {
		mmu_flush_invalidate_as(kbdev, &kbdev->as[as_nr], op_param);
	} else {
#if !MALI_USE_CSF
		mutex_lock(&kbdev->js_data.queue_mutex);
		ctx_is_in_runpool = kbase_ctx_sched_inc_refcount(kctx);
		mutex_unlock(&kbdev->js_data.queue_mutex);
#else
		ctx_is_in_runpool = kbase_ctx_sched_inc_refcount_if_as_valid(kctx);
#endif /* !MALI_USE_CSF */

		if (ctx_is_in_runpool) {
			KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

			mmu_flush_invalidate_as(kbdev, &kbdev->as[kctx->as_nr], op_param);

			release_ctx(kbdev, kctx);
		}
	}
}

/**
 * mmu_flush_invalidate_on_gpu_ctrl() - Perform a flush operation on GPU caches via
 *                                    the GPU_CONTROL interface
 * @kbdev:      The Kbase device.
 * @kctx:       The Kbase context.
 * @as_nr:      GPU address space number for which flush + invalidate is required.
 * @op_param: Non-NULL pointer to struct containing information about the MMU
 *            operation to perform.
 *
 * Perform a flush/invalidate on a particular address space via the GPU_CONTROL
 * interface.
 */
static void mmu_flush_invalidate_on_gpu_ctrl(struct kbase_device *kbdev, struct kbase_context *kctx,
					int as_nr, const struct kbase_mmu_hw_op_param *op_param)
{
	unsigned long flags;

	/* AS transaction begin */
	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->pm.backend.gpu_powered && (!kctx || kctx->as_nr >= 0)) {
		as_nr = kctx ? kctx->as_nr : as_nr;
		if (kbase_mmu_hw_do_flush_on_gpu_ctrl(kbdev, &kbdev->as[as_nr], op_param))
			dev_err(kbdev->dev, "Flush for GPU page table update did not complete");
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);
}

static void kbase_mmu_sync_pgd_gpu(struct kbase_device *kbdev, struct kbase_context *kctx,
				   phys_addr_t phys, size_t size,
				   enum kbase_mmu_op_type flush_op)
{
	kbase_mmu_flush_pa_range(kbdev, kctx, phys, size, flush_op);
}

static void kbase_mmu_sync_pgd_cpu(struct kbase_device *kbdev, dma_addr_t handle, size_t size)
{
	/* In non-coherent system, ensure the GPU can read
	 * the pages from memory
	 */
	if (kbdev->system_coherency == COHERENCY_NONE)
		dma_sync_single_for_device(kbdev->dev, handle, size,
				DMA_TO_DEVICE);
}

/**
 * kbase_mmu_sync_pgd() - sync page directory to memory when needed.
 * @kbdev:    Device pointer.
 * @kctx:     Context pointer.
 * @phys:     Starting physical address of the destination region.
 * @handle:   Address of DMA region.
 * @size:     Size of the region to sync.
 * @flush_op: MMU cache flush operation to perform on the physical address
 *            range, if GPU control is available.
 *
 * This function is called whenever the association between a virtual address
 * range and a physical address range changes, because a mapping is created or
 * destroyed.
 * One of the effects of this operation is performing an MMU cache flush
 * operation only on the physical address range affected by this function, if
 * GPU control is available.
 *
 * This should be called after each page directory update.
 */
static void kbase_mmu_sync_pgd(struct kbase_device *kbdev, struct kbase_context *kctx,
			       phys_addr_t phys, dma_addr_t handle, size_t size,
			       enum kbase_mmu_op_type flush_op)
{

	kbase_mmu_sync_pgd_cpu(kbdev, handle, size);
	kbase_mmu_sync_pgd_gpu(kbdev, kctx, phys, size, flush_op);
}

/*
 * Definitions:
 * - PGD: Page Directory.
 * - PTE: Page Table Entry. A 64bit value pointing to the next
 *        level of translation
 * - ATE: Address Translation Entry. A 64bit value pointing to
 *        a 4kB physical page.
 */

static int kbase_mmu_update_pages_no_flush(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
					   u64 vpfn, struct tagged_addr *phys, size_t nr,
					   unsigned long flags, int group_id, u64 *dirty_pgds);

/**
 * kbase_mmu_update_and_free_parent_pgds() - Update number of valid entries and
 *                                           free memory of the page directories
 *
 * @kbdev:    Device pointer.
 * @mmut:     GPU MMU page table.
 * @pgds:     Physical addresses of page directories to be freed.
 * @vpfn:     The virtual page frame number.
 * @level:    The level of MMU page table.
 * @flush_op: The type of MMU flush operation to perform.
 * @dirty_pgds: Flags to track every level where a PGD has been updated.
 */
static void kbase_mmu_update_and_free_parent_pgds(struct kbase_device *kbdev,
						  struct kbase_mmu_table *mmut, phys_addr_t *pgds,
						  u64 vpfn, int level,
						  enum kbase_mmu_op_type flush_op, u64 *dirty_pgds);

static void kbase_mmu_account_freed_pgd(struct kbase_device *kbdev, struct kbase_mmu_table *mmut)
{
	atomic_sub(1, &kbdev->memdev.used_pages);

	/* If MMU tables belong to a context then pages will have been accounted
	 * against it, so we must decrement the usage counts here.
	 */
	if (mmut->kctx) {
		kbase_process_page_usage_dec(mmut->kctx, 1);
		atomic_sub(1, &mmut->kctx->used_pages);
	}

	kbase_trace_gpu_mem_usage_dec(kbdev, mmut->kctx, 1);
}

static bool kbase_mmu_handle_isolated_pgd_page(struct kbase_device *kbdev,
					       struct kbase_mmu_table *mmut,
					       struct page *p)
{
	struct kbase_page_metadata *page_md = kbase_page_private(p);
	bool page_is_isolated = false;

	lockdep_assert_held(&mmut->mmu_lock);

	if (!kbase_page_migration_enabled)
		return false;

	spin_lock(&page_md->migrate_lock);
	if (PAGE_STATUS_GET(page_md->status) == PT_MAPPED) {
		WARN_ON_ONCE(!mmut->kctx);
		if (IS_PAGE_ISOLATED(page_md->status)) {
			page_md->status = PAGE_STATUS_SET(page_md->status,
							  FREE_PT_ISOLATED_IN_PROGRESS);
			page_md->data.free_pt_isolated.kbdev = kbdev;
			page_is_isolated = true;
		} else {
			page_md->status =
				PAGE_STATUS_SET(page_md->status, FREE_IN_PROGRESS);
		}
	} else {
		WARN_ON_ONCE(mmut->kctx);
		WARN_ON_ONCE(PAGE_STATUS_GET(page_md->status) != NOT_MOVABLE);
	}
	spin_unlock(&page_md->migrate_lock);

	if (unlikely(page_is_isolated)) {
		/* Do the CPU cache flush and accounting here for the isolated
		 * PGD page, which is done inside kbase_mmu_free_pgd() for the
		 * PGD page that did not get isolated.
		 */
		dma_sync_single_for_device(kbdev->dev, kbase_dma_addr(p), PAGE_SIZE,
					   DMA_BIDIRECTIONAL);
		kbase_mmu_account_freed_pgd(kbdev, mmut);
	}

	return page_is_isolated;
}

/**
 * kbase_mmu_free_pgd() - Free memory of the page directory
 *
 * @kbdev:   Device pointer.
 * @mmut:    GPU MMU page table.
 * @pgd:     Physical address of page directory to be freed.
 *
 * This function is supposed to be called with mmu_lock held and after
 * ensuring that GPU won't be able to access the page.
 */
static void kbase_mmu_free_pgd(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
			       phys_addr_t pgd)
{
	struct page *p;
	bool page_is_isolated = false;

	lockdep_assert_held(&mmut->mmu_lock);

	p = pfn_to_page(PFN_DOWN(pgd));
	page_is_isolated = kbase_mmu_handle_isolated_pgd_page(kbdev, mmut, p);

	if (likely(!page_is_isolated)) {
		kbase_mem_pool_free(&kbdev->mem_pools.small[mmut->group_id], p, true);
		kbase_mmu_account_freed_pgd(kbdev, mmut);
	}
}

/**
 * kbase_mmu_free_pgds_list() - Free the PGD pages present in the list
 *
 * @kbdev:          Device pointer.
 * @mmut:           GPU MMU page table.
 *
 * This function will call kbase_mmu_free_pgd() on each page directory page
 * present in the list of free PGDs inside @mmut.
 *
 * The function is supposed to be called after the GPU cache and MMU TLB has
 * been invalidated post the teardown loop.
 *
 * The mmu_lock shall be held prior to calling the function.
 */
static void kbase_mmu_free_pgds_list(struct kbase_device *kbdev, struct kbase_mmu_table *mmut)
{
	size_t i;

	lockdep_assert_held(&mmut->mmu_lock);

	for (i = 0; i < mmut->scratch_mem.free_pgds.head_index; i++)
		kbase_mmu_free_pgd(kbdev, mmut, page_to_phys(mmut->scratch_mem.free_pgds.pgds[i]));

	mmut->scratch_mem.free_pgds.head_index = 0;
}

static void kbase_mmu_add_to_free_pgds_list(struct kbase_mmu_table *mmut, struct page *p)
{
	lockdep_assert_held(&mmut->mmu_lock);

	if (WARN_ON_ONCE(mmut->scratch_mem.free_pgds.head_index > (MAX_FREE_PGDS - 1)))
		return;

	mmut->scratch_mem.free_pgds.pgds[mmut->scratch_mem.free_pgds.head_index++] = p;
}

static inline void kbase_mmu_reset_free_pgds_list(struct kbase_mmu_table *mmut)
{
	lockdep_assert_held(&mmut->mmu_lock);

	mmut->scratch_mem.free_pgds.head_index = 0;
}

/**
 * reg_grow_calc_extra_pages() - Calculate the number of backed pages to add to
 *                               a region on a GPU page fault
 * @kbdev:         KBase device
 * @reg:           The region that will be backed with more pages
 * @fault_rel_pfn: PFN of the fault relative to the start of the region
 *
 * This calculates how much to increase the backing of a region by, based on
 * where a GPU page fault occurred and the flags in the region.
 *
 * This can be more than the minimum number of pages that would reach
 * @fault_rel_pfn, for example to reduce the overall rate of page fault
 * interrupts on a region, or to ensure that the end address is aligned.
 *
 * Return: the number of backed pages to increase by
 */
static size_t reg_grow_calc_extra_pages(struct kbase_device *kbdev,
		struct kbase_va_region *reg, size_t fault_rel_pfn)
{
	size_t multiple = reg->extension;
	size_t reg_current_size = kbase_reg_current_backed_size(reg);
	size_t minimum_extra = fault_rel_pfn - reg_current_size + 1;
	size_t remainder;

	if (!multiple) {
		dev_warn(
			kbdev->dev,
			"VA Region 0x%llx extension was 0, allocator needs to set this properly for KBASE_REG_PF_GROW",
			((unsigned long long)reg->start_pfn) << PAGE_SHIFT);
		return minimum_extra;
	}

	/* Calculate the remainder to subtract from minimum_extra to make it
	 * the desired (rounded down) multiple of the extension.
	 * Depending on reg's flags, the base used for calculating multiples is
	 * different
	 */

	/* multiple is based from the current backed size, even if the
	 * current backed size/pfn for end of committed memory are not
	 * themselves aligned to multiple
	 */
	remainder = minimum_extra % multiple;

#if !MALI_USE_CSF
	if (reg->flags & KBASE_REG_TILER_ALIGN_TOP) {
		/* multiple is based from the top of the initial commit, which
		 * has been allocated in such a way that (start_pfn +
		 * initial_commit) is already aligned to multiple. Hence the
		 * pfn for the end of committed memory will also be aligned to
		 * multiple
		 */
		size_t initial_commit = reg->initial_commit;

		if (fault_rel_pfn < initial_commit) {
			/* this case is just to catch in case it's been
			 * recommitted by userspace to be smaller than the
			 * initial commit
			 */
			minimum_extra = initial_commit - reg_current_size;
			remainder = 0;
		} else {
			/* same as calculating
			 * (fault_rel_pfn - initial_commit + 1)
			 */
			size_t pages_after_initial = minimum_extra +
				reg_current_size - initial_commit;

			remainder = pages_after_initial % multiple;
		}
	}
#endif /* !MALI_USE_CSF */

	if (remainder == 0)
		return minimum_extra;

	return minimum_extra + multiple - remainder;
}

#ifdef CONFIG_MALI_CINSTR_GWT
static void kbase_gpu_mmu_handle_write_faulting_as(struct kbase_device *kbdev,
						   struct kbase_as *faulting_as,
						   u64 start_pfn, size_t nr,
						   u32 kctx_id, u64 dirty_pgds)
{
	/* Calls to this function are inherently synchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_SYNC;
	struct kbase_mmu_hw_op_param op_param;
	int ret = 0;

	mutex_lock(&kbdev->mmu_hw_mutex);

	kbase_mmu_hw_clear_fault(kbdev, faulting_as,
			KBASE_MMU_FAULT_TYPE_PAGE);

	/* flush L2 and unlock the VA (resumes the MMU) */
	op_param.vpfn = start_pfn;
	op_param.nr = nr;
	op_param.op = KBASE_MMU_OP_FLUSH_PT;
	op_param.kctx_id = kctx_id;
	op_param.mmu_sync_info = mmu_sync_info;
	if (mmu_flush_cache_on_gpu_ctrl(kbdev)) {
		unsigned long irq_flags;

		spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
		op_param.flush_skip_levels =
				pgd_level_to_skip_flush(dirty_pgds);
		ret = kbase_mmu_hw_do_flush_on_gpu_ctrl(kbdev, faulting_as, &op_param);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);
	} else {
		mmu_hw_operation_begin(kbdev);
		ret = kbase_mmu_hw_do_flush(kbdev, faulting_as, &op_param);
		mmu_hw_operation_end(kbdev);
	}

	mutex_unlock(&kbdev->mmu_hw_mutex);

	if (ret)
		dev_err(kbdev->dev,
			"Flush for GPU page fault due to write access did not complete");

	kbase_mmu_hw_enable_fault(kbdev, faulting_as,
			KBASE_MMU_FAULT_TYPE_PAGE);
}

static void set_gwt_element_page_addr_and_size(
		struct kbasep_gwt_list_element *element,
		u64 fault_page_addr, struct tagged_addr fault_phys)
{
	u64 fault_pfn = fault_page_addr >> PAGE_SHIFT;
	unsigned int vindex = fault_pfn & (NUM_4K_PAGES_IN_2MB_PAGE - 1);

	/* If the fault address lies within a 2MB page, then consider
	 * the whole 2MB page for dumping to avoid incomplete dumps.
	 */
	if (is_huge(fault_phys) && (vindex == index_in_large_page(fault_phys))) {
		element->page_addr = fault_page_addr & ~(SZ_2M - 1);
		element->num_pages = NUM_4K_PAGES_IN_2MB_PAGE;
	} else {
		element->page_addr = fault_page_addr;
		element->num_pages = 1;
	}
}

static void kbase_gpu_mmu_handle_write_fault(struct kbase_context *kctx,
			struct kbase_as *faulting_as)
{
	struct kbasep_gwt_list_element *pos;
	struct kbase_va_region *region;
	struct kbase_device *kbdev;
	struct tagged_addr *fault_phys_addr;
	struct kbase_fault *fault;
	u64 fault_pfn, pfn_offset;
	int as_no;
	u64 dirty_pgds = 0;

	as_no = faulting_as->number;
	kbdev = container_of(faulting_as, struct kbase_device, as[as_no]);
	fault = &faulting_as->pf_data;
	fault_pfn = fault->addr >> PAGE_SHIFT;

	kbase_gpu_vm_lock(kctx);

	/* Find region and check if it should be writable. */
	region = kbase_region_tracker_find_region_enclosing_address(kctx,
			fault->addr);
	if (kbase_is_region_invalid_or_free(region)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Memory is not mapped on the GPU",
				&faulting_as->pf_data);
		return;
	}

	if (!(region->flags & KBASE_REG_GPU_WR)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Region does not have write permissions",
				&faulting_as->pf_data);
		return;
	}

	pfn_offset = fault_pfn - region->start_pfn;
	fault_phys_addr = &kbase_get_gpu_phy_pages(region)[pfn_offset];

	/* Capture addresses of faulting write location
	 * for job dumping if write tracking is enabled.
	 */
	if (kctx->gwt_enabled) {
		u64 fault_page_addr = fault->addr & PAGE_MASK;
		bool found = false;
		/* Check if this write was already handled. */
		list_for_each_entry(pos, &kctx->gwt_current_list, link) {
			if (fault_page_addr == pos->page_addr) {
				found = true;
				break;
			}
		}

		if (!found) {
			pos = kmalloc(sizeof(*pos), GFP_KERNEL);
			if (pos) {
				pos->region = region;
				set_gwt_element_page_addr_and_size(pos,
					fault_page_addr, *fault_phys_addr);
				list_add(&pos->link, &kctx->gwt_current_list);
			} else {
				dev_warn(kbdev->dev, "kmalloc failure");
			}
		}
	}

	/* Now make this faulting page writable to GPU. */
	kbase_mmu_update_pages_no_flush(kbdev, &kctx->mmu, fault_pfn, fault_phys_addr, 1,
					region->flags, region->gpu_alloc->group_id, &dirty_pgds);

	kbase_gpu_mmu_handle_write_faulting_as(kbdev, faulting_as, fault_pfn, 1,
					       kctx->id, dirty_pgds);

	kbase_gpu_vm_unlock(kctx);
}

static void kbase_gpu_mmu_handle_permission_fault(struct kbase_context *kctx,
			struct kbase_as	*faulting_as)
{
	struct kbase_fault *fault = &faulting_as->pf_data;

	switch (AS_FAULTSTATUS_ACCESS_TYPE_GET(fault->status)) {
	case AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC:
	case AS_FAULTSTATUS_ACCESS_TYPE_WRITE:
		kbase_gpu_mmu_handle_write_fault(kctx, faulting_as);
		break;
	case AS_FAULTSTATUS_ACCESS_TYPE_EX:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Execute Permission fault", fault);
		break;
	case AS_FAULTSTATUS_ACCESS_TYPE_READ:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Read Permission fault", fault);
		break;
	default:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Unknown Permission fault", fault);
		break;
	}
}
#endif

/**
 * estimate_pool_space_required - Determine how much a pool should be grown by to support a future
 * allocation
 * @pool:           The memory pool to check, including its linked pools
 * @pages_required: Number of 4KiB pages require for the pool to support a future allocation
 *
 * The value returned is accounting for the size of @pool and the size of each memory pool linked to
 * @pool. Hence, the caller should use @pool and (if not already satisfied) all its linked pools to
 * allocate from.
 *
 * Note: this is only an estimate, because even during the calculation the memory pool(s) involved
 * can be updated to be larger or smaller. Hence, the result is only a guide as to whether an
 * allocation could succeed, or an estimate of the correct amount to grow the pool by. The caller
 * should keep attempting an allocation and then re-growing with a new value queried form this
 * function until the allocation succeeds.
 *
 * Return: an estimate of the amount of extra 4KiB pages in @pool that are required to satisfy an
 * allocation, or 0 if @pool (including its linked pools) is likely to already satisfy the
 * allocation.
 */
static size_t estimate_pool_space_required(struct kbase_mem_pool *pool, const size_t pages_required)
{
	size_t pages_still_required;

	for (pages_still_required = pages_required; pool != NULL && pages_still_required;
	     pool = pool->next_pool) {
		size_t pool_size_4k;

		kbase_mem_pool_lock(pool);

		pool_size_4k = kbase_mem_pool_size(pool) << pool->order;
		if (pool_size_4k >= pages_still_required)
			pages_still_required = 0;
		else
			pages_still_required -= pool_size_4k;

		kbase_mem_pool_unlock(pool);
	}
	return pages_still_required;
}

/**
 * page_fault_try_alloc - Try to allocate memory from a context pool
 * @kctx:          Context pointer
 * @region:        Region to grow
 * @new_pages:     Number of 4 KiB pages to allocate
 * @pages_to_grow: Pointer to variable to store number of outstanding pages on failure. This can be
 *                 either 4 KiB or 2 MiB pages, depending on the number of pages requested.
 * @grow_2mb_pool: Pointer to variable to store which pool needs to grow - true for 2 MiB, false for
 *                 4 KiB.
 * @prealloc_sas:  Pointer to kbase_sub_alloc structures
 *
 * This function will try to allocate as many pages as possible from the context pool, then if
 * required will try to allocate the remaining pages from the device pool.
 *
 * This function will not allocate any new memory beyond that is already present in the context or
 * device pools. This is because it is intended to be called whilst the thread has acquired the
 * region list lock with kbase_gpu_vm_lock(), and a large enough memory allocation whilst that is
 * held could invoke the OoM killer and cause an effective deadlock with kbase_cpu_vm_close().
 *
 * If 2 MiB pages are enabled and new_pages is >= 2 MiB then pages_to_grow will be a count of 2 MiB
 * pages, otherwise it will be a count of 4 KiB pages.
 *
 * Return: true if successful, false on failure
 */
static bool page_fault_try_alloc(struct kbase_context *kctx,
		struct kbase_va_region *region, size_t new_pages,
		int *pages_to_grow, bool *grow_2mb_pool,
		struct kbase_sub_alloc **prealloc_sas)
{
	size_t total_gpu_pages_alloced = 0;
	size_t total_cpu_pages_alloced = 0;
	struct kbase_mem_pool *pool, *root_pool;
	bool alloc_failed = false;
	size_t pages_still_required;
	size_t total_mempools_free_4k = 0;

	lockdep_assert_held(&kctx->reg_lock);
	lockdep_assert_held(&kctx->mem_partials_lock);

	if (WARN_ON(region->gpu_alloc->group_id >=
		MEMORY_GROUP_MANAGER_NR_GROUPS)) {
		/* Do not try to grow the memory pool */
		*pages_to_grow = 0;
		return false;
	}

	if (kctx->kbdev->pagesize_2mb && new_pages >= (SZ_2M / SZ_4K)) {
		root_pool = &kctx->mem_pools.large[region->gpu_alloc->group_id];
		*grow_2mb_pool = true;
	} else {
		root_pool = &kctx->mem_pools.small[region->gpu_alloc->group_id];
		*grow_2mb_pool = false;
	}

	if (region->gpu_alloc != region->cpu_alloc)
		new_pages *= 2;

	/* Determine how many pages are in the pools before trying to allocate.
	 * Don't attempt to allocate & free if the allocation can't succeed.
	 */
	pages_still_required = estimate_pool_space_required(root_pool, new_pages);

	if (pages_still_required) {
		/* Insufficient pages in pools. Don't try to allocate - just
		 * request a grow.
		 */
		*pages_to_grow = pages_still_required;

		return false;
	}

	/* Since we're not holding any of the mempool locks, the amount of memory in the pools may
	 * change between the above estimate and the actual allocation.
	 */
	pages_still_required = new_pages;
	for (pool = root_pool; pool != NULL && pages_still_required; pool = pool->next_pool) {
		size_t pool_size_4k;
		size_t pages_to_alloc_4k;
		size_t pages_to_alloc_4k_per_alloc;

		kbase_mem_pool_lock(pool);

		/* Allocate as much as possible from this pool*/
		pool_size_4k = kbase_mem_pool_size(pool) << pool->order;
		total_mempools_free_4k += pool_size_4k;
		pages_to_alloc_4k = MIN(pages_still_required, pool_size_4k);
		if (region->gpu_alloc == region->cpu_alloc)
			pages_to_alloc_4k_per_alloc = pages_to_alloc_4k;
		else
			pages_to_alloc_4k_per_alloc = pages_to_alloc_4k >> 1;

		if (pages_to_alloc_4k) {
			struct tagged_addr *gpu_pages =
				kbase_alloc_phy_pages_helper_locked(region->gpu_alloc, pool,
								    pages_to_alloc_4k_per_alloc,
								    &prealloc_sas[0]);

			if (!gpu_pages)
				alloc_failed = true;
			else
				total_gpu_pages_alloced += pages_to_alloc_4k_per_alloc;

			if (!alloc_failed && region->gpu_alloc != region->cpu_alloc) {
				struct tagged_addr *cpu_pages = kbase_alloc_phy_pages_helper_locked(
					region->cpu_alloc, pool, pages_to_alloc_4k_per_alloc,
					&prealloc_sas[1]);

				if (!cpu_pages)
					alloc_failed = true;
				else
					total_cpu_pages_alloced += pages_to_alloc_4k_per_alloc;
			}
		}

		kbase_mem_pool_unlock(pool);

		if (alloc_failed) {
			WARN_ON(!pages_still_required);
			WARN_ON(pages_to_alloc_4k >= pages_still_required);
			WARN_ON(pages_to_alloc_4k_per_alloc >= pages_still_required);
			break;
		}

		pages_still_required -= pages_to_alloc_4k;
	}

	if (pages_still_required) {
		/* Allocation was unsuccessful. We have dropped the mem_pool lock after allocation,
		 * so must in any case use kbase_free_phy_pages_helper() rather than
		 * kbase_free_phy_pages_helper_locked()
		 */
		if (total_gpu_pages_alloced > 0)
			kbase_free_phy_pages_helper(region->gpu_alloc, total_gpu_pages_alloced);
		if (region->gpu_alloc != region->cpu_alloc && total_cpu_pages_alloced > 0)
			kbase_free_phy_pages_helper(region->cpu_alloc, total_cpu_pages_alloced);

		if (alloc_failed) {
			/* Note that in allocating from the above memory pools, we always ensure
			 * never to request more than is available in each pool with the pool's
			 * lock held. Hence failing to allocate in such situations would be unusual
			 * and we should cancel the growth instead (as re-growing the memory pool
			 * might not fix the situation)
			 */
			dev_warn(
				kctx->kbdev->dev,
				"Page allocation failure of %zu pages: managed %zu pages, mempool (inc linked pools) had %zu pages available",
				new_pages, total_gpu_pages_alloced + total_cpu_pages_alloced,
				total_mempools_free_4k);
			*pages_to_grow = 0;
		} else {
			/* Tell the caller to try to grow the memory pool
			 *
			 * Freeing pages above may have spilled or returned them to the OS, so we
			 * have to take into account how many are still in the pool before giving a
			 * new estimate for growth required of the pool. We can just re-estimate a
			 * new value.
			 */
			pages_still_required = estimate_pool_space_required(root_pool, new_pages);
			if (pages_still_required) {
				*pages_to_grow = pages_still_required;
			} else {
				/* It's possible another thread could've grown the pool to be just
				 * big enough after we rolled back the allocation. Request at least
				 * one more page to ensure the caller doesn't fail the growth by
				 * conflating it with the alloc_failed case above
				 */
				*pages_to_grow = 1u;
			}
		}

		return false;
	}

	/* Allocation was successful. No pages to grow, return success. */
	*pages_to_grow = 0;

	return true;
}

void kbase_mmu_page_fault_worker(struct work_struct *data)
{
	u64 fault_pfn;
	u32 fault_status;
	size_t new_pages;
	size_t fault_rel_pfn;
	struct kbase_as *faulting_as;
	int as_no;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	struct kbase_va_region *region;
	struct kbase_fault *fault;
	int err;
	bool grown = false;
	int pages_to_grow;
	bool grow_2mb_pool;
	struct kbase_sub_alloc *prealloc_sas[2] = { NULL, NULL };
	int i;
	size_t current_backed_size;
#if MALI_JIT_PRESSURE_LIMIT_BASE
	size_t pages_trimmed = 0;
#endif

	/* Calls to this function are inherently synchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_SYNC;

	faulting_as = container_of(data, struct kbase_as, work_pagefault);
	fault = &faulting_as->pf_data;
	fault_pfn = fault->addr >> PAGE_SHIFT;
	as_no = faulting_as->number;

	kbdev = container_of(faulting_as, struct kbase_device, as[as_no]);
	dev_dbg(kbdev->dev, "Entering %s %pK, fault_pfn %lld, as_no %d", __func__, (void *)data,
		fault_pfn, as_no);

	/* Grab the context that was already refcounted in kbase_mmu_interrupt()
	 * Therefore, it cannot be scheduled out of this AS until we explicitly
	 * release it
	 */
	kctx = kbase_ctx_sched_as_to_ctx(kbdev, as_no);
	if (!kctx) {
		atomic_dec(&kbdev->faults_pending);
		return;
	}

	KBASE_DEBUG_ASSERT(kctx->kbdev == kbdev);

#if MALI_JIT_PRESSURE_LIMIT_BASE
#if !MALI_USE_CSF
	mutex_lock(&kctx->jctx.lock);
#endif
#endif

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	/* check if we still have GPU */
	if (unlikely(kbase_is_gpu_removed(kbdev))) {
		dev_dbg(kbdev->dev, "%s: GPU has been removed", __func__);
		goto fault_done;
	}
#endif

	if (unlikely(fault->protected_mode)) {
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Protected mode fault", fault);
		kbase_mmu_hw_clear_fault(kbdev, faulting_as,
				KBASE_MMU_FAULT_TYPE_PAGE);

		goto fault_done;
	}

	fault_status = fault->status;
	switch (fault_status & AS_FAULTSTATUS_EXCEPTION_CODE_MASK) {

	case AS_FAULTSTATUS_EXCEPTION_CODE_TRANSLATION_FAULT:
		/* need to check against the region to handle this one */
		break;

	case AS_FAULTSTATUS_EXCEPTION_CODE_PERMISSION_FAULT:
#ifdef CONFIG_MALI_CINSTR_GWT
		/* If GWT was ever enabled then we need to handle
		 * write fault pages even if the feature was disabled later.
		 */
		if (kctx->gwt_was_enabled) {
			kbase_gpu_mmu_handle_permission_fault(kctx,
							faulting_as);
			goto fault_done;
		}
#endif

		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Permission failure", fault);
		goto fault_done;

	case AS_FAULTSTATUS_EXCEPTION_CODE_TRANSTAB_BUS_FAULT:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Translation table bus fault", fault);
		goto fault_done;

	case AS_FAULTSTATUS_EXCEPTION_CODE_ACCESS_FLAG:
		/* nothing to do, but we don't expect this fault currently */
		dev_warn(kbdev->dev, "Access flag unexpectedly set");
		goto fault_done;

	case AS_FAULTSTATUS_EXCEPTION_CODE_ADDRESS_SIZE_FAULT:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Address size fault", fault);
		goto fault_done;

	case AS_FAULTSTATUS_EXCEPTION_CODE_MEMORY_ATTRIBUTES_FAULT:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Memory attributes fault", fault);
		goto fault_done;

	default:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Unknown fault code", fault);
		goto fault_done;
	}

page_fault_retry:
	if (kbdev->pagesize_2mb) {
		/* Preallocate (or re-allocate) memory for the sub-allocation structs if necessary */
		for (i = 0; i != ARRAY_SIZE(prealloc_sas); ++i) {
			if (!prealloc_sas[i]) {
				prealloc_sas[i] = kmalloc(sizeof(*prealloc_sas[i]), GFP_KERNEL);

				if (!prealloc_sas[i]) {
					kbase_mmu_report_fault_and_kill(
						kctx, faulting_as,
						"Failed pre-allocating memory for sub-allocations' metadata",
						fault);
					goto fault_done;
				}
			}
		}
	}

	/* so we have a translation fault,
	 * let's see if it is for growable memory
	 */
	kbase_gpu_vm_lock(kctx);

	region = kbase_region_tracker_find_region_enclosing_address(kctx,
			fault->addr);
	if (kbase_is_region_invalid_or_free(region)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Memory is not mapped on the GPU", fault);
		goto fault_done;
	}

	if (region->gpu_alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"DMA-BUF is not mapped on the GPU", fault);
		goto fault_done;
	}

	if (region->gpu_alloc->group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Bad physical memory group ID", fault);
		goto fault_done;
	}

	if ((region->flags & GROWABLE_FLAGS_REQUIRED)
			!= GROWABLE_FLAGS_REQUIRED) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Memory is not growable", fault);
		goto fault_done;
	}

	if ((region->flags & KBASE_REG_DONT_NEED)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Don't need memory can't be grown", fault);
		goto fault_done;
	}

	if (AS_FAULTSTATUS_ACCESS_TYPE_GET(fault_status) ==
		AS_FAULTSTATUS_ACCESS_TYPE_READ)
		dev_warn(kbdev->dev, "Grow on pagefault while reading");

	/* find the size we need to grow it by
	 * we know the result fit in a size_t due to
	 * kbase_region_tracker_find_region_enclosing_address
	 * validating the fault_address to be within a size_t from the start_pfn
	 */
	fault_rel_pfn = fault_pfn - region->start_pfn;

	current_backed_size = kbase_reg_current_backed_size(region);

	if (fault_rel_pfn < current_backed_size) {
		struct kbase_mmu_hw_op_param op_param;

		dev_dbg(kbdev->dev,
			"Page fault @ 0x%llx in allocated region 0x%llx-0x%llx of growable TMEM: Ignoring",
				fault->addr, region->start_pfn,
				region->start_pfn +
				current_backed_size);

		mutex_lock(&kbdev->mmu_hw_mutex);

		kbase_mmu_hw_clear_fault(kbdev, faulting_as,
				KBASE_MMU_FAULT_TYPE_PAGE);
		/* [1] in case another page fault occurred while we were
		 * handling the (duplicate) page fault we need to ensure we
		 * don't loose the other page fault as result of us clearing
		 * the MMU IRQ. Therefore, after we clear the MMU IRQ we send
		 * an UNLOCK command that will retry any stalled memory
		 * transaction (which should cause the other page fault to be
		 * raised again).
		 */
		op_param.mmu_sync_info = mmu_sync_info;
		op_param.kctx_id = kctx->id;
		if (!mmu_flush_cache_on_gpu_ctrl(kbdev)) {
			mmu_hw_operation_begin(kbdev);
			err = kbase_mmu_hw_do_unlock_no_addr(kbdev, faulting_as,
							     &op_param);
			mmu_hw_operation_end(kbdev);
		} else {
			/* Can safely skip the invalidate for all levels in case
			 * of duplicate page faults.
			 */
			op_param.flush_skip_levels = 0xF;
			op_param.vpfn = fault_pfn;
			op_param.nr = 1;
			err = kbase_mmu_hw_do_unlock(kbdev, faulting_as,
						     &op_param);
		}

		if (err) {
			dev_err(kbdev->dev,
				"Invalidation for MMU did not complete on handling page fault @ 0x%llx",
				fault->addr);
		}

		mutex_unlock(&kbdev->mmu_hw_mutex);

		kbase_mmu_hw_enable_fault(kbdev, faulting_as,
				KBASE_MMU_FAULT_TYPE_PAGE);
		kbase_gpu_vm_unlock(kctx);

		goto fault_done;
	}

	new_pages = reg_grow_calc_extra_pages(kbdev, region, fault_rel_pfn);

	/* cap to max vsize */
	new_pages = min(new_pages, region->nr_pages - current_backed_size);
	dev_dbg(kctx->kbdev->dev, "Allocate %zu pages on page fault", new_pages);

	if (new_pages == 0) {
		struct kbase_mmu_hw_op_param op_param;

		mutex_lock(&kbdev->mmu_hw_mutex);

		/* Duplicate of a fault we've already handled, nothing to do */
		kbase_mmu_hw_clear_fault(kbdev, faulting_as,
				KBASE_MMU_FAULT_TYPE_PAGE);

		/* See comment [1] about UNLOCK usage */
		op_param.mmu_sync_info = mmu_sync_info;
		op_param.kctx_id = kctx->id;
		if (!mmu_flush_cache_on_gpu_ctrl(kbdev)) {
			mmu_hw_operation_begin(kbdev);
			err = kbase_mmu_hw_do_unlock_no_addr(kbdev, faulting_as,
							     &op_param);
			mmu_hw_operation_end(kbdev);
		} else {
			/* Can safely skip the invalidate for all levels in case
			 * of duplicate page faults.
			 */
			op_param.flush_skip_levels = 0xF;
			op_param.vpfn = fault_pfn;
			op_param.nr = 1;
			err = kbase_mmu_hw_do_unlock(kbdev, faulting_as,
						     &op_param);
		}

		if (err) {
			dev_err(kbdev->dev,
				"Invalidation for MMU did not complete on handling page fault @ 0x%llx",
				fault->addr);
		}

		mutex_unlock(&kbdev->mmu_hw_mutex);

		kbase_mmu_hw_enable_fault(kbdev, faulting_as,
				KBASE_MMU_FAULT_TYPE_PAGE);
		kbase_gpu_vm_unlock(kctx);
		goto fault_done;
	}

	pages_to_grow = 0;

#if MALI_JIT_PRESSURE_LIMIT_BASE
	if ((region->flags & KBASE_REG_ACTIVE_JIT_ALLOC) && !pages_trimmed) {
		kbase_jit_request_phys_increase(kctx, new_pages);
		pages_trimmed = new_pages;
	}
#endif

	spin_lock(&kctx->mem_partials_lock);
	grown = page_fault_try_alloc(kctx, region, new_pages, &pages_to_grow,
			&grow_2mb_pool, prealloc_sas);
	spin_unlock(&kctx->mem_partials_lock);

	if (grown) {
		u64 dirty_pgds = 0;
		u64 pfn_offset;
		struct kbase_mmu_hw_op_param op_param;

		/* alloc success */
		WARN_ON(kbase_reg_current_backed_size(region) >
			region->nr_pages);

		/* set up the new pages */
		pfn_offset = kbase_reg_current_backed_size(region) - new_pages;
		/*
		 * Note:
		 * Issuing an MMU operation will unlock the MMU and cause the
		 * translation to be replayed. If the page insertion fails then
		 * rather then trying to continue the context should be killed
		 * so the no_flush version of insert_pages is used which allows
		 * us to unlock the MMU as we see fit.
		 */
		err = kbase_mmu_insert_pages_no_flush(
			kbdev, &kctx->mmu, region->start_pfn + pfn_offset,
			&kbase_get_gpu_phy_pages(region)[pfn_offset], new_pages, region->flags,
			region->gpu_alloc->group_id, &dirty_pgds, region, false);
		if (err) {
			kbase_free_phy_pages_helper(region->gpu_alloc,
					new_pages);
			if (region->gpu_alloc != region->cpu_alloc)
				kbase_free_phy_pages_helper(region->cpu_alloc,
						new_pages);
			kbase_gpu_vm_unlock(kctx);
			/* The locked VA region will be unlocked and the cache
			 * invalidated in here
			 */
			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
					"Page table update failure", fault);
			goto fault_done;
		}
		KBASE_TLSTREAM_AUX_PAGEFAULT(kbdev, kctx->id, as_no,
				(u64)new_pages);
		trace_mali_mmu_page_fault_grow(region, fault, new_pages);

#if MALI_INCREMENTAL_RENDERING_JM
		/* Switch to incremental rendering if we have nearly run out of
		 * memory in a JIT memory allocation.
		 */
		if (region->threshold_pages &&
			kbase_reg_current_backed_size(region) >
				region->threshold_pages) {
			dev_dbg(kctx->kbdev->dev, "%zu pages exceeded IR threshold %zu",
				new_pages + current_backed_size, region->threshold_pages);

			if (kbase_mmu_switch_to_ir(kctx, region) >= 0) {
				dev_dbg(kctx->kbdev->dev, "Get region %pK for IR", (void *)region);
				kbase_va_region_alloc_get(kctx, region);
			}
		}
#endif

		/* AS transaction begin */
		mutex_lock(&kbdev->mmu_hw_mutex);

		/* clear MMU interrupt - this needs to be done after updating
		 * the page tables but before issuing a FLUSH command. The
		 * FLUSH cmd has a side effect that it restarts stalled memory
		 * transactions in other address spaces which may cause
		 * another fault to occur. If we didn't clear the interrupt at
		 * this stage a new IRQ might not be raised when the GPU finds
		 * a MMU IRQ is already pending.
		 */
		kbase_mmu_hw_clear_fault(kbdev, faulting_as,
					 KBASE_MMU_FAULT_TYPE_PAGE);

		op_param.vpfn = region->start_pfn + pfn_offset;
		op_param.nr = new_pages;
		op_param.op = KBASE_MMU_OP_FLUSH_PT;
		op_param.kctx_id = kctx->id;
		op_param.mmu_sync_info = mmu_sync_info;
		if (mmu_flush_cache_on_gpu_ctrl(kbdev)) {
			/* Unlock to invalidate the TLB (and resume the MMU) */
			op_param.flush_skip_levels =
				pgd_level_to_skip_flush(dirty_pgds);
			err = kbase_mmu_hw_do_unlock(kbdev, faulting_as,
						     &op_param);
		} else {
			/* flush L2 and unlock the VA (resumes the MMU) */
			mmu_hw_operation_begin(kbdev);
			err = kbase_mmu_hw_do_flush(kbdev, faulting_as,
						    &op_param);
			mmu_hw_operation_end(kbdev);
		}

		if (err) {
			dev_err(kbdev->dev,
				"Flush for GPU page table update did not complete on handling page fault @ 0x%llx",
				fault->addr);
		}

		mutex_unlock(&kbdev->mmu_hw_mutex);
		/* AS transaction end */

		/* reenable this in the mask */
		kbase_mmu_hw_enable_fault(kbdev, faulting_as,
					 KBASE_MMU_FAULT_TYPE_PAGE);

#ifdef CONFIG_MALI_CINSTR_GWT
		if (kctx->gwt_enabled) {
			/* GWT also tracks growable regions. */
			struct kbasep_gwt_list_element *pos;

			pos = kmalloc(sizeof(*pos), GFP_KERNEL);
			if (pos) {
				pos->region = region;
				pos->page_addr = (region->start_pfn +
							pfn_offset) <<
							 PAGE_SHIFT;
				pos->num_pages = new_pages;
				list_add(&pos->link,
					&kctx->gwt_current_list);
			} else {
				dev_warn(kbdev->dev, "kmalloc failure");
			}
		}
#endif

#if MALI_JIT_PRESSURE_LIMIT_BASE
		if (pages_trimmed) {
			kbase_jit_done_phys_increase(kctx, pages_trimmed);
			pages_trimmed = 0;
		}
#endif
		kbase_gpu_vm_unlock(kctx);
	} else {
		int ret = -ENOMEM;

		kbase_gpu_vm_unlock(kctx);

		/* If the memory pool was insufficient then grow it and retry.
		 * Otherwise fail the allocation.
		 */
		if (pages_to_grow > 0) {
			if (kbdev->pagesize_2mb && grow_2mb_pool) {
				/* Round page requirement up to nearest 2 MB */
				struct kbase_mem_pool *const lp_mem_pool =
					&kctx->mem_pools.large[
					region->gpu_alloc->group_id];

				pages_to_grow = (pages_to_grow +
					((1 << lp_mem_pool->order) - 1))
						>> lp_mem_pool->order;

				ret = kbase_mem_pool_grow(lp_mem_pool,
					pages_to_grow, kctx->task);
			} else {
				struct kbase_mem_pool *const mem_pool =
					&kctx->mem_pools.small[
					region->gpu_alloc->group_id];

				ret = kbase_mem_pool_grow(mem_pool,
					pages_to_grow, kctx->task);
			}
		}
		if (ret < 0) {
			/* failed to extend, handle as a normal PF */
			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
					"Page allocation failure", fault);
		} else {
			dev_dbg(kbdev->dev, "Try again after pool_grow");
			goto page_fault_retry;
		}
	}

fault_done:
#if MALI_JIT_PRESSURE_LIMIT_BASE
	if (pages_trimmed) {
		kbase_gpu_vm_lock(kctx);
		kbase_jit_done_phys_increase(kctx, pages_trimmed);
		kbase_gpu_vm_unlock(kctx);
	}
#if !MALI_USE_CSF
	mutex_unlock(&kctx->jctx.lock);
#endif
#endif

	for (i = 0; i != ARRAY_SIZE(prealloc_sas); ++i)
		kfree(prealloc_sas[i]);

	/*
	 * By this point, the fault was handled in some way,
	 * so release the ctx refcount
	 */
	release_ctx(kbdev, kctx);

	atomic_dec(&kbdev->faults_pending);
	dev_dbg(kbdev->dev, "Leaving page_fault_worker %pK", (void *)data);
}

static phys_addr_t kbase_mmu_alloc_pgd(struct kbase_device *kbdev,
		struct kbase_mmu_table *mmut)
{
	u64 *page;
	struct page *p;
	phys_addr_t pgd;

	p = kbase_mem_pool_alloc(&kbdev->mem_pools.small[mmut->group_id]);
	if (!p)
		return KBASE_MMU_INVALID_PGD_ADDRESS;

	page = kmap(p);
	if (page == NULL)
		goto alloc_free;

	pgd = page_to_phys(p);

	/* If the MMU tables belong to a context then account the memory usage
	 * to that context, otherwise the MMU tables are device wide and are
	 * only accounted to the device.
	 */
	if (mmut->kctx) {
		int new_page_count;

		new_page_count = atomic_add_return(1,
			&mmut->kctx->used_pages);
		KBASE_TLSTREAM_AUX_PAGESALLOC(
			kbdev,
			mmut->kctx->id,
			(u64)new_page_count);
		kbase_process_page_usage_inc(mmut->kctx, 1);
	}

	atomic_add(1, &kbdev->memdev.used_pages);

	kbase_trace_gpu_mem_usage_inc(kbdev, mmut->kctx, 1);

	kbdev->mmu_mode->entries_invalidate(page, KBASE_MMU_PAGE_ENTRIES);

	/* As this page is newly created, therefore there is no content to
	 * clean or invalidate in the GPU caches.
	 */
	kbase_mmu_sync_pgd_cpu(kbdev, kbase_dma_addr(p), PAGE_SIZE);

	kunmap(p);
	return pgd;

alloc_free:
	kbase_mem_pool_free(&kbdev->mem_pools.small[mmut->group_id], p, false);

	return KBASE_MMU_INVALID_PGD_ADDRESS;
}

/**
 * mmu_get_next_pgd() - Given PGD PFN for level N, return PGD PFN for level N+1
 *
 * @kbdev:    Device pointer.
 * @mmut:     GPU MMU page table.
 * @pgd:      Physical addresse of level N page directory.
 * @vpfn:     The virtual page frame number.
 * @level:    The level of MMU page table (N).
 *
 * Return:
 * * 0 - OK
 * * -EFAULT - level N+1 PGD does not exist
 * * -EINVAL - kmap() failed for level N PGD PFN
 */
static int mmu_get_next_pgd(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
			    phys_addr_t *pgd, u64 vpfn, int level)
{
	u64 *page;
	phys_addr_t target_pgd;
	struct page *p;

	lockdep_assert_held(&mmut->mmu_lock);

	/*
	 * Architecture spec defines level-0 as being the top-most.
	 * This is a bit unfortunate here, but we keep the same convention.
	 */
	vpfn >>= (3 - level) * 9;
	vpfn &= 0x1FF;

	p = pfn_to_page(PFN_DOWN(*pgd));
	page = kmap(p);
	if (page == NULL) {
		dev_err(kbdev->dev, "%s: kmap failure", __func__);
		return -EINVAL;
	}

	if (!kbdev->mmu_mode->pte_is_valid(page[vpfn], level)) {
		dev_dbg(kbdev->dev, "%s: invalid PTE at level %d vpfn 0x%llx", __func__, level,
			vpfn);
		kunmap(p);
		return -EFAULT;
	} else {
		target_pgd = kbdev->mmu_mode->pte_to_phy_addr(
			kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
				kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, page[vpfn]));
	}

	kunmap(p);
	*pgd = target_pgd;

	return 0;
}

/**
 * mmu_get_lowest_valid_pgd() - Find a valid PGD at or closest to in_level
 *
 * @kbdev:    Device pointer.
 * @mmut:     GPU MMU page table.
 * @vpfn:     The virtual page frame number.
 * @in_level:     The level of MMU page table (N).
 * @out_level:    Set to the level of the lowest valid PGD found on success.
 *                Invalid on error.
 * @out_pgd:      Set to the lowest valid PGD found on success.
 *                Invalid on error.
 *
 * Does a page table walk starting from top level (L0) to in_level to find a valid PGD at or
 * closest to in_level
 *
 * Terminology:
 * Level-0 = Top-level = highest
 * Level-3 = Bottom-level = lowest
 *
 * Return:
 * * 0 - OK
 * * -EINVAL - kmap() failed during page table walk.
 */
static int mmu_get_lowest_valid_pgd(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				    u64 vpfn, int in_level, int *out_level, phys_addr_t *out_pgd)
{
	phys_addr_t pgd;
	int l;
	int err = 0;

	lockdep_assert_held(&mmut->mmu_lock);
	pgd = mmut->pgd;

	for (l = MIDGARD_MMU_TOPLEVEL; l < in_level; l++) {
		err = mmu_get_next_pgd(kbdev, mmut, &pgd, vpfn, l);

		/* Handle failure condition */
		if (err) {
			dev_dbg(kbdev->dev,
				"%s: mmu_get_next_pgd() failed to find a valid pgd at level %d",
				__func__, l + 1);
			break;
		}
	}

	*out_pgd = pgd;
	*out_level = l;

	/* -EFAULT indicates that pgd param was valid but the next pgd entry at vpfn was invalid.
	 * This implies that we have found the lowest valid pgd. Reset the error code.
	 */
	if (err == -EFAULT)
		err = 0;

	return err;
}

/*
 * On success, sets out_pgd to the PGD for the specified level of translation
 * Returns -EFAULT if a valid PGD is not found
 */
static int mmu_get_pgd_at_level(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, u64 vpfn,
				int level, phys_addr_t *out_pgd)
{
	phys_addr_t pgd;
	int l;

	lockdep_assert_held(&mmut->mmu_lock);
	pgd = mmut->pgd;

	for (l = MIDGARD_MMU_TOPLEVEL; l < level; l++) {
		int err = mmu_get_next_pgd(kbdev, mmut, &pgd, vpfn, l);
		/* Handle failure condition */
		if (err) {
			dev_err(kbdev->dev,
				"%s: mmu_get_next_pgd() failed to find a valid pgd at level %d",
				__func__, l + 1);
			return err;
		}
	}

	*out_pgd = pgd;

	return 0;
}

static void mmu_insert_pages_failure_recovery(struct kbase_device *kbdev,
					      struct kbase_mmu_table *mmut, u64 from_vpfn,
					      u64 to_vpfn, u64 *dirty_pgds,
					      struct tagged_addr *phys, bool ignore_page_migration)
{
	u64 vpfn = from_vpfn;
	struct kbase_mmu_mode const *mmu_mode;

	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(vpfn <= (U64_MAX / PAGE_SIZE));
	KBASE_DEBUG_ASSERT(from_vpfn <= to_vpfn);

	lockdep_assert_held(&mmut->mmu_lock);

	mmu_mode = kbdev->mmu_mode;
	kbase_mmu_reset_free_pgds_list(mmut);

	while (vpfn < to_vpfn) {
		unsigned int idx = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - idx;
		unsigned int pcount = 0;
		unsigned int left = to_vpfn - vpfn;
		int level;
		u64 *page;
		phys_addr_t pgds[MIDGARD_MMU_BOTTOMLEVEL + 1];
		phys_addr_t pgd = mmut->pgd;
		struct page *p = phys_to_page(pgd);

		register unsigned int num_of_valid_entries;

		if (count > left)
			count = left;

		/* need to check if this is a 2MB page or a 4kB */
		for (level = MIDGARD_MMU_TOPLEVEL;
				level <= MIDGARD_MMU_BOTTOMLEVEL; level++) {
			idx = (vpfn >> ((3 - level) * 9)) & 0x1FF;
			pgds[level] = pgd;
			page = kmap(p);
			if (mmu_mode->ate_is_valid(page[idx], level))
				break; /* keep the mapping */
			kunmap(p);
			pgd = mmu_mode->pte_to_phy_addr(kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
				kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, page[idx]));
			p = phys_to_page(pgd);
		}

		switch (level) {
		case MIDGARD_MMU_LEVEL(2):
			/* remap to single entry to update */
			pcount = 1;
			break;
		case MIDGARD_MMU_BOTTOMLEVEL:
			/* page count is the same as the logical count */
			pcount = count;
			break;
		default:
			dev_warn(kbdev->dev, "%sNo support for ATEs at level %d", __func__, level);
			goto next;
		}

		if (dirty_pgds && pcount > 0)
			*dirty_pgds |= 1ULL << level;

		num_of_valid_entries = mmu_mode->get_num_valid_entries(page);
		if (WARN_ON_ONCE(num_of_valid_entries < pcount))
			num_of_valid_entries = 0;
		else
			num_of_valid_entries -= pcount;

		/* Invalidate the entries we added */
		mmu_mode->entries_invalidate(&page[idx], pcount);

		if (!num_of_valid_entries) {
			kunmap(p);

			kbase_mmu_add_to_free_pgds_list(mmut, p);

			kbase_mmu_update_and_free_parent_pgds(kbdev, mmut, pgds, vpfn, level,
							      KBASE_MMU_OP_NONE, dirty_pgds);
			vpfn += count;
			continue;
		}

		mmu_mode->set_num_valid_entries(page, num_of_valid_entries);

		/* MMU cache flush strategy is NONE because GPU cache maintenance is
		 * going to be done by the caller
		 */
		kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (idx * sizeof(u64)),
				   kbase_dma_addr(p) + sizeof(u64) * idx, sizeof(u64) * pcount,
				   KBASE_MMU_OP_NONE);
		kunmap(p);
next:
		vpfn += count;
	}

	/* If page migration is enabled: the only way to recover from failure
	 * is to mark all pages as not movable. It is not predictable what's
	 * going to happen to these pages at this stage. They might return
	 * movable once they are returned to a memory pool.
	 */
	if (kbase_page_migration_enabled && !ignore_page_migration && phys) {
		const u64 num_pages = to_vpfn - from_vpfn + 1;
		u64 i;

		for (i = 0; i < num_pages; i++) {
			struct page *phys_page = as_page(phys[i]);
			struct kbase_page_metadata *page_md = kbase_page_private(phys_page);

			if (page_md) {
				spin_lock(&page_md->migrate_lock);
				page_md->status = PAGE_STATUS_SET(page_md->status, (u8)NOT_MOVABLE);
				spin_unlock(&page_md->migrate_lock);
			}
		}
	}
}

static void mmu_flush_invalidate_insert_pages(struct kbase_device *kbdev,
					      struct kbase_mmu_table *mmut, const u64 vpfn,
					      size_t nr, u64 dirty_pgds,
					      enum kbase_caller_mmu_sync_info mmu_sync_info,
					      bool insert_pages_failed)
{
	struct kbase_mmu_hw_op_param op_param;
	int as_nr = 0;

	op_param.vpfn = vpfn;
	op_param.nr = nr;
	op_param.op = KBASE_MMU_OP_FLUSH_PT;
	op_param.mmu_sync_info = mmu_sync_info;
	op_param.kctx_id = mmut->kctx ? mmut->kctx->id : 0xFFFFFFFF;
	op_param.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds);

#if MALI_USE_CSF
	as_nr = mmut->kctx ? mmut->kctx->as_nr : MCU_AS_NR;
#else
	WARN_ON(!mmut->kctx);
#endif

	/* MMU cache flush strategy depends on whether GPU control commands for
	 * flushing physical address ranges are supported. The new physical pages
	 * are not present in GPU caches therefore they don't need any cache
	 * maintenance, but PGDs in the page table may or may not be created anew.
	 *
	 * Operations that affect the whole GPU cache shall only be done if it's
	 * impossible to update physical ranges.
	 *
	 * On GPUs where flushing by physical address range is supported,
	 * full cache flush is done when an error occurs during
	 * insert_pages() to keep the error handling simpler.
	 */
	if (mmu_flush_cache_on_gpu_ctrl(kbdev) && !insert_pages_failed)
		mmu_invalidate(kbdev, mmut->kctx, as_nr, &op_param);
	else
		mmu_flush_invalidate(kbdev, mmut->kctx, as_nr, &op_param);
}

/**
 * update_parent_pgds() - Updates the page table from bottom level towards
 *                        the top level to insert a new ATE
 *
 * @kbdev:    Device pointer.
 * @mmut:     GPU MMU page table.
 * @cur_level:    The level of MMU page table where the ATE needs to be added.
 *                The bottom PGD level.
 * @insert_level: The level of MMU page table where the chain of newly allocated
 *                PGDs needs to be linked-in/inserted.
 *                The top-most PDG level to be updated.
 * @insert_vpfn:  The virtual page frame number for the ATE.
 * @pgds_to_insert: Ptr to an array (size MIDGARD_MMU_BOTTOMLEVEL+1) that contains
 *                  the physical addresses of newly allocated PGDs from index
 *                  insert_level+1 to cur_level, and an existing PGD at index
 *                  insert_level.
 *
 * The newly allocated PGDs are linked from the bottom level up and inserted into the PGD
 * at insert_level which already exists in the MMU Page Tables.Migration status is also
 * updated for all the newly allocated PGD pages.
 *
 * Return:
 * * 0 - OK
 * * -EFAULT - level N+1 PGD does not exist
 * * -EINVAL - kmap() failed for level N PGD PFN
 */
static int update_parent_pgds(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
			      int cur_level, int insert_level, u64 insert_vpfn,
			      phys_addr_t *pgds_to_insert)
{
	int pgd_index;
	int err = 0;

	/* Add a PTE for the new PGD page at pgd_index into the parent PGD at (pgd_index-1)
	 * Loop runs from the bottom-most to the top-most level so that all entries in the chain
	 * are valid when they are inserted into the MMU Page table via the insert_level PGD.
	 */
	for (pgd_index = cur_level; pgd_index > insert_level; pgd_index--) {
		int parent_index = pgd_index - 1;
		phys_addr_t parent_pgd = pgds_to_insert[parent_index];
		unsigned int current_valid_entries;
		u64 pte;
		phys_addr_t target_pgd = pgds_to_insert[pgd_index];
		u64 parent_vpfn = (insert_vpfn >> ((3 - parent_index) * 9)) & 0x1FF;
		struct page *parent_page = pfn_to_page(PFN_DOWN(parent_pgd));
		u64 *parent_page_va;

		if (WARN_ON_ONCE(target_pgd == KBASE_MMU_INVALID_PGD_ADDRESS)) {
			err = -EFAULT;
			goto failure_recovery;
		}

		parent_page_va = kmap(parent_page);
		if (unlikely(parent_page_va == NULL)) {
			dev_err(kbdev->dev, "%s: kmap failure", __func__);
			err = -EINVAL;
			goto failure_recovery;
		}

		current_valid_entries = kbdev->mmu_mode->get_num_valid_entries(parent_page_va);

		kbdev->mmu_mode->entry_set_pte(&pte, target_pgd);
		parent_page_va[parent_vpfn] = kbdev->mgm_dev->ops.mgm_update_gpu_pte(
			kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, parent_index, pte);
		kbdev->mmu_mode->set_num_valid_entries(parent_page_va, current_valid_entries + 1);
		kunmap(parent_page);

		if (parent_index != insert_level) {
			/* Newly allocated PGDs */
			kbase_mmu_sync_pgd_cpu(
				kbdev, kbase_dma_addr(parent_page) + (parent_vpfn * sizeof(u64)),
				sizeof(u64));
		} else {
			/* A new valid entry is added to an existing PGD. Perform the
			 * invalidate operation for GPU cache as it could be having a
			 * cacheline that contains the entry (in an invalid form).
			 */
			kbase_mmu_sync_pgd(
				kbdev, mmut->kctx, parent_pgd + (parent_vpfn * sizeof(u64)),
				kbase_dma_addr(parent_page) + (parent_vpfn * sizeof(u64)),
				sizeof(u64), KBASE_MMU_OP_FLUSH_PT);
		}

		/* Update the new target_pgd page to its stable state */
		if (kbase_page_migration_enabled) {
			struct kbase_page_metadata *page_md =
				kbase_page_private(phys_to_page(target_pgd));

			spin_lock(&page_md->migrate_lock);

			WARN_ON_ONCE(PAGE_STATUS_GET(page_md->status) != ALLOCATE_IN_PROGRESS ||
				     IS_PAGE_ISOLATED(page_md->status));

			if (mmut->kctx) {
				page_md->status = PAGE_STATUS_SET(page_md->status, PT_MAPPED);
				page_md->data.pt_mapped.mmut = mmut;
				page_md->data.pt_mapped.pgd_vpfn_level =
					PGD_VPFN_LEVEL_SET(insert_vpfn, parent_index);
			} else {
				page_md->status = PAGE_STATUS_SET(page_md->status, NOT_MOVABLE);
			}

			spin_unlock(&page_md->migrate_lock);
		}
	}

	return 0;

failure_recovery:
	/* Cleanup PTEs from PGDs. The Parent PGD in the loop above is just "PGD" here */
	for (; pgd_index < cur_level; pgd_index++) {
		phys_addr_t pgd = pgds_to_insert[pgd_index];
		struct page *pgd_page = pfn_to_page(PFN_DOWN(pgd));
		u64 *pgd_page_va = kmap(pgd_page);
		u64 vpfn = (insert_vpfn >> ((3 - pgd_index) * 9)) & 0x1FF;

		kbdev->mmu_mode->entries_invalidate(&pgd_page_va[vpfn], 1);
		kunmap(pgd_page);
	}

	return err;
}

/**
 * mmu_insert_alloc_pgds() - allocate memory for PGDs from level_low to
 *                           level_high (inclusive)
 *
 * @kbdev:    Device pointer.
 * @mmut:     GPU MMU page table.
 * @level_low:  The lower bound for the levels for which the PGD allocs are required
 * @level_high: The higher bound for the levels for which the PGD allocs are required
 * @new_pgds:   Ptr to an array (size MIDGARD_MMU_BOTTOMLEVEL+1) to write the
 *              newly allocated PGD addresses to.
 *
 * Numerically, level_low < level_high, not to be confused with top level and
 * bottom level concepts for MMU PGDs. They are only used as low and high bounds
 * in an incrementing for-loop.
 *
 * Return:
 * * 0 - OK
 * * -ENOMEM - allocation failed for a PGD.
 */
static int mmu_insert_alloc_pgds(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				 phys_addr_t *new_pgds, int level_low, int level_high)
{
	int err = 0;
	int i;

	lockdep_assert_held(&mmut->mmu_lock);

	for (i = level_low; i <= level_high; i++) {
		do {
			new_pgds[i] = kbase_mmu_alloc_pgd(kbdev, mmut);
			if (new_pgds[i] != KBASE_MMU_INVALID_PGD_ADDRESS)
				break;

			mutex_unlock(&mmut->mmu_lock);
			err = kbase_mem_pool_grow(&kbdev->mem_pools.small[mmut->group_id],
						  level_high, NULL);
			mutex_lock(&mmut->mmu_lock);
			if (err) {
				dev_err(kbdev->dev, "%s: kbase_mem_pool_grow() returned error %d",
					__func__, err);

				/* Free all PGDs allocated in previous successful iterations
				 * from (i-1) to level_low
				 */
				for (i = (i - 1); i >= level_low; i--) {
					if (new_pgds[i] != KBASE_MMU_INVALID_PGD_ADDRESS)
						kbase_mmu_free_pgd(kbdev, mmut, new_pgds[i]);
				}

				return err;
			}
		} while (1);
	}

	return 0;
}

int kbase_mmu_insert_single_page(struct kbase_context *kctx, u64 start_vpfn,
				 struct tagged_addr phys, size_t nr, unsigned long flags,
				 int const group_id, enum kbase_caller_mmu_sync_info mmu_sync_info,
				 bool ignore_page_migration)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	u64 insert_vpfn = start_vpfn;
	size_t remain = nr;
	int err;
	struct kbase_device *kbdev;
	u64 dirty_pgds = 0;
	unsigned int i;
	phys_addr_t new_pgds[MIDGARD_MMU_BOTTOMLEVEL + 1];
	enum kbase_mmu_op_type flush_op;
	struct kbase_mmu_table *mmut = &kctx->mmu;
	int l, cur_level, insert_level;

	if (WARN_ON(kctx == NULL))
		return -EINVAL;

	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(start_vpfn <= (U64_MAX / PAGE_SIZE));

	kbdev = kctx->kbdev;

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	/* If page migration is enabled, pages involved in multiple GPU mappings
	 * are always treated as not movable.
	 */
	if (kbase_page_migration_enabled && !ignore_page_migration) {
		struct page *phys_page = as_page(phys);
		struct kbase_page_metadata *page_md = kbase_page_private(phys_page);

		if (page_md) {
			spin_lock(&page_md->migrate_lock);
			page_md->status = PAGE_STATUS_SET(page_md->status, (u8)NOT_MOVABLE);
			spin_unlock(&page_md->migrate_lock);
		}
	}

	mutex_lock(&mmut->mmu_lock);

	while (remain) {
		unsigned int vindex = insert_vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - vindex;
		struct page *p;
		register unsigned int num_of_valid_entries;
		bool newly_created_pgd = false;

		if (count > remain)
			count = remain;

		cur_level = MIDGARD_MMU_BOTTOMLEVEL;
		insert_level = cur_level;

		/*
		 * Repeatedly calling mmu_get_lowest_valid_pgd() is clearly
		 * suboptimal. We don't have to re-parse the whole tree
		 * each time (just cache the l0-l2 sequence).
		 * On the other hand, it's only a gain when we map more than
		 * 256 pages at once (on average). Do we really care?
		 */
		/* insert_level < cur_level if there's no valid PGD for cur_level and insert_vpn */
		err = mmu_get_lowest_valid_pgd(kbdev, mmut, insert_vpfn, cur_level, &insert_level,
					       &pgd);

		if (err) {
			dev_err(kbdev->dev, "%s: mmu_get_lowest_valid_pgd() returned error %d",
				__func__, err);
			goto fail_unlock;
		}

		/* No valid pgd at cur_level */
		if (insert_level != cur_level) {
			/* Allocate new pgds for all missing levels from the required level
			 * down to the lowest valid pgd at insert_level
			 */
			err = mmu_insert_alloc_pgds(kbdev, mmut, new_pgds, (insert_level + 1),
						    cur_level);
			if (err)
				goto fail_unlock;

			newly_created_pgd = true;

			new_pgds[insert_level] = pgd;

			/* If we didn't find an existing valid pgd at cur_level,
			 * we've now allocated one. The ATE in the next step should
			 * be inserted in this newly allocated pgd.
			 */
			pgd = new_pgds[cur_level];
		}

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap(p);
		if (!pgd_page) {
			dev_err(kbdev->dev, "%s: kmap failure", __func__);
			err = -ENOMEM;

			goto fail_unlock_free_pgds;
		}

		num_of_valid_entries =
			kbdev->mmu_mode->get_num_valid_entries(pgd_page);

		for (i = 0; i < count; i++) {
			unsigned int ofs = vindex + i;

			/* Fail if the current page is a valid ATE entry */
			KBASE_DEBUG_ASSERT(0 == (pgd_page[ofs] & 1UL));

			pgd_page[ofs] = kbase_mmu_create_ate(kbdev,
				phys, flags, MIDGARD_MMU_BOTTOMLEVEL, group_id);
		}

		kbdev->mmu_mode->set_num_valid_entries(
			pgd_page, num_of_valid_entries + count);

		dirty_pgds |= 1ULL << (newly_created_pgd ? insert_level : MIDGARD_MMU_BOTTOMLEVEL);

		/* MMU cache flush operation here will depend on whether bottom level
		 * PGD is newly created or not.
		 *
		 * If bottom level PGD is newly created then no GPU cache maintenance is
		 * required as the PGD will not exist in GPU cache. Otherwise GPU cache
		 * maintenance is required for existing PGD.
		 */
		flush_op = newly_created_pgd ? KBASE_MMU_OP_NONE : KBASE_MMU_OP_FLUSH_PT;

		kbase_mmu_sync_pgd(kbdev, kctx, pgd + (vindex * sizeof(u64)),
				   kbase_dma_addr(p) + (vindex * sizeof(u64)), count * sizeof(u64),
				   flush_op);

		if (newly_created_pgd) {
			err = update_parent_pgds(kbdev, mmut, cur_level, insert_level, insert_vpfn,
						 new_pgds);
			if (err) {
				dev_err(kbdev->dev, "%s: update_parent_pgds() failed (%d)",
					__func__, err);

				kbdev->mmu_mode->entries_invalidate(&pgd_page[vindex], count);

				kunmap(p);
				goto fail_unlock_free_pgds;
			}
		}

		insert_vpfn += count;
		remain -= count;
		kunmap(p);
	}

	mutex_unlock(&mmut->mmu_lock);

	mmu_flush_invalidate_insert_pages(kbdev, mmut, start_vpfn, nr, dirty_pgds, mmu_sync_info,
					  false);

	return 0;

fail_unlock_free_pgds:
	/* Free the pgds allocated by us from insert_level+1 to bottom level */
	for (l = cur_level; l > insert_level; l--)
		kbase_mmu_free_pgd(kbdev, mmut, new_pgds[l]);

fail_unlock:
	if (insert_vpfn != start_vpfn) {
		/* Invalidate the pages we have partially completed */
		mmu_insert_pages_failure_recovery(kbdev, mmut, start_vpfn, insert_vpfn, &dirty_pgds,
						  NULL, true);
	}

	mmu_flush_invalidate_insert_pages(kbdev, mmut, start_vpfn, nr, dirty_pgds, mmu_sync_info,
					  true);
	kbase_mmu_free_pgds_list(kbdev, mmut);
	mutex_unlock(&mmut->mmu_lock);

	return err;
}

int kbase_mmu_insert_single_imported_page(struct kbase_context *kctx, u64 vpfn,
					  struct tagged_addr phys, size_t nr, unsigned long flags,
					  int const group_id,
					  enum kbase_caller_mmu_sync_info mmu_sync_info)
{
	/* The aliasing sink page has metadata and shall be moved to NOT_MOVABLE. */
	return kbase_mmu_insert_single_page(kctx, vpfn, phys, nr, flags, group_id, mmu_sync_info,
					    false);
}

int kbase_mmu_insert_single_aliased_page(struct kbase_context *kctx, u64 vpfn,
					 struct tagged_addr phys, size_t nr, unsigned long flags,
					 int const group_id,
					 enum kbase_caller_mmu_sync_info mmu_sync_info)
{
	/* The aliasing sink page has metadata and shall be moved to NOT_MOVABLE. */
	return kbase_mmu_insert_single_page(kctx, vpfn, phys, nr, flags, group_id, mmu_sync_info,
					    false);
}

static void kbase_mmu_progress_migration_on_insert(struct tagged_addr phys,
						   struct kbase_va_region *reg,
						   struct kbase_mmu_table *mmut, const u64 vpfn)
{
	struct page *phys_page = as_page(phys);
	struct kbase_page_metadata *page_md = kbase_page_private(phys_page);

	spin_lock(&page_md->migrate_lock);

	/* If no GPU va region is given: the metadata provided are
	 * invalid.
	 *
	 * If the page is already allocated and mapped: this is
	 * an additional GPU mapping, probably to create a memory
	 * alias, which means it is no longer possible to migrate
	 * the page easily because tracking all the GPU mappings
	 * would be too costly.
	 *
	 * In any case: the page becomes not movable. It is kept
	 * alive, but attempts to migrate it will fail. The page
	 * will be freed if it is still not movable when it returns
	 * to a memory pool. Notice that the movable flag is not
	 * cleared because that would require taking the page lock.
	 */
	if (!reg || PAGE_STATUS_GET(page_md->status) == (u8)ALLOCATED_MAPPED) {
		page_md->status = PAGE_STATUS_SET(page_md->status, (u8)NOT_MOVABLE);
	} else if (PAGE_STATUS_GET(page_md->status) == (u8)ALLOCATE_IN_PROGRESS) {
		page_md->status = PAGE_STATUS_SET(page_md->status, (u8)ALLOCATED_MAPPED);
		page_md->data.mapped.reg = reg;
		page_md->data.mapped.mmut = mmut;
		page_md->data.mapped.vpfn = vpfn;
	}

	spin_unlock(&page_md->migrate_lock);
}

static void kbase_mmu_progress_migration_on_teardown(struct kbase_device *kbdev,
						     struct tagged_addr *phys, size_t requested_nr)
{
	size_t i;

	for (i = 0; i < requested_nr; i++) {
		struct page *phys_page = as_page(phys[i]);
		struct kbase_page_metadata *page_md = kbase_page_private(phys_page);

		/* Skip the 4KB page that is part of a large page, as the large page is
		 * excluded from the migration process.
		 */
		if (is_huge(phys[i]) || is_partial(phys[i]))
			continue;

		if (page_md) {
			u8 status;

			spin_lock(&page_md->migrate_lock);
			status = PAGE_STATUS_GET(page_md->status);

			if (status == ALLOCATED_MAPPED) {
				if (IS_PAGE_ISOLATED(page_md->status)) {
					page_md->status = PAGE_STATUS_SET(
						page_md->status, (u8)FREE_ISOLATED_IN_PROGRESS);
					page_md->data.free_isolated.kbdev = kbdev;
					/* At this point, we still have a reference
					 * to the page via its page migration metadata,
					 * and any page with the FREE_ISOLATED_IN_PROGRESS
					 * status will subsequently be freed in either
					 * kbase_page_migrate() or kbase_page_putback()
					 */
					phys[i] = as_tagged(0);
				} else
					page_md->status = PAGE_STATUS_SET(page_md->status,
									  (u8)FREE_IN_PROGRESS);
			}

			spin_unlock(&page_md->migrate_lock);
		}
	}
}

u64 kbase_mmu_create_ate(struct kbase_device *const kbdev,
	struct tagged_addr const phy, unsigned long const flags,
	int const level, int const group_id)
{
	u64 entry;

	kbdev->mmu_mode->entry_set_ate(&entry, phy, flags, level);
	return kbdev->mgm_dev->ops.mgm_update_gpu_pte(kbdev->mgm_dev,
		group_id, level, entry);
}

int kbase_mmu_insert_pages_no_flush(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				    const u64 start_vpfn, struct tagged_addr *phys, size_t nr,
				    unsigned long flags, int const group_id, u64 *dirty_pgds,
				    struct kbase_va_region *reg, bool ignore_page_migration)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	u64 insert_vpfn = start_vpfn;
	size_t remain = nr;
	int err;
	struct kbase_mmu_mode const *mmu_mode;
	unsigned int i;
	phys_addr_t new_pgds[MIDGARD_MMU_BOTTOMLEVEL + 1];
	int l, cur_level, insert_level;

	/* Note that 0 is a valid start_vpfn */
	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(start_vpfn <= (U64_MAX / PAGE_SIZE));

	mmu_mode = kbdev->mmu_mode;

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	mutex_lock(&mmut->mmu_lock);

	while (remain) {
		unsigned int vindex = insert_vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - vindex;
		struct page *p;
		register unsigned int num_of_valid_entries;
		bool newly_created_pgd = false;
		enum kbase_mmu_op_type flush_op;

		if (count > remain)
			count = remain;

		if (!vindex && is_huge_head(*phys))
			cur_level = MIDGARD_MMU_LEVEL(2);
		else
			cur_level = MIDGARD_MMU_BOTTOMLEVEL;

		insert_level = cur_level;

		/*
		 * Repeatedly calling mmu_get_lowest_valid_pgd() is clearly
		 * suboptimal. We don't have to re-parse the whole tree
		 * each time (just cache the l0-l2 sequence).
		 * On the other hand, it's only a gain when we map more than
		 * 256 pages at once (on average). Do we really care?
		 */
		/* insert_level < cur_level if there's no valid PGD for cur_level and insert_vpn */
		err = mmu_get_lowest_valid_pgd(kbdev, mmut, insert_vpfn, cur_level, &insert_level,
					       &pgd);

		if (err) {
			dev_err(kbdev->dev, "%s: mmu_get_lowest_valid_pgd() returned error %d",
				__func__, err);
			goto fail_unlock;
		}

		/* No valid pgd at cur_level */
		if (insert_level != cur_level) {
			/* Allocate new pgds for all missing levels from the required level
			 * down to the lowest valid pgd at insert_level
			 */
			err = mmu_insert_alloc_pgds(kbdev, mmut, new_pgds, (insert_level + 1),
						    cur_level);
			if (err)
				goto fail_unlock;

			newly_created_pgd = true;

			new_pgds[insert_level] = pgd;

			/* If we didn't find an existing valid pgd at cur_level,
			 * we've now allocated one. The ATE in the next step should
			 * be inserted in this newly allocated pgd.
			 */
			pgd = new_pgds[cur_level];
		}

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap(p);
		if (!pgd_page) {
			dev_err(kbdev->dev, "%s: kmap failure", __func__);
			err = -ENOMEM;

			goto fail_unlock_free_pgds;
		}

		num_of_valid_entries =
			mmu_mode->get_num_valid_entries(pgd_page);

		if (cur_level == MIDGARD_MMU_LEVEL(2)) {
			int level_index = (insert_vpfn >> 9) & 0x1FF;
			pgd_page[level_index] =
				kbase_mmu_create_ate(kbdev, *phys, flags, cur_level, group_id);

			num_of_valid_entries++;
		} else {
			for (i = 0; i < count; i++) {
				unsigned int ofs = vindex + i;
				u64 *target = &pgd_page[ofs];

				/* Warn if the current page is a valid ATE
				 * entry. The page table shouldn't have anything
				 * in the place where we are trying to put a
				 * new entry. Modification to page table entries
				 * should be performed with
				 * kbase_mmu_update_pages()
				 */
				WARN_ON((*target & 1UL) != 0);

				*target = kbase_mmu_create_ate(kbdev,
					phys[i], flags, cur_level, group_id);

				/* If page migration is enabled, this is the right time
				 * to update the status of the page.
				 */
				if (kbase_page_migration_enabled && !ignore_page_migration &&
				    !is_huge(phys[i]) && !is_partial(phys[i]))
					kbase_mmu_progress_migration_on_insert(phys[i], reg, mmut,
									       insert_vpfn + i);
			}
			num_of_valid_entries += count;
		}

		mmu_mode->set_num_valid_entries(pgd_page, num_of_valid_entries);

		if (dirty_pgds)
			*dirty_pgds |= 1ULL << (newly_created_pgd ? insert_level : cur_level);

		/* MMU cache flush operation here will depend on whether bottom level
		 * PGD is newly created or not.
		 *
		 * If bottom level PGD is newly created then no GPU cache maintenance is
		 * required as the PGD will not exist in GPU cache. Otherwise GPU cache
		 * maintenance is required for existing PGD.
		 */
		flush_op = newly_created_pgd ? KBASE_MMU_OP_NONE : KBASE_MMU_OP_FLUSH_PT;

		kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (vindex * sizeof(u64)),
				   kbase_dma_addr(p) + (vindex * sizeof(u64)), count * sizeof(u64),
				   flush_op);

		if (newly_created_pgd) {
			err = update_parent_pgds(kbdev, mmut, cur_level, insert_level, insert_vpfn,
						 new_pgds);
			if (err) {
				dev_err(kbdev->dev, "%s: update_parent_pgds() failed (%d)",
					__func__, err);

				kbdev->mmu_mode->entries_invalidate(&pgd_page[vindex], count);

				kunmap(p);
				goto fail_unlock_free_pgds;
			}
		}

		phys += count;
		insert_vpfn += count;
		remain -= count;
		kunmap(p);
	}

	mutex_unlock(&mmut->mmu_lock);

	return 0;

fail_unlock_free_pgds:
	/* Free the pgds allocated by us from insert_level+1 to bottom level */
	for (l = cur_level; l > insert_level; l--)
		kbase_mmu_free_pgd(kbdev, mmut, new_pgds[l]);

fail_unlock:
	if (insert_vpfn != start_vpfn) {
		/* Invalidate the pages we have partially completed */
		mmu_insert_pages_failure_recovery(kbdev, mmut, start_vpfn, insert_vpfn, dirty_pgds,
						  phys, ignore_page_migration);
	}

	mmu_flush_invalidate_insert_pages(kbdev, mmut, start_vpfn, nr,
					  dirty_pgds ? *dirty_pgds : 0xF, CALLER_MMU_ASYNC, true);
	kbase_mmu_free_pgds_list(kbdev, mmut);
	mutex_unlock(&mmut->mmu_lock);

	return err;
}

/*
 * Map 'nr' pages pointed to by 'phys' at GPU PFN 'vpfn' for GPU address space
 * number 'as_nr'.
 */
int kbase_mmu_insert_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, u64 vpfn,
			   struct tagged_addr *phys, size_t nr, unsigned long flags, int as_nr,
			   int const group_id, enum kbase_caller_mmu_sync_info mmu_sync_info,
			   struct kbase_va_region *reg, bool ignore_page_migration)
{
	int err;
	u64 dirty_pgds = 0;

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	err = kbase_mmu_insert_pages_no_flush(kbdev, mmut, vpfn, phys, nr, flags, group_id,
					      &dirty_pgds, reg, ignore_page_migration);
	if (err)
		return err;

	mmu_flush_invalidate_insert_pages(kbdev, mmut, vpfn, nr, dirty_pgds, mmu_sync_info, false);

	return 0;
}

KBASE_EXPORT_TEST_API(kbase_mmu_insert_pages);

int kbase_mmu_insert_imported_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				    u64 vpfn, struct tagged_addr *phys, size_t nr,
				    unsigned long flags, int as_nr, int const group_id,
				    enum kbase_caller_mmu_sync_info mmu_sync_info,
				    struct kbase_va_region *reg)
{
	int err;
	u64 dirty_pgds = 0;

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	/* Imported allocations don't have metadata and therefore always ignore the
	 * page migration logic.
	 */
	err = kbase_mmu_insert_pages_no_flush(kbdev, mmut, vpfn, phys, nr, flags, group_id,
					      &dirty_pgds, reg, true);
	if (err)
		return err;

	mmu_flush_invalidate_insert_pages(kbdev, mmut, vpfn, nr, dirty_pgds, mmu_sync_info, false);

	return 0;
}

int kbase_mmu_insert_aliased_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				   u64 vpfn, struct tagged_addr *phys, size_t nr,
				   unsigned long flags, int as_nr, int const group_id,
				   enum kbase_caller_mmu_sync_info mmu_sync_info,
				   struct kbase_va_region *reg)
{
	int err;
	u64 dirty_pgds = 0;

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	/* Memory aliases are always built on top of existing allocations,
	 * therefore the state of physical pages shall be updated.
	 */
	err = kbase_mmu_insert_pages_no_flush(kbdev, mmut, vpfn, phys, nr, flags, group_id,
					      &dirty_pgds, reg, false);
	if (err)
		return err;

	mmu_flush_invalidate_insert_pages(kbdev, mmut, vpfn, nr, dirty_pgds, mmu_sync_info, false);

	return 0;
}

void kbase_mmu_update(struct kbase_device *kbdev,
		struct kbase_mmu_table *mmut,
		int as_nr)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&kbdev->mmu_hw_mutex);
	KBASE_DEBUG_ASSERT(as_nr != KBASEP_AS_NR_INVALID);

	kbdev->mmu_mode->update(kbdev, mmut, as_nr);
}
KBASE_EXPORT_TEST_API(kbase_mmu_update);

void kbase_mmu_disable_as(struct kbase_device *kbdev, int as_nr)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&kbdev->mmu_hw_mutex);

	kbdev->mmu_mode->disable_as(kbdev, as_nr);
}

void kbase_mmu_disable(struct kbase_context *kctx)
{
	/* Calls to this function are inherently asynchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_mmu_hw_op_param op_param = { 0 };
	int lock_err, flush_err;

	/* ASSERT that the context has a valid as_nr, which is only the case
	 * when it's scheduled in.
	 *
	 * as_nr won't change because the caller has the hwaccess_lock
	 */
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);
	lockdep_assert_held(&kctx->kbdev->mmu_hw_mutex);

	op_param.vpfn = 0;
	op_param.nr = ~0;
	op_param.op = KBASE_MMU_OP_FLUSH_MEM;
	op_param.kctx_id = kctx->id;
	op_param.mmu_sync_info = mmu_sync_info;

#if MALI_USE_CSF
	/* 0xF value used to prevent skipping of any levels when flushing */
	if (mmu_flush_cache_on_gpu_ctrl(kbdev))
		op_param.flush_skip_levels = pgd_level_to_skip_flush(0xF);
#endif

	/* lock MMU to prevent existing jobs on GPU from executing while the AS is
	 * not yet disabled
	 */
	lock_err = kbase_mmu_hw_do_lock(kbdev, &kbdev->as[kctx->as_nr], &op_param);
	if (lock_err)
		dev_err(kbdev->dev, "Failed to lock AS %d for ctx %d_%d", kctx->as_nr, kctx->tgid,
			kctx->id);

	/* Issue the flush command only when L2 cache is in stable power on state.
	 * Any other state for L2 cache implies that shader cores are powered off,
	 * which in turn implies there is no execution happening on the GPU.
	 */
	if (kbdev->pm.backend.l2_state == KBASE_L2_ON) {
		flush_err = kbase_gpu_cache_flush_and_busy_wait(kbdev,
								GPU_COMMAND_CACHE_CLN_INV_L2_LSC);
		if (flush_err)
			dev_err(kbdev->dev,
				"Failed to flush GPU cache when disabling AS %d for ctx %d_%d",
				kctx->as_nr, kctx->tgid, kctx->id);
	}
	kbdev->mmu_mode->disable_as(kbdev, kctx->as_nr);

	if (!lock_err) {
		/* unlock the MMU to allow it to resume */
		lock_err =
			kbase_mmu_hw_do_unlock_no_addr(kbdev, &kbdev->as[kctx->as_nr], &op_param);
		if (lock_err)
			dev_err(kbdev->dev, "Failed to unlock AS %d for ctx %d_%d", kctx->as_nr,
				kctx->tgid, kctx->id);
	}

#if !MALI_USE_CSF
	/*
	 * JM GPUs has some L1 read only caches that need to be invalidated
	 * with START_FLUSH configuration. Purge the MMU disabled kctx from
	 * the slot_rb tracking field so such invalidation is performed when
	 * a new katom is executed on the affected slots.
	 */
	kbase_backend_slot_kctx_purge_locked(kbdev, kctx);
#endif
}
KBASE_EXPORT_TEST_API(kbase_mmu_disable);

static void kbase_mmu_update_and_free_parent_pgds(struct kbase_device *kbdev,
						  struct kbase_mmu_table *mmut, phys_addr_t *pgds,
						  u64 vpfn, int level,
						  enum kbase_mmu_op_type flush_op, u64 *dirty_pgds)
{
	int current_level;

	lockdep_assert_held(&mmut->mmu_lock);

	for (current_level = level - 1; current_level >= MIDGARD_MMU_LEVEL(0);
	     current_level--) {
		phys_addr_t current_pgd = pgds[current_level];
		struct page *p = phys_to_page(current_pgd);
		u64 *current_page = kmap(p);
		unsigned int current_valid_entries =
			kbdev->mmu_mode->get_num_valid_entries(current_page);
		int index = (vpfn >> ((3 - current_level) * 9)) & 0x1FF;

		/* We need to track every level that needs updating */
		if (dirty_pgds)
			*dirty_pgds |= 1ULL << current_level;

		kbdev->mmu_mode->entries_invalidate(&current_page[index], 1);
		if (current_valid_entries == 1 &&
		    current_level != MIDGARD_MMU_LEVEL(0)) {
			kunmap(p);

			/* Ensure the cacheline containing the last valid entry
			 * of PGD is invalidated from the GPU cache, before the
			 * PGD page is freed.
			 */
			kbase_mmu_sync_pgd_gpu(kbdev, mmut->kctx,
				current_pgd + (index * sizeof(u64)),
				sizeof(u64), flush_op);

			kbase_mmu_add_to_free_pgds_list(mmut, p);
		} else {
			current_valid_entries--;

			kbdev->mmu_mode->set_num_valid_entries(
				current_page, current_valid_entries);

			kunmap(p);

			kbase_mmu_sync_pgd(kbdev, mmut->kctx, current_pgd + (index * sizeof(u64)),
					   kbase_dma_addr(p) + (index * sizeof(u64)), sizeof(u64),
					   flush_op);
			break;
		}
	}
}

/**
 * mmu_flush_invalidate_teardown_pages() - Perform flush operation after unmapping pages.
 *
 * @kbdev:         Pointer to kbase device.
 * @kctx:          Pointer to kbase context.
 * @as_nr:         Address space number, for GPU cache maintenance operations
 *                 that happen outside a specific kbase context.
 * @phys:          Array of physical pages to flush.
 * @phys_page_nr:  Number of physical pages to flush.
 * @op_param:      Non-NULL pointer to struct containing information about the flush
 *                 operation to perform.
 *
 * This function will do one of three things:
 * 1. Invalidate the MMU caches, followed by a partial GPU cache flush of the
 *    individual pages that were unmapped if feature is supported on GPU.
 * 2. Perform a full GPU cache flush through the GPU_CONTROL interface if feature is
 *    supported on GPU or,
 * 3. Perform a full GPU cache flush through the MMU_CONTROL interface.
 *
 * When performing a partial GPU cache flush, the number of physical
 * pages does not have to be identical to the number of virtual pages on the MMU,
 * to support a single physical address flush for an aliased page.
 */
static void mmu_flush_invalidate_teardown_pages(struct kbase_device *kbdev,
						struct kbase_context *kctx, int as_nr,
						struct tagged_addr *phys, size_t phys_page_nr,
						struct kbase_mmu_hw_op_param *op_param)
{
	if (!mmu_flush_cache_on_gpu_ctrl(kbdev)) {
		/* Full cache flush through the MMU_COMMAND */
		mmu_flush_invalidate(kbdev, kctx, as_nr, op_param);
	} else if (op_param->op == KBASE_MMU_OP_FLUSH_MEM) {
		/* Full cache flush through the GPU_CONTROL */
		mmu_flush_invalidate_on_gpu_ctrl(kbdev, kctx, as_nr, op_param);
	}
#if MALI_USE_CSF
	else {
		/* Partial GPU cache flush with MMU cache invalidation */
		unsigned long irq_flags;
		unsigned int i;
		bool flush_done = false;

		mmu_invalidate(kbdev, kctx, as_nr, op_param);

		for (i = 0; !flush_done && i < phys_page_nr; i++) {
			spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
			if (kbdev->pm.backend.gpu_powered && (!kctx || kctx->as_nr >= 0))
				mmu_flush_pa_range(kbdev, as_phys_addr_t(phys[i]), PAGE_SIZE,
						   KBASE_MMU_OP_FLUSH_MEM);
			else
				flush_done = true;
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);
		}
	}
#endif
}

static int kbase_mmu_teardown_pgd_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
					u64 vpfn, size_t nr, u64 *dirty_pgds,
					struct list_head *free_pgds_list,
					enum kbase_mmu_op_type flush_op)
{
	struct kbase_mmu_mode const *mmu_mode = kbdev->mmu_mode;

	lockdep_assert_held(&mmut->mmu_lock);
	kbase_mmu_reset_free_pgds_list(mmut);

	while (nr) {
		unsigned int index = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - index;
		unsigned int pcount;
		int level;
		u64 *page;
		phys_addr_t pgds[MIDGARD_MMU_BOTTOMLEVEL + 1];
		register unsigned int num_of_valid_entries;
		phys_addr_t pgd = mmut->pgd;
		struct page *p = phys_to_page(pgd);

		if (count > nr)
			count = nr;

		/* need to check if this is a 2MB page or a 4kB */
		for (level = MIDGARD_MMU_TOPLEVEL;
				level <= MIDGARD_MMU_BOTTOMLEVEL; level++) {
			phys_addr_t next_pgd;

			index = (vpfn >> ((3 - level) * 9)) & 0x1FF;
			page = kmap(p);
			if (mmu_mode->ate_is_valid(page[index], level))
				break; /* keep the mapping */
			else if (!mmu_mode->pte_is_valid(page[index], level)) {
				/* nothing here, advance */
				switch (level) {
				case MIDGARD_MMU_LEVEL(0):
					count = 134217728;
					break;
				case MIDGARD_MMU_LEVEL(1):
					count = 262144;
					break;
				case MIDGARD_MMU_LEVEL(2):
					count = 512;
					break;
				case MIDGARD_MMU_LEVEL(3):
					count = 1;
					break;
				}
				if (count > nr)
					count = nr;
				goto next;
			}
			next_pgd = mmu_mode->pte_to_phy_addr(
				kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
					kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, page[index]));
			kunmap(p);
			pgds[level] = pgd;
			pgd = next_pgd;
			p = phys_to_page(pgd);
		}

		switch (level) {
		case MIDGARD_MMU_LEVEL(0):
		case MIDGARD_MMU_LEVEL(1):
			dev_warn(kbdev->dev, "%s: No support for ATEs at level %d", __func__,
				 level);
			kunmap(p);
			goto out;
		case MIDGARD_MMU_LEVEL(2):
			/* can only teardown if count >= 512 */
			if (count >= 512) {
				pcount = 1;
			} else {
				dev_warn(
					kbdev->dev,
					"%s: limiting teardown as it tries to do a partial 2MB teardown, need 512, but have %d to tear down",
					__func__, count);
				pcount = 0;
			}
			break;
		case MIDGARD_MMU_BOTTOMLEVEL:
			/* page count is the same as the logical count */
			pcount = count;
			break;
		default:
			dev_err(kbdev->dev, "%s: found non-mapped memory, early out", __func__);
			vpfn += count;
			nr -= count;
			continue;
		}

		if (pcount > 0)
			*dirty_pgds |= 1ULL << level;

		num_of_valid_entries = mmu_mode->get_num_valid_entries(page);
		if (WARN_ON_ONCE(num_of_valid_entries < pcount))
			num_of_valid_entries = 0;
		else
			num_of_valid_entries -= pcount;

		/* Invalidate the entries we added */
		mmu_mode->entries_invalidate(&page[index], pcount);

		if (!num_of_valid_entries) {
			kunmap(p);

			/* Ensure the cacheline(s) containing the last valid entries
			 * of PGD is invalidated from the GPU cache, before the
			 * PGD page is freed.
			 */
			kbase_mmu_sync_pgd_gpu(kbdev, mmut->kctx,
				pgd + (index * sizeof(u64)),
				pcount * sizeof(u64), flush_op);

			kbase_mmu_add_to_free_pgds_list(mmut, p);

			kbase_mmu_update_and_free_parent_pgds(kbdev, mmut, pgds, vpfn, level,
							      flush_op, dirty_pgds);

			vpfn += count;
			nr -= count;
			continue;
		}

		mmu_mode->set_num_valid_entries(page, num_of_valid_entries);

		kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (index * sizeof(u64)),
				   kbase_dma_addr(p) + (index * sizeof(u64)), pcount * sizeof(u64),
				   flush_op);
next:
		kunmap(p);
		vpfn += count;
		nr -= count;
	}
out:
	return 0;
}

int kbase_mmu_teardown_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, u64 vpfn,
			     struct tagged_addr *phys, size_t nr_phys_pages, size_t nr_virt_pages,
			     int as_nr, bool ignore_page_migration)
{
	u64 start_vpfn = vpfn;
	enum kbase_mmu_op_type flush_op = KBASE_MMU_OP_NONE;
	struct kbase_mmu_hw_op_param op_param;
	int err = -EFAULT;
	u64 dirty_pgds = 0;
	LIST_HEAD(free_pgds_list);

	/* Calls to this function are inherently asynchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	/* This function performs two operations: MMU maintenance and flushing
	 * the caches. To ensure internal consistency between the caches and the
	 * MMU, it does not make sense to be able to flush only the physical pages
	 * from the cache and keep the PTE, nor does it make sense to use this
	 * function to remove a PTE and keep the physical pages in the cache.
	 *
	 * However, we have legitimate cases where we can try to tear down a mapping
	 * with zero virtual and zero physical pages, so we must have the following
	 * behaviour:
	 *  - if both physical and virtual page counts are zero, return early
	 *  - if either physical and virtual page counts are zero, return early
	 *  - if there are fewer physical pages than virtual pages, return -EINVAL
	 */
	if (unlikely(nr_virt_pages == 0 || nr_phys_pages == 0))
		return 0;

	if (unlikely(nr_virt_pages < nr_phys_pages))
		return -EINVAL;

	/* MMU cache flush strategy depends on the number of pages to unmap. In both cases
	 * the operation is invalidate but the granularity of cache maintenance may change
	 * according to the situation.
	 *
	 * If GPU control command operations are present and the number of pages is "small",
	 * then the optimal strategy is flushing on the physical address range of the pages
	 * which are affected by the operation. That implies both the PGDs which are modified
	 * or removed from the page table and the physical pages which are freed from memory.
	 *
	 * Otherwise, there's no alternative to invalidating the whole GPU cache.
	 */
	if (mmu_flush_cache_on_gpu_ctrl(kbdev) && phys &&
	    nr_phys_pages <= KBASE_PA_RANGE_THRESHOLD_NR_PAGES)
		flush_op = KBASE_MMU_OP_FLUSH_PT;

	mutex_lock(&mmut->mmu_lock);

	err = kbase_mmu_teardown_pgd_pages(kbdev, mmut, vpfn, nr_virt_pages, &dirty_pgds,
					   &free_pgds_list, flush_op);

	/* Set up MMU operation parameters. See above about MMU cache flush strategy. */
	op_param = (struct kbase_mmu_hw_op_param){
		.vpfn = start_vpfn,
		.nr = nr_virt_pages,
		.mmu_sync_info = mmu_sync_info,
		.kctx_id = mmut->kctx ? mmut->kctx->id : 0xFFFFFFFF,
		.op = (flush_op == KBASE_MMU_OP_FLUSH_PT) ? KBASE_MMU_OP_FLUSH_PT :
							    KBASE_MMU_OP_FLUSH_MEM,
		.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds),
	};
	mmu_flush_invalidate_teardown_pages(kbdev, mmut->kctx, as_nr, phys, nr_phys_pages,
					    &op_param);

	/* If page migration is enabled: the status of all physical pages involved
	 * shall be updated, unless they are not movable. Their status shall be
	 * updated before releasing the lock to protect against concurrent
	 * requests to migrate the pages, if they have been isolated.
	 */
	if (kbase_page_migration_enabled && phys && !ignore_page_migration)
		kbase_mmu_progress_migration_on_teardown(kbdev, phys, nr_phys_pages);

	kbase_mmu_free_pgds_list(kbdev, mmut);

	mutex_unlock(&mmut->mmu_lock);

	return err;
}
KBASE_EXPORT_TEST_API(kbase_mmu_teardown_pages);

/**
 * kbase_mmu_update_pages_no_flush() - Update phy pages and attributes data in GPU
 *                                     page table entries
 *
 * @kbdev: Pointer to kbase device.
 * @mmut:  The involved MMU table
 * @vpfn:  Virtual PFN (Page Frame Number) of the first page to update
 * @phys:  Pointer to the array of tagged physical addresses of the physical
 *         pages that are pointed to by the page table entries (that need to
 *         be updated). The pointer should be within the reg->gpu_alloc->pages
 *         array.
 * @nr:    Number of pages to update
 * @flags: Flags
 * @group_id: The physical memory group in which the page was allocated.
 *            Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 * @dirty_pgds: Flags to track every level where a PGD has been updated.
 *
 * This will update page table entries that already exist on the GPU based on
 * new flags and replace any existing phy pages that are passed (the PGD pages
 * remain unchanged). It is used as a response to the changes of phys as well
 * as the the memory attributes.
 *
 * The caller is responsible for validating the memory attributes.
 *
 * Return: 0 if the attributes data in page table entries were updated
 *         successfully, otherwise an error code.
 */
static int kbase_mmu_update_pages_no_flush(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
					   u64 vpfn, struct tagged_addr *phys, size_t nr,
					   unsigned long flags, int const group_id, u64 *dirty_pgds)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	int err;

	KBASE_DEBUG_ASSERT(vpfn <= (U64_MAX / PAGE_SIZE));

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	mutex_lock(&mmut->mmu_lock);

	while (nr) {
		unsigned int i;
		unsigned int index = vpfn & 0x1FF;
		size_t count = KBASE_MMU_PAGE_ENTRIES - index;
		struct page *p;
		register unsigned int num_of_valid_entries;
		int cur_level = MIDGARD_MMU_BOTTOMLEVEL;

		if (count > nr)
			count = nr;

		if (is_huge(*phys) && (index == index_in_large_page(*phys)))
			cur_level = MIDGARD_MMU_LEVEL(2);

		err = mmu_get_pgd_at_level(kbdev, mmut, vpfn, cur_level, &pgd);
		if (WARN_ON(err))
			goto fail_unlock;

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap(p);
		if (!pgd_page) {
			dev_warn(kbdev->dev, "kmap failure on update_pages");
			err = -ENOMEM;
			goto fail_unlock;
		}

		num_of_valid_entries =
			kbdev->mmu_mode->get_num_valid_entries(pgd_page);

		if (cur_level == MIDGARD_MMU_LEVEL(2)) {
			int level_index = (vpfn >> 9) & 0x1FF;
			struct tagged_addr *target_phys =
				phys - index_in_large_page(*phys);

#ifdef CONFIG_MALI_BIFROST_DEBUG
			WARN_ON_ONCE(!kbdev->mmu_mode->ate_is_valid(
					pgd_page[level_index], MIDGARD_MMU_LEVEL(2)));
#endif
			pgd_page[level_index] = kbase_mmu_create_ate(kbdev,
					*target_phys, flags, MIDGARD_MMU_LEVEL(2),
					group_id);
			kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (level_index * sizeof(u64)),
					   kbase_dma_addr(p) + (level_index * sizeof(u64)),
					   sizeof(u64), KBASE_MMU_OP_NONE);
		} else {
			for (i = 0; i < count; i++) {
#ifdef CONFIG_MALI_BIFROST_DEBUG
				WARN_ON_ONCE(!kbdev->mmu_mode->ate_is_valid(
						pgd_page[index + i],
						MIDGARD_MMU_BOTTOMLEVEL));
#endif
				pgd_page[index + i] = kbase_mmu_create_ate(kbdev,
					phys[i], flags, MIDGARD_MMU_BOTTOMLEVEL,
					group_id);
			}

			/* MMU cache flush strategy is NONE because GPU cache maintenance
			 * will be done by the caller.
			 */
			kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (index * sizeof(u64)),
					   kbase_dma_addr(p) + (index * sizeof(u64)),
					   count * sizeof(u64), KBASE_MMU_OP_NONE);
		}

		kbdev->mmu_mode->set_num_valid_entries(pgd_page,
					num_of_valid_entries);

		if (dirty_pgds && count > 0)
			*dirty_pgds |= 1ULL << cur_level;

		phys += count;
		vpfn += count;
		nr -= count;

		kunmap(p);
	}

	mutex_unlock(&mmut->mmu_lock);
	return 0;

fail_unlock:
	mutex_unlock(&mmut->mmu_lock);
	return err;
}

static int kbase_mmu_update_pages_common(struct kbase_device *kbdev, struct kbase_context *kctx,
					 u64 vpfn, struct tagged_addr *phys, size_t nr,
					 unsigned long flags, int const group_id)
{
	int err;
	struct kbase_mmu_hw_op_param op_param;
	u64 dirty_pgds = 0;
	struct kbase_mmu_table *mmut;
	/* Calls to this function are inherently asynchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;
	int as_nr;

#if !MALI_USE_CSF
	if (unlikely(kctx == NULL))
		return -EINVAL;

	as_nr = kctx->as_nr;
	mmut = &kctx->mmu;
#else
	if (kctx) {
		mmut = &kctx->mmu;
		as_nr = kctx->as_nr;
	} else {
		mmut = &kbdev->csf.mcu_mmu;
		as_nr = MCU_AS_NR;
	}
#endif

	err = kbase_mmu_update_pages_no_flush(kbdev, mmut, vpfn, phys, nr, flags, group_id,
					      &dirty_pgds);

	op_param = (const struct kbase_mmu_hw_op_param){
		.vpfn = vpfn,
		.nr = nr,
		.op = KBASE_MMU_OP_FLUSH_MEM,
		.kctx_id = kctx ? kctx->id : 0xFFFFFFFF,
		.mmu_sync_info = mmu_sync_info,
		.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds),
	};

	if (mmu_flush_cache_on_gpu_ctrl(kbdev))
		mmu_flush_invalidate_on_gpu_ctrl(kbdev, kctx, as_nr, &op_param);
	else
		mmu_flush_invalidate(kbdev, kctx, as_nr, &op_param);

	return err;
}

int kbase_mmu_update_pages(struct kbase_context *kctx, u64 vpfn, struct tagged_addr *phys,
			   size_t nr, unsigned long flags, int const group_id)
{
	if (unlikely(kctx == NULL))
		return -EINVAL;

	return kbase_mmu_update_pages_common(kctx->kbdev, kctx, vpfn, phys, nr, flags, group_id);
}

#if MALI_USE_CSF
int kbase_mmu_update_csf_mcu_pages(struct kbase_device *kbdev, u64 vpfn, struct tagged_addr *phys,
				   size_t nr, unsigned long flags, int const group_id)
{
	return kbase_mmu_update_pages_common(kbdev, NULL, vpfn, phys, nr, flags, group_id);
}
#endif /* MALI_USE_CSF */

static void mmu_page_migration_transaction_begin(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ON_ONCE(kbdev->mmu_page_migrate_in_progress);
	kbdev->mmu_page_migrate_in_progress = true;
}

static void mmu_page_migration_transaction_end(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
	WARN_ON_ONCE(!kbdev->mmu_page_migrate_in_progress);
	kbdev->mmu_page_migrate_in_progress = false;
	/* Invoke the PM state machine, as the MMU page migration session
	 * may have deferred a transition in L2 state machine.
	 */
	kbase_pm_update_state(kbdev);
}

int kbase_mmu_migrate_page(struct tagged_addr old_phys, struct tagged_addr new_phys,
			   dma_addr_t old_dma_addr, dma_addr_t new_dma_addr, int level)
{
	struct kbase_page_metadata *page_md = kbase_page_private(as_page(old_phys));
	struct kbase_mmu_hw_op_param op_param;
	struct kbase_mmu_table *mmut = (level == MIDGARD_MMU_BOTTOMLEVEL) ?
					       page_md->data.mapped.mmut :
					       page_md->data.pt_mapped.mmut;
	struct kbase_device *kbdev;
	phys_addr_t pgd;
	u64 *old_page, *new_page, *pgd_page, *target, vpfn;
	int index, check_state, ret = 0;
	unsigned long hwaccess_flags = 0;
	unsigned int num_of_valid_entries;
	u8 vmap_count = 0;

	/* Due to the hard binding of mmu_command_instr with kctx_id via kbase_mmu_hw_op_param,
	 * here we skip the no kctx case, which is only used with MCU's mmut.
	 */
	if (!mmut->kctx)
		return -EINVAL;

	if (level > MIDGARD_MMU_BOTTOMLEVEL)
		return -EINVAL;
	else if (level == MIDGARD_MMU_BOTTOMLEVEL)
		vpfn = page_md->data.mapped.vpfn;
	else
		vpfn = PGD_VPFN_LEVEL_GET_VPFN(page_md->data.pt_mapped.pgd_vpfn_level);

	kbdev = mmut->kctx->kbdev;
	index = (vpfn >> ((3 - level) * 9)) & 0x1FF;

	/* Create all mappings before copying content.
	 * This is done as early as possible because is the only operation that may
	 * fail. It is possible to do this before taking any locks because the
	 * pages to migrate are not going to change and even the parent PGD is not
	 * going to be affected by any other concurrent operation, since the page
	 * has been isolated before migration and therefore it cannot disappear in
	 * the middle of this function.
	 */
	old_page = kmap(as_page(old_phys));
	if (!old_page) {
		dev_warn(kbdev->dev, "%s: kmap failure for old page.", __func__);
		ret = -EINVAL;
		goto old_page_map_error;
	}

	new_page = kmap(as_page(new_phys));
	if (!new_page) {
		dev_warn(kbdev->dev, "%s: kmap failure for new page.", __func__);
		ret = -EINVAL;
		goto new_page_map_error;
	}

	/* GPU cache maintenance affects both memory content and page table,
	 * but at two different stages. A single virtual memory page is affected
	 * by the migration.
	 *
	 * Notice that the MMU maintenance is done in the following steps:
	 *
	 * 1) The MMU region is locked without performing any other operation.
	 *    This lock must cover the entire migration process, in order to
	 *    prevent any GPU access to the virtual page whose physical page
	 *    is being migrated.
	 * 2) Immediately after locking: the MMU region content is flushed via
	 *    GPU control while the lock is taken and without unlocking.
	 *    The region must stay locked for the duration of the whole page
	 *    migration procedure.
	 *    This is necessary to make sure that pending writes to the old page
	 *    are finalized before copying content to the new page.
	 * 3) Before unlocking: changes to the page table are flushed.
	 *    Finer-grained GPU control operations are used if possible, otherwise
	 *    the whole GPU cache shall be flushed again.
	 *    This is necessary to make sure that the GPU accesses the new page
	 *    after migration.
	 * 4) The MMU region is unlocked.
	 */
#define PGD_VPFN_MASK(level) (~((((u64)1) << ((3 - level) * 9)) - 1))
	op_param.mmu_sync_info = CALLER_MMU_ASYNC;
	op_param.kctx_id = mmut->kctx->id;
	op_param.vpfn = vpfn & PGD_VPFN_MASK(level);
	op_param.nr = 1 << ((3 - level) * 9);
	op_param.op = KBASE_MMU_OP_FLUSH_PT;
	/* When level is not MIDGARD_MMU_BOTTOMLEVEL, it is assumed PGD page migration */
	op_param.flush_skip_levels = (level == MIDGARD_MMU_BOTTOMLEVEL) ?
					     pgd_level_to_skip_flush(1ULL << level) :
					     pgd_level_to_skip_flush(3ULL << level);

	mutex_lock(&mmut->mmu_lock);

	/* The state was evaluated before entering this function, but it could
	 * have changed before the mmu_lock was taken. However, the state
	 * transitions which are possible at this point are only two, and in both
	 * cases it is a stable state progressing to a "free in progress" state.
	 *
	 * After taking the mmu_lock the state can no longer change: read it again
	 * and make sure that it hasn't changed before continuing.
	 */
	spin_lock(&page_md->migrate_lock);
	check_state = PAGE_STATUS_GET(page_md->status);
	if (level == MIDGARD_MMU_BOTTOMLEVEL)
		vmap_count = page_md->vmap_count;
	spin_unlock(&page_md->migrate_lock);

	if (level == MIDGARD_MMU_BOTTOMLEVEL) {
		if (check_state != ALLOCATED_MAPPED) {
			dev_dbg(kbdev->dev,
				"%s: state changed to %d (was %d), abort page migration", __func__,
				check_state, ALLOCATED_MAPPED);
			ret = -EAGAIN;
			goto page_state_change_out;
		} else if (vmap_count > 0) {
			dev_dbg(kbdev->dev, "%s: page was multi-mapped, abort page migration",
				__func__);
			ret = -EAGAIN;
			goto page_state_change_out;
		}
	} else {
		if (check_state != PT_MAPPED) {
			dev_dbg(kbdev->dev,
				"%s: state changed to %d (was %d), abort PGD page migration",
				__func__, check_state, PT_MAPPED);
			WARN_ON_ONCE(check_state != FREE_PT_ISOLATED_IN_PROGRESS);
			ret = -EAGAIN;
			goto page_state_change_out;
		}
	}

	ret = mmu_get_pgd_at_level(kbdev, mmut, vpfn, level, &pgd);
	if (ret) {
		dev_err(kbdev->dev, "%s: failed to find PGD for old page.", __func__);
		goto get_pgd_at_level_error;
	}

	pgd_page = kmap(phys_to_page(pgd));
	if (!pgd_page) {
		dev_warn(kbdev->dev, "%s: kmap failure for PGD page.", __func__);
		ret = -EINVAL;
		goto pgd_page_map_error;
	}

	mutex_lock(&kbdev->pm.lock);
	mutex_lock(&kbdev->mmu_hw_mutex);

	/* Lock MMU region and flush GPU cache by using GPU control,
	 * in order to keep MMU region locked.
	 */
	spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_flags);
	if (unlikely(!kbase_pm_l2_allow_mmu_page_migration(kbdev))) {
		/* Defer the migration as L2 is in a transitional phase */
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);
		mutex_unlock(&kbdev->mmu_hw_mutex);
		mutex_unlock(&kbdev->pm.lock);
		dev_dbg(kbdev->dev, "%s: L2 in transtion, abort PGD page migration", __func__);
		ret = -EAGAIN;
		goto l2_state_defer_out;
	}
	/* Prevent transitional phases in L2 by starting the transaction */
	mmu_page_migration_transaction_begin(kbdev);
	if (kbdev->pm.backend.gpu_powered && mmut->kctx->as_nr >= 0) {
		int as_nr = mmut->kctx->as_nr;
		struct kbase_as *as = &kbdev->as[as_nr];

		ret = kbase_mmu_hw_do_lock(kbdev, as, &op_param);
		if (!ret) {
				ret = kbase_gpu_cache_flush_and_busy_wait(
					kbdev, GPU_COMMAND_CACHE_CLN_INV_L2_LSC);
		}
		if (ret)
			mmu_page_migration_transaction_end(kbdev);
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);

	if (ret < 0) {
		mutex_unlock(&kbdev->mmu_hw_mutex);
		mutex_unlock(&kbdev->pm.lock);
		dev_err(kbdev->dev, "%s: failed to lock MMU region or flush GPU cache", __func__);
		goto undo_mappings;
	}

	/* Copy memory content.
	 *
	 * It is necessary to claim the ownership of the DMA buffer for the old
	 * page before performing the copy, to make sure of reading a consistent
	 * version of its content, before copying. After the copy, ownership of
	 * the DMA buffer for the new page is given to the GPU in order to make
	 * the content visible to potential GPU access that may happen as soon as
	 * this function releases the lock on the MMU region.
	 */
	dma_sync_single_for_cpu(kbdev->dev, old_dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
	memcpy(new_page, old_page, PAGE_SIZE);
	dma_sync_single_for_device(kbdev->dev, new_dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

	/* Remap GPU virtual page.
	 *
	 * This code rests on the assumption that page migration is only enabled
	 * for 4 kB pages, that necessarily live in the bottom level of the MMU
	 * page table. For this reason, the PGD level tells us inequivocably
	 * whether the page being migrated is a "content page" or another PGD
	 * of the page table:
	 *
	 * - Bottom level implies ATE (Address Translation Entry)
	 * - Any other level implies PTE (Page Table Entry)
	 *
	 * The current implementation doesn't handle the case of a level 0 PGD,
	 * that is: the root PGD of the page table.
	 */
	target = &pgd_page[index];

	/* Certain entries of a page table page encode the count of valid entries
	 * present in that page. So need to save & restore the count information
	 * when updating the PTE/ATE to point to the new page.
	 */
	num_of_valid_entries = kbdev->mmu_mode->get_num_valid_entries(pgd_page);

	if (level == MIDGARD_MMU_BOTTOMLEVEL) {
		WARN_ON_ONCE((*target & 1UL) == 0);
		*target =
			kbase_mmu_create_ate(kbdev, new_phys, page_md->data.mapped.reg->flags,
					     level, page_md->data.mapped.reg->gpu_alloc->group_id);
	} else {
		u64 managed_pte;

#ifdef CONFIG_MALI_BIFROST_DEBUG
		/* The PTE should be pointing to the page being migrated */
		WARN_ON_ONCE(as_phys_addr_t(old_phys) != kbdev->mmu_mode->pte_to_phy_addr(
			kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
				kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, pgd_page[index])));
#endif
		kbdev->mmu_mode->entry_set_pte(&managed_pte, as_phys_addr_t(new_phys));
		*target = kbdev->mgm_dev->ops.mgm_update_gpu_pte(
			kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, managed_pte);
	}

	kbdev->mmu_mode->set_num_valid_entries(pgd_page, num_of_valid_entries);

	/* This function always updates a single entry inside an existing PGD,
	 * therefore cache maintenance is necessary and affects a single entry.
	 */
	kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (index * sizeof(u64)),
			   kbase_dma_addr(phys_to_page(pgd)) + (index * sizeof(u64)), sizeof(u64),
			   KBASE_MMU_OP_FLUSH_PT);

	/* Unlock MMU region.
	 *
	 * Notice that GPUs which don't issue flush commands via GPU control
	 * still need an additional GPU cache flush here, this time only
	 * for the page table, because the function call above to sync PGDs
	 * won't have any effect on them.
	 */
	spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_flags);
	if (kbdev->pm.backend.gpu_powered && mmut->kctx->as_nr >= 0) {
		int as_nr = mmut->kctx->as_nr;
		struct kbase_as *as = &kbdev->as[as_nr];

		if (mmu_flush_cache_on_gpu_ctrl(kbdev)) {
			ret = kbase_mmu_hw_do_unlock(kbdev, as, &op_param);
		} else {
			ret = kbase_gpu_cache_flush_and_busy_wait(kbdev,
								  GPU_COMMAND_CACHE_CLN_INV_L2);
			if (!ret)
				ret = kbase_mmu_hw_do_unlock_no_addr(kbdev, as, &op_param);
		}
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);
	/* Releasing locks before checking the migration transaction error state */
	mutex_unlock(&kbdev->mmu_hw_mutex);
	mutex_unlock(&kbdev->pm.lock);

	spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_flags);
	/* Release the transition prevention in L2 by ending the transaction */
	mmu_page_migration_transaction_end(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);

	/* Checking the final migration transaction error state */
	if (ret < 0) {
		dev_err(kbdev->dev, "%s: failed to unlock MMU region.", __func__);
		goto undo_mappings;
	}

	/* Undertaking metadata transfer, while we are holding the mmu_lock */
	spin_lock(&page_md->migrate_lock);
	if (level == MIDGARD_MMU_BOTTOMLEVEL) {
		size_t page_array_index =
			page_md->data.mapped.vpfn - page_md->data.mapped.reg->start_pfn;

		WARN_ON(PAGE_STATUS_GET(page_md->status) != ALLOCATED_MAPPED);

		/* Replace page in array of pages of the physical allocation. */
		page_md->data.mapped.reg->gpu_alloc->pages[page_array_index] = new_phys;
	}
	/* Update the new page dma_addr with the transferred metadata from the old_page */
	page_md->dma_addr = new_dma_addr;
	page_md->status = PAGE_ISOLATE_SET(page_md->status, 0);
	spin_unlock(&page_md->migrate_lock);
	set_page_private(as_page(new_phys), (unsigned long)page_md);
	/* Old page metatdata pointer cleared as it now owned by the new page */
	set_page_private(as_page(old_phys), 0);

l2_state_defer_out:
	kunmap(phys_to_page(pgd));
pgd_page_map_error:
get_pgd_at_level_error:
page_state_change_out:
	mutex_unlock(&mmut->mmu_lock);

	kunmap(as_page(new_phys));
new_page_map_error:
	kunmap(as_page(old_phys));
old_page_map_error:
	return ret;

undo_mappings:
	/* Unlock the MMU table and undo mappings. */
	mutex_unlock(&mmut->mmu_lock);
	kunmap(phys_to_page(pgd));
	kunmap(as_page(new_phys));
	kunmap(as_page(old_phys));

	return ret;
}

static void mmu_teardown_level(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
			       phys_addr_t pgd, unsigned int level)
{
	u64 *pgd_page;
	int i;
	struct memory_group_manager_device *mgm_dev = kbdev->mgm_dev;
	struct kbase_mmu_mode const *mmu_mode = kbdev->mmu_mode;
	u64 *pgd_page_buffer = NULL;
	struct page *p = phys_to_page(pgd);

	lockdep_assert_held(&mmut->mmu_lock);

	pgd_page = kmap_atomic(p);
	/* kmap_atomic should NEVER fail. */
	if (WARN_ON_ONCE(pgd_page == NULL))
		return;
	if (level < MIDGARD_MMU_BOTTOMLEVEL) {
		/* Copy the page to our preallocated buffer so that we can minimize
		 * kmap_atomic usage
		 */
		pgd_page_buffer = mmut->scratch_mem.teardown_pages.levels[level];
		memcpy(pgd_page_buffer, pgd_page, PAGE_SIZE);
	}

	/* When page migration is enabled, kbase_region_tracker_term() would ensure
	 * there are no pages left mapped on the GPU for a context. Hence the count
	 * of valid entries is expected to be zero here.
	 */
	if (kbase_page_migration_enabled && mmut->kctx)
		WARN_ON_ONCE(kbdev->mmu_mode->get_num_valid_entries(pgd_page));
	/* Invalidate page after copying */
	mmu_mode->entries_invalidate(pgd_page, KBASE_MMU_PAGE_ENTRIES);
	kunmap_atomic(pgd_page);
	pgd_page = pgd_page_buffer;

	if (level < MIDGARD_MMU_BOTTOMLEVEL) {
		for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++) {
			if (mmu_mode->pte_is_valid(pgd_page[i], level)) {
				phys_addr_t target_pgd = mmu_mode->pte_to_phy_addr(
					mgm_dev->ops.mgm_pte_to_original_pte(mgm_dev,
									     MGM_DEFAULT_PTE_GROUP,
									     level, pgd_page[i]));

				mmu_teardown_level(kbdev, mmut, target_pgd, level + 1);
			}
		}
	}

	kbase_mmu_free_pgd(kbdev, mmut, pgd);
}

int kbase_mmu_init(struct kbase_device *const kbdev,
	struct kbase_mmu_table *const mmut, struct kbase_context *const kctx,
	int const group_id)
{
	if (WARN_ON(group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS) ||
	    WARN_ON(group_id < 0))
		return -EINVAL;

	compiletime_assert(KBASE_MEM_ALLOC_MAX_SIZE <= (((8ull << 30) >> PAGE_SHIFT)),
			   "List of free PGDs may not be large enough.");
	compiletime_assert(MAX_PAGES_FOR_FREE_PGDS >= MIDGARD_MMU_BOTTOMLEVEL,
			   "Array of MMU levels is not large enough.");

	mmut->group_id = group_id;
	mutex_init(&mmut->mmu_lock);
	mmut->kctx = kctx;
	mmut->pgd = KBASE_MMU_INVALID_PGD_ADDRESS;

	/* We allocate pages into the kbdev memory pool, then
	 * kbase_mmu_alloc_pgd will allocate out of that pool. This is done to
	 * avoid allocations from the kernel happening with the lock held.
	 */
	while (mmut->pgd == KBASE_MMU_INVALID_PGD_ADDRESS) {
		int err;

		err = kbase_mem_pool_grow(
			&kbdev->mem_pools.small[mmut->group_id],
			MIDGARD_MMU_BOTTOMLEVEL, kctx ? kctx->task : NULL);
		if (err) {
			kbase_mmu_term(kbdev, mmut);
			return -ENOMEM;
		}

		mutex_lock(&mmut->mmu_lock);
		mmut->pgd = kbase_mmu_alloc_pgd(kbdev, mmut);
		mutex_unlock(&mmut->mmu_lock);
	}

	return 0;
}

void kbase_mmu_term(struct kbase_device *kbdev, struct kbase_mmu_table *mmut)
{
	WARN((mmut->kctx) && (mmut->kctx->as_nr != KBASEP_AS_NR_INVALID),
	     "kctx-%d_%d must first be scheduled out to flush GPU caches+tlbs before tearing down MMU tables",
	     mmut->kctx->tgid, mmut->kctx->id);

	if (mmut->pgd != KBASE_MMU_INVALID_PGD_ADDRESS) {
		mutex_lock(&mmut->mmu_lock);
		mmu_teardown_level(kbdev, mmut, mmut->pgd, MIDGARD_MMU_TOPLEVEL);
		mutex_unlock(&mmut->mmu_lock);

		if (mmut->kctx)
			KBASE_TLSTREAM_AUX_PAGESALLOC(kbdev, mmut->kctx->id, 0);
	}

	mutex_destroy(&mmut->mmu_lock);
}

void kbase_mmu_as_term(struct kbase_device *kbdev, unsigned int i)
{
	destroy_workqueue(kbdev->as[i].pf_wq);
}

void kbase_mmu_flush_pa_range(struct kbase_device *kbdev, struct kbase_context *kctx,
			      phys_addr_t phys, size_t size,
			      enum kbase_mmu_op_type flush_op)
{
#if MALI_USE_CSF
	unsigned long irq_flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
	if (mmu_flush_cache_on_gpu_ctrl(kbdev) && (flush_op != KBASE_MMU_OP_NONE) &&
	    kbdev->pm.backend.gpu_powered && (!kctx || kctx->as_nr >= 0))
		mmu_flush_pa_range(kbdev, phys, size, KBASE_MMU_OP_FLUSH_PT);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);
#endif
}

#ifdef CONFIG_MALI_VECTOR_DUMP
static size_t kbasep_mmu_dump_level(struct kbase_context *kctx, phys_addr_t pgd,
		int level, char ** const buffer, size_t *size_left)
{
	phys_addr_t target_pgd;
	u64 *pgd_page;
	int i;
	size_t size = KBASE_MMU_PAGE_ENTRIES * sizeof(u64) + sizeof(u64);
	size_t dump_size;
	struct kbase_device *kbdev;
	struct kbase_mmu_mode const *mmu_mode;

	if (WARN_ON(kctx == NULL))
		return 0;
	lockdep_assert_held(&kctx->mmu.mmu_lock);

	kbdev = kctx->kbdev;
	mmu_mode = kbdev->mmu_mode;

	pgd_page = kmap(pfn_to_page(PFN_DOWN(pgd)));
	if (!pgd_page) {
		dev_warn(kbdev->dev, "%s: kmap failure", __func__);
		return 0;
	}

	if (*size_left >= size) {
		/* A modified physical address that contains
		 * the page table level
		 */
		u64 m_pgd = pgd | level;

		/* Put the modified physical address in the output buffer */
		memcpy(*buffer, &m_pgd, sizeof(m_pgd));
		*buffer += sizeof(m_pgd);

		/* Followed by the page table itself */
		memcpy(*buffer, pgd_page, sizeof(u64) * KBASE_MMU_PAGE_ENTRIES);
		*buffer += sizeof(u64) * KBASE_MMU_PAGE_ENTRIES;

		*size_left -= size;
	}

	if (level < MIDGARD_MMU_BOTTOMLEVEL) {
		for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++) {
			if (mmu_mode->pte_is_valid(pgd_page[i], level)) {
				target_pgd = mmu_mode->pte_to_phy_addr(
					kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
						kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP,
						level, pgd_page[i]));

				dump_size = kbasep_mmu_dump_level(kctx,
						target_pgd, level + 1,
						buffer, size_left);
				if (!dump_size) {
					kunmap(pfn_to_page(PFN_DOWN(pgd)));
					return 0;
				}
				size += dump_size;
			}
		}
	}

	kunmap(pfn_to_page(PFN_DOWN(pgd)));

	return size;
}

void *kbase_mmu_dump(struct kbase_context *kctx, int nr_pages)
{
	void *kaddr;
	size_t size_left;

	KBASE_DEBUG_ASSERT(kctx);

	if (nr_pages == 0) {
		/* can't dump in a 0 sized buffer, early out */
		return NULL;
	}

	size_left = nr_pages * PAGE_SIZE;

	if (WARN_ON(size_left == 0))
		return NULL;
	kaddr = vmalloc_user(size_left);

	mutex_lock(&kctx->mmu.mmu_lock);

	if (kaddr) {
		u64 end_marker = 0xFFULL;
		char *buffer;
		char *mmu_dump_buffer;
		u64 config[3];
		size_t dump_size, size = 0;
		struct kbase_mmu_setup as_setup;

		buffer = (char *)kaddr;
		mmu_dump_buffer = buffer;

		kctx->kbdev->mmu_mode->get_as_setup(&kctx->mmu,
				&as_setup);
		config[0] = as_setup.transtab;
		config[1] = as_setup.memattr;
		config[2] = as_setup.transcfg;
		memcpy(buffer, &config, sizeof(config));
		mmu_dump_buffer += sizeof(config);
		size_left -= sizeof(config);
		size += sizeof(config);

		dump_size = kbasep_mmu_dump_level(kctx,
				kctx->mmu.pgd,
				MIDGARD_MMU_TOPLEVEL,
				&mmu_dump_buffer,
				&size_left);

		if (!dump_size)
			goto fail_free;

		size += dump_size;

		/* Add on the size for the end marker */
		size += sizeof(u64);

		if (size > (nr_pages * PAGE_SIZE)) {
			/* The buffer isn't big enough - free the memory and
			 * return failure
			 */
			goto fail_free;
		}

		/* Add the end marker */
		memcpy(mmu_dump_buffer, &end_marker, sizeof(u64));
	}

	mutex_unlock(&kctx->mmu.mmu_lock);
	return kaddr;

fail_free:
	vfree(kaddr);
	mutex_unlock(&kctx->mmu.mmu_lock);
	return NULL;
}
KBASE_EXPORT_TEST_API(kbase_mmu_dump);
#endif /* CONFIG_MALI_VECTOR_DUMP */

void kbase_mmu_bus_fault_worker(struct work_struct *data)
{
	struct kbase_as *faulting_as;
	int as_no;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	struct kbase_fault *fault;

	faulting_as = container_of(data, struct kbase_as, work_busfault);
	fault = &faulting_as->bf_data;

	/* Ensure that any pending page fault worker has completed */
	flush_work(&faulting_as->work_pagefault);

	as_no = faulting_as->number;

	kbdev = container_of(faulting_as, struct kbase_device, as[as_no]);

	/* Grab the context, already refcounted in kbase_mmu_interrupt() on
	 * flagging of the bus-fault. Therefore, it cannot be scheduled out of
	 * this AS until we explicitly release it
	 */
	kctx = kbase_ctx_sched_as_to_ctx(kbdev, as_no);
	if (!kctx) {
		atomic_dec(&kbdev->faults_pending);
		return;
	}

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	/* check if we still have GPU */
	if (unlikely(kbase_is_gpu_removed(kbdev))) {
		dev_dbg(kbdev->dev, "%s: GPU has been removed", __func__);
		release_ctx(kbdev, kctx);
		atomic_dec(&kbdev->faults_pending);
		return;
	}
#endif

	if (unlikely(fault->protected_mode)) {
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Permission failure", fault);
		kbase_mmu_hw_clear_fault(kbdev, faulting_as,
				KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
		release_ctx(kbdev, kctx);
		atomic_dec(&kbdev->faults_pending);
		return;

	}

#if MALI_USE_CSF
	/* Before the GPU power off, wait is done for the completion of
	 * in-flight MMU fault work items. So GPU is expected to remain
	 * powered up whilst the bus fault handling is being done.
	 */
	kbase_gpu_report_bus_fault_and_kill(kctx, faulting_as, fault);
#else
	/* NOTE: If GPU already powered off for suspend,
	 * we don't need to switch to unmapped
	 */
	if (!kbase_pm_context_active_handle_suspend(kbdev,
				KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE)) {
		kbase_gpu_report_bus_fault_and_kill(kctx, faulting_as, fault);
		kbase_pm_context_idle(kbdev);
	}
#endif

	release_ctx(kbdev, kctx);

	atomic_dec(&kbdev->faults_pending);
}

void kbase_flush_mmu_wqs(struct kbase_device *kbdev)
{
	int i;

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		struct kbase_as *as = &kbdev->as[i];

		flush_workqueue(as->pf_wq);
	}
}
