/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





/**
 * @file mali_kbase_mmu.c
 * Base kernel MMU management.
 */

/* #define DEBUG    1 */
#include <linux/dma-mapping.h>
#include <mali_kbase.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_gator.h>
#include <mali_kbase_debug.h>

#define beenthere(kctx, f, a...)  dev_dbg(kctx->kbdev->dev, "%s:" f, __func__, ##a)

#include <mali_kbase_defs.h>
#include <mali_kbase_hw.h>
#include <mali_kbase_mmu_hw.h>

#define KBASE_MMU_PAGE_ENTRIES 512

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


/* Helper Function to perform assignment of page table entries, to ensure the use of
 * strd, which is required on LPAE systems.
 */

static inline void page_table_entry_set(struct kbase_device *kbdev, u64 *pte, u64 phy)
{
#ifdef CONFIG_64BIT
	*pte = phy;
#elif defined(CONFIG_ARM)
	/*
	 *
	 * In order to prevent the compiler keeping cached copies of memory, we have to explicitly
	 * say that we have updated memory.
	 *
	 * Note: We could manually move the data ourselves into R0 and R1 by specifying
	 * register variables that are explicitly given registers assignments, the down side of
	 * this is that we have to assume cpu endianess.  To avoid this we can use the ldrd to read the
	 * data from memory into R0 and R1 which will respect the cpu endianess, we then use strd to
	 * make the 64 bit assignment to the page table entry.
	 *
	 */

	asm	volatile("ldrd r0, r1, [%[ptemp]]\n\t"
				"strd r0, r1, [%[pte]]\n\t"
				: "=m" (*pte)
				: [ptemp] "r" (&phy), [pte] "r" (pte), "m" (phy)
				: "r0", "r1");
#else
#error "64-bit atomic write must be implemented for your architecture"
#endif
}

static size_t make_multiple(size_t minimum, size_t multiple)
{
	size_t remainder = minimum % multiple;
	if (remainder == 0)
		return minimum;
	else
		return minimum + multiple - remainder;
}

static void page_fault_worker(struct work_struct *data)
{
	u64 fault_pfn;
	u32 fault_access;
	size_t new_pages;
	size_t fault_rel_pfn;
	struct kbase_as *faulting_as;
	int as_no;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	struct kbase_va_region *region;
	mali_error err;

	faulting_as = container_of(data, struct kbase_as, work_pagefault);
	fault_pfn = faulting_as->fault_addr >> PAGE_SHIFT;
	as_no = faulting_as->number;

	kbdev = container_of(faulting_as, struct kbase_device, as[as_no]);

	/* Grab the context that was already refcounted in kbase_mmu_interrupt().
	 * Therefore, it cannot be scheduled out of this AS until we explicitly release it
	 *
	 * NOTE: NULL can be returned here if we're gracefully handling a spurious interrupt */
	kctx = kbasep_js_runpool_lookup_ctx_noretain(kbdev, as_no);

	if (kctx == NULL) {
		/* Only handle this if not already suspended */
		if (!kbase_pm_context_active_handle_suspend(kbdev, KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE)) {
			struct kbase_mmu_setup *current_setup = &faulting_as->current_setup;

			/* Address space has no context, terminate the work */

			/* AS transaction begin */
			mutex_lock(&faulting_as->transaction_mutex);

			/* Switch to unmapped mode */
			current_setup->transtab &= ~(u64)MMU_TRANSTAB_ADRMODE_MASK;
			current_setup->transtab |= AS_TRANSTAB_ADRMODE_UNMAPPED;

			/* Apply new address space settings */
			kbase_mmu_hw_configure(kbdev, faulting_as, kctx);

			mutex_unlock(&faulting_as->transaction_mutex);
			/* AS transaction end */

			kbase_mmu_hw_clear_fault(kbdev, faulting_as, kctx,
					KBASE_MMU_FAULT_TYPE_PAGE);
			kbase_mmu_hw_enable_fault(kbdev, faulting_as, kctx,
					KBASE_MMU_FAULT_TYPE_PAGE);
			kbase_pm_context_idle(kbdev);
		}
		return;
	}

	KBASE_DEBUG_ASSERT(kctx->kbdev == kbdev);

	kbase_gpu_vm_lock(kctx);

	region = kbase_region_tracker_find_region_enclosing_address(kctx, faulting_as->fault_addr);
	if (NULL == region || region->flags & KBASE_REG_FREE) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Memory is not mapped on the GPU");
		goto fault_done;
	}

	fault_access = faulting_as->fault_status & AS_FAULTSTATUS_ACCESS_TYPE_MASK;
	if (((fault_access == AS_FAULTSTATUS_ACCESS_TYPE_READ) &&
			!(region->flags & KBASE_REG_GPU_RD)) ||
			((fault_access == AS_FAULTSTATUS_ACCESS_TYPE_WRITE) &&
			!(region->flags & KBASE_REG_GPU_WR)) ||
			((fault_access == AS_FAULTSTATUS_ACCESS_TYPE_EX) &&
			(region->flags & KBASE_REG_GPU_NX))) {
		dev_warn(kbdev->dev, "Access permissions don't match: region->flags=0x%lx", region->flags);
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Access permissions mismatch");
		goto fault_done;
	}

	if (!(region->flags & GROWABLE_FLAGS_REQUIRED)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Memory is not growable");
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
		kbase_mmu_hw_do_operation(kbdev, faulting_as, 0, 0, 0,
				AS_COMMAND_UNLOCK, 1);
		kbase_mmu_hw_enable_fault(kbdev, faulting_as, kctx,
				KBASE_MMU_FAULT_TYPE_PAGE);
		kbase_gpu_vm_unlock(kctx);

		goto fault_done;
	}

	new_pages = make_multiple(fault_rel_pfn -
			kbase_reg_current_backed_size(region) + 1,
			region->extent);

	/* cap to max vsize */
	if (new_pages + kbase_reg_current_backed_size(region) >
			region->nr_pages)
		new_pages = region->nr_pages -
				kbase_reg_current_backed_size(region);

	if (0 == new_pages) {
		/* Duplicate of a fault we've already handled, nothing to do */
		kbase_mmu_hw_clear_fault(kbdev, faulting_as, kctx,
				KBASE_MMU_FAULT_TYPE_PAGE);
		/* See comment [1] about UNLOCK usage */
		kbase_mmu_hw_do_operation(kbdev, faulting_as, 0, 0, 0,
				AS_COMMAND_UNLOCK, 1);
		kbase_mmu_hw_enable_fault(kbdev, faulting_as, kctx,
				KBASE_MMU_FAULT_TYPE_PAGE);
		kbase_gpu_vm_unlock(kctx);
		goto fault_done;
	}

	if (MALI_ERROR_NONE == kbase_alloc_phy_pages_helper(region->alloc, new_pages)) {
		u32 op;

		/* alloc success */
		KBASE_DEBUG_ASSERT(kbase_reg_current_backed_size(region) <= region->nr_pages);

		/* AS transaction begin */
		mutex_lock(&faulting_as->transaction_mutex);

		/* set up the new pages */
		err = kbase_mmu_insert_pages(kctx, region->start_pfn + kbase_reg_current_backed_size(region) - new_pages, &kbase_get_phy_pages(region)[kbase_reg_current_backed_size(region) - new_pages], new_pages, region->flags);
		if (MALI_ERROR_NONE != err) {
			/* failed to insert pages, handle as a normal PF */
			mutex_unlock(&faulting_as->transaction_mutex);
			kbase_free_phy_pages_helper(region->alloc, new_pages);
			kbase_gpu_vm_unlock(kctx);
			/* The locked VA region will be unlocked and the cache invalidated in here */
			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
					"Page table update failure");
			goto fault_done;
		}
