/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PGTABLE_64_H
#define _ASM_X86_PGTABLE_64_H

#include <linux/const.h>
#include <asm/pgtable_64_types.h>

#ifndef __ASSEMBLY__

/*
 * This file contains the functions and defines necessary to modify and use
 * the x86-64 page table tree.
 */
#include <asm/processor.h>
#include <linux/bitops.h>
#include <linux/threads.h>

extern p4d_t level4_kernel_pgt[512];
extern p4d_t level4_ident_pgt[512];
extern pud_t level3_kernel_pgt[512];
extern pud_t level3_ident_pgt[512];
extern pmd_t level2_kernel_pgt[512];
extern pmd_t level2_fixmap_pgt[512];
extern pmd_t level2_ident_pgt[512];
extern pte_t level1_fixmap_pgt[512];
extern pgd_t init_top_pgt[];

#define swapper_pg_dir init_top_pgt

extern void paging_init(void);
static inline void sync_initial_page_table(void) { }

#define pte_ERROR(e)					\
	pr_err("%s:%d: bad pte %p(%016lx)\n",		\
	       __FILE__, __LINE__, &(e), pte_val(e))
#define pmd_ERROR(e)					\
	pr_err("%s:%d: bad pmd %p(%016lx)\n",		\
	       __FILE__, __LINE__, &(e), pmd_val(e))
#define pud_ERROR(e)					\
	pr_err("%s:%d: bad pud %p(%016lx)\n",		\
	       __FILE__, __LINE__, &(e), pud_val(e))

#if CONFIG_PGTABLE_LEVELS >= 5
#define p4d_ERROR(e)					\
	pr_err("%s:%d: bad p4d %p(%016lx)\n",		\
	       __FILE__, __LINE__, &(e), p4d_val(e))
#endif

#define pgd_ERROR(e)					\
	pr_err("%s:%d: bad pgd %p(%016lx)\n",		\
	       __FILE__, __LINE__, &(e), pgd_val(e))

struct mm_struct;

void set_pte_vaddr_p4d(p4d_t *p4d_page, unsigned long vaddr, pte_t new_pte);
void set_pte_vaddr_pud(pud_t *pud_page, unsigned long vaddr, pte_t new_pte);

static inline void native_pte_clear(struct mm_struct *mm, unsigned long addr,
				    pte_t *ptep)
{
	*ptep = native_make_pte(0);
}

static inline void native_set_pte(pte_t *ptep, pte_t pte)
{
	*ptep = pte;
}

static inline void native_set_pte_atomic(pte_t *ptep, pte_t pte)
{
	native_set_pte(ptep, pte);
}

static inline void native_set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}

static inline void native_pmd_clear(pmd_t *pmd)
{
	native_set_pmd(pmd, native_make_pmd(0));
}

static inline pte_t native_ptep_get_and_clear(pte_t *xp)
{
#ifdef CONFIG_SMP
	return native_make_pte(xchg(&xp->pte, 0));
#else
	/* native_local_ptep_get_and_clear,
	   but duplicated because of cyclic dependency */
	pte_t ret = *xp;
	native_pte_clear(NULL, 0, xp);
	return ret;
#endif
}

static inline pmd_t native_pmdp_get_and_clear(pmd_t *xp)
{
#ifdef CONFIG_SMP
	return native_make_pmd(xchg(&xp->pmd, 0));
#else
	/* native_local_pmdp_get_and_clear,
	   but duplicated because of cyclic dependency */
	pmd_t ret = *xp;
	native_pmd_clear(xp);
	return ret;
#endif
}

static inline void native_set_pud(pud_t *pudp, pud_t pud)
{
	*pudp = pud;
}

static inline void native_pud_clear(pud_t *pud)
{
	native_set_pud(pud, native_make_pud(0));
}

static inline pud_t native_pudp_get_and_clear(pud_t *xp)
{
#ifdef CONFIG_SMP
	return native_make_pud(xchg(&xp->pud, 0));
#else
	/* native_local_pudp_get_and_clear,
	 * but duplicated because of cyclic dependency
	 */
	pud_t ret = *xp;

	native_pud_clear(xp);
	return ret;
#endif
}

