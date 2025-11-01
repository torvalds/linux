// SPDX-License-Identifier: GPL-2.0
/*
 *  Helper functions for KVM guest address space mapping code
 *
 *    Copyright IBM Corp. 2007, 2025
 */

#include <linux/export.h>
#include <linux/mm_types.h>
#include <linux/mmap_lock.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/pagewalk.h>
#include <linux/ksm.h>
#include <asm/gmap_helpers.h>
#include <asm/pgtable.h>

/**
 * ptep_zap_swap_entry() - discard a swap entry.
 * @mm: the mm
 * @entry: the swap entry that needs to be zapped
 *
 * Discards the given swap entry. If the swap entry was an actual swap
 * entry (and not a migration entry, for example), the actual swapped
 * page is also discarded from swap.
 */
static void ptep_zap_swap_entry(struct mm_struct *mm, swp_entry_t entry)
{
	if (!non_swap_entry(entry))
		dec_mm_counter(mm, MM_SWAPENTS);
	else if (is_migration_entry(entry))
		dec_mm_counter(mm, mm_counter(pfn_swap_entry_folio(entry)));
	free_swap_and_cache(entry);
}

/**
 * gmap_helper_zap_one_page() - discard a page if it was swapped.
 * @mm: the mm
 * @vmaddr: the userspace virtual address that needs to be discarded
 *
 * If the given address maps to a swap entry, discard it.
 *
 * Context: needs to be called while holding the mmap lock.
 */
void gmap_helper_zap_one_page(struct mm_struct *mm, unsigned long vmaddr)
{
	struct vm_area_struct *vma;
	spinlock_t *ptl;
	pgste_t pgste;
	pte_t *ptep;

	mmap_assert_locked(mm);

	/* Find the vm address for the guest address */
	vma = vma_lookup(mm, vmaddr);
	if (!vma || is_vm_hugetlb_page(vma))
		return;

	/* Get pointer to the page table entry */
	ptep = get_locked_pte(mm, vmaddr, &ptl);
	if (unlikely(!ptep))
		return;
	if (pte_swap(*ptep)) {
		preempt_disable();
		pgste = pgste_get_lock(ptep);

		ptep_zap_swap_entry(mm, pte_to_swp_entry(*ptep));
		pte_clear(mm, vmaddr, ptep);

		pgste_set_unlock(ptep, pgste);
		preempt_enable();
	}
	pte_unmap_unlock(ptep, ptl);
}
EXPORT_SYMBOL_GPL(gmap_helper_zap_one_page);

/**
 * gmap_helper_discard() - discard user pages in the given range
 * @mm: the mm
 * @vmaddr: starting userspace address
 * @end: end address (first address outside the range)
 *
 * All userpace pages in the range [@vamddr, @end) are discarded and unmapped.
 *
 * Context: needs to be called while holding the mmap lock.
 */
void gmap_helper_discard(struct mm_struct *mm, unsigned long vmaddr, unsigned long end)
{
	struct vm_area_struct *vma;

	mmap_assert_locked(mm);

	while (vmaddr < end) {
		vma = find_vma_intersection(mm, vmaddr, end);
		if (!vma)
			return;
		if (!is_vm_hugetlb_page(vma))
			zap_page_range_single(vma, vmaddr, min(end, vma->vm_end) - vmaddr, NULL);
		vmaddr = vma->vm_end;
	}
}
EXPORT_SYMBOL_GPL(gmap_helper_discard);

static int find_zeropage_pte_entry(pte_t *pte, unsigned long addr,
				   unsigned long end, struct mm_walk *walk)
{
	unsigned long *found_addr = walk->private;

	/* Return 1 of the page is a zeropage. */
	if (is_zero_pfn(pte_pfn(*pte))) {
		/*
		 * Shared zeropage in e.g., a FS DAX mapping? We cannot do the
		 * right thing and likely don't care: FAULT_FLAG_UNSHARE
		 * currently only works in COW mappings, which is also where
		 * mm_forbids_zeropage() is checked.
		 */
		if (!is_cow_mapping(walk->vma->vm_flags))
			return -EFAULT;

		*found_addr = addr;
		return 1;
	}
	return 0;
}

