/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_TLBFLUSH_H
#define _ASM_POWERPC_BOOK3S_64_TLBFLUSH_H

#define MMU_NO_CONTEXT	~0UL

#include <linux/mm_types.h>
#include <asm/book3s/64/tlbflush-hash.h>
#include <asm/book3s/64/tlbflush-radix.h>

/* TLB flush actions. Used as argument to tlbiel_all() */
enum {
	TLB_INVAL_SCOPE_GLOBAL = 0,	/* invalidate all TLBs */
	TLB_INVAL_SCOPE_LPID = 1,	/* invalidate TLBs for current LPID */
};

static inline void tlbiel_all(void)
{
	/*
	 * This is used for host machine check and bootup.
	 *
	 * This uses early_radix_enabled and implementations use
	 * early_cpu_has_feature etc because that works early in boot
	 * and this is the machine check path which is not performance
	 * critical.
	 */
	if (early_radix_enabled())
		radix__tlbiel_all(TLB_INVAL_SCOPE_GLOBAL);
	else
		hash__tlbiel_all(TLB_INVAL_SCOPE_GLOBAL);
}

static inline void tlbiel_all_lpid(bool radix)
{
	/*
	 * This is used for guest machine check.
	 */
	if (radix)
		radix__tlbiel_all(TLB_INVAL_SCOPE_LPID);
	else
		hash__tlbiel_all(TLB_INVAL_SCOPE_LPID);
}


#define __HAVE_ARCH_FLUSH_PMD_TLB_RANGE
static inline void flush_pmd_tlb_range(struct vm_area_struct *vma,
				       unsigned long start, unsigned long end)
{
	if (radix_enabled())
		radix__flush_pmd_tlb_range(vma, start, end);
}

#define __HAVE_ARCH_FLUSH_HUGETLB_TLB_RANGE
static inline void flush_hugetlb_tlb_range(struct vm_area_struct *vma,
					   unsigned long start,
					   unsigned long end)
{
	if (radix_enabled())
		radix__flush_hugetlb_tlb_range(vma, start, end);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	if (radix_enabled())
		radix__flush_tlb_range(vma, start, end);
}

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
	if (radix_enabled())
		radix__flush_tlb_kernel_range(start, end);
}

static inline void local_flush_tlb_mm(struct mm_struct *mm)
{
	if (radix_enabled())
		radix__local_flush_tlb_mm(mm);
}

static inline void local_flush_tlb_page(struct vm_area_struct *vma,
					unsigned long vmaddr)
{
	if (radix_enabled())
		radix__local_flush_tlb_page(vma, vmaddr);
}

static inline void local_flush_tlb_page_psize(struct mm_struct *mm,
					      unsigned long vmaddr, int psize)
{
	if (radix_enabled())
		radix__local_flush_tlb_page_psize(mm, vmaddr, psize);
}

static inline void tlb_flush(struct mmu_gather *tlb)
{
	if (radix_enabled())
		radix__tlb_flush(tlb);
	else
		hash__tlb_flush(tlb);
}

#ifdef CONFIG_SMP
static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (radix_enabled())
		radix__flush_tlb_mm(mm);
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long vmaddr)
{
	if (radix_enabled())
		radix__flush_tlb_page(vma, vmaddr);
}
#else
#define flush_tlb_mm(mm)		local_flush_tlb_mm(mm)
#define flush_tlb_page(vma, addr)	local_flush_tlb_page(vma, addr)
#endif /* CONFIG_SMP */

#define flush_tlb_fix_spurious_fault flush_tlb_fix_spurious_fault
static inline void flush_tlb_fix_spurious_fault(struct vm_area_struct *vma,
						unsigned long address)
{
	/*
	 * Book3S 64 does not require spurious fault flushes because the PTE
	 * must be re-fetched in case of an access permission problem. So the
	 * only reason for a spurious fault should be concurrent modification
	 * to the PTE, in which case the PTE will eventually be re-fetched by
	 * the MMU when it attempts the access again.
	 *
	 * See: Power ISA Version 3.1B, 6.10.1.2 Modifying a Translation Table
	 * Entry, Setting a Reference or Change Bit or Upgrading Access
	 * Authority (PTE Subject to Atomic Hardware Updates):
	 *
	 * "If the only change being made to a valid PTE that is subject to
	 *  atomic hardware updates is to set the Reference or Change bit to
	 *  1 or to upgrade access authority, a simpler sequence suffices
	 *  because the translation hardware will refetch the PTE if an
	 *  access is attempted for which the only problems were reference
	 *  and/or change bits needing to be set or insufficient access
	 *  authority."
	 *
	 * The nest MMU in POWER9 does not perform this PTE re-fetch, but
	 * it avoids the spurious fault problem by flushing the TLB before
	 * upgrading PTE permissions, see radix__ptep_set_access_flags.
	 */
}

static inline bool __pte_protnone(unsigned long pte)
{
	return (pte & (pgprot_val(PAGE_NONE) | _PAGE_RWX)) == pgprot_val(PAGE_NONE);
}

static inline bool __pte_flags_need_flush(unsigned long oldval,
					  unsigned long newval)
{
	unsigned long delta = oldval ^ newval;

	/*
	 * The return value of this function doesn't matter for hash,
	 * ptep_modify_prot_start() does a pte_update() which does or schedules
	 * any necessary hash table update and flush.
	 */
	if (!radix_enabled())
		return true;

	/*
	 * We do not expect kernel mappings or non-PTEs or not-present PTEs.
	 */
	VM_WARN_ON_ONCE(!__pte_protnone(oldval) && oldval & _PAGE_PRIVILEGED);
	VM_WARN_ON_ONCE(!__pte_protnone(newval) && newval & _PAGE_PRIVILEGED);
	VM_WARN_ON_ONCE(!(oldval & _PAGE_PTE));
	VM_WARN_ON_ONCE(!(newval & _PAGE_PTE));
	VM_WARN_ON_ONCE(!(oldval & _PAGE_PRESENT));
	VM_WARN_ON_ONCE(!(newval & _PAGE_PRESENT));

	/*
	*  Must flush on any change except READ, WRITE, EXEC, DIRTY, ACCESSED.
	*
	 * In theory, some changed software bits could be tolerated, in
	 * practice those should rarely if ever matter.
	 */

	if (delta & ~(_PAGE_RWX | _PAGE_DIRTY | _PAGE_ACCESSED))
		return true;

	/*
	 * If any of the above was present in old but cleared in new, flush.
	 * With the exception of _PAGE_ACCESSED, don't worry about flushing
	 * if that was cleared (see the comment in ptep_clear_flush_young()).
	 */
	if ((delta & ~_PAGE_ACCESSED) & oldval)
		return true;

	return false;
}

static inline bool pte_needs_flush(pte_t oldpte, pte_t newpte)
{
	return __pte_flags_need_flush(pte_val(oldpte), pte_val(newpte));
}
#define pte_needs_flush pte_needs_flush

static inline bool huge_pmd_needs_flush(pmd_t oldpmd, pmd_t newpmd)
{
	return __pte_flags_need_flush(pmd_val(oldpmd), pmd_val(newpmd));
}
#define huge_pmd_needs_flush huge_pmd_needs_flush

extern bool tlbie_capable;
extern bool tlbie_enabled;

static inline bool cputlb_use_tlbie(void)
{
	return tlbie_enabled;
}

#endif /*  _ASM_POWERPC_BOOK3S_64_TLBFLUSH_H */