#ifdef CONFIG_MALI_GATOR_SUPPORT
		kbase_trace_mali_page_fault_insert_pages(as_no, new_pages);
#endif				/* CONFIG_MALI_GATOR_SUPPORT */

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

		mutex_unlock(&faulting_as->transaction_mutex);
		/* AS transaction end */

		/* reenable this in the mask */
		kbase_mmu_hw_enable_fault(kbdev, faulting_as, kctx,
					 KBASE_MMU_FAULT_TYPE_PAGE);
		kbase_gpu_vm_unlock(kctx);
	} else {
		/* failed to extend, handle as a normal PF */
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
				"Page allocation failure");
	}

fault_done:
	/*
	 * By this point, the fault was handled in some way,
	 * so release the ctx refcount
	 */
	kbasep_js_runpool_release_ctx(kbdev, kctx);
}

phys_addr_t kbase_mmu_alloc_pgd(struct kbase_context *kctx)
{
	phys_addr_t pgd;
	u64 *page;
	int i;
	struct page *p;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	kbase_atomic_add_pages(1, &kctx->used_pages);
	kbase_atomic_add_pages(1, &kctx->kbdev->memdev.used_pages);

	if (MALI_ERROR_NONE != kbase_mem_allocator_alloc(kctx->pgd_allocator, 1, &pgd))
		goto sub_pages;

	p = pfn_to_page(PFN_DOWN(pgd));
	page = kmap(p);
	if (NULL == page)
		goto alloc_free;

	kbase_process_page_usage_inc(kctx, 1);

	for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++)
		page_table_entry_set(kctx->kbdev, &page[i], ENTRY_IS_INVAL);

	/* Clean the full page */
	dma_sync_single_for_device(kctx->kbdev->dev,
				   kbase_dma_addr(p),
				   PAGE_SIZE,
				   DMA_TO_DEVICE);
	kunmap(pfn_to_page(PFN_DOWN(pgd)));
	return pgd;

alloc_free:
	kbase_mem_allocator_free(kctx->pgd_allocator, 1, &pgd, MALI_FALSE);
sub_pages:
	kbase_atomic_sub_pages(1, &kctx->used_pages);
	kbase_atomic_sub_pages(1, &kctx->kbdev->memdev.used_pages);

	return 0;
}

KBASE_EXPORT_TEST_API(kbase_mmu_alloc_pgd)

static phys_addr_t mmu_pte_to_phy_addr(u64 entry)
{
	if (!(entry & 1))
		return 0;

	return entry & ~0xFFF;
}

static u64 mmu_phyaddr_to_pte(phys_addr_t phy)
{
	return (phy & ~0xFFF) | ENTRY_IS_PTE;
}

static u64 mmu_phyaddr_to_ate(phys_addr_t phy, u64 flags)
{
	return (phy & ~0xFFF) | (flags & ENTRY_FLAGS_MASK) | ENTRY_IS_ATE;
}

