// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2010-2022 ARM Limited. All rights reserved.
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
 * through GPU_CONTROL interface
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
	int err = 0;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->pm.backend.gpu_powered && (!kctx || kctx->as_nr >= 0)) {
		as_nr = kctx ? kctx->as_nr : as_nr;
		err = kbase_mmu_hw_do_unlock(kbdev, &kbdev->as[as_nr], op_param);
	}

	if (err) {
		dev_err(kbdev->dev,
			"Invalidate after GPU page table update did not complete. Issuing GPU soft-reset to recover");
		if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_HWC_UNRECOVERABLE_ERROR))
			kbase_reset_gpu(kbdev);
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

/* Perform a flush/invalidate on a particular address space
 */
static void mmu_flush_invalidate_as(struct kbase_device *kbdev, struct kbase_as *as,
				    const struct kbase_mmu_hw_op_param *op_param)
{
	int err;
	bool gpu_powered;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	gpu_powered = kbdev->pm.backend.gpu_powered;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* GPU is off so there's no need to perform flush/invalidate.
	 * But even if GPU is not actually powered down, after gpu_powered flag
	 * was set to false, it is still safe to skip the flush/invalidate.
	 * The TLB invalidation will anyways be performed due to AS_COMMAND_UPDATE
	 * which is sent when address spaces are restored after gpu_powered flag
	 * is set to true. Flushing of L2 cache is certainly not required as L2
	 * cache is definitely off if gpu_powered is false.
	 */
	if (!gpu_powered)
		return;

	if (kbase_pm_context_active_handle_suspend(kbdev,
				KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE)) {
		/* GPU has just been powered off due to system suspend.
		 * So again, no need to perform flush/invalidate.
		 */
		return;
	}

	/* AS transaction begin */
	mutex_lock(&kbdev->mmu_hw_mutex);

	mmu_hw_operation_begin(kbdev);
	err = kbase_mmu_hw_do_flush(kbdev, as, op_param);
	mmu_hw_operation_end(kbdev);

	if (err) {
		/* Flush failed to complete, assume the GPU has hung and
		 * perform a reset to recover.
		 */
		dev_err(kbdev->dev, "Flush for GPU page table update did not complete. Issuing GPU soft-reset to recover");

		if (kbase_prepare_to_reset_gpu(
			    kbdev, RESET_FLAGS_HWC_UNRECOVERABLE_ERROR))
			kbase_reset_gpu(kbdev);
	}

	mutex_unlock(&kbdev->mmu_hw_mutex);
	/* AS transaction end */

	kbase_pm_context_idle(kbdev);
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
 *
 * If operation is set to KBASE_MMU_OP_UNLOCK then this function will only
 * invalidate the MMU caches and TLBs.
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
	int err = 0;
	unsigned long flags;

	/* AS transaction begin */
	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->pm.backend.gpu_powered && (!kctx || kctx->as_nr >= 0)) {
		as_nr = kctx ? kctx->as_nr : as_nr;
		err = kbase_mmu_hw_do_flush_on_gpu_ctrl(kbdev, &kbdev->as[as_nr],
							op_param);
	}

	if (err) {
		/* Flush failed to complete, assume the GPU has hung and
		 * perform a reset to recover.
		 */
		dev_err(kbdev->dev,
			"Flush for GPU page table update did not complete. Issuing GPU soft-reset to recover\n");

		if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_HWC_UNRECOVERABLE_ERROR))
			kbase_reset_gpu(kbdev);
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);
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
	/* In non-coherent system, ensure the GPU can read
	 * the pages from memory
	 */
	if (kbdev->system_coherency == COHERENCY_NONE)
		dma_sync_single_for_device(kbdev->dev, handle, size,
				DMA_TO_DEVICE);

}

/*
 * Definitions:
 * - PGD: Page Directory.
 * - PTE: Page Table Entry. A 64bit value pointing to the next
 *        level of translation
 * - ATE: Address Translation Entry. A 64bit value pointing to
 *        a 4kB physical page.
 */

static int kbase_mmu_update_pages_no_flush(struct kbase_context *kctx, u64 vpfn,
					   struct tagged_addr *phys, size_t nr, unsigned long flags,
					   int group_id, u64 *dirty_pgds);

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
/**
 * kbase_mmu_free_pgd() - Free memory of the page directory
 *
 * @kbdev:   Device pointer.
 * @mmut:    GPU MMU page table.
 * @pgd:     Physical address of page directory to be freed.
 * @dirty:   Flag to indicate whether the page may be dirty in the cache.
 */
static void kbase_mmu_free_pgd(struct kbase_device *kbdev,
			       struct kbase_mmu_table *mmut, phys_addr_t pgd,
			       bool dirty);
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
			"VA Region 0x%llx extension was 0, allocator needs to set this properly for KBASE_REG_PF_GROW\n",
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
	int err;

	/* Calls to this function are inherently synchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_SYNC;
	struct kbase_mmu_hw_op_param op_param;

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
		err = kbase_mmu_hw_do_flush_on_gpu_ctrl(kbdev, faulting_as,
							&op_param);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);
	} else {
		mmu_hw_operation_begin(kbdev);
		err = kbase_mmu_hw_do_flush(kbdev, faulting_as, &op_param);
		mmu_hw_operation_end(kbdev);
	}

	mutex_unlock(&kbdev->mmu_hw_mutex);

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
	int ret;
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
	ret = kbase_mmu_update_pages_no_flush(kctx, fault_pfn, fault_phys_addr, 1, region->flags,
					      region->gpu_alloc->group_id, &dirty_pgds);

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

#define MAX_POOL_LEVEL 2

/**
 * page_fault_try_alloc - Try to allocate memory from a context pool
 * @kctx:          Context pointer
 * @region:        Region to grow
 * @new_pages:     Number of 4 kB pages to allocate
 * @pages_to_grow: Pointer to variable to store number of outstanding pages on
 *                 failure. This can be either 4 kB or 2 MB pages, depending on
 *                 the number of pages requested.
 * @grow_2mb_pool: Pointer to variable to store which pool needs to grow - true
 *                 for 2 MB, false for 4 kB.
 * @prealloc_sas:  Pointer to kbase_sub_alloc structures
 *
 * This function will try to allocate as many pages as possible from the context
 * pool, then if required will try to allocate the remaining pages from the
 * device pool.
 *
 * This function will not allocate any new memory beyond that is already
 * present in the context or device pools. This is because it is intended to be
 * called with the vm_lock held, which could cause recursive locking if the
 * allocation caused the out-of-memory killer to run.
 *
 * If 2 MB pages are enabled and new_pages is >= 2 MB then pages_to_grow will be
 * a count of 2 MB pages, otherwise it will be a count of 4 kB pages.
 *
 * Return: true if successful, false on failure
 */
