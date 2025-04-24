/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_PGTABLE_H
#define __ASM_PGTABLE_H

#include <asm/bug.h>
#include <asm/proc-fns.h>

#include <asm/memory.h>
#include <asm/mte.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable-prot.h>
#include <asm/tlbflush.h>

/*
 * VMALLOC range.
 *
 * VMALLOC_START: beginning of the kernel vmalloc space
 * VMALLOC_END: extends to the available space below vmemmap
 */
#define VMALLOC_START		(MODULES_END)
#if VA_BITS == VA_BITS_MIN
#define VMALLOC_END		(VMEMMAP_START - SZ_8M)
#else
#define VMEMMAP_UNUSED_NPAGES	((_PAGE_OFFSET(vabits_actual) - PAGE_OFFSET) >> PAGE_SHIFT)
#define VMALLOC_END		(VMEMMAP_START + VMEMMAP_UNUSED_NPAGES * sizeof(struct page) - SZ_8M)
#endif

#define vmemmap			((struct page *)VMEMMAP_START - (memstart_addr >> PAGE_SHIFT))

#ifndef __ASSEMBLY__

#include <asm/cmpxchg.h>
#include <asm/fixmap.h>
#include <asm/por.h>
#include <linux/mmdebug.h>
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/page_table_check.h>

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define __HAVE_ARCH_FLUSH_PMD_TLB_RANGE

/* Set stride and tlb_level in flush_*_tlb_range */
#define flush_pmd_tlb_range(vma, addr, end)	\
	__flush_tlb_range(vma, addr, end, PMD_SIZE, false, 2)
#define flush_pud_tlb_range(vma, addr, end)	\
	__flush_tlb_range(vma, addr, end, PUD_SIZE, false, 1)
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/*
 * Outside of a few very special situations (e.g. hibernation), we always
 * use broadcast TLB invalidation instructions, therefore a spurious page
 * fault on one CPU which has been handled concurrently by another CPU
 * does not need to perform additional invalidation.
 */
#define flush_tlb_fix_spurious_fault(vma, address, ptep) do { } while (0)

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)];
#define ZERO_PAGE(vaddr)	phys_to_page(__pa_symbol(empty_zero_page))

#define pte_ERROR(e)	\
	pr_err("%s:%d: bad pte %016llx.\n", __FILE__, __LINE__, pte_val(e))

#ifdef CONFIG_ARM64_PA_BITS_52
static inline phys_addr_t __pte_to_phys(pte_t pte)
{
	pte_val(pte) &= ~PTE_MAYBE_SHARED;
	return (pte_val(pte) & PTE_ADDR_LOW) |
		((pte_val(pte) & PTE_ADDR_HIGH) << PTE_ADDR_HIGH_SHIFT);
}
static inline pteval_t __phys_to_pte_val(phys_addr_t phys)
{
	return (phys | (phys >> PTE_ADDR_HIGH_SHIFT)) & PHYS_TO_PTE_ADDR_MASK;
}
#else
static inline phys_addr_t __pte_to_phys(pte_t pte)
{
	return pte_val(pte) & PTE_ADDR_LOW;
}

static inline pteval_t __phys_to_pte_val(phys_addr_t phys)
{
	return phys;
}
#endif

#define pte_pfn(pte)		(__pte_to_phys(pte) >> PAGE_SHIFT)
#define pfn_pte(pfn,prot)	\
	__pte(__phys_to_pte_val((phys_addr_t)(pfn) << PAGE_SHIFT) | pgprot_val(prot))

#define pte_none(pte)		(!pte_val(pte))
#define __pte_clear(mm, addr, ptep) \
				__set_pte(ptep, __pte(0))
#define pte_page(pte)		(pfn_to_page(pte_pfn(pte)))

/*
 * The following only work if pte_present(). Undefined behaviour otherwise.
 */
#define pte_present(pte)	(pte_valid(pte) || pte_present_invalid(pte))
#define pte_young(pte)		(!!(pte_val(pte) & PTE_AF))
#define pte_special(pte)	(!!(pte_val(pte) & PTE_SPECIAL))
#define pte_write(pte)		(!!(pte_val(pte) & PTE_WRITE))
#define pte_rdonly(pte)		(!!(pte_val(pte) & PTE_RDONLY))
#define pte_user(pte)		(!!(pte_val(pte) & PTE_USER))
#define pte_user_exec(pte)	(!(pte_val(pte) & PTE_UXN))
#define pte_cont(pte)		(!!(pte_val(pte) & PTE_CONT))
#define pte_devmap(pte)		(!!(pte_val(pte) & PTE_DEVMAP))
#define pte_tagged(pte)		((pte_val(pte) & PTE_ATTRINDX_MASK) == \
				 PTE_ATTRINDX(MT_NORMAL_TAGGED))

#define pte_cont_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + CONT_PTE_SIZE) & CONT_PTE_MASK;	\
	(__boundary - 1 < (end) - 1) ? __boundary : (end);			\
})

#define pmd_cont_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + CONT_PMD_SIZE) & CONT_PMD_MASK;	\
	(__boundary - 1 < (end) - 1) ? __boundary : (end);			\
})

#define pte_hw_dirty(pte)	(pte_write(pte) && !pte_rdonly(pte))
#define pte_sw_dirty(pte)	(!!(pte_val(pte) & PTE_DIRTY))
#define pte_dirty(pte)		(pte_sw_dirty(pte) || pte_hw_dirty(pte))

#define pte_valid(pte)		(!!(pte_val(pte) & PTE_VALID))
#define pte_present_invalid(pte) \
	((pte_val(pte) & (PTE_VALID | PTE_PRESENT_INVALID)) == PTE_PRESENT_INVALID)
/*
 * Execute-only user mappings do not have the PTE_USER bit set. All valid
 * kernel mappings have the PTE_UXN bit set.
 */
#define pte_valid_not_user(pte) \
	((pte_val(pte) & (PTE_VALID | PTE_USER | PTE_UXN)) == (PTE_VALID | PTE_UXN))
/*
 * Returns true if the pte is valid and has the contiguous bit set.
 */
#define pte_valid_cont(pte)	(pte_valid(pte) && pte_cont(pte))
/*
 * Could the pte be present in the TLB? We must check mm_tlb_flush_pending
 * so that we don't erroneously return false for pages that have been
 * remapped as PROT_NONE but are yet to be flushed from the TLB.
 * Note that we can't make any assumptions based on the state of the access
 * flag, since __ptep_clear_flush_young() elides a DSB when invalidating the
 * TLB.
 */
#define pte_accessible(mm, pte)	\
	(mm_tlb_flush_pending(mm) ? pte_present(pte) : pte_valid(pte))

static inline bool por_el0_allows_pkey(u8 pkey, bool write, bool execute)
{
	u64 por;

	if (!system_supports_poe())
		return true;

	por = read_sysreg_s(SYS_POR_EL0);

	if (write)
		return por_elx_allows_write(por, pkey);

	if (execute)
		return por_elx_allows_exec(por, pkey);

	return por_elx_allows_read(por, pkey);
}

/*
 * p??_access_permitted() is true for valid user mappings (PTE_USER
 * bit set, subject to the write permission check). For execute-only
 * mappings, like PROT_EXEC with EPAN (both PTE_USER and PTE_UXN bits
 * not set) must return false. PROT_NONE mappings do not have the
 * PTE_VALID bit set.
 */
#define pte_access_permitted_no_overlay(pte, write) \
	(((pte_val(pte) & (PTE_VALID | PTE_USER)) == (PTE_VALID | PTE_USER)) && (!(write) || pte_write(pte)))
#define pte_access_permitted(pte, write) \
	(pte_access_permitted_no_overlay(pte, write) && \
	por_el0_allows_pkey(FIELD_GET(PTE_PO_IDX_MASK, pte_val(pte)), write, false))
#define pmd_access_permitted(pmd, write) \
	(pte_access_permitted(pmd_pte(pmd), (write)))
#define pud_access_permitted(pud, write) \
	(pte_access_permitted(pud_pte(pud), (write)))

static inline pte_t clear_pte_bit(pte_t pte, pgprot_t prot)
{
	pte_val(pte) &= ~pgprot_val(prot);
	return pte;
}

static inline pte_t set_pte_bit(pte_t pte, pgprot_t prot)
{
	pte_val(pte) |= pgprot_val(prot);
	return pte;
}

static inline pmd_t clear_pmd_bit(pmd_t pmd, pgprot_t prot)
{
	pmd_val(pmd) &= ~pgprot_val(prot);
	return pmd;
}

static inline pmd_t set_pmd_bit(pmd_t pmd, pgprot_t prot)
{
	pmd_val(pmd) |= pgprot_val(prot);
	return pmd;
}