/* Given PGD PFN for level N, return PGD PFN for level N+1 */
static phys_addr_t mmu_get_next_pgd(struct kbase_context *kctx, phys_addr_t pgd, u64 vpfn, int level)
{
	u64 *page;
	phys_addr_t target_pgd;
	struct page *p;

	KBASE_DEBUG_ASSERT(pgd);
	KBASE_DEBUG_ASSERT(NULL != kctx);

	lockdep_assert_held(&kctx->reg_lock);

	/*
	 * Architecture spec defines level-0 as being the top-most.
	 * This is a bit unfortunate here, but we keep the same convention.
	 */
	vpfn >>= (3 - level) * 9;
	vpfn &= 0x1FF;

	p = pfn_to_page(PFN_DOWN(pgd));
	page = kmap(p);
	if (NULL == page) {
		dev_warn(kctx->kbdev->dev, "mmu_get_next_pgd: kmap failure\n");
		return 0;
	}

	target_pgd = mmu_pte_to_phy_addr(page[vpfn]);

	if (!target_pgd) {
		target_pgd = kbase_mmu_alloc_pgd(kctx);
		if (!target_pgd) {
			dev_warn(kctx->kbdev->dev, "mmu_get_next_pgd: kbase_mmu_alloc_pgd failure\n");
			kunmap(p);
			return 0;
		}

		page_table_entry_set(kctx->kbdev, &page[vpfn],
				mmu_phyaddr_to_pte(target_pgd));

		dma_sync_single_for_device(kctx->kbdev->dev,
					   kbase_dma_addr(p),
					   PAGE_SIZE,
					   DMA_TO_DEVICE);
		/* Rely on the caller to update the address space flags. */
	}

	kunmap(p);
	return target_pgd;
}

static phys_addr_t mmu_get_bottom_pgd(struct kbase_context *kctx, u64 vpfn)
{
	phys_addr_t pgd;
	int l;

	pgd = kctx->pgd;

	for (l = MIDGARD_MMU_TOPLEVEL; l < 3; l++) {
		pgd = mmu_get_next_pgd(kctx, pgd, vpfn, l);
		/* Handle failure condition */
		if (!pgd) {
			dev_warn(kctx->kbdev->dev, "mmu_get_bottom_pgd: mmu_get_next_pgd failure\n");
			return 0;
		}
	}

	return pgd;
}

static phys_addr_t mmu_insert_pages_recover_get_next_pgd(struct kbase_context *kctx, phys_addr_t pgd, u64 vpfn, int level)
{
	u64 *page;
	phys_addr_t target_pgd;

	KBASE_DEBUG_ASSERT(pgd);
	KBASE_DEBUG_ASSERT(NULL != kctx);

	lockdep_assert_held(&kctx->reg_lock);

	/*
	 * Architecture spec defines level-0 as being the top-most.
	 * This is a bit unfortunate here, but we keep the same convention.
	 */
	vpfn >>= (3 - level) * 9;
	vpfn &= 0x1FF;

	page = kmap_atomic(pfn_to_page(PFN_DOWN(pgd)));
	/* kmap_atomic should NEVER fail */
	KBASE_DEBUG_ASSERT(NULL != page);

	target_pgd = mmu_pte_to_phy_addr(page[vpfn]);
	/* As we are recovering from what has already been set up, we should have a target_pgd */
	KBASE_DEBUG_ASSERT(0 != target_pgd);

	kunmap_atomic(page);
	return target_pgd;
}

static phys_addr_t mmu_insert_pages_recover_get_bottom_pgd(struct kbase_context *kctx, u64 vpfn)
{
	phys_addr_t pgd;
	int l;

	pgd = kctx->pgd;

	for (l = MIDGARD_MMU_TOPLEVEL; l < 3; l++) {
		pgd = mmu_insert_pages_recover_get_next_pgd(kctx, pgd, vpfn, l);
		/* Should never fail */
		KBASE_DEBUG_ASSERT(0 != pgd);
	}

	return pgd;
}

static void mmu_insert_pages_failure_recovery(struct kbase_context *kctx, u64 vpfn,
					      size_t nr)
{
	phys_addr_t pgd;
	u64 *pgd_page;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(0 != vpfn);
	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(vpfn <= (UINT64_MAX / PAGE_SIZE));

	lockdep_assert_held(&kctx->reg_lock);

	while (nr) {
		unsigned int i;
		unsigned int index = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - index;
		struct page *p;

		if (count > nr)
			count = nr;

		pgd = mmu_insert_pages_recover_get_bottom_pgd(kctx, vpfn);
		KBASE_DEBUG_ASSERT(0 != pgd);

		p = pfn_to_page(PFN_DOWN(pgd));

		pgd_page = kmap_atomic(p);
		KBASE_DEBUG_ASSERT(NULL != pgd_page);

		/* Invalidate the entries we added */
		for (i = 0; i < count; i++)
			page_table_entry_set(kctx->kbdev, &pgd_page[index + i],
					     ENTRY_IS_INVAL);

		vpfn += count;
		nr -= count;

		dma_sync_single_for_device(kctx->kbdev->dev,
					   kbase_dma_addr(p),
					   PAGE_SIZE, DMA_TO_DEVICE);
		kunmap_atomic(pgd_page);
	}
}

/**
 * Map KBASE_REG flags to MMU flags
 */
static u64 kbase_mmu_get_mmu_flags(unsigned long flags)
{
	u64 mmu_flags;

	/* store mem_attr index as 4:2 (macro called ensures 3 bits already) */
	mmu_flags = KBASE_REG_MEMATTR_VALUE(flags) << 2;

	/* write perm if requested */
	mmu_flags |= (flags & KBASE_REG_GPU_WR) ? ENTRY_WR_BIT : 0;
	/* read perm if requested */
	mmu_flags |= (flags & KBASE_REG_GPU_RD) ? ENTRY_RD_BIT : 0;
	/* nx if requested */
	mmu_flags |= (flags & KBASE_REG_GPU_NX) ? ENTRY_NX_BIT : 0;

	if (flags & KBASE_REG_SHARE_BOTH) {
		/* inner and outer shareable */
		mmu_flags |= SHARE_BOTH_BITS;
	} else if (flags & KBASE_REG_SHARE_IN) {
		/* inner shareable coherency */
		mmu_flags |= SHARE_INNER_BITS;
	}

	return mmu_flags;
}