static const struct mm_walk_ops find_zeropage_ops = {
	.pte_entry      = find_zeropage_pte_entry,
	.walk_lock      = PGWALK_WRLOCK,
};

/** __gmap_helper_unshare_zeropages() - unshare all shared zeropages
 * @mm: the mm whose zero pages are to be unshared
 *
 * Unshare all shared zeropages, replacing them by anonymous pages. Note that
 * we cannot simply zap all shared zeropages, because this could later
 * trigger unexpected userfaultfd missing events.
 *
 * This must be called after mm->context.allow_cow_sharing was
 * set to 0, to avoid future mappings of shared zeropages.
 *
 * mm contracts with s390, that even if mm were to remove a page table,
 * and racing with walk_page_range_vma() calling pte_offset_map_lock()
 * would fail, it will never insert a page table containing empty zero
 * pages once mm_forbids_zeropage(mm) i.e.
 * mm->context.allow_cow_sharing is set to 0.
 */
static int __gmap_helper_unshare_zeropages(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, mm, 0);
	unsigned long addr;
	vm_fault_t fault;
	int rc;

	for_each_vma(vmi, vma) {
		/*
		 * We could only look at COW mappings, but it's more future
		 * proof to catch unexpected zeropages in other mappings and
		 * fail.
		 */
		if ((vma->vm_flags & VM_PFNMAP) || is_vm_hugetlb_page(vma))
			continue;
		addr = vma->vm_start;

retry:
		rc = walk_page_range_vma(vma, addr, vma->vm_end,
					 &find_zeropage_ops, &addr);
		if (rc < 0)
			return rc;
		else if (!rc)
			continue;

		/* addr was updated by find_zeropage_pte_entry() */
		fault = handle_mm_fault(vma, addr,
					FAULT_FLAG_UNSHARE | FAULT_FLAG_REMOTE,
					NULL);
		if (fault & VM_FAULT_OOM)
			return -ENOMEM;
		/*
		 * See break_ksm(): even after handle_mm_fault() returned 0, we
		 * must start the lookup from the current address, because
		 * handle_mm_fault() may back out if there's any difficulty.
		 *
		 * VM_FAULT_SIGBUS and VM_FAULT_SIGSEGV are unexpected but
		 * maybe they could trigger in the future on concurrent
		 * truncation. In that case, the shared zeropage would be gone
		 * and we can simply retry and make progress.
		 */
		cond_resched();
		goto retry;
	}

	return 0;
}

/**
 * gmap_helper_disable_cow_sharing() - disable all COW sharing
 *
 * Disable most COW-sharing of memory pages for the whole process:
 * (1) Disable KSM and unmerge/unshare any KSM pages.
 * (2) Disallow shared zeropages and unshare any zerpages that are mapped.
 *
 * Not that we currently don't bother with COW-shared pages that are shared
 * with parent/child processes due to fork().
 */
int gmap_helper_disable_cow_sharing(void)
{
	struct mm_struct *mm = current->mm;
	int rc;

	mmap_assert_write_locked(mm);

	if (!mm->context.allow_cow_sharing)
		return 0;

	mm->context.allow_cow_sharing = 0;

	/* Replace all shared zeropages by anonymous pages. */
	rc = __gmap_helper_unshare_zeropages(mm);
	/*
	 * Make sure to disable KSM (if enabled for the whole process or
	 * individual VMAs). Note that nothing currently hinders user space
	 * from re-enabling it.
	 */
	if (!rc)
		rc = ksm_disable(mm);
	if (rc)
		mm->context.allow_cow_sharing = 1;
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_helper_disable_cow_sharing);
