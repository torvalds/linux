/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PGTABLE_2LEVEL_H
#define _ASM_X86_PGTABLE_2LEVEL_H

#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %08lx\n", __FILE__, __LINE__, (e).pte_low)
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %08lx\n", __FILE__, __LINE__, pgd_val(e))

/*
 * Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
static inline void native_set_pte(pte_t *ptep , pte_t pte)
{
	*ptep = pte;
}

static inline void native_set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}

static inline void native_set_pud(pud_t *pudp, pud_t pud)
{
}

static inline void native_set_pte_atomic(pte_t *ptep, pte_t pte)
{
	native_set_pte(ptep, pte);
}

static inline void native_pmd_clear(pmd_t *pmdp)
{
	native_set_pmd(pmdp, __pmd(0));
}

static inline void native_pud_clear(pud_t *pudp)
{
}

static inline void native_pte_clear(struct mm_struct *mm,
				    unsigned long addr, pte_t *xp)
{
	*xp = native_make_pte(0);
}

#ifdef CONFIG_SMP
static inline pte_t native_ptep_get_and_clear(pte_t *xp)
{
	return __pte(xchg(&xp->pte_low, 0));
}
#else
#define native_ptep_get_and_clear(xp) native_local_ptep_get_and_clear(xp)
#endif

#ifdef CONFIG_SMP
static inline pmd_t native_pmdp_get_and_clear(pmd_t *xp)
{
	return __pmd(xchg((pmdval_t *)xp, 0));
}
#else
#define native_pmdp_get_and_clear(xp) native_local_pmdp_get_and_clear(xp)
#endif

#ifdef CONFIG_SMP
static inline pud_t native_pudp_get_and_clear(pud_t *xp)
{
	return __pud(xchg((pudval_t *)xp, 0));
}
#else
#define native_pudp_get_and_clear(xp) native_local_pudp_get_and_clear(xp)
#endif

/* Bit manipulation helper on pte/pgoff entry */
static inline unsigned long pte_bitop(unsigned long value, unsigned int rightshift,
				      unsigned long mask, unsigned int leftshift)
{
	return ((value >> rightshift) & mask) << leftshift;
}

/* Encode and de-code a swap entry */
#define SWP_TYPE_BITS 5
#define SWP_OFFSET_SHIFT (_PAGE_BIT_PROTNONE + 1)

#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > SWP_TYPE_BITS)

#define __swp_type(x)			(((x).val >> (_PAGE_BIT_PRESENT + 1)) \
					 & ((1U << SWP_TYPE_BITS) - 1))
#define __swp_offset(x)			((x).val >> SWP_OFFSET_SHIFT)
#define __swp_entry(type, offset)	((swp_entry_t) { \
					 ((type) << (_PAGE_BIT_PRESENT + 1)) \
					 | ((offset) << SWP_OFFSET_SHIFT) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { (pte).pte_low })
#define __swp_entry_to_pte(x)		((pte_t) { .pte = (x).val })

/* No inverted PFNs on 2 level page tables */

static inline u64 protnone_mask(u64 val)
{
	return 0;
}

static inline u64 flip_protnone_guard(u64 oldval, u64 val, u64 mask)
{
	return val;
}

static inline bool __pte_needs_invert(u64 val)
{
	return false;
}

#endif /* _ASM_X86_PGTABLE_2LEVEL_H */