static inline pte_t pte_mkwrite_novma(pte_t pte)
{
	pte = set_pte_bit(pte, __pgprot(PTE_WRITE));
	pte = clear_pte_bit(pte, __pgprot(PTE_RDONLY));
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte = clear_pte_bit(pte, __pgprot(PTE_DIRTY));
	pte = set_pte_bit(pte, __pgprot(PTE_RDONLY));

	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte = set_pte_bit(pte, __pgprot(PTE_DIRTY));

	if (pte_write(pte))
		pte = clear_pte_bit(pte, __pgprot(PTE_RDONLY));

	return pte;
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	/*
	 * If hardware-dirty (PTE_WRITE/DBM bit set and PTE_RDONLY
	 * clear), set the PTE_DIRTY bit.
	 */
	if (pte_hw_dirty(pte))
		pte = set_pte_bit(pte, __pgprot(PTE_DIRTY));

	pte = clear_pte_bit(pte, __pgprot(PTE_WRITE));
	pte = set_pte_bit(pte, __pgprot(PTE_RDONLY));
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(PTE_AF));
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_AF));
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_SPECIAL));
}

static inline pte_t pte_mkcont(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_CONT));
}

static inline pte_t pte_mknoncont(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(PTE_CONT));
}

static inline pte_t pte_mkvalid(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_VALID));
}

static inline pte_t pte_mkinvalid(pte_t pte)
{
	pte = set_pte_bit(pte, __pgprot(PTE_PRESENT_INVALID));
	pte = clear_pte_bit(pte, __pgprot(PTE_VALID));
	return pte;
}

static inline pmd_t pmd_mkcont(pmd_t pmd)
{
	return __pmd(pmd_val(pmd) | PMD_SECT_CONT);
}

static inline pte_t pte_mkdevmap(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_DEVMAP | PTE_SPECIAL));
}

#ifdef CONFIG_HAVE_ARCH_USERFAULTFD_WP
static inline int pte_uffd_wp(pte_t pte)
{
	return !!(pte_val(pte) & PTE_UFFD_WP);
}

static inline pte_t pte_mkuffd_wp(pte_t pte)
{
	return pte_wrprotect(set_pte_bit(pte, __pgprot(PTE_UFFD_WP)));
}

static inline pte_t pte_clear_uffd_wp(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(PTE_UFFD_WP));
}
#endif /* CONFIG_HAVE_ARCH_USERFAULTFD_WP */

static inline void __set_pte_nosync(pte_t *ptep, pte_t pte)
{
	WRITE_ONCE(*ptep, pte);
}

static inline void __set_pte(pte_t *ptep, pte_t pte)
{
	__set_pte_nosync(ptep, pte);

	/*
	 * Only if the new pte is valid and kernel, otherwise TLB maintenance
	 * or update_mmu_cache() have the necessary barriers.
	 */
	if (pte_valid_not_user(pte)) {
		dsb(ishst);
		isb();
	}
}

static inline pte_t __ptep_get(pte_t *ptep)
{
	return READ_ONCE(*ptep);
}

extern void __sync_icache_dcache(pte_t pteval);
bool pgattr_change_is_safe(pteval_t old, pteval_t new);

/*
 * PTE bits configuration in the presence of hardware Dirty Bit Management
 * (PTE_WRITE == PTE_DBM):
 *
 * Dirty  Writable | PTE_RDONLY  PTE_WRITE  PTE_DIRTY (sw)
 *   0      0      |   1           0          0
 *   0      1      |   1           1          0
 *   1      0      |   1           0          1
 *   1      1      |   0           1          x
 *
 * When hardware DBM is not present, the sofware PTE_DIRTY bit is updated via
 * the page fault mechanism. Checking the dirty status of a pte becomes:
 *
 *   PTE_DIRTY || (PTE_WRITE && !PTE_RDONLY)
 */

static inline void __check_safe_pte_update(struct mm_struct *mm, pte_t *ptep,
					   pte_t pte)
{
	pte_t old_pte;

	if (!IS_ENABLED(CONFIG_DEBUG_VM))
		return;

	old_pte = __ptep_get(ptep);

	if (!pte_valid(old_pte) || !pte_valid(pte))
		return;
	if (mm != current->active_mm && atomic_read(&mm->mm_users) <= 1)
		return;

	/*
	 * Check for potential race with hardware updates of the pte
	 * (__ptep_set_access_flags safely changes valid ptes without going
	 * through an invalid entry).
	 */
	VM_WARN_ONCE(!pte_young(pte),
		     "%s: racy access flag clearing: 0x%016llx -> 0x%016llx",
		     __func__, pte_val(old_pte), pte_val(pte));
	VM_WARN_ONCE(pte_write(old_pte) && !pte_dirty(pte),
		     "%s: racy dirty state clearing: 0x%016llx -> 0x%016llx",
		     __func__, pte_val(old_pte), pte_val(pte));
	VM_WARN_ONCE(!pgattr_change_is_safe(pte_val(old_pte), pte_val(pte)),
		     "%s: unsafe attribute change: 0x%016llx -> 0x%016llx",
		     __func__, pte_val(old_pte), pte_val(pte));
}

static inline void __sync_cache_and_tags(pte_t pte, unsigned int nr_pages)
{
	if (pte_present(pte) && pte_user_exec(pte) && !pte_special(pte))
		__sync_icache_dcache(pte);

	/*
	 * If the PTE would provide user space access to the tags associated
	 * with it then ensure that the MTE tags are synchronised.  Although
	 * pte_access_permitted_no_overlay() returns false for exec only
	 * mappings, they don't expose tags (instruction fetches don't check
	 * tags).
	 */
	if (system_supports_mte() && pte_access_permitted_no_overlay(pte, false) &&
	    !pte_special(pte) && pte_tagged(pte))
		mte_sync_tags(pte, nr_pages);
}

/*
 * Select all bits except the pfn
 */
#define pte_pgprot pte_pgprot
static inline pgprot_t pte_pgprot(pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);

	return __pgprot(pte_val(pfn_pte(pfn, __pgprot(0))) ^ pte_val(pte));
}

#define pte_advance_pfn pte_advance_pfn
static inline pte_t pte_advance_pfn(pte_t pte, unsigned long nr)
{
	return pfn_pte(pte_pfn(pte) + nr, pte_pgprot(pte));
}

static inline void __set_ptes(struct mm_struct *mm,
			      unsigned long __always_unused addr,
			      pte_t *ptep, pte_t pte, unsigned int nr)
{
	page_table_check_ptes_set(mm, ptep, pte, nr);
	__sync_cache_and_tags(pte, nr);

	for (;;) {
		__check_safe_pte_update(mm, ptep, pte);
		__set_pte(ptep, pte);
		if (--nr == 0)
			break;
		ptep++;
		pte = pte_advance_pfn(pte, 1);
	}
}

/*
 * Hugetlb definitions.
 */
#define HUGE_MAX_HSTATE		4
#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1, UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

static inline pte_t pgd_pte(pgd_t pgd)
{
	return __pte(pgd_val(pgd));
}

static inline pte_t p4d_pte(p4d_t p4d)
{
	return __pte(p4d_val(p4d));
}

static inline pte_t pud_pte(pud_t pud)
{
	return __pte(pud_val(pud));
}

static inline pud_t pte_pud(pte_t pte)
{
	return __pud(pte_val(pte));
}

static inline pmd_t pud_pmd(pud_t pud)
{
	return __pmd(pud_val(pud));
}

static inline pte_t pmd_pte(pmd_t pmd)
{
	return __pte(pmd_val(pmd));
}

static inline pmd_t pte_pmd(pte_t pte)
{
	return __pmd(pte_val(pte));
}

static inline pgprot_t mk_pud_sect_prot(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~PUD_TYPE_MASK) | PUD_TYPE_SECT);
}

static inline pgprot_t mk_pmd_sect_prot(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~PMD_TYPE_MASK) | PMD_TYPE_SECT);
}

static inline pte_t pte_swp_mkexclusive(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_SWP_EXCLUSIVE));
}

static inline int pte_swp_exclusive(pte_t pte)
{
	return pte_val(pte) & PTE_SWP_EXCLUSIVE;
}

static inline pte_t pte_swp_clear_exclusive(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(PTE_SWP_EXCLUSIVE));
}

#ifdef CONFIG_HAVE_ARCH_USERFAULTFD_WP
static inline pte_t pte_swp_mkuffd_wp(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_SWP_UFFD_WP));
}

static inline int pte_swp_uffd_wp(pte_t pte)
{
	return !!(pte_val(pte) & PTE_SWP_UFFD_WP);
}

