/*
 * arch/xtensa/mm/tlb.c
 *
 * Logic that manipulates the Xtensa MMU.  Derived from MIPS.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2003 Tensilica Inc.
 *
 * Joe Taylor
 * Chris Zankel	<chris@zankel.net>
 * Marc Gauthier
 */

#include <linux/mm.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>


static inline void __flush_itlb_all (void)
{
	int w, i;

	for (w = 0; w < ITLB_ARF_WAYS; w++) {
		for (i = 0; i < (1 << XCHAL_ITLB_ARF_ENTRIES_LOG2); i++) {
			int e = w + (i << PAGE_SHIFT);
			invalidate_itlb_entry_no_isync(e);
		}
	}
	asm volatile ("isync\n");
}

static inline void __flush_dtlb_all (void)
{
	int w, i;

	for (w = 0; w < DTLB_ARF_WAYS; w++) {
		for (i = 0; i < (1 << XCHAL_DTLB_ARF_ENTRIES_LOG2); i++) {
			int e = w + (i << PAGE_SHIFT);
			invalidate_dtlb_entry_no_isync(e);
		}
	}
	asm volatile ("isync\n");
}


void local_flush_tlb_all(void)
{
	__flush_itlb_all();
	__flush_dtlb_all();
}

/* If mm is current, we simply assign the current task a new ASID, thus,
 * invalidating all previous tlb entries. If mm is someone else's user mapping,
 * wie invalidate the context, thus, when that user mapping is swapped in,
 * a new context will be assigned to it.
 */

void local_flush_tlb_mm(struct mm_struct *mm)
{
	int cpu = smp_processor_id();

	if (mm == current->active_mm) {
		unsigned long flags;
		local_irq_save(flags);
		mm->context.asid[cpu] = NO_CONTEXT;
		activate_context(mm, cpu);
		local_irq_restore(flags);
	} else {
		mm->context.asid[cpu] = NO_CONTEXT;
		mm->context.cpu = -1;
	}
}


#define _ITLB_ENTRIES (ITLB_ARF_WAYS << XCHAL_ITLB_ARF_ENTRIES_LOG2)
#define _DTLB_ENTRIES (DTLB_ARF_WAYS << XCHAL_DTLB_ARF_ENTRIES_LOG2)
#if _ITLB_ENTRIES > _DTLB_ENTRIES
# define _TLB_ENTRIES _ITLB_ENTRIES
#else
# define _TLB_ENTRIES _DTLB_ENTRIES
#endif

void local_flush_tlb_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	int cpu = smp_processor_id();
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;

	if (mm->context.asid[cpu] == NO_CONTEXT)
		return;

	pr_debug("[tlbrange<%02lx,%08lx,%08lx>]\n",
		 (unsigned long)mm->context.asid[cpu], start, end);
	local_irq_save(flags);

	if (end-start + (PAGE_SIZE-1) <= _TLB_ENTRIES << PAGE_SHIFT) {
		int oldpid = get_rasid_register();

		set_rasid_register(ASID_INSERT(mm->context.asid[cpu]));
		start &= PAGE_MASK;
		if (vma->vm_flags & VM_EXEC)
			while(start < end) {
				invalidate_itlb_mapping(start);
				invalidate_dtlb_mapping(start);
				start += PAGE_SIZE;
			}
		else
			while(start < end) {
				invalidate_dtlb_mapping(start);
				start += PAGE_SIZE;
			}

		set_rasid_register(oldpid);
	} else {
		local_flush_tlb_mm(mm);
	}
	local_irq_restore(flags);
}

void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int cpu = smp_processor_id();
	struct mm_struct* mm = vma->vm_mm;
	unsigned long flags;
	int oldpid;

	if (mm->context.asid[cpu] == NO_CONTEXT)
		return;

	local_irq_save(flags);

	oldpid = get_rasid_register();
	set_rasid_register(ASID_INSERT(mm->context.asid[cpu]));

	if (vma->vm_flags & VM_EXEC)
		invalidate_itlb_mapping(page);
	invalidate_dtlb_mapping(page);

	set_rasid_register(oldpid);

	local_irq_restore(flags);
}

void local_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	if (end > start && start >= TASK_SIZE && end <= PAGE_OFFSET &&
	    end - start < _TLB_ENTRIES << PAGE_SHIFT) {
		start &= PAGE_MASK;
		while (start < end) {
			invalidate_itlb_mapping(start);
			invalidate_dtlb_mapping(start);
			start += PAGE_SIZE;
		}
	} else {
		local_flush_tlb_all();
	}
}