/*
 * Map the single page 'phys' 'nr' of times, starting at GPU PFN 'vpfn'
 */
mali_error kbase_mmu_insert_single_page(struct kbase_context *kctx, u64 vpfn,
					phys_addr_t phys, size_t nr,
					unsigned long flags)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	u64 pte_entry;
	/* In case the insert_single_page only partially completes we need to be
	 * able to recover */
	mali_bool recover_required = MALI_FALSE;
	u64 recover_vpfn = vpfn;
	size_t recover_count = 0;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(0 != vpfn);
	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(vpfn <= (UINT64_MAX / PAGE_SIZE));

	lockdep_assert_held(&kctx->reg_lock);

	/* the one entry we'll populate everywhere */
	pte_entry = mmu_phyaddr_to_ate(phys, kbase_mmu_get_mmu_flags(flags));

	while (nr) {
		unsigned int i;
		unsigned int index = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - index;
		struct page *p;

		if (count > nr)
			count = nr;

		/*
		 * Repeatedly calling mmu_get_bottom_pte() is clearly
		 * suboptimal. We don't have to re-parse the whole tree
		 * each time (just cache the l0-l2 sequence).
		 * On the other hand, it's only a gain when we map more than
		 * 256 pages at once (on average). Do we really care?
		 */
		pgd = mmu_get_bottom_pgd(kctx, vpfn);
		if (!pgd) {
			dev_warn(kctx->kbdev->dev,
					       "kbase_mmu_insert_pages: "
					       "mmu_get_bottom_pgd failure\n");
			if (recover_required) {
				/* Invalidate the pages we have partially
				 * completed */
				mmu_insert_pages_failure_recovery(kctx,
								  recover_vpfn,
								  recover_count);
			}
			return MALI_ERROR_FUNCTION_FAILED;
		}

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap(p);
		if (!pgd_page) {
			dev_warn(kctx->kbdev->dev,
					       "kbase_mmu_insert_pages: "
					       "kmap failure\n");
			if (recover_required) {
				/* Invalidate the pages we have partially
				 * completed */
				mmu_insert_pages_failure_recovery(kctx,
								  recover_vpfn,
								  recover_count);
			}
			return MALI_ERROR_OUT_OF_MEMORY;
		}

		for (i = 0; i < count; i++) {
			unsigned int ofs = index + i;
			KBASE_DEBUG_ASSERT(0 == (pgd_page[ofs] & 1UL));
			page_table_entry_set(kctx->kbdev, &pgd_page[ofs],
					     pte_entry);
		}

		vpfn += count;
		nr -= count;

		dma_sync_single_for_device(kctx->kbdev->dev,
					   kbase_dma_addr(p) +
					   (index * sizeof(u64)),
					   count * sizeof(u64),
					   DMA_TO_DEVICE);


		kunmap(p);
		/* We have started modifying the page table.
		 * If further pages need inserting and fail we need to undo what
		 * has already taken place */
		recover_required = MALI_TRUE;
		recover_count += count;
	}
	return MALI_ERROR_NONE;
}

/*
 * Map 'nr' pages pointed to by 'phys' at GPU PFN 'vpfn'
 */
mali_error kbase_mmu_insert_pages(struct kbase_context *kctx, u64 vpfn,
				  phys_addr_t *phys, size_t nr,
				  unsigned long flags)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	u64 mmu_flags = 0;
	/* In case the insert_pages only partially completes we need to be able
	 * to recover */
	mali_bool recover_required = MALI_FALSE;
	u64 recover_vpfn = vpfn;
	size_t recover_count = 0;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(0 != vpfn);
	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(vpfn <= (UINT64_MAX / PAGE_SIZE));

	lockdep_assert_held(&kctx->reg_lock);

	mmu_flags = kbase_mmu_get_mmu_flags(flags);

	while (nr) {
		unsigned int i;
		unsigned int index = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - index;
		struct page *p;

		if (count > nr)
			count = nr;

		/*
		 * Repeatedly calling mmu_get_bottom_pte() is clearly
		 * suboptimal. We don't have to re-parse the whole tree
		 * each time (just cache the l0-l2 sequence).
		 * On the other hand, it's only a gain when we map more than
		 * 256 pages at once (on average). Do we really care?
		 */
		pgd = mmu_get_bottom_pgd(kctx, vpfn);
		if (!pgd) {
			dev_warn(kctx->kbdev->dev,
					       "kbase_mmu_insert_pages: "
					       "mmu_get_bottom_pgd failure\n");
			if (recover_required) {
				/* Invalidate the pages we have partially
				 * completed */
				mmu_insert_pages_failure_recovery(kctx,
								  recover_vpfn,
								  recover_count);
			}
			return MALI_ERROR_FUNCTION_FAILED;
		}

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap(p);
		if (!pgd_page) {
			dev_warn(kctx->kbdev->dev,
					       "kbase_mmu_insert_pages: "
					       "kmap failure\n");
			if (recover_required) {
				/* Invalidate the pages we have partially
				 * completed */
				mmu_insert_pages_failure_recovery(kctx,
								  recover_vpfn,
								  recover_count);
			}
			return MALI_ERROR_OUT_OF_MEMORY;
		}

		for (i = 0; i < count; i++) {
			unsigned int ofs = index + i;
			KBASE_DEBUG_ASSERT(0 == (pgd_page[ofs] & 1UL));
			page_table_entry_set(kctx->kbdev, &pgd_page[ofs],
					     mmu_phyaddr_to_ate(phys[i],
								mmu_flags)
					     );
		}

		phys += count;
		vpfn += count;
		nr -= count;

		dma_sync_single_for_device(kctx->kbdev->dev,
					   kbase_dma_addr(p) +
					   (index * sizeof(u64)),
					   count * sizeof(u64),
					   DMA_TO_DEVICE);

		kunmap(p);
		/* We have started modifying the page table. If further pages
		 * need inserting and fail we need to undo what has already
		 * taken place */
		recover_required = MALI_TRUE;
		recover_count += count;
	}
	return MALI_ERROR_NONE;
}

