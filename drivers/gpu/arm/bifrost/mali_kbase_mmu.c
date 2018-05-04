/*
 *
 * (C) COPYRIGHT 2010-2018 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */



/**
 * @file mali_kbase_mmu.c
 * Base kernel MMU management.
 */

/* #define DEBUG    1 */
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <mali_kbase.h>
#include <mali_midg_regmap.h>
#if defined(CONFIG_MALI_BIFROST_GATOR_SUPPORT)
#include <mali_kbase_gator.h>
#endif
#include <mali_kbase_tlstream.h>
#include <mali_kbase_instr_defs.h>
#include <mali_kbase_debug.h>

#define beenthere(kctx, f, a...)  dev_dbg(kctx->kbdev->dev, "%s:" f, __func__, ##a)

#include <mali_kbase_defs.h>
#include <mali_kbase_hw.h>
#include <mali_kbase_mmu_hw.h>
#include <mali_kbase_hwaccess_jm.h>
#include <mali_kbase_time.h>
#include <mali_kbase_mem.h>

#define KBASE_MMU_PAGE_ENTRIES 512

/**
 * kbase_mmu_flush_invalidate() - Flush and invalidate the GPU caches.
 * @kctx: The KBase context.
 * @vpfn: The virtual page frame number to start the flush on.
 * @nr: The number of pages to flush.
 * @sync: Set if the operation should be synchronous or not.
 *
 * Issue a cache flush + invalidate to the GPU caches and invalidate the TLBs.
 *
 * If sync is not set then transactions still in flight when the flush is issued
 * may use the old page tables and the data they write will not be written out
 * to memory, this function returns after the flush has been issued but
 * before all accesses which might effect the flushed region have completed.
 *
 * If sync is set then accesses in the flushed region will be drained
 * before data is flush and invalidated through L1, L2 and into memory,
 * after which point this function will return.
 */
static void kbase_mmu_flush_invalidate(struct kbase_context *kctx,
		u64 vpfn, size_t nr, bool sync);

/**
 * kbase_mmu_sync_pgd - sync page directory to memory
 * @kbdev:	Device pointer.
 * @handle:	Address of DMA region.
 * @size:       Size of the region to sync.
 *
 * This should be called after each page directory update.
 */

static void kbase_mmu_sync_pgd(struct kbase_device *kbdev,
		dma_addr_t handle, size_t size)
{
	/* If page table is not coherent then ensure the gpu can read
	 * the pages from memory
	 */
	if (kbdev->system_coherency != COHERENCY_ACE)
		dma_sync_single_for_device(kbdev->dev, handle, size,
				DMA_TO_DEVICE);
}

/*
 * Definitions:
 * - PGD: Page Directory.
 * - PTE: Page Table Entry. A 64bit value pointing to the next
 *        level of translation
 * - ATE: Address Transation Entry. A 64bit value pointing to
 *        a 4kB physical page.
 */

static void kbase_mmu_report_fault_and_kill(struct kbase_context *kctx,
		struct kbase_as *as, const char *reason_str);


/**
 * reg_grow_calc_extra_pages() - Calculate the number of backed pages to add to
 *                               a region on a GPU page fault
 *
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
static size_t reg_grow_calc_extra_pages(struct kbase_va_region *reg, size_t fault_rel_pfn)
{
	size_t multiple = reg->extent;
	size_t reg_current_size = kbase_reg_current_backed_size(reg);
	size_t minimum_extra = fault_rel_pfn - reg_current_size + 1;
	size_t remainder;

	if (!multiple) {
		dev_warn(reg->kctx->kbdev->dev,
				"VA Region 0x%llx extent was 0, allocator needs to set this properly for KBASE_REG_PF_GROW\n",
				((unsigned long long)reg->start_pfn) << PAGE_SHIFT);
		return minimum_extra;
	}

	/* Calculate the remainder to subtract from minimum_extra to make it
	 * the desired (rounded down) multiple of the extent.
	 * Depending on reg's flags, the base used for calculating multiples is
	 * different */
	if (reg->flags & KBASE_REG_TILER_ALIGN_TOP) {
		/* multiple is based from the top of the initial commit, which
		 * has been allocated in such a way that (start_pfn +
		 * initial_commit) is already aligned to multiple. Hence the
		 * pfn for the end of committed memory will also be aligned to
		 * multiple */
		size_t initial_commit = reg->initial_commit;

		if (fault_rel_pfn < initial_commit) {
			/* this case is just to catch in case it's been
			 * recommitted by userspace to be smaller than the
			 * initial commit */
			minimum_extra = initial_commit - reg_current_size;
			remainder = 0;
		} else {
			/* same as calculating (fault_rel_pfn - initial_commit + 1) */
			size_t pages_after_initial = minimum_extra + reg_current_size - initial_commit;

			remainder = pages_after_initial % multiple;
		}
	} else {
		/* multiple is based from the current backed size, even if the
		 * current backed size/pfn for end of committed memory are not
		 * themselves aligned to multiple */
		remainder = minimum_extra % multiple;
	}

	if (remainder == 0)
		return minimum_extra;

	return minimum_extra + multiple - remainder;
}

#ifdef CONFIG_MALI_JOB_DUMP
static void kbase_gpu_mmu_handle_write_faulting_as(struct kbase_context *kctx,
				struct kbase_device *kbdev,
				struct kbase_as *faulting_as,
				u64 start_pfn, size_t nr, u32 op)
{
	mutex_lock(&kbdev->mmu_hw_mutex);

	kbase_mmu_hw_clear_fault(kbdev, faulting_as, kctx,
			KBASE_MMU_FAULT_TYPE_PAGE);
	kbase_mmu_hw_do_operation(kbdev, faulting_as, kctx, start_pfn,
			nr, op, 1);

	mutex_unlock(&kbdev->mmu_hw_mutex);

	kbase_mmu_hw_enable_fault(kbdev, faulting_as, kctx,
			KBASE_MMU_FAULT_TYPE_PAGE);
}

static void kbase_gpu_mmu_handle_write_fault(struct kbase_context *kctx,
			struct kbase_as *faulting_as)
{
	struct kbasep_gwt_list_element *pos;
	struct kbase_va_region *region;
	struct kbase_device *kbdev;
	u64 fault_pfn, pfn_offset;
	u32 op;
	int ret;
	int as_no;

	as_no = faulting_as->number;
	kbdev = container_of(faulting_as, struct kbase_device, as[as_no]);
	fault_pfn = faulting_as->fault_addr >> PAGE_SHIFT;

	kbase_gpu_vm_lock(kctx);

	/* Find region and check if it should be writable. */
	region = kbase_region_tracker_find_region_enclosing_address(kctx,
			faulting_as->fault_addr);
	if (!region || region->flags & KBASE_REG_FREE) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Memory is not mapped on the GPU");
		return;
	}

	if (!(region->flags & KBASE_REG_GPU_WR)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Region does not have write permissions");
		return;
	}

	/* Capture addresses of faulting write location
	 * for job dumping if write tracking is enabled.
	 */
	if (kctx->gwt_enabled) {
		u64 page_addr = faulting_as->fault_addr & PAGE_MASK;
		bool found = false;
		/* Check if this write was already handled. */
		list_for_each_entry(pos, &kctx->gwt_current_list, link) {
			if (page_addr == pos->page_addr) {
				found = true;
				break;
			}
		}

		if (!found) {
			pos = kmalloc(sizeof(*pos), GFP_KERNEL);
			if (pos) {
				pos->region = region;
				pos->page_addr = page_addr;
				pos->num_pages = 1;
				list_add(&pos->link, &kctx->gwt_current_list);
			} else {
				dev_warn(kbdev->dev, "kmalloc failure");
			}
		}
	}

	pfn_offset = fault_pfn - region->start_pfn;
	/* Now make this faulting page writable to GPU. */
	ret = kbase_mmu_update_pages_no_flush(kctx, fault_pfn,
				&kbase_get_gpu_phy_pages(region)[pfn_offset],
				1, region->flags);

	/* flush L2 and unlock the VA (resumes the MMU) */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_6367))
		op = AS_COMMAND_FLUSH;
	else
		op = AS_COMMAND_FLUSH_PT;

	kbase_gpu_mmu_handle_write_faulting_as(kctx, kbdev, faulting_as,
			fault_pfn, 1, op);

	kbase_gpu_vm_unlock(kctx);
}

static void kbase_gpu_mmu_handle_permission_fault(struct kbase_context *kctx,
			struct kbase_as	*faulting_as)
{
	u32 fault_status;

	fault_status = faulting_as->fault_status;

	switch (fault_status & AS_FAULTSTATUS_ACCESS_TYPE_MASK) {
	case AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC:
	case AS_FAULTSTATUS_ACCESS_TYPE_WRITE:
		kbase_gpu_mmu_handle_write_fault(kctx, faulting_as);
		break;
	case AS_FAULTSTATUS_ACCESS_TYPE_EX:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Execute Permission fault");
		break;
	case AS_FAULTSTATUS_ACCESS_TYPE_READ:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Read Permission fault");
		break;
	default:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Unknown Permission fault");
		break;
	}
}
#endif