static bool page_fault_try_alloc(struct kbase_context *kctx,
		struct kbase_va_region *region, size_t new_pages,
		int *pages_to_grow, bool *grow_2mb_pool,
		struct kbase_sub_alloc **prealloc_sas)
{
	struct tagged_addr *gpu_pages[MAX_POOL_LEVEL] = {NULL};
	struct tagged_addr *cpu_pages[MAX_POOL_LEVEL] = {NULL};
	size_t pages_alloced[MAX_POOL_LEVEL] = {0};
	struct kbase_mem_pool *pool, *root_pool;
	int pool_level = 0;
	bool alloc_failed = false;
	size_t pages_still_required;

	if (WARN_ON(region->gpu_alloc->group_id >=
		MEMORY_GROUP_MANAGER_NR_GROUPS)) {
		/* Do not try to grow the memory pool */
		*pages_to_grow = 0;
		return false;
	}

#ifdef CONFIG_MALI_2MB_ALLOC
	if (new_pages >= (SZ_2M / SZ_4K)) {
		root_pool = &kctx->mem_pools.large[region->gpu_alloc->group_id];
		*grow_2mb_pool = true;
	} else {
#endif
		root_pool = &kctx->mem_pools.small[region->gpu_alloc->group_id];
		*grow_2mb_pool = false;
#ifdef CONFIG_MALI_2MB_ALLOC
	}
#endif

	if (region->gpu_alloc != region->cpu_alloc)
		new_pages *= 2;

	pages_still_required = new_pages;

	/* Determine how many pages are in the pools before trying to allocate.
	 * Don't attempt to allocate & free if the allocation can't succeed.
	 */
	for (pool = root_pool; pool != NULL; pool = pool->next_pool) {
		size_t pool_size_4k;

		kbase_mem_pool_lock(pool);

		pool_size_4k = kbase_mem_pool_size(pool) << pool->order;
		if (pool_size_4k >= pages_still_required)
			pages_still_required = 0;
		else
			pages_still_required -= pool_size_4k;

		kbase_mem_pool_unlock(pool);

		if (!pages_still_required)
			break;
	}

	if (pages_still_required) {
		/* Insufficient pages in pools. Don't try to allocate - just
		 * request a grow.
		 */
		*pages_to_grow = pages_still_required;

		return false;
	}

	/* Since we've dropped the pool locks, the amount of memory in the pools
	 * may change between the above check and the actual allocation.
	 */
	pool = root_pool;
	for (pool_level = 0; pool_level < MAX_POOL_LEVEL; pool_level++) {
		size_t pool_size_4k;
		size_t pages_to_alloc_4k;
		size_t pages_to_alloc_4k_per_alloc;

		kbase_mem_pool_lock(pool);

		/* Allocate as much as possible from this pool*/
		pool_size_4k = kbase_mem_pool_size(pool) << pool->order;
		pages_to_alloc_4k = MIN(new_pages, pool_size_4k);
		if (region->gpu_alloc == region->cpu_alloc)
			pages_to_alloc_4k_per_alloc = pages_to_alloc_4k;
		else
			pages_to_alloc_4k_per_alloc = pages_to_alloc_4k >> 1;

		pages_alloced[pool_level] = pages_to_alloc_4k;
		if (pages_to_alloc_4k) {
			gpu_pages[pool_level] =
					kbase_alloc_phy_pages_helper_locked(
						region->gpu_alloc, pool,
						pages_to_alloc_4k_per_alloc,
						&prealloc_sas[0]);

			if (!gpu_pages[pool_level]) {
				alloc_failed = true;
			} else if (region->gpu_alloc != region->cpu_alloc) {
				cpu_pages[pool_level] =
					kbase_alloc_phy_pages_helper_locked(
						region->cpu_alloc, pool,
						pages_to_alloc_4k_per_alloc,
						&prealloc_sas[1]);

				if (!cpu_pages[pool_level])
					alloc_failed = true;
			}
		}

		kbase_mem_pool_unlock(pool);

		if (alloc_failed) {
			WARN_ON(!new_pages);
			WARN_ON(pages_to_alloc_4k >= new_pages);
			WARN_ON(pages_to_alloc_4k_per_alloc >= new_pages);
			break;
		}

		new_pages -= pages_to_alloc_4k;

		if (!new_pages)
			break;

		pool = pool->next_pool;
		if (!pool)
			break;
	}

	if (new_pages) {
		/* Allocation was unsuccessful */
		int max_pool_level = pool_level;

		pool = root_pool;

		/* Free memory allocated so far */
		for (pool_level = 0; pool_level <= max_pool_level;
				pool_level++) {
			kbase_mem_pool_lock(pool);

			if (region->gpu_alloc != region->cpu_alloc) {
				if (pages_alloced[pool_level] &&
						cpu_pages[pool_level])
					kbase_free_phy_pages_helper_locked(
						region->cpu_alloc,
						pool, cpu_pages[pool_level],
						pages_alloced[pool_level]);
			}

			if (pages_alloced[pool_level] && gpu_pages[pool_level])
				kbase_free_phy_pages_helper_locked(
						region->gpu_alloc,
						pool, gpu_pages[pool_level],
						pages_alloced[pool_level]);

			kbase_mem_pool_unlock(pool);

			pool = pool->next_pool;
		}

		/*
		 * If the allocation failed despite there being enough memory in
		 * the pool, then just fail. Otherwise, try to grow the memory
		 * pool.
		 */
		if (alloc_failed)
			*pages_to_grow = 0;
		else
			*pages_to_grow = new_pages;

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
	dev_dbg(kbdev->dev,
		"Entering %s %pK, fault_pfn %lld, as_no %d\n",
		__func__, (void *)data, fault_pfn, as_no);

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
		dev_dbg(kbdev->dev,
				"%s: GPU has been removed\n", __func__);
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

#ifdef CONFIG_MALI_2MB_ALLOC
	/* Preallocate memory for the sub-allocation structs if necessary */
	for (i = 0; i != ARRAY_SIZE(prealloc_sas); ++i) {
		prealloc_sas[i] = kmalloc(sizeof(*prealloc_sas[i]), GFP_KERNEL);
		if (!prealloc_sas[i]) {
			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
					"Failed pre-allocating memory for sub-allocations' metadata",
					fault);
			goto fault_done;
		}
	}
#endif /* CONFIG_MALI_2MB_ALLOC */

page_fault_retry:
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
	dev_dbg(kctx->kbdev->dev, "Allocate %zu pages on page fault\n",
		new_pages);

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
		err = kbase_mmu_insert_pages_no_flush(kbdev, &kctx->mmu,
						      region->start_pfn + pfn_offset,
						      &kbase_get_gpu_phy_pages(region)[pfn_offset],
						      new_pages, region->flags,
						      region->gpu_alloc->group_id, &dirty_pgds);
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