KBASE_EXPORT_TEST_API(kbase_mmu_insert_pages)

/**
 * This function is responsible for validating the MMU PTs
 * triggering reguired flushes.
 *
 * * IMPORTANT: This uses kbasep_js_runpool_release_ctx() when the context is
 * currently scheduled into the runpool, and so potentially uses a lot of locks.
 * These locks must be taken in the correct order with respect to others
 * already held by the caller. Refer to kbasep_js_runpool_release_ctx() for more
 * information.
 */
static void kbase_mmu_flush(struct kbase_context *kctx, u64 vpfn, size_t nr)
{
	struct kbase_device *kbdev;
	mali_bool ctx_is_in_runpool;

	KBASE_DEBUG_ASSERT(NULL != kctx);

	kbdev = kctx->kbdev;

	/* We must flush if we're currently running jobs. At the very least, we need to retain the
	 * context to ensure it doesn't schedule out whilst we're trying to flush it */
	ctx_is_in_runpool = kbasep_js_runpool_retain_ctx(kbdev, kctx);

	if (ctx_is_in_runpool) {
		KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

		/* Second level check is to try to only do this when jobs are running. The refcount is
		 * a heuristic for this. */
		if (kbdev->js_data.runpool_irq.per_as_data[kctx->as_nr].as_busy_refcount >= 2) {
			int ret;
			u32 op;

			/* AS transaction begin */
			mutex_lock(&kbdev->as[kctx->as_nr].transaction_mutex);

			if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_6367))
				op = AS_COMMAND_FLUSH;
			else
				op = AS_COMMAND_FLUSH_MEM;

			ret = kbase_mmu_hw_do_operation(kbdev,
							&kbdev->as[kctx->as_nr],
							kctx, vpfn, nr,
							op, 0);
#if KBASE_GPU_RESET_EN
			if (ret) {
				/* Flush failed to complete, assume the GPU has hung and perform a reset to recover */
				dev_err(kbdev->dev, "Flush for GPU page table update did not complete. Issueing GPU soft-reset to recover\n");
				if (kbase_prepare_to_reset_gpu(kbdev))
					kbase_reset_gpu(kbdev);
			}
#endif /* KBASE_GPU_RESET_EN */

			mutex_unlock(&kbdev->as[kctx->as_nr].transaction_mutex);
			/* AS transaction end */
		}
		kbasep_js_runpool_release_ctx(kbdev, kctx);
	}
}

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
mali_error kbase_mmu_teardown_pages(struct kbase_context *kctx, u64 vpfn, size_t nr)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	struct kbase_device *kbdev;
	size_t requested_nr = nr;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	beenthere(kctx, "kctx %p vpfn %lx nr %zd", (void *)kctx, (unsigned long)vpfn, nr);

	lockdep_assert_held(&kctx->reg_lock);

	if (0 == nr) {
		/* early out if nothing to do */
		return MALI_ERROR_NONE;
	}

	kbdev = kctx->kbdev;

	while (nr) {
		unsigned int i;
		unsigned int index = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - index;
		struct page *p;
		if (count > nr)
			count = nr;

		pgd = mmu_get_bottom_pgd(kctx, vpfn);
		if (!pgd) {
			dev_warn(kbdev->dev, "kbase_mmu_teardown_pages: mmu_get_bottom_pgd failure\n");
			return MALI_ERROR_FUNCTION_FAILED;
		}

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap(p);
		if (!pgd_page) {
			dev_warn(kbdev->dev, "kbase_mmu_teardown_pages: kmap failure\n");
			return MALI_ERROR_OUT_OF_MEMORY;
		}

		for (i = 0; i < count; i++) {
			page_table_entry_set(kctx->kbdev, &pgd_page[index + i], ENTRY_IS_INVAL);
		}

		vpfn += count;
		nr -= count;

		dma_sync_single_for_device(kctx->kbdev->dev,
					   kbase_dma_addr(p) +
					   (index * sizeof(u64)),
					   count * sizeof(u64),
					   DMA_TO_DEVICE);

		kunmap(p);
	}

	kbase_mmu_flush(kctx, vpfn, requested_nr);
	return MALI_ERROR_NONE;
}