void page_fault_worker(struct work_struct *data)
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
	int err;
	bool grown = false;
	size_t min_pool_size;
	struct kbase_mem_pool *pool;
	int pages_to_grow;
	struct tagged_addr *gpu_pages, *cpu_pages;
	struct kbase_sub_alloc *prealloc_sas[2] = { NULL, NULL };
	int i;

	faulting_as = container_of(data, struct kbase_as, work_pagefault);
	fault_pfn = faulting_as->fault_addr >> PAGE_SHIFT;
	as_no = faulting_as->number;

	kbdev = container_of(faulting_as, struct kbase_device, as[as_no]);

	/* Grab the context that was already refcounted in kbase_mmu_interrupt().
	 * Therefore, it cannot be scheduled out of this AS until we explicitly release it
	 */
	kctx = kbasep_js_runpool_lookup_ctx_noretain(kbdev, as_no);
	if (WARN_ON(!kctx)) {
		atomic_dec(&kbdev->faults_pending);
		return;
	}

	KBASE_DEBUG_ASSERT(kctx->kbdev == kbdev);

	if (unlikely(faulting_as->protected_mode)) {
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Protected mode fault");
		kbase_mmu_hw_clear_fault(kbdev, faulting_as, kctx,
				KBASE_MMU_FAULT_TYPE_PAGE);

		goto fault_done;
	}

	fault_status = faulting_as->fault_status;
	switch (fault_status & AS_FAULTSTATUS_EXCEPTION_CODE_MASK) {

	case AS_FAULTSTATUS_EXCEPTION_CODE_TRANSLATION_FAULT:
		/* need to check against the region to handle this one */
		break;

	case AS_FAULTSTATUS_EXCEPTION_CODE_PERMISSION_FAULT:
#ifdef CONFIG_MALI_JOB_DUMP
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
				"Permission failure");
		goto fault_done;

	case AS_FAULTSTATUS_EXCEPTION_CODE_TRANSTAB_BUS_FAULT:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Translation table bus fault");
		goto fault_done;

	case AS_FAULTSTATUS_EXCEPTION_CODE_ACCESS_FLAG:
		/* nothing to do, but we don't expect this fault currently */
		dev_warn(kbdev->dev, "Access flag unexpectedly set");
		goto fault_done;

	case AS_FAULTSTATUS_EXCEPTION_CODE_ADDRESS_SIZE_FAULT:
		if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU))
			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
					"Address size fault");
		else
			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
					"Unknown fault code");
		goto fault_done;

	case AS_FAULTSTATUS_EXCEPTION_CODE_MEMORY_ATTRIBUTES_FAULT:
		if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU))
			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
					"Memory attributes fault");
		else
			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
					"Unknown fault code");
		goto fault_done;

	default:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Unknown fault code");
		goto fault_done;
	}

page_fault_retry:
#ifdef CONFIG_MALI_2MB_ALLOC
	/* Preallocate memory for the sub-allocation structs if necessary */
	for (i = 0; i != ARRAY_SIZE(prealloc_sas); ++i) {
		if (!prealloc_sas[i]) {
			prealloc_sas[i] = kmalloc(sizeof(*prealloc_sas[i]),
					GFP_KERNEL);
			if (!prealloc_sas[i]) {
				kbase_mmu_report_fault_and_kill(
						kctx, faulting_as,
						"Failed pre-allocating memory for sub-allocations' metadata");
				goto fault_done;
			}
		}
	}
#endif /* CONFIG_MALI_2MB_ALLOC */

	/* so we have a translation fault, let's see if it is for growable
	 * memory */
	kbase_gpu_vm_lock(kctx);

	region = kbase_region_tracker_find_region_enclosing_address(kctx,
			faulting_as->fault_addr);
	if (!region || region->flags & KBASE_REG_FREE) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Memory is not mapped on the GPU");
		goto fault_done;
	}

	if (region->gpu_alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"DMA-BUF is not mapped on the GPU");
		goto fault_done;
	}

	if ((region->flags & GROWABLE_FLAGS_REQUIRED)
			!= GROWABLE_FLAGS_REQUIRED) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Memory is not growable");
		goto fault_done;
	}

	if ((region->flags & KBASE_REG_DONT_NEED)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Don't need memory can't be grown");
		goto fault_done;
	}

	/* find the size we need to grow it by */
	/* we know the result fit in a size_t due to kbase_region_tracker_find_region_enclosing_address
	 * validating the fault_adress to be within a size_t from the start_pfn */
	fault_rel_pfn = fault_pfn - region->start_pfn;

	if (fault_rel_pfn < kbase_reg_current_backed_size(region)) {
		dev_dbg(kbdev->dev, "Page fault @ 0x%llx in allocated region 0x%llx-0x%llx of growable TMEM: Ignoring",
				faulting_as->fault_addr, region->start_pfn,
				region->start_pfn +
				kbase_reg_current_backed_size(region));

		mutex_lock(&kbdev->mmu_hw_mutex);

		kbase_mmu_hw_clear_fault(kbdev, faulting_as, kctx,
				KBASE_MMU_FAULT_TYPE_PAGE);
		/* [1] in case another page fault occurred while we were
		 * handling the (duplicate) page fault we need to ensure we
		 * don't loose the other page fault as result of us clearing
		 * the MMU IRQ. Therefore, after we clear the MMU IRQ we send
		 * an UNLOCK command that will retry any stalled memory
		 * transaction (which should cause the other page fault to be
		 * raised again).
		 */
		kbase_mmu_hw_do_operation(kbdev, faulting_as, NULL, 0, 0,
				AS_COMMAND_UNLOCK, 1);

		mutex_unlock(&kbdev->mmu_hw_mutex);

		kbase_mmu_hw_enable_fault(kbdev, faulting_as, kctx,
				KBASE_MMU_FAULT_TYPE_PAGE);
		kbase_gpu_vm_unlock(kctx);

		goto fault_done;
	}

	new_pages = reg_grow_calc_extra_pages(region, fault_rel_pfn);

	/* cap to max vsize */
	new_pages = min(new_pages, region->nr_pages - kbase_reg_current_backed_size(region));

	if (0 == new_pages) {
		mutex_lock(&kbdev->mmu_hw_mutex);

		/* Duplicate of a fault we've already handled, nothing to do */
		kbase_mmu_hw_clear_fault(kbdev, faulting_as, kctx,
				KBASE_MMU_FAULT_TYPE_PAGE);
		/* See comment [1] about UNLOCK usage */
		kbase_mmu_hw_do_operation(kbdev, faulting_as, NULL, 0, 0,
				AS_COMMAND_UNLOCK, 1);

		mutex_unlock(&kbdev->mmu_hw_mutex);

		kbase_mmu_hw_enable_fault(kbdev, faulting_as, kctx,
				KBASE_MMU_FAULT_TYPE_PAGE);
		kbase_gpu_vm_unlock(kctx);
		goto fault_done;
	}

#ifdef CONFIG_MALI_2MB_ALLOC
	if (new_pages >= (SZ_2M / SZ_4K)) {
		pool = &kctx->lp_mem_pool;
		/* Round up to number of 2 MB pages required */
		min_pool_size = new_pages + ((SZ_2M / SZ_4K) - 1);
		min_pool_size /= (SZ_2M / SZ_4K);
	} else {
#endif
		pool = &kctx->mem_pool;
		min_pool_size = new_pages;
#ifdef CONFIG_MALI_2MB_ALLOC
	}
#endif

	if (region->gpu_alloc != region->cpu_alloc)
		min_pool_size *= 2;

	pages_to_grow = 0;

	mutex_lock(&kctx->mem_partials_lock);
	kbase_mem_pool_lock(pool);
	/* We can not allocate memory from the kernel with the vm_lock held, so
	 * check that there is enough memory in the pool. If not then calculate
	 * how much it has to grow by, grow the pool when the vm_lock is
	 * dropped, and retry the allocation.
	 */
	if (kbase_mem_pool_size(pool) >= min_pool_size) {
		gpu_pages = kbase_alloc_phy_pages_helper_locked(
				region->gpu_alloc, pool, new_pages,
				&prealloc_sas[0]);

		if (gpu_pages) {
			if (region->gpu_alloc != region->cpu_alloc) {
				cpu_pages = kbase_alloc_phy_pages_helper_locked(
						region->cpu_alloc, pool,
						new_pages, &prealloc_sas[1]);

				if (cpu_pages) {
					grown = true;
				} else {
					kbase_free_phy_pages_helper_locked(
							region->gpu_alloc,
							pool, gpu_pages,
							new_pages);
				}
			} else {
				grown = true;
			}
		}
	} else {
		pages_to_grow = min_pool_size - kbase_mem_pool_size(pool);
	}
	kbase_mem_pool_unlock(pool);
	mutex_unlock(&kctx->mem_partials_lock);

	if (grown) {
		u64 pfn_offset;
		u32 op;

		/* alloc success */
		KBASE_DEBUG_ASSERT(kbase_reg_current_backed_size(region) <= region->nr_pages);

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
		err = kbase_mmu_insert_pages_no_flush(kctx,
				region->start_pfn + pfn_offset,
				&kbase_get_gpu_phy_pages(region)[pfn_offset],
				new_pages, region->flags);
		if (err) {
			kbase_free_phy_pages_helper(region->gpu_alloc, new_pages);
			if (region->gpu_alloc != region->cpu_alloc)
				kbase_free_phy_pages_helper(region->cpu_alloc,
						new_pages);
			kbase_gpu_vm_unlock(kctx);
			/* The locked VA region will be unlocked and the cache invalidated in here */
			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
					"Page table update failure");
			goto fault_done;
		}