static inline pte_t pte_swp_clear_uffd_wp(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(PTE_SWP_UFFD_WP));
}
#endif /* CONFIG_HAVE_ARCH_USERFAULTFD_WP */

#ifdef CONFIG_NUMA_BALANCING
/*
 * See the comment in include/linux/pgtable.h
 */
static inline int pte_protnone(pte_t pte)
{
	/*
	 * pte_present_invalid() tells us that the pte is invalid from HW
	 * perspective but present from SW perspective, so the fields are to be
	 * interpretted as per the HW layout. The second 2 checks are the unique
	 * encoding that we use for PROT_NONE. It is insufficient to only use
	 * the first check because we share the same encoding scheme with pmds
	 * which support pmd_mkinvalid(), so can be present-invalid without
	 * being PROT_NONE.
	 */
	return pte_present_invalid(pte) && !pte_user(pte) && !pte_user_exec(pte);
}

static inline int pmd_protnone(pmd_t pmd)
{
	return pte_protnone(pmd_pte(pmd));
}
#endif

#define pmd_present(pmd)	pte_present(pmd_pte(pmd))
#define pmd_dirty(pmd)		pte_dirty(pmd_pte(pmd))
#define pmd_young(pmd)		pte_young(pmd_pte(pmd))
#define pmd_valid(pmd)		pte_valid(pmd_pte(pmd))
#define pmd_user(pmd)		pte_user(pmd_pte(pmd))
#define pmd_user_exec(pmd)	pte_user_exec(pmd_pte(pmd))
#define pmd_cont(pmd)		pte_cont(pmd_pte(pmd))
#define pmd_wrprotect(pmd)	pte_pmd(pte_wrprotect(pmd_pte(pmd)))
#define pmd_mkold(pmd)		pte_pmd(pte_mkold(pmd_pte(pmd)))
#define pmd_mkwrite_novma(pmd)	pte_pmd(pte_mkwrite_novma(pmd_pte(pmd)))
#define pmd_mkclean(pmd)	pte_pmd(pte_mkclean(pmd_pte(pmd)))
#define pmd_mkdirty(pmd)	pte_pmd(pte_mkdirty(pmd_pte(pmd)))
#define pmd_mkyoung(pmd)	pte_pmd(pte_mkyoung(pmd_pte(pmd)))
#define pmd_mkinvalid(pmd)	pte_pmd(pte_mkinvalid(pmd_pte(pmd)))
#ifdef CONFIG_HAVE_ARCH_USERFAULTFD_WP
#define pmd_uffd_wp(pmd)	pte_uffd_wp(pmd_pte(pmd))
#define pmd_mkuffd_wp(pmd)	pte_pmd(pte_mkuffd_wp(pmd_pte(pmd)))
#define pmd_clear_uffd_wp(pmd)	pte_pmd(pte_clear_uffd_wp(pmd_pte(pmd)))
#define pmd_swp_uffd_wp(pmd)	pte_swp_uffd_wp(pmd_pte(pmd))
#define pmd_swp_mkuffd_wp(pmd)	pte_pmd(pte_swp_mkuffd_wp(pmd_pte(pmd)))
#define pmd_swp_clear_uffd_wp(pmd) \
				pte_pmd(pte_swp_clear_uffd_wp(pmd_pte(pmd)))
#endif /* CONFIG_HAVE_ARCH_USERFAULTFD_WP */

#define pmd_write(pmd)		pte_write(pmd_pte(pmd))

static inline pmd_t pmd_mkhuge(pmd_t pmd)
{
	/*
	 * It's possible that the pmd is present-invalid on entry
	 * and in that case it needs to remain present-invalid on
	 * exit. So ensure the VALID bit does not get modified.
	 */
	pmdval_t mask = PMD_TYPE_MASK & ~PTE_VALID;
	pmdval_t val = PMD_TYPE_SECT & ~PTE_VALID;

	return __pmd((pmd_val(pmd) & ~mask) | val);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define pmd_devmap(pmd)		pte_devmap(pmd_pte(pmd))
#endif
static inline pmd_t pmd_mkdevmap(pmd_t pmd)
{
	return pte_pmd(set_pte_bit(pmd_pte(pmd), __pgprot(PTE_DEVMAP)));
}

#ifdef CONFIG_ARCH_SUPPORTS_PMD_PFNMAP
#define pmd_special(pte)	(!!((pmd_val(pte) & PTE_SPECIAL)))
static inline pmd_t pmd_mkspecial(pmd_t pmd)
{
	return set_pmd_bit(pmd, __pgprot(PTE_SPECIAL));
}
#endif

#define __pmd_to_phys(pmd)	__pte_to_phys(pmd_pte(pmd))
#define __phys_to_pmd_val(phys)	__phys_to_pte_val(phys)
#define pmd_pfn(pmd)		((__pmd_to_phys(pmd) & PMD_MASK) >> PAGE_SHIFT)
#define pfn_pmd(pfn,prot)	__pmd(__phys_to_pmd_val((phys_addr_t)(pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define mk_pmd(page,prot)	pfn_pmd(page_to_pfn(page),prot)

#define pud_young(pud)		pte_young(pud_pte(pud))
#define pud_mkyoung(pud)	pte_pud(pte_mkyoung(pud_pte(pud)))
#define pud_write(pud)		pte_write(pud_pte(pud))

static inline pud_t pud_mkhuge(pud_t pud)
{
	/*
	 * It's possible that the pud is present-invalid on entry
	 * and in that case it needs to remain present-invalid on
	 * exit. So ensure the VALID bit does not get modified.
	 */
	pudval_t mask = PUD_TYPE_MASK & ~PTE_VALID;
	pudval_t val = PUD_TYPE_SECT & ~PTE_VALID;

	return __pud((pud_val(pud) & ~mask) | val);
}

#define __pud_to_phys(pud)	__pte_to_phys(pud_pte(pud))
#define __phys_to_pud_val(phys)	__phys_to_pte_val(phys)
#define pud_pfn(pud)		((__pud_to_phys(pud) & PUD_MASK) >> PAGE_SHIFT)
#define pfn_pud(pfn,prot)	__pud(__phys_to_pud_val((phys_addr_t)(pfn) << PAGE_SHIFT) | pgprot_val(prot))

#ifdef CONFIG_ARCH_SUPPORTS_PUD_PFNMAP
#define pud_special(pte)	pte_special(pud_pte(pud))
#define pud_mkspecial(pte)	pte_pud(pte_mkspecial(pud_pte(pud)))
#endif

#define pmd_pgprot pmd_pgprot
static inline pgprot_t pmd_pgprot(pmd_t pmd)
{
	unsigned long pfn = pmd_pfn(pmd);

	return __pgprot(pmd_val(pfn_pmd(pfn, __pgprot(0))) ^ pmd_val(pmd));
}

#define pud_pgprot pud_pgprot
static inline pgprot_t pud_pgprot(pud_t pud)
{
	unsigned long pfn = pud_pfn(pud);

	return __pgprot(pud_val(pfn_pud(pfn, __pgprot(0))) ^ pud_val(pud));
}

static inline void __set_pte_at(struct mm_struct *mm,
				unsigned long __always_unused addr,
				pte_t *ptep, pte_t pte, unsigned int nr)
{
	__sync_cache_and_tags(pte, nr);
	__check_safe_pte_update(mm, ptep, pte);
	__set_pte(ptep, pte);
}

static inline void set_pmd_at(struct mm_struct *mm, unsigned long addr,
			      pmd_t *pmdp, pmd_t pmd)
{
	page_table_check_pmd_set(mm, pmdp, pmd);
	return __set_pte_at(mm, addr, (pte_t *)pmdp, pmd_pte(pmd),
						PMD_SIZE >> PAGE_SHIFT);
}

static inline void set_pud_at(struct mm_struct *mm, unsigned long addr,
			      pud_t *pudp, pud_t pud)
{
	page_table_check_pud_set(mm, pudp, pud);
	return __set_pte_at(mm, addr, (pte_t *)pudp, pud_pte(pud),
						PUD_SIZE >> PAGE_SHIFT);
}

#define __p4d_to_phys(p4d)	__pte_to_phys(p4d_pte(p4d))
#define __phys_to_p4d_val(phys)	__phys_to_pte_val(phys)

#define __pgd_to_phys(pgd)	__pte_to_phys(pgd_pte(pgd))
#define __phys_to_pgd_val(phys)	__phys_to_pte_val(phys)

#define __pgprot_modify(prot,mask,bits) \
	__pgprot((pgprot_val(prot) & ~(mask)) | (bits))

#define pgprot_nx(prot) \
	__pgprot_modify(prot, PTE_MAYBE_GP, PTE_PXN)

#define pgprot_decrypted(prot) \
	__pgprot_modify(prot, PROT_NS_SHARED, PROT_NS_SHARED)
#define pgprot_encrypted(prot) \
	__pgprot_modify(prot, PROT_NS_SHARED, 0)

/*
 * Mark the prot value as uncacheable and unbufferable.
 */
#define pgprot_noncached(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_DEVICE_nGnRnE) | PTE_PXN | PTE_UXN)
#define pgprot_writecombine(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_NORMAL_NC) | PTE_PXN | PTE_UXN)
#define pgprot_device(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_DEVICE_nGnRE) | PTE_PXN | PTE_UXN)
#define pgprot_tagged(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_NORMAL_TAGGED))
#define pgprot_mhp	pgprot_tagged
/*
 * DMA allocations for non-coherent devices use what the Arm architecture calls
 * "Normal non-cacheable" memory, which permits speculation, unaligned accesses
 * and merging of writes.  This is different from "Device-nGnR[nE]" memory which
 * is intended for MMIO and thus forbids speculation, preserves access size,
 * requires strict alignment and can also force write responses to come from the
 * endpoint.
 */
#define pgprot_dmacoherent(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, \
			PTE_ATTRINDX(MT_NORMAL_NC) | PTE_PXN | PTE_UXN)

