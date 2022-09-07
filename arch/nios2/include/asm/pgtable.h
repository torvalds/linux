/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 *
 * Based on asm/pgtable-32.h from mips which is:
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000, 2001 Silicon Graphics, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_PGTABLE_H
#define _ASM_NIOS2_PGTABLE_H

#include <linux/io.h>
#include <linux/bug.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#include <asm/pgtable-bits.h>
#include <asm-generic/pgtable-nopmd.h>

#define VMALLOC_START		CONFIG_NIOS2_KERNEL_MMU_REGION_BASE
#define VMALLOC_END		(CONFIG_NIOS2_KERNEL_REGION_BASE - 1)

struct mm_struct;

/* Helper macro */
#define MKP(x, w, r) __pgprot(_PAGE_PRESENT | _PAGE_CACHED |		\
				((x) ? _PAGE_EXEC : 0) |		\
				((r) ? _PAGE_READ : 0) |		\
				((w) ? _PAGE_WRITE : 0))
/*
 * These are the macros that generic kernel code needs
 * (to populate protection_map[])
 */

/* Remove W bit on private pages for COW support */

/* Shared pages can have exact HW mapping */

/* Used all over the kernel */
#define PAGE_KERNEL __pgprot(_PAGE_PRESENT | _PAGE_CACHED | _PAGE_READ | \
			     _PAGE_WRITE | _PAGE_EXEC | _PAGE_GLOBAL)

#define PAGE_SHARED __pgprot(_PAGE_PRESENT | _PAGE_CACHED | _PAGE_READ | \
			     _PAGE_WRITE | _PAGE_ACCESSED)

#define PAGE_COPY MKP(0, 0, 1)

#define PTRS_PER_PGD	(PAGE_SIZE / sizeof(pgd_t))
#define PTRS_PER_PTE	(PAGE_SIZE / sizeof(pte_t))

#define USER_PTRS_PER_PGD	\
	(CONFIG_NIOS2_KERNEL_MMU_REGION_BASE / PGDIR_SIZE)

#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)];
#define ZERO_PAGE(vaddr)	(virt_to_page(empty_zero_page))

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern pte_t invalid_pte_table[PAGE_SIZE/sizeof(pte_t)];

/*
 * (pmds are folded into puds so this doesn't get actually called,
 * but the define is needed for a generic inline function.)
 */
static inline void set_pmd(pmd_t *pmdptr, pmd_t pmdval)
{
	*pmdptr = pmdval;
}

static inline int pte_write(pte_t pte)		\
	{ return pte_val(pte) & _PAGE_WRITE; }
static inline int pte_dirty(pte_t pte)		\
	{ return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte)		\
	{ return pte_val(pte) & _PAGE_ACCESSED; }

#define pgprot_noncached pgprot_noncached

static inline pgprot_t pgprot_noncached(pgprot_t _prot)
{
	unsigned long prot = pgprot_val(_prot);

	prot &= ~_PAGE_CACHED;

	return __pgprot(prot);
}

static inline int pte_none(pte_t pte)
{
	return !(pte_val(pte) & ~(_PAGE_GLOBAL|0xf));
}

static inline int pte_present(pte_t pte)	\
	{ return pte_val(pte) & _PAGE_PRESENT; }

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_WRITE;
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_ACCESSED;
	return pte;
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED;
	return pte;
}

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	const unsigned long mask = _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC;

	pte_val(pte) = (pte_val(pte) & ~mask) | (pgprot_val(newprot) & mask);
	return pte;
}

static inline int pmd_present(pmd_t pmd)
{
	return (pmd_val(pmd) != (unsigned long) invalid_pte_table)
			&& (pmd_val(pmd) != 0UL);
}

static inline void pmd_clear(pmd_t *pmdp)
{
	pmd_val(*pmdp) = (unsigned long) invalid_pte_table;
}

#define pte_pfn(pte)		(pte_val(pte) & 0xfffff)
#define pfn_pte(pfn, prot)	(__pte(pfn | pgprot_val(prot)))
#define pte_page(pte)		(pfn_to_page(pte_pfn(pte)))

/*
 * Store a linux PTE into the linux page table.
 */
static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
}

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pteval)
{
	unsigned long paddr = (unsigned long)page_to_virt(pte_page(pteval));

	flush_dcache_range(paddr, paddr + PAGE_SIZE);
	set_pte(ptep, pteval);
}

static inline int pmd_none(pmd_t pmd)
{
	return (pmd_val(pmd) ==
		(unsigned long) invalid_pte_table) || (pmd_val(pmd) == 0UL);
}

#define pmd_bad(pmd)	(pmd_val(pmd) & ~PAGE_MASK)

static inline void pte_clear(struct mm_struct *mm,
				unsigned long addr, pte_t *ptep)
{
	pte_t null;

	pte_val(null) = (addr >> PAGE_SHIFT) & 0xf;

	set_pte_at(mm, addr, ptep, null);
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, prot)	(pfn_pte(page_to_pfn(page), prot))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define pmd_phys(pmd)		virt_to_phys((void *)pmd_val(pmd))
#define pmd_pfn(pmd)		(pmd_phys(pmd) >> PAGE_SHIFT)
#define pmd_page(pmd)		(pfn_to_page(pmd_phys(pmd) >> PAGE_SHIFT))

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return pmd_val(pmd);
}

#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %08lx.\n", \
		__FILE__, __LINE__, pte_val(e))
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %08lx.\n", \
		__FILE__, __LINE__, pgd_val(e))

/*
 * Encode and decode a swap entry (must be !pte_none(pte) && !pte_present(pte):
 *
 * 31 30 29 28 27 26 25 24 23 22 21 20 19 18 ...  1  0
 *  0  0  0  0 type.  0  0  0  0  0  0 offset.........
 *
 * This gives us up to 2**2 = 4 swap files and 2**20 * 4K = 4G per swap file.
 *
 * Note that the offset field is always non-zero, thus !pte_none(pte) is always
 * true.
 */
#define __swp_type(swp)		(((swp).val >> 26) & 0x3)
#define __swp_offset(swp)	((swp).val & 0xfffff)
#define __swp_entry(type, off)	((swp_entry_t) { (((type) & 0x3) << 26) \
						 | ((off) & 0xfffff) })
#define __swp_entry_to_pte(swp)	((pte_t) { (swp).val })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })

#define kern_addr_valid(addr)		(1)

extern void __init paging_init(void);
extern void __init mmu_init(void);

extern void update_mmu_cache(struct vm_area_struct *vma,
			     unsigned long address, pte_t *pte);

#endif /* _ASM_NIOS2_PGTABLE_H */