#if defined(CONFIG_MALI_BIFROST_GATOR_SUPPORT)
		kbase_trace_mali_page_fault_insert_pages(as_no, new_pages);
#endif
		KBASE_TLSTREAM_AUX_PAGEFAULT(kctx->id, (u64)new_pages);

		/* AS transaction begin */
		mutex_lock(&kbdev->mmu_hw_mutex);

		/* flush L2 and unlock the VA (resumes the MMU) */
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_6367))
			op = AS_COMMAND_FLUSH;
		else
			op = AS_COMMAND_FLUSH_PT;

		/* clear MMU interrupt - this needs to be done after updating
		 * the page tables but before issuing a FLUSH command. The
		 * FLUSH cmd has a side effect that it restarts stalled memory
		 * transactions in other address spaces which may cause
		 * another fault to occur. If we didn't clear the interrupt at
		 * this stage a new IRQ might not be raised when the GPU finds
		 * a MMU IRQ is already pending.
		 */
		kbase_mmu_hw_clear_fault(kbdev, faulting_as, kctx,
					 KBASE_MMU_FAULT_TYPE_PAGE);

		kbase_mmu_hw_do_operation(kbdev, faulting_as, kctx,
					  faulting_as->fault_addr >> PAGE_SHIFT,
					  new_pages,
					  op, 1);

		mutex_unlock(&kbdev->mmu_hw_mutex);
		/* AS transaction end */

		/* reenable this in the mask */
		kbase_mmu_hw_enable_fault(kbdev, faulting_as, kctx,
					 KBASE_MMU_FAULT_TYPE_PAGE);

#ifdef CONFIG_MALI_JOB_DUMP
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
		kbase_gpu_vm_unlock(kctx);
	} else {
		int ret = -ENOMEM;

		kbase_gpu_vm_unlock(kctx);

		/* If the memory pool was insufficient then grow it and retry.
		 * Otherwise fail the allocation.
		 */
		if (pages_to_grow > 0)
			ret = kbase_mem_pool_grow(pool, pages_to_grow);

		if (ret < 0) {
			/* failed to extend, handle as a normal PF */
			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
					"Page allocation failure");
		} else {
			goto page_fault_retry;
		}
	}

fault_done:
	for (i = 0; i != ARRAY_SIZE(prealloc_sas); ++i)
		kfree(prealloc_sas[i]);

	/*
	 * By this point, the fault was handled in some way,
	 * so release the ctx refcount
	 */
	kbasep_js_runpool_release_ctx(kbdev, kctx);

	atomic_dec(&kbdev->faults_pending);
}

phys_addr_t kbase_mmu_alloc_pgd(struct kbase_context *kctx)
{
	u64 *page;
	int i;
	struct page *p;
	int new_page_count __maybe_unused;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	new_page_count = kbase_atomic_add_pages(1, &kctx->used_pages);
	kbase_atomic_add_pages(1, &kctx->kbdev->memdev.used_pages);

	p = kbase_mem_pool_alloc(&kctx->mem_pool);
	if (!p)
		goto sub_pages;

	KBASE_TLSTREAM_AUX_PAGESALLOC(
			kctx->id,
			(u64)new_page_count);

	page = kmap(p);
	if (NULL == page)
		goto alloc_free;

	kbase_process_page_usage_inc(kctx, 1);

	for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++)
		kctx->kbdev->mmu_mode->entry_invalidate(&page[i]);

	kbase_mmu_sync_pgd(kctx->kbdev, kbase_dma_addr(p), PAGE_SIZE);

	kunmap(p);
	return page_to_phys(p);

alloc_free:
	kbase_mem_pool_free(&kctx->mem_pool, p, false);
sub_pages:
	kbase_atomic_sub_pages(1, &kctx->used_pages);
	kbase_atomic_sub_pages(1, &kctx->kbdev->memdev.used_pages);

	return 0;
}

KBASE_EXPORT_TEST_API(kbase_mmu_alloc_pgd);

/* Given PGD PFN for level N, return PGD PFN for level N+1, allocating the
 * new table from the pool if needed and possible
 */
static int mmu_get_next_pgd(struct kbase_context *kctx,
		phys_addr_t *pgd, u64 vpfn, int level)
{
	u64 *page;
	phys_addr_t target_pgd;
	struct page *p;

	KBASE_DEBUG_ASSERT(*pgd);
	KBASE_DEBUG_ASSERT(NULL != kctx);

	lockdep_assert_held(&kctx->mmu_lock);

	/*
	 * Architecture spec defines level-0 as being the top-most.
	 * This is a bit unfortunate here, but we keep the same convention.
	 */
	vpfn >>= (3 - level) * 9;
	vpfn &= 0x1FF;

	p = pfn_to_page(PFN_DOWN(*pgd));
	page = kmap(p);
	if (NULL == page) {
		dev_warn(kctx->kbdev->dev, "mmu_get_next_pgd: kmap failure\n");
		return -EINVAL;
	}

	target_pgd = kctx->kbdev->mmu_mode->pte_to_phy_addr(page[vpfn]);

	if (!target_pgd) {
		target_pgd = kbase_mmu_alloc_pgd(kctx);
		if (!target_pgd) {
			dev_dbg(kctx->kbdev->dev, "mmu_get_next_pgd: kbase_mmu_alloc_pgd failure\n");
			kunmap(p);
			return -ENOMEM;
		}

		kctx->kbdev->mmu_mode->entry_set_pte(&page[vpfn], target_pgd);

		kbase_mmu_sync_pgd(kctx->kbdev, kbase_dma_addr(p), PAGE_SIZE);
		/* Rely on the caller to update the address space flags. */
	}

	kunmap(p);
	*pgd = target_pgd;

	return 0;
}

/*
 * Returns the PGD for the specified level of translation
 */
static int mmu_get_pgd_at_level(struct kbase_context *kctx,
					u64 vpfn,
					unsigned int level,
					phys_addr_t *out_pgd)
{
	phys_addr_t pgd;
	int l;

	lockdep_assert_held(&kctx->mmu_lock);
	pgd = kctx->pgd;

	for (l = MIDGARD_MMU_TOPLEVEL; l < level; l++) {
		int err = mmu_get_next_pgd(kctx, &pgd, vpfn, l);
		/* Handle failure condition */
		if (err) {
			dev_dbg(kctx->kbdev->dev,
				 "%s: mmu_get_next_pgd failure at level %d\n",
				 __func__, l);
			return err;
		}
	}

	*out_pgd = pgd;

	return 0;
}

#define mmu_get_bottom_pgd(kctx, vpfn, out_pgd) \
	mmu_get_pgd_at_level((kctx), (vpfn), MIDGARD_MMU_BOTTOMLEVEL, (out_pgd))


