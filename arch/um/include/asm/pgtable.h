/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright 2003 PathScale, Inc.
 * Derived from include/asm-i386/pgtable.h
 */

#ifndef __UM_PGTABLE_H
#define __UM_PGTABLE_H

#include <asm/fixmap.h>

#define _PAGE_PRESENT	0x001
#define _PAGE_NEWPAGE	0x002
#define _PAGE_NEWPROT	0x004
#define _PAGE_RW	0x020
#define _PAGE_USER	0x040
#define _PAGE_ACCESSED	0x080
#define _PAGE_DIRTY	0x100
/* If _PAGE_PRESENT is clear, we use these: */
#define _PAGE_PROTNONE	0x010	/* if the user mapped it with PROT_NONE;
				   pte_present gives true */

#ifdef CONFIG_3_LEVEL_PGTABLES
#include <asm/pgtable-3level.h>
#else
#include <asm/pgtable-2level.h>
#endif

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/* zero page used for uninitialized stuff */
extern unsigned long *empty_zero_page;

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */

extern unsigned long end_iomem;

#define VMALLOC_OFFSET	(__va_space)
#define VMALLOC_START ((end_iomem + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define PKMAP_BASE ((FIXADDR_START - LAST_PKMAP * PAGE_SIZE) & PMD_MASK)
#define VMALLOC_END	(FIXADDR_START-2*PAGE_SIZE)
#define MODULES_VADDR	VMALLOC_START
#define MODULES_END	VMALLOC_END
#define MODULES_LEN	(MODULES_VADDR - MODULES_END)

