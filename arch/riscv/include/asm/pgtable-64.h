/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_PGTABLE_64_H
#define _ASM_RISCV_PGTABLE_64_H

#include <linux/bits.h>
#include <linux/const.h>
#include <asm/errata_list.h>

extern bool pgtable_l4_enabled;
extern bool pgtable_l5_enabled;

#define PGDIR_SHIFT_L3  30
#define PGDIR_SHIFT_L4  39
#define PGDIR_SHIFT_L5  48
#define PGDIR_SHIFT     (pgtable_l5_enabled ? PGDIR_SHIFT_L5 : \
		(pgtable_l4_enabled ? PGDIR_SHIFT_L4 : PGDIR_SHIFT_L3))
/* Size of region mapped by a page global directory */
#define PGDIR_SIZE      (_AC(1, UL) << PGDIR_SHIFT)
#define PGDIR_MASK      (~(PGDIR_SIZE - 1))

/* p4d is folded into pgd in case of 4-level page table */
#define P4D_SHIFT_L3   30
#define P4D_SHIFT_L4   39
#define P4D_SHIFT_L5   39
#define P4D_SHIFT      (pgtable_l5_enabled ? P4D_SHIFT_L5 : \
		(pgtable_l4_enabled ? P4D_SHIFT_L4 : P4D_SHIFT_L3))
#define P4D_SIZE       (_AC(1, UL) << P4D_SHIFT)
#define P4D_MASK       (~(P4D_SIZE - 1))

/* pud is folded into pgd in case of 3-level page table */
#define PUD_SHIFT      30
#define PUD_SIZE       (_AC(1, UL) << PUD_SHIFT)
#define PUD_MASK       (~(PUD_SIZE - 1))

#define PMD_SHIFT       21
/* Size of region mapped by a page middle directory */
#define PMD_SIZE        (_AC(1, UL) << PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE - 1))

/* Page 4th Directory entry */
typedef struct {
	unsigned long p4d;
} p4d_t;

#define p4d_val(x)	((x).p4d)
#define __p4d(x)	((p4d_t) { (x) })
#define PTRS_PER_P4D	(PAGE_SIZE / sizeof(p4d_t))

/* Page Upper Directory entry */
typedef struct {
	unsigned long pud;
} pud_t;

#define pud_val(x)      ((x).pud)
#define __pud(x)        ((pud_t) { (x) })
#define PTRS_PER_PUD    (PAGE_SIZE / sizeof(pud_t))

/* Page Middle Directory entry */
typedef struct {
	unsigned long pmd;
} pmd_t;

#define pmd_val(x)      ((x).pmd)
#define __pmd(x)        ((pmd_t) { (x) })

#define PTRS_PER_PMD    (PAGE_SIZE / sizeof(pmd_t))

#define MAX_POSSIBLE_PHYSMEM_BITS 56

/*
 * rv64 PTE format:
 * | 63 | 62 61 | 60 54 | 53  10 | 9             8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
 *   N      MT     RSV    PFN      reserved for SW   D   A   G   U   X   W   R   V
 */
#define _PAGE_PFN_MASK  GENMASK(53, 10)

/*
 * [63] Svnapot definitions:
 * 0 Svnapot disabled
 * 1 Svnapot enabled
 */
#define _PAGE_NAPOT_SHIFT	63
#define _PAGE_NAPOT		BIT(_PAGE_NAPOT_SHIFT)
/*
 * Only 64KB (order 4) napot ptes supported.
 */
#define NAPOT_CONT_ORDER_BASE 4
enum napot_cont_order {
	NAPOT_CONT64KB_ORDER = NAPOT_CONT_ORDER_BASE,
	NAPOT_ORDER_MAX,
};

#define for_each_napot_order(order)						\
	for (order = NAPOT_CONT_ORDER_BASE; order < NAPOT_ORDER_MAX; order++)
#define for_each_napot_order_rev(order)						\
	for (order = NAPOT_ORDER_MAX - 1;					\
	     order >= NAPOT_CONT_ORDER_BASE; order--)
#define napot_cont_order(val)	(__builtin_ctzl((val.pte >> _PAGE_PFN_SHIFT) << 1))

#define napot_cont_shift(order)	((order) + PAGE_SHIFT)
#define napot_cont_size(order)	BIT(napot_cont_shift(order))
#define napot_cont_mask(order)	(~(napot_cont_size(order) - 1UL))
#define napot_pte_num(order)	BIT(order)

#ifdef CONFIG_RISCV_ISA_SVNAPOT
#define HUGE_MAX_HSTATE		(2 + (NAPOT_ORDER_MAX - NAPOT_CONT_ORDER_BASE))
#else
#define HUGE_MAX_HSTATE		2
#endif