static void mmu_insert_pages_failure_recovery(struct kbase_context *kctx,
					      u64 from_vpfn, u64 to_vpfn)
{
	phys_addr_t pgd;
	u64 vpfn = from_vpfn;
	struct kbase_mmu_mode const *mmu_mode;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(0 != vpfn);
	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(vpfn <= (U64_MAX / PAGE_SIZE));
	KBASE_DEBUG_ASSERT(from_vpfn <= to_vpfn);

	lockdep_assert_held(&kctx->mmu_lock);
	lockdep_assert_held(&kctx->reg_lock);

	mmu_mode = kctx->kbdev->mmu_mode;

	while (vpfn < to_vpfn) {
		unsigned int i;
		unsigned int idx = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - idx;
		unsigned int pcount = 0;
		unsigned int left = to_vpfn - vpfn;
		unsigned int level;
		u64 *page;

		if (count > left)
			count = left;

		/* need to check if this is a 2MB page or a 4kB */
		pgd = kctx->pgd;

		for (level = MIDGARD_MMU_TOPLEVEL;
				level <= MIDGARD_MMU_BOTTOMLEVEL; level++) {
			idx = (vpfn >> ((3 - level) * 9)) & 0x1FF;
			page = kmap(phys_to_page(pgd));
			if (mmu_mode->ate_is_valid(page[idx], level))
				break; /* keep the mapping */
			kunmap(phys_to_page(pgd));
			pgd = mmu_mode->pte_to_phy_addr(page[idx]);
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
			dev_warn(kctx->kbdev->dev, "%sNo support for ATEs at level %d\n",
			       __func__, level);
			goto next;
		}

		/* Invalidate the entries we added */
		for (i = 0; i < pcount; i++)
			mmu_mode->entry_invalidate(&page[idx + i]);

		kbase_mmu_sync_pgd(kctx->kbdev,
				   kbase_dma_addr(phys_to_page(pgd)) + 8 * idx,
				   8 * pcount);
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
					unsigned long flags)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	/* In case the insert_single_page only partially completes we need to be
	 * able to recover */
	bool recover_required = false;
	u64 recover_vpfn = vpfn;
	size_t recover_count = 0;
	size_t remain = nr;
	int err;
	struct kbase_mmu_mode const *mmu_mode;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(0 != vpfn);
	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(vpfn <= (U64_MAX / PAGE_SIZE));

	mmu_mode = kctx->kbdev->mmu_mode;

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	mutex_lock(&kctx->mmu_lock);

	while (remain) {
		unsigned int i;
		unsigned int index = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - index;
		struct page *p;

		if (count > remain)
			count = remain;

		/*
		 * Repeatedly calling mmu_get_bottom_pte() is clearly
		 * suboptimal. We don't have to re-parse the whole tree
		 * each time (just cache the l0-l2 sequence).
		 * On the other hand, it's only a gain when we map more than
		 * 256 pages at once (on average). Do we really care?
		 */
		do {
			err = mmu_get_bottom_pgd(kctx, vpfn, &pgd);
			if (err != -ENOMEM)
				break;
			/* Fill the memory pool with enough pages for
			 * the page walk to succeed
			 */
			mutex_unlock(&kctx->mmu_lock);
			err = kbase_mem_pool_grow(&kctx->mem_pool,
					MIDGARD_MMU_BOTTOMLEVEL);
			mutex_lock(&kctx->mmu_lock);
		} while (!err);
		if (err) {
			dev_warn(kctx->kbdev->dev, "kbase_mmu_insert_pages: mmu_get_bottom_pgd failure\n");
			if (recover_required) {
				/* Invalidate the pages we have partially
				 * completed */
				mmu_insert_pages_failure_recovery(kctx,
								  recover_vpfn,
								  recover_vpfn +
								  recover_count
								  );
			}
			goto fail_unlock;
		}

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap(p);
		if (!pgd_page) {
			dev_warn(kctx->kbdev->dev, "kbase_mmu_insert_pages: kmap failure\n");
			if (recover_required) {
				/* Invalidate the pages we have partially
				 * completed */
				mmu_insert_pages_failure_recovery(kctx,
								  recover_vpfn,
								  recover_vpfn +
								  recover_count
								  );
			}
			err = -ENOMEM;
			goto fail_unlock;
		}

		for (i = 0; i < count; i++) {
			unsigned int ofs = index + i;

			/* Fail if the current page is a valid ATE entry */
			KBASE_DEBUG_ASSERT(0 == (pgd_page[ofs] & 1UL));

			mmu_mode->entry_set_ate(&pgd_page[ofs],
						phys, flags,
						MIDGARD_MMU_BOTTOMLEVEL);
		}

		vpfn += count;
		remain -= count;

		kbase_mmu_sync_pgd(kctx->kbdev,
				kbase_dma_addr(p) + (index * sizeof(u64)),
				count * sizeof(u64));

		kunmap(p);
		/* We have started modifying the page table.
		 * If further pages need inserting and fail we need to undo what
		 * has already taken place */
		recover_required = true;
		recover_count += count;
	}
	mutex_unlock(&kctx->mmu_lock);
	kbase_mmu_flush_invalidate(kctx, vpfn, nr, false);
	return 0;

fail_unlock:
	mutex_unlock(&kctx->mmu_lock);
	kbase_mmu_flush_invalidate(kctx, vpfn, nr, false);
	return err;
}

static inline void cleanup_empty_pte(struct kbase_context *kctx, u64 *pte)
{
	phys_addr_t tmp_pgd;
	struct page *tmp_p;

	tmp_pgd = kctx->kbdev->mmu_mode->pte_to_phy_addr(*pte);
	tmp_p = phys_to_page(tmp_pgd);
	kbase_mem_pool_free(&kctx->mem_pool, tmp_p, false);
	kbase_process_page_usage_dec(kctx, 1);
	kbase_atomic_sub_pages(1, &kctx->used_pages);
	kbase_atomic_sub_pages(1, &kctx->kbdev->memdev.used_pages);
}

int kbase_mmu_insert_pages_no_flush(struct kbase_context *kctx,
				    const u64 start_vpfn,
				    struct tagged_addr *phys, size_t nr,
				    unsigned long flags)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	u64 insert_vpfn = start_vpfn;
	size_t remain = nr;
	int err;
	struct kbase_mmu_mode const *mmu_mode;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(start_vpfn);
	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(start_vpfn <= (U64_MAX / PAGE_SIZE));

	mmu_mode = kctx->kbdev->mmu_mode;

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	mutex_lock(&kctx->mmu_lock);

	while (remain) {
		unsigned int i;
		unsigned int vindex = insert_vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - vindex;
		struct page *p;
		unsigned int cur_level;

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
			err = mmu_get_pgd_at_level(kctx, insert_vpfn, cur_level,
						   &pgd);
			if (err != -ENOMEM)
				break;
			/* Fill the memory pool with enough pages for
			 * the page walk to succeed
			 */
			mutex_unlock(&kctx->mmu_lock);
			err = kbase_mem_pool_grow(&kctx->mem_pool,
					cur_level);
			mutex_lock(&kctx->mmu_lock);
		} while (!err);

		if (err) {
			dev_warn(kctx->kbdev->dev,
				 "%s: mmu_get_bottom_pgd failure\n", __func__);
			if (insert_vpfn != start_vpfn) {
				/* Invalidate the pages we have partially
				 * completed */
				mmu_insert_pages_failure_recovery(kctx,
								  start_vpfn,
								  insert_vpfn);
			}
			goto fail_unlock;
		}

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap(p);
		if (!pgd_page) {
			dev_warn(kctx->kbdev->dev, "%s: kmap failure\n",
				 __func__);
			if (insert_vpfn != start_vpfn) {
				/* Invalidate the pages we have partially
				 * completed */
				mmu_insert_pages_failure_recovery(kctx,
								  start_vpfn,
								  insert_vpfn);
			}
			err = -ENOMEM;
			goto fail_unlock;
		}

		if (cur_level == MIDGARD_MMU_LEVEL(2)) {
			unsigned int level_index = (insert_vpfn >> 9) & 0x1FF;
			u64 *target = &pgd_page[level_index];

			if (mmu_mode->pte_is_valid(*target, cur_level))
				cleanup_empty_pte(kctx, target);
			mmu_mode->entry_set_ate(target, *phys, flags,
						cur_level);
		} else {
			for (i = 0; i < count; i++) {
				unsigned int ofs = vindex + i;
				u64 *target = &pgd_page[ofs];

				/* Fail if the current page is a valid ATE entry
				 * unless gwt_was_enabled as in that case all
				 * pages will be valid from when
				 * kbase_gpu_gwt_start() cleared the gpu
				 * write flag.
				 */
#ifdef CONFIG_MALI_JOB_DUMP
				if (!kctx->gwt_was_enabled)
#endif
					KBASE_DEBUG_ASSERT
						(0 == (*target & 1UL));
				kctx->kbdev->mmu_mode->entry_set_ate(target,
						phys[i], flags, cur_level);
			}
		}

		phys += count;
		insert_vpfn += count;
		remain -= count;

		kbase_mmu_sync_pgd(kctx->kbdev,
				kbase_dma_addr(p) + (vindex * sizeof(u64)),
				count * sizeof(u64));

		kunmap(p);
	}

	mutex_unlock(&kctx->mmu_lock);
	return 0;

fail_unlock:
	mutex_unlock(&kctx->mmu_lock);
	return err;
}

/*
 * Map 'nr' pages pointed to by 'phys' at GPU PFN 'vpfn'
 */
int kbase_mmu_insert_pages(struct kbase_context *kctx, u64 vpfn,
				  struct tagged_addr *phys, size_t nr,
				  unsigned long flags)
{
	int err;

	err = kbase_mmu_insert_pages_no_flush(kctx, vpfn, phys, nr, flags);
	kbase_mmu_flush_invalidate(kctx, vpfn, nr, false);
	return err;
}

KBASE_EXPORT_TEST_API(kbase_mmu_insert_pages);

/**
 * kbase_mmu_flush_invalidate_noretain() - Flush and invalidate the GPU caches
 * without retaining the kbase context.
 * @kctx: The KBase context.
 * @vpfn: The virtual page frame number to start the flush on.
 * @nr: The number of pages to flush.
 * @sync: Set if the operation should be synchronous or not.
 *
 * As per kbase_mmu_flush_invalidate but doesn't retain the kctx or do any
 * other locking.
 */
