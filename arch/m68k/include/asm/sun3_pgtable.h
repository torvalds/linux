/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SUN3_PGTABLE_H
#define _SUN3_PGTABLE_H

#include <asm/sun3mmu.h>

#ifndef __ASSEMBLY__
#include <asm/virtconvert.h>
#include <linux/linkage.h>

/*
 * This file contains all the things which change drastically for the sun3
 * pagetable stuff, to avoid making too much of a mess of the generic m68k
 * `pgtable.h'; this should only be included from the generic file. --m
 */

/* For virtual address to physical address conversion */
#define VTOP(addr)	__pa(addr)
#define PTOV(addr)	__va(addr)


#endif	/* !__ASSEMBLY__ */

/* These need to be defined for compatibility although the sun3 doesn't use them */
#define _PAGE_NOCACHE030 0x040
#define _CACHEMASK040   (~0x060)
#define _PAGE_NOCACHE_S 0x040

/* Page protection values within PTE. */
#define SUN3_PAGE_VALID     (0x80000000)
#define SUN3_PAGE_WRITEABLE (0x40000000)
#define SUN3_PAGE_SYSTEM    (0x20000000)
#define SUN3_PAGE_NOCACHE   (0x10000000)
#define SUN3_PAGE_ACCESSED  (0x02000000)
#define SUN3_PAGE_MODIFIED  (0x01000000)


/* Externally used page protection values. */
#define _PAGE_PRESENT	(SUN3_PAGE_VALID)
#define _PAGE_ACCESSED	(SUN3_PAGE_ACCESSED)

/* Compound page protection values. */
//todo: work out which ones *should* have SUN3_PAGE_NOCACHE and fix...
// is it just PAGE_KERNEL and PAGE_SHARED?
#define PAGE_NONE	__pgprot(SUN3_PAGE_VALID \
				 | SUN3_PAGE_ACCESSED \
				 | SUN3_PAGE_NOCACHE)
#define PAGE_SHARED	__pgprot(SUN3_PAGE_VALID \
				 | SUN3_PAGE_WRITEABLE \
				 | SUN3_PAGE_ACCESSED \
				 | SUN3_PAGE_NOCACHE)
#define PAGE_COPY	__pgprot(SUN3_PAGE_VALID \
				 | SUN3_PAGE_ACCESSED \
				 | SUN3_PAGE_NOCACHE)
#define PAGE_READONLY	__pgprot(SUN3_PAGE_VALID \
				 | SUN3_PAGE_ACCESSED \
				 | SUN3_PAGE_NOCACHE)
#define PAGE_KERNEL	__pgprot(SUN3_PAGE_VALID \
				 | SUN3_PAGE_WRITEABLE \
				 | SUN3_PAGE_SYSTEM \
				 | SUN3_PAGE_NOCACHE \
				 | SUN3_PAGE_ACCESSED \
				 | SUN3_PAGE_MODIFIED)
#define PAGE_INIT	__pgprot(SUN3_PAGE_VALID \
				 | SUN3_PAGE_WRITEABLE \
				 | SUN3_PAGE_SYSTEM \
				 | SUN3_PAGE_NOCACHE)

/* Use these fake page-protections on PMDs. */
#define SUN3_PMD_VALID	(0x00000001)
#define SUN3_PMD_MASK	(0x0000003F)
#define SUN3_PMD_MAGIC	(0x0000002B)

/* We borrow bit 6 to store the exclusive marker in swap PTEs. */
#define _PAGE_SWP_EXCLUSIVE	0x040

#ifndef __ASSEMBLY__

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, pgprot) pfn_pte(page_to_pfn(page), (pgprot))

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & SUN3_PAGE_CHG_MASK) | pgprot_val(newprot);
	return pte;
}

#define pmd_set(pmdp,ptep) do {} while (0)

#define __pte_page(pte) \
(__va ((pte_val (pte) & SUN3_PAGE_PGNUM_MASK) << PAGE_SHIFT))

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return (unsigned long)__va(pmd_val(pmd) & PAGE_MASK);
}