/*
 * [62:61] Svpbmt Memory Type definitions:
 *
 *  00 - PMA    Normal Cacheable, No change to implied PMA memory type
 *  01 - NC     Non-cacheable, idempotent, weakly-ordered Main Memory
 *  10 - IO     Non-cacheable, non-idempotent, strongly-ordered I/O memory
 *  11 - Rsvd   Reserved for future standard use
 */
#define _PAGE_NOCACHE_SVPBMT	(1UL << 61)
#define _PAGE_IO_SVPBMT		(1UL << 62)
#define _PAGE_MTMASK_SVPBMT	(_PAGE_NOCACHE_SVPBMT | _PAGE_IO_SVPBMT)

/*
 * [63:59] T-Head Memory Type definitions:
 * bit[63] SO - Strong Order
 * bit[62] C - Cacheable
 * bit[61] B - Bufferable
 * bit[60] SH - Shareable
 * bit[59] Sec - Trustable
 * 00110 - NC   Weakly-ordered, Non-cacheable, Bufferable, Shareable, Non-trustable
 * 01110 - PMA  Weakly-ordered, Cacheable, Bufferable, Shareable, Non-trustable
 * 10010 - IO   Strongly-ordered, Non-cacheable, Non-bufferable, Shareable, Non-trustable
 */
#define _PAGE_PMA_THEAD		((1UL << 62) | (1UL << 61) | (1UL << 60))
#define _PAGE_NOCACHE_THEAD	((1UL << 61) | (1UL << 60))
#define _PAGE_IO_THEAD		((1UL << 63) | (1UL << 60))
#define _PAGE_MTMASK_THEAD	(_PAGE_PMA_THEAD | _PAGE_IO_THEAD | (1UL << 59))

static inline u64 riscv_page_mtmask(void)
{
	u64 val;

	ALT_SVPBMT(val, _PAGE_MTMASK);
	return val;
}

static inline u64 riscv_page_nocache(void)
{
	u64 val;

	ALT_SVPBMT(val, _PAGE_NOCACHE);
	return val;
}

static inline u64 riscv_page_io(void)
{
	u64 val;

	ALT_SVPBMT(val, _PAGE_IO);
	return val;
}

#define _PAGE_NOCACHE		riscv_page_nocache()
#define _PAGE_IO		riscv_page_io()
#define _PAGE_MTMASK		riscv_page_mtmask()

/* Set of bits to preserve across pte_modify() */
#define _PAGE_CHG_MASK  (~(unsigned long)(_PAGE_PRESENT | _PAGE_READ |	\
					  _PAGE_WRITE | _PAGE_EXEC |	\
					  _PAGE_USER | _PAGE_GLOBAL |	\
					  _PAGE_MTMASK))

static inline int pud_present(pud_t pud)
{
	return (pud_val(pud) & _PAGE_PRESENT);
}

static inline int pud_none(pud_t pud)
{
	return (pud_val(pud) == 0);
}

static inline int pud_bad(pud_t pud)
{
	return !pud_present(pud) || (pud_val(pud) & _PAGE_LEAF);
}

#define pud_leaf	pud_leaf
static inline bool pud_leaf(pud_t pud)
{
	return pud_present(pud) && (pud_val(pud) & _PAGE_LEAF);
}

static inline int pud_user(pud_t pud)
{
	return pud_val(pud) & _PAGE_USER;
}

static inline void set_pud(pud_t *pudp, pud_t pud)
{
	WRITE_ONCE(*pudp, pud);
}

static inline void pud_clear(pud_t *pudp)
{
	set_pud(pudp, __pud(0));
}

static inline pud_t pfn_pud(unsigned long pfn, pgprot_t prot)
{
	return __pud((pfn << _PAGE_PFN_SHIFT) | pgprot_val(prot));
}

static inline unsigned long _pud_pfn(pud_t pud)
{
	return __page_val_to_pfn(pud_val(pud));
}

static inline pmd_t *pud_pgtable(pud_t pud)
{
	return (pmd_t *)pfn_to_virt(__page_val_to_pfn(pud_val(pud)));
}

static inline struct page *pud_page(pud_t pud)
{
	return pfn_to_page(__page_val_to_pfn(pud_val(pud)));
}

#define mm_p4d_folded  mm_p4d_folded
static inline bool mm_p4d_folded(struct mm_struct *mm)
{
	if (pgtable_l5_enabled)
		return false;

	return true;
}

#define mm_pud_folded  mm_pud_folded
static inline bool mm_pud_folded(struct mm_struct *mm)
{
	if (pgtable_l4_enabled)
		return false;

	return true;
}

#define pmd_index(addr) (((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))

static inline pmd_t pfn_pmd(unsigned long pfn, pgprot_t prot)
{
	unsigned long prot_val = pgprot_val(prot);

	ALT_THEAD_PMA(prot_val);

	return __pmd((pfn << _PAGE_PFN_SHIFT) | prot_val);
}