#define __HAVE_PHYS_MEM_ACCESS_PROT
struct file;
extern pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot);

#define pmd_none(pmd)		(!pmd_val(pmd))

#define pmd_table(pmd)		((pmd_val(pmd) & PMD_TYPE_MASK) == \
				 PMD_TYPE_TABLE)
#define pmd_sect(pmd)		((pmd_val(pmd) & PMD_TYPE_MASK) == \
				 PMD_TYPE_SECT)
#define pmd_leaf(pmd)		(pmd_present(pmd) && !pmd_table(pmd))
#define pmd_bad(pmd)		(!pmd_table(pmd))

#define pmd_leaf_size(pmd)	(pmd_cont(pmd) ? CONT_PMD_SIZE : PMD_SIZE)
#define pte_leaf_size(pte)	(pte_cont(pte) ? CONT_PTE_SIZE : PAGE_SIZE)

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline int pmd_trans_huge(pmd_t pmd)
{
	/*
	 * If pmd is present-invalid, pmd_table() won't detect it
	 * as a table, so force the valid bit for the comparison.
	 */
	return pmd_val(pmd) && pmd_present(pmd) &&
	       !pmd_table(__pmd(pmd_val(pmd) | PTE_VALID));
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#if defined(CONFIG_ARM64_64K_PAGES) || CONFIG_PGTABLE_LEVELS < 3
static inline bool pud_sect(pud_t pud) { return false; }
static inline bool pud_table(pud_t pud) { return true; }
#else
#define pud_sect(pud)		((pud_val(pud) & PUD_TYPE_MASK) == \
				 PUD_TYPE_SECT)
#define pud_table(pud)		((pud_val(pud) & PUD_TYPE_MASK) == \
				 PUD_TYPE_TABLE)
#endif

extern pgd_t init_pg_dir[];
extern pgd_t init_pg_end[];
extern pgd_t swapper_pg_dir[];
extern pgd_t idmap_pg_dir[];
extern pgd_t tramp_pg_dir[];
extern pgd_t reserved_pg_dir[];

extern void set_swapper_pgd(pgd_t *pgdp, pgd_t pgd);

static inline bool in_swapper_pgdir(void *addr)
{
	return ((unsigned long)addr & PAGE_MASK) ==
	        ((unsigned long)swapper_pg_dir & PAGE_MASK);
}

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
#ifdef __PAGETABLE_PMD_FOLDED
	if (in_swapper_pgdir(pmdp)) {
		set_swapper_pgd((pgd_t *)pmdp, __pgd(pmd_val(pmd)));
		return;
	}
#endif /* __PAGETABLE_PMD_FOLDED */

	WRITE_ONCE(*pmdp, pmd);

	if (pmd_valid(pmd)) {
		dsb(ishst);
		isb();
	}
}

static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}

static inline phys_addr_t pmd_page_paddr(pmd_t pmd)
{
	return __pmd_to_phys(pmd);
}

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return (unsigned long)__va(pmd_page_paddr(pmd));
}

/* Find an entry in the third-level page table. */
#define pte_offset_phys(dir,addr)	(pmd_page_paddr(READ_ONCE(*(dir))) + pte_index(addr) * sizeof(pte_t))

#define pte_set_fixmap(addr)		((pte_t *)set_fixmap_offset(FIX_PTE, addr))
#define pte_set_fixmap_offset(pmd, addr)	pte_set_fixmap(pte_offset_phys(pmd, addr))
#define pte_clear_fixmap()		clear_fixmap(FIX_PTE)

#define pmd_page(pmd)			phys_to_page(__pmd_to_phys(pmd))

/* use ONLY for statically allocated translation tables */
#define pte_offset_kimg(dir,addr)	((pte_t *)__phys_to_kimg(pte_offset_phys((dir), (addr))))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page,prot)	pfn_pte(page_to_pfn(page),prot)

#if CONFIG_PGTABLE_LEVELS > 2

#define pmd_ERROR(e)	\
	pr_err("%s:%d: bad pmd %016llx.\n", __FILE__, __LINE__, pmd_val(e))

#define pud_none(pud)		(!pud_val(pud))
#define pud_bad(pud)		((pud_val(pud) & PUD_TYPE_MASK) != \
				 PUD_TYPE_TABLE)
#define pud_present(pud)	pte_present(pud_pte(pud))
#ifndef __PAGETABLE_PMD_FOLDED
#define pud_leaf(pud)		(pud_present(pud) && !pud_table(pud))
#else
#define pud_leaf(pud)		false
#endif
#define pud_valid(pud)		pte_valid(pud_pte(pud))
#define pud_user(pud)		pte_user(pud_pte(pud))
#define pud_user_exec(pud)	pte_user_exec(pud_pte(pud))

static inline bool pgtable_l4_enabled(void);

static inline void set_pud(pud_t *pudp, pud_t pud)
{
	if (!pgtable_l4_enabled() && in_swapper_pgdir(pudp)) {
		set_swapper_pgd((pgd_t *)pudp, __pgd(pud_val(pud)));
		return;
	}

	WRITE_ONCE(*pudp, pud);

	if (pud_valid(pud)) {
		dsb(ishst);
		isb();
	}
}

static inline void pud_clear(pud_t *pudp)
{
	set_pud(pudp, __pud(0));
}

static inline phys_addr_t pud_page_paddr(pud_t pud)
{
	return __pud_to_phys(pud);
}

static inline pmd_t *pud_pgtable(pud_t pud)
{
	return (pmd_t *)__va(pud_page_paddr(pud));
}

/* Find an entry in the second-level page table. */
#define pmd_offset_phys(dir, addr)	(pud_page_paddr(READ_ONCE(*(dir))) + pmd_index(addr) * sizeof(pmd_t))

#define pmd_set_fixmap(addr)		((pmd_t *)set_fixmap_offset(FIX_PMD, addr))
#define pmd_set_fixmap_offset(pud, addr)	pmd_set_fixmap(pmd_offset_phys(pud, addr))
#define pmd_clear_fixmap()		clear_fixmap(FIX_PMD)

#define pud_page(pud)			phys_to_page(__pud_to_phys(pud))

/* use ONLY for statically allocated translation tables */
#define pmd_offset_kimg(dir,addr)	((pmd_t *)__phys_to_kimg(pmd_offset_phys((dir), (addr))))

#else

#define pud_valid(pud)		false
#define pud_page_paddr(pud)	({ BUILD_BUG(); 0; })
#define pud_user_exec(pud)	pud_user(pud) /* Always 0 with folding */

/* Match pmd_offset folding in <asm/generic/pgtable-nopmd.h> */
#define pmd_set_fixmap(addr)		NULL
#define pmd_set_fixmap_offset(pudp, addr)	((pmd_t *)pudp)
#define pmd_clear_fixmap()

#define pmd_offset_kimg(dir,addr)	((pmd_t *)dir)

#endif	/* CONFIG_PGTABLE_LEVELS > 2 */

#if CONFIG_PGTABLE_LEVELS > 3

static __always_inline bool pgtable_l4_enabled(void)
{
	if (CONFIG_PGTABLE_LEVELS > 4 || !IS_ENABLED(CONFIG_ARM64_LPA2))
		return true;
	if (!alternative_has_cap_likely(ARM64_ALWAYS_BOOT))
		return vabits_actual == VA_BITS;
	return alternative_has_cap_unlikely(ARM64_HAS_VA52);
}

static inline bool mm_pud_folded(const struct mm_struct *mm)
{
	return !pgtable_l4_enabled();
}
#define mm_pud_folded  mm_pud_folded