#ifdef CONFIG_DEBUG_TLB_SANITY

static unsigned get_pte_for_vaddr(unsigned vaddr)
{
	struct task_struct *task = get_current();
	struct mm_struct *mm = task->mm;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	if (!mm)
		mm = task->active_mm;
	pgd = pgd_offset(mm, vaddr);
	if (pgd_none_or_clear_bad(pgd))
		return 0;
	pmd = pmd_offset(pgd, vaddr);
	if (pmd_none_or_clear_bad(pmd))
		return 0;
	pte = pte_offset_map(pmd, vaddr);
	if (!pte)
		return 0;
	return pte_val(*pte);
}

enum {
	TLB_SUSPICIOUS	= 1,
	TLB_INSANE	= 2,
};

static void tlb_insane(void)
{
	BUG_ON(1);
}

static void tlb_suspicious(void)
{
	WARN_ON(1);
}

/*
 * Check that TLB entries with kernel ASID (1) have kernel VMA (>= TASK_SIZE),
 * and TLB entries with user ASID (>=4) have VMA < TASK_SIZE.
 *
 * Check that valid TLB entries either have the same PA as the PTE, or PTE is
 * marked as non-present. Non-present PTE and the page with non-zero refcount
 * and zero mapcount is normal for batched TLB flush operation. Zero refcount
 * means that the page was freed prematurely. Non-zero mapcount is unusual,
 * but does not necessary means an error, thus marked as suspicious.
 */
static int check_tlb_entry(unsigned w, unsigned e, bool dtlb)
{
	unsigned tlbidx = w | (e << PAGE_SHIFT);
	unsigned r0 = dtlb ?
		read_dtlb_virtual(tlbidx) : read_itlb_virtual(tlbidx);
	unsigned vpn = (r0 & PAGE_MASK) | (e << PAGE_SHIFT);
	unsigned pte = get_pte_for_vaddr(vpn);
	unsigned mm_asid = (get_rasid_register() >> 8) & ASID_MASK;
	unsigned tlb_asid = r0 & ASID_MASK;
	bool kernel = tlb_asid == 1;
	int rc = 0;

	if (tlb_asid > 0 && ((vpn < TASK_SIZE) == kernel)) {
		pr_err("%cTLB: way: %u, entry: %u, VPN %08x in %s PTE\n",
				dtlb ? 'D' : 'I', w, e, vpn,
				kernel ? "kernel" : "user");
		rc |= TLB_INSANE;
	}

	if (tlb_asid == mm_asid) {
		unsigned r1 = dtlb ? read_dtlb_translation(tlbidx) :
			read_itlb_translation(tlbidx);
		if ((pte ^ r1) & PAGE_MASK) {
			pr_err("%cTLB: way: %u, entry: %u, mapping: %08x->%08x, PTE: %08x\n",
					dtlb ? 'D' : 'I', w, e, r0, r1, pte);
			if (pte == 0 || !pte_present(__pte(pte))) {
				struct page *p = pfn_to_page(r1 >> PAGE_SHIFT);
				pr_err("page refcount: %d, mapcount: %d\n",
						page_count(p),
						page_mapcount(p));
				if (!page_count(p))
					rc |= TLB_INSANE;
				else if (page_mapcount(p))
					rc |= TLB_SUSPICIOUS;
			} else {
				rc |= TLB_INSANE;
			}
		}
	}
	return rc;
}

void check_tlb_sanity(void)
{
	unsigned long flags;
	unsigned w, e;
	int bug = 0;

	local_irq_save(flags);
	for (w = 0; w < DTLB_ARF_WAYS; ++w)
		for (e = 0; e < (1 << XCHAL_DTLB_ARF_ENTRIES_LOG2); ++e)
			bug |= check_tlb_entry(w, e, true);
	for (w = 0; w < ITLB_ARF_WAYS; ++w)
		for (e = 0; e < (1 << XCHAL_ITLB_ARF_ENTRIES_LOG2); ++e)
			bug |= check_tlb_entry(w, e, false);
	if (bug & TLB_INSANE)
		tlb_insane();
	if (bug & TLB_SUSPICIOUS)
		tlb_suspicious();
	local_irq_restore(flags);
}

#endif /* CONFIG_DEBUG_TLB_SANITY */