KBASE_EXPORT_TEST_API(kbase_mmu_teardown_pages)

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
mali_error kbase_mmu_update_pages(struct kbase_context *kctx, u64 vpfn, phys_addr_t* phys, size_t nr, unsigned long flags)
{
	phys_addr_t pgd;
	u64* pgd_page;
	u64 mmu_flags = 0;
	size_t requested_nr = nr;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(0 != vpfn);
	KBASE_DEBUG_ASSERT(vpfn <= (UINT64_MAX / PAGE_SIZE));

	lockdep_assert_held(&kctx->reg_lock);

	mmu_flags = kbase_mmu_get_mmu_flags(flags);

	dev_warn(kctx->kbdev->dev, "kbase_mmu_update_pages(): updating page share flags "\
			"on GPU PFN 0x%llx from phys %p, %zu pages", 
			vpfn, phys, nr);


	while(nr) {
		unsigned int i;
		unsigned int index = vpfn & 0x1FF;
		size_t count = KBASE_MMU_PAGE_ENTRIES - index;
		struct page *p;

		if (count > nr)
			count = nr;

		pgd = mmu_get_bottom_pgd(kctx, vpfn);
		if (!pgd) {
			dev_warn(kctx->kbdev->dev, "mmu_get_bottom_pgd failure\n");
			return MALI_ERROR_FUNCTION_FAILED;
		}

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap(p);
		if (!pgd_page) {
			dev_warn(kctx->kbdev->dev, "kmap failure\n");
			return MALI_ERROR_OUT_OF_MEMORY;
		}

		for (i = 0; i < count; i++) {
			page_table_entry_set(kctx->kbdev, &pgd_page[index + i],  mmu_phyaddr_to_ate(phys[i], mmu_flags));
		}

		phys += count;
		vpfn += count;
		nr -= count;

		dma_sync_single_for_device(kctx->kbdev->dev,
					   kbase_dma_addr(p) +
					   (index * sizeof(u64)),
					   count * sizeof(u64),
					   DMA_TO_DEVICE);

		kunmap(pfn_to_page(PFN_DOWN(pgd)));
	}

	kbase_mmu_flush(kctx, vpfn, requested_nr);

	return MALI_ERROR_NONE;
}

static int mmu_pte_is_valid(u64 pte)
{
	return ((pte & 3) == ENTRY_IS_ATE);
}

/* This is a debug feature only */
static void mmu_check_unused(struct kbase_context *kctx, phys_addr_t pgd)
{
	u64 *page;
	int i;

	page = kmap_atomic(pfn_to_page(PFN_DOWN(pgd)));
	/* kmap_atomic should NEVER fail. */
	KBASE_DEBUG_ASSERT(NULL != page);

	for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++) {
		if (mmu_pte_is_valid(page[i]))
			beenthere(kctx, "live pte %016lx", (unsigned long)page[i]);
	}
	kunmap_atomic(page);
}

static void mmu_teardown_level(struct kbase_context *kctx, phys_addr_t pgd, int level, int zap, u64 *pgd_page_buffer)
{
	phys_addr_t target_pgd;
	u64 *pgd_page;
	int i;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	lockdep_assert_held(&kctx->reg_lock);

	pgd_page = kmap_atomic(pfn_to_page(PFN_DOWN(pgd)));
	/* kmap_atomic should NEVER fail. */
	KBASE_DEBUG_ASSERT(NULL != pgd_page);
	/* Copy the page to our preallocated buffer so that we can minimize kmap_atomic usage */
	memcpy(pgd_page_buffer, pgd_page, PAGE_SIZE);
	kunmap_atomic(pgd_page);
	pgd_page = pgd_page_buffer;

	for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++) {
		target_pgd = mmu_pte_to_phy_addr(pgd_page[i]);

		if (target_pgd) {
			if (level < 2) {
				mmu_teardown_level(kctx, target_pgd, level + 1, zap, pgd_page_buffer + (PAGE_SIZE / sizeof(u64)));
			} else {
				/*
				 * So target_pte is a level-3 page.
				 * As a leaf, it is safe to free it.
				 * Unless we have live pages attached to it!
				 */
				mmu_check_unused(kctx, target_pgd);
			}

			beenthere(kctx, "pte %lx level %d", (unsigned long)target_pgd, level + 1);
			if (zap) {
				kbase_mem_allocator_free(kctx->pgd_allocator, 1, &target_pgd, MALI_TRUE);
				kbase_process_page_usage_dec(kctx, 1);
				kbase_atomic_sub_pages(1, &kctx->used_pages);
				kbase_atomic_sub_pages(1, &kctx->kbdev->memdev.used_pages);
			}
		}
	}
}

mali_error kbase_mmu_init(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL == kctx->mmu_teardown_pages);

	/* Preallocate MMU depth of four pages for mmu_teardown_level to use */
	kctx->mmu_teardown_pages = kmalloc(PAGE_SIZE * 4, GFP_KERNEL);

	kctx->mem_attrs = (AS_MEMATTR_IMPL_DEF_CACHE_POLICY <<
			   (AS_MEMATTR_INDEX_IMPL_DEF_CACHE_POLICY * 8)) |
			  (AS_MEMATTR_FORCE_TO_CACHE_ALL    <<
			   (AS_MEMATTR_INDEX_FORCE_TO_CACHE_ALL * 8)) |
			  (AS_MEMATTR_WRITE_ALLOC           <<
			   (AS_MEMATTR_INDEX_WRITE_ALLOC * 8)) |
			  0; /* The other indices are unused for now */

	if (NULL == kctx->mmu_teardown_pages)
		return MALI_ERROR_OUT_OF_MEMORY;

	return MALI_ERROR_NONE;
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
	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != kctx->mmu_teardown_pages);

	lockdep_assert_held(&kctx->reg_lock);

	mmu_teardown_level(kctx, kctx->pgd, MIDGARD_MMU_TOPLEVEL, 1, kctx->mmu_teardown_pages);

	beenthere(kctx, "pgd %lx", (unsigned long)kctx->pgd);
	kbase_mem_allocator_free(kctx->pgd_allocator, 1, &kctx->pgd, MALI_TRUE);
	kbase_process_page_usage_dec(kctx, 1);
	kbase_atomic_sub_pages(1, &kctx->used_pages);
	kbase_atomic_sub_pages(1, &kctx->kbdev->memdev.used_pages);
}

KBASE_EXPORT_TEST_API(kbase_mmu_free_pgd)