#ifdef CONFIG_PAGE_TABLE_ISOLATION
/*
 * All top-level PAGE_TABLE_ISOLATION page tables are order-1 pages
 * (8k-aligned and 8k in size).  The kernel one is at the beginning 4k and
 * the user one is in the last 4k.  To switch between them, you
 * just need to flip the 12th bit in their addresses.
 */
#define PTI_PGTABLE_SWITCH_BIT	PAGE_SHIFT

/*
 * This generates better code than the inline assembly in
 * __set_bit().
 */
static inline void *ptr_set_bit(void *ptr, int bit)
{
	unsigned long __ptr = (unsigned long)ptr;

	__ptr |= BIT(bit);
	return (void *)__ptr;
}
static inline void *ptr_clear_bit(void *ptr, int bit)
{
	unsigned long __ptr = (unsigned long)ptr;

	__ptr &= ~BIT(bit);
	return (void *)__ptr;
}

static inline pgd_t *kernel_to_user_pgdp(pgd_t *pgdp)
{
	return ptr_set_bit(pgdp, PTI_PGTABLE_SWITCH_BIT);
}

static inline pgd_t *user_to_kernel_pgdp(pgd_t *pgdp)
{
	return ptr_clear_bit(pgdp, PTI_PGTABLE_SWITCH_BIT);
}

static inline p4d_t *kernel_to_user_p4dp(p4d_t *p4dp)
{
	return ptr_set_bit(p4dp, PTI_PGTABLE_SWITCH_BIT);
}

static inline p4d_t *user_to_kernel_p4dp(p4d_t *p4dp)
{
	return ptr_clear_bit(p4dp, PTI_PGTABLE_SWITCH_BIT);
}
#endif /* CONFIG_PAGE_TABLE_ISOLATION */

/*
 * Page table pages are page-aligned.  The lower half of the top
 * level is used for userspace and the top half for the kernel.
 *
 * Returns true for parts of the PGD that map userspace and
 * false for the parts that map the kernel.
 */
static inline bool pgdp_maps_userspace(void *__ptr)
{
	unsigned long ptr = (unsigned long)__ptr;

	return (ptr & ~PAGE_MASK) < (PAGE_SIZE / 2);
}

#ifdef CONFIG_PAGE_TABLE_ISOLATION
pgd_t __pti_set_user_pgd(pgd_t *pgdp, pgd_t pgd);

/*
 * Take a PGD location (pgdp) and a pgd value that needs to be set there.
 * Populates the user and returns the resulting PGD that must be set in
 * the kernel copy of the page tables.
 */
static inline pgd_t pti_set_user_pgd(pgd_t *pgdp, pgd_t pgd)
{
	if (!static_cpu_has(X86_FEATURE_PTI))
		return pgd;
	return __pti_set_user_pgd(pgdp, pgd);
}
#else
static inline pgd_t pti_set_user_pgd(pgd_t *pgdp, pgd_t pgd)
{
	return pgd;
}
#endif

static inline void native_set_p4d(p4d_t *p4dp, p4d_t p4d)
{
	pgd_t pgd;

	if (pgtable_l5_enabled || !IS_ENABLED(CONFIG_PAGE_TABLE_ISOLATION)) {
		*p4dp = p4d;
		return;
	}

	pgd = native_make_pgd(native_p4d_val(p4d));
	pgd = pti_set_user_pgd((pgd_t *)p4dp, pgd);
	*p4dp = native_make_p4d(native_pgd_val(pgd));
}

static inline void native_p4d_clear(p4d_t *p4d)
{
	native_set_p4d(p4d, native_make_p4d(0));
}

static inline void native_set_pgd(pgd_t *pgdp, pgd_t pgd)
{
	*pgdp = pti_set_user_pgd(pgdp, pgd);
}