			dev_dbg(kctx->kbdev->dev,
				"%zu pages exceeded IR threshold %zu\n",
				new_pages + current_backed_size,
				region->threshold_pages);

			if (kbase_mmu_switch_to_ir(kctx, region) >= 0) {
				dev_dbg(kctx->kbdev->dev,
					"Get region %pK for IR\n",
					(void *)region);
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
#ifdef CONFIG_MALI_2MB_ALLOC
			if (grow_2mb_pool) {
				/* Round page requirement up to nearest 2 MB */
				struct kbase_mem_pool *const lp_mem_pool =
					&kctx->mem_pools.large[
					region->gpu_alloc->group_id];

				pages_to_grow = (pages_to_grow +
					((1 << lp_mem_pool->order) - 1))
						>> lp_mem_pool->order;

				ret = kbase_mem_pool_grow(lp_mem_pool,
					pages_to_grow);
			} else {
#endif
				struct kbase_mem_pool *const mem_pool =
					&kctx->mem_pools.small[
					region->gpu_alloc->group_id];

				ret = kbase_mem_pool_grow(mem_pool,
					pages_to_grow);
#ifdef CONFIG_MALI_2MB_ALLOC
			}
#endif
		}
		if (ret < 0) {
			/* failed to extend, handle as a normal PF */
			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
					"Page allocation failure", fault);
		} else {
			dev_dbg(kbdev->dev, "Try again after pool_grow\n");
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
	dev_dbg(kbdev->dev, "Leaving page_fault_worker %pK\n", (void *)data);
}

static phys_addr_t kbase_mmu_alloc_pgd(struct kbase_device *kbdev,
		struct kbase_mmu_table *mmut)
{
	u64 *page;
	int i;
	struct page *p;
	phys_addr_t pgd;

	p = kbase_mem_pool_alloc(&kbdev->mem_pools.small[mmut->group_id]);
	if (!p)
		return 0;

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

	for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++)
		kbdev->mmu_mode->entry_invalidate(&page[i]);

	/* MMU cache flush strategy is NONE because this page is newly created, therefore
	 * there is no content to clean or invalidate in the GPU caches.
	 */
	kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd, kbase_dma_addr(p), PAGE_SIZE, KBASE_MMU_OP_NONE);

	kunmap(p);
	return pgd;

alloc_free:
	kbase_mem_pool_free(&kbdev->mem_pools.small[mmut->group_id], p, false);

	return 0;
}

/* Given PGD PFN for level N, return PGD PFN for level N+1, allocating the
 * new table from the pool if needed and possible
 */
static int mmu_get_next_pgd(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
			    phys_addr_t *pgd, u64 vpfn, int level, bool *newly_created_pgd,
			    u64 *dirty_pgds)
{
	u64 *page;
	phys_addr_t target_pgd;
	struct page *p;

	KBASE_DEBUG_ASSERT(*pgd);

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
		dev_warn(kbdev->dev, "%s: kmap failure\n", __func__);
		return -EINVAL;
	}

	target_pgd = kbdev->mmu_mode->pte_to_phy_addr(
		kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
			kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, page[vpfn]));

	if (!target_pgd) {
		enum kbase_mmu_op_type flush_op = KBASE_MMU_OP_NONE;
		unsigned int current_valid_entries;
		u64 managed_pte;
		target_pgd = kbase_mmu_alloc_pgd(kbdev, mmut);
		if (!target_pgd) {
			dev_dbg(kbdev->dev, "%s: kbase_mmu_alloc_pgd failure\n",
					__func__);
			kunmap(p);
			return -ENOMEM;
		}

		current_valid_entries = kbdev->mmu_mode->get_num_valid_entries(page);
		kbdev->mmu_mode->entry_set_pte(&managed_pte, target_pgd);
		page[vpfn] = kbdev->mgm_dev->ops.mgm_update_gpu_pte(
			kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, managed_pte);
		kbdev->mmu_mode->set_num_valid_entries(page, current_valid_entries + 1);

		/* Rely on the caller to update the address space flags. */
		if (newly_created_pgd && !*newly_created_pgd) {
			*newly_created_pgd = true;
			/* If code reaches here we know parent PGD of target PGD was
			 * not newly created and should be flushed.
			 */
			flush_op = KBASE_MMU_OP_FLUSH_PT;

			if (dirty_pgds)
				*dirty_pgds |= 1ULL << level;
		}

		/* MMU cache flush strategy is FLUSH_PT because a new entry is added
		 * to an existing PGD which may be stored in GPU caches and needs a
		 * "clean" operation. An "invalidation" operation is not required here
		 * as this entry points to a new page and cannot be present in GPU
		 * caches.
		 */
		kbase_mmu_sync_pgd(kbdev, mmut->kctx, *pgd, kbase_dma_addr(p), PAGE_SIZE, flush_op);
	}

	kunmap(p);
	*pgd = target_pgd;

	return 0;
}

/*
 * Returns the PGD for the specified level of translation
 */
static int mmu_get_pgd_at_level(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, u64 vpfn,
				int level, phys_addr_t *out_pgd, bool *newly_created_pgd,
				u64 *dirty_pgds)
{
	phys_addr_t pgd;
	int l;

	lockdep_assert_held(&mmut->mmu_lock);
	pgd = mmut->pgd;

	for (l = MIDGARD_MMU_TOPLEVEL; l < level; l++) {
		int err =
			mmu_get_next_pgd(kbdev, mmut, &pgd, vpfn, l, newly_created_pgd, dirty_pgds);
		/* Handle failure condition */
		if (err) {
			dev_dbg(kbdev->dev,
				 "%s: mmu_get_next_pgd failure at level %d\n",
				 __func__, l);
			return err;
		}
	}

	*out_pgd = pgd;

	return 0;
}

static int mmu_get_bottom_pgd(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, u64 vpfn,
			      phys_addr_t *out_pgd, bool *newly_created_pgd, u64 *dirty_pgds)
{
	return mmu_get_pgd_at_level(kbdev, mmut, vpfn, MIDGARD_MMU_BOTTOMLEVEL, out_pgd,
				    newly_created_pgd, dirty_pgds);
}

