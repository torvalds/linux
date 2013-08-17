/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_kbase_mmu.c
 * Base kernel MMU management.
 */

/* #define DEBUG    1 */
#include <osk/mali_osk.h>
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_midg_regmap.h>
#include <kbase/src/common/mali_kbase_gator.h>

#define beenthere(f, a...)  OSK_PRINT_INFO(OSK_BASE_MMU, "%s:" f, __func__, ##a)

#include <kbase/src/common/mali_kbase_defs.h>
#include <kbase/src/common/mali_kbase_hw.h>

#define KBASE_MMU_PAGE_ENTRIES 512

/*
 * Definitions:
 * - PGD: Page Directory.
 * - PTE: Page Table Entry. A 64bit value pointing to the next
 *        level of translation
 * - ATE: Address Transation Entry. A 64bit value pointing to
 *        a 4kB physical page.
 */

static void kbase_mmu_report_fault_and_kill(kbase_context *kctx, kbase_as * as, mali_addr64 fault_addr);
static u64 lock_region(kbase_device * kbdev, u64 pfn, u32 num_pages);

static void ksync_kern_vrange_gpu(osk_phy_addr paddr, osk_virt_addr vaddr, size_t size)
{
	osk_sync_to_memory(paddr, vaddr, size);
}

static u32 make_multiple(u32 minimum, u32 multiple)
{
	u32 remainder = minimum % multiple;
	if (remainder == 0)
	{
		return minimum;
	}
	else
	{
		return minimum + multiple - remainder;
	}
}

static void mmu_mask_reenable(kbase_device * kbdev, kbase_context *kctx, kbase_as * as)
{
	unsigned long flags;
	u32 mask;
	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);
	mask = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK), kctx);
	mask |= ((1UL << as->number) | (1UL << (MMU_REGS_BUS_ERROR_FLAG(as->number))));
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), mask, kctx);
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}

static void page_fault_worker(struct work_struct *data)
{
	u64 fault_pfn;
	u32 new_pages;
	u32 fault_rel_pfn;
	kbase_as * faulting_as;
	int as_no;
	kbase_context * kctx;
	kbase_device * kbdev;
	kbase_va_region *region;
	mali_error err;

	u32 fault_status;

	faulting_as = container_of(data, kbase_as, work_pagefault);
	fault_pfn = faulting_as->fault_addr >> PAGE_SHIFT;
	as_no = faulting_as->number;

	kbdev = container_of( faulting_as, kbase_device, as[as_no] );

	/* Grab the context that was already refcounted in kbase_mmu_interrupt().
	 * Therefore, it cannot be scheduled out of this AS until we explicitly release it
	 *
	 * NOTE: NULL can be returned here if we're gracefully handling a spurious interrupt */
	kctx = kbasep_js_runpool_lookup_ctx_noretain( kbdev, as_no );

	if ( kctx == NULL )
	{
		/* Address space has no context, terminate the work */
		u32 reg;
		/* AS transaction begin */
		mutex_lock(&faulting_as->transaction_mutex);
		reg = kbase_reg_read(kbdev, MMU_AS_REG(as_no, ASn_TRANSTAB_LO), NULL);
		reg = (reg & (~(u32)MMU_TRANSTAB_ADRMODE_MASK)) | ASn_TRANSTAB_ADRMODE_UNMAPPED;
		kbase_reg_write(kbdev, MMU_AS_REG(as_no, ASn_TRANSTAB_LO), reg, NULL);
		kbase_reg_write(kbdev, MMU_AS_REG(as_no, ASn_COMMAND), ASn_COMMAND_UPDATE, NULL);
		kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), (1UL << as_no), NULL);
		mutex_unlock(&faulting_as->transaction_mutex);
		/* AS transaction end */

		mmu_mask_reenable(kbdev, NULL, faulting_as);
		return;
	}

	fault_status = kbase_reg_read(kbdev, MMU_AS_REG(as_no, ASn_FAULTSTATUS), NULL);

	OSK_ASSERT( kctx->kbdev == kbdev );

	kbase_gpu_vm_lock(kctx);

	/* find the region object for this VA */
	region = kbase_region_tracker_find_region_enclosing_address(kctx, faulting_as->fault_addr);
	if (NULL == region || (GROWABLE_FLAGS_REQUIRED != (region->flags & GROWABLE_FLAGS_MASK)))
	{
		kbase_gpu_vm_unlock(kctx);
		/* failed to find the region or mismatch of the flags */
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, faulting_as->fault_addr);
		goto fault_done;
	}

	if ((((fault_status & ASn_FAULTSTATUS_ACCESS_TYPE_MASK) == ASn_FAULTSTATUS_ACCESS_TYPE_READ) &&
	        !(region->flags & KBASE_REG_GPU_RD)) ||
	    (((fault_status & ASn_FAULTSTATUS_ACCESS_TYPE_MASK) == ASn_FAULTSTATUS_ACCESS_TYPE_WRITE) &&
	        !(region->flags & KBASE_REG_GPU_WR)) ||
	    (((fault_status & ASn_FAULTSTATUS_ACCESS_TYPE_MASK) == ASn_FAULTSTATUS_ACCESS_TYPE_EX) &&
	        (region->flags & KBASE_REG_GPU_NX)))
	{
		OSK_PRINT_WARN(OSK_BASE_MMU, "Access permissions don't match: region->flags=0x%x", region->flags);
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, faulting_as->fault_addr);
		goto fault_done;
	}

	/* find the size we need to grow it by */
	/* we know the result fit in a u32 due to kbase_region_tracker_find_region_enclosing_address
	 * validating the fault_adress to be within a u32 from the start_pfn */
	fault_rel_pfn = fault_pfn - region->start_pfn;
	
	if (fault_rel_pfn < region->nr_alloc_pages)
	{
		OSK_PRINT_WARN(OSK_BASE_MMU, "Fault in allocated region of growable TMEM: Ignoring");
		kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), (1UL << as_no), NULL);
		mmu_mask_reenable(kbdev, kctx, faulting_as);
		kbase_gpu_vm_unlock(kctx);
		goto fault_done;
	}

	new_pages = make_multiple(fault_rel_pfn - region->nr_alloc_pages + 1, region->extent);
	if (new_pages + region->nr_alloc_pages > region->nr_pages)
	{
		/* cap to max vsize */
		new_pages = region->nr_pages - region->nr_alloc_pages;
	}

	if (0 == new_pages)
	{
		/* Duplicate of a fault we've already handled, nothing to do */
		kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), (1UL << as_no), NULL);
		mmu_mask_reenable(kbdev, kctx, faulting_as);
		kbase_gpu_vm_unlock(kctx);
		goto fault_done;
	}

	if (MALI_ERROR_NONE == kbase_alloc_phy_pages_helper(region, new_pages))
	{
		/* alloc success */
		mali_addr64 lock_addr;
		OSK_ASSERT(region->nr_alloc_pages <= region->nr_pages);

		/* AS transaction begin */
		mutex_lock(&faulting_as->transaction_mutex);

		/* Lock the VA region we're about to update */
		lock_addr = lock_region(kbdev, faulting_as->fault_addr >> PAGE_SHIFT, new_pages);
		kbase_reg_write(kbdev, MMU_AS_REG(as_no, ASn_LOCKADDR_LO), lock_addr & 0xFFFFFFFFUL, kctx);
		kbase_reg_write(kbdev, MMU_AS_REG(as_no, ASn_LOCKADDR_HI), lock_addr >> 32, kctx);
		kbase_reg_write(kbdev, MMU_AS_REG(as_no, ASn_COMMAND), ASn_COMMAND_LOCK, kctx);

		/* set up the new pages */
		err = kbase_mmu_insert_pages(kctx, region->start_pfn + region->nr_alloc_pages - new_pages,
		                             &region->phy_pages[region->nr_alloc_pages - new_pages],
		                             new_pages, region->flags);
		if(MALI_ERROR_NONE != err)
		{
			/* failed to insert pages, handle as a normal PF */
			mutex_unlock(&faulting_as->transaction_mutex);
			kbase_gpu_vm_unlock(kctx);
			/* The locked VA region will be unlocked and the cache invalidated in here */
			kbase_mmu_report_fault_and_kill(kctx, faulting_as, faulting_as->fault_addr);
			goto fault_done;
		}

#ifdef CONFIG_MALI_GATOR_SUPPORT
		kbase_trace_mali_page_fault_insert_pages(as_no, new_pages);
#endif /* CONFIG_MALI_GATOR_SUPPORT */
		/* clear the irq */
		/* MUST BE BEFORE THE FLUSH/UNLOCK */
		kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), (1UL << as_no), NULL);

		/* flush L2 and unlock the VA (resumes the MMU) */
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_6367))
		{
			kbase_reg_write(kbdev, MMU_AS_REG(as_no, ASn_COMMAND), ASn_COMMAND_FLUSH, kctx);
		}
		else
		{
			kbase_reg_write(kbdev, MMU_AS_REG(as_no, ASn_COMMAND), ASn_COMMAND_FLUSH_PT, kctx);
		}

		/* wait for the flush to complete */
		while (kbase_reg_read(kbdev, MMU_AS_REG(as_no, ASn_STATUS), kctx) & 1);

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_9630))
		{
			/* Issue an UNLOCK command to ensure that valid page tables are re-read by the GPU after an update.
			Note that, the FLUSH command should perform all the actions necessary, however the bus logs show
			that if multiple page faults occur within an 8 page region the MMU does not always re-read the
			updated page table entries for later faults or is only partially read, it subsequently raises the
			page fault IRQ for the same addresses, the unlock ensures that the MMU cache is flushed, so updates
			can be re-read.  As the region is now unlocked we need to issue 2 UNLOCK commands in order to flush the
			MMU/uTLB, see PRLAM-8812.
                        */
			kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_COMMAND), ASn_COMMAND_UNLOCK, kctx);
			kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_COMMAND), ASn_COMMAND_UNLOCK, kctx);
		}

		mutex_unlock(&faulting_as->transaction_mutex);
		/* AS transaction end */

		/* reenable this in the mask */
		mmu_mask_reenable(kbdev, kctx, faulting_as);
		kbase_gpu_vm_unlock(kctx);
	}
	else
	{
		/* failed to extend, handle as a normal PF */
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, faulting_as->fault_addr);
	}