static inline int pte_none (pte_t pte) { return !pte_val (pte); }
static inline int pte_present (pte_t pte) { return pte_val (pte) & SUN3_PAGE_VALID; }
static inline void pte_clear (struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_val (*ptep) = 0;
}

#define pte_pfn(pte)            (pte_val(pte) & SUN3_PAGE_PGNUM_MASK)
#define pfn_pte(pfn, pgprot) \
({ pte_t __pte; pte_val(__pte) = pfn | pgprot_val(pgprot); __pte; })

#define pte_page(pte)		virt_to_page(__pte_page(pte))
#define pmd_pfn(pmd)		(pmd_val(pmd) >> PAGE_SHIFT)
#define pmd_page(pmd)		virt_to_page((void *)pmd_page_vaddr(pmd))


static inline int pmd_none2 (pmd_t *pmd) { return !pmd_val (*pmd); }
#define pmd_none(pmd) pmd_none2(&(pmd))
//static inline int pmd_bad (pmd_t pmd) { return (pmd_val (pmd) & SUN3_PMD_MASK) != SUN3_PMD_MAGIC; }
static inline int pmd_bad2 (pmd_t *pmd) { return 0; }
#define pmd_bad(pmd) pmd_bad2(&(pmd))
static inline int pmd_present2 (pmd_t *pmd) { return pmd_val (*pmd) & SUN3_PMD_VALID; }
/* #define pmd_present(pmd) pmd_present2(&(pmd)) */
#define pmd_present(pmd) (!pmd_none2(&(pmd)))
static inline void pmd_clear (pmd_t *pmdp) { pmd_val (*pmdp) = 0; }


#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))


/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not...
 * [we have the full set here even if they don't change from m68k]
 */
static inline int pte_write(pte_t pte)		{ return pte_val(pte) & SUN3_PAGE_WRITEABLE; }
static inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & SUN3_PAGE_MODIFIED; }
static inline int pte_young(pte_t pte)		{ return pte_val(pte) & SUN3_PAGE_ACCESSED; }

static inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) &= ~SUN3_PAGE_WRITEABLE; return pte; }
static inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~SUN3_PAGE_MODIFIED; return pte; }
static inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~SUN3_PAGE_ACCESSED; return pte; }
static inline pte_t pte_mkwrite_novma(pte_t pte){ pte_val(pte) |= SUN3_PAGE_WRITEABLE; return pte; }
static inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= SUN3_PAGE_MODIFIED; return pte; }
static inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= SUN3_PAGE_ACCESSED; return pte; }
static inline pte_t pte_mknocache(pte_t pte)	{ pte_val(pte) |= SUN3_PAGE_NOCACHE; return pte; }
// use this version when caches work...
//static inline pte_t pte_mkcache(pte_t pte)	{ pte_val(pte) &= SUN3_PAGE_NOCACHE; return pte; }
// until then, use:
static inline pte_t pte_mkcache(pte_t pte)	{ return pte; }

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern pgd_t kernel_pg_dir[PTRS_PER_PGD];

/*
 * Encode/decode swap entries and swap PTEs. Swap PTEs are all PTEs that
 * are !pte_none() && !pte_present().
 *
 * Format of swap PTEs:
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   0 <--------------------- offset ----------------> E <- type -->
 *
 *   E is the exclusive marker that is not stored in swap entries.
 */
#define __swp_type(x)		((x).val & 0x3f)
#define __swp_offset(x)		(((x).val) >> 7)
#define __swp_entry(type, offset) ((swp_entry_t) { (((type) & 0x3f) | \
						   (((offset) << 7) & ~SUN3_PAGE_VALID)) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

static inline int pte_swp_exclusive(pte_t pte)
{
	return pte_val(pte) & _PAGE_SWP_EXCLUSIVE;
}

static inline pte_t pte_swp_mkexclusive(pte_t pte)
{
	pte_val(pte) |= _PAGE_SWP_EXCLUSIVE;
	return pte;
}

static inline pte_t pte_swp_clear_exclusive(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_SWP_EXCLUSIVE;
	return pte;
}

#endif	/* !__ASSEMBLY__ */
#endif	/* !_SUN3_PGTABLE_H */