static void mmu_insert_pages_failure_recovery(struct kbase_device *kbdev,
					      struct kbase_mmu_table *mmut, u64 from_vpfn,
					      u64 to_vpfn, u64 *dirty_pgds)
{
	phys_addr_t pgd;
	u64 vpfn = from_vpfn;
	struct kbase_mmu_mode const *mmu_mode;

	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(vpfn <= (U64_MAX / PAGE_SIZE));
	KBASE_DEBUG_ASSERT(from_vpfn <= to_vpfn);

	lockdep_assert_held(&mmut->mmu_lock);

	mmu_mode = kbdev->mmu_mode;

	while (vpfn < to_vpfn) {
		unsigned int i;
		unsigned int idx = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - idx;
		unsigned int pcount = 0;
		unsigned int left = to_vpfn - vpfn;
		int level;
		u64 *page;
		phys_addr_t pgds[MIDGARD_MMU_BOTTOMLEVEL + 1];

		register unsigned int num_of_valid_entries;

		if (count > left)
			count = left;

		/* need to check if this is a 2MB page or a 4kB */
		pgd = mmut->pgd;

		for (level = MIDGARD_MMU_TOPLEVEL;
				level <= MIDGARD_MMU_BOTTOMLEVEL; level++) {
			idx = (vpfn >> ((3 - level) * 9)) & 0x1FF;
			pgds[level] = pgd;
			page = kmap(phys_to_page(pgd));
			if (mmu_mode->ate_is_valid(page[idx], level))
				break; /* keep the mapping */
			kunmap(phys_to_page(pgd));
			pgd = mmu_mode->pte_to_phy_addr(kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
				kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, page[idx]));
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
			dev_warn(kbdev->dev, "%sNo support for ATEs at level %d\n",
			       __func__, level);
			goto next;
		}

		if (dirty_pgds && pcount > 0)
			*dirty_pgds |= 1ULL << level;

		num_of_valid_entries = mmu_mode->get_num_valid_entries(page);
		if (WARN_ON_ONCE(num_of_valid_entries < pcount))
			num_of_valid_entries = 0;
		else
			num_of_valid_entries -= pcount;

		if (!num_of_valid_entries) {
			kunmap(phys_to_page(pgd));

			kbase_mmu_free_pgd(kbdev, mmut, pgd, true);

			kbase_mmu_update_and_free_parent_pgds(kbdev, mmut, pgds, vpfn, level,
							      KBASE_MMU_OP_NONE, dirty_pgds);
			vpfn += count;
			continue;
		}

		/* Invalidate the entries we added */
		for (i = 0; i < pcount; i++)
			mmu_mode->entry_invalidate(&page[idx + i]);

		mmu_mode->set_num_valid_entries(page, num_of_valid_entries);

		/* MMU cache flush strategy is NONE because GPU cache maintenance is
		 * going to be done by the caller
		 */
		kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (idx * sizeof(u64)),
				   kbase_dma_addr(phys_to_page(pgd)) + 8 * idx, 8 * pcount,
				   KBASE_MMU_OP_NONE);
		kunmap(phys_to_page(pgd));
next:
		vpfn += count;
	}
}

/*
 * Map the single page 'phys' 'nr' of times, starting at GPU PFN 'vpfn'
 */
int kbase_mmu_insert_single_page(struct kbase_context *kctx, u64 vpfn,
				 struct tagged_addr phys, size_t nr,
				 unsigned long flags, int const group_id,
				 enum kbase_caller_mmu_sync_info mmu_sync_info)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	/* In case the insert_single_page only partially completes
	 * we need to be able to recover
	 */
	bool recover_required = false;
	u64 start_vpfn = vpfn;
	size_t recover_count = 0;
	size_t remain = nr;
	int err;
	struct kbase_device *kbdev;
	enum kbase_mmu_op_type flush_op;
	struct kbase_mmu_hw_op_param op_param;
	u64 dirty_pgds = 0;

	if (WARN_ON(kctx == NULL))
		return -EINVAL;

	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(vpfn <= (U64_MAX / PAGE_SIZE));

	kbdev = kctx->kbdev;

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	/* Set up MMU flush operation parameters. */
	op_param = (struct kbase_mmu_hw_op_param){
		.vpfn = vpfn,
		.nr = nr,
		.op = KBASE_MMU_OP_FLUSH_PT,
		.kctx_id = kctx->id,
		.mmu_sync_info = mmu_sync_info,
	};

	mutex_lock(&kctx->mmu.mmu_lock);

	while (remain) {
		unsigned int i;
		unsigned int index = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - index;
		struct page *p;
		register unsigned int num_of_valid_entries;
		bool newly_created_pgd = false;

		if (count > remain)
			count = remain;

		/*
		 * Repeatedly calling mmu_get_bottom_pgd() is clearly
		 * suboptimal. We don't have to re-parse the whole tree
		 * each time (just cache the l0-l2 sequence).
		 * On the other hand, it's only a gain when we map more than
		 * 256 pages at once (on average). Do we really care?
		 */
		do {
			err = mmu_get_bottom_pgd(kbdev, &kctx->mmu, vpfn, &pgd, &newly_created_pgd,
						 &dirty_pgds);
			if (err != -ENOMEM)
				break;
			/* Fill the memory pool with enough pages for
			 * the page walk to succeed
			 */
			mutex_unlock(&kctx->mmu.mmu_lock);
			err = kbase_mem_pool_grow(
				&kbdev->mem_pools.small[
					kctx->mmu.group_id],
				MIDGARD_MMU_BOTTOMLEVEL);
			mutex_lock(&kctx->mmu.mmu_lock);
		} while (!err);
		if (err) {
			dev_warn(kbdev->dev, "%s: mmu_get_bottom_pgd failure\n",
				 __func__);
			if (recover_required) {
				/* Invalidate the pages we have partially
				 * completed
				 */
				mmu_insert_pages_failure_recovery(kbdev, &kctx->mmu, start_vpfn,
								  start_vpfn + recover_count,
								  &dirty_pgds);
			}
			goto fail_unlock;
		}

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap(p);
		if (!pgd_page) {
			dev_warn(kbdev->dev, "%s: kmap failure\n", __func__);
			if (recover_required) {
				/* Invalidate the pages we have partially
				 * completed
				 */
				mmu_insert_pages_failure_recovery(kbdev, &kctx->mmu, start_vpfn,
								  start_vpfn + recover_count,
								  &dirty_pgds);
			}
			err = -ENOMEM;
			goto fail_unlock;
		}

		num_of_valid_entries =
			kbdev->mmu_mode->get_num_valid_entries(pgd_page);

		for (i = 0; i < count; i++) {
			unsigned int ofs = index + i;

			/* Fail if the current page is a valid ATE entry */
			KBASE_DEBUG_ASSERT(0 == (pgd_page[ofs] & 1UL));

			pgd_page[ofs] = kbase_mmu_create_ate(kbdev,
				phys, flags, MIDGARD_MMU_BOTTOMLEVEL, group_id);
		}

		kbdev->mmu_mode->set_num_valid_entries(
			pgd_page, num_of_valid_entries + count);

		vpfn += count;
		remain -= count;

		if (count > 0 && !newly_created_pgd)
			dirty_pgds |= 1ULL << MIDGARD_MMU_BOTTOMLEVEL;

		/* MMU cache flush operation here will depend on whether bottom level
		 * PGD is newly created or not.
		 *
		 * If bottom level PGD is newly created then no cache maintenance is
		 * required as the PGD will not exist in GPU cache. Otherwise GPU cache
		 * maintenance is required for existing PGD.
		 */
		flush_op = newly_created_pgd ? KBASE_MMU_OP_NONE : KBASE_MMU_OP_FLUSH_PT;

		kbase_mmu_sync_pgd(kbdev, kctx, pgd + (index * sizeof(u64)),
				   kbase_dma_addr(p) + (index * sizeof(u64)), count * sizeof(u64),
				   flush_op);

		kunmap(p);
		/* We have started modifying the page table.
		 * If further pages need inserting and fail we need to undo what
		 * has already taken place
		 */
		recover_required = true;
		recover_count += count;
	}
	mutex_unlock(&kctx->mmu.mmu_lock);

	op_param.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds);
	/* If FLUSH_PA_RANGE is supported then existing PGDs will have been flushed
	 * and all that remains is TLB (or MMU cache) invalidation which is done via
	 * MMU UNLOCK command.
	 */
	if (mmu_flush_cache_on_gpu_ctrl(kbdev))
		mmu_invalidate(kbdev, kctx, kctx->as_nr, &op_param);
	else
		mmu_flush_invalidate(kbdev, kctx, kctx->as_nr, &op_param);
	return 0;