fault_done:
	/* By this point, the fault was handled in some way, so release the ctx refcount */
	kbasep_js_runpool_release_ctx( kbdev, kctx );
}

osk_phy_addr kbase_mmu_alloc_pgd(kbase_context *kctx)
{
	osk_phy_addr pgd;
	u64 *page;
	int i;
	OSK_ASSERT( NULL != kctx);
	if (MALI_ERROR_NONE != kbase_mem_usage_request_pages(&kctx->usage, 1))
	{
		return 0;
	}
	if (MALI_ERROR_NONE != kbase_mem_allocator_alloc(kctx->pgd_allocator, 1, &pgd, 0))
	{
		kbase_mem_usage_release_pages(&kctx->usage, 1);
		return 0;
	}

	page = osk_kmap(pgd);
	if(NULL == page)
	{
		kbase_mem_allocator_free(kctx->pgd_allocator, 1, &pgd);
		kbase_mem_usage_release_pages(&kctx->usage, 1);
		return 0;
	}

	kbase_process_page_usage_inc(kctx, 1);

	for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++)
		page[i] = ENTRY_IS_INVAL;

	/* Clean the full page */
	ksync_kern_vrange_gpu(pgd, page, KBASE_MMU_PAGE_ENTRIES * sizeof(u64));
	osk_kunmap(pgd, page);
	return pgd;
}
KBASE_EXPORT_TEST_API(kbase_mmu_alloc_pgd)

static osk_phy_addr mmu_pte_to_phy_addr(u64 entry)
{
	if (!(entry & 1))
		return 0;

	return entry & ~0xFFF;
}

static u64 mmu_phyaddr_to_pte(osk_phy_addr phy)
{
	return (phy & ~0xFFF) | ENTRY_IS_PTE;
}

static u64 mmu_phyaddr_to_ate(osk_phy_addr phy, u64 flags)
{
	return (phy & ~0xFFF) | (flags & ENTRY_FLAGS_MASK) | ENTRY_IS_ATE;
}