static inline unsigned long _pmd_pfn(pmd_t pmd)
{
	return __page_val_to_pfn(pmd_val(pmd));
}

#define pmd_ERROR(e) \
	pr_err("%s:%d: bad pmd %016lx.\n", __FILE__, __LINE__, pmd_val(e))

#define pud_ERROR(e)   \
	pr_err("%s:%d: bad pud %016lx.\n", __FILE__, __LINE__, pud_val(e))

#define p4d_ERROR(e)   \
	pr_err("%s:%d: bad p4d %016lx.\n", __FILE__, __LINE__, p4d_val(e))

static inline void set_p4d(p4d_t *p4dp, p4d_t p4d)
{
	if (pgtable_l4_enabled)
		WRITE_ONCE(*p4dp, p4d);
	else
		set_pud((pud_t *)p4dp, (pud_t){ p4d_val(p4d) });
}

static inline int p4d_none(p4d_t p4d)
{
	if (pgtable_l4_enabled)
		return (p4d_val(p4d) == 0);

	return 0;
}

static inline int p4d_present(p4d_t p4d)
{
	if (pgtable_l4_enabled)
		return (p4d_val(p4d) & _PAGE_PRESENT);

	return 1;
}

static inline int p4d_bad(p4d_t p4d)
{
	if (pgtable_l4_enabled)
		return !p4d_present(p4d);

	return 0;
}

static inline void p4d_clear(p4d_t *p4d)
{
	if (pgtable_l4_enabled)
		set_p4d(p4d, __p4d(0));
}

static inline p4d_t pfn_p4d(unsigned long pfn, pgprot_t prot)
{
	return __p4d((pfn << _PAGE_PFN_SHIFT) | pgprot_val(prot));
}

static inline unsigned long _p4d_pfn(p4d_t p4d)
{
	return __page_val_to_pfn(p4d_val(p4d));
}

static inline pud_t *p4d_pgtable(p4d_t p4d)
{
	if (pgtable_l4_enabled)
		return (pud_t *)pfn_to_virt(__page_val_to_pfn(p4d_val(p4d)));

	return (pud_t *)pud_pgtable((pud_t) { p4d_val(p4d) });
}
#define p4d_page_vaddr(p4d)	((unsigned long)p4d_pgtable(p4d))

static inline struct page *p4d_page(p4d_t p4d)
{
	return pfn_to_page(__page_val_to_pfn(p4d_val(p4d)));
}

#define pud_index(addr) (((addr) >> PUD_SHIFT) & (PTRS_PER_PUD - 1))

#define pud_offset pud_offset
pud_t *pud_offset(p4d_t *p4d, unsigned long address);

static inline void set_pgd(pgd_t *pgdp, pgd_t pgd)
{
	if (pgtable_l5_enabled)
		WRITE_ONCE(*pgdp, pgd);
	else
		set_p4d((p4d_t *)pgdp, (p4d_t){ pgd_val(pgd) });
}

static inline int pgd_none(pgd_t pgd)
{
	if (pgtable_l5_enabled)
		return (pgd_val(pgd) == 0);

	return 0;
}

static inline int pgd_present(pgd_t pgd)
{
	if (pgtable_l5_enabled)
		return (pgd_val(pgd) & _PAGE_PRESENT);

	return 1;
}

static inline int pgd_bad(pgd_t pgd)
{
	if (pgtable_l5_enabled)
		return !pgd_present(pgd);

	return 0;
}

static inline void pgd_clear(pgd_t *pgd)
{
	if (pgtable_l5_enabled)
		set_pgd(pgd, __pgd(0));
}

static inline p4d_t *pgd_pgtable(pgd_t pgd)
{
	if (pgtable_l5_enabled)
		return (p4d_t *)pfn_to_virt(__page_val_to_pfn(pgd_val(pgd)));

	return (p4d_t *)p4d_pgtable((p4d_t) { pgd_val(pgd) });
}
#define pgd_page_vaddr(pgd)	((unsigned long)pgd_pgtable(pgd))

static inline struct page *pgd_page(pgd_t pgd)
{
	return pfn_to_page(__page_val_to_pfn(pgd_val(pgd)));
}
#define pgd_page(pgd)	pgd_page(pgd)

#define p4d_index(addr) (((addr) >> P4D_SHIFT) & (PTRS_PER_P4D - 1))

#define p4d_offset p4d_offset
p4d_t *p4d_offset(pgd_t *pgd, unsigned long address);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline pte_t pmd_pte(pmd_t pmd);
static inline pte_t pud_pte(pud_t pud);
#endif

#endif /* _ASM_RISCV_PGTABLE_64_H */