#define pud_ERROR(e)	\
	pr_err("%s:%d: bad pud %016llx.\n", __FILE__, __LINE__, pud_val(e))

#define p4d_none(p4d)		(pgtable_l4_enabled() && !p4d_val(p4d))
#define p4d_bad(p4d)		(pgtable_l4_enabled() && \
				((p4d_val(p4d) & P4D_TYPE_MASK) != \
				 P4D_TYPE_TABLE))
#define p4d_present(p4d)	(!p4d_none(p4d))

static inline void set_p4d(p4d_t *p4dp, p4d_t p4d)
{
	if (in_swapper_pgdir(p4dp)) {
		set_swapper_pgd((pgd_t *)p4dp, __pgd(p4d_val(p4d)));
		return;
	}

	WRITE_ONCE(*p4dp, p4d);
	dsb(ishst);
	isb();
}

static inline void p4d_clear(p4d_t *p4dp)
{
	if (pgtable_l4_enabled())
		set_p4d(p4dp, __p4d(0));
}

static inline phys_addr_t p4d_page_paddr(p4d_t p4d)
{
	return __p4d_to_phys(p4d);
}

#define pud_index(addr)		(((addr) >> PUD_SHIFT) & (PTRS_PER_PUD - 1))

static inline pud_t *p4d_to_folded_pud(p4d_t *p4dp, unsigned long addr)
{
	/* Ensure that 'p4dp' indexes a page table according to 'addr' */
	VM_BUG_ON(((addr >> P4D_SHIFT) ^ ((u64)p4dp >> 3)) % PTRS_PER_P4D);

	return (pud_t *)PTR_ALIGN_DOWN(p4dp, PAGE_SIZE) + pud_index(addr);
}

static inline pud_t *p4d_pgtable(p4d_t p4d)
{
	return (pud_t *)__va(p4d_page_paddr(p4d));
}

static inline phys_addr_t pud_offset_phys(p4d_t *p4dp, unsigned long addr)
{
	BUG_ON(!pgtable_l4_enabled());

	return p4d_page_paddr(READ_ONCE(*p4dp)) + pud_index(addr) * sizeof(pud_t);
}

static inline
pud_t *pud_offset_lockless(p4d_t *p4dp, p4d_t p4d, unsigned long addr)
{
	if (!pgtable_l4_enabled())
		return p4d_to_folded_pud(p4dp, addr);
	return (pud_t *)__va(p4d_page_paddr(p4d)) + pud_index(addr);
}
#define pud_offset_lockless pud_offset_lockless

static inline pud_t *pud_offset(p4d_t *p4dp, unsigned long addr)
{
	return pud_offset_lockless(p4dp, READ_ONCE(*p4dp), addr);
}
#define pud_offset	pud_offset

static inline pud_t *pud_set_fixmap(unsigned long addr)
{
	if (!pgtable_l4_enabled())
		return NULL;
	return (pud_t *)set_fixmap_offset(FIX_PUD, addr);
}

static inline pud_t *pud_set_fixmap_offset(p4d_t *p4dp, unsigned long addr)
{
	if (!pgtable_l4_enabled())
		return p4d_to_folded_pud(p4dp, addr);
	return pud_set_fixmap(pud_offset_phys(p4dp, addr));
}

static inline void pud_clear_fixmap(void)
{
	if (pgtable_l4_enabled())
		clear_fixmap(FIX_PUD);
}

/* use ONLY for statically allocated translation tables */
static inline pud_t *pud_offset_kimg(p4d_t *p4dp, u64 addr)
{
	if (!pgtable_l4_enabled())
		return p4d_to_folded_pud(p4dp, addr);
	return (pud_t *)__phys_to_kimg(pud_offset_phys(p4dp, addr));
}

#define p4d_page(p4d)		pfn_to_page(__phys_to_pfn(__p4d_to_phys(p4d)))

#else

static inline bool pgtable_l4_enabled(void) { return false; }

#define p4d_page_paddr(p4d)	({ BUILD_BUG(); 0;})

/* Match pud_offset folding in <asm/generic/pgtable-nopud.h> */
#define pud_set_fixmap(addr)		NULL
#define pud_set_fixmap_offset(pgdp, addr)	((pud_t *)pgdp)
#define pud_clear_fixmap()

#define pud_offset_kimg(dir,addr)	((pud_t *)dir)

#endif  /* CONFIG_PGTABLE_LEVELS > 3 */

#if CONFIG_PGTABLE_LEVELS > 4

static __always_inline bool pgtable_l5_enabled(void)
{
	if (!alternative_has_cap_likely(ARM64_ALWAYS_BOOT))
		return vabits_actual == VA_BITS;
	return alternative_has_cap_unlikely(ARM64_HAS_VA52);
}

static inline bool mm_p4d_folded(const struct mm_struct *mm)
{
	return !pgtable_l5_enabled();
}
#define mm_p4d_folded  mm_p4d_folded

#define p4d_ERROR(e)	\
	pr_err("%s:%d: bad p4d %016llx.\n", __FILE__, __LINE__, p4d_val(e))

#define pgd_none(pgd)		(pgtable_l5_enabled() && !pgd_val(pgd))
#define pgd_bad(pgd)		(pgtable_l5_enabled() && \
				((pgd_val(pgd) & PGD_TYPE_MASK) != \
				 PGD_TYPE_TABLE))
#define pgd_present(pgd)	(!pgd_none(pgd))

static inline void set_pgd(pgd_t *pgdp, pgd_t pgd)
{
	if (in_swapper_pgdir(pgdp)) {
		set_swapper_pgd(pgdp, __pgd(pgd_val(pgd)));
		return;
	}

	WRITE_ONCE(*pgdp, pgd);
	dsb(ishst);
	isb();
}

static inline void pgd_clear(pgd_t *pgdp)
{
	if (pgtable_l5_enabled())
		set_pgd(pgdp, __pgd(0));
}

static inline phys_addr_t pgd_page_paddr(pgd_t pgd)
{
	return __pgd_to_phys(pgd);
}

#define p4d_index(addr)		(((addr) >> P4D_SHIFT) & (PTRS_PER_P4D - 1))

static inline p4d_t *pgd_to_folded_p4d(pgd_t *pgdp, unsigned long addr)
{
	/* Ensure that 'pgdp' indexes a page table according to 'addr' */
	VM_BUG_ON(((addr >> PGDIR_SHIFT) ^ ((u64)pgdp >> 3)) % PTRS_PER_PGD);

	return (p4d_t *)PTR_ALIGN_DOWN(pgdp, PAGE_SIZE) + p4d_index(addr);
}

static inline phys_addr_t p4d_offset_phys(pgd_t *pgdp, unsigned long addr)
{
	BUG_ON(!pgtable_l5_enabled());

	return pgd_page_paddr(READ_ONCE(*pgdp)) + p4d_index(addr) * sizeof(p4d_t);
}

static inline
p4d_t *p4d_offset_lockless(pgd_t *pgdp, pgd_t pgd, unsigned long addr)
{
	if (!pgtable_l5_enabled())
		return pgd_to_folded_p4d(pgdp, addr);
	return (p4d_t *)__va(pgd_page_paddr(pgd)) + p4d_index(addr);
}
#define p4d_offset_lockless p4d_offset_lockless

static inline p4d_t *p4d_offset(pgd_t *pgdp, unsigned long addr)
{
	return p4d_offset_lockless(pgdp, READ_ONCE(*pgdp), addr);
}

static inline p4d_t *p4d_set_fixmap(unsigned long addr)
{
	if (!pgtable_l5_enabled())
		return NULL;
	return (p4d_t *)set_fixmap_offset(FIX_P4D, addr);
}

static inline p4d_t *p4d_set_fixmap_offset(pgd_t *pgdp, unsigned long addr)
{
	if (!pgtable_l5_enabled())
		return pgd_to_folded_p4d(pgdp, addr);
	return p4d_set_fixmap(p4d_offset_phys(pgdp, addr));
}

static inline void p4d_clear_fixmap(void)
{
	if (pgtable_l5_enabled())
		clear_fixmap(FIX_P4D);
}

/* use ONLY for statically allocated translation tables */
static inline p4d_t *p4d_offset_kimg(pgd_t *pgdp, u64 addr)
{
	if (!pgtable_l5_enabled())
		return pgd_to_folded_p4d(pgdp, addr);
	return (p4d_t *)__phys_to_kimg(p4d_offset_phys(pgdp, addr));
}

#define pgd_page(pgd)		pfn_to_page(__phys_to_pfn(__pgd_to_phys(pgd)))

#else

static inline bool pgtable_l5_enabled(void) { return false; }

#define p4d_index(addr)		(((addr) >> P4D_SHIFT) & (PTRS_PER_P4D - 1))