/* Given PGD PFN for level N, return PGD PFN for level N+1 */
static osk_phy_addr mmu_get_next_pgd(kbase_context *kctx,
                                     osk_phy_addr pgd, u64 vpfn, int level)
{
	u64 *page;
	osk_phy_addr target_pgd;

	OSK_ASSERT(pgd);

	/*
	 * Architecture spec defines level-0 as being the top-most.
	 * This is a bit unfortunate here, but we keep the same convention.
	 */
	vpfn >>= (3 - level) * 9;
	vpfn &= 0x1FF;

	page = osk_kmap(pgd);
	if(NULL == page)
	{
		OSK_PRINT_WARN(OSK_BASE_MMU, "mmu_get_next_pgd: kmap failure\n");
		return 0;
	}

	target_pgd = mmu_pte_to_phy_addr(page[vpfn]);

	if (!target_pgd) {
		target_pgd = kbase_mmu_alloc_pgd(kctx);
		if(!target_pgd)
		{
			OSK_PRINT_WARN(OSK_BASE_MMU, "mmu_get_next_pgd: kbase_mmu_alloc_pgd failure\n");
			osk_kunmap(pgd, page);
			return 0;
		}
		
		page[vpfn] = mmu_phyaddr_to_pte(target_pgd);
		ksync_kern_vrange_gpu(pgd + (vpfn * sizeof(u64)), page + vpfn, sizeof(u64));
		/* Rely on the caller to update the address space flags. */
	}

	osk_kunmap(pgd, page);
	return target_pgd;
}

static osk_phy_addr mmu_get_bottom_pgd(kbase_context *kctx, u64 vpfn)
{
	osk_phy_addr pgd;
	int l;

	pgd = kctx->pgd;

	for (l = MIDGARD_MMU_TOPLEVEL; l < 3; l++) {
		pgd = mmu_get_next_pgd(kctx, pgd, vpfn, l);
		/* Handle failure condition */
		if(!pgd)
		{
			OSK_PRINT_WARN(OSK_BASE_MMU, "mmu_get_bottom_pgd: mmu_get_next_pgd failure\n");
			return 0;
		}
	}

	return pgd;
}

static osk_phy_addr mmu_insert_pages_recover_get_next_pgd(kbase_context *kctx,
                                                          osk_phy_addr pgd, u64 vpfn, int level)
{
	u64 *page;
	osk_phy_addr target_pgd;

	OSK_ASSERT(pgd);
	CSTD_UNUSED(kctx);

	/*
	 * Architecture spec defines level-0 as being the top-most.
	 * This is a bit unfortunate here, but we keep the same convention.
	 */
	vpfn >>= (3 - level) * 9;
	vpfn &= 0x1FF;

	page = osk_kmap_atomic(pgd);
	/* osk_kmap_atomic should NEVER fail */
	OSK_ASSERT(NULL != page);

	target_pgd = mmu_pte_to_phy_addr(page[vpfn]);
	/* As we are recovering from what has already been set up, we should have a target_pgd */
	OSK_ASSERT(0 != target_pgd);

	osk_kunmap_atomic(pgd, page);
	return target_pgd;
}

static osk_phy_addr mmu_insert_pages_recover_get_bottom_pgd(kbase_context *kctx, u64 vpfn)
{
	osk_phy_addr pgd;
	int l;

	pgd = kctx->pgd;

	for (l = MIDGARD_MMU_TOPLEVEL; l < 3; l++) {
		pgd = mmu_insert_pages_recover_get_next_pgd(kctx, pgd, vpfn, l);
		/* Should never fail */
		OSK_ASSERT(0 != pgd);
	}

	return pgd;
}

static void mmu_insert_pages_failure_recovery(kbase_context *kctx, u64 vpfn,
                                              osk_phy_addr *phys, u32 nr)
{
	osk_phy_addr pgd;
	u64 *pgd_page;

	OSK_ASSERT( NULL != kctx );
	OSK_ASSERT( 0 != vpfn );
	OSK_ASSERT( vpfn <= (UINT64_MAX / PAGE_SIZE) ); /* 64-bit address range is the max */

	while (nr) {
		u32 i;
		u32 index = vpfn & 0x1FF;
		u32 count = KBASE_MMU_PAGE_ENTRIES - index;
		
		if (count > nr)
		{
			count = nr;
		}

		pgd = mmu_insert_pages_recover_get_bottom_pgd(kctx, vpfn);
		OSK_ASSERT(0 != pgd);

		pgd_page = osk_kmap_atomic(pgd);
		OSK_ASSERT(NULL != pgd_page);

		/* Invalidate the entries we added */
		for (i = 0; i < count; i++) {
			pgd_page[index + i] = ENTRY_IS_INVAL;
		}

		phys += count;
		vpfn += count;
		nr -= count;

		ksync_kern_vrange_gpu(pgd + (index * sizeof(u64)), pgd_page + index, count * sizeof(u64));

		osk_kunmap_atomic(pgd, pgd_page);
	}
}

/*
 * Map 'nr' pages pointed to by 'phys' at GPU PFN 'vpfn'
 */
