/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_PGTABLE_H
#define __ASM_CSKY_PGTABLE_H

#include <asm/fixmap.h>
#include <asm/addrspace.h>
#include <abi/pgtable-bits.h>
#include <asm-generic/pgtable-nopmd.h>

#define PGDIR_SHIFT		22
#define PGDIR_SIZE		(1UL << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))

#define USER_PTRS_PER_PGD	(0x80000000UL/PGDIR_SIZE)
#define FIRST_USER_ADDRESS	0UL

#define PKMAP_BASE		(0xff800000)

#define VMALLOC_START		(0xc0008000)
#define VMALLOC_END		(PKMAP_BASE - 2*PAGE_SIZE)

/*
 * C-SKY is two-level paging structure:
 */
#define PGD_ORDER	0
#define PTE_ORDER	0

#define PTRS_PER_PGD	((PAGE_SIZE << PGD_ORDER) / sizeof(pgd_t))
#define PTRS_PER_PMD	1
#define PTRS_PER_PTE	((PAGE_SIZE << PTE_ORDER) / sizeof(pte_t))

#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, (e).pte_low)
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/* Find an entry in the third-level page table.. */
#define __pte_offset_t(address) \
	(((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir, address) \
	(pmd_page_vaddr(*(dir)) + __pte_offset_t(address))
#define pte_offset_map(dir, address) \
	((pte_t *)page_address(pmd_page(*(dir))) + __pte_offset_t(address))
#define pmd_page(pmd)	(pfn_to_page(pmd_phys(pmd) >> PAGE_SHIFT))
#define pte_clear(mm, addr, ptep)	set_pte((ptep), \
			(((unsigned int)addr&0x80000000)?__pte(1):__pte(0)))
#define pte_none(pte)	(!(pte_val(pte)&0xfffffffe))
#define pte_present(pte)	(pte_val(pte) & _PAGE_PRESENT)
#define pte_pfn(x)	((unsigned long)((x).pte_low >> PAGE_SHIFT))
#define pfn_pte(pfn, prot) __pte(((unsigned long long)(pfn) << PAGE_SHIFT) \
				| pgprot_val(prot))

#define __READABLE	(_PAGE_READ | _PAGE_VALID | _PAGE_ACCESSED)
#define __WRITEABLE	(_PAGE_WRITE | _PAGE_DIRTY | _PAGE_MODIFIED)

#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_MODIFIED | \
			 _CACHE_MASK)

#define pte_unmap(pte)	((void)(pte))

#define __swp_type(x)			(((x).val >> 4) & 0xff)
#define __swp_offset(x)			((x).val >> 12)
#define __swp_entry(type, offset)	((swp_entry_t) {((type) << 4) | \
					((offset) << 12) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

#define pte_page(x)			pfn_to_page(pte_pfn(x))
#define __mk_pte(page_nr, pgprot)	__pte(((page_nr) << PAGE_SHIFT) | \
					pgprot_val(pgprot))

/*
 * CSKY can't do page protection for execute, and considers that the same like
 * read. Also, write permissions imply read permissions. This is the closest
 * we can get by reasonable means..
 */
#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _CACHE_CACHED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
				_CACHE_CACHED)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_READ | _CACHE_CACHED)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_READ | _CACHE_CACHED)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | __READABLE | __WRITEABLE | \
				_PAGE_GLOBAL | _CACHE_CACHED)
#define PAGE_USERIO	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
				_CACHE_CACHED)

#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY
#define __P110	PAGE_COPY
#define __P111	PAGE_COPY

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED

extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)];
#define ZERO_PAGE(vaddr)	(virt_to_page(empty_zero_page))

extern void load_pgd(unsigned long pg_dir);
extern pte_t invalid_pte_table[PTRS_PER_PTE];

static inline int pte_special(pte_t pte) { return 0; }
static inline pte_t pte_mkspecial(pte_t pte) { return pte; }

static inline void set_pte(pte_t *p, pte_t pte)
{
	*p = pte;
#if defined(CONFIG_CPU_NEED_TLBSYNC)
	dcache_wb_line((u32)p);
#endif
	/* prevent out of order excution */
	smp_mb();
}
#define set_pte_at(mm, addr, ptep, pteval) set_pte(ptep, pteval)