static size_t kbasep_mmu_dump_level(struct kbase_context *kctx, phys_addr_t pgd, int level, char ** const buffer, size_t *size_left)
{
	phys_addr_t target_pgd;
	u64 *pgd_page;
	int i;
	size_t size = KBASE_MMU_PAGE_ENTRIES * sizeof(u64) + sizeof(u64);
	size_t dump_size;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	lockdep_assert_held(&kctx->reg_lock);

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

	for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++) {
		if ((pgd_page[i] & ENTRY_IS_PTE) == ENTRY_IS_PTE) {
			target_pgd = mmu_pte_to_phy_addr(pgd_page[i]);

			dump_size = kbasep_mmu_dump_level(kctx, target_pgd, level + 1, buffer, size_left);
			if (!dump_size) {
				kunmap(pfn_to_page(PFN_DOWN(pgd)));
				return 0;
			}
			size += dump_size;
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

	lockdep_assert_held(&kctx->reg_lock);

	if (0 == nr_pages) {
		/* can't find in a 0 sized buffer, early out */
		return NULL;
	}

	size_left = nr_pages * PAGE_SIZE;

	KBASE_DEBUG_ASSERT(0 != size_left);
	kaddr = vmalloc_user(size_left);

	if (kaddr) {
		u64 end_marker = 0xFFULL;
		char *buffer = (char *)kaddr;

		size_t size = kbasep_mmu_dump_level(kctx, kctx->pgd, MIDGARD_MMU_TOPLEVEL, &buffer, &size_left);
		if (!size) {
			vfree(kaddr);
			return NULL;
		}

		/* Add on the size for the end marker */
		size += sizeof(u64);

		if (size > nr_pages * PAGE_SIZE || size_left < sizeof(u64)) {
			/* The buffer isn't big enough - free the memory and return failure */
			vfree(kaddr);
			return NULL;
		}

		/* Add the end marker */
		memcpy(buffer, &end_marker, sizeof(u64));
	}

	return kaddr;
}
KBASE_EXPORT_TEST_API(kbase_mmu_dump)

static void bus_fault_worker(struct work_struct *data)
{
	struct kbase_as *faulting_as;
	int as_no;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
#if KBASE_GPU_RESET_EN
	mali_bool reset_status = MALI_FALSE;
#endif /* KBASE_GPU_RESET_EN */

	faulting_as = container_of(data, struct kbase_as, work_busfault);

	as_no = faulting_as->number;

	kbdev = container_of(faulting_as, struct kbase_device, as[as_no]);

	/* Grab the context that was already refcounted in kbase_mmu_interrupt().
	 * Therefore, it cannot be scheduled out of this AS until we explicitly release it
	 *
	 * NOTE: NULL can be returned here if we're gracefully handling a spurious interrupt */
	kctx = kbasep_js_runpool_lookup_ctx_noretain(kbdev, as_no);
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
		struct kbase_mmu_setup *current_setup = &faulting_as->current_setup;

		/* switch to UNMAPPED mode, will abort all jobs and stop any hw counter dumping */
		/* AS transaction begin */
		mutex_lock(&kbdev->as[as_no].transaction_mutex);

		/* Set the MMU into unmapped mode */
		current_setup->transtab &= ~(u64)MMU_TRANSTAB_ADRMODE_MASK;
		current_setup->transtab |= AS_TRANSTAB_ADRMODE_UNMAPPED;

		/* Apply the new settings */
		kbase_mmu_hw_configure(kbdev, faulting_as, kctx);

		mutex_unlock(&kbdev->as[as_no].transaction_mutex);
		/* AS transaction end */

		kbase_mmu_hw_clear_fault(kbdev, faulting_as, kctx,
					 KBASE_MMU_FAULT_TYPE_BUS);
		kbase_mmu_hw_enable_fault(kbdev, faulting_as, kctx,
					 KBASE_MMU_FAULT_TYPE_BUS);

		kbase_pm_context_idle(kbdev);
	}
#if KBASE_GPU_RESET_EN
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8245) && reset_status)
		kbase_reset_gpu(kbdev);
#endif /* KBASE_GPU_RESET_EN */
	/* By this point, the fault was handled in some way, so release the ctx refcount */
	if (kctx != NULL)
		kbasep_js_runpool_release_ctx(kbdev, kctx);
}

const char *kbase_exception_name(u32 exception_code)
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
	default:
		e = "UNKNOWN";
		break;
	};

	return e;
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
	struct kbase_mmu_setup *current_setup;
	struct kbasep_js_device_data *js_devdata;

#if KBASE_GPU_RESET_EN
	mali_bool reset_status = MALI_FALSE;