mali_error kbase_mmu_insert_pages(kbase_context *kctx, u64 vpfn,
                                  osk_phy_addr *phys, u32 nr, u32 flags)
{
	osk_phy_addr pgd;
	u64 *pgd_page;
	u64 mmu_flags = 0;
	/* In case the insert_pages only partially completes we need to be able to recover */
	mali_bool recover_required = MALI_FALSE;
	u64 recover_vpfn = vpfn;
	osk_phy_addr *recover_phys = phys;
	u32 recover_count = 0;

	OSK_ASSERT( NULL != kctx );
	OSK_ASSERT( 0 != vpfn );
	OSK_ASSERT( (flags & ~((1 << KBASE_REG_FLAGS_NR_BITS) - 1)) == 0 );
	OSK_ASSERT( vpfn <= (UINT64_MAX / PAGE_SIZE) ); /* 64-bit address range is the max */

	mmu_flags |= (flags & KBASE_REG_GPU_WR) ? ENTRY_WR_BIT : 0; /* write perm if requested */
	mmu_flags |= (flags & KBASE_REG_GPU_RD) ? ENTRY_RD_BIT : 0; /* read perm if requested */
	mmu_flags |= (flags & KBASE_REG_GPU_NX) ? ENTRY_NX_BIT : 0; /* nx if requested */
	
	if (flags & KBASE_REG_SHARE_BOTH)
	{
		/* inner and outer shareable */
		mmu_flags |= SHARE_BOTH_BITS;
	}
	else if (flags & KBASE_REG_SHARE_IN)
	{
		/* inner shareable coherency */
		mmu_flags |= SHARE_INNER_BITS;
	}

	while (nr) {
		u32 i;
		u32 index = vpfn & 0x1FF;
		u32 count = KBASE_MMU_PAGE_ENTRIES - index;
		
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
		if(!pgd)
		{
			OSK_PRINT_WARN(OSK_BASE_MMU, "kbase_mmu_insert_pages: mmu_get_bottom_pgd failure\n");
			if(recover_required)
			{
				/* Invalidate the pages we have partially completed */
				mmu_insert_pages_failure_recovery(kctx, recover_vpfn, recover_phys, recover_count);
			}
			return MALI_ERROR_FUNCTION_FAILED;
		}

		pgd_page = osk_kmap(pgd);
		if(!pgd_page)
		{
			OSK_PRINT_WARN(OSK_BASE_MMU, "kbase_mmu_insert_pages: kmap failure\n");
			if(recover_required)
			{
				/* Invalidate the pages we have partially completed */
				mmu_insert_pages_failure_recovery(kctx, recover_vpfn, recover_phys, recover_count);
			}
			return MALI_ERROR_OUT_OF_MEMORY;
		}

		for (i = 0; i < count; i++) {
			OSK_ASSERT(0 == (pgd_page[index + i] & 1UL));
			pgd_page[index + i] = mmu_phyaddr_to_ate(phys[i], mmu_flags);
		}

		phys += count;
		vpfn += count;
		nr -= count;

		ksync_kern_vrange_gpu(pgd + (index * sizeof(u64)), pgd_page + index, count * sizeof(u64));

		osk_kunmap(pgd, pgd_page);
		/* We have started modifying the page table. If further pages need inserting and fail we need to
		 * undo what has already taken place */
		recover_required = MALI_TRUE;
		recover_count+= count;
	}
	return MALI_ERROR_NONE;
}
KBASE_EXPORT_TEST_API(kbase_mmu_insert_pages)

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
 mali_error kbase_mmu_teardown_pages(kbase_context *kctx, u64 vpfn, u32 nr)
{
	osk_phy_addr pgd;
	u64 *pgd_page;
	kbase_device *kbdev;
	mali_bool ctx_is_in_runpool;
	u32 requested_nr = nr;

	beenthere("kctx %p vpfn %lx nr %d", (void *)kctx, (unsigned long)vpfn, nr);

	OSK_ASSERT(NULL != kctx);

	if (0 == nr)
	{
		/* early out if nothing to do */
		return MALI_ERROR_NONE;
	}

	kbdev = kctx->kbdev;

	while (nr)
	{
		u32 i;
		u32 index = vpfn & 0x1FF;
		u32 count = KBASE_MMU_PAGE_ENTRIES - index;
		if (count > nr)
			count = nr;

		pgd = mmu_get_bottom_pgd(kctx, vpfn);
		if(!pgd)
		{
			OSK_PRINT_WARN(OSK_BASE_MMU, "kbase_mmu_teardown_pages: mmu_get_bottom_pgd failure\n");
			return MALI_ERROR_FUNCTION_FAILED;
		}

		pgd_page = osk_kmap(pgd);
		if(!pgd_page)
		{
			OSK_PRINT_WARN(OSK_BASE_MMU, "kbase_mmu_teardown_pages: kmap failure\n");
			return MALI_ERROR_OUT_OF_MEMORY;
		}

		for (i = 0; i < count; i++) {
			/*
			 * Possible micro-optimisation: only write to the
			 * low 32bits. That's enough to invalidate the mapping.
			 */
			pgd_page[index + i] = ENTRY_IS_INVAL;
		}

		vpfn += count;
		nr -= count;

		ksync_kern_vrange_gpu(pgd + (index * sizeof(u64)), pgd_page + index, count * sizeof(u64));

		osk_kunmap(pgd, pgd_page);
	}

	/* We must flush if we're currently running jobs. At the very least, we need to retain the
	 * context to ensure it doesn't schedule out whilst we're trying to flush it */
	ctx_is_in_runpool = kbasep_js_runpool_retain_ctx( kbdev, kctx );

	if ( ctx_is_in_runpool )
	{
		OSK_ASSERT( kctx->as_nr != KBASEP_AS_NR_INVALID );

		/* Second level check is to try to only do this when jobs are running. The refcount is
		 * a heuristic for this. */
		if ( kbdev->js_data.runpool_irq.per_as_data[kctx->as_nr].as_busy_refcount >= 2 )
		{
			/* Lock the VA region we're about to update */
			u64 lock_addr = lock_region(kbdev, vpfn, requested_nr);
			u32 max_loops = KBASE_AS_FLUSH_MAX_LOOPS;

			/* AS transaction begin */
			mutex_lock(&kbdev->as[kctx->as_nr].transaction_mutex);
			kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_LOCKADDR_LO), lock_addr & 0xFFFFFFFFUL, kctx);
			kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_LOCKADDR_HI), lock_addr >> 32, kctx);
			kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_COMMAND), ASn_COMMAND_LOCK, kctx);

			/* flush L2 and unlock the VA */
			if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_6367))
			{
				kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_COMMAND), ASn_COMMAND_FLUSH, kctx);
			}
			else
			{
				kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_COMMAND), ASn_COMMAND_FLUSH_MEM, kctx);
			}

			/* wait for the flush to complete */
			while (--max_loops &&
			       kbase_reg_read(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_STATUS), kctx) & ASn_STATUS_FLUSH_ACTIVE)
			{
			}

			if (!max_loops)
			{
				/* Flush failed to complete, assume the GPU has hung and perform a reset to recover */
				OSK_PRINT_ERROR(OSK_BASE_MMU, "ASn_STATUS_FLUSH_ACTIVE stuck set. Resetting to recover\n");
				if (kbase_prepare_to_reset_gpu(kbdev))
				{
					kbase_reset_gpu(kbdev);
				}
			}

			if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_9630))
			{
				/* Issue an UNLOCK command to ensure that valid page tables are re-read by the GPU after an update.
				Note that, the FLUSH command should perform all the actions necessary, however the bus logs show
				that if multiple page faults occur within an 8 page region the MMU does not always re-read the
				updated page table entries for later faults or is only partially read, it subsequently raises the
				page fault IRQ for the same addresses, the unlock ensures that the MMU cache is flushed, so updates
				can be re-read.  As the region is now unlocked we need to issue 2 UNLOCK commands in order to flush the
				MMU/uTLB, see PRLAM-8812.
                                */
				kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_COMMAND), ASn_COMMAND_UNLOCK, kctx);
				kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_COMMAND), ASn_COMMAND_UNLOCK, kctx);
			}

			mutex_unlock(&kbdev->as[kctx->as_nr].transaction_mutex);
			/* AS transaction end */
		}
		kbasep_js_runpool_release_ctx( kbdev, kctx );
	}
	return MALI_ERROR_NONE;
}
KBASE_EXPORT_TEST_API(kbase_mmu_teardown_pages)