fail_unlock:
	mutex_unlock(&kctx->mmu.mmu_lock);
	op_param.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds);
	if (mmu_flush_cache_on_gpu_ctrl(kbdev))
		mmu_flush_invalidate_on_gpu_ctrl(kbdev, kctx, kctx->as_nr, &op_param);
	else
		mmu_flush_invalidate(kbdev, kctx, kctx->as_nr, &op_param);
	return err;
}

static void kbase_mmu_free_pgd(struct kbase_device *kbdev,
			       struct kbase_mmu_table *mmut, phys_addr_t pgd,
			       bool dirty)
{
	struct page *p;

	lockdep_assert_held(&mmut->mmu_lock);

	p = pfn_to_page(PFN_DOWN(pgd));

	kbase_mem_pool_free(&kbdev->mem_pools.small[mmut->group_id],
			    p, dirty);

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
				    unsigned long flags, int const group_id, u64 *dirty_pgds)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	u64 insert_vpfn = start_vpfn;
	size_t remain = nr;
	int err;
	struct kbase_mmu_mode const *mmu_mode;

	/* Note that 0 is a valid start_vpfn */
	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(start_vpfn <= (U64_MAX / PAGE_SIZE));

	mmu_mode = kbdev->mmu_mode;

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	mutex_lock(&mmut->mmu_lock);

	while (remain) {
		unsigned int i;
		unsigned int vindex = insert_vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - vindex;
		struct page *p;
		int cur_level;
		register unsigned int num_of_valid_entries;
		enum kbase_mmu_op_type flush_op;
		bool newly_created_pgd = false;

		if (count > remain)
			count = remain;

		if (!vindex && is_huge_head(*phys))
			cur_level = MIDGARD_MMU_LEVEL(2);
		else
			cur_level = MIDGARD_MMU_BOTTOMLEVEL;

		/*
		 * Repeatedly calling mmu_get_pgd_at_level() is clearly
		 * suboptimal. We don't have to re-parse the whole tree
		 * each time (just cache the l0-l2 sequence).
		 * On the other hand, it's only a gain when we map more than
		 * 256 pages at once (on average). Do we really care?
		 */
		do {
			err = mmu_get_pgd_at_level(kbdev, mmut, insert_vpfn, cur_level, &pgd,
						   &newly_created_pgd, dirty_pgds);
			if (err != -ENOMEM)
				break;
			/* Fill the memory pool with enough pages for
			 * the page walk to succeed
			 */
			mutex_unlock(&mmut->mmu_lock);
			err = kbase_mem_pool_grow(
				&kbdev->mem_pools.small[mmut->group_id],
				cur_level);
			mutex_lock(&mmut->mmu_lock);
		} while (!err);

		if (err) {
			dev_warn(kbdev->dev, "%s: mmu_get_pgd_at_level failure\n", __func__);
			if (insert_vpfn != start_vpfn) {
				/* Invalidate the pages we have partially
				 * completed
				 */
				mmu_insert_pages_failure_recovery(kbdev, mmut, start_vpfn,
								  insert_vpfn, dirty_pgds);
			}
			goto fail_unlock;
		}

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap(p);
		if (!pgd_page) {
			dev_warn(kbdev->dev, "%s: kmap failure\n",
				 __func__);
			if (insert_vpfn != start_vpfn) {
				/* Invalidate the pages we have partially
				 * completed
				 */
				mmu_insert_pages_failure_recovery(kbdev, mmut, start_vpfn,
								  insert_vpfn, dirty_pgds);
			}
			err = -ENOMEM;
			goto fail_unlock;
		}

		num_of_valid_entries =
			mmu_mode->get_num_valid_entries(pgd_page);

		if (cur_level == MIDGARD_MMU_LEVEL(2)) {
			int level_index = (insert_vpfn >> 9) & 0x1FF;
			u64 *target = &pgd_page[level_index];

			if (mmu_mode->pte_is_valid(*target, cur_level)) {
				kbase_mmu_free_pgd(
					kbdev, mmut,
					kbdev->mmu_mode->pte_to_phy_addr(
						kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
							kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP,
							cur_level, *target)),
					false);
				num_of_valid_entries--;
			}
			*target = kbase_mmu_create_ate(kbdev, *phys, flags,
				cur_level, group_id);

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
			}
			num_of_valid_entries += count;
		}

		mmu_mode->set_num_valid_entries(pgd_page, num_of_valid_entries);

		if (dirty_pgds && count > 0 && !newly_created_pgd)
			*dirty_pgds |= 1ULL << cur_level;

		phys += count;
		insert_vpfn += count;
		remain -= count;

		/* For the most part, the creation of a new virtual memory mapping does
		 * not require cache flush operations, because the operation results
		 * into the creation of new memory pages which are not present in GPU
		 * caches. Therefore the defaul operation is NONE.
		 *
		 * However, it is quite common for the mapping to start and/or finish
		 * at an already existing PGD. Moreover, the PTEs modified are not
		 * necessarily aligned with GPU cache lines. Therefore, GPU cache
		 * maintenance is required for existing PGDs.
		 */
		flush_op = newly_created_pgd ? KBASE_MMU_OP_NONE : KBASE_MMU_OP_FLUSH_PT;

		kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (vindex * sizeof(u64)),
				   kbase_dma_addr(p) + (vindex * sizeof(u64)), count * sizeof(u64),
				   flush_op);

		kunmap(p);
	}

	err = 0;

fail_unlock:
	mutex_unlock(&mmut->mmu_lock);
	return err;
}

/*
 * Map 'nr' pages pointed to by 'phys' at GPU PFN 'vpfn' for GPU address space
 * number 'as_nr'.
 */