#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)
#define __PAGE_KERNEL_EXEC                                              \
	 (_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define PAGE_NONE	__pgprot(_PAGE_PROTNONE | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define PAGE_KERNEL_EXEC	__pgprot(__PAGE_KERNEL_EXEC)

/*
 * The i386 can't do page protection for execute, and considers that the same
 * are read.
 * Also, write permissions imply read permissions. This is the closest we can
 * get..
 */
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

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
#define ZERO_PAGE(vaddr) virt_to_page(empty_zero_page)

#define pte_clear(mm,addr,xp) pte_set_val(*(xp), (phys_t) 0, __pgprot(_PAGE_NEWPAGE))

#define pmd_none(x)	(!((unsigned long)pmd_val(x) & ~_PAGE_NEWPAGE))
#define	pmd_bad(x)	((pmd_val(x) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)

#define pmd_present(x)	(pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)	do { pmd_val(*(xp)) = _PAGE_NEWPAGE; } while (0)

#define pmd_newpage(x)  (pmd_val(x) & _PAGE_NEWPAGE)
#define pmd_mkuptodate(x) (pmd_val(x) &= ~_PAGE_NEWPAGE)

#define pud_newpage(x)  (pud_val(x) & _PAGE_NEWPAGE)
#define pud_mkuptodate(x) (pud_val(x) &= ~_PAGE_NEWPAGE)

#define pmd_page(pmd) phys_to_page(pmd_val(pmd) & PAGE_MASK)

#define pte_page(x) pfn_to_page(pte_pfn(x))

#define pte_present(x)	pte_get_bits(x, (_PAGE_PRESENT | _PAGE_PROTNONE))

/*
 * =================================
 * Flags checking section.
 * =================================
 */

static inline int pte_none(pte_t pte)
{
	return pte_is_zero(pte);
}

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_read(pte_t pte)
{ 
	return((pte_get_bits(pte, _PAGE_USER)) &&
	       !(pte_get_bits(pte, _PAGE_PROTNONE)));
}

static inline int pte_exec(pte_t pte){
	return((pte_get_bits(pte, _PAGE_USER)) &&
	       !(pte_get_bits(pte, _PAGE_PROTNONE)));
}

static inline int pte_write(pte_t pte)
{
	return((pte_get_bits(pte, _PAGE_RW)) &&
	       !(pte_get_bits(pte, _PAGE_PROTNONE)));
}

static inline int pte_dirty(pte_t pte)
{
	return pte_get_bits(pte, _PAGE_DIRTY);
}

static inline int pte_young(pte_t pte)
{
	return pte_get_bits(pte, _PAGE_ACCESSED);
}

static inline int pte_newpage(pte_t pte)
{
	return pte_get_bits(pte, _PAGE_NEWPAGE);
}

static inline int pte_newprot(pte_t pte)
{ 
	return(pte_present(pte) && (pte_get_bits(pte, _PAGE_NEWPROT)));
}

static inline int pte_special(pte_t pte)
{
	return 0;
}

/*
 * =================================
 * Flags setting section.
 * =================================
 */

static inline pte_t pte_mknewprot(pte_t pte)
{
	pte_set_bits(pte, _PAGE_NEWPROT);
	return(pte);
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_clear_bits(pte, _PAGE_DIRTY);
	return(pte);
}

static inline pte_t pte_mkold(pte_t pte)	
{ 
	pte_clear_bits(pte, _PAGE_ACCESSED);
	return(pte);
}

static inline pte_t pte_wrprotect(pte_t pte)
{ 
	if (likely(pte_get_bits(pte, _PAGE_RW)))
		pte_clear_bits(pte, _PAGE_RW);
	else
		return pte;
	return(pte_mknewprot(pte)); 
}

static inline pte_t pte_mkread(pte_t pte)
{ 
	if (unlikely(pte_get_bits(pte, _PAGE_USER)))
		return pte;
	pte_set_bits(pte, _PAGE_USER);
	return(pte_mknewprot(pte)); 
}

static inline pte_t pte_mkdirty(pte_t pte)
{ 
	pte_set_bits(pte, _PAGE_DIRTY);
	return(pte);
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_set_bits(pte, _PAGE_ACCESSED);
	return(pte);
}

static inline pte_t pte_mkwrite(pte_t pte)	
{
	if (unlikely(pte_get_bits(pte,  _PAGE_RW)))
		return pte;
	pte_set_bits(pte, _PAGE_RW);
	return(pte_mknewprot(pte)); 
}

static inline pte_t pte_mkuptodate(pte_t pte)	
{
	pte_clear_bits(pte, _PAGE_NEWPAGE);
	if(pte_present(pte))
		pte_clear_bits(pte, _PAGE_NEWPROT);
	return(pte); 
}

static inline pte_t pte_mknewpage(pte_t pte)
{
	pte_set_bits(pte, _PAGE_NEWPAGE);
	return(pte);
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return(pte);
}

static inline void set_pte(pte_t *pteptr, pte_t pteval)
{
	pte_copy(*pteptr, pteval);

	/* If it's a swap entry, it needs to be marked _PAGE_NEWPAGE so
	 * fix_range knows to unmap it.  _PAGE_NEWPROT is specific to
	 * mapped pages.
	 */

	*pteptr = pte_mknewpage(*pteptr);
	if(pte_present(*pteptr)) *pteptr = pte_mknewprot(*pteptr);
}

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *pteptr, pte_t pteval)
{
	set_pte(pteptr, pteval);
}

#define __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t pte_a, pte_t pte_b)
{
	return !((pte_val(pte_a) ^ pte_val(pte_b)) & ~_PAGE_NEWPAGE);
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

#define phys_to_page(phys) pfn_to_page(phys_to_pfn(phys))
#define __virt_to_page(virt) phys_to_page(__pa(virt))
#define page_to_phys(page) pfn_to_phys(page_to_pfn(page))
#define virt_to_page(addr) __virt_to_page((const unsigned long) addr)

#define mk_pte(page, pgprot) \
	({ pte_t pte;					\
							\
	pte_set_val(pte, page_to_phys(page), (pgprot));	\
	if (pte_present(pte))				\
		pte_mknewprot(pte_mknewpage(pte));	\
	pte;})

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_set_val(pte, (pte_val(pte) & _PAGE_CHG_MASK), newprot);
	return pte; 
}

/*
 * the pgd page can be thought of an array like this: pgd_t[PTRS_PER_PGD]
 *
 * this macro returns the index of the entry in the pgd page which would
 * control the given virtual address
 */
#define pgd_index(address) (((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))

/*
 * pgd_offset() returns a (pgd_t *)
 * pgd_index() is used get the offset into the pgd page's array of pgd_t's;
 */
#define pgd_offset(mm, address) ((mm)->pgd+pgd_index(address))

/*
 * a shortcut which implies the use of the kernel's pgd, instead
 * of a process's
 */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/*
 * the pmd page can be thought of an array like this: pmd_t[PTRS_PER_PMD]
 *
 * this macro returns the index of the entry in the pmd page which would
 * control the given virtual address
 */
#define pmd_page_vaddr(pmd) ((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))
#define pmd_index(address) (((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))

#define pmd_page_vaddr(pmd) \
	((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))

/*
 * the pte page can be thought of an array like this: pte_t[PTRS_PER_PTE]
 *
 * this macro returns the index of the entry in the pte page which would
 * control the given virtual address
 */
#define pte_index(address) (((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir, address) \
	((pte_t *) pmd_page_vaddr(*(dir)) +  pte_index(address))
#define pte_offset_map(dir, address) \
	((pte_t *)page_address(pmd_page(*(dir))) + pte_index(address))
#define pte_unmap(pte) do { } while (0)

struct mm_struct;
extern pte_t *virt_to_pte(struct mm_struct *mm, unsigned long addr);

#define update_mmu_cache(vma,address,ptep) do ; while (0)

/* Encode and de-code a swap entry */
#define __swp_type(x)			(((x).val >> 5) & 0x1f)
#define __swp_offset(x)			((x).val >> 11)

#define __swp_entry(type, offset) \
	((swp_entry_t) { ((type) << 5) | ((offset) << 11) })
#define __pte_to_swp_entry(pte) \
	((swp_entry_t) { pte_val(pte_mkuptodate(pte)) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

#define kern_addr_valid(addr) (1)

#include <asm-generic/pgtable.h>

/* Clear a kernel PTE and flush it from the TLB */
#define kpte_clear_flush(ptep, vaddr)		\
do {						\
	pte_clear(&init_mm, (vaddr), (ptep));	\
	__flush_tlb_one((vaddr));		\
} while (0)

#endif