static int mmu_pte_is_valid(u64 pte)
{
	return ((pte & 3) == ENTRY_IS_ATE);
}

/* This is a debug feature only */
static void mmu_check_unused(kbase_context *kctx, osk_phy_addr pgd)
{
	u64 *page;
	int i;
	CSTD_UNUSED(kctx);

	page = osk_kmap_atomic(pgd);
	/* kmap_atomic should NEVER fail. */
	OSK_ASSERT(NULL != page);

	for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++)
	{
		if (mmu_pte_is_valid(page[i]))
		{
			beenthere("live pte %016lx", (unsigned long)page[i]);
		}
	}
	osk_kunmap_atomic(pgd, page);
}

static void mmu_teardown_level(kbase_context *kctx, osk_phy_addr pgd, int level, int zap, u64 *pgd_page_buffer)
{
	osk_phy_addr target_pgd;
	u64 *pgd_page;
	int i;

	pgd_page = osk_kmap_atomic(pgd);
	/* kmap_atomic should NEVER fail. */
	OSK_ASSERT(NULL != pgd_page);
	/* Copy the page to our preallocated buffer so that we can minimize kmap_atomic usage */
	memcpy(pgd_page_buffer, pgd_page, PAGE_SIZE);
	osk_kunmap_atomic(pgd, pgd_page);
	pgd_page = pgd_page_buffer;

	for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++) {
		target_pgd = mmu_pte_to_phy_addr(pgd_page[i]);

		if (target_pgd) {
			if (level < 2)
			{
				mmu_teardown_level(kctx, target_pgd, level + 1, zap, pgd_page_buffer+(PAGE_SIZE/sizeof(u64)));
			}
			else {
				/*
				 * So target_pte is a level-3 page.
				 * As a leaf, it is safe to free it.
				 * Unless we have live pages attached to it!
				 */
				mmu_check_unused(kctx, target_pgd);
			}

			beenthere("pte %lx level %d", (unsigned long)target_pgd, level + 1);
			if (zap)
			{
				kbase_mem_allocator_free(kctx->pgd_allocator, 1, &target_pgd);
				kbase_process_page_usage_dec(kctx, 1);
				kbase_mem_usage_release_pages(&kctx->usage, 1);
			}
		}
	}
}

mali_error kbase_mmu_init(kbase_context *kctx)
{
	OSK_ASSERT(NULL != kctx);
	OSK_ASSERT(NULL == kctx->mmu_teardown_pages);

	/* Preallocate MMU depth of four pages for mmu_teardown_level to use */
	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		kctx->mmu_teardown_pages = NULL;
	}
	else
	{
		kctx->mmu_teardown_pages = kmalloc(PAGE_SIZE*4, GFP_KERNEL);
	}

	if(NULL == kctx->mmu_teardown_pages)
	{
		return MALI_ERROR_OUT_OF_MEMORY;
	}
	return MALI_ERROR_NONE;
}

void kbase_mmu_term(kbase_context *kctx)
{
	OSK_ASSERT(NULL != kctx);
	OSK_ASSERT(NULL != kctx->mmu_teardown_pages);

	kfree(kctx->mmu_teardown_pages);
	kctx->mmu_teardown_pages = NULL;
}

void kbase_mmu_free_pgd(kbase_context *kctx)
{
	OSK_ASSERT(NULL != kctx);
	OSK_ASSERT(NULL != kctx->mmu_teardown_pages);

	mmu_teardown_level(kctx, kctx->pgd, MIDGARD_MMU_TOPLEVEL, 1, kctx->mmu_teardown_pages);

	beenthere("pgd %lx", (unsigned long)kctx->pgd);
	kbase_mem_allocator_free(kctx->pgd_allocator, 1, &kctx->pgd);
	kbase_process_page_usage_dec(kctx, 1 );
	kbase_mem_usage_release_pages(&kctx->usage, 1);
}
KBASE_EXPORT_TEST_API(kbase_mmu_free_pgd)

static size_t kbasep_mmu_dump_level(kbase_context *kctx, osk_phy_addr pgd, int level, char **buffer, size_t *size_left)
{
	osk_phy_addr target_pgd;
	u64 *pgd_page;
	int i;
	size_t size = KBASE_MMU_PAGE_ENTRIES*sizeof(u64)+sizeof(u64);
	size_t dump_size;

	pgd_page = osk_kmap(pgd);
	if(!pgd_page)
	{
		OSK_PRINT_WARN(OSK_BASE_MMU, "kbasep_mmu_dump_level: kmap failure\n");
		return 0;
	}

	if (*size_left >= size)
	{
		/* A modified physical address that contains the page table level */
		u64 m_pgd = pgd | level;

		/* Put the modified physical address in the output buffer */
		memcpy(*buffer, &m_pgd, sizeof(m_pgd));
		*buffer += sizeof(m_pgd);

		/* Followed by the page table itself */
		memcpy(*buffer, pgd_page, sizeof(u64)*KBASE_MMU_PAGE_ENTRIES);
		*buffer += sizeof(u64)*KBASE_MMU_PAGE_ENTRIES;

		*size_left -= size;
	}
	
	for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++) {
		if ((pgd_page[i] & ENTRY_IS_PTE) == ENTRY_IS_PTE) {
			target_pgd = mmu_pte_to_phy_addr(pgd_page[i]);

			 dump_size = kbasep_mmu_dump_level(kctx, target_pgd, level + 1, buffer, size_left);
			 if(!dump_size)
			 {
				osk_kunmap(pgd, pgd_page);
			 	return 0;
			 }
			size += dump_size;
		}
	}

	osk_kunmap(pgd, pgd_page);

	return size;
}