int kbase_mmu_insert_pages(struct kbase_device *kbdev,
			   struct kbase_mmu_table *mmut, u64 vpfn,
			   struct tagged_addr *phys, size_t nr,
			   unsigned long flags, int as_nr, int const group_id,
			   enum kbase_caller_mmu_sync_info mmu_sync_info)
{
	int err;
	struct kbase_mmu_hw_op_param op_param = { 0 };
	u64 dirty_pgds = 0;

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	err = kbase_mmu_insert_pages_no_flush(kbdev, mmut, vpfn, phys, nr, flags, group_id,
					      &dirty_pgds);

	op_param.vpfn = vpfn;
	op_param.nr = nr;
	op_param.op = KBASE_MMU_OP_FLUSH_PT;
	op_param.mmu_sync_info = mmu_sync_info;
	op_param.kctx_id = mmut->kctx ? mmut->kctx->id : 0xFFFFFFFF;
	op_param.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds);

	/* MMU cache flush strategy depends on whether GPU control commands for
	 * flushing physical address ranges are supported. The new physical pages
	 * are not present in GPU caches there for they don't need any cache
	 * maintenance, but PGDs in the page table may or may not be created anew.
	 *
	 * Operations that affect the whole GPU cache shall only be done if it's
	 * impossible to update physical ranges.
	 */
	if (mmu_flush_cache_on_gpu_ctrl(kbdev))
		mmu_invalidate(kbdev, mmut->kctx, as_nr, &op_param);
	else
		mmu_flush_invalidate(kbdev, mmut->kctx, as_nr, &op_param);

	return err;
}

KBASE_EXPORT_TEST_API(kbase_mmu_insert_pages);

/**
 * kbase_mmu_flush_noretain() - Flush and invalidate the GPU caches
 * without retaining the kbase context.
 * @kctx: The KBase context.
 * @vpfn: The virtual page frame number to start the flush on.
 * @nr: The number of pages to flush.
 *
 * As per kbase_mmu_flush_invalidate but doesn't retain the kctx or do any
 * other locking.
 */
static void kbase_mmu_flush_noretain(struct kbase_context *kctx, u64 vpfn, size_t nr)
{
	struct kbase_device *kbdev = kctx->kbdev;
	int err;
	/* Calls to this function are inherently asynchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;
	struct kbase_mmu_hw_op_param op_param;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);
	lockdep_assert_held(&kctx->kbdev->mmu_hw_mutex);

	/* Early out if there is nothing to do */
	if (nr == 0)
		return;

	/* flush L2 and unlock the VA (resumes the MMU) */
	op_param.vpfn = vpfn;
	op_param.nr = nr;
	op_param.op = KBASE_MMU_OP_FLUSH_MEM;
	op_param.kctx_id = kctx->id;
	op_param.mmu_sync_info = mmu_sync_info;
	if (mmu_flush_cache_on_gpu_ctrl(kbdev)) {
		/* Value used to prevent skipping of any levels when flushing */
		op_param.flush_skip_levels = pgd_level_to_skip_flush(0xF);
		err = kbase_mmu_hw_do_flush_on_gpu_ctrl(kbdev, &kbdev->as[kctx->as_nr],
							&op_param);
	} else {
		err = kbase_mmu_hw_do_flush_locked(kbdev, &kbdev->as[kctx->as_nr],
						   &op_param);
	}

	if (err) {
		/* Flush failed to complete, assume the
		 * GPU has hung and perform a reset to recover
		 */
		dev_err(kbdev->dev, "Flush for GPU page table update did not complete. Issuing GPU soft-reset to recover");

		if (kbase_prepare_to_reset_gpu_locked(kbdev, RESET_FLAGS_NONE))
			kbase_reset_gpu_locked(kbdev);
	}
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
	/* ASSERT that the context has a valid as_nr, which is only the case
	 * when it's scheduled in.
	 *
	 * as_nr won't change because the caller has the hwaccess_lock
	 */
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);
	lockdep_assert_held(&kctx->kbdev->mmu_hw_mutex);

	/*
	 * The address space is being disabled, drain all knowledge of it out
	 * from the caches as pages and page tables might be freed after this.
	 *
	 * The job scheduler code will already be holding the locks and context
	 * so just do the flush.
	 */
	kbase_mmu_flush_noretain(kctx, 0, ~0);

	kctx->kbdev->mmu_mode->disable_as(kctx->kbdev, kctx->as_nr);
#if !MALI_USE_CSF
	/*
	 * JM GPUs has some L1 read only caches that need to be invalidated
	 * with START_FLUSH configuration. Purge the MMU disabled kctx from
	 * the slot_rb tracking field so such invalidation is performed when
	 * a new katom is executed on the affected slots.
	 */
	kbase_backend_slot_kctx_purge_locked(kctx->kbdev, kctx);
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
		u64 *current_page = kmap(phys_to_page(pgds[current_level]));
		unsigned int current_valid_entries =
			kbdev->mmu_mode->get_num_valid_entries(current_page);

		/* We need to track every level that needs updating */
		if (dirty_pgds)
			*dirty_pgds |= 1ULL << current_level;

		if (current_valid_entries == 1 &&
		    current_level != MIDGARD_MMU_LEVEL(0)) {
			kunmap(phys_to_page(pgds[current_level]));

			kbase_mmu_free_pgd(kbdev, mmut, pgds[current_level],
					   true);
		} else {
			int index = (vpfn >> ((3 - current_level) * 9)) & 0x1FF;

			kbdev->mmu_mode->entry_invalidate(&current_page[index]);

			current_valid_entries--;

			kbdev->mmu_mode->set_num_valid_entries(
				current_page, current_valid_entries);

			kbase_mmu_sync_pgd(
				kbdev, mmut->kctx, pgds[current_level] + (index * sizeof(u64)),
				kbase_dma_addr(phys_to_page(pgds[current_level])) + 8 * index,
				8 * 1, flush_op);

			kunmap(phys_to_page(pgds[current_level]));
			break;
		}
	}
}

/**
 * mmu_flush_invalidate_teardown_pages() - Perform flush operation after unmapping pages.
 *
 * @kbdev:       Pointer to kbase device.
 * @kctx:        Pointer to kbase context.
 * @as_nr:       Address space number, for GPU cache maintenance operations
 *               that happen outside a specific kbase context.
 * @phys:        Array of physical pages to flush.
 * @op_param:  Non-NULL pointer to struct containing information about the flush
 *             operation to perform.
 *
 * This function will do one of three things:
 * 1. Invalidate the MMU caches, followed by a partial GPU cache flush of the
 *    individual pages that were unmapped if feature is supported on GPU.
 * 2. Perform a full GPU cache flush through the GPU_CONTROL interface if feature is
 *    supported on GPU or,
 * 3. Perform a full GPU cache flush through the MMU_CONTROL interface.
 */