static void kbase_mmu_flush_invalidate_noretain(struct kbase_context *kctx,
		u64 vpfn, size_t nr, bool sync)
{
	struct kbase_device *kbdev = kctx->kbdev;
	int err;
	u32 op;

	/* Early out if there is nothing to do */
	if (nr == 0)
		return;

	if (sync)
		op = AS_COMMAND_FLUSH_MEM;
	else
		op = AS_COMMAND_FLUSH_PT;

	err = kbase_mmu_hw_do_operation(kbdev,
				&kbdev->as[kctx->as_nr],
				kctx, vpfn, nr, op, 0);
#if KBASE_GPU_RESET_EN
	if (err) {
		/* Flush failed to complete, assume the
		 * GPU has hung and perform a reset to
		 * recover */
		dev_err(kbdev->dev, "Flush for GPU page table update did not complete. Issuing GPU soft-reset to recover\n");

		if (kbase_prepare_to_reset_gpu_locked(kbdev))
			kbase_reset_gpu_locked(kbdev);
	}
#endif /* KBASE_GPU_RESET_EN */

#ifndef CONFIG_MALI_BIFROST_NO_MALI
	/*
	 * As this function could be called in interrupt context the sync
	 * request can't block. Instead log the request and the next flush
	 * request will pick it up.
	 */
	if ((!err) && sync &&
			kbase_hw_has_issue(kctx->kbdev, BASE_HW_ISSUE_6367))
		atomic_set(&kctx->drain_pending, 1);
#endif /* !CONFIG_MALI_BIFROST_NO_MALI */
}

static void kbase_mmu_flush_invalidate(struct kbase_context *kctx,
		u64 vpfn, size_t nr, bool sync)
{
	struct kbase_device *kbdev;
	bool ctx_is_in_runpool;
#ifndef CONFIG_MALI_BIFROST_NO_MALI
	bool drain_pending = false;

	if (atomic_xchg(&kctx->drain_pending, 0))
		drain_pending = true;
#endif /* !CONFIG_MALI_BIFROST_NO_MALI */

	/* Early out if there is nothing to do */
	if (nr == 0)
		return;

	kbdev = kctx->kbdev;
	mutex_lock(&kbdev->js_data.queue_mutex);
	ctx_is_in_runpool = kbasep_js_runpool_retain_ctx(kbdev, kctx);
	mutex_unlock(&kbdev->js_data.queue_mutex);

	if (ctx_is_in_runpool) {
		KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

		if (!kbase_pm_context_active_handle_suspend(kbdev,
			KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE)) {
			int err;
			u32 op;

			/* AS transaction begin */
			mutex_lock(&kbdev->mmu_hw_mutex);

			if (sync)
				op = AS_COMMAND_FLUSH_MEM;
			else
				op = AS_COMMAND_FLUSH_PT;

			err = kbase_mmu_hw_do_operation(kbdev,
						&kbdev->as[kctx->as_nr],
						kctx, vpfn, nr, op, 0);

#if KBASE_GPU_RESET_EN
			if (err) {
				/* Flush failed to complete, assume the
				 * GPU has hung and perform a reset to
				 * recover */
				dev_err(kbdev->dev, "Flush for GPU page table update did not complete. Issueing GPU soft-reset to recover\n");

				if (kbase_prepare_to_reset_gpu(kbdev))
					kbase_reset_gpu(kbdev);
			}
#endif /* KBASE_GPU_RESET_EN */

			mutex_unlock(&kbdev->mmu_hw_mutex);
			/* AS transaction end */

#ifndef CONFIG_MALI_BIFROST_NO_MALI
			/*
			 * The transaction lock must be dropped before here
			 * as kbase_wait_write_flush could take it if
			 * the GPU was powered down (static analysis doesn't
			 * know this can't happen).
			 */
			drain_pending |= (!err) && sync &&
					kbase_hw_has_issue(kctx->kbdev,
							BASE_HW_ISSUE_6367);
			if (drain_pending) {
				/* Wait for GPU to flush write buffer */
				kbase_wait_write_flush(kctx);
			}
#endif /* !CONFIG_MALI_BIFROST_NO_MALI */

			kbase_pm_context_idle(kbdev);
		}
		kbasep_js_runpool_release_ctx(kbdev, kctx);
	}
}

void kbase_mmu_update(struct kbase_context *kctx)
{
	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);
	lockdep_assert_held(&kctx->kbdev->mmu_hw_mutex);
	/* ASSERT that the context has a valid as_nr, which is only the case
	 * when it's scheduled in.
	 *
	 * as_nr won't change because the caller has the hwaccess_lock */
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

	kctx->kbdev->mmu_mode->update(kctx);
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
	 * as_nr won't change because the caller has the hwaccess_lock */
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	/*
	 * The address space is being disabled, drain all knowledge of it out
	 * from the caches as pages and page tables might be freed after this.
	 *
	 * The job scheduler code will already be holding the locks and context
	 * so just do the flush.
	 */
	kbase_mmu_flush_invalidate_noretain(kctx, 0, ~0, true);

	kctx->kbdev->mmu_mode->disable_as(kctx->kbdev, kctx->as_nr);
}
KBASE_EXPORT_TEST_API(kbase_mmu_disable);

/*
 * We actually only discard the ATE, and not the page table
 * pages. There is a potential DoS here, as we'll leak memory by
 * having PTEs that are potentially unused.  Will require physical
 * page accounting, so MMU pages are part of the process allocation.
 *
 * IMPORTANT: This uses kbasep_js_runpool_release_ctx() when the context is
 * currently scheduled into the runpool, and so potentially uses a lot of locks.
 * These locks must be taken in the correct order with respect to others
 * already held by the caller. Refer to kbasep_js_runpool_release_ctx() for more
 * information.
 */
int kbase_mmu_teardown_pages(struct kbase_context *kctx, u64 vpfn, size_t nr)
{
	phys_addr_t pgd;
	size_t requested_nr = nr;
	struct kbase_mmu_mode const *mmu_mode;
	int err = -EFAULT;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	beenthere(kctx, "kctx %p vpfn %lx nr %zd", (void *)kctx, (unsigned long)vpfn, nr);

	if (0 == nr) {
		/* early out if nothing to do */
		return 0;
	}

	mutex_lock(&kctx->mmu_lock);

	mmu_mode = kctx->kbdev->mmu_mode;

	while (nr) {
		unsigned int i;
		unsigned int index = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - index;
		unsigned int pcount;
		unsigned int level;
		u64 *page;

		if (count > nr)
			count = nr;

		/* need to check if this is a 2MB or a 4kB page */
		pgd = kctx->pgd;

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
			next_pgd = mmu_mode->pte_to_phy_addr(page[index]);
			kunmap(phys_to_page(pgd));
			pgd = next_pgd;
		}

		switch (level) {
		case MIDGARD_MMU_LEVEL(0):
		case MIDGARD_MMU_LEVEL(1):
			dev_warn(kctx->kbdev->dev,
				 "%s: No support for ATEs at level %d\n",
				 __func__, level);
			kunmap(phys_to_page(pgd));
			goto out;
		case MIDGARD_MMU_LEVEL(2):
			/* can only teardown if count >= 512 */
			if (count >= 512) {
				pcount = 1;
			} else {
				dev_warn(kctx->kbdev->dev,
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
			dev_err(kctx->kbdev->dev,
				"%s: found non-mapped memory, early out\n",
				__func__);
			vpfn += count;
			nr -= count;
			continue;
		}

		/* Invalidate the entries we added */
		for (i = 0; i < pcount; i++)
			mmu_mode->entry_invalidate(&page[index + i]);

		kbase_mmu_sync_pgd(kctx->kbdev,
				   kbase_dma_addr(phys_to_page(pgd)) +
				   8 * index, 8*pcount);

next:
		kunmap(phys_to_page(pgd));
		vpfn += count;
		nr -= count;
	}
	err = 0;
out:
	mutex_unlock(&kctx->mmu_lock);
	kbase_mmu_flush_invalidate(kctx, vpfn, requested_nr, true);
	return err;
}

KBASE_EXPORT_TEST_API(kbase_mmu_teardown_pages);

/**
 * Update the entries for specified number of pages pointed to by 'phys' at GPU PFN 'vpfn'.
 * This call is being triggered as a response to the changes of the mem attributes
 *
 * @pre : The caller is responsible for validating the memory attributes
 *
 * IMPORTANT: This uses kbasep_js_runpool_release_ctx() when the context is
 * currently scheduled into the runpool, and so potentially uses a lot of locks.
 * These locks must be taken in the correct order with respect to others
 * already held by the caller. Refer to kbasep_js_runpool_release_ctx() for more
 * information.
 */
int kbase_mmu_update_pages_no_flush(struct kbase_context *kctx, u64 vpfn,
					struct tagged_addr *phys, size_t nr,
					unsigned long flags)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	struct kbase_mmu_mode const *mmu_mode;
	int err;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(0 != vpfn);
	KBASE_DEBUG_ASSERT(vpfn <= (U64_MAX / PAGE_SIZE));

	/* Early out if there is nothing to do */
	if (nr == 0)
		return 0;

	mutex_lock(&kctx->mmu_lock);

	mmu_mode = kctx->kbdev->mmu_mode;

	while (nr) {
		unsigned int i;
		unsigned int index = vpfn & 0x1FF;
		size_t count = KBASE_MMU_PAGE_ENTRIES - index;
		struct page *p;

		if (count > nr)
			count = nr;

		do {
			err = mmu_get_bottom_pgd(kctx, vpfn, &pgd);
			if (err != -ENOMEM)
				break;
			/* Fill the memory pool with enough pages for
			 * the page walk to succeed
			 */
			mutex_unlock(&kctx->mmu_lock);
			err = kbase_mem_pool_grow(&kctx->mem_pool,
					MIDGARD_MMU_BOTTOMLEVEL);
			mutex_lock(&kctx->mmu_lock);
		} while (!err);
		if (err) {
			dev_warn(kctx->kbdev->dev,
				 "mmu_get_bottom_pgd failure\n");
			goto fail_unlock;
		}

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap(p);
		if (!pgd_page) {
			dev_warn(kctx->kbdev->dev, "kmap failure\n");
			err = -ENOMEM;
			goto fail_unlock;
		}

		for (i = 0; i < count; i++)
			mmu_mode->entry_set_ate(&pgd_page[index + i], phys[i],
						flags, MIDGARD_MMU_BOTTOMLEVEL);

		phys += count;
		vpfn += count;
		nr -= count;

		kbase_mmu_sync_pgd(kctx->kbdev,
				kbase_dma_addr(p) + (index * sizeof(u64)),
				count * sizeof(u64));

		kunmap(pfn_to_page(PFN_DOWN(pgd)));
	}

	mutex_unlock(&kctx->mmu_lock);
	return 0;

fail_unlock:
	mutex_unlock(&kctx->mmu_lock);
	return err;
}