void *kbase_mmu_dump(kbase_context *kctx,int nr_pages)
{
	void *kaddr;
	size_t size_left;

	OSK_ASSERT(kctx);

	if (0 == nr_pages)
	{
		/* can't find in a 0 sized buffer, early out */
		return NULL;
	}

	size_left = nr_pages * PAGE_SIZE;

	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		kaddr = NULL;
	}
	else
	{
		OSK_ASSERT(0 != size_left);
		kaddr = vmalloc_user(size_left);
	}

	if (kaddr)
	{
		u64 end_marker = 0xFFULL;
		char *buffer = (char*)kaddr;

		size_t size = kbasep_mmu_dump_level(kctx, kctx->pgd, MIDGARD_MMU_TOPLEVEL, &buffer, &size_left);
		if(!size)
		{
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

static u64 lock_region(kbase_device *kbdev, u64 pfn, u32 num_pages)
{
	u64 region;

	/* can't lock a zero sized range */
	OSK_ASSERT(num_pages);

	region = pfn << PAGE_SHIFT;
	/*
	 * osk_clz returns (given the ASSERT above):
	 * 32-bit: 0 .. 31
	 * 64-bit: 0 .. 63
	 *
	 * 32-bit: 32 + 10 - osk_clz(num_pages)
	 * results in the range (11 .. 42)
	 * 64-bit: 64 + 10 - osk_clz(num_pages)
	 * results in the range (11 .. 42)
	 */

	/* gracefully handle num_pages being zero */
	if (0 == num_pages)
	{
		region |= 11;
	}
	else
	{
		u8 region_width;
		region_width = ( OSK_BITS_PER_LONG + 10 - osk_clz(num_pages) );
		if (num_pages != (1ul << (region_width - 11)))
		{
			/* not pow2, so must go up to the next pow2 */
			region_width += 1;
		}
		OSK_ASSERT(region_width <= KBASE_LOCK_REGION_MAX_SIZE);
		OSK_ASSERT(region_width >= KBASE_LOCK_REGION_MIN_SIZE);
		region |= region_width;
	}

	return region;
}

static void bus_fault_worker(struct work_struct *data)
{
	const int num_as = 16;
	kbase_as * faulting_as;
	int as_no;
	kbase_context * kctx;
	kbase_device * kbdev;
	u32 reg;
	mali_bool reset_status= MALI_FALSE;

	faulting_as = container_of(data, kbase_as, work_busfault);
	as_no = faulting_as->number;

	kbdev = container_of( faulting_as, kbase_device, as[as_no] );

	/* Grab the context that was already refcounted in kbase_mmu_interrupt().
	 * Therefore, it cannot be scheduled out of this AS until we explicitly release it
	 *
	 * NOTE: NULL can be returned here if we're gracefully handling a spurious interrupt */
	kctx = kbasep_js_runpool_lookup_ctx_noretain( kbdev, as_no );

	/* switch to UNMAPPED mode, will abort all jobs and stop any hw counter dumping */
	/* AS transaction begin */
	mutex_lock(&kbdev->as[as_no].transaction_mutex);

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8245))
	{
		/* Due to H/W issue 8245 we need to reset the GPU after using UNMAPPED mode.
		 * We start the reset before switching to UNMAPPED to ensure that unrelated jobs
		 * are evicted from the GPU before the switch.
		 */
		OSK_PRINT_WARN(OSK_BASE_MMU, "NOTE: GPU will now be reset as a workaround for a hardware issue*");
		reset_status = kbase_prepare_to_reset_gpu(kbdev);
	}

	reg = kbase_reg_read(kbdev, MMU_AS_REG(as_no, ASn_TRANSTAB_LO), kctx);
	reg &= ~3;
	kbase_reg_write(kbdev, MMU_AS_REG(as_no, ASn_TRANSTAB_LO), reg, kctx);
	kbase_reg_write(kbdev, MMU_AS_REG(as_no, ASn_COMMAND), ASn_COMMAND_UPDATE, kctx);

	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), (1UL << as_no) | (1UL << (as_no + num_as))  , NULL);
	mutex_unlock(&kbdev->as[as_no].transaction_mutex);
	/* AS transaction end */

	mmu_mask_reenable( kbdev, kctx, faulting_as );
	
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8245) && reset_status)
	{
		kbase_reset_gpu(kbdev);
	}

	/* By this point, the fault was handled in some way, so release the ctx refcount */
	if ( kctx != NULL )
	{
		kbasep_js_runpool_release_ctx( kbdev, kctx );
	}
}