static inline void native_pgd_clear(pgd_t *pgd)
{
	native_set_pgd(pgd, native_make_pgd(0));
}

extern void sync_global_pgds(unsigned long start, unsigned long end);

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

/*
 * Level 4 access.
 */
static inline int pgd_large(pgd_t pgd) { return 0; }
#define mk_kernel_pgd(address) __pgd((address) | _KERNPG_TABLE)

/* PUD - Level3 access */

/* PMD  - Level 2 access */

/* PTE - Level 1 access. */

/* x86-64 always has all page tables mapped. */
#define pte_offset_map(dir, address) pte_offset_kernel((dir), (address))
#define pte_unmap(pte) ((void)(pte))/* NOP */

/*
 * Encode and de-code a swap entry
 *
 * |     ...            | 11| 10|  9|8|7|6|5| 4| 3|2| 1|0| <- bit number
 * |     ...            |SW3|SW2|SW1|G|L|D|A|CD|WT|U| W|P| <- bit names
 * | OFFSET (14->63) | TYPE (9-13)  |0|0|X|X| X| X|X|SD|0| <- swp entry
 *
 * G (8) is aliased and used as a PROT_NONE indicator for
 * !present ptes.  We need to start storing swap entries above
 * there.  We also need to avoid using A and D because of an
 * erratum where they can be incorrectly set by hardware on
 * non-present PTEs.
 *
 * SD (1) in swp entry is used to store soft dirty bit, which helps us
 * remember soft dirty over page migration
 *
 * Bit 7 in swp entry should be 0 because pmd_present checks not only P,
 * but also L and G.
 */
#define SWP_TYPE_FIRST_BIT (_PAGE_BIT_PROTNONE + 1)
#define SWP_TYPE_BITS 5
/* Place the offset above the type: */
#define SWP_OFFSET_FIRST_BIT (SWP_TYPE_FIRST_BIT + SWP_TYPE_BITS)

#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > SWP_TYPE_BITS)

#define __swp_type(x)			(((x).val >> (SWP_TYPE_FIRST_BIT)) \
					 & ((1U << SWP_TYPE_BITS) - 1))
#define __swp_offset(x)			((x).val >> SWP_OFFSET_FIRST_BIT)
#define __swp_entry(type, offset)	((swp_entry_t) { \
					 ((type) << (SWP_TYPE_FIRST_BIT)) \
					 | ((offset) << SWP_OFFSET_FIRST_BIT) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val((pte)) })
#define __pmd_to_swp_entry(pmd)		((swp_entry_t) { pmd_val((pmd)) })
#define __swp_entry_to_pte(x)		((pte_t) { .pte = (x).val })
#define __swp_entry_to_pmd(x)		((pmd_t) { .pmd = (x).val })

extern int kern_addr_valid(unsigned long addr);
extern void cleanup_highmap(void);

#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN

#define pgtable_cache_init()   do { } while (0)
#define check_pgt_cache()      do { } while (0)

#define PAGE_AGP    PAGE_KERNEL_NOCACHE
#define HAVE_PAGE_AGP 1

/* fs/proc/kcore.c */
#define	kc_vaddr_to_offset(v) ((v) & __VIRTUAL_MASK)
#define	kc_offset_to_vaddr(o) ((o) | ~__VIRTUAL_MASK)

#define __HAVE_ARCH_PTE_SAME

#define vmemmap ((struct page *)VMEMMAP_START)

extern void init_extra_mapping_uc(unsigned long phys, unsigned long size);
extern void init_extra_mapping_wb(unsigned long phys, unsigned long size);

#define gup_fast_permitted gup_fast_permitted
static inline bool gup_fast_permitted(unsigned long start, int nr_pages,
		int write)
{
	unsigned long len, end;

	len = (unsigned long)nr_pages << PAGE_SHIFT;
	end = start + len;
	if (end < start)
		return false;
	if (end >> __VIRTUAL_MASK_SHIFT)
		return false;
	return true;
}

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_PGTABLE_64_H */