static void mmu_flush_invalidate_teardown_pages(struct kbase_device *kbdev,
						struct kbase_context *kctx, int as_nr,
						struct tagged_addr *phys,
						struct kbase_mmu_hw_op_param *op_param)
{

	if (!mmu_flush_cache_on_gpu_ctrl(kbdev)) {
		mmu_flush_invalidate(kbdev, kctx, as_nr, op_param);
		return;
	} else if (op_param->op == KBASE_MMU_OP_FLUSH_MEM) {
		mmu_flush_invalidate_on_gpu_ctrl(kbdev, kctx, as_nr, op_param);
		return;
	}

}

/**
 * kbase_mmu_teardown_pages - Remove GPU virtual addresses from the MMU page table
 *
 * @kbdev:    Pointer to kbase device.
 * @mmut:     Pointer to GPU MMU page table.
 * @vpfn:     Start page frame number of the GPU virtual pages to unmap.
 * @phys:     Array of physical pages currently mapped to the virtual
 *            pages to unmap, or NULL. This is only used for GPU cache
 *            maintenance.
 * @nr:       Number of pages to unmap.
 * @as_nr:    Address space number, for GPU cache maintenance operations
 *            that happen outside a specific kbase context.
 *
 * We actually discard the ATE and free the page table pages if no valid entries
 * exist in PGD.
 *
 * IMPORTANT: This uses kbasep_js_runpool_release_ctx() when the context is
 * currently scheduled into the runpool, and so potentially uses a lot of locks.
 * These locks must be taken in the correct order with respect to others
 * already held by the caller. Refer to kbasep_js_runpool_release_ctx() for more
 * information.
 *
 * The @p phys pointer to physical pages is not necessary for unmapping virtual memory,
 * but it is used for fine-grained GPU cache maintenance. If @p phys is NULL,
 * GPU cache maintenance will be done as usual, that is invalidating the whole GPU caches
 * instead of specific physical address ranges.
 *
 * Return: 0 on success, otherwise an error code.
 */
int kbase_mmu_teardown_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, u64 vpfn,
			     struct tagged_addr *phys, size_t nr, int as_nr)
{
	phys_addr_t pgd;
	u64 start_vpfn = vpfn;
	size_t requested_nr = nr;
	enum kbase_mmu_op_type flush_op = KBASE_MMU_OP_NONE;
	struct kbase_mmu_mode const *mmu_mode;
	struct kbase_mmu_hw_op_param op_param;
	unsigned int i;
	int err = -EFAULT;
	u64 dirty_pgds = 0;

	/* Calls to this function are inherently asynchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	if (nr == 0) {
		/* early out if nothing to do */
		return 0;
	}

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
	if (mmu_flush_cache_on_gpu_ctrl(kbdev) && phys && nr <= KBASE_PA_RANGE_THRESHOLD_NR_PAGES)
		flush_op = KBASE_MMU_OP_FLUSH_PT;

	mutex_lock(&mmut->mmu_lock);

	mmu_mode = kbdev->mmu_mode;

	while (nr) {
		unsigned int index = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - index;
		unsigned int pcount;
		int level;
		u64 *page;
		phys_addr_t pgds[MIDGARD_MMU_BOTTOMLEVEL + 1];
		register unsigned int num_of_valid_entries;

		if (count > nr)
			count = nr;

		/* need to check if this is a 2MB or a 4kB page */
		pgd = mmut->pgd;

		for (level = MIDGARD_MMU_TOPLEVEL;
				level <= MIDGARD_MMU_BOTTOMLEVEL; level++) {
			phys_addr_t next_pgd;

			index = (vpfn >> ((3 - level) * 9)) & 0x1FF;
			page = kmap(phys_to_page(pgd));
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
			pgds[level] = pgd;
			kunmap(phys_to_page(pgd));
			pgd = next_pgd;
		}

		switch (level) {
		case MIDGARD_MMU_LEVEL(0):
		case MIDGARD_MMU_LEVEL(1):
			dev_warn(kbdev->dev,
				 "%s: No support for ATEs at level %d\n",
				 __func__, level);
			kunmap(phys_to_page(pgd));
			goto out;
		case MIDGARD_MMU_LEVEL(2):
			/* can only teardown if count >= 512 */
			if (count >= 512) {
				pcount = 1;
			} else {
				dev_warn(kbdev->dev,
					 "%s: limiting teardown as it tries to do a partial 2MB teardown, need 512, but have %d to tear down\n",
					 __func__, count);
				pcount = 0;
			}
			break;
		case MIDGARD_MMU_BOTTOMLEVEL:
			/* page count is the same as the logical count */
			pcount = count;
			break;
		default:
			dev_err(kbdev->dev,
				"%s: found non-mapped memory, early out\n",
				__func__);
			vpfn += count;
			nr -= count;
			continue;
		}

		if (pcount > 0)
			dirty_pgds |= 1ULL << level;

		num_of_valid_entries = mmu_mode->get_num_valid_entries(page);
		if (WARN_ON_ONCE(num_of_valid_entries < pcount))
			num_of_valid_entries = 0;
		else
			num_of_valid_entries -= pcount;

		if (!num_of_valid_entries) {
			kunmap(phys_to_page(pgd));

			kbase_mmu_free_pgd(kbdev, mmut, pgd, true);

			kbase_mmu_update_and_free_parent_pgds(kbdev, mmut, pgds, vpfn, level,
							      flush_op, &dirty_pgds);

			vpfn += count;
			nr -= count;
			continue;
		}

		/* Invalidate the entries we added */
		for (i = 0; i < pcount; i++)
			mmu_mode->entry_invalidate(&page[index + i]);

		mmu_mode->set_num_valid_entries(page, num_of_valid_entries);

		kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (index * sizeof(u64)),
				   kbase_dma_addr(phys_to_page(pgd)) + 8 * index, 8 * pcount,
				   flush_op);
next:
		kunmap(phys_to_page(pgd));
		vpfn += count;
		nr -= count;
	}
	err = 0;
out:
	mutex_unlock(&mmut->mmu_lock);
	/* Set up MMU operation parameters. See above about MMU cache flush strategy. */
	op_param = (struct kbase_mmu_hw_op_param){
		.vpfn = start_vpfn,
		.nr = requested_nr,
		.mmu_sync_info = mmu_sync_info,
		.kctx_id = mmut->kctx ? mmut->kctx->id : 0xFFFFFFFF,
		.op = (flush_op == KBASE_MMU_OP_FLUSH_PT) ? KBASE_MMU_OP_FLUSH_PT :
							    KBASE_MMU_OP_FLUSH_MEM,
		.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds),
	};
	mmu_flush_invalidate_teardown_pages(kbdev, mmut->kctx, as_nr, phys, &op_param);

	return err;
}

KBASE_EXPORT_TEST_API(kbase_mmu_teardown_pages);