void kbase_mmu_interrupt(kbase_device * kbdev, u32 irq_stat)
{
	unsigned long flags;
	const int num_as = 16;
	kbasep_js_device_data *js_devdata;
	const int busfault_shift = 16;
	const int pf_shift = 0;
	const unsigned long mask = (1UL << num_as) - 1;
	
	u64 fault_addr;
	u32 new_mask;
	u32 tmp;

	u32 bf_bits = (irq_stat >> busfault_shift) & mask; /* bus faults */
	/* Ignore ASes with both pf and bf */
	u32 pf_bits = ((irq_stat >> pf_shift) & mask) & ~bf_bits;  /* page faults */

	OSK_ASSERT( NULL != kbdev);

	js_devdata = &kbdev->js_data;

	/* remember current mask */
	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);
	new_mask = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK), NULL);
	/* mask interrupts for now */
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), 0, NULL);
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);

	while (bf_bits)
	{
		kbase_as * as;
		int as_no;
		kbase_context * kctx;

		/* the while logic ensures we have a bit set, no need to check for not-found here */
		as_no = osk_find_first_set_bit(bf_bits);

		/* Refcount the kctx ASAP - it shouldn't disappear anyway, since Bus/Page faults
		 * _should_ only occur whilst jobs are running, and a job causing the Bus/Page fault
		 * shouldn't complete until the MMU is updated */
		kctx = kbasep_js_runpool_lookup_ctx( kbdev, as_no );

		/* mark as handled */
		bf_bits &= ~(1UL << as_no);

		/* find faulting address */
		fault_addr = kbase_reg_read(kbdev, MMU_AS_REG(as_no, ASn_FAULTADDRESS_HI), kctx);
		fault_addr <<= 32;
		fault_addr |= kbase_reg_read(kbdev, MMU_AS_REG(as_no, ASn_FAULTADDRESS_LO), kctx);

		if (kctx)
		{
			/* hw counters dumping in progress, signal the other thread that it failed */
			if ((kbdev->hwcnt.kctx == kctx) && (kbdev->hwcnt.state == KBASE_INSTR_STATE_DUMPING))
			{
				kbdev->hwcnt.state = KBASE_INSTR_STATE_FAULT;
			}

			/* Stop the kctx from submitting more jobs and cause it to be scheduled
			 * out/rescheduled when all references to it are released */
			spin_lock_irqsave( &js_devdata->runpool_irq.lock, flags);
			kbasep_js_clear_submit_allowed( js_devdata, kctx );
			spin_unlock_irqrestore( &js_devdata->runpool_irq.lock, flags);

			OSK_PRINT_WARN(OSK_BASE_MMU, "Bus error in AS%d at 0x%016llx\n", as_no, fault_addr);
		}
		else
		{
			OSK_PRINT_WARN(OSK_BASE_MMU,
						   "Bus error in AS%d at 0x%016llx with no context present! "
						   "Suprious IRQ or SW Design Error?\n",
						   as_no, fault_addr);
		}

		as = &kbdev->as[as_no];

		/* remove the queued BFs from the mask */
		new_mask &= ~(1UL << (as_no + num_as));

		/* We need to switch to UNMAPPED mode - but we do this in a worker so that we can sleep */
		OSK_ASSERT(0 == object_is_on_stack(&as->work_busfault));
		INIT_WORK(&as->work_busfault, bus_fault_worker);
		queue_work(as->pf_wq, &as->work_busfault);
	}

	/*
	 * pf_bits is non-zero if we have at least one AS with a page fault and no bus fault.
	 * Handle the PFs in our worker thread.
	 */
	while (pf_bits)
	{
		kbase_as * as;
		/* the while logic ensures we have a bit set, no need to check for not-found here */
		int as_no = osk_find_first_set_bit(pf_bits);
		kbase_context * kctx;

		/* Refcount the kctx ASAP - it shouldn't disappear anyway, since Bus/Page faults
		 * _should_ only occur whilst jobs are running, and a job causing the Bus/Page fault
		 * shouldn't complete until the MMU is updated */
		kctx = kbasep_js_runpool_lookup_ctx( kbdev, as_no );

		/* mark as handled */
		pf_bits &= ~(1UL << as_no);

		/* find faulting address */
		fault_addr = kbase_reg_read(kbdev, MMU_AS_REG(as_no, ASn_FAULTADDRESS_HI), kctx);
		fault_addr <<= 32;
		fault_addr |= kbase_reg_read(kbdev, MMU_AS_REG(as_no, ASn_FAULTADDRESS_LO), kctx);

		if ( kctx == NULL )
		{
			OSK_PRINT_WARN(OSK_BASE_MMU,
						   "Page fault in AS%d at 0x%016llx with no context present! "
						   "Suprious IRQ or SW Design Error?\n",
						   as_no, fault_addr);
		}

		as = &kbdev->as[as_no];

		/* remove the queued PFs from the mask */
		new_mask &= ~((1UL << as_no) | (1UL << (as_no + num_as)));

		/* queue work pending for this AS */
		as->fault_addr = fault_addr;

		OSK_ASSERT(0 == object_is_on_stack(&as->work_pagefault));
		INIT_WORK(&as->work_pagefault, page_fault_worker);
		queue_work(as->pf_wq, &as->work_pagefault);
	}

	/* reenable interrupts */
	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);
	tmp = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK), NULL);
	new_mask |= tmp;
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), new_mask, NULL);
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}
KBASE_EXPORT_TEST_API(kbase_mmu_interrupt)

const char *kbase_exception_name(u32 exception_code)
{
	const char *e;

	switch(exception_code)
	{
		/* Non-Fault Status code */
		case 0x00: e = "NOT_STARTED/IDLE/OK"; break;
		case 0x01: e = "DONE"; break;
		case 0x02: e = "INTERRUPTED"; break;
		case 0x03: e = "STOPPED"; break;
		case 0x04: e = "TERMINATED"; break;
		case 0x08: e = "ACTIVE"; break;
		/* Job exceptions */
		case 0x40: e = "JOB_CONFIG_FAULT"; break;
		case 0x41: e = "JOB_POWER_FAULT"; break;
		case 0x42: e = "JOB_READ_FAULT"; break;
		case 0x43: e = "JOB_WRITE_FAULT"; break;
		case 0x44: e = "JOB_AFFINITY_FAULT"; break;
		case 0x48: e = "JOB_BUS_FAULT"; break;
		case 0x50: e = "INSTR_INVALID_PC"; break;
		case 0x51: e = "INSTR_INVALID_ENC"; break;
		case 0x52: e = "INSTR_TYPE_MISMATCH"; break;
		case 0x53: e = "INSTR_OPERAND_FAULT"; break;
		case 0x54: e = "INSTR_TLS_FAULT"; break;
		case 0x55: e = "INSTR_BARRIER_FAULT"; break;
		case 0x56: e = "INSTR_ALIGN_FAULT"; break;
		case 0x58: e = "DATA_INVALID_FAULT"; break;
		case 0x59: e = "TILE_RANGE_FAULT"; break;
		case 0x5A: e = "ADDR_RANGE_FAULT"; break;
		case 0x60: e = "OUT_OF_MEMORY"; break;
		/* GPU exceptions */
		case 0x80: e = "DELAYED_BUS_FAULT"; break;
		case 0x81: e = "SHAREABILITY_FAULT"; break;
		/* MMU exceptions */
		case 0xC0: case 0xC1: case 0xC2: case 0xC3:
		case 0xC4: case 0xC5: case 0xC6: case 0xC7:
			e = "TRANSLATION_FAULT"; break;
		case 0xC8: e = "PERMISSION_FAULT"; break;
		case 0xD0: case 0xD1: case 0xD2: case 0xD3:
		case 0xD4: case 0xD5: case 0xD6: case 0xD7:
			e = "TRANSTAB_BUS_FAULT"; break;
		case 0xD8: e = "ACCESS_FLAG"; break;
		default:
			e = "UNKNOWN"; break;
	};

	return e;
}

/**
 * The caller must ensure it's retained the ctx to prevent it from being scheduled out whilst it's being worked on.
 */