int kbase_mmu_update_pages(struct kbase_context *kctx, u64 vpfn,
			   struct tagged_addr *phys, size_t nr,
			   unsigned long flags)
{
	int err;

	err = kbase_mmu_update_pages_no_flush(kctx, vpfn, phys, nr, flags);
	kbase_mmu_flush_invalidate(kctx, vpfn, nr, true);
	return err;
}

static void mmu_teardown_level(struct kbase_context *kctx, phys_addr_t pgd,
			       int level, u64 *pgd_page_buffer)
{
	phys_addr_t target_pgd;
	struct page *p;
	u64 *pgd_page;
	int i;
	struct kbase_mmu_mode const *mmu_mode;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	lockdep_assert_held(&kctx->mmu_lock);
	lockdep_assert_held(&kctx->reg_lock);

	pgd_page = kmap_atomic(pfn_to_page(PFN_DOWN(pgd)));
	/* kmap_atomic should NEVER fail. */
	KBASE_DEBUG_ASSERT(NULL != pgd_page);
	/* Copy the page to our preallocated buffer so that we can minimize
	 * kmap_atomic usage */
	memcpy(pgd_page_buffer, pgd_page, PAGE_SIZE);
	kunmap_atomic(pgd_page);
	pgd_page = pgd_page_buffer;

	mmu_mode = kctx->kbdev->mmu_mode;

	for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++) {
		target_pgd = mmu_mode->pte_to_phy_addr(pgd_page[i]);

		if (target_pgd) {
			if (mmu_mode->pte_is_valid(pgd_page[i], level)) {
				mmu_teardown_level(kctx,
						   target_pgd,
						   level + 1,
						   pgd_page_buffer +
						   (PAGE_SIZE / sizeof(u64)));
			}
		}
	}

	p = pfn_to_page(PFN_DOWN(pgd));
	kbase_mem_pool_free(&kctx->mem_pool, p, true);
	kbase_process_page_usage_dec(kctx, 1);
	kbase_atomic_sub_pages(1, &kctx->used_pages);
	kbase_atomic_sub_pages(1, &kctx->kbdev->memdev.used_pages);
}

int kbase_mmu_init(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL == kctx->mmu_teardown_pages);

	mutex_init(&kctx->mmu_lock);

	/* Preallocate MMU depth of four pages for mmu_teardown_level to use */
	kctx->mmu_teardown_pages = kmalloc(PAGE_SIZE * 4, GFP_KERNEL);

	if (NULL == kctx->mmu_teardown_pages)
		return -ENOMEM;

	return 0;
}

void kbase_mmu_term(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != kctx->mmu_teardown_pages);

	kfree(kctx->mmu_teardown_pages);
	kctx->mmu_teardown_pages = NULL;
}

void kbase_mmu_free_pgd(struct kbase_context *kctx)
{
	int new_page_count = 0;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != kctx->mmu_teardown_pages);

	mutex_lock(&kctx->mmu_lock);
	mmu_teardown_level(kctx, kctx->pgd, MIDGARD_MMU_TOPLEVEL,
			   kctx->mmu_teardown_pages);
	mutex_unlock(&kctx->mmu_lock);

	KBASE_TLSTREAM_AUX_PAGESALLOC(
			kctx->id,
			(u64)new_page_count);
}

KBASE_EXPORT_TEST_API(kbase_mmu_free_pgd);

static size_t kbasep_mmu_dump_level(struct kbase_context *kctx, phys_addr_t pgd, int level, char ** const buffer, size_t *size_left)
{
	phys_addr_t target_pgd;
	u64 *pgd_page;
	int i;
	size_t size = KBASE_MMU_PAGE_ENTRIES * sizeof(u64) + sizeof(u64);
	size_t dump_size;
	struct kbase_mmu_mode const *mmu_mode;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	lockdep_assert_held(&kctx->mmu_lock);

	mmu_mode = kctx->kbdev->mmu_mode;

	pgd_page = kmap(pfn_to_page(PFN_DOWN(pgd)));
	if (!pgd_page) {
		dev_warn(kctx->kbdev->dev, "kbasep_mmu_dump_level: kmap failure\n");
		return 0;
	}

	if (*size_left >= size) {
		/* A modified physical address that contains the page table level */
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
						pgd_page[i]);

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

	if (0 == nr_pages) {
		/* can't dump in a 0 sized buffer, early out */
		return NULL;
	}

	size_left = nr_pages * PAGE_SIZE;

	KBASE_DEBUG_ASSERT(0 != size_left);
	kaddr = vmalloc_user(size_left);

	mutex_lock(&kctx->mmu_lock);

	if (kaddr) {
		u64 end_marker = 0xFFULL;
		char *buffer;
		char *mmu_dump_buffer;
		u64 config[3];
		size_t dump_size, size = 0;

		buffer = (char *)kaddr;
		mmu_dump_buffer = buffer;

		if (kctx->api_version >= KBASE_API_VERSION(8, 4)) {
			struct kbase_mmu_setup as_setup;

			kctx->kbdev->mmu_mode->get_as_setup(kctx, &as_setup);
			config[0] = as_setup.transtab;
			config[1] = as_setup.memattr;
			config[2] = as_setup.transcfg;
			memcpy(buffer, &config, sizeof(config));
			mmu_dump_buffer += sizeof(config);
			size_left -= sizeof(config);
			size += sizeof(config);
		}

		dump_size = kbasep_mmu_dump_level(kctx,
				kctx->pgd,
				MIDGARD_MMU_TOPLEVEL,
				&mmu_dump_buffer,
				&size_left);

		if (!dump_size)
			goto fail_free;

		size += dump_size;

		/* Add on the size for the end marker */
		size += sizeof(u64);

		if (size > (nr_pages * PAGE_SIZE)) {
			/* The buffer isn't big enough - free the memory and return failure */
			goto fail_free;
		}

		/* Add the end marker */
		memcpy(mmu_dump_buffer, &end_marker, sizeof(u64));
	}

	mutex_unlock(&kctx->mmu_lock);
	return kaddr;

fail_free:
	vfree(kaddr);
	mutex_unlock(&kctx->mmu_lock);
	return NULL;
}
KBASE_EXPORT_TEST_API(kbase_mmu_dump);

void bus_fault_worker(struct work_struct *data)
{
	struct kbase_as *faulting_as;
	int as_no;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
#if KBASE_GPU_RESET_EN
	bool reset_status = false;
#endif /* KBASE_GPU_RESET_EN */

	faulting_as = container_of(data, struct kbase_as, work_busfault);

	as_no = faulting_as->number;

	kbdev = container_of(faulting_as, struct kbase_device, as[as_no]);

	/* Grab the context that was already refcounted in kbase_mmu_interrupt().
	 * Therefore, it cannot be scheduled out of this AS until we explicitly release it
	 */
	kctx = kbasep_js_runpool_lookup_ctx_noretain(kbdev, as_no);
	if (WARN_ON(!kctx)) {
		atomic_dec(&kbdev->faults_pending);
		return;
	}

	if (unlikely(faulting_as->protected_mode)) {
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Permission failure");
		kbase_mmu_hw_clear_fault(kbdev, faulting_as, kctx,
				KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
		kbasep_js_runpool_release_ctx(kbdev, kctx);
		atomic_dec(&kbdev->faults_pending);
		return;

	}

#if KBASE_GPU_RESET_EN
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8245)) {
		/* Due to H/W issue 8245 we need to reset the GPU after using UNMAPPED mode.
		 * We start the reset before switching to UNMAPPED to ensure that unrelated jobs
		 * are evicted from the GPU before the switch.
		 */
		dev_err(kbdev->dev, "GPU bus error occurred. For this GPU version we now soft-reset as part of bus error recovery\n");
		reset_status = kbase_prepare_to_reset_gpu(kbdev);
	}