/* Match p4d_offset folding in <asm/generic/pgtable-nop4d.h> */
#define p4d_set_fixmap(addr)		NULL
#define p4d_set_fixmap_offset(p4dp, addr)	((p4d_t *)p4dp)
#define p4d_clear_fixmap()

#define p4d_offset_kimg(dir,addr)	((p4d_t *)dir)

static inline
p4d_t *p4d_offset_lockless_folded(pgd_t *pgdp, pgd_t pgd, unsigned long addr)
{
	/*
	 * With runtime folding of the pud, pud_offset_lockless() passes
	 * the 'pgd_t *' we return here to p4d_to_folded_pud(), which
	 * will offset the pointer assuming that it points into
	 * a page-table page. However, the fast GUP path passes us a
	 * pgd_t allocated on the stack and so we must use the original
	 * pointer in 'pgdp' to construct the p4d pointer instead of
	 * using the generic p4d_offset_lockless() implementation.
	 *
	 * Note: reusing the original pointer means that we may
	 * dereference the same (live) page-table entry multiple times.
	 * This is safe because it is still only loaded once in the
	 * context of each level and the CPU guarantees same-address
	 * read-after-read ordering.
	 */
	return p4d_offset(pgdp, addr);
}
#define p4d_offset_lockless p4d_offset_lockless_folded

#endif  /* CONFIG_PGTABLE_LEVELS > 4 */

#define pgd_ERROR(e)	\
	pr_err("%s:%d: bad pgd %016llx.\n", __FILE__, __LINE__, pgd_val(e))

#define pgd_set_fixmap(addr)	((pgd_t *)set_fixmap_offset(FIX_PGD, addr))
#define pgd_clear_fixmap()	clear_fixmap(FIX_PGD)

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	/*
	 * Normal and Normal-Tagged are two different memory types and indices
	 * in MAIR_EL1. The mask below has to include PTE_ATTRINDX_MASK.
	 */
	const pteval_t mask = PTE_USER | PTE_PXN | PTE_UXN | PTE_RDONLY |
			      PTE_PRESENT_INVALID | PTE_VALID | PTE_WRITE |
			      PTE_GP | PTE_ATTRINDX_MASK | PTE_PO_IDX_MASK;

	/* preserve the hardware dirty information */
	if (pte_hw_dirty(pte))
		pte = set_pte_bit(pte, __pgprot(PTE_DIRTY));

	pte_val(pte) = (pte_val(pte) & ~mask) | (pgprot_val(newprot) & mask);
	/*
	 * If we end up clearing hw dirtiness for a sw-dirty PTE, set hardware
	 * dirtiness again.
	 */
	if (pte_sw_dirty(pte))
		pte = pte_mkdirty(pte);
	return pte;
}

static inline pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot)
{
	return pte_pmd(pte_modify(pmd_pte(pmd), newprot));
}

extern int __ptep_set_access_flags(struct vm_area_struct *vma,
				 unsigned long address, pte_t *ptep,
				 pte_t entry, int dirty);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define __HAVE_ARCH_PMDP_SET_ACCESS_FLAGS
static inline int pmdp_set_access_flags(struct vm_area_struct *vma,
					unsigned long address, pmd_t *pmdp,
					pmd_t entry, int dirty)
{
	return __ptep_set_access_flags(vma, address, (pte_t *)pmdp,
							pmd_pte(entry), dirty);
}

static inline int pud_devmap(pud_t pud)
{
	return 0;
}

static inline int pgd_devmap(pgd_t pgd)
{
	return 0;
}
#endif

#ifdef CONFIG_PAGE_TABLE_CHECK
static inline bool pte_user_accessible_page(pte_t pte)
{
	return pte_valid(pte) && (pte_user(pte) || pte_user_exec(pte));
}

static inline bool pmd_user_accessible_page(pmd_t pmd)
{
	return pmd_valid(pmd) && !pmd_table(pmd) && (pmd_user(pmd) || pmd_user_exec(pmd));
}

static inline bool pud_user_accessible_page(pud_t pud)
{
	return pud_valid(pud) && !pud_table(pud) && (pud_user(pud) || pud_user_exec(pud));
}
#endif

/*
 * Atomic pte/pmd modifications.
 */
static inline int __ptep_test_and_clear_young(struct vm_area_struct *vma,
					      unsigned long address,
					      pte_t *ptep)
{
	pte_t old_pte, pte;

	pte = __ptep_get(ptep);
	do {
		old_pte = pte;
		pte = pte_mkold(pte);
		pte_val(pte) = cmpxchg_relaxed(&pte_val(*ptep),
					       pte_val(old_pte), pte_val(pte));
	} while (pte_val(pte) != pte_val(old_pte));

	return pte_young(pte);
}

