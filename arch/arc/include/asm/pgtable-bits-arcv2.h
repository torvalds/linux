/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

/*
 * page table flags for software walked/managed MMUv3 (ARC700) and MMUv4 (HS)
 * There correspond to the corresponding bits in the TLB
 */

#ifndef _ASM_ARC_PGTABLE_BITS_ARCV2_H
#define _ASM_ARC_PGTABLE_BITS_ARCV2_H

#ifdef CONFIG_ARC_CACHE_PAGES
#define _PAGE_CACHEABLE		(1 << 0)  /* Cached (H) */
#else
#define _PAGE_CACHEABLE		0
#endif

#define _PAGE_EXECUTE		(1 << 1)  /* User Execute  (H) */
#define _PAGE_WRITE		(1 << 2)  /* User Write    (H) */
#define _PAGE_READ		(1 << 3)  /* User Read     (H) */
#define _PAGE_ACCESSED		(1 << 4)  /* Accessed      (s) */
#define _PAGE_DIRTY		(1 << 5)  /* Modified      (s) */
#define _PAGE_SPECIAL		(1 << 6)
#define _PAGE_GLOBAL		(1 << 8)  /* ASID agnostic (H) */
#define _PAGE_PRESENT		(1 << 9)  /* PTE/TLB Valid (H) */

/* We borrow bit 5 to store the exclusive marker in swap PTEs. */
#define _PAGE_SWP_EXCLUSIVE	_PAGE_DIRTY

#ifdef CONFIG_ARC_MMU_V4
#define _PAGE_HW_SZ		(1 << 10)  /* Normal/super (H) */
#else
#define _PAGE_HW_SZ		0
#endif

/* Defaults for every user page */
#define ___DEF		(_PAGE_PRESENT | _PAGE_CACHEABLE)

/* Set of bits not changed in pte_modify */
#define _PAGE_CHG_MASK	(PAGE_MASK_PHYS | _PAGE_ACCESSED | _PAGE_DIRTY | \
							   _PAGE_SPECIAL)

/* More Abbrevaited helpers */
#define PAGE_U_NONE     __pgprot(___DEF)
#define PAGE_U_R        __pgprot(___DEF | _PAGE_READ)
#define PAGE_U_W_R      __pgprot(___DEF | _PAGE_READ | _PAGE_WRITE)
#define PAGE_U_X_R      __pgprot(___DEF | _PAGE_READ | _PAGE_EXECUTE)
#define PAGE_U_X_W_R    __pgprot(___DEF \
				| _PAGE_READ | _PAGE_WRITE | _PAGE_EXECUTE)
#define PAGE_KERNEL     __pgprot(___DEF | _PAGE_GLOBAL \
				| _PAGE_READ | _PAGE_WRITE | _PAGE_EXECUTE)

#define PAGE_SHARED	PAGE_U_W_R

#define pgprot_noncached(prot)	(__pgprot(pgprot_val(prot) & ~_PAGE_CACHEABLE))

/*
 * Mapping of vm_flags (Generic VM) to PTE flags (arch specific)
 *
 * Certain cases have 1:1 mapping
 *  e.g. __P101 means VM_READ, VM_EXEC and !VM_SHARED
 *       which directly corresponds to  PAGE_U_X_R
 *
 * Other rules which cause the divergence from 1:1 mapping
 *
 *  1. Although ARC700 can do exclusive execute/write protection (meaning R
 *     can be tracked independet of X/W unlike some other CPUs), still to
 *     keep things consistent with other archs:
 *      -Write implies Read:   W => R
 *      -Execute implies Read: X => R
 *
 *  2. Pvt Writable doesn't have Write Enabled initially: Pvt-W => !W
 *     This is to enable COW mechanism
 */
	/* xwr */
#ifndef __ASSEMBLY__

#define pte_write(pte)		(pte_val(pte) & _PAGE_WRITE)
#define pte_dirty(pte)		(pte_val(pte) & _PAGE_DIRTY)
#define pte_young(pte)		(pte_val(pte) & _PAGE_ACCESSED)
#define pte_special(pte)	(pte_val(pte) & _PAGE_SPECIAL)

#define PTE_BIT_FUNC(fn, op) \
	static inline pte_t pte_##fn(pte_t pte) { pte_val(pte) op; return pte; }

PTE_BIT_FUNC(mknotpresent,     &= ~(_PAGE_PRESENT));
PTE_BIT_FUNC(wrprotect,	&= ~(_PAGE_WRITE));
PTE_BIT_FUNC(mkwrite,	|= (_PAGE_WRITE));
PTE_BIT_FUNC(mkclean,	&= ~(_PAGE_DIRTY));
PTE_BIT_FUNC(mkdirty,	|= (_PAGE_DIRTY));
PTE_BIT_FUNC(mkold,	&= ~(_PAGE_ACCESSED));
PTE_BIT_FUNC(mkyoung,	|= (_PAGE_ACCESSED));
PTE_BIT_FUNC(mkspecial,	|= (_PAGE_SPECIAL));
PTE_BIT_FUNC(mkhuge,	|= (_PAGE_HW_SZ));

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

struct vm_fault;
void update_mmu_cache_range(struct vm_fault *vmf, struct vm_area_struct *vma,
		unsigned long address, pte_t *ptep, unsigned int nr);

#define update_mmu_cache(vma, addr, ptep) \
	update_mmu_cache_range(NULL, vma, addr, ptep, 1)

/*
 * Encode/decode swap entries and swap PTEs. Swap PTEs are all PTEs that
 * are !pte_none() && !pte_present().
 *
 * Format of swap PTEs:
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   <-------------- offset -------------> <--- zero --> E < type ->
 *
 *   E is the exclusive marker that is not stored in swap entries.
 *   The zero'ed bits include _PAGE_PRESENT.
 */
#define __swp_entry(type, off)		((swp_entry_t) \
					{ ((type) & 0x1f) | ((off) << 13) })

/* Decode a PTE containing swap "identifier "into constituents */
#define __swp_type(pte_lookalike)	(((pte_lookalike).val) & 0x1f)
#define __swp_offset(pte_lookalike)	((pte_lookalike).val >> 13)

#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

static inline int pte_swp_exclusive(pte_t pte)
{
	return pte_val(pte) & _PAGE_SWP_EXCLUSIVE;
}

PTE_BIT_FUNC(swp_mkexclusive, |= (_PAGE_SWP_EXCLUSIVE));
PTE_BIT_FUNC(swp_clear_exclusive, &= ~(_PAGE_SWP_EXCLUSIVE));

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#include <asm/hugepage.h>
#endif

#endif /* __ASSEMBLY__ */

#endif