static inline pte_t *pmd_page_vaddr(pmd_t pmd)
{
	unsigned long ptr;

	ptr = pmd_val(pmd);

	return __va(ptr);
}

#define pmd_phys(pmd) pmd_val(pmd)

static inline void set_pmd(pmd_t *p, pmd_t pmd)
{
	*p = pmd;
#if defined(CONFIG_CPU_NEED_TLBSYNC)
	dcache_wb_line((u32)p);
#endif
	/* prevent specul excute */
	smp_mb();
}


static inline int pmd_none(pmd_t pmd)
{
	return pmd_val(pmd) == __pa(invalid_pte_table);
}

#define pmd_bad(pmd)	(pmd_val(pmd) & ~PAGE_MASK)

static inline int pmd_present(pmd_t pmd)
{
	return (pmd_val(pmd) != __pa(invalid_pte_table));
}

static inline void pmd_clear(pmd_t *p)
{
	pmd_val(*p) = (__pa(invalid_pte_table));
#if defined(CONFIG_CPU_NEED_TLBSYNC)
	dcache_wb_line((u32)p);
#endif
}

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_read(pte_t pte)
{
	return pte.pte_low & _PAGE_READ;
}

static inline int pte_write(pte_t pte)
{
	return (pte).pte_low & _PAGE_WRITE;
}

static inline int pte_dirty(pte_t pte)
{
	return (pte).pte_low & _PAGE_MODIFIED;
}

static inline int pte_young(pte_t pte)
{
	return (pte).pte_low & _PAGE_ACCESSED;
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_WRITE | _PAGE_DIRTY);
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_MODIFIED|_PAGE_DIRTY);
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_ACCESSED|_PAGE_VALID);
	return pte;
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	if (pte_val(pte) & _PAGE_MODIFIED)
		pte_val(pte) |= _PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_MODIFIED;
	if (pte_val(pte) & _PAGE_WRITE)
		pte_val(pte) |= _PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED;
	if (pte_val(pte) & _PAGE_READ)
		pte_val(pte) |= _PAGE_VALID;
	return pte;
}

#define __pgd_offset(address)	pgd_index(address)
#define __pud_offset(address)	(((address) >> PUD_SHIFT) & (PTRS_PER_PUD-1))
#define __pmd_offset(address)	(((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address)	pgd_offset(&init_mm, address)

#define pgd_index(address)	((address) >> PGDIR_SHIFT)

/*
 * Macro to make mark a page protection value as "uncacheable".  Note
 * that "protection" is really a misnomer here as the protection value
 * contains the memory attribute bits, dirty bits, and various other
 * bits as well.
 */
#define pgprot_noncached pgprot_noncached

static inline pgprot_t pgprot_noncached(pgprot_t _prot)
{
	unsigned long prot = pgprot_val(_prot);

	prot = (prot & ~_CACHE_MASK) | _CACHE_UNCACHED;

	return __pgprot(prot);
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, pgprot)    pfn_pte(page_to_pfn(page), (pgprot))
static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) |
		     (pgprot_val(newprot)));
}

/* to find an entry in a page-table-directory */
static inline pgd_t *pgd_offset(struct mm_struct *mm, unsigned long address)
{
	return mm->pgd + pgd_index(address);
}

/* Find an entry in the third-level page table.. */
static inline pte_t *pte_offset(pmd_t *dir, unsigned long address)
{
	return (pte_t *) (pmd_page_vaddr(*dir)) +
		((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern void paging_init(void);

extern void show_jtlb_table(void);

void update_mmu_cache(struct vm_area_struct *vma, unsigned long address,
		      pte_t *pte);

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define kern_addr_valid(addr)	(1)

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()	do {} while (0)

#define io_remap_pfn_range(vma, vaddr, pfn, size, prot) \
	remap_pfn_range(vma, vaddr, pfn, size, prot)

#include <asm-generic/pgtable.h>

#endif /* __ASM_CSKY_PGTABLE_H */