#endif /* KBASE_GPU_RESET_EN */
	/* NOTE: If GPU already powered off for suspend, we don't need to switch to unmapped */
	if (!kbase_pm_context_active_handle_suspend(kbdev, KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE)) {
		unsigned long flags;

		/* switch to UNMAPPED mode, will abort all jobs and stop any hw counter dumping */
		/* AS transaction begin */
		mutex_lock(&kbdev->mmu_hw_mutex);

		/* Set the MMU into unmapped mode */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_mmu_disable(kctx);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		mutex_unlock(&kbdev->mmu_hw_mutex);
		/* AS transaction end */

		kbase_mmu_hw_clear_fault(kbdev, faulting_as, kctx,
					 KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
		kbase_mmu_hw_enable_fault(kbdev, faulting_as, kctx,
					 KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);

		kbase_pm_context_idle(kbdev);
	}

#if KBASE_GPU_RESET_EN
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8245) && reset_status)
		kbase_reset_gpu(kbdev);
#endif /* KBASE_GPU_RESET_EN */

	kbasep_js_runpool_release_ctx(kbdev, kctx);

	atomic_dec(&kbdev->faults_pending);
}

const char *kbase_exception_name(struct kbase_device *kbdev, u32 exception_code)
{
	const char *e;

	switch (exception_code) {
		/* Non-Fault Status code */
	case 0x00:
		e = "NOT_STARTED/IDLE/OK";
		break;
	case 0x01:
		e = "DONE";
		break;
	case 0x02:
		e = "INTERRUPTED";
		break;
	case 0x03:
		e = "STOPPED";
		break;
	case 0x04:
		e = "TERMINATED";
		break;
	case 0x08:
		e = "ACTIVE";
		break;
		/* Job exceptions */
	case 0x40:
		e = "JOB_CONFIG_FAULT";
		break;
	case 0x41:
		e = "JOB_POWER_FAULT";
		break;
	case 0x42:
		e = "JOB_READ_FAULT";
		break;
	case 0x43:
		e = "JOB_WRITE_FAULT";
		break;
	case 0x44:
		e = "JOB_AFFINITY_FAULT";
		break;
	case 0x48:
		e = "JOB_BUS_FAULT";
		break;
	case 0x50:
		e = "INSTR_INVALID_PC";
		break;
	case 0x51:
		e = "INSTR_INVALID_ENC";
		break;
	case 0x52:
		e = "INSTR_TYPE_MISMATCH";
		break;
	case 0x53:
		e = "INSTR_OPERAND_FAULT";
		break;
	case 0x54:
		e = "INSTR_TLS_FAULT";
		break;
	case 0x55:
		e = "INSTR_BARRIER_FAULT";
		break;
	case 0x56:
		e = "INSTR_ALIGN_FAULT";
		break;
	case 0x58:
		e = "DATA_INVALID_FAULT";
		break;
	case 0x59:
		e = "TILE_RANGE_FAULT";
		break;
	case 0x5A:
		e = "ADDR_RANGE_FAULT";
		break;
	case 0x60:
		e = "OUT_OF_MEMORY";
		break;
		/* GPU exceptions */
	case 0x80:
		e = "DELAYED_BUS_FAULT";
		break;
	case 0x88:
		e = "SHAREABILITY_FAULT";
		break;
		/* MMU exceptions */
	case 0xC0:
	case 0xC1:
	case 0xC2:
	case 0xC3:
	case 0xC4:
	case 0xC5:
	case 0xC6:
	case 0xC7:
		e = "TRANSLATION_FAULT";
		break;
	case 0xC8:
		e = "PERMISSION_FAULT";
		break;
	case 0xC9:
	case 0xCA:
	case 0xCB:
	case 0xCC:
	case 0xCD:
	case 0xCE:
	case 0xCF:
		if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU))
			e = "PERMISSION_FAULT";
		else
			e = "UNKNOWN";
		break;
	case 0xD0:
	case 0xD1:
	case 0xD2:
	case 0xD3:
	case 0xD4:
	case 0xD5:
	case 0xD6:
	case 0xD7:
		e = "TRANSTAB_BUS_FAULT";
		break;
	case 0xD8:
		e = "ACCESS_FLAG";
		break;
	case 0xD9:
	case 0xDA:
	case 0xDB:
	case 0xDC:
	case 0xDD:
	case 0xDE:
	case 0xDF:
		if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU))
			e = "ACCESS_FLAG";
		else
			e = "UNKNOWN";
		break;
	case 0xE0:
	case 0xE1:
	case 0xE2:
	case 0xE3:
	case 0xE4:
	case 0xE5:
	case 0xE6:
	case 0xE7:
		if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU))
			e = "ADDRESS_SIZE_FAULT";
		else
			e = "UNKNOWN";
		break;
	case 0xE8:
	case 0xE9:
	case 0xEA:
	case 0xEB:
	case 0xEC:
	case 0xED:
	case 0xEE:
	case 0xEF:
		if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU))
			e = "MEMORY_ATTRIBUTES_FAULT";
		else
			e = "UNKNOWN";
		break;
	default:
		e = "UNKNOWN";
		break;
	};

	return e;
}

static const char *access_type_name(struct kbase_device *kbdev,
		u32 fault_status)
{
	switch (fault_status & AS_FAULTSTATUS_ACCESS_TYPE_MASK) {
	case AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC:
		if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU))
			return "ATOMIC";
		else
			return "UNKNOWN";
	case AS_FAULTSTATUS_ACCESS_TYPE_READ:
		return "READ";
	case AS_FAULTSTATUS_ACCESS_TYPE_WRITE:
		return "WRITE";
	case AS_FAULTSTATUS_ACCESS_TYPE_EX:
		return "EXECUTE";
	default:
		WARN_ON(1);
		return NULL;
	}
}

/**
 * The caller must ensure it's retained the ctx to prevent it from being scheduled out whilst it's being worked on.
 */
static void kbase_mmu_report_fault_and_kill(struct kbase_context *kctx,
		struct kbase_as *as, const char *reason_str)
{
	unsigned long flags;
	int exception_type;
	int access_type;
	int source_id;
	int as_no;
	struct kbase_device *kbdev;
	struct kbasep_js_device_data *js_devdata;

#if KBASE_GPU_RESET_EN
	bool reset_status = false;
#endif

	as_no = as->number;
	kbdev = kctx->kbdev;
	js_devdata = &kbdev->js_data;

	/* ASSERT that the context won't leave the runpool */
	KBASE_DEBUG_ASSERT(atomic_read(&kctx->refcount) > 0);

	/* decode the fault status */
	exception_type = as->fault_status & 0xFF;
	access_type = (as->fault_status >> 8) & 0x3;
	source_id = (as->fault_status >> 16);

	/* terminal fault, print info about the fault */
	dev_err(kbdev->dev,
		"Unhandled Page fault in AS%d at VA 0x%016llX\n"
		"Reason: %s\n"
		"raw fault status: 0x%X\n"
		"decoded fault status: %s\n"
		"exception type 0x%X: %s\n"
		"access type 0x%X: %s\n"
		"source id 0x%X\n"
		"pid: %d\n",
		as_no, as->fault_addr,
		reason_str,
		as->fault_status,
		(as->fault_status & (1 << 10) ? "DECODER FAULT" : "SLAVE FAULT"),
		exception_type, kbase_exception_name(kbdev, exception_type),
		access_type, access_type_name(kbdev, as->fault_status),
		source_id,
		kctx->pid);

	/* hardware counters dump fault handling */
	if ((kbdev->hwcnt.kctx) && (kbdev->hwcnt.kctx->as_nr == as_no) &&
			(kbdev->hwcnt.backend.state ==
						KBASE_INSTR_STATE_DUMPING)) {
		unsigned int num_core_groups = kbdev->gpu_props.num_core_groups;

		if ((as->fault_addr >= kbdev->hwcnt.addr) &&
				(as->fault_addr < (kbdev->hwcnt.addr +
						(num_core_groups * 2048))))
			kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_FAULT;
	}

	/* Stop the kctx from submitting more jobs and cause it to be scheduled
	 * out/rescheduled - this will occur on releasing the context's refcount */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbasep_js_clear_submit_allowed(js_devdata, kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* Kill any running jobs from the context. Submit is disallowed, so no more jobs from this
	 * context can appear in the job slots from this point on */
	kbase_backend_jm_kill_jobs_from_kctx(kctx);
	/* AS transaction begin */
	mutex_lock(&kbdev->mmu_hw_mutex);
#if KBASE_GPU_RESET_EN
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8245)) {
		/* Due to H/W issue 8245 we need to reset the GPU after using UNMAPPED mode.
		 * We start the reset before switching to UNMAPPED to ensure that unrelated jobs
		 * are evicted from the GPU before the switch.
		 */
		dev_err(kbdev->dev, "Unhandled page fault. For this GPU version we now soft-reset the GPU as part of page fault recovery.");
		reset_status = kbase_prepare_to_reset_gpu(kbdev);
	}