static inline int __ptep_clear_flush_young(struct vm_area_struct *vma,
					 unsigned long address, pte_t *ptep)
{
	int young = __ptep_test_and_clear_young(vma, address, ptep);

	if (young) {
		/*
		 * We can elide the trailing DSB here since the worst that can
		 * happen is that a CPU continues to use the young entry in its
		 * TLB and we mistakenly reclaim the associated page. The
		 * window for such an event is bounded by the next
		 * context-switch, which provides a DSB to complete the TLB
		 * invalidation.
		 */
		flush_tlb_page_nosync(vma, address);
	}

	return young;
}

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) || defined(CONFIG_ARCH_HAS_NONLEAF_PMD_YOUNG)
#define __HAVE_ARCH_PMDP_TEST_AND_CLEAR_YOUNG
static inline int pmdp_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long address,
					    pmd_t *pmdp)
{
	/* Operation applies to PMD table entry only if FEAT_HAFT is enabled */
	VM_WARN_ON(pmd_table(READ_ONCE(*pmdp)) && !system_supports_haft());
	return __ptep_test_and_clear_young(vma, address, (pte_t *)pmdp);
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE || CONFIG_ARCH_HAS_NONLEAF_PMD_YOUNG */

static inline pte_t __ptep_get_and_clear(struct mm_struct *mm,
				       unsigned long address, pte_t *ptep)
{
	pte_t pte = __pte(xchg_relaxed(&pte_val(*ptep), 0));

	page_table_check_pte_clear(mm, pte);

	return pte;
}

static inline void __clear_full_ptes(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, unsigned int nr, int full)
{
	for (;;) {
		__ptep_get_and_clear(mm, addr, ptep);
		if (--nr == 0)
			break;
		ptep++;
		addr += PAGE_SIZE;
	}
}

static inline pte_t __get_and_clear_full_ptes(struct mm_struct *mm,
				unsigned long addr, pte_t *ptep,
				unsigned int nr, int full)
{
	pte_t pte, tmp_pte;

	pte = __ptep_get_and_clear(mm, addr, ptep);
	while (--nr) {
		ptep++;
		addr += PAGE_SIZE;
		tmp_pte = __ptep_get_and_clear(mm, addr, ptep);
		if (pte_dirty(tmp_pte))
			pte = pte_mkdirty(pte);
		if (pte_young(tmp_pte))
			pte = pte_mkyoung(pte);
	}
	return pte;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define __HAVE_ARCH_PMDP_HUGE_GET_AND_CLEAR
static inline pmd_t pmdp_huge_get_and_clear(struct mm_struct *mm,
					    unsigned long address, pmd_t *pmdp)
{
	pmd_t pmd = __pmd(xchg_relaxed(&pmd_val(*pmdp), 0));

	page_table_check_pmd_clear(mm, pmd);

	return pmd;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

static inline void ___ptep_set_wrprotect(struct mm_struct *mm,
					unsigned long address, pte_t *ptep,
					pte_t pte)
{
	pte_t old_pte;

	do {
		old_pte = pte;
		pte = pte_wrprotect(pte);
		pte_val(pte) = cmpxchg_relaxed(&pte_val(*ptep),
					       pte_val(old_pte), pte_val(pte));
	} while (pte_val(pte) != pte_val(old_pte));
}

/*
 * __ptep_set_wrprotect - mark read-only while transferring potential hardware
 * dirty status (PTE_DBM && !PTE_RDONLY) to the software PTE_DIRTY bit.
 */
static inline void __ptep_set_wrprotect(struct mm_struct *mm,
					unsigned long address, pte_t *ptep)
{
	___ptep_set_wrprotect(mm, address, ptep, __ptep_get(ptep));
}

static inline void __wrprotect_ptes(struct mm_struct *mm, unsigned long address,
				pte_t *ptep, unsigned int nr)
{
	unsigned int i;

	for (i = 0; i < nr; i++, address += PAGE_SIZE, ptep++)
		__ptep_set_wrprotect(mm, address, ptep);
}

static inline void __clear_young_dirty_pte(struct vm_area_struct *vma,
					   unsigned long addr, pte_t *ptep,
					   pte_t pte, cydp_t flags)
{
	pte_t old_pte;

	do {
		old_pte = pte;

		if (flags & CYDP_CLEAR_YOUNG)
			pte = pte_mkold(pte);
		if (flags & CYDP_CLEAR_DIRTY)
			pte = pte_mkclean(pte);

		pte_val(pte) = cmpxchg_relaxed(&pte_val(*ptep),
					       pte_val(old_pte), pte_val(pte));
	} while (pte_val(pte) != pte_val(old_pte));
}

static inline void __clear_young_dirty_ptes(struct vm_area_struct *vma,
					    unsigned long addr, pte_t *ptep,
					    unsigned int nr, cydp_t flags)
{
	pte_t pte;

	for (;;) {
		pte = __ptep_get(ptep);

		if (flags == (CYDP_CLEAR_YOUNG | CYDP_CLEAR_DIRTY))
			__set_pte(ptep, pte_mkclean(pte_mkold(pte)));
		else
			__clear_young_dirty_pte(vma, addr, ptep, pte, flags);

		if (--nr == 0)
			break;
		ptep++;
		addr += PAGE_SIZE;
	}
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define __HAVE_ARCH_PMDP_SET_WRPROTECT
static inline void pmdp_set_wrprotect(struct mm_struct *mm,
				      unsigned long address, pmd_t *pmdp)
{
	__ptep_set_wrprotect(mm, address, (pte_t *)pmdp);
}

#define pmdp_establish pmdp_establish
static inline pmd_t pmdp_establish(struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmdp, pmd_t pmd)
{
	page_table_check_pmd_set(vma->vm_mm, pmdp, pmd);
	return __pmd(xchg_relaxed(&pmd_val(*pmdp), pmd_val(pmd)));
}
#endif

/*
 * Encode and decode a swap entry:
 *	bits 0-1:	present (must be zero)
 *	bits 2:		remember PG_anon_exclusive
 *	bit  3:		remember uffd-wp state
 *	bits 6-10:	swap type
 *	bit  11:	PTE_PRESENT_INVALID (must be zero)
 *	bits 12-61:	swap offset
 */
#define __SWP_TYPE_SHIFT	6
#define __SWP_TYPE_BITS		5
#define __SWP_TYPE_MASK		((1 << __SWP_TYPE_BITS) - 1)
#define __SWP_OFFSET_SHIFT	12
#define __SWP_OFFSET_BITS	50
#define __SWP_OFFSET_MASK	((1UL << __SWP_OFFSET_BITS) - 1)

#define __swp_type(x)		(((x).val >> __SWP_TYPE_SHIFT) & __SWP_TYPE_MASK)
#define __swp_offset(x)		(((x).val >> __SWP_OFFSET_SHIFT) & __SWP_OFFSET_MASK)
#define __swp_entry(type,offset) ((swp_entry_t) { ((type) << __SWP_TYPE_SHIFT) | ((offset) << __SWP_OFFSET_SHIFT) })

#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(swp)	((pte_t) { (swp).val })

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
#define __pmd_to_swp_entry(pmd)		((swp_entry_t) { pmd_val(pmd) })
#define __swp_entry_to_pmd(swp)		__pmd((swp).val)
#endif /* CONFIG_ARCH_ENABLE_THP_MIGRATION */

/*
 * Ensure that there are not more swap files than can be encoded in the kernel
 * PTEs.
 */
#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > __SWP_TYPE_BITS)

#ifdef CONFIG_ARM64_MTE

#define __HAVE_ARCH_PREPARE_TO_SWAP
extern int arch_prepare_to_swap(struct folio *folio);

#define __HAVE_ARCH_SWAP_INVALIDATE
static inline void arch_swap_invalidate_page(int type, pgoff_t offset)
{
	if (system_supports_mte())
		mte_invalidate_tags(type, offset);
}

static inline void arch_swap_invalidate_area(int type)
{
	if (system_supports_mte())
		mte_invalidate_tags_area(type);
}

#define __HAVE_ARCH_SWAP_RESTORE
extern void arch_swap_restore(swp_entry_t entry, struct folio *folio);

#endif /* CONFIG_ARM64_MTE */

/*
 * On AArch64, the cache coherency is handled via the __set_ptes() function.
 */
static inline void update_mmu_cache_range(struct vm_fault *vmf,
		struct vm_area_struct *vma, unsigned long addr, pte_t *ptep,
		unsigned int nr)
{
	/*
	 * We don't do anything here, so there's a very small chance of
	 * us retaking a user fault which we just fixed up. The alternative
	 * is doing a dsb(ishst), but that penalises the fastpath.
	 */
}

#define update_mmu_cache(vma, addr, ptep) \
	update_mmu_cache_range(NULL, vma, addr, ptep, 1)
#define update_mmu_cache_pmd(vma, address, pmd) do { } while (0)

#ifdef CONFIG_ARM64_PA_BITS_52
#define phys_to_ttbr(addr)	(((addr) | ((addr) >> 46)) & TTBR_BADDR_MASK_52)
#else
#define phys_to_ttbr(addr)	(addr)
#endif

/*
 * On arm64 without hardware Access Flag, copying from user will fail because
 * the pte is old and cannot be marked young. So we always end up with zeroed
 * page after fork() + CoW for pfn mappings. We don't always have a
 * hardware-managed access flag on arm64.
 */
#define arch_has_hw_pte_young		cpu_has_hw_af

#ifdef CONFIG_ARCH_HAS_NONLEAF_PMD_YOUNG
#define arch_has_hw_nonleaf_pmd_young	system_supports_haft
#endif

/*
 * Experimentally, it's cheap to set the access flag in hardware and we
 * benefit from prefaulting mappings as 'old' to start with.
 */
#define arch_wants_old_prefaulted_pte	cpu_has_hw_af

static inline bool pud_sect_supported(void)
{
	return PAGE_SIZE == SZ_4K;
}


#define __HAVE_ARCH_PTEP_MODIFY_PROT_TRANSACTION
#define ptep_modify_prot_start ptep_modify_prot_start
extern pte_t ptep_modify_prot_start(struct vm_area_struct *vma,
				    unsigned long addr, pte_t *ptep);

#define ptep_modify_prot_commit ptep_modify_prot_commit
extern void ptep_modify_prot_commit(struct vm_area_struct *vma,
				    unsigned long addr, pte_t *ptep,
				    pte_t old_pte, pte_t new_pte);

#ifdef CONFIG_ARM64_CONTPTE

/*
 * The contpte APIs are used to transparently manage the contiguous bit in ptes
 * where it is possible and makes sense to do so. The PTE_CONT bit is considered
 * a private implementation detail of the public ptep API (see below).
 */
extern void __contpte_try_fold(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, pte_t pte);
extern void __contpte_try_unfold(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, pte_t pte);
extern pte_t contpte_ptep_get(pte_t *ptep, pte_t orig_pte);
extern pte_t contpte_ptep_get_lockless(pte_t *orig_ptep);
extern void contpte_set_ptes(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, pte_t pte, unsigned int nr);
extern void contpte_clear_full_ptes(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, unsigned int nr, int full);
extern pte_t contpte_get_and_clear_full_ptes(struct mm_struct *mm,
				unsigned long addr, pte_t *ptep,
				unsigned int nr, int full);
extern int contpte_ptep_test_and_clear_young(struct vm_area_struct *vma,
				unsigned long addr, pte_t *ptep);
extern int contpte_ptep_clear_flush_young(struct vm_area_struct *vma,
				unsigned long addr, pte_t *ptep);
extern void contpte_wrprotect_ptes(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, unsigned int nr);
extern int contpte_ptep_set_access_flags(struct vm_area_struct *vma,
				unsigned long addr, pte_t *ptep,
				pte_t entry, int dirty);
extern void contpte_clear_young_dirty_ptes(struct vm_area_struct *vma,
				unsigned long addr, pte_t *ptep,
				unsigned int nr, cydp_t flags);

static __always_inline void contpte_try_fold(struct mm_struct *mm,
				unsigned long addr, pte_t *ptep, pte_t pte)
{
	/*
	 * Only bother trying if both the virtual and physical addresses are
	 * aligned and correspond to the last entry in a contig range. The core
	 * code mostly modifies ranges from low to high, so this is the likely
	 * the last modification in the contig range, so a good time to fold.
	 * We can't fold special mappings, because there is no associated folio.
	 */

	const unsigned long contmask = CONT_PTES - 1;
	bool valign = ((addr >> PAGE_SHIFT) & contmask) == contmask;

	if (unlikely(valign)) {
		bool palign = (pte_pfn(pte) & contmask) == contmask;

		if (unlikely(palign &&
		    pte_valid(pte) && !pte_cont(pte) && !pte_special(pte)))
			__contpte_try_fold(mm, addr, ptep, pte);
	}
}

static __always_inline void contpte_try_unfold(struct mm_struct *mm,
				unsigned long addr, pte_t *ptep, pte_t pte)
{
	if (unlikely(pte_valid_cont(pte)))
		__contpte_try_unfold(mm, addr, ptep, pte);
}

#define pte_batch_hint pte_batch_hint
static inline unsigned int pte_batch_hint(pte_t *ptep, pte_t pte)
{
	if (!pte_valid_cont(pte))
		return 1;

	return CONT_PTES - (((unsigned long)ptep >> 3) & (CONT_PTES - 1));
}

/*
 * The below functions constitute the public API that arm64 presents to the
 * core-mm to manipulate PTE entries within their page tables (or at least this
 * is the subset of the API that arm64 needs to implement). These public
 * versions will automatically and transparently apply the contiguous bit where
 * it makes sense to do so. Therefore any users that are contig-aware (e.g.
 * hugetlb, kernel mapper) should NOT use these APIs, but instead use the
 * private versions, which are prefixed with double underscore. All of these
 * APIs except for ptep_get_lockless() are expected to be called with the PTL
 * held. Although the contiguous bit is considered private to the
 * implementation, it is deliberately allowed to leak through the getters (e.g.
 * ptep_get()), back to core code. This is required so that pte_leaf_size() can
 * provide an accurate size for perf_get_pgtable_size(). But this leakage means
 * its possible a pte will be passed to a setter with the contiguous bit set, so
 * we explicitly clear the contiguous bit in those cases to prevent accidentally
 * setting it in the pgtable.
 */

#define ptep_get ptep_get
static inline pte_t ptep_get(pte_t *ptep)
{
	pte_t pte = __ptep_get(ptep);

	if (likely(!pte_valid_cont(pte)))
		return pte;

	return contpte_ptep_get(ptep, pte);
}

#define ptep_get_lockless ptep_get_lockless
static inline pte_t ptep_get_lockless(pte_t *ptep)
{
	pte_t pte = __ptep_get(ptep);

	if (likely(!pte_valid_cont(pte)))
		return pte;

	return contpte_ptep_get_lockless(ptep);
}

static inline void set_pte(pte_t *ptep, pte_t pte)
{
	/*
	 * We don't have the mm or vaddr so cannot unfold contig entries (since
	 * it requires tlb maintenance). set_pte() is not used in core code, so
	 * this should never even be called. Regardless do our best to service
	 * any call and emit a warning if there is any attempt to set a pte on
	 * top of an existing contig range.
	 */
	pte_t orig_pte = __ptep_get(ptep);

	WARN_ON_ONCE(pte_valid_cont(orig_pte));
	__set_pte(ptep, pte_mknoncont(pte));
}

#define set_ptes set_ptes
static __always_inline void set_ptes(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, pte_t pte, unsigned int nr)
{
	pte = pte_mknoncont(pte);

	if (likely(nr == 1)) {
		contpte_try_unfold(mm, addr, ptep, __ptep_get(ptep));
		__set_ptes(mm, addr, ptep, pte, 1);
		contpte_try_fold(mm, addr, ptep, pte);
	} else {
		contpte_set_ptes(mm, addr, ptep, pte, nr);
	}
}

static inline void pte_clear(struct mm_struct *mm,
				unsigned long addr, pte_t *ptep)
{
	contpte_try_unfold(mm, addr, ptep, __ptep_get(ptep));
	__pte_clear(mm, addr, ptep);
}

#define clear_full_ptes clear_full_ptes
static inline void clear_full_ptes(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, unsigned int nr, int full)
{
	if (likely(nr == 1)) {
		contpte_try_unfold(mm, addr, ptep, __ptep_get(ptep));
		__clear_full_ptes(mm, addr, ptep, nr, full);
	} else {
		contpte_clear_full_ptes(mm, addr, ptep, nr, full);
	}
}

#define get_and_clear_full_ptes get_and_clear_full_ptes
static inline pte_t get_and_clear_full_ptes(struct mm_struct *mm,
				unsigned long addr, pte_t *ptep,
				unsigned int nr, int full)
{
	pte_t pte;

	if (likely(nr == 1)) {
		contpte_try_unfold(mm, addr, ptep, __ptep_get(ptep));
		pte = __get_and_clear_full_ptes(mm, addr, ptep, nr, full);
	} else {
		pte = contpte_get_and_clear_full_ptes(mm, addr, ptep, nr, full);
	}

	return pte;
}

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm,
				unsigned long addr, pte_t *ptep)
{
	contpte_try_unfold(mm, addr, ptep, __ptep_get(ptep));
	return __ptep_get_and_clear(mm, addr, ptep);
}

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
static inline int ptep_test_and_clear_young(struct vm_area_struct *vma,
				unsigned long addr, pte_t *ptep)
{
	pte_t orig_pte = __ptep_get(ptep);

	if (likely(!pte_valid_cont(orig_pte)))
		return __ptep_test_and_clear_young(vma, addr, ptep);

	return contpte_ptep_test_and_clear_young(vma, addr, ptep);
}

#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
static inline int ptep_clear_flush_young(struct vm_area_struct *vma,
				unsigned long addr, pte_t *ptep)
{
	pte_t orig_pte = __ptep_get(ptep);

	if (likely(!pte_valid_cont(orig_pte)))
		return __ptep_clear_flush_young(vma, addr, ptep);

	return contpte_ptep_clear_flush_young(vma, addr, ptep);
}

#define wrprotect_ptes wrprotect_ptes
static __always_inline void wrprotect_ptes(struct mm_struct *mm,
				unsigned long addr, pte_t *ptep, unsigned int nr)
{
	if (likely(nr == 1)) {
		/*
		 * Optimization: wrprotect_ptes() can only be called for present
		 * ptes so we only need to check contig bit as condition for
		 * unfold, and we can remove the contig bit from the pte we read
		 * to avoid re-reading. This speeds up fork() which is sensitive
		 * for order-0 folios. Equivalent to contpte_try_unfold().
		 */
		pte_t orig_pte = __ptep_get(ptep);

		if (unlikely(pte_cont(orig_pte))) {
			__contpte_try_unfold(mm, addr, ptep, orig_pte);
			orig_pte = pte_mknoncont(orig_pte);
		}
		___ptep_set_wrprotect(mm, addr, ptep, orig_pte);
	} else {
		contpte_wrprotect_ptes(mm, addr, ptep, nr);
	}
}

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm,
				unsigned long addr, pte_t *ptep)
{
	wrprotect_ptes(mm, addr, ptep, 1);
}

#define __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
static inline int ptep_set_access_flags(struct vm_area_struct *vma,
				unsigned long addr, pte_t *ptep,
				pte_t entry, int dirty)
{
	pte_t orig_pte = __ptep_get(ptep);

	entry = pte_mknoncont(entry);

	if (likely(!pte_valid_cont(orig_pte)))
		return __ptep_set_access_flags(vma, addr, ptep, entry, dirty);

	return contpte_ptep_set_access_flags(vma, addr, ptep, entry, dirty);
}

#define clear_young_dirty_ptes clear_young_dirty_ptes
static inline void clear_young_dirty_ptes(struct vm_area_struct *vma,
					  unsigned long addr, pte_t *ptep,
					  unsigned int nr, cydp_t flags)
{
	if (likely(nr == 1 && !pte_cont(__ptep_get(ptep))))
		__clear_young_dirty_ptes(vma, addr, ptep, nr, flags);
	else
		contpte_clear_young_dirty_ptes(vma, addr, ptep, nr, flags);
}

#else /* CONFIG_ARM64_CONTPTE */

#define ptep_get				__ptep_get
#define set_pte					__set_pte
#define set_ptes				__set_ptes
#define pte_clear				__pte_clear
#define clear_full_ptes				__clear_full_ptes
#define get_and_clear_full_ptes			__get_and_clear_full_ptes
#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
#define ptep_get_and_clear			__ptep_get_and_clear
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define ptep_test_and_clear_young		__ptep_test_and_clear_young
#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
#define ptep_clear_flush_young			__ptep_clear_flush_young
#define __HAVE_ARCH_PTEP_SET_WRPROTECT
#define ptep_set_wrprotect			__ptep_set_wrprotect
#define wrprotect_ptes				__wrprotect_ptes
#define __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
#define ptep_set_access_flags			__ptep_set_access_flags
#define clear_young_dirty_ptes			__clear_young_dirty_ptes

#endif /* CONFIG_ARM64_CONTPTE */

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_PGTABLE_H */