#endif
	static const char * const access_type_names[] = { "RESERVED", "EXECUTE", "READ", "WRITE" };

	as_no = as->number;
	kbdev = kctx->kbdev;
	js_devdata = &kbdev->js_data;

	/* ASSERT that the context won't leave the runpool */
	KBASE_DEBUG_ASSERT(kbasep_js_debug_check_ctx_refcount(kbdev, kctx) > 0);

	/* decode the fault status */
	exception_type = as->fault_status & 0xFF;
	access_type = (as->fault_status >> 8) & 0x3;
	source_id = (as->fault_status >> 16);

	/* terminal fault, print info about the fault */
	dev_err(kbdev->dev,
		"Unhandled Page fault in AS%d at VA 0x%016llX\n"
		"Reason: %s\n"
		"raw fault status 0x%X\n"
		"decoded fault status: %s\n"
		"exception type 0x%X: %s\n"
		"access type 0x%X: %s\n"
		"source id 0x%X\n",
		as_no, as->fault_addr,
		reason_str,
		as->fault_status,
		(as->fault_status & (1 << 10) ? "DECODER FAULT" : "SLAVE FAULT"),
		exception_type, kbase_exception_name(exception_type),
		access_type, access_type_names[access_type],
		source_id);

	/* hardware counters dump fault handling */
	if ((kbdev->hwcnt.kctx) && (kbdev->hwcnt.kctx->as_nr == as_no) && (kbdev->hwcnt.state == KBASE_INSTR_STATE_DUMPING)) {
		unsigned int num_core_groups = kbdev->gpu_props.num_core_groups;
		if ((as->fault_addr >= kbdev->hwcnt.addr) && (as->fault_addr < (kbdev->hwcnt.addr + (num_core_groups * 2048))))
			kbdev->hwcnt.state = KBASE_INSTR_STATE_FAULT;
	}

	/* Stop the kctx from submitting more jobs and cause it to be scheduled
	 * out/rescheduled - this will occur on releasing the context's refcount */
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
	kbasep_js_clear_submit_allowed(js_devdata, kctx);
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	/* Kill any running jobs from the context. Submit is disallowed, so no more jobs from this
	 * context can appear in the job slots from this point on */
	kbase_job_kill_jobs_from_context(kctx);
	/* AS transaction begin */
	mutex_lock(&as->transaction_mutex);
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
	current_setup = &as->current_setup;

	current_setup->transtab &= ~(u64)MMU_TRANSTAB_ADRMODE_MASK;
	current_setup->transtab |= AS_TRANSTAB_ADRMODE_UNMAPPED;

	/* Apply the new address space setting */
	kbase_mmu_hw_configure(kbdev, as, kctx);

	mutex_unlock(&as->transaction_mutex);
	/* AS transaction end */

	/* Clear down the fault */
	kbase_mmu_hw_clear_fault(kbdev, as, kctx, KBASE_MMU_FAULT_TYPE_PAGE);
	kbase_mmu_hw_enable_fault(kbdev, as, kctx, KBASE_MMU_FAULT_TYPE_PAGE);

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
	mutex_lock(&as->transaction_mutex);
	/* Force a uTLB invalidate */
	kbase_mmu_hw_do_operation(kbdev, as, kctx, 0, 0,
				  AS_COMMAND_UNLOCK, 0);
	mutex_unlock(&as->transaction_mutex);
	/* AS transaction end */

	spin_lock_irqsave(&kbdev->js_data.runpool_irq.lock, flags);
	if (as->poke_refcount &&
		!(as->poke_state & KBASE_AS_POKE_STATE_KILLING_POKE)) {
		/* Only queue up the timer if we need it, and we're not trying to kill it */
		hrtimer_start(&as->poke_timer, HR_TIMER_DELAY_MSEC(5), HRTIMER_MODE_REL);
	}
	spin_unlock_irqrestore(&kbdev->js_data.runpool_irq.lock, flags);

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
 * The caller must hold kbasep_js_device_data::runpool_irq::lock
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
	lockdep_assert_held(&kbdev->js_data.runpool_irq.lock);

	if (katom->poking)
		return;

	katom->poking = 1;

	/* It's safe to work on the as/as_nr without an explicit reference,
	 * because the caller holds the runpool_irq lock, and the atom itself
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

	spin_lock_irqsave(&kbdev->js_data.runpool_irq.lock, flags);
	KBASE_DEBUG_ASSERT(as->poke_refcount > 0);
	KBASE_DEBUG_ASSERT(as->poke_state & KBASE_AS_POKE_STATE_IN_FLIGHT);

	if (--(as->poke_refcount) == 0) {
		as->poke_state |= KBASE_AS_POKE_STATE_KILLING_POKE;
		spin_unlock_irqrestore(&kbdev->js_data.runpool_irq.lock, flags);

		hrtimer_cancel(&as->poke_timer);
		flush_workqueue(as->poke_wq);

		spin_lock_irqsave(&kbdev->js_data.runpool_irq.lock, flags);

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
	spin_unlock_irqrestore(&kbdev->js_data.runpool_irq.lock, flags);

	katom->poking = 0;
}

void kbase_mmu_interrupt_process(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbase_as *as)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	unsigned long flags;

	if (kctx == NULL) {
		dev_warn(kbdev->dev, "%s in AS%d at 0x%016llx with no context present! Suprious IRQ or SW Design Error?\n",
				 kbase_as_has_bus_fault(as) ? "Bus error" : "Page fault",
				 as->number, as->fault_addr);
	}

	if (kbase_as_has_bus_fault(as)) {
		if (kctx) {
			/*
			 * hw counters dumping in progress, signal the
			 * other thread that it failed
			 */
			if ((kbdev->hwcnt.kctx == kctx) &&
			    (kbdev->hwcnt.state == KBASE_INSTR_STATE_DUMPING))
				kbdev->hwcnt.state = KBASE_INSTR_STATE_FAULT;

			/*
			 * Stop the kctx from submitting more jobs and cause it
			 * to be scheduled out/rescheduled when all references
			 * to it are released
			 */
			spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
			kbasep_js_clear_submit_allowed(js_devdata, kctx);
			spin_unlock_irqrestore(&js_devdata->runpool_irq.lock,
					       flags);

			dev_warn(kbdev->dev, "Bus error in AS%d at 0x%016llx\n",
					 as->number, as->fault_addr);
		}

		/*
		 * We need to switch to UNMAPPED mode - but we do this in a
		 * worker so that we can sleep
		 */
		KBASE_DEBUG_ASSERT(0 == object_is_on_stack(&as->work_busfault));
		INIT_WORK(&as->work_busfault, bus_fault_worker);
		queue_work(as->pf_wq, &as->work_busfault);
	} else {
		KBASE_DEBUG_ASSERT(0 == object_is_on_stack(&as->work_pagefault));
		INIT_WORK(&as->work_pagefault, page_fault_worker);
		queue_work(as->pf_wq, &as->work_pagefault);
	}
}