#endif /* KBASE_GPU_RESET_EN */
	/* switch to UNMAPPED mode, will abort all jobs and stop any hw counter dumping */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_mmu_disable(kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	mutex_unlock(&kbdev->mmu_hw_mutex);
	/* AS transaction end */
	/* Clear down the fault */
	kbase_mmu_hw_clear_fault(kbdev, as, kctx,
			KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
	kbase_mmu_hw_enable_fault(kbdev, as, kctx,
			KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);

#if KBASE_GPU_RESET_EN
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8245) && reset_status)
		kbase_reset_gpu(kbdev);
#endif /* KBASE_GPU_RESET_EN */
}

void kbasep_as_do_poke(struct work_struct *work)
{
	struct kbase_as *as;
	struct kbase_device *kbdev;
	struct kbase_context *kctx;
	unsigned long flags;

	KBASE_DEBUG_ASSERT(work);
	as = container_of(work, struct kbase_as, poke_work);
	kbdev = container_of(as, struct kbase_device, as[as->number]);
	KBASE_DEBUG_ASSERT(as->poke_state & KBASE_AS_POKE_STATE_IN_FLIGHT);

	/* GPU power will already be active by virtue of the caller holding a JS
	 * reference on the address space, and will not release it until this worker
	 * has finished */

	/* Further to the comment above, we know that while this function is running
	 * the AS will not be released as before the atom is released this workqueue
	 * is flushed (in kbase_as_poking_timer_release_atom)
	 */
	kctx = kbasep_js_runpool_lookup_ctx_noretain(kbdev, as->number);

	/* AS transaction begin */
	mutex_lock(&kbdev->mmu_hw_mutex);
	/* Force a uTLB invalidate */
	kbase_mmu_hw_do_operation(kbdev, as, kctx, 0, 0,
				  AS_COMMAND_UNLOCK, 0);
	mutex_unlock(&kbdev->mmu_hw_mutex);
	/* AS transaction end */

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	if (as->poke_refcount &&
		!(as->poke_state & KBASE_AS_POKE_STATE_KILLING_POKE)) {
		/* Only queue up the timer if we need it, and we're not trying to kill it */
		hrtimer_start(&as->poke_timer, HR_TIMER_DELAY_MSEC(5), HRTIMER_MODE_REL);
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

enum hrtimer_restart kbasep_as_poke_timer_callback(struct hrtimer *timer)
{
	struct kbase_as *as;
	int queue_work_ret;

	KBASE_DEBUG_ASSERT(NULL != timer);
	as = container_of(timer, struct kbase_as, poke_timer);
	KBASE_DEBUG_ASSERT(as->poke_state & KBASE_AS_POKE_STATE_IN_FLIGHT);

	queue_work_ret = queue_work(as->poke_wq, &as->poke_work);
	KBASE_DEBUG_ASSERT(queue_work_ret);
	return HRTIMER_NORESTART;
}

/**
 * Retain the poking timer on an atom's context (if the atom hasn't already
 * done so), and start the timer (if it's not already started).
 *
 * This must only be called on a context that's scheduled in, and an atom
 * that's running on the GPU.
 *
 * The caller must hold hwaccess_lock
 *
 * This can be called safely from atomic context
 */
void kbase_as_poking_timer_retain_atom(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	struct kbase_as *as;

	KBASE_DEBUG_ASSERT(kbdev);
	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(katom);
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (katom->poking)
		return;

	katom->poking = 1;

	/* It's safe to work on the as/as_nr without an explicit reference,
	 * because the caller holds the hwaccess_lock, and the atom itself
	 * was also running and had already taken a reference  */
	as = &kbdev->as[kctx->as_nr];

	if (++(as->poke_refcount) == 1) {
		/* First refcount for poke needed: check if not already in flight */
		if (!as->poke_state) {
			/* need to start poking */
			as->poke_state |= KBASE_AS_POKE_STATE_IN_FLIGHT;
			queue_work(as->poke_wq, &as->poke_work);
		}
	}
}

/**
 * If an atom holds a poking timer, release it and wait for it to finish
 *
 * This must only be called on a context that's scheduled in, and an atom
 * that still has a JS reference on the context
 *
 * This must \b not be called from atomic context, since it can sleep.
 */
void kbase_as_poking_timer_release_atom(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	struct kbase_as *as;
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev);
	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(katom);
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

	if (!katom->poking)
		return;

	as = &kbdev->as[kctx->as_nr];

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	KBASE_DEBUG_ASSERT(as->poke_refcount > 0);
	KBASE_DEBUG_ASSERT(as->poke_state & KBASE_AS_POKE_STATE_IN_FLIGHT);

	if (--(as->poke_refcount) == 0) {
		as->poke_state |= KBASE_AS_POKE_STATE_KILLING_POKE;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		hrtimer_cancel(&as->poke_timer);
		flush_workqueue(as->poke_wq);

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

		/* Re-check whether it's still needed */
		if (as->poke_refcount) {
			int queue_work_ret;
			/* Poking still needed:
			 * - Another retain will not be starting the timer or queueing work,
			 * because it's still marked as in-flight
			 * - The hrtimer has finished, and has not started a new timer or
			 * queued work because it's been marked as killing
			 *
			 * So whatever happens now, just queue the work again */
			as->poke_state &= ~((kbase_as_poke_state)KBASE_AS_POKE_STATE_KILLING_POKE);
			queue_work_ret = queue_work(as->poke_wq, &as->poke_work);
			KBASE_DEBUG_ASSERT(queue_work_ret);
		} else {
			/* It isn't - so mark it as not in flight, and not killing */
			as->poke_state = 0u;

			/* The poke associated with the atom has now finished. If this is
			 * also the last atom on the context, then we can guarentee no more
			 * pokes (and thus no more poking register accesses) will occur on
			 * the context until new atoms are run */
		}
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	katom->poking = 0;
}

void kbase_mmu_interrupt_process(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbase_as *as)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (!kctx) {
		dev_warn(kbdev->dev, "%s in AS%d at 0x%016llx with no context present! Suprious IRQ or SW Design Error?\n",
				 kbase_as_has_bus_fault(as) ? "Bus error" : "Page fault",
				 as->number, as->fault_addr);

		/* Since no ctx was found, the MMU must be disabled. */
		WARN_ON(as->current_setup.transtab);

		if (kbase_as_has_bus_fault(as)) {
			kbase_mmu_hw_clear_fault(kbdev, as, kctx,
					KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
			kbase_mmu_hw_enable_fault(kbdev, as, kctx,
					KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
		} else if (kbase_as_has_page_fault(as)) {
			kbase_mmu_hw_clear_fault(kbdev, as, kctx,
					KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
			kbase_mmu_hw_enable_fault(kbdev, as, kctx,
					KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
		}

#if KBASE_GPU_RESET_EN
		if (kbase_as_has_bus_fault(as) &&
				kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8245)) {
			bool reset_status;
			/*
			 * Reset the GPU, like in bus_fault_worker, in case an
			 * earlier error hasn't been properly cleared by this
			 * point.
			 */
			dev_err(kbdev->dev, "GPU bus error occurred. For this GPU version we now soft-reset as part of bus error recovery\n");
			reset_status = kbase_prepare_to_reset_gpu_locked(kbdev);
			if (reset_status)
				kbase_reset_gpu_locked(kbdev);
		}
#endif /* KBASE_GPU_RESET_EN */

		return;
	}

	if (kbase_as_has_bus_fault(as)) {
		/*
		 * hw counters dumping in progress, signal the
		 * other thread that it failed
		 */
		if ((kbdev->hwcnt.kctx == kctx) &&
		    (kbdev->hwcnt.backend.state ==
					KBASE_INSTR_STATE_DUMPING))
			kbdev->hwcnt.backend.state =
						KBASE_INSTR_STATE_FAULT;

		/*
		 * Stop the kctx from submitting more jobs and cause it
		 * to be scheduled out/rescheduled when all references
		 * to it are released
		 */
		kbasep_js_clear_submit_allowed(js_devdata, kctx);

		if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU))
			dev_warn(kbdev->dev,
					"Bus error in AS%d at VA=0x%016llx, IPA=0x%016llx\n",
					as->number, as->fault_addr,
					as->fault_extra_addr);
		else
			dev_warn(kbdev->dev, "Bus error in AS%d at 0x%016llx\n",
					as->number, as->fault_addr);

		/*
		 * We need to switch to UNMAPPED mode - but we do this in a
		 * worker so that we can sleep
		 */
		WARN_ON(!queue_work(as->pf_wq, &as->work_busfault));
		atomic_inc(&kbdev->faults_pending);
	} else {
		WARN_ON(!queue_work(as->pf_wq, &as->work_pagefault));
		atomic_inc(&kbdev->faults_pending);
	}
}

void kbase_flush_mmu_wqs(struct kbase_device *kbdev)
{
	int i;

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		struct kbase_as *as = &kbdev->as[i];

		flush_workqueue(as->pf_wq);
	}
}