static void kbase_mmu_report_fault_and_kill(kbase_context *kctx, kbase_as * as, mali_addr64 fault_addr)
{
	unsigned long flags;
	u32 fault_status;
	u32 reg;
	int exception_type;
	int access_type;
	int source_id;
	int as_no;
	kbase_device * kbdev;
	kbasep_js_device_data *js_devdata;
	mali_bool reset_status = MALI_FALSE;
#ifdef CONFIG_MALI_DEBUG
	static const char *access_type_names[] = { "RESERVED", "EXECUTE", "READ", "WRITE" };
#endif /* CONFIG_MALI_DEBUG */

	OSK_ASSERT(as);
	OSK_ASSERT(kctx);
	CSTD_UNUSED(fault_addr);

	as_no = as->number;
	kbdev = kctx->kbdev;
	js_devdata = &kbdev->js_data;

	/* ASSERT that the context won't leave the runpool */
	OSK_ASSERT( kbasep_js_debug_check_ctx_refcount( kbdev, kctx ) > 0 );

	fault_status = kbase_reg_read(kbdev, MMU_AS_REG(as_no, ASn_FAULTSTATUS), kctx);

	/* decode the fault status */
	exception_type = fault_status       & 0xFF;
	access_type =   (fault_status >> 8) & 0x3;
	source_id =     (fault_status >> 16);

	/* terminal fault, print info about the fault */
	OSK_PRINT_WARN(OSK_BASE_MMU, "Fault in AS%d at VA 0x%016llX", as_no, fault_addr);
	OSK_PRINT_WARN(OSK_BASE_MMU, "raw fault status 0x%X", fault_status);
	OSK_PRINT_WARN(OSK_BASE_MMU, "decoded fault status (%s):", (fault_status & (1 << 10) ? "DECODER FAULT" : "SLAVE FAULT"));
	OSK_PRINT_WARN(OSK_BASE_MMU, "exception type 0x%X: %s", exception_type, kbase_exception_name(exception_type));
	OSK_PRINT_WARN(OSK_BASE_MMU, "access type 0x%X: %s", access_type,  access_type_names[access_type]);
	OSK_PRINT_WARN(OSK_BASE_MMU, "source id 0x%X", source_id);

	/* hardware counters dump fault handling */
	if ((kbdev->hwcnt.kctx) &&
	    (kbdev->hwcnt.kctx->as_nr == as_no) &&
		(kbdev->hwcnt.state == KBASE_INSTR_STATE_DUMPING))
	{
		u32 num_core_groups = kbdev->gpu_props.num_core_groups;
		if ((fault_addr >= kbdev->hwcnt.addr) && (fault_addr < (kbdev->hwcnt.addr + (num_core_groups * 2048))))
		{
			kbdev->hwcnt.state = KBASE_INSTR_STATE_FAULT;
		}
	}

	/* Stop the kctx from submitting more jobs and cause it to be scheduled
	 * out/rescheduled - this will occur on releasing the context's refcount */
	spin_lock_irqsave( &js_devdata->runpool_irq.lock, flags);
	kbasep_js_clear_submit_allowed( js_devdata, kctx );
	spin_unlock_irqrestore( &js_devdata->runpool_irq.lock, flags);

	/* Kill any running jobs from the context. Submit is disallowed, so no more jobs from this
	 * context can appear in the job slots from this point on */
	kbase_job_kill_jobs_from_context(kctx);
	/* AS transaction begin */
	mutex_lock(&as->transaction_mutex);

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8245))
	{
		/* Due to H/W issue 8245 we need to reset the GPU after using UNMAPPED mode.
		 * We start the reset before switching to UNMAPPED to ensure that unrelated jobs
		 * are evicted from the GPU before the switch.
		 */
		OSK_PRINT_WARN(OSK_BASE_MMU, "NOTE: GPU will now be reset as a workaround for a hardware issue**");
		reset_status = kbase_prepare_to_reset_gpu(kbdev);
	}

	/* switch to UNMAPPED mode, will abort all jobs and stop any hw counter dumping */
	reg = kbase_reg_read(kbdev, MMU_AS_REG(as_no, ASn_TRANSTAB_LO), kctx);
	reg &= ~3;
	kbase_reg_write(kbdev, MMU_AS_REG(as_no, ASn_TRANSTAB_LO), reg, kctx);
	kbase_reg_write(kbdev, MMU_AS_REG(as_no, ASn_COMMAND), ASn_COMMAND_UPDATE, kctx);

	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), (1UL << as_no), NULL);

	mutex_unlock(&as->transaction_mutex);
	/* AS transaction end */
	mmu_mask_reenable(kbdev, kctx, as);

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8245) && reset_status)
	{
		kbase_reset_gpu(kbdev);
	}
}

void kbasep_as_do_poke(struct work_struct *work)
{
	kbase_as * as;
	kbase_device * kbdev;

	OSK_ASSERT(work);
	as = container_of(work, kbase_as, poke_work);
	kbdev = container_of(as, kbase_device, as[as->number]);

	kbase_pm_context_active(kbdev);

	/* AS transaction begin */
	mutex_lock(&as->transaction_mutex);
	/* Force a uTLB invalidate */
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, ASn_COMMAND), ASn_COMMAND_UNLOCK, NULL);
	mutex_unlock(&as->transaction_mutex);
	/* AS transaction end */

	kbase_pm_context_idle(kbdev);

	if (atomic_read(&as->poke_refcount))
	{
		/* still someone depending on the UNLOCK, schedule a run */
		hrtimer_start(&as->poke_timer, HR_TIMER_DELAY_MSEC(5), HRTIMER_MODE_REL );
	}
}

enum hrtimer_restart kbasep_as_poke_timer_callback(struct hrtimer * timer)
{
	kbase_as * as = container_of(timer, kbase_as, poke_timer);

	OSK_ASSERT(NULL != as);

	queue_work(as->poke_wq, &as->poke_work);

	return HRTIMER_NORESTART;
}

void kbase_as_poking_timer_retain(kbase_as * as)
{
	OSK_ASSERT(as);

	if (1 == atomic_inc_return(&as->poke_refcount))
	{
		/* need to start poking */
		queue_work(as->poke_wq, &as->poke_work);
	}
}

void kbase_as_poking_timer_release(kbase_as * as)
{
	OSK_ASSERT(as);
	atomic_dec(&as->poke_refcount);
}

