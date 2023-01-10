/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PGTABLE_3LEVEL_H
#define _ASM_X86_PGTABLE_3LEVEL_H

/*
 * Intel Physical Address Extension (PAE) Mode - three-level page
 * tables on PPro+ CPUs.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#define pte_ERROR(e)							\
	pr_err("%s:%d: bad pte %p(%08lx%08lx)\n",			\
	       __FILE__, __LINE__, &(e), (e).pte_high, (e).pte_low)
#define pmd_ERROR(e)							\
	pr_err("%s:%d: bad pmd %p(%016Lx)\n",				\
	       __FILE__, __LINE__, &(e), pmd_val(e))
#define pgd_ERROR(e)							\
	pr_err("%s:%d: bad pgd %p(%016Lx)\n",				\
	       __FILE__, __LINE__, &(e), pgd_val(e))

#define pxx_xchg64(_pxx, _ptr, _val) ({					\
	_pxx##val_t *_p = (_pxx##val_t *)_ptr;				\
	_pxx##val_t _o = *_p;						\
	do { } while (!try_cmpxchg64(_p, &_o, (_val)));			\
	native_make_##_pxx(_o);						\
})

/*
 * Rules for using set_pte: the pte being assigned *must* be
 * either not present or in a state where the hardware will
 * not attempt to update the pte.  In places where this is
 * not possible, use pte_get_and_clear to obtain the old pte
 * value and then use set_pte to update it.  -ben
 */
static inline void native_set_pte(pte_t *ptep, pte_t pte)
{
	WRITE_ONCE(ptep->pte_high, pte.pte_high);
	smp_wmb();
	WRITE_ONCE(ptep->pte_low, pte.pte_low);
}

static inline void native_set_pte_atomic(pte_t *ptep, pte_t pte)
{
	pxx_xchg64(pte, ptep, native_pte_val(pte));
}

static inline void native_set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	pxx_xchg64(pmd, pmdp, native_pmd_val(pmd));
}

static inline void native_set_pud(pud_t *pudp, pud_t pud)
{
#ifdef CONFIG_PAGE_TABLE_ISOLATION
	pud.p4d.pgd = pti_set_user_pgtbl(&pudp->p4d.pgd, pud.p4d.pgd);
#endif
	pxx_xchg64(pud, pudp, native_pud_val(pud));
}

/*
 * For PTEs and PDEs, we must clear the P-bit first when clearing a page table
 * entry, so clear the bottom half first and enforce ordering with a compiler
 * barrier.
 */
static inline void native_pte_clear(struct mm_struct *mm, unsigned long addr,
				    pte_t *ptep)
{
	WRITE_ONCE(ptep->pte_low, 0);
	smp_wmb();
	WRITE_ONCE(ptep->pte_high, 0);
}

static inline void native_pmd_clear(pmd_t *pmdp)
{
	WRITE_ONCE(pmdp->pmd_low, 0);
	smp_wmb();
	WRITE_ONCE(pmdp->pmd_high, 0);
}

static inline void native_pud_clear(pud_t *pudp)
{
}

static inline void pud_clear(pud_t *pudp)
{
	set_pud(pudp, __pud(0));

	/*
	 * According to Intel App note "TLBs, Paging-Structure Caches,
	 * and Their Invalidation", April 2007, document 317080-001,
	 * section 8.1: in PAE mode we explicitly have to flush the
	 * TLB via cr3 if the top-level pgd is changed...
	 *
	 * Currently all places where pud_clear() is called either have
	 * flush_tlb_mm() followed or don't need TLB flush (x86_64 code or
	 * pud_clear_bad()), so we don't need TLB flush here.
	 */
}


#ifdef CONFIG_SMP
static inline pte_t native_ptep_get_and_clear(pte_t *ptep)
{
	return pxx_xchg64(pte, ptep, 0ULL);
}

static inline pmd_t native_pmdp_get_and_clear(pmd_t *pmdp)
{
	return pxx_xchg64(pmd, pmdp, 0ULL);
}

static inline pud_t native_pudp_get_and_clear(pud_t *pudp)
{
	return pxx_xchg64(pud, pudp, 0ULL);
}
#else
#define native_ptep_get_and_clear(xp) native_local_ptep_get_and_clear(xp)
#define native_pmdp_get_and_clear(xp) native_local_pmdp_get_and_clear(xp)
#define native_pudp_get_and_clear(xp) native_local_pudp_get_and_clear(xp)
#endif

#ifndef pmdp_establish
#define pmdp_establish pmdp_establish
static inline pmd_t pmdp_establish(struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmdp, pmd_t pmd)
{
	pmd_t old;

	/*
	 * If pmd has present bit cleared we can get away without expensive
	 * cmpxchg64: we can update pmdp half-by-half without racing with
	 * anybody.
	 */
	if (!(pmd_val(pmd) & _PAGE_PRESENT)) {
		/* xchg acts as a barrier before setting of the high bits */
		old.pmd_low = xchg(&pmdp->pmd_low, pmd.pmd_low);
		old.pmd_high = READ_ONCE(pmdp->pmd_high);
		WRITE_ONCE(pmdp->pmd_high, pmd.pmd_high);

		return old;
	}

	return pxx_xchg64(pmd, pmdp, pmd.pmd);
}
#endif

/* Encode and de-code a swap entry */
#define SWP_TYPE_BITS		5

#define SWP_OFFSET_FIRST_BIT	(_PAGE_BIT_PROTNONE + 1)

/* We always extract/encode the offset by shifting it all the way up, and then down again */
#define SWP_OFFSET_SHIFT	(SWP_OFFSET_FIRST_BIT + SWP_TYPE_BITS)

#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > SWP_TYPE_BITS)
#define __swp_type(x)			(((x).val) & ((1UL << SWP_TYPE_BITS) - 1))
#define __swp_offset(x)			((x).val >> SWP_TYPE_BITS)
#define __swp_entry(type, offset)	((swp_entry_t){(type) | (offset) << SWP_TYPE_BITS})

/*
 * Normally, __swp_entry() converts from arch-independent swp_entry_t to
 * arch-dependent swp_entry_t, and __swp_entry_to_pte() just stores the result
 * to pte. But here we have 32bit swp_entry_t and 64bit pte, and need to use the
 * whole 64 bits. Thus, we shift the "real" arch-dependent conversion to
 * __swp_entry_to_pte() through the following helper macro based on 64bit
 * __swp_entry().
 */
#define __swp_pteval_entry(type, offset) ((pteval_t) { \
	(~(pteval_t)(offset) << SWP_OFFSET_SHIFT >> SWP_TYPE_BITS) \
	| ((pteval_t)(type) << (64 - SWP_TYPE_BITS)) })

#define __swp_entry_to_pte(x)	((pte_t){ .pte = \
		__swp_pteval_entry(__swp_type(x), __swp_offset(x)) })
/*
 * Analogically, __pte_to_swp_entry() doesn't just extract the arch-dependent
 * swp_entry_t, but also has to convert it from 64bit to the 32bit
 * intermediate representation, using the following macros based on 64bit
 * __swp_type() and __swp_offset().
 */
#define __pteval_swp_type(x) ((unsigned long)((x).pte >> (64 - SWP_TYPE_BITS)))
#define __pteval_swp_offset(x) ((unsigned long)(~((x).pte) << SWP_TYPE_BITS >> SWP_OFFSET_SHIFT))

#define __pte_to_swp_entry(pte)	(__swp_entry(__pteval_swp_type(pte), \
					     __pteval_swp_offset(pte)))

#include <asm/pgtable-invert.h>

#endif /* _ASM_X86_PGTABLE_3LEVEL_H */