/**
 * kbase_mmu_update_pages_no_flush() - Update attributes data in GPU page table entries
 *
 * @kctx:  Kbase context
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
 * the new flags that are passed (the physical pages pointed to by the page
 * table entries remain unchanged). It is used as a response to the changes of
 * the memory attributes.
 *
 * The caller is responsible for validating the memory attributes.
 *
 * Return: 0 if the attributes data in page table entries were updated
 *         successfully, otherwise an error code.
 */
static int kbase_mmu_update_pages_no_flush(struct kbase_context *kctx, u64 vpfn,
					   struct tagged_addr *phys, size_t nr, unsigned long flags,
					   int const group_id, u64 *dirty_pgds)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	int err;
	struct kbase_device *kbdev;

	if (WARN_ON(kctx == NULL))
		return -EINVAL;

	KBASE_DEBUG_ASSERT(vpfn <= (U64_MAX / PAGE_SIZE));

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	mutex_lock(&kctx->mmu.mmu_lock);

	kbdev = kctx->kbdev;

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

		err = mmu_get_pgd_at_level(kbdev, &kctx->mmu, vpfn, cur_level, &pgd, NULL,
					   dirty_pgds);
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
			kbase_mmu_sync_pgd(kbdev, kctx, pgd + (level_index * sizeof(u64)),
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
			kbase_mmu_sync_pgd(kbdev, kctx, pgd + (index * sizeof(u64)),
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

	mutex_unlock(&kctx->mmu.mmu_lock);
	return 0;

fail_unlock:
	mutex_unlock(&kctx->mmu.mmu_lock);
	return err;
}

int kbase_mmu_update_pages(struct kbase_context *kctx, u64 vpfn,
			   struct tagged_addr *phys, size_t nr,
			   unsigned long flags, int const group_id)
{
	int err;
	struct kbase_mmu_hw_op_param op_param;
	u64 dirty_pgds = 0;

	/* Calls to this function are inherently asynchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	err = kbase_mmu_update_pages_no_flush(kctx, vpfn, phys, nr, flags, group_id, &dirty_pgds);

	op_param = (const struct kbase_mmu_hw_op_param){
		.vpfn = vpfn,
		.nr = nr,
		.op = KBASE_MMU_OP_FLUSH_MEM,
		.kctx_id = kctx->id,
		.mmu_sync_info = mmu_sync_info,
		.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds),
	};

	if (mmu_flush_cache_on_gpu_ctrl(kctx->kbdev))
		mmu_flush_invalidate_on_gpu_ctrl(kctx->kbdev, kctx, kctx->as_nr, &op_param);
	else
		mmu_flush_invalidate(kctx->kbdev, kctx, kctx->as_nr, &op_param);
	return err;
}

static void mmu_teardown_level(struct kbase_device *kbdev,
		struct kbase_mmu_table *mmut, phys_addr_t pgd,
		int level)
{
	phys_addr_t target_pgd;
	u64 *pgd_page;
	int i;
	struct kbase_mmu_mode const *mmu_mode;
	u64 *pgd_page_buffer;

	lockdep_assert_held(&mmut->mmu_lock);

	/* Early-out. No need to kmap to check entries for L3 PGD. */
	if (level == MIDGARD_MMU_BOTTOMLEVEL) {
		kbase_mmu_free_pgd(kbdev, mmut, pgd, true);
		return;
	}

	pgd_page = kmap_atomic(pfn_to_page(PFN_DOWN(pgd)));
	/* kmap_atomic should NEVER fail. */
	if (WARN_ON(pgd_page == NULL))
		return;
	/* Copy the page to our preallocated buffer so that we can minimize
	 * kmap_atomic usage
	 */
	pgd_page_buffer = mmut->mmu_teardown_pages[level];
	memcpy(pgd_page_buffer, pgd_page, PAGE_SIZE);
	kunmap_atomic(pgd_page);
	pgd_page = pgd_page_buffer;

	mmu_mode = kbdev->mmu_mode;

	for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++) {
		target_pgd = mmu_mode->pte_to_phy_addr(kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
			kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP,
			level, pgd_page[i]));

		if (target_pgd) {
			if (mmu_mode->pte_is_valid(pgd_page[i], level)) {
				mmu_teardown_level(kbdev, mmut,
						   target_pgd,
						   level + 1);
			}
		}
	}

	kbase_mmu_free_pgd(kbdev, mmut, pgd, true);
}

int kbase_mmu_init(struct kbase_device *const kbdev,
	struct kbase_mmu_table *const mmut, struct kbase_context *const kctx,
	int const group_id)
{
	int level;

	if (WARN_ON(group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS) ||
	    WARN_ON(group_id < 0))
		return -EINVAL;

	mmut->group_id = group_id;
	mutex_init(&mmut->mmu_lock);
	mmut->kctx = kctx;
	mmut->pgd = 0;

	/* Preallocate MMU depth of 3 pages for mmu_teardown_level to use */
	for (level = MIDGARD_MMU_TOPLEVEL;
			level < MIDGARD_MMU_BOTTOMLEVEL; level++) {
		mmut->mmu_teardown_pages[level] =
			kmalloc(PAGE_SIZE, GFP_KERNEL);

		if (!mmut->mmu_teardown_pages[level]) {
			kbase_mmu_term(kbdev, mmut);
			return -ENOMEM;
		}
	}

	/* We allocate pages into the kbdev memory pool, then
	 * kbase_mmu_alloc_pgd will allocate out of that pool. This is done to
	 * avoid allocations from the kernel happening with the lock held.
	 */
	while (!mmut->pgd) {
		int err;

		err = kbase_mem_pool_grow(
			&kbdev->mem_pools.small[mmut->group_id],
			MIDGARD_MMU_BOTTOMLEVEL);
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
	int level;

	if (mmut->pgd) {
		mutex_lock(&mmut->mmu_lock);
		mmu_teardown_level(kbdev, mmut, mmut->pgd, MIDGARD_MMU_TOPLEVEL);
		mutex_unlock(&mmut->mmu_lock);

		if (mmut->kctx)
			KBASE_TLSTREAM_AUX_PAGESALLOC(kbdev, mmut->kctx->id, 0);
	}

	for (level = MIDGARD_MMU_TOPLEVEL;
			level < MIDGARD_MMU_BOTTOMLEVEL; level++) {
		if (!mmut->mmu_teardown_pages[level])
			break;
		kfree(mmut->mmu_teardown_pages[level]);
	}

	mutex_destroy(&mmut->mmu_lock);
}

void kbase_mmu_as_term(struct kbase_device *kbdev, int i)
{
	destroy_workqueue(kbdev->as[i].pf_wq);
}

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
		dev_warn(kbdev->dev, "%s: kmap failure\n", __func__);
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
		dev_dbg(kbdev->dev,
				"%s: GPU has been removed\n", __func__);
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
